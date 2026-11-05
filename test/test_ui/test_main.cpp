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

#include <unity.h>

#include "emu/model.h"
#include "emu/options.h"
#include "emu/surface.h"
#include "emu/gui.h"
#include "emu/ink.h"
#include "hal/hal.h"
#include "kernel/display.h"
#include "kernel/meter.h"
#include "kernel/ui.h"
#include "pal/pal.h"

// One name for the policy, so the tests read like the document (docs/30).
namespace ui = kernel::ui;

void setUp(void) {}

void tearDown(void) {}

namespace {

bool g_booted = false;

// A bench **with a glass**: the glass is the model's (`Model::glass`, the Paper) and it is on unless the
// run says no, which is what the binary does by itself. Only the two pieces this suite is about are
// booted —the board's surface and the app's canvas— because a full boot drags the disk and the radio in
// for nothing.
void boot_with_a_panel() {
  if (g_booted) return;
  emu::Options opts = emu::options();
  // **The board that has the panel**: the default model of the bench is the ATOM, which has no screen to
  // try anything against (docs/50, docs/85).
  opts.board = "m5stack-paper";
  opts.display = true;
  // **The times of the hardware are paid in this suite**: it measures the ink and the bus of the glass,
  // and a paint that costs nothing is a paint no panel has (docs/85).
  opts.delays = true;
  emu::set_options(opts);
  kernel::display::boot(hal::platform());
  // **In the glass' own frame**, which is 960x540: this suite is about the damage and the copy, and the
  // turn has its own proof (test_orientation) and its own check in test_bar.
  kernel::display::set_rotation(0);
  ui::boot(hal::platform(), 0);  // no bar in this suite: the view is the whole glass
  g_booted = true;
}

emu::MemorySurface& glass() { return *emu::memory_surface(); }

// The level of one pixel of the view of the app, which is where a control without a buffer paints.
uint8_t view_level(uint16_t x, uint16_t y) {
  const ui::Canvas& view = ui::canvas();
  if (view.bits == nullptr || x >= view.width || y >= view.height) return 0;
  const uint8_t byte = view.bits[static_cast<uint32_t>(y) * view.stride + x / 2];
  return ((x & 1) == 0) ? static_cast<uint8_t>(byte >> 4) : static_cast<uint8_t>(byte & 0x0F);
}

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

// **Waiting for ink is waiting on this machine**: the bench's clock is the PC's, so the only way to see a
// paint end is to let time pass, which is what the kernel's own queue does when it has nothing to do
// (docs/85).
void wait_ms(int32_t ms) {
  pal::Event event;
  pal::system().queue()->pop(event, ms);
  // **Time passing is what moves the ink**: the panel is asked the way the window asks it when it
  // redraws, so a test that waits for a paint sees the paint arrive (docs/85).
  emu::memory_surface()->step_ink();
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


// **The window shows the glass in the colours of the panel**, and the part of that which can be proved
// without a screen is the translation: four bits per pixel with the left one in the high nibble become
// one pixel each, black at 0 and white at 15 (docs/50, docs/85).
static void test_the_window_shows_the_glass_in_the_colours_of_the_panel() {
  // **The two ends are the ink and the paper of a real panel**, which is not black and white: the paper is
  // warm and the darkest level is a very dark grey (docs/85).
  TEST_ASSERT_EQUAL_HEX32(0x001A1B18u, emu::gui_level_colour(0));
  TEST_ASSERT_EQUAL_HEX32(0x00EFEADCu, emu::gui_level_colour(15));
  // A level of the middle is between the two, and it is warm too: the red is above the blue.
  const uint32_t middle = emu::gui_level_colour(8);
  const uint32_t middle_r = (middle >> 16) & 0xFF;
  const uint32_t middle_b = middle & 0xFF;
  TEST_ASSERT_TRUE(middle_r > middle_b);
  TEST_ASSERT_TRUE(middle_r > 0x1Au && middle_r < 0xEFu);

  // A byte is two pixels, the left one first: 0x3C is the level 3 and then the level 12.
  const uint8_t packed[2] = {0x3Cu, 0xFFu};
  uint32_t out[4] = {0, 0, 0, 0};
  emu::gui_expand(packed, 4, 1, out, 1);
  TEST_ASSERT_EQUAL_HEX32(emu::gui_level_colour(3), out[0]);
  TEST_ASSERT_EQUAL_HEX32(emu::gui_level_colour(12), out[1]);
  TEST_ASSERT_EQUAL_HEX32(emu::gui_level_colour(15), out[2]);
  TEST_ASSERT_EQUAL_HEX32(emu::gui_level_colour(15), out[3]);

  // A glass of an odd width ends on a nibble nobody wrote, and it is white like the panel's own memory.
  uint32_t odd[3] = {0, 0, 0};
  emu::gui_expand(packed, 3, 1, odd, 1);
  TEST_ASSERT_EQUAL_HEX32(emu::gui_level_colour(3), odd[0]);
  TEST_ASSERT_EQUAL_HEX32(emu::gui_level_colour(12), odd[1]);
  TEST_ASSERT_EQUAL_HEX32(emu::gui_level_colour(15), odd[2]);

  // **At half, every other pixel**: a page of four columns is two, and what is drawn is the left one of
  // each pair, which is what lets a page taller than the screen fit instead of being cut (docs/85).
  uint32_t half[2] = {0, 0};
  emu::gui_expand(packed, 4, 1, half, 2);
  TEST_ASSERT_EQUAL_HEX32(emu::gui_level_colour(3), half[0]);
  TEST_ASSERT_EQUAL_HEX32(emu::gui_level_colour(15), half[1]);
}


// **The glass moves ink and does not switch pixels** (docs/85): with the model on, a `du` keeps a part of
// the page before it, the trace adds up while nothing cleans, and a `gc16` takes the page back. `level_at`
// is what the paint asked for and `shown_at` is where the pigment ended up.
static void test_the_ink_of_a_du_keeps_a_trace_and_a_gc16_cleans_it(void) {
  fresh_view();
  TEST_ASSERT_TRUE(glass().set_ink(true));
  // A black square on a white page, painted with the quick quality.
  TEST_ASSERT_TRUE(ui::canvas_fill_rect(0, 10, 10, 40, 40, 0, "du"));
  TEST_ASSERT_TRUE(ui::present());
  // **A paint lands when the ink has arrived**: a `du` is 120 ms, and until then the glass is the page
  // before it and the dirt of the paint has not been added (docs/85).
  wait_ms(150);
  // The paint asked for black and the ink reached it; the dirt of that paint is on the glass now.
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(20, 20));
  TEST_ASSERT_TRUE(glass().dirt_at(20, 20) > 0);

  // White over it, **with the same quick quality**: a part of the black is still there, which is exactly
  // what a person sees on a panel painted that way.
  TEST_ASSERT_TRUE(ui::canvas_fill_rect(0, 10, 10, 40, 40, 15, "du"));
  TEST_ASSERT_TRUE(ui::present());
  wait_ms(150);
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(20, 20));
  const uint8_t trace = glass().shown_at(20, 20);
  TEST_ASSERT_TRUE_MESSAGE(trace < 15, "the du left no trace of the black it painted over");
  TEST_ASSERT_TRUE_MESSAGE(trace > 7, "the trace is too dark to be a du on a page");
  // And the ink of the page is not the ink of the paint: this is the whole point of the model.
  TEST_ASSERT_TRUE(glass().ink_errors() > 0);

  // **A paint that cleans takes it back**: white with `gc16` and the pixel is white again, without dirt.
  TEST_ASSERT_TRUE(ui::canvas_fill_rect(0, 10, 10, 40, 40, 15, "gc16"));
  TEST_ASSERT_TRUE(ui::present());
  // **A `gc16` blinks before the image arrives**: the glass is not the page it painted until the ink has
  // stopped, which is the whole of the 450 ms (docs/85).
  wait_ms(500);
  TEST_ASSERT_EQUAL_UINT8(15, glass().shown_at(20, 20));
  TEST_ASSERT_EQUAL_UINT8(0, glass().dirt_at(20, 20));
  // **A paint that cleans takes back the region it paints**: white with `gc16` and every pixel of it is
  // white again, without dirt. The speckle of a paint somewhere else on the glass is somebody else's
  // page and is not cleaned by this one (docs/85).
  TEST_ASSERT_TRUE(ui::canvas_fill_rect(0, 10, 10, 40, 40, 15, "gc16"));
  TEST_ASSERT_TRUE(ui::present());
  wait_ms(500);
  for (uint16_t y = 10; y < 50; ++y) {
    for (uint16_t x = 10; x < 50; ++x) {
      TEST_ASSERT_EQUAL_UINT8(15, glass().shown_at(x, y));
      TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(x, y));
    }
  }
  TEST_ASSERT_EQUAL_UINT8(0, glass().dirt_at(20, 20));
  TEST_ASSERT_TRUE(glass().set_ink(false));
}

// **The level a band of the glass shows**, averaged over twenty pixels: one pixel can be a miss of the
// speckle, and what the test below is about is the shape of the trace and not the grain of it (docs/85).
static uint8_t band_of_the_glass(uint16_t y) {
  uint32_t sum = 0;
  uint16_t count = 0;
  for (uint16_t x = 30; x < 50; ++x) {
    sum += glass().shown_at(x, y);
    ++count;
  }
  return static_cast<uint8_t>((sum + count / 2) / count);
}

// **A rich page leaves a rich trace, and a flat one leaves almost nothing** (docs/85): a quick paint
// cannot take what it finds, so what is left of a ladder of sixteen levels is that ladder at lower
// contrast —its dark end still dark, its light end still light— and what is left of a page that was all
// one colour is a page that is all one grey. That difference is exactly what a person sees as ghosting,
// and it is the reason this model exists.
static void test_the_trace_of_a_rich_page_is_richer_than_a_flat_one(void) {
  fresh_view();
  TEST_ASSERT_TRUE(glass().set_ink(true));

  // A ladder of sixteen levels, a band each, painted with the quality that cleans.
  for (uint8_t level = 0; level < 16; ++level) {
    TEST_ASSERT_TRUE(ui::canvas_fill_rect(0, 20, static_cast<uint16_t>(10 + level * 8), 80, 8, level,
                                          "gc16"));
  }
  TEST_ASSERT_TRUE(ui::present());
  wait_ms(500);

  // White over it with the quick quality: what comes out is the ladder at lower contrast.
  TEST_ASSERT_TRUE(ui::canvas_fill_rect(0, 20, 10, 80, 128, 15, "du"));
  TEST_ASSERT_TRUE(ui::present());
  wait_ms(150);
  uint8_t rich_low = 15;
  uint8_t rich_high = 0;
  for (uint8_t level = 0; level < 16; ++level) {
    const uint8_t shown = band_of_the_glass(static_cast<uint16_t>(10 + level * 8 + 3));
    if (shown < rich_low) rich_low = shown;
    if (shown > rich_high) rich_high = shown;
  }
  const uint8_t rich_spread = static_cast<uint8_t>(rich_high - rich_low);

  // The same white with the same quick quality over a page that was all black: the result is one grey
  // with no shape in it, which is what a person sees when a page of text is repainted.
  TEST_ASSERT_TRUE(ui::canvas_fill_rect(0, 20, 10, 80, 128, 0, "gc16"));
  TEST_ASSERT_TRUE(ui::present());
  wait_ms(500);
  TEST_ASSERT_TRUE(ui::canvas_fill_rect(0, 20, 10, 80, 128, 15, "du"));
  TEST_ASSERT_TRUE(ui::present());
  wait_ms(150);
  uint8_t flat_low = 15;
  uint8_t flat_high = 0;
  for (uint8_t level = 0; level < 16; ++level) {
    const uint8_t shown = band_of_the_glass(static_cast<uint16_t>(10 + level * 8 + 3));
    if (shown < flat_low) flat_low = shown;
    if (shown > flat_high) flat_high = shown;
  }
  const uint8_t flat_spread = static_cast<uint8_t>(flat_high - flat_low);

  TEST_ASSERT_TRUE_MESSAGE(rich_spread >= 4, "a page of sixteen levels left no shape in its trace");
  TEST_ASSERT_TRUE_MESSAGE(flat_spread <= 1, "a flat page left a shape in its trace");
  TEST_ASSERT_TRUE(glass().set_ink(false));
}

// **A paint takes as long as the quality takes**: a `du` is about a tenth of a second and a `gc16` about
// half of one, and `ui.idle()` is the question that says when the ink has stopped moving — which is what
// makes a measurement taken on the bench mean something (docs/30, docs/85).
static void test_a_paint_takes_the_time_of_its_quality(void) {
  fresh_view();
  TEST_ASSERT_TRUE(glass().set_ink(true));
  TEST_ASSERT_TRUE(ui::canvas_fill_rect(0, 10, 10, 10, 10, 0, "du"));
  TEST_ASSERT_TRUE(ui::present());
  // Just asked: the ink has not moved yet.
  TEST_ASSERT_FALSE(ui::idle());
  wait_ms(200);
  TEST_ASSERT_TRUE_MESSAGE(ui::idle(), "a du of a few pixels is not two hundred milliseconds");

  TEST_ASSERT_TRUE(ui::canvas_fill_rect(0, 10, 10, 10, 10, 0, "gc16"));
  TEST_ASSERT_TRUE(ui::present());
  wait_ms(200);
  // **Half a second is still painting a `gc16`**, and this is the number the datasheet gives for it.
  TEST_ASSERT_FALSE_MESSAGE(ui::idle(), "a gc16 is not over in two hundred milliseconds");
  wait_ms(400);
  TEST_ASSERT_TRUE(ui::idle());
  TEST_ASSERT_TRUE(glass().set_ink(false));
}

// **The waits are one switch**: with them off —`--no-delay`, and every suite except this one— the model
// is the same and the clock is never asked, so a `du` still leaves its trace and `ui.idle()` is true the
// moment the paint is asked for. It is what makes a test that is not about time run in milliseconds
// (docs/85).
static void test_with_the_waits_off_a_paint_is_instant_and_still_leaves_ink() {
  fresh_view();
  TEST_ASSERT_TRUE(glass().set_ink(true));
  emu::Options opts = emu::options();
  opts.delays = false;
  emu::set_options(opts);
  TEST_ASSERT_TRUE(ui::canvas_fill_rect(0, 10, 10, 40, 40, 15, "du"));
  TEST_ASSERT_TRUE(ui::present());
  // Nothing of the clock: no bus, no ink, and the glass is over at once.
  TEST_ASSERT_TRUE_MESSAGE(ui::idle(), "a paint with the waits off is not over at once");
  // **And the ink is still the ink**: the dirt of the paint is on the pixel, which is the model and not
  // the clock.
  TEST_ASSERT_TRUE(glass().dirt_at(20, 20) > 0);
  TEST_ASSERT_TRUE(glass().set_ink(false));
  opts = emu::options();
  opts.delays = true;
  emu::set_options(opts);
}

// **The bus is part of the paint, and not a detail of the bench**: the page of `run panel` is the whole
// view with `du` (540 x 932) and the ladder of sixteen levels with `gc16` (528 x 120), and on 1/11 it
// came to **1 970 ms at the bench** and **1 985 ms on the board** (docs/95, E-7). This is the arithmetic
// that makes those two numbers the same number: if it moves, a constant in `emu_ink.cpp` moved and the
// documents have to move with it (docs/85).
static void test_a_page_costs_what_the_board_says_a_page_costs(void) {
  const uint32_t du_ms = emu::ink_mode(hal::UpdateMode::kDu).ms;
  const uint32_t gc16_ms = emu::ink_mode(hal::UpdateMode::kGc16).ms;
  const uint32_t bus = emu::ink_transfer_us(540, 932) + emu::ink_transfer_us(528, 120);
  const uint32_t page = bus + (du_ms + gc16_ms) * 1000u;
  TEST_ASSERT_UINT32_WITHIN_MESSAGE(20000, 1970000, page, "the page of run panel is not the page the bench painted");
  TEST_ASSERT_UINT32_WITHIN_MESSAGE(40000, 1985000, page, "the page of run panel is not the page the board painted");
  // A region of nothing costs nothing, and a full page of the glass is over half a second of wire and rows.
  TEST_ASSERT_EQUAL_UINT32(0, emu::ink_transfer_us(0, 540));
  TEST_ASSERT_EQUAL_UINT32(0, emu::ink_transfer_us(540, 0));
  TEST_ASSERT_TRUE(emu::ink_transfer_us(960, 540) > 540000u);
  // Twice the rows is twice the price.
  TEST_ASSERT_EQUAL_UINT32(2 * emu::ink_transfer_us(960, 270), emu::ink_transfer_us(960, 540));
}

// **An `a2` blinks**: the page goes white, then black, and only then the image, and what an app waits for
// with `ui.idle()` is all three. The two blinks are frames with no paint behind them, which is why the
// surface counts them and the window redraws on the count (docs/85).
static void test_an_a2_flashes_the_page_before_the_image_arrives(void) {
  fresh_view();
  TEST_ASSERT_TRUE(glass().set_ink(true));
  const uint32_t steps_before = glass().ink_steps();
  TEST_ASSERT_TRUE(ui::canvas_fill_rect(0, 10, 10, 40, 40, 5, "a2"));
  TEST_ASSERT_TRUE(ui::present());
  // The paint asked for the grey and the glass is white: the blink has begun and the ink has not moved.
  TEST_ASSERT_EQUAL_UINT8(5, glass().level_at(20, 20));
  TEST_ASSERT_EQUAL_UINT8(15, glass().shown_at(20, 20));
  TEST_ASSERT_FALSE(ui::idle());
  TEST_ASSERT_TRUE(glass().ink_steps() > steps_before);

  // A third of the wait in, the page is black.
  wait_ms(120);
  TEST_ASSERT_FALSE(ui::idle());
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, glass().shown_at(20, 20), "an a2 does not blink black");

  // And the last third is the image arriving, with the ink of its quality over what was there.
  wait_ms(240);
  TEST_ASSERT_TRUE(ui::idle());
  TEST_ASSERT_UINT8_WITHIN(2, 5, glass().shown_at(20, 20));
  TEST_ASSERT_TRUE(glass().set_ink(false));
}

