// Whole apps, end to end: a real app on a real (emulated) disk, started the way
// the shell starts it, with what it writes captured and checked.
//
// This is the level docs/80 calls "app": the manifest, the interpreter, the
// promises, the console and the exit code working together. A unit test can be
// green while an app does not run; this one cannot.
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <unity.h>

#include "alarms/alarms.h"
#include "emu/gt911.h"
#include "emu/memories.h"
#include "emu/options.h"
#include "emu/surface.h"
#include "hal/hal.h"
#include "json/json.h"
#include "kernel/apps.h"
#include "kernel/cfg.h"
#include "kernel/kernel.h"
#include "kernel/meter.h"
#include "luabind/luabind.h"
#include "kernel/power.h"
#include "kernel/shell.h"
#include "kernel/touch.h"
#include "kernel/ui.h"
#include "kernel/vfs.h"
#include "pal/pal.h"
#include "pal_host.h"

namespace {
void close_any_app();
}  // namespace

namespace {

// **The console of the test, while a capture holds it** (docs/80). The two numbers are file descriptors and they live
// here and not inside the function that takes the console away, because **a test that fails does not come back through
// it**: Unity's `TEST_ASSERT_*` leaves through a `longjmp`, so the line that puts stdout back was never reached and the
// console stayed pointing at the file of the capture —the failure was written there, and so was every test after it,
// which is a run that looks hung with no output at all (found the 4/11)—.
int g_console_saved = -1;
int g_console_sink = -1;
const char* const kConsolePath = "/tmp/lunok-app-tests.out";

// What the console said while it was captured, said again on the console of the test: a failure inside a captured call
// is the one time a person needs to read it.
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

void console_give_back(bool replay);

// Takes the console away: everything written to stdout goes to the file until `console_give_back`. It answers false
// when there is no file to write, and then the console is the console and a test still runs.
bool console_take_away() {
  if (g_console_saved >= 0) return true;  // a capture already open: the file is the same one
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

// **And gives it back, however the test ended**: `replay` is for a test that failed, which is the one whose captured
// console nobody has seen.
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

}  // namespace

void setUp(void) {}

void tearDown(void) {
  // **The console comes back before anything else**: a test that failed with a capture open left stdout pointing at
  // the file of the capture, and Unity writes the failure and the rest of the run to wherever stdout points. It is the
  // first line of `tearDown` because Unity calls it for every test, passed, failed or blown up (docs/80).
  console_give_back(Unity.CurrentTestFailed != 0);
  // A test that fails halfway leaves its app running, and the next test then says
  // "cannot start the interpreter" and blames code that is fine — found by breaking
  // the fake sensor on purpose (31/10). Three taps are the system's way out and a
  // person's too, so every test leaves the way a person would (docs/30).
  //
  // And with the dial up: a key left down is a key the next app reads as pressed, and
  // that is how the keys app printed "value 1 (the middle of the face…)" in a window
  // where nobody had pressed the middle (31/10).
  for (int key = 0; key < 3; ++key) emu::press_key(key, false);
  close_any_app();
  // And the bench goes back to the ATOM: one binary is one bench, and a test that was
  // the Matrix would make the next one refuse an app for no reason (docs/85).
  const bool had_card = emu::options().sd_dir != nullptr;
  const bool was_another_board = emu::options().board != nullptr;
  emu::Options opts = emu::options();
  opts.board = nullptr;
  opts.sd_dir = nullptr;
  opts.battery_mv = 0;
  emu::set_options(opts);
  // Going back is a boot and not just an option: the hardware the kernel installs —the sensor, the
  // gauge, the light— is the board's, and a kernel that booted as a Paper is a kernel with no light
  // for ever (docs/85).
  if (was_another_board) kernel::boot(hal::platform());
  if (had_card) {
    // The mounts were built with a card in them, so out it goes with the table that knew
    // about it: a test that left `/sd` pointing at its own directory would give the next
    // one a card nobody inserted (docs/70).
    kernel::vfs::init(hal::platform());
    kernel::cfg::boot(hal::platform());
  }
}

namespace {

const char kRoot[] = "/tmp/lunok-app-tests";
const int kTurnBudget = 2000;  // turns of 1 ms an app may take in a test

// A disk of this run and no other: a leftover from the run before would add apps
// that the tests did not write, and they would be testing nothing.
char g_root[64];

const char* root() {
  if (g_root[0] == '\0') {
    snprintf(g_root, sizeof(g_root), "/tmp/lunok-app-tests-%d", static_cast<int>(getpid()));
  }
  return g_root;
}

bool g_booted = false;

void make_dirs(const char* path) {
  char partial[256];
  const size_t len = strlen(path);
  if (len >= sizeof(partial)) return;
  memcpy(partial, path, len + 1);
  for (size_t i = 1; i <= len; ++i) {
    if (partial[i] == '/' || partial[i] == '\0') {
      const char saved = partial[i];
      partial[i] = '\0';
      ::mkdir(partial, 0777);  // it fails when it is already there, and that is fine
      partial[i] = saved;
    }
  }
}

bool write_text(hal::IStorage* storage, const char* path, const char* text) {
  hal::FileSlot slot;
  if (!storage->open(path, hal::OpenMode::kWrite, slot)) return false;
  const bool ok = storage->write(slot, text, static_cast<uint32_t>(strlen(text))) > 0;
  storage->close(slot);
  return ok;
}

// Reads a file of the repository from the host. The tests run from the project
// root, but not every way of running them does, so the parents are tried too.
size_t read_host_file(const char* path, char* out, size_t cap) {
  static const char* const kParents[] = {"", "../", "../../", "../../../"};
  for (size_t i = 0; i < sizeof(kParents) / sizeof(kParents[0]); ++i) {
    char full[256];
    snprintf(full, sizeof(full), "%s%s", kParents[i], path);
    FILE* file = fopen(full, "rb");
    if (file == nullptr) continue;
    const size_t got = fread(out, 1, cap - 1, file);
    fclose(file);
    out[got] = '\0';
    return got;
  }
  return 0;
}

// One app on the disk: a directory with a manifest and its entry file. The entry
// file is named by the caller because a manifest that lives in the repository
// names its own, and a test that renamed it would be testing another app.
bool add_app_file(hal::IStorage* storage, const char* id, const char* manifest, const char* source,
                  const char* entry) {
  char dir[128];
  char path[160];
  snprintf(dir, sizeof(dir), "/apps/%s", id);
  storage->mkdir(dir);
  snprintf(path, sizeof(path), "/apps/%s/manifest.json", id);
  if (!write_text(storage, path, manifest)) return false;
  snprintf(path, sizeof(path), "/apps/%s/%s", id, entry);
  return write_text(storage, path, source);
}

bool add_app(hal::IStorage* storage, const char* id, const char* manifest, const char* source) {
  return add_app_file(storage, id, manifest, source, "app.lua");
}

// **A face of the repository, in the disk of this bench** (4/11): the suite keeps its own volume, so what it
// lists has to be written here — `data/res/fonts/` is the disk's `/res/fonts` and the bench does not read the
// disk of the build (docs/70, docs/85). Binary and not text: a `.ttf` is outlines with zero bytes inside.
bool copy_a_face(hal::IStorage* storage, const char* name) {
  static char bytes[65536];
  char host[128];
  snprintf(host, sizeof(host), "data/res/fonts/%s", name);
  const size_t got = read_host_file(host, bytes, sizeof(bytes));
  if (got == 0) return false;
  char path[160];
  snprintf(path, sizeof(path), "/res/fonts/%s", name);
  storage->mkdir("/res");
  storage->mkdir("/res/fonts");
  hal::FileSlot slot;
  if (!storage->open(path, hal::OpenMode::kWrite, slot)) return false;
  const bool ok = storage->write(slot, bytes, static_cast<uint32_t>(got)) == static_cast<int32_t>(got);
  storage->close(slot);
  return ok;
}

bool boot_once() {
  if (g_booted) return true;
  // The emulated disk is a directory; the storage backends expect their own root
  // to be there before they are asked for anything.
  char fs_dir[96];
  snprintf(fs_dir, sizeof(fs_dir), "%s/littlefs", root());
  make_dirs(root());
  make_dirs(fs_dir);

  // The kernel boots on a disk of its own, so nothing of the real emulator is
  // touched and the app set is the one written here.
  emu::Options opts;
  opts.fs_dir = root();
  emu::set_options(opts);

  if (!pal::init()) return false;
  kernel::boot(hal::platform());

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  if (storage == nullptr) return false;
  storage->mkdir("/apps");
  storage->mkdir("/res");
  if (!add_app(storage, "hello",
               "{\"title\":\"Hello\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
               "console.print(\"hello from an app\")\n"))
    return false;
  if (!add_app(storage, "boom",
               "{\"title\":\"Boom\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
               "error(\"boom\")\n"))
    return false;
  if (!add_app(storage, "bye",
               "{\"title\":\"Bye\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
               "console.print(\"bye\")\napp.exit(7)\n"))
    return false;
  // This one ends after waiting, so the command comes from the poll path and not
  // from the one that notices an app that never waited.
  if (!add_app(storage, "slowbye",
               "{\"title\":\"Slow bye\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
               "sys.delay(1):await()\napp.exit(5)\n"))
    return false;
  if (!add_app(storage,
               "txt",
               "{\"title\":\"Txt\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"opens\":\"txt\",\"boards\":[\"esp32\"]}",
               "if app.args then console.print(\"file \" .. app.args.path) end\n"))
    return false;
  // An app that downloads a page: the whole path, from Lua to the task of the
  // board and back, with the status and the body (docs/30).
  if (!add_app(storage, "page",
               "{\"title\":\"Page\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
               "local caps = net.caps()\n"
               "console.print(\"caps http \" .. tostring(caps.http) .. \" tls \" .. "
               "tostring(caps.tls))\n"
               "local r = http.get(\"https://example.com/hola\"):await()\n"
               "console.print(\"status \" .. r.status .. \" bytes \" .. r.bytes)\n"
               "console.print(\"body \" .. r.body)\n"))
    return false;
  if (!write_text(storage, "/res/hola.txt", "una linia\n")) return false;

  g_booted = true;
  return true;
}

void sleep_one_ms() {
  struct timespec ts;

  ts.tv_sec = 0;
  ts.tv_nsec = 1000 * 1000;
  nanosleep(&ts, nullptr);
}

// A card in the slot of the bench, or none. The mount table is built at boot, so a card
// that arrives later is the same two calls boot makes —the table and the settings door—
// and not a path of its own: on the board the card is in the slot or it is not, and the
// far side has to be told either way (docs/70, docs/85).
bool card_on(const char* dir) {
  emu::Options opts = emu::options();
  opts.sd_dir = dir;
  emu::set_options(opts);
  kernel::vfs::init(hal::platform());
  kernel::cfg::boot(hal::platform());
  return hal::platform().storage("/sd") != nullptr;
}

// The board this test is about: a board is chosen and never assumed, because the bench answers with
// the hardware of the model it was told to be, and a test that expects a gauge on an ATOM is a test
// that would pass on a board nobody has (docs/50, docs/85).
void use_board(const char* model) {
  emu::Options opts = emu::options();
  opts.board = model;
  emu::set_options(opts);
  // The board's operations (the sensor, the gauge) are installed by the boot, so a board is not
  // something a test can change under a running kernel: it boots again on the new one (docs/85).
  kernel::boot(hal::platform());
}

// The card of this run: a directory of its own, empty but for what the test writes, so a
// leftover file from the run before cannot make a test pass.
void make_card() {
  char path[96];
  snprintf(path, sizeof(path), "%s/sd", root());
  make_dirs(path);
}

// Runs one line with the console sent to a file, so what the app writes can be
// read back. through_console feeds the bytes the way a person's keyboard does
// (echo, prompt), instead of calling execute() directly. out receives the text;
// false when the app never ended.
bool feed_captured(const char* line, bool through_console, char* out, size_t cap) {
  if (out != nullptr && cap > 0) out[0] = '\0';
  if (!console_take_away()) return false;

  if (through_console) {
    char typed[160];
    snprintf(typed, sizeof(typed), "%s\r", line);
    kernel::shell::feed(typed, strlen(typed));
  } else {
    kernel::execute(line);
  }
  bool ended = true;
  for (int i = 0; i < kTurnBudget && kernel::app_running(); ++i) {
    kernel::poll();
    sleep_one_ms();
  }
  if (kernel::app_running()) ended = false;

  console_give_back(false);

  if (out != nullptr && cap > 0) {
    FILE* file = fopen(kConsolePath, "r");
    if (file != nullptr) {
      const size_t got = fread(out, 1, cap - 1, file);
      out[got] = '\0';
      fclose(file);
    }
  }
  return ended;
}

bool run_captured(const char* line, char* out, size_t cap) {
  return feed_captured(line, false, out, cap);
}

// Turns the kernel for a while with the console captured and no command behind
// it: an alarm has no line to run, and the window it opens writes to the console.
// With `line` it runs that command first, which is how a test starts an app that
// is meant to stay alive (run_captured would wait for it to end).
void pump_captured(int turns, char* out, size_t cap, const char* line = nullptr,
                   const char* typed = nullptr) {
  if (out != nullptr && cap > 0) out[0] = '\0';
  if (!console_take_away()) return;

  if (line != nullptr) kernel::execute(line);
  // Bytes as a keyboard sends them, after the command: a person pressing a key
  // while a command waits (docs/30).
  if (typed != nullptr) kernel::shell::feed(typed, strlen(typed));
  for (int i = 0; i < turns; ++i) {
    // Without an event the loop would block for its whole idle timeout on every
    // turn; a timer event makes one turn cost one millisecond.
    pal::Event tick;
    tick.type = pal::kEventTimer;
    tick.a = 0;
    tick.b = 0;
    pal::system().queue()->push(tick, 0);
    kernel::poll();
    sleep_one_ms();
  }

  console_give_back(false);

  if (out != nullptr && cap > 0) {
    FILE* file = fopen(kConsolePath, "r");
    if (file != nullptr) {
      const size_t got = fread(out, 1, cap - 1, file);
      out[got] = '\0';
      fclose(file);
    }
  }
}

// The console of a call that is not a command: a boot that brings an app back is a
// function and not a line of the shell, and what it says has to be read too.
void capture_begin() { console_take_away(); }

void capture_end(char* out, size_t cap) {
  console_give_back(false);
  if (out == nullptr || cap == 0) return;
  out[0] = '\0';
  FILE* file = fopen(kConsolePath, "r");
  if (file == nullptr) return;
  const size_t got = fread(out, 1, cap - 1, file);
  out[got] = '\0';
  fclose(file);
}

// Turns the kernel without touching the console: for a capture the caller started.
void pump_turns(int turns) {
  for (int i = 0; i < turns; ++i) {
    pal::Event tick;
    tick.type = pal::kEventTimer;
    tick.a = 0;
    tick.b = 0;
    pal::system().queue()->push(tick, 0);
    kernel::poll();
    sleep_one_ms();
  }
}

// The console of a run, including a QR when provisioning prints one: 37 modules in
// 19 lines of UTF-8 blocks is a few thousand bytes, so this is not small.
char g_out[8192];

void close_any_app() {
  if (!g_booted || !kernel::app_running()) return;
  for (int tap = 0; tap < 3 && kernel::app_running(); ++tap) {
    emu::press_button(true);
    pump_turns(10);
    emu::press_button(false);
    pump_turns(10);
  }
  // A page with controls uses the centre as Enter, so the three taps above are not a way out of it
  // (docs/30, docs/90). The test closes it the same way the shell does.
  if (kernel::app_running()) {
    kernel::shell::close_app();
    pump_turns(10);
  }
}

}  // namespace

// The plainest app there is: it starts, it writes, it ends, and the shell is back.
void test_an_app_runs_and_writes(void) {
  TEST_ASSERT_TRUE(boot_once());
  TEST_ASSERT_TRUE(run_captured("run hello", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "hello from an app"), g_out);
  TEST_ASSERT_FALSE(kernel::app_running());
}

// An app that dies says why, and the shell survives it.
void test_a_failing_app_says_why_and_the_shell_survives(void) {
  TEST_ASSERT_TRUE(boot_once());
  TEST_ASSERT_TRUE(run_captured("run boom", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "boom"), g_out);
  TEST_ASSERT_FALSE(kernel::app_running());
}

// app.exit(code) is not a death: the shell says the code, also when the app ends
// after waiting for a promise (the other place the shell notices it is over).
void test_the_exit_code_reaches_the_shell(void) {
  TEST_ASSERT_TRUE(boot_once());
  TEST_ASSERT_TRUE(run_captured("run bye", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "bye"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "app exit 7"), g_out);

  TEST_ASSERT_TRUE(run_captured("run slowbye", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "app exit 5"), g_out);
}

// open hands the file to the app that declares the extension. **The disk of this build has three apps that open
// `.txt`** —`edit`, `wc` and `txt`—, so the test names the one it wants: `open <path>` alone asks which one, and the
// question would eat the next line of the test as its answer (docs/40, docs/30).
void test_open_hands_the_file_to_the_app(void) {
  TEST_ASSERT_TRUE(boot_once());
  TEST_ASSERT_TRUE(run_captured("open txt res/hola.txt", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL(strstr(g_out, "file res/hola.txt"));
}

// A file nobody handles is refused, and nothing is started.
void test_a_file_without_a_handler_is_refused(void) {
  TEST_ASSERT_TRUE(boot_once());
  TEST_ASSERT_TRUE(run_captured("open apps/hello/app.lua", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL(strstr(g_out, "no app opens .lua"));
}

// With two handlers the shell asks every time: an answer kept from last time
// hides the other app and leaves no way to reach it. Naming the app is the way.
void test_two_handlers_ask_every_time(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "txt2",
                            "{\"title\":\"Txt two\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"opens\":\"txt\",\"boards\":[\"esp32\"]}",
                            "if app.args then console.print(\"second handler, file \"\n"
                            "  .. app.args.path) end\n"));

  // It asks, and nothing starts on its own.
  TEST_ASSERT_TRUE(run_captured("open res/hola.txt", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "more than one app opens .txt"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "which one?"), g_out);
  TEST_ASSERT_FALSE(kernel::app_running());

  // The number is the answer and starts the chosen app.
  TEST_ASSERT_TRUE(run_captured("1", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "file res/hola.txt"), g_out);

  // The next time it asks again: no answer is carried over.
  TEST_ASSERT_TRUE(run_captured("open res/hola.txt", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "which one?"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "as you chose before"), g_out);

  // Answering runs it again; the two handlers both print the file they get.
  TEST_ASSERT_TRUE(run_captured("1", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "file res/hola.txt"), g_out);

  // The other handler is reached by naming it, and that never asks.
  TEST_ASSERT_TRUE(run_captured("open txt2 res/hola.txt", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "second handler, file res/hola.txt"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "which one?"), g_out);

  storage->remove("/apps/txt2/app.lua");
  storage->remove("/apps/txt2/manifest.json");
  storage->remove("/apps/txt2");
}

// The question is the only prompt: typing the answer has to happen right after
// "which one? (1-2) ", not after a shell prompt printed in between. It brings
// its own extension so the apps of other tests do not take part in the question.
void test_the_question_is_the_only_prompt(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  const char kManifest[] =
      "{\"title\":\"Lunok handler\",\"version\":\"0.1.0\",\"entry\":\"app.lua\","
      "\"opens\":\"lunok\",\"boards\":[\"esp32\"]}";
  TEST_ASSERT_TRUE(add_app(storage, "luna", kManifest, "console.print(\"luna\")\n"));
  TEST_ASSERT_TRUE(add_app(storage, "lunb", kManifest, "console.print(\"lunb\")\n"));
  TEST_ASSERT_TRUE(write_text(storage, "/res/x.lunok", "x\n"));

  TEST_ASSERT_TRUE(feed_captured("open res/x.lunok", true, g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "which one?"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "lunok>"), g_out);

  // Cancelling leaves the shell at the prompt again, with nothing running.
  TEST_ASSERT_TRUE(feed_captured("x", true, g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "cancelled"), g_out);
  TEST_ASSERT_FALSE(kernel::app_running());

  storage->remove("/apps/luna/app.lua");
  storage->remove("/apps/luna/manifest.json");
  storage->remove("/apps/luna");
  storage->remove("/apps/lunb/app.lua");
  storage->remove("/apps/lunb/manifest.json");
  storage->remove("/apps/lunb");
  storage->remove("/res/x.lunok");
}

// A long path is not cut on the way to the app. The shell hands over a path of
// 109 bytes, the question keeps it whole, and the app is given all of it: a cut
// path would start the app with a file nobody asked for.
void test_a_long_path_reaches_the_app(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "txtl",
                            "{\"title\":\"Txt long\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"opens\":\"txt\",\"boards\":[\"esp32\"]}",
                            "if app.args then console.print(\"long handler, file \"\n"
                            "  .. app.args.path) end\n"));

  char name[120];
  char path[160];
  char line[200];
  memset(name, 'a', 100);
  name[100] = '\0';
  snprintf(path, sizeof(path), "/res/%s.txt", name);
  // Longer than the 96 bytes the selector kept, and short enough for the file
  // system and for a shell line.
  TEST_ASSERT_TRUE(strlen(path) > 96);
  TEST_ASSERT_TRUE(strlen(path) < 128);
  TEST_ASSERT_TRUE(write_text(storage, path, "x\n"));

  snprintf(line, sizeof(line), "open %s", path);
  TEST_ASSERT_TRUE(run_captured(line, g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "which one?"), g_out);

  TEST_ASSERT_TRUE(run_captured("1", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, path), g_out);

  storage->remove(path);
  storage->remove("/apps/txtl/app.lua");
  storage->remove("/apps/txtl/manifest.json");
  storage->remove("/apps/txtl");
}

// An alarm is delivered to the app that owns it, between two turns of the app,
// with its payload and how much it was missed by (docs/60).
void test_an_alarm_reaches_the_app_that_owns_it(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "clocked",
                            "{\"title\":\"Clocked\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                            "function on_alarm(id, payload, meta)\n"
                            "  console.print('alarm ' .. payload .. ' missed ' .. meta.missed)\n"
                            "end\n"
                            "alarm.after(120, 'ring')\n"
                            "sys.delay(400):await()\n"
                            "console.print('done')\n"));

  TEST_ASSERT_TRUE(run_captured("run clocked", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "alarm ring missed 0"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "done"), g_out);
  // It arrives while the app waits for its promise, not after it ends.
  TEST_ASSERT_TRUE(strstr(g_out, "alarm ring") < strstr(g_out, "done"));

  storage->remove("/apps/clocked/app.lua");
  storage->remove("/apps/clocked/manifest.json");
  storage->remove("/apps/clocked");
}

// A cancelled alarm never fires, and list() says what is armed and what is not.
void test_a_cancelled_alarm_never_fires(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "cancel",
                            "{\"title\":\"Cancel\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                            "function on_alarm(id, payload)\n"
                            "  console.print('fired ' .. payload)\n"
                            "end\n"
                            "local id = alarm.after(120, 'ring')\n"
                            "console.print('armed ' .. #alarm.list())\n"
                            "console.print('cancel ' .. tostring(alarm.cancel(id)))\n"
                            "console.print('left ' .. #alarm.list())\n"
                            "sys.delay(400):await()\n"
                            "console.print('quiet')\n"));

  TEST_ASSERT_TRUE(run_captured("run cancel", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "armed 1"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "cancel true"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "left 0"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "quiet"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "fired"), g_out);

  storage->remove("/apps/cancel/app.lua");
  storage->remove("/apps/cancel/manifest.json");
  storage->remove("/apps/cancel");
}

// caps() says how many fit, and a payload that cannot be kept is refused with a
// code an app can test, not with silence.
void test_alarm_caps_and_a_payload_that_cannot_be_kept(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "caps",
                            "{\"title\":\"Caps\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                            "console.print('max ' .. alarm.caps().max)\n"
                            "local ok, err = pcall(function() alarm.after(120, {}) end)\n"
                            "console.print('table ' .. tostring(ok))\n"
                            "console.print('code ' .. tostring(err.code))\n"));

  TEST_ASSERT_TRUE(run_captured("run caps", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "max 8"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "table false"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "code E_INVAL"), g_out);

  storage->remove("/apps/caps/app.lua");
  storage->remove("/apps/caps/manifest.json");
  storage->remove("/apps/caps");
}

// An alarm for an app that is not loaded runs in a window: the app is loaded,
// its on_alarm is called, and the VM goes away afterwards (docs/60).
void test_an_alarm_for_an_app_that_is_not_loaded_runs_in_a_window(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "inback",
                            "{\"title\":\"In back\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                            "function on_alarm(id, payload, meta)\n"
                            "  console.print('window ' .. payload .. ' fg '\n"
                            "    .. tostring(console.is_foreground()))\n"
                            "end\n"
                            "console.print('loaded fg ' .. tostring(console.is_foreground()))\n"
                            "sys.delay(5000):await()\n"));

  hal::IClock* clock = hal::platform().clock();
  TEST_ASSERT_NOT_NULL(clock);
  alarms::Spec spec;
  spec.app = "inback";
  spec.on_mono_ms = clock->monotonic_ms() + 120;
  spec.payload_kind = alarms::Payload::kText;
  spec.payload = "ring";
  TEST_ASSERT_TRUE(alarms::add(spec) != 0);

  pump_captured(600, g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "loaded fg false"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "window ring fg false"), g_out);
  TEST_ASSERT_FALSE(kernel::app_running());

  storage->remove("/apps/inback/app.lua");
  storage->remove("/apps/inback/manifest.json");
  storage->remove("/apps/inback");
}

// raise: an alarm for an app that is not loaded puts that app in front, and the
// console is its own from then on (docs/60).
void test_an_alarm_with_raise_puts_the_app_in_front(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "front",
                            "{\"title\":\"Front\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                            "console.print('front here fg ' .. tostring(console.is_foreground()))\n"
                            "sys.delay(5000):await()\n"));

  hal::IClock* clock = hal::platform().clock();
  TEST_ASSERT_NOT_NULL(clock);
  alarms::Spec spec;
  spec.app = "front";
  spec.on_mono_ms = clock->monotonic_ms() + 120;
  spec.raise_to_front = true;
  TEST_ASSERT_TRUE(alarms::add(spec) != 0);

  pump_captured(600, g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "front here fg true"), g_out);
  TEST_ASSERT_TRUE(kernel::app_running());
  TEST_ASSERT_EQUAL_STRING("front", kernel::shell::app_id());

  // Leave the shell with no app, as every other test expects to find it.
  kernel::shell::close_app();
  TEST_ASSERT_FALSE(kernel::app_running());

  storage->remove("/apps/front/app.lua");
  storage->remove("/apps/front/manifest.json");
  storage->remove("/apps/front");
}

// The key-value store keeps bytes and nothing else: what is written comes back,
// a key that is not there says no, and erasing it makes it disappear.
void test_the_key_value_store_keeps_bytes(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IKeyValue* nvs = hal::platform().nvs();
  TEST_ASSERT_NOT_NULL(nvs);

  const char kValue[] = "hello nvs";
  TEST_ASSERT_TRUE(nvs->set("test", "greeting", kValue, sizeof(kValue)));

  char buf[32];
  size_t len = 0;
  TEST_ASSERT_TRUE(nvs->get("test", "greeting", buf, sizeof(buf), len));
  TEST_ASSERT_EQUAL_UINT32(sizeof(kValue), len);
  TEST_ASSERT_EQUAL_STRING(kValue, buf);

  // A buffer with no room reads nothing and says so, which is what the one-number
  // version of this call could not do: a caller that passed its length as zero
  // got a quiet "no" and was never told why (measured on the board, 31/10).
  size_t no_room = 0;
  TEST_ASSERT_FALSE(nvs->get("test", "greeting", buf, 0, no_room));

  size_t missing = 0;
  TEST_ASSERT_FALSE(nvs->get("test", "nope", buf, sizeof(buf), missing));
  // Erasing what is not there is false, and it is not an error (the board logged
  // one before this: docs/70).
  TEST_ASSERT_FALSE(nvs->erase("test", "never-written"));

  TEST_ASSERT_TRUE(nvs->erase("test", "greeting"));
  size_t after = 0;
  TEST_ASSERT_FALSE(nvs->get("test", "greeting", buf, sizeof(buf), after));
}

