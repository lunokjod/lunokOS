// The view of an app: the canvas it draws on, the damage it leaves with the quality of each region, the
// translation to where the view sits on the glass, and what happens when the system changes its size
// (docs/30, docs/50, docs/85).
//
// It is checked against the **bench's own panel** (`emu::MemorySurface`), which speaks the same
// `hal::ISurface` contract as the Paper's controller and remembers each paint it was asked for: a
// canvas that copies itself with the wrong region or the wrong quality is a failed assertion here,
// instead of a page nobody can read on the board.
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <unity.h>

#include "emu/options.h"
#include "emu/surface.h"
#include "hal/hal.h"
#include "kernel/display.h"
#include "kernel/ui.h"
#include "pal/pal.h"

// One name for the policy, so the tests read like the document (docs/30).
namespace ui = kernel::ui;

void setUp(void) {}

void tearDown(void) {}

namespace {

bool g_booted = false;

// A bench **with a glass**: the option is what turns the panel on, and a board without one is the
// default of every other suite (docs/85). Only the two pieces this suite is about are booted —the
// board's surface and the app's canvas— because a full boot drags the disk and the radio in for
// nothing.
void boot_with_a_panel() {
  if (g_booted) return;
  emu::Options opts = emu::options();
  opts.display = true;
  opts.read_delay_us = 0;
  emu::set_options(opts);
  kernel::display::boot(hal::platform());
  // **In the glass' own frame**, which is 960x540: this suite is about the damage and the copy, and the
  // turn has its own proof (test_orientation) and its own check in test_bar.
  kernel::display::set_rotation(0);
  ui::boot(hal::platform(), 0);  // no bar in this suite: the view is the whole glass
  g_booted = true;
}

emu::MemorySurface& glass() { return *emu::memory_surface(); }

// Every test starts from the same place: an app attached, the view at the corner, nothing pending and
// **a white glass**. The glass is wiped through the app's own path —a clear and a present— because the
// bench keeps what the last test painted, and a pixel *outside* the region of a test then answers
// about the test before it (it did: the `nothing beyond the region` assertion failed with 0).
void fresh_view() {
  boot_with_a_panel();
  ui::place(0, 0);
  ui::attach();
  ui::clear(15, nullptr);
  ui::present();
}

}  // namespace

// Before an app is attached there is no view: drawing does nothing and a present is refused, because
// the system does not hand a canvas to nobody (docs/30).
static void test_without_an_app_there_is_no_view() {
  boot_with_a_panel();
  TEST_ASSERT_FALSE(ui::present());
  TEST_ASSERT_EQUAL_UINT8(0, ui::damage_count());
  ui::fill_rect(0, 0, 4, 4, 0, nullptr);
  TEST_ASSERT_EQUAL_UINT8(15, ui::canvas().bits[0] >> 4);
  TEST_ASSERT_EQUAL_UINT8(15, ui::canvas().bits[0] & 0x0F);
}

// The canvas is the size of the glass, and it is white before anybody draws on it.
static void test_the_canvas_is_the_glass_and_starts_white() {
  fresh_view();
  TEST_ASSERT_TRUE(ui::available());
  TEST_ASSERT_EQUAL_UINT16(960, ui::width());
  TEST_ASSERT_EQUAL_UINT16(540, ui::height());
  TEST_ASSERT_EQUAL_UINT8(16, ui::levels());
  // Two pixels per byte, so a line of 960 is 480 bytes (docs/50).
  TEST_ASSERT_EQUAL_UINT16(480, ui::canvas().stride);
  TEST_ASSERT_EQUAL_UINT32(480u * 540u, ui::canvas().bytes);
  TEST_ASSERT_EQUAL_UINT8(15, ui::canvas().bits[0] >> 4);
  TEST_ASSERT_EQUAL_UINT8(15, ui::canvas().bits[0] & 0x0F);
}

// Drawing does not reach the glass: it stays on the canvas, and it is recorded as damage.
static void test_drawing_stays_on_the_canvas_and_leaves_damage() {
  fresh_view();
  const uint32_t blits = glass().blits();
  TEST_ASSERT_TRUE(ui::fill_rect(0, 0, 4, 2, 0, nullptr));
  TEST_ASSERT_EQUAL_UINT32(blits, glass().blits());
  TEST_ASSERT_EQUAL_UINT8(0, ui::canvas().bits[0]);        // two black pixels in the first byte
  TEST_ASSERT_EQUAL_UINT8(15, ui::canvas().bits[3] >> 4);  // and the rest is still white
  TEST_ASSERT_EQUAL_UINT8(1, ui::damage_count());
  TEST_ASSERT_EQUAL_UINT16(0, ui::damage(0).x);
  TEST_ASSERT_EQUAL_UINT16(4, ui::damage(0).w);
  TEST_ASSERT_EQUAL_UINT16(2, ui::damage(0).h);
  TEST_ASSERT_EQUAL(hal::UpdateMode::kDu, ui::damage(0).mode);  // the default quality
}

// A present copies **only the damage**, and asks for it with the quality it was drawn with.
static void test_present_sends_the_damage_with_its_quality() {
  fresh_view();
  const uint32_t before = glass().paint_count();
  TEST_ASSERT_TRUE(ui::fill_rect(8, 4, 4, 2, 0, "gc16"));
  TEST_ASSERT_TRUE(ui::present());
  TEST_ASSERT_EQUAL_UINT32(before + 1, glass().paint_count());
  const emu::MemorySurface::Paint* paint = glass().paint_at(before);
  TEST_ASSERT_NOT_NULL(paint);
  TEST_ASSERT_EQUAL(hal::UpdateMode::kGc16, paint->mode);
  TEST_ASSERT_EQUAL_UINT16(8, paint->x);
  TEST_ASSERT_EQUAL_UINT16(4, paint->y);
  TEST_ASSERT_EQUAL_UINT16(4, paint->w);
  TEST_ASSERT_EQUAL_UINT16(2, paint->h);
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(8, 4));
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(11, 5));
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(12, 5));  // the edge of the rectangle
  TEST_ASSERT_EQUAL_UINT8(0, ui::damage_count());        // and nothing is pending now
}

