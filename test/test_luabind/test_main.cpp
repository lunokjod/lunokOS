// The interpreter: it starts, it runs a chunk, it says no when it has to, it
// lets an app wait without freezing the system, and it gives every byte back
// when the app ends.
//
// These run on the host build with the host PAL, so they exercise exactly the
// code the emulator and the board run.
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <time.h>

#include <unity.h>

#include "emu/model.h"
#include "emu/options.h"
#include "hal/hal.h"
#include "luabind/luabind.h"
#include "pal/pal.h"

void setUp(void) {}

// A failing assertion leaves the test early: the state must not survive it.
void tearDown(void) { luabind::stop(); }

namespace {

const size_t kGenerousLimit = 256 * 1024;
const int kTurnBudget = 2000;  // turns of 1 ms that a test may spend on one app

// An app that waits for a very long time: it never gets there on its own, so
// whatever ends it is the thing under test.
const char kLongWait[] = "sys.delay(5000):await()";

luabind::Boot test_boot() {
  luabind::Boot boot;
  boot.board = "test";
  boot.version = "0.1.0";
  boot.emulator = true;
  return boot;
}

void sleep_one_ms() {
  struct timespec ts;

  ts.tv_sec = 0;
  ts.tv_nsec = 1000 * 1000;
  nanosleep(&ts, nullptr);
}

// The turns the kernel would be doing while an app is alive. Bounded: a stuck
// app has to fail the test, not hang it.
bool pump() {
  for (int i = 0; i < kTurnBudget; ++i) {
    if (!luabind::poll()) return true;  // the app is over
    sleep_one_ms();
  }
  return false;
}

// Compiles a chunk and drives it to its end. False if it never got there.
bool run(const char* source) {
  if (!luabind::run_source("test", source, strlen(source))) return false;
  return pump();
}

}  // namespace

// A clock that nobody has set has no epoch: the answer is 0 and the source says
// "none", which is what the board showed when it said "17" (docs/60). This one
// has to run before any test that sets the epoch, because the clock lives for
// the whole process.
void test_time_without_an_epoch_says_none(void) {
  pal::init();
  hal::IClock* clock = hal::platform().clock();
  TEST_ASSERT_NOT_NULL(clock);
  TEST_ASSERT_EQUAL_INT64(0, clock->epoch_ms());
  TEST_ASSERT_EQUAL_STRING("none", clock->source());
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("assert(time.now() == 0, time.now())\n"
                       "assert(time.now_ms() == 0)\n"
                       "local c = time.caps()\n"
                       "assert(c.source == 'none', tostring(c.source))"));
  luabind::stop();
}

// time.now() is the wall clock, and it is the platform's: setting it is enough
// for the app to see the new time (docs/60).
void test_time_now_is_the_platform_clock(void) {
  pal::init();
  hal::IClock* clock = hal::platform().clock();
  TEST_ASSERT_NOT_NULL(clock);
  TEST_ASSERT_TRUE(clock->set_epoch_ms(1767225600000LL));  // 2026-01-01 00:00:00 UTC
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("local n = time.now()\n"
                       "assert(n >= 1767225600 and n < 1767225700, n)"));
  luabind::stop();
}

// now_ms is the same clock in milliseconds, and it moves on its own.
void test_time_now_ms_moves_with_the_seconds(void) {
  pal::init();
  hal::IClock* clock = hal::platform().clock();
  TEST_ASSERT_NOT_NULL(clock);
  TEST_ASSERT_TRUE(clock->set_epoch_ms(1000000000000LL));
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("local a = time.now_ms()\n"
                       "sys.delay(20):await()\n"
                       "local b = time.now_ms()\n"
                       "assert(b >= a, 'the clock went back')\n"
                       "assert(b - a >= 10, 'the clock did not move')"));
  luabind::stop();
}

// caps says where the epoch comes from, that nothing was synced yet, and that
// this board cannot wake itself up. It runs after the tests that set the clock,
// so now the source is the SoC's.
void test_time_caps_says_where_the_epoch_comes_from(void) {
  pal::init();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("local c = time.caps()\n"
                       "assert(c.source == 'soc', tostring(c.source))\n"
                       "assert(c.synced == false)\n"
                       "assert(c.wake == 'no', tostring(c.wake))"));
  luabind::stop();
}

