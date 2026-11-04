// PAL for POSIX, used by the emulator and by the native tests.
//
// There is no threading here on purpose: the emulator is single threaded, and
// "block until an event or a deadline" becomes select() on stdin.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <chrono>
#include <thread>

#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include "pal/pal.h"

namespace {

// How many times the console was flushed. It lives outside `pal` because the tests ask for
// it by name (`pal_host::console_flushes`) and the object of the PAL is private (docs/85).
uint32_t g_flushes = 0;

// The speed of the console, when it is a wire and not a file: zero is no wait at all, which is what
// a test wants, and the emulator asks for 115200 (docs/85).
uint32_t g_baud = 0;
int64_t g_wire_free_ns = 0;  // when the last byte handed over would finish leaving the UART
// **What the heap costs and what the extra memory moves**, in nanoseconds a node of the allocator's
// walk and bytes a microsecond (docs/85). Zero is the PC's heap: it answers at once, which is what every
// test wants, and the emulator sets the numbers of the board's allocator and its PSRAM.
uint32_t g_step_ns = 0;
uint32_t g_bytes_per_us = 0;
// What a wait that expired costs before its deadline is delivered, in microseconds (docs/85).
uint32_t g_dispatch_us = 0;
// The longest a wait of the queue may block: zero is a wait that lasts as long as it was told (docs/85).
uint32_t g_wait_slice_ms = 0;
uint32_t g_walk_ns = 0;  // what the walks have run up but not yet paid

// **The two homes of the board's memory, each one a heap of its own** (docs/75, docs/85). The PC has
// memory and no chip to be faithful to, so both homes start without a region —a plain heap, nothing
// counted— and the model of the board the run is emulating gives them theirs when the run begins
// (`set_memory_bytes`). 0 is the board's own RAM and 1 is its extra memory (the PSRAM of the Paper).
//
// A home is **one region of bytes cut into chunks**, and the chunks tile it: `size` is the whole chunk,
// header included, and `prev_size` is the one before it, so a release can look at both neighbours. The
// free ones are also chained by `next_free`, which is what the allocator walks, and a block that is let
// go is merged with a free neighbour —that is what keeps a heap from turning into confetti— while the
// pieces that no longer touch stay apart, which is what a real heap does and what makes a block of a
// page fail on a heap with a page free in pieces (docs/75).
struct Chunk {
  uint32_t size;       // the whole chunk, header included, a multiple of eight
  uint32_t prev_size;  // the chunk before this one, or 0 at the start of a region
  uint32_t next_free;  // the next free chunk, as an offset in the region, or kNoChunk
  uint8_t home;
  uint8_t used;
  uint16_t pad;
};
static_assert(sizeof(Chunk) == 16, "a chunk header is sixteen bytes, as the board's allocator costs one");

constexpr size_t kChunkHeader = sizeof(Chunk);
constexpr size_t kAlign = 8;
// The smallest piece worth keeping apart: a header and the alignment of a small object.
constexpr size_t kMinSplit = kChunkHeader + 32;
constexpr uint8_t kHome = 0;
constexpr uint8_t kExtra = 1;
// A block of a home that has no ceiling: it comes from the PC's heap and nothing is counted.
constexpr uint8_t kPlain = 255;
constexpr uint32_t kNoChunk = 0xFFFFFFFFu;

struct Home {
  uint8_t* base = nullptr;
  size_t size = 0;      // bytes of the region, and the ceiling while it is there
  size_t used = 0;      // the chunks that are taken, headers included
  size_t peak = 0;
  uint32_t free_head = kNoChunk;
};
Home g_home[2];

size_t round_up(size_t bytes) { return (bytes + (kAlign - 1)) & ~(kAlign - 1); }
size_t round_down(size_t bytes) { return bytes & ~(kAlign - 1); }

Chunk* chunk_at(const Home& home, size_t offset) {
  return reinterpret_cast<Chunk*>(home.base + offset);
}

size_t chunk_offset(const Home& home, const Chunk* chunk) {
  return static_cast<size_t>(reinterpret_cast<const uint8_t*>(chunk) - home.base);
}

// **The next chunk in memory**, or nullptr at the end of the region.
Chunk* chunk_after(const Home& home, const Chunk* chunk) {
  const size_t end = chunk_offset(home, chunk) + chunk->size;
  if (end >= home.size) return nullptr;
  return chunk_at(home, end);
}

// The `prev_size` of the chunk that follows is what points back, and it has to be kept true every time
// a chunk changes size: a neighbouring block that moved would otherwise merge with the wrong bytes.
void fix_prev_of_next(const Home& home, Chunk* chunk) {
  Chunk* next = chunk_after(home, chunk);
  if (next != nullptr) next->prev_size = chunk->size;
}

void free_list_remove(Home& home, const Chunk* chunk) {
  const size_t wanted = chunk_offset(home, chunk);
  uint32_t offset = home.free_head;
  uint32_t previous = kNoChunk;
  while (offset != kNoChunk) {
    Chunk* node = chunk_at(home, offset);
    if (offset == wanted) {
      if (previous == kNoChunk) {
        home.free_head = node->next_free;
      } else {
        chunk_at(home, previous)->next_free = node->next_free;
      }
      node->next_free = kNoChunk;
      return;
    }
    previous = offset;
    offset = node->next_free;
  }
}

void free_list_push(Home& home, Chunk* chunk) {
  chunk->next_free = home.free_head;
  home.free_head = static_cast<uint32_t>(chunk_offset(home, chunk));
}

size_t home_free(uint8_t home) {
  const Home& h_home = g_home[home];
  if (h_home.base == nullptr) return 0;
  return (h_home.used < h_home.size) ? (h_home.size - h_home.used) : 0;
}

size_t home_lowest(uint8_t home) {
  const Home& h_home = g_home[home];
  if (h_home.base == nullptr) return 0;
  return (h_home.peak < h_home.size) ? (h_home.size - h_home.peak) : 0;
}

// **The biggest block a home could hand out now**: the free chunks are not one heap, and a page that
// does not fit in any of them is a page that fails with room to spare (docs/75).
size_t home_largest(uint8_t home) {
  const Home& h_home = g_home[home];
  size_t largest = 0;
  uint32_t offset = h_home.free_head;
  while (offset != kNoChunk) {
    const Chunk* node = chunk_at(h_home, offset);
    const size_t payload = (node->size > kChunkHeader) ? (node->size - kChunkHeader) : 0;
    if (payload > largest) largest = payload;
    offset = node->next_free;
  }
  return largest;
}

// **What a walk costs**, in nanoseconds, saved until it is a microsecond or more: a heap that is not
// walking does not sleep, and the walks of a script add up to what they would cost on the board. The
// board's allocator is not a list of every block —a bitmap of levels finds a free chunk in a handful of
// steps—, so **a walk stops costing after sixteen nodes**: what the bench charges is what the chip's
// allocator costs and not the length of this bench's own list (docs/75, docs/85).
const size_t kMaxWalkSteps = 16;

void pay_walk(size_t steps) {
  if (g_step_ns == 0) return;
  if (steps > kMaxWalkSteps) steps = kMaxWalkSteps;
  g_walk_ns += static_cast<uint32_t>(steps) * g_step_ns;
  if (g_walk_ns >= 1000) {
    pal::delay_us(g_walk_ns / 1000);
    g_walk_ns %= 1000;
  }
}

// A block that has to move is copied through the bus, and the PSRAM moves twenty megabytes a second:
// a page of canvas that changes home costs what the board would pay for it (docs/75).
void pay_copy(size_t bytes) {
  if (g_bytes_per_us == 0 || bytes == 0) return;
  pal::delay_us(static_cast<uint32_t>((bytes + g_bytes_per_us - 1) / g_bytes_per_us));
}

int64_t now_ns() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
}

