// The panel of the bench: a buffer with the same packing the real glass takes (docs/85).
#include "emu/surface.h"

#include <stdlib.h>
#include <string.h>

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
  return true;
}

const MemorySurface::Paint* MemorySurface::paint_at(uint32_t index) const {
  if (index >= paint_count_) return nullptr;
  if (paint_count_ - index > kMaxPaints) return nullptr;
  return &paints_[index % kMaxPaints];
}

bool MemorySurface::idle() { return true; }

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
