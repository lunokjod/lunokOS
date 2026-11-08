// The energy policy: the board can stop its clocks, and this is the one place that
// says when, what is said to the app first and what is read afterwards (docs/90).
#include "kernel/meter.h"
#include "kernel/power.h"
#include "kernel/splash.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/shell.h"
#include "kernel/led.h"
#include "kernel/log.h"
#include "alarms/alarms.h"
#include "hal/alarm.h"
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

// Where the last wake leaves its number: how far it was from the hour it was given, and
// what brought it back. The console line is one boot earlier and gone, so the number that
// decides whether the wake has to move to the clock chip has to be readable afterwards
// (docs/50, docs/60, docs/90).
const char kPowerNs[] = "power";
const char kDriftKey[] = "drift";
const char kCauseKey[] = "cause";

// The idle policy. Two numbers, and both are a decision rather than a measurement:
//
//   - the console has to be **quiet** before the board puts itself down, or a person would
//     be typing at a board that keeps powering off between two commands;
//   - a wait shorter than ten seconds is not worth a power cycle: a wake from a power off is a
//     boot, and a boot costs around a second of work. Below a minute that is only true because
//     the clock chip's **countdown** counts seconds (docs/50).
const int64_t kIdleQuietMs = 10000;
const int64_t kMinIdleSleepMs = 10000;

bool g_idle_allowed = true;
int64_t g_last_activity_ms = 0;
// The hour the board was woken early for. The clock reaches a day at most, so a longer wait is
// armed in stages, and a stage is **not** the wake: the app must not come back until its hour
// (docs/60).
int64_t g_coming_back_at = 0;

int64_t mono_now() {
  // The RTC chip, read once a turn (docs/30, docs/50).
  meter::Scope scope(meter::kClockMono);
  return (g_clock != nullptr) ? g_clock->monotonic_ms() : 0;
}

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

void write_in(const char* ns, const char* key, const char* text) {
  if (g_nvs == nullptr) return;
  // **One row for every write to the store**, erasing included: an erase is a write to the flash like
  // any other and it is the one that wears it (docs/70, docs/75).
  meter::Scope scope(meter::kKvSet);
  if (text == nullptr || text[0] == '\0') {
    g_nvs->erase(ns, key);
    return;
  }
  if (!g_nvs->set(ns, key, text, strlen(text) + 1)) {
    log_printf(pal::kLogWarn, "POWER", "the nvs did not take '%s'", key);
  }
}

bool read_in(const char* ns, const char* key, char* out, size_t cap) {
  if (g_nvs == nullptr || out == nullptr || cap == 0) return false;
  size_t len = 0;
  if (!g_nvs->get(ns, key, out, cap - 1, len) || len == 0) return false;
  out[(len < cap) ? len : cap - 1] = '\0';
  return out[0] != '\0';
}

// The number of the last sleep, kept where a person can ask for it later. It is written
// every time it is measured, so what `power last` shows is the last one and not the best
// one (docs/60).
void keep_measurement(int64_t drift_ms, const char* cause) {
  char number[24];
  snprintf(number, sizeof(number), "%+lld", static_cast<long long>(drift_ms));
  write_in(kPowerNs, kDriftKey, number);
  write_in(kPowerNs, kCauseKey, cause);
}

void write_text(const char* key, const char* text) { write_in(kNs, key, text); }

