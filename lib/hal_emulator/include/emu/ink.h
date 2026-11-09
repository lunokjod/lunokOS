// **The ink of the emulator's glass, as a model and not as a photograph** (docs/85).
//
// An e-ink panel does not switch pixels: it moves pigment. What was there does not go away in one paint
// —`du` leaves a trace of the page before, and the traces add up— and only the modes that clean (`gc16`
// and its family) take the page back. This is that, in one table and one integer blend, so the emulator can
// show what the board shows instead of a perfect picture nobody has ever seen.
//
// **The numbers are the maker's, not a measurement of this unit**: 450 ms for a `gc16` and 120 for a
// `du` are the datasheet's; the trace each mode leaves and the dirt it adds are the model's. They are
// here, in one place, so a day on the board can change them and nothing else has to move (docs/95).
#ifndef LUNOK_EMU_INK_H
#define LUNOK_EMU_INK_H

#include <stdint.h>

#include "hal/hal.h"

namespace emu {

struct InkMode {
  // How long the glass takes to move the ink of a paint, in milliseconds.
  uint16_t ms;
  // **How much of what was shown stays**: sixteenths of the old level. Zero is a paint that cleans.
  uint8_t keep_of_16;
  // How much dirt this paint leaves behind, in sixteenths, added to the pixel it touches. A paint that
  // cleans takes it back to zero instead (docs/85).
  uint8_t dirt_add_of_16;
  // **Of every thousand pixels of the region, how many do not land on the level they were asked for**:
  // the speckle a real panel has, and the reason a page painted over and over is not a clean page.
  uint16_t err_per_mille;
  // **Whether the glass blinks before it settles** —the page goes white and then black before the image
  // arrives—, which is what `a2` is for and the reason it is only used for a clock (docs/50, docs/85).
  bool flashes;
  // **The blinks, in order**: the level the whole region goes to before the image. A `gc16` blinks black
  // and white twice —which is why it cleans—, a `gl16` once, and an `a2` goes white and then black. A mode
  // with no blink (`du`, `du4`, `glr16`, `gld16`) has none (docs/85).
  uint8_t phases;
  uint8_t levels[6];
  // **How many visible steps the image takes to arrive** after the last blink: the glass is not a switch,
  // and a person looking at the region sees it land instead of jumping (docs/85).
  uint8_t settle_steps;
};

InkMode ink_mode(hal::UpdateMode mode);

// **What sending a region to the glass costs, in microseconds**: the pixels over the wire and the row
// around them. A byte is two pixels in four bits, the driver's bus runs at ten megahertz —eight hundred
// nanoseconds a byte— and a row costs more than its bytes, because the driver hands the bus one row at a
// time and every row is a transaction of its own (docs/50, docs/85).
uint32_t ink_transfer_us(uint16_t w, uint16_t h);

// The level the glass shows after a paint of `target` over what was showing, with that much dirt in the
// pixel. Integer arithmetic and no table: one multiply, one add and one shift per pixel.
uint8_t ink_blend(uint8_t target, uint8_t shown, uint8_t dirt, const InkMode& mode);

// **Whether this pixel of the region misses**, and by how much: a deterministic hash of the place, so a
// run is the same run twice and a test can say what it expects (docs/85).
bool ink_misses(uint16_t x, uint16_t y, uint16_t err_per_mille);
int8_t ink_miss_by(uint16_t x, uint16_t y);

}  // namespace emu

#endif  // LUNOK_EMU_INK_H
