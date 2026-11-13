// The card of the system that hangs from the bar: it opens with a drag down from the titlebar and goes with a drag up
// (docs/30).
#include "kernel/panel.h"

#include <string.h>

#include "kernel/apps.h"
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
// **I quaranta cap amunt per tancar-la, amb una condició** (0.179.49): the owner asked for the swipe up *«desde fora del
// panell»* and that condition is the whole of it. 0.179.42 took a drag up away because it was the gesture the page under the
// card uses for its own scroll; since then **the page does not run while the card is out** (that same version), so a swipe
// up below the card belongs to nobody and it is the person saying «this again». What still does not close it is a drag that
// begins **on** the card: those rows are the card's (docs/30, docs/50).
const int16_t kPushPx = 40;

// **The handle of the card, at the bottom of it** (0.179.24): a black bar in the same ink as the line that closes the card,
// centred and fourteen rows above it. It is the shape the bar of the system has, and since 0.179.49 it is a lie that can be
// believed again: the swipe up that puts the card away starts outside it —on the page below or on the bar, which are the two
// places the finger that pulls it out comes from— and a drag that begins **on** the rows of the card is the card's and does
// nothing (docs/30, docs/92).
const uint16_t kHandleW = 96;
const uint16_t kHandleH = 6;
const uint16_t kHandleGap = 14;
// The two rows that close the card, which are the same line the bar has under itself.
const uint16_t kLineH = 2;

// **The button of the corner** (0.179.23, and its shape in 0.179.26 and 0.179.31): a **button of the system** —a key of
// sixty-four a side, painted by the one function every button of this system is painted by— with the ink of the card: paper
// face, ink shadow under it, ink frame and the word in ink. It was a disc of forty drawn by hand and then a square with a
// cross of pixels, and a system that has a button cannot have a second way of drawing one (the owner, 12/11). **Twenty-four
// rows and twenty-four columns off the corner**: the first version was fourteen from the edge and he found it small and too
// close to the corners of the glass (docs/30, docs/45, docs/92).
const int16_t kCrossSide = 64;       // the side of the button, so what a finger lands on is a key
const uint16_t kCrossTop = 24;
const uint16_t kCrossRight = 24;
// **I el de la tornada, a l'esquerra** (0.179.52): the owner: *«Afegir botó de "back" al panell lliscant… A la part esquerra del
// panell, a la dreta el "close" com fins ara. El back ha de ser una icona, com el close»*. The same key on the other side, with
// the same margin and the same rows, so the two of them are one pair and not two numbers that happen to be close.
const uint16_t kBackLeft = 24;
// **The word and its steps**: the box leaves thirty-two pixels of word —the sixteen of air a button has on each side— and
// `X` at four steps of the face is twenty-two, which is the measure the two numbers were chosen with. A bigger step is cut
// by the control and a smaller one is a dot in a box.
const char kCrossWord[] = "X";
const uint8_t kCrossScale = 4;
// **La icona de la tornada és una fletxa i no un dibuix d'aquest fitxer** (0.179.52): `0x81`, which is the leftwards arrow of this
// system —the one byte CP1252 leaves empty and that the face answers with `U+2190` (0.158.0), so it is a character like the `X`
// and not three bytes of UTF-8 that this system would read as three characters (`â†↓`, found on the glass)— at three steps,
// because an arrow is wider than an `X` and at four the frame of the control cuts it. **Una icona i no una paraula**: «back» is
// the word of a language and an arrow is not.
const char kBackWord[] = "\x81";
const uint8_t kBackScale = 3;

bool g_open = false;

// **A third of what the bar leaves**: enough for the rows of switches that come later and little enough that the page
// under it is still the page of the app (0.179.22).
uint16_t card_height() {
  const uint16_t glass = display::height();
  const uint16_t top = bar::height();
  if (glass <= top) return 0;
  return static_cast<uint16_t>((glass - top) / 3);
}

// **The word of the control** (0.179.31): the button of the system draws its own word, centred and cut at its frame, so there
// is no cross of pixels here any more —`paint_the_cross` was a drawing of this file and it is gone—. What is left is the
// box and the position.
//
// **Where the button is, in the frame of the view** (0.179.23): the fingers arrive with the rows of the bar taken off and the
// card is painted with them put back, so a hit test that mixed the two frames would be a button a whole bar away from the
// thing the person aimed at (docs/30). The two numbers are **the middle of the button**, which is what a tap aims at.
int16_t cross_x() { return static_cast<int16_t>(ui::width()) - static_cast<int16_t>(kCrossRight) - kCrossSide / 2; }
int16_t cross_y() { return static_cast<int16_t>(kCrossTop) + kCrossSide / 2; }
int16_t back_x() { return static_cast<int16_t>(kBackLeft) + kCrossSide / 2; }

