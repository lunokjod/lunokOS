#ifndef LUNOK_ATOM_STORAGE_H
#define LUNOK_ATOM_STORAGE_H

#include "hal/hal.h"

namespace hal {

// Returns the LittleFS backend for "/littlefs", or nullptr if the partition does
// not mount. Mounting happens on the first call.
IStorage* atom_storage(const char* mount);

}  // namespace hal

#endif  // LUNOK_ATOM_STORAGE_H
