// The serial shell: line editing, the command table, and the app console once
// there is an app to run. It is the only user interface there is, because the
// board has no screen (docs/10 and docs/50).
#include "kernel/shell.h"

#include <stdlib.h>
#include <string.h>

#include "hal/hal.h"
#include "kernel/apps.h"
#include "kernel/button.h"
#include "kernel/kernel.h"
#include "kernel/led.h"
#include "kernel/log.h"
#include "kernel/net.h"
#include "kernel/power.h"
#include "kernel/version.h"
#include "kernel/vfs.h"
#include "luabind/luabind.h"
#include "pal/pal.h"

namespace kernel {
namespace shell {
namespace {

const size_t kLineBytes = 128;
const size_t kPrintCap = 1024;
// Largest entry chunk that will be run. It was 4 096 B and the `matrix` app grew
// past it, which is a limit saying no to an app that fits the board, so it is
// 8 192 B now; it is static RAM and it costs that, so it does not grow again
// without a number (docs/75).
const size_t kSourceCap = 8192;
const size_t kDefaultHeapBytes = 64 * 1024;   // used until the board ceiling is measured

char g_line[kLineBytes];
size_t g_len = 0;
bool g_last_was_cr = false;
// The app that is alive between turns: while it is, the prompt is not shown and
// the keyboard is not the shell's.
bool g_app_alive = false;
// True while the prompt is an interpreter (the `lua` command): the lines are not
// commands, they are Lua, and the prompt says so (docs/30).
bool g_lua = false;
// ESC pressed in a row: three of them stop a command that is waiting, the same
// way they leave an app (docs/30).
const uint8_t kEscToStop = 3;
uint8_t g_esc = 0;

void out_bytes(const void* data, size_t len) {
  pal::IConsole* console = pal::system().console();
  if (console == nullptr) return;
  console->write(data, len);
}

void out(const char* text) { out_bytes(text, strlen(text)); }

void newline() { out("\r\n"); }

void spaces(size_t count) {
  while (count > 0) {
    out(" ");
    --count;
  }
}

// A word of a command line is 127 bytes at most, and a path with spaces goes
// between quotes: `cat "res/el meu fitxer.txt"`. There is no escaping, because a
// quote is a quote and nothing in a file system of this size needs more.
const size_t kWordCap = 128;
const size_t kWordsBad = static_cast<size_t>(-1);

// Words of an argument, or kWordsBad: a quote left open, a word too long, or
// more words than the command takes. The shell's own line is cut at 128 B before
// it gets here, so a word over 127 B only comes from a caller with a longer
// line, and then it is refused instead of cut.
size_t split_words(const char* text, char words[][kWordCap], size_t max_words) {
  size_t count = 0;
  const char* cursor = text;
  while (true) {
    while (*cursor == ' ') ++cursor;
    if (*cursor == '\0') return count;
    if (count >= max_words) return kWordsBad;
    size_t used = 0;
    bool quoted = false;
    while (*cursor != '\0' && (quoted || *cursor != ' ')) {
      if (*cursor == '"') {
        quoted = !quoted;
        ++cursor;
        continue;
      }
      if (used + 1 >= kWordCap) return kWordsBad;
      words[count][used++] = *cursor++;
    }
    if (quoted) return kWordsBad;
    words[count][used] = '\0';
    ++count;
  }
}

// Says why a line was refused, in one place, so every command refuses it with
// the same words.
bool read_words(const char* argument, char words[][kWordCap], size_t max_words, size_t& count) {
  count = split_words(argument, words, max_words);
  if (count != kWordsBad) return true;
  count = 0;
  out("bad line: a quote is open, a word is over 127 B, or there are too many words\r\n");
  out("a path with spaces goes in quotes\r\n");
  return false;
}

void out_num(uint32_t value) {
  char buf[12];
  size_t i = sizeof(buf) - 1;
  buf[i] = '\0';
  if (value == 0) {
    buf[--i] = '0';
  } else {
    while (value > 0 && i > 0) {
      buf[--i] = static_cast<char>('0' + (value % 10));
      value /= 10;
    }
  }
  out(&buf[i]);
}

void print_help() {
  out("  help              this list\r\n");
  out("  version           version and board\r\n");
  out("  caps              what this board has\r\n");
  out("  mem               free memory and buffers\r\n");
  out("  apps              the apps in /apps\r\n");
  out("  mounts            the mounted file systems and their room\r\n");
  out("  run <id>          run an app\r\n");
  out("  wifi              the stored network and the radio\r\n");
  out("  wifi scan         the networks the radio sees\r\n");
  out("  wifi add <ssid> [password]  store a network and go on it\r\n");
  out("  wifi connect      go on the network the board has stored\r\n");
  out("  wifi forget       forget the stored network\r\n");
  out("  wifi stop         drop whatever the radio is doing\r\n");
  out("  provision [pop]   the QR and the Espressif app, over Bluetooth\r\n");
  out("  provision softap [pop]  the same over the board's own access point\r\n");
  out("  ntp [server]      set the clock from a time server\r\n");
  out("  http <url>        download a page and show its status\r\n");
  out("  power [cpu [mhz]] what this board can do about energy\r\n");
  out("  led [rrggbb|off]  the light of the board\r\n");
  out("  button            the button, right now\r\n");
  out("  sleep [light|deep] <ms>  sleep, and come back at a time\r\n");
  out("  open [app] <path> open a file with the app that handles it\r\n");
  out("  ls [path]         list a directory (default /)\r\n");
  out("  cat <path>        print a file\r\n");
  out("  lua               the prompt becomes the interpreter (`exit` leaves)\r\n");
  out("  log               last lines of the log ring\r\n");
  out("  verbose on|off    mirror the log to the console too\r\n");
  out("  clear             clear the screen\r\n");
  out("  reboot            restart\r\n");
  out("three ESC in a row leave an app, or stop a command that waits\r\n");
  out("a path with spaces goes in quotes: cat \"res/el meu fitxer.txt\"\r\n");
}

void print_version() {
  out("lunokOS ");
  out(kVersion);
  out("  board ");
  out(platform().id());
  newline();
}

void print_caps() {
  const hal::Caps& caps = platform().caps();
  for (uint32_t i = 0; i < hal::cap_count(); ++i) {
    const bool has = caps.has(static_cast<hal::Capability>(hal::cap_bit(i)));
    out("  ");
    out(hal::cap_name(i));
    out(has ? "  yes" : "  no");
    newline();
  }
}

void print_mem() {
  const pal::HeapInfo heap = pal::system().heap();
  if (heap.valid) {
    out("  free heap          ");
    out_num(heap.free_bytes);
    newline();
    out("  min free heap      ");
    out_num(heap.min_free_bytes);
    newline();
  } else {
    out("  heap               not available on this build\r\n");
  }
  // The last app, if one has run: what it asked for and what it was allowed.
  if (luabind::limit_bytes() > 0) {
    out("  lua peak / limit   ");
    out_num(static_cast<uint32_t>(luabind::peak_bytes()));
    out(" / ");
    out_num(static_cast<uint32_t>(luabind::limit_bytes()));
    newline();
  }
  pal::IQueue* queue = pal::system().queue();
  out("  event queue        ");
  out_num(queue->size());
  out("/");
  out_num(queue->capacity());
  newline();
  out("  log ring (bytes)   ");
  out_num(static_cast<uint32_t>(log_bytes()));
  newline();
}

// The app list is not stored anywhere: it is walked and printed as it is found,
// one manifest at a time (docs/40).
bool print_app(const apps::Entry& entry, void* context) {
  (void)context;
  const size_t id_len = strlen(entry.id);
  const size_t version_len = strlen(entry.version);
  out("  ");
  out(entry.id);
  spaces((id_len < 16) ? (16 - id_len) : 1);
  out(entry.version);
  spaces((version_len < 10) ? (10 - version_len) : 1);
  out(entry.title);
  // An app of another board is not hidden: it says so, with the boards it wants, so
  // that a person does not have to guess where it went (docs/40).
  if (!entry.works_here) {
    out("  [needs ");
    for (uint32_t i = 0; i < entry.board_count; ++i) {
      out((i == 0) ? "" : " or ");
      out(entry.boards[i]);
    }
    out("]");
  }
  newline();
  return true;
}

void print_apps() {
  if (apps::for_each(nullptr, &print_app, nullptr) == 0) {
    out("  no apps\r\n");
  }
}

// One line per mount: what it is, whether it can be written and how much room is
// left. Two of the numbers M4 asks for come from here (docs/90).
void print_mounts() {
  const uint32_t count = vfs::mount_count();
  for (uint32_t i = 0; i < count; ++i) {
    const char* path = vfs::mount_path(i);
    out("  ");
    out(path);
    spaces(strlen(path) < 12 ? 12 - strlen(path) : 1);
    out(vfs::mount_kind(i));
    spaces(strlen(vfs::mount_kind(i)) < 10 ? 10 - strlen(vfs::mount_kind(i)) : 1);
    out(vfs::mount_writable(i) ? "rw  " : "ro  ");

    uint64_t total = 0;
    uint64_t free_bytes = 0;
    if (vfs::space(path, total, free_bytes)) {
      out("free ");
      out_num(static_cast<uint32_t>(free_bytes));
      out(" of ");
      out_num(static_cast<uint32_t>(total));
      out(" B");
    } else {
      // Not "no room": the values of the NVS are not files, and space() cannot say
    // anything about them. Saying "unknown" is the truth (docs/90).
    out("space unknown");
    }
    newline();
  }
}

class ListPrinter : public hal::ListSink {
 public:
  bool on_entry(const hal::ListEntry& entry) override {
    if (entry.type == hal::NodeType::kDir) {
      out("  <dir>   ");
      out(entry.name);
    } else {
      out("  file    ");
      out(entry.name);
      out("  ");
      out_num(entry.size);
    }
    newline();
    return true;
  }
};

void list_at(const char* path) {
  ListPrinter printer;
  if (!vfs::list(path, printer)) {
    out("cannot list: ");
    out(path);
    newline();
  }
}

// cat: prints the file, and says which of the two things went wrong instead of
// one word for both. It looks first so a missing file is not an error inside the
// backend, which logs it (docs/70).
bool print_file(const char* path) {
  hal::Stat info;
  if (!vfs::stat(path, info)) {
    out("no such file: ");
    out(path);
    newline();
    return false;
  }
  if (info.type != hal::NodeType::kFile) {
    out("that is not a file: ");
    out(path);
    newline();
    return false;
  }
  char buf[kPrintCap];
  size_t len = 0;
  if (!vfs::read_file(path, buf, sizeof(buf), len)) {
    out("cannot read: ");
    out(path);
    newline();
    return false;
  }
  if (len > 0) out_bytes(buf, len);
  if (len > 0 && buf[len - 1] != '\n') newline();
  return true;
}

// The entry chunk is handed to the interpreter whole, because Lua compiles a
// script from its source and cannot be fed a line at a time. The buffer is static
// and the ceiling is checked before reading (docs/20).
char g_source[kSourceCap];

struct RunTarget {
  char path[96];
  char title[48];
  char version[16];
  char opens[16];
  // The boards the app asks for, and whether this one is among them: the refusal
  // says both things, because being refused by name is not a mystery (docs/40).
  char boards[apps::Entry::kMaxBoards][24];
  uint32_t board_count;
  bool works_here;
  bool found;
};

bool find_entry(const apps::Entry& entry, void* context) {
  RunTarget* target = static_cast<RunTarget*>(context);
  static const char kPrefix[] = "/apps/";
  const size_t prefix = sizeof(kPrefix) - 1;
  const size_t id_len = strlen(entry.id);
  const size_t entry_len = strlen(entry.entry_file);
  if (prefix + id_len + 1 + entry_len + 1 > sizeof(target->path)) return true;

  size_t used = 0;
  memcpy(target->path + used, kPrefix, prefix);
  used += prefix;
  memcpy(target->path + used, entry.id, id_len);
  used += id_len;
  target->path[used++] = '/';
  memcpy(target->path + used, entry.entry_file, entry_len);
  used += entry_len;
  target->path[used] = '\0';
  memcpy(target->title, entry.title, sizeof(target->title));
  memcpy(target->version, entry.version, sizeof(target->version));
  memcpy(target->opens, entry.opens, sizeof(target->opens));
  memcpy(target->boards, entry.boards, sizeof(target->boards));
  target->board_count = entry.board_count;
  target->works_here = entry.works_here;
  target->found = true;
  return false;  // stop the walk: there is nothing else to look for
}

// Finds the app and reads its entry chunk. It prepares no state: whether the app
// takes the foreground or is only a window is decided by whoever calls this.
bool load_source(const char* id, RunTarget& target, size_t& len) {
  target.path[0] = '\0';
  target.found = false;
  apps::for_each(id, &find_entry, &target);
  if (!target.found) {
    out("no such app: ");
    out(id);
    newline();
    return false;
  }
  // An app of another board is refused **here**, before a VM is built, and it says
  // which boards it wants and which board this is: a listed app that will not run is
  // the one thing a person must not have to guess (docs/40).
  if (!target.works_here) {
    out(id);
    out(" runs on");
    for (uint32_t i = 0; i < target.board_count; ++i) {
      out((i == 0) ? " " : ", ");
      out(target.boards[i]);
    }
    out(" and this board is ");
    out(platform().id());
    newline();
    return false;
  }
  if (!vfs::read_file(target.path, g_source, kSourceCap - 1, len)) {
    out("cannot read: ");
    out(target.path);
    newline();
    return false;
  }
  if (len == kSourceCap - 1) {
    out("too big to run: ");
    out(target.path);
    newline();
    return false;
  }
  return true;
}

bool run_app(const char* id, const char* open_path, bool restore = false) {
  RunTarget target;
  size_t len = 0;
  if (!load_source(id, target, len)) return false;

  luabind::Boot boot;
  boot.board = platform().id();
  boot.version = kVersion;
  boot.emulator = platform().emulator();
  boot.app_id = id;
  boot.app_title = target.title;
  boot.app_version = target.version;
  boot.app_opens = target.opens;
  // True when the app is being brought back from a sleep: it is how an app knows
  // to read what it saved instead of starting from nothing (docs/90).
  boot.args_restore = restore;
  char extension[16];
  if (open_path != nullptr) {
    const bool has_ext = apps::extension_of(open_path, extension, sizeof(extension));
    boot.args_path = open_path;
    boot.args_ext = has_ext ? extension : nullptr;
    boot.args_source = restore ? "sleep" : "open";
  }

  const uint32_t limit_kb = platform().heap_limit_kb();
  const size_t limit = (limit_kb > 0) ? (static_cast<size_t>(limit_kb) * 1024) : kDefaultHeapBytes;
  if (!luabind::start(boot, limit)) {
    out("cannot start the interpreter\r\n");
    return false;
  }

  // The app is compiled and given a task, but not run here: the first turn is
  // the first poll, and from then on the kernel keeps turning (docs/30).
  if (!luabind::run_source(target.path, g_source, len)) {
    luabind::stop();
    return false;
  }
  if (!luabind::poll()) {  // it never waited: it is already over
    const int code = luabind::exit_code();
    luabind::stop();
    if (code != 0) {
      out("app exit ");
      out_num(static_cast<uint32_t>(code));
      newline();
    }
    return true;
  }
  g_app_alive = true;
  // The light says nothing about an app being in front: it is on the console, and a
  // light that is on costs milliamps (docs/30, docs/75).
  return true;
}

// When more than one app handles the same extension, the shell asks which one,// every time. Remembering the answer was wrong: it hides the other app and there
// is no way to reach it without naming it on the command line. docs/40 says how
// it behaves.
const int kMaxChoices = 8;
// The same ceiling the virtual file system uses: a path that fits there fits here.
const size_t kChoicePathBytes = 128;

char g_choice_ext[16];
char g_choice_path[kChoicePathBytes];
char g_choice_ids[kMaxChoices][32];
uint32_t g_choice_count = 0;
bool g_choosing = false;

void copy_text(char* dst, size_t cap, const char* src) {
  if (cap == 0) return;
  size_t i = 0;
  for (; src[i] != '\0' && i + 1 < cap; ++i) dst[i] = src[i];
  dst[i] = '\0';
}

bool collect_choice(const apps::Entry& entry, void* context) {
  (void)context;
  if (entry.opens[0] == '\0') return true;
  if (strcmp(entry.opens, g_choice_ext) != 0) return true;
  if (g_choice_count >= static_cast<uint32_t>(kMaxChoices)) return false;
  copy_text(g_choice_ids[g_choice_count], sizeof(g_choice_ids[0]), entry.id);
  ++g_choice_count;
  return true;
}

// Asks which app to use and leaves the shell waiting for the number.
bool ask_which_app(const char* ext, const char* path) {
  // A path that does not fit is refused here and said out loud: a cut path would
  // start the app with a file nobody asked for.
  const size_t path_len = strlen(path);
  if (path_len >= sizeof(g_choice_path)) {
    out("that path is too long to keep while asking: ");
    out_num(static_cast<uint32_t>(path_len));
    out(" bytes, the most is ");
    out_num(static_cast<uint32_t>(sizeof(g_choice_path) - 1));
    newline();
    return false;
  }
  copy_text(g_choice_ext, sizeof(g_choice_ext), ext);
  copy_text(g_choice_path, sizeof(g_choice_path), path);
  g_choice_count = 0;
  apps::for_each(nullptr, &collect_choice, nullptr);
  if (g_choice_count == 0) {
    out("no app opens .");
    out(ext);
    newline();
    return false;
  }

  out("more than one app opens .");
  out(ext);
  out(":\r\n");
  for (uint32_t i = 0; i < g_choice_count; ++i) {
    out("  ");
    out_num(i + 1);
    out(") ");
    out(g_choice_ids[i]);
    newline();
  }
  out("which one? (1-");
  out_num(g_choice_count);
  out(") ");
  g_choosing = true;
  return true;
}

// The line that answers the question: a number, or anything else to give up.
bool answer_choice(const char* line) {
  g_choosing = false;
  while (*line == ' ') ++line;
  uint32_t chosen = 0;
  for (const char* p = line; *p >= '0' && *p <= '9'; ++p) {
    chosen = chosen * 10 + static_cast<uint32_t>(*p - '0');
  }
  if (chosen == 0 || chosen > g_choice_count) {
    out("cancelled\r\n");
    return true;
  }

  char id[32];
  char path[kChoicePathBytes];
  copy_text(id, sizeof(id), g_choice_ids[chosen - 1]);
  copy_text(path, sizeof(path), g_choice_path);
  return run_app(id, path);
}

// open [app] <path>: the file's extension picks the app, unless the app is named.
bool open_file(const char* argument) {
  // One word is a path; two are an app and a path. The words come from the one
  // tokenizer, so a path with spaces goes between quotes and a long path is
  // refused instead of being cut (docs/70).
  char words[2][kWordCap];
  size_t count = 0;
  if (!read_words(argument, words, 2, count)) return false;
  if (count == 0) {
    out("usage: open [app] <path>\r\n");
    return false;
  }

  char app_id[32];
  app_id[0] = '\0';
  const char* path = words[count - 1];
  if (count == 2) {
    if (strlen(words[0]) + 1 > sizeof(app_id)) {
      out("no app has a name that long: ");
      out(words[0]);
      newline();
      return false;
    }
    copy_text(app_id, sizeof(app_id), words[0]);
  }

  hal::Stat info;
  if (!vfs::stat(path, info)) {
    out("no such file: ");
    out(path);
    newline();
    return false;
  }
  if (info.type != hal::NodeType::kFile) {
    out("that is not a file: ");
    out(path);
    newline();
    return false;
  }

  char extension[16];
  if (!apps::extension_of(path, extension, sizeof(extension))) {
    out("that file has no extension to open it with\r\n");
    return false;
  }

  if (app_id[0] != '\0') return run_app(app_id, path);  // the app was named

  apps::Entry handler;
  uint32_t matches = 0;
  if (!apps::find_for_extension(extension, handler, matches)) {
    out("no app opens .");
    out(extension);
    newline();
    return false;
  }
  if (matches == 1) return run_app(handler.id, path);

  // More than one app handles it: the shell asks, every time (docs/40).
  return ask_which_app(extension, path);
}

// ------------------------------------------------------------- the radio
// `wifi` and `provision` are commands like the rest, not apps: the network of the
// board is the system's, and having to install an app to reach it was a detour
// (docs/30, docs/50). A command does not block: it starts the job and hears the
// answer in the kernel's turn, where the prompt comes back.
const uint32_t kScanMs = 15000;
const uint32_t kAddMs = 20000;
const uint32_t kProvisionMs = 180000;
const uint32_t kNtpMs = 40000;
// A page gets the deadline of a slow server with room to spare, and it may cost
// this much memory out of the free heap (docs/75).
const uint32_t kHttpMs = 40000;
const uint32_t kHttpBytes = 16384;
// How much of the page is printed by hand. A console is not a browser and the rest
// is said as a number instead of shown (docs/30).
const uint32_t kHttpShow = 800;

enum class RadioJob { kNone, kScan, kAdd, kConnect, kProvision, kNtp, kHttp };
RadioJob g_job = RadioJob::kNone;
char g_job_ssid[33];

void out_int(int32_t value) {
  if (value < 0) {
    out("-");
    out_num(static_cast<uint32_t>(-value));
    return;
  }
  out_num(static_cast<uint32_t>(value));
}

void print_stored() {
  char ssid[33];
  if (net::stored_ssid(ssid, sizeof(ssid))) {
    out("network: ");
    out(ssid);
    newline();
  } else {
    out("network: none\r\n");
  }
}

void print_networks(uint32_t count) {
  out("networks: ");
  out_num(count);
  newline();
  for (uint32_t i = 0; i < count; ++i) {
    char ssid[33];
    int32_t rssi = 0;
    if (!net::scan_record(i, ssid, sizeof(ssid), rssi)) continue;
    out("  ");
    out_num(i + 1);
    out(") ");
    // A network that does not say its name is listed as such: an entry that is
    // skipped is indistinguishable from a scan that returned nothing (docs/50).
    out((ssid[0] != '\0') ? ssid : "(hidden network)");
    out("  (");
    out_int(rssi);
    out(" dBm)");
    newline();
  }
}

void say_failed(const char* what, const char* code) {
  out(what);
  out((code != nullptr) ? code : "E_NONET");
  newline();
}

// The answer of the one radio job that was running. It arrives in the kernel's
// turn, so this is where the prompt comes back (docs/30).
void radio_done(bool ok, const char* code, uint32_t count) {
  const RadioJob job = g_job;
  g_job = RadioJob::kNone;
  if (job == RadioJob::kScan) {
    if (ok) {
      print_networks(count);
    } else {
      say_failed("wifi: the scan failed, ", code);
    }
  } else if (job == RadioJob::kAdd) {
    if (ok) {
      out("wifi: on the network ");
      out(g_job_ssid);
      newline();
    } else {
      say_failed("wifi: no network, ", code);
    }
  } else if (job == RadioJob::kNtp) {
    if (ok) {
      // The clock is the board's and it has just been set: what a person wants to
      // see is the time it now believes (docs/60).
      hal::IClock* clock = platform().clock();
      const int64_t epoch = (clock != nullptr) ? clock->epoch_ms() : 0;
      out("time: ");
      out_num(static_cast<uint32_t>(epoch / 1000));
      out(" (epoch seconds, from ");
      out((clock != nullptr) ? clock->source() : "none");
      out(")\r\n");
    } else if (code != nullptr && strcmp(code, "E_NONET") == 0) {
      // It could not even get on a network, and that is worth its own sentence:
      // "no time came back" would send a person looking at the time server.
      out("ntp: the board has no network to use, try `wifi add`\r\n");
    } else {
      say_failed("ntp: no time came back, ", code);
    }
  } else if (job == RadioJob::kConnect) {
    if (ok) {
      print_stored();
    } else {
      say_failed("wifi: the board did not get on the network, ", code);
    }
  } else if (job == RadioJob::kProvision) {
    if (ok) {
      print_stored();
    } else {
      say_failed("provision: nothing came of it, ", code);
    }
  } else if (job == RadioJob::kHttp) {
    if (ok) {
      uint32_t len = 0;
      const char* body = net::http_body(len);
      out("http: ");
      out_int(net::http_status());
      out(" ");
      out_num(len);
      out(" bytes\r\n");
      // The body is bytes and not text, so it goes out with its length and only
      // the beginning of it: the rest is a number and not a screenful nobody
      // asked for (docs/30).
      const uint32_t shown = (len < kHttpShow) ? len : kHttpShow;
      if (body != nullptr && shown > 0) out_bytes(body, shown);
      if (shown < len) {
        out("... ");
        out_num(len - shown);
        out(" bytes more");
      }
      newline();
    } else if (code != nullptr && strcmp(code, "E_NONET") == 0) {
      out("http: the board has no network to use, try `wifi add`\r\n");
    } else if (code != nullptr && strcmp(code, "E_TLS") == 0) {
      out("http: the certificate was not accepted\r\n");
    } else {
      say_failed("http: nothing came back, ", code);
    }
  }
  prompt();
}

bool take_radio(RadioJob job) {
  if (!net::available()) {
    // Not "this board has no radio": every ESP32 has one, and the Paper is a
    // board whose HAL does not implement it yet. Saying which of the two it is
    // saves the next person the same question (docs/50, docs/90).
    out("no radio in this build: the HAL of this board does not provide one\r\n");
    return false;
  }
  if (g_job != RadioJob::kNone || net::busy()) {
    // Saying what it is doing, because "busy" with no subject left the board
    // locked: a provisioning waiting for a phone answered `the radio is busy` to
    // everything and there was no way out (measured on the board, 31/10).
    out("the radio is busy: ");
    out(net::busy_with());
    out(", use `wifi stop` to drop it\r\n");
    return false;
  }
  g_job = job;
  net::set_done(&radio_done);
  return true;
}

void drop_radio() {
  g_job = RadioJob::kNone;
  net::set_done(nullptr);
}

// Drops whatever the radio is doing. False when it was doing nothing, which is
// not an error: the person asked to stop something that was already stopped.
bool stop_radio_now() {
  const bool doing = (g_job != RadioJob::kNone) || net::busy() || net::is_up();
  if (!doing) return false;
  // The radio goes off the network too: a board that nobody asked to keep
  // connected does not stay connected (docs/75).
  net::stop();
  if (g_job != RadioJob::kNone) drop_radio();
  return true;
}

// wifi: what the board has, a scan, a network added by hand, a network forgotten.
bool command_wifi(const char* argument) {
  char words[3][kWordCap];
  size_t count = 0;
  if (!read_words(argument, words, 3, count)) return false;

  if (count == 0) {
    if (!net::available()) {
      out("wifi: no radio in this build: the HAL of this board does not provide one\r\n");
      return false;
    }
    // The state, and nothing else: `help` is where the commands are listed, and
    // a status that ends with a usage line says less than it looks (31/10).
    print_stored();
    out("state: ");
    out(net::state_name());
    newline();
    return true;
  }

  if (strcmp(words[0], "scan") == 0) {
    if (count != 1) {
      out("usage: wifi scan\r\n");
      return false;
    }
    if (!take_radio(RadioJob::kScan)) return false;
    if (!net::scan_start(kScanMs)) {
      drop_radio();
      out("wifi: the radio did not start the scan\r\n");
      return false;
    }
    return true;
  }

  // Getting on the network the board already knows, which is what a person
  // expects after a reboot: the record is there, the radio is not, and this is
  // the word that puts them together (docs/50).
  // `start` is the same word as `connect`: it is what a person types when the
  // board is off and they want it on (docs/50).
  if (strcmp(words[0], "connect") == 0 || strcmp(words[0], "start") == 0) {
    if (count != 1) {
      out("usage: wifi connect\r\n");
      return false;
    }
    char ssid[33];
    if (!net::stored_ssid(ssid, sizeof(ssid))) {
      out("wifi: the board has no network to connect to\r\n");
      return false;
    }
    if (!take_radio(RadioJob::kConnect)) return false;
    if (!net::connect_stored(kAddMs)) {
      drop_radio();
      out("wifi: the radio did not start the connection\r\n");
      return false;
    }
    return true;
  }

  if (strcmp(words[0], "add") == 0) {
    if (count < 2 || count > 3) {
      out("usage: wifi add <ssid> [password]\r\n");
      return false;
    }
    if (!take_radio(RadioJob::kAdd)) return false;
    copy_text(g_job_ssid, sizeof(g_job_ssid), words[1]);
    // The password goes on the command line, so it is in the terminal and in the
    // shell's history: there is no screen to type it on, and that is the price
    // (docs/50).
    if (!net::add_network(g_job_ssid, (count == 3) ? words[2] : "", kAddMs)) {
      drop_radio();
      out("wifi: the board did not take the network\r\n");
      return false;
    }
    return true;
  }

  if (strcmp(words[0], "forget") == 0 || strcmp(words[0], "del") == 0) {
    if (count != 1) {
      out("usage: wifi forget\r\n");
      return false;
    }
    // Forgetting is the person's decision over the radio, so whatever was
    // running is dropped first: a command that cannot answer leaves the board
    // with no way out (measured on the board, 31/10).
    if (stop_radio_now()) out("radio: stopped\r\n");
    if (!net::forget()) {
      out("wifi: the board did not let go of the network\r\n");
      return false;
    }
    out("network: none\r\n");
    return true;
  }

  // The way out of a job that is going nowhere: the access point goes down and
  // the prompt comes back (docs/30).
  if (strcmp(words[0], "stop") == 0) {
    if (count != 1) {
      out("usage: wifi stop\r\n");
      return false;
    }
    if (!stop_radio_now()) {
      out("radio: nothing to stop\r\n");
      return true;
    }
    out("radio: stopped\r\n");
    return true;
  }

  out("usage: wifi connect | wifi scan | wifi add <ssid> [password] | wifi forget | wifi stop\r\n");
  return false;
}

// ntp [server]: sets the clock from a time server. Short on purpose: it is the
// command a person types to see whether the board knows what day it is (docs/60).
bool command_ntp(const char* argument) {
  char words[1][kWordCap];
  size_t count = 0;
  if (!read_words(argument, words, 1, count)) return false;
  if (!take_radio(RadioJob::kNtp)) return false;
  // `ntp` is a task of the queue: it brings the network up by itself and lets go
  // of it when it is done, so a person does not have to connect first (docs/75).
  if (!net::ntp((count > 0) ? words[0] : nullptr, kNtpMs)) {
    drop_radio();
    out("ntp: the radio did not take it\r\n");
    return false;
  }
  return true;
}

// http <url>: a page, downloaded and shown. It is a task of the queue like the
// time: it brings the network up by itself and lets go of it afterwards, so a
// person does not have to connect first (docs/30, docs/75).
bool command_http(const char* argument) {
  char words[1][kWordCap];
  size_t count = 0;
  if (!read_words(argument, words, 1, count)) return false;
  if (count != 1) {
    out("usage: http <url>\r\n");
    return false;
  }
  if (!take_radio(RadioJob::kHttp)) return false;
  const luabind::NetRefuse refused = net::http_get(words[0], kHttpBytes, kHttpMs);
  if (refused != luabind::NetRefuse::kOk) {
    drop_radio();
    if (refused == luabind::NetRefuse::kTooBig) {
      // The address does not fit, and it is said with the number: half an address
      // is a page nobody asked for (docs/30).
      out("http: that address is longer than the ");
      out_num(net::http_url_cap());
      out(" bytes the system carries\r\n");
    } else if (refused == luabind::NetRefuse::kInval) {
      out("http: there is no address to fetch\r\n");
    } else {
      out("http: the radio did not take it\r\n");
    }
    return false;
  }
  return true;
}

// power: what this board can do about energy, with the numbers and not with
// adjectives (docs/30, docs/90).
bool command_power(const char* argument) {
  char words[2][kWordCap];
  size_t count = 0;
  if (!read_words(argument, words, 2, count)) return false;

  // power cpu [mhz]: the speed of the processor, read or moved. It is part of the
  // same decision as the sleep, and the steps are the board's (docs/50).
  if (count > 0 && strcmp(words[0], "cpu") == 0) {
    if (count == 1) {
      out("cpu: ");
      out_num(power::cpu_mhz());
      out(" MHz\r\n");
      return true;
    }
    if (count != 2) {
      out("usage: power cpu [mhz]\r\n");
      return false;
    }
    const int32_t asked = static_cast<int32_t>(strtoll(words[1], nullptr, 10));
    if (asked <= 0) {
      out("usage: power cpu [mhz]\r\n");
      return false;
    }
    if (!power::set_cpu_mhz(static_cast<uint32_t>(asked))) {
      // The number is said back: a speed that is not one of the board's steps is
      // refused and not rounded, and the person has to know which ones there are.
      out("cpu: this board does not run at ");
      out_int(asked);
      out(" MHz (80, 160 or 240)\r\n");
      return false;
    }
    out("cpu: ");
    out_num(power::cpu_mhz());
    out(" MHz\r\n");
    return true;
  }

  if (count != 0) {
    out("usage: power | power cpu [mhz]\r\n");
    return false;
  }
  out("sleep: ");
  const bool light = power::can_sleep(hal::SleepMode::kLight);
  const bool deep = power::can_sleep(hal::SleepMode::kDeep);
  if (light) out("light");
  if (deep) out((light ? " deep" : "deep"));
  if (!light && !deep) out("none");
  newline();
  out("wake from: ");
  out(power::wakes());
  newline();

  int mv = 0;
  int pct = 0;
  bool charging = false;
  if (power::battery(mv, pct, charging)) {
    out("battery: ");
    out_int(mv);
    out(" mV (");
    out_int(pct);
    out(charging ? " %, charging)" : " %)");
    newline();
  } else {
    // Not zero volts: "cannot tell", which is what this board means (docs/75).
    out("battery: no gauge on this board\r\n");
  }
  out("keep awake: ");
  out(power::keeping_awake() ? "yes" : "no");
  newline();
  out("cpu: ");
  out_num(power::cpu_mhz());
  out(" MHz\r\n");
  return true;
}

// sleep [light|deep] <ms> | at <epoch>: the person asks the board to stop, and
// writes down the hour it has to come back at. It is the command the energy
// numbers are measured with (docs/90).
bool command_sleep(const char* argument) {
  char words[3][kWordCap];
  size_t count = 0;
  if (!read_words(argument, words, 3, count)) return false;

  hal::SleepMode mode = hal::SleepMode::kLight;
  size_t first = 0;
  if (count > 0 && strcmp(words[0], "light") == 0) {
    mode = hal::SleepMode::kLight;
    first = 1;
  } else if (count > 0 && strcmp(words[0], "deep") == 0) {
    mode = hal::SleepMode::kDeep;
    first = 1;
  }
  if (count <= first) {
    out("usage: sleep [light|deep] <ms> | at <epoch seconds>\r\n");
    return false;
  }

  hal::IClock* clock = platform().clock();
  const int64_t now = (clock != nullptr) ? clock->epoch_ms() : 0;
  if (now <= 0) {
    // Without an hour there is no hour to come back at, and a board that sleeps
    // without one is a board that waits for a finger (docs/60).
    out("sleep: the board does not know the hour; set it first (`ntp`)\r\n");
    return false;
  }

  int64_t at = 0;
  if (count == first + 2 && strcmp(words[first], "at") == 0) {
    at = strtoll(words[first + 1], nullptr, 10) * 1000;
  } else if (count == first + 1) {
    const int64_t ms = strtoll(words[first], nullptr, 10);
    if (ms <= 0) {
      out("sleep: the time to sleep is a positive number of ms\r\n");
      return false;
    }
    at = now + ms;
  } else {
    out("usage: sleep [light|deep] <ms> | at <epoch seconds>\r\n");
    return false;
  }
  if (at <= now) {
    out("sleep: that hour is already past\r\n");
    return false;
  }

  out("sleep: ");
  out((mode == hal::SleepMode::kDeep) ? "deep" : "light");
  out(" until ");
  out_int(static_cast<int32_t>(at / 1000));
  newline();
  if (!power::sleep(mode, at)) {
    out("sleep: the board did not sleep\r\n");
    return false;
  }
  // Only a light sleep comes back here: a deep one restarts the board and whoever
  // reports the wake is the boot (docs/90). This band says which one it is.
  out((mode == hal::SleepMode::kDeep) ? "sleep: the board did not restart (this band does not)\r\n"
                                     : "sleep: awake\r\n");
  return true;
}

// Six hex digits, the way a colour is written everywhere: 0 is black, 0xffffff is
// white and the middle is what matters here.
bool parse_colour(const char* text, uint8_t& r, uint8_t& g, uint8_t& b) {
  if (text == nullptr || strlen(text) != 6) return false;
  uint32_t value = 0;
  for (const char* p = text; *p != '\0'; ++p) {
    uint8_t digit = 0;
    if (*p >= '0' && *p <= '9') {
      digit = static_cast<uint8_t>(*p - '0');
    } else if (*p >= 'a' && *p <= 'f') {
      digit = static_cast<uint8_t>(*p - 'a' + 10);
    } else if (*p >= 'A' && *p <= 'F') {
      digit = static_cast<uint8_t>(*p - 'A' + 10);
    } else {
      return false;
    }
    value = (value << 4) | digit;
  }
  r = static_cast<uint8_t>((value >> 16) & 0xFF);
  g = static_cast<uint8_t>((value >> 8) & 0xFF);
  b = static_cast<uint8_t>(value & 0xFF);
  return true;
}

void print_colour(uint8_t r, uint8_t g, uint8_t b) {
  const char kDigits[] = "0123456789abcdef";
  char text[7];
  text[0] = kDigits[(r >> 4) & 0xF];
  text[1] = kDigits[r & 0xF];
  text[2] = kDigits[(g >> 4) & 0xF];
  text[3] = kDigits[g & 0xF];
  text[4] = kDigits[(b >> 4) & 0xF];
  text[5] = kDigits[b & 0xF];
  text[6] = '\0';
  out(text);
}

// led [rrggbb|off]: the light of the board, by hand. It is the colour an app takes,
// so `led off` gives it back to the system and the shell says which one is showing
// (docs/30, docs/50).
bool command_led(const char* argument) {
  char words[1][kWordCap];
  size_t count = 0;
  if (!read_words(argument, words, 1, count)) return false;
  if (!led::available()) {
    out("led: this board has no light\r\n");
    return false;
  }
  if (count == 0) {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    led::current(r, g, b);
    out("led: ");
    out_num(led::count());
    out(" light, showing ");
    print_colour(r, g, b);
    out(led::has_colour() ? " (taken)\r\n" : " (the system's)\r\n");
    return true;
  }
  if (strcmp(words[0], "off") == 0) {
    led::clear_colour();
    out("led: the light is the system's again\r\n");
    return true;
  }
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  if (!parse_colour(words[0], r, g, b)) {
    out("usage: led [rrggbb|off]\r\n");
    return false;
  }
  if (!led::set_colour(r, g, b)) {
    out("led: this board has no light\r\n");
    return false;
  }
  out("led: ");
  print_colour(r, g, b);
  // The panel of the Matrix stops at the maker's number, and a message that only
  // said what was asked would be a lie about what is shining (docs/30, docs/50).
  const uint32_t ceiling = led::max_value();
  if (r > ceiling || g > ceiling || b > ceiling) {
    out(" (this board stops at ");
    out_num(ceiling);
    out(")");
  }
  newline();
  return true;
}

// button: what the finger is doing right now, and the gesture the system keeps for
// itself. It is how a person sees the board feel the press before blaming the app
// (docs/50).
bool command_button(const char* argument) {
  char words[1][kWordCap];
  size_t count = 0;
  if (!read_words(argument, words, 1, count)) return false;
  if (count != 0) {
    out("usage: button\r\n");
    return false;
  }
  if (!button::available()) {
    out("button: this board has no button\r\n");
    return false;
  }
  const bool down = button::down();
  out("button: ");
  out(down ? "down" : "up");
  if (down) {
    out(" (");
    out_int(static_cast<int32_t>(button::held_ms()));
    out(" ms)");
  }
  newline();
  out_num(button::taps_to_close());
  out(" taps in a row close the app\r\n");
  return true;
}

// provision [pop]: the access point, the QR and the Espressif app. The name and
// the password come from the phone (docs/50).
bool command_provision(const char* argument) {
  char words[2][kWordCap];
  size_t count = 0;
  if (!read_words(argument, words, 2, count)) return false;
  // Bluetooth is the transport the maker of the chip recommends, and the phone
  // does not have to leave its own network for it. The access point is still
  // here with a word, because it needs no Bluetooth at all (docs/50).
  hal::ProvTransport transport = hal::ProvTransport::kBle;
  size_t first = 0;
  if (count > 0 && strcmp(words[0], "softap") == 0) {
    transport = hal::ProvTransport::kSoftAp;
    first = 1;
  } else if (count > 0 && strcmp(words[0], "ble") == 0) {
    first = 1;
  }
  const char* pop = (count > first) ? words[first] : nullptr;
  if (!take_radio(RadioJob::kProvision)) return false;
  if (!net::provision(pop, nullptr, transport, kProvisionMs)) {
    drop_radio();
    out("provision: the board did not start it\r\n");
    return false;
  }
  return true;
}

}  // namespace

void prompt() { out(g_lua ? "lua> " : "lunok> "); }

bool app_running() { return g_app_alive; }

bool native_running() { return g_job != RadioJob::kNone; }

const char* app_id() { return g_app_alive ? luabind::app_id() : ""; }

void close_app() {
  if (!g_app_alive) return;
  // The colour the app took goes with it, and it goes **off** and not back to some
  // other colour: a light that outlives its app is power spent by nobody. On a board
  // with no light both calls do nothing (docs/30, docs/75).
  led::clear_colour();
  led::set_state(led::State::kOff);
  // docs/40: an app that is about to be closed gets to say goodbye, in this
  // order. Each one is optional and each one is short: they run here, between
  // turns, so they cannot await.
  luabind::call_hook("on_pause");
  luabind::call_hook("on_save");
  luabind::call_hook("on_close");
  luabind::stop();
  g_app_alive = false;
  out("app closed\r\n");
}

bool restore_app(const char* id, const char* path) { return run_app(id, path, true); }

bool raise_app(const char* id) {
  close_app();
  return run_app(id, nullptr);
}

// A window of an alarm (docs/60): the app is loaded for one call and thrown
// away. It never runs on top of another app, and its console writes are at the
// foreground prompt because there is no separate log console yet.
bool run_window(const char* id, const alarms::Entry& entry, int64_t late_ms) {
  if (g_app_alive) return false;
  RunTarget target;
  size_t len = 0;
  if (!load_source(id, target, len)) return false;

  luabind::Boot boot;
  boot.board = platform().id();
  boot.version = kVersion;
  boot.emulator = platform().emulator();
  boot.window = true;  // the app is not in front, and it can tell (docs/60)
  boot.app_id = id;
  boot.app_title = target.title;
  boot.app_version = target.version;
  boot.app_opens = target.opens;

  const uint32_t limit_kb = platform().heap_limit_kb();
  const size_t limit = (limit_kb > 0) ? (static_cast<size_t>(limit_kb) * 1024) : kDefaultHeapBytes;
  if (!luabind::start(boot, limit)) return false;

  bool ok = luabind::run_source(target.path, g_source, len);
  if (ok) {
    // One turn: the entry runs to its first wait or to its end. A window is not a
    // session, and it does not get the keyboard.
    luabind::poll();
    ok = luabind::call_alarm(entry, late_ms);
  }
  luabind::stop();
  return ok;
}

int32_t app_wait_ms() {
  if (!g_app_alive) return -1;
  return luabind::until_next_ms();
}

bool poll_app() {
  if (!g_app_alive) return false;
  // Three ESC in a row, or a line that says !!exit, are the way out of an app
  // that has stopped listening. The app does not get a say, but it still gets
  // its goodbye hooks (docs/30 and docs/40).
  if (luabind::exit_requested()) {
    close_app();
    prompt();
    return false;
  }
  if (luabind::poll()) return true;
  const int code = luabind::exit_code();
  const bool died = luabind::failed();
  luabind::stop();
  g_app_alive = false;
  // However the app ended, the light goes off: the console is where an error is
  // read, and a red light left on for nobody is the energy this system does not
  // spend (docs/30, docs/75).
  led::clear_colour();
  led::set_state(led::State::kOff);
  if (code != 0 && !died) {
    out("app exit ");
    out_num(static_cast<uint32_t>(code));
    newline();
  }
  prompt();
  return false;
}

void start() {
  g_len = 0;
  g_last_was_cr = false;
  g_app_alive = false;
  newline();
  prompt();
}

// `lua`: the prompt becomes the interpreter. It is the same state an app gets —the
// whole API of docs/30, the same ceiling, the same ground—, one line at a time and
// with the kernel still turning between lines, so the button, the light and the
// alarms keep working while somebody programs (docs/30).
bool command_lua() {
  if (g_lua) return true;
  luabind::Boot boot;
  boot.board = platform().id();
  boot.version = kVersion;
  boot.emulator = platform().emulator();
  // A session is a band of its own: `fs.data()` is `/data/lua`, and an alarm it arms
  // has no app to be run from once the session is closed. That is what a prompt is
  // (docs/40).
  boot.app_id = "lua";
  boot.app_title = "lua";
  boot.app_version = kVersion;
  const uint32_t limit_kb = platform().heap_limit_kb();
  const size_t limit = (limit_kb > 0) ? (static_cast<size_t>(limit_kb) * 1024) : kDefaultHeapBytes;
  if (!luabind::start(boot, limit)) {
    out("cannot start the interpreter\r\n");
    return false;
  }
  g_lua = true;
  out("lua: one line at a time, the whole API (docs/30). `exit` leaves.\r\n");
  return true;
}

// One line of the session. `exit` is the way out and it is checked before the line
// reaches the interpreter, because a bare word would not even compile (docs/30).
bool lua_line(const char* line) {
  if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0 ||
      strcmp(line, ".exit") == 0) {
    // What a session took goes with it, light included: a light left on by a
    // prompt nobody is at is the energy this system does not spend (docs/30,
    // docs/75).
    led::clear_colour();
    led::set_state(led::State::kOff);
    luabind::stop();
    g_lua = false;
    out("lua: session closed\r\n");
    return true;
  }
  return luabind::eval(line, strlen(line));
}

bool execute(char* line) {
  while (*line == ' ') ++line;
  // The shell asked which app to use: this line is the answer, not a command.
  if (g_choosing) return answer_choice(line);
  size_t len = strlen(line);
  while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t')) {
    line[--len] = '\0';
  }
  if (*line == '\0') return true;

