#!/bin/sh
# Copies data/ onto the emulator's disk.
#
# The emulator does not read data/: it reads a directory of its own
# (emulator/littlefs by default), and that directory is a copy. Editing an app in
# data/apps changes the board image and nothing else, which has already fooled us
# twice: the emulator kept running an old app and the bug looked like it was in
# the system (docs/85).
#
# The copy is a replacement, not a merge: an app removed from data/ is removed
# from the emulator too, or the two disks drift apart in silence.
set -e

here=$(cd "$(dirname "$0")/.." && pwd)
target="$here/emulator/littlefs"

mkdir -p "$target"
rm -rf "$target/apps" "$target/res"
cp -r "$here/data/apps" "$target/"
[ -d "$here/data/res" ] && cp -r "$here/data/res" "$target/"

echo "emulator disk updated from data/: $(ls "$target/apps" | tr '\n' ' ')"
