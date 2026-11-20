// The card of the system that hangs from the bar: it opens with a drag down from the titlebar and goes with a drag up
// (docs/30).
#include "kernel/panel.h"

#include <string.h>

#include "kernel/apps.h"
#include "kernel/bar.h"
#include "kernel/cfg.h"
#include "kernel/display.h"
#include "kernel/log.h"
#include "kernel/power.h"
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

// **Les vuit claus de la targeta, en dues files** (0.185.0; la sisena des de la 0.189.25, **la casa** des de la 0.189.139, **el
// candau** i les dues files des de la 0.189.151). The card carries **eight keys**: the three that stop the board —the power off,
// **the deep sleep** and the restart— in a row of their own at the top, and the five of the system below —the way back, the house
// of the page of the start, the gear of the settings, **the padlock of the lock** and the cross of the way out—. All eight are
// **one key**: a `UIButton` of this system with **no frame and no
// shadow** (`framed` false), so what a person
// sees is the mark and nothing else, and it sinks by turning over while the finger is on it, which is the press every button of
// this glass has (the owner: *«vull que siguin UIButton, però sense vores (afegir propietat al botó si no la te ja) i amb una
// imatge/icona com a contingut»*) (docs/30, docs/45, docs/92).
// **Cada clau porta la seva paraula** (0.185.3, l'amo: *«afegeix text per als botons; "Back" i "Close" al costat de la
// icona … als botons de Settings, Reboot i Powerdown posa text sota cada icona»*). Una clau del sistema ja no és un quadrat d'una
// icona: és **una icona amb el seu nom**, i el nom és dins del botó, o sigui que prémer el nom és prémer la clau.
//
//   - **les quatre de dalt són una mica més petites** (0.189.151, l'amo: *«A dalt de tot Poweroff, Sleep, Reboot una mica mes
//     petits (lleugerament, no tant petits com es ara "Home") a la part de dalt del panell»*): 104 × 88, que és la marca a sobre i
//     la paraula a sota amb la mida d'abans de la 0.189.139 i una mica menys d'aire;
//   - **les quatre de baix tenen la mida que tenien les quatre del mig** (*«A baix de tot Home, Close i Settings en el tamany que
//     tenen ara PowerOff Sleep i Reboot»*): 96 × 104, i el mateix dibuix per a totes vuit —la marca a sobre, la paraula a sota i
//     la paraula a dos passos de la cara del sistema, que és el que fa que quatre n'hi càpiguen (0.189.25)—;
//   - **el candau va amb els que paren la placa** (0.189.152, l'amo: *«Posa el lock amb els de poweroff, sleep i reboot: Power off,
//     Sleep, Reboot, Lock»*): the page of `cfg lock`, the fourth of the top row, perquè una clau que tapa el vidre és una de les
//     que paren la placa i no una de les que hi naveguen (docs/30, docs/40, docs/60);
//   - **i la de baix és `Settings`, `Home`, `Close`, `Back`** (0.189.158, l'amo: *«al panell desplegable del sistema "Back" està
//     al costat de "Home"... posem-ho així: Settings, Home, Close, Back»*): the two that go somewhere on the left —the gear and
//     the house— and the two that end the page on the right, with the way out **before** the way back, perquè la creu tanca el
//     que la fletxa ha deixat enrere (docs/30, docs/45).
const uint16_t kTopRowY = 20;      // la fila de les quatre que paren la placa
const uint16_t kTopW = 104;        // una mica més ampla i més baixa que les de sota
const uint16_t kTopH = 88;
const uint16_t kTopGap = 16;       // l'aire entre dues claus d'aquesta fila
const uint16_t kBottomRowY = 168;  // la fila de les quatre del sistema
const uint16_t kBottomW = 96;      // la mida que tenien les quatre del mig
const uint16_t kBottomH = 104;
const uint16_t kBottomGap = 8;
// **Una sola escala per a vuit paraules** (0.185.3, 0.189.25 i 0.189.151): a un pas fa onze píxels i no es llegeix —es va veure a
// l'emulador—, i a dos la paraula més ampla, `Poweroff`, fa noranta-un píxels, que és el que una clau de noranta-sis aguanta.
// L'escala de tres era de les cantonades amples, i amb elles se'n va anar quan el dibuix de la targeta es va fer de dues files.
const uint8_t kWordScale = 2;