// An alarm with `persist` survives the power going away: it is written to the
// NVS, and loading it back after a reboot puts it in the queue with its epoch, so
// an alarm that should have fired while the board was off arrives late (docs/60).
void test_a_persistent_alarm_comes_back_after_a_reboot(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "late",
                            "{\"title\":\"Late\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                            "function on_alarm(id, payload, meta)\n"
                            "  console.print('late ' .. payload .. ' '\n"
                            "    .. tostring(meta.late_ms >= 100))\n"
                            "end\n"
                            "if not console.is_foreground() then return end\n"));

  hal::IClock* clock = hal::platform().clock();
  TEST_ASSERT_NOT_NULL(clock);
  const int64_t epoch = 1767225600000LL;  // 2026-01-01 00:00:00 UTC
  TEST_ASSERT_TRUE(clock->set_epoch_ms(epoch));

  alarms::Spec spec;
  spec.app = "late";
  spec.on_mono_ms = clock->monotonic_ms() + 200;
  spec.persist = true;
  spec.payload_kind = alarms::Payload::kText;
  spec.payload = "kept";
  TEST_ASSERT_TRUE(alarms::add(spec) != 0);

  // Two turns with an event each: the kernel notices the queue changed and
  // writes it, and the alarm is not due yet.
  pump_captured(2, nullptr, 0);

  // The power goes away: the queue is empty, and only the NVS knows.
  alarms::reset();
  TEST_ASSERT_EQUAL_UINT32(0, alarms::count());

  // Five seconds pass with the board off: the alarm is now late, not lost.
  TEST_ASSERT_TRUE(clock->set_epoch_ms(epoch + 5000));
  kernel::alarms_restore();
  TEST_ASSERT_EQUAL_UINT32(1, alarms::count());

  pump_captured(400, g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "late kept true"), g_out);
  TEST_ASSERT_EQUAL_UINT32(0, alarms::count());  // one shot: it is gone after firing

  storage->remove("/apps/late/app.lua");
  storage->remove("/apps/late/manifest.json");
  storage->remove("/apps/late");
}

// raise closes the app that was in front, and that app gets its goodbye hooks in
// the order docs/40 says: on_pause, on_save, on_close.
void test_raise_calls_the_goodbye_hooks_of_the_app_in_front(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "polite",
                            "{\"title\":\"Polite\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                            "function on_pause() console.print('bye pause') end\n"
                            "function on_save() console.print('bye save') end\n"
                            "function on_close() console.print('bye close') end\n"
                            "console.print('polite here')\n"
                            "sys.delay(5000):await()\n"));
  TEST_ASSERT_TRUE(add_app(storage, "winner",
                            "{\"title\":\"Winner\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                            "console.print('winner here')\n"
                            "sys.delay(5000):await()\n"));

  // The polite app takes the front and stays there, waiting.
  pump_captured(60, g_out, sizeof(g_out), "run polite");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "polite here"), g_out);
  TEST_ASSERT_TRUE(kernel::app_running());

  hal::IClock* clock = hal::platform().clock();
  TEST_ASSERT_NOT_NULL(clock);
  alarms::Spec spec;
  spec.app = "winner";
  spec.on_mono_ms = clock->monotonic_ms() + 120;
  spec.raise_to_front = true;
  TEST_ASSERT_TRUE(alarms::add(spec) != 0);

  pump_captured(600, g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "bye pause"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "bye save"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "bye close"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "winner here"), g_out);
  // In that order, and all of them before the app that took the front.
  TEST_ASSERT_TRUE(strstr(g_out, "bye pause") < strstr(g_out, "bye save"));
  TEST_ASSERT_TRUE(strstr(g_out, "bye save") < strstr(g_out, "bye close"));
  TEST_ASSERT_TRUE(strstr(g_out, "bye close") < strstr(g_out, "winner here"));
  TEST_ASSERT_EQUAL_STRING("winner", kernel::shell::app_id());

  kernel::shell::close_app();
  storage->remove("/apps/polite/app.lua");
  storage->remove("/apps/polite/manifest.json");
  storage->remove("/apps/polite");
  storage->remove("/apps/winner/app.lua");
  storage->remove("/apps/winner/manifest.json");
  storage->remove("/apps/winner");
}

// /nvs is the key-value store as files: a namespace is a directory and a key is
// a file whose contents are the value (docs/70). This is what makes `ls /nvs`
// and `cat /nvs/alarms/queue` possible from the console.
void test_the_nvs_mount_shows_the_keys_as_files(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IKeyValue* nvs = hal::platform().nvs();
  TEST_ASSERT_NOT_NULL(nvs);
  TEST_ASSERT_TRUE(nvs->set("unit", "hello", "hi there", sizeof("hi there")));

  hal::Stat info;
  TEST_ASSERT_TRUE(kernel::vfs::stat("/nvs", info));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(hal::NodeType::kDir), static_cast<int>(info.type));
  TEST_ASSERT_TRUE(kernel::vfs::stat("/nvs/unit", info));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(hal::NodeType::kDir), static_cast<int>(info.type));
  TEST_ASSERT_TRUE(kernel::vfs::stat("/nvs/unit/hello", info));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(sizeof("hi there")), static_cast<int>(info.size));

  char buf[16];
  size_t len = 0;
  TEST_ASSERT_TRUE(kernel::vfs::read_file("/nvs/unit/hello", buf, sizeof(buf), len));
  TEST_ASSERT_EQUAL_UINT32(sizeof("hi there"), len);
  TEST_ASSERT_EQUAL_STRING("hi there", buf);

  // And from the shell, which is how a person looks at it.
  TEST_ASSERT_TRUE(run_captured("ls /nvs", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "unit"), g_out);
  TEST_ASSERT_TRUE(run_captured("cat /nvs/unit/hello", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "hi there"), g_out);

  // The store has two levels and no more, so a deeper path is not a file.
  TEST_ASSERT_FALSE(kernel::vfs::stat("/nvs/unit/hello/deeper", info));

  nvs->erase("unit", "hello");
}

// fs.* works from inside an app, on the real VFS: the kernel installed the operations at
// boot, and an app never learns what a VFS is (docs/70). And `/res` is **its own**: what an
// app ships with lives in `apps/<id>/res/`, so the `res/` of the disk —the samples of the
// person— is not what it reads, and it cannot write either one (docs/70).
void test_an_app_reads_its_own_resources_with_fs(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "reader",
                            "{\"title\":\"Reader\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                            "local s = fs.read('res/hola.txt')\n"
                            "local st = fs.stat('res/hola.txt')\n"
                            "console.print('read ' .. tostring(s) .. ' size ' .. tostring(st.size))\n"
                            "console.print('first ' .. fs.read_at('res/hola.txt', 0, 3))\n"
                            "local entries = fs.list('res')\n"
                            "console.print('entries ' .. #entries)\n"
                            "local shared, err = fs.read('/littlefs/res/hola.txt')\n"
                            "console.print('shared ' .. tostring(shared) .. ' ' .. tostring(err and err.code))\n"
                            "local w, err2 = fs.write('res/x.txt', 'no')\n"
                            "console.print('write ' .. tostring(w) .. ' ' .. tostring(err2 and err2.code))\n"));
  // The app's own file, with a name that also exists on the disk: the boot of these tests
  // writes `res/hola.txt` (`una linia`, 10 bytes) and this one is 8, so the numbers below
  // say which of the two was read. The directory is made here because the storage is the
  // raw backend: the one that makes the way is `fs.write`, and that is the app's side.
  TEST_ASSERT_TRUE(storage->mkdir("apps/reader/res"));
  TEST_ASSERT_TRUE(write_text(storage, "apps/reader/res/hola.txt", "de dins\n"));

  TEST_ASSERT_TRUE(run_captured("run reader", g_out, sizeof(g_out)));
  // The file ends with a newline, so the size is what proves it read 8 bytes and not the
  // 10 of the one on the disk with the same name.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "read de dins"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "size 8"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "first de "), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "entries 1"), g_out);
  // The disk of the system is not an app's, not even to read the samples on it, and its
  // own resources are what it ships with: not a place to write.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "shared nil E_INVAL"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "write nil E_INVAL"), g_out);

  storage->remove("apps/reader/res/hola.txt");
  storage->remove("apps/reader/res");
  storage->remove("/apps/reader/app.lua");
  storage->remove("/apps/reader/manifest.json");
  storage->remove("/apps/reader");
}

// Writing: an app writes inside its own ground, atomically, and the rules of
// docs/70 hold: nothing outside /data/<id>/, no leftovers, **no quota** (the disk is the
// limit) and the drives of the system are not an app's.
void test_an_app_writes_inside_its_own_ground(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "writer",
                            "{\"title\":\"Writer\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                            "local ground = fs.data()\n"
                            "console.print('ground ' .. ground)\n"
                            "fs.mkdir(ground)\n"
                            "local path = fs.data('t.txt')\n"
                            "local n = fs.write(path, 'hola mon')\n"
                            "console.print('wrote ' .. n .. ' back ' .. fs.read(path))\n"
                            "console.print('entries ' .. #fs.list(ground))\n"
                            "local ok, err = fs.write('/data/other/x.txt', 'no')\n"
                            "console.print('outside ' .. tostring(ok) .. ' ' .. tostring(err and err.code))\n"
                            "local other, err2 = fs.read('/data/other/x.txt')\n"
                            "console.print('other ' .. tostring(other) .. ' ' .. tostring(err2 and err2.code))\n"
                            "for i = 1, 6 do\n"
                            "  assert(fs.write(fs.data('c' .. i .. '.bin'), string.rep('a', 15000)))\n"
                            "end\n"
                            "local info = fs.stat(fs.data('c6.bin'))\n"
                            "console.print('big ' .. tostring(info and info.size))\n"
                            "local sys, err4 = fs.read('/littlefs/apps/writer/manifest.json')\n"
                            "console.print('system ' .. tostring(sys) .. ' ' .. tostring(err4 and err4.code))\n"
                            "local okc = fs.write('/cfg/s.json', 'x')\n"
                            "console.print('cfg ' .. tostring(okc) .. ' ' .. fs.read('/cfg/s.json'))\n"));

  TEST_ASSERT_TRUE(run_captured("run writer", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "ground /data/writer"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "wrote 8 back hola mon"), g_out);
  // One entry: the file, and no .tmp left behind.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "entries 1"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "outside nil E_INVAL"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "other nil E_INVAL"), g_out);
  // 90 000 B: over the 64 KB that used to be the limit, and it goes through because the
  // limit is the disk (docs/70).
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "big 15000"), g_out);
  // And the drives of the system are not an app's, neither to read nor to list.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "system nil E_INVAL"), g_out);
  // `/cfg` is not one of them: it is the door to its own settings, and fs.write answers
  // the bytes written like every other write (docs/70).
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "cfg 1 x"), g_out);

  // And the ground is where the system said: on the disk, under data/writer.
  hal::Stat info;
  TEST_ASSERT_TRUE(storage->stat("data/writer/t.txt", info));
  TEST_ASSERT_EQUAL_UINT32(8, info.size);

  storage->remove("cfg/writer/s.json");
  storage->remove("cfg/writer");
  storage->remove("data/writer/t.txt");
  for (int i = 1; i <= 6; ++i) {
    char path[64];
    snprintf(path, sizeof(path), "data/writer/c%d.bin", i);
    storage->remove(path);
  }
  storage->remove("data/writer");
  storage->remove("/apps/writer/app.lua");
  storage->remove("/apps/writer/manifest.json");
  storage->remove("/apps/writer");
}

// **`fs.write` sobre una memòria crua va al lloc i no a un temporal** (0.65.1): `/rtc` és *un* fitxer i no
// té `/rtc.tmp` on escriure per després reanomenar, o sigui que el camí atòmic responia `E_NOENT` —el `no
// such file or directory` que veia `run hexedit /fram` en desar (4/11). El que s'escriu és el que es
// llegeix, la resta de la memòria queda a zero i el que no hi cap sencer és `E_NOSPC` sense tocar res
// (docs/50, docs/70).
void test_a_write_to_raw_memory_goes_in_place(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "raw",
                            "{\"title\":\"Raw\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                            "local n, err = fs.write('/rtc', 'hola')\n"
                            "console.print('wrote ' .. tostring(n) .. ' ' .. tostring(err and err.code))\n"
                            "local back = fs.read('/rtc')\n"
                            "console.print('back ' .. #back .. ' ' .. back:sub(1, 4))\n"
                            "console.print('tail zero ' .. tostring(back:sub(5, 9) == string.rep('\\0', 5)))\n"
                            "local big, why = fs.write('/rtc', string.rep('x', 8000))\n"
                            "console.print('big ' .. tostring(big) .. ' ' .. tostring(why and why.code))\n"
                            "console.print('still ' .. fs.read('/rtc'):sub(1, 4))\n"));

  TEST_ASSERT_TRUE(run_captured("run raw", g_out, sizeof(g_out)));
  // `/rtc` is 7 168 B, the memory is written from its start and its tail is zeroed.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "wrote 4 nil"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "back 7168 hola"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "tail zero true"), g_out);
  // **Una escriptura que no hi cap no hi deixa res a mitges**: 8 000 B en una memòria de 7 168 és un no.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "big nil E_NOSPC"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "still hola"), g_out);

  storage->remove("/apps/raw/app.lua");
  storage->remove("/apps/raw/manifest.json");
  storage->remove("/apps/raw");
}

// `/cfg` is a door and not a disk: an app writes `/cfg/s.json` and the system puts it in
// `cfg/<id>/s.json` on the unit the person chose. These tests have no card, so the unit is
// the flash, and the app is not told —it asks with fs.cfg() when it wants to know— and the
// door does not open onto anybody else's room (docs/70).
void test_the_settings_of_an_app_go_behind_the_door_of_cfg(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "keeper",
                           "{\"title\":\"Keeper\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local c = fs.cfg()\n"
                           "console.print('unit ' .. c.unit .. ' dir ' .. c.dir)\n"
                           "console.print('write ' .. tostring(fs.write('/cfg/s.json', 'set')))\n"
                           "console.print('back ' .. tostring(fs.read('/cfg/s.json')))\n"
                           "local moved, err = fs.cfg('sd')\n"
                           "console.print('card ' .. tostring(moved) .. ' ' .. tostring(err and err.code))\n"
                           "local escape, err2 = fs.write('/cfg/../data/other/x', 'no')\n"
                           "console.print('escape ' .. tostring(escape) .. ' ' .. tostring(err2 and err2.code))\n"
                           "local same = fs.cfg('littlefs')\n"
                           "console.print('flash ' .. tostring(same and same.unit))\n"));

  TEST_ASSERT_TRUE(run_captured("run keeper", g_out, sizeof(g_out)));
  // The app learns where its settings are, but it never needed to: the write below went
  // through `/cfg` and only the system knows which unit answered.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "unit littlefs dir /littlefs/cfg/keeper"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "write 3"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "back set"), g_out);
  // No card, so asking for one is refused instead of silently saving to the flash.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "card nil E_UNAVAIL"), g_out);
  // And the door is not a way out: ".." is refused the same on both sides.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "escape nil E_INVAL"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "flash littlefs"), g_out);

  // On the disk it is cfg/<id>/, not data/<id>/.
  hal::Stat info;
  TEST_ASSERT_TRUE(storage->stat("cfg/keeper/s.json", info));
  TEST_ASSERT_EQUAL_UINT32(3, info.size);
  hal::Stat missed;
  TEST_ASSERT_FALSE(storage->stat("data/keeper/s.json", missed));

  // The app moved the preference to the flash and it stays moved, because it is the
  // person's: the test puts it back where it found it before it leaves.
  TEST_ASSERT_TRUE(kernel::cfg::set_unit("auto"));

  storage->remove("cfg/keeper/s.json");
  storage->remove("cfg/keeper");
  storage->remove("cfg");
  storage->remove("/apps/keeper/app.lua");
  storage->remove("/apps/keeper/manifest.json");
  storage->remove("/apps/keeper");
}

// The card is a volume of its own: `/sd` is there because a card is in the slot, an app
// may read it and may not write it, and what the test put on the card is what the app
// reads — the whole path, from FATFS on the board and from a directory on the PC, which
// is the point of having a card on the bench (docs/70, docs/85).
void test_a_card_in_the_slot_is_at_sd_and_an_app_may_only_read_it(void) {
  TEST_ASSERT_TRUE(boot_once());
  // The slot is a thing the model has: the Paper has one and the ATOMs do not, so a card is a card
  // on the board that can hold it (docs/50, docs/85).
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  make_card();
  char card_path[96];
  snprintf(card_path, sizeof(card_path), "%s/sd", root());
  TEST_ASSERT_TRUE(card_on(card_path));

  hal::IStorage* card = hal::platform().storage("/sd");
  TEST_ASSERT_NOT_NULL(card);
  TEST_ASSERT_TRUE(write_text(card, "/leeme.txt", "de la targeta"));

  // It is a mount the person can see and walk.
  pump_captured(20, g_out, sizeof(g_out), "mounts");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "/sd"), g_out);
  pump_captured(20, g_out, sizeof(g_out), "ls sd");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "leeme.txt"), g_out);

  TEST_ASSERT_TRUE(add_app(storage, "sdreader",
                           "{\"title\":\"Reader\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "console.print('read ' .. tostring(fs.read('/sd/leeme.txt')))\n"
                           "local ok, err = fs.write('/sd/x.txt', 'no')\n"
                           "console.print('write ' .. tostring(ok) .. ' ' .. tostring(err and err.code))\n"));

  TEST_ASSERT_TRUE(run_captured("run sdreader", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "read de la targeta"), g_out);
  // A card is the person's: an app reads it and does not write it (docs/70).
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "write nil E_INVAL"), g_out);
  hal::Stat info;
  TEST_ASSERT_FALSE_MESSAGE(card->stat("x.txt", info), "an app wrote on the card");

  card->remove("/leeme.txt");
  storage->remove("/apps/sdreader/app.lua");
  storage->remove("/apps/sdreader/manifest.json");
  storage->remove("/apps/sdreader");
}

// **The faces of the disk are one area for every app** (3/11): `/fonts` is the disk's `res/fonts/`
// seen from the root, so an app reads a face without carrying a copy of it, and what it finds there
// is not only a file but a face the reader can wear — the two halves of "a font works" (docs/30,
// docs/70).
void test_the_faces_of_the_disk_are_at_fonts_for_every_app(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);

  // **Les cares del repositori, en aquest disc**: el banc no llegeix el disc del build, o sigui que les
  // dues que aquesta prova nomena s'escriuen aquí (docs/70, docs/85).
  TEST_ASSERT_TRUE(copy_a_face(storage, "dejavusans-bold.ttf"));
  TEST_ASSERT_TRUE(copy_a_face(storage, "LICENCE.txt"));

  // The prompt sees the mount and walks it, and the licence of the faces travels with them.
  pump_captured(20, g_out, sizeof(g_out), "mounts");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "/fonts"), g_out);
  pump_captured(20, g_out, sizeof(g_out), "ls fonts");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "dejavusans-bold.ttf"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "LICENCE.txt"), g_out);

  TEST_ASSERT_TRUE(add_app(storage, "facedoor",
                           "{\"title\":\"Facedoor\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local licence, lerr = fs.read('/fonts/LICENCE.txt', 64)\n"
                           "console.print('read ' .. tostring(licence and #licence) .. ' '\n"
                           "  .. tostring(lerr and lerr.code))\n"
                           "local face_bytes = fs.read('/fonts/dejavusans-bold.ttf', 4096)\n"
                           "console.print('face ' .. tostring(face_bytes and #face_bytes))\n"
                           "local ok, werr = fs.write('/fonts/x.txt', 'no')\n"
                           "console.print('write ' .. tostring(ok) .. ' ' .. tostring(werr and "
                           "werr.code))\n"));

  TEST_ASSERT_TRUE(run_captured("run facedoor", g_out, sizeof(g_out)));
  // The file is reachable: sixty-four bytes of the licence come back and no error with them.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "read 64 nil"), g_out);
  // **La cara també arriba sencera** (4096 B del `.ttf` que hi ha darrere de la porta). Que el lector la
  // converteixi en una cara que es pot portar és una altra prova i un altre banc: `ui.load_font` necessita
  // el vidre de l'app —l'`ui::boot` que instal·la les operacions— i aquest banc és un ATOM sense pantalla,
  // on `E_UNSUPPORTED` és la veritat de la placa i no una fallada. El lector de `.ttf` té la seva suite
  // (`test_ttf`, amb dos fitxers de debò) i `test_font` prova les cares de `/res` (docs/30, docs/50).
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "face 4096"), g_out);
  // A face of the disk is the person's: an app reads it and does not write it (docs/70).
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "write nil E_INVAL"), g_out);
  hal::Stat info;
  TEST_ASSERT_FALSE_MESSAGE(storage->stat("res/fonts/x.txt", info), "an app wrote in /fonts");

  storage->remove("/res/fonts/dejavusans-bold.ttf");
  storage->remove("/res/fonts/LICENCE.txt");
  storage->remove("/res/fonts");
  storage->remove("/res");
  storage->remove("/apps/facedoor/app.lua");
  storage->remove("/apps/facedoor/manifest.json");
  storage->remove("/apps/facedoor");
}

// The settings follow the card, and that is the reason the card comes first: a card can be
// taken out and put in another board, and its `cfg/` goes with it (docs/70).
void test_the_settings_of_an_app_go_to_the_card_when_there_is_one(void) {
  TEST_ASSERT_TRUE(boot_once());
  // The settings go to the card of a board that has a slot to put one in (docs/50, docs/85).
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  make_card();
  char card_path[96];
  snprintf(card_path, sizeof(card_path), "%s/sd", root());
  TEST_ASSERT_TRUE(card_on(card_path));
  hal::IStorage* card = hal::platform().storage("/sd");
  TEST_ASSERT_NOT_NULL(card);

  // The prompt says which unit it is, and it says the card because the card answers.
  pump_captured(20, g_out, sizeof(g_out), "cfg");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "settings: sd (asked for auto)"), g_out);

  TEST_ASSERT_TRUE(add_app(storage, "cardcfg",
                           "{\"title\":\"Card cfg\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local c = fs.cfg()\n"
                           "console.print('unit ' .. c.unit .. ' dir ' .. c.dir)\n"
                           "console.print('write ' .. tostring(fs.write('/cfg/s.json', 'set')))\n"));

  TEST_ASSERT_TRUE(run_captured("run cardcfg", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "unit sd dir /sd/cfg/cardcfg"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "write 3"), g_out);

  hal::Stat info;
  TEST_ASSERT_TRUE_MESSAGE(card->stat("cfg/cardcfg/s.json", info),
                           "the settings are not on the card");
  hal::Stat missed;
  TEST_ASSERT_FALSE_MESSAGE(storage->stat("cfg/cardcfg/s.json", missed),
                            "the settings went to the flash as well");

  card->remove("cfg/cardcfg/s.json");
  card->remove("cfg/cardcfg");
  card->remove("cfg");
  storage->remove("/apps/cardcfg/app.lua");
  storage->remove("/apps/cardcfg/manifest.json");
  storage->remove("/apps/cardcfg");
}


// The battery is a measurement and a curve, and the curve is the part that can be wrong
// without any hardware: a single cell lives most of its charge in its last volt, so a
// straight line from 3,3 to 4,2 would say 45 % at 3,7 V (docs/50).
void test_the_battery_curve_falls_and_is_clamped(void) {
  TEST_ASSERT_EQUAL_UINT32(100, hal::battery_percent(4200));
  TEST_ASSERT_EQUAL_UINT32(100, hal::battery_percent(4350));  // a charger reads over full
  TEST_ASSERT_EQUAL_UINT32(0, hal::battery_percent(3300));
  TEST_ASSERT_EQUAL_UINT32(0, hal::battery_percent(3100));
  TEST_ASSERT_EQUAL_UINT32(70, hal::battery_percent(3900));
  TEST_ASSERT_EQUAL_UINT32(42, hal::battery_percent(3700));
  // And it never goes up while the voltage goes down, which is the one thing an app
  // showing a battery icon needs from it.
  uint32_t previous = 101;
  for (int mv = 4300; mv >= 3100; mv -= 10) {
    const uint32_t pct = hal::battery_percent(mv);
    TEST_ASSERT_TRUE_MESSAGE(pct <= previous, "the curve goes up when the cell goes down");
    TEST_ASSERT_TRUE_MESSAGE(pct <= 100, "a percentage over a hundred");
    previous = pct;
  }
}

// The battery of the bench is a number the person sets, and with no number the whole
// answer is nil, which is not zero volts (docs/30, docs/85).
void test_the_battery_reaches_the_app_as_a_measurement(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "gauge",
                           "{\"title\":\"Gauge\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local b = power.battery()\n"
                           "if b then console.print('battery ' .. b.mv .. ' ' .. b.pct)\n"
                           "else console.print('battery nil') end\n"));

  // A board that cannot say: nil. The gauge is the Paper's, so this is one (docs/50).
  use_board("m5stack-paper");
  emu::Options opts = emu::options();
  opts.battery_mv = 0;
  emu::set_options(opts);
  TEST_ASSERT_TRUE(run_captured("run gauge", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "battery nil"), g_out);

  // And a cell at 3,9 V, which the curve says is 70 %.
  opts.battery_mv = 3900;
  emu::set_options(opts);
  TEST_ASSERT_TRUE(run_captured("run gauge", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "battery 3900 70"), g_out);

  storage->remove("/apps/gauge/app.lua");
  storage->remove("/apps/gauge/manifest.json");
  storage->remove("/apps/gauge");
}

// The extra memory of a board is there for **any** app and it is transparent: the app asks for
// memory, the interpreter puts the big blocks in the PSRAM and the small ones in the board's own RAM,
// and the same app fits on the board that has it and does not on the one that does not. The numbers
// are the board's and the app can ask for them (docs/75, docs/50).
void test_the_extra_memory_is_there_for_any_app(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);

  // The app: says what the board has, and then builds a table of about a megabyte.
  TEST_ASSERT_TRUE(add_app(storage, "big",
                           "{\"title\":\"Big\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local h = sys.heap()\n"
                           "console.print('limit ' .. h.limit .. ' extra ' .. h.extra_total)\n"
                           "local t = {}\n"
                           "for i = 1, 60000 do t[i] = i end\n"
                           "console.print('big ' .. #t)\n"));

  // The ATOM: no extra memory, and the kernel's own ceiling, so a megabyte does not fit.
  hal::ExtraMemory extra = hal::platform().extra_memory();
  TEST_ASSERT_EQUAL_UINT32(0, extra.chip_bytes);
  TEST_ASSERT_EQUAL_UINT32(0, extra.total_bytes);
  TEST_ASSERT_EQUAL_UINT32(0, extra.free_bytes);
  TEST_ASSERT_EQUAL_UINT32(0, hal::platform().heap_limit_kb());
  TEST_ASSERT_TRUE(run_captured("run big", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "extra 0"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "limit 131072"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "big 60000"), g_out);

  // The Paper: the chip carries 8 MB and the CPU addresses 4, the ceiling is that memory, and the
  // same app runs.
  use_board("m5stack-paper");
  extra = hal::platform().extra_memory();
  TEST_ASSERT_EQUAL_UINT32(8192u * 1024u, extra.chip_bytes);
  TEST_ASSERT_EQUAL_UINT32(4096u * 1024u, extra.total_bytes);
  TEST_ASSERT_EQUAL_UINT32(4096, hal::platform().heap_limit_kb());
  TEST_ASSERT_TRUE(run_captured("run big", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "extra 4194304"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "limit 4194304"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "big 60000"), g_out);

  storage->remove("/apps/big/app.lua");
  storage->remove("/apps/big/manifest.json");
  storage->remove("/apps/big");
}

// The heap demo of the repository, run from the disk on the board that has the extra memory: it asks
// for a megabyte, keeps it and gives it back, and the numbers are the app's budget and the PSRAM's
// (docs/75, docs/95).
void test_the_heap_demo_app_from_the_disk_runs(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);

  static char manifest[256];
  static char source[4096];
  TEST_ASSERT_GREATER_THAN(0,
                           read_host_file("apps/heap/manifest.json", manifest, sizeof(manifest)));
  TEST_ASSERT_GREATER_THAN(0, read_host_file("apps/heap/heap.lua", source, sizeof(source)));
  TEST_ASSERT_TRUE(add_app_file(storage, "heap-app", manifest, source, "heap.lua"));

  TEST_ASSERT_TRUE(run_captured("run heap-app", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "limit 4194304 B"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "holding 60000"), g_out);
  // A megabyte held, and back after dropping it: the block was the app's and not the board's.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "after dropping it"), g_out);

  storage->remove("/apps/heap-app/heap.lua");
  storage->remove("/apps/heap-app/manifest.json");
  storage->remove("/apps/heap-app");
}