// **A `gc16` blinks before the image arrives**: the region goes black, white, black and white again over the
// first two thirds of its 450 ms, and the last third is the image landing in pieces. It is what a person sees
// on a panel that cleans, and the reason the window redraws every phase and not only at the end (docs/85).
static void test_a_gc16_blinks_before_the_image_arrives() {
  fresh_view();
  TEST_ASSERT_TRUE(glass().set_ink(true));
  const uint32_t steps_before = glass().ink_steps();
  TEST_ASSERT_TRUE(ui::canvas_fill_rect(0, 10, 10, 40, 40, 5, "gc16"));
  TEST_ASSERT_TRUE(ui::present());
  // The first blink is black and it is on the glass the moment the paint is asked for.
  TEST_ASSERT_EQUAL_UINT8(0, glass().shown_at(20, 20));
  TEST_ASSERT_FALSE(ui::idle());
  // A blink is a frame with no paint behind it.
  TEST_ASSERT_TRUE(glass().ink_steps() > steps_before);
  // A quarter of a second in, the region is white: the blink goes both ways and not once.
  wait_ms(100);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(15, glass().shown_at(20, 20), "a gc16 blinked only one way");
  wait_ms(80);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, glass().shown_at(20, 20), "the second black of a gc16 is not there");
  // And the last third is the image: when the ink stops the pixel is the grey the paint asked for.
  wait_ms(300);
  TEST_ASSERT_TRUE(ui::idle());
  TEST_ASSERT_UINT8_WITHIN(2, 5, glass().shown_at(20, 20));
  TEST_ASSERT_TRUE(glass().set_ink(false));
}

// **The panel keeps blinking while the driver waits** (docs/85). A paint holds its caller for the wire of
// the next one and for the ink, and that is where the blinks of a `gc16` were lost: the window was redrawn
// once per turn, a turn is one paint, and a person saw the page before and the page after with nothing in
// between (found on the window, 4/11). The bench that has a window installs a pump —`pump_gui`— and the
// glass calls it from inside its own waits, so this is the same question a window asks: does the ink move
// while nobody's code is running?
static uint32_t g_pumps = 0;
static uint32_t g_blinks_the_pump_saw = 0;

static void pump_like_a_window(void*) {
  ++g_pumps;
  const uint32_t before = emu::memory_surface()->ink_steps();
  emu::memory_surface()->step_ink();
  if (emu::memory_surface()->ink_steps() != before) ++g_blinks_the_pump_saw;
}

static void test_the_glass_is_walked_while_the_paint_waits(void) {
  fresh_view();
  TEST_ASSERT_TRUE(glass().set_ink(true));
  g_pumps = 0;
  g_blinks_the_pump_saw = 0;
  glass().set_wait_pump(&pump_like_a_window, nullptr);

  // The page that starts the flash, and then **a second paint**, which is the one that waits for the ink
  // of the first: four hundred and fifty milliseconds in which none of the app's code runs.
  TEST_ASSERT_TRUE(ui::canvas_fill_rect(0, 10, 10, 200, 200, 0, "gc16"));
  TEST_ASSERT_TRUE(ui::present());
  TEST_ASSERT_TRUE(ui::canvas_fill_rect(0, 10, 10, 200, 200, 15, "gc16"));
  TEST_ASSERT_TRUE(ui::present());

  TEST_ASSERT_TRUE_MESSAGE(g_pumps > 0, "a paint waited without asking the window once");
  // **The blinks were frames and not a sleep**: three of them at least happened while the caller was
  // inside the wait, which is what the window draws and what the board shows.
  TEST_ASSERT_TRUE_MESSAGE(g_blinks_the_pump_saw >= 3,
                           "the blinks of a gc16 were lost inside the wait of the next paint");
  glass().set_wait_pump(nullptr, nullptr);
  TEST_ASSERT_TRUE(glass().set_ink(false));
}

// **Only the board that has a glass announces one**, and the same for the finger: the hardware is the
// model's (`Model::glass`, `Model::touch`) and an option cannot give a screen to a board that has none —
// a bench that did is a board nobody can buy. It is the rule the binary obeys to turn both on by itself
// on a Paper, and the reason `run draw` works there without a flag (docs/50, docs/85).
static void test_only_the_board_that_has_a_glass_announces_one() {
  // The model is the one list of the hardware, and nothing else needs to be asked.
  TEST_ASSERT_TRUE_MESSAGE(emu::model_of("m5stack-paper").glass, "the Paper has no glass");
  TEST_ASSERT_TRUE_MESSAGE(emu::model_of("m5stack-paper").touch, "the Paper has no touch panel");
  TEST_ASSERT_FALSE_MESSAGE(emu::model_of("m5stack-atom").glass, "an ATOM has a glass");
  TEST_ASSERT_FALSE_MESSAGE(emu::model_of("m5stack-atom").touch, "an ATOM has a touch panel");
  TEST_ASSERT_FALSE_MESSAGE(emu::model_of("m5stack-atom-matrix").glass, "a Matrix has a glass");

  // **The options are not enough**: with the glass and the finger asked for, a Paper announces both and
  // an ATOM announces neither (docs/50).
  const emu::Options before = emu::options();
  emu::Options opts = before;
  opts.display = true;
  opts.touch = true;
  opts.board = "m5stack-paper";
  emu::set_options(opts);
  TEST_ASSERT_TRUE(hal::platform().caps().has(hal::kCapDisplay));
  TEST_ASSERT_TRUE(hal::platform().caps().has(hal::kCapTouch));

  opts.board = "m5stack-atom";
  emu::set_options(opts);
  TEST_ASSERT_FALSE(hal::platform().caps().has(hal::kCapDisplay));
  TEST_ASSERT_FALSE(hal::platform().caps().has(hal::kCapTouch));
  emu::set_options(before);
}

// **The T-Koala is one light, no key, a card slot and the mapped PSRAM**: the bench asks its model list
// for the board by the name the firmware answers, and the board has no screen to fake (docs/50, docs/85).
static void test_the_t_koala_model_is_one_light_a_slot_and_four_megabytes_of_psram() {
  const emu::Model koala = emu::model_of("ttgo-t-koala");
  TEST_ASSERT_EQUAL_STRING("ttgo-t-koala", koala.name);
  TEST_ASSERT_EQUAL_UINT32(1, koala.lights);
  TEST_ASSERT_EQUAL_UINT32(0, koala.keys);
  TEST_ASSERT_FALSE_MESSAGE(koala.glass, "the T-Koala has a glass");
  TEST_ASSERT_FALSE_MESSAGE(koala.touch, "the T-Koala has a touch panel");
  TEST_ASSERT_FALSE_MESSAGE(koala.imu, "the T-Koala has an inertial sensor");
  TEST_ASSERT_FALSE_MESSAGE(koala.gauge, "the T-Koala has a battery gauge");
  TEST_ASSERT_TRUE_MESSAGE(koala.slot, "the T-Koala has no card slot");
  TEST_ASSERT_EQUAL_UINT32(8192, koala.psram_chip_kb);
  TEST_ASSERT_EQUAL_UINT32(4096, koala.psram_kb);
  TEST_ASSERT_EQUAL_UINT32(4096, koala.app_limit_kb);
  TEST_ASSERT_EQUAL_UINT32(0x160000, koala.disk_bytes);
}

// **A pixel of the window is a block of the page**, and the finger of the mouse lands in the middle of it:
// the window draws one pixel for every `scale` of the glass, and the way back is what makes a click a tap
// on the pixel a person is pointing at. A point outside the page is pinned to its edge instead of being a
// finger on no glass (docs/85).
static void test_a_pixel_of_the_window_lands_in_the_middle_of_its_block() {
  uint16_t x = 0;
  uint16_t y = 0;
  // The page as it is: the window pixel is the page pixel.
  emu::gui_page_point(10, 20, 1, 540, 960, x, y);
  TEST_ASSERT_EQUAL_UINT16(10, x);
  TEST_ASSERT_EQUAL_UINT16(20, y);
  // At half, one window pixel stands for four page pixels and the finger is on the middle one.
  emu::gui_page_point(10, 20, 2, 540, 960, x, y);
  TEST_ASSERT_EQUAL_UINT16(21, x);
  TEST_ASSERT_EQUAL_UINT16(41, y);
  // The two corners of the window are the two corners of the page, without one pixel left over.
  emu::gui_page_point(0, 0, 2, 540, 960, x, y);
  TEST_ASSERT_EQUAL_UINT16(1, x);
  TEST_ASSERT_EQUAL_UINT16(1, y);
  emu::gui_page_point(269, 479, 2, 540, 960, x, y);
  TEST_ASSERT_EQUAL_UINT16(539, x);
  TEST_ASSERT_EQUAL_UINT16(959, y);
  // And a pointer that is not on the page is the edge of it, not a finger off the glass.
  emu::gui_page_point(9999, -5, 2, 540, 960, x, y);
  TEST_ASSERT_EQUAL_UINT16(539, x);
  TEST_ASSERT_EQUAL_UINT16(0, y);
}

// **The size of the page in the window is decided once** (docs/85): without asking, the window takes the
// smallest scale that fits the screen with the strip of the keys, and a run that asks for one gets it even
// when the screen would hold the page whole — which is how the page is watched at half on a big display.
// The page of the model is 540x960 and none of this changes it.
static void test_the_window_is_the_page_at_the_scale_that_fits_or_the_one_asked_for() {
  // A screen of 1920x1080 holds the page whole, with the Paper's column of three keys on the right.
  TEST_ASSERT_EQUAL_UINT8(1, emu::gui_fit_scale(540, 960, 3, emu::KeySide::kRight, 1920, 1080, 0));
  // One of 900 rows does not: 960 does not fit and 480 does, so the page is at half.
  TEST_ASSERT_EQUAL_UINT8(2, emu::gui_fit_scale(540, 960, 3, emu::KeySide::kRight, 1280, 900, 0));
  // Shorter than half the page, the window takes a quarter of it, and never goes past four.
  TEST_ASSERT_EQUAL_UINT8(4, emu::gui_fit_scale(540, 960, 3, emu::KeySide::kRight, 800, 300, 0));
  // **Asked for, the scale wins**: half on a screen that could show the page whole, and whole on one that
  // could not.
  TEST_ASSERT_EQUAL_UINT8(2, emu::gui_fit_scale(540, 960, 3, emu::KeySide::kRight, 1920, 1080, 2));
  TEST_ASSERT_EQUAL_UINT8(1, emu::gui_fit_scale(540, 960, 3, emu::KeySide::kRight, 800, 300, 1));
  // An ATOM has no page: its window is the row of its one key, and that has no scale to choose.
  TEST_ASSERT_EQUAL_UINT8(1, emu::gui_fit_scale(0, 0, 1, emu::KeySide::kBottom, 1280, 900, 0));
}

// **A label ends with three dots and not with a letter cut in half** (docs/30, docs/90): the system face is
// five pixels wide with one between glyphs, so "Hello, world" is 71 pixels; a narrower label keeps the
// longest prefix whose three dots still fit. When even the dots do not fit they are what is drawn, and the
// frame cuts them.
static void test_a_label_cuts_its_text_with_three_dots() {
  char shown[32];
  TEST_ASSERT_EQUAL_UINT16(71, ui::label_text(-1, "Hello, world", 100, 1, shown, sizeof(shown)));
  TEST_ASSERT_EQUAL_STRING("Hello, world", shown);
  TEST_ASSERT_EQUAL_UINT16(59, ui::label_text(-1, "Hello, world", 60, 1, shown, sizeof(shown)));
  TEST_ASSERT_EQUAL_STRING("Hello, ...", shown);
  TEST_ASSERT_EQUAL_UINT16(35, ui::label_text(-1, "Hello, world", 40, 1, shown, sizeof(shown)));
  TEST_ASSERT_EQUAL_STRING("Hel...", shown);
  TEST_ASSERT_EQUAL_UINT16(29, ui::label_text(-1, "Hello, world", 30, 1, shown, sizeof(shown)));
  TEST_ASSERT_EQUAL_STRING("He...", shown);
  TEST_ASSERT_EQUAL_UINT16(17, ui::label_text(-1, "Hello, world", 20, 1, shown, sizeof(shown)));
  TEST_ASSERT_EQUAL_STRING("...", shown);
  TEST_ASSERT_EQUAL_UINT16(17, ui::label_text(-1, "Hello, world", 10, 1, shown, sizeof(shown)));
  TEST_ASSERT_EQUAL_STRING("...", shown);
  // A label with no text is a white box and no line.
  TEST_ASSERT_EQUAL_UINT16(0, ui::label_text(-1, "", 100, 1, shown, sizeof(shown)));
  TEST_ASSERT_EQUAL_STRING("", shown);
}

// **The line sits where the two alignments say** (docs/30, docs/90): the label is a frame of 40 by 20, the
// H is five by seven, and the three positions are the left/top corner, the centre and the right/bottom one.
// What is outside the text is the background of the label, not the page.
static void test_a_label_places_its_line_by_alignment() {
  fresh_view();
  int8_t slot = -1;
  TEST_ASSERT_TRUE(ui::create(40, 20, 0, false, slot));
  TEST_ASSERT_TRUE(ui::place(slot, 10, 10));
  TEST_ASSERT_TRUE(ui::label_draw(slot, "H", 0, 15, 0, 0, 1));  // left, top
  TEST_ASSERT_EQUAL_UINT8(0, view_level(10, 10));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(14, 10));  // the other leg of the H
  TEST_ASSERT_EQUAL_UINT8(15, view_level(15, 10));
  TEST_ASSERT_TRUE(ui::label_draw(slot, "H", 0, 15, 1, 1, 1));  // centre, middle
  TEST_ASSERT_EQUAL_UINT8(0, view_level(27, 16));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(26, 16));
  TEST_ASSERT_TRUE(ui::label_draw(slot, "H", 0, 15, 2, 2, 1));  // right, bottom
  TEST_ASSERT_EQUAL_UINT8(0, view_level(45, 23));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(44, 23));
  // The label's frame is 10,10 to 49,29: one pixel outside it is still the page, which the label has not
  // touched.
  TEST_ASSERT_EQUAL_UINT8(15, view_level(9, 9));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(50, 30));
  ui::release(slot);
}

// **A button is a rounded frame with a border and a body**, and while it is held the bevel swaps: the child
// does not move and the frame does not grow. The fill and the border are two levels of the same canvas, and a
// level outside the sixteen is refused (docs/30, docs/90).
// **The drawing of a button is PaperOS's** (0.97.0): a box in the colour of the shadow, the face on top of it, and the
// four pixels of box left at the bottom are the shadow. Held, the face goes down those four pixels and the shadow goes
// away, which is a button pushed into the page (docs/92).
static void test_a_button_sits_on_its_shadow_and_sinks_when_it_is_pressed() {
  fresh_view();
  int8_t slot = -1;
  TEST_ASSERT_TRUE(ui::create(40, 20, 0, false, slot));
  TEST_ASSERT_TRUE(ui::place(slot, 10, 10));
  TEST_ASSERT_TRUE(ui::button_draw(slot, 15, 0, false));
  // Six pixels of corner on a button twenty tall: the corner of the box is the page, the middle of the top edge is the
  // ink of the frame, and the body is paper inside it.
  TEST_ASSERT_EQUAL_UINT8(15, view_level(10, 10));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(16, 10));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(29, 10));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(10, 20));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(15, 20));
  // **The shadow is four pixels of the box under the face**: the face is sixteen tall and its frame is one pixel.
  TEST_ASSERT_EQUAL_UINT8(15, view_level(29, 24));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(29, 25));  // the frame of the face
  TEST_ASSERT_EQUAL_UINT8(0, view_level(29, 26));  // the shadow
  TEST_ASSERT_EQUAL_UINT8(0, view_level(29, 29));  // to the bottom edge of the box
  TEST_ASSERT_EQUAL_UINT8(15, view_level(9, 9));   // and nothing outside the box
  // **Held, the face is ink and it has gone down four pixels**: paper on top, ink to the bottom edge.
  TEST_ASSERT_TRUE(ui::button_draw(slot, 15, 0, true));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(29, 11));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(29, 13));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(29, 14));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(29, 29));
  TEST_ASSERT_FALSE(ui::button_draw(slot, 16, 0, false));
  ui::release(slot);
}

// **The frame of a button is opaque**: when the button paints it again, the child it covers has to paint
// again too. The button has no buffer to compose under the child, so it asks the child for a draw
// (docs/30, docs/90).
static void test_a_button_that_redraws_asks_its_child_to_draw_again() {
  fresh_view();
  int8_t button = -1;
  int8_t child = -1;
  TEST_ASSERT_TRUE(ui::create(40, 20, 0, false, button));
  TEST_ASSERT_TRUE(ui::create(10, 10, button, false, child));
  TEST_ASSERT_TRUE(ui::mark_drawn(child));
  TEST_ASSERT_FALSE(ui::needs_draw(child));
  TEST_ASSERT_TRUE(ui::button_draw(button, 15, 0, false));
  TEST_ASSERT_TRUE(ui::needs_draw(child));
  ui::release(button);
}

// **A label inside a button is a child, and the button's frame is its border too**: the label may be
// larger than the button, but what it paints stops at the frame the button gave it (docs/30, docs/90).
static void test_a_label_inside_a_button_is_clipped_to_the_button() {
  fresh_view();
  int8_t button = -1;
  int8_t label = -1;
  TEST_ASSERT_TRUE(ui::create(40, 30, 0, false, button));
  TEST_ASSERT_TRUE(ui::place(button, 10, 10));
  TEST_ASSERT_TRUE(ui::create(100, 100, button, false, label));
  TEST_ASSERT_TRUE(ui::place(label, 5, 5));
  TEST_ASSERT_TRUE(ui::button_draw(button, 15, 0, false));
  TEST_ASSERT_TRUE(ui::label_draw(label, "", 0, 0, 0, 0, 1));
  // The label's background starts at the button's 15,15 and stops at the button's 49,39.
  TEST_ASSERT_EQUAL_UINT8(0, view_level(15, 15));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(48, 38));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(50, 40));
  // **The corner of the button is round, so the pixel of its corner was never ink**: what is there is the page,
  // and the frame of three pixels does not reach two pixels in from the corner either.
  TEST_ASSERT_EQUAL_UINT8(15, view_level(10, 10));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(12, 12));
  ui::release(button);
}

