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
#include "kernel/touch.h"
#include "kernel/vfs.h"
#include "luabind/luabind.h"
#include "pal/pal.h"

#include <new>  // placement new: the table of canvases is raw memory from the extra memory
#include "ttf/ttf.h"

namespace kernel {
namespace ui {
namespace {

// **The canvases the system holds**, slot 0 is the view of the app and the rest are the controls an app makes
// inside it. **The table is the extra memory's and the memory is the ceiling** (0.102.0): half a megabyte of the
// PSRAM of the Paper holds twenty-seven hundred and thirty controls —a hundred and ninety-two bytes each— and the
// rest of the extra memory is left for the buffers, which are the ones that cost a quarter of a megabyte. The
// number of slots is asked with `canvas_count()` and not written here (docs/75, docs/90).
const size_t kSlotTableBytes = 512u * 1024u;
const uint16_t kMinSlots = 32;
Canvas* g_canvases = nullptr;
uint16_t g_canvas_count = 0;

// **The table of canvases, from the extra memory** and cut to what it will take: the block asked for is halved
// until the allocator says yes, so a board with little extra memory gets fewer slots instead of no display, and a
// board without extra memory at all still gets `kMinSlots` from the heap (docs/75).
bool boot_table() {
  if (g_canvases != nullptr) return true;
  size_t count = kSlotTableBytes / sizeof(Canvas);
  if (count > 32767u) count = 32767u;  // a slot is an `int16_t`
  while (count >= kMinSlots) {
    void* block = pal::extra_memory().allocate(count * sizeof(Canvas), true);
    if (block != nullptr) {
      Canvas* table = static_cast<Canvas*>(block);
      for (size_t i = 0; i < count; ++i) new (&table[i]) Canvas();
      g_canvases = table;
      g_canvas_count = static_cast<uint16_t>(count);
      // **The number is said once in the log**, because it is the board's memory talking and not a constant of the
      // code: it is the answer to "how many controls does this page hold" (0.102.0, docs/75).
      log_printf(pal::kLogInfo, "UI", "the table of canvases: %u slots of %u bytes",
                 static_cast<unsigned>(g_canvas_count), static_cast<unsigned>(sizeof(Canvas)));
      return true;
    }
    count /= 2;
  }
  return false;
}


bool g_attached = false;
// **Whether the app in front has the whole glass** (0.136.0): the view is then the panel and the bar of the system is
// away. It is the system's state and not a wish of the app's —an app that ends leaves it false— (docs/30, docs/46 D1).
bool g_full_screen = false;
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

// **The face the whole glass is drawn with** (0.96.0): the bold face of the disk —the same subset the paper project
// settled on— at `/fonts`, which is the alias of the disk's `res/fonts` and the only copy the board carries. A board
// without it keeps the five by seven of the flash and says so once in the log (docs/30, docs/70).
const char kSystemFace[] = "/fonts/dejavusans-bold.ttf";
void load_system_face();

// **What the page has shown since it was last cleaned**: which of the sixteen levels were painted, and how
// many boxes of pixels were put down. It is what the system reads to choose the cleaner between two apps: a
// page of text does not need the cleaner a photograph needs, and a page that has taken many quick paints
// needs a full one even if it is only text (docs/50, docs/90).
uint16_t g_levels = 0;
uint16_t g_paints = 0;
// **And the passes of ink the glass has been asked for since the page was last washed whole** (0.179.5): a pass is what
// leaves a ghost on an e-ink panel, so this is the budget the quality of a present counts, and it goes back to nothing
// every time the page goes up whole (docs/50).
uint16_t g_updates = 0;

// **The number of quick paints after which what is left is ghost**, which is what a panel shows after a few
// dozen partial updates: past this a page of text needs the full wash too, and the number is one constant to
// change after somebody counts it on the board (docs/50).
const uint16_t kManyPaints = 32;

void note_level(uint8_t level) {
  if (level > 15) return;
  g_levels = static_cast<uint16_t>(g_levels | (1u << level));
  ++g_paints;
}

Canvas& root() { return g_canvases[0]; }

// A slot is a control when it is in use, **with a buffer or without one**: a grid or a box is a control
// that paints nothing and still has a frame, children and a draw (docs/90).
bool live(int16_t slot) { return slot >= 0 && slot < g_canvas_count && g_canvases[slot].in_use; }

// The first canvas that holds this control: the nearest father —or the control itself— that has a buffer.
// It is where the pixels of a control without a view go (docs/90).
int16_t painting_holder(int16_t slot) {
  for (int16_t walk = slot; walk >= 0; walk = g_canvases[walk].parent) {
    if (g_canvases[walk].bits != nullptr) return walk;
  }
  return kNoSlot;
}

Canvas* find(int16_t slot) { return live(slot) ? &g_canvases[slot] : nullptr; }

// Two regions of the same quality this close become one paint: a frame drawn side by side, a line of
// text one letter at a time. Merging them costs a few pixels of ink and saves a whole update, and an
// update is what an e-ink panel is slow at (docs/50).
// **And the merge is only worth it when it does not paint the air between them** (0.177.4): the union of two rectangles
// that do not touch is a rectangle that also covers what nobody changed, and that is ink in the wrong place on a panel
// that flashes when it is written. Measured on the launcher: the cell the dial left and the cell it entered, one on the
// row above the other, came out as a single `500x246` —both rows of the grid— for two cells of `250x123` (docs/90).
const uint16_t kMergeGap = 2;
// The air a merge may add and still be worth one update less: a quarter of the smaller of the two, which keeps the small
// joins —a letter of a text, the two halves of a frame— and refuses the bands.
const int32_t kMergeWaste = 4;

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

// **The quality that reaches the glass when two pieces become one** (0.177.4): the one that cannot look wrong of the two,
// because a region is painted once and the ink of the lesser quality would lose what the other asked for. The order is
// this panel's and not the order of the enum: `gc16` is the clean page, `gl16` its greys and `du` the fast bilevel
// (docs/50).
uint8_t quality_rank(hal::UpdateMode mode) {
  switch (mode) {
    case hal::UpdateMode::kGc16:
      return 4;
    case hal::UpdateMode::kGl16:
    case hal::UpdateMode::kGlr16:
    case hal::UpdateMode::kGld16:
      return 3;
    case hal::UpdateMode::kDu4:
    case hal::UpdateMode::kA2:
      return 2;
    case hal::UpdateMode::kDu:
      return 1;
    default:
      return 0;
  }
}

hal::UpdateMode stronger_mode(hal::UpdateMode kept, hal::UpdateMode other) {
  return (quality_rank(other) > quality_rank(kept)) ? other : kept;
}

// **One piece leaves the map and the ones after it take a step back** (0.177.4): the list is what the glass owes, and a
// piece that another one already covers is a piece that would be painted twice.
void drop_region(Canvas& canvas, uint8_t at) {
  for (uint8_t i = static_cast<uint8_t>(at + 1); i < canvas.damage_count; ++i) {
    canvas.damage[i - 1] = canvas.damage[i];
  }
  if (canvas.damage_count > 0) --canvas.damage_count;
}

// Records one region. Same quality and close by: one paint. The whole list full: one paint, because an app that draws more
// pieces than the list holds is an app that is redrawing a page and a page is one pass of ink —with the quality the page is
// given when the present comes, not one this line guesses— (0.179.5, docs/30, docs/50).
void damage_add(Canvas& canvas, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                hal::UpdateMode mode, bool named = false) {
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
  // **A piece the map already owes is not added again** (0.177.4): the same pixels twice are two updates of one
  // rectangle, and the map is the answer to "what does the glass owe". It is what a child's own ink asked for one quality
  // and the box of the control that holds it asked for another —the picture of a cell inside the cell— and the record of
  // both was a paint of the picture after the paint of the cell that already had it (docs/30, docs/90).
  for (uint8_t i = 0; i < canvas.damage_count; ++i) {
    Damage& region = canvas.damage[i];
    if (region.x <= x && region.y <= y && region.x + region.w >= x1 && region.y + region.h >= y1) {
      region.mode = stronger_mode(region.mode, mode);
      region.named = region.named || named;
      return;
    }
  }
  // **And a piece that covers others takes them out of the map** (0.177.4), with the quality of the strongest of them.
  for (uint8_t i = 0; i < canvas.damage_count;) {
    const Damage& region = canvas.damage[i];
    if (region.x >= x && region.y >= y && region.x + region.w <= x1 && region.y + region.h <= y1) {
      mode = stronger_mode(mode, region.mode);
      named = named || region.named;
      drop_region(canvas, i);
      continue;
    }
    ++i;
  }
  // **Two pieces of one quality this close are one paint, as long as it does not paint the air between them** (0.177.4):
  // an update is what an e-ink panel is slow at, and a merged band that covers what nobody changed is ink in the wrong
  // place. The waste of the union may be a quarter of the smaller piece —the two halves of a frame, the letters of a line
  // of text— and no more; a cell and the cell of the row below were `500x246` for `2 x 250x123`, half of it air.
  const int32_t area = static_cast<int32_t>(w) * static_cast<int32_t>(h);
  for (uint8_t i = 0; i < canvas.damage_count; ++i) {
    Damage& region = canvas.damage[i];
    if (region.mode != mode) continue;
    const bool near = (x <= region.x + region.w + kMergeGap) &&
                      (region.x <= x1 + kMergeGap) && (y <= region.y + region.h + kMergeGap) &&
                      (region.y <= y1 + kMergeGap);
    if (!near) continue;
    const uint16_t merged_x0 = (x < region.x) ? x : region.x;
    const uint16_t merged_y0 = (y < region.y) ? y : region.y;
    const uint16_t merged_x1 = (x1 > region.x + region.w) ? x1
                                                          : static_cast<uint16_t>(region.x + region.w);
    const uint16_t merged_y1 = (y1 > region.y + region.h) ? y1
                                                          : static_cast<uint16_t>(region.y + region.h);
    const int32_t union_area = static_cast<int32_t>(merged_x1 - merged_x0) *
                               static_cast<int32_t>(merged_y1 - merged_y0);
    const int32_t region_area = static_cast<int32_t>(region.w) * static_cast<int32_t>(region.h);
    const int32_t waste = union_area - area - region_area;
    const int32_t smaller = (area < region_area) ? area : region_area;
    if (waste > smaller / kMergeWaste) continue;
    region.x = merged_x0;
    region.y = merged_y0;
    region.w = static_cast<uint16_t>(merged_x1 - merged_x0);
    region.h = static_cast<uint16_t>(merged_y1 - merged_y0);
    region.named = region.named || named;
    return;
  }
  if (canvas.damage_count == kMaxDamage) {
    // **The list is full and the page is one region** (0.179.5): the quality of that one region is the strongest of what it
    // holds —and whether one of them named it—, because a present with no quality of its own is the one that decides what a
    // page goes up with, and a page of nine boxes is a page and not a photograph (docs/30, docs/50).
    hal::UpdateMode all_mode = mode;
    bool all_named = named;
    for (uint8_t i = 0; i < canvas.damage_count; ++i) {
      all_mode = stronger_mode(all_mode, canvas.damage[i].mode);
      all_named = all_named || canvas.damage[i].named;
    }
    merge_all(canvas, all_mode);
    canvas.damage[0].named = all_named;
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
  region.named = named;
}

// Takes a canvas out of the list of children of its father, if it has one and it is there.
void unlink_from_parent(int16_t slot) {
  const int16_t father = g_canvases[slot].parent;
  if (father < 0) return;
  Canvas& parent = g_canvases[father];
  if (parent.first_child == slot) {
    parent.first_child = g_canvases[slot].next_sibling;
  } else {
    for (int16_t walk = parent.first_child; walk != kNoSlot; walk = g_canvases[walk].next_sibling) {
      if (g_canvases[walk].next_sibling == slot) {
        g_canvases[walk].next_sibling = g_canvases[slot].next_sibling;
        break;
      }
    }
  }
  g_canvases[slot].next_sibling = kNoSlot;
  g_canvases[slot].parent = kNoSlot;
}

// **Asks for a control to go away, and does not free it yet.** The rectangle it occupies is owed to the
// first control that paints —so the hole is painted over on the next draw— and every child goes with it.
// The buffer is wiped and the slot given back at the end of the next present, when the page is already
// clean (docs/90).
// The fills and the paper are written further down with the drawing, and the hole a release leaves needs them here.
bool draw_fill_rect(Canvas& canvas, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t level,
                    hal::UpdateMode mode, bool named = false);
void draw_round_fill(Canvas& canvas, int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius, uint8_t level,
                     hal::UpdateMode mode, bool named = false);
extern uint8_t g_paper_level;


// **A control that painted over the page and goes away owes the page** (0.173.2): a card has no buffer of its own, so its
// ink went into the pixels of the canvas that holds it, and those pixels are the only place it exists —the buffer of the
// card is nobody's—. When it is released, the region must be painted again, and **the children that have a buffer are the
// ones that can paint it**: the page of a reader is a canvas, its buffer is whole, and the card never wrote in it. This
// asks every buffered control of the subtree that overlaps the hole to compose itself again, which is what the rule of
// `invalidate` does for a father without a buffer, here said in the coordinates of the painter (docs/30, docs/90).
// **And what the app painted with its own primitives is not here**: a primitive is not a control, so the system cannot draw
// it again —a page with an `on_draw` is asked for it by `request_release`, and a page without one is a page whose
// primitives are lost with the card, which is the sentence docs/30 has (0.173.2, 0.173.3).
void invalidate_covered(int16_t under, int32_t x, int32_t y, uint16_t w, uint16_t h, int32_t origin_x,
                        int32_t origin_y) {
  for (int16_t index = g_canvases[under].first_child; index != kNoSlot; index = g_canvases[index].next_sibling) {
    if (!live(index) || g_canvases[index].pending_release) continue;
    Canvas& child = g_canvases[index];
    const int32_t at_x = origin_x + child.x;
    const int32_t at_y = origin_y + child.y;
    if (at_x + child.width > x && at_y + child.height > y && at_x < x + static_cast<int32_t>(w) &&
        at_y < y + static_cast<int32_t>(h)) {
      // **Every control, with a buffer or without one** (0.173.2): one with a buffer composes its whole box into the
      // page, and one without paints itself into the page when it is asked to draw —a label fills its own paper, a
      // button paints its frame—, which is what the page of `wipe` is made of. What is left is the app's own
      // primitives, and that is the sentence docs/30 has: a page painted with `ui.text` is painted again by the app.
      if (child.bits != nullptr) {
        damage_add(child, 0, 0, child.width, child.height, hal::UpdateMode::kGl16);
      }
      // **A control without a buffer needs nothing more than being asked**: a label is opaque and fills its own box,
      // and a button paints the box, the face and the border, so their own draw is the clean (docs/92).
      invalidate(index);
      invalidate_covered(index, x, y, w, h, at_x, at_y);
    }
  }
}

void request_release(int16_t slot, bool top) {
  if (!live(slot)) return;
  Canvas& control = g_canvases[slot];
  if (control.pending_release) return;
  control.pending_release = true;
  // **The children go with it, marked first** (0.173.2): what has to paint the hole is the page that stays, so the subtree
  // that is leaving is marked before the hole is filled —a child inside the hole that is going away is not a control that
  // owes a draw, and asking it would fill the list of damage with pieces of a hole that is already paper—. **The next one of
  // the list is read before the children are told**, because a child that goes away changes the list it is in.
  int16_t child = control.first_child;
  while (child != kNoSlot) {
    const int16_t next = g_canvases[child].next_sibling;
    request_release(child, false);
    child = next;
  }
  if (!top) return;
  // **Only the top of the subtree is owed to the painter.** A father that goes away is a rectangle that
  // has to be painted over once, and everything inside it goes with it: making each child say its own
  // rectangle would fill the list of eight with pieces of the same hole and end up in one `gc16` of the
  // whole union, which is the opposite of cheap (docs/90).
  {
    const int16_t painter = painting_holder(control.parent);
    if (painter >= 0) {
      int32_t offset_x = 0;
      int32_t offset_y = 0;
      for (int16_t walk = slot; walk != painter && walk >= 0; walk = g_canvases[walk].parent) {
        offset_x += g_canvases[walk].x;
        offset_y += g_canvases[walk].y;
      }
      // **The hole is paper before it is anything else** (0.173.2): the card painted its ink into the page's own pixels, so
      // the only way to take it away is to paint the paper over it —nobody keeps a copy of what was underneath, and the
      // system's answer to «what was there» is the app's controls, which paint themselves when they are asked—. Filled here,
      // cut at the painter's box, and added to its damage in `gl16`: a wash, because a `du` over a frame of ink keeps its
      // ghost (docs/30, docs/50).
      const int32_t page_w = static_cast<int32_t>(g_canvases[painter].width);
      const int32_t page_h = static_cast<int32_t>(g_canvases[painter].height);
      const int32_t hole_w = (offset_x + static_cast<int32_t>(control.width) <= page_w)
                                 ? static_cast<int32_t>(control.width)
                                 : (page_w - offset_x);
      const int32_t hole_h = (offset_y + static_cast<int32_t>(control.height) <= page_h)
                                 ? static_cast<int32_t>(control.height)
                                 : (page_h - offset_y);
      if (hole_w > 0 && hole_h > 0) {
        // **A page that paints itself is asked to paint the hole, and the system does not guess** (0.173.3): an app whose view
        // has an `on_draw` is called with its draw cut at this box —the same `begin_draw`/`end_draw` a control gets— and what
        // it paints there is the page as it wants it, primitives included. That is the exact answer and it costs the hole.
        // A page without one cannot paint itself, and then the hole is paper: the paper of the page and the controls that
        // were inside it, which is the 0.173.2 rule kept for the apps that do not want a draw of their own (docs/30, docs/90).
        if (!g_canvases[painter].paints_itself) {
          // **The shape the control drew with** (0.173.2): a card is a rounded box of radius twelve —cut at a third of its
          // shorter side, which is the arithmetic of `dialog_draw`— and its corners are the page's, so a plain rectangle would
          // erase four pieces of the page that the card never touched. Anything else is a square and gets a square.
          int32_t radius = 0;
          if (control.card) {
            radius = ((hole_w < hole_h) ? hole_w : hole_h) / 3;
            if (radius > 12) radius = 12;
          }
          draw_round_fill(g_canvases[painter], offset_x, offset_y, hole_w, hole_h, radius, g_paper_level,
                          hal::UpdateMode::kGl16);
        }
        // **And what was under it comes back**: the controls that were inside the hole paint themselves again in the same
        // turn, with a buffer of their own or without one (0.173.2, docs/30).
        invalidate_covered(painter, offset_x, offset_y, static_cast<uint16_t>(hole_w), static_cast<uint16_t>(hole_h), 0, 0);
      }
      invalidate_rect(painter, static_cast<uint16_t>(offset_x), static_cast<uint16_t>(offset_y),
                      control.width, control.height);
    }
  }
}

// Gives one canvas back, and everything that was inside it: a child whose parent is gone could never be
// composed and would keep its buffer for nothing (docs/90). **The buffer is wiped white first**, so that
// nothing of this control can be composed out of a block somebody else is about to get.
void release_slot(int16_t slot) {
  if (!live(slot)) return;
  // **The next of the list is read before the child is released**, because releasing a child changes the list it
  // came from (docs/90).
  int16_t child = g_canvases[slot].first_child;
  while (child != kNoSlot) {
    const int16_t next = g_canvases[child].next_sibling;
    release_slot(child);
    child = next;
  }
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
bool attach_to(int16_t slot, int16_t parent) {
  if (!live(slot) || slot == 0 || !live(parent) || slot == parent) return false;
  // A control cannot end up inside itself: the walk would never end.
  for (int16_t walk = parent; walk != kNoSlot; walk = g_canvases[walk].parent) {
    if (walk == slot) return false;
  }
  unlink_from_parent(slot);
  Canvas& father = g_canvases[parent];
  // **The last one added is the one in front**: the list is walked to its end and the new child goes there, and
  // there is no number of children that cannot be reached (docs/90).
  g_canvases[slot].next_sibling = kNoSlot;
  if (father.first_child == kNoSlot) {
    father.first_child = slot;
  } else {
    int16_t last = father.first_child;
    while (g_canvases[last].next_sibling != kNoSlot) last = g_canvases[last].next_sibling;
    g_canvases[last].next_sibling = slot;
  }
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
  for (uint16_t i = 1; i < g_canvas_count; ++i) release_slot(static_cast<int16_t>(i));
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

// **The size and the place of the view, made again** (0.136.0): the one place where the system decides how much glass an
// app has. `full` is the whole panel with no bar of the system; otherwise the panel under the bar, which is the view
// every app is given. False when there is no memory for the new view, and then **nothing has changed at all** — not the
// glass, not the bar, not the app (docs/30, docs/50, docs/46 D1).
bool make_view(bool full) {
  hal::ISurface* surface = display::surface();
  if (surface == nullptr) return false;
  const uint16_t top = full ? 0 : bar::height();
  if (surface->height() <= top) return false;
  if (!make_root(surface->width(), static_cast<uint16_t>(surface->height() - top))) return false;
  // The view of an app starts at the left corner of the page whether the bar is there or not: the bar is above it or
  // it is not, and never a margin on the side (docs/30).
  root().x = 0;
  root().y = top;
  g_full_screen = full;
  bar::show(!full);
  // **And the strip comes back whole whatever the bar believed it had** (0.164.3): `show(true)` already owes a whole
  // paint, and this is the same wish said again for the case the app painted over the top rows while it had them —
  // leaving full screen is the moment the glass under the strip stops being the app's (docs/30, docs/50).
  if (!full) bar::refresh();
  return true;
}

// Composes one region and asks for its paint. **The translation happens here and only here**: the
// damage is in the canvas' coordinates and the glass wants panel ones, and a view that sits in a corner
// with a strip of the system next to it is the normal case (docs/30).
bool paint(const Damage& region) {
  hal::ISurface* surface = display::surface();
  if (surface == nullptr) return false;
  // **The region goes to the driver with its name** (0.118.6): a page-sized one at info and a blink at debug, and the two
  // lines together —this one and the `paint` of the panel— say whether what the page asked for is what the glass was
  // told (docs/50).
  const bool big = static_cast<uint32_t>(region.w) * region.h >= 10000u;
  log_printf(big ? pal::kLogInfo : pal::kLogDebug, "UI", "paint %u,%u %ux%u mode %u %s",
             static_cast<unsigned>(region.x), static_cast<unsigned>(region.y), static_cast<unsigned>(region.w),
             static_cast<unsigned>(region.h), static_cast<unsigned>(region.mode),
             hal::mode_name(region.mode));
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
  if (w <= 0 || h <= 0) {
    // **A region that falls outside the panel is dropped, and it says so** (0.118.6): a page nobody sees is a page that
    // has to say which rectangle it was, or the driver looks innocent while the rectangle was never sent (docs/50).
    log_printf(pal::kLogWarn, "UI", "a region is off the glass: %u,%u %ux%u in a view of %u,%u %ux%u",
               static_cast<unsigned>(x), static_cast<unsigned>(y), static_cast<unsigned>(region.w),
               static_cast<unsigned>(region.h), static_cast<unsigned>(view.x), static_cast<unsigned>(view.y),
               static_cast<unsigned>(surface->width()), static_cast<unsigned>(surface->height()));
    return true;
  }
  // **A `du` never covers the glass** (0.151.2, docs/50). The two-level waveform is a *partial* one, and this
  // controller stops answering on the bus when it is asked for the whole page in it: from that moment every word it
  // hands back is `ffff`, every paint is accepted by nobody, and the glass keeps the page it had for ever — that is the
  // frozen board of the reader and of the splash, *«no pinta res de res al eink i després ningú pot tocar la pantalla
  // fins al reinici»* (measured on the board, 8/11, docs/92 §33). And it is ghosting as well: a page that big is a
  // wash, and a wash is `gc16`. The threshold is generous on purpose —seven eighths of the glass— so a block of text
  // that covers most of the view is still a `du` and the clock of the bar is still a `du`.
  hal::UpdateMode mode = region.mode;
  const uint32_t glass_area = static_cast<uint32_t>(surface->width()) * static_cast<uint32_t>(surface->height());
  if ((mode == hal::UpdateMode::kDu || mode == hal::UpdateMode::kDu4 || mode == hal::UpdateMode::kA2) &&
      glass_area != 0 && static_cast<uint32_t>(w) * static_cast<uint32_t>(h) >= (glass_area / 8u) * 7u) {
    log_printf(pal::kLogDebug, "UI", "a %s of %u,%u %ux%u is the whole glass, and it goes up as a gc16",
               (mode == hal::UpdateMode::kA2) ? "a2" : "du", static_cast<unsigned>(x), static_cast<unsigned>(y),
               static_cast<unsigned>(w), static_cast<unsigned>(h));
    mode = hal::UpdateMode::kGc16;
  }
  surface->blit(static_cast<uint16_t>(x), static_cast<uint16_t>(y), static_cast<uint16_t>(w),
                static_cast<uint16_t>(h),
                view.bits + static_cast<uint32_t>(region.y) * view.stride + region.x / 2,
                view.stride);
  return display::present(mode);
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
  damage_add(parent, at_x, at_y, w, h, region.mode, region.named);
}

// **The page is the white one the system handed over and nothing has been painted since** (0.168.0): `attach()` washes the
// view in one pass and this flag says so, which is what makes a `ui.clear(15, …)` of an app that has not drawn anything yet
// cost nothing at all —the paper is already clean— instead of a second whole-page clean on a glass that flashes every time
// it is cleaned (docs/30, docs/50). It is declared here and not with the other flags because the first place that reads it
// is the tree and the one that composes it.
bool g_page_owes_nothing = false;

// **The level the page's paper has** (0.173.2): the last level an app cleared its view with, white by default, because a hole
// left by a control that painted over the page has to be filled with something and paper is what a page is made of. An app
// that paints its page in ink says so with `ui.clear(0, …)` and gets ink; a page nobody cleared is the white one the system
// hands over (docs/30, docs/50).
uint8_t g_paper_level = 15;

// **And the ink that reached the view from a control's own canvas** (0.169.2): `g_levels` is what the page's primitives
// drew, and a control's pixels are composed into the view without passing through one of them —so a page that painted
// only in canvases read as white paper and was not washed between two apps, and the next one started on the ghost of
// it—. This flag is set by the composition and cleared by the wash that pays for it (docs/50, docs/90).
bool g_ink_on_the_view = false;

// **The tree, and the controls without a view are not a hole in it.** Depth first, in the order the
// controls were added —the one added later is in front—, and each view is composed into the first canvas
// that holds it: the offset is what every control between the two adds with its place, so a label inside a
// box inside a panel lands where the three of them say (docs/90).
void compose_tree(int16_t slot, int16_t destination, int32_t origin_x, int32_t origin_y) {
  Canvas& control = g_canvases[slot];
  // **The page is not the white one the system handed over any more** (0.168.0): the moment a control is composed into it
  // there is ink on the paper, and a `ui.clear` that came after it has something to erase again (docs/30, docs/50).
  if (slot == 0) {
    g_page_owes_nothing = false;
    g_ink_on_the_view = true;
  }
  // **The children first.** A father's buffer has to be complete before it travels up: composing him into
  // his own father and *then* composing his children into him was a father that reached the glass without
  // what his children had just put in him (found by this suite, 1/11).
  const int16_t theirs = (control.bits != nullptr) ? slot : destination;
  const int32_t base_x = (control.bits != nullptr) ? 0 : origin_x;
  const int32_t base_y = (control.bits != nullptr) ? 0 : origin_y;
  for (int16_t index = control.first_child; index != kNoSlot; index = g_canvases[index].next_sibling) {
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
//
// **A paint the panel refused is still owed** (0.118.2): the glass of the Paper says no to a page that arrives while
// another one is moving its ink (`wait_paint`, three seconds) and the splash of the boot moves it for longer than that.
// The region used to be forgotten with the rest —the list was emptied whatever happened—, so an app that presents once
// and then waits ran with the screen of the boot still on it: the owner saw exactly that on the board, *«les apps
// s'executen però no mostren res a pantalla»*. What did not go up goes back to the list, and the next `present` —or the
// turn of the kernel, through `idle()`— asks again (docs/50, docs/92 §33).
bool g_owed_paint = false;

// **A page the system owes whole** (0.167.0): giving a view back to the system changes the page the glass is showing and
// the page below was drawn before it, so the next present is the whole view with a mode that cleans instead of the
// damage of the page the app has just built (docs/30, docs/50).
bool g_owe_all = false;

// **And a paint is never asked for from inside a paint** (0.118.2): the wait for the ink turns the kernel on the emulator
// —`wait_and_pump`, docs/85— and the turn asks whether something is owed, so without this flag a refused page would be
// asked for again in the middle of the paint that is asking for it.
bool g_painting = false;

// **The sentence of a refused page is said once while it does not change** (0.151.5): a glass that is gone refuses every
// paint, and one warning per turn is a console nobody can read —the board of 8/11 filled a screen with the same line
// while the panel was being asked the same question three seconds at a time—. The reason is part of the sentence, so a
// reason that changes is said again.
char g_last_refusal[96] = {0};

bool paint_root() {
  if (g_painting) return false;
  g_painting = true;
  bool ok = true;
  // Each region with its own quality, in the order the app drew them: the first paint of a page and the
  // last one after it are not the same kind of thing (docs/30).
  const uint8_t count = root().damage_count;
  uint8_t kept = 0;
  for (uint8_t i = 0; i < count; ++i) {
    const Damage region = root().damage[i];
    if (paint(region)) {
      if (g_updates < 0xFFFF) ++g_updates;
      continue;
    }
    ok = false;
    // **And it is said out loud** (0.118.4): a page that does not go up is the difference between an app that works and
    // an app that runs with a dead glass, and the sentence of the panel is the only thing that tells the four reasons
    // apart. One line per paint, not per turn, because the retry would otherwise fill the console (docs/50, docs/92).
    const char* why = display::last_error();
    if (why == nullptr) why = "";
    if (g_last_refusal[0] == '\0' || strcmp(g_last_refusal, why) != 0) {
      snprintf(g_last_refusal, sizeof(g_last_refusal), "%s", why);
      log_printf(pal::kLogWarn, "UI", "the glass refused a page (%u,%u %ux%u, %s): %s",
                 static_cast<unsigned>(root().x + region.x), static_cast<unsigned>(root().y + region.y),
                 static_cast<unsigned>(region.w), static_cast<unsigned>(region.h),
                 hal::mode_name(region.mode), why);
    }
    // The refused one stays, and it stays in its own order: `kept` never passes `i`, so this is a copy in place.
    root().damage[kept++] = region;
  }
  root().damage_count = kept;
  if (kept == 0 && g_owed_paint) {
    log_printf(pal::kLogInfo, "UI", "the page the glass refused is up");
  }
  g_owed_paint = (kept > 0);
  settle_root();
  g_painting = false;
  return ok;
}

// **The glass under a view that has just been made again is washed before the app draws in it** (0.136.0): what is
// there is the strip of the bar or the page of an app that had the whole glass, and a page drawn over it with `du`
// would keep the ghost of the hour. It is the same wash `attach` asks for between two apps, with the cleaner the page
// that was here deserves, and the counters go back to nothing because the glass is white paper again (docs/50, docs/90).
//
// **`over_the_bar` is the view that has just grown over the strip of the system**: the hour and the line under it are
// ink the bar put there and not the app, and the levels of the app say nothing about them, so they are added here or a
// `du` wash —which is what an app that has drawn nothing deserves— leaves them on the glass as a ghost (docs/50).
void clean_the_glass(bool over_the_bar) {
  uint16_t levels = g_levels;
  if (over_the_bar) levels = static_cast<uint16_t>(levels | (1u << 0) | (1u << 15));
  const hal::UpdateMode mode = cleaner_for(levels, g_updates);
  damage_add(root(), 0, 0, root().width, root().height, mode);
  paint_root();
  g_levels = 0;
  g_paints = 0;
  g_updates = 0;
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

// **A block of pixels into a canvas** (0.133.0, docs/46 D4): the same nibble-per-pixel the decoder of `lib/img`
// hands out, copied row by row and cut at the canvas and at what the control owes. A picture drawn on a page is a
// **copy** and not a conversion, which is the whole reason this is thirty lines and not a second reader.
void put_bitmap(Canvas& canvas, int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t* bits,
                int32_t stride, bool dark) {
  if (bits == nullptr || w <= 0 || h <= 0) return;
  if (!clip_to_redraw(canvas, x, y, w, h)) return;

  int32_t from_x = 0;
  int32_t from_y = 0;
  int32_t width = w;
  int32_t height = h;
  if (x < 0) {
    from_x = -x;
    width += x;
    x = 0;
  }
  if (y < 0) {
    from_y = -y;
    height += y;
    y = 0;
  }
  if (x + width > canvas.width) width = canvas.width - x;
  if (y + height > canvas.height) height = canvas.height - y;
  if (width <= 0 || height <= 0) return;

  for (int32_t row = 0; row < height; ++row) {
    const uint8_t* source = bits + static_cast<int32_t>(from_y + row) * stride;
    uint8_t* line = canvas.bits + static_cast<uint32_t>(y + row) * canvas.stride;
    for (int32_t column = 0; column < width; ++column) {
      const int32_t at = from_x + column;
      const uint8_t byte = source[at / 2];
      const uint8_t read = static_cast<uint8_t>(((at & 1) == 0) ? (byte >> 4) : (byte & 0x0F));
      // **A page with its ink and its paper swapped takes the picture the other way round** (0.137.0, docs/46 C9): the
      // level of the file is the level of a page that is white, and on a page that is black the paper of a plate has to
      // be black too or the plate is a hole in the page.
      const uint8_t level = dark ? static_cast<uint8_t>(15 - read) : read;
      note_level(level);
      uint8_t& target = line[static_cast<uint32_t>(x + column) / 2];
      if (((x + column) & 1) == 0) {
        target = static_cast<uint8_t>((target & 0x0F) | (level << 4));
      } else {
        target = static_cast<uint8_t>((target & 0xF0) | level);
      }
    }
  }
}

// The box a shape covers, cut to the canvas, marked as one region.
void mark_box(Canvas& canvas, int32_t x, int32_t y, int32_t w, int32_t h, hal::UpdateMode mode,
              bool named = false) {
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
             static_cast<uint16_t>(h), mode, named);
}

void draw_frame(Canvas& canvas, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t level,
                hal::UpdateMode mode, bool named = false) {
  if (w <= 0 || h <= 0) return;
  put_rect(canvas, x, y, w, 1, level);              // the top
  put_rect(canvas, x, y + h - 1, w, 1, level);      // the bottom
  put_rect(canvas, x, y, 1, h, level);              // the left
  put_rect(canvas, x + w - 1, y, 1, h, level);      // the right
  mark_box(canvas, x, y, w, h, mode, named);
}

// **A frame of more than one pixel**, which is what the row of the focus of a bar and the frame of a selector wear: one
// pixel of ink on this glass is a line a finger does not see (docs/90).
void draw_frame_thick(Canvas& canvas, int32_t x, int32_t y, int32_t w, int32_t h, int32_t thickness,
                      uint8_t level, hal::UpdateMode mode, bool named = false) {
  if (w <= 0 || h <= 0 || thickness <= 0) return;
  for (int32_t i = 0; i < thickness; ++i) {
    if (w - 2 * i <= 0 || h - 2 * i <= 0) break;
    put_rect(canvas, x + i, y + i, w - 2 * i, 1, level);
    put_rect(canvas, x + i, y + h - 1 - i, w - 2 * i, 1, level);
    put_rect(canvas, x + i, y + i, 1, h - 2 * i, level);
    put_rect(canvas, x + w - 1 - i, y + i, 1, h - 2 * i, level);
  }
  mark_box(canvas, x, y, w, h, mode, named);
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
                     uint8_t level, hal::UpdateMode mode, bool named) {
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
  mark_box(canvas, x, y, w, h, mode, named);
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
               hal::UpdateMode mode, bool named = false) {
  const int32_t left = (x0 < x1) ? x0 : x1;
  const int32_t top = (y0 < y1) ? y0 : y1;
  const int32_t box_w = ((x0 < x1) ? x1 - x0 : x0 - x1) + 1;
  const int32_t box_h = ((y0 < y1) ? y1 - y0 : y0 - y1) + 1;
  if (x0 == x1 || y0 == y1) {
    put_rect(canvas, left, top, box_w, box_h, level);
    mark_box(canvas, left, top, box_w, box_h, mode, named);
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
  mark_box(canvas, left, top, box_w, box_h, mode, named);
}

// The midpoint circle, eight points at a time: no floating point and no table (docs/50).
void draw_circle(Canvas& canvas, int32_t cx, int32_t cy, int32_t radius, uint8_t level,
                 hal::UpdateMode mode, bool named = false) {
  if (radius <= 0) {
    put_pixel(canvas, cx, cy, level);
    mark_box(canvas, cx, cy, 1, 1, mode, named);
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
  mark_box(canvas, cx - radius, cy - radius, 2 * radius + 1, 2 * radius + 1, mode, named);
}

// The same circle with its inside: each step of the same walk is a row of the disc (docs/50).
void draw_disc(Canvas& canvas, int32_t cx, int32_t cy, int32_t radius, uint8_t level,
               hal::UpdateMode mode, bool named = false) {
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
  mark_box(canvas, cx - radius, cy - radius, 2 * radius + 1, 2 * radius + 1, mode, named);
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
               uint8_t scale, uint8_t level, hal::UpdateMode mode, const font::Ink& ink = font::Ink(),
               bool named = false) {
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
  font::runs(face, text, x, y, scale, box, canvas_run, &sink, ink);
  // What a text covers is its box, cut at the canvas: the runs are many and the damage is one. **The box
  // is measured with the face that was drawn**, which is not the system's when the canvas loaded one
  // (docs/30, docs/90).
  const int32_t text_w = font::width(face, text, scale);
  const int32_t line_h = static_cast<int32_t>((face.height != 0) ? face.height : font::kGlyphHeight);
  // **The damage of a text that is off the baseline covers the step too** (0.129.0): a `sup` paints above the
  // box of its line, and a box that stopped at the line would leave the mark of the note on the glass for ever
  // (docs/30, docs/46 A4).
  int32_t mark_at = y;
  int32_t mark_height = line_h * scale;
  if (ink.rise != 0) {
    const int32_t step = line_h * scale / 3;
    const int32_t down = line_h * scale / 5;
    mark_at -= step;
    mark_height += step + down;
  }
  mark_box(canvas, x, mark_at, text_w, mark_height, mode, named);
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

DrawTarget open_draw(int16_t slot) {
  DrawTarget out;
  if (!live(slot)) return out;
  const int16_t holder = painting_holder(slot);
  if (holder < 0) return out;
  Canvas& canvas = g_canvases[holder];
  out.canvas = &canvas;
  // The clip: the frame of the control, and of every control between it and the canvas that holds it,
  // **each one in the canvas' own pixels** —which is why the origin of each is the sum of all the places
  // above it, and not added one at a time (found by this suite, 1/11).
  // **The walk is a slot of the table and the table is as long as the memory** (0.110.0): this was an `int8_t`, so a
  // control past the hundred and twenty-seventh of a page walked its chain as a negative number, read the table before
  // its first slot and **took the process down** —the whole emulator stopped at the test of a page of a hundred and forty
  // buttons, and it is the same fault the slot of the dial had until 0.106.0— (docs/90, docs/92).
  int32_t offset_x = 0;
  int32_t offset_y = 0;
  for (int16_t walk = slot; walk != holder && walk >= 0; walk = g_canvases[walk].parent) {
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
  for (int16_t walk = slot; walk != holder && walk >= 0; walk = g_canvases[walk].parent) {
    const Canvas& control = g_canvases[walk];
    if (at_x > x0) x0 = at_x;
    if (at_y > y0) y0 = at_y;
    if (at_x + control.width < x1) x1 = at_x + control.width;
    if (at_y + control.height < y1) y1 = at_y + control.height;
    at_x -= control.x;
    at_y -= control.y;
  }
  if (holder != slot) {
    // **And what the control owes, brought into the canvas that draws it** (0.168.2): a control without a view of its own
    // paints straight into the nearest canvas that has one, so the rectangle of its redraw —two letters of a text box, one
    // ring of a button— has to be cut here as well, in the pixels of that canvas. Without this the redraw box lived on the
    // control and nothing applied it, which is why a blink of the caret went on painting the whole box (found on the emulator,
    // 8/11: `paint 20,40 420x64` for a caret of `9x33`) (docs/30, docs/90).
    const Canvas& control = g_canvases[slot];
    if (control.redraw_w > 0 && control.redraw_h > 0) {
      int32_t ox = 0;
      int32_t oy = 0;
      for (int16_t walk = slot; walk != holder && walk >= 0; walk = g_canvases[walk].parent) {
        ox += g_canvases[walk].x;
        oy += g_canvases[walk].y;
      }
      const int32_t rx0 = ox + control.redraw_x;
      const int32_t ry0 = oy + control.redraw_y;
      const int32_t rx1 = rx0 + control.redraw_w;
      const int32_t ry1 = ry0 + control.redraw_h;
      if (rx0 > x0) x0 = rx0;
      if (ry0 > y0) y0 = ry0;
      if (rx1 < x1) x1 = rx1;
      if (ry1 < y1) y1 = ry1;
    }
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

bool draw_clear(Canvas& canvas, uint8_t level, hal::UpdateMode mode, bool named = false) {
  if (canvas.bits == nullptr) return false;
  const uint8_t level_value = clamped(level);
  memset(canvas.bits, static_cast<uint8_t>((level_value << 4) | level_value), canvas.bytes);
  // **A page that was emptied in one level is a page that showed that level** (0.136.0): it is written with a `memset`
  // and not a box at a time, so it was the one primitive that told the counters nothing, and the wash that the system
  // chooses between two pages —and after an app that had the whole glass— read a black page as a page of nothing
  // (docs/50, docs/90).
  note_level(level_value);
  damage_add(canvas, 0, 0, canvas.width, canvas.height, mode, named);
  return true;
}

bool draw_fill_rect(Canvas& canvas, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t level,
                    hal::UpdateMode mode, bool named) {
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
  damage_add(canvas, x, y, w, h, mode, named);
  return true;
}

bool draw_pixel(Canvas& canvas, uint16_t x, uint16_t y, uint8_t level, hal::UpdateMode mode,
                bool named = false) {
  if (canvas.bits == nullptr) return false;
  if (x >= canvas.width || y >= canvas.height) return false;
  const uint8_t level_value = clamped(level);
  uint8_t& byte = canvas.bits[static_cast<uint32_t>(y) * canvas.stride + x / 2];
  if ((x & 1) == 0) {
    byte = static_cast<uint8_t>((byte & 0x0F) | (level_value << 4));
  } else {
    byte = static_cast<uint8_t>((byte & 0xF0) | level_value);
  }
  damage_add(canvas, x, y, 1, 1, mode, named);
  return true;
}

// **A box turned over: ink to paper and paper to ink, every level swapped with its opposite** (0.178.0). It is the one
// drawing that says "this is the thing that will answer" on this glass —a selected cell is a black block, not a grey one—
// and it is a primitive of the canvas and not a mode of the paint, because what has to be turned over is **what is
// already there**: a picture with its greys, a letter, the paper between them. The old project did exactly this with the
// rectangle of its selected cell (`invertRoundRect`), and that is why its icons seemed to float on the block: the icon's
// own white paper became the black of the block instead of a white box on top of it (docs/30, docs/90, docs/92).
bool draw_invert(Canvas& canvas, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 hal::UpdateMode mode, bool named = false) {
  if (canvas.bits == nullptr || w == 0 || h == 0) return false;
  int32_t cx = x;
  int32_t cy = y;
  int32_t cw = w;
  int32_t ch = h;
  if (!clip_to_redraw(canvas, cx, cy, cw, ch)) return true;
  if (cx >= canvas.width || cy >= canvas.height) return false;
  if (cx + cw > canvas.width) cw = canvas.width - cx;
  if (cy + ch > canvas.height) ch = canvas.height - cy;
  if (cw <= 0 || ch <= 0) return true;
  for (int32_t row = 0; row < ch; ++row) {
    uint8_t* line = canvas.bits + static_cast<uint32_t>(cy + row) * canvas.stride;
    for (int32_t column = 0; column < cw; ++column) {
      const int32_t at = cx + column;
      // The two pixels of a byte are two nibbles and each one is its own opposite: no nibble is shared, so a byte at a
      // time is a byte that can be turned over whole.
      uint8_t& byte = line[at / 2];
      if ((at & 1) == 0) {
        byte = static_cast<uint8_t>((byte & 0x0F) | ((15 - (byte >> 4)) << 4));
      } else {
        byte = static_cast<uint8_t>((byte & 0xF0) | (15 - (byte & 0x0F)));
      }
    }
  }
  // **The ink of the box is what it was, one level swapped for another**, so nothing is noted in the book of the levels:
  // that book answers "is this page one ink" for a wash, and a turn-over is every level of the palette at once.
  damage_add(canvas, static_cast<uint16_t>(cx), static_cast<uint16_t>(cy), static_cast<uint16_t>(cw),
             static_cast<uint16_t>(ch), mode, named);
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
  // **The mask of the row that is being written** (0.169.0), one nibble a pixel and `nullptr` when the app did not ask for one:
  // `15` is a pixel of the picture and `0` is a place the picture leaves alone, which is kept as the canvas had it.
  const uint8_t* alpha = nullptr;
};

// **The row of the mask, before the colours of it** (0.169.0): it is the picture's own transparency, and a row that has no
// mask is a row that asked for none —the pointer stays where it was and nothing is read— (docs/30).
void image_mask_row(void* context, uint16_t source_y, const uint8_t* packed) {
  (void)source_y;
  static_cast<ImagePlace*>(context)->alpha = packed;
}

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
      // **A transparent pixel is not a white one** (0.169.0): it is no pixel at all, and the canvas keeps what it had there.
      // The test is the middle of the sixteen levels, because the mask is a byte of the file squeezed into a nibble.
      if (place.alpha != nullptr) {
        const uint8_t alpha_byte = place.alpha[at / 2];
        const uint8_t alpha = ((at & 1) == 0) ? static_cast<uint8_t>(alpha_byte >> 4)
                                              : static_cast<uint8_t>(alpha_byte & 0x0F);
        if (alpha < 8) continue;
      }
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

bool invert_for_lua(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char* mode) {
  return invert(x, y, w, h, mode);
}

bool canvas_invert_for_lua(int16_t slot, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char* mode) {
  return canvas_invert(slot, x, y, w, h, mode);
}

bool present_for_lua(const char* mode) {
  if (mode == nullptr) return present();
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (!display::mode_from_name(mode, chosen)) return false;
  return present_all(chosen);
}

bool idle_for_lua() { return idle(); }

bool resize_for_lua(uint16_t width, uint16_t height) { return resize(width, height); }

uint16_t canvas_count_for_lua() { return canvas_count(); }

uint8_t damage_for_lua() { return damage_count(); }

uint16_t rotation_for_lua() { return rotation(); }

bool set_rotation_for_lua(uint16_t degrees) { return set_rotation(degrees); }

// **The whole glass, asked by the app** (0.136.0, docs/46 D1): the same two answers `ui.rotation` gives —set it, or ask
// it and get a boolean— and the system's own way out is `poll_full_screen`, which no app can call (docs/30).
bool full_screen_for_lua(bool on) { return full_screen(on); }
bool is_full_screen_for_lua() { return is_full_screen(); }

bool title_for_lua(const char* text) { return title(text); }

const char* last_error_for_lua() { return display::last_error(); }

bool canvas_create_for_lua(uint16_t width, uint16_t height, bool with_view, int16_t& slot,
                           uint16_t& generation) {
  generation = 0;
  if (!g_attached) return false;
  if (!create(width, height, 0, with_view, slot)) return false;
  generation = g_canvases[slot].generation;
  return true;
}

bool canvas_alive_for_lua(int16_t slot, uint16_t generation) {
  const Canvas* target = find(slot);
  return target != nullptr && target->generation == generation;
}

// **A control on its way out** (0.172.1): given back by the app and still on the glass until the paint that covers it, which is
// the question the focus asks about a card that is closing (docs/30, docs/90).
bool canvas_pending_for_lua(int16_t slot) {
  const Canvas* target = find(slot);
  return target != nullptr && target->pending_release;
}

// Two wrappers and not the public names: `place` is also the move of the view, and a pointer taken from an
// overloaded name is a thing the next reader has to work out (docs/30).
bool canvas_place_for_lua(int16_t slot, uint16_t x, uint16_t y) { return place(slot, x, y); }

bool canvas_release_for_lua(int16_t slot) { return release(slot); }

bool canvas_caps_for_lua(int16_t slot, uint16_t& width, uint16_t& height, uint16_t& x, uint16_t& y) {
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
uint16_t panel_layout_for_lua(int16_t slot, uint8_t flow, uint16_t padding, uint16_t gap,
                              const luabind::UIOps::UIPanelChild* children, uint16_t count) {
  if (children == nullptr || count == 0) return 0;
  // **The list of a panel is copied into a block of its own and not into sixteen places on the stack** (0.102.0):
  // a panel of forty rows is a page, and the sixteen was the stack talking (docs/75, docs/90).
  PanelChild on_stack[16];
  PanelChild* mine = on_stack;
  void* block = nullptr;
  if (count > 16) {
    block = pal::extra_memory().allocate(sizeof(PanelChild) * count, true);
    if (block != nullptr) mine = static_cast<PanelChild*>(block);
  }
  const uint16_t used = (block != nullptr || count <= 16) ? count : 16;
  for (uint16_t i = 0; i < used; ++i) {
    mine[i].slot = children[i].slot;
    mine[i].weight = children[i].weight;
    mine[i].share = children[i].share;
  }
  const uint16_t moved =
      panel_layout(slot, static_cast<PanelFlow>(flow), padding, gap, mine, used);
  if (block != nullptr) pal::extra_memory().release(block);
  return moved;
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
bool menu_draw_for_lua(int16_t slot, const char* title, const luabind::UIOps::UIMenuOption* items, uint8_t count,
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

uint8_t menu_hit_for_lua(int16_t slot, const char* title, const luabind::UIOps::UIMenuOption* items, uint8_t count,
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

bool menu_bar_draw_for_lua(int16_t slot, const luabind::UIOps::UIMenuBarTitle* titles, uint8_t count, uint8_t open,
                           uint8_t index, uint16_t bar_height, uint8_t color, uint8_t dim, uint8_t background,
                           uint8_t scale, bool focused) {
  MenuBarTitle mine[kMaxMenuBarTitles];
  MenuBarEntry entries[kMaxMenuBarEntries];
  if (!menu_bar_build(titles, count, mine, entries)) return false;
  return menu_bar_draw(slot, mine, count, open, index, bar_height, color, dim, background, scale, focused);
}

uint8_t menu_bar_title_at_for_lua(int16_t slot, const luabind::UIOps::UIMenuBarTitle* titles, uint8_t count,
                                  uint16_t bar_height, uint8_t scale, uint16_t x, uint16_t y) {
  MenuBarTitle mine[kMaxMenuBarTitles];
  MenuBarEntry entries[kMaxMenuBarEntries];
  if (!menu_bar_build(titles, count, mine, entries)) return 0;
  return menu_bar_title_at(slot, mine, count, bar_height, scale, x, y);
}

uint8_t menu_bar_entry_at_for_lua(int16_t slot, const luabind::UIOps::UIMenuBarTitle* titles, uint8_t count,
                                  uint8_t open, uint16_t bar_height, uint8_t scale, uint16_t x, uint16_t y) {
  MenuBarTitle mine[kMaxMenuBarTitles];
  MenuBarEntry entries[kMaxMenuBarEntries];
  if (!menu_bar_build(titles, count, mine, entries)) return 0;
  return menu_bar_entry_at(slot, mine, count, open, bar_height, scale, x, y);
}

// **The cards of a band, said in the kernel's words**: three of them at most on the glass, and the rest are counted by the
// control (docs/90).
bool notification_bar_draw_for_lua(int16_t slot, const luabind::UIOps::UINotification* items, uint8_t shown,
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

bool icon_grid_draw_for_lua(int16_t slot, const luabind::UIOps::UIGridItem* items, uint8_t count, uint8_t index,
                            uint8_t marked, uint8_t columns, uint8_t color, uint8_t dim, uint8_t background,
                            uint8_t scale, bool focused, bool cells_of_the_kernel) {
  GridItem mine[kMaxGridItems];
  if (!icon_grid_build(items, count, mine)) return false;
  return icon_grid_draw(slot, mine, count, index, marked, columns, color, dim, background, scale, focused,
                        cells_of_the_kernel);
}

bool icon_grid_cell_for_lua(int16_t slot, uint8_t count, uint8_t index, uint8_t columns, uint8_t scale,
                            uint16_t which, uint16_t& x, uint16_t& y, uint16_t& w, uint16_t& h) {
  return icon_grid_cell(slot, count, index, columns, scale, which, x, y, w, h);
}

uint8_t icon_grid_hit_for_lua(int16_t slot, const luabind::UIOps::UIGridItem* items, uint8_t count, uint8_t index,
                              uint8_t columns, uint8_t scale, uint16_t x, uint16_t y) {
  GridItem mine[kMaxGridItems];
  if (!icon_grid_build(items, count, mine)) return 0;
  return icon_grid_hit(slot, mine, count, index, columns, scale, x, y);
}

bool icon_grid_ring_for_lua(int16_t slot, uint8_t count, uint8_t from, uint8_t to, bool cursor_moved,
                            uint8_t columns, uint8_t scale, bool cells_of_the_kernel) {
  return icon_grid_ring(slot, count, from, to, cursor_moved, columns, scale, cells_of_the_kernel);
}

}  // namespace

// **The present that keeps the boxes apart** (0.168.1), declared here because the table of the ops is filled a few lines
// below and its body lives with the other presents of the page, after `paint_root` (docs/30).
bool present_each(hal::UpdateMode mode);

// **The strip of dots of the old project, measured once** (0.112.0): the numbers are the ones its owner settled on
// with the board in his hand —*«vull que els cercles siguin una mica mes grans, amb una vora negra i les boles
// unides per un pal»*, and then *«l'actual mes fosc que els altres»* and *«el cercle que representa la pagina actual
// ha de ser mes gros que els altres»*— and they are **fixed**, and not a third of the height of the box: a strip of
// this build has to look the same in every app that shows it, and a counter whose balls change size with the box they
// were given is not the same counter (docs/30, docs/92 §27).
const int32_t kDotSmall = 19;  // the diameter of the balls of the pages that are not open, drawn as a ring of ink
const int32_t kDotBig = 25;    // the page that is read: a solid disc, and the room of every ball, or the strip moves
const int32_t kDotRing = 2;    // the ink of a ring: two pixels, and the paper inside is what cuts the rod into beads
const int32_t kDotRod = 3;     // the rod the balls are threaded on, seen only in the air between two of them
const int32_t kDotGap = 7;     // the air between two balls, so that two of them are two and not one
const int32_t kDotAir = 12;    // the air the strip leaves under itself, so it does not hug the edge of the glass
const int32_t kDotDesignH = kDotBig + kDotAir;  // the box the design asks for: thirty-seven pixels
// **The rows a foot needs before the strip's box**: the line that closes the rows and three of air, so the ball of a list
// sits where the ball of the other two controls sits (0.176.0, docs/30, docs/92).
const int32_t kFootRule = 4;

// **The paginator of the system, painted in a box** (0.174.2): the foot of a list and the `ui.dots` control are the same
// drawing —the balls of the paper project, a rod through them, the page that is read bigger and a line of marks that
// closes a window that does not fit— and this is the one place that draws it. False when the box cannot say it, and then
// the caller decides what to put there (docs/30, docs/90).
bool paint_dots(Canvas& canvas, int32_t x, int32_t y, int32_t width, int32_t ball, uint16_t pages,
                uint16_t current, uint8_t level, uint8_t background, hal::UpdateMode mode);

bool boot(hal::IPlatform& platform, uint16_t top_rows) {
  (void)platform;
  // **The face of the whole glass, before anything is drawn and whether or not there is a glass**: the typeface is the
  // system's and not the panel's, and a board whose model has no screen still asks for the same text —a run with no
  // display writes to the console and the pages of a window on the emulator are drawn with it too— (docs/30, docs/92).
  load_system_face();
  // **The table of canvases first**: it is the extra memory's and its size is the number of controls this board can
  // hold, so everything after it —the view, the buffers— is taken knowing that this much is spoken for (0.102.0,
  // docs/75, docs/90).
  if (!boot_table()) return false;
  if (!display::available()) return false;
  // The view of an app is the glass **under the bar of the system**, and never the bar: a page that goes
  // wrong cannot take the hour with it (docs/30).
  if (display::height() <= top_rows) return false;
  const uint16_t usable = static_cast<uint16_t>(display::height() - top_rows);
  if (!make_root(display::width(), usable)) return false;
  root().y = top_rows;
  luabind::set_ui_ops(luabind::UIOps{caps_for_lua,
                                     canvas_count_for_lua,
                                     clear_for_lua,
                                     rect_for_lua,
                                     pixel_for_lua,
                                     present_for_lua,
                                     [](uint8_t mode) { return present_each(static_cast<hal::UpdateMode>(mode)); },
                                     idle_for_lua,
                                     resize_for_lua,
                                     damage_for_lua,
                                     last_error_for_lua,
                                     title_for_lua,
                                     rotation_for_lua,
                                     set_rotation_for_lua,
                                     full_screen_for_lua,
                                     is_full_screen_for_lua,
                                     canvas_create_for_lua,
                                     canvas_alive_for_lua,
                                     canvas_pending_for_lua,
                                     mark_card,
                                     mark_paints_itself,
                                     canvas_release_for_lua,
                                     canvas_place_for_lua,
                                     canvas_clear,
                                     canvas_fill_rect,
                                     canvas_pixel,
                                     canvas_capture,
                                     canvas_bitmap,
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
                                     font_load_bytes,
                                     font_release,
                                     font_info,
                                     canvas_set_font,
                                     app_set_font,
                                     font_text_width,
                                     font_line_height,
                                     label_draw,
                                     button_draw,
                                     checkbox_side,
                                     checkbox_draw,
                                     progress_draw,
                                     slider_draw,
                                     row_draw,
                                     entry_draw,
                                     panel_layout_for_lua,
                                     canvas_resize,
                                     tabs_draw,
                                     tabs_draw_closeable,
                                     tabs_close_at,
                                     tabs_tab_at,
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
                                     image_on_canvas,
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
                                     textbox_caret_box,
                                     textbox_caret,
                                     textbox_multiline_row,
                                     selector_draw,
                                     notification_bar_draw_for_lua,
                                     notification_bar_card_at,
                                     notification_bar_close_at,
                                     icon_grid_draw_for_lua,
                                     icon_grid_hit_for_lua,
                                     icon_grid_cell_for_lua,
                                     icon_grid_page_cell,
                                     icon_grid_ring_for_lua,
                                     invert_for_lua,
                                     canvas_invert_for_lua});
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

// **How many controls this board can hold** (0.102.0): the slots of the table, which the extra memory sized at boot.
// A page asks it when it wants to know how much it can build, and the tests ask it to prove that the number is the
// memory's and not a constant of this file (docs/75, docs/90).
uint16_t canvas_count() { return g_canvas_count; }

void attach() {
  if (root().bits == nullptr) return;
  // **The view of the app that starts is the view of every app** (0.136.0): an app that took the whole glass and ended
  // —by its own will, by an error, or by another app taking the front— does not pass it on to the next one, and the bar
  // of the system comes back with the view (docs/30, docs/46 D1). It is done before the wash below, because the wash is
  // of the view the app is about to draw in.
  give_back_the_glass();
  // **The keyboard is the system's and does not outlive the page that opened it**: a band left on the glass
  // after the app that asked for it would be a keyboard typing into nothing (docs/90). It goes **with the page out of
  // the books**, because the app that leaves is not told that its view grew and the one that comes has not asked for a
  // keyboard yet (0.84.0).
  g_attached = false;
  keyboard_hide();
  g_attached = true;
  // A white view, nothing drawn and nothing pending, and **no controls inside it**: the app that had
  // them is gone (docs/30, docs/90). The glass is not touched until the app presents.
  for (uint16_t i = 1; i < g_canvas_count; ++i) release_slot(static_cast<int16_t>(i));
  // **And the faces it loaded go back too**: the block of an app is the app's, and an app that ends with a
  // font loaded would leave a kilobyte of the extra memory spoken for by nobody (docs/30, docs/75).
  release_faces();
  memset(root().bits, 0xFF, root().bytes);
  root().damage_count = 0;
  // **And the paper of the page is white again** (0.173.2): what an app cleared its view with is the app's, and a hole that a
  // control leaves on the page of the one that comes next is filled with the paper of the system, not with the ink of the
  // app that has just gone (docs/30, docs/50).
  g_paper_level = 15;
  // **And the glass is left clean, with the cleaner the last page deserves** (0.37.0): the app that was
  // here painted something and the next one starts on white paper, and what that wash costs is the
  // system's to choose —`a2` for a page of text, `gl16` for a few grays, `gc16` for a photograph— and not
  // the app's, because no app knows what the one before it left (docs/50, docs/90). The paint is asked for
  // and not waited on: the next app's own paint queues behind it.
  //
  // **And what says whether there is anything to clean is the page and not only its counters** (0.169.2): `g_levels` is
  // what the page's own primitives drew, and the pixels of a control arrive through its canvas, composed into the view
  // without passing through one of them —so a page that painted only in canvases read as white paper and was not washed
  // between two apps, and the next one started on the ghost of it—. `g_page_owes_nothing` is false the moment anything
  // reaches the view, composed or drawn, and when it says there is ink that nobody counted the wash is the sixteen-level
  // one, which cleans without the flash of the full one (found by this suite, 9/11).
  const bool ink_on_the_glass = g_levels != 0 || g_ink_on_the_view;
  if (ink_on_the_glass) {
    // **The budget of quick paints is the passes and not the boxes the page was drawn with** (0.179.5): a page of forty words
    // is one page and not forty updates, and counting the boxes made every page of text owe the full wash (docs/50).
    hal::UpdateMode mode = cleaner_for(g_levels, g_updates);
    // **Ink that nobody counted is at least a wash of sixteen levels**: `cleaner_for` answers `du` for nothing painted, and
    // a `du` over a page that has pixels in it keeps the ghost of them (docs/50).
    if (g_levels == 0) mode = hal::UpdateMode::kGl16;
    damage_add(root(), 0, 0, root().width, root().height, mode);
    paint_root();
  }
  g_levels = 0;
  g_paints = 0;
  g_updates = 0;
  g_ink_on_the_view = false;
  // **The page is the system's white one until the app draws in it** (0.168.0): the wash above is the one pass the owner
  // asked for —the view and the screen erased before it is handed over— and this is what says so, so the app's first
  // `ui.clear(15, …)` does not pay for it a second time (docs/30, docs/50).
  // **A page that was not washed is not white** (0.169.2): at the boot of a run there is nothing in the counters to wash —the
  // picture of the splash is on the glass and it never passed through a primitive of the page— and the app's own
  // `ui.clear(15, …)` is then the pass that paints the page whole, which is what it is for (docs/30, docs/50).
  g_page_owes_nothing = ink_on_the_glass;
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
  // **A page the system has just handed over white is already washed** (0.168.0): `attach()` fills the view with white and
  // asks the glass for it in one pass before an app draws a single control, so an app that starts its page with
  // `ui.clear(15, …)` —which six apps of this tree do, eleven times between them— was paying a second whole-page clean for
  // paper that is already clean, and on e-ink that clean is a flash of the glass. The owner's rule is the other way round:
  // *«en cas de que el sistema demani un redibuixat de la vista a una app, aquesta ho farà a la qualitat que necessiti…
  // el sistema sempre et passarà una vista neta»*. Nothing has been painted since, so there is nothing to erase
  // (docs/30, docs/50).
  if (g_page_owes_nothing && level == 15) {
    log_printf(pal::kLogDebug, "UI", "a clear of a page the system handed white: nothing to wash");
    return true;
  }
  draw_clear(root(), level, chosen, mode != nullptr);
  g_page_owes_nothing = false;
  g_paper_level = level;
  // **The page is white again, so the band is not there any more**: it is a picture drawn into the page, and the next
  // present has to put it back (docs/50, docs/90).
  keyboard_page_cleared();
  return true;
}

bool fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t level, const char* mode) {
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (!mode_or_default(mode, chosen)) return false;
  if (!g_attached) return true;
  draw_fill_rect(root(), x, y, w, h, level, chosen, mode != nullptr);
  the_band_may_be_gone(x, y, w, h);
  return true;
}

// **The picture of the app's own view**, which is what `ui.image` draws on: the same call a control gets,
// with the root as the canvas (docs/30).
bool image(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char* path, const char* mode, bool mask,
           const char*& code, const char*& reason) {
  return canvas_image(0, x, y, w, h, path, mode, mask, code, reason);
}

// **And the same picture in a canvas that is not the page** (0.153.0): what `canvas:image` asks for, and the reason a
// launcher needs it —the icon of an app drawn inside the cell of a grid, which is a canvas of the app's own—. One
// primitive and two doors, so a picture cannot be drawn one way on the page and another in a control (docs/30).
bool image_on_canvas(int16_t slot, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char* path, const char* mode,
                     bool mask, const char*& code, const char*& reason) {
  return canvas_image(slot, x, y, w, h, path, mode, mask, code, reason);
}

bool pixel(uint16_t x, uint16_t y, uint8_t level, const char* mode) {
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (!mode_or_default(mode, chosen)) return false;
  if (!g_attached) return true;
  draw_pixel(root(), x, y, level, chosen, mode != nullptr);
  the_band_may_be_gone(x, y, 1, 1);
  return true;
}

// **A box of the view turned over** (0.178.0): the same primitive a canvas has, on the page, for a part of the page that
// says "this is the one" without a grey (docs/30).
bool invert(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char* mode) {
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (!mode_or_default(mode, chosen)) return false;
  if (!g_attached) return true;
  draw_invert(root(), x, y, w, h, chosen, mode != nullptr);
  the_band_may_be_gone(x, y, w, h);
  return true;
}

uint8_t damage_count() { return root().damage_count; }

const Damage& damage(uint8_t index) {
  static const Damage kNone;
  if (index >= root().damage_count) return kNone;
  return root().damage[index];
}

const Canvas* at(int16_t slot) { return find(slot); }

bool create(uint16_t width, uint16_t height, int16_t parent, bool with_view, int16_t& slot) {
  slot = kNoSlot;
  const int16_t holder = (parent < 0) ? 0 : parent;
  // **A control with no room in its father is not made**: it would hold a frame that no composition would
  // ever reach (docs/90).
  if (!live(holder)) return false;
  // The same rule as the view: a canvas is a whole number of bytes wide, or a byte would hold the last
  // pixel of one line and the first of the next (docs/50).
  width = static_cast<uint16_t>(width & ~1u);
  if (width == 0 || height == 0) return false;
  // **The first free slot of the table, and the table is as long as the memory made it**: a page that fills it is
  // a page with more than two thousand controls on it, and the refusal is the memory's and not a number of this
  // file (docs/75, docs/90).
  int16_t free_slot = kNoSlot;
  for (uint16_t i = 1; i < g_canvas_count; ++i) {
    if (!g_canvases[i].in_use) {
      free_slot = static_cast<int16_t>(i);
      break;
    }
  }
  if (free_slot < 0) return false;
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
  made.generation = ++g_generation;
  // **Into its father, at the end**: the father is set by `attach_to`, which is also the only place that knows the
  // order of the layers, so a canvas made here goes through the same door as one that changes father (docs/90).
  if (!attach_to(free_slot, holder)) {
    g_canvases[free_slot] = Canvas{};
    return false;
  }
  slot = free_slot;
  return true;
}

bool release(int16_t slot) {
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
  for (uint16_t i = 1; i < g_canvas_count; ++i) {
    if (g_canvases[i].in_use && g_canvases[i].pending_release) {
      release_slot(static_cast<int16_t>(i));
    }
  }
}

bool reparent(int16_t slot, int16_t parent) {
  const int16_t holder = (parent < 0) ? 0 : parent;
  const int16_t was = parent_of(slot);
  const Canvas* canvas = find(slot);
  if (canvas == nullptr || !attach_to(slot, holder)) return false;
  // The father it leaves owes exactly the rectangle it was occupying; the new father owes nothing (the
  // child is composed over it) and the child paints itself again because its place is now read in another
  // frame (docs/90).
  if (was >= 0) invalidate_rect(was, canvas->x, canvas->y, canvas->width, canvas->height);
  invalidate(slot);
  return true;
}

bool mark_paints_itself(int16_t slot, bool on) {
  Canvas* canvas = find(slot);
  if (canvas == nullptr) return false;
  canvas->paints_itself = on;
  return true;
}

bool mark_card(int16_t slot) {
  Canvas* canvas = find(slot);
  if (canvas == nullptr) return false;
  canvas->card = true;
  return true;
}

int16_t parent_of(int16_t slot) {
  const Canvas* target = find(slot);
  return (target != nullptr) ? target->parent : kNoSlot;
}

// **Marks this canvas and every canvas that holds it** as needing a redraw: a child that moved left its
// pixels inside its father's buffer, and the father is the one that has to paint them over —which it
// does when its own `on_draw` paints its canvas from scratch— (docs/90).
bool invalidate_rect(int16_t slot, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
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
bool invalidate(int16_t slot) {
  Canvas* canvas = find(slot);
  if (canvas == nullptr) return false;
  // **A control that repaints itself whole has no block to remember** (0.168.2): the app changed the text, the cursor or the
  // size, and the box of the caret of the draw before is a rectangle that may not be where the new one is.
  canvas->caret_box = false;
  // **And a father that paints itself whole owes what its children had there** (0.168.4): the paper of a grid covers the
  // canvases of its cells, and a child with nothing changed of its own is never composed again —so the picture stayed under
  // the paper and the name, which has no canvas at all, was gone from the glass for good—. That is what the owner sees as
  // *«l'efecte de perdre el contingut és quan uso el focus»*: the ring of a cell invalidates the grid, the grid paints its
  // whole box and the cells it holds are not asked for their buffers. Asking them is exactly the rule of the composition,
  // the father asks every child, and it is done here so that no control has to remember it (docs/30, docs/90).
  //
  // **The rectangle of a child goes back to the damage list here and not in `invalidate_rect`** (0.169.2): that call is
  // also how a father is told which hole a child left behind —a move, a canvas going away—, and its rectangle is in the
  // father's pixels and not in the child's, so damaging it there painted the page at the wrong place *and* twice: the case
  // the suite found was two paints of a whole page where one control had moved (docs/30, docs/90). The quality is `gl16`
  // because the paper that covered the child was painted with a wash and a `du` on top of it keeps the ghost.
  //
  // **And only a father that paints into the canvas that holds it can cover them**: a control with a buffer of its own
  // composes its children into it, so its own drawing never takes their pixels away; one without a buffer paints straight
  // into the canvas of the glass, which is the paper of the grid over the cells of the launcher (docs/30, docs/90).
  for (int16_t index = canvas->first_child; index != kNoSlot; index = g_canvases[index].next_sibling) {
    if (!live(index) || g_canvases[index].pending_release) continue;
    Canvas& child = g_canvases[index];
    if (canvas->bits == nullptr && child.bits != nullptr) {
      damage_add(child, 0, 0, child.width, child.height, hal::UpdateMode::kGl16);
    }
    invalidate(index);
  }
  return invalidate_rect(slot, 0, 0, canvas->width, canvas->height);
}

bool begin_draw(int16_t slot) {
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

bool end_draw(int16_t slot) {
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

bool needs_draw(int16_t slot) {
  const Canvas* target = find(slot);
  return (target != nullptr) && target->needs_draw;
}

bool mark_drawn(int16_t slot) {
  Canvas* target = find(slot);
  if (target == nullptr) return false;
  target->needs_draw = false;
  return true;
}

uint16_t generation_of(int16_t slot) {
  const Canvas* target = find(slot);
  return (target != nullptr) ? target->generation : 0;
}

bool place(int16_t slot, uint16_t x, uint16_t y) {
  Canvas* target = find(slot);
  if (target == nullptr) return false;
  if (target->x != x || target->y != y) {
    // **The place the child leaves is owed to the first control that paints**, which may be a grandfather:
    // a box inside a panel holds children but paints nothing, so it is the panel that has to paint over
    // the hole, in the panel's own pixels (docs/90).
    const int16_t painter = painting_holder(target->parent);
    if (painter >= 0) {
      int32_t offset_x = 0;
      int32_t offset_y = 0;
      for (int16_t walk = slot; walk != painter && walk >= 0; walk = g_canvases[walk].parent) {
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

bool canvas_clear(int16_t slot, uint8_t level, const char* mode) {
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
                 width, height, level, chosen, mode != nullptr);
  close_draw(target);
  return true;
}

bool canvas_fill_rect(int16_t slot, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t level,
                      const char* mode) {
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (!mode_or_default(mode, chosen)) return false;
  if (!g_attached) return true;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return true;
  draw_fill_rect(*target.canvas, static_cast<uint16_t>(target.x + x),
                 static_cast<uint16_t>(target.y + y), w, h, level, chosen, mode != nullptr);
  close_draw(target);
  return true;
}

// **A picture, from a file of the volume to the pixels of the canvas** (docs/30): the size the app asks for
// —or the size of the picture— scaled as it is read, cut to the canvas and to what the control owes, and the
// damage left where it landed. The file is read into the app's extra memory and the decoder's block with it,
// and both go back before this returns: a picture is on the page, not in the heap (docs/75).
bool canvas_image(int16_t slot, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char* path, const char* mode,
                  bool mask, const char*& code, const char*& reason) {
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
          // **A mask is one row of the picture and it is the only thing an app pays for one** (0.169.0): the decoder owns it
          // —`kMaxSide / 2 + 1` bytes, 271 for the widest picture of this glass— and hands it to `image_mask_row` a row at a
          // time; the canvas, the file and the scratch are the same whether the app wants transparency or not (docs/75).
          img::RowSink mask_sink(image_mask_row, &place);
          const img::RowSink* wanted_mask = nullptr;
          if (mask) wanted_mask = &mask_sink;
          status = img::decode(data, static_cast<uint32_t>(read), sink,
                               static_cast<uint8_t*>(scratch), scratch_bytes, wanted_mask);
          if (status == img::kOk && place.wrote) {
            const uint16_t damage_x = static_cast<uint16_t>(place.clip_x0);
            const uint16_t damage_y = static_cast<uint16_t>(place.clip_y0);
            const uint16_t damage_w = static_cast<uint16_t>(place.clip_x1 - place.clip_x0);
            const uint16_t damage_h = static_cast<uint16_t>(place.clip_y1 - place.clip_y0);
            damage_add(canvas, damage_x, damage_y, damage_w, damage_h, chosen, mode != nullptr);
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

bool canvas_pixel(int16_t slot, uint16_t x, uint16_t y, uint8_t level, const char* mode) {
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (!mode_or_default(mode, chosen)) return false;
  if (!g_attached) return true;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return true;
  draw_pixel(*target.canvas, static_cast<uint16_t>(target.x + x),
             static_cast<uint16_t>(target.y + y), level, chosen, mode != nullptr);
  close_draw(target);
  return true;
}

uint8_t canvas_damage(int16_t slot) {
  const Canvas* target = find(slot);
  return (target != nullptr) ? target->damage_count : 0;
}

bool canvas_frame(int16_t slot, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t level,
                  const char* mode) {
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (!mode_or_default(mode, chosen)) return false;
  if (!g_attached) return true;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return true;
  draw_frame(*target.canvas, target.x + x, target.y + y, w, h, clamped(level), chosen, mode != nullptr);
  close_draw(target);
  return true;
}

// **A box of a canvas turned over** (0.178.0): what a cell of a launcher asks for when the dial lands on it, so that the
// picture it holds goes into the black block with its own greys and without the box of its paper (docs/30, docs/92).
bool canvas_invert(int16_t slot, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char* mode) {
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (!mode_or_default(mode, chosen)) return false;
  if (!g_attached) return true;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return true;
  draw_invert(*target.canvas, static_cast<uint16_t>(target.x + x), static_cast<uint16_t>(target.y + y), w, h,
              chosen, mode != nullptr);
  close_draw(target);
  return true;
}

bool canvas_line(int16_t slot, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t level,
                 const char* mode) {
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (!mode_or_default(mode, chosen)) return false;
  if (!g_attached) return true;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return true;
  draw_line(*target.canvas, target.x + x0, target.y + y0, target.x + x1, target.y + y1,
            clamped(level), chosen, mode != nullptr);
  close_draw(target);
  return true;
}

bool canvas_circle(int16_t slot, int16_t center_x, int16_t center_y, int16_t radius, uint8_t level,
                   const char* mode) {
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (!mode_or_default(mode, chosen)) return false;
  if (!g_attached || radius < 0) return true;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return true;
  draw_circle(*target.canvas, target.x + center_x, target.y + center_y, radius, clamped(level),
              chosen, mode != nullptr);
  close_draw(target);
  return true;
}

bool canvas_disc(int16_t slot, int16_t center_x, int16_t center_y, int16_t radius, uint8_t level,
                 const char* mode) {
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (!mode_or_default(mode, chosen)) return false;
  if (!g_attached || radius < 0) return true;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return true;
  draw_disc(*target.canvas, target.x + center_x, target.y + center_y, radius, clamped(level),
            chosen, mode != nullptr);
  close_draw(target);
  return true;
}

bool canvas_text(int16_t slot, int16_t x, int16_t y, const char* text, uint8_t scale, uint8_t level,
                 const char* mode) {
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (!mode_or_default(mode, chosen)) return false;
  if (!g_attached || text == nullptr || scale == 0) return true;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return true;
  draw_text(*target.canvas, face_of(target.canvas->face), target.x + x, target.y + y, text, scale,
            clamped(level), chosen, font::Ink(), mode != nullptr);
  close_draw(target);
  return true;
}

// **The same text with the ink a line of a book has** (0.129.0): the bold, the italic and the step off the
// baseline of a `sup`/`sub`, which the faces of this system cannot give and the rectangles of a glyph can
// (docs/46 A4). And **with the face it is asked for** (0.132.0): `-1` is the face the canvas draws with.
bool canvas_text_styled(int16_t slot, int8_t font, int16_t x, int16_t y, const char* text, uint8_t scale,
                        uint8_t level, const char* mode, const font::Ink& ink) {
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (!mode_or_default(mode, chosen)) return false;
  if (!g_attached || text == nullptr || scale == 0) return true;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return true;
  const font::Face& face = (font >= 0) ? face_of(font) : face_of(target.canvas->face);
  draw_text(*target.canvas, face, target.x + x, target.y + y, text, scale, clamped(level), chosen, ink,
            mode != nullptr);
  close_draw(target);
  return true;
}

// **The pixels of a canvas, out** (0.179.0): the other half of `canvas_bitmap`, and what an app needs to keep a picture it
// has already decoded. The block is four bits a pixel with the left one in the high nibble and the stride of the canvas,
// which is what `canvas_bitmap` takes back —so a capture and a blit of the same box are the same pixels— (docs/30,
// docs/75). False when the box is bigger than what the caller brought, and then nothing is written.
bool canvas_capture(int16_t slot, uint8_t* out, size_t capacity, size_t& wrote) {
  wrote = 0;
  if (!live(slot) || out == nullptr) return false;
  const Canvas& canvas = g_canvases[slot];
  if (canvas.bits == nullptr || canvas.width == 0 || canvas.height == 0) return false;
  const size_t bytes = static_cast<size_t>(canvas.stride) * canvas.height;
  if (capacity < bytes) return false;
  memcpy(out, canvas.bits, bytes);
  wrote = bytes;
  return true;
}

// **A block of pixels into a canvas** (0.133.0, docs/46 D4): what a page of a book needs for the pictures of the
// book, and what any app that decodes one already has in its hand.
bool canvas_bitmap(int16_t slot, int16_t x, int16_t y, uint16_t w, uint16_t h, const uint8_t* bits,
                   uint16_t stride, const char* mode, bool dark) {
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (!mode_or_default(mode, chosen)) return false;
  if (!g_attached || bits == nullptr || w == 0 || h == 0) return true;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return true;

  put_bitmap(*target.canvas, target.x + x, target.y + y, w, h, bits, stride, dark);
  mark_box(*target.canvas, target.x + x, target.y + y, w, h, chosen, mode != nullptr);
  close_draw(target);

  return true;
}

// **A page of a book, painted** (0.132.0): the plan of the paginator turned into pixels, in the canvas the app
// gave. A word is a text with its ink and its face, a rule is one row of ink, and a picture is not painted yet —
// the reader does not decode one (docs/46, A5 and D2).
bool canvas_book_page(int16_t slot, const epub::Item* items, const char* words, int16_t count,
                      const PageFonts& fonts, uint8_t level, const char* mode) {
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (!mode_or_default(mode, chosen)) return false;
  if (!g_attached || items == nullptr || words == nullptr) return true;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return true;

  for (int16_t i = 0; i < count; ++i) {
    const epub::Item& item = items[i];
    if (item.kind == epub::Item::kWord) {
      font::Ink ink = font::Ink();
      ink.bold = (item.ink & epub::kInkBold) != 0;
      ink.italic = (item.ink & epub::kInkItalic) != 0;
      if ((item.ink & epub::kInkRiseMask) == epub::kInkRiseUp) ink.rise = -1;
      if ((item.ink & epub::kInkRiseMask) == epub::kInkRiseDown) ink.rise = 1;

      int8_t handle = fonts.body;
      if (item.face == epub::kFaceTitle) handle = fonts.title;
      if (item.face == epub::kFaceSmall) handle = fonts.small;

      const font::Face& face = (handle >= 0) ? face_of(handle) : face_of(target.canvas->face);
      draw_text(*target.canvas, face, target.x + item.x, target.y + item.y, words + item.text, item.scale,
                clamped(level), chosen, ink, mode != nullptr);
    } else if (item.kind == epub::Item::kRule) {
      const uint16_t w = (item.w > 0) ? static_cast<uint16_t>(item.w) : 1;
      draw_fill_rect(*target.canvas, static_cast<uint16_t>(target.x + item.x),
                     static_cast<uint16_t>(target.y + item.y), w, 1, clamped(level), chosen);
    }
    // A picture is a box the page reserved and nothing is decoded for it yet (docs/46, A5).
  }

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

bool label_draw(int16_t slot, const char* text, uint8_t color, uint8_t background, uint8_t align,
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

// **The drawing of a button, and it is the one thing that draws a key of this system** (0.97.0, docs/92): the paper
// project's `Button::draw`, number by number. It is a function of its own and not the body of `button_draw` because the
// options of a menu are buttons too, and a second drawing of a button would be a second look for the same control.
//
//   - **fourteen pixels of corner** —the theme's ten plus four— and never more than a third of the smaller side;
//   - **the face is lifted four pixels off the bottom of its box** (a third of the height as the most) and the strip
//     that is left in sight under it is **the shadow**: that is what makes a button a key and not a rectangle. With the
//     finger on it the face slides down to the bottom and what is left in sight is at the top and of paper —the button
//     has sunk—, which is the half of "a button that is pressed" that a bevel does not say;
//   - **there is no bevel**: the volume is the shadow and nothing else. A bevel of two greys is what stopped the paper
//     project from cleaning a press without a flash, and this system does not have one either (docs/50);
//   - **the selected button is inverted** —ink face, paper word—, and the focus and the finger are one signal: a ring
//     round a white button says "look here", an inverted one says "this is the one that will answer";
//   - **the frame goes on the face and not on the box**, or it would stay where it was while the button goes down: one
//     pixel, and **three on a primary button**, which is the only one that wears a frame with weight;
//   - and the word is centred in what the tag leaves, with sixteen pixels of air on each side.
void paint_button(Canvas& view, int8_t face_slot, const font::Face& face, int32_t x, int32_t y, int32_t width,
                  int32_t height, uint8_t background, uint8_t border, bool pressed, bool focused, bool primary,
                  const char* title, const char* tag, uint8_t color, uint8_t scale, hal::UpdateMode mode) {
  if (width <= 0 || height <= 0 || scale == 0) return;
  int32_t radius = 14;
  const int32_t most = ((width < height) ? width : height) / 3;
  if (radius > most) radius = most;
  if (radius < 1) radius = 1;
  int32_t lift = 4;
  const int32_t lift_cap = height / 3;
  if (lift > lift_cap) lift = lift_cap;
  const int32_t drop = pressed ? lift : 0;
  const int32_t face_y = y + drop;
  const int32_t face_h = height - lift;
  const bool selected = (pressed || focused);
  const uint8_t face_level = selected ? border : background;
  const uint8_t text_level = selected ? background : color;

  // 1. **The box, in the colour of the shadow**: ink under the face while the button is up, and paper while it is down.
  draw_round_fill(view, x, y, static_cast<uint16_t>(width), static_cast<uint16_t>(height), radius,
                  (pressed || lift == 0) ? background : border, mode);
  // 2. The face, which is where all the ink of the button is.
  draw_round_fill(view, x, face_y, static_cast<uint16_t>(width), static_cast<uint16_t>(face_h), radius, face_level,
                  mode);
  // 3. **The frame goes on the face and not on the box**, or it would stay where it was while the button goes down:
  // one pixel, and **three on a primary button**, which is the only one that wears a frame with weight. And it is drawn
  // only when the button is not selected, because on a black face a black frame is nothing: what has to call for
  // attention is the fill (docs/92).
  if (!selected) {
    const int32_t thickness = primary ? 3 : 1;
    for (int32_t i = 0; i < thickness; ++i) {
      if (width - 2 * i < 2 || face_h - 2 * i < 2) break;
      const int32_t r = (radius > i) ? (radius - i) : 1;
      draw_round_bevel(view, x + i, face_y + i, static_cast<uint16_t>(width - 2 * i),
                       static_cast<uint16_t>(face_h - 2 * i), r, border, border, mode);
    }
  }
  // 4. **The tag and the word, inside the face**: a tag is a small word in an outlined box at the left —the same ink as
  // the word and not a fill— and the word is centred in what is left on its right. Both measurements come out of the
  // same `padX` of sixteen, so a tag cannot cut the word the day it grows (docs/92).
  const int32_t line_h = static_cast<int32_t>(face.height) * static_cast<int32_t>(scale);
  int32_t text_left = x + 16;
  int32_t text_width = width - 32;
  if (tag != nullptr && tag[0] != '\0') {
    const uint16_t tag_text_w = font::width(face, tag, scale);
    const int32_t tag_w = static_cast<int32_t>(tag_text_w) + 12;
    const int32_t tag_h = line_h + 4;
    const int32_t tag_x = x + 8;
    const int32_t tag_y = face_y + (face_h - tag_h) / 2;
    draw_round_bevel(view, tag_x, tag_y, static_cast<uint16_t>(tag_w), static_cast<uint16_t>(tag_h), 4, text_level,
                     text_level, mode);
    static char shown_tag[64];
    label_text(face_slot, tag, static_cast<uint16_t>(tag_w - 12), scale, shown_tag, sizeof(shown_tag));
    draw_text(view, face, tag_x + 6, tag_y + 2, shown_tag, scale, text_level, mode);
    text_left = tag_x + tag_w + 8;
    text_width = width - (text_left - x) - 16;
  }
  if (title != nullptr && title[0] != '\0' && text_width > 0 && line_h > 0) {
    // **The word is cut by the control and not by the frame**: the prefix that fits and three dots, which is what a
    // label does, so a button and a label end the same way (docs/30, docs/90).
    static char shown[512];
    const uint16_t shown_w =
        label_text(face_slot, title, static_cast<uint16_t>(text_width), scale, shown, sizeof(shown));
    const int32_t word_x = text_left + (text_width - static_cast<int32_t>(shown_w)) / 2;
    const int32_t word_y = face_y + (face_h - line_h) / 2;
    draw_text(view, face, word_x, word_y, shown, scale, text_level, mode);
  }
}

bool button_draw(int16_t slot, uint8_t background, uint8_t border, bool pressed, bool focused,
                 bool primary, const char* title, const char* tag, uint8_t color, uint8_t scale) {
  if (!live(slot) || background > 15 || border > 15 || color > 15 || scale == 0 || scale > 16) return false;
  const Canvas& button = g_canvases[slot];
  if (button.width == 0 || button.height == 0) return true;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return false;
  paint_button(*target.canvas, button.face, face_of(button.face), target.x, target.y, button.width, button.height,
               background, border, pressed, focused, primary, title, tag, color, scale, hal::UpdateMode::kDu);
  // **The frame is opaque, and it covers everything inside it**: a child that did not redraw would be erased by the
  // button's own fill, so the child is asked to draw again whenever the frame does. **And a child is not inverted**: it
  // is a control the app paints with the colours the app gave it, and this system does not reach inside another
  // control's fields (docs/30, docs/90).
  for (int16_t child = button.first_child; child != kNoSlot; child = g_canvases[child].next_sibling) {
    invalidate(child);
  }
  close_draw(target);
  return true;
}

int16_t checkbox_side(int16_t slot) {
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

bool checkbox_draw(int16_t slot, uint8_t background, uint8_t border, uint8_t mark, bool checked,
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
    // **A square and a cross** (0.102.1): the owner asked for the box to be more schematic —*«un quadrat i una X dins
    // si està activat o res si no ho està»*—, so there are **no round corners and no tick**: four strokes of frame,
    // paper inside, and when the box is checked two diagonals from one inside corner to the opposite one. It is read
    // from a metre away, it is the same at every size, and it costs the ink of the frame and of the two lines (docs/92).
    const int32_t frame = 2;
    // **A checkbox has no pressed state**: what the finger does is change the state and that is already the cross, so
    // the picture of a held box is the picture of a box. The parameter stays for the shape of the call, which the
    // bridge of Lua fills.
    (void)pressed;
    // **And the box with the focus goes inverted** —frame of paper, inside of ink, cross of paper—, which is the same
    // signal a button gives: it is the one thing a person can see on a glass of two and a half tones, and a ring of one
    // pixel round a box of eighteen could not be told from the border of the box beside it (docs/92). **The label is a
    // child of the app and this system does not recolour it here**: when the button that holds it inverts, the label
    // swaps its own two tones (0.101.0).
    const bool selected = focused;
    const uint8_t ring = selected ? background : border;
    const uint8_t body = selected ? border : background;
    const uint8_t cross_level = selected ? background : mark;
    put_rect(*target.canvas, box_x, box_y, side, frame, ring);
    put_rect(*target.canvas, box_x, box_y + side - frame, side, frame, ring);
    put_rect(*target.canvas, box_x, box_y, frame, side, ring);
    put_rect(*target.canvas, box_x + side - frame, box_y, frame, side, ring);
    put_rect(*target.canvas, box_x + frame, box_y + frame, side - 2 * frame, side - 2 * frame, body);
    // **The cross is two lines of two pixels**, from the inside corner to the opposite one and with a pixel of air so
    // it does not touch the frame: a person reads a cross and not a blob, and only paper and ink are used, so any pass
    // of the glass can carry it (docs/50, docs/92).
    if (checked && side >= 10) {
      const int32_t inset = frame + 1;
      const int32_t left = box_x + inset;
      const int32_t top = box_y + inset;
      const int32_t right = box_x + side - 1 - inset;
      const int32_t bottom = box_y + side - 1 - inset;
      for (int32_t i = 0; i < 2; ++i) {
        draw_line(*target.canvas, left, top + i, right, bottom + i, cross_level, mode);
        draw_line(*target.canvas, right, top + i, left, bottom + i, cross_level, mode);
      }
    }
  }
  // **The row is opaque too**: the child that names the setting was drawn after this control and the row
  // covers it, so a checkbox that redraws asks its children to draw again, as the button does (docs/30,
  // docs/90).
  for (int16_t child = control.first_child; child != kNoSlot; child = g_canvases[child].next_sibling) {
    invalidate(child);
  }
  close_draw(target);
  return true;
}

// ---- a progress bar (docs/90) ----
//
// **The track and the fill are the same rounded box and the fill is a prefix of it**, which is what makes the
// end of a bar a rounded end and not a cut, and it is two calls because a bar is the cheapest thing on the
// page: no border of three pixels, no ring, no bevel. A bar is the answer to "how much of this is done" and
// nothing about it says "press me" (docs/90).
bool progress_draw(int16_t slot, uint8_t background, uint8_t border, uint8_t track, uint8_t fill,
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
    // **And the part that is done wears a ring of ink** (the paper project's `ProgressBar::draw`): the fill is the
    // light tone of the theme —level ten, one of the four levels the cheapest wash of this panel reproduces exactly—
    // and it is the ink of the ring that says where the bar has got to, because a light grey on paper has no edge of
    // its own. A black bar would be a hundred thousand pixels of ink to say the same (docs/50, docs/92).
    if (filled > 4 && inner_h > 4 && border != fill) {
      const int32_t r = (inner_radius > 0) ? inner_radius - 1 : 0;
      draw_round_bevel(*target.canvas, inner_x, inner_y, static_cast<uint16_t>(filled),
                       static_cast<uint16_t>(inner_h), r, border, border, mode);
    }
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
bool slider_draw(int16_t slot, uint8_t background, uint8_t border, uint8_t track, uint8_t fill,
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
  // **The slider is the paper project's `Slider::draw()`** (0.97.0, docs/92): the track is a **tube** —paper inside and
  // an outline of one pixel— and the part the value has taken is **filled with the ink of the outline**, so "where am I"
  // and "how much is left" are two things a person reads at once. The knob is a **circle of the height of the control**:
  // paper with an outline, and **filled ink with a dot of paper in the middle while it has the focus**, which is the
  // signal an inverted button gives. The number goes in the right column with its unit.
  //
  // **And there is no frame round the control**, which is what this drawing had: a chip round a slider says "press me",
  // and a slider is not a thing a finger presses — it is a thing the dial moves (docs/30, docs/90).
  (void)pressed;
  // **The control paints every pixel of its rectangle and not only the tube**: the canvas of an app is not cleared
  // between frames, so a knob that has moved would leave the knob of the frame before on the glass. It is the same
  // promise a button makes with its box, and the paper project gets it from the panel under the slider (docs/30, docs/90).
  draw_fill_rect(*target.canvas, static_cast<uint16_t>(target.x), static_cast<uint16_t>(target.y),
                 static_cast<uint16_t>(width), static_cast<uint16_t>(height), background, mode);
  const int32_t line = 1;
  int32_t knob = height - 6;
  if (knob > 40) knob = 40;
  if (knob < 10) knob = 10;
  if (knob > height) knob = height;
  const int32_t radius = knob / 2;
  const int32_t column = (text_w > 0) ? (text_w + 12) : 0;
  const int32_t left = target.x + radius;
  const int32_t right = target.x + width - column - radius;
  int32_t track_h = height / 4;
  if (track_h < 8) track_h = 8;
  if (track_h > knob) track_h = knob;
  const int32_t middle = target.y + height / 2;
  if (right > left) {
    // The tube, empty: paper and the outline of the ink.
    draw_round_fill(*target.canvas, left, middle - track_h / 2, right - left, track_h, track_h / 2, background, mode);
    draw_round_bevel(*target.canvas, left, middle - track_h / 2, right - left, track_h, track_h / 2, border, border,
                     mode);
    int32_t position = left;
    if (maximum > minimum) {
      position = left + static_cast<int32_t>((static_cast<int64_t>(right - left) * (shown_value - minimum)) /
                                             (maximum - minimum));
    }
    if (position - left > line) {
      draw_round_fill(*target.canvas, left, middle - track_h / 2, position - left, track_h, track_h / 2, fill, mode);
    }
    // **The knob**, a circle of the height of the control, on the value.
    const int32_t kx = position - radius;
    const int32_t ky = middle - radius;
    draw_round_fill(*target.canvas, kx, ky, knob, knob, radius, background, mode);
    if (focused) {
      // **With the focus the knob is ink and leaves a dot of paper in the middle**, which is two colours and can be
      // cleaned with the cheapest wash (docs/50, docs/92).
      draw_round_fill(*target.canvas, kx, ky, knob, knob, radius, border, mode);
      const int32_t dot = radius / 2;
      if (dot >= 4) {
        draw_round_fill(*target.canvas, kx + (knob - dot) / 2, ky + (knob - dot) / 2, dot, dot, dot / 2, background,
                        mode);
      }
    } else {
      draw_round_bevel(*target.canvas, kx, ky, knob, knob, radius, border, border, mode);
    }
    // And the number, in its column.
    if (text_w > 0) {
      draw_text(*target.canvas, face, target.x + width - column + 12, target.y + (height - line_h) / 2, shown, scale,
                text, mode);
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

bool row_draw(int16_t slot, const char* title, const char* detail, const char* subtitle, uint8_t text,
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

// **A row of a list that the app builds** (0.104.0): the paper of the row —or its ink when it is the marked one—, the
// line of the separator under it, and the ring of the focus as the inversion every control of this system uses. **The
// words are not here**: they are the control the app adds inside the entry, which is the whole point of an entry that
// is a control and not a line of data (docs/30, docs/90).
bool entry_draw(int16_t slot, uint8_t background, uint8_t border, bool separator, bool marked,
                bool focused) {
  if (!live(slot) || background > 15 || border > 15) return false;
  const Canvas& entry = g_canvases[slot];
  if (entry.width == 0 || entry.height == 0) return true;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return false;
  const hal::UpdateMode mode = hal::UpdateMode::kDu;
  // **Inverted and not greyed**, the same rule the rows of a data list follow: ink and paper are the two levels that
  // can say "this one" at once, and the focus says it with the same signal a button gives (docs/90).
  const bool lit = marked || focused;
  const uint8_t paper = lit ? border : background;
  const uint8_t ink = lit ? background : border;
  draw_fill_rect(*target.canvas, static_cast<uint16_t>(target.x), static_cast<uint16_t>(target.y),
                 entry.width, entry.height, paper, mode);
  if (separator && entry.height > 2) {
    const int32_t pad = (entry.width > 24) ? 8 : 4;
    put_rect(*target.canvas, target.x + pad, target.y + entry.height - 1,
             static_cast<uint16_t>(static_cast<int32_t>(entry.width) - 2 * pad), 1, ink);
    mark_box(*target.canvas, target.x + pad, target.y + entry.height - 1,
             static_cast<uint16_t>(static_cast<int32_t>(entry.width) - 2 * pad), 1, mode);
  }
  // The row is opaque: the child that says what it is was drawn after it and this paint covers it, so an entry
  // that redraws asks its children to draw again, as a button does (docs/30, docs/90).
  for (int16_t child = entry.first_child; child != kNoSlot; child = g_canvases[child].next_sibling) {
    invalidate(child);
  }
  close_draw(target);
  return true;
}

// ---- a panel: the room that arranges what it holds (docs/90) ----

bool canvas_resize(int16_t slot, uint16_t width, uint16_t height) {
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

uint16_t panel_layout(int16_t slot, PanelFlow flow, uint16_t padding, uint16_t gap,
                      const PanelChild* children, uint16_t count) {
  if (!live(slot) || children == nullptr || count == 0) return 0;
  const Canvas& panel = g_canvases[slot];
  const int32_t inner_w = static_cast<int32_t>(panel.width) - 2 * static_cast<int32_t>(padding);
  const int32_t inner_h = static_cast<int32_t>(panel.height) - 2 * static_cast<int32_t>(padding);
  if (inner_w <= 0 || inner_h <= 0) return 0;

  // **Only the children that are still this panel's** are arranged: a control that moved to another panel is
  // not this one's business, and counting it would put a gap where nobody is (docs/90). **The list is as long as the
  // panel is** (0.103.0): sixteen of them live on the stack and a longer one takes a block, so a page of forty rows in
  // one panel is a page laid out and not a page laid out in half (docs/75, docs/90).
  PanelChild on_stack[16];
  PanelChild* mine = on_stack;
  void* block = nullptr;
  if (count > 16) {
    block = pal::extra_memory().allocate(sizeof(PanelChild) * count, true);
    if (block != nullptr) mine = static_cast<PanelChild*>(block);
  }
  const uint16_t room = (block != nullptr || count <= 16) ? count : 16;
  uint16_t used = 0;
  for (uint16_t i = 0; i < room; ++i) {
    const Canvas* child = find(children[i].slot);
    if (child == nullptr || child->parent != slot) continue;
    mine[used] = children[i];
    ++used;
  }
  if (used == 0) {
    if (block != nullptr) pal::extra_memory().release(block);
    return 0;
  }

  uint8_t moved = 0;
  if (flow == PanelFlow::kOverlay) {
    for (uint16_t i = 0; i < used; ++i) {
      if (place(mine[i].slot, padding, padding)) ++moved;
    }
    if (block != nullptr) pal::extra_memory().release(block);
    return moved;
  }

  const bool vertical = (flow == PanelFlow::kVertical);
  const int32_t span = vertical ? inner_h : inner_w;
  const int32_t across = vertical ? inner_w : inner_h;
  // **Three ways to be laid out, and each cell says which one** (0.103.0): a `share` claims that percentage of the
  // inside of the box and comes out first, a `weight` splits what is left after the shares and the cells that keep
  // their own size, and a cell with neither keeps the size it was made with.
  int32_t fixed = 0;
  int32_t shared = 0;
  int32_t total_weight = 0;
  uint16_t weighted = 0;
  for (uint16_t i = 0; i < used; ++i) {
    const Canvas& child = g_canvases[mine[i].slot];
    const int32_t size = vertical ? child.height : child.width;
    if (mine[i].share > 0) {
      int32_t claimed = (span * static_cast<int32_t>(mine[i].share)) / 100;
      if (claimed > span) claimed = span;
      if (claimed < 0) claimed = 0;
      shared += claimed;
    } else if (mine[i].weight == 0) {
      fixed += size;
    } else {
      total_weight += mine[i].weight;
      ++weighted;
    }
  }
  const int32_t gaps = static_cast<int32_t>(gap) * ((used > 1) ? (used - 1) : 0);
  int32_t free_room = span - fixed - gaps - shared;
  if (free_room < 0) free_room = 0;
  int32_t given = 0;
  int32_t cursor = static_cast<int32_t>(padding);
  for (uint16_t i = 0; i < used; ++i) {
    Canvas& child = g_canvases[mine[i].slot];
    int32_t size = vertical ? child.height : child.width;
    if (mine[i].share > 0) {
      // **The percentage is of the inside of the box** and not of what is left: a cell that says 50 is half the box
      // whether its brothers are fixed, weighted or absent.
      size = (span * static_cast<int32_t>(mine[i].share)) / 100;
      if (size < 0) size = 0;
      if (size > span) size = span;
    } else if (mine[i].weight > 0 && total_weight > 0) {
      // **The last one takes the remainder**, or a hundred pixels of rounding leave a strip of page at the
      // bottom of the panel that nobody asked for (docs/90).
      size = (weighted <= 1) ? (free_room - given) : ((free_room * mine[i].weight) / total_weight);
      if (size < 0) size = 0;
      given += size;
      --weighted;
    }
    // Across the flow a child is as big as the panel —a row of a column is as wide as the column— and along
    // it, the size it was given. A control with a buffer keeps its own size and only moves.
    const uint16_t new_w = static_cast<uint16_t>(vertical ? across : size);
    const uint16_t new_h = static_cast<uint16_t>(vertical ? size : across);
    canvas_resize(mine[i].slot, new_w, new_h);
    const uint16_t x = static_cast<uint16_t>(vertical ? padding : cursor);
    const uint16_t y = static_cast<uint16_t>(vertical ? cursor : padding);
    if (place(mine[i].slot, x, y)) ++moved;
    cursor += size + static_cast<int32_t>(gap);
  }
  if (block != nullptr) pal::extra_memory().release(block);
  return moved;
}

// ---- a strip of tabs (docs/90) ----
//
// **Equal columns and a line that is missing under the chosen one**: on two tones there is no bold and no
// colour, so "you are here" is said by taking the line away instead of by adding anything, and a tab that is
// drawn with a line under it is a tab that is not the one (docs/90).
//
// **The cross of a tab that can be closed is one arithmetic** (0.161.0): `tab_cross_side` says how big it is, the
// paint puts it on the right of the tab and `tab_under` answers with the same box, so what a finger presses is what
// the glass painted (docs/30, docs/90).
int32_t tab_cross_side(int32_t height, int32_t column) {
  int32_t side = height / 3;
  if (side > column / 6) side = column / 6;
  if (side < 8) side = 8;
  if (side > height - 8) side = (height > 8) ? height - 8 : 8;
  return side;
}

bool tabs_paint(int16_t slot, const char* const* labels, uint8_t count, uint8_t index, uint8_t lit,
                uint8_t text, uint8_t background, uint8_t scale, bool closeable) {
  if (!live(slot) || text > 15 || background > 15) return false;
  // **A strip that says how many tabs it has and hands no name is not a strip** (0.169.2): `count == 0` is the empty
  // page state that washes itself (0.164.2) and it is a different thing from three tabs with nothing to write in them,
  // which is a control asked for something nobody can see and is refused here (docs/30, docs/90).
  if (count > 0 && labels == nullptr) return false;
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
  // **A strip with no tab is a strip that washes itself** (0.164.2): the empty box is a page state of
  // its own —a terminal whose only tab was closed— and returning before the fill left the words of the
  // tabs that go away on the glass for ever, which is a broken page and not an empty one. The count is
  // checked here and not in the guard because the guard is also what decides whether the paint costs
  // anything at all (docs/30, docs/90).
  if (labels == nullptr || count == 0) {
    close_draw(target);
    return true;
  }
  const int32_t column = width / count;
  // **Two pixels of line**, which is the thickness the paper project gives a tab and the bar of a menu (docs/92).
  int32_t line = 2;
  if (line > height / 4) line = (height / 4 > 0) ? height / 4 : 1;
  const int32_t line_h =
      static_cast<int32_t>((face.height != 0) ? face.height : font::kGlyphHeight) * scale;
  static char shown[128];
  for (uint8_t i = 0; i < count; ++i) {
    const int32_t x0 = target.x + static_cast<int32_t>(i) * column;
    const int32_t x1 = (i + 1 == count) ? (target.x + width) : (x0 + column);
    const bool chosen = (static_cast<uint8_t>(i + 1) == index);
    // **The tab that will answer is drawn inverted**, which is the one signal this system uses for "here is the
    // dial" and the paper project's rule for its strip (`lit = enabled() && (focused() || isPressed())`): the face
    // takes the ink, the word takes the paper, and the lines of the bookmark and of the bottom go the other way
    // round. On a glass of two and a half tones the inversion is the only thing a person reads as "this one"
    // (0.99.0, docs/92).
    const bool the_one = (lit != 0) && (static_cast<uint8_t>(i + 1) == lit);
    const uint8_t line_level = the_one ? background : text;
    // The word keeps the ink unless the face took it: a lit tab is ink with a paper word, and every other tab is
    // paper with an ink one (the paper project: `lit ? panelBg : text`).
    const uint8_t face_level = the_one ? background : text;
    if (the_one) {
      put_rect(*target.canvas, x0, target.y, x1 - x0, height, text);
    }
    // **The tab you are on is a bookmark**: two pixels of line on its left, on its top and on its right, **and not on
    // the bottom**, so the line it sits on is not painted under it and the eye reads that tab as being in front of the
    // page below. On a glass of two and a half tones there is no colour to say which one is in front, and those three
    // lines are the whole signal —it is the paper project's `TabStrip::draw()`, and it is not the underline this
    // control had before (0.97.0, docs/92).
    if (chosen) {
      put_rect(*target.canvas, x0, target.y, line, height, line_level);
      put_rect(*target.canvas, x1 - line, target.y, line, height, line_level);
      put_rect(*target.canvas, x0, target.y, x1 - x0, line, line_level);
    } else {
      put_rect(*target.canvas, x0, target.y + height - line, x1 - x0, line, line_level);
    }
    // The word, in the middle of its column and between the bookmark and the line.
    if (labels[i] != nullptr && labels[i][0] != '\0' && (x1 - x0 - 2 * line) > 8) {
      // **The cross takes its room from the word** (0.161.0): a strip that closes its tabs gives the right of every
      // tab to the cross, so the label is measured against what is left and a long word is cut there instead of being
      // painted under it (docs/30, docs/90).
      const int32_t room = (x1 - x0 - 2 * line) - 8 - (closeable ? tab_cross_side(height, x1 - x0) + 6 : 0);
      if (room > 8) {
        const uint16_t taken = label_text(strip.face, labels[i], static_cast<uint16_t>(room), scale, shown, sizeof(shown));
        const int32_t at_x = x0 + (room - static_cast<int32_t>(taken)) / 2;
        const int32_t at_y = target.y + (height - line_h) / 2;
        draw_text(*target.canvas, face, at_x, at_y, shown, scale, face_level, mode);
      }
    }
    // **The cross of a tab that can be closed** (0.161.0): two diagonals of two pixels on the right of the tab and
    // centred vertically, in the ink of the tab, and drawn where `tabs_close_at` answers —the paint and the finger are
    // the same arithmetic and not two— (docs/30, docs/90).
    if (closeable) {
      const int32_t side = tab_cross_side(height, x1 - x0);
      const int32_t cx = x1 - 6 - side;
      const int32_t cy = target.y + (height - side) / 2;
      for (int32_t step = 0; step < side; ++step) {
        put_rect(*target.canvas, cx + step, cy + step, 2, 2, face_level);
        put_rect(*target.canvas, cx + side - step - 2, cy + step, 2, 2, face_level);
      }
    }
  }
  close_draw(target);
  return true;
}

bool tabs_draw(int16_t slot, const char* const* labels, uint8_t count, uint8_t index, uint8_t lit, uint8_t text,
               uint8_t background, uint8_t scale) {
  return tabs_paint(slot, labels, count, index, lit, text, background, scale, false);
}

bool tabs_draw_closeable(int16_t slot, const char* const* labels, uint8_t count, uint8_t index, uint8_t lit,
                         uint8_t text, uint8_t background, uint8_t scale) {
  return tabs_paint(slot, labels, count, index, lit, text, background, scale, true);
}

// **Which tab of a strip is at a point, and whether the point is on its cross** (0.161.0): one arithmetic for the two
// answers, because a finger has to land on what the glass painted and the paint is `tabs_paint`'s (docs/30).
uint8_t tab_under(int16_t slot, uint8_t count, uint16_t x, uint16_t y, bool on_the_cross) {
  if (!live(slot) || count == 0) return 0;
  const Canvas& strip = g_canvases[slot];
  if (strip.width == 0 || strip.height == 0) return 0;
  const int32_t column = strip.width / count;
  if (column <= 0) return 0;
  const int32_t px = static_cast<int32_t>(x) - static_cast<int32_t>(strip.x);
  const int32_t py = static_cast<int32_t>(y) - static_cast<int32_t>(strip.y);
  if (py < 0 || py >= strip.height) return 0;
  if (px < 0 || px >= strip.width) return 0;
  const uint8_t which = static_cast<uint8_t>(px / column + 1);
  if (which == 0 || which > count) return 0;
  const int32_t x1 = (which == count) ? strip.width : static_cast<int32_t>(which) * column;
  const int32_t side = tab_cross_side(strip.height, x1 - static_cast<int32_t>(which - 1) * column);
  // **The cross a finger presses is at least `kTabCrossHit` on a side** (0.161.0): what is painted is the cross, what
  // is pressed is a box around it, because a finger is not a mouse —and that box is the strip's own and not a number
  // an app guesses— (docs/90).
  const int32_t hit = (side > kTabCrossHit) ? side : kTabCrossHit;
  const int32_t cx = x1 - 6 - side + side / 2;
  const int32_t cy = strip.height / 2;
  const bool inside = (px >= cx - hit / 2) && (px < cx + hit / 2) && (py >= cy - hit / 2) && (py < cy + hit / 2);
  if (on_the_cross && !inside) return 0;
  return which;
}

uint8_t tabs_close_at(int16_t slot, uint8_t count, uint16_t x, uint16_t y) {
  return tab_under(slot, count, x, y, true);
}

uint8_t tabs_tab_at(int16_t slot, uint8_t count, uint16_t x, uint16_t y) {
  return tab_under(slot, count, x, y, false);
}

// ---- a list (docs/90) ----

namespace {

// **The band of the footer is the box the paginator is designed in, and not a third of a row** (0.176.0): the owner,
// *«el paginador ha de ser igual a tot arreu, si no no s'entén que és un control paginador»*. It was a third of the row
// clamped between twenty-four and thirty-seven, so **the same control came out at twenty pixels in a list of short rows,
// at twenty-four under a grid and at twenty-five in a `ui.dots`** —three sizes of one thing, and a size is what tells a
// person that two drawings are the same control—. Now the strip is always the design: a ball of twenty-five, twelve of
// air under it, thirty-seven of band. A tall row gives it nothing extra and a short one gives it what it needs (docs/30,
// docs/92).
uint16_t list_footer(uint16_t row_height) {
  (void)row_height;
  // **The four rows before the box are the rule and its air** (0.176.0): the line that closes the rows is drawn where a
  // separator would be, and the strip's box —thirty-seven, the ball and the twelve of air under it— begins four rows
  // under it, which is exactly where it begins under a grid and inside a `ui.dots` (docs/30, docs/92).
  return static_cast<uint16_t>(kDotDesignH + kFootRule);
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

bool list_draw(int16_t slot, uint8_t count, uint8_t rows, uint8_t page, uint8_t pages, uint8_t marked,
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
    // **The foot sits under the last row and not at the bottom of the box** (0.174.2, the owner: *«a la part d'abaix has
    // posat un pager? perqué ocupa tant espai i es veu tant petit?»*): the band is the box the app gave the list less the
    // rows that are on it, and a list with more room than rows used to leave that air **between** the last row and its
    // pager, which reads as a page that lost its end. The leftover paper is under the foot, where a page ends (docs/90).
    const int32_t on_page = (rows > 0) ? static_cast<int32_t>(rows) : static_cast<int32_t>(count);
    const int32_t used = on_page * static_cast<int32_t>(row_height);
    const int32_t band_y = target.y + ((used + footer <= height) ? used : (height - footer));
    // **The line closes the page when the rows are the list's own** (0.176.0): a list of data paints its rows here and the
    // last of them has no separator —the separator belongs to the row above the next one, and there is none—, so the foot
    // draws it at the top of its band. A list of **entries** (`count` 0: the rows are controls of the app) brings its own
    // separators with every entry, the last of the page included, and a rule here would be a second line one pixel under
    // the first (docs/30, docs/92).
    if (count > 0) {
      put_rect(*target.canvas, target.x, band_y, width, 1, dim);
      mark_box(*target.canvas, target.x, band_y, width, 1, mode);
    }
    // **And the pages are the paginator of the system** (0.174.2): the balls of the paper project threaded on a rod, the
    // page that is read bigger, the marks that close a window too long and the number of the page when the band is too
    // narrow to draw any of it. It used to be four private dots of four pixels —the same idea drawn twice, and the list's
    // copy was the small one— and now `ui.dots` and the foot of a list are one drawing (docs/30, docs/92).
    // **The band of the foot is the design's room for the ball** (0.176.0): the rule and the air of the design go first
    // and the strip keeps its twenty-five, so the paginator of a list and a `ui.dots` come out the same size —a band of
    // twenty-four gave a ball of twenty, and the same control at three sizes is three controls (docs/30, docs/92).
    if (!paint_dots(*target.canvas, target.x, band_y + kFootRule, width, kDotBig, pages, page, text, background, mode)) {
      const font::Face& face = face_of(list.face);
      const int32_t line_h =
          static_cast<int32_t>((face.height != 0) ? face.height : font::kGlyphHeight) * scale;
      char shown[24];
      snprintf(shown, sizeof(shown), "%u / %u", static_cast<unsigned>(page), static_cast<unsigned>(pages));
      const uint16_t taken = font::width(face, shown, scale);
      draw_text(*target.canvas, face, target.x + (width - static_cast<int32_t>(taken)) / 2,
                band_y + (footer - line_h) / 2, shown, scale, text, mode);
    }
  }
  // **The focus is the frame of the list**, and it is drawn last so the rows do not paint over it: a list that
  // has the focus is the one the arrows of the dial turn (docs/90).
  // **And a list of entries has no frame** (0.167.1): its rows are controls the app built, each with its own paper and
  // its own ring, so a border around the whole column is a box the app never asked for and the owner did not want —
  // *«no posis vora a la llista de llibreries d'About»*—. A list that draws its rows itself keeps it, because there the
  // frame is the only thing that says where the list is (docs/30, docs/90).
  if (focused && count > 0 && width > 8 && height > 8) {
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

bool textbox_draw(int16_t slot, const char* text, uint16_t cursor, bool caret, bool mask, uint8_t color,
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
  // "h", then "hhe", then "hhehel", and a box that looked like a ghost of every word typed so far (found on the emulator,
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
  // **The box of the block is kept from the draw that put it down** (0.168.2): the block blinks, and half of the blinks
  // draw the box without it —the off phase—, so forgetting where it was would leave the system painting the whole control
  // on every second blink. It is the same place, and `invalidate` of the whole control is what forgets it (docs/30).
  if (caret) {
    int32_t caret_x = inner_x + before;
    int32_t cell = caret_w;
    if (caret_x + cell > inner_x + inner_w) cell = inner_x + inner_w - caret_x;
    if (cell > 0) {
      // **The box of the block is what a blink of the system repaints** (0.168.2): one letter, and not the box of the
      // whole text, which is what the system used to mark for a caret that turns twice a second (docs/30, docs/90).
      window.canvas->caret_box = true;
      window.canvas->caret_x = caret_x;
      window.canvas->caret_y = top;
      window.canvas->caret_w = cell;
      window.canvas->caret_h = line_h;
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

uint16_t textbox_inside(int16_t slot, uint8_t scale) {
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
    // **A text that ends in a break has a row after it** (0.108.0): the last byte of the text being a `\n` means the
    // cursor can be at the start of a row that has no letters, which is the state of a document right after the enter
    // that ended its last line. Without that row the caret of the end of such a text belonged to no row of the walk and
    // was drawn in the first one, over the words of the document —the text grew and the block stayed at the top, which
    // is what the owner saw with the board in his hand (docs/90, docs/92).
    if (taken == 0) break;
  } while (at[0] != '\0' || (offset > 0 && at[-1] == '\n'));
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

bool textbox_multiline_draw(int16_t slot, const char* text, uint16_t cursor, bool caret, bool wrap, uint8_t color,
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
        // **The block of this row is the box a blink repaints** (0.168.2), in the pixels of the canvas that holds it.
        target.canvas->caret_box = true;
        target.canvas->caret_x = caret_at;
        target.canvas->caret_y = y;
        target.canvas->caret_w = caret_w;
        target.canvas->caret_h = layout.line_h;
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

// **The box of the caret the last draw put down** (0.168.2): the block and the letter under it, in the control's own
// pixels. The system turns the block twice a second, and a blink that dirtied the whole box of the text was repainting a
// page for a letter —measured on the emulator, 8/11: fifteen paints in twelve seconds with the keyboard's target and five
// without it—. A box that drew no block answers no, and then the system marks the whole control, which is what it did
// before this existed (docs/30, docs/90).
bool textbox_caret_box(int16_t slot, uint16_t& x, uint16_t& y, uint16_t& w, uint16_t& h) {
  if (!live(slot)) return false;
  // **The block is recorded where it was drawn**, and a control without a view of its own draws into the nearest canvas that
  // has one: on the emulator, 8/11, a box of text put its caret on canvas 0, which is the page, and asking the control for a
  // box it never had is why the blink went on painting the whole control. The rectangle is read from the canvas that drew
  // it and **brought back to the pixels of the control**, which is what `invalidate_rect` takes (docs/30, docs/90).
  const int16_t holder = painting_holder(slot);
  if (holder < 0) return false;
  const Canvas& canvas = g_canvases[holder];
  if (!canvas.caret_box || canvas.caret_w <= 0 || canvas.caret_h <= 0) return false;
  int32_t offset_x = 0;
  int32_t offset_y = 0;
  for (int16_t walk = slot; walk != holder && walk >= 0; walk = g_canvases[walk].parent) {
    offset_x += g_canvases[walk].x;
    offset_y += g_canvases[walk].y;
  }
  const int32_t left = canvas.caret_x - offset_x;
  const int32_t top = canvas.caret_y - offset_y;
  if (left < 0 || top < 0) return false;
  x = static_cast<uint16_t>(left);
  y = static_cast<uint16_t>(top);
  w = static_cast<uint16_t>(canvas.caret_w);
  h = static_cast<uint16_t>(canvas.caret_h);
  return true;
}

uint16_t textbox_multiline_hit(int16_t slot, const char* text, uint16_t cursor, bool wrap, uint8_t scale,
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

uint8_t textbox_multiline_rows(int16_t slot, uint8_t scale) {
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
uint16_t textbox_multiline_total(int16_t slot, const char* text, bool wrap, uint8_t scale) {
  if (!live(slot)) return 0;
  if (scale == 0) scale = 1;
  if (scale > 16) scale = 16;
  const Canvas& box = g_canvases[slot];
  const MultilineLayout layout = multiline_layout(box, text, 0, wrap, scale, -1);
  return static_cast<uint16_t>((layout.total > 65535) ? 65535 : layout.total);
}

// **The height of one row in pixels**, which is what a finger has to travel to move the window one row: the same
// arithmetic of the paint, or the gesture and the text do not speak of the same distance (0.88.0, docs/90).
uint8_t textbox_multiline_line(int16_t slot, uint8_t scale) {
  if (!live(slot)) return 0;
  if (scale == 0) scale = 1;
  if (scale > 16) scale = 16;
  const font::Face& face = face_of(g_canvases[slot].face);
  const int32_t line_h = static_cast<int32_t>((face.height != 0) ? face.height : font::kGlyphHeight) * scale;
  if (line_h < 1) return 1;
  return static_cast<uint8_t>((line_h > 255) ? 255 : line_h);
}

// **The caret one letter back or forward in a box of one line** (0.116.0): the two keys of a board inside a field, which
// are the same as the two arrows of a slider once the box has been entered. **A letter is a code point and not a byte**,
// so a step never lands between the two bytes of an accent, and the two ends are the ends: the answer is always a
// cursor the box can draw (docs/30, docs/90).
uint16_t textbox_caret(int16_t slot, const char* text, uint16_t cursor, int16_t steps) {
  if (!live(slot) || steps == 0) return cursor;
  const char* at = (text != nullptr) ? text : "";
  const int32_t length = static_cast<int32_t>(strlen(at));
  int32_t caret = (cursor > length) ? length : static_cast<int32_t>(cursor);
  while (steps > 0 && caret < length) {
    ++caret;
    while (caret < length && (static_cast<uint8_t>(at[caret]) & 0xC0u) == 0x80u) ++caret;
    --steps;
  }
  while (steps < 0 && caret > 0) {
    --caret;
    while (caret > 0 && (static_cast<uint8_t>(at[caret]) & 0xC0u) == 0x80u) --caret;
    ++steps;
  }
  return static_cast<uint16_t>(caret);
}

// **The caret one row up or down in a box of several lines** (0.116.0): the rows are the ones the glass shows, which is
// why this is here and not in the app —with `wrap` a row and a line are not the same thing— and the column is kept as
// near as the row allows, half a letter at a time, so a caret at the end of a long row stays at the end of a short one.
// At the first and the last row the caret stays where it is (docs/30, docs/90).
uint16_t textbox_multiline_row(int16_t slot, const char* text, uint16_t cursor, bool wrap, uint8_t scale,
                               int16_t top, int16_t rows) {
  if (!live(slot) || rows == 0) return cursor;
  if (scale == 0) scale = 1;
  if (scale > 16) scale = 16;
  const Canvas& box = g_canvases[slot];
  const font::Face& face = face_of(box.face);
  const char* at = (text != nullptr) ? text : "";
  const MultilineLayout layout = multiline_layout(box, at, cursor, wrap, scale, top);
  int32_t target = layout.cursor_row + rows;
  if (target < 0) target = 0;
  if (target > layout.total - 1) target = layout.total - 1;
  if (target == layout.cursor_row) return cursor;
  // **The walk is the one of the measure and of the paint**: a second rule here would put the caret on a row nobody
  // sees, and the row the key lands on is the row a person is looking at (docs/90).
  int32_t offset = 0;
  for (int32_t row = 0; row <= target; ++row) {
    int32_t taken = 0;
    int32_t caret_x = 0;
    bool here = false;
    const int32_t span = row_span(face, at + offset, layout.room, scale, wrap, -1, taken, caret_x, here);
    if (row == target) {
      int32_t used = 0;
      int32_t width = 0;
      while (used < span) {
        const int32_t step = advance_of(face, static_cast<uint8_t>(at[offset + used]), scale);
        // Half a letter: the caret goes to the letter whose middle is nearest the column it came from.
        if (width + step / 2 > layout.caret_x) break;
        width += step;
        ++used;
      }
      return static_cast<uint16_t>(offset + used);
    }
    offset += taken;
    if (taken == 0) break;
  }
  return cursor;
}

// **Where the window is left when a finger moves it** (0.88.0): the rows the finger has travelled, added to the window
// that is seen now —the cursor's if the app has not chosen one— and turned back into a row of the bar's track. The
// result is what the app has to write in `top`, and what the system writes when the gesture is its own
// (docs/30, docs/90).
uint16_t textbox_multiline_scroll(int16_t slot, const char* text, uint16_t cursor, bool wrap, uint8_t scale,
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
  int32_t first;  // the column the first of the month goes in: zero for the one a week begins with
  uint8_t days;
  uint8_t start;  // the weekday a week begins with, 1 Monday and 7 Sunday
};

MonthCells cells_of(const Canvas& grid, uint16_t year, uint8_t month) {
  MonthCells cells;
  cells.cell_w = static_cast<int32_t>(grid.width) / 7;
  cells.row_h = static_cast<int32_t>(grid.height) / 7;
  // **The first column is the day a week begins with, and a person chooses that day** (0.170.0): the weekday of the
  // first of the month is a fact of the sky —1 Monday to 7 Sunday— and the column it lands in is the setting turned
  // into an offset, so a week that begins on Sunday puts the first of the month in a different column without the
  // calendar itself moving (docs/30, docs/60).
  cells.start = calendar::week_start();
  const uint8_t weekday = calendar::weekday_of(year, month, 1);
  cells.first = (weekday != 0) ? static_cast<int32_t>((weekday + 7 - cells.start) % 7) + 1 : 1;
  cells.days = calendar::days_in_month(year, month);
  return cells;
}

}  // namespace

bool month_draw(int16_t slot, uint16_t year, uint8_t month, uint8_t selected, uint8_t today,
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
  // **The names of the days are the app's**: the kernel draws them and does not know what a Tuesday is called. They
  // arrive in the order of a week that begins on Monday —the seven names, Monday first— and the columns turn with the
  // setting, so the app writes one list and the glass says the week the person asked for (0.170.0, docs/30).
  if (weekdays != nullptr) {
    const uint8_t small = static_cast<uint8_t>((scale > 1) ? scale - 1 : 1);
    for (int32_t column = 0; column < 7; ++column) {
      const char* name = weekdays[(column + cells.start - 1) % 7];
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

uint8_t month_hit(int16_t slot, uint16_t year, uint8_t month, uint16_t x, uint16_t y) {
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

bool sparkline_draw(int16_t slot, const uint16_t* samples, const uint16_t* top, uint8_t count, uint8_t color,
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

bool pie_draw(int16_t slot, const uint16_t* slices, uint8_t count, uint8_t color, uint8_t background) {
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

bool dialog_draw(int16_t slot, const char* title, const char* text, const char* note, const char* accept,
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
  int32_t radius = 12;
  const int32_t most = ((width < height) ? width : height) / 3;
  if (radius > most) radius = most;
  // **A card is drawn like the paper project's `Modal::drawCard()`** (docs/92): paper inside and **three rings of one
  // pixel**, one inside the other, which is what tells a person that this is on top of the page and not part of it. A
  // single ring on a rounded corner leaves a shard of half a pixel that shows on this glass, and a frame of one solid
  // block of ink is heavier than the drawing of the system.
  draw_round_fill(*target.canvas, target.x, target.y, width, height, radius, background, mode);
  for (int32_t i = 0; i < 3; ++i) {
    if (width - 2 * i < 2 || height - 2 * i < 2) break;
    const int32_t r = (radius > i) ? (radius - i) : 1;
    draw_round_bevel(*target.canvas, target.x + i, target.y + i, static_cast<uint16_t>(width - 2 * i),
                     static_cast<uint16_t>(height - 2 * i), r, text_level, text_level, mode);
  }
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
  // **The message wraps** in what is left above the doors, and a note goes under it in the dim colour. **The room is
  // measured inside the card and not on the page** (0.171.0): `y` carries the place of the card in the view and
  // `doors.y` is a row of the card, so a card that is not at the top of the page used to have no room at all and its
  // message was never drawn (found on the emulator with the card of the eraser, 9/11).
  const int32_t room = doors.y - (y - target.y) - (doors.pad / 2);
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
  if (note != nullptr && note[0] != '\0' && (y - target.y) + line_h <= doors.y - 2) {
    const uint16_t taken = label_text(card.face, note, static_cast<uint16_t>(width - 2 * doors.pad),
                                      static_cast<uint8_t>((scale > 1) ? scale - 1 : 1), shown,
                                      sizeof(shown));
    draw_text(*target.canvas, face, target.x + (width - static_cast<int32_t>(taken)) / 2, y, shown,
              static_cast<uint8_t>((scale > 1) ? scale - 1 : 1), dim, mode);
  }
  // **The two doors are buttons**, and they are drawn by the one function of this system that draws a key (0.97.0,
  // docs/92): the box, the face lifted four pixels, the frame on the face and the word in the middle. **The door the
  // dial is on is the pressed one**, so it inverts —ink face and paper word—, and **the door the app emphasised wears
  // the frame of three** of a primary button, which is what the paper project's dialog did with `setPrimary(true)`.
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
    label_text(card.face, word, static_cast<uint16_t>(box_w - 16), scale, words[i], sizeof(words[i]));
    paint_button(*target.canvas, card.face, face, box_x, target.y + doors.y, box_w, doors.height, background,
                 text_level, false, (focused && choice == which), (emphasis == which), words[i], nullptr, text_level,
                 scale, mode);
  }
  close_draw(target);
  return true;
}

uint8_t dialog_hit(int16_t slot, uint16_t x, uint16_t y) {
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

bool menu_draw(int16_t slot, const char* title, const MenuOption* items, uint8_t count, uint8_t index,
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
  const bool has_title = (title != nullptr && title[0] != '\0');
  const MenuGrid grid = menu_grid(card, has_title, menu_visible(items, count), scale);
  const int32_t fitted = static_cast<int32_t>(grid.rows) * static_cast<int32_t>(grid.columns);
  int32_t radius = 12;
  const int32_t most = ((width < height) ? width : height) / 3;
  if (radius > most) radius = most;
  // **The card is a card and it is drawn like the paper project's `Modal::drawCard()`** (docs/92): paper inside and
  // **three rings of one pixel**, one inside the other. One ring on a rounded corner leaves a shard of half a pixel and
  // it shows; three make the corner read as a corner of ink and not as a rounded blob of paper.
  draw_round_fill(*target.canvas, target.x, target.y, width, height, radius, background, mode);
  for (int32_t i = 0; i < grid.frame; ++i) {
    if (width - 2 * i < 2 || height - 2 * i < 2) break;
    const int32_t r = (radius > i) ? (radius - i) : 1;
    draw_round_bevel(*target.canvas, target.x + i, target.y + i, static_cast<uint16_t>(width - 2 * i),
                     static_cast<uint16_t>(height - 2 * i), r, color, color, mode);
  }
  // **The title of a card is one size over its options**, which is what `fontTitle` and `fontBody` were in the original,
  // and it is centred: the card is the page of a menu and the title is its name (docs/92).
  static char shown[192];
  if (has_title) {
    const uint16_t taken = label_text(card.face, title, static_cast<uint16_t>(width - 2 * grid.pad),
                                      static_cast<uint8_t>(scale + 1), shown, sizeof(shown));
    draw_text(*target.canvas, face, target.x + (width - static_cast<int32_t>(taken)) / 2, target.y + grid.pad, shown,
              static_cast<uint8_t>(scale + 1), color, mode);
  }
  for (uint8_t n = 1; n <= count; ++n) {
    const int32_t rank = menu_rank(items, count, n);
    if (rank < 0 || rank >= fitted) continue;
    const MenuBox box = menu_cell(grid, rank);
    const MenuOption& option = items[n - 1];
    // **A row of a menu is a line of words and not a key** (0.177.0): the reference of this system is the contextual
    // menu of the paper project, which is the card with its entries written on it —the one the finger or the dial is on
    // is a **band of ink from edge to edge** with the word turned over— and not a stack of raised keys. Drawing an
    // option with the function of a key gave every row a face lifted four pixels over a shadow and a frame of its own,
    // and that is what the owner saw: *«el desplegable te botons»* (docs/92, and `MenuBar::draw()` of that project).
    const bool lit = (focused && index == n);
    paint_row(*target.canvas, target.x + box.x, target.y + box.y, box.w, box.h, card.face,
              (option.label != nullptr) ? option.label : "", option.tag, nullptr, color, dim, background, scale, lit,
              false, mode);
    // **What is left of the frame of three a key had**: a row the app calls primary wears a hairline, and it is the
    // only mark a menu keeps for it. The row of the dial carries no frame, because its band already says everything
    // (docs/90, docs/92).
    if (option.primary && !lit) {
      draw_round_bevel(*target.canvas, target.x + box.x, target.y + box.y, static_cast<uint16_t>(box.w),
                       static_cast<uint16_t>(box.h), 4, dim, dim, mode);
    }
  }
  close_draw(target);
  return true;
}

uint8_t menu_hit(int16_t slot, const char* title, const MenuOption* items, uint8_t count, uint8_t scale,
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
  // **Seventy-two pixels is the biggest a mark of a grid is drawn, and it was measured with the owner's own grid**
  // (0.166.1): a hundred and twenty and then ninety-six were tried, and in both the last two of his nine apps came out
  // without a picture while their names were there —the canvases of a page of nine at four bits are a page the heap the
  // emulator builds does not have (docs/75), and a canvas that cannot be made is not drawn and is not said—. The grid
  // got its size from somewhere else: two columns instead of three and a name at thirty-three pixels, so a cell is 250
  // wide with a mark of 72 in the middle of it (docs/30, docs/75, docs/90).
  if (icon > 72) icon = 72;
  if (icon < 24) icon = 24;
  cells.icon = icon;
  cells.cell_h = icon + 8 + line_h + 8;
  // **The band of the foot is the paginator's and not a number of the grid** (0.176.0): thirty-seven is the box the
  // design asks for, so the strip under a grid is the same strip a list has and a `ui.dots` has (docs/30, docs/92).
  cells.footer = (height > 160) ? static_cast<int32_t>(kDotDesignH) : 0;
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

bool icon_grid_draw(int16_t slot, const GridItem* items, uint8_t count, uint8_t index, uint8_t marked,
                    uint8_t columns, uint8_t color, uint8_t dim, uint8_t background, uint8_t scale,
                    bool focused, bool cells_of_the_kernel) {
  if (!live(slot) || color > 15 || dim > 15 || background > 15) return false;
  const Canvas& grid = g_canvases[slot];
  const int32_t width = grid.width;
  const int32_t height = grid.height;
  if (width == 0 || height == 0) return true;
  if (scale == 0) scale = 1;
  if (scale > 16) scale = 16;
  // **The cells of a grid of entries are counted in its own tree** (0.105.0): the app wrote no list of names, so the
  // pages the footer says and the cell the dial is on are counted here, and the app does not have to say it twice.
  if (!cells_of_the_kernel) {
    uint8_t kids = 0;
    for (int16_t kid = grid.first_child; kid != kNoSlot && kids < 255; kid = g_canvases[kid].next_sibling) {
      ++kids;
    }
    count = kids;
  }
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
  // **A grid of entries paints no cell**: the cells are controls the app built and they paint themselves; what this
  // control owes is the paper of its page and the footer that says which page it is (0.105.0, docs/90).
  for (int32_t which = cells_of_the_kernel ? cells.first : cells.first + per_page;
       which < cells.first + per_page && which < static_cast<int32_t>(count); ++which) {
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
  // **The foot of a grid is the paginator of the system** (0.175.0, the owner: *«que es 1 / 2 que hi ha sota el grid? usa
  // el paginador Dots … no inventis controls o dibuixs controls falsos»*). It used to be the words `N / M` and two marks
  // drawn to look like arrows —a paginator of its own, the third one in this file— and now it is the same strip the
  // `ui.dots` control and the foot of a list draw: the balls of the paper project threaded on a rod, the page that is read
  // bigger, the lines of marks that close a window too long (docs/30, docs/92). A grid of a thousand cells still says where
  // a person is without a scroll, and it says it with the one drawing of this system that means "page".
  if (cells.footer > 0 && cells.pages > 1) {
    const int32_t band_y = static_cast<int32_t>(height) - cells.footer;
    // **The ball of the band, and a rule of air above it**, the same arithmetic the foot of a list makes (0.174.2): a foot
    // of twenty-eight gives a ball of twenty-four.
    // **The ink of the paginator is the ink of the control's words** (0.176.0, the owner: *«per qué és tant gris el
    // paginador?»*): it was `dim`, the lighter tone of a secondary word, so the strip under a grid was grey while the same
    // strip under a list was black and the `ui.dots` control was black. One drawing, one tone: `color`, and the `dim` of
    // the control goes on being what it is for (docs/30, docs/92).
    if (!paint_dots(*target.canvas, target.x, target.y + band_y, width, kDotBig, static_cast<uint16_t>(cells.pages),
                    static_cast<uint16_t>(cells.page), color, background, mode)) {
      // **A grid too narrow to draw a strip says the page in words**, which is what a list does and the only honest thing
      // left: a page number is a page number and not a control (docs/30, docs/90).
      char page[16];
      snprintf(page, sizeof(page), "%d / %d", static_cast<int>(cells.page), static_cast<int>(cells.pages));
      const int32_t taken = static_cast<int32_t>(font::width(face, page, scale));
      const int32_t y = band_y + (cells.footer - line_h) / 2;
      draw_text(*target.canvas, face, target.x + (width - taken) / 2, target.y + y, page, scale, color, mode);
    }
  }
  close_draw(target);
  return true;
}

// **Where a cell is**, in the frame of the grid, so a cell can be a control of the app (0.105.0). It is the same
// geometry the paint and the finger use —`icon_grid_cells` and `icon_grid_box`— and zero for a cell that is not on the
// page the grid is on, which is how the bridge knows to put it out of sight (docs/30, docs/90).
bool icon_grid_cell(int16_t slot, uint8_t count, uint8_t index, uint8_t columns, uint8_t scale,
                    uint16_t which, uint16_t& x, uint16_t& y, uint16_t& w, uint16_t& h) {
  if (!live(slot) || count == 0 || which == 0) return false;
  if (scale == 0) scale = 1;
  if (scale > 16) scale = 16;
  if (count > kMaxGridItems) count = kMaxGridItems;
  // **A cell that is not in the list has no place either** (0.109.1): the page it would be on is not enough, because a
  // grid of eight cells has no ninth and an app that asked for its box would be given one that nobody draws.
  if (which > count) return false;
  const Canvas& grid = g_canvases[slot];
  const GridCells cells = icon_grid_cells(grid, count, index, columns, scale);
  if (cells.columns <= 0 || cells.rows <= 0) return false;
  const int32_t at = static_cast<int32_t>(which) - 1 - cells.first;
  if (at < 0 || at >= cells.columns * cells.rows) return false;
  const GridBox box = icon_grid_box(cells, at);
  x = static_cast<uint16_t>((box.x > 0) ? box.x : 0);
  y = static_cast<uint16_t>((box.y > 0) ? box.y : 0);
  w = static_cast<uint16_t>((box.w > 0) ? box.w : 0);
  h = static_cast<uint16_t>((box.h > 0) ? box.h : 0);
  return w > 0 && h > 0;
}

// **What a move of the ring costs** (0.177.3): the rectangle of the two cells that wore it and wear it —a paint of a grid
// covers the air between two cells, so the rectangle they span has to be whole cells and this is why the two boxes are
// taken to one— and nothing else. Every move of the dial inside a grid used to ask for the frame of the control, which is
// the page: nine pictures painted again to move a ring from one to the next.
bool icon_grid_ring(int16_t slot, uint8_t count, uint8_t from, uint8_t to, bool cursor_moved, uint8_t columns,
                    uint8_t scale, bool cells_of_the_kernel) {
  if (!live(slot)) return false;
  if (scale == 0) scale = 1;
  if (scale > 16) scale = 16;
  if (count > kMaxGridItems) count = kMaxGridItems;
  if (count == 0) return false;
  const Canvas& grid = g_canvases[slot];
  if (grid.width == 0 || grid.height == 0) return false;
  // **The page follows the cursor**, so a cursor that walked has to stay on its page: a cell that walked onto another page
  // lays every cell out again and moves the footer, and that is the whole control. **A ring that comes or goes does not
  // move the cursor** —the two numbers are the cell it is on, once with the ring and once without— and asks nothing of the
  // pages; the page it is on is the one in `from` (docs/90).
  const GridCells now = icon_grid_cells(grid, count, cursor_moved ? to : from, columns, scale);
  if (cursor_moved) {
    const GridCells was = icon_grid_cells(grid, count, from, columns, scale);
    if (was.page != now.page) return false;
  }
  // **A grid of entries paints no cell of its own** (0.105.0): the paper of the page and the footer are what it owes, and
  // neither moves when the ring does —the cells are controls of the app and each one paints its own ring— (docs/30).
  if (!cells_of_the_kernel) return true;
  const int32_t per_page = now.columns * now.rows;
  if (per_page <= 0) return false;
  int32_t x0 = grid.width;
  int32_t y0 = grid.height;
  int32_t x1 = 0;
  int32_t y1 = 0;
  const uint8_t pair[2] = {from, to};
  for (uint8_t half = 0; half < 2; ++half) {
    if (pair[half] == 0) continue;
    const int32_t at = static_cast<int32_t>(pair[half]) - 1 - now.first;
    if (at < 0 || at >= per_page) continue;
    const GridBox box = icon_grid_box(now, at);
    if (box.x < x0) x0 = box.x;
    if (box.y < y0) y0 = box.y;
    if (box.x + box.w > x1) x1 = box.x + box.w;
    if (box.y + box.h > y1) y1 = box.y + box.h;
  }
  // Neither cell is on this page: nothing on the glass wore a ring or wears one now.
  if (x1 <= x0 || y1 <= y0) return true;
  return invalidate_rect(slot, static_cast<uint16_t>(x0), static_cast<uint16_t>(y0),
                         static_cast<uint16_t>(x1 - x0), static_cast<uint16_t>(y1 - y0));
}

// **The cell that opens the page `delta` pages away** (0.175.0): what a swipe over a grid turns to. The first cell of the
// page is the one the cursor goes to, because the page of a grid follows the cursor (docs/90) and a page turned with
// nothing on it is a page where the dial has nowhere to stand.
uint8_t icon_grid_page_cell(int16_t slot, uint8_t count, uint8_t index, uint8_t columns, uint8_t scale,
                            int8_t delta) {
  if (!live(slot) || count == 0 || delta == 0) return 0;
  if (count > kMaxGridItems) count = kMaxGridItems;
  if (scale == 0) scale = 1;
  if (scale > 16) scale = 16;
  const Canvas& grid = g_canvases[slot];
  const GridCells cells = icon_grid_cells(grid, count, index, columns, scale);
  const int32_t per_page = cells.columns * cells.rows;
  if (per_page <= 0) return 0;
  const int32_t wanted = cells.page - 1 + delta;
  if (wanted < 0 || wanted >= cells.pages) return 0;
  return static_cast<uint8_t>(wanted * per_page + 1);
}

uint8_t icon_grid_hit(int16_t slot, const GridItem* items, uint8_t count, uint8_t index, uint8_t columns,
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

bool notification_bar_draw(int16_t slot, const Notification* items, uint8_t shown, uint16_t total, uint8_t color,
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

uint8_t notification_bar_card_at(int16_t slot, uint8_t shown, uint16_t total, uint8_t scale, uint16_t x, uint16_t y) {
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

bool notification_bar_close_at(int16_t slot, uint8_t shown, uint16_t total, uint8_t scale, uint16_t x, uint16_t y) {
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

bool selector_draw(int16_t slot, const char* label, const char* value, bool choosing, uint8_t color, uint8_t dim,
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

bool menu_bar_draw(int16_t slot, const MenuBarTitle* titles, uint8_t count, uint8_t open, uint8_t index,
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
  // **The unfolded menu is a card of the system and not a box with a hairline** (docs/92): the paper project's
  // `Modal::drawCard()` —paper inside, **twelve pixels of corner and three rings of one pixel**—, because a dropdown of
  // a bar is the same thing as the card of a modal: a white sheet over the app. One pixel of frame on this panel reads
  // as a grey and ragged line, and the owner of the paper project said it in as many words.
  int32_t radius = 12;
  const int32_t most = ((grid.menu_w < menu_h) ? grid.menu_w : menu_h) / 3;
  if (radius > most) radius = most;
  draw_round_fill(*target.canvas, menu_x, menu_y, grid.menu_w, menu_h, radius, background, mode);
  for (int32_t i = 0; i < 3; ++i) {
    if (grid.menu_w - 2 * i < 2 || menu_h - 2 * i < 2) break;
    const int32_t r = (radius > i) ? (radius - i) : 1;
    draw_round_bevel(*target.canvas, menu_x + i, menu_y + i, static_cast<uint16_t>(grid.menu_w - 2 * i),
                     static_cast<uint16_t>(menu_h - 2 * i), r, color, color, mode);
  }
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
    // **The entry the dial is on goes inverted —the whole row in ink and the word in paper—**, which is the same signal
    // a chosen button gives and the only one that says "this is the one that will answer" on a glass of two
    // and a half tones (the paper project's `MenuBar::draw`, docs/92). A ring round a white row says much less, and it
    // is what this control had before.
    if (hot) {
      draw_fill_rect(*target.canvas, static_cast<uint16_t>(menu_x + 3), static_cast<uint16_t>(y),
                     static_cast<uint16_t>(grid.menu_w - 6), static_cast<uint16_t>(grid.row_h), color, mode);
    }
    const uint8_t entry_ink = hot ? background : (entry.enabled ? color : dim);
    const uint8_t box_ink = hot ? background : color;
    const int32_t box_x = menu_x + grid.pad;
    const int32_t box_y = y + (grid.row_h - kMenuBarBox) / 2;
    if (entry.checkable) {
      // **A setting keeps its box when it is off**: the empty square is what tells a switch from a command, and the
      // tick in its middle is the one level of ink that says "this one is on" (docs/90). **And on an inverted row the
      // box and the tick are paper**, or they would be ink on ink.
      draw_frame_thick(*target.canvas, box_x, box_y, kMenuBarBox, kMenuBarBox, 2, box_ink, mode);
      if (entry.checked) put_rect(*target.canvas, box_x + 5, box_y + 5, kMenuBarBox - 10, kMenuBarBox - 10, box_ink);
    }
    // **An entry that cannot be used now is the dim tone and not a third colour**: the ink is all the levels a control
    // may spend on "no", and its row is still there so the menu does not change size when it comes back (docs/90).
    label_text(bar.face, (entry.label != nullptr) ? entry.label : "",
               static_cast<uint16_t>(grid.menu_w - 2 * grid.pad - kMenuBarCheckRoom), scale, shown, sizeof(shown));
    draw_text(*target.canvas, face, box_x + kMenuBarCheckRoom, y + (grid.row_h - line_h) / 2, shown, scale, entry_ink,
              mode);
    y += grid.row_h;
  }
  close_draw(target);
  return true;
}

uint8_t menu_bar_title_at(int16_t slot, const MenuBarTitle* titles, uint8_t count, uint16_t bar_height,
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

uint8_t menu_bar_entry_at(int16_t slot, const MenuBarTitle* titles, uint8_t count, uint8_t open,
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
// the old row nobody painted over— (found on the emulator, 4/11, docs/90).
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

// **The keys keep the margin of the glass** (12 px, the paper project's `edgeMargin`): the panel of this board reads a
// finger imprecisely near a border, and a key that sits on one is a key of which half the presses land on nothing (the
// owner of the paper project, 30/10: *«per norma no has de posar botons aprop de les vores»*). **The strip itself does
// not move** and keeps its whole rectangle, because the strip is what the system asks before handing a touch to the
// keyboard, and a margin that answered "not mine" would be a hidden door that closes the keyboard (docs/92).
const int32_t kKeyMargin = 12;

// **Where the keys live**, which is not the whole strip: the margin on the three sides that are a border of the glass
// —the left, the right and the bottom; the top is the page an app has— and the rows sharing the height of what is
// left. One place, so the paint, the finger and the damage read the same rectangle (docs/90, docs/92).
struct KeyArea {
  int32_t left = 0;
  int32_t top = 0;
  int32_t width = 0;
  int32_t height = 0;
  int32_t row_h = 0;
};

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
  KeyArea keys() const;
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

// **Where a key is, in pixels**: the one function the paint and the finger share, because a key drawn where it cannot
// be pressed is a key that does not exist (docs/90). The width is shared out by the weights of the row (`span`, in
// twentieths) and the two edges of a key come out of the same formula as the edges of its neighbour, so two keys share
// an edge —no gap and no overlap— and the last one reaches the right margin exactly.
void cell_place(const KeyArea& area, const RowDef& row, uint8_t index, int32_t& x, int32_t& w) {
  int32_t before = row.indent;
  for (uint8_t i = 0; i < index; ++i) before += row.keys[i].span;
  const int32_t left = area.left + static_cast<int32_t>((static_cast<int64_t>(before) * area.width) / kRowUnits);
  const int32_t right =
      area.left + static_cast<int32_t>((static_cast<int64_t>(before + row.keys[index].span) * area.width) / kRowUnits);
  x = left;
  w = right - left;
}

// **And where a row is**: the rows share the height of the area and the last one takes what is left over, so the grid
// ends exactly on the bottom margin and not one pixel above it, which is the paper project's `keyRect` (docs/92).
void cell_row(const KeyArea& area, uint8_t rows, uint8_t row, int32_t& y, int32_t& h) {
  y = area.top + row * area.row_h;
  h = ((row + 1) == rows) ? (area.top + area.height - y) : area.row_h;
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
// **Ninety-six pixels of block** (0.107.0): the glyph is rendered at twice its size, so a letter of `scale` five is
// ninety pixels of it (`kOutlineBody` nine, twice). With sixty-four the tallest letters did not fit, `ttf::render`
// refused them and the key fell back to the blocky text of the table —which on a key of sixty-four would be precisely
// the letters the owner cannot read— (docs/50, docs/90).
const uint16_t kAaMaxWidth = 96;
const uint16_t kAaMaxHeight = 96;
// **The tallest a letter of a key goes**: it is what the block above holds, and one step more is a letter drawn
// without its four shades of ink (docs/90).
const uint8_t kKeyTextScale = 5;
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
// the emulator, and then the caller draws them the plain way.
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

// **A stroke of `t` pixels between two points, centred on the line** (0.109.0): the block of each step goes half a
// stroke back and half up, because a block pinned at its corner leaves the stroke half a stroke to the right and half
// down —and a cross drawn that way is a cross that sits off the middle of its key, which is what the owner saw on the
// glass— (docs/90, docs/92).
void ink_line(Canvas& view, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t t, uint8_t level) {
  const int32_t back = t / 2;
  const int32_t dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
  const int32_t dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
  const int32_t steps = (dx > dy) ? dx : dy;
  if (steps <= 0) {
    put_rect(view, x0 - back, y0 - back, t, t, level);
    return;
  }
  for (int32_t i = 0; i <= steps; ++i) {
    const int32_t px = x0 + ((x1 - x0) * i) / steps;
    const int32_t py = y0 + ((y1 - y0) * i) / steps;
    put_rect(view, px - back, py - back, t, t, level);
  }
}

// **The backspace is the shape of a backspace**: the pentagon of the box it deletes —two arms to a point on the left
// and three straight sides— with a cross in the middle, which is the symbol every screen keyboard carries and none of
// the other keys of this band uses (0.107.0, docs/90, docs/92).
//
// **The cross is measured from the body and not from the box** (0.109.0): it was centred on `head + (w - head) / 2`,
// which counts the point of the pentagon as if the cross could go there, and its arms were a fifth of the height, so
// the two strokes reached the right side of the body and read as one blob pushed to the right. Now the body of the
// pentagon is the region between its three straight sides, the cross takes a third of what is inside it, and the
// stroke is centred on its line (docs/90).
void icon_backspace(Canvas& view, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t level) {
  const int32_t t = (h >= 18) ? 3 : 2;
  // The point of the pentagon: a third of the box, and never a corner.
  int32_t head = w / 3;
  if (head < t * 3) head = t * 3;
  const int32_t mid = y + h / 2;
  const int32_t left = x + head;      // where the body of the box starts
  const int32_t right = x + w - t;    // the inside of its right side
  const int32_t top = y + t;          // under its top side
  const int32_t bottom = y + h - t;   // over its bottom side
  // The two arms of the point, and the three sides of the body joined to them.
  ink_line(view, x + t / 2, mid, left, top, t, level);
  ink_line(view, x + t / 2, mid, left, y + h - t / 2, t, level);
  put_rect(view, left - t / 2, y, right - left + t / 2, t, level);
  put_rect(view, left - t / 2, y + h - t, right - left + t / 2, t, level);
  put_rect(view, right, top - t, t, bottom - top + 2 * t, level);
  // **The cross**, in the middle of the body and clear of its sides.
  const int32_t cx = (left + right) / 2;
  const int32_t cy = (top + bottom) / 2;
  const int32_t inner = ((right - left) < (bottom - top)) ? (right - left) : (bottom - top);
  int32_t arm = inner / 3;
  if (arm < t) arm = t;
  ink_line(view, cx - arm, cy - arm, cx + arm, cy + arm, t, level);
  ink_line(view, cx - arm, cy + arm, cx + arm, cy - arm, t, level);
}

// **The enter is the arrow that turns**: down the right side and along the bottom to the left, which is the symbol of
// a key that ends a line and not the word for it (docs/90).
//
// **One drawing and not two segments** (0.109.0): the bar along the bottom stopped `head` pixels short of the stroke
// that comes down —it was given `2 * arm - head` of width and the stroke is at `cx + arm - t`—, so the key showed a
// corner with a gap in it and a small head beside it. The bar now reaches the stroke, the two meet in the corner of the
// right side, and the head is a triangle whose columns carry the stroke of the bar so that the two are one piece
// (docs/90, docs/92).
void icon_enter(Canvas& view, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t level) {
  const int32_t t = (h >= 18) ? 3 : 2;
  // How long the head is along the bar: a quarter of the box, which leaves the stroke that comes down the rest.
  const int32_t head = ((h < w) ? h : w) / 4;
  const int32_t right = x + w - t;
  // The centre line of the bar, with the room its head needs under it: the head grows a pixel on each side per column.
  const int32_t bar_mid = y + h - head - t / 2;
  put_rect(view, right, y + t / 2, t, bar_mid - (y + t / 2) + t / 2, level);
  put_rect(view, x + head, bar_mid - t / 2, (right + t) - (x + head), t, level);
  for (int32_t i = 0; i < head; ++i) {
    const int32_t tall = t + 2 * i;
    put_rect(view, x + i, bar_mid - tall / 2, 1, tall, level);
  }
}

void draw_icon(Canvas& view, KeyAction action, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t level) {
  switch (action) {
    case KeyAction::kBack:
      icon_backspace(view, x, y, w, h, level);
      break;
    case KeyAction::kEnter:
      icon_enter(view, x, y, w, h, level);
      break;
    case KeyAction::kSpace:
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
  const KeyArea area = keys();
  int32_t x = 0;
  int32_t w = 0;
  int32_t y = 0;
  int32_t h = 0;
  cell_place(area, def, static_cast<uint8_t>(key), x, w);
  cell_row(area, current.count, static_cast<uint8_t>(row), y, h);
  note_band(x, y, w, h);
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
// It is not called from the `attach` of a new page —the band went before that— and an emulator with no interpreter answers
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
  // **A key is a finger and not a line of text** (0.107.0): forty-four pixels is a row of a table, and the owner said
  // what it does on the glass —*«el teclat ha de ser més alt i amb tecles i lletres més grans, gairebé no es poden
  // polsar»*—. This panel reads a finger at about six pixels a millimetre, so a key of sixty-four is a little over a
  // centimetre, which is the smallest a thumb presses without looking (docs/90). The band still never takes more than
  // two fifths of the view, so a small glass shrinks it instead of eating the page.
  int32_t key_h = 64;
  const int32_t room = (static_cast<int32_t>(view.height) * 2) / 5;
  if (key_h * rows > room) key_h = room / rows;
  if (key_h < 32) key_h = 32;
  key_h_ = static_cast<uint16_t>(key_h);
}

int32_t Keyboard::band_top() const {
  if (!shown_ || key_h_ == 0) return 0;
  return static_cast<int32_t>(root().height) - current_layout().count * static_cast<int32_t>(key_h_);
}

// **The rectangle of the keys of the band that is on the glass now**, the margin of the glass taken off three sides. A
// strip too narrow to keep a margin keeps none —a keyboard with no keys is worse than a key on the border— and then the
// keys are the whole strip, which is what the band was before (docs/92).
KeyArea Keyboard::keys() const {
  KeyArea area;
  const int32_t top = band_top();
  if (top < 0 || key_h_ == 0) return area;
  const uint8_t rows = current_layout().count;
  if (rows == 0) return area;
  area.left = 0;
  area.top = top;
  area.width = static_cast<int32_t>(root().width);
  area.height = static_cast<int32_t>(rows) * static_cast<int32_t>(key_h_);
  area.row_h = static_cast<int32_t>(key_h_);
  const int32_t inner_w = area.width - 2 * kKeyMargin;
  const int32_t inner_h = area.height - kKeyMargin;
  if (inner_w <= kKeyMargin || inner_h <= 0) return area;
  area.left = kKeyMargin;
  area.width = inner_w;
  area.height = inner_h;
  area.row_h = inner_h / static_cast<int32_t>(rows);
  if (area.row_h <= 0) return KeyArea();
  return area;
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
  // **And the space says its word** and not a bar of pixels: it is the widest key of the band and the one a finger
  // looks for without reading, so the word fits where a shape would have to be guessed (the paper project's
  // `labelOf`, docs/92).
  if (key.action == KeyAction::kSpace) {
    snprintf(buffer, cap, "%s", "space");
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
  // **The margin of the glass is not the keyboard's to press**: a finger on it is a touch of the band that presses
  // nothing, which is what `keyboard_touch` answers, and the page under the band never hears it (docs/90, docs/92).
  const KeyArea area = keys();
  if (area.row_h <= 0 || area.width <= 0) return false;
  if (static_cast<int32_t>(x) < area.left || static_cast<int32_t>(x) >= area.left + area.width) return false;
  if (static_cast<int32_t>(y) < area.top || static_cast<int32_t>(y) >= area.top + area.height) return false;
  const int32_t at_row = (static_cast<int32_t>(y) - area.top) / area.row_h;
  if (at_row < 0 || at_row >= static_cast<int32_t>(current.count)) return false;
  const RowDef& def = current.rows[at_row];
  for (uint8_t i = 0; i < def.count; ++i) {
    int32_t cell_x = 0;
    int32_t cell_w = 0;
    cell_place(area, def, i, cell_x, cell_w);
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
  // through —two keyboards on one band, which is what the emulator showed when the mode was switched— (docs/50,
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
  // **Two pixels of ink over the band**: the page ends here, and the keyboard has to have a beginning that does not
  // depend on what an app painted above it (the paper project: `bounds_.top()` and two rows, docs/92).
  put_rect(view, 0, top, view.width, 2, 0);
  const int32_t body = static_cast<int32_t>((face.height != 0) ? face.height : font::kGlyphHeight);
  const KeyArea area = keys();
  if (area.width <= 0 || area.row_h <= 0) {
    keep.give_back(view);
    return;
  }
  for (uint8_t row = 0; row < current.count; ++row) {
    const RowDef& def = current.rows[row];
    int32_t y = 0;
    int32_t h = 0;
    cell_row(area, current.count, row, y, h);
    for (uint8_t i = 0; i < def.count; ++i) {
      int32_t x = 0;
      int32_t w = 0;
      cell_place(area, def, i, x, w);
      if (w <= 2 || h <= 2) continue;
      const KeyDef& key = def.keys[i];
      // **The keys are a grid and not forty-five little boxes** (the paper project, docs/92): a key that is not lit
      // draws **one pixel of ink on its left edge** —the line that separates it from the key beside it— and nothing else.
      // A frame round every key is ten thousand pixels of edge to paint a band that has to be seen and is not the one
      // that matters, and the grid costs a third of that and reads as well or better. The rule of this house is the
      // form and not the ink.
      //
      // **And the key the dial is on is the whole cell in ink with its letter in paper**: on this glass there is
      // nothing else to say "this is the one", and the same block says the capitals are on (docs/90).
      const bool chosen = (row_ == row && key_ == i);
      const bool lit = chosen || (key.action == KeyAction::kCaps && shift_);
      if (lit) {
        draw_fill_rect(view, static_cast<uint16_t>(x), static_cast<uint16_t>(y), static_cast<uint16_t>(w),
                       static_cast<uint16_t>(h), 0, mode);
      } else {
        put_rect(view, x, y, 1, h, 0);
      }
      const uint8_t ink = lit ? 15 : 0;
      const uint8_t paper = lit ? 0 : 15;
      char label[8];
      const char* shown = label_of(key, label, sizeof(label));
      if (shown[0] != '\0') {
        // **A word of a key is smaller than its letter**: `123`, `MAY` and `space` are three cells and more, and a
        // letter is one, and the body that fits one of them is not the body that fits five (docs/90).
        // **A letter takes the height of its key and a word takes two steps less** (0.107.0): a letter was capped at
        // two steps of a key of forty-four, which is eighteen pixels tall on a band of a hundred and seventy-six —the
        // letters of a keyboard nobody can read from arm's length—. Now the height of the cell decides, and the word
        // of a key (`123`, `MAY`, `space`) is two steps under its letter so that it fits a cell of five or six
        // characters wide (docs/90, docs/92).
        const bool word = shown[1] != '\0';
        const int32_t room_text = w - 8;
        uint8_t scale = static_cast<uint8_t>((h - 8) / body);
        if (word && scale > 2) scale = static_cast<uint8_t>(scale - 2);
        if (scale < 2) scale = 2;
        if (scale > kKeyTextScale) scale = kKeyTextScale;
        int32_t taken = static_cast<int32_t>(font::width(face, shown, scale));
        while (scale > 1 && taken > room_text) {
          --scale;
          taken = static_cast<int32_t>(font::width(face, shown, scale));
        }
        const int32_t line_h = body * scale;
        const int32_t at_x = x + (w - taken) / 2;
        // **The letters sit on the line and the line sits in the key**: a face of outlines is measured from the top
        // of its body, and the ink of a capital starts a fifth of it below that —a fixed share of the line, so the
        // same rule holds at every size— (docs/90).
        int32_t at_y = y + (h - line_h) / 2;
        if (face.outline != nullptr) at_y -= (line_h * 6) / 100;
        if (at_y < area.top) continue;
        draw_key_text(view, face, at_x, at_y, shown, scale, ink, paper, mode);
      } else {
        // **A key with no word is a shape**: a square of two fifths of the height of the key, centred, and never wider
        // than the key less its air —which is what the three drawn keys are: the back, the enter and the space— (the
        // paper project's `iconBox`, docs/92).
        // **Three fifths of the key and not two** (0.109.0): a symbol of twenty-five pixels in a key of sixty-four is a
        // pentagon and a cross that touch each other, and the two strokes of the cross need the room to be two strokes
        // (docs/90).
        int32_t side = (h * 3) / 5;
        const int32_t room_icon = w - 8;
        if (side > room_icon) side = room_icon;
        if (side < 5) side = 5;
        draw_icon(view, key.action, x + (w - side) / 2, y + (h - side) / 2, side, side, ink);
      }
    }
    // **And the line that separates this row from the one under it**, as wide as the keys and not as the band: a line
    // that ran the whole strip would cross the margin the keys do not use, which is what the paper project found on
    // its own keyboard (docs/92).
    if ((row + 1) < current.count) put_rect(view, area.left, y + h, area.width, 1, 0);
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
  // docs/90). The finger of the emulator is the same: a click is a frame and a held button is a run of them.
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
namespace {

// **A disc of ink, row by row**: the kernel has no floating point and a circle of eight pixels is a square with the
// corners taken off (docs/90). The strip needs it twice for every ball —the edge and the paper inside it— and it **marks
// no region**: the strip owes one rectangle and the caller says it (docs/30, docs/90).
void put_disc(Canvas& canvas, int32_t cx, int32_t cy, int32_t radius, uint8_t level) {
  if (radius <= 0) {
    put_rect(canvas, cx, cy, 1, 1, level);
    return;
  }
  for (int32_t row = -radius; row <= radius; ++row) {
    const int32_t wide = radius * radius - row * row;
    int32_t half = 0;
    while ((half + 1) * (half + 1) <= wide) ++half;
    put_rect(canvas, cx - half, cy + row, half * 2 + 1, 1, level);
  }
}

}  // namespace

// **The paginator of the system** (0.174.2): the balls of the paper project threaded on a rod, the page that is read
// bigger and solid, the lines of marks that close a window of pages too long to show, and the air under the balls. It
// was the body of `dots_draw` and it is now a function of a box, so **the foot of a list and the `ui.dots` control are
// one drawing** and not two that can drift (docs/30, docs/92).
bool paint_dots(Canvas& canvas, int32_t x, int32_t y, int32_t width, int32_t ball, uint16_t pages,
                uint16_t current, uint8_t level, uint8_t background, hal::UpdateMode mode) {
  if (pages <= 1 || width <= 0 || ball <= 0) return false;

  // **What it is asked for is the ball of the page that is read, and every other number is a fraction of it** (0.174.2):
  // the design settled the sizes against each other —the ball of the open page of 25, the others of 19, a ring of 2, a rod
  // of 3 and an air of 7— and a strip asked for a smaller ball keeps the shape instead of losing the numbers that make it
  // one paginator. The foot of a list asks for the ball its band holds and the `ui.dots` control for the one its box is
  // designed around (docs/30, docs/92 §27).
  const auto of = [ball](int32_t value) {
    return (value * ball) / kDotBig;
  };
  int32_t small = of(kDotSmall);
  int32_t big = ball;
  int32_t ring = of(kDotRing);
  int32_t rod = of(kDotRod);
  int32_t gap = of(kDotGap);
  if (small < 3) small = 3;
  if (big < small + 2) big = small + 2;
  if (ring < 1) ring = 1;
  if (rod < 1) rod = 1;
  if (gap < 2) gap = 2;
  // **What the strip can say, and a window when there is more** (0.94.1): with the pages that do not fit the strip
  // used to run off both edges of its box, and a counter that leaves the glass is not a counter. It shows as many
  // balls as fit —never fewer than one— and, when there are more pages than that, the window of them that holds the
  // open page, so the strip is always inside its box and the page a person is on is always one of the balls
  // (docs/30, docs/90).
  const int32_t step = big + gap;
  int32_t fits = (width + gap) / step;
  if (fits < 1) fits = 1;
  // **And the pages that do not fit say so with a line that closes** (0.139.0): the strip of the paper project ends
  // the window it cannot fill with **three balls that get smaller**, and those marks are what tells a person "there
  // are more pages than balls" —the owner, 7/11: *«quan hi han masses pàgines, el paginador original fa una línia amb
  // marques de nivell»*—. The foot of the reader of the paper writes them beside a ball of 12 as **8, 6 and 4**
  // (`FOOT_TRAIL`), with **4** of air between two and **8** from the strip, and that is 2/3, 1/2 and 1/3 of the ball,
  // an air of a third and a separation of two thirds: at the ball of 25 of this strip they are **16, 12 and 8**, with
  // **8** of air and **16** of separation, and the drawing closes with the same look (docs/30, docs/46).
  const int32_t trail[3] = {(big * 2) / 3, big / 2, big / 3};
  const int32_t trailAir = big / 3;
  const int32_t trailSpace = (big * 2) / 3;
  const int32_t trailW = trailSpace + trail[0] + trail[1] + trail[2] + trailAir * 2;
  // **The window shrinks so the marks fit at both ends**, which is the widest case: a strip that asked for the marks
  // on one side only would change its width while the page turns, and a strip that moves is worse than a small ball.
  // A box too narrow for the three marks keeps the plain window of 0.94.1 —it says "many" with the page that is read
  // and nothing else— (docs/30, docs/90).
  int32_t shown = fits;
  const int32_t with_trails = (width - 2 * trailW + gap) / step;
  const bool many = (static_cast<int32_t>(pages) > fits) && (with_trails >= 3);
  if (many) shown = with_trails;
  if (shown > static_cast<int32_t>(pages)) shown = static_cast<int32_t>(pages);
  int32_t first = 0;
  if (shown < static_cast<int32_t>(pages)) {
    first = static_cast<int32_t>(current) - 1 - shown / 2;
    if (first < 0) first = 0;
    const int32_t most = static_cast<int32_t>(pages) - shown;
    if (first > most) first = most;
  }
  const int32_t total = shown * step - gap;
  // **The strip is centred in its box**, and the box's own place is the one this drawing was given (0.174.2): a list
  // hands it the band of its foot and the `ui.dots` control hands it the whole control, and neither of them knows how
  // wide what it says will be (docs/30).
  int32_t at = x + (width - total) / 2 + big / 2;
  // **And the strip is at the top of its box** (the owner, 11/11: *«Que els punts (cercles) marca-pàgines estiguin una
  // mica mes amunt estàn enganxades a la part d'abaix»*): what is left over of the height is the air **under** it and
  // not around it, so the strip keeps that air whichever box it is given (docs/90, docs/92).
  const int32_t middle = y + big / 2;
  const int32_t from = at;
  // **The rod goes first and the balls on top of it**: it is drawn before them, so what is seen of it is the air
  // between two balls and nothing else —the edge of every ball is what threads them onto it—, and a rod drawn after
  // would cross the middle of each one (docs/90).
  const int32_t to = at + (shown - 1) * step;
  for (int32_t t = 0; t < rod; ++t) {
    put_rect(canvas, from, middle - rod / 2 + t, to - from + 1, 1, level);
  }
  for (int32_t i = 0; i < shown; ++i) {
    const bool here = (first + i + 1 == static_cast<int32_t>(current));
    const int32_t radius = (here ? big : small) / 2;
    // The ring first, as a disc of ink: the body is the same disc two pixels smaller and what is left of the first one
    // is the ring. The page that is read has no paper inside it, and that is what says which one it is.
    put_disc(canvas, at, middle, radius, level);
    if (!here && radius > ring) put_disc(canvas, at, middle, radius - ring, background);
    at += step;
  }
  // **The marks of "there are more"**, at the ends the window does not reach: they are drawn in the air the window
  // reserved for them, after the strip, and they are the same ball only smaller, so the drawing of the strip is one
  // drawing and not two (docs/30).
  int32_t markFrom = from - big / 2;
  int32_t markTo = to + big / 2;
  if (many && first > 0) {
    int32_t tx = markFrom - trailSpace;
    for (int32_t k = 0; k < 3; ++k) {
      tx -= trail[k];
      put_disc(canvas, tx + trail[k] / 2, middle, trail[k] / 2, level);
      if (k < 2) tx -= trailAir;
      markFrom = tx;
    }
  }
  if (many && first + shown < static_cast<int32_t>(pages)) {
    int32_t tx = markTo + trailSpace;
    for (int32_t k = 0; k < 3; ++k) {
      put_disc(canvas, tx + trail[k] / 2, middle, trail[k] / 2, level);
      tx += trail[k];
      if (k < 2) tx += trailAir;
    }
    markTo = tx;
  }
  // **One region for the strip and not one per ball** (0.94.1): with a ball made of two discs, a rod and up to six
  // marks the regions would pass the eight the canvas holds, and past them the whole page becomes one paint of the
  // quality that cannot look wrong —a wash of the panel for a page turn—. The strip is one control and it owes one
  // rectangle: the box of the balls and their marks, not the air under them (docs/30, docs/90).
  mark_box(canvas, markFrom, middle - big / 2, markTo - markFrom + 1, big, mode);
  return true;
}

// **The strip of a page** (0.112.0): the control fills its box with its paper and asks the system for the paginator,
// which is the same drawing the foot of a list makes (0.174.2, docs/30, docs/90).
bool dots_draw(int16_t slot, uint16_t pages, uint16_t current, uint8_t level, uint8_t background) {
  if (!live(slot) || level > 15 || background > 15) return false;
  const Canvas& strip = g_canvases[slot];
  const int32_t width = strip.width;
  const int32_t height = strip.height;
  if (width == 0 || height == 0) return true;
  const DrawTarget target = open_draw(slot);
  if (target.canvas == nullptr) return false;
  const hal::UpdateMode mode = hal::UpdateMode::kDu;
  // **A strip with one page still owes its paper** (0.176.0): it is a control and its box is covered like the box of a list
  // with no rows —the reader of a book whose chapter is one page long found the balls of the chapter before on the
  // glass, because a control that paints nothing paints nothing over what it had— (docs/30, docs/90).
  draw_fill_rect(*target.canvas, static_cast<uint16_t>(target.x), static_cast<uint16_t>(target.y),
                 static_cast<uint16_t>(width), static_cast<uint16_t>(height), background, mode);
  if (pages <= 1) {
    close_draw(target);
    return true;
  }
  // **A box of the size the design asks for is the design** (0.112.0): thirty-seven pixels, the ball of the page that is
  // read and the air under it. A smaller box shrinks the whole strip by the same amount —every number, so the aspect
  // stays— and a taller one keeps the size and gives the air the room, which is what an app gets by handing it the whole
  // height it has (docs/30, docs/92).
  const int32_t ball = (height >= kDotDesignH) ? kDotBig : (kDotBig * height) / kDotDesignH;
  paint_dots(*target.canvas, target.x, target.y, width, ball, pages, current, level, background, mode);
  close_draw(target);
  return true;
}

// **The quality a present asks for when the app does not name one** (0.179.5). The owner: *«Perqué totes refresquen amb
// tanta qualitat quan no fa falta? usa sempre el mode necessari adequat per els canvis del buffer que es volen pujar a
// l'eink»*. The panel is the one thing in this system that is slow and that blinks, so the quality is not the app's
// taste: it is what the glass has been asked for since the page was last washed whole, which is a number this system
// already counts (`kManyPaints`):
//   - **a page is `gl16`**, which puts it up with its sixteen levels and **does not flash**: the page that came before it was
//     left clean by the system —it washes the glass between two apps— so there is nothing to burn away, and `gc16` here
//     would be four blinks per page turn for paper that ends up the same;
//   - **a page that has taken the quick paints after which the panel keeps a ghost is `gc16`**, the only wash that leaves
//     the paper white (docs/50), and so is the one an app names when it knows it carries a plate;
//   - **`a2` and `du` are never chosen here**: the two-level pass leaves two sixteenths of the page before it, and a `du`
//     does not clean what it writes over, so an app that wants them says so at the present (docs/30, docs/50).
hal::UpdateMode quality_of_the_page() {
  // **Past the quick paints the panel keeps a ghost, and then the page is washed whole** (docs/50): the budget is the one the
  // cleaner between two apps already counts (`kManyPaints`), and what it counts here is **passes of ink and not the boxes a
  // page is drawn with** —a page of text drawn once is one pass, and the same page updated forty times is forty— (0.179.5).
  // Until then the page goes up in `gl16`, which is its sixteen levels and **no flash**: a page with a picture in it is the
  // only one that has a reason to ask for more, and that reason is the app's —the cover of a book says `gc16` because it
  // knows it is a plate— (docs/30, docs/50).
  return (g_updates >= kManyPaints) ? hal::UpdateMode::kGc16 : hal::UpdateMode::kGl16;
}

// **When the damage is the page and not a part of it** (0.179.5): a page built again, a turn, the first paint of an app,
// a ring that moves. Four boxes are four passes of ink for one page —the panel paints one box at a time and each pass
// waits for the ink— so from four on the page goes up whole, and so does a change that covers half of it. **Two boxes are
// two**: a ring that leaves a control and arrives at another is two boxes that do not touch, and one band between them
// would paint the air that nobody changed (0.177.4, docs/30, docs/50).
const uint8_t kManyBoxes = 4;

bool the_damage_is_the_page() {
  if (root().damage_count == 0) return false;
  if (root().damage_count >= kManyBoxes) return true;
  uint16_t x0 = 0xFFFF;
  uint16_t y0 = 0xFFFF;
  uint16_t x1 = 0;
  uint16_t y1 = 0;
  for (uint8_t i = 0; i < root().damage_count; ++i) {
    const Damage& region = root().damage[i];
    if (region.x < x0) x0 = region.x;
    if (region.y < y0) y0 = region.y;
    if (region.x + region.w > x1) x1 = static_cast<uint16_t>(region.x + region.w);
    if (region.y + region.h > y1) y1 = static_cast<uint16_t>(region.y + region.h);
  }
  const uint32_t page = static_cast<uint32_t>(root().width) * static_cast<uint32_t>(root().height);
  const uint32_t covered = static_cast<uint32_t>(x1 - x0) * static_cast<uint32_t>(y1 - y0);
  return page != 0 && covered >= page / 2u;
}

// **What the regions that named their own quality say about the whole paint** (0.179.5): a quality the app wrote —a plate
// with `gc16`, a `du4` of its own— is the one thing this layer does not know better, so the page goes up with the strongest
// of them and not with the one the system would have guessed; when nobody named one, the page gets what the ink of the glass
// deserves (docs/30, docs/50).
hal::UpdateMode quality_of_the_damage() {
  bool any = false;
  hal::UpdateMode named = hal::UpdateMode::kDu;
  for (uint8_t i = 0; i < root().damage_count; ++i) {
    const Damage& region = root().damage[i];
    if (!region.named) continue;
    named = any ? stronger_mode(named, region.mode) : region.mode;
    any = true;
  }
  // **The quality the app named is the app's**, and the page's own is what it gets when it named none: what comes out of
  // here is one paint of the page, and the ink of a page that asked for the wash cannot be the one of a page that did not
  // (docs/30, docs/50). A `du` of the whole page is caught after this, by the rule the panel's own hazard asks for.
  return any ? named : quality_of_the_page();
}

bool present() {
  if (!g_attached || root().bits == nullptr) {
    // **A present with no page behind it** (0.118.6): the app drew into a canvas the system has already given back, and
    // the paint is not asked for at all — which on the glass is the same as a paint nobody accepted (docs/30).
    log_printf(pal::kLogWarn, "UI", "a present arrived with no page attached");
    return false;
  }
  // **A page the system owes whole is painted whole** (0.167.0): giving a view back is a change of page the system made,
  // so the app's next present is the whole view with a mode that cleans and not the damage of the page it built. It is
  // asked here and not in `present_all`, because an app that presents by itself is exactly the one that has to be given
  // the right page (docs/30, docs/50).
  if (g_owe_all) {
    g_owe_all = false;
    return present_all(hal::UpdateMode::kGc16);
  }
  compose_tree(0, -1, 0, 0);
  // **The keyboard goes over the page and not in it**: the tree is composed first and the band last, so a
  // control of the page under it is covered and does not have to know that the keyboard is there (docs/90).
  keyboard_paint();
  if (root().damage_count > 0) {
    const bool whole = the_damage_is_the_page();
    const hal::UpdateMode mode = quality_of_the_damage();
    if (whole) {
      merge_all(root(), mode);
      // **And the page is clean again**: a wash is the whole page and it is what the inks of the page owed, so the budget
      // of quick paints and the levels the wash between two apps reads start again from here (docs/50).
      g_levels = 0;
      g_paints = 0;
      g_updates = 0;
    } else {
      // **The boxes that never named a quality are the system's to paint** (0.179.5): a control asks for `du` because it is
      // the cheap one and it is not making a promise about the panel, so a present with no quality of its own gives it the
      // one the page deserves; **a quality the app named is the app's** and stays —a `du4` of its own is a wish and the
      // system has no business overruling it— (docs/30, docs/50).
      for (uint8_t i = 0; i < root().damage_count; ++i) {
        Damage& region = root().damage[i];
        if (region.named) continue;
        if (region.mode == hal::UpdateMode::kDu || region.mode == hal::UpdateMode::kDu4 ||
            region.mode == hal::UpdateMode::kA2) {
          region.mode = mode;
        }
      }
    }
  }
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
  // **A whole page is a wash**: whatever the page owed is paid by the pass that covered it, so the budget of quick paints
  // and the levels the wash between two apps reads start again from here (0.179.5, docs/50).
  g_levels = 0;
  g_paints = 0;
  g_updates = 0;
  sweep_released();
  return ok;
}

// **The same quality for everything pending, and every box where it is** (0.168.1): a focus that moves dirties **two**
// controls —the one that loses the ring and the one that takes it— and both have to be cleaned, because a ring that goes
// away is ink that goes away. Merging the two boxes would clean the air between them too —measured on the emulator, 8/11:
// two rows of `300x64` became one region of `300x344`, which is 103 200 pixels of ink where 38 400 are owed— so the mode
// is written into each region that is already there and the boxes stay apart. An app that wants a uniform page calls
// `present_all` and gets the merge it asked for (docs/30, docs/50).
bool present_each(hal::UpdateMode mode) {
  if (!g_attached || root().bits == nullptr) return false;
  // **A page the system owes whole is not a present of boxes** (0.179.5): the ring that moves asks for a paint of the damage,
  // and a page that has just replaced a picture —the splash of the boot, an app's own picture— has to go up whole and clean
  // before anything else. The wish of the page comes first, and the boxes of the ring are part of it (docs/30, docs/50).
  if (g_owe_all) return present();
  compose_tree(0, -1, 0, 0);
  Canvas& page = root();
  // **And a page of boxes is one paint** (0.179.5): a ring that moves is two controls and the paper between them is nobody's
  // business, but a page that owes four boxes or half of itself is the page going up, and four passes of the panel for one
  // page is ink and time spent on the air between the boxes (docs/30, docs/50).
  if (the_damage_is_the_page()) {
    merge_all(page, mode);
    g_levels = 0;
    g_paints = 0;
    g_updates = 0;
  }
  for (uint8_t i = 0; i < page.damage_count; ++i) page.damage[i].mode = mode;
  keyboard_paint();
  const bool ok = paint_root();
  sweep_released();
  return ok;
}

bool idle() {
  // **And a page the panel refused is asked for again here** (0.118.2): `idle()` is the question "is the ink over?"
  // that an app in a loop already asks, so it is where the system paints what it owes without anybody inventing another
  // call. The paint is only asked for when the panel says its ink is over, which is what keeps this from blocking on
  // `wait_paint` every turn (docs/30, docs/50).
  if (g_owed_paint && !g_painting && !root().in_draw && display::idle()) paint_root();
  return display::idle();
}

// **Whether the glass is owed a page** (0.118.2): the kernel asks it every turn and only then pays for `idle()`, so a
// turn that owes nothing costs nothing (docs/30, docs/50).
bool owes_paint() { return g_owed_paint; }

void owe_the_page() { g_owe_all = true; }

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
  // **The new view is white paper and the glass still shows the page of the size before** (0.169.2): a page that is 420 wide
  // over a glass that held 540 leaves the old one around it, so the page is not the system's white one any more and the
  // app's `ui.clear(15, …)` —the one its `on_resize` does before drawing— has the old page to erase. **Nothing is marked
  // here**: a view that has just been made owes nothing, and a paint asked for in the middle of the keyboard coming up is a
  // flash of the glass at every key and holds the finger behind it (found by these suites, 9/11) (docs/30, docs/50, docs/90).
  g_page_owes_nothing = false;
  luabind::call_resize(root().width, root().height);
  return true;
}

bool set_rotation(uint16_t degrees) {
  const uint16_t was = display::rotation();
  // The glass decides: its driver keeps the image in the frame of the page and turns what it sends.
  if (!display::set_rotation(degrees)) return false;
  // **A page that has the whole glass turns with it** (0.136.0): the strip of the bar is not there, so the view of the
  // app is the whole page and no rows are given away (docs/30, docs/46 D1).
  const uint16_t top = g_full_screen ? 0 : bar::height();
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

// ---- the whole glass for an app that asks for it (0.136.0, docs/46 D1) ----

bool full_screen(bool on) {
  if (!g_attached || root().bits == nullptr) return false;
  // **Asking for what it has is free**: no view is made, no memory is paid and no repaint is owed, and an app that asks
  // on every turn —or on every page— is not asking the system to do anything (docs/30).
  if (on == g_full_screen) return true;
  if (!make_view(on)) {
    log_printf(pal::kLogWarn, "UI", "no memory for the %s view", on ? "whole glass" : "view under the bar");
    return false;
  }
  // **The glass is washed before the app draws in it**: the strip of the bar went away taking its hour with it, and a
  // page drawn over the rows it used with `du` keeps the ghost of it (docs/50, docs/90).
  clean_the_glass(on);
  // And the app is told the size it has now, which is the hook a turn already gives it: it is the one that knows what
  // belongs on a page of a size and the system does not (docs/30).
  luabind::call_resize(root().width, root().height);
  log_printf(pal::kLogInfo, "UI", "%s glass: the view is %ux%u at %u,%u", on ? "whole" : "under the bar",
             static_cast<unsigned>(root().width), static_cast<unsigned>(root().height),
             static_cast<unsigned>(root().x), static_cast<unsigned>(root().y));
  return true;
}

bool is_full_screen() { return g_full_screen; }

void give_back_the_glass() {
  if (!g_full_screen) return;
  // The app that asked is gone or is going: there is nobody to tell, and the view of every app and the bar of the
  // system are what the next page needs (docs/30, docs/46 D1). A view that cannot be made is said and not hidden.
  if (!make_view(false)) {
    log_printf(pal::kLogWarn, "UI", "no memory for the view under the bar");
  }
}

bool poll_full_screen() {
  if (!g_full_screen) return false;
  // **Three taps on the glass are the system's and not the app's** (0.136.0): the gesture is taken out of the queue
  // before the app reads it, so a page of a book that covers everything cannot take away the way back to the bar.
  // What the app does hear is `on_resize`, which is how it knows its view is another size (docs/30).
  touch::Gesture gesture;
  if (!touch::take_gesture_of("triple", gesture)) return false;
  log_printf(pal::kLogInfo, "UI", "three taps at %u,%u: the glass goes back",
             static_cast<unsigned>(gesture.x), static_cast<unsigned>(gesture.y));
  return full_screen(false);
}

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
// **The face the whole system draws with** (0.96.0): the slot of the bold face the boot loads from the disk, or -1 when
// there is none and the five by seven of the flash is what there is. It is not a face an app can release, and it is what
// `font::system_face()` answers from the moment the boot has it (docs/30, docs/92).
int8_t g_system_face_slot = -1;

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

// **A face out of text, which is where both doors end** (0.150.0, docs/46 D6): the file of an app and the face a book
// carries differ in where the bytes came from and in nothing else. `text` is a block of `length + 1` bytes with its
// terminator, and the face keeps it when it is a face of outlines —its glyphs are contours inside those bytes, so the
// block *is* the face (docs/30)— and gives it back when it is a table of bits.
int8_t face_from_text(char* text, uint32_t length, const char*& reason) {
  reason = nullptr;
  // A free slot first: a face that cannot be kept is not worth reading the text for.
  int16_t slot = -1;
  for (uint8_t i = 0; i < kMaxFaces; ++i) {
    if (!g_faces[i].in_use) {
      slot = static_cast<int8_t>(i);
      break;
    }
  }
  if (slot < 0) {
    pal::extra_memory().release(text);
    reason = "there is no room for another face";
    return -1;
  }

  LoadedFace& made = g_faces[slot];
  if (font::looks_like_ttf(text, length)) {
    const char* outline_why = font::load_ttf(text, length, made.outline, made.face);
    if (outline_why == nullptr) {
      made.file = text;
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
  const char* why = font::load_any(text, length, bits, made.face);
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
  return face_from_text(as_text, static_cast<uint32_t>(read), reason);
}

// **And a face from bytes** (0.150.0, docs/46 D6): a book carries its own face inside itself and the kernel has already
// taken it out of the zip, so what is left is a copy —the block of the book is given to the next file it reads— and the
// same door as the file. **Half a megabyte is the ceiling of a face of a book**: the fonts of a book are between fifty
// and three hundred kilobytes, and a whole family is past this and is not a face a page needs (docs/75).
const uint32_t kFaceFromBytes = 512u * 1024u;

int8_t font_load_bytes(const uint8_t* data, uint32_t length, const char*& reason) {
  reason = nullptr;
  if (data == nullptr || length == 0) {
    reason = "there is no face to read";
    return -1;
  }
  if (length > kFaceFromBytes) {
    reason = "a face of a book is up to half a megabyte";
    return -1;
  }
  void* text = nullptr;
  {
    meter::Scope scope(meter::kExtraAlloc);
    text = pal::extra_memory().allocate(static_cast<size_t>(length) + 1, true);
  }
  if (text == nullptr) {
    reason = "there is no memory for the face";
    return -1;
  }
  char* as_text = static_cast<char*>(text);
  memcpy(as_text, data, length);
  as_text[length] = '\0';
  return face_from_text(as_text, length, reason);
}

bool font_release(int8_t handle) {
  if (handle < 0 || handle >= static_cast<int8_t>(kMaxFaces)) return false;
  // **The face of the system is the system's** (0.96.0): an app may load its own and give it back, and the one every
  // control is drawn with is not a face an app can take away —it would hand the whole glass back to the five by seven
  // of the flash in the middle of a page— (docs/30, docs/90).
  if (handle == g_system_face_slot) return false;
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

namespace {

void load_system_face() {
  const char* reason = nullptr;
  const int16_t handle = font_load(kSystemFace, reason);
  if (handle < 0) {
    // **A glass with no face on its disk is a glass with the table of the flash**, and the log says which of the two it
    // is: a page of five by seven that nobody expected is a font that was not read (docs/30, docs/70).
    log_printf(pal::kLogWarn, "UI", "no face at %s (%s), the five by seven stays", kSystemFace,
               (reason != nullptr) ? reason : "?");
    return;
  }
  g_system_face_slot = handle;
  font::set_system_face(&g_faces[handle].face);
  log_printf(pal::kLogInfo, "UI", "face %s, %u px of body at scale 1", kSystemFace,
             static_cast<unsigned>(g_faces[handle].face.height));
}

}  // namespace

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

bool canvas_set_font(int16_t slot, int8_t handle) {
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

// **The height of a line**, which is the number `draw_text` puts down as damage and the number an app
// needs to centre a glyph in a box (0.121.0): a face of outlines says eleven pixels of line per step
// (`kOutlineBody` is eight of body) and the five by seven of the flash says seven, so the app asks and
// never guesses — the two faces differ and a guessed eleven is a text two pixels low on a board with
// the table (docs/30, docs/92).
uint16_t font_line_height(int8_t handle, uint8_t scale) {
  const font::Face& face = face_of(handle);
  const uint8_t step = (face.height != 0) ? face.height : font::kGlyphHeight;
  return static_cast<uint16_t>(step * scale);
}

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

// **The face behind a handle**, which is the table `font_load` filled (docs/30, docs/46 C13).
const font::Face& font_face(int8_t handle) { return face_of(handle); }

}  // namespace ui
}  // namespace kernel
