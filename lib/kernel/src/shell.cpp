// The serial shell: line editing, the command table, and the app console once
// there is an app to run. It is the only user interface there is, because the
// board has no screen (docs/10 and docs/50).
#include "kernel/shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hal/hal.h"
#include "kernel/apps.h"
#include "kernel/button.h"
#include "kernel/cfg.h"
#include "kernel/colour.h"
#include "kernel/kernel.h"
#include "kernel/led.h"
#include "kernel/line_editor.h"
#include "kernel/log.h"
#include "kernel/meter.h"
#include "kernel/net.h"
#include "kernel/bar.h"
#include "kernel/power.h"
#include "kernel/ui.h"
#include "kernel/version.h"
#include "kernel/vfs.h"
#include "luabind/luabind.h"
#include "pal/pal.h"

namespace kernel {
namespace shell {

bool ansi_reply(const char* data, size_t len) {
  if (data == nullptr || len < 4) return false;
  for (size_t i = 0; i + 2 < len; ++i) {
    if (data[i] != 0x1b || data[i + 1] != '[') continue;
    // A reply is `ESC [ ?` and then digits and semicolons until a `c`. An arrow (`ESC [ A`) and a cursor
    // report (`ESC [ 12 ; 40 R`) are not answers, and neither is anything a person typed.
    size_t at = i + 2;
    if (at >= len || data[at] != '?') continue;
    ++at;
    while (at < len && ((data[at] >= '0' && data[at] <= '9') || data[at] == ';')) ++at;
    if (at < len && data[at] == 'c') return true;
  }
  return false;
}

namespace {

const size_t kPrintCap = 1024;
// Largest entry chunk that will be run. It was 4 096 B and the `matrix` app grew past it; it was 8 192 B and
// the `buttons` example grew past it at 9 329 B, which is a limit saying no to an app that fits the board. It
// was 16 384 B and the editor that speaks vim grew past it at 26 644 B (4/11), and the buffer is not static
// RAM since 0.56.0: it is asked of the heap and given back the moment the chunk is compiled, so **32 768 B
// costs 16 KB of heap while an app starts and nothing while it runs** (docs/75). It does not grow again
// without a number and a paragraph in docs/75.
const size_t kSourceCap = 32768;
const size_t kDefaultHeapBytes = 64 * 1024;   // used until the board ceiling is measured
// A word of a command line is 127 bytes at most, and a path with spaces goes between quotes:
// `cat "res/el meu fitxer.txt"`. There is no escaping, because a quote is a quote and nothing in a file
// system of this size needs more.
const size_t kWordCap = 128;

line::Editor g_editor;
// The prompt is kept because redrawing the line after a cursor move has to put it back: the prompt of
// the `lua` session and the question of the app selector are not the same line (docs/30, docs/85).
char g_prompt[40] = "lunok> ";
size_t g_prompt_len = 7;
size_t g_visible = 0;  // characters of the prompt and the line that are on the terminal
bool g_last_was_cr = false;
// The app that is alive between turns: while it is, the prompt is not shown and
// the keyboard is not the shell's.
bool g_app_alive = false;
// True while the prompt is an interpreter (the `lua` command): the lines are not
// commands, they are Lua, and the prompt says so (docs/30).
bool g_lua = false;
// ESC pressed in a row: three of them stop a command that is waiting, the same
// way they leave an app (docs/30). An arrow or a function key is `ESC [ …` and is
// one key press, so the parser below keeps its state between feeds.
const uint8_t kEscToStop = 3;
uint8_t g_esc = 0;
enum class Escape : uint8_t { kNone, kEsc, kCsi, kCsiNum, kSs3 };
Escape g_escape = Escape::kNone;
uint32_t g_csi_number = 0;

void out_bytes(const void* data, size_t len) {
  pal::IConsole* console = pal::system().console();
  if (console == nullptr) return;
  console->write(data, len);
}

void out(const char* text) { out_bytes(text, strlen(text)); }

void newline() {
  out("\r\n");
  g_visible = 0;
}

void spaces(size_t count) {
  while (count > 0) {
    out(" ");
    --count;
  }
}

// **Whether the terminal speaks ANSI is asked, not assumed.** The Device Attributes query is one sequence
// (`ESC [ c`), a terminal that understands it answers `ESC [ ? ... c`, and a terminal that does not stays
// quiet: that is a detection and not the guess `TERM` and `isatty` make (docs/85).
//
// The answer is cached, because asking is a wait and the redraw of a line asks on every key. **The platform's
// own belief is the gate**: a console that already says it is not a terminal is not asked, which is what keeps
// a pipe, a file and the native suite from paying 150 ms and from reading escape bytes that nobody sent.
const uint32_t kAnsiProbeMs = 150;
bool g_ansi = false;
bool g_ansi_known = false;
// What arrived while the question was in the air. It is fed to the line afterwards if it was not an answer,
// so the first thing a person types at boot is not eaten by the boot.
char g_early[32];
size_t g_early_len = 0;

// **The question and the wait**, asked once by `start` and remembered: it is the only place that reads the
// port before there is a prompt.
void detect_terminal() {
  g_early_len = 0;
  g_ansi_known = true;
  g_ansi = false;
  pal::IConsole* console = pal::system().console();
  // **Not a terminal, not asked**: a pipe, a file, or the native suite. Nobody is going to answer, and paying
  // 150 ms to hear nothing is not a detection (docs/85).
  const bool asks = (console != nullptr) && console->ansi();
  if (asks) {
    const char ask[] = "\x1b[c";
    console->write(ask, sizeof(ask) - 1);
    console->flush();
    const int64_t deadline = pal::system().millis() + static_cast<int64_t>(kAnsiProbeMs);
    while (pal::system().millis() < deadline) {
      char byte = 0;
      if (console->read(&byte, 1) == 0) continue;
      if (g_early_len < sizeof(g_early)) g_early[g_early_len++] = byte;
      // The reply ends at its `c`, and the collection stops with it.
      if (byte == 'c' && ansi_reply(g_early, g_early_len)) {
        g_ansi = true;
        g_early_len = 0;
        break;
      }
    }
  }
  // **What was detected is said in one line**, because a terminal that is not what the platform believed is
  // exactly the thing a person has to be able to see without guessing (docs/85).
  log_printf(pal::kLogInfo, "TERM", "%s",
             !asks ? "no terminal, ansi not asked"
                   : (g_ansi ? "ansi" : "no ansi, spaces and backspaces"));
  // The ring echoes and dumps with the same answer: a log line does not know where it is going (docs/30).
  log_colour(g_ansi);
}

bool ansi_now() {
  // **What the terminal said of itself at start is the answer**, and the platform's belief is only the gate
  // that decides whether asking is worth 150 ms (docs/85).
  if (g_ansi_known) return g_ansi;
  pal::IConsole* console = pal::system().console();
  return console != nullptr && console->ansi();
}

// **The colour is a courtesy and not a contract**: a terminal that answered the question gets the sequences,
// one that did not gets the same words and not one byte it cannot read. Every writer of the shell goes
// through here, so the two are the same picture (docs/30, docs/85).
void colour(const char* code) {
  if (code != nullptr && ansi_now()) out(code);
}

void colour_off() { colour(kColourOff); }

void error_out(const char* text) {
  colour(kColourError);
  out(text);
  colour_off();
}

void good_out(const char* text) {
  colour(kColourGood);
  out(text);
  colour_off();
}

void usage_out(const char* text) {
  colour(kColourWarn);
  out(text);
  colour_off();
}

// **The prompt is the one thing that is always coloured**, because it is the line a person reads before
// every command: the name in green and the `>` that follows it in grey. The bytes of `g_prompt` stay the
// visible text —a redraw counts characters and not codes— and the question of the selector, which does not
// end in `> `, is written as it is (docs/30).
void write_prompt() {
  const bool is_prompt = g_prompt_len >= 2 && g_prompt[g_prompt_len - 2] == '>' &&
                         g_prompt[g_prompt_len - 1] == ' ';
  if (!ansi_now() || !is_prompt) {
    out_bytes(g_prompt, g_prompt_len);
    return;
  }
  size_t name = 0;
  while (name < g_prompt_len && g_prompt[name] != '>') ++name;
  colour(kColourName);
  out_bytes(g_prompt, name);
  colour_off();
  colour(kColourDim);
  out_bytes(g_prompt + name, g_prompt_len - name);
  colour_off();
}

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
  error_out("bad line: a quote is open, a word is over 127 B, or there are too many words\r\n");
  error_out("a path with spaces goes in quotes\r\n");
  return false;
}

void out_num(uint32_t value);

// A count of bytes as megabytes with one decimal (11.8), because a whole number hides the
// difference between an empty partition and one holding a few apps (docs/70).
void print_megabytes(uint64_t bytes) {
  const uint64_t tenth = bytes / (1024ULL * 1024ULL / 10ULL);
  out_num(static_cast<uint32_t>(tenth / 10));
  out(".");
  out_num(static_cast<uint32_t>(tenth % 10));
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

// The prompt that a redraw has to put back. The selector asks its own question and the interpreter says
// `lua>`, so the text is a value and not a call to `prompt()` (docs/30).
void set_prompt_text(const char* text, bool write) {
  size_t len = strlen(text);
  if (len >= sizeof(g_prompt)) len = sizeof(g_prompt) - 1;
  memcpy(g_prompt, text, len);
  g_prompt[len] = '\0';
  g_prompt_len = len;
  if (write) write_prompt();
  g_visible = len;
}

// **The line is redrawn from the prompt and the cursor is put back where it belongs**. ANSI terminals get
// the erase and the left move; a terminal without ANSI gets spaces to wash the old tail and backspaces to
// reach the cursor, which is the same picture with the codes the shell already had (docs/30, docs/85).
// The line is short, so a redraw per key is cheaper than keeping a second copy of the screen.
void redraw_line() {
  const bool ansi = ansi_now();
  const size_t len = g_editor.length();
  const size_t cursor = g_editor.cursor();
  const size_t old_line = (g_visible > g_prompt_len) ? g_visible - g_prompt_len : 0;
  out("\r");
  write_prompt();
  out_bytes(g_editor.text(), len);
  if (ansi) {
    out("\x1b[K");
    if (cursor < len) {
      out("\x1b[");
      out_num(static_cast<uint32_t>(len - cursor));
      out("D");
    }
  } else {
    const size_t pad = (old_line > len) ? old_line - len : 0;
    spaces(pad);
    const size_t back = len + pad - cursor;
    for (size_t i = 0; i < back; ++i) out("\b");
  }
  g_visible = g_prompt_len + len;
}

// **The command is the first thing after the two spaces and it is what goes in bold**: the description is
// the rest of the line, and it stays as it was. A line without a description —the two pieces of advice at
// the end— is written plainly, because it has no column to be a command (docs/30).
void help_line(const char* text) {
  const char* start = text;
  while (*start == ' ') ++start;
  const char* end = strstr(start, "  ");
  if (end == nullptr) {
    out(start);
    out("\r\n");
    return;
  }
  out("  ");
  colour(kColourBold);
  out_bytes(start, static_cast<size_t>(end - start));
  colour_off();
  out(end);
  out("\r\n");
}

void print_help() {
  help_line("  help              this list");
  help_line("  version           version and board");
  help_line("  caps              what this board has");
  help_line("  mem               free memory and buffers");
  help_line("  apps              the apps in /apps");
  help_line("  mounts            the mounted file systems and their room");
  help_line("  cfg [auto|sd|littlefs]  where the apps' settings are kept");
  help_line("  run <id> [path]   run an app, optionally pointed at a file");
  help_line("  wifi              the stored network and the radio");
  help_line("  wifi scan         the networks the radio sees");
  help_line("  wifi add <ssid> [password]  store a network and go on it");
  help_line("  wifi connect      go on the network the board has stored");
  help_line("  wifi forget       forget the stored network");
  help_line("  wifi stop         drop whatever the radio is doing");
  help_line("  provision [pop]   the QR and the Espressif app, over Bluetooth");
  help_line("  provision softap [pop]  the same over the board's own access point");
  help_line("  ntp [server]      set the clock from a time server");
  help_line("  http <url> [path] fetch a page, and save it when it is too big");
  help_line("  power [cpu [mhz]]  what this board can do about energy");
  help_line("  power last        how far the last sleep was from the hour it was given");
  help_line("  power idle [on|off]  whether the board may sleep on its own to the next alarm");
  help_line("  led [rrggbb|off]  the light of the board");
  help_line("  button            the button, right now");
  help_line("  sleep [light|deep] <ms>  sleep, and come back at a time");
  help_line("  sleep at <epoch seconds>  the same, at a time of the clock");
  help_line("  open [app] <path>  open a file with the app that handles it");
  help_line("  ls [path]         list a directory (default /)");
  help_line("  cat <path>        print a file");
  help_line("  hexdump [path]    the bytes of a file in hex (default /rtc)");
  help_line("  lua               the prompt becomes the interpreter (`exit` or Ctrl+D leaves)");
  help_line("  log               last lines of the log ring");
  help_line("  stats [reset]     what each device cost: calls, time, last call");
  help_line("  verbose on|off    mirror the log to the console too");
  help_line("  ansi [on|off]     the colours of the terminal, or why there are none");
  help_line("  clear             clear the screen");
  help_line("  reboot            restart");
  help_line("  poweroff          cut the power of the board, if it can (docs/50)");
  help_line("three ESC in a row leave an app, or stop a command that waits");
  help_line("a path with spaces goes in quotes: cat \"res/el meu fitxer.txt\"");
}

void print_version() {
  out("lunokOS ");
  colour(kColourBold);
  out(kVersion);
  colour_off();
  out("  board ");
  out(platform().id());
  // The board and the bench are both true at once on the PC (docs/85).
  if (platform().emulator()) {
    colour(kColourDim);
    out(" (emulator)");
    colour_off();
  }
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
    // **What a page could still take**: the free bytes can be in pieces, and a block that does not fit in
    // any of them fails on a heap with room to spare. The board has the number and so does the bench
    // (docs/75, docs/85).
    out("  largest block      ");
    out_num(heap.largest_free_bytes);
    newline();
  } else {
    out("  heap               not available on this build\r\n");
  }
  // **What is left of the stack of this task**: a page of controls, a font of outlines and the interpreter
  // share it, and an overflow does not say so —it corrupts a backtrace and reboots— so the number is here
  // (docs/75).
  const int32_t stack_left = pal::system().stack_free();
  if (stack_left >= 0) {
    out("  stack left         ");
    out_num(static_cast<uint32_t>(stack_left));
    out(" B\r\n");
  }
  // The extra memory of the board (the PSRAM of the Paper), with the three numbers that are not the
  // same: what the chip says it has, what the CPU can address, and what is left. It was one number
  // and it read as a bug —`4067 of 8192 KB`— because a pool of 4 MB was counted against an 8 MB chip
  // (docs/75). On a board with none, the line says so and that is not an error.
  const hal::ExtraMemory extra = platform().extra_memory();
  if (extra.chip_bytes > 0) {
    out("  extra memory       ");
    out_num(extra.free_bytes / 1024);
    out(" of ");
    out_num(extra.total_bytes / 1024);
    out(" KB usable, of ");
    out_num(extra.chip_bytes / 1024);
    out(" KB on the chip (psram)\r\n");
  } else {
    out("  extra memory       none on this board\r\n");
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
  colour(kColourBold);
  out(entry.id);
  colour_off();
  spaces((id_len < 16) ? (16 - id_len) : 1);
  colour(kColourDim);
  out(entry.version);
  colour_off();
  spaces((version_len < 10) ? (10 - version_len) : 1);
  out(entry.title);
  // An app of another board is not hidden: it says so, with the boards it wants, so
  // that a person does not have to guess where it went (docs/40).
  if (!entry.works_here) {
    colour(kColourWarn);
    out("  [needs ");
    for (uint32_t i = 0; i < entry.board_count; ++i) {
      out((i == 0) ? "" : " or ");
      out(entry.boards[i]);
    }
    out("]");
    colour_off();
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
      // Bytes below a megabyte and megabytes above it: a 16 GB card in bytes is a number nobody counts,
      // and `out_num` takes 32 bits, so the card overflowed it and printed zero (measured on the bench,
      // 31/10). The flash of a board fits either way (917 504 B).
      const uint64_t megabyte = 1024ULL * 1024ULL;
      if (total >= megabyte) {
        // One decimal, because whole megabytes hide the difference between a partition that is
        // empty and one that has a few apps in it (the Paper's is 11.87 MB, docs/70).
        out("free ");
        print_megabytes(free_bytes);
        out(" MB of ");
        print_megabytes(total);
        out(" MB");
      } else {
        out("free ");
        out_num(static_cast<uint32_t>(free_bytes));
        out(" of ");
        out_num(static_cast<uint32_t>(total));
        out(" B");
      }
    } else {
      // Not "no room": the values of the NVS are not files and there is no byte count to
      // give. What there is, is a number of entries, and that is the number a person needs
      // before the next thing that has to be saved does not fit (docs/70).
      uint32_t used_entries = 0;
      uint32_t total_entries = 0;
      hal::IKeyValue* kv = platform().nvs();
      if (kv != nullptr && kv->stats(used_entries, total_entries)) {
        out("entries ");
        out_num(used_entries);
        if (total_entries > 0) {
          out(" of ");
          out_num(total_entries);
        }
      } else {
        out("space unknown");
      }
    }
    newline();
  }
}

class ListPrinter : public hal::ListSink {
 public:
  bool on_entry(const hal::ListEntry& entry) override {
    // **A name is the answer and the rest is the frame round it**: the kind and the size go dim, a directory
    // is blue, and a file is the paper. It is the oldest use of colour there is and it still reads at a
    // glance (docs/30, docs/85).
    if (entry.type == hal::NodeType::kDir) {
      out("  ");
      colour(kColourDim);
      out("<dir>   ");
      colour_off();
      colour(kColourDir);
      out(entry.name);
      colour_off();
    } else {
      out("  ");
      colour(kColourDim);
      out("file    ");
      colour_off();
      out(entry.name);
      out("  ");
      colour(kColourDim);
      out_num(entry.size);
      colour_off();
    }
    newline();
    return true;
  }
};

void list_at(const char* path) {
  ListPrinter printer;
  if (!vfs::list(path, printer)) {
    error_out("cannot list: ");
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
    error_out("no such file: ");
    out(path);
    newline();
    return false;
  }
  if (info.type != hal::NodeType::kFile) {
    error_out("that is not a file: ");
    out(path);
    newline();
    return false;
  }
  char buf[kPrintCap];
  size_t len = 0;
  if (!vfs::read_file(path, buf, sizeof(buf), len)) {
    error_out("cannot read: ");
    out(path);
    newline();
    return false;
  }
  if (len > 0) out_bytes(buf, len);
  if (len > 0 && buf[len - 1] != '\n') newline();
  return true;
}

// Hexdump: a file as eight digits of offset, sixteen bytes in hex, and the same sixteen between bars.
// It is a command of the shell and not an app (docs/30, docs/70): it is how a person looks at the RTC
// memory or the FRAM when there is no app for it, and it must work before the interpreter does.
const size_t kHexChunk = 256;

void hexdump_row(uint32_t offset, const uint8_t* data, size_t len) {
  static const char kHex[] = "0123456789abcdef";
  char line[96];
  size_t at = 0;
  for (int shift = 28; shift >= 0; shift -= 4) {
    line[at++] = kHex[(offset >> shift) & 0x0F];
  }
  line[at++] = ' ';
  line[at++] = ' ';
  for (size_t i = 0; i < 16; ++i) {
    if (i > 0) line[at++] = ' ';
    if (i < len) {
      line[at++] = kHex[data[i] >> 4];
      line[at++] = kHex[data[i] & 0x0F];
    } else {
      // A short last row stays aligned with the ones above it.
      line[at++] = ' ';
      line[at++] = ' ';
    }
  }
  line[at++] = ' ';
  line[at++] = ' ';
  line[at++] = '|';
  for (size_t i = 0; i < 16; ++i) {
    if (i >= len) {
      line[at++] = ' ';
    } else {
      line[at++] = (data[i] >= 32 && data[i] <= 126) ? static_cast<char>(data[i]) : '.';
    }
  }
  line[at++] = '|';
  out_bytes(line, at);
  newline();
}

// The last byte that is not zero and whether there is one at all. The empty tail is not printed: a
// memory is mostly zeros and the zeros teach nothing (docs/70).
bool hexdump_scan(hal::IStorage* storage, const char* rel, uint32_t size, uint32_t& last, bool& any) {
  uint8_t buf[kHexChunk];
  hal::FileSlot slot;
  meter::Scope scope(meter::storage_id(meter::kOpRead, vfs::unit_of(storage)));
  if (!storage->open(rel, hal::OpenMode::kRead, slot)) return false;

  uint32_t at = 0;
  while (at < size) {
    const uint32_t left = size - at;
    const uint32_t want = (left < kHexChunk) ? left : static_cast<uint32_t>(kHexChunk);
    const int32_t got = storage->read(slot, buf, want);
    if (got <= 0) break;
    for (int32_t i = 0; i < got; ++i) {
      if (buf[i] != 0) {
        last = at + static_cast<uint32_t>(i);
        any = true;
      }
    }
    at += static_cast<uint32_t>(got);
  }
  storage->close(slot);
  return true;
}

// The rows are printed while the file is open, so the console time must not land in the read meter:
// a `Scope` would include every `hexdump_row` between the open and the close (docs/30).
class StorageReadClock {
 public:
  explicit StorageReadClock(hal::IStorage* storage)
      : id_(meter::storage_id(meter::kOpRead, vfs::unit_of(storage))) {}
  ~StorageReadClock() { meter::add(id_, us_); }
  void begin() { start_ = meter::now_us(); }
  void end() { us_ += meter::now_us() - start_; }

