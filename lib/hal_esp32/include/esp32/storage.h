// LittleFS on the ESP32's data partition.
//
// The partition is this chip's flash and it is found by its label, which is why the
// label is asked for and never guessed: the core's default is not this project's
// table. Handles come from a fixed pool, so opening a file never allocates
// (docs/70, docs/75).
//
// It is here, and not in a board library, because nothing in it knows which board
// it is running on: the ATOM and the Paper are the same chip and the same
// filesystem (docs/50).
#ifndef LUNOK_ESP32_STORAGE_H
#define LUNOK_ESP32_STORAGE_H

#include "hal/hal.h"

namespace hal {

// The LittleFS backend of `mount` (only "/littlefs" is served), or nullptr when the
// partition does not mount. `partition` is the label in the board's partition table.
// Mounting happens on the first call.
IStorage* esp32_storage(const char* mount, const char* partition);

}  // namespace hal

#endif  // LUNOK_ESP32_STORAGE_H
