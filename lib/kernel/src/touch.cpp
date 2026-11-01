// The glass: the frame of the last turn and the gestures made out of it (docs/30, docs/50).
#include "kernel/touch.h"

#include "kernel/display.h"
#include "kernel/meter.h"
#include "kernel/orientation.h"
#include "kernel/ui.h"
#include "luabind/luabind.h"

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

// A finger that lands and leaves inside this is a `tap`. The window is the button's, because two
// different windows on one board is a person learning two things (docs/30).
const int32_t kTapMs = 400;
// Held this long and it is a `long`, said once, when the threshold is crossed and not when the
// finger leaves: the same rule the button follows (docs/30).
const int32_t kLongMs = 1000;
// How far a finger may move and still be the same press, in the chip's own units, which are pixels
// of the panel (docs/50).
const int32_t kSlopPx = 20;

bool g_down = false;
int32_t g_down_ms = 0;
uint16_t g_start_x = 0;
uint16_t g_start_y = 0;
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

void emit(const char* kind, uint16_t x, uint16_t y, int16_t dx, int16_t dy, int16_t spread,
          int32_t held_ms, uint8_t fingers) {
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
}

// A tap that is alone becomes a `tap`; two in a row inside the window are a `double` and three are a
// `triple`, which is said at the third one and does not wait for a fourth (docs/30).
void settle_taps(int32_t now) {
  if (g_taps == 0) return;
  if ((now - g_last_tap_ms) <= kMultiTapMs) return;
  const char* kind = (g_taps >= 3) ? "triple" : ((g_taps == 2) ? "double" : "tap");
  emit(kind, g_tap_x, g_tap_y, 0, 0, 0, g_tap_held_ms, 1);
  g_taps = 0;
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
  const orientation::Frame view = {ui::width(), ui::height()};
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
      if (page.x >= off_x) page.x = static_cast<uint16_t>(page.x - off_x);
      if (page.y >= off_y) page.y = static_cast<uint16_t>(page.y - off_y);
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

bool boot(hal::IPlatform& platform) {
  g_glass = platform.touch();
  g_clock = platform.clock();
  g_count = 0;
  g_down = false;
  g_queue_count = 0;
  g_taps = 0;
  if (g_glass == nullptr) return false;
  luabind::set_touch_ops(luabind::TouchOps{caps_for_lua, points_for_lua, gesture_for_lua});
  return true;
}

bool available() { return g_glass != nullptr; }

void poll() {
  if (g_glass == nullptr) return;
  // **The read of the chip, and only the read**: the gestures are arithmetic over two frames and cost
  // nothing, so what is measured here is what the I2C costs once per turn (docs/30, docs/50).
  meter::Scope scope(meter::kTouchRead);
  g_count = g_glass->read(g_frame, kMaxPoints);
  turn_frame();
  const int32_t now = now_ms();

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
    // The finger left: short and still is a tap, moved is a drag with the way it went (docs/30).
    const int32_t held = now - g_down_ms;
    if (g_moved) {
      emit("drag", g_start_x, g_start_y, static_cast<int16_t>(static_cast<int32_t>(g_last_x) -
                                                             static_cast<int32_t>(g_start_x)),
           static_cast<int16_t>(static_cast<int32_t>(g_last_y) -
                                static_cast<int32_t>(g_start_y)),
           0, held, 1);
    } else if (held <= kTapMs) {
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

}  // namespace touch
}  // namespace kernel
