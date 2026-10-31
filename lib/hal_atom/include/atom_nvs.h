#ifndef LUNOK_ATOM_NVS_H
#define LUNOK_ATOM_NVS_H

#include "hal/hal.h"

namespace hal {

// The board's key-value store: the `nvs` partition of partitions.csv, through
// Arduino's Preferences. Namespaces and keys are plain names, and the value is
// opaque bytes, exactly like the emulator's files (docs/70).
IKeyValue* atom_nvs();

}  // namespace hal

#endif  // LUNOK_ATOM_NVS_H
