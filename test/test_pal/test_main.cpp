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
#include <string.h>

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

// **Els llibres de la memòria** (0.190.0, l'amo: *«podríem fer una llista de punter, tamany i qui l'ha demanat?»*): every block that
// leaves the door is written down with its owner and the app that was in front, a `resize` keeps the owner of the block it moves, i
// alliberar-lo el treu de la llista, i el que no hi cap **es compta** en comptes de desaparèixer (docs/75).
void test_the_books_say_which_block_is_whose(void) {
  pal::set_memory_bytes(kHomeBytes, kExtraBytes);
  // **La taula la demana la primera marca** i no el fet de posar els sostres (0.190.0): a system that has never said who it is has no
  // books, and the table is taken from the memory the board has at that moment (docs/75).
  TEST_ASSERT_EQUAL_UINT32(0, pal::memory_slots());
  const uint32_t lost_before = pal::memory_lost();

  // **Una pàgina que demana el seu tros**: the mark is the module and the app, and both are copied into the row because the app that
  // asked will be gone when the list is read (docs/75).
  void* view = nullptr;
  {
    pal::MemoryTag tag(pal::kMemoryUi, "home_eink");
    view = pal::extra_memory().allocate(64000, true);
  }
  TEST_ASSERT_NOT_NULL(view);
  TEST_ASSERT_GREATER_THAN_UINT32(0, pal::memory_slots());
  pal::MemoryBlock rows[16] = {};
  uint32_t count = pal::memory_blocks(rows, 16);
  TEST_ASSERT_GREATER_THAN_UINT32(0, count);
  // **El més gran és el primer**, que és com es llegeix la llista.
  TEST_ASSERT_EQUAL_PTR(view, rows[0].block);
  TEST_ASSERT_EQUAL_UINT32(64000, rows[0].bytes);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(pal::kMemoryUi), rows[0].owner);
  TEST_ASSERT_EQUAL_STRING("home_eink", rows[0].app);
  TEST_ASSERT_EQUAL_STRING("ui", pal::memory_owner_word(static_cast<pal::MemoryOwner>(rows[0].owner)));

  // **I el resum per amo diu el que el mòdul té**, que és la primera pregunta d'una fuita (docs/75).
  pal::MemoryBill bills[8] = {};
  const uint32_t owners = pal::memory_bills(bills, 8);
  TEST_ASSERT_GREATER_THAN_UINT32(0, owners);
  bool found = false;
  for (uint32_t i = 0; i < owners; ++i) {
    if (bills[i].owner != static_cast<uint8_t>(pal::kMemoryUi)) continue;
    if (strcmp(bills[i].app, "home_eink") != 0) continue;
    found = true;
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(64000, bills[i].bytes);
  }
  TEST_ASSERT_TRUE_MESSAGE(found, "the summary has no row for the module that asked");

  // **I créixer no canvia d'amo**: a `resize` is the same block with another size, i el que es llegeix és la mida nova (docs/75).
  {
    pal::MemoryTag tag(pal::kMemoryLua, "about");
    void* grown = pal::extra_memory().resize(view, 96000, true);
    TEST_ASSERT_NOT_NULL(grown);
    view = grown;
  }
  count = pal::memory_blocks(rows, 16);
  TEST_ASSERT_GREATER_THAN_UINT32(0, count);
  TEST_ASSERT_EQUAL_PTR(view, rows[0].block);
  TEST_ASSERT_EQUAL_UINT32(96000, rows[0].bytes);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(static_cast<uint8_t>(pal::kMemoryUi), rows[0].owner,
                                  "a block that grew was filed under whoever grew it");

  // **I un `resize` d'un bloc que no hi era és un bloc nou** (0.190.0): `realloc(NULL, n)` is how the interpreter asks for a block it
  // has never had, and looking for a row with a null pointer used to find the first free one —which is nobody's— and leave the block
  // with no owner at all. Es va veure a l'emulador: dues-centes quaranta-nou files sense amo.
  void* fresh = nullptr;
  {
    pal::MemoryTag tag(pal::kMemoryBook, "reader");
    fresh = pal::extra_memory().resize(nullptr, 5000, false);
  }
  TEST_ASSERT_NOT_NULL(fresh);
  count = pal::memory_blocks(rows, 16);
  found = false;
  for (uint32_t i = 0; i < count && i < 16; ++i) {
    if (rows[i].block != fresh) continue;
    found = true;
    TEST_ASSERT_EQUAL_UINT32(5000, rows[i].bytes);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(pal::kMemoryBook), rows[i].owner);
    TEST_ASSERT_EQUAL_STRING("reader", rows[i].app);
  }
  TEST_ASSERT_TRUE_MESSAGE(found, "a block asked for with a null resize is not in the list");
  pal::extra_memory().release(fresh);

  // **I alliberar-lo el treu de la llista**, que és el que fa que la llista sigui de blocs vius i no d'història (docs/75).
  pal::extra_memory().release(view);
  count = pal::memory_blocks(rows, 16);
  for (uint32_t i = 0; i < count && i < 16; ++i) {
    TEST_ASSERT_TRUE_MESSAGE(rows[i].block != view, "a block that was given back is still in the list");
  }

  // **I el que no hi cap es compta**: more asks than rows, and the difference is the number the books say they lost —a list that is
  // not the whole list has to say so (docs/75).
  const uint32_t slots = pal::memory_slots();
  void* many[420];
  const uint32_t asks = (slots + 40 > 420) ? 420 : slots + 40;
  for (uint32_t i = 0; i < asks; ++i) {
    pal::MemoryTag tag(pal::kMemoryLua, "filler");
    many[i] = pal::extra_memory().allocate(16, false);
    TEST_ASSERT_NOT_NULL(many[i]);
  }
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(lost_before, pal::memory_lost(),
                                          "the table did not count the asks that did not fit");
  for (uint32_t i = 0; i < asks; ++i) pal::extra_memory().release(many[i]);
}