namespace {

// A fake file system for fs.*: 5 KB of a repeating pattern, and counters that say
// how many reads were asked for and how big the biggest one was. That is how the
// window of docs/70 is measured, without a disk.
const size_t kFakeBig = 5000;
int g_read_calls = 0;
uint32_t g_biggest_read = 0;

int32_t fake_read_at(const char* path, int64_t offset, char* buf, uint32_t len,
                     luabind::FsError& err) {
  err = luabind::FsError::kOk;
  if (strcmp(path, "/fake/big.txt") != 0) {
    err = luabind::FsError::kNoEnt;
    return -1;
  }
  ++g_read_calls;
  if (len > g_biggest_read) g_biggest_read = len;
  if (offset >= static_cast<int64_t>(kFakeBig)) return 0;
  size_t left = kFakeBig - static_cast<size_t>(offset);
  if (left > len) left = len;
  for (size_t i = 0; i < left; ++i) {
    buf[i] = static_cast<char>('a' + ((offset + static_cast<int64_t>(i)) % 26));
  }
  return static_cast<int32_t>(left);
}

int64_t fake_size(const char* path, luabind::FsError& err) {
  err = luabind::FsError::kOk;
  if (strcmp(path, "/fake/big.txt") == 0) return static_cast<int64_t>(kFakeBig);
  if (strcmp(path, "/fake/small.txt") == 0) return 3;
  err = luabind::FsError::kNoEnt;
  return -1;
}

bool fake_is_dir(const char* path, luabind::FsError& err) {
  err = luabind::FsError::kOk;
  if (strcmp(path, "/fake") != 0) err = luabind::FsError::kNoEnt;
  return strcmp(path, "/fake") == 0;
}

bool fake_list(const char* path, luabind::DirVisitor& visitor, luabind::FsError& err) {
  if (strcmp(path, "/fake") != 0) {
    err = luabind::FsError::kNoEnt;
    return false;
  }
  err = luabind::FsError::kOk;
  visitor.visit("big.txt", static_cast<int>(hal::NodeType::kFile), kFakeBig, 0);
  visitor.visit("small.txt", static_cast<int>(hal::NodeType::kFile), 3, 0);
  return true;
}

// Two mounts and a room, so the two questions an app asks before writing can be
// checked without a board (docs/30).
uint32_t fake_mounts(luabind::MountInfo* out, uint32_t max) {
  if (max < 2) return 0;
  memset(out, 0, sizeof(*out) * 2);
  strcpy(out[0].path, "/littlefs");
  strcpy(out[0].kind, "littlefs");
  out[0].writable = false;
  strcpy(out[1].path, "/data");
  strcpy(out[1].kind, "littlefs");
  out[1].writable = true;
  return 2;
}

bool fake_space(const char* path, uint64_t& total, uint64_t& free_bytes, luabind::FsError& err) {
  err = luabind::FsError::kOk;
  if (strcmp(path, "/nvs") == 0) {
    err = luabind::FsError::kUnsupported;
    return false;
  }
  total = 917504;
  free_bytes = 671744;
  return true;
}

// **The modules of a fake app** (0.148.0, docs/46 D6): what the kernel would hand `require` for a name, and how many
// times it was asked — the second `require` of a name must not come here at all.
const char* g_part_source = nullptr;
int g_part_asked = 0;

int32_t fake_module_source(const char* name, char* buf, uint32_t cap, luabind::FsError& err) {
  err = luabind::FsError::kOk;
  ++g_part_asked;
  if (g_part_source == nullptr || strcmp(name, "part") != 0) {
    err = luabind::FsError::kNoEnt;
    return -1;
  }
  const size_t len = strlen(g_part_source);
  if (len >= cap) {
    err = luabind::FsError::kTooBig;
    return -1;
  }
  memcpy(buf, g_part_source, len);
  return static_cast<int32_t>(len);
}

void install_fake_fs(bool with_mounts = false) {
  luabind::FileOps ops = {};
  ops.read_at = &fake_read_at;
  ops.size = &fake_size;
  ops.is_dir = &fake_is_dir;
  ops.list = &fake_list;
  ops.module_source = &fake_module_source;
  if (with_mounts) {
    ops.mounts = &fake_mounts;
    ops.space = &fake_space;
  }
  luabind::set_file_ops(ops);
  g_read_calls = 0;
  g_biggest_read = 0;
  g_part_source = nullptr;
  g_part_asked = 0;
}

}  // namespace

// The 5 KB file is read in windows of 512 B: that is the number the criterion of
// M4 asks for, and it is measured here rather than guessed (docs/70).
void test_fs_read_uses_a_window_of_512_bytes(void) {
  pal::init();
  install_fake_fs();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("local s = fs.read('/fake/big.txt', 8192)\n"
                       "assert(#s == 5000, #s)\n"
                       "assert(s:sub(1, 3) == 'abc', s:sub(1, 3))"));
  TEST_ASSERT_EQUAL_INT(10, g_read_calls);   // ceil(5000 / 512)
  TEST_ASSERT_EQUAL_UINT32(512, g_biggest_read);
  luabind::stop();
}

// A board with no radio does not pretend: net.caps() is all false and
// net.connect gives back a promise that already carries E_UNSUPPORTED, because an
// app writes `net.connect(...):await()` and must get an error it can catch
// (docs/30).
void test_net_without_a_radio_says_unsupported(void) {
  pal::init();
  luabind::set_net_ops(luabind::NetOps());
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("local c = net.caps()\n"
                       "assert(c.wifi == false and c.http == false and c.tls == false)\n"
                       "assert(c.prov == false)\n"
                       "assert(net.status() == 'off', net.status())\n"
                       "assert(net.provisioned() == false)\n"
                       "local ok, err = net.connect('casa'):await()\n"
                       "assert(ok == nil)\n"
                       "assert(err.code == 'E_UNSUPPORTED', tostring(err and err.code))\n"
                       "local p2, e2 = net.provision('lunokos'):await()\n"
                       "assert(p2 == nil)\n"
                       "assert(e2.code == 'E_UNSUPPORTED', tostring(e2 and e2.code))\n"
                       "local s2, e3 = net.connect_stored()\n"
                       "assert(s2 == nil)\n"
                       "assert(e3.code == 'E_UNSUPPORTED', tostring(e3 and e3.code))"));
  luabind::stop();
}

// The kernel starts the attempt and answers later: the promise is still pending
// while the radio works, and it is net_progress that settles it, between turns
// (docs/30). The proof is that the app was still running after two turns and
// that it finished only once the kernel had answered.
bool g_net_started = false;
char g_net_ssid[32];
uint32_t g_net_timeout = 0;

bool fake_net_start(const char* ssid, const char* pass, uint32_t timeout_ms) {
  (void)pass;
  g_net_started = true;
  snprintf(g_net_ssid, sizeof(g_net_ssid), "%s", ssid);
  g_net_timeout = timeout_ms;
  return true;
}

