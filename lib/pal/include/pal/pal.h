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
  // Waits until what was written has left the port. A serial line keeps bytes in flight
  // after write() returns, so the last thing a person reads before the board stops has to
  // be pushed out first or it is a word cut in half (docs/50).
  virtual void flush() = 0;
};

struct HeapInfo {
  bool valid;             // false on platforms without a heap to report
  uint32_t free_bytes;
  uint32_t min_free_bytes;
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
  virtual uint32_t random() = 0;
  virtual HeapInfo heap() const = 0;
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

// **What handing out a block of the extra memory costs**, in microseconds. On the board the allocator
// walks its free lists and the block is not there until it does; on the PC it is a `malloc` and the
// walk is instant, so the bench is told what to charge (docs/85). Zero —the default, and every test
// that does not say otherwise— is no wait at all. What this does **not** model is the bytes: reading
// and writing the PSRAM is CPU time against a cache, and this bench has no CPU clock.
void set_extra_memory_us(uint32_t us);

// **Wait this many microseconds.** The kernel does not use it —its waits are events and alarms— and it
// is here for the one thing that has to spend time on purpose: a bench that simulates a processor it is
// not running on, where the interpreter's instructions cost what they cost on the board (docs/85). On
// the board it is the chip's own busy wait and nothing calls it with a number.
void delay_us(uint32_t us);

// The extra memory of the board (PSRAM on the M5Paper) as a place to allocate from.
//
// It exists so the interpreter does not have to ask which board it is on: it asks for big blocks
// from here and small ones from the normal heap, and a board without extra memory takes all of them
// from the normal heap and says zero in `HeapInfo` (docs/75). Nothing here is a policy: the number
// of bytes and the choice of home come from the caller.
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
};

// Valid before and after init(): the object is a static of the platform, because the interpreter may
// allocate before anybody brought the layer up.
IExtraMemory& extra_memory();

}  // namespace pal

#endif  // LUNOK_PAL_PAL_H