// **Les dues cases, comptades a part** (0.192.92, l'amo: *«Estem segurs que estem obtenint la memoria ocupada real?»*): the map of the
// memory paints the RAM and the extra memory in columns of their own, so the books have to say which home a block is in —and they say it
// **by the pointer** and not by a flag kept in the row, perquè un `resize` pot moure un bloc d'una casa a l'altra i una fila que
// recordés on va néixer pintaria els bytes de la PSRAM a la barra de la RAM (docs/75).
void test_the_books_count_each_home_apart(void) {
  pal::set_memory_bytes(kHomeBytes, kExtraBytes);
  // A large block that asks for the extra memory goes there, and a small one asked without that wish goes home while home keeps its floor.
  void* big = nullptr;
  void* small = nullptr;
  {
    pal::MemoryTag tag(pal::kMemoryUi, "home_eink");
    big = pal::extra_memory().allocate(64000, true);
    small = pal::extra_memory().allocate(kSmallBytes, false);
  }
  TEST_ASSERT_NOT_NULL(big);
  TEST_ASSERT_NOT_NULL(small);
  TEST_ASSERT_TRUE_MESSAGE(pal::extra_memory().in_the_extra(big), "a block that asked for the extra memory is not there");
  TEST_ASSERT_FALSE_MESSAGE(pal::extra_memory().in_the_extra(small), "a small block went to the extra memory");

  const pal::MemoryHeld ram = pal::memory_held(0);
  const pal::MemoryHeld extra = pal::memory_held(1);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(kSmallBytes, ram.bytes, "the books lost the block of the board's RAM");
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(64000, extra.bytes, "the books lost the block of the extra memory");
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1, ram.blocks);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1, extra.blocks);
  // **I el total de la casa és el que el heap pot donar**, que és contra què es llegeix el lliure (0.192.92): the emulator was told the
  // ceiling of the two homes, o sigui que el número hi és i és el de la regió.
  const pal::HeapInfo heap = pal::system().heap();
  TEST_ASSERT_TRUE(heap.valid);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(kHomeBytes, heap.total_bytes, "the heap does not say how much the home can give");
  TEST_ASSERT_TRUE_MESSAGE(heap.free_bytes < heap.total_bytes, "nothing was taken from a home that handed out two blocks");

  // **Un bloc que es mou canvia de casa**: a `resize` down to a small block and with no wish for the extra memory brings it home, i els
  // llibres el compten on és ara (docs/75).
  {
    pal::MemoryTag tag(pal::kMemoryLua, "about");
    void* moved = pal::extra_memory().resize(big, kSmallBytes, false);
    TEST_ASSERT_NOT_NULL(moved);
    TEST_ASSERT_FALSE_MESSAGE(pal::extra_memory().in_the_extra(moved),
                              "a block resized down to the board's RAM is still in the extra memory");
    big = moved;
  }
  const pal::MemoryHeld extra_after = pal::memory_held(1);
  TEST_ASSERT_LESS_THAN_UINT32_MESSAGE(64000, extra_after.bytes, "the books still count the block in the home it left");

  pal::extra_memory().release(big);
  pal::extra_memory().release(small);
}

