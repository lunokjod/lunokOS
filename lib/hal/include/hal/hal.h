// Hardware abstraction layer.
//
// Interfaces only: this library has no sources worth speaking of. Each board is
// its own library (hal_atom, hal_emulator) and implements what it really has.
// A board that lacks a device returns nullptr and leaves the capability off.
#ifndef LUNOK_HAL_HAL_H
#define LUNOK_HAL_HAL_H

#include <stddef.h>
#include <stdint.h>

namespace hal {

enum Capability : uint32_t {
  kCapConsole = 1u << 0,
  kCapAnsi = 1u << 1,
  kCapDisplay = 1u << 2,
  kCapInput = 1u << 3,
  kCapLed = 1u << 4,
  kCapLittlefs = 1u << 5,
  kCapSd = 1u << 6,
  kCapNvs = 1u << 7,
  kCapRtc = 1u << 8,
  kCapRtcWake = 1u << 9,
  kCapWifi = 1u << 10,
  kCapHttp = 1u << 11,
  kCapBattery = 1u << 12,
  kCapSleepLight = 1u << 13,
  kCapSleepDeep = 1u << 14,
  kCapProvisioning = 1u << 15,
  kCapImu = 1u << 16,
  // The air around the board: temperature and humidity, which is the SHT30 of the Paper and nothing
  // at all on the ATOMs (docs/50).
  kCapEnv = 1u << 17,
  // The glass: somebody can touch the board, and the kernel turns the fingers into taps, long
  // presses and drags (docs/30, docs/50).
  kCapTouch = 1u << 18
};

struct Caps {
  uint32_t bits = 0;

