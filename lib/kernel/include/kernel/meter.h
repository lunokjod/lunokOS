// **What the system costs, counted at the door** (docs/30, docs/50).
//
// Every call that reaches a device —the glass, the card, the flash, the touch, the filesystem— is
// counted, timed and dated, and so is every entry into the interpreter. The numbers are what `stats`
// prints and what a person reads before deciding that something is slow: a count, a time and the
// moment of the last call, and never an adjective.
//
// The table is fixed and lives in RAM, in the tens of bytes per meter: an ESP32 has 320 KB and a meter
// that allocates is a meter that changes what it measures (docs/75).
#ifndef LUNOK_KERNEL_METER_H
#define LUNOK_KERNEL_METER_H

#include <stddef.h>
#include <stdint.h>

namespace kernel {
namespace meter {

// **The units of the filesystem**, because a card is not a flash: a read of 4 KB from the flash and a
// read of 4 KB from the card are not the same number, and one row for both would hide the one that
// costs (docs/30, docs/70).
enum Unit : uint8_t {
  kUnitFlash = 0,
  kUnitSd,
  kUnitNvs,
  kUnitRtc,
  kUnitFram,
  kUnitCount,
};

// What is done with a unit. A whole file read is `kOpRead` even when it opens, reads and closes: what an
// app pays is the read (docs/30).
enum Op : uint8_t { kOpRead = 0, kOpWrite, kOpList, kOpStat, kOpCount };

enum Id : uint8_t {
  kBoot = 0,
  // One row per unit and per door: `sd.read`, `flash.write`, `rtc.stat`…
  kStorageFirst = 1,
  kGlassFlush = kStorageFirst + kUnitCount * kOpCount,
  // The paints that clean: `gc16` and its family are the slow ones, and knowing how many of the paints
  // were one of them is the difference between a page that flickers and a page that does not (docs/50).
  kGlassGc16,
  kGlassIdle,
  kTouchRead,
  // **The RTC is a chip on a bus**: both the hour and the monotonic count are reads of it, and the bar
  // and the sensor modules ask for them several times a turn (docs/50).
  kClockNow,
  kClockMono,
  kClockWake,
  kKeysRead,
  // The key-value store, which is not the `/nvs` mount: one is a chip with keys and the other is files
  // kept in it (docs/70).
  kKvGet,
  kKvSet,
  // **The kernel itself and the interpreter**: the loop of turns, the time inside Lua, and the blocks
  // of the extra memory. They contain other meters —a turn contains everything— and that is why `stats`
  // prints them apart and never adds them twice (docs/30).
  kLoop,
  kLuaRun,
  kLuaOwn,
  kExtraAlloc,
  kExtraRelease,
  kPowerBattery,
  kSleep,
  kCpuSet,
  kLedShow,
  kImuRead,
  kEnvRead,
  kNetWifi,
  kNetHttp,
  kNetNtp,
  // The interpreter: **every call an app makes into the system**, counted and timed by the
  // interpreter's own hook (docs/30).
  kLuaCall,
  kCount,
};

// The id of one door of one unit, for a caller that has the unit at hand.
inline uint8_t storage_id(Op op, uint8_t unit) {
  return static_cast<uint8_t>(kStorageFirst + op * kUnitCount + unit);
}

// The clock the meters read: monotonic microseconds. A call to a device measured in milliseconds is a
// call to a device that was not measured (docs/30).
uint32_t now_us();

// Adds a stretch of time that has already happened, for a place that cannot use the scope.
void add(uint8_t id, uint32_t us);
void count(uint8_t id);

const char* name(uint8_t id);
uint32_t calls(uint8_t id);
uint64_t us(uint8_t id);
uint32_t last_ms(uint8_t id);
// **The shortest and the longest single call**, which the mean hides: one call of four milliseconds and
// a thousand of one are the same mean and two different problems (docs/30).
uint32_t min_us(uint8_t id);
uint32_t max_us(uint8_t id);
uint64_t total_us();

// **The doors, without the meters that contain other meters**: a turn of the kernel contains everything
// and the interpreter contains the calls an app makes, so adding all three would count the same
// microsecond three times (docs/30).
uint64_t door_us();

// Back to zero, all of them: the measurement of a run and not of a life (docs/95).
void reset();

// **How long the system has been up since the table was put to zero**: the wall clock the split of
// `stats` is measured against —the last boot, or the last `stats reset` (docs/30).
uint32_t uptime_ms();

// **A duration a person can read at a glance**, and the only way `stats` writes one: `845 us`,
// `12.3 ms`, `1.98 s`, `3 min 07 s`, `1 h 12 min`. A raw millisecond count of five million is a number
// that has to be divided in the head before it means anything (docs/30).
size_t human_us(char* out, size_t cap, uint64_t us);

// The table, one line per meter **this build and this board have** —a display, a card or a radio the board
// does not carry is not a row— and every one of them is printed even when it was never called: a zero is a
// measurement too, and a table that hides what was not used is a table a person has to guess about (docs/30,
// docs/50, docs/85).
void dump();

// **An app is measured on its own**: what it costs is the difference between the table when its
// interpreter started and the table now, and `stats` prints that on a line of its own (docs/30).
void note_app_start(const char* app_id);

// **Times one call.** It is a scope object and not two macros so that the early returns of a hundred
// functions cannot leave a meter open (docs/80).
class Scope {
 public:
  explicit Scope(uint8_t id);
  ~Scope();
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;

 private:
  uint8_t id_;
  uint32_t start_;
};

}  // namespace meter
}  // namespace kernel

#endif  // LUNOK_KERNEL_METER_H
