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

#include "alarms/alarms.h"
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

// **`time.epoch` és la inversa de `time.date`, i només accepta dies que existeixen** (0.189.142): the page of the settings
// offers the thirty-first of every month, and a call that turned the thirty-first of February into the second of March would
// set the clock to a day nobody asked for and say the one they did. The length of February follows `cfg leap`, which is the
// calendar's own switch —the same one `time.date` counts back— (docs/30, docs/60).
void test_time_epoch_is_the_inverse_of_date_and_refuses_a_day_that_does_not_exist(void) {
  pal::init();
  hal::IClock* clock = hal::platform().clock();
  TEST_ASSERT_NOT_NULL(clock);
  TEST_ASSERT_TRUE(clock->set_epoch_ms(1767225600000LL));  // 2026-01-01 00:00:00 UTC
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("time.tz(0)\n"
                       "time.leap(true)\n"
                       "assert(time.epoch(2026, 1, 1, 0, 0) == 1767225600)\n"
                       "local d = time.date()\n"
                       "assert(time.epoch(d.year, d.month, d.day, d.hour, d.min) == time.now() - time.now() % 60)\n"
                       "local bad, why = time.epoch(2026, 2, 31)\n"
                       "assert(bad == nil and why.code == 'E_INVAL', tostring(why and why.code))\n"
                       "local bad2, why2 = time.epoch(2026, 4, 31)\n"
                       "assert(bad2 == nil and why2.code == 'E_INVAL', tostring(why2 and why2.code))\n"
                       "assert(time.epoch(2026, 4, 30, 12, 0) ~= nil)\n"
                       "assert(time.epoch(2028, 2, 29, 12, 0) ~= nil)\n"
                       "local bad3, why3 = time.epoch(2026, 2, 29, 12, 0)\n"
                       "assert(bad3 == nil and why3.code == 'E_INVAL', tostring(why3 and why3.code))\n"
                       "time.leap(false)\n"
                       "local bad4, why4 = time.epoch(2028, 2, 29, 12, 0)\n"
                       "assert(bad4 == nil and why4.code == 'E_INVAL', tostring(why4 and why4.code))\n"
                       "time.leap(true)\n"
                       "assert(time.epoch(2026, 5, 1, 0, 0) == time.epoch(2026, 4, 30, 0, 0) + 86400)"));
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

// **Emptying a volume is one answer and not a walk the app does** (0.171.0, docs/70): the fake says how many things
// went and which path it was given, so the bridge is measured and not the file system.
char g_format_path[64];
uint32_t g_format_removed = 0;
bool g_format_ok = true;
luabind::FsError g_format_error = luabind::FsError::kInval;

bool fake_format(const char* path, uint32_t& removed, luabind::FsError& err) {
  snprintf(g_format_path, sizeof(g_format_path), "%s", path);
  if (!g_format_ok) {
    err = g_format_error;
    return false;
  }
  removed = g_format_removed;
  return true;
}

