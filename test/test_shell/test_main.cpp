// M0 tests: the shell answers what it says it answers, and it says so out loud.
//
// These run on the host build with a fake board (lib/hal_emulator) and the host
// PAL, so they exercise the same code the emulator runs.
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
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
#include "luabind/luabind.h"
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

namespace {

// **The writer of the cases of the apps**: it is defined with the rest of the helpers, further down, and the cases
// that start an app ask for it from here (docs/85).
bool write_text(hal::IStorage* storage, const char* path, const char* text);

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
// were red for days and why one default elsewhere in the emulator could turn them green without touching them:
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
  emu::Options opts{};
  opts.fs_dir = g_root;
  emu::set_options(opts);
  pal::init();
  // **The three doors of a disk that has just been formatted**: an empty volume has no `/apps`, `/res` or
  // `/data`, and the shell's paths are those, so the suite makes them once for every test in it. **`/apps` is not made
  // here on purpose** (0.88.19): `vfs::init` creates it, because the door of the apps has to be there on a disk that
  // never had one —the emulator carries no app at all, and `repo install` writes the first one—. A suite that made the
  // folder itself would not ask the question, and the question was a real bug: the install worked and `run` said
  // `cannot read: /apps/<id>/<entry>` (docs/70, docs/85).
  if (hal::IStorage* storage = hal::platform().storage("/littlefs")) {
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

  // **The beginning of a command is not the command**: `pow` is not `power`, and what the board says is that it does
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
// emulator hands out the board's own RAM and its extra memory from the machine's heap, and a block that
// does not fit either home is refused here, before the board would have refused it. The bytes that come
// back are the bytes that went out, a block that has to move keeps what it held, and a home that is
// asked for and full is not the other home's refusal.
static void test_a_block_that_does_not_fit_the_ceilings_is_refused() {
  ensure_booted();
  // **The books start here and not at boot**: the blocks the boot took were handed out by an emulator with no
  // ceiling, and counting them now would make this test's arithmetic the boot's (docs/85).
  // **The home of this case is the floor plus four kilobytes** and not four kilobytes: a block goes to the
  // chip's own RAM only while sixty-four kilobytes are left after it (0.151.20, `pal::kHomeReserveBytes`), so a
  // home of 4096 B would send every block of this case to the extra memory and the case would be measuring the
  // floor the whole way instead of the block.
  const uint32_t kHome = static_cast<uint32_t>(pal::kHomeReserveBytes) + 4096;
  pal::set_memory_bytes(kHome, 8192);
  pal::IExtraMemory& memory = pal::extra_memory();

  const pal::MemoryLeft home_before = pal::memory_left(false);
  TEST_ASSERT_TRUE_MESSAGE(home_before.counted, "the home has no ceiling");
  TEST_ASSERT_EQUAL_UINT32(kHome, home_before.free_bytes);
  TEST_ASSERT_EQUAL_UINT32(8192, pal::memory_left(true).free_bytes);

  // **The block carries its own sixteen bytes of bookkeeping**, and one that leaves the home above its floor goes
  // to the chip's RAM: 4000 B of block are 4016 B of heap and eighty bytes are left over the floor, not ninety-six.
  char* first = static_cast<char*>(memory.allocate(4000, false));
  TEST_ASSERT_NOT_NULL(first);
  TEST_ASSERT_EQUAL_UINT32(pal::kHomeReserveBytes + 80, pal::memory_left(false).free_bytes);
  TEST_ASSERT_EQUAL_UINT32(8192, pal::memory_left(true).free_bytes);
  memset(first, 'a', 4000);

  // **The next one would take the home under its floor**, so the rule sends it to the extra memory and the home
  // does not move a byte: the floor is the home's and not the caller's (0.151.20).
  char* second = static_cast<char*>(memory.allocate(2000, false));
  TEST_ASSERT_NOT_NULL(second);
  TEST_ASSERT_EQUAL_UINT32(pal::kHomeReserveBytes + 80, pal::memory_left(false).free_bytes);
  TEST_ASSERT_EQUAL_UINT32(8192 - 2016, pal::memory_left(true).free_bytes);

  // **The floor says where a block goes first and not whether it goes**: a block of eight kilobytes would leave
  // the home under its floor, so the extra memory is asked first, and when that one cannot hold it the home still
  // does —a block is a block and an app does not have to know where it lives— (0.151.20, docs/75). What is refused
  // is the block that fits nowhere: sixty-six kilobytes against 65 616 B of home and 6 176 B of extra memory.
  void* under_the_floor = memory.allocate(8000, false);
  TEST_ASSERT_NOT_NULL(under_the_floor);
  TEST_ASSERT_EQUAL_UINT32(kHome - 4016 - 8016, pal::memory_left(false).free_bytes);
  memory.release(under_the_floor);
  TEST_ASSERT_EQUAL_UINT32(pal::kHomeReserveBytes + 80, pal::memory_left(false).free_bytes);
  TEST_ASSERT_NULL(memory.allocate(66000, false));

  // **A block that shrinks and moves to the extra memory**: 3000 B still do not leave the home above its floor,
  // so the resize takes the block where it can live, the bytes it held go with it, and the home it left is whole
  // again.
  first = static_cast<char*>(memory.resize(first, 3000, false));
  TEST_ASSERT_NOT_NULL(first);
  TEST_ASSERT_EQUAL_MEMORY("aaaa", first, 4);
  TEST_ASSERT_EQUAL_UINT32(kHome, pal::memory_left(false).free_bytes);
  TEST_ASSERT_EQUAL_UINT32(8192 - 2016 - 3016, pal::memory_left(true).free_bytes);

  // **And a block that grows where it is**: five thousand and sixteen bytes of the extra memory, which has them.
  first = static_cast<char*>(memory.resize(first, 5000, false));
  TEST_ASSERT_NOT_NULL(first);
  TEST_ASSERT_EQUAL_MEMORY("aaaa", first, 4);
  TEST_ASSERT_EQUAL_UINT32(kHome, pal::memory_left(false).free_bytes);
  TEST_ASSERT_EQUAL_UINT32(8192 - 2016 - 5016, pal::memory_left(true).free_bytes);

  memory.release(first);
  memory.release(second);
  TEST_ASSERT_EQUAL_UINT32(kHome, pal::memory_left(false).free_bytes);
  TEST_ASSERT_EQUAL_UINT32(8192, pal::memory_left(true).free_bytes);
  // The least that was left is the mark of the run and it does not go back up: `mem` prints it as the
  // `min free heap` of the board (docs/75). It is the moment the block of eight kilobytes was in the home, under
  // the floor, because the floor says the order and not the refusal.
  TEST_ASSERT_EQUAL_UINT32(kHome - 4016 - 8016, pal::memory_left(false).lowest_free_bytes);

  // **And an emulator with no model is an emulator with no ceiling**, which is what every other test of this
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

// **`&` sends an app to the background** (0.160.0, as the owner asked): the line starts the app and **gives the prompt
// back at once** —its stdin, its stdout and its stderr are detached from this console— so the shell keeps working
// while the app runs. **There is no thread and no second task**: the app is polled by the same turn of the kernel as
// always, one thing at a time, which is the whole point of doing it without one (docs/30, docs/50).
void test_an_ampersand_runs_an_app_in_the_background(void) {
  ensure_booted();
  static char said[4096];
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  const char kManifest[] =
      "{\"title\":\"Background\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}";
  storage->mkdir("/apps/lunok-bg");
  TEST_ASSERT_TRUE(write_text(storage, "/apps/lunok-bg/manifest.json", kManifest));
  // An app that says one line and then waits: it is alive while it waits, which is the state this case measures.
  TEST_ASSERT_TRUE(write_text(storage, "/apps/lunok-bg/app.lua",
                              "console.print('bg started')\nwhile true do sys.delay(20):await() end\n"));

  capture_begin();
  TEST_ASSERT_TRUE(kernel::execute("run lunok-bg &"));
  capture_end(said, sizeof(said));
  // **The app is alive and its words are not on this wire**: a detached app's console is the log, because two writers
  // on one prompt is the mess `&` exists to avoid.
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), "the app is alive");
  TEST_ASSERT_NULL_MESSAGE(strstr(said, "bg started"), said);

  // **And the prompt is the shell's**: a line typed now runs in the shell and not in the app.
  capture_begin();
  kernel::shell::feed("version\r", 9);
  capture_end(said, sizeof(said));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "lunokOS "), said);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "lunok> "), said);  // the prompt came back

  // **`&` is only for an app**: every other command is over before a person could type the next one, and the shell
  // says so instead of obeying in silence.
  capture_begin();
  TEST_ASSERT_FALSE(kernel::execute("version &"));
  capture_end(said, sizeof(said));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "only an app goes to the background"), said);

  // The emulator is left as it was found: the app in the background is closed, with its goodbye hooks.
  kernel::shell::close_app();
  TEST_ASSERT_FALSE_MESSAGE(kernel::app_running(), "the app is closed");
  storage->remove("/apps/lunok-bg/app.lua");
  storage->remove("/apps/lunok-bg/manifest.json");
  storage->remove("/apps/lunok-bg");
}

