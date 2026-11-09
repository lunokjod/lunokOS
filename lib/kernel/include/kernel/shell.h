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

// **Whether the terminal gets the colours**: the answer to the question above, or what `ansi on|off` said.
// A terminal that is a pipe or a file is not asked and does not get one escape byte (docs/30, docs/85).
bool console_ansi();
void set_ansi(bool on);
// Bytes as they arrive from the console. Handles echo, backspace and newline.
void feed(const char* data, size_t len);
// Runs one command line (already null-terminated). Returns false on error.
bool execute(char* line);

// **The restart, asked for by the prompt or by an app** (0.171.0): the picture of the moment goes on the glass, the ink
// is waited on and the board restarts, which is what `reboot` does — it is one function because a restart is a moment a
// person sees and three copies of it would be three moments (docs/30, docs/50). `reason` is written in the log with the
// app's name in front when an app asked; the prompt passes nothing, because a person at the prompt is the reason.
bool reboot(const char* reason);

// ---- a session of the shell that is not the console (0.159.0, docs/30) ----
//
// **The shell is the system's and not the wire's**: `open_session` gives a handle and the caller talks to a shell
// through three pipes —`session_write` is the lines in, `session_read` the answers and `session_read_error` the
// complaints— with no console in the middle, no prompt and no echo. The handle is one based, and `open_session`
// answers 0 with the reason in `why` when it cannot give one.
// **What a session costs**: 1 024 B of answers and 256 B of complaints asked of the heap, given back by
// `close_session`; a byte that does not fit the ring is dropped and counted (`session_dropped`), because an app that
// stops reading must not stop the shell. One session at a time, which is the number `kMaxSessions` says
// (docs/30, docs/75).
uint8_t open_session(char* why, size_t why_cap);
void close_session(uint8_t id);
bool session_open(uint8_t id);
bool session_write(uint8_t id, const char* data, size_t len);
size_t session_read(uint8_t id, char* out, size_t cap);
size_t session_read_error(uint8_t id, char* out, size_t cap);
uint32_t session_dropped(uint8_t id);

// **The six calls, into the interpreter** (0.159.0): `luabind` never includes the kernel, so the boot hands it the
// table and `sys.shell()` is the Lua side of the same session (docs/20, docs/30).
void install_shell_ops();

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

// **Going back one view, which is what every way out asks for first** (0.163.1): the app gives the page on top back
// and hears `on_back(depth)`; true means the app is still in front and the caller does not close it. It is here and
// not in the three places that ask for it —the pinch from big to small, the three taps of the middle key and the
// three taps on the glass— because «go back» has one meaning and a page that stops listening is what the ways out
// exist for (docs/30).
bool go_back_one_view();

// **What going out of a page ended in** (0.167.0), so the three gestures can say it in their own words and a caller does
// not have to ask the same questions twice (docs/30).
enum class LeaveOutcome {
  kNothing,     // no app is in front
  kLauncher,    // a launcher is the one page that is not left
  kBack,        // a page was given back and the app is still in front
  kClosed,      // the only page went and the prompt is what there is
  kClosedHome,  // the only page went and the page that asked for it took the glass back
};

// **The one road out of a page, which every way out asks for** (0.167.0): a page with another one on top gives the top
// one back and stays —the app hears `on_back` and the page below it is refreshed by the system—, and a page that is the
// only one goes with the app, **and the page it was started from takes the glass back**, which is the launcher when a
// launcher asked for it. The pinch, the three presses of the middle key and the three taps on the glass all come here,
// because «leave this page» is one meaning and three gestures that mean it differently are three behaviours (docs/30).
LeaveOutcome leave_one_page();

// The app of the alarm goes to the front: what was in front is closed and this
// app is started in its place (docs/60).
bool raise_app(const char* id);

// **An app asks for another one** (0.153.0): what `app.run` does from Lua, and the switch happens after the turn —the app
// in front goes away with its goodbye hooks and the one asked for comes up in its place—, because the app that asked is
// the one the interpreter belongs to and closing it inside the call would destroy the state the call is running in.
// False with a reason when there is nothing to ask right now; the name is taken at most once per turn (docs/30, docs/90).
// `back` is what a launcher wants: the app that asked is started again when the one it opened ends, which is the only
// way back to a page on a board whose only way out of an app is a key (docs/30, docs/90).
bool ask_run(const char* id, bool back, char* why, size_t why_cap);

// The app that was in front when the board went to sleep comes back, with
// `app.args.restore` in its args so it knows to read what it saved (docs/90).
bool restore_app(const char* id, const char* path);

// **The app the system starts by itself** (0.119.0): the home app of the settings, started the way `run` starts
// one but without a prompt in between —no restore flag, because it is not coming back from anything— and only when
// nothing is in front (docs/30, docs/40).
bool start_app(const char* id);

// The window of an alarm: the app is loaded, turned once and its on_alarm is
// called, and then the VM goes away. Never runs on top of another app.
bool run_window(const char* id, const alarms::Entry& entry, int64_t late_ms);

}  // namespace shell
}  // namespace kernel

#endif  // LUNOK_KERNEL_SHELL_H
