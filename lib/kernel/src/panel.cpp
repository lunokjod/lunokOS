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

// **Els cinc botons de la targeta** (0.185.0): the arrow of the way back in the left corner and the cross of the way out in the
// right one —the two of 0.179.52— and **the three of the system in a row in the middle**: the settings, the restart and the power
// off. All five are **one key**: a `UIButton` of this system with **no frame and no shadow** (`framed` false), so what a person
// sees is the mark and nothing else, and it sinks by turning over while the finger is on it, which is the press every button of
// this glass has (the owner: *«vull que siguin UIButton, però sense vores (afegir propietat al botó si no la te ja) i amb una
// imatge/icona com a contingut»*) (docs/30, docs/45, docs/92).
// **Les cinc claus porten la seva paraula** (0.185.3, l'amo: *«afegeix text per als botons; "Back" i "Close" al costat de la
// icona … als botons de Settings, Reboot i Powerdown posa text sota cada icona»*). Una clau del sistema ja no és un quadrat d'una
// icona: és **una icona amb el seu nom**, i el nom és dins del botó, o sigui que prémer el nom és prémer la clau.
//
//   - **les cantonades són amples** —`← Back` i `Close ✕`, amb la marca a la vora de fora, que és el que fa que les dues
//     cantonades mirin enfora— i el repartiment el fa el dibuix del botó (`ui::kMarkLeft` i `ui::kMarkRight`);
//   - **les tres del mig són altes**: la marca a sobre i la paraula a sota, dins del mateix rectangle, i la paraula a escala 1
//     de la cara del sistema (a escala 2 no hi caben les dues coses en una clau de 128);
//   - **`Settings` va separada de la parella** amb quaranta-vuit píxels en lloc dels dotze d'entre claus, que és el que el dibuix
//     de l'amo marcava amb `|||` (docs/30, docs/45, docs/92).
const uint16_t kCornerTop = 20;   // la fila de les dues cantonades
const uint16_t kCornerW = 190;    // una clau ampla: la marca i la paraula al costat
const uint16_t kCornerH = 64;
const uint16_t kKeyMargin = 24;
const uint16_t kRowTop = 108;     // la fila de les tres del mig
const uint16_t kMiddleW = 150;    // una clau alta: la marca a sobre i la paraula a sota
const uint16_t kMiddleH = 104;
const uint16_t kRowGap = 12;      // l'aire entre dues claus del mig
const uint16_t kSettingsGap = 48;  // i el doble entre Settings i la parella
// **La paraula, a tres passos de la cara del sistema** (0.185.3): a un pas fa onze píxels i no es llegeix —es va veure a
// l'emulador—, i a tres són trenta-tres, que és el que cap en una clau de cent quatre files amb la marca a sobre (docs/30, docs/92).
const uint8_t kWordScale = 3;

// **Quin botó és quin** (0.185.0): the five of the card, in the order the row of the three is drawn —the settings first and then
// the pair that stops the board— (docs/30).
enum class Key : uint8_t { kBack = 0, kClose = 1, kSettings = 2, kPowerOff = 3, kReboot = 4 };
const uint8_t kKeyCount = 5;

bool g_open = false;
// **Quina clau té el dit a sobre** (0.185.2): the card is painted on the glass and not by a page of controls, so a press is a
// paint of that key and not a state kept somewhere: the finger arrives, the key is drawn sunken, and the lift runs its action.
// The tap's own press used to be the only paint of it —at the lift, with the action on top of it— and a person never saw a key
// go down (measured on the emulator: the gear was painted turned over and the settings took the glass on the same frame,
// docs/30, docs/50).
int16_t g_sunk = -1;

// **On és el botó, en el marc de la vista** (0.179.23): the fingers arrive with the rows of the bar taken off and the card is
// painted with them put back, so a hit test that mixed the two frames would be a button a whole bar away from the thing the person
// aimed at. The rectangle is the ink and the hit test both —**a key with a word in it is the whole word**— and the tall ones are
// taller than wide so the word has rows of its own (docs/30, docs/92).
struct KeyBox {
  int16_t x = 0;
  int16_t y = 0;
  uint16_t w = 0;
  uint16_t h = 0;
  uint8_t mark = ui::kMarkAbove;
  const char* word = nullptr;
};