// **The shell is the system's and not the wire's** (0.159.0): anybody can ask for a session —an app asks for it with
// `sys.shell()`— and it is **the same shell** the console talks to, with three pipes instead of a terminal. The lines
// go in on `session_write`, the answers come out of `session_read` and the complaints out of `session_read_error`,
// with **no prompt and no echo**, because a pipe is not a terminal. What this case measures is that door: a command
// that answers, a command that is not a command (and the pipe it lands on), the second session that says no instead
// of waiting, `exit` that closes it from the inside, and the same thing again through Lua —which is the road an app
// has— (docs/30, docs/75).
void test_a_session_of_the_shell_is_a_shell_with_three_pipes(void) {
  ensure_booted();
  static char said[1200];

  char why[96] = {0};
  const uint8_t id = kernel::shell::open_session(why, sizeof(why));
  TEST_ASSERT_TRUE_MESSAGE(id > 0, why);

  // **The answer of a command**, and no prompt in it: a session reads what the command printed and nothing else.
  TEST_ASSERT_TRUE(kernel::shell::session_write(id, "version\n", 8));
  const size_t answered = kernel::shell::session_read(id, said, sizeof(said));
  TEST_ASSERT_TRUE_MESSAGE(answered > 0, said);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "lunokOS "), said);
  TEST_ASSERT_NULL_MESSAGE(strstr(said, "lunok> "), said);

  // **What went wrong is on its own pipe**: a word that is not a command answers nothing and complains once.
  TEST_ASSERT_TRUE(kernel::shell::session_write(id, "no_existeix\n", 12));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, static_cast<uint32_t>(kernel::shell::session_read(id, said, sizeof(said))),
                                   said);
  TEST_ASSERT_TRUE_MESSAGE(kernel::shell::session_read_error(id, said, sizeof(said)) > 0, said);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "unknown command: no_existeix"), said);

  // **One at a time**, and the second one is told why instead of waiting for a session that may never come.
  TEST_ASSERT_EQUAL_UINT8(0, kernel::shell::open_session(why, sizeof(why)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(why, "already a session"), why);

  // **`exit` closes the session from the inside**, which is how a shell is left, and the pipes go with it.
  TEST_ASSERT_TRUE(kernel::shell::session_write(id, "exit\n", 5));
  TEST_ASSERT_FALSE_MESSAGE(kernel::shell::session_open(id), "the session is closed");

  // **And an app asks for the same one**, with the three pipes of `sys.shell()`: what it prints goes to the console
  // of the app —another road, and it must not land in the session— (docs/30).
  luabind::Boot boot;
  boot.board = "test";
  boot.version = "0.1.0";
  boot.emulator = true;
  TEST_ASSERT_TRUE_MESSAGE(luabind::start(boot, 131072), "the interpreter starts");
  static const char kScript[] =
      "local s = sys.shell()\n"
      "if s == nil then print('no session') return end\n"
      "s.stdin:write('version\\n')\n"
      "local said = s.stdout:read()\n"
      "print(said ~= nil and said ~= '' and 'session answered' or 'session silent')\n"
      "s.stdin:write('no_existeix\\n')\n"
      "local why = s.stderr:read()\n"
      "print(why ~= nil and why ~= '' and 'complaint read' or 'no complaint')\n"
      "s:close()\n"
      "print('session closed')\n";
  capture_begin();
  TEST_ASSERT_TRUE(luabind::eval(kScript, sizeof(kScript) - 1));
  capture_end(said, sizeof(said));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "session answered"), said);   // the shell's own line came back
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "complaint read"), said);     // and the complaint on its pipe
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "session closed"), said);
  TEST_ASSERT_NULL_MESSAGE(strstr(said, "no session"), said);
  luabind::stop();
}