// Two regions drawn with two qualities are two paints, each with its own: a photo and the line of text
// under it are not the same kind of ink (docs/30).
static void test_two_qualities_are_two_paints() {
  fresh_view();
  const uint32_t before = glass().paint_count();
  TEST_ASSERT_TRUE(ui::fill_rect(0, 0, 40, 20, 0, "du4"));
  TEST_ASSERT_TRUE(ui::fill_rect(0, 200, 40, 20, 3, "gc16"));
  TEST_ASSERT_EQUAL_UINT8(2, ui::damage_count());
  TEST_ASSERT_TRUE(ui::present());
  TEST_ASSERT_EQUAL_UINT32(before + 2, glass().paint_count());
  const emu::MemorySurface::Paint* first = glass().paint_at(before);
  const emu::MemorySurface::Paint* second = glass().paint_at(before + 1);
  TEST_ASSERT_NOT_NULL(first);
  TEST_ASSERT_NOT_NULL(second);
  TEST_ASSERT_EQUAL(hal::UpdateMode::kDu4, first->mode);
  TEST_ASSERT_EQUAL_UINT16(20, first->h);
  TEST_ASSERT_EQUAL(hal::UpdateMode::kGc16, second->mode);
  TEST_ASSERT_EQUAL_UINT16(200, second->y);
}

// Two pieces of the same quality close together are one paint: an update is what an e-ink panel is slow
// at, and two of them for a frame drawn in two halves is a waste (docs/50).
static void test_damage_of_the_same_quality_merges() {
  fresh_view();
  const uint32_t before = glass().paint_count();
  TEST_ASSERT_TRUE(ui::fill_rect(0, 0, 40, 20, 0, "du"));
  TEST_ASSERT_TRUE(ui::fill_rect(40, 0, 40, 20, 0, "du"));
  TEST_ASSERT_EQUAL_UINT8(1, ui::damage_count());
  TEST_ASSERT_EQUAL_UINT16(80, ui::damage(0).w);
  TEST_ASSERT_TRUE(ui::present());
  TEST_ASSERT_EQUAL_UINT32(before + 1, glass().paint_count());
  TEST_ASSERT_EQUAL_UINT16(80, glass().paint_at(before)->w);
}

// A present with one quality paints everything pending with it: what a page that must look uniform
// asks for (docs/30).
static void test_present_with_a_quality_overrides_the_damage() {
  fresh_view();
  const uint32_t before = glass().paint_count();
  TEST_ASSERT_TRUE(ui::fill_rect(0, 0, 40, 20, 0, "du4"));
  TEST_ASSERT_TRUE(ui::fill_rect(0, 200, 40, 20, 3, "du4"));
  TEST_ASSERT_TRUE(ui::present_all(hal::UpdateMode::kGc16));
  TEST_ASSERT_EQUAL_UINT32(before + 1, glass().paint_count());
  const emu::MemorySurface::Paint* paint = glass().paint_at(before);
  TEST_ASSERT_NOT_NULL(paint);
  TEST_ASSERT_EQUAL(hal::UpdateMode::kGc16, paint->mode);
  TEST_ASSERT_EQUAL_UINT16(0, paint->y);
  TEST_ASSERT_EQUAL_UINT16(220, paint->h);  // the union of both regions
}

// **The damage is in the canvas' coordinates and the glass wants panel ones.** A view that sits in a
// corner is the normal case the day there are two of them (docs/30).
static void test_the_damage_is_translated_to_where_the_view_sits() {
  fresh_view();
  TEST_ASSERT_TRUE(ui::place(100, 50));
  TEST_ASSERT_EQUAL_UINT16(100, ui::canvas().x);
  TEST_ASSERT_EQUAL_UINT16(50, ui::canvas().y);
  const uint32_t before = glass().paint_count();
  TEST_ASSERT_TRUE(ui::fill_rect(0, 0, 4, 2, 0, "du"));
  TEST_ASSERT_TRUE(ui::present());
  const emu::MemorySurface::Paint* paint = glass().paint_at(before);
  TEST_ASSERT_NOT_NULL(paint);
  TEST_ASSERT_EQUAL_UINT16(100, paint->x);
  TEST_ASSERT_EQUAL_UINT16(50, paint->y);
  TEST_ASSERT_EQUAL_UINT16(4, paint->w);
  TEST_ASSERT_EQUAL_UINT16(2, paint->h);
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(100, 50));
}

// A view that hangs off the glass paints only the part that is on it.
static void test_the_damage_is_cut_at_the_edge_of_the_glass() {
  fresh_view();
  TEST_ASSERT_TRUE(ui::place(940, 0));
  const uint32_t before = glass().paint_count();
  TEST_ASSERT_TRUE(ui::fill_rect(0, 0, 40, 2, 0, "du"));
  TEST_ASSERT_TRUE(ui::present());
  const emu::MemorySurface::Paint* paint = glass().paint_at(before);
  TEST_ASSERT_NOT_NULL(paint);
  TEST_ASSERT_EQUAL_UINT16(940, paint->x);
  TEST_ASSERT_EQUAL_UINT16(20, paint->w);  // twenty pixels are on the glass, twenty are not
  // And a region that is entirely off the glass is not a paint at all.
  const uint32_t after = glass().paint_count();
  TEST_ASSERT_TRUE(ui::place(1000, 0));
  TEST_ASSERT_TRUE(ui::fill_rect(0, 0, 4, 2, 0, "du"));
  TEST_ASSERT_TRUE(ui::present());
  TEST_ASSERT_EQUAL_UINT32(after, glass().paint_count());
}

