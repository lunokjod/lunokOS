// Who this firmware is for.
//
// Same file name as in lib/board_atom/include/board/, different content: that is
// how the band is chosen, instead of an #ifdef (docs/50). This header includes
// nothing else on purpose, so any translation unit of the board library can use it.
#ifndef BOARD_IDENTITY_H
#define BOARD_IDENTITY_H

#include <stdint.h>

namespace board {

// The PC is not a board and has no `board =` line in platformio.ini, so it says
// what it is. An app that checks sys.board knows it is on the bench, where the
// clock, the flash and the serial port are not the real ones.
inline const char* id() { return "emulator"; }
// The bench stands in for the band it imitates, and that band is an ESP32: an app
// that needs nothing but a generic esp32 runs here, and one that names a board
// does not —it is listed and refused, saying which boards it wants (docs/40,
// docs/85). It is not a chip of its own, so there is no other family to answer.
inline const char* family() { return "esp32"; }

// True: nothing here is real hardware.
inline bool emulator() { return true; }

// The fake light imitates the panel of the Matrix, ceiling included: a bench that
// let 255 through would hide the limit the board has, and the app would look fine
// on the PC and cook the panel (docs/30, docs/85).
inline constexpr uint32_t led_max() { return 20; }

// True: the fake board also imitates the Matrix's MPU6886, so an app that reads the
// sensor can be tried on the PC (docs/85).
inline constexpr bool has_imu() { return true; }

}  // namespace board

#endif  // BOARD_IDENTITY_H