  bool has(Capability c) const { return (bits & static_cast<uint32_t>(c)) != 0; }
  void add(Capability c) { bits |= static_cast<uint32_t>(c); }
  // A board starts from `esp32_caps()` and takes away here the one its hardware really
  // lacks, with the reason written next to it (docs/50).
  void remove(Capability c) { bits &= ~static_cast<uint32_t>(c); }
};

// Capability table, indexed by position. Used by the shell and by sys.caps().
uint32_t cap_count();
const char* cap_name(uint32_t index);
uint32_t cap_bit(uint32_t index);

// Why the board came back. `kPowerOn` is a plain boot, `kTimer` is the chip's own sleep
// timer, `kConsole` a byte on the serial port, `kOther` anything else, and `kClock` is the
// **clock chip's alarm**: a board that was powered off and that a quartz brought back, which
// is not the same thing as a timer and is the strongest evidence of a good wake (docs/60).
enum class WakeCause : uint8_t { kPowerOn = 0, kTimer = 1, kConsole = 2, kOther = 3, kClock = 4 };

enum class OpenMode : uint8_t { kRead = 0, kWrite = 1, kAppend = 2, kReadWrite = 3 };

enum class NodeType : uint8_t { kNone = 0, kFile = 1, kDir = 2 };

struct Stat {
  NodeType type = NodeType::kNone;
  uint32_t size = 0;
  int64_t mtime = 0;  // epoch seconds, 0 when the backend does not know
};

struct ListEntry {
  const char* name = nullptr;  // owned by the backend during the call only
  NodeType type = NodeType::kNone;
  uint32_t size = 0;
  int64_t mtime = 0;
};

class ListSink {
 public:
  virtual ~ListSink() {}
  // Return false to stop the listing early.
  virtual bool on_entry(const ListEntry& entry) = 0;
};

// Space the VFS pool lends to a backend for one open file. Fixed size on
// purpose: opening a file must never allocate.
struct FileSlot {
  uint32_t size = 0;
  void* impl = nullptr;
};

class IClock {
 public:
  virtual ~IClock() {}
  virtual int64_t epoch_ms() = 0;
  virtual bool set_epoch_ms(int64_t epoch_ms) = 0;
  virtual int64_t monotonic_ms() = 0;
  // Only one wake is ever armed: the nearest deadline. See docs/60.
  virtual bool arm_wake(int64_t at_epoch_ms) = 0;
  virtual void disarm_wake() = 0;
  virtual WakeCause last_wake() = 0;
  virtual bool can_wake() const = 0;
  // Takes the hour back from wherever this board keeps it properly. A board whose hour is a
  // chip with a battery behind it answers by reading it; one whose hour is the chip's own
  // counter has nothing to take and does nothing. The system asks after every sleep that came
  // back, because a sleep is exactly when its own counter drifts (docs/60).
  virtual void resync() {}
  // Where the epoch comes from: "rtc" when a battery-backed chip keeps it,
  // "soc" when it is the SoC's own counter, "none" when nobody has set it yet
  // and the epoch is 0. docs/60 is the rule; time.caps() repeats it to apps.
  virtual const char* source() const = 0;
};

class IStorage {
 public:
  virtual ~IStorage() {}
  virtual bool stat(const char* rel, Stat& out) = 0;
  virtual bool list(const char* rel, ListSink& sink) = 0;
  virtual bool open(const char* rel, OpenMode mode, FileSlot& slot) = 0;
  virtual int32_t read(FileSlot& slot, void* buf, uint32_t len) = 0;
  virtual int32_t write(FileSlot& slot, const void* buf, uint32_t len) = 0;
  virtual bool seek(FileSlot& slot, uint32_t pos) = 0;
  virtual bool close(FileSlot& slot) = 0;
  virtual bool remove(const char* rel) = 0;
  virtual bool mkdir(const char* rel) = 0;
  virtual bool rename(const char* from, const char* to) = 0;
  virtual bool space(uint64_t& total, uint64_t& free) = 0;
};

class IKeyValue {
 public:
  virtual ~IKeyValue() {}
  // `cap` is how much room the caller has and `len` comes back with what was
  // read: two numbers and not one, because the one-number version asked the
  // caller to put the capacity in `len` before the call, and a caller that wrote
  // `size_t len = 0;` read nothing and was never told (measured on the board,
  // 31/10: the network was saved and `network:` kept saying none).
  virtual bool get(const char* ns, const char* key, void* buf, size_t cap, size_t& len) = 0;
  virtual bool set(const char* ns, const char* key, const void* buf, size_t len) = 0;
  virtual bool erase(const char* ns, const char* key) = 0;
  // Iteration: pass first = true for the first key of the namespace.
  virtual bool next(const char* ns, const char*& key, bool& first) = 0;
  // The namespaces themselves, for the /nvs mount to list them as directories
  // (docs/70). Same convention: first = true starts again.
  virtual bool next_namespace(const char*& ns, bool& first) = 0;
  // How full the store is, in **entries**: the NVS is not bytes, it holds a fixed number
  // of entries and a store with none left refuses the next save. `used` of `total` is the
  // number that says whether the next alarm fits; `total` is 0 when the backend has no
  // ceiling to report, which is the emulator's case (docs/70).
  virtual bool stats(uint32_t& used, uint32_t& total) = 0;
};

// How the network is, as the kernel keeps asking. A board never blocks on the
// radio: it starts and then says in which of these it is (docs/30, docs/50).
enum class NetState : uint8_t { kOff = 0, kConnecting = 1, kUp = 2, kFailed = 3 };

// Provisioning: the board opens its own access point, somebody hands it the
// network (the Espressif app, over protocomm) and then it is done. It is the
// board that keeps the credentials, in its own store, so that `net.provision`
// is only about the act of handing them over (docs/50).
enum class ProvState : uint8_t { kOff = 0, kRunning = 1, kDone = 2, kFailed = 3 };

// How a person hands the network to the board. Bluetooth is the way the maker of
// the chip recommends and it does not take the phone off its own network; the
// access point is kept because it needs no Bluetooth at all, and a board that
// cannot open one is a board that cannot be provisioned (docs/50).
enum class ProvTransport : uint8_t { kSoftAp = 0, kBle = 1 };

// What a download is allowed to cost. There is no header list: nothing has asked
// for one and a list of headers costs memory that docs/75 has not given away.
struct HttpOpts {
  uint32_t timeout_ms = 10000;
  // How many bytes of the page the caller wants in memory. **Zero is a page that is only wanted on a
  // volume**, and then `sink` has to be there (docs/30).
  uint32_t max_bytes = 16384;
  // **A page too big for memory goes to a file instead of being refused**: with a sink and a path
  // inside that unit, the body that grows past `max_bytes` is written there —the first `max_bytes`
  // included— and the whole page can be worked with once it is down. A page that fits never touches
  // the volume. `nullptr` is a page that only lives in memory (docs/30, docs/75).
  IStorage* sink = nullptr;
  const char* sink_path = nullptr;
};

// Why a page did not arrive, said as a cause and not as a word: the code an app
// reads (E_HTTP, E_TLS, E_TOOBIG) is the kernel's to choose (docs/30).
enum class HttpError : uint8_t {
  kNone = 0,
  kConnect,   // the server was not reachable, or it stopped talking
  kTls,       // the certificate was not accepted, or the handshake failed
  kTooBig,    // more bytes than the caller allowed
  kNoMemory,  // no room for the page in what is left of the heap
  kStorage,   // the volume refused the page: no room, or a write that did not go in
  kStopped    // nobody wants the answer any more (docs/75)
};

struct HttpResult {
  int status = 0;        // the HTTP status, 0 when there was no answer
  char* body = nullptr;  // owned by the board until http_release
  // In memory, or **in the file** when `saved`: the size the page has, which is what the caller wants
  // to hear whether it went to the heap or to a volume (docs/30).
  uint32_t body_len = 0;
  // True when the page is in the sink and not here: `body` is null and `body_len` is the size of the
  // file that was written, the whole page from its first byte (docs/30).
  bool saved = false;
  HttpError error = HttpError::kNone;
};

class INet {
 public:
  virtual ~INet() {}
  // Starts connecting and returns at once. There is no timeout here on purpose:
  // the deadline belongs to the kernel, which is the one that must keep turning
  // while the radio works (docs/30).
  virtual bool wifi_start(const char* ssid, const char* pass) = 0;
  // Connects with the network the board already has stored. This is what makes
  // provisioning worth anything after a reboot (docs/50).
  virtual bool wifi_start_stored() = 0;
  // Puts a network in the board's own store, so that it is there after a reboot
  // and so that `net.connect_stored` can use it. It does not connect.
  virtual bool wifi_store(const char* ssid, const char* pass) = 0;
  // Which network the board has stored, so an app can say it out loud.
  virtual bool wifi_stored_ssid(char* out, size_t cap) = 0;
  virtual NetState wifi_state() = 0;
  virtual bool wifi_disconnect() = 0;

