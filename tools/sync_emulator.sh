#!/bin/sh
# Copies the assembled disk onto a disk of the emulator's own.
#
# **The binary reads its own build's disk by default** (`build/disk/emulator`, from
# `-DLUNOK_DISK_DIR`), so this copy is only for a run that asks for a private disk:
# `--flash emulator/littlefs`. That directory is a copy, and editing an app does not
# change it, which has already fooled us twice: the emulator kept running an old app
# and the bug looked like it was in the system (docs/85).
#
# The copy is a replacement, not a merge: an app removed from the repository is
# removed from the emulator too, or the two disks drift apart in silence. What the
# emulator itself wrote stays where it is: only `apps/` and `res/` are replaced.
#
# **`-L` because the assembled disk carries the apps as a link**: `make_disk` puts
# `apps/` there as a symbolic link, which `mklittlefs` and the emulator follow
# (measured 3/11, docs/70) but which would point nowhere from inside
# `emulator/littlefs`, so the copy materialises it.
set -e

here=$(cd "$(dirname "$0")/.." && pwd)
target="$here/emulator/littlefs"
assembled="$here/build/disk/emulator"

if [ ! -d "$assembled" ]; then
  echo "there is no build/disk/emulator: run 'pio run -e emulator' first, it is what assembles it" >&2
  exit 1
fi

mkdir -p "$target"
rm -rf "$target/apps" "$target/res"
[ -d "$assembled/apps" ] && cp -rL "$assembled/apps" "$target/"
[ -d "$assembled/res" ] && cp -rL "$assembled/res" "$target/"

if [ -d "$target/apps" ]; then
  echo "emulator disk updated from build/disk/emulator: $(ls "$target/apps" | tr '\n' ' ')"
else
  echo "emulator disk updated from build/disk/emulator: no apps"
fi
