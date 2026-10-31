// The energy policy of the system: when the board sleeps, what it writes down
// before it goes, and what comes back when it wakes (docs/90, M6).
//
// The board knows how to stop the clocks (hal::IPower) and the kernel is the one
// that decides when that is allowed, what has to be said to the app first and what
// has to be read afterwards (docs/30).
#ifndef LUNOK_KERNEL_POWER_H
#define LUNOK_KERNEL_POWER_H

#include <stdint.h>

#include "hal/hal.h"

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

// Does the pending request, if there is one. The kernel calls it in every turn,
// after the alarms and before the app's turn.
void poll();

// "Do not sleep on your own while this is set." The system does not sleep on its
// own yet, so this is what an app says to a policy that will read it: it is
// remembered and reported, not silently ignored (docs/90).
bool keep_awake(bool on);
bool keeping_awake();

// The battery, when the board has a gauge. False is "this board cannot tell", and
// that is not "empty" (docs/75).
bool battery(int& mv, int& pct, bool& charging);

// The speed of the processor. It is the same decision as the sleep —energy— and it
// is taken here and not inside an app: the board says which steps it has, and a
// step it does not have is refused instead of rounded (docs/30, docs/50).
uint32_t cpu_mhz();
bool set_cpu_mhz(uint32_t mhz);

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
