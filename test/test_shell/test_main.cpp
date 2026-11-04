// M0 tests: the shell answers what it says it answers, and it says so out loud.
//
// These run on the host build with a fake board (lib/hal_emulator) and the host
// PAL, so they exercise the same code the emulator runs.
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <unity.h>

#include "emu/options.h"
#include "hal/hal.h"
#include "hal/log.h"
#include "kernel/apps.h"
#include "kernel/kernel.h"
#include "kernel/log.h"
#include "kernel/meter.h"
#include "kernel/repository.h"
#include "kernel/shell.h"
#include "kernel/vfs.h"
#include "pal/pal.h"

// The meters and the test file are in different namespaces: one line and the rest reads the same as in
// the kernel (docs/30).
namespace meter = kernel::meter;

namespace {

// **The console of the test, while a capture holds it** (docs/80): the descriptors live here and not inside the
// function that takes the console away, because a failing assertion leaves through Unity's `longjmp` and never reaches
// the line that gives it back —and then the failure and the rest of the run are written into the file of the capture,
// which is a run that shows nothing at all— (found the 4/11).
int g_console_saved = -1;
int g_console_sink = -1;
const char* const kConsolePath = "/tmp/lunok-shell-out.txt";

// The captured console, said again on the console of the test: a failure is the one time it is worth reading.
void console_replay() {
  fflush(stdout);
  FILE* file = fopen(kConsolePath, "rb");
  if (file == nullptr) return;
  char line[256];
  size_t got = 0;
  printf("--- the console of the test while it was captured ---\n");
  while ((got = fread(line, 1, sizeof(line), file)) > 0) fwrite(line, 1, got, stdout);
  printf("--- end of the captured console ---\n");
  fclose(file);
  fflush(stdout);
}

void console_give_back(bool replay) {
  fflush(stdout);
  if (g_console_saved >= 0) {
    dup2(g_console_saved, STDOUT_FILENO);
    close(g_console_saved);
    g_console_saved = -1;
  }
  if (g_console_sink >= 0) {
    close(g_console_sink);
    g_console_sink = -1;
  }
  if (replay) console_replay();
}

bool console_take_away() {
  if (g_console_saved >= 0) return true;
  fflush(stdout);
  g_console_saved = dup(STDOUT_FILENO);
  g_console_sink = open(kConsolePath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (g_console_saved < 0 || g_console_sink < 0) {
    console_give_back(false);
    return false;
  }
  dup2(g_console_sink, STDOUT_FILENO);
  return true;
}

}  // namespace

void setUp(void) {}

void tearDown(void) {
  // **The console comes back before anything else**, whatever the test did: Unity calls this for every test, passed,
  // failed or blown up, and a capture left open is a run with no output (docs/80).
  console_give_back(Unity.CurrentTestFailed != 0);
}

namespace {

bool g_booted = false;

// **The filesystem is counted per unit and per door**, so a test that asks "was it asked" adds the four
// units of that door: the fake board has the flash and the key-value store, and a card that a test
// mounts would be one more (docs/30, docs/70).
uint32_t storage_calls(meter::Op op) {
  uint32_t total = 0;
  for (uint8_t unit = 0; unit < meter::kUnitCount; ++unit) {
    total += meter::calls(meter::storage_id(op, unit));
  }
  return total;
}

// **The disk of this suite, and only of this suite** (4/11): a directory of its own that the run makes and
// wipes, because these tests write apps and files and then ask the shell to find them. They used to write
// into whatever disk the options happened to name —`data/` and then the build's— which is why four of them
// were red for days and why one default elsewhere in the bench could turn them green without touching them:
// a test that writes in the repository is not testing the repository (docs/85).
char g_root[96];

// Every directory on the way, because the volume has to exist before the boot looks for it: a backend that
// finds no directory is a board with no disk (docs/85).
void make_dirs(const char* path) {
  char partial[256];
  const size_t len = strlen(path);
  if (len >= sizeof(partial)) return;
  memcpy(partial, path, len + 1);
  for (size_t i = 1; i <= len; ++i) {
    if (partial[i] == '/' || partial[i] == '\0') {
      const char saved = partial[i];
      partial[i] = '\0';
      ::mkdir(partial, 0777);  // already there is not an error
      partial[i] = saved;
    }
  }
}

void ensure_booted() {
  if (g_booted) return;
  snprintf(g_root, sizeof(g_root), "/tmp/lunok-shell-XXXXXX");
  if (mkdtemp(g_root) == nullptr) g_root[0] = '\0';  // no disk: the tests that need one say so
  char volume[128];
  snprintf(volume, sizeof(volume), "%s/littlefs", g_root);
  make_dirs(volume);
  emu::Options opts;
  opts.fs_dir = g_root;
  emu::set_options(opts);
  pal::init();
  // **The three doors of a disk that has just been formatted**: an empty volume has no `/apps`, `/res` or
  // `/data`, and the shell's paths are those, so the suite makes them once for every test in it. **The folders
  // come before the boot** (0.88.2): `/apps` is a mount only when the flash has the folder, the way a disk that
  // carries apps is the one a board boots on (docs/70, docs/85).
  if (hal::IStorage* storage = hal::platform().storage("/littlefs")) {
    storage->mkdir("/apps");
    storage->mkdir("/res");
    storage->mkdir("/data");
  }
  kernel::boot(hal::platform());
  g_booted = true;
}

// **What the console said, captured the way a person's terminal would read it**: the host console is
// stdout, so a file around the call is the whole of it.
void capture_begin() { console_take_away(); }

void capture_end(char* out, size_t cap) {
  console_give_back(false);
  if (out == nullptr || cap == 0) return;
  out[0] = '\0';
  FILE* file = fopen(kConsolePath, "rb");
  if (file == nullptr) return;
  const size_t got = fread(out, 1, cap - 1, file);
  out[got] = '\0';
  fclose(file);
}

}  // namespace

void test_version_command_is_known(void) {
  ensure_booted();
  TEST_ASSERT_TRUE(kernel::execute("version"));
}

void test_help_command_is_known(void) {
  ensure_booted();
  TEST_ASSERT_TRUE(kernel::execute("help"));
}

// **`help` with a command is the lines of that command** (0.88.1): `help repo` is the three lines of `repo` and no
// other, `help repo install` is the one line of that syntax, a word that is only the beginning of a command is not the
// command, and a word that answers to nothing is said instead of swallowed (docs/30).
void test_help_with_a_command_is_the_lines_of_that_command(void) {
  ensure_booted();
  static char said[4096];

  capture_begin();
  TEST_ASSERT_TRUE(kernel::execute("help repo"));
  capture_end(said, sizeof(said));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "repo [owner/repo] [dir]"), said);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "repo info <app>"), said);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "repo install <app>"), said);
  // Nothing of the other commands: not the one that starts with the same letters, not the list itself.
  TEST_ASSERT_NULL_MESSAGE(strstr(said, "power"), said);
  TEST_ASSERT_NULL_MESSAGE(strstr(said, "version"), said);

  capture_begin();
  TEST_ASSERT_TRUE(kernel::execute("help repo install"));
  capture_end(said, sizeof(said));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "repo install <app>"), said);
  TEST_ASSERT_NULL_MESSAGE(strstr(said, "repo info"), said);

  // **El començament d'una comanda no és la comanda**: `pow` is not `power`, and what the board says is that it does
  // not know it and where the whole list is.
  capture_begin();
  TEST_ASSERT_TRUE(kernel::execute("help pow"));
  capture_end(said, sizeof(said));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "no such command: pow"), said);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "`help` lists them all"), said);
}