void sleep_until(int64_t when_ns) {
  const int64_t wait = when_ns - now_ns();
  if (wait <= 0) return;
  std::this_thread::sleep_for(std::chrono::nanoseconds(wait));
}

// **The wire has two directions and both cost the same**: a byte that arrives from the terminal is ten
// bits at the speed of the port, so a line that is pasted into the emulator arrives over the time it
// would take to type it and not as a block that appears at once. Zero —the default and every test— is a
// console that answers when it is read (docs/85).
void wire_receive(size_t bytes) {
  if (g_baud == 0 || bytes == 0) return;
  const int64_t ns_per_byte = 10000000000LL / static_cast<int64_t>(g_baud);
  if (ns_per_byte <= 0) return;
  sleep_until(now_ns() + static_cast<int64_t>(bytes) * ns_per_byte);
}

}  // namespace

namespace pal {
namespace {

const uint16_t kQueueLength = 16;

class HostSystem : public ISystem, public IQueue, public IConsole {
 public:
  bool bring_up() {
    if (isatty(STDIN_FILENO) != 0 && tcgetattr(STDIN_FILENO, &saved_) == 0) {
      struct termios raw = saved_;
      raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
      raw.c_cc[VMIN] = 0;
      raw.c_cc[VTIME] = 0;
      if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) raw_active_ = true;
    }
    seed_ = static_cast<uint32_t>(now_ns()) ^ static_cast<uint32_t>(getpid() << 8);
    if (seed_ == 0) seed_ = 0x9e3779b9u;
    return true;
  }

