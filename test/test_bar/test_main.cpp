// The bar of the system: the strip at the top that is nobody's app, and the view an app gets under it
// (docs/30, docs/50, docs/85).
//
// It is checked against the **emulator's own panel** (`emu::MemorySurface`), which speaks the same
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
  // **The board that has the panel** (`Model::glass`): an ATOM of the emulator has no screen, and
  // `display()` says so even with the option on (docs/50, docs/85).
  opts.board = "m5stack-paper";
  opts.display = true;
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
  TEST_ASSERT_EQUAL_UINT16(48, bar::height());
  TEST_ASSERT_EQUAL_UINT16(540 - 48, ui::height());
  TEST_ASSERT_EQUAL_UINT16(48, ui::canvas().y);
  TEST_ASSERT_EQUAL_UINT16(0, ui::canvas().x);
  // A present of the whole view cannot reach the bar, because the view does not start there.
  ui::attach();
  TEST_ASSERT_TRUE(ui::clear(0, "gc16"));
  TEST_ASSERT_TRUE(ui::present());
  const uint32_t paints = glass().paint_count();
  const emu::MemorySurface::Paint* last = glass().paint_at(paints - 1);
  TEST_ASSERT_NOT_NULL(last);
  TEST_ASSERT_EQUAL_UINT16(48, last->y);
  TEST_ASSERT_EQUAL_UINT16(540 - 48, last->h);
}

