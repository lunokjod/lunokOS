// The canvas of the app, the damage it leaves on it and the copy that puts it on the glass
// (docs/30): see `ui.h` for what the separation is for.
#include "kernel/ui.h"

#include <stdio.h>
#include <string.h>

#include "kernel/bar.h"
#include "kernel/display.h"
#include "luabind/luabind.h"
#include "pal/pal.h"

namespace kernel {
namespace ui {
namespace {

Canvas g_canvas;
bool g_attached = false;

Damage g_damage[kMaxDamage];
uint8_t g_damage_count = 0;

// Two regions of the same quality this close become one paint: a frame drawn side by side, a line of
// text one letter at a time. Merging them costs a few pixels of ink and saves a whole update, and an
// update is what an e-ink panel is slow at (docs/50).
const uint16_t kMergeGap = 2;

uint8_t clamped(uint8_t level) { return (level > 15) ? 15 : level; }

// A quality name from an app, or `du` when it named none. False only when the name is not one of the
// eight of this glass, which the caller is told instead of being rounded to another (docs/30).
bool mode_or_default(const char* name, hal::UpdateMode& out) {
  if (name == nullptr) {
    out = hal::UpdateMode::kDu;
    return true;
  }
  return display::mode_from_name(name, out);
}

void merge_all(hal::UpdateMode mode) {
  if (g_damage_count == 0) return;
  uint16_t x0 = g_damage[0].x;
  uint16_t y0 = g_damage[0].y;
  uint16_t x1 = static_cast<uint16_t>(g_damage[0].x + g_damage[0].w);
  uint16_t y1 = static_cast<uint16_t>(g_damage[0].y + g_damage[0].h);
  for (uint8_t i = 1; i < g_damage_count; ++i) {
    if (g_damage[i].x < x0) x0 = g_damage[i].x;
    if (g_damage[i].y < y0) y0 = g_damage[i].y;
    if (g_damage[i].x + g_damage[i].w > x1) x1 = static_cast<uint16_t>(g_damage[i].x + g_damage[i].w);
    if (g_damage[i].y + g_damage[i].h > y1) y1 = static_cast<uint16_t>(g_damage[i].y + g_damage[i].h);
  }
  g_damage[0].x = x0;
  g_damage[0].y = y0;
  g_damage[0].w = static_cast<uint16_t>(x1 - x0);
  g_damage[0].h = static_cast<uint16_t>(y1 - y0);
  g_damage[0].mode = mode;
  g_damage_count = 1;
}

// Records one region. Same quality and close by: one paint. The whole list full: one paint with the
// quality that cannot look wrong, because an app that draws more pieces than the list holds is an app
// that is redrawing a page and a page wants `gc16` (docs/30).
void damage_add(uint16_t x, uint16_t y, uint16_t w, uint16_t h, hal::UpdateMode mode) {
  if (g_canvas.bits == nullptr || w == 0 || h == 0) return;
  if (x >= g_canvas.width || y >= g_canvas.height) return;
  if (x + w > g_canvas.width) w = static_cast<uint16_t>(g_canvas.width - x);
  if (y + h > g_canvas.height) h = static_cast<uint16_t>(g_canvas.height - y);
  // **The region is kept in whole nibbles.** Two pixels share a byte, so a region that starts at an odd
  // x carries its left neighbour: the copy is then byte for byte instead of pixel by pixel, and the
  // neighbour is the canvas' own pixel, which is the truth of the view anyway (docs/50).
  const uint16_t snap_first = static_cast<uint16_t>(x & ~1u);
  uint16_t snap_last = static_cast<uint16_t>((x + w + 1) & ~1u);
  if (snap_last > g_canvas.width) snap_last = g_canvas.width;
  x = snap_first;
  w = static_cast<uint16_t>(snap_last - snap_first);
  const uint16_t x1 = static_cast<uint16_t>(x + w);
  const uint16_t y1 = static_cast<uint16_t>(y + h);
  for (uint8_t i = 0; i < g_damage_count; ++i) {
    Damage& region = g_damage[i];
    if (region.mode != mode) continue;
    const bool near = (x <= region.x + region.w + kMergeGap) &&
                      (region.x <= x1 + kMergeGap) && (y <= region.y + region.h + kMergeGap) &&
                      (region.y <= y1 + kMergeGap);
    if (!near) continue;
    uint16_t merged_x0 = (x < region.x) ? x : region.x;
    uint16_t merged_y0 = (y < region.y) ? y : region.y;
    uint16_t merged_x1 = (x1 > region.x + region.w) ? x1
                                                    : static_cast<uint16_t>(region.x + region.w);
    uint16_t merged_y1 = (y1 > region.y + region.h) ? y1
                                                    : static_cast<uint16_t>(region.y + region.h);
    region.x = merged_x0;
    region.y = merged_y0;
    region.w = static_cast<uint16_t>(merged_x1 - merged_x0);
    region.h = static_cast<uint16_t>(merged_y1 - merged_y0);
    return;
  }
  if (g_damage_count == kMaxDamage) {
    merge_all(hal::UpdateMode::kGc16);
    // **A full list becomes one region that also covers this piece.** Assuming that the last piece falls
    // inside what was already pending was only true for a page drawn in one corner: an app that keeps
    // drawing somewhere else had the newest of its regions thrown away and left unpainted (found by the
    // native suite, 1/11: eight pieces at the top and one at the bottom came out as a region of 430 rows
    // instead of 510).
    Damage& all = g_damage[0];
    const uint16_t last_x1 = static_cast<uint16_t>(all.x + all.w);
    const uint16_t last_y1 = static_cast<uint16_t>(all.y + all.h);
    if (x < all.x) all.x = x;
    if (y < all.y) all.y = y;
    if (x1 > last_x1) {
      all.w = static_cast<uint16_t>(x1 - all.x);
    } else {
      all.w = static_cast<uint16_t>(last_x1 - all.x);
    }
    if (y1 > last_y1) {
      all.h = static_cast<uint16_t>(y1 - all.y);
    } else {
      all.h = static_cast<uint16_t>(last_y1 - all.y);
    }
    return;
  }
  Damage& region = g_damage[g_damage_count++];
  region.x = x;
  region.y = y;
  region.w = w;
  region.h = h;
  region.mode = mode;
}

void release_canvas() {
  if (g_canvas.bits != nullptr) pal::extra_memory().release(g_canvas.bits);
  g_canvas = Canvas{};
}

bool make_canvas(uint16_t width, uint16_t height) {
  // An odd width would leave a byte holding the last pixel of one line and the first of the next,
  // which no present can copy in one go: the view is a whole number of bytes wide (docs/50).
  width = static_cast<uint16_t>(width & ~1u);
  if (width == 0 || height == 0) return false;
  release_canvas();
  const uint16_t stride = static_cast<uint16_t>(width / 2);
  const uint32_t bytes = static_cast<uint32_t>(stride) * height;
  // The canvas goes to the extra memory (the PSRAM on the Paper): 253 KB is not something the internal
  // heap of this chip can hold next to the interpreter (docs/75).
  void* block = pal::extra_memory().allocate(bytes, true);
  if (block == nullptr) return false;
  g_canvas.width = width;
  g_canvas.height = height;
  g_canvas.stride = stride;
  g_canvas.bits = static_cast<uint8_t*>(block);
  g_canvas.bytes = bytes;
  memset(g_canvas.bits, 0xFF, bytes);  // white: level 15 in both nibbles (docs/50)
  g_damage_count = 0;
  return true;
}

// Composes one region and asks for its paint. **The translation happens here and only here**: the
// damage is in the canvas' coordinates and the glass wants panel ones, and a view that sits in a corner
// with a strip of the system next to it is the normal case (docs/30).
bool paint(const Damage& region) {
  hal::ISurface* surface = display::surface();
  if (surface == nullptr) return false;
  int32_t x = static_cast<int32_t>(g_canvas.x) + region.x;
  int32_t y = static_cast<int32_t>(g_canvas.y) + region.y;
  int32_t w = region.w;
  int32_t h = region.h;
  // What hangs off the glass is not painted. The origin is never negative, so the source pointer only
  // ever moves forward and the copy stays a copy (docs/50).
  const int32_t panel_w = static_cast<int32_t>(surface->width());
  const int32_t panel_h = static_cast<int32_t>(surface->height());
  if (x + w > panel_w) w = panel_w - x;
  if (y + h > panel_h) h = panel_h - y;
  if (w <= 0 || h <= 0) return true;  // the whole region is off the glass
  surface->blit(static_cast<uint16_t>(x), static_cast<uint16_t>(y), static_cast<uint16_t>(w),
                static_cast<uint16_t>(h),
                g_canvas.bits + static_cast<uint32_t>(region.y) * g_canvas.stride + region.x / 2,
                g_canvas.stride);
  return display::present(region.mode);
}

// ---- the interpreter's side ----

bool caps_for_lua(luabind::UIInfo& out) {
  if (!g_attached || g_canvas.bits == nullptr) return false;
  out.width = g_canvas.width;
  out.height = g_canvas.height;
  out.levels = 16;
  out.x = g_canvas.x;
  out.y = g_canvas.y;
  return true;
}

bool clear_for_lua(uint8_t level, const char* mode) {
  return clear(level, mode);
}

bool rect_for_lua(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t level, const char* mode) {
  return fill_rect(x, y, w, h, level, mode);
}

bool pixel_for_lua(uint16_t x, uint16_t y, uint8_t level, const char* mode) {
  return pixel(x, y, level, mode);
}

bool present_for_lua(const char* mode) {
  if (mode == nullptr) return present();
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (!display::mode_from_name(mode, chosen)) return false;
  return present_all(chosen);
}

bool idle_for_lua() { return idle(); }

bool resize_for_lua(uint16_t width, uint16_t height) { return resize(width, height); }

uint8_t damage_for_lua() { return damage_count(); }

uint16_t rotation_for_lua() { return rotation(); }

bool set_rotation_for_lua(uint16_t degrees) { return set_rotation(degrees); }

bool title_for_lua(const char* text) { return title(text); }

const char* last_error_for_lua() { return display::last_error(); }

}  // namespace

bool boot(hal::IPlatform& platform, uint16_t top_rows) {
  (void)platform;
  if (!display::available()) return false;
  // The view of an app is the glass **under the bar of the system**, and never the bar: a page that goes
  // wrong cannot take the hour with it (docs/30).
  if (display::height() <= top_rows) return false;
  const uint16_t usable = static_cast<uint16_t>(display::height() - top_rows);
  if (!make_canvas(display::width(), usable)) return false;
  g_canvas.y = top_rows;
  luabind::set_ui_ops(luabind::UIOps{caps_for_lua, clear_for_lua, rect_for_lua, pixel_for_lua,
                                     present_for_lua, idle_for_lua, resize_for_lua, damage_for_lua,
                                     last_error_for_lua, title_for_lua, rotation_for_lua,
                                     set_rotation_for_lua});
  // **Said once, like the panel's own line**: the canvas the system made, because an app that reports a
  // view of another size than the glass is a system that sized it wrong, and this line is where that is
  // visible without a scope (docs/30).
  printf("ui: view %ux%u at %u,%u, %u bytes of canvas\n", g_canvas.width, g_canvas.height, g_canvas.x,
         g_canvas.y, static_cast<unsigned>(g_canvas.bytes));
  return true;
}

bool available() { return g_canvas.bits != nullptr; }
const Canvas& canvas() { return g_canvas; }
uint16_t width() { return g_canvas.width; }
uint16_t height() { return g_canvas.height; }
uint8_t levels() { return 16; }

void attach() {
  if (g_canvas.bits == nullptr) return;
  g_attached = true;
  // A white view, nothing drawn and nothing pending: the app starts its own page and the glass is not
  // touched until it presents (docs/30).
  memset(g_canvas.bits, 0xFF, g_canvas.bytes);
  g_damage_count = 0;
}

bool clear(uint8_t level, const char* mode) {
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (!mode_or_default(mode, chosen)) return false;
  if (!g_attached || g_canvas.bits == nullptr) return true;
  const uint8_t level_value = clamped(level);
  memset(g_canvas.bits, static_cast<uint8_t>((level_value << 4) | level_value), g_canvas.bytes);
  damage_add(0, 0, g_canvas.width, g_canvas.height, chosen);
  return true;
}

bool fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t level, const char* mode) {
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (!mode_or_default(mode, chosen)) return false;
  if (!g_attached || g_canvas.bits == nullptr || w == 0 || h == 0) return true;
  if (x >= g_canvas.width || y >= g_canvas.height) return true;
  if (x + w > g_canvas.width) w = static_cast<uint16_t>(g_canvas.width - x);
  if (y + h > g_canvas.height) h = static_cast<uint16_t>(g_canvas.height - y);
  const uint8_t level_value = clamped(level);
  const uint8_t packed = static_cast<uint8_t>((level_value << 4) | level_value);
  // **The two ends are written a nibble at a time.** Two pixels share a byte, so a rectangle that
  // starts or ends at an odd pixel splits one, and painting the whole byte would paint the pixel next
  // to it, which belongs to whatever was drawn there before. The middle is whole bytes and goes out in
  // one `memset` (docs/50).
  const uint16_t end = static_cast<uint16_t>(x + w);
  for (uint16_t row = 0; row < h; ++row) {
    uint8_t* line = g_canvas.bits + static_cast<uint32_t>(y + row) * g_canvas.stride;
    uint16_t at = x;
    if ((at & 1) != 0) {
      line[at / 2] = static_cast<uint8_t>((line[at / 2] & 0xF0) | level_value);
      ++at;
    }
    const uint16_t whole = static_cast<uint16_t>((end - at) / 2);
    if (whole > 0) memset(line + at / 2, packed, whole);
    at = static_cast<uint16_t>(at + whole * 2);
    if (at < end) line[at / 2] = static_cast<uint8_t>((line[at / 2] & 0x0F) | (level_value << 4));
  }
  damage_add(x, y, w, h, chosen);
  return true;
}

bool pixel(uint16_t x, uint16_t y, uint8_t level, const char* mode) {
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (!mode_or_default(mode, chosen)) return false;
  if (!g_attached || g_canvas.bits == nullptr) return true;
  if (x >= g_canvas.width || y >= g_canvas.height) return true;
  const uint8_t level_value = clamped(level);
  uint8_t& byte = g_canvas.bits[static_cast<uint32_t>(y) * g_canvas.stride + x / 2];
  if ((x & 1) == 0) {
    byte = static_cast<uint8_t>((byte & 0x0F) | (level_value << 4));
  } else {
    byte = static_cast<uint8_t>((byte & 0xF0) | level_value);
  }
  damage_add(x, y, 1, 1, chosen);
  return true;
}

uint8_t damage_count() { return g_damage_count; }

const Damage& damage(uint8_t index) {
  static const Damage kNone;
  if (index >= g_damage_count) return kNone;
  return g_damage[index];
}

bool present() {
  if (!g_attached || g_canvas.bits == nullptr) return false;
  bool ok = true;
  // Each region with its own quality, in the order the app drew them: the first paint of a page and the
  // last one after it are not the same kind of thing (docs/30).
  const uint8_t count = g_damage_count;
  for (uint8_t i = 0; i < count; ++i) {
    if (!paint(g_damage[i])) ok = false;
  }
  g_damage_count = 0;
  return ok;
}

bool present_all(hal::UpdateMode mode) {
  if (!g_attached || g_canvas.bits == nullptr) return false;
  merge_all(mode);
  return present();
}

bool idle() { return display::idle(); }

bool title(const char* text) {
  bar::title(text);
  return true;
}

bool place(uint16_t x, uint16_t y) {
  if (g_canvas.bits == nullptr) return false;
  g_canvas.x = x;
  g_canvas.y = y;
  return true;
}

bool resize(uint16_t width, uint16_t height) {
  if (!g_attached) return false;
  const uint16_t at_x = g_canvas.x;
  const uint16_t at_y = g_canvas.y;
  if (!make_canvas(width, height)) return false;
  g_canvas.x = at_x;
  g_canvas.y = at_y;
  // The app is told and it is the one that draws again: the system has no idea what belongs on a view
  // it did not draw (docs/30).
  luabind::call_resize(g_canvas.width, g_canvas.height);
  return true;
}

bool set_rotation(uint16_t degrees) {
  // The glass decides: its driver keeps the image in the frame of the page and turns what it sends.
  if (!display::set_rotation(degrees)) return false;
  const uint16_t top = bar::height();
  if (display::height() <= top) return false;
  // **The page is made again**, because its two sides have swapped, and the bar is painted again at the
  // top of the new page. The app is the one that draws it: `on_resize` is how it knows (docs/30, docs/50).
  if (!make_canvas(display::width(), static_cast<uint16_t>(display::height() - top))) return false;
  g_canvas.y = top;
  g_canvas.x = 0;
  bar::refresh();
  if (g_attached) luabind::call_resize(g_canvas.width, g_canvas.height);
  return true;
}

uint16_t rotation() { return display::rotation(); }

}  // namespace ui
}  // namespace kernel