// **A console that can be hung up and a console that cannot** (0.158.0, as the owner asked): `exit`, `logout` and
// Ctrl+D ask for a closed session, and **what closing means is the console's business** (`IConsole::virtual_terminal`).
// On a **serial port** nobody is at the other end to close the wire and it is the only way into a board with no
// glass, so the session is not closed at all: the board says so, nothing that arrives is a line, and the **Enter** of
// the terminal opens a new one. On a **virtual terminal** —the console of the emulator, a program on a PC that somebody
// started— the session really closes and the run stops, which is what `poweroff` does on the emulator (docs/30, docs/50,
// docs/85).
void test_the_session_of_the_console_closes_and_opens_again(void) {
  ensure_booted();
  static char said[4096];

  // The console of this half of the case is the **serial port of a board**, which is the default of a test's PAL:
  // nobody is at the other end to hang it up (docs/30, docs/85).
  pal::set_virtual_terminal(false);

  capture_begin();
  TEST_ASSERT_TRUE(kernel::execute("logout"));
  capture_end(said, sizeof(said));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "logout: the session is closed; press Enter to open a new one"), said);
  // **A closed session does not ask**: the prompt is the sign of a session that is listening, and it is not there.
  TEST_ASSERT_NULL_MESSAGE(strstr(said, "lunok> "), said);

  // **A letter is neither a line nor a key**: while the session is closed nothing runs and nothing opens it.
  capture_begin();
  kernel::shell::feed("help", 4);
  capture_end(said, sizeof(said));
  TEST_ASSERT_NULL_MESSAGE(strstr(said, "login:"), said);
  TEST_ASSERT_NULL_MESSAGE(strstr(said, "lunok> "), said);

  // **The Enter opens it**, and the line that arrives after it runs.
  capture_begin();
  kernel::shell::feed("\r", 1);
  kernel::shell::feed("help\r", 5);
  capture_end(said, sizeof(said));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "login: a new session"), said);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "lunok> "), said);        // the prompt of the new session
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "help [command]"), said);  // the line ran
  TEST_ASSERT_NULL_MESSAGE(strstr(said, "unknown command"), said);

  // `exit` is the other name of the same verb.
  capture_begin();
  TEST_ASSERT_TRUE(kernel::execute("exit"));
  capture_end(said, sizeof(said));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "press Enter to open a new one"), said);

  // **Ctrl+D is `exit`**, and the line of the moment goes with the session instead of running.
  capture_begin();
  kernel::shell::feed("\r", 1);       // the Enter that opens it again
  kernel::shell::feed("version", 7);  // a line that must not run
  kernel::shell::feed("\x04", 1);     // Ctrl+D
  capture_end(said, sizeof(said));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "logout: the session is closed"), said);
  TEST_ASSERT_NULL_MESSAGE(strstr(said, "lunokOS "), said);  // `version` was not run

  // **And the console of the emulator is a virtual terminal**: `logout` closes the session for real and the run stops.
  // The case boots again so that the ones after it find the emulator running with its console open (docs/85).
  kernel::shell::feed("\r", 1);
  pal::set_virtual_terminal(true);
  capture_begin();
  TEST_ASSERT_TRUE(kernel::execute("logout"));
  capture_end(said, sizeof(said));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "logout: the session is closed"), said);
  TEST_ASSERT_FALSE_MESSAGE(kernel::running(), said);
  kernel::boot(hal::platform());
  TEST_ASSERT_TRUE_MESSAGE(kernel::running(), "the emulator is running again");

  // **`help` answers each of the two names**: they are two lines of the list and not one with a comma, because a
  // word of the list has to be the whole command —`help pow` is not `power`— (docs/30).
  capture_begin();
  TEST_ASSERT_TRUE(kernel::execute("help logout"));
  capture_end(said, sizeof(said));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "the same as exit"), said);

  // The emulator is left as it was found: a console that cannot be hung up, which is what the cases after this one have.
  pal::set_virtual_terminal(false);
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
  // The extension of a manifest arrives as a list of one since 0.120.0, and `.LUNOK` is `lunok` (docs/40).
  TEST_ASSERT_EQUAL_UINT32(1, found.opens_count);
  TEST_ASSERT_EQUAL_STRING("lunok", found.opens[0]);

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