void install_fake_fs(bool with_mounts = false, bool with_format = false) {
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
  if (with_format) ops.format = &fake_format;
  luabind::set_file_ops(ops);
  g_read_calls = 0;
  g_biggest_read = 0;
  g_part_source = nullptr;
  g_part_asked = 0;
  g_format_path[0] = '\0';
  g_format_removed = 0;
  g_format_ok = true;
  g_format_error = luabind::FsError::kInval;
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

// **`net.ntp` is settled by the queue like every other promise of the network** (0.174.0). It was not: the call was the
// only one of the seven that did not put its promise where `net_progress` looks (`kNetKey`), so an app's
// `net.ntp():await()` waited for ever while the shell's `ntp` answered —the shell waits on the native waiter and not on
// this promise—. Found by the case of the `tz` page, which is the first app of this tree that asks the clock for the
// time (10/11, docs/30, docs/85). This case is the one that holds it: the request starts, the app waits, the kernel
// answers, and the app goes on.
bool g_ntp_started = false;
char g_ntp_server[64];
uint32_t g_ntp_timeout = 0;

bool fake_ntp_start(const char* server, uint32_t timeout_ms) {
  g_ntp_started = true;
  snprintf(g_ntp_server, sizeof(g_ntp_server), "%s", (server != nullptr) ? server : "");
  g_ntp_timeout = timeout_ms;
  return true;
}

void test_net_ntp_answers_the_app_and_not_only_the_shell(void) {
  pal::init();
  luabind::NetOps ops = {};
  ops.ntp_start = &fake_ntp_start;
  luabind::set_net_ops(ops);
  g_ntp_started = false;
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(luabind::run_source("test",
                                       "local ok = net.ntp('pool.test', 4321):await()\n"
                                       "assert(ok == true, tostring(ok))",
                                       strlen("local ok = net.ntp('pool.test', 4321):await()\n"
                                              "assert(ok == true, tostring(ok))")));
  // **The first turn runs the chunk up to the `await`**: the app is alive, the request has started, and the arguments
  // of the app went through to the radio.
  TEST_ASSERT_TRUE(luabind::poll());
  TEST_ASSERT_TRUE(g_ntp_started);
  TEST_ASSERT_EQUAL_STRING("pool.test", g_ntp_server);
  TEST_ASSERT_EQUAL_UINT32(4321, g_ntp_timeout);

  // **And it is still waiting**: nothing has answered yet, which is a promise that is pending and not one that was
  // never registered anywhere.
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

// **`fs.format` answers how many things went, or the word of why not** (0.171.0, docs/30, docs/70): the path goes
// through unchanged —the volume is the kernel's to find and not the app's to name— and a backend that cannot empty a
// volume is `E_UNSUPPORTED` and not a silent `true`.
void test_fs_format_answers_how_many_went_or_the_word_of_why_not(void) {
  pal::init();
  install_fake_fs(true, true);
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  g_format_removed = 7;
  TEST_ASSERT_TRUE(run("local r = fs.format('/nvs')\n"
                       "assert(r ~= nil)\n"
                       "assert(r.erased == 7, tostring(r and r.erased))"));
  TEST_ASSERT_EQUAL_STRING("/nvs", g_format_path);
  // A volume that is not there and one that cannot be emptied are two answers, and the two are the backend's.
  g_format_ok = false;
  g_format_error = luabind::FsError::kInval;
  TEST_ASSERT_TRUE(run("local r, err = fs.format('/sd/notes')\n"
                       "assert(r == nil)\n"
                       "assert(err.code == 'E_INVAL', tostring(err and err.code))\n"
                       "assert(err.message ~= nil and #err.message > 0)"));
  g_format_error = luabind::FsError::kUnsupported;
  TEST_ASSERT_TRUE(run("local r, err = fs.format('/littlefs')\n"
                       "assert(r == nil)\n"
                       "assert(err.code == 'E_UNSUPPORTED', tostring(err and err.code))"));
  luabind::stop();
}

// **A board whose file system has no door for it says so**: the call is not gone —an app asks and gets a word— and it
// is the same one a mount that cannot be emptied gives (docs/30).
void test_fs_format_without_a_door_says_unsupported(void) {
  pal::init();
  install_fake_fs(true);
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("local r, err = fs.format('/nvs')\n"
                       "assert(r == nil)\n"
                       "assert(err.code == 'E_UNSUPPORTED', tostring(err and err.code))"));
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
                       "assert(c.ansi == nil, 'ansi was taken out with the escape codes')\n"
                       "assert(c.littlefs == true, 'littlefs')\n"
                       "assert(c.display == false, 'display')\n"
                       "assert(c.net_wifi == false, 'net_wifi')"));
  luabind::stop();
}

// term: the size is the documented default and there is nothing that escapes (0.190.187, l'amo: *«Encara queden coses amb
// ansi! Busca per tot el codi restes d'ansi i treu-lo»*). The six functions that wrote `ESC [ ... m`, and the colour names that
// only they used, are gone: a page of text asks how wide it is and never how to colour itself.
void test_term_measures_a_page_and_escapes_nothing(void) {
  pal::init();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("assert(term.width() == 80 and term.height() == 24)\n"
                       "assert(term.clear == nil and term.color == nil and term.move == nil)\n"
                       "assert(term.hide_cursor == nil and term.show_cursor == nil and term.reset == nil)\n"
                       "assert(term.RED == nil and term.BOLD == nil)"));
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