void test_net_connect_waits_for_the_kernel_to_say_so(void) {
  pal::init();
  luabind::NetOps ops = {};
  ops.connect = &fake_net_start;
  luabind::set_net_ops(ops);
  g_net_started = false;
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(luabind::run_source("test",
                                       "local ok = net.connect('casa', 'secret', 1234):await()\n"
                                       "assert(ok == true, tostring(ok))",
                                       strlen("local ok = net.connect('casa', 'secret', 1234):await()\n"
                                              "assert(ok == true, tostring(ok))")));
  // The first turn runs the chunk up to the await; the app is alive and the
  // attempt has been started with the arguments the app gave.
  TEST_ASSERT_TRUE(luabind::poll());
  TEST_ASSERT_TRUE(g_net_started);
  TEST_ASSERT_EQUAL_STRING("casa", g_net_ssid);
  TEST_ASSERT_EQUAL_UINT32(1234, g_net_timeout);

  // Another turn and it is still waiting: nothing has answered yet.
  TEST_ASSERT_TRUE(luabind::poll());

  luabind::net_progress(true, nullptr);
  TEST_ASSERT_TRUE(pump());
  luabind::stop();
}

// qr is a general library, not a private thing of the provisioning: an app can
// encode any text at any level and get the modules back (docs/30).
void test_qr_encodes_any_text_for_an_app(void) {
  pal::init();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run(
      "local c = qr.caps()\n"
      "assert(c.max_version == 20, tostring(c.max_version))\n"
      "assert(c.max_side == 97, tostring(c.max_side))\n"
      "assert(c.limit == 4096, tostring(c.limit))\n"
      // Numbers pack denser than bytes, and the table says so.
      "local code = qr.encode('0123456789', 'm')\n"
      "assert(code.mode == 'numeric', code.mode)\n"
      "assert(code.level == 'm', code.level)\n"
      "assert(code.side == 21, tostring(code.side))\n"
      "assert(#code.modules == code.side * code.side, #code.modules)\n"
      // The top-left finder is dark at its corner and light one module in:
      // if the modules were upside down or shifted this would not hold.
      "assert(code.modules:sub(1, 1) == '1')\n"
      "local at = function(row, col) return code.modules:sub(row * code.side + col + 1,"
      " row * code.side + col + 1) end\n"
      "assert(at(0, 1) == '1' and at(1, 1) == '0' and at(1, 2) == '1')\n"
      // A level that needs more room gives a bigger symbol.
      "local high = qr.encode('0123456789', 'h')\n"
      "assert(high.side >= code.side, tostring(high.side))\n"
      // A mode that cannot carry the text is refused with a code, not silently.
      "local bad, err = qr.encode('abc', 'm', 'numeric')\n"
      "assert(bad == nil and err.code == 'E_INVAL', tostring(err and err.code))\n"
      // Too big for an app is said too, instead of taking the heap away.
      "local huge = string.rep('a', 300)\n"
      "local big, errb = qr.encode(huge, 'l')\n"
      "assert(big == nil and errb.code == 'E_TOOBIG', tostring(errb and errb.code))"));
  luabind::stop();
}

// json is the same pull helper the system uses: no tree, one field at a time
// (docs/30, docs/75).
void test_json_reads_a_field_without_building_a_tree(void) {
  pal::init();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run(
      "local t = '{\"name\":\"casa\",\"port\":80,\"esc\":\"a\\tb\"}'\n"
      "assert(json.get_string(t, 'name') == 'casa')\n"
      "assert(json.get_int(t, 'port') == 80)\n"
      "assert(json.get_string(t, 'nope') == nil)\n"
      "assert(json.get_int(t, 'name') == nil)\n"
      "assert(json.get_string(t, 'esc') == 'a\\tb', tostring(json.get_string(t, 'esc')))"));
  luabind::stop();
}

// fs.mounts() says what is mounted and whether it takes writes, and fs.space()
// answers the room when it is asked: two calls, because the second is the one
// that costs a walk of the file system (docs/70).
void test_fs_mounts_and_space_answer_without_guessing(void) {
  pal::init();
  install_fake_fs(true);
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("local m = fs.mounts()\n"
                       "assert(#m == 2, #m)\n"
                       "assert(m[1].path == '/littlefs', m[1].path)\n"
                       "assert(m[1].writable == false)\n"
                       "assert(m[2].path == '/data', m[2].path)\n"
                       "assert(m[2].writable == true)\n"
                       "local s = fs.space('/data')\n"
                       "assert(s.total == 917504, s.total)\n"
                       "assert(s.free == 671744, s.free)\n"
                       "local n, err = fs.space('/nvs')\n"
                       "assert(n == nil)\n"
                       "assert(err.code == 'E_UNSUPPORTED', tostring(err and err.code))"));
  luabind::stop();
}

// Without a file system the two calls are absent, not silently empty: an app
// that gets an empty list cannot tell a board with nothing mounted from a board
// with no mounts at all (docs/30).
void test_fs_mounts_without_a_file_system_says_unsupported(void) {
  pal::init();
  install_fake_fs();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("local m, err = fs.mounts()\n"
                       "assert(m == nil)\n"
                       "assert(err.code == 'E_UNSUPPORTED', tostring(err and err.code))\n"
                       "local s, err2 = fs.space('/')\n"
                       "assert(s == nil)\n"
                       "assert(err2.code == 'E_UNSUPPORTED', tostring(err2 and err2.code))"));
  luabind::stop();
}

