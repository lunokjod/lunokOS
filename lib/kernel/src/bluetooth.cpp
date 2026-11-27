// **El teclat de Bluetooth, que és una porta i no una xarxa** (0.190.179, l'amo: *«afegeix bluetooth al projecte, anem a fer un
// experiment»*). The board owns the radio —it knows about NimBLE and about the HID service— and this file is the policy: whether the
// board has one at all, what a person writes turned into HID usage IDs, and the one rule the rest of the system has to know, which is
// that a radio on means a chip that does not nap (docs/30, docs/50, docs/60).
#include "kernel/bluetooth.h"

#include <string.h>

#include "hal/hal.h"
#include "kernel/cfg.h"
#include "kernel/log.h"
#include "kernel/meter.h"
#include "kernel/shell.h"
#include "pal/pal.h"

namespace kernel {
namespace bt {
namespace {

hal::IBluetooth* the_radio() { return hal::platform().bluetooth(); }

// **Quan es va encendre**, perquè el llum de la ràdio respira i un respir té un començament (0.190.182) (docs/30, docs/50).
int64_t g_on_since_ms = 0;

// **La identitat del beacon, que és del nucli i no de la ràdio** (0.194.0): the radio copies it when it advertises —it has to, because it
// keeps announcing by itself— but the numbers a person reads at the prompt are these, i el que es pot canviar sense reiniciar res és
// aquestes (docs/30, docs/50).
struct BeaconProfile {
  uint8_t uuid[16];
  uint16_t major;
  uint16_t minor;
  int8_t power;
};
BeaconProfile g_beacon = {{0x6c, 0x75, 0x6e, 0x6f, 0x6b, 0x4f, 0x53, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01},
                          kBeaconMajor, kBeaconMinor, kBeaconPower};

// **Un caràcter d'aquest teclat, en el codi que entén un altre aparell** (docs/50): the HID usage page for keyboards numbers the keys
// from `0x04` (the `a`) to `0x65`, the digits are `0x1E` to `0x27`, and everything a keyboard types with the other hand needs the
// Shift bit —`0x02` in the first byte of the report—. A character this table does not know sends nothing, which is the honest answer
// for a key that does not exist (docs/30).
struct Key {
  uint8_t mods;
  uint8_t usage;
};

Key key_of(char c) {
  if (c >= 'a' && c <= 'z') return {0x00, static_cast<uint8_t>(0x04 + (c - 'a'))};
  if (c >= 'A' && c <= 'Z') return {0x02, static_cast<uint8_t>(0x04 + (c - 'A'))};
  if (c >= '1' && c <= '9') return {0x00, static_cast<uint8_t>(0x1E + (c - '1'))};
  switch (c) {
    case '0': return {0x00, 0x27};
    case ' ': return {0x00, 0x2C};
    // **L'<intro>** (0.190.280, l'amo: *«a l'app btHID Keyboard… no s'envía l' <intro>»*): the key Enter of the HID is usage 0x28 and a
    // page that mirrors a box asks for it with the character a line ends in. The band of the system hands the box a **13** (`\r`) and
    // a box of one line turns it into `on_enter` and not into a letter —so the app sends this one itself—, while a box of several
    // lines writes the 10. Les dues són la mateixa tecla i la taula no les ha de distingir (docs/30, docs/50).
    case '\r':
    case '\n': return {0x00, 0x28};
    // **El retrocés** (0.190.204, l'amo: *«el teclat gracies a un Textbox»*): a page that mirrors a box into somebody else's cursor needs
    // the key that erases, i el `\b` és el caràcter que una caixa esborra quan en té un. Usage 0x2A és Backspace al HID (docs/50).
    case '\b': return {0x00, 0x2A};
    case '\t': return {0x00, 0x2B};
    case '-': return {0x00, 0x2D};
    case '_': return {0x02, 0x2D};
    case '=': return {0x00, 0x2E};
    case '+': return {0x02, 0x2E};
    case '[': return {0x00, 0x2F};
    case '{': return {0x02, 0x2F};
    case ']': return {0x00, 0x30};
    case '}': return {0x02, 0x30};
    case '\\': return {0x00, 0x31};
    case '|': return {0x02, 0x31};
    case ';': return {0x00, 0x33};
    case ':': return {0x02, 0x33};
    case '\'': return {0x00, 0x34};
    case '"': return {0x02, 0x34};
    case '`': return {0x00, 0x35};
    case '~': return {0x02, 0x35};
    case ',': return {0x00, 0x36};
    case '<': return {0x02, 0x36};
    case '.': return {0x00, 0x37};
    case '>': return {0x02, 0x37};
    case '/': return {0x00, 0x38};
    case '?': return {0x02, 0x38};
    case '!': return {0x02, 0x1E};
    case '@': return {0x02, 0x1F};
    case '#': return {0x02, 0x20};
    case '$': return {0x02, 0x21};
    case '%': return {0x02, 0x22};
    case '^': return {0x02, 0x23};
    case '&': return {0x02, 0x24};
    case '*': return {0x02, 0x25};
    case '(': return {0x02, 0x26};
    case ')': return {0x02, 0x27};
    default: return {0x00, 0x00};
  }
}

}  // namespace

bool available() {
  hal::IBluetooth* radio = the_radio();
  return radio != nullptr && radio->can_bluetooth();
}

bool start(const char* name) {
  hal::IBluetooth* radio = the_radio();
  if (radio == nullptr || !radio->can_bluetooth()) return false;
  const bool was_working = radio_is_working();
  // **El que costa encendre-la**: la pila de NimBLE i els seus trenta kilobytes de heap, que és la xifra que decideix si el ràdio
  // es pot quedar en una placa sense PSRAM (docs/50, docs/75).
  meter::Scope metering(meter::kRadioUp);
  // **I no hi ha cap mode a tornar a aplicar** (0.191.0): el mapa d'aquesta placa és sempre el mateix, o sigui que l'únic que calia
  // abans d'encendre la ràdio —que el que la pila servís fos el de la botiga i no un teclat de sèrie— ja no existeix, i amb ell se'n va
  // la parella de perills que hi havia al darrere: esborrar un vincle viu o néixer amb el mapa d'una altra arrencada (docs/30, docs/50).
  const bool ok = radio->keyboard_start(name);
  // **Quan el Bluetooth s'encén i quan no** (0.197.7, l'amo: *«Vull logs quan la wifi o el ble s'activen o s'apaguen»*): the failure
  // used to be a `false` and nothing else, i una placa que no publica res no deixava cap rastre de per què (docs/30, docs/50).
  if (!ok) {
    log_printf(pal::kLogWarn, "BT", "the device did not start: the stack of this board said no");
  }
  if (ok) {
    // **El començament és el de la ràdio i no el d'un costat** (0.190.195): the breath of the light starts when the radio starts doing
    // something, o sigui que encendre el dispositiu quan l'amfitrió ja escoltava no reinicia el dibuix (docs/30, docs/50).
    if (!was_working) g_on_since_ms = pal::system().millis();
    // **I el ràdio mana sobre la son** (0.190.179): un son lleuger atura els rellotges que el link viu, o sigui que el torn rep la
    // resposta de `wants_the_chip_awake` i no obre el pany mentre això és engegat (docs/50, docs/60).
    // **El registre diu el que la placa és i no el nom de la porta** (0.191.0): la placa publica el teclat i el ratolí, o sigui que la línia
    // que deia «the keyboard is on» deixava mitja veritat a fora (docs/50).
    log_printf(pal::kLogInfo, "BT", "the keyboard and the mouse are on as '%s': the waits of this board are spent awake while it lasts",
               (name != nullptr) ? name : "");
  }
  return ok;
}

void stop() {
  hal::IBluetooth* radio = the_radio();
  if (radio == nullptr) return;
  // **El beacon és l'altra cosa que aquest costat pot ser** (0.194.0): `bt dev off` vol dir que aquesta placa no diu res a l'aire, i un
  // beacon que sobreviu a l'ordre és exactament el contrari (docs/30, docs/50).
  radio->beacon_stop();
  radio->keyboard_stop();
  log_printf(pal::kLogInfo, "BT", "the device is off: nothing of this board is on the air");
}

bool is_on() {
  hal::IBluetooth* radio = the_radio();
  return radio != nullptr && radio->wants_the_chip_awake();
}

// ------------------------------------------------------------------ l'iBeacon (0.194.0)

const char kBeaconUuid[] = "6c756e6f-6b4f-5321-0000-000000000001";

namespace {

int hex_of(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

// **Un UUID de text a setze bytes**: els guions es salten on siguin —un UUID s'escriu amb quatre, i el que importa són els trenta-dos
// dígits— i el que no és un dígit hexadecimal és el que fa que això digui que no (docs/30, docs/50).
bool beacon_uuid_from_text(const char* text, uint8_t out[16]) {
  if (text == nullptr || out == nullptr) return false;
  uint8_t made[16] = {};
  size_t at = 0;
  for (const char* p = text; *p != '\0'; ++p) {
    if (*p == '-' || *p == ':' || *p == ' ') continue;
    const int high = hex_of(*p);
    if (high < 0) return false;
    ++p;
    const int low = hex_of(*p);
    if (low < 0) return false;
    if (at >= sizeof made) return false;
    made[at++] = static_cast<uint8_t>((high << 4) | low);
  }
  if (at != sizeof made) return false;
  memcpy(out, made, sizeof made);
  return true;
}

const char* beacon_uuid_text(const uint8_t uuid[16]) {
  static char text[48];
  const uint8_t* u = (uuid != nullptr) ? uuid : g_beacon.uuid;
  snprintf(text, sizeof(text), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", u[0], u[1], u[2],
           u[3], u[4], u[5], u[6], u[7], u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
  return text;
}

const uint8_t* beacon_uuid() { return g_beacon.uuid; }

bool beacon_start(const uint8_t uuid[16], uint16_t major, uint16_t minor, int8_t power) {
  hal::IBluetooth* radio = the_radio();
  if (radio == nullptr || !radio->can_bluetooth() || uuid == nullptr) return false;
  hal::IBluetooth::Beacon beacon;
  memcpy(beacon.uuid, uuid, sizeof beacon.uuid);
  beacon.major = major;
  beacon.minor = minor;
  beacon.power = power;
  // **El que costa encendre-la**: la pila de NimBLE i els seus trenta kilobytes, igual que el teclat (docs/50, docs/75).
  meter::Scope metering(meter::kRadioUp);
  const bool was_working = radio_is_working();
  if (!radio->beacon_start(beacon)) {
    // **I el motiu, al registre**: the shell says «the reason is in the log», and a radio that says no without a word is a command that
    // lies about where the answer is (docs/30, docs/50).
    log_printf(pal::kLogWarn, "BT", "this radio did not put the beacon in the air");
    return false;
  }
  memcpy(g_beacon.uuid, uuid, sizeof g_beacon.uuid);
  g_beacon.major = major;
  g_beacon.minor = minor;
  g_beacon.power = power;
  if (!was_working) g_on_since_ms = pal::system().millis();
  return true;
}

void beacon_stop() {
  hal::IBluetooth* radio = the_radio();
  if (radio == nullptr) return;
  radio->beacon_stop();
}

bool beacon_on() {
  hal::IBluetooth* radio = the_radio();
  return radio != nullptr && radio->beacon_on();
}

uint16_t beacon_major() { return g_beacon.major; }
uint16_t beacon_minor() { return g_beacon.minor; }
int8_t beacon_power() { return g_beacon.power; }

bool connected() {
  hal::IBluetooth* radio = the_radio();
  return radio != nullptr && radio->keyboard_connected();
}

bool tap(uint8_t mods, uint8_t usage) {
  hal::IBluetooth* radio = the_radio();
  if (radio == nullptr) return false;
  // **Una tecla enviada**, amb els dotze mil·lisegons que espera entre prémer i deixar anar: és el que costa escriure cap a fora,
  // i el que fa que `bt type` d'una frase sigui una mesura i no una endevinalla (docs/50).
  meter::Scope metering(meter::kRadioTap);
  return radio->keyboard_tap(mods, usage);
}

size_t type(const char* text) {
  if (text == nullptr) return 0;
  size_t sent = 0;
  for (const char* p = text; *p != '\0'; ++p) {
    const Key key = key_of(*p);
    if (key.usage == 0) continue;
    if (!tap(key.mods, key.usage)) return sent;
    ++sent;
  }
  return sent;
}

int64_t on_since_ms() { return g_on_since_ms; }

// **El que la ràdio és, amb els dos costats comptats** (0.190.194, l'amo: *«Si es posen a off tant dev com host, bt ha de ser off»*):
// `off` vol dir que no hi ha cap dels dos, i qualsevol que hi sigui ja és un `on`. El dispositiu diu a més si algú s'hi ha connectat,
// o sigui que una placa que ofereix un teclat i ningú el veu no és el mateix que una que té algú escrivint-hi (docs/30, docs/50).
const char* state_word() {
  if (!available()) return "this board has no radio";
  if (beacon_on()) return host_is_up() ? "on, a beacon and a host" : "on, a beacon";
  const bool device = is_on();
  const bool host = host_is_up();
  if (!device && !host) return "off";
  if (device && host) return connected() ? "on, a keyboard with a device on it and a host" : "on, a keyboard and a host";
  if (device) return connected() ? "on, a keyboard with a device on it" : "on, a keyboard, offered and waiting";
  return host_connected() ? "on, a host with a keyboard writing into it" : "on, a host";
}

// ------------------------------------------------------------------ l'amfitrió (0.190.188)
//
// **La mateixa ràdio, l'altra direcció** (l'amo: *«Ja tenim bluetooth HID device, fem el Bluetooth HID Host»*). The radio gives eight
// bytes of a HID report and nothing else; what a key means is this file's business, and the way it means it is **the same road the
// wire takes**: `shell::feed`. El prompt, les apps, l'editor de línia, Ctrl+D i Escape no saben —ni han de saber— si la tecla ha vingut
// d'un cable o de l'aire (docs/30, docs/50).

namespace {

const uint8_t kShiftMask = 0x22;    // Shift esquerre i dret, els dos bits del primer byte
const uint8_t kControlMask = 0x11;  // Control esquerre i dret, els dos bits del primer byte

// **La taula de tecles, girada**: `key_of` diu quin Usage ID és un caràcter; el que arriba d'un teclat és el camí al revés. No es copia
// la taula —es pregunta la que ja hi ha— perquè dues taules de tecles són dues taules que s'acaben desdient (docs/50).
char char_of(uint8_t usage, bool shifted) {
  for (char c = ' '; c < 0x7f; ++c) {
    const Key key = key_of(c);
    if (key.usage != usage) continue;
    if (((key.mods & kShiftMask) != 0) == shifted) return c;
  }
  return '\0';
}

// **Les tecles que no són un caràcter**: the arrows and the delete are escape sequences because that is what the line editor and the
// apps already read from a wire, o sigui que el teclat de l'aire no inventa un tercer dialecte (docs/30, docs/85).
bool special(uint8_t usage, char* text, size_t& used, size_t cap) {
  const char* sequence = nullptr;
  size_t length = 0;
  switch (usage) {
    case 0x28: sequence = "\r"; length = 1; break;      // Enter
    case 0x29: sequence = "\x1b"; length = 1; break;    // Escape
    case 0x2A: sequence = "\x7f"; length = 1; break;    // Backspace
    case 0x2B: sequence = "\t"; length = 1; break;      // Tab
    case 0x4A: sequence = "\x1b[H"; length = 3; break;  // Home
    case 0x4C: sequence = "\x1b[3~"; length = 4; break; // Delete
    case 0x4D: sequence = "\x1b[F"; length = 3; break;  // End
    case 0x4F: sequence = "\x1b[C"; length = 3; break;  // Right
    case 0x50: sequence = "\x1b[D"; length = 3; break;  // Left
    case 0x51: sequence = "\x1b[B"; length = 3; break;  // Down
    case 0x52: sequence = "\x1b[A"; length = 3; break;  // Up
    default: return false;
  }
  if (used + length > cap) return true;
  memcpy(text + used, sequence, length);
  used += length;
  return true;
}

// **On arriben les tecles del teclat d'algú altre**: eight bytes, the modifiers and up to six keys, and the reports of a key held down
// arrive again and again —the board is told about the key and not about the repeats, o sigui que el que és igual a l'últim no es
// torna a escriure (docs/50).
// **El ratolí, que és el mateix servei amb un altre report** (0.190.191): three bytes —the buttons and the movement in x and y— and a
// fourth when it has a wheel. El que aquest sistema en fa és **comptar-lo** i deixar que el prompt i les apps ho preguntin: un cursor a
// la pàgina és una feina de la UI i no d'aquest fitxer (docs/30, docs/50).
int16_t g_mouse_x = 0;
int16_t g_mouse_y = 0;
uint8_t g_mouse_buttons = 0;
bool g_mouse_seen = false;

void mouse_arrived(const uint8_t* report, uint8_t len) {
  const uint8_t buttons = report[0];
  g_mouse_seen = true;
  g_mouse_x = static_cast<int16_t>(g_mouse_x + static_cast<int8_t>(report[1]));
  g_mouse_y = static_cast<int16_t>(g_mouse_y + static_cast<int8_t>(report[2]));
  // **Els botons es diuen quan canvien i el moviment no**: a mouse moves dozens of times a second and a line for each one is a log
  // nobody can read, o sigui que el que queda al registre és el clic (docs/50).
  if (buttons != g_mouse_buttons) {
    g_mouse_buttons = buttons;
    log_printf(pal::kLogInfo, "BT", "the mouse: button(s) %u, at %d, %d", static_cast<unsigned>(buttons), g_mouse_x, g_mouse_y);
  }
  (void)len;
}

void keys_arrived(const uint8_t* report, uint8_t len) {
  if (report == nullptr) return;
  // **Un teclat són vuit bytes i un ratolí tres o quatre**: the length is the one honest thing the report has for telling them apart
  // without the report map, i el que arriba més curt no és cap teclat (docs/50).
  if (len >= 3 && len < 8) {
    mouse_arrived(report, len);
    return;
  }
  if (len < 8) return;
  static uint8_t last[8] = {};
  if (memcmp(last, report, sizeof last) == 0) return;
  memcpy(last, report, sizeof last);
  const uint8_t mods = report[0];
  const bool shifted = (mods & kShiftMask) != 0;
  const bool control = (mods & kControlMask) != 0;
  char text[24];
  size_t used = 0;
  for (int i = 2; i < 8; ++i) {
    const uint8_t usage = report[i];
    if (usage == 0) continue;
    if (special(usage, text, used, sizeof text)) continue;
    char c = char_of(usage, shifted);
    if (c == '\0') continue;
    // **I el Control és el d'un teclat**: `a` amb Control és `0x01`, que és el que el prompt ja entén com a `Ctrl+D` i `Ctrl+C`.
    if (control && c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 1);
    if (used < sizeof text) text[used++] = c;
  }
  if (used > 0) shell::feed(text, used);
}

}  // namespace

// **Si el ràdio té alguna cosa a què tornar, sense encendre'l per preguntar-ho** (0.190.203): la resposta és del HAL.
bool host_remembers() {
  hal::IBluetooth* radio = the_radio();
  return radio != nullptr && radio->host_remembers();
}

// **Connectat i escoltat són dues coses** (0.190.208): the host writes the CCCD of the report it wants a moment after it connects, i
// una pàgina que només mira `connected()` pot estar enviant tecles a un aparell que encara no les rep (docs/30, docs/50).
//
// **I des de la 0.191.0 es pot preguntar per un dels dos reports** (l'amo: *«i no podem anunciar els dos dispositius sempre?»*): la placa
// en publica dos i l'altre cap pot encendre'n un i no l'altre, o sigui que una pàgina que mou un punter vol saber del ratolí. `which` a
// nul vol dir «algun dels dos», que és el que una persona vol saber d'un cop d'ull (docs/30, docs/50).
bool reports_enabled(const char* which) {
  hal::IBluetooth* radio = the_radio();
  if (radio == nullptr) return false;
  if (which == nullptr) return radio->reports_enabled();
  if (strcmp(which, "mouse") == 0) return radio->report_enabled(hal::IBluetooth::Report::kMouse);
  if (strcmp(which, "keyboard") == 0) return radio->report_enabled(hal::IBluetooth::Report::kKeyboard);
  return false;
}

bool can_host() {
  hal::IBluetooth* radio = the_radio();
  return radio != nullptr && radio->can_host();
}

uint8_t host_scan(uint32_t ms, Peer* out, uint8_t cap) {
  hal::IBluetooth* radio = the_radio();
  if (radio == nullptr || out == nullptr || cap == 0) return 0;
  hal::IBluetooth::Peer found[8] = {};
  // **L'exploració, que és el que una persona espera mirant el prompt**: tres segons de sèrie i el que costi el ràdio a sobre (docs/50).
  meter::Scope metering(meter::kRadioScan);
  const uint8_t count = radio->host_scan(ms, found, 8);
  const uint8_t room = (count < cap) ? count : cap;
  for (uint8_t i = 0; i < room; ++i) {
    memcpy(out[i].addr, found[i].addr, sizeof out[i].addr);
    out[i].addr_type = found[i].addr_type;
    for (size_t j = 0; j < sizeof out[i].name - 1; ++j) {
      out[i].name[j] = static_cast<char>(found[i].name[j]);
      if (out[i].name[j] == '\0') break;
    }
    out[i].name[sizeof out[i].name - 1] = '\0';
    out[i].hid = found[i].hid;
    out[i].rssi = found[i].rssi;
    out[i].appearance = found[i].appearance;
    out[i].service = found[i].service;
  }
  return room;
}

bool host_pair(uint8_t index) {
  hal::IBluetooth* radio = the_radio();
  if (radio == nullptr) return false;
  // **Connectar, descobrir i subscriure's**, que és una sola espera des del prompt i quatre feines del ràdio (docs/50).
  meter::Scope metering(meter::kRadioPair);
  return radio->host_pair(index);
}

bool host_connected() {
  hal::IBluetooth* radio = the_radio();
  return radio != nullptr && radio->host_connected();
}

bool host_reconnect() {
  hal::IBluetooth* radio = the_radio();
  if (radio == nullptr) return false;
  // **Tornar a un aparell ja conegut costa el mateix que lligar-s'hi**: connectar i subscriure's, sense descobrir (docs/50).
  meter::Scope metering(meter::kRadioPair);
  return radio->host_reconnect();
}

bool host_on() {
  hal::IBluetooth* radio = the_radio();
  if (radio == nullptr) return false;
  const bool was_working = radio_is_working();
  // **La segona porta del ràdio**, amb el mateix preu que la primera: la pila i el heap (docs/50).
  meter::Scope metering(meter::kRadioUp);
  const bool up = radio->host_on();
  if (up && !was_working) g_on_since_ms = pal::system().millis();
  // **Les dues bandes de la ràdio es diuen igual** (0.197.7): encendre l'amfitrió és encendre el Bluetooth, i el registre no ho ha de
  // fer endevinar (docs/30, docs/50).
  if (up) {
    log_printf(pal::kLogInfo, "BT", "the host side is on: this board listens to the other end");
  } else {
    log_printf(pal::kLogWarn, "BT", "the host side did not come up: the stack of this board said no");
  }
  return up;
}

const char* name() {
  hal::IBluetooth* radio = the_radio();
  return (radio != nullptr) ? radio->device_name() : "lunokOS";
}

bool radio_is_working() {
  hal::IBluetooth* radio = the_radio();
  if (radio == nullptr) return false;
  return radio->wants_the_chip_awake() || radio->host_is_up();
}

bool host_is_up() {
  hal::IBluetooth* radio = the_radio();
  return radio != nullptr && radio->host_is_up();
}

void host_off() {
  hal::IBluetooth* radio = the_radio();
  if (radio != nullptr) {
    radio->host_off();
    log_printf(pal::kLogInfo, "BT", "the host side is off");
  }
}

// **Ja no hi ha mode a triar** (0.191.0, l'amo: *«i no podem anunciar els dos dispositius sempre? després que hi hagi una app de teclat
// o de mouse és indiferent»*): `device_word`, `has_a_mouse` i `be` vivien aquí perquè la placa era una cosa o l'altra i el mapa es
// publicava segons la tria. Ara el mapa és el dels dos reports —el teclat l'1 i el ratolí el 2— i no es toca mai, o sigui que no hi ha
// res a canviar ni cap vincle a esborrar: el que una app faci amb la placa és cosa de l'app (docs/30, docs/50).

// **Oblidar l'altre cap** (0.191.2): el que es fa és dir-ho a la ràdio, que és qui té els vincles; i el registre i la resposta del prompt
// diuen el que queda per fer a l'altre costat, que és treure l'aparell de la seva llista (docs/50).
void forget_the_device() {
  hal::IBluetooth* radio = the_radio();
  if (radio != nullptr) radio->device_forget();
}

bool move(int8_t dx, int8_t dy) {
  hal::IBluetooth* radio = the_radio();
  return radio != nullptr && radio->mouse_move(dx, dy);
}

bool press(uint8_t buttons) {
  hal::IBluetooth* radio = the_radio();
  return radio != nullptr && radio->mouse_press(buttons);
}

// **Les dues portes d'una pàgina** (0.190.204): the HAL counts in signed bytes and a page counts in whatever a finger did, o sigui que
// el que arriba més enllà de cent vint-i-set es fa a trossos i no es desborda el signe; i el botó és una paraula perquè una pàgina no
// ha de saber què és el bit 0 d'un report (docs/30, docs/50).
bool move_for_a_page(int dx, int dy) {
  int left_x = dx;
  int left_y = dy;
  while (left_x != 0 || left_y != 0) {
    const int step_x = (left_x > 127) ? 127 : ((left_x < -127) ? -127 : left_x);
    const int step_y = (left_y > 127) ? 127 : ((left_y < -127) ? -127 : left_y);
    if (!move(static_cast<int8_t>(step_x), static_cast<int8_t>(step_y))) return false;
    left_x -= step_x;
    left_y -= step_y;
  }
  return true;
}

bool click_for_a_page(const char* button) {
  if (button == nullptr) return false;
  if (strcmp(button, "right") == 0) return click(0x02);
  if (strcmp(button, "middle") == 0) return click(0x04);
  if (strcmp(button, "left") == 0) return click(0x01);
  return false;
}

bool click(uint8_t buttons) {
  hal::IBluetooth* radio = the_radio();
  return radio != nullptr && radio->mouse_click(buttons);
}

uint8_t buttons() {
  hal::IBluetooth* radio = the_radio();
  return (radio != nullptr) ? radio->mouse_buttons() : 0;
}

uint8_t leds() {
  hal::IBluetooth* radio = the_radio();
  return (radio != nullptr) ? radio->keyboard_leds() : 0;
}

bool mouse_seen() { return g_mouse_seen; }
int16_t mouse_x() { return g_mouse_x; }
int16_t mouse_y() { return g_mouse_y; }
uint8_t mouse_buttons() { return g_mouse_buttons; }

void host_forget() {
  hal::IBluetooth* radio = the_radio();
  if (radio != nullptr) radio->host_forget();
}

void set_the_host_sink() {
  hal::IBluetooth* radio = the_radio();
  if (radio != nullptr) radio->host_sink(&keys_arrived);
}

// ------------------------------------------------------------------ la bateria i la consola per BLE (0.196.0)

namespace {

// **El que arriba per la consola de BLE és el que arriba pel cable**: the bytes are given to `shell::feed`, que és la mateixa porta que
// el teclat de l'amfitrió i el filferro, o sigui que el prompt, les fletxes, `Ctrl+D` i els retorns de carro es comporten igual
// (docs/30, docs/50).
void console_arrived(const uint8_t* data, uint16_t len) {
  if (data == nullptr || len == 0) return;
  shell::feed(reinterpret_cast<const char*>(data), len);
}

// **I el que la consola escriu cap a l'altre costat**: el mirall del PAL rep cada tros que surt pel sèrie —el prompt, la resposta d'una
// ordre i el registre quan `verbose` el posa aquí—, i el que es fa és donar-lo a la ràdio; si no hi ha ningú escoltant es perd, que és
// el que aquest camí ha de fer (docs/30, docs/50).
void console_out(const uint8_t* data, size_t len) {
  hal::IBluetooth* radio = the_radio();
  if (radio == nullptr || data == nullptr || len == 0) return;
  size_t done = 0;
  while (done < len) {
    const size_t left = len - done;
    const uint16_t piece = static_cast<uint16_t>((left > 512) ? 512 : left);
    if (!radio->console_send(data + done, piece)) return;
    done += piece;
  }
}

}  // namespace

bool battery_on() {
  hal::IBluetooth* radio = the_radio();
  if (radio == nullptr || !radio->can_bluetooth()) return false;
  return radio->battery_on();
}

void battery_off() {
  hal::IBluetooth* radio = the_radio();
  if (radio != nullptr) radio->battery_off();
}

bool battery_on_now() {
  hal::IBluetooth* radio = the_radio();
  return radio != nullptr && radio->battery_is_on();
}

bool battery_notify(int percent) {
  hal::IBluetooth* radio = the_radio();
  if (radio == nullptr || percent < 0) return false;
  const uint8_t level = static_cast<uint8_t>((percent > 100) ? 100 : percent);
  return radio->battery_level(level);
}

bool console_on() {
  hal::IBluetooth* radio = the_radio();
  if (radio == nullptr || !radio->can_bluetooth()) return false;
  if (!radio->console_on()) return false;
  radio->console_sink(&console_arrived);
  // **I el mirall del sèrie, que és el que fa que sigui «el mateix»** (l'amo: *«el mateix que es veu via serial, però per ble»*): without
  // it the other side would type into a console that never answers (docs/30, docs/50).
  pal::IConsole* console = pal::system().console();
  if (console != nullptr) console->mirror(&console_out);
  return true;
}

void console_off() {
  hal::IBluetooth* radio = the_radio();
  if (radio == nullptr) return;
  pal::IConsole* console = pal::system().console();
  if (console != nullptr) console->mirror(nullptr);
  radio->console_off();
}

bool console_on_now() {
  hal::IBluetooth* radio = the_radio();
  return radio != nullptr && radio->console_is_on();
}

}  // namespace bt
}  // namespace kernel
