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
  return UNITY_END();
}
