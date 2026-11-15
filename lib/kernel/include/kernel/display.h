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

// **Com s'envien els píxels al controlador** (0.189.66): tres maneres —una transacció per fila, bursts, o un bit per píxel— que
// el vidre tria i que el prompt canvia amb `panel flush`. La que el vidre no sàpiga fer es refusa i es diu (docs/50).
hal::FlushStyle flush_style();
bool set_flush_style(hal::FlushStyle style);
// The word of a style and the style of a word, so the prompt and the log speak the same three names (docs/50).
const char* flush_style_name(hal::FlushStyle style);
bool flush_style_of(const char* name, hal::FlushStyle& out);

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

// **El rellotge de l'experiment** (0.189.74): sending the pixels to the panel is a page of work done at the resting step of
// the processor —the board measures three hundred and twenty milliseconds of a page— and whether that time is the code or the
// memory of the chip is a question only one variable answers: the speed. This asks for the top step for a window that covers
// a paint of this panel and leaves it to the system's own clock to bring it down again (docs/50, docs/95). **És un
// experiment i no una política**: a step that stays up is a board burning its battery, and the window below is generous on
// purpose so that no paint is left half way.
// **I el rellotge del bus** (0.189.77): `panel bus <mhz>`, so the three numbers of the wire are measured in one session of the
// board and not in three flashes. It is not remembered across a boot either (docs/50).
uint32_t bus_mhz();
bool set_bus_mhz(uint32_t mhz);

const int32_t kPaintWindowMs = 1500;
void hold_for_a_paint();
bool idle();
// Why the last paint was refused, in the panel's own words (docs/50).
const char* last_error();

}  // namespace display
}  // namespace kernel

#endif  // LUNOK_KERNEL_DISPLAY_H
