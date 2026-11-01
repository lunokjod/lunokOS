// The interpreter, as seen from the kernel.
//
// One state is created when an app starts and destroyed when it ends: nothing is
// shared between apps, and the ceiling is enforced by the allocator (docs/20 and
// docs/75). This layer knows nothing about files or the VFS on purpose: whoever
// has the source hands it in.
#ifndef LUNOK_LUABIND_LUABIND_H
#define LUNOK_LUABIND_LUABIND_H

#include <stddef.h>
#include <stdint.h>

#include "alarms/alarms.h"

namespace luabind {

// What the interpreter needs from a file system, filled in by whoever has one.
// The dependency goes this way on purpose: the kernel knows the VFS and the
// interpreter does not (docs/70), and a test can hand in its own ops.
class DirVisitor {
 public:
  virtual ~DirVisitor() {}
  // kFile, kDir as in hal::NodeType; size and mtime as the backend gives them.
  virtual void visit(const char* name, int type, uint32_t size, int64_t mtime) = 0;
};

// Why a file operation failed, in the words an app can test (docs/30).
enum class FsError : uint8_t {
  kOk = 0,
  kNoEnt,       // there is no such file or directory
  kInval,       // the path is not one this app may use
  kNoSpc,       // the room this app has is not enough
  kTooBig,      // a path or a name that does not fit
  kNoHandles,   // every file this system can have open at once is taken
  kUnsupported,  // the backend does not know how
  kUnavail      // what was asked for is not on this board right now
};

// A mount as the interpreter sees it: the name, the kind, and whether it takes
// writes. The room is not here on purpose: asking a backend for it walks the
// file system, and a list of mounts must not cost that (docs/70). fs.space(path)
// is the call that asks.
struct MountInfo {
  char path[16];
  char kind[12];
  bool writable;
};

struct FileOps {
  // Reads at most len bytes at offset into buf. Returns what it read, or -1.
  int32_t (*read_at)(const char* path, int64_t offset, char* buf, uint32_t len, FsError& err);
  // The size of a regular file, or -1 when it is not one.
  int64_t (*size)(const char* path, FsError& err);
  // True when the path is a directory.
  bool (*is_dir)(const char* path, FsError& err);
  // Lists a directory, handing every entry to the visitor. False when it cannot.
  bool (*list)(const char* path, DirVisitor& visitor, FsError& err);
  // Writes a whole file: either the new contents are there or the old ones are
  // untouched, never half of each (docs/70).
  int64_t (*write_atomic)(const char* path, const char* data, size_t len, FsError& err);
  bool (*mkdir)(const char* path, FsError& err);
  bool (*remove)(const char* path, FsError& err);
  bool (*rename)(const char* from, const char* to, FsError& err);

  // An open file. The interpreter keeps the handle in a userdata and the kernel
  // keeps the file; the pool is what makes the ninth open fail with E_NO_HANDLES
  // instead of taking memory nobody accounted for (docs/70).
  int32_t (*open)(const char* path, const char* mode, FsError& err);
  int32_t (*file_read)(int32_t handle, char* buf, uint32_t len, FsError& err);
  int64_t (*file_write)(int32_t handle, const char* data, size_t len, FsError& err);
  int64_t (*file_position)(int32_t handle, FsError& err);
  bool (*file_seek)(int32_t handle, int64_t position, FsError& err);
  int64_t (*file_size)(int32_t handle, FsError& err);
  bool (*file_close)(int32_t handle, FsError& err);

  // The mounts, in the order the kernel keeps them. Returns how many were
  // written, which is at most max.
  uint32_t (*mounts)(MountInfo* out, uint32_t max);
  // Room on the mount a path lives on, in bytes. False when the backend cannot
  // say it, and that is not the same as there being none (docs/70).
  bool (*space)(const char* path, uint64_t& total, uint64_t& free_bytes, FsError& err);