// **A turn of the glass does not owe the paint of the frame before it** (docs/85): the damage of the old
// rectangle is dropped with the buffer, so a flush after a turn has nothing to paint and cannot write a
// region of the old frame into the new one.
static void test_a_turn_does_not_keep_the_damage_of_the_old_frame() {
  boot_with_a_panel();
  const uint32_t painted = glass().paint_count();
  glass().clear(0);  // a write with no flush after it: the whole page is pending
  TEST_ASSERT_TRUE(glass().set_rotation(90));
  TEST_ASSERT_EQUAL_UINT16(540, glass().width());
  TEST_ASSERT_EQUAL_UINT16(960, glass().height());
  TEST_ASSERT_TRUE(glass().flush(hal::UpdateMode::kDu));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(painted, glass().paint_count(),
                                   "a turn kept the old frame's damage and flushed it");
  // And back, so the tests after this one see the page they know.
  TEST_ASSERT_TRUE(glass().set_rotation(0));
  TEST_ASSERT_EQUAL_UINT16(960, glass().width());
  TEST_ASSERT_EQUAL_UINT16(540, glass().height());
}

// **The strip of the keys is the dial of the board**: a column of buttons whose middle is the middle key,
// up on top and down at the bottom, on the side the descriptor of the input says —the left face on the
// Paper— with the gaps between them pressing nothing. A button that presses the wrong key is a bug nobody
// sees —the light blinks and the number does not move— so the map is a function with proof of its own
// (docs/50, docs/85).
static void test_the_keys_of_the_window_are_the_dial_of_the_board() {
  // **The descriptor of the bench says what the board says** (1/11): the dial is on the right of the page and
  // the two side keys are up and down, which is what the window reads to place the buttons (docs/50).
  TEST_ASSERT_NOT_NULL(hal::platform().input());
  TEST_ASSERT_TRUE_MESSAGE(hal::platform().input()->face(0) == hal::Face::kRight,
                           "the bench's dial is not on the right of the page");
  TEST_ASSERT_TRUE(hal::platform().input()->spot(1) == hal::Spot::kUp);
  TEST_ASSERT_TRUE(hal::platform().input()->spot(2) == hal::Spot::kDown);
  TEST_ASSERT_TRUE(hal::platform().input()->spot(0) == hal::Spot::kCentre);

  // The order of the column, for the three keys of the Paper and for the one of an ATOM.
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, emu::gui_key_of_slot(3, 0), "the top of the dial is not the up key");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, emu::gui_key_of_slot(3, 1), "the middle of the dial is not the middle key");
  TEST_ASSERT_EQUAL_INT_MESSAGE(2, emu::gui_key_of_slot(3, 2), "the bottom of the dial is not the down key");
  TEST_ASSERT_EQUAL_INT(0, emu::gui_key_of_slot(1, 0));
  TEST_ASSERT_EQUAL_INT(-1, emu::gui_key_of_slot(3, 3));

  // **A column takes 52 of window** (a button of 40 and two margins of 6), a row takes 52 of height, the buttons are
  // square with 6 between: a column of three is 132 and one button is 40 —small, because the pointer of a bench is a
  // mouse and not a finger— (0.85.1, docs/85).
  TEST_ASSERT_EQUAL_UINT16(52, emu::gui_key_strip_width());
  TEST_ASSERT_EQUAL_UINT16(52, emu::gui_key_row_height());
  TEST_ASSERT_EQUAL_UINT16(40 * 3 + 6 * 2, emu::gui_keys_height(3));
  TEST_ASSERT_EQUAL_UINT16(40, emu::gui_keys_height(1));
  TEST_ASSERT_EQUAL_UINT16(0, emu::gui_keys_height(0));
  TEST_ASSERT_EQUAL_UINT16(40 * 3 + 6 * 2, emu::gui_keys_width(3));
  // And both are **centred**: the column of three in a window of 480 starts at 174 (132 + 174 + 174 = 480),
  // and the row of three in a page of 366 starts at 117 ((366 - 132) / 2).
  TEST_ASSERT_EQUAL_INT(174, emu::gui_keys_top(480, 3));
  TEST_ASSERT_EQUAL_INT(220, emu::gui_keys_top(480, 1));
  TEST_ASSERT_EQUAL_INT(117, emu::gui_keys_left(366, 3));
  TEST_ASSERT_EQUAL_INT(115, emu::gui_keys_left(270, 1));
  // A window smaller than the keys starts them at the edge instead of cutting the first button.
  TEST_ASSERT_EQUAL_INT(0, emu::gui_keys_top(100, 3));
  TEST_ASSERT_EQUAL_INT(0, emu::gui_keys_left(100, 3));

  // **A dial on the left face is a column down the left**: the strip is 0..52 and the buttons 6..46.
  const int top = emu::gui_keys_top(480, 3);
  TEST_ASSERT_EQUAL_INT(1, emu::gui_key_at(26, top + 20, 3, 366, 480, emu::KeySide::kLeft));
  TEST_ASSERT_EQUAL_INT(0, emu::gui_key_at(26, top + 40 + 6 + 20, 3, 366, 480, emu::KeySide::kLeft));
  TEST_ASSERT_EQUAL_INT(2, emu::gui_key_at(26, top + 2 * (40 + 6) + 20, 3, 366, 480, emu::KeySide::kLeft));
  // A click in a gap, on the margin, above the column, past the last button or on the page is no key at all.
  TEST_ASSERT_EQUAL_INT(-1, emu::gui_key_at(26, top + 40 + 2, 3, 366, 480, emu::KeySide::kLeft));
  TEST_ASSERT_EQUAL_INT(-1, emu::gui_key_at(3, top + 20, 3, 366, 480, emu::KeySide::kLeft));
  TEST_ASSERT_EQUAL_INT(-1, emu::gui_key_at(26, top - 1, 3, 366, 480, emu::KeySide::kLeft));
  TEST_ASSERT_EQUAL_INT(-1, emu::gui_key_at(26, top + 132, 3, 366, 480, emu::KeySide::kLeft));
  TEST_ASSERT_EQUAL_INT(-1, emu::gui_key_at(200, top + 20, 3, 366, 480, emu::KeySide::kLeft));
  // **The same dial on the right face is the same column on the other side**: the buttons are 320..360 and the left of
  // the window is the page.
  TEST_ASSERT_EQUAL_INT(1, emu::gui_key_at(340, top + 20, 3, 366, 480, emu::KeySide::kRight));
  TEST_ASSERT_EQUAL_INT(0, emu::gui_key_at(340, top + 40 + 6 + 20, 3, 366, 480, emu::KeySide::kRight));
  TEST_ASSERT_EQUAL_INT(-1, emu::gui_key_at(26, top + 20, 3, 366, 480, emu::KeySide::kRight));
  // **A key on the top face is a row under the page**: the row is the last 52 rows of the window, the buttons 40 wide
  // with 6 between, starting at 69 of a 270-wide window (132 in the middle).
  const int row_top = 550 - 52 + 6;
  TEST_ASSERT_EQUAL_INT(1, emu::gui_key_at(89, row_top + 20, 3, 270, 550, emu::KeySide::kBottom));
  TEST_ASSERT_EQUAL_INT(0, emu::gui_key_at(135, row_top + 20, 3, 270, 550, emu::KeySide::kBottom));
  TEST_ASSERT_EQUAL_INT(2, emu::gui_key_at(181, row_top + 20, 3, 270, 550, emu::KeySide::kBottom));
  TEST_ASSERT_EQUAL_INT(-1, emu::gui_key_at(111, row_top + 20, 3, 270, 550, emu::KeySide::kBottom));
  TEST_ASSERT_EQUAL_INT(-1, emu::gui_key_at(89, 545, 3, 270, 550, emu::KeySide::kBottom));
  TEST_ASSERT_EQUAL_INT(-1, emu::gui_key_at(0, row_top + 20, 3, 270, 550, emu::KeySide::kBottom));
  // A board of one key has its button in the middle of the window, and nothing under it.
  TEST_ASSERT_EQUAL_INT(0, emu::gui_key_at(26, 240, 1, 366, 480, emu::KeySide::kLeft));

  // **The sign of a key, as runs of one pixel** (0.85.1): the three of the dial are an up arrow, a full circle and a
  // down arrow, and a board of one button has no sign —its key is the whole board and it keeps its word—.
  emu::KeyRun runs[emu::kKeyIconRuns];
  TEST_ASSERT_EQUAL_UINT8(0, emu::gui_key_icon(1, 0, 40, 40, runs, emu::kKeyIconRuns));
  const uint8_t up = emu::gui_key_icon(3, 1, 40, 40, runs, emu::kKeyIconRuns);
  TEST_ASSERT_GREATER_THAN(0, up);
  // The head of an arrow is its narrowest row on top and the widest under it, and the stem is under the widest.
  TEST_ASSERT_EQUAL_UINT16(1, runs[0].w);
  TEST_ASSERT_GREATER_THAN(runs[0].w, runs[up - 2].w);
  // Centred in the key: the tip is one pixel on the middle column, which is twenty of forty.
  TEST_ASSERT_EQUAL_UINT16(20, runs[0].x);
  TEST_ASSERT_EQUAL_UINT16(5, runs[up - 1].w);  // the stem: forty over eight, made odd
  // The arrow pointing down is the same shape the other way up, and its stem is over its head.
  const uint8_t down = emu::gui_key_icon(3, 2, 40, 40, runs, emu::kKeyIconRuns);
  TEST_ASSERT_EQUAL_UINT8(up, down);
  const uint16_t stem_row = runs[down - 1].y;
  TEST_ASSERT_LESS_THAN(runs[down - 2].y, stem_row);
  // **The middle is a filled circle**: its rows grow from one pixel to the diameter and go back, and the row of the
  // middle is the widest of them.
  const uint8_t disc = emu::gui_key_icon(3, 0, 40, 40, runs, emu::kKeyIconRuns);
  TEST_ASSERT_GREATER_THAN(1, disc);
  TEST_ASSERT_EQUAL_UINT16(1, runs[0].w);
  TEST_ASSERT_EQUAL_UINT16(1, runs[disc - 1].w);
  TEST_ASSERT_EQUAL_UINT16(2 * (40 / 5) + 1, runs[disc / 2].w);
  TEST_ASSERT_EQUAL_UINT16(20, runs[disc / 2].y);
  TEST_ASSERT_EQUAL_INT(-1, emu::gui_key_at(48, 400, 1, 366, 480, emu::KeySide::kLeft));
}

// **The keyboard of the system is a QWERTY that the finger and the glass agree on** (0.83.0): the band is four
// rows of a key and never more than two fifths of the view, a letter is the tenth of the width a finger hits, the
// row of `a` is staggered half a key to the right of the row of `q`, the space is a bar of six tenths where a thumb
// looks for it, and the shift changes the case of the letters and lets go after one of them (docs/90).
static void test_the_keyboard_is_a_qwerty_the_finger_and_the_glass_agree_on() {
  fresh_view();
  TEST_ASSERT_TRUE(ui::keyboard_show(ui::kKeyboardAlpha));
  TEST_ASSERT_TRUE(ui::keyboard_visible());
  // Four rows of forty-four pixels in a view of five hundred and forty, and never the two fifths the rule allows.
  TEST_ASSERT_EQUAL_UINT16(176, ui::keyboard_height());
  TEST_ASSERT_LESS_OR_EQUAL_UINT16(static_cast<uint16_t>((ui::canvas().height * 2) / 5), ui::keyboard_height());
  // **The band is not part of the view an app has** (0.84.0): the canvas is still the five hundred and forty of the
  // glass under the bar, and `ui.height()` —what `ui.caps().height` says to an app— is what is left over the band.
  const uint16_t top = static_cast<uint16_t>(ui::canvas().height - ui::keyboard_height());
  TEST_ASSERT_EQUAL_UINT16(364, top);
  TEST_ASSERT_EQUAL_UINT16(540, ui::canvas().height);
  TEST_ASSERT_EQUAL_UINT16(364, ui::height());
  TEST_ASSERT_TRUE(ui::present());
  // **The keys keep the margin of the glass** (0.97.0, docs/92): the twelve pixels at the left of the band are the
  // paper of the band and not a key, and the first key of the row starts there with its one pixel of line.
  TEST_ASSERT_EQUAL_UINT8(15, view_level(0, top + 10));   // the margin of the glass, at the left
  TEST_ASSERT_EQUAL_UINT8(15, view_level(11, top + 10));  // eleven pixels of it
  TEST_ASSERT_EQUAL_UINT8(0, view_level(12, top + 10));   // and the key of the row starts here, in ink

  // **The page above the band is the page's**: a finger there is not the keyboard's.
  TEST_ASSERT_FALSE(ui::keyboard_touch(48, top - 1, true));
  // **A present with nothing pressed does not paint the band** (0.83.1): it is painted on the canvas because it is the
  // top layer, and what reaches the glass is only what changed of it —a keyboard that marks its nine thousand pixels on
  // every present is a keyboard that flashes on every blink of the caret of the box it types into— (docs/50, docs/90).
  const uint32_t painted = glass().paint_count();
  TEST_ASSERT_TRUE(ui::present());
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(painted, glass().paint_count(), "a present with nothing pressed painted the band");
  // And a key is the dial's too: it walks the first row before anything else, and the centre presses what it is on.
  TEST_ASSERT_TRUE(ui::keyboard_key("press", 2));
  TEST_ASSERT_TRUE(ui::present());

  // **The row of `a` is staggered half a key**: the first twentieth of it is air, so a finger there is a touch of
  // the band that presses nothing and the dial stays where it was —on the second key of the first row—.
  TEST_ASSERT_TRUE(ui::keyboard_touch(20, top + 44 + 22, true));
  ui::keyboard_touch(0, 0, false);
  TEST_ASSERT_TRUE(ui::present());
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, view_level(150, top + 10), "the dial left the key it was on");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(15, view_level(40, top + 10), "a key of the first row is still the chosen one");
  // A finger on the `a` itself puts the dial there, which is the cell the paint put down: the keys keep the twelve
  // pixels of margin of the glass, so the row of `a` starts at thirty-seven, and **the chosen key is its whole cell in
  // ink** and not a block inside three pixels of air (0.97.0, docs/92).
  TEST_ASSERT_TRUE(ui::keyboard_touch(100, top + 41 + 22, true));
  ui::keyboard_touch(0, 0, false);
  TEST_ASSERT_TRUE(ui::present());
  TEST_ASSERT_EQUAL_UINT8(0, view_level(70, top + 41 + 4));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(150, top + 10));

  // **The capitals are a lock and not a shift**: what a capital costs is more ink in the cell of the letter and the
  // same cell, the key of the case is lit while it is on, and **a letter does not put it out** —the picture of the
  // band is the same after a letter, which is the other half of "the keyboard does not repaint on a key"- (docs/30,
  // docs/90).
  const auto ink_in = [&](uint16_t x0, uint16_t y0) {
    int ink = 0;
    for (uint16_t x = x0; x < x0 + 96; ++x) {
      for (uint16_t y = y0; y < y0 + 36; ++y) {
        if (view_level(x, y) == 0) ++ink;
      }
    }
    return ink;
  };
  const int lower = ink_in(192, top + 4);
  TEST_ASSERT_GREATER_THAN(0, lower);
  // The case is the first key of the third row: three twentieths of width, a hundred and forty-four pixels.
  TEST_ASSERT_TRUE(ui::keyboard_touch(72, top + 82 + 20, true));
  ui::keyboard_touch(0, 0, false);
  TEST_ASSERT_TRUE(ui::present());
  const int capital = ink_in(192, top + 4);
  TEST_ASSERT_GREATER_THAN_MESSAGE(lower, capital, "the case did not change the letters");
  // The key is lit while it is on, and the band is painted once for it.
  TEST_ASSERT_EQUAL_UINT8(0, view_level(20, top + 82 + 10));
  // **A letter of the row of `q` does not put it out**, and it does not paint the band either: the capitals stay and
  // the key stays lit —what a lock is for— (0.84.4).
  // The dial walks off the case key and onto a letter —the `z` of its row—, which is a move and paints the band once.
  TEST_ASSERT_TRUE(ui::keyboard_key("press", 2));
  TEST_ASSERT_TRUE(ui::present());
  // **And the centre presses that letter**, which is a letter and not a move: the band's picture is the same one, so the
  // glass is not refreshed for it (0.84.4).
  const uint32_t before_the_letter = glass().paint_count();
  TEST_ASSERT_TRUE(ui::keyboard_key("press", 0));
  TEST_ASSERT_TRUE(ui::present());
  TEST_ASSERT_EQUAL_INT_MESSAGE(capital, ink_in(192, top + 4), "a letter put the capitals out");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, view_level(20, top + 82 + 10), "the key of the case went out with a letter");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(before_the_letter, glass().paint_count(), "a letter painted the band");
  // And the key again takes the capitals back.
  TEST_ASSERT_TRUE(ui::keyboard_touch(72, top + 88 + 22, true));
  ui::keyboard_touch(0, 0, false);
  TEST_ASSERT_TRUE(ui::present());
  TEST_ASSERT_EQUAL_INT_MESSAGE(lower, ink_in(192, top + 4), "the lock did not turn the capitals off");

  // **The band is a picture drawn into the page, and a page that is wiped takes it with it** (0.84.4): `ui.clear` says
  // so and the next present draws it again —without that, a page that starts again would leave a hole where the
  // keyboard was, and the app would have to know that the band is not its own— (docs/50, docs/90).
  TEST_ASSERT_EQUAL_UINT8(0, view_level(0, top));
  TEST_ASSERT_TRUE(ui::clear(15, nullptr));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(0, top));
  TEST_ASSERT_TRUE(ui::present());
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, view_level(0, top), "the band did not come back after a clear");
  // The same with a rectangle the app paints over it, which is the other way a page wipes the band.
  TEST_ASSERT_TRUE(ui::fill_rect(0, static_cast<uint16_t>(top), 540, 176, 15, nullptr));
  TEST_ASSERT_TRUE(ui::present());
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, view_level(0, top), "the band did not come back after a rectangle");

  // **The walk of the dial is one paint of the cells it moves between** (0.83.1), and the rest of the band is left alone.
  const uint32_t walked = glass().paint_count();
  TEST_ASSERT_TRUE(ui::keyboard_key("press", 2));
  TEST_ASSERT_TRUE(ui::present());
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(walked + 1, glass().paint_count(), "a walk of the dial painted more than once");

  // **The digits are the other alphabet and the same band**: four rows, because a page laid out around a band of
  // a hundred and seventy-six does not move when the digits come (docs/90).
  TEST_ASSERT_TRUE(ui::keyboard_show(ui::kKeyboardNumeric));
  TEST_ASSERT_EQUAL_UINT16(176, ui::keyboard_height());
  TEST_ASSERT_TRUE(ui::present());
  // The `1` of the first row is ink and the band is still at the bottom of the canvas.
  const uint16_t foot = static_cast<uint16_t>(ui::canvas().height - ui::keyboard_height());
  TEST_ASSERT_EQUAL_UINT16(364, foot);
  TEST_ASSERT_EQUAL_UINT8(0, view_level(0, foot));
  // The dial is on the `1` of the first row, and the chosen key is a block of ink with the digit in paper.
  TEST_ASSERT_EQUAL_UINT8(0, view_level(20, foot + 10));
  TEST_ASSERT_TRUE(ui::keyboard_hide());
  TEST_ASSERT_EQUAL_UINT16(0, ui::keyboard_height());
  // **And the page is the whole glass again** when the band goes.
  TEST_ASSERT_EQUAL_UINT16(540, ui::height());
  TEST_ASSERT_FALSE(ui::keyboard_touch(48, static_cast<uint16_t>(ui::height() - 1), true));
}

