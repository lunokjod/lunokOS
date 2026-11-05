// What has to be linked for this board.
//
// One file with this name per board library, same includes on the inside, and the include path decides which
// one is seen. src/board_link.cpp includes it so that PlatformIO's dependency finder compiles this band's
// libraries (docs/50, docs/80).
//
// The T-Koala is of the ESP32 family and takes the ESP32's PAL like every board of the chip; its board HAL
// is its own because the light, the key and the card slot are wired to pins the ATOM and the Paper do not
// share (docs/50).
#ifndef BOARD_IMPL_H
#define BOARD_IMPL_H

#include "board/identity.h"
#include "hal_tkoala.h"
#include "pal_esp32.h"

#endif  // BOARD_IMPL_H