  // Where the settings go: the unit behind /cfg, which is "sd" or "littlefs". An app
  // does not need to know it —it writes /cfg/... and the kernel decides— but the person
  // has to be able to ask and to change it, and that is what these three are for
  // (docs/30, docs/70).
  const char* (*cfg_unit)();
  bool (*cfg_set_unit)(const char* unit, FsError& err);
  // The real directory this app's settings land in, for showing: "/sd/cfg/<id>".
  int32_t (*cfg_dir)(char* out, size_t cap, FsError& err);
};

// Installs the file operations. Without them fs.* answers E_UNSUPPORTED.
void set_file_ops(const FileOps& ops);

// What net.caps() may say. A bit is only promised when the board really has it
// (docs/30, docs/50).
enum NetFeature : uint32_t {
  kNetWifi = 1u << 0,
  kNetHttp = 1u << 1,
  kNetTls = 1u << 2,
  kNetProv = 1u << 3
};

// Why a download did not go into the queue. The interpreter tells one from the
// other because they are not the same mistake: an address that does not fit is
// the app's, and a busy radio is not (docs/30).
enum class NetRefuse : uint8_t {
  kOk = 0,
  kNoRadio,  // this board has no network at all
  kBusy,     // the radio is doing something else, or the queue is full
  kTooBig,   // the address is longer than the queue carries
  kInval     // nothing to ask for
};

// The network as the interpreter uses it: **start it and let go**. There is no
// blocking call here, and no timeout either: the kernel keeps the deadline and
// answers with net_progress when it is done (docs/30).
struct NetOps {
  bool (*connect)(const char* ssid, const char* pass, uint32_t timeout_ms);
  // Connects with the network the board has stored, so an app does not need to
  // know the password after provisioning (docs/50).
  bool (*connect_stored)(uint32_t timeout_ms);
  // Hands the network to the board from outside and waits for the station to be
  // up. False when there is no radio, one thing is already running, or the
  // board cannot start (docs/50).
  bool (*provision)(const char* pop, const char* service_name, uint32_t timeout_ms);
  bool (*provisioned)();
  // Puts a network in the board's store and starts the association with it. False
  // when there is no radio, or when the store refuses (docs/50).
  bool (*add_network)(const char* ssid, const char* pass, uint32_t timeout_ms);
  // Starts a scan of the networks around. False when there is no radio or the
  // radio is busy; the count arrives with net_progress (docs/50).
  bool (*scan_start)(uint32_t timeout_ms);
  // Writes the network the board has stored into out. False when there is none.
  bool (*stored_ssid)(char* out, size_t cap);
  // One entry of the last finished scan, for an app that wants to show a list.
  bool (*scan_record)(uint32_t index, char* ssid, size_t cap, int32_t& rssi);
  bool (*forget)();
  // Sets the clock from a time server; the answer is `true` when the clock is set
  // (docs/30, docs/60).
  bool (*ntp_start)(const char* server, uint32_t timeout_ms);
  // Downloads a page. It needs the network like the time server does, and the
  // answer is a status and a body, not a yes (docs/30).
  NetRefuse (*http_get)(const char* url, uint32_t max_bytes, uint32_t timeout_ms);
  void (*cancel)();  // the app that asked is gone
  bool (*disconnect)();
  // 0 off, 1 connecting, 2 up, 3 failed: hal::NetState as a number, so that this
  // library keeps knowing nothing about the HAL.
  uint8_t (*state)();
  uint32_t (*features)();
};

void set_net_ops(const NetOps& ops);

// The power of the board, as the interpreter uses it. The sleep mode is a number
// (0 light, 1 deep) so that this library keeps knowing nothing about the HAL, the
// same way `state` is (docs/30).
struct PowerOps {
  bool (*can_sleep)(uint8_t mode);
  // Blocks until the board is back and **never returns from a deep sleep**: the
  // board restarts and the app comes back from the session record (docs/90).
  bool (*sleep)(uint8_t mode, int64_t at_epoch_ms);
  // False when the board has no gauge, which is not the same as empty (docs/75).
  bool (*battery)(int& mv, int& pct, bool& charging);
  bool (*keep_awake)(bool on);
  bool (*keeping_awake)();
  const char* (*wakes)();
  // The processor speed: what it is, and the change to a step the board has. A
  // step it does not have is refused, never rounded (docs/30).
  uint32_t (*cpu_mhz)();
  bool (*set_cpu_mhz)(uint32_t mhz);
};

void set_power_ops(const PowerOps& ops);

// One effect of the light, as an app asks for it. Plain numbers and not the kernel's
// struct, so the interpreter and the policy do not have to share a header for one
// message: the numbers are the ones in `kernel/led.h` and the names are the same on both
// sides (docs/30).
struct LedEffectSpec {
  uint8_t kind;  // 0 none, 1 breathe, 2 blink, 3 sos, 4 bounce
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  // The other end of the ramp the effect runs along its cycle (docs/30).
  uint8_t red2;
  uint8_t green2;
  uint8_t blue2;
  uint32_t period_ms;
  uint32_t cycles;  // 0 = until it is stopped
  int32_t pixel;    // -1 = the whole light
  uint8_t level;    // the brightest this effect may get
};

// The light of the board, as the interpreter uses it. An app takes a colour or plays an
// effect while it is in front and gives it back when it goes; the system's four effects
// are the app's to redefine and not the kernel's to keep (docs/30).
struct LedOps {
  uint32_t (*count)();
  // The highest a channel may be driven at on this board (docs/30).
  uint32_t (*max_value)();
  bool (*set_colour)(uint8_t r, uint8_t g, uint8_t b);
  // One light of a strip, for a board with more than one (docs/30).
  bool (*set_pixel)(uint32_t index, uint8_t r, uint8_t g, uint8_t b);
  void (*clear_colour)();
  bool (*has_colour)();
  // An effect that runs on the turn clock and never blocks (docs/30).
  bool (*play)(const LedEffectSpec& effect);
  // Give it back: now, or fading to black over the milliseconds asked for (docs/30).
  void (*stop)(uint32_t fade_ms);
  // The effect the app is playing, if any, and how long it has been playing (docs/30).
  bool (*effect)(LedEffectSpec& out, uint32_t& elapsed_ms);
  // The system's four effects: read one, change it, put the maker's back (docs/30).
  bool (*system_effect)(uint8_t trigger, LedEffectSpec& out);
  bool (*set_system_effect)(uint8_t trigger, const LedEffectSpec& effect);
  void (*restore_system_effect)(uint8_t trigger);
  // The names of both, so the interpreter does not keep a second list of them that can
  // disagree with the kernel's (docs/30).
  const char* (*effect_name)(uint8_t kind);
  bool (*effect_from_name)(const char* name, uint8_t& kind);
  const char* (*trigger_name)(uint8_t trigger);
  bool (*trigger_from_name)(const char* name, uint8_t& trigger);
};

void set_led_ops(const LedOps& ops);

// The ambient sensor, as the interpreter uses it. A board without one leaves the operation absent
// and env.read() answers E_UNSUPPORTED (docs/30, docs/50).
struct EnvOps {
  bool (*read)(float& celsius, float& humidity);
};

void set_env_ops(const EnvOps& ops);

// The inertial sensor, as the interpreter uses it. A board without one leaves the
// operation absent and imu.read() answers E_UNSUPPORTED (docs/30, docs/50).
struct ImuOps {
  bool (*read)(float& ax, float& ay, float& az, float& gx, float& gy, float& gz,
               float& celsius);
};

void set_imu_ops(const ImuOps& ops);

// The button, as the interpreter uses it: an app asks for the next press as a
// promise (docs/30).
// What a key is and where it is, said in words: the interpreter does not need the
// enumerations of the HAL, and an app is not going to compare numbers (docs/30).
struct KeyInfo {
  uint32_t key;
  const char* face;    // "front", "back", "left", "right", "top", "bottom"
  const char* spot;    // "centre", "up", "down", "left", "right"
  const char* kind;    // "simple" or "compound"
  uint32_t group;      // keys of the same physical component share it
};

struct ButtonOps {
  bool (*wait)(uint32_t timeout_ms);
  void (*cancel)();
  bool (*available)();
  // Several keys: the board says how many, where each one is and what it is doing
  // now, so a person can see the key before blaming the app and an app can give a
  // function to a position (docs/30, docs/50).
  uint32_t (*count)();
  bool (*down)(uint32_t key);
  int64_t (*held)(uint32_t key);
  bool (*info)(uint32_t key, KeyInfo& out);
};

void set_button_ops(const ButtonOps& ops);

// The glass, as the interpreter uses it: how big it is, the fingers that are down and the last
// gesture (docs/30, docs/50). Plain numbers: the interpreter does not need the HAL and an app is not
// going to compare enumerations. **There is no promise**, and it is on purpose: a finger is a state
// read in a loop, not a thing an app waits for, and an app that waits for a touch is an app that does
// nothing else.
struct TouchPointInfo {
  uint8_t id;
  uint16_t x;
  uint16_t y;
  uint16_t size;
};

struct TouchInfo {
  uint8_t max_points;
  uint16_t width;
  uint16_t height;
};

struct TouchOps {
  bool (*caps)(TouchInfo& out);
  // The frame of the last turn of the kernel: the chip gives a frame once and it has already been
  // taken, so this is not a second read of the glass (docs/30).
  uint8_t (*points)(TouchPointInfo* out, uint8_t capacity);
  // The last gesture, taken once. False when there is none.
  bool (*gesture)(const char** kind, uint16_t& x, uint16_t& y, int16_t& dx, int16_t& dy,
                  int16_t& spread, int32_t& held_ms, uint8_t& fingers);
};

void set_touch_ops(const TouchOps& ops);

// The view of the app, as the interpreter uses it: the canvas it draws on and the moment it asks the
// system to put it on the glass. **The system never draws**: every call here comes from an app, and
// the panel's own buffer is not reachable from Lua (docs/30, docs/50).
struct UIInfo {
  uint16_t width;
  uint16_t height;
  uint8_t levels;
  // Where the view sits on the glass. A canvas is not the panel: it can be smaller and somewhere else,
  // and an app that wants to place something relative to the glass needs to know (docs/30).
  uint16_t x;
  uint16_t y;
};

struct UIOps {
  bool (*caps)(UIInfo& out);
  // The last argument is the quality the app asked for, or nullptr for the default. False when the name
  // is not one of this glass, which is reported and not rounded to another one (docs/30).
  bool (*clear)(uint8_t level, const char* mode);
  bool (*rect)(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t level, const char* mode);
  bool (*pixel)(uint16_t x, uint16_t y, uint8_t level, const char* mode);
  // nullptr is "each region with the quality it was drawn with" (docs/30).
  bool (*present)(const char* mode);
  bool (*idle)();
  // Another size for the view. The system answers by making a new canvas and calling `on_resize`, and
  // an app that asks for one it cannot have is told no (docs/30).
  bool (*resize)(uint16_t width, uint16_t height);
  // How many regions are waiting for a present (docs/30).
  uint8_t (*damage)();
  // Why the last paint was refused, for the message an app reads (docs/50).
  const char* (*last_error)();
  // The title this app shows in the bar of the system, which is not the app's (docs/30).
  bool (*title)(const char* text);
  // The turn of the page: 0, 90, 180 or 270. Reading it and setting it are the same value (docs/50).
  uint16_t (*rotation)();
  bool (*set_rotation)(uint16_t degrees);
  // **A canvas of the app's own**, the control layer's basic thing (docs/90): a buffer, its place
  // inside the parent, and a number that says whether the object an app is holding still means the
  // canvas that is there —a view made again hands the slot to somebody else, and a stale object has to
  // be refused instead of drawing into it—.
  bool (*canvas_create)(uint16_t width, uint16_t height, bool with_view, int8_t& slot,
                        uint16_t& generation);
  bool (*canvas_alive)(int8_t slot, uint16_t generation);
  bool (*canvas_release)(int8_t slot);
  bool (*canvas_place)(int8_t slot, uint16_t x, uint16_t y);
  bool (*canvas_clear)(int8_t slot, uint8_t level, const char* mode);
  bool (*canvas_rect)(int8_t slot, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t level,
                      const char* mode);
  bool (*canvas_pixel)(int8_t slot, uint16_t x, uint16_t y, uint8_t level, const char* mode);
  uint8_t (*canvas_damage)(int8_t slot);
  bool (*canvas_caps)(int8_t slot, uint16_t& width, uint16_t& height, uint16_t& x, uint16_t& y);
  // **The primitives of a canvas**, and the text with the font of the system. `slot` 0 is the view of the
  // app, so `ui.line` and `cv:line` are the same call with a different canvas. `level` is the colour —0
  // black, 15 white— and `mode` the quality, or nullptr for `du` (docs/30, docs/90).
  bool (*canvas_frame)(int8_t slot, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t level,
                       const char* mode);
  bool (*canvas_line)(int8_t slot, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t level,
                      const char* mode);
  bool (*canvas_circle)(int8_t slot, int16_t center_x, int16_t center_y, int16_t radius,
                        uint8_t level, const char* mode);
  bool (*canvas_disc)(int8_t slot, int16_t center_x, int16_t center_y, int16_t radius,
                      uint8_t level, const char* mode);
  bool (*canvas_text)(int8_t slot, int16_t x, int16_t y, const char* text, uint8_t scale,
                      uint8_t level, const char* mode);
  // **A canvas inside another one** (docs/90): one father, and the children in the order they were added,
  // which is the order of the layers.
  bool (*canvas_reparent)(int8_t slot, int8_t parent);
  // The canvas that holds this one, or -1 for the view of the app (docs/90).
  int8_t (*canvas_parent)(int8_t slot);
  // **Redrawing, only when it is needed** (docs/90): whether a control has anything to redraw, that it has
  // been drawn, that something changed and it and its fathers must draw again, and the number of the
  // canvas a slot holds —so the object of an app can tell whether it is still the one it made—.
  bool (*canvas_needs_draw)(int8_t slot);
  bool (*canvas_drawn)(int8_t slot);
  bool (*canvas_invalidate)(int8_t slot);
  uint16_t (*canvas_generation)(int8_t slot);
  // The box a control owes, and the window in which its drawing is cut at it: a control that redraws itself
  // paints the smallest part of the glass, not the page (docs/30, docs/90).
  bool (*canvas_invalidate_rect)(int8_t slot, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
  bool (*canvas_begin_draw)(int8_t slot);
  bool (*canvas_end_draw)(int8_t slot);
};

void set_ui_ops(const UIOps& ops);

// The system changed the size of the view under the app: `on_resize(width, height)` if the app has
// one. False when it does not, which is a legal app —it just draws the same thing again— and the
// canvas is white when it happens (docs/30).
bool call_resize(uint16_t width, uint16_t height);

// The press the app was waiting for arrived, or it never came. Called between
// turns, never from inside Lua (docs/30).
void button_progress(const char* kind, int64_t held_ms, uint32_t key);
void button_failed(const char* code);

// A press for an app that did not ask for it as a promise: the `on_button` hook, in
// the same shape as on_alarm, so it cannot await (docs/30).
bool call_button(const char* kind, int64_t held_ms, uint32_t key);

// The kernel says the board is back from the sleep an app asked for. Called
// between turns, never from inside Lua (docs/30).
void power_progress(bool ok);

// The kernel says how the attempt it started ended. `code` is an error code when
// `up` is false, and nullptr when it worked. Called between turns, never from
// inside Lua. For a scan, `count` carries how many networks were found.
// The answer of the one network job that was waiting. `count` is the number of
// networks a scan found, or -1 for the jobs that answer `true` (connecting,
// provisioning): a scan that finds nothing answers 0 and not `true`, because an
// app loops over that number and `for i = 1, true` is a Lua error (measured on
// the board, 30/10).
void net_progress(bool up, const char* code, int32_t count = -1);

// The same, for a page, which is not a yes or a number: the promise is settled
// with `{status, body, bytes}` when it arrived, and with an error table when it
// did not. `body` may be nullptr and is only read when `ok` is true (docs/30).
void http_progress(bool ok, const char* code, int status, const char* body, uint32_t len);


struct Boot {
  Boot()
      : board("?"),
        version("0.0.0"),
        emulator(false),
        window(false),
        app_id(""),
        app_title(""),
        app_version(""),
        app_opens(""),
        args_path(nullptr),
        args_ext(nullptr),
        args_source(nullptr),
        args_restore(false) {}