// **The system chooses the wash between two apps, and it chooses it from what the page showed** (docs/50):
// black and white and nothing else is a page the two-level wash takes away in 290 ms, a few grays take the
// sixteen-level wash without the black flash, and a photograph —or a page that has taken many quick paints,
// because what is left is ghost— takes the full wash in 450. No app says which one: it cannot know what the
// one before it left.
static void test_the_cleaner_between_apps_follows_the_page() {
  const uint16_t text = static_cast<uint16_t>((1u << 0) | (1u << 15));
  TEST_ASSERT_TRUE_MESSAGE(hal::UpdateMode::kA2 == kernel::ui::cleaner_for(text, 3),
                           "a page of text does not take the two-level wash");
  // A mid grey is no longer a page of text, and a pair of them is still few grays.
  TEST_ASSERT_TRUE(hal::UpdateMode::kGl16 == kernel::ui::cleaner_for(text | (1u << 8) | (1u << 4), 3));
  TEST_ASSERT_TRUE_MESSAGE(hal::UpdateMode::kGc16 == kernel::ui::cleaner_for(0xFFFF, 3),
                           "a photograph does not take the full wash");
  // **Many quick paints leave ghost, and only a full wash takes ghost away.**
  TEST_ASSERT_TRUE(hal::UpdateMode::kA2 == kernel::ui::cleaner_for(text, 31));
  TEST_ASSERT_TRUE_MESSAGE(hal::UpdateMode::kGc16 == kernel::ui::cleaner_for(text, 32),
                           "a page of text that has taken many quick paints needs the full wash");
  // Nothing painted is nothing to clean.
  TEST_ASSERT_TRUE(hal::UpdateMode::kDu == kernel::ui::cleaner_for(0, 0));
}

void test_a_checkbox_is_a_box_with_a_tick_and_a_ring() {
  fresh_view();
  int8_t box = -1;
  TEST_ASSERT_TRUE(ui::create(60, 20, 0, false, box));
  TEST_ASSERT_TRUE(ui::place(box, 10, 10));
  // **Twelve pixels of box in a row of twenty**: the square on the left is the control and the rest of the row is the
  // room the child that names the setting needs.
  TEST_ASSERT_EQUAL_INT16(12, ui::checkbox_side(box));

  // **Out: a ring of two pixels and paper inside it** (the paper project's `Checkbox::paintBox()`, docs/92). The row
  // here is 12 so the row is told apart from the paper of the page, and the ring is ink.
  TEST_ASSERT_TRUE(ui::checkbox_draw(box, 12, 0, 0, false, false, false));
  TEST_ASSERT_EQUAL_UINT8(12, view_level(40, 20));  // the row, right of the box: the room for the words
  TEST_ASSERT_EQUAL_UINT8(0, view_level(10, 20));   // the ring, two pixels of it
  TEST_ASSERT_EQUAL_UINT8(0, view_level(11, 20));
  TEST_ASSERT_EQUAL_UINT8(12, view_level(12, 20));  // and the body, which is the row's own tone and has nothing in it
  TEST_ASSERT_EQUAL_UINT8(12, view_level(15, 23));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(9, 9));    // and the page outside the row

  // **Checked, the body has a tick in it and the row does not move**: the knee of the V lands on that same pixel, and
  // the row outside the box is still the row.
  TEST_ASSERT_TRUE(ui::checkbox_draw(box, 12, 0, 0, true, false, false));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(14, 23));
  TEST_ASSERT_EQUAL_UINT8(12, view_level(40, 20));

  // **The tick has a colour of its own**: a black box with a white tick is a checkbox that can be read with the rest of
  // the page black (docs/30, docs/90).
  TEST_ASSERT_TRUE(ui::checkbox_draw(box, 0, 15, 15, true, false, false));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(14, 23));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(10, 20));  // the ring is the other colour and it did not move

  // **A checkbox has no pressed state** (the paper project says it in as many words): the picture of a box under a
  // finger is the picture of a box, because what the finger does is change the state and the state is the mark.
  TEST_ASSERT_TRUE(ui::checkbox_draw(box, 12, 0, 0, false, true, false));
  TEST_ASSERT_EQUAL_UINT8(12, view_level(15, 20));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(10, 20));

  // **The chosen box goes inverted** —ring of row, body of ink, and the tick of row—, which is the same signal a button
  // gives and the one thing a person can see on this glass. **The word is a child of the app and this system does not
  // recolour it** (docs/92).
  TEST_ASSERT_TRUE(ui::checkbox_draw(box, 12, 0, 0, false, false, false));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(10, 20));   // the ring of a box that is not chosen is ink
  TEST_ASSERT_TRUE(ui::checkbox_draw(box, 12, 0, 0, false, false, true));
  TEST_ASSERT_EQUAL_UINT8(12, view_level(10, 20));  // the ring is the row now
  TEST_ASSERT_EQUAL_UINT8(0, view_level(15, 20));   // and the body is ink
  TEST_ASSERT_TRUE(ui::checkbox_draw(box, 12, 0, 0, true, false, true));
  TEST_ASSERT_EQUAL_UINT8(12, view_level(14, 23));  // the tick, in the tone of the ring

  // A level outside the sixteen is a colour this panel does not have, and the box is asked for three of them.
  TEST_ASSERT_FALSE(ui::checkbox_draw(box, 16, 0, 0, false, false, false));
  TEST_ASSERT_FALSE(ui::checkbox_draw(box, 15, 16, 0, false, false, false));
  TEST_ASSERT_FALSE(ui::checkbox_draw(box, 15, 0, 16, false, false, false));
  ui::release(box);
}

// **A bar is a track and a prefix of it**: the fill does not reach the end until the value does, the frame is
// one pixel of the border colour, and a value of minus one is "nothing to say yet" —the track alone— because
// a page that shows a full bar before it knows anything is a page that lies (docs/30, docs/90).
void test_a_progress_bar_fills_a_prefix_of_its_track() {
  fresh_view();
  int8_t bar = -1;
  TEST_ASSERT_TRUE(ui::create(100, 16, 0, false, bar));
  TEST_ASSERT_TRUE(ui::place(bar, 10, 10));

  // Half: the left of the track is the fill and the right of it is still the track.
  TEST_ASSERT_TRUE(ui::progress_draw(bar, 15, 0, 12, 0, 50));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(20, 18));    // the fill, on the left
  TEST_ASSERT_EQUAL_UINT8(12, view_level(100, 18));  // the track, on the right
  TEST_ASSERT_EQUAL_UINT8(0, view_level(10, 18));    // the frame, one pixel
  TEST_ASSERT_EQUAL_UINT8(15, view_level(9, 9));     // and the page outside it

  // **Nothing to say**: the track is drawn and nothing is filled.
  TEST_ASSERT_TRUE(ui::progress_draw(bar, 15, 0, 12, 0, -1));
  TEST_ASSERT_EQUAL_UINT8(12, view_level(20, 18));

  // **A bar that says something always shows something**: one per cent of a wide bar is still a rounded end
  // and not a pixel of nothing.
  TEST_ASSERT_TRUE(ui::progress_draw(bar, 15, 0, 12, 0, 1));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(15, 18));

  // A level outside the sixteen is a colour this panel does not have, and the bar is asked for four of them.
  TEST_ASSERT_FALSE(ui::progress_draw(bar, 16, 0, 12, 0, 50));
  TEST_ASSERT_FALSE(ui::progress_draw(bar, 15, 16, 12, 0, 50));
  TEST_ASSERT_FALSE(ui::progress_draw(bar, 15, 0, 16, 0, 50));
  TEST_ASSERT_FALSE(ui::progress_draw(bar, 15, 0, 12, 16, 50));
  ui::release(bar);
}

// **The knob sits where the value says and the number is in the right column**: the track runs from the middle
// of where the knob goes at the minimum to the middle of where it goes at the maximum, so a knob at either end
// is inside the frame and not half outside it (docs/30, docs/90).
void test_a_slider_puts_the_knob_where_the_value_says() {
  fresh_view();
  int8_t slider = -1;
  TEST_ASSERT_TRUE(ui::create(200, 40, 0, false, slider));
  TEST_ASSERT_TRUE(ui::place(slider, 10, 10));

  // **At the minimum the knob is a circle at the left** —paper inside and one pixel of ink round it, which is the paper
  // project's `Slider`— and the tube of the track carries on to the right, empty.
  TEST_ASSERT_TRUE(ui::slider_draw(slider, 15, 0, 15, 0, 0, 0, 0, 100, "%", 2, false, false));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(10, 30));    // the outline of the knob, on its left edge
  TEST_ASSERT_EQUAL_UINT8(15, view_level(20, 30));   // and the paper inside the knob
  TEST_ASSERT_EQUAL_UINT8(15, view_level(100, 26));  // the empty track is paper, with its outline above and below
  TEST_ASSERT_EQUAL_UINT8(15, view_level(100, 30));

  // At the maximum the knob has gone to the right and the track it left behind is filled: where the knob was, the paper of
  // the control is, because the track starts at the middle of the knob at the minimum.
  TEST_ASSERT_TRUE(ui::slider_draw(slider, 15, 0, 15, 0, 0, 100, 0, 100, "%", 2, false, false));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(100, 30));   // what the value has taken is the ink of the outline
  TEST_ASSERT_EQUAL_UINT8(15, view_level(10, 30));   // and where the knob was, the paper of the control is

  // **The number is drawn by the control and not by the app**: there is ink in the right column, and the
  // column is walked instead of pointed at, because a letter of a face is not a pixel a test should know.
  int ink = 0;
  for (uint16_t y = 20; y < 42; ++y) {
    for (uint16_t x = 168; x < 208; ++x) {
      if (view_level(x, y) == 0) ++ink;
    }
  }
  TEST_ASSERT_GREATER_THAN(0, ink);

  // The unit travels with the number: without the `%` the same value is not the same page.
  TEST_ASSERT_TRUE(ui::slider_draw(slider, 15, 0, 12, 8, 0, 0, 0, 100, "", 2, false, false));
  int bare = 0;
  for (uint16_t y = 20; y < 42; ++y) {
    for (uint16_t x = 168; x < 208; ++x) {
      if (view_level(x, y) == 0) ++bare;
    }
  }
  TEST_ASSERT_LESS_THAN(ink, bare);

  // A level outside the sixteen and a scale of nothing are not a slider.
  TEST_ASSERT_FALSE(ui::slider_draw(slider, 16, 0, 12, 8, 0, 0, 0, 100, "%", 2, false, false));
  TEST_ASSERT_FALSE(ui::slider_draw(slider, 15, 0, 12, 8, 16, 0, 0, 100, "%", 2, false, false));
  ui::release(slider);
}

// **A row is one line and the two columns of it**: the title on the left, the detail on the right, the line of
// the separator under it, and a **marked** row is inverted —ink and paper swapped— because that is what "this
// is the one" says on a glass of sixteen levels (docs/30, docs/90).
void test_a_row_puts_its_detail_on_the_right_and_inverts_when_it_is_marked() {
  fresh_view();
  int8_t row = -1;
  TEST_ASSERT_TRUE(ui::create(240, 40, 0, false, row));
  TEST_ASSERT_TRUE(ui::place(row, 10, 10));

  TEST_ASSERT_TRUE(ui::row_draw(row, "Hola", "12 KB", "", 0, 8, 15, 2, false, true));
  // The row is paper, and the line under it is the dim colour and one pixel of it.
  TEST_ASSERT_EQUAL_UINT8(15, view_level(12, 12));
  TEST_ASSERT_EQUAL_UINT8(8, view_level(100, 49));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(100, 45));

  // The title is on the left, the detail on the right, and the air between the two columns has no ink at all.
  int left = 0;
  for (uint16_t y = 10; y < 50; ++y) {
    for (uint16_t x = 18; x < 120; ++x) {
      if (view_level(x, y) == 0) ++left;
    }
  }
  TEST_ASSERT_GREATER_THAN(0, left);
  int middle = 0;
  for (uint16_t y = 20; y < 40; ++y) {
    for (uint16_t x = 120; x < 180; ++x) {
      if (view_level(x, y) == 0) ++middle;
    }
  }
  TEST_ASSERT_EQUAL_INT(0, middle);
  int right = 0;
  for (uint16_t y = 20; y < 40; ++y) {
    for (uint16_t x = 180; x < 248; ++x) {
      // **The detail is the dim tone**: it is the second voice of the row and the two are told apart by the tone.
      if (view_level(x, y) == 8) ++right;
    }
  }
  TEST_ASSERT_GREATER_THAN(0, right);

  // **Marked, the row is ink and the words are paper**, and the separator is not asked for this time.
  TEST_ASSERT_TRUE(ui::row_draw(row, "Hola", "12 KB", "", 0, 8, 15, 2, true, false));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(12, 12));
  ui::release(row);
}

// **The row of a checkbox is opaque too**: the child that names the setting is drawn after the control, so a
// checkbox that paints its frame again has to ask it to draw again, exactly as a button does (docs/30,
// docs/90).
void test_a_checkbox_that_redraws_asks_its_child_to_draw_again() {
  fresh_view();
  int8_t box = -1;
  int8_t label = -1;
  TEST_ASSERT_TRUE(ui::create(200, 30, 0, false, box));
  TEST_ASSERT_TRUE(ui::create(150, 20, box, false, label));
  TEST_ASSERT_TRUE(ui::mark_drawn(label));
  TEST_ASSERT_FALSE(ui::needs_draw(label));
  TEST_ASSERT_TRUE(ui::checkbox_draw(box, 15, 0, 0, true, false, false));
  TEST_ASSERT_TRUE(ui::needs_draw(label));
  ui::release(box);
}

// **A panel is a declaration and not a list of places**: a column stacks its children with the gap between them,
// stretches each one to the width of the panel, and the room that is left is split by weight with the last one
// taking the remainder (docs/30, docs/90).
void test_a_panel_stacks_its_children_in_a_column() {
  fresh_view();
  int8_t panel = -1;
  int8_t first = -1;
  int8_t second = -1;
  int8_t third = -1;
  TEST_ASSERT_TRUE(ui::create(200, 100, 0, false, panel));
  TEST_ASSERT_TRUE(ui::place(panel, 10, 10));
  TEST_ASSERT_TRUE(ui::create(40, 40, panel, false, first));
  TEST_ASSERT_TRUE(ui::create(40, 40, panel, false, second));
  TEST_ASSERT_TRUE(ui::create(40, 40, panel, false, third));

  const ui::PanelChild kids[3] = {{first, 0}, {second, 1}, {third, 2}};
  // 100 of height, twenty of padding and two gaps of four leave 72: the first child keeps its 40 and the other
  // 32 are split one to three —ten for the second and the rest for the last— and nothing is left over.
  TEST_ASSERT_EQUAL_UINT8(3, ui::panel_layout(panel, ui::PanelFlow::kVertical, 10, 4, kids, 3));
  const ui::Canvas* one = ui::at(first);
  const ui::Canvas* two = ui::at(second);
  const ui::Canvas* three = ui::at(third);
  TEST_ASSERT_NOT_NULL(one);
  TEST_ASSERT_NOT_NULL(two);
  TEST_ASSERT_NOT_NULL(three);
  TEST_ASSERT_EQUAL_UINT16(180, one->width);
  TEST_ASSERT_EQUAL_UINT16(40, one->height);
  TEST_ASSERT_EQUAL_UINT16(10, one->y);
  TEST_ASSERT_EQUAL_UINT16(54, two->y);
  TEST_ASSERT_EQUAL_UINT16(10, two->height);
  TEST_ASSERT_EQUAL_UINT16(68, three->y);
  TEST_ASSERT_EQUAL_UINT16(22, three->height);

  // **A child that is not this panel's is not arranged by it**: moving one to another room and laying the panel out again
  // leaves two children, and the room the third one had is taken by the second, which is the only one with a weight —so no
  // hole is left where nobody is, which is what the panel is for.
  TEST_ASSERT_TRUE(ui::reparent(third, 0));
  ui::panel_layout(panel, ui::PanelFlow::kVertical, 10, 4, kids, 3);
  TEST_ASSERT_EQUAL_UINT16(54, two->y);
  TEST_ASSERT_EQUAL_UINT16(36, two->height);

  ui::release(first);
  ui::release(second);
  ui::release(third);
  ui::release(panel);
}

// **A row panel splits the width and stretches the height**: the same arithmetic along the other axis, which is
// what a bar of buttons and a strip of tabs are (docs/30, docs/90).
void test_a_row_panel_splits_the_width_and_stretches_the_height() {
  fresh_view();
  int8_t panel = -1;
  int8_t one = -1;
  int8_t two = -1;
  TEST_ASSERT_TRUE(ui::create(200, 60, 0, false, panel));
  TEST_ASSERT_TRUE(ui::create(50, 20, panel, false, one));
  TEST_ASSERT_TRUE(ui::create(50, 20, panel, false, two));

  const ui::PanelChild kids[2] = {{one, 1}, {two, 1}};
  // 200 of width and one gap of ten leave 190, which is 95 each: the last takes the remainder and both are as
  // tall as the panel.
  TEST_ASSERT_EQUAL_UINT8(2, ui::panel_layout(panel, ui::PanelFlow::kHorizontal, 0, 10, kids, 2));
  const ui::Canvas* left = ui::at(one);
  const ui::Canvas* right = ui::at(two);
  TEST_ASSERT_NOT_NULL(left);
  TEST_ASSERT_NOT_NULL(right);
  TEST_ASSERT_EQUAL_UINT16(0, left->x);
  TEST_ASSERT_EQUAL_UINT16(95, left->width);
  TEST_ASSERT_EQUAL_UINT16(60, left->height);
  TEST_ASSERT_EQUAL_UINT16(105, right->x);
  TEST_ASSERT_EQUAL_UINT16(95, right->width);

  ui::release(one);
  ui::release(two);
  ui::release(panel);
}

