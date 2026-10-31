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
#include "emu/options.h"
#include "hal/hal.h"
#include "json/json.h"
#include "kernel/apps.h"
#include "kernel/kernel.h"
#include "kernel/power.h"
#include "kernel/shell.h"
#include "kernel/vfs.h"
#include "pal/pal.h"

namespace {
void close_any_app();
}  // namespace

void setUp(void) {}

void tearDown(void) {
  // A test that fails halfway leaves its app running, and the next test then says
  // "cannot start the interpreter" and blames code that is fine — found by breaking
  // the fake sensor on purpose (31/10). Three taps are the system's way out and a
  // person's too, so every test leaves the way a person would (docs/30).
  close_any_app();
}

namespace {

const char kRoot[] = "/tmp/lunok-app-tests";
const char kSinkPath[] = "/tmp/lunok-app-tests.out";
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

// Runs one line with the console sent to a file, so what the app writes can be
// read back. through_console feeds the bytes the way a person's keyboard does
// (echo, prompt), instead of calling execute() directly. out receives the text;
// false when the app never ended.
bool feed_captured(const char* line, bool through_console, char* out, size_t cap) {
  if (out != nullptr && cap > 0) out[0] = '\0';

  const int saved = dup(STDOUT_FILENO);
  const int sink = open(kSinkPath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (saved < 0 || sink < 0) return false;
  dup2(sink, STDOUT_FILENO);

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

  dup2(saved, STDOUT_FILENO);
  close(saved);
  close(sink);

  if (out != nullptr && cap > 0) {
    FILE* file = fopen(kSinkPath, "r");
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

  const int saved = dup(STDOUT_FILENO);
  const int sink = open(kSinkPath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (saved < 0 || sink < 0) return;
  dup2(sink, STDOUT_FILENO);

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

  dup2(saved, STDOUT_FILENO);
  close(saved);
  close(sink);

  if (out != nullptr && cap > 0) {
    FILE* file = fopen(kSinkPath, "r");
    if (file != nullptr) {
      const size_t got = fread(out, 1, cap - 1, file);
      out[got] = '\0';
      fclose(file);
    }
  }
}

// The console of a call that is not a command: a boot that brings an app back is a
// function and not a line of the shell, and what it says has to be read too.
int g_capture_saved = -1;
int g_capture_sink = -1;

void capture_begin() {
  g_capture_saved = dup(STDOUT_FILENO);
  g_capture_sink = open(kSinkPath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (g_capture_saved >= 0 && g_capture_sink >= 0) dup2(g_capture_sink, STDOUT_FILENO);
}

void capture_end(char* out, size_t cap) {
  if (g_capture_saved >= 0) {
    dup2(g_capture_saved, STDOUT_FILENO);
    close(g_capture_saved);
  }
  if (g_capture_sink >= 0) close(g_capture_sink);
  g_capture_saved = -1;
  g_capture_sink = -1;
  if (out == nullptr || cap == 0) return;
  out[0] = '\0';
  FILE* file = fopen(kSinkPath, "r");
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

// open hands the file to the app that declares the extension.
void test_open_hands_the_file_to_the_app(void) {
  TEST_ASSERT_TRUE(boot_once());
  TEST_ASSERT_TRUE(run_captured("open res/hola.txt", g_out, sizeof(g_out)));
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

// fs.* works from inside an app, on the real VFS: the kernel installed the
// operations at boot, and an app never learns what a VFS is (docs/70).
void test_an_app_reads_a_file_with_fs(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "reader",
                            "{\"title\":\"Reader\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                            "local s = fs.read('res/hola.txt')\n"
                            "local st = fs.stat('res/hola.txt')\n"
                            "console.print('read ' .. #s .. ' size ' .. st.size)\n"
                            "console.print('first ' .. fs.read_at('res/hola.txt', 0, 3))\n"
                            "local entries = fs.list('res')\n"
                            "console.print('entries ' .. #entries)\n"));

  TEST_ASSERT_TRUE(run_captured("run reader", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "read 10 size 10"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "first una"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "entries "), g_out);

  storage->remove("/apps/reader/app.lua");
  storage->remove("/apps/reader/manifest.json");
  storage->remove("/apps/reader");
}

// Writing: an app writes inside its own ground, atomically, and the rules of
// docs/70 hold: nothing outside /data/<id>/, no leftovers, and the quota says no
// before anything is touched.
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
                            "for i = 1, 4 do\n"
                            "  assert(fs.write(fs.data('c' .. i .. '.bin'), string.rep('a', 15000)))\n"
                            "end\n"
                            "local big, err3 = fs.write(fs.data('c5.bin'), string.rep('a', 15000))\n"
                            "console.print('quota ' .. tostring(big) .. ' ' .. tostring(err3 and err3.code))\n"));

  TEST_ASSERT_TRUE(run_captured("run writer", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "ground /data/writer"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "wrote 8 back hola mon"), g_out);
  // One entry: the file, and no .tmp left behind.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "entries 1"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "outside nil E_INVAL"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "other nil E_INVAL"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "quota nil E_NOSPC"), g_out);

  // And the ground is where the system said: on the disk, under data/writer.
  hal::Stat info;
  TEST_ASSERT_TRUE(storage->stat("data/writer/t.txt", info));
  TEST_ASSERT_EQUAL_UINT32(8, info.size);

  storage->remove("data/writer/t.txt");
  storage->remove("data/writer");
  storage->remove("/apps/writer/app.lua");
  storage->remove("/apps/writer/manifest.json");
  storage->remove("/apps/writer");
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
// six mounts of the board, and a room that is a number when the backend can say
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
                            "console.print('room ' .. tostring(s) .. ' '\n"
                            "  .. tostring(err and err.code))\n"
                            "local n, err2 = fs.space('/nvs')\n"
                            "console.print('nvs room ' .. tostring(n) .. ' '\n"
                            "  .. tostring(err2 and err2.code))\n"));

