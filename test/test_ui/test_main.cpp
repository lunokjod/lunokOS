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
static void test_a_button_paints_a_frame_and_a_second_border_when_pressed() {
  fresh_view();
  int8_t slot = -1;
  TEST_ASSERT_TRUE(ui::create(40, 20, 0, false, slot));
  TEST_ASSERT_TRUE(ui::place(slot, 10, 10));
  TEST_ASSERT_TRUE(ui::button_draw(slot, 15, 0, false));
  // **Six pixels of corner on a button twenty tall**: the corner of the frame is the page, the top edge is
  // ink, and the body is inside the two pixels of border.
  TEST_ASSERT_EQUAL_UINT8(15, view_level(10, 10));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(16, 10));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(29, 10));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(10, 20));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(15, 20));
  // The bevel of a white button is the shadow along its bottom, and held it is the light.
  TEST_ASSERT_EQUAL_UINT8(11, view_level(29, 27));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(29, 25));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(9, 9));
  TEST_ASSERT_TRUE(ui::button_draw(slot, 15, 0, true));
  TEST_ASSERT_EQUAL_UINT8(7, view_level(29, 12));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(29, 27));
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

  // **A column takes 96 of window** (a button of 80 and two margins of 8), a row takes 70 of height, the
  // buttons are 54 tall with 8 between: a column of three is 178 and one button is 54.
  TEST_ASSERT_EQUAL_UINT16(96, emu::gui_key_strip_width());
  TEST_ASSERT_EQUAL_UINT16(70, emu::gui_key_row_height());
  TEST_ASSERT_EQUAL_UINT16(54 * 3 + 8 * 2, emu::gui_keys_height(3));
  TEST_ASSERT_EQUAL_UINT16(54, emu::gui_keys_height(1));
  TEST_ASSERT_EQUAL_UINT16(0, emu::gui_keys_height(0));
  TEST_ASSERT_EQUAL_UINT16(80 * 3 + 8 * 2, emu::gui_keys_width(3));
  // And both are **centred**: the column of three in a window of 480 starts at 151 (178 + 151 + 151 = 480),
  // and the row of three in a page of 366 starts at 55 ((366 - 256) / 2).
  TEST_ASSERT_EQUAL_INT(151, emu::gui_keys_top(480, 3));
  TEST_ASSERT_EQUAL_INT(213, emu::gui_keys_top(480, 1));
  TEST_ASSERT_EQUAL_INT(55, emu::gui_keys_left(366, 3));
  TEST_ASSERT_EQUAL_INT(95, emu::gui_keys_left(270, 1));
  // A window smaller than the keys starts them at the edge instead of cutting the first button.
  TEST_ASSERT_EQUAL_INT(0, emu::gui_keys_top(100, 3));
  TEST_ASSERT_EQUAL_INT(0, emu::gui_keys_left(100, 3));

  // **A dial on the left face is a column down the left**: the strip is 0..96 and the buttons 8..88.
  const int top = emu::gui_keys_top(480, 3);
  TEST_ASSERT_EQUAL_INT(1, emu::gui_key_at(48, top + 27, 3, 366, 480, emu::KeySide::kLeft));
  TEST_ASSERT_EQUAL_INT(0, emu::gui_key_at(48, top + 54 + 8 + 27, 3, 366, 480, emu::KeySide::kLeft));
  TEST_ASSERT_EQUAL_INT(2, emu::gui_key_at(48, top + 2 * (54 + 8) + 27, 3, 366, 480, emu::KeySide::kLeft));
  // A click in a gap, on the margin, above the column, past the last button or on the page is no key at all.
  TEST_ASSERT_EQUAL_INT(-1, emu::gui_key_at(48, top + 54 + 2, 3, 366, 480, emu::KeySide::kLeft));
  TEST_ASSERT_EQUAL_INT(-1, emu::gui_key_at(4, top + 27, 3, 366, 480, emu::KeySide::kLeft));
  TEST_ASSERT_EQUAL_INT(-1, emu::gui_key_at(48, top - 1, 3, 366, 480, emu::KeySide::kLeft));
  TEST_ASSERT_EQUAL_INT(-1, emu::gui_key_at(48, top + 178, 3, 366, 480, emu::KeySide::kLeft));
  TEST_ASSERT_EQUAL_INT(-1, emu::gui_key_at(200, top + 27, 3, 366, 480, emu::KeySide::kLeft));
  // **The same dial on the right face is the same column on the other side**: the buttons are 278..358 and
  // the left of the window is the page.
  TEST_ASSERT_EQUAL_INT(1, emu::gui_key_at(278 + 40, top + 27, 3, 366, 480, emu::KeySide::kRight));
  TEST_ASSERT_EQUAL_INT(0, emu::gui_key_at(278 + 40, top + 54 + 8 + 27, 3, 366, 480, emu::KeySide::kRight));
  TEST_ASSERT_EQUAL_INT(-1, emu::gui_key_at(48, top + 27, 3, 366, 480, emu::KeySide::kRight));
  // **A key on the top face is a row under the page**: the row is the last 70 rows of the window, the
  // buttons 54 tall and 80 wide with 8 between, starting at 7 of a 270-wide window.
  const int row_top = 550 - 70 + 8;
  TEST_ASSERT_EQUAL_INT(1, emu::gui_key_at(47, row_top + 27, 3, 270, 550, emu::KeySide::kBottom));
  TEST_ASSERT_EQUAL_INT(0, emu::gui_key_at(135, row_top + 27, 3, 270, 550, emu::KeySide::kBottom));
  TEST_ASSERT_EQUAL_INT(2, emu::gui_key_at(223, row_top + 27, 3, 270, 550, emu::KeySide::kBottom));
  TEST_ASSERT_EQUAL_INT(-1, emu::gui_key_at(91, row_top + 27, 3, 270, 550, emu::KeySide::kBottom));
  TEST_ASSERT_EQUAL_INT(-1, emu::gui_key_at(47, 480, 3, 270, 550, emu::KeySide::kBottom));
  TEST_ASSERT_EQUAL_INT(-1, emu::gui_key_at(0, row_top + 27, 3, 270, 550, emu::KeySide::kBottom));
  // A board of one key has its button in the middle of the window, and nothing under it.
  TEST_ASSERT_EQUAL_INT(0, emu::gui_key_at(48, 240, 1, 366, 480, emu::KeySide::kLeft));
  TEST_ASSERT_EQUAL_INT(-1, emu::gui_key_at(48, 400, 1, 366, 480, emu::KeySide::kLeft));
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

