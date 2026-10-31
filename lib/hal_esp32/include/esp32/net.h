// The radio of the ESP32.
//
// The same driver, the same provisioning manager, the same NTP and the same download
// on every board of this chip: nothing in here knows which board it is on, and that
// is why it is not in a board library (docs/50).
#ifndef LUNOK_ESP32_NET_H
#define LUNOK_ESP32_NET_H

#include "hal/hal.h"

namespace hal {

// The one radio of the board: association, scans, the provisioning access point or
// Bluetooth, NTP and a page. The board library returns it from `net()`, and a board
// that has no antenna wired does not (docs/50).
INet* esp32_net();

}  // namespace hal

#endif  // LUNOK_ESP32_NET_H