  TEST_ASSERT_TRUE(run_captured("run mtab", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "mounts 6"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "/data littlefs true"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "/nvs nvs false"), g_out);
  // The disk of this band is the host's, and it says it cannot report room; the
  // board says numbers for /data (docs/85 and docs/70). Both are honest, and the
  // test says which one it expects here instead of accepting either.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "room nil E_UNSUPPORTED"), g_out);
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
  TEST_ASSERT_FALSE_MESSAGE(kernel::shell::native_running(), "the command never ended");
}

// A page bigger than the room the command gave it is refused and not cut: half a
// page reads as a page that is half as long (docs/30).
void test_the_http_command_refuses_a_page_that_does_not_fit(void) {
  TEST_ASSERT_TRUE(boot_once());
  static char big[20000];
  memset(big, 'x', sizeof(big) - 1);
  big[sizeof(big) - 1] = '\0';

  emu::Options opts = emu::options();
  opts.wifi_absent = false;
  opts.wifi_ms = 100;
  opts.http_ms = 50;
  opts.http_body = big;
  emu::set_options(opts);

  pump_captured(20, g_out, sizeof(g_out), "wifi forget");
  pump_captured(400, g_out, sizeof(g_out), "wifi add casa secret");
  pump_captured(400, g_out, sizeof(g_out), "http https://example.com/gran");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "E_TOOBIG"), g_out);
  TEST_ASSERT_FALSE_MESSAGE(kernel::shell::native_running(), "the command never ended");
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
      0, read_host_file("data/apps/page/manifest.json", manifest, sizeof(manifest)));
  TEST_ASSERT_GREATER_THAN(0,
                           read_host_file("data/apps/page/page.lua", source, sizeof(source)));
  TEST_ASSERT_TRUE(add_app_file(storage, "page-app", manifest, source, "page.lua"));

  emu::Options opts = emu::options();
  opts.wifi_absent = false;
  opts.wifi_ms = 100;
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

// A page asked for while the radio is still up from the job before it: the
// request must not be started a second time when its answer comes. Started twice,
// the second start found the first one in the way and the person saw `E_BUSY`
// with a network that was working (measured on the PC with the batch mode, 31/10).
void test_a_page_on_a_radio_that_is_still_up(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.wifi_absent = false;
  opts.wifi_ms = 100;
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
      0, read_host_file("data/apps/light/manifest.json", manifest, sizeof(manifest)));
  TEST_ASSERT_GREATER_THAN(
      0, read_host_file("data/apps/light/light.lua", source, sizeof(source)));
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
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);

  static char manifest[256];
  static char source[8192];
  TEST_ASSERT_GREATER_THAN(
      0, read_host_file("data/apps/matrix/manifest.json", manifest, sizeof(manifest)));
  const size_t source_len =
      read_host_file("data/apps/matrix/matrix.lua", source, sizeof(source));
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