  void restore_terminal() {
    if (!raw_active_) return;
    tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
    raw_active_ = false;
  }

  // ISystem -----------------------------------------------------------------

  IQueue* queue() override { return this; }
  IConsole* console() override { return this; }

  int64_t millis() const override { return now_ns() / 1000000; }
  int64_t micros() const override { return now_ns() / 1000; }

  void reboot() override { reboot_ = true; }
  bool reboot_requested() const override { return reboot_; }

  uint32_t random() override {
    // xorshift32: deterministic when the seed is, and good enough for delays.
    seed_ ^= seed_ << 13;
    seed_ ^= seed_ >> 17;
    seed_ ^= seed_ << 5;
    return seed_;
  }

  HeapInfo heap() const override {
    // **The board's own RAM, with the board's books**: when the run gave the bench the memory of the
    // model, these are the two numbers `mem` reads on the glass —what is left and the least that was
    // left— counted on everything the system and the apps took through `pal::extra_memory()` (docs/75,
    // docs/85). A run with no ceilings says so instead of inventing a number.
    HeapInfo info;
    info.valid = (g_home[kHome].base != nullptr);
    info.free_bytes = static_cast<uint32_t>(home_free(kHome));
    info.min_free_bytes = static_cast<uint32_t>(home_lowest(kHome));
    info.largest_free_bytes = static_cast<uint32_t>(home_largest(kHome));
    return info;
  }

  // **The host has no task of its own to measure**: the process has a stack the kernel gave it and nobody
  // here knows where it started (-1 is "not measured", which is not zero, docs/75).
  int32_t stack_free() const override { return -1; }

  // IQueue ------------------------------------------------------------------

  bool push(const Event& ev, uint32_t timeout_ms) override {
    (void)timeout_ms;  // never full at the sizes we use; see capacity below
    if (count_ == kQueueLength) return false;
    ring_[tail_] = ev;
    tail_ = static_cast<uint16_t>((tail_ + 1) % kQueueLength);
    ++count_;
    return true;
  }

  bool pop(Event& out, int32_t timeout_ms) override {
    if (count_ == 0) wait_for_input(timeout_ms);
    if (count_ == 0) return false;
    out = ring_[head_];
    head_ = static_cast<uint16_t>((head_ + 1) % kQueueLength);
    --count_;
    return true;
  }

  uint16_t size() const override { return count_; }
  uint16_t capacity() const override { return kQueueLength; }

  // IConsole ----------------------------------------------------------------

