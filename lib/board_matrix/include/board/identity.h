// Who this firmware is for: the M5 ATOM Matrix.
//
// Every board carries a header with exactly this name, with the same names inside
// and its own content, and platformio.ini puts the right one on the include path
// (-Ilib/board_<id>/include). Nothing in the project writes #ifdef for a board:
// whoever needs to know asks the HAL, and the HAL answers from here (docs/50).
// This header includes nothing else on purpose, so any translation unit of the
// board can use it.
#ifndef BOARD_IDENTITY_H
#define BOARD_IDENTITY_H

#include <stdint.h>

namespace board {

// The name of this board, and **not** the `board =` line of platformio.ini: the
// Matrix is the Lite's twin for PlatformIO (same ESP32-PICO-D4, same 4 MB of
// flash, same partition table, same button), so both environments write
// `board = m5stack-atom` and that name cannot tell them apart. An app that has to
// pick a hardware path reads this as sys.board, and this is the only place that
// knows which of the two it is (docs/30, docs/50).
inline const char* id() { return "m5stack-atom-matrix"; }

// The chip family: the Matrix is an ESP32 like the Lite, so an app that says it
// runs on "esp32" runs here too (docs/40).
inline const char* family() { return "esp32"; }

// False: there is real hardware under this build.
inline bool emulator() { return false; }

// The panel: twenty-five WS2812 chips (5 by 5) on GPIO27 instead of the Lite's
// one. The driver's frame is this number of lights and no bigger (docs/50,
// docs/75).
inline constexpr uint32_t led_count() { return 25; }

// The ceiling of one channel, and it is the maker's number: M5Stack says that with
// this panel a brightness above **20** heats the LEDs and the acrylic and can
// damage them (the FastLED recommendation in the Atom-Matrix sheet). Twenty-five
// 2020 LEDs in a closed case are not one LED in the open, so nothing above this
// reaches them (docs/30, docs/50).
inline constexpr uint32_t led_max() { return 20; }

// True: the Matrix carries a MPU6886 —three axes of acceleration and three of turn
// rate— on its own I2C bus, SDA G25 and SCL G21 at 100 kHz, address 0x68. Those two
// pins are not free on this board, which is the other difference from the Lite
// (docs/50).
inline constexpr bool has_imu() { return true; }

}  // namespace board

#endif  // BOARD_IDENTITY_H
