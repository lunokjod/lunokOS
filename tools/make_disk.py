# **One disk per build**, assembled here: the system's own content from `data/`
# and the apps this environment carries, whose folders are **links to `apps/`**
# at the root of the project.
#
# **Why per build.** `PROJECT_DATA_DIR` is one value for the whole project, and a
# single `build/disk` meant that building the ATOM after the Paper changed the
# disk the Paper was built from —and the one the emulator reads— behind its back. Now
# every environment has `build/disk/<env>` and this script points
# `PROJECT_DATA_DIR` at it, so two builds cannot write over each other and a build
# that carries no apps leaves the others alone (docs/80).
#
# **Why links and not copies.** `mklittlefs` **follows a symbolic link to a
# directory** —measured on 3/11 with 1.203.210628: the image comes out with
# `/apps/hello/hello.lua` inside— and so does the emulator, which uses `stat()` and
# `S_ISDIR()` (`emu_storage.cpp`). One source, and no copy to go stale. **The tests
# are the exception and say so**: a test creates and deletes apps in the disk it
# mounts, and writing through a link would write into the repository, so the `test`
# environment asks for copies (`custom_apps_copied = yes`).
#
# **What a build carries**, in order of precedence:
#   - `custom_apps = id1,id2`     those of the list, and **only the ones that name
#                                 this board**: the filter below is the same for
#                                 both options, because carrying an app the kernel
#                                 would refuse at `run` is an image that lies;
#   - `custom_apps_in_disk = yes` **the apps that name this board**: the ones whose
#                                 `manifest.json` lists the board's id or its chip
#                                 family ("esp32"), which is the same rule the
#                                 kernel uses to refuse an app at `run` (docs/40).
#                                 The emulator and the tests have no board, so they
#                                 carry every app of `apps/`;
#   - neither                     none, and the disk is the system alone, which is
#                                 what every board does since 0.118.3 (docs/70).
# A name that is asked for and does not end up in the disk says why, once a line:
# an option obeyed in silence is an image short of an app nobody notices.
import glob
import json
import os
import re
import shutil
import subprocess

try:
    # **What this run is building**: a short disk is a problem of the image and not of the firmware, and a
    # plain `pio run -e m5-atom` must still compile (docs/80).
    from SCons.Script import COMMAND_LINE_TARGETS

    TARGETS = set(COMMAND_LINE_TARGETS)
except ImportError:
    TARGETS = set()  # outside SCons (a probe of the script), so nothing is being built

Import("env")

PROJECT = env.subst("$PROJECT_DIR")
SYSTEM_DIR = os.path.join(PROJECT, "data")
APPS_DIR = os.path.join(PROJECT, "apps")
ENV_NAME = env.subst("$PIOENV")
DISK_DIR = os.path.join(PROJECT, "build", "disk", ENV_NAME)
COPY_APPS = env.GetProjectOption("custom_apps_copied", "no").lower() in ("yes", "true", "on", "1")


def option(name, fallback=""):
    """**An option of the environment as text**: `build_flags` comes as a list and the rest as a string, and a
    filter that reads only one of the two silently takes everything (found by building, 4/11)."""
    value = env.GetProjectOption(name, fallback)
    if isinstance(value, (list, tuple)):
        return " ".join(str(item) for item in value)
    return value.strip() if isinstance(value, str) else fallback


def board_identity():
    """The id and the family this environment builds for, read from its own header."""
    # **The board is the one its `-Ilib/board_<id>/include` selects**, and not the `board =` option: the ATOM
    # Matrix shares the PlatformIO board of the Lite (`board = m5stack-atom`) and is another board by its
    # header, which is the whole point of that design (docs/50). The emulator and the tests have no such flag, so
    # they have no board and carry every app. The header is read here and not asked to the platform because a
    # pre-script runs before it is loaded —in PlatformIO's `builder/main.py` the extra scripts come at line
    # 167 and the platform's own script at 173— and `BoardConfig` has nothing in it yet.
    flags = option("build_flags")
    found = re.search(r"-I\s*(\S*lib/board_[^/\s]+/include)", flags)
    if not found:
        return "", ""
    header = os.path.join(PROJECT, found.group(1), "board", "identity.h")
    if not os.path.isfile(header):
        print("make_disk: %s is not there, so this build takes no app" % found.group(1))
        return "", None
    with open(header) as handle:
        text = handle.read()
    found_id = re.search(r'id\(\)\s*\{\s*return\s*"([^"]+)"', text)
    found_family = re.search(r'family\(\)\s*\{\s*return\s*"([^"]+)"', text)
    if not found_id or not found_family:
        print("make_disk: %s has no id or no family this script can read, so it takes no app" % header)
        return "", None
    return found_id.group(1), found_family.group(1)


