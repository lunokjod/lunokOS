// The panel, as the kernel hands it to an app (docs/30).
//
// **Nothing here draws on its own.** There is no splash, no status bar, no clearing the glass when an
// app closes: an app says what to draw and when, and what it leaves on the panel is what stays there.
// That is both the rule the system follows and the cheapest thing an e-ink glass can do —a picture
// that nobody asks to be repainted costs nothing (docs/50).
#ifndef LUNOK_KERNEL_DISPLAY_H
#define LUNOK_KERNEL_DISPLAY_H

#include <stdint.h>

#include "hal/hal.h"

namespace kernel {
namespace display {

// Reads the board's surface. False when this board has no screen (docs/50).
bool boot(hal::IPlatform& platform);
bool available();

uint16_t width();
uint16_t height();
// How many levels the glass has: sixteen on the Paper, black at 0 and white at 15 (docs/50).
uint8_t levels();

}  // namespace display
}  // namespace kernel

#endif  // LUNOK_KERNEL_DISPLAY_H