// Without a bound, the whole file is read, however big it is: the ceiling was this
// function's own and it is gone — what is left is the window and the interpreter's
// memory (docs/30, docs/70).
void test_fs_read_without_a_bound_reads_the_whole_file(void) {
  pal::init();
  install_fake_fs();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("local s = fs.read('/fake/big.txt')\n"
                       "assert(#s == 5000, #s)"));
  TEST_ASSERT_EQUAL_INT(10, g_read_calls);  // still in windows of 512
  TEST_ASSERT_EQUAL_UINT32(512, g_biggest_read);
  luabind::stop();
}

// read_at reads from an offset, and a file that is not there says E_NOENT.
void test_fs_read_at_and_a_missing_file(void) {
  pal::init();
  install_fake_fs();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("local s = fs.read_at('/fake/big.txt', 10, 3)\n"
                       "assert(s == 'klm', s)\n"
                       "local none, err = fs.read('/fake/nope.txt')\n"
                       "assert(none == nil and err.code == 'E_NOENT', tostring(err and err.code))"));
  luabind::stop();
}

// stat and list answer with what the file system says, in a shape an app can read.
void test_fs_stat_and_list(void) {
  pal::init();
  install_fake_fs();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("local d = fs.stat('/fake')\n"
                       "assert(d.type == 'dir', d.type)\n"
                       "local f = fs.stat('/fake/big.txt')\n"
                       "assert(f.type == 'file' and f.size == 5000, f.size)\n"
                       "local entries = fs.list('/fake')\n"
                       "assert(#entries == 2, #entries)\n"
                       "assert(entries[1].name == 'big.txt', entries[1].name)\n"
                       "assert(entries[2].size == 3)"));
  luabind::stop();
}

// One call, one line out: what the hello app does.
void test_a_chunk_that_prints_runs(void) {
  pal::init();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("console.print('ok')"));
  TEST_ASSERT_TRUE(luabind::running());
  luabind::stop();
}

// The chunk sees the boot fields it was started with, and nothing else.
void test_the_state_knows_its_board(void) {
  pal::init();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("assert(sys.board == 'test', sys.board)\n"
                       "assert(sys.version == '0.1.0')\n"
                       "assert(sys.emulator == true)"));
  luabind::stop();
}

// Loading a file is not something an app can do (docs/20).
void test_there_is_no_way_to_load_a_file(void) {
  pal::init();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("assert(dofile == nil and loadfile == nil and load == nil)"));
  luabind::stop();
}

// **A module runs once and the name answers with what it gave** (0.148.0, docs/46 D6): the second `require` of a name
// does not touch the kernel at all, and `package.loaded` is where the value lives — that is Lua's own rule and it is
// what makes a module a module.
void test_a_module_is_read_once_and_the_name_remembers_it(void) {
  pal::init();
  install_fake_fs();
  g_part_source = "return { n = 41 }";
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("local a = require('part')\n"
                       "local b = require('part')\n"
                       "assert(a.n == 41, 'the module did not run')\n"
                       "assert(a == b, 'the second require built another one')\n"
                       "assert(package.loaded.part == a, 'the name is not kept')"));
  TEST_ASSERT_EQUAL_INT(1, g_part_asked);
  luabind::stop();
}

// **A module with no value is `true`** and a module that is not there is an error the app can catch, not a silent
// `nil`: an app that asks for a file it does not have has to find out (docs/30).
void test_a_module_with_no_value_is_true_and_a_missing_one_says_so(void) {
  pal::init();
  install_fake_fs();
  g_part_source = "local x = 1";
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("assert(require('part') == true, 'no value is true')\n"
                       "assert(package.loaded.part == true)\n"
                       "local ok, why = pcall(require, 'nope')\n"
                       "assert(not ok, 'a module that is not there did not fail')\n"
                       "assert(tostring(why):find('nope') ~= nil, tostring(why))"));
  TEST_ASSERT_EQUAL_INT(2, g_part_asked);  // `part` once and `nope` once
  luabind::stop();
}

// **A module that dies says where**, with its own name and line, and the app can catch it like any other error: the
// chunk is compiled with the name of the file, so the message is `part.lua:1:` and not a buffer number.
void test_a_module_that_raises_says_its_name_and_its_line(void) {
  pal::init();
  install_fake_fs();
  g_part_source = "error('boom')";
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("local ok, why = pcall(require, 'part')\n"
                       "assert(not ok, 'the module did not fail')\n"
                       "assert(tostring(why):find('part.lua:1:') ~= nil, tostring(why))"));
  luabind::stop();
}

// **And `package` is a table with one field**: no search path and no loader, because a module is a file of the app and
// the kernel names it — a `package.path` would be a promise this system cannot keep (docs/30).
void test_package_carries_no_search_path(void) {
  pal::init();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("assert(type(package) == 'table')\n"
                       "assert(type(package.loaded) == 'table')\n"
                       "assert(package.path == nil and package.cpath == nil)\n"
                       "assert(package.preload == nil and package.searchers == nil)"));
  luabind::stop();
}

// sys.delay really waits, and the app goes on afterwards: the kernel is free in
// between, which is the whole point of this slice.
void test_sys_delay_suspends_and_resumes_the_app(void) {
  static const char kSource[] =
      "local t0 = sys.time_ms()\n"
      "sys.delay(40):await()\n"
      "assert(sys.time_ms() - t0 >= 30, 'it did not wait')";
  pal::init();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(luabind::run_source("test", kSource, sizeof(kSource) - 1));
  TEST_ASSERT_TRUE(luabind::poll());  // the first turn yields: nothing is ready
  TEST_ASSERT_TRUE(pump());
  luabind::stop();
}