// A clear marks the whole view: it is the app saying "this page is mine from corner to corner".
static void test_a_clear_is_the_whole_view() {
  fresh_view();
  const uint32_t before = glass().paint_count();
  TEST_ASSERT_TRUE(ui::clear(0, "init"));
  TEST_ASSERT_TRUE(ui::present());
  const emu::MemorySurface::Paint* paint = glass().paint_at(before);
  TEST_ASSERT_NOT_NULL(paint);
  TEST_ASSERT_EQUAL(hal::UpdateMode::kInit, paint->mode);
  TEST_ASSERT_EQUAL_UINT16(0, paint->x);
  TEST_ASSERT_EQUAL_UINT16(0, paint->y);
  TEST_ASSERT_EQUAL_UINT16(960, paint->w);
  TEST_ASSERT_EQUAL_UINT16(540, paint->h);
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(500, 300));
}

// A pixel is one nibble: the even x is the high one, which is the packing the glass takes.
static void test_a_pixel_is_one_nibble() {
  fresh_view();
  TEST_ASSERT_TRUE(ui::pixel(0, 10, 3, nullptr));
  TEST_ASSERT_EQUAL_UINT8(3, ui::canvas().bits[10 * 480] >> 4);
  TEST_ASSERT_EQUAL_UINT8(15, ui::canvas().bits[10 * 480] & 0x0F);
  TEST_ASSERT_TRUE(ui::pixel(1, 10, 5, nullptr));
  TEST_ASSERT_EQUAL_UINT8(5, ui::canvas().bits[10 * 480] & 0x0F);
}

// Nothing is drawn outside the view, and a rectangle that hangs over the edge is cut: the canvas is the
// only thing an app can reach.
static void test_drawing_outside_the_view_is_dropped() {
  fresh_view();
  TEST_ASSERT_TRUE(ui::fill_rect(958, 538, 40, 40, 0, nullptr));
  TEST_ASSERT_EQUAL_UINT8(1, ui::damage_count());
  TEST_ASSERT_EQUAL_UINT16(2, ui::damage(0).w);
  TEST_ASSERT_EQUAL_UINT16(2, ui::damage(0).h);
  TEST_ASSERT_TRUE(ui::present());
  TEST_ASSERT_TRUE(ui::fill_rect(960, 0, 10, 10, 0, nullptr));  // entirely outside
  TEST_ASSERT_EQUAL_UINT8(0, ui::damage_count());
}

// A quality that is not one of the eight of this glass is refused, not rounded to another one.
static void test_an_unknown_quality_is_refused() {
  fresh_view();
  TEST_ASSERT_FALSE(ui::fill_rect(0, 0, 4, 4, 0, "gc17"));
  TEST_ASSERT_EQUAL_UINT8(0, ui::damage_count());
  TEST_ASSERT_FALSE(ui::clear(15, "fast"));
  TEST_ASSERT_FALSE(ui::pixel(0, 0, 0, "du2"));
}

// Past the room of the list everything pending becomes one region with the quality that cannot look
// wrong, and an app can see it happening (docs/30).
static void test_a_flood_of_regions_becomes_one_paint() {
  fresh_view();
  for (uint16_t i = 0; i < ui::kMaxDamage; ++i) {
    TEST_ASSERT_TRUE(ui::fill_rect(0, static_cast<uint16_t>(i * 60), 10, 10, 0, "du"));
  }
  TEST_ASSERT_EQUAL_UINT8(ui::kMaxDamage, ui::damage_count());
  // One more and the list is full: everything becomes one region.
  TEST_ASSERT_TRUE(ui::fill_rect(0, 500, 10, 10, 0, "du"));
  TEST_ASSERT_EQUAL_UINT8(1, ui::damage_count());
  TEST_ASSERT_EQUAL(hal::UpdateMode::kGc16, ui::damage(0).mode);
  TEST_ASSERT_EQUAL_UINT16(510, ui::damage(0).h);
}

// Another size is another canvas, white, with nothing pending, and the app is told to draw it again —
// the system does not know what belongs on a view it did not draw (docs/30).
static void test_a_resize_makes_a_white_canvas_of_another_size() {
  fresh_view();
  TEST_ASSERT_TRUE(ui::place(20, 30));
  TEST_ASSERT_TRUE(ui::fill_rect(0, 0, 8, 8, 0, nullptr));
  TEST_ASSERT_EQUAL_UINT8(1, ui::damage_count());
  TEST_ASSERT_TRUE(ui::resize(320, 240));
  TEST_ASSERT_EQUAL_UINT16(320, ui::width());
  TEST_ASSERT_EQUAL_UINT16(240, ui::height());
  TEST_ASSERT_EQUAL_UINT16(160, ui::canvas().stride);
  TEST_ASSERT_EQUAL_UINT8(0, ui::damage_count());
  // The place of the view is the system's and it survives a resize.
  TEST_ASSERT_EQUAL_UINT16(20, ui::canvas().x);
  TEST_ASSERT_EQUAL_UINT16(30, ui::canvas().y);
  TEST_ASSERT_EQUAL_UINT8(15, ui::canvas().bits[0] >> 4);
  TEST_ASSERT_TRUE(ui::resize(960, 540));  // back, for the tests that come next
  TEST_ASSERT_TRUE(ui::place(0, 0));
}