void test_caps_command_is_known(void) {
  ensure_booted();
  TEST_ASSERT_TRUE(kernel::execute("caps"));
}

void test_mem_command_is_known(void) {  ensure_booted();
  TEST_ASSERT_TRUE(kernel::execute("mem"));
}

// **The ceilings of the two homes are the board's and the books are exact** (docs/75, docs/85): the
// bench hands out the board's own RAM and its extra memory from the machine's heap, and a block that
// does not fit either home is refused here, before the board would have refused it. The bytes that come
// back are the bytes that went out, a block that has to move keeps what it held, and a home that is
// asked for and full is not the other home's refusal.
static void test_a_block_that_does_not_fit_the_ceilings_is_refused() {
  ensure_booted();
  // **The books start here and not at boot**: the blocks the boot took were handed out by a bench with no
  // ceiling, and counting them now would make this test's arithmetic the boot's (docs/85).
  pal::set_memory_bytes(4096, 8192);
  pal::IExtraMemory& memory = pal::extra_memory();

  const pal::MemoryLeft home_before = pal::memory_left(false);
  TEST_ASSERT_TRUE_MESSAGE(home_before.counted, "the home has no ceiling");
  TEST_ASSERT_EQUAL_UINT32(4096, home_before.free_bytes);
  TEST_ASSERT_EQUAL_UINT32(8192, pal::memory_left(true).free_bytes);

  // The block carries its own sixteen bytes of bookkeeping, and the books count them: a home of 4096 B
  // that hands out 4000 B has 80 B left and not 96 (docs/85).
  char* first = static_cast<char*>(memory.allocate(4000, false));
  TEST_ASSERT_NOT_NULL(first);
  TEST_ASSERT_EQUAL_UINT32(80, pal::memory_left(false).free_bytes);
  TEST_ASSERT_EQUAL_UINT32(8192, pal::memory_left(true).free_bytes);
  memset(first, 'a', 4000);

  // **The other home before a refusal**: 2000 B do not fit in the 80 B left of the board's RAM, and the
  // extra memory takes them, which is what the board's allocator does (docs/75).
  char* second = static_cast<char*>(memory.allocate(2000, false));
  TEST_ASSERT_NOT_NULL(second);
  TEST_ASSERT_EQUAL_UINT32(80, pal::memory_left(false).free_bytes);
  TEST_ASSERT_EQUAL_UINT32(8192 - 2016, pal::memory_left(true).free_bytes);

  // Neither home has room for the third: this is the refusal that makes an app that asks for more than
  // the glass has die on the bench and not on the paper (docs/85).
  TEST_ASSERT_NULL(memory.allocate(8000, false));

  // A block that shrinks stays where it is and gives its bytes back exactly.
  first = static_cast<char*>(memory.resize(first, 3000, false));
  TEST_ASSERT_NOT_NULL(first);
  TEST_ASSERT_EQUAL_UINT32(80 + 1000, pal::memory_left(false).free_bytes);
  TEST_ASSERT_EQUAL_MEMORY("aaaa", first, 4);

  // **A block that grows past its home moves to the other one**, and the bytes it held come with it: the
  // move is a `realloc` that could not happen in place, and the caller keeps the new pointer (docs/85).
  first = static_cast<char*>(memory.resize(first, 5000, false));
  TEST_ASSERT_NOT_NULL(first);
  TEST_ASSERT_EQUAL_MEMORY("aaaa", first, 4);
  // The home it left is whole again and its bytes went with it: only the extra memory holds the block.
  TEST_ASSERT_EQUAL_UINT32(4096, pal::memory_left(false).free_bytes);
  TEST_ASSERT_EQUAL_UINT32(8192 - 2016 - 5016, pal::memory_left(true).free_bytes);

  memory.release(first);
  memory.release(second);
  TEST_ASSERT_EQUAL_UINT32(4096, pal::memory_left(false).free_bytes);
  TEST_ASSERT_EQUAL_UINT32(8192, pal::memory_left(true).free_bytes);
  // The least that was left is the mark of the run and it does not go back up: `mem` prints it as the
  // `min free heap` of the board (docs/75).
  TEST_ASSERT_EQUAL_UINT32(80, pal::memory_left(false).lowest_free_bytes);

  // **And a bench with no model is a bench with no ceiling**, which is what every other test of this
  // suite is and what the PC's own heap is (docs/85).
  pal::set_memory_bytes(0, 0);
  TEST_ASSERT_FALSE(pal::memory_left(false).counted);
  TEST_ASSERT_FALSE(pal::memory_left(true).counted);
  void* free_block = memory.allocate(1024 * 1024, true);
  TEST_ASSERT_NOT_NULL(free_block);
  memory.release(free_block);
}