// A settled promise is not a wait at all.
void test_a_settled_promise_answers_at_once(void) {
  pal::init();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("local p = Promise.resolve(7)\n"
                       "assert(p:await() == 7)\n"
                       "assert(Promise.is(p) and p.state == 'ok')"));
  luabind::stop();
}

// and_then() gets the value and passes on what the callback returns.
void test_and_then_passes_the_value_on(void) {
  pal::init();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("local v = sys.delay(1):and_then(function() return 5 end):await()\n"
                       "assert(v == 5, tostring(v))"));
  luabind::stop();
}

// await outside a task is an error an app can catch, not a crash.
void test_await_outside_a_task_is_enotask(void) {
  pal::init();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("local q = sys.delay(1):and_then(function() sys.delay(1):await() end)\n"
                       "local ok, e = pcall(function() q:await() end)\n"
                       "assert(not ok and e.code == 'E_NOTASK', tostring(e and e.code))"));
  luabind::stop();
}

// A promise that does not settle in time fails with E_TIMEOUT, and the app can
// catch it.
void test_timeout_gives_e_timeout(void) {
  pal::init();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("local slow = sys.delay(5000)\n"
                       "local ok, e = pcall(function() slow:timeout(20):await() end)\n"
                       "assert(not ok and e.code == 'E_TIMEOUT', tostring(e and e.code))"));
  luabind::stop();
}

// A bad chunk fails, but the state survives it: the shell can try again.
void test_a_broken_chunk_does_not_kill_the_state(void) {
  pal::init();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_FALSE(run("this is not lua"));  // it does not even compile
  TEST_ASSERT_TRUE(luabind::running());
  TEST_ASSERT_TRUE(run("error('boom')"));     // it runs and dies
  TEST_ASSERT_TRUE(luabind::failed());
  TEST_ASSERT_TRUE(luabind::running());
  TEST_ASSERT_TRUE(run("assert(true)"));
  TEST_ASSERT_FALSE(luabind::failed());
  luabind::stop();
}

// The ceiling is real: a chunk that asks for more than it was given dies with a
// message instead of eating the board.
void test_a_chunk_over_the_ceiling_is_refused(void) {
  pal::init();
  // **The ceiling holds the interpreter and the task of the chunk and not the table**: with thirty-two
  // kilobytes there was not even room for the thread (`lua_newthread` answered no and Lua aborted the process,
  // 8/11), so what this case measures was never the chunk. A hundred kilobytes is the interpreter, its
  // libraries and a task, and the two hundred thousand entries of the chunk do not fit in them.
  TEST_ASSERT_TRUE(luabind::start(test_boot(), 100 * 1024));
  TEST_ASSERT_TRUE(run("local t = {} for i = 1, 200000 do t[i] = i end"));
  TEST_ASSERT_TRUE(luabind::failed());
  TEST_ASSERT_TRUE(luabind::running());
  luabind::stop();
}

// A ceiling that does not even hold the interpreter is refused, and it is said:
// no panic, no half-started state.
void test_a_ceiling_too_small_for_the_interpreter_is_refused(void) {
  pal::init();
  TEST_ASSERT_FALSE(luabind::start(test_boot(), 4 * 1024));
  TEST_ASSERT_FALSE(luabind::running());
  luabind::stop();
}

// Nothing is left behind: the numbers an operator reads come back to zero.
void test_every_byte_comes_back_when_the_state_stops(void) {
  pal::init();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("local t = {} for i = 1, 200 do t[i] = i end"));
  TEST_ASSERT_TRUE(luabind::used_bytes() > 0);
  TEST_ASSERT_TRUE(luabind::peak_bytes() >= luabind::used_bytes());
  luabind::stop();
  TEST_ASSERT_FALSE(luabind::running());
  TEST_ASSERT_EQUAL_size_t(0, luabind::used_bytes());
}

// What sys.heap reports is the ceiling that is enforced, not a guess.
void test_the_heap_numbers_match_the_ceiling(void) {
  pal::init();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("local h = sys.heap()\n"
                       "assert(h.limit == 262144)\n"
                       "assert(h.used <= h.limit)"));
  luabind::stop();
}

// The board's memory is a wall: an emulator told to give a megabyte gives the board's 320 KB, because
// the firmware being emulated is the one on the glass (docs/75, docs/85).
void test_a_ceiling_above_the_board_is_the_board(void) {
  pal::init();
  emu::Options opts = emu::options();
  opts.mem_cap_kb = 100000;  // a hundred megabytes, which no ESP32 of this family has
  emu::set_options(opts);
  TEST_ASSERT_EQUAL_UINT32(emu::model().heap_kb, hal::platform().heap_limit_kb());
  opts.mem_cap_kb = 0;  // and zero is the kernel's own ceiling, the same as the board gives
  emu::set_options(opts);
  TEST_ASSERT_EQUAL_UINT32(0, hal::platform().heap_limit_kb());
}

// The app is told the board the platform says, not a name the shell invents: on the
// emulator that is the model it was told to be, and `sys.emulator` is the flag that says the
// hardware under it is a fake (docs/50, docs/85).
void test_the_app_sees_the_board_the_platform_says(void) {
  pal::init();
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  emu::set_options(opts);

  luabind::Boot boot;
  boot.board = hal::platform().id();
  boot.version = "0.1.0";
  boot.emulator = hal::platform().emulator();
  TEST_ASSERT_EQUAL_STRING("m5stack-paper", boot.board);
  TEST_ASSERT_TRUE(luabind::start(boot, kGenerousLimit));
  TEST_ASSERT_TRUE(run("assert(sys.board == 'm5stack-paper', sys.board)\n"
                       "assert(sys.emulator == true)"));
  luabind::stop();

  // And with nobody saying otherwise it is the ATOM, which is a board that exists.
  opts.board = nullptr;
  emu::set_options(opts);
  TEST_ASSERT_EQUAL_STRING("m5stack-atom", hal::platform().id());
}

