// Whole apps, end to end: a real app on a real (emulated) disk, started the way
// the shell starts it, with what it writes captured and checked.
//
// This is the level docs/80 calls "app": the manifest, the interpreter, the
// promises, the console and the exit code working together. A unit test can be
// green while an app does not run; this one cannot.
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <unity.h>

#include "alarms/alarms.h"
#include "emu/gt911.h"
#include "emu/memories.h"
#include "emu/model.h"
#include "emu/options.h"
#include "emu/surface.h"
#include "hal/hal.h"
#include "img/img.h"
#include "json/json.h"
#include "kernel/apps.h"
#include "kernel/calendar.h"
#include "kernel/cfg.h"
#include "kernel/kernel.h"
#include "kernel/log.h"
#include "kernel/meter.h"
#include "kernel/session.h"
#include "kernel/splash.h"
#include "luabind/luabind.h"
#include "kernel/power.h"
#include "kernel/shell.h"
#include "kernel/touch.h"
#include "kernel/font.h"
#include "kernel/ui.h"
#include "kernel/vfs.h"
#include "kernel/version.h"
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
  // **The keyboard is a setting of the run**: a test that chose AZERTY leaves QWERTY for the next one, in the object
  // and in the key-value store, so a later boot cannot bring the other one back (0.93.0, docs/30, docs/70).
  kernel::ui::keyboard_select("qwerty");
  kernel::cfg::set_keyboard("qwerty");
  // **The drawing of the logo is a setting of the run too** (0.115.0): a test that chose the clipart leaves the
  // photograph for the next one, in the object and in the store, and a later boot would come up with the other
  // picture without anybody having asked (docs/30, docs/50, docs/70).
  kernel::cfg::set_boot_splash("boot");
  // **And the app the board starts by itself** (0.119.0, found leaking on 8/11): a case that names a home app and then
  // fails has not cleared it, so the next boot of the emulator starts that app —an app running before the case has asked for
  // one, and every "cannot start the interpreter" after it—. The empty name is no home app, which is the emulator's.
  kernel::cfg::set_home_app(nullptr);
  // And the emulator goes back to the ATOM: one binary is one emulator, and a test that was
  // the Matrix would make the next one refuse an app for no reason (docs/85).
  const bool had_card = emu::options().sd_dir != nullptr;
  const bool was_another_board = emu::options().board != nullptr;
  // **The emulator goes back whole and not field by field** (0.151.0): a case that aborted halfway leaves whatever it had
  // turned on —`delays`, `ink_refuse`, `display`, `touch`, a card, a battery— and the case after it inherits that. A
  // glass that refuses paints while an app starts makes the interpreter's own setup raise, and the next case then reads
  // "cannot start the interpreter" and blames code that is fine: it is the leak that hid a dozen cases for days. What is
  // kept is only where this suite keeps its disks.
  const emu::Options was = emu::options();
  // **With its braces**: `emu::Options fresh;` is a plain struct with no constructor, so it is whatever the stack had —
  // and a garbage `dump` in it is a picture of the glass written to a garbage file name on every paint, which is how the
  // root of the repository filled with 518 KB files on 8/11. Every field has a default now (docs/85), and this says so.
  emu::Options fresh{};
  fresh.fs_dir = was.fs_dir;
  fresh.state_dir = was.state_dir;
  emu::set_options(fresh);
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

// **A face of the repository, in the disk of this emulator** (4/11): the suite keeps its own volume, so what it
// lists has to be written here — `data/res/fonts/` is the disk's `/res/fonts` and the emulator does not read the
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

// **A file of the repository, in the disk of this emulator**: the suite keeps its own volume, so an app that is a page of
// `apps/` and not something a test writes by hand comes from there —with the pictures it ships— and not from a copy
// that goes stale (docs/85).
bool copy_a_host_file(hal::IStorage* storage, const char* host, const char* disk) {
  static char bytes[65536];
  const size_t got = read_host_file(host, bytes, sizeof(bytes));
  if (got == 0) return false;
  hal::FileSlot slot;
  if (!storage->open(disk, hal::OpenMode::kWrite, slot)) return false;
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
  emu::Options opts{};
  opts.fs_dir = root();
  emu::set_options(opts);

  if (!pal::init()) return false;
  // **The folders of the disk come before the boot** (0.88.2): `/apps` is a mount only when the flash has it, and
  // a disk that carries apps is the one a board boots on (docs/70, docs/85).
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  if (storage == nullptr) return false;
  storage->mkdir("/apps");
  storage->mkdir("/res");
  kernel::boot(hal::platform());
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

// A card in the slot of the emulator, or none. The mount table is built at boot, so a card
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

// The board this test is about: a board is chosen and never assumed, because the emulator answers with
// the hardware of the model it was told to be, and a test that expects a gauge on an ATOM is a test
// that would pass on a board nobody has (docs/50, docs/85).
// **The board comes with its parts** (0.152.4): the binary gives a model the glass it has —`Options` has it off because a
// board nobody looks at does not read a bus once a turn— and this helper gave the name and nothing else, so a case that
// ran an app that presents was running it on a board with no screen and the app died in `ui.present`. Same rule as the
// binary's (`docs/85`), and a case that wants a board without one turns it off itself.
void use_board(const char* model) {
  const emu::Model hw = emu::model_of(model);
  emu::Options opts = emu::options();
  opts.board = model;
  if (hw.glass) opts.display = true;
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
// **A capture is a capture of the log, and the log's mirror is asked for here** (0.169.2): an app test turns the mirror off
// when it has read what it needed, and the next case that captures then reads an empty window and blames the app —it was
// the order the tests happened to run in—. Asking for it at the door makes every case stand on its own (found by this
// suite, 9/11).
// **The level of a pixel of the page**, in the nibble packing the kernel writes: it is how a case of this suite says what a
// person sees and not only what a control was asked to draw (docs/80, docs/90).
uint8_t page_level(uint16_t x, uint16_t y) {
  const kernel::ui::Canvas& view = kernel::ui::canvas();
  if (view.bits == nullptr || x >= view.width || y >= view.height) return 0;
  const uint8_t byte = view.bits[static_cast<uint32_t>(y) * view.stride + x / 2];
  return ((x & 1) == 0) ? static_cast<uint8_t>(byte >> 4) : static_cast<uint8_t>(byte & 0x0F);
}

// **What the glass was asked to paint**, in pixels: every `paint x,y wxh` line of the kernel's console, added up. It is
// how a case says «this gesture cost this much ink» instead of «this call happened», and the difference between the two is
// the whole law of the damage (docs/80, docs/90).
int32_t painted_pixels(const char* log) {
  int32_t total = 0;
  const char* at = log;
  while (at != nullptr && (at = strstr(at, "paint ")) != nullptr) {
    at += 6;
    unsigned x = 0;
    unsigned y = 0;
    unsigned w = 0;
    unsigned h = 0;
    if (sscanf(at, "%u,%u %ux%u", &x, &y, &w, &h) == 4) {
      total += static_cast<int32_t>(w * h);
    }
  }
  return total;
}

void capture_begin() {
  kernel::log_echo(true);
  console_take_away();
}

void capture_end(char* out, size_t cap) {
  console_give_back(false);
  if (out == nullptr || cap == 0) return;
  out[0] = '\0';
  FILE* file = fopen(kConsolePath, "r");
  if (file == nullptr) return;
  const size_t got = fread(out, 1, cap - 1, file);
  out[got] = '\0';
  // **A capture that did not fit says so** (0.157.0): the end of a console that was cut is missing, and a case that
  // cannot find a line then looks like an app that did not answer. The marker is at the end of what did fit, because the
  // beginning of a capture is what most cases read.
  const bool more = fgetc(file) != EOF;
  fclose(file);
  if (more) {
    const char* mark = "\n[this capture was longer than the buffer of the case]\n";
    const size_t mark_len = strlen(mark);
    if (got + mark_len < cap) {
      memcpy(out + got, mark, mark_len + 1);
    } else {
      memcpy(out + cap - 1 - mark_len, mark, mark_len + 1);
    }
  }
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
// **The console of a capture is read into this**, and **sixty-four kilobytes and not eight** (0.157.0): a case that
// presses twenty keys of the calculator writes a line of the paint for every press, and eight kilobytes cut the console
// where the results are —the case passed or failed by how many paints the timer let in, which is the worst kind of red—
// (found on the 8/11 flaking in a full run of the suite).
char g_out[65536];

// **What a file of the disk carries**, for a test that says a download landed as it was and not as a name (docs/70).
bool read_text(hal::IStorage* storage, const char* path, char* out, size_t cap) {
  if (out == nullptr || cap < 2) return false;
  out[0] = '\0';
  hal::FileSlot slot;
  if (!storage->open(path, hal::OpenMode::kRead, slot)) return false;
  const int32_t got = storage->read(slot, out, static_cast<uint32_t>(cap - 1));
  storage->close(slot);
  if (got < 0) return false;
  out[got] = '\0';
  return true;
}

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

// **The emulator's options start empty** (0.151.1): `emu::Options` is a plain struct with no constructor, and a `dump`
// that is not nullptr is a picture of the glass —518 415 bytes of it— written to whatever name it holds, once per
// paint. An uninitialised one filled the root of the repository with files on 8/11 (docs/85). The defaults are the
// guard; this is the case that says the guard is still there. It needs no boot: it is the struct, nothing else.
void test_the_options_of_the_emulator_start_empty(void) {
  const emu::Options one{};
  TEST_ASSERT_NULL(one.dump);
  // **`fs_dir` is not null and never was** (0.152.3): the constructor of the emulator gives it `"emulator"`, which is the
  // folder the emulated disks live in when nobody says otherwise, and this test asserted null from the day it was
  // written —it was red from 0.151.1 and I did not see it, which is what a suite of fifteen reds hides—. What the case is
  // about is that **no field comes out as rubbish**, not that every one is zero.
  TEST_ASSERT_EQUAL_STRING("emulator", one.fs_dir);
  TEST_ASSERT_NULL(one.state_dir);
  TEST_ASSERT_NULL(one.flash_dir);
  TEST_ASSERT_NULL(one.sd_dir);
  TEST_ASSERT_NULL(one.board);
  TEST_ASSERT_NULL(one.script);
  TEST_ASSERT_NULL(one.run_app);
  TEST_ASSERT_EQUAL_INT32(-1, one.baud);
  TEST_ASSERT_EQUAL_INT32(0, one.ink);
  TEST_ASSERT_EQUAL_UINT32(1, one.time_scale);
  TEST_ASSERT_EQUAL_UINT32(0, one.seed);
  TEST_ASSERT_FALSE(one.verbose);
  TEST_ASSERT_FALSE(one.display);
  TEST_ASSERT_FALSE(one.touch);
  TEST_ASSERT_FALSE(one.delays);
  TEST_ASSERT_FALSE(one.ink_refuse);
  TEST_ASSERT_FALSE(one.persist);
  // **And the sensors of a board that is lying still**: a g downwards, twenty-five degrees and half the air, which is what
  // the constructor of this emulator has always given them —the header leaves those nine to it on purpose— and what an app
  // asking an IMU nobody configured has to find (docs/85).
  TEST_ASSERT_EQUAL_FLOAT(0.0f, one.imu_ax);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, one.imu_az);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, one.imu_gy);
  TEST_ASSERT_EQUAL_FLOAT(25.0f, one.imu_celsius);
  TEST_ASSERT_EQUAL_FLOAT(25.0f, one.env_celsius);
  TEST_ASSERT_EQUAL_FLOAT(50.0f, one.env_humidity);
}

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

// **`fs.write` on raw memory goes in place and not to a temporary file** (0.65.1): `/rtc` is *one* file and it has no
// `/rtc.tmp` to write to and then rename, so the atomic path answered `E_NOENT` —the `no such file or directory` that
// `run hexedit /fram` saw when saving (4/11). What is written is what is read, the rest of the memory is left at zero and
// what does not fit whole is `E_NOSPC` without touching anything (docs/50, docs/70).
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
  // **A write that does not fit leaves nothing half done**: 8 000 B in a memory of 7 168 is a no.
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
// is the point of having a card on the emulator (docs/70, docs/85).
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

// **The same app on the two disks is listed twice and each one runs** (0.164.0, and 0.88.20 before it): `/apps` is the
// one door of the apps and it presents the copy of the flash as `twice` and the copy of the card as **`*twice`** —the
// mark of a redirect, with the word of its unit at the end of the name, `Card twice-SD`— instead of one of them
// disappearing behind the other. `root_of` answers the door for either of them, the disk that answers is the one the door
// resolved —which is what `[card]` says in the listing—, and an app's own `res/` follows the same name to the same disk.
// **The mark is a folder that exists**: `/apps/*twice/res/...` is the card's `res/`, which is what the launcher asks for
// when it draws the picture of a cell and what `-SD` on the id could never answer (docs/40, docs/70).
void test_an_app_on_the_two_disks_is_listed_twice_and_the_card_one_says_SD(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  make_card();
  char card_path[96];
  snprintf(card_path, sizeof(card_path), "%s/sd", root());
  TEST_ASSERT_TRUE(card_on(card_path));
  hal::IStorage* card = hal::platform().storage("/sd");
  TEST_ASSERT_NOT_NULL(card);

  // The same id on both drives, and each copy says which one it is. The card has no `apps/` until something makes it,
  // which is what `repo install` does with the way of a download (docs/70).
  card->mkdir("/apps");
  TEST_ASSERT_TRUE(add_app(storage, "twice",
                           "{\"title\":\"Flash twice\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "console.print('from the flash')\n"
                           "console.print('res ' .. tostring(fs.read('res/which.txt')))\n"));
  TEST_ASSERT_TRUE(add_app(card, "twice",
                           "{\"title\":\"Card twice\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "console.print('from the card')\n"
                           "console.print('res ' .. tostring(fs.read('res/which.txt')))\n"));
  // **The files of each copy are its own** (docs/70): the two say the same name and the door sends each app to the disk it
  // was started from, which is the whole point of keeping the two apart.
  TEST_ASSERT_TRUE(storage->mkdir("apps/twice/res"));
  TEST_ASSERT_TRUE(write_text(storage, "apps/twice/res/which.txt", "flash-res\n"));
  TEST_ASSERT_TRUE(card->mkdir("/apps/twice/res"));
  TEST_ASSERT_TRUE(write_text(card, "/apps/twice/res/which.txt", "card-res\n"));

  char root[96];
  TEST_ASSERT_TRUE(kernel::apps::root_of("twice", root, sizeof(root)));
  TEST_ASSERT_EQUAL_STRING("/apps/twice", root);
  TEST_ASSERT_TRUE(kernel::apps::root_of("*twice", root, sizeof(root)));
  TEST_ASSERT_EQUAL_STRING("/apps/*twice", root);

  // **The door names the copy with the word of its unit and keeps the mark in the id** (0.164.0): the id is what runs and
  // what a path is built from, and the name is what a person reads.
  kernel::apps::Entry found = {};
  TEST_ASSERT_TRUE(kernel::apps::read("*twice", found));
  TEST_ASSERT_EQUAL_STRING("*twice", found.id);
  TEST_ASSERT_EQUAL_STRING("Card twice-SD", found.title);
  TEST_ASSERT_TRUE_MESSAGE(found.on_card, "the redirected copy is the card's");

  // **And the picture the launcher asks for is a path that answers** (0.164.0): it is the same file the app reads itself,
  // asked through the id the door presented, which is the whole bug the shell app taught the launcher on 8/11.
  char said[64] = {0};
  size_t said_len = 0;
  TEST_ASSERT_TRUE_MESSAGE(kernel::vfs::read_file("/apps/*twice/res/which.txt", said, sizeof(said) - 1, said_len),
                           "/apps/*twice/res is the card's res");
  TEST_ASSERT_EQUAL_STRING("card-res\n", said);

  // **The list names both**: the two names, and the one of the card carries the word of its unit and says it is the
  // card's (docs/40).
  pump_captured(20, g_out, sizeof(g_out), "apps");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "Flash twice"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "Card twice-SD"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "*twice"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "[card]"), g_out);

  // **And each name runs its own copy**, with its own `res/`.
  TEST_ASSERT_TRUE(run_captured("run twice", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "from the flash"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "res flash-res"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "from the card"), g_out);
  TEST_ASSERT_TRUE(run_captured("run *twice", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "from the card"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "res card-res"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "from the flash"), g_out);

  // **An app that is not there says where the door is** (0.88.20): the one path, and not a path of a disk that the system
  // did not offer (docs/40).
  TEST_ASSERT_TRUE(run_captured("run nadie_ha_vist_aquesta", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "no such app: nadie_ha_vist_aquesta"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "apps are in /apps"), g_out);

  // **The card out of the slot takes the redirected id with it**: the flash app is still there and still runs (docs/70).
  card->remove("/apps/twice/res/which.txt");
  card->remove("/apps/twice/res");
  card->remove("/apps/twice/app.lua");
  card->remove("/apps/twice/manifest.json");
  card->remove("/apps/twice");
  TEST_ASSERT_TRUE(run_captured("run *twice", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "no such app: *twice"), g_out);
  TEST_ASSERT_TRUE(run_captured("run twice", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "from the flash"), g_out);

  storage->remove("apps/twice/res/which.txt");
  storage->remove("apps/twice/res");
  storage->remove("/apps/twice/app.lua");
  storage->remove("/apps/twice/manifest.json");
  storage->remove("/apps/twice");
}

// **A name that already says which unit it is from keeps its word** (0.166.2): the rule the owner gave is that an id with
// a mark in front is read by the last word behind a dash of the app's name —`Editor-SD` is the card's copy—, so a card app
// titled `Twice-SD` is not renamed `Twice-SD-SD`, and one titled `Twice-Littlefs` while it lives on the card keeps the
// name it was given and says in the log that the door answered the other disk: the name is what a person reads and
// hiding the disagreement would be the lie (docs/40, docs/70).
void test_a_name_that_already_says_its_unit_keeps_its_word(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  make_card();
  char card_path[96];
  snprintf(card_path, sizeof(card_path), "%s/sd", root());
  TEST_ASSERT_TRUE(card_on(card_path));
  hal::IStorage* card = hal::platform().storage("/sd");
  TEST_ASSERT_NOT_NULL(card);
  card->mkdir("/apps");

  TEST_ASSERT_TRUE(add_app(storage, "twice",
                           "{\"title\":\"Flash twice\",\"version\":\"0.1.0\",\"entry\":\"app.lua\","
                           "\"boards\":[\"esp32\"]}",
                           "console.print('from the flash')\n"));
  // The card's copy says the word of its own unit, which is the one the door would have written by itself.
  TEST_ASSERT_TRUE(add_app(card, "twice",
                           "{\"title\":\"Card twice-SD\",\"version\":\"0.1.0\",\"entry\":\"app.lua\","
                           "\"boards\":[\"esp32\"]}",
                           "console.print('from the card')\n"));

  kernel::apps::Entry found = {};
  TEST_ASSERT_TRUE(kernel::apps::read("*twice", found));
  TEST_ASSERT_EQUAL_STRING_MESSAGE("Card twice-SD", found.title, "a name is not given a second word");
  TEST_ASSERT_TRUE_MESSAGE(found.on_card, "the word of the name and the disk agree");

  // **And a name that disagrees with the disk keeps its word** (0.166.2): the last word behind a dash is the unit the
  // name says, and the log says the door answered the other one instead of the two being silently mixed.
  TEST_ASSERT_TRUE(add_app(card, "twice",
                           "{\"title\":\"Card twice-Littlefs\",\"version\":\"0.1.0\",\"entry\":\"app.lua\","
                           "\"boards\":[\"esp32\"]}",
                           "console.print('from the card')\n"));
  TEST_ASSERT_TRUE(kernel::apps::read("*twice", found));
  TEST_ASSERT_EQUAL_STRING("Card twice-Littlefs", found.title);
  TEST_ASSERT_FALSE_MESSAGE(found.on_card, "the word of the name decides which unit the name says");

  // **A word alone is not a word**: `SD` without a dash in front of it is the name of an app and says nothing about a
  // disk, so the door keeps naming it after the disk it answered from.
  TEST_ASSERT_TRUE(
      add_app(card, "twice",
              "{\"title\":\"Card SD\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
              "console.print('from the card')\n"));
  TEST_ASSERT_TRUE(kernel::apps::read("*twice", found));
  TEST_ASSERT_EQUAL_STRING("Card SD-SD", found.title);
  TEST_ASSERT_TRUE(found.on_card);

  card->remove("/apps/twice/app.lua");
  card->remove("/apps/twice/manifest.json");
  card->remove("/apps/twice");
  storage->remove("/apps/twice/app.lua");
  storage->remove("/apps/twice/manifest.json");
  storage->remove("/apps/twice");
}

// **A manifest of nine boards says it names too many** (0.88.11): `get_strings` refuses the whole list when it does not
// fit, which is right —a list with one board left out would say the app runs where it does not—, and what the shell said
// about it was `%s does not say which boards it runs on`: a lie about a file that says nine of them. The reader says why
// now and how many there were, and both places that read a list of boards —the walk of the disk and the card of
// `repo info`— tell the person what is really in the file (docs/40, docs/75).
void test_a_manifest_of_nine_boards_says_it_names_too_many(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "manyboards",
                           "{\"title\":\"Manyboards\",\"version\":\"0.1.0\",\"entry\":\"app.lua\","
                           "\"boards\":[\"esp32\",\"m5stack-paper\",\"m5stack-atom\",\"one\",\"two\",\"three\","
                           "\"four\",\"five\",\"six\"]}",
                           "console.print('nine')\n"));
  // **The line of the log is the one a person reads**, so it is the one the test reads: the boot mirrors the log to the
  // console and the capture takes it (docs/30).
  kernel::log_echo(true);

  pump_captured(20, g_out, sizeof(g_out), "apps");
  // **The app is not an app** —a list of boards with one of them left out would say it runs where it does not— **and the
  // line says the reason that is true**: nine boards, eight carried (docs/40).
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "Manyboards"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "names 9 boards and this system carries 8"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "does not say which boards"), g_out);

  // **And a card that showed a dash for it would be a card that lies**: `repo info` says how long the list is (docs/30).
  emu::Options opts = emu::options();
  opts.wifi_absent = false;
  opts.wifi_ms = 50;
  opts.http_fake = true;
  opts.http_ms = 20;
  opts.http_status = 200;
  opts.http_body =
      "{\"title\":\"Manyboards\",\"version\":\"0.1.0\",\"entry\":\"app.lua\","
      "\"boards\":[\"esp32\",\"m5stack-paper\",\"m5stack-atom\",\"one\",\"two\",\"three\",\"four\",\"five\",\"six\"]}";
  emu::set_options(opts);
  pump_captured(20, g_out, sizeof(g_out), "wifi forget");
  pump_captured(400, g_out, sizeof(g_out), "wifi add casa secret");
  pump_captured(400, g_out, sizeof(g_out), "repo info manyboards");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "boards      9 in the manifest, more than this system carries"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "boards      --"), g_out);

  storage->remove("/apps/manyboards/app.lua");
  storage->remove("/apps/manyboards/manifest.json");
  storage->remove("/apps/manyboards");
  opts.http_body = nullptr;
  opts.http_fake = false;
  emu::set_options(opts);
}

// **`/apps` is the two disks in one directory** (0.88.19): the flash's apps and the card's in one listing, with the names
// the flash also has presented twice —`both` for the flash's copy and `*both` for the card's— so neither copy disappears
// behind the other (0.88.20, 0.164.0). The mark in front of the id is a folder that exists (`/apps/*both`), which a
// `-SD` on the id never was, and the word of the unit is at the end of the **name** —`Both card-SD`— and not of the id. It
// is the rule `apps` already follows to list and to run, and the door has to answer the same thing: a page that lists one
// app and reads another is a page nobody can trust (docs/40, docs/70).
void test_the_door_of_the_apps_puts_the_card_and_the_flash_together(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  make_card();
  char card_path[96];
  snprintf(card_path, sizeof(card_path), "%s/sd", root());
  TEST_ASSERT_TRUE(card_on(card_path));
  hal::IStorage* card = hal::platform().storage("/sd");
  TEST_ASSERT_NOT_NULL(card);

  TEST_ASSERT_TRUE(add_app(card, "oncard",
                           "{\"title\":\"On card\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "console.print('the one of the card')\n"));
  TEST_ASSERT_TRUE(
      add_app(storage, "onflash",
              "{\"title\":\"On flash\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
              "console.print('the one of the flash')\n"));
  // **The same id on both disks**: the door presents the two of them, `both` for the flash's copy and `*both` for the
  // card's (0.88.20, 0.164.0, docs/40).
  TEST_ASSERT_TRUE(add_app(card, "both",
                           "{\"title\":\"Both card\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "console.print('both, from the card')\n"));
  TEST_ASSERT_TRUE(add_app(storage, "both",
                           "{\"title\":\"Both flash\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "console.print('both, from the flash')\n"));

  // The listing of the door, which is the two of them: the six apps of this suite are on the flash too, so what is
  // counted here is the names this test put there and not the whole list.
  struct Names : public hal::ListSink {
    char rows[32][32] = {};
    uint8_t count = 0;
    bool on_entry(const hal::ListEntry& entry) override {
      if (count < 32 && entry.name != nullptr) snprintf(rows[count], sizeof(rows[0]), "%s", entry.name);
      ++count;
      return true;
    }
  } names;
  TEST_ASSERT_TRUE(kernel::vfs::list("/apps", names));
  int both = 0;
  bool card_one = false;
  bool flash_one = false;
  bool card_suffixed = false;
  for (uint8_t i = 0; i < names.count && i < 32; ++i) {
    if (strcmp(names.rows[i], "both") == 0) ++both;
    if (strcmp(names.rows[i], "*both") == 0) card_suffixed = true;
    if (strcmp(names.rows[i], "oncard") == 0) card_one = true;
    if (strcmp(names.rows[i], "onflash") == 0) flash_one = true;
  }
  TEST_ASSERT_TRUE_MESSAGE(card_one, "the app of the card is not in the door");
  TEST_ASSERT_TRUE_MESSAGE(flash_one, "the app of the flash is not in the door");
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, both, "the app of the flash is not listed once under its own name");
  TEST_ASSERT_TRUE_MESSAGE(card_suffixed, "the app of the card is not listed with the mark in front of its id");

  // **And a path of the door reads the copy its name says**: `/apps/both` is the flash's, `/apps/*both` is the card's,
  // and the one only the flash has is the flash's (docs/70). `read_file` hands back how many bytes it read and does not
  // terminate the buffer —the caller owns the room—, so each read is terminated here or the previous name's tail would
  // be read as part of this one.
  static char text[64];
  size_t len = 0;
  TEST_ASSERT_TRUE(kernel::vfs::read_file("/apps/both/app.lua", text, sizeof(text), len));
  text[len] = '\0';
  TEST_ASSERT_EQUAL_STRING("console.print('both, from the flash')\n", text);
  TEST_ASSERT_TRUE(kernel::vfs::read_file("/apps/*both/app.lua", text, sizeof(text), len));
  text[len] = '\0';
  TEST_ASSERT_EQUAL_STRING("console.print('both, from the card')\n", text);
  TEST_ASSERT_TRUE(kernel::vfs::read_file("/apps/onflash/app.lua", text, sizeof(text), len));
  text[len] = '\0';
  TEST_ASSERT_EQUAL_STRING("console.print('the one of the flash')\n", text);

  card->remove("/apps/oncard/app.lua");
  card->remove("/apps/oncard/manifest.json");
  card->remove("/apps/oncard");
  card->remove("/apps/both/app.lua");
  card->remove("/apps/both/manifest.json");
  card->remove("/apps/both");
  storage->remove("/apps/onflash/app.lua");
  storage->remove("/apps/onflash/manifest.json");
  storage->remove("/apps/onflash");
  storage->remove("/apps/both/app.lua");
  storage->remove("/apps/both/manifest.json");
  storage->remove("/apps/both");
}

// **`/fonts` is the two disks and the app in one directory** (0.174.1, the owner: *«/fonts ha de ser com /apps ha de
// juntar /littlefs/fonts i /sd/fonts. En cas de el que veu l'app també suma el seu /apps/<id>/fonts»*). The flash's
// `res/fonts/` —the system's faces, where `ui::boot` looks for them— and the card's `fonts/` are listed together, a name
// the flash also has is presented from the card with the `*` mark of `/apps`, and a path of the door reads the copy its
// name says. **The app's own folder is not in the shell's door**: it is the app's, and a person listing `/fonts` from the
// prompt sees the faces of the board and not the ones of an app that is not running (docs/30, docs/40, docs/70).
void test_the_door_of_the_fonts_puts_the_flash_and_the_card_together(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  make_card();
  char card_path[96];
  snprintf(card_path, sizeof(card_path), "%s/sd", root());
  TEST_ASSERT_TRUE(card_on(card_path));
  hal::IStorage* card = hal::platform().storage("/sd");
  TEST_ASSERT_NOT_NULL(card);

  // **Two disks, three names**: one only the flash has, one only the card has, and one on both. **The two folders are
  // made by the system and are already there** —`res/fonts/` is the disk's and `fonts/` is made at boot like the folder
  // of the apps— so the `mkdir` is only the case where this suite runs on a disk that has neither (docs/70).
  storage->mkdir("/res/fonts");
  card->mkdir("fonts");
  const char flash_face[] = "FLASH-FACE";
  const char card_face[] = "CARD-FACE";
  const char card_both[] = "CARD-BOTH";
  const char flash_both[] = "FLASH-BOTH";
  auto write = [](hal::IStorage* where, const char* path, const char* text) {
    hal::FileSlot slot;
    if (!where->open(path, hal::OpenMode::kWrite, slot)) return false;
    const bool ok = where->write(slot, text, static_cast<uint32_t>(strlen(text))) ==
                    static_cast<int32_t>(strlen(text));
    where->close(slot);
    return ok;
  };
  TEST_ASSERT_TRUE(write(storage, "res/fonts/only-flash.lfont", flash_face));
  TEST_ASSERT_TRUE(write(card, "fonts/only-card.lfont", card_face));
  TEST_ASSERT_TRUE(write(storage, "res/fonts/both.lfont", flash_both));
  TEST_ASSERT_TRUE(write(card, "fonts/both.lfont", card_both));

  struct Names : public hal::ListSink {
    char rows[32][40] = {};
    uint8_t count = 0;
    bool on_entry(const hal::ListEntry& entry) override {
      if (count < 32 && entry.name != nullptr) snprintf(rows[count], sizeof(rows[0]), "%s", entry.name);
      ++count;
      return true;
    }
  } names;
  TEST_ASSERT_TRUE(kernel::vfs::list("/fonts", names));
  bool only_flash = false;
  bool only_card = false;
  bool both_plain = false;
  bool both_marked = false;
  for (uint8_t i = 0; i < names.count && i < 32; ++i) {
    if (strcmp(names.rows[i], "only-flash.lfont") == 0) only_flash = true;
    if (strcmp(names.rows[i], "only-card.lfont") == 0) only_card = true;
    if (strcmp(names.rows[i], "both.lfont") == 0) both_plain = true;
    if (strcmp(names.rows[i], "*both.lfont") == 0) both_marked = true;
  }
  TEST_ASSERT_TRUE_MESSAGE(only_flash, "the face of the flash is not in the door");
  TEST_ASSERT_TRUE_MESSAGE(only_card, "the face of the card is not in the door");
  TEST_ASSERT_TRUE_MESSAGE(both_plain, "the name the two disks share is not listed once under its own name");
  TEST_ASSERT_TRUE_MESSAGE(both_marked, "the card's copy of a shared name is not listed with the mark");

  // **And a path of the door reads the copy its name says**: `/fonts/both.lfont` is the flash's and
  // `/fonts/*both.lfont` is the card's, which is the rule `/apps` already follows (docs/70).
  static char text[64];
  size_t len = 0;
  TEST_ASSERT_TRUE(kernel::vfs::read_file("/fonts/both.lfont", text, sizeof(text), len));
  text[len] = '\0';
  TEST_ASSERT_EQUAL_STRING(flash_both, text);
  TEST_ASSERT_TRUE(kernel::vfs::read_file("/fonts/*both.lfont", text, sizeof(text), len));
  text[len] = '\0';
  TEST_ASSERT_EQUAL_STRING(card_both, text);
  TEST_ASSERT_TRUE(kernel::vfs::read_file("/fonts/only-card.lfont", text, sizeof(text), len));
  text[len] = '\0';
  TEST_ASSERT_EQUAL_STRING(card_face, text);

  // **The system's own face is still where every document says it is**: the door answers `/fonts/<x>` with the flash's
  // `res/fonts/` when nobody else has that name, which is what `ui::boot` and the reader depend on (docs/30).
  hal::Stat info;
  TEST_ASSERT_TRUE(kernel::vfs::stat("/fonts/only-flash.lfont", info));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(strlen(flash_face)), info.size);

  card->remove("fonts/only-card.lfont");
  card->remove("fonts/both.lfont");
  storage->remove("/res/fonts/only-flash.lfont");
  storage->remove("/res/fonts/both.lfont");
}

// **The door presents the apps and nothing else** (0.88.20): a folder of the flash or of the card that carries no
// `manifest.json` is not an app there, so `/apps` does not show it, `run` does not find it and the search for a handler
// never reads it —which is the point of the door being the one entry point: a folder the system does not present is a
// folder nothing reaches—. A folder that does carry one is presented even when it names another board, because the
// listing says what it needs instead of hiding it (docs/40, docs/70).
void test_the_door_of_the_apps_presents_no_folder_that_is_not_an_app(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  make_card();
  char card_path[96];
  snprintf(card_path, sizeof(card_path), "%s/sd", root());
  TEST_ASSERT_TRUE(card_on(card_path));
  hal::IStorage* card = hal::platform().storage("/sd");
  TEST_ASSERT_NOT_NULL(card);

  // Two folders that are not apps —one per disk— and one app, so the listing has something true to show.
  TEST_ASSERT_TRUE(storage->mkdir("apps/notanapp"));
  TEST_ASSERT_TRUE(write_text(storage, "apps/notanapp/notes.txt", "res\n"));
  card->mkdir("/apps");
  TEST_ASSERT_TRUE(card->mkdir("/apps/alsonotanapp"));
  TEST_ASSERT_TRUE(write_text(card, "/apps/alsonotanapp/notes.txt", "res\n"));
  TEST_ASSERT_TRUE(add_app(storage, "realapp",
                           "{\"title\":\"Real app\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "console.print('real')\n"));

  struct Names : public hal::ListSink {
    char rows[40][32] = {};
    uint8_t count = 0;
    bool on_entry(const hal::ListEntry& entry) override {
      if (count < 40 && entry.name != nullptr) snprintf(rows[count], sizeof(rows[0]), "%s", entry.name);
      ++count;
      return true;
    }
  } names;
  TEST_ASSERT_TRUE(kernel::vfs::list("/apps", names));
  bool real = false;
  bool not_an_app = false;
  for (uint8_t i = 0; i < names.count && i < 40; ++i) {
    if (strcmp(names.rows[i], "realapp") == 0) real = true;
    if (strcmp(names.rows[i], "notanapp") == 0) not_an_app = true;
    if (strcmp(names.rows[i], "alsonotanapp") == 0) not_an_app = true;
  }
  TEST_ASSERT_TRUE_MESSAGE(real, "the app is not in the door");
  TEST_ASSERT_FALSE_MESSAGE(not_an_app, "a folder that is not an app is in the door");

  // **And it is not runnable either**: the walk that finds the entry file is the same door, so the answer is the same.
  TEST_ASSERT_TRUE(run_captured("run notanapp", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "no such app: notanapp"), g_out);
  TEST_ASSERT_TRUE(run_captured("run alsonotanapp", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "no such app: alsonotanapp"), g_out);
  TEST_ASSERT_TRUE(run_captured("run realapp", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "real"), g_out);

  card->remove("/apps/alsonotanapp/notes.txt");
  card->remove("/apps/alsonotanapp");
  storage->remove("apps/notanapp/notes.txt");
  storage->remove("apps/notanapp");
  storage->remove("/apps/realapp/app.lua");
  storage->remove("/apps/realapp/manifest.json");
  storage->remove("/apps/realapp");
}

// **`apps remove <id>` takes the copy the name says** (0.89.0): the id is what the listing shows —`both` for the flash
// and `*both` for the card (0.164.0)— so one of two copies leaves the disk and the other stays, and once the flash's copy
// is gone the card's one loses the mark and `run both` is the app of the card: what the door presents is what exists.
// **Only the app goes**: what it left in `/data` is not the app, and a folder of `/apps` that carries no manifest is not
// an app and cannot be deleted by typing its name (docs/30, docs/40, docs/70).
void test_apps_remove_takes_the_app_the_name_says_and_leaves_the_rest(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  make_card();
  char card_path[96];
  snprintf(card_path, sizeof(card_path), "%s/sd", root());
  TEST_ASSERT_TRUE(card_on(card_path));
  hal::IStorage* card = hal::platform().storage("/sd");
  TEST_ASSERT_NOT_NULL(card);

  TEST_ASSERT_TRUE(add_app(storage, "gone",
                           "{\"title\":\"Flash gone\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "console.print('gone, from the flash')\n"));
  TEST_ASSERT_TRUE(add_app(card, "gone",
                           "{\"title\":\"Gone card\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "console.print('gone, from the card')\n"));
  // What the app left behind, which is not the app: its data, and a folder of `/apps` that is not an app at all.
  TEST_ASSERT_TRUE(storage->mkdir("data/gone"));
  TEST_ASSERT_TRUE(write_text(storage, "data/gone/notes.txt", "mine\n"));
  TEST_ASSERT_TRUE(storage->mkdir("apps/notanapp"));

  // **The flash's copy goes by its own name**, and the message says the disk it was on and what came back.
  TEST_ASSERT_TRUE(run_captured("apps remove gone", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "apps: gone removed from /littlefs/apps/gone"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "2 files"), g_out);
  hal::Stat info;
  TEST_ASSERT_FALSE_MESSAGE(storage->stat("apps/gone", info), "the app of the flash is still there");
  TEST_ASSERT_TRUE_MESSAGE(card->stat("apps/gone", info), "the copy of the card went with it");

  // **The card's copy is now the only one, and it is the one without the suffix**.
  pump_captured(20, g_out, sizeof(g_out), "apps");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "Gone card"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "*gone"), g_out);
  TEST_ASSERT_TRUE(run_captured("run gone", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "gone, from the card"), g_out);

  // **The data is not the app**: the file the app wrote is still there after the app is gone.
  TEST_ASSERT_TRUE_MESSAGE(storage->stat("data/gone/notes.txt", info), "the data of the app went with it");

  // **A folder that is not an app is not deletable by its name**, and a name that is not there says so.
  TEST_ASSERT_TRUE(run_captured("apps remove notanapp", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "not an app: notanapp has no manifest.json"), g_out);
  TEST_ASSERT_TRUE_MESSAGE(storage->stat("apps/notanapp", info), "a folder that is not an app was deleted");
  TEST_ASSERT_TRUE(run_captured("apps remove nadie", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "no such app: nadie"), g_out);
  TEST_ASSERT_TRUE(run_captured("apps remove", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "usage: apps [info <id> | remove <id>]"), g_out);

  // **And the copy of the card goes by the name it has now**, which is the one without the suffix.
  TEST_ASSERT_TRUE(run_captured("apps remove gone", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "apps: gone removed from /sd/apps/gone"), g_out);
  TEST_ASSERT_FALSE(card->stat("apps/gone", info));

  // **The mark picks the copy of the card**: with both disks carrying `pair`, `*pair` takes the card's one away and the
  // flash's stays under its own name (0.164.0, docs/40, docs/70).
  TEST_ASSERT_TRUE(add_app(storage, "pair",
                           "{\"title\":\"Flash pair\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "console.print('pair, from the flash')\n"));
  TEST_ASSERT_TRUE(add_app(card, "pair",
                           "{\"title\":\"Gone pair\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "console.print('pair, from the card')\n"));
  TEST_ASSERT_TRUE(run_captured("apps remove *pair", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "apps: *pair removed from /sd/apps/pair"), g_out);
  TEST_ASSERT_FALSE(card->stat("apps/pair", info));
  TEST_ASSERT_TRUE_MESSAGE(storage->stat("apps/pair", info), "the copy of the flash went with the card's");

  storage->remove("/apps/pair/app.lua");
  storage->remove("/apps/pair/manifest.json");
  storage->remove("/apps/pair");
  storage->remove("apps/notanapp");
  storage->remove("data/gone/notes.txt");
  storage->remove("data/gone");
}

// **A card of more than two dozen apps lists the one of the flash too** (0.88.10, 0.88.20): the walk once remembered the
// ids of the card in a table of twenty-four, so from the twenty-fifth on an app of two disks was presented by a rule and
// not by the door. Now the suffix is decided by the listing of the door, one name at a time: the flash's copy is
// `shared` and the card's `*shared` whatever place they take in the folder, and the thirty of the card are all there
// (docs/40, docs/70, docs/75).
void test_a_card_of_more_than_two_dozen_apps_lists_the_one_of_the_flash_too(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  make_card();
  char card_path[96];
  snprintf(card_path, sizeof(card_path), "%s/sd", root());
  TEST_ASSERT_TRUE(card_on(card_path));
  hal::IStorage* card = hal::platform().storage("/sd");
  TEST_ASSERT_NOT_NULL(card);
  card->mkdir("/apps");

  // **Thirty of them on the card**, and then the one the flash also has: past twenty-four ids is where the table gave up
  // and the listing showed the app of two disks twice (docs/40).
  static char manifest[128];
  for (int i = 1; i <= 30; ++i) {
    char id[16];
    snprintf(id, sizeof(id), "card%02d", i);
    snprintf(manifest, sizeof(manifest),
             "{\"title\":\"Card %02d\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}", i);
    TEST_ASSERT_TRUE(add_app(card, id, manifest, "console.print('from the card')\n"));
  }
  TEST_ASSERT_TRUE(
      add_app(card, "shared",
              "{\"title\":\"Shared card\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
              "console.print('from the card')\n"));
  TEST_ASSERT_TRUE(
      add_app(storage, "shared",
              "{\"title\":\"Shared flash\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
              "console.print('from the flash')\n"));

  pump_captured(20, g_out, sizeof(g_out), "apps");
  // **Both copies are listed**, the one of the flash under its own name and the one of the card with the suffix, past the
  // twenty-fourth id of the card as well (0.88.20, docs/40).
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "Shared card"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "Shared flash"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "*shared"), g_out);
  // **And the thirty of the card are all there, each one once**: the count of what a person can read on the listing.
  int shown = 0;
  for (const char* p = g_out; (p = strstr(p, "Card ")) != nullptr; p += 5) ++shown;
  TEST_ASSERT_EQUAL_INT(30, shown);
  // **And each name runs its own copy** (0.88.20, docs/40, docs/70).
  TEST_ASSERT_TRUE(run_captured("run shared", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "from the flash"), g_out);
  TEST_ASSERT_TRUE(run_captured("run *shared", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "from the card"), g_out);

  for (int i = 1; i <= 30; ++i) {
    char id[16];
    snprintf(id, sizeof(id), "card%02d", i);
    char path[64];
    snprintf(path, sizeof(path), "/apps/%s/app.lua", id);
    card->remove(path);
    snprintf(path, sizeof(path), "/apps/%s/manifest.json", id);
    card->remove(path);
    snprintf(path, sizeof(path), "/apps/%s", id);
    card->remove(path);
  }
  card->remove("/apps/shared/app.lua");
  card->remove("/apps/shared/manifest.json");
  card->remove("/apps/shared");
  storage->remove("/apps/shared/app.lua");
  storage->remove("/apps/shared/manifest.json");
  storage->remove("/apps/shared");
}

// **A board with no card takes no turn through the void** (0.87.1): the door is `/apps` and nothing behind it asks for a
// card that is not there, and an app that is not there names the door and says the card is not in the slot
// (docs/40, docs/50, docs/70).
void test_a_board_with_no_card_looks_only_in_the_flash(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.sd_dir = nullptr;
  emu::set_options(opts);
  kernel::vfs::init(hal::platform());
  kernel::cfg::boot(hal::platform());
  TEST_ASSERT_NULL(hal::platform().storage("/sd"));
  TEST_ASSERT_FALSE(kernel::apps::card_here());

  TEST_ASSERT_TRUE(run_captured("run nadie_ha_vist_aquesta", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "no such app: nadie_ha_vist_aquesta"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "apps are in /apps, and there is no card in the slot"), g_out);
}

// **The faces of the disk are one area for every app** (3/11): `/fonts` is the disk's `res/fonts/`
// seen from the root, so an app reads a face without carrying a copy of it, and what it finds there
// is not only a file but a face the reader can wear — the two halves of "a font works" (docs/30,
// docs/70).
void test_the_faces_of_the_disk_are_at_fonts_for_every_app(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);

  // **The faces of the repository, on this disk**: the emulator does not read the disk of the build, so the two this
  // test names are written here (docs/70, docs/85).
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
  // **The face arrives whole as well** (4096 B of the `.ttf` that is behind the door). Whether the reader turns it into
  // a face that can be carried is another test and another emulator: `ui.load_font` needs the glass of the app —the
  // `ui::boot` that installs the operations— and this emulator is an ATOM with no screen, where `E_UNSUPPORTED` is the
  // truth of the board and not a failure. The reader of `.ttf` has its own suite (`test_ttf`, with two real files) and
  // `test_font` tests the faces of `/res` (docs/30, docs/50).
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

// The battery of the emulator is a number the person sets, and with no number the whole
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
  // The flash of the emulator is a directory, but the board's is a partition of a size its own table
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

  // Quoted: one path, the extension finds the app, and it receives it whole. **The selector lists
  // the handlers in the order the disk gives them**, and two file systems do not give the same one:
  // the test reads the number it printed for `txts` instead of assuming that `txt` is second.
  TEST_ASSERT_TRUE(run_captured("open \"/res/el meu fitxer.txt\"", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "which one?"), g_out);
  const char* listed = strstr(g_out, ") txts");
  TEST_ASSERT_NOT_NULL_MESSAGE(listed, g_out);
  const char* number = listed;
  while (number > g_out && number[-1] >= '0' && number[-1] <= '9') --number;
  char answer[8];
  const size_t digits = static_cast<size_t>(listed - number);
  TEST_ASSERT_TRUE(digits > 0 && digits < sizeof(answer));
  memcpy(answer, number, digits);
  answer[digits] = '\0';
  TEST_ASSERT_TRUE(run_captured(answer, g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "spaced handler, file /res/el meu fitxer.txt"),
                                g_out);

  storage->remove(kPath);
  storage->remove("/apps/txts/app.lua");
  storage->remove("/apps/txts/manifest.json");
  storage->remove("/apps/txts");
}

// **A turn budget and not a kill** (0.90.0): a loop long enough to pass the 100 000 instructions of a
// turn gives the kernel its turn, so the console is read and the app can be closed. The infinite version
// (`while true do end`) was checked on the emulator; this one is finite so a regression fails the test instead
// of hanging the suite (docs/20, docs/75).
void test_a_long_loop_gives_the_turn_back(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "spinner",
                            "{\"title\":\"Spinner\",\"version\":\"0.1.0\","
                            "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                            "console.print(\"spin\")\n"
                            "local n = 0\n"
                            "while n < 5000000 do n = n + 1 end\n"
                            "console.print(\"done\")\n"));

  // `pump_captured` turns the kernel: before the budget the first poll ran the whole loop.
  pump_captured(4, g_out, sizeof(g_out), "run spinner");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "spin"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "done"), g_out);
  TEST_ASSERT_TRUE(kernel::app_running());

  // The kernel has had turns to read the console: three ESC close the app that no longer yields.
  pump_captured(4, g_out, sizeof(g_out), nullptr, "\x1b\x1b\x1b");
  TEST_ASSERT_FALSE(kernel::app_running());

  storage->remove("/apps/spinner/app.lua");
  storage->remove("/apps/spinner/manifest.json");
  storage->remove("/apps/spinner");
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

// **A refusal says which one it was** (0.118.10): the ATOM answered `E_BUSY` to `wifi scan` and to
// `provision` while the Paper did both, and `E_BUSY` is four faults under one name —a radio working on
// something else, a radio that never came up, one with no memory for itself, a scan the chip turned
// down—. The driver hands its own words back and the prompt prints them instead (docs/30, docs/50).
void test_a_radio_that_refuses_says_why(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.wifi_absent = false;
  opts.refuse_why = "the radio did not come up (ESP_ERR_NO_MEM)";
  emu::set_options(opts);

  pump_captured(300, g_out, sizeof(g_out), "wifi scan");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "wifi: the scan failed"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "the radio did not come up (ESP_ERR_NO_MEM)"), g_out);
  // And not the four letters that hid it: `E_BUSY` stays for a radio that is really working on something.
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "E_BUSY"), g_out);

  opts.refuse_why = nullptr;
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
  // **And the radio is a job that has to end first** (0.152.4): `wifi forget` spends `wifi_ms` —a hundred in this emulator—
  // and a command that arrives while it is still running answers `E_BUSY`, which is true and is not what this case is
  // about. Twenty milliseconds was the whole wait, so the case was asking the wrong question and has been red for it.
  char forgot[512];
  pump_captured(20, forgot, sizeof(forgot), "wifi forget");
  // **The forget has to have happened**: it is the state this case is about, and a command that arrives while another
  // one is running answers `E_BUSY` —which is true and is another question— (0.152.4).
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(forgot, "network: none"), forgot);
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

// **`repo info <app>` reads the manifest and makes a card of it** (0.82.1): the command asks GitHub for the raw file
// and paints it with the JSON reader of the system, which is the same one that reads the manifests of the disk. The
// network is the fake one of the emulator —a canned answer— because what is tested is the command and not the address
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

  // **A manifest without `entry` is not an app that can be run**, and the card says so instead of pretending it is one
  // (docs/40).
  opts.http_body = "{\"title\":\"Mig\"}";
  emu::set_options(opts);
  pump_captured(400, g_out, sizeof(g_out), "repo info mija");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "not a usable app: no entry"), g_out);

  // **The name of the command is `repo` and nothing else** (0.84.2): the old `repository` is not an alias that lives on
  // —two names for one command is a person who types the one that does not work— (docs/30).
  pump_captured(40, g_out, sizeof(g_out), "repository info alguna");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "unknown command: repository"), g_out);
}

// **The chain of an install, one answer per address** (0.88.6): the app `chain`, with its manifest, its code and a folder
// of two files —four files and a folder, which is the smallest app that walks every step of the chain: the listing of the
// app, the listing of the folder, and a download for each file—. The answers are asked in the order they were given and
// the first key that appears in the address wins, so the folder goes in before the app: the address of the folder starts
// with the address of the app (docs/30, docs/70, docs/85).
const char* const kChainManifest =
    "{\"title\":\"Chain\",\"version\":\"1.0.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}";
const char* const kChainSource = "console.print('chain ok')\n";
const char* const kChainNotes = "one line\n";
const char* const kChainNumbers = "42\n";
// **The address of a file is the one the listing gives it**, and the emulator answers by a part of it: what the address of
// GitHub looks like is GitHub's business and not the chain's (docs/30).
const char* const kChainAppListing =
    "[{\"name\":\"manifest.json\",\"download_url\":\"https://h/a/manifest.json\",\"type\":\"file\"},"
    "{\"name\":\"app.lua\",\"download_url\":\"https://h/a/app.lua\",\"type\":\"file\"},"
    "{\"name\":\"res\",\"download_url\":null,\"type\":\"dir\"}]";
const char* const kChainFolderListing =
    "[{\"name\":\"notes.txt\",\"download_url\":\"https://h/a/notes.txt\",\"type\":\"file\"},"
    "{\"name\":\"numbers.bin\",\"download_url\":\"https://h/a/numbers.bin\",\"type\":\"file\"}]";

// The six answers of the chain, with the status of the last file of the folder —which is how a server that fails at the
// end of an install is tried (docs/30).
bool install_the_chain(int32_t numbers_status) {
  emu::http_canned_clear();
  return emu::http_canned("contents/apps/chain/res", kChainFolderListing, 200) &&
         emu::http_canned("contents/apps/chain", kChainAppListing, 200) &&
         emu::http_canned("a/manifest.json", kChainManifest, 200) &&
         emu::http_canned("a/app.lua", kChainSource, 200) &&
         emu::http_canned("a/notes.txt", kChainNotes, 200) &&
         emu::http_canned("a/numbers.bin", kChainNumbers, numbers_status);
}

// **An app of ten folders and a folder inside a folder** (0.88.9): the queue of folders was a table of six, and the
// seventh folder of an app was the install that said so and stopped; a folder inside a folder had no room in the table at
// all. The queue is a file of the app's folder now, so ten folders cost what one costs, and the path a folder has inside
// the app is the path its files land on (docs/30, docs/70, docs/75). The emulator answers one address per folder and per
// file, which is the whole of the chain.
const char* const kWideManifest =
    "{\"title\":\"Wide\",\"version\":\"1.0.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}";
const char* const kWideSource = "console.print('wide ok')\n";

bool install_a_wide_app(void) {
  emu::http_canned_clear();
  static char app_listing[1024];
  static char one[256];
  static char key[64];
  // The listing of the app: the manifest, the code and nine folders.
  size_t used = 0;
  used += static_cast<size_t>(snprintf(
      app_listing + used, sizeof(app_listing) - used,
      "[{\"name\":\"manifest.json\",\"download_url\":\"https://f/m\",\"type\":\"file\"},"));
  used += static_cast<size_t>(snprintf(
      app_listing + used, sizeof(app_listing) - used,
      "{\"name\":\"app.lua\",\"download_url\":\"https://f/a\",\"type\":\"file\"},"));
  for (int i = 1; i <= 9; ++i) {
    used += static_cast<size_t>(snprintf(app_listing + used, sizeof(app_listing) - used,
                                         "{\"name\":\"f%d\",\"download_url\":null,\"type\":\"dir\"}%s", i,
                                         (i < 9) ? "," : ""));
  }
  used += static_cast<size_t>(snprintf(app_listing + used, sizeof(app_listing) - used, "]"));
  // **The folder inside the folder goes in first**, and then the nine: the address of `f1/inner` starts with the one of
  // `f1`, and the first answer that matches an address wins (docs/85).
  snprintf(one, sizeof(one), "[{\"name\":\"deep.txt\",\"download_url\":\"https://f/d\",\"type\":\"file\"}]");
  if (!emu::http_canned("contents/apps/wide/f1/inner", one, 200)) return false;
  for (int i = 1; i <= 9; ++i) {
    if (i == 1) {
      // **The folder inside the folder**: it is a `dir` of this listing, which is how the plan learns a path with a
      // slash in it (docs/30, docs/70).
      snprintf(one, sizeof(one),
               "[{\"name\":\"inner\",\"download_url\":null,\"type\":\"dir\"},"
               "{\"name\":\"x1.txt\",\"download_url\":\"https://f/x1\",\"type\":\"file\"}]");
    } else {
      snprintf(one, sizeof(one), "[{\"name\":\"x%d.txt\",\"download_url\":\"https://f/x%d\",\"type\":\"file\"}]", i, i);
    }
    snprintf(key, sizeof(key), "contents/apps/wide/f%d", i);
    if (!emu::http_canned(key, one, 200)) return false;
  }
  // And the listing of the app, whose address is the beginning of every one of those.
  if (!emu::http_canned("contents/apps/wide", app_listing, 200)) return false;
  if (!emu::http_canned("f/m", kWideManifest, 200)) return false;
  if (!emu::http_canned("f/a", kWideSource, 200)) return false;
  if (!emu::http_canned("f/d", "deep\n", 200)) return false;
  for (int i = 1; i <= 9; ++i) {
    snprintf(key, sizeof(key), "f/x%d", i);
    snprintf(one, sizeof(one), "file %d\n", i);
    if (!emu::http_canned(key, one, 200)) return false;
  }
  return true;
}

// **`repo install <app>` is a chain of downloads and it ends with the app on the disk** (0.88.6): the listing of the app,
// the listing of its folder and each one of its files are a download each, the reader walks every listing once and gives
// it back, and what the summary says is what the disk has. The emulator answers per address, which is what makes a chain
// testable at all: one page for every URL would be the same file saved four times, and a listing read as a file (docs/30,
// docs/70, docs/85).
void test_the_repo_command_installs_an_app_with_its_folders(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  // **The flash and not a card that another test left in the slot**: `repo install` writes on the disk `cfg` chose, and
  // a test that says `/littlefs/apps/chain` has to be able to say which disk that is (docs/70, docs/85).
  opts.sd_dir = nullptr;
  emu::set_options(opts);
  kernel::vfs::init(hal::platform());
  kernel::cfg::boot(hal::platform());
  opts.wifi_absent = false;
  opts.wifi_ms = 100;
  opts.http_fake = true;
  opts.http_ms = 50;
  opts.http_status = 200;
  // **A canned page that is not a listing**: an address of the chain that were answered here instead of by its own
  // answer would stop the install with "the answer is not where it was saved", so the test fails out loud and not by
  // luck (docs/85).
  opts.http_body = "not a listing";
  emu::set_options(opts);
  TEST_ASSERT_TRUE(install_the_chain(200));

  pump_captured(20, g_out, sizeof(g_out), "wifi forget");
  pump_captured(400, g_out, sizeof(g_out), "wifi add casa secret");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "wifi: on the network casa"), g_out);

  pump_captured(2000, g_out, sizeof(g_out), "repo install chain");
  TEST_ASSERT_FALSE_MESSAGE(kernel::shell::native_running(), "the chain never ended");
  // **The count is measured and not written here**: what the line says is the four bodies that came down, and the test
  // builds the same number out of them (docs/30).
  char expected[128];
  snprintf(expected, sizeof(expected), "repo: chain in /littlefs/apps/chain, 4 files, 1 folder, %u bytes",
           static_cast<unsigned>(strlen(kChainManifest) + strlen(kChainSource) + strlen(kChainNotes) +
                                 strlen(kChainNumbers)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, expected), g_out);

  // **What the line says is what the disk has**, byte by byte: a count of four files with three of them empty is a count
  // of downloads and not an app (docs/70).
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  // **Eight hundred bytes and static**: a buffer of this suite lives outside the frame, the way the ones of the board
  // do (docs/75).
  static char file[400];
  TEST_ASSERT_TRUE(read_text(storage, "/apps/chain/manifest.json", file, sizeof(file)));
  TEST_ASSERT_EQUAL_STRING(kChainManifest, file);
  TEST_ASSERT_TRUE(read_text(storage, "/apps/chain/app.lua", file, sizeof(file)));
  TEST_ASSERT_EQUAL_STRING(kChainSource, file);
  // **The file of a folder is the point of the queue**: a chain that lists a folder and never downloads what is in it
  // says "one folder" and carries nothing (docs/30).
  TEST_ASSERT_TRUE(read_text(storage, "/apps/chain/res/notes.txt", file, sizeof(file)));
  TEST_ASSERT_EQUAL_STRING(kChainNotes, file);
  TEST_ASSERT_TRUE(read_text(storage, "/apps/chain/res/numbers.bin", file, sizeof(file)));
  TEST_ASSERT_EQUAL_STRING(kChainNumbers, file);

  // **A listing is a temporary file of the system inside the folder of the app**, and the app's own `fs.list` would read
  // it: none is left, neither the one of the app nor the one of its folder (docs/70).
  hal::Stat info;
  TEST_ASSERT_FALSE(storage->stat("/apps/chain/.listing.json", info));
  TEST_ASSERT_FALSE(storage->stat("/apps/chain/res/.listing.json", info));

  // **And what landed is an app the system runs**, which is what the install was for (docs/40).
  pump_captured(300, g_out, sizeof(g_out), "run chain");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "chain ok"), g_out);

  storage->remove("/apps/chain/res/notes.txt");
  storage->remove("/apps/chain/res/numbers.bin");
  storage->remove("/apps/chain/res");
  storage->remove("/apps/chain/app.lua");
  storage->remove("/apps/chain/manifest.json");
  storage->remove("/apps/chain");
  emu::http_canned_clear();
  opts.http_body = nullptr;
  opts.http_fake = false;
  emu::set_options(opts);
}

// **An app with more folders than a table in RAM carries, and a folder inside a folder** (0.88.9): the queue was
// `g_install_waiting[6][48]` and the seventh folder of an app was an install that said "this app has more folders than
// an install carries" and stopped. It is a file of the app's folder now —one line per folder, with the path it has
// inside the app—, so the ceiling is the disk, and a folder inside a folder is one more line instead of a path nobody
// could write down (docs/30, docs/70, docs/75).
void test_an_app_with_more_folders_than_a_table_in_ram_carries_is_installed(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.sd_dir = nullptr;
  emu::set_options(opts);
  kernel::vfs::init(hal::platform());
  kernel::cfg::boot(hal::platform());
  opts.wifi_absent = false;
  // **Fifty milliseconds a network and twenty a page**: the chain is twenty-three downloads and the suite does not have
  // to pay a second for each one (docs/85).
  opts.wifi_ms = 50;
  opts.http_fake = true;
  opts.http_ms = 20;
  opts.http_status = 200;
  opts.http_body = "not a listing";
  emu::set_options(opts);
  TEST_ASSERT_TRUE(install_a_wide_app());

  pump_captured(20, g_out, sizeof(g_out), "wifi forget");
  pump_captured(400, g_out, sizeof(g_out), "wifi add casa secret");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "wifi: on the network casa"), g_out);

  pump_captured(3000, g_out, sizeof(g_out), "repo install wide");
  TEST_ASSERT_FALSE_MESSAGE(kernel::shell::native_running(), "the chain never ended");
  // **Twelve files and ten folders**, which is above the six of the table: the manifest, the code, one file per folder
  // and one in the folder of the first folder (docs/30).
  char expected[128];
  snprintf(expected, sizeof(expected), "repo: wide in /littlefs/apps/wide, 12 files, 10 folders, %u bytes",
           static_cast<unsigned>(strlen(kWideManifest) + strlen(kWideSource) + 9 * 7 + 5));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, expected), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "more folders"), g_out);

  // **The folders are on the disk with the files they carry**, the ninth one included: a queue that stopped at the sixth
  // would leave four folders of the plan unanswered (docs/70).
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  static char file[400];
  TEST_ASSERT_TRUE(read_text(storage, "/apps/wide/f1/x1.txt", file, sizeof(file)));
  TEST_ASSERT_EQUAL_STRING("file 1\n", file);
  TEST_ASSERT_TRUE(read_text(storage, "/apps/wide/f9/x9.txt", file, sizeof(file)));
  TEST_ASSERT_EQUAL_STRING("file 9\n", file);
  // **And the file of the folder inside the folder lands where its path says**: `f1/inner/deep.txt` and not
  // `inner/deep.txt` of the root, which is what a queue of bare names would have made (docs/30, docs/70).
  TEST_ASSERT_TRUE(read_text(storage, "/apps/wide/f1/inner/deep.txt", file, sizeof(file)));
  TEST_ASSERT_EQUAL_STRING("deep\n", file);

  // **And the plan is a temporary file of the system like the listing**: none of them is left inside the app (docs/70).
  hal::Stat info;
  TEST_ASSERT_FALSE(storage->stat("/apps/wide/.folders", info));
  TEST_ASSERT_FALSE(storage->stat("/apps/wide/.listing.json", info));
  TEST_ASSERT_FALSE(storage->stat("/apps/wide/f1/.listing.json", info));
  TEST_ASSERT_FALSE(storage->stat("/apps/wide/f1/inner/.listing.json", info));

  // **And it runs**, which is what an install of twelve files was for (docs/40).
  pump_captured(300, g_out, sizeof(g_out), "run wide");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "wide ok"), g_out);

  for (int i = 1; i <= 9; ++i) {
    char path[64];
    snprintf(path, sizeof(path), "/apps/wide/f%d/x%d.txt", i, i);
    storage->remove(path);
    snprintf(path, sizeof(path), "/apps/wide/f%d", i);
    storage->remove(path);
  }
  storage->remove("/apps/wide/f1/inner/deep.txt");
  storage->remove("/apps/wide/f1/inner");
  storage->remove("/apps/wide/app.lua");
  storage->remove("/apps/wide/manifest.json");
  storage->remove("/apps/wide");
  emu::http_canned_clear();
  opts.http_body = nullptr;
  opts.http_fake = false;
  emu::set_options(opts);
}

// **An install that fails in the middle says which file and can be tried again** (0.88.6): the chain stops at the first
// answer that is not a 200, the reader goes back with it, and what is left is a state a new install can use —a command
// that keeps the board and has to be given it back is a command whose second try is not a try (docs/30, docs/75).
void test_an_install_that_hits_a_404_says_so_and_can_be_tried_again(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  // Without a card, for the reason of the test above.
  opts.sd_dir = nullptr;
  emu::set_options(opts);
  kernel::vfs::init(hal::platform());
  kernel::cfg::boot(hal::platform());
  opts.wifi_absent = false;
  opts.wifi_ms = 100;
  opts.http_fake = true;
  opts.http_ms = 50;
  opts.http_status = 200;
  opts.http_body = "not a listing";
  emu::set_options(opts);

  pump_captured(20, g_out, sizeof(g_out), "wifi forget");
  pump_captured(400, g_out, sizeof(g_out), "wifi add casa secret");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "wifi: on the network casa"), g_out);

  // **The last file of the folder is a 404**: the three of before it came, and the install says where it stopped and
  // not that it worked (docs/30).
  TEST_ASSERT_TRUE(install_the_chain(404));
  pump_captured(2000, g_out, sizeof(g_out), "repo install chain");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "repo: the server answered 404 for a file of the app"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "4 files, 1 folder"), g_out);
  TEST_ASSERT_FALSE_MESSAGE(kernel::shell::native_running(), "the command never ended");

  // **And what is left is nothing, and not half an app** (0.88.14): the four files that did come are in a folder of the
  // system's that goes away with the failure, so `apps/<id>` never appears and the listing does not count an app that
  // cannot run (docs/30, docs/70).
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  hal::Stat info;
  TEST_ASSERT_FALSE_MESSAGE(storage->stat("/apps/chain", info), "half an app stayed on the disk");
  TEST_ASSERT_FALSE_MESSAGE(storage->stat("/apps/.chain.install", info), "the folder of the work stayed on the disk");
  pump_captured(20, g_out, sizeof(g_out), "apps");
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "Chain"), g_out);

  // **The same command again with the wire answering is a whole install** and not the rest of the one before: the
  // counters, the queue of folders and the reader are the ones of a new one (docs/75).
  TEST_ASSERT_TRUE(install_the_chain(200));
  pump_captured(2000, g_out, sizeof(g_out), "repo install chain");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "repo: chain in /littlefs/apps/chain, 4 files, 1 folder,"), g_out);

  static char file[400];
  TEST_ASSERT_TRUE(read_text(storage, "/apps/chain/res/numbers.bin", file, sizeof(file)));
  TEST_ASSERT_EQUAL_STRING(kChainNumbers, file);

  // **An update that dies leaves the app that was there** (0.88.14): the second install of the same id fails at its
  // last file and the one of before is whole, with its own bytes —a name taken before the files are there would have
  // left a mixture of the two, which is the half app this rule is for (docs/30, docs/70).
  TEST_ASSERT_TRUE(install_the_chain(404));
  pump_captured(2000, g_out, sizeof(g_out), "repo install chain");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "repo: the server answered 404 for a file of the app"), g_out);
  TEST_ASSERT_TRUE(read_text(storage, "/apps/chain/res/numbers.bin", file, sizeof(file)));
  TEST_ASSERT_EQUAL_STRING(kChainNumbers, file);
  TEST_ASSERT_TRUE(read_text(storage, "/apps/chain/app.lua", file, sizeof(file)));
  TEST_ASSERT_EQUAL_STRING(kChainSource, file);
  TEST_ASSERT_FALSE_MESSAGE(storage->stat("/apps/chain.old", info), "the app of before stayed under another name");
  pump_captured(20, g_out, sizeof(g_out), "apps");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "Chain"), g_out);

  storage->remove("/apps/chain/res/notes.txt");
  storage->remove("/apps/chain/res/numbers.bin");
  storage->remove("/apps/chain/res");
  storage->remove("/apps/chain/app.lua");
  storage->remove("/apps/chain/manifest.json");
  storage->remove("/apps/chain");
  emu::http_canned_clear();
  opts.http_body = nullptr;
  opts.http_fake = false;
  emu::set_options(opts);
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

  // **The radio is a job and it has to end** (0.152.4): the same twenty milliseconds that `wifi_ms` does not fit, and
  // the `E_BUSY` of a command that arrives in the middle of a forget.
  char forgot[512];
  pump_captured(20, forgot, sizeof(forgot), "wifi forget");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(forgot, "network: none"), forgot);
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

// **`ui.focus` puts the dial where the app says** (0.97.4): a page that is built again —the pages of a catalogue under
// a strip of tabs are given back and built at every tab— loses the control the dial was on and the walk starts at the
// first one of the tree, which is the strip; the arrows then change the tab for ever and nothing of the page can be
// pressed (the owner, 7/11: *«no puc pitjar res»*). The test is the centre key: without the call it presses the first
// button of the page, and with it the one the app named.
void test_ui_focus_puts_the_dial_where_the_app_says(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "focuser",
                           "{\"title\":\"Focuser\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local root = ui.panel(540, 900, 'vertical')\n"
                           "root:place(0, 0)\n"
                           "local column = ui.panel(500, 600, 'vertical')\n"
                           "column.weight = 1\n"
                           "root:add(column)\n"
                           "local a = ui.button(400, 68)\n"
                           "a.on_click = function(self) console.print('pressed a') end\n"
                           "column:add(a)\n"
                           "local b = ui.button(400, 68)\n"
                           "b.on_click = function(self) console.print('pressed b') end\n"
                           "column:add(b)\n"
                           "ui.focus(b)\n"
                           "ui.present('gc16')\n"
                           "sys.delay(3000):await()\n"));
  pump_captured(20, g_out, sizeof(g_out), "run focuser");
  TEST_ASSERT_TRUE(kernel::app_running());
  capture_begin();
  luabind::ui_button_event("press", 0);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "pressed b"), g_out);
  close_any_app();
  pump_turns(10);
  storage->remove("/apps/focuser/app.lua");
  storage->remove("/apps/focuser/manifest.json");
  storage->remove("/apps/focuser");
}

// **The showcase runs, and its pages are built and given back on the same glass** (0.94.2): the app that shows every control
// of the system is the page that says what the system has, and it is also the only one that walks a path nothing else
// walks —a page of controls that is **released whole and built again** when the tabs choose another one, children before
// their father—. If a slot leaked, the second page would build it on top of the first and `ui.create` would start
// refusing; if the tabs did not take the finger, the page would never turn (docs/30, docs/90).

// **A list of entries is a list of controls** (0.104.0): the app builds the rows itself —a `ui.list_entry` with a
// label, a row of two cells or a box inside— and the list places the ones of the page it is on. The entries of the
// other pages are placed past the frame, where the clip hides them and the walk does not reach them, and the arrows
// turn the pages of the list just as they do when the rows are data (docs/30, docs/90).
void test_a_list_of_entries_places_a_page_of_controls(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "entries",
                           "{\"title\":\"Entries\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local list = ui.list(400, 180)\n"
                           "list:place(20, 20)\n"
                           "list.row_height = 56\n"
                           "local turns = 0\n"
                           "list.on_page = function(self, page)\n"
                           "  turns = turns + 1\n"
                           "  console.print('page ' .. page)\n"
                           "end\n"
                           "local rows = {}\n"
                           "for i = 1, 6 do\n"
                           "  local entry = ui.list_entry(400, 56)\n"
                           "  local words = ui.label(200, 44)\n"
                           "  words.text = 'row ' .. i\n"
                           "  words:place(12, 6)\n"
                           "  entry:add(words)\n"
                           "  list:add(entry)\n"
                           "  rows[i] = entry\n"
                           "end\n"
                           "ui.present('gc16')\n"
                           "console.print('entries up')\n"
                           "sys.delay(30):await()\n"
                           // **Where the six entries ended up**: the two of this page at the top of the frame and the
                           // other four past it.
                           "local ys = {}\n"
                           "for i = 1, 6 do\n"
                           "  ys[i] = rows[i]:caps().y\n"
                           "end\n"
                           "console.print('at ' .. ys[1] .. ' ' .. ys[2] .. ' ' .. ys[3])\n"
                           "while turns < 1 do sys.delay(10):await() end\n"
                           "console.print('page now ' .. list.page)\n"));

  pump_captured(40, g_out, sizeof(g_out), "run entries");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "entries up"), g_out);
  // **Two rows of fifty-six in a hundred and eighty**: the footer of the pages takes twenty, so a hundred and sixty
  // are left and two rows fit. The first two are the page and the third is past the frame, which is the number the
  // clip uses (the frame plus a row).
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "at 0 56 236"), g_out);

  // And the arrows turn the page of a list of entries.
  capture_begin();
  pump_turns(10);
  emu::press_key(2, true);
  pump_turns(20);
  emu::press_key(2, false);
  pump_turns(30);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "page 2"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "page now 2"), g_out);

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  storage->remove("/apps/entries/app.lua");
  storage->remove("/apps/entries/manifest.json");
  storage->remove("/apps/entries");
}

// **A cell of a box says how much of it it wants** (0.103.0): `cell.share = 30` is thirty per cent of the inside of
// the panel, and 0 (the normal case) is "lay me out by my weight" or by the size I was made with. The field is the
// app's, and the arithmetic is the kernel's (docs/30, docs/90).
// **A grid of entries is a grid of controls** (0.105.0): the app builds one cell for each thing it wants to show —an
// icon and its name, a box of two, a picture— and the control says where each cell is, the same geometry the paint and
// the finger use, and pushes the ones of another page past the frame, where nothing composes them (docs/30, docs/90).
void test_a_grid_of_entries_places_cells_of_controls(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "cells",
                           "{\"title\":\"Cells\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local grid = ui.icon_grid(400, 200)\n"
                           "grid:place(20, 20)\n"
                           "grid.columns = 4\n"
                           "grid.scale = 2\n"
                           "grid.on_page = function(self, page)\n"
                           "  console.print('page ' .. page)\n"
                           "end\n"
                           // **A cell is a control the app builds**: here a mark, nothing else, which is enough to
                           // have a box of its own in the grid.
                           "local cells = {}\n"
                           "for i = 1, 6 do\n"
                           "  local cell = ui.grid_entry(100, 100)\n"
                           "  local words = ui.label(80, 24)\n"
                           "  words.text = 'cell ' .. i\n"
                           "  words:place(8, 8)\n"
                           "  cell:add(words)\n"
                           "  grid:add(cell)\n"
                           "  cells[i] = cell\n"
                           "end\n"
                           "ui.present('gc16')\n"
                           "console.print('cells up')\n"
                           "sys.delay(30):await()\n"
                           // **Where the cells ended up**: the first of the page in the corner of the grid, the second
                           // one column to the right, and the fifth past the frame —four fit, and a page is one row
                           // of four—.
                           "local a = cells[1]:caps()\n"
                           "local b = cells[2]:caps()\n"
                           "console.print('at ' .. a.x .. ' ' .. a.y .. ' ' .. a.width)\n"
                           "console.print('next ' .. b.x .. ' ' .. b.y)\n"
                           "console.print('off ' .. cells[5]:caps().y)\n"
                           // **The page follows the cursor**: with the fifth cell under the dial the grid is on the
                           // second page and that cell is the corner of the frame, which is the same arithmetic the
                           // paint uses —the arrows walk the cells, and a page is where the cell they are on is—.
                           "grid.index = 5\n"
                           "ui.present('gld16')\n"
                           "local e = cells[5]:caps()\n"
                           "console.print('back ' .. e.x .. ' ' .. e.y)\n"
                           "sys.delay(500):await()\n"));

  pump_captured(40, g_out, sizeof(g_out), "run cells");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "cells up"), g_out);
  // **The cell of the corner of the grid**: the frame is a hundred wide per column, so the first cell is at nought and
  // nought and the second one at a hundred.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "at 0 0 100"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "next 100 0"), g_out);
  // **The fifth cell of six in a page of four is past the frame**: the frame is two hundred and the bridge puts what
  // is off the page a frame plus a cell lower, where the clip does not compose it and the walk does not reach it.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "off 240"), g_out);
  // And when the page turns, that same cell is the one in the corner.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "back 0 0"), g_out);

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  storage->remove("/apps/cells/app.lua");
  storage->remove("/apps/cells/manifest.json");
}

void test_a_cell_of_a_box_claims_a_share_of_it(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "box",
                           "{\"title\":\"Box\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local column = ui.panel(200, 100, 'vertical')\n"
                           "column:place(20, 20)\n"
                           "local a = ui.label(10, 10)\n"
                           "a.share = 30\n"
                           "column:add(a)\n"
                           "local b = ui.label(10, 10)\n"
                           "b.share = 70\n"
                           "column:add(b)\n"
                           "ui.present('gc16')\n"
                           "console.print('a ' .. a:caps().height .. ' b ' .. b:caps().height)\n"
                           // **And a row is a VBox turned round**: the same declaration with `horizontal`.
                           "local row = ui.panel(300, 100, 'horizontal')\n"
                           "row:place(20, 200)\n"
                           "local c = ui.label(10, 10)\n"
                           "c.share = 25\n"
                           "row:add(c)\n"
                           "local d = ui.label(10, 10)\n"
                           "d.share = 75\n"
                           "row:add(d)\n"
                           "ui.present('gc16')\n"
                           "console.print('c ' .. c:caps().width .. ' d ' .. d:caps().width)\n"
                           "while true do sys.delay(20):await() end\n"));

  pump_captured(40, g_out, sizeof(g_out), "run box");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "a 30 b 70"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "c 75 d 225"), g_out);

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  storage->remove("/apps/box/app.lua");
  storage->remove("/apps/box/manifest.json");
  storage->remove("/apps/box");
}

// **The walk of the dial is as long as the page** (0.102.0): forty buttons on the glass, and the ring reaches the
// thirty-sixth. The list of stops used to be thirty-two on the stack, so a page with more controls than that had the
// rest unreachable —the ring turned round at the thirty-second and the buttons after it could not be pressed—.
void test_the_dial_walks_more_stops_than_the_old_list_held(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "many",
                           "{\"title\":\"Many\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local column = ui.panel(400, 900, 'vertical')\n"
                           "column:place(20, 10)\n"
                           "column.gap = 0\n"
                           "for i = 1, 40 do\n"
                           "  local b = ui.button(380, 20)\n"
                           "  b.on_focus = function(self, on)\n"
                           "    if on then console.print('at ' .. i) end\n"
                           "  end\n"
                           "  column:add(b)\n"
                           "end\n"
                           "ui.present('gc16')\n"
                           "console.print('many up')\n"
                           "while true do sys.delay(20):await() end\n"));

  pump_captured(60, g_out, sizeof(g_out), "run many");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "many up"), g_out);
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), g_out);

  // Thirty-five arrows down the column: the thirty-sixth button is the one with the ring, and thirty-three of them are
  // past the thirty-two the old list could hold.
  capture_begin();
  pump_turns(10);
  for (int i = 0; i < 35; ++i) {
    emu::press_key(2, true);
    pump_turns(6);
    emu::press_key(2, false);
    pump_turns(6);
  }
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "at 36"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "at 33"), g_out);

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  storage->remove("/apps/many/app.lua");
  storage->remove("/apps/many/manifest.json");
  storage->remove("/apps/many");
}

// **A line of text and the dots of a page take no children** (0.101.0): they are their whole frame and their painter
// owns every pixel of it. It is refused with the error an app can read, and not silently accepted like before, when a
// child sat on top of the word and was part of nothing (docs/30, docs/92).
void test_a_label_and_the_dots_take_no_children(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "nonest",
                           "{\"title\":\"No nest\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local kid = ui.label(20, 20)\n"
                           "local lab = ui.label(100, 40)\n"
                           "local ok, err = lab:add(kid)\n"
                           "console.print('label ' .. tostring(ok) .. ' ' .. tostring(err and err.code))\n"
                           "local dots = ui.dots(100, 20)\n"
                           "local ok2, err2 = dots:add(kid)\n"
                           "console.print('dots ' .. tostring(ok2) .. ' ' .. tostring(err2 and err2.code))\n"
                           // **And the box that does hold it still does**: the refusal is of the control that is
                           // its own frame and not of the tree.
                           "local panel = ui.panel(200, 100, 'vertical')\n"
                           "local ok3, err3 = panel:add(kid)\n"
                           "console.print('panel ' .. tostring(ok3) .. ' ' .. tostring(err3))\n"));

  pump_captured(30, g_out, sizeof(g_out), "run nonest");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "label nil E_INVAL"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "dots nil E_INVAL"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "panel true nil"), g_out);
  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  storage->remove("/apps/nonest/app.lua");
  storage->remove("/apps/nonest/manifest.json");
  storage->remove("/apps/nonest");
}

// **A button that has the ring inverts what it holds** (0.101.0): its face goes to ink while it can be pressed, and
// a label inside it is paper on ink and not a white hole in a black box. Measured before this: the button came out
// as a black bar under a white rectangle, which is a button that a person cannot read (docs/30, docs/92).
void test_a_button_that_has_the_ring_inverts_what_it_holds(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "nest",
                           "{\"title\":\"Nest\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           // **The button is the first control that can take the dial**, so it is the one the ring
                           // lands on the day the board is used (0.179.7: the ring is not painted until then).
                           "local b = ui.button(400, 80)\n"
                           "b:place(40, 40)\n"
                           "b.background = 15\n"
                           "b.border = 0\n"
                           "local w = ui.label(360, 44)\n"
                           "w.text = 'inside'\n"
                           "w.scale = 4\n"
                           "b:add(w)\n"
                           "ui.present('gc16')\n"
                           "console.print('nest up')\n"
                           "while true do sys.delay(20):await() end\n"));

  pump_captured(40, g_out, sizeof(g_out), "run nest");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "nest up"), g_out);
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), g_out);
  emu::MemorySurface* glass = emu::memory_surface();
  TEST_ASSERT_NOT_NULL(glass);
  // **A key of the board is what puts the ring on the glass** (0.179.7): with one stop of the dial the walk stays on the
  // button and the paint it asks for is the ring appearing, which is the state a person presses (docs/30).
  emu::press_key(2, true);
  pump_turns(10);
  emu::press_key(2, false);
  pump_turns(20);
  // The label is where the button put it, at (0, 0) of the button, and the button is at (40, 40) of the view which
  // starts under the bar of the system: its own paper is now ink, four pixels of it even below the label.
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, glass->level_at(390, 100), "the label's paper is the ink of the face");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, glass->level_at(60, 150), "and the face itself is still ink");
  // **And the word is the paper of that face**, which is the whole point: an inverted box with an invisible word is
  // a box a person reads as empty.
  int words = 0;
  for (uint16_t y = 88; y < 132; ++y) {
    for (uint16_t x = 40; x < 220; ++x) {
      if (glass->level_at(x, y) == 15) ++words;
    }
  }
  TEST_ASSERT_GREATER_THAN_MESSAGE(20, words, "the word of the label is in paper");

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  storage->remove("/apps/nest/app.lua");
  storage->remove("/apps/nest/manifest.json");
  storage->remove("/apps/nest");

  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **What a control holds is drawn on the face the control leaves behind** (0.106.0), and it is the control's own
// property and not a list of names inside the draw. A `ui.list_entry` paints its paper in ink when it is the marked one
// or has the ring, so the label inside it has to be told: before this rule it was left on paper, which on an ink row is
// a white patch and a word nobody can read.
void test_an_entry_that_is_marked_inverts_what_it_holds(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "row",
                           "{\"title\":\"Row\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local e = ui.list_entry(400, 80)\n"
                           "e:place(40, 40)\n"
                           "e.background = 15\n"
                           "e.marked = true\n"
                           "local w = ui.label(360, 44)\n"
                           "w.text = 'inside'\n"
                           "w.scale = 4\n"
                           "e:add(w)\n"
                           "ui.present('gc16')\n"
                           "console.print('row up')\n"
                           "while true do sys.delay(20):await() end\n"));

  pump_captured(40, g_out, sizeof(g_out), "run row");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "row up"), g_out);
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), g_out);
  emu::MemorySurface* glass = emu::memory_surface();
  TEST_ASSERT_NOT_NULL(glass);
  // The entry is at (40, 40) of the view, which starts under the bar of the system, and the label is at (0, 0) of it:
  // the paper of the label is the ink of the row, and the word is the paper of that face.
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, glass->level_at(390, 100), "the label's paper is the ink of the row");
  int words = 0;
  for (uint16_t y = 88; y < 132; ++y) {
    for (uint16_t x = 40; x < 220; ++x) {
      if (glass->level_at(x, y) == 15) ++words;
    }
  }
  TEST_ASSERT_GREATER_THAN_MESSAGE(20, words, "the word of the label is in paper");

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  storage->remove("/apps/row/app.lua");
  storage->remove("/apps/row/manifest.json");
  storage->remove("/apps/row");

  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **The state of a box that cannot be used is the state of everything inside it** (0.106.0), whether the app turned the
// father off before or after it put the child in, and **a child the app turned off itself is not given back** when the
// father comes back: the two facts are different and the system keeps them apart.
void test_a_child_of_a_disabled_control_is_born_disabled(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "off",
                           "{\"title\":\"Off\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local p = ui.panel(400, 300)\n"
                           "p:place(20, 20)\n"
                           "p.disabled = true\n"
                           "local inside = ui.button(200, 60)\n"
                           "p:add(inside)\n"
                           "console.print('inside ' .. tostring(inside.disabled))\n"
                           // **One added after the father was turned off**: the walk of the field only saw the
                           // children that were there then, and this is the hole it left.
                           "local late = ui.button(200, 60)\n"
                           "p:add(late)\n"
                           "console.print('late ' .. tostring(late.disabled))\n"
                           "p.disabled = false\n"
                           "console.print('back ' .. tostring(inside.disabled) .. ' ' .. tostring(late.disabled))\n"
                           // **And the one the app turned off itself stays off** when the father comes back.
                           "inside.disabled = true\n"
                           "p.disabled = true\n"
                           "p.disabled = false\n"
                           "console.print('own ' .. tostring(inside.disabled))\n"
                           "sys.delay(500):await()\n"));

  pump_captured(40, g_out, sizeof(g_out), "run off");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "inside true"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "late true"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "back false false"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "own true"), g_out);

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  storage->remove("/apps/off/app.lua");
  storage->remove("/apps/off/manifest.json");
}

// **A control declares whether the dial can land on it** (0.106.0): `focusable` is a field of every control, false by
// default for the ones that are only a drawing, and a button that says no is out of the walk and cannot be given the
// ring —which is what the owner asked for: a physical key has to know whom it may focus—.
void test_a_control_declares_whether_it_takes_the_dial(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "pick",
                           "{\"title\":\"Pick\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local b = ui.button(200, 60)\n"
                           "b:place(20, 20)\n"
                           "b.focusable = false\n"
                           "local w = ui.label(200, 40)\n"
                           "w:place(20, 100)\n"
                           "w.text = 'a stop'\n"
                           "w.focusable = true\n"
                           "ui.present('gc16')\n"
                           // **A button that says no cannot be given the ring**, and a label that says yes can.
                           "ui.focus(b)\n"
                           "console.print('button ' .. tostring(b.focused))\n"
                           "ui.focus(w)\n"
                           "console.print('label ' .. tostring(w.focused))\n"
                           "sys.delay(30):await()\n"
                           // A value of `focusable` that is not true or false is refused, because it is the one
                           // field that decides whether a person can reach a control.
                           "local ok, why = pcall(function() w.focusable = 3 end)\n"
                           "console.print('typo ' .. tostring(ok) .. ' ' .. tostring(why))\n"
                           "sys.delay(500):await()\n"));

  pump_captured(40, g_out, sizeof(g_out), "run pick");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "button false"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "label true"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "typo false"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "true or false"), g_out);

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  storage->remove("/apps/pick/app.lua");
  storage->remove("/apps/pick/manifest.json");
}

// **The slot of the ring is a slot of the table** (0.106.0): it was an `int8_t`, so from the hundred and twenty-eighth
// control of a page the comparison never matched and **the ring was invisible**. The table has held thousands since
// 0.102.0, and this is the number that says so: a page of a hundred and forty buttons takes the ring on the last one.
void test_the_ring_reaches_a_control_past_the_hundred_and_twenty_seventh(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "many",
                           "{\"title\":\"Many\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local last = nil\n"
                           "for i = 1, 140 do\n"
                           "  local b = ui.button(20, 20)\n"
                           "  b:place(0, 0)\n"
                           "  last = b\n"
                           "end\n"
                           "ui.focus(last)\n"
                           "console.print('ring ' .. tostring(last.focused))\n"
                           "sys.delay(500):await()\n"));

  pump_captured(60, g_out, sizeof(g_out), "run many");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "ring true"), g_out);

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  storage->remove("/apps/many/app.lua");
  storage->remove("/apps/many/manifest.json");
}

// **A control can be disabled: it is drawn in the lighter tone of its theme and it answers nothing** (0.100.0).
// It is still on the page —a person reads what is not available right now— but its ink falls to its `dim`, it
// never wears the ring, and it is not a stop of the walk: the dial skips it, the finger does not reach it and the
// keyboard does not open on it, because the three ask the same question (docs/30, docs/92).
void test_a_disabled_control_is_drawn_lighter_and_answers_nothing(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  // The finger is one of the three ways in, so the emulator has a glass that takes one.
  opts.touch = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());
  TEST_ASSERT_NOT_NULL(hal::platform().touch());
  TEST_ASSERT_TRUE(kernel::touch::boot(hal::platform()));

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "off",
                           "{\"title\":\"Off\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local column = ui.panel(340, 240, 'vertical')\n"
                           "column:place(20, 20)\n"
                           "column.gap = 10\n"
                           "local b1 = ui.button(320, 70)\n"
                           "b1.on_focus = function(self, on) console.print('focus one ' .. tostring(on)) end\n"
                           "b1.on_click = function(self) console.print('click one') end\n"
                           "column:add(b1)\n"
                           "local b2 = ui.button(320, 70)\n"
                           "local w = ui.label(200, 40)\n"
                           "w.text = 'off'\n"
                           "b2:add(w)\n"
                           "console.print('child before ' .. tostring(w.disabled))\n"
                           "b2.disabled = true\n"
                           "console.print('child off ' .. tostring(w.disabled))\n"
                           "b2.disabled = false\n"
                           "console.print('child back ' .. tostring(w.disabled))\n"
                           "b2.disabled = true\n"
                           "b2.on_focus = function(self, on) console.print('focus two ' .. tostring(on)) end\n"
                           "b2.on_click = function(self) console.print('click two') end\n"
                           "column:add(b2)\n"
                           "local b3 = ui.button(320, 70)\n"
                           "b3.on_focus = function(self, on) console.print('focus three ' .. tostring(on)) end\n"
                           "b3.on_click = function(self) console.print('click three') end\n"
                           "column:add(b3)\n"
                           "local lower = ui.panel(340, 140, 'vertical')\n"
                           "lower:place(20, 300)\n"
                           "lower.gap = 0\n"
                           "local r1 = ui.row(320, 60)\n"
                           "r1.title = 'on'\n"
                           "r1.marked = true\n"
                           "lower:add(r1)\n"
                           "local r2 = ui.row(320, 60)\n"
                           "r2.title = 'off'\n"
                           "r2.marked = true\n"
                           "r2.disabled = true\n"
                           "lower:add(r2)\n"
                           "ui.present('gc16')\n"
                           "console.print('off up')\n"
                           "while true do sys.delay(20):await() end\n"));

  pump_captured(40, g_out, sizeof(g_out), "run off");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "off up"), g_out);
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), g_out);
  // **The state goes down the tree**: the label inside the button is off with it, because the box and the word
  // inside it are one thing to the person reading the page, and it comes back when the button does (docs/30,
  // docs/92).
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "child before nil"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "child off true"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "child back false"), g_out);
  emu::MemorySurface* glass = emu::memory_surface();
  TEST_ASSERT_NOT_NULL(glass);
  // **The two rows are the same drawing with one field between them**: the marked row fills itself with the ink of
  // its words, so the one that is on is a block of black and the one that is off is a block of its `dim` (8), which
  // is the level a secondary word already uses (docs/30, docs/92).
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, glass->level_at(170, 378), "the row that is on is in ink");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(8, glass->level_at(170, 438), "and the row that is off, in its lighter tone");

  // **One arrow skips it**: the ring goes from the first button to the third, and the middle one never hears the
  // focus because it is not a stop at all.
  capture_begin();
  pump_turns(10);
  emu::press_key(2, true);
  pump_turns(20);
  emu::press_key(2, false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "focus three true"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "focus two"), g_out);

  // **And the centre presses the one the ring is on**: a disabled control does not answer, and nothing here ever
  // says it did.
  capture_begin();
  pump_turns(10);
  emu::press_key(0, true);
  pump_turns(20);
  emu::press_key(0, false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "click three"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "click two"), g_out);

  // **And the finger does not reach it either**: a tap on the disabled button lands on nothing, while the same tap
  // on the one that is on is a click. The hand and the dial ask the same question, so a page cannot be read one way
  // by the keys and another by the hand (docs/30, docs/50, docs/92).
  emu::Gt911Panel* panel = emu::touch_panel();
  TEST_ASSERT_NOT_NULL(panel);
  capture_begin();
  TEST_ASSERT_TRUE(panel->finger_down(0, 170, 190));  // the second button, the one that is off
  pump_turns(10);
  TEST_ASSERT_TRUE(panel->finger_up(0));
  pump_turns(30);
  TEST_ASSERT_TRUE(panel->finger_down(0, 170, 100));  // and the first one, which is on
  pump_turns(10);
  TEST_ASSERT_TRUE(panel->finger_up(0));
  pump_turns(30);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "click one"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "click two"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "focus two"), g_out);

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  storage->remove("/apps/off/app.lua");
  storage->remove("/apps/off/manifest.json");
  storage->remove("/apps/off");

  // The next test gets the default emulator back: the display and the finger were this test's.
  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  opts.touch = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **Every tab of a strip is a stop of the dial** (0.99.0), which is what the paper project does: its tabs are
// `Button`s and the navigator walks them one at a time, the arrows go from one tab to the next and out of the
// strip at either end, and the centre chooses the tab the ring is on. One stop for the whole strip made the arrows
// change the tab instead of moving the dial, and the page under it could not be reached without turning pages
// (the owner, 7/11 and 8/11, docs/92).
void test_each_tab_of_a_strip_is_a_stop_of_the_dial(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "stops",
                           "{\"title\":\"Stops\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local tabs = ui.tabs(300, 40)\n"
                           "tabs:place(20, 20)\n"
                           "tabs.items = {'One', 'Two', 'Three'}\n"
                           "tabs.index = 1\n"
                           "tabs.on_change = function(self, index) console.print('chose ' .. index) end\n"
                           "tabs.on_focus = function(self, on) console.print('strip focus ' .. tostring(on)) end\n"
                           "local column = ui.panel(300, 200, 'vertical')\n"
                           "column:place(20, 100)\n"
                           "local b = ui.button(280, 60)\n"
                           "b.on_focus = function(self, on) console.print('button focus ' .. tostring(on)) end\n"
                           "b.on_click = function(self) console.print('button pressed') end\n"
                           "column:add(b)\n"
                           "ui.present('gc16')\n"
                           "console.print('stops up')\n"
                           "while true do sys.delay(20):await() end\n"));

  pump_captured(40, g_out, sizeof(g_out), "run stops");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "stops up"), g_out);
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), g_out);
  // **The tab that will answer is the one drawn inverted**, and the one next to it is paper: the strip sits at
  // (20, 20) of the view and the view starts under the bar of the system, so a point four pixels inside each tab
  // and in the middle of its height is on the face and not on the word (docs/50, docs/92).
  emu::MemorySurface* glass = emu::memory_surface();
  TEST_ASSERT_NOT_NULL(glass);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, glass->level_at(24, 88), "the first tab opens with the ring on it");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(15, glass->level_at(124, 88), "and the second one is paper");

  // **One arrow, one tab**: the ring moves inside the strip and the button below it is still not the dial's, which
  // is the whole difference with the strip of one stop (the first arrow then landed on the button).
  capture_begin();
  pump_turns(10);
  emu::press_key(2, true);
  pump_turns(20);
  emu::press_key(2, false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "button focus true"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "chose"), g_out);
  // **And the glass moved the ring to that tab**: the inversion follows the dial and not the page.
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(15, glass->level_at(24, 88), "the first tab is paper now");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, glass->level_at(124, 88), "and the second one has the ring");

  // **Two more and the walk leaves the strip**: the third tab was the last stop of it, and the next arrow lands on
  // the button of the page under it. No tab was chosen on the way, because an arrow does not choose.
  capture_begin();
  pump_turns(10);
  for (int i = 0; i < 2; ++i) {
    emu::press_key(2, true);
    pump_turns(20);
    emu::press_key(2, false);
    pump_turns(20);
  }
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "button focus true"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "chose"), g_out);

  // **And the centre presses what the ring has**, which after three stops is the button and not a tab.
  capture_begin();
  pump_turns(10);
  emu::press_key(0, true);
  pump_turns(20);
  emu::press_key(0, false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "button pressed"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "chose"), g_out);

  // **A walk back leaves the button and lands on the last tab**, which is the strip's end nearest to it.
  capture_begin();
  pump_turns(10);
  emu::press_key(1, true);
  pump_turns(20);
  emu::press_key(1, false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "button focus false"), g_out);

  // And the centre on that tab is what turns the page: the app hears the tab the ring is on, which is the last one.
  capture_begin();
  pump_turns(10);
  emu::press_key(0, true);
  pump_turns(20);
  emu::press_key(0, false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "chose 3"), g_out);

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  storage->remove("/apps/stops/app.lua");
  storage->remove("/apps/stops/manifest.json");
  storage->remove("/apps/stops");

  // The next test gets the default emulator back: the display was this test's.
  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **A key paints its own answer on the glass** (0.98.0). The ring of the focus and the button going down are the
// system's doing —the app never sees the key, `ui_button_event` takes it— so the paint that shows them is the
// system's too. A page that the app presented once and then went to sleep has to show where the dial went; without
// that paint the controls move in the tree and the glass says nothing, which is what the owner read on the board as
// *«l'app Showcase no respon a l'usuari»* (docs/30, docs/50, docs/92).
void test_a_key_paints_the_ring_the_app_cannot_see(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "ring",
                           "{\"title\":\"Ring\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local column = ui.panel(500, 400, 'vertical')\n"
                           "column:place(20, 20)\n"
                           "local a = ui.button(400, 68)\n"
                           "column:add(a)\n"
                           "local b = ui.button(400, 68)\n"
                           "column:add(b)\n"
                           "ui.present('gc16')\n"
                           "console.print('ring up')\n"
                           "while true do sys.delay(50):await() end\n"));
  emu::MemorySurface* glass = emu::memory_surface();
  TEST_ASSERT_NOT_NULL(glass);

  pump_captured(40, g_out, sizeof(g_out), "run ring");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "ring up"), g_out);
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), g_out);
  const uint32_t paints_before = glass->paint_count();

  // The dial goes to the second button of a page that will not present again: the app is in its wait and has no
  // hook on the focus, so the only thing that can put the ring on the glass is the system.
  capture_begin();
  pump_turns(10);
  emu::press_key(2, true);
  pump_turns(20);
  emu::press_key(2, false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_TRUE_MESSAGE(glass->paint_count() > paints_before, g_out);

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  storage->remove("/apps/ring/app.lua");
  storage->remove("/apps/ring/manifest.json");
  storage->remove("/apps/ring");

  // The next test gets the default emulator back: the display was this test's.
  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **And the centre is answered by the page the app wrote** (0.98.0): the ring lands on the first control the app
// gave the dial, the centre presses it, and the app hears `on_click` and says so. It is the whole way a person
// uses the showcase —dial, press, answer— and the one the board had to make before this test existed (docs/30,
// docs/92).

// The demo app that downloads a page is a file in the repository, so it is run
// here: an example that stops working is a lie in the documentation.

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

// **A file is read in pieces and the emulator does not say no** (0.66.0): `fs.read_at` asks the interpreter that grows for
// a block, and the heap of the emulator answered `not enough memory` with megabytes free because a block that could not
// grow where it was only tried the other memory (docs/75, docs/85). The ceiling of this test emulator is the one of the ATOM
// (64 KB per app), so the piece is 12 KB and the file is 60: what is tested is that every piece arrives whole.
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

// **The path the prompt names is the only door of the app to the disk of the person** (0.65.0): an app started with
// `run <id> <path>` can read and write **that** file, and any other path of the disk of the person keeps answering
// `E_INVAL`. It is what makes an editor save where the person said and not where the app wants
// (docs/30, docs/70).
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
  // **The rest of the disk stays closed**: the named path is an exception and not an open door.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "other nil E_INVAL"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "read from the app"), g_out);
  // **The temporary of the named path is the named path too** (0.66.0): saving from an app is writing `<path>.tmp`
  // and renaming it, and without that the atomic write could not exist from an app.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "temp true nil"), g_out);

  // **And the name outlives the command** (4/11): the word of the prompt lives on a stack that is not there any more
  // when the app runs, and the path the kernel asks about to let it save has to be a copy.
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
  // The emulator is the ATOM here: one light, so light number five is not there.
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

  // The Paper: the chip answers the two numbers the emulator was told (docs/85).
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

// The Matrix app of the repository, run on the fake panel: twenty-five lights painted
// one by one with a hue that walks around them, which is the one thing the app of the
// single light cannot say, and the whole panel given back when it goes (docs/30,
// docs/50).

// What makes an app an app **of this board**: the boards it lists, and the chip
// family, which every board of it accepts. The discard happens in `apps::load()`,
// and no PC can walk that branch because the emulator discards nothing (docs/85), so
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
// rule, a list that is not this board's, is the unit test above, because the emulator
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
// (docs/40). The board named in the refusal is the one the emulator was told to be —the
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

// The emulator is not a board, and a manifest does not name it: the person says which one it
// stands in for and the emulator answers that name, so an app runs here with the list it
// would carry on the glass — and an app of another board is refused, which is the half
// that proves the answer is the model and not "yes to everything" (docs/40, docs/85).
void test_the_emulator_is_the_board_it_was_told_to_be(void) {
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

  // And the Matrix's app, on the same emulator, is refused before a VM exists.
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

// **The launcher of the glass** (0.153.0): the app of `apps/home_eink` on a real disk, reading the door of the apps and
// nothing else. It lists what the door presents —and not itself, and not another launcher—, it counts the apps of
// another board in its line instead of the grid, it says the one whose picture does not come up, and **a press of the
// centre starts the app of the first cell**: the first cell is the one the dial is born on, and the app that is opened
// ends by itself, which is where the page has to come back.
void test_the_launcher_lists_the_apps_and_starts_the_one_that_is_pressed(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  // **The finger is one of the ways a launcher is used**, so the emulator has a glass that takes one (docs/50).
  opts.touch = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());
  TEST_ASSERT_NOT_NULL(hal::platform().touch());
  TEST_ASSERT_TRUE(kernel::touch::boot(hal::platform()));

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  // **The launcher of the repository and not a copy of it**: its manifest, its script and its picture, because what is
  // under test is the file that ships (docs/45).
  TEST_ASSERT_TRUE(storage->mkdir("/apps/home_eink"));
  TEST_ASSERT_TRUE(storage->mkdir("/apps/home_eink/res"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/home_eink/home_eink.lua", "/apps/home_eink/home_eink.lua"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/home_eink/manifest.json", "/apps/home_eink/manifest.json"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/home_eink/res/icon.png", "/apps/home_eink/res/icon.png"));
  // **Two apps to list**, of which one has the picture of an app and the other has none: the page has to say the second
  // once, because an empty cell and a broken picture look the same on the glass (docs/30).
  TEST_ASSERT_TRUE(add_app(storage, "alpha",
                           "{\"title\":\"Alpha\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"],"
                           "\"icon\":\"res/icon.png\"}",
                           "console.print('alpha here')\n"
                           "sys.delay(300):await()\n"
                           "console.print('alpha bye')\n"));
  TEST_ASSERT_TRUE(storage->mkdir("/apps/alpha/res"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/home_eink/res/icon.png", "/apps/alpha/res/icon.png"));
  TEST_ASSERT_TRUE(add_app(storage, "bravo",
                           "{\"title\":\"Bravo\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "console.print('bravo here')\n"
                           "while true do sys.delay(20):await() end\n"));
  // **Another launcher and an app of another board**: neither is on the grid, and the second is counted in the line.
  TEST_ASSERT_TRUE(add_app(storage, "other_launcher",
                           "{\"title\":\"Other\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"],"
                           "\"launcher\":true}",
                           "console.print('other here')\n"
                           "while true do sys.delay(20):await() end\n"));
  TEST_ASSERT_TRUE(add_app(storage, "elsewhere",
                           "{\"title\":\"Elsewhere\",\"version\":\"0.1.0\",\"entry\":\"app.lua\","
                           "\"boards\":[\"m5stack-atom-matrix\"]}",
                           "console.print('elsewhere here')\n"
                           "while true do sys.delay(20):await() end\n"));

  // **A turn per app in the queue and fifty milliseconds between turns**: the ten names of this disk are read in about
  // a second of the emulator's time, so the capture has to be longer than the queue and not as long as one page.
  pump_captured(1500, g_out, sizeof(g_out), "run home_eink");
  // **The page arrives before its pictures** (0.154.0): the names of the door are said first, the whole glass is painted
  // with a placeholder in every cell, and only then does the queue read the manifests —one app per turn— and say the
  // name a person reads for each. The order of the two in the console is the whole point of the two calls.
  const char* ids_said = strstr(g_out, " ids: ");
  const char* whole_page = strstr(g_out, "paint 0,0 540x912");
  const char* first_app = strstr(g_out, "home: app ");
  TEST_ASSERT_NOT_NULL_MESSAGE(ids_said, g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(whole_page, g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(first_app, g_out);
  TEST_ASSERT_TRUE_MESSAGE(ids_said < whole_page, g_out);
  TEST_ASSERT_TRUE_MESSAGE(whole_page < first_app, g_out);
  // The queue read the manifest of the two apps that are apps of this board, and **the launcher that is asking was never
  // asked for** —its own name leaves the list before anything is read— and the other launcher and the app of another
  // board are taken out when their manifest says so (docs/30, docs/40).
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "home: app alpha Alpha"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "home: app bravo Bravo"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "home: other_launcher is a launcher"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "home: elsewhere needs another board"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "home: home_eink"), g_out);
  // **The picture of the one that has one is drawn and the other is said**, once, in the console.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "home: bravo has no picture"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "home: alpha has no picture"), g_out);
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), g_out);
  TEST_ASSERT_EQUAL_STRING("home_eink", kernel::shell::app_id());

  // **A press of the centre is a click on the first cell**, which is the control the dial is born on.
  capture_begin();
  emu::press_button(true);
  pump_turns(80);
  emu::press_button(false);
  pump_turns(600);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "home: alpha"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "alpha here"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "alpha bye"), g_out);
  // **And the page the person comes back to is the launcher** (0.153.0): the app that asked for `alpha` is started again
  // when `alpha` ends, which is the whole reason `app.run` takes a second argument (docs/30, docs/90).
  TEST_ASSERT_EQUAL_STRING_MESSAGE("home_eink", kernel::shell::app_id(), g_out);
  const char* after = strstr(g_out, "alpha bye");
  TEST_ASSERT_NOT_NULL(after);
  // **And the page it comes back to is whole without reading a manifest** (0.179.0): the manifests and the pictures of the
  // cells are in the memory of the session that the kernel keeps under the key `apps`, so the second page costs no disk —
  // the cache is warm, the grid has every cell, and `home: app` is not in this part of the console because nothing was
  // asked for (docs/30, docs/75).
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(after, "home: cache warm"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(after, " cells"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(after, "home: app "), g_out);

  // **A resize builds the page again and gives the grid every cell it has** (0.157.0): the pages are the control's, so
  // what the launcher says of itself is how many names the door answered and not which page is on the glass —that is the
  // grid's own footer, and the case of the pages below walks it—.
  pump_turns(1500);
  capture_begin();
  TEST_ASSERT_TRUE(kernel::ui::resize(420, 360));
  pump_turns(80);
  capture_end(g_out, sizeof(g_out));
  // The page is laid out again in the new box: the whole of it is painted, and its line says how many cells the grid has.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "paint 0,0 420x"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, " cells"), g_out);
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), g_out);
  TEST_ASSERT_EQUAL_STRING("home_eink", kernel::shell::app_id());

  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), g_out);
  TEST_ASSERT_EQUAL_STRING("home_eink", kernel::shell::app_id());

  kernel::shell::close_app();
  pump_captured(10, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());

  storage->remove("/apps/home_eink/res/icon.png");
  storage->remove("/apps/home_eink/res");
  storage->remove("/apps/home_eink/home_eink.lua");
  storage->remove("/apps/home_eink/manifest.json");
  storage->remove("/apps/home_eink");
  storage->remove("/apps/alpha/res/icon.png");
  storage->remove("/apps/alpha/res");
  storage->remove("/apps/alpha/app.lua");
  storage->remove("/apps/alpha/manifest.json");
  storage->remove("/apps/alpha");
  storage->remove("/apps/bravo/app.lua");
  storage->remove("/apps/bravo/manifest.json");
  storage->remove("/apps/bravo");
  storage->remove("/apps/other_launcher/app.lua");
  storage->remove("/apps/other_launcher/manifest.json");
  storage->remove("/apps/other_launcher");
  storage->remove("/apps/elsewhere/app.lua");
  storage->remove("/apps/elsewhere/manifest.json");
  storage->remove("/apps/elsewhere");

  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **A cache of the session that the memory takes is not the end of the page** (0.179.8). The owner pasted the log of a run
// where the relief gave the launcher's block back —`[I][MEMORY] home_eink gives back 'apps': 32768 B, the memory is short`
// and `… 1 block(s), 32768 B given back for an ask of 95296 B`— and the launcher went on painting: four lines of
// `present: … the session memory was given back` and a page whose cells were letters again. The block answers `alive()` and
// the app asks for another one in the turn after, which is the whole contract of a memory the system may reclaim
// (docs/30, docs/75).
void test_a_cache_of_the_session_the_memory_takes_does_not_kill_the_page(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  // **The launcher of the repository and not a copy of it**: what is under test is the file that ships (docs/45).
  TEST_ASSERT_TRUE(storage->mkdir("/apps/home_eink"));
  TEST_ASSERT_TRUE(storage->mkdir("/apps/home_eink/res"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/home_eink/home_eink.lua", "/apps/home_eink/home_eink.lua"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/home_eink/manifest.json", "/apps/home_eink/manifest.json"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/home_eink/res/icon.png", "/apps/home_eink/res/icon.png"));
  // **Two apps with a picture each**, so the block holds two pictures and a cell that paints again is a cell that would have
  // read its picture out of the block the memory has taken.
  const char* const kTwoApps[] = {"alpha", "bravo"};
  for (const char* id : kTwoApps) {
    char manifest[192];
    snprintf(manifest, sizeof(manifest),
             "{\"title\":\"%s\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"],"
             "\"icon\":\"res/icon.png\"}",
             id);
    TEST_ASSERT_TRUE(add_app(storage, id, manifest, "while true do sys.delay(20):await() end\n"));
    char dir[32];
    snprintf(dir, sizeof(dir), "/apps/%s/res", id);
    TEST_ASSERT_TRUE(storage->mkdir(dir));
    char png[40];
    snprintf(png, sizeof(png), "/apps/%s/res/icon.png", id);
    TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/home_eink/res/icon.png", png));
  }

  pump_captured(1500, g_out, sizeof(g_out), "run home_eink");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, " cells"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "home: cache kept"), g_out);
  TEST_ASSERT_EQUAL_UINT8(1, kernel::session::count());
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), g_out);
  TEST_ASSERT_EQUAL_STRING("home_eink", kernel::shell::app_id());

  // **The relief of the allocator, which is the door it uses** (0.179.6): every session memory goes, and the launcher's
  // `cache` object is a block of somebody else's from this line on.
  kernel::session::release_all();
  TEST_ASSERT_EQUAL_UINT8(0, kernel::session::count());

  // **And a key paints the page again**: the cell the ring lands on is drawn with its picture, which is the draw that used
  // to raise. Nothing of this may reach the console as a `present:` failure (docs/30, docs/50).
  capture_begin();
  emu::press_key(2, true);
  pump_turns(30);
  emu::press_key(2, false);
  pump_turns(400);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "the session memory was given back"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "present: "), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "home: the cache of the session was given back"), g_out);
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), g_out);
  TEST_ASSERT_EQUAL_STRING("home_eink", kernel::shell::app_id());

  // **And the turn after asks for another one, outside every draw**: a block that is asked for again is zeroed, so it says
  // `cache cold`, and the page writes its records into it once the names have all arrived (docs/30, docs/75). Both lines
  // come after the draw that found the block gone, which is the order the loop of this app has (docs/30).
  const char* taken = strstr(g_out, "home: the cache of the session was given back");
  const char* cold = strstr(g_out, "home: cache cold, ");
  const char* kept = strstr(g_out, "home: cache kept");
  TEST_ASSERT_NOT_NULL_MESSAGE(cold, g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(kept, g_out);
  TEST_ASSERT_TRUE_MESSAGE(taken < cold, g_out);
  TEST_ASSERT_TRUE_MESSAGE(cold < kept, g_out);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, kernel::session::count(), g_out);

  kernel::shell::close_app();
  pump_captured(10, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  storage->remove("/apps/home_eink/res/icon.png");
  storage->remove("/apps/home_eink/res");
  storage->remove("/apps/home_eink/home_eink.lua");
  storage->remove("/apps/home_eink/manifest.json");
  storage->remove("/apps/home_eink");
  for (const char* id : kTwoApps) {
    char dir[40];
    snprintf(dir, sizeof(dir), "/apps/%s/res/icon.png", id);
    storage->remove(dir);
    snprintf(dir, sizeof(dir), "/apps/%s/res", id);
    storage->remove(dir);
    snprintf(dir, sizeof(dir), "/apps/%s/app.lua", id);
    storage->remove(dir);
    snprintf(dir, sizeof(dir), "/apps/%s/manifest.json", id);
    storage->remove(dir);
    snprintf(dir, sizeof(dir), "/apps/%s", id);
    storage->remove(dir);
  }
  kernel::session::release_all();

  emu::Options opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **A step of the dial in the launcher costs the cells it crosses and nothing else** (0.177.4). The map of what the glass
// owes is the law of this system, and the launcher is where it was being broken twice over: the walk invalidated the
// control the dial left **three times** —`focused`, `editing` and `focus_index`, the last two because a field the app
// never wrote was answered as a change—, and the pieces of two cells that do not touch were joined into the band they
// span. Measured with the keys: the step from the last cell of a row to the first of the next painted **two rows**
// (`500x246`) for two cells of `250x123`, and the picture of each cell was presented again inside the band that already
// had it (docs/30, docs/90).
void test_a_step_of_the_dial_in_the_launcher_paints_the_cells_it_crosses(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  storage->mkdir("/apps/home_eink");
  storage->mkdir("/apps/home_eink/res");
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/home_eink/home_eink.lua", "/apps/home_eink/home_eink.lua"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/home_eink/manifest.json", "/apps/home_eink/manifest.json"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/home_eink/res/icon.png", "/apps/home_eink/res/icon.png"));
  // **Nine apps of two lines**, so the page is a grid of more than one row and the step that wraps is in it.
  for (int i = 1; i <= 9; ++i) {
    char id[16];
    snprintf(id, sizeof(id), "app%d", i);
    char manifest[256];
    snprintf(manifest, sizeof(manifest),
             "{\"title\":\"App %d\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"],"
             "\"icon\":\"res/icon.png\"}",
             i);
    TEST_ASSERT_TRUE(add_app(storage, id, manifest, "while true do sys.delay(20):await() end\n"));
    char dir[32];
    snprintf(dir, sizeof(dir), "/apps/app%d/res", i);
    TEST_ASSERT_TRUE(storage->mkdir(dir));
    char png[32];
    snprintf(png, sizeof(png), "/apps/app%d/res/icon.png", i);
    TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/home_eink/res/icon.png", png));
  }

  pump_captured(2500, g_out, sizeof(g_out), "run home_eink");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, " cells"), g_out);

  // **Three steps of the dial**, the last of the first row and the two of the second.
  capture_begin();
  for (int step = 0; step < 3; ++step) {
    emu::press_key(2, true);
    pump_turns(15);
    emu::press_key(2, false);
    pump_turns(40);
  }
  capture_end(g_out, sizeof(g_out));
  // **The ink of the three steps**: six cells of two hundred and fifty by a hundred and twenty-three are **184 500 pixels**,
  // and that is the ceiling with a tenth of air for the edges of the damage. The band of two rows cost **278 000** before
  // this —half of it the two cells of the row that nobody touched— (measured, `test/test_apps`).
  const int32_t ink = painted_pixels(g_out);
  TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(200000, ink, g_out);
  // **And the band that spans two rows is not there**: the cell of the row above and the cell of the row below are two
  // regions, because joining them paints the air between and the two cells that nobody touched.
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "500x246"), g_out);
  // The cells themselves are what was painted, one region each where they do not touch.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "paint 270,8 250x123"), g_out);

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  storage->remove("/apps/home_eink/res/icon.png");
  storage->remove("/apps/home_eink/res");
  storage->remove("/apps/home_eink/home_eink.lua");
  storage->remove("/apps/home_eink/manifest.json");
  storage->remove("/apps/home_eink");
  for (int i = 1; i <= 9; ++i) {
    char png[32];
    snprintf(png, sizeof(png), "/apps/app%d/res/icon.png", i);
    storage->remove(png);
    char dir[32];
    snprintf(dir, sizeof(dir), "/apps/app%d/res", i);
    storage->remove(dir);
    char lua[32];
    snprintf(lua, sizeof(lua), "/apps/app%d/app.lua", i);
    storage->remove(lua);
    char manifest[32];
    snprintf(manifest, sizeof(manifest), "/apps/app%d/manifest.json", i);
    storage->remove(manifest);
    char app_dir[32];
    snprintf(app_dir, sizeof(app_dir), "/apps/app%d", i);
    storage->remove(app_dir);
  }
}

// **The picture of a cell is turned over with the cell** (0.178.0): a selected cell of this system is a black block, and
// what the app put inside it has to go with it. The old project turned its whole selected cell over with one call —its
// notes say the bitmap door could not be used there because `it paints the paper of the drawing too (15)`— and that is why
// an icon there had no white box of its own. A canvas of this system with no `on_draw` cannot do it —it composes the same
// box it holds— and with one it can: it is told which face it is drawn on (docs/30, docs/92).
void test_the_picture_of_a_cell_is_turned_over_with_the_cell(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "ink",
                           "{\"title\":\"Ink\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           // Two cells of the app's own, each with a picture that is **all paper**: a box that is one
                           // level from edge to edge, so a point inside it says whether it was turned over.
                           "local grid = ui.icon_grid(400, 200)\n"
                           "grid:place(20, 20)\n"
                           "grid.columns = 2\n"
                           "local cells = {}\n"
                           "local pics = {}\n"
                           "for i = 1, 2 do\n"
                           "  local cell = ui.grid_entry(200, 100)\n"
                           "  cell.background = 15\n"
                           "  local pic = ui.canvas(60, 60)\n"
                           "  pic:place(10, 10)\n"
                           "  pic.on_draw = function(self, inverted)\n"
                           "    self:clear(15)\n"
                           "    if inverted then self:invert(0, 0, 60, 60) end\n"
                           "  end\n"
                           "  cell:add(pic)\n"
                           "  grid:add(cell)\n"
                           "  cells[i] = cell\n"
                           "  pics[i] = pic\n"
                           "end\n"
                           "ui.focus(cells[1])\n"
                           "ui.present('gc16')\n"
                           "for i = 1, 2 do\n"
                           "  local p = pics[i]:caps()\n"
                           "  local c = cells[i]:caps()\n"
                           "  console.print(string.format('pic %d %d', 20 + c.x + p.x, 20 + c.y + p.y))\n"
                           "end\n"
                           "while true do sys.delay(20):await() end\n"));

  pump_captured(300, g_out, sizeof(g_out), "run ink");
  const char* first = strstr(g_out, "pic ");
  TEST_ASSERT_NOT_NULL_MESSAGE(first, g_out);
  int x1 = 0;
  int y1 = 0;
  TEST_ASSERT_EQUAL_INT_MESSAGE(2, sscanf(first + 4, "%d %d", &x1, &y1), g_out);
  const char* second = strstr(first + 4, "pic ");
  TEST_ASSERT_NOT_NULL_MESSAGE(second, g_out);
  int x2 = 0;
  int y2 = 0;
  TEST_ASSERT_EQUAL_INT_MESSAGE(2, sscanf(second + 4, "%d %d", &x2, &y2), g_out);

  // **Nothing is turned over yet** (0.179.7): `ui.focus(cells[1])` puts the dial on the first cell, but the ring is not
  // painted until a key of the board is used, so both pictures are the paper they were drawn with (docs/30).
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      15, page_level(static_cast<uint16_t>(x1 + 6), static_cast<uint16_t>(y1 + 6)), g_out);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      15, page_level(static_cast<uint16_t>(x2 + 6), static_cast<uint16_t>(y2 + 6)), g_out);

  // **And the key brings the ring on the next cell**: the picture of the cell it lands on is drawn on ink, so the whole
  // box of it is ink where it was paper, and the one it left keeps its own.
  capture_begin();
  emu::press_key(2, true);
  pump_turns(30);
  emu::press_key(2, false);
  pump_turns(60);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      15, page_level(static_cast<uint16_t>(x1 + 6), static_cast<uint16_t>(y1 + 6)), g_out);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      0, page_level(static_cast<uint16_t>(x2 + 6), static_cast<uint16_t>(y2 + 6)), g_out);

  // **And the next key goes on with it**, which is the whole point: the picture follows the face of its cell in both
  // directions, and the page is a ring, so the turn comes back to the first cell.
  emu::press_key(2, true);
  pump_turns(30);
  emu::press_key(2, false);
  pump_turns(60);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      0, page_level(static_cast<uint16_t>(x1 + 6), static_cast<uint16_t>(y1 + 6)), g_out);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      15, page_level(static_cast<uint16_t>(x2 + 6), static_cast<uint16_t>(y2 + 6)), g_out);

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  storage->remove("/apps/ink/app.lua");
  storage->remove("/apps/ink/manifest.json");
  storage->remove("/apps/ink");
}

// **A field the app never wrote reads its default** (0.179.11): the system writes `editing`, `focus_index` and the rest on
// the controls the dial touches, and **writing the default of a field that was `nil` is not a change of pixels** —that was
// three invalidations of the control the dial left for a ring that moves— but the field still has to be there: an app reads
// them (`#text == self.cursor`, `self.focus_index + 1`) and a number that is `nil` is an error in the app, not a smaller
// drawing (docs/30, docs/90).
void test_a_field_the_app_never_wrote_reads_its_default(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "fields",
                           "{\"title\":\"Fields\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local a = ui.button(140, 40)\n"
                           "a:place(20, 20)\n"
                           "a.title = 'A'\n"
                           "local b = ui.button(140, 40)\n"
                           "b:place(20, 80)\n"
                           "b.title = 'B'\n"
                           "ui.focus(a)\n"
                           "ui.present('gc16')\n"
                           // **The fields are read once the dial has gone**, which is after the walk has written what it
                           // writes: the hook of the control that loses the ring is called before them and a person's
                           // app reads the state it had (docs/30).
                           "local said = false\n"
                           "while true do\n"
                           "  sys.delay(20):await()\n"
                           "  if not said and not a.focused then\n"
                           "    said = true\n"
                           "    console.print('a reads ' .. tostring(a.focus_index) .. ' ' .. tostring(a.editing))\n"
                           "  end\n"
                           "end\n"));

  pump_captured(300, g_out, sizeof(g_out), "run fields");
  // The key walks from the first button to the second, and the first one says what it reads as the dial leaves it.
  capture_begin();
  emu::press_key(2, true);
  pump_turns(30);
  emu::press_key(2, false);
  pump_turns(60);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "a reads 0 false"), g_out);

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  storage->remove("/apps/fields/app.lua");
  storage->remove("/apps/fields/manifest.json");
  storage->remove("/apps/fields");
}

// **The ring that comes back costs the cell it is on** (0.179.11): a finger puts the selector of the focus away and the next
// key brings it back, and a ring that comes or goes on a grid of the kernel's cells is **ink of that cell** and nothing else
// —the cursor does not move, so no page is compared and the paper of the grid is not painted—. It is the law of 0.177.3 and
// the door that hides the ring had forgotten it, so the first key of a page repainted every picture of the grid.
void test_the_ring_that_comes_back_costs_the_cell_it_is_on(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "onecell",
                           "{\"title\":\"One\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           // One item of a grid of three columns: a cell of a hundred by a hundred and two in a frame of
                           // three hundred, which is what makes the difference between one cell and the page readable.
                           "local grid = ui.icon_grid(300, 300)\n"
                           "grid:place(20, 60)\n"
                           "grid.columns = 3\n"
                           "grid.items = { { label = 'One', mark = '1' } }\n"
                           "ui.present('gc16')\n"
                           "while true do sys.delay(20):await() end\n"));

  pump_captured(300, g_out, sizeof(g_out), "run onecell");
  // **The ring is off until a key** (0.179.7), so the page is up with no ring at all and this key is the first that paints
  // it. What it may cost is the cell: 10 200 pixels of a cell of a hundred by a hundred and two, and never the 90 000 of the
  // frame it sits in.
  capture_begin();
  emu::press_key(2, true);
  pump_turns(30);
  emu::press_key(2, false);
  pump_turns(40);
  capture_end(g_out, sizeof(g_out));
  const int32_t ink = painted_pixels(g_out);
  TEST_ASSERT_GREATER_THAN_MESSAGE(2000, ink, g_out);
  TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(30000, ink, g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "300x300"), g_out);

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  storage->remove("/apps/onecell/app.lua");
  storage->remove("/apps/onecell/manifest.json");
  storage->remove("/apps/onecell");
}

// **The page is a ring and every stop of it is a control a person can see** (0.156.1): the dial goes from the last
// control of the glass to the first and from the first back to the last, and **a grid with the app's own cells in it
// is a box and not a stop** —it was a phantom the ring could land on, where nothing was ringed and the walk went on in
// the order of the tree, which the owner read as the focus leaving the page (his words of 8/11: *«sembla que et pots
// sortir per l'esquerra i per la dreta amb el focus»*)—.
void test_the_dial_walks_the_controls_of_the_page_in_a_ring(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "ring",
                           "{\"title\":\"Ring\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           // **The hook says only where the ring arrives**, which is the half of it that a page
                           // reads: it is called with `true` on the control it lands on and `false` on the one it
                           // leaves (docs/30).
                           "local function tag(what)\n"
                           "  return function(self, on)\n"
                           "    if on then console.print('focus ' .. what) end\n"
                           "  end\n"
                           "end\n"
                           "local grid = ui.icon_grid(300, 420)\n"
                           "grid:place(20, 20)\n"
                           "grid.columns = 2\n"
                           "grid.on_focus = function(self, on)\n"
                           "  if on then console.print('focus the grid') end\n"
                           "end\n"
                           "local cells = {}\n"
                           "for i = 1, 3 do\n"
                           "  local cell = ui.grid_entry(140, 90)\n"
                           "  cell.background = 15\n"
                           "  cell.on_focus = tag('cell' .. i)\n"
                           "  grid:add(cell)\n"
                           "  cells[i] = cell\n"
                           "end\n"
                           "local left = ui.button(90, 50)\n"
                           "left:place(20, 500)\n"
                           "left.on_focus = tag('left')\n"
                           "local right = ui.button(90, 50)\n"
                           "right:place(140, 500)\n"
                           "right.on_focus = tag('right')\n"
                           "ui.focus(cells[1])\n"
                           "ui.present('gc16')\n"
                           "console.print('ring up')\n"
                           "while true do sys.delay(20):await() end\n"));

  pump_captured(40, g_out, sizeof(g_out), "run ring");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "ring up"), g_out);
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), g_out);

  // **Five presses forward**: the three cells, the two buttons of the foot and back to the first cell.
  capture_begin();
  for (int i = 0; i < 5; ++i) {
    emu::press_key(2, true);
    pump_turns(20);
    emu::press_key(2, false);
    pump_turns(20);
  }
  capture_end(g_out, sizeof(g_out));
  const char* second = strstr(g_out, "focus cell2");
  const char* third = strstr(g_out, "focus cell3");
  const char* first_button = strstr(g_out, "focus left");
  const char* second_button = strstr(g_out, "focus right");
  const char* wrapped = (second_button != nullptr) ? strstr(second_button, "focus cell1") : nullptr;
  TEST_ASSERT_NOT_NULL_MESSAGE(second, g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(third, g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(first_button, g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(second_button, g_out);
  TEST_ASSERT_TRUE_MESSAGE(second < third, g_out);
  TEST_ASSERT_TRUE_MESSAGE(third < first_button, g_out);
  TEST_ASSERT_TRUE_MESSAGE(first_button < second_button, g_out);
  // **And the ring comes back to the first control of the page**: the last one goes on to the first.
  TEST_ASSERT_NOT_NULL_MESSAGE(wrapped, g_out);
  // **The grid is never a stop**: a ring on the whole box of the grid is a ring nobody sees.
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "focus the grid"), g_out);

  // **And the other way round**: from the first control the key before it is the last of the page.
  capture_begin();
  emu::press_key(1, true);
  pump_turns(20);
  emu::press_key(1, false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "focus right"), g_out);

  close_any_app();
  storage->remove("/apps/ring/app.lua");
  storage->remove("/apps/ring/manifest.json");
  storage->remove("/apps/ring");

  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **The pages of a grid are the control's, and the dial turns them** (0.157.0): a grid keeps the page of the cell its
// `index` says and paints `N / M` at its foot when there is more than one, and a cell that takes the ring says which one
// it is in its `on_focus`. With a box that holds two cells of five, walking with the keys has to reach the third and the
// fifth —the second and the third page— and the page has to be laid out again with them on the glass. **Before this the
// walk only saw the cells of the page** and could never leave the first one, which is why a launcher of more apps than
// fit had no way of reaching the rest.
void test_the_pages_of_a_grid_follow_the_dial(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "pages",
                           "{\"title\":\"Pages\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           // Two cells a page of a box of three hundred by two hundred, and five cells.
                           "local grid = ui.icon_grid(300, 200)\n"
                           "grid:place(20, 20)\n"
                           "grid.columns = 2\n"
                           "local cells = {}\n"
                           "for i = 1, 5 do\n"
                           "  local cell = ui.grid_entry(140, 90)\n"
                           "  cell.background = 15\n"
                           "  cell.on_focus = function(self, on)\n"
                           "    if not on then return end\n"
                           "    grid.index = i\n"
                           "    grid:invalidate()\n"
                           "    console.print('focus cell' .. i)\n"
                           "  end\n"
                           "  grid:add(cell)\n"
                           "  cells[i] = cell\n"
                           "end\n"
                           "ui.focus(cells[1])\n"
                           "ui.present('gc16')\n"
                           "console.print('grid up')\n"
                           "while true do sys.delay(20):await() end\n"));

  pump_captured(40, g_out, sizeof(g_out), "run pages");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "grid up"), g_out);
  // **Four presses forward**: the second cell, and then the cells of the pages behind the first.
  capture_begin();
  for (int i = 0; i < 4; ++i) {
    emu::press_key(2, true);
    pump_turns(20);
    emu::press_key(2, false);
    pump_turns(20);
  }
  capture_end(g_out, sizeof(g_out));
  const char* second = strstr(g_out, "focus cell2");
  const char* third = strstr(g_out, "focus cell3");
  const char* fourth = strstr(g_out, "focus cell4");
  const char* fifth = strstr(g_out, "focus cell5");
  TEST_ASSERT_NOT_NULL_MESSAGE(second, g_out);
  // **The third cell is on the second page** and the fifth on the third: reaching them is what the pages of the grid are
  // for, and the footer of the control is the paginator that says where the person is.
  TEST_ASSERT_NOT_NULL_MESSAGE(third, g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(fourth, g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(fifth, g_out);
  TEST_ASSERT_TRUE_MESSAGE(second < third, g_out);
  TEST_ASSERT_TRUE_MESSAGE(third < fourth, g_out);
  TEST_ASSERT_TRUE_MESSAGE(fourth < fifth, g_out);
  // And the page of the grid was painted again for each of them: a cell of another page is placed on the glass only when
  // the control lays itself out again.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "paint 20,20 300x200"), g_out);
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), g_out);

  close_any_app();
  storage->remove("/apps/pages/app.lua");
  storage->remove("/apps/pages/manifest.json");
  storage->remove("/apps/pages");

  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **The cursor of a grid is written by the system and not by the app** (0.179.4): the page of a grid follows its `index`,
// so every launcher kept that number in the `on_focus` of every cell —one line per cell, and the app that forgets it has a
// page that never turns—. The ring is the system's and the focus arrives in one place, so the cursor is written there and
// an app that writes nothing pages like any other. This grid has no `grid.index = i` in its cells and its page turns all
// the same (docs/30, docs/90).
void test_the_cursor_of_a_grid_follows_the_ring_without_the_app(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "cursor",
                           "{\"title\":\"Cursor\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           // Two cells a page of a box of three hundred by two hundred, and five cells.
                           "local grid = ui.icon_grid(300, 200)\n"
                           "grid:place(20, 20)\n"
                           "grid.columns = 2\n"
                           "grid.index = 1\n"
                           "local cells = {}\n"
                           "for i = 1, 5 do\n"
                           "  local cell = ui.grid_entry(140, 90)\n"
                           "  cell.background = 15\n"
                           "  cell.on_focus = function(self, on)\n"
                           "    if not on then return end\n"
                           "    console.print('cell ' .. i .. ' cursor ' .. grid.index)\n"
                           "  end\n"
                           "  grid:add(cell)\n"
                           "  cells[i] = cell\n"
                           "end\n"
                           "ui.focus(cells[1])\n"
                           "ui.present('gc16')\n"
                           "console.print('grid up')\n"
                           "while true do sys.delay(20):await() end\n"));

  pump_captured(40, g_out, sizeof(g_out), "run cursor");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "grid up"), g_out);
  // **Three steps**: the second cell of the page and then the two cells of the page behind it, whose cursor is the number
  // the system wrote before the ring was painted.
  capture_begin();
  for (int i = 0; i < 3; ++i) {
    emu::press_key(2, true);
    pump_turns(20);
    emu::press_key(2, false);
    pump_turns(20);
  }
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "cell 2 cursor 2"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "cell 3 cursor 3"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "cell 4 cursor 4"), g_out);
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), g_out);

  close_any_app();
  storage->remove("/apps/cursor/app.lua");
  storage->remove("/apps/cursor/manifest.json");
  storage->remove("/apps/cursor");

  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **A finger that walks sideways over a grid turns its page** (0.175.0, the owner: *«usa el paginador Dots i el gest swipe
// left/right per canviar de pàgina»*). The cells are the app's controls and the page is the grid's, and the system joins
// them the way a person does: the cell's father is the grid, the grid says which cell opens the page the finger asked for,
// and the cursor goes there —because the page of a grid follows the cursor— so the ring lands on the page a person is
// looking at and the page comes with it. **A gesture is not a press**: the cell it ends over is not picked.
void test_a_finger_that_walks_sideways_over_a_grid_turns_the_page(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  // **The glass and the finger**: a board with a panel is a board whose kernel reads it, and the model only hands the
  // panel over when the run says it has one (docs/85).
  opts.touch = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  // Two cells a page of eight, so the first page is the cells one and two and the second begins with the third.
  TEST_ASSERT_TRUE(add_app(storage, "swipe",
                           "{\"title\":\"Swipe\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local grid = ui.icon_grid(300, 200)\n"
                           "grid:place(20, 20)\n"
                           "grid.columns = 2\n"
                           "local cells = {}\n"
                           "for i = 1, 8 do\n"
                           "  local cell = ui.grid_entry(140, 90)\n"
                           "  cell.background = 15\n"
                           "  cell.on_focus = function(self, on)\n"
                           "    if not on then return end\n"
                           "    grid.index = i\n"
                           "    grid:invalidate()\n"
                           "    console.print('focus cell' .. i)\n"
                           "  end\n"
                           "  cell.on_click = function() console.print('pick cell' .. i) end\n"
                           "  grid:add(cell)\n"
                           "  cells[i] = cell\n"
                           "end\n"
                           "ui.focus(cells[1])\n"
                           "ui.present('gc16')\n"
                           "console.print('grid up')\n"
                           "while true do sys.delay(20):await() end\n"));

  pump_captured(40, g_out, sizeof(g_out), "run swipe");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "grid up"), g_out);
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), g_out);

  emu::Gt911Panel* panel = emu::touch_panel();
  TEST_ASSERT_NOT_NULL(panel);
  const kernel::ui::Canvas& view = kernel::ui::canvas();
  // The first cell of the grid is at 20,20 of the view and is a hundred and forty wide: the finger lands on it and walks a
  // hundred pixels to the left, which is more than the slop of a page and still on the glass.
  const uint16_t start_x = static_cast<uint16_t>(view.x + 160);
  const uint16_t start_y = static_cast<uint16_t>(view.y + 20 + 40);
  capture_begin();
  TEST_ASSERT_TRUE(panel->finger_down(0, start_x, start_y));
  pump_turns(20);
  TEST_ASSERT_TRUE(panel->finger_move(0, static_cast<uint16_t>(start_x - 100), start_y));
  pump_turns(20);
  TEST_ASSERT_TRUE(panel->finger_up(0));
  pump_turns(40);
  capture_end(g_out, sizeof(g_out));
  // **The page turned and the third cell has the ring**, which is the first of the second page: the app heard the focus of
  // a cell it had never walked to and the control was laid out again for it.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "focus cell3"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "paint 20,20 300x200"), g_out);
  // **And the gesture picked nothing**: the release after a swipe is not the click of the cell the finger stopped over.
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "pick cell"), g_out);

  // **The same finger the other way brings the first page back**: from the cell the ring is on now, to the right.
  capture_begin();
  TEST_ASSERT_TRUE(panel->finger_down(0, start_x, start_y));
  pump_turns(20);
  TEST_ASSERT_TRUE(panel->finger_move(0, static_cast<uint16_t>(start_x + 60), start_y));
  pump_turns(20);
  TEST_ASSERT_TRUE(panel->finger_up(0));
  pump_turns(40);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "focus cell1"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "pick cell"), g_out);

  close_any_app();
  storage->remove("/apps/swipe/app.lua");
  storage->remove("/apps/swipe/manifest.json");
  storage->remove("/apps/swipe");

  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **A dial step past the last row of a page turns it** (0.179.4, the owner: *«si el focus sobrepassa l'últim o el primer
// item visible per canviar de pàgina»*). A list of entries places the rows of the other pages out of its frame, where the
// walk does not reach them: without the rule the down key on the last visible row left the list and the rows behind the
// page could only be reached with a finger. The page is the control's —the footer of the list says which one it is— and
// the app hears `on_page` as it does when the arrows of a list of data turn it (docs/30, docs/90).
void test_a_dial_step_past_the_last_row_of_a_page_turns_it(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  // Two rows a page of a list of three hundred by two hundred with a footer of forty-one: rows one and two are the first
  // page and rows three and four the second (docs/30).
  TEST_ASSERT_TRUE(add_app(storage, "rows",
                           "{\"title\":\"Rows\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local l = ui.list(300, 200)\n"
                           "l:place(20, 20)\n"
                           "l.row_height = 56\n"
                           "local rows = {}\n"
                           "for i = 1, 5 do\n"
                           "  local row = ui.list_entry(300, 56)\n"
                           "  row.background = 15\n"
                           "  row.on_focus = function(self, on)\n"
                           "    if not on then return end\n"
                           "    console.print('focus row ' .. i)\n"
                           "  end\n"
                           "  l:add(row)\n"
                           "  rows[i] = row\n"
                           "end\n"
                           "l.on_page = function(self, page) console.print('page ' .. page) end\n"
                           "ui.focus(rows[1])\n"
                           "ui.present('gc16')\n"
                           "console.print('rows up')\n"
                           "while true do sys.delay(20):await() end\n"));

  pump_captured(40, g_out, sizeof(g_out), "run rows");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "rows up"), g_out);
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), g_out);
  // **Two steps**: the first walks to the second row of the page and changes nothing, the second passes the last visible
  // row and is the page.
  capture_begin();
  for (int i = 0; i < 2; ++i) {
    emu::press_key(2, true);
    pump_turns(20);
    emu::press_key(2, false);
    pump_turns(20);
  }
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "focus row 2"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "page 2"), g_out);
  // **And the ring is on the first row of the page the dial walked into**, not left behind on a row that is now out of
  // the frame (docs/30, docs/90).
  const char* turn = strstr(g_out, "page 2");
  TEST_ASSERT_NOT_NULL_MESSAGE(turn, g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(turn, "focus row 3"), g_out);

  close_any_app();
  storage->remove("/apps/rows/app.lua");
  storage->remove("/apps/rows/manifest.json");
  storage->remove("/apps/rows");
}

// **A finger that walks sideways over a list turns its page** (0.179.4): the same gesture a grid takes, said for the rows,
// so an app does not read `touch.gesture()` to turn a control's page —which is what `about` did until this version—. The
// ring goes to the first row of the page the finger asked for and the gesture picks nothing (docs/30, docs/90).
void test_a_finger_that_walks_sideways_over_a_list_turns_the_page(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  opts.touch = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "rowswipe",
                           "{\"title\":\"Row swipe\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local l = ui.list(300, 200)\n"
                           "l:place(20, 20)\n"
                           "l.row_height = 56\n"
                           "local rows = {}\n"
                           "for i = 1, 5 do\n"
                           "  local row = ui.list_entry(300, 56)\n"
                           "  row.background = 15\n"
                           "  row.on_focus = function(self, on)\n"
                           "    if not on then return end\n"
                           "    console.print('focus row ' .. i)\n"
                           "  end\n"
                           "  row.on_click = function() console.print('pick row ' .. i) end\n"
                           "  l:add(row)\n"
                           "  rows[i] = row\n"
                           "end\n"
                           "l.on_page = function(self, page) console.print('page ' .. page) end\n"
                           "ui.focus(rows[1])\n"
                           "ui.present('gc16')\n"
                           "console.print('rows up')\n"
                           "while true do sys.delay(20):await() end\n"));

  pump_captured(40, g_out, sizeof(g_out), "run rowswipe");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "rows up"), g_out);
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), g_out);

  emu::Gt911Panel* panel = emu::touch_panel();
  TEST_ASSERT_NOT_NULL(panel);
  const kernel::ui::Canvas& view = kernel::ui::canvas();
  // The finger lands on the first row —twenty pixels down and half the width across— and walks a hundred pixels to the
  // left, which is more than the slop of a page and still on the glass.
  const uint16_t start_x = static_cast<uint16_t>(view.x + 20 + 150);
  const uint16_t start_y = static_cast<uint16_t>(view.y + 20 + 28);
  capture_begin();
  TEST_ASSERT_TRUE(panel->finger_down(0, start_x, start_y));
  pump_turns(20);
  TEST_ASSERT_TRUE(panel->finger_move(0, static_cast<uint16_t>(start_x - 100), start_y));
  pump_turns(20);
  TEST_ASSERT_TRUE(panel->finger_up(0));
  pump_turns(40);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "page 2"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "focus row 3"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "pick row"), g_out);

  // **The same finger the other way brings the first page back**, with the ring on its first row.
  capture_begin();
  TEST_ASSERT_TRUE(panel->finger_down(0, start_x, start_y));
  pump_turns(20);
  TEST_ASSERT_TRUE(panel->finger_move(0, static_cast<uint16_t>(start_x + 60), start_y));
  pump_turns(20);
  TEST_ASSERT_TRUE(panel->finger_up(0));
  pump_turns(40);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "page 1"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "focus row 1"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "pick row"), g_out);

  close_any_app();
  storage->remove("/apps/rowswipe/app.lua");
  storage->remove("/apps/rowswipe/manifest.json");
  storage->remove("/apps/rowswipe");

  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **A canvas inside a cell of a grid is still there after the page is painted** (0.154.1): a launcher draws the badge of
// an app in a canvas of its own and puts that canvas in a cell, which is what `canvas:image` is for. The page is painted
// twice —the first time is the page, the second is the one a manifest arriving asks for— and the pictures have to be
// there both times: one that is quietly gone shows a letter where the badge of an app belongs.
void test_a_canvas_inside_a_cell_of_a_grid_survives_the_page(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "cells",
                           "{\"title\":\"Cells\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local grid = ui.icon_grid(360, 260)\n"
                           "grid:place(20, 20)\n"
                           "grid.columns = 3\n"
                           "local pics = {}\n"
                           "for i = 1, 6 do\n"
                           "  local cell = ui.grid_entry(118, 126)\n"
                           "  cell.background = 15\n"
                           "  local pic = ui.canvas(72, 72)\n"
                           "  pic:place(4, 4)\n"
                           "  pic:clear(15)\n"
                           "  cell:add(pic)\n"
                           "  grid:add(cell)\n"
                           "  pics[i] = pic\n"
                           "end\n"
                           "ui.present('gc16')\n"
                           "console.print('cells up')\n"
                           "local drawn, why = pics[3]:image(0, 0, '/apps/cells/icon.png', 72, 72)\n"
                           "console.print('cell 3 picture ' .. tostring(drawn) .. ' ' .. tostring(why and why.message or ''))\n"
                           "ui.present()\n"
                           "console.print('cells up two')\n"
                           "for i = 1, 6 do\n"
                           "  local caps = pics[i]:caps()\n"
                           "  console.print('cell ' .. i .. ' alive ' .. tostring(caps ~= nil))\n"
                           "end\n"
                           "local later, why2 = pics[4]:image(0, 0, '/apps/cells/icon.png', 72, 72)\n"
                           "console.print('cell 4 picture ' .. tostring(later) .. ' ' .. tostring(why2 and why2.message or ''))\n"
                           "while true do sys.delay(20):await() end\n"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/home_eink/res/icon.png", "/apps/cells/icon.png"));

  pump_captured(200, g_out, sizeof(g_out), "run cells");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "cells up two"), g_out);
  // **Six of them alive after the second page**, and a picture in the third and in the fourth.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "cell 1 alive true"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "cell 6 alive true"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "cell 3 picture true"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "cell 4 picture true"), g_out);

  close_any_app();
  storage->remove("/apps/cells/icon.png");
  storage->remove("/apps/cells/app.lua");
  storage->remove("/apps/cells/manifest.json");
  storage->remove("/apps/cells");

  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **`app.run` answers with the reason** (0.153.0): a launcher that presses a picture and gets nothing has to be able to
// say why, and the three ways it can be wrong are the system's to say —the app that asks for itself, an id that is not
// on the disk, and an app that names another board—. Nothing is started here: the page stays where it was.
void test_app_run_says_why_it_cannot_start_an_app(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "nosy",
                           "{\"title\":\"Nosy\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local ok, why = app.run('nosy')\n"
                           "console.print('self ' .. tostring(ok) .. ' ' .. tostring(why and why.code) .. ' ' ..\n"
                           "                tostring(why and why.message))\n"
                           "local ok2, why2 = app.run('nothing')\n"
                           "console.print('none ' .. tostring(ok2) .. ' ' .. tostring(why2 and why2.message))\n"
                           "local ok3, why3 = app.run('elsewhere')\n"
                           "console.print('board ' .. tostring(ok3) .. ' ' .. tostring(why3 and why3.message))\n"
                           "while true do sys.delay(20):await() end\n"));
  TEST_ASSERT_TRUE(add_app(storage, "elsewhere",
                           "{\"title\":\"Elsewhere\",\"version\":\"0.1.0\",\"entry\":\"app.lua\","
                           "\"boards\":[\"m5stack-atom-matrix\"]}",
                           "console.print('elsewhere here')\n"
                           "while true do sys.delay(20):await() end\n"));

  pump_captured(120, g_out, sizeof(g_out), "run nosy");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "self nil E_INVAL that is the app that is asking"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "none nil there is no such app on this disk"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(
      strstr(g_out, "board nil elsewhere runs on another board and this one is m5stack-paper"), g_out);
  // Nothing was started by any of the three, and the app that asked is still the one in front.
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "elsewhere here"), g_out);
  TEST_ASSERT_EQUAL_STRING("nosy", kernel::shell::app_id());

  kernel::shell::close_app();
  pump_captured(10, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  storage->remove("/apps/nosy/app.lua");
  storage->remove("/apps/nosy/manifest.json");
  storage->remove("/apps/nosy");
  storage->remove("/apps/elsewhere/app.lua");
  storage->remove("/apps/elsewhere/manifest.json");
  storage->remove("/apps/elsewhere");

  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// Every app of the repository says which boards it runs on, and none of them names
// the emulator: the emulator is not a board, it is told which one it is, so writing
// `emulator` in a list is a name no board would ever answer (docs/40, docs/85).
// A manifest that forgot the list would still be caught here, because no list is not
// "every board" (docs/40).
//
// **And the apps the board carries have to be among the ones audited** (0.152.6): the guard used to be a floor on
// the count —seventeen—, which said nothing about *which* app was missing and was a number of a set that is being
// made again. The set of this repository is the twenty-four apps of the paper project (`OLD/PaperOS/data/app`:
// `About`, `Agenda`, `Banner`, `Battery`, `Calculator`, `Clock`, `EPUB`, `Editor`, `FRAM`, `Files`, `ImageViewer`,
// `Jellyfin`, `Life`, `Meteo`, `News`, `Pacman`, `Paint`, `Password`, `SerialFUSE`, `Snake`, `Tamagotchi`, `Tetris`,
// `Timeclock` and `2048`), ported one by one, and `reader` is the port of `EPUB`. The ids below are the ones
// `platformio.ini` carries on the Paper (`custom_apps`): **an id goes out of this list in the same commit as the app**
// (docs/40, docs/45), which is what happened to the instrument app of the emulator when the owner deleted it.
void test_every_app_of_the_repository_declares_its_boards(void) {
  static const char* const kParents[] = {"", "../", "../../", "../../../"};
  static const char* const kMustShip[] = {"about", "calculator", "editor", "home_eink", "reader", "timeclock"};
  static char text[1024];
  bool ship_seen[sizeof(kMustShip) / sizeof(kMustShip[0])] = {false};
  const uint32_t kMust = sizeof(kMustShip) / sizeof(kMustShip[0]);
  bool found_the_folder = false;
  uint32_t seen = 0;
  for (size_t p = 0; p < sizeof(kParents) / sizeof(kParents[0]); ++p) {
    char dir_path[256];
    snprintf(dir_path, sizeof(dir_path), "%sapps", kParents[p]);
    DIR* dir = opendir(dir_path);
    if (dir == nullptr) continue;
    found_the_folder = true;
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
      TEST_ASSERT_TRUE_MESSAGE(lunok::json::get_strings(text, got, "boards", boards,
                                                        kernel::apps::Entry::kMaxBoards, count) ==
                                   lunok::json::Strings::kOk,
                               item->d_name);
      TEST_ASSERT_TRUE_MESSAGE(count > 0, item->d_name);
      for (uint32_t i = 0; i < count; ++i) {
        TEST_ASSERT_TRUE_MESSAGE(strcmp(boards[i], "emulator") != 0, item->d_name);
      }
      ++seen;
      for (uint32_t m = 0; m < kMust; ++m) {
        if (strcmp(item->d_name, kMustShip[m]) == 0) ship_seen[m] = true;
      }
    }
    closedir(dir);
    break;
  }
  // A folder that was not found would pass every check above by not running any of them, and so would a folder
  // that is there but empty.
  TEST_ASSERT_TRUE_MESSAGE(found_the_folder, "the apps of the repository were not found");
  TEST_ASSERT_TRUE_MESSAGE(seen > 0, "the folder of the apps of the repository is empty");
  for (uint32_t m = 0; m < kMust; ++m) {
    TEST_ASSERT_TRUE_MESSAGE(ship_seen[m], kMustShip[m]);
  }
}

// **Every app of this repository carries a picture** (0.170.1, the owner: *«totes les apps han de tenir icona»*): the
// manifest says it and the file is beside it, because a launcher that has to write the first letter of a name in a box
// is a launcher saying that the app has no picture — and the rule is a rule of this project, so the suite is where it
// is checked and not a document nobody opens (docs/40, docs/85).
void test_every_app_of_the_repository_carries_an_icon(void) {
  static const char* const kParents[] = {"", "../", "../../", "../../../"};
  static char text[1024];
  bool found_the_folder = false;
  uint32_t seen = 0;
  for (size_t p = 0; p < sizeof(kParents) / sizeof(kParents[0]); ++p) {
    char dir_path[256];
    snprintf(dir_path, sizeof(dir_path), "%sapps", kParents[p]);
    DIR* dir = opendir(dir_path);
    if (dir == nullptr) continue;
    found_the_folder = true;
    struct dirent* item = nullptr;
    while ((item = readdir(dir)) != nullptr) {
      if (item->d_name[0] == '.') continue;
      char manifest[560];
      snprintf(manifest, sizeof(manifest), "%s/%s/manifest.json", dir_path, item->d_name);
      FILE* file = fopen(manifest, "rb");
      if (file == nullptr) continue;
      const size_t got = fread(text, 1, sizeof(text) - 1, file);
      fclose(file);
      text[got] = '\0';

      char icon[64] = {0};
      TEST_ASSERT_TRUE_MESSAGE(lunok::json::get_string(text, got, "icon", icon, sizeof(icon)), item->d_name);
      // **Inside its own folder and with a picture in it**: `..` and a leading `/` are what the kernel refuses, and a
      // name with no file behind it is an app that says it has a picture and does not (docs/40).
      TEST_ASSERT_TRUE_MESSAGE(icon[0] != '\0' && icon[0] != '/', item->d_name);
      TEST_ASSERT_NULL_MESSAGE(strstr(icon, ".."), item->d_name);

      char picture[640];
      snprintf(picture, sizeof(picture), "%s/%s/%s", dir_path, item->d_name, icon);
      FILE* art = fopen(picture, "rb");
      TEST_ASSERT_NOT_NULL_MESSAGE(art, picture);
      unsigned char head[8] = {0};
      const size_t head_got = fread(head, 1, sizeof(head), art);
      fseek(art, 0, SEEK_END);
      const long size = ftell(art);
      fclose(art);
      // **A PNG and not an empty file**: eight bytes of signature and something after them, which is the format the
      // recipe of `tools/make_icon.py` writes for every app (docs/40).
      TEST_ASSERT_EQUAL_INT_MESSAGE(8, static_cast<int>(head_got), picture);
      static const unsigned char kPng[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
      TEST_ASSERT_EQUAL_MEMORY_MESSAGE(kPng, head, sizeof(kPng), picture);
      TEST_ASSERT_GREATER_THAN_MESSAGE(200, size, picture);
      ++seen;
    }
    closedir(dir);
    break;
  }
  TEST_ASSERT_TRUE_MESSAGE(found_the_folder, "the apps of the repository were not found");
  TEST_ASSERT_TRUE_MESSAGE(seen > 0, "the folder of the apps of the repository is empty");
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

  // The next test gets the default emulator back: the display and the panel were this test's.
  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  opts.touch = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **A swipe on a box of several lines moves the window, and the gesture belongs to the app that registered it**
// (0.88.0): the system does the arithmetic —how many rows the finger has travelled— and writes the `top` of the box, the
// page presents itself and the app hears it at `on_swipe`. **And the gesture does not outlive the page**: an app that
// ends with the finger down leaves no half gesture to the next one, because the press of the second is a new press
// (docs/30, docs/90).
void test_a_swipe_on_a_box_of_lines_moves_the_window_and_ends_with_its_page(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  opts.touch = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());
  TEST_ASSERT_NOT_NULL(hal::platform().touch());
  TEST_ASSERT_TRUE(kernel::touch::boot(hal::platform()));
  emu::Gt911Panel* panel = emu::touch_panel();
  TEST_ASSERT_NOT_NULL(panel);

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  const char* kApp =
      "local lines = {}\n"
      "for i = 1, 40 do lines[i] = 'row ' .. i end\n"
      "local box = ui.textbox_multiline(300, 200)\n"
      "box:place(10, 10)\n"
      "box.scale = 2\n"
      "box.text = table.concat(lines, '\\n')\n"
      "box.cursor = 0\n"
      // The page is a sheet to scroll and not a field to type into: since 0.91.0 a finger on a box that takes keys
      // brings the keyboard, so a box that does not want one says it (docs/30, docs/90).
      "box.keyboard = false\n"
      "console.print('lines ' .. box:lines() .. ' rows ' .. box:rows())\n"
      "box.on_swipe = function(self, rows)\n"
      "  console.print('swipe ' .. rows .. ' top ' .. tostring(self.top))\n"
      "end\n"
      "ui.present()\n"
      "while true do sys.delay(20):await() end\n";
  TEST_ASSERT_TRUE(add_app(storage, "swiper",
                           "{\"title\":\"Swiper\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           kApp));

  pump_captured(20, g_out, sizeof(g_out), "run swiper");
  // The box shows thirteen rows of the fourteen of a letter and the text is forty, which is a bar and not no bar.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "lines 40 rows 13"), g_out);

  // **El dit baixa, camina cap amunt i deixa anar**: sixty pixels are four rows of fourteen, and the window of the box
  // goes from the first row to the fifth. The panel counts on the glass, which is the view shifted by its place
  // (docs/30, docs/50, docs/90).
  const kernel::ui::Canvas& view = kernel::ui::canvas();
  capture_begin();
  TEST_ASSERT_TRUE(panel->finger_down(0, static_cast<uint16_t>(view.x + 100), static_cast<uint16_t>(view.y + 160)));
  pump_turns(10);
  for (int step = 0; step < 4; ++step) {
    TEST_ASSERT_TRUE(panel->finger_move(0, static_cast<uint16_t>(view.x + 100),
                                        static_cast<uint16_t>(view.y + 160 - 15 * (step + 1))));
    pump_turns(10);
  }
  TEST_ASSERT_TRUE(panel->finger_up(0));
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "swipe "), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "top "), g_out);

  // **The gesture is the page's and goes with it**: the finger goes down again and the app is closed with the finger
  // still on the glass. The app that comes next starts with no gesture at all, so a still finger is not a swipe there
  // (docs/30, docs/90).
  TEST_ASSERT_TRUE(panel->finger_down(0, static_cast<uint16_t>(view.x + 100), static_cast<uint16_t>(view.y + 160)));
  pump_turns(10);
  kernel::shell::close_app();
  pump_turns(10);
  TEST_ASSERT_FALSE(kernel::app_running());
  pump_captured(20, g_out, sizeof(g_out), "run swiper");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "lines 40 rows 13"), g_out);
  capture_begin();
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "swipe "), g_out);
  TEST_ASSERT_TRUE(panel->finger_up(0));
  pump_turns(20);

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());

  storage->remove("/apps/swiper/app.lua");
  storage->remove("/apps/swiper/manifest.json");
  storage->remove("/apps/swiper");

  // The next test gets the default emulator back: the display and the panel were this test's.
  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  opts.touch = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **The document of the editor scrolls with a finger** (0.113.0): the box of several lines is where the page of the
// editor lives, and a finger that walks it upwards has to move the window and say the row a person is on. This is the
// box the owner could not scroll on the board, and the test drives the app of the disk and not a stand-in
// (docs/30, docs/90, docs/92 §28).
void test_the_document_of_the_editor_scrolls_with_a_finger(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  opts.touch = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());
  TEST_ASSERT_NOT_NULL(hal::platform().touch());
  TEST_ASSERT_TRUE(kernel::touch::boot(hal::platform()));
  emu::Gt911Panel* panel = emu::touch_panel();
  TEST_ASSERT_NOT_NULL(panel);

  // **The editor of the disk, copied into the disk of the emulator**: it is a page of `apps/` and not something this test
  // writes, so it comes from the repository with its sample (docs/85).
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  storage->mkdir("/apps");
  storage->mkdir("/apps/editor");
  storage->mkdir("/apps/editor/res");
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/editor/manifest.json", "/apps/editor/manifest.json"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/editor/editor.lua", "/apps/editor/editor.lua"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/editor/res/notes.txt", "/apps/editor/res/notes.txt"));

  // The editor opens its own sample page, which is long enough to scroll.
  pump_captured(40, g_out, sizeof(g_out), "run editor");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "the sample page is"), g_out);
  const kernel::ui::Canvas& view = kernel::ui::canvas();

  // **A finger that walks up the box**: sixty pixels upwards, which is more than two rows of the document at the size
  // the editor draws it.
  capture_begin();
  TEST_ASSERT_TRUE(panel->finger_down(0, static_cast<uint16_t>(view.x + 270),
                                      static_cast<uint16_t>(view.y + 400)));
  pump_turns(10);
  for (int step = 0; step < 4; ++step) {
    TEST_ASSERT_TRUE(panel->finger_move(0, static_cast<uint16_t>(view.x + 270),
                                        static_cast<uint16_t>(view.y + 400 - 15 * (step + 1))));
    pump_turns(10);
  }
  TEST_ASSERT_TRUE(panel->finger_up(0));
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  // **The window moved and the app says the row**, which is the number the line under the page shows.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "editor: row "), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "editor: row 1 of"), g_out);

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  storage->remove("/apps/editor/editor.lua");
  storage->remove("/apps/editor/manifest.json");
  storage->remove("/apps/editor/res/notes.txt");

  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  opts.touch = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **The page of pictures walks its nine families with the controls of the system** (0.118.0): the grid of cells is the
// menu —the dial walks the cells and the centre picks one—, the box under the picture is read and not written, and the
// strip of dots says which of the nine is on the glass. The decoders behind the page are the ones that answered the
// owner's board with `***ERROR*** A stack overflow in task loopTask has been detected` after three pictures: a GIF kept
// 16 656 bytes of table, string and row in locals against the 7 320 the app task prints free, and it keeps 256 now
// (docs/75, docs/85, docs/92 §33).

// How many times a text appears in what an app has printed: a gesture that arrives is a line, and a gesture that does
// not arrive is the same line not there (0.113.0).
int times_in(const char* hay, const char* needle) {
  int times = 0;
  const size_t step = strlen(needle);
  for (const char* at = strstr(hay, needle); at != nullptr; at = strstr(at + step, needle)) ++times;
  return times;
}

// **The rows of a box go to a box that has the dial** (0.113.0): the app hands the focus to another box from the hook
// the first swipe wakes, and the finger that keeps walking the first one must not move anything after that. The owner's
// complaint was a document that did not answer the finger, and the other half of that promise is a gesture that reaches
// the control the person is on and no other (docs/30, docs/92 §28).
void test_a_swipe_only_moves_the_box_that_has_the_dial(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  opts.touch = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());
  TEST_ASSERT_NOT_NULL(hal::platform().touch());
  TEST_ASSERT_TRUE(kernel::touch::boot(hal::platform()));
  emu::Gt911Panel* panel = emu::touch_panel();
  TEST_ASSERT_NOT_NULL(panel);

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  const char* kApp =
      "local lines = {}\n"
      "for i = 1, 40 do lines[i] = 'row ' .. i end\n"
      "local box = ui.textbox_multiline(300, 200)\n"
      "box:place(10, 10)\n"
      "box.scale = 2\n"
      "box.text = table.concat(lines, '\\n')\n"
      "box.cursor = 0\n"
      "box.keyboard = false\n"
      "local button = ui.button(120, 40)\n"
      "button:place(10, 260)\n"
      "box.on_swipe = function(self, rows)\n"
      "  console.print('swipe ' .. rows .. ' box focused ' .. tostring(self.focused))\n"
      "  ui.focus(button)\n"
      "  console.print('handed over ' .. tostring(button.focused) .. ' box ' .. tostring(box.focused))\n"
      "end\n"
      "ui.present()\n"
      "while true do sys.delay(20):await() end\n";
  TEST_ASSERT_TRUE(add_app(storage, "handover",
                           "{\"title\":\"Handover\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           kApp));

  pump_captured(20, g_out, sizeof(g_out), "run handover");
  const kernel::ui::Canvas& view = kernel::ui::canvas();

  // **The finger walks the first box and the hook gives the dial away**: one row of the first swipe, and the three
  // moves after it are on a box that is not the one the dial is on.
  capture_begin();
  TEST_ASSERT_TRUE(panel->finger_down(0, static_cast<uint16_t>(view.x + 100),
                                      static_cast<uint16_t>(view.y + 160)));
  pump_turns(10);
  for (int step = 0; step < 4; ++step) {
    TEST_ASSERT_TRUE(panel->finger_move(0, static_cast<uint16_t>(view.x + 100),
                                        static_cast<uint16_t>(view.y + 160 - 15 * (step + 1))));
    pump_turns(10);
  }
  TEST_ASSERT_TRUE(panel->finger_up(0));
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "swipe 1 box focused true"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "handed over true box false"), g_out);
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, times_in(g_out, "swipe "), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "swipe 2"), g_out);

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  storage->remove("/apps/handover/app.lua");
  storage->remove("/apps/handover/manifest.json");
  storage->remove("/apps/handover");

  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  opts.touch = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **A swipe that goes out of the box stops where it left it** (0.113.0): the finger that walks off the frame of the
// control is not walking the control any more, and the rows it had travelled are dropped with it —a window that jumps
// when the finger comes back is worse than one that stopped where it was— (docs/30, docs/92 §28).
void test_a_swipe_that_leaves_the_box_stops_where_it_left_it(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  opts.touch = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());
  TEST_ASSERT_NOT_NULL(hal::platform().touch());
  TEST_ASSERT_TRUE(kernel::touch::boot(hal::platform()));
  emu::Gt911Panel* panel = emu::touch_panel();
  TEST_ASSERT_NOT_NULL(panel);

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  const char* kApp =
      "local lines = {}\n"
      "for i = 1, 40 do lines[i] = 'row ' .. i end\n"
      "local box = ui.textbox_multiline(300, 200)\n"
      "box:place(10, 10)\n"
      "box.scale = 2\n"
      "box.text = table.concat(lines, '\\n')\n"
      "box.cursor = 0\n"
      "box.keyboard = false\n"
      "box.on_swipe = function(self, rows)\n"
      "  console.print('swipe ' .. rows .. ' top ' .. tostring(self.top))\n"
      "end\n"
      "ui.present()\n"
      "while true do sys.delay(20):await() end\n";
  TEST_ASSERT_TRUE(add_app(storage, "leaver",
                           "{\"title\":\"Leaver\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           kApp));

  pump_captured(20, g_out, sizeof(g_out), "run leaver");
  const kernel::ui::Canvas& view = kernel::ui::canvas();

  // **One row inside the box, and then four hundred pixels below it**: the box is at ten to three hundred and ten, so
  // a finger at four hundred is off the frame and the swipes of the way back are not the box's.
  capture_begin();
  TEST_ASSERT_TRUE(panel->finger_down(0, static_cast<uint16_t>(view.x + 100),
                                      static_cast<uint16_t>(view.y + 160)));
  pump_turns(10);
  TEST_ASSERT_TRUE(panel->finger_move(0, static_cast<uint16_t>(view.x + 100),
                                      static_cast<uint16_t>(view.y + 145)));
  pump_turns(10);
  TEST_ASSERT_TRUE(panel->finger_move(0, static_cast<uint16_t>(view.x + 100),
                                      static_cast<uint16_t>(view.y + 400)));
  pump_turns(10);
  TEST_ASSERT_TRUE(panel->finger_move(0, static_cast<uint16_t>(view.x + 100),
                                      static_cast<uint16_t>(view.y + 120)));
  pump_turns(10);
  TEST_ASSERT_TRUE(panel->finger_move(0, static_cast<uint16_t>(view.x + 100),
                                      static_cast<uint16_t>(view.y + 60)));
  pump_turns(10);
  TEST_ASSERT_TRUE(panel->finger_up(0));
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "swipe 1 "), g_out);
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, times_in(g_out, "swipe "), g_out);

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  storage->remove("/apps/leaver/app.lua");
  storage->remove("/apps/leaver/manifest.json");
  storage->remove("/apps/leaver");

  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  opts.touch = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **A box that is given back takes its gesture with it** (0.113.0): the app releases the control the finger is on and
// builds another one in the same place, and the finger that is still down must not move the new one —the slot it took
// is the one the gesture was walking—. This is the "app that ends with the finger on the glass" of the paper project,
// said of one control instead of a whole page (docs/30, docs/92 §28).
void test_a_box_that_is_given_back_leaves_no_swipe_to_the_one_that_follows(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  opts.touch = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());
  TEST_ASSERT_NOT_NULL(hal::platform().touch());
  TEST_ASSERT_TRUE(kernel::touch::boot(hal::platform()));
  emu::Gt911Panel* panel = emu::touch_panel();
  TEST_ASSERT_NOT_NULL(panel);

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  const char* kApp =
      "local lines = {}\n"
      "for i = 1, 40 do lines[i] = 'row ' .. i end\n"
      "local text = table.concat(lines, '\\n')\n"
      "local box = ui.textbox_multiline(300, 200)\n"
      "box:place(10, 10)\n"
      "box.scale = 2\n"
      "box.text = text\n"
      "box.cursor = 0\n"
      "box.keyboard = false\n"
      "local fresh = nil\n"
      "local heard = 0\n"
      "box.on_swipe = function(self, rows)\n"
      "  console.print('swipe ' .. rows)\n"
      "  heard = heard + 1\n"
      "  if heard == 2 then\n"
      "    box:release()\n"
      "    fresh = ui.textbox_multiline(300, 200)\n"
      "    fresh:place(10, 10)\n"
      "    fresh.scale = 2\n"
      "    fresh.text = text\n"
      "    fresh.keyboard = false\n"
      "    fresh.on_swipe = function(self, said) console.print('fresh ' .. said) end\n"
      "    console.print('given back')\n"
      "    ui.present()\n"
      "  end\n"
      "end\n"
      "ui.present()\n"
      "while true do sys.delay(20):await() end\n";
  TEST_ASSERT_TRUE(add_app(storage, "giver",
                           "{\"title\":\"Giver\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           kApp));

  pump_captured(20, g_out, sizeof(g_out), "run giver");
  const kernel::ui::Canvas& view = kernel::ui::canvas();

  // **The whole gesture completes**: two rows of the first box, which is the one that is given back, and two more moves
  // of a finger that has no control any more — it is on the glass over the box the app has just built, and that box is
  // not pressed by a contact that began on another one (0.113.0, docs/30).
  capture_begin();
  TEST_ASSERT_TRUE(panel->finger_down(0, static_cast<uint16_t>(view.x + 100),
                                      static_cast<uint16_t>(view.y + 160)));
  pump_turns(10);
  for (int step = 0; step < 4; ++step) {
    TEST_ASSERT_TRUE(panel->finger_move(0, static_cast<uint16_t>(view.x + 100),
                                        static_cast<uint16_t>(view.y + 160 - 15 * (step + 1))));
    pump_turns(10);
  }
  TEST_ASSERT_TRUE(panel->finger_up(0));
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "given back"), g_out);
  TEST_ASSERT_EQUAL_INT_MESSAGE(2, times_in(g_out, "swipe "), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "fresh "), g_out);

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  storage->remove("/apps/giver/app.lua");
  storage->remove("/apps/giver/manifest.json");
  storage->remove("/apps/giver");

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
// so every `ui.present('gc16')` was a present with no mode (found on the emulator, 3/11).
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
                            // **A small square and not the whole glass**: a `du`/`du4` region that covers seven
                            // eighths of the panel is promoted to `gc16` by the system (docs/50), which is a rule of its
                            // own and would hide the mode this case is about (0.152.4).
                            "ui.rect(20, 20, 40, 40, 0)\n"
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

  // The next test gets the default emulator back: the display was this test's.
  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **A present with no quality of its own goes up with the one the ink asks for** (0.179.5, the owner: *«Perqué totes
// refresquen amb tanta qualitat quan no fa falta? usa sempre el mode necessari adequat per els canvis del buffer que es volen
// pujar a l'eink»*). A page of words and controls is one paint of sixteen levels and no flash —`gl16`— and not the four
// blinks of `gc16` that every app of this tree was asking for; a box that changed afterwards is that box and no more. The
// page the glass had before is the system's business: it washes it between two apps, so a page does not have to burn away a
// paper that is already white (docs/30, docs/50).
void test_a_page_goes_up_with_the_quality_its_ink_asks_for(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "quiet",
                           "{\"title\":\"Quiet\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local head = ui.label(300, 40)\n"
                           "head.text = 'the first line'\n"
                           "head.scale = 4\n"
                           "head:place(20, 20)\n"
                           "for i = 1, 6 do\n"
                           "  local b = ui.button(300, 50)\n"
                           "  b.title = 'a door ' .. i\n"
                           "  b:place(20, 80 + (i - 1) * 60)\n"
                           "end\n"
                           "ui.present()\n"
                           "console.print('page up')\n"
                           "head.text = 'the line that changed'\n"
                           "ui.present()\n"
                           "console.print('box up')\n"
                           "sys.delay(5000):await()\n"));

  pump_captured(200, g_out, sizeof(g_out), "run quiet");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "page up"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "box up"), g_out);

  emu::MemorySurface* glass = emu::memory_surface();
  TEST_ASSERT_NOT_NULL(glass);
  // **The page is one paint and it is sixteen levels without a flash**: the head and the six doors are seven boxes and one
  // page, a page is one paint, and the quality is the one the ink deserves (docs/30, docs/50).
  const emu::MemorySurface::Paint* page = nullptr;
  for (uint32_t i = 0; i < emu::MemorySurface::kMaxPaints; ++i) {
    const emu::MemorySurface::Paint* paint = glass->paint_at(glass->paint_count() - 1 - i);
    if (paint != nullptr && paint->w == 300 && paint->h > 300) page = paint;
  }
  TEST_ASSERT_NOT_NULL_MESSAGE(page, "the page was painted as one box of 300 wide");
  TEST_ASSERT_EQUAL(hal::UpdateMode::kGl16, page->mode);
  TEST_ASSERT_EQUAL_UINT16(kernel::ui::canvas().y + 20, page->y);

  // **And the box that changed is one box**, a line of words with the same quality and not the page again (docs/50).
  const emu::MemorySurface::Paint* box = nullptr;
  for (uint32_t i = 0; i < emu::MemorySurface::kMaxPaints; ++i) {
    const emu::MemorySurface::Paint* paint = glass->paint_at(glass->paint_count() - 1 - i);
    if (paint != nullptr && paint->w == 300 && paint->h <= 60) {
      box = paint;
      break;
    }
  }
  TEST_ASSERT_NOT_NULL_MESSAGE(box, "the line that changed was painted, and only it");
  TEST_ASSERT_EQUAL(hal::UpdateMode::kGl16, box->mode);
  TEST_ASSERT_EQUAL_UINT16(kernel::ui::canvas().y + 20, box->y);

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  storage->remove("/apps/quiet/app.lua");
  storage->remove("/apps/quiet/manifest.json");
  storage->remove("/apps/quiet");

  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **A page of black and white is never sent to the wash that flashes four times** (0.179.15). The budget of
// thirty-two painted boxes sends a page to `gc16` —the wash that takes the ghost of the greys away— and a page whose
// ink is one bit has none of that ghost: the text of the system is rasterised one bit a pixel (`lib/ttf`, no
// antialiasing), so a line of it is level 0 and level 15 and nothing between. The case paints a line forty times and
// reads the quality of every paint: the thirty-nine after the budget used to be `gc16`, four blinks each, and they
// are `gl16` now — one blink, and only because the page asked for a wash at all (docs/30, docs/50).
void test_a_page_of_one_bit_is_never_sent_to_the_full_wash(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "oneline",
                           "{\"title\":\"One line\",\"version\":\"0.1.0\",\"entry\":\"app.lua\","
                           "\"boards\":[\"esp32\"]}",
                           "local head = ui.label(300, 40)\n"
                           "head.text = 'one'\n"
                           "head.scale = 4\n"
                           "head:place(20, 20)\n"
                           "ui.present()\n"
                           "for i = 1, 40 do\n"
                           "  head.text = 'line ' .. i\n"
                           "  ui.present()\n"
                           "end\n"
                           "console.print('forty lines')\n"
                           "sys.delay(5000):await()\n"));

  pump_captured(300, g_out, sizeof(g_out), "run oneline");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "forty lines"), g_out);

  emu::MemorySurface* glass = emu::memory_surface();
  TEST_ASSERT_NOT_NULL(glass);
  // **Forty-one paints and not one of them is the four-blink wash**: the page, and then one box a line.
  uint32_t lines = 0;
  uint32_t full = 0;
  for (uint32_t i = 0; i < emu::MemorySurface::kMaxPaints; ++i) {
    const emu::MemorySurface::Paint* paint = glass->paint_at(glass->paint_count() - 1 - i);
    if (paint == nullptr) continue;
    if (paint->mode == hal::UpdateMode::kGc16) ++full;
    if (paint->mode == hal::UpdateMode::kGl16) ++lines;
  }
  // **The last paints of the run and not all of them**: the glass of the emulator keeps the last eight, and eight is
  // more than enough —the budget of 32 is spent long before, so every paint this case can see is past it—.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, full, "a page of one bit was sent to the full wash");
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(6, lines, "the boxes went up, and all of them in gl16");

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  storage->remove("/apps/oneline/app.lua");
  storage->remove("/apps/oneline/manifest.json");
  storage->remove("/apps/oneline");

  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **The selector of the focus comes and goes with the hands** (0.179.7): it is off until a key of the board is used, the
// first finger puts it away, and the next key brings it back where the dial is. The dial itself never moves with the
// hand —`focused` is still where a person is and the app still hears it—, it is only the ring that is not painted: a page
// read with a finger has no box telling anybody which one the dial would take (docs/30, docs/90).
void test_the_selector_of_the_focus_comes_and_goes_with_the_hands(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  opts.touch = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "rings",
                           "{\"title\":\"Rings\",\"version\":\"0.1.0\",\"entry\":\"app.lua\","
                           "\"boards\":[\"esp32\"]}",
                           "local a = ui.button(200, 60)\n"
                           "a.on_focus = function(self, on) console.print('focus one ' .. tostring(on)) end\n"
                           "a.title = 'one'\n"
                           "a.background = 15\n"
                           "a.border = 0\n"
                           "a:place(20, 20)\n"
                           "local b = ui.button(200, 60)\n"
                           "b.on_focus = function(self, on) console.print('focus two ' .. tostring(on)) end\n"
                           "b.title = 'two'\n"
                           "b.background = 15\n"
                           "b.border = 0\n"
                           "b:place(20, 120)\n"
                           "ui.present('gc16')\n"
                           "console.print('rings up')\n"
                           "while true do sys.delay(20):await() end\n"));

  pump_captured(40, g_out, sizeof(g_out), "run rings");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "rings up"), g_out);
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), g_out);
  emu::MemorySurface* glass = emu::memory_surface();
  TEST_ASSERT_NOT_NULL(glass);
  // A button that has the ring fills its face with the ink of its frame and one that has not keeps its paper, so one
  // pixel inside the face says it all (docs/92). The two doors are at (20, 20) and (20, 120) of the view, which starts
  // under the bar of the system, and **the app has just opened**, which is the state the owner asked for: no ring until a
  // key of the board is used (docs/30).
  const uint16_t face_x = 40;
  const uint16_t first_y = kernel::ui::canvas().y + 26;
  const uint16_t second_y = kernel::ui::canvas().y + 126;
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(15, glass->level_at(face_x, first_y), "no ring until the board is used");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(15, glass->level_at(face_x, second_y), "and none on the other door either");

  // **A key brings it back**: the ring shows up where the dial already was —the first door— and the same key walks it on
  // to the second, so the two doors swap their ink in one paint (docs/30, docs/90).
  capture_begin();
  emu::press_key(2, true);
  pump_turns(10);
  emu::press_key(2, false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "focus one false"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "focus two true"), g_out);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(15, glass->level_at(face_x, first_y), "the ring left the first door");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, glass->level_at(face_x, second_y), "and it is on the second");

  // **A finger on the glass puts it away**, and the page is painted in the same turn: the ring is gone from the door it
  // was on even though nothing else on the page changed.
  emu::Gt911Panel* panel = emu::touch_panel();
  TEST_ASSERT_NOT_NULL(panel);
  TEST_ASSERT_TRUE(panel->finger_down(0, 460, 800));  // the empty paper under the two doors
  pump_turns(10);
  TEST_ASSERT_TRUE(panel->finger_up(0));
  pump_turns(30);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(15, glass->level_at(face_x, second_y), "the finger puts the ring away");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(15, glass->level_at(face_x, first_y), "and the page has no ring anywhere");

  // **And the dial did not move while it was not painted**: the next key walks one door on from where the ring was —the
  // page is a ring, so the second door's step is the first one—, which is only true if the dial stayed on the second. A
  // hidden ring is not a lost dial (docs/30, docs/90).
  emu::press_key(2, true);
  pump_turns(10);
  emu::press_key(2, false);
  pump_turns(20);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, glass->level_at(face_x, first_y), "the ring is back, wrapping to the first door");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(15, glass->level_at(face_x, second_y), "and the second one is paper again");

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  storage->remove("/apps/rings/app.lua");
  storage->remove("/apps/rings/manifest.json");
  storage->remove("/apps/rings");

  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  opts.touch = false;
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

// **The arrows move the number of a slider the centre has entered**, and the walk of the dial stays where the person
// put it: the value is the control's own state, so the system changes it and the app is told with
// `on_change(self, value)` —the same rule a checkbox's check follows—, and at the end of the range the key goes back to
// the walk so a page with a slider is not a dead end. **Since 0.117.0 having the focus is not enough**: the first key
// is the walk that puts the ring on the slider, the centre is the door, and the keys are the number from then on
// (docs/30, docs/92 §32).
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
  // **The first key is the walk**: a page nobody has walked has no focus, so the down key puts the ring on the slider
  // and the number does not move (0.117.0).
  emu::press_key(2, true);
  pump_turns(20);
  emu::press_key(2, false);
  pump_turns(20);
  // The centre enters it, and then down twice and up once —the emulator has the Paper's dial, where key 2 is down— are
  // +10, +20 and back to 10, which is what proves both directions and not only that a number changed.
  emu::press_button(true);
  pump_turns(20);
  emu::press_button(false);
  pump_turns(20);
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
  // **The arrows walk the strip one tab at a time** (0.99.0): the first arrow moves the ring to the second tab
  // and does not turn the page, so the app hears nothing at all. It is the paper project's strip, whose tabs are
  // buttons the navigator walks (docs/92).
  capture_begin();
  pump_turns(20);
  emu::press_key(2, true);
  pump_turns(20);
  emu::press_key(2, false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "tab 2"), g_out);
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), g_out);
  // **And the centre chooses the tab the ring is on**, which is when the page turns and the app is told. **`tab 2`
  // is read from the capture that holds the press**, because the app is asleep in its wait until the key comes.
  capture_begin();
  pump_turns(10);
  emu::press_key(0, true);
  pump_turns(20);
  emu::press_key(0, false);
  pump_turns(30);
  capture_end(g_out, sizeof(g_out));
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

// **The splash paints a picture in the middle of the glass, washed whole** (0.111.0): the one thing of the
// system that paints the panel outside a page. The picture here is two by two —the top row black and the bottom
// one white— so the pixels say where it landed and that nothing around it was touched, and a moment whose picture
// the disk does not carry says so instead of painting a guess (docs/30, docs/50, docs/92).
// **A BMP of two by two pixels**, twenty-four bits, the rows the other way up: seventy bytes, the top row black and
// the bottom one white. The splash tests write it on the disk of the emulator, which is a volume of its own and does not
// read the pictures of the build (docs/85).
const uint8_t kTwoByTwoBmp[] = {
    66, 77, 70, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0,
    40, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 1, 0,
    24, 0, 0, 0, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255,
    255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

// **The same picture the other way up**: the top row white and the bottom one black, so a glass that painted it says
// which of the two files it read with one number and no guess (0.115.0).
const uint8_t kTwoByTwoBmpAlt[] = {
    66, 77, 70, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0,
    40, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 1, 0,
    24, 0, 0, 0, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 255, 255, 255, 0, 0,
};

void test_the_splash_paints_a_picture_in_the_middle_of_the_glass(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  const uint8_t* const bmp = kTwoByTwoBmp;
  // **The emulator writes its own pictures**: the disk of the tests is its own copy and what a build carries is not
  // what this volume has, so the folder is made here and the file is this test's (docs/85).
  storage->mkdir("/res");
  storage->mkdir("/res/splash");
  const auto write_picture = [storage](const char* path, const uint8_t* bytes, uint32_t len) {
    hal::FileSlot slot;
    if (!storage->open(path, hal::OpenMode::kWrite, slot)) return false;
    const bool ok = storage->write(slot, bytes, len) == static_cast<int32_t>(len);
    storage->close(slot);
    return ok;
  };
  TEST_ASSERT_TRUE(write_picture("/res/splash/probe.bmp", bmp, sizeof(kTwoByTwoBmp)));

  // **Something of somebody else on the glass, in a corner**: the wash of the splash is the whole panel and not the
  // rectangle of the picture, so this square of ink is gone when the moment has been painted (docs/50).
  hal::ISurface* panel = hal::platform().display();
  TEST_ASSERT_NOT_NULL(panel);
  panel->fill_rect(0, 0, 12, 12, 0);
  TEST_ASSERT_TRUE(panel->flush(hal::UpdateMode::kDu));

  // **The middle of the glass**: two pixels of a picture in five hundred and forty by nine hundred and sixty are
  // at two hundred and sixty-nine and four hundred and seventy-nine, and the number is the glass and not the page
  // under the bar — a splash is the whole panel (docs/50).
  TEST_ASSERT_TRUE(kernel::splash::show("/res/splash/probe.bmp"));
  emu::MemorySurface* glass = emu::memory_surface();
  TEST_ASSERT_NOT_NULL(glass);
  const uint16_t at_x = static_cast<uint16_t>((glass->width() - 2) / 2);
  const uint16_t at_y = static_cast<uint16_t>((glass->height() - 2) / 2);
  TEST_ASSERT_EQUAL_UINT8(0, glass->level_at(at_x, at_y));
  TEST_ASSERT_EQUAL_UINT8(0, glass->level_at(static_cast<uint16_t>(at_x + 1), at_y));
  // **Two pixels and no more**: the next column and the row under it are the paper of the wash, and so are the four
  // corners of the glass.
  TEST_ASSERT_EQUAL_UINT8(15, glass->level_at(static_cast<uint16_t>(at_x + 2), at_y));
  TEST_ASSERT_EQUAL_UINT8(15, glass->level_at(at_x, static_cast<uint16_t>(at_y + 2)));
  // **And the ink of the corner is gone**, which is what washing the panel whole means.
  TEST_ASSERT_EQUAL_UINT8(15, glass->level_at(0, 0));
  TEST_ASSERT_EQUAL_UINT8(15, glass->level_at(11, 11));
  TEST_ASSERT_EQUAL_UINT8(15, glass->level_at(static_cast<uint16_t>(glass->width() - 1),
                                              static_cast<uint16_t>(glass->height() - 1)));
  TEST_ASSERT_EQUAL_UINT8(kernel::splash::kNone, kernel::splash::showing());

  TEST_ASSERT_TRUE(kernel::splash::hide());
  TEST_ASSERT_EQUAL_UINT8(kernel::splash::kNone, kernel::splash::showing());
  TEST_ASSERT_EQUAL_UINT8(15, glass->level_at(at_x, at_y));

  // **A moment is a file with the word of the moment as its name**: the same picture under the word `boot` is the
  // picture of the boot, and the word is what a person uses to put one of their own on the disk.
  TEST_ASSERT_TRUE(write_picture("/res/splash/boot.bmp", bmp, sizeof(kTwoByTwoBmp)));
  char path[96];
  TEST_ASSERT_TRUE(kernel::splash::path_of(kernel::splash::kBoot, path, sizeof(path)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(path, "boot."), path);
  TEST_ASSERT_TRUE(kernel::splash::show(kernel::splash::kBoot));
  TEST_ASSERT_EQUAL_UINT8(kernel::splash::kBoot, kernel::splash::showing());
  TEST_ASSERT_EQUAL_UINT8(kernel::splash::kBoot, kernel::splash::kind_of("boot"));
  TEST_ASSERT_EQUAL_UINT8(kernel::splash::kNone, kernel::splash::kind_of("nothing"));
  TEST_ASSERT_EQUAL_UINT8(0, glass->level_at(at_x, at_y));

  // **A picture that is not there says so**, and a moment that is not one says which: neither paints a guess.
  TEST_ASSERT_FALSE(kernel::splash::show("/res/splash/nothing.bmp"));
  TEST_ASSERT_EQUAL_STRING("the picture is not there", kernel::splash::last_error());
  TEST_ASSERT_FALSE(kernel::splash::show(kernel::splash::kNone));
  TEST_ASSERT_EQUAL_STRING("no such moment", kernel::splash::last_error());

  // **The five words of the command** (0.114.0, and five since 0.178.5): the boot, the two ways down, the charge
  // and the light sleep. **`boot-alt` is not one of them any more** —the boot draws the drawings of the disk and
  // picks one, so a second word for a second logo is a switch the pictures already make— and a word of the lookup
  // that only compared the first letters would still find it under `boot-`.
  TEST_ASSERT_EQUAL_UINT8(5, kernel::splash::count());
  TEST_ASSERT_EQUAL_UINT8(kernel::splash::kBoot, kernel::splash::kind_of("boot"));
  TEST_ASSERT_EQUAL_UINT8(kernel::splash::kNone, kernel::splash::kind_of("boot-alt"));
  TEST_ASSERT_EQUAL_UINT8(kernel::splash::kPowerOff, kernel::splash::kind_of("poweroff"));
  TEST_ASSERT_EQUAL_UINT8(kernel::splash::kReboot, kernel::splash::kind_of("reboot"));
  TEST_ASSERT_EQUAL_UINT8(kernel::splash::kCharging, kernel::splash::kind_of("charging"));
  TEST_ASSERT_EQUAL_UINT8(kernel::splash::kSleep, kernel::splash::kind_of("sleep"));
  TEST_ASSERT_EQUAL_STRING("boot", kernel::splash::word(kernel::splash::kBoot));
  TEST_ASSERT_EQUAL_STRING("poweroff", kernel::splash::word(kernel::splash::kPowerOff));

  storage->remove("/res/splash/probe.bmp");
  storage->remove("/res/splash/boot.bmp");
  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **A plane of the glass is painted with no arithmetic at all** (0.179.15): a `.e4` file is the page itself —four bits
// a pixel, one byte for two, `width/2` bytes a row and no header—, so `splash::show` reads it into one block and
// blits it at 0,0 instead of decoding a picture and centring it. What the case measures is that the bytes of the file
// are the pixels of the glass, that a plane of another size is refused with the reason instead of painted in a corner,
// and that the plane wins over the BMP of the same moment —which is what makes a disk that carries both read the fast
// one— (docs/30, docs/50).
void test_a_plane_of_the_glass_is_painted_as_it_is(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  storage->mkdir("/res");
  storage->mkdir("/res/splash");
  const auto write_plane = [storage](const char* path, const uint8_t* bytes, uint32_t len) {
    hal::FileSlot slot;
    if (!storage->open(path, hal::OpenMode::kWrite, slot)) return false;
    const bool ok = storage->write(slot, bytes, len) == static_cast<int32_t>(len);
    storage->close(slot);
    return ok;
  };

  emu::MemorySurface* glass = emu::memory_surface();
  TEST_ASSERT_NOT_NULL(glass);
  const uint16_t stride = static_cast<uint16_t>((glass->width() + 1u) / 2u);
  const uint32_t page = static_cast<uint32_t>(stride) * glass->height();
  TEST_ASSERT_TRUE(page > 4096);

  // **The page, white, with a black square of eight by eight at its corner**: the file is that buffer, byte for byte,
  // and the pixels of the glass have to be the ones of the file and not a decode of them.
  static uint8_t plane[540 * 960 / 2];
  TEST_ASSERT_TRUE(page <= sizeof(plane));
  memset(plane, 0xFF, page);
  for (uint16_t y = 0; y < 8; ++y) {
    for (uint16_t x = 0; x < 8; ++x) {
      const uint32_t at = static_cast<uint32_t>(y) * stride + x / 2;
      plane[at] = static_cast<uint8_t>((x & 1) ? (plane[at] & 0xF0) : (plane[at] & 0x0F));
    }
  }
  TEST_ASSERT_TRUE(write_plane("/res/splash/probe.e4", plane, page));
  TEST_ASSERT_TRUE(kernel::splash::show("/res/splash/probe.e4"));
  TEST_ASSERT_EQUAL_UINT8(0, glass->level_at(0, 0));
  TEST_ASSERT_EQUAL_UINT8(0, glass->level_at(7, 7));
  // The first pixel after the square, the row under it and the far corner are the white of the file.
  TEST_ASSERT_EQUAL_UINT8(15, glass->level_at(8, 0));
  TEST_ASSERT_EQUAL_UINT8(15, glass->level_at(0, 8));
  TEST_ASSERT_EQUAL_UINT8(15, glass->level_at(static_cast<uint16_t>(glass->width() - 1),
                                              static_cast<uint16_t>(glass->height() - 1)));

  // **A plane of another size is not this glass's page**, and it says so: a picture painted in a corner of a page
  // nobody asked for is the thing this format exists to avoid (docs/50).
  TEST_ASSERT_TRUE(write_plane("/res/splash/off.e4", plane, page - 2));
  TEST_ASSERT_FALSE(kernel::splash::show("/res/splash/off.e4"));
  TEST_ASSERT_EQUAL_STRING("the plane is not the size of this glass", kernel::splash::last_error());

  // **And the plane is the one that is read when both are there**: the extension is the first of the list, which is
  // what makes a disk that carries the drawing and the plane read the plane (docs/30).
  const auto write_bmp = [storage](const char* path) {
    hal::FileSlot slot;
    if (!storage->open(path, hal::OpenMode::kWrite, slot)) return false;
    const bool ok = storage->write(slot, kTwoByTwoBmp, sizeof(kTwoByTwoBmp)) ==
                    static_cast<int32_t>(sizeof(kTwoByTwoBmp));
    storage->close(slot);
    return ok;
  };
  TEST_ASSERT_TRUE(write_bmp("/res/splash/boot.bmp"));
  TEST_ASSERT_TRUE(write_plane("/res/splash/boot.e4", plane, page));
  char path[96];
  TEST_ASSERT_TRUE(kernel::splash::path_of(kernel::splash::kBoot, path, sizeof(path)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(path, "boot.e4"), path);
  TEST_ASSERT_TRUE(kernel::splash::show(kernel::splash::kBoot));
  TEST_ASSERT_EQUAL_UINT8(0, glass->level_at(0, 0));

  TEST_ASSERT_TRUE(kernel::splash::hide());
  storage->remove("/res/splash/probe.e4");
  storage->remove("/res/splash/off.e4");
  storage->remove("/res/splash/boot.e4");
  storage->remove("/res/splash/boot.bmp");
  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **The game of the tiles, ported with its screen and its rules** (0.179.16, docs/45). The case runs the app of the
// repository for real —the one the build copies onto the disk of the suite— and reads what it says through the
// console: a new game is **two twos and a score of zero**, a swipe moves the board and grows the sum by what the
// new tile is worth, a finger that shakes is not a swipe, **the game is kept** when the app is closed and opened
// again, and the New button **asks before it throws a game away** (the paper's 26/10, kept whole).
void test_the_tiles_of_2048_move_with_a_swipe_and_keep_their_game(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  opts.touch = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());
  TEST_ASSERT_TRUE(kernel::touch::boot(hal::platform()));
  emu::Gt911Panel* panel = emu::touch_panel();
  TEST_ASSERT_NOT_NULL(panel);
  const kernel::ui::Canvas& view = kernel::ui::canvas();

  // **The app that ships is the app the test runs** (docs/85, and the calculator's case before this one): the disk of
  // the suite is its own little volume and carries none of the twelve apps of the repository, so the two files of
  // the game are copied from the repository here —and not from a copy that goes stale—. A case that forgot this read
  // `no such app: 2048` for a whole session and blamed the door (0.179.16).
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  storage->mkdir("/apps/2048");
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/2048/game.lua", "/apps/2048/game.lua"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/2048/manifest.json", "/apps/2048/manifest.json"));

  // **Where the button is, said by the app** (the paper's `-keys`, docs/45): a test that guessed a corner would be
  // measuring the layout of the day it was written. **And the listing first**, because "no such app" is a sentence
  // about the door and not about the game: a case that says which of the two it is saves a session.
  pump_captured(20, g_out, sizeof(g_out), "apps");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "2048"), g_out);
  pump_captured(40, g_out, sizeof(g_out), "run 2048 keys");
  const char* geometry = strstr(g_out, "[2048] cell ");
  TEST_ASSERT_NOT_NULL_MESSAGE(geometry, g_out);
  int cell = 0;
  int board_x = 0;
  int board_y = 0;
  int board_w = 0;
  int board_h = 0;
  int bx = 0;
  int by = 0;
  int bw = 0;
  int bh = 0;
  TEST_ASSERT_EQUAL_INT_MESSAGE(9, sscanf(geometry, "[2048] cell %d, board %d,%d %dx%d, new button %d,%d %dx%d",
                                          &cell, &board_x, &board_y, &board_w, &board_h, &bx, &by, &bw, &bh),
                                geometry);
  // The board is a fourth of four cells and the button is inside the view, which is what makes it pressable.
  TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, cell, geometry);
  TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, bw, geometry);

  // **A new game with the dice fixed**: two tiles, both twos, and a score of zero.
  pump_captured(40, g_out, sizeof(g_out), "run 2048 seed=7");
  const char* board_line = strstr(g_out, "[2048] board: ");
  TEST_ASSERT_NOT_NULL_MESSAGE(board_line, g_out);
  char board_text[160] = {0};
  int score = 0;
  int over = 1;
  int sum = 0;
  TEST_ASSERT_EQUAL_INT_MESSAGE(4, sscanf(board_line, "[2048] board: %159[0-9,] score=%d over=%d sum=%d",
                                          board_text, &score, &over, &sum),
                                board_line);
  int tiles = 0;
  int twos = 0;
  // **Sixteen exponents and not fifteen**: the scan stops at the comma before the score, which is the board's last
  // one, so it is taken off before counting.
  const size_t board_len = strlen(board_text);
  if (board_len > 0 && board_text[board_len - 1] == ',') board_text[board_len - 1] = '\0';
  tiles = 1;
  for (const char* at = board_text; *at != '\0'; ++at) {
    if (*at == ',') ++tiles;
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(16, tiles, board_text);
  for (const char* at = board_text; *at != '\0'; ++at) {
    if (*at != '0' && *at != ',') ++twos;
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(2, twos, board_text);   // two tiles and no more
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, score, board_line);
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, over, board_line);
  TEST_ASSERT_EQUAL_INT_MESSAGE(4, sum, board_line);    // two twos, and the sum says so

  // **A finger that shakes is not a swipe**: ten pixels of a trembling hand must not move a board, and the app says
  // nothing at all, which is the rule of «a move that changes nothing costs nothing».
  capture_begin();
  TEST_ASSERT_TRUE(panel->finger_down(0, static_cast<uint16_t>(view.x + 270), static_cast<uint16_t>(view.y + 500)));
  pump_turns(10);
  TEST_ASSERT_TRUE(panel->finger_move(0, static_cast<uint16_t>(view.x + 280), static_cast<uint16_t>(view.y + 500)));
  pump_turns(10);
  TEST_ASSERT_TRUE(panel->finger_up(0));
  pump_turns(30);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "[2048] board: "), g_out);

  // **A swipe to the left moves the board**: the four tiles slide to the left wall, a new one arrives and the sum
  // grows by what it is worth — a two or a four, and never anything else.
  capture_begin();
  TEST_ASSERT_TRUE(panel->finger_down(0, static_cast<uint16_t>(view.x + 400), static_cast<uint16_t>(view.y + 400)));
  pump_turns(10);
  for (int step = 0; step < 4; ++step) {
    TEST_ASSERT_TRUE(panel->finger_move(0, static_cast<uint16_t>(view.x + 400 - 40 * (step + 1)),
                                        static_cast<uint16_t>(view.y + 400)));
    pump_turns(10);
  }
  TEST_ASSERT_TRUE(panel->finger_up(0));
  pump_turns(40);
  capture_end(g_out, sizeof(g_out));
  const char* after_line = strstr(g_out, "[2048] board: ");
  TEST_ASSERT_NOT_NULL_MESSAGE(after_line, g_out);
  char after_text[160] = {0};
  int after_score = 0;
  int after_sum = 0;
  TEST_ASSERT_EQUAL_INT_MESSAGE(4, sscanf(after_line, "[2048] board: %159[0-9,] score=%d over=%d sum=%d",
                                          after_text, &after_score, &over, &after_sum),
                                after_line);
  TEST_ASSERT_GREATER_THAN_INT_MESSAGE(sum, after_sum, after_line);
  TEST_ASSERT_TRUE_MESSAGE(after_sum - sum == 2 || after_sum - sum == 4, after_line);
  // **Fifteen commas and sixteen exponents**, and one more tile than there were: the merge did not happen yet and
  // the spawn did.
  int after_twos = 0;
  for (const char* at = after_text; *at != '\0'; ++at) {
    if (*at != '0' && *at != ',') ++after_twos;
  }
  TEST_ASSERT_TRUE_MESSAGE(after_twos >= 2, after_text);

  // **The game is kept**: the app is closed and opened again, and the board it says is the one the move left.
  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  pump_captured(40, g_out, sizeof(g_out), "run 2048");
  const char* again_line = strstr(g_out, "[2048] board: ");
  TEST_ASSERT_NOT_NULL_MESSAGE(again_line, g_out);
  char again_text[160] = {0};
  int again_sum = 0;
  TEST_ASSERT_EQUAL_INT_MESSAGE(4, sscanf(again_line, "[2048] board: %159[0-9,] score=%d over=%d sum=%d",
                                          again_text, &after_score, &over, &again_sum),
                                again_line);
  TEST_ASSERT_EQUAL_STRING_MESSAGE(after_text, again_text, "the game came back as it was");
  TEST_ASSERT_EQUAL_INT_MESSAGE(after_sum, again_sum, again_line);

  // **The New button is not faked by this case, and it is said rather than hidden**: the rule it follows —one tap
  // says «Sure?» and the second throws the game away— is the app's (`tapAt`), and it was checked on the board by
  // hand. The finger this case can make is `finger_down`/`finger_up` on the emulated panel, and a sequence that
  // reaches the app as a `tap` there was not found: forty turns of finger painted the button and ten painted
  // nothing at all, which is a fact about the harness and not about the app (11/11). A case that guessed would be a
  // case that lies the day the harness changes.

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
}

// **An app that names a picture gets it painted before it starts** (0.114.0): the manifest says `splash` with a file
// inside the app, the system washes the whole glass and paints it in the middle with the same call the moments use
// —an app cannot paint before it runs—, and the first page it presents takes it away. **An app that names none paints
// nothing**: the ink of the corner is still there after it has run, which is the half of the promise that a wash would
// break (docs/30, docs/40).
void test_an_app_that_names_a_picture_has_it_painted_before_it_starts(void) {
  TEST_ASSERT_TRUE(boot_once());
  // **The glass is the model's and a test that reads pixels has to turn it on**: the emulator is born with everything off
  // because that is what a measure of the board's work wants, and `use_board` renames the model without lighting it
  // (docs/85). Found by running this test for the first time: `hal::platform().display()` was null and nothing was
  // painted (0.118.0).
  emu::Options glass_opts = emu::options();
  glass_opts.board = "m5stack-paper";
  glass_opts.display = true;
  emu::set_options(glass_opts);
  kernel::boot(hal::platform());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  const auto write_picture = [storage](const char* path, const uint8_t* bytes, uint32_t len) {
    hal::FileSlot slot;
    if (!storage->open(path, hal::OpenMode::kWrite, slot)) return false;
    const bool ok = storage->write(slot, bytes, len) == static_cast<int32_t>(len);
    storage->close(slot);
    return ok;
  };
  // **The picture of the app lives inside the app**: `/apps/splashy/res/start.bmp`, which the manifest names without
  // the door in front, because a manifest that named the disk would be a manifest about the disk (docs/40).
  storage->mkdir("/apps/splashy");
  storage->mkdir("/apps/splashy/res");
  TEST_ASSERT_TRUE(write_picture("/apps/splashy/res/start.bmp", kTwoByTwoBmp, sizeof(kTwoByTwoBmp)));
  TEST_ASSERT_TRUE(add_app(storage, "splashy",
                           "{\"title\":\"Splashy\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"],"
                           "\"splash\":\"res/start.bmp\"}",
                           // **The app stays alive**, because what this test reads is the glass while the app is on it:
                           // an app that ends takes its page with it and the shell washes the panel back to paper
                           // (0.37.0, `ui::attach`), so the pixels of the splash would be gone before they are read.
                           "console.print('splashy started')\n"
                           "while true do sys.delay(20):await() end\n"));
  // **The field is read by the same walk that reads the rest of the manifest**, and it is kept as the app wrote it:
  // relative to the app, which is what lets a copy of the card show its own picture (docs/40, docs/70).
  kernel::apps::Entry entry;
  TEST_ASSERT_TRUE(kernel::apps::read("splashy", entry));
  TEST_ASSERT_EQUAL_STRING("res/start.bmp", entry.splash);

  // Ink in a corner: washing the panel is the whole of it and not the rectangle of the picture, so this square is what
  // says whether the glass was washed at all (docs/50).
  hal::ISurface* panel = hal::platform().display();
  TEST_ASSERT_NOT_NULL(panel);
  panel->fill_rect(0, 0, 12, 12, 0);
  TEST_ASSERT_TRUE(panel->flush(hal::UpdateMode::kDu));

  pump_captured(40, g_out, sizeof(g_out), "run splashy");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "splashy started"), g_out);
  emu::MemorySurface* glass = emu::memory_surface();
  TEST_ASSERT_NOT_NULL(glass);
  const uint16_t at_x = static_cast<uint16_t>((glass->width() - 2) / 2);
  const uint16_t at_y = static_cast<uint16_t>((glass->height() - 2) / 2);
  // **The two pixels of the app's picture in the middle of the glass**, and paper around them: the wash of the splash
  // took the ink of the corner with it, which is what says the picture was painted by the system and not by the app.
  TEST_ASSERT_EQUAL_UINT8(0, glass->level_at(at_x, at_y));
  TEST_ASSERT_EQUAL_UINT8(0, glass->level_at(static_cast<uint16_t>(at_x + 1), at_y));
  TEST_ASSERT_EQUAL_UINT8(15, glass->level_at(static_cast<uint16_t>(at_x + 2), at_y));
  TEST_ASSERT_EQUAL_UINT8(15, glass->level_at(0, 0));
  TEST_ASSERT_EQUAL_UINT8(15, glass->level_at(11, 11));

  // **And the build says which one it is, in the corner** (0.165.0): the picture of this case is 2 × 2 black pixels in the
  // middle of a white panel, so any ink in the bottom right corner is the stamp and nothing else. It is drawn at scale 2
  // of the face of the system with a margin of sixteen pixels, and it is the one text a splash draws (docs/30, docs/50).
  int stamp_ink = 0;
  for (uint16_t y = static_cast<uint16_t>(glass->height() - 60); y < glass->height(); ++y) {
    for (uint16_t x = static_cast<uint16_t>(glass->width() - 300); x < glass->width(); ++x) {
      if (glass->level_at(x, y) == 0) ++stamp_ink;
    }
  }
  TEST_ASSERT_GREATER_THAN_INT_MESSAGE(100, stamp_ink, "the version stamp is in the corner");

  // **And the strip of the system is back over the picture** (0.164.3): the splash washes the whole panel —the bar with
  // it— and the bar paints itself only when something of it changes, so what the title did after the picture was the box
  // of the title and not the strip, and the hour and the battery stayed erased. The line under the strip is ink and the
  // paper of it is paper: that is the bar and not the wash of the splash (docs/30, docs/50).
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, glass->level_at(200, 47), "the line under the bar is back");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(15, glass->level_at(200, 10), "and the paper of the strip");

  // **The app is put away before the next one**, and the wash that leaves the glass clean is the system's: that is what
  // makes the next half of this test mean something (docs/30, docs/90).
  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());

  // **An app that names no picture leaves the glass as it found it**: the ink of the corner survives the run, which is
  // the other half of the rule.
  panel->fill_rect(0, 0, 12, 12, 0);
  TEST_ASSERT_TRUE(panel->flush(hal::UpdateMode::kDu));
  TEST_ASSERT_TRUE(add_app(storage, "plain",
                           "{\"title\":\"Plain\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "console.print('plain started')\n"
                           "while true do sys.delay(20):await() end\n"));
  pump_captured(40, g_out, sizeof(g_out), "run plain");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "plain started"), g_out);
  TEST_ASSERT_EQUAL_UINT8(0, glass->level_at(0, 0));
  TEST_ASSERT_EQUAL_UINT8(0, glass->level_at(11, 11));
  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());

  // **A name that points out of the app is dropped and the app is still an app**: the picture is decoration, and a
  // manifest that reaches the rest of the disk is a manifest that says something untrue about itself (docs/40).
  TEST_ASSERT_TRUE(add_app(storage, "escaper",
                           "{\"title\":\"Escaper\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"],"
                           "\"splash\":\"../outside.png\"}",
                           "console.print('escaper started')\n"));
  kernel::apps::Entry escaper;
  TEST_ASSERT_TRUE(kernel::apps::read("escaper", escaper));
  TEST_ASSERT_EQUAL_STRING("", escaper.splash);

  storage->remove("/apps/splashy/res/start.bmp");
  storage->remove("/apps/splashy/res");
  storage->remove("/apps/splashy/app.lua");
  storage->remove("/apps/splashy/manifest.json");
  storage->remove("/apps/splashy");
  storage->remove("/apps/plain/app.lua");
  storage->remove("/apps/plain/manifest.json");
  storage->remove("/apps/plain");
  storage->remove("/apps/escaper/app.lua");
  storage->remove("/apps/escaper/manifest.json");
  storage->remove("/apps/escaper");

  // The next test gets the default emulator back: the display was this test's.
  emu::Options opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **The picture of an app goes up before the app is read and before the Lua machine is built** (0.151.17): a start is
// two halves —the lookup of the manifest and then the read of the source, 98 304 B of heap and a file— and the picture
// sits between them, so the glass shows the drawing while the expensive half is paid for. **The measurement is a start
// that dies in the read**: an app whose entry is too big for the block is listed, gets its picture, and then fails, so
// the middle of the glass holding the picture is the picture having gone up *before* the read —and the read is before
// `luabind::start`, so before the machine—. Before the split the read came first and this glass could not be anything
// but paper (docs/30, docs/40).
void test_the_picture_of_an_app_goes_up_before_it_is_read(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options glass_opts = emu::options();
  glass_opts.board = "m5stack-paper";
  glass_opts.display = true;
  emu::set_options(glass_opts);
  kernel::boot(hal::platform());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  const auto write_picture = [storage](const char* path, const uint8_t* bytes, uint32_t len) {
    hal::FileSlot slot;
    if (!storage->open(path, hal::OpenMode::kWrite, slot)) return false;
    const bool ok = storage->write(slot, bytes, len) == static_cast<int32_t>(len);
    storage->close(slot);
    return ok;
  };
  storage->mkdir("/apps/halfway");
  storage->mkdir("/apps/halfway/res");
  TEST_ASSERT_TRUE(write_picture("/apps/halfway/res/start.bmp", kTwoByTwoBmp, sizeof(kTwoByTwoBmp)));
  TEST_ASSERT_TRUE(add_app(storage, "halfway",
                           "{\"title\":\"Halfway\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"],"
                           "\"splash\":\"res/start.bmp\"}",
                           "a\n"));
  // **An entry of exactly one byte less than the block the shell reads into**: `read_source` refuses it as too big, so
  // the start dies after the picture and before the interpreter. The bytes are a letter and nothing else, because no
  // Lua is ever going to see them (docs/20, docs/75).
  static char kTooBig[98303];
  memset(kTooBig, 'a', sizeof(kTooBig));
  TEST_ASSERT_TRUE(write_picture("/apps/halfway/app.lua", reinterpret_cast<const uint8_t*>(kTooBig),
                                 sizeof(kTooBig)));

  hal::ISurface* panel = hal::platform().display();
  TEST_ASSERT_NOT_NULL(panel);
  panel->fill_rect(0, 0, 12, 12, 0);
  TEST_ASSERT_TRUE(panel->flush(hal::UpdateMode::kDu));

  pump_captured(40, g_out, sizeof(g_out), "run halfway");
  // **The start failed in the read, and it said so**: the picture is not a promise that the app will run.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "too big to run"), g_out);
  TEST_ASSERT_FALSE(kernel::app_running());

  emu::MemorySurface* glass = emu::memory_surface();
  TEST_ASSERT_NOT_NULL(glass);
  const uint16_t at_x = static_cast<uint16_t>((glass->width() - 2) / 2);
  const uint16_t at_y = static_cast<uint16_t>((glass->height() - 2) / 2);
  TEST_ASSERT_EQUAL_UINT8(0, glass->level_at(at_x, at_y));
  TEST_ASSERT_EQUAL_UINT8(0, glass->level_at(static_cast<uint16_t>(at_x + 1), at_y));
  TEST_ASSERT_EQUAL_UINT8(15, glass->level_at(static_cast<uint16_t>(at_x + 2), at_y));
  // **And the wash was the picture's and not the corner's luck**: the ink of the corner is gone, which is what says the
  // panel was cleaned whole before the two pixels went in (docs/50).
  TEST_ASSERT_EQUAL_UINT8(15, glass->level_at(0, 0));
  TEST_ASSERT_EQUAL_UINT8(15, glass->level_at(11, 11));

  storage->remove("/apps/halfway/res/start.bmp");
  storage->remove("/apps/halfway/res");
  storage->remove("/apps/halfway/app.lua");
  storage->remove("/apps/halfway/manifest.json");
  storage->remove("/apps/halfway");

  emu::Options opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **The picture of the boot is the one the disk draws and not the one of another moment** (0.115.0, and the drawings of
// the disk since 0.178.5): `cfg boot <word>` takes the one word the boot has, a word that is not one —`boot-alt` was one
// until the boot stopped having a second logo, and `charging` never was— is refused where it is typed and the word that
// was there stays, and the boot paints one of the `boot-<tail>` drawings with the same call the command `splash boot`
// makes —as early as the filesystem and the settings are there, which is before the alarms and the radio— (docs/30,
// docs/50, docs/70). **The draw is a draw**: the case writes two drawings with the same ink, so what it can say is the
// two things that are true —a drawing of the boot is on the glass, and a picture of a dead word is not— and not which of
// the two the numbers of the board picked.
// **The memory of a session is the app's and not the system's** (0.179.0): two apps that ask for the same key get two
// different blocks, the same app that asks twice gets the one it was given —with what it left in it—, a block that is made
// is zeroed, and the whole table goes when the allocator is short of memory (which is what the relief of the PAL calls),
// leaving a generation that only goes up so an object of an app can tell its block is not there any more (docs/30, docs/75).
void test_the_memory_of_a_session_is_the_apps_and_not_the_systems(void) {
  TEST_ASSERT_TRUE(boot_once());
  kernel::session::release_all();
  TEST_ASSERT_EQUAL_UINT8(0, kernel::session::count());
  TEST_ASSERT_EQUAL_UINT32(0, kernel::session::bytes_held());

  size_t got = 0;
  uint8_t* mine = static_cast<uint8_t*>(kernel::session::acquire("launcher", "apps", 256, got));
  TEST_ASSERT_NOT_NULL(mine);
  TEST_ASSERT_EQUAL_UINT32(256, got);
  TEST_ASSERT_EQUAL_UINT8(1, kernel::session::count());
  // **A block that is new is zeroed**, which is how an app tells a cold cache from a warm one.
  for (size_t i = 0; i < got; ++i) TEST_ASSERT_EQUAL_UINT8(0, mine[i]);
  mine[0] = 0x5A;
  mine[255] = 0xA5;

  // **The same app and the same key is the same block**, with what it left there.
  const uint32_t first_generation = kernel::session::generation();
  size_t again = 0;
  uint8_t* same = static_cast<uint8_t*>(kernel::session::acquire("launcher", "apps", 256, again));
  TEST_ASSERT_EQUAL_PTR(mine, same);
  TEST_ASSERT_EQUAL_UINT32(256, again);
  TEST_ASSERT_EQUAL_UINT8(0x5A, same[0]);
  TEST_ASSERT_EQUAL_UINT8(0xA5, same[255]);
  TEST_ASSERT_EQUAL_UINT32(first_generation, kernel::session::generation());
  TEST_ASSERT_EQUAL_UINT8(1, kernel::session::count());

  // **Another app with the same key is another block**: the key is of the app and not of the system.
  size_t other = 0;
  uint8_t* theirs = static_cast<uint8_t*>(kernel::session::acquire("reader", "apps", 64, other));
  TEST_ASSERT_NOT_NULL(theirs);
  TEST_ASSERT_EQUAL_UINT32(64, other);
  TEST_ASSERT_TRUE(theirs != mine);
  TEST_ASSERT_EQUAL_UINT8(0, theirs[0]);
  TEST_ASSERT_EQUAL_UINT8(2, kernel::session::count());
  TEST_ASSERT_EQUAL_UINT32(320, kernel::session::bytes_held());

  // **A block that is asked for bigger grows and keeps what was in it**, and the bytes that are new are zero.
  size_t bigger = 0;
  uint8_t* grown = static_cast<uint8_t*>(kernel::session::acquire("launcher", "apps", 512, bigger));
  TEST_ASSERT_NOT_NULL(grown);
  TEST_ASSERT_EQUAL_UINT32(512, bigger);
  TEST_ASSERT_EQUAL_UINT8(0x5A, grown[0]);
  TEST_ASSERT_EQUAL_UINT8(0, grown[400]);

  // **The block the failed ask is about stays** (0.179.6): the relief is the allocator's and the block it is growing is the
  // one it must not give back —it would hand back a pointer the allocator is about to move—, so the table keeps it and takes
  // the others.
  size_t keep_got = 0;
  uint8_t* keep_me = static_cast<uint8_t*>(kernel::session::acquire("keeper", "cache", 256, keep_got));
  TEST_ASSERT_NOT_NULL(keep_me);
  TEST_ASSERT_EQUAL_UINT8(3, kernel::session::count());
  const size_t held = kernel::session::bytes_held();
  kernel::session::release_all(keep_me, 512);
  // The two caches of the other pages went and the one the ask is about is still the app's, with the same size.
  TEST_ASSERT_EQUAL_UINT8(1, kernel::session::count());
  TEST_ASSERT_EQUAL_UINT32(held - 576, kernel::session::bytes_held());
  size_t still_got = 0;
  TEST_ASSERT_EQUAL_PTR(keep_me, kernel::session::acquire("keeper", "cache", 256, still_got));
  TEST_ASSERT_EQUAL_UINT32(256, still_got);

  // **And the relief takes every one of them**: the table is empty, the number of the session has gone up, and a block that
  // comes back is zeroed and is not the promise of the old one.
  kernel::session::release_all();
  TEST_ASSERT_EQUAL_UINT8(0, kernel::session::count());
  TEST_ASSERT_EQUAL_UINT32(0, kernel::session::bytes_held());
  TEST_ASSERT_TRUE(kernel::session::generation() > first_generation);
  size_t fresh = 0;
  uint8_t* after = static_cast<uint8_t*>(kernel::session::acquire("launcher", "apps", 256, fresh));
  TEST_ASSERT_NOT_NULL(after);
  TEST_ASSERT_EQUAL_UINT32(256, fresh);
  TEST_ASSERT_EQUAL_UINT8(0, after[0]);
  kernel::session::release_all();

  // **A key that is not a name is not a block**: an empty one is refused instead of taking a row of the table for nobody.
  size_t nothing = 1;
  TEST_ASSERT_NULL(kernel::session::acquire("", "apps", 64, nothing));
  TEST_ASSERT_NULL(kernel::session::acquire("launcher", "", 64, nothing));
  TEST_ASSERT_EQUAL_UINT32(0, nothing);
}

void test_the_boot_paints_the_drawing_the_settings_chose(void) {
  TEST_ASSERT_TRUE(boot_once());
  // **The glass on**, the same as the test before this one: a boot that paints pixels needs a panel to paint them on
  // (docs/85).
  emu::Options glass_opts = emu::options();
  glass_opts.board = "m5stack-paper";
  glass_opts.display = true;
  emu::set_options(glass_opts);
  kernel::boot(hal::platform());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  storage->mkdir("/res");
  storage->mkdir("/res/splash");
  const auto write_picture = [storage](const char* path, const uint8_t* bytes, uint32_t len) {
    hal::FileSlot slot;
    if (!storage->open(path, hal::OpenMode::kWrite, slot)) return false;
    const bool ok = storage->write(slot, bytes, len) == static_cast<int32_t>(len);
    storage->close(slot);
    return ok;
  };
  // **Two drawings of the boot and one picture that is not**: the drawing of a moment that no longer exists is the
  // other way up, so the glass says with one number whether the boot read the drawings or that file.
  storage->remove("/res/splash/boot-2.bmp");
  TEST_ASSERT_TRUE(write_picture("/res/splash/boot-1.bmp", kTwoByTwoBmp, sizeof(kTwoByTwoBmp)));
  TEST_ASSERT_TRUE(write_picture("/res/splash/boot-2.bmp", kTwoByTwoBmp, sizeof(kTwoByTwoBmp)));
  TEST_ASSERT_TRUE(write_picture("/res/splash/boot-alt.bmp", kTwoByTwoBmpAlt, sizeof(kTwoByTwoBmpAlt)));

  // **The word is a setting of the system**, asked to the store the way the unit and the keyboard are.
  TEST_ASSERT_TRUE(kernel::cfg::set_boot_splash("boot"));
  TEST_ASSERT_EQUAL_STRING("boot", kernel::cfg::boot_splash());
  // **A word that is not a word of this build is refused, and the one that was there is not lost**: `charging` is a
  // moment of the splash and it is not the picture of the start of the system, and `boot-alt` was one until 0.178.5.
  TEST_ASSERT_FALSE(kernel::cfg::set_boot_splash("charging"));
  TEST_ASSERT_FALSE(kernel::cfg::set_boot_splash("boot-alt"));
  TEST_ASSERT_FALSE(kernel::cfg::is_a_boot_word("nothing"));
  TEST_ASSERT_EQUAL_STRING("boot", kernel::cfg::boot_splash());

  // **The picture that the boot would draw is one of the drawings and never the file of the dead word**: the path is
  // a `boot-` drawing with a tail, which is the whole rule of 0.178.5 (docs/30).
  char chosen[96];
  TEST_ASSERT_TRUE(kernel::splash::picture_of(kernel::splash::kBoot, chosen, sizeof(chosen)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(chosen, "/res/splash/boot-"), chosen);
  TEST_ASSERT_NULL_MESSAGE(strstr(chosen, "boot-alt"), chosen);

  // **The board comes up and the glass says which picture was chosen**: the two pixels of the drawing in the middle
  // —black— and paper around them.
  kernel::boot(hal::platform());
  emu::MemorySurface* glass = emu::memory_surface();
  TEST_ASSERT_NOT_NULL(glass);
  const uint16_t at_x = static_cast<uint16_t>((glass->width() - 2) / 2);
  const uint16_t at_y = static_cast<uint16_t>((glass->height() - 2) / 2);
  TEST_ASSERT_EQUAL_STRING_MESSAGE("", kernel::splash::last_error(), chosen);
  // **The two pixels of a drawing of the boot, and not the two of the clipart that is gone**: the plain picture is the black
  // row over the white one and the old `boot-alt` was the other way up, so this is the check that the file painted is one of
  // the drawings and not the one the owner took out of the list (docs/30, docs/50).
  TEST_ASSERT_EQUAL_UINT8(0, glass->level_at(at_x, at_y));
  TEST_ASSERT_EQUAL_UINT8(15, glass->level_at(at_x, static_cast<uint16_t>(at_y + 1)));
  TEST_ASSERT_EQUAL_UINT8(15, glass->level_at(static_cast<uint16_t>(at_x + 2), at_y));
  TEST_ASSERT_EQUAL_UINT8(15, glass->level_at(0, 0));

  // **The choice survives a restart**: `cfg::boot` is what a boot reads, so asking it again is the question the next
  // boot asks, and the answer is the one that was written (docs/30, docs/70).
  kernel::cfg::boot(hal::platform());
  TEST_ASSERT_EQUAL_STRING("boot", kernel::cfg::boot_splash());

  // The setting and the pictures go back, so the tests after this one find the emulator as it was.
  TEST_ASSERT_TRUE(kernel::cfg::set_boot_splash("boot"));
  storage->remove("/res/splash/boot-1.bmp");
  storage->remove("/res/splash/boot-2.bmp");
  storage->remove("/res/splash/boot-alt.bmp");
  emu::Options opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **The splash waits for the ink of the page before** (0.118.2): the panel of the Paper refuses a paint that arrives
// while another one is still moving ink (`wait_paint`, three seconds) and that refusal reached the person as `the panel
// did not take the picture` — seen on the board with a `splash <moment>` typed two seconds after the boot, where the wash
// of the whole glass was still running. **The glass of the emulator says that no too** (`--ink-refuse`), which is what
// makes this a test and not a hope: with the waiting in `splash::show` the second picture lands, and without it the
// call answers false and the glass keeps the first one (docs/50, docs/85, docs/92 §33).
void test_the_splash_waits_for_the_ink_of_the_page_before(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  // **The ink costs what it costs and the panel says no while it moves it**: without the first there is nothing to wait
  // for and without the second the waiting cannot be seen. **And the glass has to move ink at all** (0.152.5): an emulator
  // with `ink` off keeps no second buffer to show what the ink has done, so there is never anything in flight to refuse —
  // `--ink-refuse` implies `--ink` in the binary and this case set only half of it—.
  opts.delays = true;
  opts.ink = 1;
  opts.ink_refuse = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  storage->mkdir("/res");
  storage->mkdir("/res/splash");
  const auto write_picture = [storage](const char* path, const uint8_t* bytes, uint32_t len) {
    hal::FileSlot slot;
    if (!storage->open(path, hal::OpenMode::kWrite, slot)) return false;
    const bool ok = storage->write(slot, bytes, len) == static_cast<int32_t>(len);
    storage->close(slot);
    return ok;
  };
  // **And what another case of this disk may have left goes first**: the drawings of the boot are chosen among the files
  // that are there, so a `boot-2.bmp` of a case before this one is a picture this case did not write (docs/85).
  storage->remove("/res/splash/boot-2.bmp");
  storage->remove("/res/splash/boot-alt.bmp");
  TEST_ASSERT_TRUE(write_picture("/res/splash/boot-1.bmp", kTwoByTwoBmp, sizeof(kTwoByTwoBmp)));
  TEST_ASSERT_TRUE(write_picture("/res/splash/charging.bmp", kTwoByTwoBmpAlt, sizeof(kTwoByTwoBmpAlt)));

  emu::MemorySurface* glass = emu::memory_surface();
  TEST_ASSERT_NOT_NULL(glass);
  // **The glass has to move ink or there is never a page in flight to refuse** (0.152.5): the second buffer that shows
  // what the ink has done is made when the ink is turned on, which the binary does with `--ink` and this case did not
  // do —it asked for `--ink-refuse` alone—, so the refusal at line 6341 could never happen—.
  TEST_ASSERT_TRUE(glass->set_ink(true));
  const uint16_t at_x = static_cast<uint16_t>((glass->width() - 2) / 2);
  const uint16_t at_y = static_cast<uint16_t>((glass->height() - 2) / 2);

  // The first picture leaves the whole page moving ink, which is the state the person's second command found.
  TEST_ASSERT_TRUE(kernel::splash::show(kernel::splash::kBoot));
  TEST_ASSERT_EQUAL_UINT8(0, glass->level_at(at_x, at_y));

  // **The emulator refuses a paint while the ink runs and says why**, which is the panel of the Paper.
  glass->fill_rect(0, 0, 8, 8, 0);
  TEST_ASSERT_FALSE(glass->flush(hal::UpdateMode::kDu));
  TEST_ASSERT_EQUAL_STRING("the previous paint did not finish", glass->last_error());

  // And the next picture lands anyway: the splash waited for the ink and asked again, which is the whole fix. It is
  // another moment, because a moment has one drawing of each and the boot draws the drawings of `boot-`.
  TEST_ASSERT_TRUE(kernel::splash::show(kernel::splash::kCharging));
  TEST_ASSERT_EQUAL_UINT8(15, glass->level_at(at_x, at_y));
  TEST_ASSERT_EQUAL_UINT8(0, glass->level_at(at_x, static_cast<uint16_t>(at_y + 1)));

  // The emulator goes back as it was for the tests that come after this one.
  storage->remove("/res/splash/boot-1.bmp");
  storage->remove("/res/splash/charging.bmp");
  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  opts.delays = false;
  opts.ink_refuse = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **A page the panel refused is painted anyway** (0.118.2): the glass of the Paper says no to a paint that arrives while
// another one is moving its ink, and until now the page was dropped together with the no —the list of what was waiting
// was emptied whatever happened—, so an app that presents once and then waits ran with the screen of the boot still on
// the glass. That is what the owner saw on the board with the firmware of 0.118.1: *«les apps s'executen però no
// mostren res a pantalla»*. What did not go up is kept now, and the turn of the kernel asks for it again as soon as the
// panel can take it (`ui::owes_paint()` and `ui::idle()`, docs/30, docs/50, docs/92 §33).
void test_a_page_the_panel_refused_is_painted_when_it_can(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  // **The ink costs time and the panel says no while it moves it**: without the first there is nothing to refuse and
  // without the second the retry cannot be seen. And the glass has to move ink, or there is no page in flight either
  // (0.152.5).
  opts.delays = true;
  opts.ink = 1;
  opts.ink_refuse = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  storage->mkdir("/res");
  storage->mkdir("/res/splash");
  const auto write_picture = [storage](const char* path, const uint8_t* bytes, uint32_t len) {
    hal::FileSlot slot;
    if (!storage->open(path, hal::OpenMode::kWrite, slot)) return false;
    const bool ok = storage->write(slot, bytes, len) == static_cast<int32_t>(len);
    storage->close(slot);
    return ok;
  };
  TEST_ASSERT_TRUE(write_picture("/res/splash/boot.bmp", kTwoByTwoBmp, sizeof(kTwoByTwoBmp)));

  // **The glass is left with the ink of the boot moving**, which is the state the app found on the board.
  TEST_ASSERT_TRUE(kernel::splash::show(kernel::splash::kBoot));

  // **One page, one present, and then it waits**: the shape of `run images` and of half the disk, which is the one that
  // stayed with the boot on the glass.
  TEST_ASSERT_TRUE(add_app(storage, "once",
                           "{\"title\":\"Once\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "ui.clear(15)\n"
                           "ui.rect(20, 20, 200, 200, 0)\n"
                           "local ok, err = ui.present('gc16')\n"
                           "console.print('present ' .. tostring(ok) .. ' ' .. tostring(err and err.reason))\n"
                           "while true do sys.delay(20):await() end\n"));
  pump_captured(40, g_out, sizeof(g_out), "run once");
  // **The panel said no and it said why**: the page did not go up, and the glass still has the picture of the boot.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "present nil the previous paint did not finish"), g_out);
  emu::MemorySurface* glass = emu::memory_surface();
  TEST_ASSERT_NOT_NULL(glass);
  // The ink on, for the reason the splash case above gives (0.152.5): the refusal needs a second buffer to show.
  TEST_ASSERT_TRUE(glass->set_ink(true));
  const kernel::ui::Canvas& view = kernel::ui::canvas();
  // **Two buffers, two questions** (0.152.5): `level_at` is the page the system has drawn and `shown_at` is what the
  // glass shows, which with the ink on is the second one (`docs/85`). Before this fix the test asked `level_at` for
  // both and passed only because there was no ink model at all —the app's page is drawn there the moment it is drawn,
  // refused or not—.
  const auto drawn = [glass, &view](uint16_t x, uint16_t y) {
    return glass->level_at(static_cast<uint16_t>(view.x + x), static_cast<uint16_t>(view.y + y));
  };
  const auto on_the_glass = [glass, &view](uint16_t x, uint16_t y) {
    return glass->shown_at(static_cast<uint16_t>(view.x + x), static_cast<uint16_t>(view.y + y));
  };
  // The page was drawn and it did not go up: the glass still has the white of the boot splash, and the black of the
  // app's square is only in the page the system holds.
  TEST_ASSERT_EQUAL_UINT8(15, on_the_glass(60, 60));
  TEST_ASSERT_EQUAL_UINT8(0, drawn(60, 60));

  // **The system paints what it owes**: the ink of the boot ends, a turn asks the glass, and the square is there —with
  // nobody presenting again, which is the whole point (0.118.2).
  pump_turns(4000);
  TEST_ASSERT_EQUAL_UINT8(0, on_the_glass(60, 60));

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  storage->remove("/apps/once/app.lua");
  storage->remove("/apps/once/manifest.json");
  storage->remove("/apps/once");
  storage->remove("/res/splash/boot.bmp");

  // The next test gets the emulator it expects: no waiting, no refusals, no glass.
  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  opts.delays = false;
  opts.ink = 0;
  opts.ink_refuse = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
  // The second buffer goes with the ink, and the ink now stays off for every test that comes after.
  TEST_ASSERT_TRUE(emu::memory_surface()->set_ink(false));
}

// **The glass is one surface however many times it is asked** (0.118.9): `platform().display()` is the one
// door to the panel, and a board that built a new one per ask would power the rail and shake hands with the
// controller again in the middle of a page. On the Paper that same mistake has a second face, and it is the
// one that cost a night on the board: the CS of the panel belongs to the SPI driver from the moment the
// device is added, and the boot's own "park the pin" call (`deselect_panel`) ran **after** it —the boot asks
// for the capabilities right after the splash—, so the controller was never selected again: every read came
// back `ffff` and the glass kept the logo of the boot for ever while the apps ran and showed nothing
// (docs/50, docs/95). The emulator can measure the first half of that, and it is the half a board cannot say
// out loud: two asks, one surface.
void test_the_glass_is_one_surface_however_often_it_is_asked(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());

  hal::ISurface* once = hal::platform().display();
  TEST_ASSERT_NOT_NULL_MESSAGE(once, "the Paper has no glass at all");
  // **The same pointer**: a second surface is a second panel on a board, with its rail and its handshake.
  TEST_ASSERT_EQUAL_PTR_MESSAGE(once, hal::platform().display(),
                                "a second ask built the glass again");
  // And the size is still the one the controller answered, which is what a rebuilt surface would lose.
  TEST_ASSERT_EQUAL_UINT16(540, once->width());
  TEST_ASSERT_EQUAL_UINT16(960, once->height());

  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **A box that is read and not written keeps the caret and never brings a band** (0.116.0): `readonly` is the field of
// the control the system asks before it puts the keyboard up, and the block is the focus's —a page nobody types into is
// still a place a person is at—. Both kinds of box carry it, and entering one with the centre is the caret without the
// letters (docs/30, docs/92).
void test_a_read_only_box_has_a_caret_and_never_a_band(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "reader",
                           "{\"title\":\"Reader\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local kb = ui.keyboard()\n"
                           "local lines = ui.textbox_multiline(400, 200)\n"
                           "lines:place(10, 10)\n"
                           "lines.text = 'one\\ntwo'\n"
                           "lines.readonly = true\n"
                           "lines.on_activate = function(self)\n"
                           "  console.print('lines entered caret ' .. tostring(self.caret) ..\n"
                           "    ' band ' .. tostring(kb:visible()))\n"
                           "end\n"
                           "ui.focus(lines)\n"
                           "console.print('lines caret ' .. tostring(lines.caret) ..\n"
                           "  ' band ' .. tostring(kb:visible()))\n"
                           "local one = ui.textbox(400, 48)\n"
                           "one:place(10, 240)\n"
                           "one.text = 'abc'\n"
                           "one.readonly = true\n"
                           "ui.focus(one)\n"
                           "console.print('one caret ' .. tostring(one.caret) ..\n"
                           "  ' band ' .. tostring(kb:visible()))\n"
                           "ui.present('gc16')\n"
                           "while true do sys.delay(20):await() end\n"));

  pump_captured(20, g_out, sizeof(g_out), "run reader");
  // **The block is there and the band is not**, in both kinds of box, and it is still that way after the centre has
  // entered the box of lines: what the centre adds to a read-only page is the keys walking the caret (docs/30).
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "lines caret true band false"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "one caret true band false"), g_out);
  // The dial goes back to the box of lines —it is the stop before the field the app focused last— and the centre enters
  // it: the caret stays and the band does not come (docs/30).
  capture_begin();
  emu::press_key(2, true);
  pump_turns(10);
  emu::press_key(2, false);
  pump_turns(10);
  emu::press_button(true);
  pump_turns(10);
  emu::press_button(false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "lines entered caret true band false"), g_out);

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  storage->remove("/apps/reader/app.lua");
  storage->remove("/apps/reader/manifest.json");
  storage->remove("/apps/reader");
}

// **The keys walk the caret of a box the centre has entered** (0.116.0): down and up are one row of the glass —wrapped
// or not, which is why the arithmetic is the kernel's— and the app hears where the caret went so the line under the page
// says the row. Before the centre, the same keys walk the dial and the caret does not move: that is the whole point of
// entering (docs/30, docs/90, docs/92).
void test_the_keys_walk_the_rows_of_a_box_the_centre_entered(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "walker",
                           "{\"title\":\"Walker\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local box = ui.textbox_multiline(300, 200)\n"
                           "box:place(10, 10)\n"
                           "box.scale = 2\n"
                           "box.text = 'one\\ntwo\\nthree'\n"
                           "box.cursor = 0\n"
                           "box.readonly = true\n"
                           "box.on_move = function(self, cursor) console.print('cursor ' .. cursor) end\n"
                           "ui.focus(box)\n"
                           "ui.present('gc16')\n"
                           "while true do sys.delay(20):await() end\n"));

  pump_captured(20, g_out, sizeof(g_out), "run walker");
  // **The keys of the walk before the centre**: the caret does not move, because the box has not been entered and the
  // two keys belong to the dial (docs/30).
  capture_begin();
  emu::press_key(2, true);
  pump_turns(10);
  emu::press_key(2, false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "cursor "), g_out);

  // The centre enters the box, and then down is the second row and down again the third: "one\n" is four bytes, so the
  // caret of the start of the rows is 4 and 8 (docs/30).
  capture_begin();
  emu::press_button(true);
  pump_turns(10);
  emu::press_button(false);
  pump_turns(10);
  emu::press_key(2, true);
  pump_turns(10);
  emu::press_key(2, false);
  pump_turns(10);
  emu::press_key(2, true);
  pump_turns(10);
  emu::press_key(2, false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "cursor 4"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "cursor 8"), g_out);

  // And up goes back a row, which is what makes the two keys a pair and not one.
  capture_begin();
  emu::press_key(1, true);
  pump_turns(10);
  emu::press_key(1, false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "cursor 4"), g_out);

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  storage->remove("/apps/walker/app.lua");
  storage->remove("/apps/walker/manifest.json");
  storage->remove("/apps/walker");
}

// **The keys walk the letters of a box of one line the same way** (0.116.0): the same two keys, one letter a press, and
// a letter is a code point and not a byte (docs/30, docs/92).
void test_the_keys_walk_the_letters_of_a_line(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "letters",
                           "{\"title\":\"Letters\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local box = ui.textbox(300, 48)\n"
                           "box:place(10, 10)\n"
                           "box.text = 'abc'\n"
                           "box.cursor = 0\n"
                           "box.readonly = true\n"
                           "box.on_move = function(self, cursor) console.print('cursor ' .. cursor) end\n"
                           "ui.focus(box)\n"
                           "ui.present('gc16')\n"
                           "while true do sys.delay(20):await() end\n"));

  pump_captured(20, g_out, sizeof(g_out), "run letters");
  capture_begin();
  emu::press_button(true);
  pump_turns(10);
  emu::press_button(false);
  pump_turns(10);
  emu::press_key(2, true);
  pump_turns(10);
  emu::press_key(2, false);
  pump_turns(10);
  emu::press_key(2, true);
  pump_turns(10);
  emu::press_key(2, false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "cursor 1"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "cursor 2"), g_out);

  capture_begin();
  emu::press_key(1, true);
  pump_turns(10);
  emu::press_key(1, false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "cursor 1"), g_out);

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  storage->remove("/apps/letters/app.lua");
  storage->remove("/apps/letters/manifest.json");
  storage->remove("/apps/letters");
}

// **A box without a frame is a box whose frame is the colour of its inside** (0.116.0): the control paints its frame in
// `border` and its inside in `background`, and `framed = false` hands the second as the first. A document that is the
// page has nothing to be framed by, which is what the owner asked for with the editor in his hand (docs/30, docs/92).
void test_a_box_without_a_frame_leaves_its_edge_in_paper(void) {
  TEST_ASSERT_TRUE(boot_once());
  // **The glass on**: the four edges are read from the panel and an emulator born with the panel off paints nothing
  // (0.118.0, found by running it, docs/85).
  emu::Options glass_opts = emu::options();
  glass_opts.board = "m5stack-paper";
  glass_opts.display = true;
  emu::set_options(glass_opts);
  kernel::boot(hal::platform());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "frames",
                           "{\"title\":\"Frames\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "ui.clear(15)\n"
                           "local framed = ui.textbox(200, 48)\n"
                           "framed:place(20, 20)\n"
                           "framed.text = ''\n"
                           "local bare = ui.textbox(200, 48)\n"
                           "bare:place(20, 90)\n"
                           "bare.text = ''\n"
                           "bare.framed = false\n"
                           "local framed_lines = ui.textbox_multiline(200, 100)\n"
                           "framed_lines:place(300, 20)\n"
                           "framed_lines.text = 'x'\n"
                           "local bare_lines = ui.textbox_multiline(200, 100)\n"
                           "bare_lines:place(300, 140)\n"
                           "bare_lines.text = 'x'\n"
                           "bare_lines.framed = false\n"
                           "ui.present('gc16')\n"
                           "while true do sys.delay(20):await() end\n"));

  pump_captured(20, g_out, sizeof(g_out), "run frames");
  pump_turns(20);
  emu::MemorySurface* glass = emu::memory_surface();
  TEST_ASSERT_NOT_NULL(glass);
  const kernel::ui::Canvas& view = kernel::ui::canvas();
  // **The top edge of each box, in the middle of it**: the frame of the control is two pixels of ink there, and a box
  // that has no frame leaves the paper of the page (docs/30).
  const auto edge = [glass, &view](uint16_t x, uint16_t y) {
    return glass->level_at(static_cast<uint16_t>(view.x + x), static_cast<uint16_t>(view.y + y));
  };
  TEST_ASSERT_EQUAL_UINT8(0, edge(120, 20));
  TEST_ASSERT_EQUAL_UINT8(15, edge(120, 90));
  TEST_ASSERT_EQUAL_UINT8(0, edge(400, 20));
  TEST_ASSERT_EQUAL_UINT8(15, edge(400, 140));

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  storage->remove("/apps/frames/app.lua");
  storage->remove("/apps/frames/manifest.json");
  storage->remove("/apps/frames");
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
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "band 256"), g_out);
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
  // **The third letter waits out the window of the way out** (0.167.0): three presses of the centre inside a second and a
  // half are the system's —the page with the ring gives a view back, the only page closes the app— and this case types
  // three letters with the same key, so the reading pause is what keeps the third one a letter (docs/30, docs/92).
  pump_turns(1600);
  emu::press_button(true);
  pump_turns(20);
  emu::press_button(false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  // Four rows of a key of sixty-four pixels in a view of the Paper: the band is two hundred and fifty-six.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "typed q at 1"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "typed qq at 2"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "typed qqw at 3"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "word qqw"), g_out);
  TEST_ASSERT_FALSE_MESSAGE(kernel::app_running(), "the app did not end after its three keys");

  storage->remove("/apps/typer/app.lua");
  storage->remove("/apps/typer/manifest.json");
  storage->remove("/apps/typer");
}

// **The band of the keyboard comes out of the view the app is given** (0.84.0): `ui.caps().height` is six hundred and
// fifty-six with the keyboard up on the Paper and nine hundred and twelve without it, `kb:height()` still says the band,
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
  // With the band up, the four rows of sixty-four are the system's and the app has six hundred and fifty-six.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "band 256, view 656"), g_out);
  // And the band going gives them all back, without a word from the app: the hook was not defined yet at that point.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "gone, view 912"), g_out);
  // The show that comes after it finds the hook and tells it the view is another one.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "resize 540x656"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "again, view 656"), g_out);
  TEST_ASSERT_FALSE_MESSAGE(kernel::app_running(), "the app did not end after its keys");

  storage->remove("/apps/resizer/app.lua");
  storage->remove("/apps/resizer/manifest.json");
  storage->remove("/apps/resizer");
}


// **The keyboard follows the box the finger lands on** (0.91.0): a box that takes keys brings the band and becomes its
// target; a box of the same alphabet only moves the target; a button takes the band away, and a finger on the empty page
// ends the edition. The box has the focus and the keyboard is its, which is the rule the app used to write by hand with
// `kb:show()` (docs/30, docs/90).
void test_the_keyboard_follows_the_box_that_takes_the_focus(void) {
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
  TEST_ASSERT_TRUE(add_app(storage, "focus",
                           "{\"title\":\"Focus\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local kb = ui.keyboard()\n"
                           "local one = ui.textbox(200, 48)\n"
                           "one:place(20, 60)\n"
                           "one.scale = 2\n"
                           "one.on_focus = function(self, on) console.print('one focus ' .. tostring(on)) end\n"
                           "one.on_change = function(self, text) console.print('one ' .. text) end\n"
                           "local two = ui.textbox(200, 48)\n"
                           "two:place(20, 130)\n"
                           "two.scale = 2\n"
                           "two.on_focus = function(self, on) console.print('two focus ' .. tostring(on)) end\n"
                           "two.on_change = function(self, text) console.print('two ' .. text) end\n"
                           "local button = ui.button(200, 48)\n"
                           "button:place(20, 210)\n"
                           "button.on_focus = function(self, on) console.print('button focus ' .. tostring(on)) end\n"
                           "button.on_click = function(self) console.print('click button') end\n"
                           "function on_resize(w, h)\n"
                           "  console.print('resize ' .. w .. 'x' .. h .. ' kb ' .. tostring(kb:visible()))\n"
                           "end\n"
                           "console.print('before ' .. tostring(kb:visible()))\n"
                           "ui.present('gc16')\n"
                           "while true do sys.delay(20):await() end\n"));

  pump_captured(20, g_out, sizeof(g_out), "run focus");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "before false"), g_out);

  emu::Gt911Panel* panel = emu::touch_panel();
  TEST_ASSERT_NOT_NULL(panel);
  const kernel::ui::Canvas& view = kernel::ui::canvas();

  // The first box takes the finger: the band comes and the view is the part over it.
  capture_begin();
  TEST_ASSERT_TRUE(panel->finger_down(0, static_cast<uint16_t>(view.x + 30),
                                      static_cast<uint16_t>(view.y + 80)));
  pump_turns(10);
  TEST_ASSERT_TRUE(panel->finger_up(0));
  pump_turns(60);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "resize 540x656 kb true"), g_out);

  // The centre presses the key the keyboard is on: a `q` goes into the box that took the finger.
  capture_begin();
  emu::press_button(true);
  pump_turns(10);
  emu::press_button(false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "one q"), g_out);

  // A second box of the same alphabet takes the target without taking the band down: the next key is a `w` because the
  // down key of the dial walked the keyboard one key.
  capture_begin();
  TEST_ASSERT_TRUE(panel->finger_down(0, static_cast<uint16_t>(view.x + 30),
                                      static_cast<uint16_t>(view.y + 150)));
  pump_turns(10);
  TEST_ASSERT_TRUE(panel->finger_up(0));
  pump_turns(60);
  emu::press_key(2, true);
  pump_turns(10);
  emu::press_key(2, false);
  pump_turns(10);
  emu::press_button(true);
  pump_turns(10);
  emu::press_button(false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "resize 540x912"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "two focus true"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "two w"), g_out);

  // A button is not a box: the band goes and the view grows before the release clicks it.
  // **Two taps are a `double` and three a `triple`** (0.136.0), and this case taps the glass four times: the window of the
  // gesture is let past here so that every tap arrives as a tap of a control (docs/30, docs/92).
  pump_turns(400);
  capture_begin();
  TEST_ASSERT_TRUE(panel->finger_down(0, static_cast<uint16_t>(view.x + 30),
                                      static_cast<uint16_t>(view.y + 230)));
  pump_turns(10);
  TEST_ASSERT_TRUE(panel->finger_up(0));
  pump_turns(60);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "resize 540x912 kb false"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "button focus true"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "click button"), g_out);

  // The first box again: a finger on it brings the keyboard back.
  pump_turns(400);
  capture_begin();
  TEST_ASSERT_TRUE(panel->finger_down(0, static_cast<uint16_t>(view.x + 30),
                                      static_cast<uint16_t>(view.y + 80)));
  pump_turns(10);
  TEST_ASSERT_TRUE(panel->finger_up(0));
  pump_turns(60);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "resize 540x656 kb true"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "one focus true"), g_out);

  // And the empty page ends the edition: no ring and no keyboard.
  pump_turns(400);
  capture_begin();
  TEST_ASSERT_TRUE(panel->finger_down(0, static_cast<uint16_t>(view.x + 400),
                                      static_cast<uint16_t>(view.y + 400)));
  pump_turns(10);
  TEST_ASSERT_TRUE(panel->finger_up(0));
  pump_turns(60);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "resize 540x912 kb false"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "one focus false"), g_out);

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());

  storage->remove("/apps/focus/app.lua");
  storage->remove("/apps/focus/manifest.json");
  storage->remove("/apps/focus");

  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  opts.touch = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **A box that takes no keys is a sheet to scroll** (0.91.0): the field `keyboard = false` keeps the finger and the
// caret and leaves the band closed; a read-only page has somewhere to put the thumb and nothing to type.
void test_a_box_that_takes_no_keys_does_not_open_the_keyboard(void) {
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
  TEST_ASSERT_TRUE(add_app(storage, "sheet",
                           "{\"title\":\"Sheet\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local kb = ui.keyboard()\n"
                           "local box = ui.textbox(200, 48)\n"
                           "box:place(20, 60)\n"
                           "box.keyboard = false\n"
                           "box.on_focus = function(self, on)\n"
                           "  console.print('focus ' .. tostring(on) .. ' kb ' .. tostring(kb:visible()))\n"
                           "end\n"
                           "function on_resize(w, h)\n"
                           "  console.print('resize kb ' .. tostring(kb:visible()))\n"
                           "end\n"
                           "kb.target = box\n"
                           "local ok, err = kb:show('alpha')\n"
                           "console.print('show ' .. tostring(ok) .. ' ' .. tostring(err and err.code) ..\n"
                           "  ' kb ' .. tostring(kb:visible()))\n"
                           "console.print('before ' .. tostring(kb:visible()))\n"
                           "ui.present('gc16')\n"
                           "while true do sys.delay(20):await() end\n"));

  pump_captured(20, g_out, sizeof(g_out), "run sheet");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "show nil E_INVAL kb false"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "before false"), g_out);

  emu::Gt911Panel* panel = emu::touch_panel();
  TEST_ASSERT_NOT_NULL(panel);
  const kernel::ui::Canvas& view = kernel::ui::canvas();

  // The box is the first control and it has the focus from its birth; a finger outside ends it, and the page says so.
  capture_begin();
  TEST_ASSERT_TRUE(panel->finger_down(0, static_cast<uint16_t>(view.x + 400),
                                      static_cast<uint16_t>(view.y + 400)));
  pump_turns(10);
  TEST_ASSERT_TRUE(panel->finger_up(0));
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "focus false kb false"), g_out);

  // A finger on the box takes the focus, and the box says it takes no keys: the band does not come.
  capture_begin();
  TEST_ASSERT_TRUE(panel->finger_down(0, static_cast<uint16_t>(view.x + 30),
                                      static_cast<uint16_t>(view.y + 80)));
  pump_turns(10);
  TEST_ASSERT_TRUE(panel->finger_up(0));
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "focus true kb false"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "resize"), g_out);

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());

  storage->remove("/apps/sheet/app.lua");
  storage->remove("/apps/sheet/manifest.json");
  storage->remove("/apps/sheet");

  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  opts.touch = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **A keyboard with no box is not a keyboard** (0.91.0): `kb:show()` refuses it with `E_INVAL` instead of painting a
// band that writes nowhere. The target is the control that needs the keys, and an app that has none has no keyboard.
void test_a_keyboard_without_a_target_is_refused(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "nowhere",
                           "{\"title\":\"Nowhere\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local kb = ui.keyboard()\n"
                           "local ok, err = kb:show('alpha')\n"
                           "console.print('show ' .. tostring(ok) .. ' ' .. tostring(err and err.code) ..\n"
                           "  ' kb ' .. tostring(kb:visible()))\n"
                           "while true do sys.delay(20):await() end\n"));

  pump_captured(20, g_out, sizeof(g_out), "run nowhere");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "show nil E_INVAL kb false"), g_out);

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());

  storage->remove("/apps/nowhere/app.lua");
  storage->remove("/apps/nowhere/manifest.json");
  storage->remove("/apps/nowhere");
}

// **The keyboard is chosen in the settings and the choice writes the letters of that keyboard** (0.93.0): `cfg keyboard
// azerty` selects the child, persists its id and a boot reads it back; the other child is qwerty, and a name that is not
// one of them is refused with the list instead of silently keeping the old one (docs/30, docs/70, docs/90).
void test_the_keyboard_is_chosen_in_cfg_and_writes_its_letters(void) {
  TEST_ASSERT_TRUE(boot_once());

  pump_captured(20, g_out, sizeof(g_out), "cfg keyboard azerty");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "settings: keyboard azerty"), g_out);
  TEST_ASSERT_EQUAL_STRING("azerty", kernel::cfg::keyboard());

  // A boot reads the setting: the Paper is a boot, and the UI selects what the door saved.
  use_board("m5stack-paper");
  TEST_ASSERT_EQUAL_STRING("azerty", kernel::ui::keyboard_current());
  TEST_ASSERT_EQUAL_STRING("azerty", kernel::cfg::keyboard());

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "aztype",
                           "{\"title\":\"Az type\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local box = ui.textbox(300, 48)\n"
                           "box:place(20, 60)\n"
                           "local kb = ui.keyboard()\n"
                           "kb.target = box\n"
                           "kb:show('alpha')\n"
                           "local done = false\n"
                           "box.on_change = function(self, text)\n"
                           "  console.print('typed ' .. text)\n"
                           "  done = true\n"
                           "end\n"
                           "ui.present('gc16')\n"
                           "while not done do sys.delay(50):await() end\n"));

  pump_captured(20, g_out, sizeof(g_out), "run aztype");
  capture_begin();
  emu::press_button(true);
  pump_turns(10);
  emu::press_button(false);
  pump_turns(40);
  capture_end(g_out, sizeof(g_out));
  // The first key of the first row of AZERTY is an `a`; a QWERTY would have typed a `q` here.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "typed a"), g_out);
  TEST_ASSERT_FALSE_MESSAGE(kernel::app_running(), "the app did not end after its key");

  storage->remove("/apps/aztype/app.lua");
  storage->remove("/apps/aztype/manifest.json");
  storage->remove("/apps/aztype");

  // The prompt says the keyboard and the list; a name outside the list is refused and the old one stays.
  pump_captured(20, g_out, sizeof(g_out), "cfg");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "keyboard: azerty"), g_out);
  pump_captured(20, g_out, sizeof(g_out), "cfg keyboard dvorak");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "cfg: no keyboard 'dvorak'"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "cfg: keyboards are qwerty azerty"), g_out);
  TEST_ASSERT_EQUAL_STRING("azerty", kernel::ui::keyboard_current());
  TEST_ASSERT_EQUAL_STRING("azerty", kernel::cfg::keyboard());

  // The settings go back to the default, because they are a setting of the run.
  pump_captured(20, g_out, sizeof(g_out), "cfg keyboard qwerty");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "settings: keyboard qwerty"), g_out);
  TEST_ASSERT_EQUAL_STRING("qwerty", kernel::ui::keyboard_current());
  TEST_ASSERT_EQUAL_STRING("qwerty", kernel::cfg::keyboard());
}

// **The app the settings call home comes up with the board** (0.119.0): `cfg home <id>` keeps the name in the board's
// own store, a boot reads it and starts that app instead of leaving a person at the prompt —which is what every board
// did until now— and `cfg home none` puts the prompt back. The default of a board comes from the build
// (`LUNOK_HOME_APP` in `platformio.ini`: empty in the environments of this suite, the launcher on the Paper and on the
// emulator since 0.155.0), so what this measures is the setting and the boot that honours it (docs/30, docs/40).
void test_the_home_app_comes_up_with_the_board(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "comer",
                           "{\"title\":\"Comer\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "console.print('home here')\n"
                           "while true do sys.delay(20):await() end\n"));

  // The setting says what it kept, and it says the default of the build next to it: a person has to know whether
  // they are looking at their choice or at the board's.
  pump_captured(20, g_out, sizeof(g_out), "cfg home comer");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "settings: home comer"), g_out);
  TEST_ASSERT_EQUAL_STRING("comer", kernel::cfg::home_app());
  pump_captured(20, g_out, sizeof(g_out), "cfg home");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "settings: home comer"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "this board's default is none"), g_out);

  // **The boot starts it**: nothing is in front, so the home app is the one that comes up, and the prompt is not a
  // place the board stopped at. **The capture wraps the boot and not the turns after it** (0.152.5): the source of an
  // app is read and run inside `run_app`, so its first line is already on the console when `kernel::boot` returns and
  // a capture opened after it sees nothing.
  capture_begin();
  kernel::boot(hal::platform());
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), "the home app did not come up with the board");
  TEST_ASSERT_EQUAL_STRING("comer", kernel::shell::app_id());
  pump_turns(30);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "home here"), g_out);

  // And `none` is the prompt again, on the next boot.
  close_any_app();
  pump_captured(20, g_out, sizeof(g_out), "cfg home none");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "settings: home none"), g_out);
  TEST_ASSERT_EQUAL_STRING("", kernel::cfg::home_app());
  kernel::boot(hal::platform());
  TEST_ASSERT_FALSE_MESSAGE(kernel::app_running(), "the board came up inside an app after `cfg home none`");

  storage->remove("/apps/comer/app.lua");
  storage->remove("/apps/comer/manifest.json");
  storage->remove("/apps/comer");
}

// **A name the door does not present is refused where it is typed** (0.119.0), and a name that is on the store but not
// on the disk is a line in the log and the prompt: a board that insisted would be a boot loop nobody asked for —the
// same rule the session record follows (docs/30, docs/40, docs/90).
void test_a_home_app_that_is_not_there_leaves_the_prompt(void) {
  TEST_ASSERT_TRUE(boot_once());

  pump_captured(20, g_out, sizeof(g_out), "cfg home ghost");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "cfg: no app 'ghost'"), g_out);
  TEST_ASSERT_EQUAL_STRING("", kernel::cfg::home_app());

  // Written straight into the setting —which is what a disk that lost the app looks like on the next boot— and the
  // board still comes up: with the prompt, and able to answer.
  kernel::cfg::set_home_app("ghost");
  kernel::boot(hal::platform());
  TEST_ASSERT_FALSE_MESSAGE(kernel::app_running(), "the board came up inside an app that is not there");
  pump_captured(20, g_out, sizeof(g_out), "cfg home");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "settings: home ghost"), g_out);
  kernel::cfg::set_home_app(nullptr);
}

// **The block of the caret blinks without the app** (0.92.0): the kernel turns `box.caret` every six hundred milliseconds
// while the keyboard is on the glass and paints the box; a page that only waits has a cursor, and a page that types does
// not keep a clock for it. The test reads the field as the app would and sees the two halves of a blink (docs/30,
// docs/90).
void test_the_caret_blinks_without_the_app(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "blink",
                           "{\"title\":\"Blink\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local box = ui.textbox(200, 48)\n"
                           "box:place(20, 60)\n"
                           "box.scale = 2\n"
                           "local kb = ui.keyboard()\n"
                           "kb.target = box\n"
                           "kb:show('alpha')\n"
                           "ui.present('gc16')\n"
                           "console.print('caret start ' .. tostring(box.caret))\n"
                           "local last = box.caret\n"
                           "local changes = 0\n"
                           "local waited = 0\n"
                           "while changes < 2 and waited < 3000 do\n"
                           "  sys.delay(100):await()\n"
                           "  waited = waited + 100\n"
                           "  if box.caret ~= last then\n"
                           "    changes = changes + 1\n"
                           "    last = box.caret\n"
                           "    console.print('caret ' .. changes .. ' ' .. tostring(last))\n"
                           "  end\n"
                           "end\n"
                           "console.print('caret changes ' .. changes)\n"));

  pump_captured(2000, g_out, sizeof(g_out), "run blink");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "caret start true"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "caret 1 false"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "caret 2 true"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "caret changes 2"), g_out);
  TEST_ASSERT_FALSE_MESSAGE(kernel::app_running(), "the app did not end after its two halves");

  storage->remove("/apps/blink/app.lua");
  storage->remove("/apps/blink/manifest.json");
  storage->remove("/apps/blink");
}

// **The dial stops on a text box and the centre enters it** (0.116.0, and it was the finger's only since 0.85.2): a board
// with keys and no touch has to be able to land on a field, and the ring on a field is the caret in it and no band —the
// band is what entering buys—. This is the shape a slider has with its value, which is what the owner asked for: focus,
// then the centre to get in (docs/30, docs/90, docs/92).
void test_the_dial_stops_on_a_text_box_and_the_centre_enters_it(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "dialbox",
                           "{\"title\":\"DialBox\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local kb = ui.keyboard()\n"
                           "local button = ui.button(200, 60)\n"
                           "button:place(20, 20)\n"
                           "button.on_click = function(self) console.print('click button') end\n"
                           "local box = ui.textbox(200, 48)\n"
                           "box:place(20, 140)\n"
                           // **The band is not up when the ring lands on the field** and it is when the centre gets in:
                           // the two numbers the page cannot see are printed by the app that can ask (docs/30).
                           "box.on_focus = function(self, on)\n"
                           "  console.print('focus box ' .. tostring(on) .. ' band ' .. tostring(kb:visible()))\n"
                           "end\n"
                           "box.on_activate = function(self)\n"
                           "  console.print('enter box band ' .. tostring(kb:visible()) .. ' editing ' .. tostring(self.editing))\n"
                           "end\n"
                           // **The band is the keyboard's while it is up**: a letter typed into the box is what gives the
                           // keys back, and hiding the band from the app is the way a page says it has finished typing
                           // (0.83.0, 0.91.0, docs/30).
                           "box.on_change = function(self, text)\n"
                           "  console.print('typed ' .. text)\n"
                           "  kb:hide()\n"
                           "end\n"
                           "button.on_focus = function(self, on) console.print('focus button ' .. tostring(on)) end\n"
                           "ui.present('gc16')\n"
                           "while true do sys.delay(20):await() end\n"));

  pump_captured(20, g_out, sizeof(g_out), "run dialbox");
  // The down key walks the dial from the button to the box: the field has the ring and no band.
  capture_begin();
  emu::press_key(2, true);
  pump_turns(10);
  emu::press_key(2, false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "focus box true band false"), g_out);
  // **The centre is the door**: the box is entered and the band comes with it, and the app hears `on_activate`.
  capture_begin();
  emu::press_button(true);
  pump_turns(10);
  emu::press_button(false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "enter box band true"), g_out);
  // And the button is still reachable: **the band is the keyboard's while it is up**, so the centre of this window types
  // a letter into the field —the same key that entered it— and the app hides the band when it is typed. **The centre is
  // what leaves the field** (`editing` off), and only then do the arrows belong to the walk again: the up key walks the
  // dial to the button and its centre clicks it, which is what says the field did not eat the walk (docs/30, docs/92).
  capture_begin();
  emu::press_button(true);
  pump_turns(20);
  emu::press_button(false);
  pump_turns(20);
  // **The third press of the centre waits out the system's window** (0.167.0): three of them inside a second and a half are
  // the way out of the page —a view back, or the app closed when it is the only one— and this case presses the centre three
  // times to say three things to the app (docs/30, docs/92).
  pump_turns(1600);
  emu::press_button(true);
  pump_turns(20);
  emu::press_button(false);
  pump_turns(20);
  emu::press_key(1, true);
  pump_turns(10);
  emu::press_key(1, false);
  pump_turns(20);
  emu::press_button(true);
  pump_turns(10);
  emu::press_button(false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "typed "), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "editing false"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "click button"), g_out);

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());

  storage->remove("/apps/dialbox/app.lua");
  storage->remove("/apps/dialbox/manifest.json");
  storage->remove("/apps/dialbox");
}

// **A slider is entered with the centre before its keys are its own** (0.117.0): the owner with the board in his hand,
// *«Al rebre el focus, els botons continuarán com sempre (canviant focus) s'ha de polsar el botó central per "entrar" al
// slider i llavors si escollir el valor amb els mateixos dos botons»*. Outside, the two keys walk the dial as always;
// inside, they are the number; and the centre gives them back and means "leave" and nothing else (docs/30, docs/92).
void test_a_slider_takes_the_keys_only_after_the_centre(void) {
  TEST_ASSERT_TRUE(boot_once());
  use_board("m5stack-paper");
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "knob",
                           "{\"title\":\"Knob\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local slider = ui.slider(400, 60)\n"
                           "slider:place(20, 60)\n"
                           "slider.min = 0\n"
                           "slider.max = 10\n"
                           "slider.step = 1\n"
                           "slider.value = 5\n"
                           "slider.on_change = function(self, value) console.print('value ' .. value) end\n"
                           "slider.on_click = function(self)\n"
                           "  console.print('click slider ' .. tostring(self.editing))\n"
                           "end\n"
                           "ui.focus(slider)\n"
                           "ui.present('gc16')\n"
                           "while true do sys.delay(20):await() end\n"));

  pump_captured(20, g_out, sizeof(g_out), "run knob");

  // **The keys walk and the number does not move**: the slider has the ring and it is not entered, so down is the walk
  // of the dial —which, with one stop, comes back to the same control— and not the value (docs/30).
  capture_begin();
  emu::press_key(2, true);
  pump_turns(10);
  emu::press_key(2, false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "value "), g_out);

  // The centre enters it: the app hears the press and `editing` is already true.
  capture_begin();
  emu::press_button(true);
  pump_turns(10);
  emu::press_button(false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "click slider true"), g_out);

  // **Inside, the two keys are the number**: down twice is six and then seven, and the app hears each one.
  capture_begin();
  for (int i = 0; i < 2; ++i) {
    emu::press_key(2, true);
    pump_turns(10);
    emu::press_key(2, false);
    pump_turns(20);
  }
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "value 6"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "value 7"), g_out);

  // The centre gives the keys back to the walk, and the same key that moved the number changes nothing now.
  capture_begin();
  emu::press_button(true);
  pump_turns(10);
  emu::press_button(false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "click slider false"), g_out);
  capture_begin();
  emu::press_key(2, true);
  pump_turns(10);
  emu::press_key(2, false);
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "value "), g_out);

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  storage->remove("/apps/knob/app.lua");
  storage->remove("/apps/knob/manifest.json");
  storage->remove("/apps/knob");
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

// **A card that goes away takes its ink with it** (0.173.2, the owner: *«sembla que els modals al tancar-se no netegen
// correctament la pantalla»*). A card has no buffer of its own: it paints into the pixels of the page, so nothing keeps a
// copy of what was underneath and a release that only marks the box leaves the frame and the words on the glass for ever.
// The system fills the hole with the paper of the page and asks every control that was inside it to paint itself again —
// with a buffer or without one—, and this case counts the pixels of the glass before the card and after it: the page of a
// label and a button comes back **the same**, and there is no ink of the card left anywhere in its box (docs/30, docs/90).
int ink_in(emu::MemorySurface* glass, const kernel::ui::Canvas& view, int x0, int y0, int x1, int y1) {
  int inked = 0;
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      if (x < 0 || y < 0) continue;
      if (glass->level_at(static_cast<uint16_t>(view.x + x), static_cast<uint16_t>(view.y + y)) == 0) ++inked;
    }
  }
  return inked;
}

void test_a_card_that_goes_away_takes_its_ink_with_it(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(copy_a_face(storage, "dejavusans-bold.ttf"));
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());
  emu::MemorySurface* glass = emu::memory_surface();
  TEST_ASSERT_NOT_NULL(glass);

  // The page of the app is a label and a button —the shape of `wipe`, which is what the owner saw— and the card is put
  // **over the two of them**, so what comes back is not empty paper but the page.
  TEST_ASSERT_TRUE(add_app(storage, "under",
                           "{\"title\":\"Under\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local head = ui.label(300, 50)\n"
                           "head.text = 'THE PAGE BEHIND'\n"
                           "head.scale = 5\n"
                           "head:place(20, 20)\n"
                           // **A dark square where the card's top-right corner will be** (0.173.2): outside every control, so
                           // the card's rounded corner is the only thing that can keep it or erase it. A card paints a rounded
                           // box and its corners are the page's, so the hole is filled with the same radius —a rectangle would
                           // take these pixels of the page that the card never touched— (docs/92).
                           "ui.rect(430, 60, 40, 40, 0)\n"
                           "local door = ui.button(300, 100)\n"
                           "door:place(20, 100)\n"
                           "local words = ui.label(260, 80)\n"
                           "words.text = 'A door of the page'\n"
                           "words.scale = 4\n"
                           "words.align = 'center'\n"
                           "words.valign = 'middle'\n"
                           "door:add(words)\n"
                           "ui.present('gc16')\n"
                           "sys.delay(100):await()\n"
                           "console.print('page painted')\n"
                           "sys.delay(600):await()\n"
                           "local card = ui.dialog(420, 260)\n"
                           "card:place(40, 60)\n"
                           "card.title = 'A question'\n"
                           "card.text = 'This card covers the label and the door'\n"
                           "card.accept = 'Yes'\n"
                           "card.cancel = 'No'\n"
                           "ui.present('gc16')\n"
                           "sys.delay(300):await()\n"
                           "card:release()\n"
                           "console.print('card closed')\n"
                           "ui.present('gc16')\n"
                           "sys.delay(4000):await()\n"));

  // **The page before the card**: the ink of the label and of the door, counted in the box the card will cover. The run
  // waits for the two lines of the app, so the glass has the page when the first count is taken (docs/85).
  pump_captured(500, g_out, sizeof(g_out), "run under");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "page painted"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "card closed"), "the card went before this case could look at the page");
  const kernel::ui::Canvas& view = kernel::ui::canvas();
  // **Counted from where the dark square ends**, because a primitive under a card is the app's to paint again: the system
  // fills the hole with paper and asks the **controls** to draw themselves, and a `ui.rect` is not a control (docs/30).
  const int whole_page = ink_in(glass, view, 50, 25, 320, 200);
  TEST_ASSERT_GREATER_THAN_MESSAGE(2000, whole_page, "the page of the app has no ink to come back");

  // **And after the card went**: the same box, the same ink. A ghost of the frame or of the two doors would add pixels
  // and the card's title over the label would take them away, so the equality is what says the page came back whole.
  capture_begin();
  pump_turns(900);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "card closed"), g_out);
  pump_turns(100);
  const int page_again = ink_in(glass, view, 50, 25, 320, 200);
  TEST_ASSERT_EQUAL_INT_MESSAGE(whole_page, page_again, "the card left ink on the glass");
  // **And a primitive under the card goes with it, which is the rule for a page with no draw of its own** (docs/30): this
  // page is painted with `ui.rect`/`ui.text` and has no `on_draw`, so what the system can paint again is a control and a
  // primitive is not one. An app that wants its primitives back gives its view an `on_draw`, which is the case below. The
  // pixel is asserted here so that the day this changes, it changes on purpose.
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      15, glass->level_at(static_cast<uint16_t>(view.x + 440), static_cast<uint16_t>(view.y + 80)),
      "a primitive under a card came back from nowhere");
  // **And the corner of the card is the page's**: the dark square the app painted under the card's top-left corner is still
  // ink where the card's radius does not reach it —one pixel in, at the very corner—, which is what says the hole was filled
  // with the shape of the card and not with its bounding box (0.173.2, docs/92).
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      0, glass->level_at(static_cast<uint16_t>(view.x + 458), static_cast<uint16_t>(view.y + 61)),
      "the fill of the hole erased a corner of the page that the card never touched");

  // **An app left running is the next case's `cannot start the interpreter`** (0.151.0): this one waits for ever, so it is
  // closed the way the shell closes it before the board goes back to the model (docs/85).
  if (kernel::app_running()) {
    kernel::shell::close_app();
    pump_turns(10);
  }
  storage->remove("/apps/under/app.lua");
  storage->remove("/apps/under/manifest.json");
  storage->remove("/apps/under");
  kernel::font::set_system_face(nullptr);
  storage->remove("/res/fonts/dejavusans-bold.ttf");
  emu::Options after = emu::options();
  after.board = nullptr;
  after.display = false;
  emu::set_options(after);
  kernel::boot(hal::platform());
}

// **The page that paints itself paints its own hole** (0.173.3, the owner: *«l'app hauria de repintar la seva area dañada,
// no?»*). The page of the case above has no draw of its own, so a primitive under the card is lost —the system can only
// paint paper and ask the controls—. An app whose view has an `on_draw` is the other way round: the release leaves the box
// owed to the page, `ui.present` calls the draw of the view **cut at that box**, and the dark square of the app comes back
// with its ink and not as paper. The page is one primitive and nothing else, so there is no control to ask and the only
// thing that can bring it back is the app's own draw (docs/30, docs/90). **What the flag saves is the wash**, and that is
// pinned in `test_ui` (`test_a_page_that_paints_itself_keeps_the_hole_for_the_app`): an app that paints its whole hole
// would paint over the paper anyway, so the difference at the glass is the extra `gl16` pass and not the picture.
void test_a_page_that_paints_itself_paints_its_own_hole(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());
  emu::MemorySurface* glass = emu::memory_surface();
  TEST_ASSERT_NOT_NULL(glass);

  // **The whole page is one primitive of the app**, which is the shape of a page the system cannot paint again: the dark
  // square covers the box the card will take, so the only way it comes back is the app's own draw.
  TEST_ASSERT_TRUE(add_app(storage, "painter",
                           "{\"title\":\"Painter\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local root = ui.root()\n"
                           "local drew = 0\n"
                           "root.on_draw = function(self)\n"
                           "  drew = drew + 1\n"
                           "  self:clear(15)\n"
                           "  self:rect(20, 20, 320, 260, 0)\n"
                           "end\n"
                           "root:invalidate()\n"
                           "ui.present('gc16')\n"
                           "sys.delay(100):await()\n"
                           "console.print('page painted ' .. drew)\n"
                           "sys.delay(600):await()\n"
                           "local card = ui.dialog(260, 180)\n"
                           "card:place(50, 60)\n"
                           "card.title = 'A question'\n"
                           "card.text = 'Over the dark square'\n"
                           "card.accept = 'Yes'\n"
                           "card.cancel = 'No'\n"
                           "ui.present('gc16')\n"
                           "sys.delay(300):await()\n"
                           "card:release()\n"
                           "console.print('card closed ' .. drew)\n"
                           "ui.present('gc16')\n"
                           "sys.delay(4000):await()\n"));

  pump_captured(500, g_out, sizeof(g_out), "run painter");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "page painted 1"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "card closed"), "the card went before this case could look at the page");
  const kernel::ui::Canvas& view = kernel::ui::canvas();
  // **The ink of the page inside the box the card covers**, which is the dark square and only the dark square.
  const int under_the_card = ink_in(glass, view, 60, 70, 300, 230);
  TEST_ASSERT_GREATER_THAN_MESSAGE(20000, under_the_card, "the dark square of the app is not there to come back");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      0, glass->level_at(static_cast<uint16_t>(view.x + 160), static_cast<uint16_t>(view.y + 150)),
      "the card did not cover the dark square");

  capture_begin();
  pump_turns(900);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "card closed"), g_out);
  pump_turns(100);
  // **The same ink in the same box**: the hole was left to the app —the system does not paint paper over a page that has a
  // draw of its own— and the draw of the view ran again cut at it, which is what the count of the draws says too.
  const int back = ink_in(glass, view, 60, 70, 300, 230);
  TEST_ASSERT_EQUAL_INT_MESSAGE(under_the_card, back, "the hole of the card was not painted by the app");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      0, glass->level_at(static_cast<uint16_t>(view.x + 160), static_cast<uint16_t>(view.y + 150)),
      "the middle of the hole came back as paper and not as the page of the app");
  int painted_after = 0;
  TEST_ASSERT_TRUE_MESSAGE(sscanf(strstr(g_out, "card closed ") + 12, "%d", &painted_after) == 1, g_out);
  TEST_ASSERT_GREATER_THAN_MESSAGE(1, painted_after, "the draw of the view did not run again when the card went");

  if (kernel::app_running()) {
    kernel::shell::close_app();
    pump_turns(10);
  }
  storage->remove("/apps/painter/app.lua");
  storage->remove("/apps/painter/manifest.json");
  storage->remove("/apps/painter");
  emu::Options after = emu::options();
  after.board = nullptr;
  after.display = false;
  emu::set_options(after);
  kernel::boot(hal::platform());
}

// **The viewer of the faces opens every format the system reads** (0.174.1, the owner: *«fes una app visualitzadora de
// fonts. Que obri tots els formats suportats per el sistema i mostri la font»*). The shelf asks the kernel to load each
// file of `/fonts` once and says what it is —a `.lfont`, a `.bdf`, a `.pcf` and a TrueType, which are the four readers of
// `font::load_any`— and a file that is not a face says the reason instead of disappearing. Then a finger on a row opens
// the specimen, the face draws its own words at its own sizes, and the way back is a button and the pinch. **The three
// places of the door are in it**: the flash, the card and the app's own `apps/<id>/fonts` (docs/30, docs/70, docs/85).
void test_the_font_viewer_opens_every_format_the_system_reads(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(copy_a_face(storage, "dejavusans-bold.ttf"));
  make_card();
  char card_path[96];
  snprintf(card_path, sizeof(card_path), "%s/sd", root());

  // **The card is in the slot before the boot**, because the mount table is built at boot: the card of this case is a
  // folder of the run and the two disks have to be there when the door of the faces is made (docs/70, docs/85).
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  opts.touch = true;
  opts.sd_dir = card_path;
  emu::set_options(opts);
  kernel::boot(hal::platform());
  TEST_ASSERT_NOT_NULL(hal::platform().touch());
  TEST_ASSERT_TRUE(kernel::touch::boot(hal::platform()));
  emu::Gt911Panel* panel = emu::touch_panel();
  TEST_ASSERT_NOT_NULL(panel);
  hal::IStorage* card = hal::platform().storage("/sd");
  TEST_ASSERT_NOT_NULL(card);

  // **The app that ships is the app the test runs** (docs/85).
  TEST_ASSERT_TRUE(storage->mkdir("/apps/fonts"));
  TEST_ASSERT_TRUE(storage->mkdir("/apps/fonts/res"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/fonts/fonts.lua", "/apps/fonts/fonts.lua"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/fonts/manifest.json", "/apps/fonts/manifest.json"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/fonts/res/icon.png", "/apps/fonts/res/icon.png"));

  // **One face of each of the four formats the kernel reads**, and the licence that is not a face: the four names are
  // the four readers of `font::load_any`, and the page has to say which is which (docs/30).
  storage->mkdir("/res/fonts");
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "data/res/fonts/sample-8x8.bdf", "/res/fonts/bdf-sample.bdf"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "data/res/fonts/sample-8x8.pcf", "/res/fonts/pcf-sample.pcf"));
  TEST_ASSERT_TRUE(
      copy_a_host_file(storage, "data/res/fonts/dejavusansmono-6x12.lfont", "/res/fonts/lfont-sample.lfont"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "data/res/fonts/dejavusans.ttf", "/res/fonts/ttf-sample.ttf"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "data/res/fonts/LICENCE.txt", "/res/fonts/LICENCE.txt"));
  // **And the other two places of the door**: one face on the card and one the app carries itself, which is the half of
  // the door only that app sees (docs/30, docs/40).
  card->mkdir("fonts");
  TEST_ASSERT_TRUE(copy_a_host_file(card, "data/res/fonts/dejavuserif.ttf", "fonts/card-face.ttf"));
  TEST_ASSERT_TRUE(storage->mkdir("/apps/fonts/fonts"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "data/res/fonts/dejavusansmono.ttf", "/apps/fonts/fonts/own-face.ttf"));

  // `run fonts shelf` prints the box of every row, so a finger lands on a name and not on a coordinate this case would
  // have to know (docs/30, docs/85).
  kernel::log_echo(true);
  pump_captured(400, g_out, sizeof(g_out), "run fonts shelf");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "fonts: bdf-sample.bdf BDF"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "fonts: pcf-sample.pcf PCF"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "fonts: lfont-sample.lfont lunokOS"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "fonts: ttf-sample.ttf TrueType"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "fonts: LICENCE.txt none"), g_out);
  // **The card and the app's own folder are the other two places of the door** (0.174.1).
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "fonts: card-face.ttf TrueType"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "fonts: own-face.ttf TrueType"), g_out);
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), g_out);

  // **The row of the bdf, where the page says it is**: the first page carries all of them and the app prints the centre
  // of each one.
  int row_x = -1;
  int row_y = -1;
  const char* at = strstr(g_out, "fonts: row bdf-sample.bdf ");
  TEST_ASSERT_NOT_NULL_MESSAGE(at, g_out);
  TEST_ASSERT_EQUAL_INT(2, sscanf(at + strlen("fonts: row bdf-sample.bdf "), "%d %d", &row_x, &row_y));

  const kernel::ui::Canvas& view = kernel::ui::canvas();
  auto tap = [&](int x, int y) {
    if (!panel->finger_down(0, static_cast<uint16_t>(view.x + x), static_cast<uint16_t>(view.y + y))) return false;
    pump_turns(40);
    if (!panel->finger_up(0)) return false;
    pump_turns(400);
    return true;
  };

  capture_begin();
  TEST_ASSERT_TRUE(tap(row_x, row_y));
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "fonts: open bdf-sample.bdf BDF"), g_out);
  int back_x = -1;
  int back_y = -1;
  at = strstr(g_out, "fonts: backbox ");
  TEST_ASSERT_NOT_NULL_MESSAGE(at, g_out);
  TEST_ASSERT_EQUAL_INT(2, sscanf(at + strlen("fonts: backbox "), "%d %d", &back_x, &back_y));

  // **And the way back is a button**, which is the finger's road to the shelf the pinch takes for a person who knows the
  // gesture (docs/30).
  capture_begin();
  TEST_ASSERT_TRUE(tap(back_x, back_y));
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "fonts: back"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "fonts: row bdf-sample.bdf"), g_out);

  if (kernel::app_running()) {
    kernel::shell::close_app();
    pump_turns(10);
  }
  card->remove("fonts/card-face.ttf");
  storage->remove("/apps/fonts/fonts/own-face.ttf");
  storage->remove("/apps/fonts/fonts");
  storage->remove("/res/fonts/bdf-sample.bdf");
  storage->remove("/res/fonts/pcf-sample.pcf");
  storage->remove("/res/fonts/lfont-sample.lfont");
  storage->remove("/res/fonts/ttf-sample.ttf");
  storage->remove("/res/fonts/LICENCE.txt");
  storage->remove("/apps/fonts/fonts.lua");
  storage->remove("/apps/fonts/manifest.json");
  storage->remove("/apps/fonts/res/icon.png");
  storage->remove("/apps/fonts/res");
  storage->remove("/apps/fonts");
  kernel::font::set_system_face(nullptr);
  emu::Options after = emu::options();
  after.board = nullptr;
  after.display = false;
  after.touch = false;
  emu::set_options(after);
  kernel::boot(hal::platform());
}

// **A folder is made with the fathers it needs, and that is the kernel's walk and not the app's** (0.173.1, and the claim
// of 0.172.2 which is what this case pins): `fs.mkdir` is one call and the chain is made inside the storage by
// `fs_ops::mkdir` (`make_dirs`) since the import, so an app that writes in its own ground the first time —the marks of the
// time clock, which is what reported *«not saved»* on a board— does not have to ask for `/data`, then `/data/<id>`, then
// the file's folder (docs/30, docs/70).
void test_a_folder_is_made_with_the_fathers_it_needs(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  // **A name no other case uses**, so the ground of this one is not there when it starts: the walk has to make
  // `/data/<id>` and the two folders under it.
  hal::Stat earlier;
  TEST_ASSERT_FALSE_MESSAGE(storage->stat("/data/deepfolder", earlier), "the ground of this case was already there");
  TEST_ASSERT_TRUE(add_app(storage, "deepfolder",
                           "{\"title\":\"Deep\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local deep = fs.data('a/b')\n"
                           "local ok, err = fs.mkdir(deep)\n"
                           "console.print('mkdir ' .. tostring(ok) .. ' ' .. tostring(err and err.code))\n"
                           "local wrote, why = fs.open(deep .. '/x.txt', 'w')\n"
                           "console.print('open ' .. tostring(wrote ~= nil) .. ' ' .. tostring(why and why.code))\n"
                           "if wrote then wrote:write('deep'); wrote:close() end\n"
                           "console.print('read ' .. tostring(fs.read(deep .. '/x.txt')))\n"));
  TEST_ASSERT_TRUE(run_captured("run deepfolder", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "mkdir true nil"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "open true nil"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "read deep"), g_out);

  hal::Stat info;
  TEST_ASSERT_TRUE(storage->stat("/data/deepfolder/a/b/x.txt", info));
  storage->remove("/data/deepfolder/a/b/x.txt");
  storage->remove("/data/deepfolder/a/b");
  storage->remove("/data/deepfolder/a");
  storage->remove("/data/deepfolder");
  storage->remove("/apps/deepfolder/app.lua");
  storage->remove("/apps/deepfolder/manifest.json");
  storage->remove("/apps/deepfolder");
}

// **A card over the page takes the walk, the ring and the finger, and gives the ring back when it goes** (0.172.0 and
// 0.172.1: the owner saw the physical keys still on the page behind a question nobody had answered). What is proved here
// is the rule and not the drawing: the control behind the card is **not** a stop while the card is up, the dial's arrows
// and the centre answer the card, a finger that lands outside the card is its **cancel** door and not the button under it,
// and the ring comes back to the control that had it in the present that closes the card (docs/30, docs/90).
void test_a_card_over_the_page_takes_the_walk_and_gives_it_back(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(copy_a_face(storage, "dejavusans-bold.ttf"));
  // The glass and the panel are the model's and are asked for by name: `use_board` gives a board its glass and not its
  // finger, and this case needs a finger (docs/85).
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  opts.touch = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());
  TEST_ASSERT_NOT_NULL(hal::platform().touch());
  TEST_ASSERT_TRUE(kernel::touch::boot(hal::platform()));
  emu::Gt911Panel* panel = emu::touch_panel();
  TEST_ASSERT_NOT_NULL(panel);

  TEST_ASSERT_TRUE(add_app(storage, "modaler",
                           "{\"title\":\"Modaler\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           // A button behind the card: its click and its focus are lines of the log, and what this case
                           // looks for is that the click never happens while a question is up. **The card is asked from
                           // the loop and not from a hook**, because a hook runs inside the system's call and cannot wait.
                           "local behind = ui.button(200, 60)\n"
                           "behind.title = 'Behind'\n"
                           "behind.scale = 4\n"
                           "behind:place(20, 120)\n"
                           "behind.on_click = function() console.print('behind clicked') end\n"
                           "behind.on_focus = function(self, on) console.print('behind focus ' .. tostring(on)) end\n"
                           "ui.present('gc16')\n"
                           "local asked = 0\n"
                           "local went = false\n"
                           "local card = nil\n"
                           "local function ask(n)\n"
                           "  card = ui.dialog(380, 260)\n"
                           "  card:place(80, 500)\n"
                           "  card.title = 'Question ' .. n\n"
                           "  card.text = 'A card over the page'\n"
                           "  card.accept = 'Yes'\n"
                           "  card.cancel = 'No'\n"
                           "  card.emphasis = 2\n"
                           "  card.on_accept = function(self)\n"
                           "    console.print('accept ' .. tostring(self.choice))\n"
                           "    went = true\n"
                           "    self:release()\n"
                           "    card = nil\n"
                           "    ui.present('gc16')\n"
                           "  end\n"
                           "  card.on_cancel = function(self)\n"
                           "    console.print('cancel ' .. tostring(self.choice))\n"
                           "    went = true\n"
                           "    self:release()\n"
                           "    card = nil\n"
                           "    ui.present('gc16')\n"
                           "  end\n"
                           "  asked = n\n"
                           "  console.print('phase ' .. n)\n"
                           "  ui.present('gc16')\n"
                           "end\n"
                           "ask(1)\n"
                           "while true do\n"
                           "  if went and asked == 1 then\n"
                           "    went = false\n"
                           "    ask(2)\n"
                           "  end\n"
                           "  sys.delay(20):await()\n"
                           "end\n"));

  pump_captured(60, g_out, sizeof(g_out), "run modaler");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "phase 1"), g_out);
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), g_out);
  // **Nothing has been said yet, and that is the contract**: the walk of the focus is asked for by a key or a finger, and
  // a page that only paints does not move any ring. The first control of a page is born with it and its `on_focus` does
  // not run there, so the log has no line about the button until something asks (docs/30).
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, times_in(g_out, "behind focus false"), g_out);
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, times_in(g_out, "behind focus true"), g_out);

  // **The arrows and the centre are the card's**: one step to the right is the accept door, whatever the page behind
  // has, and the button behind does not hear the press.
  capture_begin();
  emu::press_key(2, true);
  pump_turns(20);
  emu::press_key(2, false);
  pump_turns(20);
  emu::press_button(true);
  pump_turns(20);
  emu::press_button(false);
  pump_turns(60);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "accept 2"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "behind clicked"), "the key went through the card");
  // **And the button behind heard that the ring went**: the first key of the card is the walk that finds it, and the
  // control that had the focus is told, because it is no longer a stop and the walk cannot tell it any other way (0.173.1).
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, times_in(g_out, "behind focus false"), g_out);
  // And the present that closed the card is the one that gave the ring back: the first time the button hears that it has
  // the focus is that paint, and not another question (0.172.1).
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, times_in(g_out, "behind focus true"), g_out);

  // **And a finger outside the card is its cancel**: the app asks a second question from its loop, the card goes up, and
  // the touch lands on the box of the button —a box nobody is offering while the question is up— so the card hears the
  // cancel and the button hears nothing (docs/30).
  capture_begin();
  pump_turns(200);
  const kernel::ui::Canvas& view = kernel::ui::canvas();
  TEST_ASSERT_TRUE(panel->finger_down(0, static_cast<uint16_t>(view.x + 120), static_cast<uint16_t>(view.y + 150)));
  pump_turns(40);
  TEST_ASSERT_TRUE(panel->finger_up(0));
  pump_turns(400);
  capture_end(g_out, sizeof(g_out));
  // **The second question is up**: the app asks it from its loop as soon as the first one is answered, so the capture of
  // this phase begins with the card already on the glass (and its `phase 2` line went out in the one before).
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "cancel 2"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "behind clicked"), "the finger went through the card");
  // **The capture is this phase's**: the ring went to the button on the way down —the card was up, so the button had it
  // and lost it before the finger left the glass— and it came back in the present the cancel painted (0.172.1, 0.173.1).
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, times_in(g_out, "behind focus false"), g_out);
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, times_in(g_out, "behind focus true"), g_out);

  for (int i = 0; i < 3 && kernel::app_running(); ++i) {
    emu::press_button(true);
    pump_turns(10);
    emu::press_button(false);
    pump_turns(10);
  }
  storage->remove("/apps/modaler/app.lua");
  storage->remove("/apps/modaler/manifest.json");
  storage->remove("/apps/modaler");

  // **The next case gets the emulator and the face it had**: this one loaded the bold face of the disk and asked for a
  // finger, and both are the whole system's —a face that stayed would decide what the next case measures (docs/85).
  kernel::font::set_system_face(nullptr);
  storage->remove("/res/fonts/dejavusans-bold.ttf");
  emu::Options after = emu::options();
  after.board = nullptr;
  after.display = false;
  after.touch = false;
  emu::set_options(after);
  kernel::boot(hal::platform());
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

  // The next test gets the default emulator back: the display and the panel were this test's.
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

  // The next test gets the default emulator back: the display and the panel were this test's.
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
  // **Six rows of a box of two hundred pixels with a line of the system**: a line is forty-four pixels at the size of
  // every control now (0.97.2), so the box holds six and not the eight of a body of twenty-four (docs/92).
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "rows 6"), g_out);
  capture_begin();
  pump_turns(20);
  // **The enter key of the keyboard is a break and not the end of the edition**: the key arrives as the number the
  // keyboard hands over, and the letter after it goes on the new line.
  luabind::ui_keyboard_typed(13);
  pump_turns(20);
  luabind::ui_keyboard_typed('X');
  pump_turns(20);
  capture_end(g_out, sizeof(g_out));
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

  // The next test gets the default emulator back: the display and the panel were this test's.
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
  // The emulator is the Paper here: one compound dial of three keys on the **right** face of the page, with the
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

  // The emulator comes back 243 s before the hour it was given, which is what a board whose supply
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

// The dump command of the shell, native C++ and not an app: the memory is read as hex and as text, a
// byte that cannot be printed is a dot, and the empty tail is not printed at all (docs/50, docs/70).
// **The command is given the file it dumps** (0.164.4): the default of `/rtc` was a guess, and a command
// that opens a file says which one.
void test_the_hexdump_command_shows_the_bytes_of_the_rtc_memory(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* rtc = hal::platform().storage("/rtc");
  TEST_ASSERT_NOT_NULL(rtc);

  // An empty memory is said in one line and not in four hundred rows of zeros. **The path is asked and not guessed**
  // (0.164.4): `cat` asks for one and so does `hexdump`, which used to answer with the room of the RTC because nobody had
  // asked for it (docs/30, docs/70).
  memset(emu::rtc_ram(), 0, emu::kRtcRamBytes);
  TEST_ASSERT_TRUE(run_captured("hexdump /rtc", g_out, sizeof(g_out)));
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

  TEST_ASSERT_TRUE(run_captured("hexdump /rtc", g_out, sizeof(g_out)));
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
  // eighth, which is how `/cfg` disappeared on the emulator (31/10, docs/70).
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "/cfg"), g_out);

  // And the command of the shell looks at it when it is told which file: `hexdump /fram`.
  TEST_ASSERT_TRUE(run_captured("hexdump /fram", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "|kept in the fram|"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(
      strstr(g_out, "00000000  6b 65 70 74 20 69 6e 20 74 68 65 20 66 72 61 6d"), g_out);

  // The memory that does not need the power: a power cycle of the emulator does not touch it.
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

// The alarm demos of the repository, run from the disk: the queue the system keeps and the
// timers an app arms. A demo that stops working is a lie in the documentation (docs/40), and
// these two are the ones that show the queue of `lib/alarms` and the clock behind it.

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

  // The emulator keeps the board powered, so its deep sleep is a sleep and not a power off. A
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

  // A light sleep on the emulator is the clock jumping: the number is what came out, and it
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
// alarm, and a person can turn that off while working at the prompt. This emulator has no chip,
// so what is proved here is the switch and that the emulator is honest about not sleeping by
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
// ATOM has no hold on its supply; the Paper has one, and the model of the emulator says so.
void test_poweroff_cuts_the_power_of_the_board_that_can(void) {
  TEST_ASSERT_TRUE(boot_once());

  // The ATOM, which is the board a test starts on: no hold, so the line says why and the system
  // goes on running. A command that looked like it worked and left the board on is worse.
  TEST_ASSERT_TRUE(run_captured("poweroff", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "powering off..."), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "cannot cut its own power"), g_out);
  TEST_ASSERT_TRUE(kernel::running());

  // The Paper: the supply is cut, and the emulator ends because a board that is off is a board that is
  // off. There is nothing to say after the cut, so the system is not running any more.
  use_board("m5stack-paper");
  TEST_ASSERT_TRUE(run_captured("poweroff", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "powering off..."), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "cannot cut its own power"), g_out);
  TEST_ASSERT_FALSE(kernel::running());
}

// The demo app that shows handles by hand is a file in the repository, so it is
// run here: an example that stops working is a lie in the documentation.

// The sensor of the band is a fake (docs/85), so a reading can be tried on the PC:
// this walks the whole path, from the app to the fake chip and back, and the light
// of the panel follows what it says (docs/30, docs/50).

// A sensor that is not there is an error and not a number: an app cannot be left
// believing it read 0 g and is level (docs/30). The board without a sensor says
// E_UNSUPPORTED and this emulator cannot walk that branch —it always has one— so what
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

// **The face of the whole system comes from the disk** (0.96.0): the bar, every control and every text that says no face
// of its own are the bold face the kernel loads at boot from `/fonts/dejavusans-bold.ttf` —the same typeface the paper
// project settled on— and a board whose disk carries no font keeps the five by seven of the flash, which is 14 px of
// line at `scale` 2: 1,5 mm on this glass, a stamp. The step of a face of outlines is eight pixels of body and the step of
// the table is seven, which is why a caller that measured the old one would draw its text past the box it cleaned
// (docs/30, docs/92).
void test_the_face_of_the_system_comes_from_the_disk(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);

  // The emulator's disk carries no font, so what the glass has is the table of the flash: a face of bits, seven pixels of
  // body for every step of `scale`.
  TEST_ASSERT_NULL(kernel::font::system_face().outline);
  TEST_ASSERT_EQUAL_UINT8(7, kernel::font::system_face().height);

  // **With the bold face on the disk the next boot takes it**, and it is a face of outlines: **eleven pixels of line per
  // step**, which is a body of nine —the paper project's text is 36 px of body and a line of 43— (0.97.2, docs/92).
  TEST_ASSERT_TRUE(copy_a_face(storage, "dejavusans-bold.ttf"));
  kernel::boot(hal::platform());
  TEST_ASSERT_NOT_NULL(kernel::font::system_face().outline);
  TEST_ASSERT_EQUAL_UINT8(11, kernel::font::system_face().height);

  // **And the emulator is left as it was**: the face is one global of the whole glass, and a test that changed it for the
  // ones after it would be a test that decides what the others measure.
  kernel::font::set_system_face(nullptr);
  storage->remove("/res/fonts/dejavusans-bold.ttf");
}

// **An app says which extensions it opens, and it may name several** (0.120.0): the manifests of PaperOS wrote
// `"opens": "txt md lua json"` from the first day —fifteen names in its Editor— and this system read a single extension,
// so a list was either refused or cut. Now every word of the string is a handler, whatever case and dot it carries, and
// the file whose extension is the second name of the list is opened by the app (docs/40).
void test_an_app_opens_a_list_of_extensions(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "multi",
                           "{\"title\":\"Multi\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"],"
                           "\"opens\":\"lunok2 .WORD2\"}",
                           "console.print('ext ' .. tostring(app.args.ext))\n"));
  TEST_ASSERT_TRUE(write_text(storage, "/res/a.word2", "x\n"));

  // The list is read as a list, in the order the manifest writes it and with each name normalised (docs/40).
  kernel::apps::Entry found;
  uint32_t matches = 0;
  TEST_ASSERT_TRUE(kernel::apps::read("multi", found));
  TEST_ASSERT_EQUAL_UINT32(2, found.opens_count);
  TEST_ASSERT_EQUAL_STRING("lunok2", found.opens[0]);
  TEST_ASSERT_EQUAL_STRING("word2", found.opens[1]);
  char whole[32];
  kernel::apps::opens_text(found, whole, sizeof(whole));
  TEST_ASSERT_EQUAL_STRING("lunok2 word2", whole);

  // **The second name of the list is a handler too**, which is the whole point of the change: the search and the
  // question the shell asks see the same list.
  TEST_ASSERT_TRUE(kernel::apps::find_for_extension("word2", found, matches));
  TEST_ASSERT_EQUAL_STRING("multi", found.id);
  TEST_ASSERT_TRUE(kernel::apps::find_for_extension("LUNOK2", found, matches));

  // And `open` takes the app through that name, with the extension of the file it was handed.
  TEST_ASSERT_TRUE(run_captured("open res/a.word2", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "ext word2"), g_out);

  storage->remove("/apps/multi/app.lua");
  storage->remove("/apps/multi/manifest.json");
  storage->remove("/apps/multi");
  storage->remove("/res/a.word2");
}

// **The switches a manifest asks for reach the app and nobody else** (0.120.0): `opensArgs` is `-ro` in PaperOS's
// Editor, and the system does not read it —only the app knows what its own switches mean— so it goes to the app at
// `app.args.flags` as it was written. A value that does not look like a switch is dropped with a line, because handing
// an app a stray word as one of its own flags is worse than handing it nothing (docs/30, docs/40).
void test_the_switches_of_a_manifest_reach_the_app(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "roflag",
                           "{\"title\":\"Ro flag\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"],"
                           "\"opens\":\"roflag\",\"opensArgs\":\"-ro\"}",
                           "if app.args ~= nil and app.args.flags ~= nil then\n"
                           "  console.print('flags ' .. app.args.flags)\n"
                           "else\n"
                           "  console.print('no flags')\n"
                           "end\n"));
  TEST_ASSERT_TRUE(write_text(storage, "/res/x.roflag", "x\n"));

  TEST_ASSERT_TRUE(run_captured("open res/x.roflag", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "flags -ro"), g_out);

  // **An `opensArgs` that is not a switch is not a switch**: the app is told nothing and the log says what was in the
  // file, so a person can see why their flag never arrived.
  TEST_ASSERT_TRUE(add_app(storage, "badsw",
                           "{\"title\":\"Bad switch\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"],"
                           "\"opens\":\"badsw\",\"opensArgs\":\"ro\"}",
                           "if app.args ~= nil and app.args.flags ~= nil then\n"
                           "  console.print('flags ' .. app.args.flags)\n"
                           "else\n"
                           "  console.print('no flags')\n"
                           "end\n"));
  TEST_ASSERT_TRUE(write_text(storage, "/res/x.badsw", "x\n"));
  kernel::log_echo(true);
  TEST_ASSERT_TRUE(run_captured("open res/x.badsw", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "no flags"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "has an opensArgs that is not a switch (ro)"), g_out);

  storage->remove("/apps/roflag/app.lua");
  storage->remove("/apps/roflag/manifest.json");
  storage->remove("/apps/roflag");
  storage->remove("/apps/badsw/app.lua");
  storage->remove("/apps/badsw/manifest.json");
  storage->remove("/apps/badsw");
  storage->remove("/res/x.roflag");
  storage->remove("/res/x.badsw");
}

// **`apps info <id>` shows the manifest of an app of the disk, key by key** (0.120.0): the same card `repo info` shows
// for an app of the repository, with the keys the manifests of PaperOS carry —author, description, icon, opensArgs— and
// a name the door does not have is said as a missing app and not as an empty card (docs/30, docs/40).
void test_apps_info_shows_the_keys_a_manifest_carries(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "card",
                           "{\"title\":\"Card\",\"author\":\"@someone\",\"version\":\"2.0.1\",\"entry\":\"app.lua\","
                           "\"boards\":[\"esp32\"],\"opens\":\"txt md\",\"opensArgs\":\"-ro\","
                           "\"icon\":\"res/badge.png\",\"description\":\"First line.\\nSecond line.\"}",
                           "console.print('card')\n"));
  // **The folder of the app's own files is made here**: `add_app` writes the manifest and the entry and nothing else, and
  // a file that has no folder to live in is a write that fails and a case that says nothing about the manifest (0.152.4).
  storage->mkdir("/apps/card/res");
  TEST_ASSERT_TRUE(write_text(storage, "/apps/card/res/badge.png", "not really a picture\n"));

  TEST_ASSERT_TRUE(run_captured("apps info card", g_out, sizeof(g_out)));
  // The first line says what it is and where it read it from, and every key follows with its own line (docs/30).
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "app: Card (/apps/card)"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "author      @someone"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "version     2.0.1"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "opens       txt md"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "opensArgs   -ro"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "icon        res/badge.png"), g_out);
  // **A description of two lines stays two lines**: that is how the manifests of PaperOS write theirs.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "First line.\nSecond line."), g_out);

  // **The badge is a picture of the app and nothing else** (0.120.0): a name that climbs out of the folder is dropped
  // with a line, as a splash that does, because a manifest that points outside what it carries lies about it.
  TEST_ASSERT_TRUE(add_app(storage, "outside",
                           "{\"title\":\"Outside\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"],"
                           "\"icon\":\"../badge.png\"}",
                           "console.print('outside')\n"));
  kernel::log_echo(true);
  pump_captured(2, g_out, sizeof(g_out), "apps");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "outside names an icon outside the app"), g_out);
  kernel::apps::Entry read_out;
  TEST_ASSERT_TRUE(kernel::apps::read("outside", read_out));
  TEST_ASSERT_EQUAL_STRING("", read_out.icon);

  // A name that is not there is a missing app, not an empty card.
  TEST_ASSERT_TRUE(run_captured("apps info nowhere", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "no such app: nowhere"), g_out);

  storage->remove("/apps/card/res/badge.png");
  storage->remove("/apps/card/res");
  storage->remove("/apps/card/app.lua");
  storage->remove("/apps/card/manifest.json");
  storage->remove("/apps/card");
  storage->remove("/apps/outside/app.lua");
  storage->remove("/apps/outside/manifest.json");
  storage->remove("/apps/outside");
}

// **A list of extensions longer than the system carries says so, and a word that is not an extension is named**
// (0.120.0): the ceiling is eight names —what `kernel::apps::Entry` holds— and the line says both numbers, as the list of boards
// does, because "it declares more than this system carries" and "it declares nothing" are two different files (docs/40).
void test_a_manifest_that_names_more_extensions_than_the_system_carries_says_so(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "manyopens",
                           "{\"title\":\"Many opens\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"],"
                           "\"opens\":\"a1 b2 c3 d4 e5 f6 g7 h8 i9\"}",
                           "console.print('many')\n"));

  kernel::log_echo(true);
  pump_captured(4, g_out, sizeof(g_out), "apps");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "names 9 extensions and this system carries 8"), g_out);
  // The eight that fit are kept and the app is still an app: a handler that declares one extension too many still opens
  // the eight it named first, which is more use than refusing the whole list (docs/40).
  kernel::apps::Entry found;
  TEST_ASSERT_TRUE(kernel::apps::read("manyopens", found));
  TEST_ASSERT_EQUAL_UINT32(8, found.opens_count);
  TEST_ASSERT_EQUAL_STRING("h8", found.opens[7]);

  // A word that is not an extension is dropped and named: `/` is not a character an extension has.
  TEST_ASSERT_TRUE(add_app(storage, "onebad",
                           "{\"title\":\"One bad\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"],"
                           "\"opens\":\"ok1 nope/xx ok2\"}",
                           "console.print('onebad')\n"));
  pump_captured(4, g_out, sizeof(g_out), "apps");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "declares an opens that is not an extension (nope/xx)"), g_out);
  TEST_ASSERT_TRUE(kernel::apps::read("onebad", found));
  TEST_ASSERT_EQUAL_UINT32(2, found.opens_count);
  TEST_ASSERT_EQUAL_STRING("ok2", found.opens[1]);

  storage->remove("/apps/manyopens/app.lua");
  storage->remove("/apps/manyopens/manifest.json");
  storage->remove("/apps/manyopens");
  storage->remove("/apps/onebad/app.lua");
  storage->remove("/apps/onebad/manifest.json");
  storage->remove("/apps/onebad");
}

// **The calculator of the paper project, key for key** (0.121.0): the app is painted by hand, so the test
// asks the app where its keys are —`run calculator keys` prints the display box and the centre of every
// one— and presses them with the finger of the emulator. What comes back is the line the app prints at every
// `=`, which is the whole path at once: the frame of the glass, the hit test by rectangle, the arithmetic
// and the console.
//
// **The layout is the original's and it is asserted as numbers**: a display of 508x200 under the bar and a
// keypad of four columns and five rows, the zero double, which is what the port promises. And **a key
// repaints the display box and not the page**, which the log says with the size of the region painted.
void test_the_calculator_of_the_paper_project_adds_with_the_finger(void) {
  TEST_ASSERT_TRUE(boot_once());

  // **The face of the board is on this emulator's disk for this case** (0.158.0): the keys carry the four signs
  // of the original —`←`, `±`, `÷`, `×`— and the five by seven of the flash carries the printable ASCII, so
  // a sign there would be a hole and a key that looks broken. The suite's disk has no face of its own ("the
  // emulator's disk carries no font", the case of the face of the system), so it is copied here and the boot of
  // this case is the one that loads it (docs/30, docs/85).
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(copy_a_face(storage, "dejavusans-bold.ttf"));

  // **The glass and the panel are of the model and a test's `Options()` has them off** (docs/85): an app
  // that needs a glass says so and stops, which is right on a headless emulator and is not what this reads.
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  opts.touch = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());
  TEST_ASSERT_NOT_NULL(hal::platform().touch());
  TEST_ASSERT_TRUE(kernel::touch::boot(hal::platform()));
  emu::Gt911Panel* panel = emu::touch_panel();
  TEST_ASSERT_NOT_NULL(panel);
  TEST_ASSERT_NOT_NULL_MESSAGE(kernel::font::system_face().outline, "the face of the disk is loaded");

  TEST_ASSERT_TRUE(storage->mkdir("/apps/calculator"));
  // **The app that ships is the app the test runs** (docs/85): the two files come from the repository and
  // not from a copy that goes stale.
  TEST_ASSERT_TRUE(
      copy_a_host_file(storage, "apps/calculator/calculator.lua", "/apps/calculator/calculator.lua"));
  TEST_ASSERT_TRUE(
      copy_a_host_file(storage, "apps/calculator/manifest.json", "/apps/calculator/manifest.json"));

  // **Where the keys are, from the app**: one line per key and one for the display box (docs/30).
  kernel::log_echo(true);
  pump_captured(40, g_out, sizeof(g_out), "run calculator keys");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "Calculator display 16 16 508 200"), g_out);
  // The first page is the whole view and it is one paint: 540x912 is the page under the bar of the system.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "540x912"), g_out);

  // **The four sizes are the paper project's, measured in pixels of line** (0.121.0): `small` 33, `body` 43,
  // `title` 57 and `big` 82 (`lib/gfx/fonts/fonts.h` of PaperOS). The port was written in steps of the face
  // first and the page came out noticeably smaller than the original; the sizes are asked in pixels now, and
  // the face of the disk answers in whole steps —**eleven pixels of line per step**, so the four come out
  // 33, 44, 55 and 77 and the worst is five pixels off, which is what an outline face can do and the bitmap
  // faces of the paper project, baked at 33, 43, 57 and 82, could not— (docs/30, docs/92 §34). A page that
  // lost one step is a page that fails this line.
  const int wanted[4] = {33, 43, 57, 82};
  int got[4] = {0, 0, 0, 0};
  const char* sizes = strstr(g_out, "Calculator sizes ");
  TEST_ASSERT_NOT_NULL_MESSAGE(sizes, g_out);
  TEST_ASSERT_EQUAL_INT_MESSAGE(4, sscanf(sizes, "Calculator sizes %d %d %d %d", &got[0], &got[1], &got[2],
                                          &got[3]),
                                g_out);
  for (int i = 0; i < 4; ++i) {
    const int step = kernel::font::system_face().height;
    const int off = (got[i] > wanted[i]) ? (got[i] - wanted[i]) : (wanted[i] - got[i]);
    TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(step - 1, off, sizes);
  }

  struct Spot {
    char label[8];
    int x;
    int y;
  };
  Spot spots[24] = {};
  int spot_count = 0;
  // **The four signs are written by the face that is loaded and a number says so** (0.158.0): the labels of
  // the keypad are `÷`, `×`, `±` and `←`, three of them Latin-1 and the arrow one of the five codes
  // Windows-1252 leaves empty, and a face without one of them paints a blank key. The width of the four is
  // asked of the app, so a zero here is a red test and not something somebody has to see on the glass
  // (docs/30).
  const char* signs = strstr(g_out, "Calculator symbols ");
  TEST_ASSERT_NOT_NULL_MESSAGE(signs, g_out);
  int sign_w[4] = {0, 0, 0, 0};
  TEST_ASSERT_EQUAL_INT_MESSAGE(4, sscanf(signs, "Calculator symbols %d %d %d %d", &sign_w[0], &sign_w[1],
                                          &sign_w[2], &sign_w[3]),
                                g_out);
  for (int i = 0; i < 4; ++i) TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, sign_w[i], signs);
  for (const char* at = strstr(g_out, "Calculator key "); at != nullptr && spot_count < 24;
       at = strstr(at + 1, "Calculator key ")) {
    char label[8] = {0};
    int x = 0;
    int y = 0;
    if (sscanf(at, "Calculator key %7s %d %d", label, &x, &y) != 3) continue;
    snprintf(spots[spot_count].label, sizeof(spots[0].label), "%s", label);
    spots[spot_count].x = x;
    spots[spot_count].y = y;
    ++spot_count;
  }
  // The nineteen keys of the original: ten digits, the four operations, the point, equals, clear, back and
  // the sign. A keypad that lost one of them is a calculator that cannot be used.
  TEST_ASSERT_EQUAL_INT_MESSAGE(19, spot_count, g_out);

  const kernel::ui::Canvas& view = kernel::ui::canvas();
  // A key is pressed where the app says it is, and the panel counts in the frame of the page: the point of
  // the view is the point of the glass plus where the view sits under the bar (docs/30, docs/50).
  int taps_since_a_pause = 0;
  auto press = [&](const char* want) {
    for (int i = 0; i < spot_count; ++i) {
      if (strcmp(spots[i].label, want) != 0) continue;
      if (!panel->finger_down(0, static_cast<uint16_t>(view.x + spots[i].x),
                              static_cast<uint16_t>(view.y + spots[i].y))) {
        return false;
      }
      pump_turns(40);
      if (!panel->finger_up(0)) return false;
      // **The lift has to be seen before the next key goes down** (0.157.0): the kernel keeps the last frame of the
      // finger until the glass has been quiet for thirty milliseconds, and the calculator presses a key on the first
      // frame that sees a finger and not while it believes one is still there. Forty turns were the edge of that
      // window and the last key of the case was lost one run in five (found on the 8/11, docs/30, docs/50).
      //
      // **And every second key waits out the window of the double and the triple** (0.136.0): two taps inside three
      // hundred and fifty milliseconds are a `double` and three a `triple`, gestures of the glass that a key of this page
      // does not read as a press —the third of four presses was swallowed and the sum came out short— (docs/30, docs/92).
      if (++taps_since_a_pause >= 2) {
        taps_since_a_pause = 0;
        pump_turns(400);
      } else {
        pump_turns(90);
      }
      return true;
    }
    return false;
  };

  kernel::log_echo(true);
  capture_begin();
  TEST_ASSERT_TRUE(press("7"));
  TEST_ASSERT_TRUE(press("+"));
  TEST_ASSERT_TRUE(press("3"));
  TEST_ASSERT_TRUE(press("="));
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "Calculator: 7 + 3 = 10"), g_out);
  // **A key paints the display box and not the page**: what was painted after the four presses is the inside
  // of the frame of the box —508 wide and 200 tall at the most, and the frame itself does not change— and not
  // one of 540x912, which would be the whole page flashing at every digit (docs/50).
  int painted_w = 0;
  int painted_h = 0;
  int paints = 0;
  for (const char* at = strstr(g_out, "[I][UI] paint "); at != nullptr;
       at = strstr(at + 1, "[I][UI] paint ")) {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    if (sscanf(at, "[I][UI] paint %d,%d %dx%d", &x, &y, &w, &h) != 4) continue;
    painted_w = w;
    painted_h = h;
    ++paints;
  }
  TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, paints, g_out);
  TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(508, painted_w, g_out);
  TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(200, painted_h, g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "540x912"), g_out);

  // Dividing by zero has no answer, and the next key clears the way instead of asking for Clear first.
  capture_begin();
  TEST_ASSERT_TRUE(press("C"));
  TEST_ASSERT_TRUE(press("8"));
  TEST_ASSERT_TRUE(press("\xF7"));  // the divide key carries `÷`, as on the paper calculator
  TEST_ASSERT_TRUE(press("0"));
  TEST_ASSERT_TRUE(press("="));
  TEST_ASSERT_TRUE(press("2"));
  TEST_ASSERT_TRUE(press("+"));
  TEST_ASSERT_TRUE(press("2"));
  TEST_ASSERT_TRUE(press("="));
  capture_end(g_out, sizeof(g_out));
  // The line is read as the paper calculator writes it: the sign and not the letter of the operation.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "Calculator: 8 \xF7" " 0 = Error"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "Calculator: 2 + 2 = 4"), g_out);

  // The point, the backspace, the sign and the times.
  capture_begin();
  TEST_ASSERT_TRUE(press("C"));
  TEST_ASSERT_TRUE(press("1"));
  TEST_ASSERT_TRUE(press("2"));
  TEST_ASSERT_TRUE(press("3"));
  TEST_ASSERT_TRUE(press("\x81"));  // the arrow of the backspace (`←`, the code CP1252 leaves empty)
  TEST_ASSERT_TRUE(press("="));
  TEST_ASSERT_TRUE(press("C"));
  TEST_ASSERT_TRUE(press("5"));
  TEST_ASSERT_TRUE(press("."));
  TEST_ASSERT_TRUE(press("5"));
  TEST_ASSERT_TRUE(press("+"));
  TEST_ASSERT_TRUE(press("1"));
  TEST_ASSERT_TRUE(press("="));
  TEST_ASSERT_TRUE(press("C"));
  TEST_ASSERT_TRUE(press("5"));
  TEST_ASSERT_TRUE(press("\xB1"));  // the sign key carries `±`
  TEST_ASSERT_TRUE(press("+"));
  TEST_ASSERT_TRUE(press("1"));
  TEST_ASSERT_TRUE(press("="));
  TEST_ASSERT_TRUE(press("C"));
  TEST_ASSERT_TRUE(press("6"));
  TEST_ASSERT_TRUE(press("\xD7"));  // the times key carries `×`
  TEST_ASSERT_TRUE(press("7"));
  TEST_ASSERT_TRUE(press("="));
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "Calculator: 12 = 12"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "Calculator: 5.5 + 1 = 6.5"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "Calculator: -5 + 1 = -4"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "Calculator: 6 \xD7" " 7 = 42"), g_out);

  kernel::shell::close_app();
  pump_captured(10, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  kernel::log_echo(false);

  storage->remove("/apps/calculator/calculator.lua");
  storage->remove("/apps/calculator/manifest.json");
  storage->remove("/apps/calculator");

  // **The emulator is left as it was found**: the face of the system is one global of the glass, and a case that
  // left the bold face behind would decide what the cases after it measure (docs/30).
  kernel::font::set_system_face(nullptr);
  storage->remove("/res/fonts/dejavusans-bold.ttf");
}

// **How many libraries the tree carries**: a folder of `lib/` with a `library.json` in it is a library, and that is
// exactly what `tools/gen_about.py` counts when it writes the list the page reads (docs/45). Counting it here is what
// makes the next case able to say that the page is listing the tree and not a file that went stale.
int count_libraries_in_the_tree() {
  static const char* const kParents[] = {"", "../", "../../", "../../../"};
  int count = 0;
  for (size_t i = 0; i < sizeof(kParents) / sizeof(kParents[0]); ++i) {
    char dir_path[96];
    snprintf(dir_path, sizeof(dir_path), "%slib", kParents[i]);
    DIR* dir = opendir(dir_path);
    if (dir == nullptr) continue;
    struct dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
      if (entry->d_name[0] == '.') continue;
      // A folder of `lib/` is a name of at most sixty-four characters —and one longer than that is not a library this
      // tree has—, so the name goes in with its own bound and the path has a size the compiler can check.
      char manifest[192];
      snprintf(manifest, sizeof(manifest), "%s/%.64s/library.json", dir_path, entry->d_name);
      struct stat info = {};
      if (stat(manifest, &info) == 0) ++count;
    }
    closedir(dir);
    if (count > 0) return count;
  }
  return count;
}

// **About, the port of the paper project's page** (0.122.0): two screens —a cover with the big line and the list of
// the libraries of this project, and the detail of one of them— read with the middle key of the board, because the
// app is a tree of controls and the centre of the dial presses the one that has the ring (docs/30, docs/90).
//
// **The list is the libraries and not the pieces of the board** (0.124.0): an About page of a system says what the
// system is made of, and this one is made of the libraries of `lib/`. The list is generated by `tools/gen_about.py`
// into `res/libs.txt` —the `library.json` of every folder, which is the file the build reads— and the app parses it
// as data, because `load` is removed from an app (docs/20, docs/45).
void test_the_about_app_lists_the_libraries_of_the_project(void) {
  TEST_ASSERT_TRUE(boot_once());
  // The page needs a glass and nothing else: the rows are buttons, and a button answers the dial and the finger.
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(storage->mkdir("/apps/about"));
  TEST_ASSERT_TRUE(storage->mkdir("/apps/about/res"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/about/about.lua", "/apps/about/about.lua"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/about/manifest.json", "/apps/about/manifest.json"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/about/res/libs.txt", "/apps/about/res/libs.txt"));

  // **The version the page prints is the version the tree carries**: read from `lib/alarms/library.json`, which is the
  // file the generator reads, so the case says both that the row exists and that it is not an old number.
  static char text[1024];
  TEST_ASSERT_GREATER_THAN(0, read_host_file("lib/alarms/library.json", text, sizeof(text)));
  char alarms_version[24] = {0};
  TEST_ASSERT_TRUE(lunok::json::get_string(text, strlen(text), "version", alarms_version, sizeof(alarms_version)));
  char first_row[96];
  snprintf(first_row, sizeof(first_row), "about: row alarms %s", alarms_version);

  const int libraries = count_libraries_in_the_tree();
  TEST_ASSERT_GREATER_THAN(1, libraries);

  kernel::log_echo(true);
  pump_captured(60, g_out, sizeof(g_out), "run about");
  // The first line is the count of the tree and the system it is running on: the same number the generator counted.
  char headline[96];
  snprintf(headline, sizeof(headline), "about: %d libraries, lunokOS", libraries);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, headline), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "m5stack-paper"), g_out);
  // And the rows are the libraries, alphabetically, each with its version.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, first_row), g_out);
  // **The rows are the entries of a list and there is no paginator of the app** (0.166.0): every library is a row of
  // the list —the ones that do not fit a page are placed out of its frame by the control— and the line the app used to
  // print of its own pages is gone; what says where the person is is the foot of the list (docs/30).
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "about: cover page"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, " rows in a list of "), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "attempt to"), g_out);
  // **The page is washed whole and once**: 540x912 is the view of this board under the bar of the system, and a
  // cover that painted only the box of its panel would leave the drawing of the boot around it (docs/50).
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "540x912"), g_out);

  // **The centre of the dial opens the first library**: the first row is the control that is born with the ring,
  // so a press of the middle key is a click on it.
  char first_open[96];
  snprintf(first_open, sizeof(first_open), "about: open alarms %s", alarms_version);
  capture_begin();
  emu::press_button(true);
  pump_turns(60);
  emu::press_button(false);
  pump_turns(120);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, first_open), g_out);

  // **And the Back of the detail is the only control of that page**, so it is the one with the ring and the same
  // press goes home.
  capture_begin();
  emu::press_button(true);
  pump_turns(60);
  emu::press_button(false);
  pump_turns(120);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "about: back"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "attempt to"), g_out);

  kernel::shell::close_app();
  pump_captured(10, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  kernel::log_echo(false);

  // **The icon of the paper project travels with the two ported apps** (docs/45): the manifest names it and the
  // reader keeps the name, and the drawing is in `res/` beside the master it was made from. The icon is not
  // painted by anything yet —the page that would list the apps on the glass does not exist— so what is asserted
  // here is what the system knows of it (docs/40).
  kernel::apps::Entry ported = {};
  TEST_ASSERT_TRUE(kernel::apps::read("about", ported));
  TEST_ASSERT_EQUAL_STRING("res/icon.png", ported.icon);
  // The calculator's manifest is read where it lives: this suite's disk holds the app only while its own case
  // runs, and the icon of the manifest is a fact of the repository and not of the disk of a test.
  TEST_ASSERT_GREATER_THAN(0, read_host_file("apps/calculator/manifest.json", text, sizeof(text)));
  char icon[48] = {0};
  TEST_ASSERT_TRUE(lunok::json::get_string(text, strlen(text), "icon", icon, sizeof(icon)));
  TEST_ASSERT_EQUAL_STRING("res/icon.png", icon);

  storage->remove("/apps/about/res/libs.txt");
  storage->remove("/apps/about/res");
  storage->remove("/apps/about/about.lua");
  storage->remove("/apps/about/manifest.json");
  storage->remove("/apps/about");
}

// **The time clock of the paper project, key by key** (0.125.0): the two sheets —the day with its three keys and the
// week behind the eye—, the mark a press writes, the file it writes it to and the hour the clock is set to.
//
// The three keys are the app's whole job and they are pressed the way a person presses them: the dial walks the page
// and the centre of the paper's dial presses what the ring is on, so a page of real controls is walked and not called.
// The clock is fixed at nine in the morning of a Sunday —inside the work day, and a day whose week is known— so the
// mark has one hour and not the hour the suite happens to run at (docs/45, docs/85).
void test_the_timeclock_of_the_paper_project_keeps_the_day(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  opts.touch = true;
  // **Nine in the morning of a Sunday**: inside the work day of this app —07:00 to 19:00— and a day whose week is
  // known, so the hour of the mark does not depend on when the suite runs (docs/45, docs/85).
  opts.now_epoch = 1793523600;  // 2026-11-01 09:00:00 UTC
  emu::set_options(opts);
  kernel::boot(hal::platform());
  TEST_ASSERT_NOT_NULL(hal::platform().touch());
  TEST_ASSERT_TRUE(kernel::touch::boot(hal::platform()));
  emu::Gt911Panel* panel = emu::touch_panel();
  TEST_ASSERT_NOT_NULL(panel);

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(storage->mkdir("/apps/timeclock"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/timeclock/timeclock.lua", "/apps/timeclock/timeclock.lua"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/timeclock/manifest.json", "/apps/timeclock/manifest.json"));
  // **And the face of its three keys** (0.173.0): without the file the app says the words `Start` and `Pause` —which is
  // its fallback and not a failure— and this case is about the marks, so the face is in the disk and the log has to say
  // that it was read (docs/30, docs/45).
  TEST_ASSERT_TRUE(storage->mkdir("/apps/timeclock/res"));
  TEST_ASSERT_TRUE(
      copy_a_host_file(storage, "apps/timeclock/res/keys.ttf", "/apps/timeclock/res/keys.ttf"));

  // **The app says where its controls are** when it is asked with `keys`, so the finger of the test goes where the page
  // drew them and not where a layout of this file guessed they would be (docs/30, docs/85).
  kernel::log_echo(true);
  pump_captured(60, g_out, sizeof(g_out), "run timeclock keys");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "timeclock: three keys"), g_out);
  // **The day it opens with**: today, with nothing on it, the two numbers at zero and the state read from the marks.
  TEST_ASSERT_NOT_NULL_MESSAGE(
      strstr(g_out, "timeclock: day 2026-11-01, 0 marks, work 0h 00m, rest 0h 00m, Not started today"), g_out);
  // The page is washed whole and once: 540x912 is the view of this board under the bar of the system (docs/50).
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "540x912"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "attempt to"), g_out);

  struct Spot {
    char name[8];
    int x;
    int y;
    int w;
    int h;
  };
  Spot spots[8] = {};
  int spot_count = 0;
  for (const char* at = strstr(g_out, "Timeclock "); at != nullptr && spot_count < 8;
       at = strstr(at + 1, "Timeclock ")) {
    char name[8] = {0};
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    if (sscanf(at, "Timeclock %7s %d %d %d %d", name, &x, &y, &w, &h) != 5) continue;
    snprintf(spots[spot_count].name, sizeof(spots[0].name), "%s", name);
    spots[spot_count].x = x;
    spots[spot_count].y = y;
    spots[spot_count].w = w;
    spots[spot_count].h = h;
    ++spot_count;
  }
  // The three keys, the eye and the box of the note: five controls, and the row of the list and the chart are the
  // other two.
  TEST_ASSERT_EQUAL_INT_MESSAGE(6, spot_count, g_out);

  const kernel::ui::Canvas& view = kernel::ui::canvas();

  // **The three keys are figures and not words** (0.173.0): the face is in the disk, so the line that says the keys say
  // the words is not in the log, and the ink of each key is a shape at its middle —the play triangle 35 x 39 px, the two
  // bars, the square— and not a blank rectangle. The numbers are the count of level 0 pixels inside the middle of the
  // key, where a mark of forty-odd pixels and no letter of a word of the fallback paints (docs/30, docs/45, docs/85).
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "the keys say words"), g_out);
  emu::MemorySurface* glass = emu::memory_surface();
  TEST_ASSERT_NOT_NULL(glass);
  static const char* const kPainted[] = {"start", "pause", "stop"};
  for (size_t key = 0; key < sizeof(kPainted) / sizeof(kPainted[0]); ++key) {
    int inked = 0;
    for (int i = 0; i < spot_count; ++i) {
      if (strcmp(spots[i].name, kPainted[key]) != 0) continue;
      for (int y = spots[i].y - 22; y <= spots[i].y + 22; ++y) {
        for (int x = spots[i].x - 30; x <= spots[i].x + 30; ++x) {
          if (glass->level_at(static_cast<uint16_t>(view.x + x), static_cast<uint16_t>(view.y + y)) == 0) ++inked;
        }
      }
    }
    TEST_ASSERT_GREATER_THAN_MESSAGE(150, inked, kPainted[key]);
  }

  // A control is pressed where the app says it is, and the panel counts in the frame of the page: the point of the view
  // is the point of the glass plus where the view sits under the bar (docs/30, docs/50).
  int taps_since_a_pause = 0;
  auto press = [&](const char* want) {
    for (int i = 0; i < spot_count; ++i) {
      if (strcmp(spots[i].name, want) != 0) continue;
      if (!panel->finger_down(0, static_cast<uint16_t>(view.x + spots[i].x),
                              static_cast<uint16_t>(view.y + spots[i].y))) {
        return false;
      }
      pump_turns(60);
      if (!panel->finger_up(0)) return false;
      // **Every second key waits out the window of the double and the triple** (0.136.0): three taps inside three hundred
      // and fifty milliseconds are a `triple` of the glass, which a key of this page does not read as a press, and the
      // third key of this case was the one that never arrived (docs/30, docs/92).
      if (++taps_since_a_pause >= 2) {
        taps_since_a_pause = 0;
        pump_turns(400);
      } else {
        pump_turns(60);
      }
      return true;
    }
    return false;
  };

  // **The play key opens the day**: a mark of the kind the file names, at the hour of the clock and not at any other.
  capture_begin();
  TEST_ASSERT_TRUE(press("start"));
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "timeclock: mark Start at 09:00"), g_out);

  // And it is in the file, in the format the paper project writes: one line of twenty-four bytes, the day, the hour
  // and the kind, and the file only ever grows (docs/45, docs/70).
  static char marks[256];
  TEST_ASSERT_TRUE_MESSAGE(read_text(storage, "/data/timeclock/marks.txt", marks, sizeof(marks)),
                           "the mark of the play key is not on the disk");
  TEST_ASSERT_EQUAL_STRING("2026-11-01|09:00:00|S\n", marks);

  // **The same key twice is pause and resume**, which is the one thing the middle key does: it says what the day is.
  capture_begin();
  TEST_ASSERT_TRUE(press("pause"));
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "timeclock: mark Pause at 09:00"), g_out);

  capture_begin();
  TEST_ASSERT_TRUE(press("pause"));
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "timeclock: mark Resume at 09:00"), g_out);

  // **The eye turns the sheet**: the week is read when it is the one that is about to be shown, and it says its five
  // days and the average of the days that have marks (the Sunday of this clock is not one of them).
  capture_begin();
  TEST_ASSERT_TRUE(press("eye"));
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "timeclock: week 10/26-10/30, avg 0h 00m in 0 day(s)"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "attempt to"), g_out);

  // And the same control, in the same place, brings the day back —and reads it again, because minutes may have passed
  // under the chart.
  capture_begin();
  TEST_ASSERT_TRUE(press("eye"));
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "timeclock: day 2026-11-01, 3 marks"), g_out);

  kernel::shell::close_app();
  pump_captured(10, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  kernel::log_echo(false);

  storage->remove("/apps/timeclock/timeclock.lua");
  storage->remove("/apps/timeclock/manifest.json");
  storage->remove("/apps/timeclock/res/keys.ttf");
  storage->remove("/apps/timeclock/res");
  storage->remove("/apps/timeclock");
  storage->remove("/data/timeclock/marks.txt");
  storage->remove("/data/timeclock/notes.txt");
  storage->remove("/data/timeclock");

  // The next test gets the default emulator back: the glass and the panel were this test's.
  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  opts.touch = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **The reader turns its page with a finger and with the keys after the centre** (0.140.0 and 0.141.0, docs/46 C3, C8,
// C10 and C13): the app of `apps/reader` on a real disk with a book and the two faces of the book, the glass and the touch
// of the model, and the position the app keeps in its own file as the proof of which page is on the glass. The pass that
// turns a text page over a text page is a `du` —mode 1 of the panel— and a page that comes after one with a plate takes
// the wash (docs/50).
void test_the_reader_turns_its_page_with_a_finger_and_with_the_keys(void) {
  TEST_ASSERT_TRUE(boot_once());
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  opts.touch = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());
  TEST_ASSERT_NOT_NULL(hal::platform().touch());
  TEST_ASSERT_TRUE(kernel::touch::boot(hal::platform()));
  emu::Gt911Panel* panel = emu::touch_panel();
  TEST_ASSERT_NOT_NULL(panel);

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(storage->mkdir("/apps/reader"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/reader/reader.lua", "/apps/reader/reader.lua"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/reader/manifest.json", "/apps/reader/manifest.json"));
  // **And its three modules** (0.149.0, docs/46 D6): the reader is `require`s of `library`, `chapters` and `notice`,
  // and a disk with only the entry would run an app whose first line raises.
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/reader/library.lua", "/apps/reader/library.lua"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/reader/chapters.lua", "/apps/reader/chapters.lua"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/reader/notice.lua", "/apps/reader/notice.lua"));
  // **The book and the two faces of the book are the ones of the repository** (docs/46, C13): a page that reads its
  // words in the serif and its titles in the sans.
  TEST_ASSERT_TRUE(storage->mkdir("/epub"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "data/epub/la-vaca-cega.epub", "/epub/la-vaca-cega.epub"));
  TEST_ASSERT_TRUE(copy_a_face(storage, "dejavuserif-latin1.ttf"));
  TEST_ASSERT_TRUE(copy_a_face(storage, "dejavusans-latin1.ttf"));

  // **The book opens through the loading page** (docs/46, C8): the turn that says which book is being opened and the one
  // after it, which reads the chapter and paints the page.
  kernel::log_echo(true);
  pump_captured(300, g_out, sizeof(g_out), "run reader /littlefs/epub/la-vaca-cega.epub");
  // **The page is painted on the whole view the system gave it** (docs/46, D1). This case used to look for a line
  // the app printed with the size of its view (`whole glass: the view is 540x960`), which no line of `apps/reader`
  // writes and has not written since 0.151.11 moved the whole glass to the person's choice: the size is taken from
  // the canvas the app ended with and the paint that covers it is what the log has to show (0.152.5).
  char whole_glass[64];
  snprintf(whole_glass, sizeof(whole_glass), "paint 0,0 %ux%u", kernel::ui::canvas().width,
           kernel::ui::canvas().height);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, whole_glass), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "attempt to"), g_out);
  // **And its four files are on the disk** (0.149.0, docs/46 D6): the reader asks for `library`, `chapters` and
  // `notice` before it draws anything, and a module that is not there says `require:` in the log.
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "require:"), g_out);
  // **And the serif was on the disk**: a book read in the face of the system says so, and this test wants the two faces.
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "reader: no serif face"), g_out);
  // **The title and the spine are the book's, from its OPF** (docs/46, C2): the sample says its own title and the two
  // chapters the spine orders, which is not the same thing as the names of its files. **And its index** (C14) comes from
  // the `toc.ncx` of the book, and **its cover** (C7) from the item the metadata names.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "reader: La vaca cega, 2 chapter(s), index 2 (ncx), cover yes"), g_out);
  // **And the cover decodes**: a picture that cannot be read leaves its page in paper and says so in the log.
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "reader: the cover cannot be read"), g_out);

  // **And the cache of the book is beside the book, hidden and with its name** (docs/46, C11):
  // `.<name>.epub.cache.ics` in the folder of the `.epub`, an `ICS1` of the cover scaled to 64 pixels —48 × 64 for the
  // cover of the sample— that this library reads back as a picture.
  static uint8_t cache[4096];
  hal::FileSlot cache_slot;
  TEST_ASSERT_TRUE_MESSAGE(storage->open("/epub/.la-vaca-cega.epub.cache.ics", hal::OpenMode::kRead, cache_slot),
                           "the cache of the book is not beside the book");
  const int32_t cache_len = storage->read(cache_slot, cache, sizeof(cache));
  storage->close(cache_slot);
  TEST_ASSERT_GREATER_THAN(8, cache_len);
  img::Info cache_info;
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(img::read_info(cache, static_cast<uint32_t>(cache_len), cache_info)));
  TEST_ASSERT_EQUAL_STRING("ICS1", cache_info.format);
  TEST_ASSERT_EQUAL_UINT16(48, cache_info.width);
  TEST_ASSERT_EQUAL_UINT16(64, cache_info.height);

  const char* path = "/data/reader/reading.txt";
  char where[256] = {0};
  // **The line of a position is `path TAB chapter TAB byte TAB cover`** (docs/46, C6 and C7): the byte is the third
  // field and the cover flag the fourth, so the last tab of the line is the cover and not the byte.
  auto byte_of = [&]() {
    if (!read_text(storage, path, where, sizeof(where))) return -1;
    const char* last = strrchr(where, '\t');
    if (last == nullptr) return -1;
    const char* before = nullptr;
    for (const char* at = where; at < last; ++at) {
      if (*at == '\t') before = at;
    }
    return (before == nullptr) ? -1 : atoi(before + 1);
  };
  auto cover_of = [&]() {
    if (!read_text(storage, path, where, sizeof(where))) return -1;
    const char* last = strrchr(where, '\t');
    return (last == nullptr) ? -1 : atoi(last + 1);
  };
  // **A tap is counted and only said once the window of the double has closed** (docs/30): the finger is down sixty
  // milliseconds —well inside the four hundred of a tap— and the kernel and the app get half a second after it.
  auto tap = [&](int x, int y) {
    if (!panel->finger_down(0, static_cast<uint16_t>(x), static_cast<uint16_t>(y))) return false;
    pump_turns(60);
    if (!panel->finger_up(0)) return false;
    pump_turns(500);
    return true;
  };

  // **The first page of the book is the byte zero**, and the app says so in its own file (docs/46, C6).
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, byte_of(), where);

  // **A tap on the right half is the page after this one** (docs/46, C3).
  TEST_ASSERT_TRUE(tap(400, 400));
  const int second = byte_of();
  TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, second, where);

  // **And a page of text over a page of text is the two-level pass** (docs/46, C10): the first page of this chapter has
  // the plate of the book, so the second takes the wash and the third —two pages of words— takes the `du`.
  capture_begin();
  TEST_ASSERT_TRUE(tap(400, 400));
  capture_end(g_out, sizeof(g_out));
  const int third = byte_of();
  TEST_ASSERT_GREATER_THAN_INT_MESSAGE(second, third, where);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "mode 1 du"), g_out);

  // **A tap on the left half is the page before this one**, twice back to the byte zero.
  TEST_ASSERT_TRUE(tap(100, 400));
  TEST_ASSERT_EQUAL_INT_MESSAGE(second, byte_of(), where);
  TEST_ASSERT_TRUE(tap(100, 400));
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, byte_of(), where);

  // **The cover is a state and not a byte** (docs/46, C7): from the first page of the first chapter, a tap on the left
  // half is the cover; it is saved with the position, and a tap on the right half is the first page again. The byte is
  // zero in both, which is the whole reason the state exists.
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, cover_of(), where);
  TEST_ASSERT_TRUE(tap(100, 400));
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, cover_of(), where);
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, byte_of(), where);
  TEST_ASSERT_TRUE(tap(400, 400));
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, cover_of(), where);
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, byte_of(), where);

  // **The keys of the board are the page's only after the centre enters it** (0.141.0, docs/46 C3): the gate the slider
  // has. Before the centre the arrows are the walk of the dial, and this page carries no control to walk, so nothing
  // moves; after it, up goes back and down goes forward.
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), "the reader died before the keys");
  TEST_ASSERT_NOT_NULL_MESSAGE(hal::platform().input(), "the emulator has no keys at all");
  TEST_ASSERT_EQUAL_INT_MESSAGE(3, (int)hal::platform().input()->count(), "the paper has three keys");
  emu::press_key(2, true);
  TEST_ASSERT_TRUE_MESSAGE(hal::platform().input()->pressed(2), "the emulator did not see the key go down");
  emu::press_key(2, false);
  TEST_ASSERT_FALSE_MESSAGE(hal::platform().input()->pressed(2), "the emulator did not see the key come up");
  auto press = [&](int key) {
    emu::press_key(key, true);
    pump_turns(60);
    emu::press_key(key, false);
    pump_turns(60);
  };
  press(2);
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, byte_of(), where);

  press(0);  // the centre enters the page
  capture_begin();
  press(2);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_EQUAL_INT_MESSAGE(second, byte_of(), g_out);
  press(1);
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, byte_of(), where);

  // **And the centre leaves it again**: the window of the three presses that close an app is a second and a half, so a
  // reading pause is more than that, and the arrow is the walk of the dial once more (docs/30).
  pump_turns(1600);
  press(0);
  press(2);
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, byte_of(), where);

  // **The menu is reachable from the keys** (0.145.0, docs/46 C4 and C15): a long press of the centre opens it with the
  // Book unfolded, the two arrows walk Shelf, Contents and Page, and the centre chooses. **Page** puts up the pages of
  // the chapter and its second row is the second page of the book — the number is walked and not typed.
  emu::press_key(0, true);
  pump_turns(1100);
  emu::press_key(0, false);
  pump_turns(120);
  // The console of the whole walk is what a failure has to show: the list answers "There is no page to choose" when
  // the count of the chapter has not landed, and that is a different fault from a row that opened the wrong page.
  capture_begin();
  press(2);  // Shelf -> Contents
  press(2);  // Contents -> Page
  press(0);  // the centre chooses Page
  // **The list is a stop of the dial before its rows are** (docs/30, `ui.list` with entries: «una llista de sis files
  // té sis parades a més de la pròpia llista»). The dial comes from the menu bar, which has just been released, so the
  // first arrow falls on the list, the second walks into the first row and the third into the second —which is the
  // row of page 2—. This case had two arrows and opened page 1, and nobody saw it because it stopped at a line the
  // app has not printed since 0.151.11 (0.152.5).
  press(2);  // the dial falls on the list
  press(2);  // into the first row
  press(2);  // and this one to the second row
  press(0);  // the centre opens page 2
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_EQUAL_INT_MESSAGE(second, byte_of(), g_out);

  kernel::shell::close_app();
  pump_captured(10, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  kernel::log_echo(false);

  storage->remove("/apps/reader/reader.lua");
  storage->remove("/apps/reader/manifest.json");
  storage->remove("/apps/reader/library.lua");
  storage->remove("/apps/reader/chapters.lua");
  storage->remove("/apps/reader/notice.lua");
  storage->remove("/apps/reader");
  storage->remove("/epub/.la-vaca-cega.epub.cache.ics");
  storage->remove("/epub/la-vaca-cega.epub");
  storage->remove("/epub");
  storage->remove("/data/reader/reading.txt");
  storage->remove("/data/reader");

  // The next test gets the default emulator back: the glass and the panel were this test's.
  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  opts.touch = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **An app can be more than one file** (0.148.0, docs/46 D6): `require('part')` reads `part.lua` from the folder the
// app was installed in —the flash, here—, runs it once and hands back what it returned; and a name that climbs out of
// that folder is refused, so a module of another app is not something an app can reach by asking for it.
void test_an_app_reads_its_own_modules_and_no_one_elses(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(add_app(storage, "split",
                           "{\"title\":\"Split\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local part = require('part')\n"
                           "local again = require('part')\n"
                           "console.print('split: ' .. part.word .. ', same ' .. tostring(again == part))\n"
                           "console.print('climb: ' .. tostring(pcall(require, '../other/secret')))\n"
                           "while true do sys.delay(20):await() end\n"));
  TEST_ASSERT_TRUE(write_text(storage, "/apps/split/part.lua", "return { word = 'half' }"));
  // Another app with a file of its own: the climb above names it, and what this proves is that it is never read.
  TEST_ASSERT_TRUE(add_app(storage, "other",
                           "{\"title\":\"Other\",\"version\":\"0.1.0\",\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "console.print('other must not run')\n"));
  TEST_ASSERT_TRUE(write_text(storage, "/apps/other/secret.lua", "return { word = 'secret' }"));

  pump_captured(40, g_out, sizeof(g_out), "run split");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "split: half, same true"), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "other must not run"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "climb: false"), g_out);
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), g_out);

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  storage->remove("/apps/split/part.lua");
  storage->remove("/apps/split/app.lua");
  storage->remove("/apps/split/manifest.json");
  storage->remove("/apps/split");
  storage->remove("/apps/other/secret.lua");
  storage->remove("/apps/other/app.lua");
  storage->remove("/apps/other/manifest.json");
  storage->remove("/apps/other");
}

// **The shelf has the foot of a page** (0.147.0, docs/46 C1): the band of 84 rows at the bottom of the glass with the
// paper of the page, the line across its top, the books it has on the left and what the centre does on the right. **No
// strip of balls**: the grid of cells already says which page it is on at the bottom of its own box (docs/30, docs/90),
// so the shelf's own foot is the words and not a second counter of the same number.
void test_the_shelf_has_a_foot_of_its_own(void) {
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
  // **The folders may already be there**: `mkdir` answers false for one that is, and the case of the reader before
  // this one runs on the same disk and leaves `/apps/reader` and `/epub` behind, so the test that asks for them was
  // red only when the whole suite ran and green alone (0.152.5).
  storage->mkdir("/apps/reader");
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/reader/reader.lua", "/apps/reader/reader.lua"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/reader/manifest.json", "/apps/reader/manifest.json"));
  // **And its three modules** (0.149.0, docs/46 D6): the reader is `require`s of `library`, `chapters` and `notice`,
  // and a disk with only the entry would run an app whose first line raises.
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/reader/library.lua", "/apps/reader/library.lua"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/reader/chapters.lua", "/apps/reader/chapters.lua"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/reader/notice.lua", "/apps/reader/notice.lua"));
  storage->mkdir("/epub");
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "data/epub/la-vaca-cega.epub", "/epub/la-vaca-cega.epub"));

  // **The shelf is where the reader starts when the shell gives it no book** (docs/46, B10).
  pump_captured(200, g_out, sizeof(g_out), "run reader");
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), g_out);
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "attempt to"), g_out);

  emu::MemorySurface* glass = emu::memory_surface();
  TEST_ASSERT_NOT_NULL(glass);
  // The band starts 84 rows above the bottom edge —960 - 84 = 876— and its line is the second row of it, from x 16 to
  // x 523: sixteen pixels of paper on each side is what makes it a line and not the edge of the screen.
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, glass->level_at(270, 878), "the line across the foot of the shelf is not there");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(15, glass->level_at(8, 878), "the line of the foot reaches the edge of the glass");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(15, glass->level_at(10, 940), "the paper under the words of the foot is not paper");

  // **The two words of the band**: the books on the left and the help on the right, both in the ink of the page. A label
  // of a colour that hides it would pass every other check in this test.
  auto ink_in = [&](uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1) {
    int ink = 0;
    for (uint16_t y = y0; y < y1; ++y) {
      for (uint16_t x = x0; x < x1; ++x) {
        if (glass->level_at(x, y) == 0) ++ink;
      }
    }
    return ink;
  };
  TEST_ASSERT_GREATER_THAN_MESSAGE(20, ink_in(16, 324, 884, 914), "the foot of the shelf says no book");
  TEST_ASSERT_GREATER_THAN_MESSAGE(20, ink_in(324, 524, 884, 914), "the foot of the shelf says no help");
  // **And the words are under the cells and not on them**: the grid stops eight rows above the band, so the row of the
  // paper right under the grid is still paper.
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(15, glass->level_at(270, 872), "the cells of the shelf reach into the foot");

  kernel::shell::close_app();
  pump_captured(10, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());

  storage->remove("/apps/reader/reader.lua");
  storage->remove("/apps/reader/manifest.json");
  storage->remove("/apps/reader/library.lua");
  storage->remove("/apps/reader/chapters.lua");
  storage->remove("/apps/reader/notice.lua");
  storage->remove("/apps/reader");
  storage->remove("/epub/.la-vaca-cega.epub.cache.ics");
  storage->remove("/epub/la-vaca-cega.epub");
  storage->remove("/epub");

  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  opts.touch = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **The Shell app is the shell of the system in the glass** (0.162.0): the page on which a board with a pen and no
// keyboard can still type a line. What is measured from here is the half of it that belongs to the kernel —**the app
// that ships opens a session of the shell and not an interpreter of its own**— and that the session answers with what
// the shell answers: the line written into it is a command and the version of the kernel comes back.
//
// The page itself —the menu of File, the tabs and the cross— is a page of controls, and a control that is drawn is
// not a control that can be read from a test (`test_the_each_tab_of_a_strip_is_a_stop_of_the_dial` and the cases of
// the box are the ones that read the strip); what this case holds is that the app is a client of the shell and not a
// second shell (docs/30, docs/85).
void test_the_shell_app_is_a_page_with_a_session_of_the_shell(void) {
  TEST_ASSERT_TRUE(boot_once());

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);

  // The glass and the panel of the model, because an app that needs a glass says so and stops.
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  opts.touch = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());
  TEST_ASSERT_NOT_NULL(hal::platform().touch());
  TEST_ASSERT_TRUE(kernel::touch::boot(hal::platform()));

  // **The app that ships is the app the test runs**: the two files come from the repository and not from a copy
  // that goes stale.
  TEST_ASSERT_TRUE(storage->mkdir("/apps/shell"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/shell/shell.lua", "/apps/shell/shell.lua"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/shell/manifest.json", "/apps/shell/manifest.json"));

  pump_captured(40, g_out, sizeof(g_out), "run shell");
  TEST_ASSERT_EQUAL_STRING("shell", kernel::shell::app_id());

  // **One tab is one session**: the ids of a session are one based and zero is the console, so the first session of
  // an app is the first tab of its page. A page that never asked for one is a page with an empty box.
  TEST_ASSERT_TRUE_MESSAGE(kernel::shell::session_open(1), "the page opened a session");

  // And it is the shell that answers: `version` is a command of the shell and what comes back is the version of the
  // kernel, so a session that were anything else would not answer this line.
  TEST_ASSERT_TRUE(kernel::shell::session_write(1, "version\n", 8));
  char said[256] = {0};
  kernel::shell::session_read(1, said, sizeof(said) - 1);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, kernel::kVersion), said);

  // **A session left open by an app that went away is a session nobody reads**, and the second tab of the page would
  // be refused with `E_BUSY`. `exit` is how a shell is left and it closes the session whatever the page does next
  // (docs/30).
  TEST_ASSERT_TRUE(kernel::shell::session_write(1, "exit\n", 5));
  TEST_ASSERT_FALSE_MESSAGE(kernel::shell::session_open(1), "the session was left open");

  kernel::shell::close_app();
  storage->remove("/apps/shell/shell.lua");
  storage->remove("/apps/shell/manifest.json");
  storage->remove("/apps/shell");

  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  opts.touch = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **A pinch from big to small closes** (0.163.0). The gesture is the system's and it has three wishes in it: an app
// with views on top of its first one gives the last of them back and hears `on_back(depth)`; an app on its only page
// is closed; and a launcher is not, because a board with no page at all is a board a person reaches with a cable.
//
// The pinch is made the way a person makes it: two fingers eighty pixels either side of the middle of the page, four
// steps of fifteen pixels each coming back, and the first one that leaves the glass is where the pair says what it did
// (`pinch`, with `spread` at minus a hundred and twenty). What the test measures is the two numbers the system reads
// —`apps::views()`— and the app that is still there and the one that is gone (docs/30, docs/46).
void test_a_pinch_from_big_to_small_goes_back_and_then_closes(void) {
  TEST_ASSERT_TRUE(boot_once());

  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);

  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  opts.touch = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());
  TEST_ASSERT_NOT_NULL(hal::platform().touch());
  TEST_ASSERT_TRUE(kernel::touch::boot(hal::platform()));
  emu::Gt911Panel* panel = emu::touch_panel();
  TEST_ASSERT_NOT_NULL(panel);

  // An app with one view on top of its only page, and the hook that puts the page below it back.
  static const char kPage[] =
      "local depth = app.view_push()\n"
      "console.print('pinchy view ' .. depth)\n"
      "function on_back(left) console.print('pinchy back ' .. left) end\n"
      "while true do sys.delay(20):await() end\n";
  TEST_ASSERT_TRUE(add_app(storage, "pinchy",
                           "{\"title\":\"Pinchy\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           kPage));
  // And a launcher, which is the page that cannot be closed.
  TEST_ASSERT_TRUE(add_app(storage, "stay",
                           "{\"title\":\"Stay\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"],\"launcher\":true}",
                           "console.print('stay up')\nwhile true do sys.delay(20):await() end\n"));

  pump_captured(20, g_out, sizeof(g_out), "run pinchy");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "pinchy view 1"), g_out);
  TEST_ASSERT_EQUAL_UINT8(1, kernel::apps::views());

  const kernel::ui::Canvas& view = kernel::ui::canvas();
  const uint16_t mid_x = static_cast<uint16_t>(view.x + view.width / 2);
  const uint16_t mid_y = static_cast<uint16_t>(view.y + view.height / 2);

  // **Two fingers that come together**: eighty pixels either side of the middle, down to twenty, in four steps of
  // fifteen —a hundred and twenty of `spread`, which is twice the sixty the system asks for— and the pair only says
  // what it did when the first of them leaves the glass.
  auto pinch = [&]() {
    TEST_ASSERT_TRUE(panel->finger_down(0, static_cast<uint16_t>(mid_x - 80), mid_y));
    TEST_ASSERT_TRUE(panel->finger_down(1, static_cast<uint16_t>(mid_x + 80), mid_y));
    pump_turns(10);
    for (int step = 0; step < 4; ++step) {
      const uint16_t gap = static_cast<uint16_t>(80 - 15 * (step + 1));
      TEST_ASSERT_TRUE(panel->finger_move(0, static_cast<uint16_t>(mid_x - gap), mid_y));
      TEST_ASSERT_TRUE(panel->finger_move(1, static_cast<uint16_t>(mid_x + gap), mid_y));
      pump_turns(10);
    }
    TEST_ASSERT_TRUE(panel->finger_up(0));
    pump_turns(10);
    TEST_ASSERT_TRUE(panel->finger_up(1));
    pump_turns(10);
  };

  // **The view goes and the app stays**: the number the system reads drops to zero and the app is told, which is what
  // puts the page below it back on the glass.
  capture_begin();
  pinch();
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, kernel::apps::views(), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "pinchy back 0"), g_out);
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), "the first pinch took a view and not the app");

  // **And the second one is the app**, because there is nothing left on top of its only page.
  capture_begin();
  pinch();
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_FALSE_MESSAGE(kernel::app_running(), "the second pinch closed the app");

  // **A launcher does not close**: the gesture is taken and refused with a line, and the page is still there.
  pump_captured(20, g_out, sizeof(g_out), "run stay");
  TEST_ASSERT_TRUE(kernel::app_running());
  capture_begin();
  pinch();
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), "a launcher stays where it is");
  // **The sentence is the one road out of a page says** (0.167.0): the pinch, the three presses of the middle key and the
  // three taps on the glass all go through the same door, and a launcher is the page that is not left (docs/30, docs/90).
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "is a launcher: it is the one page that is not left"), g_out);

  // **And the middle key goes by the same road** (0.163.1): its three taps on an app with a view give the view back and
  // keep the app, and only the next three close it. The page of this case has no control on it, so the middle key
  // counts taps and is not an Enter (docs/30).
  kernel::shell::close_app();
  pump_captured(20, g_out, sizeof(g_out), "run pinchy");
  TEST_ASSERT_EQUAL_UINT8(1, kernel::apps::views());

  auto three_taps = [&]() {
    for (int tap = 0; tap < 3; ++tap) {
      emu::press_button(true);
      pump_turns(10);
      emu::press_button(false);
      pump_turns(10);
    }
  };

  capture_begin();
  three_taps();
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, kernel::apps::views(), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "button: three taps, back one view"), g_out);
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), "three taps took a view and not the app");

  capture_begin();
  three_taps();
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "button: three taps, closing the app"), g_out);
  TEST_ASSERT_FALSE_MESSAGE(kernel::app_running(), "the second three taps closed the app");

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());

  storage->remove("/apps/pinchy/app.lua");
  storage->remove("/apps/pinchy/manifest.json");
  storage->remove("/apps/pinchy");
  storage->remove("/apps/stay/app.lua");
  storage->remove("/apps/stay/manifest.json");
  storage->remove("/apps/stay");

  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  opts.touch = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **A strip that closes its tabs draws its crosses** (0.163.2): `closeable = true` is a field the painter has to read
// **while the fields table is still on the stack**. It was read after the pops —on whatever was under it— and every
// app with a strip that can be closed died on its first present with `attempt to index a boolean value`, which is
// what the shell app did on the emulator on 8/11. The case runs a page with two tabs and a cross and asks for the
// present; a page that dies leaves no `present true` in the log (docs/30, docs/46).
void test_a_strip_that_closes_its_tabs_draws_its_crosses(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);

  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());

  static const char kApp[] =
      "local v = ui.caps()\n"
      "local s = ui.tabs(v.width - 40, 60)\n"
      "s:place(20, 20)\n"
      "s.items = { 'One', 'Two' }\n"
      "s.closeable = true\n"
      "console.print('strip ready')\n"
      "console.print('present ' .. tostring(ui.present('gc16')))\n"
      "while true do sys.delay(50):await() end\n";
  TEST_ASSERT_TRUE(add_app(storage, "strip",
                           "{\"title\":\"Strip\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           kApp));

  pump_captured(20, g_out, sizeof(g_out), "run strip");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "strip ready"), g_out);
  // **The present goes through the painter of the crosses**: a strip whose tabs can be closed is drawn by the call
  // that paints them, and the answer is `true` when the glass took the page.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "present true"), g_out);
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), "the page with a strip stayed up");

  kernel::shell::close_app();
  pump_captured(5, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());

  storage->remove("/apps/strip/app.lua");
  storage->remove("/apps/strip/manifest.json");
  storage->remove("/apps/strip");

  opts = emu::options();
  opts.board = nullptr;
  opts.display = false;
  emu::set_options(opts);
  kernel::boot(hal::platform());
}

// **`LUNOK_TEST=<name>` runs one case** (0.152.5): the suite is a minute long, and hunting one red case at a
// time meant a minute for every question asked of it. Unity has no filter of its own, so the one `RUN_TEST` it
// gives is replaced here by one that asks the environment first; without the variable every case runs as before
// and the summary counts what ran, not what was skipped.
static bool runs_now(const char* name) {
  const char* only = getenv("LUNOK_TEST");
  return only == nullptr || only[0] == '\0' || strcmp(only, name) == 0;
}

#undef RUN_TEST
#define RUN_TEST(func)                             \
  do {                                             \
    if (runs_now(#func)) {                         \
      UnityDefaultTestRun(func, #func, __LINE__);  \
    }                                              \
  } while (0)

// **The hour of the person, with a finger on a map of the world** (0.170.0): the app of the time zone reads the offset,
// the leap day and the first day of the week from the system, a finger on the map picks a band of fifteen degrees, and
// the offset is written **when the app closes and not while it is used** —which is the owner's rule— because `time.tz`
// writes the setting at once (docs/30, docs/60, docs/90).
void test_the_time_zone_app_picks_a_zone_and_saves_it_on_the_close(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);

  // The glass and the panel are the model's, and the face of the disk is the one the page is measured with.
  TEST_ASSERT_TRUE(copy_a_face(storage, "dejavusans-bold.ttf"));
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  opts.touch = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());
  TEST_ASSERT_NOT_NULL(hal::platform().touch());
  TEST_ASSERT_TRUE(kernel::touch::boot(hal::platform()));
  emu::Gt911Panel* panel = emu::touch_panel();
  TEST_ASSERT_NOT_NULL(panel);

  // **The app that ships is the app the test runs**, its manifest and the picture of the map (docs/85).
  TEST_ASSERT_TRUE(storage->mkdir("/apps/tz"));
  TEST_ASSERT_TRUE(storage->mkdir("/apps/tz/res"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/tz/tz.lua", "/apps/tz/tz.lua"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/tz/manifest.json", "/apps/tz/manifest.json"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/tz/res/world.png", "/apps/tz/res/world.png"));

  // **A board with no network saved, so the button of the time server answers the same thing every time** (0.174.0):
  // the radio is there and its store is empty, which is the state of a board nobody has provisioned and the one that
  // made the app die —a refusal arrives as a raise and not as a `nil`— (docs/30, docs/85). The forget has to have
  // happened, or the case would be reading the network another case of this suite left behind (0.152.4).
  char forgot[512];
  pump_captured(300, forgot, sizeof(forgot), "wifi forget");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(forgot, "network: none"), forgot);

  // **Where the map, the hours and the three controls are, from the app** (docs/30, docs/85): a test that knew the
  // layout would be a test that a change of layout moves onto another zone without saying so.
  kernel::log_echo(true);
  pump_captured(120, g_out, sizeof(g_out), "run tz zones");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "tz map 20 46 500 250"), g_out);

  struct Place {
    char what[16];
    int x;
    int y;
    int w;
    int h;
  };
  Place places[40] = {};
  int count = 0;
  for (const char* at = strstr(g_out, "tz "); at != nullptr && count < 40; at = strstr(at + 3, "tz ")) {
    Place one = {};
    const int read = sscanf(at, "tz %15s %d %d %d %d", one.what, &one.x, &one.y, &one.w, &one.h);
    // **A line of an hour is `tz hour <hour> <x> <y>` and a box is `tz <word> <x> <y> <w> <h>`**: the shapes are told
    // apart by how many numbers `sscanf` took, and the three of an hour leave the place of the finger in `x` and `y`
    // (docs/85).
    if (read == 4 && strcmp(one.what, "hour") == 0) {
      // `what`, the hour and the two pixels: the hour is not a place of the page.
      one.x = one.y;
      one.y = one.w;
      one.w = 0;
      one.h = 0;
    } else if (read == 4 && strcmp(one.what, "leap") == 0) {
      // `what`, the place and one side of it: the checkbox is a square and the app says one side.
      one.h = one.w;
    } else if (read != 5) {
      continue;
    }
    places[count++] = one;
  }
  auto place = [&](const char* what) -> const Place* {
    for (int i = 0; i < count; ++i) {
      if (strcmp(places[i].what, what) == 0) return &places[i];
    }
    return nullptr;
  };
  // **And the page says which controls take the ring** (0.174.0): three lines of `tz stop true`, one per setting, which
  // is what makes this page different from the one the owner saw —there, nothing was focusable because nothing was a
  // control— (docs/30).
  int stops = 0;
  for (const char* at = strstr(g_out, "tz stop true"); at != nullptr; at = strstr(at + 12, "tz stop true")) ++stops;
  TEST_ASSERT_EQUAL_INT_MESSAGE(3, stops, g_out);
  const Place* hour_three = place("hour");
  // The first `tz hour` line is -12 and the sixteenth is +3: the app prints one line per hour.
  int seen = 0;
  for (int i = 0; i < count; ++i) {
    if (strcmp(places[i].what, "hour") != 0) continue;
    if (++seen == 16) hour_three = &places[i];
  }
  TEST_ASSERT_NOT_NULL_MESSAGE(place("sync"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(place("leap"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(place("week"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(place("list"), g_out);
  TEST_ASSERT_EQUAL_INT_MESSAGE(25, seen, g_out);
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), g_out);

  const kernel::ui::Canvas& view = kernel::ui::canvas();
  auto tap = [&](int x, int y) {
    if (!panel->finger_down(0, static_cast<uint16_t>(view.x + x), static_cast<uint16_t>(view.y + y))) return false;
    pump_turns(40);
    if (!panel->finger_up(0)) return false;
    pump_turns(400);
    return true;
  };

  // **Nothing has touched a setting yet**: the page is only holding the hour it read.
  TEST_ASSERT_TRUE_MESSAGE(kernel::cfg::leap_years(), "the leap day went off with no finger on it");

  // **The dial is the system's again** (0.174.0): the page reads no key any more, and the ring is on the first control
  // of the page —the page handed the focus to the button of the time server— so one step of the right key takes it to the
  // checkbox and the centre is Enter on it: the app hears `on_change` and not a key (docs/30). It comes before the finger
  // because **a tap on the picture is a tap on no control and the system takes the ring away with it**, which is its rule
  // for the empty part of a page.
  capture_begin();
  emu::press_key(2, true);
  pump_turns(20);
  emu::press_key(2, false);
  pump_turns(20);
  emu::press_button(true);
  pump_turns(20);
  emu::press_button(false);
  pump_turns(60);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "tz: leap off"), g_out);
  TEST_ASSERT_FALSE_MESSAGE(kernel::cfg::leap_years(), "the centre did not press the box the ring was on");

  // **A finger on the band of +3** picks it and says so, and nothing is written yet: the page is holding it.
  TEST_ASSERT_NOT_NULL(hour_three);
  const int before = kernel::cfg::tz_minutes();
  capture_begin();
  TEST_ASSERT_TRUE(tap(hour_three->x, hour_three->y));
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "tz: picked +03:00"), g_out);
  TEST_ASSERT_EQUAL_INT_MESSAGE(before, kernel::cfg::tz_minutes(), "the page saved the hour before the close");
  TEST_ASSERT_FALSE_MESSAGE(kernel::cfg::leap_years(), "the map turned the leap day, and that is not its setting");

  // **The button of the time server is a `ui.button` of the system and it hears the finger** (0.174.0, the owner: *«el
  // botó de sync NTP tampoc sembla un botó de sistema»*): the click is a hook and a hook cannot wait, so the button leaves
  // the wish and the loop of the page asks the server, which is the rule this project has for a card's accept.
  const Place* sync = place("sync");
  TEST_ASSERT_NOT_NULL(sync);
  capture_begin();
  TEST_ASSERT_TRUE(tap(sync->x + sync->w / 2, sync->y + sync->h / 2));
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "tz: no clock: no network saved"), g_out);

  // **The leap day and the week are settings with an immediate effect**: a checkbox and a list are not a choice that
  // waits for the close, and the finger is the other road to the same box (docs/60).
  const Place* leap = place("leap");
  capture_begin();
  TEST_ASSERT_TRUE(tap(leap->x, leap->y));
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "tz: leap on"), g_out);
  TEST_ASSERT_TRUE_MESSAGE(kernel::cfg::leap_years(), "the finger did not turn the box back on");
  TEST_ASSERT_EQUAL_UINT8(29, kernel::calendar::days_in_month(2024, 2));

  const Place* week = place("week");
  capture_begin();
  TEST_ASSERT_TRUE(tap(week->x + week->w / 2, week->y + week->h / 2));
  capture_end(g_out, sizeof(g_out));
  // **The list of the two days is a `ui.menu` of the system** (0.174.0): the app says the card and the options, the system
  // draws the rows and takes the finger. The boxes inside a card are its own —a row is never under 52 pixels and the rows
  // grow into the room—, so the second option is the bottom third of the card and not an offset this test would have to
  // know (docs/30, docs/90).
  const Place* list = place("list");
  TEST_ASSERT_NOT_NULL(list);
  capture_begin();
  TEST_ASSERT_TRUE(tap(list->x + list->w / 2, list->y + (list->h * 2) / 3));
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "tz: week sunday"), g_out);
  TEST_ASSERT_EQUAL_UINT8(7, kernel::cfg::week_start());

  // **And the close is what saves the hour**: the shell takes the app away —which is the same road the pinch and `exit`
  // take—, `on_close` runs, and the setting is the one the page was holding. **The three taps of the centre are not the
  // way out of this page any more** (0.174.0): a page with focusable controls has the centre for Enter, which is the rule
  // of the system, so this case closes the app the way the pinch does (docs/30).
  capture_begin();
  kernel::shell::close_app();
  pump_turns(40);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_FALSE_MESSAGE(kernel::app_running(), "the app survived a close of the shell");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "tz: saved +03:00"), g_out);
  TEST_ASSERT_EQUAL_INT(180, kernel::cfg::tz_minutes());

  // And the prompt says the same thing the page said.
  pump_captured(20, g_out, sizeof(g_out), "cfg tz");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "settings: tz +03:00"), g_out);
  pump_captured(20, g_out, sizeof(g_out), "cfg week");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "settings: week sunday"), g_out);
  pump_captured(20, g_out, sizeof(g_out), "cfg leap");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "settings: leap on"), g_out);

  // The board goes back to the calendar it was born with, or the cases after this one read a Sunday.
  kernel::cfg::set_leap_years(true);
  kernel::cfg::set_week_start(1);
  kernel::cfg::set_tz_minutes(0);

  storage->remove("/apps/tz/res/world.png");
  storage->remove("/apps/tz/tz.lua");
  storage->remove("/apps/tz/manifest.json");
}

// **The eraser of the repository, on the disk of this run**: a case that failed before its clean-up leaves the folder
// behind, so the folder is taken away first and a second case cannot fail on the leftovers of the first (docs/85).
bool install_the_eraser(hal::IStorage* storage) {
  storage->remove("/apps/wipe/res/icon.png");
  storage->remove("/apps/wipe/res/icon-source.png");
  storage->remove("/apps/wipe/wipe.lua");
  storage->remove("/apps/wipe/manifest.json");
  storage->remove("/apps/wipe/res");
  storage->remove("/apps/wipe");
  if (!storage->mkdir("/apps/wipe")) return false;
  if (!storage->mkdir("/apps/wipe/res")) return false;
  if (!copy_a_host_file(storage, "apps/wipe/wipe.lua", "/apps/wipe/wipe.lua")) return false;
  if (!copy_a_host_file(storage, "apps/wipe/manifest.json", "/apps/wipe/manifest.json")) return false;
  return copy_a_host_file(storage, "apps/wipe/res/icon.png", "/apps/wipe/res/icon.png");
}

// **How many entries a volume has**: the format of the card is proved by the card being empty, and the card is a
// volume of folders the case wrote itself (docs/70).
class CountingSink : public hal::ListSink {
 public:
  bool on_entry(const hal::ListEntry& entry) override {
    (void)entry;
    ++count;
    return true;
  }
  int count = 0;
};

int entries_of(hal::IStorage* volume, const char* path) {
  if (volume == nullptr) return -1;
  CountingSink sink;
  if (!volume->list(path, sink)) return -1;
  return sink.count;
}

// **The page that empties the store and the card** (0.171.0): `fs.format` is the one door of the system that reaches
// outside the ground of an app —an app cannot write `/sd` and cannot touch `/nvs`— and it is behind a warning card whose
// default door is the safe one. What is proved here: nothing is written before the accept, the accept empties the store,
// and the card that ends offers a restart without taking it (docs/30, docs/70, docs/90).
void test_the_eraser_app_empties_the_store_after_the_card_is_accepted(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(copy_a_face(storage, "dejavusans-bold.ttf"));

  // The glass and the panel of the Paper, and no card in the slot: the door of the card is the one that is closed.
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  opts.touch = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());
  TEST_ASSERT_NOT_NULL(hal::platform().touch());
  TEST_ASSERT_TRUE(kernel::touch::boot(hal::platform()));
  emu::Gt911Panel* panel = emu::touch_panel();
  TEST_ASSERT_NOT_NULL(panel);

  // **The app that ships is the app the test runs**, with the picture of its icon (docs/85).
  TEST_ASSERT_TRUE(install_the_eraser(storage));

  // Two keys of the store, so that the erase has something to take and the count is a number and not a zero.
  hal::IKeyValue* kv = hal::platform().nvs();
  TEST_ASSERT_NOT_NULL(kv);
  const uint8_t on[2] = {'o', 'n'};
  TEST_ASSERT_TRUE(kv->set("eraser", "one", on, sizeof(on)));
  TEST_ASSERT_TRUE(kv->set("eraser", "two", on, sizeof(on)));

  // **Where the three doors are, said by the app** and not by this file (docs/30, docs/85).
  kernel::log_echo(true);
  pump_captured(120, g_out, sizeof(g_out), "run wipe boxes");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "wipe nvs "), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "wipe nvs open"), g_out);
  // **A board with an empty slot has a door that is closed** and a button that the dial walks past: the page says which
  // of the three it is, and the test reads it from there. **The flash is open on every board** (0.171.1).
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "wipe sd closed"), g_out);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "wipe lfs open"), g_out);

  int nvs_x = 0, nvs_y = 0, nvs_w = 0, nvs_h = 0;
  const char* nvs_line = strstr(g_out, "wipe nvs ");
  TEST_ASSERT_NOT_NULL(nvs_line);
  TEST_ASSERT_EQUAL_INT_MESSAGE(4, sscanf(nvs_line, "wipe nvs %d %d %d %d", &nvs_x, &nvs_y, &nvs_w, &nvs_h), nvs_line);
  TEST_ASSERT_TRUE_MESSAGE(nvs_w > 0 && nvs_h > 0, nvs_line);
  int lfs_x = 0, lfs_y = 0, lfs_w = 0, lfs_h = 0;
  const char* lfs_line = strstr(g_out, "wipe lfs ");
  TEST_ASSERT_NOT_NULL(lfs_line);
  TEST_ASSERT_EQUAL_INT_MESSAGE(4, sscanf(lfs_line, "wipe lfs %d %d %d %d", &lfs_x, &lfs_y, &lfs_w, &lfs_h), lfs_line);
  TEST_ASSERT_TRUE_MESSAGE(lfs_w > 0 && lfs_h > 0, lfs_line);

  const kernel::ui::Canvas& view = kernel::ui::canvas();
  auto tap = [&](int x, int y) {
    if (!panel->finger_down(0, static_cast<uint16_t>(view.x + x), static_cast<uint16_t>(view.y + y))) return false;
    pump_turns(40);
    if (!panel->finger_up(0)) return false;
    pump_turns(400);
    return true;
  };

  // **A finger on the door of the store opens the warning, and nothing is written yet**: the warning is the system's
  // card and the keys are still there until the accept.
  capture_begin();
  TEST_ASSERT_TRUE(tap(nvs_x + nvs_w / 2, nvs_y + nvs_h / 2));
  capture_end(g_out, sizeof(g_out));
  char value[8] = {0};
  size_t len = 0;
  TEST_ASSERT_TRUE_MESSAGE(kv->get("eraser", "one", value, sizeof(value), len), "the key went with no accept");
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), "the page died on the warning");

  // **The dial starts on the safe door and has to be walked to the other one**: one step forward and the centre, and
  // the store is empty.
  capture_begin();
  emu::press_key(2, true);
  pump_turns(20);
  emu::press_key(2, false);
  pump_turns(20);
  emu::press_button(true);
  pump_turns(20);
  emu::press_button(false);
  pump_turns(60);
  capture_end(g_out, sizeof(g_out));
  // **The count is the page's and not this case's**: the store of the suite has whatever the cases before it left, so the
  // number is read and what is proved is that it took the two keys of this one with it.
  int erased = 0;
  const char* erased_line = strstr(g_out, "wipe: nvs erased ");
  TEST_ASSERT_NOT_NULL_MESSAGE(erased_line, g_out);
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, sscanf(erased_line, "wipe: nvs erased %d", &erased), erased_line);
  TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(2, erased, erased_line);
  TEST_ASSERT_FALSE_MESSAGE(kv->get("eraser", "one", value, sizeof(value), len), "the key is still in the store");
  TEST_ASSERT_FALSE_MESSAGE(kv->get("eraser", "two", value, sizeof(value), len), "the key is still in the store");

  // **The card that ends says so and offers a restart that the person may refuse**: the dial is on `Restart` and one
  // step back is `Not now`, which closes the card and leaves the app where it was —a restart in the middle of a test
  // would end the board for every case after it— (docs/30).
  capture_begin();
  emu::press_key(1, true);
  pump_turns(20);
  emu::press_key(1, false);
  pump_turns(20);
  emu::press_button(true);
  pump_turns(20);
  emu::press_button(false);
  pump_turns(60);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), "the app went with the card of the end");
  TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "wipe: nvs erased"), "the erase ran twice");

  // **Three taps of the middle key inside a second and a half are the system's way out of an app** (`kTapsToClose`,
  // docs/30): this case has already given two of them —the accept and the `Not now`— and the third would close the page
  // instead of answering a card. The wait is the rule, not a convenience.
  pump_turns(1700);

  // **And the door of the flash is wired the same way and asks before it does anything** (0.171.1): the card opens, the
  // disk is still whole, and the safe door closes it with nothing lost. The erasing itself is not run here —it would take
  // the apps of this suite with it— and has its own case one level down, on the volume of `test_disk`.
  capture_begin();
  TEST_ASSERT_TRUE(tap(lfs_x + lfs_w / 2, lfs_y + lfs_h / 2));
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), "the app went with the tap of the flash");
  hal::Stat whole;
  TEST_ASSERT_TRUE_MESSAGE(storage->stat("/apps/wipe/wipe.lua", whole), "the flash went with no accept");
  // **The card opens with the dial on the safe door**, so the centre is the cancel and no arrow is needed.
  emu::press_button(true);
  pump_turns(20);
  emu::press_button(false);
  pump_turns(60);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_TRUE_MESSAGE(storage->stat("/apps/wipe/wipe.lua", whole), "the flash went with a cancel");
  TEST_ASSERT_TRUE_MESSAGE(kernel::app_running(), "the app went with the card of the flash");

  // And the page is closable: three taps of the centre take it away, which is the rule of every app (docs/30).
  for (int i = 0; i < 3 && kernel::app_running(); ++i) {
    emu::press_button(true);
    pump_turns(10);
    emu::press_button(false);
    pump_turns(10);
  }
  TEST_ASSERT_FALSE_MESSAGE(kernel::app_running(), "the app survived three taps of the centre");

  kv->erase("eraser", "one");
  kv->erase("eraser", "two");
  storage->remove("/apps/wipe/res/icon.png");
  storage->remove("/apps/wipe/wipe.lua");
  storage->remove("/apps/wipe/manifest.json");
  storage->remove("/apps/wipe/res");
  storage->remove("/apps/wipe");
}

// **The card, which an app may read and never write, is emptied by the door of the format and not by a file call**
// (0.171.0): the same page with a card in the slot has both doors open, and the accept of the card empties every file and
// every folder on it (docs/30, docs/70).
void test_the_eraser_app_empties_the_card_it_may_not_write(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_TRUE(copy_a_face(storage, "dejavusans-bold.ttf"));
  make_card();
  char card_path[96];
  snprintf(card_path, sizeof(card_path), "%s/sd", root());

  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  opts.touch = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());
  TEST_ASSERT_TRUE(card_on(card_path));
  TEST_ASSERT_NOT_NULL(hal::platform().touch());
  TEST_ASSERT_TRUE(kernel::touch::boot(hal::platform()));
  emu::Gt911Panel* panel = emu::touch_panel();
  TEST_ASSERT_NOT_NULL(panel);

  hal::IStorage* card = hal::platform().storage("/sd");
  TEST_ASSERT_NOT_NULL(card);
  TEST_ASSERT_TRUE(card->mkdir("/notes"));
  hal::FileSlot slot;
  TEST_ASSERT_TRUE(card->open("/notes/one.txt", hal::OpenMode::kWrite, slot));
  TEST_ASSERT_TRUE(card->write(slot, "one", 3) == 3);
  TEST_ASSERT_TRUE(card->close(slot));

  TEST_ASSERT_TRUE(install_the_eraser(storage));

  kernel::log_echo(true);
  pump_captured(120, g_out, sizeof(g_out), "run wipe boxes");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "wipe sd open"), g_out);
  int sd_x = 0, sd_y = 0, sd_w = 0, sd_h = 0;
  const char* sd_line = strstr(g_out, "wipe sd ");
  TEST_ASSERT_NOT_NULL(sd_line);
  TEST_ASSERT_EQUAL_INT_MESSAGE(4, sscanf(sd_line, "wipe sd %d %d %d %d", &sd_x, &sd_y, &sd_w, &sd_h), sd_line);

  const kernel::ui::Canvas& view = kernel::ui::canvas();
  auto tap = [&](int x, int y) {
    if (!panel->finger_down(0, static_cast<uint16_t>(view.x + x), static_cast<uint16_t>(view.y + y))) return false;
    pump_turns(40);
    if (!panel->finger_up(0)) return false;
    pump_turns(400);
    return true;
  };

  capture_begin();
  TEST_ASSERT_TRUE(tap(sd_x + sd_w / 2, sd_y + sd_h / 2));
  capture_end(g_out, sizeof(g_out));
  // Still whole: a warning that emptied the card when it opened would be no warning at all.
  hal::Stat info;
  TEST_ASSERT_TRUE_MESSAGE(card->stat("/notes/one.txt", info), "the card went with no accept");

  capture_begin();
  emu::press_key(2, true);
  pump_turns(20);
  emu::press_key(2, false);
  pump_turns(20);
  emu::press_button(true);
  pump_turns(20);
  emu::press_button(false);
  pump_turns(60);
  capture_end(g_out, sizeof(g_out));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "wipe: sd erased "), g_out);
  TEST_ASSERT_FALSE_MESSAGE(card->stat("/notes", info), "a folder survived the format of the card");
  TEST_ASSERT_FALSE_MESSAGE(card->stat("/notes/one.txt", info), "a file survived the format of the card");
  // **And the count of the page is the truth of the volume**: a card that says three and holds one is a card that lies.
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, entries_of(card, "/"), "the card of this test is not empty");

  // **The door of the format is the only one**: an app still may not write the card it has just emptied (docs/70).
  TEST_ASSERT_TRUE(add_app(storage, "cardwriter",
                           "{\"title\":\"Writer\",\"version\":\"0.1.0\","
                           "\"entry\":\"app.lua\",\"boards\":[\"esp32\"]}",
                           "local ok, err = fs.write('/sd/x.txt', 'no')\n"
                           "console.print('write ' .. tostring(ok) .. ' ' .. tostring(err and err.code))\n"));
  TEST_ASSERT_TRUE(run_captured("run cardwriter", g_out, sizeof(g_out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "write nil E_INVAL"), g_out);

  // And back: the card of this case is emptied, the app and its keys go, so the next case finds a disk it knows.
  for (int i = 0; i < 3 && kernel::app_running(); ++i) {
    emu::press_button(true);
    pump_turns(10);
    emu::press_button(false);
    pump_turns(10);
  }
  storage->remove("/apps/cardwriter/app.lua");
  storage->remove("/apps/cardwriter/manifest.json");
  storage->remove("/apps/cardwriter");
  storage->remove("/apps/wipe/res/icon.png");
  storage->remove("/apps/wipe/wipe.lua");
  storage->remove("/apps/wipe/manifest.json");
  storage->remove("/apps/wipe/res");
  storage->remove("/apps/wipe");
}

// **A face that a page draws with lives as long as the page** (0.177.1): the app of the faces is a shelf and a specimen, and
// the specimen loads a face of `/fonts` and gives it back when it goes. **The board keeps three of them** (`kMaxFaces`), so
// four specimens opened one after the other are the case: with the face and its page sharing a life, all four open and none
// is refused. With the face left to the collector of Lua, the fourth is answered «there is no room for another face» —and,
// worse, the labels of a page whose face was collected draw with the face of the system without a line anywhere— (docs/30,
// docs/75).
void test_the_faces_app_opens_more_specimens_than_the_board_keeps(void) {
  TEST_ASSERT_TRUE(boot_once());
  hal::IStorage* storage = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(storage);

  // **Four faces in the disk of this test**, which is what the shelf lists.
  TEST_ASSERT_TRUE(copy_a_face(storage, "dejavusans.ttf"));
  TEST_ASSERT_TRUE(copy_a_face(storage, "dejavusans-bold.ttf"));
  TEST_ASSERT_TRUE(copy_a_face(storage, "dejavusansmono.ttf"));
  TEST_ASSERT_TRUE(copy_a_face(storage, "dejavuserif.ttf"));

  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  opts.touch = true;
  emu::set_options(opts);
  kernel::boot(hal::platform());
  TEST_ASSERT_NOT_NULL(hal::platform().touch());
  TEST_ASSERT_TRUE(kernel::touch::boot(hal::platform()));
  emu::Gt911Panel* panel = emu::touch_panel();
  TEST_ASSERT_NOT_NULL(panel);

  TEST_ASSERT_TRUE(storage->mkdir("/apps/fonts"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/fonts/fonts.lua", "/apps/fonts/fonts.lua"));
  TEST_ASSERT_TRUE(copy_a_host_file(storage, "apps/fonts/manifest.json", "/apps/fonts/manifest.json"));

  // **Where the rows are, from the app** (`run fonts shelf`), and which of them are faces: a file that is not a face has a
  // row with no door on it, so the case takes the names that end in one of the four a face can have (docs/30, docs/85).
  kernel::log_echo(true);
  pump_captured(120, g_out, sizeof(g_out), "run fonts shelf");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "fonts: row "), g_out);

  struct Row {
    char name[64];
    int x;
    int y;
  };
  Row rows[24] = {};
  int row_count = 0;
  for (const char* at = strstr(g_out, "fonts: row "); at != nullptr && row_count < 24;
       at = strstr(at + 12, "fonts: row ")) {
    Row one = {};
    if (sscanf(at, "fonts: row %63s %d %d", one.name, &one.x, &one.y) != 3) continue;
    if (strstr(one.name, ".ttf") == nullptr && strstr(one.name, ".bdf") == nullptr &&
        strstr(one.name, ".pcf") == nullptr && strstr(one.name, ".lfont") == nullptr) {
      continue;
    }
    rows[row_count++] = one;
  }
  TEST_ASSERT_GREATER_THAN_MESSAGE(3, row_count, g_out);

  const kernel::ui::Canvas& view = kernel::ui::canvas();
  auto tap = [&](int x, int y) {
    if (!panel->finger_down(0, static_cast<uint16_t>(view.x + x), static_cast<uint16_t>(view.y + y))) return false;
    pump_turns(40);
    if (!panel->finger_up(0)) return false;
    // The window of the double and of the triple, the same as the case of the time clock's keys (docs/30, docs/92).
    pump_turns(400);
    return true;
  };

  // **Four specimens, one after the other**: each one opens its face, is looked at and goes back to the shelf —which is
  // where the face has to be given back, because the board keeps three (docs/75).
  for (int i = 0; i < 4 && i < row_count; ++i) {
    capture_begin();
    TEST_ASSERT_TRUE_MESSAGE(tap(rows[i].x, rows[i].y), rows[i].name);
    capture_end(g_out, sizeof(g_out));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "fonts: open "), g_out);
    TEST_ASSERT_NULL_MESSAGE(strstr(g_out, "fonts: cannot open"), g_out);

    // And back, from the door the app says where it is.
    int back_x = 0;
    int back_y = 0;
    const char* box = strstr(g_out, "fonts: backbox ");
    TEST_ASSERT_NOT_NULL_MESSAGE(box, g_out);
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, sscanf(box, "fonts: backbox %d %d", &back_x, &back_y), g_out);
    capture_begin();
    TEST_ASSERT_TRUE(tap(back_x, back_y));
    capture_end(g_out, sizeof(g_out));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_out, "fonts: back"), g_out);
  }

  kernel::shell::close_app();
  pump_captured(10, nullptr, 0);
  TEST_ASSERT_FALSE(kernel::app_running());
  kernel::log_echo(false);
  storage->remove("/apps/fonts/fonts.lua");
  storage->remove("/apps/fonts/manifest.json");
  storage->remove("/apps/fonts");
}

int main(int argc, char** argv) {  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_the_options_of_the_emulator_start_empty);
  RUN_TEST(test_an_app_runs_and_writes);
  RUN_TEST(test_a_failing_app_says_why_and_the_shell_survives);
  RUN_TEST(test_the_exit_code_reaches_the_shell);
  RUN_TEST(test_open_hands_the_file_to_the_app);
  RUN_TEST(test_a_file_without_a_handler_is_refused);
  RUN_TEST(test_two_handlers_ask_every_time);
  RUN_TEST(test_the_question_is_the_only_prompt);
  RUN_TEST(test_a_long_path_reaches_the_app);
  RUN_TEST(test_a_path_with_spaces_reaches_the_app_in_quotes);
  RUN_TEST(test_a_long_loop_gives_the_turn_back);
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
  RUN_TEST(test_an_app_on_the_two_disks_is_listed_twice_and_the_card_one_says_SD);
  RUN_TEST(test_a_name_that_already_says_its_unit_keeps_its_word);
  RUN_TEST(test_a_manifest_of_nine_boards_says_it_names_too_many);
  RUN_TEST(test_the_door_of_the_apps_puts_the_card_and_the_flash_together);
  RUN_TEST(test_the_door_of_the_fonts_puts_the_flash_and_the_card_together);
  RUN_TEST(test_the_door_of_the_apps_presents_no_folder_that_is_not_an_app);
  RUN_TEST(test_apps_remove_takes_the_app_the_name_says_and_leaves_the_rest);
  RUN_TEST(test_a_card_of_more_than_two_dozen_apps_lists_the_one_of_the_flash_too);
  RUN_TEST(test_a_board_with_no_card_looks_only_in_the_flash);
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
  RUN_TEST(test_the_hexdump_command_shows_the_bytes_of_the_rtc_memory);
  RUN_TEST(test_the_fram_is_one_file_of_its_bytes);
  RUN_TEST(test_the_root_lists_the_memories_as_files);
  RUN_TEST(test_the_extra_memory_is_there_for_any_app);
  RUN_TEST(test_the_wifi_command_scans_and_lists_the_networks);
  RUN_TEST(test_a_radio_that_refuses_says_why);
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
  RUN_TEST(test_the_repo_command_installs_an_app_with_its_folders);
  RUN_TEST(test_an_app_with_more_folders_than_a_table_in_ram_carries_is_installed);
  RUN_TEST(test_an_install_that_hits_a_404_says_so_and_can_be_tried_again);
  RUN_TEST(test_the_http_command_saves_a_page_that_does_not_fit);
  RUN_TEST(test_http_without_a_network_is_refused);
  RUN_TEST(test_a_page_on_a_radio_that_is_still_up);
  RUN_TEST(test_an_app_downloads_a_page);
  RUN_TEST(test_ui_focus_puts_the_dial_where_the_app_says);
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
  RUN_TEST(test_a_sensor_that_does_not_answer_is_said);
  RUN_TEST(test_an_app_belongs_to_the_boards_it_declares);
  RUN_TEST(test_a_manifest_without_boards_is_not_an_app);
  RUN_TEST(test_the_lua_command_opens_the_interpreter);
  RUN_TEST(test_an_app_for_another_board_is_listed_and_refused);
  RUN_TEST(test_the_emulator_is_the_board_it_was_told_to_be);
  RUN_TEST(test_app_run_says_why_it_cannot_start_an_app);
  RUN_TEST(test_the_launcher_lists_the_apps_and_starts_the_one_that_is_pressed);
  RUN_TEST(test_a_step_of_the_dial_in_the_launcher_paints_the_cells_it_crosses);
  RUN_TEST(test_a_cache_of_the_session_the_memory_takes_does_not_kill_the_page);
  RUN_TEST(test_the_picture_of_a_cell_is_turned_over_with_the_cell);
  RUN_TEST(test_a_field_the_app_never_wrote_reads_its_default);
  RUN_TEST(test_the_ring_that_comes_back_costs_the_cell_it_is_on);
  RUN_TEST(test_the_dial_walks_the_controls_of_the_page_in_a_ring);
  RUN_TEST(test_the_pages_of_a_grid_follow_the_dial);
  RUN_TEST(test_a_finger_that_walks_sideways_over_a_grid_turns_the_page);
  RUN_TEST(test_the_cursor_of_a_grid_follows_the_ring_without_the_app);
  RUN_TEST(test_a_finger_that_walks_sideways_over_a_list_turns_the_page);
  RUN_TEST(test_a_dial_step_past_the_last_row_of_a_page_turns_it);
  RUN_TEST(test_a_canvas_inside_a_cell_of_a_grid_survives_the_page);
  RUN_TEST(test_every_app_of_the_repository_declares_its_boards);
  RUN_TEST(test_every_app_of_the_repository_carries_an_icon);
  RUN_TEST(test_the_store_says_how_full_it_is);
  RUN_TEST(test_a_press_reaches_the_app_as_a_promise);
  RUN_TEST(test_a_present_with_a_mode_paints_with_that_mode);
  RUN_TEST(test_a_page_goes_up_with_the_quality_its_ink_asks_for);
  RUN_TEST(test_a_page_of_one_bit_is_never_sent_to_the_full_wash);
  RUN_TEST(test_the_selector_of_the_focus_comes_and_goes_with_the_hands);
  RUN_TEST(test_a_button_is_navigated_and_touched);
  RUN_TEST(test_a_swipe_on_a_box_of_lines_moves_the_window_and_ends_with_its_page);
  RUN_TEST(test_the_document_of_the_editor_scrolls_with_a_finger);
  RUN_TEST(test_a_swipe_only_moves_the_box_that_has_the_dial);
  RUN_TEST(test_a_swipe_that_leaves_the_box_stops_where_it_left_it);
  RUN_TEST(test_a_box_that_is_given_back_leaves_no_swipe_to_the_one_that_follows);
  RUN_TEST(test_a_press_reaches_the_app_as_a_hook);
  RUN_TEST(test_three_taps_close_the_app);
  RUN_TEST(test_a_key_reaches_the_app_with_its_number);
  RUN_TEST(test_the_board_says_where_its_keys_are);
  RUN_TEST(test_three_taps_of_a_side_key_keep_the_app);
  RUN_TEST(test_the_dial_walks_the_buttons_of_a_page);
  RUN_TEST(test_the_dial_flips_a_checkbox);
  RUN_TEST(test_the_dial_moves_the_value_of_a_slider);
  RUN_TEST(test_a_panel_arranges_the_page_and_the_dial_changes_the_tab);
  RUN_TEST(test_a_list_of_entries_places_a_page_of_controls);
  RUN_TEST(test_a_grid_of_entries_places_cells_of_controls);
  RUN_TEST(test_a_cell_of_a_box_claims_a_share_of_it);
  RUN_TEST(test_the_dial_walks_more_stops_than_the_old_list_held);
  RUN_TEST(test_a_label_and_the_dots_take_no_children);
  RUN_TEST(test_a_button_that_has_the_ring_inverts_what_it_holds);
  RUN_TEST(test_an_entry_that_is_marked_inverts_what_it_holds);
  RUN_TEST(test_a_child_of_a_disabled_control_is_born_disabled);
  RUN_TEST(test_a_control_declares_whether_it_takes_the_dial);
  RUN_TEST(test_the_ring_reaches_a_control_past_the_hundred_and_twenty_seventh);
  RUN_TEST(test_a_disabled_control_is_drawn_lighter_and_answers_nothing);
  RUN_TEST(test_each_tab_of_a_strip_is_a_stop_of_the_dial);
  RUN_TEST(test_a_key_paints_the_ring_the_app_cannot_see);
  RUN_TEST(test_a_list_turns_its_page_with_the_dial);
  RUN_TEST(test_the_keyboard_types_into_the_box_it_was_given);
  RUN_TEST(test_the_splash_paints_a_picture_in_the_middle_of_the_glass);
  RUN_TEST(test_a_plane_of_the_glass_is_painted_as_it_is);
  RUN_TEST(test_the_tiles_of_2048_move_with_a_swipe_and_keep_their_game);
  RUN_TEST(test_an_app_that_names_a_picture_has_it_painted_before_it_starts);
  RUN_TEST(test_the_picture_of_an_app_goes_up_before_it_is_read);
  RUN_TEST(test_the_boot_paints_the_drawing_the_settings_chose);
  RUN_TEST(test_the_memory_of_a_session_is_the_apps_and_not_the_systems);
  RUN_TEST(test_the_splash_waits_for_the_ink_of_the_page_before);
  RUN_TEST(test_a_page_the_panel_refused_is_painted_when_it_can);
  RUN_TEST(test_the_glass_is_one_surface_however_often_it_is_asked);
  RUN_TEST(test_the_keyboard_takes_its_band_out_of_the_view_the_app_is_given);
  RUN_TEST(test_the_keyboard_follows_the_box_that_takes_the_focus);
  RUN_TEST(test_a_box_that_takes_no_keys_does_not_open_the_keyboard);
  RUN_TEST(test_a_keyboard_without_a_target_is_refused);
  RUN_TEST(test_the_keyboard_is_chosen_in_cfg_and_writes_its_letters);
  RUN_TEST(test_the_home_app_comes_up_with_the_board);
  RUN_TEST(test_a_home_app_that_is_not_there_leaves_the_prompt);
  RUN_TEST(test_the_caret_blinks_without_the_app);
  RUN_TEST(test_the_dial_stops_on_a_text_box_and_the_centre_enters_it);
  RUN_TEST(test_a_read_only_box_has_a_caret_and_never_a_band);
  RUN_TEST(test_the_keys_walk_the_rows_of_a_box_the_centre_entered);
  RUN_TEST(test_the_keys_walk_the_letters_of_a_line);
  RUN_TEST(test_a_box_without_a_frame_leaves_its_edge_in_paper);
  RUN_TEST(test_a_slider_takes_the_keys_only_after_the_centre);
  RUN_TEST(test_a_dialog_answers_with_the_door_the_dial_is_on);
  RUN_TEST(test_a_card_that_goes_away_takes_its_ink_with_it);
  RUN_TEST(test_a_page_that_paints_itself_paints_its_own_hole);
  RUN_TEST(test_the_font_viewer_opens_every_format_the_system_reads);
  RUN_TEST(test_a_folder_is_made_with_the_fathers_it_needs);
  RUN_TEST(test_a_card_over_the_page_takes_the_walk_and_gives_it_back);
  RUN_TEST(test_a_menu_skips_the_option_the_app_hid);
  RUN_TEST(test_a_menu_bar_answers_with_the_title_and_the_entry);
  RUN_TEST(test_a_box_of_lines_types_and_the_enter_writes_a_break);
  RUN_TEST(test_a_selector_opens_the_list_the_app_puts_up);
  RUN_TEST(test_the_cross_of_a_notice_dismisses_the_newest);
  RUN_TEST(test_a_grid_walks_its_cells_and_pages_itself);
  RUN_TEST(test_an_app_asks_for_the_processor_speed);
  RUN_TEST(test_a_light_sleep_comes_back_with_the_clock_moved);
  RUN_TEST(test_an_app_sleeps_deeply_and_comes_back_where_it_was);
  RUN_TEST(test_an_app_writes_the_file_the_prompt_named_and_no_other);
  RUN_TEST(test_a_big_file_is_read_in_chunks);
  RUN_TEST(test_a_failing_app_says_its_error_once);
  RUN_TEST(test_an_app_provisions_and_then_forgets);
  RUN_TEST(test_provisioning_gives_up_when_nobody_arrives);
  RUN_TEST(test_the_face_of_the_system_comes_from_the_disk);
  RUN_TEST(test_an_app_opens_a_list_of_extensions);
  RUN_TEST(test_the_switches_of_a_manifest_reach_the_app);
  RUN_TEST(test_apps_info_shows_the_keys_a_manifest_carries);
  RUN_TEST(test_a_manifest_that_names_more_extensions_than_the_system_carries_says_so);
  RUN_TEST(test_the_calculator_of_the_paper_project_adds_with_the_finger);
  RUN_TEST(test_the_about_app_lists_the_libraries_of_the_project);
  RUN_TEST(test_the_timeclock_of_the_paper_project_keeps_the_day);
  RUN_TEST(test_the_reader_turns_its_page_with_a_finger_and_with_the_keys);
  RUN_TEST(test_the_shelf_has_a_foot_of_its_own);
  RUN_TEST(test_the_shell_app_is_a_page_with_a_session_of_the_shell);
  RUN_TEST(test_a_pinch_from_big_to_small_goes_back_and_then_closes);
  RUN_TEST(test_a_strip_that_closes_its_tabs_draws_its_crosses);
  RUN_TEST(test_an_app_reads_its_own_modules_and_no_one_elses);
  RUN_TEST(test_the_time_zone_app_picks_a_zone_and_saves_it_on_the_close);
  RUN_TEST(test_the_faces_app_opens_more_specimens_than_the_board_keeps);
  RUN_TEST(test_the_eraser_app_empties_the_store_after_the_card_is_accepted);
  RUN_TEST(test_the_eraser_app_empties_the_card_it_may_not_write);
  UNITY_END();
  return 0;
}