// **The cross is a button only when there is a page it can close** (0.179.25): over a launcher —the one page that is not left— and
// over no page at all, the corner of the card is paper, and a tap there is a tap on the card. A button that does nothing is worse
// than no button: it says the system can do something it cannot (docs/30, docs/45).
bool the_cross_is_there() { return shell::the_page_in_front_can_be_left(); }

// **The button is the square a finger lands on** (0.179.26): the ink and the hit test are the same rectangle, so what a person
// sees is what a person presses.
bool in_a_button(int16_t cx, int16_t cy, uint16_t x, uint16_t y) {
  const int32_t half = kCrossSide / 2;
  const int32_t left = static_cast<int32_t>(cx) - half;
  const int32_t top = static_cast<int32_t>(cy) - half;
  const int32_t px = static_cast<int32_t>(x);
  const int32_t py = static_cast<int32_t>(y);
  return px >= left && px < left + kCrossSide && py >= top && py < top + kCrossSide;
}

bool in_the_cross(uint16_t x, uint16_t y) {
  return the_cross_is_there() && in_a_button(cross_x(), cross_y(), x, y);
}

// **La tornada és un botó només quan hi ha una vista a sota** (0.179.52): an app with one page has nowhere to go back to, and a
// button that does nothing is worse than no button —the same rule the cross follows over a launcher (0.179.25)—. What it asks is
// the number the system already keeps for every app: the views on top of its first page (docs/30, docs/45).
bool the_back_is_there() { return apps::views() > 0; }

bool in_the_back(uint16_t x, uint16_t y) {
  return the_back_is_there() && in_a_button(back_x(), cross_y(), x, y);
}

// **Whether the finger landed on the card** (0.179.42): the card hangs from the bar, so its rows are the first
// `card_height()` rows of the view and every row of the page below that is outside it. The bar itself is outside too —it is
// the handle of the card and not the card—, and the flag that says so is the chip's own row kept before the frame was
// turned, because a finger on the bar arrives at the top rows of the view and the numbers cannot tell the two apart
// (docs/30).
bool on_the_card(const touch::Gesture& gesture) {
  if (gesture.on_the_bar) return false;
  return gesture.y < card_height();
}

// **The button, painted by the system** (0.179.31): `ui::paint_a_button` is the drawing of every button of this tree —the
// box in the colour of its shadow with the face lifted four pixels, the frame on the face, the word centred in what the air
// leaves— and the panel only says where it is and with which ink. The `X` is the control's word and it is not a shape of
// this file: a cross of pixels was a second button in a system that has one (the owner, 12/11, docs/30, docs/92).
void paint_the_button(hal::ISurface& surface, int16_t cx, int16_t cy, const char* word, uint8_t scale) {
  const uint16_t left = static_cast<uint16_t>(cx - kCrossSide / 2);
  const uint16_t top = static_cast<uint16_t>(cy - kCrossSide / 2);
  ui::paint_a_button(surface, left, top, static_cast<uint16_t>(kCrossSide), static_cast<uint16_t>(kCrossSide),
                     /*background=*/15, /*border=*/0, /*pressed=*/false, /*focused=*/false, /*primary=*/false,
                     word, /*color=*/0, scale);
}

void paint() {
  hal::ISurface* surface = display::surface();
  const uint16_t rows = card_height();
  if (surface == nullptr || rows == 0) return;
  const uint16_t width = surface->width();
  const uint16_t top = bar::height();
  surface->fill_rect(0, top, width, rows, 15);
  surface->fill_rect(0, static_cast<uint16_t>(top + rows - kLineH), width, kLineH, 0);
  surface->fill_rect(static_cast<uint16_t>((width - kHandleW) / 2),
                     static_cast<uint16_t>(top + rows - kLineH - kHandleGap - kHandleH), kHandleW, kHandleH, 0);
  // **I els dos botons de la targeta**, on the rows of the card and not of the view: the arrow of the way back at the left, when
  // there is a view under the page, and the cross of the way out at the right, when the page can be left. Each one is a key the
  // system paints with the same function every button of this system is painted with (0.179.52, docs/30).
  if (the_back_is_there()) {
    paint_the_button(*surface, back_x(), static_cast<int16_t>(top + cross_y()), kBackWord, kBackScale);
  }
  if (the_cross_is_there()) {
    paint_the_button(*surface, cross_x(), static_cast<int16_t>(top + cross_y()), kCrossWord, kCrossScale);
  }
  // **`gc16` and not `gld16`**: the card is the whole box and it has to come out clean the first time, which is what the
  // strip of the bar asks for when it paints itself whole (docs/50, docs/92).
  display::present(hal::UpdateMode::kGc16);
}

void put_it_away();