// What is typed while the app is alive reaches the app, and Enter is what
// settles the read (with the line editing of a terminal: the backspace works).
void test_read_line_gets_the_line_that_is_typed(void) {
  static const char kSource[] =
      "local line = console.read_line():await()\n"
      "assert(line == 'hola', tostring(line))";
  pal::init();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(luabind::run_source("test", kSource, sizeof(kSource) - 1));
  TEST_ASSERT_TRUE(luabind::poll());  // it is waiting for the line
  luabind::feed_console("hol", 3);
  luabind::feed_console("q", 1);
  luabind::feed_console("\b", 1);  // the q is rubbed out
  luabind::feed_console("a\r", 2);
  TEST_ASSERT_TRUE(pump());
  TEST_ASSERT_FALSE(luabind::failed());
  luabind::stop();
}

// **A key is a byte and not a line** (0.65.0): in raw mode the console hands the app the keys as they
// arrive —the arrows whole, the control characters whole, nothing echoed— which is what an editor
// needs and what a prompt must never get (docs/30).
void test_raw_mode_hands_the_keys_to_the_app(void) {
  static const char kSource[] =
      "assert(console.raw(true) == true)\n"
      "local seen = {}\n"
      "for i = 1, 6 do seen[i] = console.read_byte():await() end\n"
      "assert(seen[1] == 27 and seen[2] == 91 and seen[3] == 66, table.concat(seen, ','))\n"
      "assert(seen[4] == 13, 'enter is a carriage return in raw mode: ' .. tostring(seen[4]))\n"
      "assert(seen[5] == 3, 'a control character is a key: ' .. tostring(seen[5]))\n"
      "assert(seen[6] == 113, 'and a letter is its own byte')\n"
      "assert(console.raw(false) == false)";
  pal::init();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(luabind::run_source("test", kSource, sizeof(kSource) - 1));
  TEST_ASSERT_TRUE(luabind::poll());  // it is waiting for the first key
  // **An arrow arrives as three bytes and it is one key**: they are handed over one by one, whole.
  luabind::feed_console("\x1b[B", 3);
  luabind::feed_console("\r\x03q", 3);
  TEST_ASSERT_TRUE(pump());
  TEST_ASSERT_FALSE(luabind::failed());
  luabind::stop();
}

// Nothing is echoed in raw mode: the console is the app's canvas and the app decides what a key does.
void test_raw_mode_does_not_echo_and_keeps_the_keys_it_arrived_with(void) {
  static const char kSource[] =
      "console.raw(true)\n"
      "local a = console.read_byte():await()\n"
      "local b = console.read_byte():await()\n"
      "assert(a == 0x7f and b == 0x08, tostring(a) .. ',' .. tostring(b))";
  pal::init();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(luabind::run_source("test", kSource, sizeof(kSource) - 1));
  TEST_ASSERT_TRUE(luabind::poll());
  // The backspace of a terminal is two bytes and neither of them is editing anything here.
  luabind::feed_console("\x7f\x08", 2);
  TEST_ASSERT_TRUE(pump());
  TEST_ASSERT_FALSE(luabind::failed());
  luabind::stop();
}

// **A key that arrives before the read is not lost** (0.65.0): the console takes two bytes in one
// chunk and the app reads two keys, the second with no wait at all —which is what makes typing faster
// than the turn loop (docs/75).
void test_a_key_that_arrives_before_the_read_waits_for_it(void) {
  static const char kSource[] =
      "console.raw(true)\n"
      "local a = console.read_byte():await()\n"
      "local b = console.read_byte():await()\n"
      "assert(a == 97 and b == 98, tostring(a) .. ',' .. tostring(b))";
  pal::init();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(luabind::run_source("test", kSource, sizeof(kSource) - 1));
  TEST_ASSERT_TRUE(luabind::poll());  // it is raw and waiting for the first key
  // **Both keys arrive in one chunk and one read is waiting**: the second is not lost, it waits in the
  // queue for the read that comes next.
  luabind::feed_console("ab", 2);
  TEST_ASSERT_TRUE(pump());
  TEST_ASSERT_FALSE(luabind::failed());
  luabind::stop();
}

// Two reads at once would not know which line is whose.
void test_two_reads_at_once_is_ebusy(void) {
  pal::init();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("local p = console.read_line()\n"
                       "local ok, e = pcall(function() console.read_line() end)\n"
                       "assert(not ok and e.code == 'E_BUSY', tostring(e and e.code))"));
  luabind::stop();
}

// Three ESC in a row are the way out of an app that stopped listening.
void test_three_escapes_ask_to_leave(void) {
  pal::init();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(luabind::run_source("test", kLongWait, sizeof(kLongWait) - 1));
  TEST_ASSERT_TRUE(luabind::poll());
  TEST_ASSERT_FALSE(luabind::exit_requested());
  luabind::feed_console("\x1b", 1);
  luabind::feed_console("\x1b", 1);
  TEST_ASSERT_FALSE(luabind::exit_requested());
  luabind::feed_console("\x1b", 1);
  TEST_ASSERT_TRUE(luabind::exit_requested());
  luabind::stop();
}

