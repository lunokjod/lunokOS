// What has to be linked for this board.
//
// One file with this name per board, same includes on the inside, and the include
// path decides which one is seen. src/board_link.cpp includes it so that
// PlatformIO's dependency finder compiles this band's libraries (docs/50, docs/80).
//
// Same content as lib/board_atom/include/board/impl.h on purpose: the Matrix is the
// same band as the Lite, with the same HAL and the same PAL, and the only thing
// that changes between the two is the identity header above (docs/50).
#ifndef BOARD_IMPL_H
#define BOARD_IMPL_H

#include "board/identity.h"
#include "hal_atom.h"
#include "pal_esp32.h"

#endif  // BOARD_IMPL_H
