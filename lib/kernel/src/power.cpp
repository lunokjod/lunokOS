// The energy policy: the board can stop its clocks, and this is the one place that
// says when, what is said to the app first and what is read afterwards (docs/90).
#include "kernel/power.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/shell.h"
#include "kernel/log.h"
#include "luabind/luabind.h"
#include "pal/pal.h"

namespace kernel {
namespace power {
namespace {

hal::IPower* g_power = nullptr;
hal::IClock* g_clock = nullptr;
hal::IKeyValue* g_nvs = nullptr;
uint32_t g_caps = 0;
bool g_keep_awake = false;

// A sleep that an app asked for, waiting for the next turn: what is running cannot
// be closed from inside its own call (docs/30).
bool g_pending = false;
bool g_pending_from_app = false;
hal::SleepMode g_pending_mode = hal::SleepMode::kLight;
int64_t g_pending_at = 0;

// Where a sleep leaves its note. One namespace and three short keys: the app in
// front, the file it was opened with, and the hour it was told to come back.
const char kNs[] = "session";
const char kAppKey[] = "app";
const char kPathKey[] = "path";
const char kWakeKey[] = "wake_at";
const size_t kIdCap = 32;
const size_t kPathCap = 128;

// Every line this file says to a person goes out through the console and not
// through printf: `printf` is buffered and the shell is not, so a line written
// with it comes out after lines that came later, and a test that captures the
// console never sees it at all (measured on the PC, 31/10).
void say(const char* text) {
  pal::IConsole* console = pal::system().console();
  if (console != nullptr && text != nullptr) console->write(text, strlen(text));
}

void sayf(const char* format, ...) {
  char line[128];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  say(line);
}

void write_text(const char* key, const char* text) {
  if (g_nvs == nullptr) return;
  if (text == nullptr || text[0] == '\0') {
    g_nvs->erase(kNs, key);
    return;
  }
  if (!g_nvs->set(kNs, key, text, strlen(text) + 1)) {
    log_printf(pal::kLogWarn, "power: the nvs did not take '%s'", key);
  }
}

bool read_text(const char* key, char* out, size_t cap) {
  if (g_nvs == nullptr || out == nullptr || cap == 0) return false;
  size_t len = 0;
  if (!g_nvs->get(kNs, key, out, cap - 1, len) || len == 0) return false;
  out[(len < cap) ? len : cap - 1] = '\0';
  return out[0] != '\0';
}

// What a deep sleep has to write down, because the RAM does not survive it: the
// app in front and the file it was opened with (docs/90).
void save_session() {
  const char* id = shell::app_id();
  if (id == nullptr || id[0] == '\0') {
    write_text(kAppKey, nullptr);
    write_text(kPathKey, nullptr);
    return;
  }
  write_text(kAppKey, id);
  write_text(kPathKey, luabind::app_path());
}

// The hour this sleep was told to come back at, written where a board that
// restarted can still read it.
void save_deadline(int64_t at_epoch_ms) {
  char text[24];
  if (at_epoch_ms <= 0) {
    write_text(kWakeKey, nullptr);
    return;
  }
  snprintf(text, sizeof(text), "%lld", static_cast<long long>(at_epoch_ms));
  write_text(kWakeKey, text);
}

// The number M6 asks for: whether the board came back when it said it would. It is
// printed because it is measured and not estimated (docs/90). The note of the hour
// is the only thing that can say it, because a light sleep does not restart the
// clock and the wake cause of the chip is only read after a real sleep.
void report_wake() {
  char text[24];
  const bool armed = read_text(kWakeKey, text, sizeof(text));
  const int64_t at = armed ? strtoll(text, nullptr, 10) : 0;
  write_text(kWakeKey, nullptr);
  if (g_clock == nullptr) return;

  const int64_t epoch = g_clock->epoch_ms();
  if (at > 0 && epoch >= at) {
    sayf("power: woke on the timer, %+lld ms from the hour it was given\r\n",
         static_cast<long long>(epoch - at));
    // Picked up by the log ring as well, so `log` shows it after the console has
    // scrolled.
    log_printf(pal::kLogInfo, "power: woke %+lld ms from the hour it was given",
               static_cast<long long>(epoch - at));
    return;
  }

  // No note and nothing to check: a plain boot says nothing, because the banner
  // already said the board is up. With a note that the clock has not reached, a
  // finger on the button brought it back early and that is worth a line.
  if (at <= 0) return;
  const char* how = "the power coming back";
  switch (g_clock->last_wake()) {
    case hal::WakeCause::kTimer:
      how = "the timer";
      break;
    case hal::WakeCause::kConsole:
      how = "the console";
      break;
    case hal::WakeCause::kOther:
      how = "a button";
      break;
    default:
      break;
  }
  sayf("power: woke on %s, %+lld ms before the hour it was given\r\n", how,
       static_cast<long long>(epoch - at));
}

// The interpreter keeps knowing nothing about the HAL, so the mode travels as a
// number and is turned back into the board's word here (docs/30).
hal::SleepMode mode_of(uint8_t mode) {
  return (mode == 1) ? hal::SleepMode::kDeep : hal::SleepMode::kLight;
}

bool can_sleep_op(uint8_t mode) { return can_sleep(mode_of(mode)); }

bool sleep_op(uint8_t mode, int64_t at_epoch_ms) { return request_sleep(mode_of(mode), at_epoch_ms); }

}  // namespace

void boot(hal::IPlatform& platform) {
  g_power = platform.power();
  g_clock = platform.clock();
  g_nvs = platform.nvs();
  g_caps = platform.caps().bits;
  g_keep_awake = false;

  // What an app may ask for. Without a power interface the operations stay
  // absent, and power.* answers E_UNSUPPORTED instead of pretending (docs/30).
  luabind::PowerOps ops = {};
  if (g_power != nullptr) {
    ops.can_sleep = &can_sleep_op;
    ops.sleep = &sleep_op;
    ops.battery = &battery;
    ops.cpu_mhz = &cpu_mhz;
    ops.set_cpu_mhz = &set_cpu_mhz;
    ops.keep_awake = &keep_awake;
    ops.keeping_awake = &keeping_awake;
    ops.wakes = &wakes;
  }
  luabind::set_power_ops(ops);

  if (g_power == nullptr) return;
  report_wake();
  restore_session();
}

bool available() { return g_power != nullptr; }

bool can_sleep(hal::SleepMode mode) {
  if (g_power == nullptr) return false;
  const uint32_t bit = (mode == hal::SleepMode::kDeep) ? hal::kCapSleepDeep : hal::kCapSleepLight;
  return (g_caps & static_cast<uint32_t>(bit)) != 0;
}

const char* wakes() { return (g_power != nullptr) ? g_power->wakes() : "none"; }

bool sleep(hal::SleepMode mode, int64_t at_epoch_ms) {
  if (g_power == nullptr) return false;
  if (!can_sleep(mode)) return false;
  // A deep sleep with no hour to come back at is a board that waits for somebody
  // to press the button. That is a decision and not an accident, so it is refused
  // here and the caller says which one it wanted (docs/90).
  if (mode == hal::SleepMode::kDeep && at_epoch_ms <= 0) return false;

  if (mode == hal::SleepMode::kDeep) {
    // What was in front is written down **before** it is closed: closing stops the
    // interpreter and forgets which app it was (docs/90).
    save_session();
    if (shell::app_running()) shell::close_app();
  }
  save_deadline(at_epoch_ms);

  const bool back = g_power->sleep(mode, at_epoch_ms);
  if (!back) return false;
  // A board that answers slept lightly (or is the emulator): what it was told is
  // checked against what happened, and the note is not kept for a restart that is
  // not coming.
  report_wake();
  return true;
}

bool request_sleep(hal::SleepMode mode, int64_t at_epoch_ms) {
  if (g_power == nullptr || !can_sleep(mode)) return false;
  if (mode == hal::SleepMode::kDeep && at_epoch_ms <= 0) return false;
  // One sleep at a time: two answers would be two promises for one wake.
  if (g_pending) return false;
  g_pending = true;
  g_pending_from_app = true;
  g_pending_mode = mode;
  g_pending_at = at_epoch_ms;
  return true;
}

void poll() {
  if (!g_pending) return;
  const bool from_app = g_pending_from_app;
  const hal::SleepMode mode = g_pending_mode;
  const int64_t at = g_pending_at;
  g_pending = false;
  g_pending_from_app = false;

  const bool back = sleep(mode, at);
  // The promise of an app is kept when the board is back. A deep sleep closes the
  // app, so there is nobody left to tell (docs/30).
  if (from_app && mode != hal::SleepMode::kDeep) luabind::power_progress(back);
}

bool keep_awake(bool on) {
  g_keep_awake = on;
  return g_keep_awake;
}

bool keeping_awake() { return g_keep_awake; }

bool battery(int& mv, int& pct, bool& charging) {
  if (g_power == nullptr) return false;
  return g_power->battery(mv, pct, charging);
}

uint32_t cpu_mhz() { return (g_power != nullptr) ? g_power->cpu_mhz() : 0; }

bool set_cpu_mhz(uint32_t mhz) {
  if (g_power == nullptr) return false;
  // The steps are the board's, and the kernel does not invent them: 80 is the
  // floor because below it the APB goes down with the CPU and the serial port
  // stops making sense, and 240 is the ceiling of this chip (docs/50).
  if (mhz < 80 || mhz > 240) return false;
  return g_power->set_cpu_mhz(mhz);
}

void restore_session() {
  if (g_nvs == nullptr) return;
  char id[kIdCap];
  if (!read_text(kAppKey, id, sizeof(id))) return;
  char path[kPathCap];
  const bool has_path = read_text(kPathKey, path, sizeof(path));
  // Read once: an app that cannot start must not make the board boot into it for
  // ever, and a boot that comes back to the same app is the same as a boot loop
  // nobody asked for (docs/90).
  write_text(kAppKey, nullptr);
  write_text(kPathKey, nullptr);

  sayf("power: %s comes back where it was\r\n", id);
  if (!shell::restore_app(id, has_path ? path : nullptr)) {
    sayf("power: %s could not come back (is it still there?)\r\n", id);
  }
}

void forget_session() {
  write_text(kAppKey, nullptr);
  write_text(kPathKey, nullptr);
  write_text(kWakeKey, nullptr);
}

}  // namespace power
}  // namespace kernel
