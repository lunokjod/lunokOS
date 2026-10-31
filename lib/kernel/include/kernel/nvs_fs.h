// /nvs as files (docs/70): a namespace is a directory and a key is a file whose
// contents are the bytes the key-value store keeps.
#ifndef LUNOK_KERNEL_NVS_FS_H
#define LUNOK_KERNEL_NVS_FS_H

#include "hal/hal.h"

namespace kernel {

// The backend of the /nvs mount. `store` is the platform's key-value store.
// Reading only: writing, creating and erasing arrive with the rest of M4, and
// until then the mount is not writable.
hal::IStorage* nvs_storage(hal::IKeyValue* store);

}  // namespace kernel

#endif  // LUNOK_KERNEL_NVS_FS_H
