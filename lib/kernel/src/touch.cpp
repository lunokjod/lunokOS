// The glass: the frame of the last turn and the gestures made out of it (docs/30, docs/50).
#include "kernel/touch.h"

#include <string.h>

#include "kernel/display.h"
#include "kernel/log.h"
#include "kernel/meter.h"
#include "kernel/orientation.h"
#include "kernel/ui.h"
#include "luabind/luabind.h"

// **Nothing of this file exists on a board without a glass** (0.179.9): this is the finger —a panel turned into
// taps, long presses and drags in the turns of the loop— and a board with no screen can never run a line of it.
// The question is asked of the board and not of a name in this file: `LUNOK_GLASS` is one line of its
// `board/identity.h`, and it is what decides which translation units exist at all. The four boards of the project
// without a panel —the two ATOMs, the bare ESP32 and the T-Koala— carry neither this code nor the libraries it
// pulls (docs/50, docs/75).
#include "board/identity.h"

#if LUNOK_GLASS

namespace kernel {
namespace touch {
namespace {

hal::ITouch* g_glass = nullptr;
hal::IClock* g_clock = nullptr;

// The frame of the last turn. It is kept because the chip gives a frame **once**: it hands it over
// and forgets it, so the kernel takes it in the turn and both the gestures and the app read this
// copy (docs/50).
hal::TouchPoint g_frame[kMaxPoints];
uint8_t g_count = 0;

// A finger that lands and leaves without moving is a `tap`, and **the four hundred milliseconds that used to be its
// ceiling are gone** (0.179.29): a press held for six hundred and lifted was neither a tap nor a `long`, so the app heard
// nothing at all while the control under it clicked. `held_ms` says how long it was, which is the number an app needs.
const int32_t kLongMs = 1000;
// How far a finger may move and still be the same press, in the chip's own units, which are pixels
// of the panel (docs/50).
const int32_t kSlopPx = 20;

bool g_down = false;
// **L'AMO DEL CONTACTE** (0.179.34): who the finger that is on the glass belongs to, decided on the frame it landed and
// not asked again. The three that exist are the page (its controls), the band of the keyboard, and the system —the strip of
// the bar and a card over the page, which deliver to nobody here because the panel and the bar read the queue—. It is what
// makes a swipe that ends on a button nobody's: the owner was decided where the finger began (docs/30, docs/90).
enum : uint8_t { kNoContact = 0, kThePage = 1, kTheKeyboard = 2, kTheSystem = 3 };
uint8_t g_contact = kNoContact;
int32_t g_down_ms = 0;
uint16_t g_start_x = 0;
uint16_t g_start_y = 0;
// **The bar is not in the frame of an app** (0.179.22): a finger that lands on the strip of the system keeps its own y —the
// view's frame only subtracts the bar from the fingers that are below it—, so "above the view" is the one thing the turned
// frame cannot say and this flag is what the drag of the shade reads (docs/30).
bool g_on_the_bar = false;
bool g_start_on_the_bar = false;
uint16_t g_last_x = 0;
uint16_t g_last_y = 0;
bool g_moved = false;
bool g_long_said = false;

// Two fingers are a gesture of their own and they are tracked as a **pair**: the middle of the two
// and how far apart they are. A pinch is the change of that distance, and it is nothing the chip
// tells us —Goodix's own gestures are not read on this board— so it is arithmetic over two points
// (docs/30, docs/50).
bool g_pair = false;
int32_t g_pair_ms = 0;
int32_t g_pair_start_mid_x = 0;
int32_t g_pair_start_mid_y = 0;
int32_t g_pair_last_mid_x = 0;
int32_t g_pair_last_mid_y = 0;
int32_t g_pair_start_gap = 0;
int32_t g_pair_last_gap = 0;
// A pair can do two things, and both are said in pixels: it can move **together** (a drag of two
// fingers, a pan) or it can come **closer or further apart** (a pinch). Under this the pair did
// neither, and it was a two-finger tap, which `two` already said (docs/30).
const int32_t kPairPx = 15;

// Gestures wait their turn in a small queue and not in one slot: a tap is **held back** for the length
// of a double-tap window —it is not known to be a single tap until that window closes— and something
// else may happen while it waits (docs/30).
const uint8_t kQueueLength = 4;
Gesture g_queue[kQueueLength];
uint8_t g_queue_count = 0;

// A tap is counted and not said: two taps are a `double`, three are a `triple`, and until the window
// closes a tap is not known to be alone. **The cost is said here**: a `tap` arrives **350 ms after**
// the finger leaves. The other way —saying it at once and amending it later— would make every app undo
// what it already did with the first tap, and that is worse than a third of a second (docs/30).
const int32_t kMultiTapMs = 350;
uint8_t g_taps = 0;
int32_t g_last_tap_ms = 0;
uint16_t g_tap_x = 0;
uint16_t g_tap_y = 0;
int32_t g_tap_held_ms = 0;

int32_t now_ms() {
  return (g_clock != nullptr) ? static_cast<int32_t>(g_clock->monotonic_ms()) : 0;
}

// **Els tocs que esperen la finestra, dits d'una vegada** (0.179.29): it is the same sentence `settle_taps` says when the
// window closes, and `emit` asks for it as well when **another gesture** arrives, because a drag between two taps is not a
// double.
void say_the_taps();

void emit(const char* kind, uint16_t x, uint16_t y, int16_t dx, int16_t dy, int16_t spread,
          int32_t held_ms, uint8_t fingers) {
  // **Un altre gest tanca la finestra dels tocs** (0.179.29): a tap waits three hundred and fifty milliseconds to know
  // whether it is alone, and a person who does something else in that time has answered for it —a tap, a short drag and
  // another tap were said as a `drag` and a `double`, and the page heard two taps where there were two taps but also a
  // drag between them—. What was waiting goes in the queue **before** the gesture that closed the window, which is the
  // order it happened in (docs/30, docs/90).
  if (kind != nullptr && strcmp(kind, "tap") != 0 && strcmp(kind, "double") != 0 &&
      strcmp(kind, "triple") != 0) {
    say_the_taps();
  }
  if (g_queue_count == kQueueLength) {
    // Full: the oldest goes. Losing a gesture is bad; losing the **newest** is worse, because the last
    // thing a person did is the one they are waiting for (docs/30).
    for (uint8_t i = 1; i < kQueueLength; ++i) g_queue[i - 1] = g_queue[i];
    --g_queue_count;
  }
  Gesture& slot = g_queue[g_queue_count++];
  slot.kind = kind;
  slot.x = x;
  slot.y = y;
  slot.dx = dx;
  slot.dy = dy;
  slot.spread = spread;
  slot.held_ms = held_ms;
  slot.fingers = fingers;
  // **A gesture made by one finger also says where it landed** (0.179.22): a pair's middle is not on the bar, and the
  // gestures of one finger are the ones the shade is made of (docs/30).
  slot.on_the_bar = (fingers < 2) && g_start_on_the_bar;
  // **I cada gest diu què és, on i amb quants dits** (0.179.28): every gesture of this system is made here and nowhere
  // else, so this one line is the whole of what a person with the board in their hand cannot see —the chip gives points
  // and the names are the kernel's—. It is `kLogInfo` and not a debug line because a gesture is **one event and not a
  // frame**: a drag is said once, on the lift, and so is a tap when the window of the multi-tap closes, so a panel moved
  // all over costs a handful of lines and not one a turn (docs/30, docs/85).
  log_printf(pal::kLogInfo, "TOUCH", "%s %u,%u dx %d dy %d spread %d, %u finger(s), %d ms%s", kind,
             static_cast<unsigned>(x), static_cast<unsigned>(y), static_cast<int>(dx),
             static_cast<int>(dy), static_cast<int>(spread), static_cast<unsigned>(fingers),
             static_cast<int>(held_ms), slot.on_the_bar ? " on the bar" : "");
}

// A tap that is alone becomes a `tap`; two in a row inside the window are a `double` and three are a
// `triple`, which is said at the third one and does not wait for a fourth (docs/30).
void say_the_taps() {
  if (g_taps == 0) return;
  const char* kind = (g_taps >= 3) ? "triple" : ((g_taps == 2) ? "double" : "tap");
  g_taps = 0;
  emit(kind, g_tap_x, g_tap_y, 0, 0, 0, g_tap_held_ms, 1);
}

void settle_taps(int32_t now) {
  if (g_taps == 0) return;
  if ((now - g_last_tap_ms) <= kMultiTapMs) return;
  say_the_taps();
}

// The distance between two points, without floating point: the kernel has no maths library and this
// is the one root it needs. It is a Newton step from the bit length downward, which is exact for the
// numbers a glass of this size gives (docs/50).
uint32_t isqrt(uint32_t value) {
  if (value == 0) return 0;
  uint32_t x = value;
  uint32_t y = (x + 1) / 2;
  while (y < x) {
    x = y;
    y = (x + value / x) / 2;
  }
  return x;
}

// **The one place where the fingers are turned into the app's frame** (docs/90): the chip counts in one
// frame, an app draws in a view that starts under the bar of the system, and after this the frame the
// kernel keeps is in the view's coordinates.
//
// **The chip speaks in the page's own frame, and it is the same way round**: there is no quarter turn
// between the fingers and the glass and no mirror either. Measured on the board (1/11, docs/95 E-6) by
// touching the four corners of the portrait page —the top left is the clock— and reading what the
// chip said: 46,28 · 515,49 · 31,926 · 500,920. `x` grows to the right and `y` downwards, exactly
// as the page is drawn, so the only turn left is the one between the page the chip was measured in and
// the page the app asks for.
const uint16_t kChipPage = 270;

void turn_frame() {
  if (g_count == 0) return;
  const orientation::Frame chip = {g_glass->width(), g_glass->height()};
  // **The frame the finger is clamped into is the canvas and not the page left over the keyboard** (0.84.0): `ui.height()`
  // is what an app draws in, and the band of the keyboard is still part of the glass —a finger that lands on it has to
  // arrive at the keyboard with its own y and not squeezed into the last row of the page— (docs/50, docs/90).
  const orientation::Frame view = {ui::canvas().width, ui::canvas().height};
  const uint16_t off_x = ui::canvas().x;
  const uint16_t off_y = ui::canvas().y;
  // Turning the page by 180 degrees turns the finger by 180 degrees too; at `kChipPage` the page and the
  // chip are the same frame and the turn is nothing.
  const uint16_t page_turn =
      static_cast<uint16_t>((kChipPage + 360 - (display::rotation() % 360)) % 360);
  for (uint8_t i = 0; i < g_count; ++i) {
    orientation::Frame turned;
    orientation::Point page = {g_frame[i].x, g_frame[i].y};
    const orientation::Point raw = page;
    const bool placed = orientation::turn(raw, chip, page_turn, turned, page);
    if (placed) {
      // **A finger above the view landed on the bar of the system** (0.179.22): it keeps its own y instead of being moved
      // into the view —the bar is not part of any app— and this is the one place that can tell it apart from a finger on the
      // first rows of the page (docs/30).
      const bool landed = (i == 0) && (page.y < off_y);
      if (i == 0) g_on_the_bar = landed;
      // **And the finger that landed there keeps the bar's rows for the whole gesture**: subtracting the offset from the
      // frames that follow but not from the one it landed on would make a pull of a hundred pixels a pull of fifty-two on a
      // bar of forty-eight, and a finger that never left the strip would be a finger that went up (docs/30).
      const bool on_the_strip = (i == 0) && (g_down ? g_start_on_the_bar : landed);
      if (page.x >= off_x) page.x = static_cast<uint16_t>(page.x - off_x);
      if (page.y >= off_y && !on_the_strip) page.y = static_cast<uint16_t>(page.y - off_y);
      // A finger that lands on the bar belongs to the system, but it is still a finger: it is clamped to
      // the edge of the view instead of vanishing. **A point is never dropped for not fitting what this
      // code expects** — dropping the ones that fell outside was a touch that answered nothing at all
      // (measured on the board, 1/11).
      if (view.width > 0 && page.x >= view.width) page.x = static_cast<uint16_t>(view.width - 1);
      if (view.height > 0 && page.y >= view.height) page.y = static_cast<uint16_t>(view.height - 1);
    }
    g_frame[i].x = page.x;
    g_frame[i].y = page.y;
  }
}

// What the interpreter asked for: **the frame of the view of the app**, which is the frame the fingers
// are given in. The chip counts in its own frame and that one is not an app's business (docs/30, docs/90).
bool caps_for_lua(luabind::TouchInfo& out) {
  if (g_glass == nullptr) return false;
  out.max_points = g_glass->max_points();
  out.width = ui::width();
  out.height = ui::height();
  return true;
}

uint8_t points_for_lua(luabind::TouchPointInfo* out, uint8_t capacity) {
  if (out == nullptr) return 0;
  const uint8_t count = (g_count < capacity) ? g_count : capacity;
  for (uint8_t i = 0; i < count; ++i) {
    out[i].id = g_frame[i].id;
    out[i].x = g_frame[i].x;
    out[i].y = g_frame[i].y;
    out[i].size = g_frame[i].size;
  }
  return count;
}

bool gesture_for_lua(const char** kind, uint16_t& x, uint16_t& y, int16_t& dx, int16_t& dy,
                     int16_t& spread, int32_t& held_ms, uint8_t& fingers) {
  Gesture mine;
  if (!take_gesture(mine)) return false;
  *kind = mine.kind;
  x = mine.x;
  y = mine.y;
  dx = mine.dx;
  dy = mine.dy;
  spread = mine.spread;
  held_ms = mine.held_ms;
  fingers = mine.fingers;
  return true;
}

}  // namespace

// **A frame that is not there yet is not a finger that left.** The chip hands the frame of a scan over once and the
// driver takes it away, so a caller that asks faster than the glass reads "nobody" between two frames of the same
// finger. Reading those gaps as a lift turned one finger held down into a run of presses, releases and taps —a tap on
// a menu picked two options, and a held button let go of itself— (found on the emulator, 3/11). Only a glass that has said
// nothing for a whole scan is a finger that went away; a scan of this panel is ten milliseconds and the window is
// three of them.
const int32_t kQuietMs = 30;
// When a frame was last handed over. `-1` is a glass that has said nothing since the app started or since the last
// finger left.
int32_t g_seen_ms = -1;
// When the chip was last asked. `-1` is a glass nobody has asked yet, and the gate below is what keeps a turn of a
// millisecond from asking a chip that scans every ten (0.174.4, docs/50, docs/95).
int32_t g_asked_ms = -1;

// **The chip is asked once per scan and not once per turn** (0.174.4): the glass scans the panel by itself and keeps
// the frame of its last scan until a read takes it, so a turn that asks again inside the same scan can only be told
// what the last read already said. The emulator counts those reads as `missed` —and a model whose caller asks ten
// times a turn would make the cost of asking disappear, which is the opposite of what it is for— and on the board
// each one is the status of the chip on a hundred-kilohertz bus it shares with the clock, the FRAM and the sensor:
// 400 microseconds of a turn that can be one millisecond (docs/50, docs/95). A glass that does not say how often it
// scans (`scan_ms() == 0`) is asked on every turn, which is what every caller did before.
bool ask_the_chip(int32_t now) {
  const uint32_t scan = g_glass->scan_ms();
  if (scan == 0) return true;
  if (g_asked_ms >= 0 && static_cast<uint32_t>(now - g_asked_ms) < scan) return false;
  g_asked_ms = now;
  return true;
}

bool boot(hal::IPlatform& platform) {
  g_glass = platform.touch();
  g_clock = platform.clock();
  g_count = 0;
  g_down = false;
  g_contact = kNoContact;
  g_queue_count = 0;
  g_taps = 0;
  g_seen_ms = -1;
  g_asked_ms = -1;
  if (g_glass == nullptr) return false;
  luabind::set_touch_ops(luabind::TouchOps{caps_for_lua, points_for_lua, gesture_for_lua});
  return true;
}

bool available() { return g_glass != nullptr; }

void poll() {
  if (g_glass == nullptr) return;
  const int32_t now = now_ms();
  // **A frame that did not come keeps the one that did**: the finger is still there until the glass has been quiet for
  // a whole scan (docs/30, docs/50). And the chip is asked at the rate it can have news and not at the rate the kernel
  // turns (0.174.4): what is measured as `touch.read` is the reads of the chip and not the turns that had nothing to
  // ask it (docs/30, docs/95).
  uint8_t taken = 0;
  if (ask_the_chip(now)) {
    meter::Scope scope(meter::kTouchRead);
    taken = g_glass->read(g_frame, kMaxPoints);
  }
  if (taken > 0) {
    // **A frame of a finger that is already gone is a tap that is over**, and it carries its own release: the chip
    // hands over the frame of its last scan and a finger that came and went between two reads is not on the glass any
    // more. The keyboard hears that release before the press, or the tap after one whose release was missed —while a
    // paint held the turns— lands on a finger that is still held and nobody types it (found on the emulator, 4/11,
    // docs/50, docs/90).
    if (!g_glass->finger_still_down()) ui::keyboard_touch(0, 0, false);
    g_count = taken;
    g_seen_ms = now;
    // **The turn is for the frame that just came and for nothing else**: `turn_frame` writes the view's coordinates over
    // the chip's in the same buffer, so turning a frame that is being kept turned it again on every poll and the finger
    // walked away on its own (found on the emulator, 3/11).
    turn_frame();
  } else if (g_seen_ms < 0 || (now - g_seen_ms) >= kQuietMs) {
    g_count = 0;
    g_seen_ms = -1;
  }
  // **QUI REP L'EVENT ES DECIDEIX EN COMENÇAR I NO ES TORNA A MIRAR** (0.179.34): a contact has **one owner** from its
  // first frame —the strip of the system, the band of the keyboard, or the page— and every frame that comes after goes to
  // that one. It is the rule the owner wrote down: *«l'event, tant gest com toc(s) l'ha de rebre l'objecte UI que hi ha
  // sota quan comença l'acció»*, and both of its halves are needed: a swipe that begins on the bar is the bar's, **and a
  // swipe that ENDS on a button is nobody's**, because the finger began somewhere else and a button that answers the end
  // of a gesture is a button acting on something it never heard (docs/30, docs/90).
  //
  // **The keyboard is why the owner is remembered and not asked again in every frame**: `keyboard_touch` is given the
  // point of the finger, so a drag that began on the page and crossed the band used to type the key it happened to pass
  // over —the band taking a contact that was never its own—. With one owner the band is asked **once**, with the point
  // the finger landed on (docs/90).
  if (g_count == 0) {
    // **El contacte s'ha acabat**: the owner hears the lift, and the next contact decides its own.
    if (g_contact != kNoContact) {
      log_printf(pal::kLogDebug, "TOUCH", "the contact is over (%d was the owner)", static_cast<int>(g_contact));
    }
    if (g_contact == kTheKeyboard) ui::keyboard_touch(0, 0, false);
    if (g_contact == kThePage) luabind::ui_touch_progress(0, 0, false);
    g_contact = kNoContact;
  } else if (g_contact == kNoContact) {
    // **Un contacte nou vol dir que el dit de la banda de abans ja no hi és** (0.179.45): the band keeps `held_` between
    // frames so a finger that stays on a key types once, and a lift that the glass never reported —a frame lost while a
    // paint held the turns, which this panel does by the hundred— left it set for ever; from that moment the band answered
    // `true` to every finger and typed **none** of them, and the dial was refused with the same word (`held_`). A contact
    // that begins is a contact that is not the one before, so the band is told its finger is over before it is asked whose
    // this one is (docs/30, docs/90).
    ui::keyboard_touch(0, 0, false);
    // **La primera passada del contacte, i el propietari es decideix aquí**: `on_the_bar` is the strip of the system,
    // which is not part of any view —the first rows of the bar and the first rows of the page are the same numbers—; then
    // the band of the keyboard, asked with the row the finger landed on, so a page under it cannot be pressed through it;
    // and everything else is the page. **A card of the system over the page** (0.179.22) is the system's too: a button
    // under a card that acts is a button acting on a page nobody is looking at (docs/30, docs/50).
    if (g_on_the_bar || ui::page_held()) {
      g_contact = kTheSystem;
      log_printf(pal::kLogDebug, "TOUCH", "the contact at %u,%u is the system's: bar %d, page held %d",
                 static_cast<unsigned>(g_frame[0].x), static_cast<unsigned>(g_frame[0].y), g_on_the_bar ? 1 : 0,
                 ui::page_held() ? 1 : 0);
    } else if (ui::keyboard_touch(g_frame[0].x, g_frame[0].y, true)) {
      g_contact = kTheKeyboard;
    } else {
      g_contact = kThePage;
      log_printf(pal::kLogDebug, "TOUCH", "the contact at %u,%u is the page's",
                 static_cast<unsigned>(g_frame[0].x), static_cast<unsigned>(g_frame[0].y));
      luabind::ui_touch_progress(g_frame[0].x, g_frame[0].y, true);
    }
  } else if (g_contact == kThePage) {
    luabind::ui_touch_progress(g_frame[0].x, g_frame[0].y, true);
  } else if (g_contact == kTheKeyboard) {
    ui::keyboard_touch(g_frame[0].x, g_frame[0].y, true);
  }
  // `kTheSystem` goes to nobody here: the panel and the bar read the gestures of the queue and the page hears neither the
  // press nor the click (docs/30, docs/50).

  // A tap waiting for its window: when the window closes it is a tap, and if another one arrived inside
  // it, it is a double (docs/30).
  settle_taps(now);

  // Two fingers are a gesture of their own, and while there are two of them the one-finger state
  // machine is **off**: a pinch is not a drag of the finger that happens to be listed first, and a
  // long press of a pair is not a long press (measured on the Paper, 1/11: a two-finger gesture came
  // out as a drag of the first finger, docs/50).
  if (g_count >= 2) {
    const int32_t mid_x = (static_cast<int32_t>(g_frame[0].x) + g_frame[1].x) / 2;
    const int32_t mid_y = (static_cast<int32_t>(g_frame[0].y) + g_frame[1].y) / 2;
    const int32_t gap_x = static_cast<int32_t>(g_frame[0].x) - static_cast<int32_t>(g_frame[1].x);
    const int32_t gap_y = static_cast<int32_t>(g_frame[0].y) - static_cast<int32_t>(g_frame[1].y);
    if (!g_pair) {
      g_pair = true;
      g_pair_ms = now;
      g_pair_start_mid_x = mid_x;
      g_pair_start_mid_y = mid_y;
      g_pair_start_gap = gap_x * gap_x + gap_y * gap_y;
      emit("two", static_cast<uint16_t>(mid_x), static_cast<uint16_t>(mid_y), 0, 0, 0, 0, g_count);
    }
    g_pair_last_mid_x = mid_x;
    g_pair_last_mid_y = mid_y;
    g_pair_last_gap = gap_x * gap_x + gap_y * gap_y;
    g_down = false;
    g_moved = false;
    return;
  }

  if (g_pair) {
    // The pair came apart, or went down to one finger. A pair has **two** things it can do and it is
    // named after the bigger one: move together is a `drag` of two fingers —a pan—, and come closer
    // or further apart is a `pinch`. **Both numbers go in the payload anyway**, so an app that wants
    // the whole story reads `dx`/`dy` and `spread` and not only the name. A pair that did neither
    // was a two-finger tap, which `two` already said (docs/30).
    g_pair = false;
    const int32_t spread = isqrt(g_pair_last_gap) - isqrt(g_pair_start_gap);
    const int32_t moved_x = g_pair_last_mid_x - g_pair_start_mid_x;
    const int32_t moved_y = g_pair_last_mid_y - g_pair_start_mid_y;
    const int32_t abs_spread = (spread < 0) ? -spread : spread;
    const int32_t abs_x = (moved_x < 0) ? -moved_x : moved_x;
    const int32_t abs_y = (moved_y < 0) ? -moved_y : moved_y;
    const int32_t moved = (abs_x > abs_y) ? abs_x : abs_y;
    const char* kind = nullptr;
    if (abs_spread >= kPairPx && abs_spread >= moved) {
      kind = "pinch";
    } else if (moved >= kPairPx) {
      kind = "drag";
    }
    if (kind != nullptr) {
      emit(kind, static_cast<uint16_t>(g_pair_start_mid_x),
           static_cast<uint16_t>(g_pair_start_mid_y), static_cast<int16_t>(moved_x),
           static_cast<int16_t>(moved_y), static_cast<int16_t>(spread), now - g_pair_ms, 2);
    }
  }

  if (g_count == 0) {
    if (!g_down) return;
    // The finger left: still is a tap, moved is a drag with the way it went (docs/30).
    const int32_t held = now - g_down_ms;
    if (g_moved) {
      emit("drag", g_start_x, g_start_y, static_cast<int16_t>(static_cast<int32_t>(g_last_x) -
                                                             static_cast<int32_t>(g_start_x)),
           static_cast<int16_t>(static_cast<int32_t>(g_last_y) -
                                static_cast<int32_t>(g_start_y)),
           0, held, 1);
    } else if (!g_long_said) {
      // **Un dit quiet que s'aixeca és un toc, encara que s'hagi pres amb calma** (0.179.29): the ceiling of four hundred
      // milliseconds this file had (`kTapMs`, now gone) was the definition of a tap, and a finger pressed for six hundred
      // and lifted without moving was **neither a tap nor a long**: the control under it clicked —because the controls work
      // on the frame and not on the name— and the app heard nothing at all, so a page of Lua that reads `touch.gesture()`
      // lost the press a person had made on purpose. `held_ms` already says how long it was, which is the number the app
      // needed. **A `long` is said while the finger is still there** and it is not a tap as well (docs/30, docs/90).
      // Counted and not said yet: see `settle_taps`. A third one is a `triple` at once, because
      // nothing longer than three exists and waiting would only add delay (docs/30).
      g_taps = (g_taps > 0 && (now - g_last_tap_ms) <= kMultiTapMs)
                   ? static_cast<uint8_t>(g_taps + 1)
                   : 1;
      g_last_tap_ms = now;
      g_tap_x = g_start_x;
      g_tap_y = g_start_y;
      g_tap_held_ms = held;
      if (g_taps >= 3) {
        emit("triple", g_tap_x, g_tap_y, 0, 0, 0, g_tap_held_ms, 1);
        g_taps = 0;
      }
    }
    g_down = false;
    g_moved = false;
    return;
  }

  if (!g_down) {
    g_down = true;
    g_down_ms = now;
    g_start_x = g_frame[0].x;
    g_start_y = g_frame[0].y;
    g_start_on_the_bar = g_on_the_bar;
    g_last_x = g_start_x;
    g_last_y = g_start_y;
    g_moved = false;
    g_long_said = false;
    return;
  }
  g_last_x = g_frame[0].x;
  g_last_y = g_frame[0].y;
  const int32_t dx = static_cast<int32_t>(g_last_x) - static_cast<int32_t>(g_start_x);
  const int32_t dy = static_cast<int32_t>(g_last_y) - static_cast<int32_t>(g_start_y);
  if (dx > kSlopPx || dx < -kSlopPx || dy > kSlopPx || dy < -kSlopPx) g_moved = true;
  if (!g_long_said && (now - g_down_ms) >= kLongMs) {
    g_long_said = true;
    emit("long", g_start_x, g_start_y, 0, 0, 0, now - g_down_ms, 1);
  }
}

uint8_t points(hal::TouchPoint* out, uint8_t capacity) {
  if (out == nullptr) return 0;
  const uint8_t count = (g_count < capacity) ? g_count : capacity;
  for (uint8_t i = 0; i < count; ++i) out[i] = g_frame[i];
  return count;
}

bool take_gesture(Gesture& out) {
  if (g_queue_count == 0) return false;
  out = g_queue[0];
  for (uint8_t i = 1; i < g_queue_count; ++i) g_queue[i - 1] = g_queue[i];
  --g_queue_count;
  return true;
}

bool take_gesture_of(const char* kind, Gesture& out) {
  if (kind == nullptr) return false;
  for (uint8_t i = 0; i < g_queue_count; ++i) {
    if (g_queue[i].kind == nullptr || strcmp(g_queue[i].kind, kind) != 0) continue;
    out = g_queue[i];
    // **The ones before it keep the order they had** (0.136.0): a gesture that a person made before the three taps is
    // still theirs, and taking the whole queue would throw away what the app has not read yet (docs/30).
    for (uint8_t j = static_cast<uint8_t>(i + 1); j < g_queue_count; ++j) g_queue[j - 1] = g_queue[j];
    --g_queue_count;
    return true;
  }
  return false;
}

bool take_drag_down_from_the_bar(int16_t min_px, Gesture& out) {
  if (min_px <= 0) return false;
  for (uint8_t i = 0; i < g_queue_count; ++i) {
    const Gesture& mine = g_queue[i];
    if (mine.kind == nullptr || strcmp(mine.kind, "drag") != 0) continue;
    if (!mine.on_the_bar) continue;
    // **Mostly down and not sideways**: a finger that comes down the bar but walks along the page is a page gesture that
    // began one row too high, and a shade that opens on it would be a shade that opens by accident (docs/30).
    const int32_t down = mine.dy;
    const int32_t sideways = (mine.dx < 0) ? -static_cast<int32_t>(mine.dx) : mine.dx;
    if (down < min_px || down < sideways) continue;
    out = mine;
    for (uint8_t j = static_cast<uint8_t>(i + 1); j < g_queue_count; ++j) g_queue[j - 1] = g_queue[j];
    --g_queue_count;
    return true;
  }
  return false;
}

void forget_gestures() { g_queue_count = 0; }

bool take_pinch_in(int16_t min_px, Gesture& out) {
  if (min_px <= 0) return false;
  for (uint8_t i = 0; i < g_queue_count; ++i) {
    if (g_queue[i].kind == nullptr || strcmp(g_queue[i].kind, "pinch") != 0) continue;
    // **Big to small is the one that closes**: positive is apart —it is how every map and every photo app reads
    // it—, so the gesture that comes back is the one whose `spread` is under zero by `min_px` pixels. A pinch that
    // does not come back that far, or that opens, is left in the queue for the app (docs/30).
    if (g_queue[i].spread > static_cast<int16_t>(-min_px)) continue;
    out = g_queue[i];
    for (uint8_t j = static_cast<uint8_t>(i + 1); j < g_queue_count; ++j) g_queue[j - 1] = g_queue[j];
    --g_queue_count;
    return true;
  }
  return false;
}

}  // namespace touch
}  // namespace kernel

#endif  // LUNOK_GLASS
