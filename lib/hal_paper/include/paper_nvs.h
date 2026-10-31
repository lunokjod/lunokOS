// The key-value store of the board, over the `nvs` partition (docs/70).
#ifndef LUNOK_PAPER_NVS_H
#define LUNOK_PAPER_NVS_H

#include "hal/hal.h"

namespace hal {

IKeyValue* paper_nvs();

}  // namespace hal

#endif  // LUNOK_PAPER_NVS_H