def apps_of_board(boards, board_id, family):
    return board_id in boards or (family is not None and family != "" and family in boards)


def wanted_apps():
    listed = option("custom_apps")
    # **A name that was asked for and is not carried says why**: `custom_apps` obeyed in silence is an image short of
    # an app that nobody notices until the board says `no such app` (0.118.3, docs/70).
    asked = bool(listed)
    if listed:
        ids = [item.strip() for item in listed.split(",") if item.strip()]
    elif option("custom_apps_in_disk", "no").lower() in ("yes", "true", "on", "1"):
        ids = sorted(os.listdir(APPS_DIR))
    else:
        return []
    board_id, family = board_identity()
    keep = []
    for app in ids:
        folder = os.path.join(APPS_DIR, app)
        if not os.path.isdir(folder):
            if asked:
                print("make_disk: there is no app called %s, so it stays out" % app)
            continue
        if not board_id:
            keep.append(app)  # no board: the emulator and the tests carry them all
            continue
        try:
            with open(os.path.join(folder, "manifest.json")) as handle:
                boards = json.load(handle).get("boards", [])
        except (OSError, ValueError):
            print("make_disk: %s has no manifest this script can read, so it stays out" % app)
            continue
        if apps_of_board(boards, board_id, family):
            keep.append(app)
        elif asked:
            print("make_disk: %s names %s and not this board (%s), so it stays out"
                  % (app, ", ".join(boards) if boards else "no board", board_id))
    return keep


def rebuild():
    if os.path.abspath(DISK_DIR) == os.path.abspath(SYSTEM_DIR):
        print("make_disk: data_dir is data/ itself, nothing to assemble")
        return
    os.makedirs(DISK_DIR, exist_ok=True)
    # **The system's own content is replaced, the state is not.** What comes from
    # `data/` is code and a build must not run on an old copy of it; `data/` inside
    # the disk (`/littlefs/data/<app>/`) is what an app wrote while the emulator ran,
    # and wiping it on every `pio run` would throw away the run of the person who is
    # testing (docs/85).
    for name in sorted(os.listdir(SYSTEM_DIR)):
        if name in ("apps", "data"):
            continue  # `apps` would be a copy that goes stale; `data` is the state
        source = os.path.join(SYSTEM_DIR, name)
        target = os.path.join(DISK_DIR, name)
        if os.path.islink(target) or os.path.isfile(target):
            os.remove(target)  # a link first: `isdir` would follow it and delete what it points at
        elif os.path.isdir(target):
            shutil.rmtree(target)
        if os.path.isdir(source):
            shutil.copytree(source, target)
        else:
            shutil.copy2(source, target)
    os.makedirs(os.path.join(DISK_DIR, "data"), exist_ok=True)
    # **The folder of apps is made again every time**: an app removed from `apps/`, or one a build stops
    # carrying, has to leave the disk, or the two drift apart in silence.
    apps_dir = os.path.join(DISK_DIR, "apps")
    if os.path.islink(apps_dir) or os.path.isfile(apps_dir):
        os.remove(apps_dir)
    elif os.path.isdir(apps_dir):
        shutil.rmtree(apps_dir)
    # **A disk that carries no app has no `apps/` folder at all** (0.88.2): the folder is the door `/apps`, and a
    # door that opens on nothing is what `apps` and `ls /` were showing on the emulator. The log says `cannot list`
    # the day something asks, which is the truth of that disk (docs/70, docs/85).
    apps = wanted_apps()
    if apps:
        os.makedirs(apps_dir)
    for app in apps:
        source = os.path.join(APPS_DIR, app)
        target = os.path.join(apps_dir, app)
        if COPY_APPS:
            shutil.copytree(source, target)
        else:
            # Absolute on purpose: the disk is generated and never moved, and a relative target would point
            # nowhere if the disk ever gained a level —and a link that points nowhere leaves the app out of
            # the image without a word.
            os.symlink(source, target)
    # **The builders read `$PROJECT_DATA_DIR`, and this is what points it at this build's disk**: the project
    # has one value for every environment and the image of an environment is its own (docs/80).
    env.Replace(PROJECT_DATA_DIR=DISK_DIR)
    # The price of the disk, measured and not guessed: a person who is about to flash it wants to know how
    # many bytes, how many files and what it carries.
    files = 0
    payload = 0
    for root, dirs, names in os.walk(DISK_DIR, followlinks=True):
        for name in names:
            files += 1
            payload += os.path.getsize(os.path.join(root, name))
    if not apps:
        # **A disk of the system alone is the normal one since 0.118.3**: no board carries apps in its image, and the
        # line says which of the two empty disks it is —a board that carries none by rule, or the emulator, which has no
        # board and receives its apps with `repo install` or a card (docs/70, docs/85).
        how = "no apps: the system alone"
    elif COPY_APPS:
        how = "%d apps, copied" % len(apps)
    else:
        how = "%d apps, linked to apps/" % len(apps)
    print("make_disk: %s is %d B in %d files, %s" % (os.path.relpath(DISK_DIR, PROJECT), payload, files, how))
    check_it_fits(files, apps)
    # **The card is not this script's** (11/11, the owner: *«treu les apps del SD per ara»*): the apps of the
    # emulator are the disk's, and what the slot of a board carries is the person's —the emulator's card is a
    # directory of this machine and it is left as it is, with whatever `repo install` or a hand put there (docs/85).


