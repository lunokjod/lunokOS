// The button of the board: the taps the system keeps for itself and the same taps
// handed to the app (docs/30).
#include "kernel/meter.h"
#include "kernel/button.h"

#include <stdio.h>
#include <string.h>

#include "kernel/log.h"
#include "kernel/shell.h"
#include "kernel/ui.h"
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

// The board may have more than one key (three on the Paper) and the kernel keeps
// the state of each one: who is down and who has already been told as `long`.
// Only key 0 counts taps, because three taps of a side key is a menu and three of
// the middle one is "get out" (docs/30, docs/50).
const uint32_t kMaxKeys = 4;
bool g_was_down[kMaxKeys] = {false, false, false, false};
bool g_long_sent[kMaxKeys] = {false, false, false, false};
uint32_t g_key_count = 0;
int64_t g_tap_started_ms = 0;
uint32_t g_taps = 0;

bool g_waiting = false;
int64_t g_wait_deadline_ms = 0;

int64_t now_ms() {
  // **The clock is read once a turn and once per key, and that is what `stats` has to see**: the scope goes
  // before the answer and not after it, where it was dead code the compiler threw away (docs/30, docs/50).
  meter::Scope scope(meter::kClockMono);
  return (g_clock != nullptr) ? g_clock->monotonic_ms() : 0;
}

// **La frase de les tres pulsacions va al log i el port sèrie la copia** (0.179.55): aquestes línies són decisions del
// sistema i han de ser al ring com les altres —`log` les ha de poder ensenyar—, i el text es queda com és perquè és el
// que una persona llegeix (docs/30, docs/50). Abans anaven escrites directament al port i no eren al log.
void say(const char* text) { log_say(text); }

// The press goes to the app: the promise it is waiting for, or the hook it
// declared. A promise first, because an app that is waiting for a press is waiting
// for exactly this one (docs/30).
// **Ja no és el camí de tothom** (0.180.0): this is the **system's own subscriber** of the queue, the first one in
// cascade, and it is where the dial, the promise and `on_button` live. It is registered at `boot()` and it can be
// skipped by an exclusive subscriber, who is a control that has taken the keys for itself (docs/30, docs/90).
void system_listener(const char* kind, uint32_t key, int64_t held_ms, void* owner) {
  (void)owner;
  // **El dial no toca el teclat de programari** (0.179.46): the owner: *«No vull selector amb botons físics al teclat per
  // software, al teclat per software només vull tocs de dits»*. So the keys of the board go straight to the page, and a box a
  // person has entered takes them as the caret —left and right in a box of one line, a row up and down in a box of several
  // lines keeping the column it came from—, which is the same thing the finger does when it lands on a letter (docs/30,
  // docs/90).
  // **I una app que s'ha posat a la cua no rep el hook a més** (0.180.0): an app that registered a listener chose the
  // queue, so `on_button` is not called a second time behind its back (docs/30).
  if (!g_waiting && luabind::ui_button_event(kind, key)) return;
  if (g_waiting) {
    g_waiting = false;
    luabind::button_progress(kind, held_ms, key);
    return;
  }
  // **I una app que s'ha posat a la cua no rep el hook a més** (0.181.2): an app that registered a listener chose the queue, and
  // the same press arriving twice —once with the event and once with the hook— is a page that moves two steps for one key
  // (docs/30).
  if (luabind::has_button_listener()) return;
  luabind::call_button(kind, held_ms, key);
}

// ---- the queue of subscribers (0.180.0) ----

// **Eight is the page's, not the system's**: the system takes one, a control that takes the keys takes one, and an app
// takes one; the rest is room for a dialog over a page that already took them (docs/30, docs/75).
const uint8_t kMaxSubscribers = 8;

struct Slot {
  uint16_t handle;
  Listener listener;
  void* owner;
  Mode mode;
  bool used;
};

Slot g_slots[kMaxSubscribers] = {};
uint16_t g_next_handle = 1;
// The owner of the system's own subscription: an address nothing else can have (docs/30).
char g_system_owner = 0;

void deliver(const char* kind, int64_t held_ms, uint32_t key) {
  // **L'exclusiu de més amunt és qui respon** (0.180.0): the last exclusive in the queue is the one that answers and
  // nobody else hears the event —a control that took the keys sits over the app's queue, and an app that took them sits
  // over the system's own delivery, which is what "exclusive" means (docs/30, docs/90).
  int top = -1;
  for (uint8_t i = 0; i < kMaxSubscribers; ++i) {
    if (g_slots[i].used && g_slots[i].mode == Mode::kExclusive) top = i;
  }
  if (top >= 0) {
    const Listener listener = g_slots[top].listener;
    void* const owner = g_slots[top].owner;
    listener(kind, key, held_ms, owner);
    return;
  }
  // **Cascade és tots, en l'ordre en què es van registrar**: a listener may unsubscribe itself or another one while it
  // runs, so what is called is copied before the call and the table is read again for the next one (docs/30).
  for (uint8_t i = 0; i < kMaxSubscribers; ++i) {
    if (!g_slots[i].used) continue;
    const Listener listener = g_slots[i].listener;
    void* const owner = g_slots[i].owner;
    if (listener != nullptr) listener(kind, key, held_ms, owner);
  }
}

}  // namespace

