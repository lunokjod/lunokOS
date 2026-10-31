#ifndef LUNOK_KERNEL_FS_OPS_H
#define LUNOK_KERNEL_FS_OPS_H

namespace kernel {

// Hands the VFS to the interpreter as the four operations fs.* needs. Called once
// at boot, before any app can run (docs/70).
void install_fs_ops();

}  // namespace kernel

#endif  // LUNOK_KERNEL_FS_OPS_H
