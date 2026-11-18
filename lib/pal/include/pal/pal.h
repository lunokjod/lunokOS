// Platform abstraction layer.
//
// The kernel asks for a queue, a console, a monotonic clock and a way to reboot.
// It never talks to FreeRTOS, POSIX or Arduino directly. Each system implements
// this header once: pal_esp32 for the board, pal_host for the emulator.
#ifndef LUNOK_PAL_PAL_H
#define LUNOK_PAL_PAL_H

#include <stddef.h>
#include <stdint.h>

namespace pal {

// Event types travel through the queue as fixed-size PODs. Not one of them
// carries an owning pointer: the queue must never be a source of leaks.
enum EventType : uint16_t {
  kEventNone = 0,
  kEventConsoleRx = 1,
  kEventConsoleOwner = 2,
  kEventTimer = 3,
  kEventAlarmDue = 4,
  kEventPromiseDone = 5,
  kEventAppExit = 6,
  kEventPower = 7,
  kEventSystem = 8,
  kEventTypeCount = 9
};

struct Event {
  uint16_t type;
  uint16_t a;
  int32_t b;
};

enum LogLevel : uint8_t {
  kLogDebug = 0,
  kLogInfo = 1,
  kLogWarn = 2,
  kLogError = 3
};

class IQueue {
 public:
  virtual ~IQueue() {}
  // Never blocks for more than timeout_ms (0 = try once).
  virtual bool push(const Event& ev, uint32_t timeout_ms) = 0;
  // Blocks until an event or the timeout. timeout_ms < 0 means forever.
  virtual bool pop(Event& out, int32_t timeout_ms) = 0;
  virtual uint16_t size() const = 0;
  virtual uint16_t capacity() const = 0;
};

class IConsole {
 public:
  virtual ~IConsole() {}
  virtual size_t write(const void* data, size_t len) = 0;
  // Returns what is available right now, 0 if nothing. Never blocks.
  virtual size_t read(void* data, size_t len) = 0;
  virtual bool ansi() const = 0;
  // **Whether this console is a session that can really end** (0.158.0): a **virtual terminal** —the console of
  // the emulator, which is a program on a PC that somebody started and can hang up— is a session, and `logout` ends
  // it. **The serial port of a board is not one**: nobody can hang it up and it is the only way into a board
  // with no glass, so `logout` says the session is closed and waits for an Enter, and the console starts again
  // (docs/30, docs/50).
  virtual bool virtual_terminal() const { return false; }
  // Waits until what was written has left the port. A serial line keeps bytes in flight
  // after write() returns, so the last thing a person reads before the board stops has to
  // be pushed out first or it is a word cut in half (docs/50).
  virtual void flush() = 0;
};

struct HeapInfo {
  bool valid;             // false on platforms without a heap to report
  uint32_t free_bytes;
  uint32_t min_free_bytes;
  // **The biggest block that would fit right now**, which is not `free_bytes`: a heap with 200 KB free
  // in a thousand pieces cannot give a page of 100 KB, and the board says so with
  // `heap_caps_get_largest_free_block` (docs/75). Zero on a build that cannot ask.
  uint32_t largest_free_bytes;
};

class ISystem {
 public:
  virtual ~ISystem() {}
  virtual IQueue* queue() = 0;
  virtual IConsole* console() = 0;
  // Monotonic milliseconds. Never goes backwards, never jumps.
  virtual int64_t millis() const = 0;
  // Monotonic microseconds, for the meters: a call to a device is measured in microseconds or it is not
  // measured at all, and it is the same clock the milliseconds come from (docs/30).
  virtual int64_t micros() const = 0;
  virtual void reboot() = 0;
  // True once reboot() has been called on systems that cannot reboot in place.
  virtual bool reboot_requested() const = 0;
  // **A 32-bit number of this band**: on the board it comes from the chip —the true word of its generator while the
  // radio is up, and a mixer seeded from the SAR ADC's noise and the clock when it is not, docs/50— and on a PC from a
  // mixer the run seeded. It is what the interpreter starts its own generator with, and what an app that needs a seed
  // asks for (docs/20, docs/75).
  virtual uint32_t random() = 0;
  virtual HeapInfo heap() const = 0;
  // **How much of the task's own stack is left at its lowest point**, in bytes, or -1 on a build that
  // cannot ask. A page of controls, a font of outlines and the interpreter share one task, and a stack that
  // overflows does not say so: it corrupts a backtrace and reboots (measured on the Paper, 2/11).
  virtual int32_t stack_free() const = 0;
};

// Brings the layer up: console mode, queue allocation, reader tasks.
bool init();
void shutdown();

// Valid after init().
ISystem& system();

// The speed of the console, when the console is a wire whose speed matters. A real UART at 115200
// baud carries ten bits per byte (8N1), and a console that prints a screen instantly is faster than
// the board in a way a person notices (docs/85). Zero —the default, and what the tests want— is no
// wait at all: a file is not a wire. Only a host whose console is a terminal calls this.
void set_console_baud(uint32_t baud);

// **Whether the console is a session that can really end** (0.158.0), which is the `IConsole`'s policy and the one
// the shell asks before it closes anything: a **virtual terminal** —the console of the emulator, a program on a PC that
// somebody started and can hang up— is, and `exit`, `logout` and Ctrl+D end the run; a **serial port** is not, and
// there the session is not closed at all —the board says so and the Enter of the terminal opens a new one— (docs/30,
// docs/50, docs/85). False is the default and what a test wants: its console is a file or a pipe and its case models
// the board.
void set_virtual_terminal(bool on);
bool virtual_terminal();

// **What the heap of the emulator costs**, because on the board a block is not free: the allocator walks
// its free list before it hands one over, and moving a block that has to move is a copy of its bytes
// through the bus. `step_ns` is one node of that walk and `bytes_per_us` is what the extra memory
// moves in a microsecond (the PSRAM of the Paper is 20 MB/s); zero —the default, and what every test
// wants— is a heap that answers at once and copies for free (docs/85).
void set_heap_timing(uint32_t step_ns, uint32_t bytes_per_us);

// **What dispatching a deadline costs**, in microseconds: on the board a wake-up lands on a tick and the
// task that was waiting for it runs on the **next turn**, which is why a hundred awaits of one
// millisecond take 199 ms there and 115 ms on a PC (measured, docs/75). The emulator pays this after a wait
// that expired with nothing to read, so a promise of one millisecond costs what it costs on the glass.
// Zero —the default, and what every test wants— is a wait that is over when it is over (docs/85).
void set_dispatch_us(uint32_t us);

// **The longest a wait of the kernel's queue may block, in milliseconds.** On the board the chip's own tick
// wakes the system many times a second, and a wait that ends when the chip says so is the chip's business. On
// a PC the queue sleeps in one `select` until the console has a byte or the deadline arrives, and everything
// that is not the console —the window of the emulator and the finger on its glass— is looked at between those
// waits: a click that comes and goes inside one of them is drained in a batch with the next one, which is two
// taps poured into the panel at once and therefore one tap (found on the emulator, docs/85, docs/90). Zero —the
// default, and what every test wants— is a wait that lasts as long as it was told (docs/85).
void set_wait_slice_ms(uint32_t ms);

// **Keep the host alive while the kernel waits for a person** (0.190.3). The turn of the kernel is what looks at the window of
// the emulator and at the finger on its glass, and a page that waits for a person —the page of an app that has died, which waits
// for its `Dismiss` with no end (docs/30)— stops that turn: the window stops answering the window manager, the mouse stops
// reaching the panel and the button nobody can press is a run that never ends (found on the window: *«es veu l'error al log i
// després l'emulator es penja»*, docs/85). A host that has something to look at while it waits registers it here, and the second
// rule is the important one: **a wait of the queue that expires with nothing to read is a look at the window too**, so a wait
// inside a paint or a page is kept alive by the same function as a wait in a turn.
//
// The function answers **true when the person has asked the run to end** —the corner of the window of the emulator—, and the
// page lets go of a run that is ending instead of waiting for a Dismiss that is no longer on any screen. A board registers
// nothing: its glass is the panel and there is no window to pump (docs/50).
void set_pump(bool (*fn)(void*), void* ctx);
// Looks at the host once. False when there is nothing to look at, which is every board.
bool pump();

// **A seed for the numbers of the band, when the run wants to be told which ones** (0.174.8): zero —the default— is
// "seed me from this machine", which on a PC is the clock and the process and on a board is the hardware RNG; any other
// number makes `ISystem::random()` repeat the same sequence, which is what `--seed` of the emulator is for and what a
// case that draws a page at random needs to be reproducible (docs/20, docs/85). On a board it does nothing: the chip
// has one source of noise and a firmware that could freeze it would be a firmware that can be made predictable.
void set_random_seed(uint32_t seed);

// **What a home of the memory has left**, in bytes: the board's own RAM (`extra` false) or its extra
// memory (`extra` true). `counted` is false on a PAL that hands blocks out of the machine's heap and
// keeps no books, which is what a test wants and what an emulator without a model is (docs/85).
struct MemoryLeft {
  size_t free_bytes;
  size_t lowest_free_bytes;  // the least that was left since the ceilings were set
  bool counted;
};
MemoryLeft memory_left(bool extra);

// **The ceilings of the two homes, in bytes**: the board's own RAM and its extra memory, as the board
// really has them (docs/75). The blocks of `extra_memory()` are what is counted against them, which is
// what makes an app that asks for more than the glass has die here and not on the paper. Zero is a
// home with no ceiling, and a call starts the books again, so it goes once and before the system takes
// its first block. On a board it does nothing: the chip is the chip (docs/85).
void set_memory_bytes(uint32_t home_bytes, uint32_t extra_bytes);

// **Wait this many microseconds.** The kernel does not use it —its waits are events and alarms— and it
// is here for the one thing that has to spend time on purpose: an emulator that simulates a processor it is
// not running on, where the interpreter's instructions cost what they cost on the board (docs/85). On
// the board it is the chip's own busy wait and nothing calls it with a number.
void delay_us(uint32_t us);

// The extra memory of the board (PSRAM on the M5Paper) as a place to allocate from.
//
// It exists so the interpreter does not have to ask which board it is on: it asks for big blocks
// from here and small ones from the normal heap, and a board without extra memory takes all of them
// from the normal heap and says zero in `HeapInfo` (docs/75).

// **The floor of the board's own RAM, and the rule that keeps it** (0.151.20): a block goes to the chip's
// RAM only while what is left after it stays above this, and to the extra memory after that. The rule is here
// and not in a PAL so that the board and the emulator obey the same one, and so that a test can hold it.
//
// **Why it exists** (measured on the Paper, 8/11): the Lua machine of the reader asks for about 120 KB in
// small tables and strings, and with every block under 4 KB going home it took the pool the file system needs
// for a `FILE` and the card for a DMA buffer. `fopen` then answered `ENOMEM` with 1,7 MB of PSRAM free,
// `ls /littlefs` printed nothing and a read of the card failed with an `ESP_ERR_INVALID_CRC`, until the board
// was rebooted. `mem` says the board's RAM has **125 464 B free at rest**, so sixty-four thousand leaves the
// drivers room for the pieces they ask for in one block (docs/75).
constexpr size_t kHomeReserveBytes = 64 * 1024;

// True when the block should be taken from the extra memory first: the caller asked for it, or the board's own
// RAM would drop below its floor by giving it away. A block that asked for the extra memory and does not get it
// falls back home in every PAL, so a board with no PSRAM keeps working exactly as it did.
inline bool take_from_the_extra(size_t free_home, size_t bytes, bool prefer_extra) {
  if (prefer_extra) return true;
  return free_home < bytes + kHomeReserveBytes;
}

class IExtraMemory {
 public:
  virtual ~IExtraMemory() {}
  // A block of `bytes`, from the extra memory when `prefer_extra` and the board has it, and from the
  // normal heap otherwise. When the preferred home is full it tries the other one before saying no:
  // a block is a block and an app does not have to know where it lives. nullptr is a refusal.
  virtual void* allocate(size_t bytes, bool prefer_extra) = 0;
  // The same block, grown or shrunk. It is **moved** when its home changes —one that grows past the
  // caller's threshold goes to the extra memory and one that comes back goes home— and stays where
  // it is when it does not. On failure the block is untouched and nullptr is returned.
  virtual void* resize(void* block, size_t bytes, bool prefer_extra) = 0;
  virtual void release(void* block) = 0;
  // **La taula dels llibres d'aquesta plataforma** (0.190.0): nullptr until the first block has been asked for —the table is the
  // PAL's to give and it is given once— and nullptr for ever on a platform whose books are off. The books of `lib/pal` find their
  // way back here instead of keeping a pointer of their own (docs/75).
  virtual void* books() const { return nullptr; }
  // **I és la marca qui la demana primer** (0.190.0): a tag that arrives before the table exists would write its owner into nothing and
  // the first hundreds of blocks of the run —the VM of the launcher, which is the first thing that asks— would have no owner at all.
  // El constructor de `MemoryTag` crida aquesta porta i la taula hi és abans que la primera petició (docs/75).
  virtual void open_the_books() {}
};

// Valid before and after init(): the object is a static of the platform, because the interpreter may
// allocate before anybody brought the layer up.
IExtraMemory& extra_memory();

// **ELS LLIBRES DE LA MEMÒRIA** (0.190.0, l'amo: *«El sistema fa un seguiment de memoria que es demana i s'allibera? podríem fer una
// llista de punter, tamany i qui l'ha demanat?»*): every block that leaves the door of `IExtraMemory` is written down —the pointer,
// the bytes and **who asked**—, and `mem blocks` lists them and adds them up per owner. La pregunta no tenia resposta: `mem` deia
// els totals de cada casa i el pic de l'app del vidre, i **cap bloc sabia de qui era** (docs/75).
//
// **The books are of the system and not of the app**: a Lua machine asks for tens of thousands of blocks in a second, and what a
// person needs to know is which module is eating the memory and not which string of a table it was (mesurat a l'emulador: 24 000
// peticions de menys de 256 B en dotze segons, 478 de mitjanes i 28 de grans).
//
// **The table is small on purpose and says what it has lost**, and the big blocks have a part of it for themselves: a block of
// four kilobytes or more can always be written down while the small ones fill the rest, perquè el que s'ha de veure primer és el
// bloc que no hi cap i no la cadena que hi sobra (docs/75).

// **Who is asking.** The word is the module —the app in front is the second half of the answer and it comes from `set_app_now`—,
// and the words live in the flash like the names of the counters of the port (docs/75, docs/95).
enum MemoryOwner : uint8_t {
  kMemoryNobody = 0,
  kMemoryKernel,
  kMemoryShell,
  kMemoryUi,
  kMemorySession,
  kMemoryBook,
  kMemorySplash,
  kMemoryLua,
  kMemoryMeter,
  kMemoryNet,
  kMemoryBoard,
  kMemoryOther,
};
const char* memory_owner_word(MemoryOwner who);

// **The mark of what is being asked now**: a stack, so a module that calls another one and the other one that asks for memory ends
// with the block under the innermost mark —`pal::MemoryTag who_asks(pal::kMemoryUi, shell::app_id());` at the top of the function
// that asks— (docs/75). An object and not a call to remember and another to forget, because the way out of a function has to be the
// way out of the mark. **L'app és la del moment de la marca** i no la de cada petició: una funció que demana vint blocs ja sap de qui
// són, i preguntar-ho a cada bloc seria un `strncpy` per bloc per a la mateixa resposta.
//
// **Zero bytes estàtics**: the mark lives in the table of the books, which is the platform's —the PAL object is already in RAM—, o
// sigui que aquesta capçalera no afegeix ni un byte al Paper, que no en té ni un de lliure (docs/75).
class MemoryTag {
 public:
  MemoryTag(MemoryOwner who, const char* app = nullptr);
  ~MemoryTag();
  MemoryTag(const MemoryTag&) = delete;
  MemoryTag& operator=(const MemoryTag&) = delete;

