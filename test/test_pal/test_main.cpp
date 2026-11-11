// **The floor of the board's own RAM, held by a test** (0.151.20).
//
// The rule lives in `pal::take_from_the_extra` (`pal/pal.h`) and both PALs obey it: the chip's, which reads the
// internal pool, and the emulator's, which has two homes of its own with a ceiling each. This suite is the reason the
// rule is in the PAL and not inside `pal_esp32`: the consequence that bricked the board —an app taking the pool the
// file system needs— can be held here, without a board, with the two ceilings the emulator already knows how to set.
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
  TEST_ASSERT_TRUE_MESSAGE(start_home.counted, "the emulator did not take the ceilings");
  TEST_ASSERT_TRUE_MESSAGE(start_extra.counted, "the emulator did not take the ceilings");

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

// **The relief of the allocator is a failed ask and nothing else** (0.179.6, and the owner said the rule with the log in
// front of him: *«Com es decideix si falta ram? Només s'ha de fer si falla una alocació de memoria d'una app o sistema.
// (s'esborra i es reintenta de forma transparent per l'app)»*). There is no threshold of free bytes behind it: a block that
// fits is never paid for with somebody's cache, and the one ask that failed is tried again as it was.
int g_reliefs = 0;
void* g_kept = nullptr;
size_t g_asked = 0;

void a_relief_that_writes_it_down(void* keep, size_t bytes) {
  ++g_reliefs;
  g_kept = keep;
  g_asked = bytes;
}

void test_the_relief_is_a_failed_ask_and_not_a_heap_that_looks_full(void) {
  pal::set_memory_bytes(kHomeBytes, kExtraBytes);
  pal::set_memory_relief(&a_relief_that_writes_it_down);
  g_reliefs = 0;
  g_kept = nullptr;
  g_asked = 0;

  // **A block that fits runs nothing**: it is the whole rule, and the ceilings are already low enough to be tempted by a
  // heap that "looks full" —the extra memory is a megabyte and the home keeps its floor—.
  void* small = pal::extra_memory().allocate(kSmallBytes, true);
  TEST_ASSERT_NOT_NULL(small);
  TEST_ASSERT_EQUAL_INT(0, g_reliefs);

  // **Ask until the two homes say no**: that one ask runs the relief exactly once, with no block to keep and with the bytes
  // it wanted, and it is tried again after the relief —which frees nothing here, so the answer is the no the app gets—.
  const size_t big = 64 * 1024;
  void* taken[64];
  size_t count = 0;
  while (count < sizeof(taken) / sizeof(taken[0])) {
    void* block = pal::extra_memory().allocate(big, true);
    if (block == nullptr) break;
    taken[count++] = block;
  }
  TEST_ASSERT_TRUE(count > 0);
  TEST_ASSERT_EQUAL_INT(1, g_reliefs);
  TEST_ASSERT_NULL(g_kept);
  TEST_ASSERT_EQUAL_size_t(big, g_asked);

  for (size_t i = 0; i < count; ++i) pal::extra_memory().release(taken[i]);
  pal::extra_memory().release(small);
  pal::set_memory_relief(nullptr);
}

void test_a_block_that_is_being_moved_is_not_one_the_relief_may_take(void) {
  pal::set_memory_bytes(kHomeBytes, kExtraBytes);
  pal::set_memory_relief(&a_relief_that_writes_it_down);
  g_reliefs = 0;
  g_kept = nullptr;

  // The extra memory full to the brim, and one of the blocks with a mark of its own in it.
  const size_t big = 64 * 1024;
  void* taken[64];
  size_t count = 0;
  while (count < sizeof(taken) / sizeof(taken[0])) {
    void* block = pal::extra_memory().allocate(big, true);
    if (block == nullptr) break;
    taken[count++] = block;
  }
  TEST_ASSERT_TRUE(count > 0);
  unsigned char* mine = static_cast<unsigned char*>(taken[0]);
  mine[0] = 0x11;
  mine[big - 1] = 0x22;

  // **A growth that cannot happen** asks the relief about the very block it wants to move: `keep` is that block, and the
  // block is still the app's when the answer is no —a relief that gave it back would leave the allocator moving a block that
  // is not there any more—.
  g_reliefs = 0;
  void* grown = pal::extra_memory().resize(taken[0], big * 2, true);
  TEST_ASSERT_NULL(grown);
  TEST_ASSERT_EQUAL_INT(1, g_reliefs);
  TEST_ASSERT_EQUAL_PTR(taken[0], g_kept);
  TEST_ASSERT_EQUAL_UINT8(0x11, mine[0]);
  TEST_ASSERT_EQUAL_UINT8(0x22, mine[big - 1]);

  for (size_t i = 0; i < count; ++i) pal::extra_memory().release(taken[i]);
  pal::set_memory_relief(nullptr);
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_the_home_keeps_its_floor_and_the_app_goes_to_the_extra_memory);
  RUN_TEST(test_a_board_with_no_extra_memory_takes_everything_home);
  RUN_TEST(test_a_big_block_asks_for_the_extra_memory_first);
  RUN_TEST(test_the_relief_is_a_failed_ask_and_not_a_heap_that_looks_full);
  RUN_TEST(test_a_block_that_is_being_moved_is_not_one_the_relief_may_take);
  return UNITY_END();
}
