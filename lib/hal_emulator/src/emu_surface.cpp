// The panel of the bench: a buffer with the same packing the real glass takes (docs/85).
#include "emu/surface.h"

#include <stdlib.h>
#include <string.h>

#include "emu/ink.h"
#include "emu/options.h"
#include "pal/pal.h"

namespace emu {
namespace {

const uint16_t kPanelWidth = 960;
const uint16_t kPanelHeight = 540;

}  // namespace

MemorySurface::MemorySurface(uint16_t width, uint16_t height)
    : width_(width), height_(height), stride_(static_cast<uint16_t>(width / 2)) {
  const size_t bytes = static_cast<size_t>(stride_) * height_;
  bits_ = static_cast<uint8_t*>(calloc(bytes, 1));
  // White, like the real glass after its own clear (docs/50).
  memset(bits_, 0xFF, bytes);
  alloc_ink();
}

// **The two buffers of the ink**, only when the model is on: what the glass shows and how dirty it is,
// four bits each, the same packing as the image (docs/85).
void MemorySurface::alloc_ink() {
  free(shown_);
  free(dirt_);
  shown_ = nullptr;
  dirt_ = nullptr;
  busy_until_ms_ = 0;
  if (options().ink == 0) return;
  (void)make_ink();
}

// The two buffers, white and clean: what the glass shows starts as the clear of the panel (docs/50).
bool MemorySurface::make_ink() {
  const size_t bytes = static_cast<size_t>(stride_) * height_;
  shown_ = static_cast<uint8_t*>(calloc(bytes, 1));
  dirt_ = static_cast<uint8_t*>(calloc(bytes, 1));
  if (shown_ == nullptr || dirt_ == nullptr) {
    free(shown_);
    free(dirt_);
    shown_ = nullptr;
    dirt_ = nullptr;
    return false;
  }
  memset(shown_, 0xFF, bytes);
  return true;
}

bool MemorySurface::set_ink(bool on) {
  if (on == ink_on()) return true;
  free(shown_);
  free(dirt_);
  shown_ = nullptr;
  dirt_ = nullptr;
  busy_until_ms_ = 0;
  return on ? make_ink() : true;
}

void MemorySurface::mark(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  if (w == 0 || h == 0) return;
  const uint16_t x1 = static_cast<uint16_t>(x + w);
  const uint16_t y1 = static_cast<uint16_t>(y + h);
  if (!dirty_) {
    dirty_ = true;
    dirty_x0_ = x;
    dirty_y0_ = y;
    dirty_x1_ = x1;
    dirty_y1_ = y1;
    return;
  }
  if (x < dirty_x0_) dirty_x0_ = x;
  if (y < dirty_y0_) dirty_y0_ = y;
  if (x1 > dirty_x1_) dirty_x1_ = x1;
  if (y1 > dirty_y1_) dirty_y1_ = y1;
}

void MemorySurface::clear(uint8_t level) {
  if (bits_ == nullptr) return;
  const uint8_t packed = static_cast<uint8_t>(((level & 0x0F) << 4) | (level & 0x0F));
  memset(bits_, packed, static_cast<size_t>(stride_) * height_);
  mark(0, 0, width_, height_);
}

void MemorySurface::fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t level) {
  if (bits_ == nullptr || w == 0 || h == 0) return;
  if (x >= width_ || y >= height_) return;
  if (x + w > width_) w = static_cast<uint16_t>(width_ - x);
  if (y + h > height_) h = static_cast<uint16_t>(height_ - y);
  const uint8_t packed = static_cast<uint8_t>(((level & 0x0F) << 4) | (level & 0x0F));
  // **The two ends are written a nibble at a time**: a rectangle that starts or ends at an odd pixel
  // splits a byte with its neighbour, and writing the whole byte writes that neighbour too. The panel
  // driver does the same, pixel for pixel (docs/50).
  const uint16_t end = static_cast<uint16_t>(x + w);
  for (uint16_t row = 0; row < h; ++row) {
    uint8_t* line = bits_ + static_cast<size_t>(y + row) * stride_;
    uint16_t at = x;
    if ((at & 1) != 0) {
      line[at / 2] = static_cast<uint8_t>((line[at / 2] & 0xF0) | (level & 0x0F));
      ++at;
    }
    const uint16_t whole = static_cast<uint16_t>((end - at) / 2);
    if (whole > 0) memset(line + at / 2, packed, whole);
    at = static_cast<uint16_t>(at + whole * 2);
    if (at < end) line[at / 2] = static_cast<uint8_t>((line[at / 2] & 0x0F) | ((level & 0x0F) << 4));
  }
  mark(x, y, w, h);
}