// **`cat` prints the whole file and not its first screen** (0.82.2): it read one window of 1 024 B with
// `vfs::read_file` and printed it without saying anything, so a page of `http` stopped after a screen and a half and
// looked like a cut file. The test writes 4 KB with a mark on the last line and looks for it; what a big `cat` costs is
// the wire and not a ceiling (docs/30, docs/70).
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

  // And a file that does not end with a newline gets one, or the prompt would be glued to the last letter.
  TEST_ASSERT_TRUE(write_text(storage, "/nonl.txt", "sense salt"));
  capture_begin();
  TEST_ASSERT_TRUE(kernel::execute("cat /littlefs/nonl.txt"));
  capture_end(said, sizeof(said));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "sense salt\r\n"), said);

  storage->remove("/long.txt");
  storage->remove("/nonl.txt");
}

// **A path that is not there says why and where the one that is there lives** (0.86.1): `ls /sd/app/timers` with an
// `apps` next to it is what finds the letter that is missing, and a path that is a file is listed with its name and its
// size instead of saying it is not a folder. It is the same for `cat`, which already said "no such file" and now also
// says where the piece that exists is (docs/30, docs/70).
void test_a_path_that_is_not_there_says_where_the_rest_is(void) {
  ensure_booted();
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  storage->mkdir("/apps");
  storage->mkdir("/apps/listed");
  TEST_ASSERT_TRUE(write_text(storage, "/apps/listed/one.lua", "x\n"));
  // **The hint is what the door presents** (0.88.20): `/apps` is the one entry point of the apps and a folder that
  // carries no `manifest.json` is not an app, so `listed` says it is one —the point of this test is the letter that is
  // missing in `/apps/listd`, and the name offered has to be a name the door would really list (docs/40, docs/70).
  TEST_ASSERT_TRUE(write_text(storage, "/apps/listed/manifest.json",
                              "{\"title\":\"Listed\",\"version\":\"0.1.0\",\"entry\":\"one.lua\","
                              "\"boards\":[\"esp32\"]}\n"));

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

  // **The piece that is there, and what it holds**: `/apps/listd` is not there and what is next to it is `listed`.
  capture_begin();
  TEST_ASSERT_TRUE(kernel::execute("ls /apps/listd/one.lua"));
  capture_end(said, sizeof(said));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "no such directory: /apps/listd/one.lua"), said);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "/apps has:"), said);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "listed"), said);

  // And `cat` says where it is all the same, which is the same path typed by hand.
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

  // **`/apps` is not removed**: it is the folder the other tests of this file use, and the `remove` of a folder is
  // this backend's and not a rule of the test (docs/70).
  storage->remove("/apps/listed/one.lua");
  storage->remove("/apps/listed");
}

// **The listing of a directory of GitHub, without holding it whole** (0.82.0): the `repository` command downloads the
// JSON of the contents API and takes the folders out of it with a window of two kilobytes. The document of the test is
// bigger than the window on purpose, and one object falls right in the middle to check that half an object is kept for
// the piece after (docs/70, docs/75).
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

  // **A file that says it is a listing and is not turns around and does not hang**: what is not recognised is not a
  // folder (docs/30).
  TEST_ASSERT_TRUE(write_text(storage, "/broken.json", "{\"name\": \"x\""));
  kernel::repository::Listing broken;
  TEST_ASSERT_TRUE(broken.open("/littlefs/broken.json"));
  TEST_ASSERT_FALSE(broken.next(name, sizeof(name)));
  broken.close();
  TEST_ASSERT_FALSE(broken.open("/littlefs/nowhere.json"));

  storage->remove("/listing.json");
  storage->remove("/broken.json");
}