// **The numbers of the band follow the seed of the run** (0.174.8): `pal::set_random_seed(n)` is what makes a run repeat
// —which is what `--seed` of the emulator is for and what a case that draws a page needs to be reproducible— and a run
// that asked for nothing is the machine's own clock and process, so it is never the run before. The board answers the
// same question with the chip's noise and a nudge to this door does nothing there (docs/20, docs/85).
static void test_the_numbers_of_the_band_follow_the_seed_of_the_run(void) {
  pal::set_random_seed(4242);
  TEST_ASSERT_TRUE(pal::init());
  const uint32_t first = pal::system().random();
  const uint32_t second = pal::system().random();
  // **Two in a row are not the same word**: the generator is stepped here and not asked of the clock again.
  TEST_ASSERT_NOT_EQUAL(first, second);

  pal::set_random_seed(4242);
  TEST_ASSERT_TRUE(pal::init());
  TEST_ASSERT_EQUAL_UINT32(first, pal::system().random());
  TEST_ASSERT_EQUAL_UINT32(second, pal::system().random());

  pal::set_random_seed(4243);
  TEST_ASSERT_TRUE(pal::init());
  TEST_ASSERT_NOT_EQUAL(first, pal::system().random());

  // **And with no seed there is no repetition**: the clock of the machine in nanoseconds and the process are the seed.
  pal::set_random_seed(0);
  TEST_ASSERT_TRUE(pal::init());
  const uint32_t one = pal::system().random();
  sleep_one_ms();
  TEST_ASSERT_TRUE(pal::init());
  TEST_ASSERT_NOT_EQUAL(one, pal::system().random());
}

// **`rle.pack` i `rle.unpack` des de Lua** (0.189.6): the module is `lib/rle` with the ergonomics of a string, which is
// what a cache of icons is written to and read from —the app never sees a byte, it sees a text it can hand back—. The
// case walks the three answers the two calls have: a text that packs to less than it was, a text that packs and comes
// back as it was, and one that is not a stream of runs at all (docs/30).
void test_rle_packs_and_unpacks_a_text_of_lua(void) {
  pal::init();
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("local paper = string.rep(string.char(255), 400)\n"
                       "local packed = rle.pack(paper)\n"
                       "assert(type(packed) == 'string', type(packed))\n"
                       "assert(#packed < #paper, #packed .. ' of ' .. #paper)\n"
                       "assert(rle.unpack(packed) == paper, 'the paper did not come back')\n"
                       // **Un text que no repeteix res** també ha de tornar sencer, que és el cas en què el stream creix.
                       "local mixed = ''\n"
                       "for i = 0, 200 do mixed = mixed .. string.char(i % 251) end\n"
                       "assert(rle.unpack(rle.pack(mixed)) == mixed, 'the mixed text did not come back')\n"
                       // **I el buit és el buit**: ni capçalera ni error.
                       "assert(rle.pack('') == '', 'the empty text')\n"
                       // **I el que no és un stream de tirades té la seva paraula** i no una imatge de brossa.
                       "local back, why = rle.unpack('this is not a stream of runs')\n"
                       "assert(back == nil, tostring(back))\n"
                       "assert(type(why) == 'table' and why.code == 'E_INVAL', tostring(why and why.code))"));
  luabind::stop();
}

namespace {

// **The snapshot of the settings that this test hands to the door** (0.189.13): the shape of the table is what is under
// test here and not where the numbers come from —that is `kernel/src/settings_ops.cpp`, which reads `cfg`—.
bool fake_read_settings(luabind::Settings& into) {
  into.unit = "sd";
  into.keyboard = "qwerty";
  into.boot = "logo";
  into.home = "home_eink";
  into.week = "sunday";
  into.tz = "+02:00";
  into.leap = false;
  into.dst = true;
  into.sort_by_usage = true;
  into.turn = 12000;
  return true;
}

}  // namespace

