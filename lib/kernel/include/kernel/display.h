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

// **The one orientation of the system**: how far the content of the glass is turned from the frame the
// touch controller counts in. 0, 90, 180 or 270, and the bar, the view of an app and the fingers all
// read it from here instead of having an opinion of their own (docs/90). On this board it is **90**: the
// chip counts in 540×960 and the glass is 960×540.
uint16_t rotation();
bool set_rotation(uint16_t degrees);

// **Whether a partial paint may start where the change is** (0.179.12): the glass of this project was measured to
// want the left edge of its own image (0.118.7) on a night when a second fault was in the way (0.118.9), and until
// it is asked again every partial paint pays the pixels between the edge and the change —the whole height of the
// page for a cell at the bottom of a launcher—. The kernel does not have an opinion: it asks the glass, and the
// prompt asks with `panel` (docs/30, docs/50).
bool partial_from_the_box();
bool set_partial_from_the_box(bool on);

// **The board's buffer, and it belongs to the system.** An app never touches it: it draws on its own
// canvas and `ui::present` is what composes it here, which is the only way the glass changes
// (docs/30).
hal::ISurface* surface();

// The one place a quality name becomes what the controller does: the eight names of the panel's own
// table, and nothing else. An unknown name is refused instead of rounded to a default that would look
// like it worked (docs/30, docs/50).
bool mode_from_name(const char* name, hal::UpdateMode& out);

// Sends what changed on the surface since the last call and asks for it to be painted **with that
// quality**. What changed is the damage the app left there, so a present that follows one region pays
// for one region (docs/30).
bool present(hal::UpdateMode mode);
bool idle();
// Why the last paint was refused, in the panel's own words (docs/50).
const char* last_error();

}  // namespace display
}  // namespace kernel

#endif  // LUNOK_KERNEL_DISPLAY_H
