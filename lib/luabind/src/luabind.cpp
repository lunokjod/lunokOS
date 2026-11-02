// The Lua side of lunokOS: one state per app, an allocator with a ceiling, the
// few functions an app may see, and the task that runs it (docs/20, docs/30).
//
// The app is not run as one call that never returns. It runs inside a coroutine
// of its own, and a promise that has not settled yet makes it wait with a
// lua_yield; the kernel keeps turning and the task is resumed from poll() when
// what it waits for is ready. That is why there is a task here and not just a
// chunk.
#include "json/json.h"
#include "luabind/luabind.h"
#include "qr/qr.h"

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

namespace luabind {
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
const size_t kAppIdBytes = 32;
char g_app_id[kAppIdBytes] = {0};

// The most a line at the prompt may carry. It is the shell's line (128 bytes) with
// room to spare, and it is here so the interpreter can refuse a line that would not
// fit before it touches the stack (docs/30).
const size_t kPromptLineBytes = 240;

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
const char kTaskKey[] = "lunokOS.task";

// The promise the task is waiting for, as a registry reference. It is held here
// and handed to the await continuation through lua_KContext, because a promise
// that settles during the same turn must not be confused with the next one.
int g_waiting_ref = LUA_NOREF;

// How the last app ended, for whoever drove it and has to say so.
bool g_failed = false;
int g_exit_code = 0;
bool g_exited_by_app = false;  // app.exit() was called: the error is not one

// The keyboard of the app. A line is assembled here and handed to whoever is
// waiting for it; the echo is done here too, because the shell is not looking at
// the bytes while an app is alive (docs/30).
const size_t kConsoleLineCap = 128;
char g_console_line[kConsoleLineCap];
size_t g_console_len = 0;
bool g_last_was_cr = false;  // a terminal sends \r\n: the second half is not Enter
int g_read_ref = LUA_NOREF;  // the promise console.read_line() is waiting on

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

struct Promise {
  int state;
  int64_t deadline_ms;  // 0 = no timer; something else settles it
  // **When the call that made this promise started**, in microseconds, or zero when the system is not
  // metering. A call that returns a promise is not over when it returns: it is over when the promise
  // settles, and the meter reads this to know how long it took (docs/30).
  uint32_t meter_start_us;
};

void con_write(const char* text, size_t len) {
  pal::IConsole* console = pal::system().console();
  if (console != nullptr) console->write(text, len);
}

void con_text(const char* text) { con_write(text, strlen(text)); }

int64_t now_ms() { return static_cast<int64_t>(pal::system().millis()); }

// Every byte Lua asks for goes through here. If the ceiling would be crossed the
// request is refused, Lua raises a memory error and the app dies with a message
// instead of taking the system down with it.
//
// Where the block lives is not the app's problem: a big one goes to the extra memory of the board
// when it has it and a small one to the board's own RAM, and the PAL takes the other home when the
// first one is full (docs/75).
void* counting_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
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
  if (alloc->limit > 0 && next > alloc->limit) return nullptr;

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
// go, because a sleep per instruction would be a bench that asks for time more often than it simulates
// it.
const uint32_t kHookInstructions = 2000;
const uint32_t kPayThresholdUs = 500;
uint32_t g_pending_us = 0;

void pay_the_instructions(uint32_t pending) {
  if (g_meter_ops.spend_us != nullptr && pending > 0) g_meter_ops.spend_us(pending);
}

void meter_hook(lua_State* L, lua_Debug* ar) {
  (void)L;
  if (ar == nullptr) return;
  if (ar->event == LUA_HOOKCOUNT) {
    if (g_meter_ops.instruction_ns == nullptr) return;
    const uint32_t ns = g_meter_ops.instruction_ns();
    if (ns == 0) return;
    g_pending_us += (kHookInstructions * ns) / 1000u;
    if (g_pending_us >= kPayThresholdUs) {
      pay_the_instructions(g_pending_us);
      g_pending_us = 0;
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
  lua_createtable(L, 0, 6);
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
  return 1;
}

int l_sys_time_ms(lua_State* L) {
  lua_pushinteger(L, static_cast<lua_Integer>(now_ms()));
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
int l_time_caps(lua_State* L) {
  hal::IClock* clock = wall_clock();
  lua_createtable(L, 0, 3);
  lua_pushstring(L, (clock != nullptr) ? clock->source() : "none");
  lua_setfield(L, -2, "source");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "synced");
  lua_pushstring(L, (clock != nullptr && clock->can_wake()) ? "yes" : "no");
  lua_setfield(L, -2, "wake");
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

  const uint32_t id = alarms::add(spec);
  if (id == 0) return raise_coded(L, "E_NOMEM", "no room for another alarm");
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

  const uint32_t id = alarms::add(spec);
  if (id == 0) return raise_coded(L, "E_NOMEM", "no room for another alarm");
  lua_pushinteger(L, static_cast<lua_Integer>(id));
  return 1;
}

int l_alarm_cancel(lua_State* L) {
  const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
  lua_pushboolean(L, alarms::cancel(alarm_owner(), id) ? 1 : 0);
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
    lua_createtable(L, 0, 5);
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
NetOps g_net_ops = {};    // same rule: without them net.* answers E_UNSUPPORTED
PowerOps g_power_ops = {};  // and power.* too
LedOps g_led_ops = {};      // and led.*
ImuOps g_imu_ops = {};      // and imu.*
EnvOps g_env_ops = {};      // and env.*
ButtonOps g_button_ops = {};  // and button.*

int push_nil_error(lua_State* L, const char* code, const char* message) {
  lua_pushnil(L);
  push_error_table(L, code, message);
  return 2;
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
// The VFS keeps ten rows (`vfs.cpp`, kMaxMounts) and this is the same number: an app on the Paper
// with a card has nine mounts (`/littlefs`, `/apps`, `/res`, `/data`, `/sd`, `/rtc`, `/fram`, `/nvs`
// and `/cfg`), and a list that stopped at eight would hide the last two without saying so.
const uint32_t kMaxMountsInLua = 10;

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

// ---------------------------------------------------------------------------
// term: ANSI codes, and nothing at all when the console does not speak them
// ---------------------------------------------------------------------------

bool ansi_console() { return hal::platform().caps().has(hal::kCapAnsi); }

void con_num(int value) {
  char text[12];
  const int written = snprintf(text, sizeof(text), "%d", value);
  if (written > 0) con_write(text, static_cast<size_t>(written));
}

int l_term_clear(lua_State* L) {
  (void)L;
  if (ansi_console()) con_text("\x1b[2J\x1b[H");
  return 0;
}

int l_term_reset(lua_State* L) {
  (void)L;
  if (ansi_console()) con_text("\x1b[0m");
  return 0;
}

int l_term_color(lua_State* L) {
  const int fg = static_cast<int>(luaL_checkinteger(L, 1));
  if (!ansi_console()) return 0;
  con_text("\x1b[");
  con_num(fg);
  if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
    con_text(";");
    con_num(static_cast<int>(luaL_checkinteger(L, 2)));
  }
  con_text("m");
  return 0;
}

int l_term_move(lua_State* L) {
  const int row = static_cast<int>(luaL_checkinteger(L, 1));
  const int col = static_cast<int>(luaL_checkinteger(L, 2));
  if (!ansi_console()) return 0;
  con_text("\x1b[");
  con_num(row);
  con_text(";");
  con_num(col);
  con_text("H");
  return 0;
}

int l_term_hide_cursor(lua_State* L) {
  (void)L;
  if (ansi_console()) con_text("\x1b[?25l");
  return 0;
}

int l_term_show_cursor(lua_State* L) {
  (void)L;
  if (ansi_console()) con_text("\x1b[?25h");
  return 0;
}

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
  lua_pushvalue(L, p);
  return 1;
}

// How much of a page an app may hold by default. Sixteen kilobytes is a page of
// text and a quarter of the ceiling an app has (docs/75).
const lua_Integer kHttpBytesDefault = 16384;

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
  luabind::NetRefuse refused = NetRefuse::kNoRadio;
  if (g_net_ops.http_get != nullptr) {
    refused = g_net_ops.http_get(url, static_cast<uint32_t>(max_bytes),
                                 static_cast<uint32_t>(timeout));
  }
  const char* code = nullptr;
  const char* message = nullptr;
  switch (refused) {
    case NetRefuse::kOk:
      break;
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
      message = "there is no address to fetch";
      break;
    case NetRefuse::kBusy:
      code = "E_BUSY";
      message = "the network is doing something else";
      break;
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
uint8_t g_qr_modules[kQrLuaLimit];

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

  qr::Info info;
  if (!qr::encode(text, level, mode, g_qr_modules, sizeof(g_qr_modules), info)) {
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
    luaL_addchar(&buffer, g_qr_modules[i] != 0 ? '1' : '0');
  }
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
  lua_createtable(L, 0, 3);
  lua_pushinteger(L, static_cast<lua_Integer>(
                         (g_led_ops.count != nullptr) ? g_led_ops.count() : 0));
  lua_setfield(L, -2, "count");
  lua_pushinteger(L, static_cast<lua_Integer>(
                         (g_led_ops.max_value != nullptr) ? g_led_ops.max_value() : 0));
  lua_setfield(L, -2, "max");
  lua_pushboolean(L, (g_led_ops.has_colour != nullptr && g_led_ops.has_colour()) ? 1 : 0);
  lua_setfield(L, -2, "taken");
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
// a breath at five seconds is about the resting rate of a person, a blink at 800 ms is seen
// without being a strobe, and a dot of 200 ms is the Morse that a person sends (docs/30).
// The names come from the kernel, so this switch cannot list an effect the light does not
// have.
uint32_t default_period(uint8_t kind) {
  const char* name = (g_led_ops.effect_name != nullptr) ? g_led_ops.effect_name(kind) : nullptr;
  if (name == nullptr) return 1000;
  if (strcmp(name, "breathe") == 0) return 5000;
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

// How long an app waits for a press by default. Twenty seconds is a person looking
// at the board; an app that wants to wait for ever asks for a bigger number (docs/30).
const lua_Integer kButtonWaitDefaultMs = 20000;

// button.wait([ms]): a promise of `{kind = "press"|"long", held_ms}`. The press is
// delivered between turns, like an alarm, so the app is not frozen while it waits
// (docs/30).
// button.count(): how many keys this board has. Zero is a board with none, and
// it is not an error: an app asks before it shows three of them (docs/30).
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

// The five points a frame can hold, which is what the chips here report and what the kernel keeps
// (docs/50). It is not a copy of a HAL constant on purpose: the interpreter asks and the kernel
// clamps.
const uint8_t kTouchPointsInFrame = 5;

UIOps g_ui_ops = {};  // and ui.*

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

// The view as a control, defined further down with the rest of the canvas object: `present` asks it to
// draw before composing (docs/90).
int l_ui_root(lua_State* L);

// ui.present([mode]): the system composes what the app drew onto the board's surface and asks for it to
// be painted. **Without a mode each region is painted with the quality it was drawn with**; with one,
// that quality is used for everything pending, which is what a page that must look uniform wants
// (docs/30, docs/50).
int l_ui_present(lua_State* L) {
  if (g_ui_ops.present == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no display");
  }
  // **What changed is drawn first**: the view draws itself and asks its children, and a control with
  // nothing changed is not asked to draw at all. Then the tree is composed and the glass asked for the
  // paint, so an app that presents on every turn pays only for what it touched (docs/30, docs/90).
  if (l_ui_root(L) == 1) {
    lua_getfield(L, -1, "draw");
    if (lua_isfunction(L, -1)) {
      lua_pushvalue(L, -2);
      if (lua_pcall(L, 1, 0, 0) != LUA_OK) return lua_error(L);
    }
    lua_pop(L, 2);
  }
  const char* mode = luaL_optstring(L, 1, nullptr);
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

// ui.idle(): whether the paint that was asked for has finished. It is asked and not waited for,
// because the ink takes 120 to 450 ms and the app has other things to do (docs/30).
int l_ui_idle(lua_State* L) {
  if (g_ui_ops.idle == nullptr) {
    return push_nil_error(L, "E_UNSUPPORTED", "this board has no display");
  }
  lua_pushboolean(L, g_ui_ops.idle() ? 1 : 0);
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
  int8_t slot;
  uint16_t generation;
};

// **Who has the focus and what the finger is holding**, per interpreter. It is not in the Lua table
// because it is the system's state: an app may read `focused` but it does not decide it, and a slot
// only means something together with the generation that handed it out (docs/30, docs/90).
int8_t g_ui_focus_slot = -1;
uint16_t g_ui_focus_generation = 0;
int8_t g_ui_touch_slot = -1;
uint16_t g_ui_touch_generation = 0;
bool g_ui_touch_inside = false;

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
// own: the button does not keep a copy of it and does not need a field for it.
bool is_button_field(const char* key) {
  return strcmp(key, "background") == 0 || strcmp(key, "border") == 0 ||
         strcmp(key, "pressed") == 0 || strcmp(key, "focused") == 0 ||
         strcmp(key, "focusable") == 0;
}

bool button_field_valid(lua_State* L, const char* key, int value) {
  if (strcmp(key, "pressed") == 0 || strcmp(key, "focused") == 0 ||
      strcmp(key, "focusable") == 0) {
    return lua_type(L, value) == LUA_TBOOLEAN;
  }
  if (!lua_isnumber(L, value)) return false;
  const lua_Integer level = lua_tointeger(L, value);
  return level >= 0 && level <= 15;
}

int l_canvas_newindex(lua_State* L) {
  lua_getiuservalue(L, 1, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return luaL_error(L, "a canvas takes no fields");
  }
  const char* key = lua_type(L, 2) == LUA_TSTRING ? lua_tostring(L, 2) : nullptr;
  bool redraw = false;
  if (key != nullptr) {
    lua_getfield(L, -1, "__label");
    const bool label = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__button");
    const bool button = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    if (strcmp(key, "on_draw") == 0) {
      redraw = lua_type(L, 3) == LUA_TFUNCTION;
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
    } else if (button && is_button_field(key)) {
      if (!button_field_valid(L, key, 3)) {
        return luaL_error(L, "a button's %s is not a value it can use", key);
      }
      lua_pushvalue(L, 2);
      lua_rawget(L, -2);
      const bool same = lua_rawequal(L, -1, 3);
      lua_pop(L, 1);
      redraw = !same;
    }
  }
  lua_pushvalue(L, 2);
  lua_pushvalue(L, 3);
  lua_rawset(L, -3);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  // **A control that stops being focusable gives the focus back**: leaving the ring on it would be a
  // control the dialog says is out of the walk and still looks like the one Enter activates. The first
  // control that becomes focusable takes the empty focus, the same way a new button does.
  if (key != nullptr && strcmp(key, "focusable") == 0) {
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
      lua_pushboolean(L, 0);
      lua_setfield(L, -2, "focused");
      redraw = true;
    } else if (wanted && focus_gone) {
      g_ui_focus_slot = canvas->slot;
      g_ui_focus_generation = canvas->generation;
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
  int8_t slot = -1;
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
  // that is not the one being composed (found running `layers` on the bench, 1/11: nothing drew).
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

// **The `on_draw` of a label**: the fields the app put on the object are the whole state of the line, and
// the kernel is the one that owns the metrics, the ellipsis and the alignment. It draws nothing if the label
// is gone, and it returns no value: a draw is a question the presenter asks, not an answer (docs/30,
// docs/90).
int l_label_draw(lua_State* L) {
  if (!canvas_ready(L, 1)) return 0;
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
  const int color = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "background");
  const int background = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
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
    g_ui_ops.label_draw(canvas->slot, text, static_cast<uint8_t>(color),
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
  lua_pushinteger(L, 1);
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
  const bool focused = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  lua_pop(L, 1);  // the table of the object's own fields
  if (g_ui_ops.button_draw != nullptr) {
    g_ui_ops.button_draw(canvas->slot, static_cast<uint8_t>(background),
                         static_cast<uint8_t>(border), pressed, focused);
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
    lua_getiuservalue(L, -1, 1);
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "focused");
    lua_pop(L, 1);
    if (g_ui_ops.canvas_invalidate != nullptr) g_ui_ops.canvas_invalidate(created->slot);
  }
  return 1;
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

int frame_at(lua_State* L, int8_t slot, int base) {
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

int line_at(lua_State* L, int8_t slot, int base) {
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

int circle_at(lua_State* L, int8_t slot, int base, bool filled) {
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
int text_at(lua_State* L, int8_t slot, int base) {
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
  const int8_t slot = canvas_slot_of(L);
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
  if (g_ui_ops.canvas_reparent == nullptr || !g_ui_ops.canvas_reparent(child->slot, father->slot)) {
    return push_nil_error(L, "E_INVAL", "a canvas cannot go inside that one");
  }
  // The tree the drawing follows is the app's, in its own table: the kernel keeps the layers in the order
  // they were added and this keeps the same order, so a child drawn later is the one in front. The list of
  // the father it leaves keeps a stale entry, and the draw skips it because the system has already said
  // that canvas belongs to somebody else (docs/90).
  append_child(L, 1, 2);
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
        if (lua_pcall(L, 1, 0, 0) != LUA_OK) return lua_error(L);
      } else {
        lua_pop(L, 1);
      }
    }
    lua_pop(L, 1);
    if (g_ui_ops.canvas_end_draw != nullptr) g_ui_ops.canvas_end_draw(canvas->slot);
  }

  // **The children are asked even when this control has nothing to redraw**: a child that changed has to
  // paint even if its father did not. And the list is **rebuilt as it is walked**: a child that the system
  // no longer says is ours —one that went away, or one that changed father— is dropped there and then, so
  // the father's list does not keep a dead object alive and does not grow with ghosts (docs/90).
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
          if (lua_pcall(L, 2, 1, 0) != LUA_OK) return lua_error(L);
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
int l_canvas_release(lua_State* L) {
  if (!canvas_ready(L, 1)) return refuse_canvas(L);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  if (g_ui_ops.canvas_release != nullptr) g_ui_ops.canvas_release(canvas->slot);
  canvas->slot = -1;
  canvas->generation = 0;
  lua_pushboolean(L, 1);
  return 1;
}

int l_canvas_gc(lua_State* L) {
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_checkudata(L, 1, kCanvasMeta));
  if (canvas->slot >= 0 && g_ui_ops.canvas_alive != nullptr &&
      g_ui_ops.canvas_alive(canvas->slot, canvas->generation) && g_ui_ops.canvas_release != nullptr) {
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
  lua_getiuservalue(L, 1, 1);
  if (lua_istable(L, -1)) {
    lua_getfield(L, -1, "__label");
    label = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "__button");
    button = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  const char* kind = button ? "button" : (label ? "label" : "canvas");
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
  set_function(L, "place", l_canvas_place);
  set_function(L, "frame", l_canvas_frame);
  set_function(L, "line", l_canvas_line);
  set_function(L, "circle", l_canvas_circle);
  set_function(L, "disc", l_canvas_disc);
  set_function(L, "text", l_canvas_text);
  set_function(L, "font", l_canvas_font);
  set_function(L, "add", l_canvas_add);
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
  int8_t slot;
  uint16_t generation;
  int32_t x;
  int32_t y;
  int32_t w;
  int32_t h;
  int depth;
  int order;
};

const int kMaxFocusTargets = 32;

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
  const bool same = !lua_isnil(L, -1) && (lua_toboolean(L, -1) != 0) == value;
  lua_pop(L, 1);
  if (same) {
    lua_pop(L, 1);
    return false;
  }
  lua_pushboolean(L, value ? 1 : 0);
  lua_setfield(L, -2, key);
  lua_pop(L, 1);
  return true;
}

bool ui_target_is_focusable(lua_State* L, int object) {
  const bool button = ui_field_bool(L, object, "__button", false);
  return ui_field_bool(L, object, "focusable", button);
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

// Calls one of the app's hooks on a control. `focused` says whether the hook takes the extra boolean
// that `on_focus` needs; `on_click` and `on_activate` take only the control. A hook that throws is
// said on the console and does not take the app down, the same as `on_button` (docs/30).
bool ui_call_hook(lua_State* L, int object, const char* name, bool with_focus, bool focused) {
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
  if (with_focus) {
    lua_pushboolean(L, focused ? 1 : 0);
    ++arguments;
  }
  if (lua_pcall(L, arguments, 0, 0) != LUA_OK) {
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

// Walks the tree the drawing walks: the children of the app, in their order, cut at the frame of
// their fathers. A control is a target when it is a button or when the app said `focusable`; a label
// inside a button is not one, which is what makes the button the thing a person lands on.
void ui_walk_targets(lua_State* L, int object, int32_t base_x, int32_t base_y, int32_t clip_x0,
                     int32_t clip_y0, int32_t clip_x1, int32_t clip_y1, int depth,
                     FocusTarget* targets, int& count, int& order, int objects) {
  object = lua_absindex(L, object);
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(L, object, kCanvasMeta));
  if (!ui_control_alive(canvas)) return;

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
      lua_pop(L, 1);
      continue;
    }

    if (ui_target_is_focusable(L, -1) && count < kMaxFocusTargets) {
      FocusTarget& target = targets[count];
      target.slot = child->slot;
      target.generation = child->generation;
      target.x = cx0;
      target.y = cy0;
      target.w = cx1 - cx0;
      target.h = cy1 - cy0;
      target.depth = depth;
      target.order = order++;
      lua_pushvalue(L, -1);
      lua_rawseti(L, objects, count + 1);
      ++count;
    }
    ui_walk_targets(L, -1, x, y, cx0, cy0, cx1, cy1, depth + 1, targets, count, order, objects);
    lua_pop(L, 1);  // the child
  }
  lua_settop(L, base);
}

// Builds the list and **leaves the Lua objects in a table on top of the stack**: both the event and
// the thing the event changes live in Lua, and finding a slot again after a callback is a search that
// can hit a released control. The caller pops the table.
int ui_collect_targets(lua_State* L, FocusTarget* targets) {
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
  ui_walk_targets(L, -1, 0, 0, 0, 0, info.width, info.height, 0, targets, count, order, objects);
  lua_pop(L, 1);  // the root
  return count;
}

int ui_target_index(const FocusTarget* targets, int count, int8_t slot, uint16_t generation) {
  for (int i = 0; i < count; ++i) {
    if (targets[i].slot == slot && targets[i].generation == generation) return i;
  }
  return -1;
}

void ui_apply_focus(lua_State* L, const FocusTarget* targets, int count, int objects, int index) {
  const int old_index = ui_target_index(targets, count, g_ui_focus_slot, g_ui_focus_generation);
  if (old_index != index && old_index >= 0) {
    lua_rawgeti(L, objects, old_index + 1);
    if (ui_set_field_bool(L, -1, "focused", false)) {
      ui_invalidate(L, -1);
      ui_call_hook(L, -1, "on_focus", true, false);
    }
    lua_pop(L, 1);
  }
  if (index < 0 || index >= count) {
    g_ui_focus_slot = -1;
    g_ui_focus_generation = 0;
    return;
  }
  g_ui_focus_slot = targets[index].slot;
  g_ui_focus_generation = targets[index].generation;
  lua_rawgeti(L, objects, index + 1);
  if (ui_set_field_bool(L, -1, "focused", true)) {
    ui_invalidate(L, -1);
    ui_call_hook(L, -1, "on_focus", true, true);
  }
  lua_pop(L, 1);
}

bool ui_activate(lua_State* L, int count, int objects, int index) {
  if (index < 0 || index >= count) return false;
  lua_rawgeti(L, objects, index + 1);
  const int object = lua_gettop(L);
  if (ui_set_field_bool(L, object, "pressed", true)) ui_invalidate(L, object);
  if (!ui_call_hook(L, object, "on_click", false, false)) {
    ui_call_hook(L, object, "on_activate", false, false);
  }
  // The system owns the held state: a callback that returns leaves the button up. A callback that
  // released the control has no frame left to paint, and the object says so by its slot.
  LuaCanvas* canvas = static_cast<LuaCanvas*>(luaL_testudata(L, object, kCanvasMeta));
  if (ui_control_alive(canvas)) {
    if (ui_set_field_bool(L, object, "pressed", false)) ui_invalidate(L, object);
  }
  lua_pop(L, 1);
  return true;
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
  lua_createtable(L, 0, 3);
  lua_pushinteger(L, static_cast<lua_Integer>(mv));
  lua_setfield(L, -2, "mv");
  lua_pushinteger(L, static_cast<lua_Integer>(pct));
  lua_setfield(L, -2, "pct");
  lua_pushboolean(L, charging ? 1 : 0);
  lua_setfield(L, -2, "charging");
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

void open_libraries(lua_State* L) {
  luaL_requiref(L, "_G", luaopen_base, 1);
  luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
  luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
  luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
  luaL_requiref(L, LUA_COLIBNAME, luaopen_coroutine, 1);
  luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
  lua_pop(L, 6);
}

// Nothing here reaches the outside world: no files, no C modules, no way to load
// a precompiled chunk (docs/20).
void seal_globals(lua_State* L) {
  lua_pushnil(L);
  lua_setglobal(L, "dofile");
  lua_pushnil(L);
  lua_setglobal(L, "loadfile");
  lua_pushnil(L);
  lua_setglobal(L, "load");

  lua_pushcfunction(L, &l_print);
  lua_setglobal(L, "print");

  lua_createtable(L, 0, 9);
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
  set_function(L, "gc", l_sys_gc);
  set_function(L, "caps", l_sys_caps);
  set_function(L, "delay", l_sys_delay);
  lua_setglobal(L, "sys");

  // time: the wall clock. It can jump when somebody sets it; sys.time_ms() is
  // the one that never goes back (docs/60).
  lua_createtable(L, 0, 3);
  set_function(L, "now", l_time_now);
  set_function(L, "now_ms", l_time_now_ms);
  set_function(L, "caps", l_time_caps);
  lua_setglobal(L, "time");

  // alarm: what the system keeps for the app when the app is not running.
  lua_createtable(L, 0, 5);
  set_function(L, "at", l_alarm_at);
  set_function(L, "after", l_alarm_after);
  set_function(L, "cancel", l_alarm_cancel);
  set_function(L, "list", l_alarm_list);
  set_function(L, "caps", l_alarm_caps);
  lua_setglobal(L, "alarm");

  // qr: any QR this build can draw, for anything an app wants to encode.
  lua_createtable(L, 0, 2);
  set_function(L, "encode", l_qr_encode);
  set_function(L, "caps", l_qr_caps);
  lua_setglobal(L, "qr");

  // json: reading fields out of a text, with the same helpers the manifests use.
  lua_createtable(L, 0, 2);
  set_function(L, "get_string", l_json_get_string);
  set_function(L, "get_int", l_json_get_int);
  lua_setglobal(L, "json");

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

  // http: a page. One call, a promise, and the answer is the status and the body
  // (docs/30).
  lua_createtable(L, 0, 1);
  set_function(L, "get", l_http_get);
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

  lua_newtable(L);
  set_function(L, "read", l_imu_read);
  lua_setglobal(L, "imu");

  // The air: temperature and humidity, from the chip that is on some boards and not on others
  // (docs/30, docs/50).
  lua_newtable(L);
  set_function(L, "read", l_env_read);
  lua_setglobal(L, "env");
  lua_createtable(L, 0, 1);
  set_function(L, "wait", l_button_wait);
  set_function(L, "count", l_button_count);
  set_function(L, "down", l_button_down);
  set_function(L, "held", l_button_held);
  set_function(L, "info", l_button_info);
  set_function(L, "list", l_button_list);
  lua_setglobal(L, "button");

  // touch: the glass, read in a loop. caps says how big it is, read gives the fingers of the last
  // turn of the kernel and gesture gives the last shape the kernel made out of them (docs/30).
  lua_createtable(L, 0, 3);
  set_function(L, "caps", l_touch_caps);
  set_function(L, "read", l_touch_read);
  set_function(L, "gesture", l_touch_gesture);
  lua_setglobal(L, "touch");

  // ui: the view of the app. Nothing is drawn by the system: clear and rect paint on the app's own
  // canvas, present composes it onto the board's surface and says how good the paint has to look, and
  // idle says whether the glass has finished (docs/30, docs/50).
  lua_createtable(L, 0, 8);
  set_function(L, "caps", l_ui_caps);
  set_function(L, "clear", l_ui_clear);
  set_function(L, "rect", l_ui_rect);
  set_function(L, "pixel", l_ui_pixel);
  set_function(L, "present", l_ui_present);
  set_function(L, "idle", l_ui_idle);
  set_function(L, "resize", l_ui_resize);
  set_function(L, "damage", l_ui_damage);
  set_function(L, "title", l_ui_title);
  set_function(L, "rotation", l_ui_rotation);
  set_function(L, "canvas", l_ui_canvas);
  set_function(L, "control", l_ui_control);
  set_function(L, "label", l_ui_label);
  set_function(L, "button", l_ui_button);
  set_function(L, "root", l_ui_root);
  set_function(L, "frame", l_ui_frame);
  set_function(L, "line", l_ui_line);
  set_function(L, "circle", l_ui_circle);
  set_function(L, "disc", l_ui_disc);
  set_function(L, "text", l_ui_text);
  set_function(L, "text_width", l_ui_text_width);
  set_function(L, "load_font", l_ui_load_font);
  set_function(L, "font", l_ui_font);
  lua_setglobal(L, "ui");
  open_canvas_library(L);
  open_font_library(L);

  // power: the energy of the board and the sleep that ends the app (docs/30,
  // docs/90).
  lua_createtable(L, 0, 4);
  set_function(L, "caps", l_power_caps);
  set_function(L, "battery", l_power_battery);
  set_function(L, "sleep", l_power_sleep);
  set_function(L, "keep_awake", l_power_keep_awake);
  set_function(L, "cpu", l_power_cpu);
  lua_setglobal(L, "power");

  // fs: reading, writing and asking. A call that is not here is not there
  // either, so nothing is promised that the kernel does not do (docs/30).
  open_file_library(L);
  lua_createtable(L, 0, 13);
  set_function(L, "open", l_fs_open);
  set_function(L, "list", l_fs_list);
  set_function(L, "stat", l_fs_stat);
  set_function(L, "read", l_fs_read);
  set_function(L, "read_at", l_fs_read_at);
  set_function(L, "write", l_fs_write);
  set_function(L, "mkdir", l_fs_mkdir);
  set_function(L, "remove", l_fs_remove);
  set_function(L, "rename", l_fs_rename);
  set_function(L, "data", l_fs_data);
  set_function(L, "mounts", l_fs_mounts);
  set_function(L, "space", l_fs_space);
  set_function(L, "cfg", l_fs_cfg);
  lua_setglobal(L, "fs");

  lua_createtable(L, 0, 3);
  set_function(L, "print", l_print);
  set_function(L, "write", l_console_write);
  set_function(L, "read_line", l_console_read_line);
  set_function(L, "is_foreground", l_console_is_foreground);
  lua_setglobal(L, "console");

  // term: the ANSI codes, and the colour names an app may use (docs/30).
  lua_createtable(L, 0, 16);
  set_function(L, "clear", l_term_clear);
  set_function(L, "reset", l_term_reset);
  set_function(L, "color", l_term_color);
  set_function(L, "move", l_term_move);
  set_function(L, "hide_cursor", l_term_hide_cursor);
  set_function(L, "show_cursor", l_term_show_cursor);
  set_function(L, "width", l_term_width);
  set_function(L, "height", l_term_height);
  lua_pushinteger(L, 30);
  lua_setfield(L, -2, "BLACK");
  lua_pushinteger(L, 31);
  lua_setfield(L, -2, "RED");
  lua_pushinteger(L, 32);
  lua_setfield(L, -2, "GREEN");
  lua_pushinteger(L, 33);
  lua_setfield(L, -2, "YELLOW");
  lua_pushinteger(L, 34);
  lua_setfield(L, -2, "BLUE");
  lua_pushinteger(L, 35);
  lua_setfield(L, -2, "MAGENTA");
  lua_pushinteger(L, 36);
  lua_setfield(L, -2, "CYAN");
  lua_pushinteger(L, 37);
  lua_setfield(L, -2, "WHITE");
  lua_pushinteger(L, 1);
  lua_setfield(L, -2, "BOLD");
  lua_pushinteger(L, 2);
  lua_setfield(L, -2, "DIM");
  lua_setglobal(L, "term");

  // app: the identity of the app and the file it was asked to open (docs/30).
  lua_createtable(L, 0, 3);
  lua_pushstring(L, g_boot.app_id);
  lua_setfield(L, -2, "id");
  if (g_boot.args_path != nullptr || g_boot.args_restore) {
    lua_createtable(L, 0, 4);
    if (g_boot.args_path != nullptr) {
      lua_pushstring(L, g_boot.args_path);
      lua_setfield(L, -2, "path");
      lua_pushstring(L, (g_boot.args_ext != nullptr) ? g_boot.args_ext : "");
      lua_setfield(L, -2, "ext");
      lua_pushstring(L, (g_boot.args_source != nullptr) ? g_boot.args_source : "");
      lua_setfield(L, -2, "source");
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
  lua_setglobal(L, "app");
}

// Prints whatever an app threw: a string, or a {code, message} table.
void print_error_value(lua_State* L, int idx) {
  idx = lua_absindex(L, idx);
  if (lua_type(L, idx) == LUA_TSTRING) {
    size_t len = 0;
    const char* text = lua_tolstring(L, idx, &len);
    if (len > kErrorTextCap) len = kErrorTextCap;
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
    con_text("lua: ");
    if (code != nullptr) {
      con_text(code);
      con_text(": ");
    }
    con_text((message != nullptr) ? message : "(no message)");
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
}

// Everything a fresh state needs before an app can run. It is called through
// lua_pcall: opening the libraries asks for memory too, and a board with a small
// ceiling has to say no and go on, not panic.
int open_everything(lua_State* L) {
  open_libraries(L);
  open_promises(L);
  seal_globals(L);
  return 0;
}

}  // namespace

bool start(const Boot& boot, size_t limit_bytes) {
  if (g_state != nullptr) return false;

  g_boot = boot;
  {
    const char* id = (boot.app_id != nullptr) ? boot.app_id : "";
    size_t i = 0;
    for (; id[i] != '\0' && i + 1 < kAppIdBytes; ++i) g_app_id[i] = id[i];
    g_app_id[i] = '\0';
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
  // **The count hook is installed only where instructions are priced**: on a board they are not, and a
  // hook that runs every two thousand instructions to do nothing is a tax on the thing being measured
  // (docs/85). A band that answers with a price is a bench that is simulating a slower processor.
  const uint32_t instruction_ns =
      (g_meter_ops.instruction_ns != nullptr) ? g_meter_ops.instruction_ns() : 0;
  if (instruction_ns > 0) {
    lua_sethook(g_state, &meter_hook, LUA_MASKCALL | LUA_MASKRET | LUA_MASKCOUNT, kHookInstructions);
  } else {
    lua_sethook(g_state, &meter_hook, LUA_MASKCALL | LUA_MASKRET, 0);
  }

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
  g_console_len = 0;
  g_last_was_cr = false;
  g_escape_count = 0;
  g_escape_first_ms = 0;
  g_exit_requested = false;
  g_failed = false;
  g_ui_focus_slot = -1;
  g_ui_focus_generation = 0;
  g_ui_touch_slot = -1;
  g_ui_touch_generation = 0;
  g_ui_touch_inside = false;
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
  g_waiting_ref = LUA_NOREF;
  g_read_ref = LUA_NOREF;
  g_console_len = 0;
  g_last_was_cr = false;
  g_escape_count = 0;
  g_escape_first_ms = 0;
  g_exit_requested = false;
  g_ui_focus_slot = -1;
  g_ui_focus_generation = 0;
  g_ui_touch_slot = -1;
  g_ui_touch_generation = 0;
  g_ui_touch_inside = false;
  lua_close(g_state);
  g_state = nullptr;
}

bool running() { return g_state != nullptr; }

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

const char* app_id() { return g_app_id; }

const char* app_path() { return g_boot.args_path; }

void set_file_ops(const FileOps& ops) { g_file_ops = ops; }

void set_net_ops(const NetOps& ops) { g_net_ops = ops; }

void set_power_ops(const PowerOps& ops) { g_power_ops = ops; }

void set_led_ops(const LedOps& ops) { g_led_ops = ops; }

void set_imu_ops(const ImuOps& ops) { g_imu_ops = ops; }

void set_env_ops(const EnvOps& ops) { g_env_ops = ops; }

void set_button_ops(const ButtonOps& ops) { g_button_ops = ops; }

// The glass, the same way: the kernel installs it at boot and the three calls of `touch` answer from
// it (docs/30, docs/50).
void set_touch_ops(const TouchOps& ops) { g_touch_ops = ops; }

void set_meter_ops(const MeterOps& ops) { g_meter_ops = ops; }

// And the panel, which the kernel installs only when the board answers with a surface (docs/50).
void set_ui_ops(const UIOps& ops) { g_ui_ops = ops; }

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
void http_progress(bool ok, const char* code, int status, const char* body, uint32_t len) {
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
      if (body != nullptr) {
        lua_pushlstring(L, body, static_cast<size_t>(len));
      } else {
        lua_pushliteral(L, "");
      }
      lua_setfield(L, -2, "body");
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



// **The dial walks the controls.** The spots come from the board, not from the number of the key:
// up/left is previous, down/right is next, centre is enter. A board with one key has only the centre.
bool ui_button_event(const char* kind, uint32_t key) {
  if (g_state == nullptr || kind == nullptr || strcmp(kind, "press") != 0) return false;
  FocusTarget targets[kMaxFocusTargets];
  const int top = lua_gettop(g_state);
  const int count = ui_collect_targets(g_state, targets);
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
  const int current = ui_target_index(targets, count, g_ui_focus_slot, g_ui_focus_generation);
  int target = -1;
  if (activate) {
    target = (current >= 0) ? current : 0;
    if (current < 0) ui_apply_focus(g_state, targets, count, objects, target);
    ui_activate(g_state, count, objects, target);
  } else {
    target = (current >= 0) ? (current + direction + count) % count : (direction > 0 ? 0 : count - 1);
    ui_apply_focus(g_state, targets, count, objects, target);
  }
  lua_settop(g_state, top);
  return true;
}

// True while a page has at least one control a person can land on. The kernel asks it before it gives
// the middle key to the three-tap exit: a page whose centre is enter cannot also close on the third
// centre press (docs/30, docs/90).
bool ui_has_focusable() {
  if (g_state == nullptr) return false;
  FocusTarget targets[kMaxFocusTargets];
  const int top = lua_gettop(g_state);
  const int count = ui_collect_targets(g_state, targets);
  lua_settop(g_state, top);
  return count > 0;
}

// **The finger is the same event as the dial**: a touch that starts on a button focuses it and holds
// it down; a move outside releases the hold, and a release inside clicks it. The frame is the one
// `touch::poll` has just put in the view's coordinates (docs/30, docs/50, docs/90).
void ui_touch_progress(uint16_t x, uint16_t y, bool down) {
  if (g_state == nullptr) return;
  // **A turn with nobody on the glass and nothing held costs nothing**: the panel is read every turn,
  // and walking the whole tree only to find that there is no finger is work the app pays for no reason.
  if (!down && g_ui_touch_slot < 0) return;
  FocusTarget targets[kMaxFocusTargets];
  const int top = lua_gettop(g_state);
  const int count = ui_collect_targets(g_state, targets);
  const int objects = top + 1;

  if (!down) {
    if (g_ui_touch_slot >= 0) {
      const int index = ui_target_index(targets, count, g_ui_touch_slot, g_ui_touch_generation);
      if (index >= 0) {
        lua_rawgeti(g_state, objects, index + 1);
        if (ui_set_field_bool(g_state, -1, "pressed", false)) ui_invalidate(g_state, -1);
        lua_pop(g_state, 1);
        if (g_ui_touch_inside) ui_activate(g_state, count, objects, index);
      }
      g_ui_touch_slot = -1;
      g_ui_touch_generation = 0;
      g_ui_touch_inside = false;
    }
    lua_settop(g_state, top);
    return;
  }

  if (g_ui_touch_slot >= 0) {
    const int index = ui_target_index(targets, count, g_ui_touch_slot, g_ui_touch_generation);
    const bool inside =
        index >= 0 && x >= targets[index].x && y >= targets[index].y &&
        x < targets[index].x + targets[index].w && y < targets[index].y + targets[index].h;
    if (inside != g_ui_touch_inside) {
      if (index >= 0) {
        lua_rawgeti(g_state, objects, index + 1);
        if (ui_set_field_bool(g_state, -1, "pressed", inside)) ui_invalidate(g_state, -1);
        lua_pop(g_state, 1);
      }
      g_ui_touch_inside = inside;
    }
    lua_settop(g_state, top);
    return;
  }

  const int hit = ui_hit(targets, count, x, y);
  if (hit >= 0) {
    ui_apply_focus(g_state, targets, count, objects, hit);
    g_ui_touch_slot = targets[hit].slot;
    g_ui_touch_generation = targets[hit].generation;
    g_ui_touch_inside = true;
    lua_rawgeti(g_state, objects, hit + 1);
    if (ui_set_field_bool(g_state, -1, "pressed", true)) ui_invalidate(g_state, -1);
    lua_pop(g_state, 1);
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
    con_text("on_button: ");
    const char* message = lua_tostring(L, -1);
    con_text((message != nullptr) ? message : "(no message)");
    con_text("\r\n");
    lua_settop(L, top);
    return false;
  }
  lua_settop(L, top);
  return true;
}

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
    con_text("on_resize: ");
    const char* message = lua_tostring(L, -1);
    con_text((message != nullptr) ? message : "(no message)");
    con_text("\r\n");
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
bool call_alarm(const alarms::Entry& entry, int64_t late_ms) {
  if (g_state == nullptr) return false;

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
    con_text("on_alarm: ");
    const char* message = lua_tostring(L, -1);
    con_text((message != nullptr) ? message : "(no message)");
    con_text("\r\n");
    lua_settop(L, top);
    return false;
  }
  lua_settop(L, top);
  return true;
}

bool run_source(const char* name, const char* source, size_t len) {
  if (g_state == nullptr) return false;
  if (g_task != nullptr) return false;  // one app at a time, always

  g_failed = false;  // how this app ends is what failed() will answer
  g_exit_code = 0;
  g_exited_by_app = false;

  if (luaL_loadbuffer(g_state, source, len, name) != LUA_OK) {
    report_error();
    lua_pop(g_state, 1);
    g_failed = true;
    return false;
  }

  lua_State* task = lua_newthread(g_state);  // stack: chunk, thread
  lua_pushvalue(g_state, -2);
  lua_xmove(g_state, task, 1);               // the chunk moves into the task
  lua_setfield(g_state, LUA_REGISTRYINDEX, kTaskKey);  // keeps the task alive
  lua_pop(g_state, 1);                       // the copy left on the main stack
  g_task = task;
  return true;
}

bool poll() {
  if (g_state == nullptr || g_task == nullptr) return true;

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
  return best;
}

size_t used_bytes() { return g_alloc.used; }

size_t peak_bytes() { return g_alloc.peak; }

size_t limit_bytes() { return g_alloc.limit; }

}  // namespace luabind