  // Inside a session the prompt is the interpreter and nothing else: `apps` is a
  // Lua expression there, and the way out is `exit` (docs/30).
  if (g_lua) return lua_line(line);

  if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0) {
    print_help();
    return true;
  }
  if (strcmp(line, "version") == 0) {
    print_version();
    return true;
  }
  if (strcmp(line, "caps") == 0) {
    print_caps();
    return true;
  }
  if (strcmp(line, "mem") == 0) {
    print_mem();
    return true;
  }
  if (strcmp(line, "apps") == 0) {
    print_apps();
    return true;
  }
  if (strcmp(line, "lua") == 0) {
    return command_lua();
  }
  if (strcmp(line, "mounts") == 0) {
    print_mounts();
    return true;
  }
  if (strncmp(line, "run", 3) == 0 && (line[3] == '\0' || line[3] == ' ')) {
    const char* argument = line + 3;
    while (*argument == ' ') ++argument;
    if (*argument == '\0') {
      out("usage: run <id>\r\n");
      return false;
    }
    return run_app(argument, nullptr);
  }
  if (strncmp(line, "open", 4) == 0 && (line[4] == '\0' || line[4] == ' ')) {
    return open_file(line + 4);
  }
  if (line[0] == 'l' && line[1] == 's' && (line[2] == '\0' || line[2] == ' ')) {
    char words[1][kWordCap];
    size_t count = 0;
    if (!read_words(line + 2, words, 1, count)) return false;
    list_at((count == 0) ? "/" : words[0]);
    return true;
  }
  if (strncmp(line, "cat ", 4) == 0) {
    char words[1][kWordCap];
    size_t count = 0;
    if (!read_words(line + 4, words, 1, count)) return false;
    if (count == 0) {
      out("usage: cat <path>\r\n");
      return false;
    }
    return print_file(words[0]);
  }
  if (strcmp(line, "log") == 0) {
    log_dump();
    return true;
  }
  if (strcmp(line, "clear") == 0) {
    out("\x1b[2J\x1b[H");
    return true;
  }
  if (strcmp(line, "reboot") == 0) {
    out("restarting...\r\n");
    pal::system().reboot();
    return true;
  }
  if (strncmp(line, "verbose", 7) == 0) {
    const char* argument = line + 7;
    while (*argument == ' ') ++argument;
    if (strcmp(argument, "on") == 0) {
      log_echo(true);
      out("log mirroring: on\r\n");
      return true;
    }
    if (strcmp(argument, "off") == 0) {
      log_echo(false);
      out("log mirroring: off\r\n");
      return true;
    }
    out("usage: verbose on|off\r\n");
    return false;
  }

  if (strncmp(line, "wifi", 4) == 0 && (line[4] == '\0' || line[4] == ' ')) {
    return command_wifi(line + 4);
  }
  if (strncmp(line, "provision", 9) == 0 && (line[9] == '\0' || line[9] == ' ')) {
    return command_provision(line + 9);
  }
  if (strncmp(line, "ntp", 3) == 0 && (line[3] == '\0' || line[3] == ' ')) {
    return command_ntp(line + 3);
  }
  if (strncmp(line, "http", 4) == 0 && (line[4] == '\0' || line[4] == ' ')) {
    return command_http(line + 4);
  }
  if (strncmp(line, "power", 5) == 0 && (line[5] == '\0' || line[5] == ' ')) {
    return command_power(line + 5);
  }
  if (strncmp(line, "led", 3) == 0 && (line[3] == '\0' || line[3] == ' ')) {
    return command_led(line + 3);
  }
  if (strncmp(line, "button", 6) == 0 && (line[6] == '\0' || line[6] == ' ')) {
    return command_button(line + 6);
  }
  // `sleep` takes the whole line, because the mode is a word and the hour is a
  // number: there is no third shape.
  if (strncmp(line, "sleep", 5) == 0 && (line[5] == '\0' || line[5] == ' ')) {
    return command_sleep(line + 5);
  }
  out("unknown command: ");
  out(line);
  newline();
  out("try 'help'.\r\n");
  return false;
}