  // Scanning the networks around, without waiting for the radio: start it, ask
  // whether it is done in every turn, and then how many it found (docs/50).
  virtual bool wifi_scan_start() = 0;
  // **Why the last thing asked of the radio was refused**, in the driver's own words when it has them
  // (0.118.10). `E_BUSY` is the right answer for a radio that is working on something else, and the
  // wrong one for a radio that never came up —no memory for it, a driver that would not start, a scan
  // the chip turned down—: they are different faults with different fixes and the board answered the
  // same four letters to all of them (the ATOM, 8/11) (docs/30, docs/50).
  virtual const char* last_error() const { return ""; }
  virtual bool wifi_scan_done() = 0;
  virtual uint32_t wifi_scan_count() = 0;
  // One entry of the last finished scan: its name and its signal. True when the
  // index is there.
  virtual bool wifi_scan_record(uint32_t index, char* ssid, size_t cap, int32_t& rssi) = 0;

  // Provisioning, again without waiting: start the access point and the
  // protocomm service, and ask how it is going in every turn. `pop` may be
  // nullptr when the board has no way to show one (docs/50).
  // `service_name` comes in empty and the board fills it: the name a phone has to
  // look for is the one the radio announces, and only the board knows its MAC. The
  // caller prints the QR with what comes back (docs/50).
  virtual bool prov_start(const char* pop, char* service_name, size_t cap,
                          ProvTransport transport) = 0;
  // Time from a server on the network: start and return, and the kernel asks
  // `ntp_synced` in every turn. `epoch_ms` comes back with the time the server
  // gave, and the kernel puts it in the clock, so both bands set the clock the
  // same way (docs/60).
  virtual bool ntp_start(const char* server) = 0;
  virtual bool ntp_synced(int64_t& epoch_ms) = 0;

