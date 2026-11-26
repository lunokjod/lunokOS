// **La targeta de baix** (0.192.23): the mirror of the card of the bar. It hangs from the strip of the bottom, it comes out with a drag
// up and it goes away with a drag down that begins outside it or with a tap outside it —the two gestures of the card of the top, at the
// other edge—. **Des de la 0.192.24 té les quatre claus que naveguen** (l'amo: *«Tots els botons de fila de Settings han d'anar abaix:
// Settings, Home, Close i Back al panell de sistema inferior»*), amb el paper, la línia i el mànec de sempre.
#include "kernel/panel_low.h"

#include <string.h>

#include "hal/hal.h"
#include "kernel/bar.h"
#include "kernel/display.h"
#include "kernel/log.h"
#include "kernel/panel.h"
#include "kernel/touch.h"
#include "kernel/ui.h"

#if LUNOK_GLASS

namespace kernel {
namespace panel_low {
namespace {

// The same two numbers of the card of the top: a pull nobody makes by accident and a shorter push away.
const int16_t kPullPx = 60;
const int16_t kPushPx = 40;
// The line that closes the card and the handle, the same shape as the one above —i el mànec a dalt de tot, perquè la targeta puja i el
// de la de dalt és a la seva vora de baix: aquest és el mirall (docs/30).
const uint16_t kHandleW = 96;
const uint16_t kHandleH = 6;
const uint16_t kHandleGap = 14;
const uint16_t kLineH = 2;

// **Les quatre claus que naveguen** (0.192.24): the size the four of the bottom row of the card of the top had —96 × 104, la marca a
// sobre i la paraula a sota a dos passos de la cara del sistema—, i **a frec de la franja de baix**, que és on el dit hi és (docs/30).
const uint16_t kKeyW = 96;
const uint16_t kKeyH = 104;
const uint16_t kKeyGap = 8;
const uint16_t kKeyTop = 20;
const uint16_t kKeyAir = 10;
const uint8_t kWordScale = 2;

// **L'ordre és el de la fila que hi havia a l'altra targeta** (0.189.158, l'amo: *«posem-ho així: Settings, Home, Close, Back»*): the
// two that go somewhere on the left —the gear and the house— and the two that end the page on the right, amb la sortida **abans** que
// el camí enrere, perquè la creu tanca el que la fletxa ha deixat enrere (docs/30, docs/45).
enum class Key : uint8_t { kSettings = 0, kHome = 1, kClose = 2, kBack = 3 };
const uint8_t kKeyCount = 4;

bool g_open = false;

// **Quina clau té el dit a sobre**: the card is painted on the glass and not by a page of controls, so the press is a paint of that key
// turned over and a lift that runs its action —a person has to see the key go down (docs/30, docs/50).
int16_t g_sunk = -1;

// **Les files que la targeta necessita, i prou** (0.192.25, l'amo: *«Ara els panells son molt grans per el contingut que tenen, no?»*): el mànec
// amb la seva línia, l'aire i la fila de claus, i res més; el terç del vidre que era la forma d'abans deixava mitja targeta de paper buit. **En
// files de la vista**: that is the frame the fingers arrive in (`touch` takes the rows of the bar off), i el pintat hi afegeix les files de la
// barra perquè el vidre compta des de dalt de tot (docs/30, docs/50).
uint16_t card_height() {
  const uint16_t view = static_cast<uint16_t>(display::height() - bar::height());
  const uint16_t strip = bar::bottom_height();
  if (view <= strip) return 0;
  const uint16_t rows = static_cast<uint16_t>(kLineH + kHandleGap + kHandleH + kKeyTop + kKeyH + kKeyAir);
  const uint16_t room = static_cast<uint16_t>(view - strip);
  return (rows < room) ? rows : room;
}

uint16_t the_top() {
  const uint16_t view = static_cast<uint16_t>(display::height() - bar::height());
  const uint16_t rows = card_height();
  const uint16_t strip = bar::bottom_height();
  return (view > strip + rows) ? static_cast<uint16_t>(view - strip - rows) : 0;
}

// **On és el botó, en el marc de la vista** (0.179.23): the ink and the hit test are the same rectangle, so what a person sees is what a
// person presses —i la fila és a baix de la targeta, a deu files de la franja (docs/30, docs/92).
struct KeyBox {
  int16_t x = 0;
  int16_t y = 0;
  uint16_t w = 0;
  uint16_t h = 0;
  const char* word = nullptr;
};

KeyBox key_box(Key which) {
  const int16_t width = static_cast<int16_t>(ui::width());
  const int16_t total = static_cast<int16_t>(4 * kKeyW + 3 * kKeyGap);
  const int16_t left = static_cast<int16_t>((width - total) / 2);
  uint8_t place = 0;
  if (which == Key::kHome) place = 1;
  if (which == Key::kClose) place = 2;
  if (which == Key::kBack) place = 3;
  KeyBox box;
  box.x = static_cast<int16_t>(left + place * (kKeyW + kKeyGap));
  box.y = static_cast<int16_t>(the_top() + kLineH + kHandleGap + kHandleH + kKeyTop);
  box.w = kKeyW;
  box.h = kKeyH;
  box.word = (which == Key::kSettings) ? "Settings"
             : (which == Key::kHome) ? "Home"
             : (which == Key::kBack) ? "Back"
                                     : "Close";
  return box;
}

const char* key_icon(Key which) {
  switch (which) {
    case Key::kSettings:
      return "settings";
    case Key::kHome:
      // **La casa** (0.189.139): `U+F015` of the clipart, the mark of `data/res/icons/home.png` (docs/45, docs/50).
      return "home";
    case Key::kClose:
      return "close";
    default:
      return "back";
  }
}

// **La clau hi és si faria alguna cosa**: the question is `panel`'s —the portal knows which apps are on the disk and the shell knows which
// page can be left—, o sigui que les dues targetes no tenen dues idees de les mateixes quatre claus (docs/30, docs/45).
bool key_is_there(Key which) { return panel::a_page_key_is_there(static_cast<panel::PageKey>(which)); }

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

// **Si el dit va caure damunt la targeta**: the rows between its first one and the strip, i la franja de baix és el seu mànec i no la
// targeta —el dit que hi cau la tanca, que és el mateix que fa la barra de dalt (docs/30).
bool on_the_card(const touch::Gesture& gesture) {
  if (gesture.on_the_strip) return false;
  return gesture.y >= the_top();
}

// **El botó, pintat pel sistema** (0.185.0): `ui::paint_a_button` is the drawing of every button of this tree and this card only says
// where it is, which mark it carries, which word says what it does and whether the finger is on it. **Sense vores i sense ombra**
// (`framed` false): el paper de la targeta és el paper de la clau, i la premsa la gira (docs/30, docs/92).
void paint_the_key(hal::ISurface& surface, Key which, bool pressed) {
  const KeyBox box = key_box(which);
  ui::paint_a_button(surface, static_cast<uint16_t>(box.x), static_cast<uint16_t>(bar::height() + box.y), box.w,
                     box.h, /*background=*/15, /*border=*/0, pressed, /*focused=*/false, /*primary=*/false,
                     box.word, /*color=*/0, kWordScale, /*framed=*/false, key_icon(which), ui::kMarkAbove);
}

void paint() {
  hal::ISurface* surface = display::surface();
  const uint16_t rows = card_height();
  if (surface == nullptr || rows == 0) return;
  const uint16_t width = surface->width();
  const uint16_t top = static_cast<uint16_t>(bar::height() + the_top());
  surface->fill_rect(0, top, width, rows, 15);
  surface->fill_rect(0, top, width, kLineH, 0);
  surface->fill_rect(static_cast<uint16_t>((width - kHandleW) / 2), static_cast<uint16_t>(top + kLineH + kHandleGap),
                     kHandleW, kHandleH, 0);
  for (uint8_t i = 0; i < kKeyCount; ++i) {
    const Key key = static_cast<Key>(i);
    if (!key_is_there(key)) continue;
    paint_the_key(*surface, key, /*pressed=*/false);
  }
  display::present(hal::UpdateMode::kGc16);
}

// **El botó s'enfonsa mentre el dit hi és** (0.185.0, l'amo: *«com qualsevol botó s'enfonsa al pitjar»*): one box of the card and not the
// card, i després el que la clau diu (docs/30, docs/50).
void press_the_key(Key which) {
  hal::ISurface* surface = display::surface();
  if (surface == nullptr) return;
  paint_the_key(*surface, which, /*pressed=*/true);
  display::present(hal::UpdateMode::kDu);
  g_sunk = static_cast<int16_t>(which);
}

// **Un botó és la seva acció**: the four of this card, one switch, i les accions són les de `panel`, que és qui sap què és tornar
// enrere, què és tancar i quina app és la de casa (docs/30, docs/45).
void run_the_key(Key which) { panel::press_a_page_key(static_cast<panel::PageKey>(which)); }

void pull_it_out() {
  if (g_open) return;
  g_open = true;
  // **Es torna a preguntar què hi ha** (0.190.81): una app es pot haver instal·lat o esborrat des de l'última vegada que la targeta va
  // sortir, i el que es pinta és el que hi ha ara i no el que hi havia (docs/30, docs/40).
  panel::look_at_what_is_there();
  ui::hold_the_page(true);
  paint();
  log_printf(pal::kLogInfo, "PANEL", "pulled up: the card of the bottom is %u rows", static_cast<unsigned>(card_height()));
}

void put_it_away() {
  if (!g_open) return;
  g_open = false;
  g_sunk = -1;
  ui::hold_the_page(false);
  log_printf(pal::kLogInfo, "PANEL", "put away: the card of the bottom is gone and the page is back");
}

}  // namespace

void boot() { g_open = false; }

bool open() { return g_open; }

uint16_t height() { return card_height(); }

uint16_t top_row() { return the_top(); }

void close_the_card() {
  if (!g_open) return;
  g_open = false;
  g_sunk = -1;
  log_printf(pal::kLogInfo, "PANEL", "the card of the bottom goes with the key that was pressed");
}

void poll() {
  if (!display::available()) return;
  if (g_open && !ui::page_held()) {
    g_open = false;
    log_printf(pal::kLogWarn, "PANEL", "the card of the bottom went with the page it was over");
    return;
  }
  if (!g_open) {
    // **Obrir-la només quan no hi ha cap altra targeta**: amb la de dalt a sobre, l'arròs que puja és el gest que la tanca i no el que
    // obre aquesta, i aquesta no se'l pot quedar (0.192.23).
    if (panel::open()) return;
    // **I la pujada, mesurada a la darrera fila del VIDRE** (0.192.33): the mirror of the pull of the top card, and for the same
    // reason —the flag was a conclusion of the frame and the number cannot lie—. Amb el vidre sencer, la mateixa pujada torna la
    // barra i la targeta penja d'ella (docs/30, docs/46 D1).
    touch::Gesture gesture;
    if (!touch::take_a_drag_up_from_the_system_rows(kPullPx, gesture)) return;
    if (!bar::visible() && !ui::full_screen(false)) {
      log_printf(pal::kLogWarn, "PANEL", "a push from the whole glass did not give the bar back");
      return;
    }
    pull_it_out();
    touch::forget_gestures();
    return;
  }
  // **El dit que hi ha ara mateix**: the contact of the last turn is what sinks a key, perquè un toc només és gest quan s'acaba i un
  // botó que s'enfonsa en aixecar-se és un botó que ningú no veu moure (docs/30, docs/50).
  {
    hal::TouchPoint live[touch::kMaxPoints];
    const uint8_t down = touch::points(live, touch::kMaxPoints);
    int16_t under = -1;
    if (down > 0) {
      Key held = Key::kSettings;
      if (the_key_under(live[0].x, live[0].y, held)) under = static_cast<int16_t>(held);
    }
    if (under != g_sunk) {
      hal::ISurface* surface = display::surface();
      if (surface != nullptr) {
        // **Una clau que ja no hi és no es repinta** (0.189.117): `g_sunk` pot sobreviure a la targeta que la tenia, i la clau d'una
        // pàgina que ha tornat enrere no és la de la targeta de la pàgina de sota (docs/30, docs/45).
        if (g_sunk >= 0 && key_is_there(static_cast<Key>(g_sunk))) {
          paint_the_key(*surface, static_cast<Key>(g_sunk), /*pressed=*/false);
        }
        if (under >= 0) paint_the_key(*surface, static_cast<Key>(under), /*pressed=*/true);
        display::present(hal::UpdateMode::kDu);
      }
      g_sunk = under;
    }
  }
  touch::Gesture gesture;
  while (touch::take_gesture(gesture)) {
    if (gesture.kind == nullptr) continue;
    // **Un toc a una clau fa el que la clau diu; fora de la targeta la tanca** (0.179.42, l'amo: *«Ningú ha de poder tancar el panell
    // excepte l'usuari, picant fora de la zona desplegada»*) (docs/30).
    if (strcmp(gesture.kind, "tap") == 0 || strcmp(gesture.kind, "long") == 0) {
      Key which = Key::kSettings;
      if (the_key_under(gesture.x, gesture.y, which)) {
        touch::forget_gestures();
        press_the_key(which);
        run_the_key(which);
        return;
      }
      if (on_the_card(gesture)) continue;
      touch::forget_gestures();
      put_it_away();
      return;
    }
    if (strcmp(gesture.kind, "drag") != 0 || on_the_card(gesture)) continue;
    const int32_t down = gesture.dy;
    const int32_t sideways = (gesture.dx < 0) ? -static_cast<int32_t>(gesture.dx) : gesture.dx;
    if (down < kPushPx || down < sideways) continue;
    touch::forget_gestures();
    put_it_away();
    return;
  }
}

}  // namespace panel_low
}  // namespace kernel

#endif  // LUNOK_GLASS