// **La fletxa torna l'app a la vista de sota** (0.179.52): `shell::go_back_one_view` is the door the pinch and the three taps
// already use for «one page back» —the app gives the top page back, hears `on_back(depth)` and repaints the one below—, so the
// button is a way of asking for the same thing and not a second behaviour (docs/30, docs/45).
//
// **I la targeta marxa abans que l'app pinti**: the hold of the page is dropped **without a paint** and the whole page is owed,
// so the present the app makes inside `on_back` is the page whole and the card goes with it. A paint here and another one there
// would be two passes of this glass for one finger, and the second would be the app's own (docs/50, docs/92).
void go_back_a_view() {
  if (!the_back_is_there()) return;
  g_open = false;
  ui::forget_the_page_hold();
  ui::owe_the_page();
  shell::go_back_one_view();
  log_printf(pal::kLogInfo, "PANEL", "the arrow: the app went to the view below");
}

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
  // **And it says so** (0.179.42): this is the one close that is not the person's —no finger, no cross—, and a card that
  // disappeared with nothing in the log was a card the owner could not account for. It can only come from the system now:
  // while the card is out the app's turn is not run, so the app cannot drop the hold of the page it is behind.
  if (g_open && !ui::page_held()) {
    g_open = false;
    log_printf(pal::kLogWarn, "PANEL", "the card went with the page it was over");
    return;
  }
  if (!g_open) {
    // **In full screen there is no handle to take hold of**: the strip of the bar is not on the glass, and a page that covers
    // everything does not offer the system's card (docs/46 D1).
    if (!bar::visible()) return;
    touch::Gesture gesture;
    if (!touch::take_drag_down_from_the_bar(kPullPx, gesture)) return;
    pull_it_out();
    // **I EL QUE HA PASSAT ABANS DE LA TARGETA NO LA POT TANCAR** (0.179.44): the gestures of the queue are the ones the
    // page was told about or nobody was, and a tap of a moment ago is a tap that happened **before** there was a card to
    // dismiss. It was the fault that made the card open and close on the same pull: the drag pulled it out, and the tap
    // that was waiting in the queue put it away on the very next turn, with no finger on the card at all —the board's log
    // showed `pulled down` and `put away` with nothing in between— (docs/30, docs/50).
    touch::forget_gestures();
    return;
  }
  // **The ways out are the person's** (0.179.42, 0.179.49). The owner: *«Ningú ha de poder tancar el panell excepte l'usuari,
  // picant fora de la zona desplegada»*, and then *«afegeix també el swipe up desde fora del panell, que fa que el tancament
  // sigui molt més natural»*. So, while the card is out:
  //   - a **tap on the button of the corner** closes the app under it, and it is the only thing on the card that answers a
  //     finger (`in_the_cross` is false over a launcher, where the button is not painted at all);
  //   - a **tap outside its rows** —the page below it, or the bar it hangs from— puts the card away. A tap **on** the card is
  //     the card's: the switches that come later will take their taps, and the rows are not a thing to dismiss with;
  //   - and a **drag up that begins outside it** puts it away too. That is the gesture the page under the card would use for
  //     its own scroll —which is why 0.179.42 took it away— and the reason it is back is that the same version stopped the
  //     app: while the card is out there is no scroll underneath, so a hand that swipes up there is telling the card to go.
  //     A drag that begins **on** the card is still the card's and does nothing.
  // What is left is nobody's: the app hears none of it, and its turn is not run while the card is out (`kernel.cpp`), so
  // nothing but this function can close the card (docs/30).
  touch::Gesture gesture;
  while (touch::take_gesture(gesture)) {
    if (gesture.kind == nullptr) continue;
    if (strcmp(gesture.kind, "tap") == 0) {
      if (in_the_cross(gesture.x, gesture.y)) {
        touch::forget_gestures();
        close_the_app();
        return;
      }
      if (in_the_back(gesture.x, gesture.y)) {
        touch::forget_gestures();
        go_back_a_view();
        return;
      }
      if (!on_the_card(gesture)) {
        touch::forget_gestures();
        put_it_away();
        return;
      }
      continue;
    }
    if (strcmp(gesture.kind, "drag") != 0 || on_the_card(gesture)) continue;
    // **Cap amunt i més amunt que de costat**, the same shape the pull has: a finger that walks the page sideways is not
    // asking for the card to go anywhere.
    const int32_t up = -static_cast<int32_t>(gesture.dy);
    const int32_t sideways = (gesture.dx < 0) ? -static_cast<int32_t>(gesture.dx) : gesture.dx;
    if (up < kPushPx || up < sideways) continue;
    touch::forget_gestures();
    put_it_away();
    return;
  }
}

}  // namespace panel
}  // namespace kernel

#endif  // LUNOK_GLASS
