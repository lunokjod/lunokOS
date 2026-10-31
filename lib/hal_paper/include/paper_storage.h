// LittleFS on the Paper's data partition, found by its label (docs/70).
#ifndef LUNOK_PAPER_STORAGE_H
#define LUNOK_PAPER_STORAGE_H

#include "hal/hal.h"

namespace hal {

// "/littlefs" on this board, or nullptr when the partition does not mount.
IStorage* paper_storage(const char* mount);

}  // namespace hal

#endif  // LUNOK_PAPER_STORAGE_H