  size_t write(const void* data, size_t len) override {
    // **One wire, and in the order it was written.** On the board the kernel's own lines (`printf`) and
    // the console go out of the same UART, so the order of the two is the order they were produced
    // here a `printf` sits in a buffer of its own while the console writes with `::write`, and the boot
    // lines came out after the prompt and after an app's output (measured on the bench, 1/11). What is
    // buffered is pushed out first (docs/85).
    ::fflush(stdout);
    if (g_baud == 0 || len == 0) {
      const ssize_t plain = ::write(STDOUT_FILENO, data, len);
      return (plain < 0) ? 0 : static_cast<size_t>(plain);
    }
    // The bytes go out at the speed of the wire, one slice at a time: a whole screen handed over in
    // one `write` and then a wait at the end would look like a paste, and what a person sees on the
    // glass is the characters arriving (docs/85). A byte is ten bits at 8N1, so 115200 baud is
    // 86,8 us per byte and a slice of two milliseconds is about twenty-three of them.
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    const int64_t ns_per_byte = 10000000000LL / static_cast<int64_t>(g_baud);
    uint32_t slice = static_cast<uint32_t>(2000000LL / ((ns_per_byte > 0) ? ns_per_byte : 1));
    if (slice == 0) slice = 1;
    size_t done = 0;
    while (done < len) {
      const size_t left = len - done;
      const uint32_t take = static_cast<uint32_t>((left < slice) ? left : slice);
      // The wire may still be busy with what was written before: wait for it to be free and only
      // then put this slice on, which is what the UART does when the buffer is full.
      const int64_t start = (now_ns() > g_wire_free_ns) ? now_ns() : g_wire_free_ns;
      sleep_until(start);
      const ssize_t n = ::write(STDOUT_FILENO, bytes + done, take);
      if (n <= 0) return done;
      g_wire_free_ns = start + static_cast<int64_t>(n) * ns_per_byte;
      done += static_cast<size_t>(n);
    }
    return done;
  }

  // A pipe to a person's terminal has no buffer of ours to wait for, but the count is
  // kept so a test can see that the kernel did ask before sleeping (docs/85).
  void flush() override {
    ::fflush(stdout);
    ++g_flushes;
  }

  size_t read(void* data, size_t len) override {
    if (len == 0) return 0;
    uint8_t* out = static_cast<uint8_t*>(data);
    size_t done = 0;
    // **What the queue took to tell a key from an idle stdin comes back first**: the queue may read one
    // byte to find out whether select() meant data or end of file, and the console is the owner of it
    // (docs/85).
    while (done < len && pushed_count_ > 0) {
      out[done++] = pushed_[0];
      --pushed_count_;
      memmove(pushed_, pushed_ + 1, pushed_count_);
    }
    if (done == len || input_eof_) return done;
    // A terminal in raw mode has VMIN=0: read() returns 0 when there is simply
    // no data yet, and that is NOT end of file. Ask select() first and only
    // treat a 0 that comes after "readable" as EOF. Getting this wrong declares
    // the console dead on the first empty poll and loses every later keystroke,
    // which is exactly what happened once.
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    const int ready = select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);
    if (ready <= 0) return done;
    if (FD_ISSET(STDIN_FILENO, &fds) == 0) return done;
    const ssize_t n = ::read(STDIN_FILENO, out + done, len - done);
    if (n < 0) return done;
    if (n == 0) {
      input_eof_ = true;  // the pipe is closed or the terminal went away
      return done;
    }
    // **A line pasted into the terminal arrives at the speed of the port**: the console hands it to the
    // kernel after the wire has carried it, which is what a person sees when they paste a script
    // (docs/85).
    wire_receive(static_cast<size_t>(n));
    return done + static_cast<size_t>(n);
  }

  bool ansi() const override {
    return isatty(STDOUT_FILENO) != 0 && getenv("TERM") != nullptr;
  }