// A view of nothing is refused instead of being made into a canvas that cannot be copied.
static void test_a_view_of_nothing_is_refused() {
  fresh_view();
  TEST_ASSERT_FALSE(ui::resize(0, 100));
  TEST_ASSERT_FALSE(ui::resize(100, 0));
  TEST_ASSERT_EQUAL_UINT16(960, ui::width());
}

// A region that starts at an odd pixel: the two pixels of a nibble go to two different bytes of the
// glass, and reading the source with the panel's column instead of the region's put somebody else's
// pixels there (found while reviewing the path, 1/11).
static void test_a_region_at_an_odd_x_lands_where_it_says() {
  fresh_view();
  const uint32_t before = glass().paint_count();
  TEST_ASSERT_TRUE(ui::fill_rect(101, 10, 2, 2, 0, "du"));
  // The damage is kept in whole nibbles: the region starts at 100 and carries one extra pixel.
  TEST_ASSERT_EQUAL_UINT16(100, ui::damage(0).x);
  TEST_ASSERT_EQUAL_UINT16(4, ui::damage(0).w);
  TEST_ASSERT_TRUE(ui::present());
  const emu::MemorySurface::Paint* paint = glass().paint_at(before);
  TEST_ASSERT_NOT_NULL(paint);
  TEST_ASSERT_EQUAL_UINT16(100, paint->x);
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(100, 10));  // the neighbour, from the canvas
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(101, 10));
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(102, 11));
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(103, 10));
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(104, 10));  // and nothing beyond the region
}

// **A canvas of the app's own lands where it is placed, and only there.** It is a buffer the app fills
// and the system composes into the view at the place it was given: the ink of a control costs nothing
// until it moves (docs/90).
static void test_a_child_canvas_lands_where_it_is_placed() {
  fresh_view();
  int8_t slot = -1;
  TEST_ASSERT_TRUE(ui::create(20, 10, -1, true, slot));
  TEST_ASSERT_TRUE(ui::canvas_fill_rect(slot, 0, 0, 20, 10, 0, "du"));
  TEST_ASSERT_TRUE(ui::place(slot, 100, 50));
  TEST_ASSERT_TRUE(ui::present());
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(100, 50));
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(119, 59));
  // Outside it, the page is the page's.
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(99, 50));
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(120, 59));
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(100, 60));
}

// A canvas has a border and nothing gets out of it: a rectangle bigger than the canvas is cut at the
// border, and one that starts outside paints nothing at all (docs/90).
static void test_a_canvas_is_cut_at_its_border() {
  fresh_view();
  int8_t slot = -1;
  TEST_ASSERT_TRUE(ui::create(20, 10, -1, true, slot));
  TEST_ASSERT_TRUE(ui::canvas_fill_rect(slot, 0, 0, 200, 200, 0, "du"));
  TEST_ASSERT_TRUE(ui::canvas_fill_rect(slot, 40, 40, 10, 10, 0, "du"));  // outside: nothing
  TEST_ASSERT_TRUE(ui::place(slot, 0, 0));
  TEST_ASSERT_TRUE(ui::present());
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(19, 9));
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(20, 0));
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(0, 10));
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(45, 45));
}

// **A canvas that paints nothing is composed as nothing**: a present with nothing painted anywhere does
// not touch the glass, and a painted control is one paint of its own rectangle (docs/30, docs/90).
static void test_a_canvas_that_paints_nothing_composes_nothing() {
  fresh_view();
  const uint32_t before = glass().paint_count();
  int8_t slot = -1;
  TEST_ASSERT_TRUE(ui::create(20, 10, -1, true, slot));
  TEST_ASSERT_TRUE(ui::place(slot, 30, 30));
  TEST_ASSERT_TRUE(ui::present());
  TEST_ASSERT_EQUAL_UINT32(before, glass().paint_count());

  TEST_ASSERT_TRUE(ui::canvas_fill_rect(slot, 0, 0, 20, 10, 0, "du"));
  TEST_ASSERT_TRUE(ui::present());
  TEST_ASSERT_EQUAL_UINT32(before + 1, glass().paint_count());
  const emu::MemorySurface::Paint* paint = glass().paint_at(before);
  TEST_ASSERT_NOT_NULL(paint);
  TEST_ASSERT_EQUAL_UINT16(30, paint->x);
  TEST_ASSERT_EQUAL_UINT16(30, paint->y);
  TEST_ASSERT_EQUAL_UINT16(20, paint->w);
  TEST_ASSERT_EQUAL_UINT16(10, paint->h);
}

// The table of canvases has a ceiling and a canvas given back is a canvas to take. **The view is the
// system's** and an app cannot give it back (docs/90).
static void test_the_canvases_have_a_ceiling_and_are_given_back() {
  fresh_view();
  int8_t slots[ui::kMaxCanvases] = {};
  for (uint8_t i = 0; i + 1 < ui::kMaxCanvases; ++i) {
    TEST_ASSERT_TRUE(ui::create(8, 8, -1, true, slots[i]));
    TEST_ASSERT_TRUE(ui::at(slots[i]) != nullptr);
  }
  int8_t extra = -1;
  TEST_ASSERT_FALSE(ui::create(8, 8, -1, true, extra));
  TEST_ASSERT_TRUE(ui::release(slots[0]));
  // **Given back does not mean free yet**: the buffer lives until the next present has painted over it, and
  // then the slot is one to take (docs/90).
  TEST_ASSERT_NOT_NULL(ui::at(slots[0]));
  TEST_ASSERT_TRUE(ui::present());
  TEST_ASSERT_NULL(ui::at(slots[0]));
  TEST_ASSERT_TRUE(ui::create(8, 8, -1, true, extra));
  TEST_ASSERT_FALSE(ui::release(0));
}

