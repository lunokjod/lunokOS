#!/usr/bin/env python3
"""Ask the compiler how much stack each function of a library uses, and fail if one goes over.

The board answers `***ERROR*** A stack overflow in task loopTask has been detected` when a task walks past the
end of its stack, and that is what a page of pictures did on 0.118.0: the decoder of a GIF kept its table of
12 288 bytes, its string of 4 096 and a row of 2 048 in locals, against the 7 320 bytes the app task prints
free. GCC can answer the question before the board has to: `-fstack-usage` writes a `.su` file next to every
object with the frames of every function, and this reads them.

    python3 tools/check_stack.py                        # lib/img, 1024 bytes a frame
    python3 tools/check_stack.py --all --budget 4096    # every library, the survey
    python3 tools/check_stack.py --lib lib/luabind --budget 2048

It is a measurement and not a test: nothing of the firmware runs.
"""
import argparse
import glob
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# **The frame of one function**, not the whole chain below it. The app task prints 7 320 bytes free on the
# Paper, so a kilobyte for the worst function leaves room for its caller and the drawing under it (docs/75).
BUDGET = 1024


def include_flags(lib):
    """The include paths of a library and of the ones under it, the same way PlatformIO builds them."""
    flags = ['-I' + os.path.join(ROOT, lib, 'include'), '-I' + os.path.join(ROOT, lib, 'src')]
    for other in sorted(glob.glob(os.path.join(ROOT, 'lib', '*', 'include'))):
        flags.append('-I' + other)
    return flags


def frames(lib, tmp):
    """Compile every source of a library with `-fstack-usage` and read the frames it wrote."""
    out = []
    for source in sorted(glob.glob(os.path.join(ROOT, lib, 'src', '*.cpp'))):
        obj = os.path.join(tmp, os.path.basename(source) + '.o')
        cmd = ['g++', '-std=gnu++17', '-O2', '-fstack-usage', '-c', source] + include_flags(lib) + ['-o', obj]
        done = subprocess.run(cmd, capture_output=True, text=True)
        if done.returncode != 0:
            # A library that needs the headers of the board (`Arduino.h`, `freertos/...`) is not measured here: the
            # host has no such headers and the question is about the board's frames anyway (docs/80).
            raise NeedsTheBoard(source)
        # The compiler names the file after the object without its extension (`bmp.cpp.o` answers `bmp.cpp.su`).
        table = os.path.splitext(obj)[0] + '.su'
        if not os.path.exists(table):
            table = obj + '.su'
        with open(table) as handle:
            for line in handle:
                parts = line.rstrip('\n').split('\t')
                if len(parts) != 3:
                    continue
                where, size, kind = parts
                out.append((int(size), kind, where))
    return out


class NeedsTheBoard(Exception):
    pass


def main():
    parser = argparse.ArgumentParser(description='Measure the stack frame of every function of a library.')
    parser.add_argument('--lib', action='append', help='a library under lib/ (default: img)')
    parser.add_argument('--all', action='store_true', help='every library that has src/')
    parser.add_argument('--budget', type=int, default=BUDGET, help='bytes a single frame may take')
    parser.add_argument('--top', type=int, default=8, help='how many frames to print')
    args = parser.parse_args()

    libs = args.lib
    if args.all:
        libs = sorted(os.path.relpath(path, ROOT) for path in glob.glob(os.path.join(ROOT, 'lib', '*'))
                      if os.path.isdir(os.path.join(path, 'src')))
    if not libs:
        libs = ['lib/img']

    worse = []
    with tempfile.TemporaryDirectory() as tmp:
        for lib in libs:
            try:
                got = frames(lib, tmp)
            except NeedsTheBoard as error:
                print('== %s: skipped, %s needs the headers of the board' % (lib, error))
                continue
            if not got:
                continue
            got.sort(reverse=True)
            print('== %s: %d functions, the worst %d' % (lib, len(got), min(args.top, len(got))))
            for size, kind, where in got[:args.top]:
                mark = 'OVER' if size > args.budget else '    '
                print('%s %7d B  %-12s %s' % (mark, size, kind, where.replace(ROOT + os.sep, '')))
            worse += [(size, where) for size, _kind, where in got if size > args.budget]

    if worse:
        print('\n%d function(s) over the budget of %d bytes: one of them is a stack overflow waiting for a '
              'page of pictures.' % (len(worse), args.budget))
        return 1
    print('\nEvery frame is under %d bytes.' % args.budget)
    return 0


if __name__ == '__main__':
    sys.exit(main())