// An open file is a handle with a pool behind it: read, write, size and close,
// and the ninth open is refused because the system has eight (docs/70).
void test_an_app_opens_files_and_the_ninth_is_refused(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "opener",
                            "{\"title\":\"Opener\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                            "local g = fs.data()\n"
                            "fs.mkdir(g)\n"
                            "local nl = string.char(10)\n"
                            "local path = fs.data('data.txt')\n"
                            "fs.write(path, 'linia u' .. nl .. 'linia dos' .. nl)\n"
                            "local f = fs.open(path, 'r')\n"
                            "console.print('type ' .. tostring(f))\n"
                            "console.print('line1 ' .. tostring(f:read('*l')))\n"
                            "console.print('rest ' .. tostring(f:read('*a')):gsub(nl, '/'))\n"
                            "console.print('size ' .. f:size())\n"
                            "f:close()\n"
                            "local w = fs.open(fs.data('out.txt'), 'w')\n"
                            "w:write('abc')\n"
                            "w:close()\n"
                            "console.print('out ' .. fs.read(fs.data('out.txt')))\n"
                            "local held = {}\n"
                            "for i = 1, 8 do held[i] = fs.open(path, 'r') end\n"
                            "local extra, err = fs.open(path, 'r')\n"
                            "console.print('ninth ' .. tostring(extra) .. ' ' .. tostring(err and err.code))\n"
                            "for i = 1, 8 do held[i]:close() end\n"));

  TEST_ASSERT_TRUE(run_captured("run opener", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "type file (open)"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "line1 linia u"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "rest linia dos/"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "size 18"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "out abc"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "ninth nil E_NO_HANDLES"), g_out);

  storage->remove("data/opener/data.txt");
  storage->remove("data/opener/out.txt");
  storage->remove("data/opener");
  storage->remove("/apps/opener/app.lua");
  storage->remove("/apps/opener/manifest.json");
  storage->remove("/apps/opener");
}

// The fake network of this band takes `wifi_ms` of simulated time (docs/85), so
// "the kernel does not wait for the radio" can be measured here: the app waits
// for 20 ms while the network is still connecting, and only then gets its answer.
void test_an_app_connects_without_stopping_the_kernel(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.wifi_ms = 200;
  opts.wifi_absent = false;
  emu::set_options(opts);

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "netup",
                            "{\"title\":\"Net up\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                            "local waiting = net.connect('casa', 'secret', 5000)\n"
                            "console.print('status ' .. net.status())\n"
                            "local started = sys.time_ms()\n"
                            "sys.delay(20):await()\n"
                            "console.print('during ' .. net.status() .. ' after '\n"
                            "  .. (sys.time_ms() - started) .. ' ms')\n"
                            "local ok = waiting:await()\n"
                            "console.print('then ' .. tostring(ok) .. ' ' .. net.status())\n"));

  TEST_ASSERT_TRUE(run_captured("run netup", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "status connecting"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "during connecting after"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "then true up"), g_out);

  storage->remove("/apps/netup/app.lua");
  storage->remove("/apps/netup/manifest.json");
  storage->remove("/apps/netup");
}

// A deadline that comes first is answered with E_TIMEOUT, and the attempt is
// dropped instead of left running (docs/30).
void test_net_connect_gives_up_at_the_deadline(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.wifi_ms = 300;
  opts.wifi_absent = false;
  emu::set_options(opts);

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "netlate",
                            "{\"title\":\"Net late\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                            // `await` raises when the promise failed (docs/30), so an
                            // app that expects a failure catches it.
                            "local p = net.connect('casa', 'x', 40)\n"
                            "local fine, err = pcall(function() return p:await() end)\n"
                            "console.print('deadline ' .. tostring(fine) .. ' '\n"
                            "  .. tostring(err and err.code))\n"
                            "console.print('after ' .. net.status())\n"));

  TEST_ASSERT_TRUE(run_captured("run netlate", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "deadline false E_TIMEOUT"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "after off"), g_out);

  storage->remove("/apps/netlate/app.lua");
  storage->remove("/apps/netlate/manifest.json");
  storage->remove("/apps/netlate");
}

// One attempt at a time: the second ask is answered with E_BUSY and does not
// disturb the first one (docs/30).
void test_a_second_connect_says_busy(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.wifi_ms = 100;
  opts.wifi_absent = false;
  emu::set_options(opts);

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "nettwo",
                            "{\"title\":\"Net two\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                            "local first = net.connect('casa', 'x', 5000)\n"
                            "local busy = net.connect('casa', 'x', 5000)\n"
                            "local fine, err = pcall(function() return busy:await() end)\n"
                            "console.print('second ' .. tostring(fine) .. ' '\n"
                            "  .. tostring(err and err.code))\n"
                            "console.print('first ' .. tostring(first:await()))\n"));

  TEST_ASSERT_TRUE(run_captured("run nettwo", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "second false E_BUSY"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "first true"), g_out);

  storage->remove("/apps/nettwo/app.lua");
  storage->remove("/apps/nettwo/manifest.json");
  storage->remove("/apps/nettwo");
}

// fs.mounts() and fs.space() answer from the real VFS inside a whole app: the
// seven mounts of the board, and a room that is a number when the backend can say
// it (docs/30, docs/70).
void test_an_app_asks_what_is_mounted_and_how_much_room_is_left(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "mtab",
                            "{\"title\":\"Mtab\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                            "local m = fs.mounts()\n"
                            "console.print('mounts ' .. #m)\n"
                            "for _, one in ipairs(m) do\n"
                            "  console.print('  ' .. one.path .. ' ' .. one.type .. ' '\n"
                            "    .. tostring(one.writable))\n"
                            "end\n"
                            "local s, err = fs.space('/data')\n"
                            "console.print('room ' .. tostring(s and s.total) .. ' free '\n"
                            "  .. tostring(s and s.free) .. ' ' .. tostring(err and err.code))\n"
                            "local n, err2 = fs.space('/nvs')\n"
                            "console.print('nvs room ' .. tostring(n) .. ' '\n"
                            "  .. tostring(err2 and err2.code))\n"));

  TEST_ASSERT_TRUE(run_captured("run mtab", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "mounts 8"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "/data littlefs true"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "/nvs nvs false"), g_out);
  // The flash of the bench is a directory, but the board's is a partition of a size its own table
  // says, so `/data` answers the real number (0x0E0000, the littlefs partition of partitions.csv).
  // `/nvs` still cannot: it holds entries and not bytes (docs/70, docs/85).
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "room 917504 free "), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "nvs room nil E_UNSUPPORTED"), g_out);

  storage->remove("/apps/mtab/app.lua");
  storage->remove("/apps/mtab/manifest.json");
  storage->remove("/apps/mtab");
}

// A path with a space reaches the app whole when it goes between quotes: the
// shell splits words once, in one place, and the selector does not see a split
// path (docs/70). Without the quotes it refuses instead of guessing.
void test_a_path_with_spaces_reaches_the_app_in_quotes(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "txts",
                            "{\"title\":\"Txt spaces\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"opens\":\"txt\",\"boards\":[\"esp32\"]}",
                            "if app.args then console.print(\"spaced handler, file \" .. "
                            "app.args.path) end\n"));

  const char kPath[] = "/res/el meu fitxer.txt";
  TEST_ASSERT_TRUE(write_text(storage, kPath, "x\n"));

  // Unquoted: three words, and the shell says how the path has to be written
  // instead of asking which app and handing it half a path. `run_captured` does
  // not report the exit of the command, so the proof is the words: the hint is
  // there and the question is not.
  TEST_ASSERT_TRUE(run_captured("open res/el meu fitxer.txt", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "a path with spaces goes in quotes"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "which one?"), g_out);

  // Quoted: one path, the extension finds the app, and it receives it whole.
  TEST_ASSERT_TRUE(run_captured("open \"/res/el meu fitxer.txt\"", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "which one?"), g_out);
  TEST_ASSERT_TRUE(run_captured("1", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "spaced handler, file /res/el meu fitxer.txt"),
                                g_out);

  storage->remove(kPath);
  storage->remove("/apps/txts/app.lua");
  storage->remove("/apps/txts/manifest.json");
  storage->remove("/apps/txts");
}

// Provisioning, end to end on the fake of this band: the access point comes up,
// the board gets a network, and the promise means **on the network**, not merely
// holding a password (docs/50). Then `forget` takes it away, and with it the
// promise of `connect_stored`.
void test_an_app_provisions_and_then_forgets(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.wifi_absent = false;
  opts.wifi_ms = 100;
  opts.prov_ms = 150;
  opts.prov_never = false;
  opts.prov_fail = false;
  opts.prov_ssid = "casa";
  opts.prov_pass = "secret";
  emu::set_options(opts);

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  // The fake network of this band is a single object for the whole run: the test
  // starts from a board with no network, whatever ran before.
  TEST_ASSERT_TRUE(add_app(storage, "nettidy",
                           "{\"title\":\"Net tidy\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "net.forget()\n"));
  TEST_ASSERT_TRUE(run_captured("run nettidy", g_out, sizeof(g_out)));
  storage->remove("/apps/nettidy/app.lua");
  storage->remove("/apps/nettidy/manifest.json");
  storage->remove("/apps/nettidy");

  TEST_ASSERT_TRUE(add_app(storage, "prov1",
                            "{\"title\":\"Prov\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                            "console.print('before ' .. tostring(net.provisioned()))\n"
                            "console.print('caps ' .. tostring(net.caps().prov))\n"
                            "local ok = net.provision('lunokos', 'lunokos-prov', 5000):await()\n"
                            "console.print('provisioned ' .. tostring(ok) .. ' ' .. net.status())\n"
                            "console.print('now ' .. tostring(net.provisioned()))\n"
                            "console.print('forget ' .. tostring(net.forget()))\n"
                            "console.print('after ' .. tostring(net.provisioned()) .. ' '\n"
                            "  .. net.status())\n"));

  TEST_ASSERT_TRUE(run_captured("run prov1", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "before false"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "caps true"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "provisioned true up"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "now true"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "forget true"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "after false off"), g_out);

  storage->remove("/apps/prov1/app.lua");
  storage->remove("/apps/prov1/manifest.json");
  storage->remove("/apps/prov1");
}

// Nobody ever arrives with the phone: the deadline is answered with E_TIMEOUT and
// the access point is taken down instead of left running (docs/50).
void test_provisioning_gives_up_when_nobody_arrives(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.wifi_absent = false;
  opts.prov_never = true;
  opts.prov_fail = false;
  emu::set_options(opts);

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "prov2",
                            "{\"title\":\"Prov late\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                            "local p = net.provision('lunokos', nil, 80)\n"
                            "local fine, err = pcall(function() return p:await() end)\n"
                            "console.print('gave ' .. tostring(fine) .. ' '\n"
                            "  .. tostring(err and err.code))\n"
                            "console.print('still ' .. tostring(net.provisioned()))\n"));

  TEST_ASSERT_TRUE(run_captured("run prov2", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "gave false E_TIMEOUT"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "still false"), g_out);

  storage->remove("/apps/prov2/app.lua");
  storage->remove("/apps/prov2/manifest.json");
  storage->remove("/apps/prov2");
  opts.prov_never = false;
  emu::set_options(opts);
}

// A line typed the way a person types it while an app is running: it goes to the
// app's console and the app gets a few turns, without waiting for it to end (which
// is what feed_captured does, and an app asking two questions never ends in one).
void type_to_app(const char* line) {
  char typed[160];
  snprintf(typed, sizeof(typed), "%s\r", line);
  kernel::shell::feed(typed, strlen(typed));
  for (int i = 0; i < 100 && kernel::app_running(); ++i) {
    kernel::poll();
    sleep_one_ms();
  }
}

// The radio is reached from the shell now, with no app in the middle: scanning,
// adding and forgetting are commands like `ls` (docs/30, docs/50).
void test_the_wifi_command_scans_and_lists_the_networks(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.wifi_absent = false;
  opts.wifi_ms = 100;
  opts.scan_count = 2;
  opts.prov_ssid = "casa";
  emu::set_options(opts);

  pump_captured(300, g_out, sizeof(g_out), "wifi scan");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "networks: 2"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "1) casa"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "2) neighbour"), g_out);
  TEST_ASSERT_FALSE_MESSAGE(kernel::shell::native_running(), "the command never ended");

  opts.scan_count = 1;
  emu::set_options(opts);
}

// A network that does not say its name is still something the radio saw: the list
// says so instead of skipping it, because a skipped entry is indistinguishable
// from a scan that returned nothing (docs/50).
void test_a_network_with_no_name_is_listed_as_hidden(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.wifi_absent = false;
  opts.wifi_ms = 100;
  opts.prov_ssid = "";
  emu::set_options(opts);

  pump_captured(300, g_out, sizeof(g_out), "wifi scan");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "networks: 1"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "1) (hidden network)"), g_out);
  TEST_ASSERT_FALSE_MESSAGE(kernel::shell::native_running(), "the command never ended");

  opts.prov_ssid = "casa";
  emu::set_options(opts);
}

// A scan that finds nothing answers zero, not `true`: an app loops over that
// number, and `for i = 1, true` is a Lua error that a person sees instead of a
// list (measured on the board, 30/10).
void test_a_scan_that_finds_nothing_answers_zero(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.wifi_absent = false;
  opts.wifi_ms = 100;
  opts.scan_count = 0;
  emu::set_options(opts);

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "zeronet",
                           "{\"title\":\"Zero\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "console.print('found: ' .. tostring(net.scan(2000):await()))\n"));
  TEST_ASSERT_TRUE(run_captured("run zeronet", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "found: 0"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "found: true"), g_out);
  storage->remove("/apps/zeronet/app.lua");
  storage->remove("/apps/zeronet/manifest.json");
  storage->remove("/apps/zeronet");

  opts.scan_count = 1;
  emu::set_options(opts);
}

// Adding a network and forgetting it are the two halves a person needs, and both
// answer in the shell's own words (docs/50).
void test_the_wifi_command_adds_and_forgets_a_network(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.wifi_absent = false;
  opts.wifi_ms = 100;
  emu::set_options(opts);

  // Whatever ran before, this test starts from a board with no network.
  pump_captured(20, g_out, sizeof(g_out), "wifi forget");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "network: none"), g_out);

  pump_captured(60, g_out, sizeof(g_out), "wifi");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "network: none"), g_out);

  pump_captured(400, g_out, sizeof(g_out), "wifi add casa secret");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "wifi: on the network casa"), g_out);
  TEST_ASSERT_FALSE_MESSAGE(kernel::shell::native_running(), "the command never ended");

  pump_captured(20, g_out, sizeof(g_out), "wifi");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "network: casa"), g_out);
  // The network is stored; the radio is not kept on for it. Being on a network is
  // something a task does for a moment, not a state the board sits in (docs/75).
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "state: off"), g_out);

  pump_captured(20, g_out, sizeof(g_out), "wifi forget");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "network: none"), g_out);
  pump_captured(20, g_out, sizeof(g_out), "wifi");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "network: none"), g_out);
}

// A command that waits for the radio must never lock the board: `wifi forget` is
// the person's decision and it drops whatever was running (measured on the board,
// 31/10, where a provisioning waiting for a phone answered "busy" to everything).
void test_wifi_forget_stops_a_job_that_is_running(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.wifi_absent = false;
  opts.wifi_ms = 100;
  opts.prov_ms = 5000;  // a fake access point that waits for a phone
  emu::set_options(opts);

  pump_captured(20, g_out, sizeof(g_out), "wifi forget");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "network: none"), g_out);

  // The provisioning is started and left waiting: nothing is going to answer it.
  pump_captured(10, g_out, sizeof(g_out), "provision");
  TEST_ASSERT_TRUE_MESSAGE(kernel::shell::native_running(), g_out);

  // And forgetting stops it instead of being refused.
  pump_captured(30, g_out, sizeof(g_out), "wifi forget");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "radio: stopped"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "network: none"), g_out);
  TEST_ASSERT_FALSE_MESSAGE(kernel::shell::native_running(), "the job was not stopped");

  opts.prov_ms = 2000;
  emu::set_options(opts);
}

// A board that remembers a network but is not on it: `wifi connect` is the word
// that puts the record and the radio together, and with nothing stored it says so
// instead of starting a job (docs/50).
void test_the_wifi_command_connects_with_the_stored_network(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.wifi_absent = false;
  opts.wifi_ms = 100;
  emu::set_options(opts);

  pump_captured(20, g_out, sizeof(g_out), "wifi forget");
  pump_captured(20, g_out, sizeof(g_out), "wifi connect");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "wifi: the board has no network to connect to"), g_out);
  TEST_ASSERT_FALSE_MESSAGE(kernel::shell::native_running(), "a job started with nothing stored");

  // With one stored, the command connects and the shell says which network.
  pump_captured(400, g_out, sizeof(g_out), "wifi add casa secret");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "wifi: on the network casa"), g_out);
  pump_captured(400, g_out, sizeof(g_out), "wifi connect");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "network: casa"), g_out);
  TEST_ASSERT_FALSE_MESSAGE(kernel::shell::native_running(), "the command never ended");
}

// Three ESC in a row stop a command that is waiting for the radio, the same way
// they leave an app: a provisioning waiting for a phone had no exit but a word
// you had to know (measured on the board, 31/10).
void test_three_esc_stop_a_radio_command(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.wifi_absent = false;
  opts.prov_ms = 5000;
  emu::set_options(opts);

  pump_captured(20, g_out, sizeof(g_out), "wifi forget");
  pump_captured(10, g_out, sizeof(g_out), "provision");
  TEST_ASSERT_TRUE_MESSAGE(kernel::shell::native_running(), g_out);

  pump_captured(10, g_out, sizeof(g_out), nullptr, "\x1b\x1b\x1b");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "radio: stopped"), g_out);
  TEST_ASSERT_FALSE_MESSAGE(kernel::shell::native_running(), "the command was not stopped");

  // Two ESC are not three: the shell is not a keyboard of one key.
  pump_captured(10, g_out, sizeof(g_out), "provision");
  TEST_ASSERT_TRUE(kernel::shell::native_running());
  pump_captured(10, g_out, sizeof(g_out), nullptr, "\x1b\x1b");
  TEST_ASSERT_TRUE_MESSAGE(kernel::shell::native_running(), g_out);
  pump_captured(10, g_out, sizeof(g_out), "wifi stop");
  TEST_ASSERT_FALSE_MESSAGE(kernel::shell::native_running(), "wifi stop did not stop it");

  opts.prov_ms = 2000;
  emu::set_options(opts);
}

// The QR names the app of the transport it is going to be used with: a Bluetooth
// board read with the SoftAP app leads nowhere (docs/50).
void test_the_qr_names_the_app_of_the_transport(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.wifi_absent = false;
  opts.prov_ms = 5000;
  emu::set_options(opts);

  pump_captured(20, g_out, sizeof(g_out), "wifi forget");

  pump_captured(10, g_out, sizeof(g_out), "provision");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "scan this with the ESP BLE Provisioning app"), g_out);
  pump_captured(10, g_out, sizeof(g_out), "wifi stop");
  TEST_ASSERT_FALSE_MESSAGE(kernel::shell::native_running(), "the command was not stopped");

  pump_captured(10, g_out, sizeof(g_out), "provision softap");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "scan this with the ESP SoftAP Provisioning app"), g_out);
  pump_captured(10, g_out, sizeof(g_out), "wifi stop");
  TEST_ASSERT_FALSE_MESSAGE(kernel::shell::native_running(), "the command was not stopped");

  opts.prov_ms = 2000;
  emu::set_options(opts);
}

// The radio is on only while there is something to do: a scan that nobody keeps
// turns it off when the queue empties, and a network that somebody asked for
// keeps it on (docs/75).
void test_a_scan_leaves_the_radio_off(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.wifi_absent = false;
  opts.wifi_ms = 100;
  opts.scan_count = 1;
  emu::set_options(opts);

  pump_captured(300, g_out, sizeof(g_out), "wifi scan");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "networks: 1"), g_out);

  pump_captured(20, g_out, sizeof(g_out), "wifi");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "state: off"), g_out);
}

// The clock is set from a time server and the app sees it: `time.now()` goes from
// zero to the time the server gave, and `net.ntp` answers when it is done
// (docs/60).
void test_ntp_sets_the_clock(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.wifi_absent = false;
  opts.wifi_ms = 100;
  opts.now_epoch = 0;  // a board that has never known what day it is
  emu::set_options(opts);

  pump_captured(20, g_out, sizeof(g_out), "wifi forget");
  // A time server needs a network: the board gets on one first, and it is on it
  // for the rest of the test.
  pump_captured(400, g_out, sizeof(g_out), "wifi add casa secret");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "wifi: on the network casa"), g_out);

  // A board with no clock says zero, and there is a number after the time server.
  pump_captured(400, g_out, sizeof(g_out), "ntp");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "time: "), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "time: 0 "), g_out);
  TEST_ASSERT_FALSE_MESSAGE(kernel::shell::native_running(), "the command never ended");
}

// A time server without a network is refused at once and said out loud: the
// person was watching a command that waited for a deadline it could not meet
// (measured on the board, 31/10).
void test_ntp_without_a_network_is_refused(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.wifi_absent = false;
  opts.wifi_ms = 100;
  emu::set_options(opts);

  // A board with no network stored: `ntp` cannot even get on one.
  pump_captured(20, g_out, sizeof(g_out), "wifi forget");
  pump_captured(20, g_out, sizeof(g_out), "ntp");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "ntp: the board has no network to use"), g_out);
  TEST_ASSERT_FALSE_MESSAGE(kernel::shell::native_running(), "a job started with no network");
}

// A page comes down and the console shows its status and its body. The network is
// brought up by the download itself: there is no `wifi connect` in front of it
// (docs/30, docs/75).
void test_the_http_command_downloads_a_page(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.wifi_absent = false;
  opts.wifi_ms = 100;
  opts.http_fake = true;
  opts.http_ms = 50;
  opts.http_status = 200;
  opts.http_body = "una pagina de prova";
  emu::set_options(opts);

  pump_captured(20, g_out, sizeof(g_out), "wifi forget");
  pump_captured(400, g_out, sizeof(g_out), "wifi add casa secret");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "wifi: on the network casa"), g_out);

  pump_captured(400, g_out, sizeof(g_out), "http https://example.com/hola");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "http: 200 19 bytes"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "una pagina de prova"), g_out);
  // A page that fits is a page in memory: nothing of it touches the volume (docs/30).
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "saved to"), g_out);
  TEST_ASSERT_FALSE_MESSAGE(kernel::shell::native_running(), "the command never ended");
}

// **`repo info <app>` llegeix el manifest i en fa una fitxa** (0.82.1): la comanda demana el fitxer cru a
// GitHub i el pinta amb el lector de JSON del sistema, que és el mateix que llegeix els manifests del disc. La
// xarxa és la falsa del banc —una resposta enllaunada— perquè el que es prova és la comanda i no l'adreça
// (docs/30, docs/40).
void test_the_repo_command_shows_the_manifest_of_an_app(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.wifi_absent = false;
  opts.wifi_ms = 100;
  opts.http_fake = true;
  opts.http_ms = 50;
  opts.http_status = 200;
  opts.http_body =
      "{\"title\":\"Una app\",\"version\":\"1.2.3\",\"author\":\"@algu\",\"entry\":\"a.lua\","
      "\"opens\":\"txt\",\"boards\":[\"esp32\",\"m5stack-paper\"],"
      "\"description\":\"A description that is long enough to be worth wrapping when the console is eighty "
      "columns wide, which is what this line is for.\"}";
  emu::set_options(opts);

  pump_captured(20, g_out, sizeof(g_out), "wifi forget");
  pump_captured(400, g_out, sizeof(g_out), "wifi add casa secret");
  pump_captured(400, g_out, sizeof(g_out), "repo info alguna");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "repo: Una app (lunokjod/lunokOS/apps/alguna)"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "title       Una app"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "version     1.2.3"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "author      @algu"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "entry       a.lua"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "opens       txt"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "boards      esp32, m5stack-paper"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "description A description"), g_out);
  TEST_ASSERT_FALSE_MESSAGE(kernel::shell::native_running(), "the command never ended");

  // **Un manifest sense `entry` no és una app que es pugui córrer**, i la fitxa ho diu en lloc de fer veure que
  // sí (docs/40).
  opts.http_body = "{\"title\":\"Mig\"}";
  emu::set_options(opts);
  pump_captured(400, g_out, sizeof(g_out), "repo info mija");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "not a usable app: no entry"), g_out);

  // **The name of the command is `repo` and nothing else** (0.84.2): the old `repository` is not an alias that lives on
  // —two names for one command is a person who types the one that does not work— (docs/30).
  pump_captured(40, g_out, sizeof(g_out), "repository info alguna");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "unknown command: repository"), g_out);
}

// A page bigger than the room the command has is not cut and not lost: **it goes to a file whole**, and
// the shell says where. The point of asking for a page is to be able to read it, and half a page is not
// one (docs/30, docs/70).
void test_the_http_command_saves_a_page_that_does_not_fit(void) {
  TEST_ASSERT_TRUE(boot_once());
  static char big[20000];
  memset(big, 'x', sizeof(big) - 1);
  big[sizeof(big) - 1] = '\0';

  emu::Options opts = emu::options();
  opts.wifi_absent = false;
  opts.wifi_ms = 100;
  opts.http_fake = true;
  opts.http_ms = 50;
  opts.http_body = big;
  emu::set_options(opts);

  pump_captured(20, g_out, sizeof(g_out), "wifi forget");
  pump_captured(400, g_out, sizeof(g_out), "wifi add casa secret");
  pump_captured(400, g_out, sizeof(g_out), "http https://example.com/gran");
  const char* at = strstr(g_out, "http: 200 19999 bytes, saved to ");
  TEST_ASSERT_NOT_NULL_MESSAGE(at, g_out);

  // The path the shell printed is the file, and the file **is the whole page**: not the beginning, not
  // a page with the end missing (docs/70).
  char path[128];
  size_t used = 0;
  for (const char* p = at + strlen("http: 200 19999 bytes, saved to ");
       *p != '\0' && *p != '\r' && used + 1 < sizeof(path); ++p) {
    path[used++] = *p;
  }
  path[used] = '\0';
  TEST_ASSERT_GREATER_THAN_MESSAGE(0, used, g_out);
  static char page[20001];
  size_t got = 0;
  TEST_ASSERT_TRUE_MESSAGE(kernel::vfs::read_file(path, page, sizeof(page) - 1, got), path);
  page[got] = '\0';
  TEST_ASSERT_EQUAL_UINT32(19999, static_cast<uint32_t>(got));
  TEST_ASSERT_EQUAL_STRING(big, page);

  // What a person sees is the beginning of it, read back from the volume, and the rest as a number.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "xxxx"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "bytes more"), g_out);
  TEST_ASSERT_FALSE_MESSAGE(kernel::shell::native_running(), "the command never ended");

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  if (storage != nullptr) storage->remove("/http/gran");
}

// A page without a network is refused at once and said out loud, like the time
// server: the person is not left watching a deadline it cannot meet (docs/30).
void test_http_without_a_network_is_refused(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.wifi_absent = false;
  opts.wifi_ms = 100;
  emu::set_options(opts);

  pump_captured(20, g_out, sizeof(g_out), "wifi forget");
  pump_captured(20, g_out, sizeof(g_out), "http https://example.com/");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "http: the board has no network to use"),
                               g_out);
  TEST_ASSERT_FALSE_MESSAGE(kernel::shell::native_running(), "a job started with no network");
}

// An app downloads a page while the system keeps turning, and gets a table with
// the status and the body (docs/30).
void test_an_app_downloads_a_page(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.wifi_absent = false;
  opts.wifi_ms = 100;
  opts.http_fake = true;
  opts.http_ms = 50;
  opts.http_status = 201;
  opts.http_body = "coses";
  emu::set_options(opts);

  pump_captured(20, g_out, sizeof(g_out), "wifi forget");
  pump_captured(400, g_out, sizeof(g_out), "wifi add casa secret");
  TEST_ASSERT_TRUE(run_captured("run page", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "caps http true tls true"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "status 201 bytes 5"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "body coses"), g_out);
}

// The demo app that downloads a page is a file in the repository, so it is run
// here: an example that stops working is a lie in the documentation.
void test_the_page_demo_app_from_the_disk_runs(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);

  static char manifest[256];
  static char source[4096];
  TEST_ASSERT_GREATER_THAN(
      0, read_host_file("apps/page/manifest.json", manifest, sizeof(manifest)));
  TEST_ASSERT_GREATER_THAN(0,
                           read_host_file("apps/page/page.lua", source, sizeof(source)));
  TEST_ASSERT_TRUE(add_app_file(storage, "page-app", manifest, source, "page.lua"));

  emu::Options opts = emu::options();
  opts.wifi_absent = false;
  opts.wifi_ms = 100;
  opts.http_fake = true;
  opts.http_ms = 50;
  opts.http_status = 200;
  opts.http_body = "l'exemple de la pagina";
  emu::set_options(opts);
  pump_captured(20, g_out, sizeof(g_out), "wifi forget");
  pump_captured(400, g_out, sizeof(g_out), "wifi add casa secret");

  TEST_ASSERT_TRUE(run_captured("run page-app", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "fetching https://example.com/"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "status 200 (22 bytes)"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "l'exemple de la pagina"), g_out);

  storage->remove("/apps/page-app/page.lua");
  storage->remove("/apps/page-app/manifest.json");
  storage->remove("/apps/page-app");
}

