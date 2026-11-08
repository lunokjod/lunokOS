// **The floor of the board's own RAM, held by a test** (0.151.20).
//
// The rule lives in `pal::take_from_the_extra` (`pal/pal.h`) and both PALs obey it: the chip's, which reads the
// internal pool, and the bench's, which has two homes of its own with a ceiling each. This suite is the reason the
// rule is in the PAL and not inside `pal_esp32`: the consequence that bricked the board —an app taking the pool the
// file system needs— can be held here, without a board, with the two ceilings the bench already knows how to set.
//
// What was measured on the Paper on 8/11, and is what these cases are shaped like: `mem` says the board's RAM has
// **125 464 B** free with nothing running, and the Lua machine of the reader asks for about **120 KB** in small
// tables and strings. With every block under 4 KB going home, `fopen` answered `ENOMEM` with 1,7 MB of PSRAM free and
// the flash could not be read again until the board was rebooted (docs/75).
#include <stddef.h>
#include <stdint.h>

#include <unity.h>

#include "pal/pal.h"

void setUp(void) {}

void tearDown(void) {}

namespace {

// A hundred and sixty thousand bytes of the board's RAM and a megabyte of extra memory: a small board with a slow
// home, which is the Paper with the numbers divided by two and is enough to see where a block goes.
const uint32_t kHomeBytes = 160 * 1024;
const uint32_t kExtraBytes = 1024 * 1024;

// **A block of two hundred and fifty-six bytes is what a table of Lua costs**: it is under everything anybody calls
// big, so it goes home while home keeps its floor.
const size_t kSmallBytes = 256;

// The blocks are kept here, because the offsets tell the homes apart without asking the PAL where a pointer lives:
// the home's free bytes go down while the blocks come from it and the extra memory's do not.
void* g_blocks[512];

void test_the_home_keeps_its_floor_and_the_app_goes_to_the_extra_memory(void) {
  pal::set_memory_bytes(kHomeBytes, kExtraBytes);
  const pal::MemoryLeft start_home = pal::memory_left(false);
  const pal::MemoryLeft start_extra = pal::memory_left(true);
  TEST_ASSERT_TRUE_MESSAGE(start_home.counted, "the bench did not take the ceilings");
  TEST_ASSERT_TRUE_MESSAGE(start_extra.counted, "the bench did not take the ceilings");

  size_t taken = 0;
  for (size_t i = 0; i < sizeof(g_blocks) / sizeof(g_blocks[0]); ++i) {
    g_blocks[i] = pal::extra_memory().allocate(kSmallBytes, false);
    TEST_ASSERT_NOT_NULL(g_blocks[i]);
    ++taken;
    // **The floor is not crossed, ever**: what is left of the board's RAM stays above the constant that the rule
    // uses, which is what a `FILE` and a DMA buffer find when they ask afterwards.
    TEST_ASSERT_TRUE_MESSAGE(pal::memory_left(false).free_bytes >= pal::kHomeReserveBytes,
                             "the app ate the board's RAM");
  }

  const pal::MemoryLeft home = pal::memory_left(false);
  const pal::MemoryLeft extra = pal::memory_left(true);
  // **The home gave what it had above the floor and no more**: 128 KB of blocks do not fit in 160 - 64.
  TEST_ASSERT_TRUE_MESSAGE(home.free_bytes >= pal::kHomeReserveBytes, "the home went under its floor");
  TEST_ASSERT_TRUE_MESSAGE(extra.free_bytes < start_extra.free_bytes, "the extra memory took nothing");
  TEST_ASSERT_TRUE_MESSAGE(home.lowest_free_bytes >= pal::kHomeReserveBytes, "the lowest of the home crossed it");
  // And the extra memory is where the rest went: it is the one that paid for the blocks over the floor.
  TEST_ASSERT_TRUE_MESSAGE(taken * kSmallBytes > start_home.free_bytes - home.free_bytes,
                           "the home took everything and the extra memory nothing");
}

void test_a_board_with_no_extra_memory_takes_everything_home(void) {
  // The ATOM and the DevKit have no second home: a floor they cannot jump over would be a board that refuses to
  // work, so the rule asks for the extra memory first and the same block falls back home (docs/75).
  pal::set_memory_bytes(kHomeBytes, 0);
  TEST_ASSERT_FALSE(pal::memory_left(true).counted);
  size_t taken = 0;
  for (size_t i = 0; i < 200; ++i) {
    g_blocks[i] = pal::extra_memory().allocate(kSmallBytes, false);
    TEST_ASSERT_NOT_NULL(g_blocks[i]);
    ++taken;
  }
  // Fifty thousand bytes of a home of a hundred and sixty thousand: all of them are home, floor and all, because
  // there is nowhere else to put them.
  TEST_ASSERT_EQUAL_UINT32(200, static_cast<uint32_t>(taken));
  TEST_ASSERT_TRUE_MESSAGE(pal::memory_left(false).free_bytes < kHomeBytes, "the blocks did not come from home");
}

void test_a_big_block_asks_for_the_extra_memory_first(void) {
  pal::set_memory_bytes(kHomeBytes, kExtraBytes);
  const pal::MemoryLeft start_home = pal::memory_left(false);
  const pal::MemoryLeft start_extra = pal::memory_left(true);
  void* page = pal::extra_memory().allocate(64 * 1024, true);
  TEST_ASSERT_NOT_NULL(page);
  const pal::MemoryLeft home = pal::memory_left(false);
  const pal::MemoryLeft extra = pal::memory_left(true);
  // **The block the caller called big pays the slow memory**, and the board's own RAM is untouched: that is what a
  // page of canvas and a chapter of a book do (docs/75).
  TEST_ASSERT_EQUAL_size_t(start_home.free_bytes, home.free_bytes);
  TEST_ASSERT_TRUE_MESSAGE(extra.free_bytes <= start_extra.free_bytes - 64 * 1024, "the page did not go to the extra");
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_the_home_keeps_its_floor_and_the_app_goes_to_the_extra_memory);
  RUN_TEST(test_a_board_with_no_extra_memory_takes_everything_home);
  RUN_TEST(test_a_big_block_asks_for_the_extra_memory_first);
  return UNITY_END();
}
