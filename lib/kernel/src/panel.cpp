// The card of the system that hangs from the bar: it opens with a drag down from the titlebar and goes with a drag up
// (docs/30).
#include "kernel/panel.h"
#include "kernel/panel_low.h"

#include <string.h>

#include "kernel/apps.h"
#include "kernel/bar.h"
#include "kernel/bluetooth.h"
#include "kernel/cfg.h"
#include "kernel/display.h"
#include "kernel/log.h"
#include "kernel/mesh.h"
#include "kernel/net.h"
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
// **L'aire entre l'última fila de claus i l'ansalada** (0.190.219): six rows, which is what keeps the handle from touching the box of a
// key —the ink of a key is centred in its box and the box has air of its own, però un handle enganxat a la vora d'una clau es llegeix
// com una clau més— (docs/30, docs/92).
const uint16_t kAirUnderTheKeys = 6;

// **Les set claus que queden a la targeta, en dues files** (0.192.24, l'amo: *«Tots els botons de fila de Settings han d'anar abaix:
// Settings, Home, Close i Back al panell de sistema inferior»*): the three that stop the board —the power off, **the deep sleep** and
// the restart— with the padlock, i **les tres ràdios** —the Bluetooth, the network and the mesh— a sota d'elles. Les quatre que
// naveguen —la roda, la casa, la creu i la fletxa— són a la targeta de baix (`panel_low`), que és on l'amo les ha demanades; el que
// fan ho sap aquest mòdul, perquè el portal i el shell que ho contesten són aquí (`a_page_key_is_there`, `press_a_page_key`) (docs/30).
//
// **Cada clau és un `UIButton` d'aquest sistema sense vores ni ombra** (`framed` false) —el que es veu és la marca i la paraula—, i
// s'enfonsa girant-se mentre el dit hi és, que és la premsa de tots els botons d'aquest vidre (l'amo: *«vull que siguin UIButton, però
// sense vores (afegir propietat al botó si no la te ja) i amb una imatge/icona com a contingut»*) (docs/30, docs/45, docs/92).
// **Cada clau porta la seva paraula** (0.185.3, l'amo: *«afegeix text per als botons; "Back" i "Close" al costat de la icona … als
// botons de Settings, Reboot i Powerdown posa text sota cada icona»*): una clau del sistema no és un quadrat d'una icona sinó **una
// icona amb el seu nom**, i el nom és dins del botó, o sigui que prémer el nom és prémer la clau.
//
//   - **les quatre de dalt són una mica més petites** (0.189.151, l'amo: *«A dalt de tot Poweroff, Sleep, Reboot una mica mes
//     petits (lleugerament, no tant petits com es ara "Home") a la part de dalt del panell»*): 104 × 88, la marca a sobre i la
//     paraula a sota;
//   - **el candau va amb els que paren la placa** (0.189.152, l'amo: *«Posa el lock amb els de poweroff, sleep i reboot: Power off,
//     Sleep, Reboot, Lock»*): la pàgina de `cfg lock` és una de les que paren la placa i no una de les que hi naveguen (docs/30, docs/60);
//   - **i les tres de la ràdio al mig** (0.190.219, l'amo: *«Posa 3 icones noves al panell de sistema: Una icona de bluetooth per
//     encendre/apagar, un de wifi i un de ble-mesh»*): tan amples com la paraula «Bluetooth» i no un píxel més, que el que diuen és
//     una paraula i no una marca (docs/30, docs/92).
const uint16_t kTopRowY = 20;      // la fila de les quatre que paren la placa
const uint16_t kTopW = 104;        // una mica més ampla i més baixa que les de sota
const uint16_t kTopH = 88;
const uint16_t kTopGap = 16;       // l'aire entre dues claus d'aquesta fila
// **I la fila del mig, la de les tres ràdios** (0.190.219, l'amo: *«Posa 3 icones noves al panell de sistema: Una icona de bluetooth per
// encendre/apagar, un de wifi i un de ble-mesh»*): three keys as wide as the word «Bluetooth» needs and not a pixel more, because what
// they say is a word and not a mark —the mark is above it, as in every key of this card (docs/30, docs/92).
const uint16_t kSwitchW = 156;
// **I una mica més altes des de la 0.192.46**: the circle of the state takes a band of three pixels off the square of the mark and the
// drawing has to keep the size it had —the owner: *«Fes les icones una mica més grans»*—, o sigui que la clau creix el que el cercle
// s'endú i uns píxels més: the mark of a key of 100 is a square of 78 and the drawing inside the ring is 68, against the 58 of before
// (docs/30, docs/92).
const uint16_t kSwitchH = 100;
const uint16_t kSwitchGap = 16;
// **L'aire entre les dues files** (0.192.24): the card kept the third of the glass it has always wanted, and with two rows instead of three the
// switches hang from the handle at the bottom instead of leaving a band of paper under them (docs/30, docs/92).
const uint16_t kRowsGap = 24;
// **Una sola escala per a set paraules** (0.185.3, 0.189.25 i 0.189.151): a un pas fa onze píxels i no es llegeix —es va veure a
// l'emulador—, i a dos la paraula més ampla, `Poweroff`, fa noranta-un píxels, que és el que una clau de noranta-sis aguanta.
const uint8_t kWordScale = 2;