void MemorySurface::pixel(uint16_t x, uint16_t y, uint8_t level) {
  if (bits_ == nullptr || x >= width_ || y >= height_) return;
  uint8_t& byte = bits_[static_cast<size_t>(y) * stride_ + x / 2];
  if ((x & 1) == 0) {
    byte = static_cast<uint8_t>((byte & 0x0F) | ((level & 0x0F) << 4));
  } else {
    byte = static_cast<uint8_t>((byte & 0xF0) | (level & 0x0F));
  }
  mark(x, y, 1, 1);
}

void MemorySurface::blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t* packed,
                         uint16_t stride) {
  if (bits_ == nullptr || packed == nullptr || w == 0 || h == 0) return;
  if (x >= width_ || y >= height_) return;
  if (x + w > width_) w = static_cast<uint16_t>(width_ - x);
  if (y + h > height_) h = static_cast<uint16_t>(height_ - y);
  ++blits_;
  mark(x, y, w, h);
  if ((x & 1) != 0) {
    // **The source is indexed by its own column**: `packed` starts at the first pixel of the region, so
    // indexing it with the destination's x reads a byte that belongs to another part of the image. This
    // is what the driver does, and the emulator has to do the same or a test here says nothing about the
    // board (docs/50).
    for (uint16_t row = 0; row < h; ++row) {
      const uint8_t* line = packed + static_cast<size_t>(row) * stride;
      for (uint16_t column = 0; column < w; ++column) {
        const uint8_t byte = line[column / 2];
        const uint8_t level = ((column & 1) == 0) ? (byte >> 4) : (byte & 0x0F);
        pixel(static_cast<uint16_t>(x + column), static_cast<uint16_t>(y + row), level);
      }
    }
    return;
  }
  const uint16_t bytes = static_cast<uint16_t>(w / 2);
  for (uint16_t row = 0; row < h; ++row) {
    memcpy(bits_ + static_cast<size_t>(y + row) * stride_ + x / 2,
           packed + static_cast<size_t>(row) * stride, bytes);
  }
}

bool MemorySurface::set_rotation(uint16_t degrees) {
  if (degrees != 0 && degrees != 90 && degrees != 180 && degrees != 270) return false;
  const bool quarter = (degrees == 90 || degrees == 270);
  const uint16_t width = quarter ? kPanelHeight : kPanelWidth;
  const uint16_t height = quarter ? kPanelWidth : kPanelHeight;
  rotation_ = degrees;
  if (width == width_ && height == height_) return true;
  free(bits_);
  width_ = width;
  height_ = height;
  stride_ = static_cast<uint16_t>(width / 2);
  const size_t bytes = static_cast<size_t>(stride_) * height_;
  bits_ = static_cast<uint8_t*>(calloc(bytes, 1));
  memset(bits_, 0xFF, bytes);
  alloc_ink();
  return true;
}

bool MemorySurface::flush(hal::UpdateMode mode) {
  ++flushes_;
  last_mode_ = mode;
  // **With nothing marked there is nothing to paint**, which is what the panel driver does: a page that
  // nobody asked to be repainted is exactly what this system must not do (docs/30, docs/50).
  if (!dirty_) return true;
  // A paint is the rectangle that changed since the last one, and the quality it was asked for.
  Paint& paint = paints_[paint_count_ % kMaxPaints];
  paint.mode = mode;
  paint.x = dirty_x0_;
  paint.y = dirty_y0_;
  paint.w = static_cast<uint16_t>(dirty_x1_ - dirty_x0_);
  paint.h = static_cast<uint16_t>(dirty_y1_ - dirty_y0_);
  last_x_ = paint.x;
  last_y_ = paint.y;
  last_w_ = paint.w;
  last_h_ = paint.h;
  ++paint_count_;
  dirty_ = false;

  // **And now the ink.** The pixels of the region move towards what they were painted with, according to
  // the quality —a `du` keeps a quarter of what was there, a `gc16` cleans— and the paint takes as long
  // as the mode takes on the glass, which is what `idle()` will say (docs/50, docs/85).
  const InkMode ink = ink_mode(mode);
  if (shown_ != nullptr) apply_ink(paint.x, paint.y, paint.w, paint.h, mode);
  if (shown_ != nullptr && ink.ms > 0) {
    busy_until_ms_ = static_cast<uint32_t>(pal::system().millis()) + ink.ms;
  }
  return true;
}