 private:
  void* books_;
  uint8_t was_owner_;
  char was_app_[16];
};

// **One row of the listing**: the pointer the caller has, the bytes it asked for, who asked, and the app that was in front when
// it asked. The name is copied here —a row outlives the app that asked it— with the sixteen bytes a session name takes (docs/75).
struct MemoryBlock {
  const void* block = nullptr;
  uint32_t bytes = 0;
  uint8_t owner = kMemoryNobody;
  char app[16] = {};
};

// **The live blocks, the biggest first**: `cap` is the room of the caller's array and the answer is how many blocks the books
// hold, which is not how many fitted in it (docs/75).
uint32_t memory_blocks(MemoryBlock* out, uint32_t cap);

// **What each owner holds**, the biggest first: a row per module and app, because a leak is a name and a number (docs/75).
struct MemoryBill {
  uint8_t owner = kMemoryNobody;
  char app[16] = {};
  uint32_t blocks = 0;
  uint32_t bytes = 0;
};
uint32_t memory_bills(MemoryBill* out, uint32_t cap);

// **A list that is not the whole list says so**: how many asks did not fit in the table and how many rows it has, and **how many
// blocks it is holding** —the number of rows is the room and not the answer— (docs/75).
uint32_t memory_lost();
uint32_t memory_slots();
uint32_t memory_live();

// **The doors of the books, for a PAL**: `allocate`/`resize`/`release` of `IExtraMemory` call them, and nothing else does —the
// books cannot see the heap and the heap cannot book itself—. `memory_books_begin` is the table, which the PAL gives from the
// extra memory when the board has one and from home when it does not; a table of zero bytes is a build with no books, and then
// every ask is lost and `mem` says it (docs/75, docs/85). The table stays the PAL's and `IExtraMemory::books()` is how the books
// find it again, que és el que fa que `lib/pal` no tingui ni un punter estàtic.
constexpr uint32_t kMemoryBookBytes = 8 * 1024;
void memory_books_begin(void* table, size_t bytes);
void memory_book_taken(void* block, size_t bytes);
void memory_book_moved(void* was, void* block, size_t bytes);
void memory_book_released(void* block);

// **What to give up when an allocation fails** (0.179.0, and the rule said by the owner in 0.179.6: *«Només s'ha de fer
// si falla una alocació de memoria d'una app o sistema. (s'esborra i es reintenta de forma transparent per l'app)»*): the
// kernel installs a function that frees what can be made again —the session memories of the apps (docs/30)—, and **it is
// called from the one place where an ask has already failed in every home and nowhere else**: never by a threshold of free
// bytes and never before trying. What comes after it is the same ask again, so the app that asked never learns that its
// own cache was the price.
//
// **`keep` is the block the ask is about**, when it is about one —a `resize` of a block that is already there— and the
// relief must not give it back: freeing it and asking the allocator to move it afterwards is a block used after it was
// given away. `allocate` passes nullptr; `resize` passes the block.
//
// It is here and not in a PAL so that the board and the emulator obey the same rule and a test can hold it: a session
// cache is the first thing to go and the last thing anyone misses. The function is not called from inside itself, so a
// relief that frees through `release()` cannot recurse.
void set_memory_relief(void (*relief)(void* keep, size_t bytes));

}  // namespace pal

#endif  // LUNOK_PAL_PAL_H