// A page asked for with a place to go: memory holds nothing, the file has the page, and the shell says
// where it went (docs/30, docs/70).
void test_the_http_command_writes_where_it_is_told(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);

  emu::Options opts = emu::options();
  opts.wifi_absent = false;
  opts.wifi_ms = 100;
  opts.http_fake = true;
  opts.http_ms = 50;
  opts.http_status = 200;
  opts.http_body = "una pagina de prova";
  emu::set_options(opts);

  pump_captured(20, g_out, sizeof(g_out), "wifi forget");
  pump_captured(400, g_out, sizeof(g_out), "wifi add casa secret");
  pump_captured(400, g_out, sizeof(g_out), "http https://example.com/hola /littlefs/pagina.html");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "http: 200 19 bytes, saved to /littlefs/pagina.html"),
                                g_out);
  char page[64];
  size_t got = 0;
  TEST_ASSERT_TRUE_MESSAGE(kernel::vfs::read_file("/littlefs/pagina.html", page, sizeof(page) - 1, got),
                           "the page was not written where the shell said");
  page[got] = '\0';
  TEST_ASSERT_EQUAL_STRING("una pagina de prova", page);
  storage->remove("/pagina.html");
}

// A place that is not a volume of this board is refused before the page is asked for, and said out
// loud: a download that fails at the last byte is a download nobody wanted (docs/30, docs/70).
void test_the_http_command_refuses_a_place_that_is_not_there(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.wifi_absent = false;
  opts.wifi_ms = 100;
  opts.http_fake = true;
  opts.http_ms = 50;
  opts.http_body = "no hi cap";
  emu::set_options(opts);

  pump_captured(20, g_out, sizeof(g_out), "wifi forget");
  pump_captured(400, g_out, sizeof(g_out), "wifi add casa secret");
  pump_captured(400, g_out, sizeof(g_out), "http https://example.com/hola /nocard/pagina.html");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "http: that is not a place this board can write"), g_out);
  TEST_ASSERT_FALSE_MESSAGE(kernel::shell::native_running(), "the command never ended");
}

// An app downloads a page **to a file** and reads it back: the page is the file's and the heap is the
// app's, which is how a page of a few hundred kilobytes is worked with on a board (docs/30, docs/75).
void test_an_app_downloads_a_page_to_a_file(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  static char manifest[256];
  static char source[1024];
  snprintf(manifest, sizeof(manifest),
           "{\"title\":\"Dl\",\"version\":\"0.1.0\",\"entry\":\"dl.lua\","
           "\"boards\":[\"esp32\"]}");
  // **The page goes where the app may write**: its own ground, which is the only place a write of an
  // app lands (docs/70).
  snprintf(source, sizeof(source),
           "local r = http.download('https://example.com/un', '/data/dl/baixada.txt'):await()\n"
           "console.print('status ' .. r.status .. ' bytes ' .. r.bytes)\n"
           "console.print('path ' .. tostring(r.path))\n"
           "local f = fs.open(r.path, 'r')\n"
           "console.print('head ' .. tostring(f:read(40)))\n"
           "f:close()\n");
  TEST_ASSERT_TRUE(add_app_file(storage, "dl", manifest, source, "dl.lua"));

  emu::Options opts = emu::options();
  opts.wifi_absent = false;
  opts.wifi_ms = 100;
  opts.http_fake = true;
  opts.http_ms = 50;
  opts.http_status = 201;
  opts.http_body = "una pagina";
  emu::set_options(opts);
  pump_captured(20, g_out, sizeof(g_out), "wifi forget");
  pump_captured(400, g_out, sizeof(g_out), "wifi add casa secret");

  TEST_ASSERT_TRUE(run_captured("run dl", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "status 201 bytes 10"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "path /data/dl/baixada.txt"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "head una pagina"), g_out);

  storage->remove("/data/dl/baixada.txt");
  storage->remove("/apps/dl/dl.lua");
  storage->remove("/apps/dl/manifest.json");
  storage->remove("/apps/dl");
}

// **Un fitxer es llegeix a trossos i el banc no diu que no** (0.66.0): `fs.read_at` demana un bloc a
// l'intèrpret que creix, i el heap del banc responia `not enough memory` amb megabytes lliures perquè un bloc
// que no podia créixer on era només provava l'altra memòria (docs/75, docs/85). El sostre d'aquest banc de
// proves és el de l'ATOM (64 KB per app), o sigui que el tros és de 12 KB i el fitxer de 60: el que es prova
// és que cada tros arriba sencer.
void test_a_big_file_is_read_in_chunks(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);

  static const size_t kBytes = 60 * 1024;
  static char payload[kBytes];
  for (size_t i = 0; i < kBytes; ++i) payload[i] = static_cast<char>('a' + (i % 26));
  hal::FileSlot slot;
  TEST_ASSERT_TRUE(storage->open("/big.bin", hal::OpenMode::kWrite, slot));
  TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(kBytes),
                          storage->write(slot, payload, static_cast<uint32_t>(kBytes)));
  storage->close(slot);

  TEST_ASSERT_TRUE(add_app(storage, "reader",
                           "{\"title\":\"Reader\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local total = 0\n"
                           "local wrong = 0\n"
                           "local at = 0\n"
                           "local path = app.args.path\n"
                           "while at < 61440 do\n"
                           "  local want = math.min(12288, 61440 - at)\n"
                           "  local got = fs.read_at(path, at, want)\n"
                           "  if got == nil or #got ~= want then wrong = wrong + 1 break end\n"
                           "  total = total + #got\n"
                           "  at = at + #got\n"
                           "end\n"
                           "console.print('read ' .. total .. ' wrong ' .. wrong)\n"));

  TEST_ASSERT_TRUE(run_captured("run reader /littlefs/big.bin", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "read 61440 wrong 0"), g_out);

  storage->remove("/big.bin");
  storage->remove("/apps/reader/app.lua");
  storage->remove("/apps/reader/manifest.json");
  storage->remove("/apps/reader");
}

// **El camí que el prompt nomena és l'única porta de l'app al disc de la persona** (0.65.0): una app
// arrencada amb `run <id> <path>` pot llegir i escriure **aquell** fitxer, i qualsevol altre camí del disc
// de la persona continua responent `E_INVAL`. És el que fa que un editor desi on la persona ha dit i no on
// l'app vulgui (docs/30, docs/70).
void test_an_app_writes_the_file_the_prompt_named_and_no_other(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);

  static const char kManifest[] =
      "{\"title\":\"Writer\",\"version\":\"0.0.1\",\"entry\":\"writer.lua\","
      "\"boards\":[\"esp32\"]}";
  static const char kSource[] =
      "local path = app.args and app.args.path or nil\n"
      "console.print('path ' .. tostring(path))\n"
      "if path ~= nil then\n"
      "  local ok, err = fs.write(path, 'from the app')\n"
      "  console.print('named ' .. tostring(ok ~= nil) .. ' ' .. tostring(err and err.message))\n"
      "  console.print('read ' .. tostring(fs.read(path)))\n"
      "end\n"
      "local other, why = fs.write('/littlefs/elsewhere.txt', 'x')\n"
      "console.print('other ' .. tostring(other) .. ' ' .. tostring(why and why.code))\n"
      "local temp, temp_why = fs.open(path .. '.tmp', 'w')\n"
      "console.print('temp ' .. tostring(temp ~= nil) .. ' ' .. tostring(temp_why and temp_why.code))\n"
      "if temp ~= nil then temp:close() end\n";
  TEST_ASSERT_TRUE(add_app_file(storage, "writer", kManifest, kSource, "writer.lua"));
  TEST_ASSERT_TRUE(write_text(storage, "/notes.txt", "original"));

  TEST_ASSERT_TRUE(run_captured("run writer /littlefs/notes.txt", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "named true nil"), g_out);
  // **La resta del disc segueix tancada**: el camí nomenat és una excepció i no una porta oberta.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "other nil E_INVAL"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "read from the app"), g_out);
  // **El temporal del camí nomenat també és el camí nomenat** (0.66.0): desar des d'una app és escriure
  // `<path>.tmp` i reanomenar-lo, i sense això l'escriptura atòmica no podia existir des d'una app.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "temp true nil"), g_out);

  // **I el nom sobreviu a la comanda** (4/11): la paraula del prompt viu en una pila que ja no hi és quan
  // l'app corre, i el camí que el nucli consulta per deixar-la desar ha de ser una còpia.
  TEST_ASSERT_TRUE(kernel::execute("run writer /littlefs/notes.txt"));
  // **The turns are borrowed and not waited for**: an app that never ends has to fail this test and not take the whole
  // run with it, which is a suite that stops with no output and a person who cannot tell why (found the 4/11,
  // docs/80).
  int turns = 0;
  while (kernel::app_running() && turns++ < kTurnBudget) kernel::poll();
  TEST_ASSERT_FALSE_MESSAGE(kernel::app_running(), "the app never ended");
  TEST_ASSERT_EQUAL_STRING("/littlefs/notes.txt", luabind::app_path());

  // An app started with no file has no way into the person's disk at all.
  TEST_ASSERT_TRUE(run_captured("run writer", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "path nil"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "other nil E_INVAL"), g_out);

  storage->remove("/notes.txt");
  storage->remove("/notes.txt.tmp");
  storage->remove("/apps/writer/writer.lua");
  storage->remove("/apps/writer/manifest.json");
  storage->remove("/apps/writer");
}

// A page asked for while the radio is still up from the job before it: the
// request must not be started a second time when its answer comes. Started twice,
// the second start found the first one in the way and the person saw `E_BUSY`
// with a network that was working (measured on the PC with the batch mode, 31/10).
void test_a_page_on_a_radio_that_is_still_up(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.wifi_absent = false;
  opts.wifi_ms = 100;
  opts.http_fake = true;
  opts.http_ms = 50;
  opts.http_status = 200;
  opts.http_body = "encara hi era";
  emu::set_options(opts);

  pump_captured(20, g_out, sizeof(g_out), "wifi forget");
  // The command finishes and **nothing is polled after it**, so the radio is
  // still on when the next line runs: that is what a batch script does.
  kernel::execute("wifi add casa secret");
  for (int i = 0; i < kTurnBudget && kernel::shell::native_running(); ++i) {
    kernel::poll();
    sleep_one_ms();
  }
  TEST_ASSERT_FALSE_MESSAGE(kernel::shell::native_running(), "the network never came up");

  pump_captured(400, g_out, sizeof(g_out), "http https://example.com/");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "http: 200 13 bytes"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "encara hi era"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "E_BUSY"), g_out);
}

// What the board can do about energy, said with the numbers and not with
// adjectives: the modes, where it comes back from, and that it has no gauge
// (docs/30, docs/50).
void test_the_power_command_says_what_the_board_can_do(void) {
  TEST_ASSERT_TRUE(boot_once());
  pump_captured(20, g_out, sizeof(g_out), "power");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "sleep: light deep"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "wake from: timer"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "battery: no gauge on this board"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "keep awake: no"), g_out);
}

// The light is painted by the system, an app or a person can take it, and giving it
// back is one call. What matters is that the board really shows it: the test asks
// the band and not the policy (docs/30, docs/50).
void test_the_led_command_paints_and_gives_it_back(void) {
  TEST_ASSERT_TRUE(boot_once());
  const uint32_t before = emu::led_frames();

  pump_captured(20, g_out, sizeof(g_out), "led 00ff00");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "led: 00ff00"), g_out);
  TEST_ASSERT_TRUE_MESSAGE(emu::led_frames() > before, "the light was never painted");
  // Twenty and not 255: this is the panel of the Matrix, and its ceiling is the
  // maker's number (docs/30, docs/50).
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "led: 00ff00 (this board stops at 20)"), g_out);
  TEST_ASSERT_EQUAL_UINT8(20, emu::led_green());
  TEST_ASSERT_EQUAL_UINT8(0, emu::led_red());

  pump_captured(20, g_out, sizeof(g_out), "led");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "showing 001400 (taken)"), g_out);

  pump_captured(20, g_out, sizeof(g_out), "led off");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "the light is the system's again"), g_out);
  pump_captured(20, g_out, sizeof(g_out));
  TEST_ASSERT_EQUAL_UINT8(0, emu::led_green());

  // A colour that is not six hex digits is refused instead of being read as black.
  pump_captured(20, g_out, sizeof(g_out), "led notacolour");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "usage: led [rrggbb|off]"), g_out);
}

// The colour an app took is given back when the app goes, and **given back means
// off**: a light that outlives its app is energy spent by nobody (docs/30, docs/75).
void test_the_light_goes_off_when_its_app_goes(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "lit",
                            "{\"title\":\"Lit\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                            "console.print('taking the light')\n"
                            "led.set(0, 0, 255)\n"
                            "sys.delay(50):await()\n"
                            "console.print('giving it back')\n"));

  TEST_ASSERT_TRUE_MESSAGE(run_captured("run lit", g_out, sizeof(g_out)), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "taking the light"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "giving it back"), g_out);
  TEST_ASSERT_FALSE(kernel::app_running());

  // What is on the band now is what the exit left there: the light the app took is
  // not showing any more.
  pump_captured(20, g_out, sizeof(g_out));
  TEST_ASSERT_EQUAL_UINT8(0, emu::led_red());
  TEST_ASSERT_EQUAL_UINT8(0, emu::led_green());
  TEST_ASSERT_EQUAL_UINT8(0, emu::led_blue());
  pump_captured(20, g_out, sizeof(g_out), "led");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "showing 000000 (the system's)"), g_out);

  storage->remove("/apps/lit/app.lua");
  storage->remove("/apps/lit/manifest.json");
  storage->remove("/apps/lit");

  // The other way out: an app that is closed while it is still alive (this is what
  // three taps do) gives the light back too. Two exits, two paths, and a test for
  // each, like the exit code (docs/30, docs/80).
  TEST_ASSERT_TRUE(add_app(storage, "stuck",
                            "{\"title\":\"Stuck\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                            "console.print('stuck with the light')\n"
                            "led.set(255, 0, 0)\n"
                            "sys.delay(30000):await()\n"));
  pump_captured(60, g_out, sizeof(g_out), "run stuck");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "stuck with the light"), g_out);
  pump_captured(20, g_out, sizeof(g_out));
  TEST_ASSERT_EQUAL_UINT8(20, emu::led_red());

  capture_begin();
  for (int tap = 0; tap < 3; ++tap) {
    emu::press_button(true);
    pump_turns(10);
    emu::press_button(false);
    pump_turns(10);
  }
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_FALSE_MESSAGE(kernel::app_running(), "the app survived three taps");
  pump_captured(20, g_out, sizeof(g_out));
  TEST_ASSERT_EQUAL_UINT8(0, emu::led_red());

  storage->remove("/apps/stuck/app.lua");
  storage->remove("/apps/stuck/manifest.json");
  storage->remove("/apps/stuck");
}

// An app plays an effect and keeps working: the light is painted between turns, so a
// promise that waits does not stop it and the app does not wait for it (docs/30).
void test_an_app_plays_an_effect_while_it_works(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "glow",
                            "{\"title\":\"Glow\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                            "led.play{kind='breathe', color={0, 0, 255}, period=1000, cycles=2}\n"
                            "local e = led.effect()\n"
                            "console.print('playing ' .. e.kind .. ' ' .. e.period)\n"
                            "sys.delay(120):await()\n"
                            "console.print('still working ' .. led.effect().kind)\n"));

  const uint32_t before = emu::led_frames();
  TEST_ASSERT_TRUE_MESSAGE(run_captured("run glow", g_out, sizeof(g_out)), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "playing breathe 1000"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "still working breathe"), g_out);
  TEST_ASSERT_TRUE_MESSAGE(emu::led_frames() > before, "the effect was never painted");

  // The app went, and the light went with it: the blue was the app's and not the
  // system's (docs/30, docs/75).
  pump_captured(20, g_out, sizeof(g_out));
  TEST_ASSERT_EQUAL_UINT32(0, emu::led_red() + emu::led_green() + emu::led_blue());

  storage->remove("/apps/glow/app.lua");
  storage->remove("/apps/glow/manifest.json");
  storage->remove("/apps/glow");
}

// The four moments of the system are the app's to change, and the maker's come back with
// one word (docs/30).
void test_an_app_changes_the_system_moments(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "moods",
                            "{\"title\":\"Moods\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                            "console.print('alarm is ' .. led.system('alarm').kind)\n"
                            "led.system('alarm', {kind='blink', color={9, 8, 7}, period=400, cycles=1})\n"
                            "local now = led.system('alarm')\n"
                            "console.print('now ' .. now.kind .. ' ' .. now.color[3])\n"
                            "led.system('alarm', 'default')\n"
                            "console.print('back ' .. led.system('alarm').kind)\n"
                            "local all = led.system()\n"
                            "console.print('four ' .. tostring(all.boot ~= nil and all.radio ~= nil "
                            "and all.alarm ~= nil and all.error ~= nil))\n"));

  TEST_ASSERT_TRUE_MESSAGE(run_captured("run moods", g_out, sizeof(g_out)), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "alarm is bounce"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "now blink 7"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "back bounce"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "four true"), g_out);

  storage->remove("/apps/moods/app.lua");
  storage->remove("/apps/moods/manifest.json");
  storage->remove("/apps/moods");
}

// What the light cannot do is refused with a reason and not guessed: an effect that does
// not exist, a call that is not a table, and a light of the strip that is not there
// (docs/30).
void test_led_play_refuses_what_it_cannot_do(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "wrong",
                            "{\"title\":\"Wrong\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                            "local _, bad = led.play{kind='disco'}\n"
                            "console.print('kind ' .. bad.code)\n"
                            "local _, notatable = led.play('nope')\n"
                            "console.print('table ' .. notatable.code)\n"
                            "local _, nolight = led.play{kind='blink', pixel=5}\n"
                            "console.print('pixel ' .. nolight.code)\n"
                            "local ok = led.stop(0)\n"
                            "console.print('stop ' .. tostring(ok))\n"));

  TEST_ASSERT_TRUE_MESSAGE(run_captured("run wrong", g_out, sizeof(g_out)), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "kind E_INVAL"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "table E_INVAL"), g_out);
  // The bench is the ATOM here: one light, so light number five is not there.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "pixel E_INVAL"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "stop true"), g_out);

  storage->remove("/apps/wrong/app.lua");
  storage->remove("/apps/wrong/manifest.json");
  storage->remove("/apps/wrong");
}

// The air around the board is read from the board and not invented: the Paper has an SHT30, the ATOM
// says it has none, and the two are two different answers (docs/30, docs/50).
void test_the_air_is_read_from_the_board_that_has_it(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "air",
                            "{\"title\":\"Air\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                            "console.print('env cap ' .. tostring(sys.caps().env))\n"
                            "local air, err = env.read()\n"
                            "if air then\n"
                            "  console.print(string.format('air %.1f %.1f', air.celsius, air.humidity))\n"
                            "else\n"
                            "  console.print('no air ' .. err.code)\n"
                            "end\n"));

  // The ATOM: the capability is false and the call says which of the two it is. The board is said and
  // not assumed, like every test that is about a board (docs/85).
  use_board("m5stack-atom");
  TEST_ASSERT_TRUE_MESSAGE(run_captured("run air", g_out, sizeof(g_out)), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "env cap false"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "no air E_UNSUPPORTED"), g_out);

  // The Paper: the chip answers the two numbers the bench was told (docs/85).
  use_board("m5stack-paper");
  emu::Options opts = emu::options();
  opts.env_celsius = 21.5f;
  opts.env_humidity = 48.0f;
  opts.env_fail = false;
  emu::set_options(opts);
  TEST_ASSERT_TRUE_MESSAGE(run_captured("run air", g_out, sizeof(g_out)), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "env cap true"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "air 21.5 48.0"), g_out);

  // And a chip that is there and does not answer is `E_IO`, not `E_UNSUPPORTED`: an app has to be
  // able to tell a fault from a board that never had the sensor (docs/30).
  opts.env_fail = true;
  emu::set_options(opts);
  TEST_ASSERT_TRUE_MESSAGE(run_captured("run air", g_out, sizeof(g_out)), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "no air E_IO"), g_out);
  opts.env_fail = false;
  emu::set_options(opts);

  storage->remove("/apps/air/app.lua");
  storage->remove("/apps/air/manifest.json");
  storage->remove("/apps/air");
}

// The demo app of the light is a file in the repository, so it is run here: a wheel
// that never ends on its own, a press that steps the brightness and the system's
// gesture to leave (docs/30).
void test_the_light_demo_app_from_the_disk_runs(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);

  static char manifest[256];
  static char source[4096];
  TEST_ASSERT_GREATER_THAN(
      0, read_host_file("apps/light/manifest.json", manifest, sizeof(manifest)));
  TEST_ASSERT_GREATER_THAN(
      0, read_host_file("apps/light/light.lua", source, sizeof(source)));
  TEST_ASSERT_TRUE(add_app_file(storage, "light-app", manifest, source, "light.lua"));

  // It does not end: it is a wheel. The prompt comes back when the system's gesture
  // closes it, so this runs it and takes turns instead of waiting for it.
  pump_captured(40, g_out, sizeof(g_out), "run light-app");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "a colour wheel"), g_out);
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), "the wheel ended on its own");
  const uint32_t turned = emu::led_frames();
  TEST_ASSERT_TRUE_MESSAGE(turned > 0, "the light was never painted");

  // One press goes up a step: the brightest channel of the colour is the step's
  // value, and the light keeps turning. The press and the release are **one**
  // capture: the app hears the press between the two pumps, and a second capture
  // would truncate the first one and look at an empty window.
  capture_begin();
  emu::press_button(true);
  pump_turns(20);
  emu::press_button(false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "brightness 4 of 20"), g_out);
  TEST_ASSERT_TRUE_MESSAGE(emu::led_frames() > turned, "the wheel stopped");
  uint8_t bright = emu::led_red();
  if (emu::led_green() > bright) bright = emu::led_green();
  if (emu::led_blue() > bright) bright = emu::led_blue();
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(4, bright, "the light did not take the step");

  // Three taps are the way out, and the light the app took goes with it.
  capture_begin();
  for (int tap = 0; tap < 2; ++tap) {
    emu::press_button(true);
    pump_turns(10);
    emu::press_button(false);
    pump_turns(10);
  }
  capture_end(nullptr, 0);
  TEST_ASSERT_FALSE_MESSAGE(kernel::app_running(), "three taps did not close it");
  pump_captured(20, g_out, sizeof(g_out));
  TEST_ASSERT_EQUAL_UINT32(0, emu::led_red() + emu::led_green() + emu::led_blue());

  storage->remove("/apps/light-app/light.lua");
  storage->remove("/apps/light-app/manifest.json");
  storage->remove("/apps/light-app");
}

// The Matrix app of the repository, run on the fake panel: twenty-five lights painted
// one by one with a hue that walks around them, which is the one thing the app of the
// single light cannot say, and the whole panel given back when it goes (docs/30,
// docs/50).
void test_the_matrix_app_from_the_disk_paints_the_panel(void) {
  TEST_ASSERT_TRUE(boot_once());
  // The app says it runs on the Matrix, and the bench is told to be one: on the PC as
  // on the glass, an app of another board is refused before a VM is built (docs/85).
  emu::Options opts = emu::options();
  opts.board = "m5stack-atom-matrix";
  emu::set_options(opts);

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);

  static char manifest[256];
  static char source[8192];
  TEST_ASSERT_GREATER_THAN(
      0, read_host_file("apps/matrix/manifest.json", manifest, sizeof(manifest)));
  const size_t source_len =
      read_host_file("apps/matrix/matrix.lua", source, sizeof(source));
  TEST_ASSERT_GREATER_THAN(0, source_len);
  // A truncated read is a test that runs half an app and blames the app: the
  // buffer is bigger than the file and the read says how much it took (docs/80).
  TEST_ASSERT_LESS_THAN(sizeof(source) - 1, source_len);
  TEST_ASSERT_TRUE(add_app_file(storage, "matrix-app", manifest, source, "matrix.lua"));

  // It does not end: it is a wheel. The prompt comes back when the system's gesture
  // closes it, so this runs it and takes turns instead of waiting for it.
  pump_captured(40, g_out, sizeof(g_out), "run matrix-app");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "a colour wheel around 25 lights"), g_out);
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), "the wheel ended on its own");

  // The panel is painted light by light and it is not one colour: that is what makes
  // it the Matrix and not the Lite (docs/50). Every light has something to say —a
  // board that painted only the first one would leave twenty-four dark and only a
  // weaker test would believe it— and two lights a quarter of the wheel apart cannot
  // come out the same.
  uint8_t r0 = 0, g0 = 0, b0 = 0;
  for (uint32_t light = 0; light < 25; ++light) {
    uint8_t r = 0, g = 0, b = 0;
    TEST_ASSERT_TRUE(emu::led_pixel(light, r, g, b));
    TEST_ASSERT_TRUE_MESSAGE(r > 0 || g > 0 || b > 0,
                             "a light of the panel was never painted");
    if (light == 0) {
      r0 = r;
      g0 = g;
      b0 = b;
    }
  }
  uint8_t r12 = 0, g12 = 0, b12 = 0;
  TEST_ASSERT_TRUE(emu::led_pixel(12, r12, g12, b12));
  TEST_ASSERT_TRUE_MESSAGE(r0 != r12 || g0 != g12 || b0 != b12,
                           "the whole panel was painted with one colour");
  // The twenty-fifth light exists and the twenty-sixth does not: the count of the
  // identity is what sizes the panel (docs/50).
  TEST_ASSERT_TRUE(emu::led_pixel(24, r0, g0, b0));
  TEST_ASSERT_FALSE(emu::led_pixel(25, r0, g0, b0));

  // One press goes up a step, like the app of one light: the brightest channel of a
  // light of the panel is the step's value. One capture for the whole press, for the
  // same reason as `light` above.
  capture_begin();
  emu::press_button(true);
  pump_turns(20);
  emu::press_button(false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "brightness 4 of 20"), g_out);
  uint8_t r = 0, g = 0, b = 0;
  TEST_ASSERT_TRUE(emu::led_pixel(7, r, g, b));
  uint8_t bright = r;
  if (g > bright) bright = g;
  if (b > bright) bright = b;
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(4, bright, "the panel did not take the step");

  // Three taps are the way out, and a panel the app painted goes with it: **every**
  // light and not only the first one (docs/30, docs/75).
  capture_begin();
  for (int tap = 0; tap < 2; ++tap) {
    emu::press_button(true);
    pump_turns(10);
    emu::press_button(false);
    pump_turns(10);
  }
  capture_end(nullptr, 0);
  TEST_ASSERT_FALSE_MESSAGE(kernel::app_running(), "three taps did not close it");
  pump_captured(20, g_out, sizeof(g_out));
  for (uint32_t light = 0; light < 25; ++light) {
    TEST_ASSERT_TRUE(emu::led_pixel(light, r, g, b));
    TEST_ASSERT_TRUE_MESSAGE(r == 0 && g == 0 && b == 0,
                             "a light of the panel outlived its app");
  }

  storage->remove("/apps/matrix-app/matrix.lua");
  storage->remove("/apps/matrix-app/manifest.json");
  storage->remove("/apps/matrix-app");

  // An index the panel does not have is refused and said, instead of painting some
  // other square: a picture that lands one light off is a picture that lies
  // (docs/30).
  TEST_ASSERT_TRUE(add_app(storage, "offpanel",
                           "{\"title\":\"Off panel\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local ok, err = led.pixel(25, 1, 2, 3)\n"
                           "console.print('pixel 25 ' .. tostring(ok) .. ' ' .. "
                           "tostring(err and err.code))\n"));
  TEST_ASSERT_TRUE(run_captured("run offpanel", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "pixel 25 nil E_INVAL"), g_out);
  storage->remove("/apps/offpanel/app.lua");
  storage->remove("/apps/offpanel/manifest.json");
  storage->remove("/apps/offpanel");
}

// What makes an app an app **of this board**: the boards it lists, and the chip
// family, which every board of it accepts. The discard happens in `apps::load()`,
// and no PC can walk that branch because the bench discards nothing (docs/85), so
// the rule is asked here the way the kernel asks it (docs/40).
void test_an_app_belongs_to_the_boards_it_declares(void) {
  kernel::apps::Entry entry = {};
  strcpy(entry.boards[0], "esp32");
  entry.board_count = 1;
  TEST_ASSERT_TRUE(kernel::apps::compatible(entry, "m5stack-atom", "esp32"));
  TEST_ASSERT_TRUE(kernel::apps::compatible(entry, "m5stack-atom-matrix", "esp32"));
  TEST_ASSERT_FALSE(kernel::apps::compatible(entry, "emulator", "host"));

  // A board's own name is not a family: an app of the panel does not run on the
  // Lite, and a name that is not this board's family is not this board's.
  strcpy(entry.boards[0], "m5stack-atom-matrix");
  TEST_ASSERT_TRUE(kernel::apps::compatible(entry, "m5stack-atom-matrix", "esp32"));
  TEST_ASSERT_FALSE(kernel::apps::compatible(entry, "m5stack-atom", "esp32"));
  TEST_ASSERT_FALSE(kernel::apps::compatible(entry, "emulator", "host"));

  // No list is not "every board": an app that does not say where it runs runs
  // nowhere.
  entry.board_count = 0;
  TEST_ASSERT_FALSE(kernel::apps::compatible(entry, "m5stack-atom", "esp32"));
}