// A view made again —a resize, a turn— **takes the canvases inside it with it**: the slots are free and
// the objects an app was holding point at nothing, which is what the generation check is for (docs/90).
static void test_a_view_made_again_takes_the_canvases_with_it() {
  fresh_view();
  int8_t slot = -1;
  TEST_ASSERT_TRUE(ui::create(20, 10, -1, true, slot));
  TEST_ASSERT_TRUE(ui::resize(320, 240));
  TEST_ASSERT_NULL(ui::at(slot));
  TEST_ASSERT_TRUE(ui::resize(960, 540));
}

// **A line is a line, and it costs one region.** A diagonal drawn pixel by pixel must not look like two
// hundred separate draws: the damage of a shape is the box it covers, or the list of eight fills and the
// whole canvas becomes one `gc16` (docs/30, docs/50).
static void test_a_line_is_a_line_and_costs_one_region() {
  fresh_view();
  int8_t slot = -1;
  TEST_ASSERT_TRUE(ui::create(40, 40, -1, true, slot));
  TEST_ASSERT_TRUE(ui::canvas_line(slot, 0, 0, 39, 39, 0, "du"));
  TEST_ASSERT_EQUAL_UINT8(1, ui::canvas_damage(slot));
  TEST_ASSERT_TRUE(ui::place(slot, 10, 10));
  TEST_ASSERT_TRUE(ui::present());
  // The diagonal is ink, its neighbours are not.
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(10, 10));
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(30, 30));
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(11, 10));
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(10, 11));
  // A straight line is a rectangle, and it lands on its own row.
  int8_t flat = -1;
  TEST_ASSERT_TRUE(ui::create(20, 6, -1, true, flat));
  TEST_ASSERT_TRUE(ui::canvas_line(flat, 0, 3, 19, 3, 0, "du"));
  TEST_ASSERT_TRUE(ui::place(flat, 0, 60));
  TEST_ASSERT_TRUE(ui::present());
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(0, 63));
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(19, 63));
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(0, 62));
}

// A frame is the outside of the box and the inside is left alone; a disc is all of it and a circle is the
// line round (docs/30).
static void test_a_frame_an_empty_inside_and_a_disc_a_full_one() {
  fresh_view();
  int8_t slot = -1;
  TEST_ASSERT_TRUE(ui::create(40, 40, -1, true, slot));
  TEST_ASSERT_TRUE(ui::canvas_frame(slot, 4, 4, 20, 20, 0, "du"));
  TEST_ASSERT_TRUE(ui::canvas_disc(slot, 30, 10, 6, 0, "du"));
  TEST_ASSERT_TRUE(ui::canvas_circle(slot, 30, 30, 6, 0, "du"));
  TEST_ASSERT_TRUE(ui::place(slot, 0, 0));
  TEST_ASSERT_TRUE(ui::present());
  // The frame: border ink, inside the canvas' own white.
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(4, 4));
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(23, 23));
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(10, 10));
  // The disc: its middle is ink. The circle: its middle is not, and its edge is.
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(30, 10));
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(30, 30));
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(30 + 6, 30));
}

// **A text is cut at the border of the canvas**, which is what a label is made of: a canvas has a border
// and nothing gets out of it, so a text that is too big loses what does not fit and nothing else
// (docs/30, docs/90).
static void test_a_text_is_cut_at_the_canvas_border() {
  fresh_view();
  int8_t slot = -1;
  TEST_ASSERT_TRUE(ui::create(20, 10, -1, true, slot));
  // "II" at twice the size is 22 pixels wide and 14 tall: wider and taller than the canvas.
  TEST_ASSERT_TRUE(ui::canvas_text(slot, 0, 0, "II", 2, 0, "du"));
  TEST_ASSERT_TRUE(ui::place(slot, 100, 100));
  TEST_ASSERT_TRUE(ui::present());
  // The stem of the first letter —the middle column of the glyph, at twice the size— is ink.
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(104, 102));
  // The second letter starts at twelve and its right column lands at eighteen: **the text reaches the
  // border**, which is what cutting means; a text that did not fit was dropped whole before this.
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(118, 100));
  // And nothing at all past the canvas, on either side: it is twenty by ten and the text is 22 by 14.
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(120, 100));
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(100, 110));
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(121, 105));
}

// The same five calls exist on the view of the app, which is the canvas the system gave it (docs/30).
static void test_the_primitives_are_on_the_view_too() {
  fresh_view();
  TEST_ASSERT_TRUE(ui::canvas_line(0, 0, 0, 30, 30, 0, "du"));
  TEST_ASSERT_TRUE(ui::canvas_frame(0, 40, 40, 10, 10, 0, "du"));
  TEST_ASSERT_TRUE(ui::canvas_disc(0, 60, 60, 3, 0, "du"));
  TEST_ASSERT_TRUE(ui::canvas_circle(0, 70, 70, 3, 0, "du"));
  TEST_ASSERT_TRUE(ui::canvas_text(0, 0, 200, "HI", 1, 0, "du"));
  TEST_ASSERT_TRUE(ui::present());
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(0, 0));
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(45, 40));
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(60, 60));
}

// **A canvas can hold another one, and the inside lands inside it**: the place of a child is read in the
// pixels of its father, so nesting is nesting and not arithmetic at every draw (docs/90).
static void test_a_canvas_can_hold_another_one_and_lands_inside_it() {
  fresh_view();
  int8_t outer = -1;
  int8_t inner = -1;
  TEST_ASSERT_TRUE(ui::create(40, 40, -1, true, outer));
  TEST_ASSERT_TRUE(ui::create(20, 20, outer, true, inner));
  TEST_ASSERT_EQUAL_INT8(outer, ui::parent_of(inner));
  TEST_ASSERT_TRUE(ui::canvas_fill_rect(inner, 0, 0, 20, 20, 0, "du"));
  TEST_ASSERT_TRUE(ui::place(outer, 10, 10));
  TEST_ASSERT_TRUE(ui::place(inner, 5, 5));
  TEST_ASSERT_TRUE(ui::present());
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(15, 15));
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(34, 34));
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(35, 35));
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(14, 14));
}

