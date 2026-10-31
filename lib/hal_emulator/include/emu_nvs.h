#ifndef LUNOK_EMU_NVS_H
#define LUNOK_EMU_NVS_H

#include "hal/hal.h"

namespace hal {

// The emulator's key-value store: one file per key, under
// <state_dir>/nvs/<namespace>/<key>. Same interface as the board's NVS, and the
// value is opaque bytes in both (docs/70).
IKeyValue* emu_nvs();

}  // namespace hal

#endif  // LUNOK_EMU_NVS_H