// An arrow key is ESC too, and pressing it three times is not a request to
// leave: the rest of the sequence says it is a key, not the escape hatch.
void test_escape_sequences_are_not_a_request_to_leave(void) {
  pal::init();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(luabind::run_source("test", kLongWait, sizeof(kLongWait) - 1));
  TEST_ASSERT_TRUE(luabind::poll());
  luabind::feed_console("\x1b[A\x1b[A\x1b[A", 9);
  TEST_ASSERT_FALSE(luabind::exit_requested());
  luabind::stop();
}

// !!exit closes an app that is not reading, and is just a line when it is.
void test_bang_bang_exit(void) {
  static const char kReader[] =
      "local line = console.read_line():await()\n"
      "assert(line == '!!exit', tostring(line))";
  pal::init();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(luabind::run_source("test", kLongWait, sizeof(kLongWait) - 1));
  TEST_ASSERT_TRUE(luabind::poll());
  luabind::feed_console("!!exit\r", 7);
  TEST_ASSERT_TRUE(luabind::exit_requested());
  luabind::stop();

  // Same line, but this app is reading: it gets it as data, not as a command.
  luabind::start(test_boot(), kGenerousLimit);
  TEST_ASSERT_TRUE(luabind::run_source("test", kReader, sizeof(kReader) - 1));
  TEST_ASSERT_TRUE(luabind::poll());
  luabind::feed_console("!!exit\r", 7);
  TEST_ASSERT_FALSE(luabind::exit_requested());
  TEST_ASSERT_TRUE(pump());
  TEST_ASSERT_FALSE(luabind::failed());
  luabind::stop();
}

// Two reads in a row: the LF half of the Enter must not answer the next read
// with an empty line (a terminal sends \r\n, and the halves can arrive apart).
void test_the_lf_of_an_enter_does_not_answer_the_next_read(void) {
  static const char kSource[] =
      "local a = console.read_line():await()\n"
      "local b = console.read_line():await()\n"
      "assert(a == 'one' and b == 'two', tostring(a) .. '|' .. tostring(b))";
  pal::init();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(luabind::run_source("test", kSource, sizeof(kSource) - 1));
  TEST_ASSERT_TRUE(luabind::poll());  // waiting on the first read
  luabind::feed_console("one\r", 4);
  TEST_ASSERT_TRUE(luabind::poll());  // the app goes on and waits on the second
  luabind::feed_console("\n", 1);     // the other half of the CRLF, arriving late
  luabind::feed_console("two\r", 4);
  TEST_ASSERT_TRUE(pump());
  TEST_ASSERT_FALSE(luabind::failed());
  luabind::stop();
}

// sys.caps() says what the platform says, with the names the HAL publishes.
void test_sys_caps_matches_the_platform(void) {
  pal::init();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("local c = sys.caps()\n"
                       "assert(c.console == true, 'console')\n"
                       "assert(c.ansi == true, 'ansi')\n"
                       "assert(c.littlefs == true, 'littlefs')\n"
                       "assert(c.display == false, 'display')\n"
                       "assert(c.net_wifi == false, 'net_wifi')"));
  luabind::stop();
}

// term: the colours are numbers, the size is the documented default, and using
// them on a console that speaks ANSI does not raise.
void test_term_speaks_when_the_console_does(void) {
  pal::init();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("assert(term.RED == 31 and term.BOLD == 1 and term.WHITE == 37)\n"
                       "assert(term.width() == 80 and term.height() == 24)\n"
                       "term.color(term.RED, term.BLACK)\n"
                       "term.move(1, 1)\n"
                       "term.hide_cursor()\n"
                       "term.show_cursor()\n"
                       "term.clear()\n"
                       "term.reset()"));
  luabind::stop();
}

// A hundred waits in a row: the machinery has to survive being used, not only
// being started.
void test_a_hundred_waits_in_a_row_work(void) {
  pal::init();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("for i = 1, 100 do sys.delay(1):await() end\n"
                       "local h = sys.heap()\n"
                       "assert(h.peak > 0 and h.used <= h.limit)"));
  TEST_ASSERT_FALSE(luabind::failed());
  TEST_ASSERT_TRUE(luabind::peak_bytes() > 0);
  luabind::stop();
}

// app.exit() ends the app with a code, and it is not an error.
void test_app_exit_ends_the_app_with_a_code(void) {
  pal::init();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("app.exit(3)"));
  TEST_ASSERT_FALSE(luabind::failed());
  TEST_ASSERT_EQUAL_INT(3, luabind::exit_code());
  luabind::stop();
}

// The app knows who it is and what file it was handed.
void test_app_knows_its_id_and_its_file(void) {
  pal::init();
  luabind::Boot boot = test_boot();
  boot.app_id = "txt";
  boot.args_path = "/res/hola.txt";
  boot.args_ext = "txt";
  boot.args_source = "open";
  TEST_ASSERT_TRUE(luabind::start(boot, kGenerousLimit));
  TEST_ASSERT_TRUE(run("assert(app.id == 'txt', app.id)\n"
                       "assert(app.args.path == '/res/hola.txt', app.args.path)\n"
                       "assert(app.args.ext == 'txt' and app.args.source == 'open')"));
  TEST_ASSERT_FALSE(luabind::failed());
  TEST_ASSERT_EQUAL_INT(0, luabind::exit_code());
  luabind::stop();
}

namespace {

// What the band says about the interpreter, in the test's hands: a price for one instruction and a
// pocket to collect what is paid (docs/85).
uint32_t g_spent_us = 0;
uint32_t g_instruction_ns = 60;

void count_the_spend(uint32_t us) { g_spent_us += us; }
uint32_t price_of_an_instruction() { return g_instruction_ns; }

}  // namespace

