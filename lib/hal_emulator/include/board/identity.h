// Who this firmware is for.
//
// Same file name as in lib/board_atom/include/board/, different content: that is
// how the band is chosen, instead of an #ifdef (docs/50). This header includes
// nothing else on purpose, so any translation unit of the board library can use it.
#ifndef BOARD_IDENTITY_H
#define BOARD_IDENTITY_H

#include <stdint.h>

// Which board this bench is. A PC is not a board and has no `board =` line in
// platformio.ini, so the person says which one it stands in for and it answers **that
// name**, the one the board has in its environment; `sys.emulator` stays true because
// nothing here is real hardware (docs/50, docs/85). A bench that answered "emulator"
// would be telling an app that it is somewhere no board has ever been.
//
// It is a function and not a literal because the answer comes from the options, which
// can change between runs of the same binary: `--board m5stack-paper` is a Paper and
// the default is the ATOM.
namespace emu {
const char* active_board();
}  // namespace emu

namespace board {

inline const char* id() { return emu::active_board(); }

// The bench stands in for the band it imitates, and that band is an ESP32: an app
// that needs nothing but a generic esp32 runs here, and one that names a board does
// not —it is listed and refused, saying which boards it wants (docs/40,
// docs/85). It is not a chip of its own, so there is no other family to answer.
inline const char* family() { return "esp32"; }

// True: nothing here is real hardware. It is what tells an app that the numbers under
// this name are a fake.
inline bool emulator() { return true; }

// The fake light imitates the panel of the Matrix, ceiling included: a bench that
// let 255 through would hide the limit the board has, and the app would look fine
// on the PC and cook the panel (docs/30, docs/85).
inline constexpr uint32_t led_max() { return 20; }

// True: the fake board also imitates the Matrix's MPU6886, so an app that reads the
// sensor can be tried on the PC (docs/85).
inline constexpr bool has_imu() { return true; }

// The models this bench knows how to be. It is the list `--board` accepts, and the
// reason the default can be named: one of them is always the answer, so `sys.board` is
// never a name that does not exist (docs/85). The hardware it fakes is the union of the
// three, which is why an app that asks for more than its model has will find it here and
// must trust `sys.emulator` before believing the numbers.
inline constexpr uint32_t models_count() { return 3; }
inline const char* model(uint32_t index) {
  if (index == 0) return "m5stack-atom";
  if (index == 1) return "m5stack-atom-matrix";
  if (index == 2) return "m5stack-paper";
  return nullptr;
}

// The model the bench is when nobody says otherwise: the ATOM, which is the family this
// band imitates and the smallest of the three.
inline const char* default_model() { return model(0); }

}  // namespace board

#endif  // BOARD_IDENTITY_H
