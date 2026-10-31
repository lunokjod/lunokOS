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
DATA_DIR = os.path.join(env.subst("$PROJECT_DIR"), "data")


def too_long():
    found = []
    if not os.path.isdir(DATA_DIR):
        return found
    for root, dirs, files in os.walk(DATA_DIR):
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
        print("  %3d chars  data/%s" % (length, rel))
    print("check_fs_names: shorten them, or nest the path in directories of %d characters." % NAME_MAX)
    env.Exit(1)
