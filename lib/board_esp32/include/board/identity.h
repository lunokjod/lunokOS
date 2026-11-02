// Who this firmware is for: **a plain ESP32 and nothing else**.
//
// Every board carries a header with exactly this name, with the same names inside and its own content, and
// platformio.ini puts the right one on the include path (-Ilib/board_<id>/include). Nothing in the project
// writes #ifdef for a board: whoever needs to know asks the HAL, and the HAL answers from here (docs/50).
//
// This one exists for the opposite reason to the Paper: not to show what a board with a glass, a battery and a
// touch panel can do, but to be **the smallest board the system has to fit in** —an ESP32 DevKit of 4 MB of
// flash, 520 KB of RAM, no PSRAM, no panel, no card, no sensor— so that a page of code that only works on a
// board with four megabytes of extra memory is caught here and not there (docs/80).
//
// What it does **not** have, said plainly so nobody wires it and waits:
//
//   - **no light**: the strip of the family HAL is on GPIO27 and this board has nothing on it, so `led.play`
//     will be a write to a pin with no chip behind it. `led_count()` is one because the frame of the driver is
//     a template on it and zero of them is not a frame;
//   - **no button it can press**: the family's key is GPIO39, which on a DevKit is a bare pad and not the
//     BOOT button (GPIO0). The dial will read "not pressed" for ever, so the way in and out is the console;
//   - **no glass, no touch, no battery gauge, no IMU**: the capabilities say so by themselves, because there is
//     no driver for any of them on this band.
#ifndef BOARD_IDENTITY_H
#define BOARD_IDENTITY_H

#include <stdint.h>

namespace board {

// The name this board has in platformio.ini, letter for letter (`board = esp32dev`).
inline const char* id() { return "esp32dev"; }

// The chip family, which is what an app means when its manifest says it runs on "esp32" (docs/40).
inline const char* family() { return "esp32"; }

// False: there is real hardware under this build.
inline bool emulator() { return false; }

// **One light because the driver's frame is a template on this number**: there is no addressable chip on a
// DevKit, and a frame of zero is not a frame. What it costs is the RMT memory of one pixel (docs/75).
inline constexpr uint32_t led_count() { return 1; }

// The whole range: there is no case to keep the light of this board below its maximum.
inline constexpr uint32_t led_max() { return 255; }

// False: a DevKit has no inertial sensor on its bus (docs/50).
inline constexpr bool has_imu() { return false; }

}  // namespace board

#endif  // BOARD_IDENTITY_H