bool read_text(const char* key, char* out, size_t cap) { return read_in(kNs, key, out, cap); }

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
//
// `restarted` says the board came back from a boot and not from a light sleep, and it is what makes
// the number about the sleep and not about the firmware: after a restart the clock is read once the
// bootloader, the banner, the flash, the card and the settings have had their second, and counting
// that second as the clock's error is measuring the wrong thing (measured on the Paper, 31/10: a
// 20 s sleep came out 6 % off until the boot was taken out; docs/60).
void report_wake(bool restarted) {
  char text[24];
  const bool armed = read_text(kWakeKey, text, sizeof(text));
  const int64_t at = armed ? strtoll(text, nullptr, 10) : 0;
  write_text(kWakeKey, nullptr);
  if (g_clock == nullptr) return;

  // What brought the board back. Which of the two clocks armed the wake decides the name: the
  // chip's own timer on a board that kept the power on, or the clock chip's quartz on a board that
  // was off (docs/50, docs/60).
  const hal::WakeCause woke = g_clock->last_wake();
  const bool by_clock = (woke == hal::WakeCause::kClock);
  const char* how = "the power coming back";
  const char* cause = "power";
  switch (woke) {
    case hal::WakeCause::kTimer:
      how = "the timer";
      cause = "timer";
      break;
    case hal::WakeCause::kConsole:
      how = "the console";
      cause = "console";
      break;
    case hal::WakeCause::kOther:
      how = "a button";
      cause = "button";
      break;
    case hal::WakeCause::kClock:
      how = "the clock";
      cause = "clock";
      break;
    default:
      break;
  }

  const int64_t epoch = g_clock->epoch_ms();
  // The boot is only taken out of a wake that a sleep caused: a plain boot has no hour to compare
  // and the numbers there are the "cut short" ones, which say how early and not how precise.
  const bool slept = restarted && (woke != hal::WakeCause::kPowerOn);
  const int64_t uptime = slept ? g_clock->monotonic_ms() : 0;
  const int64_t error = hal::wake_error_ms(epoch, uptime, at, slept);
  if (at > 0 && epoch >= at) {
    keep_measurement(error, cause);
    sayf("power: woke on the %s, %+lld ms from the hour it was given\r\n", cause,
         static_cast<long long>(error));
    // Picked up by the log ring as well, so `log` shows it after the console has
    // scrolled.
    log_printf(pal::kLogInfo, "POWER", "woke %+lld ms from the hour it was given",
               static_cast<long long>(error));
    return;
  }

  // No note and nothing to check: a plain boot says nothing, because the banner
  // already said the board is up.
  if (at <= 0) return;
  // A deadline further away than the clock can express is armed in stages: the chip fires on
  // the first match of its hour and minute, the board finds nothing due, and it arms the rest.
  // That is the clock doing its job and it is not a drift, so it is not measured —but the hour
  // has to be put back where the next stage will look for it, and the app must stay closed, or
  // it would come back a day early and never know (docs/60).
  if (by_clock) {
    save_deadline(at);
    g_coming_back_at = at;
    sayf("power: the clock woke the board early to arm again, %+lld ms to go\r\n",
         static_cast<long long>(epoch - at));
    return;
  }
  // Everything else that comes back before its hour **did not run its sleep to the end**: a finger
  // on the button, a cable plugged in, a supply that blinked. The number is how early it was cut
  // short, and calling it a drift was a lie about the clock —the board never got to prove
  // anything— so it is said as what it is and the last measurement keeps the word `short` (docs/60).
  const int64_t early = at - epoch;
  char marked[24];
  snprintf(marked, sizeof(marked), "%s, cut short", cause);
  keep_measurement(-early, marked);
  sayf("power: the sleep was cut short by %s, %lld ms before the hour it was given\r\n", how,
       static_cast<long long>(early));
  log_printf(pal::kLogInfo, "POWER", "sleep cut short by %s, %lld ms before its hour", cause,
             static_cast<long long>(early));
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
  g_idle_allowed = true;
  // The quiet count starts with this boot: nothing has been typed at it yet.
  g_last_activity_ms = 0;
  // A stage of a longer wait belongs to one boot: `report_wake()` is what sets it again
  // (docs/60).
  g_coming_back_at = 0;

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
  report_wake(true);  // this is a boot: the board came back from a restart or from nothing at all
  // A stage of a longer wait is not the wake: the app stays closed until its hour. If the wait
  // left is down to a few seconds, that is close enough to be the wake and the app comes back
  // (docs/60).
  if (g_coming_back_at > 0 && g_clock != nullptr &&
      g_coming_back_at - g_clock->epoch_ms() < kMinIdleSleepMs) {
    g_coming_back_at = 0;
  }
  if (g_coming_back_at <= 0) restore_session();
}

bool available() { return g_power != nullptr; }