// **A strip of tabs says "you are here" by taking the line away**: on two tones there is no bold and no colour,
// so the chosen tab is the one with nothing under it (docs/30, docs/90).
void test_a_strip_of_tabs_has_no_line_under_the_chosen_one() {
  fresh_view();
  int8_t strip = -1;
  TEST_ASSERT_TRUE(ui::create(300, 40, 0, false, strip));
  TEST_ASSERT_TRUE(ui::place(strip, 0, 0));
  const char* labels[3] = {"One", "Two", "Three"};

  // The second is the chosen one: the line of two pixels is under the first and under the third and it is not
  // under the second.
  TEST_ASSERT_TRUE(ui::tabs_draw(strip, labels, 3, 2, 0, 15, 1));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(50, 39));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(150, 39));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(250, 39));
  // And the words are above the line: each column has ink of its own.
  int ink = 0;
  for (uint16_t y = 0; y < 36; ++y) {
    for (uint16_t x = 100; x < 200; ++x) {
      if (view_level(x, y) == 0) ++ink;
    }
  }
  TEST_ASSERT_GREATER_THAN(0, ink);

  // Index 0 is "none chosen" and leaves the line whole, which is what a strip that has not been used yet says.
  TEST_ASSERT_TRUE(ui::tabs_draw(strip, labels, 3, 0, 0, 15, 1));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(150, 39));
  TEST_ASSERT_FALSE(ui::tabs_draw(strip, nullptr, 3, 2, 0, 15, 1));
  ui::release(strip);
}

// **A button is the drawing of the paper project, number by number** (0.97.0): a box in the colour of its shadow with
// the face lifted **four pixels** off the bottom —the strip of box in sight under it is what makes it a key and not a
// rectangle—, a corner of fourteen, and **the chosen one inverted**: ink face, because the focus and the finger are one
// signal. With the finger on it the face slides down those four pixels and the strip under it is gone, so what is in
// sight at the top is paper: that half of "pressed" is what a bevel cannot say. A primary button wears a frame of three
// pixels on the face, and the frame walks with the face and does not stay on the box (docs/30, docs/90, docs/92).
void test_a_button_is_the_drawing_of_the_paper_project(void) {
  fresh_view();
  int8_t box = -1;
  TEST_ASSERT_TRUE(ui::create(200, 64, 0, false, box));
  TEST_ASSERT_TRUE(ui::place(box, 0, 0));

  // Up and not chosen: the face is paper and the four pixels of box under it are the shadow, of ink.
  TEST_ASSERT_TRUE(ui::button_draw(box, 15, 0, false, false));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(100, 0));    // the frame of one pixel, on the top edge of the face
  TEST_ASSERT_EQUAL_UINT8(15, view_level(100, 1));   // the face, paper
  TEST_ASSERT_EQUAL_UINT8(15, view_level(100, 30));  // and paper all the way down the face
  TEST_ASSERT_EQUAL_UINT8(0, view_level(100, 59));   // the frame of the bottom edge of the face
  TEST_ASSERT_EQUAL_UINT8(0, view_level(100, 61));   // the strip of the shadow, the box under the face
  TEST_ASSERT_EQUAL_UINT8(0, view_level(100, 63));   // to the bottom edge of the box

  // **With the finger on it the face has gone down four pixels and it is ink**: paper above it and ink below, which is
  // the button pushed into the page.
  TEST_ASSERT_TRUE(ui::button_draw(box, 15, 0, true, false));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(100, 2));   // paper where the frame was
  TEST_ASSERT_EQUAL_UINT8(15, view_level(100, 3));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(100, 4));    // the ink face, four pixels down
  TEST_ASSERT_EQUAL_UINT8(0, view_level(100, 63));   // and ink to the bottom edge: there is no shadow while it is down

  // **The chosen one is inverted and has no strip**: the box and the face are both ink, so the whole button is ink.
  TEST_ASSERT_TRUE(ui::button_draw(box, 15, 0, false, true));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(100, 0));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(100, 30));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(100, 63));

  // **A primary button is the one with a frame of three pixels**, on the face and not on the box.
  TEST_ASSERT_TRUE(ui::button_draw(box, 15, 0, false, false, true));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(100, 0));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(100, 1));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(100, 2));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(100, 3));   // the face starts here

  ui::release(box);
}

// **The dots of a page are a strip with the one you are on bigger**, and **a page with one page draws none**:
// one dot says nothing and it would cost a paint of the panel to say it (docs/30, docs/90).
void test_the_dots_of_a_page_draw_nothing_when_there_is_one_page() {
  fresh_view();
  int8_t strip = -1;
  TEST_ASSERT_TRUE(ui::create(100, 20, 0, false, strip));
  TEST_ASSERT_TRUE(ui::place(strip, 0, 0));

  TEST_ASSERT_TRUE(ui::dots_draw(strip, 1, 1, 0, 15));
  int none = 0;
  for (uint16_t y = 0; y < 20; ++y) {
    for (uint16_t x = 0; x < 100; ++x) {
      if (view_level(x, y) == 0) ++none;
    }
  }
  TEST_ASSERT_EQUAL_INT(0, none);

  // Three pages and the second one: three dots, the middle one bigger, and all of them in the middle of the
  // strip.
  ui::clear(15, nullptr);
  TEST_ASSERT_TRUE(ui::dots_draw(strip, 3, 2, 0, 15));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(50, 10));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(50, 5));   // the big one reaches higher than the others
  TEST_ASSERT_EQUAL_UINT8(15, view_level(2, 10));  // and the strip is not painted from edge to edge
  ui::release(strip);
}

// **The strip is two tones and a rod, and the open page is the solid ball** (0.94.1): the balls are rings with paper
// inside and the one being read is a solid disc and bigger, so the counter says "which one" without a grey —and a strip
// of two tones is one the panel erases with the binary waveform (docs/50, docs/90).
void test_the_dots_of_a_page_are_rings_on_a_rod_and_the_open_one_is_solid(void) {
  fresh_view();
  int8_t strip = -1;
  TEST_ASSERT_TRUE(ui::create(100, 24, 0, false, strip));
  TEST_ASSERT_TRUE(ui::place(strip, 0, 0));

  // Three pages and the second one in a box of 100 by 24: balls of 8 and 12 pixels, a gap of 8 and a rod of 2. The
  // strip is 52 wide and starts at column 24, so the centres are at 30, 50 and 70 and the row of the centres is 12.
  TEST_ASSERT_TRUE(ui::dots_draw(strip, 3, 2, 0, 15));

  // The page that is open: a solid disc, ink from its top row to its bottom one.
  TEST_ASSERT_EQUAL_UINT8(0, view_level(50, 12));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(50, 7));   // the top of the big ball, six pixels above the centre
  // The others: ink at the edge and paper in the middle, which is the whole difference between a ring and a ball.
  TEST_ASSERT_EQUAL_UINT8(0, view_level(30, 8));   // the top edge of the small ball
  TEST_ASSERT_EQUAL_UINT8(15, view_level(30, 12));  // paper inside the ring
  // And the rod: ink in the air between two balls, on the row of their centres.
  TEST_ASSERT_EQUAL_UINT8(0, view_level(40, 12));
  // Nothing is painted outside the strip.
  TEST_ASSERT_EQUAL_UINT8(15, view_level(2, 12));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(97, 12));
  ui::release(strip);
}

// **More pages than fit are a window and not a strip running off the edge** (0.94.1): forty pages in a strip of 100 px
// used to be drawn from outside the box, so the counter left the glass on both sides. It shows the five balls that fit
// and the window of them that holds the open page, and the open one is still the solid ball of the strip.
void test_the_dots_of_a_page_keep_the_open_one_inside_a_narrow_strip(void) {
  fresh_view();
  int8_t strip = -1;
  TEST_ASSERT_TRUE(ui::create(100, 24, 0, false, strip));
  TEST_ASSERT_TRUE(ui::place(strip, 0, 0));
  // Forty pages and the thirty-seventh: five balls fit, the window is 35..39 and the open one is the middle one of it.
  TEST_ASSERT_TRUE(ui::dots_draw(strip, 40, 37, 0, 15));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(50, 12));   // the open page, solid, in the middle of the strip
  TEST_ASSERT_EQUAL_UINT8(15, view_level(30, 12));  // its neighbour, a ring
  // The two ends of the window are inside the box: a ball of 8 pixels at column 10 keeps paper outside its edge.
  TEST_ASSERT_EQUAL_UINT8(15, view_level(0, 12));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(10, 8));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(90, 8));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(99, 12));

  // **The first page is the first ball of the window and the last one the last**: the window moves to the ends instead of
  // hanging out of them.
  ui::clear(15, nullptr);
  TEST_ASSERT_TRUE(ui::dots_draw(strip, 40, 1, 0, 15));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(10, 12));   // the first page, solid, in the first slot
  TEST_ASSERT_EQUAL_UINT8(15, view_level(30, 12));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(90, 12));  // and the fifth sees the fifth dot of a ring
  ui::release(strip);
}

// **A list measures its pages once and both the app and the drawing ask the same question**: a list that fits
// takes no footer and says nothing about pages, and one that does not takes the band and counts the rows again
// in what is left (docs/30, docs/90).
void test_a_list_measures_its_pages_with_one_arithmetic() {
  // Four rows of fifty in two hundred of height and four items: they fit, so there is one page and no footer.
  ui::ListPage fits = ui::list_page_of(200, 50, 4);
  TEST_ASSERT_EQUAL_UINT8(4, fits.rows);
  TEST_ASSERT_EQUAL_UINT8(1, fits.pages);

  // Twelve items: three pages of four are not enough once the footer of twenty pixels is taken, so a page holds
  // three rows and the list has four pages.
  ui::ListPage long_list = ui::list_page_of(200, 50, 12);
  TEST_ASSERT_EQUAL_UINT8(3, long_list.rows);
  TEST_ASSERT_EQUAL_UINT8(4, long_list.pages);

  // Nothing to say is no page at all, and a row that does not fit in the frame is not a row.
  ui::ListPage empty = ui::list_page_of(200, 50, 0);
  TEST_ASSERT_EQUAL_UINT8(0, empty.pages);
  ui::ListPage tiny = ui::list_page_of(40, 50, 9);
  TEST_ASSERT_EQUAL_UINT8(0, tiny.pages);
}

// **A list draws the rows of one page, the chosen row inverted, and the band that says which page it is**: the
// band is the line of the footer and the words in it, and **no row is drawn under it** (docs/30, docs/90).
void test_a_list_draws_one_page_and_says_which_one_it_is() {
  fresh_view();
  int8_t list = -1;
  TEST_ASSERT_TRUE(ui::create(300, 200, 0, false, list));
  TEST_ASSERT_TRUE(ui::place(list, 0, 0));
  const char* titles[3] = {"One", "Two", "Three"};
  const char* details[3] = {"1", "2", "3"};

  TEST_ASSERT_TRUE(ui::list_draw(list, 3, 1, 4, 2, titles, details, nullptr, 50, 0, 8, 15, 2, true, false));
  // The second row of the page is the chosen one and it is inverted: ink where the others are paper.
  TEST_ASSERT_EQUAL_UINT8(15, view_level(290, 25));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(290, 75));
  // The band of the footer starts at 200 less twenty: the line above it, and no row under it.
  TEST_ASSERT_EQUAL_UINT8(8, view_level(150, 180));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(290, 175));
  // The words of the page are in the band, and a page that is not the last has the chevron of the next one.
  int ink = 0;
  for (uint16_t y = 181; y < 200; ++y) {
    for (uint16_t x = 0; x < 300; ++x) {
      if (view_level(x, y) == 0) ++ink;
    }
  }
  TEST_ASSERT_GREATER_THAN(0, ink);

  // **One page is no band at all**: the list fills with rows and there is nothing under them.
  TEST_ASSERT_TRUE(ui::list_draw(list, 3, 1, 1, 0, titles, details, nullptr, 50, 0, 8, 15, 2, false, false));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(150, 180));
  ui::release(list);
}

// **The quality the system lives on is not a free clean** (0.88.18): `glr16` and `gld16` are what an app presents with,
// what the bar repaints a box with and what the band of the keyboard is washed with, and they are chosen **because they
// do not blink** —the rule of this project is that nothing that flickers reaches the glass—. A waveform that does not
// blink does not take the page back either: it lands close to the level and not on it, so a share of the page before
// stays and the paint adds a little dirt. **The bench said the opposite until now**, and with that the one choice this
// system takes everywhere was free here —a page that never gets dirty no matter how many times it is painted—, which is
// the one thing a bench must not say (docs/85, docs/92).
void test_a_gld16_leaves_a_trace_and_a_gc16_takes_it_away(void) {
  fresh_view();
  TEST_ASSERT_TRUE(glass().set_ink(true));
  // A black square with the quick quality, and white over it with `gld16`, which does not blink.
  TEST_ASSERT_TRUE(ui::canvas_fill_rect(0, 10, 10, 40, 40, 0, "du"));
  TEST_ASSERT_TRUE(ui::present());
  wait_ms(150);
  TEST_ASSERT_TRUE(ui::canvas_fill_rect(0, 10, 10, 40, 40, 15, "gld16"));
  TEST_ASSERT_TRUE(ui::present());
  // **A reduced quality takes its 450 ms and does not put a frame in between**: no blink, so there is nothing to see
  // until it lands (docs/85).
  wait_ms(500);
  const uint8_t left = glass().shown_at(20, 20);
  TEST_ASSERT_LESS_THAN_MESSAGE(15, left, "a gld16 left the page before it exactly as it was");
  TEST_ASSERT_GREATER_THAN_MESSAGE(9, left, "the trace of a gld16 is as dark as the one of a du");
  // **And it leaves dirt**, which is what a page painted this way for an hour shows: the ghost of the hour before.
  TEST_ASSERT_TRUE_MESSAGE(glass().dirt_at(20, 20) > 0, "a gld16 that does not blink adds no dirt");

  // **A `gc16` takes the trace all the way to the page**, because that is the whole difference between the two.
  TEST_ASSERT_TRUE(ui::canvas_fill_rect(0, 10, 10, 40, 40, 15, "gc16"));
  TEST_ASSERT_TRUE(ui::present());
  wait_ms(500);
  TEST_ASSERT_EQUAL_UINT8(15, glass().shown_at(20, 20));
  TEST_ASSERT_EQUAL_UINT8(0, glass().dirt_at(20, 20));
}

// **The keyboards are children of one parent and are chosen by name** (0.93.0): the parent has the band, the finger, the
// dial and the face; each child says its id and its rows. The list is what `cfg keyboard` prints, and a name that is not
// one of the children is refused instead of silently selecting another (docs/30, docs/70, docs/90).
void test_the_keyboards_are_children_of_one_parent_and_are_chosen_by_name() {
  TEST_ASSERT_EQUAL_UINT8(2, ui::keyboard_count());
  TEST_ASSERT_EQUAL_STRING("qwerty", ui::keyboard_at(0));
  TEST_ASSERT_EQUAL_STRING("azerty", ui::keyboard_at(1));
  TEST_ASSERT_NULL(ui::keyboard_at(2));
  TEST_ASSERT_EQUAL_STRING("qwerty", ui::keyboard_current());
  TEST_ASSERT_TRUE(ui::keyboard_select("azerty"));
  TEST_ASSERT_EQUAL_STRING("azerty", ui::keyboard_current());
  TEST_ASSERT_FALSE(ui::keyboard_select("dvorak"));
  TEST_ASSERT_EQUAL_STRING("azerty", ui::keyboard_current());
  TEST_ASSERT_TRUE(ui::keyboard_select("qwerty"));
  TEST_ASSERT_EQUAL_STRING("qwerty", ui::keyboard_current());
}

