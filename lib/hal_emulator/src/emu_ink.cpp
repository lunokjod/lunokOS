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

}  // namespace

InkMode ink_mode(hal::UpdateMode mode) {
  // **Which mode costs what, and what it leaves.** The times are the datasheet's for this glass (450 ms
  // for a `gc16`, 120 for a `du`); the trace and the dirt are the model's, and they say what a person
  // sees on a panel: a page painted with `du` keeps the one before it at a quarter of its ink, and a
  // page cleaned with `gc16` is clean (docs/50, docs/85).
  switch (mode) {
    case hal::UpdateMode::kInit:
      // The two seconds of the maker's own init, and the only paint that also takes the dirt of the
      // panel away (docs/50).
      return InkMode{2000, 0, 0, 0, false};
    case hal::UpdateMode::kGc16:
    case hal::UpdateMode::kGl16:
      return InkMode{450, 0, 0, 0, false};
    case hal::UpdateMode::kGlr16:
    case hal::UpdateMode::kGld16:
      return InkMode{450, 0, 0, 0, false};
    case hal::UpdateMode::kDu:
      // The quick one: a quarter of what was there stays, and it leaves a little dirt.
      return InkMode{120, 4, 1, 1, false};
    case hal::UpdateMode::kDu4:
      // Quicker and dirtier: it is meant for a cursor and not for a page.
      return InkMode{120, 6, 2, 2, false};
    case hal::UpdateMode::kA2:
      // The flashing one: it gets close to the level and leaves no dirt, but it blinks the page white
      // and black first, which on this glass is the reason it is only used for a clock (docs/50).
      return InkMode{290, 1, 0, 0, true};
  }
  return InkMode{120, 4, 1, 1, false};
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
  // **The trace grows with the dirt**: a page painted ten times with `du` keeps more of the page before
  // than one painted once, which is exactly what happens on the glass (docs/85).
  uint16_t keep = mode.keep_of_16;
  keep += (dirt > 11) ? 11 : dirt;
  if (keep == 0) return level;
  if (keep > 15) keep = 15;
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
  // Half of the pixels that miss land a level low and half a level high, which is what a panel with a
  // tired waveform does: it does not smear in one direction (docs/85).
  const uint32_t hash = (static_cast<uint32_t>(x) * 83492791u) ^ (static_cast<uint32_t>(y) * 2971215073u);
  return ((hash >> 3) & 1u) ? static_cast<int8_t>(1) : static_cast<int8_t>(-1);
}

}  // namespace emu