  virtual ProvState prov_state() = 0;
  virtual bool prov_stop() = 0;
  // True when the board already knows a network: provisioned once, or given one
  // by hand. There is nothing to provision in that case.
  virtual bool prov_provisioned() = 0;
  // Forgets the stored network, so the next boot is a board with no network at
  // all (docs/50).
  virtual bool prov_forget() = 0;
  // A page, downloaded in a task of the board's own so that nothing here blocks:
  // the kernel starts it and asks `http_poll` in every turn (docs/30). The answer
  // is 0 when there is nothing to start, and then nothing will be answered.
  virtual uint32_t http_get_start(const char* url, const HttpOpts& opts) = 0;
  // False when the operation is not one of this board's. `done` says whether the
  // answer is ready; `out` is only read when it is.
  virtual bool http_poll(uint32_t op, bool& done, HttpResult& out) = 0;
  // Frees what the request left behind, and it must be called once whatever the
  // outcome was: a caller that forgets it leaves a page in the heap. On a request
  // that is still running it means "nobody wants the answer", and the task drops
  // it (docs/75).
  virtual void http_release(uint32_t op) = 0;
};

// How deep the sleep is (docs/90, M6). `kLight` keeps the memory, the interpreter
// and the console: the system goes on where it was. `kDeep` keeps only what the
// RTC holds: the board restarts and boots again, and what was running comes back
// from the session record (docs/30).
enum class SleepMode : uint8_t { kLight = 0, kDeep = 1 };

class IPower {
 public:
  virtual ~IPower() {}
  // The battery, when the board can tell. False is "this board has no gauge", not
  // "empty": the ATOM is powered by its cable and says it cannot tell (docs/50).
  virtual bool battery(int& mv, int& pct, bool& charging) = 0;
  // Sleeps until `at_epoch_ms` (0 = no deadline). **A deep sleep does not return**:
  // the board restarts (docs/90). True when it came back from a light sleep.
  virtual bool sleep(SleepMode mode, int64_t at_epoch_ms) = 0;
  // Whether this board's deep sleep is a **full power off**: the board does not sleep,
  // it stops being powered and the clock chip's alarm turns it back on (docs/50). It is
  // the least current a board can draw, and it means the wake does not come from the
  // chip's own timer but from the clock, which is why the system says so at boot.
  virtual bool deep_powers_off() const { return false; }
  // Cuts the board's own supply, and **nothing comes back from it**: it is not a sleep, no alarm is
  // armed, and only the button —or the cable, on a board that cannot switch itself off while it is
  // plugged in— brings the board up again (docs/50). False is a board that cannot do it, said out
  // loud instead of looking like it worked: the ATOMs have no hold on their supply and the Paper
  // does not drop while the cable is in.
  virtual bool power_off() { return false; }
  // Where this board can be woken from: "timer", "button", "console" or "none".
  // Said by the board, because it is the wiring that decides it (docs/50).
  virtual const char* wakes() = 0;
  // How fast the processor runs, in MHz, and the change of that speed. The steps
  // are the board's: a value it cannot do is refused, and never rounded to the
  // nearest one, because a rounded speed is a lie about what it costs (docs/50).
  virtual uint32_t cpu_mhz() = 0;
  virtual bool set_cpu_mhz(uint32_t mhz) = 0;
};

// Reserved: implemented when a board with a screen exists (docs/50).
class IDisplay {
 public:
  virtual ~IDisplay() {}
};

// How the glass paints (docs/50). The panel of the Paper has these and they are the whole vocabulary
// of an e-ink refresh: `kInit` is the two-second beginning, `kDu` and `kDu4` are the quick ones for
// text and menus, and `kGc16` is the one that cleans the ghost of the quick ones.
enum class UpdateMode : uint8_t {
  kInit = 0,
  kDu = 1,
  kGc16 = 2,
  kGl16 = 3,
  kGlr16 = 4,
  kGld16 = 5,
  kDu4 = 6,
  kA2 = 7,
};

// **The name of a quality**, which is the word the glass speaks: every line of the log says `gl16` and not a
// five, because a number a reader has to look up in a header is a number that says nothing —and the question
// «which quality painted this rectangle?» is the one the log is read for— (docs/50, docs/85).
inline const char* mode_name(UpdateMode mode) {
  switch (mode) {
    case UpdateMode::kInit:
      return "init";
    case UpdateMode::kDu:
      return "du";
    case UpdateMode::kGc16:
      return "gc16";
    case UpdateMode::kGl16:
      return "gl16";
    case UpdateMode::kGlr16:
      return "glr16";
    case UpdateMode::kGld16:
      return "gld16";
    case UpdateMode::kDu4:
      return "du4";
    case UpdateMode::kA2:
      return "a2";
  }
  return "?";
}

// A surface an app can draw on, and the paint that puts it on the glass. It is a **surface** and not a
// streaming driver: what is drawn lives in the board's own memory, a rectangle remembers what changed,
// and `flush` sends only that rectangle. An e-ink glass takes 120 to 450 ms to move its ink, so a
// paint is asked for and **not waited on**: `idle()` is how a later turn knows it is done (docs/50).
class ISurface : public IDisplay {
 public:
  virtual uint16_t width() const = 0;
  virtual uint16_t height() const = 0;
  // 0 is black and 15 is white: the four bits of the panel, which is what every level here means
  // (docs/50).
  virtual void clear(uint8_t level) = 0;
  virtual void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t level) = 0;
  virtual void pixel(uint16_t x, uint16_t y, uint8_t level) = 0;
  // A whole rectangle of 4-bit pixels **that is already drawn**, one byte per two of them and the
  // left one in the high nibble: it is how the view of an app reaches the glass in one pass instead
  // of half a million `pixel` calls (docs/30, docs/50). `stride` is the bytes of a line of the source,
  // which is not always `w / 2` —a rectangle out of a wider canvas is the normal case— and pixels
  // outside the panel are dropped. **`packed` points at the first pixel of the region**, so a source
  // is indexed by its own column and not by the column it lands on: an odd `x` splits a byte with the
  // neighbour, and reading the source with the panel's column is a rectangle of somebody else's
  // pixels (docs/50).
  virtual void blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t* packed,
                    uint16_t stride) = 0;
  // Sends what changed and asks for it to be painted, with the mode that says how good it has to
  // look. False when the panel did not take it.
  //
  // **Every write above marks the rectangle it touched**, and this call sends the one rectangle that
  // covers them all and forgets it. With nothing marked there is nothing to paint and nothing is sent:
  // a page nobody asked to be repainted is a page this system does not touch (docs/30, docs/50).
  virtual bool flush(UpdateMode mode) = 0;
  virtual bool idle() = 0;
  // **How the surface is turned.** 0, 90, 180 or 270 degrees: the width and height the surface reports are
  // the ones an app draws in, and a surface that turns does the work itself when it sends what changed.
  // A surface that cannot turn says no, and the panel of this board turns in software (docs/50).
  virtual bool set_rotation(uint16_t degrees) {
    (void)degrees;
    return false;
  }
  virtual uint16_t rotation() const { return 0; }
  // Why the last thing that failed failed, in words, for the line the caller writes. Empty when it did
  // not fail. A paint that is refused without a reason is a flash to find out which of five things it
  // was (docs/50).
  virtual const char* last_error() const { return ""; }
};