uint16_t subscribe(Listener listener, void* owner, Mode mode) {
  if (listener == nullptr) return 0;
  for (uint8_t i = 0; i < kMaxSubscribers; ++i) {
    if (g_slots[i].used) continue;
    if (g_next_handle == 0) g_next_handle = 1;
    g_slots[i].handle = g_next_handle++;
    g_slots[i].listener = listener;
    g_slots[i].owner = owner;
    g_slots[i].mode = mode;
    g_slots[i].used = true;
    return g_slots[i].handle;
  }
  return 0;
}

void unsubscribe(uint16_t handle) {
  if (handle == 0) return;
  for (uint8_t i = 0; i < kMaxSubscribers; ++i) {
    if (!g_slots[i].used || g_slots[i].handle != handle) continue;
    g_slots[i] = Slot{};
    return;
  }
}

void unsubscribe_owner(void* owner) {
  for (uint8_t i = 0; i < kMaxSubscribers; ++i) {
    if (!g_slots[i].used || g_slots[i].owner != owner) continue;
    g_slots[i] = Slot{};
  }
}

uint16_t subscribers() {
  uint16_t count = 0;
  for (uint8_t i = 0; i < kMaxSubscribers; ++i) {
    if (g_slots[i].used) ++count;
  }
  return count;
}

void reset() {
  for (uint8_t i = 0; i < kMaxSubscribers; ++i) g_slots[i] = Slot{};
  // **El sistema hi torna a ser, i primer** (0.180.0): the dial, the promise and `on_button` are one subscriber in cascade,
  // so a board that boots with nobody else in the queue behaves exactly as it did before there was a queue (docs/30).
  subscribe(&system_listener, &g_system_owner, Mode::kCascade);
}

// A subscriber that is not in the queue is a listener and nothing else: the shell and the tests send an event through the
// same road the board does (docs/30).
void emit(const char* kind, uint32_t key, int64_t held_ms) { deliver(kind, held_ms, key); }

namespace {

// The interpreter's door: an app registers through `luabind`, which hands this module a plain function and its owner
// (docs/10, docs/30).
uint16_t subscribe_for_lua(Listener listener, void* owner, uint8_t mode) {
  return subscribe(listener, owner, (mode == 1) ? Mode::kExclusive : Mode::kCascade);
}

// The same description, in the words the interpreter hands to Lua. The HAL names
// the faces and the spots once, so a board cannot invent a synonym (docs/30).
bool info_for_lua(uint32_t key, luabind::KeyInfo& out) {
  Info mine = {};
  if (!info(key, mine)) return false;
  out.key = mine.key;
  out.face = hal::face_name(mine.face);
  out.spot = hal::spot_name(mine.spot);
  out.kind = hal::kind_name(mine.kind);
  out.group = mine.group;
  return true;
}

}  // namespace

bool boot(hal::IPlatform& platform) {
  g_input = platform.input();
  g_clock = platform.clock();
  g_key_count = (g_input != nullptr) ? g_input->count() : 0;
  if (g_key_count > kMaxKeys) g_key_count = kMaxKeys;
  for (uint32_t key = 0; key < kMaxKeys; ++key) {
    g_was_down[key] = false;
    g_long_sent[key] = false;
  }
  g_tap_started_ms = 0;
  g_taps = 0;
  g_waiting = false;
  g_wait_deadline_ms = 0;

  // **La cua es buida i el sistema hi entra primer** (0.180.0): the dial, the promise and `on_button` are one subscriber
  // in cascade, so a board that boots with nobody else in the queue behaves exactly as it did before there was a queue
  // (docs/30, docs/90).
  reset();

  // What an app may ask for. Without a button the operations stay absent and
  // button.wait() says E_UNSUPPORTED instead of hanging (docs/30).
  luabind::ButtonOps ops = {};
  if (g_input != nullptr) {
    ops.wait = &wait_start;
    ops.cancel = &cancel;
    ops.available = &available;
    ops.count = &count;
    ops.down = &down;
    ops.held = &held_ms;
    ops.info = &info_for_lua;
    // **I la cua** (0.180.0): an app registers a listener of its own and gives it back when it leaves; the kernel keeps
    // the handle and the interpreter is the one that remembers it (docs/30).
    ops.subscribe = &subscribe_for_lua;
    ops.unsubscribe = &unsubscribe;
  }
  luabind::set_button_ops(ops);
  return g_input != nullptr;
}