// **The heap is a heap and not a counter**: a block takes a piece of a region, the piece it does not use
// comes back when it goes, and a hundred blocks of sizes that do not line up give every byte back when
// they are let go. A heap that leaked a header here and a slack there would still pass the test of one
// block and would slowly strangle an app (docs/75).
static void test_the_heap_gives_every_byte_back_when_the_blocks_go() {
  ensure_booted();
  pal::set_memory_bytes(32768, 131072);
  pal::IExtraMemory& memory = pal::extra_memory();
  enum { kBlocks = 64 };
  void* blocks[kBlocks] = {nullptr};
  for (int i = 0; i < kBlocks; ++i) {
    blocks[i] = memory.allocate(static_cast<size_t>(24 + i * 13), (i % 2) == 0);
    TEST_ASSERT_NOT_NULL(blocks[i]);
  }
  // Half of them go first and the other half after: the order is part of what a heap has to survive.
  for (int i = 0; i < kBlocks; i += 2) memory.release(blocks[i]);
  for (int i = 1; i < kBlocks; i += 2) memory.release(blocks[i]);
  TEST_ASSERT_EQUAL_UINT32(32768, pal::memory_left(false).free_bytes);
  TEST_ASSERT_EQUAL_UINT32(131072, pal::memory_left(true).free_bytes);
  pal::set_memory_bytes(0, 0);
}

// **Fragmentation is the truth of a heap and not a bug** (docs/75): a home with room for a page in pieces
// cannot give the page, and the number that says so is the largest free block and not the free total.
static void test_a_page_does_not_fit_a_home_that_has_it_in_pieces() {
  ensure_booted();
  pal::set_memory_bytes(4096, 65536);
  pal::IExtraMemory& memory = pal::extra_memory();
  // **Sixteen blocks of 4080 fill the 64 KB home exactly**, a chunk of 4096 each, and every other one is
  // let go: the heap has the room in eight pieces and not one of them takes a page (docs/75).
  enum { kSmall = 16 };
  void* small[kSmall] = {nullptr};
  for (int i = 0; i < kSmall; ++i) {
    small[i] = memory.allocate(4080, true);
    TEST_ASSERT_NOT_NULL(small[i]);
  }
  for (int i = 0; i < kSmall; i += 2) memory.release(small[i]);
  TEST_ASSERT_TRUE(pal::memory_left(true).free_bytes >= 8 * 4096);
  // Neither home can take it: the board's RAM is smaller than the block and the PSRAM has the room in
  // pieces (docs/75).
  TEST_ASSERT_NULL(memory.allocate(6000, true));
  // And it says yes to what fits in a piece, which is what a heap does.
  void* fits = memory.allocate(4000, true);
  TEST_ASSERT_NOT_NULL(fits);
  memory.release(fits);
  for (int i = 1; i < kSmall; i += 2) memory.release(small[i]);
  TEST_ASSERT_EQUAL_UINT32(65536, pal::memory_left(true).free_bytes);
  pal::set_memory_bytes(0, 0);
}

// mounts says what is mounted and how much room is left, which is where two of
// the numbers of M4 come from (docs/90).
void test_mounts_command_lists_the_mounts(void) {  ensure_booted();
  TEST_ASSERT_TRUE(kernel::execute("mounts"));
}

// `cfg` is the command that says where the apps' settings are kept, and it refuses a unit
// that is not mounted instead of pretending (docs/70). The fake board has no card, so the
// unit is the flash and asking for the card is an error.
void test_cfg_command_says_the_unit_and_refuses_a_missing_card(void) {
  ensure_booted();
  TEST_ASSERT_TRUE(kernel::execute("cfg"));
  TEST_ASSERT_FALSE(kernel::execute("cfg sd"));
  TEST_ASSERT_TRUE(kernel::execute("cfg littlefs"));
  TEST_ASSERT_FALSE(kernel::execute("cfg diskette"));
}

void test_unknown_command_fails(void) {
  ensure_booted();
  TEST_ASSERT_FALSE(kernel::execute("no_existeix"));
}

void test_empty_line_is_not_an_error(void) {
  ensure_booted();
  TEST_ASSERT_TRUE(kernel::execute(""));
}

void test_overlong_line_is_cut_not_crashed(void) {
  ensure_booted();
  // 300 bytes of junk: the kernel copies at most its buffer and still answers.
  char line[301];
  for (size_t i = 0; i < sizeof(line) - 1; ++i) line[i] = 'x';
  line[sizeof(line) - 1] = '\0';
  TEST_ASSERT_FALSE(kernel::execute(line));
}

void test_board_has_a_console(void) {
  ensure_booted();
  const hal::Caps& caps = kernel::platform().caps();
  TEST_ASSERT_TRUE(caps.has(hal::kCapConsole));
}

void test_kernel_is_running(void) {
  ensure_booted();
  TEST_ASSERT_TRUE(kernel::running());
}

