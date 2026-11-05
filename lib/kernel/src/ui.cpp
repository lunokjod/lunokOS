// The canvas of the app, the damage it leaves on it and the copy that puts it on the glass
// (docs/30): see `ui.h` for what the separation is for.
//
// **The sections of this file, in order**:
//   - the canvases and what they owe: the state, attach, compose and present;
//   - the primitives: pixel, box, frame, bevel, line, circle, disc and text;
//   - where a control's pixels go, and drawing into a canvas that is not the root;
//   - the interpreter's side: every `ui.*` call;
//   - the controls: progress, slider, a row of a list, panel, tabs, list, text box, box of several
//     lines, month, sparkline and pie, dialog, menu, grid, band of notifications, selector and menu bar;
//   - the keyboard, the face of its keys and the three icons that are not letters;
//   - the faces a text can be drawn with.
// The `// ----` banners are the same index in place (docs/10, docs/90).
#include "kernel/log.h"
#include "kernel/meter.h"
#include "kernel/ui.h"

#include <stdio.h>
#include <string.h>

#include "img/img.h"

#include "kernel/bar.h"
#include "kernel/calendar.h"
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
// **Whether the canvas still holds the band of the keyboard**: it is a picture drawn into the page, so it is there
// until something wipes it —the app's `clear`, a rectangle or a pixel over it, a new canvas— and a letter typed does
// not. The flag is the band's, and `clear` asks it through this door (docs/50, docs/90).
void keyboard_page_cleared();
// The number the next canvas takes, so an object of an app that points at a slot that was given back
// never matches the one that took its place (docs/90).
uint16_t g_generation = 0;

// The faces an app loaded are given back when the app ends; the table is at the bottom of this file, with
// the rest of the font work (docs/30).
void release_faces();
// **The band of the keyboard is painted into the page** (docs/90), so whatever paints over it leaves a hole where it
// was: this says whether a rectangle the app drew touches the band, and the band is drawn again at the next present.
void the_band_may_be_gone(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
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

// **A frame of more than one pixel**, which is what the row of the focus of a bar and the frame of a selector wear: one
// pixel of ink on this glass is a line a finger does not see (docs/90).
void draw_frame_thick(Canvas& canvas, int32_t x, int32_t y, int32_t w, int32_t h, int32_t thickness,
                      uint8_t level, hal::UpdateMode mode) {
  if (w <= 0 || h <= 0 || thickness <= 0) return;
  for (int32_t i = 0; i < thickness; ++i) {
    if (w - 2 * i <= 0 || h - 2 * i <= 0) break;
    put_rect(canvas, x + i, y + i, w - 2 * i, 1, level);
    put_rect(canvas, x + i, y + h - 1 - i, w - 2 * i, 1, level);
    put_rect(canvas, x + i, y + i, 1, h - 2 * i, level);
    put_rect(canvas, x + w - 1 - i, y + i, 1, h - 2 * i, level);
  }
  mark_box(canvas, x, y, w, h, mode);
}

// **How far a corner pushes the ink in on a row**, which is a quarter of a circle and therefore a square root
// of integers: no floating point, and a table of radii would be more code than this loop (docs/90).
int32_t corner_inset(int32_t radius, int32_t down) {
  if (radius <= 0 || down >= radius) return 0;
  const int32_t left = radius * radius - down * down;
  int32_t root = 0;
  while ((root + 1) * (root + 1) <= left) ++root;
  return radius - root;
}

// **A box with the corners rounded off**, row by row: a control with volume needs a shape, and a rounded one
// is what a button of this panel looks like (docs/90).
void draw_round_fill(Canvas& canvas, int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius,
                     uint8_t level, hal::UpdateMode mode) {
  if (w <= 0 || h <= 0) return;
  const int32_t most = ((w < h) ? w : h) / 2;
  if (radius > most) radius = most;
  if (radius < 0) radius = 0;
  for (int32_t row = 0; row < h; ++row) {
    int32_t down = radius;
    if (row < radius) {
      down = radius - row - 1;
    } else if (row >= h - radius) {
      down = row - (h - radius);
    }
    const int32_t inset = corner_inset(radius, down);
    put_rect(canvas, x + inset, y + row, w - 2 * inset, 1, level);
  }
  mark_box(canvas, x, y, w, h, mode);
}

// **The two edges of a bevel, in two levels** (0.85.4): the one that catches the light on the top and the left and the
// shadow on the bottom and the right, and what the mouth of a pressed control is made of. On a body of paper the light
// edge has nothing to draw and the shadow is ink; on a body of ink it is the other way round. **A control that is black
// and white is a control that can be cleaned without a flash**, and a bevel of greys —a white button's was eleven and
// seven— is a control that needs the wash of sixteen levels and leaves a grey ghost when its bevel moves
// (docs/50, docs/92, docs/90).
struct Bevel {
  uint8_t light;
  uint8_t dark;
  uint8_t deep;
};

Bevel bevel_of(uint8_t background) {
  const bool paper = (background >= 8);
  Bevel out;
  out.light = paper ? background : 15;
  out.dark = paper ? 0 : background;
  out.deep = paper ? 0 : 15;
  return out;
}

// **The edge of a rounded box, one pixel of it, split into a light half and a dark one**: that is what makes
// a button look like it comes out of the page, and swapping the two makes it look pressed in (docs/90).
void draw_round_bevel(Canvas& canvas, int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius,
                      uint8_t top_left_level, uint8_t bottom_right_level, hal::UpdateMode mode) {
  if (w <= 2 || h <= 2) return;
  if (radius > (w < h ? w : h) / 2) radius = (w < h ? w : h) / 2;
  if (radius < 0) radius = 0;
  for (int32_t row = 0; row < h; ++row) {
    int32_t down = radius;
    if (row < radius) {
      down = radius - row - 1;
    } else if (row >= h - radius) {
      down = row - (h - radius);
    }
    const int32_t inset = corner_inset(radius, down);
    const int32_t left = x + inset;
    const int32_t right = x + w - 1 - inset;
    if (left >= right) continue;
    const bool on_top = (row == 0);
    const bool on_bottom = (row == h - 1);
    if (on_top) {
      put_rect(canvas, left, y, right - left + 1, 1, top_left_level);
      continue;
    }
    if (on_bottom) {
      put_rect(canvas, left, y + h - 1, right - left + 1, 1, bottom_right_level);
      continue;
    }
    put_pixel(canvas, left, y + row, top_left_level);
    put_pixel(canvas, right, y + row, bottom_right_level);
  }
  mark_box(canvas, x, y, w, h, mode);
}

// A line that is straight up, down, left or right is a rectangle, and a rectangle is exact; the rest
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

// **A picture on a canvas**: the rows of the file land where the app put them, scaled as it asked, cut to the
// canvas and to what the control owes. It is the same copy a glyph gets and the same packing, which is why a
// picture costs no conversion at all (docs/30, docs/50).
struct ImagePlace {
  Canvas* canvas = nullptr;
  // The rectangle the picture occupies on the canvas, before any cut (docs/30).
  int32_t x = 0;
  int32_t y = 0;
  int32_t w = 0;
  int32_t h = 0;
  // What of it may be written: the canvas, the frame of the control and what it owes (docs/90).
  int32_t clip_x0 = 0;
  int32_t clip_y0 = 0;
  int32_t clip_x1 = 0;
  int32_t clip_y1 = 0;
  uint32_t source_w = 0;
  uint32_t source_h = 0;
  bool wrote = false;
};

void image_row(void* context, uint16_t source_y, const uint8_t* packed) {
  ImagePlace& place = *static_cast<ImagePlace*>(context);
  Canvas& canvas = *place.canvas;
  // **The nearest neighbour**: every row of the file covers the rows of the page it lands on, and every
  // pixel does the same. A page of sixteen levels has nothing better to offer, and a better filter is a
  // change of this one loop (docs/30).
  const int32_t first = static_cast<int32_t>((static_cast<int64_t>(source_y) * place.h) / place.source_h);
  int32_t last = static_cast<int32_t>((static_cast<int64_t>(source_y + 1) * place.h) / place.source_h);
  if (last <= first) last = first + 1;
  for (int32_t target = first; target < last; ++target) {
    const int32_t canvas_y = place.y + target;
    if (canvas_y < place.clip_y0 || canvas_y >= place.clip_y1) continue;
    uint8_t* line = canvas.bits + static_cast<uint32_t>(canvas_y) * canvas.stride;
    for (int32_t column = 0; column < place.w; ++column) {
      const int32_t canvas_x = place.x + column;
      if (canvas_x < place.clip_x0 || canvas_x >= place.clip_x1) continue;
      const uint32_t source_x =
          static_cast<uint32_t>((static_cast<int64_t>(column) * place.source_w) / place.w);
      const uint32_t at = (source_x < place.source_w) ? source_x : place.source_w - 1;
      const uint8_t byte = packed[at / 2];
      const uint8_t level = ((at & 1) == 0) ? static_cast<uint8_t>(byte >> 4)
                                            : static_cast<uint8_t>(byte & 0x0F);
      uint8_t& target_byte = line[canvas_x / 2];
      if ((canvas_x & 1) == 0) {
        target_byte = static_cast<uint8_t>((target_byte & 0x0F) | (level << 4));
      } else {
        target_byte = static_cast<uint8_t>((target_byte & 0xF0) | level);
      }
      place.wrote = true;
    }
  }
}

// The biggest file a picture may be and the room its decoder may take: a page of a photograph is a few
// hundred kilobytes, the PSRAM of the Paper is four megabytes, and a file that is not a picture of a page is
// not a file this board reads (docs/30, docs/75).
const uint32_t kImageFileBytes = 2u << 20;
const uint32_t kImageScratchBytes = 3u << 20;

// Why a picture could not be read, in the words a person reads, and the code an app branches on (docs/30).
const char* image_why(img::Status status, const char*& code) {
  switch (status) {
    case img::kOk:
      return nullptr;
    case img::kUnknown:
      code = "E_INVAL";  // not a picture of a family this board knows
      return "that is not a picture this board knows";
    case img::kBadData:
      code = "E_IO";  // a file that says it is a picture and is not one
      return "the picture is broken";
    case img::kUnsupported:
      code = "E_UNSUPPORTED";
      return "this board does not read that kind of picture";
    case img::kTruncated:
      code = "E_IO";
      return "the picture ends in the middle";
    case img::kNoRoom:
      code = "E_NOSPC";
      return "there is no memory for this picture";
  }
  code = "E_IO";
  return "the picture could not be read";
}

// ---- the interpreter's side ----

bool caps_for_lua(luabind::UIInfo& out) {
  const Canvas& view = root();
  if (!g_attached || view.bits == nullptr) return false;
  out.width = view.width;
  // **The height is the page and not the canvas** (0.84.0): the band of the keyboard is the system's.
  out.height = height();
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

// **The bridge does not know what a panel flow is**: it says a number and this is the one place that turns it
// into the enum, so `luabind` keeps no knowledge of the layout of the kernel (docs/10, docs/90).
uint8_t panel_layout_for_lua(int8_t slot, uint8_t flow, uint16_t padding, uint16_t gap,
                             const luabind::UIOps::UIPanelChild* children, uint8_t count) {
  if (children == nullptr || count > 16) return 0;
  PanelChild mine[16];
  for (uint8_t i = 0; i < count; ++i) {
    mine[i].slot = children[i].slot;
    mine[i].weight = children[i].weight;
  }
  return panel_layout(slot, static_cast<PanelFlow>(flow), padding, gap, mine, count);
}

// **One arithmetic and two callers**: the app asks how many rows and pages a list of that size makes and the
// drawing asks the same function, so a list cannot say "2 / 3" over the rows of page four (docs/90).
uint8_t list_page_of_for_lua(uint16_t height, uint16_t row_height, uint16_t items, uint8_t& rows) {
  const ListPage page = list_page_of(height, row_height, items);
  rows = page.rows;
  return page.pages;
}

// **The options of a menu, said in the kernel's words**: the bridge reads them from the app's table and the drawing
// takes the system's array, because the two sides of a control do not share a struct (docs/90).
bool menu_draw_for_lua(int8_t slot, const char* title, const luabind::UIOps::UIMenuOption* items, uint8_t count,
                       uint8_t index, uint8_t color, uint8_t dim, uint8_t background, uint8_t scale,
                       bool focused) {
  if (items == nullptr || count > kMaxMenuOptions) return false;
  MenuOption mine[kMaxMenuOptions];
  for (uint8_t i = 0; i < count; ++i) {
    mine[i].label = items[i].label;
    mine[i].tag = items[i].tag;
    mine[i].hidden = items[i].hidden;
    mine[i].primary = items[i].primary;
  }
  return menu_draw(slot, title, mine, count, index, color, dim, background, scale, focused);
}

uint8_t menu_hit_for_lua(int8_t slot, const char* title, const luabind::UIOps::UIMenuOption* items, uint8_t count,
                         uint8_t scale, uint16_t x, uint16_t y) {
  if (items == nullptr || count > kMaxMenuOptions) return 0;
  MenuOption mine[kMaxMenuOptions];
  for (uint8_t i = 0; i < count; ++i) {
    mine[i].label = items[i].label;
    mine[i].tag = items[i].tag;
    mine[i].hidden = items[i].hidden;
    mine[i].primary = items[i].primary;
  }
  return menu_hit(slot, title, mine, count, scale, x, y);
}

// **The bar of menus, said in the kernel's words**: the titles point into one block of entries, so a bar of five titles
// and sixteen entries is two arrays and no allocation (docs/90).
bool menu_bar_build(const luabind::UIOps::UIMenuBarTitle* titles, uint8_t count, MenuBarTitle* mine,
                    MenuBarEntry* entries) {
  if (titles == nullptr || count > kMaxMenuBarTitles) return false;
  uint8_t used = 0;
  for (uint8_t i = 0; i < count; ++i) {
    mine[i].label = titles[i].label;
    mine[i].entries = &entries[used];
    mine[i].count = titles[i].count;
    for (uint8_t j = 0; j < titles[i].count && used < kMaxMenuBarEntries; ++j) {
      entries[used].label = titles[i].entries[j].label;
      entries[used].separator = titles[i].entries[j].separator;
      entries[used].checked = titles[i].entries[j].checked;
      entries[used].checkable = titles[i].entries[j].checkable;
      entries[used].enabled = titles[i].entries[j].enabled;
      ++used;
    }
  }
  return true;
}

bool menu_bar_draw_for_lua(int8_t slot, const luabind::UIOps::UIMenuBarTitle* titles, uint8_t count, uint8_t open,
                           uint8_t index, uint16_t bar_height, uint8_t color, uint8_t dim, uint8_t background,
                           uint8_t scale, bool focused) {
  MenuBarTitle mine[kMaxMenuBarTitles];
  MenuBarEntry entries[kMaxMenuBarEntries];
  if (!menu_bar_build(titles, count, mine, entries)) return false;
  return menu_bar_draw(slot, mine, count, open, index, bar_height, color, dim, background, scale, focused);
}

uint8_t menu_bar_title_at_for_lua(int8_t slot, const luabind::UIOps::UIMenuBarTitle* titles, uint8_t count,
                                  uint16_t bar_height, uint8_t scale, uint16_t x, uint16_t y) {
  MenuBarTitle mine[kMaxMenuBarTitles];
  MenuBarEntry entries[kMaxMenuBarEntries];
  if (!menu_bar_build(titles, count, mine, entries)) return 0;
  return menu_bar_title_at(slot, mine, count, bar_height, scale, x, y);
}

uint8_t menu_bar_entry_at_for_lua(int8_t slot, const luabind::UIOps::UIMenuBarTitle* titles, uint8_t count,
                                  uint8_t open, uint16_t bar_height, uint8_t scale, uint16_t x, uint16_t y) {
  MenuBarTitle mine[kMaxMenuBarTitles];
  MenuBarEntry entries[kMaxMenuBarEntries];
  if (!menu_bar_build(titles, count, mine, entries)) return 0;
  return menu_bar_entry_at(slot, mine, count, open, bar_height, scale, x, y);
}

// **The cards of a band, said in the kernel's words**: three of them at most on the glass, and the rest are counted by the
// control (docs/90).
bool notification_bar_draw_for_lua(int8_t slot, const luabind::UIOps::UINotification* items, uint8_t shown,
                                   uint16_t total, uint8_t color, uint8_t dim, uint8_t background, uint8_t border,
                                   uint8_t scale, bool focused) {
  if (items == nullptr || shown > kMaxNotifications) return false;
  Notification mine[kMaxNotifications];
  for (uint8_t i = 0; i < shown; ++i) {
    mine[i].title = items[i].title;
    mine[i].text = items[i].text;
  }
  return notification_bar_draw(slot, mine, shown, total, color, dim, background, border, scale, focused);
}

// **The cells of a grid, said in the kernel's words** (docs/90).
bool icon_grid_build(const luabind::UIOps::UIGridItem* items, uint8_t count, GridItem* mine) {
  if (items == nullptr || count > kMaxGridItems) return false;
  for (uint8_t i = 0; i < count; ++i) {
    mine[i].label = items[i].label;
    mine[i].mark = items[i].mark;
  }
  return true;
}

bool icon_grid_draw_for_lua(int8_t slot, const luabind::UIOps::UIGridItem* items, uint8_t count, uint8_t index,
                            uint8_t marked, uint8_t columns, uint8_t color, uint8_t dim, uint8_t background,
                            uint8_t scale, bool focused) {
  GridItem mine[kMaxGridItems];
  if (!icon_grid_build(items, count, mine)) return false;
  return icon_grid_draw(slot, mine, count, index, marked, columns, color, dim, background, scale, focused);
}

uint8_t icon_grid_hit_for_lua(int8_t slot, const luabind::UIOps::UIGridItem* items, uint8_t count, uint8_t index,
                              uint8_t columns, uint8_t scale, uint16_t x, uint16_t y) {
  GridItem mine[kMaxGridItems];
  if (!icon_grid_build(items, count, mine)) return 0;
  return icon_grid_hit(slot, mine, count, index, columns, scale, x, y);
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
                                     button_draw,
                                     checkbox_side,
                                     checkbox_draw,
                                     progress_draw,
                                     slider_draw,
                                     row_draw,
                                     panel_layout_for_lua,
                                     canvas_resize,
                                     tabs_draw,
                                     dots_draw,
                                     list_page_of_for_lua,
                                     list_draw,
                                     dialog_draw,
                                     dialog_hit,
                                     sparkline_draw,
                                     pie_draw,
                                     month_draw,
                                     month_hit,
                                     calendar::days_in_month,
                                     textbox_draw,
                                     textbox_inside,
                                     keyboard_show,
                                     keyboard_hide,
                                     keyboard_visible,
                                     keyboard_height,
                                     luabind::ui_keyboard_typed,
                                     image,
                                     image_info,
                                     menu_draw_for_lua,
                                     menu_hit_for_lua,
                                     menu_bar_draw_for_lua,
                                     menu_bar_title_at_for_lua,
                                     menu_bar_entry_at_for_lua,
                                     textbox_multiline_draw,
                                     textbox_multiline_hit,
                                     textbox_multiline_rows,
                                     textbox_multiline_total,
                                     textbox_multiline_line,
                                     textbox_multiline_scroll,
                                     selector_draw,
                                     notification_bar_draw_for_lua,
                                     notification_bar_card_at,
                                     notification_bar_close_at,
                                     icon_grid_draw_for_lua,
                                     icon_grid_hit_for_lua});
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
// **The page an app has is what is left over the keyboard** (0.84.0): the band is the system's and it is painted over
// the bottom of the glass, so a view an app lays itself out in ends where the band begins. `ui.height()` and
// `ui.caps().height` say that number —736 and not 912 on the Paper— and the app hears `on_resize` when it changes, the
// same way a turn or a resize tells it (docs/30, docs/90).
uint16_t height() {
  const uint16_t band = keyboard_height();
  return (band < root().height) ? static_cast<uint16_t>(root().height - band) : 0;
}
uint8_t levels() { return 16; }

void attach() {
  if (root().bits == nullptr) return;
  // **The keyboard is the system's and does not outlive the page that opened it**: a band left on the glass
  // after the app that asked for it would be a keyboard typing into nothing (docs/90). It goes **with the page out of
  // the books**, because the app that leaves is not told that its view grew and the one that comes has not asked for a
  // keyboard yet (0.84.0).
  g_attached = false;
  keyboard_hide();
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
  // **The page is white again, so the band is not there any more**: it is a picture drawn into the page, and the next
  // present has to put it back (docs/50, docs/90).
  keyboard_page_cleared();
  return true;
}

bool fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t level, const char* mode) {
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (!mode_or_default(mode, chosen)) return false;
  if (!g_attached) return true;
  draw_fill_rect(root(), x, y, w, h, level, chosen);
  the_band_may_be_gone(x, y, w, h);
  return true;
}

// **The picture of the app's own view**, which is what `ui.image` draws on: the same call a control gets,
// with the root as the canvas (docs/30).
bool image(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char* path, const char* mode,
           const char*& code, const char*& reason) {
  return canvas_image(0, x, y, w, h, path, mode, code, reason);
}