// The bar draws itself: white behind, a black line under it, and the hour on the left.
static void test_the_bar_paints_its_own_strip() {
  boot_with_a_panel();
  // The first paint of the bar is the whole strip.
  const emu::MemorySurface::Paint* first = glass().paint_at(0);
  TEST_ASSERT_NOT_NULL(first);
  TEST_ASSERT_EQUAL_UINT16(0, first->y);
  TEST_ASSERT_EQUAL_UINT16(48, first->h);
  TEST_ASSERT_EQUAL_UINT16(0, first->x);
  TEST_ASSERT_EQUAL_UINT16(960, first->w);
  // The line under the bar is black and the rows above it are white, except where a glyph is. The line is
  // the last two rows of the strip, which is 48 now and was 28.
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(0, 46));
  TEST_ASSERT_EQUAL_UINT8(0, glass().level_at(959, 47));
  TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(950, 2));
  // And the hour has ink in it: the first glyph of "HH:MM" starts at the left padding.
  bool ink = false;
  for (uint16_t y = 0; y < 46 && !ink; ++y) {
    for (uint16_t x = 12; x < 120 && !ink; ++x) {
      if (glass().level_at(x, y) == 0 && y < 46) ink = true;
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
  TEST_ASSERT_EQUAL_UINT16(960 - 48, ui::height());
  TEST_ASSERT_EQUAL_UINT16(48, ui::canvas().y);
  // And back, so the tests after this one see the frame they expect.
  TEST_ASSERT_TRUE(ui::set_rotation(0));
  TEST_ASSERT_EQUAL_UINT16(960, ui::width());
  TEST_ASSERT_EQUAL_UINT16(540 - 48, ui::height());
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

// **A new title is the middle of the bar and not the bar** (0.85.2): the box between the hour and the charge is
// cleaned and drawn again, with the two-level wash, and the hour and the charge are not touched. A whole strip of
// `gc16` for a word that changed was four blinks of the bar every time an app opened (docs/50, docs/85).
static void test_a_new_title_repaints_the_bar() {
  boot_with_a_panel();
  const uint32_t before = glass().paint_count();
  bar::title("SQUARES");
  bar::poll();
  TEST_ASSERT_EQUAL_UINT32(before + 1, glass().paint_count());
  const emu::MemorySurface::Paint* paint = glass().paint_at(before);
  TEST_ASSERT_NOT_NULL(paint);
  TEST_ASSERT_EQUAL(hal::UpdateMode::kGld16, paint->mode);  // cleans, and does not blink
  // The box: after the hour and its air (12 + "00:00" of 116 + 8) and before the charge and its own (92 + 8 + 12),
  // on the rows of the text and not on the whole strip.
  // **The box is the middle of the bar and not the whole of it**: after the hour and its air —12 and "00:00" of 87 at
  // three steps of the table of the flash— and before the charge, on the rows of a line of the face that is on the
  // glass now: twenty-one pixels, which is seven times three with the table of the bits (docs/85).
  TEST_ASSERT_EQUAL_UINT16(107, paint->x);
  TEST_ASSERT_EQUAL_UINT16(6, paint->y);
  TEST_ASSERT_EQUAL_UINT16(764, paint->w);
  TEST_ASSERT_EQUAL_UINT16(21, paint->h);
  // And the same title again is not a change.
  const uint32_t after = glass().paint_count();
  bar::title("SQUARES");
  bar::poll();
  TEST_ASSERT_EQUAL_UINT32(after, glass().paint_count());
}

// **The hour is its own box too**, and the ten-minute full wash is gone: a minute is a digit and a digit is a box
// cleaned without a blink, not a strip blinking four times (0.85.2, docs/50, docs/92).
static void test_a_new_minute_repaints_the_hour_and_not_the_strip() {
  boot_with_a_panel();
  bar::poll();
  hal::IClock* clock = hal::platform().clock();
  TEST_ASSERT_NOT_NULL(clock);
  const int64_t was = clock->epoch_ms();
  TEST_ASSERT_TRUE(clock->set_epoch_ms(was + 60000));
  // The bar reads the chip every two seconds, so the hour is looked at again after them: the turn of the emulator is a
  // wait on the queue, which is what a turn with nothing to do is (docs/85).
  const uint32_t before = glass().paint_count();
  pal::Event event;
  pal::system().queue()->pop(event, 2100);
  bar::poll();
  TEST_ASSERT_EQUAL_UINT32(before + 1, glass().paint_count());
  const emu::MemorySurface::Paint* paint = glass().paint_at(before);
  TEST_ASSERT_NOT_NULL(paint);
  TEST_ASSERT_EQUAL(hal::UpdateMode::kGld16, paint->mode);
  TEST_ASSERT_EQUAL_UINT16(12, paint->x);
  TEST_ASSERT_EQUAL_UINT16(6, paint->y);
  TEST_ASSERT_EQUAL_UINT16(87, paint->w);
  TEST_ASSERT_EQUAL_UINT16(21, paint->h);
}

// **The strip is whole when the glass under it moved**: a turn or a new size is not a box, and the whole bar is
// washed with the quality that leaves no history (0.85.2).
static void test_a_turn_paints_the_whole_strip() {
  boot_with_a_panel();
  bar::poll();
  const uint32_t before = glass().paint_count();
  bar::refresh();
  bar::poll();
  TEST_ASSERT_EQUAL_UINT32(before + 1, glass().paint_count());
  const emu::MemorySurface::Paint* paint = glass().paint_at(before);
  TEST_ASSERT_NOT_NULL(paint);
  TEST_ASSERT_EQUAL(hal::UpdateMode::kGc16, paint->mode);
  TEST_ASSERT_EQUAL_UINT16(0, paint->x);
  TEST_ASSERT_EQUAL_UINT16(0, paint->y);
  TEST_ASSERT_EQUAL_UINT16(48, paint->h);
}

// **A title too long for the middle is cut at the border of its box and does not run over the hour.**
// The box is what is left between the hour and the charge, and the strip is measured on the portrait
// page of the board (540 wide), where the longest title a manifest can carry —47 characters, 562
// pixels— is wider than the room it has. It was not always so: a title that did not fit was dropped
// whole, and before that it ran under the hour (docs/30, docs/90).
static void test_a_long_title_is_cut_at_its_box() {
  boot_with_a_panel();
  TEST_ASSERT_TRUE(ui::set_rotation(270));
  bar::title("");
  bar::poll();
  bar::title("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
  bar::poll();

  // **The hour, the air and the title, found by their ink and not by a number of pixels**: the width of "HH:MM" is the
  // face's business, and a bar whose face changes is not a broken bar (0.97.2, docs/85). The rows are the ones of the
  // letters and not the whole strip, because the line at the bottom is ink in every column.
  const uint16_t text_top = 6;
  const uint16_t text_bottom = 27;
  // The hour is the first run of ink and the title the next one, after a hole of the air of the bar: the widths are the
  // face's business and this way the test does not carry a number that a face change moves.
  int hour_right = -1;
  int title_left = -1;
  int hole = 0;
  bool in_ink = false;
  for (uint16_t x = 0; x < 400 && title_left < 0; ++x) {
    bool ink = false;
    for (uint16_t y = text_top; y < text_bottom; ++y) {
      if (glass().level_at(x, y) == 0) {
        ink = true;
        break;
      }
    }
    if (ink) {
      if (in_ink && hole >= 6) {
        title_left = static_cast<int>(x);
        break;
      }
      in_ink = true;
      hole = 0;
      hour_right = static_cast<int>(x);
    } else if (in_ink) {
      ++hole;
    }
  }
  TEST_ASSERT_GREATER_THAN(0, hour_right);
  // The title starts after the hour and its eight pixels of air.
  TEST_ASSERT_GREATER_THAN(hour_right + 4, title_left);
  // And between the two the strip is still paper: the title did not run over the hour.
  for (uint16_t x = static_cast<uint16_t>(hour_right + 1); x < static_cast<uint16_t>(title_left); ++x) {
    for (uint16_t y = text_top; y < text_bottom; ++y) {
      TEST_ASSERT_EQUAL_UINT8(15, glass().level_at(x, y));
    }
  }
  // Back, so the tests after this one see the frame they expect.
  TEST_ASSERT_TRUE(ui::set_rotation(0));
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

// **An app that asks for the whole glass gets it** (0.136.0, docs/46 D1): the strip of the system goes away, the view is
// the panel and it starts on the first row, and a page drawn in it covers the rows the hour used to have.
static void test_an_app_that_asks_for_the_whole_glass_gets_it() {
  boot_with_a_panel();
  ui::attach();
  TEST_ASSERT_FALSE(ui::is_full_screen());
  TEST_ASSERT_TRUE(ui::full_screen(true));
  TEST_ASSERT_TRUE(ui::is_full_screen());
  TEST_ASSERT_EQUAL_UINT16(960, ui::canvas().width);
  TEST_ASSERT_EQUAL_UINT16(540, ui::canvas().height);
  TEST_ASSERT_EQUAL_UINT16(0, ui::canvas().x);
  TEST_ASSERT_EQUAL_UINT16(0, ui::canvas().y);
  // **And the strip is not painted any more**: a title that changes is a title nobody draws, because the bar is not on
  // the glass at all.
  bar::poll();
  const uint32_t before = glass().paint_count();
  bar::title("WHOLE");
  bar::poll();
  TEST_ASSERT_EQUAL_UINT32(before, glass().paint_count());
  // The page of the app reaches the first row of the glass, which is where the hour was.
  TEST_ASSERT_TRUE(ui::clear(0, "gc16"));
  TEST_ASSERT_TRUE(ui::present());
  const emu::MemorySurface::Paint* last = glass().paint_at(glass().paint_count() - 1);
  TEST_ASSERT_NOT_NULL(last);
  TEST_ASSERT_EQUAL_UINT16(0, last->y);
  TEST_ASSERT_EQUAL_UINT16(540, last->h);
  // Nobody's title for the tests after this one, and the glass given back.
  bar::title("");
  TEST_ASSERT_TRUE(ui::full_screen(false));
}

// **And the bar comes back with the view it owns** (0.136.0, docs/46 D1): asking for the glass back makes the view the one
// of every app —the panel under the strip— and the strip is painted whole, because what is under it is a page and not the
// bar any more.
static void test_the_bar_comes_back_when_the_app_gives_the_glass_back() {
  boot_with_a_panel();
  ui::attach();
  TEST_ASSERT_TRUE(ui::full_screen(true));
  TEST_ASSERT_TRUE(ui::full_screen(false));
  TEST_ASSERT_FALSE(ui::is_full_screen());
  TEST_ASSERT_EQUAL_UINT16(960, ui::canvas().width);
  TEST_ASSERT_EQUAL_UINT16(540 - 48, ui::canvas().height);
  TEST_ASSERT_EQUAL_UINT16(48, ui::canvas().y);
  const uint32_t before = glass().paint_count();
  bar::poll();
  TEST_ASSERT_EQUAL_UINT32(before + 1, glass().paint_count());
  const emu::MemorySurface::Paint* paint = glass().paint_at(before);
  TEST_ASSERT_NOT_NULL(paint);
  TEST_ASSERT_EQUAL(hal::UpdateMode::kGc16, paint->mode);
  TEST_ASSERT_EQUAL_UINT16(0, paint->x);
  TEST_ASSERT_EQUAL_UINT16(0, paint->y);
  TEST_ASSERT_EQUAL_UINT16(48, paint->h);
}

// **An app that ends gives the glass back, however it ends** (0.136.0, docs/46 D1): the shell says so when the app is
// gone —by its own will or by an error— and `attach` promises the view of every app to the one that comes next, so a page
// that died with the whole glass cannot leave the system without its hour.
static void test_the_view_of_every_app_comes_back_when_the_app_ends() {
  boot_with_a_panel();
  ui::attach();
  TEST_ASSERT_TRUE(ui::full_screen(true));
  ui::give_back_the_glass();
  TEST_ASSERT_FALSE(ui::is_full_screen());
  TEST_ASSERT_EQUAL_UINT16(48, ui::canvas().y);
  TEST_ASSERT_EQUAL_UINT16(540 - 48, ui::canvas().height);
  // And the app that comes next is attached in the view of every app even when the one before did not give it back.
  TEST_ASSERT_TRUE(ui::full_screen(true));
  ui::attach();
  TEST_ASSERT_FALSE(ui::is_full_screen());
  TEST_ASSERT_EQUAL_UINT16(48, ui::canvas().y);
  TEST_ASSERT_EQUAL_UINT16(540 - 48, ui::canvas().height);
}

// **The whole glass turns with the page**: a page that took the panel keeps the whole panel when it is turned, and the
// rows of the bar do not come back behind its back (0.136.0, docs/30).
static void test_the_whole_glass_turns_with_the_page() {
  boot_with_a_panel();
  ui::attach();
  TEST_ASSERT_TRUE(ui::full_screen(true));
  TEST_ASSERT_TRUE(ui::set_rotation(90));
  TEST_ASSERT_EQUAL_UINT16(540, ui::width());
  TEST_ASSERT_EQUAL_UINT16(960, ui::canvas().height);
  TEST_ASSERT_EQUAL_UINT16(0, ui::canvas().y);
  // Back, so the tests after this one see the frame they expect.
  TEST_ASSERT_TRUE(ui::set_rotation(0));
  TEST_ASSERT_EQUAL_UINT16(540, ui::canvas().height);
  TEST_ASSERT_TRUE(ui::full_screen(false));
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
  RUN_TEST(test_a_new_minute_repaints_the_hour_and_not_the_strip);
  RUN_TEST(test_a_turn_paints_the_whole_strip);
  RUN_TEST(test_a_long_title_is_cut_at_its_box);
  RUN_TEST(test_the_title_goes_away_with_the_app);
  RUN_TEST(test_an_app_that_asks_for_the_whole_glass_gets_it);
  RUN_TEST(test_the_bar_comes_back_when_the_app_gives_the_glass_back);
  RUN_TEST(test_the_view_of_every_app_comes_back_when_the_app_ends);
  RUN_TEST(test_the_whole_glass_turns_with_the_page);
  return UNITY_END();
}
