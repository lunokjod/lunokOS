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
  const char* device_name() const override;
  bool wants_the_chip_awake() const override;
  // **I l'altra direcció** (0.190.188, l'amo: *«Ja tenim bluetooth HID device, fem el Bluetooth HID Host»*): the same radio, the
  // board as the host and somebody else's keyboard. El rol central de NimBLE ja és al `sdkconfig`, o sigui que el que hi ha és GATT a
  // mà: explorar, connectar, demanar el boot protocol i subscriure's al report de vuit bytes (docs/50).
  bool can_host() const override;
  // **Si aquesta placa ha aparellat mai res** (0.190.203): una marca a la NVS, que no vol la pila a dalt com la botiga del ràdio.
  bool reports_enabled() const override;
  bool host_remembers() const override;
  bool host_on() override;
  void host_off() override;
  bool host_is_up() const override;
  uint8_t host_scan(uint32_t ms, Peer* out, uint8_t cap) override;
  bool host_pair(uint8_t index) override;
  bool host_reconnect() override;
  bool host_connected() const override;
  void host_forget() override;
  void host_sink(HostSink sink) override;
  uint8_t keyboard_leds() const override;
  bool report_enabled(hal::IBluetooth::Report which) const override;
  void device_forget() override;
  bool mouse_move(int8_t dx, int8_t dy) override;
  bool mouse_press(uint8_t buttons) override;
  bool mouse_click(uint8_t buttons) override;
  uint8_t mouse_buttons() const override;
};

// **La instància d'aquesta banda**, que cada placa torna des del seu `platform` (0.190.179): a board whose build has no radio gets the
// object that answers that it cannot, o sigui que el nucli no ha de recordar cap punter nul (docs/50).
IBluetooth* esp32_bluetooth();

// **La ràdio per a qui no és el teclat** (0.190.212): the stack of NimBLE is one and the same for the HID and for the mesh, o sigui que el
// mesh ha de poder encendre'l sense oferir-se com a teclat (`bring_up_the_radio` i prou). False when the radio refused to come up.
bool esp32_radio_for(const char* why);
// **I el segon pas, que fa qui ha posat els seus serveis entremig** (0.190.217): `nimble_port_freertos_init` és el que fa córrer la pila, o
// sigui que el mesh l'ha de cridar **després** del seu `esp_ble_mesh_init` i no abans (docs/50).
void esp32_radio_start(const char* why);
bool esp32_radio_is_up();

}  // namespace hal

#endif  // LUNOK_HAL_ESP32_BLUETOOTH_H