void feed(const char* data, size_t len) {
  // While an app runs the keyboard is the app's: it echoes, it edits, and it is
  // the one that gets the lines (docs/30). The shell sees nothing of this.
  if (g_app_alive) {
    luabind::feed_console(data, len);
    return;
  }

  for (size_t i = 0; i < len; ++i) {
    const char c = data[i];

    // A terminal sends \r\n; the second half is not a new command.
    if (c == '\n' && g_last_was_cr) {
      g_last_was_cr = false;
      continue;
    }
    g_last_was_cr = (c == '\r');

    if (c == '\r' || c == '\n') {
      newline();
      if (g_len > 0) {
        g_line[g_len] = '\0';
        execute(g_line);
        g_len = 0;
      }
      // The question of the selector is its own prompt: printing "lunok> " there
      // left the person answering after a prompt that belongs to the shell.
      if (!g_app_alive && !g_choosing && !native_running()) prompt();
    } else if (c == 0x7f || c == 0x08) {
      if (g_len > 0) {
        --g_len;
        out("\b \b");
      }
    } else if (c == 0x1b) {
      // Three ESC in a row is the way out of whatever is running, and a command
      // that waits for the radio runs too: a provisioning waiting for a phone had
      // no exit except a word you had to know (`wifi stop`) (docs/30).
      if (native_running() && ++g_esc >= kEscToStop) {
        g_esc = 0;
        if (stop_radio_now()) {
          newline();
          out("radio: stopped\r\n");
          prompt();
        }
      }
      continue;
    } else if (c >= 0x20 && c < 0x7f) {
      g_esc = 0;
      if (g_len + 1 < kLineBytes) {
        g_line[g_len++] = c;
        const char echo[2] = {c, '\0'};
        out(echo);
      }
    }
  }
}

}  // namespace shell
}  // namespace kernel