// One finger on the glass, as the controller reports it (docs/50): where it is, how much of the
// glass it covers, and which of the fingers of this gesture it is. The number matters on a panel
// that reports five at once: it is what tells `the same finger moved` from `one finger went and
// another came` (docs/50).
struct TouchPoint {
  uint16_t x = 0;
  uint16_t y = 0;
  // The area the chip measured under the finger. It is a size and not a pressure: the controller
  // counts how much of the glass is covered, and two people pressing equally hard give two sizes
  // (docs/50).
  uint16_t size = 0;
  uint8_t id = 0;
};

// The glass of the board. The chip measures by itself and keeps its last frame until somebody takes
// it, so this is a **read and not a wait**: the kernel asks in its own turns and the answer is what
// the glass has now (docs/50, docs/30).
class ITouch {
 public:
  virtual ~ITouch() {}
  // How many fingers the glass reports at once.
  virtual uint8_t max_points() const = 0;
  // The frame the coordinates come in: what the controller was configured with, which is the panel
  // in its own portrait and not the way the board is held (docs/50).
  virtual uint16_t width() const = 0;
  virtual uint16_t height() const = 0;
  // The points that are down now, up to `capacity`; 0 when nobody is touching. A frame that is
  // taken is cleared, so a stale finger is never read twice (docs/50).
  virtual uint8_t read(TouchPoint* out, uint8_t capacity) = 0;
  // **Whether the frame that was just handed over is of a finger that is still there** (0.83.0). A controller that
  // scans the glass by itself hands over the frame of its last scan, and a finger that came down and went away
  // between two reads is a tap that is **already over** when the caller hears about it: `true` here would say the
  // release comes later, and a keyboard of one press per finger would swallow the next tap. A panel that cannot
  // tell says `true`, which is the safe answer —a frame is a finger until the panel says otherwise— (docs/50).
  virtual bool finger_still_down() const { return true; }
};

// Where a key sits, said the same way on every board: the board is a box and a
// key is on one of its six faces, at one of the five places of that face. An app
// that wants "up" to mean one thing and "left" another has to be able to ask
// (docs/30, docs/50).
enum class Face : uint8_t { kFront = 0, kBack = 1, kLeft = 2, kRight = 3, kTop = 4, kBottom = 5 };