bool can_sleep(hal::SleepMode mode) {
  if (g_power == nullptr) return false;
  const uint32_t bit = (mode == hal::SleepMode::kDeep) ? hal::kCapSleepDeep : hal::kCapSleepLight;
  return (g_caps & static_cast<uint32_t>(bit)) != 0;
}

const char* wakes() { return (g_power != nullptr) ? g_power->wakes() : "none"; }

bool last_wake_report(char* out, size_t cap) {
  if (out == nullptr || cap == 0) return false;
  char drift[24] = {0};
  // The cause is a word and, since a sleep can be cut short, a marker after it (`timer, cut short`):
  // sixteen bytes was enough for the word alone and cut the marker in half (docs/60).
  char cause[24] = {0};
  if (!read_in(kPowerNs, kDriftKey, drift, sizeof(drift))) return false;
  if (!read_in(kPowerNs, kCauseKey, cause, sizeof(cause))) cause[0] = '\0';
  const int written = snprintf(out, cap, "%s ms (%s)", drift, (cause[0] != '\0') ? cause : "?");
  return written > 0 && static_cast<size_t>(written) < cap;
}

// A **light** sleep comes back to the same place and a finger can already cut it short, so the
// nearest alarm may cut it short too: the alarm belongs to the system and must not be made to wait
// for an app that is asleep. The hour that is written down is the one the board was really told, or
// `power last` would measure a difference nobody asked for (docs/60).
int64_t light_sleep_until(int64_t at_epoch_ms) {
  if (g_clock == nullptr) return at_epoch_ms;
  const int64_t next = alarms::next_due_mono_ms();
  if (next < 0) return at_epoch_ms;
  const int64_t epoch = g_clock->epoch_ms();
  if (epoch <= 0) return at_epoch_ms;
  const int64_t alarm_epoch = epoch + (next - g_clock->monotonic_ms());
  if (alarm_epoch > 0 && alarm_epoch < at_epoch_ms) return alarm_epoch;
  return at_epoch_ms;
}

bool sleep(hal::SleepMode mode, int64_t at_epoch_ms) {
  // **A sleep is a call that does not come back until the board wakes**, so it is measured whole
  // (docs/30, docs/50).
  meter::Scope scope(meter::kSleep);
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
  const int64_t until = (mode == hal::SleepMode::kLight) ? light_sleep_until(at_epoch_ms)
                                                        : at_epoch_ms;
  save_deadline(until);

  // The line is emptied before the board stops: the UART keeps bytes in flight after a
  // write returns, and both sleeps stop its clock, so the last thing a person reads would
  // be a word cut in half. It is done here and not in each board's sleep because the
  // console belongs to the system and not to the chip (docs/50, docs/60).
  pal::IConsole* console = pal::system().console();
  if (console != nullptr) console->flush();

  // **The picture of the way down** (0.111.0): a board that is going to stop its clocks says so on the glass
  // first —after the app is closed and before the chip stops— so a person finds the reason it went dark instead of
  // the last page of an app that is no longer there. A light sleep is a nap and paints nothing: the board keeps what
  // it has and is back in a moment (docs/30, docs/50, docs/92).
  if (mode == hal::SleepMode::kDeep) splash::show(splash::kPowerOff);
  // **And the ink of the picture is over before the chip stops** (0.165.1): the deep sleep cuts the clocks where the call
  // found them, so a picture asked for and not waited on is a picture that nobody sees (docs/50).
  if (mode == hal::SleepMode::kDeep) splash::wait();
  const bool back = g_power->sleep(mode, until);
  if (!back) return false;
  // A board that answers slept lightly (or is the emulator): what it was told is
  // checked against what happened, and the note is not kept for a restart that is
  // not coming.
  //
  // The hour is taken back from wherever the board keeps it properly **before** it is
  // measured: a sleep is exactly when the chip's own counter drifts, and a board with a clock
  // chip has a better hour than its counter does (docs/60).
  if (g_clock != nullptr) g_clock->resync();
  report_wake(false);
  return true;
}

void note_activity() { g_last_activity_ms = mono_now(); }

bool idle_sleep_allowed() { return g_idle_allowed; }

void set_idle_sleep_allowed(bool on) {
  g_idle_allowed = on;
  // Turning the policy back on does not put the board down in the same millisecond: the
  // quiet count starts again here.
  note_activity();
}