// A manifest that does not say which boards it runs on is not an app anywhere —the
// PC included—: "no list" is not "every board" (docs/40). The other half of the
// rule, a list that is not this board's, is the unit test above, because the bench
// discards nothing by the list (docs/85).
void test_a_manifest_without_boards_is_not_an_app(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "nowhere",
                           "{\"title\":\"Nowhere\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\"}",
                           "console.print('nowhere')\n"));

  kernel::apps::Entry entry = {};
  TEST_ASSERT_FALSE(kernel::apps::read("nowhere", entry));

  // And it is not offered to anybody: it is not in the set the shell walks.
  uint32_t seen = 0;
  kernel::apps::for_each(nullptr,
                         [](const kernel::apps::Entry& app, void* context) {
                           if (strcmp(app.id, "nowhere") == 0) {
                             ++*static_cast<uint32_t*>(context);
                           }
                           return true;
                         },
                         &seen);
  TEST_ASSERT_EQUAL_UINT32(0, seen);

  storage->remove("/apps/nowhere/app.lua");
  storage->remove("/apps/nowhere/manifest.json");
  storage->remove("/apps/nowhere");
}

// The prompt can become the interpreter: the whole API, one line at a time, with the
// kernel still turning between lines, and `exit` gives the shell back (docs/30).
void test_the_lua_command_opens_the_interpreter(void) {
  TEST_ASSERT_TRUE(boot_once());

  TEST_ASSERT_TRUE(feed_captured("lua", true, g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "one line at a time"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "lua> "), g_out);

  // A statement, an expression that answers, and a variable that is still there in
  // the next line: a prompt that forgets between lines is not a prompt.
  TEST_ASSERT_TRUE(feed_captured("x = 21", true, g_out, sizeof(g_out)));
  TEST_ASSERT_TRUE(feed_captured("x * 2", true, g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "42"), g_out);

  // The whole API and not a toy: the session has its own ground for files, and the
  // light of the board is there to be taken.
  TEST_ASSERT_TRUE(feed_captured("fs.data()", true, g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "/data/lua"), g_out);
  TEST_ASSERT_TRUE(feed_captured("led.set(0, 0, 255)", true, g_out, sizeof(g_out)));
  pump_captured(20, nullptr, 0);
  TEST_ASSERT_EQUAL_UINT8(0, emu::led_red());
  TEST_ASSERT_EQUAL_UINT8(0, emu::led_green());
  // 20 and not 255: the same ceiling an app finds, because it is the board's
  // (docs/30, docs/50).
  TEST_ASSERT_EQUAL_UINT8(20, emu::led_blue());

  // A mistake is one line and the session goes on: the state is not thrown away
  // because of a typo (docs/30).
  capture_begin();
  const bool typo = kernel::execute("this is not lua");
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_FALSE(typo);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "lua: "), g_out);
  TEST_ASSERT_TRUE(feed_captured("x + 1", true, g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "22"), g_out);

  // `exit` gives the prompt back, and the light the session took goes with it.
  TEST_ASSERT_TRUE(feed_captured("exit", true, g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "session closed"), g_out);
  pump_captured(20, g_out, sizeof(g_out));
  TEST_ASSERT_EQUAL_UINT8(0, emu::led_red() + emu::led_green() + emu::led_blue());
  TEST_ASSERT_TRUE(feed_captured("version", true, g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "lunokOS "), g_out);
}

// An app of a board this build is not is not hidden: `apps` lists it with what it
// needs, and `run` refuses it by name, says which boards it wants and which board this
// is. Losing an app without a word is exactly what this list is here to avoid
// (docs/40). The board named in the refusal is the one the bench was told to be —the
// ATOM here— and not the word "emulator", which is not a board (docs/85).
void test_an_app_for_another_board_is_listed_and_refused(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "elsewhere",
                           "{\"title\":\"Elsewhere\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"m5stack-core2\"]}",
                           "console.print('elsewhere')\n"));

  // Listed, with the board it wants.
  pump_captured(20, g_out, sizeof(g_out), "apps");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "elsewhere"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "[needs m5stack-core2]"), g_out);

  // And refused before a VM is built, saying both things. The refusal is the
  // command's own answer and not "the app ended": a run that never started has
  // ended too, and only one of the two is a failure worth reporting.
  capture_begin();
  const bool refused = kernel::execute("run elsewhere");
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_FALSE(refused);
  TEST_ASSERT_NOT_NULL_MESSAGE(
      strstr(g_out, "elsewhere runs on m5stack-core2 and this board is m5stack-atom"), g_out);
  TEST_ASSERT_FALSE(kernel::app_running());

  storage->remove("/apps/elsewhere/app.lua");
  storage->remove("/apps/elsewhere/manifest.json");
  storage->remove("/apps/elsewhere");
}

// The bench is not a board, and a manifest does not name it: the person says which one it
// stands in for and the bench answers that name, so an app runs here with the list it
// would carry on the glass — and an app of another board is refused, which is the half
// that proves the answer is the model and not "yes to everything" (docs/40, docs/85).
void test_the_bench_is_the_board_it_was_told_to_be(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "paperish",
                           "{\"title\":\"Paperish\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"m5stack-paper\"]}",
                           "console.print('from the paper')\n"));
  TEST_ASSERT_TRUE(add_app(storage, "panelish",
                           "{\"title\":\"Panelish\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"m5stack-atom-matrix\"]}",
                           "console.print('on the panel')\n"));

  // Told to be a Paper: its app is listed without a pending board, runs, and says so to
  // itself — `sys.board` is the model, and `sys.emulator` is still true (docs/50).
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  emu::set_options(opts);

  pump_captured(20, g_out, sizeof(g_out), "apps");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "paperish"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "[needs m5stack-paper"), g_out);
  // Listed with what it wants: the name, the title and then the boards it needs.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "panelish"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "[needs m5stack-atom-matrix]"), g_out);

  pump_captured(60, g_out, sizeof(g_out), "run paperish");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "from the paper"), g_out);
  TEST_ASSERT_FALSE_MESSAGE(kernel::app_running(), "the app never ended");

  // And the Matrix's app, on the same bench, is refused before a VM exists.
  capture_begin();
  const bool refused = kernel::execute("run panelish");
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_FALSE(refused);
  TEST_ASSERT_NOT_NULL_MESSAGE(
      strstr(g_out, "panelish runs on m5stack-atom-matrix and this board is m5stack-paper"),
      g_out);
  TEST_ASSERT_FALSE(kernel::app_running());

  storage->remove("/apps/paperish/app.lua");
  storage->remove("/apps/paperish/manifest.json");
  storage->remove("/apps/paperish");
  storage->remove("/apps/panelish/app.lua");
  storage->remove("/apps/panelish/manifest.json");
  storage->remove("/apps/panelish");
}

// Every app of the repository says which boards it runs on, and none of them names
// the bench: the bench is not a board, it is told which one it is, so writing
// `emulator` in a list is a name no board would ever answer (docs/40, docs/85).
// A manifest that forgot the list would still be caught here, because no list is not
// "every board" (docs/40).
void test_every_app_of_the_repository_declares_its_boards(void) {
  static const char* const kParents[] = {"", "../", "../../", "../../../"};
  static char text[1024];
  uint32_t seen = 0;
  for (size_t p = 0; p < sizeof(kParents) / sizeof(kParents[0]); ++p) {
    char dir_path[256];
    snprintf(dir_path, sizeof(dir_path), "%sapps", kParents[p]);
    DIR* dir = opendir(dir_path);
    if (dir == nullptr) continue;
    struct dirent* item = nullptr;
    while ((item = readdir(dir)) != nullptr) {
      if (item->d_name[0] == '.') continue;
      // Room for the longest path a test root can have plus the longest name: 256 was
      // not enough for the compiler to be sure, and a silent cut would open the wrong file.
      char manifest[560];
      snprintf(manifest, sizeof(manifest), "%s/%s/manifest.json", dir_path, item->d_name);
      FILE* file = fopen(manifest, "rb");
      if (file == nullptr) continue;
      const size_t got = fread(text, 1, sizeof(text) - 1, file);
      fclose(file);
      text[got] = '\0';
      char boards[kernel::apps::Entry::kMaxBoards][lunok::json::kStringItemCap];
      uint32_t count = 0;
      TEST_ASSERT_TRUE_MESSAGE(
          lunok::json::get_strings(text, got, "boards", boards,
                                   kernel::apps::Entry::kMaxBoards, count),
          item->d_name);
      TEST_ASSERT_TRUE_MESSAGE(count > 0, item->d_name);
      for (uint32_t i = 0; i < count; ++i) {
        TEST_ASSERT_TRUE_MESSAGE(strcmp(boards[i], "emulator") != 0, item->d_name);
      }
      ++seen;
    }
    closedir(dir);
    break;
  }
  // Eighteen apps ship today: a directory that was not found would pass every
  // check above by not running any of them.
  TEST_ASSERT_TRUE_MESSAGE(seen >= 17, "the apps of the repository were not found");
}

// The store answers how full it is in **entries**, which is the number that says whether the
// next alarm fits: the NVS is not bytes. On the PC there is no ceiling —a key is a file— and
// the total says so by being zero (docs/70).
void test_the_store_says_how_full_it_is(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IKeyValue* kv = hal::platform().nvs();
  TEST_ASSERT_NOT_NULL(kv);

  uint32_t before_used = 0;
  uint32_t before_total = 0;
  TEST_ASSERT_TRUE(kv->stats(before_used, before_total));

  TEST_ASSERT_TRUE(kv->set("prova", "clau", "valor", 5));
  uint32_t used = 0;
  uint32_t total = 0;
  TEST_ASSERT_TRUE(kv->stats(used, total));
  TEST_ASSERT_EQUAL_UINT32(before_used + 1, used);
  TEST_ASSERT_EQUAL_UINT32(0, total);

  // Erasing it takes the entry away, and the count goes back: a number that only grows
  // would make the next person think the store is fuller than it is.
  TEST_ASSERT_TRUE(kv->erase("prova", "clau"));
  TEST_ASSERT_TRUE(kv->stats(used, total));
  TEST_ASSERT_EQUAL_UINT32(before_used, used);
}

// A file is read whatever its size: the system moves a fixed window and the app gets the
// bytes it asked for. The only limit is the interpreter's memory, and a line longer than a
// window comes out whole instead of being cut at 256 bytes (docs/30, docs/70).
void test_a_file_is_read_whatever_its_size(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "sizer",
                           "{\"title\":\"Sizer\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local path = fs.data('long.txt')\n"
                           "assert(fs.write(path, string.rep('a', 15000)))\n"
                           "console.print('whole ' .. #fs.read(path))\n"
                           "console.print('prefix ' .. #fs.read(path, 1000))\n"
                           "console.print('at ' .. #fs.read_at(path, 14000, 1000))\n"
                           "local file = fs.open(path, 'r')\n"
                           "local total = 0\n"
                           "for i = 1, 3 do total = total + #file:read(4000) end\n"
                           "total = total + #file:read(3000)\n"
                           "file:close()\n"
                           "console.print('windows ' .. total)\n"
                           "local lines = fs.data('lines.txt')\n"
                           "assert(fs.write(lines, string.rep('b', 1000) .. '\\n' .. 'short'))\n"
                           "local lf = fs.open(lines, 'r')\n"
                           "console.print('line ' .. #lf:read('*l') .. ' ' .. lf:read('*l'))\n"
                           "lf:close()\n"));

  TEST_ASSERT_TRUE(run_captured("run sizer", g_out, sizeof(g_out)));
  // 15 000 B in one call: the old 4 096 ceiling would have refused it.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "whole 15000"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "prefix 1000"), g_out);
  // A window far past anything the old ceiling allowed.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "at 1000"), g_out);
  // The same file through the file object: 3 x 4 000 + 3 000.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "windows 15000"), g_out);
  // A 1 000 byte line and the one after it, which is the seek back in action.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "line 1000 short"), g_out);

  storage->remove("data/sizer/long.txt");
  storage->remove("data/sizer/lines.txt");
  storage->remove("data/sizer");
  storage->remove("/apps/sizer/app.lua");
  storage->remove("/apps/sizer/manifest.json");
  storage->remove("/apps/sizer");
}

// A press reaches an app that is waiting for it, as a promise, and the answer says
// what kind of press it was (docs/30).
void test_a_press_reaches_the_app_as_a_promise(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "waiter",
                            "{\"title\":\"Waiter\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                            "console.print('waiting for a press')\n"
                            "local press = button.wait(5000):await()\n"
                            "console.print('got ' .. press.kind .. ' ' .. press.held_ms)\n"));

  pump_captured(60, g_out, sizeof(g_out), "run waiter");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "waiting for a press"), g_out);
  TEST_ASSERT_TRUE(kernel::app_running());

  emu::press_button(true);
  pump_captured(60, g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "got press 0"), g_out);
  emu::press_button(false);
  pump_captured(20, g_out, sizeof(g_out));
  TEST_ASSERT_FALSE_MESSAGE(kernel::app_running(), "the app never ended");

  storage->remove("/apps/waiter/app.lua");
  storage->remove("/apps/waiter/manifest.json");
  storage->remove("/apps/waiter");
}

// **A button is a control a person can reach without a mouse**: the first one takes the focus,
// the dial's up and down walk the pressable controls, the centre clicks, and a finger does the same
// through the touch panel. The app only says what a click means; the system owns the focus and the
// press state (docs/30, docs/90).
void test_a_button_is_navigated_and_touched(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  opts.touch = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());
  TEST_ASSERT_NOT_NULL(hal::platform().touch());
  TEST_ASSERT_TRUE(kernel::touch::boot(hal::platform()));

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "nav",
                           "{\"title\":\"Nav\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local function make(y, text)\n"
                           "  local b, e = ui.button(200, 60)\n"
                           "  if not b then console.print('no button ' .. tostring(e)); return nil end\n"
                           "  b:place(10, y)\n"
                           "  local l = ui.label(180, 40)\n"
                           "  l.text = text l:place(10, 10) b:add(l)\n"
                           "  b.on_focus = function(self, on)\n"
                           "    console.print('focus ' .. text .. ' ' .. tostring(on))\n"
                           "  end\n"
                           "  b.on_click = function(self) console.print('click ' .. text) end\n"
                           "  return b\n"
                           "end\n"
                           "local first = make(10, 'one')\n"
                           "local second = make(80, 'two')\n"
                           "local plain = ui.control(200, 40)\n"
                           "plain:place(10, 150)\n"
                           "plain.focusable = true\n"
                           "plain.on_focus = function(self, on)\n"
                           "  console.print('focus plain ' .. tostring(on))\n"
                           "end\n"
                           "plain.on_activate = function(self) console.print('activate plain') end\n"
                           "console.print('initial ' .. tostring(first and first.focused) .. ' ' ..\n"
                           "  tostring(second and second.focused))\n"
                           "ui.present()\n"
                           "while true do\n"
                           "  local f = touch.read()\n"
                           "  if #f > 0 then console.print('finger ' .. f[1].x .. ',' .. f[1].y) end\n"
                           "  sys.delay(20):await()\n"
                           "end\n"));

  pump_captured(20, g_out, sizeof(g_out), "run nav");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "initial true false"), g_out);

  // The down key is the next control. It is key 2 on the Paper and the system reads the spot, not
  // the number; the old focus loses the ring and the new one gets it (docs/30, docs/50).
  capture_begin();
  emu::press_key(2, true);
  pump_turns(20);
  emu::press_key(2, false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "focus one false"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "focus two true"), g_out);

  // The centre is Enter: the focused button is clicked.
  capture_begin();
  emu::press_button(true);
  pump_turns(20);
  emu::press_button(false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "click two"), g_out);

  // **A control that is pressable without being a button is in the same walk**: `focusable` is what
  // makes it a stop, and `on_activate` is its click (docs/30, docs/90).
  capture_begin();
  emu::press_key(2, true);
  pump_turns(20);
  emu::press_key(2, false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "focus plain true"), g_out);
  capture_begin();
  emu::press_button(true);
  pump_turns(20);
  emu::press_button(false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "activate plain"), g_out);

  // **A finger is the same event**: the first button is at 10,10 and is 200x60, so 30,30 in the
  // view is on it. The panel counts on the glass, which is the view shifted by its place, and down
  // holds the button while up clicks it (docs/30, docs/50).
  emu::Gt911Panel* panel = emu::touch_panel();
  TEST_ASSERT_NOT_NULL(panel);
  const kernel::ui::Canvas& view = kernel::ui::canvas();
  const uint16_t finger_x = static_cast<uint16_t>(view.x + 30);
  const uint16_t finger_y = static_cast<uint16_t>(view.y + 30);
  capture_begin();
  const bool finger_down = panel->finger_down(0, finger_x, finger_y);
  hal::TouchPoint point = {};
  bool seen = false;
  for (int i = 0; i < 20 && !seen; ++i) {
    pump_turns(5);
    seen = kernel::touch::points(&point, 1) > 0;
  }
  const bool finger_up = panel->finger_up(0);
  pump_turns(60);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_TRUE(finger_down);
  TEST_ASSERT_TRUE_MESSAGE(seen, "the panel never gave the frame to the kernel");
  TEST_ASSERT_EQUAL_UINT16(30, point.x);
  TEST_ASSERT_EQUAL_UINT16(30, point.y);
  TEST_ASSERT_TRUE(finger_up);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "focus one true"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "click one"), g_out);

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());

  storage->remove("/apps/nav/app.lua");
  storage->remove("/apps/nav/manifest.json");
  storage->remove("/apps/nav");

  // The next test gets the default bench back: the display and the panel were this test's.
  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  opts.touch = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **The quality an app asks for at the present is the quality of the paint** (docs/30, docs/50): a
// page that must look uniform asks for `gc16` once, and the controls —which ask for `du` on their own—
// do not leave the ghost of what moved. The mode was read from a stack the root had been pushed on,
// so every `ui.present('gc16')` was a present with no mode (found on the bench, 3/11).
void test_a_present_with_a_mode_paints_with_that_mode(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "washed",
                            "{\"title\":\"Washed\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                            "ui.clear(15, 'du')\n"
                            "local ok = ui.present('du4')\n"
                            "console.print('washed ' .. tostring(ok))\n"
                            "sys.delay(5000):await()\n"));
  emu::MemorySurface* glass = emu::memory_surface();
  TEST_ASSERT_NOT_NULL(glass);

  pump_captured(40, g_out, sizeof(g_out), "run washed");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "washed true"), g_out);
  // **One of the last paints is the app's present**: the clear was drawn with `du`, and a present that
  // lost the mode paints that `du` and says nothing. `du4` is not a quality the bar asks for, so a
  // paint of the bar after the app cannot pass for the app's present.
  bool asked = false;
  for (uint32_t i = 0; i < emu::MemorySurface::kMaxPaints; ++i) {
    const emu::MemorySurface::Paint* paint = glass->paint_at(glass->paint_count() - 1 - i);
    if (paint != nullptr && paint->mode == hal::UpdateMode::kDu4) asked = true;
  }
  TEST_ASSERT_TRUE_MESSAGE(asked, "the present with a mode painted with the mode of the controls");

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  storage->remove("/apps/washed/app.lua");
  storage->remove("/apps/washed/manifest.json");
  storage->remove("/apps/washed");

  // The next test gets the default bench back: the display was this test's.
  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// An app that declared on_button gets the press there instead, called between
// turns like on_alarm (docs/30).
void test_a_press_reaches_the_app_as_a_hook(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "tapper",
                            "{\"title\":\"Tapper\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                            "function on_button(kind, held)\n"
                            "  console.print('hook ' .. kind .. ' ' .. held)\n"
                            "end\n"
                            "console.print('tapper ready')\n"
                            "sys.delay(3000):await()\n"));

  pump_captured(60, g_out, sizeof(g_out), "run tapper");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "tapper ready"), g_out);

  emu::press_button(true);
  pump_captured(20, g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "hook press 0"), g_out);
  emu::press_button(false);
  pump_captured(20, g_out, sizeof(g_out));

  storage->remove("/apps/tapper/app.lua");
  storage->remove("/apps/tapper/manifest.json");
  storage->remove("/apps/tapper");
  // The app is still waiting for its three seconds: closing it here keeps the rest
  // of the suite from inheriting a live interpreter (`!!exit` is a line of the
  // keyboard and not a command, docs/30).
  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
}

// Three taps in a row close what is in front: the system keeps that gesture for
// itself, so an app cannot swallow it (docs/30).
void test_three_taps_close_the_app(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "idle",
                            "{\"title\":\"Idle\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                            "console.print('idle here')\n"
                            "sys.delay(30000):await()\n"));

  pump_captured(60, g_out, sizeof(g_out), "run idle");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "idle here"), g_out);
  TEST_ASSERT_TRUE(kernel::app_running());

  // One capture around the three taps: each press takes its own turns and what is
  // said has to be read together, or the last capture would be the only one left.
  capture_begin();
  for (int tap = 0; tap < 3; ++tap) {
    emu::press_button(true);
    pump_turns(10);
    emu::press_button(false);
    pump_turns(10);
  }
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "button: three taps, closing the app"), g_out);
  TEST_ASSERT_FALSE_MESSAGE(kernel::app_running(), "the app survived three taps");

  storage->remove("/apps/idle/app.lua");
  storage->remove("/apps/idle/manifest.json");
  storage->remove("/apps/idle");
}

// The speed of the processor is part of the same decision as the sleep: it is read,
// it moves between the steps the board has, and a step it does not have is refused
// instead of rounded (docs/50).
void test_the_power_command_moves_the_cpu(void) {
  TEST_ASSERT_TRUE(boot_once());
  pump_captured(20, g_out, sizeof(g_out), "power");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "cpu: 160 MHz"), g_out);

  pump_captured(20, g_out, sizeof(g_out), "power cpu 240");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "cpu: 240 MHz"), g_out);

  // A speed between the steps is refused, and the number is said back: a rounded
  // speed would be a lie about what it costs.
  pump_captured(20, g_out, sizeof(g_out), "power cpu 200");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "cpu: this board does not run at 200 MHz"), g_out);
  pump_captured(20, g_out, sizeof(g_out), "power cpu");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "cpu: 240 MHz"), g_out);

  // Back where it booted, so the rest of the suite runs at the same speed.
  pump_captured(20, g_out, sizeof(g_out), "power cpu 160");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "cpu: 160 MHz"), g_out);
}

// An app reads the speed, moves it, and gets a refusal it can catch (docs/30).
// The Paper has three keys and the system says **which** one moved: the promise
// and the hook both carry the key, or an app could not tell its way up from its
// way down (docs/30, docs/50).
void test_a_key_reaches_the_app_with_its_number(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "thirdkey",
                           "{\"title\":\"Third key\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "function on_button(kind, held, key)\n"
                           "  console.print('press ' .. kind .. ' key ' .. key)\n"
                           "end\n"
                           "console.print('keys ' .. button.count())\n"
                           "while true do sys.delay(50):await() end\n"));

  // Three keys is the Paper's dial; an ATOM has one button and would print 1 (docs/50).
  use_board("m5stack-paper");
  pump_captured(20, g_out, sizeof(g_out), "run thirdkey");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "keys 3"), g_out);

  // The right-hand key, which is number 2, and not the main one.
  capture_begin();
  emu::press_key(2, true);
  pump_turns(20);
  emu::press_key(2, false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "press press key 2"), g_out);

  // And the way out is still three taps of the middle one.
  capture_begin();
  for (int tap = 0; tap < 3 && kernel::app_running(); ++tap) {
    emu::press_button(true);
    pump_turns(10);
    emu::press_button(false);
    pump_turns(10);
  }
  capture_end(nullptr, 0);
  TEST_ASSERT_FALSE_MESSAGE(kernel::app_running(), "three taps of the middle key did not close it");

  storage->remove("/apps/thirdkey/app.lua");
  storage->remove("/apps/thirdkey/manifest.json");
  storage->remove("/apps/thirdkey");
}

// Three taps of a side key are a menu and not a way out: the gesture belongs to
// the main key, and an app that needs the sides keeps them (docs/30).
void test_three_taps_of_a_side_key_keep_the_app(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "idleside",
                           "{\"title\":\"Idle side\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "console.print('idle here')\n"
                           "while true do sys.delay(50):await() end\n"));
  pump_captured(20, g_out, sizeof(g_out), "run idleside");
  TEST_ASSERT_TRUE(kernel::app_running());

  capture_begin();
  for (int tap = 0; tap < 3; ++tap) {
    emu::press_key(1, true);
    pump_turns(10);
    emu::press_key(1, false);
    pump_turns(10);
  }
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), "a side key closed the app");
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "closing the app"), g_out);

  capture_begin();
  for (int tap = 0; tap < 3 && kernel::app_running(); ++tap) {
    emu::press_button(true);
    pump_turns(10);
    emu::press_button(false);
    pump_turns(10);
  }
  capture_end(nullptr, 0);
  TEST_ASSERT_FALSE_MESSAGE(kernel::app_running(), "the middle key stopped closing it");

  storage->remove("/apps/idleside/app.lua");
  storage->remove("/apps/idleside/manifest.json");
  storage->remove("/apps/idleside");
}

// **The dial reaches the callbacks of a page of controls**: the centre presses the control that has the
// focus, a side key walks to the next one, and `on_click` and `on_activate` are the two names a press can
// arrive with (docs/30, docs/90). It is the whole chain —the key, the focus, the hook and the label that
// changes— and the app ends itself when it has counted two presses, which is how the suite knows it ran.
void test_the_dial_walks_the_buttons_of_a_page(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "dial",
                           "{\"title\":\"Dial\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local presses = 0\n"
                           "local done = false\n"
                           "local function make(name, x, how)\n"
                           "  local b = ui.button(200, 80)\n"
                           "  b:place(x, 100)\n"
                           "  local l = ui.label(180, 60)\n"
                           "  l.text = name\n"
                           "  l.align = 'center'\n"
                           "  l.valign = 'middle'\n"
                           "  l:place(10, 10)\n"
                           "  b:add(l)\n"
                           "  local hook = function(self)\n"
                           "    presses = presses + 1\n"
                           "    l.text = name .. ' ' .. presses\n"
                           "    console.print('press ' .. name .. ' ' .. presses)\n"
                           "    if presses >= 2 then done = true end\n"
                           "  end\n"
                           "  if how == 'activate' then b.on_activate = hook else b.on_click = hook end\n"
                           "  b.on_focus = function(self, focused)\n"
                           "    if focused then console.print('focus ' .. name) end\n"
                           "  end\n"
                           "end\n"
                           "make('one', 20, 'click')\n"
                           "make('two', 260, 'activate')\n"
                           "ui.present()\n"
                           "while not done do sys.delay(10):await() end\n"
                           "console.print('presses ' .. presses)\n"));

  pump_captured(20, g_out, sizeof(g_out), "run dial");
  capture_begin();
  pump_turns(20);
  // The centre presses the control that has the focus, and the first one is the one the system focused when
  // it appeared.
  emu::press_button(true);
  pump_turns(20);
  emu::press_button(false);
  pump_turns(20);
  // A side key walks the focus to the next control and the centre presses that one, which answers
  // `on_activate` because that is the name it declared.
  emu::press_key(2, true);
  pump_turns(20);
  emu::press_key(2, false);
  pump_turns(20);
  emu::press_button(true);
  pump_turns(20);
  emu::press_button(false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "press one 1"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "focus two"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "press two 2"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "presses 2"), g_out);
  TEST_ASSERT_FALSE_MESSAGE(kernel::app_running(), "the app did not end after its two presses");

  storage->remove("/apps/dial/app.lua");
  storage->remove("/apps/dial/manifest.json");
  storage->remove("/apps/dial");
}

