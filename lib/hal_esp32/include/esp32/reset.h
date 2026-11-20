// **Per què s'ha reiniciat el xip** (0.190.68), que només ell sap i que cap placa pot endevinar
// amb una marca desada.
//
// Un ESP32 arrenca de maneres molt diferents i no totes volen dir el mateix: el corrent que
// arriba per primer cop, el despert d'un son profund, un reinici demanat pel sistema, un
// pànic, una caiguda de tensió. `esp_reset_reason()` ho respon i el firmware no hauria
// d'endevinar-ho: el que en surt és el que fa que una arrencada es pugui dir (docs/50,
// docs/60, docs/85).
#ifndef LUNOK_ESP32_RESET_H
#define LUNOK_ESP32_RESET_H

#include "hal/hal.h"

namespace hal {

// The chip's own answer, said in the words of the HAL. It is safe to ask at any point of the
// boot: the reason is read from a register the boot does not touch.
ResetCause esp32_reset_cause();

}  // namespace hal

#endif  // LUNOK_ESP32_RESET_H
