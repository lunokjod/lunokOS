// The network policy of the system: one attempt, one deadline, and the answer
// given as a promise. docs/30 has the rule; this file is the rule in code.
#include "kernel/meter.h"
#include "kernel/net.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "kernel/fs_ops.h"
#include "kernel/led.h"
#include "kernel/vfs.h"
#include "luabind/luabind.h"
#include "pal/pal.h"
#include "qr/qr.h"

namespace kernel {
namespace net {
namespace {

hal::INet* g_net = nullptr;
hal::IClock* g_clock = nullptr;
uint32_t g_caps = 0;

// What the system is doing with the radio, one thing at a time. Provisioning and
// connecting are the same wait from the app's side: the deadline runs from the
// moment it asked (docs/30, docs/50).
// `kRefused` is a job the radio would not take: the answer is given in the next
// turn and not inside the call that asked, because at that moment nobody is
// listening yet (docs/30).
enum Phase : uint8_t {
  kIdle = 0,
  kConnecting = 1,
  kProvisioning = 2,
  kScanning = 3,
  kRefused = 4,
  kNtp = 5,
  kHttp = 6
};

Phase g_phase = kIdle;
int64_t g_deadline_ms = 0;

int64_t now_ms() { return (g_clock != nullptr) ? g_clock->monotonic_ms() : 0; 
  // The RTC chip, read once a turn (docs/30, docs/50).
  meter::Scope scope(meter::kClockMono);
  return (g_clock != nullptr) ? g_clock->monotonic_ms() : 0;
}

// ------------------------------------------------------------------ the queue
// The radio belongs to the system and an app (or the shell) only **asks**: the
// requests go into one FIFO, they are done one at a time, and the radio is on
// only while there is something in it. Without this, two things that both want
// the radio fight, and the radio stays on after the last one has its answer
// (docs/30, docs/90).
enum RequestKind : uint8_t {
  kReqNone = 0,
  kReqConnect,
  kReqConnectStored,
  kReqProvision,
  kReqScan,
  kReqAdd,
  kReqForget,
  kReqNtp,
  kReqHttp,
};

// The longest address a request can carry. The board refuses anything longer, so
// the two are the same number and a page is never asked for with half an address
// (docs/30).
const uint32_t kNetUrlCap = 192;

// The longest path inside a volume that a page can be written to. It is shorter than the VFS's own
// limit because a copy of it rides in every request of the queue, and the queue lives in the RAM the
// board has and not in the PC's (docs/75).
const uint32_t kSinkPathCap = 96;

struct Request {
  RequestKind kind = kReqNone;
  char ssid[33] = {0};
  char pass[65] = {0};
  char pop[33] = {0};
  char server[64] = {0};
  char url[kNetUrlCap] = {0};
  // How many bytes of the page the caller is willing to hold (docs/75).
  uint32_t max_bytes = 0;
  // **Where a page that does not fit goes**: the unit and the path inside it, both resolved when the
  // request was made, so a place that cannot be written is a refusal and not a page lost at the end
  // (docs/30, docs/70).
  hal::IStorage* sink = nullptr;
  char sink_rel[kSinkPathCap] = {0};
  hal::ProvTransport transport = hal::ProvTransport::kBle;
  uint32_t timeout_ms = 0;
  // Who is waiting for the answer: the shell's callback, or `nullptr` when it is
  // an app and the answer is a promise. They cannot both be waiting, because a
  // command does not run on top of an app (docs/30).
  Done done = nullptr;
  // True when the work needs the network: the request gets on it first (with what
  // the board has stored) and lets go of it afterwards like everything else. A
  // time server is a task of the queue, not a reason to keep the radio on
  // (docs/75).
  bool needs_network = false;
  // Which of the two steps of such a request is running.
  bool on_the_network = false;
};

const uint8_t kQueueLen = 8;
Request g_queue[kQueueLen];
uint8_t g_head = 0;
uint8_t g_count = 0;
Request g_current;               // the one that is running
bool g_radio_on = false;         // the radio was used and has not been switched off
Done g_pending_done = nullptr;   // the waiter of the next request the shell pushes

// The page that is being downloaded, and the one that has just arrived. The body
// belongs to the board until the answer is given, and the kernel never keeps it
// (docs/75).
uint32_t g_http_op = 0;
int g_http_status = 0;
const char* g_http_body = nullptr;
uint32_t g_http_body_len = 0;
// **The page that went to a file**: where it is, so whoever waited for it can open it. It is one path
// and not one per request because a page is one at a time —`page_pending()` is what keeps it so— and
// the queue lives in the board's RAM (docs/30, docs/75).
bool g_http_saved = false;
char g_http_saved_path[kSinkPathCap] = {0};

void start_next();
bool begin(Request& request);

// Why the last request could not start. A task that needs the network and has none
// to use says `E_NONET`; anything else is a busy radio (docs/30).
const char* g_refuse = "E_BUSY";

// How long the association gets after the phone has handed the credentials over.
// Twenty seconds is a normal association with room to spare, and it is short
// enough that a person is not left looking at a prompt that never comes back.
const int64_t kProvConnectMs = 20000;

void finish(bool up, const char* code, int32_t count = -1) {
  // A request that needs the network has two steps: get on it, then do the work.
  // This is the end of the first one, so the second begins here and the answer is
  // not given yet (docs/30, docs/75).
  if (g_current.needs_network && !g_current.on_the_network) {
    g_phase = kIdle;
    g_deadline_ms = 0;
    if (up) {
      g_current.on_the_network = true;
      g_deadline_ms = now_ms() + static_cast<int64_t>(g_current.timeout_ms);
      if (!begin(g_current)) g_phase = kRefused;
      return;
    }
    // The network did not come up, so the task cannot be done. It falls through to
    // the ordinary answer below, with the second step taken off: answering by
    // calling itself again was a stack overflow (measured, 31/10).
    g_current.needs_network = false;
  }

  const Done waiter = g_current.done;
  const RequestKind kind = g_current.kind;
  g_phase = kIdle;
  // Done with the radio: the light goes back to what it was saying before
  // (docs/30).
  led::set_state(led::State::kOff);
  g_deadline_ms = 0;
  g_current.kind = kReqNone;
  g_current.done = nullptr;
  g_current.needs_network = false;
  if (waiter != nullptr) {
    waiter(up, code, (count > 0) ? static_cast<uint32_t>(count) : 0u);
  } else if (kind == kReqHttp) {
    // A page is not a yes or a number: it is a status and a body —or the file it went to when it was
    // too big for the heap—, and the app gets them as a table (docs/30).
    luabind::http_progress(up, code, g_http_status, g_http_body, g_http_body_len,
                           g_http_saved ? g_http_saved_path : nullptr);
  } else {
    luabind::net_progress(up, code, count);
  }
  // A page that has been read goes back to the board here, after whoever was
  // waiting has read it and **before** the next request starts: the buffer was
  // only lent, and a second page must not find the first one holding the task
  // (docs/75).
  if (kind == kReqHttp && g_http_op != 0) {
    g_net->http_release(g_http_op);
    g_http_op = 0;
    g_http_status = 0;
    g_http_body = nullptr;
    g_http_body_len = 0;
    g_http_saved = false;
    g_http_saved_path[0] = '\0';
  }
  // The next one starts in this same turn: a chain of requests costs no time.
  start_next();
}

// What the interpreter may promise an app. A board that can fetch a page carries
// esp-tls in this build, so the two bits go together: there is no second switch
// for TLS and docs/50 says what it costs.
uint32_t features() {
  uint32_t bits = 0;
  if (g_net != nullptr) bits |= luabind::kNetWifi;
  if ((g_caps & static_cast<uint32_t>(hal::kCapProvisioning)) != 0) bits |= luabind::kNetProv;
  if ((g_caps & static_cast<uint32_t>(hal::kCapHttp)) != 0) {
    bits |= luabind::kNetHttp | luabind::kNetTls;
  }
  return bits;
}

uint8_t state_code() {
  if (g_net == nullptr) return 0;
  return static_cast<uint8_t>(g_net->wifi_state());
}

// The payload the Espressif app reads from the QR: the schema of wifi_prov_mgr,
// nothing invented here (docs/50).
void build_payload(const char* pop, const char* service, hal::ProvTransport transport, char* out,
                   size_t cap) {
  // The schema of wifi_prov_mgr, nothing invented here: the app reads `transport`
  // and looks for the board over the access point or over Bluetooth (docs/50).
  snprintf(out, cap, "{\"ver\":\"v1\",\"name\":\"%s\",\"pop\":\"%s\",\"transport\":\"%s\"}",
           service, pop, (transport == hal::ProvTransport::kBle) ? "ble" : "softap");
}

// Two modules in one character of the terminal, with half blocks: a phone needs
// squares, and a terminal character is twice as tall as it is wide (docs/50).
// Room for version 7 (45 modules a side, 122 bytes at level M): the provisioning
// payload is about 70, so this is twice what is needed and far from the 9 KB that
// a symbol of version 20 would take from a board with 320 KB (docs/75).
const uint32_t kQrSide = 45;
uint8_t g_qr[kQrSide * kQrSide];

void print_qr(const char* payload) {
  pal::IConsole* console = pal::system().console();
  if (console == nullptr) return;
  qr::Info info;
  if (!qr::encode(payload, qr::Level::kM, qr::Mode::kAuto, g_qr, sizeof(g_qr), info)) {
    // No QR is better than a wrong one: the payload goes out as text and the
    // person can type it into the app.
    const char kCannot[] = "the payload does not fit a QR; type it into the app:\r\n";
    console->write(kCannot, sizeof(kCannot) - 1);
    console->write(payload, strlen(payload));
    console->write("\r\n", 2);
    return;
  }
  const uint32_t side = info.side;

  const char* kTop = "\xE2\x96\x80";     // upper half block
  const char* kBottom = "\xE2\x96\x84";  // lower half block
  const char* kFull = "\xE2\x96\x88";    // full block
  const uint32_t quiet = 2;
  char line[(qr::kMaxSide + 2 * 4) * 3 + 2];

  // The quiet zone is part of the symbol for a scanner, so it is printed.
  for (uint32_t r = 0; r < quiet; ++r) {
    size_t used = 0;
    for (uint32_t c = 0; c < side + quiet * 2; ++c) line[used++] = ' ';
    line[used++] = '\r';
    line[used++] = '\n';
    console->write(line, used);
  }

  for (uint32_t row = 0; row < side; row += 2) {
    size_t used = 0;
    for (uint32_t c = 0; c < quiet; ++c) line[used++] = ' ';
    for (uint32_t col = 0; col < side; ++col) {
      const bool top = g_qr[row * side + col] != 0;
      const bool bottom = (row + 1 < side) && g_qr[(row + 1) * side + col] != 0;
      const char* glyph = " ";
      size_t len = 1;
      if (top && bottom) {
        glyph = kFull;
        len = 3;
      } else if (top) {
        glyph = kTop;
        len = 3;
      } else if (bottom) {
        glyph = kBottom;
        len = 3;
      }
      memcpy(line + used, glyph, len);
      used += len;
    }
    for (uint32_t c = 0; c < quiet; ++c) line[used++] = ' ';
    line[used++] = '\r';
    line[used++] = '\n';
    console->write(line, used);
  }

  for (uint32_t r = 0; r < quiet; ++r) {
    size_t used = 0;
    for (uint32_t c = 0; c < side + quiet * 2; ++c) line[used++] = ' ';
    line[used++] = '\r';
    line[used++] = '\n';
    console->write(line, used);
  }
}


// Provisioning on a board that already knows a network is not an error: it goes
// straight to connecting with what is stored, which is what the person asked for
// (docs/50).
bool begin_provision(const Request& request) {
  const char* pop = (request.pop[0] != '\0') ? request.pop : nullptr;
  const hal::ProvTransport transport = request.transport;
  char service[32];
  service[0] = '\0';
  g_deadline_ms = now_ms() + static_cast<int64_t>(request.timeout_ms);
  if (g_net->prov_provisioned()) {
    if (!g_net->wifi_start_stored()) {
      g_deadline_ms = 0;
      return false;
    }
    g_phase = kConnecting;
    return true;
  }
  const char* proof = (pop != nullptr && pop[0] != '\0') ? pop : "lunokos";
  if (!g_net->prov_start(proof, service, sizeof(service), transport)) {
    g_deadline_ms = 0;
    return false;
  }
  g_phase = kProvisioning;

  // The QR goes out on the console before the access point is up, so the person
  // can point the phone at it and not type anything (docs/50).
  char payload[128];
  build_payload(proof, service, transport, payload, sizeof(payload));
  pal::IConsole* console = pal::system().console();
  if (console != nullptr) {
    // The app has to be the one of the transport: the QR of a Bluetooth board read
    // with the SoftAP app leads nowhere (docs/50).
    const char kHeaderBle[] = "scan this with the ESP BLE Provisioning app:\r\n";
    const char kHeaderAp[] = "scan this with the ESP SoftAP Provisioning app:\r\n";
    if (transport == hal::ProvTransport::kBle) {
      console->write(kHeaderBle, sizeof(kHeaderBle) - 1);
    } else {
      console->write(kHeaderAp, sizeof(kHeaderAp) - 1);
    }
  }
  print_qr(payload);
  return true;
}

// Drops everything and takes the radio off the network. It is what an app's death
// means and what `wifi stop` means: nobody is waiting for an answer, the queue is
// not anybody's, and a radio that stays on with nothing to do is the energy that
// docs/75 does not want to spend. The credentials stay where they are, and the
// next person that wants the network asks for it again (docs/30, docs/75).
void stop_all() {
  if (g_net == nullptr) return;
  const Phase was = g_phase;
  g_phase = kIdle;
  g_deadline_ms = 0;
  g_count = 0;
  g_head = 0;
  g_pending_done = nullptr;
  g_radio_on = false;
  g_current.kind = kReqNone;
  g_current.done = nullptr;
  if (was == kProvisioning) g_net->prov_stop();
  if (was == kHttp && g_http_op != 0) {
    // A page that nobody is going to read: the board drops it (docs/75).
    g_net->http_release(g_http_op);
    g_http_op = 0;
    g_http_status = 0;
    g_http_body = nullptr;
    g_http_body_len = 0;
    g_http_saved = false;
    g_http_saved_path[0] = '\0';
  }
  g_net->wifi_disconnect();
}

void cancel() { stop_all(); }

bool drop() {
  if (g_net == nullptr) return false;
  g_phase = kIdle;
  g_deadline_ms = 0;
  if (g_net->prov_state() == hal::ProvState::kRunning) g_net->prov_stop();
  return g_net->wifi_disconnect();
}

bool is_provisioned() { return (g_net != nullptr) && g_net->prov_provisioned(); }

// Forgetting is a request like the rest, but it has nothing to wait for: it is
// done the moment it starts (docs/30).
void begin_forget() {
  const bool gone = g_net->prov_forget();
  finish(gone, gone ? nullptr : "E_BUSY");
}

// Pushes one request. False only when the queue is full or there is no radio:
// "later" is not "no", and that is the whole point of the queue (docs/30).
bool push(const Request& request) {
  // One request at a time, and one waiter: an app has a single promise slot, so a
  // second ask is refused with E_BUSY instead of being queued into a promise that
  // would never be settled (docs/30). The FIFO is here so the system can chain its
  // own work and so the radio knows when there is nothing left to do.
  if (g_net == nullptr || g_phase != kIdle || g_count >= kQueueLen) return false;
  g_queue[(g_head + g_count) % kQueueLen] = request;
  ++g_count;
  // It begins now and not in the next turn: a caller that asks how the radio is
  // right after asking has to hear the truth (measured in an app: `status off`
  // when a connection had just been asked, 31/10).
  start_next();
  return true;
}

Request make(RequestKind kind, uint32_t timeout_ms, bool needs_network) {
  Request request;
  request.kind = kind;
  request.timeout_ms = timeout_ms;
  request.needs_network = needs_network;
  // The waiter the shell left for this request, if any.
  request.done = g_pending_done;
  g_pending_done = nullptr;
  return request;
}

// Begins the request that is at the front. False when the radio refused it, and
// then the answer goes out at once: a request that cannot start must not wait for
// a deadline nobody is going to meet (docs/30).
// **A request of the radio is metered by what it asks of the device**: a scan, a page or the hour. The
// wait for an answer is not in this number —the request is a promise, and the promise carries its own
// time until it settles (docs/30).
uint8_t request_meter(uint8_t kind) {
  switch (kind) {
    case kReqScan:
    case kReqConnect:
    case kReqConnectStored:
    case kReqAdd:
    case kReqForget:
      return meter::kNetWifi;
    case kReqHttp:
      return meter::kNetHttp;
    case kReqNtp:
      return meter::kNetNtp;
    default:
      return meter::kNetWifi;
  }
}

bool begin(Request& request) {
  meter::Scope scope(request_meter(request.kind));
  g_radio_on = true;
  // The light says the radio is working: on a board with no screen, "is it doing
  // something?" is the first question a person asks (docs/30, docs/50).
  led::set_state(led::State::kRadio);
  // The first step of a request that needs the network: on it, with what the board
  // has stored. The step is marked as taken either way, because a request that
  // finds itself already on the network has had it too: leaving the mark off made
  // the answer start the request a second time and the second start was refused as
  // busy (measured on the PC, 31/10).
  if (request.needs_network && !request.on_the_network) {
    if (!is_up()) {
      if (!g_net->wifi_start_stored()) {
        // No stored network to get on with, so this task cannot be done at all.
        g_refuse = "E_NONET";
        return false;
      }
      g_phase = kConnecting;
      return true;
    }
    request.on_the_network = true;
  }
  switch (request.kind) {
    case kReqConnect:
      if (!g_net->wifi_start(request.ssid, request.pass)) return false;
      g_phase = kConnecting;
      return true;
    case kReqConnectStored:
      if (!g_net->wifi_start_stored()) return false;
      g_phase = kConnecting;
      return true;
    case kReqAdd:
      if (!g_net->wifi_store(request.ssid, request.pass)) return false;
      if (!g_net->wifi_start_stored()) return false;
      g_phase = kConnecting;
      return true;
    case kReqScan:
      if (!g_net->wifi_scan_start()) return false;
      g_phase = kScanning;
      return true;
    case kReqProvision:
      return begin_provision(request);
    case kReqForget:
      begin_forget();
      return true;
    case kReqNtp:
      if (!g_net->ntp_start(request.server)) return false;
      g_phase = kNtp;
      return true;
    case kReqHttp: {
      hal::HttpOpts opts;
      opts.timeout_ms = request.timeout_ms;
      opts.max_bytes = request.max_bytes;
      opts.sink = request.sink;
      opts.sink_path = (request.sink != nullptr) ? request.sink_rel : nullptr;
      g_http_op = g_net->http_get_start(request.url, opts);
      if (g_http_op == 0) return false;
      g_phase = kHttp;
      return true;
    }
    default:
      return false;
  }
}

void start_next() {
  while (g_phase == kIdle && g_count > 0) {
    g_current = g_queue[g_head];
    g_head = static_cast<uint8_t>((g_head + 1) % kQueueLen);
    --g_count;
    g_deadline_ms = (g_current.timeout_ms > 0) ? now_ms() + g_current.timeout_ms : 0;
    if (begin(g_current)) return;
    // The radio refused it. The answer is given in the next turn: the one who
    // asked may still be inside its own call (docs/30).
    g_phase = kRefused;
  }
}

}  // namespace

bool stored_ssid(char* out, size_t cap) {
  if (g_net == nullptr) return false;
  return g_net->wifi_stored_ssid(out, cap);
}

bool scan_record(uint32_t index, char* ssid, size_t cap, int32_t& rssi) {
  if (g_net == nullptr) return false;
  return g_net->wifi_scan_record(index, ssid, cap, rssi);
}

// Every one of these only **asks**: it goes into the queue and the answer arrives
// in `poll()` through whoever is waiting (docs/30).
bool connect(const char* ssid, const char* pass, uint32_t timeout_ms) {
  if (ssid == nullptr || ssid[0] == '\0') return false;
  Request request = make(kReqConnect, timeout_ms, false);
  snprintf(request.ssid, sizeof(request.ssid), "%s", ssid);
  snprintf(request.pass, sizeof(request.pass), "%s", (pass != nullptr) ? pass : "");
  return push(request);
}

bool connect_stored(uint32_t timeout_ms) {
  return push(make(kReqConnectStored, timeout_ms, false));
}

bool scan_start(uint32_t timeout_ms) { return push(make(kReqScan, timeout_ms, false)); }

bool add_network(const char* ssid, const char* pass, uint32_t timeout_ms) {
  if (ssid == nullptr || ssid[0] == '\0') return false;
  Request request = make(kReqAdd, timeout_ms, false);
  snprintf(request.ssid, sizeof(request.ssid), "%s", ssid);
  snprintf(request.pass, sizeof(request.pass), "%s", (pass != nullptr) ? pass : "");
  return push(request);
}

bool forget() { return push(make(kReqForget, 0, false)); }

// The clock is set from a server on the network. It does not keep the radio on:
// the answer is a time, not a connection (docs/60, docs/75).
bool is_up() { return (g_net != nullptr) && (g_net->wifi_state() == hal::NetState::kUp); }

bool ntp(const char* server, uint32_t timeout_ms) {
  // Without a network a time server answers nothing, and the person would watch a
  // command that waits for a deadline it cannot meet: it is refused at once and
  // the shell says why (measured on the board, 31/10).
  Request request = make(kReqNtp, timeout_ms, true);
  if (server != nullptr) snprintf(request.server, sizeof(request.server), "%s", server);
  return push(request);
}

// A page is a task of the queue like the time: it gets on the network with what the
// board has stored, downloads, answers and lets go (docs/30, docs/75).
// **One page at a time**: the answer of a page is a buffer the board lends, a status and the path of
// the file it may have gone to, and a second page in the queue would take them from the first
// (docs/30, docs/75).
bool page_pending() {
  if (g_phase != kIdle && g_current.kind == kReqHttp) return true;
  for (uint8_t i = 0; i < g_count; ++i) {
    const uint8_t at = static_cast<uint8_t>((g_head + i) % kQueueLen);
    if (g_queue[at].kind == kReqHttp) return true;
  }
  return false;
}

// The place a page is written to, resolved before the request starts: the unit and the path inside
// it, or nothing when the mount is not there or is not writable (docs/70).
bool resolve_sink(const char* path, hal::IStorage*& storage, char* rel, size_t cap) {
  if (path == nullptr || path[0] == '\0') return false;
  if (!vfs::resolve(path, storage, rel, cap)) return false;
  return vfs::is_writable(path);
}

// The three shapes of a page are one request (docs/30): in memory, in memory **and then a file** when
// it does not fit, or straight to the file with `max_bytes` at zero. `person` says who is asking, because
// a page is a write and a write has a policy (docs/70).
luabind::NetRefuse http_request(const char* url, uint32_t max_bytes, const char* spill_path,
                                uint32_t timeout_ms, bool person) {
  if (g_net == nullptr) return luabind::NetRefuse::kNoRadio;
  if (url == nullptr || url[0] == '\0') return luabind::NetRefuse::kInval;
  if (max_bytes == 0 && spill_path == nullptr) return luabind::NetRefuse::kInval;
  if (strlen(url) >= kNetUrlCap) return luabind::NetRefuse::kTooBig;
  hal::IStorage* storage = nullptr;
  char rel[kSinkPathCap] = {0};
  // **The page lands where the caller could write a file**: the door and the ground of docs/70 are the
  // filesystem's, and a download that answered to a different rule would be a hole in them
  // (docs/30, docs/70).
  char place[kSinkPathCap] = {0};
  if (spill_path != nullptr) {
    // The path as the filesystem sees it, then whether the drive is there —a mount that is not is a place
    // that does not exist and not a permission problem—, then the policy, and the directories on the way
    // (docs/30, docs/70).
    if (!app_path(spill_path, place, sizeof(place))) return luabind::NetRefuse::kInval;
    if (!resolve_sink(place, storage, rel, sizeof(rel))) return luabind::NetRefuse::kNoVolume;
    luabind::FsError fs_err = luabind::FsError::kOk;
    if (!write_allowed(place, person, fs_err)) return luabind::NetRefuse::kInval;
    if (!app_make_way(place)) return luabind::NetRefuse::kNoVolume;
  }
  if (page_pending()) return luabind::NetRefuse::kBusy;
  Request request = make(kReqHttp, timeout_ms, true);
  snprintf(request.url, sizeof(request.url), "%s", url);
  request.max_bytes = max_bytes;
  request.sink = storage;
  if (storage != nullptr) snprintf(request.sink_rel, sizeof(request.sink_rel), "%s", rel);
  if (!push(request)) return luabind::NetRefuse::kBusy;
  // The answer of this page, and not the one of the page before: a page is one at a time.
  g_http_saved = false;
  g_http_saved_path[0] = '\0';
  if (spill_path != nullptr) snprintf(g_http_saved_path, sizeof(g_http_saved_path), "%s", place);
  return luabind::NetRefuse::kOk;
}

luabind::NetRefuse http_get(const char* url, uint32_t max_bytes, uint32_t timeout_ms) {
  return http_request(url, max_bytes, nullptr, timeout_ms, true);
}

luabind::NetRefuse http_get_spill(const char* url, uint32_t max_bytes, const char* spill_path,
                                  uint32_t timeout_ms) {
  return http_request(url, max_bytes, spill_path, timeout_ms, true);
}

luabind::NetRefuse http_download(const char* url, const char* path, uint32_t timeout_ms) {
  return http_request(url, 0, path, timeout_ms, true);
}

// The same for an app, which writes where `fs.write` lets it and nowhere else (docs/70). It is not in the
// header because the only caller is the interpreter, through the operations installed below.
luabind::NetRefuse download_from_app(const char* url, const char* path, uint32_t timeout_ms) {
  return http_request(url, 0, path, timeout_ms, false);
}

bool http_saved() { return g_http_saved; }

const char* http_saved_path() { return g_http_saved_path; }

int http_status() { return g_http_status; }

const char* http_body(uint32_t& len) {
  len = g_http_body_len;
  return g_http_body;
}

uint32_t http_url_cap() { return kNetUrlCap; }

bool provisioned() { return is_provisioned(); }

// What an app gets: the transport the maker of the chip recommends, because a
// phone that is provisioning a board has no reason to leave its own network
// (docs/50).
static bool provision_ble(const char* pop, const char* service_name, uint32_t timeout_ms) {
  (void)service_name;
  Request request = make(kReqProvision, timeout_ms, false);
  request.transport = hal::ProvTransport::kBle;
  if (pop != nullptr) snprintf(request.pop, sizeof(request.pop), "%s", pop);
  return push(request);
}

bool provision(const char* pop, const char* service_name, hal::ProvTransport transport,
                uint32_t timeout_ms) {
  (void)service_name;
  Request request = make(kReqProvision, timeout_ms, false);
  request.transport = transport;
  if (pop != nullptr) snprintf(request.pop, sizeof(request.pop), "%s", pop);
  return push(request);
}

void boot(hal::IPlatform& platform) {
  g_net = platform.net();
  g_clock = platform.clock();
  g_caps = platform.caps().bits;
  g_phase = kIdle;
  g_deadline_ms = 0;

  luabind::NetOps ops = {};
  ops.connect = &connect;
  ops.connect_stored = &connect_stored;
  ops.provision = &provision_ble;
  ops.provisioned = &is_provisioned;
  ops.add_network = &add_network;
  ops.scan_start = &scan_start;
  ops.stored_ssid = &stored_ssid;
  ops.scan_record = &scan_record;
  ops.forget = &forget;
  ops.ntp_start = &ntp;
  ops.http_get = &http_get;
  ops.download = &download_from_app;
  ops.cancel = &cancel;
  ops.disconnect = &drop;
  ops.state = &state_code;
  ops.features = &features;
  luabind::set_net_ops(ops);
}

bool available() { return g_net != nullptr; }

uint8_t state() { return state_code(); }

const char* state_name() {
  if (g_net == nullptr) return "no radio";
  switch (g_net->wifi_state()) {
    case hal::NetState::kOff:
      return "off";
    case hal::NetState::kConnecting:
      return "connecting";
    case hal::NetState::kUp:
      return "up";
    case hal::NetState::kFailed:
      return "failed";
  }
  return "?";
}

void cancel_public() { cancel(); }

void stop() { stop_all(); }

void set_done(Done done) { g_pending_done = done; }

bool busy() { return g_phase != kIdle || g_count > 0; }

const char* busy_with() {
  switch (g_phase) {
    case kScanning:
      return "scanning";
    case kConnecting:
      return "connecting";
    case kProvisioning:
      return "provisioning";
    case kNtp:
      return "synchronising the clock";
    case kHttp:
      return "downloading a page";
    default:
      break;
  }
  return (g_count > 0) ? "a queue of requests" : "idle";
}

void poll() {
  if (g_net == nullptr) return;

  // Nothing running: the front of the queue gets its turn, and when the queue is
  // empty the radio is switched off unless what it has is the answer somebody
  // asked for (docs/75).
  if (g_phase == kIdle) start_next();
  if (g_phase == kRefused) {
    const char* why = g_refuse;
    g_refuse = "E_BUSY";
    finish(false, why);
    return;
  }
  if (g_phase == kIdle) {
    // Nothing left to do: the radio goes off. There is no request that keeps it on,
    // because the network is a thing a task uses and then lets go of, not a state
    // the board sits in (docs/75).
    if (g_count == 0 && g_radio_on) {
      g_net->wifi_disconnect();
      g_radio_on = false;
    }
    return;
  }

  if (g_phase == kNtp) {
    int64_t epoch = 0;
    if (g_net->ntp_synced(epoch)) {
      // The clock is the board's, and both bands set it the same way: the real
      // one is set by lwip and the fake one by the emulator, and the kernel only
      // tells the clock what time it is (docs/60).
      if (g_clock != nullptr) g_clock->set_epoch_ms(epoch);
      finish(true, nullptr);
    } else if (now_ms() >= g_deadline_ms) {
      finish(false, "E_TIMEOUT");
    }
    return;
  }

  if (g_phase == kScanning) {
    if (g_net->wifi_scan_done()) {
      finish(true, nullptr, g_net->wifi_scan_count());
    } else if (now_ms() >= g_deadline_ms) {
      finish(false, "E_TIMEOUT");
    }
    return;
  }

  // The page: the board downloads it in a task of its own and answers here, so a
  // slow server costs the prompt nothing (docs/30).
  if (g_phase == kHttp) {
    const uint32_t op = g_http_op;
    bool done = false;
    hal::HttpResult result;
    if (!g_net->http_poll(op, done, result)) {
      g_http_op = 0;
      g_net->http_release(op);
      finish(false, "E_HTTP");
      return;
    }
    if (!done) {
      if (now_ms() >= g_deadline_ms) {
        // Nobody is going to want this page: the buffers go back as soon as the
        // board's task notices (docs/75).
        g_http_op = 0;
        g_net->http_release(op);
        finish(false, "E_TIMEOUT");
      }
      return;
    }
    g_http_status = result.status;
    g_http_body = result.body;
    g_http_body_len = result.body_len;
    g_http_saved = result.saved;
    const char* code = nullptr;
    switch (result.error) {
      case hal::HttpError::kNone:
        break;
      case hal::HttpError::kTls:
        code = "E_TLS";
        break;
      case hal::HttpError::kTooBig:
        code = "E_TOOBIG";
        break;
      case hal::HttpError::kNoMemory:
        code = "E_NOSPC";
        break;
      case hal::HttpError::kStorage:
        code = "E_IO";
        break;
      default:
        code = "E_HTTP";
        break;
    }
    // The waiter reads the page inside this call: the shell prints it and the
    // interpreter copies it into a string. `finish()` gives the buffer back right
    // after that, so it is not released here.
    finish(code == nullptr, code);
    return;
  }

  // Provisioning first: when the credentials are in, the board connects with
  // what it stored, and the same promise is kept until the station is up.
  if (g_phase == kProvisioning) {
    const hal::ProvState prov = g_net->prov_state();
    if (prov == hal::ProvState::kFailed) {
      g_net->prov_stop();
      finish(false, "E_NONET");
      return;
    }
    if (prov == hal::ProvState::kDone) {
      if (!g_net->wifi_start_stored()) {
        finish(false, "E_NONET");
        return;
      }
      // The credentials are in, so what is left is an association like any other
      // and it gets a normal deadline. The provisioning deadline is for a person
      // with a phone in their hand; keeping it here froze the prompt for three
      // minutes when the association did not come up (measured on the board,
      // 31/10).
      g_deadline_ms = now_ms() + kProvConnectMs;
      g_phase = kConnecting;
    } else if (now_ms() >= g_deadline_ms) {
      g_net->prov_stop();
      finish(false, "E_TIMEOUT");
      return;
    } else {
      return;
    }
  }

  const hal::NetState state = g_net->wifi_state();
  if (state == hal::NetState::kUp) {
    finish(true, nullptr);
    return;
  }
  if (state == hal::NetState::kFailed) {
    finish(false, "E_NONET");
    return;
  }
  if (now_ms() >= g_deadline_ms) {
    // The attempt is dropped, not left running: an association that nobody waits
    // for would keep the radio on for nothing (docs/75).
    g_net->wifi_disconnect();
    finish(false, "E_TIMEOUT");
  }
}

}  // namespace net
}  // namespace kernel
