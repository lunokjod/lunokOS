// Who this firmware is for.
//
// Every board carries a header with exactly this name, with the same names inside
// and its own content, and platformio.ini puts the right one on the include path
// (-Ilib/board_<id>/include). Nothing in the project writes #ifdef for a board:
// whoever needs to know asks the HAL, and the HAL answers from here (docs/50).
// This header includes nothing else on purpose, so any translation unit of the
// board can use it.
//
// It lives next to the board and not inside the implementation library because
// hal_atom is the **family** HAL and serves two boards (the Lite and the Matrix):
// two headers with this name inside lib/ would both be on the include path and
// which one won would be a matter of order, which is the one thing this design
// does not leave to chance (docs/50).
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
inline const char* id() { return "m5stack-atom"; }

// The chip family, which is what an app means when its manifest says it runs on
// "esp32": every board of the family accepts that app, and a board that names a
// chip this one does not have is not an app of this board (docs/40).
inline const char* family() { return "esp32"; }

// False: there is real hardware under this build.
inline bool emulator() { return false; }

// How many lights the strip on GPIO27 has: one on the Lite, twenty-five on the
// Matrix. It is a constant of this header and not a number read at run time, so
// the driver's frame is the size of this board's strip and no bigger (docs/50,
// docs/75).
inline constexpr uint32_t led_count() { return 1; }

// The highest a channel of this light may be driven at. One LED on its own takes
// the whole range; a panel tight in a plastic case does not (see board_matrix).
inline constexpr uint32_t led_max() { return 255; }

// False: the Lite has a light, a button and an infrared emitter, and no inertial
// sensor (docs/50).
inline constexpr bool has_imu() { return false; }

}  // namespace board

#endif  // BOARD_IDENTITY_H