// **The caret is a block on the glass and never outside it**: an empty box has the bar at the left of the inside,
// without the caret the box is paper again, and a text longer than the frame is a **window** that keeps the caret
// where the next letter goes (docs/30, docs/90).
void test_a_textbox_keeps_the_caret_on_the_glass() {
  fresh_view();
  int8_t box = -1;
  TEST_ASSERT_TRUE(ui::create(200, 40, 0, false, box));
  TEST_ASSERT_TRUE(ui::place(box, 0, 0));
  // Two pixels of border and six of padding on each side: a hundred and eighty-four pixels of inside.
  TEST_ASSERT_EQUAL_UINT16(184, ui::textbox_inside(box, 2));

  // An empty box with the caret: a block of the width of a letter at the left of the inside, as tall as a line of the
  // face. **Since 0.92.0 it is a block and not the three-pixel hairline it was**: an empty box had a bar nobody saw.
  TEST_ASSERT_TRUE(ui::textbox_draw(box, "", 0, true, false, 0, 15, 0, 2));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(9, 14));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(9, 25));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(9, 30));  // the inside, below the line
  TEST_ASSERT_EQUAL_UINT8(15, view_level(60, 20)); // and nothing else is drawn
  int caret_columns = 0;
  for (uint16_t x = 8; x < 60; ++x) {
    bool solid = true;
    for (uint16_t y = 14; y <= 25; ++y) {
      if (view_level(x, y) != 0) {
        solid = false;
        break;
      }
    }
    if (solid) ++caret_columns;
  }
  TEST_ASSERT_GREATER_THAN_MESSAGE(6, caret_columns, "the caret at the end of the text is still a hairline");

  // **Without the caret the box is paper**: the system blinks it, and this is the other half of the blink.
  TEST_ASSERT_TRUE(ui::textbox_draw(box, "", 0, false, false, 0, 15, 0, 2));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(9, 14));

  // **A text longer than the frame with the caret at its end**: the caret ends up against the right side of the
  // inside and not past the border, which is the whole point of the window.
  TEST_ASSERT_TRUE(ui::textbox_draw(box, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 26, true, false, 0, 15, 0, 2));
  int32_t widest = -1;
  // **The frame of the box is a solid column of ink too**, so the scan stays inside it: what is looked for is the caret.
  for (uint16_t x = 12; x < 198; ++x) {
    bool solid = true;
    for (uint16_t y = 14; y <= 25; ++y) {
      if (view_level(x, y) != 0) {
        solid = false;
        break;
      }
    }
    if (solid) widest = x;
  }
  TEST_ASSERT_GREATER_THAN(100, widest);   // the window pushed it to the end
  // **The border of the box is not the text**: the last column the text may touch is the one before the frame.
  TEST_ASSERT_LESS_THAN(198, widest);

  // **The text that fits whole is drawn whole** (0.87.0): with the caret at the end and a text shorter than the window,
  // what is before the caret is the whole text and it has to be seen from the left edge. Up to 0.86.1 the box drew
  // "what is left after the caret" starting at the beginning of the window and the piece in front was never drawn, so a
  // path of twenty-two characters came out cut with the caret alone on the right (found in the graphical editor, 4/11,
  // docs/90).
  int8_t wide = -1;
  TEST_ASSERT_TRUE(ui::create(400, 40, 0, false, wide));
  TEST_ASSERT_TRUE(ui::place(wide, 0, 60));
  TEST_ASSERT_TRUE(ui::textbox_draw(wide, "/data/editor/note.txt", 22, true, false, 0, 15, 0, 2));
  // The first letter is an `l` of ink at the left of the inside, and the last `t` is before the caret, at six
  // pixels a letter of a face of bits.
  bool first_letter = false;
  for (uint16_t x = 8; x < 20 && !first_letter; ++x) {
    for (uint16_t y = 74; y < 86 && !first_letter; ++y) {
      if (view_level(x, y) == 0) first_letter = true;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(first_letter, "the head of the text was not drawn");
  bool last_letter = false;
  for (uint16_t x = 8 + 21 * 12; x < 8 + 22 * 12 && !last_letter; ++x) {
    for (uint16_t y = 74; y < 86 && !last_letter; ++y) {
      if (view_level(x, y) == 0) last_letter = true;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(last_letter, "the tail of a text that fits was cut");
  ui::release(wide);

  // A colour that is not a level of this panel and a scale of nothing are refused, and the mask is drawn in the
  // place of the letters.
  TEST_ASSERT_FALSE(ui::textbox_draw(box, "x", 1, true, false, 16, 15, 0, 2));
  TEST_ASSERT_FALSE(ui::textbox_draw(box, "x", 1, true, false, 0, 16, 0, 2));
  TEST_ASSERT_FALSE(ui::textbox_draw(box, "x", 1, true, false, 0, 15, 16, 2));
  TEST_ASSERT_TRUE(ui::textbox_draw(box, "secret", 6, false, true, 0, 15, 0, 2));
  ui::release(box);
}

// **The window of a text box carries the width of the tail and does not measure it again** (0.88.13): to know how much
// of the rest fits, the paint asked `font::width` for the whole accumulated tail after every letter it added, which is
// O(n²) in the letters the window shows, and `strlen` for the same buffer. The width is carried in a variable now, the
// way `advance_of` carries it for a row of a box of several lines, and what a paint costs on the bench is the number
// this test prints —the bench's numbers and not the board's, but the shape of the loop is the same— (docs/75, docs/90).
void test_the_window_of_a_textbox_carries_the_width_of_the_tail(void) {
  fresh_view();
  int8_t wide = -1;
  TEST_ASSERT_TRUE(ui::create(900, 40, 0, false, wide));
  TEST_ASSERT_TRUE(ui::place(wide, 0, 0));
  // **Three hundred letters with the caret at the front**: the window fills nine hundred pixels, which is about seventy
  // letters of the face at this scale, so the loop runs seventy times and not three hundred (docs/90).
  static char text[301];
  for (int i = 0; i < 300; ++i) text[i] = static_cast<char>('a' + (i % 26));
  text[300] = '\0';

  const uint32_t kPaints = 20;
  const uint32_t from = kernel::meter::now_us();
  for (uint32_t i = 0; i < kPaints; ++i) {
    TEST_ASSERT_TRUE(ui::textbox_draw(wide, text, 0, true, false, 0, 15, 0, 2));
  }
  const uint32_t us = (kernel::meter::now_us() - from) / kPaints;
  printf("textbox window: %u us a paint of a line of 300 letters\n", static_cast<unsigned>(us));

  // **And the tail stops at the inside of the box**: a width carried wrong draws more letters than fit and they land on
  // the paper between the inside and the frame —the box is nine hundred wide, the inside is eight hundred and eighty-four
  // from column eight, and the frame is the last two columns—, which is what a text running over its own border is. The
  // rows are the ones of the line and not the whole box: the corners of the frame are round and the last eight columns
  // of the last eight rows are the frame itself (docs/90).
  for (uint16_t x = 892; x < 898; ++x) {
    for (uint16_t y = 13; y < 27; ++y) {
      TEST_ASSERT_EQUAL_UINT8_MESSAGE(15, view_level(x, y), "a letter of the tail is drawn past the inside");
    }
  }
  // **And the tail reaches the right side of the inside**, which is the other half: a window that stops too soon is a
  // box that shows half a line and a person who cannot see what they typed (docs/90).
  bool near_end = false;
  for (uint16_t x = 860; x < 892 && !near_end; ++x) {
    for (uint16_t y = 6; y < 34 && !near_end; ++y) {
      if (view_level(x, y) == 0) near_end = true;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(near_end, "the tail of the window stops before the frame");
  ui::release(wide);
}

// **A dialog is a card with a message that wraps and two doors**: the card is opaque with the weight of a card, the
// emphasised door is inverted, and the finger and the paint ask the same geometry (docs/30, docs/90).
void test_a_dialog_wraps_its_message_and_has_two_doors() {
  fresh_view();
  int8_t card = -1;
  TEST_ASSERT_TRUE(ui::create(400, 300, 0, false, card));
  TEST_ASSERT_TRUE(ui::place(card, 0, 0));
  const char* message = "This card deletes every note you wrote and there is no way back at all";

  TEST_ASSERT_TRUE(ui::dialog_draw(card, "Delete?", message, "Only yours", "Delete", "Keep", 1, 1, 0, 8, 15, 2,
                                   false));
  // The card: three pixels of ink round it and paper inside.
  TEST_ASSERT_EQUAL_UINT8(0, view_level(1, 150));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(12, 150));
  // **The message wrapped**: there is ink on the first line of it and on a second one under it, which is what a cut
  // message would not have.
  int first = 0;
  int second = 0;
  for (uint16_t x = 20; x < 380; ++x) {
    for (uint16_t y = 49; y < 62; ++y) {
      if (view_level(x, y) == 0) ++first;
    }
    for (uint16_t y = 64; y < 77; ++y) {
      if (view_level(x, y) == 0) ++second;
    }
  }
  TEST_ASSERT_GREATER_THAN(0, first);
  TEST_ASSERT_GREATER_THAN(0, second);

  // **The two doors**: the cancel is the emphasised one and it is ink inside, the accept is paper. **The row is
  // the one the finger asks for**, so a message that wraps to another number of lines —a lowercase face is
  // narrower than a face of capitals— moves the doors and not this test (docs/30, docs/90).
  uint16_t door_top = 0;
  uint16_t door_bottom = 0;
  for (uint16_t y = 200; y < 300; ++y) {
    if (ui::dialog_hit(card, 100, y) != 1) continue;
    if (door_top == 0) door_top = y;
    door_bottom = y;
  }
  TEST_ASSERT_GREATER_THAN_UINT16(200, door_top);
  // The middle of the door and not its first row, which is the frame of it.
  const uint16_t door_y = static_cast<uint16_t>((door_top + door_bottom) / 2);
  // **The two doors are buttons** (0.97.0, docs/92): paper inside both of them, and **the door the app emphasised wears
  // the frame of three of a primary button** —which is the paper project's `setPrimary(true)`— while the other wears one
  // pixel and the shadow under its face.
  TEST_ASSERT_EQUAL_UINT8(15, view_level(30, door_y));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(100, door_top));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(100, door_top + 1));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(100, door_top + 2));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(100, door_top + 3));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(220, door_top));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(220, door_top + 1));
  // And the geometry a finger asks is the one that was painted.
  TEST_ASSERT_EQUAL_UINT8(1, ui::dialog_hit(card, 100, door_y));
  TEST_ASSERT_EQUAL_UINT8(2, ui::dialog_hit(card, 300, door_y));
  TEST_ASSERT_EQUAL_UINT8(0, ui::dialog_hit(card, 200, 100));
  TEST_ASSERT_EQUAL_UINT8(0, ui::dialog_hit(card, 200, 20));

  // A colour that is not a level of this panel is refused.
  TEST_ASSERT_FALSE(ui::dialog_draw(card, "x", "y", "", "OK", "", 2, 2, 16, 8, 15, 2, false));
  ui::release(card);
}

// **A calendar is seven columns and six weeks, and the first day goes where the weekday says**: the chosen day is
// inverted, today wears a frame, and the finger asks the same cells the paint put down (docs/30, docs/90).
void test_a_calendar_puts_the_first_day_where_the_weekday_says() {
  fresh_view();
  int8_t grid = -1;
  TEST_ASSERT_TRUE(ui::create(280, 280, 0, false, grid));
  TEST_ASSERT_TRUE(ui::place(grid, 0, 0));
  const char* names[7] = {"M", "T", "W", "T", "F", "S", "S"};

  // November of 2026 starts on a Sunday, which is the last column of the first week, and the third is chosen.
  TEST_ASSERT_TRUE(ui::month_draw(grid, 2026, 11, 3, 5, names, 0, 8, 15, 2, false));
  // The chosen day: the cell is ink inside and the number is paper.
  TEST_ASSERT_EQUAL_UINT8(0, view_level(42, 82));
  // The day before it is not chosen and its cell is paper.
  TEST_ASSERT_EQUAL_UINT8(15, view_level(2, 82));
  // **Today wears a frame**: the top left pixel of its cell is ink and the inside is not.
  TEST_ASSERT_EQUAL_UINT8(0, view_level(121, 81));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(123, 83));
  // The names of the days are on the row of the header, in the dim tone.
  int header = 0;
  for (uint16_t x = 0; x < 280; ++x) {
    for (uint16_t y = 0; y < 40; ++y) {
      if (view_level(x, y) == 8) ++header;
    }
  }
  TEST_ASSERT_GREATER_THAN(0, header);

  // **The same cells for the finger**: the chosen cell is the third, the first column of the second week is the
  // second, and the header is no day at all.
  TEST_ASSERT_EQUAL_UINT8(3, ui::month_hit(grid, 2026, 11, 50, 90));
  TEST_ASSERT_EQUAL_UINT8(2, ui::month_hit(grid, 2026, 11, 20, 90));
  TEST_ASSERT_EQUAL_UINT8(0, ui::month_hit(grid, 2026, 11, 20, 10));
  // A month of thirty ends before a cell that would be day thirty-one, and a month that does not exist is a paper
  // frame with no days in it and no day under the finger.
  TEST_ASSERT_EQUAL_UINT8(0, ui::month_hit(grid, 2026, 11, 260, 250));
  TEST_ASSERT_TRUE(ui::month_draw(grid, 2026, 13, 3, 5, names, 0, 8, 15, 2, false));
  TEST_ASSERT_EQUAL_UINT8(0, ui::month_hit(grid, 2026, 13, 50, 90));
  ui::release(grid);
}

// **A menu is a card of options and the system's**: an option the app cannot do is hidden and takes no room, the option
// of the moment is inverted, the ring is where the dial is, the options fill a column before the next one, and the
// finger asks the same boxes the paint put down (docs/30, docs/90).
void test_a_menu_hides_the_options_the_app_cannot_do() {
  fresh_view();
  int8_t card = -1;
  TEST_ASSERT_TRUE(ui::create(300, 200, 0, false, card));
  TEST_ASSERT_TRUE(ui::place(card, 0, 0));

  // Three options in a card of 300x200: three rows of fifty-four pixels, the third one inverted because it is the
  // option of the moment, and the ring on the second one.
  const ui::MenuOption three[3] = {
      {"Copy", "", false, false}, {"Rename", "", false, false}, {"Delete", "", false, true}};
  TEST_ASSERT_TRUE(ui::menu_draw(card, "", three, 3, 2, 0, 8, 15, 2, true));
  // The card: three pixels of ink round it and paper inside.
  TEST_ASSERT_EQUAL_UINT8(0, view_level(1, 100));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(12, 100));
  // **Every option of a menu is a button** (0.97.0, docs/92): paper inside with a frame of ink, **the option the dial
  // is on inverted** —ink face and paper word—, and the one the app calls primary wearing the frame of three.
  TEST_ASSERT_EQUAL_UINT8(15, view_level(26, 30));   // the first row, paper inside its frame
  TEST_ASSERT_EQUAL_UINT8(0, view_level(30, 100));   // the second row, which is the row the dial is on
  TEST_ASSERT_EQUAL_UINT8(15, view_level(30, 138));  // the third, which is primary and not the one the dial is on
  // **And the frame of a primary button is three pixels**, in the face and not on the box: three rows of ink and the
  // fourth the paper of the inside.
  // **A primary button wears a heavier frame than the others**, counted over the first rows of each option.
  int thin_frame = 0;
  int thick_frame = 0;
  for (uint16_t y = 15; y < 30; ++y) {
    if (view_level(30, y) == 0) ++thin_frame;
  }
  for (uint16_t y = 127; y < 142; ++y) {
    if (view_level(30, y) == 0) ++thick_frame;
  }
  TEST_ASSERT_GREATER_THAN(thin_frame, thick_frame);
  // **The boxes the finger asks**: one per row, the padding round the card is no option at all.
  TEST_ASSERT_EQUAL_UINT8(1, ui::menu_hit(card, "", three, 3, 2, 150, 47));
  TEST_ASSERT_EQUAL_UINT8(2, ui::menu_hit(card, "", three, 3, 2, 150, 101));
  TEST_ASSERT_EQUAL_UINT8(3, ui::menu_hit(card, "", three, 3, 2, 150, 155));
  TEST_ASSERT_EQUAL_UINT8(0, ui::menu_hit(card, "", three, 3, 2, 150, 10));
  TEST_ASSERT_EQUAL_UINT8(0, ui::menu_hit(card, "", three, 3, 2, 150, 190));

  // **The hidden option takes no room**: two rows of eighty-two pixels, so the third option is the second row and
  // there is nothing under it, and the number the finger gets is the option of the app and not its place on the card.
  const ui::MenuOption two[3] = {
      {"Copy", "", false, false}, {"Paste", "", true, false}, {"Delete", "", false, false}};
  TEST_ASSERT_TRUE(ui::menu_draw(card, "", two, 3, 3, 0, 8, 15, 2, true));
  TEST_ASSERT_EQUAL_UINT8(1, ui::menu_hit(card, "", two, 3, 2, 150, 60));
  TEST_ASSERT_EQUAL_UINT8(3, ui::menu_hit(card, "", two, 3, 2, 150, 155));
  TEST_ASSERT_EQUAL_UINT8(0, ui::menu_hit(card, "", two, 3, 2, 150, 190));
  // **The dial went with the third option, which is the second row**: that one is inverted —ink face— and the row of
  // the one above it is paper (0.97.0, docs/92).
  TEST_ASSERT_EQUAL_UINT8(0, view_level(22, 122));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(22, 66));

  // **Four options fill a column before the next one**: two columns of two with a gap between them, and the gap is no
  // option.
  const ui::MenuOption four[4] = {
      {"One", "", false, false}, {"Two", "", false, false}, {"Three", "", false, false}, {"Four", "", false, false}};
  TEST_ASSERT_TRUE(ui::menu_draw(card, "", four, 4, 1, 0, 8, 15, 2, false));
  TEST_ASSERT_EQUAL_UINT8(1, ui::menu_hit(card, "", four, 4, 2, 40, 50));
  TEST_ASSERT_EQUAL_UINT8(2, ui::menu_hit(card, "", four, 4, 2, 40, 150));
  TEST_ASSERT_EQUAL_UINT8(3, ui::menu_hit(card, "", four, 4, 2, 200, 50));
  TEST_ASSERT_EQUAL_UINT8(4, ui::menu_hit(card, "", four, 4, 2, 200, 150));
  TEST_ASSERT_EQUAL_UINT8(0, ui::menu_hit(card, "", four, 4, 2, 150, 50));

  // A colour that is not a level of this panel is refused.
  TEST_ASSERT_FALSE(ui::menu_draw(card, "", three, 3, 1, 16, 8, 15, 2, false));
  ui::release(card);
}

