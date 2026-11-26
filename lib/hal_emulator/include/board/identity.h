// Who this firmware is for.
//
// Same file name as in lib/board_atom/include/board/, different content: that is
// how the band is chosen, instead of an #ifdef (docs/50). This header includes
// nothing else on purpose, so any translation unit of the board library can use it.
#ifndef BOARD_IDENTITY_H
#define BOARD_IDENTITY_H

#include <stdint.h>

// Which board this emulator is. A PC is not a board and has no `board =` line in
// platformio.ini, so the person says which one it stands in for and it answers **that
// name**, the one the board has in its environment; `sys.emulator` stays true because
// nothing here is real hardware (docs/50, docs/85). An emulator that answered "emulator"
// would be telling an app that it is somewhere no board has ever been.
//
// It is a function and not a literal because the answer comes from the options, which
// can change between runs of the same binary: `--board m5stack-paper` is a Paper and
// the default is the ATOM.
namespace emu {
const char* active_board();
}  // namespace emu

// **Whether this build carries a glass** (0.179.9): **yes**, which is the default. The emulator can be told to be the
// Paper —and it is by default a board with a screen in the run of a person—, so every page of the system is built into it
// and the board it is standing in for decides at run time whether the glass is used.
//
// **And it is the one place where the answer can be given from outside** (`-DLUNOK_GLASS=0`), because the PC is also the
// only way to see what a board without a glass boots into: the same kernel, the same shell and the same interpreter, with
// the page, the reader and the faces left out. The four boards that say `0` in their own header are the reason the flag
// exists, and a build of this one that says it is how their boot is tried without flashing a chip (docs/50, docs/85).
#ifndef LUNOK_GLASS
#define LUNOK_GLASS 1
#endif

namespace board {

inline const char* id() { return emu::active_board(); }

// The emulator stands in for the band it imitates, and that band is an ESP32: an app
// that needs nothing but a generic esp32 runs here, and one that names a board does
// not —it is listed and refused, saying which boards it wants (docs/40,
// docs/85). It is not a chip of its own, so there is no other family to answer.
inline const char* family() { return "esp32"; }

// True: nothing here is real hardware. It is what tells an app that the numbers under
// this name are a fake.
inline bool emulator() { return true; }

// **El sostre del llum ja no és una constant d'aquest fitxer** (0.192.35): it was the Matrix's twenty for every model, o sigui que la
// retroil·luminació de la T5S3 —que arriba a 255— s'hi quedava al vuit per cent, i el panell del Matrix hi era igual. El número és del
// model (`emu::Model::led_max`), que és qui sap quina placa s'està emulant, i el demana `FakeLed::max_value()` i la finestra (docs/85).

// True: the fake board also imitates the Matrix's MPU6886, so an app that reads the
// sensor can be tried on the PC (docs/85).
inline constexpr bool has_imu() { return true; }

// The models this emulator knows how to be. It is the list `--board` accepts, and the
// reason the default can be named: one of them is always the answer, so `sys.board` is
// never a name that does not exist (docs/85). The hardware it fakes is the union of the
// four, which is why an app that asks for more than its model has will find it here and
// must trust `sys.emulator` before believing the numbers.
inline constexpr uint32_t models_count() { return 5; }
inline const char* model(uint32_t index) {
  if (index == 0) return "m5stack-atom";
  if (index == 1) return "m5stack-atom-matrix";
  if (index == 2) return "m5stack-paper";
  if (index == 3) return "ttgo-t-koala";
  // **La placa on s'han trobat la meitat dels defectes d'aquest sistema** (0.192.4, l'amo: *«Afegeix a l'emulador la placa T5S3 dels
  // collons i parem de fer l'imbécil, no confío en tu amb els canvis»*): `--board lilygo-t5s3-epaper-pro` fa aquest emulador la T5 E-Paper
  // S3 Pro —el vidre de 960x540 amb el GT911, el BQ27220 i el BQ25896, el PCF8563 i el son profund de tres portes— i el nom és el de la
  // seva banda, lletra per lletra, perquè `sys.board` digui el mateix que a la placa (docs/50, docs/85).
  if (index == 4) return "lilygo-t5s3-epaper-pro";
  return nullptr;
}

// The model the emulator is when nobody says otherwise: the ATOM, which is the family this
// band imitates and the smallest of the four.
inline const char* default_model() { return model(0); }

}  // namespace board

#endif  // BOARD_IDENTITY_H
