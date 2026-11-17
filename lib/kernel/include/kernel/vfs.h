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
// **The prefix inside the backend** ("" or "apps"): `/littlefs` is a whole volume and `/apps` is a folder of it, and the
// only call that cares is the one that empties a volume —`fs.format`, which refuses a folder— (0.171.1, docs/70).
const char* mount_sub(uint32_t index);
const char* mount_kind(uint32_t index);
bool mount_writable(uint32_t index);

// Longest-prefix resolution. rel_out receives the path inside the backend, which
// never sees the mount prefix. Refuses relative paths, "..", and paths of 128
// bytes or more.
bool resolve(const char* path, hal::IStorage*& storage, char* rel_out, size_t rel_cap);

// **Which unit is behind a storage**, for the meters: flash, card, the key-value store, the RTC memory
// or the FRAM (docs/30, docs/70).
uint8_t unit_of(const hal::IStorage* storage);

// **The whole path of a path of the door, on the disk that answered** (0.89.0): `/apps/squares` is
// `/littlefs/apps/squares` and `/apps/*squares` is `/sd/apps/squares` (0.164.0). A door is not a place, and a message
// that has to say where something was says the row of the table that is a disk and not the alias (docs/70).
bool on_disk(const char* path, char* out, size_t cap);

bool stat(const char* path, hal::Stat& out);
bool list(const char* path, hal::ListSink& sink);

// **Una carpeta i tot el que hi ha a dins, esborrat** (0.189.157): the shell's `apps remove`, the one an app's uninstall walks
// and a folder that moves all throw away a tree, i la recursió és la mateixa per als tres. **Què s'hi ha trobat es compta** en
// els tres números —fitxers, carpetes i bytes— perquè una esborrada que diu què ha deixat anar és una esborrada que una
// persona pot comprovar, i un punter nul és un àmbit que no ho vol saber (docs/70).
struct Dropped {
  uint32_t files = 0;
  uint32_t folders = 0;
  uint32_t bytes = 0;
};
// **The folder goes with everything inside it**, and what is left after is the answer: false when the path is not there or
// when the disk would not take it away —a read-only mount, or one that was pulled out says the same as a success— (docs/70).
bool drop_tree(const char* path, Dropped& gone);

// **Què ocupa una carpeta, sense tocar-la** (0.189.160): the same walk as `drop_tree` with the removal taken out —the files,
// the folders and the bytes of everything inside—, que és el que una pàgina que diu quant disc gasta una app necessita. A path
// that is not there answers zeros and true: a ground nobody has written is empty and not an error (docs/40, docs/70).
bool measure(const char* path, Dropped& size);

// True when the mount the path lives on was mounted writable (docs/70).
bool is_writable(const char* path);

// Room on the mount the path lives on, in bytes. False when the backend does not
// know how to say it.
bool space(const char* path, uint64_t& total, uint64_t& free_bytes);

// Reads at most cap bytes. out_len says how many were read; a file larger than cap
// comes back truncated, and that is the caller's business.
bool read_file(const char* path, char* buf, size_t cap, size_t& out_len);

// **What an `/apps` listing presents** (0.88.20): `/apps` is the one door of the apps —the flash's `apps/` and the
// card's `apps/` in one directory— and it is also where the system decides what there is. The filesystem cannot know
// what an app is (a folder that carries a manifest, a board it does not run on), so the kernel gives it a presenter and
// every name it refuses is not in the listing at all: what is not presented is never looked for, which is what a disk
// with a hundred folders of things that are not apps would cost otherwise (docs/40, docs/70). A null presenter presents
// everything, which is what a test with no kernel sees.
//
// A name the flash has and the card also has is presented **once as `name` and once as `*name`** (0.164.0), so the two
// can be read and run apart.
//
// The presenter is asked about a name **and the disk it came from** (`/littlefs/apps` or `/sd/apps`), because the same
// name on the two disks is two different folders and only one of them may be an app.
using AppsPresenter = bool (*)(const char* disk, const char* name);
void set_apps_presenter(AppsPresenter presenter);
// **The app whose own faces are part of `/fonts`** (0.174.1): the door joins the app's `apps/<id>/fonts`, the flash's
// `res/fonts` and the card's `fonts`, and this is how it knows which app is asking — the hook answers an empty name from
// the prompt (docs/30, docs/70).
void set_fonts_app(const char* (*id)());

}  // namespace vfs
}  // namespace kernel

#endif  // LUNOK_KERNEL_VFS_H