// **The entries of a listing, with their type and their address** (0.86.0): `repo install` downloads the whole
// directory of an app, and that is why it wants the files (with the raw address to download them from) and the folders
// (to ask for their listing). The address is the only string of the document that is not a name, and the test wants it
// whole: the buffer of a name cut it at forty-eight bytes and the file came back as a 404 (docs/30, docs/70).
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
  // **A folder has no address**, and it is not an empty address that decides it: it is the `type` (docs/30).
  TEST_ASSERT_TRUE(kernel::repository::entry_at("/littlefs/files.json", 1, kind, name, sizeof(name), url, sizeof(url)));
  TEST_ASSERT_EQUAL(kernel::repository::Kind::kDir, kind);
  TEST_ASSERT_EQUAL_STRING("res", name);
  TEST_ASSERT_EQUAL_STRING("", url);
  TEST_ASSERT_TRUE(kernel::repository::entry_at("/littlefs/files.json", 2, kind, name, sizeof(name), url, sizeof(url)));
  TEST_ASSERT_EQUAL_STRING("images.lua", name);
  // What goes past the end is no entry, and a document that is not one has none either.
  TEST_ASSERT_FALSE(kernel::repository::entry_at("/littlefs/files.json", 3, kind, name, sizeof(name), url, sizeof(url)));
  TEST_ASSERT_FALSE(kernel::repository::entry_at("/littlefs/nowhere.json", 0, kind, name, sizeof(name), url, sizeof(url)));

  storage->remove("/files.json");
}

// **The listing is one object and not a frame of the stack** (0.88.4): it carries a window of two kilobytes, a name and
// an address —about 2 300 B—, and the stack of the loop task is sixteen with the shell, the radio and the install
// already on it; the QR of four kilobytes broke the board once with a canary watchpoint (docs/75). So the reader the
// install and the shell walk is the same object every time, **and an open that fails does not leave a state behind**:
// the next one starts from the beginning.
void test_the_listing_is_one_object_and_not_a_frame_of_the_stack(void) {
  ensure_booted();
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  static const char json[] = "[\n  {\"name\": \"notes\", \"type\": \"dir\"},\n"
                             "  {\"name\": \"app.lua\", \"download_url\": null, \"type\": \"file\"}\n]\n";
  // **A second listing with two folders in it**, to see which object `entry_at` walks: if it built one of its own, the
  // shared reader would still be on this file, half way through.
  static const char other[] = "[\n  {\"name\": \"photos\", \"type\": \"dir\"},\n"
                              "  {\"name\": \"music\", \"type\": \"dir\"}\n]\n";
  TEST_ASSERT_TRUE(write_text(storage, "/one.json", json));
  TEST_ASSERT_TRUE(write_text(storage, "/two.json", other));

  kernel::repository::Listing& first = kernel::repository::Listing::one();
  // Every caller gets the same object: two of them would be two windows of two kilobytes, and the point is to have
  // one of each.
  TEST_ASSERT_EQUAL_PTR(&first, &kernel::repository::Listing::one());

  // A file that is not there leaves the reader as it was: the next open starts at the first byte of the new file.
  TEST_ASSERT_TRUE(first.open("/littlefs/nowhere.json") == false);
  char name[48];
  TEST_ASSERT_FALSE(first.next(name, sizeof(name)));
  TEST_ASSERT_TRUE(first.open("/littlefs/one.json"));
  // **`next` is the short way for a list of folders** and a file in the document is not one of them (docs/30).
  TEST_ASSERT_TRUE(first.next(name, sizeof(name)));
  TEST_ASSERT_EQUAL_STRING("notes", name);
  TEST_ASSERT_FALSE(first.next(name, sizeof(name)));
  first.close();

  // And `entry_at`, which walks for a caller that asks one entry at a time, uses that same object and answers the same
  // thing after the walk above.
  kernel::repository::Kind kind = kernel::repository::Kind::kFile;
  char url[148];
  TEST_ASSERT_TRUE(kernel::repository::entry_at("/littlefs/one.json", 0, kind, name, sizeof(name), url, sizeof(url)));
  TEST_ASSERT_EQUAL_STRING("notes", name);
  TEST_ASSERT_EQUAL(kernel::repository::Kind::kDir, kind);
  TEST_ASSERT_TRUE(kernel::repository::entry_at("/littlefs/one.json", 1, kind, name, sizeof(name), url, sizeof(url)));
  TEST_ASSERT_EQUAL_STRING("app.lua", name);
  TEST_ASSERT_EQUAL(kernel::repository::Kind::kFile, kind);

  // **`entry_at` walks the shared reader and leaves it closed**: it is put on the second file and half walked, and an
  // `entry_at` on the first one takes the object away from it —which is what a frame of its own would not do—, so the
  // next name of the first walk is not there any more.
  TEST_ASSERT_TRUE(first.open("/littlefs/two.json"));
  TEST_ASSERT_TRUE(first.next(name, sizeof(name)));
  TEST_ASSERT_EQUAL_STRING("photos", name);
  TEST_ASSERT_TRUE(kernel::repository::entry_at("/littlefs/one.json", 0, kind, name, sizeof(name), url, sizeof(url)));
  TEST_ASSERT_EQUAL_STRING("notes", name);
  TEST_ASSERT_FALSE(first.next(name, sizeof(name)));

  storage->remove("/one.json");
  storage->remove("/two.json");
}