 private:
  uint8_t id_;
  uint32_t start_ = 0;
  uint32_t us_ = 0;
};

bool hexdump_rows(hal::IStorage* storage, const char* rel, uint32_t last) {
  uint8_t row[16];
  hal::FileSlot slot;
  StorageReadClock clock(storage);
  clock.begin();
  const bool opened = storage->open(rel, hal::OpenMode::kRead, slot);
  clock.end();
  if (!opened) return false;
  clock.begin();
  const bool sought = storage->seek(slot, 0);
  clock.end();
  if (!sought) {
    clock.begin();
    storage->close(slot);
    clock.end();
    return false;
  }

  uint32_t at = 0;
  while (at <= last) {
    const uint32_t left = last - at;
    size_t got = 0;
    while (got < sizeof(row)) {
      clock.begin();
      const int32_t n = storage->read(slot, row + got, static_cast<uint32_t>(sizeof(row) - got));
      clock.end();
      if (n <= 0) break;
      got += static_cast<size_t>(n);
    }
    if (got == 0) break;
    hexdump_row(at, row, got);
    if (got > left) break;
    at += static_cast<uint32_t>(got);
  }
  clock.begin();
  storage->close(slot);
  clock.end();
  return true;
}

bool hexdump_file(const char* path) {
  hal::Stat info;
  if (!vfs::stat(path, info)) {
    error_out("hexdump: no such file: ");
    out(path);
    newline();
    return false;
  }
  if (info.type != hal::NodeType::kFile) {
    error_out("hexdump: that is not a file: ");
    out(path);
    newline();
    return false;
  }

  hal::IStorage* storage = nullptr;
  char rel[128];
  if (!vfs::resolve(path, storage, rel, sizeof(rel))) {
    error_out("hexdump: cannot read: ");
    out(path);
    newline();
    return false;
  }

  uint32_t last = 0;
  bool any = false;
  if (!hexdump_scan(storage, rel, info.size, last, any)) {
    error_out("hexdump: cannot read: ");
    out(path);
    newline();
    return false;
  }
  out("hexdump: ");
  out(path);
  out(" is ");
  out_num(info.size);
  if (!any) {
    out(" B and every one of them is zero\r\n");
    return true;
  }
  out(" B, ");
  out_num(last + 1);
  out(" in use and ");
  out_num(info.size - last - 1);
  out(" zero after\r\n");
  if (!hexdump_rows(storage, rel, last)) {
    error_out("hexdump: cannot go back to the start\r\n");
    return false;
  }
  return true;
}

// The entry chunk is handed to the interpreter whole, because Lua compiles a script from its source and cannot
// be fed a line at a time. **It is taken from the heap and given back as soon as it has been compiled**, and
// not kept as a static block: sixteen kilobytes reserved for the millisecond of a `run_source` are sixteen
// kilobytes the plain ESP32 does not have to give away for ever —with a static buffer the ATOM Matrix came out
// **1 184 B over `dram0`** (measured, 2/11, docs/75). The ceiling is still checked before reading (docs/20).
char* g_source = nullptr;

void drop_source() {
  if (g_source != nullptr) {
    free(g_source);
    g_source = nullptr;
  }
}

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
    error_out("no such app: ");
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
    if (platform().emulator()) out(" (the bench is told with --board)");
    newline();
    return false;
  }
  g_source = static_cast<char*>(malloc(kSourceCap));
  if (g_source == nullptr) {
    out("no memory for the source\r\n");
    return false;
  }
  if (!vfs::read_file(target.path, g_source, kSourceCap - 1, len)) {
    drop_source();
    error_out("cannot read: ");
    out(target.path);
    newline();
    return false;
  }
  if (len == kSourceCap - 1) {
    drop_source();
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
  // **A breadcrumb before the interpreter takes over the task**: a page of controls, a font of outlines and
  // Lua share one stack, and when that stack dies the only thing left is what was said before it did
  // (measured on the Paper with `run buttons`, 2/11, docs/75).
  const int32_t stack_left = pal::system().stack_free();
  if (stack_left >= 0) {
    log_printf(pal::kLogInfo, "APPS", "%s, stack %d B free", boot.app_id, static_cast<int>(stack_left));
  } else {
    log_printf(pal::kLogInfo, "APPS", "%s", boot.app_id);
  }
  if (!luabind::start(boot, limit)) {
    drop_source();  // the text was read and nobody is going to compile it
    error_out("cannot start the interpreter\r\n");
    return false;
  }
  // The app is measured on its own from here (docs/30).
  meter::note_app_start(boot.app_id);
  // The app gets a view of its own, white and with nothing pending; the glass is not touched until it
  // presents (docs/30).
  ui::attach();
  // And its title goes to the middle of the bar, which is the system's and not the app's (docs/30).
  bar::title(target.title);

  // The app is compiled and given a task, but not run here: the first turn is
  // the first poll, and from then on the kernel keeps turning (docs/30).
  const bool compiled = luabind::run_source(target.path, g_source, len);
  // **The text is the interpreter's from here**, which has compiled it into its own state: the block goes back
  // before the app runs a single line (docs/20, docs/75).
  drop_source();
  if (!compiled) {
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
  char question[40];
  snprintf(question, sizeof(question), "which one? (1-%u) ", static_cast<unsigned>(g_choice_count));
  out(question);
  // **The question is the prompt for the line editor**: an arrow or a correction on this line has to
  // put the question back, not the shell's `lunok> ` (docs/30).
  set_prompt_text(question, false);
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
    usage_out("usage: open [app] <path>\r\n");
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
    error_out("no such file: ");
    out(path);
    newline();
    return false;
  }
  if (info.type != hal::NodeType::kFile) {
    error_out("that is not a file: ");
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
    colour(kColourDim);
    out_num(i + 1);
    colour_off();
    out(") ");
    // A network that does not say its name is listed as such: an entry that is
    // skipped is indistinguishable from a scan that returned nothing (docs/50).
    colour(kColourBold);
    out((ssid[0] != '\0') ? ssid : "(hidden network)");
    colour_off();
    colour(kColourDim);
    out("  (");
    out_int(rssi);
    out(" dBm)");
    colour_off();
    newline();
  }
}

void say_failed(const char* what, const char* code) {
  error_out(what);
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
      good_out("wifi: on the network ");
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
      good_out("time: ");
      out_num(static_cast<uint32_t>(epoch / 1000));
      out(" (epoch seconds, from ");
      out((clock != nullptr) ? clock->source() : "none");
      out(")\r\n");
    } else if (code != nullptr && strcmp(code, "E_NONET") == 0) {
      // It could not even get on a network, and that is worth its own sentence:
      // "no time came back" would send a person looking at the time server.
      error_out("ntp: the board has no network to use, try `wifi add`\r\n");
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
    // **A page that arrived is an answer and not a failure**: a 2xx in green, anything else in red,
    // and the bytes after it as they were (docs/30). A page longer than what the shell carries
    // arrived too —the board keeps the first bytes and answers `E_TOOBIG`—, so the status and the
    // beginning are shown and the rest is said as a number (docs/30, docs/85).
    const bool cut = !ok && code != nullptr && strcmp(code, "E_TOOBIG") == 0;
    if (ok || cut) {
      uint32_t len = 0;
      const char* body = net::http_body(len);
      const int32_t status = net::http_status();
      if (status >= 200 && status < 300) {
        good_out("http: ");
      } else {
        error_out("http: ");
      }
      out_int(status);
      out(" ");
      out_num(len);
      out(" bytes");
      const bool saved = net::http_saved();
      if (saved) {
        // **Where the whole page is**: a path a person can `cat`, `app` or copy to the card, which is
        // the point of downloading a page that does not fit (docs/30, docs/70).
        out(", saved to ");
        out(net::http_saved_path());
      }
      if (cut) {
        error_out(", and the page is longer than the ");
        out_num(kHttpBytes);
        error_out(" bytes this shell carries");
      }
      out("\r\n");
      // The body is bytes and not text, so it goes out with its length and only
      // the beginning of it: the rest is a number and not a screenful nobody
      // asked for (docs/30). A page that went to a file is read back from there,
      // because that file is the page (docs/30, docs/70).
      uint32_t shown = (len < kHttpShow) ? len : kHttpShow;
      if (saved) {
        size_t got = 0;
        char head[kHttpShow];
        if (vfs::read_file(net::http_saved_path(), head, sizeof(head), got) && got > 0) {
          out_bytes(head, static_cast<uint32_t>(got));
          shown = static_cast<uint32_t>(got);
        } else {
          shown = 0;
        }
      } else if (body != nullptr && shown > 0) {
        out_bytes(body, shown);
      }
      if (shown < len) {
        out("... ");
        out_num(len - shown);
        out(" bytes more");
      }
      newline();
    } else if (code != nullptr && strcmp(code, "E_NONET") == 0) {
      error_out("http: the board has no network to use, try `wifi add`\r\n");
    } else if (code != nullptr && strcmp(code, "E_TLS") == 0) {
      error_out("http: the certificate was not accepted\r\n");
    } else if (code != nullptr && strcmp(code, "E_IO") == 0) {
      // The page had a file to go to and the volume did not take it: no room, or a write that did not
      // go in. It is said as what happened and not as a code (docs/30, docs/70).
      error_out("http: the page did not fit where it was going\r\n");
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
      error_out("wifi: no radio in this build: the HAL of this board does not provide one\r\n");
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
      usage_out("usage: wifi scan\r\n");
      return false;
    }
    if (!take_radio(RadioJob::kScan)) return false;
    if (!net::scan_start(kScanMs)) {
      drop_radio();
      error_out("wifi: the radio did not start the scan\r\n");
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
      usage_out("usage: wifi connect\r\n");
      return false;
    }
    char ssid[33];
    if (!net::stored_ssid(ssid, sizeof(ssid))) {
      error_out("wifi: the board has no network to connect to\r\n");
      return false;
    }
    if (!take_radio(RadioJob::kConnect)) return false;
    if (!net::connect_stored(kAddMs)) {
      drop_radio();
      error_out("wifi: the radio did not start the connection\r\n");
      return false;
    }
    return true;
  }