KeyBox key_box(Key which) {
  const int16_t width = static_cast<int16_t>(ui::width());
  KeyBox box;
  if (which == Key::kBack) {
    box.x = static_cast<int16_t>(kKeyMargin);
    box.y = static_cast<int16_t>(kCornerTop);
    box.w = kCornerW;
    box.h = kCornerH;
    box.mark = ui::kMarkLeft;
    box.word = "Back";
    return box;
  }
  if (which == Key::kClose) {
    box.x = static_cast<int16_t>(width - static_cast<int16_t>(kKeyMargin) - kCornerW);
    box.y = static_cast<int16_t>(kCornerTop);
    box.w = kCornerW;
    box.h = kCornerH;
    box.mark = ui::kMarkRight;
    box.word = "Close";
    return box;
  }
  // **Settings, Poweroff, Reboot**, en aquest ordre i amb la separació de l'amo entre la primera i la parella.
  const int16_t total = static_cast<int16_t>(3 * kMiddleW + kSettingsGap + kRowGap);
  const int16_t left = static_cast<int16_t>((width - total) / 2);
  int16_t at = left;
  if (which == Key::kPowerOff) at = static_cast<int16_t>(left + kMiddleW + kSettingsGap);
  if (which == Key::kReboot) at = static_cast<int16_t>(left + 2 * kMiddleW + kSettingsGap + kRowGap);
  box.x = at;
  box.y = static_cast<int16_t>(kRowTop);
  box.w = kMiddleW;
  box.h = kMiddleH;
  box.mark = ui::kMarkAbove;
  box.word = (which == Key::kSettings) ? "Settings" : ((which == Key::kPowerOff) ? "Poweroff" : "Reboot");
  return box;
}

const char* key_icon(Key which) {
  switch (which) {
    case Key::kBack:
      return "back";
    case Key::kClose:
      return "close";
    case Key::kSettings:
      return "settings";
    case Key::kReboot:
      return "reboot";
    default:
      return "poweroff";
  }
}

// **La creu és un botó només quan hi ha una pàgina que es pugui deixar** (0.179.25): over a launcher —the one page that is not
// left— and over no page at all, the corner of the card is paper, and a tap there is a tap on the card. A button that does nothing
// is worse than no button: it says the system can do something it cannot (docs/30, docs/45). **La fletxa, quan hi ha una vista a
// sota**: an app with one page has nowhere to go back to, and the same rule applies to it.
//
// **Els tres del mig hi són sempre**: the settings, the restart and the power off do not depend on how many pages the app has on
// top —they are the system's, and the console that a close leaves behind has them too— (docs/30, docs/45).
bool key_is_there(Key which) {
  if (which == Key::kClose) return shell::the_page_in_front_can_be_left();
  if (which == Key::kBack) return apps::views() > 0;
  return true;
}

// **The button is the rectangle a finger lands on** (0.179.26): the ink and the hit test are the same rectangle, so what a person
// sees is what a person presses —and a key with a word in it is the word and the mark, not the square the mark sits in— (docs/30).
bool in_a_button(const KeyBox& box, uint16_t x, uint16_t y) {
  const int32_t px = static_cast<int32_t>(x);
  const int32_t py = static_cast<int32_t>(y);
  return px >= box.x && px < static_cast<int32_t>(box.x) + static_cast<int32_t>(box.w) && py >= box.y &&
         py < static_cast<int32_t>(box.y) + static_cast<int32_t>(box.h);
}

bool the_key_under(uint16_t x, uint16_t y, Key& which) {
  for (uint8_t i = 0; i < kKeyCount; ++i) {
    const Key key = static_cast<Key>(i);
    if (!key_is_there(key)) continue;
    if (!in_a_button(key_box(key), x, y)) continue;
    which = key;
    return true;
  }
  return false;
}