void MemorySurface::apply_ink(uint16_t x, uint16_t y, uint16_t w, uint16_t h, hal::UpdateMode mode) {
  if (shown_ == nullptr || bits_ == nullptr) return;
  const InkMode ink = ink_mode(mode);
  const uint8_t dirt_step = ink.dirt_add_of_16;
  const uint16_t end_x = static_cast<uint16_t>(x + w);
  const uint16_t end_y = static_cast<uint16_t>(y + h);
  for (uint16_t row = y; row < end_y; ++row) {
    for (uint16_t column = x; column < end_x; ++column) {
      const uint8_t target = level_at(column, row);
      const size_t index = static_cast<size_t>(row) * stride_ + column / 2;
      const bool high = ((column & 1) == 0);
      uint8_t dirt_byte = dirt_[index];
      const uint8_t dirt = high ? static_cast<uint8_t>(dirt_byte >> 4) : static_cast<uint8_t>(dirt_byte & 0x0F);
      uint8_t shown_byte = shown_[index];
      const uint8_t was = high ? static_cast<uint8_t>(shown_byte >> 4) : static_cast<uint8_t>(shown_byte & 0x0F);

      uint8_t level = ink_blend(target, was, dirt, ink);
      // **The speckle**: a paint that does not clean can miss the level it asked for, and the pixel keeps
      // the miss until something cleans the page (docs/85).
      if (ink.err_per_mille != 0 && ink_misses(column, row, ink.err_per_mille)) {
        const int16_t moved = static_cast<int16_t>(level) + ink_miss_by(column, row);
        level = static_cast<uint8_t>((moved < 0) ? 0 : ((moved > 15) ? 15 : moved));
      }

      uint8_t next_dirt = (dirt_step == 0) ? 0 : static_cast<uint8_t>(dirt + dirt_step);
      if (next_dirt > 15) next_dirt = 15;

      if (high) {
        shown_byte = static_cast<uint8_t>((shown_byte & 0x0F) | (level << 4));
        dirt_byte = static_cast<uint8_t>((dirt_byte & 0x0F) | (next_dirt << 4));
      } else {
        shown_byte = static_cast<uint8_t>((shown_byte & 0xF0) | level);
        dirt_byte = static_cast<uint8_t>((dirt_byte & 0xF0) | next_dirt);
      }
      shown_[index] = shown_byte;
      dirt_[index] = dirt_byte;
    }
  }
}

uint8_t MemorySurface::shown_at(uint16_t x, uint16_t y) const {
  const uint8_t* from = (shown_ != nullptr) ? shown_ : bits_;
  if (from == nullptr || x >= width_ || y >= height_) return 0;
  const uint8_t byte = from[static_cast<size_t>(y) * stride_ + x / 2];
  return ((x & 1) == 0) ? static_cast<uint8_t>(byte >> 4) : static_cast<uint8_t>(byte & 0x0F);
}

uint8_t MemorySurface::dirt_at(uint16_t x, uint16_t y) const {
  if (dirt_ == nullptr || x >= width_ || y >= height_) return 0;
  const uint8_t byte = dirt_[static_cast<size_t>(y) * stride_ + x / 2];
  return ((x & 1) == 0) ? static_cast<uint8_t>(byte >> 4) : static_cast<uint8_t>(byte & 0x0F);
}

// **How much of the glass is not what it was painted with**, which is the number that says how dirty a
// page is: the speckle of a `du` and the trace of the page before it (docs/85).
uint32_t MemorySurface::ink_errors() const {
  if (shown_ == nullptr || bits_ == nullptr) return 0;
  uint32_t errors = 0;
  for (uint16_t row = 0; row < height_; ++row) {
    for (uint16_t column = 0; column < width_; ++column) {
      if (shown_at(column, row) != level_at(column, row)) ++errors;
    }
  }
  return errors;
}

const MemorySurface::Paint* MemorySurface::paint_at(uint32_t index) const {
  if (index >= paint_count_) return nullptr;
  if (paint_count_ - index > kMaxPaints) return nullptr;
  return &paints_[index % kMaxPaints];
}

// **A paint takes as long as the glass takes**, and `idle()` is the question for it: this is what makes
// `run panel` in the bench say a number that means something, and what an app that waits for the ink
// sees (docs/30, docs/85).
bool MemorySurface::idle() {
  if (!ink_on()) return true;
  return static_cast<uint32_t>(pal::system().millis()) >= busy_until_ms_;
}

uint8_t MemorySurface::level_at(uint16_t x, uint16_t y) const {
  if (bits_ == nullptr || x >= width_ || y >= height_) return 0;
  const uint8_t byte = bits_[static_cast<size_t>(y) * stride_ + x / 2];
  return ((x & 1) == 0) ? static_cast<uint8_t>(byte >> 4) : static_cast<uint8_t>(byte & 0x0F);
}

MemorySurface* memory_surface() {
  static MemorySurface panel(kPanelWidth, kPanelHeight);
  return &panel;
}

}  // namespace emu