// **A listing is walked once and not once per entry** (0.88.5): `entry_at` answers an index by reading the document from
// its first byte, so asking it for the twenty files of an app was twenty opens and twenty passes over forty-four
// kilobytes of JSON. `start` / `next` / `stop` walk it in order —one open, one pass— and give the same entries, which is
// what an install asks for. The two ways are measured here on a document of forty entries: the numbers are of the emulator
// and not of the board, but the ratio is the point (docs/75).
void test_a_listing_is_walked_once_and_not_once_per_entry(void) {
  ensure_booted();
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);

  // Forty entries of about a hundred bytes, like the real answer of the contents API.
  static char json[6144];
  size_t used = 0;
  used += static_cast<size_t>(snprintf(json + used, sizeof(json) - used, "[\n"));
  for (int i = 1; i <= 40; ++i) {
    used += static_cast<size_t>(snprintf(
        json + used, sizeof(json) - used,
        "  {\n    \"name\": \"app%02d\",\n    \"path\": \"apps/app%02d\",\n"
        "    \"sha\": \"0123456789abcdef0123456789abcdef01234567\",\n    \"size\": 0,\n"
        "    \"type\": \"dir\"\n  }%s\n",
        i, i, (i < 40) ? "," : ""));
  }
  used += static_cast<size_t>(snprintf(json + used, sizeof(json) - used, "]\n"));
  TEST_ASSERT_TRUE(write_text(storage, "/walk.json", json));

  // The way of before: one call per entry, each of them from the first byte of the document.
  char name[48];
  char url[148];
  kernel::repository::Kind kind = kernel::repository::Kind::kFile;
  const uint32_t indexed_from = meter::now_us();
  uint32_t indexed = 0;
  for (uint32_t i = 0; i < 40; ++i) {
    if (!kernel::repository::entry_at("/littlefs/walk.json", i, kind, name, sizeof(name), url, sizeof(url))) break;
    ++indexed;
  }
  const uint32_t indexed_us = meter::now_us() - indexed_from;
  TEST_ASSERT_EQUAL_UINT32(40, indexed);

  // The way of now: the document is opened once and the entries come out one behind the other.
  TEST_ASSERT_TRUE(kernel::repository::start("/littlefs/walk.json"));
  const uint32_t walked_from = meter::now_us();
  uint32_t walked = 0;
  char first[48] = {0};
  char last[48] = {0};
  while (kernel::repository::next(kind, name, sizeof(name), url, sizeof(url))) {
    if (walked == 0) memcpy(first, name, sizeof(first));
    memcpy(last, name, sizeof(last));
    ++walked;
  }
  const uint32_t walked_us = meter::now_us() - walked_from;
  // **The reader has to be given back**: every entry, the same ones, and the next question after `stop` says there is
  // no more, which is a reader that closed its file.
  kernel::repository::stop();
  TEST_ASSERT_EQUAL_UINT32(40, walked);
  TEST_ASSERT_EQUAL_STRING("app01", first);
  TEST_ASSERT_EQUAL_STRING("app40", last);
  TEST_ASSERT_FALSE(kernel::repository::next(kind, name, sizeof(name), url, sizeof(url)));
  TEST_ASSERT_FALSE(kernel::repository::start("/littlefs/nowhere.json"));

  printf("listing: 40 entries, %u us with one call each, %u us in one walk\n",
         static_cast<unsigned>(indexed_us), static_cast<unsigned>(walked_us));

  storage->remove("/walk.json");
}

