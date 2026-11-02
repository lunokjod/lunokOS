// The canvas of the app, the damage it leaves on it and the copy that puts it on the glass
// (docs/30): see `ui.h` for what the separation is for.
#include "kernel/log.h"
#include "kernel/meter.h"
#include "kernel/ui.h"

#include <stdio.h>
#include <string.h>

#include "kernel/bar.h"
#include "kernel/display.h"
#include "kernel/font.h"
#include "kernel/fs_ops.h"
#include "kernel/vfs.h"
#include "luabind/luabind.h"
#include "pal/pal.h"
#include "ttf/ttf.h"

namespace kernel {
namespace ui {
namespace {

// **The canvases the system holds**, slot 0 is the view of the app and the rest are the controls an app
// makes inside it. A fixed table and not a list: a canvas is handed out and given back, and an app that
// needs more of them than this is an app that should compose two into one (docs/90).
Canvas g_canvases[kMaxControls];
bool g_attached = false;
// The number the next canvas takes, so an object of an app that points at a slot that was given back
// never matches the one that took its place (docs/90).
uint16_t g_generation = 0;

// The faces an app loaded are given back when the app ends; the table is at the bottom of this file, with
// the rest of the font work (docs/30).
void release_faces();
// The face a handle means, which is the font of the system when the handle is -1 or was given back.
const font::Face& face_of(int8_t handle);

// **What the page has shown since it was last cleaned**: which of the sixteen levels were painted, and how
// many boxes of pixels were put down. It is what the system reads to choose the cleaner between two apps: a
// page of text does not need the cleaner a photograph needs, and a page that has taken many quick paints
// needs a full one even if it is only text (docs/50, docs/90).
uint16_t g_levels = 0;
uint16_t g_paints = 0;

void note_level(uint8_t level) {
  if (level > 15) return;
  g_levels = static_cast<uint16_t>(g_levels | (1u << level));
  ++g_paints;
}

Canvas& root() { return g_canvases[0]; }

// A slot is a control when it is in use, **with a buffer or without one**: a grid or a box is a control
// that paints nothing and still has a frame, children and a draw (docs/90).
bool live(int8_t slot) {
  return slot >= 0 && slot < static_cast<int8_t>(kMaxControls) && g_canvases[slot].in_use;
}

// The first canvas that holds this control: the nearest father —or the control itself— that has a buffer.
// It is where the pixels of a control without a view go (docs/90).
int8_t painting_holder(int8_t slot) {
  for (int8_t walk = slot; walk >= 0; walk = g_canvases[walk].parent) {
    if (g_canvases[walk].bits != nullptr) return walk;
  }
  return -1;
}

Canvas* find(int8_t slot) { return live(slot) ? &g_canvases[slot] : nullptr; }

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

void merge_all(Canvas& canvas, hal::UpdateMode mode) {
  if (canvas.damage_count == 0) return;
  uint16_t x0 = canvas.damage[0].x;
  uint16_t y0 = canvas.damage[0].y;
  uint16_t x1 = static_cast<uint16_t>(canvas.damage[0].x + canvas.damage[0].w);
  uint16_t y1 = static_cast<uint16_t>(canvas.damage[0].y + canvas.damage[0].h);
  for (uint8_t i = 1; i < canvas.damage_count; ++i) {
    const Damage& region = canvas.damage[i];
    if (region.x < x0) x0 = region.x;
    if (region.y < y0) y0 = region.y;
    if (region.x + region.w > x1) x1 = static_cast<uint16_t>(region.x + region.w);
    if (region.y + region.h > y1) y1 = static_cast<uint16_t>(region.y + region.h);
  }
  canvas.damage[0].x = x0;
  canvas.damage[0].y = y0;
  canvas.damage[0].w = static_cast<uint16_t>(x1 - x0);
  canvas.damage[0].h = static_cast<uint16_t>(y1 - y0);
  canvas.damage[0].mode = mode;
  canvas.damage_count = 1;
}

// Records one region. Same quality and close by: one paint. The whole list full: one paint with the
// quality that cannot look wrong, because an app that draws more pieces than the list holds is an app
// that is redrawing a page and a page wants `gc16` (docs/30).
void damage_add(Canvas& canvas, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                hal::UpdateMode mode) {
  if (canvas.bits == nullptr || w == 0 || h == 0) return;
  if (x >= canvas.width || y >= canvas.height) return;
  if (x + w > canvas.width) w = static_cast<uint16_t>(canvas.width - x);
  if (y + h > canvas.height) h = static_cast<uint16_t>(canvas.height - y);
  // **The region is kept in whole nibbles.** Two pixels share a byte, so a region that starts at an odd
  // x carries its left neighbour: the copy is then byte for byte instead of pixel by pixel, and the
  // neighbour is the canvas' own pixel, which is the truth of the view anyway (docs/50).
  const uint16_t snap_first = static_cast<uint16_t>(x & ~1u);
  uint16_t snap_last = static_cast<uint16_t>((x + w + 1) & ~1u);
  if (snap_last > canvas.width) snap_last = canvas.width;
  x = snap_first;
  w = static_cast<uint16_t>(snap_last - snap_first);
  const uint16_t x1 = static_cast<uint16_t>(x + w);
  const uint16_t y1 = static_cast<uint16_t>(y + h);
  for (uint8_t i = 0; i < canvas.damage_count; ++i) {
    Damage& region = canvas.damage[i];
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
  if (canvas.damage_count == kMaxDamage) {
    merge_all(canvas, hal::UpdateMode::kGc16);
    // **A full list becomes one region that also covers this piece.** Assuming that the last piece falls
    // inside what was already pending was only true for a page drawn in one corner: an app that keeps
    // drawing somewhere else had the newest of its regions thrown away and left unpainted (found by the
    // native suite, 1/11: eight pieces at the top and one at the bottom came out as a region of 430 rows
    // instead of 510).
    Damage& all = canvas.damage[0];
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
  Damage& region = canvas.damage[canvas.damage_count++];
  region.x = x;
  region.y = y;
  region.w = w;
  region.h = h;
  region.mode = mode;
}

// Takes a canvas out of the list of children of its father, if it has one and it is there.
void unlink_from_parent(int8_t slot) {
  const int8_t father = g_canvases[slot].parent;
  if (father < 0) return;
  Canvas& parent = g_canvases[father];
  uint8_t kept = 0;
  for (uint8_t i = 0; i < parent.child_count; ++i) {
    if (parent.children[i] == slot) continue;
    parent.children[kept++] = parent.children[i];
  }
  parent.child_count = kept;
}

// **Asks for a control to go away, and does not free it yet.** The rectangle it occupies is owed to the
// first control that paints —so the hole is painted over on the next draw— and every child goes with it.
// The buffer is wiped and the slot given back at the end of the next present, when the page is already
// clean (docs/90).
void request_release(int8_t slot, bool top) {
  if (!live(slot)) return;
  Canvas& control = g_canvases[slot];
  if (control.pending_release) return;
  control.pending_release = true;
  // **Only the top of the subtree is owed to the painter.** A father that goes away is a rectangle that
  // has to be painted over once, and everything inside it goes with it: making each child say its own
  // rectangle would fill the list of eight with pieces of the same hole and end up in one `gc16` of the
  // whole union, which is the opposite of cheap (docs/90).
  if (top) {
    const int8_t painter = painting_holder(control.parent);
    if (painter >= 0) {
      int32_t offset_x = 0;
      int32_t offset_y = 0;
      for (int8_t walk = slot; walk != painter && walk >= 0; walk = g_canvases[walk].parent) {
        offset_x += g_canvases[walk].x;
        offset_y += g_canvases[walk].y;
      }
      invalidate_rect(painter, static_cast<uint16_t>(offset_x), static_cast<uint16_t>(offset_y),
                      control.width, control.height);
    }
  }
  // The children go with it, marked, without a rectangle of their own: theirs are all inside this one.
  int8_t inside[kMaxControls];
  const uint8_t count = control.child_count;
  for (uint8_t i = 0; i < count; ++i) inside[i] = control.children[i];
  for (uint8_t i = 0; i < count; ++i) request_release(inside[i], false);
}

// Gives one canvas back, and everything that was inside it: a child whose parent is gone could never be
// composed and would keep its buffer for nothing (docs/90). **The buffer is wiped white first**, so that
// nothing of this control can be composed out of a block somebody else is about to get.
void release_slot(int8_t slot) {
  if (!live(slot)) return;
  // A copy of the list, because releasing a child changes the list it came from.
  int8_t inside[kMaxControls];
  uint8_t count = g_canvases[slot].child_count;
  for (uint8_t i = 0; i < count; ++i) inside[i] = g_canvases[slot].children[i];
  for (uint8_t i = 0; i < count; ++i) release_slot(inside[i]);
  unlink_from_parent(slot);
  if (g_canvases[slot].bits != nullptr) {
    memset(g_canvases[slot].bits, 0xFF, g_canvases[slot].bytes);
    {
      meter::Scope scope(meter::kExtraRelease);
      pal::extra_memory().release(g_canvases[slot].bits);
    }
  }
  g_canvases[slot] = Canvas{};
}

// A canvas becomes the **last** child of another one: the layers are the order of the list, and the last
// one added is the one in front (docs/90).
bool attach_to(int8_t slot, int8_t parent) {
  if (!live(slot) || slot == 0 || !live(parent) || slot == parent) return false;
  // A control cannot end up inside itself: the walk would never end.
  for (int8_t walk = parent; walk >= 0; walk = g_canvases[walk].parent) {
    if (walk == slot) return false;
  }
  unlink_from_parent(slot);
  Canvas& father = g_canvases[parent];
  if (father.child_count >= kMaxControls) return false;
  father.children[father.child_count++] = slot;
  g_canvases[slot].parent = parent;
  return true;
}

// The view of the app: a fresh, white canvas at the place the system gives it, and **no canvases inside
// it**, because the app that had them is gone (docs/30, docs/90).
bool make_root(uint16_t width, uint16_t height) {
  // An odd width would leave a byte holding the last pixel of one line and the first of the next,
  // which no present can copy in one go: a canvas is a whole number of bytes wide (docs/50).
  width = static_cast<uint16_t>(width & ~1u);
  if (width == 0 || height == 0) return false;
  const uint16_t stride = static_cast<uint16_t>(width / 2);
  const uint32_t bytes = static_cast<uint32_t>(stride) * height;
  // The canvas goes to the extra memory (the PSRAM on the Paper): 251 640 B is not something the
  // internal heap of this chip can hold next to the interpreter (docs/75).
  //
  // **The new view is taken before the old one is given back**: if there is no memory for it, the system
  // keeps the view it has and the caller can put the glass back where it was —a brand new size with no
  // view is the one state `set_rotation` says must never happen (docs/50, docs/90).
  void* block = pal::extra_memory().allocate(bytes, true);
  if (block == nullptr) return false;
  for (uint8_t i = 1; i < kMaxControls; ++i) release_slot(static_cast<int8_t>(i));
  const uint16_t at_x = root().x;
  const uint16_t at_y = root().y;
  release_slot(0);
  Canvas& view = root();
  // **The view is a control like the others and it is marked as one**: without this line `live(0)` is false
  // and everything that hangs off the view —every canvas, every place, every redraw— answers no.
  view.in_use = true;
  view.width = width;
  view.height = height;
  view.stride = stride;
  view.bits = static_cast<uint8_t*>(block);
  view.bytes = bytes;
  view.x = at_x;
  view.y = at_y;
  view.parent = -1;
  view.generation = ++g_generation;
  memset(view.bits, 0xFF, bytes);  // white: level 15 in both nibbles (docs/50)
  return true;
}

// Composes one region and asks for its paint. **The translation happens here and only here**: the
// damage is in the canvas' coordinates and the glass wants panel ones, and a view that sits in a corner
// with a strip of the system next to it is the normal case (docs/30).
bool paint(const Damage& region) {
  hal::ISurface* surface = display::surface();
  if (surface == nullptr) return false;
  const Canvas& view = root();
  int32_t x = static_cast<int32_t>(view.x) + region.x;
  int32_t y = static_cast<int32_t>(view.y) + region.y;
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
                view.bits + static_cast<uint32_t>(region.y) * view.stride + region.x / 2,
                view.stride);
  return display::present(region.mode);
}

// **One canvas onto the one that holds it.** A region the child painted is copied at the child's place,
// **cut at the border of the parent**, and the parent takes it as its own damage with the quality the
// child asked for: the quality belongs to the ink and not to the canvas it travels through, so what
// reaches the glass is what the control said (docs/30, docs/90).
void compose_region(Canvas& parent, const Canvas& child, int32_t origin_x, int32_t origin_y,
                    const Damage& region) {
  if (parent.bits == nullptr || child.bits == nullptr || region.w == 0 || region.h == 0) return;
  if (origin_x < 0 || origin_y < 0) return;
  const uint16_t at_x = static_cast<uint16_t>(origin_x + region.x);
  const uint16_t at_y = static_cast<uint16_t>(origin_y + region.y);
  if (at_x >= parent.width || at_y >= parent.height) return;
  uint16_t w = region.w;
  uint16_t h = region.h;
  if (at_x + w > parent.width) w = static_cast<uint16_t>(parent.width - at_x);
  if (at_y + h > parent.height) h = static_cast<uint16_t>(parent.height - at_y);
  if (w == 0 || h == 0) return;
  const uint8_t* src = child.bits + static_cast<uint32_t>(region.y) * child.stride + region.x / 2;
  if ((at_x & 1) != 0) {
    // An odd column on the parent puts the two pixels of a source byte in two different bytes there, so
    // this goes pixel by pixel and the source is read by its own column (docs/50).
    for (uint16_t row = 0; row < h; ++row) {
      const uint8_t* line = src + static_cast<uint32_t>(row) * child.stride;
      uint8_t* to = parent.bits + static_cast<uint32_t>(at_y + row) * parent.stride;
      for (uint16_t column = 0; column < w; ++column) {
        const uint8_t byte = line[column / 2];
        const uint8_t level = ((column & 1) == 0) ? (byte >> 4) : (byte & 0x0F);
        const uint16_t x = static_cast<uint16_t>(at_x + column);
        if ((x & 1) == 0) {
          to[x / 2] = static_cast<uint8_t>((to[x / 2] & 0x0F) | (level << 4));
        } else {
          to[x / 2] = static_cast<uint8_t>((to[x / 2] & 0xF0) | level);
        }
      }
    }
  } else {
    // Byte for byte: the packing of a canvas is the packing of the one that holds it.
    const uint16_t bytes = static_cast<uint16_t>(w / 2);
    for (uint16_t row = 0; row < h; ++row) {
      memcpy(parent.bits + static_cast<uint32_t>(at_y + row) * parent.stride + at_x / 2,
             src + static_cast<uint32_t>(row) * child.stride, bytes);
    }
  }
  damage_add(parent, at_x, at_y, w, h, region.mode);
}

// **The tree, and the controls without a view are not a hole in it.** Depth first, in the order the
// controls were added —the one added later is in front—, and each view is composed into the first canvas
// that holds it: the offset is what every control between the two adds with its place, so a label inside a
// box inside a panel lands where the three of them say (docs/90).
void compose_tree(int8_t slot, int8_t destination, int32_t origin_x, int32_t origin_y) {
  Canvas& control = g_canvases[slot];
  // **The children first.** A father's buffer has to be complete before it travels up: composing him into
  // his own father and *then* composing his children into him was a father that reached the glass without
  // what his children had just put in him (found by this suite, 1/11).
  const int8_t theirs = (control.bits != nullptr) ? slot : destination;
  const int32_t base_x = (control.bits != nullptr) ? 0 : origin_x;
  const int32_t base_y = (control.bits != nullptr) ? 0 : origin_y;
  for (uint8_t i = 0; i < control.child_count; ++i) {
    const int8_t index = control.children[i];
    // A control on its way out is **not composed**: its pixels are what the painter has just painted
    // over, and composing them again would put the ghost back on the page (docs/90).
    if (!live(index) || g_canvases[index].pending_release) continue;
    compose_tree(index, theirs, base_x + g_canvases[index].x, base_y + g_canvases[index].y);
  }
  // And then this control's own pixels, which now already have his children in them.
  if (control.bits != nullptr && destination >= 0) {
    Canvas& into = g_canvases[destination];
    for (uint8_t d = 0; d < control.damage_count; ++d) {
      compose_region(into, control, origin_x, origin_y, control.damage[d]);
    }
    control.damage_count = 0;
  }
}

// **What has been painted is paid.** The box of a control exists to cut one draw, and once the page has
// been painted there is nothing left to cut: a box that outlived its paint would be unioned into the next
// reason and the view would be painted again over a part that is already clean (found by this suite,
// 1/11 — the rectangle a `place` left behind came back inside the rectangle of a `release`).
void settle_root() {
  // A present from inside `on_draw` does not end the draw: the rest of the callback is still cut.
  if (root().in_draw) return;
  root().needs_draw = false;
  root().redraw_x = 0;
  root().redraw_y = 0;
  root().redraw_w = 0;
  root().redraw_h = 0;
}

// What is waiting in the view, painted with the quality each region was drawn with.
bool paint_root() {
  bool ok = true;
  // Each region with its own quality, in the order the app drew them: the first paint of a page and the
  // last one after it are not the same kind of thing (docs/30).
  const uint8_t count = root().damage_count;
  for (uint8_t i = 0; i < count; ++i) {
    if (!paint(root().damage[i])) ok = false;
  }
  root().damage_count = 0;
  settle_root();
  return ok;
}

// ---- the primitives ----
//
// **A shape is drawn pixel by pixel and marked once.** Every put below writes into the canvas and says
// nothing about damage; the primitive adds one region at the end, the box the shape covers. Marking each
// pixel would be two hundred regions and, past the eight the list holds, one `gc16` paint of the whole
// canvas —which is the opposite of what drawing a line should cost (docs/30, docs/50).

// **While a control draws itself because something changed, it may only paint what changed.** The box is
// what it owes, and a primitive that falls outside is cut there: the smallest part of the glass, in the
// shortest time, which is the whole point of the damage and of its quality (docs/30, docs/90).
bool clip_to_redraw(Canvas& canvas, int32_t& x, int32_t& y, int32_t& w, int32_t& h) {
  // **The frame of the control that is drawing**, when it is a control with no view of its own.
  if (canvas.clipped) {
    if (x < canvas.clip_x) {
      w -= canvas.clip_x - x;
      x = canvas.clip_x;
    }
    if (y < canvas.clip_y) {
      h -= canvas.clip_y - y;
      y = canvas.clip_y;
    }
    if (x + w > canvas.clip_x1) w = canvas.clip_x1 - x;
    if (y + h > canvas.clip_y1) h = canvas.clip_y1 - y;
    if (w <= 0 || h <= 0) return false;
  }
  if (!canvas.in_draw) return true;
  const int32_t right = canvas.redraw_x + canvas.redraw_w;
  const int32_t bottom = canvas.redraw_y + canvas.redraw_h;
  if (x < canvas.redraw_x) {
    w -= canvas.redraw_x - x;
    x = canvas.redraw_x;
  }
  if (y < canvas.redraw_y) {
    h -= canvas.redraw_y - y;
    y = canvas.redraw_y;
  }
  if (x + w > right) w = right - x;
  if (y + h > bottom) h = bottom - y;
  return w > 0 && h > 0;
}

// One pixel, clipped at the border of the canvas and at what it owes.
void put_pixel(Canvas& canvas, int32_t x, int32_t y, uint8_t level) {
  int32_t w = 1;
  int32_t h = 1;
  if (!clip_to_redraw(canvas, x, y, w, h)) return;
  if (x < 0 || y < 0 || x >= canvas.width || y >= canvas.height) return;
  note_level(level);
  uint8_t& byte = canvas.bits[static_cast<uint32_t>(y) * canvas.stride + static_cast<uint32_t>(x) / 2];
  if ((x & 1) == 0) {
    byte = static_cast<uint8_t>((byte & 0x0F) | (level << 4));
  } else {
    byte = static_cast<uint8_t>((byte & 0xF0) | level);
  }
}

// A box of one colour, **the two ends a nibble at a time**, clipped at the border of the canvas.
void put_rect(Canvas& canvas, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t level) {
  if (w <= 0 || h <= 0 || level > 15) return;
  if (!clip_to_redraw(canvas, x, y, w, h)) return;
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > canvas.width) w = canvas.width - x;
  if (y + h > canvas.height) h = canvas.height - y;
  if (w <= 0 || h <= 0) return;
  note_level(level);
  const uint8_t packed = static_cast<uint8_t>((level << 4) | level);
  const int32_t end = x + w;
  for (int32_t row = 0; row < h; ++row) {
    uint8_t* line = canvas.bits + static_cast<uint32_t>(y + row) * canvas.stride;
    int32_t at = x;
    if ((at & 1) != 0) {
      line[at / 2] = static_cast<uint8_t>((line[at / 2] & 0xF0) | level);
      ++at;
    }
    const int32_t whole = (end - at) / 2;
    if (whole > 0) memset(line + at / 2, packed, static_cast<size_t>(whole));
    at += whole * 2;
    if (at < end) line[at / 2] = static_cast<uint8_t>((line[at / 2] & 0x0F) | (level << 4));
  }
}

// The box a shape covers, cut to the canvas, marked as one region.
void mark_box(Canvas& canvas, int32_t x, int32_t y, int32_t w, int32_t h, hal::UpdateMode mode) {
  if (w <= 0 || h <= 0) return;
  if (!clip_to_redraw(canvas, x, y, w, h)) return;
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x >= canvas.width || y >= canvas.height) return;
  if (x + w > canvas.width) w = canvas.width - x;
  if (y + h > canvas.height) h = canvas.height - y;
  if (w <= 0 || h <= 0) return;
  damage_add(canvas, static_cast<uint16_t>(x), static_cast<uint16_t>(y), static_cast<uint16_t>(w),
             static_cast<uint16_t>(h), mode);
}

void draw_frame(Canvas& canvas, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t level,
                hal::UpdateMode mode) {
  if (w <= 0 || h <= 0) return;
  put_rect(canvas, x, y, w, 1, level);              // the top
  put_rect(canvas, x, y + h - 1, w, 1, level);      // the bottom
  put_rect(canvas, x, y, 1, h, level);              // the left
  put_rect(canvas, x + w - 1, y, 1, h, level);      // the right
  mark_box(canvas, x, y, w, h, mode);
}

// **A line that is straight up, down, left or right is a rectangle**, and a rectangle is exact; the rest
// is Bresenham, which is the same line the panel would draw and no floating point (docs/50).
void draw_line(Canvas& canvas, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t level,
               hal::UpdateMode mode) {
  const int32_t left = (x0 < x1) ? x0 : x1;
  const int32_t top = (y0 < y1) ? y0 : y1;
  const int32_t box_w = ((x0 < x1) ? x1 - x0 : x0 - x1) + 1;
  const int32_t box_h = ((y0 < y1) ? y1 - y0 : y0 - y1) + 1;
  if (x0 == x1 || y0 == y1) {
    put_rect(canvas, left, top, box_w, box_h, level);
    mark_box(canvas, left, top, box_w, box_h, mode);
    return;
  }
  int32_t dx = (x1 > x0) ? x1 - x0 : x0 - x1;
  int32_t dy = (y1 > y0) ? y0 - y1 : y1 - y0;
  int32_t sx = (x0 < x1) ? 1 : -1;
  int32_t sy = (y0 < y1) ? 1 : -1;
  int32_t err = dx + dy;
  for (;;) {
    put_pixel(canvas, x0, y0, level);
    if (x0 == x1 && y0 == y1) break;
    const int32_t twice = 2 * err;
    if (twice >= dy) {
      err += dy;
      x0 += sx;
    }
    if (twice <= dx) {
      err += dx;
      y0 += sy;
    }
  }
  mark_box(canvas, left, top, box_w, box_h, mode);
}

// The midpoint circle, eight points at a time: no floating point and no table (docs/50).
void draw_circle(Canvas& canvas, int32_t cx, int32_t cy, int32_t radius, uint8_t level,
                 hal::UpdateMode mode) {
  if (radius <= 0) {
    put_pixel(canvas, cx, cy, level);
    mark_box(canvas, cx, cy, 1, 1, mode);
    return;
  }
  int32_t x = radius;
  int32_t y = 0;
  int32_t err = 1 - radius;
  while (x >= y) {
    put_pixel(canvas, cx + x, cy + y, level);
    put_pixel(canvas, cx + y, cy + x, level);
    put_pixel(canvas, cx - y, cy + x, level);
    put_pixel(canvas, cx - x, cy + y, level);
    put_pixel(canvas, cx - x, cy - y, level);
    put_pixel(canvas, cx - y, cy - x, level);
    put_pixel(canvas, cx + y, cy - x, level);
    put_pixel(canvas, cx + x, cy - y, level);
    ++y;
    if (err < 0) {
      err += 2 * y + 1;
    } else {
      --x;
      err += 2 * (y - x) + 1;
    }
  }
  mark_box(canvas, cx - radius, cy - radius, 2 * radius + 1, 2 * radius + 1, mode);
}

// The same circle with its inside: each step of the same walk is a row of the disc (docs/50).
void draw_disc(Canvas& canvas, int32_t cx, int32_t cy, int32_t radius, uint8_t level,
               hal::UpdateMode mode) {
  if (radius < 0) return;
  int32_t x = radius;
  int32_t y = 0;
  int32_t err = 1 - radius;
  while (x >= y) {
    put_rect(canvas, cx - x, cy + y, 2 * x + 1, 1, level);
    put_rect(canvas, cx - x, cy - y, 2 * x + 1, 1, level);
    put_rect(canvas, cx - y, cy + x, 2 * y + 1, 1, level);
    put_rect(canvas, cx - y, cy - x, 2 * y + 1, 1, level);
    ++y;
    if (err < 0) {
      err += 2 * y + 1;
    } else {
      --x;
      err += 2 * (y - x) + 1;
    }
  }
  mark_box(canvas, cx - radius, cy - radius, 2 * radius + 1, 2 * radius + 1, mode);
}

// The sink of a canvas: a run of glyph is one rectangle in its buffer, and the whole text is one region
// of damage (docs/30).
struct CanvasSink {
  Canvas* canvas;
  uint8_t level;
};

void canvas_run(void* user, int32_t x, int32_t y, int32_t w, int32_t h) {
  CanvasSink* sink = static_cast<CanvasSink*>(user);
  put_rect(*sink->canvas, x, y, w, h, sink->level);
}

void draw_text(Canvas& canvas, const font::Face& face, int32_t x, int32_t y, const char* text,
               uint8_t scale, uint8_t level, hal::UpdateMode mode) {
  if (text == nullptr || scale == 0) return;
  font::Clip box;
  box.x0 = 0;
  box.y0 = 0;
  box.x1 = canvas.width;
  box.y1 = canvas.height;
  if (canvas.in_draw) {
    // A text is cut by its canvas and by what the control owes: a letter half outside the box loses the
    // part that is outside, exactly like a rectangle (docs/30).
    if (canvas.redraw_x > box.x0) box.x0 = canvas.redraw_x;
    if (canvas.redraw_y > box.y0) box.y0 = canvas.redraw_y;
    if (canvas.redraw_x + canvas.redraw_w < box.x1) box.x1 = canvas.redraw_x + canvas.redraw_w;
    if (canvas.redraw_y + canvas.redraw_h < box.y1) box.y1 = canvas.redraw_y + canvas.redraw_h;
  }
  CanvasSink sink = {&canvas, level};
  font::runs(face, text, x, y, scale, box, canvas_run, &sink);
  // What a text covers is its box, cut at the canvas: the runs are many and the damage is one. **The box
  // is measured with the face that was drawn**, which is not the system's when the canvas loaded one
  // (docs/30, docs/90).
  const int32_t text_w = font::width(face, text, scale);
  const int32_t line_h = static_cast<int32_t>((face.height != 0) ? face.height : font::kGlyphHeight);
  mark_box(canvas, x, y, text_w, line_h * scale, mode);
}

// ---- where a control's pixels go (docs/90) ----

// **A control with a buffer draws on it; a control without one draws on the first canvas that holds it**,
// with its own origin translated and with the clip at its frame. This is the whole difference between a
// `UICanvas` and a grid: the pixels, not the behaviour.
struct DrawTarget {
  Canvas* canvas = nullptr;
  int32_t x = 0;
  int32_t y = 0;
};

DrawTarget open_draw(int8_t slot) {
  DrawTarget out;
  if (!live(slot)) return out;
  const int8_t holder = painting_holder(slot);
  if (holder < 0) return out;
  Canvas& canvas = g_canvases[holder];
  out.canvas = &canvas;
  // The clip: the frame of the control, and of every control between it and the canvas that holds it,
  // **each one in the canvas' own pixels** —which is why the origin of each is the sum of all the places
  // above it, and not added one at a time (found by this suite, 1/11).
  int32_t offset_x = 0;
  int32_t offset_y = 0;
  for (int8_t walk = slot; walk != holder; walk = g_canvases[walk].parent) {
    offset_x += g_canvases[walk].x;
    offset_y += g_canvases[walk].y;
  }
  out.x = offset_x;
  out.y = offset_y;
  int32_t x0 = 0;
  int32_t y0 = 0;
  int32_t x1 = canvas.width;
  int32_t y1 = canvas.height;
  int32_t at_x = offset_x;
  int32_t at_y = offset_y;
  for (int8_t walk = slot; walk != holder; walk = g_canvases[walk].parent) {
    const Canvas& control = g_canvases[walk];
    if (at_x > x0) x0 = at_x;
    if (at_y > y0) y0 = at_y;
    if (at_x + control.width < x1) x1 = at_x + control.width;
    if (at_y + control.height < y1) y1 = at_y + control.height;
    at_x -= control.x;
    at_y -= control.y;
  }
  if (holder != slot) {
    canvas.clipped = true;
    canvas.clip_x = x0;
    canvas.clip_y = y0;
    canvas.clip_x1 = x1;
    canvas.clip_y1 = y1;
  }
  return out;
}

void close_draw(const DrawTarget& target) {
  if (target.canvas != nullptr) target.canvas->clipped = false;
}

// ---- drawing into a canvas, whoever it belongs to ----

bool draw_clear(Canvas& canvas, uint8_t level, hal::UpdateMode mode) {
  if (canvas.bits == nullptr) return false;
  const uint8_t level_value = clamped(level);
  memset(canvas.bits, static_cast<uint8_t>((level_value << 4) | level_value), canvas.bytes);
  damage_add(canvas, 0, 0, canvas.width, canvas.height, mode);
  return true;
}

bool draw_fill_rect(Canvas& canvas, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t level,
                    hal::UpdateMode mode) {
  if (canvas.bits == nullptr || w == 0 || h == 0) return false;
  int32_t cx = x;
  int32_t cy = y;
  int32_t cw = w;
  int32_t ch = h;
  if (!clip_to_redraw(canvas, cx, cy, cw, ch)) return true;
  x = static_cast<uint16_t>(cx);
  y = static_cast<uint16_t>(cy);
  w = static_cast<uint16_t>(cw);
  h = static_cast<uint16_t>(ch);
  if (x >= canvas.width || y >= canvas.height) return false;
  if (x + w > canvas.width) w = static_cast<uint16_t>(canvas.width - x);
  if (y + h > canvas.height) h = static_cast<uint16_t>(canvas.height - y);
  const uint8_t level_value = clamped(level);
  const uint8_t packed = static_cast<uint8_t>((level_value << 4) | level_value);
  // **The two ends are written a nibble at a time.** Two pixels share a byte, so a rectangle that
  // starts or ends at an odd pixel splits one, and painting the whole byte would paint the pixel next
  // to it, which belongs to whatever was drawn there before. The middle is whole bytes and goes out in
  // one `memset` (docs/50).
  const uint16_t end = static_cast<uint16_t>(x + w);
  for (uint16_t row = 0; row < h; ++row) {
    uint8_t* line = canvas.bits + static_cast<uint32_t>(y + row) * canvas.stride;
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
  damage_add(canvas, x, y, w, h, mode);
  return true;
}

bool draw_pixel(Canvas& canvas, uint16_t x, uint16_t y, uint8_t level, hal::UpdateMode mode) {
  if (canvas.bits == nullptr) return false;
  if (x >= canvas.width || y >= canvas.height) return false;
  const uint8_t level_value = clamped(level);
  uint8_t& byte = canvas.bits[static_cast<uint32_t>(y) * canvas.stride + x / 2];
  if ((x & 1) == 0) {
    byte = static_cast<uint8_t>((byte & 0x0F) | (level_value << 4));
  } else {
    byte = static_cast<uint8_t>((byte & 0xF0) | level_value);
  }
  damage_add(canvas, x, y, 1, 1, mode);
  return true;
}

// ---- the interpreter's side ----

bool caps_for_lua(luabind::UIInfo& out) {
  const Canvas& view = root();
  if (!g_attached || view.bits == nullptr) return false;
  out.width = view.width;
  out.height = view.height;
  out.levels = 16;
  out.x = view.x;
  out.y = view.y;
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

bool canvas_create_for_lua(uint16_t width, uint16_t height, bool with_view, int8_t& slot,
                           uint16_t& generation) {
  generation = 0;
  if (!g_attached) return false;
  if (!create(width, height, 0, with_view, slot)) return false;
  generation = g_canvases[slot].generation;
  return true;
}

bool canvas_alive_for_lua(int8_t slot, uint16_t generation) {
  const Canvas* target = find(slot);
  return target != nullptr && target->generation == generation;
}

// Two wrappers and not the public names: `place` is also the move of the view, and a pointer taken from an
// overloaded name is a thing the next reader has to work out (docs/30).
bool canvas_place_for_lua(int8_t slot, uint16_t x, uint16_t y) { return place(slot, x, y); }

bool canvas_release_for_lua(int8_t slot) { return release(slot); }

bool canvas_caps_for_lua(int8_t slot, uint16_t& width, uint16_t& height, uint16_t& x, uint16_t& y) {
  const Canvas* target = find(slot);
  if (target == nullptr) return false;
  width = target->width;
  height = target->height;
  x = target->x;
  y = target->y;
  return true;
}

}  // namespace

bool boot(hal::IPlatform& platform, uint16_t top_rows) {
  (void)platform;
  if (!display::available()) return false;
  // The view of an app is the glass **under the bar of the system**, and never the bar: a page that goes
  // wrong cannot take the hour with it (docs/30).
  if (display::height() <= top_rows) return false;
  const uint16_t usable = static_cast<uint16_t>(display::height() - top_rows);
  if (!make_root(display::width(), usable)) return false;
  root().y = top_rows;
  luabind::set_ui_ops(luabind::UIOps{caps_for_lua,
                                     clear_for_lua,
                                     rect_for_lua,
                                     pixel_for_lua,
                                     present_for_lua,
                                     idle_for_lua,
                                     resize_for_lua,
                                     damage_for_lua,
                                     last_error_for_lua,
                                     title_for_lua,
                                     rotation_for_lua,
                                     set_rotation_for_lua,
                                     canvas_create_for_lua,
                                     canvas_alive_for_lua,
                                     canvas_release_for_lua,
                                     canvas_place_for_lua,
                                     canvas_clear,
                                     canvas_fill_rect,
                                     canvas_pixel,
                                     canvas_damage,
                                     canvas_caps_for_lua,
                                     canvas_frame,
                                     canvas_line,
                                     canvas_circle,
                                     canvas_disc,
                                     canvas_text,
                                     reparent,
                                     parent_of,
                                     needs_draw,
                                     mark_drawn,
                                     invalidate,
                                     generation_of,
                                     invalidate_rect,
                                     begin_draw,
                                     end_draw,
                                     font_load,
                                     font_release,
                                     font_info,
                                     canvas_set_font,
                                     app_set_font,
                                     font_text_width,
                                     label_draw,
                                     button_draw});
  // **Said once, like the panel's own line**: the canvas the system made, because an app that reports a
  // view of another size than the glass is a system that sized it wrong, and this line is where that is
  // visible without a scope (docs/30).
  log_printf(pal::kLogInfo, "UI", "view %ux%u at %u,%u, %u bytes of canvas", root().width,
             root().height, root().x, root().y, static_cast<unsigned>(root().bytes));
  return true;
}

bool available() { return root().bits != nullptr; }
const Canvas& canvas() { return root(); }
uint16_t width() { return root().width; }
uint16_t height() { return root().height; }
uint8_t levels() { return 16; }

void attach() {
  if (root().bits == nullptr) return;
  g_attached = true;
  // A white view, nothing drawn and nothing pending, and **no controls inside it**: the app that had
  // them is gone (docs/30, docs/90). The glass is not touched until the app presents.
  for (uint8_t i = 1; i < kMaxControls; ++i) release_slot(static_cast<int8_t>(i));
  // **And the faces it loaded go back too**: the block of an app is the app's, and an app that ends with a
  // font loaded would leave a kilobyte of the extra memory spoken for by nobody (docs/30, docs/75).
  release_faces();
  memset(root().bits, 0xFF, root().bytes);
  root().damage_count = 0;
  // **And the glass is left clean, with the cleaner the last page deserves** (0.37.0): the app that was
  // here painted something and the next one starts on white paper, and what that wash costs is the
  // system's to choose —`a2` for a page of text, `gl16` for a few grays, `gc16` for a photograph— and not
  // the app's, because no app knows what the one before it left (docs/50, docs/90). The paint is asked for
  // and not waited on: the next app's own paint queues behind it.
  if (g_levels != 0) {
    damage_add(root(), 0, 0, root().width, root().height, cleaner_for(g_levels, g_paints));
    paint_root();
  }
  g_levels = 0;
  g_paints = 0;
  // **A view that has never been drawn owes a draw**, like a canvas that has just been made: the app that
  // is starting is the one that has to fill it (docs/90).
  root().needs_draw = true;
  root().redraw_w = 0;
  root().redraw_h = 0;
}

bool clear(uint8_t level, const char* mode) {
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (!mode_or_default(mode, chosen)) return false;
  if (!g_attached) return true;
  draw_clear(root(), level, chosen);
  return true;
}

bool fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t level, const char* mode) {
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (!mode_or_default(mode, chosen)) return false;
  if (!g_attached) return true;
  draw_fill_rect(root(), x, y, w, h, level, chosen);
  return true;
}

bool pixel(uint16_t x, uint16_t y, uint8_t level, const char* mode) {
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (!mode_or_default(mode, chosen)) return false;
  if (!g_attached) return true;
  draw_pixel(root(), x, y, level, chosen);
  return true;
}

uint8_t damage_count() { return root().damage_count; }

const Damage& damage(uint8_t index) {
  static const Damage kNone;
  if (index >= root().damage_count) return kNone;
  return root().damage[index];
}

const Canvas* at(int8_t slot) { return find(slot); }

bool create(uint16_t width, uint16_t height, int8_t parent, bool with_view, int8_t& slot) {
  slot = -1;
  const int8_t holder = (parent < 0) ? 0 : parent;
  if (!live(holder)) return false;
  // **A control with no room in its father is not made**: it would hold a frame that no composition would
  // ever reach (docs/90).
  if (g_canvases[holder].child_count >= kMaxControls) return false;
  // The same rule as the view: a canvas is a whole number of bytes wide, or a byte would hold the last
  // pixel of one line and the first of the next (docs/50).
  width = static_cast<uint16_t>(width & ~1u);
  if (width == 0 || height == 0) return false;
  int8_t free_slot = -1;
  for (uint8_t i = 1; i < kMaxControls; ++i) {
    if (!g_canvases[i].in_use) {
      free_slot = static_cast<int8_t>(i);
      break;
    }
  }
  if (free_slot < 0) return false;
  if (with_view) {
    // How many views are spoken for already. It is counted here and not kept anywhere else so the number
    // and the table can never disagree (docs/90).
    uint8_t views = 0;
    for (uint8_t i = 0; i < kMaxControls; ++i) {
      if (g_canvases[i].in_use && g_canvases[i].bits != nullptr) ++views;
    }
    if (views >= kMaxCanvases) return false;
  }
  Canvas& made = g_canvases[free_slot];
  made = Canvas{};
  made.in_use = true;
  made.width = width;
  made.height = height;
  made.stride = static_cast<uint16_t>(width / 2);
  if (with_view) {
    made.bytes = static_cast<uint32_t>(made.stride) * height;
    void* block = [&]() {
      // **The PSRAM is a device of this board**: a block taken from it is a call like any other (docs/75).
      meter::Scope scope(meter::kExtraAlloc);
      return pal::extra_memory().allocate(made.bytes, true);
    }();
    if (block == nullptr) {
      made = Canvas{};
      return false;
    }
    made.bits = static_cast<uint8_t*>(block);
    memset(made.bits, 0xFF, made.bytes);
  }
  made.parent = holder;
  made.generation = ++g_generation;
  Canvas& father = g_canvases[holder];
  father.children[father.child_count++] = free_slot;
  slot = free_slot;
  return true;
}

bool release(int8_t slot) {
  // The view of the app is the system's: an app gives back what it made and nothing else (docs/90).
  if (slot == 0 || !live(slot)) return false;
  // **It is asked for, not done**: the pixels are still on the page and the buffer is still in use until
  // the next present has painted over them (docs/90).
  request_release(slot, true);
  return true;
}

// What has been asked to go away is given back **after the page is clean**, and the buffer is wiped on the
// way out. Anything that still points at one of these slots answers `E_INVAL`, because the object of the
// app was told at the moment it asked (docs/90).
static void sweep_released() {
  for (uint8_t i = 1; i < kMaxControls; ++i) {
    if (g_canvases[i].in_use && g_canvases[i].pending_release) release_slot(static_cast<int8_t>(i));
  }
}

bool reparent(int8_t slot, int8_t parent) {
  const int8_t holder = (parent < 0) ? 0 : parent;
  const int8_t was = parent_of(slot);
  const Canvas* canvas = find(slot);
  if (canvas == nullptr || !attach_to(slot, holder)) return false;
  // The father it leaves owes exactly the rectangle it was occupying; the new father owes nothing (the
  // child is composed over it) and the child paints itself again because its place is now read in another
  // frame (docs/90).
  if (was >= 0) invalidate_rect(was, canvas->x, canvas->y, canvas->width, canvas->height);
  invalidate(slot);
  return true;
}

int8_t parent_of(int8_t slot) {
  const Canvas* target = find(slot);
  return (target != nullptr) ? target->parent : -1;
}

// **Marks this canvas and every canvas that holds it** as needing a redraw: a child that moved left its
// pixels inside its father's buffer, and the father is the one that has to paint them over —which it
// does when its own `on_draw` paints its canvas from scratch— (docs/90).
bool invalidate_rect(int8_t slot, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  Canvas* canvas = find(slot);
  if (canvas == nullptr) return false;
  canvas->needs_draw = true;
  if (canvas->redraw_w <= 0 || canvas->redraw_h <= 0) {
    canvas->redraw_x = x;
    canvas->redraw_y = y;
    canvas->redraw_w = w;
    canvas->redraw_h = h;
    return true;
  }
  // Two reasons to repaint the same control become one box: the union of the two.
  const int32_t x1 = static_cast<int32_t>(x + w);
  const int32_t y1 = static_cast<int32_t>(y + h);
  int32_t keep_x1 = canvas->redraw_x + canvas->redraw_w;
  int32_t keep_y1 = canvas->redraw_y + canvas->redraw_h;
  if (x < canvas->redraw_x) canvas->redraw_x = x;
  if (y < canvas->redraw_y) canvas->redraw_y = y;
  if (x1 > keep_x1) keep_x1 = x1;
  if (y1 > keep_y1) keep_y1 = y1;
  canvas->redraw_w = keep_x1 - canvas->redraw_x;
  canvas->redraw_h = keep_y1 - canvas->redraw_y;
  return true;
}

// **It does not go up to the fathers, and that is on purpose.** A control that changed its own pixels is a
// control whose layer is composed over its father again at the next present: the father has nothing of its
// own to paint. What the father owes is what a child **leaves behind** —a move, a canvas going away— and
// those reasons say so explicitly, with the rectangle, where they happen (docs/90).
bool invalidate(int8_t slot) {
  const Canvas* canvas = find(slot);
  if (canvas == nullptr) return false;
  return invalidate_rect(slot, 0, 0, canvas->width, canvas->height);
}

bool begin_draw(int8_t slot) {
  Canvas* canvas = find(slot);
  if (canvas == nullptr) return false;
  if (canvas->redraw_w <= 0 || canvas->redraw_h <= 0) {
    canvas->redraw_x = 0;
    canvas->redraw_y = 0;
    canvas->redraw_w = canvas->width;
    canvas->redraw_h = canvas->height;
  }
  canvas->in_draw = true;
  return true;
}

bool end_draw(int8_t slot) {
  Canvas* canvas = find(slot);
  if (canvas == nullptr) return false;
  canvas->in_draw = false;
  canvas->needs_draw = false;
  canvas->redraw_x = 0;
  canvas->redraw_y = 0;
  canvas->redraw_w = 0;
  canvas->redraw_h = 0;
  return true;
}

bool needs_draw(int8_t slot) {
  const Canvas* target = find(slot);
  return (target != nullptr) && target->needs_draw;
}

bool mark_drawn(int8_t slot) {
  Canvas* target = find(slot);
  if (target == nullptr) return false;
  target->needs_draw = false;
  return true;
}

uint16_t generation_of(int8_t slot) {
  const Canvas* target = find(slot);
  return (target != nullptr) ? target->generation : 0;
}

bool place(int8_t slot, uint16_t x, uint16_t y) {
  Canvas* target = find(slot);
  if (target == nullptr) return false;
  if (target->x != x || target->y != y) {
    // **The place the child leaves is owed to the first control that paints**, which may be a grandfather:
    // a box inside a panel holds children but paints nothing, so it is the panel that has to paint over
    // the hole, in the panel's own pixels (docs/90).
    const int8_t painter = painting_holder(target->parent);
    if (painter >= 0) {
      int32_t offset_x = 0;
      int32_t offset_y = 0;
      for (int8_t walk = slot; walk != painter && walk >= 0; walk = g_canvases[walk].parent) {
        offset_x += g_canvases[walk].x;
        offset_y += g_canvases[walk].y;
      }
      invalidate_rect(painter, static_cast<uint16_t>(offset_x), static_cast<uint16_t>(offset_y),
                      target->width, target->height);
    }
    invalidate(slot);
  }
  target->x = x;
  target->y = y;
  return true;
}

bool canvas_clear(int8_t slot, uint8_t level, const char* mode) {
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (!mode_or_default(mode, chosen)) return false;
  const Canvas* control = find(slot);
  if (control == nullptr || !g_attached) return true;
  // The frame of the control, in the pixels of the canvas that holds it: a control clears its own box and
  // nothing else, and if it has a view of its own that box is the whole view (docs/90).
  const uint16_t width = control->width;
  const uint16_t height = control->height;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return true;
  draw_fill_rect(*target.canvas, static_cast<uint16_t>(target.x), static_cast<uint16_t>(target.y),
                 width, height, level, chosen);
  close_draw(target);
  return true;
}

bool canvas_fill_rect(int8_t slot, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t level,
                      const char* mode) {
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (!mode_or_default(mode, chosen)) return false;
  if (!g_attached) return true;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return true;
  draw_fill_rect(*target.canvas, static_cast<uint16_t>(target.x + x),
                 static_cast<uint16_t>(target.y + y), w, h, level, chosen);
  close_draw(target);
  return true;
}

bool canvas_pixel(int8_t slot, uint16_t x, uint16_t y, uint8_t level, const char* mode) {
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (!mode_or_default(mode, chosen)) return false;
  if (!g_attached) return true;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return true;
  draw_pixel(*target.canvas, static_cast<uint16_t>(target.x + x),
             static_cast<uint16_t>(target.y + y), level, chosen);
  close_draw(target);
  return true;
}

uint8_t canvas_damage(int8_t slot) {
  const Canvas* target = find(slot);
  return (target != nullptr) ? target->damage_count : 0;
}

bool canvas_frame(int8_t slot, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t level,
                  const char* mode) {
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (!mode_or_default(mode, chosen)) return false;
  if (!g_attached) return true;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return true;
  draw_frame(*target.canvas, target.x + x, target.y + y, w, h, clamped(level), chosen);
  close_draw(target);
  return true;
}

bool canvas_line(int8_t slot, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t level,
                 const char* mode) {
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (!mode_or_default(mode, chosen)) return false;
  if (!g_attached) return true;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return true;
  draw_line(*target.canvas, target.x + x0, target.y + y0, target.x + x1, target.y + y1,
            clamped(level), chosen);
  close_draw(target);
  return true;
}

bool canvas_circle(int8_t slot, int16_t center_x, int16_t center_y, int16_t radius, uint8_t level,
                   const char* mode) {
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (!mode_or_default(mode, chosen)) return false;
  if (!g_attached || radius < 0) return true;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return true;
  draw_circle(*target.canvas, target.x + center_x, target.y + center_y, radius, clamped(level),
              chosen);
  close_draw(target);
  return true;
}

bool canvas_disc(int8_t slot, int16_t center_x, int16_t center_y, int16_t radius, uint8_t level,
                 const char* mode) {
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (!mode_or_default(mode, chosen)) return false;
  if (!g_attached || radius < 0) return true;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return true;
  draw_disc(*target.canvas, target.x + center_x, target.y + center_y, radius, clamped(level),
            chosen);
  close_draw(target);
  return true;
}

bool canvas_text(int8_t slot, int16_t x, int16_t y, const char* text, uint8_t scale, uint8_t level,
                 const char* mode) {
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (!mode_or_default(mode, chosen)) return false;
  if (!g_attached || text == nullptr || scale == 0) return true;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return true;
  draw_text(*target.canvas, face_of(target.canvas->face), target.x + x, target.y + y, text, scale,
            clamped(level), chosen);
  close_draw(target);
  return true;
}

uint16_t label_text(int8_t face, const char* text, uint16_t width, uint8_t scale, char* out,
                    size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = '\0';
  if (text == nullptr || text[0] == '\0' || scale == 0) return 0;
  const font::Face& current = face_of(face);
  const uint16_t whole = font::width(current, text, scale);
  if (whole <= width) {
    size_t len = strlen(text);
    if (len > cap - 1) len = cap - 1;
    memcpy(out, text, len);
    out[len] = '\0';
    return font::width(current, out, scale);
  }
  // **The cut is a prefix and three dots**, and it is the prefix that fits and not three dots that are
  // then cut by the frame: the frame is the last resort, not the way a label ends (docs/30, docs/90).
  static const char kDots[] = "...";
  if (cap < sizeof(kDots)) return 0;
  const uint16_t dots = font::width(current, kDots, scale);
  if (dots > width) {
    memcpy(out, kDots, sizeof(kDots));
    return dots;
  }
  size_t keep = strlen(text);
  if (keep > cap - sizeof(kDots)) keep = cap - sizeof(kDots);
  for (;;) {
    memcpy(out, text, keep);
    memcpy(out + keep, kDots, sizeof(kDots));
    const uint16_t shown = font::width(current, out, scale);
    if (shown <= width || keep == 0) return shown;
    --keep;
  }
}

bool label_draw(int8_t slot, const char* text, uint8_t color, uint8_t background, uint8_t align,
                uint8_t valign, uint8_t scale) {
  if (!live(slot) || color > 15 || background > 15 ||
      align > static_cast<uint8_t>(LabelAlign::kRight) ||
      valign > static_cast<uint8_t>(LabelVAlign::kBottom) || scale == 0 || scale > 16) {
    return false;
  }
  const Canvas& label = g_canvases[slot];
  const uint16_t width = label.width;
  const uint16_t height = label.height;
  if (width == 0 || height == 0) return true;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return false;
  // **The frame is the label's, and the background is paid before the text**: a control without a buffer
  // writes into the canvas that holds it, and what it owed is the hole the text leaves when it changes
  // (docs/30, docs/90).
  // **A label is a page of text in miniature, so it is painted with the quality that leaves no ghost of
  // the word before it**: the whole frame is one `gc16`, and a text that changes is clean on the next
  // present (docs/50, docs/90).
  draw_fill_rect(*target.canvas, static_cast<uint16_t>(target.x), static_cast<uint16_t>(target.y),
                 width, height, background, hal::UpdateMode::kGc16);
  if (text != nullptr && text[0] != '\0') {
    const font::Face& face = face_of(label.face);
    char shown[1024];
    const uint16_t shown_w = label_text(label.face, text, width, scale, shown, sizeof(shown));
    const uint16_t line_h =
        static_cast<uint16_t>((face.height != 0) ? face.height * scale : font::kGlyphHeight * scale);
    int32_t x = 0;
    if (align == static_cast<uint8_t>(LabelAlign::kCentre)) {
      x = (static_cast<int32_t>(width) - static_cast<int32_t>(shown_w)) / 2;
    } else if (align == static_cast<uint8_t>(LabelAlign::kRight)) {
      x = static_cast<int32_t>(width) - static_cast<int32_t>(shown_w);
    }
    int32_t y = 0;
    if (valign == static_cast<uint8_t>(LabelVAlign::kMiddle)) {
      y = (static_cast<int32_t>(height) - static_cast<int32_t>(line_h)) / 2;
    } else if (valign == static_cast<uint8_t>(LabelVAlign::kBottom)) {
      y = static_cast<int32_t>(height) - static_cast<int32_t>(line_h);
    }
    draw_text(*target.canvas, face, target.x + x, target.y + y, shown, scale, color,
              hal::UpdateMode::kGc16);
  }
  close_draw(target);
  return true;
}

bool button_draw(int8_t slot, uint8_t background, uint8_t border, bool pressed, bool focused) {
  if (!live(slot) || background > 15 || border > 15) return false;
  const Canvas& button = g_canvases[slot];
  const uint16_t width = button.width;
  const uint16_t height = button.height;
  if (width == 0 || height == 0) return true;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return false;
  // **The frame is the button**: a fill and one border, and a second border while it is held. The child
  // is not painted here —the tree draws it after this control, clipped to the same frame— and that is
  // what makes a label inside a button move with it (docs/30, docs/90).
  draw_fill_rect(*target.canvas, static_cast<uint16_t>(target.x), static_cast<uint16_t>(target.y),
                 width, height, background, hal::UpdateMode::kGc16);
  draw_frame(*target.canvas, target.x, target.y, width, height, border, hal::UpdateMode::kGc16);
  if (pressed && width > 4 && height > 4) {
    draw_frame(*target.canvas, target.x + 1, target.y + 1, static_cast<int32_t>(width) - 2,
               static_cast<int32_t>(height) - 2, border, hal::UpdateMode::kGc16);
  }
  // **The focus is a second border, two pixels in**: a button that is held already has one at one
  // pixel, and the two can be there at once. The child is drawn after and may cover it if it fills
  // the whole frame; the app owns that (docs/30, docs/90).
  if (focused && width > 6 && height > 6) {
    draw_frame(*target.canvas, target.x + 2, target.y + 2, static_cast<int32_t>(width) - 4,
               static_cast<int32_t>(height) - 4, border, hal::UpdateMode::kGc16);
  }
  // **The frame is opaque, and it covers everything inside it**: a child that did not redraw would be
  // erased by the button's own fill. The button has no buffer to compose under the child, so the child
  // is asked to draw again whenever the frame does (docs/30, docs/90).
  for (uint8_t i = 0; i < button.child_count; ++i) invalidate(button.children[i]);
  close_draw(target);
  return true;
}

bool present() {
  if (!g_attached || root().bits == nullptr) return false;
  compose_tree(0, -1, 0, 0);
  const bool ok = paint_root();
  // **And now the ones that asked to go**: the page has been composed and the paint asked for, so the
  // buffers can be wiped and the slots given back without a ghost and without anything stale (docs/90).
  sweep_released();
  return ok;
}

bool present_all(hal::UpdateMode mode) {
  if (!g_attached || root().bits == nullptr) return false;
  compose_tree(0, -1, 0, 0);
  merge_all(root(), mode);
  const bool ok = paint_root();
  sweep_released();
  return ok;
}

bool idle() { return display::idle(); }

bool title(const char* text) {
  bar::title(text);
  return true;
}

bool place(uint16_t x, uint16_t y) {
  if (root().bits == nullptr) return false;
  root().x = x;
  root().y = y;
  return true;
}

bool resize(uint16_t width, uint16_t height) {
  if (!g_attached) return false;
  // The place survives a resize and the view keeps it; **the canvases inside it do not**, because they
  // are the app's and the app is told to draw again (docs/30, docs/90).
  if (!make_root(width, height)) return false;
  luabind::call_resize(root().width, root().height);
  return true;
}

bool set_rotation(uint16_t degrees) {
  const uint16_t was = display::rotation();
  // The glass decides: its driver keeps the image in the frame of the page and turns what it sends.
  if (!display::set_rotation(degrees)) return false;
  const uint16_t top = bar::height();
  if (display::height() <= top) {
    display::set_rotation(was);
    return false;
  }
  // **The page is made again**, because its two sides have swapped, and the bar is painted again at the
  // top of the new page. The app is the one that draws it: `on_resize` is how it knows (docs/30, docs/50).
  // If there is no memory for it the glass is put back where it was: a panel turned one way and a view
  // of the other size is the one state this system must never be in (docs/50).
  if (!make_root(display::width(), static_cast<uint16_t>(display::height() - top))) {
    display::set_rotation(was);
    return false;
  }
  root().y = top;
  root().x = 0;
  bar::refresh();
  if (g_attached) luabind::call_resize(root().width, root().height);
  return true;
}

uint16_t rotation() { return display::rotation(); }

// ---- the faces a text can be drawn with (docs/30) ----

namespace {

// **The faces an app has loaded**, each with its own block of glyphs. Three is what fits a bar, a title and
// a page of an app that wants three sizes, and a fourth is a face nobody needs (docs/30).
struct LoadedFace {
  bool in_use = false;
  font::Face face;
  uint8_t* bits = nullptr;
  // **A face of outlines keeps its file**: its glyphs are contours inside the bytes, so the file cannot be
  // given back when it has been read — it *is* the face, and the block of bits is never allocated (docs/30).
  ttf::Face outline;
  char* file = nullptr;
};

LoadedFace g_faces[kMaxFaces];
// The face of the view and of the shorthand calls, or -1 for the font of the system.
int8_t g_app_face = -1;

void release_faces() {
  for (uint8_t i = 0; i < kMaxFaces; ++i) {
    if (g_faces[i].in_use) font_release(static_cast<int8_t>(i));
  }
  g_app_face = -1;
}

const font::Face& face_of(int8_t handle) {
  if (handle < 0 || handle >= static_cast<int8_t>(kMaxFaces)) return font::system_face();
  const LoadedFace& loaded = g_faces[handle];
  if (!loaded.in_use) return font::system_face();
  if (loaded.bits == nullptr && loaded.outline.data == nullptr) return font::system_face();
  return loaded.face;
}

// **The biggest font file this system reads**: a face of ninety-six glyphs of eight rows is about four
// kilobytes of text, and a TrueType font of a page is a few tens of kilobytes —a whole family of them is
// past this and is not a face a page needs— (docs/30, docs/75).
const uint32_t kFontFileBytes = 131072;

}  // namespace

int8_t font_load(const char* path, const char*& reason) {
  reason = nullptr;
  if (path == nullptr) {
    reason = "no path";
    return -1;
  }
  // **The path is the app's, not the kernel's**: `/res/x` from an app is `apps/<id>/res/x`, which is where
  // the files an app ships with live, so the same rule that the `fs.*` calls obey is applied here (docs/70).
  char full[128];
  if (!app_path(path, full, sizeof(full))) {
    reason = "that is not a path an app may name";
    return -1;
  }
  hal::Stat info;
  if (!vfs::stat(full, info)) {
    reason = "there is no such file";
    return -1;
  }
  if (info.size == 0 || info.size > kFontFileBytes) {
    reason = "a font file is up to a hundred and twenty-eight kilobytes";
    return -1;
  }
  // A free slot first: a face that cannot be kept is not worth reading the file for.
  int8_t slot = -1;
  for (uint8_t i = 0; i < kMaxFaces; ++i) {
    if (!g_faces[i].in_use) {
      slot = static_cast<int8_t>(i);
      break;
    }
  }
  if (slot < 0) {
    reason = "there is no room for another face";
    return -1;
  }
  // **The text of the file and the glyphs are two blocks of the app's memory**, and the text is given back
  // as soon as it has been read: what a face keeps is the glyphs (docs/30, docs/75).
  void* text = nullptr;
  {
    meter::Scope scope(meter::kExtraAlloc);
    text = pal::extra_memory().allocate(static_cast<size_t>(info.size) + 1, true);
  }
  if (text == nullptr) {
    reason = "there is no memory for the file";
    return -1;
  }
  size_t read = 0;
  char* as_text = static_cast<char*>(text);
  const bool ok = vfs::read_file(full, as_text, static_cast<size_t>(info.size), read);
  as_text[read] = '\0';
  if (!ok) {
    pal::extra_memory().release(text);
    reason = "the file could not be read";
    return -1;
  }
  // **A TrueType file is a face of outlines and not a table of bits** (docs/30): the file is what the face is
  // made of, so it stays in the app's memory and the block of glyphs is never allocated for it.
  LoadedFace& made = g_faces[slot];
  if (font::looks_like_ttf(as_text, static_cast<uint32_t>(read))) {
    const char* outline_why = font::load_ttf(as_text, static_cast<uint32_t>(read), made.outline, made.face);
    if (outline_why == nullptr) {
      made.file = as_text;
      made.bits = nullptr;
      made.in_use = true;
      return slot;
    }
    pal::extra_memory().release(text);
    reason = outline_why;
    return -1;
  }
  uint8_t* bits = static_cast<uint8_t*>(pal::extra_memory().allocate(font::kFaceBytes, true));
  if (bits == nullptr) {
    pal::extra_memory().release(text);
    reason = "there is no memory for the glyphs";
    return -1;
  }
  const char* why = font::load_any(as_text, static_cast<uint32_t>(read), bits, made.face);
  pal::extra_memory().release(text);
  if (why != nullptr) {
    pal::extra_memory().release(bits);
    reason = why;
    return -1;
  }
  made.bits = bits;
  made.in_use = true;
  return slot;
}

bool font_release(int8_t handle) {
  if (handle < 0 || handle >= static_cast<int8_t>(kMaxFaces)) return false;
  LoadedFace& loaded = g_faces[handle];
  if (!loaded.in_use) return false;
  // **The block goes back and the slot is free**, and every canvas that was drawing with it falls back to
  // the font of the system: a face is data, and data that an app gave back cannot be drawn from (docs/30).
  pal::extra_memory().release(loaded.bits);
  if (loaded.file != nullptr) pal::extra_memory().release(loaded.file);
  loaded = LoadedFace{};
  if (g_app_face == handle) g_app_face = -1;
  return true;
}

bool font_info(int8_t handle, uint8_t& width, uint8_t& height, uint8_t& gap, uint8_t& glyphs) {
  if (handle < 0) {
    const font::Face& face = font::system_face();
    width = face.width;
    height = face.height;
    gap = face.gap;
    glyphs = face.count;
    return true;
  }
  if (handle >= static_cast<int8_t>(kMaxFaces) || !g_faces[handle].in_use) return false;
  const font::Face& face = g_faces[handle].face;
  width = face.width;
  height = face.height;
  gap = face.gap;
  glyphs = face.count;
  return true;
}

bool canvas_set_font(int8_t slot, int8_t handle) {
  if (!live(slot)) return false;
  if (handle >= static_cast<int8_t>(kMaxFaces)) return false;
  if (handle >= 0 && !g_faces[handle].in_use) return false;
  g_canvases[slot].face = handle;
  return true;
}

bool app_set_font(int8_t handle) {
  if (handle >= static_cast<int8_t>(kMaxFaces)) return false;
  if (handle >= 0 && !g_faces[handle].in_use) return false;
  g_app_face = handle;
  // The view is the canvas the app draws on with the shorthand calls, so it wears the face too.
  g_canvases[0].face = handle;
  return true;
}

uint16_t font_text_width(int8_t handle, const char* text, uint8_t scale) {
  return font::width(face_of(handle), text, scale);
}

// **The number of quick paints after which what is left is ghost**, which is what a panel shows after a few
// dozen partial updates: past this a page of text needs the full wash too, and the number is one constant to
// change after somebody counts it on the board (docs/50).
const uint16_t kManyPaints = 32;

hal::UpdateMode cleaner_for(uint16_t levels, uint16_t paints) {
  // Nothing was painted: there is nothing to clean and the cheapest answer is the one that does nothing.
  if (levels == 0) return hal::UpdateMode::kDu;
  // **A page that has taken many quick paints owes a full wash whatever it showed** (docs/50).
  if (paints >= kManyPaints) return hal::UpdateMode::kGc16;
  // **Text**: black and white and nothing between them is a page the two-level wash takes away.
  const uint16_t text_only = static_cast<uint16_t>((1u << 0) | (1u << 15));
  if ((levels & static_cast<uint16_t>(~text_only)) == 0) return hal::UpdateMode::kA2;
  // A few grays: the sixteen-level wash, which is the same time as the full one but does not flash black.
  uint8_t grays = 0;
  for (uint8_t level = 0; level < 16; ++level) {
    if ((levels & (1u << level)) != 0) ++grays;
  }
  if (grays <= 4) return hal::UpdateMode::kGl16;
  // **A photograph**: all sixteen, or most of them, and only the full wash leaves the paper clean.
  return hal::UpdateMode::kGc16;
}

uint16_t levels_shown() { return g_levels; }
uint16_t paints_since_clean() { return g_paints; }

}  // namespace ui
}  // namespace kernel