 private:
  void wait_for_input(int32_t timeout_ms) {
    // **A wait is cut to the slice of the bench**, when there is one: the window of the run and the finger on
    // its glass are looked at between waits, and a wait of six hundred milliseconds would be six hundred
    // milliseconds of a mouse that is not seen (docs/85, docs/90). The deadline is not lost: it is dispatched
    // on the slice that reaches it, and the next turn asks the queue again.
    int32_t slice = timeout_ms;
    if (g_wait_slice_ms > 0 && (slice < 0 || slice > static_cast<int32_t>(g_wait_slice_ms))) {
      slice = static_cast<int32_t>(g_wait_slice_ms);
    }
    if (input_eof_) {
      // Nothing can arrive any more: still honour the deadline, without spinning.
      sleep_ms(slice);
      dispatch_deadline(slice);
      return;
    }
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);

    struct timeval tv;
    struct timeval* ptv = nullptr;
    if (slice >= 0) {
      tv.tv_sec = slice / 1000;
      tv.tv_usec = (slice % 1000) * 1000;
      ptv = &tv;
    }

    const int ready = select(STDIN_FILENO + 1, &fds, nullptr, nullptr, ptv);
    if (ready <= 0 || FD_ISSET(STDIN_FILENO, &fds) == 0) {
      dispatch_deadline(slice);
      return;
    }
    // **Readable is not data.** A /dev/null or a pipe at EOF is readable for ever, and a console event
    // for it turns `pop(timeout)` into a spin: the native suite binds stdin to /dev/null, and three UI
    // tests and a long press failed on exactly this (docs/85). The byte is kept for the console, which
    // is the one that owns stdin, and a read of zero is the end of the input for good.
    if (pushed_count_ >= sizeof(pushed_)) {
      // The console has not taken what was already put aside: it will find the rest in stdin.
      Event ev;
      ev.type = kEventConsoleRx;
      ev.a = 0;
      ev.b = 0;
      push(ev, 0);
      return;
    }
    uint8_t byte = 0;
    const ssize_t n = ::read(STDIN_FILENO, &byte, 1);
    if (n <= 0) {
      if (n == 0) input_eof_ = true;
      sleep_ms(timeout_ms);
      dispatch_deadline(timeout_ms);
      return;
    }
    // The byte crossed the wire before the kernel could read it (docs/85).
    wire_receive(1);
    pushed_[pushed_count_++] = byte;
    Event ev;
    ev.type = kEventConsoleRx;
    ev.a = 0;
    ev.b = 0;
    push(ev, 0);
  }

  static void sleep_ms(int32_t timeout_ms) {
    if (timeout_ms <= 0) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
  }

  static void sleep_us(int64_t us) {
    if (us <= 0) return;
    std::this_thread::sleep_for(std::chrono::microseconds(us));
  }

  // **A deadline is dispatched on the next turn and not the instant it expires**: the board's queue
  // wakes on a tick and the task that waited for it runs in the turn after, which is the extra
  // millisecond a promise of one costs there (measured, docs/75). Only when the run is paying the times
  // of the hardware (`--no-delay` leaves it at zero) and the wait had a deadline (docs/85).
  static void dispatch_deadline(int32_t timeout_ms) {
    if (g_dispatch_us > 0 && timeout_ms >= 0) sleep_us(static_cast<int64_t>(g_dispatch_us));
  }

  Event ring_[kQueueLength];
  uint16_t head_ = 0;
  uint16_t tail_ = 0;
  uint16_t count_ = 0;

  struct termios saved_;
  bool raw_active_ = false;
  bool input_eof_ = false;
  bool reboot_ = false;
  uint32_t seed_ = 1;
  // The byte the queue took from stdin to tell a key from an idle stdin, kept for the console.
  uint8_t pushed_[64];
  size_t pushed_count_ = 0;
};

HostSystem g_system;