// **A document deeper than a signed byte does not invent an entry** (0.88.7): the depth of the two walkers was a `char`,
// and a listing with a hundred and thirty objects one inside the other took it past `CHAR_MAX` and into negative
// numbers —signed overflow, which is undefined behaviour—. With a negative depth the reset of the name at the end of an
// object never comes (`depth == 0` is never true), so the `name` of one object is answered as the entry of a later
// `type` that has no name of its own: an entry that is not in the answer, and a file an install would download for
// nobody (docs/30, docs/70).
void test_a_listing_deeper_than_a_signed_byte_does_not_invent_an_entry(void) {
  ensure_booted();
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  static char json[2048];
  size_t used = 0;
  used += static_cast<size_t>(snprintf(json + used, sizeof(json) - used, "[\n  {\n"));
  // A hundred and thirty objects one inside the other, with a name in the deepest one and no type of its own.
  for (int i = 0; i < 130; ++i) used += static_cast<size_t>(snprintf(json + used, sizeof(json) - used, "{\n"));
  used += static_cast<size_t>(snprintf(json + used, sizeof(json) - used, "\"name\": \"ghost\"\n"));
  for (int i = 0; i < 130; ++i) used += static_cast<size_t>(snprintf(json + used, sizeof(json) - used, "}\n"));
  // And a second object of the answer, this one with a type and no name.
  used += static_cast<size_t>(snprintf(json + used, sizeof(json) - used, "  },\n  {\n    \"type\": \"dir\"\n  }\n]\n"));
  TEST_ASSERT_TRUE(write_text(storage, "/deep.json", json));

  kernel::repository::Kind kind = kernel::repository::Kind::kFile;
  char name[48];
  char url[148];
  TEST_ASSERT_TRUE(kernel::repository::start("/littlefs/deep.json"));
  // **No entry at all**: the deep object has a name and no type, and the one that has a type has no name.
  TEST_ASSERT_FALSE(kernel::repository::next(kind, name, sizeof(name), url, sizeof(url)));
  kernel::repository::stop();

  // **The walker of folders carries the same counter and the same fix**: a hundred and thirty objects one inside the
  // other with a name at the bottom is not a folder of anybody (docs/30).
  kernel::repository::Listing& folders = kernel::repository::Listing::one();
  TEST_ASSERT_TRUE(folders.open("/littlefs/deep.json"));
  TEST_ASSERT_FALSE(folders.next(name, sizeof(name)));
  folders.close();

  storage->remove("/deep.json");
}

// **A key that does not fit is dropped whole and never compared from its beginning** (0.88.8): the key was read into
// sixteen bytes and cut at fifteen without a word, and the lesson of the address that was cut at forty-eight and cost a
// 404 was not applied to it. A key cut in the middle can only ever fail a comparison, so what it must not do is what a
// prefix comparison would do —take the address of a field that only starts like `download_url`— and what it must not do
// either is take the real fields of the object with it (docs/30, docs/75).
void test_a_key_longer_than_the_buffer_is_dropped_and_does_not_match(void) {
  ensure_booted();
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  // **The long key starts with `download_url` and comes before it**: a reader that looked at the beginning of the key
  // would give the entry the address of this field, which is a file the listing never named (docs/30).
  static const char json[] =
      "[\n"
      "  {\n"
      "    \"download_url_and_this_is_not_the_one_of_the_entry\": \"https://h/a/wrong.txt\",\n"
      "    \"name\": \"right.txt\",\n"
      "    \"download_url\": \"https://h/a/right.txt\",\n"
      "    \"type\": \"file\"\n"
      "  }\n"
      "]\n";
  TEST_ASSERT_TRUE(write_text(storage, "/longkey.json", json));

  kernel::repository::Kind kind = kernel::repository::Kind::kDir;
  char name[48];
  char url[148];
  TEST_ASSERT_TRUE(
      kernel::repository::entry_at("/littlefs/longkey.json", 0, kind, name, sizeof(name), url, sizeof(url)));
  TEST_ASSERT_EQUAL(kernel::repository::Kind::kFile, kind);
  // **The name and the address of the entry and not the ones of the field of before**: the key that did not fit was
  // dropped whole, so the object is read as if it were not there (docs/30).
  TEST_ASSERT_EQUAL_STRING("right.txt", name);
  TEST_ASSERT_EQUAL_STRING("https://h/a/right.txt", url);
  // And the answer has one entry and not two.
  TEST_ASSERT_FALSE(
      kernel::repository::entry_at("/littlefs/longkey.json", 1, kind, name, sizeof(name), url, sizeof(url)));

  storage->remove("/longkey.json");
}

// **The terminal of the person who ran the suite comes back as it was, however many times the board came up**
// (0.88.17): `pal::init()` puts stdin in raw mode —a key at a time, no echo— and saves what it found to put it back at
// the end. The suites that boot the interpreter once per test call it forty-three times, and the second call saved the
// **raw** mode that the first had just put there, so the restore at the end of the process put raw mode back and the
// terminal was left with no echo until a `reset` (found the 4/11, docs/80, docs/85).
void test_the_terminal_comes_back_after_more_than_one_init(void) {
  // **No terminal, nothing to save**: the suite can run with its stdin on a file, and then this is not a thing.
  if (isatty(STDIN_FILENO) == 0) return;
  // Whatever the test before left, this starts from a terminal the emulator has given back.
  pal::shutdown();
  struct termios before;
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, tcgetattr(STDIN_FILENO, &before), "the terminal cannot be read");

  // The board comes up three times, which is what a suite of forty tests does.
  pal::init();
  pal::init();
  pal::init();
  // And the raw mode is on, which is what lets a key be read without a line.
  struct termios during;
  TEST_ASSERT_EQUAL_INT(0, tcgetattr(STDIN_FILENO, &during));
  TEST_ASSERT_TRUE_MESSAGE((during.c_lflag & ECHO) == 0, "the emulator did not put stdin in raw mode");

  pal::shutdown();
  struct termios after;
  TEST_ASSERT_EQUAL_INT(0, tcgetattr(STDIN_FILENO, &after));
  // **The flags of before and not the raw ones**: a terminal with no `ECHO` and no `ICANON` after the suite is a shell
  // nobody can type into (docs/85).
  TEST_ASSERT_EQUAL_MESSAGE(before.c_lflag, after.c_lflag, "the flags of the terminal are not the ones of before");
  TEST_ASSERT_EQUAL_MESSAGE(before.c_cc[VMIN], after.c_cc[VMIN], "VMIN is not the one of before");
  TEST_ASSERT_EQUAL_MESSAGE(before.c_cc[VTIME], after.c_cc[VTIME], "VTIME is not the one of before");
}

