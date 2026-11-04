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
# bench itself wrote stays where it is: only `apps/` and `res/` are replaced.
#
# **The apps come from the card and the rest from the assembled disk** (0.88.2): the
# bench carries its apps on the card (`emulator/sd/apps`, a copy that `make_disk` keeps
# up to date) and its flash is the system alone, so a private flash that is meant to
# look like a board's has to take the apps from there.
set -e

here=$(cd "$(dirname "$0")/.." && pwd)
target="$here/emulator/littlefs"
assembled="$here/build/disk/emulator"
card_apps="$here/emulator/sd/apps"

if [ ! -d "$assembled" ]; then
  echo "there is no build/disk/emulator: run 'pio run -e emulator' first, it is what assembles it" >&2
  exit 1
fi
if [ ! -d "$card_apps" ]; then
  echo "there are no apps on the card: run 'pio run -e emulator' first, it is what copies them" >&2
  exit 1
fi

mkdir -p "$target"
rm -rf "$target/apps" "$target/res"
cp -r "$card_apps" "$target/apps"
[ -d "$assembled/res" ] && cp -rL "$assembled/res" "$target/"

echo "emulator disk updated for --flash: $(ls "$target/apps" | tr '\n' ' ')"
