#include "emu/gui.h"

#include <stdio.h>

// **The part of the window that has no window in it**: the colours of the paper, the four bits to a
// display pixel, the way back from a mouse to the page, the geometry of the keys and the scale that
// fits. It is the same for X11 and SDL, and it is the part a test can prove without a screen (docs/85).
#include <stdlib.h>
#include <string.h>

namespace emu {

// **An e-ink panel is not black and white**: its paper is warm —a little yellow, a little grey— and its
// darkest level is not the absence of light but the blackest the pigment gets. The window shows that and
// not a perfect monochrome, because a page read on a perfect monochrome is a page that looks better than
// the one the person will hold (docs/85). The four bits still mean the same sixteen levels; only their
// colour changes.
const uint8_t kInkBlackR = 0x1A;
const uint8_t kInkBlackG = 0x1B;
const uint8_t kInkBlackB = 0x18;
const uint8_t kPaperR = 0xEF;
const uint8_t kPaperG = 0xEA;
const uint8_t kPaperB = 0xDC;

uint8_t mix_channel(uint8_t dark, uint8_t light, uint8_t level) {
  // level 0 is the ink and 15 is the paper, and the steps in between are straight: the waveform of this
  // glass is not linear, but a window that draws a curve nobody measured would be inventing physics
  // (docs/85).
  const int32_t span = static_cast<int32_t>(light) - static_cast<int32_t>(dark);
  const int32_t value = static_cast<int32_t>(dark) + (span * static_cast<int32_t>(level & 0x0F) + 7) / 15;
  return static_cast<uint8_t>((value < 0) ? 0 : ((value > 255) ? 255 : value));
}

uint32_t gui_level_colour(uint8_t level) {
  const uint8_t r = mix_channel(kInkBlackR, kPaperR, level);
  const uint8_t g = mix_channel(kInkBlackG, kPaperG, level);
  const uint8_t b = mix_channel(kInkBlackB, kPaperB, level);
  return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
}

// The four bits of two neighbours become two pixels, **the left one in the high nibble**, which is the
// packing of the panel itself (docs/50).
void gui_expand(const uint8_t* packed, uint16_t width, uint16_t height, uint32_t* out, uint8_t scale) {
  if (packed == nullptr || out == nullptr || scale == 0) return;
  const uint16_t out_width = static_cast<uint16_t>((width + scale - 1) / scale);
  const uint16_t out_height = static_cast<uint16_t>((height + scale - 1) / scale);
  for (uint16_t oy = 0; oy < out_height; ++oy) {
    const uint16_t y = static_cast<uint16_t>(oy * scale);
    const uint8_t* row = packed + static_cast<size_t>(y) * (width / 2);
    uint32_t* line = out + static_cast<size_t>(oy) * out_width;
    for (uint16_t ox = 0; ox < out_width; ++ox) {
      const uint16_t x = static_cast<uint16_t>(ox * scale);
      if (x >= width) {
        // A glass of an odd width ends on a nibble nobody wrote: it is white, like the panel's memory.
        line[ox] = gui_level_colour(15);
        continue;
      }
      const uint8_t byte = row[x / 2];
      line[ox] = gui_level_colour(((x & 1) == 0) ? static_cast<uint8_t>(byte >> 4)
                                                 : static_cast<uint8_t>(byte & 0x0F));
    }
  }
}

// **The way back**: the pixel of the window to the pixel of the page under it, which is what makes the
// mouse a finger. It is outside either backend because a test asks for it without a screen (docs/85).
void gui_page_point(int window_x, int window_y, uint8_t scale, uint16_t page_width, uint16_t page_height,
                    uint16_t& out_x, uint16_t& out_y) {
  const int step = (scale == 0) ? 1 : static_cast<int>(scale);
  int page_x = window_x * step + step / 2;
  int page_y = window_y * step + step / 2;
  if (page_x < 0) page_x = 0;
  if (page_y < 0) page_y = 0;
  if (page_x > static_cast<int>(page_width) - 1) page_x = static_cast<int>(page_width) - 1;
  if (page_y > static_cast<int>(page_height) - 1) page_y = static_cast<int>(page_height) - 1;
  out_x = static_cast<uint16_t>(page_x);
  out_y = static_cast<uint16_t>(page_y);
}

// **The geometry of the keys**, which is the one the descriptor of the input asks for: a dial on a side face
// is a **column** down that side —up, centre and down from the top, the way the wheel of the board is— and a
// key on the top or the front is a **row** under the page. The buttons are measured in window pixels and not
// in page pixels: how big a page is scaled to is the page's business, and a button that shrinks with it is a
// button a person cannot hit (docs/50, docs/85).
// A column takes a strip as wide as a button and its two margins; a row takes one as tall.

uint16_t gui_key_strip_width() { return kKeyStripWidth; }
uint16_t gui_key_row_height() { return kKeyRowHeight; }

uint16_t gui_keys_height(uint8_t count) {
  if (count == 0) return 0;
  return static_cast<uint16_t>(count * kKeyButtonHeight + (count - 1) * kKeyGap);
}

uint16_t gui_keys_width(uint8_t count) {
  if (count == 0) return 0;
  return static_cast<uint16_t>(count * kKeyButtonWidth + (count - 1) * kKeyGap);
}

// The column is **centred** in the window, which is where the wheel of the board is on its face.
int gui_keys_top(uint16_t window_height, uint8_t count) {
  const int height = static_cast<int>(gui_keys_height(count));
  if (height == 0 || static_cast<int>(window_height) <= height) return 0;
  return (static_cast<int>(window_height) - height) / 2;
}

// And the row is centred too, under the page.
int gui_keys_left(uint16_t window_width, uint8_t count) {
  const int width = static_cast<int>(gui_keys_width(count));
  if (width == 0 || static_cast<int>(window_width) <= width) return 0;
  return (static_cast<int>(window_width) - width) / 2;
}

int gui_key_of_slot(uint8_t count, uint8_t slot) {
  if (count == 0 || slot >= count) return -1;
  // One key is the middle one, which is the only button an ATOM has and the centre of the Paper's dial.
  if (count == 1) return 0;
  // Two are the sides with no middle to be the middle of.
  if (count == 2) return (slot == 0) ? 1 : 2;
  // **The wheel**: up first, the centre in the middle and down last, which is the order the Paper's `Spot`
  // gives the three keys (docs/50).
  if (slot == 0) return 1;
  if (slot == 1) return 0;
  return 2;
}

int gui_key_at(int window_x, int window_y, uint8_t count, uint16_t window_width, uint16_t window_height,
               KeySide side) {
  if (count == 0) return -1;
  if (side == KeySide::kBottom) {
    // **A row under the page**: the gap between two buttons is horizontal here.
    const int top = static_cast<int>(window_height) - static_cast<int>(kKeyRowHeight - kKeyMargin);
    if (window_y < top || window_y >= top + static_cast<int>(kKeyButtonHeight)) return -1;
    const int from_the_left = window_x - gui_keys_left(window_width, count);
    if (from_the_left < 0) return -1;
    const int pitch = static_cast<int>(kKeyButtonWidth + kKeyGap);
    const int slot = from_the_left / pitch;
    if (slot >= static_cast<int>(count)) return -1;
    if (from_the_left - slot * pitch >= static_cast<int>(kKeyButtonWidth)) return -1;
    return gui_key_of_slot(count, static_cast<uint8_t>(slot));
  }
  // **A column down a side**: the gap is vertical, and which side is the descriptor's to say.
  const int left = (side == KeySide::kRight)
                       ? static_cast<int>(window_width) - static_cast<int>(kKeyStripWidth) +
                             static_cast<int>(kKeyMargin)
                       : static_cast<int>(kKeyMargin);
  if (window_x < left || window_x >= left + static_cast<int>(kKeyButtonWidth)) return -1;
  const int pitch = static_cast<int>(kKeyButtonHeight + kKeyGap);
  const int below_the_top = window_y - gui_keys_top(window_height, count);
  if (below_the_top < 0) return -1;
  const int slot = below_the_top / pitch;
  if (slot >= static_cast<int>(count)) return -1;
  // The space between two buttons is not a button: a click there presses nothing, the same as a finger on
  // the case (docs/85).
  if (below_the_top - slot * pitch >= static_cast<int>(kKeyButtonHeight)) return -1;
  return gui_key_of_slot(count, static_cast<uint8_t>(slot));
}

// **The size of the page in the window, decided once** (docs/85): the run can ask for it (`--gui-scale`),
// and when it does not, the window takes the smallest scale that fits the screen whole —a page cut by the
// frame is worse than a page at half— with the room the window manager needs. It is pure and outside the X11
// backend because the choice has proof without a screen, like the expansion it feeds (docs/85).
uint8_t gui_fit_scale(uint16_t page_width, uint16_t page_height, uint8_t keys, KeySide side,
                      int screen_width, int screen_height, uint8_t requested) {
  if (requested >= 1 && requested <= 4) return requested;
  const int strip_w = (keys == 0) ? 0
                                  : ((side == KeySide::kBottom) ? 0
                                                                : static_cast<int>(gui_key_strip_width()));
  const int strip_h = (keys == 0) ? 0
                                  : ((side == KeySide::kBottom) ? static_cast<int>(gui_key_row_height()) : 0);
  uint8_t scale = 1;
  while (scale < 4 && (strip_w + static_cast<int>(page_width) / scale > screen_width - 40 ||
                       strip_h + static_cast<int>(page_height) / scale > screen_height - 80)) {
    ++scale;
  }
  return scale;
}

// The word on a key: one button is the whole board, and a dial is up, centre and down from the top.
const char* gui_key_label(uint8_t count, int key) {
  if (count == 1) return "Button";
  if (key == 1) return "Up";
  if (key == 2) return "Down";
  return "Centre";
}

// **The sign on a key of the dial** (0.85.1), in the key's own box: an arrow up, an arrow down and a full circle. The
// runs are one pixel tall, so a backend fills rectangles and nothing else, and the same numbers are what a test reads.
// A board of one button answers zero and keeps its word: its key is the whole board and a word is what it says.
uint8_t gui_key_icon(uint8_t count, int key, uint16_t width, uint16_t height, KeyRun* out, uint8_t capacity) {
  if (count < 2 || out == nullptr || capacity == 0 || width < 8 || height < 8) return 0;
  if (key != 0 && key != 1 && key != 2) return 0;
  uint8_t used = 0;
  const int cx = static_cast<int>(width) / 2;
  const int cy = static_cast<int>(height) / 2;
  if (key == 0) {
    // **The middle is a filled circle**: the wheel's own spot, and the one a person presses for "yes".
    int radius = static_cast<int>((width < height) ? width : height) / 5;
    if (radius < 3) radius = 3;
    for (int dy = -radius; dy <= radius && used < capacity; ++dy) {
      int dx = 0;
      while ((dx + 1) * (dx + 1) + dy * dy <= radius * radius) ++dx;
      const int wide = 2 * dx + 1;
      out[used].x = static_cast<uint16_t>(cx - dx);
      out[used].y = static_cast<uint16_t>(cy + dy);
      out[used].w = static_cast<uint16_t>(wide);
      out[used].h = 1;
      ++used;
    }
    return used;
  }
  // **An arrow**: a head that widens a line at a time and a stem under it —over it, when it points down—.
  const int head = static_cast<int>(height) / 4;
  const int half = static_cast<int>(width) / 5;
  const int stem = (static_cast<int>(width) / 8) | 1;
  const int tail = static_cast<int>(height) / 6;
  const int total = head + tail;
  const int top = (static_cast<int>(height) - total) / 2;
  const bool up = (key == 1);
  for (int i = 0; i < head && used < capacity; ++i) {
    const int wide = 1 + (2 * half * i) / (head - 1);
    const int row = up ? (top + i) : (top + total - 1 - i);
    out[used].x = static_cast<uint16_t>(cx - wide / 2);
    out[used].y = static_cast<uint16_t>(row);
    out[used].w = static_cast<uint16_t>(wide);
    out[used].h = 1;
    ++used;
  }
  if (used < capacity) {
    out[used].x = static_cast<uint16_t>(cx - stem / 2);
    out[used].y = static_cast<uint16_t>(up ? (top + head) : top);
    out[used].w = static_cast<uint16_t>(stem);
    out[used].h = static_cast<uint16_t>(tail);
    ++used;
  }
  return used;
}

}  // namespace emu
