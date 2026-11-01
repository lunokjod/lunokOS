// The bar of the system: the strip at the top that is nobody's app, and the view an app gets under it
// (docs/30, docs/50, docs/85).
//
// It is checked against the **bench's own panel** (`emu::MemorySurface`), which speaks the same
// `hal::ISurface` contract as the Paper's controller and remembers each paint it was asked for: a bar
// that paints when nothing changed, or that does not paint when the hour did, is a failed assertion
// here instead of a page nobody can read on the board.
#include <stddef.h>
#include <string.h>

#include <unity.h>

#include "emu/options.h"
#include "emu/surface.h"
#include "hal/hal.h"
#include "kernel/bar.h"
#include "kernel/display.h"
#include "kernel/ui.h"
#include "pal/pal.h"

namespace bar = kernel::bar;
namespace ui = kernel::ui;

void setUp(void) {}

void tearDown(void) {}

namespace {

bool g_booted = false;

void boot_with_a_panel() {
  if (g_booted) return;
  emu::Options opts = emu::options();
  opts.display = true;
  opts.read_delay_us = 0;
  emu::set_options(opts);
  kernel::display::boot(hal::platform());
  kernel::display::set_rotation(0);  // the glass' own frame; the portrait page is tried below
  bar::boot(hal::platform());
  ui::boot(hal::platform(), bar::height());
  g_booted = true;
}

emu::MemorySurface& glass() { return *emu::memory_surface(); }

}  // namespace

// The bar keeps the top rows of the glass and the app's view starts under them: a page that goes wrong
// cannot take the hour with it (docs/30).
static void test_the_bar_keeps_the_top_of_the_glass() {
  boot_with_a_panel();
  TEST_ASSERT_EQUAL_UINT16(28, bar::height());
  TEST_ASSERT_EQUAL_UINT16(540 - 28, ui::height());
  TEST_ASSERT_EQUAL_UINT16(28, ui::canvas().y);
  TEST_ASSERT_EQUAL_UINT16(0, ui::canvas().x);
  // A present of the whole view cannot reach the bar, because the view does not start there.
  ui::attach();
  TEST_ASSERT_TRUE(ui::clear(0, "gc16"));
  TEST_ASSERT_TRUE(ui::present());
  const uint32_t paints = glass().paint_count();
  const emu::MemorySurface::Paint* last = glass().paint_at(paints - 1);
  TEST_ASSERT_NOT_NULL(last);
  TEST_ASSERT_EQUAL_UINT16(28, last->y);
  TEST_ASSERT_EQUAL_UINT16(540 - 28, last->h);
}

// The bar draws itself: white behind, a black line under it, and the hour on the left.
static void test_the_bar_paints_its_own_strip() {
  boot_with_a_panel();
  // The first paint of the bar is the whole strip.
  const emu::MemorySurface::Paint* first = glass().paint_at(0);
  TEST_ASSERT_NOT_NULL(first);
  TEST_ASSERT_EQUAL_UINT16(0, first->y);
  TEST_ASSERT_EQUAL_UINT16(28, first->h);
  TEST_ASSERT_EQUAL_UINT16(0, first->x);
  TEST_ASSERT_EQUAL_UINT16(960, first->w);
  // The line under the bar is black and the rows above it are white, except where a glyph is.
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(0, 26));
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(959, 27));
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(950, 2));
  // And the hour has ink in it: the first glyph of "HH:MM" starts at the left padding.
  bool ink = false;
  for (uint16_t y = 0; y < 28 && !ink; ++y) {
    for (uint16_t x = 8; x < 70 && !ink; ++x) {
      if (glass().level_at(x, y) == 0 && y < 26) ink = true;
    }
  }
  TEST_ASSERT_TRUE(ink);
}

// **The page turns and the bar goes with it**: the same call moves the glass, the strip and the view,
// because they all read the one value (docs/50).
static void test_turning_the_page_turns_the_bar_and_the_view() {
  boot_with_a_panel();
  TEST_ASSERT_TRUE(ui::set_rotation(90));
  TEST_ASSERT_EQUAL_UINT16(90, ui::rotation());
  TEST_ASSERT_EQUAL_UINT16(540, ui::width());
  TEST_ASSERT_EQUAL_UINT16(960 - 28, ui::height());
  TEST_ASSERT_EQUAL_UINT16(28, ui::canvas().y);
  // And back, so the tests after this one see the frame they expect.
  TEST_ASSERT_TRUE(ui::set_rotation(0));
  TEST_ASSERT_EQUAL_UINT16(960, ui::width());
  TEST_ASSERT_EQUAL_UINT16(540 - 28, ui::height());
}

// A turn where nothing changed paints nothing: the glass is paid by the repaint, not by the clock.
static void test_a_turn_with_nothing_new_paints_nothing() {
  boot_with_a_panel();
  // **The turn of the test before this one left a repaint owed**, and the next poll pays it: a strip that
  // has to be redrawn cannot be drawn by a poll that refuses to work. The count is taken after that, so
  // what is measured here is a bar with nothing new (found by this suite, 1/11).
  bar::poll();
  const uint32_t before = glass().paint_count();
  bar::poll();
  bar::poll();
  TEST_ASSERT_EQUAL_UINT32(before, glass().paint_count());
}

// A new title is a new middle of the bar, and it is painted at once.
static void test_a_new_title_repaints_the_bar() {
  boot_with_a_panel();
  const uint32_t before = glass().paint_count();
  bar::title("SQUARES");
  bar::poll();
  TEST_ASSERT_EQUAL_UINT32(before + 1, glass().paint_count());
  const emu::MemorySurface::Paint* paint = glass().paint_at(before);
  TEST_ASSERT_NOT_NULL(paint);
  TEST_ASSERT_EQUAL(hal::UpdateMode::kGc16, paint->mode);  // a new title is worth a clean paint
  TEST_ASSERT_EQUAL_UINT16(0, paint->y);
  TEST_ASSERT_EQUAL_UINT16(28, paint->h);
  // And the same title again is not a change.
  const uint32_t after = glass().paint_count();
  bar::title("SQUARES");
  bar::poll();
  TEST_ASSERT_EQUAL_UINT32(after, glass().paint_count());
}

// The title of nobody is the bar with no title, and it is a change like any other.
static void test_the_title_goes_away_with_the_app() {
  boot_with_a_panel();
  bar::title("PANEL");
  bar::poll();
  const uint32_t before = glass().paint_count();
  bar::title("");
  bar::poll();
  TEST_ASSERT_EQUAL_UINT32(before + 1, glass().paint_count());
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_the_bar_keeps_the_top_of_the_glass);
  RUN_TEST(test_the_bar_paints_its_own_strip);
  RUN_TEST(test_turning_the_page_turns_the_bar_and_the_view);
  RUN_TEST(test_a_turn_with_nothing_new_paints_nothing);
  RUN_TEST(test_a_new_title_repaints_the_bar);
  RUN_TEST(test_the_title_goes_away_with_the_app);
  return UNITY_END();
}