namespace {

const char kTestAppDir[] = "/apps/lunok-test";

bool write_text(hal::IStorage* storage, const char* path, const char* text) {
  hal::FileSlot slot;
  if (!storage->open(path, hal::OpenMode::kWrite, slot)) return false;
  const bool ok = storage->write(slot, text, static_cast<uint32_t>(strlen(text))) > 0;
  storage->close(slot);
  return ok;
}

bool saw_app(const kernel::apps::Entry& entry, void* context) {
  (void)entry;
  *static_cast<bool*>(context) = true;
  return false;
}

bool app_is_listed() {
  bool seen = false;
  kernel::apps::for_each("lunok-test", &saw_app, &seen);
  return seen;
}

}  // namespace

// A manifest alone is not an app: without the entry file it must not be listed,
// because it could not be run (docs/40).
void test_an_app_without_its_entry_file_is_not_listed(void) {
  ensure_booted();
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  // A run that was cut short leaves this directory behind on the emulator's disk,
  // and with a stale entry file in it the case cannot be seen at all.
  storage->remove("/apps/lunok-test/app.lua");
  storage->remove("/apps/lunok-test/manifest.json");
  storage->mkdir(kTestAppDir);
  TEST_ASSERT_TRUE(write_text(storage, "/apps/lunok-test/manifest.json",
                              "{\"title\":\"Lunok test\",\"version\":\"0.1.0\","
                              "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}"));
  TEST_ASSERT_FALSE(app_is_listed());

  TEST_ASSERT_TRUE(write_text(storage, "/apps/lunok-test/app.lua", "console.print('x')"));
  TEST_ASSERT_TRUE(app_is_listed());

  storage->remove("/apps/lunok-test/app.lua");
  storage->remove("/apps/lunok-test/manifest.json");
  storage->remove(kTestAppDir);
  TEST_ASSERT_FALSE(app_is_listed());
}

// Two apps may declare the same extension: the finder has to say how many there
// are, so the shell can be honest about which one it uses (docs/40).
void test_the_extension_finder_counts_every_app_that_opens_it(void) {
  ensure_booted();
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);

  const char kManifest[] =
      "{\"title\":\"Lunok handler\",\"version\":\"0.1.0\",\"entry\":\"app.lua\","
      "\"opens\":\".LUNOK\",\"boards\":[\"esp32\"]}";
  storage->mkdir("/apps/lunok-one");
  storage->mkdir("/apps/lunok-two");
  TEST_ASSERT_TRUE(write_text(storage, "/apps/lunok-one/manifest.json", kManifest));
  TEST_ASSERT_TRUE(write_text(storage, "/apps/lunok-one/app.lua", "console.print('one')"));
  TEST_ASSERT_TRUE(write_text(storage, "/apps/lunok-two/manifest.json", kManifest));
  TEST_ASSERT_TRUE(write_text(storage, "/apps/lunok-two/app.lua", "console.print('two')"));

  kernel::apps::Entry found;
  uint32_t matches = 0;
  TEST_ASSERT_TRUE(kernel::apps::find_for_extension("lunok", found, matches));
  TEST_ASSERT_EQUAL_UINT32(2, matches);
  TEST_ASSERT_EQUAL_STRING("lunok", found.opens);

  // An extension nobody declares is not found, and says zero.
  TEST_ASSERT_FALSE(kernel::apps::find_for_extension("nadie", found, matches));
  TEST_ASSERT_EQUAL_UINT32(0, matches);

  storage->remove("/apps/lunok-one/app.lua");
  storage->remove("/apps/lunok-one/manifest.json");
  storage->remove("/apps/lunok-one");
  storage->remove("/apps/lunok-two/app.lua");
  storage->remove("/apps/lunok-two/manifest.json");
  storage->remove("/apps/lunok-two");
}

// **`cat` imprimeix el fitxer sencer i no la seva primera pantalla** (0.82.2): llegia una finestra de 1 024 B
// amb `vfs::read_file` i la imprimia sense dir res, o sigui que una pàgina de l'`http` s'aturava a la primera
// pantalla i mitja i semblava un fitxer tallat. La prova escriu 4 KB amb una marca a l'última línia i mira que
// hi sigui; el que triga un `cat` gran és el cable i no un sostre (docs/30, docs/70).
void test_cat_prints_the_whole_file_and_not_its_first_screen(void) {
  ensure_booted();
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);

  static char text[4096];
  size_t used = 0;
  for (int i = 1; i <= 80; ++i) {
    used += static_cast<size_t>(snprintf(text + used, sizeof(text) - used, "linia %02d of the file\n", i));
  }
  used += static_cast<size_t>(snprintf(text + used, sizeof(text) - used, "the last line is here\n"));
  TEST_ASSERT_TRUE(write_text(storage, "/long.txt", text));
  // The file is longer than the 1 024 B window `cat` used to read, which is the whole point of the case.
  TEST_ASSERT_GREATER_THAN(1024, static_cast<int>(used));

  static char said[8192];
  capture_begin();
  const bool ok = kernel::execute("cat /littlefs/long.txt");
  capture_end(said, sizeof(said));
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "linia 01 of the file"), said);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "linia 80 of the file"), said);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "the last line is here"), said);

  // I un fitxer que no acaba amb un salt en rep un, o el prompt quedaria enganxat a l'última lletra.
  TEST_ASSERT_TRUE(write_text(storage, "/nonl.txt", "sense salt"));
  capture_begin();
  TEST_ASSERT_TRUE(kernel::execute("cat /littlefs/nonl.txt"));
  capture_end(said, sizeof(said));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "sense salt\r\n"), said);

  storage->remove("/long.txt");
  storage->remove("/nonl.txt");
}

