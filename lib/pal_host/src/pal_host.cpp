// PAL for POSIX, used by the emulator and by the native tests.
//
// There is no threading here on purpose: the emulator is single threaded, and
// "block until an event or a deadline" becomes select() on stdin.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
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

int64_t now_ns() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + static_cast<int64_t>(ts.tv_nsec);
}

void sleep_until(int64_t when_ns) {
  const int64_t wait = when_ns - now_ns();
  if (wait <= 0) return;
  struct timespec ts;
  ts.tv_sec = static_cast<time_t>(wait / 1000000000LL);
  ts.tv_nsec = static_cast<long>(wait % 1000000000LL);
  nanosleep(&ts, nullptr);
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
    const struct timespec ts = now_ts();
    seed_ = static_cast<uint32_t>(ts.tv_nsec) ^ static_cast<uint32_t>(getpid() << 8);
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

  int64_t millis() const override {
    const struct timespec ts = now_ts();
    return static_cast<int64_t>(ts.tv_sec) * 1000 + static_cast<int64_t>(ts.tv_nsec / 1000000);
  }

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
    HeapInfo info;
    info.valid = false;
    info.free_bytes = 0;
    info.min_free_bytes = 0;
    return info;
  }

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
    if (input_eof_ || len == 0) return 0;
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
    if (ready <= 0) return 0;
    if (FD_ISSET(STDIN_FILENO, &fds) == 0) return 0;
    const ssize_t n = ::read(STDIN_FILENO, data, len);
    if (n < 0) return 0;
    if (n == 0) {
      input_eof_ = true;  // the pipe is closed or the terminal went away
      return 0;
    }
    return static_cast<size_t>(n);
  }

  bool ansi() const override {
    return isatty(STDOUT_FILENO) != 0 && getenv("TERM") != nullptr;
  }

 private:
  static struct timespec now_ts() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts;
  }

  void wait_for_input(int32_t timeout_ms) {
    if (input_eof_) {
      // Nothing can arrive any more: still honour the deadline, without spinning.
      if (timeout_ms > 0) {
        struct timespec ts;
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = static_cast<long>(timeout_ms % 1000) * 1000000L;
        nanosleep(&ts, nullptr);
      }
      return;
    }
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);

    struct timeval tv;
    struct timeval* ptv = nullptr;
    if (timeout_ms >= 0) {
      tv.tv_sec = timeout_ms / 1000;
      tv.tv_usec = (timeout_ms % 1000) * 1000;
      ptv = &tv;
    }

    const int ready = select(STDIN_FILENO + 1, &fds, nullptr, nullptr, ptv);
    if (ready > 0 && FD_ISSET(STDIN_FILENO, &fds) != 0) {
      Event ev;
      ev.type = kEventConsoleRx;
      ev.a = 0;
      ev.b = 0;
      push(ev, 0);
    }
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
};

HostSystem g_system;

// The PC has no PSRAM and its heap is the process's: a block is a block and there is no second home
// to move it to. The bench says which board it is imitating in `hal_emulator` and **cannot** account
// for the extra memory it claims, which docs/85 says out loud.
class HostExtraMemory : public IExtraMemory {
 public:
  void* allocate(size_t bytes, bool prefer_extra) override {
    (void)prefer_extra;
    return (bytes == 0) ? nullptr : ::malloc(bytes);
  }
  void* resize(void* block, size_t bytes, bool prefer_extra) override {
    (void)prefer_extra;
    if (bytes == 0) {
      ::free(block);
      return nullptr;
    }
    return ::realloc(block, bytes);
  }
  void release(void* block) override { ::free(block); }
};

HostExtraMemory g_extra;

}  // namespace

bool init() { return g_system.bring_up(); }

void shutdown() { g_system.restore_terminal(); }

ISystem& system() { return g_system; }

IExtraMemory& extra_memory() { return g_extra; }

void set_console_baud(uint32_t baud) {
  g_baud = baud;
  g_wire_free_ns = 0;  // a new speed starts from a wire that is free
}

}  // namespace pal

namespace pal_host {

uint32_t console_flushes() { return g_flushes; }

}  // namespace pal_host