  if (strcmp(words[0], "add") == 0) {
    if (count < 2 || count > 3) {
      usage_out("usage: wifi add <ssid> [password]\r\n");
      return false;
    }
    if (!take_radio(RadioJob::kAdd)) return false;
    copy_text(g_job_ssid, sizeof(g_job_ssid), words[1]);
    // The password goes on the command line, so it is in the terminal and in the
    // shell's history: there is no screen to type it on, and that is the price
    // (docs/50).
    if (!net::add_network(g_job_ssid, (count == 3) ? words[2] : "", kAddMs)) {
      drop_radio();
      error_out("wifi: the board did not take the network\r\n");
      return false;
    }
    return true;
  }

  if (strcmp(words[0], "forget") == 0 || strcmp(words[0], "del") == 0) {
    if (count != 1) {
      usage_out("usage: wifi forget\r\n");
      return false;
    }
    // Forgetting is the person's decision over the radio, so whatever was
    // running is dropped first: a command that cannot answer leaves the board
    // with no way out (measured on the board, 31/10).
    if (stop_radio_now()) out("radio: stopped\r\n");
    if (!net::forget()) {
      error_out("wifi: the board did not let go of the network\r\n");
      return false;
    }
    out("network: none\r\n");
    return true;
  }

  // The way out of a job that is going nowhere: the access point goes down and
  // the prompt comes back (docs/30).
  if (strcmp(words[0], "stop") == 0) {
    if (count != 1) {
      usage_out("usage: wifi stop\r\n");
      return false;
    }
    if (!stop_radio_now()) {
      out("radio: nothing to stop\r\n");
      return true;
    }
    out("radio: stopped\r\n");
    return true;
  }

