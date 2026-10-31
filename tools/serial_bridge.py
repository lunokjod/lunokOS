#!/usr/bin/env python3
"""Which USB-to-serial bridge is on the other side of a port, and which driver.

The M5 Paper does not always carry the same bridge: the v1.1 units come with a
CP2104 or a CH9102 depending on what the maker had that day (docs/50). Nothing on
the ESP32 can see the difference —the bridge is a USB device that talks to the
UART of the chip and to nothing else— but the PC can, and it is what decides
whether the port shows up as /dev/ttyUSB0 or /dev/ttyACM0 and whether the driver
is in the kernel.

    tools/serial_bridge.py                 # every serial port it can find
    tools/serial_bridge.py /dev/ttyUSB0    # one of them

Nothing is opened: it reads /sys, so it works while a monitor has the port and
does not disturb a board that is running.
"""

import os
import sys

# The makers whose bridges a board of this project may carry. The vendor id is
# the part that is worth trusting: the model is what the vendor's driver says.
VENDORS = {
    "10c4": "Silicon Labs (CP210x)",
    "1a86": "WCH (CH34x / CH910x)",
    "0403": "FTDI",
    "303a": "Espressif (USB of the chip itself)",
    "1a86:55d4": "WCH CH9102",
    "10c4:ea60": "Silicon Labs CP2104",
}


def usb_device_of(port):
    """The sysfs directory of the USB device behind a tty name, or None."""
    base = "/sys/class/tty/%s/device" % os.path.basename(port)
    if not os.path.exists(base):
        return None
    path = os.path.realpath(base)
    # The tty hangs off an interface, the interface off a configuration and the
    # configuration off the device: only the device has idVendor.
    while path != "/":
        if os.path.exists(os.path.join(path, "idVendor")):
            return path
        path = os.path.dirname(path)
    return None


def read(path):
    try:
        with open(path, "r") as handle:
            return handle.read().strip()
    except OSError:
        return ""


def driver_of(port):
    link = "/sys/class/tty/%s/device/driver" % os.path.basename(port)
    if not os.path.exists(link):
        return "?"
    return os.path.basename(os.path.realpath(link))


def describe(port):
    device = usb_device_of(port)
    if device is None:
        return "%s: not a USB serial port (no idVendor above it)" % port
    vid = read(os.path.join(device, "idVendor"))
    pid = read(os.path.join(device, "idProduct"))
    product = read(os.path.join(device, "product"))
    maker = read(os.path.join(device, "manufacturer"))
    name = VENDORS.get("%s:%s" % (vid, pid)) or VENDORS.get(vid) or "unknown vendor"
    line = "%s: %s (%s:%s) driver %s" % (port, name, vid, pid, driver_of(port))
    if product or maker:
        line += "\n    %s%s" % (maker, (" " + product) if product else "")
    if vid == "1a86" and driver_of(port) == "cdc_acm":
        # WCH's chips are CDC-ACM compliant, and the generic driver is what they
        # get by default: that is why the port is a ttyACM and not a ttyUSB.
        line += "\n    (CDC-ACM: the generic driver; WCH's own one makes it a ttyCH343USB)"
    return line


def main():
    ports = sys.argv[1:]
    if not ports:
        for name in sorted(os.listdir("/sys/class/tty")):
            if name.startswith("ttyUSB") or name.startswith("ttyACM") or \
                    name.startswith("ttyCH343"):
                ports.append("/dev/" + name)
    if not ports:
        print("no USB serial port is plugged in")
        return 1
    for port in ports:
        print(describe(port))
    return 0


if __name__ == "__main__":
    sys.exit(main())