// **A bar of menus is a row of words with a line under it and one menu that hangs from the word that is down**: the word
// is inverted, the line under the row is ink, an entry that cannot be used is the dim tone, a dash is not a row a finger
// presses, and the finger asks the same boxes the paint put down (docs/30, docs/90).
void test_a_bar_of_menus_hangs_the_menu_under_its_word() {
  fresh_view();
  int8_t bar = -1;
  TEST_ASSERT_TRUE(ui::create(400, 400, 0, false, bar));
  TEST_ASSERT_TRUE(ui::place(bar, 0, 0));

  // label, separator, checked, checkable, enabled — "Save" is the setting that is off, so it carries an empty square.
  const ui::MenuBarEntry file[4] = {
      {"Open", false, true, true, true}, {"", true, false, false, true}, {"Quit", false, false, false, false},
      {"Save", false, false, true, true}};
  const ui::MenuBarTitle titles[2] = {{"File", file, 4}, {"Edit", nullptr, 0}};
  TEST_ASSERT_TRUE(ui::menu_bar_draw(bar, titles, 2, 1, 1, 52, 0, 8, 15, 2, true));
  // **The row**: paper, with the word whose menu is down inverted, and the line of ink under it.
  TEST_ASSERT_EQUAL_UINT8(0, view_level(10, 26));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(80, 26));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(200, 51));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(200, 52));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(200, 53));
  // **The menu is a card of the system** (0.97.0, docs/92): three rings of ink from 0 and 54, and paper inside them.
  TEST_ASSERT_EQUAL_UINT8(0, view_level(1, 80));
  // **And the rings are three and paper comes after them**: the left edge of a card in its straight part.
  TEST_ASSERT_EQUAL_UINT8(0, view_level(1, 80));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(3, 80));
  // **The box of the tick of the first entry**, which is the row the dial is on: the row is ink and the box flips to
  // paper, tick of paper, and between them the ink of the row (docs/92).
  TEST_ASSERT_EQUAL_UINT8(15, view_level(12, 80));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(17, 80));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(26, 80));
  // **An entry that cannot be used is the dim tone**: there is a level eight in the row of "Quit" and none in the row of
  // "Open", which is the row of a word that works.
  int dim_row = 0;
  int ink_row = 0;
  for (uint16_t x = 38; x < 180; ++x) {
    // **The third entry of the menu is the second row under the dash**, and it is the one that cannot be used.
    for (uint16_t y = 145; y < 159; ++y) {
      if (view_level(x, y) == 8) ++dim_row;
    }
    for (uint16_t y = 75; y < 89; ++y) {
      if (view_level(x, y) == 8) ++ink_row;
    }
  }
  TEST_ASSERT_GREATER_THAN(0, dim_row);
  TEST_ASSERT_EQUAL_INT(0, ink_row);
  // **An entry that is a setting keeps its box when it is off**: the square of "Save" is a frame of ink with paper in
  // its middle, which is what tells a switch from a command without a word (docs/90).
  // The square of "Save" is around y 204, which is a point of its row: a frame of ink at both sides and paper in the
  // middle, where the tick would be.
  TEST_ASSERT_EQUAL_UINT8(0, view_level(12, 204));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(20, 204));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(29, 204));
  // **The boxes the finger asks**: the titles of the row, the entries of the menu, and neither the line between two
  // entries nor the space past the menu's width.
  TEST_ASSERT_EQUAL_UINT8(1, ui::menu_bar_title_at(bar, titles, 2, 52, 2, 30, 20));
  TEST_ASSERT_EQUAL_UINT8(2, ui::menu_bar_title_at(bar, titles, 2, 52, 2, 100, 20));
  TEST_ASSERT_EQUAL_UINT8(0, ui::menu_bar_title_at(bar, titles, 2, 52, 2, 200, 20));
  TEST_ASSERT_EQUAL_UINT8(0, ui::menu_bar_title_at(bar, titles, 2, 52, 2, 30, 60));
  TEST_ASSERT_EQUAL_UINT8(1, ui::menu_bar_entry_at(bar, titles, 2, 1, 52, 2, 100, 80));
  // **The dash is the second entry of the menu**, so the third one starts right under it and the line itself is no entry.
  TEST_ASSERT_EQUAL_UINT8(0, ui::menu_bar_entry_at(bar, titles, 2, 1, 52, 2, 100, 116));
  TEST_ASSERT_EQUAL_UINT8(3, ui::menu_bar_entry_at(bar, titles, 2, 1, 52, 2, 100, 130));
  TEST_ASSERT_EQUAL_UINT8(3, ui::menu_bar_entry_at(bar, titles, 2, 1, 52, 2, 100, 170));
  TEST_ASSERT_EQUAL_UINT8(4, ui::menu_bar_entry_at(bar, titles, 2, 1, 52, 2, 100, 200));
  TEST_ASSERT_EQUAL_UINT8(0, ui::menu_bar_entry_at(bar, titles, 2, 1, 52, 2, 300, 80));
  TEST_ASSERT_EQUAL_UINT8(0, ui::menu_bar_entry_at(bar, titles, 2, 0, 52, 2, 100, 80));

  // A colour that is not a level of this panel is refused.
  TEST_ASSERT_FALSE(ui::menu_bar_draw(bar, titles, 2, 1, 1, 52, 16, 8, 15, 2, true));
  ui::release(bar);
}

// **The scroll bar of a box of lines** (0.88.0): when the text is taller than the box, the right side is a track with a
// thumb; the thumb is as long as the part of the text that is seen and it walks the rest of the track according to the
// first row shown. A box the text fits in has no bar, and the text loses no room to it (docs/90).
void test_a_box_of_lines_that_overflows_carries_a_bar_on_its_right(void) {
  fresh_view();
  int8_t box = -1;
  // A box of a hundred pixels is six rows of the fourteen of a letter: fifteen lines do not fit and five do.
  TEST_ASSERT_TRUE(ui::create(200, 100, 0, false, box));
  TEST_ASSERT_TRUE(ui::place(box, 0, 0));
  const char* text = "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\nnine\nten\neleven\ntwelve\nthirteen\n"
                     "fourteen\nfifteen";

  // **Sense barra quan el text hi cap**: the five lines of the short text leave the right of the box in paper.
  TEST_ASSERT_TRUE(ui::textbox_multiline_draw(box, "one\ntwo", 0, false, false, 0, 15, 0, 2));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(192, 40));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(196, 40));

  // **Amb barra**: the inner right of the box is the track —a frame of ink— and the thumb is a block of ink inside it.
  // The track goes from the air of the frame to the last row that fits, and it is the same for every row.
  TEST_ASSERT_TRUE(ui::textbox_multiline_draw(box, text, 0, false, false, 0, 15, 0, 2));
  int column = -1;
  for (uint16_t x = 170; x < 200 && column < 0; ++x) {
    int ink = 0;
    for (uint16_t y = 8; y < 92; ++y) {
      if (view_level(x, y) == 0) ++ink;
    }
    if (ink > 60) column = x;  // the left side of the track is a line of ink from the top to the bottom
  }
  TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(0, column, "there is no bar on the right of the box");
  // The frame of the track is the whole height of the rows and the thumb is inside it, so the inside of the bar has
  // ink where the thumb is and paper where it is not: the frame is the longer one and the thumb is shorter than it.
  int frame = 0;
  int thumb = 0;
  for (uint16_t y = 8; y < 92; ++y) {
    if (view_level(column, y) == 0) ++frame;
    if (view_level(static_cast<uint16_t>(column + 2), y) == 0) ++thumb;
  }
  TEST_ASSERT_GREATER_THAN(0, frame);
  TEST_ASSERT_GREATER_THAN(0, thumb);
  TEST_ASSERT_LESS_THAN(frame, thumb);

  // **The thumb walks with the first row**: with `top` at zero it is at the very top and the middle of the bar is
  // paper; with the last window it is at the bottom and it is the top of the bar that is paper.
  const auto inside_ink = [column](uint16_t y0, uint16_t y1) {
    int ink = 0;
    for (uint16_t y = y0; y < y1; ++y) {
      if (view_level(static_cast<uint16_t>(column + 2), y) == 0) ++ink;
    }
    return ink;
  };
  // The two ends of the track are part of its frame, so the bands are measured inside it.
  TEST_ASSERT_TRUE(ui::textbox_multiline_draw(box, text, 0, false, false, 0, 15, 0, 2, 0));
  TEST_ASSERT_GREATER_THAN(0, inside_ink(10, 40));
  TEST_ASSERT_EQUAL_INT(0, inside_ink(60, 90));
  TEST_ASSERT_TRUE(ui::textbox_multiline_draw(box, text, 0, false, false, 0, 15, 0, 2, 9));
  TEST_ASSERT_EQUAL_INT(0, inside_ink(10, 40));
  TEST_ASSERT_GREATER_THAN(0, inside_ink(60, 90));

  ui::release(box);
}

// **The window of a box of lines is the one the app says** (0.88.0): `top` rules over the cursor —the finger that has
// moved the bar must not go back to the row of the caret on the next paint— and the box gives it up to the limits of the
// text, so an app does not have to know how many rows there are to move the window (docs/30, docs/90).
void test_the_window_of_a_box_of_lines_is_the_one_the_app_says(void) {
  fresh_view();
  int8_t box = -1;
  TEST_ASSERT_TRUE(ui::create(200, 100, 0, false, box));
  TEST_ASSERT_TRUE(ui::place(box, 0, 0));
  const char* text = "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\nnine\nten\neleven\ntwelve\nthirteen\n"
                     "fourteen\nfifteen";
  // The cursor is at the start and the window is the one the cursor asks for, which is the first row.
  TEST_ASSERT_EQUAL_UINT16(0, ui::textbox_multiline_scroll(box, text, 0, false, 2, -1, 0));
  // Three rows down from there, and the same three from a window that was already three down.
  TEST_ASSERT_EQUAL_UINT16(3, ui::textbox_multiline_scroll(box, text, 0, false, 2, -1, 3));
  TEST_ASSERT_EQUAL_UINT16(6, ui::textbox_multiline_scroll(box, text, 0, false, 2, 3, 3));
  // **A window does not come out of the text**: nine rows fit five, so the last window is the fourth row of the last
  // five.
  TEST_ASSERT_EQUAL_UINT16(9, ui::textbox_multiline_scroll(box, text, 0, false, 2, 3, 99));
  TEST_ASSERT_EQUAL_UINT16(0, ui::textbox_multiline_scroll(box, text, 0, false, 2, 3, -99));
  // **The caret rules when the app has not chosen a window**: with the cursor at the end the window is the last five
  // rows.
  const uint16_t end = static_cast<uint16_t>(strlen(text));
  TEST_ASSERT_EQUAL_UINT16(9, ui::textbox_multiline_scroll(box, text, end, false, 2, -1, 0));
  // **And how many pixels one row is is what the finger has to travel**: the same fourteen of the paint.
  TEST_ASSERT_EQUAL_UINT8(14, ui::textbox_multiline_line(box, 2));
  // **How many rows the whole text has**, which is what the app says in its status line.
  TEST_ASSERT_EQUAL_UINT16(15, ui::textbox_multiline_total(box, text, false, 2));

  // **The first row of the glass is `top`**: the row that starts at the top of the box is the fourth line, so the byte
  // the finger asks for one row below the top of the box is the one after "four", which is where "five" starts.
  TEST_ASSERT_TRUE(ui::textbox_multiline_draw(box, text, 0, false, false, 0, 15, 0, 2, 3));
  TEST_ASSERT_EQUAL_UINT16(19, ui::textbox_multiline_hit(box, text, 0, false, 2, 8, 23, 3));

  ui::release(box);
}

// **The caret of a box of lines is where the cursor is, and the two ends of a row count** (0.84.1): the block of a
// cursor at zero is the first cell of the first row —it used to be drawn at the end of it, because the letters of a row
// are counted from one and the fallback that closes a row took every position that was not counted— and the block of a
// cursor at the end of a row is one cell after its last letter (docs/90).
static void test_the_caret_of_a_box_of_lines_is_at_the_cursor_and_not_at_the_end_of_the_row() {
  fresh_view();
  int8_t box = -1;
  TEST_ASSERT_TRUE(ui::create(300, 120, 0, false, box));
  TEST_ASSERT_TRUE(ui::place(box, 0, 0));
  // The ink of a cell of the first line: the letters of "Notes" at scale 2 are five cells of twelve pixels from eight.
  const auto ink_between = [](uint16_t x0, uint16_t x1) {
    int ink = 0;
    for (uint16_t x = x0; x < x1; ++x) {
      for (uint16_t y = 8; y < 22; ++y) {
        if (view_level(x, y) == 0) ++ink;
      }
    }
    return ink;
  };

  TEST_ASSERT_TRUE(ui::textbox_multiline_draw(box, "Notes\nthe rest", 0, false, false, 0, 15, 0, 2));
  const int start_without = ink_between(8, 18);
  const int end_without = ink_between(70, 80);
  // **The cursor at zero puts a block on the first cell** (0.84.1): the fallback that closes a row took every position
  // that the loop had not counted, and the first letter is counted from one, so the block of a cursor at the start of
  // the text was drawn at the end of its first line (docs/90).
  TEST_ASSERT_TRUE(ui::textbox_multiline_draw(box, "Notes\nthe rest", 0, true, false, 0, 15, 0, 2));
  TEST_ASSERT_GREATER_THAN_MESSAGE(start_without, ink_between(8, 18), "the caret at zero is not on the first cell");
  TEST_ASSERT_EQUAL_INT_MESSAGE(end_without, ink_between(70, 80), "the caret at zero is at the end of the row");

  // And the cursor at the end of the first line is a block after its last letter, which is where it was drawn before.
  TEST_ASSERT_TRUE(ui::textbox_multiline_draw(box, "Notes\nthe rest", 5, true, false, 0, 15, 0, 2));
  TEST_ASSERT_EQUAL_INT_MESSAGE(start_without, ink_between(8, 18), "the caret went back to the start of the row");
  TEST_ASSERT_GREATER_THAN_MESSAGE(end_without, ink_between(62, 74), "the caret at the end of the row is not there");

  ui::release(box);
}

// **A box paints the text it was given and not the one before it** (0.84.1): the tail of the line is a buffer kept
// between paints and it was never emptied, so every paint appended its text to the last one —a box with "h", then "he",
// then "hel" showed "h", "hhe", "hhehel", which on the glass is a box full of ghosts of every word typed so far— (found
// on the bench with `run typing`, 4/11, docs/90).
static void test_a_box_paints_its_text_and_not_the_one_before_it() {
  fresh_view();
  int8_t box = -1;
  TEST_ASSERT_TRUE(ui::create(480, 60, 0, false, box));
  TEST_ASSERT_TRUE(ui::place(box, 20, 20));
  // The ink of the line: the box centres a line of twenty-one pixels in sixty, and the text starts eight pixels in.
  const auto ink_between = [](uint16_t x0, uint16_t x1) {
    int ink = 0;
    for (uint16_t x = x0; x < x1; ++x) {
      for (uint16_t y = 40; y < 58; ++y) {
        if (view_level(x, y) == 0) ++ink;
      }
    }
    return ink;
  };
  TEST_ASSERT_TRUE(ui::textbox_draw(box, "h", 1, false, false, 0, 15, 0, 3));
  TEST_ASSERT_TRUE(ui::present());
  const int one = ink_between(26, 100);
  TEST_ASSERT_GREATER_THAN(0, one);
  // Two letters and no more: what the first paint left is not part of this one.
  TEST_ASSERT_TRUE(ui::textbox_draw(box, "he", 2, false, false, 0, 15, 0, 3));
  TEST_ASSERT_TRUE(ui::present());
  TEST_ASSERT_GREATER_THAN_MESSAGE(one, ink_between(26, 100), "the second paint did not add its letter");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, ink_between(80, 300), "the box kept the text of the paint before");
  // And the same with the caret, which is where the tail is built for a box the person is typing in.
  TEST_ASSERT_TRUE(ui::textbox_draw(box, "hel", 3, true, false, 0, 15, 0, 3));
  TEST_ASSERT_TRUE(ui::present());
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, ink_between(100, 300), "the box kept the text of the paint before");
  ui::release(box);
}

// **A box of several lines keeps the caret on the glass**: eight lines in a box of six rows, so the window starts at the
// third one, the caret is at the end of the last row it shows, and the finger that lands on a row asks for the byte of the
// text that is there —which is the same arithmetic the paint used— (docs/30, docs/90).
void test_a_box_of_lines_keeps_the_caret_on_the_glass() {
  fresh_view();
  int8_t box = -1;
  TEST_ASSERT_TRUE(ui::create(200, 100, 0, false, box));
  TEST_ASSERT_TRUE(ui::place(box, 0, 0));
  const char* text = "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight";
  const uint16_t end = static_cast<uint16_t>(strlen(text));

  TEST_ASSERT_TRUE(ui::textbox_multiline_draw(box, text, end, true, false, 0, 15, 0, 2));
  // Six rows of fourteen pixels in a box of a hundred with a frame and its air.
  TEST_ASSERT_EQUAL_UINT8(6, ui::textbox_multiline_rows(box, 2));
  // **The first row on the glass is the third line**, so there is ink at the top of the window.
  int first_row = 0;
  for (uint16_t x = 8; x < 60; ++x) {
    for (uint16_t y = 8; y < 22; ++y) {
      if (view_level(x, y) == 0) ++first_row;
    }
  }
  TEST_ASSERT_GREATER_THAN(0, first_row);
  // **The caret is a block after the word of the last row it shows**, and what follows it is paper.
  TEST_ASSERT_EQUAL_UINT8(0, view_level(68, 85));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(80, 85));
  // **The finger asks the same window**: the first row of the glass is the byte where "three" starts, the second one is
  // after it, and a point past the end of a row asks for the end of that row.
  TEST_ASSERT_EQUAL_UINT16(8, ui::textbox_multiline_hit(box, text, end, false, 2, 8, 9));
  TEST_ASSERT_EQUAL_UINT16(14, ui::textbox_multiline_hit(box, text, end, false, 2, 8, 23));
  TEST_ASSERT_EQUAL_UINT16(18, ui::textbox_multiline_hit(box, text, end, false, 2, 300, 23));
  // **A finger below the last line is the end of the text** (0.85.2): the row it found has no letter, so there is no
  // letter to be beside and the caret goes after the last one, at whatever x the finger landed. It used to go to the
  // middle of the last line, which is not the height the person touched.
  TEST_ASSERT_EQUAL_UINT16(end, ui::textbox_multiline_hit(box, text, end, false, 2, 8, 200));
  TEST_ASSERT_EQUAL_UINT16(end, ui::textbox_multiline_hit(box, text, end, false, 2, 300, 200));
  // And a row that is there and has no letter in it is still a row: the caret goes to it, at the height of the finger.
  const char* gaps = "no\n\nsir";
  TEST_ASSERT_EQUAL_UINT16(3, ui::textbox_multiline_hit(box, gaps, 0, false, 2, 8, 23));
  TEST_ASSERT_EQUAL_UINT16(3, ui::textbox_multiline_hit(box, gaps, 0, false, 2, 300, 23));
  TEST_ASSERT_EQUAL_UINT16(4, ui::textbox_multiline_hit(box, gaps, 0, false, 2, 8, 37));
  TEST_ASSERT_EQUAL_UINT16(7, ui::textbox_multiline_hit(box, gaps, 0, false, 2, 300, 37));

  // **A wrapped row breaks at the last space that fits**: the room is eighty-four pixels and a letter is twelve, so
  // "aaaa" is the first row, the space is the break and the next row starts after it.
  int8_t narrow = -1;
  TEST_ASSERT_TRUE(ui::create(100, 60, 0, false, narrow));
  TEST_ASSERT_TRUE(ui::place(narrow, 0, 0));
  const char* words = "aaaa bbbb cccc";
  TEST_ASSERT_TRUE(ui::textbox_multiline_draw(narrow, words, 0, false, true, 0, 15, 0, 2));
  TEST_ASSERT_EQUAL_UINT16(0, ui::textbox_multiline_hit(narrow, words, 0, true, 2, 8, 9));
  TEST_ASSERT_EQUAL_UINT16(5, ui::textbox_multiline_hit(narrow, words, 0, true, 2, 8, 23));
  // Without the wrapping the same row is the whole line and the second row of the glass is not a row at all: a finger
  // there is below the last letter, and the end of the text is where it goes (0.85.2).
  TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(strlen(words)),
                           ui::textbox_multiline_hit(narrow, words, 0, false, 2, 8, 23));

  // A colour that is not a level of this panel is refused.
  TEST_ASSERT_FALSE(ui::textbox_multiline_draw(box, text, end, true, false, 16, 15, 0, 2));
  ui::release(narrow);
  ui::release(box);
}