  usage_out("usage: wifi connect | wifi scan | wifi add <ssid> [password] | wifi forget | wifi stop\r\n");
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
    error_out("ntp: the radio did not take it\r\n");
    return false;
  }
  return true;
}

// The name a page gets on a volume: the last part of the address, without the query or the anchor, and
// only the characters a file name can have — `index.html?x=1` is `index.html` and not a path of its own
// (docs/70).
void page_name(const char* url, char* out, size_t cap) {
  const char* begin = url;
  const char* end = url;
  for (const char* p = url; *p != '\0'; ++p) {
    if (*p == '?' || *p == '#') break;
    if (*p == '/') begin = p + 1;
    end = p + 1;
  }
  size_t used = 0;
  for (const char* p = begin; p < end && used + 1 < cap; ++p) {
    const char c = *p;
    const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
    out[used++] = plain ? c : '_';
  }
  if (used == 0) {
    const char kDefault[] = "page.html";
    const size_t take = (sizeof(kDefault) - 1 < cap - 1) ? sizeof(kDefault) - 1 : cap - 1;
    memcpy(out, kDefault, take);
    used = take;
  }
  out[used] = '\0';
}

// **Where a page that does not fit goes**: the card when the board has one that can be written, the
// flash when it has not, and a directory of pages that is made here so the path a person reads is a
// path that exists (docs/30, docs/70). False when neither volume takes it, and then the page is asked
// for in memory only, which is what the shell did before there was a place to put it.
bool page_place(const char* url, char* out, size_t cap) {
  const char* dir = "/littlefs/http";
  hal::Stat probe;
  if (vfs::stat("/sd", probe) && vfs::is_writable("/sd")) dir = "/sd/http";
  hal::IStorage* storage = nullptr;
  char rel[96];
  if (!vfs::resolve(dir, storage, rel, sizeof(rel)) || storage == nullptr) return false;
  if (!vfs::is_writable(dir)) return false;
  hal::Stat there;
  if (!storage->stat(rel, there)) storage->mkdir(rel);
  char name[32];
  page_name(url, name, sizeof(name));
  const int written = snprintf(out, cap, "%s/%s", dir, name);
  return written > 0 && static_cast<size_t>(written) < cap;
}