// **The order of the children is the order of the layers**: the one added later is composed later and it
// is in front where the two overlap (docs/90).
static void test_children_are_layers_in_the_order_they_were_added() {
  fresh_view();
  int8_t first = -1;
  int8_t second = -1;
  TEST_ASSERT_TRUE(ui::create(30, 30, -1, true, first));
  TEST_ASSERT_TRUE(ui::create(30, 30, -1, true, second));
  TEST_ASSERT_TRUE(ui::canvas_fill_rect(first, 0, 0, 30, 30, 0, "du"));
  TEST_ASSERT_TRUE(ui::canvas_fill_rect(second, 0, 0, 30, 30, 15, "du"));
  TEST_ASSERT_TRUE(ui::place(first, 0, 0));
  TEST_ASSERT_TRUE(ui::place(second, 10, 10));
  TEST_ASSERT_TRUE(ui::present());
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(20, 20));  // the later one wins
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(5, 5));     // where only the first one is
}

// **One father**: a canvas moved into another leaves the one it was in, and a canvas cannot end up inside
// itself. The view of the app is nobody's child (docs/90).
static void test_a_canvas_has_one_father_and_cannot_hold_itself() {
  fresh_view();
  int8_t outer = -1;
  int8_t inner = -1;
  int8_t other = -1;
  TEST_ASSERT_TRUE(ui::create(20, 20, -1, true, outer));
  TEST_ASSERT_TRUE(ui::create(10, 10, outer, true, inner));
  TEST_ASSERT_FALSE(ui::reparent(outer, inner));
  TEST_ASSERT_FALSE(ui::reparent(0, inner));
  TEST_ASSERT_TRUE(ui::create(40, 40, -1, true, other));
  TEST_ASSERT_TRUE(ui::reparent(inner, other));
  TEST_ASSERT_EQUAL_INT8(other, ui::parent_of(inner));
  // Its place is now read in the new father's pixels.
  TEST_ASSERT_TRUE(ui::place(inner, 1, 1));
  TEST_ASSERT_TRUE(ui::canvas_fill_rect(inner, 0, 0, 10, 10, 0, "du"));
  TEST_ASSERT_TRUE(ui::place(other, 100, 100));
  TEST_ASSERT_TRUE(ui::present());
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(101, 101));
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(110, 110));  // the last pixel of a 10 by 10 canvas
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(111, 111));
}

// **A control with nothing changed is not drawn.** The flag is set when the canvas is made, it is cleared
// when it has been drawn, and **moving a canvas sets it again, and on its fathers too**: a child that
// moved left its pixels inside its father's buffer and the father is the one that paints them over
// (docs/90).
static void test_a_canvas_is_only_drawn_when_something_changed() {
  fresh_view();
  int8_t father = -1;
  int8_t child = -1;
  TEST_ASSERT_TRUE(ui::create(40, 40, -1, true, father));
  TEST_ASSERT_TRUE(ui::create(20, 20, father, true, child));
  // New canvases owe a draw, and the view inside them does too.
  TEST_ASSERT_TRUE(ui::needs_draw(father));
  TEST_ASSERT_TRUE(ui::needs_draw(child));
  TEST_ASSERT_TRUE(ui::mark_drawn(father));
  TEST_ASSERT_TRUE(ui::mark_drawn(child));
  TEST_ASSERT_FALSE(ui::needs_draw(father));
  TEST_ASSERT_FALSE(ui::needs_draw(child));
  // Moving the child is a reason, and it goes up to the father.
  TEST_ASSERT_TRUE(ui::place(child, 5, 5));
  TEST_ASSERT_TRUE(ui::needs_draw(child));
  TEST_ASSERT_TRUE(ui::needs_draw(father));
  // Asking for the place it already has is not a change.
  TEST_ASSERT_TRUE(ui::mark_drawn(father));
  TEST_ASSERT_TRUE(ui::mark_drawn(child));
  TEST_ASSERT_TRUE(ui::place(child, 5, 5));
  TEST_ASSERT_FALSE(ui::needs_draw(child));
  // And what the app says is a change, is one — **but only for that canvas**: a control that changed its
  // own pixels is composed over its father again, so the father has nothing of its own to paint (docs/90).
  TEST_ASSERT_TRUE(ui::invalidate(child));
  TEST_ASSERT_TRUE(ui::needs_draw(child));
  TEST_ASSERT_FALSE(ui::needs_draw(father));
}