// **Quin botó és quin** (0.185.0): les set de la targeta, en l'ordre en què les dues files es dibuixen —el corrent, **el son**, el
// reinici i el candau a dalt, amb el son entre el corrent i el reinici, que és on l'amo el va demanar: com més a prop de l'apagat és
// una manera de baixar, més a prop seu seu—, i **les tres de la ràdio** a sota, que són les tres coses d'aquesta placa que s'encenen i
// s'apaguen i que fins ara només es podien canviar des del prompt o des d'una app (docs/30, docs/45).
enum class Key : uint8_t {
  kPowerOff = 0,
  kDeepSleep = 1,
  kReboot = 2,
  kLock = 3,
  kBluetooth = 4,
  kWifi = 5,
  kMesh = 6
};
const uint8_t kKeyCount = 7;

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

uint16_t card_height();

// **La fila de les ràdios, penjada del mànec** (0.192.24): la targeta és el terç que sempre ha volgut, i amb dues files en lloc de tres les
// ràdios pengen del mànec de baix en lloc de deixar una banda de paper buit sota d'elles (docs/30, docs/92).
uint16_t switch_row_y() {
  const uint16_t below = static_cast<uint16_t>(kHandleGap + kHandleH + kLineH + kAirUnderTheKeys);
  return static_cast<uint16_t>(card_height() - below - kSwitchH);
}

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
  // **I la fila de sota: les tres ràdios** (0.190.219): the Bluetooth, the network and the mesh, centrades i de la mateixa mida, amb la
  // paraula a sota de la marca com la resta de la targeta. **Són les úniques que diuen un estat** i per això són les úniques que poden estar
  // apagades: la tinta de la seva marca i de la seva paraula és la que diu si la cosa és encesa (docs/30, docs/92).
  const int16_t total = static_cast<int16_t>(3 * kSwitchW + 2 * kSwitchGap);
  const int16_t left = static_cast<int16_t>((width - total) / 2);
  uint8_t place = 0;
  if (which == Key::kWifi) place = 1;
  if (which == Key::kMesh) place = 2;
  box.x = static_cast<int16_t>(left + place * (kSwitchW + kSwitchGap));
  box.y = static_cast<int16_t>(switch_row_y());
  box.w = kSwitchW;
  box.h = kSwitchH;
  box.word = (which == Key::kBluetooth) ? "Bluetooth" : (which == Key::kWifi) ? "WiFi" : "Mesh";
  return box;
}