// **Una ruta que no hi és diu per què i on és el que sí que hi ha** (0.86.1): `ls /sd/app/timers` amb un `apps` al
// costat és el que fa trobar la lletra que falta, i un camí que és un fitxer es llista amb el seu nom i la seva mida en
// comptes de dir que no és cap carpeta. És el mateix per a `cat`, que ja deia «no such file» i ara diu també on és el
// tros que existeix (docs/30, docs/70).
void test_a_path_that_is_not_there_says_where_the_rest_is(void) {
  ensure_booted();
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  storage->mkdir("/apps");
  storage->mkdir("/apps/listed");
  TEST_ASSERT_TRUE(write_text(storage, "/apps/listed/one.lua", "x\n"));

  // A directory lists its entries, with the kind and the size.
  capture_begin();
  TEST_ASSERT_TRUE(kernel::execute("ls /apps/listed"));
  static char said[2048];
  capture_end(said, sizeof(said));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "one.lua"), said);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "file"), said);

  // **A file is listed as one entry**, which is what a person asking for a path wants to know (docs/30).
  capture_begin();
  TEST_ASSERT_TRUE(kernel::execute("ls /apps/listed/one.lua"));
  capture_end(said, sizeof(said));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "one.lua"), said);
  TEST_ASSERT_NULL_MESSAGE(strstr(said, "no such directory"), said);

  // **El tros que hi és, i què té a dins**: `/apps/listd` no hi és i el que hi ha al costat és `listed`.
  capture_begin();
  TEST_ASSERT_TRUE(kernel::execute("ls /apps/listd/one.lua"));
  capture_end(said, sizeof(said));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "no such directory: /apps/listd/one.lua"), said);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "/apps has:"), said);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "listed"), said);

  // I `cat` diu on és igual, que és el mateix camí escrit a mà.
  capture_begin();
  TEST_ASSERT_FALSE(kernel::execute("cat /apps/listd/one.lua"));
  capture_end(said, sizeof(said));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "no such file: /apps/listd/one.lua"), said);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "/apps has:"), said);

  // A path whose nothing exists still names the root and what it holds, instead of a "cannot list" with no subject.
  capture_begin();
  TEST_ASSERT_TRUE(kernel::execute("ls /nowhere/deeper/x"));
  capture_end(said, sizeof(said));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "no such directory: /nowhere/deeper/x"), said);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "the root has:"), said);

  // **`/apps` no s'esborra**: és la carpeta que les altres proves d'aquest fitxer fan servir, i `remove` d'una carpeta
  // és d'aquest backend i no una regla de la prova (docs/70).
  storage->remove("/apps/listed/one.lua");
  storage->remove("/apps/listed");
}

// **El llistat d'un directori de GitHub, sense tenir-lo sencer** (0.82.0): la comanda `repository` baixa el
// JSON de l'API de continguts i en treu les carpetes amb una finestra de dos kilobytes. El document de la prova
// és més gran que la finestra a posta, i un objecte cau just al mig per comprovar que mig objecte es guarda per
// al tros següent (docs/70, docs/75).
void test_the_listing_of_a_repository_is_read_in_chunks(void) {
  ensure_booted();
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);

  static char json[6144];
  size_t used = 0;
  used += static_cast<size_t>(snprintf(json + used, sizeof(json) - used, "[\n"));
  // Forty objects of about a hundred bytes: 4 KB, two windows, and one of them starts inside an object.
  for (int i = 1; i <= 40; ++i) {
    used += static_cast<size_t>(snprintf(
        json + used, sizeof(json) - used,
        "  {\n    \"name\": \"app%02d\",\n    \"path\": \"apps/app%02d\",\n"
        "    \"sha\": \"0123456789abcdef0123456789abcdef01234567\",\n    \"size\": 0,\n"
        "    \"type\": \"dir\"\n  }%s\n",
        i, i, (i < 40) ? "," : ""));
  }
  used += static_cast<size_t>(snprintf(json + used, sizeof(json) - used, "]\n"));
  TEST_ASSERT_TRUE(write_text(storage, "/listing.json", json));

  kernel::repository::Listing listing;
  TEST_ASSERT_TRUE(listing.open("/littlefs/listing.json"));
  char name[48];
  int count = 0;
  char last[48] = {0};
  while (listing.next(name, sizeof(name))) {
    ++count;
    memcpy(last, name, sizeof(last));
  }
  listing.close();
  TEST_ASSERT_EQUAL_INT(40, count);
  // The last one is the last object and not a name cut by the end of the buffer.
  TEST_ASSERT_EQUAL_STRING("app40", last);

  // **Un fitxer que diu ser un llistat i no ho és dona la volta i no penja**: el que no es reconeix no és una
  // carpeta (docs/30).
  TEST_ASSERT_TRUE(write_text(storage, "/broken.json", "{\"name\": \"x\""));
  kernel::repository::Listing broken;
  TEST_ASSERT_TRUE(broken.open("/littlefs/broken.json"));
  TEST_ASSERT_FALSE(broken.next(name, sizeof(name)));
  broken.close();
  TEST_ASSERT_FALSE(broken.open("/littlefs/nowhere.json"));

  storage->remove("/listing.json");
  storage->remove("/broken.json");
}

