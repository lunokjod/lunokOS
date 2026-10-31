// What has to be linked for this board.
//
// One file with this name per board library, same includes on the inside, and the
// include path decides which one is seen. src/board_link.cpp includes it so that
// PlatformIO's dependency finder compiles this band's libraries (docs/50, docs/80).
#ifndef BOARD_IMPL_H
#define BOARD_IMPL_H

#include "board/identity.h"
#include "hal_paper.h"
#include "pal_esp32.h"

#endif  // BOARD_IMPL_H
