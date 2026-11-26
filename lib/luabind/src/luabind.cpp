// The Lua side of lunokOS: one state per app, an allocator with a ceiling, the
// few functions an app may see, and the task that runs it (docs/20, docs/30).
//
// The app is not run as one call that never returns. It runs inside a coroutine
// of its own, and a promise that has not settled yet makes it wait with a
// lua_yield; the kernel keeps turning and the task is resumed from poll() when
// what it waits for is ready. That is why there is a task here and not just a
// chunk.
#include "json/json.h"
#include <stdio.h>
#include "luabind/luabind.h"
#include "qr/qr.h"
#include "rle/rle.h"

#include <stdlib.h>
#include <string.h>

/* The Lua headers are plain C: they carry no extern "C" guard of their own. */
extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include "pal/pal.h"
#include "hal/hal.h"
// **La traça del gest** (0.188.2): a swipe that does not turn a page has three reasons and none of them is visible on the
// board —a finger that landed on nothing, a travel that nobody asked, a control that is already on its last page—, and this
// is the line that tells them apart without a flash per guess (docs/30, docs/90).
#include "hal/log.h"

namespace luabind {

// **Els dos passos que una tecla pot fer** (0.181.0): declared before the unnamed namespace because the listener that holds the
// keys of a control lives with the queue, higher up than their definitions, and a key it cannot name is a key that does nothing
// (docs/30, docs/90).
bool ui_step_caret(lua_State* L, int objects, int index, int direction, bool letters_only);
bool ui_step_value(lua_State* L, int objects, int index, int direction);

namespace {

const size_t kErrorTextCap = 512;
const int kMaxJobDepth = 8;  // how deep a then() chain may resolve in one go

struct Allocator {
  size_t used;
  size_t peak;
  size_t limit;
};

Allocator g_alloc = {0, 0, 0};
lua_State* g_state = nullptr;  // the state; alive from start() to stop()

// From this size up a block goes to the **extra** memory of the board (the PSRAM of the M5Paper) when
// it has one. Small blocks stay in the board's own RAM, which is faster and where a thousand little
// objects fragment a thousand times less; a table of a few thousand entries gets the extra memory and
// the app never knows —it asks for memory and the interpreter decides where it lives— (docs/75).
const size_t kExtraThreshold = 4096;
lua_State* g_task = nullptr;   // the app's coroutine, null when it has none
Boot g_boot;  // the default Boot says "?" and nothing else
// A copy of the app id. g_boot.app_id points at the caller's text, which may be
// a buffer that dies while the app is still alive (an alarm's entry, a line of
// the shell), so this is the one that is answered.
// **The id is the name the door of the apps presents** (0.88.20): `squares` for the copy of the flash and `squares-SD` for
// the copy of the card, so the room holds the suffix on top of the longest id `apps::Entry` carries. `/cfg` and `/res` are
// hung from this name, and a truncation here would point an app at the files of another one (docs/40, docs/70).
const size_t kAppIdBytes = 40;
char g_app_id[kAppIdBytes] = {0};

// The most a line at the prompt may carry. It is the shell's line (128 bytes) with
// room to spare, and it is here so the interpreter can refuse a line that would not
// fit before it touches the stack (docs/30).
const size_t kPromptLineBytes = 240;

// **Twelve options is a menu and not a page**: the limit is the card's and not the taste's —a card of this glass holds
// four columns of three rows— and an app that needs more than twelve things done to one object is an app that wants
// two menus (docs/30, docs/90).
const uint8_t kMaxMenuItems = 12;

// **Five words in the row of a bar and sixteen entries in the menu that hangs from one of them**: 540 px of paper shared
// between five leaves 108 px for each word, and the sixteen entries are the ceiling the old bar measured for the whole
// tree (docs/30, docs/90).
const uint8_t kMaxMenuBarTitles = 5;
const uint8_t kMaxMenuBarEntries = 16;

// **Three cards in a band of notifications**: what does not fit is not lost, it waits and the band says how many with a
// `+N`, because a notice that disappears without saying so is a notice somebody has lost (docs/30, docs/90).
const uint8_t kMaxNotifications = 3;

// **Forty-eight cells in a grid**: a launcher of this glass shows twelve or sixteen at a time and the app hands over the whole
// list —the page follows the cursor— so this is the ceiling of a page and a half (docs/30, docs/90).
const uint8_t kMaxGridCells = 48;

// Names used inside the registry. None of this is visible to an app.
const char kPromiseMeta[] = "lunokOS.Promise";
const char kPendingKey[] = "lunokOS.pending";
// The promise a network attempt will settle. Only one attempt is ever running,
// so one place is enough (docs/30).
const char kNetKey[] = "lunokOS.net";
// The promise a page will settle. It is a place of its own because the answer is
// a table and not a yes or a number (docs/30).
const char kHttpKey[] = "lunokOS.http";
// The promise a sleep will settle, when the board is back (docs/30).
const char kPowerKey[] = "lunokOS.power";
// The promise a button press will settle (docs/30).
const char kButtonKey[] = "lunokOS.button";
// **La funció de l'app a la cua dels botons** (0.180.0): one function per app —an app has one place in the queue— kept
// by a reference of the registry, and the handle the kernel gave back to give it up with (docs/30, docs/75).
const char kButtonListenerKey[] = "lunokOS.button.listener";
uint16_t g_app_button_handle = 0;
// **Qui té les tecles mentre s'edita** (0.181.0): the control that a person has entered —a box of text, a box of several
// lines, a slider— takes one **exclusive** place in the queue of the kernel while it says `editing` and gives it back the
// moment it does not. It is the one road for the three of them, and it is why the page is not walked while a person is
// writing or dragging (docs/30, docs/90). The slot and the generation are kept with it because that is what says WHICH
// control it is after a page is built again (docs/75).
uint16_t g_keys_handle = 0;
int16_t g_keys_slot = -1;
uint16_t g_keys_generation = 0;
const char kTaskKey[] = "lunokOS.task";

// The promise the task is waiting for, as a registry reference. It is held here
// and handed to the await continuation through lua_KContext, because a promise
// that settles during the same turn must not be confused with the next one.
int g_waiting_ref = LUA_NOREF;

// How the last app ended, for whoever drove it and has to say so.
bool g_failed = false;
int g_exit_code = 0;
bool g_exited_by_app = false;  // app.exit() was called: the error is not one
// **Una app que s'acomiada des d'un ganxo** (0.192.49): `app.exit()` llança un error, i dins d'un `on_pick` o un `on_change`
// aquest error el recull el `pcall` del ganxo i la pàgina segueix viva —el sistema deia `on_pick: app.exit` al registre i no
// tancava res—. El comiat és una intenció i no un error, o sigui que es desa aquí i el torn la compleix: **qualsevol porta de
// l'API es pot fer servir des de qualsevol lloc**, que és el que una app espera d'ella (trobat a `apps/lora`, docs/30, docs/90).
bool g_app_wants_to_stop = false;

// The keyboard of the app. A line is assembled here and handed to whoever is
// waiting for it; the echo is done here too, because the shell is not looking at
// the bytes while an app is alive (docs/30).
const size_t kConsoleLineCap = 128;
char g_console_line[kConsoleLineCap];
size_t g_console_len = 0;
bool g_last_was_cr = false;  // a terminal sends \r\n: the second half is not Enter
int g_read_ref = LUA_NOREF;  // the promise console.read_line() is waiting on

// **The name the app was started with**, copied at `start` because the shell's word is a stack frame that
// does not outlive the command (see `start`) (docs/30).
char g_app_path[128];
char g_app_ext[16];

// **The keyboard in raw mode**: an editor is not a prompt. A prompt waits for a line the terminal has
// assembled and edited; an editor wants the keys themselves —the arrows, the escape, the control
// characters— and paints each one where it decides. `console.raw(true)` says which of the two the app
// is, and from then on every byte the console takes is a byte of the app's (docs/30).
bool g_console_raw = false;
// A ring of the keys that arrived and nobody has read yet: a pasted line arrives faster than a paint,
// and dropping what a person pressed is not an option. Full, the oldest goes (docs/30, docs/75).
const size_t kKeyQueue = 256;
uint8_t g_key_queue[kKeyQueue];
size_t g_key_read = 0;
size_t g_key_count = 0;
int g_byte_ref = LUA_NOREF;  // the promise console.read_byte() is waiting on

// Three ESC in a row, close enough in time, are the way out of an app that has
// stopped listening.
const int kEscapeCount = 3;
const int64_t kEscapeWindowMs = 1500;
int g_escape_count = 0;
int64_t g_escape_first_ms = 0;
bool g_exit_requested = false;

// The escape hatch without the escape key, as a whole line.
const char kExitLine[] = "!!exit";

enum PromiseState { kPending = 0, kOk = 1, kErr = 2 };

// A promise is a userdata with two words of state and a table for the rest
// (value, jobs, watch). Nothing is allocated outside Lua: see docs/75.
// Where the kernel puts what a call costs; empty until it does, and the hook then does nothing.
MeterOps g_meter_ops = {};
// **I la taula de comptadors que una pàgina llegeix** (0.189.108): the numbers of `stats`, empty until the kernel hands them
// over, and `sys.stats()` answers `E_UNSUPPORTED` until then (docs/30).
StatsOps g_stats_ops = {};

struct Promise {
  int state;
  int64_t deadline_ms;  // 0 = no timer; something else settles it
  // **When the call that made this promise started**, in microseconds, or zero when the system is not
  // metering. A call that returns a promise is not over when it returns: it is over when the promise
  // settles, and the meter reads this to know how long it took (docs/30).
  uint32_t meter_start_us;
};

// **Where an app's console words go** (0.160.0): the terminal, unless the app was sent to the background —`run x &`
// detaches its stdin, stdout and stderr from the session that started it— and then the sink the kernel installed,
// which is the log of the system. Two writers on one prompt is the mess `&` exists to avoid (docs/30, docs/50).
bool g_console_attached = true;
void (*g_console_away)(const char* text, size_t len) = nullptr;

void con_write(const char* text, size_t len) {
  if (!g_console_attached) {
    if (g_console_away != nullptr) g_console_away(text, len);
    return;
  }
  pal::IConsole* console = pal::system().console();
  if (console != nullptr) console->write(text, len);
}

void con_text(const char* text) { con_write(text, strlen(text)); }

// **Un número a la consola** (0.190.187): it was next to the escape codes and it is not one —`lua: the interpreter does not fit in
// 8192 bytes` is a sentence and the number is part of it—, so it stays while the codes went (docs/30).
void con_num(int value) {
  char text[12];
  const int written = snprintf(text, sizeof(text), "%d", value);
  if (written > 0) con_write(text, static_cast<size_t>(written));
}

int64_t now_ms() { return static_cast<int64_t>(pal::system().millis()); }

// Every byte Lua asks for goes through here. If the ceiling would be crossed the
// request is refused, Lua raises a memory error and the app dies with a message
// instead of taking the system down with it.
//
// Where the block lives is not the app's problem: a big one goes to the extra memory of the board
// when it has it and a small one to the board's own RAM, and the PAL takes the other home when the
// first one is full (docs/75).
// **The room a memory error is written in**, which is kept out of what an app may take (0.156.0).
constexpr size_t kErrorReserve = 256;

void* counting_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
  // **Qui demana, per als llibres de la memòria**: every block of the interpreter is filed under `lua` and under the app in front
  // (0.190.0, docs/75).
  pal::MemoryTag who_asks(pal::kMemoryLua, app_id());
  Allocator* alloc = static_cast<Allocator*>(ud);
  pal::IExtraMemory& memory = pal::extra_memory();

  if (nsize == 0) {
    if (ptr != nullptr) {
      memory.release(ptr);
      alloc->used -= osize;
    }
    return nullptr;
  }

  const size_t base = (ptr == nullptr) ? alloc->used : (alloc->used - osize);
  const size_t next = base + nsize;
  // **The last two hundred and fifty-six bytes of a ceiling are not the app's** (0.156.0): a state whose ceiling is
  // exactly full cannot build the sentence that says so —the string `not enough memory` needs a few bytes and the
  // allocator says no—, and Lua does not throw at the app then: it **aborts the process** (`luaD_throw` with
  // nobody to catch it). On the emulator that was a SIGABRT of the suite; on the board it would be a restart of the
  // kernel in the middle of an app that only asked for too much. The reserve is the room the sentence is written
  // in, and an app never sees it as its own (docs/20, docs/75).
  if (alloc->limit > 0 && next + kErrorReserve > alloc->limit) return nullptr;

  void* block = memory.resize(ptr, nsize, nsize >= kExtraThreshold);
  if (block == nullptr) return nullptr;

  alloc->used = next;
  if (alloc->used > alloc->peak) alloc->peak = alloc->used;
  return block;
}

// ---------------------------------------------------------------------------
// Promises
// ---------------------------------------------------------------------------

Promise* promise_at(lua_State* L, int idx) {
  return static_cast<Promise*>(lua_touserdata(L, idx));
}

bool is_promise(lua_State* L, int idx) { return luaL_testudata(L, idx, kPromiseMeta) != nullptr; }

// An error an app can read: {code = "E_...", message = "..."} (docs/30).
void push_error_table(lua_State* L, const char* code, const char* message) {
  lua_createtable(L, 0, 2);
  lua_pushstring(L, code);
  lua_setfield(L, -2, "code");
  lua_pushstring(L, message);
  lua_setfield(L, -2, "message");
}

int raise_coded(lua_State* L, const char* code, const char* message) {
  push_error_table(L, code, message);
  return lua_error(L);
}

// The promises that have not settled yet. They live in the registry so that the
// collector, and nothing else, decides when they go away.
void push_pending(lua_State* L) {
  lua_getfield(L, LUA_REGISTRYINDEX, kPendingKey);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, kPendingKey);
  }
}

// Pushes a new promise and returns its absolute stack index.
int push_promise(lua_State* L, int64_t deadline_ms) {
  Promise* p = static_cast<Promise*>(lua_newuserdatauv(L, sizeof(Promise), 1));
  p->state = kPending;
  p->deadline_ms = deadline_ms;
  // **A userdata comes from Lua's heap and is not zeroed.** A promise that nobody timed must not be
  // handed to the meter as if it had a start: that read rubbish once and put an hour and a half into
  // `lua.call` from five calls (measured on the board, 1/11).
  p->meter_start_us = 0;
  luaL_getmetatable(L, kPromiseMeta);
  lua_setmetatable(L, -2);
  lua_newtable(L);  // uservalue: what the promise carries
  lua_pushnil(L);
  lua_setfield(L, -2, "value");
  lua_newtable(L);
  lua_setfield(L, -2, "jobs");
  lua_setiuservalue(L, -2, 1);
  return lua_absindex(L, -1);
}

void pending_add(lua_State* L, int pidx) {
  pidx = lua_absindex(L, pidx);
  push_pending(L);
  const lua_Integer n = static_cast<lua_Integer>(lua_rawlen(L, -1));
  lua_pushvalue(L, pidx);
  lua_rawseti(L, -2, n + 1);
  lua_pop(L, 1);
}

void run_jobs(lua_State* L, int pidx, int depth);

// Gives the promise an answer and runs whatever was waiting on it.
void promise_settle(lua_State* L, int pidx, int state, int vidx) {
  pidx = lua_absindex(L, pidx);
  vidx = lua_absindex(L, vidx);
  Promise* p = promise_at(L, pidx);
  if (p == nullptr || p->state != kPending) return;
  p->state = state;
  p->deadline_ms = 0;
  // **The call is measured until here and not until it returned**: a promise that takes five hundred
  // milliseconds to settle is a call that cost five hundred milliseconds (docs/30).
  if (p->meter_start_us != 0 && g_meter_ops.add_call != nullptr && g_meter_ops.now_us != nullptr) {
    const uint32_t now = g_meter_ops.now_us();
    // A start in the future is not a start: it is a field that never held one, and subtracting it
    // would wrap around and add hours to a call of microseconds (docs/30).
    if (p->meter_start_us <= now) g_meter_ops.add_call(now - p->meter_start_us);
    p->meter_start_us = 0;
  }
  lua_getiuservalue(L, pidx, 1);
  lua_pushvalue(L, vidx);
  lua_setfield(L, -2, "value");
  lua_pop(L, 1);
  run_jobs(L, pidx, 0);
}

// dst takes the state and the value of src.
void copy_result(lua_State* L, int dst, int src) {
  dst = lua_absindex(L, dst);
  src = lua_absindex(L, src);
  const Promise* s = promise_at(L, src);
  if (s == nullptr) return;
  const int base = lua_gettop(L);
  lua_getiuservalue(L, src, 1);
  lua_getfield(L, -1, "value");
  promise_settle(L, dst, s->state, lua_gettop(L));
  lua_settop(L, base);
}

// Runs the continuations of a promise that has just settled. The list is emptied
// first: a continuation may settle another promise, and that one must not run
// these same jobs a second time.
void run_jobs(lua_State* L, int pidx, int depth) {
  if (depth > kMaxJobDepth) return;
  pidx = lua_absindex(L, pidx);
  const Promise* p = promise_at(L, pidx);
  if (p == nullptr) return;
  const int state = p->state;
  const int base = lua_gettop(L);

  lua_getiuservalue(L, pidx, 1);
  const int uv = lua_absindex(L, -1);
  lua_getfield(L, uv, "jobs");
  const int jobs = lua_absindex(L, -1);
  const lua_Integer count =
      lua_istable(L, jobs) ? static_cast<lua_Integer>(lua_rawlen(L, jobs)) : 0;
  lua_newtable(L);
  lua_setfield(L, uv, "jobs");

  for (lua_Integer i = 1; i <= count; ++i) {
    const int itbase = lua_gettop(L);
    lua_rawgeti(L, jobs, i);
    if (lua_istable(L, -1)) {
      const int job = lua_absindex(L, -1);
      lua_getfield(L, job, "child");
      const int child = lua_absindex(L, -1);
      lua_getfield(L, job, "fin");
      const bool is_final = lua_toboolean(L, -1) != 0;
      lua_pop(L, 1);
      lua_getfield(L, job, (state == kOk) ? "ok" : "err");
      const bool has_callback = lua_isfunction(L, -1);

      if (has_callback && !is_final) {
        lua_getfield(L, uv, "value");
        if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
          promise_settle(L, child, kErr, lua_gettop(L));
        } else {
          promise_settle(L, child, kOk, lua_gettop(L));
        }
      } else if (has_callback) {
        if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
          promise_settle(L, child, kErr, lua_gettop(L));
        } else {
          copy_result(L, child, pidx);  // finally keeps the answer it saw
        }
      } else {
        copy_result(L, child, pidx);  // nobody listening: the child takes it all
      }
      lua_settop(L, itbase);
    } else {
      lua_settop(L, itbase);
    }
  }
  lua_settop(L, base);
}

// What an await returns, or the error it raises.
int push_result(lua_State* L, int pidx) {
  pidx = lua_absindex(L, pidx);
  const Promise* p = promise_at(L, pidx);
  if (p == nullptr) return raise_coded(L, "E_INVAL", "not a promise");
  lua_getiuservalue(L, pidx, 1);
  lua_getfield(L, -1, "value");
  if (p->state == kErr) return lua_error(L);
  return 1;
}

int l_promise_await_cont(lua_State* L, int status, lua_KContext ctx) {
  (void)status;
  // ctx is the registry reference of the promise this await was waiting for.
  const int ref = static_cast<int>(ctx);
  lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
  luaL_unref(L, LUA_REGISTRYINDEX, ref);
  if (g_waiting_ref == ref) g_waiting_ref = LUA_NOREF;
  return push_result(L, lua_gettop(L));
}

// p:await() — the only thing that suspends a task.
int l_promise_await(lua_State* L) {
  if (!is_promise(L, 1)) return raise_coded(L, "E_INVAL", "await needs a promise");
  if (L != g_task) return raise_coded(L, "E_NOTASK", "await outside a task");
  const Promise* p = promise_at(L, 1);
  if (p->state != kPending) return push_result(L, 1);

  lua_pushvalue(L, 1);
  const int ref = luaL_ref(L, LUA_REGISTRYINDEX);  // keeps it alive while waiting
  g_waiting_ref = ref;
  return lua_yieldk(L, 1, static_cast<lua_KContext>(ref), &l_promise_await_cont);
}

// then / catch / finally share one body: mode 0 = then, 1 = catch, 2 = finally.
int promise_chain(lua_State* L, int mode) {
  lua_settop(L, 3);  // the callbacks are nil when the app did not pass them
  if (!is_promise(L, 1)) return raise_coded(L, "E_INVAL", "not a promise");
  const int child = push_promise(L, 0);

  lua_createtable(L, 0, 4);
  const int job = lua_absindex(L, -1);
  lua_pushvalue(L, child);
  lua_setfield(L, job, "child");
  if (mode == 1) {
    lua_pushnil(L);
    lua_setfield(L, job, "ok");
    lua_pushvalue(L, 2);
    lua_setfield(L, job, "err");
    lua_pushboolean(L, 0);
  } else if (mode == 2) {
    lua_pushvalue(L, 2);
    lua_setfield(L, job, "ok");
    lua_pushvalue(L, 2);
    lua_setfield(L, job, "err");
    lua_pushboolean(L, 1);
  } else {
    lua_pushvalue(L, 2);
    lua_setfield(L, job, "ok");
    lua_pushvalue(L, 3);
    lua_setfield(L, job, "err");
    lua_pushboolean(L, 0);
  }
  lua_setfield(L, job, "fin");

  lua_getiuservalue(L, 1, 1);
  const int uv = lua_absindex(L, -1);
  lua_getfield(L, uv, "jobs");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, uv, "jobs");
  }
  const lua_Integer n = static_cast<lua_Integer>(lua_rawlen(L, -1));
  lua_pushvalue(L, job);
  lua_rawseti(L, -2, n + 1);
  lua_pop(L, 2);  // jobs, uv

  const Promise* parent = promise_at(L, 1);
  if (parent != nullptr && parent->state != kPending) run_jobs(L, 1, 0);
  lua_pushvalue(L, child);
  return 1;
}

int l_promise_and_then(lua_State* L) { return promise_chain(L, 0); }

int l_promise_catch(lua_State* L) { return promise_chain(L, 1); }

int l_promise_finally(lua_State* L) { return promise_chain(L, 2); }

// p:timeout(ms) — copies p when p settles, and fails with E_TIMEOUT if the time
// passes first.
int l_promise_timeout(lua_State* L) {
  if (!is_promise(L, 1)) return raise_coded(L, "E_INVAL", "not a promise");
  lua_Integer ms = luaL_optinteger(L, 2, 0);
  if (ms < 0) ms = 0;
  const int q = push_promise(L, now_ms() + static_cast<int64_t>(ms));
  lua_getiuservalue(L, q, 1);
  lua_pushvalue(L, 1);
  lua_setfield(L, -2, "watch");
  lua_pop(L, 1);
  pending_add(L, q);
  lua_pushvalue(L, q);
  return 1;
}

// p.state is a field, not a call (docs/30).
int l_promise_index(lua_State* L) {
  if (lua_type(L, 2) == LUA_TSTRING && strcmp(lua_tostring(L, 2), "state") == 0) {
    const Promise* p = promise_at(L, 1);
    const char* state = "pending";
    if (p != nullptr && p->state == kOk) state = "ok";
    if (p != nullptr && p->state == kErr) state = "err";
    lua_pushstring(L, state);
    return 1;
  }
  luaL_getmetatable(L, kPromiseMeta);
  lua_pushvalue(L, 2);
  lua_rawget(L, -2);
  return 1;
}

int l_promise_resolve(lua_State* L) {
  if (lua_gettop(L) < 1) lua_pushnil(L);
  const int p = push_promise(L, 0);
  promise_settle(L, p, kOk, 1);
  lua_pushvalue(L, p);
  return 1;
}

int l_promise_reject(lua_State* L) {
  if (lua_gettop(L) < 1) lua_pushnil(L);
  const int p = push_promise(L, 0);
  promise_settle(L, p, kErr, 1);
  lua_pushvalue(L, p);
  return 1;
}

int l_promise_is(lua_State* L) {
  lua_pushboolean(L, is_promise(L, 1));
  return 1;
}

// Settles one pending promise if its time has come. A promise with a watch
// copies it the moment it settles, and fails with E_TIMEOUT when its own time
// is up and the watched one is still going.
void settle_one(lua_State* L, int pidx, int64_t now) {
  Promise* p = promise_at(L, pidx);
  if (p == nullptr || p->state != kPending) return;
  const int base = lua_gettop(L);
  lua_getiuservalue(L, pidx, 1);
  lua_getfield(L, -1, "watch");
  const bool watching = lua_isuserdata(L, -1);
  lua_pop(L, 1);  // watch

  if (watching) {
    lua_getfield(L, -1, "watch");
    const int w = lua_absindex(L, -1);
    const Promise* watched = promise_at(L, w);
    if (watched != nullptr && watched->state != kPending) {
      copy_result(L, pidx, w);
    } else if (p->deadline_ms != 0 && now >= p->deadline_ms) {
      push_error_table(L, "E_TIMEOUT", "the promise did not settle in time");
      promise_settle(L, pidx, kErr, lua_gettop(L));
    }
    lua_pop(L, 1);  // watch
  } else if (p->deadline_ms != 0 && now >= p->deadline_ms) {
    lua_pushnil(L);
    promise_settle(L, pidx, kOk, lua_gettop(L));
    lua_pop(L, 1);
  }
  lua_settop(L, base);
}

// Walks the pending promises once, settles what has to settle and leaves the
// list with only what is still waiting.
void settle_due() {
  lua_State* L = g_state;
  if (L == nullptr) return;
  const int64_t now = now_ms();
  const int base = lua_gettop(L);

  push_pending(L);
  const int old = lua_absindex(L, -1);
  const lua_Integer count = static_cast<lua_Integer>(lua_rawlen(L, old));
  lua_newtable(L);
  const int fresh = lua_absindex(L, -1);
  int kept = 0;

  for (lua_Integer i = 1; i <= count; ++i) {
    const int itbase = lua_gettop(L);
    lua_rawgeti(L, old, i);
    if (lua_isuserdata(L, -1)) {
      const int p = lua_absindex(L, -1);
      settle_one(L, p, now);
      const Promise* promise = promise_at(L, p);
      if (promise != nullptr && promise->state == kPending) {
        lua_pushvalue(L, p);
        lua_rawseti(L, fresh, ++kept);
      }
    }
    lua_settop(L, itbase);
  }

  lua_pushvalue(L, fresh);
  lua_setfield(L, LUA_REGISTRYINDEX, kPendingKey);
  lua_settop(L, base);
}

void open_promises(lua_State* L) {
  luaL_newmetatable(L, kPromiseMeta);
  lua_pushcfunction(L, &l_promise_index);
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, &l_promise_await);
  lua_setfield(L, -2, "await");
  // Not `then`: that is a reserved word in Lua and would only be reachable as
  // p["then"], which is a trap for whoever writes an app.
  lua_pushcfunction(L, &l_promise_and_then);
  lua_setfield(L, -2, "and_then");
  lua_pushcfunction(L, &l_promise_catch);
  lua_setfield(L, -2, "catch");
  lua_pushcfunction(L, &l_promise_finally);
  lua_setfield(L, -2, "finally");
  lua_pushcfunction(L, &l_promise_timeout);
  lua_setfield(L, -2, "timeout");
  lua_pushstring(L, "Promise");
  lua_setfield(L, -2, "__name");
  lua_pop(L, 1);

  lua_createtable(L, 0, 3);
  lua_pushcfunction(L, &l_promise_resolve);
  lua_setfield(L, -2, "resolve");
  lua_pushcfunction(L, &l_promise_reject);
  lua_setfield(L, -2, "reject");
  lua_pushcfunction(L, &l_promise_is);
  lua_setfield(L, -2, "is");
  lua_setglobal(L, "Promise");
}

// ---------------------------------------------------------------------------
// What a call into the system costs
// ---------------------------------------------------------------------------

// **One hook and no wrapper per function.** A closure per function of the API would be around five
// kilobytes of the app's own heap, and the ATOM gives an app sixty-four; the interpreter's hook costs
// nothing but a call, and it sees every call and every return. A call whose `currentline` is -1 is a C
// function, which is exactly what a call to this system is —a function of Lua that an app wrote is its
// own business and is not counted (docs/30, docs/75).
int g_call_depth = 0;
bool g_timing_call = false;
bool g_timing_c = false;  // the call being timed is a C function and not one of the app's
uint32_t g_call_start = 0;

// **The instructions of the app, priced** (docs/85). The board runs every instruction of the interpreter
// at a few tens of nanoseconds and the PC at a fraction of that, so a loop that takes a second on the
// glass takes a millisecond here. What one instruction costs is the band's answer —zero on a board,
// where the time is the real one— and this counts them in blocks of `kHookInstructions` and pays in one
// go, because a sleep per instruction would be an emulator that asks for time more often than it simulates
// it.
const uint32_t kHookInstructions = 2000;
const uint32_t kPayThresholdUs = 500;
uint32_t g_pending_us = 0;

// **The turn budget** (0.90.0): when a turn reaches the budget, the count hook yields the coroutine back to the
// kernel. It is a slice and not a kill: an app that never yields keeps running, but the console, the alarms, the
// button, the glass and the radio get a turn between slices (docs/20, docs/75).
//
// **What a budget is worth in milliseconds is a measurement and not a guess**: the emulator's model prices an
// instruction at `kLuaInstructionNs` (60 ns, `--lua-cpu`), so 100 000 of them are **6 ms** there —and a slice of
// "~0.6 ms", what this comment said until 0.169.3, was the same arithmetic with a zero lost—. A board says its own
// with `run luaperf` (docs/95), which is the only number worth deciding on.
//
// **And the number is the latency of the board** (0.174.7): a turn of the app that runs to its end is a turn in which
// nobody read the glass, the keys or the radio, so a budget that takes six milliseconds is a finger that waits six of
// them. The owner asked for a system that answers the hand and not one that thinks a lot between two looks, so the
// build now ships **twenty thousand** —ten blocks, about 1,2 ms at 60 ns a instruction— and the number is a setting
// of the board: `cfg turn <n>` (docs/20, docs/75).
const uint32_t kTurnBudgetDefault = 20000;    // 10 blocks of kHookInstructions
// **A slice is at least one block**: below it the hook would never see the counter reach the budget and no app would
// ever be cut, which is the thing this exists to prevent.
const uint32_t kTurnBudgetMin = kHookInstructions;
// **And a slice has a ceiling**: past two hundred thousand instructions (twelve milliseconds here) the loop belongs
// to the app and the kernel looks once in a while, which is the behaviour this door exists to stop everybody from
// choosing by accident.
const uint32_t kTurnBudgetMax = 200000;
uint32_t g_turn_budget = kTurnBudgetDefault;
uint32_t g_turn_instructions = 0;
bool g_budget_pending = false;
bool g_budget_yielded = false;
bool g_call_split = false;

void pay_the_instructions(uint32_t pending) {
  if (g_meter_ops.spend_us != nullptr && pending > 0) g_meter_ops.spend_us(pending);
}

void meter_hook(lua_State* L, lua_Debug* ar) {
  if (ar == nullptr) return;
  if (ar->event == LUA_HOOKCOUNT) {
    // **The block is priced first**, so the yield of the budget pays the instructions that led to it and
    // a turn boundary does not lose the last two thousand of them (docs/85).
    uint32_t ns = 0;
    if (g_meter_ops.instruction_ns != nullptr) ns = g_meter_ops.instruction_ns();
    if (ns > 0) {
      g_pending_us += (kHookInstructions * ns) / 1000u;
      if (g_pending_us >= kPayThresholdUs) {
        pay_the_instructions(g_pending_us);
        g_pending_us = 0;
      }
    }

    // **The turn budget is counted on the app's own task**, and not in the one-turn window of an alarm: a
    // yield from a nested coroutine would return to `coroutine.resume` and not to the kernel, and a window
    // has no next turn (docs/20, docs/75).
    if (L == g_task && !g_boot.window) {
      g_turn_instructions += kHookInstructions;
      if (g_turn_instructions >= g_turn_budget) g_budget_pending = true;
      if (g_budget_pending && lua_isyieldable(L)) {
        // A call in progress is split: what it cost up to the yield is paid now, and `poll` restarts the
        // clock at the resume, so the wait between turns is not charged to the call (docs/30).
        if (g_timing_call && g_meter_ops.now_us != nullptr && g_meter_ops.add_call != nullptr) {
          g_meter_ops.add_call(g_meter_ops.now_us() - g_call_start);
          g_call_split = true;
        }
        g_budget_pending = false;
        g_budget_yielded = true;
        pay_the_instructions(g_pending_us);
        g_pending_us = 0;
        lua_yield(L, 0);  // 5.4 allows a hook to yield, with no values and no continuation
        return;           // not reached: lua_yield unwinds
      }
    }
    return;
  }
  if (ar->event == LUA_HOOKCALL) {
    ++g_call_depth;
    // **The outermost call is timed whoever it is**: a function of the app counts as the interpreter
    // working, and a C function counts in both —the interpreter and a call into the system— (docs/30).
    if (g_call_depth == 1 && g_meter_ops.now_us != nullptr) {
      g_timing_call = true;
      g_timing_c = (ar->currentline < 0);
      g_call_start = g_meter_ops.now_us();
    }
    return;
  }
  if (ar->event != LUA_HOOKRET) return;
  if (g_call_depth == 1 && g_timing_call) {
    const uint32_t elapsed = g_meter_ops.now_us() - g_call_start;
    if (g_timing_c) {
      // **A call that returns a promise is not over.** The time goes to the promise and the call is
      // closed when it settles: a read of the card that answers later costs until it answers, which is
      // what the app waited for (docs/30).
      bool handled = false;
      if (lua_gettop(L) >= 1 && is_promise(L, -1)) {
        Promise* promise = promise_at(L, -1);
        if (promise != nullptr && promise->state == kPending) {
          promise->meter_start_us = g_call_start;
          handled = true;
        }
      }
      if (!handled && g_meter_ops.add_call != nullptr) g_meter_ops.add_call(elapsed);
    }
    g_timing_call = false;
    g_timing_c = false;
  }
  if (g_call_depth > 0) --g_call_depth;
}

// ---------------------------------------------------------------------------
// What an app can see
// ---------------------------------------------------------------------------

void set_function(lua_State* L, const char* name, lua_CFunction fn) {
  lua_pushcfunction(L, fn);
  lua_setfield(L, -2, name);
}

// print and console.print: text out, tab between arguments, newline at the end.
int l_print(lua_State* L) {
  const int count = lua_gettop(L);
  for (int i = 1; i <= count; ++i) {
    if (i > 1) con_write("\t", 1);
    size_t len = 0;
    const char* text = luaL_tolstring(L, i, &len);
    if (text != nullptr) con_write(text, len);
    lua_pop(L, 1);
  }
  con_write("\r\n", 2);
  return 0;
}

int l_console_write(lua_State* L) {
  size_t len = 0;
  const char* text = luaL_checklstring(L, 1, &len);
  con_write(text, len);
  lua_pushinteger(L, static_cast<lua_Integer>(len));
  return 1;
}

// console.read_line(): a promise that Enter settles with the line, without the
// line ending. One read at a time: two would not know which line is whose.
int l_console_read_line(lua_State* L) {
  if (L != g_task) return raise_coded(L, "E_NOTASK", "read_line outside a task");
  if (g_read_ref != LUA_NOREF) return raise_coded(L, "E_BUSY", "a read is already waiting");
  const int p = push_promise(L, 0);  // no deadline: the keyboard settles it
  lua_pushvalue(L, p);
  g_read_ref = luaL_ref(L, LUA_REGISTRYINDEX);
  lua_pushvalue(L, p);
  return 1;
}

// One ESC. Three of them inside the window are a way out of the app.
void count_escape() {
  const int64_t now = now_ms();
  if (g_escape_count == 0 || (now - g_escape_first_ms) > kEscapeWindowMs) {
    g_escape_count = 0;
    g_escape_first_ms = now;
  }
  ++g_escape_count;
  if (g_escape_count >= kEscapeCount) {
    g_escape_count = 0;
    g_exit_requested = true;
    con_write("\r\n", 2);
  }
}

// One key in, one key out. The ring grows from `g_key_read` and wraps, so a key is a copy of one byte
// and the bookkeeping of a buffer of them is two numbers (docs/75).
void key_push(uint8_t byte) {
  if (g_key_count >= kKeyQueue) {
    g_key_read = (g_key_read + 1) % kKeyQueue;
    --g_key_count;
  }
  g_key_queue[(g_key_read + g_key_count) % kKeyQueue] = byte;
  ++g_key_count;
}

uint8_t key_pop() {
  const uint8_t byte = g_key_queue[g_key_read];
  g_key_read = (g_key_read + 1) % kKeyQueue;
  --g_key_count;
  return byte;
}

// **The key a `console.read_byte()` is waiting for**: it settles the promise with the oldest one, and
// an app that is not waiting keeps them for the read that comes (docs/30).
void settle_byte() {
  if (g_byte_ref == LUA_NOREF || g_key_count == 0 || g_state == nullptr) return;
  lua_State* L = g_state;
  const int base = lua_gettop(L);
  const int ref = g_byte_ref;
  g_byte_ref = LUA_NOREF;
  lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
  luaL_unref(L, LUA_REGISTRYINDEX, ref);
  const int p = lua_absindex(L, -1);
  lua_pushinteger(L, static_cast<lua_Integer>(key_pop()));
  promise_settle(L, p, kOk, lua_gettop(L));
  lua_settop(L, base);
}

// console.read_byte(): the next key of the console, 0–255. A promise like the line, so an app waits for
// a key without stopping the system, and a key that is already here settles it at once (docs/30).
int l_console_read_byte(lua_State* L) {
  if (L != g_task) return raise_coded(L, "E_NOTASK", "read_byte outside a task");
  if (g_byte_ref != LUA_NOREF) return raise_coded(L, "E_BUSY", "a read is already waiting");
  const int p = push_promise(L, 0);  // no deadline: the keyboard settles it
  lua_pushvalue(L, p);
  g_byte_ref = luaL_ref(L, LUA_REGISTRYINDEX);
  settle_byte();
  lua_pushvalue(L, p);
  return 1;
}

// console.raw([on]) **changes what the keyboard is**: while it is on there is no line, no echo and no
// editing, and `console.read_byte` is the only way to the keys —which is what an editor is (docs/30).
int l_console_raw(lua_State* L) {
  if (lua_gettop(L) >= 1) {
    const bool on = lua_toboolean(L, 1) != 0;
    if (on != g_console_raw) {
      g_console_raw = on;
      // A line half typed is not a key, and a key half read is not a line: the mode changes what the
      // console is for, and what was in the other one is not carried over (docs/30).
      g_console_len = 0;
      g_key_read = 0;
      g_key_count = 0;
      g_last_was_cr = false;
    }
  }
  lua_pushboolean(L, g_console_raw);
  return 1;
}

// Enter: the line goes to whoever is waiting for it, or it is a request to leave.
void finish_console_line() {
  con_write("\r\n", 2);
  if (g_read_ref != LUA_NOREF) {
    lua_State* L = g_state;
    const int base = lua_gettop(L);
    const int ref = g_read_ref;
    g_read_ref = LUA_NOREF;
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);  // the promise
    luaL_unref(L, LUA_REGISTRYINDEX, ref);
    const int p = lua_absindex(L, -1);
    lua_pushlstring(L, g_console_line, g_console_len);
    promise_settle(L, p, kOk, lua_gettop(L));
    lua_settop(L, base);
  } else if (g_console_len == sizeof(kExitLine) - 1 &&
             memcmp(g_console_line, kExitLine, sizeof(kExitLine) - 1) == 0) {
    // The escape hatch without the escape key: only when the app is not reading.
    g_exit_requested = true;
  }
  g_console_len = 0;
}

int l_sys_heap(lua_State* L) {
  const size_t free_bytes = (g_alloc.limit > g_alloc.used) ? (g_alloc.limit - g_alloc.used) : 0;
  lua_createtable(L, 0, 8);
  lua_pushinteger(L, static_cast<lua_Integer>(free_bytes));
  lua_setfield(L, -2, "free");
  lua_pushinteger(L, static_cast<lua_Integer>(g_alloc.used));
  lua_setfield(L, -2, "used");
  lua_pushinteger(L, static_cast<lua_Integer>(g_alloc.limit));
  lua_setfield(L, -2, "limit");
  lua_pushinteger(L, static_cast<lua_Integer>(g_alloc.peak));
  lua_setfield(L, -2, "peak");
  // The extra memory of the board (the PSRAM of the M5Paper): how much the CPU can address and what
  // is left of it. On a board without any both are zero, which is the truth and not an error
  // (docs/75).
  const hal::ExtraMemory extra = hal::platform().extra_memory();
  lua_pushinteger(L, static_cast<lua_Integer>(extra.total_bytes));
  lua_setfield(L, -2, "extra_total");
  lua_pushinteger(L, static_cast<lua_Integer>(extra.free_bytes));
  lua_setfield(L, -2, "extra_free");
  // **And what the board's own RAM has left** (0.151.20): not the sum with the extra memory, which is the number that
  // hid a whole afternoon of `ENOMEM` —the `fopen` of a file answered that with 1,7 MB of PSRAM free while this pool was
  // empty—. It is the pool the drivers and the file system need, and the floor `pal::kHomeReserveBytes` is what keeps it
  // (docs/75).
  const pal::HeapInfo board = pal::system().heap();
  lua_pushinteger(L, static_cast<lua_Integer>(board.free_bytes));
  lua_setfield(L, -2, "ram_free");
  lua_pushinteger(L, static_cast<lua_Integer>(board.largest_free_bytes));
  lua_setfield(L, -2, "ram_largest");
  return 1;
}

int l_sys_time_ms(lua_State* L) {
  lua_pushinteger(L, static_cast<lua_Integer>(now_ms()));
  return 1;
}

// **sys.turn()**: how many instructions of this app a turn is worth (0.174.7). The number is the board's and it is a
// setting (`cfg turn <n>`), so an app that reasons about turns —`luaperf` measures the price of one— asks for it here
// and does not carry a copy of a number that somebody can change at the prompt (docs/30, docs/75).
int l_sys_turn(lua_State* L) {
  lua_pushinteger(L, static_cast<lua_Integer>(g_turn_budget));
  return 1;
}
int l_sys_gc(lua_State* L) {
  const char* mode = luaL_optstring(L, 1, "step");
  if (strcmp(mode, "full") == 0) {
    lua_gc(L, LUA_GCCOLLECT, 0);
    return 0;
  }
  if (strcmp(mode, "count") == 0) {
    lua_pushinteger(L, static_cast<lua_Integer>(lua_gc(L, LUA_GCCOUNT, 0)));
    return 1;
  }
  lua_gc(L, LUA_GCSTEP, 0);
  return 0;
}

// sys.caps(): the flat table of what this board has (docs/50 is the source of
// the names, hal::cap_name() is the source of truth).
int l_sys_caps(lua_State* L) {
  const hal::Caps& caps = hal::platform().caps();
  const uint32_t count = hal::cap_count();
  lua_createtable(L, 0, static_cast<int>(count));
  for (uint32_t i = 0; i < count; ++i) {
    const bool has = caps.has(static_cast<hal::Capability>(hal::cap_bit(i)));
    lua_pushboolean(L, has ? 1 : 0);
    lua_setfield(L, -2, hal::cap_name(i));
  }
  return 1;
}

// ---------------------------------------------------------------------------
// time: the wall clock, the one that may jump
// ---------------------------------------------------------------------------

hal::IClock* wall_clock() { return hal::platform().clock(); }

int64_t epoch_ms_or_zero() {
  hal::IClock* clock = wall_clock();
  return (clock != nullptr) ? clock->epoch_ms() : 0;
}

// **The operations and the two tools of an error are defined further down**, with the rest of the doors of the system:
// they are named here because a `time` call that needs them comes before them in this file, and the alternative —a
// second copy of the error table— is the thing this comment is here to prevent (docs/30).
extern TimeOps g_time_ops;
int push_nil_error(lua_State* L, const char* code, const char* message);

int l_time_now(lua_State* L) {
  lua_pushinteger(L, static_cast<lua_Integer>(epoch_ms_or_zero() / 1000));
  return 1;
}

int l_time_now_ms(lua_State* L) {
  lua_pushinteger(L, static_cast<lua_Integer>(epoch_ms_or_zero()));
  return 1;
}

// time.caps(): where the epoch comes from, and whether it can be trusted to
// survive the power going away (docs/60). `synced` is false and stays false: nothing in the kernel
// remembers a sync, and NTP, which is there since 0.5.0, does not set it (docs/90).
// **`time.set(epoch)`: posar l'hora de la placa** (0.189.106). The RTC of the board is the one that makes `time.now()` an
// hour instead of a counter, and the page that owns it is the panel of Settings —a person who has no network and a board that
// came back from the shop with 1970 has to be able to say what day it is—. The argument is **seconds since the epoch**, the
// same number `time.now()` answers, and the answer is the epoch the clock kept after trying: a board with no battery behind
// its clock answers the number it was given until it is unplugged, which is the truth and not an error (docs/60).
int l_time_set(lua_State* L) {
  hal::IClock* clock = wall_clock();
  if (clock == nullptr) return push_nil_error(L, "E_UNSUPPORTED", "this board has no clock to set");
  const lua_Integer seconds = luaL_checkinteger(L, 1);
  if (seconds < 0) return push_nil_error(L, "E_INVAL", "an hour is a number of seconds since the epoch");
  if (!clock->set_epoch_ms(static_cast<int64_t>(seconds) * 1000)) {
    return push_nil_error(L, "E_UNAVAIL", "this clock does not take an hour");
  }
  lua_pushinteger(L, static_cast<lua_Integer>(clock->epoch_ms() / 1000));
  return 1;
}

int l_time_caps(lua_State* L) {
  hal::IClock* clock = wall_clock();
  lua_createtable(L, 0, 5);
  lua_pushstring(L, (clock != nullptr) ? clock->source() : "none");
  lua_setfield(L, -2, "source");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "synced");
  lua_pushstring(L, (clock != nullptr && clock->can_wake()) ? "yes" : "no");
  lua_setfield(L, -2, "wake");
  // **Quin xip és i si el pot portar** (0.190.153, l'amo: *«Afegeix l'informació a l'app Battery»*): `chip` és el model del
  // rellotge amb pila —"PCF8563" a la T5 S3 Pro, "BM8563" al Paper, buit on no n'hi ha cap— i `cell` és el que aquell xip diu
  // de si mateix: `"ok"` la guarda, `"lost"` la seva pila el va deixar (el bit VL), `"silent"` no contesta i `"none"` no hi és.
  // Una app que viu d'una alarma ho necessita: una mostra que depèn d'un rellotge sense pila només arriba amb la placa encesa
  // (docs/30, docs/50, docs/60).
  lua_pushstring(L, (clock != nullptr) ? clock->chip_name() : "");
  lua_setfield(L, -2, "chip");
  lua_pushstring(L, (clock != nullptr) ? clock->chip_state() : "none");
  lua_setfield(L, -2, "cell");
  return 1;
}

// **The parts of a moment** (0.125.0): the year, the month, the day, the hour, the minute and the second that an hour
// on the glass is made of, plus the weekday and the day of the year. `time.now()` is a number and a person reads a
// date, so this is the one translation there is —and it is the system's, because the calendar rule lives once
// (`hal/civil.h`, `kernel/calendar.h`) and a second copy of it is a second answer (docs/30, docs/60, docs/90).
//
// **`wday` is 1 for Monday and 7 for Sunday**, the same count the calendar of this system uses everywhere else, and
// `yday` starts at 1 on the first of January.
void push_civil(lua_State* L, int64_t epoch_ms) {
  int year = 1970;
  int month = 1;
  int day = 1;
  int hour = 0;
  int minute = 0;
  int second = 0;
  hal::epoch_ms_to_civil(epoch_ms, year, month, day, hour, minute, second);
  lua_createtable(L, 0, 8);
  lua_pushinteger(L, year);
  lua_setfield(L, -2, "year");
  lua_pushinteger(L, month);
  lua_setfield(L, -2, "month");
  lua_pushinteger(L, day);
  lua_setfield(L, -2, "day");
  lua_pushinteger(L, hour);
  lua_setfield(L, -2, "hour");
  lua_pushinteger(L, minute);
  lua_setfield(L, -2, "min");
  lua_pushinteger(L, second);
  lua_setfield(L, -2, "sec");
  const uint8_t wday = (g_time_ops.weekday_of != nullptr)
                           ? g_time_ops.weekday_of(static_cast<uint16_t>(year), static_cast<uint8_t>(month),
                                                   static_cast<uint8_t>(day))
                           : 0;
  lua_pushinteger(L, wday);
  lua_setfield(L, -2, "wday");
  // **The day of the year is a difference of the calendar the HAL already has**, not a sum of the months written here:
  // two dates of the same year and one subtraction, and February is nobody's business in this file (docs/60).
  const int64_t yday = hal::days_from_civil(year, month, day) - hal::days_from_civil(year, 1, 1) + 1;
  lua_pushinteger(L, static_cast<lua_Integer>(yday));
  lua_setfield(L, -2, "yday");
}

// The moment the call is about: the number it was given, or now.
int64_t moment_ms(lua_State* L, int index) {
  if (lua_isnumber(L, index)) {
    return static_cast<int64_t>(lua_tointeger(L, index)) * 1000;
  }
  return epoch_ms_or_zero();
}

// time.utc([epoch]): the moment as the chip counts it, which is UTC (docs/30, docs/60).
int l_time_utc(lua_State* L) {
  push_civil(L, moment_ms(L, 1));
  return 1;
}

// **time.date([epoch])**: the same moment in the hour of the person, which is the offset `cfg tz` keeps. With no offset
// set it is `time.utc()`, and that is the truth of a board nobody has told where it lives (docs/30, docs/60).
//
// **It is `date` and not `local`** because `local` is a word of Lua and `time.local()` does not even parse —the same
// wall the `in` of an alarm hit (docs/30)—, and `date` is the name the paper project gave this call, which is one more
// row of the equivalence table of docs/45 with nothing to explain.
// **L'offset efectiu i l'època d'una hora local** (0.189.130): the one number and the one inverse that every page needs and
// that each of them was writing for itself —the Time zone page summed the hour of summer by hand and the Settings page did not,
// so the same board showed +03:00 in one and +04:00 in the other, and a clock set from one of them landed an hour away—. The
// arithmetic is the kernel's (`cfg::offset_minutes`), the caller only says which hour of the wall it means (docs/30, docs/60).
int l_time_offset(lua_State* L) {
  const int32_t minutes = (g_time_ops.offset_minutes != nullptr)
                              ? g_time_ops.offset_minutes()
                              : ((g_time_ops.tz_minutes != nullptr) ? g_time_ops.tz_minutes() : 0);
  lua_pushinteger(L, static_cast<lua_Integer>(minutes));
  return 1;
}

int l_time_epoch(lua_State* L) {
  // The five numbers of a wall clock, as `time.date()` answers them: year, month, day, hour, minute.
  const lua_Integer year = luaL_checkinteger(L, 1);
  const lua_Integer month = luaL_checkinteger(L, 2);
  const lua_Integer day = luaL_checkinteger(L, 3);
  const lua_Integer hour = luaL_optinteger(L, 4, 0);
  const lua_Integer minute = luaL_optinteger(L, 5, 0);
  if (month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59) {
    return push_nil_error(L, "E_INVAL", "that is not a day of the calendar");
  }
  // **El trenta-u de febrer no és un dia i no s'ha de convertir en el dos de març** (0.189.142): `days_from_civil` is a count of
  // days and not a calendar —it will hand back the two of March for a day that does not exist—, and the page of the settings
  // offers the day thirty-one of every month, so a person who chose it would have set the clock to a day they never asked for
  // and the page would have said the one they did. The month's length is the calendar's and **the leap year is counted and not
  // set** (0.192.57): `hal::is_leap_year` is the one copy of the rule, la mateixa que compta el febrer de `time.date`
  // (docs/30, docs/50, docs/60).
  int last = 31;
  if (month == 4 || month == 6 || month == 9 || month == 11) {
    last = 30;
  } else if (month == 2) {
    last = hal::is_leap_year(year) ? 29 : 28;
  }
  if (day > last) {
    return push_nil_error(L, "E_INVAL", "that day is not in that month");
  }
  const int32_t shift = (g_time_ops.offset_minutes != nullptr)
                            ? g_time_ops.offset_minutes()
                            : ((g_time_ops.tz_minutes != nullptr) ? g_time_ops.tz_minutes() : 0);
  const int64_t days = hal::days_from_civil(static_cast<int32_t>(year), static_cast<uint32_t>(month),
                                            static_cast<uint32_t>(day));
  const int64_t seconds = days * 86400 + hour * 3600 + minute * 60 - static_cast<int64_t>(shift) * 60;
  lua_pushinteger(L, static_cast<lua_Integer>(seconds));
  return 1;
}

int l_time_date(lua_State* L) {
  // **The offset that counts is the map's plus the hour of summer** (0.182.0): `time.tz()` keeps saying what the map says and
  // this is what makes the hour of a person, so a box ticked in the Time zone page moves the clock at once (docs/30, docs/60).
  const int32_t shift = (g_time_ops.offset_minutes != nullptr) ? g_time_ops.offset_minutes()
                                                               : ((g_time_ops.tz_minutes != nullptr) ? g_time_ops.tz_minutes() : 0);
  push_civil(L, moment_ms(L, 1) + static_cast<int64_t>(shift) * 60000);
  return 1;
}

// time.tz([offset]): the offset itself, in minutes —the number the rest of the world calls UTC offset— or the text
// `+01:00` when an app wants to show it. A call with no argument answers the one that is set; a call with one sets it
// and answers the new one. **A number of minutes and not a zone name**: this system has no table of the world's zones
// and does not pretend to (docs/30, docs/60).
int l_time_tz(lua_State* L) {
  if (g_time_ops.tz_minutes == nullptr || g_time_ops.set_tz_minutes == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this system keeps no hour of its own");
  }
  if (!lua_isnoneornil(L, 1)) {
    int32_t minutes = 0;
    if (lua_isnumber(L, 1)) {
      minutes = static_cast<int32_t>(lua_tointeger(L, 1));
    } else if (lua_isstring(L, 1) && g_time_ops.parse_tz != nullptr) {
      // **The text form the settings use**: `+01:00`, `-0330`, `60`. The parser is the kernel's, so an app and the
      // prompt accept exactly the same words and refuse exactly the same ones (docs/30, docs/60).
      int32_t parsed = 0;
      if (!g_time_ops.parse_tz(lua_tostring(L, 1), parsed)) {
        return push_nil_error(L, "E_INVAL", "that is not an offset: write +01:00, -03:30 or minutes");
      }
      minutes = parsed;
    } else {
      return push_nil_error(L, "E_INVAL", "the offset is minutes, or a text like +01:00");
    }
    if (!g_time_ops.set_tz_minutes(minutes)) {
      return push_nil_error(L, "E_INVAL", "that is not an offset this clock can have");
    }
  }
  lua_pushinteger(L, g_time_ops.tz_minutes());
  return 1;
}


// time.dst([on]): **l'hora d'estiu** (0.182.0). With no argument it answers the setting; with one it writes it and answers the
// new one, and a boolean or the two words of the prompt (`on`/`off`) are the same. It is a setting of the system and it is kept
// in the store, so the hour a person reads is the same after a reboot —the RTC counts seconds and knows nothing of summer— and
// `time.date()` moves the moment this is written (docs/30, docs/60).
int l_time_dst(lua_State* L) {
  if (g_time_ops.dst == nullptr || g_time_ops.set_dst == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this system keeps no hour of its own");
  }
  if (!lua_isnoneornil(L, 1)) {
    bool on = false;
    if (lua_isboolean(L, 1)) {
      on = lua_toboolean(L, 1) != 0;
    } else if (lua_isstring(L, 1)) {
      const char* word = lua_tostring(L, 1);
      if (word != nullptr && strcmp(word, "on") == 0) {
        on = true;
      } else if (word == nullptr || strcmp(word, "off") != 0) {
        return push_nil_error(L, "E_INVAL", "the hour of summer is on or off");
      }
    } else {
      return push_nil_error(L, "E_INVAL", "the hour of summer is a boolean, or on and off");
    }
    if (!g_time_ops.set_dst(on)) {
      return push_nil_error(L, "E_INVAL", "the store did not take the hour of summer");
    }
  }
  lua_pushboolean(L, g_time_ops.dst() ? 1 : 0);
  return 1;
}

// time.leap(): **si el febrer de l'any que és al rellotge té el vint-i-nou** (0.170.0, i des de la 0.192.57 només es
// pregunta). A leap year is a fact of the Gregorian calendar and not a setting of the board —l'amo: *«sabem com calcular si
// es any de taspàs o no ho es… no cal un toggle manual»*—, so the answer is counted from the date and nothing is written:
// the rule is `hal::is_leap_year`, one copy for the whole system, i el que una pàgina fa amb el número és comptar el febrer
// del mes que dibuixa (docs/30, docs/50, docs/60).
int l_time_leap(lua_State* L) {
  if (!lua_isnoneornil(L, 1)) {
    return push_nil_error(L, "E_INVAL", "the leap day is counted from the date and not set");
  }
  // **L'any que compta és el de l'hora de la persona**, el mateix que `time.date()` dona: the offset of the map and the
  // hour of summer are the two numbers that decide which day is now, and a leap year is asked about that day (docs/30).
  const int32_t shift = (g_time_ops.offset_minutes != nullptr) ? g_time_ops.offset_minutes()
                                                               : ((g_time_ops.tz_minutes != nullptr) ? g_time_ops.tz_minutes() : 0);
  int year = 1970, month = 1, day = 1, hour = 0, minute = 0, second = 0;
  hal::epoch_ms_to_civil(moment_ms(L, 1) + static_cast<int64_t>(shift) * 60000, year, month, day, hour, minute, second);
  lua_pushboolean(L, hal::is_leap_year(year) ? 1 : 0);
  return 1;
}

// time.week([first]): **which weekday a week begins on** (0.170.0), 1 for Monday —the week this system was born with—
// and 7 for Sunday, in the same count `wday` uses. A number or the word `monday`/`sunday`; with no argument it answers
// the setting. `ui.month` turns its seven columns with it, and the app of the time zone is the one that moves it
// (docs/30, docs/60).
int l_time_week(lua_State* L) {
  if (g_time_ops.week_start == nullptr || g_time_ops.set_week_start == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this system keeps no calendar of its own");
  }
  if (!lua_isnoneornil(L, 1)) {
    uint8_t first = 0;
    if (lua_isnumber(L, 1)) {
      const lua_Integer number = lua_tointeger(L, 1);
      if (number != 1 && number != 7) {
        return push_nil_error(L, "E_INVAL", "a week begins on Monday (1) or on Sunday (7)");
      }
      first = static_cast<uint8_t>(number);
    } else if (lua_isstring(L, 1)) {
      const char* word = lua_tostring(L, 1);
      if (strcmp(word, "monday") == 0) {
        first = 1;
      } else if (strcmp(word, "sunday") == 0) {
        first = 7;
      } else {
        return push_nil_error(L, "E_INVAL", "a week begins on monday or on sunday");
      }
    } else {
      return push_nil_error(L, "E_INVAL", "a week begins on Monday (1) or on Sunday (7)");
    }
    if (!g_time_ops.set_week_start(first)) {
      return push_nil_error(L, "E_INVAL", "the calendar did not take that");
    }
  }
  lua_pushinteger(L, g_time_ops.week_start());
  return 1;
}

// ---------------------------------------------------------------------------
// alarm: it belongs to the system, not to the machine that asked for it
// ---------------------------------------------------------------------------

// An alarm that goes off sooner than this is a busy loop with extra steps.
const int64_t kMinAlarmMs = 100;

int64_t alarm_mono_now() {
  hal::IClock* clock = wall_clock();
  return (clock != nullptr) ? clock->monotonic_ms() : now_ms();
}

// The app an alarm belongs to. Without one there is nobody to wake up.
const char* alarm_owner() { return g_app_id; }

// A payload is a string or a number until there is a JSON encoder (docs/30).
//
// **I tota alarma en porta un** (0.190.7, l'amo: *«tota alarma porta un payload que se li dona a l'app quan reb l'alarma»*):
// el payload és l'única cosa que l'app sabrà del que ha sonat quan el ganxo corri —una finestra i un `raise` obren la pàgina de
// zero i no hi ha cap estat propi que ho recordi—, o sigui que una alarma que no en porta no és una alarma sinó un despertar
// sense res a dir, i es refusa aquí amb una frase que diu què hi falta (docs/30, docs/60).
bool read_payload(lua_State* L, int idx, alarms::Spec& spec, char* store, size_t cap) {
  idx = lua_absindex(L, idx);
  if (lua_isnoneornil(L, idx)) {
    spec.payload_kind = alarms::Payload::kNone;
    spec.payload = nullptr;
    return true;
  }
  if (lua_type(L, idx) == LUA_TSTRING) {
    const char* text = lua_tostring(L, idx);
    if (strlen(text) >= cap) return false;
    memcpy(store, text, strlen(text) + 1);
    spec.payload_kind = alarms::Payload::kText;
    spec.payload = store;
    return true;
  }
  if (lua_type(L, idx) == LUA_TNUMBER) {
    const int written = snprintf(store, cap, "%.17g", static_cast<double>(lua_tonumber(L, idx)));
    if (written <= 0) return false;
    spec.payload_kind = alarms::Payload::kNumber;
    spec.payload = store;
    return true;
  }
  return false;
}

// **La cua de les alarmes, al registre cada cop que canvia** (0.189.149, l'amo: *«Vull que quan una app afegeixi, tregui o rebi
// una alarma surti al log»* i *«Vull que el sistema mostri la llista de alarmes que te i quina es la mes propera»*). One line
// says what happened and how many there are with the nearest first; then one line per alarm, which is `alarms::describe`
// and not a second reader of the struct. The library of the alarm never writes to a console —that is its first rule— so
// the words are here, where the app's door is, and in the kernel for what only the kernel sees (the fire and the arming).
void note_the_queue(const char* why) {
  const uint32_t count = alarms::count();
  if (count == 0) {
    hal::logf(pal::kLogInfo, "ALARM", "%s; the queue is empty", why);
    return;
  }
  const alarms::Entry* first = alarms::at(0);
  const int64_t now = alarm_mono_now();
  const int64_t left_ms = first->on_mono_ms - now;
  hal::logf(pal::kLogInfo, "ALARM", "%s; %u of %u, the nearest is #%u (%s) in %lld s", why,
            static_cast<unsigned>(count), static_cast<unsigned>(alarms::max()),
            static_cast<unsigned>(first->id), first->app,
            static_cast<long long>((left_ms + 999) / 1000));
  const int32_t offset = (g_time_ops.offset_minutes != nullptr) ? g_time_ops.offset_minutes() : 0;
  const int64_t epoch = epoch_ms_or_zero();
  for (uint32_t i = 0; i < count; ++i) {
    char line[160];
    if (alarms::describe(i, now, epoch, offset, line, sizeof(line)) == 0) continue;
    hal::logf(pal::kLogInfo, "ALARM", "  %s", line);
  }
}

// **El que la cua ja té amb aquesta configuració, si ho té** (0.189.172, l'amo: *«les alarmes de les apps es multipliquen,
// fes que el sistema pugui avisar a l'app de que ja te una alarma amb aquesta configuració. L'app podrà decidir si
// esborrar-la i crear una nova o no fer res»*). The call answers **two values**: the id of the alarm that is in the queue
// with this configuration, and `true` when there was already one and nothing was added. O sigui que una app que només llegeix
// el primer valor —totes les que hi ha avui— no arma la mateixa promesa dues vegades, i la que vol una hora nova diu
// `alarm.cancel(id)` i torna a preguntar (docs/30, docs/60).
//
// Returns how many values it pushed —0 when there is nothing to say— so the caller has one line to answer with.
int answer_the_same_alarm(lua_State* L, const alarms::Spec& spec) {
  const uint32_t same = alarms::find_same(spec);
  if (same == 0) return 0;
  hal::logf(pal::kLogInfo, "ALARM", "%s already has #%u with this configuration: nothing added", alarm_owner(),
            static_cast<unsigned>(same));
  lua_pushinteger(L, static_cast<lua_Integer>(same));
  lua_pushboolean(L, 1);
  return 2;
}

int l_alarm_at(lua_State* L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  if (alarm_owner()[0] == '\0') {
    return raise_coded(L, "E_NOTASK", "an alarm needs an app to belong to");
  }

  alarms::Spec spec;
  spec.app = alarm_owner();

  lua_getfield(L, 1, "on");
  const bool have_on = lua_isnumber(L, -1) != 0;
  const int64_t on_epoch_ms = have_on ? static_cast<int64_t>(lua_tonumber(L, -1) * 1000.0) : 0;
  lua_pop(L, 1);

  // The millisecond key is `in_ms` and not `in`, because `in` is a reserved word in Lua: a table
  // written `{in = 1000}` does not even parse, so the documented name could never be used. The
  // bracket form `{["in"] = 1000}` still works, and it is what the older text said (docs/30).
  lua_getfield(L, 1, "in_ms");
  if (!lua_isnumber(L, -1)) {
    lua_pop(L, 1);
    lua_getfield(L, 1, "in");
  }
  const bool have_in = lua_isnumber(L, -1) != 0;
  const int64_t in_ms = have_in ? static_cast<int64_t>(lua_tointeger(L, -1)) : 0;
  lua_pop(L, 1);

  const int64_t mono_now = alarm_mono_now();
  if (have_on) {
    const int64_t epoch_now = epoch_ms_or_zero();
    if (epoch_now <= 0) {
      return raise_coded(L, "E_INVAL", "there is no clock: an absolute alarm cannot be placed");
    }
    spec.on_mono_ms = mono_now + (on_epoch_ms - epoch_now);
  } else if (have_in) {
    if (in_ms < kMinAlarmMs) {
      return raise_coded(L, "E_INVAL", "an alarm goes off at least 100 ms from now");
    }
    spec.on_mono_ms = mono_now + in_ms;
  } else {
    return raise_coded(L, "E_INVAL", "an alarm needs `on` (epoch seconds) or `in_ms` (ms)");
  }

  lua_getfield(L, 1, "repeat");
  if (lua_type(L, -1) == LUA_TSTRING && strcmp(lua_tostring(L, -1), "daily") == 0) {
    spec.repeat_ms = static_cast<int32_t>(alarms::kDailyMs);
  } else if (lua_isnumber(L, -1)) {
    spec.repeat_ms = static_cast<int32_t>(lua_tointeger(L, -1) * 1000);
  }
  lua_pop(L, 1);

  lua_getfield(L, 1, "raise");
  spec.raise_to_front = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);

  // docs/30: persist defaults to true when there is a repeat to keep.
  lua_getfield(L, 1, "persist");
  spec.persist = lua_isnil(L, -1) ? (spec.repeat_ms > 0) : (lua_toboolean(L, -1) != 0);
  lua_pop(L, 1);

  char store[alarms::kPayloadBytes];
  lua_getfield(L, 1, "payload");
  const bool payload_ok = read_payload(L, -1, spec, store, sizeof(store));
  lua_pop(L, 1);
  if (!payload_ok) {
    return raise_coded(L, "E_INVAL", "the payload of an alarm is a string or a number");
  }
  if (spec.payload_kind == alarms::Payload::kNone) {
    return raise_coded(L, "E_INVAL", "an alarm carries a payload: it is what the app is told when it rings");
  }

  // **La promesa que ja hi és es diu i no es torna a armar**: the app decides what to do with the answer (docs/30).
  const int answered = answer_the_same_alarm(L, spec);
  if (answered > 0) return answered;

  const uint32_t id = alarms::add(spec);
  if (id == 0) return raise_coded(L, "E_NOMEM", "no room for another alarm");
  char why[64];
  snprintf(why, sizeof(why), "%s adds #%u", alarm_owner(), static_cast<unsigned>(id));
  note_the_queue(why);
  lua_pushinteger(L, static_cast<lua_Integer>(id));
  return 1;
}

// alarm.after(ms[, payload]): one shot, the shortcut for ``in``.
int l_alarm_after(lua_State* L) {
  const lua_Integer ms = luaL_checkinteger(L, 1);
  if (alarm_owner()[0] == '\0') {
    return raise_coded(L, "E_NOTASK", "an alarm needs an app to belong to");
  }
  if (ms < kMinAlarmMs) {
    return raise_coded(L, "E_INVAL", "an alarm goes off at least 100 ms from now");
  }

  alarms::Spec spec;
  spec.app = alarm_owner();
  spec.on_mono_ms = alarm_mono_now() + ms;
  char store[alarms::kPayloadBytes];
  if (!read_payload(L, 2, spec, store, sizeof(store))) {
    return raise_coded(L, "E_INVAL", "the payload of an alarm is a string or a number");
  }
  if (spec.payload_kind == alarms::Payload::kNone) {
    return raise_coded(L, "E_INVAL", "an alarm carries a payload: it is what the app is told when it rings");
  }

  // **La promesa que ja hi és es diu i no es torna a armar**: the app decides what to do with the answer (docs/30).
  const int answered = answer_the_same_alarm(L, spec);
  if (answered > 0) return answered;

  const uint32_t id = alarms::add(spec);
  if (id == 0) return raise_coded(L, "E_NOMEM", "no room for another alarm");
  char why[64];
  snprintf(why, sizeof(why), "%s adds #%u", alarm_owner(), static_cast<unsigned>(id));
  note_the_queue(why);
  lua_pushinteger(L, static_cast<lua_Integer>(id));
  return 1;
}

int l_alarm_cancel(lua_State* L) {
  const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
  const bool gone = alarms::cancel(alarm_owner(), id);
  // **Una baixa que no es pot fer tambe es diu** (0.189.149): an alarm of another app and an id that is not there are two
  // different mistakes and a `false` alone does not tell them apart in a log (docs/30, docs/60).
  if (!gone) {
    hal::logf(pal::kLogWarn, "ALARM", "%s cannot cancel #%u: it is not one of its alarms", alarm_owner(),
              static_cast<unsigned>(id));
  } else {
    char why[64];
    snprintf(why, sizeof(why), "%s cancels #%u", alarm_owner(), static_cast<unsigned>(id));
    note_the_queue(why);
  }
  lua_pushboolean(L, gone ? 1 : 0);
  return 1;
}

// alarm.list(): what is armed, in the order it will fire, with the times in
// epoch seconds like every other time the system gives an app.
int l_alarm_list(lua_State* L) {
  const int64_t mono_now = alarm_mono_now();
  const int64_t epoch_now = epoch_ms_or_zero();
  lua_createtable(L, static_cast<int>(alarms::count()), 0);
  for (uint32_t i = 0; i < alarms::count(); ++i) {
    const alarms::Entry* entry = alarms::at(i);
    if (entry == nullptr) continue;
    lua_createtable(L, 0, 6);
    lua_pushinteger(L, static_cast<lua_Integer>(entry->id));
    lua_setfield(L, -2, "id");
    lua_pushinteger(L, static_cast<lua_Integer>((entry->on_mono_ms - mono_now + epoch_now) / 1000));
    lua_setfield(L, -2, "on");
    lua_pushinteger(L, static_cast<lua_Integer>(entry->repeat_ms / 1000));
    lua_setfield(L, -2, "repeat");
    lua_pushboolean(L, entry->persist ? 1 : 0);
    lua_setfield(L, -2, "persist");
    lua_pushboolean(L, entry->raise_to_front ? 1 : 0);
    lua_setfield(L, -2, "raise");
    // **I de qui és** (0.189.157): the queue is of the system and an app may look at it, so the name of the app that set the
    // alarm travels with it — it is what `admin.uninstall` takes away and what a page of apps has to be able to count
    // (docs/30, docs/60).
    lua_pushstring(L, entry->app);
    lua_setfield(L, -2, "app");
    lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
  }
  return 1;
}

int l_alarm_caps(lua_State* L) {
  hal::IClock* clock = wall_clock();
  lua_createtable(L, 0, 3);
  lua_pushinteger(L, static_cast<lua_Integer>(alarms::max()));
  lua_setfield(L, -2, "max");
  lua_pushinteger(L, static_cast<lua_Integer>(kMinAlarmMs));
  lua_setfield(L, -2, "min_period_ms");
  lua_pushstring(L, (clock != nullptr && clock->can_wake()) ? "yes" : "no");
  lua_setfield(L, -2, "wake");
  // **I on ha quedat el despertar** (0.189.149): `"chip"` és el rellotge amb pila de la placa, `"timer"` és el comptador del
  // SoC i `"none"` és que no hi ha res armat —la mateixa resposta que el registre escriu quan arma una alarma, o sigui que
  // una app pot dir per què el seu avís s'ha de creure i una altra no (docs/30, docs/60).
  lua_pushstring(L, (clock != nullptr) ? clock->wake_keeper() : "none");
  lua_setfield(L, -2, "keeper");
  return 1;
}

// ---------------------------------------------------------------------------
// tick: la rellotgeria d'una app mentre la placa és desperta (0.192.60)
// ---------------------------------------------------------------------------

// **El mínim d'un tick** (0.192.60): a `tick` is the app's own clock and not a `sys.delay` loop —the system looks for it between
// two turns of the kernel, which is what lets a page take a sample while its own loop is idle—, o sigui que una crida cada segon
// només gastaria voltes per a res i faria que una app que no vol res escrigués al disc tot el dia. Five seconds is the shortest
// interval that still means something to a sensor of this house (docs/30, docs/60).
const int64_t kMinTickMs = 5000;

// **`tick.every(segons, payload[, opcions])`** (0.192.60, l'amo: *«M'agradaría que l'app Stats rebés una crida cada minut, però
// només si la placa està desperta. No vull que vaigi a l'RTC i desperti la placa. Una crida periódica, demanada per l'app i que el
// sistema cridi sempre que estigui despert (deixant-ho de fer si cau en sleeep, i tornant quan es desperta)»*). It goes into the
// same queue as the alarms —so an idle kernel looks at nothing new— with the three marks that make it a tick and not a promise:
// `awake_only` (it never arms the wake of the chip), not `persist` (nobody writes it down: the app asks again when it opens) and,
// by default, **no window**: a tick only happens while the app is already in front, unless the options ask for `raise`
// (l'amo: *«Però només si l'app demana primer plà. Per defecte no obre finestra.»*). Answers the id, and `true` as a second value
// when there was already a tick with this very configuration (docs/30, docs/60).
int l_tick_every(lua_State* L) {
  const lua_Number seconds = luaL_checknumber(L, 1);
  if (alarm_owner()[0] == '\0') {
    return raise_coded(L, "E_NOTASK", "a tick needs an app to belong to");
  }
  const int64_t period_ms = static_cast<int64_t>(seconds * 1000.0);
  if (period_ms < kMinTickMs) {
    return raise_coded(L, "E_INVAL", "a tick repeats at least every 5 s");
  }

  alarms::Spec spec;
  spec.app = alarm_owner();
  spec.repeat_ms = static_cast<int32_t>(period_ms);
  spec.on_mono_ms = alarm_mono_now() + period_ms;
  spec.awake_only = true;
  spec.persist = false;

  char store[alarms::kPayloadBytes];
  if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
    if (!read_payload(L, 2, spec, store, sizeof(store))) {
      return raise_coded(L, "E_INVAL", "the payload of a tick is a string or a number");
    }
  }
  // **I si vol el davant** (0.192.60): `raise = true` means the app wants to be brought to the glass when the tick fires and the
  // page in front is not it —the one case where a tick is allowed to open something— (docs/30, docs/60).
  if (lua_gettop(L) >= 3 && lua_istable(L, 3)) {
    lua_getfield(L, 3, "raise");
    spec.raise_to_front = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
  }

  // **El mateix tick que ja hi és no es toca** (0.192.60): the app asks again every time it opens, and a tick that started its
  // minute again at every page would never fire (docs/30).
  const uint32_t same = alarms::find_same(spec);
  if (same != 0) {
    hal::logf(pal::kLogInfo, "TICK", "%s already has #%u with this configuration: nothing added", alarm_owner(),
              static_cast<unsigned>(same));
    lua_pushinteger(L, static_cast<lua_Integer>(same));
    lua_pushboolean(L, 1);
    return 2;
  }
  // **I un tick per app, no una col·lecció** (0.192.63, l'amo: *«es repeteixen els ticks a la cua? Només un tick per app. Si una app
  // en demana unaltre, s'esborra l'anterior i es fa la que acaben de demanar»*): the queue has eight seats for the whole system and an
  // app that changes its rhythm is not asking for two clocks. The old one goes **before** the new one arrives, so a page that asks for
  // a shorter period does not lose a turn in between (docs/30, docs/60).
  const uint32_t replaced = alarms::remove_awake_only_of(alarm_owner());
  if (replaced > 0) {
    hal::logf(pal::kLogInfo, "TICK", "%s asks for another tick and its %u old one(s) go", alarm_owner(),
              static_cast<unsigned>(replaced));
  }

  const uint32_t id = alarms::add(spec);
  if (id == 0) return raise_coded(L, "E_NOMEM", "no room for another tick");
  // **I la línia diu que és un tick** (0.192.60): the queue is one and the log has to tell which of the two things it is looking
  // at, or every line of the Stats app would be read as an alarm (docs/30, docs/60).
  hal::logf(pal::kLogInfo, "TICK", "%s ticks every %lld s as #%u%s%s", alarm_owner(),
            static_cast<long long>(period_ms / 1000), static_cast<unsigned>(id),
            (spec.raise_to_front ? ", and it raises the app" : ", only while it is in front"),
            (spec.payload_kind != alarms::Payload::kNone) ? "" : " (no payload)");
  lua_pushinteger(L, static_cast<lua_Integer>(id));
  return 1;
}

int l_tick_cancel(lua_State* L) {
  const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
  // **Una app només pot treure's les seves** (0.192.60): `alarms::cancel` ja ho mira, i aquí la línia del registre ho diu amb el
  // nom del tick i no amb el d'una alarma (docs/30, docs/60).
  const bool gone = alarms::cancel(alarm_owner(), id);
  if (!gone) {
    hal::logf(pal::kLogWarn, "TICK", "%s cannot cancel #%u: it is not one of its ticks", alarm_owner(),
              static_cast<unsigned>(id));
  } else {
    hal::logf(pal::kLogInfo, "TICK", "%s cancels #%u", alarm_owner(), static_cast<unsigned>(id));
  }
  lua_pushboolean(L, gone ? 1 : 0);
  return 1;
}

// tick.list(): the ticks of this app, with the period —not the alarm queue, which is `alarm.list()`— (docs/30).
int l_tick_list(lua_State* L) {
  uint32_t mine = 0;
  for (uint32_t i = 0; i < alarms::count(); ++i) {
    const alarms::Entry* entry = alarms::at(i);
    if (entry != nullptr && entry->awake_only && strcmp(entry->app, alarm_owner()) == 0) ++mine;
  }
  lua_createtable(L, static_cast<int>(mine), 0);
  uint32_t used = 0;
  for (uint32_t i = 0; i < alarms::count(); ++i) {
    const alarms::Entry* entry = alarms::at(i);
    if (entry == nullptr || !entry->awake_only) continue;
    if (strcmp(entry->app, alarm_owner()) != 0) continue;
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, static_cast<lua_Integer>(entry->id));
    lua_setfield(L, -2, "id");
    lua_pushinteger(L, static_cast<lua_Integer>(entry->repeat_ms / 1000));
    lua_setfield(L, -2, "period");
    lua_pushboolean(L, entry->raise_to_front ? 1 : 0);
    lua_setfield(L, -2, "raise");
    if (entry->payload_kind == alarms::Payload::kText) {
      lua_pushstring(L, entry->payload);
    } else if (entry->payload_kind == alarms::Payload::kNumber) {
      lua_pushnumber(L, atof(entry->payload));
    } else {
      lua_pushnil(L);
    }
    lua_setfield(L, -2, "payload");
    lua_rawseti(L, -2, static_cast<lua_Integer>(++used));
  }
  return 1;
}

int l_tick_caps(lua_State* L) {
  lua_createtable(L, 0, 3);
  lua_pushinteger(L, static_cast<lua_Integer>(alarms::max()));
  lua_setfield(L, -2, "max");
  lua_pushinteger(L, static_cast<lua_Integer>(kMinTickMs));
  lua_setfield(L, -2, "min_period_ms");
  // **I que un tick no despertarà res, digui el que digui la placa** (0.192.60): it is the one promise the API makes, o sigui que
  // ha de viatjar amb les capacitats i no deixar-se llegir d'un `alarm.caps().wake` que parla d'una altra cosa (docs/30, docs/60).
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "wake");
  return 1;
}

// ---------------------------------------------------------------------------
// fs: reading and writing, with the app's own ground (docs/30 and docs/70)
// ---------------------------------------------------------------------------

// The window is what is read from the disk at a time, and it is the only internal size
// there is: a file of any size is copied into a Lua string window by window, so the app
// never has the whole file in a C buffer and never has to care how big it is. The one
// limit left is the interpreter's memory (docs/30, docs/70, docs/75).
const size_t kFsWindow = 512;

FileOps g_file_ops = {};  // every operation starts absent: install_fs_ops() fills them
AppsOps g_apps_ops = {};  // the door of the apps and the wish to start one, filled by the kernel (0.153.0)
SessionOps g_session_ops = {};  // the memory of a session, per app and per key, filled by the kernel (0.179.0)
// **The settings of the system**, which an app reads and never writes (0.189.13): a page that has to obey a switch —the
// launcher and `sort apps by usage`— asks here, and the prompt's `cfg` is the only door that changes one (docs/30).
SettingsOps g_settings_ops = {};

// **Els ajustos que es poden escriure** (0.189.106): les portes de `cfg`, que és de què està fet el panell de Settings.
CfgOps g_cfg_ops = {};
// **Les portes del teclat de Bluetooth** (0.190.180): available, connected and type, i res més (docs/30, docs/50).
BtOps g_bt_ops = {};
// **A session of the shell for an app** (0.159.0), filled by the kernel: the shell is the system's, and this library
// never includes the kernel (docs/20, docs/30).
ShellOps g_shell_ops = {};
AppOps g_app_ops = {};
NetOps g_net_ops = {};    // same rule: without them net.* answers E_UNSUPPORTED
PowerOps g_power_ops = {};  // and power.* too
LedOps g_led_ops = {};      // and led.*
SerialOps g_serial_ops = {};  // and serial.*
CryptoOps g_crypto_ops = {};  // and crypto.* — the arithmetic of a secret, filled by the kernel (0.190.258)
ImuOps g_imu_ops = {};      // and imu.*
EnvOps g_env_ops = {};      // and env.*
LoraOps g_lora_ops = {};    // and lora.* — the radio that only listens (0.192.49)
GpsOps g_gps_ops = {};      // and gps.* — the receiver of the sky (0.192.63)
ButtonOps g_button_ops = {};  // and button.*
TimeOps g_time_ops = {};      // and time.tz() / time.local()

int push_nil_error(lua_State* L, const char* code, const char* message) {
  lua_pushnil(L);
  push_error_table(L, code, message);
  return 2;
}

// ---- the memory of a session (0.179.0, docs/30, docs/75) ----
//
// **A cache the kernel keeps for an app**: `sys.session('apps', 32768)` answers a block of that many bytes and the same
// call in the next run of the same app answers the same block, so the second page of a launcher costs no disk. The key is
// the app's and not the system's —the kernel asks who is running and the table is a row per (app, key)—, the block is
// **zeroed** the first time, which is how an app tells "nothing cached" from "this is mine", and it lives until the board
// goes off or the memory runs short (docs/30, docs/75).
const char kSessionMeta[] = "lunokOS.Session";

struct LuaSession {
  // **Ja no guarda cap punter** (0.190.174): a block of 32 KB or more can live in the memory behind the chip's bank switcher,
  // where the only thing that exists is a window that is mapped while a piece is read; the object keeps the key and asks the
  // kernel for the bytes with an offset. `held` is whether the kernel still keeps this block for this app (docs/30, docs/75).
  bool held;
  size_t size;
  // **The table it came from** (0.179.0): the relief of the PAL gives every session memory back when a block does not fit,
  // so a number that only goes up is what tells an object of the app that the block under it is not there any more —the
  // same idea as the generation of a canvas (docs/75, docs/90)—.
  uint32_t generation;
  // **And the key it was asked under** (0.179.3): `block:release()` is the app giving its memory back before it ends, and
  // the key is what the table is asked with (docs/30, docs/75).
  char key[16];
};

LuaSession* session_check(lua_State* L, int idx) {
  LuaSession* block = static_cast<LuaSession*>(luaL_checkudata(L, idx, kSessionMeta));
  if (!block->held || g_session_ops.generation == nullptr || g_session_ops.generation() != block->generation) {
    luaL_error(L, "the session memory was given back");
  }
  return block;
}

// **`block:alive()`** (0.179.8): whether this object still holds the block the kernel gave it. The relief of the allocator
// gives every session memory back the moment a block does not fit —that is what a session memory is for— and the app that
// keeps a cache there can lose it in the middle of a page: this is the question that is asked **before** the block is used,
// and the app asks for another one when the answer is no. Every other call of a block that went raises, because reading a
// block the relief has given away is reading somebody else's memory, which is worse than a page without a cache
// (docs/30, docs/75).
int l_session_alive(lua_State* L) {
  LuaSession* block = static_cast<LuaSession*>(luaL_checkudata(L, 1, kSessionMeta));
  const bool mine = block->held && g_session_ops.generation != nullptr &&
                    g_session_ops.generation() == block->generation;
  lua_pushboolean(L, mine ? 1 : 0);
  return 1;
}

int l_session_read(lua_State* L) {
  LuaSession* block = session_check(L, 1);
  const lua_Integer at = luaL_checkinteger(L, 2);
  lua_Integer want = luaL_optinteger(L, 3, static_cast<lua_Integer>(block->size));
  if (at < 0 || at > static_cast<lua_Integer>(block->size)) {
    return push_nil_error(L, "E_INVAL", "that offset is not in the block");
  }
  if (want < 0) want = 0;
  if (at + want > static_cast<lua_Integer>(block->size)) want = static_cast<lua_Integer>(block->size) - at;
  if (g_session_ops.read == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this system keeps no session memory");
  }
  if (want == 0) {
    lua_pushliteral(L, "");
    return 1;
  }
  // **The string is built where it is read** (0.190.174): a block of the bank is copied piece by piece into the buffer of
  // the interpreter, so a cache of two megabytes costs one call and no temporary of ours (docs/30, docs/75).
  luaL_Buffer text;
  char* room = luaL_buffinitsize(L, &text, static_cast<size_t>(want));
  const uint32_t got = g_session_ops.read(block->key, static_cast<uint32_t>(at), room, static_cast<uint32_t>(want));
  luaL_pushresultsize(&text, static_cast<size_t>(got));
  return 1;
}

int l_session_write(lua_State* L) {
  LuaSession* block = session_check(L, 1);
  const lua_Integer at = luaL_checkinteger(L, 2);
  size_t length = 0;
  const char* text = luaL_checklstring(L, 3, &length);
  if (at < 0 || at > static_cast<lua_Integer>(block->size)) {
    return push_nil_error(L, "E_INVAL", "that offset is not in the block");
  }
  // **What does not fit is not written and is not an error**: the answer is the bytes that went in, so an app that fills a
  // cache knows where it stopped instead of finding half a record (docs/30).
  size_t room = block->size - static_cast<size_t>(at);
  if (length > room) length = room;
  if (g_session_ops.write == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this system keeps no session memory");
  }
  const uint32_t went =
      (length > 0) ? g_session_ops.write(block->key, static_cast<uint32_t>(at), text, static_cast<uint32_t>(length)) : 0;
  lua_pushinteger(L, static_cast<lua_Integer>(went));
  return 1;
}

int l_session_clear(lua_State* L) {
  LuaSession* block = session_check(L, 1);
  if (g_session_ops.clear == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this system keeps no session memory");
  }
  lua_pushboolean(L, g_session_ops.clear(block->key) ? 1 : 0);
  return 1;
}

int l_session_size(lua_State* L) {
  LuaSession* block = session_check(L, 1);
  lua_pushinteger(L, static_cast<lua_Integer>(block->size));
  return 1;
}

// **`block:release()`** (0.179.3): the app gives its memory back, which is the one release that is not the allocator's —an
// app that has finished with a cache and wants the block for something else says so here, and the log line is the app's
// and not the system's (docs/30, docs/75).
int l_session_release(lua_State* L) {
  LuaSession* block = session_check(L, 1);
  if (g_session_ops.release == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this system keeps no session memory");
  }
  const bool gone = g_session_ops.release(block->key);
  block->held = false;
  block->size = 0;
  if (!gone) return push_nil_error(L, "E_INVAL", "that block was not the app's");
  lua_pushboolean(L, 1);
  return 1;
}

int l_session_gc(lua_State* L) {
  // **The block is the kernel's and it outlives the interpreter**: giving it back here would throw away the cache of the
  // app that is about to be started again, which is the whole point of a session memory (docs/30).
  LuaSession* block = static_cast<LuaSession*>(luaL_checkudata(L, 1, kSessionMeta));
  block->held = false;
  block->size = 0;
  return 0;
}

int l_session_tostring(lua_State* L) {
  LuaSession* block = static_cast<LuaSession*>(luaL_checkudata(L, 1, kSessionMeta));
  if (!block->held) {
    lua_pushstring(L, "session (given back)");
  } else {
    char text[48];
    snprintf(text, sizeof(text), "session (%u B)", static_cast<unsigned>(block->size));
    lua_pushstring(L, text);
  }
  return 1;
}

void open_session_library(lua_State* L) {
  luaL_newmetatable(L, kSessionMeta);
  lua_createtable(L, 0, 7);
  set_function(L, "read", l_session_read);
  set_function(L, "write", l_session_write);
  set_function(L, "clear", l_session_clear);
  set_function(L, "size", l_session_size);
  set_function(L, "alive", l_session_alive);
  set_function(L, "release", l_session_release);
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, &l_session_gc);
  lua_setfield(L, -2, "__gc");
  lua_pushcfunction(L, &l_session_tostring);
  lua_setfield(L, -2, "__tostring");
  lua_pop(L, 1);
}

// **`sys.session(key, bytes)`**: the block of this app under that key, made the first time and found after that
// (0.179.0). It answers the block and nothing else —`block:size()` is what it really has—, and `nil, error` when there is
// no room, which an app answers by going on without its cache (docs/30, docs/75).
int l_sys_session(lua_State* L) {
  const char* key = luaL_checkstring(L, 1);
  const lua_Integer wanted = luaL_checkinteger(L, 2);
  if (wanted <= 0 || wanted > 1024 * 1024) {
    return push_nil_error(L, "E_INVAL", "a session block is from one byte to a megabyte");
  }
  if (g_session_ops.acquire == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this system keeps no session memory");
  }
  size_t got = 0;
  if (!g_session_ops.acquire(key, static_cast<size_t>(wanted), got)) {
    return push_nil_error(L, "E_NOMEM", "there is no room for that block");
  }
  LuaSession* block = static_cast<LuaSession*>(lua_newuserdatauv(L, sizeof(LuaSession), 0));
  block->held = true;
  block->size = got;
  block->generation = g_session_ops.generation();
  snprintf(block->key, sizeof(block->key), "%s", key);
  luaL_getmetatable(L, kSessionMeta);
  lua_setmetatable(L, -2);
  return 1;
}

// **`sys.session_destroy(key)`** (0.189.16): una app llença una sessió seva —la memòria i el fitxer—, i és l'única porta
// que ho fa de debò: `release()` dóna el bloc i el desa al disc, i `clear()` posa els bytes a zero i la propera arrencada
// el torna a llegir del fitxer. The answer is the three numbers of what went, so a page a person pressed can say
// "32768 B forgotten" instead of guessing; an object of the app that still holds the block answers `alive() == false` from
// then on, because the kernel counts that memory as given back (docs/30, docs/75).
int l_sys_session_destroy(lua_State* L) {
  const char* key = luaL_checkstring(L, 1);
  if (g_session_ops.destroy == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this system keeps no session memory");
  }
  uint32_t blocks = 0;
  uint32_t files = 0;
  size_t bytes = 0;
  if (!g_session_ops.destroy(key, blocks, files, bytes)) {
    return push_nil_error(L, "E_UNAVAIL", "that session cannot be destroyed now");
  }
  lua_createtable(L, 0, 3);
  lua_pushinteger(L, static_cast<lua_Integer>(blocks));
  lua_setfield(L, -2, "blocks");
  lua_pushinteger(L, static_cast<lua_Integer>(files));
  lua_setfield(L, -2, "files");
  lua_pushinteger(L, static_cast<lua_Integer>(bytes));
  lua_setfield(L, -2, "bytes");
  return 1;
}


const char* fs_error_code(FsError err) {
  switch (err) {
    case FsError::kNoEnt:
      return "E_NOENT";
    case FsError::kInval:
      return "E_INVAL";
    case FsError::kNoSpc:
      return "E_NOSPC";
    case FsError::kTooBig:
      return "E_TOOBIG";
    case FsError::kNoHandles:
      return "E_NO_HANDLES";
    case FsError::kIo:
      return "E_IO";
    case FsError::kUnsupported:
      return "E_UNSUPPORTED";
    case FsError::kUnavail:
      return "E_UNAVAIL";
    default:
      return "E_IO";
  }
}

const char* fs_error_message(FsError err) {
  switch (err) {
    case FsError::kNoEnt:
      return "no such file or directory";
    case FsError::kInval:
      return "that path is not one this app may use";
    case FsError::kNoSpc:
      return "the room this app has is not enough";
    case FsError::kTooBig:
      return "a path or a name that does not fit";
    case FsError::kNoHandles:
      return "every file this system can have open at once is taken";
    case FsError::kIo:
      return "the file system failed";
    case FsError::kUnsupported:
      return "the file system does not do that";
    case FsError::kUnavail:
      return "that is not on this board right now";
    default:
      return "the file system failed";
  }
}

int push_fs_error(lua_State* L, FsError err) {
  return push_nil_error(L, fs_error_code(err), fs_error_message(err));
}

const char* node_type_name(int type) {
  return (type == static_cast<int>(hal::NodeType::kDir)) ? "dir" : "file";
}

int l_fs_read_at(lua_State* L) {
  const char* path = luaL_checkstring(L, 1);
  const lua_Integer offset = luaL_checkinteger(L, 2);
  const lua_Integer len = luaL_checkinteger(L, 3);
  if (offset < 0 || len < 0) return push_nil_error(L, "E_INVAL", "offset and length are not negative");
  if (g_file_ops.read_at == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "there is no file system");
  }

  char window[kFsWindow];
  luaL_Buffer buffer;
  luaL_buffinit(L, &buffer);
  int64_t done = 0;
  FsError err = FsError::kOk;
  while (done < len) {
    const size_t want = (static_cast<int64_t>(kFsWindow) < (len - done))
                            ? kFsWindow
                            : static_cast<size_t>(len - done);
    const int32_t got =
        g_file_ops.read_at(path, offset + done, window, static_cast<uint32_t>(want), err);
    if (got < 0) {
      luaL_pushresult(&buffer);
      lua_pop(L, 1);
      return push_fs_error(L, err);
    }
    if (got == 0) break;  // the end of the file
    luaL_addlstring(&buffer, window, static_cast<size_t>(got));
    done += got;
  }
  luaL_pushresult(&buffer);
  return 1;
}

int l_fs_read(lua_State* L) {
  const char* path = luaL_checkstring(L, 1);
  // The second argument is how much the caller wants, and with none the whole file is
  // wanted. **There is no ceiling of ours**: asking for a prefix of a bigger file is fine
  // and a file that does not fit in the memory the app has is the app's to ask around
  // (docs/30).
  const lua_Integer max = luaL_optinteger(L, 2, -1);
  if (max < -1) return push_nil_error(L, "E_INVAL", "a read is not negative");
  if (g_file_ops.read_at == nullptr || g_file_ops.size == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "there is no file system");
  }
  FsError err = FsError::kOk;
  int64_t size = g_file_ops.size(path, err);
  if (size < 0) return push_fs_error(L, err);
  if (max >= 0 && size > max) size = max;  // a prefix of a file that goes on

  char window[kFsWindow];
  luaL_Buffer buffer;
  luaL_buffinit(L, &buffer);
  int64_t done = 0;
  while (done < size) {
    const size_t want = (static_cast<int64_t>(kFsWindow) < (size - done))
                            ? kFsWindow
                            : static_cast<size_t>(size - done);
    const int32_t got = g_file_ops.read_at(path, done, window, static_cast<uint32_t>(want), err);
    if (got <= 0) break;
    luaL_addlstring(&buffer, window, static_cast<size_t>(got));
    done += got;
  }
  luaL_pushresult(&buffer);
  return 1;
}

int l_fs_stat(lua_State* L) {
  const char* path = luaL_checkstring(L, 1);
  if (g_file_ops.size == nullptr || g_file_ops.is_dir == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "there is no file system");
  }
  FsError err = FsError::kOk;
  const bool dir = g_file_ops.is_dir(path, err);
  if (!dir && err != FsError::kOk) return push_fs_error(L, err);
  int64_t size = 0;
  if (!dir) {
    size = g_file_ops.size(path, err);
    if (size < 0) return push_fs_error(L, err);
  }

  lua_createtable(L, 0, 3);
  lua_pushstring(L, dir ? "dir" : "file");
  lua_setfield(L, -2, "type");
  lua_pushinteger(L, static_cast<lua_Integer>(size));
  lua_setfield(L, -2, "size");
  lua_pushinteger(L, 0);  // mtime: no backend gives one yet
  lua_setfield(L, -2, "mtime");
  return 1;
}

class TableVisitor : public DirVisitor {
 public:
  explicit TableVisitor(lua_State* L) : L_(L) {}

  void visit(const char* name, int type, uint32_t size, int64_t mtime) override {
    lua_createtable(L_, 0, 4);
    lua_pushstring(L_, name);
    lua_setfield(L_, -2, "name");
    lua_pushstring(L_, node_type_name(type));
    lua_setfield(L_, -2, "type");
    lua_pushinteger(L_, static_cast<lua_Integer>(size));
    lua_setfield(L_, -2, "size");
    lua_pushinteger(L_, static_cast<lua_Integer>(mtime));
    lua_setfield(L_, -2, "mtime");
    lua_rawseti(L_, table_, ++count_);
  }

  void start() {
    lua_createtable(L_, 0, 8);
    table_ = lua_gettop(L_);
  }

  int finish() {
    lua_pushvalue(L_, table_);
    return 1;
  }

 private:
  lua_State* L_;
  int table_ = 0;
  int count_ = 0;
};

int l_fs_list(lua_State* L) {
  const char* path = luaL_checkstring(L, 1);
  if (g_file_ops.list == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "there is no file system");
  }
  TableVisitor visitor(L);
  visitor.start();
  FsError err = FsError::kOk;
  if (!g_file_ops.list(path, visitor, err)) {
    lua_settop(L, 0);
    return push_fs_error(L, err);
  }
  return visitor.finish();
}

// fs.write(path, data): a whole file, atomically. Either the new contents are
// there on return or the old ones never stopped being there (docs/70).
int l_fs_write(lua_State* L) {
  const char* path = luaL_checkstring(L, 1);
  size_t len = 0;
  const char* data = luaL_checklstring(L, 2, &len);
  if (g_file_ops.write_atomic == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "there is no file system");
  }
  FsError err = FsError::kOk;
  const int64_t written = g_file_ops.write_atomic(path, data, len, err);
  if (written < 0) return push_fs_error(L, err);
  lua_pushinteger(L, static_cast<lua_Integer>(written));
  return 1;
}

// fs.mkdir(path): **a folder and the fathers it needs**, one call. The chain is the kernel's and not this file's
// (`fs_ops::mkdir` walks the path inside the storage with `make_dirs`), and it has been that way since the import, so an
// app that writes in its own ground the first time does not have to ask for three folders in order (docs/30, docs/70).
// **A chain here in Lua was tried on 9/11 and taken away on 10/11** (0.173.1): it asked for every prefix of the path
// through the door, which is the same walk a second time, and the case of the time clock that was said to need it works
// without it —measured on the bench with the code of 0.171.2 on a flash with no `data/`: an app created `/data/<id>`
// and wrote in it— (docs/30).
int l_fs_mkdir(lua_State* L) {
  const char* path = luaL_checkstring(L, 1);
  if (g_file_ops.mkdir == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "there is no file system");
  }
  FsError err = FsError::kOk;
  if (!g_file_ops.mkdir(path, err)) return push_fs_error(L, err);
  lua_pushboolean(L, 1);
  return 1;
}

// fs.format(path): **the volume the path lives on, emptied** (0.171.0). It is the call the page of the eraser is built
// on: `/nvs` erases every key of the store and `/sd` takes every file and folder of the card. The answer is how many
// things went, and a volume this system cannot empty answers `E_UNSUPPORTED` — a card that is not there is `E_UNAVAIL`
// and a path that is not a mount is `E_INVAL` (docs/30, docs/70).
int l_fs_format(lua_State* L) {
  const char* path = luaL_checkstring(L, 1);
  if (g_file_ops.format == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this system cannot empty a volume");
  }
  uint32_t removed = 0;
  FsError err = FsError::kOk;
  if (!g_file_ops.format(path, removed, err)) return push_fs_error(L, err);
  lua_createtable(L, 0, 1);
  lua_pushinteger(L, static_cast<lua_Integer>(removed));
  lua_setfield(L, -2, "erased");
  return 1;
}

int l_fs_remove(lua_State* L) {
  const char* path = luaL_checkstring(L, 1);
  if (g_file_ops.remove == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "there is no file system");
  }
  FsError err = FsError::kOk;
  if (!g_file_ops.remove(path, err)) return push_fs_error(L, err);
  lua_pushboolean(L, 1);
  return 1;
}

int l_fs_rename(lua_State* L) {
  const char* from = luaL_checkstring(L, 1);
  const char* to = luaL_checkstring(L, 2);
  if (g_file_ops.rename == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "there is no file system");
  }
  FsError err = FsError::kOk;
  if (!g_file_ops.rename(from, to, err)) return push_fs_error(L, err);
  lua_pushboolean(L, 1);
  return 1;
}

// fs.mounts(): what is mounted, and nothing about the room. The room costs a walk
// of the file system, and an app that only wants the list should not pay it
// (docs/70).
// The VFS keeps twelve rows (`vfs.cpp`, kMaxMounts) and this is the same number: an app on the Paper
// with a card has ten mounts (`/littlefs`, `/apps`, `/res`, `/data`, `/fonts`, `/sd`, `/rtc`, `/fram`,
// `/nvs` and `/cfg`), and a list that stopped at eight would hide the last two without saying so.
const uint32_t kMaxMountsInLua = 12;

int l_fs_mounts(lua_State* L) {
  if (g_file_ops.mounts == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "there is no file system");
  }
  MountInfo list[kMaxMountsInLua];
  const uint32_t count = g_file_ops.mounts(list, kMaxMountsInLua);
  lua_createtable(L, static_cast<int>(count), 0);
  for (uint32_t i = 0; i < count; ++i) {
    lua_createtable(L, 0, 3);
    lua_pushstring(L, list[i].path);
    lua_setfield(L, -2, "path");
    lua_pushstring(L, list[i].kind);
    lua_setfield(L, -2, "type");
    lua_pushboolean(L, list[i].writable ? 1 : 0);
    lua_setfield(L, -2, "writable");
    lua_rawseti(L, -2, static_cast<lua_Integer>(i) + 1);
  }
  return 1;
}

// fs.space([path]): total and free bytes on the mount the path lives on, or nil
// and E_UNSUPPORTED when the backend cannot say. It is a question of its own
// because it is the expensive one (docs/70).
int l_fs_space(lua_State* L) {
  const char* path = luaL_optstring(L, 1, "/");
  if (g_file_ops.space == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "there is no file system");
  }
  FsError err = FsError::kOk;
  uint64_t total = 0;
  uint64_t free_bytes = 0;
  if (!g_file_ops.space(path, total, free_bytes, err)) {
    if (err == FsError::kOk) err = FsError::kUnsupported;
    return push_fs_error(L, err);
  }
  lua_createtable(L, 0, 2);
  lua_pushinteger(L, static_cast<lua_Integer>(total));
  lua_setfield(L, -2, "total");
  lua_pushinteger(L, static_cast<lua_Integer>(free_bytes));
  lua_setfield(L, -2, "free");
  return 1;
}

// **`fs.thumbnail(path[, max])`: la cara d'un fitxer, feta pel sistema** (0.192.38, l'amo: *«no han de ser les apps qui facin els
// thumbnails, sinó el sistema. Que revisi si hi ha thumbnail i si no hi ha, el genera. Les apps no tenen que fer res»*). Answers
// the path of the cache beside the picture —`.<name>.cache.ics`— and writes it if it is not there, or the path of the picture
// itself when it is already as small as a face. **L'app no escala, no codifica i no escriu res**: diu quin fitxer dibuixa i rep
// una foto que sap pintar, que és el que fa que la icona d'un fitxer sigui el fitxer i no un dibuix del seu tipus (docs/30,
// docs/46). `max` is the longest side of the cache, 64 if it is not said (docs/46, C11).
int l_fs_thumbnail(lua_State* L) {
  const char* path = luaL_checkstring(L, 1);
  const lua_Integer max = luaL_optinteger(L, 2, 0);
  if (max < 0 || max > 4096) {
    return push_nil_error(L, "E_INVAL", "a face is up to 4096 pixels a side");
  }
  if (g_file_ops.thumbnail == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this system cannot make a face of a picture");
  }
  char out[200];
  const char* why = nullptr;
  if (!g_file_ops.thumbnail(path, static_cast<int32_t>(max), out, sizeof(out), why)) {
    return push_nil_error(L, "E_INVAL", (why != nullptr) ? why : "there is no face for that file");
  }
  lua_pushstring(L, out);
  return 1;
}

// fs.cfg([unit]): where this app's settings go, and how to move them.
//
// With no argument it answers a table {unit=..., dir=...}: the unit behind /cfg ("sd" or
// "littlefs") and the real directory behind it, which is the one a person can go and look
// at. With an argument —"auto", "sd" or "littlefs"— it asks for that unit, saves the
// choice, and answers the same table once it is in force. Asking for a card that is not
// there answers nil and E_UNAVAIL instead of moving the settings in silence (docs/70).
int l_fs_cfg(lua_State* L) {
  if (g_file_ops.cfg_unit == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "there is no file system");
  }
  if (!lua_isnoneornil(L, 1)) {
    const char* want = luaL_checkstring(L, 1);
    FsError err = FsError::kOk;
    if (!g_file_ops.cfg_set_unit(want, err)) {
      if (err == FsError::kOk) err = FsError::kUnavail;
      return push_fs_error(L, err);
    }
  }
  lua_createtable(L, 0, 2);
  const char* unit = g_file_ops.cfg_unit();
  lua_pushstring(L, (unit != nullptr) ? unit : "");
  lua_setfield(L, -2, "unit");
  char dir[96];
  FsError err = FsError::kOk;
  if (g_file_ops.cfg_dir != nullptr && g_file_ops.cfg_dir(dir, sizeof(dir), err) > 0) {
    lua_pushstring(L, dir);
  } else {
    lua_pushstring(L, "");
  }
  lua_setfield(L, -2, "dir");
  return 1;
}

// fs.data([sub]): where this app is allowed to write. It is the friendly face of
// the rule that an app only writes inside its own ground (docs/70).
int l_fs_data(lua_State* L) {
  if (g_app_id[0] == '\0') return raise_coded(L, "E_NOTASK", "no app, so no ground of its own");
  const char* sub = luaL_optstring(L, 1, nullptr);

  char path[128];
  if (sub == nullptr || sub[0] == '\0') {
    snprintf(path, sizeof(path), "/data/%s", g_app_id);
  } else {
    snprintf(path, sizeof(path), "/data/%s/%s", g_app_id, sub);
  }
  lua_pushstring(L, path);
  return 1;
}

// --- the file object ------------------------------------------------------
//
// An open file is a userdata with the handle the kernel gave it. The pool is the
// kernel's: the ninth open says E_NO_HANDLES (docs/70).

const char kFileMeta[] = "lunokOS.File";

struct LuaFile {
  int32_t handle;  // 0 when it is closed
};

LuaFile* file_check(lua_State* L, int idx) {
  LuaFile* file = static_cast<LuaFile*>(luaL_checkudata(L, idx, kFileMeta));
  if (file->handle == 0) luaL_error(L, "the file is closed");
  return file;
}

int push_file_error(lua_State* L, FsError err) {
  return push_nil_error(L, fs_error_code(err), fs_error_message(err));
}

int l_file_read(lua_State* L) {
  LuaFile* file = file_check(L, 1);
  if (g_file_ops.file_read == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "there is no file system");
  }
  FsError err = FsError::kOk;
  char window[kFsWindow];

  if (lua_type(L, 2) == LUA_TSTRING) {
    const char* what = lua_tostring(L, 2);
    if (strcmp(what, "*a") == 0) {
      // To the end of the file, whatever that is: the string grows in the interpreter's
      // heap as the windows arrive (docs/70).
      luaL_Buffer buffer;
      luaL_buffinit(L, &buffer);
      for (;;) {
        const int32_t got = g_file_ops.file_read(file->handle, window, sizeof(window), err);
        if (got < 0) {
          luaL_pushresult(&buffer);
          lua_pop(L, 1);
          return push_file_error(L, err);
        }
        if (got == 0) break;
        luaL_addlstring(&buffer, window, static_cast<size_t>(got));
      }
      luaL_pushresult(&buffer);
      return 1;
    }
    if (strcmp(what, "*l") == 0) {
      // A line ends at the newline, so the window that brings it may bring the start of
      // the next line too: what is left over goes **back** into the file and the next read
      // starts there. Reading it a byte at a time was exact and cost one call to the disk
      // per byte, and cutting it at 256 was worse: a long line came out short and the app
      // could not tell (measured with a 1 000 byte line, 31/10).
      luaL_Buffer buffer;
      luaL_buffinit(L, &buffer);
      bool anything = false;
      bool ended = false;
      while (!ended) {
        const int32_t got = g_file_ops.file_read(file->handle, window, sizeof(window), err);
        if (got < 0) {
          luaL_pushresult(&buffer);
          lua_pop(L, 1);
          return push_file_error(L, err);
        }
        if (got == 0) break;
        for (int32_t i = 0; i < got; ++i) {
          anything = true;
          if (window[i] == '\n') {
            const int32_t back = got - i - 1;
            if (back > 0) {
              const int64_t here = g_file_ops.file_position(file->handle, err);
              if (here < 0 || !g_file_ops.file_seek(file->handle, here - back, err)) {
                luaL_pushresult(&buffer);
                lua_pop(L, 1);
                return push_file_error(L, err);
              }
            }
            ended = true;
            break;
          }
          if (window[i] != '\r') luaL_addchar(&buffer, window[i]);
        }
      }
      if (!anything) {
        lua_pushnil(L);  // the end of the file
        return 1;
      }
      luaL_pushresult(&buffer);
      return 1;
    }
    return push_nil_error(L, "E_INVAL", "read takes a number, '*a' or '*l'");
  }

  lua_Integer wanted = luaL_optinteger(L, 2, 1);
  if (wanted < 0) return push_nil_error(L, "E_INVAL", "a read is not negative");

  luaL_Buffer buffer;
  luaL_buffinit(L, &buffer);
  int64_t done = 0;
  while (done < wanted) {
    const size_t want = (static_cast<int64_t>(kFsWindow) < (wanted - done))
                            ? kFsWindow
                            : static_cast<size_t>(wanted - done);
    const int32_t got = g_file_ops.file_read(file->handle, window, static_cast<uint32_t>(want), err);
    if (got < 0) {
      luaL_pushresult(&buffer);
      lua_pop(L, 1);
      return push_file_error(L, err);
    }
    if (got == 0) break;
    luaL_addlstring(&buffer, window, static_cast<size_t>(got));
    done += got;
  }
  if (done == 0) {
    lua_pushnil(L);  // the end of the file
    return 1;
  }
  luaL_pushresult(&buffer);
  return 1;
}

int l_file_write(lua_State* L) {
  LuaFile* file = file_check(L, 1);
  size_t len = 0;
  const char* data = luaL_checklstring(L, 2, &len);
  if (g_file_ops.file_write == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "there is no file system");
  }
  FsError err = FsError::kOk;
  const int64_t put = g_file_ops.file_write(file->handle, data, len, err);
  if (put < 0) return push_file_error(L, err);
  lua_pushinteger(L, static_cast<lua_Integer>(put));
  return 1;
}

int l_file_seek(lua_State* L) {
  LuaFile* file = file_check(L, 1);
  if (g_file_ops.file_seek == nullptr || g_file_ops.file_position == nullptr ||
      g_file_ops.file_size == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "there is no file system");
  }
  FsError err = FsError::kOk;

  int64_t base = 0;
  if (lua_type(L, 2) == LUA_TSTRING) {
    const char* whence = lua_tostring(L, 2);
    if (strcmp(whence, "set") == 0) {
      base = 0;
    } else if (strcmp(whence, "cur") == 0) {
      base = g_file_ops.file_position(file->handle, err);
    } else if (strcmp(whence, "end") == 0) {
      base = g_file_ops.file_size(file->handle, err);
    } else {
      return push_nil_error(L, "E_INVAL", "seek takes 'set', 'cur' or 'end'");
    }
  } else {
    base = g_file_ops.file_position(file->handle, err);
  }
  if (base < 0) return push_file_error(L, err);

  const lua_Integer offset = luaL_optinteger(L, 3, 0);
  if (!g_file_ops.file_seek(file->handle, base + offset, err)) return push_file_error(L, err);
  lua_pushinteger(L, static_cast<lua_Integer>(base + offset));
  return 1;
}

int l_file_size(lua_State* L) {
  LuaFile* file = file_check(L, 1);
  if (g_file_ops.file_size == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "there is no file system");
  }
  FsError err = FsError::kOk;
  const int64_t size = g_file_ops.file_size(file->handle, err);
  if (size < 0) return push_file_error(L, err);
  lua_pushinteger(L, static_cast<lua_Integer>(size));
  return 1;
}

// There is nothing to flush: every write goes through the backend at once, and
// the file is only guaranteed complete when it is closed (docs/30).
int l_file_flush(lua_State* L) {
  file_check(L, 1);
  lua_pushboolean(L, 1);
  return 1;
}

int l_file_close(lua_State* L) {
  file_check(L, 1);
  LuaFile* file = static_cast<LuaFile*>(lua_touserdata(L, 1));
  if (file->handle != 0 && g_file_ops.file_close != nullptr) {
    FsError err = FsError::kOk;
    g_file_ops.file_close(file->handle, err);
  }
  file->handle = 0;
  lua_pushboolean(L, 1);
  return 1;
}

// The collector closes what the app forgot, so a handle is never lost with the
// object (docs/30).
int l_file_gc(lua_State* L) {
  LuaFile* file = static_cast<LuaFile*>(luaL_checkudata(L, 1, kFileMeta));
  if (file->handle != 0 && g_file_ops.file_close != nullptr) {
    FsError err = FsError::kOk;
    g_file_ops.file_close(file->handle, err);
  }
  file->handle = 0;
  return 0;
}

int l_file_tostring(lua_State* L) {
  LuaFile* file = static_cast<LuaFile*>(luaL_checkudata(L, 1, kFileMeta));
  lua_pushstring(L, (file->handle != 0) ? "file (open)" : "file (closed)");
  return 1;
}

void open_file_library(lua_State* L) {
  luaL_newmetatable(L, kFileMeta);
  lua_createtable(L, 0, 8);
  set_function(L, "read", l_file_read);
  set_function(L, "write", l_file_write);
  set_function(L, "seek", l_file_seek);
  set_function(L, "size", l_file_size);
  set_function(L, "flush", l_file_flush);
  set_function(L, "close", l_file_close);
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, &l_file_gc);
  lua_setfield(L, -2, "__gc");
  lua_pushcfunction(L, &l_file_tostring);
  lua_setfield(L, -2, "__tostring");
  lua_pop(L, 1);
}

int l_fs_open(lua_State* L) {
  const char* path = luaL_checkstring(L, 1);
  const char* mode = luaL_optstring(L, 2, "r");
  if (g_file_ops.open == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "there is no file system");
  }
  FsError err = FsError::kOk;
  const int32_t handle = g_file_ops.open(path, mode, err);
  if (handle < 0) return push_fs_error(L, err);

  LuaFile* file = static_cast<LuaFile*>(lua_newuserdatauv(L, sizeof(LuaFile), 0));
  file->handle = handle;
  luaL_getmetatable(L, kFileMeta);
  lua_setmetatable(L, -2);
  return 1;
}

// ---------------------------------------------------------------------------------------------
// apps: the door of the apps, and the wish to start one (0.153.0, docs/30 and docs/40)
// ---------------------------------------------------------------------------------------------

// **The list a launcher draws**: one table per app, built as the walk of the door hands them in, so nothing of the size
// of the list lives in C —the door loads one manifest at a time and this copies it into the interpreter's heap, which is
// the memory the app was given— (docs/75).
struct AppList {
  lua_State* L;
  int array;  // absolute index of the array being filled
  int count;
};

// **The list is a list of names** (0.154.0): what the door presents is an id and nothing else has been read, so the
// table is a table of strings —the cheapest thing a launcher can draw and the only thing that is true before a manifest
// is opened—.
bool add_id(const AppInfo& info, void* context) {
  AppList* list = static_cast<AppList*>(context);
  ++list->count;
  lua_pushstring(list->L, info.id);
  lua_rawseti(list->L, list->array, list->count);
  return true;
}


// apps.list(): **the ids of the door**, in the order the door presents them, and no manifest read (0.154.0). A launcher
// draws these first —the id as the name and a placeholder picture— and then asks for one app at a time with
// `apps.info`, so the page is up before the first manifest is opened and one app that is slow does not hold the others
// (docs/30, docs/40).
int l_apps_list(lua_State* L) {
  if (g_apps_ops.ids == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this system has no door of the apps");
  }
  lua_createtable(L, 0, 0);
  AppList list = {L, lua_gettop(L), 0};
  if (!g_apps_ops.ids(&add_id, &list)) {
    lua_settop(L, 0);
    return push_nil_error(L, "E_UNSUPPORTED", "the door of the apps did not answer");
  }
  return 1;
}

// apps.info(id): **the manifest of one app**, as the door reads it. The answer is the table a launcher draws —the name a
// person reads, the picture to press, whether it is a launcher of its own and whether this board can run it—, and the
// reason for a no is the system's, because a launcher that cannot say why a cell is empty is a launcher a person
// cannot use (docs/30).
int l_apps_info(lua_State* L) {
  const char* id = luaL_checkstring(L, 1);
  if (g_apps_ops.info == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this system has no door of the apps");
  }
  AppInfo info = {};
  char why[96];
  why[0] = '\0';
  if (!g_apps_ops.info(id, info, why, sizeof(why))) {
    return push_nil_error(L, "E_INVAL", (why[0] != '\0') ? why : "that app cannot be read");
  }
  lua_createtable(L, 0, 6);
  lua_pushstring(L, info.id);
  lua_setfield(L, -2, "id");
  lua_pushstring(L, info.title);
  lua_setfield(L, -2, "title");
  lua_pushstring(L, info.icon);
  lua_setfield(L, -2, "icon");
  // **I la versió** (0.189.22): el número que l'app diu que és, perquè una pàgina que guarda una cel·la pugui saber si el que té
  // guardat és de l'app d'abans (docs/30, docs/40).
  lua_pushstring(L, info.version);
  lua_setfield(L, -2, "version");
  lua_pushboolean(L, info.launcher ? 1 : 0);
  lua_setfield(L, -2, "launcher");
  lua_pushboolean(L, info.on_card ? 1 : 0);
  lua_setfield(L, -2, "card");
  lua_pushboolean(L, info.works_here ? 1 : 0);
  lua_setfield(L, -2, "works_here");
  return 1;
}

// **`apps.opener(name)`: who opens this file, asked to the manifests** (0.190.257, the old browser's `paperos.opener`).
// It answers two things or a reason: the id of the app that claims the extension —**the first one that claims it wins**,
// in the order the door lists them, because two apps that open `.txt` is a decision somebody took by putting the one
// they want first— and the switches that app asked for when a browser hands it a file (`opensArgs`, which is how the
// editor says `-ro` without the browser knowing what an editor is). **A hidden name has no opener on purpose**: the
// `.photo.png.cache.ics` beside a picture is a thumbnail, and a door that opened it would offer the cache as the file.
// **The extension is read from the last dot and in lower case**, so a card written on a computer (`FOTO.PNG`) is the
// same kind of file here as there (docs/30, docs/40, docs/45).
int l_apps_opener(lua_State* L) {
  const char* name = luaL_checkstring(L, 1);
  if (g_apps_ops.opener == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this system cannot say who opens a file");
  }
  char id[48];
  char flags[32];
  char why[96];
  id[0] = '\0';
  flags[0] = '\0';
  why[0] = '\0';
  if (!g_apps_ops.opener(name, id, sizeof(id), flags, sizeof(flags), why, sizeof(why))) {
    return push_nil_error(L, "E_INVAL", (why[0] != '\0') ? why : "no app of this system opens that file");
  }
  lua_pushstring(L, id);
  lua_pushstring(L, flags);
  return 2;
}

// app.run(id): **the wish to start another app**, which the kernel carries out at the end of the turn (docs/30, docs/90).
// It answers true and nothing has happened yet; the reason of a refusal is the system's, because the system is the one
// that knows whether the app is there, whether it runs on this board and whether it is the app that is asking.
// **sys.reboot([reason])** (0.171.0): the page of the eraser ends by offering a restart, and an app that has just
// emptied the store it is running on cannot be trusted to keep running. The reason is the app's and it is written in the
// log with the name of who asked; with none, the name of the app is the reason. It is the prompt's `reboot` with a name
// in front of it: the picture of the moment goes on the glass, the ink is waited on, and the board restarts (docs/30,
// docs/50). Never returns when it works.
int l_sys_reboot(lua_State* L) {
  if (g_app_ops.reboot == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this system does not restart on command");
  }
  const char* reason = luaL_optstring(L, 1, nullptr);
  if (!g_app_ops.reboot(reason)) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board cannot restart itself");
  }
  return 0;
}

// **sys.deepsleep([reason])** (0.189.25): la lluna de la targeta de la barra, per a una app que és una pàgina i vol la placa
// adormida amb una tecla per tornar —un lector, un rellotge—. It is not `power.sleep('deep')`, which on this board is a power
// off waiting for an hour, and it is not `sys.reboot`, which comes back by itself: here the board **keeps its supply** and the
// chip sleeps until a person presses a key, so what stays on the glass is the page and what comes back is this app, at the
// next boot (a deep sleep is a reset). The reason is the app's and goes to the log with the name of who asked; with none, it
// is the app's own name. Never returns when it works (docs/30, docs/50, docs/90).
int l_sys_deepsleep(lua_State* L) {
  if (g_power_ops.sleep_with_the_keys == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this system cannot sleep with its keys awake");
  }
  const char* reason = luaL_optstring(L, 1, nullptr);
  if (!g_power_ops.sleep_with_the_keys(reason)) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board cannot sleep with its keys awake");
  }
  return 0;
}

int l_app_run(lua_State* L) {
  const char* id = luaL_checkstring(L, 1);
  // **`app.run(id[, back[, flags]])`**: `back` is true unless the app says otherwise, because an app that opens another one is
  // the page the person expects to come back to —a launcher, a menu, a shelf— and a handover with no way back is the
  // one that needs to be asked for (docs/30, docs/90). **`flags` són els commutadors de l'app que s'arrenca** (0.189.161): they
  // reach it as `app.args.flags`, the same door `opensArgs` of a manifest uses, i una segona cadena vol dir els commutadors amb la
  // tornada de sèrie —`app.run('appinfo', '-d battery')` és el que el llançador demana al dit llarg d'una cel·la— (docs/30, docs/40).
  bool back = true;
  const char* flags = nullptr;
  if (lua_gettop(L) >= 2) {
    if (lua_type(L, 2) == LUA_TSTRING) {
      flags = lua_tostring(L, 2);
    } else {
      back = (lua_toboolean(L, 2) != 0);
    }
  }
  if (lua_gettop(L) >= 3 && lua_type(L, 3) == LUA_TSTRING) flags = lua_tostring(L, 3);
  // **I el quart argument és el fitxer** (0.190.257): `app.run('editor', true, nil, '/sd/notes.txt')` hands the file the way
  // the prompt does, and the app that arrives reads it at `app.args.path`. A browser that has resolved who opens a file
  // needs this second half or it cannot open anything at all (docs/30, docs/45).
  const char* path = nullptr;
  if (lua_gettop(L) >= 4 && lua_type(L, 4) == LUA_TSTRING) path = lua_tostring(L, 4);
  if (g_app_ops.run == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this system cannot start another app");
  }
  char why[96];
  why[0] = '\0';
  if (!g_app_ops.run(id, back, flags, path, why, sizeof(why))) {
    return push_nil_error(L, "E_INVAL", (why[0] != '\0') ? why : "that app cannot be started");
  }
  lua_pushboolean(L, 1);
  return 1;
}

// **The views of the app in front** (0.163.0): `app.view()` is how many pages the app has on top of its first one,
// and the two calls that move it are `app.view_push()` and `app.view_pop()`. A page that shows a detail over the list
// it came from pushes one, and **the system is the one that reads the number** when a pinch comes from big to small:
// a view on top is given back (the app hears `on_back(depth)`) and zero views is the app itself, which the pinch
// closes. The number is the kernel's because a pinch means the same wish in every app (docs/30, docs/46).
int l_app_view(lua_State* L) {
  if (g_app_ops.views == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this system does not count the views of an app");
  }
  lua_pushinteger(L, static_cast<lua_Integer>(g_app_ops.views()));
  return 1;
}

int l_app_view_push(lua_State* L) {
  if (g_app_ops.push_view == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this system does not count the views of an app");
  }
  lua_pushinteger(L, static_cast<lua_Integer>(g_app_ops.push_view()));
  return 1;
}

int l_app_view_pop(lua_State* L) {
  if (g_app_ops.pop_view == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this system does not count the views of an app");
  }
  lua_pushinteger(L, static_cast<lua_Integer>(g_app_ops.pop_view()));
  return 1;
}

// ---------------------------------------------------------------------------
// term: the size of the page, and no escape codes at all
// ---------------------------------------------------------------------------
//
// **Aquest sistema no parla codis d'escapament** (0.190.187, l'amo: *«Encara queden coses amb ansi! Busca per tot el codi restes
// d'ansi i treu-lo»*): `term.clear`, `reset`, `color`, `move` i els dos del cursor escrivien `ESC [ … m` al port, que és text que una
// persona no ha escrit i que el registre no ha de portar; fora ells, i amb ells les constants de color, que només tenien sentit per a
// `term.color` (docs/30). El que queda és el que una app necessita saber d'una pàgina de text: quantes columnes i quantes línies.

// The size a plain terminal has. cfg/console.json will be read when the config
// layer exists; until then this is the default the contract promises (docs/30).
int l_term_width(lua_State* L) {
  lua_pushinteger(L, 80);
  return 1;
}

// app.info(): the manifest of the app itself, as far as the system can tell.
int l_app_info(lua_State* L) {
  lua_createtable(L, 0, 4);
  lua_pushstring(L, g_boot.app_id);
  lua_setfield(L, -2, "id");
  lua_pushstring(L, g_boot.app_title);
  lua_setfield(L, -2, "title");
  lua_pushstring(L, g_boot.app_version);
  lua_setfield(L, -2, "version");
  lua_pushstring(L, g_boot.app_opens);
  lua_setfield(L, -2, "opens");
  return 1;
}

int l_term_height(lua_State* L) {
  lua_pushinteger(L, 24);
  return 1;
}

// ---------------------------------------------------------------------------
// app: who is running, what it was asked to open, and how to leave
// ---------------------------------------------------------------------------

// app.exit([code]): it does not return. The code travels to whoever started the
// app; the error used to unwind is not an error and is not printed.
int l_app_exit(lua_State* L) {
  g_exit_code = static_cast<int>(luaL_optinteger(L, 1, 0));
  g_exited_by_app = true;
  g_app_wants_to_stop = true;
  lua_pushliteral(L, "app.exit");
  return lua_error(L);
}

// sys.delay(ms): a promise that settles on its own when the time passes.
int l_sys_delay(lua_State* L) {
  lua_Integer ms = luaL_optinteger(L, 1, 0);
  if (ms < 0) ms = 0;
  const int p = push_promise(L, now_ms() + static_cast<int64_t>(ms));
  pending_add(L, p);
  lua_pushvalue(L, p);
  return 1;
}

// How long an app waits for the association by default. It is a constant so a
// person can change it in one place (docs/30).
const lua_Integer kNetTimeoutDefaultMs = 20000;

// net.connect(ssid[, pass[, timeout_ms]]): a promise, like every call that takes
// a while. **It never blocks**: the kernel keeps turning while the radio works
// and settles the promise from outside (docs/30).
int l_net_connect(lua_State* L) {
  const char* ssid = luaL_checkstring(L, 1);
  const char* pass = luaL_optstring(L, 2, nullptr);
  lua_Integer timeout = luaL_optinteger(L, 3, kNetTimeoutDefaultMs);
  if (timeout < 0) timeout = 0;

  // Even a call that cannot start gives back a promise: an app that writes
  // `net.connect(...):await()` gets an error it can catch, not a different shape
  // of answer depending on the board it runs on.
  const int p = push_promise(L, 0);
  const char* code = nullptr;
  const char* message = nullptr;
  if (g_net_ops.connect == nullptr) {
    code = "E_UNSUPPORTED";
    message = "there is no network";
  } else if (!g_net_ops.connect(ssid, pass, static_cast<uint32_t>(timeout))) {
    code = "E_BUSY";
    message = "the network is already connecting, or it refused to start";
  }
  if (code != nullptr) {
    push_error_table(L, code, message);
    promise_settle(L, p, kErr, lua_gettop(L));
    lua_pop(L, 1);
    lua_pushvalue(L, p);
    return 1;
  }

  pending_add(L, p);
  lua_pushvalue(L, p);
  lua_setfield(L, LUA_REGISTRYINDEX, kNetKey);
  lua_pushvalue(L, p);
  return 1;
}

// net.connect_stored([ms]): a promise on the network the board already has. It is
// what an app calls after a reboot, when provisioning is long done and the
// password is somewhere the app is not (docs/50).
int l_net_connect_stored(lua_State* L) {
  lua_Integer timeout = luaL_optinteger(L, 1, kNetTimeoutDefaultMs);
  if (timeout < 0) timeout = 0;

  const int p = push_promise(L, 0);
  const char* code = nullptr;
  const char* message = nullptr;
  if (g_net_ops.connect_stored == nullptr) {
    code = "E_UNSUPPORTED";
    message = "there is no network";
  } else if (!g_net_ops.connect_stored(static_cast<uint32_t>(timeout))) {
    code = "E_INVAL";
    message = "the board has no network stored, or it is already doing something";
  }
  if (code != nullptr) {
    push_error_table(L, code, message);
    promise_settle(L, p, kErr, lua_gettop(L));
    lua_pop(L, 1);
    lua_pushvalue(L, p);
    return 1;
  }

  pending_add(L, p);
  lua_pushvalue(L, p);
  lua_setfield(L, LUA_REGISTRYINDEX, kNetKey);
  lua_pushvalue(L, p);
  return 1;
}

// How long an app waits for somebody to hand the board a network. Three minutes
// is a person typing in a phone app, and it is a constant so it can be changed in
// one place (docs/50).
const lua_Integer kProvTimeoutDefaultMs = 180000;

// net.provision([pop[, service[, ms]]]): the access point goes up and the promise
// is kept until the board is **on the network**, not merely holding a password
// (docs/50). It never blocks, like everything that takes a while here.
int l_net_provision(lua_State* L) {
  const char* pop = luaL_optstring(L, 1, nullptr);
  const char* service = luaL_optstring(L, 2, nullptr);
  lua_Integer timeout = luaL_optinteger(L, 3, kProvTimeoutDefaultMs);
  if (timeout < 0) timeout = 0;

  const int p = push_promise(L, 0);
  const char* code = nullptr;
  const char* message = nullptr;
  if (g_net_ops.provision == nullptr) {
    code = "E_UNSUPPORTED";
    message = "this board cannot be provisioned";
  } else if (!g_net_ops.provision(pop, service, static_cast<uint32_t>(timeout))) {
    code = "E_BUSY";
    message = "the network is already doing something, or provisioning did not start";
  }
  if (code != nullptr) {
    push_error_table(L, code, message);
    promise_settle(L, p, kErr, lua_gettop(L));
    lua_pop(L, 1);
    lua_pushvalue(L, p);
    return 1;
  }

  pending_add(L, p);
  lua_pushvalue(L, p);
  lua_setfield(L, LUA_REGISTRYINDEX, kNetKey);
  lua_pushvalue(L, p);
  return 1;
}

// net.add(ssid[, pass[, ms]]): the network goes into the board's store and the
// board gets on it, without any phone app in the middle. It is the way in when the
// Espressif app cannot list the networks, and the way in for a headless board
// (docs/50).
int l_net_add(lua_State* L) {
  const char* ssid = luaL_checkstring(L, 1);
  const char* pass = luaL_optstring(L, 2, nullptr);
  lua_Integer timeout = luaL_optinteger(L, 3, kNetTimeoutDefaultMs);
  if (timeout < 0) timeout = 0;

  const int p = push_promise(L, 0);
  const char* code = nullptr;
  const char* message = nullptr;
  if (g_net_ops.add_network == nullptr) {
    code = "E_UNSUPPORTED";
    message = "there is no network";
  } else if (ssid[0] == '\0') {
    code = "E_INVAL";
    message = "the network needs a name";
  } else if (!g_net_ops.add_network(ssid, pass, static_cast<uint32_t>(timeout))) {
    code = "E_BUSY";
    message = "the network is already doing something, or the board refused the network";
  }
  if (code != nullptr) {
    push_error_table(L, code, message);
    promise_settle(L, p, kErr, lua_gettop(L));
    lua_pop(L, 1);
    lua_pushvalue(L, p);
    return 1;
  }

  pending_add(L, p);
  lua_pushvalue(L, p);
  lua_setfield(L, LUA_REGISTRYINDEX, kNetKey);
  lua_pushvalue(L, p);
  return 1;
}

// net.scan([ms]): a promise that answers with how many networks the radio found.
// It is how an app (or a person) knows whether the radio can see anything at all,
// which is the first question when a phone app shows an empty list (docs/50).
int l_net_scan(lua_State* L) {
  lua_Integer timeout = luaL_optinteger(L, 1, kNetTimeoutDefaultMs);
  if (timeout < 0) timeout = 0;

  const int p = push_promise(L, 0);
  const char* code = nullptr;
  const char* message = nullptr;
  if (g_net_ops.scan_start == nullptr) {
    code = "E_UNSUPPORTED";
    message = "there is no network";
  } else if (!g_net_ops.scan_start(static_cast<uint32_t>(timeout))) {
    code = "E_BUSY";
    message = "the network is doing something else, or the radio refused to scan";
  }
  if (code != nullptr) {
    push_error_table(L, code, message);
    promise_settle(L, p, kErr, lua_gettop(L));
    lua_pop(L, 1);
    lua_pushvalue(L, p);
    return 1;
  }

  pending_add(L, p);
  lua_pushvalue(L, p);
  lua_setfield(L, LUA_REGISTRYINDEX, kNetKey);
  lua_pushvalue(L, p);
  return 1;
}

// net.stored(): the name of the network the board has, or nil. A person asking
// why the board does not connect deserves to see what it thinks it knows
// (docs/50).
int l_net_stored(lua_State* L) {
  char ssid[33];
  if (g_net_ops.stored_ssid == nullptr || !g_net_ops.stored_ssid(ssid, sizeof(ssid))) {
    lua_pushnil(L);
    return 1;
  }
  lua_pushstring(L, ssid);
  return 1;
}

// net.scan_record(i): one network of the last scan, `{ssid = ..., rssi = ...}`, or
// nil. The index starts at 1 and goes up to what net.scan answered (docs/30).
int l_net_scan_record(lua_State* L) {
  const lua_Integer index = luaL_checkinteger(L, 1);
  if (index < 1 || g_net_ops.scan_record == nullptr) {
    lua_pushnil(L);
    return 1;
  }
  char ssid[33];
  int32_t rssi = 0;
  if (!g_net_ops.scan_record(static_cast<uint32_t>(index - 1), ssid, sizeof(ssid), rssi)) {
    lua_pushnil(L);
    return 1;
  }
  lua_createtable(L, 0, 2);
  lua_pushstring(L, ssid);
  lua_setfield(L, -2, "ssid");
  lua_pushinteger(L, static_cast<lua_Integer>(rssi));
  lua_setfield(L, -2, "rssi");
  return 1;
}

// net.ntp([server[, ms]]): the clock is set from a time server, and the promise
// is `true` when it is. The server comes from the app because a board has no
// address of its own to trust (docs/30, docs/60).
// `net.state()` is the HAL's state as a number (0 off, 1 connecting, 2 up,
// 3 failed): this library keeps knowing nothing about the HAL, so the number is
// named here and nowhere else (docs/30).
const uint8_t kNetUp = 2;

int l_net_ntp(lua_State* L) {
  const char* server = luaL_optstring(L, 1, nullptr);
  lua_Integer timeout = luaL_optinteger(L, 2, kNetTimeoutDefaultMs);
  if (timeout < 0) timeout = 0;

  const int p = push_promise(L, 0);
  const char* code = nullptr;
  const char* message = nullptr;
  if (g_net_ops.ntp_start == nullptr) {
    code = "E_UNSUPPORTED";
    message = "there is no network";
  } else if (!g_net_ops.ntp_start(server, static_cast<uint32_t>(timeout))) {
    // Two different refusals, and the promise has to say which: no network at
    // all, or the radio busy with something else (docs/30).
    const bool up = (g_net_ops.state != nullptr) && (g_net_ops.state() == kNetUp);
    code = up ? "E_BUSY" : "E_NONET";
    message = up ? "the network is doing something else" : "the board is not on a network";
  }
  if (code != nullptr) {
    push_error_table(L, code, message);
    promise_settle(L, p, kErr, lua_gettop(L));
    lua_pop(L, 1);
    lua_pushvalue(L, p);
    return 1;
  }

  pending_add(L, p);
  // **And it is the promise the queue answers** (0.174.0): `net_progress` settles the one the registry holds in
  // `kNetKey`, and this call was the only one of the seven that did not put it there —`connect`, `connect_stored`,
  // `provision`, `add` and `scan` all do—, so `net.ntp():await()` of an app waited for ever while the shell's `ntp`
  // answered, because the shell waits on the native promise and not on this one. Found by the case of the `tz` page,
  // which is the first app of this tree that asks the clock for the time (10/11, docs/30, docs/85).
  lua_pushvalue(L, p);
  lua_setfield(L, LUA_REGISTRYINDEX, kNetKey);
  lua_pushvalue(L, p);
  return 1;
}

// How much of a page an app may hold by default. Sixteen kilobytes is a page of
// text and a quarter of the ceiling an app has (docs/75).
const lua_Integer kHttpBytesDefault = 16384;

// Why a page did not go into the queue, said as the code an app reads (docs/30).
void refuse_page(lua_State* L, NetRefuse refused, const int p, const char* address) {
  const char* code = nullptr;
  const char* message = nullptr;
  switch (refused) {
    case NetRefuse::kOk:
      return;
    case NetRefuse::kNoRadio:
      code = "E_UNSUPPORTED";
      message = "there is no network";
      break;
    case NetRefuse::kTooBig:
      code = "E_TOOBIG";
      message = "the address is longer than the system carries";
      break;
    case NetRefuse::kInval:
      code = "E_INVAL";
      message = address;
      break;
    case NetRefuse::kNoVolume:
      code = "E_UNAVAIL";
      message = "the place the page was to go is not on this board";
      break;
    case NetRefuse::kBusy:
      code = "E_BUSY";
      message = "the network is doing something else";
      break;
  }
  push_error_table(L, code, message);
  promise_settle(L, p, kErr, lua_gettop(L));
  lua_pop(L, 1);
}

// http.get(url[, max_bytes[, ms]]): a promise of `{status, body, bytes}`. It goes
// through the same queue as the rest and brings the network up by itself, so an
// app does not have to connect first (docs/30, docs/75).
int l_http_get(lua_State* L) {
  const char* url = luaL_checkstring(L, 1);
  lua_Integer max_bytes = luaL_optinteger(L, 2, kHttpBytesDefault);
  lua_Integer timeout = luaL_optinteger(L, 3, kNetTimeoutDefaultMs);
  if (max_bytes <= 0) max_bytes = 1;
  if (timeout < 0) timeout = 0;

  const int p = push_promise(L, 0);
  NetRefuse refused = NetRefuse::kNoRadio;
  if (g_net_ops.http_get != nullptr) {
    refused = g_net_ops.http_get(url, static_cast<uint32_t>(max_bytes),
                                 static_cast<uint32_t>(timeout));
  }
  if (refused != NetRefuse::kOk) {
    refuse_page(L, refused, p, "there is no address to fetch");
    lua_pushvalue(L, p);
    return 1;
  }

  pending_add(L, p);
  lua_pushvalue(L, p);
  lua_setfield(L, LUA_REGISTRYINDEX, kHttpKey);
  lua_pushvalue(L, p);
  return 1;
}

// http.download(url, path[, ms]): a promise of `{status, path, bytes}`. The page goes to the file
// **whole**, however long it is, and the app works with it afterwards: a page of a few hundred
// kilobytes does not fit in an app's heap and a file does (docs/30, docs/70, docs/75).
int l_http_download(lua_State* L) {
  const char* url = luaL_checkstring(L, 1);
  const char* path = luaL_checkstring(L, 2);
  lua_Integer timeout = luaL_optinteger(L, 3, kNetTimeoutDefaultMs);
  if (timeout < 0) timeout = 0;

  const int p = push_promise(L, 0);
  NetRefuse refused = NetRefuse::kNoRadio;
  if (g_net_ops.download != nullptr) {
    refused = g_net_ops.download(url, path, static_cast<uint32_t>(timeout));
  }
  if (refused != NetRefuse::kOk) {
    refuse_page(L, refused, p, "there is no address to fetch or nowhere to put it");
    lua_pushvalue(L, p);
    return 1;
  }

  pending_add(L, p);
  lua_pushvalue(L, p);
  lua_setfield(L, LUA_REGISTRYINDEX, kHttpKey);
  lua_pushvalue(L, p);
  return 1;
}

// net.provisioned(): true when the board already knows a network. An app that is
// about to ask a person for their password can ask this first (docs/50).
int l_net_provisioned(lua_State* L) {
  lua_pushboolean(L, (g_net_ops.provisioned != nullptr && g_net_ops.provisioned()) ? 1 : 0);
  return 1;
}

// net.forget(): the stored network goes away, and the next boot is a board with
// no network at all. This is the person's decision, so it is a call and not a
// side effect (docs/50).
int l_net_forget(lua_State* L) {
  if (g_net_ops.forget == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board cannot be provisioned");
  }
  if (!g_net_ops.forget()) return push_nil_error(L, "E_BUSY", "the network is doing something");
  lua_pushboolean(L, 1);
  return 1;
}

// net.disconnect(): the app decides to let the radio go, for instance when it is
// done and does not want the power cost (docs/75).
int l_net_disconnect(lua_State* L) {
  if (g_net_ops.disconnect == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "there is no network");
  }
  if (!g_net_ops.disconnect()) return push_nil_error(L, "E_INVAL", "the network is not on");
  lua_pushboolean(L, 1);
  return 1;
}

// net.status(): "off", "connecting", "up" or "failed". Words and not numbers
// because an app reads them in a log.
int l_net_status(lua_State* L) {
  uint8_t state = 0;
  if (g_net_ops.state != nullptr) state = g_net_ops.state();
  const char* name = "off";
  if (state == 1) name = "connecting";
  if (state == 2) name = "up";
  if (state == 3) name = "failed";
  lua_pushstring(L, name);
  return 1;
}

// net.caps(): a bit is true only when the board really has it (docs/50).
int l_net_caps(lua_State* L) {
  const uint32_t bits = (g_net_ops.features != nullptr) ? g_net_ops.features() : 0;
  lua_createtable(L, 0, 4);
  lua_pushboolean(L, (bits & kNetWifi) != 0 ? 1 : 0);
  lua_setfield(L, -2, "wifi");
  lua_pushboolean(L, (bits & kNetHttp) != 0 ? 1 : 0);
  lua_setfield(L, -2, "http");
  lua_pushboolean(L, (bits & kNetTls) != 0 ? 1 : 0);
  lua_setfield(L, -2, "tls");
  lua_pushboolean(L, (bits & kNetProv) != 0 ? 1 : 0);
  lua_setfield(L, -2, "prov");
  return 1;
}

// qr: the library is useful on its own (a link, a code on a screen, a label), so
// it is here and not only inside the provisioning of the kernel. The symbol is
// handed over as a string of '0' and '1', one per module, row by row, because a
// table of tables would cost the app an allocation per module (docs/30).
const uint32_t kQrLuaLimit = 4096;  // modules: a 64x64 symbol, which is version 11

bool parse_qr_level(const char* name, qr::Level& out) {
  if (name == nullptr) return false;
  if (name[0] == 'l' || name[0] == 'L') {
    out = qr::Level::kL;
    return true;
  }
  if (name[0] == 'm' || name[0] == 'M') {
    out = qr::Level::kM;
    return true;
  }
  if (name[0] == 'q' || name[0] == 'Q') {
    out = qr::Level::kQ;
    return true;
  }
  if (name[0] == 'h' || name[0] == 'H') {
    out = qr::Level::kH;
    return true;
  }
  return false;
}

bool parse_qr_mode(const char* name, qr::Mode& out) {
  if (name == nullptr) return false;
  if (strcmp(name, "auto") == 0) {
    out = qr::Mode::kAuto;
    return true;
  }
  if (strcmp(name, "numeric") == 0) {
    out = qr::Mode::kNumeric;
    return true;
  }
  if (strcmp(name, "alnum") == 0 || strcmp(name, "alphanumeric") == 0) {
    out = qr::Mode::kAlnum;
    return true;
  }
  if (strcmp(name, "byte") == 0) {
    out = qr::Mode::kByte;
    return true;
  }
  return false;
}

// qr.encode(text[, "l"|"m"|"q"|"h"[, "auto"|"numeric"|"alnum"|"byte"]]): a table
// with the side, the version, the mask, the level, the mode that was used, and
// the modules.
int l_qr_encode(lua_State* L) {
  const char* text = luaL_checkstring(L, 1);
  qr::Level level = qr::Level::kM;
  qr::Mode mode = qr::Mode::kAuto;
  const char* level_name = luaL_optstring(L, 2, "m");
  const char* mode_name = luaL_optstring(L, 3, "auto");
  if (!parse_qr_level(level_name, level)) {
    return push_nil_error(L, "E_INVAL", "the level is l, m, q or h");
  }
  if (!parse_qr_mode(mode_name, mode)) {
    return push_nil_error(L, "E_INVAL", "the mode is auto, numeric, alnum or byte");
  }

  const uint32_t side = qr::side_for(text, level, mode);
  if (side == 0) {
    return push_nil_error(L, "E_INVAL", "the text does not fit, or the mode cannot carry it");
  }
  if (side * side > kQrLuaLimit) {
    return push_nil_error(L, "E_TOOBIG", "that symbol is bigger than an app may hold");
  }

  // **Els quatre kilobytes del símbol es demanen per a aquesta crida i es tornen en sortir** (0.190.202, l'amo: *«busca coses
  // allotjades estàticament i fes-les dinàmiques, passa a psram tot el que puguis»*): una taula fixa per a una funció que una app
  // pot no cridar mai és memòria que es paga sempre. La porta és la de la PSRAM quan la placa en té.
  uint8_t* modules = static_cast<uint8_t*>(pal::extra_memory().allocate(kQrLuaLimit, true));
  if (modules == nullptr) return push_nil_error(L, "E_NOMEM", "there is no memory for the symbol");
  qr::Info info;
  if (!qr::encode(text, level, mode, modules, kQrLuaLimit, info)) {
    pal::extra_memory().release(modules);
    return push_nil_error(L, "E_INVAL", "the symbol could not be drawn");
  }

  lua_createtable(L, 0, 6);
  lua_pushinteger(L, static_cast<lua_Integer>(info.side));
  lua_setfield(L, -2, "side");
  lua_pushinteger(L, static_cast<lua_Integer>(info.version));
  lua_setfield(L, -2, "version");
  lua_pushinteger(L, static_cast<lua_Integer>(info.mask));
  lua_setfield(L, -2, "mask");
  static const char* const kLevelNames[4] = {"l", "m", "q", "h"};
  lua_pushstring(L, kLevelNames[static_cast<uint32_t>(info.level)]);
  lua_setfield(L, -2, "level");
  const char* used = "byte";
  if (info.mode == qr::Mode::kNumeric) used = "numeric";
  if (info.mode == qr::Mode::kAlnum) used = "alnum";
  lua_pushstring(L, used);
  lua_setfield(L, -2, "mode");

  luaL_Buffer buffer;
  luaL_buffinit(L, &buffer);
  for (uint32_t i = 0; i < info.side * info.side; ++i) {
    luaL_addchar(&buffer, modules[i] != 0 ? '1' : '0');
  }
  pal::extra_memory().release(modules);
  luaL_pushresult(&buffer);
  lua_setfield(L, -2, "modules");
  return 1;
}

// qr.caps(): how far this build goes, and how far an app may go, which is not the
// same number (docs/75).
int l_qr_caps(lua_State* L) {
  const uint32_t version = (qr::kMaxSide - 17) / 4;
  lua_createtable(L, 0, 4);
  lua_pushinteger(L, static_cast<lua_Integer>(version));
  lua_setfield(L, -2, "max_version");
  lua_pushinteger(L, static_cast<lua_Integer>(qr::kMaxSide));
  lua_setfield(L, -2, "max_side");
  lua_pushinteger(L, static_cast<lua_Integer>(kQrLuaLimit));
  lua_setfield(L, -2, "limit");
  lua_pushstring(L, "lmqh");
  lua_setfield(L, -2, "levels");
  return 1;
}

// json: the same pull helpers the system uses for manifests, because there is no
// document tree here on purpose and an app should not build one behind our back
// (docs/30).
int l_json_get_string(lua_State* L) {
  const char* text = luaL_checkstring(L, 1);
  const char* key = luaL_checkstring(L, 2);
  char value[256];
  if (!lunok::json::get_string(text, strlen(text), key, value, sizeof(value))) {
    lua_pushnil(L);
    return 1;
  }
  lua_pushstring(L, value);
  return 1;
}

int l_json_get_int(lua_State* L) {
  const char* text = luaL_checkstring(L, 1);
  const char* key = luaL_checkstring(L, 2);
  long value = 0;
  if (!lunok::json::get_int(text, strlen(text), key, value)) {
    lua_pushnil(L);
    return 1;
  }
  lua_pushinteger(L, static_cast<lua_Integer>(value));
  return 1;
}

// **`rle.pack(text)` i `rle.unpack(text)`, la run-length del sistema** (0.189.6): the same facility `zip` is for
// containers —a thing the system offers and an app asks for by name— over a string of Lua's, which is binary-safe and
// is what a cache of icons is written to and read from. **The block is the extra memory and not the Lua heap**: the
// packed stream of a cache is the size of the cache, and a block asked for and given back in the same call is not a
// leak of a megabyte; the interpreter's heap would keep it until the collector decided (docs/30, docs/75).
int l_rle_pack(lua_State* L) {
  pal::MemoryTag who_asks(pal::kMemoryLua, app_id());
  size_t len = 0;
  const char* in = luaL_checklstring(L, 1, &len);
  if (len == 0) {
    lua_pushstring(L, "");
    return 1;
  }
  if (len > 0x7FFFFFFFu) return push_nil_error(L, "E_INVAL", "too long to pack");

  const uint32_t need = rle::packed_capacity(static_cast<uint32_t>(len));
  void* block = pal::extra_memory().allocate(need, true);
  if (block == nullptr) return push_nil_error(L, "E_NOMEM", "no room to pack");

  const uint32_t got = rle::pack(reinterpret_cast<const uint8_t*>(in), static_cast<uint32_t>(len),
                                 static_cast<uint8_t*>(block), need);
  if (got == 0) {
    pal::extra_memory().release(block);
    return push_nil_error(L, "E_INVAL", "the stream cannot be packed");
  }
  lua_pushlstring(L, static_cast<const char*>(block), static_cast<size_t>(got));
  pal::extra_memory().release(block);
  return 1;
}

// **And back**: the length is the header's, so the block is asked for before anything is unpacked, and a stream that
// is not one of ours says so with a word instead of a picture made of rubbish (docs/30).
int l_rle_unpack(lua_State* L) {
  pal::MemoryTag who_asks(pal::kMemoryLua, app_id());
  size_t len = 0;
  const char* in = luaL_checklstring(L, 1, &len);
  if (len < rle::kHeader) return push_nil_error(L, "E_INVAL", "not a stream of runs");

  const uint32_t want = rle::unpacked_size(reinterpret_cast<const uint8_t*>(in), static_cast<uint32_t>(len));
  if (want == 0) return push_nil_error(L, "E_INVAL", "not a stream of runs");

  void* block = pal::extra_memory().allocate(want, true);
  if (block == nullptr) return push_nil_error(L, "E_NOMEM", "no room to unpack");

  const uint32_t got = rle::unpack(reinterpret_cast<const uint8_t*>(in), static_cast<uint32_t>(len),
                                   static_cast<uint8_t*>(block), want);
  if (got != want) {
    pal::extra_memory().release(block);
    return push_nil_error(L, "E_INVAL", "the stream is broken");
  }
  lua_pushlstring(L, static_cast<const char*>(block), static_cast<size_t>(got));
  pal::extra_memory().release(block);
  return 1;
}

// console.is_foreground(): false inside an alarm window. An app that arms an
// alarm in its main chunk needs this, or every window it gets in arms another one
// and the system calls itself forever (docs/60).
int l_console_is_foreground(lua_State* L) {
  lua_pushboolean(L, g_boot.window ? 0 : 1);
  return 1;
}

// ---------------------------------------------------------------------------
// led and button: the two things this board does with a person
// ---------------------------------------------------------------------------

// led.caps(): how many lights the board has —one on the ATOM Lite, twenty-five on
// the Matrix— and the highest a channel may be, which on the panel is not 255. Zero
// when there is no light at all (docs/30, docs/50).
int l_led_caps(lua_State* L) {
  lua_createtable(L, 0, 5);
  lua_pushinteger(L, static_cast<lua_Integer>(
                         (g_led_ops.count != nullptr) ? g_led_ops.count() : 0));
  lua_setfield(L, -2, "count");
  lua_pushinteger(L, static_cast<lua_Integer>(
                         (g_led_ops.max_value != nullptr) ? g_led_ops.max_value() : 0));
  lua_setfield(L, -2, "max");
  lua_pushboolean(L, (g_led_ops.has_colour != nullptr && g_led_ops.has_colour()) ? 1 : 0);
  lua_setfield(L, -2, "taken");
  // **Què és la llum d'aquesta placa** (0.190.106): `role` és `light` o `backlight`, i `backlight` és cert quan ho és —una pàgina que vol
  // ensenyar el commutador del vidre pregunta això i no el nom de la placa— (docs/30, docs/50).
  const char* role = (g_led_ops.role != nullptr) ? g_led_ops.role() : "light";
  lua_pushstring(L, role);
  lua_setfield(L, -2, "role");
  lua_pushboolean(L, (role != nullptr && strcmp(role, "backlight") == 0) ? 1 : 0);
  lua_setfield(L, -2, "backlight");
  return 1;
}

// led.set(r, g, b): the colour the app wants while it is in front. It is given back
// when the app goes, so a light that is stuck on a colour cannot outlive whoever
// asked for it (docs/30).
int l_led_set(lua_State* L) {
  if (g_led_ops.set_colour == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no light");
  }
  int values[3];
  for (int i = 0; i < 3; ++i) {
    lua_Integer value = luaL_checkinteger(L, i + 1);
    if (value < 0) value = 0;
    if (value > 255) value = 255;
    values[i] = static_cast<int>(value);
  }
  const bool ok = g_led_ops.set_colour(static_cast<uint8_t>(values[0]),
                                       static_cast<uint8_t>(values[1]),
                                       static_cast<uint8_t>(values[2]));
  if (!ok) {
    return push_nil_error(L, "E_BUSY",
                          "the light is busy with what the system is saying; wait for on_light");
  }
  lua_pushboolean(L, 1);
  return 1;
}

// led.pixel(i, r, g, b): one light of the strip, for a board with more than one.
// The index is counted from zero and `led.caps().count` says how many there are;
// an index that is not there is refused instead of painting somewhere else, because
// a light that silently goes to the wrong square is a picture that lies (docs/30).
int l_led_pixel(lua_State* L) {
  if (g_led_ops.set_pixel == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no light");
  }
  const lua_Integer index = luaL_checkinteger(L, 1);
  if (index < 0) {
    return push_nil_error(L, "E_INVAL", "a light is counted from zero, not below it");
  }
  int values[3];
  for (int i = 0; i < 3; ++i) {
    lua_Integer value = luaL_checkinteger(L, i + 2);
    if (value < 0) value = 0;
    if (value > 255) value = 255;
    values[i] = static_cast<int>(value);
  }
  if (!g_led_ops.set_pixel(static_cast<uint32_t>(index), static_cast<uint8_t>(values[0]),
                           static_cast<uint8_t>(values[1]),
                           static_cast<uint8_t>(values[2]))) {
    // Two reasons and they mean different things to an app: a light of the strip that is not there
    // is the app's mistake, and a busy light is a moment to wait out (docs/30).
    if (static_cast<uint32_t>(index) >= g_led_ops.count()) {
      return push_nil_error(L, "E_INVAL", "this board has no light at that index");
    }
    return push_nil_error(L, "E_BUSY",
                          "the light is busy with what the system is saying; wait for on_light");
  }
  lua_pushboolean(L, 1);
  return 1;
}

// led.off(): black, which is the same as set(0, 0, 0) and reads better.
// **`serial.claim()`: el port passa a ser de l'app** (0.189.20, docs/30, docs/50): while it is ceded the system writes nothing
// to the wire —no prompt, no log echo, no table of `stats`— and the app's own words still go there, because the app is who
// has it. The answer is `true`, or `nil, E_BUSY` when somebody else has it: two owners of one wire is the mess this door
// exists to avoid. The id of the app is the kernel's to put, and the kernel is also the one that gives it back when the app
// closes, like the light.
int l_serial_claim(lua_State* L) {
  (void)L;
  if (g_serial_ops.claim == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build has no serial port to cede");
  }
  if (!g_serial_ops.claim(nullptr)) {
    return push_nil_error(L, "E_BUSY", "the serial port is already somebody else's");
  }
  lua_pushboolean(L, 1);
  return 1;
}

// **`serial.release()`: i torna al sistema**, which is also what the kernel does when the app that asked ends.
int l_serial_release(lua_State* L) {
  (void)L;
  if (g_serial_ops.release == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build has no serial port to cede");
  }
  if (!g_serial_ops.release(nullptr)) {
    return push_nil_error(L, "E_UNAVAIL", "the serial port was not ceded");
  }
  lua_pushboolean(L, 1);
  return 1;
}

// **`serial.claimed()`: si el port és d'algú**, which is the question a page asks before it says "the port is mine" on the
// glass (docs/30).
int l_serial_claimed(lua_State* L) {
  (void)L;
  if (g_serial_ops.claimed == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build has no serial port to cede");
  }
  lua_pushboolean(L, g_serial_ops.claimed() ? 1 : 0);
  return 1;
}

// ---- crypto: **l'aritmètica que un secret necessita** (0.190.258) ----
//
// **Four calls and not one more**, because a file of passwords needs exactly four things —entropy, a key, a seal and an
// open— and everything else (the format of the file, the screens, what is written around the block) is the app's. It is
// `paperos.crypto` of the old project, the same four names and the same block, so a vault written by the Paper opens here
// and the other way round (docs/30, docs/45).
//
// **The key is 64 bytes and it is ONE string**: the first 32 are ChaCha20's and the last 32 HMAC's. The same key doing two
// jobs is how two sound primitives become one weak one, and a script that had to split it would be a script that can split
// it wrong.
//
// **Every no comes with its reason**, and they are the old project's numbers: a band with no entropy, a key that is not the
// 64 bytes `crypto.key()` makes, a text too big for one block (16 KB, which is one whole vault), a blob too short to be
// one, or a tag that does not match —a wrong key and a changed file are the same answer on purpose, because telling them
// apart tells whoever has the file whether the password was close.
const lua_Integer kCryptoKeyBytes = 64;
const lua_Integer kCryptoTagBytes = 16;
const lua_Integer kCryptoNonceBytes = 12;
// The price of a derivation has a floor and a ceiling, in ITERATIONS. A thousand is the least that is not a joke and a
// hundred thousand is more than this board can pay inside one turn of the glass (docs/95).
const lua_Integer kCryptoMinIterations = 1000;
const lua_Integer kCryptoMaxIterations = 100000;
// 16 KB: the same ceiling a whole vault has, because one vault is one block and one write (docs/70).
const lua_Integer kCryptoMaxText = 16384;

// crypto.random(n): n bytes of the band's own entropy, 1 to 64. A salt and a nonce are what this is for, and a caller that
// wants more asks several times.
int l_crypto_random(lua_State* L) {
  const lua_Integer n = luaL_checkinteger(L, 1);
  if (n < 1 || n > 64) {
    return push_nil_error(L, "E_INVAL", "the number of random bytes has to be 1 to 64");
  }
  if (g_crypto_ops.random_bytes == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build has no entropy source");
  }
  uint8_t bytes[64];
  if (!g_crypto_ops.random_bytes(bytes, static_cast<uint32_t>(n))) {
    return push_nil_error(L, "E_UNAVAIL", "this board has no entropy source");
  }
  lua_pushlstring(L, reinterpret_cast<const char*>(bytes), static_cast<size_t>(n));
  memset(bytes, 0, sizeof(bytes));
  return 1;
}

// crypto.key(password, salt, iterations): this is the slow one, and slow ON PURPOSE. What it costs is in the kernel's log
// and in its meters, so the number of iterations can be chosen on the board (docs/95).
int l_crypto_key(lua_State* L) {
  size_t password_len = 0;
  size_t salt_len = 0;
  const char* password = luaL_checklstring(L, 1, &password_len);
  const char* salt = luaL_checklstring(L, 2, &salt_len);
  const lua_Integer iterations = luaL_checkinteger(L, 3);

  if (password_len == 0) return push_nil_error(L, "E_INVAL", "the password is empty");
  if (password_len > 128) return push_nil_error(L, "E_INVAL", "that password is too long (128 bytes at most)");
  if (salt_len < 8 || salt_len > 64) {
    return push_nil_error(L, "E_INVAL", "the salt has to be between 8 and 64 bytes");
  }
  if (iterations < kCryptoMinIterations || iterations > kCryptoMaxIterations) {
    return push_nil_error(L, "E_INVAL", "the iterations have to be between 1000 and 100000");
  }
  if (g_crypto_ops.key == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build has no key derivation");
  }

  uint8_t key[64];
  if (!g_crypto_ops.key(password, password_len, salt, salt_len, static_cast<uint32_t>(iterations), key, sizeof(key))) {
    return push_nil_error(L, "E_INVAL", "that password and that salt cannot make a key");
  }
  lua_pushlstring(L, reinterpret_cast<const char*>(key), sizeof(key));
  memset(key, 0, sizeof(key));
  return 1;
}

// crypto.seal(key, text): `nonce(12) | ciphertext | tag(16)`. **The nonce is new on every call**, so sealing the same text
// twice does not produce the same bytes: a stream cipher that repeats a nonce with a key repeats its stream, and that is
// the one mistake of this family that hands the plain text away.
int l_crypto_seal(lua_State* L) {
  size_t key_len = 0;
  size_t text_len = 0;
  const char* key = luaL_checklstring(L, 1, &key_len);
  const char* text = luaL_checklstring(L, 2, &text_len);

  if (g_crypto_ops.seal == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build cannot seal a block");
  }
  if (static_cast<lua_Integer>(key_len) != kCryptoKeyBytes) {
    return push_nil_error(L, "E_INVAL", "the key has to be the 64 bytes of crypto.key()");
  }
  if (static_cast<lua_Integer>(text_len) > kCryptoMaxText) {
    return push_nil_error(L, "E_INVAL", "that text is too big to seal in one go");
  }

  const size_t total = static_cast<size_t>(kCryptoNonceBytes) + text_len + static_cast<size_t>(kCryptoTagBytes);
  luaL_Buffer buffer;
  char* out = luaL_buffinitsize(L, &buffer, total);
  if (out == nullptr) {
    return push_nil_error(L, "E_NOMEM", "there is no room for the sealed block");
  }
  if (!g_crypto_ops.seal(reinterpret_cast<const uint8_t*>(key), key_len, text, text_len,
                         reinterpret_cast<uint8_t*>(out), total)) {
    // The stack has the buffer of the answer in it: it is emptied before the error goes in its place.
    luaL_pushresultsize(&buffer, 0);
    lua_pop(L, 1);
    return push_nil_error(L, "E_UNAVAIL", "this board has no entropy source: the nonce would repeat");
  }
  luaL_pushresultsize(&buffer, total);
  return 1;
}

// crypto.open(key, blob): the text of a block, or nil and the reason.
int l_crypto_open(lua_State* L) {
  size_t key_len = 0;
  size_t blob_len = 0;
  const char* key = luaL_checklstring(L, 1, &key_len);
  const char* blob = luaL_checklstring(L, 2, &blob_len);

  if (g_crypto_ops.open == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build cannot open a block");
  }
  if (static_cast<lua_Integer>(key_len) != kCryptoKeyBytes) {
    return push_nil_error(L, "E_INVAL", "the key has to be the 64 bytes of crypto.key()");
  }
  const size_t overhead = static_cast<size_t>(kCryptoNonceBytes) + static_cast<size_t>(kCryptoTagBytes);
  if (blob_len < overhead) {
    return push_nil_error(L, "E_INVAL", "that blob is too short to be one");
  }
  if (static_cast<lua_Integer>(blob_len) > kCryptoMaxText + static_cast<lua_Integer>(overhead)) {
    return push_nil_error(L, "E_INVAL", "that blob is too big for one block");
  }

  const size_t plain = blob_len - overhead;
  if (plain == 0) {
    lua_pushliteral(L, "");
    return 1;
  }
  luaL_Buffer buffer;
  char* out = luaL_buffinitsize(L, &buffer, plain);
  if (out == nullptr) {
    return push_nil_error(L, "E_NOMEM", "there is no room for the plain text");
  }
  const int32_t got = g_crypto_ops.open(reinterpret_cast<const uint8_t*>(key), key_len,
                                        reinterpret_cast<const uint8_t*>(blob), blob_len,
                                        reinterpret_cast<uint8_t*>(out), static_cast<uint32_t>(plain));
  if (got < 0) {
    luaL_pushresultsize(&buffer, 0);
    lua_pop(L, 1);
    return push_nil_error(L, "E_INVAL", "the key is not this blob's, or the blob was changed");
  }
  luaL_pushresultsize(&buffer, static_cast<size_t>(got));
  return 1;
}

int l_led_off(lua_State* L) {
  (void)L;
  if (g_led_ops.set_colour == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no light");
  }
  if (!g_led_ops.set_colour(0, 0, 0)) {
    return push_nil_error(L, "E_BUSY",
                          "the light is busy with what the system is saying; wait for on_light");
  }
  lua_pushboolean(L, 1);
  return 1;
}

// The period an effect has when the app does not give one. They are tastes with a reason:
// a blink at 800 ms is seen without being a strobe, and a dot of 200 ms is the Morse that a
// person sends (docs/30). **I el respir fa quinze segons i no cinc** (0.190.234, l'amo: *«fes
// que l'animació dels leds de breath sigui el triple de llarga»*): és el mateix número que el
// respir de l'arrencada i el de la ràdio, perquè el gest de la casa és un.
// The names come from the kernel, so this switch cannot list an effect the light does not
// have.
uint32_t default_period(uint8_t kind) {
  const char* name = (g_led_ops.effect_name != nullptr) ? g_led_ops.effect_name(kind) : nullptr;
  if (name == nullptr) return 1000;
  if (strcmp(name, "breathe") == 0) return 15000;
  if (strcmp(name, "blink") == 0) return 800;
  if (strcmp(name, "sos") == 0) return 200;
  if (strcmp(name, "bounce") == 0) return 700;
  return 1000;
}

// One field of a table of options, as an integer. A field that is missing keeps the
// default; a field that is there and is not a number is refused, because guessing what an
// app meant with a string where a period goes is how a light ends up doing something
// nobody asked for (docs/30).
bool spec_int(lua_State* L, int index, const char* field, lua_Integer fallback,
              lua_Integer& out) {
  lua_getfield(L, index, field);
  const int type = lua_type(L, -1);
  bool ok = true;
  if (type == LUA_TNIL) {
    out = fallback;
  } else if (type == LUA_TNUMBER) {
    out = lua_tointeger(L, -1);
  } else {
    ok = false;
  }
  lua_pop(L, 1);
  return ok;
}

// `color = {r, g, b}` and `color2 = {r, g, b}`, each clamped to 0..255 the same way `led.set` does
// it. Without a colour the effect is white, and without a second one it is a light of one colour:
// with both, the effect is a **ramp** from the first to the second along every cycle (docs/30).
bool spec_colour(lua_State* L, int index, const char* field, uint8_t& r, uint8_t& g, uint8_t& b) {
  lua_getfield(L, index, field);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    return true;
  }
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return false;
  }
  const int table = lua_gettop(L);
  uint8_t* out[3] = {&r, &g, &b};
  for (int i = 0; i < 3; ++i) {
    lua_rawgeti(L, table, i + 1);
    lua_Integer value = lua_isnumber(L, -1) ? lua_tointeger(L, -1) : 0;
    if (value < 0) value = 0;
    if (value > 255) value = 255;
    *out[i] = static_cast<uint8_t>(value);
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  return true;
}

// A table of options turned into the one struct the light understands (docs/30).
bool read_effect_spec(lua_State* L, int index, LedEffectSpec& spec) {
  if (g_led_ops.effect_from_name == nullptr) return false;
  lua_getfield(L, index, "kind");
  if (lua_type(L, -1) != LUA_TSTRING) {
    lua_pop(L, 1);
    return false;
  }
  uint8_t kind = 0;
  const bool known = g_led_ops.effect_from_name(lua_tostring(L, -1), kind);
  lua_pop(L, 1);
  if (!known) return false;

  spec.kind = kind;
  spec.red = 255;
  spec.green = 255;
  spec.blue = 255;
  if (!spec_colour(L, index, "color", spec.red, spec.green, spec.blue)) return false;
  // One colour unless the app says otherwise: a ramp is written as the two ends of it (docs/30).
  spec.red2 = spec.red;
  spec.green2 = spec.green;
  spec.blue2 = spec.blue;
  if (!spec_colour(L, index, "color2", spec.red2, spec.green2, spec.blue2)) return false;

  lua_Integer value = 0;
  if (!spec_int(L, index, "period",
                static_cast<lua_Integer>(default_period(kind)), value)) {
    return false;
  }
  spec.period_ms = (value < 0) ? 0u : static_cast<uint32_t>(value);

  if (!spec_int(L, index, "cycles", 0, value)) return false;
  spec.cycles = (value < 0) ? 0u : static_cast<uint32_t>(value);

  if (!spec_int(L, index, "pixel", -1, value)) return false;
  spec.pixel = static_cast<int32_t>(value);

  // **Twenty unless the app asks for more**: a light that says something does not have to be the
  // brightest thing on the desk. The board's own ceiling still applies above it (docs/30, docs/75).
  if (!spec_int(L, index, "level", 20, value)) return false;
  if (value < 0) value = 0;
  if (value > 255) value = 255;
  spec.level = static_cast<uint8_t>(value);
  return true;
}

void push_effect_spec(lua_State* L, const LedEffectSpec& spec, uint32_t elapsed_ms) {
  lua_createtable(L, 0, 7);
  const char* name = (g_led_ops.effect_name != nullptr) ? g_led_ops.effect_name(spec.kind) : nullptr;
  lua_pushstring(L, (name != nullptr) ? name : "none");
  lua_setfield(L, -2, "kind");
  lua_createtable(L, 3, 0);
  lua_pushinteger(L, spec.red);
  lua_rawseti(L, -2, 1);
  lua_pushinteger(L, spec.green);
  lua_rawseti(L, -2, 2);
  lua_pushinteger(L, spec.blue);
  lua_rawseti(L, -2, 3);
  lua_setfield(L, -2, "color");
  lua_createtable(L, 3, 0);
  lua_pushinteger(L, spec.red2);
  lua_rawseti(L, -2, 1);
  lua_pushinteger(L, spec.green2);
  lua_rawseti(L, -2, 2);
  lua_pushinteger(L, spec.blue2);
  lua_rawseti(L, -2, 3);
  lua_setfield(L, -2, "color2");
  lua_pushinteger(L, static_cast<lua_Integer>(spec.period_ms));
  lua_setfield(L, -2, "period");
  lua_pushinteger(L, static_cast<lua_Integer>(spec.cycles));
  lua_setfield(L, -2, "cycles");
  lua_pushinteger(L, static_cast<lua_Integer>(spec.pixel));
  lua_setfield(L, -2, "pixel");
  lua_pushinteger(L, static_cast<lua_Integer>(spec.level));
  lua_setfield(L, -2, "level");
  lua_pushinteger(L, static_cast<lua_Integer>(elapsed_ms));
  lua_setfield(L, -2, "elapsed");
}

// led.play{kind=..., color={r,g,b}, period=ms, cycles=n, pixel=i, level=n}: an effect that
// runs while the app does its own work. It returns at once — the light is painted at the
// end of every turn — so an animation never stops the app that asked for it (docs/30).
int l_led_play(lua_State* L) {
  if (g_led_ops.play == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no light");
  }
  if (!lua_istable(L, 1)) {
    return push_nil_error(
        L, "E_INVAL",
        "play takes a table: {kind, color, period, cycles, pixel, level}");
  }
  LedEffectSpec spec = {};
  if (!read_effect_spec(L, 1, spec)) {
    return push_nil_error(L, "E_INVAL",
                          "no such effect (breathe, blink, sos, bounce), or a field that is not a number");
  }
  if (spec.pixel >= 0 && static_cast<uint32_t>(spec.pixel) >= g_led_ops.count()) {
    return push_nil_error(L, "E_INVAL", "this board has no light at that index");
  }
  if (!g_led_ops.play(spec)) {
    return push_nil_error(L, "E_BUSY",
                          "the light is busy with what the system is saying; wait for on_light");
  }
  lua_pushboolean(L, 1);
  return 1;
}

// led.stop([fade_ms]): give the light back. Zero is now; a number fades to black over that
// many milliseconds and then hands it back to the system, because a light that goes out in
// one frame in the middle of the night is a slap (docs/30).
int l_led_stop(lua_State* L) {
  if (g_led_ops.stop == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no light");
  }
  lua_Integer fade = luaL_optinteger(L, 1, 0);
  if (fade < 0) fade = 0;
  g_led_ops.stop(static_cast<uint32_t>(fade));
  lua_pushboolean(L, 1);
  return 1;
}

// led.effect(): what the app is playing and how long it has been playing, or nil. It is
// the question an app asks when it wants to know whether its effect finished by itself
// (docs/30).
int l_led_effect(lua_State* L) {
  if (g_led_ops.effect == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no light");
  }
  LedEffectSpec spec = {};
  uint32_t elapsed = 0;
  if (!g_led_ops.effect(spec, elapsed)) {
    lua_pushnil(L);
    return 1;
  }
  push_effect_spec(L, spec, elapsed);
  return 1;
}

// led.system([name[, effect|"default"]]): the four effects the board has of its own —boot,
// radio, alarm and error—, read and changed. With no name it answers all four; with a name
// and nothing else, that one; with a name and a table, it replaces it; with a name and the
// word "default", the maker's comes back (docs/30).
int l_led_system(lua_State* L) {
  if (g_led_ops.system_effect == nullptr || g_led_ops.trigger_name == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no light");
  }
  // No name: the four, in one table, so an app can look before it changes anything. The
  // list of names is the kernel's: this loop ends when the kernel runs out of them.
  if (lua_gettop(L) == 0) {
    lua_createtable(L, 0, 4);
    for (uint32_t i = 0;; ++i) {
      const uint8_t index = static_cast<uint8_t>(i);
      const char* name = g_led_ops.trigger_name(index);
      if (name == nullptr) break;
      LedEffectSpec spec = {};
      if (!g_led_ops.system_effect(index, spec)) break;
      push_effect_spec(L, spec, 0);
      lua_setfield(L, -2, name);
    }
    return 1;
  }
  if (lua_type(L, 1) != LUA_TSTRING) {
    return push_nil_error(L, "E_INVAL", "system takes a name: boot, radio, alarm or error");
  }
  uint8_t trigger = 0;
  if (!g_led_ops.trigger_from_name(lua_tostring(L, 1), trigger)) {
    return push_nil_error(L, "E_INVAL", "no such moment: boot, radio, alarm or error");
  }
  if (lua_gettop(L) == 1) {
    LedEffectSpec spec = {};
    if (!g_led_ops.system_effect(trigger, spec)) {
      return push_nil_error(L, "E_UNSUPPORTED", "this board has no light");
    }
    push_effect_spec(L, spec, 0);
    return 1;
  }
  if (lua_type(L, 2) == LUA_TSTRING && strcmp(lua_tostring(L, 2), "default") == 0) {
    g_led_ops.restore_system_effect(trigger);
    lua_pushboolean(L, 1);
    return 1;
  }
  if (!lua_istable(L, 2)) {
    return push_nil_error(L, "E_INVAL",
                          "the second argument is a table of the effect, or the word \"default\"");
  }
  LedEffectSpec spec = {};
  if (!read_effect_spec(L, 2, spec)) {
    return push_nil_error(L, "E_INVAL",
                          "no such effect (breathe, blink, sos, bounce), or a field that is not a number");
  }
  if (!g_led_ops.set_system_effect(trigger, spec)) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no light");
  }
  lua_pushboolean(L, 1);
  return 1;
}

// imu.read(): what the sensor says, in one call. Acceleration in g, turn rate in
// degrees per second, temperature in degrees Celsius: the three things the chip
// answers in the same burst, so they come together or not at all (docs/30).
int l_imu_read(lua_State* L) {
  if (g_imu_ops.read == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no inertial sensor");
  }
  float ax = 0;
  float ay = 0;
  float az = 0;
  float gx = 0;
  float gy = 0;
  float gz = 0;
  float celsius = 0;
  if (!g_imu_ops.read(ax, ay, az, gx, gy, gz, celsius)) {
    return push_nil_error(L, "E_IO", "the inertial sensor did not answer");
  }
  lua_createtable(L, 0, 7);
  lua_pushnumber(L, ax);
  lua_setfield(L, -2, "ax");
  lua_pushnumber(L, ay);
  lua_setfield(L, -2, "ay");
  lua_pushnumber(L, az);
  lua_setfield(L, -2, "az");
  lua_pushnumber(L, gx);
  lua_setfield(L, -2, "gx");
  lua_pushnumber(L, gy);
  lua_setfield(L, -2, "gy");
  lua_pushnumber(L, gz);
  lua_setfield(L, -2, "gz");
  lua_pushnumber(L, celsius);
  lua_setfield(L, -2, "celsius");
  return 1;
}

// env.read(): how warm it is and how wet, in one call. Two numbers and one answer: the chip measures
// them together, so they come together or not at all (docs/30).
int l_env_read(lua_State* L) {
  if (g_env_ops.read == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no ambient sensor");
  }
  float celsius = 0;
  float humidity = 0;
  if (!g_env_ops.read(celsius, humidity)) {
    return push_nil_error(L, "E_IO", "the ambient sensor did not answer");
  }
  lua_createtable(L, 0, 2);
  lua_pushnumber(L, celsius);
  lua_setfield(L, -2, "celsius");
  lua_pushnumber(L, humidity);
  lua_setfield(L, -2, "humidity");
  return 1;
}

// ---------------------------------------------------------------------------
// lora: **l'aire, i només mirar-lo** (0.192.49)
// ---------------------------------------------------------------------------
//
// **Un monitor i no un transmissor** (l'amo: *«Fer una app Monitor/Sniffer LORA. Ha de buscar senyals»*): les set portes
// d'aquí són les d'un receptor —on s'escolta, amb quina modulació, quanta energia hi ha i què ha arribat— i **no hi ha cap
// `send`**, ni n'hi haurà: una ràdio que només escolta es pot deixar encesa sense demanar permís a ningú i no pot tapar la veu
// de ningú altre (docs/30, docs/50).
//
// **Tots els números són enters i en les unitats del xip** perquè una pàgina no ha de fer aritmètica de coma flotant per pintar
// una barra: l'RSSI arriba en dècimes de dB (`-1043` és −104,3 dBm), l'SNR en quarts de dB, i la freqüència en hertz (docs/30).
int l_lora_caps(lua_State* L) {
  hal::LoraCaps caps;
  if (g_lora_ops.caps == nullptr || !g_lora_ops.caps(caps)) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no LoRa radio");
  }
  lua_createtable(L, 0, 6);
  lua_pushboolean(L, caps.present ? 1 : 0);
  lua_setfield(L, -2, "present");
  lua_pushinteger(L, static_cast<lua_Integer>(caps.min_hz));
  lua_setfield(L, -2, "min_hz");
  lua_pushinteger(L, static_cast<lua_Integer>(caps.max_hz));
  lua_setfield(L, -2, "max_hz");
  lua_pushinteger(L, static_cast<lua_Integer>(caps.min_sf));
  lua_setfield(L, -2, "min_sf");
  lua_pushinteger(L, static_cast<lua_Integer>(caps.max_sf));
  lua_setfield(L, -2, "max_sf");
  lua_pushstring(L, (caps.chip != nullptr) ? caps.chip : "LoRa");
  lua_setfield(L, -2, "chip");
  return 1;
}

int l_lora_begin(lua_State* L) {
  if (g_lora_ops.begin == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no LoRa radio");
  }
  if (!g_lora_ops.begin()) {
    return push_nil_error(L, "E_IO", (g_lora_ops.last_error != nullptr) ? g_lora_ops.last_error()
                                                                        : "the radio did not answer");
  }
  lua_pushboolean(L, 1);
  return 1;
}

// **`lora.frequency([hz])`: on s'escolta** (0.192.49). Sense argument contesta on és; amb un, hi va i torna el que el xip ha
// acceptat, que és el mateix o un error: una freqüència fora del rang del xip es refusa i no s'aproxima, perquè un espectre
// que diu 433 i escolta 470 és una pàgina que menteix (docs/30).
int l_lora_frequency(lua_State* L) {
  if (g_lora_ops.frequency_get == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no LoRa radio");
  }
  if (lua_isnoneornil(L, 1)) {
    lua_pushinteger(L, static_cast<lua_Integer>(g_lora_ops.frequency_get()));
    return 1;
  }
  const lua_Integer hz = luaL_checkinteger(L, 1);
  if (hz < 150000000 || hz > 960000000) {
    return push_nil_error(L, "E_INVAL", "the radio listens from 150 to 960 MHz");
  }
  if (g_lora_ops.frequency_set == nullptr || !g_lora_ops.frequency_set(static_cast<uint32_t>(hz))) {
    return push_nil_error(L, "E_IO", (g_lora_ops.last_error != nullptr) ? g_lora_ops.last_error()
                                                                        : "the radio did not take the frequency");
  }
  lua_pushinteger(L, static_cast<lua_Integer>(g_lora_ops.frequency_get()));
  return 1;
}

// **`lora.reception{...}`: amb què s'escolta** (0.192.49). Els camps són els del xip i tots són opcionals: el que no es diu es
// queda com estava, o sigui que una pàgina pot canviar l'SF sense repetir la freqüència (docs/30).
int l_lora_reception(lua_State* L) {
  if (g_lora_ops.reception_set == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no LoRa radio");
  }
  if (!lua_istable(L, 1)) {
    return push_nil_error(L, "E_INVAL", "reception takes a table of the settings to listen with");
  }
  hal::LoraConfig config;
  if (g_lora_ops.frequency_get != nullptr) config.hz = g_lora_ops.frequency_get();
  lua_getfield(L, 1, "sf");
  if (lua_isnumber(L, -1)) config.sf = static_cast<uint8_t>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, 1, "bw");
  if (lua_isnumber(L, -1)) config.bw = static_cast<uint8_t>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, 1, "cr");
  if (lua_isnumber(L, -1)) config.cr = static_cast<uint8_t>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, 1, "sync");
  if (lua_isnumber(L, -1)) config.sync = static_cast<uint8_t>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, 1, "preamble");
  if (lua_isnumber(L, -1)) config.preamble = static_cast<uint16_t>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, 1, "crc");
  if (!lua_isnil(L, -1)) config.crc = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  lua_getfield(L, 1, "invert");
  if (!lua_isnil(L, -1)) config.invert_iq = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  if (config.sf < 5 || config.sf > 12) {
    return push_nil_error(L, "E_INVAL", "the spreading factor goes from 5 to 12");
  }
  if (config.bw > 9) {
    return push_nil_error(L, "E_INVAL", "the bandwidth code goes from 0 to 9 (7 is 125 kHz)");
  }
  if (config.cr < 1 || config.cr > 4) {
    return push_nil_error(L, "E_INVAL", "the coding rate is 1 to 4, or 4/5 to 4/8");
  }
  if (!g_lora_ops.reception_set(config)) {
    return push_nil_error(L, "E_IO", (g_lora_ops.last_error != nullptr) ? g_lora_ops.last_error()
                                                                        : "the radio did not take the modulation");
  }
  lua_pushboolean(L, 1);
  return 1;
}

int l_lora_listen(lua_State* L) {
  if (g_lora_ops.listen == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no LoRa radio");
  }
  const lua_Integer timeout = luaL_optinteger(L, 1, 0);
  if (!g_lora_ops.listen(static_cast<uint32_t>((timeout > 0) ? timeout : 0))) {
    return push_nil_error(L, "E_IO", (g_lora_ops.last_error != nullptr) ? g_lora_ops.last_error()
                                                                        : "the radio did not start listening");
  }
  lua_pushboolean(L, 1);
  return 1;
}

int l_lora_sniff(lua_State* L) {
  if (g_lora_ops.sniff == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no LoRa radio");
  }
  if (!g_lora_ops.sniff()) {
    return push_nil_error(L, "E_IO", (g_lora_ops.last_error != nullptr) ? g_lora_ops.last_error()
                                                                        : "the radio did not start measuring");
  }
  lua_pushboolean(L, 1);
  return 1;
}

// **`lora.rssi()`: quanta energia hi ha al canal** (0.192.49), en dècimes de dB. És la lectura d'un escombrat i no una mitjana:
// qui vulgui una mitjana la fa amb les mostres que vulgui (docs/30).
int l_lora_rssi(lua_State* L) {
  if (g_lora_ops.rssi == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no LoRa radio");
  }
  lua_pushinteger(L, static_cast<lua_Integer>(g_lora_ops.rssi()));
  return 1;
}

// **`lora.poll()`: hi ha un paquet?** (0.192.49). `nil` vol dir «encara no» i no un error, i el que torna quan n'hi ha un és la
// taula del paquet amb el que el xip n'ha mesurat. **Un paquet amb el CRC malament també arriba** (`crc = false`), perquè una
// senyal que hi és i que no s'ha pogut llegir és informació i no un silenci (docs/30).
int l_lora_poll(lua_State* L) {
  if (g_lora_ops.poll == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no LoRa radio");
  }
  hal::LoraPacket packet;
  if (!g_lora_ops.poll(packet)) {
    lua_pushnil(L);
    return 1;
  }
  lua_createtable(L, 0, 8);
  lua_pushlstring(L, reinterpret_cast<const char*>(packet.data), packet.length);
  lua_setfield(L, -2, "data");
  lua_pushinteger(L, packet.length);
  lua_setfield(L, -2, "length");
  lua_pushinteger(L, packet.rssi_dbm10);
  lua_setfield(L, -2, "rssi");
  lua_pushinteger(L, packet.snr_db4);
  lua_setfield(L, -2, "snr");
  lua_pushinteger(L, packet.freq_offset_hz);
  lua_setfield(L, -2, "offset_hz");
  lua_pushinteger(L, static_cast<lua_Integer>(packet.hz));
  lua_setfield(L, -2, "hz");
  lua_pushinteger(L, static_cast<lua_Integer>(packet.ms));
  lua_setfield(L, -2, "ms");
  lua_pushboolean(L, packet.crc_ok ? 1 : 0);
  lua_setfield(L, -2, "crc");
  return 1;
}

int l_lora_idle(lua_State* L) {
  if (g_lora_ops.idle == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no LoRa radio");
  }
  lua_pushboolean(L, g_lora_ops.idle() ? 1 : 0);
  return 1;
}

int l_lora_sleep(lua_State* L) {
  if (g_lora_ops.sleep == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no LoRa radio");
  }
  lua_pushboolean(L, g_lora_ops.sleep() ? 1 : 0);
  return 1;
}

// ---------------------------------------------------------------------------
// sensors: **el maquinari dit en magnituds i no en xips** (0.190.157)
// ---------------------------------------------------------------------------

// **L'àmbit mana el mòdul i el xip no surt enlloc** (0.190.158, l'amo: *«el que vull es que sigui així precisament! no son àlies,
// son la base de com llistar i obtenir tots els dispositius ... env.temperature[0].read ... imu ... power»*). `env`, `power` i `imu`
// no són xips: són **l'aire, l'energia i el moviment**, o sigui que una app que diu `power.temperature[0]` no sap de maquinari — sap
// de què va el número. I `sensors` és l'agregador: els mateixos objectes, ordenats per magnitud i sense àmbit, per a la pàgina que
// vol «tot el que aquesta placa mesura» (docs/30, docs/50).
enum class SensorSource : uint8_t {
  kEnvAir,
  kEnvHumidity,
  kImuChip,
  kBatteryTemperature,
  kBatteryVoltage,
  kBatteryCurrent,
  kBatteryCharge,
  kChargerVbus,
  kChargerVsys,
  kChargerCurrent,
};

struct SensorDef {
  uint8_t id;
  const char* scope;  // "env", "power" or "imu": the module it hangs from
  const char* kind;   // "temperature", "voltage", ...: the table it goes in
  const char* where;  // "air", "cell", "chip", "input", "system"
  const char* unit;
  SensorSource source;
};

// **L'adreça d'un sensor no depèn de la placa** (0.190.157, l'amo: *«sempre es passa un <id> (imagina que tenim 3 termòmetres, un
// ambiental, un de la batería de la placa i l'altre de la cpu)»*): the three thermometers of a Paper are 0, 1 and 2 whatever chip is
// under each one; el que canvia entre plaques és **quins hi són**, no on són (docs/30, docs/50).
const SensorDef kSensorDefs[] = {
    {0, "env", "temperature", "air", "C", SensorSource::kEnvAir},
    {1, "power", "temperature", "cell", "C", SensorSource::kBatteryTemperature},
    {2, "imu", "temperature", "chip", "C", SensorSource::kImuChip},
    {3, "env", "humidity", "air", "%", SensorSource::kEnvHumidity},
    {4, "power", "voltage", "cell", "V", SensorSource::kBatteryVoltage},
    {5, "power", "voltage", "input", "V", SensorSource::kChargerVbus},
    {6, "power", "voltage", "system", "V", SensorSource::kChargerVsys},
    {7, "power", "current", "cell", "mA", SensorSource::kBatteryCurrent},
    {8, "power", "current", "input", "mA", SensorSource::kChargerCurrent},
    {9, "power", "charge", "cell", "%", SensorSource::kBatteryCharge},
};

const uint32_t kSensorCount = sizeof(kSensorDefs) / sizeof(kSensorDefs[0]);

// **Les magnituds que tenen taula encara que estiguin buides**: `env.pressure` és una llista de zero sensors en aquesta placa i no
// un `nil`, o sigui que `#env.pressure` contesta sense haver de preguntar abans si existeix (docs/30).
const char* const kSensorKinds[] = {"temperature", "humidity", "pressure", "voltage", "current", "charge"};
const uint32_t kSensorKindCount = sizeof(kSensorKinds) / sizeof(kSensorKinds[0]);

bool the_board_has(const hal::Capability bit) { return hal::platform().caps().has(bit); }

bool a_sensor_exists(uint8_t id) {
  switch (id) {
    case 0:
    case 3:
      return g_env_ops.read != nullptr && the_board_has(hal::kCapEnv);
    case 2:
      return g_imu_ops.read != nullptr && the_board_has(hal::kCapImu);
    case 1:
    case 4:
    case 7:
    case 9:
      return g_power_ops.battery != nullptr && g_power_ops.battery_detail != nullptr &&
             the_board_has(hal::kCapBattery);
    case 5:
    case 6:
    case 8:
      return g_power_ops.charger_detail != nullptr;
    default:
      return false;
  }
}

// **El valor, en la unitat que la taula diu**: volts, graus i mil·liamperes creuen la frontera ja convertits, i el que el xip no
// diu no es converteix en un zero. False vol dir «aquest sensor no contesta ara» (docs/30, docs/50).
bool read_a_sensor(uint8_t id, double& value) {
  switch (kSensorDefs[id].source) {
    case SensorSource::kEnvAir:
    case SensorSource::kEnvHumidity: {
      float celsius = 0;
      float humidity = 0;
      if (g_env_ops.read == nullptr || !g_env_ops.read(celsius, humidity)) return false;
      value = (kSensorDefs[id].source == SensorSource::kEnvAir) ? celsius : humidity;
      return true;
    }
    case SensorSource::kImuChip: {
      float ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0, celsius = 0;
      if (g_imu_ops.read == nullptr || !g_imu_ops.read(ax, ay, az, gx, gy, gz, celsius)) return false;
      value = celsius;
      return true;
    }
    case SensorSource::kBatteryVoltage:
    case SensorSource::kBatteryCharge: {
      int mv = 0, pct = 0;
      bool charging = false;
      if (g_power_ops.battery == nullptr || !g_power_ops.battery(mv, pct, charging)) return false;
      value = (kSensorDefs[id].source == SensorSource::kBatteryVoltage) ? static_cast<double>(mv) / 1000.0
                                                                       : static_cast<double>(pct);
      return true;
    }
    case SensorSource::kBatteryTemperature:
    case SensorSource::kBatteryCurrent: {
      hal::IPower::BatteryDetail detail;
      if (g_power_ops.battery_detail == nullptr || !g_power_ops.battery_detail(detail)) return false;
      if (kSensorDefs[id].source == SensorSource::kBatteryTemperature) {
        if (detail.temp_c10 == hal::IPower::kUnknown) return false;
        value = static_cast<double>(detail.temp_c10) / 10.0;
      } else {
        if (detail.current_ma == hal::IPower::kUnknown) return false;
        value = static_cast<double>(detail.current_ma);
      }
      return true;
    }
    case SensorSource::kChargerVbus:
    case SensorSource::kChargerVsys:
    case SensorSource::kChargerCurrent: {
      hal::IPower::ChargerDetail charger;
      if (g_power_ops.charger_detail == nullptr || !g_power_ops.charger_detail(charger)) return false;
      if (kSensorDefs[id].source == SensorSource::kChargerVbus) {
        if (charger.vbus_mv == hal::IPower::kUnknown || charger.vbus_mv <= 0) return false;
        value = static_cast<double>(charger.vbus_mv) / 1000.0;
      } else if (kSensorDefs[id].source == SensorSource::kChargerVsys) {
        if (charger.vsys_mv == hal::IPower::kUnknown) return false;
        value = static_cast<double>(charger.vsys_mv) / 1000.0;
      } else {
        if (charger.ichgr_ma == hal::IPower::kUnknown) return false;
        value = static_cast<double>(charger.ichgr_ma);
      }
      return true;
    }
    default:
      return false;
  }
}

// **Un sensor és una cosa amb un `read`** (0.190.158): `env.temperature[0].read()` llegeix ara i no guarda res —el que vulgui
// desar el número el desa ell—, i l'id viatja a l'upvalue perquè la crida valgui igual amb punt i amb dos punts, que és com ho
// escriu l'amo (docs/30).
int l_sensor_read(lua_State* L) {
  const uint8_t id = static_cast<uint8_t>(lua_tointeger(L, lua_upvalueindex(1)));
  double value = 0;
  if (!read_a_sensor(id, value)) {
    return push_nil_error(L, "E_IO", "the sensor did not answer");
  }
  lua_pushnumber(L, value);
  return 1;
}

void push_sensor_object(lua_State* L, const SensorDef& one) {
  lua_createtable(L, 0, 4);
  lua_pushinteger(L, static_cast<lua_Integer>(one.id));
  lua_setfield(L, -2, "id");
  lua_pushstring(L, one.kind);
  lua_setfield(L, -2, "kind");
  lua_pushstring(L, one.where);
  lua_setfield(L, -2, "where");
  lua_pushstring(L, one.unit);
  lua_setfield(L, -2, "unit");
  lua_pushinteger(L, static_cast<lua_Integer>(one.id));
  lua_pushcclosure(L, l_sensor_read, 1);
  lua_setfield(L, -2, "read");
}

int l_sensors_list(lua_State* L) {
  lua_createtable(L, static_cast<int>(kSensorCount), 0);
  int at = 0;
  for (uint32_t i = 0; i < kSensorCount; ++i) {
    if (!a_sensor_exists(kSensorDefs[i].id)) continue;
    push_sensor_object(L, kSensorDefs[i]);
    lua_rawseti(L, -2, ++at);
  }
  return 1;
}

int l_sensors_count(lua_State* L) {
  const char* kind = lua_isstring(L, 1) ? lua_tostring(L, 1) : nullptr;
  lua_Integer count = 0;
  for (uint32_t i = 0; i < kSensorCount; ++i) {
    if (!a_sensor_exists(kSensorDefs[i].id)) continue;
    if (kind != nullptr && strcmp(kSensorDefs[i].kind, kind) != 0) continue;
    ++count;
  }
  lua_pushinteger(L, count);
  return 1;
}

// **I això és el que fa que env, power i imu tinguin els seus** (0.190.158): each sensor appears **twice** —in the table of its
// magnitude inside its scope (`power.temperature[0]`) and in the aggregator (`sensors.temperature[1]`)— and the object is the same
// one, o sigui que una app no ha de triar quina de les dues portes fer servir (docs/30).
void install_sensors(lua_State* L) {
  lua_newtable(L);
  for (uint32_t k = 0; k < kSensorKindCount; ++k) {
    lua_newtable(L);
    lua_setfield(L, -2, kSensorKinds[k]);
  }
  static const char* const kScopes[] = {"env", "power", "imu"};
  for (uint32_t i = 0; i < 3; ++i) {
    lua_getglobal(L, kScopes[i]);
    if (lua_istable(L, -1)) {
      for (uint32_t k = 0; k < kSensorKindCount; ++k) {
        lua_newtable(L);
        lua_setfield(L, -2, kSensorKinds[k]);
      }
    }
    lua_pop(L, 1);
  }

  uint32_t in_kind[16] = {0};
  for (uint32_t i = 0; i < kSensorCount; ++i) {
    const SensorDef& one = kSensorDefs[i];
    if (!a_sensor_exists(one.id)) continue;
    uint32_t kind_index = kSensorKindCount;
    for (uint32_t k = 0; k < kSensorKindCount; ++k) {
      if (strcmp(kSensorKinds[k], one.kind) == 0) kind_index = k;
    }
    if (kind_index >= kSensorKindCount) continue;

    // The scope's own table; the aggregator is the table at the bottom of the stack.
    lua_getglobal(L, one.scope);
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      continue;
    }
    lua_getfield(L, -1, kSensorKinds[kind_index]);
    const int at = static_cast<int>(lua_rawlen(L, -1)) + 1;
    push_sensor_object(L, one);
    lua_rawseti(L, -2, at);
    lua_pop(L, 2);

    lua_getfield(L, -1, kSensorKinds[kind_index]);
    push_sensor_object(L, one);
    lua_rawseti(L, -2, static_cast<lua_Integer>(++in_kind[kind_index]));
    lua_pop(L, 1);
  }

  set_function(L, "list", l_sensors_list);
  set_function(L, "count", l_sensors_count);
  lua_setglobal(L, "sensors");
}

// How long an app waits for a press by default. Twenty seconds is a person looking
// at the board; an app that wants to wait for ever asks for a bigger number (docs/30).
const lua_Integer kButtonWaitDefaultMs = 20000;

// button.wait([ms]): a promise of `{kind = "press"|"long", held_ms}`. The press is
// delivered between turns, like an alarm, so the app is not frozen while it waits
// (docs/30).
// button.count(): how many keys this board has. Zero is a board with none, and
// it is not an error: an app asks before it shows three of them (docs/30).
// **`button.software()`**: si la caixa té un botó de sota el vidre (0.190.110). Cert vol dir que el sistema en fa una tecla —la darrera de
// `button.list()`— quan *Use software button* és encès, i una pàgina ho pregunta abans d'ensenyar-ne la fila (docs/30, docs/50).
int l_button_software(lua_State* L) {
  lua_pushboolean(L, (g_button_ops.software_button != nullptr && g_button_ops.software_button()) ? 1 : 0);
  return 1;
}

int l_button_count(lua_State* L) {
  if (g_button_ops.count == nullptr) {
    lua_pushinteger(L, 0);
    return 1;
  }
  lua_pushinteger(L, static_cast<lua_Integer>(g_button_ops.count()));
  return 1;
}

// One description as a Lua table, used by info and by list (docs/30).
void push_key_info(lua_State* L, const KeyInfo& info) {
  lua_createtable(L, 0, 5);
  lua_pushinteger(L, static_cast<lua_Integer>(info.key));
  lua_setfield(L, -2, "key");
  lua_pushstring(L, (info.face != nullptr) ? info.face : "?");
  lua_setfield(L, -2, "face");
  lua_pushstring(L, (info.spot != nullptr) ? info.spot : "?");
  lua_setfield(L, -2, "spot");
  lua_pushstring(L, (info.kind != nullptr) ? info.kind : "?");
  lua_setfield(L, -2, "kind");
  lua_pushinteger(L, static_cast<lua_Integer>(info.group));
  lua_setfield(L, -2, "group");
}

// button.info(key): where that key is on the board and how it is built, so an app
// can decide by position instead of by number (docs/30).
int l_button_info(lua_State* L) {
  if (g_button_ops.info == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no button");
  }
  const lua_Integer key = luaL_optinteger(L, 1, 0);
  if (key < 0) return push_nil_error(L, "E_INVAL", "a key index is not negative");
  KeyInfo info = {};
  if (!g_button_ops.info(static_cast<uint32_t>(key), info)) {
    return push_nil_error(L, "E_INVAL", "this board has no such key");
  }
  push_key_info(L, info);
  return 1;
}

// button.list(): every key, described as info does. This is what an app reads
// once at the start to know what it has in its hands (docs/30).
int l_button_list(lua_State* L) {
  if (g_button_ops.count == nullptr || g_button_ops.info == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no button");
  }
  const uint32_t count = g_button_ops.count();
  lua_createtable(L, static_cast<int>(count), 0);
  for (uint32_t key = 0; key < count; ++key) {
    KeyInfo info = {};
    if (!g_button_ops.info(key, info)) continue;
    push_key_info(L, info);
    lua_rawseti(L, -2, static_cast<lua_Integer>(key) + 1);
  }
  return 1;
}

// button.down(key): is that key down right now. It is what an app that repeats a
// step while a key is held needs, and it reads the board at the moment it asks
// (docs/30).
int l_button_down(lua_State* L) {
  if (g_button_ops.down == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no button");
  }
  const lua_Integer key = luaL_optinteger(L, 1, 0);
  if (key < 0) return push_nil_error(L, "E_INVAL", "a key index is not negative");
  lua_pushboolean(L, g_button_ops.down(static_cast<uint32_t>(key)) ? 1 : 0);
  return 1;
}

// button.held(key): how long it has been down, in ms, or 0 when it is not.
int l_button_held(lua_State* L) {
  if (g_button_ops.held == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no button");
  }
  const lua_Integer key = luaL_optinteger(L, 1, 0);
  if (key < 0) return push_nil_error(L, "E_INVAL", "a key index is not negative");
  lua_pushinteger(L, static_cast<lua_Integer>(g_button_ops.held(static_cast<uint32_t>(key))));
  return 1;
}

// **The event as the app's function hears it** (0.180.0): a table, because an event has a kind, a key and where that
// key is, and `spot` is the name the board gives it —`left`, `centre`, `right`— so an app gives a function to a place
// and not to a number (docs/30, docs/50).
int push_button_event(lua_State* L, const char* kind, uint32_t key, int64_t held_ms) {
  lua_createtable(L, 0, 4);
  lua_pushstring(L, (kind != nullptr) ? kind : "press");
  lua_setfield(L, -2, "kind");
  lua_pushinteger(L, static_cast<lua_Integer>(key));
  lua_setfield(L, -2, "key");
  lua_pushinteger(L, static_cast<lua_Integer>(held_ms));
  lua_setfield(L, -2, "held_ms");
  KeyInfo info = {};
  if (g_button_ops.info != nullptr && g_button_ops.info(key, info)) {
    lua_pushstring(L, info.spot);
    lua_setfield(L, -2, "spot");
  }
  return 1;
}

// **The listener the kernel keeps in its queue** (0.180.0): the interpreter's own function pointer, with the app's
// function in a reference of the registry. The kernel knows nothing of Lua and this is the one place the two meet, the
// same road `ui_button_event` and `call_button` already take (docs/10, docs/30).
void app_button_listener(const char* kind, uint32_t key, int64_t held_ms, void* owner) {
  (void)owner;
  lua_State* L = g_state;
  if (L == nullptr) return;
  const int top = lua_gettop(L);
  lua_getfield(L, LUA_REGISTRYINDEX, kButtonListenerKey);
  if (!lua_isfunction(L, -1)) {
    lua_settop(L, top);
    return;
  }
  push_button_event(L, kind, key, held_ms);
  if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
    con_text("button listener: ");
    const char* message = lua_tostring(L, -1);
    con_text((message != nullptr) ? message : "(no message)");
    con_text("\r\n");
  }
  lua_settop(L, top);
}

// button.subscribe(fn, [mode]): **l'app entra a la cua dels botons** (0.180.0). `fn` hears every event as a table and
// the mode is `'cascade'` —the default, called with the others— or `'exclusive'`, which means **the others do not hear
// the event until this one is given back** (docs/30). It answers a handle, or nil and an error; an app has **one** place
// in the queue and a second call replaces the function and answers the same handle.
int l_button_subscribe(lua_State* L) {
  if (g_button_ops.subscribe == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no button");
  }
  if (lua_gettop(L) < 1 || !lua_isfunction(L, 1)) {
    return push_nil_error(L, "E_INVAL", "a subscription is a function");
  }
  uint8_t mode = 0;
  if (lua_isstring(L, 2)) {
    const char* asked = lua_tostring(L, 2);
    if (asked != nullptr && strcmp(asked, "exclusive") == 0) {
      mode = 1;
    } else if (asked != nullptr && strcmp(asked, "cascade") != 0) {
      return push_nil_error(L, "E_INVAL", "a subscription is cascade or exclusive");
    }
  }
  lua_pushvalue(L, 1);
  lua_setfield(L, LUA_REGISTRYINDEX, kButtonListenerKey);
  if (g_app_button_handle == 0) {
    g_app_button_handle = g_button_ops.subscribe(&app_button_listener, nullptr, mode);
    if (g_app_button_handle == 0) {
      lua_pushnil(L);
      lua_setfield(L, LUA_REGISTRYINDEX, kButtonListenerKey);
      return push_nil_error(L, "E_NOMEM", "the queue of the keys is full");
    }
  }
  lua_pushinteger(L, static_cast<lua_Integer>(g_app_button_handle));
  return 1;
}

// button.unsubscribe([handle]): **l'app surt de la cua**. With no argument it gives back its own place, and the number
// is checked so an app cannot take another owner's subscription away (docs/30).
int l_button_unsubscribe(lua_State* L) {
  if (g_button_ops.unsubscribe == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no button");
  }
  const lua_Integer asked = luaL_optinteger(L, 1, static_cast<lua_Integer>(g_app_button_handle));
  if (asked <= 0 || static_cast<uint16_t>(asked) != g_app_button_handle) {
    return push_nil_error(L, "E_INVAL", "that handle is not this app's subscription");
  }
  g_button_ops.unsubscribe(g_app_button_handle);
  g_app_button_handle = 0;
  lua_pushnil(L);
  lua_setfield(L, LUA_REGISTRYINDEX, kButtonListenerKey);
  lua_pushboolean(L, 1);
  return 1;
}

// button.mode(): the mode of the app's subscription, or nil when it is not in the queue. The shell and a test use it to
// see the state without taking it (docs/30).
int l_button_mode(lua_State* L) {
  if (g_app_button_handle == 0) {
    lua_pushnil(L);
    return 1;
  }
  lua_pushinteger(L, static_cast<lua_Integer>(g_app_button_handle));
  return 1;
}

// The five points a frame can hold, which is what the chips here report and what the kernel keeps
// (docs/50). It is not a copy of a HAL constant on purpose: the interpreter asks and the kernel
// clamps.
const uint8_t kTouchPointsInFrame = 5;

UIOps g_ui_ops = {};  // and ui.*
BookOps g_book_ops = {};  // and epub.*
// **Les imatges dels moments del sistema** (0.183.0): the interpreter does not know the kernel, so the four calls it needs
// to put one on the glass —how many moments, their words, the picture and its failure— come in through this table
// (docs/20, docs/30, docs/50).
SplashOps g_splash_ops = {};
AdminOps g_admin_ops = {};

TouchOps g_touch_ops = {};  // and touch.

// `set_touch_ops` is defined further down, **outside** this file's anonymous namespace: the kernel
// links to it (docs/30).

// touch.caps(): how big the glass is and how many fingers it reports at once (docs/30).
int l_touch_caps(lua_State* L) {
  TouchInfo info = {};
  if (g_touch_ops.caps == nullptr || !g_touch_ops.caps(info)) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no touch");
  }
  lua_createtable(L, 0, 3);
  lua_pushinteger(L, info.max_points);
  lua_setfield(L, -2, "points");
  lua_pushinteger(L, info.width);
  lua_setfield(L, -2, "width");
  lua_pushinteger(L, info.height);
  lua_setfield(L, -2, "height");
  return 1;
}

// touch.read(): the fingers that are down, as the kernel saw them in its last turn. An empty table
// and not nil when nobody is touching, so a loop can walk it without asking first (docs/30).
int l_touch_read(lua_State* L) {
  TouchPointInfo points[kTouchPointsInFrame] = {};
  if (g_touch_ops.points == nullptr || g_touch_ops.caps == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no touch");
  }
  TouchInfo info = {};
  if (!g_touch_ops.caps(info)) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no touch");
  }
  const uint8_t count = g_touch_ops.points(points, kTouchPointsInFrame);
  lua_createtable(L, count, 0);
  for (uint8_t i = 0; i < count; ++i) {
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, points[i].id);
    lua_setfield(L, -2, "id");
    lua_pushinteger(L, points[i].x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, points[i].y);
    lua_setfield(L, -2, "y");
    lua_pushinteger(L, points[i].size);
    lua_setfield(L, -2, "size");
    lua_rawseti(L, -2, static_cast<lua_Integer>(i) + 1);
  }
  return 1;
}

// touch.gesture(): the last thing the kernel made out of the stream, or nil. It is **taken once**:
// asking twice about one tap would make a loop act on it twice (docs/30).
int l_touch_gesture(lua_State* L) {
  const char* kind = nullptr;
  uint16_t x = 0;
  uint16_t y = 0;
  int16_t dx = 0;
  int16_t dy = 0;
  int16_t spread = 0;
  int32_t held_ms = 0;
  uint8_t fingers = 0;
  if (g_touch_ops.gesture == nullptr ||
      !g_touch_ops.gesture(&kind, x, y, dx, dy, spread, held_ms, fingers)) {
    lua_pushnil(L);
    return 1;
  }
  lua_createtable(L, 0, 7);
  lua_pushstring(L, kind);
  lua_setfield(L, -2, "kind");
  lua_pushinteger(L, x);
  lua_setfield(L, -2, "x");
  lua_pushinteger(L, y);
  lua_setfield(L, -2, "y");
  lua_pushinteger(L, dx);
  lua_setfield(L, -2, "dx");
  lua_pushinteger(L, dy);
  lua_setfield(L, -2, "dy");
  // How much further apart (positive) or closer (negative) two fingers ended, for a pinch (docs/30).
  lua_pushinteger(L, spread);
  lua_setfield(L, -2, "spread");
  lua_pushinteger(L, held_ms);
  lua_setfield(L, -2, "held_ms");
  lua_pushinteger(L, fingers);
  lua_setfield(L, -2, "fingers");
  return 1;
}

// **El número del parpelleig és un ajust i no un cursor** (0.189.34, i el forat de la 0.189.112): vuit-cents cinquanta
// mil·lisegons per parpelleig, el període del bloc de tinta que diu on escriu una persona. **Viu fora del guard de la
// pàgina** perquè el que hi ha aquí és el número i no el dibuix: `cfg cursor delay` el llegeix, l'escriu i el desa a la
// memòria de claus de **qualsevol** placa —la comanda és del nucli i no de la pàgina—, i una placa sense vidre no té cap
// cursor que parpellegi (el bloc, el rellotge i `caret_start` són dins `#if LUNOK_GLASS`, unes línies més avall). Quatre
// paraules i catorze bytes de `.bss` en una placa que no els fa servir, contra una comanda que hauria de desaparèixer o
// dir una mentida; i el valor de sèrie i els límits són un sol lloc, que és el que fa que el prompt i la pàgina no puguin
// dir números diferents (docs/30, docs/50, docs/75). **El va trencar** `c8cb220`, que va deixar els cinc accessors fora
// del guard amb les seves variables a dins: les quatre plaques sense vidre no enllaçaven —`'g_caret_blink_ms' was not
// declared in this scope`— i el CI no ho va veure perquè s'aturava abans, als tests.
const int64_t kCaretBlinkDefaultMs = 850;
const int64_t kCaretBlinkMinMs = 100;
const int64_t kCaretBlinkMaxMs = 5000;
int64_t g_caret_blink_ms = kCaretBlinkDefaultMs;

// **Here begins the page, and a board with no glass has none of it** (0.179.9): `ui.*` and everything under it —the
// canvases and the controls with their states, the strips, the lists, the grids, the cards, the finger and the dial, the
// keyboards and the faces— is one system and one `LUNOK_GLASS` takes all of it out of the build. `sys`, `fs`, `net`,
// `button` and the rest are above and below these lines and stay (docs/30, docs/50, docs/75).

// ui.caps(): the size of the app's view, how many levels it has and where it sits on the glass
// (docs/30).
int l_ui_caps(lua_State* L) {
  UIInfo info = {};
  if (g_ui_ops.caps == nullptr || !g_ui_ops.caps(info)) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no display");
  }
  lua_createtable(L, 0, 5);
  lua_pushinteger(L, info.width);
  lua_setfield(L, -2, "width");
  lua_pushinteger(L, info.height);
  lua_setfield(L, -2, "height");
  lua_pushinteger(L, info.levels);
  lua_setfield(L, -2, "levels");
  lua_pushinteger(L, info.x);
  lua_setfield(L, -2, "x");
  lua_pushinteger(L, info.y);
  lua_setfield(L, -2, "y");
  return 1;
}

// ui.clear(level, [mode]): the whole view in one level, and the quality it will be painted with.
int l_ui_clear(lua_State* L) {
  if (g_ui_ops.clear == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no display");
  }
  const lua_Integer level = luaL_optinteger(L, 1, 15);
  const char* mode = luaL_optstring(L, 2, nullptr);
  if (level < 0 || level > 15) return push_nil_error(L, "E_INVAL", "a level is 0 to 15");
  if (!g_ui_ops.clear(static_cast<uint8_t>(level), mode)) {
    return push_nil_error(L, "E_INVAL", "not a quality of this display");
  }
  lua_pushboolean(L, 1);
  return 1;
}

// ui.rect(x, y, w, h, [level], [mode]): a rectangle, and **the quality this rectangle wants** — a line
// of text under a photo is a different paint from the photo (docs/30).
int l_ui_rect(lua_State* L) {
  if (g_ui_ops.rect == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no display");
  }
  const lua_Integer x = luaL_checkinteger(L, 1);
  const lua_Integer y = luaL_checkinteger(L, 2);
  const lua_Integer w = luaL_checkinteger(L, 3);
  const lua_Integer h = luaL_checkinteger(L, 4);
  const lua_Integer level = luaL_optinteger(L, 5, 0);
  const char* mode = luaL_optstring(L, 6, nullptr);
  if (w < 0 || h < 0 || level < 0 || level > 15) {
    return push_nil_error(L, "E_INVAL", "a rectangle is a size and a level of 0 to 15");
  }
  if (!g_ui_ops.rect(static_cast<uint16_t>(x), static_cast<uint16_t>(y), static_cast<uint16_t>(w),
                     static_cast<uint16_t>(h), static_cast<uint8_t>(level), mode)) {
    return push_nil_error(L, "E_INVAL", "not a quality of this display");
  }
  lua_pushboolean(L, 1);
  return 1;
}

// ui.image(x, y, path[, w[, h[, mode]]]): **a picture on the page**, read from a file of the volume (docs/30).
// The size is the picture's unless the app gives one, and the quality is the one of the paint. The answer says
// why when it is false, because a picture that did not appear is a question a person asks at once.
int l_ui_image(lua_State* L) {
  if (g_ui_ops.image == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no display");
  }
  const lua_Integer x = luaL_checkinteger(L, 1);
  const lua_Integer y = luaL_checkinteger(L, 2);
  const char* path = luaL_checkstring(L, 3);
  const lua_Integer w = luaL_optinteger(L, 4, 0);
  const lua_Integer h = luaL_optinteger(L, 5, 0);
  const char* mode = luaL_optstring(L, 6, nullptr);
  // **The mask of the page's own picture** (0.169.0): the seventh argument, and the same meaning it has in `canvas:image`.
  const bool mask = (lua_gettop(L) >= 7) && (lua_toboolean(L, 7) != 0);
  if (w < 0 || h < 0 || w > 4096 || h > 4096) {
    return push_nil_error(L, "E_INVAL", "a picture is up to 4096 pixels a side");
  }
  const char* code = nullptr;
  const char* reason = nullptr;
  if (!g_ui_ops.image(0, static_cast<uint16_t>(x), static_cast<uint16_t>(y), static_cast<uint16_t>(w),
                      static_cast<uint16_t>(h), path, mode, mask, code, reason)) {
    return push_nil_error(L, (code != nullptr) ? code : "E_INVAL",
                          (reason != nullptr) ? reason : "the picture was not drawn");
  }
  lua_pushboolean(L, 1);
  return 1;
}

// ui.image_info(path): `{format, width, height}` — what a page needs to lay a picture out before drawing it
// (docs/30). The format is the four letters of the family ("PNG", "JPEG", "BMP", "GIF", "ICO", "PPM"...).
int l_ui_image_info(lua_State* L) {
  if (g_ui_ops.image_info == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no display");
  }
  const char* path = luaL_checkstring(L, 1);
  char format[8] = {0};
  uint16_t width = 0;
  uint16_t height = 0;
  const char* code = nullptr;
  const char* reason = nullptr;
  if (!g_ui_ops.image_info(path, format, sizeof(format), width, height, code, reason)) {
    return push_nil_error(L, (code != nullptr) ? code : "E_INVAL",
                          (reason != nullptr) ? reason : "the picture could not be read");
  }
  lua_createtable(L, 0, 3);
  lua_pushstring(L, format);
  lua_setfield(L, -2, "format");
  lua_pushinteger(L, width);
  lua_setfield(L, -2, "width");
  lua_pushinteger(L, height);
  lua_setfield(L, -2, "height");
  return 1;
}

int l_ui_pixel(lua_State* L) {
  if (g_ui_ops.pixel == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no display");
  }
  const lua_Integer x = luaL_checkinteger(L, 1);
  const lua_Integer y = luaL_checkinteger(L, 2);
  const lua_Integer level = luaL_optinteger(L, 3, 0);
  const char* mode = luaL_optstring(L, 4, nullptr);
  if (level < 0 || level > 15) return push_nil_error(L, "E_INVAL", "a level is 0 to 15");
  if (!g_ui_ops.pixel(static_cast<uint16_t>(x), static_cast<uint16_t>(y),
                      static_cast<uint8_t>(level), mode)) {
    return push_nil_error(L, "E_INVAL", "not a quality of this display");
  }
  lua_pushboolean(L, 1);
  return 1;
}

// **`ui.invert(x, y, w, h [, mode])`**: a box of the page turned over, ink to paper and paper to ink (0.178.0). It is the
// drawing that says «this is the one that will answer» on this glass —a selected cell is a black block and not a grey
// one— and it is a primitive because what is turned over is what is already there (docs/30, docs/92).
int l_ui_invert(lua_State* L) {
  if (g_ui_ops.invert == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no display");
  }
  const lua_Integer x = luaL_checkinteger(L, 1);
  const lua_Integer y = luaL_checkinteger(L, 2);
  const lua_Integer w = luaL_checkinteger(L, 3);
  const lua_Integer h = luaL_checkinteger(L, 4);
  const char* mode = luaL_optstring(L, 5, nullptr);
  if (w < 0 || h < 0) return push_nil_error(L, "E_INVAL", "a box is a width and a height");
  if (!g_ui_ops.invert(static_cast<uint16_t>(x), static_cast<uint16_t>(y), static_cast<uint16_t>(w),
                       static_cast<uint16_t>(h), mode)) {
    return push_nil_error(L, "E_INVAL", "not a quality of this display");
  }
  lua_pushboolean(L, 1);
  return 1;
}

// The view as a control, defined further down with the rest of the canvas object: `present` asks it to
// draw before composing (docs/90).
int l_ui_root(lua_State* L);
int l_ui_focus(lua_State* L);

// **A present of the app is a present of the block** (0.92.0): a focus or a key sets the caret and marks this counter;
// when the app presents before the kernel gets its turn, the box is already on the glass with the block on and the
// system does not paint it again with a `du` that would leave the ghost of the text it has just cleaned (docs/30).
uint32_t g_ui_present_count = 0;
uint32_t g_caret_present_mark = 0;

// ui.present([mode]): the system composes what the app drew onto the board's surface and asks for it to
// be painted. **Without a mode each region is painted with the quality it was drawn with**; with one,
// that quality is used for everything pending, which is what a page that must look uniform wants
// (docs/30, docs/50).
// **The ring comes back on the turn after a card goes** (0.172.0): the focus machinery lives further down this file —it is
// written where the walks are— and the present is the one place every app passes through after closing its card (docs/30).
void ui_restore_the_ring_if_a_card_went(lua_State* L);

int l_ui_present(lua_State* L) {
  if (g_ui_ops.present == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no display");
  }
  // **A card that has gone gives the ring back in the present that follows** (0.172.0): the walk that notices the card is the
  // same one that hands the ring back, and it only runs when a key or a finger asks for the stops —so an app that closed its
  // card and then only waits would keep a page with no ring anywhere—. It is one check per present and one walk once, and it
  // is the turn where the card is not on the page any more (docs/30, docs/90).
  ui_restore_the_ring_if_a_card_went(L);
  // **What changed is drawn first**: the view draws itself and asks its children, and a control with
  // nothing changed is not asked to draw at all. Then the tree is composed and the glass asked for the
  // paint, so an app that presents on every turn pays only for what it touched (docs/30, docs/90).
  if (l_ui_root(L) == 1) {
    // **The arguments are still the app's after the draw**: the root the call pushed is taken back
    // to the height of the stack the app had, because the next line reads the optional mode at index
    // one and the root was sitting on top of it (found on the emulator, 3/11: every `present('gc16')`
    // was a present with no mode, and the `du` that the controls ask for left the ghost of the text
    // that had moved).
    const int height = lua_gettop(L) - 1;
    lua_getfield(L, -1, "draw");
    if (lua_isfunction(L, -1)) {
      lua_pushvalue(L, -2);
      if (lua_pcall(L, 1, 0, 0) != LUA_OK) return lua_error(L);
    }
    lua_settop(L, height);
  }
  const char* mode = luaL_optstring(L, 1, nullptr);
  ++g_ui_present_count;
  if (!g_ui_ops.present(mode)) {
    // **The reason the panel gave**, when it gave one: an app that only reads "it failed" has to guess
    // between five things, and guessing costs a flash (docs/50).
    const char* why = (g_ui_ops.last_error != nullptr) ? g_ui_ops.last_error() : "";
    if (why != nullptr && why[0] != '\0') {
      lua_pushnil(L);
      lua_createtable(L, 0, 2);
      lua_pushstring(L, "E_INVAL");
      lua_setfield(L, -2, "code");
      lua_pushstring(L, why);
      lua_setfield(L, -2, "reason");
      return 2;
    }
    return push_nil_error(L, "E_INVAL", "not a quality of this display, or the panel did not take it");
  }
  lua_pushboolean(L, 1);
  return 1;
}

// **Les caixes de la pàgina damunt de la imatge de l'arrencada** (0.189.38, l'amo: *«Fes que al arrencar l'app no refresqui la
// seva vista … així es queda l'splash amb la barra de progrés que creix mentre l'app carrega completament … Un cop acabat,
// amaguem la barra de progrés i refresquem la vista sencera»*): the app names a picture in its manifest, the system washes
// the glass with it and **owes the whole page**, and that debt is what takes the picture away. This present puts what the app
// has drawn since on the glass **in its own box** and leaves the debt for the present that comes after —the page of the app
// whole and clean—, which is what makes a bar of progress grow over the splash of a start (docs/30, docs/50).
//
// **It draws the page the same way `ui.present()` does**, because a control that changed has to be drawn before it is
// composed: an app that called this and skipped the draw would send the pixels of the frame before (docs/30, docs/90).
int l_ui_present_over(lua_State* L) {
  if (g_ui_ops.present_over == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no display");
  }
  ui_restore_the_ring_if_a_card_went(L);
  if (l_ui_root(L) == 1) {
    const int height = lua_gettop(L) - 1;
    lua_getfield(L, -1, "draw");
    if (lua_isfunction(L, -1)) {
      lua_pushvalue(L, -2);
      if (lua_pcall(L, 1, 0, 0) != LUA_OK) return lua_error(L);
    }
    lua_settop(L, height);
  }
  ++g_ui_present_count;
  if (!g_ui_ops.present_over()) {
    const char* why = (g_ui_ops.last_error != nullptr) ? g_ui_ops.last_error() : "";
    if (why != nullptr && why[0] != '\0') {
      lua_pushnil(L);
      lua_createtable(L, 0, 2);
      lua_pushstring(L, "E_INVAL");
      lua_setfield(L, -2, "code");
      lua_pushstring(L, why);
      lua_setfield(L, -2, "reason");
      return 2;
    }
    return push_nil_error(L, "E_INVAL", "the panel did not take it");
  }
  lua_pushboolean(L, 1);
  return 1;
}

// **El vidre es queda amb el que té mentre l'app acaba una pàgina** (0.189.42, l'amo: *«veig que l'splash s'envà just abans de
// que les fonts es posin a la vista.. podriem treure l'splash després?»*): `ui.hold_page(true)` makes every present of the app
// compose its page and **not reach the glass** —the picture of the start stays on it—, and `ui.hold_page(false)` paints the
// page **whole and clean** in one pass, which is the moment the picture goes and the page arrives complete (docs/30, docs/50).
int l_ui_hold_page(lua_State* L) {
  if (g_ui_ops.hold_page == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no display");
  }
  const bool on = lua_toboolean(L, 1) != 0;
  if (!g_ui_ops.hold_page(on)) return push_nil_error(L, "E_NOPAGE", "there is no page to hold");
  lua_pushboolean(L, 1);
  return 1;
}

// ui.idle(): whether the paint that was asked for has finished. It is asked and not waited for,
// because the ink takes 120 to 450 ms and the app has other things to do (docs/30).
int l_ui_idle(lua_State* L) {  if (g_ui_ops.idle == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no display");
  }
  lua_pushboolean(L, g_ui_ops.idle() ? 1 : 0);
  return 1;
}

// **La imatge d'un moment del sistema, pintada pel sistema** (0.183.0): `ui.splash('sleep')` washes the glass whole and
// puts the picture of a moment on it —the same thing the prompt's `splash` does—, `ui.splash('/res/x.png')` puts any
// picture the file carries, and `ui.splash('hide')` gives the glass back to paper. It is the one call of this system
// that paints outside a page, and what the glass has afterwards is the picture and not the page: an app that uses it
// owes its page a present with the whole of it (`ui.present` after `ui.invalidate_all`) (docs/30, docs/50).
int l_ui_splash(lua_State* L) {
  if (g_splash_ops.show == nullptr) return push_nil_error(L, "E_UNSUPPORTED", "this build carries no pictures");
  const char* what = lua_isstring(L, 1) ? lua_tostring(L, 1) : nullptr;
  if (what == nullptr || what[0] == '\0') {
    return push_nil_error(L, "E_INVAL", "a splash needs a moment or a picture");
  }
  const bool hiding = strcmp(what, "hide") == 0 && g_splash_ops.hide != nullptr;
  const bool done = hiding ? g_splash_ops.hide() : g_splash_ops.show(what);
  if (!done) {
    const char* why = (g_splash_ops.last_error != nullptr) ? g_splash_ops.last_error() : nullptr;
    return push_nil_error(L, "E_INVAL", (why != nullptr && why[0] != '\0') ? why : "the picture was not shown");
  }
  lua_pushboolean(L, 1);
  return 1;
}

// **Els moments que aquest build té** (0.183.0): the words in the order of the system, for a page that offers them.
// It is the list and not a copy of it, so a page cannot name a moment this system does not know (docs/30, docs/50).
int l_ui_splashes(lua_State* L) {
  if (g_splash_ops.count == nullptr || g_splash_ops.word == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build carries no pictures");
  }
  const uint8_t moments = g_splash_ops.count();
  lua_createtable(L, moments, 0);
  for (uint8_t i = 0; i < moments; ++i) {
    lua_pushstring(L, g_splash_ops.word(i));
    lua_rawseti(L, -2, static_cast<lua_Integer>(i) + 1);
  }
  return 1;
}

// ui.resize(width, height): the app asks for a view of another size. The system makes a new canvas
// and calls `on_resize`, and the app draws it again — the system has no idea what belongs there
// (docs/30).
int l_ui_resize(lua_State* L) {
  if (g_ui_ops.resize == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no display");
  }
  const lua_Integer width = luaL_checkinteger(L, 1);
  const lua_Integer height = luaL_checkinteger(L, 2);
  if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
    return push_nil_error(L, "E_INVAL", "a view is a size in pixels");
  }
  if (!g_ui_ops.resize(static_cast<uint16_t>(width), static_cast<uint16_t>(height))) {
    return push_nil_error(L, "E_NOMEM", "there is no memory for a view of that size");
  }
  lua_pushboolean(L, 1);
  return 1;
}

// ui.damage(): how many regions are waiting for a present. An app that draws more pieces than the list
// holds sees it here, because past that they become one region painted with `gc16` (docs/30).
int l_ui_damage(lua_State* L) {
  if (g_ui_ops.damage == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no display");
  }
  lua_pushinteger(L, g_ui_ops.damage());
  return 1;
}

// ui.title(text): the title this app shows in the bar of the system. **The argument is required**: the
// bar is the only place the title lives, so asking for it would be asking the shell for something the
// app already knows (docs/30).
int l_ui_title(lua_State* L) {
  if (g_ui_ops.title == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no display");
  }
  if (lua_gettop(L) == 0 || lua_isnil(L, 1)) {
    return push_nil_error(L, "E_INVAL", "a title is a line of text");
  }
  const char* text = luaL_checkstring(L, 1);
  if (!g_ui_ops.title(text)) {
    return push_nil_error(L, "E_INVAL", "the bar did not take the title");
  }
  lua_pushboolean(L, 1);
  return 1;
}

// ui.rotation([degrees]): **the turn of the page** —of the glass, of the bar and of the fingers, which are
// the same value— and with an angle it turns it. Without an argument it answers the one it has
// (docs/50).
int l_ui_rotation(lua_State* L) {
  if (g_ui_ops.rotation == nullptr || g_ui_ops.set_rotation == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no display");
  }
  if (lua_gettop(L) == 0 || lua_isnil(L, 1)) {
    lua_pushinteger(L, g_ui_ops.rotation());
    return 1;
  }
  const lua_Integer degrees = luaL_checkinteger(L, 1);
  if (degrees != 0 && degrees != 90 && degrees != 180 && degrees != 270) {
    return push_nil_error(L, "E_INVAL", "a turn is 0, 90, 180 or 270");
  }
  if (!g_ui_ops.set_rotation(static_cast<uint16_t>(degrees))) {
    return push_nil_error(L, "E_NOMEM", "the page of that size did not fit");
  }
  lua_pushinteger(L, g_ui_ops.rotation());
  return 1;
}

// ui.full_screen([on]): **the app asks the system to keep the whole glass** (0.136.0, docs/46 D1). The strip of the bar
// goes away, the view is made the size of the panel and placed at its corner, and the app is told with `on_resize`, which
// is the hook it already knows. Without an argument it answers whether the app has the whole glass, which is the same
// shape `ui.rotation` has (docs/30).
//
// **The way out is not here**: three taps on the glass are the system's and the app never hears them, because a page that
// covers everything cannot be left to the page to open the door back (docs/30).
int l_ui_full_screen(lua_State* L) {
  if (g_ui_ops.full_screen == nullptr || g_ui_ops.is_full_screen == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no display");
  }
  if (lua_gettop(L) == 0 || lua_isnil(L, 1)) {
    lua_pushboolean(L, g_ui_ops.is_full_screen() ? 1 : 0);
    return 1;
  }
  const bool on = lua_toboolean(L, 1) != 0;
  if (!g_ui_ops.full_screen(on)) {
    // **Una pantalla bloquejada no és una pàgina sense memòria** (0.189.99): a refused `full_screen(false)` while an
    // administrator holds the lock said `E_NOMEM`, which is a lie that sends a person to look at the heap. The two
    // refusals are said apart, and the one of the lock names the lock —with `E_PERM`, the code of a permission that this
    // manifest does not have, which is what the table of errors has kept a line for since the first version— (docs/30).
    if (!on && g_admin_ops.is_locked != nullptr && g_admin_ops.is_locked()) {
      return push_nil_error(L, "E_PERM", "the screen is locked by an administrator");
    }
    return push_nil_error(L, "E_NOMEM", "there is no memory for a view of that size");
  }
  lua_pushboolean(L, 1);
  return 1;
}


// ---------------------------------------------------------------------------
// ui.canvas: the control layer's basic thing, an object with methods (docs/90)
// ---------------------------------------------------------------------------

// **A canvas is an object and not a table of numbers**: it has a buffer, a place and a life of its own,
// and an app holds it the way it holds a file. What it keeps is the slot and the number of the slot's
// generation, so a canvas made again by the system is not mistaken for the one that took its place.
//
// **It also takes fields of its own** —`on_draw`, `children`, whatever a control needs— in a table kept
// beside it (the uservalue of the object): reading looks there first and then in the methods, and writing
// always goes there, so a control is a thing an app can shape without a second table to keep in step.
struct LuaCanvas {
  int16_t slot;
  uint16_t generation;
};

// **Who has the focus and what the finger is holding**, per interpreter. It is not in the Lua table
// because it is the system's state: an app may read `focused` but it does not decide it, and a slot
// only means something together with the generation that handed it out (docs/30, docs/90).
// **A slot is an `int16_t` and this was an `int8_t`** (0.106.0): since 0.102.0 the table of canvases is as long as
// the memory holds (3 276 slots measured), so a page of more than a hundred and twenty-seven controls stored the slot
// of the dial in a signed byte, the comparison with the control's own slot never matched, and **the ring was invisible
// on every control past the hundred and twenty-seventh**. Found reading the state of the dial while answering the
// owner's question about the focus of a nested control (docs/30, docs/92).
int16_t g_ui_focus_slot = -1;
// Which tab of the strip has the ring, 0 when the ring is on a control of its own.
uint8_t g_ui_focus_item = 0;
uint16_t g_ui_focus_generation = 0;
// **El focus no es perd quan la pàgina es torna a fer sota el teclat** (0.179.81): the band takes the bottom of the glass, the
// app hears `on_resize` and builds its page again, and the box that had the dial is released —which left the walk pointing at a
// dead slot, and **the first control the rebuild created took the empty focus**: the eye of the clock's page, a row after the box
// a person had just entered—. While the band is up the box it types into is the one entered, so the place waits for that box and
// the first control that becomes focusable does not take it (docs/30, docs/90).
bool g_ui_focus_back_to_the_band = false;
// **Whether the selector of the focus is on the glass** (0.179.7, the owner: *«Si s'usa el dit el selector del focus ha
// d'amagar-se. Si es tornen a usar els botons físics ha de tornar. Per defecte el selector de focus ha d'estar desactivat
// fins que s'usen els botons»*). It is **off until a key of the board arrives** and it goes off again with the first finger:
// a control keeps saying `focused` —the dial is there and the app hears `on_focus`— and what it paints is that **and** this
// flag, so a page used with a finger has no ring drawn anywhere and the first physical key brings it back where the dial is
// (docs/30, docs/90).
bool g_ui_ring_shown = false;
int16_t g_ui_touch_slot = -1;
uint16_t g_ui_touch_generation = 0;
// Which tab of a strip the finger went down on, 0 for a control of its own.
uint8_t g_ui_touch_item = 0;
bool g_ui_touch_inside = false;
// **On ha caigut el contacte i si ha caminat des d'aleshores** (0.179.29): a press is a tap or it is a gesture, and the
// system knows which one by the pixels the finger travels —not by the name the gesture layer gives it later, which arrives
// when the finger is already gone—. A control the finger walked away from is not pressed and does not click on the way up,
// even if the finger ends inside its box: an app opened by a swipe is an app nobody asked for, and the same rule already
// stopped a text box from putting a caret (docs/30, docs/90).
uint16_t g_ui_touch_x0 = 0;
uint16_t g_ui_touch_y0 = 0;
// **Quan va caure el dit** (0.188.9): a row of a list is activated by a **tap** and by nothing else —touch and lift without
// moving—, and the travel alone cannot say it: the frames it is measured with are the ones a paint can swallow (a touch of a
// list froze the loop for four seconds on the board and the travel measured was zero). A contact of more than four hundred
// milliseconds is a long press, and a long press on a row does nothing (docs/30, docs/90).
int64_t g_ui_touch_ms0 = 0;
const int32_t kRowTapMs = 400;
// **And where the finger was the last time anyone said** (0.187.0): `touch.cpp` ends a contact with `ui_touch_progress(0, 0,
// false)` —the lift has no frame of its own—, so the travel of the whole contact, which is what tells a tap from a
// gesture, is measured from the landing to this point and not to the zero the lift carries (docs/30, docs/90).
uint16_t g_ui_touch_x = 0;
uint16_t g_ui_touch_y = 0;
// **I qui tenia el focus quan el dit va caure** (0.186.1): a finger lands the ring on the control it touched, and a contact
// that turns out to be a gesture has to give the page back the way it found it —otherwise the row the swipe stopped over is
// left wearing the ring, which is the *«es seleccionen items»* of the owner's report— (docs/30, docs/90).
int16_t g_ui_touch_focus_slot = -1;
uint16_t g_ui_touch_focus_generation = 0;
uint8_t g_ui_touch_focus_item = 0;
bool g_ui_touch_moved = false;
// **A contact that has lost its control** (0.113.0): the app releases the box the finger is on, and the finger that is
// still on the glass is not a press on whatever the app builds in its place —the contact began on another control—. It
// is cleared when the finger goes up, which is when a press of its own can begin (docs/30, docs/90).
bool g_ui_touch_orphan = false;

// **The gesture the system remembers of one finger** (0.88.0): where the finger started, where it is now, how many
// pixels have not been paid in rows yet, whether it has travelled far enough to be a gesture and not a tap, and the caret
// the tap would have left in its place if it does not move. It is state of the system and not of the app, and that is why
// it is cleared when a page comes in and when it goes: a gesture half way through an app that ends cannot be finished by
// the next one (docs/30, docs/90).
uint16_t g_ui_swipe_x0 = 0;
uint16_t g_ui_swipe_y0 = 0;
uint16_t g_ui_swipe_y = 0;
int16_t g_ui_swipe_rest = 0;
bool g_ui_swipe_moved = false;
uint16_t g_ui_swipe_tap = 0;
bool g_ui_swipe_tap_armed = false;

// **When a finger stops being a tap** (0.88.0): twelve pixels are a finger that has moved and not one that has
// trembled, and under that the box behaves as always —the tap puts the caret there (docs/90).
const int32_t kSwipeSlop = 12;

// **A page is turned by a longer finger than a caret is put** (0.175.0): forty pixels are a swipe and twelve are a tremble,
// and a grid that turned its page with the slop of a text box would turn it every time somebody pressed a cell a little to
// the side. **And a gesture turns one page**, not one per frame of the panel: the flag goes with the finger and is cleared
// when it leaves (docs/30, docs/90).
const int32_t kPageSwipeSlop = 40;
bool g_ui_swipe_page = false;
// **La llista on el gest va començar** (0.183.2, l'amo: *«Se suposa que quan un gest acaba no s'envien els tocs per no
// confondre el control, que es precisament el que passa»*): the page a finger turns is the page of the **control it landed
// on** —the rule the whole system follows for a contact, «the event goes to the control that is under it when the action
// begins»— and not of the control it happens to be over in the frame that travelled far enough. A finger that slides off
// the row it started on, which is what a swipe of a hand does, was a gesture that found no list and turned nothing.
int16_t g_ui_swipe_list = -1;
// **Un toc que arriba just després d'un gest no és un toc** (0.183.2): a contact that ends having travelled is a page or a
// window that moved, and the finger's next landing —the same finger of the same hand, or the contact the panel split in
// two— must not click what it lands on. Two hundred and fifty milliseconds are longer than any split and shorter than a
// person's second gesture (docs/30, docs/90).
int64_t g_ui_tap_swallowed_until_ms = 0;
// **Cent cinquanta mil·lisegons** (0.183.2): longer than any split of a contact by a panel that reports slowly —a hundred is
// already a chip that reports every thirty— and shorter than the flight of a hand from the end of a swipe to the next tap
// (docs/30, docs/90).
const int64_t kAfterGestureMs = 150;
// **Si el contacte que hi ha ara va néixer mut** (0.183.2): a tap that landed inside the window is born travelled and its lift
// must not open a new window —that is how a chain of swallowed taps is made, and the person is left with a list that answers
// every other touch (measured on the emulator: the tap after the swipe did not open, and the one a second later did not
// either)— (docs/30, docs/90).
bool g_ui_tap_swallowed = false;
// **El control que el contacte té premut** (0.183.3): a press belongs to the control that took the finger —it is set when the
// contact lands and let go when the finger leaves that same control— and the release used to be aimed at the control under
// the finger, which after a walk is another one: the row of a swipe stayed pressed on the glass (the owner: *«al acabar el
// gest, l'item sota el dit queda seleccionat»*). Minus one means the contact is holding nothing down (docs/30, docs/90).
int16_t g_ui_touch_pressed = -1;
// **Un lliscament d'una caixa deu una passada neta** (0.179.35): the rows go up with a `du` while the finger walks and the
// `gld16` that takes the blur away is the one at the lift, paid once (docs/30, docs/50).
bool g_ui_swipe_clean = false;
// **El rellotge de l'últim `du` d'un lliscament**: a quick finger walks several rows between two paints of the glass, and a
// paint per row is a queue that outlives the gesture. Zero means nothing has been painted for this contact yet (docs/50).
int64_t g_ui_swipe_painted_ms = 0;
const int64_t kSwipePaintMs = 60;

// **Quan un contacte deixa de ser un toc per a un control** (0.179.29): the same twenty pixels `touch.cpp` uses to call a
// contact a `drag` and not a `tap` (`kSlopPx`), said once here so a button and an app cannot disagree about what a finger
// did. Under it a control behaves as it always has; over it the finger is a gesture and the control is nobody's
// (docs/30, docs/90).
const int32_t kTapSlop = 20;

// **The text box the system's keyboard types into** (docs/30, docs/90): a reference to the object in the registry
// and not a slot, because the app holds the box in a local and the keyboard must not lose it when a page is made
// again. It is one keyboard and one box, which is what a keyboard is. **The kind is remembered beside it**: the
// alphabet of the last band a box asked for, so a focus that brings the keyboard back does not turn a numeric box
// into a QWERTY (0.91.0).
int g_keyboard_target_ref = LUA_NOREF;
uint8_t g_keyboard_kind = 0;  // 0 is alpha, 1 is numeric, the two of `ui.keyboard()`

// **The caret of the box blinks without the app** (0.92.0): the system owns the block of the box that has the focus, toggles
// it every six hundred milliseconds in the kernel loop and asks for the cell to be redrawn. `dirty` means the block has
// just come back —a focus, a key— and the next turn has to show it before the clock starts again.
//
// **I parpelleja mentre la caixa té el focus i no s'atura** (0.179.61): it stood still after six blinks, which was ink saved
// on a panel that keeps the ghost of a quick pass and read on the glass as a cursor that had died while a person was looking
// at it. The block goes on; what it costs is one cell of a hundred and twenty milliseconds every six hundred, and the count
// of the settle starts again at every key and every focus.
bool g_caret_on = true;
bool g_caret_dirty = false;
int64_t g_caret_next_ms = 0;
uint8_t g_caret_blinks = 0;
const char kCanvasMeta[] = "lunokOS.UICanvas";

// The fields of the object, and then its methods: `box.on_draw` is the app's, `box.draw` is ours.
int l_canvas_index(lua_State* L) {
  lua_getiuservalue(L, 1, 1);
  if (lua_istable(L, -1)) {
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    if (!lua_isnil(L, -1)) return 1;
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  luaL_getmetatable(L, kCanvasMeta);
  lua_getfield(L, -1, "__methods");
  lua_pushvalue(L, 2);
  lua_rawget(L, -2);
  return 1;
}

// **The fields of a label** (docs/30, docs/90): the words, the two colours, the two alignments and the size
// of the body. A label is a control with `__label` set in its own table, and changing one of these means the
// label owes a draw —but only when the value is actually another one, which is what makes a present with
// the same word cost nothing.
bool is_label_field(const char* key) {
  return strcmp(key, "text") == 0 || strcmp(key, "color") == 0 ||
         strcmp(key, "background") == 0 || strcmp(key, "align") == 0 ||
         strcmp(key, "valign") == 0 || strcmp(key, "scale") == 0;
}

bool label_field_valid(lua_State* L, const char* key, int value) {
  if (strcmp(key, "text") == 0) {
    return lua_isnoneornil(L, value) || lua_type(L, value) == LUA_TSTRING;
  }
  if (strcmp(key, "color") == 0 || strcmp(key, "background") == 0) {
    if (!lua_isnumber(L, value)) return false;
    const lua_Integer level = lua_tointeger(L, value);
    return level >= 0 && level <= 15;
  }
  if (strcmp(key, "scale") == 0) {
    if (!lua_isnumber(L, value)) return false;
    const lua_Integer scale = lua_tointeger(L, value);
    return scale >= 1 && scale <= 16;
  }
  if (strcmp(key, "align") == 0) {
    const char* text = lua_tostring(L, value);
    return text != nullptr && (strcmp(text, "left") == 0 || strcmp(text, "center") == 0 ||
                               strcmp(text, "centre") == 0 || strcmp(text, "right") == 0);
  }
  if (strcmp(key, "valign") == 0) {
    const char* text = lua_tostring(L, value);
    return text != nullptr && (strcmp(text, "top") == 0 || strcmp(text, "middle") == 0 ||
                               strcmp(text, "bottom") == 0);
  }
  return false;
}

// **The fields of a button** (docs/30, docs/90): the two colours of its frame, whether it is held and
// whether the walk of the dial and the finger can land on it. The child of a button is a control of its
// own: the button does not keep a copy of it and does not need a field for it. **`framed` i `icon`** són les dues de la
// 0.185.0: the key without a frame and the mark of the system it carries (docs/30, docs/92).
bool is_button_field(const char* key) {
  return strcmp(key, "background") == 0 || strcmp(key, "border") == 0 ||
         strcmp(key, "pressed") == 0 || strcmp(key, "focused") == 0 ||
         strcmp(key, "focusable") == 0 || strcmp(key, "primary") == 0 ||
         strcmp(key, "title") == 0 || strcmp(key, "tag") == 0 ||
         strcmp(key, "color") == 0 || strcmp(key, "scale") == 0 ||
         strcmp(key, "framed") == 0 || strcmp(key, "icon") == 0;
}

// **The fields a checkbox adds to the ones of a frame**: whether it is checked —the state the click changes
// and the app reads— and the level of the tick, which is a colour of its own because a black tick on a black
// box is a checkbox that says nothing (docs/30, docs/90).
bool is_checkbox_field(const char* key) {
  return strcmp(key, "checked") == 0 || strcmp(key, "mark") == 0;
}

bool checkbox_field_valid(lua_State* L, const char* key, int value) {
  if (strcmp(key, "checked") == 0) return lua_type(L, value) == LUA_TBOOLEAN;
  if (!lua_isnumber(L, value)) return false;
  const lua_Integer level = lua_tointeger(L, value);
  return level >= 0 && level <= 15;
}

bool button_field_valid(lua_State* L, const char* key, int value) {
  if (strcmp(key, "pressed") == 0 || strcmp(key, "focused") == 0 ||
      strcmp(key, "focusable") == 0 || strcmp(key, "primary") == 0 ||
      strcmp(key, "framed") == 0) {
    return lua_type(L, value) == LUA_TBOOLEAN;
  }
  if (strcmp(key, "title") == 0 || strcmp(key, "tag") == 0 || strcmp(key, "icon") == 0) {
    return lua_isnoneornil(L, value) || lua_type(L, value) == LUA_TSTRING;
  }
  if (!lua_isnumber(L, value)) return false;
  const lua_Integer level = lua_tointeger(L, value);
  if (strcmp(key, "scale") == 0) return level >= 1 && level <= 16;
  return level >= 0 && level <= 15;
}

// **The fields of a progress bar** (docs/30, docs/90): the four colours and the number. The number is 0 to 100
// and **a negative one is "nothing to say yet"**, which is a state of a page and not an error.
bool is_progress_field(const char* key) {
  return strcmp(key, "value") == 0 || strcmp(key, "background") == 0 ||
         strcmp(key, "border") == 0 || strcmp(key, "track") == 0 || strcmp(key, "fill") == 0;
}

bool progress_field_valid(lua_State* L, const char* key, int value) {
  if (!lua_isnumber(L, value)) return false;
  const lua_Integer number = lua_tointeger(L, value);
  if (strcmp(key, "value") == 0) return number >= -1 && number <= 100;
  return number >= 0 && number <= 15;
}

// **The fields a slider adds to the ones of a frame**: the number and its limits, the step, the unit and the
// size of the number. `min` and `max` are a 16-bit value because that is what the control draws and what the
// kernel is handed (docs/30, docs/90).
bool is_slider_field(const char* key) {
  // **`editing` is the slider being entered with the centre** (0.117.0): the field the system writes and the app reads
  // to know whether the two keys are the number or the walk, and it is writable from Lua because a page that puts the
  // person inside a slider on purpose is a page that knows what it wants (docs/30, docs/92).
  return strcmp(key, "value") == 0 || strcmp(key, "min") == 0 || strcmp(key, "max") == 0 ||
         strcmp(key, "step") == 0 || strcmp(key, "suffix") == 0 || strcmp(key, "scale") == 0 ||
         strcmp(key, "track") == 0 || strcmp(key, "fill") == 0 || strcmp(key, "text") == 0 ||
         strcmp(key, "editing") == 0;
}

bool slider_field_valid(lua_State* L, const char* key, int value) {
  if (strcmp(key, "editing") == 0) return lua_type(L, value) == LUA_TBOOLEAN;
  if (strcmp(key, "suffix") == 0) {
    return lua_isnoneornil(L, value) || lua_type(L, value) == LUA_TSTRING;
  }
  if (!lua_isnumber(L, value)) return false;
  const lua_Integer number = lua_tointeger(L, value);
  if (strcmp(key, "scale") == 0) return number >= 1 && number <= 16;
  if (strcmp(key, "step") == 0) return number >= 1 && number <= 32767;
  if (strcmp(key, "value") == 0 || strcmp(key, "min") == 0 || strcmp(key, "max") == 0) {
    return number >= -32768 && number <= 32767;
  }
  return number >= 0 && number <= 15;
}

// **The fields of a row** (docs/30, docs/90): the three texts, the two colours and the two flags of its shape.
bool is_row_field(const char* key) {
  return strcmp(key, "title") == 0 || strcmp(key, "detail") == 0 ||
         strcmp(key, "subtitle") == 0 || strcmp(key, "text") == 0 || strcmp(key, "dim") == 0 ||
         strcmp(key, "background") == 0 || strcmp(key, "scale") == 0 ||
         strcmp(key, "marked") == 0 || strcmp(key, "separator") == 0;
}

bool row_field_valid(lua_State* L, const char* key, int value) {
  if (strcmp(key, "title") == 0 || strcmp(key, "detail") == 0 || strcmp(key, "subtitle") == 0) {
    return lua_isnoneornil(L, value) || lua_type(L, value) == LUA_TSTRING;
  }
  if (strcmp(key, "marked") == 0 || strcmp(key, "separator") == 0) {
    return lua_type(L, value) == LUA_TBOOLEAN;
  }
  if (!lua_isnumber(L, value)) return false;
  const lua_Integer number = lua_tointeger(L, value);
  if (strcmp(key, "scale") == 0) return number >= 1 && number <= 16;
  return number >= 0 && number <= 15;
}

// **The fields of a panel**: the flow and the two spaces. The weight of a child is not the panel's field but the
// child's, because a weight says how much of the room the one that takes it takes (docs/30, docs/90).
bool is_panel_field(const char* key) {
  return strcmp(key, "flow") == 0 || strcmp(key, "padding") == 0 || strcmp(key, "gap") == 0;
}

bool panel_field_valid(lua_State* L, const char* key, int value) {
  if (strcmp(key, "flow") == 0) {
    const char* flow = lua_tostring(L, value);
    return flow != nullptr && (strcmp(flow, "vertical") == 0 || strcmp(flow, "horizontal") == 0 ||
                               strcmp(flow, "overlay") == 0);
  }
  if (!lua_isnumber(L, value)) return false;
  const lua_Integer number = lua_tointeger(L, value);
  return number >= 0 && number <= 64;
}

// **The fields of a strip of tabs**: the words, the chosen one, the two colours, the size of the words and whether
// every tab wears a cross to close it (0.163.2: `closeable` was a field the painter read and the strip did not accept,
// so a typo in it was a cross that did not appear instead of a line, docs/30).
bool is_tabs_field(const char* key) {
  return strcmp(key, "items") == 0 || strcmp(key, "index") == 0 || strcmp(key, "text") == 0 ||
         strcmp(key, "background") == 0 || strcmp(key, "scale") == 0 || strcmp(key, "closeable") == 0 ||
         // **On és la inversió de la tira** (0.192.49): the dial writes it when it walks the strip, i una app que la vulgui en una
         // altra pestanya que la triada també l'ha de poder escriure. **El que una app no ha de fer és escriure-la perquè la
         // pestanya segueixi**: això ho fa `ui.focus` sobre una tira, que posa el cercle a l'`index` i no a la primera parada
         // (trobat a l'app `lora`, docs/30, docs/90).
         strcmp(key, "focus_index") == 0;
}

bool tabs_field_valid(lua_State* L, const char* key, int value) {
  if (strcmp(key, "items") == 0) return lua_istable(L, value) || lua_isnil(L, value);
  if (strcmp(key, "closeable") == 0) return lua_type(L, value) == LUA_TBOOLEAN;
  if (!lua_isnumber(L, value)) return false;
  const lua_Integer number = lua_tointeger(L, value);
  if (strcmp(key, "index") == 0 || strcmp(key, "focus_index") == 0) return number >= 0 && number <= 8;
  if (strcmp(key, "scale") == 0) return number >= 1 && number <= 16;
  return number >= 0 && number <= 15;
}

// **The fields of the dots of a page**: how many, which one, and the two colours.
bool is_dots_field(const char* key) {
  return strcmp(key, "pages") == 0 || strcmp(key, "current") == 0 || strcmp(key, "level") == 0 ||
         strcmp(key, "background") == 0;
}

bool dots_field_valid(lua_State* L, const char* key, int value) {
  if (!lua_isnumber(L, value)) return false;
  const lua_Integer number = lua_tointeger(L, value);
  if (strcmp(key, "pages") == 0 || strcmp(key, "current") == 0) return number >= 0 && number <= 512;
  return number >= 0 && number <= 15;
}

// **`weight` is a field of every control and not of the panel**: it says how much of the free room a child
// takes, so it is written on the child by the app that adds it (docs/30, docs/90).
bool is_weight_field(const char* key) { return strcmp(key, "weight") == 0; }

bool weight_field_valid(lua_State* L, int value) {
  if (!lua_isnumber(L, value)) return false;
  const lua_Integer number = lua_tointeger(L, value);
  return number >= 0 && number <= 15;
}

// **The fields of a list**: the items —a Lua array of tables with `title`, `detail` and `subtitle`, or of
// strings, which are a title—, the height of a row, the page it is on, the chosen row and the three colours.
bool is_list_field(const char* key) {
  return strcmp(key, "items") == 0 || strcmp(key, "row_height") == 0 || strcmp(key, "page") == 0 ||
         strcmp(key, "marked") == 0 || strcmp(key, "text") == 0 || strcmp(key, "dim") == 0 ||
         strcmp(key, "scale") == 0 || strcmp(key, "separator") == 0;
}

// **The fields of a text box**: the word, where the caret is in it, whether the block is drawn, whether it is a
// password and the two colours with the size (docs/30, docs/90).
bool is_textbox_field(const char* key) {
  return strcmp(key, "text") == 0 || strcmp(key, "cursor") == 0 || strcmp(key, "caret") == 0 ||
         strcmp(key, "mask") == 0 || strcmp(key, "color") == 0 || strcmp(key, "scale") == 0 ||
         // **The two words of 0.116.0**: a box that is read and not written keeps its caret and never brings the band,
         // and a box that has no frame is a box whose frame is the colour of the paper (docs/30, docs/92).
         strcmp(key, "readonly") == 0 || strcmp(key, "framed") == 0 || strcmp(key, "editing") == 0;
}

// **The fields of a sparkline**: the samples, the second series and the two colours (docs/30, docs/90).
bool is_spark_field(const char* key) {
  return strcmp(key, "samples") == 0 || strcmp(key, "top") == 0 || strcmp(key, "color") == 0;
}

bool spark_field_valid(lua_State* L, const char* key, int value) {
  if (strcmp(key, "samples") == 0 || strcmp(key, "top") == 0) {
    return lua_istable(L, value) || lua_isnil(L, value);
  }
  if (!lua_isnumber(L, value)) return false;
  const lua_Integer number = lua_tointeger(L, value);
  return number >= 0 && number <= 15;
}

// **The fields of a chart**: the values, the minutes of each sample, the two ends of the scale when the app fixes
// them, the tone and the step of the numbers (docs/30, docs/90).
bool is_chart_field(const char* key) {
  return strcmp(key, "values") == 0 || strcmp(key, "times") == 0 || strcmp(key, "color") == 0 ||
         strcmp(key, "background") == 0 || strcmp(key, "scale") == 0 || strcmp(key, "suffix") == 0 ||
         strcmp(key, "min") == 0 || strcmp(key, "max") == 0 || strcmp(key, "decimals") == 0 ||
         strcmp(key, "left") == 0 || strcmp(key, "right") == 0 || strcmp(key, "gap") == 0 ||
         strcmp(key, "span") == 0;
}

bool chart_field_valid(lua_State* L, const char* key, int value) {
  if (strcmp(key, "values") == 0 || strcmp(key, "times") == 0) {
    return lua_istable(L, value) || lua_isnil(L, value);
  }
  if (strcmp(key, "suffix") == 0) return lua_isstring(L, value) || lua_isnil(L, value);
  // **Els dos caps de l'eix són els de l'app** (0.189.169): the window of a day says an hour and the window of a quarter
  // says a date, i això no ho pot saber un control que només rep minuts (docs/30, docs/90).
  if (strcmp(key, "left") == 0 || strcmp(key, "right") == 0) return lua_isstring(L, value) || lua_isnil(L, value);
  if (!lua_isnumber(L, value)) return false;
  const lua_Integer number = lua_tointeger(L, value);
  if (strcmp(key, "scale") == 0) return number >= 1 && number <= 8;
  // **Els decimals del número** (0.189.169): zero (una bateria), un (un termòmetre) o dos, i el valor arriba multiplicat
  // per la potència de deu que diu aquest camp.
  if (strcmp(key, "decimals") == 0) return number >= 0 && number <= 2;
  // **El forat, en minuts** (0.189.169): zero és «no hi ha forat possible», que és el que demana una sèrie ja aprimada.
  if (strcmp(key, "gap") == 0) return number >= 0 && number <= 255;
  // **La finestra que no es mou** (0.192.50): zero és la de les mostres, i un número són les unitats de l'eix que la finestra
  // cobreix —els segons d'aquesta pàgina, els minuts d'un registre— amb l'última mostra a la dreta (docs/30, docs/90).
  if (strcmp(key, "span") == 0) return number >= 0 && number <= 86400;
  // **Els dos caps de l'escala són números qualssevol**: a chart of temperatures goes under zero and one of a battery goes to
  // a hundred, so the range is not the range of a tone (docs/90).
  if (strcmp(key, "min") == 0 || strcmp(key, "max") == 0) {
    return number >= -32768 && number <= 32767;
  }
  return number >= 0 && number <= 15;
}

// **The fields of a pie**: the slices and the darkest tone.
bool is_pie_field(const char* key) { return strcmp(key, "slices") == 0 || strcmp(key, "color") == 0; }

// **The fields of a calendar**: the month, the chosen day, today, the seven names and the colours (docs/30, docs/90).
bool is_month_field(const char* key) {
  return strcmp(key, "year") == 0 || strcmp(key, "month") == 0 || strcmp(key, "selected") == 0 ||
         strcmp(key, "today") == 0 || strcmp(key, "weekdays") == 0 || strcmp(key, "color") == 0 ||
         strcmp(key, "dim") == 0 || strcmp(key, "scale") == 0 ||
         // **Els dies que tenen alguna cosa** (0.189.171): a list of day numbers, which is the dot an agenda's calendar
         // has under the days that are not empty (docs/30, docs/45).
         strcmp(key, "marks") == 0;
}

bool month_field_valid(lua_State* L, const char* key, int value) {
  if (strcmp(key, "weekdays") == 0 || strcmp(key, "marks") == 0) {
    return lua_istable(L, value) || lua_isnil(L, value);
  }
  if (!lua_isnumber(L, value)) return false;
  const lua_Integer number = lua_tointeger(L, value);
  if (strcmp(key, "year") == 0) return number >= 1 && number <= 9999;
  if (strcmp(key, "month") == 0) return number >= 1 && number <= 12;
  if (strcmp(key, "selected") == 0 || strcmp(key, "today") == 0) return number >= 0 && number <= 31;
  if (strcmp(key, "scale") == 0) return number >= 1 && number <= 16;
  return number >= 0 && number <= 15;
}

bool pie_field_valid(lua_State* L, const char* key, int value) {
  if (strcmp(key, "slices") == 0) return lua_istable(L, value) || lua_isnil(L, value);
  if (!lua_isnumber(L, value)) return false;
  const lua_Integer number = lua_tointeger(L, value);
  return number >= 0 && number <= 15;
}

// **The fields of a menu**: the options —a Lua array of words, or of tables with `label`, `tag`, `hidden` and
// `primary`—, the option the dial is on, the title and the three tones with the size of the words (docs/30, docs/90).
bool is_menu_field(const char* key) {
  return strcmp(key, "items") == 0 || strcmp(key, "index") == 0 || strcmp(key, "title") == 0 ||
         strcmp(key, "color") == 0 || strcmp(key, "dim") == 0 || strcmp(key, "scale") == 0;
}

bool menu_field_valid(lua_State* L, const char* key, int value) {
  if (strcmp(key, "items") == 0) return lua_istable(L, value) || lua_isnil(L, value);
  if (strcmp(key, "title") == 0) return lua_isnoneornil(L, value) || lua_type(L, value) == LUA_TSTRING;
  if (!lua_isnumber(L, value)) return false;
  const lua_Integer number = lua_tointeger(L, value);
  if (strcmp(key, "index") == 0) return number >= 0 && number <= kMaxMenuItems;
  if (strcmp(key, "scale") == 0) return number >= 1 && number <= 16;
  return number >= 0 && number <= 15;
}

// **The fields of a bar of menus**: the titles with their menus, the one that is unfolded, the entry of it the dial is on,
// the height of the row and the three tones with the size (docs/30, docs/90).
bool is_menu_bar_field(const char* key) {
  return strcmp(key, "items") == 0 || strcmp(key, "open") == 0 || strcmp(key, "index") == 0 ||
         strcmp(key, "bar_height") == 0 || strcmp(key, "color") == 0 || strcmp(key, "dim") == 0 ||
         strcmp(key, "scale") == 0;
}

bool menu_bar_field_valid(lua_State* L, const char* key, int value) {
  if (strcmp(key, "items") == 0) return lua_istable(L, value) || lua_isnil(L, value);
  if (!lua_isnumber(L, value)) return false;
  const lua_Integer number = lua_tointeger(L, value);
  if (strcmp(key, "open") == 0) return number >= 0 && number <= kMaxMenuBarTitles;
  if (strcmp(key, "index") == 0) return number >= 0 && number <= kMaxMenuBarEntries;
  if (strcmp(key, "bar_height") == 0) return number >= 20 && number <= 512;
  if (strcmp(key, "scale") == 0) return number >= 1 && number <= 16;
  return number >= 0 && number <= 15;
}

// **The fields of a box of several lines**: the text, where the caret is, whether it blinks, whether the long lines
// wrap, and the three tones with the size (docs/30, docs/90).
bool is_textbox_multiline_field(const char* key) {
  return strcmp(key, "text") == 0 || strcmp(key, "cursor") == 0 || strcmp(key, "caret") == 0 ||
         strcmp(key, "wrap") == 0 || strcmp(key, "top") == 0 || strcmp(key, "color") == 0 ||
         strcmp(key, "scale") == 0 ||
         // **The two words of 0.116.0**: read and not written, and a frame or no frame (docs/30, docs/92).
         strcmp(key, "readonly") == 0 || strcmp(key, "framed") == 0 || strcmp(key, "editing") == 0;
}

bool textbox_multiline_field_valid(lua_State* L, const char* key, int value) {
  if (strcmp(key, "text") == 0) return lua_isnoneornil(L, value) || lua_type(L, value) == LUA_TSTRING;
  if (strcmp(key, "caret") == 0 || strcmp(key, "wrap") == 0 || strcmp(key, "readonly") == 0 ||
      strcmp(key, "framed") == 0 || strcmp(key, "editing") == 0) {
    return lua_type(L, value) == LUA_TBOOLEAN;
  }
  if (!lua_isnumber(L, value)) return false;
  const lua_Integer number = lua_tointeger(L, value);
  if (strcmp(key, "cursor") == 0) return number >= 0 && number <= 65535;
  // **`top` is the only thing of this control that can be worth less than zero** (0.88.0): `-1` is not a row, it is
  // "whichever the caret asks for" (docs/30, docs/90).
  if (strcmp(key, "top") == 0) return number >= -1 && number <= 65535;
  if (strcmp(key, "scale") == 0) return number >= 1 && number <= 16;
  return number >= 0 && number <= 15;
}

// **The fields of a selector**: the question, the options, which one is chosen, the word for "nothing", whether the list is
// over the field, and the two tones with the size (docs/30, docs/90).
bool is_selector_field(const char* key) {
  return strcmp(key, "label") == 0 || strcmp(key, "items") == 0 || strcmp(key, "index") == 0 ||
         strcmp(key, "empty") == 0 || strcmp(key, "choosing") == 0 || strcmp(key, "color") == 0 ||
         strcmp(key, "dim") == 0 || strcmp(key, "scale") == 0;
}

bool selector_field_valid(lua_State* L, const char* key, int value) {
  if (strcmp(key, "label") == 0 || strcmp(key, "empty") == 0) {
    return lua_isnoneornil(L, value) || lua_type(L, value) == LUA_TSTRING;
  }
  if (strcmp(key, "items") == 0) return lua_istable(L, value) || lua_isnil(L, value);
  if (strcmp(key, "choosing") == 0) return lua_type(L, value) == LUA_TBOOLEAN;
  if (!lua_isnumber(L, value)) return false;
  const lua_Integer number = lua_tointeger(L, value);
  if (strcmp(key, "index") == 0) return number >= 0 && number <= 4096;
  if (strcmp(key, "scale") == 0) return number >= 1 && number <= 16;
  return number >= 0 && number <= 15;
}

// **The fields of a band of notifications**: the notices, the card the finger last touched and the three tones with the size
// (docs/30, docs/90).
bool is_notifications_field(const char* key) {
  return strcmp(key, "items") == 0 || strcmp(key, "index") == 0 || strcmp(key, "color") == 0 ||
         strcmp(key, "dim") == 0 || strcmp(key, "scale") == 0;
}

bool notifications_field_valid(lua_State* L, const char* key, int value) {
  if (strcmp(key, "items") == 0) return lua_istable(L, value) || lua_isnil(L, value);
  if (!lua_isnumber(L, value)) return false;
  const lua_Integer number = lua_tointeger(L, value);
  if (strcmp(key, "index") == 0) return number >= 0 && number <= 4096;
  if (strcmp(key, "scale") == 0) return number >= 1 && number <= 16;
  return number >= 0 && number <= 15;
}

// **The fields of a grid of cells**: the cells, the one the dial is on, the one that is chosen, the columns, the three tones and
// the size (docs/30, docs/90).
bool is_icon_grid_field(const char* key) {
  return strcmp(key, "items") == 0 || strcmp(key, "index") == 0 || strcmp(key, "marked") == 0 ||
         strcmp(key, "columns") == 0 || strcmp(key, "color") == 0 || strcmp(key, "dim") == 0 ||
         strcmp(key, "scale") == 0;
}

bool icon_grid_field_valid(lua_State* L, const char* key, int value) {
  if (strcmp(key, "items") == 0) return lua_istable(L, value) || lua_isnil(L, value);
  if (!lua_isnumber(L, value)) return false;
  const lua_Integer number = lua_tointeger(L, value);
  if (strcmp(key, "index") == 0 || strcmp(key, "marked") == 0) return number >= 0 && number <= kMaxGridCells;
  if (strcmp(key, "columns") == 0) return number >= 0 && number <= 8;
  if (strcmp(key, "scale") == 0) return number >= 1 && number <= 16;
  return number >= 0 && number <= 15;
}

// **The fields of a dialog**: the four texts, which door is the default and which one the dial is on, the two
// colours and the size (docs/30, docs/90).
bool is_dialog_field(const char* key) {
  return strcmp(key, "title") == 0 || strcmp(key, "text") == 0 || strcmp(key, "note") == 0 ||
         strcmp(key, "accept") == 0 || strcmp(key, "cancel") == 0 || strcmp(key, "emphasis") == 0 ||
         strcmp(key, "choice") == 0 || strcmp(key, "color") == 0 || strcmp(key, "dim") == 0 ||
         strcmp(key, "scale") == 0;
}

bool dialog_field_valid(lua_State* L, const char* key, int value) {
  if (strcmp(key, "title") == 0 || strcmp(key, "text") == 0 || strcmp(key, "note") == 0 ||
      strcmp(key, "accept") == 0 || strcmp(key, "cancel") == 0) {
    return lua_isnoneornil(L, value) || lua_type(L, value) == LUA_TSTRING;
  }
  if (!lua_isnumber(L, value)) return false;
  const lua_Integer number = lua_tointeger(L, value);
  if (strcmp(key, "scale") == 0) return number >= 1 && number <= 16;
  if (strcmp(key, "emphasis") == 0) return number >= 0 && number <= 2;
  if (strcmp(key, "choice") == 0) return number >= 1 && number <= 2;
  return number >= 0 && number <= 15;
}

bool textbox_field_valid(lua_State* L, const char* key, int value) {
  if (strcmp(key, "text") == 0) return lua_isnoneornil(L, value) || lua_type(L, value) == LUA_TSTRING;
  if (strcmp(key, "caret") == 0 || strcmp(key, "mask") == 0 || strcmp(key, "readonly") == 0 ||
      strcmp(key, "framed") == 0 || strcmp(key, "editing") == 0) {
    return lua_type(L, value) == LUA_TBOOLEAN;
  }
  if (!lua_isnumber(L, value)) return false;
  const lua_Integer number = lua_tointeger(L, value);
  if (strcmp(key, "cursor") == 0) return number >= 0 && number <= 4096;
  if (strcmp(key, "scale") == 0) return number >= 1 && number <= 16;
  return number >= 0 && number <= 15;
}

bool list_field_valid(lua_State* L, const char* key, int value) {
  if (strcmp(key, "items") == 0) return lua_istable(L, value) || lua_isnil(L, value);
  if (strcmp(key, "separator") == 0) return lua_type(L, value) == LUA_TBOOLEAN;
  if (!lua_isnumber(L, value)) return false;
  const lua_Integer number = lua_tointeger(L, value);
  if (strcmp(key, "row_height") == 0) return number >= 8 && number <= 512;
  if (strcmp(key, "scale") == 0) return number >= 1 && number <= 16;
  if (strcmp(key, "page") == 0 || strcmp(key, "marked") == 0) return number >= 0 && number <= 1024;
  return number >= 0 && number <= 15;
}

// Declared here because a control that goes off has to say so to its children, and that walk lives with the
// painters further down.
void ui_set_disabled_below(lua_State* L, int object, bool off, int depth);
bool ui_set_field_bool(lua_State* L, int object, const char* key, bool value);
// **What a move of the ring of a grid costs** (0.177.3): the field of a grid writes it here, before the paints that own
// the answer are compiled, and false is "not a grid, or the kernel cannot measure it" (docs/30, docs/90).
bool ui_invalidate_grid_ring(lua_State* L, int object, uint8_t from, uint8_t to, bool cursor_moved);

int l_canvas_newindex(lua_State* L) {
  lua_getiuservalue(L, 1, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return luaL_error(L, "a canvas takes no fields");
  }
  const char* key = lua_type(L, 2) == LUA_TSTRING ? lua_tostring(L, 2) : nullptr;
  bool redraw = false;
  // **The cell a grid's cursor was on** (0.177.3), read before the field is written: it is one of the two cells a step
  // inside the page paints (docs/30, docs/90).
  lua_Integer index_before = 0;
  if (key != nullptr) {
    lua_getfield(L, -1, "__label");
    const bool label = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__button");
    const bool button = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__checkbox");
    const bool checkbox = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__progress");
    const bool progress = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__slider");
    const bool slider = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__row");
    const bool row = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__panel");
    const bool panel = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__tabs");
    const bool tabs = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__dots");
    const bool dots = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__list");
    const bool list = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__textbox");
    const bool textbox = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__dialog");
    const bool dialog = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__spark");
    const bool spark = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__chart");
    const bool chart = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__pie");
    const bool pie = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__month");
    const bool month = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__menu");
    const bool menu = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__menu_bar");
    const bool menu_bar = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__textbox_multiline");
    const bool multiline = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__selector");
    const bool selector = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__notifications");
    const bool notifications = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__icon_grid");
    const bool icon_grid = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    if (strcmp(key, "on_draw") == 0) {
      redraw = lua_type(L, 3) == LUA_TFUNCTION;
      // **And the system is told whether this canvas paints itself** (0.173.3): a page with an `on_draw` is a page the system
      // can ask to paint the hole a card leaves, cut at the hole, instead of filling it with paper and hoping (docs/30).
      if (g_ui_ops.canvas_paints_itself != nullptr) {
        LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(L, 1, kCanvasMeta));
        if (canvas != nullptr) g_ui_ops.canvas_paints_itself(canvas->slot, redraw);
      }
    } else if (is_weight_field(key)) {
      if (!weight_field_valid(L, 3)) {
        return luaL_error(L, "a control's weight is 0 to 15");
      }
      lua_pushvalue(L, 2);
      lua_rawget(L, -2);
      const bool same = lua_rawequal(L, -1, 3);
      lua_pop(L, 1);
      redraw = !same;
    } else if (label && is_label_field(key)) {
      if (!label_field_valid(L, key, 3)) {
        return luaL_error(L, "a label's %s is not a value it can use", key);
      }
      // **The same word is not a change**: the previous value is read before the new one is put there.
      lua_pushvalue(L, 2);
      lua_rawget(L, -2);
      const bool same = lua_rawequal(L, -1, 3);
      lua_pop(L, 1);
      redraw = !same;
    } else if ((button || checkbox || slider || tabs || list || textbox || dialog || month || menu || menu_bar ||
                multiline || selector || notifications || icon_grid) &&
               is_button_field(key)) {
      if (!button_field_valid(L, key, 3)) {
        return luaL_error(L, "a %s's %s is not a value it can use",
                          checkbox   ? "checkbox"
                          : slider   ? "slider"
                          : tabs     ? "tabs"
                          : list     ? "list"
                          : textbox  ? "text box"
                          : dialog   ? "dialog"
                          : month    ? "calendar"
                          : menu     ? "menu"
                          : menu_bar ? "menu bar"
                          : multiline ? "box of lines"
                          : selector ? "selector"
                          : notifications ? "band of notifications"
                          : icon_grid ? "grid of cells"
                                     : "button",
                          key);
      }
      lua_pushvalue(L, 2);
      lua_rawget(L, -2);
      const bool same = lua_rawequal(L, -1, 3);
      lua_pop(L, 1);
      redraw = !same;
    } else if (checkbox && is_checkbox_field(key)) {
      if (!checkbox_field_valid(L, key, 3)) {
        return luaL_error(L, "a checkbox's %s is not a value it can use", key);
      }
      lua_pushvalue(L, 2);
      lua_rawget(L, -2);
      const bool same = lua_rawequal(L, -1, 3);
      lua_pop(L, 1);
      redraw = !same;
    } else if (progress && is_progress_field(key)) {
      if (!progress_field_valid(L, key, 3)) {
        return luaL_error(L, "a progress bar's %s is not a value it can use", key);
      }
      lua_pushvalue(L, 2);
      lua_rawget(L, -2);
      const bool same = lua_rawequal(L, -1, 3);
      lua_pop(L, 1);
      redraw = !same;
    } else if (slider && is_slider_field(key)) {
      if (!slider_field_valid(L, key, 3)) {
        return luaL_error(L, "a slider's %s is not a value it can use", key);
      }
      lua_pushvalue(L, 2);
      lua_rawget(L, -2);
      const bool same = lua_rawequal(L, -1, 3);
      lua_pop(L, 1);
      redraw = !same;
    } else if (row && is_row_field(key)) {
      if (!row_field_valid(L, key, 3)) {
        return luaL_error(L, "a row's %s is not a value it can use", key);
      }
      lua_pushvalue(L, 2);
      lua_rawget(L, -2);
      const bool same = lua_rawequal(L, -1, 3);
      lua_pop(L, 1);
      redraw = !same;
    } else if (panel && is_panel_field(key)) {
      if (!panel_field_valid(L, key, 3)) {
        return luaL_error(L, "a panel's %s is not a value it can use", key);
      }
      lua_pushvalue(L, 2);
      lua_rawget(L, -2);
      const bool same = lua_rawequal(L, -1, 3);
      lua_pop(L, 1);
      redraw = !same;
    } else if (tabs && is_tabs_field(key)) {
      if (!tabs_field_valid(L, key, 3)) {
        return luaL_error(L, "a strip of tabs's %s is not a value it can use", key);
      }
      lua_pushvalue(L, 2);
      lua_rawget(L, -2);
      const bool same = lua_rawequal(L, -1, 3);
      lua_pop(L, 1);
      redraw = !same;
    } else if (dots && is_dots_field(key)) {
      if (!dots_field_valid(L, key, 3)) {
        return luaL_error(L, "the dots of a page's %s is not a value it can use", key);
      }
      lua_pushvalue(L, 2);
      lua_rawget(L, -2);
      const bool same = lua_rawequal(L, -1, 3);
      lua_pop(L, 1);
      redraw = !same;
    } else if (list && is_list_field(key)) {
      if (!list_field_valid(L, key, 3)) {
        return luaL_error(L, "a list's %s is not a value it can use", key);
      }
      lua_pushvalue(L, 2);
      lua_rawget(L, -2);
      const bool same = lua_rawequal(L, -1, 3);
      lua_pop(L, 1);
      redraw = !same;
    } else if (textbox && is_textbox_field(key)) {
      if (!textbox_field_valid(L, key, 3)) {
        return luaL_error(L, "a text box's %s is not a value it can use", key);
      }
      lua_pushvalue(L, 2);
      lua_rawget(L, -2);
      const bool same = lua_rawequal(L, -1, 3);
      lua_pop(L, 1);
      redraw = !same;
    } else if (dialog && is_dialog_field(key)) {
      if (!dialog_field_valid(L, key, 3)) {
        return luaL_error(L, "a dialog's %s is not a value it can use", key);
      }
      lua_pushvalue(L, 2);
      lua_rawget(L, -2);
      const bool same = lua_rawequal(L, -1, 3);
      lua_pop(L, 1);
      redraw = !same;
    } else if (spark && is_spark_field(key)) {
      if (!spark_field_valid(L, key, 3)) {
        return luaL_error(L, "a sparkline's %s is not a value it can use", key);
      }
      // **A chart is not a value that is compared**: two tables are never the same table when the app builds a new
      // one, so a new series is a draw and that is what an app wants (docs/90).
      redraw = true;
    } else if (chart && is_chart_field(key)) {
      if (!chart_field_valid(L, key, 3)) {
        return luaL_error(L, "a chart's %s is not a value it can use", key);
      }
      // **A chart is not a value that is compared**: two tables are never the same table when the app builds a new
      // one, so a new series is a draw and that is what an app wants (docs/90).
      redraw = true;
    } else if (pie && is_pie_field(key)) {
      if (!pie_field_valid(L, key, 3)) {
        return luaL_error(L, "a pie's %s is not a value it can use", key);
      }
      redraw = true;
    } else if (month && is_month_field(key)) {
      if (!month_field_valid(L, key, 3)) {
        return luaL_error(L, "a calendar's %s is not a value it can use", key);
      }
      lua_pushvalue(L, 2);
      lua_rawget(L, -2);
      const bool same = lua_rawequal(L, -1, 3);
      lua_pop(L, 1);
      redraw = !same;
    } else if (menu && is_menu_field(key)) {
      if (!menu_field_valid(L, key, 3)) {
        return luaL_error(L, "a menu's %s is not a value it can use", key);
      }
      // **A menu is not a value that is compared**: the options are a table and a table an app builds again is never the
      // same table, so a new list of options is a draw and that is what an app wants (docs/90).
      if (strcmp(key, "items") == 0) {
        redraw = true;
      } else {
        lua_pushvalue(L, 2);
        lua_rawget(L, -2);
        const bool same = lua_rawequal(L, -1, 3);
        lua_pop(L, 1);
        redraw = !same;
      }
    } else if (menu_bar && is_menu_bar_field(key)) {
      if (!menu_bar_field_valid(L, key, 3)) {
        return luaL_error(L, "a menu bar's %s is not a value it can use", key);
      }
      // The titles and their menus are a table that an app builds again, so a new one is a draw (docs/90).
      if (strcmp(key, "items") == 0) {
        redraw = true;
      } else {
        lua_pushvalue(L, 2);
        lua_rawget(L, -2);
        const bool same = lua_rawequal(L, -1, 3);
        lua_pop(L, 1);
        redraw = !same;
      }
    } else if (multiline && is_textbox_multiline_field(key)) {
      if (!textbox_multiline_field_valid(L, key, 3)) {
        return luaL_error(L, "a box of lines's %s is not a value it can use", key);
      }
      lua_pushvalue(L, 2);
      lua_rawget(L, -2);
      const bool same = lua_rawequal(L, -1, 3);
      lua_pop(L, 1);
      redraw = !same;
    } else if (selector && is_selector_field(key)) {
      if (!selector_field_valid(L, key, 3)) {
        return luaL_error(L, "a selector's %s is not a value it can use", key);
      }
      // The options are a table an app builds again, so a new one is a draw (docs/90).
      if (strcmp(key, "items") == 0) {
        redraw = true;
      } else {
        lua_pushvalue(L, 2);
        lua_rawget(L, -2);
        const bool same = lua_rawequal(L, -1, 3);
        lua_pop(L, 1);
        redraw = !same;
      }
    } else if (notifications && is_notifications_field(key)) {
      if (!notifications_field_valid(L, key, 3)) {
        return luaL_error(L, "a band of notifications's %s is not a value it can use", key);
      }
      // The notices are a table an app builds again, so a new one is a draw (docs/90).
      if (strcmp(key, "items") == 0) {
        redraw = true;
      } else {
        lua_pushvalue(L, 2);
        lua_rawget(L, -2);
        const bool same = lua_rawequal(L, -1, 3);
        lua_pop(L, 1);
        redraw = !same;
      }
    } else if (icon_grid && is_icon_grid_field(key)) {
      if (!icon_grid_field_valid(L, key, 3)) {
        return luaL_error(L, "a grid of cells's %s is not a value it can use", key);
      }
      // The cells are a table an app builds again, so a new one is a draw (docs/90).
      if (strcmp(key, "items") == 0) {
        redraw = true;
      } else {
        lua_pushvalue(L, 2);
        lua_rawget(L, -2);
        const bool same = lua_rawequal(L, -1, 3);
        lua_pop(L, 1);
        redraw = !same;
      }
      // **The cell the dial is on is remembered here** (0.177.3): a step within the page paints the two cells the ring
      // leaves and enters, and the rectangle of them is asked for after the field is written, so the old number has to be
      // read before it is gone (docs/30, docs/90).
      if (strcmp(key, "index") == 0) {
        lua_pushvalue(L, 2);
        lua_rawget(L, -2);
        index_before = lua_isnumber(L, -1) ? lua_tointeger(L, -1) : 0;
        lua_pop(L, 1);
      }
    }
  }
  lua_pushvalue(L, 2);
  lua_pushvalue(L, 3);
  lua_rawset(L, -3);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  // **A cursor that walked within its page repaints the cell it left and the cell it entered** (0.177.3): the ring is the
  // only thing a step changed, and a launcher that repainted its whole page of pictures for it —every cell of a grid of
  // entries is damaged again by its own invalidation, because the paper of the grid covers them— (docs/30, docs/90).
  if (key != nullptr && strcmp(key, "index") == 0 && redraw) {
    const lua_Integer index_after = lua_isnumber(L, 3) ? lua_tointeger(L, 3) : 0;
    // **The two cells are the numbers of the cursor and not a ring that is there**: the grid of entries has the cursor and
    // the ring is each cell's own, and it is the **page** that has to be compared in both kinds —two cells of the page, or
    // the whole control when the page turns— (docs/30, docs/90).
    const uint8_t from = (index_before >= 1) ? static_cast<uint8_t>(index_before) : 0;
    const uint8_t to = (index_after >= 1) ? static_cast<uint8_t>(index_after) : 0;
    if (ui_invalidate_grid_ring(L, 1, from, to, true)) redraw = false;
  }
  // **A control that stops being focusable gives the focus back**: leaving the ring on it would be a
  // control the dialog says is out of the walk and still looks like the one Enter activates. The first
  // control that becomes focusable takes the empty focus, the same way a new button does.
  // **A control that is disabled repaints, and if it had the dial it gives it back** (0.100.0): the tones it is
  // drawn with change, and leaving the ring on a control that no key can reach is a page that lies about where a
  // press will land.
  if (key != nullptr && strcmp(key, "disabled") == 0) {
    redraw = true;
    const bool off = lua_toboolean(L, 3) != 0;
    // **I el control se'l queda** (0.190.144): `disabled` was handed to the **children** and never written into the state table of the
    // control itself —where `ui_disabled_ink` and `ui_field_bool` both read it—, o sigui que un `ui.button`, que porta el títol a dins i
    // no té cap fill, es pintava ple i no es podia prémer: una pàgina que diu que una cosa està disponible quan no ho està. Ho va trobar
    // el botó de `Sync from the network` dels ajustos (docs/30, docs/45).
    ui_set_field_bool(L, 1, "disabled", off);
    ui_set_disabled_below(L, 1, off, 0);
    if (off && g_ui_focus_slot == canvas->slot && g_ui_focus_generation == canvas->generation) {
      g_ui_focus_slot = -1;
      g_ui_focus_generation = 0;
      g_ui_focus_item = 0;
      lua_pushboolean(L, 0);
      lua_setfield(L, -2, "focused");
    }
  }
  if (key != nullptr && strcmp(key, "focusable") == 0) {
    // **`focusable` is true or false** (0.106.0): it is the one field that decides whether the dial, the finger and the
    // middle key can land on a control, so a typo in it is a control nobody can reach and not a value to guess.
    if (lua_type(L, 3) != LUA_TBOOLEAN) {
      return luaL_error(L, "a control's focusable is true or false");
    }
    const bool wanted = lua_toboolean(L, 3) != 0;
    const bool focused = g_ui_focus_slot == canvas->slot &&
                         g_ui_focus_generation == canvas->generation;
    const bool focus_gone =
        g_ui_focus_slot < 0 ||
        (g_ui_ops.canvas_alive != nullptr &&
         !g_ui_ops.canvas_alive(g_ui_focus_slot, g_ui_focus_generation));
    if (!wanted && focused) {
      g_ui_focus_slot = -1;
      g_ui_focus_generation = 0;
      // A control that takes the dial is a stop of its own unless it is a strip, which says its tab next.
      g_ui_focus_item = 0;
      lua_pushboolean(L, 0);
      lua_setfield(L, -2, "focused");
      redraw = true;
    } else if (wanted && focus_gone && !g_ui_focus_back_to_the_band) {
      // **Excepte quan el focus és de la caixa que el teclat escriu** (0.179.81): the empty focus of a page built again under a
      // band is not a choice —the band says which box a person is typing into— and the first focusable control of the rebuild
      // took it, which is why entering the clock's note left the dial on the eye (docs/30, docs/90).
      g_ui_focus_slot = canvas->slot;
      g_ui_focus_generation = canvas->generation;
      // A control that takes the dial is a stop of its own unless it is a strip, which says its tab next.
      g_ui_focus_item = 0;
      lua_pushboolean(L, 1);
      lua_setfield(L, -2, "focused");
      redraw = true;
    }
  }
  lua_pop(L, 1);
  if (redraw && canvas->slot >= 0 && g_ui_ops.canvas_invalidate != nullptr) {
    g_ui_ops.canvas_invalidate(canvas->slot);
  }
  return 0;
}

// **The list of children of the app and the list of the system have to say the same thing**, or the draw
// walks a tree that is not the one being composed. Both indices are absolute.
void append_child(lua_State* L, int parent_index, int child_index) {
  lua_getiuservalue(L, parent_index, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return;
  }
  lua_getfield(L, -1, "children");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_createtable(L, 4, 0);
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, "children");
  }
  lua_pushvalue(L, child_index);
  lua_rawseti(L, -2, static_cast<lua_Integer>(lua_rawlen(L, -2)) + 1);
  lua_pop(L, 2);  // the list and the fields
}

bool canvas_ready(lua_State* L, int index) {
  if (g_ui_ops.canvas_alive == nullptr) return false;
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(L, index, kCanvasMeta));
  if (canvas == nullptr) return false;
  return g_ui_ops.canvas_alive(canvas->slot, canvas->generation);
}

int refuse_canvas(lua_State* L) {
  return push_nil_error(L, "E_INVAL", "this canvas is not on the screen any more");
}

// ui.canvas(width, height): a canvas of its own inside the view of this app, white and with nothing
// drawn. It is composed into the view when the app presents, at the place it was given (docs/90).
// **A control and a canvas are the same thing with one difference: the view.** `ui.canvas(w, h)` makes a
// control with a buffer of its own —an image, a graph, a panel that is composed as a layer— and
// `ui.control(w, h)` makes one without it, which paints into the first canvas that holds it, cut at its own
// frame. A grid, a box or a spacer is the second kind, and it costs no pixels at all (docs/90).
int create_control(lua_State* L, bool with_view) {
  if (g_ui_ops.canvas_create == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no display");
  }
  const lua_Integer width = luaL_checkinteger(L, 1);
  const lua_Integer height = luaL_checkinteger(L, 2);
  if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
    return push_nil_error(L, "E_INVAL", "a control is a size in pixels");
  }
  int16_t slot = -1;
  uint16_t generation = 0;
  if (!g_ui_ops.canvas_create(static_cast<uint16_t>(width), static_cast<uint16_t>(height), with_view,
                              slot, generation)) {
    return push_nil_error(L, "E_NOMEM",
                          with_view ? "there is no room for another view of that size"
                                    : "there is no room for another control");
  }
  LuaCanvas* canvas = static_cast<LuaCanvas*>(lua_newuserdatauv(L, sizeof(LuaCanvas), 1));
  canvas->slot = slot;
  canvas->generation = generation;
  luaL_getmetatable(L, kCanvasMeta);
  lua_setmetatable(L, -2);
  // The table the app's own fields live in: `on_draw`, `children`, and whatever a control needs.
  lua_createtable(L, 0, 2);
  lua_setiuservalue(L, -2, 1);
  // **It is born a child of the view**, so the two trees agree from the first line: the system already
  // put it inside the view, and the app's list of children has to say the same or `draw()` walks a tree
  // that is not the one being composed (found running `layers` on the emulator, 1/11: nothing drew).
  const int made = lua_gettop(L);
  lua_pushvalue(L, made);
  const int kept = lua_gettop(L);
  if (l_ui_root(L) == 1) {
    append_child(L, lua_gettop(L), kept);
    lua_pop(L, 1);
  }
  lua_pop(L, 1);  // the copy of the object
  return 1;
}

int l_ui_canvas(lua_State* L) { return create_control(L, true); }

int l_ui_control(lua_State* L) { return create_control(L, false); }

// **The state of a control that is disabled** (0.100.0). A control can be off: it is still on the page —it is
// drawn, so a person reads what is not available right now— but its ink falls to the lighter tone of its own
// theme (`dim`, the same level a secondary word already uses), it never wears the ring of the focus, and it is not
// a stop of the walk: `ui_target_is_focusable` refuses it, and the dial, the finger and the keyboard all ask that
// one question (docs/30, docs/92).
//
// **The levels are not guessed here**: each painter is handed explicit tones, so this only says which of them are
// the ink. `background` stays paper —a disabled control is still a page and not a hole—, and so does the `track` of
// a slider, which is the paper of the tube.
struct DisabledInk {
  bool on;
  uint8_t dim;
};

DisabledInk ui_disabled_ink(lua_State* L, int object) {
  DisabledInk ink = {false, 8};
  object = lua_absindex(L, object);
  lua_getiuservalue(L, object, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return ink;
  }
  lua_getfield(L, -1, "disabled");
  ink.on = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  if (ink.on) {
    lua_getfield(L, -1, "dim");
    const lua_Integer dim = lua_isnumber(L, -1) ? lua_tointeger(L, -1) : 8;
    ink.dim = static_cast<uint8_t>((dim < 0) ? 0 : ((dim > 15) ? 15 : dim));
  }
  lua_pop(L, 1);
  return ink;
}

// The level a painter asked for, or the lighter tone of the control when it is off.
uint8_t ink_or_dim(const DisabledInk& off, int level) {
  if (off.on) return off.dim;
  return static_cast<uint8_t>(level);
}

// **A control hands its state to its children** (0.100.0): a disabled button dims the label it holds and gives it
// back when it is enabled again, because the box and the word inside it are one thing to the person reading the
// page. The tree is the app's own table of children, so this walks it with a depth cap; a child that goes off is
// repainted (its tones changed) and a child that had the dial gives it back, because no key can reach it any more
// (docs/30, docs/92).
void ui_set_disabled_below(lua_State* L, int object, bool off, int depth) {
  if (depth > 16) return;
  object = lua_absindex(L, object);
  lua_getiuservalue(L, object, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return;
  }
  const int fields = lua_gettop(L);
  lua_getfield(L, fields, "children");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 2);
    return;
  }
  const lua_Integer total = static_cast<lua_Integer>(lua_rawlen(L, -1));
  for (lua_Integer i = 1; i <= total; ++i) {
    lua_rawgeti(L, -1, i);
    LuaCanvas* kid = static_cast<LuaCanvas*>(luaL_testudata(L, -1, kCanvasMeta));
    if (kid != nullptr) {
      lua_getiuservalue(L, -1, 1);
      if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "__off_father");
        const bool by_father = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        lua_getfield(L, -1, "disabled");
        const bool already = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        // **A child the app turned off itself is not given back by its father** (0.106.0): the father's state is the
        // state of what it holds, and a child that says `disabled` of its own came from the app and not from here —
        // `__off_father` is the difference between the two, and it is a field of the system that an app does not
        // write—. Turning a panel back on gives back the children it took, and leaves the ones the app turned off.
        // **Only the child the father is the one turning off is the father's** (0.110.0): a child that was already
        // off was turned off by the app, and the father that comes back at it has to leave it where the app put it —
        // the first version of this walk wrote its own mark over the app's, so `inside.disabled = true` was forgotten
        // by `panel.disabled = true` followed by `panel.disabled = false` (found by the test of its own rule, 8/11).
        bool touched = false;
        if (off && !already) {
          lua_pushboolean(L, 1);
          lua_setfield(L, -2, "disabled");
          lua_pushboolean(L, 1);
          lua_setfield(L, -2, "__off_father");
          touched = true;
        } else if (!off && by_father) {
          lua_pushboolean(L, 0);
          lua_setfield(L, -2, "disabled");
          lua_pushboolean(L, 0);
          lua_setfield(L, -2, "__off_father");
          touched = true;
        }
        if (touched) {
          if (off && g_ui_focus_slot == kid->slot && g_ui_focus_generation == kid->generation) {
            g_ui_focus_slot = -1;
            g_ui_focus_generation = 0;
            g_ui_focus_item = 0;
            lua_pushboolean(L, 0);
            lua_setfield(L, -2, "focused");
          }
          if (g_ui_ops.canvas_invalidate != nullptr) g_ui_ops.canvas_invalidate(kid->slot);
          lua_pop(L, 1);
          ui_set_disabled_below(L, lua_gettop(L), off, depth + 1);
          lua_pop(L, 1);  // the child
          continue;
        }
      }
      lua_pop(L, 1);
    }
    lua_pop(L, 1);  // the child
  }
  lua_pop(L, 2);  // the children and the fields table
}

// **The `on_draw` of a label**: the fields the app put on the object are the whole state of the line, and
// the kernel is the one that owns the metrics, the ellipsis and the alignment. It draws nothing if the label
// is gone, and it returns no value: a draw is a question the presenter asks, not an answer (docs/30,
// docs/90).
int l_label_draw(lua_State* L) {
  if (!canvas_ready(L, 1)) return 0;
  // What this control is in: on, or off and drawn in its lighter tone.
  const DisabledInk off = ui_disabled_ink(L, 1);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  lua_getiuservalue(L, 1, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  lua_getfield(L, -1, "text");
  const char* text = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
  lua_pop(L, 1);
  lua_getfield(L, -1, "color");
  int color = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "background");
  int background = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  // **A label on an inverted face swaps its tones** (0.101.0): a word inside a button that has the ring or the
  // finger is paper on ink and not a white hole in a black box, which is what a label with its own paper did
  // (measured on the emulator, 8/11: the button came out as a black bar under a white rectangle). Turning the page
  // lists it is the same rule one level down (docs/92).
  if (lua_toboolean(L, 2)) {
    const int swap = color;
    color = background;
    background = swap;
  }
  lua_getfield(L, -1, "scale");
  const int scale = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "align");
  const char* align_text = lua_isstring(L, -1) ? lua_tostring(L, -1) : "left";
  const int align = (strcmp(align_text, "right") == 0)
                        ? 2
                        : ((strcmp(align_text, "center") == 0 || strcmp(align_text, "centre") == 0) ? 1 : 0);
  lua_pop(L, 1);
  lua_getfield(L, -1, "valign");
  const char* valign_text = lua_isstring(L, -1) ? lua_tostring(L, -1) : "top";
  const int valign = (strcmp(valign_text, "bottom") == 0) ? 2 : ((strcmp(valign_text, "middle") == 0) ? 1 : 0);
  lua_pop(L, 1);
  lua_pop(L, 1);  // the table of the object's own fields
  if (g_ui_ops.label_draw != nullptr) {
    g_ui_ops.label_draw(canvas->slot, text, ink_or_dim(off, color),
                        static_cast<uint8_t>(background), static_cast<uint8_t>(align),
                        static_cast<uint8_t>(valign),
                        static_cast<uint8_t>((scale == 0) ? 1 : scale));
  }
  return 0;
}

// ui.label(width, height): **a control without a view that is one line of text**, with a background colour,
// a text colour, a face (the one `cv:font` gives it), two alignments and a body size (docs/30, docs/90).
// The fields are the app's: `text`, `color`, `background`, `align`, `valign` and `scale`.
int l_ui_label(lua_State* L) {
  const int results = create_control(L, false);
  if (results != 1 || !lua_isuserdata(L, -1)) return results;
  lua_getiuservalue(L, -1, 1);
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "__label");
  lua_pushstring(L, "");
  lua_setfield(L, -2, "text");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "color");
  lua_pushinteger(L, 15);
  lua_setfield(L, -2, "background");
  lua_pushstring(L, "left");
  lua_setfield(L, -2, "align");
  lua_pushstring(L, "top");
  lua_setfield(L, -2, "valign");
  // **The size of the system and not the app's**: four steps is a body of thirty-six pixels, which is the text
  // of the paper project (its `ui36b`), and a control an app does not configure looks like the rest of the glass
  // (docs/92). Four for a strip of words, which is the dense size of three (its `fontSmall`).
  lua_pushinteger(L, 4);
  lua_setfield(L, -2, "scale");
  lua_pushcfunction(L, &l_label_draw);
  lua_setfield(L, -2, "on_draw");
  lua_pop(L, 1);  // the table
  return 1;
}

// **The `on_draw` of a button**: the frame is a fill and a border, a second border while it is held and
// another one two pixels in while it has the focus. The child of a button is not a field here: it is
// another control in the tree, added with `cv:add`, and the tree composes it after the button and clips
// it to the same frame (docs/30, docs/90).
int l_button_draw(lua_State* L) {
  if (!canvas_ready(L, 1)) return 0;
  // What this control is in: on, or off and drawn in its lighter tone.
  const DisabledInk off = ui_disabled_ink(L, 1);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  lua_getiuservalue(L, 1, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  lua_getfield(L, -1, "background");
  const int background = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "border");
  const int border = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "pressed");
  const bool pressed = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  lua_getfield(L, -1, "focused");
  const bool focused = lua_toboolean(L, -1) != 0 && g_ui_ring_shown;
  lua_pop(L, 1);
  // **The word, the tag, a primary button, the ink of the word and its size** (0.97.0): the control draws the text it
  // shows, so an inverted button has an inverted word —a label the app added is a control of the app and keeps the
  // colours the app gave it— (docs/30, docs/92).
  lua_getfield(L, -1, "primary");
  const bool primary = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  lua_getfield(L, -1, "color");
  const int color = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "scale");
  const int scale = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  // **The two words are copied and not pointed at**: the string a `lua_tostring` hands over lives in the interpreter and
  // the drawing happens with the value already popped (docs/75).
  static char title[128];
  static char tag[32];
  title[0] = '\0';
  tag[0] = '\0';
  lua_getfield(L, -1, "title");
  if (lua_isstring(L, -1)) {
    const char* said = lua_tostring(L, -1);
    size_t used = 0;
    for (; said != nullptr && said[used] != '\0' && used + 1 < sizeof(title); ++used) title[used] = said[used];
    title[used] = '\0';
  }
  lua_pop(L, 1);
  lua_getfield(L, -1, "tag");
  if (lua_isstring(L, -1)) {
    const char* said = lua_tostring(L, -1);
    size_t used = 0;
    for (; said != nullptr && said[used] != '\0' && used + 1 < sizeof(tag); ++used) tag[used] = said[used];
    tag[used] = '\0';
  }
  lua_pop(L, 1);
  // **Sense vores i amb una icona** (0.185.0, l'amo: *«vull que siguin UIButton, però sense vores … i amb una imatge/icona com a
  // contingut»*): `framed` false is a key that is its mark and nothing else, and `icon` names a mark of the system —the five of
  // `ui.icons()`— that is drawn in the place of the word. An icon that is not a name of the set draws nothing, which is a key
  // with no content and not a picture of an accident (docs/30, docs/92).
  lua_getfield(L, -1, "framed");
  const bool framed_field = lua_isboolean(L, -1) ? (lua_toboolean(L, -1) != 0) : true;
  lua_pop(L, 1);
  static char icon[16];
  icon[0] = '\0';
  lua_getfield(L, -1, "icon");
  if (lua_isstring(L, -1)) {
    const char* said = lua_tostring(L, -1);
    size_t used = 0;
    for (; said != nullptr && said[used] != '\0' && used + 1 < sizeof(icon); ++used) icon[used] = said[used];
    icon[used] = '\0';
  }
  lua_pop(L, 1);
  // **On va la marca quan el botó també té paraula** (0.185.3): `icon_side` is `'above'` (the default), `'left'` or `'right'`,
  // and the panel of the bar is what showed why it is needed: the two corners of its card carry the mark on the outer side and a
  // key that always put it in the same place would be a card with two corners that look the same (docs/30, docs/92).
  uint8_t mark_side = 0;
  lua_getfield(L, -1, "icon_side");
  if (lua_isstring(L, -1)) {
    const char* said = lua_tostring(L, -1);
    if (said != nullptr && strcmp(said, "left") == 0) {
      mark_side = 1;
    } else if (said != nullptr && strcmp(said, "right") == 0) {
      mark_side = 2;
    }
  }
  lua_pop(L, 1);
  lua_pop(L, 1);  // the table of the object's own fields
  if (g_ui_ops.button_draw != nullptr) {
    g_ui_ops.button_draw(canvas->slot, static_cast<uint8_t>(background), ink_or_dim(off, border), pressed,
                         focused && !off.on, primary, title, tag,
                         ink_or_dim(off, (color >= 0 && color <= 15) ? color : 0),
                         static_cast<uint8_t>((scale >= 1 && scale <= 16) ? scale : 1), framed_field,
                         (icon[0] != '\0') ? icon : nullptr, mark_side,
                         // **Un botó d'una app no porta cercle** (0.192.46): the ring is the system's way of saying the state of a
                         // radio on its own panel, and a page that wants a circle draws one (docs/30, docs/92).
                         /*ring=*/0);
  }
  return 0;
}

// ui.button(width, height): **a control that is a container with a frame**, and the first one whose child
// is part of what it shows. The fields are the two colours of its frame, the held state and the focus; the
// child is added with `button:add(child)`, and its coordinates are the button's (docs/30, docs/90).
int l_ui_button(lua_State* L) {
  const int results = create_control(L, false);
  if (results != 1 || !lua_isuserdata(L, -1)) return results;
  lua_getiuservalue(L, -1, 1);
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "__button");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "__inverts");
  lua_pushinteger(L, 15);
  lua_setfield(L, -2, "background");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "border");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "pressed");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "focused");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "focusable");
  // **The word, the tag and the size**: empty by default, because a button whose word the app draws as a child is a page
  // of before 0.97.0 and still has to work (docs/30, docs/92).
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "primary");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "color");
  // **The size of the system and not the app's**: 4 steps is a body of thirty-six pixels, the text of the paper project (its `ui36b`), and a control an app does not configure
  // looks like the rest of the glass (docs/92).
  lua_pushinteger(L, 4);
  lua_setfield(L, -2, "scale");
  lua_pushstring(L, "");
  lua_setfield(L, -2, "title");
  lua_pushstring(L, "");
  lua_setfield(L, -2, "tag");
  lua_pushcfunction(L, &l_button_draw);
  lua_setfield(L, -2, "on_draw");
  lua_pop(L, 1);  // the table

  // **The first pressable control is focused when it appears**: a page with controls and no ring gives
  // no clue that the dial does anything, and a board with one key would not have a way to start
  // (docs/30, docs/90).
  LuaCanvas* created = static_cast<LuaCanvas*>(luaL_checkudata(L, -1, kCanvasMeta));
  const bool focus_is_gone =
      g_ui_focus_slot < 0 ||
      (g_ui_ops.canvas_alive != nullptr &&
       !g_ui_ops.canvas_alive(g_ui_focus_slot, g_ui_focus_generation));
  if (focus_is_gone) {
    g_ui_focus_slot = created->slot;
    g_ui_focus_generation = created->generation;
    // A control that takes the dial is a stop of its own unless it is a strip, which says its tab next.
    g_ui_focus_item = 0;
    lua_getiuservalue(L, -1, 1);
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "focused");
    lua_pop(L, 1);
    if (g_ui_ops.canvas_invalidate != nullptr) g_ui_ops.canvas_invalidate(created->slot);
  }
  return 1;
}

// The fields of a control are read by the helpers that live further down this file and the checkbox asks for
// one of them here, before they are defined (docs/30).
bool ui_field_bool(lua_State* L, int object, const char* key, bool fallback);
lua_Integer ui_field_number(lua_State* L, int object, const char* key, lua_Integer fallback);
bool ui_set_field_number(lua_State* L, int object, const char* key, lua_Integer value);
bool ui_call_hook(lua_State* L, int object, const char* name, bool with_flag, bool flag);
bool ui_call_hook_text(lua_State* L, int object, const char* name, const char* text);
void ui_invalidate(lua_State* L, int object);
// **A hook with a number** (0.161.0): `on_change(self, index)` and the like are called from here, and the definition is
// further down because it needs the tables of a control —the same road as the two above—.
bool ui_call_hook_number(lua_State* L, int object, const char* name, lua_Integer number);

// **The options of a menu as the kernel wants them**: the app says a Lua array of words —an option of a label and
// nothing else— or of tables with `label`, `tag`, `hidden` and `primary`, the way a row of a list is a word or a table
// (docs/30, docs/90). The `items` table is **left on the stack** and the caller sets the stack back after the paint:
// the words of an option are pointers into strings that the table holds, and a table that nothing holds any more is a
// table the collector may have taken (docs/75, docs/90).
uint8_t ui_menu_items(lua_State* L, int object, UIOps::UIMenuOption* out) {
  if (out == nullptr) return 0;
  lua_getiuservalue(L, object, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  lua_getfield(L, -1, "items");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 2);
    return 0;
  }
  const lua_Integer total = static_cast<lua_Integer>(lua_rawlen(L, -1));
  uint8_t count = 0;
  for (lua_Integer i = 1; i <= total && count < kMaxMenuItems; ++i) {
    lua_rawgeti(L, -1, i);
    UIOps::UIMenuOption& option = out[count];
    option.label = "";
    option.tag = "";
    option.hidden = false;
    option.primary = false;
    if (lua_istable(L, -1)) {
      lua_getfield(L, -1, "label");
      if (lua_isstring(L, -1)) option.label = lua_tostring(L, -1);
      lua_pop(L, 1);
      lua_getfield(L, -1, "tag");
      if (lua_isstring(L, -1)) option.tag = lua_tostring(L, -1);
      lua_pop(L, 1);
      lua_getfield(L, -1, "hidden");
      option.hidden = lua_toboolean(L, -1) != 0;
      lua_pop(L, 1);
      lua_getfield(L, -1, "primary");
      option.primary = lua_toboolean(L, -1) != 0;
      lua_pop(L, 1);
    } else if (lua_isstring(L, -1)) {
      option.label = lua_tostring(L, -1);
    }
    lua_pop(L, 1);  // the option
    ++count;
  }
  return count;
}

// **The titles of a bar and the entries of their menus, read once**: a title is a table with `title` and `items`, an entry
// is a word —an entry with nothing but a label—, a table with `label`, `checked`, `enabled` and `separator`, or a dash
// for a line between two entries, which is the only character a menu says "air here" with (docs/30, docs/90). The
// `items` table is **left on the stack**, like the options of a menu, because the words are pointers into strings that
// only that table holds.
uint8_t ui_menu_bar_items(lua_State* L, int object, UIOps::UIMenuBarTitle* titles,
                          UIOps::UIMenuBarEntry* entries) {
  if (titles == nullptr || entries == nullptr) return 0;
  lua_getiuservalue(L, object, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  lua_getfield(L, -1, "items");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 2);
    return 0;
  }
  const lua_Integer total = static_cast<lua_Integer>(lua_rawlen(L, -1));
  uint8_t count = 0;
  uint8_t used = 0;
  for (lua_Integer i = 1; i <= total && count < kMaxMenuBarTitles; ++i) {
    lua_rawgeti(L, -1, i);
    UIOps::UIMenuBarTitle& title = titles[count];
    title.label = "";
    title.entries = &entries[used];
    title.count = 0;
    if (lua_istable(L, -1)) {
      lua_getfield(L, -1, "title");
      if (lua_isstring(L, -1)) title.label = lua_tostring(L, -1);
      lua_pop(L, 1);
      lua_getfield(L, -1, "items");
      if (lua_istable(L, -1)) {
        const lua_Integer menu = static_cast<lua_Integer>(lua_rawlen(L, -1));
        for (lua_Integer j = 1; j <= menu && used < kMaxMenuBarEntries; ++j) {
          lua_rawgeti(L, -1, j);
          UIOps::UIMenuBarEntry& entry = entries[used];
          entry.label = "";
          entry.separator = false;
          entry.checked = false;
          entry.checkable = false;
          entry.enabled = true;
          if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "label");
            if (lua_isstring(L, -1)) entry.label = lua_tostring(L, -1);
            lua_pop(L, 1);
            // **An entry with a `checked`, even a `false`, is a setting and carries its box**: the field being there is
            // what the app says with a square in front of the words, and a field that is not there is a command (docs/30,
            // docs/90).
            lua_getfield(L, -1, "checked");
            entry.checkable = !lua_isnil(L, -1);
            entry.checked = lua_toboolean(L, -1) != 0;
            lua_pop(L, 1);
            lua_getfield(L, -1, "enabled");
            entry.enabled = lua_isnil(L, -1) ? true : (lua_toboolean(L, -1) != 0);
            lua_pop(L, 1);
            lua_getfield(L, -1, "separator");
            entry.separator = lua_toboolean(L, -1) != 0;
            lua_pop(L, 1);
          } else if (lua_isstring(L, -1)) {
            const char* word = lua_tostring(L, -1);
            entry.label = word;
            if (word != nullptr && strcmp(word, "-") == 0) entry.separator = true;
          }
          lua_pop(L, 1);  // the entry
          ++used;
          ++title.count;
        }
        lua_pop(L, 1);  // the menu
      }
    } else if (lua_isstring(L, -1)) {
      title.label = lua_tostring(L, -1);
    }
    lua_pop(L, 1);  // the title
    ++count;
  }
  return count;
}

// **The `on_draw` of a checkbox**: the row in its background, the box inside it, the tick when it is checked
// and the ring of the focus. The words are not here —they are a child control, as they are in a button— and
// the geometry of the box is the kernel's, which is why the app asks for it with `cb:box()` (docs/30,
// docs/90).
int l_checkbox_draw(lua_State* L) {
  if (!canvas_ready(L, 1)) return 0;
  // What this control is in: on, or off and drawn in its lighter tone.
  const DisabledInk off = ui_disabled_ink(L, 1);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  lua_getiuservalue(L, 1, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  lua_getfield(L, -1, "background");
  int background = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "border");
  int border = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "mark");
  int mark = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  // **On an inverted face the two tones swap** (0.101.0): a checkbox inside a button with the ring is a paper box
  // with an ink frame and an ink tick, which is what says "this is still a thing you can press" while the face
  // behind it is black (docs/92).
  if (lua_toboolean(L, 2)) {
    const int swap = background;
    background = border;
    border = swap;
    mark = (swap == mark) ? background : swap;
  }
  lua_getfield(L, -1, "checked");
  const bool checked = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  lua_getfield(L, -1, "pressed");
  const bool pressed = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  lua_getfield(L, -1, "focused");
  const bool focused = lua_toboolean(L, -1) != 0 && g_ui_ring_shown;
  lua_pop(L, 1);
  lua_pop(L, 1);  // the table of the object's own fields
  if (g_ui_ops.checkbox_draw != nullptr) {
    g_ui_ops.checkbox_draw(canvas->slot, static_cast<uint8_t>(background), ink_or_dim(off, border),
                           ink_or_dim(off, mark), checked, pressed, focused && !off.on);
  }
  return 0;
}

// **`cb:box()`: how many pixels the box takes**, which is where the label that names the setting goes. It is
// the kernel's number and not a field, so an app cannot draw a box that is not the one the tick lands in. It
// is a method of every control and it only means something on a checkbox, which is why anything else is
// refused instead of reading a number off a grid or a button (docs/30, docs/90).
int l_checkbox_box(lua_State* L) {
  if (!canvas_ready(L, 1)) return refuse_canvas(L);
  if (!ui_field_bool(L, 1, "__checkbox", false)) {
    return push_nil_error(L, "E_INVAL", "only a checkbox has a box");
  }
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  if (g_ui_ops.checkbox_width == nullptr) return push_nil_error(L, "E_UNSUPPORTED", "this board has no display");
  lua_pushinteger(L, g_ui_ops.checkbox_width(canvas->slot));
  return 1;
}

// **How many rows a box of several lines shows**: the app asks it to know how much text fits in the box it made, and the
// arithmetic is the one the paint and the finger use —a copy in Lua would answer a different number the day the frame
// changes— (docs/30, docs/90).
int l_multiline_rows(lua_State* L) {
  if (!canvas_ready(L, 1)) return refuse_canvas(L);
  if (!ui_field_bool(L, 1, "__textbox_multiline", false)) {
    return push_nil_error(L, "E_INVAL", "only a box of several lines has rows");
  }
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  if (g_ui_ops.textbox_multiline_rows == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no display");
  }
  const lua_Integer scale = ui_field_number(L, 1, "scale", 3);
  lua_pushinteger(L, g_ui_ops.textbox_multiline_rows(canvas->slot, static_cast<uint8_t>((scale < 1) ? 1 : scale)));
  return 1;
}

// **How many rows the whole text has**, which is what an app wants to say where it is in the file ("row 12 of 40"):
// the measurement is the one of the paint, like `rows()` (0.88.0, docs/30, docs/90).
int l_multiline_lines(lua_State* L) {
  if (!canvas_ready(L, 1)) return refuse_canvas(L);
  if (!ui_field_bool(L, 1, "__textbox_multiline", false)) {
    return push_nil_error(L, "E_INVAL", "only a box of several lines has lines");
  }
  if (g_ui_ops.textbox_multiline_total == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no display");
  }
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  lua_getiuservalue(L, 1, 1);
  const char* text = "";
  if (lua_istable(L, -1)) {
    lua_getfield(L, -1, "text");
    if (lua_isstring(L, -1)) text = lua_tostring(L, -1);
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  const lua_Integer scale = ui_field_number(L, 1, "scale", 3);
  const bool wrap = ui_field_bool(L, 1, "wrap", false);
  lua_pushinteger(L, g_ui_ops.textbox_multiline_total(canvas->slot, text, wrap,
                                                      static_cast<uint8_t>((scale < 1) ? 1 : scale)));
  return 1;
}

// ui.checkbox(width, height): **a row with a square box and a tick, and the one control whose click means
// one thing**. The fields are the three colours —the row, the border of the box and the tick—, whether it is
// checked, held and focused; the words are a child control, added with `cb:add(label)` and placed after
// `cb:box()` (docs/30, docs/90).
int l_ui_checkbox(lua_State* L) {
  const int results = create_control(L, false);
  if (results != 1 || !lua_isuserdata(L, -1)) return results;
  lua_getiuservalue(L, -1, 1);
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "__checkbox");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "__inverts");
  lua_pushinteger(L, 15);
  lua_setfield(L, -2, "background");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "border");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "mark");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "checked");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "pressed");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "focused");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "focusable");
  lua_pushcfunction(L, &l_checkbox_draw);
  lua_setfield(L, -2, "on_draw");
  lua_pop(L, 1);  // the table

  // **The first pressable control is focused when it appears**, the same rule a button follows: a page of
  // checkboxes with no ring gives no clue that the dial does anything (docs/30, docs/90).
  LuaCanvas* created = static_cast<LuaCanvas*>(luaL_checkudata(L, -1, kCanvasMeta));
  const bool focus_is_gone =
      g_ui_focus_slot < 0 ||
      (g_ui_ops.canvas_alive != nullptr &&
       !g_ui_ops.canvas_alive(g_ui_focus_slot, g_ui_focus_generation));
  if (focus_is_gone) {
    g_ui_focus_slot = created->slot;
    g_ui_focus_generation = created->generation;
    // A control that takes the dial is a stop of its own unless it is a strip, which says its tab next.
    g_ui_focus_item = 0;
    lua_getiuservalue(L, -1, 1);
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "focused");
    lua_pop(L, 1);
    if (g_ui_ops.canvas_invalidate != nullptr) g_ui_ops.canvas_invalidate(created->slot);
  }
  return 1;
}

// **The `on_draw` of a progress bar**: the track and the fill of it, and nothing the app has to compute. The
// value is the field `value`, 0 to 100, and a negative one is "nothing to say yet" (docs/30, docs/90).
int l_progress_draw(lua_State* L) {
  if (!canvas_ready(L, 1)) return 0;
  // What this control is in: on, or off and drawn in its lighter tone.
  const DisabledInk off = ui_disabled_ink(L, 1);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  lua_getiuservalue(L, 1, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  lua_getfield(L, -1, "background");
  const int background = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "border");
  const int border = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "track");
  const int track = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "fill");
  const int fill = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "value");
  const int value = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_pop(L, 1);  // the table of the object's own fields
  if (g_ui_ops.progress_draw != nullptr) {
    g_ui_ops.progress_draw(canvas->slot, static_cast<uint8_t>(background), ink_or_dim(off, border),
                           static_cast<uint8_t>(track), ink_or_dim(off, fill),
                           static_cast<int16_t>(value));
  }
  return 0;
}

// ui.progress(width, height): **how much of something is done**, a track and a fill. It is not a control a
// finger lands on: no focus, no ring, and a frame of one pixel. The fields are the four colours and the
// number (docs/30, docs/90).
int l_ui_progress(lua_State* L) {
  const int results = create_control(L, false);
  if (results != 1 || !lua_isuserdata(L, -1)) return results;
  lua_getiuservalue(L, -1, 1);
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "__progress");
  lua_pushinteger(L, 15);
  lua_setfield(L, -2, "background");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "border");
  lua_pushinteger(L, 15);
  lua_setfield(L, -2, "track");
  // **The fill is the light tone of the paper project** and not the ink: a bar filled with ink is a hundred thousand
  // pixels of black to say one number, and level ten is one of the four the cheapest wash of this panel reproduces
  // exactly (docs/50, docs/92). The ink that says where the bar has got to is the ring the drawing puts on it.
  lua_pushinteger(L, 10);
  lua_setfield(L, -2, "fill");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "value");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "focusable");
  lua_pushcfunction(L, &l_progress_draw);
  lua_setfield(L, -2, "on_draw");
  lua_pop(L, 1);  // the table
  return 1;
}

// **The `on_draw` of a slider**: the frame, the track, the knob and the number in the right column. The
// arrows are not here: the system moves the value and calls `on_change` (docs/30, docs/90).
int l_slider_draw(lua_State* L) {
  if (!canvas_ready(L, 1)) return 0;
  // What this control is in: on, or off and drawn in its lighter tone.
  const DisabledInk off = ui_disabled_ink(L, 1);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  lua_getiuservalue(L, 1, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  lua_getfield(L, -1, "background");
  const int background = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "border");
  const int border = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "track");
  const int track = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "fill");
  const int fill = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "text");
  const int text = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "value");
  const int value = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "min");
  const int minimum = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "max");
  const int maximum = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "scale");
  const int scale = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "suffix");
  const char* suffix = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
  lua_pop(L, 1);
  lua_getfield(L, -1, "pressed");
  const bool pressed = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  lua_getfield(L, -1, "focused");
  const bool focused = lua_toboolean(L, -1) != 0 && g_ui_ring_shown;
  lua_pop(L, 1);
  lua_pop(L, 1);  // the table of the object's own fields
  if (g_ui_ops.slider_draw != nullptr) {
    g_ui_ops.slider_draw(canvas->slot, static_cast<uint8_t>(background), ink_or_dim(off, border),
                         static_cast<uint8_t>(track), ink_or_dim(off, fill),
                         ink_or_dim(off, text), static_cast<int16_t>(value),
                         static_cast<int16_t>(minimum), static_cast<int16_t>(maximum),
                         suffix, static_cast<uint8_t>((scale == 0) ? 1 : scale), pressed,
                         focused && !off.on);
  }
  return 0;
}

// ui.slider(width, height): **a number that the arrows move**. The fields are the four colours of the frame
// and the track, the size of the number, the value and its limits, the step and the unit; the moving is the
// system's and the app hears about it at `on_change(self, value)` (docs/30, docs/90).
int l_ui_slider(lua_State* L) {
  const int results = create_control(L, false);
  if (results != 1 || !lua_isuserdata(L, -1)) return results;
  lua_getiuservalue(L, -1, 1);
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "__slider");
  lua_pushinteger(L, 15);
  lua_setfield(L, -2, "background");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "border");
  // **The track is the paper of the page**, as in the paper project: a tube of paper with an outline of ink, and what
  // the value has taken is filled with that ink (docs/92).
  lua_pushinteger(L, 15);
  lua_setfield(L, -2, "track");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "fill");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "text");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "value");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "min");
  lua_pushinteger(L, 100);
  lua_setfield(L, -2, "max");
  lua_pushinteger(L, 5);
  lua_setfield(L, -2, "step");
  // **The size of the system and not the app's**: 4 steps is a body of thirty-six pixels (its `ui36b`), and a control an app does not
  // configure looks like the rest of the glass (docs/92).
  lua_pushinteger(L, 4);
  lua_setfield(L, -2, "scale");
  lua_pushstring(L, "");
  lua_setfield(L, -2, "suffix");
  // **El rang físic de la sèrie**: zero i cent per defecte, que és el d'una bateria, i el que l'app digui per a qualsevol
  // altra cosa —l'aire d'un punt no el pot creuar— (docs/90).
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "min");
  lua_pushinteger(L, 100);
  lua_setfield(L, -2, "max");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "pressed");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "focused");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "focusable");
  lua_pushcfunction(L, &l_slider_draw);
  lua_setfield(L, -2, "on_draw");
  lua_pop(L, 1);  // the table

  // The first pressable control is focused when it appears, the same rule a button follows (docs/30, docs/90).
  LuaCanvas* created = static_cast<LuaCanvas*>(luaL_checkudata(L, -1, kCanvasMeta));
  const bool focus_is_gone =
      g_ui_focus_slot < 0 ||
      (g_ui_ops.canvas_alive != nullptr &&
       !g_ui_ops.canvas_alive(g_ui_focus_slot, g_ui_focus_generation));
  if (focus_is_gone) {
    g_ui_focus_slot = created->slot;
    g_ui_focus_generation = created->generation;
    // A control that takes the dial is a stop of its own unless it is a strip, which says its tab next.
    g_ui_focus_item = 0;
    lua_getiuservalue(L, -1, 1);
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "focused");
    lua_pop(L, 1);
    if (g_ui_ops.canvas_invalidate != nullptr) g_ui_ops.canvas_invalidate(created->slot);
  }
  return 1;
}

// **The `on_draw` of an entry of a list** (0.104.0): the paper of the row —its ink when it is the marked one or the
// one with the ring— and the line of the separator. **It draws no words**, because the words are the control the app
// put inside it, and that is the whole difference between a list of data and a list of entries (docs/30, docs/90).
int l_entry_draw(lua_State* L) {
  if (!canvas_ready(L, 1)) return 0;
  const DisabledInk off = ui_disabled_ink(L, 1);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  if (g_ui_ops.entry_draw == nullptr) return 0;
  lua_getiuservalue(L, 1, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  lua_getfield(L, -1, "background");
  const int background = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "border");
  const int border = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "separator");
  const bool separator = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  lua_getfield(L, -1, "marked");
  const bool marked = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  lua_getfield(L, -1, "pressed");
  const bool pressed = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  lua_getfield(L, -1, "focused");
  const bool focused = lua_toboolean(L, -1) != 0 && g_ui_ring_shown;
  lua_pop(L, 1);
  lua_pop(L, 1);  // the fields table
  g_ui_ops.entry_draw(canvas->slot, static_cast<uint8_t>(background), ink_or_dim(off, border),
                      separator, marked, (pressed || focused) && !off.on);
  return 0;
}

// **The `on_draw` of a row**: the title and the detail on one line, the subtitle under it, and the line of the
// separator. It takes no focus: a row is the drawing of a list and the list is the app's page (docs/30,
// docs/90).
int l_row_draw(lua_State* L) {
  if (!canvas_ready(L, 1)) return 0;
  // What this control is in: on, or off and drawn in its lighter tone.
  const DisabledInk off = ui_disabled_ink(L, 1);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  lua_getiuservalue(L, 1, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  lua_getfield(L, -1, "title");
  const char* title = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
  lua_pop(L, 1);
  lua_getfield(L, -1, "detail");
  const char* detail = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
  lua_pop(L, 1);
  lua_getfield(L, -1, "subtitle");
  const char* subtitle = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
  lua_pop(L, 1);
  lua_getfield(L, -1, "text");
  int text = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "dim");
  const int dim = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "background");
  int background = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  // **On an inverted face the two tones swap** (0.101.0), so a row inside a button keeps saying what it says
  // instead of painting a white strip over the black one (docs/92).
  if (lua_toboolean(L, 2)) {
    const int swap = text;
    text = background;
    background = swap;
  }
  lua_getfield(L, -1, "scale");
  const int scale = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "marked");
  const bool marked = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  lua_getfield(L, -1, "separator");
  const bool separator = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  lua_pop(L, 1);  // the table of the object's own fields
  if (g_ui_ops.row_draw != nullptr) {
    g_ui_ops.row_draw(canvas->slot, title, detail, subtitle, ink_or_dim(off, text),
                      static_cast<uint8_t>(dim), static_cast<uint8_t>(background),
                      static_cast<uint8_t>((scale == 0) ? 1 : scale), marked, separator);
  }
  return 0;
}

// ui.row(width, height): **one line of a list**: a title, a detail on the right, a subtitle under the title,
// and the line of the separator. It takes no focus and it draws nothing but itself, so a list of ten rows is
// ten of these and no state of its own (docs/30, docs/90).
int l_ui_row(lua_State* L) {
  const int results = create_control(L, false);
  if (results != 1 || !lua_isuserdata(L, -1)) return results;
  lua_getiuservalue(L, -1, 1);
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "__row");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "__inverts");
  lua_pushstring(L, "");
  lua_setfield(L, -2, "title");
  lua_pushstring(L, "");
  lua_setfield(L, -2, "detail");
  lua_pushstring(L, "");
  lua_setfield(L, -2, "subtitle");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "text");
  lua_pushinteger(L, 8);
  lua_setfield(L, -2, "dim");
  lua_pushinteger(L, 15);
  lua_setfield(L, -2, "background");
  // **The size of the system and not the app's**: 4 steps is a body of thirty-six pixels (its `ui36b`), and a control an app does not
  // configure looks like the rest of the glass (docs/92).
  lua_pushinteger(L, 4);
  lua_setfield(L, -2, "scale");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "marked");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "separator");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "focusable");
  lua_pushcfunction(L, &l_row_draw);
  lua_setfield(L, -2, "on_draw");
  lua_pop(L, 1);  // the table
  return 1;
}

// **The `on_draw` of a panel**: it paints nothing —a panel is a room— and what it does is arrange the controls
// it holds. The layout happens here and not when the app adds them, because the frame of a panel is known when
// it is placed: a page that is resized or turned has to be arranged again without the app saying so
// (docs/30, docs/90).
int l_panel_draw(lua_State* L) {
  pal::MemoryTag who_asks(pal::kMemoryLua, app_id());
  if (!canvas_ready(L, 1)) return 0;
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  lua_getiuservalue(L, 1, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  lua_getfield(L, -1, "flow");
  const char* flow_text = lua_isstring(L, -1) ? lua_tostring(L, -1) : "vertical";
  uint8_t flow = 0;
  if (strcmp(flow_text, "horizontal") == 0) {
    flow = 1;
  } else if (strcmp(flow_text, "overlay") == 0) {
    flow = 2;
  }
  lua_pop(L, 1);
  lua_getfield(L, -1, "padding");
  const int padding = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "gap");
  const int gap = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  if (g_ui_ops.panel_layout == nullptr) {
    lua_pop(L, 1);
    return 0;
  }
  // **The weights are the app's fields and the arithmetic is the kernel's**, and the list is as long as the app's
  // panel is: sixteen of them live on the stack and a page of forty rows takes a block of the extra memory, because
  // the sixteen was the stack talking and not the memory (0.102.0, docs/75, docs/90).
  UIOps::UIPanelChild on_stack[16];
  lua_getfield(L, -1, "children");
  lua_Integer total = 0;
  if (lua_istable(L, -1)) total = static_cast<lua_Integer>(lua_rawlen(L, -1));
  UIOps::UIPanelChild* kids = on_stack;
  void* block = nullptr;
  if (total > 16) {
    block = pal::extra_memory().allocate(sizeof(UIOps::UIPanelChild) * static_cast<size_t>(total), true);
    if (block != nullptr) kids = static_cast<UIOps::UIPanelChild*>(block);
  }
  const lua_Integer room = static_cast<lua_Integer>((block != nullptr || total <= 16) ? total : 16);
  uint16_t count = 0;
  if (lua_istable(L, -1)) {
    for (lua_Integer i = 1; i <= room; ++i) {
      lua_rawgeti(L, -1, i);
      LuaCanvas* child = static_cast<LuaCanvas*>(luaL_testudata(L, -1, kCanvasMeta));
      if (child != nullptr && child->slot >= 0) {
        kids[count].slot = child->slot;
        kids[count].weight = static_cast<uint8_t>(ui_field_number(L, -1, "weight", 0));
        // **`cell.share`: a percentage of the inside of the box** (0.103.0). Fifty is half of it, and zero —the
        // normal case— asks for the weight instead (docs/30, docs/90).
        const lua_Integer share = ui_field_number(L, -1, "share", 0);
        kids[count].share = static_cast<uint8_t>((share < 0) ? 0 : ((share > 100) ? 100 : share));
        ++count;
      }
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);  // the children
  lua_pop(L, 1);  // the fields table
  if (count > 0) {
    g_ui_ops.panel_layout(canvas->slot, flow, static_cast<uint16_t>(padding),
                          static_cast<uint16_t>(gap), kids, count);
  }
  if (block != nullptr) pal::extra_memory().release(block);
  return 0;
}

// ui.panel(width, height [, flow]): **the room that arranges what it holds**. `flow` is `'vertical'` —a
// column—, `'horizontal'` —a row— or `'overlay'` —everything on top of everything—, and the fields are the
// flow, the padding and the gap. A child takes the room it was made with, or a share of the free room when the
// app gives it a `weight` (docs/30, docs/90).
int l_ui_panel(lua_State* L) {
  const char* flow = lua_isstring(L, 3) ? lua_tostring(L, 3) : "vertical";
  if (strcmp(flow, "vertical") != 0 && strcmp(flow, "horizontal") != 0 &&
      strcmp(flow, "overlay") != 0) {
    return push_nil_error(L, "E_INVAL", "a panel is vertical, horizontal or overlay");
  }
  // **The word is copied and not kept**: taking the arguments off the stack leaves a pointer into a string
  // that nothing holds any more, and a step of the collector in the middle of making the control would make
  // the panel ask for a name that is not there (docs/75, docs/90).
  char flow_copy[16];
  snprintf(flow_copy, sizeof(flow_copy), "%s", flow);
  lua_settop(L, 2);
  const int results = create_control(L, false);
  if (results != 1 || !lua_isuserdata(L, -1)) return results;
  lua_getiuservalue(L, -1, 1);
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "__panel");
  lua_pushstring(L, flow_copy);
  lua_setfield(L, -2, "flow");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "padding");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "gap");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "focusable");
  lua_pushcfunction(L, &l_panel_draw);
  lua_setfield(L, -2, "on_draw");
  lua_pop(L, 1);  // the table
  return 1;
}

// **The `on_draw` of a strip of tabs**: the words in equal columns and the line under all of them but the
// chosen one. The items are a Lua array of strings and the index is one of them, so an app says its tabs the
// way a person says them (docs/30, docs/90).
int l_tabs_draw(lua_State* L) {
  if (!canvas_ready(L, 1)) return 0;
  // What this control is in: on, or off and drawn in its lighter tone.
  const DisabledInk off = ui_disabled_ink(L, 1);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  lua_getiuservalue(L, 1, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  lua_getfield(L, -1, "text");
  const int text = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "background");
  const int background = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "scale");
  const int scale = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "index");
  const int index = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  // **The tab the dial is on**, which is the one painted inverted: the walk gives the strip one stop per tab and
  // writes here which of them has the ring, and this is the only thing the painter needs to know about the focus
  // (0.99.0, docs/92).
  lua_getfield(L, -1, "focus_index");
  const int lit = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  const char* labels[8];
  uint8_t count = 0;
  lua_getfield(L, -1, "items");
  if (lua_istable(L, -1)) {
    const lua_Integer total = static_cast<lua_Integer>(lua_rawlen(L, -1));
    for (lua_Integer i = 1; i <= total && count < 8; ++i) {
      lua_rawgeti(L, -1, i);
      labels[count] = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
      ++count;
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);  // the items
  // **`closeable` is read while the fields table is still on the stack** (0.163.2): it used to be read after the
  // pops, on a value that was not the table any more, and the app died on its first present with `attempt to index
  // a boolean value` —the stack under it happened to be one— which is what the shell app found on 8/11 (docs/30).
  lua_getfield(L, -1, "closeable");
  const bool closeable = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  lua_pop(L, 1);  // the fields table
  if (g_ui_ops.tabs_draw != nullptr) {
    // **A strip the app can close** (0.161.0): the same paint with a cross on the right of every tab, and it is the
    // system that draws it so the box a finger presses is the box the glass painted (docs/30, docs/90).
    // **And it is drawn with no tab too** (0.164.2): an empty strip washes its own box, which is what an app with no
    // tab left needs —`count > 0` here meant the words of the tabs that go away stayed on the glass— (docs/30).
    if (closeable && g_ui_ops.tabs_draw_closeable != nullptr) {
      g_ui_ops.tabs_draw_closeable(canvas->slot, labels, count, static_cast<uint8_t>((index < 0) ? 0 : index),
                                   off.on ? 0 : static_cast<uint8_t>((lit < 0) ? 0 : lit), ink_or_dim(off, text),
                                   static_cast<uint8_t>(background), static_cast<uint8_t>((scale == 0) ? 1 : scale));
      return 0;
    }
    g_ui_ops.tabs_draw(canvas->slot, labels, count, static_cast<uint8_t>((index < 0) ? 0 : index),
                       off.on ? 0 : static_cast<uint8_t>((lit < 0) ? 0 : lit), ink_or_dim(off, text),
                       static_cast<uint8_t>(background), static_cast<uint8_t>((scale == 0) ? 1 : scale));
  }
  return 0;
}

// **`tabs:tap(x, y)`**: the strip takes a finger and decides. **A cross closes its tab** —the app hears `on_close(self,
// index)` and it is the app that gives back what was behind it, because a strip does not know what a tab holds— and **a
// word chooses its tab** —`index` becomes that one and the app hears `on_change(self, index)`, the same hook the dial
// calls—. The answer is true when the strip did something with the finger (0.161.0, docs/30, docs/90).
//
// **It is one call and not two** because the geometry is the control's: an app that worked out the columns would be a
// second arithmetic, and the day the strip changes it would press a tab that is not there.
int l_tabs_tap(lua_State* L) {
  if (!canvas_ready(L, 1)) return 0;
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  const lua_Integer x = luaL_checkinteger(L, 2);
  const lua_Integer y = luaL_checkinteger(L, 3);
  int count = 0;
  lua_getiuservalue(L, 1, 1);
  if (lua_istable(L, -1)) {
    lua_getfield(L, -1, "items");
    if (lua_istable(L, -1)) count = static_cast<int>(lua_rawlen(L, -1));
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  bool did = false;
  const uint16_t px = static_cast<uint16_t>((x < 0) ? 0 : x);
  const uint16_t py = static_cast<uint16_t>((y < 0) ? 0 : y);
  if (count > 0) {
    const uint8_t the_cross =
        (g_ui_ops.tabs_close_at != nullptr)
            ? g_ui_ops.tabs_close_at(canvas->slot, static_cast<uint8_t>(count), px, py)
            : 0;
    if (the_cross > 0) {
      did = true;
      ui_call_hook_number(L, 1, "on_close", the_cross);
    } else if (g_ui_ops.tabs_tab_at != nullptr) {
      const uint8_t the_tab = g_ui_ops.tabs_tab_at(canvas->slot, static_cast<uint8_t>(count), px, py);
      if (the_tab > 0) {
        did = true;
        if (ui_set_field_number(L, 1, "index", the_tab)) ui_invalidate(L, 1);
        ui_call_hook_number(L, 1, "on_change", the_tab);
      }
    }
  }
  lua_pushboolean(L, did ? 1 : 0);
  return 1;
}

// ui.tabs(width, height): **a strip of tabs**. The fields are `items` —a Lua array of words, eight at most—,
// `index` —the chosen one, 1 to the count, and 0 for none—, the ink and the paper, and the size of the words.
// **The arrows of the dial change the chosen tab while it has the focus**, and the app hears it at
// `on_change(self, index)` (docs/30, docs/90).
int l_ui_tabs(lua_State* L) {
  const int results = create_control(L, false);
  if (results != 1 || !lua_isuserdata(L, -1)) return results;
  lua_getiuservalue(L, -1, 1);
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "__tabs");
  lua_newtable(L);
  lua_setfield(L, -2, "items");
  lua_pushinteger(L, 1);
  lua_setfield(L, -2, "index");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "text");
  lua_pushinteger(L, 15);
  lua_setfield(L, -2, "background");
  // **The size of the system and not the app's**: 3 steps is the dense size of the paper project, which is what a strip of words is, and a control an app does not
  // configure looks like the rest of the glass (docs/92).
  lua_pushinteger(L, 3);
  lua_setfield(L, -2, "scale");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "pressed");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "focused");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "focusable");
  // Which tab of the strip has the ring, 0 when the ring is on another control: the walk writes it and the painter
  // reads it, and an app may read it too to know where the dial is (0.99.0, docs/30, docs/92).
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "focus_index");
  // **Whether every tab wears a cross to close it** (0.161.0): false by default —a strip of tabs is a page selector—
  // and true for a strip whose tabs are things an app can give back, which is what `tabs:tap` reads (docs/30).
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "closeable");
  lua_pushcfunction(L, &l_tabs_draw);
  lua_setfield(L, -2, "on_draw");
  lua_pop(L, 1);  // the table

  // The first pressable control is focused when it appears, the same rule a button follows (docs/30, docs/90).
  LuaCanvas* created = static_cast<LuaCanvas*>(luaL_checkudata(L, -1, kCanvasMeta));
  const bool focus_is_gone =
      g_ui_focus_slot < 0 ||
      (g_ui_ops.canvas_alive != nullptr &&
       !g_ui_ops.canvas_alive(g_ui_focus_slot, g_ui_focus_generation));
  if (focus_is_gone) {
    g_ui_focus_slot = created->slot;
    g_ui_focus_generation = created->generation;
    // A control that takes the dial is a stop of its own unless it is a strip, which says its tab next.
    g_ui_focus_item = 0;
    // **A strip that takes the dial lights the tab it opens on**, which is the one the page below is showing: the
    // ring is one stop per tab and a strip with no lit tab says nothing about where the person is (0.99.0, docs/92).
    lua_getiuservalue(L, -1, 1);
    lua_getfield(L, -1, "index");
    const lua_Integer opening = lua_tointeger(L, -1);
    lua_pop(L, 1);
    const uint8_t lit = static_cast<uint8_t>((opening > 0) ? opening : 1);
    g_ui_focus_item = lit;
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "focused");
    lua_pushinteger(L, lit);
    lua_setfield(L, -2, "focus_index");
    lua_pop(L, 1);
    if (g_ui_ops.canvas_invalidate != nullptr) g_ui_ops.canvas_invalidate(created->slot);
  }
  return 1;
}

// **The `on_draw` of the dots of a page**: `pages` dots and the `current` one bigger, and nothing at all when
// there is only one page (docs/30, docs/90).
int l_dots_draw(lua_State* L) {
  if (!canvas_ready(L, 1)) return 0;
  // What this control is in: on, or off and drawn in its lighter tone.
  const DisabledInk off = ui_disabled_ink(L, 1);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  lua_getiuservalue(L, 1, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  lua_getfield(L, -1, "pages");
  const int pages = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "current");
  const int current = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "level");
  const int level = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "background");
  const int background = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_pop(L, 1);  // the fields table
  if (g_ui_ops.dots_draw != nullptr) {
    g_ui_ops.dots_draw(canvas->slot, static_cast<uint16_t>((pages < 0) ? 0 : pages),
                       static_cast<uint16_t>((current < 0) ? 0 : current), ink_or_dim(off, level),
                       static_cast<uint8_t>(background));
  }
  return 0;
}

// ui.dots(width, height): **where this page is among the others**. The fields are `pages`, `current`, the ink
// and the paper. It takes no focus and it is a strip: an app places it under the page it belongs to
// (docs/30, docs/90).
int l_ui_dots(lua_State* L) {
  const int results = create_control(L, false);
  if (results != 1 || !lua_isuserdata(L, -1)) return results;
  lua_getiuservalue(L, -1, 1);
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "__dots");
  lua_pushinteger(L, 1);
  lua_setfield(L, -2, "pages");
  lua_pushinteger(L, 1);
  lua_setfield(L, -2, "current");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "level");
  lua_pushinteger(L, 15);
  lua_setfield(L, -2, "background");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "focusable");
  lua_pushcfunction(L, &l_dots_draw);
  lua_setfield(L, -2, "on_draw");
  lua_pop(L, 1);  // the table
  return 1;
}

// **The `on_draw` of a list**: the rows of one page and the band that says which page it is. The app says the
// items —a Lua array of tables with `title`, `detail` and `subtitle`, or of strings, which are a title— and the
// control slices the page: how many rows fit is the kernel's arithmetic, asked here and asked by the drawing, so
// a list cannot say "2 / 3" over the rows of the fourth page (docs/30, docs/90).
int l_list_draw(lua_State* L) {
  if (!canvas_ready(L, 1)) return 0;
  // What this control is in: on, or off and drawn in its lighter tone.
  const DisabledInk off = ui_disabled_ink(L, 1);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  if (g_ui_ops.list_draw == nullptr || g_ui_ops.canvas_caps == nullptr ||
      g_ui_ops.list_page_of == nullptr) {
    return 0;
  }
  uint16_t frame_w = 0;
  uint16_t frame_h = 0;
  uint16_t frame_x = 0;
  uint16_t frame_y = 0;
  if (!g_ui_ops.canvas_caps(canvas->slot, frame_w, frame_h, frame_x, frame_y)) return 0;
  lua_getiuservalue(L, 1, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  const int fields = lua_gettop(L);
  lua_getfield(L, fields, "text");
  const int text = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, fields, "dim");
  const int dim = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, fields, "background");
  const int background = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, fields, "scale");
  const int scale = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, fields, "row_height");
  const int row_height = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, fields, "page");
  const int page = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, fields, "marked");
  const int marked = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, fields, "separator");
  const bool separator = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  lua_getfield(L, fields, "focused");
  const bool focused = lua_toboolean(L, -1) != 0 && g_ui_ring_shown;
  lua_pop(L, 1);
  // **A list of entries is a list of controls** (0.104.0): the rows are the app's —an entry is a control with whatever
  // the app put inside it— and this only places the ones of the page it is on. **The entries of the other pages are
  // placed past the frame**, where the clip does not compose them and the walk does not reach them, which is the same
  // cut that keeps a child of a box inside its box (docs/30, docs/90).
  lua_getfield(L, fields, "children");
  const int kids = lua_gettop(L);
  const lua_Integer entries = lua_istable(L, kids) ? static_cast<lua_Integer>(lua_rawlen(L, kids)) : 0;
  if (entries > 0) {
    uint8_t rows_of_entries = 0;
    const uint8_t pages_of_entries = g_ui_ops.list_page_of(
        frame_h, static_cast<uint16_t>(row_height), static_cast<uint16_t>(entries), rows_of_entries);
    int page_of_entries = (page < 1) ? 1 : page;
    if (page_of_entries > pages_of_entries) page_of_entries = pages_of_entries;
    const lua_Integer first_entry =
        static_cast<lua_Integer>(page_of_entries - 1) * static_cast<lua_Integer>(rows_of_entries);
    if (g_ui_ops.canvas_resize != nullptr && g_ui_ops.canvas_place != nullptr) {
      for (lua_Integer i = 1; i <= entries; ++i) {
        lua_rawgeti(L, kids, i);
        LuaCanvas* kid = static_cast<LuaCanvas*>(luaL_testudata(L, -1, kCanvasMeta));
        if (kid != nullptr && kid->slot >= 0) {
          const lua_Integer at = (i - 1) - first_entry;
          const bool shown = at >= 0 && at < static_cast<lua_Integer>(rows_of_entries);
          const uint16_t y = shown
                                 ? static_cast<uint16_t>(at * static_cast<lua_Integer>(row_height))
                                 : static_cast<uint16_t>(frame_h + row_height);
          g_ui_ops.canvas_resize(kid->slot, frame_w, static_cast<uint16_t>(row_height));
          g_ui_ops.canvas_place(kid->slot, 0, y);
        }
        lua_pop(L, 1);
      }
    }
    // **And the frame says which page it is**: no row is drawn by this control —the entries draw themselves— so the
    // only thing it owes is the paper behind them and the footer of the pages (docs/90).
    // **The rows of the page are the entries it placed**, which is what the foot of the pages has to sit under: the list
    // paints no row here —each entry paints itself— so `count` is zero and `rows` is the number the app sees (0.174.2).
    g_ui_ops.list_draw(canvas->slot, 0, rows_of_entries, static_cast<uint8_t>(page_of_entries), pages_of_entries, 0,
                       nullptr, nullptr, nullptr, static_cast<uint16_t>(row_height),
                       ink_or_dim(off, text), static_cast<uint8_t>(dim),
                       static_cast<uint8_t>(background), static_cast<uint8_t>((scale == 0) ? 1 : scale),
                       separator, focused && !off.on);
    if (page_of_entries != page) ui_set_field_number(L, 1, "page", page_of_entries);
    lua_settop(L, fields - 1);
    return 0;
  }
  lua_pop(L, 1);  // the children
  lua_getfield(L, fields, "items");
  const int items = lua_gettop(L);
  uint16_t total = 0;
  if (lua_istable(L, items)) total = static_cast<uint16_t>(lua_rawlen(L, items));
  uint8_t rows = 0;
  const uint8_t pages = g_ui_ops.list_page_of(frame_h, static_cast<uint16_t>(row_height), total, rows);
  if (pages == 0 || rows == 0) {
    // **A list with nothing in it is an empty frame and not a hole**: it paints its background and says no page,
    // which is the same thing an app gets before it fills the items (docs/90).
    g_ui_ops.list_draw(canvas->slot, 0, 0, 1, 1, 0, nullptr, nullptr, nullptr,
                       static_cast<uint16_t>(row_height), ink_or_dim(off, text),
                       static_cast<uint8_t>(dim), static_cast<uint8_t>(background),
                       static_cast<uint8_t>((scale == 0) ? 1 : scale), separator, focused);
    lua_settop(L, fields - 1);
    return 0;
  }
  // **The page is clamped here and not in the app**: a list turned past its end by a callback answers with a
  // page that exists, which is what the footer says and what the drawing shows (docs/90).
  int page_now = (page < 1) ? 1 : page;
  if (page_now > pages) page_now = pages;
  const uint16_t first = static_cast<uint16_t>((page_now - 1) * rows);
  uint8_t count = 0;
  while (count < rows && static_cast<uint32_t>(first) + count < total) ++count;
  const char* titles[32];
  const char* details[32];
  const char* subtitles[32];
  for (uint8_t i = 0; i < count; ++i) {
    titles[i] = "";
    details[i] = "";
    subtitles[i] = "";
    lua_rawgeti(L, items, static_cast<lua_Integer>(first) + i + 1);
    if (lua_istable(L, -1)) {
      lua_getfield(L, -1, "title");
      if (lua_isstring(L, -1)) titles[i] = lua_tostring(L, -1);
      lua_pop(L, 1);
      lua_getfield(L, -1, "detail");
      if (lua_isstring(L, -1)) details[i] = lua_tostring(L, -1);
      lua_pop(L, 1);
      lua_getfield(L, -1, "subtitle");
      if (lua_isstring(L, -1)) subtitles[i] = lua_tostring(L, -1);
      lua_pop(L, 1);
    } else if (lua_isstring(L, -1)) {
      titles[i] = lua_tostring(L, -1);
    }
    lua_pop(L, 1);
  }
  // The chosen row is a row of the whole list and the control paints one page: a row of another page is not this
  // page's mark, which is the number zero (docs/90).
  int mark_now = marked - static_cast<int>(first);
  if (mark_now < 1 || mark_now > count) mark_now = 0;
  g_ui_ops.list_draw(canvas->slot, count, rows, static_cast<uint8_t>(page_now), pages,
                     static_cast<uint8_t>(mark_now), titles, details, subtitles,
                     static_cast<uint16_t>(row_height), ink_or_dim(off, text),
                     static_cast<uint8_t>(dim), static_cast<uint8_t>(background),
                     static_cast<uint8_t>((scale == 0) ? 1 : scale), separator,
                     focused && !off.on);
  lua_settop(L, fields - 1);
  return 0;
}

// ui.list_entry(width, height): **a row of a list that the app builds** (0.104.0). The fields are the paper of the
// row, its ink, the separator, the mark and the focus, and the words are a control added inside it: a list of entries
// is a list of controls, which is what a list of things that are not a title and a detail needs (docs/30, docs/90).
// **The entry and the cell of a grid are the same control** (0.105.0): the paper of a row —or its ink when it is the
// marked one or the one with the ring— and, in a list, the line of the separator. What changes is the `separator` it
// is born with, which is what a row wants and a cell of a grid does not.
int l_ui_entry(lua_State* L, bool separator) {
  const int results = create_control(L, false);
  if (results != 1 || !lua_isuserdata(L, -1)) return results;
  lua_getiuservalue(L, -1, 1);
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "__entry");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "__inverts");
  lua_pushinteger(L, 15);
  lua_setfield(L, -2, "background");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "border");
  lua_pushboolean(L, separator ? 1 : 0);
  lua_setfield(L, -2, "separator");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "marked");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "pressed");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "focused");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "focusable");
  lua_pushcfunction(L, &l_entry_draw);
  lua_setfield(L, -2, "on_draw");
  lua_pop(L, 1);
  return 1;
}

int l_ui_list_entry(lua_State* L) { return l_ui_entry(L, true); }

// ui.grid_entry(width, height): **a cell of a grid that the app builds** (0.105.0). The same control without the line of
// the separator, and the app puts inside it what a cell is: an icon with its name, a box of two cells, a picture.
int l_ui_grid_entry(lua_State* L) { return l_ui_entry(L, false); }

// ui.list(width, height): **rows on pages and never a scroll**. The fields are `items`, `row_height`, `page` —the
// page it is on, from 1—, `marked` —the chosen row of the whole list, from 1, or 0—, the three colours of a row
// and the size of the words. **The arrows of the dial turn the page** while it has the focus, and the app hears
// it at `on_page(self, page)` (docs/30, docs/90).
int l_ui_list(lua_State* L) {
  const int results = create_control(L, false);
  if (results != 1 || !lua_isuserdata(L, -1)) return results;
  lua_getiuservalue(L, -1, 1);
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "__list");
  lua_newtable(L);
  lua_setfield(L, -2, "items");
  lua_pushinteger(L, 56);
  lua_setfield(L, -2, "row_height");
  lua_pushinteger(L, 1);
  lua_setfield(L, -2, "page");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "marked");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "text");
  lua_pushinteger(L, 8);
  lua_setfield(L, -2, "dim");
  lua_pushinteger(L, 15);
  lua_setfield(L, -2, "background");
  // **The size of the system and not the app's**: 4 steps is a body of thirty-six pixels (its `ui36b`), and a control an app does not
  // configure looks like the rest of the glass (docs/92).
  lua_pushinteger(L, 4);
  lua_setfield(L, -2, "scale");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "separator");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "pressed");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "focused");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "focusable");
  lua_pushcfunction(L, &l_list_draw);
  lua_setfield(L, -2, "on_draw");
  lua_pop(L, 1);  // the table

  // The first pressable control is focused when it appears, the same rule a button follows (docs/30, docs/90).
  LuaCanvas* created = static_cast<LuaCanvas*>(luaL_checkudata(L, -1, kCanvasMeta));
  const bool focus_is_gone =
      g_ui_focus_slot < 0 ||
      (g_ui_ops.canvas_alive != nullptr &&
       !g_ui_ops.canvas_alive(g_ui_focus_slot, g_ui_focus_generation));
  if (focus_is_gone) {
    g_ui_focus_slot = created->slot;
    g_ui_focus_generation = created->generation;
    // A control that takes the dial is a stop of its own unless it is a strip, which says its tab next.
    g_ui_focus_item = 0;
    lua_getiuservalue(L, -1, 1);
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "focused");
    lua_pop(L, 1);
    if (g_ui_ops.canvas_invalidate != nullptr) g_ui_ops.canvas_invalidate(created->slot);
  }
  return 1;
}

// ---- a calendar month (docs/30, docs/90) ----

// **The `on_draw` of a calendar**: seven columns, the names of the days the app gave, and the days of the month in
// their places —the kernel knows which weekday the first is, and that number is not an app's to compute— (docs/90).
int l_month_draw(lua_State* L) {
  if (!canvas_ready(L, 1)) return 0;
  // What this control is in: on, or off and drawn in its lighter tone.
  const DisabledInk off = ui_disabled_ink(L, 1);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  if (g_ui_ops.month_draw == nullptr) return 0;
  lua_getiuservalue(L, 1, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  const int fields = lua_gettop(L);
  const lua_Integer year = ui_field_number(L, 1, "year", 2026);
  const lua_Integer month = ui_field_number(L, 1, "month", 1);
  const lua_Integer selected = ui_field_number(L, 1, "selected", 0);
  const lua_Integer today = ui_field_number(L, 1, "today", 0);
  const lua_Integer text = ui_field_number(L, 1, "color", 0);
  const lua_Integer dim = ui_field_number(L, 1, "dim", 8);
  const lua_Integer scale = ui_field_number(L, 1, "scale", 2);
  lua_getfield(L, fields, "background");
  const int background = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, fields, "focused");
  const bool focused = lua_toboolean(L, -1) != 0 && g_ui_ring_shown;
  lua_pop(L, 1);
  const char* weekdays[7] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
  lua_getfield(L, fields, "weekdays");
  if (lua_istable(L, -1)) {
    for (lua_Integer i = 1; i <= 7; ++i) {
      lua_rawgeti(L, -1, i);
      weekdays[i - 1] = lua_isstring(L, -1) ? lua_tostring(L, -1) : nullptr;
      lua_pop(L, 1);
    }
  }
  // **Els dies marcats, en un bit cada un** (0.189.171): the app hands a list of days (`{ 3, 8, 21 }`) and the kernel turns
  // it into the mask the drawing reads, perquè el control no haja de recórrer una taula de Lua a cada píxel.
  uint32_t marks = 0;
  lua_getfield(L, fields, "marks");
  if (lua_istable(L, -1)) {
    const lua_Integer total = static_cast<lua_Integer>(lua_rawlen(L, -1));
    for (lua_Integer i = 1; i <= total; ++i) {
      lua_rawgeti(L, -1, i);
      const lua_Integer day = lua_isnumber(L, -1) ? lua_tointeger(L, -1) : 0;
      if (day >= 1 && day <= 31) marks |= (1u << static_cast<uint32_t>(day - 1));
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);
  g_ui_ops.month_draw(canvas->slot, static_cast<uint16_t>((year < 1) ? 1 : year),
                      static_cast<uint8_t>((month < 1 || month > 12) ? 1 : month),
                      static_cast<uint8_t>((selected < 0) ? 0 : selected),
                      static_cast<uint8_t>((today < 0) ? 0 : today), weekdays, marks,
                      ink_or_dim(off, text), static_cast<uint8_t>(dim),
                      static_cast<uint8_t>(background), static_cast<uint8_t>((scale == 0) ? 1 : scale),
                      focused && !off.on);
  lua_pop(L, 2);  // the weekdays and the fields table
  return 0;
}

// ui.month(width, height): **a calendar month and the day that is chosen**. The fields are `year` and `month` —1 to
// 12—, `selected` and `today` —a day of the month, or 0—, `weekdays` —seven short names, because the names of the
// days are the app's language—, the two colours and the size. **The arrows walk the days** and the centre picks one
// with `on_pick(self, day)` (docs/30, docs/90).
int l_ui_month(lua_State* L) {
  const int results = create_control(L, false);
  if (results != 1 || !lua_isuserdata(L, -1)) return results;
  lua_getiuservalue(L, -1, 1);
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "__month");
  lua_pushinteger(L, 2026);
  lua_setfield(L, -2, "year");
  lua_pushinteger(L, 1);
  lua_setfield(L, -2, "month");
  lua_pushinteger(L, 1);
  lua_setfield(L, -2, "selected");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "today");
  lua_newtable(L);
  lua_setfield(L, -2, "weekdays");
  lua_newtable(L);
  lua_setfield(L, -2, "marks");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "color");
  lua_pushinteger(L, 8);
  lua_setfield(L, -2, "dim");
  lua_pushinteger(L, 15);
  lua_setfield(L, -2, "background");
  // **The size of the system and not the app's**: 4 steps is a body of thirty-six pixels (its `ui36b`), and a control an app does not
  // configure looks like the rest of the glass (docs/92).
  lua_pushinteger(L, 4);
  lua_setfield(L, -2, "scale");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "pressed");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "focused");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "focusable");
  lua_pushcfunction(L, &l_month_draw);
  lua_setfield(L, -2, "on_draw");
  lua_pop(L, 1);  // the table

  // The first pressable control is focused when it appears, the same rule a button follows (docs/30, docs/90).
  LuaCanvas* created = static_cast<LuaCanvas*>(luaL_checkudata(L, -1, kCanvasMeta));
  const bool focus_is_gone =
      g_ui_focus_slot < 0 ||
      (g_ui_ops.canvas_alive != nullptr &&
       !g_ui_ops.canvas_alive(g_ui_focus_slot, g_ui_focus_generation));
  if (focus_is_gone) {
    g_ui_focus_slot = created->slot;
    g_ui_focus_generation = created->generation;
    // A control that takes the dial is a stop of its own unless it is a strip, which says its tab next.
    g_ui_focus_item = 0;
    lua_getiuservalue(L, -1, 1);
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "focused");
    lua_pop(L, 1);
    if (g_ui_ops.canvas_invalidate != nullptr) g_ui_ops.canvas_invalidate(created->slot);
  }
  return 1;
}

// ---- a sparkline and a pie (docs/30, docs/90) ----

// **The numbers of the two charts, and they are the kernel's too**: twenty-four samples and four slices are what a
// strip and a pie of this glass hold, and the bridge cannot include the kernel to read them, so they are written
// here once and the document is what keeps them together (docs/30, docs/90).
const uint8_t kMaxSparkSamples = 24;
const uint8_t kMaxPieSlices = 4;
// **I quaranta-vuit mostres per al gràfic** (0.189.144): the same number the kernel holds, written here once because the
// bridge cannot include the kernel to read it (docs/30, docs/75).
// **El mateix sostre que el dibuix** (0.192.60): the copy has to follow `ui.h`, or a page that gives a hundred and eighty samples
// would be cut at forty-eight on the way in and the chart would say the window is shorter than it is (docs/30).
const uint8_t kMaxChartSamples = 192;

// **A table of Lua numbers as the samples of a chart**: forty of them is all a sparkline of a page can show, and a
// table that is longer is cut and not an error (docs/90).
uint8_t chart_samples(lua_State* L, int index, uint16_t* out, uint8_t most) {
  if (!lua_istable(L, index)) return 0;
  const lua_Integer total = static_cast<lua_Integer>(lua_rawlen(L, index));
  uint8_t count = 0;
  index = lua_absindex(L, index);
  for (lua_Integer i = 1; i <= total && count < most; ++i) {
    lua_rawgeti(L, index, i);
    lua_Number value = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : 0;
    if (value < 0) value = 0;
    if (value > 65535) value = 65535;
    out[count++] = static_cast<uint16_t>(value);
    lua_pop(L, 1);
  }
  return count;
}

// **The `on_draw` of a sparkline**: one bar per sample, the scale of the window's peak, and the second series as an
// outline when it is a part of the same total (docs/90).
int l_sparkline_draw(lua_State* L) {
  if (!canvas_ready(L, 1)) return 0;
  // What this control is in: on, or off and drawn in its lighter tone.
  const DisabledInk off = ui_disabled_ink(L, 1);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  if (g_ui_ops.sparkline_draw == nullptr) return 0;
  lua_getiuservalue(L, 1, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  const int fields = lua_gettop(L);
  lua_getfield(L, fields, "color");
  int color = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, fields, "background");
  int background = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  // **On an inverted face the two tones swap** (0.101.0): a shape inside a button with the ring is drawn in the
  // paper of the face it is on, or it would be a black drawing on a black box (docs/92).
  if (lua_toboolean(L, 2)) {
    const int swap = color;
    color = background;
    background = swap;
  }
  uint16_t samples[kMaxSparkSamples];
  uint16_t top[kMaxSparkSamples];
  uint8_t count = 0;
  uint8_t tops = 0;
  lua_getfield(L, fields, "samples");
  count = chart_samples(L, -1, samples, kMaxSparkSamples);
  lua_getfield(L, fields, "top");
  tops = chart_samples(L, -1, top, kMaxSparkSamples);
  g_ui_ops.sparkline_draw(canvas->slot, samples, (tops > 0) ? top : nullptr, count,
                          ink_or_dim(off, color), static_cast<uint8_t>(background));
  lua_pop(L, 3);  // the top, the samples and the fields table
  return 0;
}

// ui.sparkline(width, height): **the shape of the last samples**. The fields are `samples` —a Lua array of numbers,
// the oldest first—, `top` —the second series, which is a part of the same total— and the two colours. It takes no
// focus and it is a strip: an app places it where the number it summarises lives (docs/30, docs/90).
int l_ui_sparkline(lua_State* L) {
  const int results = create_control(L, false);
  if (results != 1 || !lua_isuserdata(L, -1)) return results;
  lua_getiuservalue(L, -1, 1);
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "__spark");
  lua_newtable(L);
  lua_setfield(L, -2, "samples");
  lua_pushnil(L);
  lua_setfield(L, -2, "top");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "color");
  lua_pushinteger(L, 15);
  lua_setfield(L, -2, "background");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "focusable");
  lua_pushcfunction(L, &l_sparkline_draw);
  lua_setfield(L, -2, "on_draw");
  lua_pop(L, 1);  // the table
  return 1;
}

// **El rang físic amb què neix un gràfic** (0.189.148): the two numbers docs/30 promises as a default —zero and a hundred,
// which is what a percentage is—, **in one place**, perquè la fàbrica que sembra els camps i el dibuix que els llegeix no
// poden dir-ne dos de diferents: quan ho van fer, el gràfic va néixer amb un rang de zero a zero i la sèrie es va dibuixar
// com una ratlla plana sota la guia de dalt (docs/30, docs/90).
const int kChartLow = 0;
const int kChartHigh = 100;

// **A table of Lua numbers as the values of a chart**, which may be negative —a temperature is— and is cut at what the
// control holds instead of being an error (docs/90).
uint8_t chart_values(lua_State* L, int index, int16_t* out, uint8_t most) {
  if (!lua_istable(L, index)) return 0;
  const lua_Integer total = static_cast<lua_Integer>(lua_rawlen(L, index));
  uint8_t count = 0;
  index = lua_absindex(L, index);
  for (lua_Integer i = 1; i <= total && count < most; ++i) {
    lua_rawgeti(L, index, i);
    lua_Number value = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : 0;
    if (value < -32768) value = -32768;
    if (value > 32767) value = 32767;
    out[count++] = static_cast<int16_t>(value);
    lua_pop(L, 1);
  }
  return count;
}

// **The minutes of each sample**, where a negative is "this board has no clock": the same number of samples as the
// values, and one that is missing is filled with -1 rather than joined to the one before it (docs/90).
uint8_t chart_times(lua_State* L, int index, int32_t* out, uint8_t most) {
  if (!lua_istable(L, index)) return 0;
  const lua_Integer total = static_cast<lua_Integer>(lua_rawlen(L, index));
  uint8_t count = 0;
  index = lua_absindex(L, index);
  for (lua_Integer i = 1; i <= total && count < most; ++i) {
    lua_rawgeti(L, index, i);
    out[count++] = lua_isnumber(L, -1) ? static_cast<int32_t>(lua_tointeger(L, -1)) : -1;
    lua_pop(L, 1);
  }
  return count;
}

// **The `on_draw` of a chart**: the values, the minutes, the tones and the unit of the three numbers on the left. The
// arrays live in the frame of this call —two of forty-eight— and not in a static block, because a chart is drawn by the
// paint of the page and two of them must not share one buffer (docs/75, docs/90).
int l_chart_draw(lua_State* L) {
  if (!canvas_ready(L, 1)) return 0;
  const DisabledInk off = ui_disabled_ink(L, 1);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  if (g_ui_ops.chart_draw == nullptr) return 0;
  lua_getiuservalue(L, 1, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  const int fields = lua_gettop(L);
  int color = 0;
  int background = 15;
  int scale = 1;
  int low = kChartLow;
  int high = kChartHigh;
  const char* suffix = "";
  char unit[8] = {0};
  // **Un camp que no és un número no és un zero** (0.189.148): `lua_tointeger` answers 0 for a `nil` —a field the factory
  // never seeded, or one an app set to nil— and that is how the range of a chart became 0..1: `chart_draw` saw `high` at
  // zero, turned it into one so as not to divide by zero, and a series of ninety, ninety-five and a hundred came out as a
  // flat line under the top guide with the scale saying 90 % and 89 %. The default the document promises is kept instead,
  // and the two numbers are the same ones the factory seeds (docs/30, docs/90).
  int is_a_number = 0;
  lua_getfield(L, fields, "min");
  low = static_cast<int>(lua_tointegerx(L, -1, &is_a_number));
  if (!is_a_number) low = kChartLow;
  lua_pop(L, 1);
  lua_getfield(L, fields, "max");
  high = static_cast<int>(lua_tointegerx(L, -1, &is_a_number));
  if (!is_a_number) high = kChartHigh;
  lua_pop(L, 1);
  lua_getfield(L, fields, "color");
  color = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, fields, "background");
  background = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, fields, "scale");
  scale = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, fields, "suffix");
  if (lua_isstring(L, -1)) {
    snprintf(unit, sizeof(unit), "%s", lua_tostring(L, -1));
    suffix = unit;
  }
  lua_pop(L, 1);
  // **On an inverted face the two tones swap**, the same rule the sparkline follows (docs/92).
  if (lua_toboolean(L, 2)) {
    const int swap = color;
    color = background;
    background = swap;
  }
  int16_t values[kMaxChartSamples];
  int32_t times[kMaxChartSamples];
  uint8_t count = 0;
  uint8_t when = 0;
  lua_getfield(L, fields, "values");
  count = chart_values(L, -1, values, kMaxChartSamples);
  lua_getfield(L, fields, "times");
  when = chart_times(L, -1, times, kMaxChartSamples);
  // **A sample with no minute is a sample with no clock**: the array of minutes is the values' or it is not there at
  // all, and the second case is the samples one after the other and the axis saying so (docs/90).
  for (uint8_t i = when; i < count; ++i) times[i] = -1;
  // **Els decimals i els dos textos de l'eix** (0.189.169): unobtrusive defaults —zero decimals and the chart's own hour—
  // so that an app that does not say anything keeps the drawing of the 0.189.144 (docs/30, docs/90).
  int decimals = 0;
  lua_getfield(L, fields, "decimals");
  decimals = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  char axis_left[16] = {0};
  char axis_right[16] = {0};
  lua_getfield(L, fields, "left");
  if (lua_isstring(L, -1)) snprintf(axis_left, sizeof(axis_left), "%s", lua_tostring(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, fields, "right");
  if (lua_isstring(L, -1)) snprintf(axis_right, sizeof(axis_right), "%s", lua_tostring(L, -1));
  lua_pop(L, 1);
  int gap = 60;
  lua_getfield(L, fields, "gap");
  if (lua_isnumber(L, -1)) gap = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  // **La finestra fixa** (0.192.50): zero vol dir la de les mostres, que és el dibuix de sempre.
  int span = 0;
  lua_getfield(L, fields, "span");
  if (lua_isnumber(L, -1)) span = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  g_ui_ops.chart_draw(canvas->slot, values, times, count, ink_or_dim(off, static_cast<uint8_t>(color)),
                      static_cast<uint8_t>(background), static_cast<uint8_t>(scale), suffix, low, high,
                      static_cast<uint8_t>(decimals), axis_left, axis_right, static_cast<uint8_t>(gap), span);
  lua_pop(L, 3);  // the times, the values and the fields table
  return 0;
}

// ui.chart(width, height): **the series of a page against its time**. The fields are `values` —a Lua array of numbers,
// the oldest first—, `times` —the minute of the day of each one, negative when this board has no clock—, `suffix` —the
// unit written after the three numbers of the scale—, `scale` —the step of those numbers— and the two tones. It takes no
// focus: it is a drawing of numbers the app already has (docs/30, docs/90).
int l_ui_chart(lua_State* L) {
  const int results = create_control(L, false);
  if (results != 1 || !lua_isuserdata(L, -1)) return results;
  lua_getiuservalue(L, -1, 1);
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "__chart");
  lua_newtable(L);
  lua_setfield(L, -2, "values");
  lua_pushnil(L);
  lua_setfield(L, -2, "times");
  // **I el rang físic hi és des del primer dia** (0.189.148): the fields are seeded the way `color`, `background` and `scale`
  // already were, so an app that reads `chart.max` before setting it reads the hundred the document promises instead of a
  // `nil` the control was quietly drawing with (docs/30, docs/90).
  lua_pushinteger(L, kChartLow);
  lua_setfield(L, -2, "min");
  lua_pushinteger(L, kChartHigh);
  lua_setfield(L, -2, "max");
  // **I els tres camps que el dibuix llegeix sempre** (0.189.169): zero decimals i cap text d'eix són el dibuix de sempre.
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "decimals");
  lua_pushstring(L, "");
  lua_setfield(L, -2, "left");
  lua_pushstring(L, "");
  lua_setfield(L, -2, "right");
  lua_pushinteger(L, 60);
  lua_setfield(L, -2, "gap");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "span");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "color");
  lua_pushinteger(L, 15);
  lua_setfield(L, -2, "background");
  lua_pushinteger(L, 1);
  lua_setfield(L, -2, "scale");
  lua_pushstring(L, "");
  lua_setfield(L, -2, "suffix");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "focusable");
  lua_pushcfunction(L, &l_chart_draw);
  lua_setfield(L, -2, "on_draw");
  lua_pop(L, 1);  // the table
  return 1;
}

// **The `on_draw` of a pie**: the four slices, the angles from their sum and the four tones that walk from the one
// the app chose (docs/90).
int l_pie_draw(lua_State* L) {
  if (!canvas_ready(L, 1)) return 0;
  // What this control is in: on, or off and drawn in its lighter tone.
  const DisabledInk off = ui_disabled_ink(L, 1);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  if (g_ui_ops.pie_draw == nullptr) return 0;
  lua_getiuservalue(L, 1, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  const int fields = lua_gettop(L);
  lua_getfield(L, fields, "color");
  int color = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, fields, "background");
  int background = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  // **On an inverted face the two tones swap** (0.101.0): a shape inside a button with the ring is drawn in the
  // paper of the face it is on, or it would be a black drawing on a black box (docs/92).
  if (lua_toboolean(L, 2)) {
    const int swap = color;
    color = background;
    background = swap;
  }
  uint16_t slices[kMaxPieSlices];
  uint8_t count = 0;
  lua_getfield(L, fields, "slices");
  count = chart_samples(L, -1, slices, kMaxPieSlices);
  g_ui_ops.pie_draw(canvas->slot, slices, count, ink_or_dim(off, color),
                    static_cast<uint8_t>(background));
  lua_pop(L, 2);  // the slices and the fields table
  return 0;
}

// ui.pie(width, height): **a pie of four slices at most**. The fields are `slices` —a Lua array of values, and the
// angles come from their sum— and the darkest tone; the other three are four steps apart, which is what an app
// draws its legend with (docs/30, docs/90).
int l_ui_pie(lua_State* L) {
  const int results = create_control(L, false);
  if (results != 1 || !lua_isuserdata(L, -1)) return results;
  lua_getiuservalue(L, -1, 1);
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "__pie");
  lua_newtable(L);
  lua_setfield(L, -2, "slices");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "color");
  lua_pushinteger(L, 15);
  lua_setfield(L, -2, "background");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "focusable");
  lua_pushcfunction(L, &l_pie_draw);
  lua_setfield(L, -2, "on_draw");
  lua_pop(L, 1);  // the table
  return 1;
}

// ---- a dialog: the card and its two doors (docs/30, docs/90) ----

// **The `on_draw` of a dialog**: the card, the words and the two doors. What the app says is the text of it and
// which door is the default; where the doors are and where a message breaks is the kernel's, because a message cut
// in half is a question nobody can answer (docs/90).
int l_dialog_draw(lua_State* L) {
  if (!canvas_ready(L, 1)) return 0;
  // What this control is in: on, or off and drawn in its lighter tone.
  const DisabledInk off = ui_disabled_ink(L, 1);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  if (g_ui_ops.dialog_draw == nullptr) return 0;
  lua_getiuservalue(L, 1, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  lua_getfield(L, -1, "color");
  const int color = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "dim");
  const int dim = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "background");
  const int background = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "scale");
  const int scale = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "emphasis");
  const int emphasis = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "choice");
  const int choice = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "focused");
  const bool focused = lua_toboolean(L, -1) != 0 && g_ui_ring_shown;
  lua_pop(L, 1);
  // The four texts stay on the stack while the card is drawn: a string taken off the stack before the call is a
  // pointer the collector may have spoken for (docs/75, docs/90).
  lua_getfield(L, -1, "title");
  const char* title = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
  lua_getfield(L, -2, "text");
  const char* text = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
  lua_getfield(L, -3, "note");
  const char* note = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
  lua_getfield(L, -4, "accept");
  const char* accept = lua_isstring(L, -1) ? lua_tostring(L, -1) : "OK";
  lua_getfield(L, -5, "cancel");
  const char* cancel = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
  g_ui_ops.dialog_draw(canvas->slot, title, text, note, accept, cancel, static_cast<uint8_t>(emphasis),
                       static_cast<uint8_t>(choice), ink_or_dim(off, color), static_cast<uint8_t>(dim),
                       static_cast<uint8_t>(background), static_cast<uint8_t>((scale == 0) ? 1 : scale),
                       focused && !off.on);
  lua_pop(L, 6);  // the five texts and the fields table
  return 0;
}

// ui.dialog(width, height): **a card over the page with words and two doors**. The fields are `title`, `text` —the
// message, which wraps—, `note`, the two labels `accept` and `cancel`, `emphasis` —the door that is inverted, 0
// none, 1 the cancel, 2 the accept—, `choice` —the door the dial is on, from 1—, the two colours and the size. The
// press arrives at `on_accept(self)` or `on_cancel(self)`, and **closing the card is the app's**: the old rule is
// read the action, close, and then run it (docs/30, docs/90).
int l_ui_dialog(lua_State* L) {
  const int results = create_control(L, false);
  if (results != 1 || !lua_isuserdata(L, -1)) return results;
  lua_getiuservalue(L, -1, 1);
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "__dialog");
  lua_pushstring(L, "");
  lua_setfield(L, -2, "title");
  lua_pushstring(L, "");
  lua_setfield(L, -2, "text");
  lua_pushstring(L, "");
  lua_setfield(L, -2, "note");
  lua_pushstring(L, "OK");
  lua_setfield(L, -2, "accept");
  lua_pushstring(L, "Cancel");
  lua_setfield(L, -2, "cancel");
  lua_pushinteger(L, 2);
  lua_setfield(L, -2, "emphasis");
  lua_pushinteger(L, 2);
  lua_setfield(L, -2, "choice");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "color");
  lua_pushinteger(L, 8);
  lua_setfield(L, -2, "dim");
  lua_pushinteger(L, 15);
  lua_setfield(L, -2, "background");
  // **The size of the system and not the app's**: 4 steps is a body of thirty-six pixels (its `ui36b`), and a control an app does not
  // configure looks like the rest of the glass (docs/92).
  lua_pushinteger(L, 4);
  lua_setfield(L, -2, "scale");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "pressed");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "focused");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "focusable");
  lua_pushcfunction(L, &l_dialog_draw);
  lua_setfield(L, -2, "on_draw");
  lua_pop(L, 1);  // the table
  // **And the system is told this control is a card** (0.173.2): a card paints a rounded box over the page, and the hole it
  // leaves when it goes is filled with the same shape —a rectangle would erase the four corners of the page under it, which
  // the card never touched— (docs/30, docs/90).
  {
    LuaCanvas* made = static_cast<LuaCanvas*>(luaL_checkudata(L, -1, kCanvasMeta));
    if (g_ui_ops.canvas_card != nullptr) g_ui_ops.canvas_card(made->slot);
  }

  // The first pressable control is focused when it appears, the same rule a button follows (docs/30, docs/90).
  LuaCanvas* created = static_cast<LuaCanvas*>(luaL_checkudata(L, -1, kCanvasMeta));
  const bool focus_is_gone =
      g_ui_focus_slot < 0 ||
      (g_ui_ops.canvas_alive != nullptr &&
       !g_ui_ops.canvas_alive(g_ui_focus_slot, g_ui_focus_generation));
  if (focus_is_gone) {
    g_ui_focus_slot = created->slot;
    g_ui_focus_generation = created->generation;
    // A control that takes the dial is a stop of its own unless it is a strip, which says its tab next.
    g_ui_focus_item = 0;
    lua_getiuservalue(L, -1, 1);
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "focused");
    lua_pop(L, 1);
    if (g_ui_ops.canvas_invalidate != nullptr) g_ui_ops.canvas_invalidate(created->slot);
  }
  return 1;
}

// ---- a menu: a card of options over the page (docs/30, docs/90) ----

// **The `on_draw` of a menu**: the card, its title and one box per option with the tag in front of the word. The
// options the app took out with `hidden` are not drawn, and the ring sits on the option the dial is on —which is the
// same option a finger picks, because the geometry is the kernel's and not the app's (docs/90).
int l_menu_draw(lua_State* L) {
  if (!canvas_ready(L, 1)) return 0;
  // What this control is in: on, or off and drawn in its lighter tone.
  const DisabledInk off = ui_disabled_ink(L, 1);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  if (g_ui_ops.menu_draw == nullptr) return 0;
  const int base = lua_gettop(L);
  UIOps::UIMenuOption items[kMaxMenuItems];
  const uint8_t count = ui_menu_items(L, 1, items);
  if (lua_gettop(L) < base + 2) {
    lua_settop(L, base);
    return 0;
  }
  // The title stays on the stack while the card is drawn, like the texts of a dialog: it is a pointer into a string and
  // a string nobody holds is a string the collector may have taken (docs/75, docs/90).
  lua_getfield(L, -2, "title");
  const char* title = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
  const lua_Integer index = ui_field_number(L, 1, "index", 1);
  const lua_Integer color = ui_field_number(L, 1, "color", 0);
  const lua_Integer dim = ui_field_number(L, 1, "dim", 8);
  const lua_Integer background = ui_field_number(L, 1, "background", 15);
  const lua_Integer scale = ui_field_number(L, 1, "scale", 2);
  const bool focused = ui_field_bool(L, 1, "focused", false) && g_ui_ring_shown;
  g_ui_ops.menu_draw(canvas->slot, title, items, count, static_cast<uint8_t>((index < 0) ? 0 : index),
                     ink_or_dim(off, color), static_cast<uint8_t>(dim),
                     static_cast<uint8_t>(background), static_cast<uint8_t>((scale < 1) ? 1 : scale),
                     focused && !off.on);
  lua_settop(L, base);
  return 0;
}

// ui.menu(width, height): **a card of options over the page**, which is what a contextual menu is. The fields are
// `items` —a Lua array of words, or of tables with `label`, `tag`, `hidden` and `primary`, twelve at most—, `index`
// —the option the dial is on, from 1—, `title`, the ink, the dim and the paper, and the size of the words.
//
// **An option the app cannot do now is hidden and not shown dead**: the glass can draw a lighter row since 0.100.0
// (`disabled`, dim ink), but a card is the list of what can be done *now* —a dead row in a card of three is a row a
// person tries— and a menu that changes shape is honest in a way a menu of dead rows is not. The **bar of a menu**
// keeps its entries and greys them, because a bar is a list of places and not of actions. **The arrows of the dial walk the options that are
// on the card**, skipping the hidden ones, and the centre picks one with `on_pick(self, index)`; closing the menu is
// the app's —read the action, close, and then run it—, the same rule a card follows (docs/30, docs/90).
int l_ui_menu(lua_State* L) {
  const int results = create_control(L, false);
  if (results != 1 || !lua_isuserdata(L, -1)) return results;
  lua_getiuservalue(L, -1, 1);
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "__menu");
  lua_newtable(L);
  lua_setfield(L, -2, "items");
  lua_pushstring(L, "");
  lua_setfield(L, -2, "title");
  lua_pushinteger(L, 1);
  lua_setfield(L, -2, "index");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "color");
  lua_pushinteger(L, 8);
  lua_setfield(L, -2, "dim");
  lua_pushinteger(L, 15);
  lua_setfield(L, -2, "background");
  // **The size of the system and not the app's**: 4 steps is a body of thirty-six pixels (its `ui36b`), and a control an app does not
  // configure looks like the rest of the glass (docs/92).
  lua_pushinteger(L, 4);
  lua_setfield(L, -2, "scale");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "pressed");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "focused");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "focusable");
  lua_pushcfunction(L, &l_menu_draw);
  lua_setfield(L, -2, "on_draw");
  lua_pop(L, 1);  // the table

  // The first pressable control is focused when it appears, the same rule a button follows (docs/30, docs/90).
  LuaCanvas* created = static_cast<LuaCanvas*>(luaL_checkudata(L, -1, kCanvasMeta));
  const bool focus_is_gone =
      g_ui_focus_slot < 0 ||
      (g_ui_ops.canvas_alive != nullptr &&
       !g_ui_ops.canvas_alive(g_ui_focus_slot, g_ui_focus_generation));
  if (focus_is_gone) {
    g_ui_focus_slot = created->slot;
    g_ui_focus_generation = created->generation;
    // A control that takes the dial is a stop of its own unless it is a strip, which says its tab next.
    g_ui_focus_item = 0;
    lua_getiuservalue(L, -1, 1);
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "focused");
    lua_pop(L, 1);
    if (g_ui_ops.canvas_invalidate != nullptr) g_ui_ops.canvas_invalidate(created->slot);
  }
  return 1;
}

// ---- a picker: the card of one choice (0.189.102, docs/30, docs/90) ----

// **The words of a picker**: a Lua array of strings, copied one by one into the block the kernel is given. The table stays
// on the stack while the card is drawn —the words are pointers into strings the table holds—, the same rule a menu's
// options follow (docs/75, docs/90).
// **How many words a picker's card can hold**: the kernel's `kMaxPickerItems` says the same number on the other side of the
// bridge, and the two are the same because a card with more values than the kernel can paint is a card that lies (docs/90).
const uint16_t kMaxPickerWords = 64;

uint16_t ui_picker_items(lua_State* L, int object, const char** out, uint16_t capacity) {
  if (out == nullptr || capacity == 0) return 0;
  lua_getiuservalue(L, object, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  lua_getfield(L, -1, "items");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 2);
    return 0;
  }
  const lua_Integer total = static_cast<lua_Integer>(lua_rawlen(L, -1));
  uint16_t count = 0;
  for (lua_Integer i = 1; i <= total && count < capacity; ++i) {
    lua_rawgeti(L, -1, i);
    const char* word = "";
    if (lua_isstring(L, -1)) {
      word = lua_tostring(L, -1);
    } else if (lua_istable(L, -1)) {
      lua_getfield(L, -1, "label");
      if (lua_isstring(L, -1)) word = lua_tostring(L, -1);
      lua_pop(L, 1);
    }
    out[count++] = word;
    lua_pop(L, 1);
  }
  return count;
}

// **The `on_draw` of a picker**: the card, its title, the rows of the page it is showing and the two keys of the foot. The
// kernel owns every number of the geometry —the rows that fit, which page, where a key is— so the app says the words and
// the chosen one and nothing else (docs/30, docs/90).
int l_picker_draw(lua_State* L) {
  if (!canvas_ready(L, 1)) return 0;
  const DisabledInk off = ui_disabled_ink(L, 1);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  if (g_ui_ops.picker_draw == nullptr) return 0;
  const int base = lua_gettop(L);
  const char* labels[kMaxPickerWords];
  const uint16_t count = ui_picker_items(L, 1, labels, kMaxPickerWords);
  // **A card with no words is a card that is still drawn** (0.189.102): a control that returns here without painting leaves the
  // page under it as it was and a card the app put up becomes a hole nobody can see —and the fault looked like «els
  // desplegables no tenen contingut» instead of a table of values that the app never gave— (docs/30, docs/90).
  const bool has_words = (lua_gettop(L) >= base + 2);
  if (has_words) {
    lua_getfield(L, -2, "title");
  } else {
    lua_pushstring(L, "");
  }
  const char* title = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
  const lua_Integer index = ui_field_number(L, 1, "index", 0);
  const lua_Integer page = ui_field_number(L, 1, "page", 0);
  const lua_Integer color = ui_field_number(L, 1, "color", 0);
  const lua_Integer dim = ui_field_number(L, 1, "dim", 8);
  const lua_Integer background = ui_field_number(L, 1, "background", 15);
  const lua_Integer scale = ui_field_number(L, 1, "scale", 4);
  const bool focused = ui_field_bool(L, 1, "focused", false) && g_ui_ring_shown;
  g_ui_ops.picker_draw(canvas->slot, title, labels, count, static_cast<uint16_t>((index < 0) ? 0 : index),
                       static_cast<uint16_t>((page < 0) ? 0 : page), ink_or_dim(off, color),
                       static_cast<uint8_t>(dim), static_cast<uint8_t>(background),
                       static_cast<uint8_t>((scale < 1) ? 1 : ((scale > 16) ? 16 : scale)), focused && !off.on);
  lua_settop(L, base);
  return 0;
}

// ui.picker(width, height): **la targeta d'una tria** (0.189.102), the `OptionPicker` of the paper project, which is the card
// the alarm form of the Clock opens when a person touches a field. The fields are `title` —the name of the field—, `items`
// —the words, or tables with `label`—, `index` —the chosen one, 1..n, and 0 is "nothing chosen"—, `page` —which page is on
// the glass, 1..n, and 0 for the page of the chosen one—, `color`, `dim`, `background` and `scale`.
//
// **The card is a card**: it is `__dialog`, so while it is up the walk of the dial and the finger is its own and the page
// under it answers nothing —the same rule `ui.dialog` follows—, and `__picker` says which card it is (docs/30, docs/90).
// **A finger on a row picks it and a finger on one of the two keys turns the page**, and a pick arrives at
// `on_pick(self, index)`; closing the card is the app's, the old rule of read the action, close, and then run it. The dial's
// two keys walk the values —crossing pages as it goes— and the centre picks the one it is on (docs/30, docs/90).
int l_ui_picker(lua_State* L) {
  const int results = create_control(L, false);
  if (results != 1 || !lua_isuserdata(L, -1)) return results;
  lua_getiuservalue(L, -1, 1);
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "__dialog");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "__picker");
  lua_newtable(L);
  lua_setfield(L, -2, "items");
  lua_pushstring(L, "");
  lua_setfield(L, -2, "title");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "index");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "page");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "color");
  lua_pushinteger(L, 8);
  lua_setfield(L, -2, "dim");
  lua_pushinteger(L, 15);
  lua_setfield(L, -2, "background");
  // **The size of the system and not the app's**: 4 steps is a body of thirty-six pixels (docs/92).
  lua_pushinteger(L, 4);
  lua_setfield(L, -2, "scale");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "pressed");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "focused");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "focusable");
  lua_pushcfunction(L, &l_picker_draw);
  lua_setfield(L, -2, "on_draw");
  lua_pop(L, 1);  // the table

  // **The card is the one that has the ring while it is up**: it is the only stop of the walk and the value the two keys of
  // the dial walk is the band of ink inside it (docs/30, docs/90).
  LuaCanvas* created = static_cast<LuaCanvas*>(luaL_checkudata(L, -1, kCanvasMeta));
  g_ui_focus_slot = created->slot;
  g_ui_focus_generation = created->generation;
  g_ui_focus_item = 0;
  lua_getiuservalue(L, -1, 1);
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "focused");
  lua_pop(L, 1);
  if (g_ui_ops.canvas_invalidate != nullptr) g_ui_ops.canvas_invalidate(created->slot);
  return 1;
}

// ---- a bar of menus and a box of several lines (docs/30, docs/90) ----

// **The `on_draw` of a bar**: the row of words, the line under it and the menu of the word that is unfolded. The entries
// of a menu are read once and handed over as one block, so a bar of five titles and sixteen entries costs two arrays on
// the C stack and no allocation at all (docs/90).
int l_menu_bar_draw(lua_State* L) {
  if (!canvas_ready(L, 1)) return 0;
  // What this control is in: on, or off and drawn in its lighter tone.
  const DisabledInk off = ui_disabled_ink(L, 1);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  if (g_ui_ops.menu_bar_draw == nullptr) return 0;
  const int base = lua_gettop(L);
  UIOps::UIMenuBarTitle titles[kMaxMenuBarTitles];
  UIOps::UIMenuBarEntry entries[kMaxMenuBarEntries];
  const uint8_t count = ui_menu_bar_items(L, 1, titles, entries);
  if (lua_gettop(L) < base + 2) {
    lua_settop(L, base);
    return 0;
  }
  const lua_Integer open = ui_field_number(L, 1, "open", 0);
  const lua_Integer index = ui_field_number(L, 1, "index", 0);
  const lua_Integer bar_height = ui_field_number(L, 1, "bar_height", 52);
  const lua_Integer color = ui_field_number(L, 1, "color", 0);
  const lua_Integer dim = ui_field_number(L, 1, "dim", 8);
  const lua_Integer background = ui_field_number(L, 1, "background", 15);
  const lua_Integer scale = ui_field_number(L, 1, "scale", 2);
  const bool focused = ui_field_bool(L, 1, "focused", false) && g_ui_ring_shown;
  g_ui_ops.menu_bar_draw(canvas->slot, titles, count, static_cast<uint8_t>((open < 0) ? 0 : open),
                         static_cast<uint8_t>((index < 0) ? 0 : index), static_cast<uint16_t>((bar_height < 1) ? 1 : bar_height),
                         ink_or_dim(off, color), static_cast<uint8_t>(dim),
                         static_cast<uint8_t>(background), static_cast<uint8_t>((scale < 1) ? 1 : scale),
                         focused && !off.on);
  lua_settop(L, base);
  return 0;
}

// ui.menu_bar(width, height): **the bar of menus of a page**. The fields are `items` —an array of titles, and a title is
// a table with `title` and `items`, the entries of the menu that hangs from it—, `open` —the title whose menu is down,
// from 1, or 0—, `index` —the entry of that menu the dial is on, from 1—, `bar_height`, the three tones and the size of
// the words.
//
// **An entry is a word, a table with `label`, `checked` and `enabled`, or a dash for a line between two entries.** An
// entry that cannot be used now is the dim tone and does not fire; a dash is not a row a finger presses and the dial goes
// past it. **A finger on a title folds or unfolds its menu** and the app reads `open`; a finger on an entry —or the
// centre, when the dial is on one— fires `on_pick(self, title, entry)` and the system folds the menu, because a menu
// that stays open after a choice is a menu a person has to dismiss twice (docs/30, docs/90).
int l_ui_menu_bar(lua_State* L) {
  const int results = create_control(L, false);
  if (results != 1 || !lua_isuserdata(L, -1)) return results;
  lua_getiuservalue(L, -1, 1);
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "__menu_bar");
  lua_newtable(L);
  lua_setfield(L, -2, "items");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "open");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "index");
  lua_pushinteger(L, 52);
  lua_setfield(L, -2, "bar_height");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "color");
  lua_pushinteger(L, 8);
  lua_setfield(L, -2, "dim");
  lua_pushinteger(L, 15);
  lua_setfield(L, -2, "background");
  // **The size of the system and not the app's**: 3 steps is the dense size of the paper project, which is what a strip of words is, and a control an app does not
  // configure looks like the rest of the glass (docs/92).
  lua_pushinteger(L, 3);
  lua_setfield(L, -2, "scale");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "pressed");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "focused");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "focusable");
  lua_pushcfunction(L, &l_menu_bar_draw);
  lua_setfield(L, -2, "on_draw");
  lua_pop(L, 1);  // the table

  // **The bar is focusable and it does not eat the arrows**: it has to be in the walk of the targets to see the finger at
  // all, and its arrows only answer while a menu is unfolded —with nothing down the key goes back to the walk of the
  // dial, which is what keeps a page with a bar on it from being a dead end (docs/30, docs/90).
  LuaCanvas* created = static_cast<LuaCanvas*>(luaL_checkudata(L, -1, kCanvasMeta));
  const bool focus_is_gone =
      g_ui_focus_slot < 0 ||
      (g_ui_ops.canvas_alive != nullptr &&
       !g_ui_ops.canvas_alive(g_ui_focus_slot, g_ui_focus_generation));
  if (focus_is_gone) {
    g_ui_focus_slot = created->slot;
    g_ui_focus_generation = created->generation;
    // A control that takes the dial is a stop of its own unless it is a strip, which says its tab next.
    g_ui_focus_item = 0;
    lua_getiuservalue(L, -1, 1);
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "focused");
    lua_pop(L, 1);
    if (g_ui_ops.canvas_invalidate != nullptr) g_ui_ops.canvas_invalidate(created->slot);
  }
  return 1;
}

// **The `on_draw` of a box of several lines**: the window of the text that keeps the caret on the glass and the caret as
// a block with the letter under it in paper, the same two things the box of one line has. What the app says is the text,
// where the caret is in bytes and whether the long lines wrap (docs/90).
int l_textbox_multiline_draw(lua_State* L) {
  if (!canvas_ready(L, 1)) return 0;
  // What this control is in: on, or off and drawn in its lighter tone.
  const DisabledInk off = ui_disabled_ink(L, 1);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  if (g_ui_ops.textbox_multiline_draw == nullptr) return 0;
  lua_getiuservalue(L, 1, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  lua_getfield(L, -1, "text");
  const char* text = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
  const lua_Integer cursor = ui_field_number(L, 1, "cursor", 0);
  const lua_Integer color = ui_field_number(L, 1, "color", 0);
  const lua_Integer background = ui_field_number(L, 1, "background", 15);
  // **A box without a frame** (0.116.0): the control paints its frame in `border` and its inside in `background`, so a
  // frame of the colour of the inside is no frame at all. It is a flag and not a fourth colour because what an app wants
  // to say is "no frame", and a document that fills the page has nothing to be framed by (docs/30, docs/92).
  const bool framed = ui_field_bool(L, 1, "framed", true);
  const lua_Integer border = framed ? ui_field_number(L, 1, "border", 0) : background;
  const lua_Integer scale = ui_field_number(L, 1, "scale", 3);
  const bool caret = ui_field_bool(L, 1, "caret", false);
  const bool wrap = ui_field_bool(L, 1, "wrap", false);
  const lua_Integer top = ui_field_number(L, 1, "top", -1);
  g_ui_ops.textbox_multiline_draw(canvas->slot, text, static_cast<uint16_t>((cursor < 0) ? 0 : cursor), caret, wrap,
                                  ink_or_dim(off, color), static_cast<uint8_t>(background),
                                  ink_or_dim(off, border), static_cast<uint8_t>((scale < 1) ? 1 : scale),
                                  static_cast<int16_t>((top < -1) ? -1 : ((top > 4096) ? 4096 : top)));
  lua_pop(L, 2);  // the text and the fields table
  return 0;
}

// ui.textbox_multiline(width, height): **a box of several lines**, the sibling of `ui.textbox` with room to fall. The
// fields are `text`, `cursor` —how many bytes are before the caret—, `caret` —whether the block is drawn, because the
// blinking is the app's clock—, `wrap` —whether a long line is broken at the last space that fits or cut at the border—,
// and the three tones with the size of the letters.
//
// **The enter key of the system's keyboard writes a line break here and does not end the edition**, and the keyboard of
// the system is told to paint the enter key for this box. **A finger puts the caret where it is touched**: the row from
// the y and the letter from the x, with the same arithmetic the paint used (docs/30, docs/90).
int l_ui_textbox_multiline(lua_State* L) {
  const int results = create_control(L, false);
  if (results != 1 || !lua_isuserdata(L, -1)) return results;
  lua_getiuservalue(L, -1, 1);
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "__textbox_multiline");
  lua_pushstring(L, "");
  lua_setfield(L, -2, "text");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "cursor");
  // **I la de diverses línies tampoc** (0.189.181): the same rule as the box of one line, i la caixa que una app vol amb
  // bloc perquè és una pàgina que es llegeix ho diu ella (`caret = true`, com fa `fonts`) (docs/30, docs/90).
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "caret");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "wrap");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "color");
  lua_pushinteger(L, 15);
  lua_setfield(L, -2, "background");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "border");
  // **The size of the system and not the app's**: 4 steps is a body of thirty-six pixels (its `ui36b`), and a control an app does not
  // configure looks like the rest of the glass (docs/92).
  lua_pushinteger(L, 4);
  lua_setfield(L, -2, "scale");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "pressed");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "focused");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "focusable");
  // The same field as a box of one line: `'alpha'`, `'numeric'` or `false` (0.91.0, docs/30, docs/90).
  lua_pushstring(L, "alpha");
  lua_setfield(L, -2, "keyboard");
  lua_pushcfunction(L, &l_textbox_multiline_draw);
  lua_setfield(L, -2, "on_draw");
  lua_pop(L, 1);  // the table

  LuaCanvas* created = static_cast<LuaCanvas*>(luaL_checkudata(L, -1, kCanvasMeta));
  const bool focus_is_gone =
      g_ui_focus_slot < 0 ||
      (g_ui_ops.canvas_alive != nullptr &&
       !g_ui_ops.canvas_alive(g_ui_focus_slot, g_ui_focus_generation));
  if (focus_is_gone) {
    g_ui_focus_slot = created->slot;
    g_ui_focus_generation = created->generation;
    // A control that takes the dial is a stop of its own unless it is a strip, which says its tab next.
    g_ui_focus_item = 0;
    lua_getiuservalue(L, -1, 1);
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "focused");
    lua_pop(L, 1);
    if (g_ui_ops.canvas_invalidate != nullptr) g_ui_ops.canvas_invalidate(created->slot);
  }
  return 1;
}

// ---- a selector: a field that is chosen and not typed (docs/30, docs/90) ----

// **The `on_draw` of a selector**: the question, the answer and the column of the triangle. **The answer is resolved here**,
// where the table of options is, so a field shows the chosen option without the app keeping a second copy of a text that
// can be forty characters, and when nothing is chosen it is the word the app said —a dash, or `nobody` in its
// language— (docs/30, docs/90).
int l_selector_draw(lua_State* L) {
  if (!canvas_ready(L, 1)) return 0;
  // What this control is in: on, or off and drawn in its lighter tone.
  const DisabledInk off = ui_disabled_ink(L, 1);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  if (g_ui_ops.selector_draw == nullptr) return 0;
  const int base = lua_gettop(L);
  lua_getiuservalue(L, 1, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  const int fields = lua_gettop(L);
  lua_getfield(L, fields, "label");
  const char* label = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
  const lua_Integer index = ui_field_number(L, 1, "index", 0);
  lua_getfield(L, fields, "items");
  const int items = lua_gettop(L);
  // The value is pushed in both branches so the stack has the same shape whichever one it took, and it stays there while the
  // field is drawn: a string nobody holds is a string the collector may have taken (docs/75, docs/90).
  if (lua_istable(L, items) && index >= 1 && index <= static_cast<lua_Integer>(lua_rawlen(L, items))) {
    lua_rawgeti(L, items, index);
    if (!lua_isstring(L, -1)) {
      lua_pop(L, 1);
      lua_getfield(L, fields, "empty");
    }
  } else {
    lua_getfield(L, fields, "empty");
  }
  const char* value = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
  const bool choosing = ui_field_bool(L, 1, "choosing", false);
  const lua_Integer color = ui_field_number(L, 1, "color", 0);
  const lua_Integer dim = ui_field_number(L, 1, "dim", 8);
  const lua_Integer background = ui_field_number(L, 1, "background", 15);
  const lua_Integer border = ui_field_number(L, 1, "border", 0);
  const lua_Integer scale = ui_field_number(L, 1, "scale", 3);
  const bool focused = ui_field_bool(L, 1, "focused", false) && g_ui_ring_shown;
  g_ui_ops.selector_draw(canvas->slot, label, value, choosing, ink_or_dim(off, color),
                         static_cast<uint8_t>(dim), static_cast<uint8_t>(background),
                         ink_or_dim(off, border), static_cast<uint8_t>((scale < 1) ? 1 : scale),
                         focused && !off.on);
  lua_settop(L, base);
  return 0;
}

// ui.selector(width, height): **a field that is chosen and not typed**. The fields are `label` —the question, drawn in the
// dim tone and small—, `items` —the options, an array of words—, `index` —the chosen one, from 1, and **0 for none, which
// is a state of its own and not a missing value**—, `empty` —what "none" looks like in the app's language—, `choosing`
// —whether the list is over the field, which inverts only the column of the triangle—, and the three tones with the size.
//
// **The list is not in this control**, and that is the whole design: the field is a door, and what a finger and the centre
// get is `on_open(self)`, where the app puts up a `ui.menu` —drawn by the system like every other control— and writes
// `index` when one of its options is chosen. The system cannot hold a list it does not own, and the app already has one.
int l_ui_selector(lua_State* L) {
  const int results = create_control(L, false);
  if (results != 1 || !lua_isuserdata(L, -1)) return results;
  lua_getiuservalue(L, -1, 1);
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "__selector");
  lua_pushstring(L, "");
  lua_setfield(L, -2, "label");
  lua_newtable(L);
  lua_setfield(L, -2, "items");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "index");
  lua_pushstring(L, "-");
  lua_setfield(L, -2, "empty");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "choosing");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "color");
  lua_pushinteger(L, 8);
  lua_setfield(L, -2, "dim");
  lua_pushinteger(L, 15);
  lua_setfield(L, -2, "background");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "border");
  // **The size of the system and not the app's**: 4 steps is a body of thirty-six pixels (its `ui36b`), and a control an app does not
  // configure looks like the rest of the glass (docs/92).
  lua_pushinteger(L, 4);
  lua_setfield(L, -2, "scale");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "pressed");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "focused");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "focusable");
  lua_pushcfunction(L, &l_selector_draw);
  lua_setfield(L, -2, "on_draw");
  lua_pop(L, 1);  // the table

  // **It takes the focus and it does not eat the arrows**: it has to be in the walk of the targets to see the finger, and a
  // selector has nothing to move with an arrow —its value is a list, not a number— so the key goes back to the walk of the
  // dial (docs/30, docs/90).
  LuaCanvas* created = static_cast<LuaCanvas*>(luaL_checkudata(L, -1, kCanvasMeta));
  const bool focus_is_gone =
      g_ui_focus_slot < 0 ||
      (g_ui_ops.canvas_alive != nullptr &&
       !g_ui_ops.canvas_alive(g_ui_focus_slot, g_ui_focus_generation));
  if (focus_is_gone) {
    g_ui_focus_slot = created->slot;
    g_ui_focus_generation = created->generation;
    // A control that takes the dial is a stop of its own unless it is a strip, which says its tab next.
    g_ui_focus_item = 0;
    lua_getiuservalue(L, -1, 1);
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "focused");
    lua_pop(L, 1);
    if (g_ui_ops.canvas_invalidate != nullptr) g_ui_ops.canvas_invalidate(created->slot);
  }
  return 1;
}

// **The cards of a band, read once**: an entry is a word —a notice with a text and no title— or a table with `title` and
// `text`, and **the ones that are kept are the last three**, because they are the newest and the band shows the newest at
// the bottom. The `items` table stays on the stack while the band paints, like the options of a menu (docs/30, docs/90).
uint16_t ui_notifications(lua_State* L, int object, UIOps::UINotification* out, uint8_t& shown) {
  shown = 0;
  if (out == nullptr) return 0;
  lua_getiuservalue(L, object, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  lua_getfield(L, -1, "items");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 2);
    return 0;
  }
  const lua_Integer total = static_cast<lua_Integer>(lua_rawlen(L, -1));
  const lua_Integer first = (total > static_cast<lua_Integer>(kMaxNotifications))
                                ? total - static_cast<lua_Integer>(kMaxNotifications) + 1
                                : 1;
  for (lua_Integer i = first; i <= total; ++i) {
    lua_rawgeti(L, -1, i);
    UIOps::UINotification& card = out[shown];
    card.title = "";
    card.text = "";
    if (lua_istable(L, -1)) {
      lua_getfield(L, -1, "title");
      if (lua_isstring(L, -1)) card.title = lua_tostring(L, -1);
      lua_pop(L, 1);
      lua_getfield(L, -1, "text");
      if (lua_isstring(L, -1)) card.text = lua_tostring(L, -1);
      lua_pop(L, 1);
    } else if (lua_isstring(L, -1)) {
      card.text = lua_tostring(L, -1);
    }
    lua_pop(L, 1);  // the card
    ++shown;
  }
  return static_cast<uint16_t>((total < 0) ? 0 : total);
}

// **How many notices the app has**, which is what the `+N` and the number of a card need: the ones on the glass are the last
// three, and the rest are waiting (docs/90).
uint16_t ui_notification_total(lua_State* L, int object) {
  lua_getiuservalue(L, object, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  lua_getfield(L, -1, "items");
  const lua_Integer total = lua_istable(L, -1) ? static_cast<lua_Integer>(lua_rawlen(L, -1)) : 0;
  lua_pop(L, 2);
  return static_cast<uint16_t>((total < 0) ? 0 : total);
}

// **The `on_draw` of a band**: the cards from edge to edge, the newest at the bottom, its title, its text and its cross, and
// the `+N` that says how many are waiting. The app says the notices and not one pixel of them (docs/30, docs/90).
int l_notification_draw(lua_State* L) {
  if (!canvas_ready(L, 1)) return 0;
  // What this control is in: on, or off and drawn in its lighter tone.
  const DisabledInk off = ui_disabled_ink(L, 1);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  if (g_ui_ops.notification_bar_draw == nullptr) return 0;
  const int base = lua_gettop(L);
  UIOps::UINotification cards[kMaxNotifications];
  uint8_t shown = 0;
  const uint16_t total = ui_notifications(L, 1, cards, shown);
  if (lua_gettop(L) < base + 2) {
    lua_settop(L, base);
    return 0;
  }
  const lua_Integer color = ui_field_number(L, 1, "color", 0);
  const lua_Integer dim = ui_field_number(L, 1, "dim", 8);
  const lua_Integer background = ui_field_number(L, 1, "background", 15);
  const lua_Integer border = ui_field_number(L, 1, "border", 0);
  const lua_Integer scale = ui_field_number(L, 1, "scale", 2);
  const bool focused = ui_field_bool(L, 1, "focused", false) && g_ui_ring_shown;
  g_ui_ops.notification_bar_draw(canvas->slot, cards, shown, total, ink_or_dim(off, color),
                                 static_cast<uint8_t>(dim), static_cast<uint8_t>(background),
                                 ink_or_dim(off, border), static_cast<uint8_t>((scale < 1) ? 1 : scale),
                                 focused && !off.on);
  lua_settop(L, base);
  return 0;
}

// ui.notification_bar(width, height): **the band where the notices go**. The fields are `items` —an array where a notice is a
// word or a table with `title` and `text`, **the newest last**—, the three tones and the size. **The newest card is the one at
// the bottom**, because it is the one nearest the finger and the one that goes away first, and what does not fit is not
// lost: the band says how many are waiting with a `+N` (docs/30, docs/90).
//
// **A finger on the cross dismisses and tells the app which notice** (`on_close(self, item)`), and a finger on the rest of a
// card opens it (`on_pick(self, item)`); the centre dismisses the newest, because a notice that a board without a touch
// cannot take away is a notice that stays forever. The list of notices is the app's: the control paints it and does not
// keep it.
int l_ui_notification_bar(lua_State* L) {
  const int results = create_control(L, false);
  if (results != 1 || !lua_isuserdata(L, -1)) return results;
  lua_getiuservalue(L, -1, 1);
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "__notifications");
  lua_newtable(L);
  lua_setfield(L, -2, "items");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "index");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "color");
  lua_pushinteger(L, 8);
  lua_setfield(L, -2, "dim");
  lua_pushinteger(L, 15);
  lua_setfield(L, -2, "background");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "border");
  // **The size of the system and not the app's**: 4 steps is a body of thirty-six pixels (its `ui36b`), and a control an app does not
  // configure looks like the rest of the glass (docs/92).
  lua_pushinteger(L, 4);
  lua_setfield(L, -2, "scale");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "pressed");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "focused");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "focusable");
  lua_pushcfunction(L, &l_notification_draw);
  lua_setfield(L, -2, "on_draw");
  lua_pop(L, 1);  // the table

  LuaCanvas* created = static_cast<LuaCanvas*>(luaL_checkudata(L, -1, kCanvasMeta));
  const bool focus_is_gone =
      g_ui_focus_slot < 0 ||
      (g_ui_ops.canvas_alive != nullptr &&
       !g_ui_ops.canvas_alive(g_ui_focus_slot, g_ui_focus_generation));
  if (focus_is_gone) {
    g_ui_focus_slot = created->slot;
    g_ui_focus_generation = created->generation;
    // A control that takes the dial is a stop of its own unless it is a strip, which says its tab next.
    g_ui_focus_item = 0;
    lua_getiuservalue(L, -1, 1);
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "focused");
    lua_pop(L, 1);
    if (g_ui_ops.canvas_invalidate != nullptr) g_ui_ops.canvas_invalidate(created->slot);
  }
  return 1;
}

// **The cells of a grid, read once**: a cell is a word —a name and no mark— or a table with `label` and `mark`, where the mark
// is the short word that goes in the box of the icon. The `items` table stays on the stack while the grid paints, like the
// options of a menu (docs/30, docs/90).
uint8_t ui_grid_items(lua_State* L, int object, UIOps::UIGridItem* out) {
  if (out == nullptr) return 0;
  lua_getiuservalue(L, object, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  lua_getfield(L, -1, "items");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 2);
    return 0;
  }
  const lua_Integer total = static_cast<lua_Integer>(lua_rawlen(L, -1));
  uint8_t count = 0;
  for (lua_Integer i = 1; i <= total && count < kMaxGridCells; ++i) {
    lua_rawgeti(L, -1, i);
    UIOps::UIGridItem& cell = out[count];
    cell.label = "";
    cell.mark = "";
    if (lua_istable(L, -1)) {
      lua_getfield(L, -1, "label");
      if (lua_isstring(L, -1)) cell.label = lua_tostring(L, -1);
      lua_pop(L, 1);
      lua_getfield(L, -1, "mark");
      if (lua_isstring(L, -1)) cell.mark = lua_tostring(L, -1);
      lua_pop(L, 1);
    } else if (lua_isstring(L, -1)) {
      cell.label = lua_tostring(L, -1);
    }
    lua_pop(L, 1);  // the cell
    ++count;
  }
  return count;
}

// **The `on_draw` of a grid**: the cells of one page, each with the box of its mark and its name under it, and the footer
// that says which page it is. **The page follows the cursor**, so the app hands over the whole list and the control decides
// which page is on the glass (docs/30, docs/90).
int l_icon_grid_draw(lua_State* L) {
  if (!canvas_ready(L, 1)) return 0;
  // What this control is in: on, or off and drawn in its lighter tone.
  const DisabledInk off = ui_disabled_ink(L, 1);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  if (g_ui_ops.icon_grid_draw == nullptr) return 0;
  const int base = lua_gettop(L);
  UIOps::UIGridItem cells[kMaxGridCells];
  const uint8_t count = ui_grid_items(L, 1, cells);
  if (lua_gettop(L) < base + 2) {
    lua_settop(L, base);
    return 0;
  }
  const lua_Integer index = ui_field_number(L, 1, "index", 0);
  const lua_Integer marked = ui_field_number(L, 1, "marked", 0);
  const lua_Integer columns = ui_field_number(L, 1, "columns", 0);
  const lua_Integer color = ui_field_number(L, 1, "color", 0);
  const lua_Integer dim = ui_field_number(L, 1, "dim", 8);
  const lua_Integer background = ui_field_number(L, 1, "background", 15);
  const lua_Integer scale = ui_field_number(L, 1, "scale", 2);
  const bool focused = ui_field_bool(L, 1, "focused", false) && g_ui_ring_shown;
  // **A grid of entries is a grid of controls** (0.105.0): the cells are the app's and this only puts each one where the
  // kernel says the cell is —the same geometry the paint and the finger use— and the ones of another page past the
  // frame, where the clip does not compose them and the walk does not reach them (docs/30, docs/90).
  lua_getiuservalue(L, 1, 1);
  lua_getfield(L, -1, "children");
  const lua_Integer entries = lua_istable(L, -1) ? static_cast<lua_Integer>(lua_rawlen(L, -1)) : 0;
  bool cells_of_the_kernel = true;
  if (entries > 0 && g_ui_ops.icon_grid_cell != nullptr && g_ui_ops.canvas_resize != nullptr &&
      g_ui_ops.canvas_place != nullptr) {
    cells_of_the_kernel = false;
    uint16_t frame_h = 0;
    uint16_t frame_w = 0;
    uint16_t frame_x = 0;
    uint16_t frame_y = 0;
    const uint8_t total = static_cast<uint8_t>((entries > 255) ? 255 : entries);
    for (lua_Integer i = 1; i <= entries; ++i) {
      lua_rawgeti(L, -1, i);
      LuaCanvas* kid = static_cast<LuaCanvas*>(luaL_testudata(L, -1, kCanvasMeta));
      if (kid != nullptr && kid->slot >= 0 && g_ui_ops.canvas_caps != nullptr &&
          g_ui_ops.canvas_caps(canvas->slot, frame_w, frame_h, frame_x, frame_y)) {
        uint16_t cell_x = 0;
        uint16_t cell_y = 0;
        uint16_t cell_w = 0;
        uint16_t cell_h = 0;
        const bool on_page = g_ui_ops.icon_grid_cell(
            canvas->slot, total, static_cast<uint8_t>((index < 0) ? 0 : index),
            static_cast<uint8_t>((columns < 0) ? 0 : columns),
            static_cast<uint8_t>((scale < 1) ? 1 : scale), static_cast<uint16_t>(i), cell_x, cell_y,
            cell_w, cell_h);
        if (on_page) {
          g_ui_ops.canvas_resize(kid->slot, cell_w, cell_h);
          g_ui_ops.canvas_place(kid->slot, cell_x, cell_y);
        } else {
          g_ui_ops.canvas_resize(kid->slot, 40, 40);
          g_ui_ops.canvas_place(kid->slot, 0, static_cast<uint16_t>(frame_h + 40));
        }
      }
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 2);  // the children and the fields table
  g_ui_ops.icon_grid_draw(canvas->slot, cells, count, static_cast<uint8_t>((index < 0) ? 0 : index),
                          static_cast<uint8_t>((marked < 0) ? 0 : marked),
                          static_cast<uint8_t>((columns < 0) ? 0 : columns), ink_or_dim(off, color),
                          static_cast<uint8_t>(dim), static_cast<uint8_t>(background),
                          static_cast<uint8_t>((scale < 1) ? 1 : scale), focused && !off.on,
                          cells_of_the_kernel);
  lua_settop(L, base);
  return 0;
}

// ui.icon_grid(width, height): **a grid of cells on pages**, each one a box with a short mark and the name under it, which is
// how a menu of things is picked: **the mark is what is recognised at a glance and the name is what confirms it** (docs/30,
// docs/90). The fields are `items` —a word, or a table with `label` and `mark`—, `index` —the cell the dial is on, from 1, and
// 0 for none—, `marked` —the cell that is chosen, which is inverted—, `columns` (0 lets the control decide from the width),
// the three tones and the size.
//
// **The arrows walk the cells and the page follows the cursor**, so the app does not turn pages itself; the centre picks the
// cell the dial is on with `on_pick(self, index)`, and a finger picks the cell it presses.
int l_ui_icon_grid(lua_State* L) {
  const int results = create_control(L, false);
  if (results != 1 || !lua_isuserdata(L, -1)) return results;
  lua_getiuservalue(L, -1, 1);
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "__icon_grid");
  lua_newtable(L);
  lua_setfield(L, -2, "items");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "index");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "marked");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "columns");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "color");
  lua_pushinteger(L, 8);
  lua_setfield(L, -2, "dim");
  lua_pushinteger(L, 15);
  lua_setfield(L, -2, "background");
  // **The size of the system and not the app's**: 4 steps is a body of thirty-six pixels (its `ui36b`), and a control an app does not
  // configure looks like the rest of the glass (docs/92).
  lua_pushinteger(L, 4);
  lua_setfield(L, -2, "scale");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "pressed");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "focused");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "focusable");
  lua_pushcfunction(L, &l_icon_grid_draw);
  lua_setfield(L, -2, "on_draw");
  lua_pop(L, 1);  // the table

  LuaCanvas* created = static_cast<LuaCanvas*>(luaL_checkudata(L, -1, kCanvasMeta));
  const bool focus_is_gone =
      g_ui_focus_slot < 0 ||
      (g_ui_ops.canvas_alive != nullptr &&
       !g_ui_ops.canvas_alive(g_ui_focus_slot, g_ui_focus_generation));
  if (focus_is_gone) {
    g_ui_focus_slot = created->slot;
    g_ui_focus_generation = created->generation;
    // A control that takes the dial is a stop of its own unless it is a strip, which says its tab next.
    g_ui_focus_item = 0;
    lua_getiuservalue(L, -1, 1);
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "focused");
    lua_pop(L, 1);
    if (g_ui_ops.canvas_invalidate != nullptr) g_ui_ops.canvas_invalidate(created->slot);
  }
  return 1;
}

// ---- a text box and the keyboard of the system (docs/30, docs/90) ----

// **The `on_draw` of a text box**: the frame, the window of the text that keeps the caret on the glass, and the
// caret as a block with the letter under it in paper. The blink is the app's —it sets `caret`— because the
// control has no clock and a control with a clock is a control that keeps drawing when nobody is typing
// (docs/30, docs/90).
int l_textbox_draw(lua_State* L) {
  if (!canvas_ready(L, 1)) return 0;
  // What this control is in: on, or off and drawn in its lighter tone.
  const DisabledInk off = ui_disabled_ink(L, 1);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  lua_getiuservalue(L, 1, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  lua_getfield(L, -1, "color");
  const int color = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "background");
  const int background = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  // **A box without a frame is a box whose frame is the colour of its inside** (0.116.0, docs/30, docs/92).
  const bool framed = ui_field_bool(L, 1, "framed", true);
  lua_getfield(L, -1, "border");
  const int border = framed ? static_cast<int>(lua_tointeger(L, -1)) : background;
  lua_pop(L, 1);
  lua_getfield(L, -1, "scale");
  const int scale = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "cursor");
  const int cursor = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "caret");
  const bool caret = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  lua_getfield(L, -1, "mask");
  const bool mask = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  // **El botó que buida la caixa** (0.184.0): it belongs to the control and an app turns it off with `clear = false`, and
  // **while the box is being written into it is not drawn at all** —the owner: *«no es possible pitjar-lo quan està en mode
  // edició, així que millor ocultar-lo en aquest mode»*—, so the paint and the finger ask the same two fields and a cross a
  // person sees is a cross a person can press. **The edit mode of a box is its `editing` field and not its `focused` one**: a
  // page is built again whenever the band takes the bottom of the glass, every control goes with it and the new box is the
  // one a person is writing into while its `focused` field is false, and `editing` is put back on the live box by the system
  // (docs/30, docs/90).
  lua_getfield(L, -1, "clear");
  const bool clear = lua_isboolean(L, -1) ? (lua_toboolean(L, -1) != 0) : true;
  lua_pop(L, 1);
  lua_getfield(L, -1, "editing");
  const bool editing = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  // **La creu va en la tinta de les paraules** (0.184.0, l'amo: *«una X negra»*): the button of a box is a command of the control
  // and not a picture of its own, so it is drawn with the colour the text is and it dims with it when the page is not the one
  // that answers (docs/30).
  const uint8_t cross = ink_or_dim(off, color);
  // The text goes last and stays on the stack while the control draws: a string taken off the stack before the
  // call is a pointer the collector may have spoken for (docs/75, docs/90).
  lua_getfield(L, -1, "text");
  const char* text = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
  if (g_ui_ops.textbox_draw != nullptr) {
    g_ui_ops.textbox_draw(canvas->slot, text, static_cast<uint16_t>((cursor < 0) ? 0 : cursor), caret, mask,
                          ink_or_dim(off, color), static_cast<uint8_t>(background),
                          ink_or_dim(off, border), static_cast<uint8_t>((scale == 0) ? 1 : scale),
                          clear && !editing, cross);
  }
  lua_pop(L, 2);  // the text and the fields table
  return 0;
}

// ui.textbox(width, height): **a line a person types into**. The fields are `text`, `cursor` —how many bytes are
// before the caret—, `caret` —whether the block is drawn, which is what the app blinks—, `mask` for a password,
// the two colours and the size of the words. The typing is not here: the keyboard of the system writes the field
// (docs/30, docs/90).
int l_ui_textbox(lua_State* L) {
  const int results = create_control(L, false);
  if (results != 1 || !lua_isuserdata(L, -1)) return results;
  lua_getiuservalue(L, -1, 1);
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "__textbox");
  lua_pushstring(L, "");
  lua_setfield(L, -2, "text");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "cursor");
  // **Una caixa neix sense bloc** (0.189.181, l'amo: *«En una vista no pot haver dos cursors... Al afegir un event a
  // l'agenda veig que com hi ha 2 controls textbox, tots dos tenen cursor. això no te sentit. De fet, no entenc perquè el
  // TextBox te cursor mentre no te focus! El cursor ha d'apareixer quan el control te el focus»*): the block is the focused
  // box's and the system gives it —`caret_start` when a box takes the focus, a finger or a band, and `caret_stop` when it
  // loses it—, o sigui que una caixa que ningú no ha tocat no en té cap. Born with it, the second box of a form drew a
  // cursor nobody could type into (docs/30, docs/90).
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "caret");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "mask");
  // **El botó que buida la caixa ve posat** (0.184.0, l'amo: *«afegeix al control TextBox un UIButton sense vores a la dreta
  // del tot amb una X negra»*): a box a person writes into has somewhere to be emptied with one finger, and an app that does
  // not want it says `clear = false` (docs/30, docs/90).
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "clear");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "color");
  lua_pushinteger(L, 15);
  lua_setfield(L, -2, "background");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "border");
  // **The size of the system and not the app's**: 4 steps is a body of thirty-six pixels (its `ui36b`), and a control an app does not
  // configure looks like the rest of the glass (docs/92).
  lua_pushinteger(L, 4);
  lua_setfield(L, -2, "scale");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "pressed");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "focused");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "focusable");
  // **A text box says what keyboard it wants** (0.91.0): `'alpha'` by default, `'numeric'` for digits, and `false`
  // for a box that takes no keys at all —a page read-only, a sheet of text to scroll—. The keyboard follows this
  // field when the box takes the focus (docs/30, docs/90).
  lua_pushstring(L, "alpha");
  lua_setfield(L, -2, "keyboard");
  lua_pushcfunction(L, &l_textbox_draw);
  lua_setfield(L, -2, "on_draw");
  lua_pop(L, 1);  // the table

  // The first pressable control is focused when it appears, the same rule a button follows (docs/30, docs/90).
  LuaCanvas* created = static_cast<LuaCanvas*>(luaL_checkudata(L, -1, kCanvasMeta));
  const bool focus_is_gone =
      g_ui_focus_slot < 0 ||
      (g_ui_ops.canvas_alive != nullptr &&
       !g_ui_ops.canvas_alive(g_ui_focus_slot, g_ui_focus_generation));
  if (focus_is_gone) {
    g_ui_focus_slot = created->slot;
    g_ui_focus_generation = created->generation;
    // A control that takes the dial is a stop of its own unless it is a strip, which says its tab next.
    g_ui_focus_item = 0;
    lua_getiuservalue(L, -1, 1);
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "focused");
    lua_pop(L, 1);
    if (g_ui_ops.canvas_invalidate != nullptr) g_ui_ops.canvas_invalidate(created->slot);
  }
  return 1;
}

// **The keyboard of the system**: an object and not a control. It has no frame of its own and it is not in the app's
// tree —it is a band the system paints over the page— so what an app gets from it is three calls and one field, the box
// it types into (docs/30, docs/90).
const char kKeyboardMeta[] = "lunokOS.UIKeyboard";

// **The userdata of the keyboard carries nothing, and that is not an omission**: the box it types into is one for the
// whole system and it lives in the registry (`g_keyboard_target_ref`), so the object on the Lua side is an identity with
// a metatable and no data of its own. An empty struct is one byte and not zero, which is what `lua_newuserdatauv` wants;
// what it had —an `int placeholder`— was a field with a name, and the next reader takes a field with a name for data
// that is there for a reason (found in the review of 4/11, docs/30, docs/75).
struct LuaKeyboard {};
static_assert(sizeof(LuaKeyboard) == 1, "the userdata of the keyboard is one byte and not zero");

// The block of the box the keys go to: `caret` is the system's field now, and the app reads it but does not blink it
// (0.92.0, docs/30, docs/90).
void caret_set_field(lua_State* L, int object, bool on) {
  object = lua_absindex(L, object);
  lua_getiuservalue(L, object, 1);
  if (lua_istable(L, -1)) {
    lua_pushboolean(L, on ? 1 : 0);
    lua_setfield(L, -2, "caret");
  }
  lua_pop(L, 1);
}

// **`whole` is whether the control is marked for a draw here** (0.179.57): a box that takes the focus or the band has
// changed in a way nobody can place yet —the caret was not drawn, so its box is unknown— and it is marked whole, which is
// what it did before this argument existed. A key is the other case: the damage is the row of the caret, and the caller
// says where that is (`ui_invalidate_the_key`), so a whole invalidate here would be the page painted for one letter.
void caret_start(lua_State* L, int object, bool whole = true) {
  caret_set_field(L, object, true);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(L, object, kCanvasMeta));
  if (whole && canvas != nullptr && g_ui_ops.canvas_invalidate != nullptr) g_ui_ops.canvas_invalidate(canvas->slot);
  g_caret_on = true;
  g_caret_dirty = true;
  // **Un focus o una tecla tornen a guanyar els sis parpellejos** (0.179.35): the count of the settle is of this block and
  // of nothing else, so it starts again here (docs/30, docs/90).
  g_caret_blinks = 0;
  g_caret_present_mark = g_ui_present_count;
  g_caret_next_ms = now_ms() + caret_blink_ms();
}

void caret_stop(lua_State* L) {
  g_caret_on = true;
  g_caret_dirty = false;
  g_caret_blinks = 0;
  g_caret_next_ms = 0;
  if (g_keyboard_target_ref == LUA_NOREF) return;
  lua_rawgeti(L, LUA_REGISTRYINDEX, g_keyboard_target_ref);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(L, -1, kCanvasMeta));
  if (canvas != nullptr) {
    caret_set_field(L, -1, false);
    if (g_ui_ops.canvas_invalidate != nullptr) g_ui_ops.canvas_invalidate(canvas->slot);
  }
  lua_pop(L, 1);
}

// **What a text box says about the keyboard** (0.91.0): a box of one line or of several, and the field `keyboard` says
// **which alphabet** —`'alpha'` by default, `'numeric'` for digits—. It is the one question the system asks before a band comes
// up, and the answer to "is there a band at all" is not here: **a text box is for writing in, and the band is the system's**
// (l'amo: *«no trobo que l'app hagi d'escollir si un control te teclat o no… això ho fa el sistema»*, 16/11).
// **I qui ho diu que no és `readonly`** (0.116.0): a document a person reads has no keys to press, and an app that had to write
// `keyboard = false` to say "this box is not to be written in" was an app that threw the caret away with the keyboard. A
// read-only box keeps its block and never brings the band (docs/30, docs/92).
// **`keyboard = false` es va quedar sense feina i ja no la fa** (0.189.118): the third value of this field said the same as
// `readonly` with a word that hides the semicolon —and it took the caret with it—, so a `false` here is read as the alphabet
// the box did not name, which is `'alpha'`, and a box that must not be written in says `readonly` (docs/30, docs/90).
bool ui_box_keyboard(lua_State* L, int object, bool& wants, uint8_t& kind) {
  object = lua_absindex(L, object);
  const bool line = ui_field_bool(L, object, "__textbox", false);
  const bool lines = ui_field_bool(L, object, "__textbox_multiline", false);
  if (!line && !lines) return false;
  wants = !ui_field_bool(L, object, "readonly", false);
  kind = 0;
  lua_getiuservalue(L, object, 1);
  if (lua_istable(L, -1)) {
    lua_getfield(L, -1, "keyboard");
    if (lua_isstring(L, -1)) {
      const char* asked = lua_tostring(L, -1);
      kind = (asked != nullptr && strcmp(asked, "numeric") == 0) ? 1 : 0;
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  return true;
}

// The box the keys go to, kept alive by the registry; the same rule `kb.target` follows (docs/30, docs/75).
void ui_keyboard_forget_target(lua_State* L) {
  caret_stop(L);
  if (g_keyboard_target_ref != LUA_NOREF) {
    luaL_unref(L, LUA_REGISTRYINDEX, g_keyboard_target_ref);
    g_keyboard_target_ref = LUA_NOREF;
  }
}

void ui_keyboard_target_set(lua_State* L, int object) {
  ui_keyboard_forget_target(L);
  lua_pushvalue(L, object);
  g_keyboard_target_ref = luaL_ref(L, LUA_REGISTRYINDEX);
  caret_start(L, object);
}

void ui_keyboard_hide() {
  if (g_ui_ops.keyboard_hide != nullptr) g_ui_ops.keyboard_hide();
}

// **The band follows the box the finger lands on** (0.91.0): a box that takes keys becomes the target and brings the
// keyboard; a button, a card or the empty page takes both away. The target is the keyboard's focus and goes with it,
// so a band with no box under the finger cannot be shown by a later `kb:show()` without a target that says otherwise
// (docs/30, docs/90).
void ui_keyboard_follow_object(lua_State* L, int object) {
  object = lua_absindex(L, object);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(L, object, kCanvasMeta));
  bool wants = false;
  uint8_t kind = 0;
  if (canvas == nullptr || !canvas_ready(L, object) || !ui_box_keyboard(L, object, wants, kind)) {
    // **Una caixa morta no s'emporta la banda que ja té un destí** (0.179.75): the page is built again every time the band takes
    // the bottom of the glass, so the box a tap resolves to can be a dead object while the band is still the one a person is
    // typing into. Nothing is taken down in that case: the target the band already has is the box, and the caret is drawn in it.
    // With no target at all there is nothing to keep, and the band goes (docs/30, docs/90).
    if (g_keyboard_target_ref != LUA_NOREF) return;
    ui_keyboard_forget_target(L);
    ui_keyboard_hide();
    return;
  }
  // **The box keeps the block whether or not it takes keys** (0.116.0): a finger on a page nobody types into still puts
  // the caret where it landed —that is a place in the text— and the band is the only thing it does not get (docs/30).
  ui_keyboard_target_set(L, object);
  if (!wants) {
    // **Una caixa que no vol tecles no amaga el teclat** (0.179.74): a tap on a text box must not take the focus, the band, the
    // edit mode or the cursor away from what a person is writing —the box that takes no keys gets the block and nothing else—,
    // and the band goes when the finger lands **outside** the box, which is another path. Hiding it here meant that a tap on
    // the field could take the keyboard down with the caret alive behind it (docs/30, docs/90).
    return;
  }
  if (g_ui_ops.keyboard_show == nullptr) return;
  const bool visible = (g_ui_ops.keyboard_visible != nullptr) && g_ui_ops.keyboard_visible();
  const uint8_t before = g_keyboard_kind;
  g_keyboard_kind = kind;
  // A new box of the same alphabet only moves the target: the band is already the one it wants.
  if (!visible || before != kind) g_ui_ops.keyboard_show(kind);
}

// **L'app que aixeca la banda diu que escriu en aquella caixa** (0.182.1): the rule of who owns the two keys while a person
// edits lives further down, after the listener that answers them, and here the show only needs the name of the place it takes
// (docs/30, docs/90).
void ui_keys_follow_the_control(lua_State* L, int object);

int l_keyboard_show(lua_State* L) {
  if (g_ui_ops.keyboard_show == nullptr) return push_nil_error(L, "E_UNSUPPORTED", "this board has no keys");
  const int base = lua_gettop(L);
  if (g_keyboard_target_ref == LUA_NOREF) {
    ui_keyboard_hide();
    return push_nil_error(L, "E_INVAL", "a keyboard needs a text box");
  }
  lua_rawgeti(L, LUA_REGISTRYINDEX, g_keyboard_target_ref);
  const int target = lua_gettop(L);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(L, target, kCanvasMeta));
  bool wants = false;
  uint8_t box_kind = 0;
  if (canvas == nullptr || !canvas_ready(L, target) || !ui_box_keyboard(L, target, wants, box_kind)) {
    lua_settop(L, base);
    ui_keyboard_forget_target(L);
    ui_keyboard_hide();
    return push_nil_error(L, "E_INVAL", "a keyboard needs a text box");
  }
  if (!wants) {
    // **A box that said it takes no keys is not overruled by a show** (0.91.0): the read-only page of the editor is
    // scrollable and its finger is alive, and an app that calls `kb:show()` by mistake does not bring a keyboard over it.
    // **And the block stays** (0.116.0): the answer is the band away, not the caret of the page being read (docs/30).
    lua_settop(L, base);
    ui_keyboard_hide();
    return push_nil_error(L, "E_INVAL", "the box does not take a keyboard");
  }
  // The call names the alphabet or, with no argument, the one the box already asked for: a numeric field keeps its
  // digits when the app shows the keyboard without saying it again (docs/30, docs/90).
  uint8_t wanted = box_kind;
  if (lua_isstring(L, 2)) {
    const char* kind = lua_tostring(L, 2);
    if (strcmp(kind, "numeric") == 0) {
      wanted = 1;
    } else if (strcmp(kind, "alpha") != 0) {
      lua_settop(L, base);
      return push_nil_error(L, "E_INVAL", "a keyboard is alpha or numeric");
    } else {
      wanted = 0;
    }
  }
  // **The show is the app saying the box takes keys**: it writes the alphabet on the box, so the next focus that
  // brings the band back brings the same one (0.91.0, docs/30). **And the block comes back with it** (0.92.0): a
  // keyboard on the glass is an edition, and an edition has a caret.
  caret_start(L, target);
  lua_getiuservalue(L, target, 1);
  lua_pushstring(L, (wanted == 1) ? "numeric" : "alpha");
  lua_setfield(L, -2, "keyboard");
  lua_pop(L, 1);
  g_keyboard_kind = wanted;
  const bool shown = g_ui_ops.keyboard_show(wanted);
  // **I la caixa que la banda té de destí queda entrada i es queda les dues tecles** (0.182.1): the editor opens its page with
  // `kb:show` and no finger has landed on the box, so nothing had marked it `editing` and the keys went to the place that does
  // nothing with them —the centre could not even enter the field, and the two keys never walked the text—. A band with a box
  // under it is a box a person is writing in: the state says it, the queue follows it, and the three taps stay away because the
  // box's place is exclusive too (docs/30, docs/90).
  if (shown) {
    if (ui_set_field_bool(L, target, "editing", true)) {
      LuaCanvas* shown_canvas = static_cast<LuaCanvas*>(luaL_testudata(L, target, kCanvasMeta));
      if (shown_canvas != nullptr && g_ui_ops.canvas_invalidate != nullptr) g_ui_ops.canvas_invalidate(shown_canvas->slot);
    }
    ui_keys_follow_the_control(L, target);
  }
  lua_settop(L, base);
  lua_pushboolean(L, shown ? 1 : 0);
  return 1;
}

int l_keyboard_hide(lua_State* L) {
  // **The band goes and the block stays** (0.116.0): the caret is the focus's and not the keyboard's —a read-only page
  // has a place in its text and no band— so hiding the keys is not hiding where the person is. The block stops when the
  // box loses the focus, which is the dial leaving it or the target being forgotten (docs/30, docs/92).
  if (g_keyboard_target_ref != LUA_NOREF) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, g_keyboard_target_ref);
    LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(L, -1, kCanvasMeta));
    if (canvas != nullptr && canvas->slot >= 0) {
      caret_set_field(L, -1, true);
      if (g_ui_ops.canvas_invalidate != nullptr) g_ui_ops.canvas_invalidate(canvas->slot);
      g_caret_on = true;
    }
    lua_pop(L, 1);
  }
  ui_keyboard_hide();
  lua_pushboolean(L, 1);
  return 1;
}

int l_keyboard_height(lua_State* L) {
  lua_pushinteger(L, (g_ui_ops.keyboard_height != nullptr) ? g_ui_ops.keyboard_height() : 0);
  return 1;
}

int l_keyboard_visible(lua_State* L) {
  lua_pushboolean(L, (g_ui_ops.keyboard_visible != nullptr && g_ui_ops.keyboard_visible()) ? 1 : 0);
  return 1;
}

// **`kb.target = box`**: the one field of the keyboard and the reason it is not a control. The reference is the
// app's object kept alive by the registry, so a page made again does not leave the keyboard typing into a box
// that is not on the glass any more (docs/30, docs/90).
int l_keyboard_newindex(lua_State* L) {
  const char* key = lua_type(L, 2) == LUA_TSTRING ? lua_tostring(L, 2) : nullptr;
  if (key == nullptr || strcmp(key, "target") != 0) return luaL_error(L, "a keyboard takes a target and nothing else");
  if (g_keyboard_target_ref != LUA_NOREF) {
    luaL_unref(L, LUA_REGISTRYINDEX, g_keyboard_target_ref);
    g_keyboard_target_ref = LUA_NOREF;
  }
  if (lua_isuserdata(L, 3)) {
    lua_pushvalue(L, 3);
    g_keyboard_target_ref = luaL_ref(L, LUA_REGISTRYINDEX);
  }
  return 0;
}

int l_keyboard_tostring(lua_State* L) {
  lua_pushliteral(L, "keyboard");
  return 1;
}

int l_ui_keyboard(lua_State* L) {
  lua_newuserdatauv(L, sizeof(LuaKeyboard), 0);
  luaL_getmetatable(L, kKeyboardMeta);
  lua_setmetatable(L, -2);
  return 1;
}


// The fields of the keyboard are read through its own table and then its methods: `kb:show()` is ours and
// `kb.target` is the one field it has (docs/90).
int l_keyboard_index(lua_State* L) {
  if (lua_type(L, 2) != LUA_TSTRING) return 0;
  luaL_getmetatable(L, kKeyboardMeta);
  lua_getfield(L, -1, "__methods");
  lua_pushvalue(L, 2);
  lua_rawget(L, -2);
  return 1;
}

void open_keyboard_library(lua_State* L) {
  luaL_newmetatable(L, kKeyboardMeta);
  lua_createtable(L, 0, 4);
  set_function(L, "show", l_keyboard_show);
  set_function(L, "hide", l_keyboard_hide);
  set_function(L, "height", l_keyboard_height);
  set_function(L, "visible", l_keyboard_visible);
  lua_setfield(L, -2, "__methods");
  lua_pushcfunction(L, &l_keyboard_index);
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, &l_keyboard_newindex);
  lua_setfield(L, -2, "__newindex");
  lua_pushcfunction(L, &l_keyboard_tostring);
  lua_setfield(L, -2, "__tostring");
  lua_pop(L, 1);
}

int l_canvas_clear(lua_State* L) {
  if (!canvas_ready(L, 1)) return refuse_canvas(L);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  const lua_Integer level = luaL_optinteger(L, 2, 15);
  const char* mode = luaL_optstring(L, 3, nullptr);
  if (level < 0 || level > 15) return push_nil_error(L, "E_INVAL", "a level is 0 to 15");
  if (g_ui_ops.canvas_clear == nullptr ||
      !g_ui_ops.canvas_clear(canvas->slot, static_cast<uint8_t>(level), mode)) {
    return push_nil_error(L, "E_INVAL", "not a quality of this display");
  }
  lua_pushboolean(L, 1);
  return 1;
}

// **canvas:image(x, y, path[, w[, h[, mode]]])** (0.153.0): the picture of `ui.image` drawn in a canvas of the app and
// not on the page, which is what a launcher needs —the icon of an app inside the cell of a grid—. The canvas is composed
// into its father like what is drawn on it with `rect` or `text`, and only the box it touched is owed (docs/30, docs/90).
int l_canvas_image(lua_State* L) {
  if (!canvas_ready(L, 1)) return refuse_canvas(L);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  if (g_ui_ops.image == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no display");
  }
  const lua_Integer x = luaL_checkinteger(L, 2);
  const lua_Integer y = luaL_checkinteger(L, 3);
  const char* path = luaL_checkstring(L, 4);
  const lua_Integer w = luaL_optinteger(L, 5, 0);
  const lua_Integer h = luaL_optinteger(L, 6, 0);
  const char* mode = luaL_optstring(L, 7, nullptr);
  // **The mask is the eighth argument and it is optional** (0.169.0): `canvas:image(x, y, path, w, h, 'du', 'mask')` leaves
  // the pixels the picture calls transparent as the canvas had them, and `canvas:image(x, y, path, w, h)` paints the white of
  // a PNG's alpha over them as it always did (docs/30).
  const bool mask = (lua_gettop(L) >= 8) && (lua_toboolean(L, 8) != 0);
  if (x < 0 || y < 0 || x > 4096 || y > 4096 || w < 0 || h < 0 || w > 4096 || h > 4096) {
    return push_nil_error(L, "E_INVAL", "a picture is up to 4096 pixels a side");
  }
  const char* code = nullptr;
  const char* reason = nullptr;
  if (!g_ui_ops.image(canvas->slot, static_cast<uint16_t>(x), static_cast<uint16_t>(y), static_cast<uint16_t>(w),
                      static_cast<uint16_t>(h), path, mode, mask, code, reason)) {
    return push_nil_error(L, (code != nullptr) ? code : "E_INVAL",
                          (reason != nullptr) ? reason : "the picture was not drawn");
  }
  lua_pushboolean(L, 1);
  return 1;
}

// **Un tros d'un bloc, portat on el pintor el vol** (0.190.174): a canvas captures into a buffer and paints from one, and a
// session of 32 KB or more may live behind the bank switcher of the chip, where a pointer only exists while a window is
// mapped. So the piece is copied to the extra memory, used and given back —the copy is the picture and never the whole cache
// (docs/30, docs/50, docs/75).
uint8_t* piece_borrowed(LuaSession* block, uint32_t at, uint32_t bytes) {
  if (g_session_ops.read == nullptr || bytes == 0) return nullptr;
  uint8_t* buffer = static_cast<uint8_t*>(pal::extra_memory().allocate(bytes, true));
  if (buffer == nullptr) return nullptr;
  if (g_session_ops.read(block->key, at, reinterpret_cast<char*>(buffer), bytes) != bytes) {
    pal::extra_memory().release(buffer);
    return nullptr;
  }
  return buffer;
}

uint8_t* piece_lent(uint32_t bytes) {
  if (g_session_ops.write == nullptr || bytes == 0) return nullptr;
  return static_cast<uint8_t*>(pal::extra_memory().allocate(bytes, true));
}

// **`cv:capture(session [, offset])`** (0.179.0): the pixels of this canvas into the memory of a session, which is how an
// app keeps a picture it has already decoded —the file and the decoder are paid once and not at every paint—. The block
// is four bits a pixel with the left one in the high nibble, exactly what `cv:blit` takes back (docs/30, docs/75).
// **`cv:export(path)`: el canvas, guardat com a PNG pel sistema** (0.192.51, l'amo: *«Si exportar la imatge a png es massa feixuc per
// l'app lua, que ho faci el sistema (li passes el canvas i ell guarda)»*). The app gives the path and nothing else: the pixels are
// already in the kernel, and turning them into a file is one call instead of half a million steps of Lua. El que torna és el camí o
// `nil` i el perquè —`E_NOSPC` si al disc no hi cap, `E_IO` si no s'ha pogut escriure— (docs/30, docs/70).
int l_canvas_export(lua_State* L) {
  if (!canvas_ready(L, 1)) return refuse_canvas(L);
  const char* path = luaL_checkstring(L, 2);
  if (g_ui_ops.canvas_export == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board cannot write a picture");
  }
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  const char* code = "E_IO";
  const char* reason = "";
  if (!g_ui_ops.canvas_export(canvas->slot, path, code, reason)) {
    return push_nil_error(L, code, (reason != nullptr && reason[0] != '\0') ? reason : "the picture did not write");
  }
  lua_pushstring(L, path);
  return 1;
}

int l_canvas_capture(lua_State* L) {
  if (!canvas_ready(L, 1)) return refuse_canvas(L);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  LuaSession* block = session_check(L, 2);
  const lua_Integer at = luaL_optinteger(L, 3, 0);
  if (at < 0 || at > static_cast<lua_Integer>(block->size)) {
    return push_nil_error(L, "E_INVAL", "that offset is not in the block");
  }
  if (g_ui_ops.canvas_capture == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no display");
  }
  const uint32_t room = static_cast<uint32_t>(block->size - static_cast<size_t>(at));
  uint8_t* piece = piece_lent(room);
  if (piece == nullptr) {
    return push_nil_error(L, "E_NOMEM", "there is no room for the picture of this canvas");
  }
  size_t wrote = 0;
  const bool captured = g_ui_ops.canvas_capture(canvas->slot, piece, room, wrote);
  uint32_t put = 0;
  if (captured && wrote > 0) {
    put = g_session_ops.write(block->key, static_cast<uint32_t>(at), reinterpret_cast<const char*>(piece),
                              static_cast<uint32_t>(wrote));
  }
  pal::extra_memory().release(piece);
  if (!captured) {
    return push_nil_error(L, "E_NOMEM", "the block is smaller than the canvas");
  }
  lua_pushinteger(L, static_cast<lua_Integer>(put));
  return 1;
}

// **`cv:blit(session, offset, x, y, w, h [, mode])`** (0.179.0): the other half, and what a launcher does with the icons it
// captured —a copy of the pixels and no file, no decoder and no scaling—. It is the same primitive a page of a book uses
// for its plates (docs/30, docs/46).
int l_canvas_blit(lua_State* L) {
  if (!canvas_ready(L, 1)) return refuse_canvas(L);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  LuaSession* block = session_check(L, 2);
  const lua_Integer at = luaL_checkinteger(L, 3);
  const lua_Integer x = luaL_checkinteger(L, 4);
  const lua_Integer y = luaL_checkinteger(L, 5);
  const lua_Integer w = luaL_checkinteger(L, 6);
  const lua_Integer h = luaL_optinteger(L, 7, 0);
  const char* mode = luaL_optstring(L, 8, nullptr);
  if (at < 0 || at > static_cast<lua_Integer>(block->size)) {
    return push_nil_error(L, "E_INVAL", "that offset is not in the block");
  }
  if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
    return push_nil_error(L, "E_INVAL", "a block of pixels is up to 4096 a side");
  }
  const size_t stride = (static_cast<size_t>(w) + 1u) / 2u;
  const size_t needed = stride * static_cast<size_t>(h);
  if (static_cast<size_t>(at) + needed > block->size) {
    return push_nil_error(L, "E_INVAL", "the block is smaller than the picture");
  }
  if (g_ui_ops.canvas_bitmap == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no display");
  }
  uint8_t* piece = piece_borrowed(block, static_cast<uint32_t>(at), static_cast<uint32_t>(needed));
  if (piece == nullptr) {
    return push_nil_error(L, "E_NOMEM", "the picture of that block is not there");
  }
  const bool painted = g_ui_ops.canvas_bitmap(canvas->slot, static_cast<int16_t>(x), static_cast<int16_t>(y),
                                              static_cast<uint16_t>(w), static_cast<uint16_t>(h), piece,
                                              static_cast<uint16_t>(stride), mode, false);
  pal::extra_memory().release(piece);
  if (!painted) {
    return push_nil_error(L, "E_INVAL", "not a quality of this display");
  }
  lua_pushboolean(L, 1);
  return 1;
}

int l_canvas_rect(lua_State* L) {
  if (!canvas_ready(L, 1)) return refuse_canvas(L);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  const lua_Integer x = luaL_checkinteger(L, 2);
  const lua_Integer y = luaL_checkinteger(L, 3);
  const lua_Integer w = luaL_checkinteger(L, 4);
  const lua_Integer h = luaL_checkinteger(L, 5);
  const lua_Integer level = luaL_optinteger(L, 6, 0);
  const char* mode = luaL_optstring(L, 7, nullptr);
  if (w < 0 || h < 0 || level < 0 || level > 15) {
    return push_nil_error(L, "E_INVAL", "a rectangle is a size and a level of 0 to 15");
  }
  if (g_ui_ops.canvas_rect == nullptr ||
      !g_ui_ops.canvas_rect(canvas->slot, static_cast<uint16_t>(x), static_cast<uint16_t>(y),
                            static_cast<uint16_t>(w), static_cast<uint16_t>(h),
                            static_cast<uint8_t>(level), mode)) {
    return push_nil_error(L, "E_INVAL", "not a quality of this display");
  }
  lua_pushboolean(L, 1);
  return 1;
}

int l_canvas_pixel(lua_State* L) {
  if (!canvas_ready(L, 1)) return refuse_canvas(L);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  const lua_Integer x = luaL_checkinteger(L, 2);
  const lua_Integer y = luaL_checkinteger(L, 3);
  const lua_Integer level = luaL_optinteger(L, 4, 0);
  const char* mode = luaL_optstring(L, 5, nullptr);
  if (level < 0 || level > 15) return push_nil_error(L, "E_INVAL", "a level is 0 to 15");
  if (g_ui_ops.canvas_pixel == nullptr ||
      !g_ui_ops.canvas_pixel(canvas->slot, static_cast<uint16_t>(x), static_cast<uint16_t>(y),
                             static_cast<uint8_t>(level), mode)) {
    return push_nil_error(L, "E_INVAL", "not a quality of this display");
  }
  lua_pushboolean(L, 1);
  return 1;
}

// **`cv:invert(x, y, w, h [, mode])`**: a box of this canvas turned over (0.178.0), the same primitive the page has. It is
// what a launcher asks for when the dial lands on a cell: the picture the canvas holds goes into the black block with its
// own greys, and the paper of the picture stops being a white box on top of the block (docs/30, docs/92).
int l_canvas_invert(lua_State* L) {
  if (!canvas_ready(L, 1)) return refuse_canvas(L);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  const lua_Integer x = luaL_checkinteger(L, 2);
  const lua_Integer y = luaL_checkinteger(L, 3);
  const lua_Integer w = luaL_checkinteger(L, 4);
  const lua_Integer h = luaL_checkinteger(L, 5);
  const char* mode = luaL_optstring(L, 6, nullptr);
  if (w < 0 || h < 0) return push_nil_error(L, "E_INVAL", "a box is a width and a height");
  if (g_ui_ops.canvas_invert == nullptr ||
      !g_ui_ops.canvas_invert(canvas->slot, static_cast<uint16_t>(x), static_cast<uint16_t>(y),
                              static_cast<uint16_t>(w), static_cast<uint16_t>(h), mode)) {
    return push_nil_error(L, "E_INVAL", "not a quality of this display");
  }
  lua_pushboolean(L, 1);
  return 1;
}

// **`cv:fill(x, y, level [, mode])`**: the bucket (0.192.47). The region of the level under the point —and not of the shape the app
// thinks it drew— becomes `level`, which is what a person expects of a bucket: the ink that is on the glass is the truth, and a
// fill that reads a model of the drawing would leave the seams a thick stroke left. The walk is the kernel's (`canvas_flood`),
// perquè la pàgina d'un dibuix són centenars de kilobytes i el bucle de Lua els pagaria en torns (docs/30, docs/50).
int l_canvas_fill(lua_State* L) {
  if (!canvas_ready(L, 1)) return refuse_canvas(L);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  const lua_Integer x = luaL_checkinteger(L, 2);
  const lua_Integer y = luaL_checkinteger(L, 3);
  const lua_Integer level = luaL_checkinteger(L, 4);
  const char* mode = luaL_optstring(L, 5, nullptr);
  if (level < 0 || level > 15) return push_nil_error(L, "E_INVAL", "a level is 0 to 15");
  if (g_ui_ops.canvas_fill == nullptr ||
      !g_ui_ops.canvas_fill(canvas->slot, static_cast<uint16_t>(x), static_cast<uint16_t>(y),
                            static_cast<uint8_t>(level), mode)) {
    return push_nil_error(L, "E_INVAL", "not a quality of this display");
  }
  lua_pushboolean(L, 1);
  return 1;
}

// canvas:place(x, y): where it sits inside the one that holds it, in that canvas' pixels (docs/90).
int l_canvas_place(lua_State* L) {
  if (!canvas_ready(L, 1)) return refuse_canvas(L);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  const lua_Integer x = luaL_checkinteger(L, 2);
  const lua_Integer y = luaL_checkinteger(L, 3);
  if (x < 0 || y < 0) return push_nil_error(L, "E_INVAL", "a place is a column and a row");
  if (g_ui_ops.canvas_place == nullptr ||
      !g_ui_ops.canvas_place(canvas->slot, static_cast<uint16_t>(x), static_cast<uint16_t>(y))) {
    return push_nil_error(L, "E_INVAL", "this canvas is not on the screen any more");
  }
  lua_pushboolean(L, 1);
  return 1;
}

// canvas:resize(w, h): **another size for a control that has no pixels of its own** —a panel, a label, a box of lines—,
// which is what the `on_resize` of a page needs to arrange itself again when the view changes. A canvas with a buffer is
// refused: its pixels are a block handed out with a size, and another size is another canvas, which is the app's to ask
// with `ui.canvas` (0.84.0, docs/30, docs/90).
int l_canvas_resize(lua_State* L) {
  if (!canvas_ready(L, 1)) return refuse_canvas(L);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  const lua_Integer w = luaL_checkinteger(L, 2);
  const lua_Integer h = luaL_checkinteger(L, 3);
  if (w <= 0 || h <= 0 || w > 0xFFFF || h > 0xFFFF) {
    return push_nil_error(L, "E_INVAL", "a size is a column and a row");
  }
  if (g_ui_ops.canvas_resize == nullptr ||
      !g_ui_ops.canvas_resize(canvas->slot, static_cast<uint16_t>(w), static_cast<uint16_t>(h))) {
    return push_nil_error(L, "E_INVAL", "this canvas has its own pixels and a size of its own");
  }
  lua_pushboolean(L, 1);
  return 1;
}

// **The primitives, written once for the view and for a canvas.** `ui.line(...)` and `cv:line(...)` are the
// same call with a different canvas in the first place, so the body is here and the difference is the slot
// and where its arguments start (1 for the view, 2 for a method) (docs/30, docs/90).
int8_t canvas_slot_of(lua_State* L) {
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  return canvas->slot;
}

int refuse_shape(lua_State* L) {
  return push_nil_error(L, "E_INVAL", "not a quality of this display");
}

int frame_at(lua_State* L, int16_t slot, int base) {
  const lua_Integer x = luaL_checkinteger(L, base);
  const lua_Integer y = luaL_checkinteger(L, base + 1);
  const lua_Integer w = luaL_checkinteger(L, base + 2);
  const lua_Integer h = luaL_checkinteger(L, base + 3);
  const lua_Integer level = luaL_optinteger(L, base + 4, 0);
  const char* mode = luaL_optstring(L, base + 5, nullptr);
  if (w < 0 || h < 0 || level < 0 || level > 15) {
    return push_nil_error(L, "E_INVAL", "a box is a size and a level of 0 to 15");
  }
  if (g_ui_ops.canvas_frame == nullptr ||
      !g_ui_ops.canvas_frame(slot, static_cast<uint16_t>(x), static_cast<uint16_t>(y),
                             static_cast<uint16_t>(w), static_cast<uint16_t>(h),
                             static_cast<uint8_t>(level), mode)) {
    return refuse_shape(L);
  }
  lua_pushboolean(L, 1);
  return 1;
}

int line_at(lua_State* L, int16_t slot, int base) {
  const lua_Integer x0 = luaL_checkinteger(L, base);
  const lua_Integer y0 = luaL_checkinteger(L, base + 1);
  const lua_Integer x1 = luaL_checkinteger(L, base + 2);
  const lua_Integer y1 = luaL_checkinteger(L, base + 3);
  const lua_Integer level = luaL_optinteger(L, base + 4, 0);
  const char* mode = luaL_optstring(L, base + 5, nullptr);
  if (level < 0 || level > 15) return push_nil_error(L, "E_INVAL", "a level is 0 to 15");
  if (g_ui_ops.canvas_line == nullptr ||
      !g_ui_ops.canvas_line(slot, static_cast<int16_t>(x0), static_cast<int16_t>(y0),
                            static_cast<int16_t>(x1), static_cast<int16_t>(y1),
                            static_cast<uint8_t>(level), mode)) {
    return refuse_shape(L);
  }
  lua_pushboolean(L, 1);
  return 1;
}

int circle_at(lua_State* L, int16_t slot, int base, bool filled) {
  const lua_Integer cx = luaL_checkinteger(L, base);
  const lua_Integer cy = luaL_checkinteger(L, base + 1);
  const lua_Integer radius = luaL_checkinteger(L, base + 2);
  const lua_Integer level = luaL_optinteger(L, base + 3, 0);
  const char* mode = luaL_optstring(L, base + 4, nullptr);
  if (radius < 0) return push_nil_error(L, "E_INVAL", "a radius is not negative");
  if (level < 0 || level > 15) return push_nil_error(L, "E_INVAL", "a level is 0 to 15");
  const bool ok = filled ? (g_ui_ops.canvas_disc != nullptr &&
                            g_ui_ops.canvas_disc(slot, static_cast<int16_t>(cx),
                                                 static_cast<int16_t>(cy),
                                                 static_cast<int16_t>(radius),
                                                 static_cast<uint8_t>(level), mode))
                         : (g_ui_ops.canvas_circle != nullptr &&
                            g_ui_ops.canvas_circle(slot, static_cast<int16_t>(cx),
                                                   static_cast<int16_t>(cy),
                                                   static_cast<int16_t>(radius),
                                                   static_cast<uint8_t>(level), mode));
  if (!ok) return refuse_shape(L);
  lua_pushboolean(L, 1);
  return 1;
}

// A text: the letter of the system's font, `scale` times its size, in the colour asked for, and cut at
// the border of the canvas it is drawn into (docs/30).
int text_at(lua_State* L, int16_t slot, int base) {
  const lua_Integer x = luaL_checkinteger(L, base);
  const lua_Integer y = luaL_checkinteger(L, base + 1);
  const char* text = luaL_checkstring(L, base + 2);
  const lua_Integer scale = luaL_optinteger(L, base + 3, 1);
  const lua_Integer level = luaL_optinteger(L, base + 4, 0);
  const char* mode = luaL_optstring(L, base + 5, nullptr);
  if (scale <= 0 || scale > 16) return push_nil_error(L, "E_INVAL", "a text is one to sixteen times its size");
  if (level < 0 || level > 15) return push_nil_error(L, "E_INVAL", "a level is 0 to 15");
  if (g_ui_ops.canvas_text == nullptr ||
      !g_ui_ops.canvas_text(slot, static_cast<int16_t>(x), static_cast<int16_t>(y), text,
                            static_cast<uint8_t>(scale), static_cast<uint8_t>(level), mode)) {
    return refuse_shape(L);
  }
  lua_pushboolean(L, 1);
  return 1;
}

int l_canvas_frame(lua_State* L) {
  if (!canvas_ready(L, 1)) return refuse_canvas(L);
  return frame_at(L, canvas_slot_of(L), 2);
}

int l_canvas_line(lua_State* L) {
  if (!canvas_ready(L, 1)) return refuse_canvas(L);
  return line_at(L, canvas_slot_of(L), 2);
}

int l_canvas_circle(lua_State* L) {
  if (!canvas_ready(L, 1)) return refuse_canvas(L);
  return circle_at(L, canvas_slot_of(L), 2, false);
}

int l_canvas_disc(lua_State* L) {
  if (!canvas_ready(L, 1)) return refuse_canvas(L);
  return circle_at(L, canvas_slot_of(L), 2, true);
}

int l_canvas_text(lua_State* L) {
  if (!canvas_ready(L, 1)) return refuse_canvas(L);
  return text_at(L, canvas_slot_of(L), 2);
}

// The same five on the view, which is the canvas the system gave the app: slot 0 and the arguments from
// the first one (docs/30).
int l_ui_frame(lua_State* L) { return frame_at(L, 0, 1); }
int l_ui_line(lua_State* L) { return line_at(L, 0, 1); }
int l_ui_circle(lua_State* L) { return circle_at(L, 0, 1, false); }
int l_ui_disc(lua_State* L) { return circle_at(L, 0, 1, true); }
int l_ui_text(lua_State* L) { return text_at(L, 0, 1); }

// ---- the faces of a text (docs/30) ----

// **A face an app loaded from a portable font file**: the object holds a handle into the table of the
// kernel, and the block of glyphs goes back when the object is collected or the app ends (docs/30).
struct LuaFont {
  int8_t handle;
};

const char kFontMeta[] = "lunokOS.UIFont";

bool font_ready(lua_State* L, int index) {
  LuaFont* font = static_cast<LuaFont*>(luaL_testudata(L, index, kFontMeta));
  return font != nullptr && font->handle >= 0;
}

int push_font(lua_State* L, int8_t handle) {
  LuaFont* font = static_cast<LuaFont*>(lua_newuserdatauv(L, sizeof(LuaFont), 0));
  font->handle = handle;
  luaL_getmetatable(L, kFontMeta);
  lua_setmetatable(L, -2);
  return 1;
}

// `ui.load_font(path)`: a face from a file, or nil and the reason the file is not one (docs/30).
int l_ui_load_font(lua_State* L) {
  const char* path = luaL_checkstring(L, 1);
  if (g_ui_ops.font_load == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this system has no faces");
  }
  const char* reason = nullptr;
  const int8_t handle = g_ui_ops.font_load(path, reason);
  if (handle < 0) return push_nil_error(L, "E_INVAL", (reason != nullptr) ? reason : "not a font");
  return push_font(L, handle);
}

int l_font_gc(lua_State* L) {
  LuaFont* font = static_cast<LuaFont*>(luaL_checkudata(L, 1, kFontMeta));
  if (font->handle >= 0 && g_ui_ops.font_release != nullptr) g_ui_ops.font_release(font->handle);
  font->handle = -1;
  return 0;
}

int l_font_release(lua_State* L) {
  LuaFont* font = static_cast<LuaFont*>(luaL_checkudata(L, 1, kFontMeta));
  if (font->handle >= 0 && g_ui_ops.font_release != nullptr) g_ui_ops.font_release(font->handle);
  font->handle = -1;
  lua_pushboolean(L, 1);
  return 1;
}

// `font:text_width(text [, scale])`: how wide it is, which is what centring it needs (docs/30).
int l_font_text_width(lua_State* L) {
  LuaFont* font = static_cast<LuaFont*>(luaL_checkudata(L, 1, kFontMeta));
  const char* text = luaL_checkstring(L, 2);
  const lua_Integer scale = luaL_optinteger(L, 3, 1);
  if (font->handle < 0 || g_ui_ops.font_text_width == nullptr || scale <= 0 || scale > 16) {
    lua_pushinteger(L, 0);
    return 1;
  }
  lua_pushinteger(L, g_ui_ops.font_text_width(font->handle, text, static_cast<uint8_t>(scale)));
  return 1;
}

// `font:size()`: how big a glyph is and how many codes the face carries, which is what placing a line needs
// (docs/30).
int l_font_size(lua_State* L) {
  LuaFont* font = static_cast<LuaFont*>(luaL_checkudata(L, 1, kFontMeta));
  uint8_t width = 0;
  uint8_t height = 0;
  uint8_t gap = 0;
  uint16_t glyphs = 0;
  if (font->handle < 0 || g_ui_ops.font_info == nullptr ||
      !g_ui_ops.font_info(font->handle, width, height, gap, glyphs)) {
    lua_pushinteger(L, 0);
    lua_pushinteger(L, 0);
    lua_pushinteger(L, 0);
    return 3;
  }
  lua_pushinteger(L, width);
  lua_pushinteger(L, height);
  lua_pushinteger(L, glyphs);
  return 3;
}

// `ui.text_width(text [, scale])`: how wide it is with the font of the system, which is what centring a
// line needs and what used to be a number an app guessed (docs/30).
int l_ui_text_width(lua_State* L) {
  const char* text = luaL_checkstring(L, 1);
  const lua_Integer scale = luaL_optinteger(L, 2, 1);
  if (g_ui_ops.font_text_width == nullptr || scale <= 0 || scale > 16) {
    lua_pushinteger(L, 0);
    return 1;
  }
  lua_pushinteger(L, g_ui_ops.font_text_width(-1, text, static_cast<uint8_t>(scale)));
  return 1;
}

// `ui.line_height([scale])`: how tall a line is with the font of the system, which is the other half of
// `ui.text_width` —`ui.text` puts the top of the line at `y`, so centring a glyph in a box is
// `y + (box_h - ui.line_height(scale)) / 2`— (0.121.0, docs/30). It answers a number and never an error:
// a face without a height is the five by seven, which is what the table of the flash is.
int l_ui_line_height(lua_State* L) {
  const lua_Integer scale = luaL_optinteger(L, 1, 1);
  if (g_ui_ops.font_line_height == nullptr || scale <= 0 || scale > 16) {
    lua_pushinteger(L, 0);
    return 1;
  }
  lua_pushinteger(L, g_ui_ops.font_line_height(-1, static_cast<uint8_t>(scale)));
  return 1;
}

int l_font_tostring(lua_State* L) {
  LuaFont* font = static_cast<LuaFont*>(luaL_checkudata(L, 1, kFontMeta));
  char text[64];
  uint8_t width = 0;
  uint8_t height = 0;
  uint8_t gap = 0;
  uint16_t glyphs = 0;
  if (font->handle >= 0 && g_ui_ops.font_info != nullptr &&
      g_ui_ops.font_info(font->handle, width, height, gap, glyphs)) {
    snprintf(text, sizeof(text), "font %ux%u gap %u, %u glyphs", static_cast<unsigned>(width),
             static_cast<unsigned>(height), static_cast<unsigned>(gap),
             static_cast<unsigned>(glyphs));
  } else {
    snprintf(text, sizeof(text), "font (given back)");
  }
  lua_pushstring(L, text);
  return 1;
}

// **The face a canvas draws its text with** (`cv:font(font)`), and the one of the view (`ui.font(font)`):
// nil is the font of the system (docs/30).
int8_t font_handle_of(lua_State* L, int index) {
  if (lua_isnoneornil(L, index)) return -1;
  if (!font_ready(L, index)) return -2;
  return static_cast<LuaFont*>(luaL_checkudata(L, index, kFontMeta))->handle;
}

int l_canvas_font(lua_State* L) {
  if (!canvas_ready(L, 1)) return refuse_canvas(L);
  const int8_t handle = font_handle_of(L, 2);
  const int16_t slot = canvas_slot_of(L);
  if (handle == -2 || g_ui_ops.canvas_set_font == nullptr ||
      !g_ui_ops.canvas_set_font(slot, handle)) {
    return refuse_shape(L);
  }
  // **Another face is another drawing**: the control owes a draw, or the label keeps the old letters
  // (docs/30, docs/90).
  if (g_ui_ops.canvas_invalidate != nullptr) g_ui_ops.canvas_invalidate(slot);
  lua_pushboolean(L, 1);
  return 1;
}

int l_ui_font(lua_State* L) {
  const int8_t handle = font_handle_of(L, 1);
  if (handle == -2 || g_ui_ops.app_set_font == nullptr || !g_ui_ops.app_set_font(handle)) {
    return refuse_shape(L);
  }
  lua_pushboolean(L, 1);
  return 1;
}

void open_font_library(lua_State* L) {
  luaL_newmetatable(L, kFontMeta);
  lua_createtable(L, 0, 3);
  set_function(L, "text_width", l_font_text_width);
  set_function(L, "size", l_font_size);
  set_function(L, "release", l_font_release);
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, &l_font_gc);
  lua_setfield(L, -2, "__gc");
  lua_pushcfunction(L, &l_font_tostring);
  lua_setfield(L, -2, "__tostring");
  lua_pop(L, 1);
}

// cv:add(child): **the child goes inside this canvas**, and it is the last one, so it is the layer in
// front. One father: a canvas added to another leaves the one it was in (docs/90).
int l_canvas_add(lua_State* L) {
  if (!canvas_ready(L, 1) || !canvas_ready(L, 2)) return refuse_canvas(L);
  LuaCanvas* father = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  LuaCanvas* child = static_cast<LuaCanvas*>(luaL_checkudata(L, 2, kCanvasMeta));
  // **A line of text and the dots of a page take no children** (0.101.0): they are their whole frame and their
  // painter owns every pixel of it, so a child would sit on top of the word and be part of nothing. The rest of the
  // controls that paint their own content —a list, a strip, a card of options, a calendar, a grid— have the same
  // problem and it is a redesign of those controls and not a refusal, so they still take a child today and it is
  // documented (docs/30, docs/92).
  if (ui_field_bool(L, 1, "__label", false) || ui_field_bool(L, 1, "__dots", false)) {
    return push_nil_error(L, "E_INVAL", "a label and the dots of a page take no children");
  }
  if (g_ui_ops.canvas_reparent == nullptr || !g_ui_ops.canvas_reparent(child->slot, father->slot)) {
    return push_nil_error(L, "E_INVAL", "a canvas cannot go inside that one");
  }
  // The tree the drawing follows is the app's, in its own table: the kernel keeps the layers in the order
  // they were added and this keeps the same order, so a child drawn later is the one in front. The list of
  // the father it leaves keeps a stale entry, and the draw skips it because the system has already said
  // that canvas belongs to somebody else (docs/90).
  append_child(L, 1, 2);
  // **A child of a control that is disabled is born disabled** (0.106.0): the state of a box that cannot be used is the
  // state of everything inside it, and it has to hold whether the app disabled the box before or after it put the child
  // in —`ui_set_disabled_below` only walks the children that were there when the field was written—. A control that is
  // disabled is not a stop of the walk (`ui_target_is_focusable`), so a cell added later to a page that is off is not a
  // hole a person falls into (docs/30, docs/92).
  if (ui_field_bool(L, 1, "disabled", false)) {
    ui_set_field_bool(L, 2, "disabled", true);
    ui_set_field_bool(L, 2, "__off_father", true);
    if (g_ui_focus_slot == child->slot && g_ui_focus_generation == child->generation) {
      g_ui_focus_slot = -1;
      g_ui_focus_generation = 0;
      g_ui_focus_item = 0;
      ui_set_field_bool(L, 2, "focused", false);
    }
    ui_set_disabled_below(L, 2, true, 0);
    if (child->slot >= 0 && g_ui_ops.canvas_invalidate != nullptr) {
      g_ui_ops.canvas_invalidate(child->slot);
    }
  }
  lua_pushboolean(L, 1);
  return 1;
}

// cv:draw(): **a control draws itself and then asks its children**, in the order they were added. That is
// what composing a view is: a control that painted its canvas and forgot its children is a control with a
// hole in it (docs/90). `on_draw(control)` is a field the app sets, and a control without one is a control
// that only passes the turn on.
//
// **And it only draws if it has something to redraw.** A control with nothing changed is a control that
// does nothing at all —no call, no pixels— which on a glass that pays for every region is the difference
// between a page and a page repainted on every turn. It returns **true when it drew**, so the caller (and
// the present) can tell the difference (docs/90).
int l_canvas_draw(lua_State* L) {
  if (!canvas_ready(L, 1)) return refuse_canvas(L);
  const lua_Integer depth = luaL_optinteger(L, 2, 0);
  if (depth > 16) return push_nil_error(L, "E_INVAL", "a control is nested too deep");
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  // **Whether this control is drawn on an inverted face** (0.101.0): a button with the ring or the finger paints its
  // face in ink, and what it holds is paper on ink so the words inside it read. It arrives **as an argument** and not
  // as a state of the system because a draw that throws and long-jumps out of the interpreter would leave a flag
  // behind, and the rest of the page would come out inverted (docs/90, docs/92).
  const bool inverted = lua_toboolean(L, 3) != 0;
  const bool owed = (g_ui_ops.canvas_needs_draw != nullptr) && g_ui_ops.canvas_needs_draw(canvas->slot);
  if (owed) {
    // **The box opens here**: from this line to `end_draw`, every primitive of this canvas is cut at what
    // the control owes, so a control that repaints itself paints the smallest part of the glass and not
    // the page (docs/30, docs/90).
    if (g_ui_ops.canvas_begin_draw != nullptr) g_ui_ops.canvas_begin_draw(canvas->slot);
    lua_getiuservalue(L, 1, 1);
    if (lua_istable(L, -1)) {
      lua_getfield(L, -1, "on_draw");
      if (lua_isfunction(L, -1)) {
        lua_pushvalue(L, 1);
        lua_pushboolean(L, inverted ? 1 : 0);
        if (lua_pcall(L, 2, 0, 0) != LUA_OK) return lua_error(L);
      } else {
        lua_pop(L, 1);
      }
    }
    lua_pop(L, 1);
    if (g_ui_ops.canvas_end_draw != nullptr) g_ui_ops.canvas_end_draw(canvas->slot);
    // **I el que ha pintat va al pare en aquest mateix moment** (0.189.9): the layers of a page are the order of its
    // children, and a control without a buffer —the bar of a menu, which paints its card straight into the page— is
    // painted before the buffers of the controls made before it are composed: the card of the menu reached the page and
    // the box of the terminal took it away, so `File` unfolded with nothing hanging from it (the editor and the shell,
    // found on the emulator; docs/30, docs/90).
    if (g_ui_ops.canvas_compose != nullptr) g_ui_ops.canvas_compose(canvas->slot);
  }

  // **The children are asked even when this control has nothing to redraw**: a child that changed has to
  // paint even if its father did not. And the list is **rebuilt as it is walked**: a child that the system
  // no longer says is ours —one that went away, or one that changed father— is dropped there and then, so
  // the father's list does not keep a dead object alive and does not grow with ghosts (docs/90).
  // **What the children are drawn on**: the face this control leaves behind. A button (and the box of a checkbox)
  // turns its own face to ink while it has the ring or the finger, so what it holds is drawn on ink; a button that
  // is itself on an ink face turns to paper, which is the same swap one level down (docs/92).
  // **It is a property of the control and not a list of names here** (0.106.0): a control that paints its own face in
  // ink while it has the ring, the finger or the mark says so with `__inverts`, and what it holds is drawn on ink. The
  // button and the box of a checkbox were the only two in a list inside this draw, so a `ui.list_entry` or a
  // `ui.grid_entry` —which paint their paper in ink when they are marked or focused— left their labels on paper: a white
  // patch on the ink of the row, which is what the owner saw with the board in his hand (docs/92).
  bool below = inverted;
  if (ui_field_bool(L, 1, "__inverts", false) &&
      ((ui_field_bool(L, 1, "focused", false) && g_ui_ring_shown) || ui_field_bool(L, 1, "pressed", false) ||
       ui_field_bool(L, 1, "marked", false))) {
    below = !inverted;
  }

  bool drew = owed;
  const int base = lua_gettop(L);
  lua_getiuservalue(L, 1, 1);
  if (lua_istable(L, base + 1)) {
    lua_getfield(L, base + 1, "children");
    if (lua_istable(L, base + 2)) {
      const lua_Integer count = static_cast<lua_Integer>(lua_rawlen(L, base + 2));
      lua_createtable(L, static_cast<int>(count), 0);
      for (lua_Integer i = 1; i <= count; ++i) {
        lua_rawgeti(L, base + 2, i);
        LuaCanvas* kid = static_cast<LuaCanvas*>(luaL_testudata(L, base + 4, kCanvasMeta));
        if (kid == nullptr || g_ui_ops.canvas_parent == nullptr ||
            g_ui_ops.canvas_parent(kid->slot) != canvas->slot) {
          lua_pop(L, 1);
          continue;
        }
        lua_pushvalue(L, base + 4);
        lua_rawseti(L, base + 3, static_cast<lua_Integer>(lua_rawlen(L, base + 3)) + 1);
        lua_getfield(L, base + 4, "draw");
        if (lua_isfunction(L, base + 5)) {
          lua_pushvalue(L, base + 4);
          lua_pushinteger(L, depth + 1);
          lua_pushboolean(L, below ? 1 : 0);
          if (lua_pcall(L, 3, 1, 0) != LUA_OK) return lua_error(L);
          if (lua_toboolean(L, -1)) drew = true;
          lua_pop(L, 1);
        } else {
          lua_pop(L, 1);
        }
        lua_pop(L, 1);
      }
      lua_setfield(L, base + 1, "children");
      lua_pop(L, 1);
    } else {
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);
  lua_pushboolean(L, drew ? 1 : 0);
  return 1;
}

// cv:invalidate(): the control changed what it shows —a number, a state, a word— and it has to be drawn
// again. It goes up to the fathers, because a child that changed leaves its father's buffer wrong
// (docs/90).
int l_canvas_invalidate(lua_State* L) {
  if (!canvas_ready(L, 1)) return refuse_canvas(L);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  // **With a box, that is what has to be painted.** An app that knows what changed —the digit of a clock,
  // one row of a list— says so and its `on_draw` is cut there; without it, the whole canvas (docs/30).
  if (lua_gettop(L) >= 5) {
    const lua_Integer x = luaL_checkinteger(L, 2);
    const lua_Integer y = luaL_checkinteger(L, 3);
    const lua_Integer w = luaL_checkinteger(L, 4);
    const lua_Integer h = luaL_checkinteger(L, 5);
    if (w <= 0 || h <= 0) return push_nil_error(L, "E_INVAL", "a box is a size");
    if (g_ui_ops.canvas_invalidate_rect == nullptr ||
        !g_ui_ops.canvas_invalidate_rect(canvas->slot, static_cast<uint16_t>(x),
                                         static_cast<uint16_t>(y), static_cast<uint16_t>(w),
                                         static_cast<uint16_t>(h))) {
      return push_nil_error(L, "E_INVAL", "this canvas is not on the screen any more");
    }
  } else if (g_ui_ops.canvas_invalidate == nullptr ||
             !g_ui_ops.canvas_invalidate(canvas->slot)) {
    return push_nil_error(L, "E_INVAL", "this canvas is not on the screen any more");
  }
  lua_pushboolean(L, 1);
  return 1;
}

// canvas:parent(): the canvas that holds this one, or nil when it is the view of the app (docs/90).
int l_canvas_parent(lua_State* L) {
  if (!canvas_ready(L, 1)) return refuse_canvas(L);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  if (g_ui_ops.canvas_parent == nullptr) return push_nil_error(L, "E_UNSUPPORTED", "no display");
  const int8_t father = g_ui_ops.canvas_parent(canvas->slot);
  if (father < 0) {
    lua_pushnil(L);
    return 1;
  }
  lua_pushinteger(L, father);
  return 1;
}

// canvas:caps(): how big it is and where it sits, which is what a control needs to place what it holds.
int l_canvas_caps(lua_State* L) {
  if (!canvas_ready(L, 1)) return refuse_canvas(L);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  uint16_t width = 0;
  uint16_t height = 0;
  uint16_t x = 0;
  uint16_t y = 0;
  if (g_ui_ops.canvas_caps == nullptr ||
      !g_ui_ops.canvas_caps(canvas->slot, width, height, x, y)) {
    return push_nil_error(L, "E_INVAL", "this canvas is not on the screen any more");
  }
  lua_createtable(L, 0, 4);
  lua_pushinteger(L, width);
  lua_setfield(L, -2, "width");
  lua_pushinteger(L, height);
  lua_setfield(L, -2, "height");
  lua_pushinteger(L, x);
  lua_setfield(L, -2, "x");
  lua_pushinteger(L, y);
  lua_setfield(L, -2, "y");
  return 1;
}

// canvas:damage(): how many regions of it are waiting to be composed (docs/90).
int l_canvas_damage(lua_State* L) {
  if (!canvas_ready(L, 1)) return refuse_canvas(L);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  if (g_ui_ops.canvas_damage == nullptr) return push_nil_error(L, "E_UNSUPPORTED", "no display");
  lua_pushinteger(L, g_ui_ops.canvas_damage(canvas->slot));
  return 1;
}

// canvas:release(): gives the buffer back now instead of waiting for the collector. The object is spent
// afterwards (docs/90).
// **A control that is given back takes its gesture with it** (0.113.0): a box the app releases in the middle of a swipe
// —a page that is built again, a card that closes, an app that ends with the finger on the glass— is not a box any
// more, and the finger that is still down must not move a window that nobody draws. The same for the keyboard it was
// typing into: a reference to a slot that is being given back is a number that means another control in a moment
// (docs/30, docs/90).
void ui_gesture_forget_if(LuaCanvas* canvas) {
  if (canvas == nullptr || canvas->slot < 0) return;
  // **The box the keys go to can be given back too** (0.113.0): a box that is released while the band is up leaves the
  // keyboard holding an object that will never be drawn again, and the keys would go to a window nobody sees. This is
  // asked before the slot goes, because a released slot is a number that another control takes (docs/30, docs/75).
  const bool band_up = (g_ui_ops.keyboard_visible != nullptr) && g_ui_ops.keyboard_visible();
  if (g_keyboard_target_ref != LUA_NOREF && g_state != nullptr) {
    lua_rawgeti(g_state, LUA_REGISTRYINDEX, g_keyboard_target_ref);
    LuaCanvas* typed = static_cast<LuaCanvas*>(luaL_testudata(g_state, -1, kCanvasMeta));
    const bool mine = typed != nullptr && typed->slot == canvas->slot && typed->generation == canvas->generation;
    lua_pop(g_state, 1);
    // **Una caixa que es dona amb la banda a la pantalla no s'emporta el destí** (0.179.82): the page is built again when the band
    // takes the bottom of the glass, and that rebuild happens **inside the very tap that brought the band up** —`keyboard_show`
    // tells the app, the app gives every control back and builds its page again—. Forgetting the target here left the band with
    // nothing to type into, and the next thing the same tap does —`ui_keyboard_follow_object` with the box that is gone— found no
    // target and **took the band down again**, which is the band that is painted half and disappears with the cursor (measured on
    // the emulator, 13/11). The target stays pointing at the box that went and the next turn bends it to the live box of the page
    // (`caret_poll`, 0.179.81); when the page has no box at all, `keyboard_object` answers nothing and the band goes with the next
    // finger (docs/30, docs/90).
    if (mine && !band_up) ui_keyboard_forget_target(g_state);
  }
  // **La caixa que tenia el focus i el dona l'ha de tornar a rebre** (0.179.81): a page that is built again under the band
  // releases the box a person is typing into, and the walk cannot find a slot that is gone; the place is kept here —and the
  // focus is cleared so no dead slot is left behind— for the box the band types into, which `caret_poll` puts it back on in the
  // same turn (docs/30, docs/90).
  if (g_ui_focus_slot == canvas->slot && g_ui_focus_generation == canvas->generation && band_up) {
    g_ui_focus_back_to_the_band = true;
    g_ui_focus_slot = -1;
    g_ui_focus_generation = 0;
    g_ui_focus_item = 0;
  }
  if (g_ui_touch_slot != canvas->slot || g_ui_touch_generation != canvas->generation) return;
  g_ui_touch_orphan = true;
  g_ui_touch_slot = -1;
  g_ui_touch_generation = 0;
  g_ui_touch_item = 0;
  g_ui_touch_inside = false;
  g_ui_touch_moved = false;
  g_ui_swipe_tap_armed = false;
  g_ui_swipe_moved = false;
  g_ui_swipe_page = false;
  g_ui_swipe_clean = false;
  g_ui_swipe_painted_ms = 0;
  g_ui_swipe_rest = 0;
  g_ui_swipe_list = -1;
  g_ui_touch_pressed = -1;
  g_ui_swipe_y0 = 0;
  g_ui_swipe_y = 0;
  g_ui_swipe_tap = 0;
}

// The walk that finds the object of a slot, defined with the dial's helpers at the end of this file: a control that goes away
// needs it to reach its father in the app's table (docs/30, docs/90).
bool ui_push_slot_from(lua_State* L, int object, int16_t slot, int depth);

// **Un control que marxa també surt de l'arbre que veu l'app** (0.190.257): a released child stayed in its father's
// `children` table until that father was drawn again —the draw is what compacts the table—, i un control que **compta** els
// seus fills ho pagava: una `ui.list` diu quantes pàgines té i on va cada fila comptant els fills que té, o sigui que deu files
// mortes i deu de noves la feien dir dues pàgines, posar les noves **fora del seu marc** i pintar un peu seu. El que es veia al
// vidre era una pàgina que no canviava quan s'entrava en una carpeta i dos paginadors l'un sobre l'altre (l'app `files`,
// 22/11). Una baixa és un control que ja no hi és, i l'arbre és de l'app: es treu aquí i no s'espera un dibuix.
void ui_take_out_of_the_father(lua_State* L, int object, int8_t father_slot) {
  const int top = lua_gettop(L);
  object = lua_absindex(L, object);
  if (l_ui_root(L) != 1) {
    lua_settop(L, top);
    return;
  }
  const int root = top + 1;
  if (!ui_push_slot_from(L, root, father_slot, 0)) {
    lua_settop(L, top);
    return;
  }
  const int father = top + 2;
  lua_getiuservalue(L, father, 1);
  if (lua_istable(L, -1)) {
    const int fields = father + 1;
    lua_getfield(L, fields, "children");
    if (lua_istable(L, -1)) {
      const int kids = fields + 1;
      const lua_Integer count = static_cast<lua_Integer>(lua_rawlen(L, kids));
      lua_createtable(L, static_cast<int>((count > 0) ? count : 0), 0);
      const int kept = kids + 1;
      lua_Integer n = 0;
      for (lua_Integer i = 1; i <= count; ++i) {
        lua_rawgeti(L, kids, i);
        if (lua_rawequal(L, -1, object)) {
          lua_pop(L, 1);  // the one that is going away stays out
          continue;
        }
        lua_rawseti(L, kept, ++n);
      }
      lua_pushvalue(L, kept);
      lua_setfield(L, fields, "children");
    }
  }
  lua_settop(L, top);
}

int l_canvas_release(lua_State* L) {
  if (!canvas_ready(L, 1)) return refuse_canvas(L);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  // **Qui el tenia, preguntat abans que el seu número se'n vagi**: the slot of the father is what the tree of the app needs to
  // forget the row that is being given back, and a released canvas has no slot any more.
  int8_t father = -1;
  if (g_ui_ops.canvas_parent != nullptr) father = g_ui_ops.canvas_parent(canvas->slot);
  ui_gesture_forget_if(canvas);
  if (g_ui_ops.canvas_release != nullptr) g_ui_ops.canvas_release(canvas->slot);
  canvas->slot = -1;
  canvas->generation = 0;
  if (father >= 0) ui_take_out_of_the_father(L, 1, father);
  lua_pushboolean(L, 1);
  return 1;
}

int l_canvas_gc(lua_State* L) {
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  if (canvas->slot >= 0 && g_ui_ops.canvas_alive != nullptr &&
      g_ui_ops.canvas_alive(canvas->slot, canvas->generation) && g_ui_ops.canvas_release != nullptr) {
    ui_gesture_forget_if(canvas);
    g_ui_ops.canvas_release(canvas->slot);
  }
  canvas->slot = -1;
  return 0;
}

int l_canvas_tostring(lua_State* L) {
  if (!canvas_ready(L, 1)) {
    lua_pushstring(L, "canvas (gone)");
    return 1;
  }
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  uint16_t width = 0;
  uint16_t height = 0;
  uint16_t x = 0;
  uint16_t y = 0;
  char text[48];
  bool label = false;
  bool button = false;
  bool checkbox = false;
  bool progress = false;
  bool slider = false;
  bool row = false;
  bool panel = false;
  bool tabs = false;
  bool dots = false;
  bool list = false;
  bool textbox = false;
  bool dialog = false;
  bool spark = false;
  bool chart = false;
  bool pie = false;
  bool month = false;
  bool menu = false;
  bool menu_bar = false;
  bool multiline = false;
  bool selector = false;
  bool notifications = false;
  bool icon_grid = false;
  lua_getiuservalue(L, 1, 1);
  if (lua_istable(L, -1)) {
    lua_getfield(L, -1, "__label");
    label = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__button");
    button = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__checkbox");
    checkbox = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__progress");
    progress = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__slider");
    slider = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__row");
    row = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__panel");
    panel = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__tabs");
    tabs = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__dots");
    dots = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__list");
    list = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__textbox");
    textbox = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__dialog");
    dialog = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__spark");
    spark = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__chart");
    chart = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__pie");
    pie = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__month");
    month = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__menu");
    menu = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__menu_bar");
    menu_bar = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__textbox_multiline");
    multiline = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__selector");
    selector = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__notifications");
    notifications = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__icon_grid");
    icon_grid = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  const char* kind = checkbox   ? "checkbox"
                     : button   ? "button"
                     : slider   ? "slider"
                     : progress ? "progress"
                     : row      ? "row"
                     : panel    ? "panel"
                     : tabs     ? "tabs"
                     : dots     ? "dots"
                     : list     ? "list"
                     : textbox  ? "textbox"
                     : dialog   ? "dialog"
                     : spark    ? "sparkline"
                     : chart    ? "chart"
                     : pie      ? "pie"
                     : month    ? "month"
                     : menu     ? "menu"
                     : menu_bar ? "menu bar"
                     : multiline ? "box of lines"
                     : selector ? "selector"
                     : notifications ? "notifications"
                     : icon_grid ? "icon grid"
                     : label    ? "label"
                                : "canvas";
  if (g_ui_ops.canvas_caps != nullptr && g_ui_ops.canvas_caps(canvas->slot, width, height, x, y)) {
    snprintf(text, sizeof(text), "%s %ux%u at %u,%u", kind, width, height, x, y);
  } else {
    snprintf(text, sizeof(text), "%s (gone)", kind);
  }
  lua_pushstring(L, text);
  return 1;
}

void open_canvas_library(lua_State* L) {
  luaL_newmetatable(L, kCanvasMeta);
  lua_createtable(L, 0, 14);
  set_function(L, "clear", l_canvas_clear);
  set_function(L, "rect", l_canvas_rect);
  set_function(L, "pixel", l_canvas_pixel);
  set_function(L, "invert", l_canvas_invert);
  set_function(L, "fill", l_canvas_fill);
  set_function(L, "place", l_canvas_place);
  set_function(L, "resize", l_canvas_resize);
  set_function(L, "frame", l_canvas_frame);
  set_function(L, "line", l_canvas_line);
  set_function(L, "circle", l_canvas_circle);
  set_function(L, "disc", l_canvas_disc);
  set_function(L, "text", l_canvas_text);
  set_function(L, "image", l_canvas_image);
  set_function(L, "capture", l_canvas_capture);
  set_function(L, "export", l_canvas_export);
  set_function(L, "blit", l_canvas_blit);
  set_function(L, "font", l_canvas_font);
  set_function(L, "add", l_canvas_add);
  set_function(L, "box", l_checkbox_box);
  set_function(L, "rows", l_multiline_rows);
  set_function(L, "lines", l_multiline_lines);
  set_function(L, "tap", l_tabs_tap);
  set_function(L, "draw", l_canvas_draw);
  set_function(L, "invalidate", l_canvas_invalidate);
  set_function(L, "parent", l_canvas_parent);
  set_function(L, "damage", l_canvas_damage);
  set_function(L, "caps", l_canvas_caps);
  set_function(L, "release", l_canvas_release);
  lua_setfield(L, -2, "__methods");
  lua_pushcfunction(L, &l_canvas_index);
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, &l_canvas_newindex);
  lua_setfield(L, -2, "__newindex");
  lua_pushcfunction(L, &l_canvas_gc);
  lua_setfield(L, -2, "__gc");
  lua_pushcfunction(L, &l_canvas_tostring);
  lua_setfield(L, -2, "__tostring");
  lua_pop(L, 1);
}

// **The view of the app, as a control of its own.** It is the same object every time —kept in the
// registry— so `ui.root():add(box)` and `box:add(other)` build the same tree, and `ui.line(...)` is the
// same control as `ui.root():line(...)` (docs/30, docs/90).
const char kRootCanvasKey[] = "lunokOS.ui.root";

int l_ui_root(lua_State* L) {
  if (g_ui_ops.canvas_generation == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no display");
  }
  lua_getfield(L, LUA_REGISTRYINDEX, kRootCanvasKey);
  if (lua_type(L, -1) == LUA_TUSERDATA) {
    // The view can be made again (a turn, a resize): the slot is the same and the canvas is another one,
    // so the number is refreshed instead of holding an object that answers `E_INVAL` for ever.
    LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, -1, kCanvasMeta));
    canvas->slot = 0;
    canvas->generation = g_ui_ops.canvas_generation(0);
    return 1;
  }
  lua_pop(L, 1);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(lua_newuserdatauv(L, sizeof(LuaCanvas), 1));
  canvas->slot = 0;
  canvas->generation = g_ui_ops.canvas_generation(0);
  luaL_getmetatable(L, kCanvasMeta);
  lua_setmetatable(L, -2);
  lua_createtable(L, 0, 2);
  lua_setiuservalue(L, -2, 1);
  lua_pushvalue(L, -1);
  lua_setfield(L, LUA_REGISTRYINDEX, kRootCanvasKey);
  return 1;
}


// ---------------------------------------------------------------------------
// The controls and the person: focus, the dial, the finger (docs/30, docs/90)
// ---------------------------------------------------------------------------

// The controls a person can land on, in tree order. The list is rebuilt for every event: a control
// that was released or changed father is not in the tree any more, and a cached list would press a
// button that is not there. The ceiling is the same as the tree's (docs/90).
struct FocusTarget {
  int16_t slot;
  uint16_t generation;
  // **Which tab of a strip this stop is**, 1..count, and 0 for a control that is a stop of its own: a strip of
  // tabs is one control and **one stop per tab**, as it is in the paper project, where the tabs are `Button`s and
  // the navigator walks them one at a time (0.99.0, docs/92).
  uint8_t item;
  int32_t x;
  int32_t y;
  int32_t w;
  int32_t h;
  int depth;
  int order;
};

// **The list of stops is as long as the table of controls** (0.102.0): the walk asks the board how many slots it has
// and takes a block that holds one stop per slot, then gives it back when the event is over. It used to be an array
// of thirty-two on the stack, and a page with more controls than that had its stops cut at the thirty-second
// (docs/75, docs/90).
// **The list of stops is asked once and kept** (0.189.10). Until this version it was built on **every event** —a touch
// frame, a key press, a walk of the dial— with one entry per slot of the table of canvases, which on this board is
// `2 849 × 32 B = 91 168 B` (measured on the emulator, 14/11). Two things came out of that:
//
//   - **the ask is what made the memory short**: a block of 91 168 B does not fit the extra memory of a page that already
//     holds its canvases, the allocator's relief was called, and it is the relief that takes the session memories of the
//     apps —`home_eink gives back 'apps': 32768 B, the memory is short` and then
//     `the memory is short: 1 block(s), 32768 B given back for an ask of 91168 B`, which is this line and no other—, so the
//     launcher paid its own cache for every tap it answered (docs/75);
//   - **and when the block could not be had at all, `capacity` was set to zero and the page stopped answering anything**:
//     no ring, no finger, no key, and not one line in the log to say it. The launcher's twelve cells were walked and none
//     of them became a stop, so a tap fell on the page and opened nothing (found on the emulator, 14/11, docs/30, docs/90).
//
// So: the block lives between events, it starts at the floor of a page and grows only when a page really needs more, and a
// memory that has nothing to give is asked again for less rather than leaving the page with no stops at all.
//
// **The floor is two hundred and fifty-six** (8 192 B): the longest page of this system is the list of commands of the
// shell —fifty-three stops with its title, its sentence and its `Back`— and the launcher is twelve, so the floor is five
// times the biggest and the tail of a page never falls outside it. A page that does not fit grows on its next event and
// says so once (docs/30, docs/75, docs/90).
const int kStopsFloor = 256;
// **The smallest block worth a walk**: sixteen stops are half a kilobyte and the launcher fits in them, so a memory that
// is nearly full still leaves a page answering something. There is no static array to fall back to: the eight kilobytes
// one took did not fit the internal RAM of the Paper either —`region dram0_0_seg' overflowed by 8088 bytes`, measured—
// and a fallback that does not link is not a fallback (docs/75).
const int kStopsLeast = 16;
static FocusTarget* g_stops = nullptr;
static int g_stops_capacity = 0;
// What the next block has to hold: a page that did not fit says so here, and the event after it asks for twice as much.
static int g_stops_wanted = 0;
static bool g_stops_short_said = false;

int ui_stops_ceiling() { return (g_ui_ops.slots != nullptr) ? static_cast<int>(g_ui_ops.slots()) : 32; }

// **A page that filled the block asks for twice as much next time**: one warning, and the next event of the same page
// walks with room for all of it. It is a number that grows and never shrinks, because the page that needed it is the page
// the app is on (docs/30, docs/90).
void ui_stops_more_room(int capacity) {
  int more = (capacity > 0) ? capacity * 2 : kStopsFloor * 2;
  const int ceiling = ui_stops_ceiling();
  if (more > ceiling) more = ceiling;
  if (more <= g_stops_wanted) return;
  g_stops_wanted = more;
  hal::logf(pal::kLogWarn, "UI", "the list of stops is full at %d: the page answers those and the next walk asks for %d\r\n",
            capacity, more);
}

// The block of stops of this event, and how many it holds: the same one until a page says it needs more, and **a smaller one
// rather than none** when the memory is nearly full — the ladder is the whole of what a page can do about it, and every
// refusal remembers that the next event has to ask again (docs/75, docs/90).
FocusTarget* ui_stops_block(int& capacity) {
  pal::MemoryTag who_asks(pal::kMemoryLua, app_id());
  const int ceiling = ui_stops_ceiling();
  int want = (g_stops_wanted > kStopsFloor) ? g_stops_wanted : kStopsFloor;
  if (want > ceiling) want = ceiling;
  if (g_stops == nullptr || g_stops_capacity < want) {
    for (int ask = want; ask >= kStopsLeast; ask /= 2) {
      FocusTarget* block =
          static_cast<FocusTarget*>(pal::extra_memory().allocate(sizeof(FocusTarget) * static_cast<size_t>(ask), true));
      if (block == nullptr) continue;
      // The block that is there is given back only when the new one is in hand, so a page never loses the stops it had.
      if (g_stops != nullptr) pal::extra_memory().release(g_stops);
      g_stops = block;
      g_stops_capacity = ask;
      hal::logf(pal::kLogInfo, "UI", "the list of stops: %d of %d slots, %u B\r\n", ask, ceiling,
                static_cast<unsigned>(sizeof(FocusTarget) * static_cast<size_t>(ask)));
      break;
    }
    if (g_stops == nullptr && !g_stops_short_said) {
      g_stops_short_said = true;
      hal::logf(pal::kLogWarn, "UI", "the memory has no room for the list of stops: the page answers nothing yet\r\n");
    }
  }
  capacity = g_stops_capacity;
  return g_stops;
}

// **A block of stops has no life of its own**: it is the page's and it goes when the interpreter goes, so an app that ends
// leaves nothing behind (docs/75).
void ui_stops_release() {
  if (g_stops != nullptr) pal::extra_memory().release(g_stops);
  g_stops = nullptr;
  g_stops_capacity = 0;
  g_stops_wanted = 0;
  g_stops_short_said = false;
}

struct FocusList {
  FocusTarget* items = nullptr;
  int capacity = 0;

  FocusList() { items = ui_stops_block(capacity); }
  ~FocusList() = default;
  FocusList(const FocusList&) = delete;
  FocusList& operator=(const FocusList&) = delete;
};

bool ui_field_bool(lua_State* L, int object, const char* key, bool fallback) {
  object = lua_absindex(L, object);
  lua_getiuservalue(L, object, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return fallback;
  }
  lua_getfield(L, -1, key);
  const bool value = lua_isnil(L, -1) ? fallback : (lua_toboolean(L, -1) != 0);
  lua_pop(L, 2);
  return value;
}

bool ui_set_field_bool(lua_State* L, int object, const char* key, bool value) {
  object = lua_absindex(L, object);
  lua_getiuservalue(L, object, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return false;
  }
  lua_getfield(L, -1, key);
  // **The field is always written and the answer is only about the pixels** (0.177.4, 0.179.11): answering «it changed»
  // when a field the app never wrote got its default made the walk invalidate the control the dial left **three times**
  // —`focused`, `editing` and `focus_index`— and with it every child it holds, for a ring that only moves. **And the field
  // has to be there with the default in it**: an app reads them (`#text == self.cursor`, `self.focus_index + 1`) and a
  // number that is `nil` is an error in the app and not a smaller drawing (docs/30, docs/90).
  const bool was_nil = lua_isnil(L, -1);
  const bool same = (was_nil && !value) || (!was_nil && (lua_toboolean(L, -1) != 0) == value);
  lua_pop(L, 1);
  lua_pushboolean(L, value ? 1 : 0);
  lua_setfield(L, -2, key);
  lua_pop(L, 1);
  return !same;
}

// **A number written in the fields of a control**, which is how the system moves something the app reads: `top` of a
// box of several lines is what a swipe changes and what the paint and the bar obey (0.88.0, docs/30, docs/90).
bool ui_set_field_number(lua_State* L, int object, const char* key, lua_Integer value) {
  object = lua_absindex(L, object);
  lua_getiuservalue(L, object, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return false;
  }
  lua_getfield(L, -1, key);
  // The same rule as a flag (0.177.4, 0.179.11): no `focus_index` is the tab nought of a strip that is not one, the field
  // is written with it, and the answer is only whether the pixels changed.
  const bool same = lua_isnil(L, -1) ? (value == 0)
                                     : (lua_isnumber(L, -1) && lua_tointeger(L, -1) == value);
  lua_pop(L, 1);
  lua_pushinteger(L, value);
  lua_setfield(L, -2, key);
  lua_pop(L, 1);
  return !same;
}

// **Whether a control has the hook we ask about**, which is what says whether the gesture is registered: the field
// can be `nil` and then the control has not asked for anything (0.88.0, docs/30).
bool ui_has_hook(lua_State* L, int object, const char* name) {
  object = lua_absindex(L, object);
  lua_getiuservalue(L, object, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return false;
  }
  lua_getfield(L, -1, name);
  const bool there = lua_isfunction(L, -1);
  lua_pop(L, 2);
  return there;
}

bool ui_target_is_focusable(lua_State* L, int object) {
  // **A control that is disabled is not a stop** (0.100.0): it is drawn so a person reads what is not available,
  // and none of the three ways in reaches it —the dial asks this, the finger asks this, and the keyboard only opens
  // on a box the walk gave the focus to—. It is the same rule as `focusable = false`, said with the state the app
  // gives a control instead of with the walk's own flag (docs/30, docs/92).
  if (ui_field_bool(L, object, "disabled", false)) return false;
  // **A box of text is a stop of the dial too** (0.116.0, and it was the finger's only since 0.85.2): a person with a
  // board that has keys and no touch has to be able to land on a field, and the ring on a field is the caret in it and
  // no band —the band is what the centre brings when it enters the box, which is the shape a slider has with its
  // value—. `focusable` keeps being the one flag that decides whether a control is reachable (docs/30, docs/90,
  // docs/92).
  const bool box = ui_field_bool(L, object, "__textbox", false) ||
                   ui_field_bool(L, object, "__textbox_multiline", false);
  if (box) return ui_field_bool(L, object, "focusable", true);
  const bool button = ui_field_bool(L, object, "__button", false);
  const bool checkbox = ui_field_bool(L, object, "__checkbox", false);
  const bool slider = ui_field_bool(L, object, "__slider", false);
  return ui_field_bool(L, object, "focusable", button || checkbox || slider);
}

bool ui_control_alive(LuaCanvas* canvas) {
  return canvas != nullptr && canvas->slot >= 0 && g_ui_ops.canvas_alive != nullptr &&
         g_ui_ops.canvas_alive(canvas->slot, canvas->generation);
}

void ui_invalidate(lua_State* L, int object) {
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(L, object, kCanvasMeta));
  if (ui_control_alive(canvas) && g_ui_ops.canvas_invalidate != nullptr) {
    g_ui_ops.canvas_invalidate(canvas->slot);
  }
}

// **What a move of the ring of a grid costs, asked before the whole control is thrown away** (0.177.3): the cells of a
// grid are the only thing that changes when the dial steps inside it —or when the walk gives the grid the focus, or takes
// it away—, so a call to `ui_invalidate` here paints every picture of the page for one ring that moved (docs/30, docs/90).
// The two numbers are the cell that wore the ring and the cell that wears it, from 1 and 0 for no ring, and
// `cursor_moved` says whether the cursor walked —the pages are compared and a cursor that changes page is the whole
// control— or the ring only came or went on the cell the cursor does not leave, which is the same cell twice. False when
// this control is not a grid or the kernel has no answer for it, and then the caller invalidates it the way it always did.
bool ui_invalidate_grid_ring(lua_State* L, int object, uint8_t from, uint8_t to, bool cursor_moved) {
  if (g_ui_ops.icon_grid_ring == nullptr) return false;
  object = lua_absindex(L, object);
  if (!ui_field_bool(L, object, "__icon_grid", false)) return false;
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(L, object, kCanvasMeta));
  if (!ui_control_alive(canvas)) return false;
  // **A grid of entries counts its cells in its own tree** (0.105.0), the same rule the paint and the step follow: a grid
  // whose cells the app built has nothing of the kernel to paint, and a grid of names is the kernel's own cells.
  lua_Integer cells = 0;
  lua_Integer entries = 0;
  lua_getiuservalue(L, object, 1);
  if (lua_istable(L, -1)) {
    lua_getfield(L, -1, "items");
    if (lua_istable(L, -1)) cells = static_cast<lua_Integer>(lua_rawlen(L, -1));
    lua_pop(L, 1);
    lua_getfield(L, -1, "children");
    if (lua_istable(L, -1)) entries = static_cast<lua_Integer>(lua_rawlen(L, -1));
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  // **The last word is the `children` table and not `items`** (0.178.3), which is the rule `l_icon_grid_draw` decides the
  // paint with: a control the app built wins over the words it also gave, so a grid with both painted the app's cells and
  // owed the damage of the kernel's —two answers to one question, and only one of them could be right (docs/30).
  const bool of_the_kernel = entries == 0 && cells > 0;
  lua_Integer total = of_the_kernel ? cells : entries;
  if (total <= 0) return false;
  if (total > static_cast<lua_Integer>(kMaxGridCells)) total = static_cast<lua_Integer>(kMaxGridCells);
  // The same defaults the paint uses, because a box measured with another number is not the box that was drawn.
  lua_Integer columns = ui_field_number(L, object, "columns", 0);
  lua_Integer scale = ui_field_number(L, object, "scale", 2);
  if (columns < 0) columns = 0;
  if (columns > 255) columns = 255;
  if (scale < 1) scale = 1;
  if (scale > 16) scale = 16;
  return g_ui_ops.icon_grid_ring(canvas->slot, static_cast<uint8_t>(total), from, to, cursor_moved,
                                 static_cast<uint8_t>(columns), static_cast<uint8_t>(scale), of_the_kernel);
}

// Calls one of the app's hooks on a control. `with_flag` says whether the hook takes the extra boolean, and
// `flag` is that boolean: the focus for `on_focus` and whether the box is now checked for `on_change`
// (docs/30). A hook that throws is said on the console and does not take the app down, the same as
// `on_button` (docs/30).
bool ui_call_hook(lua_State* L, int object, const char* name, bool with_flag, bool flag) {
  object = lua_absindex(L, object);
  lua_getiuservalue(L, object, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return false;
  }
  lua_getfield(L, -1, name);
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 2);
    return false;
  }
  lua_pushvalue(L, object);
  int arguments = 1;
  if (with_flag) {
    lua_pushboolean(L, flag ? 1 : 0);
    ++arguments;
  }
  if (lua_pcall(L, arguments, 0, 0) != LUA_OK && !g_exited_by_app) {
    con_text(name);
    con_text(": ");
    const char* message = lua_tostring(L, -1);
    con_text((message != nullptr) ? message : "(no message)");
    con_text("\r\n");
    lua_pop(L, 1);
  } else if (g_exited_by_app) {
    lua_pop(L, 1);  // the message of the goodbye, which is not one
  }
  lua_pop(L, 1);  // the fields table
  return true;
}

// **A field that is a number and not a yes or no**: `min`, `max`, `step` and `value` of a slider are the
// state of the control, and the system reads them without asking an app to keep a copy (docs/30, docs/90).
lua_Integer ui_field_number(lua_State* L, int object, const char* key, lua_Integer fallback) {
  object = lua_absindex(L, object);
  lua_getiuservalue(L, object, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return fallback;
  }
  lua_getfield(L, -1, key);
  const lua_Integer value = lua_isnumber(L, -1) ? lua_tointeger(L, -1) : fallback;
  lua_pop(L, 2);
  return value;
}

// **The same, for a hook that carries a number**: a slider tells its app which value it moved to, and an app
// that had to read the field back would be an app that can read a stale one (docs/30, docs/90).
bool ui_call_hook_number(lua_State* L, int object, const char* name, lua_Integer number) {
  object = lua_absindex(L, object);
  lua_getiuservalue(L, object, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return false;
  }
  lua_getfield(L, -1, name);
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 2);
    return false;
  }
  lua_pushvalue(L, object);
  lua_pushinteger(L, number);
  if (lua_pcall(L, 2, 0, 0) != LUA_OK && !g_exited_by_app) {
    con_text(name);
    con_text(": ");
    const char* message = lua_tostring(L, -1);
    con_text((message != nullptr) ? message : "(no message)");
    con_text("\r\n");
    lua_pop(L, 1);
  }
  lua_pop(L, 1);  // the fields table
  return true;
}

// **A hook with two numbers**, for the one control that answers with a pair: the bar of menus says which title and which
// entry of it, because a number alone would not say which menu the entry belongs to (docs/30, docs/90).
bool ui_call_hook_pair(lua_State* L, int object, const char* name, lua_Integer first, lua_Integer second) {
  object = lua_absindex(L, object);
  lua_getiuservalue(L, object, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return false;
  }
  lua_getfield(L, -1, name);
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 2);
    return false;
  }
  lua_pushvalue(L, object);
  lua_pushinteger(L, first);
  lua_pushinteger(L, second);
  if (lua_pcall(L, 3, 0, 0) != LUA_OK && !g_exited_by_app) {
    con_text(name);
    con_text(": ");
    const char* message = lua_tostring(L, -1);
    con_text((message != nullptr) ? message : "(no message)");
    con_text("\r\n");
    lua_pop(L, 1);
  } else if (g_exited_by_app) {
    lua_pop(L, 1);  // the message of the goodbye, which is not one
  }
  lua_pop(L, 1);  // the fields table
  return true;
}

// The same, for a hook that carries a text: the keyboard typed into the box and the box tells its app which word
// it now holds, with the field already written (docs/30, docs/90).
bool ui_call_hook_text(lua_State* L, int object, const char* name, const char* text) {
  object = lua_absindex(L, object);
  lua_getiuservalue(L, object, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return false;
  }
  lua_getfield(L, -1, name);
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 2);
    return false;
  }
  lua_pushvalue(L, object);
  lua_pushstring(L, (text != nullptr) ? text : "");
  if (lua_pcall(L, 2, 0, 0) != LUA_OK && !g_exited_by_app) {
    con_text(name);
    con_text(": ");
    const char* message = lua_tostring(L, -1);
    con_text((message != nullptr) ? message : "(no message)");
    con_text("\r\n");
    lua_pop(L, 1);
  } else if (g_exited_by_app) {
    lua_pop(L, 1);  // the message of the goodbye, which is not one
  }
  lua_pop(L, 1);  // the fields table
  return true;
}

// **How many tabs a strip carries**, asked of the table the control keeps its labels in, and zero for anything
// that is not a strip: the walk needs the number to give each tab a stop of its own (0.99.0, docs/90, docs/92).
int ui_tabs_items(lua_State* L, int object) {
  object = lua_absindex(L, object);
  if (!ui_field_bool(L, object, "__tabs", false)) return 0;
  lua_getiuservalue(L, object, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  lua_getfield(L, -1, "items");
  const int count = lua_istable(L, -1) ? static_cast<int>(lua_rawlen(L, -1)) : 0;
  lua_pop(L, 2);
  return (count > 0) ? count : 0;
}

// **A grid with the app's own cells in it is a box and not a stop** (0.156.1): the stops are the cells it holds, and the
// grid as a stop of its own was a phantom the dial could land on —the ring was the whole box of the grid, which the
// control does not paint, so it looked like the ring had left the page and the walk went on from there in the order of
// the tree instead of round the page (the owner: *«sembla que et pots sortir per l'esquerra i per la dreta amb el
// focus»*, 8/11)—. A grid of its own items is still a stop, because there the cells are the control's and `index`
// walks them (docs/90, docs/92).
bool ui_grid_has_cells(lua_State* L, int object) {
  object = lua_absindex(L, object);
  if (!ui_field_bool(L, object, "__icon_grid", false)) return false;
  lua_getiuservalue(L, object, 1);
  bool cells = false;
  if (lua_istable(L, -1)) {
    lua_getfield(L, -1, "children");
    cells = lua_istable(L, -1) && lua_rawlen(L, -1) > 0;
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  return cells;
}

// Walks the tree the drawing walks: the children of the app, in their order, cut at the frame of
// their fathers. A control is a target when it is a button or when the app said `focusable`; a label
// inside a button is not one, which is what makes the button the thing a person lands on.
// **The card that is over the page** (0.172.0) and whether a control is it or inside it: the walk of the focus asks this for
// every control while a card is up, following the chain of fathers —the same chain the clipping follows— (docs/30).
extern int16_t g_modal_slot;
bool ui_in_the_card(int16_t slot);
bool ui_leads_to_the_card(int16_t slot);

void ui_walk_targets(lua_State* L, int object, int32_t base_x, int32_t base_y, int32_t clip_x0,
                     int32_t clip_y0, int32_t clip_x1, int32_t clip_y1, int depth,
                     FocusTarget* targets, int capacity, int& count, int& order, int objects) {
  object = lua_absindex(L, object);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(L, object, kCanvasMeta));
  if (!ui_control_alive(canvas)) return;
  // **While a card is up the walk is the card's** (0.172.0): a control that is not in the card and does not lead to it is not a
  // stop and its children are not walked, which is what keeps a key press from landing on a page behind a question nobody
  // answered. The page and the fathers of the card are walked **through** —the card's boxes have to be in the page's pixels—
  // and they are not stops themselves (docs/30, docs/90).
  if (!ui_in_the_card(canvas->slot) && !ui_leads_to_the_card(canvas->slot)) return;

  // **The cells of a grid are stops even when they are on another page of it** (0.157.0): the promise of a grid is
  // that **the page follows the cursor**, so the dial has to be able to walk out of the first page into the second —the
  // cell it lands on is placed on the glass by the grid when the app moves its `index`, which is what the `on_focus` of
  // a cell is for—. A walk that only saw the cells of the page could never leave it, and a launcher with more apps than
  // fit had no way of reaching the rest (docs/90, docs/92).
  const bool in_a_grid = ui_grid_has_cells(L, object);
  const int base = lua_gettop(L);
  lua_getiuservalue(L, object, 1);
  if (!lua_istable(L, -1)) {
    lua_settop(L, base);
    return;
  }
  lua_getfield(L, -1, "children");
  if (!lua_istable(L, -1)) {
    lua_settop(L, base);
    return;
  }
  const lua_Integer children = static_cast<lua_Integer>(lua_rawlen(L, -1));
  for (lua_Integer i = 1; i <= children; ++i) {
    lua_rawgeti(L, -1, i);
    LuaCanvas* child = static_cast<LuaCanvas*>(luaL_testudata(L, -1, kCanvasMeta));
    if (child == nullptr || child->slot < 0 || g_ui_ops.canvas_parent == nullptr ||
        g_ui_ops.canvas_parent(child->slot) != canvas->slot) {
      lua_pop(L, 1);
      continue;
    }
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t local_x = 0;
    uint16_t local_y = 0;
    if (g_ui_ops.canvas_caps == nullptr ||
        !g_ui_ops.canvas_caps(child->slot, width, height, local_x, local_y)) {
      lua_pop(L, 1);
      continue;
    }
    const int32_t x = base_x + local_x;
    const int32_t y = base_y + local_y;
    const int32_t x1 = x + width;
    const int32_t y1 = y + height;
    const int32_t cx0 = (x > clip_x0) ? x : clip_x0;
    const int32_t cy0 = (y > clip_y0) ? y : clip_y0;
    const int32_t cx1 = (x1 < clip_x1) ? x1 : clip_x1;
    const int32_t cy1 = (y1 < clip_y1) ? y1 : clip_y1;
    if (cx1 <= cx0 || cy1 <= cy0) {
      // A cell of a grid that is on another page: it is a stop, and the box it has now is the one the grid gave it out
      // of sight, which is a box no finger can land on (see the note above).
      if (!in_a_grid) {
        lua_pop(L, 1);
        continue;
      }
    }

    const bool focusable_here = ui_target_is_focusable(L, -1);
    const bool grid_here = ui_grid_has_cells(L, -1);
    if (ui_in_the_card(child->slot) && focusable_here && !grid_here) {
      // **A strip of tabs is one stop per tab** (0.99.0): the arrows walk into the strip and from one tab to the
      // next, out of it at either end, and the centre chooses the tab the ring is on. One stop for the whole strip
      // made the arrows change the tab instead of moving the dial, which is the owner's complaint of 7/11 and what
      // the paper project never did —there the tabs are buttons and the navigator walks them (docs/90, docs/92).
      const int items = ui_tabs_items(L, -1);
      if (items > 0) {
        // **Les pestanyes poden caure en més d'una fila** (0.189.106): the cells are the strip's own grid —the one the paint
        // uses— so a finger on the second row chooses a tab of the second row and the arrows walk the tabs in the order the
        // glass shows them (docs/30, docs/90).
        uint8_t grid_rows = 1;
        uint8_t grid_columns = static_cast<uint8_t>((items > 255) ? 255 : items);
        if (g_ui_ops.tabs_grid != nullptr) {
          g_ui_ops.tabs_grid(child->slot, static_cast<uint8_t>((items > 255) ? 255 : items), &grid_rows, &grid_columns);
        }
        if (grid_rows == 0) grid_rows = 1;
        if (grid_columns == 0) grid_columns = 1;
        const int32_t span = cx1 - cx0;
        const int32_t tall = cy1 - cy0;
        for (int tab = 0; tab < items && count < capacity; ++tab) {
          const int32_t row = tab / grid_columns;
          const int32_t col = tab % grid_columns;
          if (row >= grid_rows) break;
          const int32_t left = cx0 + (span * col) / grid_columns;
          const int32_t right = cx0 + (span * (col + 1)) / grid_columns;
          const int32_t top = cy0 + (tall * row) / grid_rows;
          const int32_t bottom = cy0 + (tall * (row + 1)) / grid_rows;
          if (right <= left || bottom <= top) continue;
          FocusTarget& target = targets[count];
          target.slot = child->slot;
          target.generation = child->generation;
          target.item = static_cast<uint8_t>(tab + 1);
          target.x = left;
          target.y = top;
          target.w = right - left;
          target.h = bottom - top;
          target.depth = depth;
          target.order = order++;
          lua_pushvalue(L, -1);
          lua_rawseti(L, objects, count + 1);
          ++count;
        }
        // A strip that did not fit the block of stops says so, like any other page (0.189.10).
        if (count >= capacity) ui_stops_more_room(capacity);
      } else if (count < capacity) {
        FocusTarget& target = targets[count];
        target.slot = child->slot;
        target.generation = child->generation;
        target.item = 0;
        target.x = cx0;
        target.y = cy0;
        target.w = cx1 - cx0;
        target.h = cy1 - cy0;
        target.depth = depth;
        target.order = order++;
        lua_pushvalue(L, -1);
        lua_rawseti(L, objects, count + 1);
        ++count;
      } else {
        // **La pàgina no hi cap i ho diu** (0.189.10): the block of stops was full, so this control is not a stop of this
        // walk and the page would answer everything else and not it. The next walk of the same page asks for twice as much
        // (docs/30, docs/90).
        ui_stops_more_room(capacity);
      }
    }
    ui_walk_targets(L, -1, x, y, cx0, cy0, cx1, cy1, depth + 1, targets, capacity, count,
                    order, objects);
    lua_pop(L, 1);  // the child
  }
  lua_settop(L, base);
}

// Builds the list and **leaves the Lua objects in a table on top of the stack**: both the event and
// the thing the event changes live in Lua, and finding a slot again after a callback is a search that
// can hit a released control. The caller pops the table.
// **The card that is over the page** (0.172.0): while a `ui.dialog` is up, the dial, the finger and the middle key belong to
// it and to nothing else. The owner saw the failure on the eraser: *«els botons físics continuen amb el focus de l'app quan hi
// ha un diàleg modal obert»*, which is a page where a key press can start something **under** a question nobody answered. The
// app does not have to hide its controls while a card is up —an app that had to would be an app that forgets— because the walk
// of the focus is the system's and the system is what knows there is a card (docs/30, docs/90).
int16_t g_modal_slot = -1;
// **Quina mena de targeta és** (0.192.39, l'amo: *«El menú contextual de Files no reacciona gaire bé i passa l'event a la vista … El
// menú contextual es comporta com un modal, sempre sobre el resta de la vista i s'ha de tancar per tornar a enviar events a la
// vista. Es tanca com el teclat virtual o un modal: tocant fora»*): a question of the app closes when the app says so, and a **menu**
// is the system's to fold —the finger that lands outside it is not an answer to anything, and a menu that stays because the app
// forgot to close it is a page nobody can press—. The kind is what the press asks before it decides who closes what (docs/30, docs/90).
enum ModalKind : uint8_t { kModalCard = 0, kModalMenu, kModalBar };
ModalKind g_modal_kind = kModalCard;
// **Who had the ring when the card came up** (0.172.0): a card is a question and the ring goes to its first door; when the card
// goes the ring goes back to the control it was on, which is what the owner asked for —*«quan el modal desapareix, tornen els
// focus de l'app»*—. The three numbers are the three of a stop, because a slot alone is a number a released control forgets
// (docs/30, docs/90).
// **The card that was up, with its generation** (0.172.1): a present asks whether it is still alive before it does anything, and
// a slot alone cannot answer that —a released control gives its slot to the next one— (docs/90).
int16_t g_modal_card_slot = -1;
uint16_t g_modal_card_generation = 0;
int16_t g_modal_was_slot = -1;
uint16_t g_modal_was_generation = 0;
uint8_t g_modal_was_item = 0;
bool g_modal_was_up = false;

// **The two questions about a stop and the one that moves the ring** (0.172.0): they are defined below with the rest of the
// focus machinery, and the collection of stops needs them before that.
int ui_target_index(const FocusTarget* targets, int count, int16_t slot, uint16_t generation, uint8_t item);
void ui_apply_focus(lua_State* L, const FocusTarget* targets, int count, int objects, int index);

bool ui_in_the_card(int16_t slot) {
  if (g_modal_slot < 0) return true;
  if (g_ui_ops.canvas_parent == nullptr) return false;
  for (int16_t walk = slot; walk >= 0; walk = g_ui_ops.canvas_parent(walk)) {
    if (walk == g_modal_slot) return true;
  }
  return false;
}

// **Whether this control is on the way to the card** (0.172.0), asked while a card is up: the walk has to go **through** the
// page and through every father of the card to reach it, and it must not go anywhere else.
bool ui_leads_to_the_card(int16_t slot) {
  if (g_modal_slot < 0 || g_ui_ops.canvas_parent == nullptr) return false;
  for (int16_t walk = g_modal_slot; walk >= 0; walk = g_ui_ops.canvas_parent(walk)) {
    if (walk == slot) return true;
  }
  return false;
}

// **The control that has a slot, wherever it is in the tree** (0.172.0): it is what lets the system take the ring off the
// control that had it when a card comes up. The app cannot be asked to do it —it is the one that would forget— so the walk
// gives the object and the ring is written here (docs/30, docs/90). The three lines of the stack are the ones the finder of
// the card needed three tries to get right: `replace` to put the found object where the levels of the walk were and `settop`
// after it, because `replace` alone leaves the levels above.
bool ui_push_slot_from(lua_State* L, int object, int16_t slot, int depth) {
  object = lua_absindex(L, object);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(L, object, kCanvasMeta));
  if (!ui_control_alive(canvas)) return false;
  if (canvas->slot == slot) {
    lua_pushvalue(L, object);
    return true;
  }
  if (depth >= 8) return false;
  const int base = lua_gettop(L);
  lua_getiuservalue(L, object, 1);
  if (!lua_istable(L, -1)) {
    lua_settop(L, base);
    return false;
  }
  lua_getfield(L, -1, "children");
  if (!lua_istable(L, -1)) {
    lua_settop(L, base);
    return false;
  }
  const lua_Integer children = static_cast<lua_Integer>(lua_rawlen(L, -1));
  for (lua_Integer i = 1; i <= children; ++i) {
    lua_rawgeti(L, -1, i);
    const bool found = ui_push_slot_from(L, lua_gettop(L), slot, depth + 1);
    if (found) {
      lua_replace(L, base + 1);
      lua_settop(L, base + 1);
      return true;
    }
    lua_pop(L, 1);  // the child, and the table of children stays for the next one
  }
  lua_settop(L, base);
  return false;
}

// Pushes the card if there is one, walking the tree the way the dial walks it; the object stays on the stack and nothing else
// does. A nested card is the one that answers, which is why the walk stops at the first one it finds.
bool ui_push_modal_from(lua_State* L, int object, int depth, bool with_the_apps_card = true) {
  object = lua_absindex(L, object);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(L, object, kCanvasMeta));
  if (!ui_control_alive(canvas)) return false;
  // **I una pàgina que una app aixeca com una pregunta, també** (0.192.40, l'amo: *«Fes-lo en una vista nova i menys problema»*):
  // a `ui.panel` that covers the glass and carries the question is a card in everything but its name, and the field an app
  // writes —`modal = true`— is the one this walk reads. Without it the finger of a button of the question went to the row of
  // the list that was under it —the list is deeper in the tree and `ui_hit` takes the deepest— and `Create` created nothing and
  // opened a folder instead (found on the emulator with `Files` → `New...`) (docs/30, docs/90).
  if (ui_field_bool(L, object, "__dialog", false) ||
      (with_the_apps_card && ui_field_bool(L, object, "modal", false))) {
    lua_pushvalue(L, object);
    return true;
  }
  // **Un menú és una targeta mentre és a la pàgina** (0.192.39): a `ui.menu` is open for as long as it exists —the app makes it
  // when the question is asked and gives it back when the answer arrives— o sigui que mentre hi és, el que hi ha sota no és el
  // que el dit toca. És el que fa que un menú contextual sigui el mateix cas que una targeta i no un control de més (docs/30, docs/90).
  if (ui_field_bool(L, object, "__menu", false)) {
    lua_pushvalue(L, object);
    return true;
  }
  // **I una barra de menús amb un desplegable obert, també**: the rest of the page is not the page while a menu is down, and the
  // bar is the one that gives the finger its entries and its titles (docs/30, docs/90).
  if (ui_field_bool(L, object, "__menu_bar", false) && ui_field_number(L, object, "open", 0) > 0) {
    lua_pushvalue(L, object);
    return true;
  }
  if (depth >= 8) return false;
  const int base = lua_gettop(L);
  lua_getiuservalue(L, object, 1);
  if (!lua_istable(L, -1)) {
    lua_settop(L, base);
    return false;
  }
  lua_getfield(L, -1, "children");
  if (!lua_istable(L, -1)) {
    lua_settop(L, base);
    return false;
  }
  const lua_Integer children = static_cast<lua_Integer>(lua_rawlen(L, -1));
  for (lua_Integer i = 1; i <= children; ++i) {
    lua_rawgeti(L, -1, i);
    const bool found = ui_push_modal_from(L, lua_gettop(L), depth + 1, with_the_apps_card);
    if (found) {
      // **The card goes down to where the walk was and the walk's own levels go**: `lua_replace` is the one that keeps the
      // value and drops everything above it. Two earlier versions of these three lines were wrong in two different ways —a
      // `settop` that dropped the card and kept the last table of the tree, and a `remove(-2)` that took the table of children
      // away when the answer was «no»— and both ended in a segmentation fault; the bench is what said so (docs/90).
      lua_replace(L, base + 1);
      // **And what the walk pushed above it goes**: `lua_replace` puts the card where the levels of the finder were and pops the
      // copy, but the levels themselves are still above it, so the stack is cut to the card. Without this the card never came
      // back to the caller —the `settop` and the `remove` of the two earlier versions each broke the stack in its own way, and
      // this is the third and the one that the bench says is right— (docs/90).
      lua_settop(L, base + 1);
      return true;
    }
    lua_pop(L, 1);  // the child, and nothing else: the table of children stays for the next one
  }
  lua_settop(L, base);
  return false;
}

int ui_collect_targets(lua_State* L, FocusTarget* targets, int capacity) {
  const int top = lua_gettop(L);
  lua_createtable(L, 0, 0);
  const int objects = top + 1;
  UIInfo info = {};
  if (g_ui_ops.caps == nullptr || !g_ui_ops.caps(info)) return 0;
  if (l_ui_root(L) != 1) {
    lua_settop(L, objects);
    return 0;
  }
  int count = 0;
  int order = 0;
  const int root_at = lua_gettop(L);
  // **A card over the page takes the whole walk** (0.172.0): the list of stops is the card's doors and nothing else, so the
  // dial cannot leave a question unanswered and a finger on the page behind it is not a press of what is there (docs/30).
  g_modal_slot = -1;
  g_modal_kind = kModalCard;
  // **The walk is the root's and the card is a filter** (0.172.0): walking **from** the card was the first attempt and it gave
  // no stops at all —the walk adds a control after its children, so starting at the card asked for the doors of a card that
  // has none in the tree—, and every box of the walk has to be in the page's own pixels anyway. So the tree is walked whole
  // and every control that is not the card, or inside it, is not a stop (docs/30, docs/90).
  // **Les targetes del sistema primer, i la de l'app després** (0.192.40): a menu or a dialog that an app has put up over its own
  // page of a question is the one that answers —the dropdown of the `Kind` of `Files` was unreachable because the page under it
  // was found first—, and the app's own card is the answer only when the system has nothing over it (docs/30, docs/90).
  int modal_up = 0;
  if (ui_push_modal_from(L, root_at, 0, /*with_the_apps_card=*/false)) {
    modal_up = 1;
  } else if (ui_push_modal_from(L, root_at, 0, /*with_the_apps_card=*/true)) {
    modal_up = 2;
  }
  if (modal_up != 0) {
    const int modal = lua_gettop(L);
    LuaCanvas* card = static_cast<LuaCanvas*>(luaL_testudata(L, modal, kCanvasMeta));
    if (card != nullptr) {
      g_modal_slot = card->slot;
      g_modal_card_slot = card->slot;
      g_modal_card_generation = card->generation;
      g_modal_kind = ui_field_bool(L, modal, "__menu", false)
                         ? kModalMenu
                         : (ui_field_bool(L, modal, "__menu_bar", false) ? kModalBar : kModalCard);
    }
    lua_pop(L, 1);  // the card
  }
  ui_walk_targets(L, root_at, 0, 0, 0, 0, info.width, info.height, 0, targets, capacity, count, order, objects);
  lua_pop(L, 1);  // the root
  // **A card que apareix es queda el dial** (0.189.136): while a card is up the stops are its own doors and nothing else, so a
  // control of the page that had the focus is not a stop any more — and the key pressed then found no place (`which < 0`), gave
  // the keys back and did nothing at all: a card that cannot be answered until the page's control lets go of the ring. A finger
  // on a door of the page leaves exactly that focus, and the warning it opens was asking *Format the SD card?* with nobody able
  // to say yes. The first door of the card takes the dial, and the block below gives it back to the page when the card goes
  // (docs/30, docs/90).
  if (g_modal_slot >= 0 && count > 0 &&
      ui_target_index(targets, count, g_ui_focus_slot, g_ui_focus_generation, g_ui_focus_item) < 0) {
    ui_apply_focus(L, targets, count, objects, 0);
  }
  // **And the card gone gives the ring back** (0.172.0): the control that had it takes it again if it is still there, and a
  // page that was built again in the meantime leaves the ring where the app put it (docs/30, docs/90). **One block and not
  // two** (0.173.1): the same six lines were written twice, one after the other, and the second could never run —the first
  // clears `g_modal_was_up`— so it was a copy left behind and a reader's trap.
  if (g_modal_was_up) {
    g_modal_was_up = false;
    const int back = ui_target_index(targets, count, g_modal_was_slot, g_modal_was_generation, g_modal_was_item);
    if (back >= 0) ui_apply_focus(L, targets, count, objects, back);
    g_modal_was_slot = -1;
    g_modal_was_generation = 0;
    g_modal_was_item = 0;
    g_modal_card_slot = -1;
    g_modal_card_generation = 0;
  }
  return count;
}

// **A card that has gone gives the ring back**: the walk that notices it is the one that hands it back, and an app that closed
// its card and then only waits would keep a page with no ring anywhere (0.172.0, docs/30).
void ui_restore_the_ring_if_a_card_went(lua_State* L) {
  if (!g_modal_was_up) return;
  // **And only when the card is really gone** (0.172.1): asking for the stops on every present while the card is up put a walk
  // and a focus —and the hooks of the app behind them— inside the app's own draw, which is what the owner saw as *«el focus fa
  // coses rares»*. The card's slot and generation say whether it is still there (docs/30, docs/90).
  if (g_ui_ops.canvas_alive != nullptr && g_modal_card_slot >= 0 &&
      g_ui_ops.canvas_alive(g_modal_card_slot, g_modal_card_generation) &&
      (g_ui_ops.canvas_pending == nullptr || !g_ui_ops.canvas_pending(g_modal_card_slot))) {
    return;
  }
  const int top = lua_gettop(L);
  FocusList list;
  ui_collect_targets(L, list.items, list.capacity);
  // **The table the collection leaves behind goes**: it is the objects of the stops, and its callers keep it on purpose —they
  // read the objects out of it— and cut the stack themselves. A call from here is not one of them, and the first version of
  // this left it on the stack: `ui.present('gc16')` then read the table as its mode and the bench said
  // `bad argument #1 to 'present' (string expected, got table)` (docs/90).
  lua_settop(L, top);
}

int ui_target_index(const FocusTarget* targets, int count, int16_t slot, uint16_t generation,
                    uint8_t item) {
  for (int i = 0; i < count; ++i) {
    if (targets[i].slot == slot && targets[i].generation == generation && targets[i].item == item) {
      return i;
    }
  }
  return -1;
}

// **The page of a grid follows the ring and the app does not write it** (0.179.4): the cursor of a grid is `index`, and
// every launcher of this system kept it in the `on_focus` of its cells —one line per cell, and the one an app forgets is a
// page that does not turn—. The control knows its cells and the ring is the system's, so the number is written here, in
// the one place where the focus arrives: a cell of another page that takes the ring is a page the grid paints again, and
// the footer of the control says which page it is without the app being asked (docs/30, docs/90). A grid of its own items
// has no cells to follow —its `index` is the cursor and the arrows of the dial move it, in `ui_step_value`.
void ui_grid_follow_the_cell(lua_State* L, int object) {
  if (g_ui_ops.canvas_parent == nullptr) return;
  LuaCanvas* cell = static_cast<LuaCanvas*>(luaL_testudata(L, object, kCanvasMeta));
  if (cell == nullptr || cell->slot < 0) return;
  const int16_t father = g_ui_ops.canvas_parent(cell->slot);
  if (father < 0) return;
  const int base = lua_gettop(L);
  if (l_ui_root(L) != 1 || !ui_push_slot_from(L, base + 1, father, 0)) {
    lua_settop(L, base);
    return;
  }
  lua_remove(L, base + 1);  // the root
  const int grid = base + 1;
  if (!ui_field_bool(L, grid, "__icon_grid", false)) {
    lua_settop(L, base);
    return;
  }
  // The place of the cell among the grid's children, which is the number `index` counts from one: the order the grid
  // counts its cells in is the order it holds them (docs/30, docs/90).
  int place = 0;
  lua_getiuservalue(L, grid, 1);
  if (lua_istable(L, -1)) {
    lua_getfield(L, -1, "children");
    if (lua_istable(L, -1)) {
      const lua_Integer children = static_cast<lua_Integer>(lua_rawlen(L, -1));
      for (lua_Integer i = 1; i <= children && place == 0; ++i) {
        lua_rawgeti(L, -1, i);
        LuaCanvas* child = static_cast<LuaCanvas*>(luaL_testudata(L, -1, kCanvasMeta));
        if (child != nullptr && child->slot == cell->slot) place = static_cast<int>(i);
        lua_pop(L, 1);
      }
    }
  }
  if (place > 0) {
    // **What the move costs is the kernel's answer, the same one a field the app writes gets** (0.177.3): a cursor that walks
    // inside its page paints the cell it leaves and the cell it enters —the paper of the grid covers its cells, so a whole
    // control here is every picture of the page painted for one ring— and only a cursor that changes page repaints it
    // (docs/30, docs/90).
    const lua_Integer before = ui_field_number(L, grid, "index", 0);
    if (ui_set_field_number(L, grid, "index", place)) {
      const uint8_t from = (before >= 1) ? static_cast<uint8_t>(before) : 0;
      const uint8_t to = (place >= 1) ? static_cast<uint8_t>(place) : 0;
      if (!ui_invalidate_grid_ring(L, grid, from, to, true)) ui_invalidate(L, grid);
    }
  }
  lua_settop(L, base);
}

// **The selector of the focus comes and goes with the hands** (0.179.7): the first key of the board brings it up where the
// dial is, the first finger puts it away. What this does is the flag and the damage of the control that has the dial —the
// ring is ink of that box and nothing else on the page changes—; the present is the caller's, because the key that comes
// back with a ring always paints something of its own and the finger's landing may paint nothing at all (docs/30,
// docs/90). False when the flag was already what the caller wanted, and then nothing is owed.
bool ui_ring_shown(bool on) {
  if (g_ui_ring_shown == on) return false;
  g_ui_ring_shown = on;
  if (g_state == nullptr || g_ui_focus_slot < 0) return true;
  const int base = lua_gettop(g_state);
  if (l_ui_root(g_state) == 1 && ui_push_slot_from(g_state, base + 1, g_ui_focus_slot, 0)) {
    LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(g_state, -1, kCanvasMeta));
    if (canvas != nullptr && canvas->generation == g_ui_focus_generation) {
      // **And a grid of the kernel's cells is asked for the one cell that wears the ring** (0.177.3, found again here at
      // 0.179.11): the ring coming or going is ink of that cell and the cursor does not move, so the pages of a grid were
      // being repainted whole —every picture of the page— for a ring, which is the law of the damage broken by the door
      // that hid it (docs/30, docs/90).
      const lua_Integer cell = ui_field_number(g_state, -1, "index", 0);
      const uint8_t at = (cell >= 1) ? static_cast<uint8_t>(cell) : 0;
      if (!ui_invalidate_grid_ring(g_state, -1, at, at, false)) ui_invalidate(g_state, -1);
    }
  }
  lua_settop(g_state, base);
  return true;
}

// ---- qui té les tecles mentre una persona edita (0.181.0) ----
//
// **El listener és més avall**, after `ui_activate`: it is the middle key that enters and leaves a control, so the
// listener that answers the centre needs it already defined, and here it is only named for the place it takes in the queue
// (docs/30, docs/90).
void ui_keys_listener(const char* kind, uint32_t key, int64_t held_ms, void* owner);

// **Deixa la plaça** is defined below, after the listener that can need it on the road out (docs/30).
void ui_release_the_keys(lua_State* L, int object);

// ---- qui té les tecles mentre una persona edita (0.181.0) ----
//

// **La plaça de la banda** (0.181.3): a band with no field entered takes the keys and does nothing with them —there is nothing to
// type into—, and what it buys is that the system's own delivery does not hear the press at all: the three taps cannot close the
// app while a person has the keyboard on the glass (docs/30, docs/90).
void ui_band_listener(const char* kind, uint32_t key, int64_t held_ms, void* owner) {
  (void)kind;
  (void)key;
  (void)held_ms;
  (void)owner;
}

// **Deixa la plaça, i només si és seva**: a control that leaves gives the keys back, and a control that was never the one
// holding them does not take them away from the one that is (docs/30, docs/90).
void ui_release_the_keys(lua_State* L, int object) {
  if (g_keys_handle == 0) return;
  if (object != 0) {
    LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(L, object, kCanvasMeta));
    if (canvas != nullptr && (canvas->slot != g_keys_slot || canvas->generation != g_keys_generation)) return;
  }
  if (g_button_ops.unsubscribe != nullptr) g_button_ops.unsubscribe(g_keys_handle);
  g_keys_handle = 0;
  g_keys_slot = -1;
  g_keys_generation = 0;
}

// **El control entrat es queda les tecles**: one exclusive place in the queue, taken with the slot of the control so the
// listener knows whose keys they are. A second call for the same control does nothing —`editing` is read once per key and
// not once per turn— (docs/30, docs/90).
void ui_hold_the_keys(lua_State* L, int object) {
  if (g_button_ops.subscribe == nullptr) return;
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(L, object, kCanvasMeta));
  if (canvas == nullptr || canvas->slot < 0) return;
  if (g_keys_handle != 0 && canvas->slot == g_keys_slot && canvas->generation == g_keys_generation) return;
  ui_release_the_keys(L, 0);
  g_keys_handle = g_button_ops.subscribe(&ui_keys_listener, nullptr, /*exclusive=*/1);
  if (g_keys_handle != 0) {
    g_keys_slot = canvas->slot;
    g_keys_generation = canvas->generation;
  }
}

// **Una sola regla per a la caixa, la multilínia i l'slider**: the state of the control is the truth. While it says
// `editing` a person is inside it and the keys are its own; when it stops saying it —the centre, the dial, a page that took
// the focus away— the place goes back and the dial is the page's again (docs/30, docs/90).
void ui_keys_follow_the_control(lua_State* L, int object) {
  if (ui_field_bool(L, object, "editing", false)) {
    ui_hold_the_keys(L, object);
  } else {
    ui_release_the_keys(L, object);
  }
}

// **The ring taken from a control, said once** (0.179.4): it stops being focused —and a grid is asked for that one cell,
// because the paper of the control did not change—, it is not entered any more (the boxes and the slider, 0.116.0 and
// 0.117.0) and it stops being the lit tab of its strip (0.99.0). The caller leaves the stack as it found it (docs/30,
// docs/92).
void ui_take_the_focus_from(lua_State* L, int object) {
  if (ui_set_field_bool(L, object, "focused", false)) {
    // **The ring of a grid leaves one cell** (0.177.3): the paper of the control and the other cells of the page did not
    // change, so the grid is asked for that cell alone —the cursor does not move, so the cell is given twice and no page
    // is compared— and the whole control is only painted when the kernel has no answer (docs/30, docs/90).
    const lua_Integer cell = ui_field_number(L, object, "index", 0);
    const uint8_t at = (cell >= 1) ? static_cast<uint8_t>(cell) : 0;
    if (!ui_invalidate_grid_ring(L, object, at, at, false)) {
      ui_invalidate(L, object);
    }
    ui_call_hook(L, object, "on_focus", true, false);
  }
  // **A control the dial leaves is not entered any more** (0.116.0 for the boxes, 0.117.0 for the slider): the other
  // control has the ring, and a person who comes back has to say "enter" again (docs/30, docs/92).
  if (ui_set_field_bool(L, object, "editing", false)) {
    ui_invalidate(L, object);
    // **I deixa les tecles si les tenia** (0.181.0): the focus going away is one of the roads out of an edition, and it is the
    // one the dial takes —the ring is on another control and the two keys are the page's again— (docs/30, docs/90).
    ui_keys_follow_the_control(L, object);
  }
  // **The tab the dial leaves stops being the lit one**: the strip is one control and the paint needs to know
  // which of its tabs has the ring (0.99.0, docs/92).
  if (ui_set_field_number(L, object, "focus_index", 0)) ui_invalidate(L, object);
}

void ui_apply_focus(lua_State* L, const FocusTarget* targets, int count, int objects, int index) {
  const int old_index =
      ui_target_index(targets, count, g_ui_focus_slot, g_ui_focus_generation, g_ui_focus_item);
  if (old_index != index) {
    if (old_index >= 0) {
      lua_rawgeti(L, objects, old_index + 1);
      ui_take_the_focus_from(L, -1);
      lua_pop(L, 1);
    } else if (g_ui_focus_slot >= 0) {
      // **A ring on a control that is not a stop of this walk any more** (0.179.4): a page turned by a finger or by the dial
      // places the row that had it out of the frame, where `ui_target_index` cannot find it —and a row left saying it has the
      // ring would wear it again the next time its page came back, and its app would never hear `on_focus(self, false)`—. The
      // tree still holds the control, so it is found by the slot it had, which is the road a card already took to take a ring
      // away (0.173.1, docs/30, docs/90).
      const int base = lua_gettop(L);
      if (l_ui_root(L) == 1 && ui_push_slot_from(L, base + 1, g_ui_focus_slot, 0)) {
        LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(L, -1, kCanvasMeta));
        if (canvas != nullptr && canvas->generation == g_ui_focus_generation) ui_take_the_focus_from(L, -1);
      }
      lua_settop(L, base);
    }
  }
  if (index < 0 || index >= count) {
    g_ui_focus_slot = -1;
    g_ui_focus_generation = 0;
    // A control that takes the dial is a stop of its own unless it is a strip, which says its tab next.
    g_ui_focus_item = 0;
    g_ui_focus_item = 0;
    // **A dial that leaves everything takes the block with it**: the caret is the focus's and there is no focus at all
    // (0.116.0, docs/30).
    ui_keyboard_forget_target(L);
    ui_keyboard_hide();
    return;
  }
  g_ui_focus_slot = targets[index].slot;
  g_ui_focus_generation = targets[index].generation;
  g_ui_focus_item = targets[index].item;
  lua_rawgeti(L, objects, index + 1);
  // **The cursor of a grid goes where the ring landed, before the ring is painted** (0.179.4): a cell of another page is a
  // page the grid lays out again, so the number is written first and the paint of the ring is part of the page it owes
  // (docs/30, docs/90).
  ui_grid_follow_the_cell(L, -1);
  if (ui_set_field_bool(L, -1, "focused", true)) {
    // **And the ring of a grid arrives on one cell** (0.177.3), the same rule as the one that leaves: the cell the cursor
    // is on —given twice, because the cursor does not move— is the one rectangle, and a grid of entries damages nothing at
    // all because its cells paint their own ring (docs/30, docs/90).
    const lua_Integer cell = ui_field_number(L, -1, "index", 0);
    const uint8_t at = (cell >= 1) ? static_cast<uint8_t>(cell) : 0;
    if (!ui_invalidate_grid_ring(L, -1, at, at, false)) {
      ui_invalidate(L, -1);
    }
    ui_call_hook(L, -1, "on_focus", true, true);
  }
  if (targets[index].item != 0 &&
      ui_set_field_number(L, -1, "focus_index", targets[index].item)) {
    ui_invalidate(L, -1);
  }
  // **The block of a box is the focus's and not the keyboard's** (0.116.0): a box the dial has landed on has a caret in
  // it —a read-only document has a place in its text and no band— and the band is only what entering it brings. A
  // control that is not a box takes the block and the band away, because a band with nothing to type into is a band
  // over a page nobody is writing (docs/30, docs/90).
  if (ui_field_bool(L, -1, "__textbox", false) || ui_field_bool(L, -1, "__textbox_multiline", false)) {
    ui_keyboard_target_set(L, -1);
    // **A page nobody types into does not keep a band that was up for the box before** (0.116.0): the target is the new
    // box and the letters go with the box that takes them (docs/30, docs/92).
    if (ui_field_bool(L, -1, "readonly", false)) ui_keyboard_hide();
  } else {
    ui_keyboard_forget_target(L);
    ui_keyboard_hide();
  }
  lua_pop(L, 1);
}

// **The page answers the key on its own** (0.98.0). The dial moving and a control going down are the system's
// doing —the app never sees the key, `ui_button_event` takes it— so the paint that shows them is the system's
// too. The paper project paints the ring the moment the focus moves and the button the moment it is pressed
// (`feedback()`, `src/ui/button.cpp`, *"the pressed state is immediate feedback: it is what the user is waiting
// for"*), and a page that only repaints when its app says so is a page whose controls look dead: the owner
// pressed left and right on the board and the showcase did not answer, because the ring moved in the tree and
// nothing was ever put on the glass (docs/30, docs/50, docs/90).
//
// **The paint is the app's page and it is only the regions that changed**: a present composes the controls that
// owe a draw and sends their rectangles, and a page where nothing moved owes nothing — the call costs nothing and
// paints nothing — so a button going down costs its own box and not the glass (docs/30, docs/50).
// **The draw of the page and then the boxes it owes, each where it is and all cleaned** (0.168.1): a focus that moves, or a
// button that goes down and up, changes **two** controls —the ring that comes and the ring that goes—, and a `du` on them
// leaves the ghost of the ring on the glass because `du` paints and does not clean. The quality is `gl16`, which cleans
// without the flash of `gc16`, and **each region keeps its own box**: merging them would also paint the air between the two
// controls, which is the opposite of the smallest area a change is owed (docs/30, docs/50).
int l_ui_present_feedback(lua_State* L) {
  if (g_ui_ops.present_each == nullptr) return 0;
  if (l_ui_root(L) == 1) {
    const int height = lua_gettop(L) - 1;
    lua_getfield(L, -1, "draw");
    if (lua_isfunction(L, -1)) {
      lua_pushvalue(L, -2);
      if (lua_pcall(L, 1, 0, 0) != LUA_OK) return lua_error(L);
    }
    lua_settop(L, height);
  }
  ++g_ui_present_count;
  // **The system's own feedback is the two-level pass** (0.179.41): a ring that moves, a control that let go and a value
  // that was stepped are ink of two levels —a frame and a face— on a page that may be anything, and what they cost is
  // the one thing this pass can know. It was `gl16`: four hundred and fifty milliseconds and **a blink** of every region
  // that happened to be damaged, which on the editor's page was its whole document box on the first touch of the glass
  // (docs/30, docs/50). `a2` is two hundred and ninety and it erases what it writes over; a region the app painted with a
  // quality of its own keeps it, because `present_each` no longer writes over the app's wish (docs/30, docs/50).
  g_ui_ops.present_each(static_cast<uint8_t>(hal::UpdateMode::kA2));
  return 0;
}

// **The page drawn and not presented** (0.179.4): the rows of a list are placed by the draw of the list itself —the entries
// of the other pages sit out of its frame until it is asked for a page— so a page turned by a key or by a finger has to ask
// for that draw **before** the ring can be put on a row of the new page. The present is the one that comes after, when the
// ring is where a person is looking (docs/30, docs/90).
void ui_draw_the_page() {
  if (g_state == nullptr) return;
  const int top = lua_gettop(g_state);
  if (l_ui_root(g_state) == 1) {
    lua_getfield(g_state, -1, "draw");
    if (lua_isfunction(g_state, -1)) {
      lua_pushvalue(g_state, -2);
      if (lua_pcall(g_state, 1, 0, 0) != LUA_OK) {
        con_text("draw: ");
        const char* message = lua_tostring(g_state, -1);
        con_text((message != nullptr) ? message : "(no message)");
        con_text("\r\n");
      }
    }
  }
  lua_settop(g_state, top);
}

void ui_feedback() {
  if (g_state == nullptr || g_ui_ops.present == nullptr) return;
  // **The same way in as the app**: what changed is drawn first —the view asks its children, and a control with
  // nothing changed is not asked at all— and then the tree is composed and the glass asked for the paint. Asking
  // the glass without drawing first paints what was there before, which is exactly the nothing this call exists to
  // fix (found by its own test, 8/11).
  const int top = lua_gettop(g_state);
  // **Protected, like every other hook**: a draw of the app that throws must say so on the console and leave the
  // page as it was, and an error that long-jumps out of a key handler takes the app down with it.
  lua_pushcfunction(g_state, l_ui_present_feedback);
  if (lua_pcall(g_state, 0, 0, 0) != LUA_OK) {
    con_text("present: ");
    const char* message = lua_tostring(g_state, -1);
    con_text((message != nullptr) ? message : "(no message)");
    con_text("\r\n");
  }
  lua_settop(g_state, top);
}

// **`ui.focus(control)`: the app says where the dial is.** A page that is built again —the pages of a catalogue under
// a strip of tabs are given back and built at every tab— loses the control the dial was on, and the walk starts again
// at the first one of the tree, which is the strip: the arrows then change the tab for ever and nothing the page has
// can be pressed (found by the owner with the board in his hand, 7/11: *«no puc pitjar res»*). This is the call that
// puts the dial back where the app wants it, and it does nothing when the control cannot be landed on (docs/30,
// docs/90).
int l_ui_focus(lua_State* L) {
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(L, 1, kCanvasMeta));
  if (canvas == nullptr || !ui_control_alive(canvas)) return 0;
  FocusList list;
  FocusTarget* targets = list.items;
  const int top = lua_gettop(L);
  const int objects = top + 1;
  const int count = ui_collect_targets(L, targets, list.capacity);
  if (count <= 0) {
    lua_settop(L, top);
    return 0;
  }
  for (int i = 0; i < count; ++i) {
    if (targets[i].slot == canvas->slot) {
      ui_apply_focus(L, targets, count, objects, i);
      // **Una tira que rep el cercle el posa a la pestanya triada i no a la primera** (0.192.49): the walk gives a strip one
      // stop per tab, and the stop the walk lands on is the first one; una app que diu «el focus és d'aquest control» vol dir
      // la pestanya que té, i el vidre ha d'ensenyar la que la pàgina diu. El dial no passa per aquí —camina amb
      // `ui_apply_focus` i prou—, o sigui que el cercle de la tira encara es pot moure per triar-ne una altra (docs/30, docs/90).
      lua_rawgeti(L, objects, i + 1);
      if (ui_field_bool(L, -1, "__tabs", false)) {
        const lua_Integer chosen = ui_field_number(L, -1, "index", 0);
        if (chosen >= 1 && ui_set_field_number(L, -1, "focus_index", chosen)) ui_invalidate(L, -1);
      }
      lua_pop(L, 1);
      // The ring the app asked for is on the glass without waiting for the page to be presented again: this call
      // is made right after a page is built, before its first present, and a ring nobody sees is not an answer.
      ui_feedback();
      break;
    }
  }
  lua_settop(L, top);
  return 0;
}

bool ui_activate(lua_State* L, int count, int objects, int index, bool by_finger = false) {
  if (index < 0 || index >= count) return false;
  lua_rawgeti(L, objects, index + 1);
  const int object = lua_gettop(L);
  // The button is **painted down** before the app is told, and painted up when the callback returns: the two
  // moments are the two `feedback()` calls of the paper project, and without the first one a press on a button
  // that changes nothing of its own is a press nobody sees (docs/30, docs/90).
  if (ui_set_field_bool(L, object, "pressed", true)) {
    ui_invalidate(L, object);
    ui_feedback();
  }
  // **A checkbox's click means one thing and the system does it**: the box is checked or it is not, and the
  // press writes it before the app is told. An app that had to flip it in a callback would be an app that can
  // forget to, and a checkbox that does not mark the page is a checkbox that lied. The new state arrives at
  // `on_change(self, checked)`, and `on_click` and `on_activate` are not called for this control: the click
  // already means something (docs/30, docs/90).
  const bool checkbox = ui_field_bool(L, object, "__checkbox", false);
  // **A picker is a card too, and its centre is not the centre of a dialog**: it is `__dialog` so that the walk and the finger
  // are its own while it is up, but what the centre does there is pick the value the band of ink is on —and a picker read as a
  // two-button card asked the app for `on_accept`, which it does not have, and a tap on a value chose nothing at all (found on
  // the emulator, 0.189.102)— (docs/30, docs/90).
  const bool dialog = !ui_field_bool(L, object, "__picker", false) && ui_field_bool(L, object, "__dialog", false);
  if (checkbox) {
    const bool was = ui_field_bool(L, object, "checked", false);
    if (ui_set_field_bool(L, object, "checked", !was)) ui_invalidate(L, object);
    ui_call_hook(L, object, "on_change", true, !was);
  } else if (dialog) {
    // **The centre presses the door the dial is on**, which is the one thing a card does: the app hears which door
    // and closing the card is its own (docs/30, docs/90).
    const lua_Integer choice = ui_field_number(L, object, "choice", 2);
    ui_call_hook(L, object, (choice == 1) ? "on_cancel" : "on_accept", false, false);
  } else if (ui_field_bool(L, object, "__picker", false)) {
    // **El centre tria el valor on és el cercle, i una premuda a una clau del peu no tria res** (0.189.102): a page turn is not
    // a choice, and the release of the finger that pressed a key arrives here as an activation —`__stepped` is what the press
    // of the card wrote and what this spends (docs/30, docs/90).
    if (ui_field_bool(L, object, "__stepped", false)) {
      ui_set_field_bool(L, object, "__stepped", false);
    } else {
      ui_call_hook_number(L, object, "on_pick", ui_field_number(L, object, "index", 0));
    }
  } else if (ui_field_bool(L, object, "__month", false)) {
    // **The centre picks the day the dial is on**, and the app hears which one (docs/90).
    ui_call_hook_number(L, object, "on_pick", ui_field_number(L, object, "selected", 1));
  } else if (ui_field_bool(L, object, "__menu", false)) {
    // **The centre picks the option the dial is on** and the finger picks the one it pressed, and the app hears which
    // one; closing the menu is the app's, the same rule a card follows (docs/30, docs/90).
    ui_call_hook_number(L, object, "on_pick", ui_field_number(L, object, "index", 1));
  } else if (ui_field_bool(L, object, "__menu_bar", false)) {
    // **The centre picks the entry of the open menu** and the app hears which title and which entry of it: a number
    // alone would not say which menu the entry belongs to. **The system folds the menu after a choice**, because a menu
    // that stays open after one is a menu a person has to dismiss twice (docs/30, docs/90).
    const lua_Integer open = ui_field_number(L, object, "open", 0);
    const lua_Integer entry = ui_field_number(L, object, "index", 0);
    if (open > 0 && entry > 0) {
      // **Read the action, fold, then run it**: the app hears the choice with the menu already folded, which is the same
      // order a card of the system follows (docs/90).
      lua_getiuservalue(L, object, 1);
      if (lua_istable(L, -1)) {
        lua_pushinteger(L, 0);
        lua_setfield(L, -2, "open");
      }
      lua_pop(L, 1);
      // **I el rectangle del desplegable torna a paper** (0.192.39): a pick folds the menu, and the bar has no buffer of its own —
      // the card it painted went straight into the page—, o sigui que sense això el menú triat es quedava pintat sobre la pàgina
      // amb les files de sota a trossos. És el mateix forat que fa una targeta que marxa (docs/30, docs/90).
      LuaCanvas* bar = static_cast<LuaCanvas*>(luaL_testudata(L, object, kCanvasMeta));
      if (bar != nullptr && g_ui_ops.menu_bar_hole != nullptr) g_ui_ops.menu_bar_hole(bar->slot);
      ui_invalidate(L, object);
      ui_call_hook_pair(L, object, "on_pick", open, entry);
    }
  } else if (ui_field_bool(L, object, "__selector", false)) {
    // **The door of a selector is `on_open`**: the field is the question and the app puts the list up, because the system
    // cannot hold a table it does not own. `choosing` is already true when the app hears it, so the column of the triangle
    // is inverted; **turning it off is the app's**, when its list goes away (docs/30, docs/90).
    lua_getiuservalue(L, object, 1);
    if (lua_istable(L, -1)) {
      lua_pushboolean(L, 1);
      lua_setfield(L, -2, "choosing");
    }
    lua_pop(L, 1);
    ui_invalidate(L, object);
    ui_call_hook(L, object, "on_open", false, false);
  } else if (ui_field_bool(L, object, "__notifications", false)) {
    // **The centre dismisses the newest notice**, because a notice that a board without a touch cannot take away is a notice
    // that stays forever; and a finger on a card body opens the card it touched, which is the same hook (docs/90).
    const lua_Integer total = static_cast<lua_Integer>(ui_notification_total(L, object));
    lua_Integer which = ui_field_number(L, object, "index", 0);
    if (which < 1 || which > total) which = total;
    if (which > 0) ui_call_hook_number(L, object, "on_pick", which);
  } else if (ui_field_bool(L, object, "__icon_grid", false)) {
    // **The centre picks the cell the dial is on**, and the app hears which one; the page is the control's business, so the app
    // only has to keep the number (docs/30, docs/90).
    const lua_Integer which = ui_field_number(L, object, "index", 0);
    if (which > 0) ui_call_hook_number(L, object, "on_pick", which);
  } else if (ui_field_bool(L, object, "__tabs", false)) {
    // **The centre chooses the tab the dial is on** (0.99.0). The strip has one stop per tab, so the click belongs
    // to a tab and not to the strip: `index` becomes the tab of the ring, the app hears `on_change(self, index)`,
    // and the page below is the app's to build. It is the paper project's `TabStrip::onKey`, which gives the key to
    // the tab that has the focus and to nobody else (docs/30, docs/92).
    const lua_Integer which = ui_field_number(L, object, "focus_index", 0);
    if (which > 0) {
      if (ui_set_field_number(L, object, "index", which)) ui_invalidate(L, object);
      ui_call_hook_number(L, object, "on_change", which);
    }
  } else if (ui_field_bool(L, object, "__slider", false)) {
    // **Un dit al tub posa el número on ha caigut** (0.192.34, l'amo: *«L'slider de l'app banner es un control UI? … I el més
    // important… ha de funcionar xD»*). A slider was only moved by the two keys of the dial, and a board that has a glass and one
    // key has no dial at all: the control was there and nothing could move it. A finger that lands on the track asks the kernel
    // for the number under it —`slider_value_at`, the same geometry the knob is drawn with— and the app hears `on_change` with
    // the field already written, which is the rule every value control follows. **No entra en `editing`**: entering is what gives
    // the two keys to the number, and a finger has already said which number it wants (docs/30, docs/90, docs/92).
    if (by_finger && g_ui_ops.slider_value_at != nullptr) {
      LuaCanvas* knob = static_cast<LuaCanvas*>(luaL_testudata(L, object, kCanvasMeta));
      if (knob != nullptr) {
        lua_getfield(L, object, "suffix");
        const char* suffix = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
        const lua_Integer scale = ui_field_number(L, object, "scale", 4);
        const lua_Integer minimum = ui_field_number(L, object, "min", 0);
        const lua_Integer maximum = ui_field_number(L, object, "max", 100);
        const lua_Integer step = ui_field_number(L, object, "step", 1);
        const lua_Integer value = ui_field_number(L, object, "value", 0);
        const uint8_t scale8 = static_cast<uint8_t>((scale < 1) ? 1 : ((scale > 16) ? 16 : scale));
        const int16_t asked =
            g_ui_ops.slider_value_at(knob->slot, g_ui_touch_x0, suffix, scale8, static_cast<int16_t>(minimum),
                                     static_cast<int16_t>(maximum), static_cast<int16_t>(step));
        lua_pop(L, 1);
        if (asked != value) {
          lua_getiuservalue(L, object, 1);
          lua_pushinteger(L, asked);
          lua_setfield(L, -2, "value");
          lua_pop(L, 1);
          ui_invalidate(L, object);
          ui_call_hook_number(L, object, "on_change", asked);
        }
        // **The press of the ring is not a button's**: a slider that a finger moved is not painted down and up, and the
        // object leaves the stack the way every branch above leaves it (docs/90).
        lua_pop(L, 1);
        return true;
      }
    }
    // **The centre enters a slider and leaves it** (0.117.0): while it is inside, the two keys of the board are the
    // number and not the walk; the centre gives them back. The app hears the press both times, so a slider with an
    // `on_click` keeps answering, and nothing else changes state: no band, no target, no paint of its own (docs/30,
    // docs/92).
    const bool editing = ui_field_bool(L, object, "editing", false);
    if (ui_set_field_bool(L, object, "editing", !editing)) ui_invalidate(L, object);
    // **L'slider entrat es queda les dues tecles** (0.181.0): inside it the two keys are the number and not the walk, and it is a
    // place of the queue and not a branch of the dial, so a page that is built again under the finger keeps them (docs/30, docs/92).
    ui_keys_follow_the_control(L, object);
    ui_call_hook(L, object, "on_click", false, false);
  } else if (ui_field_bool(L, object, "__textbox", false) ||
             ui_field_bool(L, object, "__textbox_multiline", false)) {
    // **The centre enters a box of text and leaves it** (0.116.0): a field is not a button, and once a person is inside
    // it the two keys of the board are the caret —the shape a slider has, where the keys move the value of the control
    // the ring is on—. Entering brings the band when the box takes keys and only the block when it does not, which is
    // what a read-only document is; leaving takes the band and leaves the block, because the box still has the focus
    // (docs/30, docs/90, docs/92).
    const bool editing = ui_field_bool(L, object, "editing", false);
    bool wants = false;
    uint8_t kind = 0;
    ui_box_keyboard(L, object, wants, kind);
    // **A finger on the box that is being typed into is a caret and nothing else** (0.179.61): it does not leave the
    // field, it does not take the band down and it does not move the focus —only the centre and a tap outside the box do
    // that—, which is what a person is doing when they tap the middle of the line they are writing and the caret has to
    // go where the finger asked. The caret went there on the way up, and that is the whole of the change (docs/30, docs/90).
    const bool stays = editing && by_finger;
    if (!stays && ui_set_field_bool(L, object, "editing", !editing)) {
      ui_invalidate(L, object);
      // **La caixa que entra o surt es pinta de seguida, i no amb la passada ràpida** (0.184.0): the cross that empties the box
      // is drawn while the box is not being written into and it is not drawn while it is, so entering the field **erases ink**
      // —and a `du` paints and does not clean: the emulator showed the cross at level ten of sixteen left behind, which is a
      // command a person sees and cannot press—. `ui_feedback` is the present of the system and it goes with `a2`, which lands
      // two sixteenths off the level: the cross is gone and nothing blinks (docs/30, docs/50, docs/92).
      ui_feedback();
    }
    if (stays) {
      // **Un dit dins la caixa que s'escriu torna a ser el destí de la banda i el bloc** (0.179.73): the tap focuses the
      // control first (`ui_apply_focus`) and a focus that changes **stops the block** of the one it leaves, so a second tap
      // inside the same field left it entered, with the band up and **no cursor at all** —the field looked like it had lost
      // the focus, and the pointer of the keyboard had been written off behind the scenes—. Nothing else changes: the band
      // stays, the field stays entered and the caret stays where the finger put it.
      ui_keyboard_target_set(L, object);
    } else if (editing) {
      ui_keyboard_hide();
    } else {
      // The target is the box in both cases: the block is the focus's, and the block is the point of entering a page
      // nobody types into (docs/30).
      ui_keyboard_target_set(L, object);
      if (wants && g_ui_ops.keyboard_show != nullptr) {
        g_keyboard_kind = kind;
        g_ui_ops.keyboard_show(kind);
      }
    }
    // **La caixa que s'ha entrat es queda les tecles, i la que s'ha deixat les dona** (0.181.0): one line for the three roads
    // because the state of the control is the truth —`editing` says whether a person is inside it— and the queue follows it, the
    // same line the slider and the block of the band use (docs/30, docs/90).
    ui_keys_follow_the_control(L, object);
    ui_call_hook(L, object, "on_activate", false, false);
  } else if (!ui_call_hook(L, object, "on_click", false, false)) {
    ui_call_hook(L, object, "on_activate", false, false);
  }
  // The system owns the held state: a callback that returns leaves the button up. A callback that
  // released the control has no frame left to paint, and the object says so by its slot.
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(L, object, kCanvasMeta));
  if (ui_control_alive(canvas)) {
    if (ui_set_field_bool(L, object, "pressed", false)) {
      ui_invalidate(L, object);
      ui_feedback();
    }
  }
  lua_pop(L, 1);
  return true;
}

// **El control entrat té les tecles, i les tres menes de control passen per aquí**: a box of one line and a box of several
// take the two keys as the caret, a slider takes them as its value, and the centre is what enters and leaves the control —so
// an exclusive place must still answer the centre, or the only way out of an edition would be gone—. A key that does not move
// anything is **consumed all the same**: a caret at the first letter has nowhere to go back to, and it used to fall through to
// the walk and take the focus away from the field a person was writing in (docs/30, docs/90, docs/92).
// **El control entrat té les tecles, i les tres menes de control passen per aquí**: a box of one line and a box of several
// take the two keys as the caret, a slider takes them as its value, and the centre is what enters and leaves the control —so
// an exclusive place must still answer the centre, or the only way out of an edition would be gone—. A key that does not move
// anything is **consumed all the same**: a caret at the first letter has nowhere to go back to, and it used to fall through to
// the walk and take the focus away from the field a person was writing in (docs/30, docs/90, docs/92).
void ui_keys_listener(const char* kind, uint32_t key, int64_t held_ms, void* owner) {
  (void)held_ms;
  (void)owner;
  if (g_state == nullptr || kind == nullptr || strcmp(kind, "press") != 0) return;
  int direction = 0;
  bool centre = false;
  KeyInfo info = {};
  if (g_button_ops.info != nullptr && g_button_ops.info(key, info) && info.spot != nullptr) {
    // **Amunt és la lletra següent i avall la d'abans** (0.182.1): the owner: *«amb el mapa al revés, per mi avall és esquerra, amunt
    // dreta»*. It is the opposite of the dial, which walks the page with `up` as the control before it, and it is on purpose: inside a
    // box the two keys are the line a person is writing and not the walk (docs/30, docs/90).
    if (strcmp(info.spot, "up") == 0 || strcmp(info.spot, "right") == 0) {
      direction = 1;
    } else if (strcmp(info.spot, "down") == 0 || strcmp(info.spot, "left") == 0) {
      direction = -1;
    } else if (strcmp(info.spot, "centre") == 0 || strcmp(info.spot, "center") == 0) {
      centre = true;
    }
  }
  if (direction == 0 && !centre) return;
  const int top = lua_gettop(g_state);
  const int objects = top + 1;
  FocusList list;
  const int count = ui_collect_targets(g_state, list.items, list.capacity);
  int which = -1;
  for (int i = 0; i < count; ++i) {
    if (list.items[i].slot != g_keys_slot || list.items[i].generation != g_keys_generation) continue;
    which = i;
    break;
  }
  if (which < 0) {
    // **El control ja no és a la pàgina**: the place goes back and the keys with it (docs/30).
    ui_release_the_keys(g_state, 0);
    lua_settop(g_state, top);
    return;
  }
  if (centre) {
    // **El centre és del control**, not of the dial: it is what enters a box and leaves it, and what leaves a slider (docs/30).
    ui_activate(g_state, count, objects, which);
  } else {
    lua_rawgeti(g_state, objects, which + 1);
    const bool box = ui_field_bool(g_state, -1, "__textbox", false) || ui_field_bool(g_state, -1, "__textbox_multiline", false);
    lua_pop(g_state, 1);
    if (box) {
      // **La lletra i no la fila** (0.182.1), la mateixa caixa d'una línia que la de diverses (docs/30, docs/90).
      ui_step_caret(g_state, objects, which, direction, /*letters_only=*/true);
    } else {
      ui_step_value(g_state, objects, which, direction);
    }
  }
  // **I la plaça es queda mentre el control digui que està entrat**: the centre may have left it, and a page built again by the
  // band has already taken the old object with it (docs/30, docs/90).
  lua_rawgeti(g_state, objects, which + 1);
  const bool still = ui_field_bool(g_state, -1, "editing", false);
  lua_pop(g_state, 1);
  lua_settop(g_state, top);
  if (!still) ui_release_the_keys(g_state, 0);
}

int ui_hit(const FocusTarget* targets, int count, int x, int y) {
  int best = -1;
  for (int i = 0; i < count; ++i) {
    if (x < targets[i].x || y < targets[i].y || x >= targets[i].x + targets[i].w ||
        y >= targets[i].y + targets[i].h) {
      continue;
    }
    if (best < 0 || targets[i].depth > targets[best].depth ||
        (targets[i].depth == targets[best].depth && targets[i].order > targets[best].order)) {
      best = i;
    }
  }
  return best;
}

int l_button_wait(lua_State* L) {
  lua_Integer timeout = luaL_optinteger(L, 1, kButtonWaitDefaultMs);
  if (timeout <= 0) {
    return push_nil_error(L, "E_INVAL", "the wait needs a time above zero");
  }
  if (g_button_ops.wait == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no button");
  }
  if (!g_button_ops.wait(static_cast<uint32_t>(timeout))) {
    return push_nil_error(L, "E_BUSY", "a press is already on its way");
  }

  const int p = push_promise(L, 0);
  pending_add(L, p);
  lua_pushvalue(L, p);
  lua_setfield(L, LUA_REGISTRYINDEX, kButtonKey);
  lua_pushvalue(L, p);
  return 1;
}

// ---------------------------------------------------------------------------
// power: what the board can do about energy, and the sleep that ends the app
// ---------------------------------------------------------------------------

// power.caps(): `sleep_light`, `sleep_deep`, `battery`, `charging` and `wake`. A
// bit is only true when the board really has it, and `wake` says where it comes
// back from so an app does not promise a person an alarm on a board that cannot
// wake (docs/30).
// **`sys.idle_ms()`** (0.190.9, l'amo: *«el sistema exposa aquest valor a les apps lua que ho vulguin saber. es el comptador d'idle
// del usuari»*): how many milliseconds since a person last did anything on the board. It is **the same counter** the deep sleep
// and the lock of the screen read —one counter and not three—, and the three things that move it are a byte of console, a finger
// on the glass and a key of the dial. Zero is a boot that has just stamped itself (docs/30, docs/60).
int l_sys_idle_ms(lua_State* L) {
  if (g_power_ops.idle_ms == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build keeps no counter of the hand");
  }
  lua_pushinteger(L, static_cast<lua_Integer>(g_power_ops.idle_ms()));
  return 1;
}

int l_power_caps(lua_State* L) {
  lua_createtable(L, 0, 5);
  lua_pushboolean(L, (g_power_ops.can_sleep != nullptr && g_power_ops.can_sleep(0)) ? 1 : 0);
  lua_setfield(L, -2, "sleep_light");
  lua_pushboolean(L, (g_power_ops.can_sleep != nullptr && g_power_ops.can_sleep(1)) ? 1 : 0);
  lua_setfield(L, -2, "sleep_deep");
  int mv = 0;
  int pct = 0;
  bool charging = false;
  const bool gauge =
      (g_power_ops.battery != nullptr) && g_power_ops.battery(mv, pct, charging);
  lua_pushboolean(L, gauge ? 1 : 0);
  lua_setfield(L, -2, "battery");
  lua_pushboolean(L, (gauge && charging) ? 1 : 0);
  lua_setfield(L, -2, "charging");
  lua_pushstring(L, (g_power_ops.wakes != nullptr) ? g_power_ops.wakes() : "none");
  lua_setfield(L, -2, "wake");
  return 1;
}

// power.battery(): {mv, pct, charging}, or nil when the board cannot tell. Nil and
// zero volts are different answers (docs/75).
int l_power_battery(lua_State* L) {
  int mv = 0;
  int pct = 0;
  bool charging = false;
  if (g_power_ops.battery == nullptr || !g_power_ops.battery(mv, pct, charging)) {
    lua_pushnil(L);
    return 1;
  }
  lua_createtable(L, 0, 6);
  lua_pushinteger(L, static_cast<lua_Integer>(mv));
  lua_setfield(L, -2, "mv");
  lua_pushinteger(L, static_cast<lua_Integer>(pct));
  lua_setfield(L, -2, "pct");
  lua_pushboolean(L, charging ? 1 : 0);
  lua_setfield(L, -2, "charging");
  // **I el que el comptador de coulombs sap de més** (0.190.155, l'amo: *«Mira els exemples de batería (es el hardware que tenim?)
  // ... Si ens serveixen, afegeix la informació a l'app battery»*): `ma` és el corrent que entra o surt de la cel·la —negatiu
  // mentre alimenta la placa— i `mah`/`mah_full` són la càrrega que queda i la que cap. **El camp que el xip no diu no hi és**,
  // en lloc de ser un zero, perquè un zero és una mesura i una absència no (docs/30, docs/50).
  hal::IPower::BatteryDetail detail;
  if (g_power_ops.battery_detail != nullptr && g_power_ops.battery_detail(detail)) {
    if (detail.current_ma != hal::IPower::kUnknown) {
      lua_pushinteger(L, static_cast<lua_Integer>(detail.current_ma));
      lua_setfield(L, -2, "ma");
    }
    if (detail.left_mah != hal::IPower::kUnknown && detail.full_mah != hal::IPower::kUnknown) {
      lua_pushinteger(L, static_cast<lua_Integer>(detail.left_mah));
      lua_setfield(L, -2, "mah");
      lua_pushinteger(L, static_cast<lua_Integer>(detail.full_mah));
      lua_setfield(L, -2, "mah_full");
    }
    // **Com és la cel·la i no com està** (0.190.156): the temperature its own sensor reads, the health the gauge trusts, the
    // cycles it has counted and the capacity it was sold with. La temperatura va en dècimes de grau perquè no es perdi res pel
    // camí; l'app la divideix (docs/30, docs/50).
    if (detail.temp_c10 != hal::IPower::kUnknown) {
      lua_pushinteger(L, static_cast<lua_Integer>(detail.temp_c10));
      lua_setfield(L, -2, "temp_c10");
    }
    if (detail.health != hal::IPower::kUnknown) {
      lua_pushinteger(L, static_cast<lua_Integer>(detail.health));
      lua_setfield(L, -2, "health");
    }
    if (detail.cycles != hal::IPower::kUnknown) {
      lua_pushinteger(L, static_cast<lua_Integer>(detail.cycles));
      lua_setfield(L, -2, "cycles");
    }
    if (detail.design_mah != hal::IPower::kUnknown) {
      lua_pushinteger(L, static_cast<lua_Integer>(detail.design_mah));
      lua_setfield(L, -2, "mah_design");
    }
  }
  return 1;
}

// power.charger(): **l'altre xip de la bateria** (0.190.156, l'amo: *«Tenim el PMU molt abandonat ... Busca quin model tenim al
// T5S3 Pro i implementa'l»*). The cable, what kind of supply it is, how much of it is going into the cell and what the rails are
// doing. `nil` on a board without one; every field the part does not answer is absent from the table and never a zero, que és la
// diferència entre «no carrega» i «no ho sé» (docs/30, docs/50, docs/60).
int l_power_charger(lua_State* L) {
  hal::IPower::ChargerDetail detail;
  if (g_power_ops.charger_detail == nullptr || !g_power_ops.charger_detail(detail)) {
    lua_pushnil(L);
    return 1;
  }
  lua_createtable(L, 0, 7);
  if (detail.vbus_mv != hal::IPower::kUnknown) {
    lua_pushinteger(L, static_cast<lua_Integer>(detail.vbus_mv));
    lua_setfield(L, -2, "vbus_mv");
  }
  if (detail.ichgr_ma != hal::IPower::kUnknown) {
    lua_pushinteger(L, static_cast<lua_Integer>(detail.ichgr_ma));
    lua_setfield(L, -2, "ichgr_ma");
  }
  if (detail.vsys_mv != hal::IPower::kUnknown) {
    lua_pushinteger(L, static_cast<lua_Integer>(detail.vsys_mv));
    lua_setfield(L, -2, "vsys_mv");
  }
  if (detail.ntc_pct != hal::IPower::kUnknown) {
    lua_pushinteger(L, static_cast<lua_Integer>(detail.ntc_pct));
    lua_setfield(L, -2, "ntc_pct");
  }
  if (detail.input != nullptr && detail.input[0] != '\0') {
    lua_pushstring(L, detail.input);
    lua_setfield(L, -2, "input");
  }
  if (detail.charge != nullptr && detail.charge[0] != '\0') {
    lua_pushstring(L, detail.charge);
    lua_setfield(L, -2, "charge");
  }
  lua_pushboolean(L, detail.power_good ? 1 : 0);
  lua_setfield(L, -2, "good");
  return 1;
}

// power.cpu([mhz]): the speed of the processor, in MHz. Without arguments it says
// where it is; with one it moves, and the answer is the speed the board is really
// running at — a step the board does not have is refused and not rounded, because
// a rounded speed is a lie about what it costs (docs/30, docs/50).
int l_power_cpu(lua_State* L) {
  if (g_power_ops.cpu_mhz == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board does not say how fast it runs");
  }
  if (lua_gettop(L) == 0 || lua_isnil(L, 1)) {
    lua_pushinteger(L, static_cast<lua_Integer>(g_power_ops.cpu_mhz()));
    return 1;
  }
  const lua_Integer asked = luaL_checkinteger(L, 1);
  if (asked < 80 || asked > 240) {
    return push_nil_error(L, "E_INVAL", "the processor goes from 80 to 240 MHz");
  }
  if (g_power_ops.set_cpu_mhz == nullptr ||
      !g_power_ops.set_cpu_mhz(static_cast<uint32_t>(asked))) {
    return push_nil_error(L, "E_INVAL", "this board does not run at that speed: 80, 160 or 240");
  }
  lua_pushinteger(L, static_cast<lua_Integer>(g_power_ops.cpu_mhz()));
  return 1;
}

// **`power.cpu_auto([on])`: la política automàtica de la velocitat** (0.189.107): whether the load decides the step of the
// processor. `power.cpu(<mhz>)` fixes one and stops the policy —a person who fixed a step did it to measure something—, and
// this is the door that says where the switch is and turns it back on, which is the pair the prompt has with `power auto`
// (docs/30, docs/50).
int l_power_cpu_auto(lua_State* L) {
  if (g_power_ops.cpu_auto == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board does not decide its own speed");
  }
  if (lua_isnoneornil(L, 1)) {
    lua_pushboolean(L, g_power_ops.cpu_auto() ? 1 : 0);
    return 1;
  }
  if (g_power_ops.set_cpu_auto == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board does not decide its own speed");
  }
  g_power_ops.set_cpu_auto(lua_toboolean(L, 1) != 0);
  lua_pushboolean(L, g_power_ops.cpu_auto() ? 1 : 0);
  return 1;
}

// **`power.load()`: com va la màquina** (0.192.48, l'amo: *«A l'app d'Stats: afegir una pestanya nova: "Load", i anar monitoritzant la
// cárrega del sistema (comanda load) fent una gráfica»*): the share of the last window —a quarter of a second, or the time since
// somebody last asked if that is longer— that the kernel spent working and not waiting, **in tenths of a point** so that a chart of
// a hundred lines does not lose the decimal, and the word of the speed policy beside it. Two values and not one because they are two readers: a graph wants the number and a person wants `normal`
// (docs/30, docs/60). The window belongs to the kernel —the same one `load` at the prompt prints— i una app no en pot inventar cap.
int l_power_load(lua_State* L) {
  if (g_power_ops.load == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build does not measure its own load");
  }
  const char* word = nullptr;
  const uint32_t pct10 = g_power_ops.load(&word);
  lua_pushinteger(L, static_cast<lua_Integer>(pct10));
  if (word != nullptr) {
    lua_pushstring(L, word);
  } else {
    lua_pushnil(L);
  }
  return 2;
}

// power.keep_awake(b): the app says it is doing something that must not be
// interrupted by a sleep the system decides on its own (docs/30).
int l_power_keep_awake(lua_State* L) {
  if (g_power_ops.keep_awake == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board cannot sleep");
  }
  const bool on = lua_toboolean(L, 1) != 0;
  g_power_ops.keep_awake(on);
  lua_pushboolean(L, on ? 1 : 0);
  return 1;
}

// power.sleep({mode, in_ms | until}): a promise that is kept when the board is
// back. It is the one call that waits for real: while the chip is asleep there is
// no other turn to take (docs/30). A **deep** sleep does not come back; the app is
// closed before it goes and comes back with `app.args.restore` (docs/90). The
// millisecond key is `in_ms` and not `in`, because `in` is a reserved word in Lua
// and a table built with it does not even parse (measured, 31/10).
int l_power_sleep(lua_State* L) {
  char mode_name[8] = "light";
  lua_Integer in_ms = 0;
  lua_Integer until_s = 0;
  if (lua_istable(L, 1)) {
    lua_getfield(L, 1, "mode");
    if (lua_isstring(L, -1)) snprintf(mode_name, sizeof(mode_name), "%s", lua_tostring(L, -1));
    lua_pop(L, 1);
    lua_getfield(L, 1, "in_ms");
    if (lua_isinteger(L, -1)) in_ms = lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 1, "until");
    if (lua_isinteger(L, -1)) until_s = lua_tointeger(L, -1);
    lua_pop(L, 1);
  }

  uint8_t mode = 0;
  if (strcmp(mode_name, "deep") == 0) {
    mode = 1;
  } else if (strcmp(mode_name, "light") != 0) {
    return push_nil_error(L, "E_INVAL", "the sleep is light or deep");
  }
  if (g_power_ops.sleep == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board cannot sleep");
  }

  // The hour is what brings the board back: without one there is nothing to arm,
  // and a sleep with no deadline waits for a finger on the button (docs/60).
  hal::IClock* clock = hal::platform().clock();
  const int64_t now = (clock != nullptr) ? clock->epoch_ms() : 0;
  if (now <= 0) {
    return push_nil_error(L, "E_INVAL", "the board does not know the hour to come back at");
  }
  int64_t at = 0;
  if (until_s > 0) {
    at = static_cast<int64_t>(until_s) * 1000;
  } else if (in_ms > 0) {
    at = now + static_cast<int64_t>(in_ms);
  } else {
    return push_nil_error(L, "E_INVAL", "the sleep needs `in_ms` (ms) or `until` (epoch seconds)");
  }
  if (at <= now) {
    return push_nil_error(L, "E_INVAL", "that hour is already past");
  }

  const int p = push_promise(L, 0);
  if (!g_power_ops.sleep(mode, at)) {
    push_error_table(L, "E_BUSY", "this board cannot sleep like that, or a sleep is already on its way");
    promise_settle(L, p, kErr, lua_gettop(L));
    lua_pop(L, 1);
    lua_pushvalue(L, p);
    return 1;
  }
  // The promise is kept by the kernel between turns, when the board is back: the
  // sleep itself happens there and not inside this call (docs/30).
  pending_add(L, p);
  lua_pushvalue(L, p);
  lua_setfield(L, LUA_REGISTRYINDEX, kPowerKey);
  lua_pushvalue(L, p);
  return 1;
}

// **The seed `math.random` starts with, which is the band's and not the C library's** (0.174.8). Lua seeds its own
// generator with `setrandfunc` of `lmathlib.c`: `time(NULL)` and the address of the state. On this board `time(NULL)` is
// zero until the hour is set and the address of the state is the same on every boot —the heap is the heap— so **every
// boot was the same sequence of numbers**, which is a page that shuffles itself the same way for ever and a game whose
// first move is always the one of the run before (found reading docs/20, 10/11, docs/30, docs/50). The generator is
// stepped once here with two words of the band —the hardware RNG of the chip, mixed with the noise the ADC gives while
// the radio is off (docs/50)— and an app that wants to reproduce a run still calls `math.randomseed(n)` itself, which is
// the same door (docs/20).
void seed_the_numbers(lua_State* L) {
  const int base = lua_gettop(L);
  lua_getglobal(L, "math");
  if (!lua_istable(L, -1)) {
    lua_settop(L, base);
    return;
  }
  lua_getfield(L, -1, "randomseed");
  if (!lua_isfunction(L, -1)) {
    lua_settop(L, base);
    return;
  }
  const uint32_t first = pal::system().random();
  const uint32_t second = pal::system().random();
  lua_pushinteger(L, static_cast<lua_Integer>(first));
  lua_pushinteger(L, static_cast<lua_Integer>(second));
  // **Protected**, because a library that is not what it says must not take the boot with it: `randomseed` returns the two
  // seeds it used and nothing here reads them.
  if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
    const char* why = lua_tostring(L, -1);
    con_text("luabind: math.randomseed refused the seed: ");
    con_text((why != nullptr) ? why : "?");
    con_text("\r\n");
  }
  lua_settop(L, base);
}

void open_libraries(lua_State* L) {
  luaL_requiref(L, "_G", luaopen_base, 1);
  luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
  luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
  luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
  luaL_requiref(L, LUA_COLIBNAME, luaopen_coroutine, 1);
  luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
  lua_pop(L, 6);
  seed_the_numbers(L);
}

// **A book is a page, and a page is a glass** (0.179.9): the reader of the system —the book, its chapters and the pages
// of them laid out in a canvas of the app— asks for faces and for canvases on every line, so it goes with the page and not
// with the rest of the system. A board with no screen carries neither this code nor `epub`, `zip` and `inflate`
// (docs/46, docs/50, docs/75).
// ---------------------------------------------------------------------------
// epub: a book of the disk, a chapter of it, and a page of the chapter (docs/46 A5)
// ---------------------------------------------------------------------------
//
// **One book at a time**, which is the system's rule and not a limitation of the door: a chapter of a book is half
// a megabyte of the extra memory and there is one reader. The module IS the book —`open`, `chapter`, `page`,
// `close`— and every call says so in the words of a person when there is none.
//
// **A page is composed into a canvas the app already has** (docs/46, D2): it costs no surface, no `free()` and no
// pixels of the system's, and what comes back is where the next page starts —a byte— and what the page took.

double epub_opt_number(lua_State* L, int index, const char* field, double fallback) {
  if (!lua_istable(L, index)) return fallback;
  lua_getfield(L, index, field);
  // **`true` is one and `false` is zero** (0.137.0): the fields of `epub.page` that are switches —`measure`, and `dark`
  // since 0.137.0— are written as a boolean by anybody who means it, and a switch that only answers to a number is a
  // switch that silently does nothing (docs/30, docs/46 C9).
  double value = fallback;
  if (lua_isnumber(L, -1)) {
    value = static_cast<double>(lua_tonumber(L, -1));
  } else if (lua_isboolean(L, -1)) {
    value = lua_toboolean(L, -1) ? 1.0 : 0.0;
  }
  lua_pop(L, 1);

  return value;
}

// **The face an app asks for is the object `ui.load_font` gave it** and not a number (docs/30): the field is read
// as a font and its handle is what the kernel wants, or the face of the system when the field is nil.
int8_t epub_opt_font(lua_State* L, int index, const char* field, int8_t fallback) {
  if (!lua_istable(L, index)) return fallback;

  lua_getfield(L, index, field);
  int8_t handle = fallback;
  if (!lua_isnoneornil(L, -1)) {
    const int8_t read = font_handle_of(L, -1);
    if (read >= 0) handle = read;
  }
  lua_pop(L, 1);

  return handle;
}

int l_epub_open(lua_State* L) {
  if (g_book_ops.open == nullptr) return push_nil_error(L, "E_STATE", "this build carries no reader");
  const char* path = luaL_checkstring(L, 1);
  const char* why = g_book_ops.open(path);
  if (why != nullptr) return push_nil_error(L, "E_INVAL", why);

  lua_pushboolean(L, 1);
  return 1;
}

int l_epub_close(lua_State* L) {
  if (g_book_ops.close != nullptr) g_book_ops.close();
  (void)L;
  return 0;
}

// epub.books(): the `.epub` of the flash and the card, as paths, which is what the shelf of a reader is.
int l_epub_books(lua_State* L) {
  if (g_book_ops.books == nullptr || g_book_ops.book_at == nullptr) {
    return push_nil_error(L, "E_STATE", "this build carries no reader");
  }

  const int32_t count = g_book_ops.books();
  lua_createtable(L, count, 0);
  for (int32_t i = 0; i < count; ++i) {
    const char* path = g_book_ops.book_at(i);
    if (path == nullptr) continue;
    lua_pushstring(L, path);
    lua_rawseti(L, -2, i + 1);
  }

  return 1;
}

int l_epub_entries(lua_State* L) {
  if (g_book_ops.entries == nullptr) return push_nil_error(L, "E_STATE", "this build carries no reader");
  lua_pushinteger(L, g_book_ops.entries());
  return 1;
}

// The name of the entry at that index, counting from one as Lua does.
int l_epub_name(lua_State* L) {
  if (g_book_ops.name == nullptr) return push_nil_error(L, "E_STATE", "this build carries no reader");
  const int32_t index = static_cast<int32_t>(luaL_checkinteger(L, 1));
  const char* name = g_book_ops.name(index - 1);
  if (name == nullptr) return push_nil_error(L, "E_INVAL", "this book has no such entry");

  lua_pushstring(L, name);
  return 1;
}

int l_epub_first_chapter(lua_State* L) {
  if (g_book_ops.first_chapter == nullptr) return push_nil_error(L, "E_STATE", "this build carries no reader");
  const char* name = g_book_ops.name(g_book_ops.first_chapter());
  if (name == nullptr) return push_nil_error(L, "E_INVAL", "this book carries no chapter");

  lua_pushstring(L, name);
  return 1;
}

// The chapter is inflated and kept by the system; what comes back is how many bytes it has, because an app does
// not have to hold a chapter to read one.
int l_epub_chapter(lua_State* L) {
  if (g_book_ops.chapter == nullptr) return push_nil_error(L, "E_STATE", "this build carries no reader");
  const char* name = luaL_checkstring(L, 1);
  const int32_t bytes = g_book_ops.chapter(name);
  if (bytes < 0) return push_nil_error(L, "E_INVAL", "this book carries no such chapter");

  lua_pushinteger(L, bytes);
  return 1;
}

// **A small file of the book, whole** (0.142.0, docs/46 B6): `META-INF/container.xml`, the OPF, the index and a
// stylesheet are kilobytes and an app reads them to know the title of the book, the order of its spine and its cover.
// The door copies the bytes into a Lua string **once**, so the block of the book is free for the next read (docs/30).
int l_epub_read(lua_State* L) {
  if (g_book_ops.read == nullptr) return push_nil_error(L, "E_STATE", "this build carries no reader");
  const char* name = luaL_checkstring(L, 1);
  uint32_t size = 0;
  const char* data = g_book_ops.read(name, size);
  if (data == nullptr) {
    return push_nil_error(L, "E_INVAL",
                          "the book carries no such file, or it is bigger than the table of the book");
  }

  lua_pushlstring(L, data, static_cast<size_t>(size));
  return 1;
}

// epub.page(canvas, from, opts): a page of the chapter that is open, painted in that canvas.
//
// `opts` is `{face, title_face, small_face, scale, title_scale, small_scale, indent}`, and every one of them has a
// default: the face of the system and the steps of a page of a book.
int l_epub_page(lua_State* L) {
  if (g_book_ops.page == nullptr) return push_nil_error(L, "E_STATE", "this build carries no reader");
  // **The canvas can be the object an app holds or its number** (0.134.0): an app has the object, and a probe that
  // paints on the view of the app has the zero.
  int16_t slot = 0;
  if (lua_isnumber(L, 1)) {
    slot = static_cast<int16_t>(lua_tointeger(L, 1));
  } else if (canvas_ready(L, 1)) {
    slot = canvas_slot_of(L);
  } else {
    return push_nil_error(L, "E_INVAL", "a page needs a canvas");
  }
  const int32_t from = static_cast<int32_t>(luaL_optinteger(L, 2, 0));

  BookStyle style;
  style.body = epub_opt_font(L, 3, "face", style.body);
  style.title = epub_opt_font(L, 3, "title_face", style.body);
  style.small = epub_opt_font(L, 3, "small_face", style.body);
  style.body_scale = static_cast<uint8_t>(epub_opt_number(L, 3, "scale", style.body_scale));
  style.title_scale = static_cast<uint8_t>(epub_opt_number(L, 3, "title_scale", style.title_scale));
  style.small_scale = static_cast<uint8_t>(epub_opt_number(L, 3, "small_scale", style.small_scale));
  style.indent = static_cast<int16_t>(epub_opt_number(L, 3, "indent", style.indent));
  style.measure = epub_opt_number(L, 3, "measure", 0) != 0;
  // **The dark page** (0.137.0, docs/46 C9): `dark = true` paints the same page with the ink and the paper swapped, and it
  // is the app that clears its canvas to black — the level it passes here is the ink (docs/46, A5).
  style.dark = epub_opt_number(L, 3, "dark", 0) != 0;

  if (!g_book_ops.page(slot, from, style)) {
    return push_nil_error(L, "E_INVAL", "that page cannot be composed: no book, no chapter, or no canvas");
  }

  lua_pushinteger(L, style.next);
  lua_pushboolean(L, style.ended ? 1 : 0);
  lua_pushinteger(L, style.used);
  lua_pushboolean(L, style.has_picture ? 1 : 0);
  return 4;
}

// **A picture of the book, whole, on a canvas of the app** (0.144.0, docs/46 C7): the cover. The name is the one inside
// the book (`OEBPS/img/cover.png`) and not one relative to a chapter, because the cover is not in a chapter.
int l_epub_picture(lua_State* L) {
  if (g_book_ops.picture == nullptr) return push_nil_error(L, "E_STATE", "this build carries no reader");
  int16_t slot = 0;
  if (lua_isnumber(L, 1)) {
    slot = static_cast<int16_t>(lua_tointeger(L, 1));
  } else if (canvas_ready(L, 1)) {
    slot = canvas_slot_of(L);
  } else {
    return push_nil_error(L, "E_INVAL", "a picture needs a canvas");
  }
  const char* name = luaL_checkstring(L, 2);
  if (!g_book_ops.picture(slot, name)) {
    // **The reason the picture did not come up** (0.151.20): the book may not carry it, it may be bigger than the block
    // that holds a picture —with both numbers— or it may not be a picture this system reads. One sentence for seven
    // causes is a sentence that sends a person to guess (docs/46, A5 and C7).
    const char* why = (g_book_ops.picture_why != nullptr) ? g_book_ops.picture_why()
                                                         : "the book carries no such picture";
    return push_nil_error(L, "E_INVAL", why);
  }

  lua_pushboolean(L, 1);
  return 1;
}

// **The cache of a book, beside the book** (0.146.0, docs/46 C11): the cover scaled to `max` pixels and written as an
// `ICS1` in the folder of the `.epub`, hidden. `true` when the cache is there —or when the flash has no room for it,
// because a book whose cache cannot be written is not a book to try again at every page— and `nil` and the reason when
// the picture cannot be read.
int l_epub_thumbnail(lua_State* L) {
  if (g_book_ops.thumbnail == nullptr) return push_nil_error(L, "E_STATE", "this build carries no reader");
  const char* name = luaL_checkstring(L, 1);
  const int32_t max = static_cast<int32_t>(luaL_optinteger(L, 2, 64));
  if (!g_book_ops.thumbnail(name, max)) {
    const char* why = (g_book_ops.picture_why != nullptr) ? g_book_ops.picture_why()
                                                         : "the cache of the book cannot be written";
    return push_nil_error(L, "E_INVAL", why);
  }

  lua_pushboolean(L, 1);
  return 1;
}

// ---- require: an app in more than one file (0.148.0, docs/46 D6) ----
//
// **An app is one file until it is not.** `require('shelf')` reads `shelf.lua` from the folder the app was installed
// in and runs it the first time it is asked; the value it returns is the answer of every later `require` of that name,
// which is what `package.loaded` means in Lua. And the reason it exists is the room: **a module is a chunk of its
// own**, with its own ceiling of source (`kModuleCap`) and its own two hundred locals, so a reader of fifteen hundred
// lines has somewhere to grow that is not the main chunk of the app (docs/46, D6).
//
// What it is not is a search path: there is no `package.path`, no `package.cpath`, no searcher and no preload —the
// table `package` holds one field, `loaded`— because a module is a file of the app's own folder, named by the name
// that was given, and the kernel is the one that refuses a name which climbs, starts at a unit or carries a dot
// (docs/30, docs/70). And `load`, `loadfile` and `dofile` stay sealed: an app still cannot open a file and call it.
const uint32_t kModuleCap = 64u * 1024u;

int l_require(lua_State* L) {
  const char* name = luaL_checkstring(L, 1);   // [name]
  if (name[0] == '\0') return luaL_error(L, "require: a module needs a name");
  if (g_file_ops.module_source == nullptr) {
    return luaL_error(L, "require: this build cannot read the files of an app");
  }

  // **Everything below is counted from the top of the stack and not from the bottom**: the arguments of a function
  // written in C are at 1, so what is at 1 here is the name and not the table of `package` (this cost a turn, 0.150.1).
  lua_getglobal(L, "package");               // [name, package]
  if (!lua_istable(L, -1)) return luaL_error(L, "require: this app has no package table");
  lua_getfield(L, -1, "loaded");             // [name, package, loaded]
  if (!lua_istable(L, -1)) return luaL_error(L, "require: this app has no loaded table");
  lua_getfield(L, -1, name);                 // [name, package, loaded, value]
  if (!lua_isnil(L, -1)) return 1;           // the module of the first time, whatever it was
  lua_pop(L, 1);                             // [name, package, loaded]

  char* source = static_cast<char*>(malloc(kModuleCap));
  if (source == nullptr) return luaL_error(L, "require: no memory for the module '%s'", name);
  FsError err = FsError::kOk;
  const int32_t got = g_file_ops.module_source(name, source, kModuleCap, err);
  char chunk[96];
  snprintf(chunk, sizeof(chunk), "@%s.lua", name);
  // A module of no bytes is a chunk that does nothing and not a missing file: only a refusal of the kernel is an error.
  const int status = (got >= 0) ? luaL_loadbuffer(L, source, static_cast<size_t>(got), chunk) : LUA_ERRSYNTAX;
  // The chunk has its own copies of its strings by now, and the block is given back **before** the module runs: a
  // module that waits forever must not be holding the text of itself (docs/75).
  free(source);
  if (got < 0) {
    return luaL_error(L, "require: '%s' is not a file of this app (%s)", name, fs_error_message(err));
  }
  if (status != LUA_OK) return lua_error(L);  // the message of the compiler is the top of the stack

  // **The name is marked before it runs**: two modules that require each other answer `true` to each other instead of
  // looping until the interpreter's stack is gone, and the value the module returns replaces the mark below.
  lua_pushboolean(L, 1);                     // [name, package, loaded, chunk, true]
  lua_setfield(L, -3, name);                 // loaded[name] = true, and the true is popped
  lua_pushstring(L, name);                   // [name, package, loaded, chunk, name]
  lua_call(L, 1, 1);                         // [name, package, loaded, value]
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    lua_pushboolean(L, 1);                   // a module with no value is `true`, as in Lua
  }
  lua_pushvalue(L, -1);                      // [name, package, loaded, value, value]
  lua_setfield(L, -3, name);                 // loaded[name] = value
  return 1;                                  // [.., value]
}

// **A face of a book is a face of the page** (0.179.9): `epub.font` reads the face out of the book and hands it to the
// same three slots `ui.load_font` uses, so it goes with the page like the rest of the reader (docs/46).
// **The face a book carries inside itself** (0.150.0, docs/46 D6): `epub.font(name)` reads the font entry of the book
// —a TrueType or a `.woff`, which the kernel opens into the same kind of bytes— and makes a face of it, which is a font
// object like the one `ui.load_font` answers: the app gives it to `epub.page` as `face` or `title_face` and gives it back
// with `font:release()`. **The three faces of an app are shared between the two doors** (docs/30): a book's face takes
// one of the three slots, so an app that reads two faces of the disk and one of a book has no room for a fourth.
int l_epub_font(lua_State* L) {
  if (g_book_ops.font == nullptr) return push_nil_error(L, "E_STATE", "this build carries no reader");
  if (g_ui_ops.font_load_bytes == nullptr) return push_nil_error(L, "E_UNSUPPORTED", "this build carries no faces");
  const char* name = luaL_checkstring(L, 1);

  const uint8_t* bytes = nullptr;
  uint32_t size = 0;
  const char* why = nullptr;
  if (!g_book_ops.font(name, &bytes, size, why)) {
    return push_nil_error(L, "E_INVAL", (why != nullptr) ? why : "the book carries no such face");
  }
  const char* reason = nullptr;
  const int8_t handle = g_ui_ops.font_load_bytes(bytes, size, reason);
  if (handle < 0) return push_nil_error(L, "E_INVAL", (reason != nullptr) ? reason : "that is not a face");
  return push_font(L, handle);
}

// ---------------------------------------------------------------------------
// a session of the shell, for an app (0.159.0, docs/30)
// ---------------------------------------------------------------------------
//

// **The shell is the system's and not the wire's.** `sys.shell()` asks for a session of the system's own shell and
// what comes back is a handle with the **three pipes** of one: what is written on `stdin` are the lines the shell
// runs, `stdout` is what the shell answers and `stderr` is what it complains about. No console, no prompt, no echo —
// a pipe is not a terminal— and one shell underneath: the lines go to the same `execute` the console uses, which is
// what makes a session a session and not a second shell (docs/30, docs/75).
//
// **The kernel gives them and `luabind` asks**: the table of function pointers of `luabind.h`, installed by the
// kernel, because this library never includes the kernel (docs/20).
const char kShellMeta[] = "lunokOS.ShellSession";

// The handle a session is, in Lua: the id the kernel gave, and zero once it is closed.
struct LuaShell {
  uint8_t id = 0;
};

// **The bytes one read takes at most**: the ring of a session is 1 024 B and a read that asks for more than that is
// a read that waits for a byte that is not there (docs/30, docs/75).
const size_t kShellReadCap = 1024;

int l_sys_shell(lua_State* L);

// **A pipe answers with or without a colon** (0.163.2): `stdin`, `stdout` and `stderr` are closures over the id of
// their session, so the table they are read from is not an argument they need — and `session.stdout:read()` is how the
// session is written in every example of docs/30 and in the app that found it, which passed the pipe table as the
// first argument and got `calling 'read' on bad self (number expected, got table)` on the emulator on 8/11. The table is
// stepped over instead of refused, and `stdout.read(64)` keeps working the same.
static int pipe_arg(lua_State* L) { return lua_istable(L, 1) ? 2 : 1; }

// **`stdin:write(text)`**: the lines of the session, one command per line, and `true` when the shell took them.
int l_shell_stdin_write(lua_State* L) {
  const uint8_t id = static_cast<uint8_t>(lua_tointeger(L, lua_upvalueindex(1)));
  size_t len = 0;
  const char* text = luaL_checklstring(L, pipe_arg(L), &len);
  if (g_shell_ops.write == nullptr || id == 0) {
    lua_pushboolean(L, 0);
    return 1;
  }
  lua_pushboolean(L, g_shell_ops.write(id, text, len) ? 1 : 0);
  return 1;
}

// **One read of a pipe**: the bytes that are waiting right now and never more than the cap, which is what a pipe
// does —an empty string is a pipe with nothing in it and not the end of anything— (docs/30).
int l_shell_pipe_read(lua_State* L, bool error_pipe) {
  const uint8_t id = static_cast<uint8_t>(lua_tointeger(L, lua_upvalueindex(1)));
  lua_Integer asked = luaL_optinteger(L, pipe_arg(L), static_cast<lua_Integer>(kShellReadCap));
  if (asked <= 0) asked = 1;
  const size_t cap = (static_cast<size_t>(asked) < kShellReadCap) ? static_cast<size_t>(asked) : kShellReadCap;
  char out[kShellReadCap + 1];
  size_t got = 0;
  if (g_shell_ops.read != nullptr && id != 0) {
    got = error_pipe ? g_shell_ops.read_error(id, out, cap + 1) : g_shell_ops.read(id, out, cap + 1);
  }
  lua_pushlstring(L, out, got);
  return 1;
}

int l_shell_stdout_read(lua_State* L) { return l_shell_pipe_read(L, false); }

int l_shell_stderr_read(lua_State* L) { return l_shell_pipe_read(L, true); }

// **The fields of one session** and the verbs of all of them: `stdin`, `stdout` and `stderr` are made with the
// session —each one a closure over its id— and live in the uservalue, so a second session does not answer with the
// pipes of the first (docs/30).
int l_shell_index(lua_State* L) {
  if (lua_getiuservalue(L, 1, 1) == LUA_TTABLE) {
    lua_pushvalue(L, 2);
    lua_gettable(L, -2);
    if (!lua_isnil(L, -1)) return 1;
    lua_pop(L, 2);
  } else {
    lua_pop(L, 1);
  }
  luaL_getmetatable(L, kShellMeta);
  lua_pushvalue(L, 2);
  lua_gettable(L, -2);
  return 1;
}

int l_shell_close(lua_State* L) {
  LuaShell* session = static_cast<LuaShell*>(luaL_checkudata(L, 1, kShellMeta));
  if (session->id != 0 && g_shell_ops.close != nullptr) g_shell_ops.close(session->id);
  session->id = 0;
  lua_pushboolean(L, 1);
  return 1;
}

int l_shell_gc(lua_State* L) {
  LuaShell* session = static_cast<LuaShell*>(luaL_testudata(L, 1, kShellMeta));
  if (session != nullptr && session->id != 0 && g_shell_ops.close != nullptr) g_shell_ops.close(session->id);
  if (session != nullptr) session->id = 0;
  return 0;
}

// **`session:dropped()`**: the bytes the shell said and nobody read, because a session that loses words has to be
// able to tell (docs/30).
int l_shell_dropped(lua_State* L) {
  LuaShell* session = static_cast<LuaShell*>(luaL_checkudata(L, 1, kShellMeta));
  if (session->id == 0 || g_shell_ops.dropped == nullptr) {
    lua_pushinteger(L, 0);
    return 1;
  }
  lua_pushinteger(L, static_cast<lua_Integer>(g_shell_ops.dropped(session->id)));
  return 1;
}

int l_shell_id(lua_State* L) {
  LuaShell* session = static_cast<LuaShell*>(luaL_checkudata(L, 1, kShellMeta));
  lua_pushinteger(L, session->id);
  return 1;
}

int l_shell_tostring(lua_State* L) {
  LuaShell* session = static_cast<LuaShell*>(luaL_checkudata(L, 1, kShellMeta));
  lua_pushfstring(L, "shell session %d", static_cast<int>(session->id));
  return 1;
}

// The three pipes and the verbs of one session: one table of fields (the uservalue) and one metatable of methods.
int push_shell(lua_State* L, uint8_t id) {
  LuaShell* session = static_cast<LuaShell*>(lua_newuserdatauv(L, sizeof(LuaShell), 1));
  session->id = id;
  luaL_getmetatable(L, kShellMeta);
  lua_setmetatable(L, -2);

  lua_newtable(L);
  lua_newtable(L);
  lua_pushinteger(L, id);
  lua_pushcclosure(L, l_shell_stdin_write, 1);
  lua_setfield(L, -2, "write");
  lua_setfield(L, -2, "stdin");
  lua_newtable(L);
  lua_pushinteger(L, id);
  lua_pushcclosure(L, l_shell_stdout_read, 1);
  lua_setfield(L, -2, "read");
  lua_setfield(L, -2, "stdout");
  lua_newtable(L);
  lua_pushinteger(L, id);
  lua_pushcclosure(L, l_shell_stderr_read, 1);
  lua_setfield(L, -2, "read");
  lua_setfield(L, -2, "stderr");
  lua_setiuservalue(L, -2, 1);
  return 1;
}

// **`sys.commands()`**: the words of the prompt, which is what the app of the shell offers as a list so a person picks a
// command instead of typing it (0.189.9). The kernel owns the table and answers how many there are; this asks twice —once
// for the count and once for the names— because that is what an array of an unknown length costs and the list is forty
// words (docs/30).
int l_sys_commands(lua_State* L) {
  if (g_shell_ops.commands == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this system has no shell to give");
  }
  const uint8_t total = g_shell_ops.commands(nullptr, 0);
  const char* names[64];
  const uint8_t copied = g_shell_ops.commands(names, static_cast<uint8_t>((total > 64) ? 64 : total));
  lua_createtable(L, copied, 0);
  for (uint8_t i = 0; i < copied; ++i) {
    lua_pushstring(L, names[i]);
    lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
  }
  return 1;
}

// **`sys.stats([<word>])`: els comptadors del sistema** (0.189.108, l'amo: *«si pot ser agafa de les estadístiques els tempos
// de les apps lua, així es pot valorar quin es el numero mágic i quins valors son segurs per ambdos sistemes»*). With no
// argument it answers **the meters this build has**, as words, so a page lists what exists and does not copy a list that will
// disagree with the table. With one it answers **two numbers** —the calls and the microseconds— or `nil` for a word that is
// not a meter here, and the page divides them: what a turn of the interpreter costs is `us / calls` of `kernel.loop`, what
// part of it is Lua is `lua.run`, and what part is the app's own code is `lua.own` (docs/30, docs/75, docs/95).
int l_sys_stats(lua_State* L) {
  if (g_stats_ops.count == nullptr || g_stats_ops.name == nullptr || g_stats_ops.read == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build counts nothing");
  }
  const uint8_t total = g_stats_ops.count();
  if (lua_isnoneornil(L, 1)) {
    lua_createtable(L, total, 0);
    for (uint8_t i = 0; i < total; ++i) {
      const char* word = g_stats_ops.name(i);
      if (word == nullptr) continue;
      lua_pushstring(L, word);
      lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    return 1;
  }
  const char* asked = luaL_checkstring(L, 1);
  for (uint8_t i = 0; i < total; ++i) {
    const char* word = g_stats_ops.name(i);
    if (word == nullptr || strcmp(word, asked) != 0) continue;
    uint32_t calls = 0;
    uint64_t us = 0;
    uint32_t shortest = 0;
    uint32_t longest = 0;
    uint32_t last_ms = 0;
    // **Els dos números de sempre i, darrere, les tres columnes de la taula** (0.192.36): a caller that takes two values
    // (`local calls, us = sys.stats(w)`) keeps working, perquè Lua llença el que ningú no recull (docs/30).
    if (g_stats_ops.read_full != nullptr) {
      if (!g_stats_ops.read_full(i, &calls, &us, &shortest, &longest, &last_ms)) break;
    } else if (!g_stats_ops.read(i, &calls, &us)) {
      break;
    }
    lua_pushinteger(L, static_cast<lua_Integer>(calls));
    lua_pushinteger(L, static_cast<lua_Integer>(us));
    lua_pushinteger(L, static_cast<lua_Integer>(shortest));
    lua_pushinteger(L, static_cast<lua_Integer>(longest));
    lua_pushinteger(L, static_cast<lua_Integer>(last_ms));
    return 5;
  }
  lua_pushnil(L);
  lua_pushstring(L, "there is no meter with that name");
  return 2;
}

// **`sys.shell()`**: a session of the shell, or `nil` and why there is none (docs/30).
// **`sys.settings()`**: a snapshot of what the system is set to, in the words `cfg` prints, so a page that has to obey a
// switch reads the same value a person reads at the prompt (0.189.13, docs/30, docs/70).
//
// It is a copy and not a door: an app that changed a setting would be an app that changes the board for every other app,
// and the settings are the person's. `cfg` is where they are written, and this is where they are read.
int l_sys_settings(lua_State* L) {
  if (g_settings_ops.read == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this system keeps no settings");
  }
  Settings now;
  if (!g_settings_ops.read(now)) {
    return push_nil_error(L, "E_UNAVAIL", "the settings are not there yet");
  }
  lua_createtable(L, 0, 10);
  lua_pushstring(L, (now.unit != nullptr) ? now.unit : "");
  lua_setfield(L, -2, "unit");
  lua_pushstring(L, (now.keyboard != nullptr) ? now.keyboard : "");
  lua_setfield(L, -2, "keyboard");
  lua_pushstring(L, (now.boot != nullptr) ? now.boot : "");
  lua_setfield(L, -2, "boot");
  lua_pushstring(L, (now.home != nullptr) ? now.home : "");
  lua_setfield(L, -2, "home");
  lua_pushstring(L, (now.week != nullptr) ? now.week : "monday");
  lua_setfield(L, -2, "week");
  lua_pushstring(L, (now.tz != nullptr) ? now.tz : "+00:00");
  lua_setfield(L, -2, "tz");
  lua_pushboolean(L, now.dst);
  lua_setfield(L, -2, "dst");
  lua_pushboolean(L, now.sort_by_usage);
  lua_setfield(L, -2, "sort_by_usage");
  lua_pushinteger(L, static_cast<lua_Integer>(now.turn));
  lua_setfield(L, -2, "turn");
  lua_pushinteger(L, static_cast<lua_Integer>(now.cursor_delay));
  lua_setfield(L, -2, "cursor");
  // **L'app del botó dels ajustos** (0.189.109): the empty string is a gear that does nothing, which is a choice of the person and
  // not a missing value (docs/30, docs/40).
  lua_pushstring(L, now.settings_app);
  lua_setfield(L, -2, "settings_app");
  return 1;
}

// ---- cfg: els ajustos del sistema, llegits i escrits (0.189.106) ----
//
// **La mateixa porta que la comanda `cfg`** (l'amo: *«Lua app de Settings de sistema … Analitza totes les opcions de la
// comanda cfg»*): each call takes an optional value —with none it reads, with one it writes— and **answers the value that is
// in force after asking**, so a page that a keyboard does not exist for learns it from the answer and not from a guess. The
// values are the words the prompt prints: a keyboard is its id, the boot is its word, the home app is its id (`"none"` for
// no home app), and the three numbers are a slice of instructions, a blink in milliseconds and the hour of the map.
//
// **El que no és d'aquesta taula és el rellotge i el disc**, que ja tenen les seves portes de sempre: `time.tz`, `time.dst`,
// `time.week` i `fs.cfg` (docs/30, docs/70).

// **Els sis mots d'un commutador** (0.189.13): the same six `cfg` takes —`on`, `true`, `yes` and `off`, `false`, `no`— and a
// Lua boolean, because a page writes `true` and not the word.
bool cfg_switch(lua_State* L, int at, bool& on) {
  if (lua_isboolean(L, at)) {
    on = lua_toboolean(L, at) != 0;
    return true;
  }
  const char* word = luaL_checkstring(L, at);
  if (strcmp(word, "on") == 0 || strcmp(word, "true") == 0 || strcmp(word, "yes") == 0) {
    on = true;
    return true;
  }
  if (strcmp(word, "off") == 0 || strcmp(word, "false") == 0 || strcmp(word, "no") == 0) {
    on = false;
    return true;
  }
  return false;
}

int l_cfg_keyboard(lua_State* L) {
  if (g_cfg_ops.keyboard == nullptr || g_cfg_ops.set_keyboard == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build keeps no keyboard");
  }
  if (!lua_isnoneornil(L, 1)) {
    const char* id = luaL_checkstring(L, 1);
    if (!g_cfg_ops.set_keyboard(id)) {
      return push_nil_error(L, "E_INVAL", "this build carries no keyboard with that id");
    }
  }
  lua_pushstring(L, g_cfg_ops.keyboard());
  return 1;
}

int l_cfg_boot(lua_State* L) {
  if (g_cfg_ops.boot == nullptr || g_cfg_ops.set_boot == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build keeps no picture of the boot");
  }
  if (!lua_isnoneornil(L, 1)) {
    const char* word = luaL_checkstring(L, 1);
    if (!g_cfg_ops.set_boot(word)) {
      return push_nil_error(L, "E_INVAL", "the disk has no picture with that word");
    }
  }
  lua_pushstring(L, g_cfg_ops.boot());
  return 1;
}

int l_cfg_home(lua_State* L) {
  if (g_cfg_ops.home == nullptr || g_cfg_ops.set_home == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build keeps no home app");
  }
  if (!lua_isnoneornil(L, 1)) {
    const char* id = luaL_checkstring(L, 1);
    if (!g_cfg_ops.set_home(id)) {
      return push_nil_error(L, "E_INVAL", "no app with that id runs on this board");
    }
  }
  lua_pushstring(L, g_cfg_ops.home());
  return 1;
}

int l_cfg_sort(lua_State* L) {
  if (g_cfg_ops.sort_by_usage == nullptr || g_cfg_ops.set_sort_by_usage == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build keeps no order of the apps");
  }
  if (!lua_isnoneornil(L, 1)) {
    bool on = false;
    if (!cfg_switch(L, 1, on) || !g_cfg_ops.set_sort_by_usage(on)) {
      return push_nil_error(L, "E_INVAL", "sorting by usage is on or off");
    }
  }
  lua_pushboolean(L, g_cfg_ops.sort_by_usage() ? 1 : 0);
  return 1;
}

int l_cfg_turn(lua_State* L) {
  if (g_cfg_ops.turn == nullptr || g_cfg_ops.set_turn == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build keeps no slice of a turn");
  }
  if (!lua_isnoneornil(L, 1)) {
    const lua_Integer asked = luaL_checkinteger(L, 1);
    if (asked < 0 || asked > 1000000 || !g_cfg_ops.set_turn(static_cast<uint32_t>(asked))) {
      return push_nil_error(L, "E_INVAL", "a turn is between two thousand instructions and the ceiling of a slice");
    }
  }
  lua_pushinteger(L, static_cast<lua_Integer>(g_cfg_ops.turn()));
  return 1;
}

int l_cfg_cursor(lua_State* L) {
  if (g_cfg_ops.cursor_delay == nullptr || g_cfg_ops.set_cursor_delay == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build keeps no caret");
  }
  if (!lua_isnoneornil(L, 1)) {
    const lua_Integer asked = luaL_checkinteger(L, 1);
    if (asked < 0 || asked > 100000 || !g_cfg_ops.set_cursor_delay(static_cast<int32_t>(asked))) {
      return push_nil_error(L, "E_INVAL", "a blink is a number of milliseconds");
    }
  }
  lua_pushinteger(L, static_cast<lua_Integer>(g_cfg_ops.cursor_delay()));
  return 1;
}

// **`cfg.settings([<id>])`: l'app que obre el botó dels ajustos** (0.189.109, l'amo: *«Enllaça el botó de Settings del panell
// desplegable de sistema amb l'app <id: settings> … Aegeix un selector d'aplicació per gestionar els settings»*). With no argument
// it answers the name, or **the empty string when a person chose `none`** —a gear that does nothing is a choice and not a missing
// value—; with a name it writes it and answers the one that stands, like `cfg.home`. Which names are apps of this board is the
// kernel's question: the door writes what it is given and `E_INVAL` is a name it cannot keep (docs/30, docs/40).
int l_cfg_settings_app(lua_State* L) {
  if (g_cfg_ops.settings_app == nullptr || g_cfg_ops.set_settings_app == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build has no app of settings");
  }
  if (!lua_isnoneornil(L, 1)) {
    const char* asked = luaL_checkstring(L, 1);
    if (!g_cfg_ops.set_settings_app(asked)) {
      return push_nil_error(L, "E_INVAL", "that is not a name an app of this system can have");
    }
  }
  lua_pushstring(L, g_cfg_ops.settings_app());
  return 1;
}

// **`cfg.lock([<id>])`: la pàgina que tapa el vidre** (0.189.150): with no argument it answers the name —the empty string when a
// person chose `none`—, and with a name it writes it and answers the one that stands, exactly like `cfg.home` and
// `cfg.settings`. `cfg.lockon([on])` és el commutador de la pestanya Privacy i `cfg.lockafter([minutes])` els minuts sense
// tocar res, amb els tres de la llista i res més (docs/30, docs/40, docs/60).
int l_cfg_lock(lua_State* L) {
  if (g_cfg_ops.lock_app == nullptr || g_cfg_ops.set_lock_app == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build keeps no lock of the screen");
  }
  if (!lua_isnoneornil(L, 1)) {
    const char* asked = luaL_checkstring(L, 1);
    if (!g_cfg_ops.set_lock_app(asked)) {
      return push_nil_error(L, "E_INVAL", "that is not a name an app of this system can have");
    }
  }
  lua_pushstring(L, g_cfg_ops.lock_app());
  return 1;
}

// **`cfg.gps_sync([on])`** (0.192.63): el commutador de Settings→Clock, el mateix que `cfg gpssync on|off` del prompt. Una placa
// que no mira el cel el refusa en lloc d'acceptar un sí que no vol dir res (docs/30, docs/60).
int l_cfg_gps_sync(lua_State* L) {
  if (g_cfg_ops.gps_sync == nullptr || g_cfg_ops.set_gps_sync == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board does not look at the sky");
  }
  if (!lua_isnoneornil(L, 1)) {
    // **Una placa que no mira el cel no pot acceptar un sí** (0.192.63): the switch of Settings is drawn dead on a board without a
    // receiver, and a page that turned it on anyway would be storing a promise nothing can keep (docs/30, docs/50).
    if (!hal::platform().caps().has(hal::kCapGps)) {
      return push_nil_error(L, "E_UNSUPPORTED", "this board does not look at the sky");
    }
    bool on = false;
    if (!cfg_switch(L, 1, on) || !g_cfg_ops.set_gps_sync(on)) {
      return push_nil_error(L, "E_INVAL", "the sky sets the clock or it does not: on or off");
    }
  }
  lua_pushboolean(L, g_cfg_ops.gps_sync() ? 1 : 0);
  return 1;
}

int l_cfg_lock_on(lua_State* L) {
  if (g_cfg_ops.lock_on == nullptr || g_cfg_ops.set_lock_on == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build keeps no lock of the screen");
  }
  if (!lua_isnoneornil(L, 1)) {
    bool on = false;
    if (!cfg_switch(L, 1, on) || !g_cfg_ops.set_lock_on(on)) {
      return push_nil_error(L, "E_INVAL", "the lock of the screen is on or off");
    }
  }
  lua_pushboolean(L, g_cfg_ops.lock_on() ? 1 : 0);
  return 1;
}

// **Si la placa s'adorm sola** (0.190.5, l'amo: *«On es l'opció a Settings per desactivar-ho?»*): the switch of the System tab asks the
// board whether it powers itself off until its next alarm, and a person turns it off from there. The value answered is the one the
// policy holds, so the page shows what the board is doing and not what the page asked (docs/30, docs/60).
int l_cfg_sleep(lua_State* L) {
  if (g_cfg_ops.sleep_on == nullptr || g_cfg_ops.set_sleep_on == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build keeps no idle sleep");
  }
  if (!lua_isnoneornil(L, 1)) {
    bool on = false;
    if (!cfg_switch(L, 1, on) || !g_cfg_ops.set_sleep_on(on)) {
      return push_nil_error(L, "E_INVAL", "the sleep of the board is on or off");
    }
  }
  lua_pushboolean(L, g_cfg_ops.sleep_on() ? 1 : 0);
  return 1;
}

// **`cfg.idle([<seconds>])`: quanta quietud abans del son profund** (0.190.9, l'amo: *«afegir selector de temps per l'idle de
// settings->battery (per fer el DeepSleep)»*). With no argument it answers the seconds a person chose; with one it writes it and
// answers what stands, exactly like the minutes of the lock: **ten, sixty or three hundred and nothing else**, perquè un número
// que no és a la llista és un ajust que la pàgina no sap ensenyar ni desfer (docs/30, docs/60).
int l_cfg_idle(lua_State* L) {
  if (g_cfg_ops.idle_seconds == nullptr || g_cfg_ops.set_idle_seconds == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build keeps no idle sleep");
  }
  if (!lua_isnoneornil(L, 1)) {
    const lua_Integer asked = luaL_checkinteger(L, 1);
    if (asked < 0 || asked > 65535 || !g_cfg_ops.set_idle_seconds(static_cast<uint16_t>(asked))) {
      return push_nil_error(L, "E_INVAL", "the board sleeps after 5, 10 or 15 minutes of quiet");
    }
  }
  lua_pushinteger(L, static_cast<lua_Integer>(g_cfg_ops.idle_seconds()));
  return 1;
}

// **Si el vidre desperta el son profund** (0.190.16, l'amo: *«opcionalment el panell tàctil (per defecte false) així no cal mantenir
// despert el panell»*): the third door of the way down, and the only one that costs current while the board sleeps. Off is what a board
// that has not been told anything answers (docs/30, docs/60).
// cfg.uart_wake([on]): **si la consola desperta la becaina** (0.190.147, l'amo: *«Posa un toggle a Settings->Battery que doni la opció a
// "Wake from UART"»*). Sense argument pregunta, i amb un llegeix el de sempre (`on`, `off`, `true`, `false`). És la porta que no es pot
// perdre en una placa sense teclat, i per això ve encesa: el que fa el commutador és deixar-la tancar a qui vulgui (docs/30, docs/50).
int l_cfg_uart_wake(lua_State* L) {
  if (g_cfg_ops.uart_wake == nullptr || g_cfg_ops.set_uart_wake == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build keeps no console as a door");
  }
  if (!lua_isnoneornil(L, 1)) {
    bool on = false;
    if (!cfg_switch(L, 1, on) || !g_cfg_ops.set_uart_wake(on)) {
      return push_nil_error(L, "E_INVAL", "the console is awake or asleep");
    }
  }
  lua_pushboolean(L, g_cfg_ops.uart_wake() ? 1 : 0);
  return 1;
}

int l_cfg_touch_wake(lua_State* L) {
  if (g_cfg_ops.wake_on_touch == nullptr || g_cfg_ops.set_wake_on_touch == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build keeps no way down to the glass");
  }
  if (!lua_isnoneornil(L, 1)) {
    bool on = false;
    if (!cfg_switch(L, 1, on) || !g_cfg_ops.set_wake_on_touch(on)) {
      return push_nil_error(L, "E_INVAL", "the glass of the sleep is on or off");
    }
  }
  lua_pushboolean(L, g_cfg_ops.wake_on_touch() ? 1 : 0);
  return 1;
}

// **`cfg.nap([true|false])`: si les esperes es passen dormint** (0.190.63, l'amo: *«[X] Light sleep instead of wait. Aplica lightSleep allá on
// puguis»*). With no argument it answers what the system does; with one it writes it and answers what stands, like every other switch of
// the page. Una placa que no fa becaines accepta l'ajust i no en fa res, que és el que `power` diu de si mateixa (docs/30, docs/50).
int l_cfg_nap(lua_State* L) {
  if (g_cfg_ops.nap_on == nullptr || g_cfg_ops.set_nap_on == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build keeps no nap");
  }
  if (!lua_isnoneornil(L, 1)) {
    bool on = false;
    if (!cfg_switch(L, 1, on) || !g_cfg_ops.set_nap_on(on)) {
      return push_nil_error(L, "E_INVAL", "the nap of the board is on or off");
    }
  }
  lua_pushboolean(L, g_cfg_ops.nap_on() ? 1 : 0);
  return 1;
}

// **`cfg.bt([true|false])`: el teclat de Bluetooth** (0.190.180). With no argument it answers whether the radio is offered; with one it
// opens or closes it and answers what stands. **En aquesta placa la resposta no és només un gust**: mentre és encès el xip no fa becaines,
// o sigui que la persona que l'encén canvia com es passa l'estona la placa (docs/30, docs/50, docs/60).
// **`cfg.btmode` ja no hi és** (0.191.0): hi havia hagut tres modes i una paraula per triar-los, i el mode era el que obligava a
// esborrar el vincle de l'altre cap cada cop que canviava. La placa publica sempre el teclat i el ratolí, o sigui que no hi ha res a
// triar; el que queda és encendre-la o apagar-la, que és `cfg.bt` (docs/30, docs/50).
int l_cfg_bt(lua_State* L) {
  if (g_cfg_ops.bt_on == nullptr || g_cfg_ops.set_bt_on == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build keeps no keyboard");
  }
  if (!lua_isnoneornil(L, 1)) {
    bool on = false;
    if (!cfg_switch(L, 1, on) || !g_cfg_ops.set_bt_on(on)) {
      return push_nil_error(L, "E_INVAL", "the keyboard of the board is on or off");
    }
  }
  lua_pushboolean(L, g_cfg_ops.bt_on() ? 1 : 0);
  return 1;
}

// **`bt.available()`**: si aquesta placa té ràdio i el build la porta a dins. Una pàgina que ho pregunta no ha d'ensenyar un control que no
// pot fer res (docs/30, docs/50).
int l_bt_available(lua_State* L) {
  if (g_bt_ops.available == nullptr) {
    lua_pushboolean(L, 0);
    return 1;
  }
  lua_pushboolean(L, g_bt_ops.available() ? 1 : 0);
  return 1;
}

// **`bt.connected()`**: si algú hi és a l'altre cap. Un teclat que no ho diu envia caràcters al buit (docs/30).
int l_bt_connected(lua_State* L) {
  if (g_bt_ops.connected == nullptr) {
    lua_pushboolean(L, 0);
    return 1;
  }
  lua_pushboolean(L, g_bt_ops.connected() ? 1 : 0);
  return 1;
}

// **`bt.type(<text>)`: escriure a l'altre aparell**, i la resposta són quants caràcters han sortit. Un text que no hi cap o un teclat sense
// ningú connectat tornen `0` i no un error: el que ha passat no és un problema de la crida, és que no hi havia on escriure (docs/30).
int l_bt_type(lua_State* L) {
  if (g_bt_ops.type == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build keeps no keyboard");
  }
  const char* text = luaL_checkstring(L, 1);
  lua_pushinteger(L, static_cast<lua_Integer>(g_bt_ops.type(text)));
  return 1;
}

// **`bt.ready([<which>])`**: si l'altre cap ha encès els reports (0.190.208). `bt.connected()` diu que hi ha un aparell a l'altre
// costat; això diu que a més a més escolta, que és el que fa que una tecla hi arribi. És la diferència que una pàgina ha de poder
// ensenyar, perquè entremig les notificacions es descarten i sembla que la placa no enviï (docs/30, docs/50).
//
// **I des de la 0.191.0 la placa en publica dos i es poden preguntar un a un** (l'amo: *«i no podem anunciar els dos dispositius sempre?»*):
// una pàgina que mou un punter vol saber del report del ratolí, i prendre el del teclat per bo és una pàgina que es creu viva mentre el dit
// no mou res. Sense argument contesta el «algun» de sempre, que és el que una persona vol saber d'un cop d'ull (docs/30, docs/50).
int l_bt_ready(lua_State* L) {
  if (g_bt_ops.ready == nullptr) return push_nil_error(L, "E_UNSUPPORTED", "this build keeps no keyboard");
  const char* which = luaL_optstring(L, 1, nullptr);
  if (which != nullptr && strcmp(which, "keyboard") != 0 && strcmp(which, "mouse") != 0) {
    return push_nil_error(L, "E_INVAL", "the report is keyboard, mouse, or nothing for the two of them");
  }
  lua_pushboolean(L, g_bt_ops.ready(which) ? 1 : 0);
  return 1;
}

// **`bt.name()`**: el nom amb què aquesta placa s'anuncia, que és el que una persona busca a la llista del telèfon.
int l_bt_name(lua_State* L) {
  if (g_bt_ops.name == nullptr) return push_nil_error(L, "E_UNSUPPORTED", "this build keeps no keyboard");
  lua_pushstring(L, g_bt_ops.name());
  return 1;
}

// **`bt.move(dx, dy)`**: un moviment relatiu del ratolí, i el report torna a zero de seguida. El que no hi cap és del report i no de
// la crida: un moviment més gran de cent vint-i-set s'ha de fer a trossos, i una pàgina que arrossega un dit de sis-cents píxels ho ha
// de saber (docs/30, docs/50).
int l_bt_move(lua_State* L) {
  if (g_bt_ops.move == nullptr) return push_nil_error(L, "E_UNSUPPORTED", "this build keeps no mouse");
  const int dx = static_cast<int>(luaL_checkinteger(L, 1));
  const int dy = static_cast<int>(luaL_checkinteger(L, 2));
  // **Un dit pot saltar més de 127 píxels en un fotograma i el report no els hi cap** (0.190.259, i la pàgina ho va ensenyar a l'amo:
  // *«la idea es usar el panell com a mouse»*): a HID report carries −127 to 127, i el que fa el sistema és **partir el moviment en
  // tants reports com calgui** (`bt::move_for_a_page`, que és qui ho sap) en lloc de refusar-lo, perquè un moviment refusat és un dit
  // que arrossega i un punter que es queda a mig camí. La pàgina no ha de saber res del signe d'un byte (docs/30, docs/50).
  lua_pushboolean(L, g_bt_ops.move(dx, dy) ? 1 : 0);
  return 1;
}

// **`bt.click([<button>])`**: `left` per defecte, i `right` o `middle`. Prem, espera el que un dit hi posa i deixa anar: un botó premut
// per sempre és el que l'altre cap veu si no es deixa anar (docs/30, docs/50).
int l_bt_click(lua_State* L) {
  if (g_bt_ops.click == nullptr) return push_nil_error(L, "E_UNSUPPORTED", "this build keeps no mouse");
  const char* button = luaL_optstring(L, 1, "left");
  if (strcmp(button, "left") != 0 && strcmp(button, "right") != 0 && strcmp(button, "middle") != 0) {
    return push_nil_error(L, "E_INVAL", "the button is left, right or middle");
  }
  lua_pushboolean(L, g_bt_ops.click(button) ? 1 : 0);
  return 1;
}

// **`cfg.loglevel([<word>])`: quantes línies vol veure una persona** (0.190.89, l'amo: *«Afegeix "Log level" a Settings->System Amb els
// nivells de log, per defecte Debug (com la comanda log)»*). With no argument it answers the word the ring obeys; with one it writes it and
// answers what stands, like `cfg.keyboard`. Una paraula que no és cap de les quatre es refusa amb `E_INVAL` i no canvia res (docs/30, docs/50).
int l_cfg_loglevel(lua_State* L) {
  if (g_cfg_ops.log_level == nullptr || g_cfg_ops.set_log_level == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build keeps no level for the log");
  }
  if (!lua_isnoneornil(L, 1)) {
    const char* word = luaL_checkstring(L, 1);
    if (!g_cfg_ops.set_log_level(word)) {
      return push_nil_error(L, "E_INVAL", "the log is debug, info, warn or error");
    }
  }
  lua_pushstring(L, g_cfg_ops.log_level());
  return 1;
}

// **`cfg.backlight([on])` i `cfg.backlight_level([v])`**: la llum del vidre (0.190.106, l'amo: *«Afegeix a Settings->System un toggle amb "Use
// backlight" i un selector desplegable amb tots els valors possibles del backlight»*). Sense argument contesten el que hi ha; amb un, ho
// escriuen i contesten el que queda, com la resta de ports. El nivell es tanca entre u i el sostre de la llum de la placa, perquè una llum
// encesa a zero és una llum apagada i per a això ja hi ha el commutador (docs/30, docs/50).
int l_cfg_backlight(lua_State* L) {
  if (g_cfg_ops.backlight == nullptr || g_cfg_ops.set_backlight == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build keeps no light for the glass");
  }
  if (!lua_isnoneornil(L, 1)) g_cfg_ops.set_backlight(lua_toboolean(L, 1) != 0);
  lua_pushboolean(L, g_cfg_ops.backlight() ? 1 : 0);
  return 1;
}

int l_cfg_backlight_level(lua_State* L) {
  if (g_cfg_ops.backlight_level == nullptr || g_cfg_ops.set_backlight_level == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build keeps no light for the glass");
  }
  if (!lua_isnoneornil(L, 1)) {
    lua_Integer asked = luaL_checkinteger(L, 1);
    const lua_Integer ceiling = (g_led_ops.max_value != nullptr) ? static_cast<lua_Integer>(g_led_ops.max_value()) : 255;
    if (asked < 1) asked = 1;
    if (asked > ceiling) asked = ceiling;
    g_cfg_ops.set_backlight_level(static_cast<uint8_t>(asked));
  }
  lua_pushinteger(L, static_cast<lua_Integer>(g_cfg_ops.backlight_level()));
  return 1;
}

// **`cfg.software_button([on])`**: el botó de sota el vidre (0.190.110, l'amo: *«Afegeix a Settings->Input un Toggle per habilitar-lo o
// deshabilitar-lo "Use software button"»*). Sense argument contesta el que hi ha; amb un, ho escriu i el sistema deixa de fer-ne una tecla —o
// torna a fer-ne— al torn següent, perquè qui la llegeix és el tàctil i no una còpia (docs/30, docs/50).
int l_cfg_software_button(lua_State* L) {
  if (g_cfg_ops.software_button == nullptr || g_cfg_ops.set_software_button == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build keeps no button under the glass");
  }
  if (!lua_isnoneornil(L, 1)) g_cfg_ops.set_software_button(lua_toboolean(L, 1) != 0);
  lua_pushboolean(L, g_cfg_ops.software_button() ? 1 : 0);
  return 1;
}

// **`cfg.dim_when_idle([on])`**: the light that goes out when nobody looks (0.190.118, l'amo: *«Aquest en 1 minut que no es toqui l apantalla el
// backlight s'apaga i es torna a encendre quan toquen el panell»*). Sense argument contesta el que hi ha; amb un, ho escriu, i el minut i el
// tornar a encendre són del nucli i no de la pàgina (docs/30, docs/50).
int l_cfg_dim_when_idle(lua_State* L) {
  if (g_cfg_ops.dim_when_idle == nullptr || g_cfg_ops.set_dim_when_idle == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build keeps no light for the glass");
  }
  if (!lua_isnoneornil(L, 1)) g_cfg_ops.set_dim_when_idle(lua_toboolean(L, 1) != 0);
  lua_pushboolean(L, g_cfg_ops.dim_when_idle() ? 1 : 0);
  return 1;
}

// **`cfg.breathe_while_charging([on])`**: el vidre que respira amb el cable posat (0.190.162, l'amo: *«Si el T5S3 està carregant batería vull el
// efecte "breathe" al backight fins que deixa de carregar»*). Sense argument contesta el que hi ha; amb un, ho escriu, i el respir i l'aturar-se
// són del nucli —el carregador és qui diu si hi ha corrent— i no de la pàgina (docs/30, docs/50).
int l_cfg_breathe_while_charging(lua_State* L) {
  if (g_cfg_ops.breathe_while_charging == nullptr || g_cfg_ops.set_breathe_while_charging == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build keeps no light for the glass");
  }
  if (!lua_isnoneornil(L, 1)) g_cfg_ops.set_breathe_while_charging(lua_toboolean(L, 1) != 0);
  lua_pushboolean(L, g_cfg_ops.breathe_while_charging() ? 1 : 0);
  return 1;
}

// **`cfg.announce_charge([on])`**: la foto del cable (0.190.165, l'amo: *«un toggle a Settings->System "Announce charge" (per defecte true) que
// mostrarà l' splash de "charging" quan es detecti que s'endolla»*). Sense argument contesta el que hi ha; amb un, ho escriu, i el flanc i la
// imatge són del nucli i no de la pàgina (docs/30, docs/50).
int l_cfg_announce_charge(lua_State* L) {
  if (g_cfg_ops.announce_charge == nullptr || g_cfg_ops.set_announce_charge == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build keeps no picture of the charge");
  }
  if (!lua_isnoneornil(L, 1)) g_cfg_ops.set_announce_charge(lua_toboolean(L, 1) != 0);
  lua_pushboolean(L, g_cfg_ops.announce_charge() ? 1 : 0);
  return 1;
}

// **`cfg.touch_adaptive([on])`**: el règim dels escombrats del vidre (0.190.107, l'amo: *«afegeix a Settings->Input un toggle que posi
// "Adaptative touch scan", aquesta opciò activa o desactiva el comportament del panell de baixar la sensibilitat i pujar-la quan hi ha dit.
// Deixant el panell a máxima velocitat mentre estigui el dispositiu despert»*). Sense argument contesta el que hi ha; amb un, ho escriu i ho
// aplica al vidre a l'instant, perquè el commutador es vegi al dit i no al proper boot (docs/30, docs/50).
int l_cfg_touch_adaptive(lua_State* L) {
  if (g_cfg_ops.touch_adaptive == nullptr || g_cfg_ops.set_touch_adaptive == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build keeps no scan of the glass");
  }
  if (!lua_isnoneornil(L, 1)) g_cfg_ops.set_touch_adaptive(lua_toboolean(L, 1) != 0);
  lua_pushboolean(L, g_cfg_ops.touch_adaptive() ? 1 : 0);
  return 1;
}

int l_cfg_lock_after(lua_State* L) {
  if (g_cfg_ops.lock_after_minutes == nullptr || g_cfg_ops.set_lock_after_minutes == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build keeps no lock of the screen");
  }
  if (!lua_isnoneornil(L, 1)) {
    const lua_Integer asked = luaL_checkinteger(L, 1);
    if (!g_cfg_ops.set_lock_after_minutes(static_cast<uint8_t>(asked))) {
      return push_nil_error(L, "E_INVAL", "the screen locks after 5, 10 or 15 minutes");
    }
  }
  lua_pushinteger(L, static_cast<lua_Integer>(g_cfg_ops.lock_after_minutes()));
  return 1;
}

// **`cfg.freq([<mhz>, <on>])`: les velocitats del processador** (0.189.107, l'amo: *«[X] Frequency scaling allowed:
// [X] 80Mhz [X] 160Mhz [X] 240Mhz»*). With no argument it answers **the list of the steps the policy may use**, in
// ascending order; with a step and a boolean it turns that one on or off and answers the list it has now —which is the
// door's rule of every other setting: what it answers is what stands, and a step that would leave the board with none is
// refused with `E_INVAL` and the list untouched (docs/30, docs/50).
int l_cfg_freq(lua_State* L) {
  if (g_cfg_ops.freq_mask == nullptr || g_cfg_ops.set_freq_allowed == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build has no speed to allow");
  }
  if (!lua_isnoneornil(L, 1)) {
    const lua_Integer asked = luaL_checkinteger(L, 1);
    const bool on = lua_toboolean(L, 2) != 0;
    if (!g_cfg_ops.set_freq_allowed(static_cast<uint32_t>(asked), on)) {
      return push_nil_error(L, "E_INVAL", "that is not a step of this processor, or it would leave none");
    }
  }
  const uint8_t mask = g_cfg_ops.freq_mask();
  lua_createtable(L, 3, 0);
  int at = 0;
  static const uint32_t kSteps[3] = {80, 160, 240};
  for (uint8_t i = 0; i < 3; ++i) {
    if ((mask & (1u << i)) == 0) continue;
    lua_pushinteger(L, static_cast<lua_Integer>(kSteps[i]));
    lua_rawseti(L, -2, ++at);
  }
  return 1;
}

// **Les llistes d'un desplegable**: the keyboards this build carries and the pictures of the boot the disk has, as arrays of
// words, so a page that offers a choice offers the ones that exist (docs/30).
int push_cfg_words(lua_State* L, uint8_t (*list)(const char**, uint8_t)) {
  if (list == nullptr) return push_nil_error(L, "E_UNSUPPORTED", "this build keeps no list like that");
  const uint8_t total = list(nullptr, 0);
  const char* words[32];
  const uint8_t copied = list(words, static_cast<uint8_t>((total > 32) ? 32 : total));
  lua_createtable(L, copied, 0);
  for (uint8_t i = 0; i < copied; ++i) {
    lua_pushstring(L, words[i]);
    lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
  }
  return 1;
}

int l_cfg_keyboards(lua_State* L) { return push_cfg_words(L, g_cfg_ops.keyboards); }
int l_cfg_boots(lua_State* L) { return push_cfg_words(L, g_cfg_ops.boots); }

int l_sys_shell(lua_State* L) {
  if (g_shell_ops.open == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this system has no shell to give");
  }
  char why[96] = {0};
  const uint8_t id = g_shell_ops.open(why, sizeof(why));
  if (id == 0) return push_nil_error(L, "E_BUSY", (why[0] != '\0') ? why : "no session");
  return push_shell(L, id);
}

void open_shell_library(lua_State* L) {
  luaL_newmetatable(L, kShellMeta);
  lua_pushcfunction(L, &l_shell_index);
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, &l_shell_gc);
  lua_setfield(L, -2, "__gc");
  lua_pushcfunction(L, &l_shell_tostring);
  lua_setfield(L, -2, "__tostring");
  lua_pushcfunction(L, &l_shell_close);
  lua_setfield(L, -2, "close");
  lua_pushcfunction(L, &l_shell_dropped);
  lua_setfield(L, -2, "dropped");
  lua_pushcfunction(L, &l_shell_id);
  lua_setfield(L, -2, "id");
  lua_pop(L, 1);
}

// Nothing here reaches the outside world: no files, no C modules, no way to load
// a precompiled chunk (docs/20).
// **La porta dels tres tocs viu amb el seu lector i no amb el `admin`** (0.190.254): es declara aquí perquè la taula de `touch` —que
// es fa en aquesta funció— la crida abans que el cos sigui escrit més avall, i una funció que es declara després del seu lector no
// existeix per a ell.
int l_touch_triple_tap_close(lua_State* L);

// ---------------------------------------------------------------------------
// gps: el cel, i el que un receptor en diu (0.192.63)
// ---------------------------------------------------------------------------

// **`gps.caps()`**: what this board's receiver is —the name of the module, its bauds and whether the rail is on—, or
// `E_UNSUPPORTED` on a board that does not look at the sky (docs/30, docs/50).
int l_gps_caps(lua_State* L) {
  if (g_gps_ops.caps == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board does not look at the sky");
  }
  hal::GpsCaps caps;
  if (!g_gps_ops.caps(caps)) return push_nil_error(L, "E_UNSUPPORTED", "this board does not look at the sky");
  lua_createtable(L, 0, 4);
  lua_pushstring(L, caps.chip);
  lua_setfield(L, -2, "chip");
  lua_pushinteger(L, static_cast<lua_Integer>(caps.baud));
  lua_setfield(L, -2, "baud");
  lua_pushboolean(L, caps.powered ? 1 : 0);
  lua_setfield(L, -2, "powered");
  lua_pushstring(L, (g_gps_ops.last_error != nullptr) ? g_gps_ops.last_error() : "");
  lua_setfield(L, -2, "last_error");
  return 1;
}

// **`gps.on()` / `gps.off()`**: el rail i el cable, i el que ja hi havia es llegeix a la crida següent (docs/30).
int l_gps_on(lua_State* L) {
  if (g_gps_ops.on == nullptr) return push_nil_error(L, "E_UNSUPPORTED", "this board does not look at the sky");
  if (!g_gps_ops.on()) return push_nil_error(L, "E_IO", "the receiver does not answer");
  lua_pushboolean(L, 1);
  return 1;
}

int l_gps_off(lua_State* L) {
  if (g_gps_ops.off == nullptr) return push_nil_error(L, "E_UNSUPPORTED", "this board does not look at the sky");
  lua_pushboolean(L, g_gps_ops.off() ? 1 : 0);
  return 1;
}

// **`gps.info()`: tot el que el receptor diu, en una taula** (0.192.63, l'amo: *«Mostrar tota l'info que aquest MIA-M10Q GNSS ens
// pugui donar»*): the position in degrees —and in degrees times ten million, which is what the receiver gave and what a page does
// not have to lose to a float—, the height, the movement, the dilution, the time of the sky and the health of the wire. `fix` is
// false when there is no position: **una taula amb zeros no és el mateix que no tenir posició**, i una pàgina que les confongui
// ensenya el punt zero de l'Atlàntic (docs/30, docs/50).
int l_gps_info(lua_State* L) {
  if (g_gps_ops.fix == nullptr) return push_nil_error(L, "E_UNSUPPORTED", "this board does not look at the sky");
  hal::GpsFix fix;
  const bool has = g_gps_ops.fix(fix);
  lua_createtable(L, 0, 22);
  lua_pushboolean(L, has ? 1 : 0);
  lua_setfield(L, -2, "fix");
  lua_pushinteger(L, static_cast<lua_Integer>(fix.quality));
  lua_setfield(L, -2, "quality");
  lua_pushnumber(L, static_cast<lua_Number>(fix.lat_e7) / 10000000.0);
  lua_setfield(L, -2, "lat");
  lua_pushnumber(L, static_cast<lua_Number>(fix.lon_e7) / 10000000.0);
  lua_setfield(L, -2, "lon");
  lua_pushinteger(L, static_cast<lua_Integer>(fix.lat_e7));
  lua_setfield(L, -2, "lat_e7");
  lua_pushinteger(L, static_cast<lua_Integer>(fix.lon_e7));
  lua_setfield(L, -2, "lon_e7");
  lua_pushnumber(L, static_cast<lua_Number>(fix.altitude_cm) / 100.0);
  lua_setfield(L, -2, "altitude");
  lua_pushnumber(L, static_cast<lua_Number>(fix.geoid_cm) / 100.0);
  lua_setfield(L, -2, "geoid");
  lua_pushnumber(L, static_cast<lua_Number>(fix.speed_cm_s) / 100.0);
  lua_setfield(L, -2, "speed");
  // **El rumb en dècimes de grau i la seva paraula**: 45,0° és un número i `NE` és el que una persona llegeix d'un cop (docs/30).
  lua_pushnumber(L, static_cast<lua_Number>(fix.course_deg10) / 10.0);
  lua_setfield(L, -2, "course");
  static const char* const kPoints[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  int32_t point = ((fix.course_deg10 / 10) + 22) / 45;
  if (point < 0) point = 0;
  if (point > 7) point = 7;
  lua_pushstring(L, kPoints[point]);
  lua_setfield(L, -2, "course_name");
  lua_pushinteger(L, static_cast<lua_Integer>(fix.satellites_used));
  lua_setfield(L, -2, "satellites_used");
  lua_pushinteger(L, static_cast<lua_Integer>(fix.satellites_seen));
  lua_setfield(L, -2, "satellites_seen");
  lua_pushnumber(L, static_cast<lua_Number>(fix.hdop_x10) / 10.0);
  lua_setfield(L, -2, "hdop");
  lua_pushnumber(L, static_cast<lua_Number>(fix.vdop_x10) / 10.0);
  lua_setfield(L, -2, "vdop");
  lua_pushnumber(L, static_cast<lua_Number>(fix.pdop_x10) / 10.0);
  lua_setfield(L, -2, "pdop");
  // **L'hora del cel, que és UTC i no la de la persona**: la pàgina l'ha de poder ensenyar tal com arriba (docs/30, docs/60).
  lua_createtable(L, 0, 8);
  lua_pushboolean(L, fix.time.valid ? 1 : 0);
  lua_setfield(L, -2, "valid");
  lua_pushinteger(L, static_cast<lua_Integer>(fix.time.year));
  lua_setfield(L, -2, "year");
  lua_pushinteger(L, static_cast<lua_Integer>(fix.time.month));
  lua_setfield(L, -2, "month");
  lua_pushinteger(L, static_cast<lua_Integer>(fix.time.day));
  lua_setfield(L, -2, "day");
  lua_pushinteger(L, static_cast<lua_Integer>(fix.time.hour));
  lua_setfield(L, -2, "hour");
  lua_pushinteger(L, static_cast<lua_Integer>(fix.time.minute));
  lua_setfield(L, -2, "minute");
  lua_pushinteger(L, static_cast<lua_Integer>(fix.time.second));
  lua_setfield(L, -2, "second");
  lua_pushinteger(L, static_cast<lua_Integer>(fix.time.hundredths));
  lua_setfield(L, -2, "hundredths");
  lua_setfield(L, -2, "time");
  lua_pushinteger(L, static_cast<lua_Integer>(fix.sentences));
  lua_setfield(L, -2, "sentences");
  lua_pushinteger(L, static_cast<lua_Integer>(fix.bad_sentences));
  lua_setfield(L, -2, "bad_sentences");
  lua_pushboolean(L, (g_gps_ops.powered != nullptr && g_gps_ops.powered()) ? 1 : 0);
  lua_setfield(L, -2, "powered");
  // **I el que el sistema ha cregut del cel**: quan va ser l'última vegada i quant es va moure el rellotge, que és el que diu si la
  // sincronització ha fet res (docs/30, docs/60).
  if (g_gps_ops.last_sync != nullptr) {
    const int64_t ms = g_gps_ops.last_sync();
    if (ms >= 0) {
      lua_pushinteger(L, static_cast<lua_Integer>(ms));
      lua_setfield(L, -2, "synced_ms");
      lua_pushinteger(L, static_cast<lua_Integer>((g_gps_ops.last_shift != nullptr) ? g_gps_ops.last_shift() : 0));
      lua_setfield(L, -2, "shift_ms");
    }
  }
  return 1;
}

// **`gps.sky()`: els satèl·lits de l'últim GSV**, amb l'elevació, l'azimut i la força de cada un, que és el que es dibuixa en un
// mapa del cel (docs/30, docs/50).
int l_gps_sky(lua_State* L) {
  if (g_gps_ops.satellites == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board does not look at the sky");
  }
  uint8_t count = 0;
  const hal::GpsSatellite* sky = g_gps_ops.satellites(count);
  lua_createtable(L, static_cast<int>(count), 0);
  for (uint8_t i = 0; sky != nullptr && i < count; ++i) {
    lua_createtable(L, 0, 5);
    lua_pushinteger(L, static_cast<lua_Integer>(sky[i].id));
    lua_setfield(L, -2, "id");
    lua_pushinteger(L, static_cast<lua_Integer>(sky[i].elevation));
    lua_setfield(L, -2, "elevation");
    lua_pushinteger(L, static_cast<lua_Integer>(sky[i].azimuth));
    lua_setfield(L, -2, "azimuth");
    lua_pushinteger(L, static_cast<lua_Integer>(sky[i].snr));
    lua_setfield(L, -2, "snr");
    lua_pushboolean(L, sky[i].used ? 1 : 0);
    lua_setfield(L, -2, "used");
    lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
  }
  return 1;
}


void seal_globals(lua_State* L) {
  lua_pushnil(L);
  lua_setglobal(L, "dofile");
  lua_pushnil(L);
  lua_setglobal(L, "loadfile");
  lua_pushnil(L);
  lua_setglobal(L, "load");

  // **`require` is a door with a policy and not a loader** (0.148.0, docs/46 D6): it reads a module of the app's own
  // folder and nothing else, and `package` is a table with `loaded` in it and no search path at all. A module is
  // named by the app and read by the kernel; an app still cannot open a file and call it.
  lua_createtable(L, 0, 1);
  lua_newtable(L);
  lua_setfield(L, -2, "loaded");
  lua_setglobal(L, "package");
  lua_pushcfunction(L, &l_require);
  lua_setglobal(L, "require");

  lua_pushcfunction(L, &l_print);
  lua_setglobal(L, "print");

  // ---- a session of the shell (0.159.0, docs/30) ----
  //
  // **An app can be a shell's client**: `sys.shell()` asks the system for a session of its own shell and what comes
  // back is a handle with the three pipes of one —`stdin`, `stdout` and `stderr`—, so a line written on the first
  // is a command the system runs and what it prints comes out of the other two. The shell is the system's and not
  // the wire's: no console, no prompt and no echo in the middle (docs/30).
  lua_createtable(L, 0, 12);
  // Who is running this: plain values, so an app can test them without a call.
  lua_pushstring(L, "lunokOS");
  lua_setfield(L, -2, "name");
  lua_pushstring(L, g_boot.version);
  lua_setfield(L, -2, "version");
  lua_pushstring(L, g_boot.board);
  lua_setfield(L, -2, "board");
  lua_pushboolean(L, g_boot.emulator ? 1 : 0);
  lua_setfield(L, -2, "emulator");
  lua_pushstring(L, LUA_RELEASE);
  lua_setfield(L, -2, "lua");
  set_function(L, "heap", l_sys_heap);
  set_function(L, "time_ms", l_sys_time_ms);
  set_function(L, "idle_ms", l_sys_idle_ms);
  set_function(L, "turn", l_sys_turn);
  set_function(L, "gc", l_sys_gc);
  set_function(L, "caps", l_sys_caps);
  set_function(L, "delay", l_sys_delay);
  set_function(L, "shell", l_sys_shell);
  set_function(L, "commands", l_sys_commands);
  set_function(L, "session", l_sys_session);
  set_function(L, "session_destroy", l_sys_session_destroy);
  set_function(L, "settings", l_sys_settings);
  set_function(L, "stats", l_sys_stats);
  set_function(L, "reboot", l_sys_reboot);
  set_function(L, "deepsleep", l_sys_deepsleep);
  lua_setglobal(L, "sys");

  // cfg: **els ajustos del sistema**, la mateixa porta que la comanda del prompt (0.189.106). Llegir és cridar-la sense
  // argument i escriure és cridar-la amb el valor, i la resposta és sempre el que queda dempeus (docs/30, docs/70).
  lua_createtable(L, 0, 8);
  set_function(L, "keyboard", l_cfg_keyboard);
  set_function(L, "boot", l_cfg_boot);
  set_function(L, "home", l_cfg_home);
  set_function(L, "sort", l_cfg_sort);
  set_function(L, "turn", l_cfg_turn);
  set_function(L, "cursor", l_cfg_cursor);
  set_function(L, "freq", l_cfg_freq);
  set_function(L, "settings", l_cfg_settings_app);
  set_function(L, "lock", l_cfg_lock);
  set_function(L, "gps_sync", l_cfg_gps_sync);
  set_function(L, "lock_on", l_cfg_lock_on);
  set_function(L, "lock_after", l_cfg_lock_after);
  set_function(L, "sleep", l_cfg_sleep);
  set_function(L, "touch_wake", l_cfg_touch_wake);
  set_function(L, "uart_wake", l_cfg_uart_wake);
  set_function(L, "nap", l_cfg_nap);
  set_function(L, "bt", l_cfg_bt);
  set_function(L, "loglevel", l_cfg_loglevel);
  set_function(L, "touch_adaptive", l_cfg_touch_adaptive);
  set_function(L, "software_button", l_cfg_software_button);
  set_function(L, "dim_when_idle", l_cfg_dim_when_idle);
  set_function(L, "breathe_while_charging", l_cfg_breathe_while_charging);
  set_function(L, "announce_charge", l_cfg_announce_charge);
  set_function(L, "backlight", l_cfg_backlight);
  set_function(L, "backlight_level", l_cfg_backlight_level);
  set_function(L, "idle", l_cfg_idle);
  set_function(L, "keyboards", l_cfg_keyboards);
  set_function(L, "boots", l_cfg_boots);
  lua_setglobal(L, "cfg");

  // **`bt`: el teclat de Bluetooth, que és una porta** (0.190.180). It is a table of its own and not a corner of `cfg` perquè no és un
  // ajust: és una cosa que una pàgina fa —escriure en un altre aparell— i el commutador que l'encén viu a `cfg.bt`, que és on viuen tots
  // (docs/30, docs/50).
  lua_createtable(L, 0, 3);
  set_function(L, "available", l_bt_available);
  set_function(L, "connected", l_bt_connected);
  set_function(L, "ready", l_bt_ready);
  set_function(L, "type", l_bt_type);
  set_function(L, "name", l_bt_name);
  set_function(L, "move", l_bt_move);
  set_function(L, "click", l_bt_click);
  lua_setglobal(L, "bt");

  // time: the wall clock. It can jump when somebody sets it; sys.time_ms() is
  // the one that never goes back (docs/60).
  lua_createtable(L, 0, 6);
  set_function(L, "now", l_time_now);
  set_function(L, "now_ms", l_time_now_ms);
  set_function(L, "caps", l_time_caps);
  set_function(L, "set", l_time_set);
  set_function(L, "utc", l_time_utc);
  set_function(L, "date", l_time_date);
  // **`time.offset()` i `time.epoch(...)`** (0.189.130): the effective offset and the inverse of `date`, so no page writes the
  // arithmetic of the hour again (docs/30, docs/60).
  set_function(L, "offset", l_time_offset);
  set_function(L, "epoch", l_time_epoch);
  set_function(L, "tz", l_time_tz);
  // **L'hora d'estiu** (0.182.0): the setting the Time zone page ticks, kept in the store and applied to `time.date` (docs/30).
  set_function(L, "dst", l_time_dst);
  set_function(L, "leap", l_time_leap);
  set_function(L, "week", l_time_week);
  lua_setglobal(L, "time");

  // alarm: what the system keeps for the app when the app is not running.
  lua_createtable(L, 0, 5);
  set_function(L, "at", l_alarm_at);
  set_function(L, "after", l_alarm_after);
  set_function(L, "cancel", l_alarm_cancel);
  set_function(L, "list", l_alarm_list);
  set_function(L, "caps", l_alarm_caps);
  lua_setglobal(L, "alarm");

  // **tick: el rellotge d'una app mentre la placa és desperta** (0.192.60): the same queue as the alarms and none of their
  // promises —it never wakes the board, it is not written down, and by default it does not open a window— (docs/30, docs/60).
  lua_createtable(L, 0, 4);
  set_function(L, "every", l_tick_every);
  set_function(L, "cancel", l_tick_cancel);
  set_function(L, "list", l_tick_list);
  set_function(L, "caps", l_tick_caps);
  lua_setglobal(L, "tick");

  // qr: any QR this build can draw, for anything an app wants to encode.
  lua_createtable(L, 0, 2);
  set_function(L, "encode", l_qr_encode);
  set_function(L, "caps", l_qr_caps);
  lua_setglobal(L, "qr");

  // **`epub`, `ui`, `canvas`, `keyboard` and `font` are not there on a board with no glass** (0.179.9): a table that could
  // only answer "this build carries no reader" is a promise the build does not keep, and the owner asked for the functions
  // themselves to be absent. `sys.caps` still says what a board has, and `nil` here is not a failure an app has to handle:
  // an app that draws needs a board whose manifest names one with a screen (docs/30, docs/40, docs/50).
  // epub: a book of the disk, a chapter of it and a page of the chapter painted in
  // a canvas of the app (docs/46, A5 and B3).
  lua_createtable(L, 0, 12);
  set_function(L, "open", l_epub_open);
  set_function(L, "books", l_epub_books);
  set_function(L, "close", l_epub_close);
  set_function(L, "entries", l_epub_entries);
  set_function(L, "name", l_epub_name);
  set_function(L, "first_chapter", l_epub_first_chapter);
  set_function(L, "chapter", l_epub_chapter);
  set_function(L, "read", l_epub_read);
  set_function(L, "picture", l_epub_picture);
  set_function(L, "thumbnail", l_epub_thumbnail);
  set_function(L, "font", l_epub_font);
  set_function(L, "page", l_epub_page);
  lua_setglobal(L, "epub");

  // json: reading fields out of a text, with the same helpers the manifests use.
  lua_createtable(L, 0, 2);
  set_function(L, "get_string", l_json_get_string);
  set_function(L, "get_int", l_json_get_int);
  lua_setglobal(L, "json");

  // rle: the run-length of the system, the same way `zip` is there for a container (docs/30).
  lua_createtable(L, 0, 2);
  set_function(L, "pack", l_rle_pack);
  set_function(L, "unpack", l_rle_unpack);
  lua_setglobal(L, "rle");

  // net: starting things that take a while, and waiting for them as promises.
  // There is no blocking call here on purpose (docs/30).
  lua_createtable(L, 0, 12);
  set_function(L, "connect", l_net_connect);
  set_function(L, "connect_stored", l_net_connect_stored);
  set_function(L, "provision", l_net_provision);
  set_function(L, "provisioned", l_net_provisioned);
  set_function(L, "add", l_net_add);
  set_function(L, "scan", l_net_scan);
  set_function(L, "stored", l_net_stored);
  set_function(L, "scan_record", l_net_scan_record);
  set_function(L, "forget", l_net_forget);
  set_function(L, "ntp", l_net_ntp);
  set_function(L, "disconnect", l_net_disconnect);
  set_function(L, "status", l_net_status);
  set_function(L, "caps", l_net_caps);
  lua_setglobal(L, "net");

  // http: a page. One call, a promise, and the answer is the status and the body —or the file it went
  // to when it was too big for the heap (docs/30).
  lua_createtable(L, 0, 2);
  set_function(L, "get", l_http_get);
  set_function(L, "download", l_http_download);
  lua_setglobal(L, "http");

  // led: the light, while the app is in front. button: the one thing a person can
  // press (docs/30).
  lua_createtable(L, 0, 3);
  set_function(L, "caps", l_led_caps);
  set_function(L, "set", l_led_set);
  set_function(L, "pixel", l_led_pixel);
  set_function(L, "off", l_led_off);
  // The effects: what the light says while the app does its own work, and the four
  // moments of the system, which the app may redefine (docs/30).
  set_function(L, "play", l_led_play);
  set_function(L, "stop", l_led_stop);
  set_function(L, "effect", l_led_effect);
  set_function(L, "system", l_led_system);
  lua_setglobal(L, "led");

  // **serial: el port de la placa cedit a qui el vol** (0.189.20): the wire that is the way in and can be a channel instead.
  // While it is ceded the system writes nothing to it and the app's own words still go there (docs/30, docs/50).
  lua_createtable(L, 0, 3);
  set_function(L, "claim", l_serial_claim);
  set_function(L, "release", l_serial_release);
  set_function(L, "claimed", l_serial_claimed);
  lua_setglobal(L, "serial");

  // **crypto: l'aritmètica d'un secret** (0.190.258): the entropy, the derivation of a key, the seal of a block and the
  // open of one. Four calls, because a file of passwords needs exactly four things and everything else is the app's
  // (docs/30, docs/45).
  lua_createtable(L, 0, 4);
  set_function(L, "random", l_crypto_random);
  set_function(L, "key", l_crypto_key);
  set_function(L, "seal", l_crypto_seal);
  set_function(L, "open", l_crypto_open);
  lua_setglobal(L, "crypto");

  lua_newtable(L);
  set_function(L, "read", l_imu_read);
  lua_setglobal(L, "imu");

  // The air: temperature and humidity, from the chip that is on some boards and not on others
  // (docs/30, docs/50).
  lua_newtable(L);
  set_function(L, "read", l_env_read);
  lua_setglobal(L, "env");

  // **La ràdio que només escolta** (0.192.49): només on el xip hi és, o sigui que una placa sense LoRa no té cap taula `lora`
  // i una app ho pot preguntar abans de dibuixar un espectre (docs/30, docs/50).
  if (g_lora_ops.caps != nullptr) {
    lua_newtable(L);
    set_function(L, "caps", l_lora_caps);
    set_function(L, "begin", l_lora_begin);
    set_function(L, "frequency", l_lora_frequency);
    set_function(L, "reception", l_lora_reception);
    set_function(L, "listen", l_lora_listen);
    set_function(L, "sniff", l_lora_sniff);
    set_function(L, "rssi", l_lora_rssi);
    set_function(L, "poll", l_lora_poll);
    set_function(L, "idle", l_lora_idle);
    set_function(L, "sleep", l_lora_sleep);
    lua_setglobal(L, "lora");
  }

  // **I el cel, quan la placa el mira** (0.192.63): la taula només hi és amb un receptor, o sigui que una app que comprova
  // `sys.caps().gps` i després crida `gps.on()` no es pot trobar mai una funció que contesta una mentida (docs/30).
  if (g_gps_ops.caps != nullptr) {
    lua_newtable(L);
    set_function(L, "caps", l_gps_caps);
    set_function(L, "on", l_gps_on);
    set_function(L, "off", l_gps_off);
    set_function(L, "info", l_gps_info);
    set_function(L, "sky", l_gps_sky);
    lua_setglobal(L, "gps");
  }

  lua_createtable(L, 0, 1);
  set_function(L, "wait", l_button_wait);
  set_function(L, "count", l_button_count);
  set_function(L, "software", l_button_software);
  set_function(L, "down", l_button_down);
  set_function(L, "held", l_button_held);
  set_function(L, "info", l_button_info);
  set_function(L, "list", l_button_list);
  // **La cua** (0.180.0): an app can be one of the listeners of the keys, or it can take them for itself (docs/30).
  set_function(L, "subscribe", l_button_subscribe);
  set_function(L, "unsubscribe", l_button_unsubscribe);
  set_function(L, "handle", l_button_mode);
  lua_setglobal(L, "button");

  // touch: the glass, read in a loop. caps says how big it is, read gives the fingers of the last
  // turn of the kernel and gesture gives the last shape the kernel made out of them (docs/30).
  lua_createtable(L, 0, 4);
  set_function(L, "caps", l_touch_caps);
  set_function(L, "read", l_touch_read);
  set_function(L, "gesture", l_touch_gesture);
  // **I de qui és el gest dels tres tocs** (0.190.254): un joc llegeix cada toc, o sigui que el tercer no pot ser una ordre del
  // sistema que li tanca la partida. `touch.triple_tap_close(false)` els deixa a la cua de la pàgina i torna l'estat nou
  // (docs/30, docs/46).
  set_function(L, "triple_tap_close", l_touch_triple_tap_close);
  lua_setglobal(L, "touch");

  // ui: the view of the app. Nothing is drawn by the system: clear and rect paint on the app's own
  // canvas, present composes it onto the board's surface and says how good the paint has to look, and
  // idle says whether the glass has finished (docs/30, docs/50). **This is the page and it goes with `LUNOK_GLASS`**, the
  // same line that took its bindings out of the build (0.179.9).
  lua_createtable(L, 0, 8);
  set_function(L, "caps", l_ui_caps);
  set_function(L, "clear", l_ui_clear);
  set_function(L, "rect", l_ui_rect);
  set_function(L, "image", l_ui_image);
  set_function(L, "image_info", l_ui_image_info);
  set_function(L, "pixel", l_ui_pixel);
  set_function(L, "invert", l_ui_invert);
  set_function(L, "focus", l_ui_focus);
  set_function(L, "present", l_ui_present);
  // **I el present d'un arrencament, sobre la imatge que el manifest demana** (0.189.38, docs/30).
  set_function(L, "present_over", l_ui_present_over);
  // **I aguantar la pàgina perquè el vidre es quedi amb la imatge fins que sigui tota** (0.189.42, docs/30).
  set_function(L, "hold_page", l_ui_hold_page);
  set_function(L, "idle", l_ui_idle);
  set_function(L, "resize", l_ui_resize);
  set_function(L, "damage", l_ui_damage);
  set_function(L, "title", l_ui_title);
  set_function(L, "rotation", l_ui_rotation);
  set_function(L, "full_screen", l_ui_full_screen);
  // **Les imatges dels moments** (0.183.0): `ui.splashes()` is the list and `ui.splash(word)` the picture (docs/30).
  set_function(L, "splashes", l_ui_splashes);
  set_function(L, "splash", l_ui_splash);
  set_function(L, "canvas", l_ui_canvas);
  set_function(L, "control", l_ui_control);
  set_function(L, "label", l_ui_label);
  set_function(L, "button", l_ui_button);
  set_function(L, "checkbox", l_ui_checkbox);
  set_function(L, "progress", l_ui_progress);
  set_function(L, "slider", l_ui_slider);
  set_function(L, "row", l_ui_row);
  set_function(L, "panel", l_ui_panel);
  set_function(L, "tabs", l_ui_tabs);
  set_function(L, "dots", l_ui_dots);
  set_function(L, "list", l_ui_list);
  set_function(L, "list_entry", l_ui_list_entry);
  set_function(L, "grid_entry", l_ui_grid_entry);
  set_function(L, "textbox", l_ui_textbox);
  set_function(L, "dialog", l_ui_dialog);
  set_function(L, "menu", l_ui_menu);
  set_function(L, "picker", l_ui_picker);
  set_function(L, "menu_bar", l_ui_menu_bar);
  set_function(L, "textbox_multiline", l_ui_textbox_multiline);
  set_function(L, "selector", l_ui_selector);
  set_function(L, "notification_bar", l_ui_notification_bar);
  set_function(L, "icon_grid", l_ui_icon_grid);
  set_function(L, "sparkline", l_ui_sparkline);
  set_function(L, "chart", l_ui_chart);
  set_function(L, "pie", l_ui_pie);
  set_function(L, "month", l_ui_month);
  set_function(L, "keyboard", l_ui_keyboard);
  set_function(L, "root", l_ui_root);
  set_function(L, "frame", l_ui_frame);
  set_function(L, "line", l_ui_line);
  set_function(L, "circle", l_ui_circle);
  set_function(L, "disc", l_ui_disc);
  set_function(L, "text", l_ui_text);
  set_function(L, "text_width", l_ui_text_width);
  set_function(L, "line_height", l_ui_line_height);
  set_function(L, "load_font", l_ui_load_font);
  set_function(L, "font", l_ui_font);
  lua_setglobal(L, "ui");
  open_canvas_library(L);
  open_keyboard_library(L);
  open_font_library(L);

  // power: the energy of the board and the sleep that ends the app (docs/30,
  // docs/90).
  lua_createtable(L, 0, 4);
  set_function(L, "caps", l_power_caps);
  set_function(L, "battery", l_power_battery);
  set_function(L, "charger", l_power_charger);
  set_function(L, "sleep", l_power_sleep);
  set_function(L, "keep_awake", l_power_keep_awake);
  set_function(L, "cpu", l_power_cpu);
  set_function(L, "cpu_auto", l_power_cpu_auto);
  set_function(L, "load", l_power_load);
  lua_setglobal(L, "power");

  // **I els sensors, quan els tres àmbits ja existeixen** (0.190.158): `env`, `power` i `imu` són taules globals i les de cada
  // magnitud s'hi pengen aquí, amb l'agregador `sensors` al costat (docs/30, docs/50).
  install_sensors(L);

  // fs: reading, writing and asking. A call that is not here is not there
  // either, so nothing is promised that the kernel does not do (docs/30).
  open_file_library(L);
  // **The memory of a session** (0.179.0): `sys.session(key, bytes)` answers a block the kernel keeps for this app until
  // the board goes off or the memory runs short (docs/30, docs/75).
  open_session_library(L);
  lua_createtable(L, 0, 14);
  set_function(L, "open", l_fs_open);
  set_function(L, "list", l_fs_list);
  set_function(L, "stat", l_fs_stat);
  set_function(L, "read", l_fs_read);
  set_function(L, "read_at", l_fs_read_at);
  set_function(L, "write", l_fs_write);
  set_function(L, "mkdir", l_fs_mkdir);
  set_function(L, "remove", l_fs_remove);
  set_function(L, "format", l_fs_format);
  set_function(L, "rename", l_fs_rename);
  set_function(L, "data", l_fs_data);
  set_function(L, "mounts", l_fs_mounts);
  set_function(L, "space", l_fs_space);
  set_function(L, "thumbnail", l_fs_thumbnail);
  set_function(L, "cfg", l_fs_cfg);
  lua_setglobal(L, "fs");

  lua_createtable(L, 0, 3);
  set_function(L, "print", l_print);
  set_function(L, "write", l_console_write);
  set_function(L, "read_line", l_console_read_line);
  set_function(L, "read_byte", l_console_read_byte);
  set_function(L, "raw", l_console_raw);
  set_function(L, "is_foreground", l_console_is_foreground);
  lua_setglobal(L, "console");



  // term: the size of a page of text, and nothing that escapes (docs/30).
  lua_createtable(L, 0, 2);
  set_function(L, "width", l_term_width);
  set_function(L, "height", l_term_height);
  lua_setglobal(L, "term");

  // app: the identity of the app and the file it was asked to open (docs/30).
  lua_createtable(L, 0, 3);
  lua_pushstring(L, g_boot.app_id);
  lua_setfield(L, -2, "id");
  // **`app.args` hi és quan l'app té alguna cosa a llegir**: a file of the prompt, a session that comes back **or the switches a
  // wish carried** (0.189.161) —the launcher's `app.run('appinfo', '-d battery')` arrives here with no path at all, and a table
  // that did not exist for it was a page that opened on the list instead of on the app that was asked for (docs/30, docs/40).
  if (g_boot.args_path != nullptr || g_boot.args_restore || (g_boot.args_flags != nullptr && g_boot.args_flags[0] != '\0')) {
    lua_createtable(L, 0, 5);
    if (g_boot.args_path != nullptr) {
      lua_pushstring(L, g_boot.args_path);
      lua_setfield(L, -2, "path");
      lua_pushstring(L, (g_boot.args_ext != nullptr) ? g_boot.args_ext : "");
      lua_setfield(L, -2, "ext");
      lua_pushstring(L, (g_boot.args_source != nullptr) ? g_boot.args_source : "");
      lua_setfield(L, -2, "source");
    }
    // **The app's own switches, when it has any** (0.120.0): `opensArgs` of the manifest goes to the app as it was
    // written —`-ro`—, and the system does not split it, read it or act on it. A manifest that names none leaves the
    // field out instead of saying an empty string, because an empty switch is not a switch (docs/30, docs/40).
    if (g_boot.args_flags != nullptr && g_boot.args_flags[0] != '\0') {
      lua_pushstring(L, g_boot.args_flags);
      lua_setfield(L, -2, "flags");
    }
    // True when the system is bringing this app back from a sleep: the app reads
    // what it saved instead of starting from nothing (docs/30, docs/90).
    lua_pushboolean(L, g_boot.args_restore ? 1 : 0);
    lua_setfield(L, -2, "restore");
    lua_setfield(L, -2, "args");
  } else {
    lua_pushnil(L);
    lua_setfield(L, -2, "args");
  }
  set_function(L, "exit", l_app_exit);
  set_function(L, "info", l_app_info);
  set_function(L, "run", l_app_run);
  set_function(L, "view", l_app_view);
  set_function(L, "view_push", l_app_view_push);
  set_function(L, "view_pop", l_app_view_pop);
  lua_setglobal(L, "app");

  // apps: **the door of the apps** (0.153.0), which is what a launcher draws (docs/30, docs/40).
  lua_createtable(L, 0, 3);
  set_function(L, "list", l_apps_list);
  set_function(L, "info", l_apps_info);
  set_function(L, "opener", l_apps_opener);
  lua_setglobal(L, "apps");
}

// **L'últim error, copiat** (0.189.162): the text the console gets, kept for the page the system paints when an app stops. The
// text is one and the errors are one at a time —the machine that said it is destroyed right after—, and it is cleared at every
// start so a page of an error never shows the mistake of the app before (docs/30, docs/50).
//
// **I el text és demanat, no estàtic** (0.190.8, l'amo: *«has fet moltes parts estàtiques, llistes i limitacions enlloc d'usar
// psram… VULL QUE LA MEMORIA SIGUI EL MÉS DINÀMICA POSSIBLE A TOT ARREU»*): the 512 bytes this was born with were 512 bytes of
// `dram0_0_seg` —the Paper said so with `overflowed by 128 bytes` when it was 640— and the buffer is now a block that is asked
// for **the first time an app fails** and grows while a message and a traceback do not fit, in the extra memory when the board
// has one and in the heap when it does not (`IExtraMemory::allocate`, docs/75). Between two errors the block is given back, o
// sigui que una placa que no ha fallat no paga res (docs/30, docs/75).
char* g_last_error = nullptr;
size_t g_last_error_len = 0;
size_t g_last_error_cap = 0;

// **El sostre d'un traceback** (0.190.8): a page of an error shows a dozen lines and the ones that matter are the first; a
// message that asks for more than this is cut, which is what a fixed buffer did to every message (docs/30, docs/50).
const size_t kLastErrorCap = 2048;

bool room_for_the_error(size_t need) {
  if (need + 1 <= g_last_error_cap) return true;
  size_t cap = (g_last_error_cap == 0) ? 128 : g_last_error_cap;
  while (cap < need + 1 && cap < kLastErrorCap) cap *= 2;
  if (cap > kLastErrorCap) cap = kLastErrorCap;
  if (g_last_error != nullptr) {
    // A block that is already here does not shrink for an error: a resize that fails leaves it as it was, and the caller cuts the text
    // to what fits (docs/75).
    void* moved = pal::extra_memory().resize(g_last_error, cap, /*prefer_extra=*/true);
    if (moved == nullptr) return false;
    g_last_error = static_cast<char*>(moved);
    g_last_error_cap = cap;
    return need + 1 <= g_last_error_cap;
  }
  // **I la primera petició, cada cop més petita** (0.190.14): the error that matters most is the one that says the board has no memory,
  // and that is exactly the moment when a hundred and twenty-eight bytes may be too many. Asking for half —down to thirty-two— is the
  // difference between a page that says nothing and a page that says the first line of the why (docs/30, docs/75).
  for (;;) {
    void* block = pal::extra_memory().allocate(cap, /*prefer_extra=*/true);
    if (block != nullptr) {
      g_last_error = static_cast<char*>(block);
      g_last_error_cap = cap;
      return need + 1 <= g_last_error_cap;
    }
    if (cap <= 32) return false;
    cap /= 2;
  }
}

void note_the_error(const char* text, size_t len) {
  if (text == nullptr || len == 0) return;
  const bool needs_a_break = g_last_error_len > 0 && g_last_error[g_last_error_len - 1] != '\n';
  const size_t asked = g_last_error_len + (needs_a_break ? 1 : 0) + len;
  if (!room_for_the_error(asked)) {
    // The block is smaller than what the error wants: the message is cut where the memory ends.
    if (g_last_error_cap == 0) return;
    const size_t fits = g_last_error_cap - 1 - g_last_error_len - (needs_a_break ? 1 : 0);
    if (fits == 0) return;
    len = (len > fits) ? fits : len;
  } else if (g_last_error_len == 0 && g_last_error != nullptr) {
    g_last_error[0] = '\0';
  }
  if (needs_a_break) g_last_error[g_last_error_len++] = '\n';
  memcpy(g_last_error + g_last_error_len, text, len);
  g_last_error_len += len;
  g_last_error[g_last_error_len] = '\0';
}

// **I el bloc es torna quan l'app comença** (0.190.8): the page of an error asks for the text once, and a board that has not
// failed keeps no bytes for a buffer that says nothing (docs/30, docs/75).
void forget_the_error() {
  g_last_error_len = 0;
  if (g_last_error == nullptr) return;
  pal::extra_memory().release(g_last_error);
  g_last_error = nullptr;
  g_last_error_cap = 0;
}

// Prints whatever an app threw: a string, or a {code, message} table.
void print_error_value(lua_State* L, int idx) {
  idx = lua_absindex(L, idx);
  if (lua_type(L, idx) == LUA_TSTRING) {
    size_t len = 0;
    const char* text = lua_tolstring(L, idx, &len);
    if (len > kErrorTextCap) len = kErrorTextCap;
    note_the_error(text, len);
    con_text("lua: ");
    con_write(text, len);
    con_text("\r\n");
    return;
  }
  if (lua_type(L, idx) == LUA_TTABLE) {
    lua_getfield(L, idx, "code");
    lua_getfield(L, idx, "message");
    const char* code = lua_tostring(L, -2);
    const char* message = lua_tostring(L, -1);
    char said[192];
    snprintf(said, sizeof(said), "%s%s%s", (code != nullptr) ? code : "",
             (code != nullptr) ? ": " : "", (message != nullptr) ? message : "(no message)");
    note_the_error(said, strlen(said));
    con_text("lua: ");
    con_text(said);
    con_text("\r\n");
    lua_pop(L, 2);
    return;
  }
  con_text("lua: (error object is not a string)\r\n");
}

// The error of compiling a chunk sits on top of the main stack.
void report_error() { print_error_value(g_state, -1); }

// Building a traceback asks for memory, and a task that died because there is
// none must not die a second time inside the report: hence the protected call.
int push_traceback(lua_State* L) {
  lua_State* task = static_cast<lua_State*>(lua_touserdata(L, 1));
  // No message: it has already gone out above, and saying it a second time made
  // one mistake read like two (seen on the board, 30/10).
  luaL_traceback(L, task, nullptr, 1);
  return 1;
}

// The task died: say why, with the traceback of where.
void report_task_error() {
  if (g_task == nullptr) return;
  const int top = lua_gettop(g_task);
  print_error_value(g_task, top);

  const int base = lua_gettop(g_task);
  lua_pushcfunction(g_task, &push_traceback);
  lua_pushlightuserdata(g_task, g_task);
  if (lua_pcall(g_task, 1, 1, 0) == LUA_OK) {
    const char* trace = lua_tostring(g_task, -1);
    if (trace != nullptr) {
      note_the_error(trace, strlen(trace));
      con_text(trace);
      con_text("\r\n");
    }
  }
  lua_settop(g_task, base);
}

void finish_task() {
  if (g_state != nullptr) {
    lua_pushnil(g_state);
    lua_setfield(g_state, LUA_REGISTRYINDEX, kTaskKey);
  }
  g_task = nullptr;
  g_turn_instructions = 0;
  g_budget_pending = false;
  g_budget_yielded = false;
  g_call_split = false;
}



// ---------------------------------------------------------------------------
// admin.LockScreen: el paquet d'un administrador (0.189.99)
// ---------------------------------------------------------------------------

// **`admin.LockScreen([on])`**: sense argument diu si la pantalla és bloquejada, amb un booleà la bloqueja o la deixa
// anar, i sempre torna el que hi ha ara. És el paquet sencer d'aquesta versió: una app d'administrador —`"admin": true` al
// manifest— i la porta que treu la barra del sistema i, amb ella, la targeta que en penja, que és l'única cosa d'aquest
// vidre que pot tancar l'app de sota. **I no és el mateix que `ui.full_screen`**: aquella la desfà el sistema amb tres
// tocs, i aquesta no, perquè una pantalla de bloqueig que es desbloqueja amb tres tocs a qualsevol lloc no és una porta
// (docs/30, docs/46 D1).
int l_admin_lock_screen(lua_State* L) {
  if (g_admin_ops.lock_screen == nullptr || g_admin_ops.is_locked == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build has no bar to lock");
  }
  if (lua_gettop(L) >= 1 && !lua_isnil(L, 1)) {
    const bool on = lua_toboolean(L, 1) != 0;
    if (!g_admin_ops.lock_screen(on)) {
      return push_nil_error(L, "E_NOMEM", "there is no memory for a view of that size");
    }
  }
  lua_pushboolean(L, g_admin_ops.is_locked() ? 1 : 0);
  return 1;
}

// **Els tres tocs del vidre, cedits a la pàgina que els demana** (0.189.101): `admin.triple_tap_close(false)` leaves the gesture
// in the queue, so the page's `touch.gesture()` hands it a `triple` and the system closes nothing. The answer is the state
// after the call —`true` when the three taps still close the app— and it is asked even when the page only wants to know
// (docs/30, docs/46 D1).
int l_admin_triple_tap_close(lua_State* L) {
  if (g_admin_ops.three_taps_close == nullptr || g_admin_ops.set_three_taps_close == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build has no three taps to give");
  }
  if (lua_gettop(L) >= 1 && !lua_isnil(L, 1)) {
    g_admin_ops.set_three_taps_close(lua_toboolean(L, 1) != 0);
  }
  lua_pushboolean(L, g_admin_ops.three_taps_close() ? 1 : 0);
  return 1;
}

// **Els tres tocs, per a qualsevol pàgina que els vulgui** (0.190.254, l'amo: *«Desactiva els 3 tocs sobre el Tetris, l'Snake i
// qualsevol joc que no el tingui desactivat»*). Fins avui aquesta porta només existia dins del paquet `admin` —el candau la va
// demanar a la 0.189.101— i un joc no pot ser administrador per una cosa que és del seu dit: un joc que llegeix cada toc no pot
// tenir el sistema tancant-li la partida al tercer, i donar-li `admin` sencer per això seria donar-li `uninstall` i el candau.
// És el mateix gest, la mateixa resposta (`true` vol dir que els tres tocs tanquen encara) i la mateixa mà que els torna quan
// l'app s'acaba (`ui::give_back_the_glass`), o sigui que cap pàgina no pot deixar la placa sense aquesta porta (docs/30, docs/46).
int l_touch_triple_tap_close(lua_State* L) {
  if (g_admin_ops.three_taps_close == nullptr || g_admin_ops.set_three_taps_close == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build has no three taps to give");
  }
  if (lua_gettop(L) >= 1 && !lua_isnil(L, 1)) {
    g_admin_ops.set_three_taps_close(lua_toboolean(L, 1) != 0);
  }
  lua_pushboolean(L, g_admin_ops.three_taps_close() ? 1 : 0);
  return 1;
}

// **`admin.uninstall(id)`: l'app marxa, i amb ella el que havia deixat al sistema** (0.189.157, l'amo: *«Permet
// desinstalarla (esborrarla)»* i *«i que passa amb les alarmes que ha deixat l'app al sistema si es desinstala?»*). La
// carpeta de l'app al seu disc, **les alarmes que havia armat** i la cau de sessions que havia escrit; els ajustos i la dada
// de la persona es queden. Torna una taula amb el que ha deixat anar —«files», «bytes», «alarms», «cache_blocks» i
// «cache_files»— perquè una pàgina pugui dir-ho, i `nil, error` quan no hi ha cap app amb aquell nom o quan és la pàgina
// mateixa (docs/30, docs/40).
int l_admin_uninstall(lua_State* L) {
  if (g_admin_ops.uninstall == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build cannot remove an app");
  }
  const char* id = luaL_checkstring(L, 1);
  AdminGone gone;
  if (!g_admin_ops.uninstall(id, gone)) {
    return push_nil_error(L, "E_INVAL", "that is not an app of this board, or it is the one that is running");
  }
  lua_createtable(L, 0, 5);
  lua_pushinteger(L, static_cast<lua_Integer>(gone.files));
  lua_setfield(L, -2, "files");
  lua_pushinteger(L, static_cast<lua_Integer>(gone.bytes));
  lua_setfield(L, -2, "bytes");
  lua_pushinteger(L, static_cast<lua_Integer>(gone.alarms));
  lua_setfield(L, -2, "alarms");
  // **Les dues meitats de la cau i no la seva suma** (0.189.164): a block of the memory and its file are one session, o sigui
  // que el número sol deia el doble del que hi havia (docs/30, docs/75).
  lua_pushinteger(L, static_cast<lua_Integer>(gone.cache_blocks));
  lua_setfield(L, -2, "cache_blocks");
  lua_pushinteger(L, static_cast<lua_Integer>(gone.cache_files));
  lua_setfield(L, -2, "cache_files");
  return 1;
}

// **`admin.forget_cache(id)`: la caché d'una app, i només la caché** (0.189.157): els blocs de sessió d'aquella app, **de la
// memòria i del disc**, que és la mateixa porta que `sessions destroy <id>` al prompt. L'app es queda, la seva dada es queda i
// la persona la torna a obrir amb la caché freda. Torna «blocks», «files» i «bytes» —zero no és cap error: no hi havia res a
// oblidar— (docs/30, docs/75).
int l_admin_forget_cache(lua_State* L) {
  if (g_admin_ops.forget_cache == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build keeps no cache");
  }
  const char* id = luaL_checkstring(L, 1);
  AdminGone gone;
  if (!g_admin_ops.forget_cache(id, gone)) {
    return push_nil_error(L, "E_INVAL", "that is not an app of this board");
  }
  lua_createtable(L, 0, 3);
  lua_pushinteger(L, static_cast<lua_Integer>(gone.cache_blocks));
  lua_setfield(L, -2, "blocks");
  lua_pushinteger(L, static_cast<lua_Integer>(gone.files));
  lua_setfield(L, -2, "files");
  lua_pushinteger(L, static_cast<lua_Integer>(gone.bytes));
  lua_setfield(L, -2, "bytes");
  return 1;
}

// **`admin.space_of(id, ground)`: el que una app ocupa fora de la seva carpeta** (0.189.160, l'amo: *«A l'app Aplication Info no
// surt mai el tamany de caché, sempre diu 0B 0 files»*). `ground` és `"data"`, `"cfg"` o `"cache"` i la resposta és la mateixa
// forma que la del prompt: fitxers, carpetes, bytes i blocs de memòria. Les carpetes de les apps les llegeix qualsevol app; les
// dades, els ajustos i la cau són d'una app i el terreny dels fitxers les refusa a tothom més (docs/30, docs/70).
int l_admin_space_of(lua_State* L) {
  if (g_admin_ops.space_of == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this build cannot measure an app");
  }
  const char* id = luaL_checkstring(L, 1);
  const char* ground = luaL_optstring(L, 2, "data");
  AdminSpace space;
  if (!g_admin_ops.space_of(id, ground, space)) {
    return push_nil_error(L, "E_INVAL", "that is not a ground of an app of this board");
  }
  lua_createtable(L, 0, 4);
  lua_pushinteger(L, static_cast<lua_Integer>(space.files));
  lua_setfield(L, -2, "files");
  lua_pushinteger(L, static_cast<lua_Integer>(space.folders));
  lua_setfield(L, -2, "folders");
  lua_pushinteger(L, static_cast<lua_Integer>(space.bytes));
  lua_setfield(L, -2, "bytes");
  lua_pushinteger(L, static_cast<lua_Integer>(space.blocks));
  lua_setfield(L, -2, "blocks");
  return 1;
}

// **The table is made only for a machine whose manifest asked for it** (0.189.99): an app without the key does not get a
// table that refuses its calls, it gets nothing at all, so `admin ~= nil` is a question a page can ask and a `nil` is the
// whole answer. The pack is registered here and not with `ui` because it is not a control and it is not a page's business
// (docs/30, docs/40).
void open_admin(lua_State* L) {
  if (!g_boot.admin) return;
  lua_createtable(L, 0, 4);
  set_function(L, "LockScreen", l_admin_lock_screen);
  set_function(L, "triple_tap_close", l_admin_triple_tap_close);
  set_function(L, "uninstall", l_admin_uninstall);
  set_function(L, "forget_cache", l_admin_forget_cache);
  set_function(L, "space_of", l_admin_space_of);
  lua_setglobal(L, "admin");
}

// Everything a fresh state needs before an app can run. It is called through
// lua_pcall: opening the libraries asks for memory too, and a board with a small
// ceiling has to say no and go on, not panic.
int open_everything(lua_State* L) {
  open_libraries(L);
  open_promises(L);
  // **The metatable of a session of the shell** (0.159.0): its pipes are made with the session and its verbs live
  // here, so the handle of `sys.shell()` is an object with `stdin`, `stdout`, `stderr` and `close` (docs/30).
  open_shell_library(L);
  // **I el paquet d'administrador, només si el manifest el va demanar**: the table is built here, before the globals are
  // sealed, or it is not built at all (docs/30, docs/40).
  open_admin(L);
  seal_globals(L);
  return 0;
}

// **The task a chunk runs in is made inside a protected call too** (0.156.0), and for the same reason as the
// libraries above: `lua_newthread` asks for a stack, a ceiling that cannot give it is a state that says no, and
// **outside a `lua_pcall` Lua does not throw at the app: it aborts the process** (`luaD_throw` with no handler and
// then the panic). Found on the emulator on 8/11 —a chunk with a ceiling of thirty-two kilobytes left the suite with a
// SIGABRT— and on the board it would have been the kernel restarting in the middle of an app that only asked for
// too much. The argument is the chunk and the answer is the thread (docs/20, docs/75).
int make_the_task(lua_State* L) {
  lua_State* task = lua_newthread(L);  // the chunk, the thread
  lua_pushvalue(L, 1);                 // the chunk, the thread, the chunk
  lua_xmove(L, task, 1);               // the thread owns the chunk
  return 1;                            // the thread is the answer
}

}  // namespace

// **The three setters are the kernel's and they are not private** (0.159.0, 0.160.0): they have to sit outside the
// anonymous namespace above, or they are three functions with internal linkage that the kernel cannot link and the
// compiler says so (`defined but not used`, on 8/11). What they touch is the anonymous namespace's, which the
// enclosing `luabind` sees (docs/20).
void set_shell_ops(const ShellOps& ops) { g_shell_ops = ops; }
void set_console_attached(bool attached) { g_console_attached = attached; }

void set_console_away(void (*write)(const char* text, size_t len)) { g_console_away = write; }

bool start(const Boot& boot, size_t limit_bytes) {
  if (g_state != nullptr) return false;

  g_boot = boot;
  {
    const char* id = (boot.app_id != nullptr) ? boot.app_id : "";
    size_t i = 0;
    for (; id[i] != '\0' && i + 1 < kAppIdBytes; ++i) g_app_id[i] = id[i];
    g_app_id[i] = '\0';
  }
  // **The path of the app is copied and not pointed at** (4/11): the shell hands `Boot` a word of its own
  // command line, which lives on a stack frame that is gone the moment `run` returns — and the app runs for
  // minutes after that. `app.args.path` was already a copy, made into a Lua string at start; this is the
  // same copy for the kernel, which is what asks it when an editor saves the file it was given
  // (docs/30, docs/70).
  {
    g_app_path[0] = '\0';
    g_app_ext[0] = '\0';
    if (boot.args_path != nullptr) {
      size_t i = 0;
      for (; boot.args_path[i] != '\0' && i + 1 < sizeof(g_app_path); ++i) g_app_path[i] = boot.args_path[i];
      g_app_path[i] = '\0';
      g_boot.args_path = g_app_path;
    }
    if (boot.args_ext != nullptr) {
      size_t i = 0;
      for (; boot.args_ext[i] != '\0' && i + 1 < sizeof(g_app_ext); ++i) g_app_ext[i] = boot.args_ext[i];
      g_app_ext[i] = '\0';
      g_boot.args_ext = g_app_ext;
    }
  }
  g_alloc.used = 0;
  g_alloc.peak = 0;
  g_alloc.limit = limit_bytes;

  g_state = lua_newstate(&counting_alloc, &g_alloc);
  if (g_state == nullptr) {
    g_alloc.limit = 0;
    return false;
  }
  // The meter of a call into the system, from the first line the app runs (docs/30).
  g_call_depth = 0;
  g_timing_call = false;
  g_turn_instructions = 0;
  g_budget_pending = false;
  g_budget_yielded = false;
  g_call_split = false;
  // **The count hook is installed on every band**: the same hook prices the instructions on the emulator and
  // enforces the turn budget on the board, where an instruction costs what it costs and there is nothing to
  // simulate (docs/20, docs/85). The call/return hooks are the meter of the API.
  lua_sethook(g_state, &meter_hook, LUA_MASKCALL | LUA_MASKRET | LUA_MASKCOUNT, kHookInstructions);

  lua_pushcfunction(g_state, &open_everything);
  if (lua_pcall(g_state, 0, 0, 0) != LUA_OK) {
    con_text("lua: the interpreter does not fit in ");
    con_num(static_cast<int>(limit_bytes));
    con_text(" bytes\r\n");
    lua_close(g_state);
    g_state = nullptr;
    g_alloc.limit = 0;
    return false;
  }

  g_task = nullptr;
  g_waiting_ref = LUA_NOREF;
  g_read_ref = LUA_NOREF;
  g_byte_ref = LUA_NOREF;
  g_console_raw = false;
  g_key_read = 0;
  g_key_count = 0;
  g_console_len = 0;
  g_last_was_cr = false;
  g_escape_count = 0;
  g_escape_first_ms = 0;
  g_exit_requested = false;
  g_failed = false;
  // **The page's state is the page's, and a board without one has none of it** (0.179.9): the slot of the dial, the ring of
  // the selector, the finger on the glass and the box of the keyboard are the same `LUNOK_GLASS` that took their code out
  // of this build (docs/30, docs/75).
  g_ui_focus_slot = -1;
  g_ui_focus_generation = 0;
  // A control that takes the dial is a stop of its own unless it is a strip, which says its tab next.
  g_ui_focus_item = 0;
  // **Every machine starts with no ring** (0.179.7): the selector is off until a key of the board is used, and an app that
  // has just opened has not seen one (docs/30).
  g_ui_ring_shown = false;
  // **I el lloc del focus no sobreviu a la pàgina** (0.179.81): the box it waits for is a control of an app that is not here.
  g_ui_focus_back_to_the_band = false;
  g_ui_touch_slot = -1;
  g_ui_touch_generation = 0;
  g_ui_touch_inside = false;
  g_ui_touch_moved = false;
  g_ui_touch_orphan = false;
  g_ui_swipe_moved = false;
  g_ui_swipe_tap_armed = false;
  g_ui_swipe_page = false;
  g_ui_swipe_clean = false;
  g_ui_swipe_painted_ms = 0;
  g_ui_swipe_rest = 0;
  g_ui_swipe_list = -1;
  g_ui_touch_pressed = -1;
  // **The finger is not a number that outlives the interpreter** (0.113.0): the row it started on and the pixels it
  // had walked are the system's, and a new attempt that began with the ones the last one left would move a window the
  // moment it is touched (docs/30, docs/75).
  g_ui_swipe_y0 = 0;
  g_ui_swipe_y = 0;
  g_ui_swipe_tap = 0;
  // **The keyboard's box is this interpreter's**: a reference into a state that is going away is a number that
  // means nothing in the next one (docs/30, docs/75).
  g_keyboard_target_ref = LUA_NOREF;
  return true;
}

void stop() {
  if (g_state == nullptr) return;  // An attempt that nobody is waiting for is dropped here: the app that asked is
  // gone, and an association left running would keep the radio on for nothing
  // (docs/30, docs/75). The same for a press nobody is waiting for any more.
  if (g_net_ops.cancel != nullptr) g_net_ops.cancel();
  if (g_button_ops.cancel != nullptr) g_button_ops.cancel();
  lua_pushnil(g_state);
  lua_setfield(g_state, LUA_REGISTRYINDEX, kNetKey);
  g_task = nullptr;
  g_turn_instructions = 0;
  g_budget_pending = false;
  g_budget_yielded = false;
  g_call_split = false;
  g_waiting_ref = LUA_NOREF;
  g_read_ref = LUA_NOREF;
  g_byte_ref = LUA_NOREF;
  // **Una app que s'acaba surt de la cua dels botons** (0.180.0): the owner: *«si una app tanca de forma prematura, s'ha de
  // desregistrar automàticament»*. Every road out of an app comes through here —its own end, an error, the three taps, the
  // system taking the page away— so this is the one place that has to remember it, with the handle the kernel gave back
  // (docs/30, docs/75).
  if (g_app_button_handle != 0 && g_button_ops.unsubscribe != nullptr) {
    g_button_ops.unsubscribe(g_app_button_handle);
  }
  g_app_button_handle = 0;
  if (g_keys_handle != 0 && g_button_ops.unsubscribe != nullptr) {
    g_button_ops.unsubscribe(g_keys_handle);
  }
  g_keys_handle = 0;
  g_keys_slot = -1;
  g_keys_generation = 0;
  lua_pushnil(g_state);
  lua_setfield(g_state, LUA_REGISTRYINDEX, kButtonListenerKey);
  g_console_raw = false;
  g_key_read = 0;
  g_key_count = 0;
  g_console_len = 0;
  g_last_was_cr = false;
  g_escape_count = 0;
  g_escape_first_ms = 0;
  g_exit_requested = false;
  g_ui_focus_slot = -1;
  g_ui_focus_generation = 0;
  // A control that takes the dial is a stop of its own unless it is a strip, which says its tab next.
  g_ui_focus_item = 0;
  // **And a machine that goes away leaves no ring for the next one** (0.179.7): the next app opens as the owner asked,
  // with the selector off until it sees a key (docs/30).
  g_ui_ring_shown = false;
  // **I el lloc del focus no sobreviu a la pàgina** (0.179.81): the box it waits for is a control of an app that is going.
  g_ui_focus_back_to_the_band = false;
  g_ui_touch_slot = -1;
  g_ui_touch_generation = 0;
  g_ui_touch_inside = false;
  g_ui_touch_moved = false;
  g_ui_touch_orphan = false;
  g_ui_swipe_moved = false;
  g_ui_swipe_tap_armed = false;
  g_ui_swipe_page = false;
  g_ui_swipe_clean = false;
  g_ui_swipe_painted_ms = 0;
  g_ui_swipe_rest = 0;
  g_ui_swipe_list = -1;
  g_ui_touch_pressed = -1;
  // **The finger is not a number that outlives the interpreter** (0.113.0): the row it started on and the pixels it
  // had walked are the system's, and a new attempt that began with the ones the last one left would move a window the
  // moment it is touched (docs/30, docs/75).
  g_ui_swipe_y0 = 0;
  g_ui_swipe_y = 0;
  g_ui_swipe_tap = 0;
  // **And the block of stops goes with it** (0.189.10): it is the page's scratch and not the system's, so an app that
  // ends gives it back and the next one asks for the size its own page needs (docs/75, docs/90).
  ui_stops_release();
  lua_close(g_state);
  g_state = nullptr;
}

bool running() { return g_state != nullptr; }

// **L'últim error que el intèrpret ha dit** (0.189.162): the message and the traceback, kept for the page the system puts on the
// glass when an app stops. The buffer is filled where the console gets the words (`print_error_value` and the traceback of a
// task) so the page and the log never say two different things (docs/30, docs/50).
const char* last_error_text() { return (g_last_error != nullptr) ? g_last_error : ""; }

namespace {

// What a line answered, on one line: values at a prompt are the answer, and a value
// nobody asked for would look like the system talking (docs/30). It is a protected
// call of its own because a __tostring that throws must not take the session with
// it.
int print_results(lua_State* L) {
  const int count = lua_gettop(L);
  for (int i = 1; i <= count; ++i) {
    if (i > 1) con_text("\t");
    size_t len = 0;
    const char* text = luaL_tolstring(L, i, &len);
    if (text != nullptr) con_write(text, len);
    lua_pop(L, 1);
  }
  return 0;
}

}  // namespace

bool eval(const char* source, size_t len) {
  if (g_state == nullptr || source == nullptr) return false;
  lua_State* L = g_state;
  const int top = lua_gettop(L);

  // A person at a prompt means a statement or an expression, and `x = 1` and
  // `1 + 1` are one of each. The expression is tried **first**, because a call is
  // both things —`fs.data()` compiles as a statement and would answer nothing—
  // and the value of a call is exactly what somebody typing it wants to see; a
  // statement is what it falls back to, and `x = 1` is not an expression. A call
  // that returns nothing (`print(1)`) still says nothing extra, so the order costs
  // no noise (docs/30).
  char chunk[kPromptLineBytes + 8];
  int status = LUA_ERRSYNTAX;
  if (len + 7 < sizeof(chunk)) {
    memcpy(chunk, "return ", 7);
    memcpy(chunk + 7, source, len);
    status = luaL_loadbuffer(L, chunk, len + 7, "=prompt");
  }
  if (status != LUA_OK) {
    lua_settop(L, top);
    status = luaL_loadbuffer(L, source, len, "=prompt");
  }
  if (status != LUA_OK) {
    print_error_value(L, top + 1);
    lua_settop(L, top);
    return false;
  }
  if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK) {
    print_error_value(L, top + 1);
    lua_settop(L, top);
    return false;
  }

  const int results = lua_gettop(L) - top;
  if (results > 0) {
    lua_pushcfunction(L, &print_results);
    lua_insert(L, top + 1);  // the function goes before the values it is handed
    if (lua_pcall(L, results, 0, 0) != LUA_OK) print_error_value(L, top + 1);
    con_text("\r\n");
  }
  lua_settop(L, top);
  return true;
}

bool failed() { return g_failed; }

int exit_code() { return g_exit_code; }

void feed_console(const char* data, size_t len) {
  if (g_state == nullptr || data == nullptr) return;
  // **In raw mode the keys are the app's**: no echo, no line, and an arrow delivered whole instead of
  // being swallowed by the line editor. The three ESC still leave, and an arrow is still not one of
  // them —the sequence says it is a key— (docs/30).
  if (g_console_raw) {
    for (size_t i = 0; i < len; ++i) {
      const uint8_t byte = static_cast<uint8_t>(data[i]);
      if (byte == 0x1b) {
        const bool sequence = (i + 1 < len) && (data[i + 1] == '[' || data[i + 1] == 'O');
        if (!sequence) {
          count_escape();
          if (g_exit_requested) return;
        }
      }
      key_push(byte);
    }
    settle_byte();
    return;
  }
  for (size_t i = 0; i < len; ++i) {
    const char c = data[i];

    // A terminal sends \r\n, and sometimes the two halves arrive apart: the LF
    // that follows a CR is not a second Enter (it would be an empty line, and it
    // would answer the next read with nothing).
    if (c == '\n' && g_last_was_cr) {
      g_last_was_cr = false;
      continue;
    }
    g_last_was_cr = (c == '\r');

    if (c == 0x1b) {
      // An arrow key or a function key arrives as ESC [ … or ESC O …: that is one
      // key press, not a request to leave.
      if (i + 1 < len && (data[i + 1] == '[' || data[i + 1] == 'O')) {
        i += 2;
        while (i < len && (data[i] < 0x40 || data[i] > 0x7e)) ++i;
        continue;
      }
      count_escape();
      continue;
    }
    if (c == '\r' || c == '\n') {
      finish_console_line();
      continue;
    }
    if (c == 0x7f || c == 0x08) {
      if (g_console_len > 0) {
        --g_console_len;
        con_write("\b \b", 3);
      }
      continue;
    }
    if (c >= 0x20 && c < 0x7f && g_console_len + 1 < kConsoleLineCap) {
      g_console_line[g_console_len++] = c;
      con_write(&c, 1);  // echo: the app is not a terminal, this is the terminal
    }
  }
}

bool exit_requested() { return g_exit_requested; }

void clear_exit_request() { g_exit_requested = false; }

const char* app_id() { return g_app_id; }

// **I si l'app que hi ha és administradora** (0.190.257): the key of its manifest, read by `apps::read` before the machine
// was made. The filesystem asks it —an administrator holds the prompt's authority over the disk— and the page still gets
// no call it did not have (docs/30, docs/70).
bool app_is_admin() { return g_boot.admin; }

const char* app_path() { return g_app_path; }

void set_file_ops(const FileOps& ops) { g_file_ops = ops; }
void set_apps_ops(const AppsOps& ops) { g_apps_ops = ops; }
void set_session_ops(const SessionOps& ops) { g_session_ops = ops; }

void set_serial_ops(const SerialOps& ops) { g_serial_ops = ops; }

void set_stats_ops(const StatsOps& ops) { g_stats_ops = ops; }

void set_settings_ops(const SettingsOps& ops) { g_settings_ops = ops; }

void set_cfg_ops(const CfgOps& ops) { g_cfg_ops = ops; }
void set_bt_ops(const BtOps& ops) { g_bt_ops = ops; }

void set_app_ops(const AppOps& ops) { g_app_ops = ops; }
void set_net_ops(const NetOps& ops) { g_net_ops = ops; }

void set_power_ops(const PowerOps& ops) { g_power_ops = ops; }

void set_led_ops(const LedOps& ops) { g_led_ops = ops; }

void set_imu_ops(const ImuOps& ops) { g_imu_ops = ops; }

void set_crypto_ops(const CryptoOps& ops) { g_crypto_ops = ops; }

void set_env_ops(const EnvOps& ops) { g_env_ops = ops; }
void set_lora_ops(const LoraOps& ops) { g_lora_ops = ops; }

void set_gps_ops(const GpsOps& ops) { g_gps_ops = ops; }

void set_button_ops(const ButtonOps& ops) { g_button_ops = ops; }

// The hour of the person: `time.local()` and `time.tz()` answer from here, and with no operations installed they answer
// UTC and `E_UNSUPPORTED`, which is the truth of a system that was not told where it lives (docs/30, docs/60).
void set_time_ops(const TimeOps& ops) { g_time_ops = ops; }
// The glass, the same way: the kernel installs it at boot and the three calls of `touch` answer from
// it (docs/30, docs/50).
void set_touch_ops(const TouchOps& ops) { g_touch_ops = ops; }

void set_meter_ops(const MeterOps& ops) { g_meter_ops = ops; }

// And the panel, which the kernel installs only when the board answers with a surface (docs/50).
void set_ui_ops(const UIOps& ops) { g_ui_ops = ops; }
void set_splash_ops(const SplashOps& ops) { g_splash_ops = ops; }
void set_admin_ops(const AdminOps& ops) { g_admin_ops = ops; }
void set_book_ops(const BookOps& ops) { g_book_ops = ops; }

// **I el pont és el mateix a totes les plaques** (0.190.183, l'amo: *«Deixa lua com està, simplement retorna que no es possible. L'API
// hauría ser igual per tots, si no quina gracia té?»*). Fins aquí hi havia un `#if LUNOK_GLASS` al voltant de tot el pont de la pàgina —les
// taules, les 61 funcions dels controls i el lector— i una placa sense vidre **no tenia** `ui`, `canvas`, `epub` ni `term`: una app que hi
// preguntava rebia «això no existeix» en lloc de «aquesta placa no ho pot fer», que són dues respostes diferents i la primera és una mentida
// sobre l'API. El codi ja estava escrit per contestar bé quan no hi ha vidre —`create_control` diu `this board has no display` i els camins
// que no creen cap control miren el punter abans de cridar-lo—, o sigui que el guard només amagava la resposta (docs/30, docs/50).
//
// **El que sí que s'atura és el maquinari**: `no_glass.cpp` respon els mateixos símbols del nucli que el vidre respon a la Paper, i allà no
// hi ha res a pintar (docs/50).

// The answer of a sleep: `true` when the board came back. A board that went to a
// deep sleep never gets here, because the app was closed before it went and the
// next thing that runs is a fresh boot with the app restored (docs/90).
void power_progress(bool ok) {
  lua_State* L = g_state;
  if (L == nullptr) return;
  const int base = lua_gettop(L);
  lua_getfield(L, LUA_REGISTRYINDEX, kPowerKey);
  const bool waiting = lua_isuserdata(L, -1);
  lua_pushnil(L);
  lua_setfield(L, LUA_REGISTRYINDEX, kPowerKey);
  if (waiting) {
    const int p = lua_absindex(L, -1);
    if (ok) {
      lua_pushboolean(L, 1);
      promise_settle(L, p, kOk, lua_gettop(L));
    } else {
      push_error_table(L, "E_UNSUPPORTED", "the board did not sleep");
      promise_settle(L, p, kErr, lua_gettop(L));
    }
  }
  lua_settop(L, base);
}

void net_progress(bool up, const char* code, int32_t count) {
  lua_State* L = g_state;
  if (L == nullptr) return;
  const int base = lua_gettop(L);
  lua_getfield(L, LUA_REGISTRYINDEX, kNetKey);
  const bool waiting = lua_isuserdata(L, -1);
  lua_pushnil(L);
  lua_setfield(L, LUA_REGISTRYINDEX, kNetKey);
  if (waiting) {
    const int p = lua_absindex(L, -1);
    if (up) {
      // A scan answers with how many networks it found, zero included; the rest
      // of the calls answer with true, which is what an app tests. Zero and
      // "nothing to count" are different answers and cannot share a value.
      if (count >= 0) {
        lua_pushinteger(L, static_cast<lua_Integer>(count));
      } else {
        lua_pushboolean(L, 1);
      }
      promise_settle(L, p, kOk, lua_gettop(L));
    } else {
      push_error_table(L, (code != nullptr) ? code : "E_NONET", "the network did not come up");
      promise_settle(L, p, kErr, lua_gettop(L));
    }
  }
  lua_settop(L, base);
}

// The answer of a page. The body is a string of its own, copied out of the buffer
// the board lent: the kernel hands the buffer back the moment this returns
// (docs/75). A body is bytes and not necessarily text, so it goes in with its
// length (docs/30).
void http_progress(bool ok, const char* code, int status, const char* body, uint32_t len,
                   const char* saved_path) {
  lua_State* L = g_state;
  if (L == nullptr) return;
  const int base = lua_gettop(L);
  lua_getfield(L, LUA_REGISTRYINDEX, kHttpKey);
  const bool waiting = lua_isuserdata(L, -1);
  lua_pushnil(L);
  lua_setfield(L, LUA_REGISTRYINDEX, kHttpKey);
  if (waiting) {
    const int p = lua_absindex(L, -1);
    if (ok) {
      lua_createtable(L, 0, 3);
      lua_pushinteger(L, static_cast<lua_Integer>(status));
      lua_setfield(L, -2, "status");
      if (saved_path != nullptr) {
        // **The page is in the file and not here**: `path` says where, and `bytes` is what it holds,
        // so an app opens it and reads it like any other file (docs/30, docs/70).
        lua_pushstring(L, saved_path);
        lua_setfield(L, -2, "path");
      } else {
        if (body != nullptr) {
          lua_pushlstring(L, body, static_cast<size_t>(len));
        } else {
          lua_pushliteral(L, "");
        }
        lua_setfield(L, -2, "body");
      }
      lua_pushinteger(L, static_cast<lua_Integer>(len));
      lua_setfield(L, -2, "bytes");
      promise_settle(L, p, kOk, lua_gettop(L));
    } else {
      push_error_table(L, (code != nullptr) ? code : "E_HTTP", "the page did not come");
      promise_settle(L, p, kErr, lua_gettop(L));
    }
  }
  lua_settop(L, base);
}



// **The second half of the page** (0.179.9): what a control does with the arrows and the centre —the menus, the carets, the
// sliders, the pages of a list and the finger that turns them— lives after the machine of the interpreter because it asks
// the state, and it is as much the page as the bindings above. A board with no glass carries none of it (docs/30, docs/90).
// **Whether an entry of a menu of a bar can be chosen**: a line between two entries and an entry the app turned off are
// both passed over by the dial, because neither of them can be pressed (docs/90).
bool menu_bar_pickable(const UIOps::UIMenuBarTitle& title, lua_Integer number) {
  if (number < 1 || number > title.count) return false;
  const UIOps::UIMenuBarEntry& entry = title.entries[number - 1];
  return !entry.separator && entry.enabled;
}

// **The walk of the dial over the open menu of a bar**, and nothing at all when no menu is unfolded: the bar is there for
// the finger, and its keys only answer while a menu is down —which is what keeps a page with a bar on it from being a dead
// end— (docs/30, docs/90).
bool ui_step_menu_bar(lua_State* L, int object, int direction) {
  const lua_Integer open = ui_field_number(L, object, "open", 0);
  if (open < 1) return false;
  const int base = lua_gettop(L);
  UIOps::UIMenuBarTitle titles[kMaxMenuBarTitles];
  UIOps::UIMenuBarEntry entries[kMaxMenuBarEntries];
  const uint8_t count = ui_menu_bar_items(L, object, titles, entries);
  lua_settop(L, base);
  if (open > count) return false;
  const UIOps::UIMenuBarTitle& title = titles[open - 1];
  const lua_Integer value = ui_field_number(L, object, "index", 0);
  lua_Integer next = (value < 1 || value > title.count) ? ((direction > 0) ? 0 : (title.count + 1)) : value;
  for (;;) {
    next += (direction > 0) ? 1 : -1;
    if (next < 1 || next > title.count) return false;
    if (menu_bar_pickable(title, next)) break;
  }
  lua_getiuservalue(L, object, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return false;
  }
  lua_pushinteger(L, next);
  lua_setfield(L, -2, "index");
  lua_pop(L, 1);
  ui_invalidate(L, object);
  return true;
}

// **The walk of the dial over the options of a menu**: the ones that are on the card and not the hidden ones, because
// an option an app took out is not on the glass and a ring landing on nothing is a menu that looks stuck. When there
// is nothing left in that direction the key goes back to the walk of the dial, the way the end of a slider's range
// gives the arrow back (docs/30, docs/90).
bool ui_step_menu(lua_State* L, int object, int direction) {
  const int base = lua_gettop(L);
  UIOps::UIMenuOption options[kMaxMenuItems];
  const uint8_t total = ui_menu_items(L, object, options);
  lua_settop(L, base);
  if (total == 0) return false;
  const lua_Integer value = ui_field_number(L, object, "index", 1);
  // An option the app never set is entered from the end it is coming from, so the first arrow always lands somewhere.
  lua_Integer next = (value < 1 || value > total) ? ((direction > 0) ? 0 : (total + 1)) : value;
  for (;;) {
    next += (direction > 0) ? 1 : -1;
    if (next < 1 || next > total) return false;
    if (!options[next - 1].hidden) break;
  }
  lua_getiuservalue(L, object, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return false;
  }
  lua_pushinteger(L, next);
  lua_setfield(L, -2, "index");
  lua_pop(L, 1);
  ui_invalidate(L, object);
  return true;
}

// The page draws what it owes and then the glass is painted, which is what a caret moved by the system does (0.85.2).
void ui_draw_and_present(const char* mode);

// **A box of text that a person has entered takes the two keys as the caret** (0.116.0): one letter back or forward in a
// box of one line, one row up or down in a box of several. The arithmetic is the kernel's —a letter is a code point and
// a row is the one the glass shows, wrapped or not— and what is here is the field, the paint and the hook, the same
// three things every other move of a control does (docs/30, docs/90, docs/92).
// **`letters_only` és el cursor d'una lletra i no d'una fila** (0.182.1): the owner: *«el TextBox Multiline ha de fer el mateix que el
// TextBox, recórrer la fila endavant i endarrera»*. A box of several lines used to walk its **rows**, and the two keys of the board
// inside it are the line a person is writing, one letter at a time —the same arithmetic `textbox_caret` does for a box of one line,
// which is pure text and needs no geometry— (docs/30, docs/90).
bool ui_step_caret(lua_State* L, int objects, int index, int direction, bool letters_only) {
  lua_rawgeti(L, objects, index + 1);
  const int object = lua_absindex(L, -1);
  const bool line = ui_field_bool(L, object, "__textbox", false);
  const bool lines = ui_field_bool(L, object, "__textbox_multiline", false);
  if ((!line && !lines) || !ui_field_bool(L, object, "editing", false)) {
    lua_pop(L, 1);
    return false;
  }
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(L, object, kCanvasMeta));
  if (canvas == nullptr || canvas->slot < 0) {
    lua_pop(L, 1);
    return false;
  }
  lua_getiuservalue(L, object, 1);
  const char* text = "";
  if (lua_istable(L, -1)) {
    lua_getfield(L, -1, "text");
    if (lua_isstring(L, -1)) text = lua_tostring(L, -1);
    lua_pop(L, 1);
  }
  const lua_Integer cursor = ui_field_number(L, object, "cursor", 0);
  const lua_Integer scale = ui_field_number(L, object, "scale", 3);
  const uint8_t box_scale = static_cast<uint8_t>((scale < 1) ? 1 : ((scale > 16) ? 16 : scale));
  uint16_t next = static_cast<uint16_t>((cursor < 0) ? 0 : ((cursor > 65535) ? 65535 : cursor));
  if (line || letters_only) {
    next = (g_ui_ops.textbox_caret != nullptr)
               ? g_ui_ops.textbox_caret(canvas->slot, text, next, static_cast<int16_t>(direction))
               : next;
  } else if (g_ui_ops.textbox_multiline_row != nullptr) {
    const bool wrap = ui_field_bool(L, object, "wrap", false);
    const lua_Integer top = ui_field_number(L, object, "top", -1);
    next = g_ui_ops.textbox_multiline_row(canvas->slot, text, next, wrap, box_scale,
                                          static_cast<int16_t>((top < -1) ? -1 : ((top > 4096) ? 4096 : top)),
                                          static_cast<int16_t>(direction));
  }
  lua_pop(L, 1);  // the fields table
  const bool moved = (next != static_cast<uint16_t>((cursor < 0) ? 0 : cursor));
  if (moved) {
    if (ui_set_field_number(L, object, "cursor", next)) ui_invalidate(L, object);
    // **A caret moved by a key takes the window with it**, the same way a letter typed does: a block that walked out of
    // the box is a person editing a row nobody can see (docs/30, docs/90).
    if (lines && ui_set_field_number(L, object, "top", -1)) ui_invalidate(L, object);
    // **The app hears where the caret went before the glass is painted**: it is the number a line under the page says,
    // and the paint of the caret is the same one that takes the app's own words —two paints for one key is an e-ink
    // page blinking for nothing— (docs/30, docs/90).
    ui_call_hook_number(L, object, "on_move", next);
    ui_draw_and_present("gld16");
  }
  lua_pop(L, 1);
  return moved;
}

// **The controls whose centre does nothing but enter them** (0.117.0): a slider is a number and nothing else, so the
// centre has no other meaning to take away, and its keys are its own only once a person is inside it. Anything that is
// added to this list has to have no centre action of its own, or the centre would be two things at once (docs/30,
// docs/92).
bool ui_enters_to_move(lua_State* L, int object) {
  return ui_field_bool(L, object, "__slider", false);
}

// **A control that is a value takes the arrow while it has the focus, and gives it back when it cannot move
// any further.** It is the rule the old system learned —a control returns the key it does not use— and it is
// what keeps a page with a slider from being a dead end: at the end of the range the arrow walks the focus
// again. The moving is the system's and not the app's, the same way the flip of a checkbox is, because a
// number that belongs to a control and a handler that an app can forget are the same bug waiting (docs/30,
// docs/90).
bool ui_step_value(lua_State* L, int objects, int index, int direction) {
  lua_rawgeti(L, objects, index + 1);
  const int object = lua_gettop(L);
  const bool slider = ui_field_bool(L, object, "__slider", false);
  const bool list = ui_field_bool(L, object, "__list", false);
  const bool dialog = ui_field_bool(L, object, "__dialog", false);
  const bool month = ui_field_bool(L, object, "__month", false);
  const bool menu = ui_field_bool(L, object, "__menu", false);
  const bool menu_bar = ui_field_bool(L, object, "__menu_bar", false);
  // **The grid takes the arrows too, and it was missing from this list**: the walk of the dial asked it, the grid
  // answered that it did not take the key because the answer was decided here, before its own branch, and the cells
  // never moved on a page that had a grid (found on the emulator, 3/11).
  const bool grid = ui_field_bool(L, object, "__icon_grid", false);
  // **A strip of tabs is not in this list any more** (0.99.0): its arrows are the walk of the dial, which lands on
  // one tab at a time and goes out of the strip at either end, and the centre chooses the one it is on. A strip that
  // ate the arrows was a page nobody could leave (docs/90, docs/92).
  const bool picker = ui_field_bool(L, object, "__picker", false);
  if (!slider && !list && !dialog && !month && !menu && !menu_bar && !grid && !picker) {
    lua_pop(L, 1);
    return false;
  }
  // **A control whose keys move a value is entered with the centre before its keys are its own** (0.117.0, and the
  // owner asked for it with the slider in his hand): *«Al rebre el focus, els botons continuarán com sempre (canviant
  // focus) s'ha de polsar el botó central per "entrar" al slider i llavors si escollir el valor amb els mateixos dos
  // botons»*. A slider has no centre of its own —its value is the whole control— and that is the one shape this gate is
  // for: the menu, the card, the calendar, the grid and the list have a centre that means "choose", and taking that
  // away would be two changes in one (docs/30, docs/92).
  if (ui_enters_to_move(L, object) && !ui_field_bool(L, object, "editing", false)) {
    lua_pop(L, 1);
    return false;
  }
  // **The menu is the one control whose step is not an arithmetic on a number**: it walks a list of options and skips
  // the ones that are not on the card, so it answers on its own (docs/90).
  if (picker) {
    // **Les dues tecles del dial caminen els valors i la pàgina segueix el triat** (0.189.102): a card of sixty values is one
    // stop of the dial and the band of ink walks the list, which is the same rule a calendar's days follow. At the first and
    // the last value the key goes back to the walk, so the page is never a dead end (docs/30, docs/90).
    lua_Integer total = 0;
    lua_getiuservalue(L, object, 1);
    if (lua_istable(L, -1)) {
      lua_getfield(L, -1, "items");
      if (lua_istable(L, -1)) total = static_cast<lua_Integer>(lua_rawlen(L, -1));
      lua_pop(L, 1);
    }
    lua_pop(L, 1);
    if (total > static_cast<lua_Integer>(kMaxPickerWords)) total = static_cast<lua_Integer>(kMaxPickerWords);
    lua_Integer value = ui_field_number(L, object, "index", 0);
    lua_Integer next = value + ((direction > 0) ? 1 : -1);
    if (value < 1) next = (direction > 0) ? 1 : total;
    const bool moved = (next >= 1 && next <= total && next != value);
    if (moved) {
      lua_getiuservalue(L, object, 1);
      if (lua_istable(L, -1)) {
        lua_pushinteger(L, next);
        lua_setfield(L, -2, "index");
        // **La pàgina es torna a decidir**: the page the card was on was a page a finger turned, and the value that walked out
        // of it brings the card with it (docs/90).
        lua_pushinteger(L, 0);
        lua_setfield(L, -2, "page");
      }
      lua_pop(L, 1);
      ui_invalidate(L, object);
    }
    lua_pop(L, 1);
    return moved;
  }
  if (menu) {
    const bool moved = ui_step_menu(L, object, direction);
    lua_pop(L, 1);
    return moved;
  }
  if (menu_bar) {
    const bool moved = ui_step_menu_bar(L, object, direction);
    lua_pop(L, 1);
    return moved;
  }
  if (grid) {
    // **The grid walks its cells and the page follows the cursor**: at the first and the last one the key goes back to the walk
    // of the dial, which is the same rule every value control follows (docs/30, docs/90).
    lua_Integer total = 0;
    lua_getiuservalue(L, object, 1);
    if (lua_istable(L, -1)) {
      lua_getfield(L, -1, "items");
      if (lua_istable(L, -1)) total = static_cast<lua_Integer>(lua_rawlen(L, -1));
      lua_pop(L, 1);
      // **A grid of entries counts its cells, which are the controls it holds** (0.105.0): the same rule the list
      // follows, so an app that builds the cells of its grid out of labels and pictures does not also have to write the
      // list of their names (docs/30, docs/90).
      if (total == 0) {
        lua_getfield(L, -1, "children");
        if (lua_istable(L, -1)) total = static_cast<lua_Integer>(lua_rawlen(L, -1));
        lua_pop(L, 1);
      }
    }
    lua_pop(L, 1);
    if (total > static_cast<lua_Integer>(kMaxGridCells)) total = static_cast<lua_Integer>(kMaxGridCells);
    const lua_Integer value = ui_field_number(L, object, "index", 0);
    lua_Integer next = value + ((direction > 0) ? 1 : -1);
    if (value < 1) next = (direction > 0) ? 1 : total;
    const bool moved = (next >= 1 && next <= total && next != value);
    if (moved) {
      lua_getiuservalue(L, object, 1);
      if (lua_istable(L, -1)) {
        lua_pushinteger(L, next);
        lua_setfield(L, -2, "index");
      }
      lua_pop(L, 1);
      // **Two cells moved and not the page** (0.177.3): the ring the dial walks is the only thing that changed, so the
      // grid is asked for the rectangle of the cell it leaves and the cell it enters and only a step that changes the
      // page —where the cells are laid out again and the footer says another number— repaints the whole control
      // (docs/30, docs/90).
      const uint8_t from = (value >= 1) ? static_cast<uint8_t>(value) : 0;
      const uint8_t to = static_cast<uint8_t>(next);
      if (!ui_invalidate_grid_ring(L, object, from, to, true)) ui_invalidate(L, object);
    }
    lua_pop(L, 1);
    return moved;
  }
  // **A control that holds a value of its own takes the arrows while it has the focus**: a slider a number, a
  // strip of tabs the chosen one, a list the page, a dialog the door and a calendar the day. What changes between
  // them is the field, the range and the name of the hook, and that is all this function knows (docs/30, docs/90).
  const char* field = slider ? "value" : (dialog ? "choice" : (month ? "selected" : "page"));
  // **A door of a dialog and a day of a calendar are not values the app keeps**: the dial moves them and the app
  // hears about it when the centre chooses one, so there is no hook here (docs/90).
  const char* hook = list ? "on_page" : ((dialog || month) ? nullptr : "on_change");
  lua_Integer step = 1;
  lua_Integer minimum = 0;
  lua_Integer maximum = 100;
  lua_Integer value = 0;
  if (slider) {
    step = ui_field_number(L, object, "step", 1);
    minimum = ui_field_number(L, object, "min", 0);
    maximum = ui_field_number(L, object, "max", 100);
    value = ui_field_number(L, object, "value", 0);
  } else if (dialog) {
    minimum = 1;
    maximum = 2;
    value = ui_field_number(L, object, "choice", 2);
  } else if (month) {
    // **The end of the range is the end of the month**, asked from the one place that knows how long February is
    // (docs/60, docs/90).
    const lua_Integer year = ui_field_number(L, object, "year", 2026);
    const lua_Integer which = ui_field_number(L, object, "month", 1);
    const lua_Integer days = (g_ui_ops.month_days != nullptr)
                                 ? g_ui_ops.month_days(static_cast<uint16_t>(year), static_cast<uint8_t>(which))
                                 : 31;
    minimum = 1;
    maximum = (days > 0) ? days : 1;
    value = ui_field_number(L, object, "selected", 1);
  } else {
    // **How many pages a list has is the kernel's arithmetic**, the same one the drawing asks: a list whose app
    // kept its own copy of that number would turn to a page that is not there (docs/90).
    const LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(L, object, kCanvasMeta));
    uint16_t frame_w = 0;
    uint16_t frame_h = 0;
    uint16_t frame_x = 0;
    uint16_t frame_y = 0;
    if (canvas == nullptr || g_ui_ops.canvas_caps == nullptr || g_ui_ops.list_page_of == nullptr ||
        !g_ui_ops.canvas_caps(canvas->slot, frame_w, frame_h, frame_x, frame_y)) {
      lua_pop(L, 1);
      return false;
    }
    const lua_Integer row_height = ui_field_number(L, object, "row_height", 56);
    lua_getiuservalue(L, object, 1);
    lua_getfield(L, -1, "items");
    lua_Integer total = lua_istable(L, -1) ? static_cast<lua_Integer>(lua_rawlen(L, -1)) : 0;
    // **A list of entries has as many rows as children** (0.104.0): `items` is the shortcut and the entries are the
    // full thing, and the arrows turn the pages of whichever of the two the app filled (docs/30, docs/90).
    lua_getfield(L, -2, "children");
    if (total == 0 && lua_istable(L, -1)) total = static_cast<lua_Integer>(lua_rawlen(L, -1));
    lua_pop(L, 3);
    uint8_t rows = 0;
    const uint8_t pages =
        g_ui_ops.list_page_of(frame_h, static_cast<uint16_t>(row_height), static_cast<uint16_t>(total), rows);
    minimum = 1;
    maximum = (pages > 0) ? pages : 1;
    value = ui_field_number(L, object, "page", 1);
  }
  lua_Integer next = value + ((direction > 0) ? step : -step);
  if (next < minimum) next = minimum;
  if (next > maximum) next = maximum;
  if (next == value) {
    // No room left in that direction: the key goes back to the walk of the dial, or the page is stuck.
    lua_pop(L, 1);
    return false;
  }
  lua_getiuservalue(L, object, 1);
  lua_pushinteger(L, next);
  lua_setfield(L, -2, field);
  lua_pop(L, 1);
  ui_invalidate(L, object);
  if (hook != nullptr) ui_call_hook_number(L, object, hook, next);
  lua_pop(L, 1);
  return true;
}

// ---- the page a finger or a key turns, and the control that owns it (0.179.4) ----
//
// **The paginator is a part of the list and of the grid, not a control an app builds** (the owner: *«El Paginador ha
// d'anar integrat amb les llistes o grids … s'ha de mostrar per si sol a la part d'abaix del control … i s'ha de
// gestionar el swipe i també si el focus sobrepassa l'últim o el primer item visible per canviar de pàgina»*). The
// control paints it at its own foot, it counts its pages with the arithmetic it draws with (`list_page_of`,
// `icon_grid_page_cell`) and it is the one that turns: the app hears `on_page(self, page)` and keeps no page of its own,
// because a page kept twice is a paginator that says one number and rows that are another (docs/30, docs/90).
//
// What this layer adds is the joining of the two: **where the control is** —the stop under the finger or the ring, or its
// father when the stop is a row the app built— and **which page a step lands on**.

// **The list a stop belongs to**, or -1: the stop itself when it is the list —a list of `items` is one stop and paints its
// own rows— or its father when the stop is an entry the app built. The father is asked of the tree, because a slot is a
// number a released control gives back and a row is a control of the app's like any other (docs/30, docs/90). Nothing is
// left on the stack.
int16_t ui_list_of_the_stop(lua_State* L, int objects, int index) {
  if (g_ui_ops.canvas_parent == nullptr || index < 0) return -1;
  const int base = lua_gettop(L);
  lua_rawgeti(L, objects, index + 1);
  int16_t slot = -1;
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(L, -1, kCanvasMeta));
  if (canvas != nullptr) {
    if (ui_field_bool(L, -1, "__list", false)) {
      slot = canvas->slot;
    } else {
      const int16_t father = g_ui_ops.canvas_parent(canvas->slot);
      if (father >= 0 && l_ui_root(L) == 1) {
        const bool found = ui_push_slot_from(L, base + 2, father, 0);
        if (found && ui_field_bool(L, -1, "__list", false)) slot = father;
      }
    }
  }
  lua_settop(L, base);
  return slot;
}

// **One page of a list turned**: the `page` field written, the control marked for a paint and the app told at `on_page`.
// True when the page moved and false when that page is not there —the first one back, the last one forward— which is a key
// or a finger that goes on being whatever it was. **Nothing is painted here**: a page turned by the dial has to put the
// ring on a row of the new page first, and the paint of a ring that arrives after the page would be a ring on the old one
// (docs/30, docs/90).
// **And a finger asks the page from the control and from nowhere else** (0.187.0): **the whole box of the list is the place
// a swipe turns a page**, from its first row to the dots —the paginator is inside the control, so a gesture on it is a
// gesture of the control like any other—. It used to be the strip of the dots (0.186.3) and then the bottom third of the
// box (0.186.4), and both were a guess about where a finger lands: the rule of 0.186.1 already says that **a contact that
// has walked is a gesture and not a tap**, so a swipe over a row picks nothing and the list has nothing to protect. The
// owner: *«pots fer que tot el control clami el swipe? si després es fa a la part del paginador continuarà sent dins del
// control llista o grid, oi?»* (docs/30, docs/90).
bool ui_turn_the_page_of_the_list(lua_State* L, int16_t list_slot, int8_t delta) {
  if (g_ui_ops.list_page_of == nullptr || g_ui_ops.canvas_caps == nullptr || delta == 0) return false;
  const int base = lua_gettop(L);
  if (l_ui_root(L) != 1 || !ui_push_slot_from(L, base + 1, list_slot, 0)) {
    lua_settop(L, base);
    return false;
  }
  lua_remove(L, base + 1);  // the root
  const int list = base + 1;
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(L, list, kCanvasMeta));
  uint16_t frame_w = 0;
  uint16_t frame_h = 0;
  uint16_t frame_x = 0;
  uint16_t frame_y = 0;
  if (canvas == nullptr || !ui_field_bool(L, list, "__list", false) ||
      !g_ui_ops.canvas_caps(canvas->slot, frame_w, frame_h, frame_x, frame_y)) {
    lua_settop(L, base);
    return false;
  }
  const lua_Integer row_height = ui_field_number(L, list, "row_height", 56);
  // **How many pages a list has is the kernel's arithmetic**, the same one the drawing and the arrows of the dial ask:
  // `items` is the shortcut and the entries are the full thing (docs/30, docs/90).
  lua_Integer total = 0;
  lua_getiuservalue(L, list, 1);
  if (lua_istable(L, -1)) {
    lua_getfield(L, -1, "items");
    if (lua_istable(L, -1)) total = static_cast<lua_Integer>(lua_rawlen(L, -1));
    lua_pop(L, 1);
    if (total == 0) {
      lua_getfield(L, -1, "children");
      if (lua_istable(L, -1)) total = static_cast<lua_Integer>(lua_rawlen(L, -1));
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);
  uint8_t rows = 0;
  const uint8_t pages = g_ui_ops.list_page_of(
      frame_h, static_cast<uint16_t>((row_height < 8) ? 8 : row_height),
      static_cast<uint16_t>((total < 0) ? 0 : total), rows);
  const lua_Integer page = ui_field_number(L, list, "page", 1);
  const lua_Integer last = (pages > 0) ? pages : 1;
  lua_Integer next = ((page < 1) ? 1 : page) + delta;
  if (next < 1) next = 1;
  if (next > last) next = last;
  const bool moved = next != page;
  // **I la pàgina, dita** (0.188.2): which list, which page and how many it has, when it moves and when it is already at the
  // end —the third reason a swipe does nothing— (docs/30, docs/90).
  hal::logf(pal::kLogDebug, "UI", "list %d: page %d of %d%s\r\n", static_cast<int>(list_slot),
            static_cast<int>(next), static_cast<int>(last), moved ? "" : " (already there)");
  if (moved) {
    ui_set_field_number(L, list, "page", next);
    ui_invalidate(L, list);
    ui_call_hook_number(L, list, "on_page", next);
  }
  lua_settop(L, base);
  return moved;
}

// **The row of a list the ring goes to after a page turns**: the first stop that belongs to the list, or the last one when
// the page was turned back, because a person who walks down into a page lands on its first row and one who walks up lands
// on its last. -1 when the page has no stop at all, which is a page of labels and a dial with nowhere to land (docs/30,
// docs/90).
int ui_stop_of_the_list(const FocusTarget* targets, int count, int16_t list_slot, bool last) {
  if (g_ui_ops.canvas_parent == nullptr) return -1;
  int found = -1;
  for (int i = 0; i < count; ++i) {
    if (g_ui_ops.canvas_parent(targets[i].slot) != list_slot) continue;
    found = i;
    if (!last) break;
  }
  return found;
}

// **Whether the stop is the last (or the first) row of the page it is on** (0.179.4): the ring walks the stops that are
// **on** the glass, and the rows of the other pages of a list are placed out of its frame, where the walk does not reach
// them. Without this the down key on the last visible row left the list and the rows behind the page could not be reached
// with the keys at all —only with a finger— which is the half of the promise the grid already kept (docs/30, docs/90).
bool ui_at_the_edge_of_the_list(const FocusTarget* targets, int count, int16_t list_slot, int index, int direction) {
  if (g_ui_ops.canvas_parent == nullptr || index < 0) return false;
  // A row of the list and not the list itself: the arrows of the box are the value it keeps, and that is `ui_step_value`.
  if (g_ui_ops.canvas_parent(targets[index].slot) != list_slot) return false;
  // **The last row going down and the first one going up**: the edge is the end of the walk in that direction, which is
  // the middle of the page in the other (docs/30, docs/90).
  const int edge = ui_stop_of_the_list(targets, count, list_slot, direction > 0);
  return edge == index;
}

// **One key arrives from the system's keyboard and this layer writes it into the box**: the text and the caret are
// the box's state, and an app that had to insert a letter in a callback could insert the wrong one at the wrong
// place. `on_change(self, text)` arrives with the field already written, and 13 is the enter, which a box of one
// line does not type: it tells the app with `on_enter(self)` and nothing changes (docs/30, docs/90).
// **On escriu la banda** (0.179.45 i 0.179.46): the object the app named with `kb.target` and, when there is none or it is
// **not on the screen any more**, the **text box that has the focus**. Two faults of one shape: a page that does not say
// `kb.target` —the clock's note is one, and the system brings the band up for a box a finger touches— and a page the system
// made again, which leaves the registry pointing at a slot that went back to the table. In both the band typed **into
// nothing at all**, with the band painted and no letter anywhere, which is what the owner had in the editor and in the
// clock —*«el táctil tampoc funciona … el dial no escriu, però els tocs sobre les lletres tampoc»*—. The focus is the
// control the person is looking at and the one the block of the caret is on, so it is the honest second answer, and it is
// said on the console because a box the band could not find is a fault of the page and not of the person (docs/30,
// docs/90).
//
// It leaves **the object on the stack** and answers its index, or 0 when there is nothing to type into. `say` is what keeps
// the question quiet when a caller asks it only to know whether the band can type at all.
int keyboard_object(lua_State* L, bool say) {
  if (g_keyboard_target_ref != LUA_NOREF) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, g_keyboard_target_ref);
    if (canvas_ready(L, lua_gettop(L))) return lua_gettop(L);
    lua_pop(L, 1);
  }
  // **I quan l'app no n'ha nomenat cap, la caixa que ha pres el focus és la que escriu** (0.179.46): a box that a person
  // touches is focused and the system brings the band up for it, and an app that does not say `kb.target` —the clock's note
  // is one— is an app the band could not type into at all: the finger and the dial both, with the band painted and the
  // letter going nowhere. The focus is the control the person is looking at, so it is the answer when the app gave none
  // (docs/30, docs/90).
  if (g_ui_focus_slot >= 0 && l_ui_root(L) == 1) {
    const int base = lua_gettop(L);
    const bool found = ui_push_slot_from(L, base, g_ui_focus_slot, 0);
    lua_remove(L, -2);  // the root, which the finder copied from
    if (found) {
      const bool box = ui_field_bool(L, -1, "__textbox", false) || ui_field_bool(L, -1, "__textbox_multiline", false);
      if (box && canvas_ready(L, lua_gettop(L))) {
        if (say) con_text("keyboard: the band types into the box that has the focus\r\n");
        return lua_gettop(L);
      }
      lua_pop(L, 1);
    }
    lua_settop(L, base);
  }
  // **I si no hi ha cap focus, la caixa de la pàgina quan n'hi ha una de sola** (0.179.63): the band takes the bottom of
  // the glass and the page is built again —`on_resize`—, so the box the system knew is a new object and the old one is
  // gone. An app that writes `kb.target` points it again (the editor does); the clock's note does not, and its page has
  // **one** text box: one box is no choice at all, so it is the answer. The band types into it and the block blinks in
  // it, which is the behaviour the editor has, with a target and without one (docs/30, docs/90).
  {
    const int base = lua_gettop(L);
    FocusList list;
    const int count = ui_collect_targets(L, list.items, list.capacity);
    int which = -1;
    int boxes = 0;
    for (int i = 0; i < count; ++i) {
      lua_rawgeti(L, base + 1, i + 1);
      const bool is_a_box =
          ui_field_bool(L, -1, "__textbox", false) || ui_field_bool(L, -1, "__textbox_multiline", false);
      lua_pop(L, 1);
      if (!is_a_box) continue;
      ++boxes;
      which = i;
    }
    if (boxes == 1 && which >= 0) {
      lua_rawgeti(L, base + 1, which + 1);
      lua_remove(L, base + 1);  // the table the collector left
      if (canvas_ready(L, lua_gettop(L))) {
        if (say) con_text("keyboard: the band types into the only box of the page\r\n");
        return lua_gettop(L);
      }
    }
    lua_settop(L, base);
  }
  return 0;
}

bool ui_keyboard_has_target() {
  if (g_state == nullptr) return false;
  const int top = lua_gettop(g_state);
  const bool any = keyboard_object(g_state, /*say=*/false) != 0;
  lua_settop(g_state, top);
  return any;
}

// **La tecla danya la fila del cursor i no el quadre sencer** (0.179.57): the owner: *«Fes que el refresc només afecti a la zona
// on està el cursor, al proper refresc del multiline ja es veurá correctament. Al fer keystrokes nomeś has de modificar la part
// del cursor»*. A box of several lines was marking **all of itself** for one letter —measured on the emulator, 13/11: `paint
// 0,54 540x602 mode 1 du` for every key on the editor's page, and the reflow of a wrapped document under that `du` left the
// ghost of the text below—. Here the box of the caret the last draw put down says where the letter goes, and the damage is
// the rest of that row from the caret to the right edge: the line under the person's finger reads right and the rows below
// keep what they had until the next draw of the whole box, which is the trade the owner asked for (docs/30, docs/50, docs/90).
// A box of one line, or a control that drew no caret, is marked whole as it was.
bool ui_invalidate_the_key(lua_State* L, int object, bool lines) {
  if (lines && g_ui_ops.canvas_invalidate_rect != nullptr && g_ui_ops.textbox_caret_box != nullptr &&
      g_ui_ops.canvas_caps != nullptr) {
    LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(L, object, kCanvasMeta));
    if (canvas != nullptr && canvas->slot >= 0) {
      uint16_t caret_x = 0;
      uint16_t caret_y = 0;
      uint16_t caret_w = 0;
      uint16_t caret_h = 0;
      uint16_t box_w = 0;
      uint16_t box_h = 0;
      uint16_t box_x = 0;
      uint16_t box_y = 0;
      if (g_ui_ops.textbox_caret_box(canvas->slot, caret_x, caret_y, caret_w, caret_h) &&
          g_ui_ops.canvas_caps(canvas->slot, box_w, box_h, box_x, box_y) && caret_x < box_w && caret_y < box_h) {
        const uint16_t right = static_cast<uint16_t>(box_w - caret_x);
        // **La fila del cursor i les dues del costat**: the row of the caret and the two a letter can reach —an enter puts
        // the block on the row below and a backspace at the start of a row joins it to the one above—, so the band is from
        // one row up to one row down and never the whole box (docs/30, docs/90).
        const int32_t from = (static_cast<int32_t>(caret_y) > static_cast<int32_t>(caret_h))
                                 ? static_cast<int32_t>(caret_y) - caret_h
                                 : 0;
        int32_t to = static_cast<int32_t>(caret_y) + 2 * static_cast<int32_t>(caret_h);
        if (to > static_cast<int32_t>(box_h)) to = static_cast<int32_t>(box_h);
        if (to <= from) return false;
        if (g_ui_ops.canvas_invalidate_rect(canvas->slot, caret_x, static_cast<uint16_t>(from), right,
                                           static_cast<uint16_t>(to - from))) {
          return true;
        }
      }
    }
  }
  ui_invalidate(L, object);
  return false;
}

void ui_keyboard_typed(int code) {
  if (g_state == nullptr) return;
  const int top = lua_gettop(g_state);
  const int object = keyboard_object(g_state, /*say=*/true);
  if (object == 0) {
    lua_settop(g_state, top);
    return;
  }
  // **A key puts the block back and restarts the clock** (0.92.0): a person who is typing sees the caret while the
  // letter goes in, and the six hundred milliseconds are counted from the key and not from the blink before it. **And it
  // marks nothing**: the damage of a key is the row of the caret, and `ui_invalidate_the_key` is where that is said
  // (0.179.57, docs/30, docs/90).
  caret_start(g_state, object, /*whole=*/false);
  lua_getiuservalue(g_state, object, 1);
  if (!lua_istable(g_state, -1)) {
    lua_settop(g_state, top);
    return;
  }
  const int fields = lua_gettop(g_state);
  lua_getfield(g_state, fields, "text");
  // **El text de la caixa no cap en un buffer i no s'hi ha de ficar** (0.179.56): this function copied the whole field into a
  // `char word[256]` and refused the key when it did not fit (`length + 1 < sizeof(word)`), so **a box with 255 bytes or more
  // could not be typed into at all** — and said nothing: the band painted the key, the letter went nowhere, `on_change` was
  // never called. It is the owner's editor, whose document is 1 423 bytes: *«el teclat no envia keystrokes al multiline»*.
  // The text stays where it is —a string of the interpreter— and the new one is built from three pieces of it with
  // `lua_concat`, so a page of any size is typed into and nothing is copied but the letter.
  const int text_index = lua_gettop(g_state);
  size_t bytes = 0;
  const char* text = lua_isstring(g_state, text_index) ? lua_tolstring(g_state, text_index, &bytes) : "";
  if (text == nullptr) {
    text = "";
    bytes = 0;
  }
  const int length = static_cast<int>(bytes);
  const lua_Integer cursor = ui_field_number(g_state, object, "cursor", length);
  int at = (cursor < 0) ? 0 : ((cursor > length) ? length : static_cast<int>(cursor));
  const bool lines = ui_field_bool(g_state, object, "__textbox_multiline", false);
  if (code == 13 && !lines) {
    // **The enter key of a box of several lines is a line break and not the end of the edition**: the control says which
    // one it is by its marker, and the key means in the text what the letter `\n` means. A box of one line still ends the
    // edition with it, which is what the app hears at `on_enter` (docs/30, docs/90).
    ui_call_hook(g_state, object, "on_enter", false, false);
    lua_settop(g_state, top);
    return;
  }
  bool changed = false;
  if (code == 8) {
    if (at > 0) {
      // **A letter goes whole**: the caret steps back to the start of the sequence and the bytes of that letter
      // are as many as its first byte says, or half of a UTF-8 letter is left in the box (docs/90).
      int start = at - 1;
      while (start > 0 && (text[start] & 0xC0) == 0x80) --start;
      int width = 1;
      while (start + width < length && (text[start + width] & 0xC0) == 0x80 && width < 3) ++width;
      // The head and the tail of what is left: `start` bytes and then everything from `start + width`.
      lua_pushlstring(g_state, text, static_cast<size_t>(start));
      lua_pushlstring(g_state, text + start + width, bytes - static_cast<size_t>(start + width));
      lua_concat(g_state, 2);
      at = start;
      changed = true;
    }
  } else if (code > 0 && code < 127) {
    const char letter = static_cast<char>((code == 13) ? '\n' : code);
    lua_pushlstring(g_state, text, static_cast<size_t>(at));
    lua_pushlstring(g_state, &letter, 1);
    lua_pushlstring(g_state, text + at, bytes - static_cast<size_t>(at));
    lua_concat(g_state, 3);
    ++at;
    changed = true;
  }
  if (changed) {
    // **The new text is the one the hook hears**: it is on top of the stack and the old one is dropped, so a page of any
    // length is one string of the interpreter and not a copy of it here (docs/75, docs/90).
    lua_remove(g_state, text_index);
    lua_pushvalue(g_state, -1);
    lua_setfield(g_state, fields, "text");
    lua_pushinteger(g_state, at);
    lua_setfield(g_state, fields, "cursor");
    ui_invalidate_the_key(g_state, object, lines);
    ui_call_hook_text(g_state, object, "on_change", lua_tostring(g_state, -1));
  }
  lua_settop(g_state, top);
}

// **The dial walks the controls.** The spots come from the board, not from the number of the key:
// up/left is previous, down/right is next, centre is enter. A board with one key has only the centre.
bool ui_button_event(const char* kind, uint32_t key) {
  if (g_state == nullptr || kind == nullptr || strcmp(kind, "press") != 0) return false;
  FocusList list;
  FocusTarget* targets = list.items;
  const int top = lua_gettop(g_state);
  const int count = ui_collect_targets(g_state, targets, list.capacity);
  if (count <= 0) {
    lua_settop(g_state, top);
    return false;
  }

  int direction = 0;
  bool activate = false;
  KeyInfo info = {};
  const bool known = (g_button_ops.info != nullptr) && g_button_ops.info(key, info);
  if (known && info.spot != nullptr) {
    if (strcmp(info.spot, "up") == 0 || strcmp(info.spot, "left") == 0) {
      direction = -1;
    } else if (strcmp(info.spot, "down") == 0 || strcmp(info.spot, "right") == 0) {
      direction = 1;
    } else if (strcmp(info.spot, "centre") == 0 || strcmp(info.spot, "center") == 0) {
      activate = true;
    }
  }
  if (direction == 0 && !activate) {
    // A key the board did not describe still has the Paper's shape: the middle enters and the two
    // sides walk. This is the fallback and not the contract; `button.info` is the contract.
    const uint32_t keys = (g_button_ops.count != nullptr) ? g_button_ops.count() : 0;
    if (keys >= 3) {
      if (key == 1) direction = -1;
      if (key == 2) direction = 1;
      if (key == 0) activate = true;
    } else if (keys == 2) {
      if (key == 0) direction = -1;
      if (key == 1) direction = 1;
    } else if (keys == 1 && key == 0) {
      activate = true;
    }
  }
  if (direction == 0 && !activate) {
    lua_settop(g_state, top);
    return false;
  }

  const int objects = top + 1;
  // **A key of the board is the selector coming back** (0.179.7), and it comes back **inside the paint the key is already
  // going to ask for**: every road below presents —the walk paints the ring where it lands, a caret paints its box, a
  // press paints the face down and up—, so asking for one here would be the same control painted twice for one key, which
  // is the ink the launcher's own case counts (docs/30, docs/90).
  ui_ring_shown(true);
  const int current =
      ui_target_index(targets, count, g_ui_focus_slot, g_ui_focus_generation, g_ui_focus_item);
  int target = -1;
  if (activate) {
    target = (current >= 0) ? current : 0;
    if (current < 0) ui_apply_focus(g_state, targets, count, objects, target);
    ui_activate(g_state, count, objects, target);
  } else {
    // **Les tecles amb la banda a la pantalla ja no arriben aqui** (0.180.0): while the band is up the box the band types into
    // is an **exclusive subscriber** of the kernel's queue (`ui_caret_listener`), so the system's own delivery —this
    // function among it— is not called at all and the page is not walked. What is left here is the dial's own caret: a box a
    // person entered with the middle key on a page with no band (a read-only document) (docs/30, docs/90).
    // **A box a person has entered takes the arrows first** (0.116.0): inside a field the two keys are the caret, and a
    // box that is not entered gives them back to the walk, which is the same rule the slider follows with its value.
    // **I el mateix mapa dins del camp** (0.182.1): `-direction` perquè el dial compta `up` com el control d'abans i dins d'una caixa
    // `up` és la lletra següent; la lletra i no la fila, com la caixa d'una línia (docs/30, docs/90).
    bool stepped = (current >= 0) && ui_step_caret(g_state, objects, current, -direction, /*letters_only=*/true);
    if (stepped) {
      lua_settop(g_state, top);
      return true;
    }
    // A control that holds a value takes the arrow first and the walk goes on only if it gives it back
    // (docs/30, docs/90).
    if (current >= 0 && ui_step_value(g_state, objects, current, direction)) {
      // **And the paint of a value that moved is the system's** (0.179.11): a slider, the page of a list, the cursor of a
      // grid or a day of a calendar asks for its box the moment it moves, and nothing presented it —a key that was taken
      // here and a glass that showed the value before was a slider that answered the next time the app presented by
      // itself, which is a page that looks dead—. It is the same promise the walk of the dial keeps below and the caret
      // keeps above, said for the third kind of control (docs/30, docs/90).
      ui_feedback();
      lua_settop(g_state, top);
      return true;
    }
    // **A page ends where the dial walks out of it** (0.179.4): the last visible row of a list and one more step is its
    // next page, with the ring on the row a person is now looking at; the first row and one step back is the page before,
    // with the ring on its last row. It is the promise a grid already kept —its cells of the other pages are stops and the
    // page follows the cursor— said for the rows, and it is why an app no longer turns the page of a list itself
    // (docs/30, docs/90). At the last page the key goes back to the walk, which is the ring of the page and not a dead end.
    if (current >= 0) {
      const int16_t list_slot = ui_list_of_the_stop(g_state, objects, current);
      if (list_slot >= 0 && ui_at_the_edge_of_the_list(targets, count, list_slot, current, direction) &&
          ui_turn_the_page_of_the_list(g_state, list_slot, static_cast<int8_t>((direction > 0) ? 1 : -1))) {
        // **The rows of the new page are other stops**: the list places the entries it holds again when it is drawn —the
        // ones that were out of the frame come in and the ones that were in it go out— so the page is drawn, the walk is
        // collected a second time and the ring lands on a row that is on the glass. One present, after the ring: two of
        // them would be two pages of ink for one key (docs/30, docs/90).
        lua_settop(g_state, top);
        ui_draw_the_page();
        const int again = ui_collect_targets(g_state, targets, list.capacity);
        const int stop = ui_stop_of_the_list(targets, again, list_slot, direction < 0);
        if (stop >= 0) ui_apply_focus(g_state, targets, again, top + 1, stop);
        // **La qualitat no és del gir, és del que hi ha a la pàgina** (0.189.2, l'amo: *«és una puta llista de text blanc i
        // negre, la llista no decidirà a quina qualitat pintar-se, ho faran els elements de la llista un per un»*). Un gir
        // que nomena el mode pinta la caixa sencera amb aquest i trepitja el que cada fila havia demanat; sense mode, el
        // sistema pren **la més forta de les que les files han nomenat** —`a2`, dos nivells i un parpelleig de 290 ms, per a
        // una fila de text— i només cau a la de la pàgina (`gl16`) quan cap no n'ha nomenada cap (docs/30, docs/50).
        ui_draw_and_present(nullptr);
        lua_settop(g_state, top);
        return true;
      }
    }
    // **The page is a ring** (0.156.1): the last control of the glass goes on to the first and the first back to the
    // last, so a key never leaves the page —there is nowhere to leave to, and a walk that stopped at the end would be a
    // key that does nothing—. What the list holds is what is **on** the glass: a control cut out of its father's frame
    // is not in it (see `ui_walk_targets`), so the ends of the ring are the ends of the page (docs/30, docs/92).
    target = (current >= 0) ? (current + direction + count) % count : (direction > 0 ? 0 : count - 1);
    ui_apply_focus(g_state, targets, count, objects, target);
    // The ring the app cannot see: the key was taken here, so the paint of where the dial went is here too.
    ui_feedback();
  }
  lua_settop(g_state, top);
  return true;
}



// True while a page has at least one control a person can land on, **the finger's boxes included**, or the keyboard
// itself is on the glass. The kernel asks it before it gives the middle key to the three-tap exit: a page whose centre
// is enter cannot also close on the third centre press, and a keyboard that took the centre is not a way out of the app
// (0.91.0, docs/30, docs/90).
bool ui_has_focusable() {
  if (g_state == nullptr) return false;
  if (g_ui_ops.keyboard_visible != nullptr && g_ui_ops.keyboard_visible()) return true;
  FocusList list;
  FocusTarget* targets = list.items;
  const int top = lua_gettop(g_state);
  const int count = ui_collect_targets(g_state, targets, list.capacity);
  lua_settop(g_state, top);
  return count > 0;
}

// **El feedback d'un dit amb la tinta barata** (0.188.3): the same draw and present as `ui_feedback`, with `du` instead of
// the two-level pass, for the ring of a contact that is still on the glass. `ui_feedback` is written further up inside the
// private namespace of this file and cannot reach `ui_draw_and_present`, which is declared below it —that is why this one
// lives here, with the page— (docs/30, docs/50).
void ui_feedback_cheap() { ui_draw_and_present("du"); }

// **The page draws what it owes and then the glass is painted**: what `ui.present` does, for the system when it is the
// one that changed a control —a caret moved by a finger is not a letter, and a box has no `on_change` to tell the app
// that its caret moved— (0.85.2, docs/30, docs/90).
void ui_draw_and_present(const char* mode) {
  if (g_state == nullptr || g_ui_ops.present == nullptr) return;
  const int top = lua_gettop(g_state);
  if (l_ui_root(g_state) == 1) {
    lua_getfield(g_state, -1, "draw");
    if (lua_isfunction(g_state, -1)) {
      lua_pushvalue(g_state, -2);
      if (lua_pcall(g_state, 1, 0, 0) != LUA_OK) {
        // A draw that throws is said and does not take the app down, the same as an `on_draw` of a control (docs/30).
        con_text("draw: ");
        const char* message = lua_tostring(g_state, -1);
        con_text((message != nullptr) ? message : "(no message)");
        con_text("\r\n");
        lua_pop(g_state, 1);
      }
    }
  }
  lua_settop(g_state, top);
  ++g_ui_present_count;
  g_ui_ops.present(mode);
}

// **The block blinks between turns** (0.92.0): the kernel calls this every turn, and when six hundred milliseconds have
// gone it turns the `caret` of the box the keyboard types into, redraws that box and presents it. It is the system's
// clock and not the app's: a page that only waits still has a cursor, and a page that types does not have to keep a
// second loop for it. A focus or a key marked the caret dirty, and that turn shows the block before the clock restarts
// (docs/30, docs/90).
// **Hi ha una targeta a sobre de la pàgina** (0.192.40): the same question the finger asks —a `ui.dialog`, a `ui.menu` that
// exists, or a bar of menus with its dropdown down is the card of the page while it is up—  asked by whoever needs to know it
// without a touch. A control released mid-question is not alive, so the answer is read from the tree and the stack is left as
// it was found (docs/30, docs/90).
bool ui_a_card_is_up() {
  if (g_state == nullptr) return false;
  const int top = lua_gettop(g_state);
  bool up = false;
  if (l_ui_root(g_state) == 1) up = ui_push_modal_from(g_state, lua_gettop(g_state), 0);
  lua_settop(g_state, top);
  return up;
}

void caret_poll() {
  if (g_state == nullptr) return;
  const int64_t now = now_ms();
  // **El cursor calla mentre hi ha una targeta a sobre** (0.192.40, l'amo: *«Recorda que durant els modals o menús contextuals o
  // desplegables el cursor calla per evitar problemes de redibuixat»*): the block of a box is a paint of one cell every six hundred
  // milliseconds, and under a card it is a paint nobody sees that the card's paper has to cover again —and the queued redraw of a
  // box that the card took the focus from is a page asking to be drawn in the middle of a question—. The clock is restarted so
  // the block does not jump the moment the card goes (docs/30, docs/50, docs/90).
  if (ui_a_card_is_up()) {
    g_caret_dirty = false;
    g_caret_next_ms = now + caret_blink_ms();
    return;
  }
  // **Per què no es pinta el bloc** (0.179.65): the caret is asked every turn and one of its four exits says nothing, so
  // a glass with no cursor and no `paint … du` line in the log cannot be told apart from a caret that was never started.
  // One debug line per reason, only when the turn does not paint (docs/30, docs/90).
  // **La banda a la pantalla vol un bloc** (0.179.67): a band with a field under it and no block is a field nobody can type
  // into, and it does not matter which path raised the band —a tap that landed on the box, an app that shows it, a page
  // built again under it—. The block is started here, in the same turn that draws it, and from then on it blinks. Before
  // this, every one of those paths had to remember to call `caret_start` and a band could reach the glass with no cursor
  // at all (measured on the board: the clock's note, `caret: nothing to blink`, and not one paint of a cell) (docs/30).
  const bool band_up = (g_ui_ops.keyboard_visible != nullptr) && g_ui_ops.keyboard_visible();
  // **Les tecles les té el control entrat, i no la banda** (0.181.0): the queue is taken and given where `editing` changes
  // —`ui_activate` for the centre, `ui_take_the_focus_from` for the dial, the block of the band for the box the keyboard types
  // into— so this turn has nothing to guess and a box that takes no keys keeps its two keys with no band on the glass
  // (docs/30, docs/90).
  // **I el focus del dial torna a la caixa de la banda** (0.179.81): when the page is built again under the band —`on_resize`, the
  // box released— the place that was the box waits here, and the box the band types into is the one that gets it: the ring, the
  // `on_focus` of the app, the keyboard's target (so a letter goes where it is written) and the entry, because a box a person has
  // entered is a box whose two keys are its caret. It is done once and not every turn, and the flag falls with the band or when
  // there is no box to give it to (docs/30, docs/90).
  if (g_ui_focus_back_to_the_band) {
    int16_t wanted_slot = -1;
    uint16_t wanted_generation = 0;
    if (band_up) {
      const int mark = lua_gettop(g_state);
      const int box = keyboard_object(g_state, /*say=*/false);
      if (box != 0) {
        LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(g_state, box, kCanvasMeta));
        if (canvas != nullptr) {
          wanted_slot = canvas->slot;
          wanted_generation = canvas->generation;
        }
      }
      lua_settop(g_state, mark);
    }
    if (wanted_slot < 0) {
      g_ui_focus_back_to_the_band = false;
    } else {
      const int top = lua_gettop(g_state);
      const int objects = top + 1;
      FocusList list;
      const int count = ui_collect_targets(g_state, list.items, list.capacity);
      int which = -1;
      for (int i = 0; i < count; ++i) {
        if (list.items[i].slot == wanted_slot && list.items[i].generation == wanted_generation &&
            list.items[i].item == 0) {
          which = i;
          break;
        }
      }
      if (which >= 0) {
        ui_apply_focus(g_state, list.items, count, objects, which);
        // **I la caixa queda entrada**: a box whose band is up is a box a person is writing in, and `ui_step_caret` gives the two
        // keys to a box that says `editing` and to no other (0.179.81, docs/30, docs/90).
        lua_rawgeti(g_state, objects, which + 1);
        if (ui_set_field_bool(g_state, -1, "editing", true) && g_ui_ops.canvas_invalidate != nullptr) {
          LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(g_state, -1, kCanvasMeta));
          if (canvas != nullptr) g_ui_ops.canvas_invalidate(canvas->slot);
        }
        // **I la caixa nova es queda les tecles de la vella** (0.181.0): the box that was being written into went with the page, and
        // the place of the queue is the new box's, which is what the person has been typing into all along (docs/30, docs/90).
        ui_keys_follow_the_control(g_state, -1);
        lua_settop(g_state, top);
        g_ui_focus_back_to_the_band = false;
      } else {
        lua_settop(g_state, top);
        g_ui_focus_back_to_the_band = false;
      }
    }
  }
  // **La banda pren les tecles mentre és a la pantalla, i les dona quan marxa** (0.181.3): the owner: *«el teclat al sortir registra
  // el triple tap com a exclusiu i el desregistra al sortir, així el sistema no rep l'event i no tanca l'app»*. A box that is being
  // edited has a place of its own —taken above— and this one is for a band with **no field entered** (an app that raised it with
  // `kb:show`): the keys have nowhere to go, and they must not reach the dial or the gesture either. The place is marked with no
  // slot (`g_keys_slot < 0`) so the band's one and a control's one never get confused (docs/30, docs/90).
  // **I una banda que té caixa té la caixa entrada** (0.182.1): an app that raises the band before any finger lands on the box —the
  // editor opens its page with the keyboard already up— leaves the queue here only when the target it named takes no keys; `l_keyboard_show`
  // writes `editing` on the box it types into, so the box's own place is taken by then (docs/30, docs/90).
  if (band_up && g_keys_handle == 0 && g_button_ops.subscribe != nullptr) {
    g_keys_handle = g_button_ops.subscribe(&ui_band_listener, nullptr, /*exclusive=*/1);
    g_keys_slot = -1;
    g_keys_generation = 0;
  } else if (!band_up && g_keys_handle != 0 && g_keys_slot < 0) {
    ui_release_the_keys(g_state, 0);
  }
  if (!g_caret_dirty && g_caret_next_ms <= 0) {
    if (!band_up) {
      // **Una caixa que vol cursor el té sense la banda** (0.189.9): a terminal of this system is read with the keyboard
      // down, and a terminal with no cursor is a page that looks dead. A box that asks with `blink` keeps its block —the
      // block of the cell the caret is in, which is a `du` of one letter every seven hundred milliseconds— and everything
      // else waits for the band, which is the rule this layer had (docs/30, docs/50, docs/90).
      const int mark = lua_gettop(g_state);
      const int asked = keyboard_object(g_state, /*say=*/false);
      const bool wants = (asked != 0) && ui_field_bool(g_state, asked, "blink", false);
      lua_settop(g_state, mark);
      if (!wants) return;
    }
    g_caret_on = true;
    g_caret_dirty = true;
    g_caret_blinks = 0;
    g_caret_present_mark = g_ui_present_count;
    g_caret_next_ms = now + caret_blink_ms();
  }
  if (!g_caret_dirty && now < g_caret_next_ms) return;
  if (g_caret_dirty && g_ui_present_count != g_caret_present_mark) {

    // The app painted the box after the focus or the key: the block is already there, and the clock starts from now.
    g_caret_dirty = false;
    g_caret_next_ms = now + caret_blink_ms();
    // **I el comptador torna a començar**: a page that was painted is a person writing or moving the caret, and the block
    // has earned its blinks again (0.179.35).
    g_caret_blinks = 0;
    return;
  }
  // **The block blinks in the box that has the focus, named or not** (0.179.61): this asked for the target an app writes
  // (`kb.target`) and a box that never writes one —the clock's note is one— had **no block at all**: the band came up, the
  // field was entered, and the only way to see a cursor was to tap the box a second time, which took the band away. The
  // question is the one the band already asks when a letter is typed, so it is the same answer: the target, and the box
  // that has the focus when there is none or it is gone (docs/30, docs/90).
  const int top = lua_gettop(g_state);
  const int object = keyboard_object(g_state, /*say=*/false);
  if (object == 0) {

    lua_settop(g_state, top);
    return;
  }
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(g_state, object, kCanvasMeta));
  if (canvas == nullptr) {

    lua_settop(g_state, top);
    return;
  }
  if (!g_caret_dirty) {
    // **I no para de parpellejar mentre la caixa té el focus** (0.179.61): six blinks and then a block that stood still was
    // a rule against the ghost of the panel that a person reads as a cursor that died, and the ink it saves is one cell of a
    // hundred and twenty milliseconds every six hundred. `caret_start` still restarts the count at every key and every
    // focus, which is what the number was for (docs/30, docs/90).
    g_caret_on = !g_caret_on;
    ++g_caret_blinks;
  }
  caret_set_field(g_state, -1, g_caret_on);
  // **A blink repaints the block and nothing else** (0.168.2): the box of the caret the last draw put down is the part of
  // the control that has changed, and the rule of this project is that a control repaints the part that changed. Before
  // this, every blink marked the **whole** box of the text —fifteen paints in twelve seconds on the emulator where five were
  // owed— and on the board that is a page redrawn for a letter (docs/30, docs/90).
  uint16_t caret_x = 0;
  uint16_t caret_y = 0;
  uint16_t caret_w = 0;
  uint16_t caret_h = 0;
  const bool have_box = (g_ui_ops.textbox_caret_box != nullptr) &&
                        g_ui_ops.textbox_caret_box(canvas->slot, caret_x, caret_y, caret_w, caret_h);
  if (g_ui_ops.canvas_invalidate_rect != nullptr && have_box) {
    g_ui_ops.canvas_invalidate_rect(canvas->slot, caret_x, caret_y, caret_w, caret_h);
  } else {
    ui_invalidate(g_state, -1);
  }
  lua_settop(g_state, top);
  ui_draw_and_present("du");
  g_caret_dirty = false;
  g_caret_next_ms = now + caret_blink_ms();
}

// **A box of several lines that has registered the gesture** (0.88.0): the app says it wants to hear the swipes with an
// `on_swipe`, and a box that has not registered one behaves as always —the finger puts the caret there and nothing more
// (docs/30, docs/90).
bool ui_swipeable(lua_State* L, int object) {
  if (!ui_field_bool(L, object, "__textbox_multiline", false)) return false;
  return ui_has_hook(L, object, "on_swipe");
}

// **The finger that has walked a box with a gesture**: every row the finger walks moves the window, the system writes
// the `top` of the box —what the paint and the bar read—, the page presents itself with `gld16` (which cleans and does
// no flash) and the app hears it at `on_swipe(self, rows)`. The pixels that do not reach a row are kept for the next
// gesture, which is what keeps a slow finger from eating the movement (docs/30, docs/90).
void ui_swipe_by(int object, int rows) {
  if (rows == 0 || g_ui_ops.textbox_multiline_scroll == nullptr) return;
  object = lua_absindex(g_state, object);
  const lua_Integer scale = ui_field_number(g_state, object, "scale", 3);
  const uint8_t box_scale = static_cast<uint8_t>((scale < 1) ? 1 : scale);
  const lua_Integer cursor = ui_field_number(g_state, object, "cursor", 0);
  const lua_Integer top = ui_field_number(g_state, object, "top", -1);
  const bool wrap = ui_field_bool(g_state, object, "wrap", false);
  lua_getiuservalue(g_state, object, 1);
  const char* text = "";
  if (lua_istable(g_state, -1)) {
    lua_getfield(g_state, -1, "text");
    if (lua_isstring(g_state, -1)) text = lua_tostring(g_state, -1);
    lua_pop(g_state, 1);
  }
  lua_pop(g_state, 1);  // the fields table
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(g_state, object, kCanvasMeta));
  const uint16_t first = g_ui_ops.textbox_multiline_scroll(
      canvas->slot, text, static_cast<uint16_t>((cursor < 0) ? 0 : cursor), wrap, box_scale,
      static_cast<int16_t>((top < -1) ? -1 : ((top > 4096) ? 4096 : top)), static_cast<int16_t>(rows));
  if (ui_set_field_number(g_state, object, "top", first)) ui_invalidate(g_state, object);
  // **Un `du` mentre el dit camina i un sol `gld16` quan marxa** (0.179.35): a swipe of a page was a `gld16` for **every
  // row** the finger walked —four hundred and fifty milliseconds a step, so a swipe of ten rows took four and a half
  // seconds and read as a page that does not move—. The quick pass puts the rows up while the hand is moving and the clean
  // one is waited for at the lift, where nobody is looking at the blur (the owner: *«el swipe (amunt i avall) no va per
  // desplaçar-se per l'arxiu»*, docs/30, docs/50, docs/90).
  //
  // **I el `du` no es demana dues vegades en seixanta mil·lisegons**: a quick hand crosses several rows between two frames
  // of the glass, and a paint per row is a queue that outlives the gesture. The window is already written —the next pass
  // shows it whether or not this one was asked— and the `gld16` of the lift is what makes the page true (docs/30, docs/50).
  const int64_t now = now_ms();
  if (g_ui_swipe_painted_ms == 0 || now - g_ui_swipe_painted_ms >= kSwipePaintMs) {
    g_ui_swipe_painted_ms = now;
    ui_draw_and_present("du");
  }
  g_ui_swipe_clean = true;
  ui_call_hook_number(g_state, object, "on_swipe", rows);
}

// **The control of one slot among the stops of the walk**, or -1: **the object is left on the stack** when it is found, so
// the caller says what it is and pops it. A grid and its cells are two kinds of stop and the finger may land on either, so
// the question "where is this slot in the list" is asked more than once (docs/30, docs/90).
int ui_stop_of_slot(lua_State* L, int objects, int count, int16_t slot) {
  for (int i = 0; i < count; ++i) {
    lua_rawgeti(L, objects, i + 1);
    LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(L, -1, kCanvasMeta));
    if (canvas != nullptr && canvas->slot == slot) return i;
    lua_pop(L, 1);
  }
  return -1;
}

// **A finger that walks sideways over a grid turns the page** (0.175.0, the owner: *«usa … el gest swipe left/right per
// canviar de pàgina»*). The page is the grid's and its arithmetic is the grid's —`icon_grid_page_cell`— and the one thing
// the system adds is where a page begins: **the cursor goes to the first cell of it**, because the page of a grid follows
// the cursor and a page turned with the ring left behind is a page whose dial is nowhere. The control under the finger may
// be the grid itself or one of its cells —the cells of a grid of entries are controls of the app— and a cell's grid is its
// father, which is the one thing the kernel's tree knows and the walk does not (docs/30, docs/90).
//
// False when the finger is not on a grid, when that page does not exist —the first one back, the last one forward— or
// when it is already the page the grid is on, and then the finger goes on being whatever it was.
// **La graella que hi ha sota un punt de la pàgina** (0.189.155, l'amo: *«el control UI Grid ha de respondre al swipe també a la
// part blanca sense icones»*): the walk of the stops knows the cells of a grid and not the paper between them, and a finger that
// lands on the paper of a grid is a finger **on the grid** —its whole box is where its page turns since the 0.187.0—. The deepest
// grid whose box holds the point is the answer, and a card over the page still wins: nothing of the page behind it is asked
// for anything (docs/30, docs/90).
void ui_walk_for_a_grid(lua_State* L, int object, int32_t base_x, int32_t base_y, int32_t x, int32_t y, int depth,
                        int16_t& best_slot, int& best_depth) {
  object = lua_absindex(L, object);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(L, object, kCanvasMeta));
  if (!ui_control_alive(canvas)) return;
  if (!ui_in_the_card(canvas->slot) && !ui_leads_to_the_card(canvas->slot)) return;
  const int base = lua_gettop(L);
  lua_getiuservalue(L, object, 1);
  if (!lua_istable(L, -1)) {
    lua_settop(L, base);
    return;
  }
  lua_getfield(L, -1, "children");
  if (!lua_istable(L, -1)) {
    lua_settop(L, base);
    return;
  }
  const lua_Integer children = static_cast<lua_Integer>(lua_rawlen(L, -1));
  for (lua_Integer i = 1; i <= children; ++i) {
    lua_rawgeti(L, -1, i);
    LuaCanvas* child = static_cast<LuaCanvas*>(luaL_testudata(L, -1, kCanvasMeta));
    if (child == nullptr || child->slot < 0 || g_ui_ops.canvas_parent == nullptr ||
        g_ui_ops.canvas_parent(child->slot) != canvas->slot) {
      lua_pop(L, 1);
      continue;
    }
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t local_x = 0;
    uint16_t local_y = 0;
    if (g_ui_ops.canvas_caps == nullptr ||
        !g_ui_ops.canvas_caps(child->slot, width, height, local_x, local_y)) {
      lua_pop(L, 1);
      continue;
    }
    const int32_t cx = base_x + local_x;
    const int32_t cy = base_y + local_y;
    const bool inside = (x >= cx) && (y >= cy) && (x < cx + static_cast<int32_t>(width)) &&
                        (y < cy + static_cast<int32_t>(height));
    if (inside && depth > best_depth && ui_field_bool(L, -1, "__icon_grid", false)) {
      best_slot = child->slot;
      best_depth = depth;
    }
    ui_walk_for_a_grid(L, -1, cx, cy, x, y, depth + 1, best_slot, best_depth);
    lua_pop(L, 1);
  }
  lua_settop(L, base);
}

int16_t ui_grid_under(lua_State* L, int32_t x, int32_t y) {
  const int top = lua_gettop(L);
  int16_t best = -1;
  if (x < 0 || y < 0 || l_ui_root(L) != 1) {
    lua_settop(L, top);
    return best;
  }
  int best_depth = -1;
  ui_walk_for_a_grid(L, lua_gettop(L), 0, 0, x, y, 0, best, best_depth);
  lua_settop(L, top);
  return best;
}

bool ui_grid_turn_the_page(lua_State* L, const FocusTarget* targets, int count, int objects, int index, int8_t delta,
                          int32_t x, int32_t y) {
  if (g_ui_ops.icon_grid_page_cell == nullptr || g_ui_ops.canvas_parent == nullptr) return false;
  if (delta == 0) return false;
  // **I si el dit va caure al paper, la graella d'aquell punt** (0.189.155): a grid whose cells are the app's controls is not a
  // stop of the dial, o sigui que una estona damunt del paper no trobava cap control i la pàgina no girava mai (docs/30, docs/90).
  const int16_t under = (index >= 0 && index < count) ? -1 : ui_grid_under(L, x, y);
  const int base = lua_gettop(L);

  // **The grid, whether the finger is on it or on one of its cells**: a cell's grid is its father, and the grid is asked of
  // the tree by its slot and not of the walk, because **a grid with cells of the app is not a stop of the dial** —its cells
  // are, one by one, and the box of the grid is not walked— (docs/30, docs/90).
  int16_t grid_slot = under;
  if (index >= 0 && index < count) {
    lua_rawgeti(L, objects, index + 1);
    const int touched = lua_gettop(L);
    if (ui_field_bool(L, touched, "__icon_grid", false)) {
      LuaCanvas* grid = static_cast<LuaCanvas*>(luaL_testudata(L, touched, kCanvasMeta));
      if (grid != nullptr) grid_slot = grid->slot;
    } else if (LuaCanvas* cell = static_cast<LuaCanvas*>(luaL_testudata(L, touched, kCanvasMeta))) {
      const int16_t father = g_ui_ops.canvas_parent(cell->slot);
      if (father >= 0) grid_slot = father;
    }
    lua_pop(L, 1);
  }
  if (grid_slot < 0) {
    lua_settop(L, base);
    return false;
  }
  // **I la graella sencera demana la seva pàgina, de dalt a baix de la seva caixa** (0.187.0): the strip of the dots was the
  // only place a grid heard «another page» (0.186.3) and then a third of its box (0.186.4), and both were a guess about where
  // a finger would land: **a contact that has walked is a gesture and not a tap** (`kTapSlop`, 0.186.1), so a swipe over a
  // cell cannot pick it and the box has nothing to protect. The owner: *«pots fer que tot el control clami el swipe?»*
  // (docs/30, docs/90).
  // The tree, from the root down: the object that has this slot, and nothing else left on the stack.
  l_ui_root(L);
  const bool found = ui_push_slot_from(L, lua_gettop(L), grid_slot, 0);
  lua_remove(L, -2);  // the root, which the finder copied from
  if (!found || !ui_field_bool(L, -1, "__icon_grid", false)) {
    lua_settop(L, base);
    return false;
  }
  const int grid_object = lua_gettop(L);

  // **The cells of the grid**: the app's own controls —the ones whose father it is, in the order the walk goes, which is
  // the order the grid counts them— or, for a grid the control paints itself, the `items` it was given.
  int cells = 0;
  for (int i = 0; i < count; ++i) {
    lua_rawgeti(L, objects, i + 1);
    LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(L, -1, kCanvasMeta));
    lua_pop(L, 1);
    if (canvas != nullptr && g_ui_ops.canvas_parent(canvas->slot) == grid_slot) ++cells;
  }
  const bool cells_are_controls = cells > 0;
  if (!cells_are_controls) {
    UIOps::UIGridItem words[kMaxGridCells];
    cells = static_cast<int>(ui_grid_items(L, grid_object, words));
  }
  const lua_Integer grid_index = ui_field_number(L, grid_object, "index", 0);
  const lua_Integer columns = ui_field_number(L, grid_object, "columns", 0);
  const lua_Integer scale = ui_field_number(L, grid_object, "scale", 2);
  const uint8_t first = g_ui_ops.icon_grid_page_cell(
      grid_slot, static_cast<uint8_t>((cells > 255) ? 255 : cells), static_cast<uint8_t>((grid_index < 0) ? 0 : grid_index),
      static_cast<uint8_t>((columns < 0) ? 0 : columns), static_cast<uint8_t>((scale < 1) ? 1 : scale), delta);
  if (first == 0) {
    lua_settop(L, base);
    return false;
  }

  // **The page follows the cursor, so the cursor is put where the page begins** —and the cell of that place takes the ring,
  // which is what tells the app the page moved: the launcher sets its `index` from the `on_focus` of a cell (docs/30).
  if (ui_set_field_number(L, grid_object, "index", first)) ui_invalidate(L, grid_object);
  if (cells_are_controls) {
    int seen = 0;
    for (int i = 0; i < count; ++i) {
      lua_rawgeti(L, objects, i + 1);
      LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(L, -1, kCanvasMeta));
      lua_pop(L, 1);
      if (canvas == nullptr || g_ui_ops.canvas_parent(canvas->slot) != grid_slot) continue;
      if (++seen != static_cast<int>(first)) continue;
      ui_apply_focus(L, targets, count, objects, i);
      break;
    }
  }
  // **I la graella tampoc no tria la seva tinta** (0.189.2): the cells of the page name the quality they were drawn with —a
  // word is `a2`, a photograph is `gc16`— and a turn that named one would paint all of them with it (docs/30, docs/50).
  ui_draw_and_present(nullptr);
  lua_settop(L, base);
  return true;
}

// **A finger that has walked asks for a page, and that is above the tap that would come after it** (0.186.1). The owner,
// with the list of the faces and the one of the libraries on the board: *«estic fent swipe esquerra/dreta i es
// seleccionen items de la llista… és impossible passar pàgina, es selecciona un element i s'acaba obrint; el gest no
// deixa tocs»*. The page turn belongs to the **travel** and not to a frame: a slow swipe is answered on the way, one
// frame at a time, and a flick the panel reported as one jump arrives here with its whole travel and no frame in
// between —so it is asked twice, and the contact is spent either way— (docs/30, docs/90).
// **I el control sencer és el lloc del gest** (0.187.0, el mateix amo: *«pots fer que tot el control clami el swipe?»*): the
// bottom of a list or a grid was the only place it heard «another page» (0.186.3) and then a third of its box (0.186.4), and
// **a contact that has walked is already a gesture and not a tap** (0.186.1), so a swipe over a row picks nothing and there is
// no zone to guess. Every box of one page is a box of the same quality, so the page is drawn and one present puts it on the
// glass.
// **La pàgina d'una targeta de tria, girada per un costat** (0.189.107, l'amo: *«El paginador del desplegable que hi ha a
// System > app at start no respon al drag, si al focus amb els botons gpio»*). A picker with more values than a page holds is a
// list of values by pages, and the hand that turns a list is the same hand: **the sideways travel turns the card one page**,
// clamped at its two ends and written in the card's own field, which is the same write the two keys of the foot and the two
// arrows of the dial do. At the first or the last page there is no page to turn and the gesture is not this card's
// (docs/30, docs/90).
bool ui_step_the_picker(lua_State* L, int object, int16_t slot, int8_t delta) {
  if (!ui_field_bool(L, object, "__picker", false) || g_ui_ops.picker_pages == nullptr) return false;
  object = lua_absindex(L, object);
  const int base = lua_gettop(L);
  lua_getiuservalue(L, object, 1);
  const char* title = "";
  if (lua_istable(L, -1)) {
    lua_getfield(L, -1, "title");
    if (lua_isstring(L, -1)) title = lua_tostring(L, -1);
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  const char* words[kMaxPickerWords];
  const uint16_t total = ui_picker_items(L, object, words, kMaxPickerWords);
  const lua_Integer scale = ui_field_number(L, object, "scale", 4);
  const uint8_t scale8 = static_cast<uint8_t>((scale < 1) ? 1 : ((scale > 16) ? 16 : scale));
  const lua_Integer page = ui_field_number(L, object, "page", 0);
  const lua_Integer on = ui_field_number(L, object, "index", 0);
  const luabind::UIOps::UIPickerPages pages =
      g_ui_ops.picker_pages(slot, title, words, total, static_cast<uint16_t>((on < 0) ? 0 : on),
                            static_cast<uint16_t>((page < 0) ? 0 : page), scale8);
  lua_settop(L, base);
  if (pages.pages < 2) return false;
  lua_Integer next = static_cast<lua_Integer>(pages.page) + delta;
  if (next < 1 || next > static_cast<lua_Integer>(pages.pages)) return false;
  lua_getiuservalue(L, object, 1);
  if (lua_istable(L, -1)) {
    lua_pushinteger(L, next);
    lua_setfield(L, -2, "page");
  }
  lua_pop(L, 1);
  ui_invalidate(L, object);
  return true;
}

bool ui_turn_the_page_of_a_travel(int objects, FocusTarget* targets, int capacity, int count, int index, int dx,
                                  int dy, int top, int32_t x, int32_t y) {
  // **Només els costats passen pàgina** (0.188.5, l'amo: *«per passar pàgina vull dreta/esquerra no amunt/avall… recorda
  // que la pantalla està girada, el touch està llegint verticals on són horizontals»*). A page of a list turns with the
  // hand going **left or right** —right is the page before this one, left the page after, since the 0.189.1— and a finger
  // that goes up or down asks for nothing: the vertical was a second way in (0.179.51) and it was the one that stole the
  // gesture when the glass reported the two axes differently from the page, which is what the owner saw (docs/30, docs/90).
  // **I el costat mana en cas de dubte**: a hand that swipes sideways **always** drifts, so up to forty pixels of drift are
  // forgiven and the sides win while their travel is at least the threshold; past that the finger was really going up or
  // down, and up or down is not a page any more (0.188.5).
  const int walked_x = (dx < 0) ? -dx : dx;
  const int walked_y = (dy < 0) ? -dy : dy;
  const int kDriftPx = kPageSwipeSlop;
  const bool sideways = (walked_x >= kPageSwipeSlop) && ((walked_x + kDriftPx) >= walked_y);
  // **Es diu què ha caminat el dit i què hi havia sota** (0.188.2): the numbers a person cannot see on the glass —the
  // travel, the control of the walk and the list of the landing— are the ones that say why a page did not turn (docs/30).
  if (walked_x >= kTapSlop || walked_y >= kTapSlop) {
    hal::logf(pal::kLogDebug, "UI", "swipe %d,%d: %s, index %d, list %d, %d stops\r\n", dx, dy,
              sideways ? "sideways" : "not a page (the sides turn a page)", index, g_ui_swipe_list, count);
  }
  if (!sideways) return false;
  // **Dreta enrere i esquerra endavant** (0.189.1, l'amo: *«gira els eixos dreta-esquerra, que abans estava bé»*). A finger
  // that walks to the **right** asks for the page before this one and one that walks to the left for the page after it,
  // which is the way this read until the 0.188.6 —where the two were swapped, *«pel meu gust gira al revés, esquerra-dreta
  // intercanviats»*— and the way the owner has now asked it back (docs/30, docs/90).
  const int8_t delta = static_cast<int8_t>((dx > 0) ? -1 : 1);
  // **I la targeta d'una tria gira les seves pàgines amb el mateix gest** (0.189.107): the card is the only control under the
  // finger while it is up, so the travel reaches it like it reaches a list, and the contact is spent —the release must not
  // pick the value the finger happens to be over— (docs/30, docs/90).
  if (index >= 0 && index < count) {
    lua_rawgeti(g_state, objects, index + 1);
    const bool turned_the_card = ui_step_the_picker(g_state, lua_gettop(g_state), targets[index].slot, delta);
    lua_pop(g_state, 1);
    if (turned_the_card) {
      g_ui_swipe_page = true;
      g_ui_swipe_moved = true;
      g_ui_swipe_tap_armed = false;
      g_ui_touch_inside = false;
      g_ui_touch_moved = true;
      ui_draw_and_present(nullptr);
      return true;
    }
  }
  int16_t list_slot = -1;
  bool turned = ui_grid_turn_the_page(g_state, targets, count, objects, index, delta, x, y);
  if (!turned) {
    // **La llista és la del lloc on el gest va començar** (0.183.2): `index` is the control under the finger in this frame
    // and a swipe of a hand leaves the row it began on, so the list the gesture belongs to is the one the landing found
    // —the same rule as the bar and the page, which hear a contact because it began on them and not because it passed over
    // them— (docs/30, docs/90).
    list_slot = (g_ui_swipe_list >= 0) ? g_ui_swipe_list : ui_list_of_the_stop(g_state, objects, index);
    turned = list_slot >= 0 && ui_turn_the_page_of_the_list(g_state, list_slot, delta);
  }
  if (!turned) return false;
  // **The gesture is the whole page turn**: the finger is not a press of the cell it happens to be over, so the contact is
  // spent —the release must not pick anything— and the gesture turns one page and no more (docs/30, docs/90).
  g_ui_swipe_page = true;
  g_ui_swipe_moved = true;
  g_ui_swipe_tap_armed = false;
  g_ui_touch_inside = false;
  g_ui_touch_moved = true;
  if (list_slot < 0) {
    lua_rawgeti(g_state, objects, index + 1);
    if (ui_set_field_bool(g_state, -1, "pressed", false)) ui_invalidate(g_state, -1);
    lua_pop(g_state, 1);
    return true;
  }
  // **The rows of the new page are other controls**: the page is drawn so the list places them, the ring goes to **the
  // first row of the page the finger asked for** —the same place a grid gives it, going forward or back, because a swipe
  // is not a walk and a finger that went back is looking at the top of the page it turned to— and one present puts both
  // on the glass (docs/30, docs/90).
  lua_settop(g_state, top);
  ui_draw_the_page();
  const int again = ui_collect_targets(g_state, targets, capacity);
  const int stop = ui_stop_of_the_list(targets, again, list_slot, false);
  if (stop >= 0) ui_apply_focus(g_state, targets, again, top + 1, stop);
  // **I el gir no tria la tinta de la llista** (0.189.2, l'amo: *«la llista no decidirà a quina qualitat pintar-se, ho faran
  // els elements de la llista un per un»*): the rows of the new page have named their own —`a2` for a row of text, `gl16`
  // for one with a photograph on it— and the page goes up with **the strongest of those**, which is the only way a list of
  // letters stops paying 450 ms and a flash per swipe (docs/30, docs/50).
  ui_draw_and_present(nullptr);
  return true;
}

// **El gest, un cop decidit** (0.189.0, l'amo: *«fes un mètode per retardar els events que s'envien als UIControl. Si es
// decideix que és un gest no s'envien mai els events i s'envia el gest»*). The kernel decides what a contact was **before**
// the page hears anything: a contact that walked is never a press, so the controls never see its frames and this is the only
// thing they hear —once, at the end—. It can mean three things on a page and nothing else; the rest of the gestures are the
// app's and they reach it through `touch.gesture()`:
//
//  - **una pàgina girada**: the list or the grid the finger landed on, by the travel of the whole contact (`dx`, `dy`);
//  - **una caixa desplaçada**: a box of several lines that registered the gesture moves its window by the rows the finger
//    walked —the sign is the finger's, a finger that goes up shows what is further down—;
//  - **i res**: a finger over the paper, over a row, over a button. A row is a place a person taps and a gesture is not
//    a tap (docs/30, docs/90).
void ui_gesture(uint16_t x, uint16_t y, int16_t dx, int16_t dy, bool long_press) {
  if (g_state == nullptr) return;
  FocusList list;
  FocusTarget* targets = list.items;
  const int top = lua_gettop(g_state);
  const int count = ui_collect_targets(g_state, targets, list.capacity);
  const int objects = top + 1;
  const int index = ui_hit(targets, count, x, y);
  // **Una premuda llarga és del control que la vol** (0.189.161, l'amo: *«al fer polsació llarga a Home sobre una app: Inicia l'app
  // App Info amb l'app de paràmetre perquè mostri la finestra detalls directament»*). El kernel ja distingeix un dit llarg d'un toc i
  // d'un lliscament, i el dit llarg arriba aquí amb el punt on va caure: el control d'aquell punt que porta `on_long` el sent i **el
  // contacte s'acaba aquí** —no hi ha clic, no hi ha gir de pàgina i no hi ha anell—; un control que no el porta no canvia gens i el
  // gest segueix el seu camí de sempre (docs/30, docs/90).
  if (long_press && index >= 0 && index < count) {
    lua_rawgeti(g_state, objects, index + 1);
    const int object = lua_gettop(g_state);
    if (ui_call_hook(g_state, object, "on_long", false, false)) {
      ui_invalidate(g_state, object);
      lua_settop(g_state, top);
      return;
    }
    lua_pop(g_state, 1);
  }
  // **La pàgina és la primera pregunta**: a list and a grid answer for themselves and the page turn takes the whole gesture
  // when it happens, the same road the frames used to take (docs/30, docs/90).
  if (ui_turn_the_page_of_a_travel(objects, targets, list.capacity, count, index, dx, dy, top, x, y)) {
    lua_settop(g_state, top);
    return;
  }
  // **I si no, una caixa de diverses línies amb el gest registrat**: the window moves by the rows the finger walked, which is
  // what the frame path did one frame at a time before this version (docs/30, docs/90).
  if (index >= 0 && index < count && g_ui_ops.textbox_multiline_line != nullptr) {
    lua_rawgeti(g_state, objects, index + 1);
    const bool a_box = ui_field_bool(g_state, -1, "__textbox_multiline", false) && ui_swipeable(g_state, -1);
    if (a_box) {
      LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(g_state, -1, kCanvasMeta));
      const lua_Integer scale = ui_field_number(g_state, -1, "scale", 3);
      const int32_t line = (canvas != nullptr)
                               ? g_ui_ops.textbox_multiline_line(canvas->slot,
                                                                 static_cast<uint8_t>((scale < 1) ? 1 : scale))
                               : 0;
      if (line > 0) {
        // **El signe és del dit**: a finger that goes up shows what is further down, which is what a sheet of paper does.
        const int32_t rows = -static_cast<int32_t>(dy) / line;
        if (rows != 0) ui_swipe_by(-1, static_cast<int>(rows));
      }
    }
    lua_pop(g_state, 1);
  }
  lua_settop(g_state, top);
}

// **The finger is the same event as the dial**: a touch that starts on a button focuses it and holds
// it down; a move outside releases the hold, and a release inside clicks it. The frame is the one
// `touch::poll` has just put in the view's coordinates (docs/30, docs/50, docs/90).
void ui_touch_progress(uint16_t x, uint16_t y, bool down) {
  if (g_state == nullptr) return;
  // **Where the finger is, kept for the lift** (0.187.0): the release arrives with no point of its own, and the travel of
  // the contact is what says whether it was a tap or a gesture.
  if (down) {
    g_ui_touch_x = x;
    g_ui_touch_y = y;
    // **El primer dit apaga el selector** (0.179.7, i el forat que el cas va trobar): the ring is the dial's and a hand on the
    // glass is a person pointing with the finger, so it goes with the landing. **The rule was written and nobody called it**:
    // `ui_ring_shown` only had its `true`, from the key, and an app that touched its page kept the ring of a dial it was not
    // using until a key brought it back. The box that has the dial is the only ink that changed, and it is painted here
    // because the landing of a finger on paper paints nothing by itself (docs/30, docs/90).
    if (ui_ring_shown(false)) ui_draw_and_present(nullptr);
  }
  // **A turn with nobody on the glass and nothing held costs nothing**: the panel is read every turn,
  // and walking the whole tree only to find that there is no finger is work the app pays for no reason.
  if (!down && g_ui_touch_slot < 0 && !g_ui_touch_orphan) return;
  FocusList list;
  FocusTarget* targets = list.items;
  const int top = lua_gettop(g_state);
  // **The finger lands on boxes the dial does not walk** (0.85.2): a line of text is a place to put a caret.
  const int count = ui_collect_targets(g_state, targets, list.capacity);
  const int objects = top + 1;

  if (!down) {
    if (g_ui_touch_slot >= 0) {
      const int index =
          ui_target_index(targets, count, g_ui_touch_slot, g_ui_touch_generation, g_ui_touch_item);
      // **A contact that travelled is a gesture and not a tap** (0.187.0): the finger that swiped a list ended over a row,
      // and the release used to click that row —the page never turned and the item opened, which is the owner's report:
      // *«es selecciona un element i s'acaba obrint (rep el toc i no hauria, el gest no deixa tocs)»*. The travel is the one
      // of the whole contact, from the landing to this point, so it is known even when the panel reported the walk as one
      // jump; the threshold is the twenty pixels a contact stops being a tap at, the same number `touch.cpp` uses to call it
      // a `drag` (docs/30, docs/90).
      const int walked_x = static_cast<int>(g_ui_touch_x) - static_cast<int>(g_ui_touch_x0);
      const int walked_y = static_cast<int>(g_ui_touch_y) - static_cast<int>(g_ui_touch_y0);
      const bool walked = (walked_x >= kTapSlop || walked_x <= -kTapSlop ||
                           walked_y >= kTapSlop || walked_y <= -kTapSlop);
      if (walked) {
        // Nothing is pressed and nothing is clicked: the contact belongs to the gesture that is about to be answered.
        g_ui_touch_moved = true;
        g_ui_touch_inside = false;
      }
      // **La passada neta d'un lliscament, un cop, en aixecar el dit** (0.179.35): the rows of a box went up with `du`
      // while the finger walked and this is the one `gld16` that takes the blur away —one at the lift, and not one for each
      // row as it was: four hundred and fifty milliseconds a step, four and a half seconds for a swipe of ten rows, which
      // read as a page that does not move (the owner: *«el swipe (amunt i avall) no va per desplaçar-se per l'arxiu»*)—.
      // It is paid even when the box the finger was in has gone, or the blur would stay on the glass (docs/30, docs/50).
      if (g_ui_swipe_clean) {
        g_ui_swipe_clean = false;
        ui_draw_and_present("gld16");
      }
      if (index >= 0) {
        lua_rawgeti(g_state, objects, index + 1);
        if (ui_set_field_bool(g_state, -1, "pressed", false)) ui_invalidate(g_state, -1);
        // **The tap that waited for the end of the finger is a caret now** (0.88.0): a box with the gesture registered
        // does not move the caret on the way down —the finger can still be a swipe— and, if the finger has not moved
        // far enough, the caret goes there on the way up. It is the same caret and the same present as a tap without a
        // gesture (docs/30, docs/90).
        if (g_ui_swipe_tap_armed) {
          if (ui_set_field_number(g_state, -1, "cursor", g_ui_swipe_tap)) ui_invalidate(g_state, -1);
          ui_draw_and_present("gld16");
        }
        lua_pop(g_state, 1);
        if (g_ui_touch_inside) {
          // **El dit és el mateix esdeveniment que el dial, també pel focus** (0.179.62): a tap lands the ring on the control
          // it touched. Without it a finger could enter a box that **never had the focus**, and the two things that ask the
          // focus when the target an app named is gone —the band that types and the block of the caret— had nothing to go
          // to: the page was built again when the band took the bottom of the glass, the `kb.target` pointed at an object
          // that went with it, and from then on a letter went nowhere and no cursor was ever drawn (docs/30, docs/90).
          // **El dit sobre el control que ja té el focus del teclat no toca res** (0.179.76): "which control had the keyboard's
          // focus? is it this one?" — if it is, the band stays, the focus stays, the edit mode stays and the block stays; the
          // caret went where the finger asked on the way up and that is the whole of the change. A button and a checkbox are
          // happy being pressed again because pressing them is their job; a text box was re-focused, its block stopped and the
          // band taken down by the same tap that was meant to place a caret in the field it was already writing into.
          const int focused = ui_target_index(targets, count, g_ui_focus_slot, g_ui_focus_generation, g_ui_focus_item);
          bool the_same_box = false;
          if (index == focused && g_ui_ops.keyboard_visible != nullptr && g_ui_ops.keyboard_visible()) {
            lua_rawgeti(g_state, objects, index + 1);
            the_same_box =
                ui_field_bool(g_state, -1, "__textbox", false) || ui_field_bool(g_state, -1, "__textbox_multiline", false);
            lua_pop(g_state, 1);
          }
          // **Una fila només s'activa amb un toc de debò** (0.188.9, l'amo: *«els elements de la llista no han de ser sensibles
          // a res que no sigui TAP directe, un tap és tocar i aixecar sense moure»*): a row is activated by a contact that
          // **did not walk** and **lasted less than a long press**. The travel is not enough on its own, because the frames
          // it is measured with are the ones a paint can swallow: on the board a touch of a list froze the loop for four
          // seconds, the lift found the same two points it had at the landing, and the row under the finger was activated by
          // a swipe (`long …,877 dx 0`). **A contact of more than four hundred milliseconds is not a tap**, and a row does
          // nothing with it — no ink, no focus, no click (docs/30, docs/90).
          bool a_row_here = false;
          if (index >= 0 && index < count) {
            lua_rawgeti(g_state, objects, index + 1);
            a_row_here = ui_field_bool(g_state, -1, "__row", false);
            lua_pop(g_state, 1);
          }
          const bool a_tap = !a_row_here || (now_ms() - g_ui_touch_ms0) <= kRowTapMs;
          if (!the_same_box && a_tap) {
            ui_apply_focus(g_state, targets, count, objects, index);
            ui_activate(g_state, count, objects, index, /*by_finger=*/true);
          }
        }
        // **I la banda arriba quan el contacte és un toc, no quan el dit cau** (0.179.35): the keyboard followed the finger
        // that landed on a box (0.91.0), and the band **resizes the page** —the view loses the bottom of the glass and the
        // box measures its rows again—, so a swipe that began on that box lost its target and the rest of the drag scrolled
        // nothing (found by the owner in the editor, 12/11). It arrives **after** the activation and not before, because a
        // resize inside the activation would leave the control it was activating on a page that no longer exists: a person
        // who drags was scrolling, and a person who taps meant to type (docs/30, docs/90).
        if (!g_ui_touch_moved) {
          lua_rawgeti(g_state, objects, index + 1);
          bool keys_wanted = false;
          uint8_t keys_kind = 0;
          // **Només una caixa de text té banda a qui seguir** (0.179.35): a button, a card or a picture has nothing to
          // follow, and asking would take the band away from the box that has it in the lift of the wrong control.
          if (ui_box_keyboard(g_state, lua_gettop(g_state), keys_wanted, keys_kind)) {
            ui_keyboard_follow_object(g_state, lua_gettop(g_state));
          }
          lua_pop(g_state, 1);
        }
      }
      // **A press does not survive the finger that made it** (0.183.3): a control is pressed on the way **down** and let go
      // on the way **up of that same control**, and a finger that walks —the landing pressed one row, the lift lands on
      // another— leaves the first one pressed on the glass: the dark row under the finger after a swipe over a list, which
      // is the visible half of the owner's report *«al acabar el gest, l'item sota el dit queda seleccionat»*. Every control
      // of the page that is still pressed when the contact ends is let go here, and the page is painted only if one of them
      // was —a gesture that pressed nothing costs nothing— (docs/30, docs/90).
      if (g_ui_touch_moved || g_ui_swipe_page) {
        bool let_go = false;
        for (int i = 0; i < count; ++i) {
          lua_rawgeti(g_state, objects, i + 1);
          const int kind = lua_type(g_state, -1);
          if ((kind == LUA_TTABLE || lua_isuserdata(g_state, -1)) &&
              ui_field_bool(g_state, -1, "pressed", false) &&
              ui_set_field_bool(g_state, -1, "pressed", false)) {
            ui_invalidate(g_state, -1);
            let_go = true;
          }
          lua_pop(g_state, 1);
        }
        if (let_go) ui_draw_and_present("du");
      }
      // **And the page the gesture was asking for** (0.187.0): a slow swipe was answered frame by frame on the way, and this
      // is the other half —a flick the panel reported as one jump arrives with its whole travel and no frame in between—. A
      // page that cannot be turned (the first one, going back) turns nothing and spends the contact all the same.
      if (walked && !g_ui_swipe_page && (index >= 0 || g_ui_swipe_list >= 0)) {
        ui_turn_the_page_of_a_travel(objects, targets, list.capacity, count, index, walked_x, walked_y, top, -1, -1);
      }
      // **I el focus, si el gest no va girar cap pàgina** (0.186.1): the ring goes back where it was when the finger landed —or
      // nowhere, if it was nowhere, which is what a page used with a finger looks like— and the row the swipe stopped over is
      // not left looking chosen. **I només es pinta si el focus era un altre** (0.186.4): a swipe over a page that was already
      // as it found it used to present the whole list again, which is ink for nothing and reads as a page that redraws itself
      // (docs/30, docs/90).
      if (walked && !g_ui_swipe_page) {
        const int back =
            ui_target_index(targets, count, g_ui_touch_focus_slot, g_ui_touch_focus_generation, g_ui_touch_focus_item);
        const int now_focused =
            ui_target_index(targets, count, g_ui_focus_slot, g_ui_focus_generation, g_ui_focus_item);
        if (back != now_focused) {
          ui_apply_focus(g_state, targets, count, objects, back);
          ui_draw_and_present("du");
        }
      }
      g_ui_touch_slot = -1;
      g_ui_touch_generation = 0;
      g_ui_touch_item = 0;
      g_ui_touch_inside = false;
      // **A contact that ends after travelling leaves the next one deaf** (0.183.2): the finger of a gesture is not a tap, and
      // a panel that splits one contact in two —or a hand that lands again at once— would click the row it stopped over, which
      // is the whole of the owner's report: *«al acabar el gest, l'item sota el dit queda seleccionat»*. **Un contacte que ja va
      // néixer mut no torna a armar la finestra**: si ho fes, el primer toc engolit faria mut el següent i la llista no respondria
      // mai més (docs/30, docs/90).
      if ((g_ui_touch_moved || g_ui_swipe_page) && !g_ui_tap_swallowed) {
        g_ui_tap_swallowed_until_ms = now_ms() + kAfterGestureMs;
      }
      g_ui_tap_swallowed = false;
      // **A contact that is over is not a gesture any more** (0.179.29): the next press of the same finger —or of the one
      // that comes after it— measures its own travel from its own landing.
      g_ui_touch_moved = false;
      g_ui_swipe_tap_armed = false;
      g_ui_swipe_moved = false;
      g_ui_swipe_page = false;
      g_ui_swipe_clean = false;
      g_ui_swipe_painted_ms = 0;
      g_ui_swipe_rest = 0;
      g_ui_swipe_list = -1;
      g_ui_touch_pressed = -1;
    }
    // **The finger goes up and the contact is over**: the next press is a new one (0.113.0, docs/30).
    g_ui_touch_orphan = false;
    lua_settop(g_state, top);
    return;
  }

  // **I un dit ja no s'endú el cercle en caure** (0.188.8): taking the ring away on the landing was ink **before the gesture
  // had begun** —the row of the ring was repainted on every touch of the glass, four hundred and fifty milliseconds of
  // `gl16` on this board and the canvas of the list when the ring was the list—, and the 2048 does not pay it because a touch
  // there paints one cell. The ring goes away **at the lift**, which is where a tap moves it to the row that was tapped and
  // a swipe gives it back: a contact that is still on the glass changes nothing of the page (docs/30, docs/50, docs/90).

  // **A finger that is still on the glass after its control was given back presses nothing** (0.113.0): the rows of the
  // swipe are not it and neither is the press of the control the app has built in the place of the one that went, which
  // would otherwise be armed by a contact that began somewhere else (docs/30, docs/90).
  if (g_ui_touch_orphan) {
    lua_settop(g_state, top);
    return;
  }

  if (g_ui_touch_slot >= 0) {
    const int index =
        ui_target_index(targets, count, g_ui_touch_slot, g_ui_touch_generation, g_ui_touch_item);
    // **A finger that walks sideways over a grid turns its page** (0.175.0, the owner: *«usa … el gest swipe left/right
    // per canviar de pàgina»*). It is the grid's own page and the grid's own arithmetic —`icon_grid_page_cell`— and what the
    // system does is move the **cursor** to the first cell of that page, because the page of a grid follows the cursor: the
    // ring lands where a person is now looking, and the page comes with it. A page turned with the cursor left behind would
    // be a page whose dial is nowhere (docs/30, docs/90).
    if ((index >= 0 || g_ui_swipe_list >= 0) && !g_ui_swipe_page) {
      const int dx = static_cast<int>(x) - static_cast<int>(g_ui_swipe_x0);
      const int dy = static_cast<int>(y) - static_cast<int>(g_ui_swipe_y0);
      if (ui_turn_the_page_of_a_travel(objects, targets, list.capacity, count, index, dx, dy, top, -1, -1)) {
        lua_settop(g_state, top);
        return;
      }
    }
    // **The finger that walks a box with a gesture moves the window** (0.88.0): the rows it has travelled are paid one
    // at a time, and the gesture is the system's —the caret of the box and the bar are the same number— while the app
    // only registers it and hears it. A finger that does not reach a whole row spends nothing: the pixels are kept for
    // the next one (docs/30, docs/90).
    if (index >= 0 && (g_ui_swipe_tap_armed || g_ui_swipe_moved)) {
      // **The rows of a box go to a box that has the dial and that the finger is on** (0.113.0): a control that the app
      // has taken the focus from —a page built again, a dialog that takes it— or a finger that has walked out of the
      // frame of the box is a gesture that stops. The rows it had travelled are dropped with it: a window that jumps
      // when the finger comes back is worse than one that stopped where it was (docs/30, docs/90).
      lua_rawgeti(g_state, objects, index + 1);
      const int walked = lua_gettop(g_state);
      const bool on_the_box = x >= targets[index].x && y >= targets[index].y &&
                              x < targets[index].x + targets[index].w && y < targets[index].y + targets[index].h;
      const bool has_the_dial = ui_field_bool(g_state, walked, "focused", false);
      lua_pop(g_state, 1);
      if (!on_the_box || !has_the_dial) {
        g_ui_swipe_tap_armed = false;
        g_ui_swipe_moved = false;
        g_ui_swipe_rest = 0;
        lua_settop(g_state, top);
        return;
      }
      const int32_t travel = static_cast<int32_t>(y) - static_cast<int32_t>(g_ui_swipe_y);
      g_ui_swipe_y = y;
      const int32_t rest = static_cast<int32_t>(g_ui_swipe_rest) + travel;
      g_ui_swipe_rest = static_cast<int16_t>((rest > 32767) ? 32767 : ((rest < -32767) ? -32767 : rest));
      if (!g_ui_swipe_moved) {
        const int32_t moved = static_cast<int32_t>(y) - static_cast<int32_t>(g_ui_swipe_y0);
        if (moved >= kSwipeSlop || moved <= -kSwipeSlop) {
          // The finger is a swipe and not a tap: the caret it would have put is dropped, and it stays dropped even if
          // the finger comes back, because a gesture that ends where it began is still a gesture (docs/90).
          g_ui_swipe_moved = true;
          g_ui_swipe_tap_armed = false;
        }
      }
      if (g_ui_swipe_moved && g_ui_ops.textbox_multiline_line != nullptr) {
        lua_rawgeti(g_state, objects, index + 1);
        LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(g_state, -1, kCanvasMeta));
        const lua_Integer scale = ui_field_number(g_state, -1, "scale", 3);
        const int32_t line = g_ui_ops.textbox_multiline_line(canvas->slot,
                                                             static_cast<uint8_t>((scale < 1) ? 1 : scale));
        if (line > 0) {
          // **The sign is the finger's**: a finger that goes up shows what is further down, which is what a sheet of
          // paper does when you turn it upwards (docs/90).
          const int32_t rows = -static_cast<int32_t>(g_ui_swipe_rest) / line;
          if (rows != 0) {
            g_ui_swipe_rest = static_cast<int16_t>(static_cast<int32_t>(g_ui_swipe_rest) + rows * line);
            ui_swipe_by(-1, static_cast<int>(rows));
          }
        }
        lua_pop(g_state, 1);
      }
      lua_settop(g_state, top);
      return;
    }
    // **A contact that has walked is a gesture and not a press** (0.179.29): from the point it landed, in either
    // direction, and once walked it stays walked —a finger that comes back to where it began is still a gesture, which is
    // the rule the text box's caret already followed (0.88.0)—. This is the line that was missing: a `ui.button` has no
    // gesture of its own, so this ordinary path never looked at the travel point and a swipe that ended inside the button
    // clicked it.
    if (!g_ui_touch_moved) {
      const int32_t walked_x = static_cast<int32_t>(x) - static_cast<int32_t>(g_ui_touch_x0);
      const int32_t walked_y = static_cast<int32_t>(y) - static_cast<int32_t>(g_ui_touch_y0);
      if (walked_x >= kTapSlop || walked_x <= -kTapSlop || walked_y >= kTapSlop || walked_y <= -kTapSlop) {
        g_ui_touch_moved = true;
      }
    }
    const bool inside =
        !g_ui_touch_moved && index >= 0 && x >= targets[index].x && y >= targets[index].y &&
        x < targets[index].x + targets[index].w && y < targets[index].y + targets[index].h;
    if (inside != g_ui_touch_inside) {
      // **La premuda és d'un control i es deixa anar on és** (0.183.3): the press is set on the control the finger landed on
      // and the release belongs to **that** control; `index` is the one under the finger in this frame, so a finger that
      // walked away left its row pressed on the glass —the dark row of a swipe over a list—. The slot the contact is holding
      // down is remembered in `g_ui_touch_pressed` and let go here, which is also the cheap way: a row of sixty-four pixels
      // and not the whole page at the lift (docs/30, docs/90).
      if (g_ui_touch_pressed >= 0 && g_ui_touch_pressed != index) {
        lua_rawgeti(g_state, objects, g_ui_touch_pressed + 1);
        if (ui_set_field_bool(g_state, -1, "pressed", false)) ui_invalidate(g_state, -1);
        lua_pop(g_state, 1);
        g_ui_touch_pressed = -1;
      }
      if (index >= 0) {
        lua_rawgeti(g_state, objects, index + 1);
        if (ui_set_field_bool(g_state, -1, "pressed", inside)) ui_invalidate(g_state, -1);
        lua_pop(g_state, 1);
        g_ui_touch_pressed = inside ? static_cast<int16_t>(index) : -1;
      }
      g_ui_touch_inside = inside;
    }
    lua_settop(g_state, top);
    return;
  }

  int hit = ui_hit(targets, count, x, y);
  // **Un contenidor no és una cosa que un dit triï** (0.188.4, l'amo: *«per qué es refresca una llista que no ha tocat ningú?…
  // refrescar la llista sencera no té sentit a menys que canviïs de pàgina. El focus o similar seria només una entrada de la
  // llista»*). A `ui.list` and a `ui.icon_grid` are boxes: their ring is a frame around every row, and taking it costs **the
  // whole canvas of ink** —`paint 22,114 496x776`, four hundred and fifty milliseconds of `gl16` to say what a row already
  // says—. A finger on the dots or on the paper between two rows is not choosing anything, so the container keeps the ring
  // and the contact is only a **page gesture**: the list is remembered here and the page is asked for at the lift
  // (docs/30, docs/90).
  if (hit >= 0) {
    lua_rawgeti(g_state, objects, hit + 1);
    const bool container =
        ui_field_bool(g_state, -1, "__list", false) || ui_field_bool(g_state, -1, "__icon_grid", false);
    const int16_t container_slot = targets[hit].slot;
    lua_pop(g_state, 1);
    if (container) {
      // **El contacte és només el gest de la pàgina**: the travel is measured from here and the lift will ask this list, the
      // same rule the row of a list follows (0.183.2), so a swipe that begins on the paper turns the page as one that begins on
      // a row (docs/30, docs/90). **Res no es prem i el cercle no es mou**: the ring the app put on a row is the app's, and a
      // finger on the dots is not a way of taking it away.
      g_ui_touch_focus_slot = g_ui_focus_slot;
      g_ui_touch_focus_generation = g_ui_focus_generation;
      g_ui_touch_focus_item = g_ui_focus_item;
      g_ui_swipe_list = container_slot;
      g_ui_swipe_x0 = x;
      g_ui_swipe_y0 = y;
      g_ui_touch_x0 = x;
      g_ui_touch_y0 = y;
      g_ui_touch_ms0 = now_ms();
      g_ui_touch_slot = container_slot;
      g_ui_touch_generation = targets[hit].generation;
      g_ui_touch_item = targets[hit].item;
      g_ui_touch_inside = false;
      g_ui_touch_moved = false;
      g_ui_touch_orphan = false;
      g_ui_touch_pressed = -1;
      lua_settop(g_state, top);
      return;
    }
  }
  // **Quin control rep el dit, dit una vegada per premsa** (0.179.78): a tap that lands on a box of one line and does nothing is a
  // question with two answers —the box is not a target at all, or it is and the caret's road is not reached— and this line is the
  // one that tells them apart without a flash per guess. One line a press, and only with the band on the glass (docs/30, docs/90).
  if (g_ui_ops.keyboard_visible != nullptr && g_ui_ops.keyboard_visible()) {
    char said[96];
    snprintf(said, sizeof(said), "the finger at %u,%u hit %d of %d targets\r\n", static_cast<unsigned>(x),
             static_cast<unsigned>(y), hit, count);
    con_text(said);
  }
  if (hit < 0 && g_modal_slot >= 0) {
    // **A finger outside the card is a «no»** (0.172.0): the card is a question and the page behind it is not an answer, so a
    // touch that lands nowhere on it is the cancel door —the app hears `on_cancel` and closes the card, which is the same
    // rule the two doors follow, read and then close and then run (docs/30, docs/90).
    for (int i = 0; i < count; ++i) {
      if (targets[i].slot != g_modal_slot) continue;
      lua_rawgeti(g_state, objects, i + 1);
      const int card = lua_gettop(g_state);
      ui_call_hook(g_state, card, "on_cancel", false, false);
      // **I el que és del sistema el tanca el sistema** (0.192.39): a context menu and the dropdown of a bar of menus are folded
      // here, after the app has heard `on_cancel` (which is where it forgets the object it was holding). **Un menú que es queda
      // perquè l'app no l'ha tancat és una pàgina que no respon a res**, i un desplegable obert la mateixa cosa: el que l'amo
      // demana és que el dit de fora tanqui i prou, com el teclat o una targeta (docs/30, docs/90).
      LuaCanvas* menu = static_cast<LuaCanvas*>(luaL_testudata(g_state, card, kCanvasMeta));
      if (g_modal_kind == kModalMenu && ui_control_alive(menu) && g_ui_ops.canvas_release != nullptr) {
        g_ui_ops.canvas_release(menu->slot);
      } else if (g_modal_kind == kModalBar && ui_control_alive(menu)) {
        // **I el rectangle que el desplegable havia pintat torna a paper abans que la barra es torni a pintar** (0.192.39): the
        // bar has no buffer of its own, o sigui que sense això el que quedava era el fantasma del menú sobre la pàgina, amb les
        // files de sota a trossos (docs/30, docs/90).
        if (g_ui_ops.menu_bar_hole != nullptr) g_ui_ops.menu_bar_hole(menu->slot);
        if (ui_set_field_number(g_state, card, "open", 0)) {
          ui_set_field_number(g_state, card, "index", 0);
          ui_invalidate(g_state, card);
          ui_draw_and_present(nullptr);
        }
      }
      lua_pop(g_state, 1);
      break;
    }
    // **And this contact is spent** (0.173.1): the panel keeps reporting the same finger while it is on the glass, and
    // the next report would find the card gone, the page back and the button under the finger a stop —so the tap that
    // dismissed a question would press whatever was behind it—. The orphan flag is the one this system already has for a
    // finger whose control went away mid-gesture, and it is cleared when the finger goes up (docs/30, docs/90).
    g_ui_touch_orphan = true;
    lua_settop(g_state, top);
    return;
  }
  if (hit >= 0) {
    // **Qui tenia el focus abans que el dit hi caigués** (0.186.1): a gesture gives the ring back to it, because a swipe that
    // leaves the row under the finger wearing the ring is a page that looks like somebody picked something.
    g_ui_touch_focus_slot = g_ui_focus_slot;
    g_ui_touch_focus_generation = g_ui_focus_generation;
    g_ui_touch_focus_item = g_ui_focus_item;
    // **El cercle es mou en aixecar el dit, no en caure-hi** (0.188.8, l'amo: *«si el swipe no va bé, per què el panell
    // desplegable de la barra de títol sí que funciona, i el joc 2048 fa swipe en totes direccions?»*). The ring of the row
    // under the finger used to move **on the landing**, and a row lives inside the canvas of its list: moving it invalidated
    // the whole box and the app painted `500x580` at the quality of a page —four hundred and fifty milliseconds of ink, and
    // on this board the panel held the engine **4745 ms**— **before the gesture had begun**. The 2048 and the shade of the
    // bar are never touched by this because a cell is a small box and a shade is a few rows: their loop keeps turning and
    // the fingers arrive whole. Here the loop stopped, the frames of the swipe were lost and the travel measured was zero
    // (`long …,877 dx 0` and no `drag` at all). The focus is applied **at the lift**, where a tap activates and a swipe gives
    // the ring back (docs/30, docs/50, docs/90).
    // **Where the finger landed**, for the gesture that is a page turn: the sideways travel of a finger over a grid is
    // measured from here and not from the last frame, so a page does not turn twice for one swipe (0.175.0, docs/90).
    // **And it is the point every control measures its own tap from** (0.179.29): the same landing, for the same reason.
    g_ui_swipe_x0 = x;
    // **I el mateix punt per al camí de dalt a baix** (0.179.51): a page of a list is not a scroll, so a finger that goes up
    // is asking for the page after this one and the travel to measure it starts here, where the finger landed.
    g_ui_swipe_y0 = y;
    g_ui_touch_x0 = x;
    g_ui_touch_y0 = y;
    g_ui_touch_ms0 = now_ms();
    g_ui_touch_moved = false;
    g_ui_touch_slot = targets[hit].slot;
    g_ui_touch_generation = targets[hit].generation;
    g_ui_touch_item = targets[hit].item;
    g_ui_touch_inside = true;
    g_ui_touch_orphan = false;
    // **I la llista on ha caigut, per al gest que ve** (0.183.2): the row is a control of the app's and the page that a finger
    // turns belongs to the list it is inside; asking it here, once per contact, is also what stops a swipe from having to find
    // the row in every frame it travels (docs/30, docs/90).
    g_ui_swipe_list = ui_list_of_the_stop(g_state, objects, hit);
    // **Un toc just després d'un gest no és cap toc** (0.183.2): the landing is the one that decides, so the window is read
    // here and the contact is born travelled —nothing is pressed and nothing is clicked when it ends— which is the only way a
    // tap that the panel split in two does not pick the row it stopped over (docs/30, docs/90).
    if (now_ms() < g_ui_tap_swallowed_until_ms) {
      g_ui_touch_moved = true;
      g_ui_touch_inside = false;
      g_ui_tap_swallowed = true;
    }
    lua_rawgeti(g_state, objects, hit + 1);
    const int hit_object = lua_absindex(g_state, -1);
    // **A finger on a dialog picks the button it is on and not the one the dial was on**: the two boxes of a card
    // are its own, and a person who presses Cancel must get Cancel even if the ring was on the other one. **And a
    // finger on a calendar picks the day it is on** (docs/30, docs/90).
    // **Una targeta de tria no és el diàleg de dos botons** (0.189.102): it is `__dialog` so that the page under it answers
    // nothing, but its finger is `picker_hit`'s and reading it here drew a «choice» a card of one value does not have and
    // threw away the pick (found on the emulator) (docs/30, docs/90).
    if (!ui_field_bool(g_state, -1, "__picker", false) && ui_field_bool(g_state, -1, "__dialog", false) &&
        g_ui_ops.dialog_hit != nullptr) {
      const int which = g_ui_ops.dialog_hit(targets[hit].slot,
                                            static_cast<uint16_t>(x - targets[hit].x),
                                            static_cast<uint16_t>(y - targets[hit].y));
      if (which > 0) {
        lua_getiuservalue(g_state, -1, 1);
        if (lua_istable(g_state, -1)) {
          lua_pushinteger(g_state, which);
          lua_setfield(g_state, -2, "choice");
        }
        lua_pop(g_state, 1);
        ui_invalidate(g_state, -1);
      }
    } else if (ui_field_bool(g_state, -1, "__month", false) && g_ui_ops.month_hit != nullptr) {
      const lua_Integer year = ui_field_number(g_state, -1, "year", 2026);
      const lua_Integer which = ui_field_number(g_state, -1, "month", 1);
      const int day = g_ui_ops.month_hit(targets[hit].slot, static_cast<uint16_t>(year),
                                         static_cast<uint8_t>(which), static_cast<uint16_t>(x - targets[hit].x),
                                         static_cast<uint16_t>(y - targets[hit].y));
      if (day > 0) {
        lua_getiuservalue(g_state, -1, 1);
        if (lua_istable(g_state, -1)) {
          lua_pushinteger(g_state, day);
          lua_setfield(g_state, -2, "selected");
        }
        lua_pop(g_state, 1);
        ui_invalidate(g_state, -1);
      }
    } else if (ui_field_bool(g_state, -1, "__menu", false) && g_ui_ops.menu_hit != nullptr) {
      // **A finger on a menu picks the option it is on and not the one the dial was on**: the boxes of the card are its
      // own, and a person who presses the third row gets the third row even if the ring was on the first (docs/90).
      const int base = lua_gettop(g_state);
      UIOps::UIMenuOption items[kMaxMenuItems];
      const uint8_t total = ui_menu_items(g_state, base, items);
      const lua_Integer scale = ui_field_number(g_state, base, "scale", 2);
      lua_getiuservalue(g_state, base, 1);
      const char* title = "";
      if (lua_istable(g_state, -1)) {
        lua_getfield(g_state, -1, "title");
        if (lua_isstring(g_state, -1)) title = lua_tostring(g_state, -1);
        lua_pop(g_state, 1);
      }
      lua_pop(g_state, 1);
      const int which = g_ui_ops.menu_hit(targets[hit].slot, title, items, total,
                                          static_cast<uint8_t>((scale < 1) ? 1 : scale),
                                          static_cast<uint16_t>(x - targets[hit].x),
                                          static_cast<uint16_t>(y - targets[hit].y));
      lua_settop(g_state, base);
      if (which > 0) {
        lua_getiuservalue(g_state, base, 1);
        if (lua_istable(g_state, -1)) {
          lua_pushinteger(g_state, which);
          lua_setfield(g_state, -2, "index");
        }
        lua_pop(g_state, 1);
        ui_invalidate(g_state, base);
      }
    } else if (ui_field_bool(g_state, -1, "__picker", false) && g_ui_ops.picker_hit != nullptr &&
               g_ui_ops.picker_pages != nullptr) {
      // **Un dit a la targeta d'una tria** (0.189.102): a row is chosen where it is pressed —the band of ink goes there and
      // the release picks it— and **a finger on one of the two keys of the foot turns the page and chooses nothing** (the
      // release of that same finger finds `__stepped` written and spends it, see `ui_activate`). A press on the title or on
      // the air between the rows is nobody's and does not become a pick either (docs/30, docs/90).
      const int base = lua_gettop(g_state);
      lua_getiuservalue(g_state, base, 1);
      const char* title = "";
      if (lua_istable(g_state, -1)) {
        lua_getfield(g_state, -1, "title");
        if (lua_isstring(g_state, -1)) title = lua_tostring(g_state, -1);
        lua_pop(g_state, 1);
      }
      lua_pop(g_state, 1);
      // **Les paraules sí que es llegeixen aquí, i és la diferència amb un menú**: the card lays its values in as many columns
      // as the widest of them allows, so which value a finger touched is a fact of the words —and a card answered as if it had
      // one column picks the wrong one— (docs/30, docs/90).
      const char* words[kMaxPickerWords];
      const uint16_t total = ui_picker_items(g_state, base, words, kMaxPickerWords);
      const lua_Integer scale = ui_field_number(g_state, base, "scale", 4);
      const lua_Integer page = ui_field_number(g_state, base, "page", 0);
      const uint8_t scale8 = static_cast<uint8_t>((scale < 1) ? 1 : ((scale > 16) ? 16 : scale));
      const uint16_t asked = static_cast<uint16_t>((page < 0) ? 0 : page);
      const lua_Integer on = ui_field_number(g_state, base, "index", 0);
      const uint16_t on_the_card = static_cast<uint16_t>((on < 0) ? 0 : on);
      const uint8_t which = g_ui_ops.picker_hit(targets[hit].slot, title, words, total, asked, scale8,
                                                static_cast<uint16_t>(x - targets[hit].x),
                                                static_cast<uint16_t>(y - targets[hit].y));
      // **The page is read while the words are still alive**: they are pointers into the table the stack is holding, and the
      // two numbers are needed by both answers below (docs/75).
      const luabind::UIOps::UIPickerPages pages =
          g_ui_ops.picker_pages(targets[hit].slot, title, words, total, on_the_card, asked, scale8);
      lua_settop(g_state, base);
      bool wrote = false;
      bool stepped = false;
      lua_Integer chosen = 0;
      if (which == luabind::UIOps::kPickerBack || which == luabind::UIOps::kPickerForward) {
        lua_Integer next = static_cast<lua_Integer>(pages.page) + ((which == luabind::UIOps::kPickerForward) ? 1 : -1);
        if (next < 1) next = 1;
        if (next > static_cast<lua_Integer>(pages.pages)) next = static_cast<lua_Integer>(pages.pages);
        if (next != static_cast<lua_Integer>(pages.page)) {
          lua_getiuservalue(g_state, base, 1);
          if (lua_istable(g_state, -1)) {
            lua_pushinteger(g_state, next);
            lua_setfield(g_state, -2, "page");
          }
          lua_pop(g_state, 1);
          wrote = true;
        }
        stepped = true;
      } else if (which > 0) {
        // **The value of the page is the value of the list**: the card counts its values page by page —all the columns of a
        // row first— and the number it writes is the one in the whole list, which is what `on_pick` hands over (docs/90).
        lua_Integer rows = static_cast<luabind::UIOps::UIPickerPages>(pages).rows;
        if (rows < 1) rows = 1;
        chosen = (static_cast<lua_Integer>(pages.page) - 1) * rows + which;
        if (chosen > static_cast<lua_Integer>(total)) chosen = static_cast<lua_Integer>(total);
        lua_getiuservalue(g_state, base, 1);
        if (lua_istable(g_state, -1)) {
          lua_pushinteger(g_state, chosen);
          lua_setfield(g_state, -2, "index");
        }
        lua_pop(g_state, 1);
        wrote = true;
      } else {
        // A press that is not on a row and not on a key is not a pick: the release would arrive with the old value and
        // choose a row nobody pressed (docs/30).
        stepped = true;
      }
      lua_getiuservalue(g_state, base, 1);
      if (lua_istable(g_state, -1)) {
        lua_pushboolean(g_state, stepped ? 1 : 0);
        lua_setfield(g_state, -2, "__stepped");
      }
      lua_pop(g_state, 1);
      if (wrote) ui_invalidate(g_state, base);
    } else if (ui_field_bool(g_state, -1, "__menu_bar", false) && g_ui_ops.menu_bar_title_at != nullptr &&
               g_ui_ops.menu_bar_entry_at != nullptr) {
      // **A finger on a title folds or unfolds its menu, and a finger on an entry puts the dial there and the release
      // chooses it.** A dash between two entries is neither of the two, so the tap goes nowhere (docs/90).
      const int base = lua_gettop(g_state);
      UIOps::UIMenuBarTitle titles[kMaxMenuBarTitles];
      UIOps::UIMenuBarEntry entries[kMaxMenuBarEntries];
      const uint8_t count = ui_menu_bar_items(g_state, base, titles, entries);
      const lua_Integer bar_height = ui_field_number(g_state, base, "bar_height", 52);
      const lua_Integer scale = ui_field_number(g_state, base, "scale", 2);
      const lua_Integer open = ui_field_number(g_state, base, "open", 0);
      const uint16_t local_x = static_cast<uint16_t>(x - targets[hit].x);
      const uint16_t local_y = static_cast<uint16_t>(y - targets[hit].y);
      const uint8_t bar_scale = static_cast<uint8_t>((scale < 1) ? 1 : scale);
      const uint16_t height = static_cast<uint16_t>((bar_height < 1) ? 1 : bar_height);
      uint8_t entry = 0;
      if (open > 0) {
        entry = g_ui_ops.menu_bar_entry_at(targets[hit].slot, titles, count, static_cast<uint8_t>(open), height,
                                           bar_scale, local_x, local_y);
      }
      const uint8_t title = g_ui_ops.menu_bar_title_at(targets[hit].slot, titles, count, height, bar_scale, local_x,
                                                       local_y);
      lua_settop(g_state, base);
      if (entry > 0 || title > 0) {
        lua_getiuservalue(g_state, base, 1);
        if (lua_istable(g_state, -1)) {
          if (entry > 0) {
            lua_pushinteger(g_state, entry);
            lua_setfield(g_state, -2, "index");
          } else {
            // The title that was down folds, another one switches, and the entry the dial was on is forgotten.
            // **El desplegable que hi havia es plega abans que el nou es pinti** (0.192.44, l'amo: *«Quan un menú contextual
            // s'obre, tanca els demés oberts … al passar de menú en menú horitzontalment es van desplegant i no estanquen fins
            // sortir del menú»*): the bar paints its card **into the page** and it has no buffer, so a title that switched from
            // `File` to `Edit` left the card of `File` on the glass and painted `Edit` over it —two menus on the page, one of
            // them nobody could press (docs/30, docs/90).
            if (open > 0 && g_ui_ops.menu_bar_hole != nullptr) g_ui_ops.menu_bar_hole(targets[hit].slot);
            lua_pushinteger(g_state, (open == title) ? 0 : title);
            lua_setfield(g_state, -2, "open");
            lua_pushinteger(g_state, 0);
            lua_setfield(g_state, -2, "index");
          }
        }
        lua_pop(g_state, 1);
        ui_invalidate(g_state, base);
      } else if (open > 0) {
        // **Un dit fora del desplegable el plega, i no pren res més** (0.192.39, l'amo: *«El menú contextual … s'ha de tancar per
        // tornar a enviar events a la vista. Es tanca com el teclat virtual o un modal: tocant fora»*). A bar with a menu down is
        // the card of this run (`g_modal_kind == kModalBar`), so a finger that is neither on a title nor on an entry asks for the
        // menu to go —and **it does not run the entry the dial was left on**, which is what the release would have done with the
        // old `index` still written: a page that answers a tap nobody meant. The contact is spent, so the lift activates nothing
        // (docs/30, docs/90).
        lua_getiuservalue(g_state, base, 1);
        if (lua_istable(g_state, -1)) {
          lua_pushinteger(g_state, 0);
          lua_setfield(g_state, -2, "open");
          lua_pushinteger(g_state, 0);
          lua_setfield(g_state, -2, "index");
        }
        lua_pop(g_state, 1);
        // **I el paper que hi havia sota el desplegable torna abans**, o el menú plegat es quedaria pintat (0.192.39, docs/90).
        if (g_ui_ops.menu_bar_hole != nullptr) g_ui_ops.menu_bar_hole(targets[hit].slot);
        ui_invalidate(g_state, base);
        ui_draw_and_present(nullptr);
        g_ui_touch_inside = false;
      }
    } else if ((ui_field_bool(g_state, -1, "__textbox_multiline", false) ||
                ui_field_bool(g_state, -1, "__textbox", false)) &&
               g_ui_ops.textbox_multiline_hit != nullptr) {
      // **A finger in a box of several lines puts the caret where it is touched**, with the same arithmetic the paint
      // used to put the rows down (docs/90).
      const int base = lua_gettop(g_state);
      lua_getiuservalue(g_state, base, 1);
      const char* lines = "";
      if (lua_istable(g_state, -1)) {
        lua_getfield(g_state, -1, "text");
        if (lua_isstring(g_state, -1)) lines = lua_tostring(g_state, -1);
        lua_pop(g_state, 1);
      }
      lua_pop(g_state, 1);
      const lua_Integer scale = ui_field_number(g_state, base, "scale", 3);
      const uint8_t box_scale = static_cast<uint8_t>((scale < 1) ? 1 : ((scale > 16) ? 16 : scale));
      // **La creu del botó buida la caixa i la deixa en mode edició** (0.184.0, l'amo: *«polsar aquest botó esborra el buffer i
      // activa el mode edició amb el cursor a 0 i el buffer buit del control»*). It is asked **first**, before the caret, because
      // the square is the button's and not a place in the text: a finger that lands on the cross would otherwise put the caret at
      // the end of the words it is about to erase. **A box that is being written into has no button** —its `editing` field is its
      // edit mode, and the paint hides the cross on that same field—, so the rule is one and not two (docs/30, docs/90).
      if (ui_field_bool(g_state, base, "__textbox", false) && g_ui_ops.textbox_clear_box != nullptr &&
          ui_field_bool(g_state, base, "clear", true) && !ui_field_bool(g_state, base, "editing", false)) {
        uint16_t square_x = 0;
        uint16_t square_y = 0;
        uint16_t square_w = 0;
        uint16_t square_h = 0;
        const int32_t local_x = static_cast<int32_t>(x) - static_cast<int32_t>(targets[hit].x);
        const int32_t local_y = static_cast<int32_t>(y) - static_cast<int32_t>(targets[hit].y);
        if (g_ui_ops.textbox_clear_box(targets[hit].slot, box_scale, square_x, square_y, square_w, square_h) &&
            local_x >= square_x && local_y >= square_y && local_x < square_x + square_w &&
            local_y < square_y + square_h) {
          lua_getiuservalue(g_state, base, 1);
          if (lua_istable(g_state, -1)) {
            lua_pushliteral(g_state, "");
            lua_setfield(g_state, -2, "text");
            lua_pushinteger(g_state, 0);
            lua_setfield(g_state, -2, "cursor");
          }
          lua_pop(g_state, 1);
          ui_invalidate(g_state, base);
          // **L'app ho sab abans que la banda torni** (0.184.0): the box is empty and the page is about to be built again —a band
          // that arrives resizes the view and an app that keeps its own copy of the words builds its page from that copy—, so
          // `on_change` with the empty text goes **first**. It used to go last, after the band: the timer's note box was emptied,
          // the band came, the app rebuilt the page from its own note —still the old words— and the empty box a person had just
          // asked for came back with the text it had (measured on the emulator, docs/30, docs/90).
          ui_call_hook_text(g_state, base, "on_change", "");
          // **El focus, la banda i el mode edició són del sistema**: the box the finger emptied is the one a person is writing
          // into from now on, so the ring goes to it, `editing` says it, the band comes for it —the same three things a tap on
          // the box does— (docs/30, docs/90).
          ui_apply_focus(g_state, targets, count, objects, hit);
          if (ui_set_field_bool(g_state, base, "editing", true)) ui_invalidate(g_state, base);
          if (!ui_field_bool(g_state, base, "readonly", false)) {
            lua_pushvalue(g_state, base);
            ui_keyboard_follow_object(g_state, lua_gettop(g_state));
            lua_pop(g_state, 1);
          }
          ui_keys_follow_the_control(g_state, base);
          ui_draw_and_present("gl16");
          lua_settop(g_state, top);
          return;
        }
      }
      // **El dit dins una caixa d'una línia posa el cursor on toca** (0.179.77, i el camí que hi porta, 0.179.81): a box of one
      // line has no page to scroll, so there is no gesture to wait for and the column goes in on the way down —only the column,
      // because a box of one line has no rows—. **The gate of this branch asked for `__textbox_multiline`**, so the road written
      // for the box of one line was behind a door it could not open and the finger put nothing (docs/30, docs/90).
      if (ui_field_bool(g_state, base, "__textbox", false) && g_ui_ops.textbox_column_at != nullptr) {
        const uint16_t column = g_ui_ops.textbox_column_at(
            targets[hit].slot, lines, box_scale, static_cast<uint16_t>(x - targets[hit].x));
        if (ui_set_field_number(g_state, base, "cursor", column)) ui_invalidate(g_state, base);
        lua_settop(g_state, top);
        return;
      }
      const lua_Integer cursor = ui_field_number(g_state, base, "cursor", 0);
      const bool wrap = ui_field_bool(g_state, base, "wrap", false);
      // **El camp `top` de la caixa no és la pila** (0.179.81): this was `const lua_Integer top`, and the `lua_settop` of the way
      // out of the branch read a row of the box where it wanted the number of values on the stack.
      const lua_Integer first_row = ui_field_number(g_state, base, "top", -1);
      const uint16_t at = g_ui_ops.textbox_multiline_hit(
          targets[hit].slot, lines, static_cast<uint16_t>((cursor < 0) ? 0 : cursor), wrap, box_scale,
          static_cast<uint16_t>(x - targets[hit].x), static_cast<uint16_t>(y - targets[hit].y),
          static_cast<int16_t>((first_row < -1) ? -1 : ((first_row > 4096) ? 4096 : first_row)));
      // **A box that has registered the gesture does not move the caret on the way down** (0.88.0): the finger that has
      // touched the letter and the one that is starting a swipe are the same at this moment, and the caret of a tap goes
      // in on the way up —or does not go in at all if the finger has just travelled half a page—. The caret that waits and
      // the start of the gesture are the system's, and they are cleared when a page comes in and when it goes
      // (docs/30, docs/90).
      if (ui_swipeable(g_state, base)) {
        g_ui_swipe_y0 = y;
        g_ui_swipe_y = y;
        g_ui_swipe_rest = 0;
        g_ui_swipe_moved = false;
        g_ui_swipe_painted_ms = 0;
        g_ui_swipe_tap = at;
        g_ui_swipe_tap_armed = true;
        // The press a finger makes on a box is its own state and not a button's: nothing is held and nothing is painted
        // as held, so the page keeps the pixels it had until the finger says which of the two it is (docs/90).
        // **I LA BANDA NO ARRIBA ARA** (0.179.35): it was asked here for the whole of 0.91.0, and the band **resizes the
        // page** —the view loses the bottom of the glass and the box measures its rows again—, which is a swipe that dies
        // where it began: the targets of the page are new and the rest of the drag finds no control. It arrives at the lift,
        // when the contact is known to be a tap and not a drag (found by the owner in the editor, 12/11, docs/30, docs/90).
        lua_settop(g_state, top);
        return;
      }
      lua_settop(g_state, base);
      lua_getiuservalue(g_state, base, 1);
      if (lua_istable(g_state, -1)) {
        lua_pushinteger(g_state, at);
        lua_setfield(g_state, -2, "cursor");
      }
      lua_pop(g_state, 1);
      ui_invalidate(g_state, base);
      // **The caret that moved is a paint** (0.85.2): a finger on the text is not a letter, so the box has no
      // `on_change` to wake the app with and the page would keep the caret where the keyboard left it. The system moved
      // it, so the system presents, and what a box of text wants is the clean that does not blink: `du` would leave the
      // caret of the row before on the glass (docs/30, docs/90, docs/92).
      ui_draw_and_present("gld16");
    } else if (ui_field_bool(g_state, -1, "__notifications", false) &&
               g_ui_ops.notification_bar_card_at != nullptr && g_ui_ops.notification_bar_close_at != nullptr) {
      // **The cross dismisses on the way down and the rest of a card opens on the way up**: dismissing is a command and not a
      // press state, and a card that is opened is a choice like every other one of this system (docs/90).
      const int base = lua_gettop(g_state);
      UIOps::UINotification cards[kMaxNotifications];
      uint8_t shown = 0;
      const uint16_t total = ui_notifications(g_state, base, cards, shown);
      const lua_Integer scale = ui_field_number(g_state, base, "scale", 2);
      const uint8_t band_scale = static_cast<uint8_t>((scale < 1) ? 1 : scale);
      const uint16_t local_x = static_cast<uint16_t>(x - targets[hit].x);
      const uint16_t local_y = static_cast<uint16_t>(y - targets[hit].y);
      const uint8_t card =
          (total > 0)
              ? g_ui_ops.notification_bar_card_at(targets[hit].slot, shown, total, band_scale, local_x, local_y)
              : 0;
      const bool on_cross =
          (card > 0) && g_ui_ops.notification_bar_close_at(targets[hit].slot, shown, total, band_scale, local_x, local_y);
      lua_settop(g_state, base);
      if (card > 0) {
        // The card counted from the bottom is the item counted from the top: the newest is the last one the app added.
        const lua_Integer item = static_cast<lua_Integer>(total) - card + 1;
        if (on_cross) {
          ui_call_hook_number(g_state, base, "on_close", item);
        } else {
          lua_getiuservalue(g_state, base, 1);
          if (lua_istable(g_state, -1)) {
            lua_pushinteger(g_state, item);
            lua_setfield(g_state, -2, "index");
          }
          lua_pop(g_state, 1);
          ui_invalidate(g_state, base);
        }
      }
    } else if (ui_field_bool(g_state, -1, "__icon_grid", false) && g_ui_ops.icon_grid_hit != nullptr) {
      // **A finger on a grid picks the cell it is on and not the one the dial was on**, and the release chooses it: the cells
      // are the control's, and a person who presses the third cell gets the third cell (docs/90).
      const int base = lua_gettop(g_state);
      UIOps::UIGridItem cells[kMaxGridCells];
      const uint8_t count = ui_grid_items(g_state, base, cells);
      const lua_Integer index = ui_field_number(g_state, base, "index", 0);
      const lua_Integer columns = ui_field_number(g_state, base, "columns", 0);
      const lua_Integer scale = ui_field_number(g_state, base, "scale", 2);
      const uint8_t which = g_ui_ops.icon_grid_hit(targets[hit].slot, cells, count,
                                                   static_cast<uint8_t>((index < 0) ? 0 : index),
                                                   static_cast<uint8_t>((columns < 0) ? 0 : columns),
                                                   static_cast<uint8_t>((scale < 1) ? 1 : scale),
                                                   static_cast<uint16_t>(x - targets[hit].x),
                                                   static_cast<uint16_t>(y - targets[hit].y));
      lua_settop(g_state, base);
      if (which > 0) {
        lua_getiuservalue(g_state, base, 1);
        if (lua_istable(g_state, -1)) {
          lua_pushinteger(g_state, which);
          lua_setfield(g_state, -2, "index");
        }
        lua_pop(g_state, 1);
        ui_invalidate(g_state, base);
      }
    }
    // **Una fila d'una llista no es prem: només s'activa amb un toc** (0.188.4, l'amo: *«les entrades de la llista
    // només haurien de ser sensibles al tap simple, ni al long, ni al drag ni al leave ni al down ni al swipe… només un toc
    // directe els activa amb el focus»*). The press is ink a row does not owe: it made the landing of a finger paint the row
    // —four hundred and fifty milliseconds of `gl16` **before the gesture had begun**, and on the board the loop froze
    // there—. A row is a place a person taps; what the finger does on the way down is not the row's business, and the click
    // itself does not read this field: it is the release inside the same box that activates it (docs/30, docs/90).
    // **La fila es mira per l'índex absolut de l'objecte** (0.188.8): `-1` is not the control after a branch of the chain above
    // has put the stack back, and asking the wrong value whether it is a row is a press that a row pays for anyway (measured:
    // `paint 20,376 500x64 gl16` on the landing of a finger over a row of `about`) (docs/30, docs/90).
    const bool a_row = ui_field_bool(g_state, hit_object, "__row", false);
    if (!a_row && ui_set_field_bool(g_state, hit_object, "pressed", true)) ui_invalidate(g_state, hit_object);
    // **Which control the contact is holding down** (0.183.3): the release of a press belongs to the control that took the
    // finger and not to the one under it when the finger comes up, so the slot is written here and let go in
    // `ui_touch_progress` (docs/30, docs/90).
    g_ui_touch_pressed = static_cast<int16_t>(hit);
    // **The box that took the finger is the one the keys go to** (0.91.0), and a button or a card that took it leaves
    // the keyboard behind: the band is not a page of the app and does not stay over a control that cannot type.
    ui_keyboard_follow_object(g_state, hit_object);
    lua_pop(g_state, 1);
  } else {
    // **A finger outside every control ends the edition** (0.91.0): the ring goes and the keyboard with it. The page
    // under the band was never given this finger —the keyboard took it first— so this is the empty page or a label,
    // and a person who taps there is not typing any more (docs/30, docs/90).
    // **Però un dit dins la caixa que el teclat escriu no és un dit sobre la pàgina** (0.179.83): the box of the band is not
    // always a stop of the walk —an app that turns it off for a moment, a page built again under the finger— and a tap on the
    // field a person is writing into was read as a tap on the page: the band went down and the focus with it, which is
    // *«al polsar sobre el TextBox quan ja està amb focus i el teclat està desplegat, el teclat ho entén com un blur»*. The
    // frame is the box's and what a finger inside it means is a caret or nothing; the empty page is elsewhere (docs/30, docs/90).
    bool on_the_band_box = false;
    if (g_ui_ops.keyboard_visible != nullptr && g_ui_ops.keyboard_visible() && g_ui_ops.canvas_caps != nullptr) {
      // **La marca és `top + 1` i no `top`** (0.179.84): the stops live in the table at `objects` = `top + 1`, and this block asked
      // `keyboard_object` for the box —which pushes what it finds— and then put the stack back to `top`, **taking the table of the stops
      // with it**. The `ui_apply_focus` of the line below then read a stack slot that no longer existed, and the Paper panicked with
      // `LoadProhibited` at `0x8` —`luaH_getint` ← `lua_rawgeti` ← `ui_apply_focus` ← `ui_touch_progress`, read from the backtrace of
      // the crash dump— the moment a finger left the band by tapping the page (docs/30, docs/75, docs/90).
      const int mark = lua_gettop(g_state);
      const int box = keyboard_object(g_state, /*say=*/false);
      if (box != 0) {
        LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(g_state, box, kCanvasMeta));
        uint16_t w = 0;
        uint16_t h = 0;
        uint16_t bx = 0;
        uint16_t by = 0;
        if (canvas != nullptr && g_ui_ops.canvas_caps(canvas->slot, w, h, bx, by)) {
          on_the_band_box = (x >= bx && y >= by && x < bx + w && y < by + h);
        }
      }
      lua_settop(g_state, mark);
    }
    if (!on_the_band_box) {
      if (g_ui_focus_slot >= 0) ui_apply_focus(g_state, targets, count, objects, -1);
      ui_keyboard_forget_target(g_state);
      ui_keyboard_hide();
    }
  }
  lua_settop(g_state, top);
}

// The answer of the press an app was waiting for: a table, because a press has a
// kind and how long it was held (docs/30).
void button_progress(const char* kind, int64_t held_ms, uint32_t key) {
  lua_State* L = g_state;
  if (L == nullptr) return;
  const int base = lua_gettop(L);
  lua_getfield(L, LUA_REGISTRYINDEX, kButtonKey);
  const bool waiting = lua_isuserdata(L, -1);
  lua_pushnil(L);
  lua_setfield(L, LUA_REGISTRYINDEX, kButtonKey);
  if (waiting) {
    const int p = lua_absindex(L, -1);
    lua_createtable(L, 0, 3);
    lua_pushstring(L, (kind != nullptr) ? kind : "press");
    lua_setfield(L, -2, "kind");
    lua_pushinteger(L, static_cast<lua_Integer>(held_ms));
    lua_setfield(L, -2, "held_ms");
    lua_pushinteger(L, static_cast<lua_Integer>(key));
    lua_setfield(L, -2, "key");
    promise_settle(L, p, kOk, lua_gettop(L));
  }
  lua_settop(L, base);
}

// The press that never came: the app gets the deadline and not silence (docs/30).
void button_failed(const char* code) {
  lua_State* L = g_state;
  if (L == nullptr) return;
  const int base = lua_gettop(L);
  lua_getfield(L, LUA_REGISTRYINDEX, kButtonKey);
  const bool waiting = lua_isuserdata(L, -1);
  lua_pushnil(L);
  lua_setfield(L, LUA_REGISTRYINDEX, kButtonKey);
  if (waiting) {
    const int p = lua_absindex(L, -1);
    push_error_table(L, (code != nullptr) ? code : "E_TIMEOUT", "no press came in time");
    promise_settle(L, p, kErr, lua_gettop(L));
  }
  lua_settop(L, base);
}

// on_button(kind, held_ms): the app that did not ask with a promise gets the press
// here, between two turns and with the coroutine suspended, exactly like on_alarm
// (docs/30).
bool call_button(const char* kind, int64_t held_ms, uint32_t key) {
  if (g_state == nullptr) return false;
  lua_State* L = g_state;
  const int top = lua_gettop(L);
  lua_getglobal(L, "on_button");
  if (!lua_isfunction(L, -1)) {
    lua_settop(L, top);
    return false;
  }
  lua_pushstring(L, (kind != nullptr) ? kind : "press");
  lua_pushinteger(L, static_cast<lua_Integer>(held_ms));
  // The key goes **last** on purpose: an app written for the ATOM's one button
  // declares on_button(kind) or on_button(kind, held) and keeps working (docs/30).
  lua_pushinteger(L, static_cast<lua_Integer>(key));
  if (lua_pcall(L, 3, 0, 0) != LUA_OK) {
    // **`app.exit()` des d'un ganxo no és una fallada** (0.192.49): el comiat és una intenció i el torn del nucli el
    // compleix, o sigui que el ganxo no ha de dir res i l'app s'acaba igual (docs/30, docs/90).
    if (!g_exited_by_app) {
      con_text("on_button: ");
      const char* message = lua_tostring(L, -1);
      con_text((message != nullptr) ? message : "(no message)");
      con_text("\r\n");
    }
    lua_settop(L, top);
    return false;
  }
  lua_settop(L, top);
  return true;
}

bool has_button_listener() { return g_app_button_handle != 0; }

// The system gave the app a view of another size: `on_resize(width, height)`. It is a hook and not a
// call into the coroutine for the same reason the others are —it runs between turns— and an app
// without it is legal: it will draw the same thing again on the next present, and until then the
// canvas is white (docs/30).
bool call_resize(uint16_t width, uint16_t height) {
  if (g_state == nullptr) return false;

  lua_State* L = g_state;
  const int top = lua_gettop(L);
  lua_getglobal(L, "on_resize");
  if (!lua_isfunction(L, -1)) {
    lua_settop(L, top);
    return false;
  }
  lua_pushinteger(L, static_cast<lua_Integer>(width));
  lua_pushinteger(L, static_cast<lua_Integer>(height));
  if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
    // **`app.exit()` des d'un ganxo no és una fallada** (0.192.49): el comiat és una intenció i el torn del nucli el
    // compleix, o sigui que el ganxo no ha de dir res i l'app s'acaba igual (docs/30, docs/90).
    if (!g_exited_by_app) {
      con_text("on_resize: ");
      const char* message = lua_tostring(L, -1);
      con_text((message != nullptr) ? message : "(no message)");
      con_text("\r\n");
    }
    lua_settop(L, top);
    return false;
  }
  lua_settop(L, top);
  return true;
}

// **The system went back one view**: `on_back(depth)` runs between turns, like `on_resize` and the other hooks, and
// it is where an app puts the page it left back on the glass —it has to, because the system took one of its views
// away and the page under it is the app's and not the system's—. A app without the hook is legal (nothing calls it
// and the glass keeps the page it had) and `depth` is what the app has left: zero is its only page (docs/30).
bool call_back(uint8_t depth) {
  if (g_state == nullptr) return false;

  lua_State* L = g_state;
  const int top = lua_gettop(L);
  lua_getglobal(L, "on_back");
  if (!lua_isfunction(L, -1)) {
    lua_settop(L, top);
    return false;
  }
  lua_pushinteger(L, static_cast<lua_Integer>(depth));
  if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
    // **`app.exit()` des d'un ganxo no és una fallada** (0.192.49): el comiat és una intenció i el torn del nucli el
    // compleix, o sigui que el ganxo no ha de dir res i l'app s'acaba igual (docs/30, docs/90).
    if (!g_exited_by_app) {
      con_text("on_back: ");
      const char* message = lua_tostring(L, -1);
      con_text((message != nullptr) ? message : "(no message)");
      con_text("\r\n");
    }
    lua_settop(L, top);
    return false;
  }
  lua_settop(L, top);
  return true;
}

// A lifecycle hook, for the same reason on_alarm is: it runs between turns, with
// the app's coroutine suspended, so it cannot await (docs/30 and docs/40).
bool call_hook(const char* name) {
  if (g_state == nullptr || name == nullptr) return false;

  lua_State* L = g_state;
  const int top = lua_gettop(L);
  lua_getglobal(L, name);
  if (!lua_isfunction(L, -1)) {
    lua_settop(L, top);
    return false;
  }
  if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
    con_text(name);
    con_text(": ");
    const char* message = lua_tostring(L, -1);
    con_text((message != nullptr) ? message : "(no message)");
    con_text("\r\n");
    lua_settop(L, top);
    return false;
  }
  lua_settop(L, top);
  return true;
}

// An alarm arrives between two turns, with the app's coroutine suspended: the
// function runs in the main thread, so it cannot await. That is a limit of the
// delivery, not of the app, and docs/60 says so.
// **El ganxo d'un tick** (`on_tick(payload, meta)`, 0.192.60): it is not `on_alarm` because the two are not the same thing —an
// alarm is a promise the system keeps for an app that is not running, and a tick is the app's own clock while the board is
// awake—, o sigui que una pàgina que rep les dues coses les ha de poder distingir pel nom del ganxo i no per un camp (docs/30).
// `meta` porta `id` (el que va tornar `tick.every`), `period_ms` i `late_ms`; **no hi ha `missed`**: un tick que s'ha perdut
// mentre la placa dormia no s'ha perdut, no s'havia de fer (docs/60).
bool call_tick(const alarms::Entry& entry, int64_t late_ms) {
  if (g_state == nullptr) return false;
  lua_State* L = g_state;
  const int top = lua_gettop(L);
  lua_getglobal(L, "on_tick");
  if (!lua_isfunction(L, -1)) {
    lua_settop(L, top);
    return false;
  }

  if (entry.payload_kind == alarms::Payload::kText) {
    lua_pushstring(L, entry.payload);
  } else if (entry.payload_kind == alarms::Payload::kNumber) {
    lua_pushnumber(L, atof(entry.payload));
  } else {
    lua_pushnil(L);
  }
  lua_createtable(L, 0, 3);
  lua_pushinteger(L, static_cast<lua_Integer>(entry.id));
  lua_setfield(L, -2, "id");
  lua_pushinteger(L, static_cast<lua_Integer>(entry.repeat_ms));
  lua_setfield(L, -2, "period_ms");
  lua_pushinteger(L, static_cast<lua_Integer>(late_ms));
  lua_setfield(L, -2, "late_ms");

  // **I el ganxo pot dir que no hi havia res a fer** (0.192.65): `on_tick` returning `false` means "I had nothing to add with this
  // one" —the app's loop is already sampling, or the moment was not worth a point—, and the line says so instead of leaving the
  // person with a tick that fired and silence after it. A hook that returns nothing took it, which is what every app written
  // before this did (docs/30, docs/60).
  if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
    if (!g_exited_by_app) {
      con_text("on_tick: ");
      const char* message = lua_tostring(L, -1);
      con_text((message != nullptr) ? message : "(no message)");
      con_text("\r\n");
    }
    lua_settop(L, top);
    return false;
  }
  const bool nothing = lua_isboolean(L, -1) && lua_toboolean(L, -1) == 0;
  lua_settop(L, top);
  hal::logf(pal::kLogDebug, "TICK", "#%u for %s: %s", static_cast<unsigned>(entry.id), entry.app,
            nothing ? "the app had nothing to add" : "the app answered it");
  return true;
}

bool call_alarm(const alarms::Entry& entry, int64_t late_ms) {
  if (g_state == nullptr) return false;
  // **Un tick i una alarma no es criden igual** (0.192.60): the same queue, o sigui que la porta d'entrada és la mateixa i el que
  // canvia és el ganxo (docs/30, docs/60). It also covers the rare window of a raising tick, that goes through `run_window`.
  if (entry.awake_only) return call_tick(entry, late_ms);

  lua_State* L = g_state;
  const int top = lua_gettop(L);
  lua_getglobal(L, "on_alarm");
  if (!lua_isfunction(L, -1)) {
    lua_settop(L, top);
    return false;
  }

  lua_pushinteger(L, static_cast<lua_Integer>(entry.id));
  if (entry.payload_kind == alarms::Payload::kText) {
    lua_pushstring(L, entry.payload);
  } else if (entry.payload_kind == alarms::Payload::kNumber) {
    lua_pushnumber(L, atof(entry.payload));
  } else {
    lua_pushnil(L);
  }
  lua_createtable(L, 0, 2);
  lua_pushinteger(L, static_cast<lua_Integer>(late_ms));
  lua_setfield(L, -2, "late_ms");
  lua_pushinteger(L, static_cast<lua_Integer>(entry.missed));
  lua_setfield(L, -2, "missed");

  if (lua_pcall(L, 3, 0, 0) != LUA_OK) {
    // **`app.exit()` des d'un ganxo no és una fallada** (0.192.49): el comiat és una intenció i el torn del nucli el
    // compleix, o sigui que el ganxo no ha de dir res i l'app s'acaba igual (docs/30, docs/90).
    if (!g_exited_by_app) {
      con_text("on_alarm: ");
      const char* message = lua_tostring(L, -1);
      con_text((message != nullptr) ? message : "(no message)");
      con_text("\r\n");
    }
    lua_settop(L, top);
    return false;
  }
  lua_settop(L, top);
  return true;
}

// **La trucada a la porta de l'app quan la llista d'apps canvia** (0.189.23, l'amo: *«el sistema ha d'avisar al launcher
// registrat a la variable cfg home per fer-li refescar la seva llista d'apps»*): the table it gets is the vocabulary of the
// system and not a string an app has to parse —`kind` says what the door is talking about, `what` is the verb, `id` is the
// app of the change and `generation` is the number of the list, so a page that has already answered a later change does not
// answer an older one— (docs/30, docs/40). It runs in the main thread with the app suspended, like every hook: it cannot
// await, and that is what makes it safe between two turns.
bool call_apps_event(const char* what, uint32_t generation) {
  if (g_state == nullptr) return false;

  lua_State* L = g_state;
  const int top = lua_gettop(L);
  lua_getglobal(L, "on_event");
  if (!lua_isfunction(L, -1)) {
    lua_settop(L, top);
    return false;
  }

  lua_createtable(L, 0, 3);
  lua_pushstring(L, "apps");
  lua_setfield(L, -2, "kind");
  lua_pushstring(L, (what != nullptr) ? what : "");
  lua_setfield(L, -2, "what");
  lua_pushinteger(L, static_cast<lua_Integer>(generation));
  lua_setfield(L, -2, "generation");

  if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
    // **`app.exit()` des d'un ganxo no és una fallada** (0.192.49): el comiat és una intenció i el torn del nucli el
    // compleix, o sigui que el ganxo no ha de dir res i l'app s'acaba igual (docs/30, docs/90).
    if (!g_exited_by_app) {
      con_text("on_event: ");
      const char* message = lua_tostring(L, -1);
      con_text((message != nullptr) ? message : "(no message)");
      con_text("\r\n");
    }
    lua_settop(L, top);
    return false;
  }
  lua_settop(L, top);
  return true;
}

bool run_source(const char* name, const char* source, size_t len) {
  if (g_state == nullptr) return false;
  if (g_task != nullptr) return false;  // one app at a time, always

  // **El nom amb què el tros es presenta, que és el que surt a la pàgina d'un error** (0.189.162): `/apps/brokenapp/brokenapp.lua`
  // is a path a person has to read twice, and the page of the error already says which app it is: the chunk is named after the file
  // alone, which is what the person wrote and what the line of the error has to point at (docs/30, docs/50).
  const char* chunk = name;
  if (name != nullptr) {
    const char* slash = strrchr(name, '/');
    if (slash != nullptr && slash[1] != '\0') chunk = slash + 1;
  }

  g_failed = false;  // how this app ends is what failed() will answer
  // **I el perquè de l'app d'abans no és el d'aquesta** (0.189.162), i el bloc del text torna quan l'app comença (0.190.8):
  // una placa que no ha fallat no paga els bytes d'una pàgina d'error (docs/30, docs/75).
  forget_the_error();
  g_exit_code = 0;
  g_exited_by_app = false;
  g_app_wants_to_stop = false;

  if (luaL_loadbuffer(g_state, source, len, chunk) != LUA_OK) {
    report_error();
    lua_pop(g_state, 1);
    g_failed = true;
    return false;
  }

  // **The thread, asked for through a call and not here**: a `lua_newthread` that runs out of ceiling throws
  // where nobody is listening and the process goes with it (see `make_the_task`).
  lua_pushcfunction(g_state, &make_the_task);  // the chunk, the function
  lua_pushvalue(g_state, -2);                  // the chunk, the function, the chunk
  if (lua_pcall(g_state, 1, 1, 0) != LUA_OK) { // the chunk, the thread — or the chunk, the error
    report_error();
    lua_pop(g_state, 1);  // the error
    lua_pop(g_state, 1);  // the chunk
    g_failed = true;
    return false;
  }
  lua_State* task = lua_tothread(g_state, -1);
  if (task == nullptr) {
    lua_pop(g_state, 2);  // the answer and the chunk
    g_failed = true;
    return false;
  }
  lua_pushvalue(g_state, -1);
  lua_setfield(g_state, LUA_REGISTRYINDEX, kTaskKey);  // keeps the task alive
  lua_pop(g_state, 2);                                 // the answer and the chunk
  g_task = task;
  return true;
}

bool poll() {
  if (g_state == nullptr || g_task == nullptr) return true;

  // **Un comiat demanat des d'un ganxo es compleix aquí** (0.192.49): el ganxo ja ha tornat, la pàgina no ha de córrer ni una
  // línia més, i el que queda és tancar el task com qualsevol sortida neta (docs/30, docs/90).
  if (g_app_wants_to_stop) {
    g_app_wants_to_stop = false;
    finish_task();
    return false;
  }

  settle_due();

  // A task that waits for a promise only goes on when it has an answer.
  bool ready = true;
  if (g_waiting_ref != LUA_NOREF) {
    lua_rawgeti(g_state, LUA_REGISTRYINDEX, g_waiting_ref);
    const Promise* waiting = is_promise(g_state, -1) ? promise_at(g_state, -1) : nullptr;
    ready = (waiting == nullptr) || (waiting->state != kPending);
    lua_pop(g_state, 1);
  }
  if (!ready) return true;

  int results = 0;
  // **The turn of the app is the resume**: everything it does between being woken and yielding again,
  // its own code and the calls it makes into the system. `stats` shows that total and the part that was
  // calls, and the difference is the app's own (docs/30).
  const uint32_t turn_start = (g_meter_ops.now_us != nullptr) ? g_meter_ops.now_us() : 0;
  const uint64_t calls_before = (g_meter_ops.call_us != nullptr) ? g_meter_ops.call_us() : 0;
  // **The budget is per turn**, and a call split by the previous budget yield starts its clock again
  // here, so the wait between turns is not charged to the call (docs/20, docs/30).
  g_turn_instructions = 0;
  g_budget_pending = false;
  g_budget_yielded = false;
  if (g_call_split && g_timing_call && g_meter_ops.now_us != nullptr) {
    g_call_start = g_meter_ops.now_us();
  }
  g_call_split = false;
  const int status = lua_resume(g_task, nullptr, 0, &results);
  // **What the turn still owes is paid when it ends**: a loop of three thousand instructions never
  // reaches the block the hook pays in, and an app that leaves would leave its time behind (docs/85).
  pay_the_instructions(g_pending_us);
  g_pending_us = 0;
  if (turn_start != 0 && g_meter_ops.now_us != nullptr) {
    const uint32_t elapsed = g_meter_ops.now_us() - turn_start;
    if (g_meter_ops.add_run != nullptr) g_meter_ops.add_run(elapsed);
    // **What is left when the calls into the system are taken out is the app's own code**, and it is
    // measured by difference against the running total of those calls (docs/30).
    if (g_meter_ops.add_own != nullptr && g_meter_ops.call_us != nullptr) {
      const uint64_t in_calls = g_meter_ops.call_us() - calls_before;
      g_meter_ops.add_own((in_calls < elapsed) ? static_cast<uint32_t>(elapsed - in_calls) : 0u);
    }
  }
  if (status == LUA_YIELD) {
    // **A turn yielded by the budget is not an app that forgot its promise**: it is alive, and the kernel
    // resumes it next turn (docs/20, docs/75).
    if (g_budget_yielded) return true;
    // It has to be waiting for something, or nothing would ever wake it up.
    if (g_waiting_ref != LUA_NOREF) return true;
    con_text("lua: the app yielded without a promise\r\n");
    g_failed = true;
    finish_task();
    return false;
  }

  if (status != LUA_OK) {
    // app.exit() unwinds as an error on purpose: it is a clean end, not a death.
    if (!g_exited_by_app) {
      report_task_error();
      g_failed = true;
    }
  }
  finish_task();
  return false;
}

int32_t until_next_ms() {
  if (g_state == nullptr) return -1;

  // **The block of the caret is a deadline of the kernel too** (0.92.0): an app that waits for a promise does not keep
  // the board awake, and a keyboard that blinks only when the app happens to wake is a keyboard whose cursor stops. The
  // nearest blink cuts the wait, so `caret_poll` gets its turn even when the app is idle (docs/30, docs/90).
  int32_t caret_wait = -1;
  if (g_keyboard_target_ref != LUA_NOREF && g_ui_ops.keyboard_visible != nullptr &&
      g_ui_ops.keyboard_visible()) {
    if (g_caret_dirty) {
      caret_wait = 0;
    } else if (g_caret_next_ms > 0) {
      const int64_t left = g_caret_next_ms - now_ms();
      caret_wait = (left <= 0) ? 0 : static_cast<int32_t>(left);
    }
  }

  // **A task that is not waiting for a promise is runnable**: it yielded for the turn budget, and the
  // kernel has to resume it next turn, not after the idle timeout (docs/20, docs/75).
  if (g_waiting_ref == LUA_NOREF) return 0;

  // A task that could go on right now must not be made to wait: next turn.
  if (g_waiting_ref != LUA_NOREF) {
    lua_rawgeti(g_state, LUA_REGISTRYINDEX, g_waiting_ref);
    const Promise* waiting = is_promise(g_state, -1) ? promise_at(g_state, -1) : nullptr;
    const bool settled = (waiting == nullptr) || (waiting->state != kPending);
    lua_pop(g_state, 1);
    if (settled) return 0;
  }

  const int64_t now = now_ms();
  int32_t best = -1;
  const int base = lua_gettop(g_state);
  push_pending(g_state);
  const int table = lua_absindex(g_state, -1);
  const lua_Integer count = static_cast<lua_Integer>(lua_rawlen(g_state, table));
  for (lua_Integer i = 1; i <= count; ++i) {
    lua_rawgeti(g_state, table, i);
    const Promise* p = promise_at(g_state, -1);
    if (p != nullptr && p->state == kPending && p->deadline_ms != 0) {
      int64_t left = p->deadline_ms - now;
      if (left < 0) left = 0;
      if (best < 0 || left < static_cast<int64_t>(best)) best = static_cast<int32_t>(left);
    }
    lua_pop(g_state, 1);
  }
  lua_settop(g_state, base);
  if (caret_wait >= 0 && (best < 0 || caret_wait < best)) best = caret_wait;
  return best;
}

size_t used_bytes() { return g_alloc.used; }

size_t peak_bytes() { return g_alloc.peak; }

size_t limit_bytes() { return g_alloc.limit; }

// **The slice of a turn, as the board has it** (0.174.7): the hook counts in blocks and the number it compares with is
// the block ceiling of what was asked, so `sys.turn()` and `cfg turn` say the number that is really in force and not the
// one that was typed (docs/20, docs/75).
uint32_t turn_budget() { return g_turn_budget; }

uint32_t turn_budget_default() { return kTurnBudgetDefault; }

uint32_t turn_budget_min() { return kTurnBudgetMin; }

uint32_t turn_budget_max() { return kTurnBudgetMax; }

bool set_turn_budget(uint32_t instructions) {
  if (instructions < kTurnBudgetMin || instructions > kTurnBudgetMax) return false;
  const uint32_t blocks = (instructions + kHookInstructions - 1) / kHookInstructions;
  g_turn_budget = blocks * kHookInstructions;
  return true;
}

// **El parpalleig del cursor, llegit i escrit de fora** (0.189.34): the value is the anonymous namespace's, like the slice
// of a turn, and these four are the ones `cfg` and the boot are allowed to see. What a blink may be is bounded here and
// not in the door: a cursor of ten milliseconds is a page painted for nobody and one of a minute is a cursor that looks
// dead (docs/30, docs/90).
int64_t caret_blink_ms() { return (g_caret_blink_ms > 0) ? g_caret_blink_ms : kCaretBlinkDefaultMs; }

int64_t caret_blink_default_ms() { return kCaretBlinkDefaultMs; }

int64_t caret_blink_min_ms() { return kCaretBlinkMinMs; }

int64_t caret_blink_max_ms() { return kCaretBlinkMaxMs; }

bool set_caret_blink_ms(int64_t ms) {
  if (ms < kCaretBlinkMinMs || ms > kCaretBlinkMaxMs) return false;
  g_caret_blink_ms = ms;
  return true;
}

}  // namespace luabind