// **A control that redraws itself paints only what it owes.** The box opens before `on_draw` and every
// primitive is cut at it, so a control asked to repaint a strip does not repaint its page; the region that
// reaches the glass is that strip, at the quality the drawing asked for (docs/30, docs/90).
static void test_a_redraw_is_cut_to_the_box_the_control_owes() {
  fresh_view();
  int8_t slot = -1;
  TEST_ASSERT_TRUE(ui::create(40, 40, -1, true, slot));
  TEST_ASSERT_TRUE(ui::place(slot, 0, 0));
  TEST_ASSERT_TRUE(ui::mark_drawn(slot));
  // One reason, and it has a box: this is what changed.
  TEST_ASSERT_TRUE(ui::invalidate_rect(slot, 5, 5, 10, 10));
  TEST_ASSERT_TRUE(ui::begin_draw(slot));
  // The control's paint asks for the whole canvas and gets its box.
  TEST_ASSERT_TRUE(ui::canvas_fill_rect(slot, 0, 0, 40, 40, 0, "du"));
  TEST_ASSERT_TRUE(ui::canvas_text(slot, 0, 0, "WWWW", 2, 0, "du"));
  TEST_ASSERT_TRUE(ui::end_draw(slot));
  TEST_ASSERT_EQUAL_UINT8(1, ui::canvas_damage(slot));
  // The region is kept in whole nibbles, so the box that starts at 5 is copied from 4 and is 12 wide.
  TEST_ASSERT_EQUAL_UINT16(4, ui::at(slot)->damage[0].x);
  TEST_ASSERT_EQUAL_UINT16(12, ui::at(slot)->damage[0].w);
  TEST_ASSERT_TRUE(ui::present());
  // Inside the box there is ink, and outside it the page is untouched.
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(5, 5));
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(14, 14));
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(4, 5));
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(15, 5));
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(5, 4));
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(5, 15));
  // And the box is closed: the next draw of this canvas paints where it wants again.
  TEST_ASSERT_TRUE(ui::mark_drawn(slot));
  TEST_ASSERT_TRUE(ui::begin_draw(slot));
  TEST_ASSERT_TRUE(ui::end_draw(slot));
}

// **A control can have no view at all**, and it is not a hole in the tree: a grid, a box or a spacer is a
// frame with children, it costs no pixels, and its children land where the chain of places says. Twenty
// of them fit where one canvas would not (docs/90).
static void test_a_control_without_a_view_is_a_frame_in_the_tree() {
  fresh_view();
  int8_t box = -1;
  int8_t inside = -1;
  TEST_ASSERT_TRUE(ui::create(40, 40, -1, false, box));
  TEST_ASSERT_NULL(ui::at(box)->bits);
  TEST_ASSERT_EQUAL_UINT32(0, ui::at(box)->bytes);
  TEST_ASSERT_TRUE(ui::create(10, 10, box, true, inside));
  TEST_ASSERT_TRUE(ui::canvas_fill_rect(inside, 0, 0, 10, 10, 0, "du"));
  TEST_ASSERT_TRUE(ui::place(box, 20, 20));
  TEST_ASSERT_TRUE(ui::place(inside, 5, 5));
  TEST_ASSERT_TRUE(ui::present());
  // The child of a control with no view lands on the view, through the box's place.
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(25, 25));
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(34, 34));
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(35, 35));
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(24, 24));
}

// And a control with no view draws **on the canvas that holds it, cut at its own frame**: a label inside a
// grid inside the view paints on the view and cannot paint outside the cell it was given (docs/90).
static void test_a_control_without_a_view_paints_on_the_one_that_holds_it() {
  fresh_view();
  int8_t grid = -1;
  int8_t cell = -1;
  TEST_ASSERT_TRUE(ui::create(60, 40, -1, false, grid));
  TEST_ASSERT_TRUE(ui::place(grid, 10, 10));
  TEST_ASSERT_TRUE(ui::create(20, 10, grid, false, cell));
  TEST_ASSERT_TRUE(ui::place(cell, 5, 5));
  // The cell asks for more than its frame: the frame is what it gets.
  TEST_ASSERT_TRUE(ui::canvas_fill_rect(cell, 0, 0, 100, 100, 0, "du"));
  TEST_ASSERT_TRUE(ui::present());
  // Inside the cell —the grid's place plus its own— there is ink.
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(15, 15));
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(34, 24));
  // And nothing outside it, neither on the grid nor on the page.
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(35, 15));
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(15, 25));
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(40, 30));
  // The damage is on the view, because the cell has no buffer of its own.
  TEST_ASSERT_EQUAL_UINT8(0, ui::canvas_damage(cell));
}

// **A control that goes away is cleaned before it is freed.** Its pixels are on the page and its buffer is
// still in use until the next present has painted over them: it is marked, the control that paints is told
// the rectangle it leaves, and the slot is given back **after** the clean paint. No ghost on the paper and
// nothing stale in a buffer somebody else will get (docs/90).
static void test_a_canvas_that_goes_away_is_clean_before_it_is_freed() {
  fresh_view();
  int8_t slot = -1;
  TEST_ASSERT_TRUE(ui::create(20, 10, -1, true, slot));
  TEST_ASSERT_TRUE(ui::canvas_fill_rect(slot, 0, 0, 20, 10, 0, "du"));
  TEST_ASSERT_TRUE(ui::place(slot, 10, 10));
  TEST_ASSERT_TRUE(ui::present());
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(10, 10));

  TEST_ASSERT_TRUE(ui::release(slot));
  // **Still here**: the slot and the buffer live until the page is clean.
  TEST_ASSERT_NOT_NULL(ui::at(slot));
  TEST_ASSERT_TRUE(ui::at(slot)->pending_release);
  // And what paints has been told the rectangle it leaves behind.
  TEST_ASSERT_TRUE(ui::needs_draw(0));

  // The painter paints over that box, and the present lets go of the control.
  TEST_ASSERT_TRUE(ui::fill_rect(10, 10, 20, 10, 15, "du"));
  TEST_ASSERT_TRUE(ui::present());
  TEST_ASSERT_NULL(ui::at(slot));
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(10, 10));
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(29, 19));
}