// **`sys.settings()`: els ajustos del sistema, llegits per una app** (0.189.13, docs/30): the table is the one `cfg` prints at
// the prompt —nine words and a number, with `sort_by_usage` among them— and an app reads it instead of inventing a default.
// The check walks every field, because a table that answers nine of ten is a page that silently keeps a value the person
// changed, which is the fault this door exists to avoid.
void test_sys_settings_answers_what_the_prompt_prints(void) {
  pal::init();
  luabind::SettingsOps ops;
  ops.read = &fake_read_settings;
  luabind::set_settings_ops(ops);
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("local s = sys.settings()\n"
                       "assert(type(s) == 'table', type(s))\n"
                       "assert(s.unit == 'sd', tostring(s.unit))\n"
                       "assert(s.keyboard == 'qwerty', tostring(s.keyboard))\n"
                       "assert(s.boot == 'logo', tostring(s.boot))\n"
                       "assert(s.home == 'home_eink', tostring(s.home))\n"
                       "assert(s.week == 'sunday', tostring(s.week))\n"
                       "assert(s.tz == '+02:00', tostring(s.tz))\n"
                       "assert(s.leap == false, tostring(s.leap))\n"
                       "assert(s.dst == true, tostring(s.dst))\n"
                       "assert(s.sort_by_usage == true, tostring(s.sort_by_usage))\n"
                       "assert(s.turn == 12000, tostring(s.turn))"));
  luabind::stop();
}

// **I un sistema que no en sap diu per què** (0.189.13): a launcher that reads this table has to be able to go on with the
// order the door gave it, so the answer is `nil` and a code and not a table of zeroes that would look like a setting of the
// person (docs/30).
namespace {

// **La porta del ràdio, apuntada** (0.190.204): the four doors a page of `bt` has on the device side record what they were told,
// perquè el que es mira sigui que la paraula i els dos números arriben sencers i no que la pàgina s'ho inventi.
const char* g_bt_word = "keyboard";
char g_bt_asked[16] = {};
int g_bt_dx = 0;
int g_bt_dy = 0;
char g_bt_button[8] = {};

const char* fake_bt_device() { return g_bt_word; }
bool fake_bt_be(const char* what) {
  if (what == nullptr) return false;
  if (strcmp(what, "both") != 0 && strcmp(what, "mouse") != 0 && strcmp(what, "keyboard") != 0) return false;
  snprintf(g_bt_asked, sizeof(g_bt_asked), "%s", what);
  g_bt_word = (strcmp(what, "both") == 0) ? "both" : ((strcmp(what, "mouse") == 0) ? "mouse" : "keyboard");
  return true;
}
const char* fake_bt_name() { return "lunokOS-TEST"; }
bool fake_bt_move(int dx, int dy) {
  g_bt_dx = dx;
  g_bt_dy = dy;
  return true;
}
bool fake_bt_click(const char* button) {
  snprintf(g_bt_button, sizeof(g_bt_button), "%s", (button != nullptr) ? button : "");
  return true;
}

}  // namespace

// **`bt.device`, `bt.name`, `bt.move` i `bt.click`** (0.190.204, l'amo: *«una app lua... amb un teclat i un mouse»*): the four doors
// a page needs to be the two things at once, i el que la prova guarda és l'acord sencer —la paraula que es demana, la que queda, el
// nom amb què la placa s'anuncia i els dos números del moviment— perquè una pàgina que arrossega un dit i rep un cursor quiet no té
// res més a preguntar (docs/30, docs/50).
void test_bt_device_move_and_click_reach_the_radio(void) {
  pal::init();
  luabind::BtOps ops = {};
  ops.device = &fake_bt_device;
  ops.be = &fake_bt_be;
  ops.name = &fake_bt_name;
  ops.move = &fake_bt_move;
  ops.click = &fake_bt_click;
  luabind::set_bt_ops(ops);
  g_bt_word = "keyboard";
  g_bt_asked[0] = '\0';
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("assert(bt.device() == 'keyboard', tostring(bt.device()))\n"
                       "local now = bt.device('both')\n"
                       "assert(now == 'both', tostring(now))\n"
                       "assert(bt.name() == 'lunokOS-TEST', tostring(bt.name()))\n"
                       "assert(bt.move(12, -8) == true)\n"
                       "assert(bt.click() == true)\n"
                       "assert(bt.click('right') == true)\n"
                       "local bad, err = bt.device('mouse-and-toaster')\n"
                       "assert(bad == nil, tostring(bad))\n"
                       "assert(type(err) == 'table' and err.code == 'E_INVAL', tostring(err and err.code))\n"
                       "local far, why = bt.move(400, 0)\n"
                       "assert(far == nil, tostring(far))\n"
                       "assert(type(why) == 'table' and why.code == 'E_INVAL', tostring(why and why.code))\n"
                       "local which, e2 = bt.click('toaster')\n"
                       "assert(which == nil and type(e2) == 'table' and e2.code == 'E_INVAL', tostring(e2 and e2.code))"));
  TEST_ASSERT_EQUAL_STRING("both", g_bt_asked);
  TEST_ASSERT_EQUAL_INT(12, g_bt_dx);
  TEST_ASSERT_EQUAL_INT(-8, g_bt_dy);
  TEST_ASSERT_EQUAL_STRING("right", g_bt_button);
  luabind::stop();
}