def partition_bytes():
    """The size of the LittleFS partition of this environment, from its partition table."""
    table = env.GetProjectOption("board_build.partitions", "")
    path = os.path.join(PROJECT, table) if table else ""
    if not path or not os.path.isfile(path):
        return 0
    with open(path) as handle:
        for line in handle:
            line = line.split("#")[0].strip()
            if not line or line.startswith("#"):
                continue
            fields = [item.strip() for item in line.split(",")]
            if len(fields) < 5:
                continue
            name, kind, subtype, _offset, size = fields[0], fields[1], fields[2], fields[3], fields[4]
            if kind == "data" and (name == "littlefs" or subtype == "spiffs"):
                try:
                    return int(size, 16)
                except ValueError:
                    return 0
    return 0


def littlefs_tool():
    """The image builder PlatformIO installed, which is the only thing that knows whether a disk fits."""
    try:
        package = env.PioPlatform().get_package_dir("tool-mklittlefs")
    except Exception:  # the platform has no such package, or it is not installed yet
        return ""
    # **`get_package_dir` answers `None` when the package is not installed**, and it does not raise:
    # `os.path.join(None, "mklittlefs")` was a `TypeError` that stopped the build of every board on a
    # machine without the tool, after the line that says the size is not checked (found the 4/11 building
    # `atom` on a fresh machine, 4/11). The disk is still assembled; only the check is skipped.
    if not package:
        return ""
    for name in ("mklittlefs", "mklittlefs.exe"):
        path = os.path.join(package, name)
        if os.path.isfile(path):
            return path
    return ""


def check_it_fits(files, apps):
    """**A disk that does not fit is a disk the image builder empties in silence.** mklittlefs prints
    `No more free space` and `error adding file!`, leaves the file out and PlatformIO still says SUCCESS, so
    the board comes up without the app and nobody looks for the reason in the build (docs/70).

    **Measured and not guessed**: the script runs the same tool with the same numbers the platform uses —the
    size of the partition from the table, a page of `0x100` and a block of `0x1000`— into a throw-away image
    in `build/`, and reads what it says. A model of the cost of a file in LittleFS would be a guess: the
    blocks a file takes are its data rounded up plus the metadata of its commits, and that is the tool's
    arithmetic and nobody else's."""
    partition = partition_bytes()
    if partition <= 0:
        return
    tool = littlefs_tool()
    if not tool:
        print("make_disk: the image tool is not installed, so the size of this disk is not checked")
        return
    probe = os.path.join(PROJECT, "build", "disk_probe.bin")
    if os.path.exists(probe):
        os.remove(probe)
    try:
        result = subprocess.run([tool, "-c", DISK_DIR, "-s", str(partition), "-p", "256", "-b", "4096", probe],
                                capture_output=True, text=True)
        output = result.stdout + result.stderr
    except OSError as why:
        print("make_disk: the image tool could not be run (%s), so the size is not checked" % why)
        return
    finally:
        if os.path.exists(probe):
            os.remove(probe)
    dropped = output.count("error adding file!")
    if dropped == 0:
        print("make_disk: the tool takes every one of the %d files in %d B of partition, so it fits"
              % (files, partition))
        return
    print("make_disk: **this disk does not fit**: the tool leaves %d of the %d files out of %d B, so the "
          "image would be short and PlatformIO would say SUCCESS." % (dropped, files, partition))
    for line in output.splitlines():
        if "unable to open" in line or "No more free space" in line:
            print("make_disk:   %s" % line)
            break
    if "buildfs" in " ".join(TARGETS) or "uploadfs" in " ".join(TARGETS):
        print("make_disk: an image is being built, so this stops here: carry fewer apps with "
              "`custom_apps = id1,id2` in %s, or give the board a bigger littlefs partition (docs/70)."
              % ENV_NAME)
        env.Exit(1)
    print("make_disk: a firmware build does not need the disk, so this one goes on")


rebuild()
