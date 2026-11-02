// M0 tests: the shell answers what it says it answers, and it says so out loud.
//
// These run on the host build with a fake board (lib/hal_emulator) and the host
// PAL, so they exercise the same code the emulator runs.
#include <stddef.h>
#include <string.h>

#include <unity.h>

#include "hal/hal.h"
#include "hal/log.h"
#include "kernel/apps.h"
#include "kernel/kernel.h"
#include "kernel/log.h"
#include "kernel/meter.h"
#include "kernel/vfs.h"
#include "pal/pal.h"

// The meters and the test file are in different namespaces: one line and the rest reads the same as in
// the kernel (docs/30).
namespace meter = kernel::meter;

void setUp(void) {}

void tearDown(void) {}

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

void ensure_booted() {
  if (g_booted) return;
  pal::init();
  kernel::boot(hal::platform());
  g_booted = true;
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

void test_caps_command_is_known(void) {
  ensure_booted();
  TEST_ASSERT_TRUE(kernel::execute("caps"));
}

void test_mem_command_is_known(void) {  ensure_booted();
  TEST_ASSERT_TRUE(kernel::execute("mem"));
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

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_version_command_is_known);
  RUN_TEST(test_help_command_is_known);
  RUN_TEST(test_caps_command_is_known);
  RUN_TEST(test_mem_command_is_known);
  RUN_TEST(test_mounts_command_lists_the_mounts);
  RUN_TEST(test_cfg_command_says_the_unit_and_refuses_a_missing_card);
  RUN_TEST(test_a_path_with_spaces_goes_in_quotes);
  RUN_TEST(test_cat_of_a_missing_file_fails);
  RUN_TEST(test_hexdump_of_a_missing_file_fails);
  RUN_TEST(test_unknown_command_fails);
  RUN_TEST(test_empty_line_is_not_an_error);
  RUN_TEST(test_overlong_line_is_cut_not_crashed);
  RUN_TEST(test_board_has_a_console);
  RUN_TEST(test_kernel_is_running);
  RUN_TEST(test_an_app_without_its_entry_file_is_not_listed);
  RUN_TEST(test_the_extension_finder_counts_every_app_that_opens_it);
  RUN_TEST(test_a_call_through_the_vfs_is_counted_and_timed);
  RUN_TEST(test_stats_prints_the_table_and_resets_it);
  RUN_TEST(test_a_log_line_carries_its_level_and_its_tag);
  RUN_TEST(test_a_duration_is_written_in_the_scale_that_reads);
  UNITY_END();
  return 0;
}
