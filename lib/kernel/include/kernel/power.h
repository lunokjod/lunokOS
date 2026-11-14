// The energy policy of the system: when the board sleeps, what it writes down
// before it goes, and what comes back when it wakes (docs/90, M6).
//
// The board knows how to stop the clocks (hal::IPower) and the kernel is the one
// that decides when that is allowed, what has to be said to the app first and what
// has to be read afterwards (docs/30).
#ifndef LUNOK_KERNEL_POWER_H
#define LUNOK_KERNEL_POWER_H

#include <stddef.h>
#include <stdint.h>

#include "hal/hal.h"
#include "kernel/splash.h"

namespace kernel {
namespace power {

// Remembers the board's power, reports what the last sleep cost and brings back
// the app that was in front when the board went down. Runs at boot.
void boot(hal::IPlatform& platform);

// True when this board has a power interface and can sleep at all.
bool available();

// True when the board can do this kind of sleep: a band that only knows a light
// sleep says so, and `power.caps()` repeats it (docs/30).
bool can_sleep(hal::SleepMode mode);

// Where it comes back from, in the board's own words ("timer, button").
const char* wakes();

// How far the last sleep was from the hour it was given, in a line a person can read:
// `+1234 ms (timer)`. False when nothing has been measured yet. It is kept in the
// key-value store because the console line is one boot older and gone by the time
// somebody wonders, and because this number is what decides whether the wake has to move
// to the board's clock chip (docs/50, docs/60, docs/90).
bool last_wake_report(char* out, size_t cap);

// The sleep itself, done now: it is what the shell command uses, when there is no
// app in the way. A **deep** sleep does not come back: the session is written, the
// app in front is closed with its goodbye hooks and the timer is set for
// `at_epoch_ms`; the board restarts and `boot()` brings the app back (docs/30,
// docs/90). A light sleep keeps everything and returns true.
bool sleep(hal::SleepMode mode, int64_t at_epoch_ms);

// The same, asked for while the system is turning (an app, or a window): closing
// the app and stopping the chip happen **between turns**, never inside a Lua call,
// because closing the interpreter from the inside is not a thing (docs/30).
bool request_sleep(hal::SleepMode mode, int64_t at_epoch_ms);

// Does the pending request, if there is one, and then applies the **idle policy**: on a
// board whose deep sleep is a real power off (a clock chip keeps the hour while the power
// is gone) and with an alarm pending, the board goes to sleep by itself until that alarm
// instead of staying awake waiting for it (`next_alarm_epoch_ms`, 0 when there is none).
// The kernel calls it in every turn, after the alarms and before the app's turn (docs/60).
void poll(int64_t next_alarm_epoch_ms);

// A person did something (a byte arrived on the console): the board is being used and
// does not go to sleep yet. The kernel says it, and so does anything that turns the
// policy back on (docs/60).
void note_activity();

// The idle policy is a switch a person can turn off while working at the prompt:
// `power idle off` keeps the board on the whole time (docs/30).
bool idle_sleep_allowed();
void set_idle_sleep_allowed(bool on);

// "Do not sleep on your own while this is set." The idle policy reads it before it puts the
// board down, and an app that is doing something that must not be interrupted says it here
// (docs/30, docs/60).
bool keep_awake(bool on);
bool keeping_awake();

// The battery, when the board has a gauge. False is "this board cannot tell", and
// that is not "empty" (docs/75).
bool battery(int& mv, int& pct, bool& charging);

// **Under this mark a board says it is over** (0.183.0): one number, in one place, so that the boot, the way down and the
// console cannot disagree about what "low" is. Five percent of a cell of this glass is minutes and not hours (docs/50).
constexpr uint8_t kLowBatteryPercent = 5;

// **The battery a board has left, said as a yes or a no**: true when it can be read, there is no cable in the slot and
// the charge is at or under the mark. The cell is the board's and the number is the gauge's, so both a board without one
// and a gauge that answers nothing say `false` —"nothing to say"— and never "empty" (docs/50).
bool battery_is_low(int& pct);

// **The picture of a moment that the battery decides** (0.183.0). The boot asks this before this module has been booted,
// so the reading comes in as an argument —`known` is whether the gauge answered at all—: a board that comes up with a
// cell under the mark says `lowbatt` instead of its logo, because the picture of the boot is the one moment where the
// person can still be told, and the chosen drawing is what a board with a battery comes up with.
splash::Kind boot_picture(const char* chosen, bool known, int pct, bool charging);

// **The picture of the way down**: `lowbatt` when the battery is what is over, `sleep` for a deep sleep and `poweroff`
// for a board that is being asked to stop. A light sleep paints nothing at all (docs/50).
splash::Kind going_down_picture(bool deep);

// The speed of the processor. It is the same decision as the sleep —energy— and it
// is taken here and not inside an app: the board says which steps it has, and a
// step it does not have is refused instead of rounded (docs/30, docs/50).
uint32_t cpu_mhz();
bool set_cpu_mhz(uint32_t mhz);

// **La velocitat de repòs és 160 i les estones de feina són finestres** (0.179.38): the system works at the resting step
// and only a moment whose work is known to be heavy —the boot and the start of an app— asks for the top one, because a
// step that is never given back is a board burning its battery at two hundred and forty for nothing. A window is a
// number of milliseconds and the system is the one that sees it end (`poll`); a step a person asks for with `power cpu`
// is the one the system goes back to. **Every move is a line of the log** (docs/50, docs/60).
const uint32_t kRestingMhz = 160;
const uint32_t kBusyMhz = 240;
// **Els segons d'una càrrega** (0.179.38): an app reads its source, builds an interpreter and lays its first page out,
// and three seconds is what it takes on this board with room to spare.
const int32_t kStartBusyMs = 3000;
uint32_t resting_cpu_mhz();
// To a step now, and back to the resting one after `hold_ms` — `0` leaves no window and the caller steps it down.
void hold_cpu(uint32_t mhz, int32_t hold_ms, const char* why);
// The resting step now, and no window is owed any more.
void rest_cpu(const char* why);

// The app that was in front when the board went to sleep comes back, with
// `app.args.restore` in its args (docs/30). Runs at boot; exposed so a test can do
// a reboot without one.
void restore_session();

// Forgets what a sleep left behind: what a person means by "do not come back
// here".
void forget_session();

}  // namespace power
}  // namespace kernel

#endif  // LUNOK_KERNEL_POWER_H