// **A checkbox's click means one thing and the system does it**: the centre flips the box, the field is already
// flipped when the app hears about it, and the words beside the box are the app's (`on_change` changes them). It
// is the whole chain —the key, the focus, the field the system writes and the callback that reads it— and the
// app ends itself when it has counted two flips, which is how the suite knows it ran (docs/30, docs/90).
void test_the_dial_flips_a_checkbox(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "tickbox",
                           "{\"title\":\"Tick\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local flips = 0\n"
                           "local box = ui.checkbox(300, 40)\n"
                           "box:place(20, 100)\n"
                           "local l = ui.label(240, 32)\n"
                           "l.text = 'off'\n"
                           "l.valign = 'middle'\n"
                           "l:place(box:box() + 8, 4)\n"
                           "box:add(l)\n"
                           "box.on_change = function(self, on)\n"
                           "  flips = flips + 1\n"
                           "  l.text = on and 'on' or 'off'\n"
                           "  console.print('change ' .. tostring(on) .. ' ' .. tostring(self.checked))\n"
                           "end\n"
                           "console.print('box ' .. box:box())\n"
                           "ui.present()\n"
                           "while flips < 2 do sys.delay(10):await() end\n"
                           "console.print('flips ' .. flips)\n"));

  pump_captured(20, g_out, sizeof(g_out), "run tickbox");
  // **These lines are printed before the capture opens**, so they are read from the output of the run itself
  // (`pump_captured`) and not from the window the rest of the test reads (docs/85).
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "box 32"), g_out);
  capture_begin();
  pump_turns(20);
  // The centre activates the control that has the focus, and the first one is the one the system focused when
  // it appeared. Twice, because the second flip is what proves the box goes back.
  emu::press_button(true);
  pump_turns(20);
  emu::press_button(false);
  pump_turns(20);
  emu::press_button(true);
  pump_turns(20);
  emu::press_button(false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  // **A row of forty holds a box of thirty-two**, and that is the number an app places its words with.
  // The field was already written when the app was told, which is the whole point of the system doing it.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "change true true"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "change false false"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "flips 2"), g_out);
  TEST_ASSERT_FALSE_MESSAGE(kernel::app_running(), "the app did not end after its two flips");

  storage->remove("/apps/tickbox/app.lua");
  storage->remove("/apps/tickbox/manifest.json");
  storage->remove("/apps/tickbox");
}

// **The arrows move the number of a slider and the walk of the dial stays where the person put it**: the value
// is the control's own state, so the system changes it and the app is told with `on_change(self, value)` —the
// same rule a checkbox's check follows—, and at the end of the range the key goes back to the walk so a page
// with a slider is not a dead end (docs/30, docs/90).
void test_the_dial_moves_the_value_of_a_slider(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "volume",
                           "{\"title\":\"Volume\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local changes = 0\n"
                           "local s = ui.slider(300, 48)\n"
                           "s:place(20, 100)\n"
                           "s.value = 0\n"
                           "s.max = 100\n"
                           "s.step = 10\n"
                           "s.suffix = '%'\n"
                           "s.on_change = function(self, value)\n"
                           "  changes = changes + 1\n"
                           "  console.print('change ' .. value .. ' of ' .. self.value)\n"
                           "end\n"
                           "ui.present()\n"
                           "while changes < 3 do sys.delay(10):await() end\n"
                           "console.print('changes ' .. changes .. ' value ' .. s.value)\n"));

  pump_captured(20, g_out, sizeof(g_out), "run volume");
  capture_begin();
  pump_turns(20);
  // Down twice and up once —the bench has the Paper's dial, where key 2 is down—: +10, +20 and back to 10,
  // which is what proves both directions and not only that a number changed.
  for (int i = 0; i < 3; ++i) {
    emu::press_key((i < 2) ? 2 : 1, true);
    pump_turns(20);
    emu::press_key((i < 2) ? 2 : 1, false);
    pump_turns(20);
  }
  capture_end(g_out, sizeof(g_out));
  // **The field was already written when the app was told**, which is the whole point of the system doing it.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "change 10 of 10"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "change 20 of 20"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "changes 3 value 10"), g_out);
  TEST_ASSERT_FALSE_MESSAGE(kernel::app_running(), "the app did not end after its three changes");

  storage->remove("/apps/volume/app.lua");
  storage->remove("/apps/volume/manifest.json");
  storage->remove("/apps/volume");
}


// **A page is a declaration**: the panel gives each child its frame, so an app says the order and the spaces and
// never a coordinate —and the arrows of the dial change the chosen tab of a strip while it has the focus, which
// is the same rule the slider follows— (docs/30, docs/90).
void test_a_panel_arranges_the_page_and_the_dial_changes_the_tab(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "pagepanel",
                           "{\"title\":\"Page\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local tabs = ui.tabs(300, 40)\n"
                           "tabs:place(20, 20)\n"
                           "tabs.items = {'One', 'Two', 'Three'}\n"
                           "tabs.index = 1\n"
                           "local changes = 0\n"
                           "tabs.on_change = function(self, index)\n"
                           "  changes = changes + 1\n"
                           "  console.print('tab ' .. index)\n"
                           "end\n"
                           "local p = ui.panel(300, 120, 'vertical')\n"
                           "p:place(20, 100)\n"
                           "p.padding = 10\n"
                           "p.gap = 4\n"
                           "local a = ui.label(10, 20)\n"
                           "a.text = 'first'\n"
                           "local b = ui.label(10, 20)\n"
                           "b.text = 'second'\n"
                           "p:add(a)\n"
                           "p:add(b)\n"
                           "ui.present()\n"
                           "local ca = a:caps()\n"
                           "local cb = b:caps()\n"
                           "console.print('a ' .. ca.y .. ' ' .. ca.width)\n"
                           "console.print('b ' .. cb.y .. ' ' .. cb.width)\n"
                           "while changes < 1 do sys.delay(10):await() end\n"
                           "console.print('tab now ' .. tabs.index)\n"));

  pump_captured(20, g_out, sizeof(g_out), "run pagepanel");
  // **These lines are printed before the capture opens**, so they are read from the output of the run itself
  // (`pump_captured`) and not from the window the rest of the test reads (docs/85).
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "a 10 280"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "b 34 280"), g_out);
  capture_begin();
  pump_turns(20);
  // Key 2 is down on the bench's dial, which is the next tab of the strip.
  emu::press_key(2, true);
  pump_turns(20);
  emu::press_key(2, false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  // **Ten pixels of padding and four of gap**, and the width of the panel less the two paddings: the app asked
  // for a column, not for two places.
  // The strip moved to its second tab and the app was told. **`tab 2` is read from the capture that holds the
  // press**, because the app is asleep in its wait until the key comes and prints nothing before it.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "tab 2"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "tab now 2"), g_out);
  TEST_ASSERT_FALSE_MESSAGE(kernel::app_running(), "the app did not end after its one change");

  storage->remove("/apps/pagepanel/app.lua");
  storage->remove("/apps/pagepanel/manifest.json");
  storage->remove("/apps/pagepanel");
}

// **A list turns its page with the arrows and the app hears it at `on_page`**, which is the rule a slider and a
// strip of tabs already follow: the page belongs to the control and the numbers come from the same arithmetic
// the drawing uses (docs/30, docs/90).
void test_a_list_turns_its_page_with_the_dial(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "pager",
                           "{\"title\":\"Pager\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local turns = 0\n"
                           "local l = ui.list(300, 200)\n"
                           "l:place(20, 60)\n"
                           "l.row_height = 56\n"
                           "l.items = {}\n"
                           "for i = 1, 10 do\n"
                           "  l.items[i] = { title = 'Item ' .. i, detail = tostring(i) }\n"
                           "end\n"
                           "l.on_page = function(self, page)\n"
                           "  turns = turns + 1\n"
                           "  console.print('page ' .. page)\n"
                           "end\n"
                           "ui.present()\n"
                           "while turns < 1 do sys.delay(10):await() end\n"
                           "console.print('page now ' .. l.page)\n"));

  pump_captured(20, g_out, sizeof(g_out), "run pager");
  capture_begin();
  pump_turns(20);
  emu::press_key(2, true);
  pump_turns(20);
  emu::press_key(2, false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  // Three rows of ten in two hundred pixels with the footer of the paging: the second page is the second press.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "page 2"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "page now 2"), g_out);
  TEST_ASSERT_FALSE_MESSAGE(kernel::app_running(), "the app did not end after its one turn");

  storage->remove("/apps/pager/app.lua");
  storage->remove("/apps/pager/manifest.json");
  storage->remove("/apps/pager");
}

// **The keyboard is the system's and types into the box it was given**: the app says the box, shows the band and
// is told what the box holds after every key —the field is already written, the rule a checkbox and a slider
// follow— and **the dial walks the keys** so a board with three keys can type too (docs/30, docs/90).
void test_the_keyboard_types_into_the_box_it_was_given(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "typer",
                           "{\"title\":\"Typer\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local changes = 0\n"
                           "local box = ui.textbox(300, 48)\n"
                           "box:place(20, 60)\n"
                           "box.cursor = 0\n"
                           "local kb = ui.keyboard()\n"
                           "kb.target = box\n"
                           "kb:show('alpha')\n"
                           "console.print('band ' .. kb:height())\n"
                           "box.on_change = function(self, text)\n"
                           "  changes = changes + 1\n"
                           "  console.print('typed ' .. text .. ' at ' .. self.cursor)\n"
                           "end\n"
                           "ui.present('gc16')\n"
                           "while changes < 3 do sys.delay(10):await() end\n"
                           "console.print('word ' .. box.text)\n"));

  pump_captured(20, g_out, sizeof(g_out), "run typer");
  // **These lines are printed before the capture opens**, so they are read from the output of the run itself
  // (`pump_captured`) and not from the window the rest of the test reads (docs/85).
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "band 176"), g_out);
  capture_begin();
  pump_turns(20);
  // The centre presses the key the dial is on —the first one of the first row, which is a `q`— twice, then the
  // down key of the dial walks to the next key of the layout, which is the `w` of the same row, and the centre
  // types it. **The keyboard is lowercase until the shift says otherwise**, so the letters arrive as they are
  // drawn (0.83.0, docs/30, docs/90).
  for (int i = 0; i < 2; ++i) {
    emu::press_button(true);
    pump_turns(20);
    emu::press_button(false);
    pump_turns(20);
  }
  emu::press_key(2, true);
  pump_turns(20);
  emu::press_key(2, false);
  pump_turns(20);
  emu::press_button(true);
  pump_turns(20);
  emu::press_button(false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  // Four rows of a key of forty-four pixels in a view of the Paper: the band is a hundred and seventy-six.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "typed q at 1"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "typed qq at 2"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "typed qqw at 3"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "word qqw"), g_out);
  TEST_ASSERT_FALSE_MESSAGE(kernel::app_running(), "the app did not end after its three keys");

  storage->remove("/apps/typer/app.lua");
  storage->remove("/apps/typer/manifest.json");
  storage->remove("/apps/typer");
}

// **The band of the keyboard comes out of the view the app is given** (0.84.0): `ui.caps().height` is seven hundred and
// thirty-six with the keyboard up on the Paper and nine hundred and twelve without it, `kb:height()` still says the band,
// and the app hears `on_resize` —the hook that already means "the view is another one"— when it comes and when it goes.
// An app that subtracts the band by hand would take it out twice, which is why the two demos that open the keyboard say
// the size of the system and not their own (docs/30, docs/90).
void test_the_keyboard_takes_its_band_out_of_the_view_the_app_is_given(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "resizer",
                           "{\"title\":\"Resizer\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local box = ui.textbox(300, 48)\n"
                           "box:place(20, 60)\n"
                           "local kb = ui.keyboard()\n"
                           "kb.target = box\n"
                           "console.print('view ' .. ui.caps().height)\n"
                           "kb:show('alpha')\n"
                           "console.print('band ' .. kb:height() .. ', view ' .. ui.caps().height)\n"
                           "kb:hide()\n"
                           "console.print('gone, view ' .. ui.caps().height)\n"
                           "function on_resize(w, h)\n"
                           "  console.print('resize ' .. w .. 'x' .. h)\n"
                           "end\n"
                           "kb:show('numeric')\n"
                           "console.print('again, view ' .. ui.caps().height)\n"));

  pump_captured(60, g_out, sizeof(g_out), "run resizer");
  // The page of the Paper is the glass under the bar of the system: five hundred and forty by nine hundred and twelve.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "view 912"), g_out);
  // With the band up, the four rows of forty-four are the system's and the app has seven hundred and thirty-six.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "band 176, view 736"), g_out);
  // And the band going gives them all back, without a word from the app: the hook was not defined yet at that point.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "gone, view 912"), g_out);
  // The show that comes after it finds the hook and tells it the view is another one.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "resize 540x736"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "again, view 736"), g_out);
  TEST_ASSERT_FALSE_MESSAGE(kernel::app_running(), "the app did not end after its keys");

  storage->remove("/apps/resizer/app.lua");
  storage->remove("/apps/resizer/manifest.json");
  storage->remove("/apps/resizer");
}

// **A dialog has two doors and the dial picks one before it presses it**: the chosen door moves with the arrows and
// the centre fires `on_accept` or `on_cancel`, and closing the card is the app's —the old rule: read the action,
// close, and then run it— (docs/30, docs/90).
void test_a_dialog_answers_with_the_door_the_dial_is_on(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "asker",
                           "{\"title\":\"Asker\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local answers = 0\n"
                           "local card = ui.dialog(380, 260)\n"
                           "card:place(80, 200)\n"
                           "card.title = 'Delete?'\n"
                           "card.text = 'This card deletes every note you wrote'\n"
                           "card.accept = 'Delete'\n"
                           "card.cancel = 'Keep'\n"
                           "card.emphasis = 1\n"
                           "card.on_accept = function(self)\n"
                           "  answers = answers + 1\n"
                           "  console.print('accept at ' .. self.choice)\n"
                           "end\n"
                           "card.on_cancel = function(self)\n"
                           "  answers = answers + 1\n"
                           "  console.print('cancel at ' .. self.choice)\n"
                           "end\n"
                           "ui.present('gc16')\n"
                           "while answers < 2 do sys.delay(10):await() end\n"
                           "console.print('answers ' .. answers)"));

  pump_captured(20, g_out, sizeof(g_out), "run asker");
  capture_begin();
  pump_turns(20);
  // The card opens on the accept; one step back puts the ring on the cancel and the centre takes it.
  emu::press_key(1, true);
  pump_turns(20);
  emu::press_key(1, false);
  pump_turns(20);
  emu::press_button(true);
  pump_turns(20);
  emu::press_button(false);
  pump_turns(20);
  // And forward again to the accept, which is the second answer.
  emu::press_key(2, true);
  pump_turns(20);
  emu::press_key(2, false);
  pump_turns(20);
  emu::press_button(true);
  pump_turns(20);
  emu::press_button(false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "cancel at 1"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "accept at 2"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "answers 2"), g_out);

  storage->remove("/apps/asker/app.lua");
  storage->remove("/apps/asker/manifest.json");
  storage->remove("/apps/asker");
}

// **A menu walks the options that are on the card and answers with the one the dial or the finger is on**: the option
// the app hid is never reached, a finger takes the row it pressed and not the one the ring was on, and the app hears
// `on_pick(self, index)` (docs/30, docs/90).
void test_a_menu_skips_the_option_the_app_hid(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  opts.touch = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());
  TEST_ASSERT_NOT_NULL(hal::platform().touch());
  TEST_ASSERT_TRUE(kernel::touch::boot(hal::platform()));

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "picker",
                           "{\"title\":\"Picker\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local picks = 0\n"
                           "local card = ui.menu(300, 200)\n"
                           "card:place(80, 200)\n"
                           "card.items = { 'Copy', { label = 'Paste', hidden = true },\n"
                           "  { label = 'Delete', primary = true } }\n"
                           "card.on_pick = function(self, index)\n"
                           "  picks = picks + 1\n"
                           "  console.print('pick ' .. index .. ' at ' .. picks)\n"
                           "end\n"
                           "ui.present('gc16')\n"
                           "while picks < 3 do sys.delay(10):await() end\n"
                           "console.print('picks ' .. picks)"));

  pump_captured(20, g_out, sizeof(g_out), "run picker");
  capture_begin();
  pump_turns(20);
  // **One step down and the hidden option is skipped**: the app answers with the third, so the walk never landed on
  // the second.
  emu::press_key(2, true);
  pump_turns(20);
  emu::press_key(2, false);
  pump_turns(20);
  emu::press_button(true);
  pump_turns(20);
  emu::press_button(false);
  pump_turns(20);
  // **A finger on the first row picks the first option**, which is not the one the ring was on: the card is at 80,200
  // and three rows of eighty-two pixels put the first one at 20 to 102 of its own frame.
  emu::Gt911Panel* panel = emu::touch_panel();
  TEST_ASSERT_NOT_NULL(panel);
  const kernel::ui::Canvas& view = kernel::ui::canvas();
  const bool finger_down = panel->finger_down(0, static_cast<uint16_t>(view.x + 230),
                                              static_cast<uint16_t>(view.y + 260));
  TEST_ASSERT_TRUE(finger_down);
  pump_turns(60);
  const bool finger_up = panel->finger_up(0);
  TEST_ASSERT_TRUE(finger_up);
  pump_turns(60);
  // And the dial carries on from the option the finger left it on.
  emu::press_key(2, true);
  pump_turns(20);
  emu::press_key(2, false);
  pump_turns(20);
  emu::press_button(true);
  pump_turns(20);
  emu::press_button(false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "pick 3 at 1"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "pick 1 at 2"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "pick 3 at 3"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "picks 3"), g_out);

  storage->remove("/apps/picker/app.lua");
  storage->remove("/apps/picker/manifest.json");
  storage->remove("/apps/picker");

  // The next test gets the default bench back: the display and the panel were this test's.
  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  opts.touch = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **A bar of menus answers with the title and the entry**: the dial walks the entries of the open menu —past the dash and
// past the entry that the app turned off—, the centre fires `on_pick(self, title, entry)` with the menu already folded, a
// finger on a title opens it and a finger on an entry chooses it (docs/30, docs/90).
void test_a_menu_bar_answers_with_the_title_and_the_entry(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  opts.touch = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());
  TEST_ASSERT_NOT_NULL(hal::platform().touch());
  TEST_ASSERT_TRUE(kernel::touch::boot(hal::platform()));

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "barfly",
                           "{\"title\":\"Barfly\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local picks = 0\n"
                           "local bar = ui.menu_bar(400, 300)\n"
                           "bar:place(20, 100)\n"
                           "bar.items = {\n"
                           "  { title = 'File', items = { 'Open', '-',\n"
                           "    { label = 'Quit', enabled = false }, 'Save' } },\n"
                           "  { title = 'Edit', items = { 'Copy',\n"
                           "    { label = 'Paste', checked = true } } },\n"
                           "}\n"
                           "bar.open = 1\n"
                           "bar.on_pick = function(self, title, entry)\n"
                           "  picks = picks + 1\n"
                           "  console.print('pick ' .. title .. ',' .. entry .. ' open ' .. self.open)\n"
                           "end\n"
                           "ui.present('gc16')\n"
                           "while picks < 2 do sys.delay(10):await() end\n"
                           "console.print('picks ' .. picks)"));

  pump_captured(20, g_out, sizeof(g_out), "run barfly");
  capture_begin();
  pump_turns(20);
  // **The dial**: the first arrow lands on "Open", the second one goes past the dash and past "Quit", which the app turned
  // off, and the centre chooses "Save", which is the fourth entry of the first title.
  emu::press_key(2, true);
  pump_turns(20);
  emu::press_key(2, false);
  pump_turns(20);
  emu::press_key(2, true);
  pump_turns(20);
  emu::press_key(2, false);
  pump_turns(20);
  emu::press_button(true);
  pump_turns(20);
  emu::press_button(false);
  pump_turns(20);
  // **The finger**: the word "Edit" is the second title of the row —seventy pixels each from twenty— and the second entry
  // of its menu is "Paste", which hangs from 70 of the bar and starts at 108 of it.
  emu::Gt911Panel* panel = emu::touch_panel();
  TEST_ASSERT_NOT_NULL(panel);
  const kernel::ui::Canvas& view = kernel::ui::canvas();
  TEST_ASSERT_TRUE(panel->finger_down(0, static_cast<uint16_t>(view.x + 120), static_cast<uint16_t>(view.y + 120)));
  pump_turns(60);
  TEST_ASSERT_TRUE(panel->finger_up(0));
  pump_turns(60);
  TEST_ASSERT_TRUE(panel->finger_down(0, static_cast<uint16_t>(view.x + 170), static_cast<uint16_t>(view.y + 230)));
  pump_turns(60);
  TEST_ASSERT_TRUE(panel->finger_up(0));
  pump_turns(60);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "pick 1,4 open 0"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "pick 2,2 open 0"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "picks 2"), g_out);

  storage->remove("/apps/barfly/app.lua");
  storage->remove("/apps/barfly/manifest.json");
  storage->remove("/apps/barfly");

  // The next test gets the default bench back: the display and the panel were this test's.
  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  opts.touch = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **A box of several lines takes the finger and the enter key**: the finger puts the caret on the letter it is on, the
// enter key of the keyboard writes a line break and does not end the edition, and the app hears the text with the break in
// it (docs/30, docs/90).
void test_a_box_of_lines_types_and_the_enter_writes_a_break(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "writer",
                           "{\"title\":\"Writer\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local box = ui.textbox_multiline(400, 200)\n"
                           "box:place(20, 100)\n"
                           "box.text = 'ab'\n"
                           "box.cursor = 2\n"
                           "local kb = ui.keyboard()\n"
                           "kb.target = box\n"
                           "console.print('rows ' .. box:rows())\n"
                           "box.on_change = function(self, text)\n"
                           "  console.print('text ' .. text:gsub('\\n', '/') .. ' at ' .. self.cursor)\n"
                           "end\n"
                           "ui.present('gc16')\n"
                           "sys.delay(3000):await()\n"));

  pump_captured(20, g_out, sizeof(g_out), "run writer");
  // **These lines are printed before the capture opens**, so they are read from the output of the run itself
  // (`pump_captured`) and not from the window the rest of the test reads (docs/85).
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "rows 8"), g_out);
  capture_begin();
  pump_turns(20);
  // **The enter key of the keyboard is a break and not the end of the edition**: the key arrives as the number the
  // keyboard hands over, and the letter after it goes on the new line.
  luabind::ui_keyboard_typed(13);
  pump_turns(20);
  luabind::ui_keyboard_typed('X');
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  // Eight rows of a box of two hundred pixels with the letter of the body.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "text ab/ at 3"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "text ab/X at 4"), g_out);

  storage->remove("/apps/writer/app.lua");
  storage->remove("/apps/writer/manifest.json");
  storage->remove("/apps/writer");
}

// **A selector is a door and the list is a `ui.menu` the app puts up**: the centre of the dial opens it, the arrows walk
// the options of the card, and the choice writes `index` in the field —which is what it shows from then on— (docs/30,
// docs/90).
void test_a_selector_opens_the_list_the_app_puts_up(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "former",
                           "{\"title\":\"Former\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local picked = 0\n"
                           "local card = nil\n"
                           "local field = ui.selector(400, 56)\n"
                           "field:place(20, 60)\n"
                           "field.label = 'Hora'\n"
                           "field.items = { '09:00', '09:30', '10:00' }\n"
                           "field.empty = '-'\n"
                           "field.on_open = function(self)\n"
                           "  if card ~= nil then return end\n"
                           "  card = ui.menu(240, 200)\n"
                           "  card:place(20, 140)\n"
                           "  card.items = { '09:00', '09:30', '10:00' }\n"
                           "  card.on_pick = function(that, index)\n"
                           "    field.index = index\n"
                           "    field.choosing = false\n"
                           "    card:release()\n"
                           "    card = nil\n"
                           "    picked = picked + 1\n"
                           "    console.print('chose ' .. field.items[index] .. ' at ' .. field.index)\n"
                           "    ui.present('gc16')\n"
                           "  end\n"
                           "  ui.present('gc16')\n"
                           "end\n"
                           "ui.present('gc16')\n"
                           "while picked < 1 do sys.delay(10):await() end\n"
                           "console.print('field ' .. tostring(field))"));

  pump_captured(20, g_out, sizeof(g_out), "run former");
  capture_begin();
  pump_turns(20);
  // The centre opens the list, the first arrow leaves the field and lands on the card of the menu, the second one walks to
  // the second option, and the centre chooses it.
  emu::press_button(true);
  pump_turns(20);
  emu::press_button(false);
  pump_turns(20);
  emu::press_key(2, true);
  pump_turns(20);
  emu::press_key(2, false);
  pump_turns(20);
  emu::press_key(2, true);
  pump_turns(20);
  emu::press_key(2, false);
  pump_turns(20);
  emu::press_button(true);
  pump_turns(20);
  emu::press_button(false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "chose 09:30 at 2"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "field selector"), g_out);

  storage->remove("/apps/former/app.lua");
  storage->remove("/apps/former/manifest.json");
  storage->remove("/apps/former");
}

// **The cross of a notice dismisses it and the app hears which one**: the newest card is the one at the bottom, so the
// finger that presses the cross of the band takes away the last notice the app added (docs/30, docs/90).
void test_the_cross_of_a_notice_dismisses_the_newest(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  opts.touch = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());
  TEST_ASSERT_NOT_NULL(hal::platform().touch());
  TEST_ASSERT_TRUE(kernel::touch::boot(hal::platform()));

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "ringer",
                           "{\"title\":\"Ringer\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local gone = 0\n"
                           "local band = ui.notification_bar(540, 300)\n"
                           "band:place(0, 500)\n"
                           "band.items = { {title = 'Battery', text = 'low'},\n"
                           "  {title = 'Saved', text = 'your note'},\n"
                           "  {title = 'Wi-fi', text = 'connected'} }\n"
                           "band.on_close = function(self, item)\n"
                           "  gone = gone + 1\n"
                           "  table.remove(band.items, item)\n"
                           "  band.items = band.items\n"
                           "  console.print('closed ' .. item .. ' left ' .. #band.items)\n"
                           "end\n"
                           "ui.present('gc16')\n"
                           "while gone < 2 do sys.delay(10):await() end\n"
                           "console.print('gone ' .. gone)"));

  pump_captured(20, g_out, sizeof(g_out), "run ringer");
  capture_begin();
  pump_turns(20);
  // The card of the bottom is at the bottom of the band and its cross is the column on the right, which the finger presses
  // twice: twice because the notice under it takes its place.
  emu::Gt911Panel* panel = emu::touch_panel();
  TEST_ASSERT_NOT_NULL(panel);
  const kernel::ui::Canvas& view = kernel::ui::canvas();
  for (int press = 0; press < 2; ++press) {
    TEST_ASSERT_TRUE(
        panel->finger_down(0, static_cast<uint16_t>(view.x + 520), static_cast<uint16_t>(view.y + 763)));
    pump_turns(60);
    TEST_ASSERT_TRUE(panel->finger_up(0));
    pump_turns(60);
  }
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "closed 3 left 2"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "closed 2 left 1"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "gone 2"), g_out);

  storage->remove("/apps/ringer/app.lua");
  storage->remove("/apps/ringer/manifest.json");
  storage->remove("/apps/ringer");

  // The next test gets the default bench back: the display and the panel were this test's.
  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  opts.touch = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **A grid walks its cells and pages itself**: the arrows walk the cells, the page follows the cursor so the app never turns
// a page, and the centre picks the cell it is on with `on_pick(self, index)` (docs/30, docs/90).
void test_a_grid_walks_its_cells_and_pages_itself(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "launcher",
                           "{\"title\":\"Launcher\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local picks = 0\n"
                           "local grid = ui.icon_grid(300, 300)\n"
                           "grid:place(20, 60)\n"
                           "grid.columns = 3\n"
                           "grid.items = { {label = 'Notes', mark = 'N'}, {label = 'Clock', mark = 'C'},\n"
                           "  {label = 'Files', mark = 'F'}, {label = 'Paint', mark = 'P'},\n"
                           "  {label = 'Music', mark = 'M'}, {label = 'Radio', mark = 'R'},\n"
                           "  {label = 'Codes', mark = 'S'}, {label = 'Maps', mark = 'G'} }\n"
                           "grid.on_pick = function(self, index)\n"
                           "  picks = picks + 1\n"
                           "  console.print('pick ' .. index .. ' of ' .. #grid.items)\n"
                           "end\n"
                           "ui.present('gc16')\n"
                           "while picks < 1 do sys.delay(10):await() end\n"
                           "console.print('grid ' .. tostring(grid))"));

  pump_captured(20, g_out, sizeof(g_out), "run launcher");
  capture_begin();
  pump_turns(20);
  // Seven steps down from nothing: the cursor walks the cells and the page follows it, and the centre picks the seventh.
  for (int step = 0; step < 7; ++step) {
    emu::press_key(2, true);
    pump_turns(10);
    emu::press_key(2, false);
    pump_turns(10);
  }
  emu::press_button(true);
  pump_turns(20);
  emu::press_button(false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "pick 7 of 8"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "grid icon grid"), g_out);

  storage->remove("/apps/launcher/app.lua");
  storage->remove("/apps/launcher/manifest.json");
  storage->remove("/apps/launcher");
}

