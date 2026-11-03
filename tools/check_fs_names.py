# Refuses to build the flash image when a name in data/ is too long for the tool
# that builds it.
#
# Measured on 2026-10-30 against tool-mklittlefs 1.203.210628, the one PlatformIO
# installs for the ESP32: a component of 32 characters goes into the image, and a
# component of 33 makes the tool print "unable to open ... error adding file!" and
# leave the file out. PlatformIO does not turn that into an error and still says
# SUCCESS, so without this check a file just never reaches the board (docs/70).
#
# The limit is per component (each directory and the file), not per path: a long
# path is built from nested directories of 32 characters or fewer.

import os

Import("env")

NAME_MAX = 32
# **The tree that actually goes into the image**: `build/disk`, made by
# `tools/make_disk.py` from `data/` and, on a board with room, `apps/` (docs/80).
# Checking `data/` alone would say nothing about the apps any more.
DATA_DIR = env.subst("$PROJECT_DATA_DIR")


def too_long():
    found = []
    if not os.path.isdir(DATA_DIR):
        return found
    # **`followlinks` because the apps are a link in the assembled disk**: they live
    # at `apps/` in the root of the project and `make_disk` links them in, which is
    # what keeps one source and no stale copy. Without this the names inside an app
    # would never be looked at, and a name the image builder drops is exactly the
    # silent loss this script exists for (docs/70).
    for root, dirs, files in os.walk(DATA_DIR, followlinks=True):
        for name in dirs + files:
            if len(name) > NAME_MAX:
                rel = os.path.relpath(os.path.join(root, name), DATA_DIR)
                found.append((len(name), rel.replace(os.sep, "/")))
    found.sort(key=lambda item: (-item[0], item[1]))
    return found


offenders = too_long()
if offenders:
    print("check_fs_names: the image builder drops names longer than %d characters," % NAME_MAX)
    print("check_fs_names: so these files would never reach the board:")
    for length, rel in offenders:
        print("  %3d chars  %s/%s" % (length, os.path.basename(DATA_DIR), rel))
    print("check_fs_names: shorten them, or nest the path in directories of %d characters." % NAME_MAX)
    env.Exit(1)