// **Els elements d'un llistat, amb el seu tipus i la seva adreça** (0.86.0): `repo install` baixa el directori
// sencer d'una app, i per això vol els fitxers (amb l'adreça crua d'on baixar-los) i les carpetes (per demanar-ne el
// llistat). L'adreça és l'única cadena del document que no és un nom, i la prova la vol sencera: el buffer d'un nom
// la tallava a quaranta-vuit bytes i el fitxer tornava com un 404 (docs/30, docs/70).
void test_the_entries_of_a_listing_come_with_their_address(void) {
  ensure_booted();
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  static const char json[] =
      "[\n"
      "  {\n    \"name\": \"manifest.json\",\n"
      "    \"download_url\": \"https://raw.githubusercontent.com/lunokjod/lunokOS/main/apps/images/manifest.json\",\n"
      "    \"type\": \"file\"\n  },\n"
      "  {\n    \"name\": \"res\",\n    \"download_url\": null,\n    \"type\": \"dir\"\n  },\n"
      "  {\n    \"name\": \"images.lua\",\n"
      "    \"download_url\": \"https://raw.githubusercontent.com/lunokjod/lunokOS/main/apps/images/images.lua\",\n"
      "    \"type\": \"file\"\n  }\n"
      "]\n";
  TEST_ASSERT_TRUE(write_text(storage, "/files.json", json));

  kernel::repository::Kind kind = kernel::repository::Kind::kDir;
  char name[48];
  char url[148];
  TEST_ASSERT_TRUE(kernel::repository::entry_at("/littlefs/files.json", 0, kind, name, sizeof(name), url, sizeof(url)));
  TEST_ASSERT_EQUAL(kernel::repository::Kind::kFile, kind);
  TEST_ASSERT_EQUAL_STRING("manifest.json", name);
  TEST_ASSERT_EQUAL_STRING(
      "https://raw.githubusercontent.com/lunokjod/lunokOS/main/apps/images/manifest.json", url);
  // **Una carpeta no té adreça**, i no és una adreça buida la que decideix: és el `type` (docs/30).
  TEST_ASSERT_TRUE(kernel::repository::entry_at("/littlefs/files.json", 1, kind, name, sizeof(name), url, sizeof(url)));
  TEST_ASSERT_EQUAL(kernel::repository::Kind::kDir, kind);
  TEST_ASSERT_EQUAL_STRING("res", name);
  TEST_ASSERT_EQUAL_STRING("", url);
  TEST_ASSERT_TRUE(kernel::repository::entry_at("/littlefs/files.json", 2, kind, name, sizeof(name), url, sizeof(url)));
  TEST_ASSERT_EQUAL_STRING("images.lua", name);
  // El que passa del final no és cap element, i un document que no ho és tampoc no en té.
  TEST_ASSERT_FALSE(kernel::repository::entry_at("/littlefs/files.json", 3, kind, name, sizeof(name), url, sizeof(url)));
  TEST_ASSERT_FALSE(kernel::repository::entry_at("/littlefs/nowhere.json", 0, kind, name, sizeof(name), url, sizeof(url)));

  storage->remove("/files.json");
}

// A path with spaces goes between quotes, and without them the command is
// refused instead of reading half a path (docs/70). The same splitter serves
// `cat`, `ls` and `open`, and this is the `cat` and `ls` side of it.
void test_a_path_with_spaces_goes_in_quotes(void) {
  ensure_booted();
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(write_text(storage, "/res/el meu.txt", "spaces\n"));

  TEST_ASSERT_TRUE(kernel::execute("cat \"res/el meu.txt\""));
  TEST_ASSERT_TRUE(kernel::execute("ls \"res\""));
  // Without quotes the words are two and the shell does not guess which is which.
  TEST_ASSERT_FALSE(kernel::execute("cat res/el meu.txt"));
  TEST_ASSERT_FALSE(kernel::execute("ls res el meu"));
  // A quote left open is refused too, and nothing is read.
  TEST_ASSERT_FALSE(kernel::execute("cat \"res/el meu.txt"));

  storage->remove("/res/el meu.txt");
}

// `cat` fails when the file is not there, and it fails without asking the backend
// to open it: the ESP32 core logs an error for that even when it is a plain read
// (measured on the board, docs/70). The exit of the command is the proof that can
// be seen from here.
void test_cat_of_a_missing_file_fails(void) {
  ensure_booted();
  TEST_ASSERT_FALSE(kernel::execute("cat"));
  TEST_ASSERT_FALSE(kernel::execute("cat no-hi-es-aixo.txt"));
  TEST_ASSERT_FALSE(kernel::execute("cat /res"));
}

// `hexdump` is a command of the shell, not an app: it answers for itself, refuses a path that is not
// a file, and takes one path and no more (docs/30, docs/70).
void test_hexdump_of_a_missing_file_fails(void) {
  ensure_booted();
  TEST_ASSERT_FALSE(kernel::execute("hexdump no-hi-es-aixo.bin"));
  TEST_ASSERT_FALSE(kernel::execute("hexdump /res"));
  TEST_ASSERT_FALSE(kernel::execute("hexdump un cami de mes"));
}


// **What the system costs is counted at the door** (docs/30): a call through the VFS is one call in its
// meter, it carries its time, and the moment of the last one is on the table for `stats` to print.
void test_a_call_through_the_vfs_is_counted_and_timed(void) {
  ensure_booted();
  meter::reset();
  TEST_ASSERT_EQUAL_UINT32(0, storage_calls(meter::kOpStat));
  uint64_t total = 0;
  uint64_t free_bytes = 0;
  TEST_ASSERT_TRUE(kernel::vfs::space("/littlefs", total, free_bytes));
  TEST_ASSERT_TRUE(kernel::vfs::space("/littlefs", total, free_bytes));
  // Two calls of the same door of the same unit are two: the count is a count and not a flag.
  TEST_ASSERT_EQUAL_UINT32(2, storage_calls(meter::kOpStat));
  // A different door is a different meter: asking for the room is not listing a directory.
  TEST_ASSERT_EQUAL_UINT32(0, storage_calls(meter::kOpList));
  // And the time is a time: microseconds, not a number that could be negative.
  for (uint8_t unit = 0; unit < meter::kUnitCount; ++unit) {
    const uint8_t id = meter::storage_id(meter::kOpStat, unit);
    if (meter::calls(id) == 0) continue;
    TEST_ASSERT_TRUE(meter::us(id) < 10000000ULL);
    TEST_ASSERT_TRUE(meter::max_us(id) >= meter::min_us(id));
  }
}