// The five places of a face: the middle and the four sides of it.
enum class Spot : uint8_t { kCentre = 0, kUp = 1, kDown = 2, kLeft = 3, kRight = 4 };

// How a key is built. The one button of the ATOM and the three of the Paper's
// dial are the same kind of contact, and they are not the same thing to touch: a
// simple key is one key, and a compound one is several keys of a single component,
// which is why they share a group (docs/50).
enum class KeyKind : uint8_t { kSimple = 0, kCompound = 1 };

const char* face_name(Face face);
const char* spot_name(Spot spot);
const char* kind_name(KeyKind kind);

// What a person does to this board, and what the board says back. Both are
// optional: a board without them returns nullptr and leaves the capability off
// (docs/50).
class IInput {
 public:
  virtual ~IInput() {}
  // How many keys the board has: one on the ATOM, three on the Paper. Key 0 is
  // the main one, and it is the one the system counts taps on, because three
  // taps of a side key is a menu and three of the middle one is "get out"
  // (docs/30).
  virtual uint32_t count() = 0;
  // True while that key is held. The board debounces it, so the same press
  // answers the same in every turn the kernel asks in (docs/50).
  virtual bool pressed(uint32_t key) = 0;
  // How long it has been held, in ms, or 0 when it is not held or the key does
  // not exist (docs/50).
  virtual int64_t held_ms(uint32_t key) = 0;

  // Where the key is and how it is built, so an app can give a function to a
  // position instead of to a number (docs/30).
  virtual Face face(uint32_t key) = 0;
  virtual Spot spot(uint32_t key) = 0;
  virtual KeyKind kind(uint32_t key) = 0;
  // Keys of the same physical component share a group: the three of the Paper's
  // dial are group 0, and a board with two separate buttons has two groups.
  virtual uint32_t group(uint32_t key) = 0;
};

// One light or a strip of them. The system paints **the whole light** with one
// colour, so the driver of a board with one LED and the driver of a board with
// twenty-five are the same interface and the policy grows nothing (docs/50).
class ILed {
 public:
  virtual ~ILed() {}
  // How many lights the board has: one on the ATOM Lite, twenty-five on the Matrix.
  virtual uint32_t count() = 0;
  // The highest a channel may be driven at. It is a hardware limit and not a
  // taste: the panel of the Matrix heats up above the maker's number (docs/50),
  // and the system does not let an app go past it. 255 on a light that has no
  // such limit.
  virtual uint32_t max_value() = 0;
  // Paints every light with one colour. False when the board cannot.
  virtual bool fill(uint8_t r, uint8_t g, uint8_t b) = 0;
  // One light, for a board that has more than one. False when there is no such
  // index.
  virtual bool pixel(uint32_t index, uint8_t r, uint8_t g, uint8_t b) = 0;
  // Sends what has been painted: one frame is one write on the wire (docs/75).
  virtual void show() = 0;
};

// The inertial sensor: which way is down and how fast the board is turning. It is
// not on every board —the ATOM Lite has none, the Matrix has a MPU6886— and the one
// that lacks it returns nullptr and leaves the capability off (docs/50).
class IImu {
 public:
  virtual ~IImu() {}
  // Acceleration in g on the three axes, turn rate in degrees per second, and the
  // temperature the chip measures in degrees Celsius (it is in the same burst of
  // registers, so it costs nothing). False when the sensor did not answer.
  virtual bool read(float& ax, float& ay, float& az, float& gx, float& gy, float& gz,
                    float& celsius) = 0;
};

// The **extra** memory of the board —the PSRAM of the M5Paper— with the three numbers that are not
// the same and that people confuse (docs/75):
//
//   - `chip_bytes` is what the chip says it has, which is what a datasheet prints;
//   - `total_bytes` is what the CPU can **address**, which is what the system can hand out;
//   - `free_bytes` is what is left of that.
//
// On this family the first and the second do not match: the ESP32's window for external RAM is
// **4 MB**, so a board that carries 8 MB keeps four of them behind the bank switcher of the chip,
// and memory that moves under a pointer cannot be given to an app (docs/50, docs/75). All zero on a
// board with no extra memory, which is the truth about the hardware and not an error.
struct ExtraMemory {
  uint32_t chip_bytes;
  uint32_t total_bytes;
  uint32_t free_bytes;
};