// http <url> [path]: a page, downloaded and shown. It is a task of the queue like the time: it brings
// the network up by itself and lets go of it afterwards, so a person does not have to connect first.
// **With one word the page is held in memory and, when it does not fit, it goes to a file whole** —a
// page is asked for to be read, and half a page is not one—; with two, memory holds nothing and the
// page goes straight to the path (docs/30, docs/70, docs/75).
bool command_http(const char* argument) {
  char words[2][kWordCap];
  size_t count = 0;
  if (!read_words(argument, words, 2, count)) return false;
  if (count == 0) {
    usage_out("usage: http <url> [path]\r\n");
    return false;
  }
  char spill[128];
  spill[0] = '\0';
  const char* place = nullptr;
  if (count == 2) {
    place = words[1];
  } else if (page_place(words[0], spill, sizeof(spill))) {
    place = spill;
  }
  if (!take_radio(RadioJob::kHttp)) return false;
  const luabind::NetRefuse refused =
      (count == 2) ? net::http_download(words[0], place, kHttpMs)
                   : net::http_get_spill(words[0], kHttpBytes, place, kHttpMs);
  if (refused != luabind::NetRefuse::kOk) {
    drop_radio();
    if (refused == luabind::NetRefuse::kTooBig) {
      // The address does not fit, and it is said with the number: half an address
      // is a page nobody asked for (docs/30).
      error_out("http: that address is longer than the ");
      out_num(net::http_url_cap());
      out(" bytes the system carries\r\n");
    } else if (refused == luabind::NetRefuse::kInval) {
      error_out("http: there is no address to fetch\r\n");
    } else if (refused == luabind::NetRefuse::kNoVolume) {
      error_out("http: that is not a place this board can write\r\n");
    } else {
      error_out("http: the radio did not take it\r\n");
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
      usage_out("usage: power cpu [mhz]\r\n");
      return false;
    }
    const int32_t asked = static_cast<int32_t>(strtoll(words[1], nullptr, 10));
    if (asked <= 0) {
      usage_out("usage: power cpu [mhz]\r\n");
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

  if (count > 0 && strcmp(words[0], "last") == 0) {
    if (count != 1) {
      usage_out("usage: power last\r\n");
      return false;
    }
    // The number of the last sleep, kept where it can be read after a deep sleep has
    // taken the console line with the restart (docs/60).
    char report[48];
    if (!power::last_wake_report(report, sizeof(report))) {
      out("no sleep has been measured yet\r\n");
      return true;
    }
    out("last wake: ");
    out(report);
    newline();
    return true;
  }

  if (count > 0 && strcmp(words[0], "idle") == 0) {
    if (count > 2) {
      usage_out("usage: power idle [on|off]\r\n");
      return false;
    }
    if (count == 2) {
      if (strcmp(words[1], "off") == 0) {
        power::set_idle_sleep_allowed(false);
      } else if (strcmp(words[1], "on") == 0) {
        power::set_idle_sleep_allowed(true);
      } else {
        usage_out("usage: power idle [on|off]\r\n");
        return false;
      }
    }
    out("idle sleep: ");
    out(power::idle_sleep_allowed() ? "on" : "off");
    out(" (only with an alarm pending and a board that keeps the hour in a chip)\r\n");
    return true;
  }

  if (count != 0) {
    usage_out("usage: power | power cpu [mhz] | power idle [on|off] | power last\r\n");
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
  // The policy that puts the board down on its own, and whether a person has turned it
  // off (docs/60).
  out("idle sleep: ");
  out(power::idle_sleep_allowed() ? "on" : "off");
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
    usage_out("usage: sleep [light|deep] <ms> | at <epoch seconds>\r\n");
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
    usage_out("usage: sleep [light|deep] <ms> | at <epoch seconds>\r\n");
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
    usage_out("usage: led [rrggbb|off]\r\n");
    return false;
  }
  // A person at the prompt outranks a machine: this stops whatever the system was saying and takes
  // the light, which is why it is not the call an app uses (docs/30).
  if (!led::take(r, g, b)) {
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
    usage_out("usage: button\r\n");
    return false;
  }
  if (!button::available()) {
    out("button: this board has no button\r\n");
    return false;
  }
  const uint32_t keys = button::count();
  out("keys: ");
  out_num(keys);
  newline();
  for (uint32_t key = 0; key < keys; ++key) {
    out("  ");
    out_num(key);
    button::Info where = {};
    if (button::info(key, where)) {
      out(": ");
      out(hal::face_name(where.face));
      out("/");
      out(hal::spot_name(where.spot));
      out(" ");
      out(hal::kind_name(where.kind));
      if (where.kind == hal::KeyKind::kCompound) {
        out(" group ");
        out_num(where.group);
      }
    }
    out(button::down(key) ? ", down (" : ", up");
    if (button::down(key)) {
      out_int(static_cast<int32_t>(button::held_ms(key)));
      out(" ms)");
    }
    newline();
  }
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
    error_out("provision: the board did not start it\r\n");
    return false;
  }
  return true;
}

// The two ways out of the interpreter share one goodbye: the light it took, the VM and the prompt state
// all go together (docs/30). `exit` calls it from `lua_line`, and Ctrl+D calls it from `feed`.
bool leave_lua() {
  led::clear_colour();
  led::set_state(led::State::kOff);
  luabind::stop();
  g_lua = false;
  out("lua: session closed\r\n");
  return true;
}

}  // namespace

