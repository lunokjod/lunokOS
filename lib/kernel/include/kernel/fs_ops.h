#ifndef LUNOK_KERNEL_FS_OPS_H
#define LUNOK_KERNEL_FS_OPS_H

#include "luabind/luabind.h"

namespace kernel {

// Hands the VFS to the interpreter as the four operations fs.* needs. Called once
// at boot, before any app can run (docs/70).
void install_fs_ops();

// **A path the way the app that is running sees it**: absolute, with `/res/x` turned into the app's own
// `apps/<id>/res/x` and `/cfg/x` into `/cfg/<id>/x`, because those two are doors of an app and not disks
// (docs/70). False when the path is not one an app may name. It is what any kernel call that takes a path
// from an app has to use, and not only the `fs.*` ones (docs/30).
bool app_path(const char* in, char* out, size_t cap);

// **A path this caller may write to**: `/cfg` and `/res` turned into the app's own room, `..` refused, and
// the policy of docs/70 applied (`err` carries the code an app reads). `person` is the prompt —any drive
// mounted writable— and not an app, which has its ground and nothing else. A download is a write, and a
// page cannot land where the same caller could not write a file (docs/30, docs/70).
bool write_path(const char* in, char* out, size_t cap, bool person, luabind::FsError& err);

// The same policy for a path that is **already** the one the filesystem sees (`app_path` turns `/cfg` into
// the app's own room, and doing that twice would put the id in the path twice) (docs/70).
bool write_allowed(const char* path, bool person, luabind::FsError& err);

// Makes every directory on the way to `path`, as a write does, so an app can download into its own ground
// the first time without making three directories first (docs/70).
bool app_make_way(const char* path);

}  // namespace kernel

#endif  // LUNOK_KERNEL_FS_OPS_H