// An app of another board is not hidden: `apps` lists it with what it needs, and
// `run` refuses it by name, says which boards it wants and which band this is.
// Losing an app without a word is exactly what this list is here to avoid
// (docs/40).
void test_an_app_for_another_board_is_listed_and_refused(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "elsewhere",
                           "{\"title\":\"Elsewhere\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"m5stack-atom\"]}",
                           "console.print('elsewhere')\n"));

  // Listed, with the board it wants.
  pump_captured(20, g_out, sizeof(g_out), "apps");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "elsewhere"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "[needs m5stack-atom]"), g_out);

  // And refused before a VM is built, saying both things. The refusal is the
  // command's own answer and not "the app ended": a run that never started has
  // ended too, and only one of the two is a failure worth reporting.
  capture_begin();
  const bool refused = kernel::execute("run elsewhere");
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_FALSE(refused);
  TEST_ASSERT_NOT_NULL_MESSAGE(
      strstr(g_out, "elsewhere runs on m5stack-atom and this board is emulator"), g_out);
  TEST_ASSERT_FALSE(kernel::app_running());

  storage->remove("/apps/elsewhere/app.lua");
  storage->remove("/apps/elsewhere/manifest.json");
  storage->remove("/apps/elsewhere");
}

// Every app of the repository says which boards it runs on. On the PC nothing is
// discarded, so this is the only place where a manifest that forgot the list would
// be seen before a board is in front of somebody (docs/40, docs/85).
void test_every_app_of_the_repository_declares_its_boards(void) {
  static const char* const kParents[] = {"", "../", "../../", "../../../"};
  static char text[1024];
  uint32_t seen = 0;
  for (size_t p = 0; p < sizeof(kParents) / sizeof(kParents[0]); ++p) {
    char dir_path[256];
    snprintf(dir_path, sizeof(dir_path), "%sdata/apps", kParents[p]);
    DIR* dir = opendir(dir_path);
    if (dir == nullptr) continue;
    struct dirent* item = nullptr;
    while ((item = readdir(dir)) != nullptr) {
      if (item->d_name[0] == '.') continue;
      char manifest[256];
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
      ++seen;
    }
    closedir(dir);
    break;
  }
  // Eighteen apps ship today: a directory that was not found would pass every
  // check above by not running any of them.
  TEST_ASSERT_TRUE_MESSAGE(seen >= 17, "the apps of the repository were not found");
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

// Where the keys are is part of the answer: an app gives a function to a position,
// and the board says it in the same words on every board (docs/30, docs/50).
void test_the_board_says_where_its_keys_are(void) {
  TEST_ASSERT_TRUE(boot_once());
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
  // The bench imitates the Paper: one compound dial of three keys on the left
  // face, with the middle one in the middle (docs/85).
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "0 left/centre compound 0"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "1 left/left compound 0"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "2 left/right compound 0"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "middle left/centre"), g_out);
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
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);

  static char manifest[256];
  static char source[8192];
  TEST_ASSERT_GREATER_THAN(
      0, read_host_file("data/apps/keys/manifest.json", manifest, sizeof(manifest)));
  const size_t source_len = read_host_file("data/apps/keys/keys.lua", source, sizeof(source));
  TEST_ASSERT_GREATER_THAN(0, source_len);
  TEST_ASSERT_LESS_THAN(sizeof(source) - 1, source_len);
  TEST_ASSERT_TRUE(add_app_file(storage, "keys-app", manifest, source, "keys.lua"));

  // It is a loop, so it takes turns instead of ending on its own (docs/30).
  pump_captured(20, g_out, sizeof(g_out), "run keys-app");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "keys: 3"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "value 0"), g_out);
  TEST_ASSERT_TRUE(kernel::app_running());

  // One press of the right key goes up one.
  capture_begin();
  emu::press_key(2, true);
  pump_turns(20);
  emu::press_key(2, false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "up 1"), g_out);

  // And held down it repeats: the first repeat comes half a second later, so a
  // second is enough to see two more steps and a short press is one.
  capture_begin();
  emu::press_key(2, true);
  pump_turns(900);
  emu::press_key(2, false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "up 2"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "up 3"), g_out);

  // The left key goes down.
  capture_begin();
  emu::press_key(1, true);
  pump_turns(20);
  emu::press_key(1, false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "down 2"), g_out);

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

