// Who this firmware is for.
//
// Every board carries a header with exactly this name, with the same names inside
// and its own content, and platformio.ini puts the right one on the include path
// (-Ilib/board_<id>/include). Nothing in the project writes #ifdef for a board:
// whoever needs to know asks the HAL, and the HAL answers from here (docs/50).
// This header includes nothing else on purpose, so any translation unit of the
// board can use it.
//
// The M5 Paper v1.0 (K049) and v1.1 (K049-B) are the same firmware: the same
// ESP32-D0WDQ6-V3, the same 16 MB of flash, the same 8 MB of PSRAM and the same
// pin map. They differ in the panel —a rigid ED047TC1 on v1.0 and a flexible
// ET047TC1 on v1.1, both driven by the same IT8951E— and in which USB-to-serial
// bridge the board happens to carry (CP2104 or CH9102), which is a driver on the
// PC and not a line of firmware (docs/50).
#ifndef BOARD_IDENTITY_H
#define BOARD_IDENTITY_H

#include <stdint.h>

namespace board {

// The name this board has in platformio.ini, letter for letter (`board = ...`).
// A Lua app reads it as sys.board and picks its hardware path with it (docs/30).
inline const char* id() { return "m5stack-paper"; }

// The chip family, which is what an app means when its manifest says it runs on
// "esp32": every board of the family accepts that app, and a board that names a
// chip this one does not have is not an app of this board (docs/40).
inline const char* family() { return "esp32"; }

// This build is for a board and not for the PC bench.
inline bool emulator() { return false; }

// The Paper has no light of its own: what it has is a screen, and a screen is
// not an LED (docs/50).
inline uint32_t led_count() { return 0; }
inline uint32_t led_max() { return 0; }

// No inertial sensor on this board.
inline bool has_imu() { return false; }

}  // namespace board

#endif  // BOARD_IDENTITY_H