// **The answer and the switch that overrides it.** A serial bridge or a terminal that does not reply to the
// Device Attributes question would be a terminal without colours for ever, so `ansi on` says what the
// terminal could not (docs/30, docs/85).
bool console_ansi() { return ansi_now(); }

void set_ansi(bool on) {
  g_ansi_known = true;
  g_ansi = on;
  log_colour(on);
}

void prompt() {
  set_prompt_text(g_lua ? "lua> " : "lunok> ", true);
  g_esc = 0;
  g_escape = Escape::kNone;
}

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
  // The bar goes back to nobody's title: the app that was in front is not any more (docs/30).
  bar::title("");
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
  if (!luabind::start(boot, limit)) {
    drop_source();  // the text was read and nobody is going to compile it
    return false;
  }
  meter::note_app_start(boot.app_id);
  ui::attach();  // a fresh view for the app that is coming back (docs/30)
  bar::title(boot.app_title);

  bool ok = luabind::run_source(target.path, g_source, len);
  // **The text is the interpreter's now**, which has compiled it into its own state: the block goes back before
  // the app runs a single line (docs/20, docs/75).
  drop_source();
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
  bar::title("");  // the app in front is over (docs/30)
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
  g_editor.clear();
  g_last_was_cr = false;
  g_app_alive = false;
  g_esc = 0;
  g_escape = Escape::kNone;
  g_visible = 0;
  // **The terminal is asked what it is before the first line is written**, because what a person is about to
  // see depends on the answer: with ANSI the line is redrawn with its codes, and without it the shell washes
  // the old tail with spaces and backspaces (docs/85).
  detect_terminal();
  newline();
  prompt();
  if (g_early_len > 0) {
    // What arrived while the question was in the air was not an answer: it is a key, and it is not lost.
    const size_t early = g_early_len;
    g_early_len = 0;
    feed(g_early, early);
  }
}

