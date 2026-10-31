// The capabilities that belong to the ESP32 and not to a board.
#include "esp32/caps.h"

namespace hal {

Caps esp32_caps() {
  Caps caps;
  // The console is the UART of the chip, and the PAL is what writes to it.
  caps.add(kCapConsole);
  caps.add(kCapAnsi);
  // The epoch is the SoC's —the RTC domain of this chip, which is a timer and a piece
  // of memory and **not** a clock with a battery behind it— and it survives a deep
  // sleep; the RTC timer is what brings the board back (docs/60). A clock that survives
  // the cable being pulled is a chip of the board (the Paper's BM8563) and its
  // capability is the board's to add, not this one's.
  caps.add(kCapRtc);
  caps.add(kCapRtcWake);
  // The two depths of sleep are the chip's (docs/50).
  caps.add(kCapSleepLight);
  caps.add(kCapSleepDeep);
  // The `nvs` partition of this chip's table holds the small things that survive a
  // reboot (docs/70).
  caps.add(kCapNvs);
  // The radio is in the chip: whether an access point answers is another question and
  // it is `net.connect`'s to answer. Provisioning is the Espressif component, and a
  // page is esp_http_client with the certificate bundle of this build (docs/50).
  caps.add(kCapWifi);
  caps.add(kCapProvisioning);
  caps.add(kCapHttp);
  return caps;
}

}  // namespace hal