void test_sys_settings_without_a_door_says_unsupported(void) {
  pal::init();
  luabind::set_settings_ops(luabind::SettingsOps());
  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("local s, err = sys.settings()\n"
                       "assert(s == nil, tostring(s))\n"
                       "assert(type(err) == 'table' and err.code == 'E_UNSUPPORTED', tostring(err and err.code))"));
  luabind::stop();
}

namespace {

// **La porta que la prova deixa veure** (0.189.16): `sys.session_destroy` l'únic que fa aquí és passar la clau al nucli i
// tornar-li els tres números, o sigui que el que es mira és que la clau hi arribi sencera i que la taula digui el que el
// nucli ha dit —l'id de l'app el posa el kernel, que és l'únic que sap qui corre (docs/30, docs/75)—.
char g_destroyed_key[16] = {};
uint32_t g_destroyed_blocks = 1;

bool fake_session_destroy(const char* key, uint32_t& blocks, uint32_t& files, size_t& bytes) {
  snprintf(g_destroyed_key, sizeof(g_destroyed_key), "%s", key);
  blocks = g_destroyed_blocks;
  files = 1;
  bytes = 32768;
  return true;
}

uint32_t fake_session_generation() { return 7; }

}  // namespace

// **`sys.session_destroy(key)`: una app llença una sessió seva** (0.189.16, docs/30): la memòria i el fitxer, i el pont el
// que fa és dir la clau i tornar el que el nucli diu que hi havia. La prova comprova les dues meitats de l'acord: que la
// clau arriba tal com l'app l'ha escrita, i que la resposta té els tres números que una pàgina pot ensenyar.
void test_sys_session_destroy_says_how_much_went(void) {
  pal::init();
  luabind::SessionOps ops = {};
  ops.destroy = &fake_session_destroy;
  ops.generation = &fake_session_generation;
  luabind::set_session_ops(ops);
  g_destroyed_key[0] = '\0';
  g_destroyed_blocks = 1;

  TEST_ASSERT_TRUE(luabind::start(test_boot(), kGenerousLimit));
  TEST_ASSERT_TRUE(run("local gone = sys.session_destroy('apps')\n"
                       "assert(type(gone) == 'table', type(gone))\n"
                       "assert(gone.blocks == 1, tostring(gone.blocks))\n"
                       "assert(gone.files == 1, tostring(gone.files))\n"
                       "assert(gone.bytes == 32768, tostring(gone.bytes))"));
  TEST_ASSERT_EQUAL_STRING("apps", g_destroyed_key);

  // **I una sessió que no hi era són zeros i no un error**: esborrar el que no hi ha és el que la persona ha demanat.
  g_destroyed_blocks = 0;
  TEST_ASSERT_TRUE(run("local gone = sys.session_destroy('usage')\n"
                       "assert(gone.blocks == 0, tostring(gone.blocks))"));
  TEST_ASSERT_EQUAL_STRING("usage", g_destroyed_key);

  // **I un sistema sense memòria de sessió diu per què** en lloc de callar.
  luabind::set_session_ops(luabind::SessionOps());
  TEST_ASSERT_TRUE(run("local gone, err = sys.session_destroy('apps')\n"
                       "assert(gone == nil, tostring(gone))\n"
                       "assert(type(err) == 'table' and err.code == 'E_UNSUPPORTED', tostring(err and err.code))"));
  luabind::stop();
}

