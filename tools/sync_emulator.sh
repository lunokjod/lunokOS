#!/bin/sh
# Copies data/ onto a disk of the emulator's own.
#
# **The binary reads data/ by default** (`--flash data`), so this copy is only for
# a run that asks for a private disk: `--flash emulator/littlefs`. That directory
# is a copy, and editing an app in data/apps does not change it, which has already
# fooled us twice: the emulator kept running an old app and the bug looked like it
# was in the system (docs/85).
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