// And the children go with the father: a father that goes away takes everything inside it, and all of it
// is freed after the same clean paint (docs/90).
static void test_a_canvas_takes_its_children_with_it_when_it_goes() {
  fresh_view();
  int8_t father = -1;
  int8_t child = -1;
  TEST_ASSERT_TRUE(ui::create(40, 40, -1, true, father));
  TEST_ASSERT_TRUE(ui::create(10, 10, father, true, child));
  TEST_ASSERT_TRUE(ui::canvas_fill_rect(child, 0, 0, 10, 10, 0, "du"));
  TEST_ASSERT_TRUE(ui::place(father, 10, 10));
  TEST_ASSERT_TRUE(ui::present());
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(10, 10));

  TEST_ASSERT_TRUE(ui::release(father));
  TEST_ASSERT_TRUE(ui::at(father)->pending_release);
  TEST_ASSERT_TRUE(ui::at(child)->pending_release);
  TEST_ASSERT_TRUE(ui::fill_rect(10, 10, 40, 40, 15, "du"));
  TEST_ASSERT_TRUE(ui::present());
  TEST_ASSERT_NULL(ui::at(father));
  TEST_ASSERT_NULL(ui::at(child));
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(10, 10));
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(49, 49));
}

// **A father that goes away owes one rectangle, not one per child.** Deleting a subtree must not fill the
// list of damage with pieces of the same hole: the control that paints owes the father's box and nothing
// else, whatever hangs inside it (docs/90).
static void test_a_father_that_goes_away_owes_one_rectangle_not_one_per_child() {
  fresh_view();
  int8_t father = -1;
  TEST_ASSERT_TRUE(ui::create(60, 60, -1, true, father));
  for (uint8_t i = 0; i < 3; ++i) {
    int8_t kid = -1;
    TEST_ASSERT_TRUE(ui::create(10, 10, father, true, kid));
    TEST_ASSERT_TRUE(ui::place(kid, static_cast<uint16_t>(i * 15), 0));
  }
  TEST_ASSERT_TRUE(ui::place(father, 10, 10));
  TEST_ASSERT_TRUE(ui::present());
  TEST_ASSERT_EQUAL_UINT8(0, ui::damage_count());

  TEST_ASSERT_TRUE(ui::release(father));
  // The painter has been told **once**, with the father's box: it owes a draw, and nothing is damaged until
  // it draws.
  TEST_ASSERT_TRUE(ui::needs_draw(0));
  TEST_ASSERT_EQUAL_UINT8(0, ui::damage_count());
  // The painter paints its whole page, as an `on_draw` would, and gets cut at the box it owes.
  TEST_ASSERT_TRUE(ui::begin_draw(0));
  TEST_ASSERT_TRUE(ui::fill_rect(0, 0, 960, 540, 15, "du"));
  TEST_ASSERT_TRUE(ui::end_draw(0));
  TEST_ASSERT_EQUAL_UINT8(1, ui::damage_count());
  TEST_ASSERT_EQUAL_UINT16(10, ui::damage(0).x);
  TEST_ASSERT_EQUAL_UINT16(10, ui::damage(0).y);
  TEST_ASSERT_EQUAL_UINT16(60, ui::damage(0).w);
  TEST_ASSERT_EQUAL_UINT16(60, ui::damage(0).h);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_without_an_app_there_is_no_view);
  RUN_TEST(test_the_canvas_is_the_glass_and_starts_white);
  RUN_TEST(test_drawing_stays_on_the_canvas_and_leaves_damage);
  RUN_TEST(test_present_sends_the_damage_with_its_quality);
  RUN_TEST(test_two_qualities_are_two_paints);
  RUN_TEST(test_damage_of_the_same_quality_merges);
  RUN_TEST(test_present_with_a_quality_overrides_the_damage);
  RUN_TEST(test_the_damage_is_translated_to_where_the_view_sits);
  RUN_TEST(test_the_damage_is_cut_at_the_edge_of_the_glass);
  RUN_TEST(test_a_clear_is_the_whole_view);
  RUN_TEST(test_a_pixel_is_one_nibble);
  RUN_TEST(test_a_region_at_an_odd_x_lands_where_it_says);
  RUN_TEST(test_drawing_outside_the_view_is_dropped);
  RUN_TEST(test_an_unknown_quality_is_refused);
  RUN_TEST(test_a_flood_of_regions_becomes_one_paint);
  RUN_TEST(test_a_resize_makes_a_white_canvas_of_another_size);
  RUN_TEST(test_a_view_of_nothing_is_refused);
  RUN_TEST(test_a_child_canvas_lands_where_it_is_placed);
  RUN_TEST(test_a_canvas_is_cut_at_its_border);
  RUN_TEST(test_a_canvas_that_paints_nothing_composes_nothing);
  RUN_TEST(test_the_canvases_have_a_ceiling_and_are_given_back);
  RUN_TEST(test_a_view_made_again_takes_the_canvases_with_it);
  RUN_TEST(test_a_line_is_a_line_and_costs_one_region);
  RUN_TEST(test_a_frame_an_empty_inside_and_a_disc_a_full_one);
  RUN_TEST(test_a_text_is_cut_at_the_canvas_border);
  RUN_TEST(test_the_primitives_are_on_the_view_too);
  RUN_TEST(test_a_canvas_can_hold_another_one_and_lands_inside_it);
  RUN_TEST(test_children_are_layers_in_the_order_they_were_added);
  RUN_TEST(test_a_canvas_has_one_father_and_cannot_hold_itself);
  RUN_TEST(test_a_canvas_is_only_drawn_when_something_changed);
  RUN_TEST(test_a_redraw_is_cut_to_the_box_the_control_owes);
  RUN_TEST(test_a_control_without_a_view_is_a_frame_in_the_tree);
  RUN_TEST(test_a_control_without_a_view_paints_on_the_one_that_holds_it);
  RUN_TEST(test_a_canvas_that_goes_away_is_clean_before_it_is_freed);
  RUN_TEST(test_a_canvas_takes_its_children_with_it_when_it_goes);
  RUN_TEST(test_a_father_that_goes_away_owes_one_rectangle_not_one_per_child);
  return UNITY_END();
}
