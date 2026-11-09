#include "kernel/meter.h"

#include <stdio.h>

#include "hal/hal.h"
#include "hal/log.h"
#include "pal/pal.h"

namespace kernel {
namespace meter {
namespace {

struct Entry {
  uint32_t calls;
  uint32_t last_ms;
  uint64_t us;
  // **The shortest and the longest single call**, because a mean of two milliseconds can be one call of
  // four and a thousand of one, and those are two different problems (docs/30).
  uint32_t min_us;
  uint32_t max_us;
};

Entry g_entries[kCount];

// **A row of `stats` is a thing this build and this board can do.** A display the ATOM does not have is
// not a meter at zero: it is a meter that does not exist here, and a table of rows of zeros a person has to
// guess about is worse than a shorter table. What is left is printed **every time, called or not**, because
// a zero says something too: it says that the glass, the card or the clock was not asked for anything since
// the last reset (docs/30, docs/50, docs/85).
bool unit_available(uint8_t unit) {
  const hal::Caps& caps = hal::platform().caps();
  switch (unit) {
    case kUnitFlash:
      return caps.has(hal::kCapLittlefs);
    case kUnitSd:
      return caps.has(hal::kCapSd);
    case kUnitNvs:
      return caps.has(hal::kCapNvs);
    case kUnitRtc:
      return caps.has(hal::kCapRtc);
    case kUnitFram:
      // **The FRAM is a chip of the board and not a mount of the kernel**: the board that carries it has a
      // storage behind `/fram` and the one that does not has none (docs/50).
      return hal::platform().storage("/fram") != nullptr;
    default:
      return false;
  }
}

bool available(uint8_t id) {
  if (id >= kStorageFirst && id < kGlassFlush) {
    const uint8_t door = static_cast<uint8_t>(id - kStorageFirst);
    return unit_available(static_cast<uint8_t>(door % kUnitCount));
  }
  const hal::Caps& caps = hal::platform().caps();
  switch (id) {
    case kGlassFlush:
    case kGlassGc16:
    case kGlassIdle:
      return caps.has(hal::kCapDisplay);
    case kTouchRead:
      return caps.has(hal::kCapTouch);
    case kKeysRead:
      return caps.has(hal::kCapInput);
    case kKvGet:
    case kKvSet:
      return caps.has(hal::kCapNvs);
    case kClockNow:
    case kClockMono:
      return hal::platform().clock() != nullptr;
    case kClockWake:
      return caps.has(hal::kCapRtcWake);
    case kExtraAlloc:
    case kExtraRelease:
      // **The extra memory is the PSRAM the build can map**: an ATOM and the bare ESP32 have none, and a
      // row of zero allocations there would be a row that answers a question nobody can ask (docs/75).
      return hal::platform().extra_memory().chip_bytes > 0;
    case kPowerBattery:
      return caps.has(hal::kCapBattery);
    case kSleep:
      return caps.has(hal::kCapSleepLight) || caps.has(hal::kCapSleepDeep);
    case kLedShow:
      return caps.has(hal::kCapLed);
    case kImuRead:
      return caps.has(hal::kCapImu);
    case kEnvRead:
      return caps.has(hal::kCapEnv);
    case kNetWifi:
    case kNetNtp:
      return caps.has(hal::kCapWifi);
    case kNetHttp:
      return caps.has(hal::kCapHttp);
    default:
      // The boot, the turns of the kernel, the interpreter and the processor: the build has them all.
      return true;
  }
}

// One name per meter, in the order of the enum: the names are the rows of `stats` and the only thing a
// person reads, so they say what was called and not where it is called from (docs/30).
const char* const kNames[kCount] = {
    "boot",
    // read: one per unit
    "flash.read", "sd.read", "nvs.read", "rtc.read", "fram.read",
    // write
    "flash.write", "sd.write", "nvs.write", "rtc.write", "fram.write",
    // list
    "flash.list", "sd.list", "nvs.list", "rtc.list", "fram.list",
    // stat
    "flash.stat", "sd.stat", "nvs.stat", "rtc.stat", "fram.stat",
    "glass.flush",
    "glass.gc16",
    "glass.idle",
    "touch.read",
    "clock.now",
    "clock.mono",
    "clock.wake",
    "keys.read",
    "kv.get",
    "kv.set",
    "kernel.loop",
    "lua.run",
    "lua.own",
    "psram.alloc",
    "psram.free",
    "power.battery",
    "power.sleep",
    "power.cpu",
    "led.show",
    "imu.read",
    "env.read",
    "net.wifi",
    "net.http",
    "net.ntp",
    "lua.call",
};
char g_app[32] = {0};
Entry g_app_start = {0, 0, 0, 0, 0};

// **The clock the `last` column counts from**: the moment the table was last put to zero. A board has
// been off for nobody knows how long and the emulator is a PC that has been up for days, so an absolute
// millisecond is a number nobody can read; since the zero it is a number that answers "how long ago".
uint32_t g_zero_ms = 0;
bool g_zeroed = false;

uint32_t since_zero_ms() {
  const uint32_t now = static_cast<uint32_t>(pal::system().millis());
  if (!g_zeroed) {
    g_zero_ms = now;
    g_zeroed = true;
  }
  return now - g_zero_ms;
}

}  // namespace

uint32_t uptime_ms() { return since_zero_ms(); }

namespace {

}  // namespace

uint32_t now_us() {
  const int64_t us = pal::system().micros();
  return static_cast<uint32_t>(us);
}

void add(uint8_t id, uint32_t us) {
  if (id >= kCount) return;
  Entry& entry = g_entries[id];
  ++entry.calls;
  entry.us += us;
  entry.last_ms = since_zero_ms();
  if (entry.calls == 1 || us < entry.min_us) entry.min_us = us;
  if (us > entry.max_us) entry.max_us = us;
}

void count(uint8_t id) {
  if (id >= kCount) return;
  Entry& entry = g_entries[id];
  ++entry.calls;
  entry.last_ms = since_zero_ms();
}

const char* name(uint8_t id) { return (id < kCount) ? kNames[id] : "?"; }

uint32_t calls(uint8_t id) { return (id < kCount) ? g_entries[id].calls : 0; }

uint64_t us(uint8_t id) { return (id < kCount) ? g_entries[id].us : 0; }

uint32_t last_ms(uint8_t id) { return (id < kCount) ? g_entries[id].last_ms : 0; }

uint32_t min_us(uint8_t id) { return (id < kCount) ? g_entries[id].min_us : 0; }

uint32_t max_us(uint8_t id) { return (id < kCount) ? g_entries[id].max_us : 0; }

uint64_t total_us() {
  uint64_t total = 0;
  for (uint8_t i = 0; i < kCount; ++i) total += g_entries[i].us;
  return total;
}

uint64_t door_us() {
  uint64_t total = 0;
  for (uint8_t i = 0; i < kCount; ++i) {
    // A container is not a door: the boot contains every call the system makes while it comes up, and a
    // turn contains everything there is (docs/30).
    if (i == kBoot || i == kLoop || i == kLuaRun || i == kLuaOwn || i == kLuaCall) continue;
    total += g_entries[i].us;
  }
  return total;
}

void reset() {
  for (uint8_t i = 0; i < kCount; ++i) {
    g_entries[i].calls = 0;
    g_entries[i].last_ms = 0;
    g_entries[i].us = 0;
    g_entries[i].min_us = 0;
    g_entries[i].max_us = 0;
  }
  // The `last` column starts again from here, and so does the app that was being measured.
  g_zeroed = false;
  (void)since_zero_ms();
  g_app[0] = '\0';
  g_app_start.calls = 0;
  g_app_start.us = 0;
  g_app_start.last_ms = 0;
}

size_t human_us(char* out, size_t cap, uint64_t us) {
  if (out == nullptr || cap == 0) return 0;
  // **No spaces and no long words**: `stats` has six columns and eighty columns of console, and a table
  // that wraps is a table nobody reads (docs/30).
  if (us < 1000ULL) {
    return static_cast<size_t>(snprintf(out, cap, "%luus", static_cast<unsigned long>(us)));
  }
  if (us < 1000000ULL) {
    const uint64_t tenths = us / 100;  // tenths of a millisecond, without floating point
    return static_cast<size_t>(snprintf(out, cap, "%lu.%lums", static_cast<unsigned long>(tenths / 10),
                                        static_cast<unsigned long>(tenths % 10)));
  }
  if (us < 60000000ULL) {
    const uint64_t hundredths = us / 10000;  // hundredths of a second
    return static_cast<size_t>(snprintf(out, cap, "%lu.%02lus", static_cast<unsigned long>(hundredths / 100),
                                        static_cast<unsigned long>(hundredths % 100)));
  }
  const uint64_t seconds = us / 1000000ULL;
  if (seconds < 3600ULL) {
    return static_cast<size_t>(snprintf(out, cap, "%lum%02lus", static_cast<unsigned long>(seconds / 60),
                                        static_cast<unsigned long>(seconds % 60)));
  }
  return static_cast<size_t>(snprintf(out, cap, "%luh%02lum", static_cast<unsigned long>(seconds / 3600),
                                      static_cast<unsigned long>((seconds % 3600ULL) / 60ULL)));
}

void dump() {
  pal::IConsole* console = pal::system().console();
  if (console == nullptr) return;
  char line[128];
  int used = 0;

  // **This is a table and not a line of the log**: no level and no tag, because nobody greps `stats`, a
  // person reads it. The three things that decide a cost, and **every time in the scale that reads** —
  // a row of five million milliseconds is a row that hides what it says (docs/30).
  used = snprintf(line, sizeof(line), "%-14s %6s %10s %10s %10s %10s %10s\r\n", "meter", "calls",
                  "total", "mean", "min", "max", "last");
  console->write(line, static_cast<size_t>(used));
  for (uint8_t i = 0; i < kCount; ++i) {
    if (!available(i)) continue;
    const Entry& entry = g_entries[i];
    // **A meter nobody called is a row too**, with its name and a zero, and the four time columns are a
    // dash: no call was made, so there is no shortest, no longest, no mean and no last one. Writing `0us`
    // there would say that a call took no time, which is not the same thing (docs/30).
    if (entry.calls == 0) {
      used = snprintf(line, sizeof(line), "%-14s %6lu %10s %10s %10s %10s %10s\r\n", kNames[i], 0ul,
                      "0us", "-", "-", "-", "-");
      console->write(line, static_cast<size_t>(used));
      continue;
    }
    char total[16];
    char mean[16];
    char shortest[16];
    char longest[16];
    char last[16];
    human_us(total, sizeof(total), entry.us);
    human_us(mean, sizeof(mean), entry.us / entry.calls);
    human_us(shortest, sizeof(shortest), entry.min_us);
    human_us(longest, sizeof(longest), entry.max_us);
    human_us(last, sizeof(last), static_cast<uint64_t>(entry.last_ms) * 1000ULL);
    used = snprintf(line, sizeof(line), "%-14s %6lu %10s %10s %10s %10s %10s\r\n", kNames[i],
                    static_cast<unsigned long>(entry.calls), total, mean, shortest, longest, last);
    console->write(line, static_cast<size_t>(used));
  }
  // **And what the app that is running has cost**, which is the only row that answers "who is slow":
  // the difference against the table at the moment its interpreter came up (docs/30).
  if (g_app[0] != '\0') {
    const uint32_t app_calls = g_entries[kLuaCall].calls - g_app_start.calls;
    const uint64_t app_us = g_entries[kLuaCall].us - g_app_start.us;
    char spent[16];
    human_us(spent, sizeof(spent), app_us);
    used = snprintf(line, sizeof(line), "%s: %lu calls into the system, %s in them\r\n", g_app,
                    static_cast<unsigned long>(app_calls), spent);
    console->write(line, static_cast<size_t>(used));
  }
  // **The whole accounting, and nothing counted twice.** A turn of the kernel contains everything —the
  // devices, the interpreter, the app— and the interpreter contains the calls an app makes; so the
  // three are printed apart and the doors are their own sum. `idle` is the uptime the kernel was not
  // turning, which is the only part nobody was asking anything of (docs/30).
  const uint64_t up_us = static_cast<uint64_t>(since_zero_ms()) * 1000ULL;
  const uint64_t kernel_us = g_entries[kLoop].us;
  const uint64_t idle_us = (up_us > kernel_us) ? (up_us - kernel_us) : 0;
  const uint64_t run_us = g_entries[kLuaRun].us;
  const uint64_t own_us = g_entries[kLuaOwn].us;
  const uint64_t doors = door_us();
  char up_time[16];
  char kernel_time[16];
  char idle_time[16];
  char run_time[16];
  char own_time[16];
  char door_time[16];
  human_us(up_time, sizeof(up_time), up_us);
  human_us(kernel_time, sizeof(kernel_time), kernel_us);
  human_us(idle_time, sizeof(idle_time), idle_us);
  human_us(run_time, sizeof(run_time), run_us);
  human_us(own_time, sizeof(own_time), own_us);
  human_us(door_time, sizeof(door_time), doors);
  const uint64_t share = (up_us > 0) ? (doors * 1000ULL / up_us) : 0;
  used = snprintf(line, sizeof(line), "turns %lu, kernel %s of %s up, idle %s\r\n",
                  static_cast<unsigned long>(g_entries[kLoop].calls), kernel_time, up_time, idle_time);
  console->write(line, static_cast<size_t>(used));
  used = snprintf(line, sizeof(line), "lua %s: %s the app's own code (the rest is calls into it)\r\n",
                  run_time, own_time);
  console->write(line, static_cast<size_t>(used));
  used = snprintf(line, sizeof(line), "doors %s (%lu.%lu %% of the uptime)\r\n", door_time,
                  static_cast<unsigned long>(share / 10), static_cast<unsigned long>(share % 10));
  console->write(line, static_cast<size_t>(used));
}

void note_app_start(const char* app_id) {
  size_t i = 0;
  if (app_id != nullptr) {
    for (; app_id[i] != '\0' && i + 1 < sizeof(g_app); ++i) g_app[i] = app_id[i];
  }
  g_app[i] = '\0';
  g_app_start.calls = g_entries[kLuaCall].calls;
  g_app_start.us = g_entries[kLuaCall].us;
  g_app_start.last_ms = g_entries[kLuaCall].last_ms;
}

Scope::Scope(uint8_t id) : id_(id), start_(now_us()) {}

Scope::~Scope() { add(id_, now_us() - start_); }

}  // namespace meter
}  // namespace kernel