// Where the keys are is part of the answer: an app gives a function to a position,
// and the board says it in the same words on every board (docs/30, docs/50).
void test_the_board_says_where_its_keys_are(void) {
  TEST_ASSERT_TRUE(boot_once());
  // Three keys, with a face and a place, are the Paper's dial (docs/50).
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "keymap",
                           "{\"title\":\"Key map\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local keys = button.list()\n"
                           "for i = 1, #keys do\n"
                           "  local k = keys[i]\n"
                           "  console.print(k.key .. ' ' .. k.face .. '/' .. k.spot .. ' '\n"
                           "    .. k.kind .. ' ' .. k.group)\n"
                           "end\n"
                           "local middle = button.info(0)\n"
                           "console.print('middle ' .. middle.face .. '/' .. middle.spot)\n"
                           "local missing, err = button.info(9)\n"
                           "console.print('ninth ' .. tostring(missing) .. ' '\n"
                           "  .. tostring(err and err.code))\n"));

  TEST_ASSERT_TRUE(run_captured("run keymap", g_out, sizeof(g_out)));
  // The bench is the Paper here: one compound dial of three keys on the **right** face of the page, with the
  // middle one in the middle. It said `left` until 0.34.2, which is the face the maker's sheet guesses and
  // not the one a person with the board in their hands sees (docs/50, docs/85).
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "0 right/centre compound 0"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "1 right/up compound 0"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "2 right/down compound 0"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "middle right/centre"), g_out);
  // A key that is not there is said, not invented.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "ninth nil E_INVAL"), g_out);

  storage->remove("/apps/keymap/app.lua");
  storage->remove("/apps/keymap/manifest.json");
  storage->remove("/apps/keymap");
}

// The example of the keys, from the disk: the sides step a number and a key that
// stays down steps again, which is what a keyboard does (docs/30, docs/90).
void test_the_keys_demo_app_from_the_disk_runs(void) {
  TEST_ASSERT_TRUE(boot_once());
  // The demo prints the three keys of the dial, so the bench is the board that has them (docs/50).
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);

  static char manifest[256];
  static char source[8192];
  TEST_ASSERT_GREATER_THAN(
      0, read_host_file("apps/keys/manifest.json", manifest, sizeof(manifest)));
  const size_t source_len = read_host_file("apps/keys/keys.lua", source, sizeof(source));
  TEST_ASSERT_GREATER_THAN(0, source_len);
  TEST_ASSERT_LESS_THAN(sizeof(source) - 1, source_len);
  TEST_ASSERT_TRUE(add_app_file(storage, "keys-app", manifest, source, "keys.lua"));

  // It is a loop, so it takes turns instead of ending on its own (docs/30).
  pump_captured(20, g_out, sizeof(g_out), "run keys-app");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "keys: 3"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "value 0"), g_out);
  TEST_ASSERT_TRUE(kernel::app_running());

  // The key at the top of the face goes up one and the one at the bottom goes
  // down one: the function comes from the place on the board and not from the
  // number of the key (docs/30). Both are done before any long hold, so the
  // number is known.
  // A press has to last longer than the app's own turn: it wakes every 40 ms, so a key
  // that goes down and up in 20 ms can be a key it never sees (a finger never goes below
  // 50 ms, docs/50). Sixty turns are still well under the half second of the repeat.
  capture_begin();
  emu::press_key(1, true);
  pump_turns(60);
  emu::press_key(1, false);
  pump_turns(60);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "up 1"), g_out);

  capture_begin();
  emu::press_key(2, true);
  pump_turns(60);
  emu::press_key(2, false);
  pump_turns(60);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "down 0"), g_out);

  // And held down it repeats: the first repeat comes half a second later, so
  // holding for most of a second is two more steps at least. How many exactly
  // depends on the frames, so the test asks for the two that it knows.
  capture_begin();
  emu::press_key(1, true);
  pump_turns(900);
  emu::press_key(1, false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "up 2"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "up 3"), g_out);

  // Three taps of the middle key are the way out, and they are the system's.
  capture_begin();
  for (int tap = 0; tap < 3 && kernel::app_running(); ++tap) {
    emu::press_button(true);
    pump_turns(10);
    emu::press_button(false);
    pump_turns(10);
  }
  capture_end(nullptr, 0);
  TEST_ASSERT_FALSE_MESSAGE(kernel::app_running(), "the middle key did not close the app");

  storage->remove("/apps/keys-app/keys.lua");
  storage->remove("/apps/keys-app/manifest.json");
  storage->remove("/apps/keys-app");
}

void test_an_app_asks_for_the_processor_speed(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "fast",
                            "{\"title\":\"Fast\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                            "console.print('cpu ' .. power.cpu())\n"
                            "console.print('now ' .. power.cpu(80))\n"
                            "local value, err = power.cpu(100)\n"
                            "console.print('refused ' .. tostring(value) .. ' '\n"
                            "  .. tostring(err and err.code))\n"
                            "power.cpu(160)\n"));

  TEST_ASSERT_TRUE_MESSAGE(run_captured("run fast", g_out, sizeof(g_out)), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "cpu 160"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "now 80"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "refused nil E_INVAL"), g_out);
  TEST_ASSERT_EQUAL_UINT32(160, kernel::power::cpu_mhz());

  storage->remove("/apps/fast/app.lua");
  storage->remove("/apps/fast/manifest.json");
  storage->remove("/apps/fast");
}

// A sleep that did not run to its hour is **not** a measurement of the clock: a finger on the
// button, a cable plugged in or a supply that blinked ends it early, and the system says it that way
// instead of reporting the −243 530 ms of a board that never got to prove anything. Measured on the
// glass the 31/10, which is where the number came from (docs/60).
void test_a_sleep_cut_short_is_not_a_measurement(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IKeyValue* nvs = hal::platform().nvs();
  TEST_ASSERT_NOT_NULL(nvs);
  nvs->erase("power", "drift");
  nvs->erase("power", "cause");

  // The bench comes back 243 s before the hour it was given, which is what a board whose supply
  // blinks does: the sleep is over and the hour was never reached.
  emu::Options opts = emu::options();
  opts.wake_early_ms = 243530;
  emu::set_options(opts);
  pump_captured(20, g_out, sizeof(g_out), "sleep light 300000");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "the sleep was cut short by the power coming back"),
                               g_out);
  // The last millisecond is the clock's business and not the test's: what is checked is that the
  // number is the one asked for and that it is said as early (31/10).
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, ", 2435"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "ms before the hour it was given"), g_out);
  // And it is not dressed up as a drift: `power last` says what kind of number it is.
  pump_captured(10, g_out, sizeof(g_out), "power last");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "-2435"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "ms (power, cut short)"), g_out);

  // A sleep that does reach its hour is still measured as one, with the plain name of the clock that
  // brought the board back.
  opts.wake_early_ms = 0;
  emu::set_options(opts);
  pump_captured(20, g_out, sizeof(g_out), "sleep light 1000");
  pump_captured(10, g_out, sizeof(g_out), "power last");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "ms (timer)"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "cut short"), g_out);

  nvs->erase("power", "drift");
  nvs->erase("power", "cause");
}

// A light sleep comes back in the same turn, the clock has moved by the hour it
// was given, and the board says how far from that hour it woke (docs/90).
void test_a_light_sleep_comes_back_with_the_clock_moved(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IClock* clock = hal::platform().clock();
  TEST_ASSERT_NOT_NULL(clock);
  const int64_t epoch = 1767225600000LL;  // 2026-01-01 00:00:00 UTC
  TEST_ASSERT_TRUE(clock->set_epoch_ms(epoch));

  // The line has to be empty before the board stops, or the last line arrives cut in
  // half: the host console has no buffer of ours, so what proves the kernel asked is the
  // count (docs/50).
  const uint32_t flushed = pal_host::console_flushes();
  pump_captured(20, g_out, sizeof(g_out), "sleep light 1000");
  TEST_ASSERT_TRUE_MESSAGE(pal_host::console_flushes() > flushed,
                           "the console was not flushed before the sleep");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "sleep: light until"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "sleep: awake"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "power: woke on the timer"), g_out);
  // The band's sleep is the clock jumping: it lands on the hour it was given, and
  // the few milliseconds after it are the turns the test itself keeps taking
  // (docs/85).
  const int64_t after = clock->epoch_ms();
  TEST_ASSERT_TRUE_MESSAGE(after >= epoch + 1000 && after < epoch + 1100, "the clock did not move as asked");
}

// An app asks for a deep sleep: the kernel writes down what was in front, closes
// it with its goodbye hooks, and the board comes back into the same app with
// `app.args.restore` (docs/90).
void test_an_app_sleeps_deeply_and_comes_back_where_it_was(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "sleeper",
                            "{\"title\":\"Sleeper\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                            "function on_save() console.print('sleeper saves') end\n"
                            "if not console.is_foreground() then return end\n"
                            "local back = app.args ~= nil and app.args.restore\n"
                            "console.print('awake again ' .. tostring(back))\n"
                            "if back then return end\n"
                            "console.print('going to sleep')\n"
                            "power.sleep({mode='deep', in_ms=1000}):await()\n"
                            "console.print('this line never runs')\n"));

  hal::IClock* clock = hal::platform().clock();
  TEST_ASSERT_NOT_NULL(clock);
  TEST_ASSERT_TRUE(clock->set_epoch_ms(1767225600000LL));

  // The app asks and the kernel does the rest in the next turn, closing the app
  // first: closing the interpreter from inside its own call is not a thing.
  TEST_ASSERT_TRUE_MESSAGE(run_captured("run sleeper", g_out, sizeof(g_out)), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "awake again false"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "going to sleep"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "sleeper saves"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "this line never runs"), g_out);
  TEST_ASSERT_FALSE_MESSAGE(kernel::app_running(), "the app survived the deep sleep");

  // The RAM does not come back: this is the boot, without one (docs/85). The
  // capture goes around the boot, because the line that says the app is back is
  // said before the app's first turn.
  capture_begin();
  kernel::power::restore_session();
  pump_turns(60);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "sleeper comes back where it was"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "awake again true"), g_out);

  storage->remove("/apps/sleeper/app.lua");
  storage->remove("/apps/sleeper/manifest.json");
  storage->remove("/apps/sleeper");
}

// The demo app that sleeps is a file in the repository, so it is run here: an
// example that stops working is a lie in the documentation.
void test_the_sleepy_demo_app_from_the_disk_runs(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);

  static char manifest[256];
  static char source[4096];
  TEST_ASSERT_GREATER_THAN(
      0, read_host_file("apps/sleepy/manifest.json", manifest, sizeof(manifest)));
  TEST_ASSERT_GREATER_THAN(
      0, read_host_file("apps/sleepy/sleepy.lua", source, sizeof(source)));
  TEST_ASSERT_TRUE(add_app_file(storage, "sleepy-app", manifest, source, "sleepy.lua"));

  hal::IClock* clock = hal::platform().clock();
  TEST_ASSERT_NOT_NULL(clock);
  TEST_ASSERT_TRUE(clock->set_epoch_ms(1767225600000LL));

  TEST_ASSERT_TRUE_MESSAGE(run_captured("run sleepy-app", g_out, sizeof(g_out)), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "first run, nothing saved yet"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "saved after sleep 1"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "wakes without restarting"), g_out);

  // The board boots again and the app reads what it left on the disk.
  capture_begin();
  kernel::power::restore_session();
  pump_turns(60);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "back after the sleep, it kept: 1"), g_out);

  storage->remove("/apps/sleepy-app/sleepy.lua");
  storage->remove("/apps/sleepy-app/manifest.json");
  storage->remove("/apps/sleepy-app");
}

// A failing app says why once and then where. Saying why twice made a mistake in
// an app read like two mistakes (seen on the board, 30/10).
void test_a_failing_app_says_its_error_once(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "boom",
                           "{\"title\":\"Boom\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local nothing = nil\nconsole.print(nothing.ssid)\n"));
  TEST_ASSERT_TRUE(run_captured("run boom", g_out, sizeof(g_out)));

  const char* needle = "attempt to index";
  int count = 0;
  for (const char* p = g_out; (p = strstr(p, needle)) != nullptr; ++p) ++count;
  TEST_ASSERT_EQUAL_MESSAGE(1, count, g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "stack traceback:"), g_out);

  storage->remove("/apps/boom/app.lua");
  storage->remove("/apps/boom/manifest.json");
  storage->remove("/apps/boom");
}

// The demo app that asks what is mounted is a file in the repository too, and it
// is run here: it is what a person types on the board to see the room (docs/70).
void test_the_mtab_demo_app_from_the_disk_runs(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);

  static char manifest[256];
  static char source[4096];
  TEST_ASSERT_GREATER_THAN(
      0, read_host_file("apps/mtab/manifest.json", manifest, sizeof(manifest)));
  TEST_ASSERT_GREATER_THAN(0, read_host_file("apps/mtab/mtab.lua", source, sizeof(source)));
  TEST_ASSERT_TRUE(add_app_file(storage, "mtab-app", manifest, source, "mtab.lua"));

  TEST_ASSERT_TRUE(run_captured("run mtab-app", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "mounts: 8"), g_out);
  // The RTC memory is a file of the chip: a mount of its own, writable, and not a filesystem. Its
  // room is its size and all of it is free, because nothing is allocated out of raw memory and
  // counting entries there would be a number that means nothing (docs/70).
  char rtc_line[64];
  snprintf(rtc_line, sizeof(rtc_line), "/rtc rtc rw  %u of %u B free",
           static_cast<unsigned>(emu::kRtcRamBytes), static_cast<unsigned>(emu::kRtcRamBytes));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, rtc_line), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "/data littlefs rw  "), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "of 917504 B free"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "/nvs nvs ro  unknown (E_UNSUPPORTED)"), g_out);

  storage->remove("/apps/mtab-app/mtab.lua");
  storage->remove("/apps/mtab-app/manifest.json");
  storage->remove("/apps/mtab-app");
}

// The dump command of the shell, native C++ and not an app: the memory is read as hex and as text, a
// byte that cannot be printed is a dot, and the empty tail is not printed at all (docs/50, docs/70).
void test_the_hexdump_command_shows_the_bytes_of_the_rtc_memory(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* rtc = hal::platform().storage("/rtc");
  TEST_ASSERT_NOT_NULL(rtc);

  // An empty memory is said in one line and not in four hundred rows of zeros.
  memset(emu::rtc_ram(), 0, emu::kRtcRamBytes);
  TEST_ASSERT_TRUE(run_captured("hexdump", g_out, sizeof(g_out)));
  char empty[96];
  snprintf(empty, sizeof(empty), "is %u B and every one of them is zero",
           static_cast<unsigned>(emu::kRtcRamBytes));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, empty), g_out);

  // Something that is not text all the way: a byte a terminal cannot show and a zero in the middle,
  // which is what a struct of a real app looks like. It goes in through a handle, which is the only
  // way to write raw memory (docs/70).
  static const uint8_t kept[] = {'h', 'o', 'l', 'a', 0x01, 0x00, 'R', 'T', 'C'};
  hal::FileSlot slot;
  TEST_ASSERT_TRUE(rtc->open("", hal::OpenMode::kReadWrite, slot));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(sizeof(kept)),
                        rtc->write(slot, kept, sizeof(kept)));
  TEST_ASSERT_TRUE(rtc->close(slot));

  TEST_ASSERT_TRUE(run_captured("hexdump", g_out, sizeof(g_out)));
  char summary[96];
  snprintf(summary, sizeof(summary), "is %u B, %u in use and %u zero after",
           static_cast<unsigned>(emu::kRtcRamBytes), static_cast<unsigned>(sizeof(kept)),
           static_cast<unsigned>(emu::kRtcRamBytes - sizeof(kept)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, summary), g_out);
  // The offset, the hex of sixteen bytes, and the same sixteen between bars with the dot in place of
  // the control byte and of the zero, which is a dot as well because it is not printable.
  TEST_ASSERT_NOT_NULL_MESSAGE(
      strstr(g_out, "00000000  68 6f 6c 61 01 00 52 54 43 00 00 00 00 00 00 00"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "|hola..RTC.......|"), g_out);
  // Only one row: the memory has nine bytes in use and what comes after them is not shown.
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "00000010"), g_out);

  memset(emu::rtc_ram(), 0, emu::kRtcRamBytes);
}

// The FRAM is 256 bytes that **do not need the power**, and like the RTC memory it is one file an app
// reads and writes: the layout is the app's. Two things are the point here: a board without the chip
// has no `/fram` at all, and the chip is the file — what is written is in the memory when the call
// returns, with no write cycle to wait for, because it is a FRAM and not an EEPROM (docs/50,
// docs/70).
void test_the_fram_is_one_file_of_its_bytes(void) {
  TEST_ASSERT_TRUE(boot_once());

  // The board the tests start on has no FM24C02 soldered, so it has no mount to offer.
  TEST_ASSERT_NULL(hal::platform().storage("/fram"));

  use_board("m5stack-paper");
  hal::IStorage* fram = hal::platform().storage("/fram");
  TEST_ASSERT_NOT_NULL(fram);

  hal::Stat info;
  TEST_ASSERT_TRUE(fram->stat("", info));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(hal::NodeType::kFile),
                          static_cast<uint8_t>(info.type));
  TEST_ASSERT_EQUAL_UINT32(emu::kFramBytes, info.size);

  // A round trip through a handle, which is the only way to write raw memory (docs/70).
  static const char kept[] = "kept in the fram";  // sixteen bytes: one row of the dump
  hal::FileSlot slot;
  TEST_ASSERT_TRUE(fram->open("", hal::OpenMode::kReadWrite, slot));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(sizeof(kept) - 1),
                        fram->write(slot, kept, sizeof(kept) - 1));
  TEST_ASSERT_TRUE(fram->seek(slot, 0));
  char back[24] = {0};
  TEST_ASSERT_EQUAL_INT(static_cast<int>(sizeof(kept) - 1), fram->read(slot, back, sizeof(kept) - 1));
  TEST_ASSERT_EQUAL_STRING(kept, back);
  // It has an end, and writing past it is a short write and not a crash.
  TEST_ASSERT_TRUE(fram->seek(slot, emu::kFramBytes - 2));
  TEST_ASSERT_EQUAL_INT(2, fram->write(slot, kept, sizeof(kept) - 1));
  TEST_ASSERT_TRUE(fram->close(slot));

  // It is a file and not a filesystem, and its room is its size, all of it free: nothing is
  // allocated out of raw memory (docs/70).
  TEST_ASSERT_FALSE(fram->mkdir(""));
  uint64_t total = 0;
  uint64_t free_bytes = 0;
  TEST_ASSERT_TRUE(fram->space(total, free_bytes));
  TEST_ASSERT_EQUAL_UINT32(emu::kFramBytes, static_cast<uint32_t>(total));
  TEST_ASSERT_EQUAL_UINT32(emu::kFramBytes, static_cast<uint32_t>(free_bytes));

  // The prompt sees it, and says how much room it has. The path is padded by `print_mounts`, so the
  // test looks for the name and for the number and not for the whole line (docs/70).
  pump_captured(20, g_out, sizeof(g_out), "mounts");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "/fram"), g_out);
  char line[64];
  snprintf(line, sizeof(line), "free %u of %u B", static_cast<unsigned>(emu::kFramBytes),
           static_cast<unsigned>(emu::kFramBytes));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, line), g_out);
  // And the settings door is still there: the table had exactly eight rows and the FRAM took the
  // eighth, which is how `/cfg` disappeared on the bench (31/10, docs/70).
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "/cfg"), g_out);

  // And the command of the shell looks at it when it is told which file: `hexdump /fram`.
  TEST_ASSERT_TRUE(run_captured("hexdump /fram", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "|kept in the fram|"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(
      strstr(g_out, "00000000  6b 65 70 74 20 69 6e 20 74 68 65 20 66 72 61 6d"), g_out);

  // The memory that does not need the power: a power cycle of the bench does not touch it.
  memset(emu::fram(), 0, emu::kFramBytes);
}

// **The two memories are files and not directories**: the root listing is the mount table, but each
// row says what the mount is. A person reading `ls` must not be told that `/rtc` is a directory and
// then find that `cat /rtc` reads 7 168 bytes (docs/70, docs/85).
void test_the_root_lists_the_memories_as_files(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  TEST_ASSERT_NOT_NULL(hal::platform().storage("/rtc"));
  TEST_ASSERT_NOT_NULL(hal::platform().storage("/fram"));

  pump_captured(20, g_out, sizeof(g_out), "ls");
  char rtc[64];
  snprintf(rtc, sizeof(rtc), "  file    rtc  %u", static_cast<unsigned>(emu::kRtcRamBytes));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, rtc), g_out);
  char fram[64];
  snprintf(fram, sizeof(fram), "  file    fram  %u", static_cast<unsigned>(emu::kFramBytes));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, fram), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "  <dir>   apps"), g_out);
}

// The battery demo of the repository, run from the disk: the two paths are the point,
// because a board that cannot say is not a board that says zero (docs/30, docs/50).
void test_the_battery_demo_app_from_the_disk_runs(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);

  static char manifest[256];
  static char source[4096];
  TEST_ASSERT_GREATER_THAN(
      0, read_host_file("apps/battery/manifest.json", manifest, sizeof(manifest)));
  TEST_ASSERT_GREATER_THAN(
      0, read_host_file("apps/battery/battery.lua", source, sizeof(source)));
  TEST_ASSERT_TRUE(add_app_file(storage, "battery-app", manifest, source, "battery.lua"));

  // A board with no gauge: it says so, and it does not say zero volts. The battery is the Paper's,
  // so the bench has to be one of those or it would be a board answering for hardware it has not
  // got (docs/50, docs/85).
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.battery_mv = 0;
  emu::set_options(opts);
  TEST_ASSERT_TRUE(run_captured("run battery-app", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "does not know its battery"), g_out);

  // And a cell at 3,9 V: the measurement, the curve and the bar.
  opts.battery_mv = 3900;
  emu::set_options(opts);
  TEST_ASSERT_TRUE(run_captured("run battery-app", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "3900 mV, 70 %"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "charging: the board cannot tell"), g_out);

  // And the same number on an ATOM is a board with no gauge at all, which is the other path.
  opts.board = "m5stack-atom";
  emu::set_options(opts);
  TEST_ASSERT_TRUE(run_captured("run battery-app", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "does not know its battery"), g_out);
  opts.board = nullptr;
  emu::set_options(opts);

  storage->remove("/apps/battery-app/battery.lua");
  storage->remove("/apps/battery-app/manifest.json");
  storage->remove("/apps/battery-app");
}

// The alarm demos of the repository, run from the disk: the queue the system keeps and the
// timers an app arms. A demo that stops working is a lie in the documentation (docs/40), and
// these two are the ones that show the queue of `lib/alarms` and the clock behind it.
void test_the_alarm_demos_from_the_disk_run(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);

  static char manifest[256];
  static char source[4096];

  // The queue: what this board allows, three armed, one cancelled and the gap closed. The app
  // keeps running on purpose after that (its last line waits nine seconds), so it is closed
  // here by hand rather than waited for.
  TEST_ASSERT_GREATER_THAN(
      0, read_host_file("apps/alarmqueue/manifest.json", manifest, sizeof(manifest)));
  TEST_ASSERT_GREATER_THAN(
      0, read_host_file("apps/alarmqueue/alarmqueue.lua", source, sizeof(source)));
  TEST_ASSERT_TRUE(add_app_file(storage, "alarmqueue-app", manifest, source, "alarmqueue.lua"));
  pump_captured(40, g_out, sizeof(g_out), "run alarmqueue-app");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "alarms: 8 at once"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "armed "), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "cancelling "), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, ": true"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "queue without the middle one"), g_out);
  kernel::shell::close_app();
  // The two that were left are the test's now: they would fire inside the next one.
  alarms::reset();
  storage->remove("/apps/alarmqueue-app/alarmqueue.lua");
  storage->remove("/apps/alarmqueue-app/manifest.json");
  storage->remove("/apps/alarmqueue-app");

  // The timers: three of them, delivered in order while the app waits, and the app ends by
  // itself inside the turns a test gives it.
  TEST_ASSERT_GREATER_THAN(
      0, read_host_file("apps/timers/manifest.json", manifest, sizeof(manifest)));
  TEST_ASSERT_GREATER_THAN(0,
                           read_host_file("apps/timers/timers.lua", source, sizeof(source)));
  TEST_ASSERT_TRUE(add_app_file(storage, "timers-app", manifest, source, "timers.lua"));
  TEST_ASSERT_TRUE_MESSAGE(run_captured("run timers-app", g_out, sizeof(g_out)), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "three timers armed"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "timer 400 ms"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "timer 800 ms"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "timer 1200 ms"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "without stopping anything"), g_out);

  storage->remove("/apps/timers-app/timers.lua");
  storage->remove("/apps/timers-app/manifest.json");
  storage->remove("/apps/timers-app");

  // The one that arms an alarm and leaves: it ends by itself, so this one can be waited for. What
  // it leaves in the queue is taken down here, or it would go off inside the test after this one.
  TEST_ASSERT_GREATER_THAN(
      0, read_host_file("apps/later/manifest.json", manifest, sizeof(manifest)));
  TEST_ASSERT_GREATER_THAN(0, read_host_file("apps/later/later.lua", source, sizeof(source)));
  TEST_ASSERT_TRUE(add_app_file(storage, "later-app", manifest, source, "later.lua"));
  TEST_ASSERT_TRUE_MESSAGE(run_captured("run later-app", g_out, sizeof(g_out)), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "armed for"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "leaving: the queue keeps it"), g_out);
  alarms::reset();

  storage->remove("/apps/later-app/later.lua");
  storage->remove("/apps/later-app/manifest.json");
  storage->remove("/apps/later-app");
}

// The maths between a date and an epoch: no hardware in it, and it is where a leap year can
// go wrong without anybody noticing for four years (docs/50).
void test_the_civil_calendar_round_trips(void) {
  // 1970-01-01 is zero by definition, and the 31st of October 2026 is 20 757 days later.
  TEST_ASSERT_EQUAL_INT64(0, hal::days_from_civil(1970, 1, 1));
  TEST_ASSERT_EQUAL_INT64(20757, hal::days_from_civil(2026, 10, 31));

  int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
  hal::civil_from_days(20757, y, mo, d);
  TEST_ASSERT_EQUAL_INT(2026, y);
  TEST_ASSERT_EQUAL_INT(10, mo);
  TEST_ASSERT_EQUAL_INT(31, d);

  // The 29th of February exists on a leap year and not on the one after a century that is
  // not a multiple of four: 2000 yes, 2100 no.
  TEST_ASSERT_EQUAL_INT64(1, hal::days_from_civil(2024, 3, 1) - hal::days_from_civil(2024, 2, 29));
  TEST_ASSERT_EQUAL_INT64(1, hal::days_from_civil(2000, 3, 1) - hal::days_from_civil(2000, 2, 29));
  TEST_ASSERT_EQUAL_INT64(1, hal::days_from_civil(2100, 3, 1) - hal::days_from_civil(2100, 2, 28));

  // An epoch and its six fields, both ways, with an hour that has minutes and seconds.
  const int64_t ms = hal::civil_to_epoch_ms(2026, 10, 31, 19, 30, 7);
  hal::epoch_ms_to_civil(ms, y, mo, d, h, mi, s);
  TEST_ASSERT_EQUAL_INT(2026, y);
  TEST_ASSERT_EQUAL_INT(10, mo);
  TEST_ASSERT_EQUAL_INT(31, d);
  TEST_ASSERT_EQUAL_INT(19, h);
  TEST_ASSERT_EQUAL_INT(30, mi);
  TEST_ASSERT_EQUAL_INT(7, s);
  TEST_ASSERT_EQUAL_INT64(ms, hal::civil_to_epoch_ms(y, mo, d, h, mi, s));

  // And the binary-coded decimal the clock chips keep, which is not the number itself.
  TEST_ASSERT_EQUAL_UINT8(0x25, hal::bin_to_bcd(25));
  TEST_ASSERT_EQUAL_UINT8(25, hal::bcd_to_bin(0x25));
  for (uint8_t value = 0; value < 60; ++value) {
    TEST_ASSERT_EQUAL_UINT8(value, hal::bcd_to_bin(hal::bin_to_bcd(value)));
  }
}

// The alarm of the clock chip: the hour and the minute, and the day and the weekday left out
// on purpose (hal/alarm.h). This is what turns the board back on after a full power off, so
// the two fields it compares are the whole design (docs/50).
void test_the_alarm_is_the_hour_and_the_minute(void) {
  const hal::AlarmFields dawn =
      hal::alarm_fields(hal::civil_to_epoch_ms(2026, 10, 31, 7, 5, 0));
  TEST_ASSERT_EQUAL_UINT8(0x05, dawn.minute);
  TEST_ASSERT_EQUAL_UINT8(0x07, dawn.hour);
  TEST_ASSERT_EQUAL_UINT8(hal::kAlarmAny, dawn.day);
  TEST_ASSERT_EQUAL_UINT8(hal::kAlarmAny, dawn.weekday);

  // The end of the day is binary coded, which is where a decimal twenty-three would hide,
  // and a deadline with seconds on it is **rounded up**: 23:59:30 is armed for the next
  // midnight, in the next day, because a board woken at 23:59:00 would find nothing due and
  // arm the same minute again —which is the next day— (docs/50).
  const hal::AlarmFields late =
      hal::alarm_fields(hal::civil_to_epoch_ms(2026, 1, 2, 23, 59, 30));
  TEST_ASSERT_EQUAL_UINT8(0x00, late.minute);
  TEST_ASSERT_EQUAL_UINT8(0x00, late.hour);

  // A minute that is already whole is not moved: this is the case a clock alarm uses.
  const hal::AlarmFields on_the_minute =
      hal::alarm_fields(hal::civil_to_epoch_ms(2026, 1, 2, 12, 0, 0));
  TEST_ASSERT_EQUAL_UINT8(0x00, on_the_minute.minute);
  TEST_ASSERT_EQUAL_UINT8(0x12, on_the_minute.hour);

  // And one second over rolls into the next minute rather than back into this one.
  const hal::AlarmFields over =
      hal::alarm_fields(hal::civil_to_epoch_ms(2026, 1, 2, 0, 0, 59));
  TEST_ASSERT_EQUAL_UINT8(0x01, over.minute);
  TEST_ASSERT_EQUAL_UINT8(0x00, over.hour);

  // The bench keeps the board powered, so its deep sleep is a sleep and not a power off. A
  // board that switches itself off has to say so, or the system would promise an app a wake
  // from a board that is not there (docs/50).
  TEST_ASSERT_FALSE(hal::platform().power()->deep_powers_off());
}