// **El que la cua ja té, dit a l'app** (0.189.172): `alarm.at` torna l'id i, quan la configuració ja hi és, un `true` al
// costat —i no afegeix res—, i la que vol una hora nova esborra la vella i torna a preguntar. Una app que només llegeix el
// primer valor no duplica la seva promesa, que és el que fa que les que ja hi ha no s'hagin de tocar (docs/30, docs/60).
void test_an_alarm_that_is_already_there_is_named_and_not_added(void) {
  pal::init();
  alarms::reset();
  luabind::Boot boot = test_boot();
  boot.app_id = "alarmtest";
  TEST_ASSERT_TRUE(luabind::start(boot, kGenerousLimit));
  TEST_ASSERT_TRUE(run(
      "local a = alarm.at{ in_ms = 600000, payload = 's', ['repeat'] = 600, persist = true }\n"
      "assert(type(a) == 'number' and a > 0, tostring(a))\n"
      "local b, same = alarm.at{ in_ms = 600000, payload = 's', ['repeat'] = 600, persist = true }\n"
      "assert(b == a and same == true, tostring(b) .. ' ' .. tostring(same))\n"
      // A word of its own is another promise, and it is added.
      "local c, other = alarm.at{ in_ms = 600000, payload = 't', ['repeat'] = 600, persist = true }\n"
      "assert(c ~= a and other == nil, tostring(c))\n"
      // And the recipe: cancel the one that is there and ask again.
      "assert(alarm.cancel(a))\n"
      "local d, again = alarm.at{ in_ms = 600000, payload = 's', ['repeat'] = 600, persist = true }\n"
      "assert(d ~= a and again == nil, tostring(d))\n"));
  TEST_ASSERT_FALSE(luabind::failed());
  TEST_ASSERT_EQUAL_UINT32(2, alarms::count());
  luabind::stop();
  alarms::reset();
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
  RUN_TEST(test_time_epoch_is_the_inverse_of_date_and_refuses_a_day_that_does_not_exist);
  RUN_TEST(test_time_caps_says_where_the_epoch_comes_from);
  RUN_TEST(test_qr_encodes_any_text_for_an_app);
  RUN_TEST(test_json_reads_a_field_without_building_a_tree);
  RUN_TEST(test_net_without_a_radio_says_unsupported);
  RUN_TEST(test_net_connect_waits_for_the_kernel_to_say_so);
  RUN_TEST(test_net_ntp_answers_the_app_and_not_only_the_shell);
  RUN_TEST(test_fs_mounts_and_space_answer_without_guessing);
  RUN_TEST(test_fs_mounts_without_a_file_system_says_unsupported);
  RUN_TEST(test_fs_format_answers_how_many_went_or_the_word_of_why_not);
  RUN_TEST(test_fs_format_without_a_door_says_unsupported);
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
  RUN_TEST(test_term_measures_a_page_and_escapes_nothing);
  RUN_TEST(test_a_hundred_waits_in_a_row_work);
  RUN_TEST(test_app_exit_ends_the_app_with_a_code);
  RUN_TEST(test_app_knows_its_id_and_its_file);
  RUN_TEST(test_the_instructions_of_an_app_are_paid_for);
  RUN_TEST(test_the_numbers_of_the_band_follow_the_seed_of_the_run);
  RUN_TEST(test_rle_packs_and_unpacks_a_text_of_lua);
  RUN_TEST(test_sys_settings_answers_what_the_prompt_prints);
  RUN_TEST(test_sys_settings_without_a_door_says_unsupported);
  RUN_TEST(test_bt_device_move_and_click_reach_the_radio);
  RUN_TEST(test_sys_session_destroy_says_how_much_went);
  RUN_TEST(test_an_alarm_that_is_already_there_is_named_and_not_added);
  UNITY_END();
  return 0;
}