// cfg: where the settings of the apps are kept. `/cfg` is a door and not a disk, so the
// person has to be able to see which unit is behind it and to move it; the choice is
// saved in the key-value store and survives a reboot (docs/70).
bool command_cfg(const char* argument) {
  char words[1][kWordCap];
  size_t count = 0;
  if (!read_words(argument, words, 1, count)) return false;
  if (count > 1) {
    usage_out("usage: cfg [auto|sd|littlefs]\r\n");
    return false;
  }
  if (count == 1 && !cfg::set_unit(words[0])) {
    // The reason is named: a person who asked for the card needs to know whether the
    // word was wrong or the card is missing.
    if (strcmp(words[0], "sd") == 0) {
      out("cfg: there is no card\r\n");
    } else if (strcmp(words[0], "littlefs") == 0) {
      out("cfg: the flash is not mounted\r\n");
    } else {
      out("cfg: auto, sd or littlefs\r\n");
    }
    return false;
  }
  if (cfg::unit()[0] == '\0') {
    out("settings: nowhere (neither the flash nor a card)\r\n");
    return true;
  }
  out("settings: ");
  out(cfg::unit());
  out(" (asked for ");
  out(cfg::preference());
  out(")\r\n");
  char dir[64];
  if (cfg::dir_for_app(dir, sizeof(dir), "<id>")) {
    out("  an app's: ");
    out(dir);
    newline();
  }
  return true;
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
    error_out("cannot start the interpreter\r\n");
    return false;
  }
  // The app is measured on its own from here (docs/30).
  meter::note_app_start(boot.app_id);
  g_lua = true;
  ui::attach();  // the session is an app too, and it draws on a view of its own (docs/30)
  bar::title("lua");
  out("lua: one line at a time, the whole API (docs/30). `exit` or Ctrl+D leaves.\r\n");
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
    return leave_lua();
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
    // A second word is the file the app is pointed at: `run txt res/notes.txt` is the app and what
    // it has to look at, which is the same thing `open txt res/notes.txt` says. The app reads it from
    // `app.args.path` and nothing else changes (docs/30, docs/40).
    char words[2][kWordCap];
    size_t count = 0;
    if (!read_words(line + 3, words, 2, count)) return false;
    if (count == 0) {
      usage_out("usage: run <id> [path]\r\n");
      return false;
    }
    return run_app(words[0], (count > 1) ? words[1] : nullptr);
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
  if (strcmp(line, "cat") == 0) {
    usage_out("usage: cat <path>\r\n");
    return false;
  }
  if (strncmp(line, "cat ", 4) == 0) {
    char words[1][kWordCap];
    size_t count = 0;
    if (!read_words(line + 4, words, 1, count)) return false;
    if (count == 0) {
      usage_out("usage: cat <path>\r\n");
      return false;
    }
    return print_file(words[0]);
  }
  if (strncmp(line, "hexdump", 7) == 0 && (line[7] == '\0' || line[7] == ' ')) {
    char words[1][kWordCap];
    size_t count = 0;
    if (!read_words(line + 7, words, 1, count)) return false;
    return hexdump_file((count == 0) ? "/rtc" : words[0]);
  }
  if (strcmp(line, "log") == 0) {
    log_dump();
    return true;
  }
  // **What the system costs, counted at the door.** A count, a time and the moment of the last call for
  // every device the system has touched, and `stats reset` for a run that has to be measured from zero
  // (docs/30, docs/95).
  if (strcmp(line, "stats") == 0) {
    meter::dump();
    return true;
  }
  if (strcmp(line, "stats reset") == 0) {
    meter::reset();
    out("meters: back to zero\r\n");
    return true;
  }
  if (strcmp(line, "clear") == 0) {
    // **The screen is cleared with the code only on a terminal that answered the question**: a pipe or a
    // serial line that never said it understood is not sent `ESC [ 2 J`, which would be four characters of
    // noise in the middle of its file (docs/85).
    if (!console_ansi()) {
      error_out("clear: this terminal did not answer the ANSI question\r\n");
      return false;
    }
    out("\x1b[2J\x1b[H");
    return true;
  }
  if (strcmp(line, "reboot") == 0) {
    out("restarting...\r\n");
    pal::system().reboot();
    return true;
  }
  // Cutting the power is not a sleep: nothing is armed and the button is the only way back in. The
  // board that can do it does not come back from the call at all, so the only line that can follow
  // is the one of a board that cannot —the ATOMs hold nothing to switch— and the person is told
  // instead of being left in front of a board that did nothing (docs/50).
  if (strcmp(line, "poweroff") == 0) {
    out("powering off...\r\n");
    pal::IConsole* console = pal::system().console();
    if (console != nullptr) console->flush();  // the line has to be out before the supply goes
    if (!platform().power()->power_off()) {
      error_out("this board cannot cut its own power: it holds nothing to switch off\r\n");
      return false;
    }
    shutdown();  // on the bench it is a process that ends; on the glass the line below is never read
    return true;
  }
  // **A terminal that does not answer is not a terminal without colour for ever**: the question is one
  // sequence and a serial bridge may eat it, so `ansi on` says what the terminal could not. `ansi` alone
  // answers what was detected, which is also the line the boot logs (docs/30, docs/85).
  if (strncmp(line, "ansi", 4) == 0 && (line[4] == '\0' || line[4] == ' ')) {
    const char* argument = line + 4;
    while (*argument == ' ') ++argument;
    if (*argument == '\0') {
      out(console_ansi() ? "ansi: on\r\n" : "ansi: off\r\n");
      return true;
    }
    if (strcmp(argument, "on") == 0) {
      set_ansi(true);
      out("ansi: on\r\n");
      return true;
    }
    if (strcmp(argument, "off") == 0) {
      set_ansi(false);
      out("ansi: off\r\n");
      return true;
    }
    usage_out("usage: ansi [on|off]\r\n");
    return false;
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
    usage_out("usage: verbose on|off\r\n");
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
  if (strncmp(line, "cfg", 3) == 0 && (line[3] == '\0' || line[3] == ' ')) {
    return command_cfg(line + 3);
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

    // **Ctrl+D leaves the Lua interpreter**, the same way `exit` does: it is the end of the input a
    // person at a prompt expects, and the line being typed is discarded without running it (docs/30,
    // docs/85). In the shell prompt the same byte means nothing and is ignored.
    if (c == 0x04 && g_lua) {
      g_editor.reset();
      newline();
      leave_lua();
      prompt();
      continue;
    }

    // **An escape starts a key, not a command.** Three lone ESC still stop the radio, but an arrow is
    // `ESC [ A` and is one press: the parser keeps its state when the sequence arrives in pieces
    // (docs/30, docs/85).
    if (c == 0x1b) {
      g_escape = Escape::kEsc;
      if (native_running() && ++g_esc >= kEscToStop) {
        g_esc = 0;
        g_escape = Escape::kNone;
        if (stop_radio_now()) {
          newline();
          out("radio: stopped\r\n");
          prompt();
        }
      } else if (!native_running()) {
        g_esc = 0;
      }
      continue;
    }

    bool consumed = true;
    if (g_escape == Escape::kEsc) {
      if (c == '[') {
        g_escape = Escape::kCsi;
        g_esc = 0;
      } else if (c == 'O') {
        g_escape = Escape::kSs3;
        g_esc = 0;
      } else {
        // A lone ESC that is not a sequence: drop it and let the byte be a normal key.
        g_escape = Escape::kNone;
        g_esc = 0;
        consumed = false;
      }
    } else if (g_escape == Escape::kCsi) {
      g_esc = 0;
      if (c >= '0' && c <= '9') {
        g_csi_number = static_cast<uint32_t>(c - '0');
        g_escape = Escape::kCsiNum;
      } else {
        switch (c) {
          case 'A': if (g_editor.previous()) redraw_line(); break;
          case 'B': if (g_editor.next()) redraw_line(); break;
          case 'C': if (g_editor.right()) redraw_line(); break;
          case 'D': if (g_editor.left()) redraw_line(); break;
          case 'H': if (g_editor.home()) redraw_line(); break;
          case 'F': if (g_editor.end()) redraw_line(); break;
          default: break;
        }
        g_escape = Escape::kNone;
      }
    } else if (g_escape == Escape::kCsiNum) {
      if (c >= '0' && c <= '9') {
        g_csi_number = g_csi_number * 10 + static_cast<uint32_t>(c - '0');
      } else {
        if (c == '~') {
          switch (g_csi_number) {
            case 1: if (g_editor.home()) redraw_line(); break;
            case 3: if (g_editor.erase_forward()) redraw_line(); break;
            case 4: if (g_editor.end()) redraw_line(); break;
            default: break;
          }
        } else {
          switch (c) {
            case 'A': if (g_editor.previous()) redraw_line(); break;
            case 'B': if (g_editor.next()) redraw_line(); break;
            case 'C': if (g_editor.right()) redraw_line(); break;
            case 'D': if (g_editor.left()) redraw_line(); break;
            case 'H': if (g_editor.home()) redraw_line(); break;
            case 'F': if (g_editor.end()) redraw_line(); break;
            default: break;
          }
        }
        g_escape = Escape::kNone;
      }
    } else if (g_escape == Escape::kSs3) {
      switch (c) {
        case 'A': if (g_editor.previous()) redraw_line(); break;
        case 'B': if (g_editor.next()) redraw_line(); break;
        case 'C': if (g_editor.right()) redraw_line(); break;
        case 'D': if (g_editor.left()) redraw_line(); break;
        case 'H': if (g_editor.home()) redraw_line(); break;
        case 'F': if (g_editor.end()) redraw_line(); break;
        default: break;
      }
      g_escape = Escape::kNone;
    } else {
      consumed = false;
    }
    if (consumed) continue;

    if (c == '\r' || c == '\n') {
      newline();
      if (!g_editor.empty()) {
        execute(g_editor.data());
        g_editor.remember();
      } else {
        g_editor.reset();
      }
      // The question of the selector is its own prompt: printing "lunok> " there
      // left the person answering after a prompt that belongs to the shell.
      if (!g_app_alive && !g_choosing && !native_running()) prompt();
    } else if (c == 0x7f || c == 0x08) {
      if (g_editor.backspace()) redraw_line();
    } else if (c >= 0x20 && c < 0x7f) {
      g_esc = 0;
      if (g_editor.insert(c)) redraw_line();
    }
  }
}

}  // namespace shell
}  // namespace kernel