// **The heap of a home, when the run gave it a ceiling**: a first fit over the free chunks, a split that
// leaves the rest for somebody else and a merge with a free neighbour on the way out. A block that does
// not fit anywhere is refused, which is what the board's allocator does; a block that does not fit but
// the heap has room for in pieces is refused too, and that is fragmentation (docs/75).
Chunk* head_of(void* block) { return static_cast<Chunk*>(block) - 1; }

class HostExtraMemory : public IExtraMemory {
 public:
  void* allocate(size_t bytes, bool prefer_extra) override {
    if (bytes == 0) return nullptr;
    const uint8_t first = prefer_extra ? kExtra : kHome;
    if (void* block = take(first, bytes)) return block;
    // **The other home before saying no**: a block is a block and the app does not have to know where
    // it lives. On the Paper this is what keeps a small font out of the PSRAM's way and a page of
    // canvas out of the chip's (docs/75).
    return take(prefer_extra ? kHome : kExtra, bytes);
  }

  void* resize(void* block, size_t bytes, bool prefer_extra) override {
    if (block == nullptr) return allocate(bytes, prefer_extra);
    if (bytes == 0) {
      release(block);
      return nullptr;
    }
    Chunk* chunk = head_of(block);
    if (chunk->home == kPlain) return plain_resize(block, bytes);
    const uint8_t want = (prefer_extra ? kExtra : kHome);
    if (chunk->home == want) {
      if (void* same = grow(block, bytes)) return same;
      // The home that was asked for is full right here: the same block in the other one, and if that one
      // has no room either, **any other free piece of its own** —a block of eighty-eight kilobytes does not
      // need the space right behind it, only somewhere (docs/75).
      if (void* moved = move_to(block, bytes, (want == kExtra) ? kHome : kExtra)) return moved;
      return move_into(block, bytes, chunk->home);
    }
    if (void* moved = move_to(block, bytes, want)) return moved;
    // **A block that cannot move stays where it is**: a resize that fails leaves the block untouched and
    // the caller decides, and the bytes it holds are not lost (docs/75).
    if (void* grown = grow(block, bytes)) return grown;
    return move_into(block, bytes, chunk->home);
  }

  void release(void* block) override {
    if (block == nullptr) return;
    Chunk* chunk = head_of(block);
    if (chunk->home == kPlain) {
      ::free(chunk);
      return;
    }
    Home& home = g_home[chunk->home];
    home.used = (home.used > chunk->size) ? (home.used - chunk->size) : 0;
    chunk->used = 0;
    // **The neighbours come back together**: a heap that let its free pieces stay apart would run out of
    // room with room to spare, and the pieces that no longer touch stay apart, which is the other half
    // of the truth (docs/75).
    Chunk* next = chunk_after(home, chunk);
    if (next != nullptr && !next->used) {
      free_list_remove(home, next);
      chunk->size += next->size;
      fix_prev_of_next(home, chunk);
    }
    if (chunk->prev_size != 0) {
      Chunk* previous = chunk_at(home, chunk_offset(home, chunk) - chunk->prev_size);
      if (!previous->used) {
        free_list_remove(home, previous);
        previous->size += chunk->size;
        fix_prev_of_next(home, previous);
        chunk = previous;
      }
    }
    free_list_push(home, chunk);
    pay_walk(1);
  }

 private:
  // A block in that home, or nullptr when no free chunk of it is big enough. The walk is the free list
  // and not every chunk, which is what the board's allocator walks (docs/85).
  static void* take(uint8_t home, size_t bytes) {
    Home& h_home = g_home[home];
    if (h_home.base == nullptr) return plain_take(bytes);
    const size_t want = round_up(bytes);
    uint32_t offset = h_home.free_head;
    uint32_t previous = kNoChunk;
    size_t steps = 0;
    while (offset != kNoChunk) {
      Chunk* node = chunk_at(h_home, offset);
      ++steps;
      if (node->size - kChunkHeader >= want) {
        if (previous == kNoChunk) {
          h_home.free_head = node->next_free;
        } else {
          chunk_at(h_home, previous)->next_free = node->next_free;
        }
        node->next_free = kNoChunk;
        carve(h_home, node, want);
        pay_walk(steps);
        return node + 1;
      }
      previous = offset;
      offset = node->next_free;
    }
    pay_walk(steps);
    return nullptr;
  }

