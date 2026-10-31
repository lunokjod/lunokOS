// M0 tests: the shell answers what it says it answers, and it says so out loud.
//
// These run on the host build with a fake board (lib/hal_emulator) and the host
// PAL, so they exercise the same code the emulator runs.
#include <stddef.h>
#include <string.h>

#include <unity.h>

#include "hal/hal.h"
#include "kernel/apps.h"
#include "kernel/kernel.h"
#include "pal/pal.h"

void setUp(void) {}

void tearDown(void) {}

namespace {

bool g_booted = false;

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
  TEST_ASSERT_FALSE(kernel::execute("cat no-hi-es-aixo.txt"));
  TEST_ASSERT_FALSE(kernel::execute("cat /res"));
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
  RUN_TEST(test_unknown_command_fails);
  RUN_TEST(test_empty_line_is_not_an_error);
  RUN_TEST(test_overlong_line_is_cut_not_crashed);
  RUN_TEST(test_board_has_a_console);
  RUN_TEST(test_kernel_is_running);
  RUN_TEST(test_an_app_without_its_entry_file_is_not_listed);
  RUN_TEST(test_the_extension_finder_counts_every_app_that_opens_it);
  UNITY_END();
  return 0;
}