// The chip is not one alarm: it has a **countdown** (a tick a second up to 255 s, a tick a
// minute up to 255) and a **clock alarm** (hour and minute) — and the one that is used is the
// most exact that reaches (hal/alarm.h). This decides whether an app's alarm is sharp or a
// minute late, so its edges are worth a test (docs/50).
void test_the_clock_chip_picks_the_most_exact_wake_it_has(void) {
  const int64_t now = hal::civil_to_epoch_ms(2026, 1, 2, 10, 0, 0);
  const uint8_t seconds = static_cast<uint8_t>(hal::WakePlan::kTimerSeconds);
  const uint8_t minutes = static_cast<uint8_t>(hal::WakePlan::kTimerMinutes);
  const uint8_t clock = static_cast<uint8_t>(hal::WakePlan::kAlarmClock);
  const uint8_t none = static_cast<uint8_t>(hal::WakePlan::kNone);

  // Ten seconds is ten ticks of one second.
  const hal::WakeFields ten = hal::wake_for(now, now + 10000);
  TEST_ASSERT_EQUAL_UINT8(seconds, static_cast<uint8_t>(ten.plan));
  TEST_ASSERT_EQUAL_UINT8(10, ten.ticks);

  // Rounded **up**, never down: a board woken a tick short is a board woken before its hour.
  TEST_ASSERT_EQUAL_UINT8(2, hal::wake_for(now, now + 1001).ticks);
  // A tenth of a second is still one tick, and not zero.
  const hal::WakeFields tiny = hal::wake_for(now, now + 100);
  TEST_ASSERT_EQUAL_UINT8(1, tiny.ticks);
  TEST_ASSERT_EQUAL_UINT8(seconds, static_cast<uint8_t>(tiny.plan));

  // The last second the countdown can count, and the one after it, which is minutes.
  TEST_ASSERT_EQUAL_UINT8(seconds, static_cast<uint8_t>(hal::wake_for(now, now + 255000).plan));
  TEST_ASSERT_EQUAL_UINT8(255, hal::wake_for(now, now + 255000).ticks);
  const hal::WakeFields over_a_minute = hal::wake_for(now, now + 256000);
  TEST_ASSERT_EQUAL_UINT8(minutes, static_cast<uint8_t>(over_a_minute.plan));
  TEST_ASSERT_EQUAL_UINT8(5, over_a_minute.ticks);  // 256 s, up to the next whole minute

  // Four hours and a quarter is the last of the countdown; a minute more falls to the clock
  // alarm, which is the one without seconds.
  TEST_ASSERT_EQUAL_UINT8(minutes, static_cast<uint8_t>(hal::wake_for(now, now + 255 * 60000).plan));
  TEST_ASSERT_EQUAL_UINT8(255, hal::wake_for(now, now + 255 * 60000).ticks);
  const hal::WakeFields alarm = hal::wake_for(now, now + 256 * 60000);
  TEST_ASSERT_EQUAL_UINT8(clock, static_cast<uint8_t>(alarm.plan));
  TEST_ASSERT_EQUAL_UINT8(0x16, alarm.minute);  // 10:00 plus 4 h 16 is 14:16
  TEST_ASSERT_EQUAL_UINT8(0x14, alarm.hour);

  // Past a day the chip cannot be told in one go, and the board is armed **in stages** instead of
  // being handed to the RC timer: 25 hours after 10:00 is 11:00 the next day, and the board comes
  // back to arm the rest (docs/60).
  const hal::WakeFields staged = hal::wake_for(now, now + 25 * 3600000LL);
  TEST_ASSERT_EQUAL_UINT8(clock, static_cast<uint8_t>(staged.plan));
  TEST_ASSERT_EQUAL_UINT8(0x00, staged.minute);
  TEST_ASSERT_EQUAL_UINT8(0x11, staged.hour);  // eleven, in binary coded decimal

  // A moment already gone is not a wake at all, and neither is now: the board stays on instead of
  // being powered off with nothing armed (docs/50).
  TEST_ASSERT_EQUAL_UINT8(none, static_cast<uint8_t>(hal::wake_for(now, now - 1000).plan));
  TEST_ASSERT_EQUAL_UINT8(none, static_cast<uint8_t>(hal::wake_for(now, now).plan));}

// The error of a sleep is the clock's and not the firmware's: a board that restarts reports after
// the bootloader, the banner, the flash and the card have had a second, and that second was being
// counted as drift —a 20 s sleep came out 6 % off on the Paper the 31/10— (docs/60).
void test_the_boot_is_not_part_of_the_clocks_error(void) {
  const int64_t asked = 1793483154301LL;
  // Back from a restart 400 ms late, 1 100 ms after the reset: what the clock did is the 400.
  TEST_ASSERT_EQUAL_INT(400, static_cast<int>(hal::wake_error_ms(asked + 1500, 1100, asked, true)));
  // A light sleep does not restart, so its uptime is not the boot's and none of it is taken out.
  TEST_ASSERT_EQUAL_INT(1500,
                        static_cast<int>(hal::wake_error_ms(asked + 1500, 1100, asked, false)));
  // Early is negative, and a board that comes back on the second is zero.
  TEST_ASSERT_EQUAL_INT(-250,
                        static_cast<int>(hal::wake_error_ms(asked - 250, 900, asked, false)));
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(hal::wake_error_ms(asked + 900, 900, asked, true)));
}

// The RTC memory is **one file of its size**, and what is written there is what is read back: it is
// the only memory that survives a deep sleep, so a board keeps a few bytes there without touching
// the flash (docs/50, docs/70).
void test_the_rtc_memory_is_one_file_of_its_size(void) {
  hal::IStorage* rtc = hal::platform().storage("/rtc");
  TEST_ASSERT_NOT_NULL(rtc);

  hal::Stat info;
  TEST_ASSERT_TRUE(rtc->stat("", info));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(hal::NodeType::kFile),
                          static_cast<uint8_t>(info.type));
  TEST_ASSERT_EQUAL_UINT32(emu::kRtcRamBytes, info.size);

  // A round trip through a handle: the bytes come back as they went in.
  hal::FileSlot slot;
  TEST_ASSERT_TRUE(rtc->open("", hal::OpenMode::kReadWrite, slot));
  const char tag[] = "kept";
  TEST_ASSERT_EQUAL_INT(4, rtc->write(slot, tag, 4));
  TEST_ASSERT_TRUE(rtc->seek(slot, 0));
  char back[8] = {0};
  TEST_ASSERT_EQUAL_INT(4, rtc->read(slot, back, 4));
  TEST_ASSERT_EQUAL_STRING("kept", back);
  // The memory has an end, and writing past it is a short write and not a crash.
  const uint32_t last = static_cast<uint32_t>(emu::kRtcRamBytes) - 2;
  TEST_ASSERT_TRUE(rtc->seek(slot, last));
  TEST_ASSERT_EQUAL_INT(2, rtc->write(slot, tag, 4));
  TEST_ASSERT_TRUE(rtc->close(slot));

  // It is a file and not a filesystem: no directories, no renames, and nothing to list.
  TEST_ASSERT_FALSE(rtc->mkdir(""));
  TEST_ASSERT_FALSE(rtc->rename("", ""));
}

// The number the energy policy is waiting for: how far the board woke from the hour it was
// given, kept where a person can read it after a deep sleep has taken the console line with
// the restart (docs/60).
void test_the_last_wake_is_measured_and_kept(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IKeyValue* nvs = hal::platform().nvs();
  TEST_ASSERT_NOT_NULL(nvs);
  // Nothing measured yet: it says so instead of showing a zero that looks like a measurement.
  nvs->erase("power", "drift");
  nvs->erase("power", "cause");
  pump_captured(10, g_out, sizeof(g_out), "power last");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "no sleep has been measured yet"), g_out);

  // A light sleep on the bench is the clock jumping: the number is what came out, and it
  // is the timer that brought it back.
  pump_captured(20, g_out, sizeof(g_out), "sleep light 1000");
  pump_captured(10, g_out, sizeof(g_out), "power last");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "last wake: +"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "ms (timer)"), g_out);

  // And it survives a restart: it is in the key-value store and not in the RAM of a boot.
  char text[24];
  size_t len = 0;
  TEST_ASSERT_TRUE(nvs->get("power", "drift", text, sizeof(text) - 1, len));
  TEST_ASSERT_TRUE(len > 0);

  nvs->erase("power", "drift");
  nvs->erase("power", "cause");
}

// The idle policy: a board that keeps the hour in a chip puts itself to sleep until the next
// alarm, and a person can turn that off while working at the prompt. This bench has no chip,
// so what is proved here is the switch and that the bench is honest about not sleeping by
// itself (docs/60).
void test_the_idle_sleep_is_a_switch(void) {
  TEST_ASSERT_TRUE(boot_once());
  pump_captured(10, g_out, sizeof(g_out), "power idle");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "idle sleep: on"), g_out);

  pump_captured(10, g_out, sizeof(g_out), "power idle off");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "idle sleep: off"), g_out);
  TEST_ASSERT_FALSE(kernel::power::idle_sleep_allowed());

  // A word that is not on or off is refused and does not change anything.
  pump_captured(10, g_out, sizeof(g_out), "power idle maybe");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "usage: power idle"), g_out);
  TEST_ASSERT_FALSE(kernel::power::idle_sleep_allowed());

  pump_captured(10, g_out, sizeof(g_out), "power idle on");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "idle sleep: on"), g_out);
  TEST_ASSERT_TRUE(kernel::power::idle_sleep_allowed());

  // The policy reads the board before it reads a clock: this one does not stop being
  // powered when it sleeps, so it never sleeps on its own, whatever the switch says.
  TEST_ASSERT_FALSE(hal::platform().power()->deep_powers_off());
}

// Off is not a sleep: on a board that holds its own supply the command ends the system, and on a
// board that does not the board says so instead of looking like it worked (docs/50, docs/85). The
// ATOM has no hold on its supply; the Paper has one, and the model of the bench says so.
void test_poweroff_cuts_the_power_of_the_board_that_can(void) {
  TEST_ASSERT_TRUE(boot_once());

  // The ATOM, which is the board a test starts on: no hold, so the line says why and the system
  // goes on running. A command that looked like it worked and left the board on is worse.
  TEST_ASSERT_TRUE(run_captured("poweroff", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "powering off..."), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "cannot cut its own power"), g_out);
  TEST_ASSERT_TRUE(kernel::running());

  // The Paper: the supply is cut, and the bench ends because a board that is off is a board that is
  // off. There is nothing to say after the cut, so the system is not running any more.
  use_board("m5stack-paper");
  TEST_ASSERT_TRUE(run_captured("poweroff", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "powering off..."), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "cannot cut its own power"), g_out);
  TEST_ASSERT_FALSE(kernel::running());
}

// The demo app that shows handles by hand is a file in the repository, so it is
// run here: an example that stops working is a lie in the documentation.
void test_the_handles_demo_app_from_the_disk_runs(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);

  static char manifest[256];
  static char source[4096];
  TEST_ASSERT_GREATER_THAN(
      0, read_host_file("apps/handles/manifest.json", manifest, sizeof(manifest)));
  TEST_ASSERT_GREATER_THAN(
      0, read_host_file("apps/handles/handles.lua", source, sizeof(source)));
  TEST_ASSERT_TRUE(add_app_file(storage, "handles", manifest, source, "handles.lua"));

  TEST_ASSERT_TRUE(run_captured("run handles", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "opened for writing: file (open)"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "closed: file (closed)"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "line one"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "rest 8 bytes, at 8"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "holding 8 handles"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "ninth nil E_NO_HANDLES"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "after closing all: file (open)"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "missing nil E_NOENT"), g_out);

  storage->remove("data/handles/handles.txt");
  storage->remove("data/handles");
  storage->remove("/apps/handles/handles.lua");
  storage->remove("/apps/handles/manifest.json");
  storage->remove("/apps/handles");
}

// The sensor of the band is a fake (docs/85), so a reading can be tried on the PC:
// this walks the whole path, from the app to the fake chip and back, and the light
// of the panel follows what it says (docs/30, docs/50).
void test_the_tilt_app_of_the_disk_follows_the_sensor(void) {
  TEST_ASSERT_TRUE(boot_once());
  // The level is the Matrix's app and it says so, so the bench has to be one (docs/85).
  use_board("m5stack-atom-matrix");
  emu::Options opts = emu::options();
  opts.imu_fail = false;
  opts.imu_ax = 0.0f;
  opts.imu_ay = 0.0f;
  opts.imu_az = 1.0f;
  opts.imu_celsius = 24.5f;
  emu::set_options(opts);

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);

  static char manifest[256];
  static char source[8192];
  TEST_ASSERT_GREATER_THAN(
      0, read_host_file("apps/tilt/manifest.json", manifest, sizeof(manifest)));
  const size_t source_len =
      read_host_file("apps/tilt/tilt.lua", source, sizeof(source));
  TEST_ASSERT_GREATER_THAN(0, source_len);
  TEST_ASSERT_LESS_THAN(sizeof(source) - 1, source_len);
  TEST_ASSERT_TRUE(add_app_file(storage, "tilt-app", manifest, source, "tilt.lua"));

  // A level does not end: the prompt comes back with the system's gesture, so this
  // runs it and takes turns instead of waiting for it (docs/30).
  pump_captured(30, g_out, sizeof(g_out), "run tilt-app");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "tilt 0.00 0.00 g  24.5 C"), g_out);
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), "the level ended on its own");

  // Level is the middle light and only the middle light: the panel has to show
  // where gravity pulls, and a bubble left behind is a bubble that lies.
  uint8_t lit = 0;
  for (uint32_t light = 0; light < 25; ++light) {
    uint8_t r = 0, g = 0, b = 0;
    TEST_ASSERT_TRUE(emu::led_pixel(light, r, g, b));
    if (r > 0 || g > 0 || b > 0) {
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(12, light, "the bubble was not in the middle");
      lit = r;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(lit > 0, "the middle light was never painted");

  // Stood on its side: a whole g on the x axis moves the bubble to the right edge
  // one frame later, and the numbers move with it.
  opts.imu_ax = 1.0f;
  emu::set_options(opts);
  capture_begin();
  pump_turns(250);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "tilt 1.00 0.00 g"), g_out);
  uint8_t r = 0, g = 0, b = 0;
  TEST_ASSERT_TRUE(emu::led_pixel(14, r, g, b));
  TEST_ASSERT_TRUE_MESSAGE(r > 0 || g > 0 || b > 0, "the bubble did not move");
  TEST_ASSERT_TRUE(emu::led_pixel(12, r, g, b));
  TEST_ASSERT_TRUE_MESSAGE(r == 0 && g == 0 && b == 0, "two bubbles at once");

  // Three taps are the way out, and the panel goes dark with the app (docs/30).
  // Three and not two: the gesture counts presses and this app has had none yet.
  capture_begin();
  for (int tap = 0; tap < 3; ++tap) {
    emu::press_button(true);
    pump_turns(10);
    emu::press_button(false);
    pump_turns(10);
  }
  capture_end(nullptr, 0);
  TEST_ASSERT_FALSE_MESSAGE(kernel::app_running(), "three taps did not close it");
  pump_captured(20, g_out, sizeof(g_out));
  for (uint32_t light = 0; light < 25; ++light) {
    TEST_ASSERT_TRUE(emu::led_pixel(light, r, g, b));
    TEST_ASSERT_TRUE_MESSAGE(r == 0 && g == 0 && b == 0,
                             "a light of the panel outlived its app");
  }

  storage->remove("/apps/tilt-app/tilt.lua");
  storage->remove("/apps/tilt-app/manifest.json");
  storage->remove("/apps/tilt-app");
}

// A sensor that is not there is an error and not a number: an app cannot be left
// believing it read 0 g and is level (docs/30). The board without a sensor says
// E_UNSUPPORTED and this bench cannot walk that branch —it always has one— so what
// is measured here is the chip that stops answering.
void test_a_sensor_that_does_not_answer_is_said(void) {
  TEST_ASSERT_TRUE(boot_once());
  // A sensor is the Matrix's; on a board without one there is nothing to answer (docs/50).
  use_board("m5stack-atom-matrix");
  emu::Options opts = emu::options();
  opts.imu_fail = true;
  emu::set_options(opts);

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "nosensor",
                           "{\"title\":\"No sensor\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local s, err = imu.read()\n"
                           "console.print('imu ' .. tostring(s) .. ' '\n"
                           "  .. tostring(err and err.code))\n"));

  TEST_ASSERT_TRUE(run_captured("run nosensor", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "imu nil E_IO"), g_out);

  storage->remove("/apps/nosensor/app.lua");
  storage->remove("/apps/nosensor/manifest.json");
  storage->remove("/apps/nosensor");

  opts.imu_fail = false;
  emu::set_options(opts);
}


// **A call that returns a promise is over when the promise settles**, not when it returns (docs/30): an
// app that asks for fifty milliseconds pays fifty milliseconds in the meter of that call, and the eight
// microseconds the C function took to come back are not the number anyone wants.
void test_a_call_that_returns_a_promise_is_timed_until_it_settles(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);

  static const char kManifest[] =
      "{\"name\":\"waits\",\"title\":\"waits\",\"version\":\"0.1\",\"boards\":[\"esp32\"],"
      "\"entry\":\"waits.lua\"}";
  static const char kSource[] = "sys.delay(50):await()\n";
  // The entry has to be the name the manifest declares, or the app is refused before it runs and the
  // meter has nothing to show (docs/40).
  TEST_ASSERT_TRUE(add_app_file(storage, "waits", kManifest, kSource, "waits.lua"));

  kernel::meter::reset();
  TEST_ASSERT_TRUE(run_captured("run waits", g_out, sizeof(g_out)));
  // One call out of the app, and the fifty milliseconds it waited are in it — **and not an hour**: a
  // promise whose start field held rubbish put one and a half hours into this meter from five calls
  // (measured on the board, 1/11), which is why the ceiling is half of the assertion.
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1, kernel::meter::calls(kernel::meter::kLuaCall));
  const uint64_t spent = kernel::meter::us(kernel::meter::kLuaCall);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(40000, static_cast<uint32_t>(spent));
  TEST_ASSERT_LESS_THAN_UINT32(2000000, static_cast<uint32_t>(spent));
}

int main(int argc, char** argv) {  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_an_app_runs_and_writes);
  RUN_TEST(test_a_failing_app_says_why_and_the_shell_survives);
  RUN_TEST(test_the_exit_code_reaches_the_shell);
  RUN_TEST(test_open_hands_the_file_to_the_app);
  RUN_TEST(test_a_file_without_a_handler_is_refused);
  RUN_TEST(test_two_handlers_ask_every_time);
  RUN_TEST(test_the_question_is_the_only_prompt);
  RUN_TEST(test_a_long_path_reaches_the_app);
  RUN_TEST(test_a_path_with_spaces_reaches_the_app_in_quotes);
  RUN_TEST(test_an_app_asks_what_is_mounted_and_how_much_room_is_left);
  RUN_TEST(test_an_app_connects_without_stopping_the_kernel);
  RUN_TEST(test_net_connect_gives_up_at_the_deadline);
  RUN_TEST(test_a_second_connect_says_busy);
  RUN_TEST(test_an_alarm_reaches_the_app_that_owns_it);
  RUN_TEST(test_a_cancelled_alarm_never_fires);
  RUN_TEST(test_alarm_caps_and_a_payload_that_cannot_be_kept);
  RUN_TEST(test_an_alarm_for_an_app_that_is_not_loaded_runs_in_a_window);
  RUN_TEST(test_an_alarm_with_raise_puts_the_app_in_front);
  RUN_TEST(test_the_key_value_store_keeps_bytes);
  RUN_TEST(test_a_persistent_alarm_comes_back_after_a_reboot);
  RUN_TEST(test_raise_calls_the_goodbye_hooks_of_the_app_in_front);
  RUN_TEST(test_the_nvs_mount_shows_the_keys_as_files);
  RUN_TEST(test_an_app_reads_its_own_resources_with_fs);
  RUN_TEST(test_an_app_writes_inside_its_own_ground);
  RUN_TEST(test_a_write_to_raw_memory_goes_in_place);
  RUN_TEST(test_the_settings_of_an_app_go_behind_the_door_of_cfg);
  RUN_TEST(test_a_card_in_the_slot_is_at_sd_and_an_app_may_only_read_it);
  RUN_TEST(test_the_faces_of_the_disk_are_at_fonts_for_every_app);
  RUN_TEST(test_the_settings_of_an_app_go_to_the_card_when_there_is_one);
  RUN_TEST(test_the_battery_curve_falls_and_is_clamped);
  RUN_TEST(test_the_battery_reaches_the_app_as_a_measurement);
  RUN_TEST(test_the_rtc_memory_is_one_file_of_its_size);
  RUN_TEST(test_the_last_wake_is_measured_and_kept);
  RUN_TEST(test_the_idle_sleep_is_a_switch);
  RUN_TEST(test_poweroff_cuts_the_power_of_the_board_that_can);
  RUN_TEST(test_a_sleep_cut_short_is_not_a_measurement);
  RUN_TEST(test_the_civil_calendar_round_trips);
  RUN_TEST(test_the_alarm_is_the_hour_and_the_minute);
  RUN_TEST(test_the_clock_chip_picks_the_most_exact_wake_it_has);
  RUN_TEST(test_the_boot_is_not_part_of_the_clocks_error);
  RUN_TEST(test_a_file_is_read_whatever_its_size);
  RUN_TEST(test_an_app_opens_files_and_the_ninth_is_refused);
  RUN_TEST(test_the_handles_demo_app_from_the_disk_runs);
  RUN_TEST(test_the_mtab_demo_app_from_the_disk_runs);
  RUN_TEST(test_the_hexdump_command_shows_the_bytes_of_the_rtc_memory);
  RUN_TEST(test_the_fram_is_one_file_of_its_bytes);
  RUN_TEST(test_the_root_lists_the_memories_as_files);
  RUN_TEST(test_the_extra_memory_is_there_for_any_app);
  RUN_TEST(test_the_heap_demo_app_from_the_disk_runs);
  RUN_TEST(test_the_battery_demo_app_from_the_disk_runs);
  RUN_TEST(test_the_alarm_demos_from_the_disk_run);
  RUN_TEST(test_the_wifi_command_scans_and_lists_the_networks);
  RUN_TEST(test_a_network_with_no_name_is_listed_as_hidden);
  RUN_TEST(test_a_scan_that_finds_nothing_answers_zero);
  RUN_TEST(test_the_wifi_command_adds_and_forgets_a_network);
  RUN_TEST(test_wifi_forget_stops_a_job_that_is_running);
  RUN_TEST(test_the_wifi_command_connects_with_the_stored_network);
  RUN_TEST(test_three_esc_stop_a_radio_command);
  RUN_TEST(test_the_qr_names_the_app_of_the_transport);
  RUN_TEST(test_a_scan_leaves_the_radio_off);
  RUN_TEST(test_ntp_sets_the_clock);
  RUN_TEST(test_ntp_without_a_network_is_refused);
  RUN_TEST(test_the_http_command_downloads_a_page);
  RUN_TEST(test_the_repo_command_shows_the_manifest_of_an_app);
  RUN_TEST(test_the_http_command_saves_a_page_that_does_not_fit);
  RUN_TEST(test_http_without_a_network_is_refused);
  RUN_TEST(test_a_page_on_a_radio_that_is_still_up);
  RUN_TEST(test_an_app_downloads_a_page);
  RUN_TEST(test_the_page_demo_app_from_the_disk_runs);
  RUN_TEST(test_the_http_command_writes_where_it_is_told);
  RUN_TEST(test_the_http_command_refuses_a_place_that_is_not_there);
  RUN_TEST(test_an_app_downloads_a_page_to_a_file);
  RUN_TEST(test_a_call_that_returns_a_promise_is_timed_until_it_settles);
  RUN_TEST(test_the_power_command_says_what_the_board_can_do);
  RUN_TEST(test_the_power_command_moves_the_cpu);
  RUN_TEST(test_the_led_command_paints_and_gives_it_back);
  RUN_TEST(test_the_light_goes_off_when_its_app_goes);
  RUN_TEST(test_an_app_plays_an_effect_while_it_works);
  RUN_TEST(test_an_app_changes_the_system_moments);
  RUN_TEST(test_led_play_refuses_what_it_cannot_do);
  RUN_TEST(test_the_air_is_read_from_the_board_that_has_it);
  RUN_TEST(test_the_light_demo_app_from_the_disk_runs);
  RUN_TEST(test_the_matrix_app_from_the_disk_paints_the_panel);
  RUN_TEST(test_the_tilt_app_of_the_disk_follows_the_sensor);
  RUN_TEST(test_a_sensor_that_does_not_answer_is_said);
  RUN_TEST(test_an_app_belongs_to_the_boards_it_declares);
  RUN_TEST(test_a_manifest_without_boards_is_not_an_app);
  RUN_TEST(test_the_lua_command_opens_the_interpreter);
  RUN_TEST(test_an_app_for_another_board_is_listed_and_refused);
  RUN_TEST(test_the_bench_is_the_board_it_was_told_to_be);
  RUN_TEST(test_every_app_of_the_repository_declares_its_boards);
  RUN_TEST(test_the_store_says_how_full_it_is);
  RUN_TEST(test_a_press_reaches_the_app_as_a_promise);
  RUN_TEST(test_a_present_with_a_mode_paints_with_that_mode);
  RUN_TEST(test_a_button_is_navigated_and_touched);
  RUN_TEST(test_a_press_reaches_the_app_as_a_hook);
  RUN_TEST(test_three_taps_close_the_app);
  RUN_TEST(test_a_key_reaches_the_app_with_its_number);
  RUN_TEST(test_the_board_says_where_its_keys_are);
  RUN_TEST(test_three_taps_of_a_side_key_keep_the_app);
  RUN_TEST(test_the_dial_walks_the_buttons_of_a_page);
  RUN_TEST(test_the_dial_flips_a_checkbox);
  RUN_TEST(test_the_dial_moves_the_value_of_a_slider);
  RUN_TEST(test_a_panel_arranges_the_page_and_the_dial_changes_the_tab);
  RUN_TEST(test_a_list_turns_its_page_with_the_dial);
  RUN_TEST(test_the_keyboard_types_into_the_box_it_was_given);
  RUN_TEST(test_the_keyboard_takes_its_band_out_of_the_view_the_app_is_given);
  RUN_TEST(test_a_dialog_answers_with_the_door_the_dial_is_on);
  RUN_TEST(test_a_menu_skips_the_option_the_app_hid);
  RUN_TEST(test_a_menu_bar_answers_with_the_title_and_the_entry);
  RUN_TEST(test_a_box_of_lines_types_and_the_enter_writes_a_break);
  RUN_TEST(test_a_selector_opens_the_list_the_app_puts_up);
  RUN_TEST(test_the_cross_of_a_notice_dismisses_the_newest);
  RUN_TEST(test_a_grid_walks_its_cells_and_pages_itself);
  RUN_TEST(test_the_keys_demo_app_from_the_disk_runs);
  RUN_TEST(test_an_app_asks_for_the_processor_speed);
  RUN_TEST(test_a_light_sleep_comes_back_with_the_clock_moved);
  RUN_TEST(test_an_app_sleeps_deeply_and_comes_back_where_it_was);
  RUN_TEST(test_the_sleepy_demo_app_from_the_disk_runs);
  RUN_TEST(test_an_app_writes_the_file_the_prompt_named_and_no_other);
  RUN_TEST(test_a_big_file_is_read_in_chunks);
  RUN_TEST(test_a_failing_app_says_its_error_once);
  RUN_TEST(test_an_app_provisions_and_then_forgets);
  RUN_TEST(test_provisioning_gives_up_when_nobody_arrives);
  UNITY_END();
  return 0;
}
