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
  kCapImu = 1u << 16
};

struct Caps {
  uint32_t bits = 0;

  bool has(Capability c) const { return (bits & static_cast<uint32_t>(c)) != 0; }
  void add(Capability c) { bits |= static_cast<uint32_t>(c); }
};

// Capability table, indexed by position. Used by the shell and by sys.caps().
uint32_t cap_count();
const char* cap_name(uint32_t index);
uint32_t cap_bit(uint32_t index);

enum class WakeCause : uint8_t { kPowerOn = 0, kTimer = 1, kConsole = 2, kOther = 3 };

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
  uint32_t max_bytes = 16384;
};

// Why a page did not arrive, said as a cause and not as a word: the code an app
// reads (E_HTTP, E_TLS, E_TOOBIG) is the kernel's to choose (docs/30).
enum class HttpError : uint8_t {
  kNone = 0,
  kConnect,   // the server was not reachable, or it stopped talking
  kTls,       // the certificate was not accepted, or the handshake failed
  kTooBig,    // more bytes than the caller allowed
  kNoMemory,  // no room for the page in what is left of the heap
  kStopped    // nobody wants the answer any more (docs/75)
};

struct HttpResult {
  int status = 0;        // the HTTP status, 0 when there was no answer
  char* body = nullptr;  // owned by the board until http_release
  uint32_t body_len = 0;
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

class IPlatform {
 public:
  virtual ~IPlatform() {}
  // The board this build runs on, as written in platformio.ini (`board = ...`).
  // The PC build is not a board and answers "emulator".
  virtual const char* id() const = 0;
  // The chip family this board belongs to ("esp32"). It is what an app means when
  // its manifest says it runs on "esp32": every board of the family accepts it
  // (docs/40). The PC is not a chip and answers "host".
  virtual const char* family() const = 0;
  // True only in the PC build: no real clock, no real flash, no real port.
  virtual bool emulator() const = 0;
  virtual const Caps& caps() const = 0;
  virtual uint32_t heap_limit_kb() const = 0;
  virtual IClock* clock() = 0;
  virtual IStorage* storage(const char* mount) = 0;
  virtual IKeyValue* nvs() = 0;
  virtual INet* net() = 0;
  virtual IPower* power() = 0;
  virtual IDisplay* display() = 0;
  virtual ILed* led() = 0;
  virtual IInput* input() = 0;
  virtual IImu* imu() = 0;
};

// Implemented once per board library (hal_atom, hal_emulator).
IPlatform& platform();

}  // namespace hal

#endif  // LUNOK_HAL_HAL_H