// **The instructions of an app are CPU time** (docs/85): the interpreter asks the band what one costs and
// pays for them, so a loop costs here what it costs on the glass. On a board the answer is zero —its
// instruction already takes the time it takes— and nothing is spent; on the emulator it is the sixty
// nanoseconds of the model, times what the turn did.
static void test_the_instructions_of_an_app_are_paid_for(void) {
  // **The ops go in before the interpreter opens**, because the count hook is only installed on a band
  // that prices instructions: a board installs the cheap one (docs/85).
  luabind::MeterOps ops = {};
  ops.instruction_ns = &price_of_an_instruction;
  ops.spend_us = &count_the_spend;
  luabind::set_meter_ops(ops);
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));

  g_spent_us = 0;
  TEST_ASSERT_TRUE(run("local x = 0\nfor i = 1, 2000 do x = x + i end\n"));
  const uint32_t paid = g_spent_us;
  TEST_ASSERT_TRUE_MESSAGE(paid > 0, "a loop of two thousand additions was free");

  // Twice the work, twice the price, within the block the hook pays in: what is charged is the count of
  // instructions and not the clock of the PC (docs/85).
  g_spent_us = 0;
  TEST_ASSERT_TRUE(run("local x = 0\nfor i = 1, 4000 do x = x + i end\n"));
  TEST_ASSERT_UINT32_WITHIN(paid, 2 * paid, g_spent_us);

  // A band that prices nothing is a board, and a board spends nothing: the time is the real one.
  g_instruction_ns = 0;
  g_spent_us = 0;
  TEST_ASSERT_TRUE(run("local x = 0\nfor i = 1, 4000 do x = x + i end\n"));
  TEST_ASSERT_EQUAL_UINT32(0, g_spent_us);

  g_instruction_ns = 60;
  luabind::stop();
  luabind::set_meter_ops(luabind::MeterOps{});
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_a_chunk_that_prints_runs);
  RUN_TEST(test_the_state_knows_its_board);
  RUN_TEST(test_time_without_an_epoch_says_none);
  RUN_TEST(test_time_now_is_the_platform_clock);
  RUN_TEST(test_time_now_ms_moves_with_the_seconds);
  RUN_TEST(test_time_caps_says_where_the_epoch_comes_from);
  RUN_TEST(test_qr_encodes_any_text_for_an_app);
  RUN_TEST(test_json_reads_a_field_without_building_a_tree);
  RUN_TEST(test_net_without_a_radio_says_unsupported);
  RUN_TEST(test_net_connect_waits_for_the_kernel_to_say_so);
  RUN_TEST(test_fs_mounts_and_space_answer_without_guessing);
  RUN_TEST(test_fs_mounts_without_a_file_system_says_unsupported);
  RUN_TEST(test_fs_read_uses_a_window_of_512_bytes);
  RUN_TEST(test_fs_read_without_a_bound_reads_the_whole_file);
  RUN_TEST(test_fs_read_at_and_a_missing_file);
  RUN_TEST(test_fs_stat_and_list);
  RUN_TEST(test_there_is_no_way_to_load_a_file);
  RUN_TEST(test_a_module_is_read_once_and_the_name_remembers_it);
  RUN_TEST(test_a_module_with_no_value_is_true_and_a_missing_one_says_so);
  RUN_TEST(test_a_module_that_raises_says_its_name_and_its_line);
  RUN_TEST(test_package_carries_no_search_path);
  RUN_TEST(test_sys_delay_suspends_and_resumes_the_app);
  RUN_TEST(test_a_settled_promise_answers_at_once);
  RUN_TEST(test_and_then_passes_the_value_on);
  RUN_TEST(test_await_outside_a_task_is_enotask);
  RUN_TEST(test_timeout_gives_e_timeout);
  RUN_TEST(test_a_broken_chunk_does_not_kill_the_state);
  RUN_TEST(test_a_chunk_over_the_ceiling_is_refused);
  RUN_TEST(test_a_ceiling_too_small_for_the_interpreter_is_refused);
  RUN_TEST(test_every_byte_comes_back_when_the_state_stops);
  RUN_TEST(test_the_heap_numbers_match_the_ceiling);
  RUN_TEST(test_a_ceiling_above_the_board_is_the_board);
  RUN_TEST(test_the_app_sees_the_board_the_platform_says);
  RUN_TEST(test_read_line_gets_the_line_that_is_typed);
  RUN_TEST(test_two_reads_at_once_is_ebusy);
  RUN_TEST(test_raw_mode_hands_the_keys_to_the_app);
  RUN_TEST(test_raw_mode_does_not_echo_and_keeps_the_keys_it_arrived_with);
  RUN_TEST(test_a_key_that_arrives_before_the_read_waits_for_it);
  RUN_TEST(test_three_escapes_ask_to_leave);
  RUN_TEST(test_escape_sequences_are_not_a_request_to_leave);
  RUN_TEST(test_bang_bang_exit);
  RUN_TEST(test_the_lf_of_an_enter_does_not_answer_the_next_read);
  RUN_TEST(test_sys_caps_matches_the_platform);
  RUN_TEST(test_term_speaks_when_the_console_does);
  RUN_TEST(test_a_hundred_waits_in_a_row_work);
  RUN_TEST(test_app_exit_ends_the_app_with_a_code);
  RUN_TEST(test_app_knows_its_id_and_its_file);
  RUN_TEST(test_the_instructions_of_an_app_are_paid_for);
  UNITY_END();
  return 0;
}