const char* key_icon(Key which) {
  switch (which) {
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
    // **Les tres de la ràdio** (0.190.219): the rune, the fan and the net, drawn by `ui::draw_icon` with the primitives of this tree
    // perquè no depenen de cap fitxer del disc i hi són a qualsevol placa (docs/30, docs/45).
    case Key::kBluetooth:
      return "bluetooth";
    case Key::kWifi:
      return "wifi";
    case Key::kMesh:
      return "mesh";
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

// **Les quatre que naveguen no són d'aquesta targeta des de la 0.192.24**: `panel_low` les pinta i aquestes quatre preguntes són les
// seves —la creu és un botó només quan hi ha una pàgina que es pugui deixar (0.179.25), la fletxa quan hi ha una vista a sota, la casa
// quan no hi ets i la roda quan `cfg settings` nomena una app que hi és (0.189.150, 0.190.81)— (docs/30, docs/45).
bool the_close_key_is_there() { return shell::the_page_in_front_can_be_left(); }

bool the_back_key_is_there() { return apps::views() > 0; }

// **I sobre la pàgina d'inici la casa no hi és** (0.189.153, l'amo: *«No, el home si estem a home no ha de sortir, igual que el
// back no surt si no hi ha back o Close tampoc si estem al home»*): la clau de la casa és una manera **d'anar-hi**, i qui ja la mira
// no li pot demanar res (docs/30, docs/45).
bool the_home_key_is_there() {
  if (!g_present.home) return false;
  const char* home = cfg::home_app();
  return !(shell::app_running() && strcmp(shell::app_id(), home) == 0);
}

bool the_settings_key_is_there() { return g_present.settings; }

// **I el que sempre hi és són les tres que paren la placa** (0.190.81): `Poweroff` i `Reboot` no depenen de cap app, i `Sleep`
// depèn que la placa sàpiga dormir i prou; el candau depèn que `cfg lock` nomeni una app que hi sigui (docs/30, docs/45).
bool key_is_there(Key which) {
  // **I el candau, quan hi ha una app de bloqueig** (0.189.151): the same rule as the gear —`cfg lock none` is a padlock that
  // covers nothing, and the key is not painted— i la clau no mira el commutador: el commutador decideix els tres moments en què el
  // sistema es tapa sol, i aquesta clau és la persona demanant-ho ara (docs/30, docs/45, docs/60).
  if (which == Key::kLock) return g_present.lock;
  // **I el son amb les tecles vives, només si la placa en té una** (0.189.25): the same rule as the cross —a key that does
  // nothing says the system can do something it cannot— and it is asked of the board and not of the model of the glass
  // (docs/30, docs/45, docs/50).
  if (which == Key::kDeepSleep) return power::can_deep_sleep();
  // **I l'apagat, només si la placa es pot apagar de debò** (0.191.9, l'amo: *«el poweroff no funciona? està endollat i es queda amb el botó
  // poweroff activat»*). Era l'única clau de la targeta que no es preguntava a la placa: amb el cable a dins la T5S3 no pot tallar-se el
  // corrent —el carregador la sosté, i és la regla del fabricant—, o sigui que el botó es pintava, la persona el premia i no passava res: ni
  // una tinta ni una paraula. És exactament el que aquesta casa no fa amb la creu, la fletxa, la roda, la lluna ni les tres de la ràdio
  // (docs/30, docs/45, docs/50).
  if (which == Key::kPowerOff) return power::can_power_off();
  // **I les tres de la ràdio, quan la placa en té** (0.190.219): the same rule as the cross and the moon —a key that says it can turn a
  // radio on, on a board whose build carries no radio, says something untrue—, i es pregunta al build i no al model del vidre (docs/30,
  // docs/45, docs/50).
  if (which == Key::kBluetooth) return bt::available();
  if (which == Key::kWifi) return net::available();
  if (which == Key::kMesh) return mesh::available();
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

// **Les files que la targeta necessita, i prou** (0.192.25, l'amo: *«Ara els panells son molt grans per el contingut que tenen, no?»*): the card
// was a third of the glass from the 0.179.22, which was the shape of a panel with three rows of keys; with two, the third was a sheet of paper
// with a band of white under it. El que mana és el contingut —la fila de dalt, l'aire del mig, la fila de les ràdios i el mànec amb la seva
// línia—, i el terç només hi torna si el vidre és més baix que tot això (docs/30, docs/92).
uint16_t card_height() {
  const uint16_t glass = display::height();
  const uint16_t top = bar::height();
  if (glass <= top) return 0;
  const uint16_t rows = static_cast<uint16_t>(kTopRowY + kTopH + kRowsGap + kSwitchH + kHandleGap + kHandleH + kLineH +
                                               kAirUnderTheKeys);
  const uint16_t room = static_cast<uint16_t>(glass - top);
  return (rows < room) ? rows : room;
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
// **Si la cosa d'aquesta clau és encesa** (0.190.219): the three of the radios are the only keys of the card that have a state, and the
// state is not a second drawing: **the ink of the mark and of the word is the state**, black when the radio is on and grey when it is
// off, que és el mateix dos tons de sempre i no un commutador nou (docs/30, docs/92).
bool key_is_lit(Key which) {
  if (which == Key::kBluetooth) return cfg::bt_on();
  // **El wifi és encesa quan el commutador és encesa** (0.190.221, l'amo: *«de moment aquests botons son per apagar i encendre radios»*): és
  // el `cfg/wifi` que la clau encén i apaga, i no l'estat d'una connexió —aquest sistema usa la xarxa i la deixa anar quan la feina s'acaba,
  // i una clau que s'apagués tota sola cada cop que una tasca acaba seria una clau que no es pot llegir— (docs/30, docs/50).
  if (which == Key::kWifi) return cfg::wifi_on();
  if (which == Key::kMesh) return mesh::is_up();
  return true;
}

// **Les tres ràdios es veuen sempre, i el que diu l'estat és el cercle** (0.192.46, l'amo: *«Els botons del panell superior
// desplegable del sistema: Bluetooth, Wifi i l'altre han de ser sempre visibles, color negre i un cercle negre al voltant (sense
// farcit de negre) i en cas d'estar activat icona blanca amb cercle farcit negre»*). The ink of the mark and of the word used to be
// the state —black on and grey off—, and a grey word on paper read as a key that cannot be pressed, which is the one thing this card
// never draws. Now **the key is black whether the radio is on or off** and the circle says it: an empty circle around the mark is a
// radio that is off, and a filled one with the mark in paper is a radio that is on (docs/30, docs/92).
bool a_radio_key(Key which) {
  return which == Key::kBluetooth || which == Key::kWifi || which == Key::kMesh;
}

void paint_the_key(hal::ISurface& surface, Key which, bool pressed) {
  const KeyBox box = key_box(which);
  const uint8_t ring = a_radio_key(which) ? (key_is_lit(which) ? ui::kMarkRingOn : ui::kMarkRingOff) : ui::kMarkPlain;
  ui::paint_a_button(surface, static_cast<uint16_t>(box.x), static_cast<uint16_t>(bar::height() + box.y), box.w,
                     box.h, /*background=*/15, /*border=*/0, pressed, /*focused=*/false, /*primary=*/false,
                     box.word, /*color=*/0, box.scale, /*framed=*/false, key_icon(which), box.mark, ring);
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

// **La targeta que hi sigui se'n va** (0.192.24): les quatre claus que naveguen ja no són d'aquesta targeta —són a la de baix—, o sigui
// que qui les prem ha de tancar la que és fora, sigui quina sigui, i no només la que aquest fitxer recorda. El hold de la pàgina es deixa
// anar sense pintar (`paint` fals) quan el que ve darrere pinta el vidre sencer, i amb la rentada de la pàgina quan no ve res més
// (docs/30, docs/50).
void the_card_goes(bool paint) {
  if (panel_low::open()) panel_low::close_the_card();
  g_open = false;
  g_sunk = -1;
  if (paint) {
    ui::hold_the_page(false);
    return;
  }
  ui::forget_the_page_hold();
}

// **La fletxa torna l'app a la vista de sota** (0.179.52): `shell::go_back_one_view` is the door the pinch and the three taps
// already use for «one page back» —the app gives the top page back, hears `on_back(depth)` and repaints the one below—, so the
// button is a way of asking for the same thing and not a second behaviour (docs/30, docs/45).
//
// **I la targeta marxa abans que l'app pinti**: the hold of the page is dropped **without a paint** and the whole page is owed,
// so the present the app makes inside `on_back` is the page whole and the card goes with it. A paint here and another one there
// would be two passes of this glass for one finger, and the second would be the app's own (docs/50, docs/92).
void go_back_a_view() {
  if (!the_back_key_is_there()) return;
  the_card_goes(/*paint=*/false);
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
    the_card_goes(/*paint=*/false);
    log_printf(pal::kLogInfo, "PANEL", "the cross: the app is closed and its page comes back");
    return;
  }
  log_printf(pal::kLogInfo, "PANEL", "the cross: the app is closed");
  the_card_goes(/*paint=*/true);
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
  // **I si la placa no baixa, es diu** (0.191.11): the picture of the sleep has already come off and the page is on its way back
  // (`power::request_sleep`), o sigui que el que faltava era la paraula que lliga les dues coses al registre (docs/30, docs/50).
  if (!shell::deep_sleep("the card of the bar")) {
    log_printf(pal::kLogWarn, "PANEL", "the moon key: the board did not go down");
  }
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
    the_card_goes(/*paint=*/false);
    // **La targeta sí**: la persona ha de veure quina pàgina arriba (0.179.33, docs/30, docs/40).
    shell::AStart start;
    start.id = entry.id;
    if (!shell::start_a_page(start)) {
      log_printf(pal::kLogWarn, "PANEL", "the %s key: '%s' did not come up", name, entry.id);
      return;
    }
    log_printf(pal::kLogInfo, "PANEL", "the %s key: %s comes up with no page under it", name, entry.id);
    return;
  }
  // **I la pàgina que la demana queda darrere** (`back`): closing the page of settings comes back to where the person was, which is
  // what a card that opens an app from the bar owes the page under it (docs/30, docs/40).
  if (!shell::ask_run(entry.id, /*back=*/true, nullptr, nullptr, why, sizeof(why))) {
    log_printf(pal::kLogWarn, "PANEL", "the %s app is not there: %s", name, why);
    return;
  }
  the_card_goes(/*paint=*/true);
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
    the_card_goes(/*paint=*/false);
    shell::AStart start;
    start.id = entry.id;
    if (!shell::start_a_page(start)) {
      log_printf(pal::kLogWarn, "PANEL", "the house key: '%s' did not come up", entry.id);
      return;
    }
    log_printf(pal::kLogInfo, "PANEL", "the house key: %s %s", entry.id,
               already ? "starts again from its first page" : "comes up with no page under it");
    return;
  }
  char why[96] = {0};
  if (!shell::ask_run(entry.id, /*back=*/true, nullptr, nullptr, why, sizeof(why))) {
    log_printf(pal::kLogWarn, "PANEL", "the house key: %s could not be asked for: %s", entry.id, why);
    return;
  }
  the_card_goes(/*paint=*/true);
  log_printf(pal::kLogInfo, "PANEL", "the house key: %s comes up", entry.id);
}

// **Les tres claus de la ràdio, i el que fan** (0.190.219): each one is the road the prompt already has, not a second way of doing the same
// thing —`cfg::set_bt_on` is what `cfg bt on` calls, `cfg::set_wifi_on` is what `cfg wifi on` calls, `mesh::start` is what `mesh on`
// calls— and the key only says it with a finger (docs/30, docs/45).
//
// **I la targeta es queda on és**: turning a radio on is not a page, o sigui que la clau es torna a pintar amb la tinta que li toca i la
// targeta no es mou; el que ha passat és una línia del registre i, si no ha passat, també.
void toggle_the_bluetooth() {
  const bool want = !cfg::bt_on();
  (void)cfg::set_bt_on(want);
  // **El que mana és el que la ràdio ha fet i no el que se li ha demanat** (0.190.183): `set_bt_on` desa la tria i, si la ràdio no arrenca,
  // torna enrere; o sigui que el que es llegeix aquí és el resultat i no el desig (docs/30, docs/50).
  if (cfg::bt_on() == want) {
    log_printf(pal::kLogInfo, "PANEL", "the bluetooth key: the radio is %s", want ? "on" : "off");
  } else {
    log_printf(pal::kLogWarn, "PANEL", "the bluetooth key: the radio did not come up (the log above says why)");
  }
}

void toggle_the_network() {
  // **Encendre i apagar la ràdio, i res més** (0.190.221, l'amo: *«de moment aquests botons son per apagar i encendre radios»*): el primer
  // intent d'aquesta tecla connectava amb la xarxa desada, i el que l'amo volia és el commutador —el mateix que `cfg bt`—. `cfg::set_wifi_on`
  // és la porta que el prompt fa servir igual (`cfg wifi on|off`), o sigui que la tecla no inventa cap camí: encendre instal·la el driver i
  // arrenca l'estació, i apagar-la atura el driver i deixa anar el que hi hagués (docs/30, docs/45, docs/50).
  const bool want = !cfg::wifi_on();
  (void)cfg::set_wifi_on(want);
  if (cfg::wifi_on() == want) {
    log_printf(pal::kLogInfo, "PANEL", "the wifi key: the radio is %s", want ? "on" : "off");
  } else {
    log_printf(pal::kLogWarn, "PANEL", "the wifi key: the radio did not come up (the log above says why)");
  }
}

void toggle_the_mesh() {
  if (mesh::is_up()) {
    mesh::stop();
    log_printf(pal::kLogInfo, "PANEL", "the mesh key: the board is out of the mesh");
    return;
  }
  if (!mesh::start()) {
    log_printf(pal::kLogWarn, "PANEL", "the mesh key: the mesh did not come up (the log above says why)");
    return;
  }
  log_printf(pal::kLogInfo, "PANEL", "the mesh key: the mesh is up");
}

// **Tornar a pintar una clau que no tanca la targeta**: one box of the card and not the whole card, perquè el que ha canviat és la tinta
// d'una clau i el vidre cobra cada passada (docs/30, docs/50).
void paint_the_key_back(Key which) {
  hal::ISurface* surface = display::surface();
  if (surface == nullptr) return;
  if (!key_is_there(which)) return;
  paint_the_key(*surface, which, /*pressed=*/false);
  display::present(hal::UpdateMode::kDu);
}

// **Un botó és la seva acció**: the seven of the card, one switch, so the paint, the hit test and the press cannot disagree about what
// a key does (docs/30, docs/45). **Les quatre que naveguen tenen el seu commutador a `panel_low`** (0.192.24), i les accions són
// aquestes de sota, compartides (docs/30).
void run_the_key(Key which) {
  switch (which) {
    case Key::kLock:
      open_the_lock();
      return;
    case Key::kReboot:
      restart_the_board();
      return;
    case Key::kDeepSleep:
      sleep_with_the_keys();
      return;
    case Key::kBluetooth:
      toggle_the_bluetooth();
      // **I la clau es torna a pintar amb la tinta que li toca** (0.190.219): the press turned it over and the state it says may have
      // changed, o sigui que el que hi ha al vidre s'ha de tornar a dir o la clau mentiria fins al pròxim toc (docs/30, docs/92).
      paint_the_key_back(which);
      return;
    case Key::kWifi:
      toggle_the_network();
      paint_the_key_back(which);
      return;
    case Key::kMesh:
      toggle_the_mesh();
      paint_the_key_back(which);
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

// **Les quatre claus que naveguen** (0.192.24): la targeta de baix les pinta i aquestes dues portes són el que saben i el que fan, amb
// les mateixes preguntes de sempre —el portal, el shell i la pàgina que hi ha a sota— i cap segona idea de què és tornar enrere o
// tancar (docs/30, docs/45).
bool a_page_key_is_there(PageKey which) {
  switch (which) {
    case PageKey::kSettings:
      return the_settings_key_is_there();
    case PageKey::kHome:
      return the_home_key_is_there();
    case PageKey::kClose:
      return the_close_key_is_there();
    default:
      return the_back_key_is_there();
  }
}

void press_a_page_key(PageKey which) {
  switch (which) {
    case PageKey::kSettings:
      open_the_settings();
      return;
    case PageKey::kHome:
      open_the_home();
      return;
    case PageKey::kClose:
      close_the_app();
      return;
    default:
      go_back_a_view();
      return;
  }
}

void look_at_what_is_there() { read_what_is_present(); }

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
    // **I amb la targeta de baix a sobre no s'obre** (0.192.23): the drag that would pull this one out is the one that puts the
    // other away, and it belongs to the other.
    if (panel_low::open()) return;
    // **La tirada que obre la targeta es mesura al VIDRE i no a la vista** (0.192.33, l'amo: *«PERQUE LES PUTES BARRES DE SISTEMA NO?»*).
    // What opens the card is a drag that begins on the first rows of the glass and goes mostly down: `take_a_drag_down_from_the_bar`
    // asked it with `on_the_bar`, a conclusion of the turn of the frame that a view with no offset could leave at zero —and then
    // every finger of the bar was the page's and both cards were dead while the pages still worked—. The row of the glass cannot
    // say that. **And if the bar is not on the glass at all** —an app with the whole glass, docs/46 D1— the same gesture is the
    // person asking for the bar: it comes back and the card hangs from it (el mateix camí que va obrir 0.192.32, docs/30).
    touch::Gesture gesture;
    if (!touch::take_a_drag_down_from_the_system_rows(kPullPx, gesture)) return;
    if (!bar::visible() && !ui::full_screen(false)) {
      log_printf(pal::kLogWarn, "PANEL", "a pull from the whole glass did not give the bar back");
      return;
    }
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
      Key held = Key::kPowerOff;
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
      Key which = Key::kPowerOff;
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
