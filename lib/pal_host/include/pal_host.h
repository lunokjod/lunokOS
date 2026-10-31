// Marker header for lib/pal_host. See src/board_link.cpp.
#ifndef LUNOK_PAL_HOST_H
#define LUNOK_PAL_HOST_H

#include <stdint.h>

namespace pal {}

namespace pal_host {

// How many times the console was flushed. The host console has no buffer to wait for, so
// the flush is only visible as a count: it is what lets a test say that the kernel asks
// for the line to be empty before the board goes to sleep (docs/50, docs/85).
uint32_t console_flushes();

}  // namespace pal_host

#endif  // LUNOK_PAL_HOST_H