  // The rest of a free chunk stays free if it is worth a chunk of its own; if it is not, the block keeps
  // it, because a header and a handful of bytes is not a block anybody can use (docs/85).
  static void carve(Home& home, Chunk* node, size_t want) {
    const size_t spare = node->size - kChunkHeader - want;
    if (spare >= kMinSplit) {
      node->size = static_cast<uint32_t>(kChunkHeader + want);
      Chunk* rest = chunk_at(home, chunk_offset(home, node) + node->size);
      rest->size = static_cast<uint32_t>(spare);
      rest->prev_size = node->size;
      rest->next_free = kNoChunk;
      rest->home = node->home;
      rest->used = 0;
      fix_prev_of_next(home, rest);
      free_list_push(home, rest);
    }
    node->used = 1;
    home.used += node->size;
    if (home.used > home.peak) home.peak = home.used;
  }

  // A block of a home with no ceiling: the PC's heap, and nothing counted (docs/85).
  static void* plain_take(size_t bytes) {
    Chunk* head = static_cast<Chunk*>(::malloc(bytes + kChunkHeader));
    if (head == nullptr) return nullptr;
    head->size = static_cast<uint32_t>(bytes);
    head->prev_size = 0;
    head->next_free = kNoChunk;
    head->home = kPlain;
    head->used = 1;
    return head + 1;
  }

  static void* plain_resize(void* block, size_t bytes) {
    Chunk* again = static_cast<Chunk*>(::realloc(head_of(block), bytes + kChunkHeader));
    if (again == nullptr) return nullptr;
    again->size = static_cast<uint32_t>(bytes);
    again->home = kPlain;
    return again + 1;
  }

  void* move_to(void* block, size_t bytes, uint8_t home) {
    Chunk* chunk = head_of(block);
    if (chunk->home == home) return grow(block, bytes);
    return move_into(block, bytes, home);
  }

  // **Un bloc que no creix on és pot anar a qualsevol altra taca de la seva memòria**, no només a l'altra:
  // un heap de debò no demana que el bloc tingui el lloc lliure just darrere, i el de la PSRAM d'aquest banc
  // deia que no a un bloc de 88 572 B amb **quatre megabytes lliures** perquè el veí estava ocupat i només
  // provava l'altra casa, que és la petita (trobat amb l'editor obrint un fitxer d'1,3 MB, 4/11) (docs/75).
  void* move_into(void* block, size_t bytes, uint8_t home) {
    Chunk* chunk = head_of(block);
    void* fresh = take(home, bytes);
    if (fresh == nullptr) return nullptr;
    size_t moved = chunk->size - kChunkHeader;
    if (moved > bytes) moved = bytes;
    memcpy(fresh, block, moved);
    // The bytes cross a bus: the PSRAM of the Paper moves twenty megabytes a second, and a block that
    // changes home is a copy (docs/75).
    pay_copy(moved);
    release(block);
    return fresh;
  }

  // The same block, bigger or smaller, in the home it already has. It grows into the chunk after it when
  // that one is free and big enough, and it gives the tail back when the caller asked for less; when
  // neither is possible the answer is nullptr and **the block is untouched** (docs/85).
  void* grow(void* block, size_t bytes) {
    Chunk* chunk = head_of(block);
    Home& home = g_home[chunk->home];
    const size_t want = round_up(bytes);
    const size_t capacity = chunk->size - kChunkHeader;
    size_t steps = 1;
    if (want > capacity) {
      Chunk* next = chunk_after(home, chunk);
      if (next == nullptr || next->used ||
          (chunk->size + next->size - kChunkHeader) < want) {
        pay_walk(steps);
        return nullptr;
      }
      free_list_remove(home, next);
      chunk->size += next->size;
      home.used += next->size;
      fix_prev_of_next(home, chunk);
      steps += 1;
    }
    const size_t spare = chunk->size - kChunkHeader - want;
    if (spare >= kMinSplit) {
      chunk->size = static_cast<uint32_t>(kChunkHeader + want);
      Chunk* rest = chunk_at(home, chunk_offset(home, chunk) + chunk->size);
      rest->size = static_cast<uint32_t>(spare);
      rest->prev_size = chunk->size;
      rest->next_free = kNoChunk;
      rest->home = chunk->home;
      rest->used = 0;
      fix_prev_of_next(home, rest);
      free_list_push(home, rest);
      home.used -= spare;
    }
    if (home.used > home.peak) home.peak = home.used;
    pay_walk(steps);
    return block;
  }
};

