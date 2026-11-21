#ifndef LUNOK_HAL_ESP32_BLUETOOTH_H
#define LUNOK_HAL_ESP32_BLUETOOTH_H

#include <cstdint>

#include "hal/hal.h"

namespace hal {

// **El teclat de Bluetooth d'aquesta placa** (0.190.179, l'amo: *«afegeix bluetooth al projecte, anem a fer un experiment»*): the
// radio of this chip can do one thing this system needs —**be a keyboard** for another device—, and it is a capability and not a
// network: there is nothing to connect to, there are characters to send (docs/50).
//
// **La pila és NimBLE i no la del fabricant** (l'amo: *«Segur que no te nimble?»*). Bluedroid costa 684 KB de flash i uns 50-70 KB de
// heap (mesurat el 31/10, docs/75) i en aquest xip sense PSRAM no hi són; NimBLE és el host petit, i el perfil HID s'escriu aquí perquè
// `esp_hid` d'aquesta IDF només té transport per a Bluedroid (el seu `CMakeLists.txt` enllaça `ble_hidd.c` sota
// `CONFIG_BT_BLUEDROID_ENABLED`) (docs/50, docs/75).
//
// **I el ràdio vol el xip despert**: while this is on, a light sleep stops the clocks the link lives on, so `wants_the_chip_awake()`
// answers for the pany of the turn and the board spends the time awake —the price of being a keyboard (docs/50, docs/60).
class Esp32Bluetooth : public IBluetooth {
 public:
  bool can_bluetooth() const override;
  bool keyboard_start(const char* name) override;
  void keyboard_stop() override;
  bool keyboard_connected() const override;
  bool keyboard_tap(uint8_t mods, uint8_t usage) override;
  bool wants_the_chip_awake() const override;
};

// **La instància d'aquesta banda**, que cada placa torna des del seu `platform` (0.190.179): a board whose build has no radio gets the
// object that answers that it cannot, o sigui que el nucli no ha de recordar cap punter nul (docs/50).
IBluetooth* esp32_bluetooth();

}  // namespace hal

#endif  // LUNOK_HAL_ESP32_BLUETOOTH_H
