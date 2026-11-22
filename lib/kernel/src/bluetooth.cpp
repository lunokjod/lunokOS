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
  // **I la placa torna a ser el que es va triar, ABANS que la ràdio arrenqui** (0.190.208, i mogut aquí a la 0.190.259): the mode is kept
  // by `cfg` and the radio is born a keyboard, o sigui que el que s'ha d'aplicar és el de la botiga —una placa que es va deixar com a ratolí
  // ha de tornar com a ratolí (docs/30, docs/50)—.
  //
  // **I això va abans i no després per un motiu que es va veure al ratolí**: `device_be` sobre una ràdio ja engegada **esborra els vincles**,
  // perquè un amfitrió que va llegir un mapa no endevina l'altre; aplicar-ho DESPRÉS d'encendre-la volia dir que cada arrencada d'una placa
  // que és un ratolí esborrava el vincle que ella mateixa havia fet, i el telèfon s'havia de tornar a aparellar a cada reinici. Abans
  // d'encendre no hi ha res a esborrar: el mapa que el servei servirà és el que la ràdio llegirà en néixer.
  const char* wanted = cfg::bt_mode();
  if (wanted != nullptr && wanted[0] != '\0' && strcmp(wanted, device_word()) != 0) {
    (void)be(wanted);
    log_printf(pal::kLogInfo, "BT", "this board comes up as %s, as it was left", wanted);
  }
  const bool ok = radio->keyboard_start(name);
  if (ok) {
    // **El començament és el de la ràdio i no el d'un costat** (0.190.195): the breath of the light starts when the radio starts doing
    // something, o sigui que encendre el dispositiu quan l'amfitrió ja escoltava no reinicia el dibuix (docs/30, docs/50).
    if (!was_working) g_on_since_ms = pal::system().millis();
    // **I el ràdio mana sobre la son** (0.190.179): un son lleuger atura els rellotges que el link viu, o sigui que el torn rep la
    // resposta de `wants_the_chip_awake` i no obre el pany mentre això és engegat (docs/50, docs/60).
    log_printf(pal::kLogInfo, "BT", "the keyboard is on: the waits of this board are spent awake while it lasts");
  }
  return ok;
}

void stop() {
  hal::IBluetooth* radio = the_radio();
  if (radio == nullptr) return;
  radio->keyboard_stop();
}

bool is_on() {
  hal::IBluetooth* radio = the_radio();
  return radio != nullptr && radio->wants_the_chip_awake();
}

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
bool reports_enabled() {
  hal::IBluetooth* radio = the_radio();
  return radio != nullptr && radio->reports_enabled();
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
  if (radio != nullptr) radio->host_off();
}

// **El que aquesta placa és, en una paraula** (0.190.204): `keyboard`, `mouse` o `both`, que és el mode que una pàgina necessita per
// tenir un ratolí i un teclat alhora.
const char* device_word() {
  hal::IBluetooth* radio = the_radio();
  if (radio == nullptr) return "keyboard";
  switch (radio->device_is()) {
    case hal::IBluetooth::Device::kMouse: return "mouse";
    case hal::IBluetooth::Device::kBoth: return "both";
    default: return "keyboard";
  }
}

bool has_a_mouse() {
  hal::IBluetooth* radio = the_radio();
  return radio != nullptr && radio->device_is() != hal::IBluetooth::Device::kKeyboard;
}

// **Triar què és la placa** (0.190.204): the three words are the three modes, and a word this file does not know is a refusal and not
// an «I did it» —a page that shows a mouse and finds itself a keyboard is worse than one that is told no (docs/30, docs/50).
bool be(const char* what) {
  hal::IBluetooth* radio = the_radio();
  if (radio == nullptr || what == nullptr) return false;
  hal::IBluetooth::Device mode;
  if (strcmp(what, "mouse") == 0) {
    mode = hal::IBluetooth::Device::kMouse;
  } else if (strcmp(what, "both") == 0) {
    mode = hal::IBluetooth::Device::kBoth;
  } else if (strcmp(what, "keyboard") == 0) {
    mode = hal::IBluetooth::Device::kKeyboard;
  } else {
    return false;
  }
  const bool ok = radio->device_be(mode);
  if (ok) log_printf(pal::kLogInfo, "BT", "the device of this board is %s now", device_word());
  return ok;
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

}  // namespace bt
}  // namespace kernel
