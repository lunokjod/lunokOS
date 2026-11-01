#ifndef LUNOK_KERNEL_FS_OPS_H
#define LUNOK_KERNEL_FS_OPS_H

namespace kernel {

// Hands the VFS to the interpreter as the four operations fs.* needs. Called once
// at boot, before any app can run (docs/70).
void install_fs_ops();

// **A path the way the app that is running sees it**: absolute, with `/res/x` turned into the app's own
// `apps/<id>/res/x` and `/cfg/x` into `/cfg/<id>/x`, because those two are doors of an app and not disks
// (docs/70). False when the path is not one an app may name. It is what any kernel call that takes a path
// from an app has to use, and not only the `fs.*` ones (docs/30).
bool app_path(const char* in, char* out, size_t cap);

}  // namespace kernel

#endif  // LUNOK_KERNEL_FS_OPS_H