// **A third of what the bar leaves**: enough for the rows of switches that come later and little enough that the page
// under it is still the page of the app (0.179.22).
uint16_t card_height() {
  const uint16_t glass = display::height();
  const uint16_t top = bar::height();
  if (glass <= top) return 0;
  return static_cast<uint16_t>((glass - top) / 3);
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

// **El botó, pintat pel sistema** (0.185.0): `ui::paint_a_button` is the drawing of every button of this tree and the panel only
// says where it is, which mark it carries, which word says what it does and whether the finger is on it. **Sense vores i sense
// ombra** (`framed` false): the card's paper is the key's paper, so the mark and its word are the whole of what a person sees, and
// the press turns the key over —ink face, paper mark and paper word— which is the same press every button of this system has
// (docs/30, docs/92).
void paint_the_key(hal::ISurface& surface, Key which, bool pressed) {
  const KeyBox box = key_box(which);
  ui::paint_a_button(surface, static_cast<uint16_t>(box.x), static_cast<uint16_t>(bar::height() + box.y), box.w,
                     box.h, /*background=*/15, /*border=*/0, pressed, /*focused=*/false, /*primary=*/false,
                     box.word, /*color=*/0, kWordScale, /*framed=*/false, key_icon(which), box.mark);
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
  // **Els cinc botons de la targeta**, on the rows of the card and not of the view: the arrow of the way back in the left corner,
  // the cross of the way out in the right one and the three of the system in the row of the middle, each one drawn with the one
  // function every button of this system is drawn with (0.185.0, docs/30).
  for (uint8_t i = 0; i < kKeyCount; ++i) {
    const Key key = static_cast<Key>(i);
    if (!key_is_there(key)) continue;
    paint_the_key(*surface, key, /*pressed=*/false);
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
  if (!key_is_there(Key::kBack)) return;
  g_open = false;
  ui::forget_the_page_hold();
  ui::owe_the_page();
  shell::go_back_one_view();
  log_printf(pal::kLogInfo, "PANEL", "the arrow: the app went to the view below");
}

// **La creu tanca l'app i no una pàgina** (0.185.0, l'amo: *«"Close" ha de tancar l'app, no anar enrere, per aixó ja està
// "Back"»*): the arrow of the left corner is the one page back and the cross is the whole app, pages on top and all, with the
// page that asked for it taking the glass back —`shell::close_the_app` is that road, and it refuses a launcher the way the pinch
// does— (docs/30, docs/45).
void close_the_app() {
  const shell::LeaveOutcome out = shell::close_the_app();
  // **A door that did not open leaves the card where it is**: a launcher is the one page that is not left, and a page that did
  // not go is a page the panel is still over —the refusal is said in the line of the road out and not by closing the card— (docs/30,
  // docs/45).
  if (out == shell::LeaveOutcome::kNothing || out == shell::LeaveOutcome::kLauncher) return;
  // **And the card goes with the page it was over**: when the system already owes a whole page —the page of the app that asked
  // for this one— that paint is the one that erases the card, and a close does not pay for two washes of the glass.
  if (out == shell::LeaveOutcome::kClosedHome) {
    g_open = false;
    ui::forget_the_page_hold();
    log_printf(pal::kLogInfo, "PANEL", "the cross: the app is closed and its page comes back");
    return;
  }
  log_printf(pal::kLogInfo, "PANEL", "the cross: the app is closed");
  put_it_away();
}

// **El reinici i el tancament de la placa** (0.185.0): the card drops the hold of the page **sense pintar**, because the picture
// of the moment washes the whole glass in `gc16` —which is the paint the card would have paid—, and asks the shell for the one
// sequence the prompt asks for: two ways down of the same board are two things to keep in step (docs/30, docs/50).
void restart_the_board() {
  g_open = false;
  ui::forget_the_page_hold();
  log_printf(pal::kLogInfo, "PANEL", "the restart key: the board starts again");
  shell::reboot("the card of the bar");
}

void power_the_board_off() {
  g_open = false;
  ui::forget_the_page_hold();
  log_printf(pal::kLogInfo, "PANEL", "the power key: the board goes down");
  shell::power_off("the card of the bar");
}

// **Els ajustos** (0.185.0): the settings of this system live in `cfg`, which is a line of the prompt and not a page yet —there is
// no app of settings on the disk—, so the key opens the console in the glass, which is the app that speaks `cfg`, **with the page
// that asked for it behind it** (`back`): closing the console comes back to where a person was (docs/30, docs/40).
void open_the_settings() {
  char why[96] = {0};
  if (!shell::ask_run("shell", /*back=*/true, why, sizeof(why))) {
    log_printf(pal::kLogWarn, "PANEL", "the settings are not there: %s", why);
    return;
  }
  put_it_away();
  log_printf(pal::kLogInfo, "PANEL", "the settings key: the console of the system comes up");
}

// **Un botó és la seva acció**: the five of the card, one switch, so the paint, the hit test and the press cannot disagree about what
// a key does (docs/30, docs/45).
void run_the_key(Key which) {
  switch (which) {
    case Key::kBack:
      go_back_a_view();
      return;
    case Key::kClose:
      close_the_app();
      return;
    case Key::kSettings:
      open_the_settings();
      return;
    case Key::kReboot:
      restart_the_board();
      return;
    default:
      power_the_board_off();
      return;
  }
}

// **El botó s'enfonsa mentre el dit hi és** (0.185.0, l'amo: *«com qualsevol botó s'enfonsa al pitjar»*): the card is ink on the
// glass and not a page of controls, so the press is a paint of that key with its face turned over, one box of sixty-four and not the
// card, and only then does its action run (docs/30, docs/50).
void press_the_key(Key which) {
  hal::ISurface* surface = display::surface();
  if (surface == nullptr) return;
  paint_the_key(*surface, which, /*pressed=*/true);
  display::present(hal::UpdateMode::kDu);
  // **The key stays sunk under the action and the state goes with the card**: the tap is the last thing this key does, and a
  // card that is put away leaves nothing to paint back (docs/30).
  g_sunk = static_cast<int16_t>(which);
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
  // **Una clau enfonsada no sobreviu a la targeta**: a key that was painted turned over and whose card goes away would be a
  // repaint of the page under it the next turn (docs/30, docs/50).
  g_sunk = -1;
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
  //   - a **tap on one of the five keys** does what that key says —the arrow one page back, the cross the app closed, and the
  //     three of the middle the settings, the restart and the power off—, and they are the only things on the card that answer a
  //     finger (`key_is_there` is false for a key that would do nothing: the cross over a launcher, the arrow on the only page);
  //   - a **tap outside its rows** —the page below it, or the bar it hangs from— puts the card away. A tap **on** the card is
  //     the card's: the switches that come later will take their taps, and the rows are not a thing to dismiss with;
  //   - and a **drag up that begins outside it** puts it away too. That is the gesture the page under the card would use for
  //     its own scroll —which is why 0.179.42 took it away— and the reason it is back is that the same version stopped the
  //     app: while the card is out there is no scroll underneath, so a hand that swipes up there is telling the card to go.
  //     A drag that begins **on** the card is still the card's and does nothing.
  // What is left is nobody's: the app hears none of it, and its turn is not run while the card is out (`kernel.cpp`), so
  // nothing but this function can close the card (docs/30).
  touch::Gesture gesture;
  // **El dit que hi ha ara mateix**: the contact of the last turn is what sinks a key, because a tap is only a gesture when it
  // ends and a button that goes down on the way up is a button nobody sees move (docs/30, docs/50).
  {
    hal::TouchPoint live[touch::kMaxPoints];
    const uint8_t down = touch::points(live, touch::kMaxPoints);
    int16_t under = -1;
    if (down > 0) {
      Key held = Key::kBack;
      if (the_key_under(live[0].x, live[0].y, held)) under = static_cast<int16_t>(held);
    }
    if (under != g_sunk) {
      hal::ISurface* surface = display::surface();
      if (surface != nullptr) {
        if (g_sunk >= 0) paint_the_key(*surface, static_cast<Key>(g_sunk), /*pressed=*/false);
        if (under >= 0) paint_the_key(*surface, static_cast<Key>(under), /*pressed=*/true);
        display::present(hal::UpdateMode::kDu);
      }
      g_sunk = under;
    }
  }
  while (touch::take_gesture(gesture)) {
    if (gesture.kind == nullptr) continue;
    // **Un dit que s'hi està també és un toc** (0.185.2): a key of the card is a button of this glass and a person who rests a
    // finger on it for a second has pressed it —the gesture of a contact held that long is `long` and not `tap`, and the card
    // answered nothing at all: the gear stayed turned over and the settings never came (measured on the emulator)— (docs/30).
    if (strcmp(gesture.kind, "tap") == 0 || strcmp(gesture.kind, "long") == 0) {
      Key which = Key::kBack;
      if (the_key_under(gesture.x, gesture.y, which)) {
        touch::forget_gestures();
        // **El botó s'enfonsa i després fa el que diu**: the press is the paint of that key turned over, which is what a finger on a
        // key of this system sees, and the action comes after it (docs/30, docs/50).
        press_the_key(which);
        run_the_key(which);
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
