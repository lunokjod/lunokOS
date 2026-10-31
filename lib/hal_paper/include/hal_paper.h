// Marker header for lib/hal_paper.
//
// Nothing in the code includes this on purpose: the board is chosen by the
// platformio.ini environment, not by an include. It exists so that
// src/board_link.cpp has a filename to include, which is the only way
// PlatformIO's dependency finder learns that this library belongs to the build.
#ifndef LUNOK_HAL_PAPER_H
#define LUNOK_HAL_PAPER_H

namespace hal {}
namespace hal_paper {}

#endif  // LUNOK_HAL_PAPER_H