// `stats` is a command of the prompt, and `stats reset` puts the table back to zero: a measurement is of
// a run and not of a life (docs/30, docs/95).
void test_stats_prints_the_table_and_resets_it(void) {
  ensure_booted();
  // **A directory of a unit and not the root**: the root is the mount table and answers without asking a
  // device, so listing it is not a call anybody can meter (docs/70).
  TEST_ASSERT_TRUE(kernel::execute("ls /littlefs"));
  TEST_ASSERT_TRUE(storage_calls(meter::kOpList) > 0);
  TEST_ASSERT_TRUE(kernel::execute("stats"));
  TEST_ASSERT_TRUE(kernel::execute("stats reset"));
  TEST_ASSERT_EQUAL_UINT32(0, storage_calls(meter::kOpList));
  TEST_ASSERT_EQUAL_UINT32(0, storage_calls(meter::kOpStat));
  // And the clock of the table starts again with it: the uptime is of this run.
  TEST_ASSERT_TRUE(meter::uptime_ms() < 10000);
}

// **`stats` prints every meter this build and board have, called or not** (docs/30, docs/85): a row at zero
// is a measurement —the device was not asked for anything— and a device the board does not carry is not a
// row at all. The test resets the table first, so a meter with no calls is a meter with no calls and not the
// leftover of the test before it.
static void test_stats_prints_the_meters_that_were_never_called() {
  ensure_booted();
  static char said[8192];
  TEST_ASSERT_TRUE(kernel::execute("stats reset"));
  capture_begin();
  kernel::execute("stats");
  capture_end(said, sizeof(said));
  // The processor was never asked to change its speed in this run, and it is still a thing this board can
  // do: its row is there, with a zero and dashes where the times would be.
  TEST_ASSERT_EQUAL_UINT32(0, meter::calls(meter::kCpuSet));
  const char* row = strstr(said, "power.cpu");
  TEST_ASSERT_NOT_NULL_MESSAGE(row, said);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(row, "-"), said);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "kernel.loop"), said);
  // And the meters the board does not have are not rows: asking the platform is what makes the test true
  // on the ATOM and on the Paper without knowing which one the bench is.
  const hal::Caps& caps = hal::platform().caps();
  TEST_ASSERT_EQUAL(caps.has(hal::kCapDisplay) ? 1 : 0, strstr(said, "glass.flush") != nullptr ? 1 : 0);
  TEST_ASSERT_EQUAL(caps.has(hal::kCapSd) ? 1 : 0, strstr(said, "sd.read") != nullptr ? 1 : 0);
  TEST_ASSERT_EQUAL(caps.has(hal::kCapEnv) ? 1 : 0, strstr(said, "env.read") != nullptr ? 1 : 0);
  TEST_ASSERT_EQUAL(caps.has(hal::kCapImu) ? 1 : 0, strstr(said, "imu.read") != nullptr ? 1 : 0);
}

// **One shape for every line**: the level, the tag, the message, and a newline that the formatter adds so
// that a line can never be split in two (docs/30, docs/85).
void test_a_log_line_carries_its_level_and_its_tag(void) {
  char line[160];
  const size_t len = hal::format_log(line, sizeof(line), pal::kLogWarn, "PANEL", "the glass said %d", 4);
  TEST_ASSERT_EQUAL_STRING("[W][PANEL] the glass said 4\n", line);
  // What it says it wrote is what it wrote, newline included.
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(strlen(line)), static_cast<uint32_t>(len));
  // The level is a letter and never a word, and an info line has the same two brackets.
  hal::format_log(line, sizeof(line), pal::kLogInfo, "FS", "mounted");
  TEST_ASSERT_EQUAL_STRING("[I][FS] mounted\n", line);
  // A line longer than the buffer is cut, not left without an end.
  char small[24];
  hal::format_log(small, sizeof(small), pal::kLogError, "TAG", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  TEST_ASSERT_EQUAL_UINT8('\n', static_cast<uint8_t>(small[strlen(small) - 1]));
  TEST_ASSERT_TRUE(strlen(small) < sizeof(small));
}


// **The ring is painted by level and only when the terminal asked** (docs/30, docs/85): an error is red, a
// warning is yellow and an info line is the paper. The echo and the dump go through the same painter, so
// both are here, and with the answer off not one escape byte comes out of either.
static void test_the_log_is_painted_by_level() {
  ensure_booted();
  static char said[8192];
  kernel::log_colour(true);
  capture_begin();
  kernel::log_printf(pal::kLogError, "TST", "red line");
  kernel::log_printf(pal::kLogWarn, "TST", "yellow line");
  kernel::log_printf(pal::kLogInfo, "TST", "plain line");
  kernel::log_dump();
  capture_end(said, sizeof(said));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "\x1b[1;31m[E][TST] red line"), said);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "\x1b[33m[W][TST] yellow line"), said);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "[I][TST] plain line"), said);
  TEST_ASSERT_NULL_MESSAGE(strstr(said, "\x1b[1;31m[I]"), said);
  // Off: the same lines and not one escape byte.
  kernel::log_colour(false);
  kernel::log_printf(pal::kLogError, "TST", "red line, no colour");
  capture_begin();
  kernel::log_dump();
  capture_end(said, sizeof(said));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "[E][TST] red line, no colour"), said);
  TEST_ASSERT_NULL_MESSAGE(strstr(said, "\x1b"), said);
}

// **Every time of `stats` is written in a scale a person reads** (docs/30): microseconds, milliseconds,
// seconds, minutes, hours, and never a raw count of five million milliseconds.
void test_a_duration_is_written_in_the_scale_that_reads(void) {
  char out[16];
  kernel::meter::human_us(out, sizeof(out), 845);
  TEST_ASSERT_EQUAL_STRING("845us", out);
  kernel::meter::human_us(out, sizeof(out), 12340);
  TEST_ASSERT_EQUAL_STRING("12.3ms", out);
  kernel::meter::human_us(out, sizeof(out), 1985000);
  TEST_ASSERT_EQUAL_STRING("1.98s", out);
  kernel::meter::human_us(out, sizeof(out), 187000000ULL);
  TEST_ASSERT_EQUAL_STRING("3m07s", out);
  kernel::meter::human_us(out, sizeof(out), 4988744000ULL);
  TEST_ASSERT_EQUAL_STRING("1h23m", out);
}