// **Quin botó és quin** (0.185.0): the eight of the card, in the order the two rows are drawn —the gear, the house, the cross
// and the way back below; the power off, **the deep sleep**, the restart and the padlock above, with the sleep between the power off
// and the restart, which is where the owner asked for it: the closer to off a way down is, the closer they sit— (docs/30).
enum class Key : uint8_t {
  kSettings = 0,
  kHome = 1,
  kClose = 2,
  kBack = 3,
  kPowerOff = 4,
  kDeepSleep = 5,
  kReboot = 6,
  kLock = 7
};
const uint8_t kKeyCount = 8;

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
  // **L'escala de la paraula és de la clau i no de la targeta** (0.189.25): the corners have room for the system's three steps and
  // the four of the middle do not, so each key carries how big its word is drawn (docs/30, docs/92).
  uint8_t scale = kWordScale;
};

KeyBox key_box(Key which) {
  const int16_t width = static_cast<int16_t>(ui::width());
  KeyBox box;
  box.mark = ui::kMarkAbove;
  box.scale = kWordScale;
  // **La fila de dalt: les quatre que paren la placa** (0.189.151, i el candau des de la 0.189.152): el corrent, la son, el
  // reinici i **el bloqueig**, en l'ordre demanat —*«Posa el lock amb els de poweroff, sleep i reboot: Power off, Sleep, Reboot,
  // Lock»*—, centrades i a la mateixa mida entre elles, que és el que fa que la fila es llegeixi com una fila (docs/30, docs/45).
  if (which == Key::kPowerOff || which == Key::kDeepSleep || which == Key::kReboot || which == Key::kLock) {
    const int16_t total = static_cast<int16_t>(4 * kTopW + 3 * kTopGap);
    const int16_t left = static_cast<int16_t>((width - total) / 2);
    uint8_t place = 0;
    if (which == Key::kDeepSleep) place = 1;
    if (which == Key::kReboot) place = 2;
    if (which == Key::kLock) place = 3;
    box.x = static_cast<int16_t>(left + place * (kTopW + kTopGap));
    box.y = static_cast<int16_t>(kTopRowY);
    box.w = kTopW;
    box.h = kTopH;
    box.word = (which == Key::kPowerOff) ? "Poweroff"
               : (which == Key::kDeepSleep) ? "Sleep"
               : (which == Key::kReboot) ? "Reboot"
                                         : "Lock";
    return box;
  }
  // **I la fila de baix: les quatre del sistema** (0.189.152, i l'ordre de la 0.189.158): the gear, the house, the cross of the
  // way out and the arrow of one page back, en aquest ordre —les dues que van a un lloc a l'esquerra i les dues que tanquen a la
  // dreta—, que és el que l'amo va demanar després de veure-hi la fletxa a frec de la casa (docs/30, docs/45).
  const int16_t total = static_cast<int16_t>(4 * kBottomW + 3 * kBottomGap);
  const int16_t left = static_cast<int16_t>((width - total) / 2);
  uint8_t place = 0;
  if (which == Key::kHome) place = 1;
  if (which == Key::kClose) place = 2;
  if (which == Key::kBack) place = 3;
  box.x = static_cast<int16_t>(left + place * (kBottomW + kBottomGap));
  box.y = static_cast<int16_t>(kBottomRowY);
  box.w = kBottomW;
  box.h = kBottomH;
  box.word = (which == Key::kSettings) ? "Settings"
             : (which == Key::kHome) ? "Home"
             : (which == Key::kBack) ? "Back"
                                     : "Close";
  return box;
}

