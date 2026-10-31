// The button of the board: the taps the system keeps for itself and the same taps
// handed to the app (docs/30).
#include "kernel/button.h"

#include <stdio.h>
#include <string.h>

#include "kernel/shell.h"
#include "luabind/luabind.h"
#include "pal/pal.h"

namespace kernel {
namespace button {
namespace {

hal::IInput* g_input = nullptr;
hal::IClock* g_clock = nullptr;

// Three taps inside a second and a half close what is in front: it is the gesture
// the console already has with three ESC, and the same window, because a person
// should not have to remember two of them (docs/30).
const uint32_t kTapsToClose = 3;
const int64_t kTapWindowMs = 1500;
// A press held this long is a `long` and it is said once, when the threshold is
// crossed and not when the finger goes up (docs/30).
const int64_t kLongPressMs = 1000;

bool g_was_down = false;
int64_t g_tap_started_ms = 0;
uint32_t g_taps = 0;
bool g_long_sent = false;

bool g_waiting = false;
int64_t g_wait_deadline_ms = 0;

int64_t now_ms() { return (g_clock != nullptr) ? g_clock->monotonic_ms() : 0; }

// Said through the console and not with printf, which is buffered and would come
// out in another order than the shell (measured on the PC, 31/10).
void say(const char* text) {
  pal::IConsole* console = pal::system().console();
  if (console != nullptr && text != nullptr) console->write(text, strlen(text));
}

// The press goes to the app: the promise it is waiting for, or the hook it
// declared. A promise first, because an app that is waiting for a press is waiting
// for exactly this one (docs/30).
void deliver(const char* kind, int64_t held_ms) {
  if (g_waiting) {
    g_waiting = false;
    luabind::button_progress(kind, held_ms);
    return;
  }
  luabind::call_button(kind, held_ms);
}

}  // namespace

bool boot(hal::IPlatform& platform) {
  g_input = platform.input();
  g_clock = platform.clock();
  g_was_down = false;
  g_tap_started_ms = 0;
  g_taps = 0;
  g_long_sent = false;
  g_waiting = false;
  g_wait_deadline_ms = 0;

  // What an app may ask for. Without a button the operations stay absent and
  // button.wait() says E_UNSUPPORTED instead of hanging (docs/30).
  luabind::ButtonOps ops = {};
  if (g_input != nullptr) {
    ops.wait = &wait_start;
    ops.cancel = &cancel;
    ops.available = &available;
  }
  luabind::set_button_ops(ops);
  return g_input != nullptr;
}

bool available() { return g_input != nullptr; }

bool down() { return (g_input != nullptr) && g_input->pressed(); }

int64_t held_ms() { return (g_input != nullptr) ? g_input->held_ms() : 0; }

uint32_t taps_to_close() { return kTapsToClose; }

void cancel() { g_waiting = false; }

bool waiting() { return g_waiting; }

bool wait_start(uint32_t timeout_ms) {
  if (g_input == nullptr || g_waiting) return false;
  if (timeout_ms == 0) return false;
  g_waiting = true;
  g_wait_deadline_ms = now_ms() + static_cast<int64_t>(timeout_ms);
  return true;
}

void poll() {
  if (g_input == nullptr) return;
  const int64_t now = now_ms();
  const bool down = g_input->pressed();
  const bool rising = down && !g_was_down;
  g_was_down = down;

  if (rising) {
    g_long_sent = false;
    if (!shell::app_running()) {
      // Nothing to close and nobody to tell: a press is not counted, so three taps
      // given before an app started do not close it when it appears (docs/30).
      g_taps = 0;
      g_tap_started_ms = now;
    } else {
      if (g_taps == 0 || (now - g_tap_started_ms) > kTapWindowMs) {
        g_taps = 1;
        g_tap_started_ms = now;
      } else {
        ++g_taps;
      }
      // The gesture is counted **before** the app is greeted, so an app that wants
      // every press cannot swallow the one that closes it (docs/30).
      if (g_taps >= kTapsToClose) {
        g_taps = 0;
        say("button: three taps, closing the app\r\n");
        shell::close_app();
      } else {
        deliver("press", 0);
      }
    }
  }

  if (down && !g_long_sent && g_input->held_ms() >= kLongPressMs) {
    g_long_sent = true;
    deliver("long", g_input->held_ms());
  }

  // A press that never came is answered with the deadline, never with silence: an
  // app waiting for ever is an app that cannot be closed (docs/30).
  if (g_waiting && now >= g_wait_deadline_ms) {
    g_waiting = false;
    luabind::button_failed("E_TIMEOUT");
  }
}

}  // namespace button
}  // namespace kernel
