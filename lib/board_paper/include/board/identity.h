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

// **Whether this build carries a glass** (0.179.9): **yes**. This board has a panel, so the page —the canvases and the
// controls, the faces of the text, the reader of a book, the picture of the boot, the bar and, when the model has one, the
// finger— is built in. It is a macro because the question is asked **before the compiler** (`#if LUNOK_GLASS` decides which
// translation units exist), which is not something a function can do; the capability of a *running* board is still
// `kCapDisplay` (docs/50, docs/75).
#define LUNOK_GLASS 1

namespace board {

// The name this board has in platformio.ini, letter for letter (`board = ...`).
// A Lua app reads it as sys.board and picks its hardware path with it (docs/30).
inline const char* id() { return "m5stack-paper"; }

// The chip family, which is what an app means when its manifest says it runs on
// "esp32": every board of the family accepts that app, and a board that names a
// chip this one does not have is not an app of this board (docs/40).
inline const char* family() { return "esp32"; }

// This build is for a board and not for the PC emulator.
inline bool emulator() { return false; }

// The Paper has no light of its own: what it has is a screen, and a screen is
// not an LED (docs/50).
inline uint32_t led_count() { return 0; }
inline uint32_t led_max() { return 0; }

// No inertial sensor on this board.
inline bool has_imu() { return false; }

}  // namespace board

#endif  // BOARD_IDENTITY_H