// **Whether the terminal speaks ANSI is asked, not assumed** (docs/85): the answer to the Device Attributes
// query is `ESC [ ? ... c`, and everything else —an arrow, a cursor report, a keystroke, nothing at all— is
// not an answer. The rule is pure, so the question can be tried without a port.
static void test_only_a_device_attributes_reply_is_an_answer() {
  // What a VT100 and what an xterm send back.
  TEST_ASSERT_TRUE(kernel::shell::ansi_reply("\x1b[?1;2c", 7));
  TEST_ASSERT_TRUE(kernel::shell::ansi_reply("\x1b[?62;c", 7));
  // An arrow key, which is a key and not an answer, and a cursor position report.
  TEST_ASSERT_FALSE(kernel::shell::ansi_reply("\x1b[A", 3));
  TEST_ASSERT_FALSE(kernel::shell::ansi_reply("\x1b[12;40R", 8));
  // A person typing `ls` while the question was in the air is a person, not a terminal.
  TEST_ASSERT_FALSE(kernel::shell::ansi_reply("ls\r", 3));
  TEST_ASSERT_FALSE(kernel::shell::ansi_reply("", 0));
  TEST_ASSERT_FALSE(kernel::shell::ansi_reply(nullptr, 5));
  // The answer may come after something else, because a serial line does not deliver it alone.
  TEST_ASSERT_TRUE(kernel::shell::ansi_reply("ab\x1b[?1;2c", 9));
}

// **The colours are worth the bytes only on a terminal that asked for them** (docs/30, docs/85): with the
// answer off the same words come out and not one escape byte —what a pipe and a serial line without ANSI
// see—, and with it on the error is red, a directory is blue, what is not the answer is dim and the prompt
// is its own two pieces. The answer is forced because a test is not a terminal; `ansi on` is also how a
// person whose bridge eats the question gets the colours.
static void test_the_colours_are_the_prompt_an_error_and_a_directory() {
  ensure_booted();
  static char said[4096];
  // Off: the words are the same and the bytes are not there.
  kernel::shell::set_ansi(false);
  capture_begin();
  kernel::execute("cat /no-such-file");
  capture_end(said, sizeof(said));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "no such file"), said);
  TEST_ASSERT_NULL_MESSAGE(strstr(said, "\x1b"), said);
  // On: an error is red and goes back to the paper.
  kernel::shell::set_ansi(true);
  capture_begin();
  kernel::execute("cat /no-such-file");
  capture_end(said, sizeof(said));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "\x1b[1;31m"), said);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "\x1b[0m"), said);
  // A directory is blue and the kind of thing and the size are dim.
  capture_begin();
  kernel::execute("ls /");
  capture_end(said, sizeof(said));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "\x1b[1;34m"), said);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "\x1b[90m"), said);
  // And the prompt: the name in green, the `>` in grey, and the reset before the line the person types.
  capture_begin();
  kernel::shell::prompt();
  capture_end(said, sizeof(said));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "\x1b[1;32mlunok\x1b[0m"), said);
  // The bench goes back to being a pipe, so nothing after this test reads an escape byte.
  kernel::shell::set_ansi(false);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_version_command_is_known);
  RUN_TEST(test_only_a_device_attributes_reply_is_an_answer);
  RUN_TEST(test_the_colours_are_the_prompt_an_error_and_a_directory);
  RUN_TEST(test_help_command_is_known);
  RUN_TEST(test_help_with_a_command_is_the_lines_of_that_command);
  RUN_TEST(test_caps_command_is_known);
  RUN_TEST(test_mem_command_is_known);
  RUN_TEST(test_a_block_that_does_not_fit_the_ceilings_is_refused);
  RUN_TEST(test_the_heap_gives_every_byte_back_when_the_blocks_go);
  RUN_TEST(test_a_page_does_not_fit_a_home_that_has_it_in_pieces);
  RUN_TEST(test_mounts_command_lists_the_mounts);
  RUN_TEST(test_cfg_command_says_the_unit_and_refuses_a_missing_card);
  RUN_TEST(test_a_path_with_spaces_goes_in_quotes);
  RUN_TEST(test_the_listing_of_a_repository_is_read_in_chunks);
  RUN_TEST(test_the_entries_of_a_listing_come_with_their_address);
  RUN_TEST(test_cat_prints_the_whole_file_and_not_its_first_screen);
  RUN_TEST(test_cat_of_a_missing_file_fails);
  RUN_TEST(test_hexdump_of_a_missing_file_fails);
  RUN_TEST(test_a_path_that_is_not_there_says_where_the_rest_is);
  RUN_TEST(test_unknown_command_fails);
  RUN_TEST(test_empty_line_is_not_an_error);
  RUN_TEST(test_overlong_line_is_cut_not_crashed);
  RUN_TEST(test_board_has_a_console);
  RUN_TEST(test_kernel_is_running);
  RUN_TEST(test_an_app_without_its_entry_file_is_not_listed);
  RUN_TEST(test_the_extension_finder_counts_every_app_that_opens_it);
  RUN_TEST(test_a_call_through_the_vfs_is_counted_and_timed);
  RUN_TEST(test_stats_prints_the_table_and_resets_it);
  RUN_TEST(test_stats_prints_the_meters_that_were_never_called);
  RUN_TEST(test_a_log_line_carries_its_level_and_its_tag);
  RUN_TEST(test_the_log_is_painted_by_level);
  RUN_TEST(test_a_duration_is_written_in_the_scale_that_reads);
  UNITY_END();
  return 0;
}