  const char* board;       // the board name from platformio.ini, or "emulator"
  const char* version;     // lunokOS version
  bool emulator;
  // True when this state was made for an alarm window: the app is not in front,
  // nobody is reading the keyboard and console.is_foreground() says false.
  bool window;
  const char* app_id;      // the directory name of the app (app.id)
  const char* app_title;   // its title, for app.info()
  const char* app_version; // its version
  const char* app_opens;   // the extension it handles, or ""
  const char* args_path;   // the file it was asked to open, or null
  const char* args_ext;    // that file's extension, normalised, or null
  const char* args_source; // "open" when it came from a file, else null
  // True when the system is bringing the app back from a sleep, so the app knows
  // to read what it saved before going down (docs/30, docs/90).
  bool args_restore;
};

// Creates the state and opens the libraries an app may see.
// limit_bytes is a hard ceiling for everything the interpreter asks for.
bool start(const Boot& boot, size_t limit_bytes);

void stop();

// True while a state exists, which is from start() to stop().
bool running();

// Compiles a chunk and gives it a task of its own. Nothing of the chunk runs
// here: the first turn of the app is the first call to poll().
// Returns false if the source does not compile; the message is already out.
bool run_source(const char* name, const char* source, size_t len);

// One turn of the app: settles the promises whose time has come and resumes the
// task if it can go on. True while the app is alive; false when it finished or
// died, which is when stop() is due.
bool poll();

// Runs one line at a prompt, in the state that is already open, and prints what it
// answers. A line that is not a statement is tried as an expression, so `1 + 1`
// answers 2; a mistake comes out as one line and the state stays usable. There is
// no coroutine under this, so a promise cannot be awaited here (docs/30).
bool eval(const char* source, size_t len);

// How the last app ended. Meaningful once poll() has said it is over: true when
// it died with an error, which has already been printed.
bool failed();

// The code the app gave to app.exit(). 0 when it just ended.
int exit_code();

// Milliseconds until the app has to be looked at again, or -1 when nothing it
// waits for has a deadline. 0 means "as soon as possible".
int32_t until_next_ms();

// Console bytes typed while an app is alive. The app owns the keyboard: the
// shell does not see them, echo and line editing are done here, and a line ends
// with Enter (docs/30).
void feed_console(const char* data, size_t len);

// The id of the app this state belongs to, or "" when it is not an app.
const char* app_id();

// The file the app was asked to open, or nullptr. It is what a sleep has to write
// down along with the app, or the app would come back without it (docs/90).
const char* app_path();

// Calls a global function of the app with no arguments, when the app declares it
// (on_pause, on_save, on_close: docs/40). False when it is not there or when the
// call failed. Like on_alarm, a hook runs in the main thread and cannot await.
bool call_hook(const char* name);

// Calls on_alarm(id, payload, {late_ms, missed}) in the running state. False
// when the app has no such function, or when the call itself failed. on_alarm
// runs in the main thread: it cannot await, which is what makes it safe to call
// between two turns of the app (docs/60).
bool call_alarm(const alarms::Entry& entry, int64_t late_ms);

// True when those bytes asked to leave the app: three ESC in a row, or a line
// that says !!exit while the app is not reading one. Whoever drives the loop is
// the one that closes the app.
bool exit_requested();

size_t used_bytes();
size_t peak_bytes();
size_t limit_bytes();

}  // namespace luabind

#endif  // LUNOK_LUABIND_LUABIND_H