bool available() { return g_input != nullptr; }

uint32_t count() { return g_key_count; }

bool info(uint32_t key, Info& out) {
  if (g_input == nullptr || key >= g_key_count) return false;
  out.key = key;
  out.face = g_input->face(key);
  out.spot = g_input->spot(key);
  out.kind = g_input->kind(key);
  out.group = g_input->group(key);
  return true;
}

bool down(uint32_t key) {
  return (g_input != nullptr) && (key < g_key_count) && g_input->pressed(key);
}

int64_t held_ms(uint32_t key) {
  return (g_input != nullptr && key < g_key_count) ? g_input->held_ms(key) : 0;
}

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
  // The board says which keys are held: one read per turn, and the taps are arithmetic over it
  // (docs/30, docs/50).
  meter::Scope scope(meter::kKeysRead);
  if (g_input == nullptr) return;
  const int64_t now = now_ms();

  for (uint32_t key = 0; key < g_key_count; ++key) {
    const bool down = g_input->pressed(key);
    const bool rising = down && !g_was_down[key];
    g_was_down[key] = down;

    if (rising) {
      g_long_sent[key] = false;
      if (key != 0) {
        // A side key has no gesture of the system on it: that one is the app's
        // (docs/30).
        deliver("press", 0, key);
      } else if (!shell::app_running()) {
        // Nothing to close and nobody to tell: a press is not counted, so three
        // taps given before an app started do not close it when it appears
        // (docs/30).
        g_taps = 0;
        g_tap_started_ms = now;
      } else if (ui::keyboard_visible()) {
        // **Amb la banda a la pantalla, el sistema no rep la premuda** (0.181.1, i la plaça de la cua de la 0.181.3): the band is an
        // **exclusive** subscriber of the queue while it is on the glass —the owner: *«el teclat al sortir registra el triple tap com a
        // exclusiu i el desregistra al sortir»*—, so `system_listener` is not called and the gesture cannot count. The count is asked
        // here as well and not only there because the gesture is counted **before** the queue, on purpose: an app that takes the keys
        // for itself must not be able to brick a board whose only way out is three taps (docs/30, docs/90).
        g_taps = 0;
        g_tap_started_ms = now;
        deliver("press", 0, key);
      } else {
        // **The three presses are the system's on every page** (0.167.0): a page with controls has the centre as Enter, and
        // the first two presses are delivered to it as they always were; the third is the way out, because a page with no
        // way out is a board a person has to reach with a cable, and the owner's About had a Back control and no way back
        // (docs/30, docs/90).
        if (g_taps == 0 || (now - g_tap_started_ms) > kTapWindowMs) {
          g_taps = 1;
          g_tap_started_ms = now;
        } else {
          ++g_taps;
        }
        // The gesture is counted **before** the app is greeted, so an app that
        // wants every press cannot swallow the one that closes it (docs/30).
        if (g_taps >= kTapsToClose) {
          g_taps = 0;
          // **And the three presses take the same road as the pinch and the three taps on the glass** (0.167.0): a page
          // with views on top of it gives the last one back and stays, and a page that is the only one goes with the app
          // **and the page that asked for it comes back**, which is what the owner asked for —three presses used to
          // close the app and leave the prompt where his launcher had been— (docs/30, docs/45).
          switch (shell::leave_one_page()) {
            case shell::LeaveOutcome::kBack:
              say("button: three taps, back one view\r\n");
              break;
            case shell::LeaveOutcome::kClosedHome:
              say("button: three taps, the page that asked is back\r\n");
              break;
            case shell::LeaveOutcome::kLauncher:
              say("button: three taps, a launcher is the one page that is not left\r\n");
              break;
            default:
              say("button: three taps, closing the app\r\n");
              break;
          }
        } else {
          deliver("press", 0, key);
        }
      }
    }

    if (down && !g_long_sent[key] && g_input->held_ms(key) >= kLongPressMs) {
      g_long_sent[key] = true;
      deliver("long", g_input->held_ms(key), key);
    }
  }

  // A press that never came is answered with the deadline, never with silence:
  // an app waiting for ever is an app that cannot be closed (docs/30).
  if (g_waiting && now >= g_wait_deadline_ms) {
    g_waiting = false;
    luabind::button_failed("E_TIMEOUT");
  }
}

}  // namespace button
}  // namespace kernel