// **A selector is a field with a question and an answer, and its door is the column of the triangle**: an open field keeps
// its paper and only that column goes to ink, and the frame gets the weight of the focus (docs/30, docs/90).
void test_a_selector_is_a_field_with_a_door_on_its_right() {
  fresh_view();
  int8_t field = -1;
  TEST_ASSERT_TRUE(ui::create(400, 60, 0, false, field));
  TEST_ASSERT_TRUE(ui::place(field, 0, 0));

  TEST_ASSERT_TRUE(ui::selector_draw(field, "Hora", "09:30", false, 0, 8, 15, 0, 3, false));
  // The field: a hairline of ink, paper inside, and the line of the column at 366.
  TEST_ASSERT_EQUAL_UINT8(0, view_level(0, 30));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(2, 30));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(366, 10));
  // **The column is paper while the list is not over the field**, and there is a triangle of ink in the middle.
  TEST_ASSERT_EQUAL_UINT8(15, view_level(370, 10));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(383, 30));
  // **The question in the dim tone and the answer in the ink**: the label at the small size on the left, the value after it.
  int dim_text = 0;
  int ink_text = 0;
  for (uint16_t x = 13; x < 60; ++x) {
    for (uint16_t y = 20; y < 40; ++y) {
      if (view_level(x, y) == 8) ++dim_text;
    }
  }
  for (uint16_t x = 71; x < 200; ++x) {
    for (uint16_t y = 20; y < 40; ++y) {
      if (view_level(x, y) == 0) ++ink_text;
    }
  }
  TEST_ASSERT_GREATER_THAN(0, dim_text);
  TEST_ASSERT_GREATER_THAN(0, ink_text);

  // **An open field is paper and only its column goes to ink**: a field that goes dark from end to end reads as the page
  // going dark, which is what the owner said of the old one.
  TEST_ASSERT_TRUE(ui::selector_draw(field, "Hora", "09:30", true, 0, 8, 15, 0, 3, false));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(370, 10));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(383, 30));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(2, 30));

  // **The frame of the focus is two pixels and it is the tone of the words**.
  TEST_ASSERT_TRUE(ui::selector_draw(field, "Hora", "09:30", false, 0, 8, 15, 0, 3, true));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(1, 30));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(3, 30));
  // **Nothing chosen is a value too**: the word the app says, drawn where a value would be.
  TEST_ASSERT_TRUE(ui::selector_draw(field, "Hora", "-", false, 0, 8, 15, 0, 3, false));
  int empty_text = 0;
  for (uint16_t x = 71; x < 200; ++x) {
    for (uint16_t y = 20; y < 40; ++y) {
      if (view_level(x, y) == 0) ++empty_text;
    }
  }
  TEST_ASSERT_GREATER_THAN(0, empty_text);
  // A colour that is not a level of this panel is refused.
  TEST_ASSERT_FALSE(ui::selector_draw(field, "Hora", "09:30", false, 16, 8, 15, 0, 3, false));
  ui::release(field);
}

// **A band of notifications stacks its cards from the bottom, the newest down**, says how many are waiting with a `+N`, and
// the cross of a card is the box a finger dismisses with (docs/30, docs/90).
void test_a_band_of_notifications_puts_the_newest_at_the_bottom() {
  fresh_view();
  int8_t band = -1;
  TEST_ASSERT_TRUE(ui::create(400, 200, 0, false, band));
  TEST_ASSERT_TRUE(ui::place(band, 0, 0));

  const ui::Notification cards[2] = {{"Saved", "your note"}, {"Wi-fi", "connected"}};
  TEST_ASSERT_TRUE(ui::notification_bar_draw(band, cards, 2, 2, 0, 8, 15, 0, 2, false));
  // Two cards of fifty-six pixels: the newest —"Wi-fi"— is the one at the bottom of the frame.
  TEST_ASSERT_EQUAL_UINT8(15, view_level(10, 150));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(10, 100));
  // The line that separates the card from the cross, and the cross itself: there is ink in the box on the right.
  TEST_ASSERT_EQUAL_UINT8(0, view_level(356, 150));
  int cross = 0;
  for (uint16_t x = 369; x < 387; ++x) {
    for (uint16_t y = 163; y < 181; ++y) {
      if (view_level(x, y) == 0) ++cross;
    }
  }
  TEST_ASSERT_GREATER_THAN(4, cross);
  // **The cards the finger asks, counted from the bottom**: one is the newest and two is the one over it, and the column of
  // the cross is the one that dismisses.
  TEST_ASSERT_EQUAL_UINT8(1, ui::notification_bar_card_at(band, 2, 2, 2, 10, 150));
  TEST_ASSERT_EQUAL_UINT8(2, ui::notification_bar_card_at(band, 2, 2, 2, 10, 100));
  TEST_ASSERT_EQUAL_UINT8(0, ui::notification_bar_card_at(band, 2, 2, 2, 10, 20));
  TEST_ASSERT_TRUE(ui::notification_bar_close_at(band, 2, 2, 2, 380, 170));
  TEST_ASSERT_FALSE(ui::notification_bar_close_at(band, 2, 2, 2, 10, 150));

  // **What does not fit is not lost**: with five notices and two on the glass, three are waiting and the band says so.
  TEST_ASSERT_TRUE(ui::notification_bar_draw(band, cards, 2, 5, 0, 8, 15, 0, 2, false));
  int waiting = 0;
  for (uint16_t x = 12; x < 60; ++x) {
    for (uint16_t y = 64; y < 84; ++y) {
      if (view_level(x, y) == 8) ++waiting;
    }
  }
  TEST_ASSERT_GREATER_THAN(0, waiting);
  TEST_ASSERT_EQUAL_UINT8(1, ui::notification_bar_card_at(band, 2, 5, 2, 10, 150));

  // A colour that is not a level of this panel is refused.
  TEST_ASSERT_FALSE(ui::notification_bar_draw(band, cards, 2, 2, 16, 8, 15, 0, 2, false));
  ui::release(band);
}

// **A grid is cells on pages and the page is where the cursor is**: the cell that is chosen is inverted, the cell the dial is
// on wears the ring, the name is ink even on a chosen cell, and the footer says which page it is —and a finger asks the same
// cells the paint put down, page included— (docs/30, docs/90).
void test_a_grid_puts_the_page_where_the_cursor_is() {
  fresh_view();
  int8_t grid = -1;
  TEST_ASSERT_TRUE(ui::create(300, 300, 0, false, grid));
  TEST_ASSERT_TRUE(ui::place(grid, 0, 0));

  const ui::GridItem cells[8] = {{"Notes", "N"}, {"Clock", "C"}, {"Files", "F"}, {"Paint", "P"},
                                 {"Music", "M"}, {"Radio", "R"}, {"Codes", "S"}, {"Maps", "G"}};
  // Three columns of a hundred pixels: the box of the mark is seventy-two, so a cell is a hundred and two pixels high and two
  // rows fit above the footer. The seventh cell is on the second page.
  TEST_ASSERT_TRUE(ui::icon_grid_draw(grid, cells, 8, 7, 7, 3, 0, 8, 15, 2, true));
  // **The cell that is chosen is inverted**: its box is ink from edge to edge and the mark is paper.
  TEST_ASSERT_EQUAL_UINT8(0, view_level(14, 40));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(40, 40));
  int mark = 0;
  for (uint16_t x = 20; x < 80; ++x) {
    for (uint16_t y = 10; y < 70; ++y) {
      if (view_level(x, y) == 15) ++mark;
    }
  }
  TEST_ASSERT_GREATER_THAN(0, mark);
  // The cell next to it is not chosen: the frame is ink and the inside is paper.
  TEST_ASSERT_EQUAL_UINT8(0, view_level(114, 40));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(140, 40));
  // **The ring of the dial is on the cell it is on**, inside the cell and away from its edge.
  TEST_ASSERT_EQUAL_UINT8(0, view_level(2, 2));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(6, 6));
  // The name of a cell is ink, also when the cell is chosen, and the footer says which page this is in the dim tone.
  int names = 0;
  int footer = 0;
  for (uint16_t x = 0; x < 100; ++x) {
    for (uint16_t y = 84; y < 100; ++y) {
      if (view_level(x, y) == 0) ++names;
    }
  }
  for (uint16_t x = 100; x < 200; ++x) {
    for (uint16_t y = 278; y < 296; ++y) {
      if (view_level(x, y) == 8) ++footer;
    }
  }
  TEST_ASSERT_GREATER_THAN(0, names);
  TEST_ASSERT_GREATER_THAN(0, footer);

  // **The cells the finger asks, on the page that is on the glass**: the first of the two, the second, and nothing where the
  // page has no cell, on the footer or in the third column of the second row.
  TEST_ASSERT_EQUAL_UINT8(7, ui::icon_grid_hit(grid, cells, 8, 7, 3, 2, 50, 20));
  TEST_ASSERT_EQUAL_UINT8(8, ui::icon_grid_hit(grid, cells, 8, 7, 3, 2, 150, 20));
  TEST_ASSERT_EQUAL_UINT8(0, ui::icon_grid_hit(grid, cells, 8, 7, 3, 2, 250, 20));
  TEST_ASSERT_EQUAL_UINT8(0, ui::icon_grid_hit(grid, cells, 8, 7, 3, 2, 50, 150));
  TEST_ASSERT_EQUAL_UINT8(0, ui::icon_grid_hit(grid, cells, 8, 7, 3, 2, 50, 290));
  // With no cursor the grid is on the first page, and there the first cell is the first one.
  TEST_ASSERT_EQUAL_UINT8(1, ui::icon_grid_hit(grid, cells, 8, 0, 3, 2, 50, 20));

  // A colour that is not a level of this panel is refused.
  TEST_ASSERT_FALSE(ui::icon_grid_draw(grid, cells, 8, 7, 7, 3, 16, 8, 15, 2, true));
  ui::release(grid);
}

// **A sparkline is the shape of the window and not its numbers**: the bar of the biggest sample touches the top, the
// others are proportional to it, there is a gap between two bars, and **the second series is an outline** because the
// two are parts of one total and the ink is the only level a chart may use (docs/30, docs/90).
void test_a_sparkline_scales_to_the_peak_of_its_window() {
  fresh_view();
  int8_t chart = -1;
  TEST_ASSERT_TRUE(ui::create(120, 40, 0, false, chart));
  TEST_ASSERT_TRUE(ui::place(chart, 0, 0));

  const uint16_t samples[3] = {10, 20, 5};
  TEST_ASSERT_TRUE(ui::sparkline_draw(chart, samples, nullptr, 3, 0, 15));
  // Three bars of forty pixels: the first is half of the peak and the second is the peak.
  TEST_ASSERT_EQUAL_UINT8(0, view_level(10, 25));   // the first bar, at three quarters of the height
  TEST_ASSERT_EQUAL_UINT8(15, view_level(10, 15));  // above it
  TEST_ASSERT_EQUAL_UINT8(0, view_level(50, 2));    // the second is the peak and reaches the top
  TEST_ASSERT_EQUAL_UINT8(15, view_level(39, 30));  // the gap between the first and the second

  // **The peak of the window and not a fixed scale**: the same shape at another scale.
  const uint16_t other[3] = {100, 200, 50};
  TEST_ASSERT_TRUE(ui::sparkline_draw(chart, other, nullptr, 3, 0, 15));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(10, 25));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(10, 15));

  // **Two series**: the bottom one solid and the top one hollow, so the two parts of a total are told apart with
  // the ink of the frame and not with a colour.
  const uint16_t below[1] = {5};
  const uint16_t above[1] = {5};
  TEST_ASSERT_TRUE(ui::sparkline_draw(chart, below, above, 1, 0, 15));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(20, 0));   // the line on top of the outline
  TEST_ASSERT_EQUAL_UINT8(15, view_level(20, 10)); // and the outline is empty inside
  TEST_ASSERT_EQUAL_UINT8(0, view_level(20, 30));  // the solid part, at the bottom

  TEST_ASSERT_FALSE(ui::sparkline_draw(chart, samples, nullptr, 3, 16, 15));
  ui::release(chart);
}

// **A pie is four slices at most and its tones are the control's**: two equal slices are the two halves of the disc,
// the first tone is the one the app chose and the second walks four steps from it, so a legend can say the same
// numbers (docs/30, docs/90).
void test_a_pie_is_two_halves_when_the_slices_are_equal() {
  fresh_view();
  int8_t pie = -1;
  TEST_ASSERT_TRUE(ui::create(120, 120, 0, false, pie));
  TEST_ASSERT_TRUE(ui::place(pie, 0, 0));

  const uint16_t slices[2] = {1, 1};
  TEST_ASSERT_TRUE(ui::pie_draw(pie, slices, 2, 0, 15));
  // The disc starts at twelve o'clock and turns clockwise: the right half is the first slice.
  TEST_ASSERT_EQUAL_UINT8(0, view_level(89, 60));
  TEST_ASSERT_EQUAL_UINT8(4, view_level(31, 60));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(2, 2));  // the corner of the frame is not the pie

  // Four slices, the second one a quarter of the whole: the tone walks four steps per slice and the last one stops
  // at the white of the panel.
  const uint16_t four[4] = {2, 1, 1, 4};
  TEST_ASSERT_TRUE(ui::pie_draw(pie, four, 4, 0, 15));
  // The first slice is the quarter from twelve o'clock to three, and a point on the boundary at three belongs to the slice
  // that starts there: what is asked is a point inside the first quarter and not the one that ties.
  TEST_ASSERT_EQUAL_UINT8(0, view_level(80, 45));
  TEST_ASSERT_FALSE(ui::pie_draw(pie, slices, 2, 16, 15));
  ui::release(pie);
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
  RUN_TEST(test_the_window_shows_the_glass_in_the_colours_of_the_panel);
  RUN_TEST(test_the_ink_of_a_du_keeps_a_trace_and_a_gc16_cleans_it);
  RUN_TEST(test_a_gld16_leaves_a_trace_and_a_gc16_takes_it_away);
  RUN_TEST(test_the_trace_of_a_rich_page_is_richer_than_a_flat_one);
  RUN_TEST(test_a_paint_takes_the_time_of_its_quality);
  RUN_TEST(test_with_the_waits_off_a_paint_is_instant_and_still_leaves_ink);
  RUN_TEST(test_a_page_costs_what_the_board_says_a_page_costs);
  RUN_TEST(test_an_a2_flashes_the_page_before_the_image_arrives);
  RUN_TEST(test_a_gc16_blinks_before_the_image_arrives);
  RUN_TEST(test_the_glass_is_walked_while_the_paint_waits);
  RUN_TEST(test_only_the_board_that_has_a_glass_announces_one);
  RUN_TEST(test_the_t_koala_model_is_one_light_a_slot_and_four_megabytes_of_psram);
  RUN_TEST(test_a_pixel_of_the_window_lands_in_the_middle_of_its_block);
  RUN_TEST(test_the_window_is_the_page_at_the_scale_that_fits_or_the_one_asked_for);
  RUN_TEST(test_a_label_cuts_its_text_with_three_dots);
  RUN_TEST(test_a_label_places_its_line_by_alignment);
  RUN_TEST(test_a_button_sits_on_its_shadow_and_sinks_when_it_is_pressed);
  RUN_TEST(test_a_checkbox_is_a_box_with_a_tick_and_a_ring);
  RUN_TEST(test_a_checkbox_that_redraws_asks_its_child_to_draw_again);
  RUN_TEST(test_a_progress_bar_fills_a_prefix_of_its_track);
  RUN_TEST(test_a_slider_puts_the_knob_where_the_value_says);
  RUN_TEST(test_a_row_puts_its_detail_on_the_right_and_inverts_when_it_is_marked);
  RUN_TEST(test_a_panel_stacks_its_children_in_a_column);
  RUN_TEST(test_a_row_panel_splits_the_width_and_stretches_the_height);
  RUN_TEST(test_a_strip_of_tabs_has_no_line_under_the_chosen_one);
  RUN_TEST(test_a_button_is_the_drawing_of_the_paper_project);
  RUN_TEST(test_the_dots_of_a_page_draw_nothing_when_there_is_one_page);
  RUN_TEST(test_the_dots_of_a_page_are_rings_on_a_rod_and_the_open_one_is_solid);
  RUN_TEST(test_the_dots_of_a_page_keep_the_open_one_inside_a_narrow_strip);
  RUN_TEST(test_a_list_measures_its_pages_with_one_arithmetic);
  RUN_TEST(test_a_list_draws_one_page_and_says_which_one_it_is);
  RUN_TEST(test_the_keyboards_are_children_of_one_parent_and_are_chosen_by_name);
  RUN_TEST(test_a_textbox_keeps_the_caret_on_the_glass);
  RUN_TEST(test_the_window_of_a_textbox_carries_the_width_of_the_tail);
  RUN_TEST(test_a_dialog_wraps_its_message_and_has_two_doors);
  RUN_TEST(test_a_sparkline_scales_to_the_peak_of_its_window);
  RUN_TEST(test_a_pie_is_two_halves_when_the_slices_are_equal);
  RUN_TEST(test_a_calendar_puts_the_first_day_where_the_weekday_says);
  RUN_TEST(test_a_menu_hides_the_options_the_app_cannot_do);
  RUN_TEST(test_a_bar_of_menus_hangs_the_menu_under_its_word);
  RUN_TEST(test_a_box_paints_its_text_and_not_the_one_before_it);
  RUN_TEST(test_the_caret_of_a_box_of_lines_is_at_the_cursor_and_not_at_the_end_of_the_row);
  RUN_TEST(test_a_box_of_lines_keeps_the_caret_on_the_glass);
  RUN_TEST(test_a_box_of_lines_that_overflows_carries_a_bar_on_its_right);
  RUN_TEST(test_the_window_of_a_box_of_lines_is_the_one_the_app_says);
  RUN_TEST(test_a_selector_is_a_field_with_a_door_on_its_right);
  RUN_TEST(test_a_band_of_notifications_puts_the_newest_at_the_bottom);
  RUN_TEST(test_a_grid_puts_the_page_where_the_cursor_is);
  RUN_TEST(test_a_button_that_redraws_asks_its_child_to_draw_again);
  RUN_TEST(test_a_label_inside_a_button_is_clipped_to_the_button);
  RUN_TEST(test_a_turn_does_not_keep_the_damage_of_the_old_frame);
  RUN_TEST(test_the_keys_of_the_window_are_the_dial_of_the_board);
  RUN_TEST(test_the_keyboard_is_a_qwerty_the_finger_and_the_glass_agree_on);
  RUN_TEST(test_the_cleaner_between_apps_follows_the_page);
  return UNITY_END();
}
