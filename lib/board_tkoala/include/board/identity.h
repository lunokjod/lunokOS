// Who this firmware is for: **the LilyGO TTGO T-Koala**.
//
// Every board carries a header with exactly this name, with the same names inside and its own content, and
// platformio.ini puts the right one on the include path (-Ilib/board_<id>/include). Nothing in the project
// writes #ifdef for a board: whoever needs to know asks the HAL, and the HAL answers from here (docs/50).
//
// The board LilyGO sells as T-Koala V1.0 is an ESP32-WROVER-B with 4 MB of flash, 8 MB of PSRAM (the chip
// maps 4), one green LED on GPIO5, a microSD slot and a LiPo charger. It has no screen, no touch panel,
// no inertial sensor and no key the firmware can read: the only button is RST. There is also a WROOM-32 version without the PSRAM; this
// identity is the WROVER one, which is the board the PSRAM flags in boards/ttgo-t-koala.json describe
// (docs/50).
#ifndef BOARD_IDENTITY_H
#define BOARD_IDENTITY_H

#include <stdint.h>

// **Whether this build carries a glass** (0.179.9): **no**. The whole page —the canvases and the controls, the faces of the
// text, the reader of a book, the picture of the boot, the bar and the finger— is code this board can never run, and on a
// chip whose slot is nine tenths full it is flash that is not there either. It is a macro and not a function because the
// question is asked **before the compiler**: `#if LUNOK_GLASS` is what decides which translation units exist at all, and a
// function cannot leave a file out of a build. Whoever needs to know which glass a *running* board has still asks the HAL
// for `kCapDisplay` (docs/50, docs/75).
#define LUNOK_GLASS 0

namespace board {

// The name this board has in platformio.ini, letter for letter (`board = ...`).
// A Lua app reads it as sys.board and picks its hardware path with it (docs/30).
inline const char* id() { return "ttgo-t-koala"; }

// The chip family, which is what an app means when its manifest says it runs on
// "esp32": every board of the family accepts that app (docs/40).
inline const char* family() { return "esp32"; }

// False: there is real hardware under this build.
inline bool emulator() { return false; }

// **One green LED on GPIO5**: it is a plain LED and not an addressable chip, so the board HAL drives the
// pin and there is no colour. `led_count()` is one because the frame of the driver is built around a
// number of lights (docs/50).
inline constexpr uint32_t led_count() { return 1; }

// A plain LED has no per-channel ceiling of its own; 255 is the range the system paints in.
inline constexpr uint32_t led_max() { return 255; }

// False: the T-Koala has no inertial sensor (docs/50).
inline constexpr bool has_imu() { return false; }

}  // namespace board

#endif  // BOARD_IDENTITY_H
