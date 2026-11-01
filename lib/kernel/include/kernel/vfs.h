// Virtual filesystem: one root, a table of mounts, longest-prefix resolution.
// See docs/70-sistema-de-fitxers.md.
#ifndef LUNOK_KERNEL_VFS_H
#define LUNOK_KERNEL_VFS_H

#include <stddef.h>

#include "hal/hal.h"

namespace kernel {
namespace vfs {

// Builds the mount table from what the board offers.
// Returns the number of mounts. /littlefs is mandatory: if it is missing, the
// table is empty and the caller says so, but boot carries on.
uint32_t init(hal::IPlatform& platform);

// Points `/cfg` at the unit the settings live on (kernel/cfg). It is a row of this table
// like any other —the door, not the disk— and it moves when the person changes the unit
// (docs/70). A null storage takes the row away.
void set_cfg_storage(hal::IStorage* storage, const char* kind);

uint32_t mount_count();
const char* mount_path(uint32_t index);
const char* mount_kind(uint32_t index);
bool mount_writable(uint32_t index);

// Longest-prefix resolution. rel_out receives the path inside the backend, which
// never sees the mount prefix. Refuses relative paths, "..", and paths of 128
// bytes or more.
bool resolve(const char* path, hal::IStorage*& storage, char* rel_out, size_t rel_cap);

// **Which unit is behind a storage**, for the meters: flash, card, the key-value store, the RTC memory
// or the FRAM (docs/30, docs/70).
uint8_t unit_of(const hal::IStorage* storage);

bool stat(const char* path, hal::Stat& out);
bool list(const char* path, hal::ListSink& sink);

// True when the mount the path lives on was mounted writable (docs/70).
bool is_writable(const char* path);

// Room on the mount the path lives on, in bytes. False when the backend does not
// know how to say it.
bool space(const char* path, uint64_t& total, uint64_t& free_bytes);

// Reads at most cap bytes. out_len says how many were read; a file larger than cap
// comes back truncated, and that is the caller's business.
bool read_file(const char* path, char* buf, size_t cap, size_t& out_len);

}  // namespace vfs
}  // namespace kernel

#endif  // LUNOK_KERNEL_VFS_H
