#ifndef LUNOK_EMU_STORAGE_H
#define LUNOK_EMU_STORAGE_H

#include "hal/hal.h"

namespace hal {

// Returns the POSIX backend for "/littlefs", rooted at <fs_dir>/littlefs, or
// nullptr for any other mount.
IStorage* emu_storage(const char* mount);

}  // namespace hal

#endif  // LUNOK_EMU_STORAGE_H