// The air around the board: how warm it is and how wet. It is not on every board —the Paper has an
// SHT30 and the ATOMs have nothing— and a board without one returns nullptr from `sensor()` and
// leaves the capability off (docs/50).
//
// **It is a reading and not a policy**: the chip measures when it is asked and holds no state, so
// there is nothing to switch on, nothing to keep warm and nothing to turn off afterwards. The one
// thing the driver has to say is how long asking costs, and it says it in docs/50.
class ISensor {
 public:
  virtual ~ISensor() {}
  // The temperature in degrees Celsius and the relative humidity in percent. False when the chip
  // did not answer, or answered with a crossed CRC, which is the same thing to an app: no number is
  // better than a wrong one (docs/30).
  virtual bool read(float& celsius, float& humidity) = 0;
};

class IPlatform {
 public:
  virtual ~IPlatform() {}
  // The board this build runs on, by the name the board has in platformio.ini
  // (`board = ...`) — or the name of the board of its family that differs in what the
  // app can touch (`m5stack-atom-matrix`). The PC build answers the model it was told
  // to stand in for: it is never "emulator", which is not a board anybody can flash
  // (docs/50, docs/85).
  virtual const char* id() const = 0;
  // The chip family this board belongs to ("esp32"). It is what an app means when
  // its manifest says it runs on "esp32": every board of the family accepts it
  // (docs/40). The bench is not a chip of its own and answers the family of the model
  // it is, because that is what it is imitating.
  virtual const char* family() const = 0;
  // True only in the PC build: no real clock, no real flash, no real port. It is what
  // tells an app that the board under `id()` is a fake.
  virtual bool emulator() const = 0;
  // The board names this band answers to, which is what an app's `boards` list is
  // matched against. A board answers with its own `id()` and nothing else, and the bench
  // does the same with the model it was told to be: one name, so an app is accepted or
  // refused on the PC exactly as it would be on the glass (docs/40, docs/85).
  virtual uint32_t board_count() const { return 1; }
  virtual const char* board_name(uint32_t index) const {
    return (index == 0) ? id() : nullptr;
  }
  virtual const Caps& caps() const = 0;
  virtual uint32_t heap_limit_kb() const = 0;
  // The extra memory of the board, as the board sees it (docs/75). The interpreter puts its big
  // blocks there and an app is told what is left; it is the board that answers, so the bench answers
  // what the model carries (docs/85).
  virtual ExtraMemory extra_memory() const { return ExtraMemory{0, 0, 0}; }
  virtual IClock* clock() = 0;
  virtual IStorage* storage(const char* mount) = 0;
  virtual IKeyValue* nvs() = 0;
  virtual INet* net() = 0;
  virtual IPower* power() = 0;
  virtual ISurface* display() = 0;
  virtual ILed* led() = 0;
  virtual IInput* input() = 0;
  virtual IImu* imu() = 0;
  // The ambient sensor, when the board has one. The default is nullptr because that is what a
  // board without it has to say, and a board that has it overrides this (docs/50). `const` because
  // the capabilities ask it too, and a board is not changed by being asked what it has.
  virtual ISensor* sensor() const { return nullptr; }
  // The glass of the board, or nullptr when it has none (docs/50). It is not pure for the same
  // reason as `sensor()`, and it is **not announced** as a capability yet: the chip answers and its
  // frame is known, but a name an app can ask for has to come with the call that uses it, and a
  // touch means nothing without something drawn under it — the panel (docs/90).
  virtual ITouch* touch() const { return nullptr; }
  // **What one instruction of the interpreter costs when this band has to simulate a processor it is
  // not running on**, in nanoseconds: zero on a board, where the time is the real one, and a number on
  // a bench that is paying for it, scaled by `power()->cpu_mhz()` (docs/85).
  virtual uint32_t simulated_instruction_ns() { return 0; }
};

// Implemented once per board library (hal_atom, hal_emulator).
IPlatform& platform();

}  // namespace hal

// What a battery voltage means: the curve that turns millivolts into a percentage. It is
// here and not in a board because it is about a cell and not about a chip (docs/50).
#include "hal/battery.h"

// Dates and epochs: the maths a clock chip needs, and the same reason to live here (docs/50).
#include "hal/civil.h"
#include "hal/alarm.h"

#endif  // LUNOK_HAL_HAL_H