const char* key_icon(Key which) {
  switch (which) {
    case Key::kBack:
      return "back";
    case Key::kClose:
      return "close";
    case Key::kHome:
      // **La casa** (0.189.139): `U+F015` of the clipart, the same mark the sixth family drew and the seventh keeps
      // (`data/res/icons/home.png`, `tools/iconart.py`) (docs/45, docs/50).
      return "home";
    case Key::kSettings:
      return "settings";
    case Key::kLock:
      // **El candau** (0.189.150): `U+F023` of the clipart, drawn by `tools/iconart.py` into `data/res/icons/lock.png` —and a
      // padlock of lines in `draw_icon` for a board whose disk has no `/res`—, perquè una clau del sistema que no es veu és una
      // clau que no hi és (docs/45, docs/50).
      return "lock";
    case Key::kDeepSleep:
      // **La lluna** (0.189.25): the system's marks are glyphs of the clipart it already carries, and the one of a board that
      // sleeps with its keys awake is the moon of `tools/iconart.py` (`data/res/icons/deepsleep.png`) (docs/45, docs/50).
      return "deepsleep";
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
// **El reinici i l'apagat hi són sempre, i la roda quan té app** (0.189.150): the restart and the power off do not depend on
// how many pages the app has on top —they are the system's, and the console that a close leaves behind has them too— and the
// gear depends on `cfg settings` naming an app, perquè una roda que no obre res és una clau que diu el que no pot fer (docs/30,
// docs/45).
// **Un nom a `cfg` no és una app** (0.190.81, l'amo: *«Al menú desplegable de sistema surt "Lock" quan no hi ha app de lock.
// Settings tampoc ha d'apareixer si no està definida i "Home" tampoc»*). `cfg lock` i `cfg settings` guarden **un nom**, i els
// noms de sèrie són `device_lock` i `settings`: si l'app no és al disc —o no és d'aquesta placa—, la targeta pintava una clau
// que no obre res. La pregunta honesta la contesta el portal, que és qui sap què és una app d'aquí (`apps::read` diu fals per a
// una carpeta sense manifest, una entrada que no hi és o una llista de plaques que no nomena aquesta), i **es fa una vegada cada
// cop que la targeta s'obre** perquè el pintat i el test del dit no hagin de llegir manifests a cada volta (docs/30, docs/40).
struct Present {
  bool home = false;
  bool settings = false;
  bool lock = false;
};
Present g_present;

bool app_is_there(const char* id) {
  if (id == nullptr || id[0] == '\0') return false;
  apps::Entry entry = {};
  return apps::read(id, entry);
}

void read_what_is_present() {
  g_present.home = app_is_there(cfg::home_app());
  g_present.settings = app_is_there(cfg::settings_app());
  g_present.lock = app_is_there(cfg::lock_app());
}

// **I el que sempre hi és són les tres que paren la placa** (0.190.81): `Poweroff` i `Reboot` no depenen de cap app, i `Sleep`
// depèn que la placa sàpiga dormir i prou. La creu i la fletxa depenen de la pàgina que hi hagi a sota, i les tres que porten a
// una app depenen que l'app hi sigui (docs/30, docs/45).
bool key_is_there(Key which) {
  if (which == Key::kClose) return shell::the_page_in_front_can_be_left();
  // **I sobre la pàgina d'inici la casa no hi és** (0.189.153, l'amo: *«No, el home si estem a home no ha de sortir, igual que el
  // back no surt si no hi ha back o Close tampoc si estem al home»*): the key of the house is a way **to** the page of the start,
  // and a person who is already looking at it has nothing to ask of it —a key that says where you are is a key that does what
  // the page is doing, que és la regla de la fletxa sense vista i de la creu sobre un llançador (docs/30, docs/45).
  if (which == Key::kHome) {
    if (!g_present.home) return false;
    const char* home = cfg::home_app();
    return !(shell::app_running() && strcmp(shell::app_id(), home) == 0);
  }
  // **I la roda, quan hi ha una app de settings** (0.189.150, l'amo: *«Si alguna de les opcions es buida, el botó del panell
  // desplegable de sistema no mostrarà la icona corresponent»*): `cfg settings none` is a gear that opens nothing, and a key
  // that says it can do something it cannot is the thing the cross of a launcher already does not do (docs/30, docs/45).
  if (which == Key::kSettings) return g_present.settings;
  // **I el candau, quan hi ha una app de bloqueig** (0.189.151): the same rule as the gear —`cfg lock none` is a padlock that
  // covers nothing, and the key is not painted— i la clau no mira el commutador: el commutador decideix els tres moments en què el
  // sistema es tapa sol, i aquesta clau és la persona demanant-ho ara (docs/30, docs/45, docs/60).
  if (which == Key::kLock) return g_present.lock;
  if (which == Key::kBack) return apps::views() > 0;
  // **I el son amb les tecles vives, només si la placa en té una** (0.189.25): the same rule as the cross —a key that does
  // nothing says the system can do something it cannot— and it is asked of the board and not of the model of the glass
  // (docs/30, docs/45, docs/50).
  if (which == Key::kDeepSleep) return power::can_deep_sleep();
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
                     box.word, /*color=*/0, box.scale, /*framed=*/false, key_icon(which), box.mark);
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

// **La lluna: la placa s'adorm i torna amb una tecla** (0.189.25, l'amo: *«l'afegeixes al panell desplegable del sistema entre el
// botó de poweroff i el de reboot»*): the card drops the hold of the page without painting —`power::deep_sleep_with_the_keys` paints
// the picture of the sleep, which is the paint the card would have paid— and asks the shell for the one sequence the prompt asks
// for with `deepsleep`, so two ways down of the same board are two things kept in step (docs/30, docs/50).
void sleep_with_the_keys() {
  g_open = false;
  ui::forget_the_page_hold();
  log_printf(pal::kLogInfo, "PANEL", "the moon key: the board sleeps with its keys awake");
  shell::deep_sleep("the card of the bar");
}

// **Els ajustos són una app, i quina és ho diu `cfg`** (0.185.0, 0.189.109, l'amo: *«Enllaça el botó de Settings del panell
// desplegable de sistema amb l'app <id: settings> Si no està l'app no passsa res»*). The gear used to open the console —which is the
// app that speaks `cfg`— because there was no page of settings on the disk; now the page is there and the name is a choice of the
// person (`cfg settings <id>`, and `none` for a gear that does nothing), like the app of the start.
//
// **Es mira abans de demanar-la**: the shell closes the page in front before it starts the one it was asked for, so a name that is
// not on the disk would leave the board at the prompt —the app that asked gone and nothing in its place—. The door of the apps is
// the one that knows whether the name is an app of this board, and a gear that finds nothing does nothing and says it in the log
// (docs/30, docs/40).
// **Una clau que obre una app que una persona ha triat** (0.189.109, compartida des de la 0.189.151): the gear of the settings
// and the padlock of the lock take the same road —the name is `cfg`'s, the app is looked up on the disk **before** it is asked
// for, and a board with nothing in front starts it instead of asking a page that is not there— and only the words of the log
// are their own (docs/30, docs/40, docs/90).
void open_a_chosen_app(const char* id, const char* name, const char* command) {
  if (id == nullptr || id[0] == '\0') {
    log_printf(pal::kLogInfo, "PANEL", "the %s key: no app of %s is chosen (`%s`)", name, name, command);
    return;
  }
  apps::Entry entry;
  if (!apps::read(id, entry)) {
    log_printf(pal::kLogWarn, "PANEL", "the %s key: there is no app '%s' on this board", name, id);
    return;
  }
  char why[96] = {0};
  // **Amb cap pàgina a sota, el rellotge la fa arrencar i no la demana** (0.189.139): `ask_run` is one app asking for another
  // —it writes the name down, the page in front is closed and the new one starts at the next turn—, and a board that came back
  // from a sleep with nothing in front has no page to close: the key answered *«no app is in front to ask for another one»* and
  // a glass with no page kept no way to the settings at all. The system's own key starts the app the way the boot starts the
  // page of the start (docs/30, docs/40, docs/90).
  if (!shell::app_running()) {
    g_open = false;
    ui::forget_the_page_hold();
    if (!shell::start_app(entry.id, /*splash=*/true)) {
      log_printf(pal::kLogWarn, "PANEL", "the %s key: '%s' did not come up", name, entry.id);
      return;
    }
    log_printf(pal::kLogInfo, "PANEL", "the %s key: %s comes up with no page under it", name, entry.id);
    return;
  }
  // **I la pàgina que la demana queda darrere** (`back`): closing the page of settings comes back to where the person was, which is
  // what a card that opens an app from the bar owes the page under it (docs/30, docs/40).
  if (!shell::ask_run(entry.id, /*back=*/true, nullptr, why, sizeof(why))) {
    log_printf(pal::kLogWarn, "PANEL", "the %s app is not there: %s", name, why);
    return;
  }
  put_it_away();
  log_printf(pal::kLogInfo, "PANEL", "the %s key: %s comes up", name, entry.id);
}

void open_the_settings() { open_a_chosen_app(cfg::settings_app(), "settings", "cfg settings <id>"); }

// **El candau: la pàgina que tapa el vidre** (0.189.151, l'amo: *«L'app "Lock" també ha de sortir, a la línia d'icones de Home,
// Settings, Back i Close»*). It is the page `cfg lock` names and not a second idea of what a lock is: the three moments the
// system covers the glass by itself are `kernel::lock`'s, and this key is the person asking for the same page now (docs/30, docs/60).
void open_the_lock() { open_a_chosen_app(cfg::lock_app(), "lock", "cfg lock <id>"); }

// **La casa: la pàgina de casa, i cap altra cosa** (0.189.139, l'amo: *«Afegirem un nou botó al panell de sistema, un icona d'una
// casa per arrencar HOME APP»*). It is the same door the system takes when the app in front goes and nobody asked for another one
// (`shell::start_the_home_app` asks nothing of the glass and starts the page of the start), so the key and the road out of a page
// cannot disagree about which app is home —it is `cfg::home_app()`, the name a person chose (docs/30, docs/40, docs/90).
//
// **Amb una altra pàgina a sota es demana; sense cap, o sobre la de casa mateixa, s'arrenca**: the two cases are the two the gear
// has, and the reason is the same —a name written down for the next turn needs a page to close (`shell::ask_run`), and a board with
// nothing in front starts the app now— (docs/30, docs/40). **Sobre la de casa mateixa la clau no es pinta des de la 0.189.153**
// (`key_is_there`), o sigui que el camí de tornar-la a arrencar només el demana el cas de cursa: la targeta es va pintar amb una
// altra app al davant i el dit s'aixeca quan la pàgina d'inici ja hi és —i llavors el que la persona vol és el que ha demanat,
// no un toc que no fa res (docs/30, docs/45).
void open_the_home() {
  const char* home = cfg::home_app();
  if (home == nullptr || home[0] == '\0') {
    log_printf(pal::kLogInfo, "PANEL", "the house key: no app of the start is chosen (`cfg home <id>`)");
    return;
  }
  apps::Entry entry;
  if (!apps::read(home, entry)) {
    log_printf(pal::kLogWarn, "PANEL", "the house key: there is no app '%s' on this board", home);
    return;
  }
  // **Sobre la pàgina que obriria, la torna a arrencar** (0.189.140): the same road as a board with nothing in front —close what
  // there is and start it—, i des de la 0.189.153 això només ho demana la cursa de la targeta pintada abans que la pàgina canviïs,
  // perquè sobre la pàgina d'inici la clau no hi és (docs/30, docs/45).
  const bool already = shell::app_running() && strcmp(shell::app_id(), entry.id) == 0;
  if (!shell::app_running() || already) {
    g_open = false;
    ui::forget_the_page_hold();
    if (!shell::start_app(entry.id, /*splash=*/true)) {
      log_printf(pal::kLogWarn, "PANEL", "the house key: '%s' did not come up", entry.id);
      return;
    }
    log_printf(pal::kLogInfo, "PANEL", "the house key: %s %s", entry.id,
               already ? "starts again from its first page" : "comes up with no page under it");
    return;
  }
  char why[96] = {0};
  if (!shell::ask_run(entry.id, /*back=*/true, nullptr, why, sizeof(why))) {
    log_printf(pal::kLogWarn, "PANEL", "the house key: %s could not be asked for: %s", entry.id, why);
    return;
  }
  put_it_away();
  log_printf(pal::kLogInfo, "PANEL", "the house key: %s comes up", entry.id);
}

// **Un botó és la seva acció**: the eight of the card, one switch, so the paint, the hit test and the press cannot disagree about what
// a key does (docs/30, docs/45).
void run_the_key(Key which) {
  switch (which) {
    case Key::kBack:
      go_back_a_view();
      return;
    case Key::kHome:
      open_the_home();
      return;
    case Key::kClose:
      close_the_app();
      return;
    case Key::kSettings:
      open_the_settings();
      return;
    case Key::kLock:
      open_the_lock();
      return;
    case Key::kReboot:
      restart_the_board();
      return;
    case Key::kDeepSleep:
      sleep_with_the_keys();
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
  // **Es torna a preguntar què hi ha** (0.190.81): una app es pot haver instal·lat o esborrat des de l'última vegada que la targeta
  // va sortir, i el que es pinta és el que hi ha ara i no el que hi havia (docs/30, docs/40).
  read_what_is_present();
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
        // **Una tecla que ja no hi és no es repinta** (0.189.117): `g_sunk` is the key the finger is on, and it outlives the
        // card that had it —the arrow of a page that went back is not a key of the card of the page below, which has one page
        // and no way back—. Painting it here put the arrow of the page before on the card of the one after, with `views` at
        // zero and the loop of `paint()` skipping it, which is what a case of the panel found on the glass (docs/30, docs/45).
        if (g_sunk >= 0 && key_is_there(static_cast<Key>(g_sunk))) {
          paint_the_key(*surface, static_cast<Key>(g_sunk), /*pressed=*/false);
        }
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