bool pixel(uint16_t x, uint16_t y, uint8_t level, const char* mode) {
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (!mode_or_default(mode, chosen)) return false;
  if (!g_attached) return true;
  draw_pixel(root(), x, y, level, chosen);
  the_band_may_be_gone(x, y, 1, 1);
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

// **A picture, from a file of the volume to the pixels of the canvas** (docs/30): the size the app asks for
// —or the size of the picture— scaled as it is read, cut to the canvas and to what the control owes, and the
// damage left where it landed. The file is read into the app's extra memory and the decoder's block with it,
// and both go back before this returns: a picture is on the page, not in the heap (docs/75).
bool canvas_image(int8_t slot, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char* path,
                  const char* mode, const char*& code, const char*& reason) {
  code = "E_INVAL";
  reason = nullptr;
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (!mode_or_default(mode, chosen)) {
    reason = "not a quality of this display";
    return false;
  }
  if (!g_attached) return true;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return true;
  Canvas& canvas = *target.canvas;

  char full[128];
  if (!app_path(path, full, sizeof(full))) {
    close_draw(target);
    reason = "that is not a path an app may name";
    return false;
  }
  hal::Stat info;
  if (!vfs::stat(full, info) || info.type != hal::NodeType::kFile) {
    close_draw(target);
    reason = "there is no such picture";
    return false;
  }
  if (info.size == 0 || info.size > kImageFileBytes) {
    close_draw(target);
    reason = "a picture is up to two megabytes";
    return false;
  }
  void* file = nullptr;
  {
    meter::Scope scope(meter::kExtraAlloc);
    file = pal::extra_memory().allocate(static_cast<size_t>(info.size) + 1, true);
  }
  if (file == nullptr) {
    close_draw(target);
    reason = "there is no memory for the file";
    return false;
  }
  size_t read = 0;
  char* bytes = static_cast<char*>(file);
  if (!vfs::read_file(full, bytes, static_cast<size_t>(info.size), read)) {
    pal::extra_memory().release(file);
    close_draw(target);
    reason = "the picture could not be read";
    return false;
  }
  const uint8_t* data = reinterpret_cast<const uint8_t*>(bytes);
  img::Info picture;
  img::Status status = img::read_info(data, static_cast<uint32_t>(read), picture);
  if (status == img::kOk) {
    // **The size it is drawn at**: what the app asked for, or the size of the picture when it asked for
    // nothing (docs/30).
    const uint32_t asked_w = (w == 0) ? picture.width : w;
    const uint32_t asked_h = (h == 0) ? picture.height : h;
    ImagePlace place;
    place.canvas = &canvas;
    place.source_w = picture.width;
    place.source_h = picture.height;
    place.x = target.x + x;
    place.y = target.y + y;
    place.w = static_cast<int32_t>(asked_w);
    place.h = static_cast<int32_t>(asked_h);
    if (asked_w == 0 || asked_h == 0 || asked_w > 4096 || asked_h > 4096) {
      status = img::kBadData;
    } else {
      int32_t cx = place.x;
      int32_t cy = place.y;
      int32_t cw = place.w;
      int32_t ch = place.h;
      if (clip_to_redraw(canvas, cx, cy, cw, ch)) {
        place.clip_x0 = (cx > 0) ? cx : 0;
        place.clip_y0 = (cy > 0) ? cy : 0;
        place.clip_x1 = (cx + cw < canvas.width) ? cx + cw : canvas.width;
        place.clip_y1 = (cy + ch < canvas.height) ? cy + ch : canvas.height;
        const uint32_t scratch_bytes = img::scratch_bytes(data, static_cast<uint32_t>(read));
        void* scratch = nullptr;
        if (scratch_bytes > 0) {
          if (scratch_bytes > kImageScratchBytes) {
            status = img::kNoRoom;
          } else {
            meter::Scope scope(meter::kExtraAlloc);
            scratch = pal::extra_memory().allocate(scratch_bytes, true);
            if (scratch == nullptr) status = img::kNoRoom;
          }
        }
        if (status == img::kOk) {
          img::RowSink sink(image_row, &place);
          status = img::decode(data, static_cast<uint32_t>(read), sink,
                               static_cast<uint8_t*>(scratch), scratch_bytes);
          if (status == img::kOk && place.wrote) {
            const uint16_t damage_x = static_cast<uint16_t>(place.clip_x0);
            const uint16_t damage_y = static_cast<uint16_t>(place.clip_y0);
            const uint16_t damage_w = static_cast<uint16_t>(place.clip_x1 - place.clip_x0);
            const uint16_t damage_h = static_cast<uint16_t>(place.clip_y1 - place.clip_y0);
            damage_add(canvas, damage_x, damage_y, damage_w, damage_h, chosen);
          }
        }
        if (scratch != nullptr) pal::extra_memory().release(scratch);
      }
    }
  }
  pal::extra_memory().release(file);
  close_draw(target);
  if (status != img::kOk) {
    reason = image_why(status, code);
    return false;
  }
  code = nullptr;
  return true;
}

// **What a picture is, without drawing it**: the family, the size and the depth of the file, for an app that
// lays a page out before it paints it (docs/30).
bool image_info(const char* path, char* format, size_t format_cap, uint16_t& width, uint16_t& height,
                const char*& code, const char*& reason) {
  code = "E_INVAL";
  reason = nullptr;
  char full[128];
  if (!app_path(path, full, sizeof(full))) {
    reason = "that is not a path an app may name";
    return false;
  }
  hal::Stat info;
  if (!vfs::stat(full, info) || info.type != hal::NodeType::kFile) {
    reason = "there is no such picture";
    return false;
  }
  if (info.size == 0 || info.size > kImageFileBytes) {
    reason = "a picture is up to two megabytes";
    return false;
  }
  void* file = nullptr;
  {
    meter::Scope scope(meter::kExtraAlloc);
    file = pal::extra_memory().allocate(static_cast<size_t>(info.size) + 1, true);
  }
  if (file == nullptr) {
    reason = "there is no memory for the file";
    return false;
  }
  size_t read = 0;
  char* bytes = static_cast<char*>(file);
  const bool ok = vfs::read_file(full, bytes, static_cast<size_t>(info.size), read);
  img::Info picture;
  const img::Status status =
      ok ? img::read_info(reinterpret_cast<const uint8_t*>(bytes), static_cast<uint32_t>(read), picture)
         : img::kTruncated;
  pal::extra_memory().release(file);
  if (!ok) {
    reason = "the picture could not be read";
    return false;
  }
  if (status != img::kOk) {
    reason = image_why(status, code);
    return false;
  }
  code = nullptr;
  snprintf(format, format_cap, "%s", picture.format);
  width = picture.width;
  height = picture.height;
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
  //
  // **`du` and not `gc16`**: a label that changes is a press or a number, and `gc16` is the wash that flashes
  // the whole box — two of them, one when the finger goes down and one when it comes up, is what a button did
  // until 0.54.0 (measured on the Board, 2/11). A page that has to come out clean says so once, with
  // `ui.present('gc16')`, and the rest of the presses are quick (docs/50, docs/90).
  draw_fill_rect(*target.canvas, static_cast<uint16_t>(target.x), static_cast<uint16_t>(target.y),
                 width, height, background, hal::UpdateMode::kDu);
  if (text != nullptr && text[0] != '\0') {
    const font::Face& face = face_of(label.face);
    // **The line a label shows lives in a static block and not on the stack**: a kilobyte of the eight the
    // loop task has is not something a control that is drawn may borrow, and labels are drawn one at a time
    // (docs/75).
    static char shown[1024];
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
              hal::UpdateMode::kDu);
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
  const hal::UpdateMode mode = hal::UpdateMode::kDu;
  // **Twelve pixels of corner**, which is what a button of this panel looks like, and never more than a third
  // of the smaller side: a button that is all corner is a pill and not a button (docs/90).
  int32_t radius = 12;
  const int32_t most = ((width < height) ? width : height) / 3;
  if (radius > most) radius = most;
  // **The frame has weight**: three pixels of border on a button of a page, two on a small one and one on a
  // chip, and a bevel one pixel thinner than the border. A hairline frame reads as a box drawn round a word;
  // a frame with weight reads as something a finger can press, which is what a button is (docs/30, docs/90).
  int32_t thickness = 2;
  if (width > 24 && height > 24) thickness = 3;
  if (width <= 12 || height <= 12) thickness = 1;
  const int32_t bevel = (thickness > 1) ? thickness - 1 : 1;
  // **The body is a rounded box in the border colour with a rounded hole of the background in it**, which is
  // a frame of a given thickness whose corners are rounded on both sides (docs/90).
  draw_round_fill(*target.canvas, target.x, target.y, width, height, radius, border, mode);
  draw_round_fill(*target.canvas, target.x + thickness, target.y + thickness, width - 2 * thickness,
                  height - 2 * thickness, radius - thickness, background, mode);
  // **The volume**: a light edge on the top and the left, a shadow on the bottom and the right. While the
  // button is held the two swap, which is the whole difference between a button that is out and one that is
  // pushed in — and the two edges are **paper and ink and not a shade of the body** (0.85.4): a button whose bevel
  // is a grey is a button that has to be painted with sixteen levels and that leaves a grey ghost when it goes
  // (docs/50, docs/92, docs/90).
  const Bevel edge = bevel_of(background);
  const uint8_t light = edge.light;
  const uint8_t dark = edge.dark;
  const int32_t face_w = width - 2 * thickness;
  const int32_t face_h = height - 2 * thickness;
  if (!pressed) {
    for (int32_t i = 0; i < bevel; ++i) {
      draw_round_bevel(*target.canvas, target.x + thickness + i, target.y + thickness + i,
                       face_w - 2 * i, face_h - 2 * i, radius - thickness - i, light, dark, mode);
    }
  } else {
    // **A button that is pushed in is a hole, and a hole has a mouth**: the shadow on the top and the left is
    // **as many pixels of ink as the frame is thick** —of paper on a body of ink—, and the light goes to the bottom
    // and the right. More than one ring is the difference between "held" and "drawn twice" (docs/30, docs/90).
    const uint8_t deep = edge.deep;
    for (int32_t i = 0; i < thickness; ++i) {
      draw_round_bevel(*target.canvas, target.x + thickness + i, target.y + thickness + i,
                       face_w - 2 * i, face_h - 2 * i, radius - thickness - i, deep, light, mode);
    }
  }
  // **The focus is a ring a person cannot miss**: the two pixels of the bevel are the pause after the border,
  // and then a black band of seven —the border and four—, which is the double frame a selected control wears.
  // A ring that only doubles the border reads as a thicker border and not as "this is the one"; the pause is
  // what says there are two elements and the one inside is the focus (docs/30, docs/90).
  if (focused && width > 16 && height > 16) {
    const uint8_t ring = (background >= 8) ? 0 : 15;
    const int32_t short_side = (width < height) ? width : height;
    const int32_t gap = 2;
    const int32_t inset = thickness + gap;
    int32_t ring_px = thickness + 4;
    // Never more than the button can hold with a body of four pixels left: a ring that eats the button is
    // not a focus, it is a black square (docs/90).
    const int32_t room = short_side / 2 - inset - 2;
    if (ring_px > room) ring_px = room;
    if (ring_px < 2) ring_px = 2;
    const int32_t ring_radius = (radius > inset + 1) ? radius - inset : 2;
    const int32_t ring_width = width - 2 * inset;
    const int32_t ring_height = height - 2 * inset;
    draw_round_fill(*target.canvas, target.x + inset, target.y + inset, ring_width, ring_height,
                    ring_radius, ring, mode);
    draw_round_fill(*target.canvas, target.x + inset + ring_px, target.y + inset + ring_px,
                    ring_width - 2 * ring_px, ring_height - 2 * ring_px, ring_radius - ring_px,
                    background, mode);
  }
  // **The frame is opaque, and it covers everything inside it**: a child that did not redraw would be
  // erased by the button's own fill. The button has no buffer to compose under the child, so the child
  // is asked to draw again whenever the frame does (docs/30, docs/90).
  for (uint8_t i = 0; i < button.child_count; ++i) invalidate(button.children[i]);
  close_draw(target);
  return true;
}

int16_t checkbox_side(int8_t slot) {
  if (!live(slot)) return 0;
  const int32_t height = g_canvases[slot].height;
  // **Four pixels of frame above and below**, which is the room the ring of the focus needs: the ring is
  // drawn *outside* the box —two pixels of band and one of pause— and a box that touches the edge of its row
  // is a box whose ring is cut in half. A row too short for that gets two, and one shorter than six pixels
  // has no box at all (docs/90).
  int32_t inset = 4;
  if (height - 2 * inset < 8) inset = 2;
  const int32_t side = height - 2 * inset;
  if (side < 6) return 0;
  return static_cast<int16_t>(side);
}

bool checkbox_draw(int8_t slot, uint8_t background, uint8_t border, uint8_t mark, bool checked,
                   bool pressed, bool focused) {
  if (!live(slot) || background > 15 || border > 15 || mark > 15) return false;
  const Canvas& control = g_canvases[slot];
  const uint16_t width = control.width;
  const uint16_t height = control.height;
  if (width == 0 || height == 0) return true;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return false;
  const hal::UpdateMode mode = hal::UpdateMode::kDu;
  // **The row is paper and the box is the control**: what a checkbox owes is the whole of its frame —a
  // control without a buffer paints the pixels it covers—, but only the square on the left is a box. What is
  // to the right of it is the room the child that names the setting needs (docs/30, docs/90).
  draw_fill_rect(*target.canvas, static_cast<uint16_t>(target.x), static_cast<uint16_t>(target.y), width,
                 height, background, mode);
  const int32_t side = checkbox_side(slot);
  if (side > 0) {
    const int32_t inset = (static_cast<int32_t>(height) - side) / 2;
    const int32_t box_x = target.x;
    const int32_t box_y = target.y + inset;
    // **A small radius and not the twelve of a button**: at a quarter of the size, twelve pixels of corner is
    // a pill. Three is the corner a checkbox has on this panel (docs/90).
    int32_t radius = side / 6;
    if (radius > 3) radius = 3;
    // **The border has the weight of the thing it is**: three pixels on a box of a page, two on a small one and
    // one on a chip, exactly as the button counts it, and the bevel one pixel thinner (docs/30, docs/90).
    int32_t thickness = 2;
    if (side > 24) thickness = 3;
    if (side <= 12) thickness = 1;
    // **The focus is a ring outside the box**, two pixels of band and one of pause, and not a thicker border:
    // a box of twelve pixels with a border of two has no room for the double frame the button wears inside,
    // and a ring drawn inside would eat the tick. The row is the app's —the words beside the box— and an app
    // that wants them to say the focus colours them in `on_focus`, which is what the guide says a control
    // without a frame of its own does (docs/30, docs/90).
    if (focused && inset >= 3 && side >= 8) {
      const uint8_t ring = (background >= 8) ? 0 : 15;
      draw_round_fill(*target.canvas, box_x - 3, box_y - 3, side + 6, side + 6, radius + 3, ring, mode);
      draw_round_fill(*target.canvas, box_x - 1, box_y - 1, side + 2, side + 2, radius + 1, background,
                      mode);
    }
    // **Held, the box is a hole**: the bevel swaps and the body stays the two levels it had —a body that goes a step
    // darker is a grey, and a grey is the wash of sixteen levels for a tick—, which is the same language the button
    // speaks at a quarter of the size (0.85.4, docs/30, docs/92, docs/90).
    const uint8_t body = background;
    draw_round_fill(*target.canvas, box_x, box_y, side, side, radius, border, mode);
    draw_round_fill(*target.canvas, box_x + thickness, box_y + thickness, side - 2 * thickness,
                    side - 2 * thickness, (radius > thickness) ? radius - thickness : 0, body, mode);
    const int32_t inner = side - 2 * thickness;
    if (inner >= 10) {
      const int32_t bevel = (thickness > 1) ? thickness - 1 : 1;
      const Bevel edge = bevel_of(background);
      const uint8_t light = edge.light;
      const uint8_t dark = edge.dark;
      const uint8_t deep = edge.deep;
      const int32_t face_radius = (radius > thickness) ? radius - thickness : 0;
      if (!pressed) {
        for (int32_t i = 0; i < bevel; ++i) {
          draw_round_bevel(*target.canvas, box_x + thickness + i, box_y + thickness + i, inner - 2 * i,
                           inner - 2 * i, face_radius, light, dark, mode);
        }
      } else {
        for (int32_t i = 0; i < thickness; ++i) {
          draw_round_bevel(*target.canvas, box_x + thickness + i, box_y + thickness + i, inner - 2 * i,
                           inner - 2 * i, face_radius, deep, light, mode);
        }
      }
    }
    // **The tick is two strokes of a brush**, and not a font glyph and not a diagonal: the box of a page is
    // thirty pixels and its tick is five or six wide, which is what a person sees from a metre away. The
    // stroke is thinner than a third of the box, so a small checkbox still shows a V and not a blob
    // (docs/30, docs/90).
    if (checked && inner >= 8) {
      int32_t brush = inner / 5;
      if (brush > 6) brush = 6;
      if (brush < 2) brush = 2;
      const int32_t x0 = box_x + thickness;
      const int32_t y0 = box_y + thickness;
      const int32_t down_x = x0 + inner * 22 / 100;
      const int32_t down_y = y0 + inner * 55 / 100;
      const int32_t knee_x = x0 + inner * 43 / 100;
      const int32_t knee_y = y0 + inner * 82 / 100;
      const int32_t up_x = x0 + inner * 80 / 100;
      const int32_t up_y = y0 + inner * 22 / 100;
      for (int32_t i = 0; i < brush; ++i) {
        draw_line(*target.canvas, down_x + i, down_y + i, knee_x + i, knee_y + i, mark, mode);
        draw_line(*target.canvas, knee_x + i, knee_y + i, up_x + i, up_y + i, mark, mode);
      }
    }
  }
  // **The row is opaque too**: the child that names the setting was drawn after this control and the row
  // covers it, so a checkbox that redraws asks its children to draw again, as the button does (docs/30,
  // docs/90).
  for (uint8_t i = 0; i < control.child_count; ++i) invalidate(control.children[i]);
  close_draw(target);
  return true;
}

// ---- a progress bar (docs/90) ----
//
// **The track and the fill are the same rounded box and the fill is a prefix of it**, which is what makes the
// end of a bar a rounded end and not a cut, and it is two calls because a bar is the cheapest thing on the
// page: no border of three pixels, no ring, no bevel. A bar is the answer to "how much of this is done" and
// nothing about it says "press me" (docs/90).
bool progress_draw(int8_t slot, uint8_t background, uint8_t border, uint8_t track, uint8_t fill,
                   int16_t value) {
  if (!live(slot) || background > 15 || border > 15 || track > 15 || fill > 15) return false;
  const Canvas& bar = g_canvases[slot];
  const int32_t width = bar.width;
  const int32_t height = bar.height;
  if (width == 0 || height == 0) return true;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return false;
  const hal::UpdateMode mode = hal::UpdateMode::kDu;
  int32_t radius = height / 2;
  if (radius > 6) radius = 6;
  // **The frame is one pixel and it goes round the outside of the track**: a bar on a white page that has no
  // frame is a grey smear, and a bar with a frame of three pixels is a button that does nothing (docs/90).
  const int32_t thickness = (width > 8 && height > 8 && border != background) ? 1 : 0;
  draw_round_fill(*target.canvas, target.x, target.y, width, height, radius, background, mode);
  if (thickness != 0) {
    draw_round_fill(*target.canvas, target.x, target.y, width, height, radius, border, mode);
    draw_round_fill(*target.canvas, target.x + thickness, target.y + thickness, width - 2 * thickness,
                    height - 2 * thickness, radius - thickness, background, mode);
  }
  const int32_t pad = thickness + ((height > 12) ? 2 : 1);
  const int32_t inner_x = target.x + pad;
  const int32_t inner_y = target.y + pad;
  const int32_t inner_w = width - 2 * pad;
  const int32_t inner_h = height - 2 * pad;
  if (inner_w <= 0 || inner_h <= 0) {
    close_draw(target);
    return true;
  }
  int32_t inner_radius = inner_h / 2;
  if (inner_radius > 4) inner_radius = 4;
  draw_round_fill(*target.canvas, inner_x, inner_y, inner_w, inner_h, inner_radius, track, mode);
  // **A negative value fills nothing**: it is "the system does not know yet", and drawing it as a full bar
  // would be a page that says everything is done (docs/90).
  if (value > 0) {
    int32_t shown = value;
    if (shown > 100) shown = 100;
    int32_t filled = (inner_w * shown + 50) / 100;
    // **A bar that says something always shows something**: one per cent of a narrow bar is less than a
    // pixel, and a progress that says five while drawing nothing is a progress that lies (docs/90).
    const int32_t least = inner_radius * 2;
    if (filled < least && inner_w >= least) filled = least;
    draw_round_fill(*target.canvas, inner_x, inner_y, filled, inner_h, inner_radius, fill, mode);
  }
  close_draw(target);
  return true;
}

// ---- a slider (docs/90) ----
//
// **The system moves the number and the control only draws it**: the arrows of the dial change `value` by
// `step` while the slider has the focus (`luabind` does it, in `ui_button_event`), and the app hears about it
// at `on_change`. What is here is the geometry: a track from the middle of where the knob goes at the minimum
// to the middle of where it goes at the maximum —a track that runs to the edge leaves a knob half outside the
// frame—, the knob as a square of the height of the control, and the number in the right column, measured
// with the face the control was given (docs/30, docs/90).
bool slider_draw(int8_t slot, uint8_t background, uint8_t border, uint8_t track, uint8_t fill,
                 uint8_t text, int16_t value, int16_t minimum, int16_t maximum, const char* suffix,
                 uint8_t scale, bool pressed, bool focused) {
  if (!live(slot) || background > 15 || border > 15 || track > 15 || fill > 15 || text > 15) return false;
  const Canvas& slider = g_canvases[slot];
  const int32_t width = slider.width;
  const int32_t height = slider.height;
  if (width == 0 || height == 0) return true;
  if (scale == 0) scale = 1;
  if (scale > 16) scale = 16;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return false;
  const hal::UpdateMode mode = hal::UpdateMode::kDu;
  const font::Face& face = face_of(slider.face);
  // **The number and its unit**: `suffix` is the `%` or the ` C`, because a number without it means something
  // else —an app that wants another word beside it adds a label of its own, which is a child control—.
  if (maximum < minimum) {
    const int16_t swap = minimum;
    minimum = maximum;
    maximum = swap;
  }
  int32_t shown_value = value;
  if (shown_value < minimum) shown_value = minimum;
  if (shown_value > maximum) shown_value = maximum;
  char shown[32];
  snprintf(shown, sizeof(shown), "%d%s", static_cast<int>(shown_value),
           (suffix != nullptr) ? suffix : "");
  const int32_t text_w = font::width(face, shown, scale);
  const int32_t line_h =
      static_cast<int32_t>((face.height != 0) ? face.height : font::kGlyphHeight) * scale;
  // The frame is the one of a chip: rounded, one or two pixels of border, and the background inside it.
  int32_t radius = 8;
  const int32_t most = ((width < height) ? width : height) / 3;
  if (radius > most) radius = most;
  const int32_t thickness = (width > 24 && height > 16) ? 2 : 1;
  draw_round_fill(*target.canvas, target.x, target.y, width, height, radius, border, mode);
  draw_round_fill(*target.canvas, target.x + thickness, target.y + thickness, width - 2 * thickness,
                  height - 2 * thickness, radius - thickness, background, mode);
  const int32_t face_h = height - 2 * thickness;
  int32_t knob = face_h - 6;
  if (knob > 32) knob = 32;
  if (knob < 8) knob = 8;
  if (knob > face_h) knob = face_h;
  const int32_t column = (text_w > 0) ? text_w + 10 : 0;
  const int32_t left = target.x + thickness + knob / 2 + 2;
  const int32_t right = target.x + width - thickness - column - knob / 2 - 2;
  int32_t band = knob / 3;
  if (band < 3) band = 3;
  if (band > 10) band = 10;
  const int32_t middle = target.y + height / 2;
  if (right > left) {
    draw_round_fill(*target.canvas, left, middle - band / 2, right - left, band, band / 2, track, mode);
    int32_t position = left;
    if (maximum > minimum) {
      position = left + static_cast<int32_t>((static_cast<int64_t>(right - left) * (shown_value - minimum)) /
                                             (maximum - minimum));
    }
    if (position > left) {
      draw_round_fill(*target.canvas, left, middle - band / 2, position - left, band, band / 2, fill, mode);
    }
    // **The knob is a square with a hole**, the same language as a checkbox and a button: a border of two or
    // three pixels and the paper inside it. Held, it is a hole of its own colour and two pixels bigger, which
    // is the difference between "this is the knob" and "the finger is on it" (docs/90).
    int32_t side = knob + (pressed ? 2 : 0);
    if (side > face_h) side = face_h;
    const int32_t knob_x = position - side / 2;
    const int32_t knob_y = middle - side / 2;
    int32_t ring = (side > 20) ? 3 : 2;
    if (ring * 2 >= side) ring = 1;
    draw_round_fill(*target.canvas, knob_x, knob_y, side, side, 3, border, mode);
    draw_round_fill(*target.canvas, knob_x + ring, knob_y + ring, side - 2 * ring, side - 2 * ring, 2,
                    pressed ? fill : background, mode);
  }
  // **The number is in the right column and it is not cut**: the app asked for a slider of that width, so the
  // control keeps the room for the widest number it can show —the maximum— and not for the one it has now, or
  // the knob would jump when the value grows a digit (docs/90).
  if (text_w > 0) {
    draw_text(*target.canvas, face, target.x + width - thickness - 4 - text_w,
              middle - line_h / 2, shown, scale, text, mode);
  }
  // The focus is the double frame of a button, drawn inside the chip and with the ink colour of the control.
  if (focused && width > 16 && height > 16) {
    const int32_t inset = thickness + 2;
    const int32_t ring_px = 2;
    const int32_t ring_w = width - 2 * inset;
    const int32_t ring_h = height - 2 * inset;
    if (ring_w > 2 * ring_px + 4 && ring_h > 2 * ring_px + 2) {
      draw_round_fill(*target.canvas, target.x + inset, target.y + inset, ring_w, ring_h, 4, text, mode);
      draw_round_fill(*target.canvas, target.x + inset + ring_px, target.y + inset + ring_px,
                      ring_w - 2 * ring_px, ring_h - 2 * ring_px, 3, background, mode);
    }
  }
  close_draw(target);
  return true;
}

// ---- a row of a list (docs/90) ----
//
// **One line and the two columns of it**: the title on the left and the detail on the right, which is where a
// date or a size goes, and a subtitle under the title in the dim colour. Every line is cut with `"..."` at its
// own column —the ellipsis is the label's, so a row and a label cannot disagree— and a row that is **marked**
// is inverted, because on this glass "this is the one" is ink and paper swapped and not a grey (docs/90).
//
// **The painter is one function and the row and the list share it**: two painters of the same list always drift,
// and a list whose rows look almost like a `ui.row` is a page that looks almost right (docs/90).
void paint_row(Canvas& canvas, int32_t x, int32_t y, int32_t width, int32_t height, int8_t face_handle,
               const char* title, const char* detail, const char* subtitle, uint8_t text, uint8_t dim,
               uint8_t background, uint8_t scale, bool marked, bool separator, hal::UpdateMode mode) {
  if (width <= 0 || height <= 0) return;
  const font::Face& face = face_of(face_handle);
  // **Inverted, not greyed**: a marked row is the row of the page that the person is on, and the only two
  // levels that can say that at once are ink and paper (docs/90).
  const uint8_t paper = marked ? text : background;
  const uint8_t ink = marked ? background : text;
  const uint8_t faint = marked ? background : dim;
  draw_fill_rect(canvas, static_cast<uint16_t>(x), static_cast<uint16_t>(y), static_cast<uint16_t>(width),
                 static_cast<uint16_t>(height), paper, mode);
  // The lines live in a static block and not on the stack, for the reason a label's do: a control that is
  // drawn may not borrow the stack of the loop task (docs/75).
  static char lines[3][256];
  const int32_t pad = (width > 24) ? 8 : 4;
  const int32_t line_h =
      static_cast<int32_t>((face.height != 0) ? face.height : font::kGlyphHeight) * scale;
  const bool has_detail = detail != nullptr && detail[0] != '\0';
  const bool has_subtitle = subtitle != nullptr && subtitle[0] != '\0';
  const int32_t detail_w = has_detail ? font::width(face, detail, scale) : 0;
  const int32_t room = width - 2 * pad - ((detail_w > 0) ? detail_w + 12 : 0);
  if (title != nullptr && title[0] != '\0' && room > 0) {
    label_text(face_handle, title, static_cast<uint16_t>(room), scale, lines[0], sizeof(lines[0]));
    const int32_t lines_used = has_subtitle ? 2 : 1;
    const int32_t block = lines_used * line_h + ((lines_used > 1) ? 2 : 0);
    const int32_t top = y + (height - block) / 2;
    draw_text(canvas, face, x + pad, top, lines[0], scale, ink, mode);
    if (has_subtitle) {
      label_text(face_handle, subtitle, static_cast<uint16_t>(room), scale, lines[1], sizeof(lines[1]));
      draw_text(canvas, face, x + pad, top + line_h + 2, lines[1], scale, faint, mode);
    }
  }
  if (has_detail) {
    // The detail is right-aligned and cut to what is left after the title's column, so the two of them never
    // overlap: a row whose size runs into its name is a row that cannot be read (docs/90).
    label_text(face_handle, detail, static_cast<uint16_t>(detail_w), scale, lines[2], sizeof(lines[2]));
    const int32_t box = has_subtitle ? 2 * line_h + 2 : line_h;
    const int32_t top = y + (height - box) / 2;
    draw_text(canvas, face, x + width - pad - detail_w, top, lines[2], scale, faint, mode);
  }
  if (separator && height > 2) {
    put_rect(canvas, x + pad, y + height - 1, width - 2 * pad, 1, faint);
    mark_box(canvas, x + pad, y + height - 1, width - 2 * pad, 1, mode);
  }
}

bool row_draw(int8_t slot, const char* title, const char* detail, const char* subtitle, uint8_t text,
              uint8_t dim, uint8_t background, uint8_t scale, bool marked, bool separator) {
  if (!live(slot) || text > 15 || dim > 15 || background > 15) return false;
  const Canvas& row = g_canvases[slot];
  if (row.width == 0 || row.height == 0) return true;
  if (scale == 0) scale = 1;
  if (scale > 16) scale = 16;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return false;
  paint_row(*target.canvas, target.x, target.y, row.width, row.height, row.face, title, detail, subtitle,
            text, dim, background, scale, marked, separator, hal::UpdateMode::kDu);
  close_draw(target);
  return true;
}

// ---- a panel: the room that arranges what it holds (docs/90) ----

bool canvas_resize(int8_t slot, uint16_t width, uint16_t height) {
  Canvas* target = find(slot);
  if (target == nullptr || width == 0 || height == 0) return false;
  // **A control with a buffer is not resized here**: its pixels are a block of the extra memory handed out
  // with a size, and a size that changes is a release and a new canvas, which is the app's to ask (docs/90).
  if (target->bits != nullptr) return false;
  if (target->width == width && target->height == height) return true;
  target->width = width;
  target->height = height;
  invalidate(slot);
  return true;
}

uint8_t panel_layout(int8_t slot, PanelFlow flow, uint16_t padding, uint16_t gap,
                     const PanelChild* children, uint8_t count) {
  if (!live(slot) || children == nullptr || count == 0 || count > 16) return 0;
  const Canvas& panel = g_canvases[slot];
  const int32_t inner_w = static_cast<int32_t>(panel.width) - 2 * static_cast<int32_t>(padding);
  const int32_t inner_h = static_cast<int32_t>(panel.height) - 2 * static_cast<int32_t>(padding);
  if (inner_w <= 0 || inner_h <= 0) return 0;

  // **Only the children that are still this panel's** are arranged: a control that moved to another panel is
  // not this one's business, and counting it would put a gap where nobody is (docs/90).
  int8_t mine[16];
  uint8_t weights[16];
  uint8_t used = 0;
  for (uint8_t i = 0; i < count; ++i) {
    const Canvas* child = find(children[i].slot);
    if (child == nullptr || child->parent != slot) continue;
    mine[used] = children[i].slot;
    weights[used] = children[i].weight;
    ++used;
  }
  if (used == 0) return 0;

  uint8_t moved = 0;
  if (flow == PanelFlow::kOverlay) {
    for (uint8_t i = 0; i < used; ++i) {
      if (place(mine[i], padding, padding)) ++moved;
    }
    return moved;
  }

  const bool vertical = (flow == PanelFlow::kVertical);
  const int32_t span = vertical ? inner_h : inner_w;
  const int32_t across = vertical ? inner_w : inner_h;
  int32_t fixed = 0;
  int32_t total_weight = 0;
  uint8_t weighted = 0;
  for (uint8_t i = 0; i < used; ++i) {
    const Canvas& child = g_canvases[mine[i]];
    const int32_t size = vertical ? child.height : child.width;
    if (weights[i] == 0) {
      fixed += size;
    } else {
      total_weight += weights[i];
      ++weighted;
    }
  }
  const int32_t gaps = static_cast<int32_t>(gap) * ((used > 1) ? (used - 1) : 0);
  int32_t free_room = span - fixed - gaps;
  if (free_room < 0) free_room = 0;
  int32_t given = 0;
  int32_t cursor = static_cast<int32_t>(padding);
  for (uint8_t i = 0; i < used; ++i) {
    Canvas& child = g_canvases[mine[i]];
    int32_t size = vertical ? child.height : child.width;
    if (weights[i] > 0 && total_weight > 0) {
      // **The last one takes the remainder**, or a hundred pixels of rounding leave a strip of page at the
      // bottom of the panel that nobody asked for (docs/90).
      size = (weighted <= 1) ? (free_room - given) : ((free_room * weights[i]) / total_weight);
      if (size < 0) size = 0;
      given += size;
      --weighted;
    }
    // Across the flow a child is as big as the panel —a row of a column is as wide as the column— and along
    // it, the size it was given. A control with a buffer keeps its own size and only moves.
    const uint16_t new_w = static_cast<uint16_t>(vertical ? across : size);
    const uint16_t new_h = static_cast<uint16_t>(vertical ? size : across);
    canvas_resize(mine[i], new_w, new_h);
    const uint16_t x = static_cast<uint16_t>(vertical ? padding : cursor);
    const uint16_t y = static_cast<uint16_t>(vertical ? cursor : padding);
    if (place(mine[i], x, y)) ++moved;
    cursor += size + static_cast<int32_t>(gap);
  }
  return moved;
}

// ---- a strip of tabs (docs/90) ----
//
// **Equal columns and a line that is missing under the chosen one**: on two tones there is no bold and no
// colour, so "you are here" is said by taking the line away instead of by adding anything, and a tab that is
// drawn with a line under it is a tab that is not the one (docs/90).
bool tabs_draw(int8_t slot, const char* const* labels, uint8_t count, uint8_t index, uint8_t text,
               uint8_t background, uint8_t scale) {
  if (!live(slot) || labels == nullptr || count == 0 || text > 15 || background > 15) return false;
  const Canvas& strip = g_canvases[slot];
  const int32_t width = strip.width;
  const int32_t height = strip.height;
  if (width == 0 || height == 0) return true;
  if (scale == 0) scale = 1;
  if (scale > 16) scale = 16;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return false;
  const hal::UpdateMode mode = hal::UpdateMode::kDu;
  const font::Face& face = face_of(strip.face);
  draw_fill_rect(*target.canvas, static_cast<uint16_t>(target.x), static_cast<uint16_t>(target.y),
                 static_cast<uint16_t>(width), static_cast<uint16_t>(height), background, mode);
  const int32_t column = width / count;
  int32_t line = 2;
  if (line > height / 4) line = (height / 4 > 0) ? height / 4 : 1;
  const int32_t line_y = target.y + height - line;
  const int32_t line_h =
      static_cast<int32_t>((face.height != 0) ? face.height : font::kGlyphHeight) * scale;
  static char shown[128];
  for (uint8_t i = 0; i < count; ++i) {
    const int32_t x0 = target.x + static_cast<int32_t>(i) * column;
    const int32_t x1 = (i + 1 == count) ? (target.x + width) : (x0 + column);
    // The word, in the middle of its column and above the line.
    if (labels[i] != nullptr && labels[i][0] != '\0' && (x1 - x0) > 8) {
      const uint16_t taken = label_text(strip.face, labels[i], static_cast<uint16_t>(x1 - x0 - 8), scale,
                                        shown, sizeof(shown));
      const int32_t at_x = x0 + ((x1 - x0) - static_cast<int32_t>(taken)) / 2;
      const int32_t at_y = target.y + (height - line - line_h) / 2;
      draw_text(*target.canvas, face, at_x, at_y, shown, scale, text, mode);
    }
    // The line, everywhere but under the chosen tab: that missing piece is the whole signal.
    if (static_cast<uint8_t>(i + 1) != index) {
      put_rect(*target.canvas, x0, line_y, x1 - x0, line, text);
      mark_box(*target.canvas, x0, line_y, x1 - x0, line, mode);
    }
  }
  close_draw(target);
  return true;
}

// ---- a list (docs/90) ----

namespace {

// **The band of the footer**: a third of a row, with a floor of twenty pixels so a small row still has a footer
// a person can read and a ceiling of thirty-six so a tall row does not give half of itself to it (docs/90).
uint16_t list_footer(uint16_t row_height) {
  int32_t band = static_cast<int32_t>(row_height) / 3;
  if (band < 20) band = 20;
  if (band > 36) band = 36;
  return static_cast<uint16_t>(band);
}

// How many rows of that height fit in that height at all, cut at thirty-two: a page longer than that is a page
// nobody reads (docs/90).
uint8_t rows_fitting(uint16_t height, uint16_t row_height) {
  if (row_height == 0) return 0;
  int32_t rows = static_cast<int32_t>(height) / static_cast<int32_t>(row_height);
  if (rows > 32) rows = 32;
  return static_cast<uint8_t>((rows > 0) ? rows : 0);
}

}  // namespace

ListPage list_page_of(uint16_t height, uint16_t row_height, uint16_t items) {
  ListPage out = {0, 0};
  if (row_height == 0 || items == 0) return out;
  const uint8_t whole = rows_fitting(height, row_height);
  if (whole == 0) return out;
  const uint32_t loose = (static_cast<uint32_t>(items) + whole - 1) / whole;
  if (loose <= 1) {
    // **A list that fits says nothing about pages**: every row is on the one page and no band is taken from it.
    out.rows = whole;
    out.pages = 1;
    return out;
  }
  // The footer takes its band, and the rows are counted again in what is left: the band can cost a row, and a
  // list that counted without it would draw a row under the footer (docs/90).
  const int32_t room = static_cast<int32_t>(height) - static_cast<int32_t>(list_footer(row_height));
  uint8_t rows = (room > 0) ? static_cast<uint8_t>(room / static_cast<int32_t>(row_height)) : 0;
  if (rows == 0) rows = 1;
  uint32_t pages = (static_cast<uint32_t>(items) + rows - 1) / rows;
  if (pages > 255) pages = 255;
  out.rows = rows;
  out.pages = static_cast<uint8_t>(pages);
  return out;
}

bool list_draw(int8_t slot, uint8_t count, uint8_t page, uint8_t pages, uint8_t marked,
               const char* const* titles, const char* const* details, const char* const* subtitles,
               uint16_t row_height, uint8_t text, uint8_t dim, uint8_t background, uint8_t scale,
               bool separator, bool focused) {
  // **An empty list still owes its background**: a frame with nothing in it is white paper and not a hole where
  // the page before it shows through, which is the same rule a label follows (docs/90).
  if (!live(slot) || (count > 0 && titles == nullptr) || row_height == 0 || text > 15 || dim > 15 ||
      background > 15) {
    return false;
  }
  const Canvas& list = g_canvases[slot];
  const int32_t width = list.width;
  const int32_t height = list.height;
  if (width == 0 || height == 0) return true;
  if (scale == 0) scale = 1;
  if (scale > 16) scale = 16;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return false;
  const hal::UpdateMode mode = hal::UpdateMode::kDu;
  draw_fill_rect(*target.canvas, static_cast<uint16_t>(target.x), static_cast<uint16_t>(target.y),
                 static_cast<uint16_t>(width), static_cast<uint16_t>(height), background, mode);
  const int32_t footer = (pages > 1) ? static_cast<int32_t>(list_footer(row_height)) : 0;
  const int32_t last_row = height - footer;
  for (uint8_t i = 0; i < count; ++i) {
    const int32_t row_y = static_cast<int32_t>(i) * static_cast<int32_t>(row_height);
    // **A row never goes under the footer**: the page is the rows that fit and the app was told how many, so a
    // row that would not fit is a row of the next page and not a row half drawn (docs/90).
    if (row_y + static_cast<int32_t>(row_height) > last_row) break;
    paint_row(*target.canvas, target.x, target.y + row_y, width, static_cast<int32_t>(row_height), list.face,
              (titles[i] != nullptr) ? titles[i] : "",
              (details != nullptr) ? details[i] : nullptr,
              (subtitles != nullptr) ? subtitles[i] : nullptr, text, dim, background, scale,
              static_cast<uint8_t>(i + 1) == marked, separator && (i + 1 < count), mode);
  }
  if (footer > 0) {
    const int32_t band_y = target.y + height - footer;
    put_rect(*target.canvas, target.x, band_y, width, 1, dim);
    mark_box(*target.canvas, target.x, band_y, width, 1, mode);
    const font::Face& face = face_of(list.face);
    const int32_t line_h =
        static_cast<int32_t>((face.height != 0) ? face.height : font::kGlyphHeight) * scale;
    char shown[24];
    snprintf(shown, sizeof(shown), "%u / %u", static_cast<unsigned>(page), static_cast<unsigned>(pages));
    const uint16_t taken = font::width(face, shown, scale);
    const int32_t at_y = band_y + (footer - line_h) / 2;
    draw_text(*target.canvas, face, target.x + (width - static_cast<int32_t>(taken)) / 2, at_y, shown, scale,
              text, mode);
    // **A chevron is there only when that way exists**: an arrow that does nothing is a promise the page does not
    // keep, and the two of them are the same words as the keys of the dial (docs/90).
    const int32_t edge = (width > 40) ? 10 : 4;
    if (page > 1) draw_text(*target.canvas, face, target.x + edge, at_y, "<", scale, text, mode);
    if (page < pages) {
      const uint16_t arrow = font::width(face, ">", scale);
      draw_text(*target.canvas, face, target.x + width - edge - static_cast<int32_t>(arrow), at_y, ">", scale,
                text, mode);
    }
  }
  // **The focus is the frame of the list**, and it is drawn last so the rows do not paint over it: a list that
  // has the focus is the one the arrows of the dial turn (docs/90).
  if (focused && width > 8 && height > 8) {
    put_rect(*target.canvas, target.x, target.y, width, 2, text);
    put_rect(*target.canvas, target.x, target.y + height - 2, width, 2, text);
    put_rect(*target.canvas, target.x, target.y, 2, height, text);
    put_rect(*target.canvas, target.x + width - 2, target.y, 2, height, text);
    mark_box(*target.canvas, target.x, target.y, width, height, mode);
  }
  close_draw(target);
  return true;
}

// ---- a text box: the window of the text and the caret (docs/90) ----

// **The caret is a block of the size of a letter** (0.92.0): under a letter it is that letter, and at the end of the
// text —where there is no letter to take the width from— it is an `M`, the widest of the small ones, instead of the
// three-pixel hairline the first boxes drew. A cursor a person has to look for is a cursor that is not there
// (docs/30, docs/90).
static int32_t caret_cell_width(const font::Face& face, const char* under, int32_t under_bytes, uint8_t scale) {
  int32_t width = 0;
  if (under_bytes > 0 && under != nullptr) {
    width = static_cast<int32_t>(font::width(face, under, scale));
  } else {
    width = static_cast<int32_t>(font::width(face, "M", scale));
  }
  if (width < 2) width = 2;
  return width;
}

bool textbox_draw(int8_t slot, const char* text, uint16_t cursor, bool caret, bool mask, uint8_t color,
                  uint8_t background, uint8_t border, uint8_t scale) {
  if (!live(slot) || color > 15 || background > 15 || border > 15) return false;
  const Canvas& box = g_canvases[slot];
  const int32_t width = box.width;
  const int32_t height = box.height;
  if (width == 0 || height == 0) return true;
  if (scale == 0) scale = 1;
  if (scale > 16) scale = 16;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return false;
  const hal::UpdateMode mode = hal::UpdateMode::kDu;
  const font::Face& face = face_of(box.face);
  // The frame is the one of a chip, the same as a slider: it is a place a person types into and not a button.
  int32_t radius = 8;
  const int32_t most = ((width < height) ? width : height) / 3;
  if (radius > most) radius = most;
  const int32_t thickness = (width > 24 && height > 16) ? 2 : 1;
  draw_round_fill(*target.canvas, target.x, target.y, width, height, radius, border, mode);
  draw_round_fill(*target.canvas, target.x + thickness, target.y + thickness, width - 2 * thickness,
                  height - 2 * thickness, radius - thickness, background, mode);
  const int32_t pad = thickness + 6;
  const int32_t inner_x = target.x + pad;
  const int32_t inner_w = width - 2 * pad;
  const int32_t line_h =
      static_cast<int32_t>((face.height != 0) ? face.height : font::kGlyphHeight) * scale;
  const int32_t top = target.y + (height - line_h) / 2;
  close_draw(target);
  if (inner_w <= 0 || text == nullptr) return true;
  const int32_t length = static_cast<int32_t>(strlen(text));
  int32_t at = cursor;
  if (at < 0) at = 0;
  if (at > length) at = length;
  // **A masked box is the same box with dots**: the dots are the letters in their place, so the caret lands where
  // it would with the letters and the width of what is typed does not leak (docs/30).
  static char shown[512];
  const int32_t shown_length = (length < static_cast<int32_t>(sizeof(shown)) - 1)
                                   ? length
                                   : static_cast<int32_t>(sizeof(shown)) - 1;
  for (int32_t i = 0; i < shown_length; ++i) shown[i] = mask ? '.' : text[i];
  shown[shown_length] = '\0';
  // **The width of the caret is the width of the letter under it**, and at the end of the text it is a letter's
  // block: a block that does not fit the letter it sits on is a block that hides the letter after it, and a hairline
  // is a block nobody sees (0.92.0, docs/90).
  char under[8] = {0};
  int32_t under_bytes = 0;
  if (at < shown_length) {
    under_bytes = 1;
    // A UTF-8 sequence is walked whole: half of a letter is a letter that is not there.
    while (at + under_bytes < shown_length && (shown[at + under_bytes] & 0xC0) == 0x80 &&
           under_bytes < 3) {
      ++under_bytes;
    }
    for (int32_t i = 0; i < under_bytes; ++i) under[i] = shown[at + i];
    under[under_bytes] = '\0';
  }
  int32_t caret_w = caret_cell_width(face, under, under_bytes, scale);
  // **The window**: the text is drawn from a byte that keeps the caret on the glass. The search drops one letter
  // at a time from the front, which is what a person sees: the beginning of a long line goes away as the end is
  // typed, and the caret is always where the next letter goes (docs/90).
  const DrawTarget window = open_draw(slot);
  if (window.canvas == nullptr) return true;
  static char head[512];
  int32_t from = 0;
  for (;;) {
    const int32_t taken = (at > from) ? at - from : 0;
    if (taken >= static_cast<int32_t>(sizeof(head))) break;
    memcpy(head, shown + from, static_cast<size_t>(taken));
    head[taken] = '\0';
    const int32_t before = static_cast<int32_t>(font::width(face, head, scale));
    if (before + caret_w <= inner_w || from >= at) break;
    // The next letter of the front goes, and never half of one.
    ++from;
    while (from < at && (shown[from] & 0xC0) == 0x80) ++from;
  }
  const int32_t taken = (at > from) ? at - from : 0;
  memcpy(head, shown + from, static_cast<size_t>((taken < static_cast<int32_t>(sizeof(head)))
                                                    ? taken
                                                    : static_cast<int32_t>(sizeof(head)) - 1));
  head[(taken < static_cast<int32_t>(sizeof(head))) ? taken : static_cast<int32_t>(sizeof(head)) - 1] = '\0';
  const int32_t before = static_cast<int32_t>(font::width(face, head, scale));
  // What is on the glass is the window and the caret: the tail is cut at the frame by the same width budget, or a
  // long text would run under the border of the box (docs/90).
  //
  // **The tail starts empty** (0.84.1): it is a buffer kept between paints —a control draws on the stack of the turn,
  // and half a kilobyte of text in it is half a kilobyte the board does not always have— and it was never emptied, so
  // every paint **appended** the text it was given to the text of the one before: `run typing` with five keys showed
  // "h", then "hhe", then "hhehel", and a box that looked like a ghost of every word typed so far (found on the bench,
  // 4/11, docs/90).
  //
  // **And what comes after the caret** (0.87.0): the rest of the text from the caret on, measured against what is
  // left of the window. It used to be copied **from the start of the window** and the head was never drawn at all, so
  // a box whose text is wider than the window showed its first letters cut in half with the caret alone at the right
  // —`/data/editor/note.txt` came out as `/data/editor/not` with the caret at the end—; a box with little text never
  // showed it, because then the rest was the whole text (found in the graphical editor, 4/11).
  static char rest[512];
  rest[0] = '\0';
  const int32_t after_caret = (caret) ? caret_w : 0;
  const int32_t room = inner_w - before - after_caret;
  const int32_t tail_from = at + under_bytes;
  int32_t fit = 0;
  if (room > 0) {
    // How many bytes of the rest fit: measured a letter at a time, and a letter that does not fit stops the line.
    //
    // **The width of the run is carried and not measured again** (0.88.13): the loop asked `font::width` for the whole
    // accumulated tail after every letter it added —O(n²) in the letters the window shows— and `strlen` for the same
    // buffer. `pen` is how far the pen has moved (a face of bits moves a cell and its gap for every byte it draws, a
    // face of outlines the advance of the glyph) and what the run is worth without the separation after its last
    // letter is `pen` minus that gap, which is exactly the width the whole string would answer. It is the same
    // accumulation `advance_of` does for a row of a box of several lines (docs/90, docs/75).
    const int32_t gap = (face.outline != nullptr) ? 0 : static_cast<int32_t>(face.gap) * scale;
    int32_t pen = 0;
    char probe[8];
    while (tail_from + fit < shown_length) {
      int32_t step = 1;
      while (tail_from + fit + step < shown_length && (shown[tail_from + fit + step] & 0xC0) == 0x80 && step < 3) ++step;
      memcpy(probe, shown + tail_from + fit, static_cast<size_t>(step));
      probe[step] = '\0';
      const int32_t one = static_cast<int32_t>(font::width(face, probe, scale));
      const int32_t run = (pen > 0) ? pen - gap : 0;
      if (run + one > room) break;
      // **`rest` holds `fit` bytes and no others**, so its length is a number the loop already has (docs/90).
      if (static_cast<size_t>(fit) + static_cast<size_t>(step) + 1 >= sizeof(rest)) break;
      memcpy(rest + fit, probe, static_cast<size_t>(step));
      rest[fit + step] = '\0';
      pen += one + gap;
      fit += step;
    }
  }
  // **The head and the rest, each in its own place**: what is before the caret from the left edge, and what is after
  // it right behind the caret, or the two would be drawn on top of each other (docs/90).
  if (head[0] != '\0') {
    draw_text(*window.canvas, face, inner_x, top, head, scale, color, mode);
  }
  // **The caret is a block of ink with the letter under it in paper**: a filled block would hide the letter the
  // person is about to type over, and an outline would be a box drawn on a box (docs/90).
  if (caret) {
    int32_t caret_x = inner_x + before;
    int32_t cell = caret_w;
    if (caret_x + cell > inner_x + inner_w) cell = inner_x + inner_w - caret_x;
    if (cell > 0) {
      put_rect(*window.canvas, caret_x, top, cell, line_h, color);
      mark_box(*window.canvas, caret_x, top, cell, line_h, mode);
      if (under_bytes > 0) {
        draw_text(*window.canvas, face, caret_x, top, under, scale, background, mode);
      }
    }
  }
  if (rest[0] != '\0') {
    draw_text(*window.canvas, face, inner_x + before + after_caret, top, rest, scale, color, mode);
  }
  close_draw(window);
  return true;
}

uint16_t textbox_inside(int8_t slot, uint8_t scale) {
  if (!live(slot)) return 0;
  const Canvas& box = g_canvases[slot];
  if (scale == 0) scale = 1;
  const int32_t thickness = (box.width > 24 && box.height > 16) ? 2 : 1;
  const int32_t inside = static_cast<int32_t>(box.width) - 2 * (thickness + 6);
  return static_cast<uint16_t>((inside > 0) ? inside : 0);
}

// ---- a box of several lines: the same box, with room to fall (docs/30, docs/90) ----

namespace {

// **How far the pen moves after a letter**: a face of bits moves the same for every one of them —its cell and the gap—
// and a face of outlines has its own advance per glyph, which is the width of two of them minus the width of one
// (docs/90).
int32_t advance_of(const font::Face& face, uint8_t c, uint8_t scale) {
  if (face.outline == nullptr) {
    return static_cast<int32_t>(face.width + face.gap) * scale;
  }
  const char twice[3] = {static_cast<char>(c), static_cast<char>(c), '\0'};
  const char once[2] = {static_cast<char>(c), '\0'};
  return static_cast<int32_t>(font::width(face, twice, scale)) -
         static_cast<int32_t>(font::width(face, once, scale));
}

// **Where a row ends**: at a '\n' —which is the end of the row, and the next row starts after it— and, when the app asked
// for wrapping, at the last space that fits the inside of the box. `taken` is how many bytes of the text the row
// consumes with the break, and `caret_x` is where the caret lands when the cursor is `at_cursor` bytes into this row —
// `caret_here` says whether it is this row's caret at all— (docs/90).
int32_t row_span(const font::Face& face, const char* at, int32_t room, uint8_t scale, bool wrap, int32_t at_cursor,
                 int32_t& taken, int32_t& caret_x, bool& caret_here) {
  taken = 0;
  caret_x = 0;
  caret_here = false;
  int32_t used = 0;
  int32_t last_space = -1;
  int32_t w = 0;
  while (at[used] != '\0' && at[used] != '\n') {
    const int32_t step = advance_of(face, static_cast<uint8_t>(at[used]), scale);
    if (wrap && at[used] == ' ' && w + step <= room) last_space = used;
    // **A row is never empty**: a letter wider than the box is drawn and cut at the border, because a box narrower than
    // a letter that painted nothing at all would be a box nobody can type into (docs/90).
    // **The width only ends a row when the app asked for wrapping**: with it off a row is a line, and a line longer than the
    // box is cut at its border by the paint and not broken by the arithmetic of the window (docs/90).
    if (wrap && used > 0 && w + step > room) {
      if (last_space > 0) {
        used = last_space;
        w = 0;
        for (int32_t i = 0; i < used; ++i) w += advance_of(face, static_cast<uint8_t>(at[i]), scale);
      }
      break;
    }
    w += step;
    ++used;
    if (used == at_cursor) {
      caret_x = w;
      caret_here = true;
    }
  }
  // **The two ends of a row, and neither of them is "anywhere inside it"** (0.84.1): the letters of a row are counted
  // from one, so the loop above sees the cursor of every position but the first, and the fallback that closes the row
  // said `<= used` —which put the cursor of the first letter at the end of the row, and a box with the caret at the
  // start drew its block after the whole first line—. The start is zero and the end is the width of the row; anything
  // else was already counted by the loop (docs/90).
  if (!caret_here && at_cursor == 0) {
    caret_x = 0;
    caret_here = true;
  } else if (!caret_here && at_cursor == used) {
    // The cursor is at the end of the row, which is where it is when it is about to type the letter that did not fit.
    caret_x = w;
    caret_here = true;
  }
  // **The space a wrapped row breaks on is the break itself** and is not drawn, so the next row starts after it (docs/90).
  const bool ends = (at[used] == '\n') || (wrap && at[used] == ' ');
  taken = used + (ends ? 1 : 0);
  return used;
}

// **The bytes of a row that fit the inside of the box**, measured a letter at a time: with the wrapping off a row is the
// whole line and a line longer than the box is cut at its border and not under it (docs/90).
int32_t row_fit(const font::Face& face, const char* at, int32_t span, int32_t room, uint8_t scale, char* out,
                size_t cap) {
  int32_t w = 0;
  int32_t used = 0;
  while (used < span && static_cast<size_t>(used) + 2 < cap) {
    const int32_t step = advance_of(face, static_cast<uint8_t>(at[used]), scale);
    if (used > 0 && w + step > room) break;
    w += step;
    out[used] = at[used];
    ++used;
  }
  out[used] = '\0';
  return used;
}

// **The box of several lines, measured once**: the frame, the air, the rows that fit, the window and the bar that says
// where in the text the window is. The paint, the finger and the swipe ask this, or the caret of one and the bar of
// the other disagree (docs/90).
struct MultilineLayout {
  int32_t thickness;
  int32_t pad;
  int32_t room;
  int32_t line_h;
  int32_t fit;
  int32_t total;
  int32_t first;
  int32_t cursor_row;
  int32_t caret_x;
  // **The scroll bar** (0.88.0): it is not there when the text fits in the box —a bar that is always full says
  // nothing— and, when it is, it takes room from the text on the right side. `bar_w` at zero is that there is none
  // (docs/90).
  int32_t bar_w;
  int32_t bar_x;
  int32_t bar_top;
  int32_t bar_h;
  int32_t thumb_y;
  int32_t thumb_h;
};

// Every row of the text, counted with the room the window gives it: **one walk and the same `row_span` the paint
// uses**, so the row the bar says is the row the letters are on (docs/90).
void multiline_measure(const font::Face& face, const char* at, int32_t room, uint8_t scale, bool wrap,
                       int32_t at_byte, MultilineLayout& out) {
  int32_t row = 0;
  int32_t offset = 0;
  do {
    int32_t taken = 0;
    int32_t caret_x = 0;
    bool here = false;
    const int32_t span = row_span(face, at, room, scale, wrap, at_byte - offset, taken, caret_x, here);
    if (here) {
      out.cursor_row = row;
      out.caret_x = caret_x;
    }
    ++row;
    (void)span;
    at += taken;
    offset += taken;
  } while (at[0] != '\0' && row < 4096);
  out.total = row;
}

MultilineLayout multiline_layout(const Canvas& box, const char* text, uint16_t cursor, bool wrap, uint8_t scale,
                                 int16_t top) {
  MultilineLayout out = {};
  const font::Face& face = face_of(box.face);
  out.thickness = (box.width > 24 && box.height > 16) ? 2 : 1;
  out.pad = out.thickness + 6;
  const int32_t full = static_cast<int32_t>(box.width) - 2 * out.pad;
  out.room = full;
  out.line_h = static_cast<int32_t>((face.height != 0) ? face.height : font::kGlyphHeight) * scale;
  if (out.line_h <= 0) out.line_h = 1;
  out.fit = (static_cast<int32_t>(box.height) - 2 * out.pad) / out.line_h;
  if (out.fit < 1) out.fit = 1;
  if (out.fit > 255) out.fit = 255;
  const char* at = (text != nullptr) ? text : "";
  const int32_t length = static_cast<int32_t>(strlen(at));
  const int32_t at_byte = (cursor > length) ? length : static_cast<int32_t>(cursor);
  multiline_measure(face, at, out.room, scale, wrap, at_byte, out);
  // **A bar that eats the text is not a bar**: when the text does not fit, the right side is its and the measurement is
  // made a second time —the window is narrower, and the text that fits makes more rows, not fewer— (docs/90).
  const int32_t bar_w = (scale > 1) ? 8 : 5;
  const int32_t gap = (scale > 1) ? 6 : 4;
  if (out.total > out.fit && full - bar_w - gap > 8) {
    out.bar_w = bar_w;
    out.room = full - bar_w - gap;
    multiline_measure(face, at, out.room, scale, wrap, at_byte, out);
    out.bar_x = static_cast<int32_t>(box.width) - out.pad - bar_w;
    out.bar_top = out.pad;
    out.bar_h = out.fit * out.line_h;
    // **The thumb is as long as the part of the text that is seen**, and it walks the rest of the track: two tones
    // and an arithmetic the finger understands without counting pixels (docs/90).
    int32_t thumb = (out.total > 0) ? (out.bar_h * out.fit) / out.total : out.bar_h;
    if (thumb < out.line_h) thumb = out.line_h;
    if (thumb > out.bar_h) thumb = out.bar_h;
    out.thumb_h = thumb;
  }
  // **The window follows the cursor unless the app said where it is**: the caret is always on the glass, and when the
  // text is shorter than the box the window is the first row, so the lines a person wrote stay at the top where they
  // left them. **A `top` of the app rules over the cursor** (0.88.0), which is what keeps a bar moved by the finger from
  // going back to the cursor on the next paint (docs/30, docs/90).
  if (top >= 0) {
    out.first = top;
  } else if (out.cursor_row >= out.fit) {
    out.first = out.cursor_row - out.fit + 1;
  }
  if (out.first > out.total - out.fit) out.first = out.total - out.fit;
  if (out.first < 0) out.first = 0;
  if (out.bar_w > 0) {
    const int32_t span = out.bar_h - out.thumb_h;
    const int32_t most = out.total - out.fit;
    out.thumb_y = out.bar_top + ((most > 0) ? (span * out.first) / most : 0);
  }
  return out;
}

}  // namespace

bool textbox_multiline_draw(int8_t slot, const char* text, uint16_t cursor, bool caret, bool wrap, uint8_t color,
                            uint8_t background, uint8_t border, uint8_t scale, int16_t top) {
  if (!live(slot) || color > 15 || background > 15 || border > 15) return false;
  const Canvas& box = g_canvases[slot];
  const int32_t width = box.width;
  const int32_t height = box.height;
  if (width == 0 || height == 0) return true;
  if (scale == 0) scale = 1;
  if (scale > 16) scale = 16;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return false;
  const hal::UpdateMode mode = hal::UpdateMode::kDu;
  const font::Face& face = face_of(box.face);
  const MultilineLayout layout = multiline_layout(box, text, cursor, wrap, scale, top);
  // The frame is the one of a text box and of a chip: it is a place a person types into and not a button (docs/90).
  int32_t radius = 8;
  const int32_t most = ((width < height) ? width : height) / 3;
  if (radius > most) radius = most;
  draw_round_fill(*target.canvas, target.x, target.y, width, height, radius, border, mode);
  draw_round_fill(*target.canvas, target.x + layout.thickness, target.y + layout.thickness,
                  width - 2 * layout.thickness, height - 2 * layout.thickness, radius - layout.thickness, background,
                  mode);
  if (layout.room <= 0) {
    close_draw(target);
    return true;
  }
  const char* at = (text != nullptr) ? text : "";
  const int32_t length = static_cast<int32_t>(strlen(at));
  int32_t row = 0;
  int32_t offset = 0;
  // Walk to the first row of the window with the same arithmetic that drew them in the layout: a second rule here is a
  // window that starts somewhere else (docs/90).
  while (row < layout.first && at[0] != '\0') {
    int32_t taken = 0;
    int32_t caret_x = 0;
    bool here = false;
    const int32_t span = row_span(face, at, layout.room, scale, wrap, -1, taken, caret_x, here);
    (void)span;
    at += taken;
    offset += taken;
    ++row;
  }
  static char shown[512];
  int32_t y = target.y + layout.pad;
  for (int32_t which = layout.first; which < layout.total && which < layout.first + layout.fit; ++which) {
    int32_t taken = 0;
    int32_t caret_x = 0;
    bool here = false;
    const int32_t span = row_span(face, at, layout.room, scale, wrap, -1, taken, caret_x, here);
    if (span > 0) {
      row_fit(face, at, span, layout.room, scale, shown, sizeof(shown));
      draw_text(*target.canvas, face, target.x + layout.pad, y, shown, scale, color, mode);
    }
    // **The caret is a block of ink with the letter under it in paper**, the same one the one line box has: a filled
    // block would hide the letter the person is about to type over (docs/90).
    if (caret && which == layout.cursor_row) {
      char under[8] = {0};
      int32_t under_bytes = 0;
      const int32_t at_cursor = static_cast<int32_t>(cursor) - offset;
      if (at_cursor >= 0 && static_cast<int32_t>(cursor) < length && at_cursor < span) {
        under_bytes = 1;
        while (static_cast<int32_t>(cursor) + under_bytes < length && (at[at_cursor + under_bytes] & 0xC0) == 0x80 &&
               under_bytes < 3) {
          ++under_bytes;
        }
        for (int32_t i = 0; i < under_bytes; ++i) under[i] = at[at_cursor + i];
        under[under_bytes] = '\0';
      }
      int32_t caret_w = caret_cell_width(face, under, under_bytes, scale);
      int32_t caret_at = target.x + layout.pad + layout.caret_x;
      if (caret_at + caret_w > target.x + layout.pad + layout.room) {
        caret_w = target.x + layout.pad + layout.room - caret_at;
      }
      if (caret_w > 0) {
        put_rect(*target.canvas, caret_at, y, caret_w, layout.line_h, color);
        mark_box(*target.canvas, caret_at, y, caret_w, layout.line_h, mode);
        if (under_bytes > 0) {
          draw_text(*target.canvas, face, caret_at, y, under, scale, background, mode);
        }
      }
    }
    at += taken;
    offset += taken;
    y += layout.line_h;
  }
  // **The scroll bar is a frame with a thumb inside**, on the right side of the box and only when the text does not
  // fit: the track is the line and the thumb is the ink, and the part of the text that is seen is the length of the
  // thumb — two tones and no grey, which is what this glass can say about where a person is inside the file
  // (docs/90).
  if (layout.bar_w > 0 && layout.bar_h > 0) {
    draw_frame_thick(*target.canvas, target.x + layout.bar_x, target.y + layout.bar_top, layout.bar_w, layout.bar_h, 1,
                     border, mode);
    const int32_t inner_w = layout.bar_w - 2;
    if (inner_w > 0 && layout.thumb_h > 0) {
      put_rect(*target.canvas, target.x + layout.bar_x + 1, target.y + layout.thumb_y, inner_w, layout.thumb_h, color);
      mark_box(*target.canvas, target.x + layout.bar_x + 1, target.y + layout.thumb_y, inner_w, layout.thumb_h, mode);
    }
  }
  close_draw(target);
  return true;
}

uint16_t textbox_multiline_hit(int8_t slot, const char* text, uint16_t cursor, bool wrap, uint8_t scale,
                               uint16_t x, uint16_t y, int16_t top) {
  if (!live(slot)) return 0;
  if (scale == 0) scale = 1;
  if (scale > 16) scale = 16;
  const Canvas& box = g_canvases[slot];
  const font::Face& face = face_of(box.face);
  const MultilineLayout layout = multiline_layout(box, text, cursor, wrap, scale, top);
  const char* at = (text != nullptr) ? text : "";
  const int32_t length = static_cast<int32_t>(strlen(at));
  int32_t row = layout.first + ((static_cast<int32_t>(y) - layout.pad) / layout.line_h);
  if (static_cast<int32_t>(y) < layout.pad) row = layout.first;
  if (row < 0) return 0;
  // **A finger below the last letter is the end of the text** (0.85.2): the row the finger found has no letter in it,
  // and a caret put in the middle of the last line —which is where the end of the box used to send it— is a caret that
  // is not at the height the person touched. A row that is there and empty is still a row: the caret goes to it
  // (docs/90).
  if (row >= layout.total) return static_cast<uint16_t>(length);
  // **A row and then a letter**: the y is the row of the window and the x is the letter inside it, and which letter it
  // is comes from the width of what is before it, which only the font knows (docs/90).
  int32_t offset = 0;
  for (int32_t which = 0; which < row && at[0] != '\0'; ++which) {
    int32_t taken = 0;
    int32_t caret_x = 0;
    bool here = false;
    const int32_t span = row_span(face, at, layout.room, scale, wrap, -1, taken, caret_x, here);
    (void)span;
    at += taken;
    offset += taken;
  }
  int32_t taken = 0;
  int32_t caret_x = 0;
  bool here = false;
  const int32_t span = row_span(face, at, layout.room, scale, wrap, -1, taken, caret_x, here);
  const int32_t at_x = static_cast<int32_t>(x) - layout.pad;
  int32_t used = 0;
  int32_t w = 0;
  while (used < span) {
    const int32_t step = advance_of(face, static_cast<uint8_t>(at[used]), scale);
    // The middle of the letter and not its start: a finger that lands on the second half of an `m` is after the `m`
    // (docs/90).
    if (w + step / 2 > at_x) break;
    w += step;
    ++used;
  }
  return static_cast<uint16_t>(offset + used);
}

uint8_t textbox_multiline_rows(int8_t slot, uint8_t scale) {
  if (!live(slot)) return 0;
  if (scale == 0) scale = 1;
  if (scale > 16) scale = 16;
  const Canvas& box = g_canvases[slot];
  const font::Face& face = face_of(box.face);
  const int32_t thickness = (box.width > 24 && box.height > 16) ? 2 : 1;
  const int32_t pad = thickness + 6;
  const int32_t line_h = static_cast<int32_t>((face.height != 0) ? face.height : font::kGlyphHeight) * scale;
  if (line_h <= 0) return 1;
  int32_t rows = (static_cast<int32_t>(box.height) - 2 * pad) / line_h;
  if (rows < 1) rows = 1;
  if (rows > 255) rows = 255;
  return static_cast<uint8_t>(rows);
}

// **How many rows the whole text has**, which is what an app wants to say "row 12 of 40" in its status line
// (0.88.0): the same measurement the paint makes, the bar included (docs/30, docs/90).
uint16_t textbox_multiline_total(int8_t slot, const char* text, bool wrap, uint8_t scale) {
  if (!live(slot)) return 0;
  if (scale == 0) scale = 1;
  if (scale > 16) scale = 16;
  const Canvas& box = g_canvases[slot];
  const MultilineLayout layout = multiline_layout(box, text, 0, wrap, scale, -1);
  return static_cast<uint16_t>((layout.total > 65535) ? 65535 : layout.total);
}

// **The height of one row in pixels**, which is what a finger has to travel to move the window one row: the same
// arithmetic of the paint, or the gesture and the text do not speak of the same distance (0.88.0, docs/90).
uint8_t textbox_multiline_line(int8_t slot, uint8_t scale) {
  if (!live(slot)) return 0;
  if (scale == 0) scale = 1;
  if (scale > 16) scale = 16;
  const font::Face& face = face_of(g_canvases[slot].face);
  const int32_t line_h = static_cast<int32_t>((face.height != 0) ? face.height : font::kGlyphHeight) * scale;
  if (line_h < 1) return 1;
  return static_cast<uint8_t>((line_h > 255) ? 255 : line_h);
}

// **Where the window is left when a finger moves it** (0.88.0): the rows the finger has travelled, added to the window
// that is seen now —the cursor's if the app has not chosen one— and turned back into a row of the bar's track. The
// result is what the app has to write in `top`, and what the system writes when the gesture is its own
// (docs/30, docs/90).
uint16_t textbox_multiline_scroll(int8_t slot, const char* text, uint16_t cursor, bool wrap, uint8_t scale,
                                  int16_t top, int16_t rows) {
  if (!live(slot)) return 0;
  if (scale == 0) scale = 1;
  if (scale > 16) scale = 16;
  const Canvas& box = g_canvases[slot];
  const MultilineLayout layout = multiline_layout(box, text, cursor, wrap, scale, top);
  int32_t first = layout.first + rows;
  if (first > layout.total - layout.fit) first = layout.total - layout.fit;
  if (first < 0) first = 0;
  if (first > 65535) first = 65535;
  return static_cast<uint16_t>(first);
}

// ---- a month: seven columns and six weeks (docs/90) ----

namespace {

// **The cells of a calendar**: seven equal columns, a row of weekday names on top and six rows of days under it. The
// arithmetic is here once and both the paint and the finger ask it (docs/90).
struct MonthCells {
  int32_t cell_w;
  int32_t row_h;
  int32_t first;  // the weekday of the first of the month, 1 Monday
  uint8_t days;
};

MonthCells cells_of(const Canvas& grid, uint16_t year, uint8_t month) {
  MonthCells cells;
  cells.cell_w = static_cast<int32_t>(grid.width) / 7;
  cells.row_h = static_cast<int32_t>(grid.height) / 7;
  cells.first = calendar::weekday_of(year, month, 1);
  cells.days = calendar::days_in_month(year, month);
  return cells;
}

}  // namespace

bool month_draw(int8_t slot, uint16_t year, uint8_t month, uint8_t selected, uint8_t today,
                const char* const* weekdays, uint8_t text, uint8_t dim, uint8_t background, uint8_t scale,
                bool focused) {
  if (!live(slot) || text > 15 || dim > 15 || background > 15) return false;
  const Canvas& grid = g_canvases[slot];
  const int32_t width = grid.width;
  const int32_t height = grid.height;
  if (width == 0 || height == 0) return true;
  if (scale == 0) scale = 1;
  if (scale > 16) scale = 16;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return false;
  const hal::UpdateMode mode = hal::UpdateMode::kDu;
  const font::Face& face = face_of(grid.face);
  const MonthCells cells = cells_of(grid, year, month);
  if (cells.cell_w <= 0 || cells.row_h <= 0 || cells.days == 0) {
    close_draw(target);
    return true;
  }
  draw_fill_rect(*target.canvas, static_cast<uint16_t>(target.x), static_cast<uint16_t>(target.y),
                 static_cast<uint16_t>(width), static_cast<uint16_t>(height), background, mode);
  static char shown[8];
  const int32_t line_h =
      static_cast<int32_t>((face.height != 0) ? face.height : font::kGlyphHeight) * scale;
  // **The names of the days are the app's**: the kernel draws them and does not know what a Tuesday is called.
  if (weekdays != nullptr) {
    const uint8_t small = static_cast<uint8_t>((scale > 1) ? scale - 1 : 1);
    for (int32_t column = 0; column < 7; ++column) {
      const char* name = weekdays[column];
      if (name == nullptr) continue;
      const int32_t taken = static_cast<int32_t>(font::width(face, name, small));
      const int32_t small_h = static_cast<int32_t>((face.height != 0) ? face.height : font::kGlyphHeight) * small;
      draw_text(*target.canvas, face,
                target.x + column * cells.cell_w + (cells.cell_w - taken) / 2,
                target.y + (cells.row_h - small_h) / 2, name, small, dim, mode);
    }
  }
  for (int32_t day = 1; day <= cells.days; ++day) {
    const int32_t position = (day - 1) + (cells.first - 1);
    const int32_t row = 1 + position / 7;
    const int32_t column = position % 7;
    // **Six weeks and not five**: a month that starts on a Sunday needs six, and a row that appears in March and
    // disappears in April moves everything under the calendar (docs/90).
    if (row > 6) break;
    const int32_t x = target.x + column * cells.cell_w;
    const int32_t y = target.y + row * cells.row_h;
    const bool chosen = static_cast<uint8_t>(day) == selected;
    // **The chosen day is inverted and today wears a frame**: two signals for two facts, because a page has no
    // colour to spend on the second one (docs/90).
    if (chosen) {
      draw_fill_rect(*target.canvas, static_cast<uint16_t>(x + 1), static_cast<uint16_t>(y + 1),
                     static_cast<uint16_t>(cells.cell_w - 2), static_cast<uint16_t>(cells.row_h - 2), text,
                     mode);
    } else if (static_cast<uint8_t>(day) == today) {
      put_rect(*target.canvas, x + 1, y + 1, cells.cell_w - 2, 1, text);
      put_rect(*target.canvas, x + 1, y + cells.row_h - 2, cells.cell_w - 2, 1, text);
      put_rect(*target.canvas, x + 1, y + 1, 1, cells.row_h - 2, text);
      put_rect(*target.canvas, x + cells.cell_w - 2, y + 1, 1, cells.row_h - 2, text);
    }
    snprintf(shown, sizeof(shown), "%d", static_cast<int>(day));
    const int32_t taken = static_cast<int32_t>(font::width(face, shown, scale));
    draw_text(*target.canvas, face, x + (cells.cell_w - taken) / 2, y + (cells.row_h - line_h) / 2, shown, scale,
              chosen ? background : text, mode);
  }
  // **The calendar as a whole is the control**: its frame says where the dial is, and the cells say the rest.
  if (focused && width > 8 && height > 8) {
    put_rect(*target.canvas, target.x, target.y, width, 2, text);
    put_rect(*target.canvas, target.x, target.y + height - 2, width, 2, text);
    put_rect(*target.canvas, target.x, target.y, 2, height, text);
    put_rect(*target.canvas, target.x + width - 2, target.y, 2, height, text);
  }
  mark_box(*target.canvas, target.x, target.y, width, height, mode);
  close_draw(target);
  return true;
}

uint8_t month_hit(int8_t slot, uint16_t year, uint8_t month, uint16_t x, uint16_t y) {
  if (!live(slot)) return 0;
  const Canvas& grid = g_canvases[slot];
  const MonthCells cells = cells_of(grid, year, month);
  if (cells.cell_w <= 0 || cells.row_h <= 0 || cells.days == 0) return 0;
  const int32_t column = static_cast<int32_t>(x) / cells.cell_w;
  const int32_t row = static_cast<int32_t>(y) / cells.row_h;
  if (column < 0 || column > 6 || row < 1 || row > 6) return 0;
  const int32_t day = (row - 1) * 7 + column - (cells.first - 1) + 1;
  if (day < 1 || day > cells.days) return 0;
  return static_cast<uint8_t>(day);
}

// ---- a sparkline and a pie (docs/90) ----

bool sparkline_draw(int8_t slot, const uint16_t* samples, const uint16_t* top, uint8_t count, uint8_t color,
                    uint8_t background) {
  if (!live(slot) || color > 15 || background > 15) return false;
  const Canvas& chart = g_canvases[slot];
  const int32_t width = chart.width;
  const int32_t height = chart.height;
  if (width == 0 || height == 0) return true;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return false;
  const hal::UpdateMode mode = hal::UpdateMode::kDu;
  draw_fill_rect(*target.canvas, static_cast<uint16_t>(target.x), static_cast<uint16_t>(target.y),
                 static_cast<uint16_t>(width), static_cast<uint16_t>(height), background, mode);
  if (samples == nullptr || count == 0) {
    close_draw(target);
    return true;
  }
  if (count > kMaxSparkSamples) count = kMaxSparkSamples;
  // **The peak of the window and not a fixed scale**: the shape reads the same with ten as with ten thousand, and
  // that is what a sparkline is asked for (docs/90).
  uint32_t peak = 0;
  for (uint8_t i = 0; i < count; ++i) {
    const uint32_t total = static_cast<uint32_t>(samples[i]) + ((top != nullptr) ? top[i] : 0);
    if (total > peak) peak = total;
  }
  if (peak == 0) {
    close_draw(target);
    return true;
  }
  const int32_t bar = width / count;
  if (bar <= 0) {
    close_draw(target);
    return true;
  }
  const int32_t gap = (bar > 3) ? 1 : 0;
  for (uint8_t i = 0; i < count; ++i) {
    const int32_t x = target.x + static_cast<int32_t>(i) * bar;
    const int32_t solid = static_cast<int32_t>(samples[i]);
    const int32_t total = solid + ((top != nullptr) ? static_cast<int32_t>(top[i]) : 0);
    int32_t full = static_cast<int32_t>((static_cast<uint64_t>(static_cast<uint32_t>(total)) * height) / peak);
    int32_t below = static_cast<int32_t>((static_cast<uint64_t>(static_cast<uint32_t>(solid)) * height) / peak);
    if (full > height) full = height;
    if (below > full) below = full;
    // **A sample that is not zero always shows a pixel**: one of a hundred in a chart of forty is less than a
    // pixel, and a bar of nothing reads as a zero (docs/90).
    if (solid > 0 && below == 0) below = 1;
    if (below > 0) {
      put_rect(*target.canvas, x, target.y + height - below, bar - gap, below, color);
    }
    if (top != nullptr && total > solid) {
      // **The part on top is an outline**: the two series are parts of one total and the ink is the only level a
      // chart may use, so the second one is said with a frame and not with a colour (docs/90).
      const int32_t top_y = target.y + height - full;
      const int32_t top_h = full - below;
      if (top_h > 0) {
        put_rect(*target.canvas, x, top_y, bar - gap, 1, color);
        if (top_h > 1) put_rect(*target.canvas, x, top_y + top_h - 1, bar - gap, 1, color);
        if (top_h > 2) {
          put_rect(*target.canvas, x, top_y, 1, top_h, color);
          put_rect(*target.canvas, x + bar - gap - 1, top_y, 1, top_h, color);
        }
      }
    }
  }
  mark_box(*target.canvas, target.x, target.y, width, height, mode);
  close_draw(target);
  return true;
}

namespace {

// **The thirty-two directions of the circle, in a fixed point of four thousand and ninety-six**: the pie needs an
// angle for every pixel and this kernel has no floating point, so the angles are a table and the arithmetic is
// integer. Index zero is the right and the index grows **clockwise**, because that is the way a clock goes and a
// chart is read with a clock (docs/90).
const int16_t kDirections[32][2] = {
    {4096, 0},    {4017, 799},   {3784, 1567},  {3406, 2276},  {2896, 2896},  {2276, 3406},
    {1567, 3784},  {799, 4017},  {0, 4096},     {-799, 4017},  {-1567, 3784}, {-2276, 3406},
    {-2896, 2896}, {-3406, 2276}, {-3784, 1567}, {-4017, 799},  {-4096, 0},   {-4017, -799},
    {-3784, -1567}, {-3406, -2276}, {-2896, -2896}, {-2276, -3406}, {-1567, -3784}, {-799, -4017},
    {0, -4096},    {799, -4017},  {1567, -3784}, {2276, -3406}, {2896, -2896}, {3406, -2276},
    {3784, -1567}, {4017, -799},
};

// **Which of the thirty-two directions a point is**: the octant is decided with two comparisons and then four
// directions are tried, because a disc of sixty pixels is fourteen thousand of these and a scan of thirty-two per
// pixel would be half of a paint (docs/75, docs/90).
uint8_t sector_of(int32_t dx, int32_t dy) {
  const int32_t ax = (dx >= 0) ? dx : -dx;
  const int32_t ay = (dy >= 0) ? dy : -dy;
  uint8_t first = 0;
  if (dx >= 0 && dy >= 0) {
    first = (ax >= ay) ? 0 : 4;
  } else if (dx < 0 && dy >= 0) {
    first = (ay >= ax) ? 8 : 12;
  } else if (dx < 0 && dy < 0) {
    first = (ax >= ay) ? 16 : 20;
  } else {
    first = (ay >= ax) ? 24 : 28;
  }
  for (uint8_t i = 0; i < 5; ++i) {
    const uint8_t at = static_cast<uint8_t>((first + i) % 32);
    const uint8_t next = static_cast<uint8_t>((at + 1) % 32);
    const int64_t cross_a = static_cast<int64_t>(kDirections[at][0]) * dy -
                            static_cast<int64_t>(kDirections[at][1]) * dx;
    const int64_t cross_b = static_cast<int64_t>(kDirections[next][0]) * dy -
                            static_cast<int64_t>(kDirections[next][1]) * dx;
    if (cross_a >= 0 && cross_b <= 0) return at;
  }
  return first;
}

}  // namespace

bool pie_draw(int8_t slot, const uint16_t* slices, uint8_t count, uint8_t color, uint8_t background) {
  if (!live(slot) || color > 15 || background > 15) return false;
  const Canvas& chart = g_canvases[slot];
  const int32_t width = chart.width;
  const int32_t height = chart.height;
  if (width == 0 || height == 0) return true;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return false;
  const hal::UpdateMode mode = hal::UpdateMode::kDu;
  draw_fill_rect(*target.canvas, static_cast<uint16_t>(target.x), static_cast<uint16_t>(target.y),
                 static_cast<uint16_t>(width), static_cast<uint16_t>(height), background, mode);
  if (slices == nullptr || count == 0) {
    close_draw(target);
    return true;
  }
  if (count > kMaxPieSlices) count = kMaxPieSlices;
  uint32_t total = 0;
  for (uint8_t i = 0; i < count; ++i) total += slices[i];
  int32_t radius = ((width < height) ? width : height) / 2 - 2;
  if (radius > 120) radius = 120;
  if (total == 0 || radius < 4) {
    close_draw(target);
    return true;
  }
  const int32_t centre_x = target.x + width / 2;
  const int32_t centre_y = target.y + height / 2;
  // **The four tones walk from the one the app chose**: the app draws the legend with the same arithmetic, so a
  // legend and a pie cannot almost agree (docs/90).
  uint8_t tones[kMaxPieSlices];
  for (uint8_t i = 0; i < count; ++i) {
    int32_t tone = static_cast<int32_t>(color) + 4 * static_cast<int32_t>(i);
    if (tone > 15) tone = 15;
    tones[i] = static_cast<uint8_t>(tone);
  }
  // The slices own a run of directions each, starting at twelve o'clock and going clockwise, and the last one
  // closes the circle: a pie that stops at four fifths is a pie that lost its last slice (docs/90).
  uint8_t start[kMaxPieSlices];
  uint8_t stop[kMaxPieSlices];
  uint8_t at = 24;
  uint32_t running = 0;
  for (uint8_t i = 0; i < count; ++i) {
    running += slices[i];
    start[i] = at;
    at = static_cast<uint8_t>((24 + static_cast<int32_t>((static_cast<uint64_t>(running) * 32 + total / 2) /
                                                          total)) %
                              32);
    if (i + 1 == count) at = 24;
    stop[i] = at;
  }
  const int32_t square = radius * radius;
  for (int32_t dy = -radius; dy <= radius; ++dy) {
    for (int32_t dx = -radius; dx <= radius; ++dx) {
      if (dx * dx + dy * dy > square) continue;
      const uint8_t sector = sector_of(dx, dy);
      uint8_t slice = 0;
      for (uint8_t i = 0; i < count; ++i) {
        bool inside = false;
        for (uint8_t walk = start[i]; walk != stop[i]; walk = static_cast<uint8_t>((walk + 1) % 32)) {
          if (walk == sector) {
            inside = true;
            break;
          }
        }
        if (inside) {
          slice = i;
          break;
        }
      }
      put_pixel(*target.canvas, centre_x + dx, centre_y + dy, tones[slice]);
    }
  }
  mark_box(*target.canvas, centre_x - radius, centre_y - radius, radius * 2 + 1, radius * 2 + 1, mode);
  close_draw(target);
  return true;
}

// ---- a dialog: a card and two doors (docs/90) ----

namespace {

// **The two doors of a card**: where they are is one arithmetic and both the paint and the finger ask it, or the
// box that is drawn is not the box that answers (docs/90).
struct Doors {
  int32_t pad;
  int32_t height;
  int32_t width;
  int32_t y;
  int32_t gap;
};

Doors doors_of(const Canvas& card) {
  Doors doors;
  doors.pad = (card.width > 100) ? 16 : 8;
  doors.height = static_cast<int32_t>(card.height) / 7;
  if (doors.height < 32) doors.height = 32;
  if (doors.height > 56) doors.height = 56;
  doors.gap = (card.width > 160) ? 12 : 8;
  const int32_t inner = static_cast<int32_t>(card.width) - 2 * doors.pad;
  // Two doors side by side when the card is wide enough for two words, and one across the whole card when it is
  // not: a door of thirty pixels is a door nobody hits (docs/90).
  doors.width = (inner > 140) ? ((inner - doors.gap) / 2) : inner;
  doors.y = static_cast<int32_t>(card.height) - doors.pad - doors.height;
  return doors;
}

// **A message wraps and is not cut**: a dialog that says half of a question is a dialog nobody can answer. The
// lines break at the last space that fits and a word longer than a line is cut at the line; `out` holds the lines
// one after the other, each one closed by a NUL, and the return is how many there are (docs/90).
int32_t wrap_lines(int8_t face_handle, const char* text, int32_t width, uint8_t scale, char* out, size_t cap,
                   int32_t max_lines) {
  if (text == nullptr || out == nullptr || cap < 2 || max_lines <= 0) return 0;
  const font::Face& face = face_of(face_handle);
  int32_t lines = 0;
  size_t written = 0;
  const char* at = text;
  while (at[0] != '\0' && lines < max_lines) {
    char line[256];
    int32_t used = 0;
    int32_t last_space = -1;
    while (at[used] != '\0') {
      if (used + 1 >= static_cast<int32_t>(sizeof(line))) break;
      line[used] = at[used];
      line[used + 1] = '\0';
      if (at[used] == ' ') last_space = used;
      if (static_cast<int32_t>(font::width(face, line, scale)) > width && used > 0) {
        // The line is one letter too long: it ends at the last space, or here when there was no space.
        if (last_space > 0) {
          used = last_space;
        }
        break;
      }
      ++used;
    }
    if (at[used] == '\0') {
      // Everything left fits.
      if (written + static_cast<size_t>(used) + 2 > cap) break;
      memcpy(out + written, line, static_cast<size_t>(used));
      written += static_cast<size_t>(used);
      out[written++] = '\0';
      ++lines;
      break;
    }
    if (written + static_cast<size_t>(used) + 2 > cap) break;
    memcpy(out + written, line, static_cast<size_t>(used));
    written += static_cast<size_t>(used);
    out[written++] = '\0';
    ++lines;
    at += used;
    while (at[0] == ' ') ++at;
  }
  return lines;
}

}  // namespace

bool dialog_draw(int8_t slot, const char* title, const char* text, const char* note, const char* accept,
                 const char* cancel, uint8_t emphasis, uint8_t choice, uint8_t text_level, uint8_t dim,
                 uint8_t background, uint8_t scale, bool focused) {
  if (!live(slot) || text_level > 15 || dim > 15 || background > 15) return false;
  const Canvas& card = g_canvases[slot];
  const int32_t width = card.width;
  const int32_t height = card.height;
  if (width == 0 || height == 0) return true;
  if (scale == 0) scale = 1;
  if (scale > 16) scale = 16;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return false;
  const hal::UpdateMode mode = hal::UpdateMode::kDu;
  const font::Face& face = face_of(card.face);
  const Doors doors = doors_of(card);
  const int32_t frame = (width > 120) ? 3 : 2;
  int32_t radius = 12;
  const int32_t most = ((width < height) ? width : height) / 3;
  if (radius > most) radius = most;
  // **The card is opaque and it has the weight of a card**: the frame of three pixels is what tells a person that
  // this is on top of the page and not part of it (docs/90).
  draw_round_fill(*target.canvas, target.x, target.y, width, height, radius, text_level, mode);
  draw_round_fill(*target.canvas, target.x + frame, target.y + frame, width - 2 * frame, height - 2 * frame,
                  radius - frame, background, mode);
  const int32_t line_h =
      static_cast<int32_t>((face.height != 0) ? face.height : font::kGlyphHeight) * scale;
  static char shown[512];
  int32_t y = target.y + doors.pad + 4;
  // The title, one line and cut if it is long: a title that wraps is a title of three lines and the card is the
  // message's (docs/90).
  if (title != nullptr && title[0] != '\0') {
    const uint16_t taken = label_text(card.face, title, static_cast<uint16_t>(width - 2 * doors.pad),
                                      static_cast<uint8_t>(scale + 1), shown, sizeof(shown));
    draw_text(*target.canvas, face, target.x + (width - static_cast<int32_t>(taken)) / 2, y, shown,
              static_cast<uint8_t>(scale + 1), text_level, mode);
    y += ((face.height != 0) ? face.height : font::kGlyphHeight) * (scale + 1) + 8;
  }
  // **The message wraps** in what is left above the doors, and a note goes under it in the dim colour.
  const int32_t room = doors.y - y - (doors.pad / 2);
  const int32_t max_lines = (line_h > 0) ? (room / line_h) : 0;
  if (text != nullptr && text[0] != '\0' && max_lines > 0) {
    static char lines[512];
    const int32_t count = wrap_lines(card.face, text, width - 2 * doors.pad, scale, lines, sizeof(lines),
                                     (max_lines > 6) ? 6 : max_lines);
    const char* one = lines;
    for (int32_t i = 0; i < count; ++i) {
      draw_text(*target.canvas, face, target.x + doors.pad, y, one, scale, text_level, mode);
      y += line_h;
      one += strlen(one) + 1;
    }
  }
  if (note != nullptr && note[0] != '\0' && y + line_h <= doors.y - 2) {
    const uint16_t taken = label_text(card.face, note, static_cast<uint16_t>(width - 2 * doors.pad),
                                      static_cast<uint8_t>((scale > 1) ? scale - 1 : 1), shown,
                                      sizeof(shown));
    draw_text(*target.canvas, face, target.x + (width - static_cast<int32_t>(taken)) / 2, y, shown,
              static_cast<uint8_t>((scale > 1) ? scale - 1 : 1), dim, mode);
  }
  // **The two doors**: an inverted box for the emphasised one, a box of paper with an ink frame for the other, and
  // the ring of the focus on the one the dial is on (docs/90).
  const bool two = (cancel != nullptr && cancel[0] != '\0');
  static char words[2][64];
  const char* labels[2] = {cancel, accept};
  for (int32_t i = 0; i < 2; ++i) {
    const uint8_t which = static_cast<uint8_t>(i + 1);
    if (!two && which == 1) continue;
    const char* word = labels[i];
    if (word == nullptr) word = "";
    const int32_t box_x = target.x + doors.pad + ((two && i == 1) ? (doors.width + doors.gap) : 0);
    const int32_t box_w = two ? doors.width : (width - 2 * doors.pad);
    const bool hot = (emphasis == which);
    const uint8_t paper = hot ? text_level : background;
    const uint8_t ink = hot ? background : text_level;
    draw_round_fill(*target.canvas, box_x, target.y + doors.y, box_w, doors.height, 8, ink, mode);
    draw_round_fill(*target.canvas, box_x + frame, target.y + doors.y + frame, box_w - 2 * frame,
                    doors.height - 2 * frame, 8 - frame, paper, mode);
    label_text(card.face, word, static_cast<uint16_t>(box_w - 2 * frame - 12), scale, words[i], sizeof(words[i]));
    const int32_t taken = static_cast<int32_t>(font::width(face, words[i], scale));
    draw_text(*target.canvas, face, box_x + (box_w - taken) / 2,
              target.y + doors.y + (doors.height - line_h) / 2, words[i], scale, ink, mode);
    if (focused && choice == which && box_w > 16 && doors.height > 16) {
      // **The ring is the tone of the words**, which is the one that contrasts with the inside of the door whether the
      // door is paper or ink: a ring of a fixed tone disappeared on the door that was inverted, which is the door a
      // person is looking at (docs/90, found while porting the menu, 3/11).
      const uint8_t ring = ink;
      const int32_t inset = 3;
      draw_round_fill(*target.canvas, box_x + inset, target.y + doors.y + inset, box_w - 2 * inset,
                      doors.height - 2 * inset, 6, ring, mode);
      draw_round_fill(*target.canvas, box_x + inset + 2, target.y + doors.y + inset + 2, box_w - 2 * inset - 4,
                      doors.height - 2 * inset - 4, 5, paper, mode);
    }
  }
  close_draw(target);
  return true;
}

uint8_t dialog_hit(int8_t slot, uint16_t x, uint16_t y) {
  if (!live(slot)) return 0;
  const Canvas& card = g_canvases[slot];
  const Doors doors = doors_of(card);
  if (static_cast<int32_t>(y) < doors.y || static_cast<int32_t>(y) >= doors.y + doors.height) return 0;
  const int32_t at_x = static_cast<int32_t>(x) - doors.pad;
  if (at_x < 0) return 0;
  if (doors.width + doors.gap + doors.width > static_cast<int32_t>(card.width) - 2 * doors.pad) {
    // One door across the card: both halves are the same door, and `luabind` knows whether there is a second.
    return (at_x <= doors.width) ? 2 : 0;
  }
  if (at_x <= doors.width) return 1;
  if (at_x <= doors.width + doors.gap) return 0;
  if (at_x <= doors.width + doors.gap + doors.width) return 2;
  return 0;
}

// ---- a menu: a card of options (docs/30, docs/90) ----

namespace {

// **Where one option of a menu is**: the paint and the finger ask the same box, or the box that is drawn is not the
// box that answers (docs/90).
struct MenuBox {
  int32_t x;
  int32_t y;
  int32_t w;
  int32_t h;
};

// **The arithmetic of the card, once**: the frame, the padding, the room the title takes, and the grid the options are
// put in. The words fill a column before the next one, which is the way a list of words is read (docs/90).
struct MenuGrid {
  int32_t pad;
  int32_t frame;
  int32_t gap;
  int32_t left;
  int32_t top;
  int32_t row_h;
  int32_t col_w;
  uint8_t columns;
  uint8_t rows;
};

int32_t menu_line_h(const Canvas& card, uint8_t scale) {
  const font::Face& face = face_of(card.face);
  return static_cast<int32_t>((face.height != 0) ? face.height : font::kGlyphHeight) * scale;
}

// **The options that are on the card**: a hidden one is not counted, not drawn, not hit and not walked by the dial,
// because it is not on the glass (docs/90).
uint8_t menu_visible(const MenuOption* items, uint8_t count) {
  if (items == nullptr) return 0;
  uint8_t visible = 0;
  for (uint8_t i = 0; i < count; ++i) {
    if (!items[i].hidden) ++visible;
  }
  return visible;
}

// **The place of an option among the ones that are on the card**, or -1 when the app took it out (docs/90).
int32_t menu_rank(const MenuOption* items, uint8_t count, uint8_t number) {
  if (items == nullptr || number < 1 || number > count || items[number - 1].hidden) return -1;
  int32_t rank = 0;
  for (uint8_t i = 0; i + 1 < number; ++i) {
    if (!items[i].hidden) ++rank;
  }
  return rank;
}

MenuGrid menu_grid(const Canvas& card, bool has_title, uint8_t visible, uint8_t scale) {
  MenuGrid grid = {};
  grid.pad = (card.width > 100) ? 16 : 8;
  grid.frame = (card.width > 120) ? 3 : 2;
  grid.gap = (card.width > 160) ? 12 : 8;
  const int32_t line_h = menu_line_h(card, scale);
  // **A row is never under fifty-two pixels**: it is what a fingertip needs and it is what a row of a list is (docs/90).
  int32_t row_h = line_h + 20;
  if (row_h < 52) row_h = 52;
  int32_t top = grid.pad + 4;
  if (has_title) top += line_h + 12;
  const int32_t room = static_cast<int32_t>(card.height) - top - grid.pad;
  int32_t rows = (room > row_h) ? (room / row_h) : 1;
  if (rows < 1) rows = 1;
  int32_t columns = (static_cast<int32_t>(visible) > rows) ? ((static_cast<int32_t>(visible) + rows - 1) / rows) : 1;
  if (columns < 1) columns = 1;
  // Four columns is as many words as a card of this glass can hold; the options that have no box after that are not
  // drawn, which is what a card the app sized too small gets (docs/90).
  if (columns > 4) columns = 4;
  // **The options share the columns evenly**, so a menu of five is three and two and not four and one (docs/90).
  if (visible > 0) rows = (static_cast<int32_t>(visible) + columns - 1) / columns;
  if (rows < 1) rows = 1;
  // **The rows grow into the room that is left**: three options in a tall card are three tall doors, which is exactly
  // what the menu of the system wants (docs/90).
  if (room > 0) {
    const int32_t filled = room / rows;
    if (filled > row_h) row_h = filled;
  }
  if (row_h > 160) row_h = 160;
  grid.row_h = row_h;
  grid.rows = static_cast<uint8_t>(rows);
  grid.columns = static_cast<uint8_t>(columns);
  const int32_t inner = static_cast<int32_t>(card.width) - 2 * grid.pad;
  grid.col_w = (inner - grid.gap * (columns - 1)) / columns;
  if (grid.col_w < 1) grid.col_w = 1;
  grid.left = grid.pad;
  grid.top = top;
  return grid;
}

MenuBox menu_cell(const MenuGrid& grid, int32_t rank) {
  MenuBox box;
  const int32_t column = (grid.rows > 0) ? (rank / grid.rows) : 0;
  const int32_t row = (grid.rows > 0) ? (rank % grid.rows) : 0;
  box.w = grid.col_w;
  box.h = grid.row_h;
  box.x = grid.left + column * (grid.col_w + grid.gap);
  box.y = grid.top + row * grid.row_h;
  return box;
}

}  // namespace

bool menu_draw(int8_t slot, const char* title, const MenuOption* items, uint8_t count, uint8_t index,
               uint8_t color, uint8_t dim, uint8_t background, uint8_t scale, bool focused) {
  if (!live(slot) || color > 15 || dim > 15 || background > 15) return false;
  const Canvas& card = g_canvases[slot];
  const int32_t width = card.width;
  const int32_t height = card.height;
  if (width == 0 || height == 0) return true;
  if (scale == 0) scale = 1;
  if (scale > 16) scale = 16;
  if (count > kMaxMenuOptions) count = kMaxMenuOptions;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return false;
  const hal::UpdateMode mode = hal::UpdateMode::kDu;
  const font::Face& face = face_of(card.face);
  const int32_t line_h = menu_line_h(card, scale);
  const bool has_title = (title != nullptr && title[0] != '\0');
  const MenuGrid grid = menu_grid(card, has_title, menu_visible(items, count), scale);
  const int32_t fitted = static_cast<int32_t>(grid.rows) * static_cast<int32_t>(grid.columns);
  int32_t radius = 12;
  const int32_t most = ((width < height) ? width : height) / 3;
  if (radius > most) radius = most;
  // **The card is opaque and it has the weight of a card**: the frame is what tells a person that this is on top of
  // the page and not part of it (docs/90).
  draw_round_fill(*target.canvas, target.x, target.y, width, height, radius, color, mode);
  draw_round_fill(*target.canvas, target.x + grid.frame, target.y + grid.frame, width - 2 * grid.frame,
                  height - 2 * grid.frame, radius - grid.frame, background, mode);
  static char shown[192];
  if (has_title) {
    const uint16_t taken = label_text(card.face, title, static_cast<uint16_t>(width - 2 * grid.pad),
                                      static_cast<uint8_t>(scale + 1), shown, sizeof(shown));
    draw_text(*target.canvas, face, target.x + (width - static_cast<int32_t>(taken)) / 2, target.y + grid.pad,
              shown, static_cast<uint8_t>(scale + 1), color, mode);
  }
  for (uint8_t n = 1; n <= count; ++n) {
    const int32_t rank = menu_rank(items, count, n);
    if (rank < 0 || rank >= fitted) continue;
    const MenuBox box = menu_cell(grid, rank);
    const MenuOption& option = items[n - 1];
    // **The option of the moment is inverted** and the rest are paper with the weight of a card: there is no colour on
    // this glass to say which one the app cares about (docs/90).
    const bool hot = option.primary;
    const uint8_t face_level = hot ? color : background;
    const uint8_t ink = hot ? background : color;
    draw_round_fill(*target.canvas, target.x + box.x, target.y + box.y, box.w, box.h, 8, ink, mode);
    draw_round_fill(*target.canvas, target.x + box.x + grid.frame, target.y + box.y + grid.frame,
                    box.w - 2 * grid.frame, box.h - 2 * grid.frame, 8 - grid.frame, face_level, mode);
    int32_t text_x = target.x + box.x + grid.frame + 10;
    int32_t room_text = box.w - 2 * grid.frame - 20;
    // **The tag is a short word in front of the label** and it goes in the dim tone, because the label is what a person
    // reads; on an inverted row it goes in the ink and not in the app's dim, because a dim of fifteen on the ink tone
    // is a word nobody sees (docs/90).
    if (option.tag != nullptr && option.tag[0] != '\0' && scale > 1) {
      char tag[24];
      const uint8_t small = static_cast<uint8_t>(scale - 1);
      label_text(card.face, option.tag, 96, small, tag, sizeof(tag));
      const int32_t tag_w = static_cast<int32_t>(font::width(face, tag, small));
      if (tag_w + 8 < room_text) {
        draw_text(*target.canvas, face, text_x, target.y + box.y + (box.h - menu_line_h(card, small)) / 2, tag,
                  small, hot ? background : dim, mode);
        text_x += tag_w + 8;
        room_text -= tag_w + 8;
      }
    }
    // **The words of an option are on the left**: a column of actions reads down the left edge, and a column of centred
    // words of different lengths is a staircase (docs/90).
    label_text(card.face, (option.label != nullptr) ? option.label : "",
               static_cast<uint16_t>((room_text > 0) ? room_text : 0), scale, shown, sizeof(shown));
    draw_text(*target.canvas, face, text_x, target.y + box.y + (box.h - line_h) / 2, shown, scale, ink, mode);
    // **The ring of the dial** inside the box and in the tone of the words, which is the one that contrasts with the
    // inside of the box whether it is paper or ink (docs/90).
    if (focused && index == n && box.w > 20 && box.h > 20) {
      const int32_t inset = 3;
      draw_round_fill(*target.canvas, target.x + box.x + inset, target.y + box.y + inset, box.w - 2 * inset,
                      box.h - 2 * inset, 6, ink, mode);
      draw_round_fill(*target.canvas, target.x + box.x + inset + 2, target.y + box.y + inset + 2,
                      box.w - 2 * inset - 4, box.h - 2 * inset - 4, 5, hot ? color : background, mode);
    }
  }
  close_draw(target);
  return true;
}

uint8_t menu_hit(int8_t slot, const char* title, const MenuOption* items, uint8_t count, uint8_t scale,
                 uint16_t x, uint16_t y) {
  if (!live(slot)) return 0;
  if (scale == 0) scale = 1;
  if (scale > 16) scale = 16;
  if (count > kMaxMenuOptions) count = kMaxMenuOptions;
  const Canvas& card = g_canvases[slot];
  const bool has_title = (title != nullptr && title[0] != '\0');
  const MenuGrid grid = menu_grid(card, has_title, menu_visible(items, count), scale);
  const int32_t fitted = static_cast<int32_t>(grid.rows) * static_cast<int32_t>(grid.columns);
  const int32_t at_x = static_cast<int32_t>(x);
  const int32_t at_y = static_cast<int32_t>(y);
  for (uint8_t n = 1; n <= count; ++n) {
    const int32_t rank = menu_rank(items, count, n);
    if (rank < 0 || rank >= fitted) continue;
    const MenuBox box = menu_cell(grid, rank);
    if (at_x >= box.x && at_y >= box.y && at_x < box.x + box.w && at_y < box.y + box.h) return n;
  }
  return 0;
}

// ---- a grid of cells: an icon with its name under it, on pages (docs/30, docs/90) ----

namespace {

// **The grid, measured once**: the columns, the rows, the box of the mark and the page the cursor is on. The paint and the
// finger ask this, and **the page follows the cursor**: a grid never sits on a page where the cell a person is on is not
// (docs/90).
struct GridCells {
  int32_t columns;
  int32_t rows;
  int32_t cell_w;
  int32_t cell_h;
  int32_t icon;
  int32_t pad;
  int32_t footer;
  int32_t page;
  int32_t pages;
  int32_t first;
};

GridCells icon_grid_cells(const Canvas& grid, uint8_t count, uint8_t index, uint8_t columns, uint8_t scale) {
  GridCells cells = {};
  const font::Face& face = face_of(grid.face);
  const int32_t width = static_cast<int32_t>(grid.width);
  const int32_t height = static_cast<int32_t>(grid.height);
  const int32_t line_h = static_cast<int32_t>((face.height != 0) ? face.height : font::kGlyphHeight) * scale;
  cells.pad = (width > 200) ? 10 : 6;
  // **A column is a thing a finger hits**: ninety-six pixels is the narrowest a cell can be and still be one target and not
  // two halves of two (docs/90).
  int32_t columns_now = columns;
  if (columns_now <= 0) {
    columns_now = width / kGridLeastCell;
  }
  if (columns_now < 1) columns_now = 1;
  if (columns_now > 8) columns_now = 8;
  cells.columns = columns_now;
  cells.cell_w = width / columns_now;
  // The box of the mark takes what the cell can hold, and the name goes under it.
  int32_t icon = cells.cell_w - 2 * cells.pad;
  if (icon > 72) icon = 72;
  if (icon < 24) icon = 24;
  cells.icon = icon;
  cells.cell_h = icon + 8 + line_h + 8;
  cells.footer = (height > 160) ? 28 : 0;
  int32_t rows = (cells.cell_h > 0) ? ((height - cells.footer) / cells.cell_h) : 0;
  if (rows < 1) rows = 1;
  cells.rows = rows;
  const int32_t per_page = cells.columns * cells.rows;
  int32_t pages = (count > 0 && per_page > 0) ? ((static_cast<int32_t>(count) + per_page - 1) / per_page) : 1;
  if (pages < 1) pages = 1;
  cells.pages = pages;
  int32_t page = 1;
  if (index > 0) page = (static_cast<int32_t>(index) - 1) / per_page + 1;
  if (page > pages) page = pages;
  if (page < 1) page = 1;
  cells.page = page;
  cells.first = (page - 1) * per_page;
  return cells;
}

// **Where one cell is**, in the frame's own coordinates: the page decides which item it holds and the grid decides where.
struct GridBox {
  int32_t x;
  int32_t y;
  int32_t w;
  int32_t h;
};

GridBox icon_grid_box(const GridCells& cells, int32_t which) {
  GridBox box;
  const int32_t column = (cells.columns > 0) ? (which % cells.columns) : 0;
  const int32_t row = (cells.columns > 0) ? (which / cells.columns) : 0;
  box.w = cells.cell_w;
  box.h = cells.cell_h;
  box.x = column * cells.cell_w;
  box.y = row * cells.cell_h;
  return box;
}

}  // namespace

bool icon_grid_draw(int8_t slot, const GridItem* items, uint8_t count, uint8_t index, uint8_t marked,
                    uint8_t columns, uint8_t color, uint8_t dim, uint8_t background, uint8_t scale, bool focused) {
  if (!live(slot) || color > 15 || dim > 15 || background > 15) return false;
  const Canvas& grid = g_canvases[slot];
  const int32_t width = grid.width;
  const int32_t height = grid.height;
  if (width == 0 || height == 0) return true;
  if (scale == 0) scale = 1;
  if (scale > 16) scale = 16;
  if (count > kMaxGridItems) count = kMaxGridItems;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return false;
  const hal::UpdateMode mode = hal::UpdateMode::kDu;
  const font::Face& face = face_of(grid.face);
  const int32_t line_h = static_cast<int32_t>((face.height != 0) ? face.height : font::kGlyphHeight) * scale;
  const GridCells cells = icon_grid_cells(grid, count, index, columns, scale);
  // **The cell is paper and the page is paper**: the grid paints its own frame, because a grid is a page and not a band over
  // one (docs/90).
  draw_fill_rect(*target.canvas, static_cast<uint16_t>(target.x), static_cast<uint16_t>(target.y),
                 static_cast<uint16_t>(width), static_cast<uint16_t>(height), background, mode);
  const int32_t per_page = cells.columns * cells.rows;
  static char shown[64];
  for (int32_t which = cells.first; which < cells.first + per_page && which < static_cast<int32_t>(count); ++which) {
    const GridBox box = icon_grid_box(cells, which - cells.first);
    const int32_t x = target.x + box.x;
    const int32_t y = target.y + box.y;
    const int32_t icon_x = x + (box.w - cells.icon) / 2;
    const int32_t icon_y = y + 4;
    const int32_t item = which + 1;
    const bool chosen = (marked == static_cast<uint8_t>(item));
    // **The cell that is chosen is inverted and the cell the dial is on wears the ring**: two signals for two facts, the same
    // rule the calendar follows (docs/90).
    draw_round_fill(*target.canvas, icon_x, icon_y, cells.icon, cells.icon, 10, color, mode);
    draw_round_fill(*target.canvas, icon_x + 2, icon_y + 2, cells.icon - 4, cells.icon - 4, 8,
                    chosen ? color : background, mode);
    const char* mark = (items[which].mark != nullptr) ? items[which].mark : "";
    if (mark[0] != '\0') {
      const uint8_t big = static_cast<uint8_t>((scale + 1 > 16) ? 16 : scale + 1);
      label_text(grid.face, mark, static_cast<uint16_t>(cells.icon - 12), big, shown, sizeof(shown));
      const int32_t taken = static_cast<int32_t>(font::width(face, shown, big));
      const int32_t big_h = static_cast<int32_t>((face.height != 0) ? face.height : font::kGlyphHeight) * big;
      draw_text(*target.canvas, face, icon_x + (cells.icon - taken) / 2, icon_y + (cells.icon - big_h) / 2, shown, big,
                chosen ? background : color, mode);
    }
    label_text(grid.face, (items[which].label != nullptr) ? items[which].label : "",
               static_cast<uint16_t>(box.w - 2 * cells.pad), scale, shown, sizeof(shown));
    const int32_t taken = static_cast<int32_t>(font::width(face, shown, scale));
    // **The name is in the ink even when the cell is chosen**: it sits on the paper of the page and not inside the box of the
    // mark, so it does not follow the inversion (docs/90).
    draw_text(*target.canvas, face, x + (box.w - taken) / 2, icon_y + cells.icon + 8, shown, scale, color, mode);
    if (focused && index == static_cast<uint8_t>(item)) {
      // The ring is the tone of the words, inside the cell and away from its edges, so it does not touch the box above it.
      const int32_t inset = 2;
      draw_frame_thick(*target.canvas, x + inset, y + inset, box.w - 2 * inset, box.h - 2 * inset, 2, color, mode);
    }
  }
  // The footer says which page it is, and the row of dots would say it too: a grid of a thousand cells has to say where a
  // person is without a scroll (docs/90).
  if (cells.footer > 0 && cells.pages > 1) {
    char page[16];
    snprintf(page, sizeof(page), "%d / %d", static_cast<int>(cells.page), static_cast<int>(cells.pages));
    const int32_t taken = static_cast<int32_t>(font::width(face, page, scale));
    const int32_t y = static_cast<int32_t>(height) - cells.footer + (cells.footer - line_h) / 2;
    draw_text(*target.canvas, face, target.x + (width - taken) / 2, target.y + y, page, scale, dim, mode);
    // A triangle on the side that has somewhere to go, which is the same signal the list gives (docs/90).
    put_rect(*target.canvas, target.x + 10, target.y + y + line_h / 2, 3, 1, dim);
    put_rect(*target.canvas, target.x + width - 13, target.y + y + line_h / 2, 3, 1, dim);
  }
  close_draw(target);
  return true;
}

uint8_t icon_grid_hit(int8_t slot, const GridItem* items, uint8_t count, uint8_t index, uint8_t columns,
                      uint8_t scale, uint16_t x, uint16_t y) {
  // The cells have no name and no mark: where a cell is does not depend on what it says, so the app does not have to give the
  // words again to ask where a finger landed (docs/90).
  (void)items;
  if (!live(slot) || count == 0) return 0;
  if (count > kMaxGridItems) count = kMaxGridItems;
  if (scale == 0) scale = 1;
  if (scale > 16) scale = 16;
  const Canvas& grid = g_canvases[slot];
  const GridCells cells = icon_grid_cells(grid, count, index, columns, scale);
  const int32_t at_x = static_cast<int32_t>(x);
  const int32_t at_y = static_cast<int32_t>(y);
  // The footer is no cell: a tap on it is not a tap on the last cell of the page.
  if (cells.footer > 0 && at_y >= static_cast<int32_t>(grid.height) - cells.footer) return 0;
  if (at_x < 0 || at_y < 0) return 0;
  const int32_t column = at_x / cells.cell_w;
  const int32_t row = at_y / cells.cell_h;
  if (column >= cells.columns || row >= cells.rows) return 0;
  const int32_t which = cells.first + row * cells.columns + column;
  if (which < 0 || which >= static_cast<int32_t>(count)) return 0;
  return static_cast<uint8_t>(which + 1);
}

// ---- a band of notifications: the cards and how they are painted (docs/30, docs/90) ----

namespace {

// **The band, measured once**: the height of a card —a title and a text—, how many fit, how many wait and how wide the
// cross is. The paint and the finger ask this, or the cross that is drawn is not the cross that dismisses (docs/90).
struct NotificationBand {
  int32_t card_h;
  int32_t drawn;
  int32_t waiting;
  int32_t close_w;
  int32_t pad;
};

NotificationBand notification_band(const Canvas& band, uint8_t shown, uint16_t total, uint8_t scale) {
  NotificationBand layout = {};
  const font::Face& face = face_of(band.face);
  const int32_t line_h = static_cast<int32_t>((face.height != 0) ? face.height : font::kGlyphHeight) * scale;
  layout.pad = (band.width > 200) ? 12 : 8;
  // **Two lines and the air above and under them**, and never under a finger's height (docs/90).
  layout.card_h = line_h * 2 + 20;
  if (layout.card_h < 56) layout.card_h = 56;
  if (layout.card_h > 200) layout.card_h = 200;
  // The room the `+N` line takes when there is one, which is half a card.
  const int32_t more = (total > shown) ? (line_h + 10) : 0;
  layout.close_w = (band.width > 200) ? 44 : 34;
  int32_t fit = (layout.card_h > 0) ? ((static_cast<int32_t>(band.height) - more) / layout.card_h) : 0;
  if (fit < 1) fit = 1;
  if (fit > static_cast<int32_t>(shown)) fit = shown;
  layout.drawn = fit;
  layout.waiting = static_cast<int32_t>(total) - fit;
  if (layout.waiting < 0) layout.waiting = 0;
  return layout;
}

// **Where a card is**: rank zero is the newest and it sits at the bottom of the frame, because that is the one nearest the
// finger and the one that goes away first (docs/90).
struct NotificationBox {
  int32_t x;
  int32_t y;
  int32_t w;
  int32_t h;
  int32_t close_x;
  int32_t close_w;
};

NotificationBox notification_box(const Canvas& band, const NotificationBand& layout, int32_t rank) {
  NotificationBox box;
  box.w = band.width;
  box.h = layout.card_h;
  box.x = 0;
  box.y = static_cast<int32_t>(band.height) - (rank + 1) * layout.card_h;
  box.close_w = layout.close_w;
  box.close_x = static_cast<int32_t>(band.width) - layout.close_w;
  return box;
}

// **The cross of a card**: two diagonals of the ink tone, and the box it is drawn in is the box that answers the finger.
void notification_cross(Canvas& canvas, int32_t x, int32_t y, int32_t size, uint8_t level, hal::UpdateMode mode) {
  const int32_t inner = size / 3;
  draw_line(canvas, x + inner, y + inner, x + size - inner - 1, y + size - inner - 1, level, mode);
  draw_line(canvas, x + size - inner - 1, y + inner, x + inner, y + size - inner - 1, level, mode);
}

}  // namespace

bool notification_bar_draw(int8_t slot, const Notification* items, uint8_t shown, uint16_t total, uint8_t color,
                           uint8_t dim, uint8_t background, uint8_t border, uint8_t scale, bool focused) {
  if (!live(slot) || color > 15 || dim > 15 || background > 15 || border > 15) return false;
  const Canvas& band = g_canvases[slot];
  const int32_t width = band.width;
  const int32_t height = band.height;
  if (width == 0 || height == 0) return true;
  if (scale == 0) scale = 1;
  if (scale > 16) scale = 16;
  if (items == nullptr || shown == 0 || total == 0) return true;
  if (shown > kMaxNotifications) shown = kMaxNotifications;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return false;
  const hal::UpdateMode mode = hal::UpdateMode::kDu;
  const font::Face& face = face_of(band.face);
  const int32_t line_h = static_cast<int32_t>((face.height != 0) ? face.height : font::kGlyphHeight) * scale;
  const NotificationBand layout = notification_band(band, shown, total, scale);
  static char cut[160];
  // **The `+N` line says how many are waiting**: without it, a notice that did not fit is a notice that has vanished, which
  // is what somebody asked of the old band (docs/90).
  int32_t more_y = 0;
  if (layout.waiting > 0) {
    char more[16];
    snprintf(more, sizeof(more), "+%d", static_cast<int>(layout.waiting));
    more_y = static_cast<int32_t>(band.height) - layout.drawn * layout.card_h - (line_h + 10);
    draw_text(*target.canvas, face, target.x + layout.pad, target.y + more_y + 5, more, scale, dim, mode);
  }
  for (int32_t rank = 0; rank < layout.drawn; ++rank) {
    // The newest is the last of the ones that were given, and the band shows the last ones: a notice that is too old to fit
    // is not on the glass and is counted in the `+N` (docs/90).
    const int32_t which = static_cast<int32_t>(shown) - 1 - rank;
    if (which < 0) break;
    const NotificationBox box = notification_box(band, layout, rank);
    const int32_t y = target.y + box.y;
    const int32_t x = target.x + box.x;
    draw_fill_rect(*target.canvas, static_cast<uint16_t>(x), static_cast<uint16_t>(y), static_cast<uint16_t>(box.w),
                   static_cast<uint16_t>(box.h), background, mode);
    // **The newest card is the one the centre would dismiss**, so it is the one that wears the weight of the focus.
    const bool newest = (rank == 0);
    draw_frame_thick(*target.canvas, x, y, box.w, box.h, (focused && newest) ? 2 : 1,
                     (focused && newest) ? color : border, mode);
    // The line that separates the card from the cross, and the cross itself.
    put_rect(*target.canvas, x + box.close_x, y + 1, 1, box.h - 2, border);
    mark_box(*target.canvas, x + box.close_x, y + 1, 1, box.h - 2, mode);
    notification_cross(*target.canvas, x + box.close_x + (box.close_w - 18) / 2, y + (box.h - 18) / 2, 18, color, mode);
    label_text(band.face, (items[which].title != nullptr) ? items[which].title : "",
               static_cast<uint16_t>(box.close_x - 2 * layout.pad), scale, cut, sizeof(cut));
    const int32_t top = y + 8;
    draw_text(*target.canvas, face, x + layout.pad, top, cut, scale, color, mode);
    const uint8_t small = static_cast<uint8_t>((scale > 1) ? scale - 1 : 1);
    const int32_t small_h = static_cast<int32_t>((face.height != 0) ? face.height : font::kGlyphHeight) * small;
    label_text(band.face, (items[which].text != nullptr) ? items[which].text : "",
               static_cast<uint16_t>(box.close_x - 2 * layout.pad), small, cut, sizeof(cut));
    draw_text(*target.canvas, face, x + layout.pad, top + line_h + 2, cut, small, dim, mode);
    (void)small_h;
  }
  close_draw(target);
  return true;
}

uint8_t notification_bar_card_at(int8_t slot, uint8_t shown, uint16_t total, uint8_t scale, uint16_t x, uint16_t y) {
  if (!live(slot) || shown == 0 || total == 0) return 0;
  if (shown > kMaxNotifications) shown = kMaxNotifications;
  if (scale == 0) scale = 1;
  if (scale > 16) scale = 16;
  const Canvas& band = g_canvases[slot];
  const NotificationBand layout = notification_band(band, shown, total, scale);
  const int32_t at_y = static_cast<int32_t>(y);
  for (int32_t rank = 0; rank < layout.drawn; ++rank) {
    const NotificationBox box = notification_box(band, layout, rank);
    if (at_y >= box.y && at_y < box.y + box.h && static_cast<int32_t>(x) >= box.x &&
        static_cast<int32_t>(x) < box.x + box.w) {
      return static_cast<uint8_t>(rank + 1);
    }
  }
  return 0;
}

bool notification_bar_close_at(int8_t slot, uint8_t shown, uint16_t total, uint8_t scale, uint16_t x, uint16_t y) {
  if (!live(slot) || shown == 0 || total == 0) return false;
  if (shown > kMaxNotifications) shown = kMaxNotifications;
  if (scale == 0) scale = 1;
  if (scale > 16) scale = 16;
  const Canvas& band = g_canvases[slot];
  const NotificationBand layout = notification_band(band, shown, total, scale);
  const int32_t at_y = static_cast<int32_t>(y);
  for (int32_t rank = 0; rank < layout.drawn; ++rank) {
    const NotificationBox box = notification_box(band, layout, rank);
    if (at_y >= box.y && at_y < box.y + box.h && static_cast<int32_t>(x) >= box.close_x &&
        static_cast<int32_t>(x) < box.x + box.w) {
      return true;
    }
  }
  return false;
}

// ---- a selector: a field that is chosen and not typed (docs/30, docs/90) ----

namespace {

// **The column of the triangle**: a fixed wide-enough target on the right of the field, and the triangle is nine rows of
// two pixels less each one. It is the door, and it is the only part of the field that changes when the list is open
// (docs/90).
const int32_t kSelectorMark = 34;
const int32_t kSelectorTriangle = 9;
const int32_t kSelectorPad = 12;

}  // namespace

bool selector_draw(int8_t slot, const char* label, const char* value, bool choosing, uint8_t color, uint8_t dim,
                   uint8_t background, uint8_t border, uint8_t scale, bool focused) {
  if (!live(slot) || color > 15 || dim > 15 || background > 15 || border > 15) return false;
  const Canvas& field = g_canvases[slot];
  const int32_t width = field.width;
  const int32_t height = field.height;
  if (width == 0 || height == 0) return true;
  if (scale == 0) scale = 1;
  if (scale > 16) scale = 16;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return false;
  const hal::UpdateMode mode = hal::UpdateMode::kDu;
  const font::Face& face = face_of(field.face);
  const int32_t line_h = static_cast<int32_t>((face.height != 0) ? face.height : font::kGlyphHeight) * scale;
  // **The frame is a hairline and the field is paper**: this is a place with a value in it and not a button, and the one
  // thing that would make it look like something to press is weight (docs/90).
  draw_fill_rect(*target.canvas, static_cast<uint16_t>(target.x), static_cast<uint16_t>(target.y),
                 static_cast<uint16_t>(width), static_cast<uint16_t>(height), background, mode);
  const int32_t thick = focused ? 2 : 1;
  draw_frame_thick(*target.canvas, target.x, target.y, width, height, thick, focused ? color : border, mode);
  // The column of the triangle, and its line in the border tone: it says where the field ends and where the door is.
  const int32_t mark_w = (width > kSelectorMark) ? kSelectorMark : width / 3;
  const int32_t mark_x = target.x + width - mark_w;
  put_rect(*target.canvas, mark_x, target.y + thick, 1, height - 2 * thick, border);
  mark_box(*target.canvas, mark_x, target.y + thick, 1, height - 2 * thick, mode);
  if (choosing) {
    // **An open field is paper and only its column goes to ink**: the field that goes dark from end to end reads as the
    // page going dark, which is what the owner said of the old one (docs/90).
    put_rect(*target.canvas, mark_x + 1, target.y + thick, mark_w - thick - 1, height - 2 * thick, color);
    mark_box(*target.canvas, mark_x + 1, target.y + thick, mark_w - thick - 1, height - 2 * thick, mode);
  }
  const int32_t centre_x = mark_x + mark_w / 2;
  const int32_t centre_y = target.y + height / 2;
  for (int32_t i = 0; i < kSelectorTriangle; ++i) {
    const int32_t run = (kSelectorTriangle - i) * 2;
    put_rect(*target.canvas, centre_x - run / 2, centre_y - kSelectorTriangle / 2 + i, run, 1,
             choosing ? background : color);
  }
  mark_box(*target.canvas, centre_x - kSelectorTriangle, centre_y - kSelectorTriangle / 2, kSelectorTriangle * 2,
           kSelectorTriangle, mode);
  // **The question in the dim tone and at the small size, the answer in the ink**: two faces and not a third colour, which
  // is what keeps this a two-tone control that a `Du` pass can serve (docs/90).
  static char shown[192];
  int32_t x = target.x + kSelectorPad + thick;
  int32_t room = (mark_x - 1) - x - kSelectorPad;
  if (room <= 0) {
    close_draw(target);
    return true;
  }
  const uint8_t small = static_cast<uint8_t>((scale > 1) ? scale - 1 : 1);
  const int32_t small_h = static_cast<int32_t>((face.height != 0) ? face.height : font::kGlyphHeight) * small;
  if (label != nullptr && label[0] != '\0') {
    label_text(field.face, label, static_cast<uint16_t>(room / 2), small, shown, sizeof(shown));
    const int32_t taken = static_cast<int32_t>(font::width(face, shown, small));
    draw_text(*target.canvas, face, x, target.y + (height - small_h) / 2, shown, small, dim, mode);
    x += taken + kSelectorPad;
    room -= taken + kSelectorPad;
  }
  if (room > 0) {
    label_text(field.face, (value != nullptr) ? value : "", static_cast<uint16_t>(room), scale, shown, sizeof(shown));
    draw_text(*target.canvas, face, x, target.y + (height - line_h) / 2, shown, scale, color, mode);
  }
  close_draw(target);
  return true;
}

// ---- a bar of menus: the titles and the one that is unfolded (docs/30, docs/90) ----

namespace {

// **The room a tick takes in an entry**: reserved for every row and not only for the ones that have a box, or the
// words of a menu would not line up in a column (docs/90).
const int32_t kMenuBarCheckRoom = 26;
const int32_t kMenuBarBox = 18;
const int32_t kMenuBarSeparatorRoom = 18;
const int32_t kMenuBarLeastRow = 52;

// **The row and the menu of a bar, in one place**: the width of every word, where the unfolded menu hangs and how tall
// its rows come out. The paint and the finger ask this and not two arithmetics (docs/90).
struct MenuBarGrid {
  int32_t bar_h;
  int32_t line;
  int32_t pad;
  int32_t title_x[kMaxMenuBarTitles];
  int32_t title_w[kMaxMenuBarTitles];
  int32_t menu_x;
  int32_t menu_y;
  int32_t menu_w;
  int32_t row_h;
};

int32_t menu_bar_line_h(const Canvas& bar, uint8_t scale) {
  const font::Face& face = face_of(bar.face);
  return static_cast<int32_t>((face.height != 0) ? face.height : font::kGlyphHeight) * scale;
}

MenuBarGrid menu_bar_grid(const Canvas& bar, const MenuBarTitle* titles, uint8_t count, uint8_t open,
                          uint16_t bar_height, uint8_t scale) {
  MenuBarGrid grid = {};
  const font::Face& face = face_of(bar.face);
  const int32_t width = static_cast<int32_t>(bar.width);
  const int32_t line_h = menu_bar_line_h(bar, scale);
  grid.line = 2;
  grid.pad = (width > 200) ? 12 : 8;
  // **A word of the bar is a thing a finger presses**, so the row is never under forty-four pixels and never under the
  // word it says (docs/90).
  int32_t bar_h = bar_height;
  if (bar_h < line_h + 16) bar_h = line_h + 16;
  if (bar_h < 44) bar_h = 44;
  grid.bar_h = bar_h;
  grid.row_h = line_h + 18;
  if (grid.row_h < kMenuBarLeastRow) grid.row_h = kMenuBarLeastRow;
  if (count > kMaxMenuBarTitles) count = kMaxMenuBarTitles;
  // Every word takes its width and the air of its own sides, and when they do not fit they share the row in equal
  // columns: a word with the end cut off is a word nobody reads (docs/90).
  int32_t total = 0;
  for (uint8_t i = 0; i < count; ++i) {
    const char* label = (titles[i].label != nullptr) ? titles[i].label : "";
    grid.title_w[i] = static_cast<int32_t>(font::width(face, label, scale)) + 2 * grid.pad;
    total += grid.title_w[i];
  }
  if (total > width && count > 0) {
    const int32_t even = width / count;
    for (uint8_t i = 0; i < count; ++i) grid.title_w[i] = even;
  }
  int32_t at = 0;
  for (uint8_t i = 0; i < count; ++i) {
    grid.title_x[i] = at;
    at += grid.title_w[i];
  }
  // **The menu hangs from the left of its word**, so the words of the menu and the word above them start in the same
  // column, and it moves back inside the frame when that word is one of the last ones (docs/90).
  if (open >= 1 && open <= count) {
    const MenuBarTitle& title = titles[open - 1];
    int32_t widest = 0;
    for (uint8_t i = 0; i < title.count; ++i) {
      if (title.entries[i].separator) continue;
      const char* label = (title.entries[i].label != nullptr) ? title.entries[i].label : "";
      const int32_t one = static_cast<int32_t>(font::width(face, label, scale)) + 2 * grid.pad + kMenuBarCheckRoom;
      if (one > widest) widest = one;
    }
    int32_t menu_w = (widest > 200) ? widest : 200;
    if (menu_w > width) menu_w = width;
    int32_t menu_x = grid.title_x[open - 1];
    if (menu_x + menu_w > width) menu_x = width - menu_w;
    if (menu_x < 0) menu_x = 0;
    grid.menu_x = menu_x;
    grid.menu_y = grid.bar_h + grid.line;
    grid.menu_w = menu_w;
  }
  return grid;
}

// **How tall the menu of a bar is**: a row for every entry and a short one for every line between two of them, which
// is what the drawing and the finger need to know before either of them walks it (docs/90).
int32_t menu_bar_height_of(const MenuBarTitle& title, int32_t row_h) {
  int32_t total = 0;
  for (uint8_t i = 0; i < title.count; ++i) {
    total += title.entries[i].separator ? kMenuBarSeparatorRoom : row_h;
  }
  return total + 4;
}

}  // namespace

bool menu_bar_draw(int8_t slot, const MenuBarTitle* titles, uint8_t count, uint8_t open, uint8_t index,
                   uint16_t bar_height, uint8_t color, uint8_t dim, uint8_t background, uint8_t scale,
                   bool focused) {
  if (!live(slot) || color > 15 || dim > 15 || background > 15) return false;
  const Canvas& bar = g_canvases[slot];
  const int32_t width = bar.width;
  const int32_t height = bar.height;
  if (width == 0 || height == 0) return true;
  if (scale == 0) scale = 1;
  if (scale > 16) scale = 16;
  if (titles == nullptr || count == 0) return true;
  if (count > kMaxMenuBarTitles) count = kMaxMenuBarTitles;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return false;
  const hal::UpdateMode mode = hal::UpdateMode::kDu;
  const font::Face& face = face_of(bar.face);
  const MenuBarGrid grid = menu_bar_grid(bar, titles, count, open, bar_height, scale);
  const int32_t line_h = menu_bar_line_h(bar, scale);
  static char shown[192];
  // **Only the row and the unfolded menu are painted**: the rest of the frame is the page's, and a bar that filled its
  // whole frame would be a bar that erased what the app drew under it. That is why a bar can be given the height of the
  // page: it uses the room only when a menu is open (docs/90).
  draw_fill_rect(*target.canvas, static_cast<uint16_t>(target.x), static_cast<uint16_t>(target.y),
                 static_cast<uint16_t>(width), static_cast<uint16_t>(grid.bar_h), background, mode);
  put_rect(*target.canvas, target.x, target.y + grid.bar_h, width, grid.line, color);
  mark_box(*target.canvas, target.x, target.y, width, grid.bar_h + grid.line, mode);
  if (focused) {
    // **The row says it has the dial** with a frame of two pixels: the same signal a control with the focus wears, and
    // only while nothing is open, because with a menu open it is the entries that take the arrows (docs/90).
    if (open == 0) {
      draw_frame_thick(*target.canvas, target.x, target.y, width, grid.bar_h, 2, color, mode);
    }
  }
  for (uint8_t i = 0; i < count; ++i) {
    const char* label = (titles[i].label != nullptr) ? titles[i].label : "";
    const bool is_open = (open == static_cast<uint8_t>(i + 1));
    const int32_t x = target.x + grid.title_x[i];
    if (is_open) {
      // **The word whose menu is down is inverted**: on this glass the one thing a person has to know is which menu is
      // the one they are reading (docs/90).
      draw_fill_rect(*target.canvas, static_cast<uint16_t>(x), static_cast<uint16_t>(target.y),
                     static_cast<uint16_t>(grid.title_w[i]), static_cast<uint16_t>(grid.bar_h), color, mode);
    }
    label_text(bar.face, label, static_cast<uint16_t>(grid.title_w[i] - 2 * grid.pad), scale, shown, sizeof(shown));
    const int32_t taken = static_cast<int32_t>(font::width(face, shown, scale));
    draw_text(*target.canvas, face, x + (grid.title_w[i] - taken) / 2,
              target.y + (grid.bar_h - line_h) / 2, shown, scale, is_open ? background : color, mode);
  }
  if (open < 1 || open > count) {
    close_draw(target);
    return true;
  }
  const MenuBarTitle& title = titles[open - 1];
  const int32_t menu_h = menu_bar_height_of(title, grid.row_h);
  const int32_t menu_x = target.x + grid.menu_x;
  const int32_t menu_y = target.y + grid.menu_y;
  // The card: a frame of two pixels and paper inside, with the weight of a card (docs/90).
  const int32_t frame = 2;
  int32_t radius = 8;
  const int32_t most = ((grid.menu_w < menu_h) ? grid.menu_w : menu_h) / 3;
  if (radius > most) radius = most;
  draw_round_fill(*target.canvas, menu_x, menu_y, grid.menu_w, menu_h, radius, color, mode);
  draw_round_fill(*target.canvas, menu_x + frame, menu_y + frame, grid.menu_w - 2 * frame, menu_h - 2 * frame,
                  radius - frame, background, mode);
  int32_t y = menu_y + 2;
  for (uint8_t i = 0; i < title.count; ++i) {
    const MenuBarEntry& entry = title.entries[i];
    if (entry.separator) {
      // **A line between two entries is not a row a finger presses**: it has its own air and the finger goes past it
      // (docs/90).
      put_rect(*target.canvas, menu_x + grid.pad, y + kMenuBarSeparatorRoom / 2, grid.menu_w - 2 * grid.pad, 2, color);
      mark_box(*target.canvas, menu_x + grid.pad, y + kMenuBarSeparatorRoom / 2, grid.menu_w - 2 * grid.pad, 2, mode);
      y += kMenuBarSeparatorRoom;
      continue;
    }
    const bool hot = focused && index == static_cast<uint8_t>(i + 1);
    if (hot) {
      // The ring of the dial, in the tone of the words, inside the row (docs/90).
      const int32_t inset = 2;
      draw_round_fill(*target.canvas, menu_x + inset, y + inset, grid.menu_w - 2 * inset, grid.row_h - 2 * inset, 6,
                      color, mode);
      draw_round_fill(*target.canvas, menu_x + inset + 2, y + inset + 2, grid.menu_w - 2 * inset - 4,
                      grid.row_h - 2 * inset - 4, 5, background, mode);
    }
    const int32_t box_x = menu_x + grid.pad;
    const int32_t box_y = y + (grid.row_h - kMenuBarBox) / 2;
    if (entry.checkable) {
      // **A setting keeps its box when it is off**: the empty square is what tells a switch from a command, and the
      // tick in its middle is the one level of ink that says "this one is on" (docs/90).
      draw_frame_thick(*target.canvas, box_x, box_y, kMenuBarBox, kMenuBarBox, 2, color, mode);
      if (entry.checked) put_rect(*target.canvas, box_x + 5, box_y + 5, kMenuBarBox - 10, kMenuBarBox - 10, color);
    }
    // **An entry that cannot be used now is the dim tone and not a third colour**: the ink is all the levels a control
    // may spend on "no", and its row is still there so the menu does not change size when it comes back (docs/90).
    label_text(bar.face, (entry.label != nullptr) ? entry.label : "",
               static_cast<uint16_t>(grid.menu_w - 2 * grid.pad - kMenuBarCheckRoom), scale, shown, sizeof(shown));
    draw_text(*target.canvas, face, box_x + kMenuBarCheckRoom, y + (grid.row_h - line_h) / 2, shown, scale,
              entry.enabled ? color : dim, mode);
    y += grid.row_h;
  }
  close_draw(target);
  return true;
}

uint8_t menu_bar_title_at(int8_t slot, const MenuBarTitle* titles, uint8_t count, uint16_t bar_height,
                          uint8_t scale, uint16_t x, uint16_t y) {
  if (!live(slot) || titles == nullptr || count == 0) return 0;
  if (count > kMaxMenuBarTitles) count = kMaxMenuBarTitles;
  if (scale == 0) scale = 1;
  if (scale > 16) scale = 16;
  const Canvas& bar = g_canvases[slot];
  const MenuBarGrid grid = menu_bar_grid(bar, titles, count, 0, bar_height, scale);
  if (static_cast<int32_t>(y) >= grid.bar_h) return 0;
  for (uint8_t i = 0; i < count; ++i) {
    if (static_cast<int32_t>(x) >= grid.title_x[i] &&
        static_cast<int32_t>(x) < grid.title_x[i] + grid.title_w[i]) {
      return static_cast<uint8_t>(i + 1);
    }
  }
  return 0;
}

uint8_t menu_bar_entry_at(int8_t slot, const MenuBarTitle* titles, uint8_t count, uint8_t open,
                          uint16_t bar_height, uint8_t scale, uint16_t x, uint16_t y) {
  if (!live(slot) || titles == nullptr || count == 0) return 0;
  if (count > kMaxMenuBarTitles) count = kMaxMenuBarTitles;
  if (open < 1 || open > count) return 0;
  if (scale == 0) scale = 1;
  if (scale > 16) scale = 16;
  const Canvas& bar = g_canvases[slot];
  const MenuBarGrid grid = menu_bar_grid(bar, titles, count, open, bar_height, scale);
  const int32_t at_x = static_cast<int32_t>(x) - grid.menu_x;
  const int32_t at_y = static_cast<int32_t>(y) - grid.menu_y;
  if (at_x < 0 || at_x >= grid.menu_w || at_y < 0) return 0;
  const MenuBarTitle& title = titles[open - 1];
  int32_t row_y = 2;
  for (uint8_t i = 0; i < title.count; ++i) {
    const int32_t row_h = title.entries[i].separator ? kMenuBarSeparatorRoom : grid.row_h;
    if (at_y >= row_y && at_y < row_y + row_h) {
      return title.entries[i].separator ? 0 : static_cast<uint8_t>(i + 1);
    }
    row_y += row_h;
  }
  return 0;
}

// ---- the keyboard: the band of the system at the bottom of the view (docs/90) ----

namespace {

// **What a key does**: a letter, or one of the five that are not a letter. There is no empty cell in a
// layout any more —the old one walked over blanks, and half a keyboard of blanks is a keyboard that looks
// broken— (docs/90).
enum class KeyAction : uint8_t { kChar, kCaps, kBack, kSpace, kEnter, kMode };

// **A key is a label and a share of its row**: `span` counts in twentieths of the width of the view, so a
// letter is two (a tenth, the unit a finger is measured in), the bar of the space is twelve (six tenths),
// and shift and enter are three. A label that is empty is a key drawn with an icon (docs/90).
struct KeyDef {
  const char* label;
  KeyAction action;
  uint8_t span;
};

// **A row is a staggered list of keys**: `indent` is the air before the first one, in the same twentieths,
// which is what makes the row of `a` sit half a key to the right of the row of `q` instead of in a grid
// nobody types in (docs/90).
struct RowDef {
  const KeyDef* keys;
  uint8_t count;
  uint8_t indent;
};

struct LayoutDef {
  const RowDef* rows;
  uint8_t count;
};

const KeyDef kQwertyRow0[] = {
    {"q", KeyAction::kChar, 2}, {"w", KeyAction::kChar, 2}, {"e", KeyAction::kChar, 2},
    {"r", KeyAction::kChar, 2}, {"t", KeyAction::kChar, 2}, {"y", KeyAction::kChar, 2},
    {"u", KeyAction::kChar, 2}, {"i", KeyAction::kChar, 2}, {"o", KeyAction::kChar, 2},
    {"p", KeyAction::kChar, 2},
};
const KeyDef kQwertyRow1[] = {
    {"a", KeyAction::kChar, 2}, {"s", KeyAction::kChar, 2}, {"d", KeyAction::kChar, 2},
    {"f", KeyAction::kChar, 2}, {"g", KeyAction::kChar, 2}, {"h", KeyAction::kChar, 2},
    {"j", KeyAction::kChar, 2}, {"k", KeyAction::kChar, 2}, {"l", KeyAction::kChar, 2},
};
const KeyDef kQwertyRow2[] = {
    {"MAY", KeyAction::kCaps, 3}, {"z", KeyAction::kChar, 2}, {"x", KeyAction::kChar, 2},
    {"c", KeyAction::kChar, 2}, {"v", KeyAction::kChar, 2}, {"b", KeyAction::kChar, 2},
    {"n", KeyAction::kChar, 2}, {"m", KeyAction::kChar, 2}, {"", KeyAction::kBack, 3},
};
const KeyDef kQwertyRow3[] = {
    {"123", KeyAction::kMode, 3}, {" ", KeyAction::kSpace, 12}, {".", KeyAction::kChar, 2},
    {"", KeyAction::kEnter, 3},
};

// **AZERTY is the second keyboard**, and the one the class was built for: the letters are not where a QWERTY
// puts them —the `a` and the `q` are the first of their rows, the `z` and the `w` are the other way round— and the
// capitals key says `MAJ` instead of `MAY`. Everything else, the digits, the space, the enter and the dial, is the
// common behaviour of the parent (0.93.0, docs/30, docs/90).
const KeyDef kAzertyRow0[] = {
    {"a", KeyAction::kChar, 2}, {"z", KeyAction::kChar, 2}, {"e", KeyAction::kChar, 2},
    {"r", KeyAction::kChar, 2}, {"t", KeyAction::kChar, 2}, {"y", KeyAction::kChar, 2},
    {"u", KeyAction::kChar, 2}, {"i", KeyAction::kChar, 2}, {"o", KeyAction::kChar, 2},
    {"p", KeyAction::kChar, 2},
};
const KeyDef kAzertyRow1[] = {
    {"q", KeyAction::kChar, 2}, {"s", KeyAction::kChar, 2}, {"d", KeyAction::kChar, 2},
    {"f", KeyAction::kChar, 2}, {"g", KeyAction::kChar, 2}, {"h", KeyAction::kChar, 2},
    {"j", KeyAction::kChar, 2}, {"k", KeyAction::kChar, 2}, {"l", KeyAction::kChar, 2},
    {"m", KeyAction::kChar, 2},
};
const KeyDef kAzertyRow2[] = {
    {"MAJ", KeyAction::kCaps, 3}, {"w", KeyAction::kChar, 2}, {"x", KeyAction::kChar, 2},
    {"c", KeyAction::kChar, 2}, {"v", KeyAction::kChar, 2}, {"b", KeyAction::kChar, 2},
    {"n", KeyAction::kChar, 2}, {"", KeyAction::kBack, 4},
};

const KeyDef kNumericRow0[] = {
    {"1", KeyAction::kChar, 2}, {"2", KeyAction::kChar, 2}, {"3", KeyAction::kChar, 2},
    {"4", KeyAction::kChar, 2}, {"5", KeyAction::kChar, 2}, {"6", KeyAction::kChar, 2},
    {"7", KeyAction::kChar, 2}, {"8", KeyAction::kChar, 2}, {"9", KeyAction::kChar, 2},
    {"0", KeyAction::kChar, 2},
};
const KeyDef kNumericRow1[] = {
    {"-", KeyAction::kChar, 2}, {"/", KeyAction::kChar, 2}, {":", KeyAction::kChar, 2},
    {";", KeyAction::kChar, 2}, {"(", KeyAction::kChar, 2}, {")", KeyAction::kChar, 2},
    {"@", KeyAction::kChar, 2}, {"\"", KeyAction::kChar, 2}, {"&", KeyAction::kChar, 2},
    {"%", KeyAction::kChar, 2},
};
const KeyDef kNumericRow2[] = {
    {".", KeyAction::kChar, 2}, {",", KeyAction::kChar, 2}, {"+", KeyAction::kChar, 2},
    {"=", KeyAction::kChar, 2}, {"*", KeyAction::kChar, 2}, {"#", KeyAction::kChar, 2},
    {"?", KeyAction::kChar, 2}, {"!", KeyAction::kChar, 2}, {"'", KeyAction::kChar, 2},
    {"~", KeyAction::kChar, 2},
};
const KeyDef kNumericRow3[] = {
    {"ABC", KeyAction::kMode, 3}, {"", KeyAction::kBack, 3}, {" ", KeyAction::kSpace, 11},
    {"", KeyAction::kEnter, 3},
};

const RowDef kQwertyRows[] = {{kQwertyRow0, 10, 0}, {kQwertyRow1, 9, 1}, {kQwertyRow2, 9, 0}, {kQwertyRow3, 4, 0}};
const RowDef kAzertyRows[] = {{kAzertyRow0, 10, 0}, {kAzertyRow1, 10, 0}, {kAzertyRow2, 8, 1}, {kQwertyRow3, 4, 0}};
// **Four rows in both alphabets**: the band of the keyboard is a piece of the page an app lays itself out around, so
// one that changes height when the digits come would move everything above it —and the strip it left behind would be
// the old row nobody painted over— (found on the bench, 4/11, docs/90).
const RowDef kNumericRows[] = {
    {kNumericRow0, 10, 0}, {kNumericRow1, 10, 0}, {kNumericRow2, 10, 0}, {kNumericRow3, 4, 0}};

const LayoutDef kQwertyLayout = {kQwertyRows, 4};
const LayoutDef kAzertyLayout = {kAzertyRows, 4};
const LayoutDef kNumericLayout = {kNumericRows, 4};

// **The whole of a row is twenty twentieths, which is the width of the page**: a row that adds up to more runs past the
// right edge and is cut there, and one that adds up to less leaves air. The air is what the row of `a` wants —one unit
// at each end, half a key of it, is what makes a staggered keyboard read as one— and a finger that lands on it is the
// keyboard's and presses nothing, which is what `keyboard_touch` answers (0.88.16, docs/90).
const uint8_t kRowUnits = 20;

// **The parent of every keyboard** (0.93.0): the band, its four rows, the finger, the dial, the block of the chosen
// key and the face are the same for all of them; what changes from one keyboard to another is the **layout** —which
// letter sits in each cell and what the capitals key is called— and that is the one question the parent asks its
// children. The state is a member and not a file-wide variable, so two keyboards cannot share a row by accident and
// changing the selected one is changing the object the free functions talk to (docs/30, docs/90).
class Keyboard {
 public:
  virtual ~Keyboard() {}
  virtual const char* id() const = 0;
  virtual const LayoutDef& layout(uint8_t kind) const = 0;

  bool show(uint8_t kind);
  bool hide();
  bool visible() const { return shown_; }
  uint8_t kind() const { return kind_; }
  uint16_t height() const;
  bool touch(uint16_t x, uint16_t y, bool down);
  bool key(const char* kind, uint32_t key_id);
  void paint();
  void page_cleared() { needs_paint_ = true; }
  void band_may_be_gone(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

 private:
  const LayoutDef& current_layout() const { return layout(kind_); }
  void measure();
  int32_t band_top() const;
  void note_band(int32_t x, int32_t y, int32_t w, int32_t h);
  void note_the_whole_band();
  void note_cell(int8_t row, int8_t key);
  const char* label_of(const KeyDef& key, char* buffer, size_t cap) const;
  void press_key(const KeyDef& key);
  bool key_at(uint16_t x, uint16_t y, int8_t& row, int8_t& index) const;

  bool shown_ = false;
  uint8_t kind_ = kKeyboardAlpha;
  // **The capitals are a lock** (0.84.4): the key turns them on and the key turns it off, and a letter does not put
  // them back (docs/90).
  bool shift_ = false;
  int8_t row_ = 0;
  int8_t key_ = 0;
  bool held_ = false;
  uint16_t key_h_ = 0;
  bool needs_paint_ = true;
  int32_t dx_ = 0;
  int32_t dy_ = 0;
  int32_t dw_ = 0;
  int32_t dh_ = 0;
};

// **The one thing a child says**: its name and the rows of its two alphabets. A future keyboard with another set of
// keys is another child and nothing of the parent changes (docs/30, docs/90).
class QwertyKeyboard final : public Keyboard {
 public:
  const char* id() const override { return "qwerty"; }
  const LayoutDef& layout(uint8_t kind) const override {
    return (kind == kKeyboardNumeric) ? kNumericLayout : kQwertyLayout;
  }
};

class AzertyKeyboard final : public Keyboard {
 public:
  const char* id() const override { return "azerty"; }
  const LayoutDef& layout(uint8_t kind) const override {
    return (kind == kKeyboardNumeric) ? kNumericLayout : kAzertyLayout;
  }
};

QwertyKeyboard g_qwerty;
AzertyKeyboard g_azerty;
Keyboard* const g_keyboards[] = {&g_qwerty, &g_azerty};
const uint8_t kKeyboardCount = static_cast<uint8_t>(sizeof(g_keyboards) / sizeof(g_keyboards[0]));
Keyboard* g_keyboard = &g_qwerty;

void keyboard_page_cleared() { g_keyboard->page_cleared(); }

// **What an app paints can land on the band** (0.84.4): a `ui.rect` over it, a pixel, or the whole page in one level,
// which is the common one —a page that starts again paints its background where the keyboard was—. The band is the top
// layer and it is drawn again at the next present, and this is the one thing that has to be noticed (docs/50, docs/90).
void the_band_may_be_gone(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  g_keyboard->band_may_be_gone(x, y, w, h);
}

// **Where a key is, in pixels**: the one function the paint and the finger share, because a key drawn
// where it cannot be pressed is a key that does not exist (docs/90).
void cell_place(const RowDef& row, uint8_t index, int32_t& x, int32_t& w) {
  const int32_t width = static_cast<int32_t>(root().width);
  int32_t before = row.indent;
  for (uint8_t i = 0; i < index; ++i) before += row.keys[i].span;
  const int32_t left = static_cast<int32_t>((static_cast<int64_t>(before) * width) / kRowUnits);
  const int32_t right = static_cast<int32_t>((static_cast<int64_t>(before + row.keys[index].span) * width) / kRowUnits);
  x = left;
  w = right - left;
}

// ---- the face of the band: a typeface and not the table of the bar (docs/30, docs/90) ----

// **The letters of the keys are DejaVu Sans Bold from `/fonts`**, the face every disk of this system carries, and the
// five by seven table of the bar when that file is not there: a table that is right for an hour and a load is not right
// for a letter a person looks at while typing. The file is nine kilobytes of the extra memory and it is kept while the
// band is on the glass, the same trade a page makes when it loads a face (docs/30, docs/75).
const char* const kKeyboardFont = "/fonts/dejavusans-bold.ttf";
const uint32_t kKeyboardFontBytes = 65536;
char* g_kbd_font_file = nullptr;
ttf::Face g_kbd_outline;
font::Face g_kbd_face;
bool g_kbd_face_tried = false;

void release_keyboard_face() {
  if (g_kbd_font_file != nullptr) {
    void* block = g_kbd_font_file;
    g_kbd_font_file = nullptr;
    pal::extra_memory().release(block);
  }
  memset(&g_kbd_outline, 0, sizeof(g_kbd_outline));
  g_kbd_face_tried = false;
}

const font::Face& keyboard_face() {
  if (!g_kbd_face_tried) {
    g_kbd_face_tried = true;
    hal::Stat info;
    if (vfs::stat(kKeyboardFont, info) && info.size > 0 && info.size <= kKeyboardFontBytes) {
      void* block = pal::extra_memory().allocate(static_cast<size_t>(info.size) + 1, true);
      if (block != nullptr) {
        char* text = static_cast<char*>(block);
        size_t read = 0;
        if (vfs::read_file(kKeyboardFont, text, static_cast<size_t>(info.size), read) && read == info.size) {
          text[read] = '\0';
          if (font::load_ttf(text, static_cast<uint32_t>(read), g_kbd_outline, g_kbd_face) == nullptr) {
            g_kbd_font_file = text;
          }
        }
        if (g_kbd_font_file == nullptr) pal::extra_memory().release(block);
      }
    }
  }
  return (g_kbd_outline.data != nullptr) ? g_kbd_face : font::system_face();
}

// **A letter is drawn at twice its size and every pixel of the key is the four samples of that block** (docs/90). The
// file draws curves and the panel has sixteen levels, so the ink of a diagonal is four steps and not a staircase —which
// is the whole difference between a keyboard and a grid of pixels—. It costs the glyphs of forty keys, and they are
// painted when the case or the alphabet changes and not on every key (docs/50).
const uint16_t kAaMaxWidth = 64;
const uint16_t kAaMaxHeight = 64;
const uint16_t kAaRowBytes = kAaMaxWidth / 8;
uint8_t g_aa_rows[kAaRowBytes * kAaMaxHeight];

bool aa_lit(uint16_t row, uint16_t column) {
  return (g_aa_rows[row * kAaRowBytes + (column >> 3)] & (0x80u >> (column & 7))) != 0;
}

// A level between the paper of the key and the ink of the letter, from zero to four samples of ink.
uint8_t aa_level(uint16_t covered, uint8_t ink, uint8_t paper) {
  const int32_t span = static_cast<int32_t>(ink) - static_cast<int32_t>(paper);
  int32_t level = static_cast<int32_t>(paper) + (span * static_cast<int32_t>(covered)) / 4;
  if (level < 0) level = 0;
  if (level > 15) level = 15;
  return static_cast<uint8_t>(level);
}

// One row of the glyph: the four samples of every pixel of the key, in runs of one level.
void aa_row(Canvas& view, int32_t x, int32_t y, uint16_t row, uint16_t width, uint8_t ink, uint8_t paper) {
  const uint16_t columns = static_cast<uint16_t>((width + 1) / 2);
  int32_t run_start = 0;
  uint8_t run_level = aa_level(0, ink, paper);
  for (uint16_t column = 0; column <= columns; ++column) {
    uint8_t level = 255;
    if (column < columns) {
      const uint16_t sx = static_cast<uint16_t>(2 * column);
      const uint16_t sy = static_cast<uint16_t>(2 * row);
      uint16_t covered = 0;
      if (sx < width && sy + 1 < kAaMaxHeight && aa_lit(sy, sx)) ++covered;
      if (sx + 1 < width && sy + 1 < kAaMaxHeight && aa_lit(sy, sx + 1)) ++covered;
      if (sx < width && sy + 2 <= kAaMaxHeight && aa_lit(sy + 1, sx)) ++covered;
      if (sx + 1 < width && sy + 2 <= kAaMaxHeight && aa_lit(sy + 1, sx + 1)) ++covered;
      level = aa_level(covered, ink, paper);
    }
    if (column > 0 && level != run_level) {
      if (run_level != paper && run_start < static_cast<int32_t>(column)) {
        put_rect(view, x + run_start, y + static_cast<int32_t>(row), static_cast<int32_t>(column) - run_start, 1,
                 run_level);
      }
      run_start = column;
      run_level = level;
    }
  }
}

// The text of a key from a face of outlines, with its four steps of ink. False when the glyphs do not fit the block of
// the bench, and then the caller draws them the plain way.
bool outline_key_text(Canvas& view, const font::Face& face, int32_t x, int32_t y, const char* text, uint8_t scale,
                      uint8_t ink, uint8_t paper) {
  const ttf::Face& font = *face.outline;
  const uint32_t body = (font.units_per_em == 0) ? 1 : font.units_per_em;
  const uint16_t px = static_cast<uint16_t>(font::kOutlineBody * scale * 2);
  const int32_t ascent = static_cast<int32_t>((static_cast<int32_t>(font.ascent) * px) / static_cast<int32_t>(body));
  int32_t pen = x;
  for (const char* at = text; *at != '\0'; ++at) {
    const uint8_t code = static_cast<uint8_t>(*at);
    if (code < 0x20 || code > 0x7F) continue;
    uint16_t glyph = 0;
    if (ttf::glyph_of(font, code, glyph) != ttf::kOk) continue;
    ttf::Bitmap box;
    if (ttf::render(font, glyph, px, g_aa_rows, static_cast<uint8_t>(kAaRowBytes), kAaMaxWidth, kAaMaxHeight, box) !=
        ttf::kOk) {
      return false;
    }
    const int32_t left = pen + box.left / 2;
    const int32_t top = y + ascent / 2 + box.top / 2;
    for (uint16_t row = 0; 2 * row + 1 < box.height; ++row) {
      aa_row(view, left, top, row, box.width, ink, paper);
    }
    pen += static_cast<int32_t>(ttf::advance(font, glyph, px)) / 2;
  }
  return true;
}

// **The text of a key**, whichever face the band has: an outline draws with its four steps, a table of bits draws as it
// always has (docs/30, docs/90).
void draw_key_text(Canvas& view, const font::Face& face, int32_t x, int32_t y, const char* text, uint8_t scale,
                   uint8_t ink, uint8_t paper, hal::UpdateMode mode) {
  if (text == nullptr || text[0] == '\0') return;
  if (face.outline != nullptr && outline_key_text(view, face, x, y, text, scale, ink, paper)) return;
  draw_text(view, face, x, y, text, scale, ink, mode);
}

// ---- the three icons of the keys that are not letters (docs/90) ----

void icon_arrow_left(Canvas& view, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t level) {
  const int32_t t = (h >= 30) ? 4 : 3;
  int32_t arm = (h < w) ? h : w;
  arm = (arm / 3) | 1;  // an odd arm keeps the shaft on one row of pixels
  const int32_t cx = x + w / 2;
  const int32_t cy = y + h / 2;
  put_rect(view, cx - arm, cy - t / 2, 2 * arm, t, level);
  for (int32_t i = 0; i < arm; ++i) put_rect(view, cx - arm + i, cy - i - t / 2, t, 2 * i + t, level);
}

// **The enter is the arrow that turns**: down the right and along the bottom to the left, which is the
// symbol of a key that ends a line and not the word for it (docs/90).
void icon_enter(Canvas& view, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t level) {
  const int32_t t = (h >= 30) ? 4 : 3;
  int32_t arm = ((h < w) ? h : w) / 3 | 1;
  const int32_t head = (arm / 2) | 1;
  const int32_t cx = x + w / 2;
  const int32_t cy = y + h / 2;
  const int32_t bar = cy + arm - t;
  put_rect(view, cx + arm - t, cy - arm, t, 2 * arm, level);  // the stroke that comes down
  put_rect(view, cx - arm, bar, 2 * arm - head, t, level);    // the bar it turns into
  // **The arrow of the end of a line**, centred on the bar and pointing left: the same triangle as the
  // shift's, one quarter turn away (docs/90).
  for (int32_t i = 0; i < head; ++i) {
    const int32_t tall = t + (2 * i * arm) / head;
    put_rect(view, cx - arm + i, bar + t / 2 - tall / 2, 1, tall, level);
  }
}

// **The space is a bar and it says nothing**: a short line in the middle is what a keyboard with no word
// on it draws, and the width of the bar is the message (docs/90).
void icon_space(Canvas& view, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t level) {
  const int32_t t = (h >= 30) ? 5 : 4;
  const int32_t r = t / 2;
  const int32_t left = x + w / 3;
  const int32_t length = w / 3;
  const int32_t middle = y + h / 2;
  put_rect(view, left, middle - r, length, t, level);
  // **The two ends are round**, the same as the strokes of a face of outlines: a bar with square ends is a bar of a
  // drawing made in a terminal (docs/90).
  draw_disc(view, left, middle, r, level, hal::UpdateMode::kDu);
  draw_disc(view, left + length, middle, r, level, hal::UpdateMode::kDu);
}

void draw_icon(Canvas& view, KeyAction action, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t level) {
  switch (action) {
    case KeyAction::kBack:
      icon_arrow_left(view, x, y, w, h, level);
      break;
    case KeyAction::kEnter:
      icon_enter(view, x, y, w, h, level);
      break;
    case KeyAction::kSpace:
      icon_space(view, x, y, w, h, level);
      break;
    case KeyAction::kChar:
    case KeyAction::kMode:
    case KeyAction::kCaps:
      break;
  }
}

// **What is pending on the glass before the band touches it, kept aside and given back**: the band is painted on the
// canvas at every present —it is the top layer and has to be there— and none of that paint is a reason to refresh the
// glass. Only the union of what truly changed goes out, and it is `note_band`'s (docs/50, docs/90).
struct DamageKeep {
  uint8_t count = 0;
  Damage region[kMaxDamage];

  void take(Canvas& canvas) {
    count = canvas.damage_count;
    if (count > kMaxDamage) count = kMaxDamage;
    for (uint8_t i = 0; i < count; ++i) region[i] = canvas.damage[i];
  }

  void give_back(Canvas& canvas) {
    canvas.damage_count = count;
    for (uint8_t i = 0; i < count; ++i) canvas.damage[i] = region[i];
  }
};

// **What of the band changed, and nothing else** (0.83.1): a letter typed does not change a pixel of the band.
void Keyboard::note_band(int32_t x, int32_t y, int32_t w, int32_t h) {
  if (w <= 0 || h <= 0) return;
  needs_paint_ = true;
  if (dw_ <= 0 || dh_ <= 0) {
    dx_ = x;
    dy_ = y;
    dw_ = w;
    dh_ = h;
    return;
  }
  const int32_t x1 = ((x + w) > (dx_ + dw_)) ? (x + w) : (dx_ + dw_);
  const int32_t y1 = ((y + h) > (dy_ + dh_)) ? (y + h) : (dy_ + dh_);
  if (x < dx_) dx_ = x;
  if (y < dy_) dy_ = y;
  dw_ = x1 - dx_;
  dh_ = y1 - dy_;
}

void Keyboard::note_the_whole_band() {
  const int32_t top = band_top();
  if (top < 0) return;
  note_band(0, top, static_cast<int32_t>(root().width), static_cast<int32_t>(root().height) - top);
}

// The cell of a key of the layout that is on the glass now, which is the rect a chosen key moves between.
void Keyboard::note_cell(int8_t row, int8_t key) {
  if (!shown_ || key_h_ == 0) return;
  const LayoutDef& current = current_layout();
  if (row < 0 || row >= static_cast<int8_t>(current.count)) return;
  const RowDef& def = current.rows[row];
  if (key < 0 || key >= static_cast<int8_t>(def.count)) return;
  int32_t x = 0;
  int32_t w = 0;
  cell_place(def, static_cast<uint8_t>(key), x, w);
  note_band(x, band_top() + row * static_cast<int32_t>(key_h_), w, key_h_);
}

void Keyboard::band_may_be_gone(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  if (!shown_ || key_h_ == 0) return;
  const int32_t top = band_top();
  if (top < 0) return;
  if (static_cast<int32_t>(y) + static_cast<int32_t>(h) <= top) return;
  if (static_cast<int32_t>(y) >= static_cast<int32_t>(root().height)) return;
  if (w == 0 || static_cast<int32_t>(x) >= static_cast<int32_t>(root().width)) return;
  needs_paint_ = true;
}

// The flat order of the keys of a layout, which is what the dial walks: row by row, left to right.
int32_t flat_count(const LayoutDef& layout) {
  int32_t total = 0;
  for (uint8_t row = 0; row < layout.count; ++row) total += layout.rows[row].count;
  return total;
}

int32_t flat_index(const LayoutDef& layout, int8_t row, int8_t key) {
  int32_t index = 0;
  for (int8_t walk = 0; walk < row && walk < static_cast<int8_t>(layout.count); ++walk) {
    index += layout.rows[walk].count;
  }
  return index + key;
}

void flat_place(const LayoutDef& layout, int32_t index, int8_t& row, int8_t& key) {
  for (uint8_t walk = 0; walk < layout.count; ++walk) {
    const int32_t count = layout.rows[walk].count;
    if (index < count) {
      row = static_cast<int8_t>(walk);
      key = static_cast<int8_t>(index);
      return;
    }
    index -= count;
  }
}

// **An app is told when the band takes part of its page and when it gives it back** (0.84.0): `on_resize` is the hook
// that already means "the view is another one, draw it again", and the height it carries is the one `ui.height()` says.
// It is not called from the `attach` of a new page —the band went before that— and a bench with no interpreter answers
// nothing, which is what `luabind::call_resize` does with no state (docs/30, docs/90).
void tell_the_app(uint16_t before) {
  if (!g_attached) return;
  const uint16_t after = height();
  if (after == before) return;
  luabind::call_resize(width(), after);
}

void Keyboard::measure() {
  const Canvas& view = root();
  if (view.width == 0 || view.height == 0) return;
  const int32_t rows = current_layout().count;
  if (rows <= 0) {
    key_h_ = 0;
    return;
  }
  int32_t key_h = 44;
  const int32_t room = (static_cast<int32_t>(view.height) * 2) / 5;
  if (key_h * rows > room) key_h = room / rows;
  if (key_h < 24) key_h = 24;
  key_h_ = static_cast<uint16_t>(key_h);
}

int32_t Keyboard::band_top() const {
  if (!shown_ || key_h_ == 0) return 0;
  return static_cast<int32_t>(root().height) - current_layout().count * static_cast<int32_t>(key_h_);
}

// **What a key says**: a letter, and the letter is in the case the keyboard is in; the two of the mode say
// what they would switch to and not what is on (docs/90).
const char* Keyboard::label_of(const KeyDef& key, char* buffer, size_t cap) const {
  if (key.action == KeyAction::kMode) {
    snprintf(buffer, cap, "%s", (kind_ == kKeyboardNumeric) ? "ABC" : "123");
    return buffer;
  }
  if (key.action == KeyAction::kCaps) {
    snprintf(buffer, cap, "%s", (key.label != nullptr) ? key.label : "");
    return buffer;
  }
  char letter = (key.label != nullptr) ? key.label[0] : '\0';
  if (shift_ && letter >= 'a' && letter <= 'z') letter = static_cast<char>(letter - 'a' + 'A');
  buffer[0] = letter;
  buffer[1] = '\0';
  return buffer;
}

// **What a key does**: a letter goes to the box as one number —in the case the keyboard is in— and the
// three that are not letters are the numbers a box understands. The shift is one capital and it lets go,
// and the mode redraws the band with the other alphabet (docs/90).
void Keyboard::press_key(const KeyDef& key) {
  switch (key.action) {
    case KeyAction::kChar: {
      char letter = (key.label != nullptr) ? key.label[0] : '\0';
      if (shift_ && letter >= 'a' && letter <= 'z') letter = static_cast<char>(letter - 'a' + 'A');
      luabind::ui_keyboard_typed(static_cast<int>(letter));
      break;
    }
    case KeyAction::kCaps:
      // **A lock and not a shift**: the key turns the alphabet to capitals and leaves it there, and the next press of
      // it takes them back. A shift of one letter is a keyboard of a phone; a page typed into with a dial wants the
      // mode to stay where a person put it (docs/90).
      shift_ = !shift_;
      break;
    case KeyAction::kBack:
      luabind::ui_keyboard_typed(8);
      break;
    case KeyAction::kSpace:
      luabind::ui_keyboard_typed(32);
      break;
    case KeyAction::kEnter:
      luabind::ui_keyboard_typed(13);
      break;
    case KeyAction::kMode: {
      kind_ = (kind_ == kKeyboardNumeric) ? kKeyboardAlpha : kKeyboardNumeric;
      shift_ = false;
      measure();
      const LayoutDef& current = current_layout();
      if (row_ >= static_cast<int8_t>(current.count)) row_ = 0;
      if (key_ >= static_cast<int8_t>(current.rows[row_].count)) key_ = 0;
      break;
    }
  }
  // **A letter, a space, a backspace or the enter do not change the picture of the band**: the app hears the key and
  // the box changes, and the band is the same band. The two that change it are the case and the alphabet (docs/90).
  if (key.action == KeyAction::kCaps || key.action == KeyAction::kMode) note_the_whole_band();
}

// **The key under a point of the view**, which is the cell the paint used: the finger and the glass see the
// same twenty lines (docs/90).
bool Keyboard::key_at(uint16_t x, uint16_t y, int8_t& row, int8_t& index) const {
  if (!shown_ || key_h_ == 0) return false;
  const int32_t top = band_top();
  if (top < 0 || static_cast<int32_t>(y) < top) return false;
  const LayoutDef& current = current_layout();
  const int32_t at_row = (static_cast<int32_t>(y) - top) / static_cast<int32_t>(key_h_);
  if (at_row < 0 || at_row >= static_cast<int32_t>(current.count)) return false;
  const RowDef& def = current.rows[at_row];
  for (uint8_t i = 0; i < def.count; ++i) {
    int32_t cell_x = 0;
    int32_t cell_w = 0;
    cell_place(def, i, cell_x, cell_w);
    if (static_cast<int32_t>(x) >= cell_x && static_cast<int32_t>(x) < cell_x + cell_w) {
      row = static_cast<int8_t>(at_row);
      index = static_cast<int8_t>(i);
      return true;
    }
  }
  return false;
}

bool Keyboard::show(uint8_t kind) {
  if (!g_attached || root().bits == nullptr) return false;
  const uint16_t before = height();
  kind_ = (kind == kKeyboardNumeric) ? kKeyboardNumeric : kKeyboardAlpha;
  shift_ = false;
  measure();
  if (key_h_ == 0) return false;
  shown_ = true;
  row_ = 0;
  key_ = 0;
  held_ = false;
  // **The face of the band is read once per time it comes up**: the file is nine kilobytes and it is kept while the
  // keyboard is on the glass (docs/30, docs/75).
  keyboard_face();
  note_the_whole_band();
  invalidate(0);
  tell_the_app(before);
  return true;
}

bool Keyboard::hide() {
  const uint16_t before = height();
  const bool was = shown_;
  shown_ = false;
  held_ = false;
  release_keyboard_face();
  if (was) {
    needs_paint_ = true;
    invalidate(0);
    tell_the_app(before);
  }
  return true;
}

uint16_t Keyboard::height() const {
  if (!shown_) return 0;
  return static_cast<uint16_t>(current_layout().count * static_cast<int32_t>(key_h_));
}

// **The keyboard is painted over the page and not in it**: the tree is composed first, and the band goes on
// the glass after it, so a control of the page under the band is covered and does not have to know that the
// keyboard is there (docs/90).
void Keyboard::paint() {
  if (!shown_ || key_h_ == 0) return;
  Canvas& view = root();
  if (view.bits == nullptr || view.width == 0) return;
  // **A band is a picture that changes when it changes and not on every present** (0.84.4): a letter typed does not
  // touch it —the box is the one that changes—, and redrawing forty glyphs of a face of outlines on the turn of a
  // blinking caret is the page paying for the keyboard. It is drawn when it comes up, when the case or the alphabet
  // changes, and when the page under it is wiped, which is what `needs_paint_` says (docs/50, docs/90).
  if (!needs_paint_) return;
  const int32_t top = band_top();
  if (top < 0) return;
  needs_paint_ = false;
  // **The band is painted with the wash of the levels and not with the quick one**: a key that changes the number
  // or the case of the letters puts new ink where the old letters were, and a `du` leaves the old ones showing
  // through —two keyboards on one band, which is what the bench showed when the mode was switched— (docs/50,
  // docs/90). It costs the nine or ten thousand pixels of the band and not the page.
  // **And the wash is `gld16` and not `gl16`** (0.85.4): both clean the sixteen levels the letters are drawn in, and
  // `gl16` does it with two blinks of the whole band —which is what a person sees on every MAY and every `123`— and
  // `gld16` does it without one. It is the rule of this project: nothing that flickers reaches the glass (docs/92).
  const hal::UpdateMode mode = hal::UpdateMode::kGld16;
  const font::Face& face = keyboard_face();
  const LayoutDef& current = current_layout();
  const int32_t key_h = static_cast<int32_t>(key_h_);
  DamageKeep keep;
  keep.take(view);
  const int32_t band = current.count * key_h;
  draw_fill_rect(view, 0, static_cast<uint16_t>(top), view.width, static_cast<uint16_t>(band), 15, mode);
  // A line of ink over the band: the page ends here.
  put_rect(view, 0, top, view.width, 1, 0);
  const int32_t body = static_cast<int32_t>((face.height != 0) ? face.height : font::kGlyphHeight);
  for (uint8_t row = 0; row < current.count; ++row) {
    const RowDef& def = current.rows[row];
    const int32_t y = top + row * key_h;
    for (uint8_t i = 0; i < def.count; ++i) {
      int32_t x = 0;
      int32_t w = 0;
      cell_place(def, i, x, w);
      const KeyDef& key = def.keys[i];
      // **The key the dial is on is a block of ink and the letter on it is paper**: on this glass there is
      // nothing else to say "this is the one", and the same block says the capitals are on (docs/90).
      const bool chosen = (row_ == row && key_ == i);
      const bool lit = chosen || (key.action == KeyAction::kCaps && shift_);
      const int32_t inset = 3;
      const int32_t box_w = w - 2 * inset;
      const int32_t box_h = key_h - 2 * inset;
      if (box_w <= 0 || box_h <= 0) continue;
      if (lit) draw_fill_rect(view, static_cast<uint16_t>(x + inset), static_cast<uint16_t>(y + inset),
                              static_cast<uint16_t>(box_w), static_cast<uint16_t>(box_h), 0, mode);
      const uint8_t ink = lit ? 15 : 0;
      const uint8_t paper = lit ? 0 : 15;
      char label[8];
      const char* shown = label_of(key, label, sizeof(label));
      if (shown[0] != '\0') {
        // **A word of a key is smaller than its letter**: `123` and `MAY` are three cells and a letter is one, and the
        // body that fits one of them is not the body that fits three (docs/90).
        const bool word = shown[1] != '\0';
        uint8_t scale = static_cast<uint8_t>(word ? 2 : ((key_h - 12) / body));
        if (scale < 2) scale = 2;
        if (scale > 4) scale = 4;
        int32_t taken = static_cast<int32_t>(font::width(face, shown, scale));
        while (scale > 1 && taken > box_w - 6) {
          --scale;
          taken = static_cast<int32_t>(font::width(face, shown, scale));
        }
        const int32_t line_h = body * scale;
        const int32_t at_x = x + (w - taken) / 2;
        // **The letters sit on the line and the line sits in the key**: a face of outlines is measured from the top
        // of its body, and the ink of a capital starts a fifth of it below that —a fixed share of the line, so the
        // same rule holds at every size— (docs/90).
        int32_t at_y = y + (key_h - line_h) / 2;
        if (face.outline != nullptr) at_y -= (line_h * 6) / 100;
        if (at_y < top) continue;
        draw_key_text(view, face, at_x, at_y, shown, scale, ink, paper, mode);
      } else {
        draw_icon(view, key.action, x + inset, y + inset, box_w, box_h, ink);
      }
    }
  }
  keep.give_back(view);
  // **Only what changed of it goes to the glass**, and with the levels' wash: the band is a picture of nine thousand
  // pixels and the three keys a finger moves are a tenth of it (docs/50, docs/90).
  if (dw_ > 0 && dh_ > 0) {
    mark_box(view, dx_, dy_, dw_, dh_, mode);
    dw_ = 0;
    dh_ = 0;
  }
}

bool Keyboard::touch(uint16_t x, uint16_t y, bool down) {
  if (!shown_) return false;
  if (!down) {
    const bool was = held_;
    held_ = false;
    return was;
  }
  // **One press is one key**: the glass hands a frame over once per scan while the finger is there, and a
  // keyboard that typed on every one of them would type a letter per scan of a held finger (docs/50,
  // docs/90). The finger of the bench is the same: a click is a frame and a held button is a run of them.
  if (held_) return true;
  int8_t row = 0;
  int8_t index = 0;
  if (!key_at(x, y, row, index)) {
    // **A finger on the band that is not on a key is still the keyboard's**: the page under the band must
    // not be pressed through it (docs/90).
    return static_cast<int32_t>(y) >= band_top();
  }
  held_ = true;
  note_cell(row_, key_);
  row_ = row;
  key_ = index;
  note_cell(row, index);
  press_key(current_layout().rows[row].keys[index]);
  return true;
}

bool Keyboard::key(const char* kind, uint32_t key_id) {
  if (!shown_ || kind == nullptr || held_) return false;
  if (strcmp(kind, "press") != 0) return false;
  const LayoutDef& current = current_layout();
  if (row_ >= static_cast<int8_t>(current.count)) row_ = 0;
  if (key_ >= static_cast<int8_t>(current.rows[row_].count)) key_ = 0;
  // **The three keys of the dial in the shape every page has**: key 0 is the centre, 1 is up and 2 is down
  // —the Paper's dial and the ATOM's single key both arrive here— and the walk goes round the keyboard in
  // the order it is drawn, so a row that is not the width of the last one costs nothing (docs/50, docs/90).
  if (key_id == 0) {
    press_key(current.rows[row_].keys[key_]);
    return true;
  }
  const int32_t total = flat_count(current);
  if (total <= 0) return true;
  const int32_t step = (key_id == 1) ? -1 : 1;
  int32_t next = (flat_index(current, row_, key_) + step) % total;
  if (next < 0) next += total;
  note_cell(row_, key_);
  flat_place(current, next, row_, key_);
  note_cell(row_, key_);
  return true;
}

}  // namespace

bool keyboard_show(uint8_t kind) { return g_keyboard->show(kind); }

bool keyboard_hide() { return g_keyboard->hide(); }

bool keyboard_visible() { return g_keyboard->visible(); }

uint16_t keyboard_height() { return g_keyboard->height(); }

void keyboard_paint() { g_keyboard->paint(); }

bool keyboard_touch(uint16_t x, uint16_t y, bool down) { return g_keyboard->touch(x, y, down); }

bool keyboard_key(const char* kind, uint32_t key_id) { return g_keyboard->key(kind, key_id); }

uint8_t keyboard_count() { return kKeyboardCount; }

const char* keyboard_at(uint8_t index) {
  return (index < kKeyboardCount) ? g_keyboards[index]->id() : nullptr;
}

const char* keyboard_current() { return g_keyboard->id(); }

// **Selecting a keyboard is selecting the object the band talks to** (0.93.0): the state of the one that goes is its
// own and does not leak into the next, and a band that is on the glass is taken down and put up again with the same
// alphabet so the page above it does not lose its height for a frame. The list is the children themselves, so a new
// keyboard is one more row and no other code learns its name (docs/30, docs/90).
bool keyboard_select(const char* id) {
  if (id == nullptr) return false;
  for (uint8_t i = 0; i < kKeyboardCount; ++i) {
    if (strcmp(g_keyboards[i]->id(), id) != 0) continue;
    if (g_keyboards[i] == g_keyboard) return true;
    const bool was = g_keyboard->visible();
    const uint8_t kind = g_keyboard->kind();
    if (was) g_keyboard->hide();
    g_keyboard = g_keyboards[i];
    if (was) g_keyboard->show(kind);
    return true;
  }
  return false;
}

//
// **A strip of dots with the one you are on bigger**, and nothing at all when there is one page: a single dot
// says nothing and it costs a paint of the panel to say it (docs/90).
bool dots_draw(int8_t slot, uint8_t pages, uint8_t current, uint8_t level, uint8_t background) {
  if (!live(slot) || level > 15 || background > 15) return false;
  const Canvas& strip = g_canvases[slot];
  const int32_t width = strip.width;
  const int32_t height = strip.height;
  if (width == 0 || height == 0) return true;
  if (pages <= 1) return true;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return false;
  const hal::UpdateMode mode = hal::UpdateMode::kDu;
  draw_fill_rect(*target.canvas, static_cast<uint16_t>(target.x), static_cast<uint16_t>(target.y),
                 static_cast<uint16_t>(width), static_cast<uint16_t>(height), background, mode);
  int32_t small = height / 3;
  if (small < 3) small = 3;
  if (small > 12) small = 12;
  const int32_t big = small + 4;
  // **The room of a dot is the room of the biggest one**, or the strip shifts sideways when the big one moves
  // from one end to the other (docs/90).
  const int32_t step = big + ((big > 8) ? 8 : 5);
  const int32_t total = static_cast<int32_t>(pages) * step;
  int32_t x = target.x + (width - total) / 2 + step / 2;
  const int32_t middle = target.y + height / 2;
  for (uint8_t i = 0; i < pages; ++i) {
    const int32_t radius = (static_cast<uint8_t>(i + 1) == current) ? big / 2 : small / 2;
    // A dot is a disc drawn row by row: the kernel has no floating point and a circle of eight pixels is a
    // square with the corners taken off (docs/90).
    for (int32_t row = -radius; row <= radius; ++row) {
      const int32_t wide = radius * radius - row * row;
      int32_t half = 0;
      while ((half + 1) * (half + 1) <= wide) ++half;
      put_rect(*target.canvas, x - half, middle + row, half * 2 + 1, 1, level);
    }
    mark_box(*target.canvas, x - radius, middle - radius, radius * 2 + 1, radius * 2 + 1, mode);
    x += step;
  }
  close_draw(target);
  return true;
}

bool present() {
  if (!g_attached || root().bits == nullptr) return false;
  compose_tree(0, -1, 0, 0);
  // **The keyboard goes over the page and not in it**: the tree is composed first and the band last, so a
  // control of the page under it is covered and does not have to know that the keyboard is there (docs/90).
  keyboard_paint();
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
  keyboard_paint();
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

bool font_info(int8_t handle, uint8_t& width, uint8_t& height, uint8_t& gap, uint16_t& glyphs) {
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
  // **A face of outlines has no cell and its count is the font's, not a table's**: `face.count` is zero for it
  // and a page that says "zero glyphs" about DejaVu is a number that is not true (docs/30).
  glyphs = (face.outline != nullptr) ? face.outline->glyphs : face.count;
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
