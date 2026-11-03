#include "emu/ink.h"

namespace emu {
namespace {

// **The bus of the panel, and what a row costs on top of its bytes.** The clock is the driver's (ten
// megahertz, `kSpiHz` in `esp32/it8951.h`), and the per-row cost is the one number that comes out of a
// measurement instead of a datasheet: on 1 November the board painted the page of `run panel` in
// **1 985 ms** (docs/95, E-7) — a `du` of the whole view and a `gc16` of the ladder — and that is 570 ms
// of ink and 227 ms of wire over the 1 068 rows of the glass, so the rows cost the rest, about **1 112 µs
// each**. A row is sent by a transaction of its own (`It8951::flush` hands the bus one row at a time), and
// why that transaction costs a millisecond outside the wire is not answered here; the number is, and it is
// the number the bench uses so that a page here costs what a page costs there (docs/85, docs/95).
const uint32_t kBusHz = 10000000;
const uint32_t kRowUs = 1100;

// **How much a quick paint can leave, and how much dirt can add to it.** A `du` that finds a page it did
// not expect cannot take it: it lands short by a share of what it had to move, and that share is what a
// person sees as the ghost of the image before. On the glass it is not subtle —a black bar painted over
// with white in one quick pass is a grey bar— and a page painted again and again with a quick quality is
// a page that moves the ink less and less (image sticking). The dirt is capped here on purpose: past a
// point the page would stop being a page, and what a panel does is get dirty, not freeze (docs/85).
const uint16_t kDirtCapOf16 = 6;
const uint16_t kKeepCapOf16 = 13;

}  // namespace

InkMode ink_mode(hal::UpdateMode mode) {
  // **Which mode costs what, and what it leaves.** The times are the datasheet's for this glass (450 ms
  // for a `gc16`, 120 for a `du`); the trace and the dirt are the model's, and they say what a person
  // sees on a panel: a page painted with `du` keeps the one before it at a quarter of its ink, and a
  // page cleaned with `gc16` is clean (docs/50, docs/85).
  switch (mode) {
    case hal::UpdateMode::kInit:
      // The two seconds of the maker's own init, and the only paint that also takes the dirt of the
      // panel away (docs/50): four long blinks and the image arriving.
      return InkMode{2000, 0, 0, 0, true, 4, {15, 0, 15, 0, 0, 0}, 6};
    case hal::UpdateMode::kGc16:
      // **The one that cleans, and it cleans by blinking**: the whole region goes black and white twice
      // before the image. It is 450 ms of a panel with work to do, which is the price of a page without a
      // ghost (docs/50, docs/85).
      return InkMode{450, 0, 0, 0, true, 4, {0, 15, 0, 15, 0, 0}, 6};
    case hal::UpdateMode::kGl16:
      // The gentler clean: one blink, and the image arrives over the rest of the time.
      return InkMode{450, 0, 0, 0, true, 2, {0, 15, 0, 0, 0, 0}, 6};
    case hal::UpdateMode::kGlr16:
    case hal::UpdateMode::kGld16:
      // The reduced ones: they clean and they do not blink. The image lands at the end of the time, with
      // no frame in between (docs/85).
      return InkMode{450, 0, 0, 0, false, 0, {0, 0, 0, 0, 0, 0}, 0};
    case hal::UpdateMode::kDu:
      // The quick one: **almost a third of what was there stays** and it leaves a little dirt, so a page
      // painted with it once is a page with the one before it showing through (docs/85).
      return InkMode{120, 5, 1, 8, false, 0, {0, 0, 0, 0, 0, 0}, 0};
    case hal::UpdateMode::kDu4:
      // Quicker and dirtier: nearly half of it stays, and the grain is twice as much. It is meant for a
      // cursor and not for a page.
      return InkMode{120, 7, 2, 15, false, 0, {0, 0, 0, 0, 0, 0}, 0};
    case hal::UpdateMode::kA2:
      // The flashing one: it blinks the page white and black first, which is what makes it land close to
      // the level, and it is the reason it is only used for a clock (docs/50).
      return InkMode{290, 2, 0, 3, true, 2, {15, 0, 0, 0, 0, 0}, 3};
  }
  return InkMode{120, 4, 1, 1, false, 0, {0, 0, 0, 0, 0, 0}, 0};
}

uint32_t ink_transfer_us(uint16_t w, uint16_t h) {
  if (w == 0 || h == 0) return 0;
  // Four bits a pixel, so a row of `w` pixels is `w` over two bytes, and eight bits a byte.
  const uint32_t bytes = static_cast<uint32_t>(w + 1) / 2;
  const uint32_t wire_us = bytes * 8000u / (kBusHz / 1000u);
  return static_cast<uint32_t>(h) * (wire_us + kRowUs);
}

uint8_t ink_blend(uint8_t target, uint8_t shown, uint8_t dirt, const InkMode& mode) {
  const uint8_t level = static_cast<uint8_t>(target & 0x0F);
  const uint8_t was = static_cast<uint8_t>(shown & 0x0F);
  // **The trace is a share of what the pixel had to move**, which is why a page of sixteen levels leaves
  // a page of sixteen levels at lower contrast and a flat page leaves almost nothing: the ghost of a
  // picture is the picture, and the ghost of a white area is a slightly grey white (docs/85).
  uint16_t keep = mode.keep_of_16;
  keep += (dirt > kDirtCapOf16) ? kDirtCapOf16 : dirt;
  if (keep == 0) return level;
  if (keep > kKeepCapOf16) keep = kKeepCapOf16;
  // (target * (16 - keep) + shown * keep) / 16, in integers.
  const uint16_t mixed = static_cast<uint16_t>(level) * static_cast<uint16_t>(16 - keep) +
                         static_cast<uint16_t>(was) * static_cast<uint16_t>(keep);
  return static_cast<uint8_t>((mixed + 8) >> 4);
}

bool ink_misses(uint16_t x, uint16_t y, uint16_t err_per_mille) {
  if (err_per_mille == 0) return false;
  // A hash of the place and not a random number: the same pixel misses on every run, so what a person
  // sees in the window is what a test can read, and a failing test never becomes a flaky one (docs/85).
  const uint32_t hash = (static_cast<uint32_t>(x) * 73856093u) ^ (static_cast<uint32_t>(y) * 19349663u);
  return (hash % 1000u) < err_per_mille;
}

int8_t ink_miss_by(uint16_t x, uint16_t y) {
  // **A miss is not always one level**: half of the pixels that miss land low and half land high —a
  // panel with a tired waveform does not smear in one direction— and one miss in four is two levels off,
  // which is what turns the veil of a repainted page into grain (docs/85).
  const uint32_t hash = (static_cast<uint32_t>(x) * 83492791u) ^ (static_cast<uint32_t>(y) * 2971215073u);
  const int8_t step = ((hash >> 3) & 1u) ? static_cast<int8_t>(1) : static_cast<int8_t>(-1);
  const bool far = ((hash >> 5) & 3u) == 0;
  return far ? static_cast<int8_t>(step * 2) : step;
}

}  // namespace emu