// **El host que es mira mentre el nucli espera una persona** (0.190.3): the page of an app that has died stops the turn of the
// kernel, and that turn is what looks at the window of the emulator —so the page asks the host for a look itself, and the answer
// of the look is the second thing: a person who closes the window is a run that ends (docs/30, docs/85).
static int g_pump_calls = 0;

static bool count_the_pump(void* ctx) {
  ++g_pump_calls;
  return (ctx != nullptr) ? *static_cast<bool*>(ctx) : false;
}

void test_a_host_is_looked_at_with_the_answer_that_ends_a_page(void) {
  // **De sèrie no hi ha res a mirar**: a build with no window —and every case of this suite— registers nothing, and a page that
  // asks gets the answer of a board, which is that there is nothing to look at (docs/50).
  pal::set_pump(nullptr, nullptr);
  TEST_ASSERT_FALSE(pal::pump());

  // **El host és el que s'ha registrat, amb el seu context i amb la seva resposta**: the context is how a pump knows where to keep
  // what it has seen, and it is passed through unchanged.
  g_pump_calls = 0;
  bool the_run_is_over = false;
  pal::set_pump(&count_the_pump, &the_run_is_over);
  TEST_ASSERT_FALSE(pal::pump());
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_pump_calls, "the host was not looked at");
  the_run_is_over = true;
  TEST_ASSERT_TRUE(pal::pump());
  TEST_ASSERT_EQUAL_INT_MESSAGE(2, g_pump_calls, "the host was not looked at twice");

  // **I treure'l és una build que torna a no mirar res**, which is what a case that registered one leaves behind it (docs/80).
  pal::set_pump(nullptr, nullptr);
  TEST_ASSERT_FALSE(pal::pump());
  TEST_ASSERT_EQUAL_INT_MESSAGE(2, g_pump_calls, "a host that was taken out was looked at");
}

void test_a_console_that_cannot_see_its_line_says_it_is_quiet_at_once(void) {
  // The boot's settle asks the console, and the host's pipe is not a UART: it cannot watch an RX pin, so its
  // answer is "quiet" and it is the truth there. A wait of a second would be a boot that pays for a line it
  // does not have (docs/50).
  pal::IConsole* console = pal::system().console();
  TEST_ASSERT_NOT_NULL(console);
  const int64_t started = pal::system().millis();
  TEST_ASSERT_TRUE(console->wait_quiet(1000, 2000));
  TEST_ASSERT_TRUE_MESSAGE(pal::system().millis() - started < 50, "a console with no line waited for one");
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
  RUN_TEST(test_the_books_say_which_block_is_whose);
  RUN_TEST(test_the_books_count_each_home_apart);
  RUN_TEST(test_a_host_is_looked_at_with_the_answer_that_ends_a_page);
  RUN_TEST(test_a_console_that_cannot_see_its_line_says_it_is_quiet_at_once);
  return UNITY_END();
}
