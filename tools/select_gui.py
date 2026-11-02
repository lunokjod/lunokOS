# Chooses the window backend of the emulator, without making SDL2 a hard dependency of a machine that
# only has X11. `LUNOK_GUI=sdl|x11|none` forces one; the default `auto` prefers SDL2 because it is the
# portable one and falls back to Xlib, then to a bench with no window (docs/85).
Import("env")

import os
import shlex
import shutil
import subprocess


def run(args):
    try:
        result = subprocess.run(args, check=False, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    except OSError:
        return None
    if result.returncode != 0:
        return None
    return result.stdout.strip()


def split(text):
    return shlex.split(text) if text else []


def sdl_flags():
    cflags = os.environ.get("SDL2_CFLAGS")
    libs = os.environ.get("SDL2_LIBS")
    if cflags is not None or libs is not None:
        return split(cflags or ""), split(libs or "")
    if shutil.which("sdl2-config"):
        return split(run(["sdl2-config", "--cflags"]) or ""), split(run(["sdl2-config", "--libs"]) or "")
    if shutil.which("pkg-config"):
        return split(run(["pkg-config", "--cflags", "sdl2"]) or ""), split(run(["pkg-config", "--libs", "sdl2"]) or "")
    return [], []


def x11_flags():
    if shutil.which("pkg-config"):
        cflags = split(run(["pkg-config", "--cflags", "x11"]) or "")
        libs = split(run(["pkg-config", "--libs", "x11"]) or "")
        if libs:
            return cflags, libs
    return [], ["-lX11"]


choice = os.environ.get("LUNOK_GUI", "auto").strip().lower()
if choice not in ("auto", "sdl", "x11", "none"):
    choice = "auto"

cflags, libs, backend = [], [], "none"
if choice in ("auto", "sdl"):
    cflags, libs = sdl_flags()
    if libs:
        cflags.append("-DLUNOK_GUI_SDL=1")
        backend = "SDL2"
if backend == "none" and choice in ("auto", "x11"):
    cflags, libs = x11_flags()
    if libs:
        cflags.append("-DLUNOK_GUI_X11=1")
        backend = "X11"
if choice == "none":
    cflags, libs = [], []

if cflags:
    env.Append(CCFLAGS=cflags)
for flag in libs:
    # `-l` and `-L` have to reach the end of the link line: a library before the objects that use it is
    # dropped by the linker. Everything else keeps its place in the link flags.
    if flag.startswith("-l"):
        env.Append(LIBS=[flag[2:]])
    elif flag.startswith("-L"):
        env.Append(LIBPATH=[flag[2:]])
    else:
        env.Append(LINKFLAGS=[flag])
print("gui: %s backend" % (backend if backend != "none" else "no window"))