// **The door of the apps opens on a disk that never had one** (0.88.19): `/apps` is the virtual directory that puts
// `littlefs/apps` and `sd/apps` together, and the emulator carries no app at all, so `repo install` writes the first app
// into a disk that has no `apps/`. The row was decided at boot and nothing looked again: the install worked, `apps`
// listed the app (it walks the drive) and `run` answered `cannot read: /apps/<id>/<entry>` because the door was shut
// (found on the emulator, 4/11, docs/40, docs/70).
void test_the_door_of_the_apps_opens_on_a_disk_that_had_none(void) {
  ensure_booted();
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  // The disk of this suite is made without `apps`, and it is `vfs::init` that makes it.
  hal::Stat info;
  TEST_ASSERT_TRUE_MESSAGE(kernel::vfs::stat("/apps", info), "the door of the apps is not there");
  TEST_ASSERT_EQUAL(hal::NodeType::kDir, info.type);

  // **And a folder that appears later is the door's**, without a second boot: the path a `repo install` writes is the
  // one `run` reads (docs/70).
  storage->mkdir("/apps/late");
  TEST_ASSERT_TRUE(write_text(storage, "/apps/late/app.lua", "late\n"));
  char text[16] = {0};
  size_t len = 0;
  TEST_ASSERT_TRUE(kernel::vfs::read_file("/apps/late/app.lua", text, sizeof(text), len));
  TEST_ASSERT_EQUAL_STRING("late\n", text);
  TEST_ASSERT_EQUAL_UINT32(5, static_cast<uint32_t>(len));

  storage->remove("/apps/late/app.lua");
  storage->remove("/apps/late");
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

// **A command that opens a file says which file** (0.164.4; the owner: *«si cat demana <path> perqué hexdump te <path>
// per defecte?»*): `hexdump` with nothing after it answered with the 7 168 B of `/rtc`, which nobody had asked for, and
// `cat` said `usage: cat <path>`. The two say the same thing now and neither reads a file the person did not name
// (docs/30, docs/70).
void test_hexdump_with_no_path_says_what_it_wants(void) {
  ensure_booted();
  static char said[1024];
  capture_begin();
  const bool ran = kernel::execute("hexdump");
  capture_end(said, sizeof(said));
  TEST_ASSERT_FALSE_MESSAGE(ran, "a command with no path is not a command that ran");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "usage: hexdump <path>"), said);
  // **And nothing of the memory is on the wire**: the failure is a usage line and not a dump of 7 168 bytes.
  TEST_ASSERT_NULL_MESSAGE(strstr(said, "0000"), said);
  // `cat` says the same, which is the point of the change.
  capture_begin();
  TEST_ASSERT_FALSE(kernel::execute("cat"));
  capture_end(said, sizeof(said));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, "usage: cat <path>"), said);
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
  // on the ATOM and on the Paper without knowing which one the emulator is.
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
  // The emulator goes back to being a pipe, so nothing after this test reads an escape byte.
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
  RUN_TEST(test_the_terminal_comes_back_after_more_than_one_init);
  RUN_TEST(test_the_door_of_the_apps_opens_on_a_disk_that_had_none);
  RUN_TEST(test_a_path_with_spaces_goes_in_quotes);
  RUN_TEST(test_the_listing_of_a_repository_is_read_in_chunks);
  RUN_TEST(test_the_entries_of_a_listing_come_with_their_address);
  RUN_TEST(test_the_listing_is_one_object_and_not_a_frame_of_the_stack);
  RUN_TEST(test_a_listing_is_walked_once_and_not_once_per_entry);
  RUN_TEST(test_a_listing_deeper_than_a_signed_byte_does_not_invent_an_entry);
  RUN_TEST(test_a_key_longer_than_the_buffer_is_dropped_and_does_not_match);
  RUN_TEST(test_cat_prints_the_whole_file_and_not_its_first_screen);
  RUN_TEST(test_cat_of_a_missing_file_fails);
  RUN_TEST(test_hexdump_of_a_missing_file_fails);
  RUN_TEST(test_hexdump_with_no_path_says_what_it_wants);
  RUN_TEST(test_a_path_that_is_not_there_says_where_the_rest_is);
  RUN_TEST(test_unknown_command_fails);
  RUN_TEST(test_empty_line_is_not_an_error);
  RUN_TEST(test_the_session_of_the_console_closes_and_opens_again);
  RUN_TEST(test_a_session_of_the_shell_is_a_shell_with_three_pipes);
  RUN_TEST(test_an_ampersand_runs_an_app_in_the_background);
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
