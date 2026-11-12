// The card of the system that hangs from the bar: it opens with a drag down from the titlebar and goes with a drag up
// (docs/30).
#include "kernel/panel.h"

#include "kernel/bar.h"
#include "kernel/display.h"
#include "kernel/log.h"
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

bool g_open = false;

// **A third of what the bar leaves**: enough for the rows of switches that come later and little enough that the page
// under it is still the page of the app (0.179.22).
uint16_t card_height() {
  const uint16_t glass = display::height();
  const uint16_t top = bar::height();
  if (glass <= top) return 0;
  return static_cast<uint16_t>((glass - top) / 3);
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
  // **`gc16` and not `gld16`**: the card is the whole box and it has to come out clean the first time, which is what the
  // strip of the bar asks for when it paints itself whole (docs/50, docs/92).
  display::present(hal::UpdateMode::kGc16);
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