// The idle policy: **a board that keeps the hour in a chip can stop being powered and let
// the quartz count**, which is the whole reason the rule exists (README, law 9). It happens
// only when every one of these holds, and each one is a board that would sleep through
// something:
//
//   - this board's deep sleep is a real power off, which is how the system knows there is a
//     chip behind the hour. On a board whose deep sleep keeps the chip on, the software
//     queue is already free and a restart would be a cost for nothing (that is the ATOM);
//   - an alarm is pending: without a deadline there is nothing to come back to, and a board
//     that sleeps with no hour waits for a finger on the button;
//   - nobody is using it: the console has been quiet, no app is running, no app has said
//     `keep_awake`, and the light is not saying anything —it is an effect that has to be
//     painted every turn, and a board that powers off in the middle of a breath is a light
//     that lies (docs/30);
//   - the wait is long enough that a boot is cheaper than staying awake.
void idle_sleep(int64_t next_alarm_epoch_ms) {
  if (!g_idle_allowed || g_power == nullptr || g_clock == nullptr) return;
  if (!g_power->deep_powers_off()) return;
  // A stage of a longer wait has its hour already written down, and it is not in the queue, so
  // it is the one that counts (docs/60).
  const bool staging = g_coming_back_at > 0;
  const int64_t deadline = staging ? g_coming_back_at : next_alarm_epoch_ms;
  if (deadline <= 0) return;
  if (shell::app_running() || keeping_awake()) return;
  // An effect of the light is the board saying something right now, and it is painted once a
  // turn: powering off would cut it in the middle (docs/30).
  if (led::animating()) return;
  const int64_t now_epoch = g_clock->epoch_ms();
  if (now_epoch <= 0) return;
  const int64_t wait = deadline - now_epoch;
  if (wait < kMinIdleSleepMs) return;
  const int64_t mono = mono_now();
  if (g_last_activity_ms == 0) {
    g_last_activity_ms = mono;
    // A stage goes straight back to sleep: nobody has touched the board since it came back for
    // it, and every second awake here is a second of the wait wasted.
    if (!staging) return;
  } else if (mono - g_last_activity_ms < kIdleQuietMs) {
    return;
  }

  g_coming_back_at = 0;
  sayf("power: nothing to do for %lld s, sleeping until the next alarm\r\n",
       static_cast<long long>(wait / 1000));
  log_printf(pal::kLogInfo, "POWER", "idle sleep, %lld ms to the next alarm",
             static_cast<long long>(wait));
  // Does not come back: on this board the power goes and the clock chip brings it back.
  // If it does come back (a cable in the slot), the board is being used again.
  sleep(hal::SleepMode::kDeep, deadline);
  note_activity();
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

void poll(int64_t next_alarm_epoch_ms) {
  if (g_pending) {
    const bool from_app = g_pending_from_app;
    const hal::SleepMode mode = g_pending_mode;
    const int64_t at = g_pending_at;
    g_pending = false;
    g_pending_from_app = false;

    const bool back = sleep(mode, at);
    // The promise of an app is kept when the board is back. A deep sleep closes the
    // app, so there is nobody left to tell (docs/30).
    if (from_app && mode != hal::SleepMode::kDeep) luabind::power_progress(back);
    return;
  }
  idle_sleep(next_alarm_epoch_ms);
}

bool keep_awake(bool on) {
  g_keep_awake = on;
  return g_keep_awake;
}

bool keeping_awake() { return g_keep_awake; }

bool battery(int& mv, int& pct, bool& charging) {
  // The gauge answers over the I2C and the bar asks for it on every turn (docs/50).
  meter::Scope scope(meter::kPowerBattery);
  if (g_power == nullptr) return false;
  return g_power->battery(mv, pct, charging);
}

uint32_t cpu_mhz() { return (g_power != nullptr) ? g_power->cpu_mhz() : 0; }

bool set_cpu_mhz(uint32_t mhz) {
  // The processor changes market: it is a call to the board and not a variable (docs/30).
  meter::Scope scope(meter::kCpuSet);
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
