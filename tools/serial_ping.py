#!/usr/bin/env python3
"""Round-trip of the serial port: what a line costs from the PC to an app.

This is the measurement docs/90 asks M1 for: from the byte leaving the PC to the
answer coming back, with the board at 240 MHz and a 115200 baud port. It is not
the time an app takes to do something: here the port, the reader task, the kernel
turn, the promise and the write back all count.

The board has to be at the prompt with the `ping` app on its disk (it is in
apps/ping). The tool runs it, waits for its "ping", sends one line at a time
and times each answer. Three ESC at the end close the app and leave the prompt.

    tools/serial_ping.py --port /dev/ttyUSB0 --rounds 10
"""

import argparse
import os
import select
import statistics
import sys
import termios
import time
import tty

ESC = b"\x1b"

BAUDRATES = {
    9600: termios.B9600,
    19200: termios.B19200,
    38400: termios.B38400,
    57600: termios.B57600,
    115200: termios.B115200,
}


def open_port(port, baudrate):
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY)
    tty.setraw(fd)
    speed = BAUDRATES[baudrate]
    attrs = termios.tcgetattr(fd)
    attrs[4] = speed  # input speed
    attrs[5] = speed  # output speed
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd


def read_some(fd, timeout):
    ready, _, _ = select.select([fd], [], [], timeout)
    if not ready:
        return b""
    try:
        return os.read(fd, 256)
    except OSError:
        return b""


def wait_for(fd, marker, deadline):
    """Reads until the marker appears, or the deadline passes. None on timeout."""
    seen = b""
    while time.monotonic() < deadline:
        chunk = read_some(fd, 0.05)
        if not chunk:
            continue
        seen += chunk
        if marker in seen:
            return seen
    return None


def main():
    parser = argparse.ArgumentParser(description="Serial round-trip measurement.")
    parser.add_argument("--port", default="/dev/ttyUSB0", help="serial port")
    parser.add_argument("--baudrate", type=int, default=115200, choices=sorted(BAUDRATES),
                        help="baud rate")
    parser.add_argument("--rounds", type=int, default=10, help="how many lines to time")
    parser.add_argument("--warmup", type=int, default=1,
                        help="lines sent and thrown away first (a port just opened is not warm)")
    parser.add_argument("--all", action="store_true", help="print every sample, not only the summary")
    parser.add_argument("--app", default="ping", help="app to run (apps/ping)")
    args = parser.parse_args()

    fd = open_port(args.port, args.baudrate)
    try:
        os.write(fd, ("run " + args.app + "\r").encode())
        if wait_for(fd, b"ping", time.monotonic() + 10.0) is None:
            print("no 'ping' from the board: is the app on its disk?", file=sys.stderr)
            return 2

        for _ in range(args.warmup):
            started = time.monotonic()
            os.write(fd, b"x\r")
            if wait_for(fd, b"pong", started + 5.0) is None:
                print("no 'pong' for the warm-up line", file=sys.stderr)
                return 2

        trips = []
        for _ in range(args.rounds):
            started = time.monotonic()
            os.write(fd, b"x\r")
            if wait_for(fd, b"pong", started + 5.0) is None:
                print("no 'pong' for one line", file=sys.stderr)
                return 2
            trips.append((time.monotonic() - started) * 1000.0)

        os.write(fd, ESC * 3)
        print("round trip, %d lines on %s at %d baud" % (args.rounds, args.port, args.baudrate))
        if args.all:
            print("  samples: " + " ".join("%.1f" % t for t in trips))
        print("  min    %.1f ms" % min(trips))
        print("  median %.1f ms" % statistics.median(trips))
        print("  max    %.1f ms" % max(trips))
        return 0
    finally:
        os.close(fd)


if __name__ == "__main__":
    sys.exit(main())