// A light sleep comes back in the same turn, the clock has moved by the hour it
// was given, and the board says how far from that hour it woke (docs/90).
void test_a_light_sleep_comes_back_with_the_clock_moved(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IClock* clock = hal::platform().clock();
  TEST_ASSERT_NOT_NULL(clock);
  const int64_t epoch = 1767225600000LL;  // 2026-01-01 00:00:00 UTC
  TEST_ASSERT_TRUE(clock->set_epoch_ms(epoch));

  pump_captured(20, g_out, sizeof(g_out), "sleep light 1000");
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
      0, read_host_file("data/apps/sleepy/manifest.json", manifest, sizeof(manifest)));
  TEST_ASSERT_GREATER_THAN(
      0, read_host_file("data/apps/sleepy/sleepy.lua", source, sizeof(source)));
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
      0, read_host_file("data/apps/mtab/manifest.json", manifest, sizeof(manifest)));
  TEST_ASSERT_GREATER_THAN(0, read_host_file("data/apps/mtab/mtab.lua", source, sizeof(source)));
  TEST_ASSERT_TRUE(add_app_file(storage, "mtab-app", manifest, source, "mtab.lua"));

  TEST_ASSERT_TRUE(run_captured("run mtab-app", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "mounts: 6"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "/data littlefs rw  unknown (E_UNSUPPORTED)"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "/nvs nvs ro  unknown (E_UNSUPPORTED)"), g_out);

  storage->remove("/apps/mtab-app/mtab.lua");
  storage->remove("/apps/mtab-app/manifest.json");
  storage->remove("/apps/mtab-app");
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
      0, read_host_file("data/apps/handles/manifest.json", manifest, sizeof(manifest)));
  TEST_ASSERT_GREATER_THAN(
      0, read_host_file("data/apps/handles/handles.lua", source, sizeof(source)));
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
      0, read_host_file("data/apps/tilt/manifest.json", manifest, sizeof(manifest)));
  const size_t source_len =
      read_host_file("data/apps/tilt/tilt.lua", source, sizeof(source));
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
  RUN_TEST(test_an_app_reads_a_file_with_fs);
  RUN_TEST(test_an_app_writes_inside_its_own_ground);
  RUN_TEST(test_an_app_opens_files_and_the_ninth_is_refused);
  RUN_TEST(test_the_handles_demo_app_from_the_disk_runs);
  RUN_TEST(test_the_mtab_demo_app_from_the_disk_runs);
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
  RUN_TEST(test_the_http_command_refuses_a_page_that_does_not_fit);
  RUN_TEST(test_http_without_a_network_is_refused);
  RUN_TEST(test_a_page_on_a_radio_that_is_still_up);
  RUN_TEST(test_an_app_downloads_a_page);
  RUN_TEST(test_the_page_demo_app_from_the_disk_runs);
  RUN_TEST(test_the_power_command_says_what_the_board_can_do);
  RUN_TEST(test_the_power_command_moves_the_cpu);
  RUN_TEST(test_the_led_command_paints_and_gives_it_back);
  RUN_TEST(test_the_light_goes_off_when_its_app_goes);
  RUN_TEST(test_the_light_demo_app_from_the_disk_runs);
  RUN_TEST(test_the_matrix_app_from_the_disk_paints_the_panel);
  RUN_TEST(test_the_tilt_app_of_the_disk_follows_the_sensor);
  RUN_TEST(test_a_sensor_that_does_not_answer_is_said);
  RUN_TEST(test_an_app_belongs_to_the_boards_it_declares);
  RUN_TEST(test_a_manifest_without_boards_is_not_an_app);
  RUN_TEST(test_the_lua_command_opens_the_interpreter);
  RUN_TEST(test_an_app_for_another_board_is_listed_and_refused);
  RUN_TEST(test_every_app_of_the_repository_declares_its_boards);
  RUN_TEST(test_a_press_reaches_the_app_as_a_promise);
  RUN_TEST(test_a_press_reaches_the_app_as_a_hook);
  RUN_TEST(test_three_taps_close_the_app);
  RUN_TEST(test_a_key_reaches_the_app_with_its_number);
  RUN_TEST(test_the_board_says_where_its_keys_are);
  RUN_TEST(test_three_taps_of_a_side_key_keep_the_app);
  RUN_TEST(test_the_keys_demo_app_from_the_disk_runs);
  RUN_TEST(test_an_app_asks_for_the_processor_speed);
  RUN_TEST(test_a_light_sleep_comes_back_with_the_clock_moved);
  RUN_TEST(test_an_app_sleeps_deeply_and_comes_back_where_it_was);
  RUN_TEST(test_the_sleepy_demo_app_from_the_disk_runs);
  RUN_TEST(test_a_failing_app_says_its_error_once);
  RUN_TEST(test_an_app_provisions_and_then_forgets);
  RUN_TEST(test_provisioning_gives_up_when_nobody_arrives);
  UNITY_END();
  return 0;
}
