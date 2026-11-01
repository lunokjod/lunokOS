// **The glass of the bench, in a window** (docs/85).
//
// The emulator's glass is a buffer of four bits per pixel that remembers the rectangle of every paint.
// Without this it is a buffer nobody can look at; with `--gui` the same buffer is copied into an X11
// window whenever the panel is painted, so what an app draws on the PC is seen instead of guessed.
//
// It is a window and not a picture: no ink, no ghost, no flicker, and the quality of a paint is not
// drawn any differently because the bench has no waveform. The window is a way to *look*, and every
// number of how it looks is still the board's (docs/50, docs/85).
#ifndef LUNOK_EMU_GUI_H
#define LUNOK_EMU_GUI_H

#include <stdint.h>

namespace emu {

// True when this bench was built with a window in it. A build without X11 says so instead of failing
// at the first `--gui` (docs/85).
bool gui_available();

// Opens a window of that size. False when there is no display to open it on, which is not an error of
// the run: the bench carries on without it.
bool gui_open(uint16_t width, uint16_t height);

// **The glass in the colours of the display, which is the part that can be proved.** The buffer is four
// bits per pixel with the left pixel in the high nibble, and the window wants one 32-bit pixel each:
// `gui_expand` is that translation and nothing else, so a test can check it without a screen (docs/50,
// docs/85).
uint32_t gui_level_colour(uint8_t level);
// **`scale` is how many glass pixels one pixel of the window stands for**: 1 is the page as it is, and 2
// is the page at half, which is what a screen smaller than the page gets so that the whole of it fits
// instead of a corner of it (docs/85).
void gui_expand(const uint8_t* packed, uint16_t width, uint16_t height, uint32_t* out, uint8_t scale);

// **The glass, as it is now.** The buffer is the packed four-bit image the surface keeps, so the window
// costs one conversion per paint and not one per turn (docs/85).
void gui_show(const uint8_t* packed, uint16_t width, uint16_t height);

// What the window has to say, without waiting for it: an expose means draw again, and the corner that
// closes it means the run is over.
void gui_pump();

// True once the person closed the window: the run ends as if three ESC had been typed.
bool gui_close_requested();

// **How many page pixels one pixel of the window stands for**: 1 when the page fitted the screen, 2 or
// more when it had to be cut down to fit, so the run can say what a person is looking at (docs/85).
uint8_t gui_scale();

void gui_close();

}  // namespace emu

#endif  // LUNOK_EMU_GUI_H
