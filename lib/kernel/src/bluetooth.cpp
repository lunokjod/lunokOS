// **El teclat de Bluetooth, que és una porta i no una xarxa** (0.190.179, l'amo: *«afegeix bluetooth al projecte, anem a fer un
// experiment»*). The board owns the radio —it knows about NimBLE and about the HID service— and this file is the policy: whether the
// board has one at all, what a person writes turned into HID usage IDs, and the one rule the rest of the system has to know, which is
// that a radio on means a chip that does not nap (docs/30, docs/50, docs/60).
#include "kernel/bluetooth.h"

#include <string.h>

#include "hal/hal.h"
#include "kernel/log.h"
#include "pal/pal.h"

namespace kernel {
namespace bt {
namespace {

hal::IBluetooth* the_radio() { return hal::platform().bluetooth(); }

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
  const bool ok = radio->keyboard_start((name != nullptr && name[0] != '\0') ? name : "lunokOS");
  if (ok) {
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
  return radio != nullptr && radio->keyboard_tap(mods, usage);
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

const char* state_word() {
  if (!available()) return "this board has no radio";
  if (!is_on()) return "off";
  return connected() ? "connected" : "offered and waiting for a device";
}

}  // namespace bt
}  // namespace kernel