// **A button comes out of the page**: twelve pixels of round corner, a frame of three and a bevel of two, a
// shadow on the bottom and the right that swaps to the top and the left while it is held, and a focus that is
// a white gap and a black band of seven inside the frame —a double frame no one can take for the border—. It
// is a test of pixels and not of counts, because what changed is what a person sees (docs/30, docs/90).
void test_a_button_has_a_round_corner_a_bevel_and_a_ring() {
  fresh_view();
  int8_t button = -1;
  TEST_ASSERT_TRUE(ui::create(200, 100, -1, false, button));
  TEST_ASSERT_TRUE(button >= 0);
  TEST_ASSERT_TRUE(ui::place(button, 40, 40));

  TEST_ASSERT_TRUE(ui::button_draw(button, 15, 0, false, false));
  // **The corner of the frame is not the border**: the box is rounded, so the pixel of the corner is the view
  // behind it and not ink. The middle of the same top edge is ink, three pixels of it.
  TEST_ASSERT_EQUAL_UINT8(15, view_level(40, 40));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(44, 40));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(52, 40));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(140, 40));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(140, 41));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(140, 42));  // the frame is three pixels now
  TEST_ASSERT_EQUAL_UINT8(15, view_level(140, 43));  // the body, white
  // **The volume of a light button is its shadow**: a white button has no lighter edge to draw, so what says
  // it comes out of the page is a grey line of two pixels along the bottom, inside the frame.
  TEST_ASSERT_EQUAL_UINT8(11, view_level(140, 136));
  TEST_ASSERT_EQUAL_UINT8(11, view_level(140, 135));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(140, 134));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(140, 137));  // and the frame below it
  // **And the dark one's is the highlight**, which is the same idea the other way round.
  TEST_ASSERT_TRUE(ui::button_draw(button, 0, 15, false, false));
  TEST_ASSERT_EQUAL_UINT8(4, view_level(140, 43));
  TEST_ASSERT_EQUAL_UINT8(4, view_level(140, 44));
  // **Held, the two swap**: the light goes to the bottom and the shadow to the top, which is a button pushed in.
  TEST_ASSERT_TRUE(ui::button_draw(button, 0, 15, true, false));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(140, 42));  // the border is still the border
  TEST_ASSERT_EQUAL_UINT8(0, view_level(140, 43));
  TEST_ASSERT_EQUAL_UINT8(4, view_level(140, 136));
  // **And on a white button the hole has a mouth**: the shadow is as many pixels of a much darker level as the
  // frame is thick and not one line of the grey that was there when it was out, which is what a person reads
  // as pressed (docs/90).
  TEST_ASSERT_TRUE(ui::button_draw(button, 15, 0, true, false));
  TEST_ASSERT_EQUAL_UINT8(7, view_level(140, 43));
  TEST_ASSERT_EQUAL_UINT8(7, view_level(140, 44));
  TEST_ASSERT_EQUAL_UINT8(7, view_level(140, 45));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(140, 46));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(140, 42));  // the border is still the border, and it is black here
  // **The ring of the focus**: a white gap of two after the border and seven black pixels after it, and the
  // body again inside them. It is the double frame a person reads as "this is the one" (docs/30, docs/90).
  TEST_ASSERT_TRUE(ui::button_draw(button, 15, 0, false, true));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(140, 44));  // the pause, which is the light edge of the bevel
  TEST_ASSERT_EQUAL_UINT8(0, view_level(140, 45));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(140, 47));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(140, 48));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(140, 49));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(140, 50));
  TEST_ASSERT_EQUAL_UINT8(0, view_level(140, 51));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(140, 52));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(140, 70));  // and the middle of the button is untouched
  // Without the focus those seven pixels are the body, which is what makes the ring the focus and not a border.
  TEST_ASSERT_TRUE(ui::button_draw(button, 15, 0, false, false));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(140, 46));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(140, 51));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_without_an_app_there_is_no_view);
  RUN_TEST(test_the_canvas_is_the_glass_and_starts_white);
  RUN_TEST(test_a_button_has_a_round_corner_a_bevel_and_a_ring);
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
  RUN_TEST(test_the_trace_of_a_rich_page_is_richer_than_a_flat_one);
  RUN_TEST(test_a_paint_takes_the_time_of_its_quality);
  RUN_TEST(test_with_the_waits_off_a_paint_is_instant_and_still_leaves_ink);
  RUN_TEST(test_a_page_costs_what_the_board_says_a_page_costs);
  RUN_TEST(test_an_a2_flashes_the_page_before_the_image_arrives);
  RUN_TEST(test_a_gc16_blinks_before_the_image_arrives);
  RUN_TEST(test_the_glass_is_walked_while_the_paint_waits);
  RUN_TEST(test_only_the_board_that_has_a_glass_announces_one);
  RUN_TEST(test_a_pixel_of_the_window_lands_in_the_middle_of_its_block);
  RUN_TEST(test_the_window_is_the_page_at_the_scale_that_fits_or_the_one_asked_for);
  RUN_TEST(test_a_label_cuts_its_text_with_three_dots);
  RUN_TEST(test_a_label_places_its_line_by_alignment);
  RUN_TEST(test_a_button_paints_a_frame_and_a_second_border_when_pressed);
  RUN_TEST(test_a_button_that_redraws_asks_its_child_to_draw_again);
  RUN_TEST(test_a_label_inside_a_button_is_clipped_to_the_button);
  RUN_TEST(test_a_turn_does_not_keep_the_damage_of_the_old_frame);
  RUN_TEST(test_the_keys_of_the_window_are_the_dial_of_the_board);
  RUN_TEST(test_the_cleaner_between_apps_follows_the_page);
  return UNITY_END();
}
