// The ESP32's key-value store, over the `nvs` partition.
//
// A namespace is an NVS namespace and a key is a key; the value is opaque bytes on
// both sides, like the emulator's one-file-per-key store. Nothing here knows what is
// being saved (docs/70), and nothing here knows which board it is: the ATOM and the
// Paper keep their data in the same partition of the same chip (docs/50).
#ifndef LUNOK_ESP32_NVS_H
#define LUNOK_ESP32_NVS_H

#include "hal/hal.h"

namespace hal {

// The board's key-value store: the `nvs` partition, through the IDF's own API.
// Namespaces and keys are plain names, and the value is opaque bytes.
IKeyValue* esp32_nvs();

}  // namespace hal

#endif  // LUNOK_ESP32_NVS_H
