// The card of the system that hangs from the bar: it opens with a drag down from the titlebar and goes with a drag up
// (docs/30).
#include "kernel/panel.h"

#include "kernel/bar.h"
#include "kernel/display.h"
#include "kernel/log.h"
#include "kernel/shell.h"
#include "kernel/touch.h"
#include "kernel/ui.h"
#include "pal/pal.h"

// **Nothing of this file exists on a board without a glass** (0.179.9): the panel is rows of a page, and the four boards
// of the project without a panel have no page to put it on. The question is asked of the board and not of a name in this
// file: `LUNOK_GLASS` is one line of its `board/identity.h`, and it is what decides which translation units exist at all
// (docs/50, docs/75).
#include "board/identity.h"

#if LUNOK_GLASS

namespace kernel {
namespace panel {
namespace {

// **How far the finger has to go to be a pull** (0.179.22): sixty pixels down from the bar, which is a fifth of the card
// and a movement nobody makes by accident, and forty up to put it away, which is less because the card is already out and
// the person is telling the system something it is waiting to hear (docs/30).
const int16_t kPullPx = 60;
const int16_t kPushPx = 40;

// The handle of the card: a black bar at the top of it, in the same ink as the line under the bar, centred. It is what
// says the card is a card and where the finger that made it is (docs/30, docs/92).
const uint16_t kHandleW = 96;
const uint16_t kHandleH = 6;
const uint16_t kHandleTop = 14;
// The two rows that close the card, which are the same line the bar has under itself.
const uint16_t kLineH = 2;

// **The cross of the corner** (0.179.23): a disc of ink of forty pixels with a white cross in it, in the right corner because
// that is the one a right hand reaches without covering the card, on the rows of the handle and with the same margin from the
// edge (docs/30).
const int16_t kCrossR = 20;
const int16_t kCrossHalf = 9;      // the arm of the cross, nine pixels each way from the middle
const uint16_t kCrossTop = 14;
const uint16_t kCrossRight = 14;

bool g_open = false;

// **A third of what the bar leaves**: enough for the rows of switches that come later and little enough that the page
// under it is still the page of the app (0.179.22).
uint16_t card_height() {
  const uint16_t glass = display::height();
  const uint16_t top = bar::height();
  if (glass <= top) return 0;
  return static_cast<uint16_t>((glass - top) / 3);
}

// **Where the cross is, in the frame of the view** (0.179.23): the fingers arrive with the rows of the bar taken off and the
// card is painted with them put back, so a hit test that mixed the two frames would be a button a whole bar away from the
// thing the person aimed at (docs/30).
int16_t cross_x() { return static_cast<int16_t>(ui::width()) - static_cast<int16_t>(kCrossRight) - kCrossR; }
int16_t cross_y() { return static_cast<int16_t>(kCrossTop) + kCrossR; }

bool in_the_cross(uint16_t x, uint16_t y) {
  const int32_t dx = static_cast<int32_t>(x) - cross_x();
  const int32_t dy = static_cast<int32_t>(y) - cross_y();
  return (dx * dx + dy * dy) <= static_cast<int32_t>(kCrossR) * kCrossR;
}

// **A disc of ink, row by row**: the surface of the driver has no circle, and the circle of this button is a span a row — the
// widest one that is inside the radius at that height, walked down from the half of it so the edge is round and not a
// staircase (docs/30, docs/50).
void fill_disc(hal::ISurface& surface, int16_t cx, int16_t cy, int16_t r, uint8_t level) {
  for (int16_t row = -r; row <= r; ++row) {
    int16_t half = 0;
    while ((half + 1) * (half + 1) + row * row <= r * r) ++half;
    surface.fill_rect(static_cast<uint16_t>(cx - half), static_cast<uint16_t>(cy + row),
                      static_cast<uint16_t>(2 * half + 1), 1, level);
  }
}

// **The two strokes of the cross**: forty-five degrees, two pixels wide, and one pixel a step, which is what makes them a line
// and not a row of dots. An e-ink glass of sixteen levels has no room for antialiasing and this is the shape of a cross at this
// size (docs/30, docs/50).
void paint_the_cross(hal::ISurface& surface, int16_t cx, int16_t cy) {
  fill_disc(surface, cx, cy, kCrossR, 0);
  for (int16_t i = -kCrossHalf; i <= kCrossHalf; ++i) {
    for (int16_t t = 0; t < 2; ++t) {
      surface.pixel(static_cast<uint16_t>(cx + i + t), static_cast<uint16_t>(cy + i), 15);
      surface.pixel(static_cast<uint16_t>(cx + i), static_cast<uint16_t>(cy + i + t), 15);
      surface.pixel(static_cast<uint16_t>(cx + i + t), static_cast<uint16_t>(cy - i), 15);
      surface.pixel(static_cast<uint16_t>(cx + i), static_cast<uint16_t>(cy - i + t), 15);
    }
  }
}

void paint() {
  hal::ISurface* surface = display::surface();
  const uint16_t rows = card_height();
  if (surface == nullptr || rows == 0) return;
  const uint16_t width = surface->width();
  const uint16_t top = bar::height();
  surface->fill_rect(0, top, width, rows, 15);
  surface->fill_rect(0, static_cast<uint16_t>(top + rows - kLineH), width, kLineH, 0);
  surface->fill_rect(static_cast<uint16_t>((width - kHandleW) / 2), static_cast<uint16_t>(top + kHandleTop),
                     kHandleW, kHandleH, 0);
  // **And the cross of the corner**, on the rows of the card and not of the view: the one control the panel has until the
  // switches of the system come (docs/30).
  paint_the_cross(*surface, cross_x(), static_cast<int16_t>(top + cross_y()));
  // **`gc16` and not `gld16`**: the card is the whole box and it has to come out clean the first time, which is what the
  // strip of the bar asks for when it paints itself whole (docs/50, docs/92).
  display::present(hal::UpdateMode::kGc16);
}

void put_it_away();

// **The cross closes the app of the page under the card** (0.179.23), and it asks the one road out of a page for it: a page
// with another one on top gives it back, a launcher is refused with the line the pinch is refused with, and the page that
// asked for this app takes the glass again (docs/30, docs/45).
void close_the_app() {
  const shell::LeaveOutcome out = shell::leave_one_page();
  // **A door that did not open leaves the card where it is**: a launcher is the one page that is not left, and a page that did
  // not go is a page the panel is still over —the refusal is said in the line of the road out and not by closing the card— (docs/30,
  // docs/45).
  if (out == shell::LeaveOutcome::kNothing || out == shell::LeaveOutcome::kLauncher) return;
  // **And the card goes with the page it was over**: when the system already owes a whole page —the page below a view that
  // went back, the page of the app that asked for this one— that paint is the one that erases the card, and a close does not
  // pay for two washes of the glass. Anywhere else the page under the card is still there and it comes back whole (docs/50,
  // docs/92).
  if (out == shell::LeaveOutcome::kBack || out == shell::LeaveOutcome::kClosedHome) {
    g_open = false;
    ui::forget_the_page_hold();
    log_printf(pal::kLogInfo, "PANEL", "the cross: %s",
               (out == shell::LeaveOutcome::kBack) ? "a page went back" : "the app is closed and its page comes back");
    return;
  }
  log_printf(pal::kLogInfo, "PANEL", "the cross: the app is closed");
  put_it_away();
}

void pull_it_out() {
  if (g_open) return;
  g_open = true;
  // **The page is held before the card is painted** (0.179.22): a frame of the app that arrived between the two would be a
  // frame the card has to erase, and the e-ink would pay it twice (docs/30, docs/50).
  ui::hold_the_page(true);
  paint();
  log_printf(pal::kLogInfo, "PANEL", "pulled down: the card is %u rows under the bar",
             static_cast<unsigned>(card_height()));
}

void put_it_away() {
  if (!g_open) return;
  g_open = false;
  // **The page comes back whole** (0.179.22): the card left ink that is not the page's, and the frame the app drew while it
  // was out never reached the glass. One wash of the view is the two of them.
  ui::hold_the_page(false);
  log_printf(pal::kLogInfo, "PANEL", "put away: the page is back");
}

}  // namespace

void boot() {
  // A boot is a glass that nobody has touched: the card of the run before is not on it and the page owes nothing.
  g_open = false;
}

bool open() { return g_open; }

uint16_t height() { return card_height(); }

void poll() {
  if (!display::available()) return;
  // **A page that went away takes the card with it** (0.179.22): another app took the view, or the glass turned, and the
  // hold was dropped by whoever did it. What is on the glass is washed by that change, so the card goes without a paint.
  if (g_open && !ui::page_held()) {
    g_open = false;
    return;
  }
  if (!g_open) {
    // **In full screen there is no handle to take hold of**: the strip of the bar is not on the glass, and a page that covers
    // everything does not offer the system's card (docs/46 D1).
    if (!bar::visible()) return;
    touch::Gesture gesture;
    if (!touch::take_drag_down_from_the_bar(kPullPx, gesture)) return;
    pull_it_out();
    return;
  }
  touch::Gesture gesture;
  // **Up or a tap**: the drag is the gesture the panel was pulled out with and the tap is the one a person tries when the
  // card has no button in it yet (0.179.22). Both are the system's while the card is out (docs/30).
  if (touch::take_drag_up(kPushPx, gesture)) {
    put_it_away();
    return;
  }
  if (touch::take_gesture_of("tap", gesture)) {
    // **A tap on the cross closes the app and a tap anywhere else puts the card away**: the rows of switches that come will take
    // their own taps, and until then the whole card is one thing to dismiss (0.179.23, docs/30).
    if (in_the_cross(gesture.x, gesture.y)) {
      close_the_app();
      return;
    }
    put_it_away();
    return;
  }
  // **What is left is nobody's**: an app that reacted under the card would be acting on a page the person cannot see
  // (docs/30).
  touch::forget_gestures();
}

}  // namespace panel
}  // namespace kernel

#endif  // LUNOK_GLASS
