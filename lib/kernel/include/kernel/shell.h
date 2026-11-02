// The serial shell. At M0 it is the only user interface there is, because the
// board has no screen (docs/10 and docs/50).
#ifndef LUNOK_KERNEL_SHELL_H
#define LUNOK_KERNEL_SHELL_H

#include <stddef.h>
#include <stdint.h>

#include "alarms/alarms.h"

namespace kernel {
namespace shell {

void start();
void prompt();

// **Whether a terminal answered the question that was asked of it.** The shell asks the Device Attributes
// query (`ESC [ c`) once, at start, and what comes back is scanned for the reply `ESC [ ? ... c`: a terminal
// that speaks ANSI answers and one that does not stays quiet (docs/85). It is a pure rule over the bytes that
// arrived, so it can be tried without a port, and the answer is what `console_ansi()` and the redraw of the
// line use from then on.
bool ansi_reply(const char* data, size_t len);
// Bytes as they arrive from the console. Handles echo, backspace and newline.
void feed(const char* data, size_t len);
// Runs one command line (already null-terminated). Returns false on error.
bool execute(char* line);

// True while an app has been started and has not finished. An app may be waiting
// for a promise between turns of the kernel (docs/30).
bool app_running();

// True while a command that waits for the radio is running: the shell shows no
// prompt while it is, because its answer is not in yet (docs/30).
bool native_running();

// One turn for the app. Returns true while it is alive; when it finishes, the
// prompt comes back and the interpreter is stopped.
bool poll_app();

// Milliseconds until the app needs attention, or -1 when there is no app or the
// app waits for something without a deadline.
int32_t app_wait_ms();

// The id of the app that is alive, or "" when there is none.
const char* app_id();

// Closes the app that is alive, if there is one. What `raise` does to whatever
// was in front before the alarm takes over.
void close_app();

// The app of the alarm goes to the front: what was in front is closed and this
// app is started in its place (docs/60).
bool raise_app(const char* id);

// The app that was in front when the board went to sleep comes back, with
// `app.args.restore` in its args so it knows to read what it saved (docs/90).
bool restore_app(const char* id, const char* path);

// The window of an alarm: the app is loaded, turned once and its on_alarm is
// called, and then the VM goes away. Never runs on top of another app.
bool run_window(const char* id, const alarms::Entry& entry, int64_t late_ms);

}  // namespace shell
}  // namespace kernel

#endif  // LUNOK_KERNEL_SHELL_H