HostExtraMemory g_extra;

}  // namespace

// **The terminal goes back the way it was when the process ends, however it ends**: the raw mode is this bench's —a key
// read one at a time, without echo and without a line— and it is not the shell's. `shutdown` puts it back, but a suite
// of this machine ends in `UNITY_END` and a program that stops on a failing assertion ends in `exit`, and neither of
// them calls it: the terminal of the person who ran the tests was left with no echo until a `reset` (found the 4/11,
// docs/80, docs/85).
void the_terminal_back() { g_system.restore_terminal(); }

bool init() {
  if (!g_system.bring_up()) return false;
  static bool hooked = false;
  if (!hooked) {
    hooked = true;
    atexit(&the_terminal_back);
  }
  return true;
}

void shutdown() { g_system.restore_terminal(); }

ISystem& system() { return g_system; }

IExtraMemory& extra_memory() { return g_extra; }

MemoryLeft memory_left(bool extra) {
  const uint8_t home = extra ? kExtra : kHome;
  MemoryLeft out = {};
  out.counted = (g_home[home].base != nullptr);
  if (out.counted) {
    out.free_bytes = home_free(home);
    out.lowest_free_bytes = home_lowest(home);
  }
  return out;
}

// **The region of a home, cut into one free chunk**: the run says how big the board's two homes are and
// this is where they become a heap. Calling it again starts the books over —the old regions are given
// back— so it only makes sense before the system takes its first block (docs/85).
void set_memory_bytes(uint32_t home_bytes, uint32_t extra_bytes) {
  for (uint8_t home = 0; home < 2; ++home) {
    if (g_home[home].base != nullptr) ::free(g_home[home].base);
    g_home[home] = Home{};
  }
  const uint32_t budgets[2] = {home_bytes, extra_bytes};
  for (uint8_t home = 0; home < 2; ++home) {
    const size_t size = round_down(budgets[home]);
    if (size < kChunkHeader + kMinSplit) continue;
    uint8_t* base = static_cast<uint8_t*>(::malloc(size));
    if (base == nullptr) continue;
    Home& h_home = g_home[home];
    h_home.base = base;
    h_home.size = size;
    h_home.free_head = 0;
    Chunk* first = chunk_at(h_home, 0);
    first->size = static_cast<uint32_t>(size);
    first->prev_size = 0;
    first->next_free = kNoChunk;
    first->home = home;
    first->used = 0;
  }
  g_walk_ns = 0;
}

void set_console_baud(uint32_t baud) {
  g_baud = baud;
  g_wire_free_ns = 0;  // a new speed starts from a wire that is free
}

void set_dispatch_us(uint32_t us) { g_dispatch_us = us; }

void set_wait_slice_ms(uint32_t ms) { g_wait_slice_ms = ms; }

void set_heap_timing(uint32_t step_ns, uint32_t bytes_per_us) {
  g_step_ns = step_ns;
  g_bytes_per_us = bytes_per_us;
  g_walk_ns = 0;
}

void delay_us(uint32_t us) {
  if (us == 0) return;
  std::this_thread::sleep_for(std::chrono::microseconds(us));
}

}  // namespace pal

namespace pal_host {

uint32_t console_flushes() { return g_flushes; }

}  // namespace pal_host
