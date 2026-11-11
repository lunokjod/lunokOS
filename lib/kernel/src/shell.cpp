// The serial shell: line editing, the command table, and the app console once
// there is an app to run. It is the only user interface there is, because the
// board has no screen (docs/10 and docs/50).
//
// **The sections of this file, in order**:
//   - the console: output, colour, the prompt, the words of a line and the help;
//   - what the system says about itself: version, caps, mem, apps and mounts;
//   - the files: ls, cat, hexdump and open;
//   - the apps: run, the source of an app and the choice of a handler;
//   - the radio: wifi, ntp, http, provision, and the repo listing;
//   - repo install: the plan, the download and apps remove;
//   - the board: power, sleep, led, button and cfg;
//   - the app lifecycle: the window of an alarm, the turn and the close;
//   - the command table in `execute()`, and the console feed, the line editor and the Lua prompt.
#include "kernel/session.h"
#include "kernel/shell.h"
#include "kernel/splash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hal/hal.h"
#include "json/json.h"
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
#include "kernel/repository.h"
#include "kernel/touch.h"
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
//
// **48 KB since 0.125.0**: the port of the paper project's Timeclock is **44 545 B** of Lua —the day, the week, the
// chart and the page, with the comments that say why each number is what it is— and the 32 KB cap said no to an app
// that fits the board. What it costs is the moment of the load and nothing after it: the block is freed before the app
// runs a line, and the chunk the compiler keeps is **21 578 B** stripped (43 499 B with the debug names), which fits the
// 131 072 B ceiling of an interpreter with its 29 842 B floor to spare (docs/75, measured on the emulator).
//
// **64 KB since 0.143.0**: the reader of EPUB of this system is **49 710 B** of Lua —the shelf, the page, the foot, the
// menu, the index and the two faces— and the 48 KB cap said no to an app that fits the board. The block is the heap's
// and it is given back before the app runs a line (0.56.0), so 64 KB are **32 KB of heap while an app starts and
// nothing while it runs**.
//
// **96 KB since 0.149.0, and this *is* the decision of docs/46 D6**: an app may now be **more than one file** —`require`
// reads each of them into a block of its own (0.148.0)— and the ceiling of **one** file goes up as well, because the
// reader still has a page, its count and its foot in a single chunk and the next three things it wants (the faces of
// the book, the fixed layout and SVG) all add lines to it. **98 304 B** is three halves of 64 KB: the reader's own file
// is **49 215 B** and the room left is **49 089 B**, which is more than the whole app is today. What it costs is the
// moment of the load and nothing after it —the block is given back before the app runs a line— and the board that has
// to carry it is the one with the smallest heap: on the ATOM the free heap is **338 864 B** measured (docs/95), so the
// block is a quarter of it while an app starts. The whole app is now **4 files / 64 411 B** (docs/75, docs/46 D6).
const size_t kSourceCap = 98304;
// **The ceiling of an app's interpreter when the board does not say one**: 131 072 B since 4/11, and it was
// 65 536 B. The number is measured and not chosen: on an ATOM the interpreter is **29 842 B** before the app
// has run a line (13 021 of Lua itself, 13 200 of the UI library and the rest the other globals), so a 64 KB
// ceiling left about 35 KB for the app —and the same app that wrote six files of 15 000 B on the 3/11 died
// with `not enough memory` on the 4/11, because a `string.rep` of 15 000 B is two blocks of 15 000 while Lua
// builds and pools it. The ATOM answers **338 864 B free** (`docs/95`, measured with `mem`), so 131 072 is
// still 39 % of it and the system keeps 208 KB. The Paper is untouched: it says its own ceiling (4 MB of
// PSRAM) and never reaches this line (docs/75, docs/85).
const size_t kDefaultHeapBytes = 128 * 1024;  // used until the board ceiling is measured
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
// **Whether the app in front is detached from this console** (0.160.0): `run calculator &` starts it and the prompt
// comes back at once —`feed` goes to the shell and not to the app, and the app's own console words go to the log
// instead of the wire—. There is no thread and there is no second task: the app is polled by the same turn of the
// kernel as before, one thing at a time, which is the whole point (docs/30, docs/50).
bool g_app_detached = false;
// **Whether the next app to start goes to the background**: the `&` of a line says it and it lives for one start.
bool g_start_detached = false;
// True while the prompt is an interpreter (the `lua` command): the lines are not
// commands, they are Lua, and the prompt says so (docs/30).
bool g_lua = false;
// **True while the console session is open** (0.158.0): `exit` and `logout` close it —the prompt stops and the
// lines that arrive do not run— and the first byte that comes after it opens a new one. The board goes on running
// the queue and the alarms meanwhile; what closes is the session and not the machine, which is what `poweroff`
// does (docs/30, docs/50).
bool g_session_open = true;
// ESC pressed in a row: three of them stop a command that is waiting, the same
// way they leave an app (docs/30). An arrow or a function key is `ESC [ …` and is
// one key press, so the parser below keeps its state between feeds.
const uint8_t kEscToStop = 3;
uint8_t g_esc = 0;
enum class Escape : uint8_t { kNone, kEsc, kCsi, kCsiNum, kSs3 };
Escape g_escape = Escape::kNone;
uint32_t g_csi_number = 0;

// ---- the sessions of the shell that are not the console (0.159.0) ----
//
// **A session is a shell somebody asked for**: an app writes lines into it and reads what the shell answers, and the
// console is not involved at all. It is what makes the shell a thing of the **system** and not of the wire it happens
// to be talking to, and it is three pipes —the lines in, the answers out and the complaints out— because that is what
// a shell of a system is to whoever calls it (docs/30, docs/75).
//
// **One at a time, and the rings are the heap's**: the money is the ring of the answers (1 KB), the ring of the
// complaints (256 B) and the line being typed (128 B), 1 408 B in all while a session lives and nothing after it. A
// second session at once is not a refusal of principle —it is one more block of the same size and a second `Editor`
// to go with it— it is simply not written yet, and a call that says `E_BUSY` and why is better than a queue nobody
// drains (docs/30, docs/75).
const uint8_t kMaxSessions = 1;
const size_t kSessionOutBytes = 1024;  // what the shell answers and nobody has read yet
const size_t kSessionErrBytes = 256;   // the complaints, which are shorter than the answers
const size_t kSessionLineBytes = 128;  // one line of a session, the width of a word of a command

enum class Pipe : uint8_t { kOut = 0, kErr = 1 };

// **A ring inside a block**: `head` is where the next byte goes and `tail` where the next one is read, both counted
// modulo the cap. A byte that does not fit is dropped and counted, because an app that stops reading must not stop
// the shell (docs/30).
struct Ring {
  uint8_t* bytes = nullptr;
  uint16_t cap = 0;
  uint16_t head = 0;
  uint16_t tail = 0;
  uint32_t dropped = 0;

  void put(const void* data, size_t len);
  size_t get(char* out, size_t cap);
  uint16_t used() const { return static_cast<uint16_t>((head + cap - tail) % cap); }
};

struct Session {
  bool in_use = false;
  Ring out;
  Ring err;
  char line[kSessionLineBytes];
  uint16_t line_used = 0;
  bool close_asked = false;
};

Session g_sessions[kMaxSessions];
// **Where the shell's own words go**: zero is the console and anything else is a session (`id`, one based). Every
// `out` of this file goes through `out_bytes`, which is why one number is the whole of the redirection (docs/30).
uint8_t g_sink = 0;
// **Whether the words are the answer or the complaint**: `error_out` says so for the length of one line, and
// `out_bytes` sends it to the other pipe when it is (docs/30).
bool g_to_error = false;

Session* session_of(uint8_t id) {
  if (id == 0 || id > kMaxSessions) return nullptr;
  Session& session = g_sessions[id - 1];
  return session.in_use ? &session : nullptr;
}

// **A session goes back to nothing**: the block of its pipes is the caller's to give back and the rest is the shape
// of a session that is not there (docs/30).
void reset_session(Session& session) {
  session.in_use = false;
  session.out = Ring();
  session.err = Ring();
  session.line_used = 0;
  session.close_asked = false;
}

// **The bytes that did not fit are counted and not lost in silence**: an app that never reads would otherwise make
// the shell look quiet. `dropped` is the number it can ask for (docs/30).
void Ring::put(const void* data, size_t len) {
  if (bytes == nullptr || cap == 0) return;
  const uint8_t* in = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < len; ++i) {
    const uint16_t next = static_cast<uint16_t>((head + 1) % cap);
    if (next == tail) {  // full: the byte is dropped and said
      ++dropped;
      continue;
    }
    bytes[head] = in[i];
    head = next;
  }
}

size_t Ring::get(char* out, size_t cap) {
  if (bytes == nullptr || cap == 0) return 0;
  size_t got = 0;
  while (got + 1 < cap && head != tail) {
    out[got++] = static_cast<char>(bytes[tail]);
    tail = static_cast<uint16_t>((tail + 1) % cap);
  }
  out[got] = '\0';
  return got;
}

void session_put(uint8_t id, Pipe pipe, const void* data, size_t len) {
  Session* session = session_of(id);
  if (session == nullptr) return;
  ((pipe == Pipe::kErr) ? session->err : session->out).put(data, len);
}

void out_bytes(const void* data, size_t len) {
  if (g_sink != 0) {
    session_put(g_sink, g_to_error ? Pipe::kErr : Pipe::kOut, data, len);
    return;
  }
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
  // **A complaint is the other pipe of a session** (0.159.0): the flag lives for one line and `out_bytes` reads it,
  // so a session sees what went wrong on its own pipe and an app does not have to guess it from the answers
  // (docs/30).
  const bool was = g_to_error;
  g_to_error = true;
  out(text);
  g_to_error = was;
  colour_off();
}

void good_out(const char* text) {
  colour(kColourGood);
  out(text);
  colour_off();
}

void usage_out(const char* text) {
  colour(kColourWarn);
  // **A usage line is a complaint and not an answer** (0.159.0): a session has a pipe for each, so what went wrong is
  // asked for where it went, and `execute` answering false is the same thing said to the caller (docs/30).
  const bool was = g_to_error;
  g_to_error = true;
  out(text);
  g_to_error = was;
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

// **The syntax of a line of help is what is before the two spaces** —`repo info <app>`, `cat <path>`—, and it is
// what goes in bold; the description is the rest of the line and it stays as it was. A line without a description
// —the two pieces of advice at the end— has no syntax to be a command (docs/30).
const char* help_syntax(const char* text) {
  while (*text == ' ') ++text;
  return text;
}

size_t help_syntax_len(const char* text) {
  const char* start = help_syntax(text);
  const char* end = strstr(start, "  ");
  return (end == nullptr) ? 0 : static_cast<size_t>(end - start);
}

// **Whether the syntax of a line answers to the words a person asked for**: the words are a whole command and not
// the beginning of one —`help pow` is not `power`—, so what follows them has to be the end of the syntax or a space
// (0.88.1, docs/30).
bool help_matches(const char* text, const char* only) {
  const size_t len = help_syntax_len(text);
  if (len == 0) return false;
  const size_t asked = strlen(only);
  if (asked == 0 || asked > len) return false;
  if (strncmp(help_syntax(text), only, asked) != 0) return false;
  return asked == len || help_syntax(text)[asked] == ' ';
}

// **How many lines of the list answered the question**: `print_help` clears it and a word that answers to nothing is
// the one thing that is said back (0.88.1, docs/30).
uint8_t g_help_written = 0;

// **Whether the line before this one was printed**, which is what a line of continuation hangs from (0.170.0). `splash`
// and `cfg` do not fit one row of this console, so they take two, and the second one has no syntax of its own: it said
// where the command starts and nothing else, and a `help splash` that filters on the syntax of a line dropped it —the
// sentence ended in a comma— (found by reading the list, 9/11, docs/30).
bool g_help_previous = false;

// One line of the list, or nothing when the person asked for another command.
void help_line(const char* text, const char* only) {
  const size_t len = help_syntax_len(text);
  g_help_previous = (only == nullptr) || (len > 0 && help_matches(text, only));
  if (!g_help_previous) return;
  ++g_help_written;
  if (len == 0) {
    out(help_syntax(text));
    out("\r\n");
    return;
  }
  out("  ");
  colour(kColourBold);
  out_bytes(help_syntax(text), len);
  colour_off();
  out(help_syntax(text) + len);
  out("\r\n");
}

// **The rest of the sentence of the line before**: printed when that one was, and only then —a continuation has no
// syntax to be asked about, so it cannot answer a word of its own— (0.170.0, docs/30).
void help_continuation(const char* text, const char* only) {
  if (only != nullptr && !g_help_previous) return;
  ++g_help_written;
  out(text);
  out("\r\n");
}

void print_help(const char* only) {
  g_help_written = 0;
  help_line("  help [command]    this list, or the lines of one command", only);
  help_line("  version           version and board", only);
  help_line("  caps              what this board has", only);
  help_line("  mem               free memory and buffers", only);
  help_line("  apps              the apps of the flash and of the card", only);
  help_line("  apps info <id>    the manifest of an app of this disk, key by key", only);
  help_line("  apps remove <id>  delete an app, on the disk it lives on (settings and data stay)", only);
  help_line("  mounts            the mounted file systems and their room", only);
  help_line("  splash [<moment>|hide|<path>]  a picture on the glass, washed whole (boot,", only);
  help_continuation("                    poweroff, reboot, charging, sleep, or the path of a file)", only);
  help_line("  cfg [auto|sd|littlefs|keyboard [<id>]|boot [<word>]|home [<id>]]  settings: where the apps' cfg", only);
  help_continuation(
      "                    lives, the keyboard, the picture of the boot and the app that comes up with the board;", only);
  help_continuation(
      "                    `cfg tz [<offset>]`, `cfg leap [on|off]`, `cfg week [monday|sunday]` and", only);
  help_continuation("                    `cfg turn [<instructions>]`, the slice of Lua a turn is worth", only);
  help_line("  run <id> [path] [&]  run an app, optionally pointed at a file; `&` leaves it in the background", only);
  help_line("  wifi              the stored network and the radio", only);
  help_line("  wifi scan         the networks the radio sees", only);
  help_line("  wifi add <ssid> [password]  store a network and go on it", only);
  help_line("  wifi connect      go on the network the board has stored", only);
  help_line("  wifi forget       forget the stored network", only);
  help_line("  wifi stop         drop whatever the radio is doing", only);
  help_line("  provision [pop]   the QR and the Espressif app, over Bluetooth", only);
  help_line("  provision softap [pop]  the same over the board's own access point", only);
  help_line("  ntp [server]      set the clock from a time server", only);
  help_line("  http <url> [path]  fetch a page, and save it when it is too big", only);
  help_line("  repo [owner/repo] [dir]  the folders of a directory of the repository, from GitHub", only);
  help_line("  repo info <app>   the manifest of one app of the repository", only);
  help_line("  repo install <app>  the whole folder of an app, onto the disk `cfg` names", only);
  help_line("  power [cpu [mhz]]  what this board can do about energy", only);
  help_line("  power last        how far the last sleep was from the hour it was given", only);
  help_line("  power idle [on|off]  whether the board may sleep on its own to the next alarm", only);
  help_line("  led [rrggbb|off]  the light of the board", only);
  help_line("  button            the button, right now", only);
  help_line("  sleep [light|deep] <ms>  sleep, and come back at a time", only);
  help_line("  sleep at <epoch seconds>  the same, at a time of the clock", only);
  help_line("  open [app] <path>  open a file with the app that handles it", only);
  help_line("  ls [path]         list a directory or one file (default /)", only);
  help_line("  cat <path>        print a file", only);
  help_line("  hexdump <path>    the bytes of a file in hex and in visible letters", only);
  help_line("  lua               the prompt becomes the interpreter (`exit` or Ctrl+D leaves)", only);
  help_line("  log               last lines of the log ring", only);
  help_line("  stats [reset]     what each device cost: calls, time, last call", only);
  help_line("  verbose on|off    mirror the log to the console too", only);
  help_line("  ansi [on|off]     the colours of the terminal, or why there are none", only);
  help_line("  clear             clear the screen", only);
  help_line("  reboot            restart", only);
  help_line("  poweroff          cut the power of the board, if it can (docs/50)", only);
  // **Two lines and not one with a comma** (0.158.0): `help` answers a word of the syntax, and a line that says
  // `exit, logout` answers neither of the two —the rule is `help pow` is not `power`, and a comma is not a
  // space— (docs/30).
  help_line("  exit              close this session of the console; a key opens it again (Ctrl+D the same)", only);
  help_line("  logout            the same as exit", only);
  help_line("three ESC in a row leave an app, or stop a command that waits", only);
  help_line("a path with spaces goes in quotes: cat \"res/el meu fitxer.txt\"", only);
  // **A word that answers to nothing is said and not swallowed**: `help` alone lists them all, and a person who
  // typed a command that is not there is one line away from the whole list (0.88.1, docs/30).
  if (only != nullptr && g_help_written == 0) {
    error_out("no such command: ");
    out(only);
    newline();
    out("  `help` lists them all");
    newline();
  }
}

void print_version() {
  out("lunokOS ");
  colour(kColourBold);
  out(kVersion);
  colour_off();
  out("  board ");
  out(platform().id());
  // The board and the emulator are both true at once on the PC (docs/85).
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
    // any of them fails on a heap with room to spare. The board has the number and so does the emulator
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
  // **The memories of a session an app is keeping** (0.179.0): the first thing the allocator gives back when a block does not
  // fit, so a person looking at a board that is short of memory sees what is being held and by whom it is held (docs/30,
  // docs/75).
  if (session::count() > 0) {
    out("  session memory     ");
    out_num(session::bytes_held() / 1024);
    out(" KB in ");
    out_num(session::count());
    out("/");
    out_num(session::kMaxBlocks);
    out(" block(s)\r\n");
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
  // **The app of the card says so** (0.87.1): the same id may sit on the flash and on the card, and the two are presented
  // apart —`<id>` and `<id>-SD`— so a person who is trying a version can read which of the two copies each line is
  // (0.88.20, docs/40, docs/70).
  if (entry.on_card) {
    colour(kColourDim);
    out("  [card]");
    colour_off();
  }
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

// **`splash`**: the picture of a moment on the glass, washed whole, or the glass given back to paper. With no
// word it says which moments this build carries a picture for, which is the question a person asks before
// writing a picture of their own (0.111.0, docs/30, docs/92).
bool command_splash(const char* rest) {
  char words[1][kWordCap];
  size_t count = 0;
  if (!read_words(rest, words, 1, count)) return false;
  if (count == 0) {
    const uint8_t moments = splash::count();
    for (uint8_t i = 0; i < moments; ++i) {
      const splash::Kind kind = splash::at(i);
      const char* name = splash::word(kind);
      char path[96];
      out("  ");
      out(name);
      spaces(strlen(name) < 10 ? 10 - strlen(name) : 1);
      // **The picture that would be drawn** and not the plain name of the moment (0.178.5): a moment that carries
      // several drawings shows one of them at random, and a list that said `boot.png` while the boot draws `boot-3.bmp`
      // would be a list that lies (docs/30).
      if (splash::picture_of(kind, path, sizeof(path))) {
        out(path);
      } else {
        out("no picture");
      }
      out("\r\n");
    }
    out("splash [<moment>|hide|<path>]\r\n");
    return true;
  }
  if (strcmp(words[0], "hide") == 0) {
    if (!splash::hide()) {
      out("splash: ");
      out(splash::last_error());
      out("\r\n");
      return false;
    }
    out("splash: the glass is paper\r\n");
    return true;
  }
  // **A word of a moment or a path**: a picture the caller names is what an app or a person tries before it is
  // put where the system looks for it.
  const splash::Kind kind = splash::kind_of(words[0]);
  const bool painted = (kind != splash::kNone) ? splash::show(kind) : splash::show(words[0]);
  if (!painted) {
    out("splash: ");
    out(splash::last_error());
    out("\r\n");
    return false;
  }
  out("splash: ");
  out((kind != splash::kNone) ? splash::word(kind) : words[0]);
  out("\r\n");
  // **The strip of the system comes back over the picture** (0.164.3): the moment was asked for by a person, the page
  // is still the app's and the glass under the bar is not the bar any more (docs/30, docs/50). **And the page under it is
  // owed whole** (0.179.5): the picture is ink the page did not draw, so the app's next present is the whole view with the
  // pass that washes it —the same rule the boot follows— (docs/50).
  bar::refresh();
  ui::owe_the_page();
  return true;
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
      // and `out_num` takes 32 bits, so the card overflowed it and printed zero (measured on the emulator,
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

// **Eight names and no more**: the hint of a path that is not there has to fit on one screen, and what is needed
// is seeing the name that is there next to the one that was typed (docs/30).
const uint8_t kHintEntries = 8;

// Prints the names of a directory as a list of one line, or counts them when `quiet` says so: the backend lends a
// name for the length of one call, so a header that says what is coming cannot wait for the walk to end (docs/50).
class NamePrinter : public hal::ListSink {
 public:
  explicit NamePrinter(bool quiet) : quiet_(quiet) {}
  bool on_entry(const hal::ListEntry& entry) override {
    if (shown_ == kHintEntries) {
      ++more_;
      return true;
    }
    if (!quiet_) {
      out((shown_ == 0) ? " " : ", ");
      out(entry.name);
    }
    ++shown_;
    return true;
  }
  uint8_t shown() const { return shown_; }
  uint32_t more() const { return more_; }

 private:
  bool quiet_;
  uint8_t shown_ = 0;
  uint32_t more_ = 0;
};

// **Where what was typed is**: a path that is not there is said with the piece that is there and what it holds,
// because seeing `apps` next to an `app` is what finds the letter that is missing. Without it, a `cannot list`
// leaves a person looking at a path that seems right (docs/30, docs/70).
void say_where_it_is(const char* path) {
  char trial[128];
  snprintf(trial, sizeof(trial), "%s", (path != nullptr) ? path : "");
  while (true) {
    char* slash = strrchr(trial, '/');
    if (slash == nullptr) return;
    // The last component goes; if what is left is not a directory, another one goes. The root is the last answer.
    if (slash == trial) {
      trial[1] = '\0';
    } else {
      *slash = '\0';
    }
    hal::Stat info;
    if (vfs::stat(trial, info) && info.type == hal::NodeType::kDir) {
      NamePrinter count(true);
      if (!vfs::list(trial, count) || count.shown() == 0) return;
      colour(kColourDim);
      out("  ");
      out((strcmp(trial, "/") == 0) ? "the root" : trial);
      out(" has:");
      NamePrinter names(false);
      vfs::list(trial, names);
      if (names.more() > 0) {
        out(" and ");
        out_num(names.more());
        out(" more");
      }
      colour_off();
      newline();
      return;
    }
    if (slash == trial) return;
  }
}

void list_at(const char* path) {
  hal::Stat info;
  if (!vfs::stat(path, info)) {
    error_out("no such directory: ");
    out(path);
    newline();
    say_where_it_is(path);
    return;
  }
  // **A file is listed too**: a person who asks about a path wants to know what is there, and a file has a name
  // and a size like any entry of a folder (docs/30).
  if (info.type != hal::NodeType::kDir) {
    hal::ListEntry one;
    const char* base = strrchr(path, '/');
    one.name = (base != nullptr) ? base + 1 : path;
    one.type = info.type;
    one.size = info.size;
    ListPrinter printer;
    printer.on_entry(one);
    return;
  }
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
bool print_file(const char* path) {
  hal::Stat info;
  if (!vfs::stat(path, info)) {
    error_out("no such file: ");
    out(path);
    newline();
    // **And where the one that is there lives**: the same `ls` does, because a path typed by hand gets it wrong the
    // same way in both (0.86.1, docs/30).
    say_where_it_is(path);
    return false;
  }
  if (info.type != hal::NodeType::kFile) {
    error_out("that is not a file: ");
    out(path);
    newline();
    return false;
  }
  hal::IStorage* storage = nullptr;
  char rel[128];
  if (!vfs::resolve(path, storage, rel, sizeof(rel)) || storage == nullptr) {
    error_out("cannot read: ");
    out(path);
    newline();
    return false;
  }

  // **The whole file and not its first thousand bytes** (4/11): this used to read one window of 1 024 B with
  // `vfs::read_file` and print it, so a `cat` of a page of `http` stopped after a screen and a half without saying
  // anything —the worst of both worlds, because a cut output looks like a cut file. It is read in windows up to the
  // end now, and what it costs is the wire: 1.3 MB at 115 200 baud is about two minutes, which is what the board
  // would take (docs/30, docs/50, docs/70).
  hal::FileSlot slot;
  StorageReadClock clock(storage);
  clock.begin();
  const bool opened = storage->open(rel, hal::OpenMode::kRead, slot);
  clock.end();
  if (!opened) {
    error_out("cannot read: ");
    out(path);
    newline();
    return false;
  }
  char buf[kPrintCap];
  uint32_t total = 0;
  char last = '\n';
  while (true) {
    clock.begin();
    const int32_t got = storage->read(slot, buf, static_cast<uint32_t>(sizeof(buf)));
    clock.end();
    if (got <= 0) break;
    out_bytes(buf, static_cast<size_t>(got));
    last = buf[got - 1];
    total += static_cast<uint32_t>(got);
  }
  clock.begin();
  storage->close(slot);
  clock.end();
  // A file that does not end in a newline gets one, or the prompt would be glued to its last line.
  if (total > 0 && last != '\n') newline();
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
  // **The extensions of the app, joined the way a manifest writes them** (0.120.0): `"txt md lua"` is what the app
  // reads at `app.info().opens`, and it is the same text whatever way the manifest wrote it. 96 B is the most a list
  // of eight names can take, which is what `apps::Entry` carries (docs/30, docs/40).
  char opens[96];
  // **The switches the app wants when it is handed a file** (0.120.0): `opensArgs` of the manifest, which the app
  // reads at `app.args.flags` and the system never reads itself (docs/30, docs/40).
  char opens_arg[32];
  // **The picture of the app, if its manifest names one** (0.114.0): a path inside the app, said with the whole
  // door in front —`/apps/<id>/res/logo.png`— when it is painted, so the card's copy of an app shows its own
  // picture and not the flash's (docs/40).
  char splash[48];
  // The boards the app asks for, and whether this one is among them: the refusal
  // says both things, because being refused by name is not a mystery (docs/40).
  char boards[apps::Entry::kMaxBoards][24];
  uint32_t board_count;
  bool works_here;
  bool found;
};

bool find_entry(const apps::Entry& entry, void* context) {
  RunTarget* target = static_cast<RunTarget*>(context);
  // **The app is where the door says it is** (0.88.20): `/apps/<id>`, which is the flash's copy for `squares` and the
  // card's for `squares-SD` —the same answer the listing and the `res/` of the app get— (docs/40, docs/70).
  char root[96];
  if (!apps::root_of(entry.id, root, sizeof(root))) return true;
  const int written = snprintf(target->path, sizeof(target->path), "%s/%s", root, entry.entry_file);
  if (written <= 0 || static_cast<size_t>(written) >= sizeof(target->path)) return true;
  memcpy(target->title, entry.title, sizeof(target->title));
  memcpy(target->version, entry.version, sizeof(target->version));
  apps::opens_text(entry, target->opens, sizeof(target->opens));
  memcpy(target->opens_arg, entry.opens_arg, sizeof(target->opens_arg));
  memcpy(target->splash, entry.splash, sizeof(target->splash));
  memcpy(target->boards, entry.boards, sizeof(target->boards));
  target->board_count = entry.board_count;
  target->works_here = entry.works_here;
  target->found = true;
  return false;  // stop the walk: there is nothing else to look for
}

// **Where the app is and what its manifest says, with nothing read and no VM**
// (0.151.17): the picture a start shows is the one the manifest names, so it has
// to be known *before* anything that costs time begins —the source read and the
// building of the interpreter both come after it.
bool find_app(const char* id, RunTarget& target) {
  target.path[0] = '\0';
  target.found = false;
  apps::for_each(id, &find_entry, &target);
  if (!target.found) {
    error_out("no such app: ");
    out(id);
    newline();
    // **Where it was looked for, said once and in full** (0.88.20): the apps live in the door `/apps`, which is the
    // flash's `apps/` and the card's together, so a person who does not find it knows where to look before doubting the
    // name they typed —and a name the flash has and the card also has is the one without the mark— (docs/40, docs/70).
    out("  apps are in ");
    out(apps::kAppsDoor);
    if (!apps::card_here()) out(", and there is no card in the slot");
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
    if (platform().emulator()) out(" (the emulator is told with --board)");
    newline();
    return false;
  }
  return true;
}

// Reads the entry chunk of an app that `find_app` has already found. It is the
// expensive half of a start —98 304 B of heap and a file read— and it is kept
// apart from the lookup so that a start can put its picture on the glass first
// (docs/30, docs/40).
bool read_source(const RunTarget& target, size_t& len) {
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

// **The two halves together, for a caller with nothing to say to the glass in
// between** (0.151.17): an alarm window is not a start, it shows no picture and
// it owns no page, so it goes straight to the source (docs/60).
bool load_source(const char* id, RunTarget& target, size_t& len) {
  return find_app(id, target) && read_source(target, len);
}

bool run_app(const char* id, const char* open_path, bool restore = false) {
  // **A start needs the interpreter, and the one in front still has it** (0.166.3): `luabind::start` answers no while a
  // state is alive —`if (g_state != nullptr) return false`—, so `run <id>` asked for while an app was in front said
  // `cannot start the interpreter` **for every app of the disk**, which a person reads as a name that does not resolve
  // and is not one (found on the emulator on 8/11 with the launcher in front, docs/30, docs/40). The app in front goes the
  // way it goes when another app asks for the glass: its goodbye hooks, its views, its colour and the bar with it.
  if (g_app_alive) close_app();
  RunTarget target;
  // **Where the app is and what its manifest says, with nothing read yet**
  // (0.151.17): the picture a start shows is the one the manifest names, and a
  // start that shows it has to know its name before it pays for the source block
  // and for the interpreter. Only the lookup happens here —a walk of the listing
  // and a string copy— and it is also what refuses an app of another board.
  if (!find_app(id, target)) return false;

  // The app gets a view of its own, white and with nothing pending; the glass is
  // not touched until it presents (docs/30).
  ui::attach();
  // **The app's own picture, before the app is loaded and before the Lua machine
  // exists** (0.151.17, and 0.114.0 before it): a manifest that names one gets
  // the whole glass washed and the image in the middle —the system's `splash` and
  // not a paint of the app's, because an app cannot paint before it runs— and the
  // first page it presents takes it away. **It goes after `ui::attach()` and not
  // before**, because the attach washes the glass whole when the page before it
  // had painted something, and a wash after the picture is a picture nobody ever
  // sees: that is exactly what the test of this found on the emulator, where the
  // page of the test before had left its ink counted (docs/30, docs/40).
  // It is skipped for an app that comes back from a sleep: a resume is not a
  // start. A picture that is not there is a line of the log and nothing else; the
  // app runs all the same.
  if (target.splash[0] != '\0' && !restore) {
    char root[96];
    char picture[160];
    const int made = apps::root_of(id, root, sizeof(root))
                         ? snprintf(picture, sizeof(picture), "%s/%s", root, target.splash)
                         : -1;
    if (made > 0 && static_cast<size_t>(made) < sizeof(picture)) {
      if (!splash::show(picture)) {
        log_printf(pal::kLogDebug, "SPLASH", "%s: %s", id, splash::last_error());
      } else {
        // **The picture washes the strip of the system and nothing else says so** (0.164.3): the splash washes the whole
        // glass —the bar with it— and the bar paints itself only when something of it changes, so the title that comes
        // after it repainted the box of the title and not the strip, and the hour and the battery stayed erased. The
        // refresh is asked **here** and not inside the splash, because a moment that goes down (poweroff, reboot) must
        // not paint a strip on the picture (docs/30, docs/50).
        bar::refresh();
        // **And the page is owed whole** (0.179.5): the picture is on the glass and the page of the app that starts did not
        // put it there, so its first present washes it —the launcher's `gc16` of the whole first page is this rule said by
        // an app that could not know it— (docs/50).
        ui::owe_the_page();
      }
    }
  }
  // And its title goes to the middle of the bar, which is the system's and not
  // the app's (docs/30).
  bar::title(target.title);

  // **Now the expensive part**: 98 304 B of heap, a file read and a Lua machine.
  size_t len = 0;
  if (!read_source(target, len)) {
    // **The view and the bar are already the app's, and the app is not coming**
    // (0.151.17): the attach and the title happened before this read, so a start
    // that dies here gives both back, as an app that could not compile does.
    ui::give_back_the_glass();
    bar::title("");
    return false;
  }

  luabind::Boot boot;
  boot.board = platform().id();
  boot.version = kVersion;
  boot.emulator = platform().emulator();
  boot.app_id = id;
  boot.app_title = target.title;
  boot.app_version = target.version;
  boot.app_opens = target.opens;
  // **The app's own switches, never the system's** (0.120.0): `opensArgs` of the manifest goes to the app as
  // `app.args.flags` and nowhere else —the system does not read it, does not split it and does not act on it— (docs/30).
  boot.args_flags = target.opens_arg;
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
    // **The view and the bar are already the app's here too** (0.151.17): an
    // interpreter that does not come up is a start that did not happen.
    ui::give_back_the_glass();
    bar::title("");
    error_out("cannot start the interpreter\r\n");
    return false;
  }
  // The app is measured on its own from here (docs/30).
  meter::note_app_start(boot.app_id);

  // The app is compiled and given a task, but not run here: the first turn is
  // the first poll, and from then on the kernel keeps turning (docs/30).
  const bool compiled = luabind::run_source(target.path, g_source, len);
  // **The text is the interpreter's from here**, which has compiled it into its own state: the block goes back
  // before the app runs a single line (docs/20, docs/75).
  drop_source();
  if (!compiled) {
    luabind::stop();
    // **An app that did not even start gives back what it was given** (0.136.0, docs/46 D1): the title of the bar and
    // the view, if the text it could not compile had asked for the whole glass before it failed.
    ui::give_back_the_glass();
    bar::title("");
    return false;
  }
  // **The app is in front from the moment its first line runs** (0.153.0): the first turn of its entry is a turn like the
  // ones that come after, and an app that asks for another one from its own first lines —`app.run` in the start of a
  // launcher is exactly that— must not be answered "no app is in front" by a system that is running it.
  g_app_alive = true;
  // **And it starts on its only page** (0.163.0): the views of the app before it go with the app before it, or the
  // first pinch of a page would take away a view nobody pushed (docs/30).
  apps::reset_views();
  // **`run calculator &` is an app whose console is not this one** (0.160.0): it is polled like any other and the
  // prompt stays where it is, so the person at the terminal keeps their shell while the app runs. What it prints
  // goes to the log and not to the wire, because two writers on one prompt is the mess `&` exists to avoid
  // (docs/30, docs/50).
  g_app_detached = g_start_detached;
  g_start_detached = false;
  luabind::set_console_attached(!g_app_detached);
  if (g_app_detached) {
    log_printf(pal::kLogInfo, "APPS", "%s goes to the background: its console is the log", id);
  }
  if (!luabind::poll()) {  // it never waited: it is already over
    g_app_alive = false;
    g_app_detached = false;
    luabind::set_console_attached(true);
    const int code = luabind::exit_code();
    luabind::stop();
    // **An app that ends in its first turn is an app that ended** (0.136.0, docs/46 D1): same two lines as an app that
    // ends between two turns, or a page that failed would keep the bar away and its title on it (docs/30).
    ui::give_back_the_glass();
    bar::title("");
    if (code != 0) {
      out("app exit ");
      out_num(static_cast<uint32_t>(code));
      newline();
    }
    return true;
  }
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
// **The ids of the apps that can open a file** (0.88.20): the name the door of the apps presents, which carries `-SD` on the
// copy of the card, so the table is as wide as `apps::Entry::id` and an app is never offered by a cut name.
char g_choice_ids[kMaxChoices][40];
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
  if (entry.opens_count == 0) return true;
  if (!apps::handles(entry, g_choice_ext)) return true;
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

  char id[40];
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

  // **The name of an app as the door presents it** (0.88.20): `reader`, or `reader-SD` for the copy of the card, which is
  // what `run` and the listing answer to.
  char app_id[40];
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

enum class RadioJob { kNone, kScan, kAdd, kConnect, kProvision, kNtp, kHttp, kRepository, kInstall };
RadioJob g_job = RadioJob::kNone;
char g_job_ssid[33];
// **Where the listing of the repository went**: the command starts the download and folds, and the one that receives
// it when the radio has finished is `radio_done`, so the path has to live outside the stack of that call (docs/30).
char g_repo_path[128];
char g_repo_name[64];
// **What has to be done with the answer**: the list of a directory or the manifest of an app. The command starts the
// work and folds, and the one that carries on when the radio has finished is `radio_done` (docs/30).
bool g_repo_info = false;
char g_repo_app[32];

// **`repo install` is a chain and not a call** (0.86.0): the listing of a folder is one download and every file is
// another, and between two of them the radio comes back to the kernel. What has to live outside the stack is the
// plan —where it goes, what is left to do and which file of the listing it is on—, which is all of this
// (docs/30, docs/70, docs/75).
char g_install_app[32];
char g_install_repo[64];
// **Where the app is and where it is being made** (0.88.14): the name of the app is taken only when every file is there,
// so what is downloaded lives in a folder of its own inside `apps/` and a chain that dies half way does not leave half
// an app that `apps` would count as one (docs/30, docs/70).
char g_install_root[96];   // "/sd/apps/notes", and what a message says
char g_install_work[112];  // "/sd/apps/.notes.install"
char g_install_url[208];   // the address in flight
// **The path of the file in flight, whole**: the folder of the work (a hundred and eleven), the folder being read
// (forty-seven) and the name (forty-seven) are two hundred and eight with the two slashes and the terminator, and a
// destination of two hundred cut the path —the compiler found it on the board's build, 4/11—, which is a file written
// somewhere the system did not mean (docs/30, docs/70).
char g_install_dest[224];  // where the file in flight lands
char g_install_list[176];  // the listing of the folder being read
char g_install_dir[48];    // the folder being read, empty for the root of the app
// **The queue of folders is a file of the app's folder and not a table in RAM** (0.88.9): six of them was a ceiling that
// an app has no reason to respect —an app of nine folders was an install that said so and stopped—, and a folder inside
// a folder had no room in it at all. One line per folder, with the path it has inside the app, and the disk is the
// ceiling (docs/70, docs/75).
char g_install_plan[112];        // "<root>/.folders"
uint16_t g_install_folders = 0;  // folders in the plan
uint16_t g_install_folders_done = 0;
// **Whether the reader is walking the listing of this folder**: it is opened on the first entry and it stays open until
// the folder is finished, which is what turns a listing of twenty files from twenty passes over the document into one
// (0.88.5, docs/75). There is no index to keep: the reader knows where it is.
bool g_install_walking = false;
uint16_t g_install_files = 0;
uint32_t g_install_bytes = 0;
bool g_install_listing = false;  // what is in flight: a listing or a file
bool g_install_pending = false;  // the next step waits for the turn after

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
// **A manifest is small by definition** (the reader of the disk keeps 1 024): two kilobytes are margin and not an
// open door to any page (docs/40, docs/75).
const uint32_t kManifestBytes = 2048;

// **The listing is painted when the radio has finished**, and this function is further down because it is long: what
// `radio_done` needs is the name (docs/30).
void print_listing(const char* path, const char* repo, uint32_t bytes);
void print_manifest(const char* text, size_t len, const char* app, const char* label, const char* where);
// **The install, started by the command and carried on in `radio_done`**: it returns true while steps are left, and
// then the prompt does not come back (docs/30, docs/70).
bool install_step();
bool install_done(bool ok, const char* code);

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
  } else if (job == RadioJob::kInstall) {
    if (install_done(ok, code)) return;  // another download is on its way: the prompt is not due
  } else if (job == RadioJob::kRepository) {
    // **A 200 is the list and everything else is an answer that is not**: the 404 of a repository or of a directory
    // that is not there is said with the number, because looking it up in the documentation of GitHub is what a
    // person would do (docs/30).
    const int32_t status = net::http_status();
    if (ok && status == 200 && g_repo_info) {
      uint32_t len = 0;
      const char* body = net::http_body(len);
      if (body == nullptr || len == 0) {
        error_out("repo: the manifest came empty\r\n");
      } else {
        // **Where the manifest came from, in one string**: the card says it in parentheses and the two callers
        // —the repository and the disk— differ only in that (0.120.0, docs/30).
        char where[128];
        snprintf(where, sizeof(where), "%s/apps/%s", g_repo_name, g_repo_app);
        print_manifest(body, len, g_repo_app, "repo: ", where);
      }
    } else if (ok && status == 200) {
      hal::Stat info;
      uint32_t bytes = 0;
      if (vfs::stat(net::http_saved_path(), info)) bytes = info.size;
      print_listing(net::http_saved_path(), g_repo_name, bytes);
      out("repo: the answer is in ");
      out(net::http_saved_path());
      newline();
    } else if (ok && g_repo_info) {
      error_out("repo: the server answered ");
      out_int(status);
      error_out(", and a manifest is a 200 (is the app name right?)");
      newline();
    } else if (ok) {
      error_out("repo: the server answered ");
      out_int(status);
      error_out(", and a listing is a 200 (is the repository and the directory right?)");
      newline();
    } else if (code != nullptr && strcmp(code, "E_NONET") == 0) {
      error_out("repo: the board has no network to use, try `wifi add`\r\n");
    } else if (code != nullptr && strcmp(code, "E_TLS") == 0) {
      error_out("repo: the certificate was not accepted\r\n");
    } else if (code != nullptr && strcmp(code, "E_IO") == 0) {
      error_out("repo: the answer did not fit where it was going\r\n");
    } else {
      say_failed("repo: nothing came back, ", code);
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

// **The manifest of an app, read from the body that arrived**: the seven keys a manifest has and what each of them
// means. The JSON reader of the system is the same one that reads the manifests of the disk —pull, no tree— so what
// comes out here is what the system would see if the app were on the board (docs/40, docs/75).
//
// **The same card answers for an app of this disk** (0.120.0): `label` and `where` are what the first line says it is
// looking at —`repo: <title> (<repo>/apps/<id>)` for one of the repository and `app: <title> (<door>)` for one that is
// already here—, and nothing else changes, because the manifest is the same kind of file in both places.
void print_manifest(const char* text, size_t len, const char* app, const char* label, const char* where) {
  char title[64] = {0};
  char version[24] = {0};
  char author[48] = {0};
  char entry[48] = {0};
  // **A list of extensions and not a name** (0.120.0): the card shows `opens` as the manifest writes it —the fifteen
  // names of PaperOS's Editor are fifty-five bytes— so the room is what a whole list of the system takes (docs/40).
  char opens[96] = {0};
  char opens_arg[32] = {0};
  char icon[48] = {0};
  char description[256] = {0};
  char boards[8][lunok::json::kStringItemCap];
  uint32_t board_count = 0;
  uint32_t board_total = 0;
  // **A card that shows a dash for a list of nine is a card that lies**: the list is read the same way the board reads
  // it, and when it is longer than this system carries the card says how long it is (docs/30, docs/75).
  const lunok::json::Strings boards_read =
      lunok::json::get_strings(text, len, "boards", boards, 8, board_count, &board_total);

  const bool has_title = lunok::json::get_string(text, len, "title", title, sizeof(title));
  const bool has_entry = lunok::json::get_string(text, len, "entry", entry, sizeof(entry));
  lunok::json::get_string(text, len, "version", version, sizeof(version));
  lunok::json::get_string(text, len, "author", author, sizeof(author));
  lunok::json::get_string(text, len, "opens", opens, sizeof(opens));
  // **The two keys of 0.120.0**: the switches the app wants when it is handed a file and the badge a listing would
  // paint. Both are optional and the card says so with a dash instead of hiding them (docs/30, docs/40).
  lunok::json::get_string(text, len, "opensArgs", opens_arg, sizeof(opens_arg));
  lunok::json::get_string(text, len, "icon", icon, sizeof(icon));
  lunok::json::get_string(text, len, "description", description, sizeof(description));

  good_out(label);
  out((has_title) ? title : app);
  out(" (");
  out(where);
  out(")");
  newline();

  // **One key per line and the name on the left**, which is what makes a card readable: what is not there is said
  // with a long dash and is not hidden (docs/30).
  struct Field {
    const char* name;
    const char* value;
    bool known;
  };
  const Field fields[] = {
      {"title", title, has_title},
      {"version", version, version[0] != '\0'},
      {"author", author, author[0] != '\0'},
      {"entry", entry, has_entry},
      {"opens", opens, opens[0] != '\0'},
      {"opensArgs", opens_arg, opens_arg[0] != '\0'},
      {"boards", nullptr, board_count > 0 || boards_read == lunok::json::Strings::kTooMany},
      {"icon", icon, icon[0] != '\0'},
      {"description", description, description[0] != '\0'},
  };
  for (const Field& field : fields) {
    out("  ");
    out(field.name);
    for (size_t pad = strlen(field.name); pad < 12; ++pad) out(" ");
    if (!field.known) {
      colour(kColourDim);
      out("--");
      colour_off();
    } else if (field.value == nullptr) {
      if (boards_read == lunok::json::Strings::kTooMany) {
        out_num(board_total);
        out(" in the manifest, more than this system carries");
      } else {
        for (uint32_t i = 0; i < board_count; ++i) {
          if (i > 0) out(", ");
          out(boards[i]);
        }
      }
    } else if (strcmp(field.value, "") == 0) {
      out("--");
    } else if (strcmp(field.name, "description") != 0) {
      out(field.value);
    } else {
      // **The description is the only line that does not fit**: it is broken at the space before column 80 and the
      // continuations go under what the field says, which is what makes a card readable (docs/30).
      const size_t kLine = 80;
      size_t used = 12;
      const char* word = field.value;
      bool first = true;
      while (*word != '\0') {
        const char* space = strchr(word, ' ');
        const size_t len = (space != nullptr) ? static_cast<size_t>(space - word) : strlen(word);
        // **The first word goes after what the field says and the others are separated by a space**: counting it
        // wrong glues two words together, and a card that says "Amodal" cannot be read (seen on the console, 4/11).
        const size_t gap = first ? 0 : 1;
        if (!first && used + gap + len > kLine) {
          newline();
          spaces(14);
          used = 14;
        } else if (!first) {
          out(" ");
          ++used;
        }
        out_bytes(word, len);
        used += len;
        first = false;
        word += len;
        if (*word == ' ') ++word;
      }
    }
    newline();
  }
  // **An app without `title` or without `entry` cannot be run by anybody**: the manifest is there and the system
  // refuses it, so the card says it instead of pretending it is an app like the others (docs/40).
  if (!has_title || !has_entry) {
    error_out("  not a usable app: ");
    out(!has_title ? "no title" : "no entry");
    newline();
  }
}

// **`apps info <id>`** (0.120.0): the manifest of an app that is already on the disk, read and shown with the card of
// the repository —one card for the two, because a manifest is the same kind of file wherever it is—. A name the door
// does not have is said as a missing app and not as an empty card, since a card of a file that is not there would be
// the one answer that is a lie (docs/30, docs/40).
bool apps_info(const char* id) {
  char root[96];
  if (id == nullptr || id[0] == '\0' || !apps::root_of(id, root, sizeof(root))) {
    usage_out("usage: apps info <id>\r\n");
    return false;
  }
  char path[128];
  const int made = snprintf(path, sizeof(path), "%s/manifest.json", root);
  if (made <= 0 || static_cast<size_t>(made) >= sizeof(path)) {
    error_out("apps: that name is too long to read a manifest from\r\n");
    return false;
  }
  // **The ceiling of the scan, not one of its own**: 1 024 B is what the reader of the disk keeps, and a card that
  // could show more than the system reads would be showing something the board would not see (docs/40, docs/75).
  // **The buffer is the command's and not the stack's** (0.120.0): a whole manifest is a kilobyte, `apps info` is
  // inlined into `execute` —measured with `-fstack-usage`: 2 272 B became 2 640 B—, and one command runs at a time on
  // the task of the console, so a static here is a kilobyte of RAM instead of a kilobyte of the stack that an app
  // shares with everything else (docs/75).
  static char text[1024];
  size_t len = 0;
  if (!vfs::read_file(path, text, sizeof(text) - 1, len)) {
    error_out("no such app: ");
    out(id);
    newline();
    return false;
  }
  text[len] = '\0';
  print_manifest(text, len, id, "app: ", root);
  return true;
}

// **The listing, in columns and without holding it whole**: one walk of the file to count the folders and know the
// width of the widest one, and another to paint them. What is kept is a row, not the list: the document is tens of
// kilobytes and the board does not have to hold them anywhere (docs/75).
void print_listing(const char* path, const char* repo, uint32_t bytes) {
  char name[48];
  uint32_t count = 0;
  size_t widest = 0;
  {
    // **The listing is not a local**: it is about 2 300 B of window and the board's stack is sixteen kilobytes with the
    // shell, the radio and the install already on it. The two walks of this function take turns on the same object
    // (docs/75).
    repository::Listing& measure = repository::Listing::one();
    if (!measure.open(path)) {
      error_out("repo: the answer is not where it was saved\r\n");
      return;
    }
    while (measure.next(name, sizeof(name))) {
      ++count;
      const size_t len = strlen(name);
      if (len > widest) widest = len;
    }
    measure.close();
  }
  good_out("repo: ");
  out_num(count);
  out(" folders in ");
  out(repo);
  out(" (");
  out_num(bytes);
  out(" B of JSON)");
  newline();
  if (count == 0) {
    out("repo: the directory is empty, or it is not a directory of that repository\r\n");
    return;
  }
  const uint16_t kColumns = 80;
  size_t step = widest + 2;
  if (step < 8) step = 8;
  size_t per_row = static_cast<size_t>(kColumns) / step;
  if (per_row == 0) per_row = 1;
  if (per_row > 4) per_row = 4;
  char row[8 * 50];
  size_t used = 0;
  size_t in_row = 0;
  repository::Listing& listing = repository::Listing::one();
  if (!listing.open(path)) return;
  while (listing.next(name, sizeof(name))) {
    const size_t len = strlen(name);
    if (len + 1 > sizeof(row) - used) break;
    memcpy(row + used, name, len);
    used += len;
    ++in_row;
    const bool last = (in_row == per_row);
    const size_t pad = last ? 0 : (step - len);
    for (size_t i = 0; i < pad && used + 1 < sizeof(row); ++i) row[used++] = ' ';
    if (last) {
      row[used] = '\0';
      out(row);
      newline();
      used = 0;
      in_row = 0;
    }
  }
  if (used > 0) {
    row[used] = '\0';
    out(row);
    newline();
  }
  // **The shared reader does not go back to the prompt with a file open**: the next command asks it for another one
  // and its `open` would close this one anyway, but a handle held for nobody is a handle (docs/70, docs/75).
  listing.close();
}

// repo [owner/repo] [path]: **the folders of a directory of the repository, said by the repository** (4/11).
// The browser page is HTML; what a command wants is the list, and the contents API gives it as JSON. It takes the
// radio the way `http` does and lets it go when it is done, and **the answer is too big for the memory**: it goes to a
// file of the disk and is read in pieces (docs/30, docs/70, docs/75).
// ---- repo install: the whole folder of an app, one download at a time (0.86.0) ----

// **A file the system made goes with the system's own hand**: `vfs::resolve` says which drive it is on and the path
// inside it, and the backend removes it (docs/70).
bool drop_file(const char* path) {
  hal::IStorage* storage = nullptr;
  char rel[128];
  if (!vfs::resolve(path, storage, rel, sizeof(rel)) || storage == nullptr) return false;
  return storage->remove(rel);
}

// **A path the system can carry, or nothing**: `vfs::resolve` refuses a path of a hundred and twenty-eight bytes or
// more (docs/70), and a `snprintf` that does not fit writes half a path, which is a file somewhere nobody asked for. So
// every path the install makes is said whole or the install stops and says why (docs/30, docs/70).
const size_t kPathCap = 128;

bool path_fits(int made, size_t cap) {
  return made > 0 && static_cast<size_t>(made) < cap && static_cast<size_t>(made) < kPathCap;
}

// The path the next file of the listing lands on: the folder the download is being made in, the folder being read, and
// the name (docs/70). False when it does not fit, and then nothing is downloaded to it (docs/30).
bool install_file_path(char* out, size_t cap, const char* name) {
  int made = 0;
  if (g_install_dir[0] == '\0') {
    made = snprintf(out, cap, "%s/%s", g_install_work, name);
  } else {
    made = snprintf(out, cap, "%s/%s/%s", g_install_work, g_install_dir, name);
  }
  return path_fits(made, cap);
}

// **A folder goes with everything inside it**: an install that dies half way leaves the files it already had in its
// folder, and the folder of the download has to be thrown away whole —a `remove` of a folder that is not empty does
// nothing— (docs/70). The tree of an app is two or three levels of what the listing says and the walk is capped there,
// because a folder that is deeper than anything this system makes is not worth a stack.
//
// **What was thrown away is counted** (0.89.0): the same three numbers `repo install` says when it puts an app in, so an
// app that is deleted says how much room came back. A caller that does not care passes nothing.
struct Dropped {
  uint32_t files;
  uint32_t folders;
  uint32_t bytes;
};

void drop_tree(const char* path, uint8_t depth, Dropped* counted = nullptr);

class TreeSink : public hal::ListSink {
 public:
  TreeSink(hal::IStorage* storage, const char* rel, uint8_t depth, Dropped* counted)
      : storage_(storage), depth_(depth), counted_(counted) {
    copy_text(rel_, sizeof(rel_), rel);
  }

  bool on_entry(const hal::ListEntry& entry) override {
    if (entry.name == nullptr) return true;
    char child[128];
    const int made = snprintf(child, sizeof(child), "%s/%s", rel_, entry.name);
    if (made <= 0 || static_cast<size_t>(made) >= sizeof(child)) return true;
    if (counted_ != nullptr) {
      if (entry.type == hal::NodeType::kDir) {
        ++counted_->folders;
      } else {
        ++counted_->files;
        counted_->bytes += entry.size;
      }
    }
    if (entry.type == hal::NodeType::kDir && depth_ < 6) {
      TreeSink deeper(storage_, child, static_cast<uint8_t>(depth_ + 1), counted_);
      storage_->list(child, deeper);
    }
    storage_->remove(child);
    return true;
  }

 private:
  hal::IStorage* storage_;
  char rel_[128];
  uint8_t depth_;
  Dropped* counted_;
};

void drop_tree(const char* path, uint8_t depth, Dropped* counted) {
  hal::IStorage* storage = nullptr;
  char rel[128];
  if (!vfs::resolve(path, storage, rel, sizeof(rel)) || storage == nullptr || depth > 6) return;
  TreeSink sink(storage, rel, depth, counted);
  storage->list(rel, sink);
  storage->remove(rel);
}

// **`apps remove <id>`: the app goes, and only the app** (0.89.0). The id is the name `apps` and `run` show —`squares`
// for the copy of the flash and `squares-SD` for the copy of the card— so one of two copies leaves the disk without the
// other, which is the whole reason the door presents them apart. **Its settings and its data stay** (`/cfg/<id>` and
// `/data/<id>`): those are what the app left behind and not the app, and a person who wants them gone says so with
// another path, not with this one (docs/30, docs/40, docs/70).
bool apps_remove(const char* id) {
  char root[96];
  if (id == nullptr || id[0] == '\0' || !apps::root_of(id, root, sizeof(root))) {
    usage_out("usage: apps remove <id>\r\n");
    return false;
  }
  // **The disk is asked once and kept** (0.89.0): the door answers which of the two copies the name is, and every question
  // after that goes to that copy and not to the door again —a door that is asked twice would answer the card's folder as
  // soon as the flash's has gone, and a delete would report that it failed at the moment it worked— (docs/40, docs/70).
  hal::IStorage* storage = nullptr;
  char rel[128];
  hal::Stat info;
  if (!vfs::resolve(root, storage, rel, sizeof(rel)) || storage == nullptr || !storage->stat(rel, info) ||
      info.type != hal::NodeType::kDir) {
    error_out("no such app: ");
    out(id);
    newline();
    return false;
  }
  // **A folder is not an app because it is in `/apps`** (0.89.0): the manifest is the same question the door asks when it
  // lists one, so a folder of notes that a person keeps next to the apps cannot be deleted by typing its name by mistake
  // (docs/40).
  char manifest[160];
  const int made = snprintf(manifest, sizeof(manifest), "%s/manifest.json", rel);
  if (made <= 0 || static_cast<size_t>(made) >= sizeof(manifest) || !storage->stat(manifest, info) ||
      info.type != hal::NodeType::kFile) {
    error_out("not an app: ");
    out(id);
    out(" has no manifest.json\r\n");
    return false;
  }
  // **Where it was is said with the disk and not with the door**: `/apps/squares-SD` is a name and `/sd/apps/squares` is
  // a place (docs/70).
  char disk[160];
  if (!vfs::on_disk(root, disk, sizeof(disk))) copy_text(disk, sizeof(disk), root);
  Dropped gone{0, 0, 0};
  drop_tree(root, 0, &gone);
  // **A delete that deleted nothing is not a delete**: the folder is asked for again on the disk it came from, because a
  // read-only disk or one that was pulled out answers the same as a success otherwise (docs/30, docs/70).
  if (storage->stat(rel, info)) {
    error_out("apps: ");
    out(id);
    out(" is still there (a read-only disk, or a file the disk would not remove)\r\n");
    return false;
  }
  good_out("apps: ");
  out(id);
  out(" removed from ");
  out(disk);
  out(", ");
  out_num(gone.files);
  out((gone.files == 1) ? " file" : " files");
  if (gone.folders > 0) {
    out(", ");
    out_num(gone.folders);
    out((gone.folders == 1) ? " folder" : " folders");
  }
  out(", ");
  out_num(gone.bytes);
  out(" bytes\r\n");
  return true;
}

// **A folder of the disk moves**, and only inside the same one: an app is not moved from the card to the flash by a
// rename, and a caller that asked for that is asking for the wrong thing (docs/70).
bool path_rename(const char* from, const char* to) {
  hal::IStorage* a = nullptr;
  hal::IStorage* b = nullptr;
  char rel_a[128];
  char rel_b[128];
  if (!vfs::resolve(from, a, rel_a, sizeof(rel_a)) || a == nullptr) return false;
  if (!vfs::resolve(to, b, rel_b, sizeof(rel_b)) || b == nullptr) return false;
  if (a != b) return false;
  return a->rename(rel_a, rel_b);
}

// **The app goes into place in one step** (0.88.14): every file was downloaded into a folder of the system, and the name
// of the app is only taken when the chain has arrived whole. An app that was already there is moved out of the way first
// and put back if the last move fails, so an update that dies leaves the app of before and not a mixture of the two
// (docs/30, docs/70).
bool install_place() {
  hal::Stat info;
  const bool there = vfs::stat(g_install_root, info) && info.type == hal::NodeType::kDir;
  char old[112];
  const int made = snprintf(old, sizeof(old), "%s.old", g_install_root);
  if (made <= 0 || static_cast<size_t>(made) >= sizeof(old)) return false;
  if (there) {
    // A folder of an install that died in the middle of this step is not a wall.
    drop_tree(old, 0);
    if (!path_rename(g_install_root, old)) return false;
  }
  if (!path_rename(g_install_work, g_install_root)) {
    if (there) path_rename(old, g_install_root);
    return false;
  }
  if (there) drop_tree(old, 0);
  return true;
}

// **A folder of the plan, one per line**: the path it has inside the app, so a folder inside a folder is one more line
// and not a second level of a table that never had room for it (docs/30, docs/70).
bool install_plan_add(const char* path) {
  hal::IStorage* storage = nullptr;
  char rel[128];
  if (!vfs::resolve(g_install_plan, storage, rel, sizeof(rel)) || storage == nullptr) return false;
  hal::FileSlot slot;
  if (!storage->open(rel, hal::OpenMode::kAppend, slot)) return false;
  const size_t len = strlen(path);
  bool ok = storage->write(slot, path, static_cast<uint32_t>(len)) == static_cast<int32_t>(len);
  if (ok) ok = storage->write(slot, "\n", 1) == 1;
  storage->close(slot);
  return ok;
}

// **The k-th folder of the plan**: the file is read from its first byte every time, which is one pass over a few bytes
// per folder —ten folders are a hundred and twenty bytes, next to the two kilobytes of the listing it walks— and not a
// table in RAM with a ceiling on it (docs/70, docs/75).
bool install_plan_line(uint16_t index, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return false;
  out[0] = '\0';
  hal::IStorage* storage = nullptr;
  char rel[128];
  if (!vfs::resolve(g_install_plan, storage, rel, sizeof(rel)) || storage == nullptr) return false;
  hal::FileSlot slot;
  if (!storage->open(rel, hal::OpenMode::kRead, slot)) return false;
  char chunk[128];
  uint16_t line = 0;
  size_t used = 0;
  bool found = false;
  int32_t got = 0;
  while (!found && (got = storage->read(slot, chunk, sizeof(chunk))) > 0) {
    for (int32_t i = 0; i < got; ++i) {
      const char c = chunk[i];
      if (c == '\n') {
        if (line == index) {
          found = true;
          break;
        }
        ++line;
        used = 0;
        continue;
      }
      if (line == index && used + 1 < cap) out[used++] = c;
    }
  }
  storage->close(slot);
  out[used] = '\0';
  return found;
}

// **The queue of folders**: the app has as many as it has, and a folder inside a folder is queued with the path it has
// inside the app —which is what its listing is asked for and where its files land— (docs/30, docs/70, docs/75).
bool install_queue_dir(const char* name) {
  if (name[0] == '\0') return true;
  char path[48];
  int made = 0;
  if (g_install_dir[0] == '\0') {
    made = snprintf(path, sizeof(path), "%s", name);
  } else {
    made = snprintf(path, sizeof(path), "%s/%s", g_install_dir, name);
  }
  // **A path that does not fit is said and not cut**: half a path is a folder that is never asked for and an app that
  // arrives without it (docs/30, docs/75).
  if (made <= 0 || static_cast<size_t>(made) >= sizeof(path)) return false;
  if (!install_plan_add(path)) return false;
  ++g_install_folders;
  return true;
}

// The last word, and the state goes back to where it started: the next install is a new one (docs/30).
void install_report(bool ok, const char* why) {
  // **The reader goes back with it**: an install that ends in a failure in the middle of a folder leaves no file of the
  // listing open, and the next command finds the reader as it was (docs/70, docs/75).
  repository::stop();
  g_install_walking = false;
  // **A listing left behind is not left behind**: the folder of the app is the app's and a temporary file of the
  // system in it would be a file its own `fs.list` reads (docs/70).
  if (g_install_list[0] != '\0') drop_file(g_install_list);
  g_install_list[0] = '\0';
  // **The plan goes with it**: the queue of folders is the system's too, and the app's own `fs.list` would read it
  // (docs/70).
  if (g_install_plan[0] != '\0') drop_file(g_install_plan);
  // **An install that installed nothing is not an install**: a 200 that carried no entry is a page that is not a
  // listing, and saying "zero files" with a green face is a lie (docs/30).
  if (ok && g_install_files == 0) {
    ok = false;
    why = "nothing came: is that an app of that repository?";
  }
  // **The app takes its name only now** (0.88.14): every file is in the folder of the work, and what is on `apps/<id>`
  // after this line is either the app whole or nothing of it —a chain that dies at the fifth file of nine throws the
  // folder of the work away and leaves the app of before, if there was one, untouched— (docs/30, docs/70).
  if (ok && g_install_work[0] != '\0' && !install_place()) {
    ok = false;
    why = "the app could not be put in place (a disk that is gone or full)";
  }
  // **What the work folder has of the app goes with the failure**: half an app of files is not a folder a person has to
  // clean up by hand (docs/70).
  if (!ok && g_install_work[0] != '\0') drop_tree(g_install_work, 0);
  if (ok) {
    good_out("repo: ");
    out(g_install_app);
    out(" in ");
    out(g_install_root);
    out(", ");
    out_num(g_install_files);
    out((g_install_files == 1) ? " file" : " files");
    if (g_install_folders > 0) {
      out(", ");
      out_num(g_install_folders);
      out((g_install_folders == 1) ? " folder" : " folders");
    }
    out(", ");
    out_num(g_install_bytes);
    out(" bytes\r\n");
    // **A copy that lands beside one the flash already has is run by its redirected id** (0.164.0): the door presents
    // the flash's copy as `<id>` and the card's as `*<id>` —whose name says the unit, `Editor-SD`—, so a person who
    // installed an app to try a version has to know which name runs what they have just downloaded. The question is the
    // presenter's own —does the flash carry an app of that name?— and not a stat of `/apps/<id>`, which the card would
    // answer too (docs/40, docs/70).
    if (strncmp(g_install_root, "/sd/", 4) == 0 && apps::is_an_app("/littlefs/apps", g_install_app)) {
      colour(kColourDim);
      out("  the flash has a ");
      out(g_install_app);
      out(" too: run *");
      out(g_install_app);
      out(" to run this copy\r\n");
      colour_off();
    }
  } else {
    error_out("repo: ");
    out((why != nullptr) ? why : "the install did not finish");
    newline();
  }
  g_install_app[0] = '\0';
  g_install_dest[0] = '\0';
  g_install_files = 0;
  g_install_bytes = 0;
  g_install_folders = 0;
  g_install_folders_done = 0;
  g_install_dir[0] = '\0';
}
// **Every download of the chain takes the radio again** (0.86.0): `radio_done` lets it go when an answer arrives and
// the chain starts another one, so the work in progress has to be said again —if not, the prompt comes back with a
// download in the air and its answer falls into a job that no longer exists— (docs/30, docs/70).
bool install_fetch(const char* url, const char* dest, bool listing, bool address_in_place) {
  g_install_listing = listing;
  // **The address is copied unless the caller says it is already the one in the state** (0.88.14): two of the three
  // callers build it in `g_install_url` and one hands a buffer of its own, and which one it is is now a word of the
  // call and not a comparison of pointers —a pointer comparison is a rule about the caller's buffer that neither the
  // compiler nor a reader can check— (found in the review of 4/11, docs/75).
  if (!address_in_place) snprintf(g_install_url, sizeof(g_install_url), "%s", url);
  if (!take_radio(RadioJob::kInstall)) {
    install_report(false, "the radio is busy with something else");
    return false;
  }
  if (net::http_download(g_install_url, dest, kHttpMs) != luabind::NetRefuse::kOk) {
    drop_radio();
    install_report(false, "the radio did not take a download of the app");
    return false;
  }
  return true;
}

// **The next step, and the prompt only when none are left**: the next file of the listing, or the listing of the next
// folder, or the summary. True means a download is in the air and the radio will call again (docs/30, docs/70).
bool install_step() {
  while (true) {
    repository::Kind kind = repository::Kind::kFile;
    char name[48];
    char url[192];
    // **The listing is opened once and walked on**: `entry_at` answers an index by reading the document from its first
    // byte, so asking it for the twenty files of an app was twenty opens and twenty passes over the whole listing (44 KB
    // with the sixty apps of that day, 6 KB with the eight of today). The reader is the same object and it only has to be
    // where it left off (0.88.5, docs/75).
    if (!g_install_walking) {
      if (!repository::start(g_install_list)) {
        drop_radio();
        install_report(false, "the answer is not where it was saved");
        return false;
      }
      g_install_walking = true;
    }
    if (!repository::next(kind, name, sizeof(name), url, sizeof(url))) {
      // The folder is read: the reader gives the file back, its listing goes away and the next folder, if there is one,
      // is asked for.
      repository::stop();
      g_install_walking = false;
      drop_file(g_install_list);
      // **The next folder comes out of the plan, which is a file on the disk**: there is no ceiling but the disk, and a
      // folder that the plan says it has and the file does not give is a disk that went away in the middle and not a
      // finished install (docs/70, docs/75).
      if (!install_plan_line(g_install_folders_done, g_install_dir, sizeof(g_install_dir))) {
        if (g_install_folders_done < g_install_folders) {
          drop_radio();
          install_report(false, "the plan of the install is not where it was saved");
          return false;
        }
        install_report(true, nullptr);
        return false;
      }
      ++g_install_folders_done;
      // **The listing of a folder is a call to the API**, and it is what says which files it has (docs/30).
      const int made = snprintf(g_install_url, sizeof(g_install_url), "https://api.github.com/repos/%s/contents/apps/%s/%s",
                                g_install_repo, g_install_app, g_install_dir);
      if (made <= 0 || static_cast<size_t>(made) >= sizeof(g_install_url)) {
        drop_radio();
        install_report(false, "that folder has a name this system does not carry");
        return false;
      }
      // **Where the listing of that folder is saved is a path like any other**: it is said whole or the install stops
      // (docs/30, docs/70).
      const int made_list =
          snprintf(g_install_list, sizeof(g_install_list), "%s/%s/.listing.json", g_install_work, g_install_dir);
      if (!path_fits(made_list, sizeof(g_install_list))) {
        drop_radio();
        install_report(false, "the listing of that folder has a path longer than this system carries");
        return false;
      }
      return install_fetch(g_install_url, g_install_list, true, true);
    }
    if (kind == repository::Kind::kDir) {
      if (!install_queue_dir(name)) {
        drop_radio();
        install_report(false, "a folder of this app has a path this system does not carry");
        return false;
      }
      continue;
    }
    // **A file of the app**: the raw address the listing gives, written where the app lives —the system makes the
    // path and the folders of the path too, which is what a download does (docs/30, docs/70). **And a path that does not
    // fit is said and not cut**: half a path is a download that lands where nobody asked for it (docs/70).
    if (url[0] == '\0') continue;
    if (!install_file_path(g_install_dest, sizeof(g_install_dest), name)) {
      drop_radio();
      install_report(false, "a file of this app has a path longer than this system carries");
      return false;
    }
    return install_fetch(url, g_install_dest, false, false);
  }
}

// **What came back**: the listing of a folder or one of the files. True while there is work left (docs/30, docs/70).
bool install_done(bool ok, const char* code) {
  const int32_t status = net::http_status();
  char why[128];
  if (g_install_listing) {
    if (!ok || status != 200) {
      drop_radio();
      if (ok) {
        snprintf(why, sizeof(why), "the server answered %ld for a folder, and a listing is a 200",
                 static_cast<long>(status));
      } else {
        snprintf(why, sizeof(why), "the folder did not come (%s)", (code != nullptr) ? code : "E_HTTP");
      }
      install_report(false, why);
      return false;
    }
    g_install_pending = true;
    return true;
  }
  if (!ok || status != 200) {
    drop_radio();
    if (ok) {
      snprintf(why, sizeof(why), "the server answered %ld for a file of the app", static_cast<long>(status));
    } else {
      snprintf(why, sizeof(why), "a file of the app did not come (%s)", (code != nullptr) ? code : "E_HTTP");
    }
    install_report(false, why);
    return false;
  }
  hal::Stat info;
  if (vfs::stat(g_install_dest, info)) g_install_bytes += info.size;
  ++g_install_files;
  // **The chain waits for the turn after**: the page that has just arrived is still the board's until the answer
  // returns, and a download asked for from inside is a second page with one pending — `E_BUSY` (docs/30, docs/75).
  g_install_pending = true;
  return true;
}

bool command_repo(const char* argument) {
  char words[3][kWordCap];
  size_t count = 0;
  if (!read_words(argument, words, 3, count)) return false;
  // **`repo install <app>` downloads the whole directory of the app** (0.86.0): the manifest, the code and whatever
  // it carries in `res/`, onto the disk the person chose with `cfg` —the card when there is one with `auto`, the
  // flash when not—. The listing of the API says which files every folder has and the raw address to download them
  // from, and each one is a download of the queue: the command starts the first and folds, and `radio_done` carries
  // on (docs/30, docs/70).
  if (count > 0 && strcmp(words[0], "install") == 0) {
    if (count < 2) {
      usage_out("usage: repo install <app> [owner/repo]\r\n");
      return false;
    }
    const char* app = words[1];
    const char* where = (count > 2) ? words[2] : "lunokjod/lunokOS";
    // The two are what goes into an address and into a path: a space, a quote or a `..` is not a name of either
    // (docs/30, docs/70).
    if (strpbrk(app, " \"/") != nullptr || strstr(app, "..") != nullptr || app[0] == '\0' ||
        strpbrk(where, " \"?") != nullptr || strstr(where, "..") != nullptr) {
      error_out("repo: that is not an app of a repository\r\n");
      return false;
    }
    const char* unit = cfg::unit();
    if (unit == nullptr || unit[0] == '\0') {
      error_out("repo: there is nowhere to install it (neither the flash nor a card)\r\n");
      return false;
    }
    // **The disk `cfg` chose**: the card when there is one and the flash when not, which is what the command says
    // (docs/70). The directory is `apps/<app>` and not another one: it is where the system looks for apps (docs/40).
    const int made_root = snprintf(g_install_root, sizeof(g_install_root), "%s/apps/%s",
                                   (strcmp(unit, "sd") == 0) ? "/sd" : "/littlefs", app);
    // **The folder the download is made in**, next to the app and with a dot in front so a person reading `ls apps`
    // knows it is not an app: the app takes the name of the folder only when it is whole (0.88.14, docs/70).
    const int made_work = snprintf(g_install_work, sizeof(g_install_work), "%s/apps/.%s.install",
                                   (strcmp(unit, "sd") == 0) ? "/sd" : "/littlefs", app);
    copy_text(g_install_app, sizeof(g_install_app), app);
    copy_text(g_install_repo, sizeof(g_install_repo), where);
    g_install_files = 0;
    g_install_bytes = 0;
    g_install_folders = 0;
    g_install_folders_done = 0;
    g_install_dir[0] = '\0';
    g_install_dest[0] = '\0';
    const int made_url = snprintf(g_install_url, sizeof(g_install_url), "https://api.github.com/repos/%s/contents/apps/%s",
                                  where, app);
    const int made_list = snprintf(g_install_list, sizeof(g_install_list), "%s/.listing.json", g_install_work);
    // **The plan of the folders**, next to the listing and for the same reason: inside the folder of the work, so it is
    // the disk `cfg` chose that carries it (docs/70).
    const int made_plan = snprintf(g_install_plan, sizeof(g_install_plan), "%s/.folders", g_install_work);
    if (made_root <= 0 || static_cast<size_t>(made_root) >= sizeof(g_install_root) || made_work <= 0 ||
        static_cast<size_t>(made_work) >= sizeof(g_install_work) || made_url <= 0 ||
        static_cast<size_t>(made_url) >= sizeof(g_install_url) || made_list <= 0 ||
        static_cast<size_t>(made_list) >= sizeof(g_install_list) || made_plan <= 0 ||
        static_cast<size_t>(made_plan) >= sizeof(g_install_plan)) {
      error_out("repo: that app has a name longer than this system carries\r\n");
      return false;
    }
    // **A folder of work left by an install that died is not the folder of this one**: it goes whole before the first
    // file, so what lands there is this chain and nothing of the one before (docs/70).
    drop_tree(g_install_work, 0);
    return install_fetch(g_install_url, g_install_list, true, true);
  }
  // **`repo info <app>` wants the manifest and not the list**: it is a file of one kilobyte and there is no disk
  // needed, so it goes with `http_get` into the memory and `print_manifest` paints it (docs/30, docs/75).
  if (count > 0 && strcmp(words[0], "info") == 0) {
    if (count < 2) {
      usage_out("usage: repo info <app> [owner/repo]\r\n");
      return false;
    }
    const char* app = words[1];
    const char* where = (count > 2) ? words[2] : "lunokjod/lunokOS";
    // **The raw manifest and not the one of the API**: the API gives it with the contents in base64 inside an object,
    // and what is wanted is the file as it is (docs/30).
    char raw[192];
    const int made = snprintf(raw, sizeof(raw), "https://raw.githubusercontent.com/%s/main/apps/%s/manifest.json",
                              where, app);
    if (made <= 0 || static_cast<size_t>(made) >= sizeof(raw) || strpbrk(app, " \"/") != nullptr ||
        strpbrk(where, " \"?") != nullptr) {
      error_out("repo: that is not an app of a repository\r\n");
      return false;
    }
    // **The two names are copied with what fits**: what does not fit is refused by the length of the address above,
    // which is the same as the one of the two words (docs/75).
    copy_text(g_repo_name, sizeof(g_repo_name), where);
    copy_text(g_repo_app, sizeof(g_repo_app), app);
    g_repo_info = true;
    if (!take_radio(RadioJob::kRepository)) return false;
    if (net::http_get(raw, kManifestBytes, kHttpMs) != luabind::NetRefuse::kOk) {
      drop_radio();
      g_repo_info = false;
      error_out("repo: the radio did not take it\r\n");
      return false;
    }
    return true;
  }
  g_repo_info = false;
  const char* repo = (count > 0) ? words[0] : "lunokjod/lunokOS";
  const char* dir = (count > 1) ? words[1] : "apps";
  // The two are what goes into a URL, so a space or a `?` is not a name of either: it is refused here, before
  // an address nobody meant is asked (docs/30).
  char url[192];
  const int written = snprintf(url, sizeof(url), "https://api.github.com/repos/%s/contents/%s", repo, dir);
  if (written <= 0 || static_cast<size_t>(written) >= sizeof(url) ||
      strpbrk(url + 8, " \"") != nullptr) {
    error_out("repo: that is not a repository and a directory\r\n");
    return false;
  }
  if (!page_place(url, g_repo_path, sizeof(g_repo_path))) {
    error_out("repo: there is no volume to hold the answer\r\n");
    return false;
  }
  // The two words are copied and the pair is cut if it would not fit: the line above is the one that says
  // what is being asked, and a name cut is better than a buffer written past its end (docs/75).
  const int named = snprintf(g_repo_name, sizeof(g_repo_name), "%s/%s", repo, dir);
  if (named <= 0) g_repo_name[0] = '\0';
  if (!take_radio(RadioJob::kRepository)) return false;
  const luabind::NetRefuse refused = net::http_download(url, g_repo_path, kHttpMs);
  if (refused != luabind::NetRefuse::kOk) {
    drop_radio();
    if (refused == luabind::NetRefuse::kTooBig) {
      error_out("repo: that address is longer than the ");
      out_num(net::http_url_cap());
      error_out(" bytes the system carries\r\n");
    } else if (refused == luabind::NetRefuse::kNoVolume) {
      error_out("repo: that is not a place this board can write\r\n");
    } else {
      error_out("repo: the radio did not take it\r\n");
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

// **The session of the console closes, and what that means is the console's business** (0.158.0, asked for by the
// owner). A **virtual terminal** —the console of the emulator, a program on a PC that somebody started— is a session
// somebody opened and can hang up, so `exit`, `logout` and Ctrl+D **end it for real**: the line is written and the
// run stops, the same way `poweroff` stops it on the emulator. **A serial port cannot be hung up** —nobody is on the
// other side to close the wire and it is the only way into a board with no glass— so the session is not closed: the
// board says so, stops taking lines, and **the Enter of a terminal opens a new one**, which is what a getty does
// (docs/30, docs/50, docs/85).
//
// **The session of the console and the session of an app are two doors of the same shell** (0.159.0), which is why
// this is `close_console_session` and the other one is `close_session` of `kernel/shell.h`.
bool close_console_session() {
  g_editor.reset();
  pal::IConsole* console = pal::system().console();
  if (console != nullptr && console->virtual_terminal()) {
    out("logout: the session is closed\r\n");
    console->flush();  // the line has to be out before the run stops, or it is a word cut in half
    shutdown();        // on the emulator it is the program that ends, which is the session going away
    return true;
  }

  g_session_open = false;
  out("logout: the session is closed; press Enter to open a new one\r\n");
  if (console != nullptr) console->flush();
  return true;
}

// **Where the words of a detached app go** (0.160.0): `run calculator &` gives the console back to the shell, so the
// app's `print` cannot land on the same wire —two writers on one prompt is the mess `&` exists to avoid— and it goes
// to the log of the system instead, one line each and cut to what a line of the log holds. The app is not silent and
// the prompt is not a mess (docs/30, docs/50).
void console_away(const char* text, size_t len) {
  char line[128];
  const size_t room = sizeof(line) - 1;
  size_t used = (len < room) ? len : room;
  while (used > 0 && (text[used - 1] == '\n' || text[used - 1] == '\r')) --used;
  if (used == 0) return;
  memcpy(line, text, used);
  line[used] = '\0';
  log_printf(pal::kLogInfo, "APP", "%s", line);
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
  // **A closed session does not ask**: the prompt is what says the console is listening, and every caller keeps
  // asking for it —the end of an app, a new line, a command— because it is one place and not ten (docs/30).
  if (!g_session_open) return;
  // **And a session of an app is a pipe and not a terminal**: no prompt and no echo, so what the app reads is what
  // the command printed and nothing else (docs/30).
  if (g_sink != 0) return;
  set_prompt_text(g_lua ? "lua> " : "lunok> ", true);
  g_esc = 0;
  g_escape = Escape::kNone;
}

bool app_running() { return g_app_alive; }

// **The picture of the moment goes up before the board goes**: a restart is a moment a person sees, and the glass it
// leaves is the one the boot paints over (docs/50). A board without the picture is a board that restarts all the same.
// **And it goes up whole** (0.165.1): a restart cut in the middle of the `gc16` of the picture leaves the page of before
// on the glass, which is exactly what a person saw — the picture opened too late and was never seen—. The line is
// written first and the ink is waited on after it, so the wire and the glass say the same thing.
bool reboot(const char* reason) {
  if (reason != nullptr && reason[0] != '\0') {
    log_printf(pal::kLogInfo, "APPS", "%s restarts the board: %s", luabind::app_id(), reason);
  }
  splash::show(splash::kReboot);
  out("restarting...\r\n");
  splash::wait();
  pal::system().reboot();
  return true;
}

bool native_running() { return g_job != RadioJob::kNone || g_install_pending; }

const char* app_id() { return g_app_alive ? luabind::app_id() : ""; }

// **The app another app asked for** (0.153.0): `app.run` writes it and the end of the turn takes it. It is one name and
// not a queue: a launcher starts one app per press, and a second wish in the same turn is refused where it is asked, in
// the words a person reads (docs/30, docs/90).
namespace {
char g_run_wanted[40] = {0};
// **The app to come back to** (0.153.0): the launcher that asked for another one and wants it back when that one ends,
// which is what makes a launcher usable on a board whose only way out of an app is three presses of a key. Empty when
// the app that asked handed the glass over for good (docs/30, docs/90).
char g_run_back[40] = {0};
bool g_run_wanted_back = false;  // whether the app that asked wants the glass back when the other one ends
}  // namespace

bool ask_run(const char* id, bool back, char* why, size_t why_cap) {
  if (id == nullptr || id[0] == '\0') {
    snprintf(why, why_cap, "%s", "an app with no name");
    return false;
  }
  if (strlen(id) >= sizeof(g_run_wanted)) {
    snprintf(why, why_cap, "%s", "a name longer than an app of this system has");
    return false;
  }
  if (!g_app_alive) {
    snprintf(why, why_cap, "%s", "no app is in front to ask for another one");
    return false;
  }
  if (g_run_wanted[0] != '\0') {
    snprintf(why, why_cap, "%s", "an app was asked for already in this turn");
    return false;
  }
  snprintf(g_run_wanted, sizeof(g_run_wanted), "%s", id);
  g_run_wanted_back = back;
  return true;
}

// **The page the person comes back to** (0.153.0): the app that asked for the one that has just stopped being in front is
// started again. It is a function because there are two ways for an app to stop on its own —it ends, or three presses of
// a key ask it to— and a launcher that comes back in one of the two is a launcher that works half the time (docs/90).
bool start_the_one_that_asked() {
  if (g_run_back[0] == '\0') return false;
  char back_to[sizeof(g_run_back)];
  snprintf(back_to, sizeof(back_to), "%s", g_run_back);
  g_run_back[0] = '\0';
  return run_app(back_to, nullptr);
}

bool app_run_wanted(char* out, size_t cap, bool& back) {
  if (g_run_wanted[0] == '\0') return false;
  if (out != nullptr && cap > 0) snprintf(out, cap, "%s", g_run_wanted);
  back = g_run_wanted_back;
  g_run_wanted[0] = '\0';
  g_run_wanted_back = false;
  return true;
}

// **Going back one view, which is what every way out asks for first** (0.163.1): the app gives the page on top back
// and hears `on_back(depth)`, and the answer says whether there was one —true means the app is still in front and the
// caller does not close it—. **It is one function because «go back» has one meaning and three ways of asking for
// it**: the pinch from big to small, the three taps of the middle key, and the three taps on the glass (docs/30).
bool go_back_one_view() {
  if (apps::views() == 0) return false;
  const uint8_t left = apps::view_pop();
  if (!luabind::call_back(left)) {
    log_printf(pal::kLogWarn, "APPS", "%s went back to %u views and has no on_back", app_id(), left);
  }
  return true;
}

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
  // **The views go with the app that had them** (0.163.0): a count that outlived its app would make the next one
  // open with pages on top of its first one, which are pages nobody pushed (docs/30).
  apps::reset_views();
  // **The console is the shell's again** (0.160.0): an app that goes takes its detachment with it, so the prompt
  // that comes after it is a prompt that answers (docs/30).
  g_app_detached = false;
  luabind::set_console_attached(true);
  // **A wish dies with the app that wished it**: an app that asked for another one and was closed by something else
  // —three escapes, a raise, its own end— must not leave the name behind for the next app to be replaced by
  // (0.153.0, docs/30).
  g_run_wanted[0] = '\0';
  g_run_wanted_back = false;
  // **An app that took the whole glass gives it back when it goes** (0.136.0, docs/46 D1): the bar of the system and the
  // view of every app come back here, so the page that comes next —or the console— is not left without its hour, and the
  // system is not left with a view nobody asked for (docs/30).
  ui::give_back_the_glass();
  // The bar goes back to nobody's title: the app that was in front is not any more (docs/30).
  bar::title("");
  out("app closed\r\n");
}

bool restore_app(const char* id, const char* path) { return run_app(id, path, true); }

bool start_app(const char* id) { return run_app(id, nullptr, false); }

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
  boot.args_flags = target.opens_arg;

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
  // **A window is an app too, for the glass** (0.136.0, docs/46 D1): whatever it asked for is given back when it goes,
  // because the page of a window is not a page anybody asked for and the bar is not its (docs/30, docs/60).
  ui::give_back_the_glass();
  return ok;
}

int32_t app_wait_ms() {
  if (!g_app_alive) return -1;
  return luabind::until_next_ms();
}

// **How far the two fingers come back for a pinch to mean «close»** (0.163.0): sixty pixels is the step the paper
// project gave its pinches (`pinchStepPx` of its `touch_manager.h`) and it is a palm's width on this glass, so a hand
// that rests on the page does not close it. **A pinch that comes back less than this is the app's**, and it reads it
// with `touch.gesture()` like any other gesture (docs/30).
const int16_t kPinchClosePx = 60;

// **Whether the app in front is a launcher** (0.163.0): the manifest says `"launcher": true` and that is the app's own
// declaration, so it is read from the door and not guessed from its name; an app started by a path the door does not
// present answers no (docs/40).
static bool app_in_front_is_a_launcher() {
  const char* id = app_id();
  if (id == nullptr || id[0] == '\0') return false;
  apps::Entry entry = {};
  return apps::read(id, entry) && entry.launcher;
}

// **The one road out of a page** (0.167.0): the owner's complaint was that three presses of the middle key closed the app
// and **the launcher did not come back** —the page he came from was lost and the prompt was what he found—, while a pinch
// came home. It is one function now, and all three gestures call it: a page with another one on top gives the top one
// back and stays; a page that is the only one goes with the app and **the page that asked for it takes the glass back**;
// and a launcher is the one page that is not left, because a board with no page at all is a board a person has to reach
// with a cable (docs/30, docs/45).
LeaveOutcome leave_one_page() {
  if (!g_app_alive) return LeaveOutcome::kNothing;
  if (app_in_front_is_a_launcher()) {
    log_printf(pal::kLogInfo, "APPS", "%s is a launcher: it is the one page that is not left", app_id());
    return LeaveOutcome::kLauncher;
  }
  if (apps::views() > 0 && go_back_one_view()) {
    // **The page below is refreshed by the system and not by the app** (0.167.0): giving a view back is a change of page
    // that the system made, so it is the system that owes the paint —the whole page and clean, because the page below
    // was drawn before the one on top of it and its ink is not under the glass any more— (docs/30, docs/50).
    ui::owe_the_page();
    return LeaveOutcome::kBack;
  }
  close_app();
  if (start_the_one_that_asked()) return LeaveOutcome::kClosedHome;
  prompt();
  return LeaveOutcome::kClosed;
}

bool poll_app() {
  // **The next step of an install, in the turn and not inside the answer** (0.86.0): the queue of the radio has to be
  // empty and the page of before given back, and that is the turn after (found on the emulator, 4/11, docs/30, docs/75).
  if (g_install_pending) {
    g_install_pending = false;
    if (!install_step()) prompt();
    return false;
  }
  if (!g_app_alive) return false;
  // **A pinch from big to small closes** (0.163.0): the gesture is the system's because it means «leave this page» in
  // every app, and a gesture every app has to implement is a gesture one of them will not. It is taken out of the
  // queue here so the app never reads it (the pinches that open the fingers, or that come back by less, stay for it).
  // **Three wishes, in this order**: an app with views on top of its first one gives the last of them back and keeps
  // the glass —it hears `on_back(depth)` and puts the page below it back—; an app on its only page is closed, and the
  // page that asked for it comes back the way it does for the three taps; and **a launcher is the one page that cannot
  // be closed**, because a board with no page at all is a board a person has to reach with a cable (docs/30, docs/45).
  {
    touch::Gesture pinch{};
    if (touch::take_pinch_in(kPinchClosePx, pinch)) {
      const LeaveOutcome out = leave_one_page();
      if (out == LeaveOutcome::kBack || out == LeaveOutcome::kLauncher) return true;
      if (out == LeaveOutcome::kClosed || out == LeaveOutcome::kClosedHome) return g_app_alive;
    }
  }
  // **And three taps on the glass are the same way out** (0.167.0): the gesture is taken out of the queue here, as the
  // pinch is, so an app cannot swallow the one gesture that means «leave this page» in every app of this system. It used
  // to be the way back from a page that covers the whole glass and nothing else (docs/30).
  {
    touch::Gesture triple{};
    if (touch::take_gesture_of("triple", triple)) {
      const LeaveOutcome out = leave_one_page();
      if (out == LeaveOutcome::kBack || out == LeaveOutcome::kLauncher) return true;
      if (out == LeaveOutcome::kClosed || out == LeaveOutcome::kClosedHome) return g_app_alive;
    }
  }
  // Three ESC in a row, or a line that says !!exit, are the way out of an app
  // that has stopped listening. The app does not get a say, but it still gets
  // its goodbye hooks (docs/30 and docs/40).
  if (luabind::exit_requested()) {
    // **And a way out goes back before it closes** (0.163.1): the same road as the pinch and the three taps of the
    // middle key, because the wish is «leave this page» and a page with another one on top of it is not the app.
    // The wish is spent on the view it took, or the next turn would close the app anyway (docs/30).
    if (go_back_one_view()) {
      luabind::clear_exit_request();
      return true;
    }
    close_app();
    // **And the page the person came from takes the glass back** (0.153.0): the three presses that close an app are the
    // way out of a launcher's app on a board with no keyboard, so they end where they started and not at a prompt.
    if (start_the_one_that_asked()) return g_app_alive;
    prompt();
    return false;
  }
  if (luabind::poll()) {
    // **The app another app asked for comes up here** (0.153.0): `app.run` is a wish and the end of the turn is where it
    // is carried out, because the app that asked is the one the interpreter belongs to —closing it inside the call
    // would destroy the state the call is running in—. The launcher goes away with its goodbye hooks, which is what
    // `close_app` does, and the one it asked for comes up in its place; a start that fails leaves the prompt, because
    // the app that asked is already gone (docs/30, docs/90).
    char wanted[sizeof(g_run_wanted)];
    bool back = false;
    if (app_run_wanted(wanted, sizeof(wanted), back)) {
      // The app that asked is the one that goes away, and **it leaves its name behind when it wants the glass back**:
      // the wish is set after `close_app`, which clears whatever an app that is going away was holding (0.153.0).
      char asked_by[sizeof(g_run_back)];
      snprintf(asked_by, sizeof(asked_by), "%s", app_id());
      // **A detached app that asks for another one stays detached** (0.160.0): `close_app` gives the console back
      // and the one that comes in its place is the continuation of the same wish and not a new page in front of a
      // prompt somebody is typing at (docs/30).
      const bool was_detached = g_app_detached;
      close_app();
      if (back) snprintf(g_run_back, sizeof(g_run_back), "%s", asked_by);
      g_start_detached = was_detached;
      if (!run_app(wanted, nullptr)) prompt();
      g_start_detached = false;
      return g_app_alive;
    }
    return true;
  }
  const int code = luabind::exit_code();
  const bool died = luabind::failed();
  // **A detached app that ends says so** (0.160.0): the prompt was the shell's the whole time, so a line is the only
  // sign that the app in the background is over, and without it a person keeps waiting for a page that is gone
  // (docs/30).
  const bool was_detached = g_app_detached;
  if (was_detached) {
    char ended[72];
    snprintf(ended, sizeof(ended), "[%s ended in the background]\r\n", app_id());
    out(ended);
  }
  luabind::stop();
  g_app_alive = false;
  // **The console is the shell's again** (0.160.0): the detachment goes with the app that asked for it.
  g_app_detached = false;
  luabind::set_console_attached(true);
  // **The app that asked for this one gets the glass back** (0.153.0): a launcher opens an app and is the page the person
  // comes back to when they close it, which is the whole reason `app.run` remembers who asked. It is started before the
  // report of how this one ended, so the page a person sees is the launcher and not the prompt of a board with no
  // keyboard (docs/30, docs/90).
  if (g_run_back[0] != '\0') {
    ui::give_back_the_glass();
    bar::title("");
    // The page the person comes back to is the one that sent this app to run, so it goes back to wherever it was:
    // detached stays detached (0.160.0).
    g_start_detached = was_detached;
    if (start_the_one_that_asked()) return g_app_alive;
    g_start_detached = false;
    prompt();
    return false;
  }
  // **However the app ended, the view of the system comes back** (0.136.0, docs/46 D1): an app that asked for the whole
  // glass and died —a mistake in its Lua, a reset of the interpreter— would otherwise leave the bar away and the page of
  // a stranger on the glass, with no way back for the person in front of it (docs/30).
  ui::give_back_the_glass();
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
  // **A start is a session** (0.158.0): a board that has just booted has its console open, whatever a session
  // before it did —an emulator that re-boots in a test would otherwise stay closed— (docs/30, docs/85).
  g_session_open = true;
  // **No app is detached at a start** (0.160.0), and the console of an app is the terminal's until one says
  // otherwise: the log is where a detached app's words go, and it is installed here because it is the shell who
  // knows what a console is (docs/30, docs/50).
  g_app_detached = false;
  g_start_detached = false;
  luabind::set_console_attached(true);
  luabind::set_console_away(&console_away);
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

// cfg: the settings of the system and where the settings of the apps are kept. `/cfg` is a door and not a disk, so the
// person has to be able to see which unit is behind it and to move it; the choice is saved in the key-value store and
// survives a reboot. **The keyboard is the other setting** (0.93.0): the id of one of the children of the UI, chosen
// here and read at boot (docs/30, docs/70).
void say_keyboards() {
  out("cfg: keyboards are");
  for (uint8_t i = 0; i < ui::keyboard_count(); ++i) {
    out(" ");
    out(ui::keyboard_at(i));
  }
  newline();
}

// **The two drawings of the logo, with the one that comes up marked** (0.115.0): a person who changes this setting is
// going to restart the board to see it, so the answer says which picture the disk has for each word —a word with no
// picture is a boot that paints nothing and says why, and it is better to know it before the restart than after it
// (docs/30, docs/50).
void say_boot_pictures() {
  out("cfg: boot pictures are\r\n");
  for (uint8_t i = 0; i < splash::count(); ++i) {
    const splash::Kind kind = splash::at(i);
    if (kind != splash::kBoot) continue;
    const char* name = splash::word(kind);
    out((strcmp(cfg::boot_splash(), name) == 0) ? "  * " : "    ");
    out(name);
    char path[96];
    if (splash::picture_of(kind, path, sizeof(path))) {
      out("  ");
      out(path);
    } else {
      out("  no picture");
    }
    newline();
  }
}

bool command_cfg(const char* argument) {
  char words[2][kWordCap];
  size_t count = 0;
  if (!read_words(argument, words, 2, count)) return false;
  if (count > 2) {
    usage_out("usage: cfg [auto|sd|littlefs|keyboard [<id>]|boot [<word>]|home [<id>|none]|tz [<offset>]|"
              "leap [on|off]|week [monday|sunday]|turn [<instructions>]]\r\n");
    return false;
  }

  if (count == 2) {
    // **The app that comes up with the board** (0.119.0): it is checked against the door of the apps, which is what
    // knows whether there is such an app on this board, so a typo is refused here and not discovered at the next
    // restart. `none` is the word that puts the prompt back (docs/30, docs/40).
    if (strcmp(words[0], "home") == 0) {
      if (strcmp(words[1], "none") == 0) {
        const bool kept = cfg::set_home_app(nullptr);
        out("settings: home none (the prompt)\r\n");
        if (!kept) out("cfg: the choice lives until the board restarts (there is no key-value store)\r\n");
        return true;
      }
      apps::Entry entry;
      if (!apps::read(words[1], entry)) {
        out("cfg: no app '");
        out(words[1]);
        out("' that runs on this board; `apps` lists what is there\r\n");
        return false;
      }
      // **The name the door gave back**, which carries the `-SD` suffix when the app is the card's copy: the boot has
      // to start the one the person chose and both halves of the door have the same folder name (docs/40).
      const bool kept = cfg::set_home_app(entry.id);
      out("settings: home ");
      out(cfg::home_app());
      newline();
      if (!kept) out("cfg: the choice lives until the board restarts (there is no key-value store)\r\n");
      return true;
    }
    // **The drawing of the logo that comes up with the board** (0.115.0): the word is checked against the moments
    // the splash carries, so a typo is refused here and not discovered at the next restart (docs/30, docs/50).
    if (strcmp(words[0], "boot") == 0) {
      if (!cfg::is_a_boot_word(words[1])) {
        out("cfg: no boot picture '");
        out(words[1]);
        out("'\r\n");
        say_boot_pictures();
        return false;
      }
      const bool kept = cfg::set_boot_splash(words[1]);
      out("settings: boot ");
      out(cfg::boot_splash());
      newline();
      if (!kept) out("cfg: the choice lives until the board restarts (there is no key-value store)\r\n");
      return true;
    }
    // **The calendar of the person** (0.170.0): the leap day and the first day of the week, the two settings the app of
    // the time zone writes from Lua with the same words —`on`/`off` and `monday`/`sunday`— so that a person and an app
    // never disagree about what a calendar is (docs/30, docs/60).
    if (strcmp(words[0], "leap") == 0 || strcmp(words[0], "week") == 0) {
      bool kept = false;
      if (strcmp(words[0], "leap") == 0) {
        bool on = false;
        if (strcmp(words[1], "on") == 0) {
          on = true;
        } else if (strcmp(words[1], "off") != 0) {
          out("cfg: the leap day is on or off\r\n");
          return false;
        }
        kept = cfg::set_leap_years(on);
        out("settings: leap ");
        out(cfg::leap_years() ? "on" : "off");
        newline();
      } else {
        uint8_t first = 0;
        if (strcmp(words[1], "monday") == 0) {
          first = 1;
        } else if (strcmp(words[1], "sunday") == 0) {
          first = 7;
        } else {
          out("cfg: a week begins on monday or on sunday\r\n");
          return false;
        }
        kept = cfg::set_week_start(first);
        out("settings: week ");
        out((cfg::week_start() == 7) ? "sunday" : "monday");
        newline();
      }
      if (!kept) out("cfg: the choice lives until the board restarts (there is no key-value store)\r\n");
      return true;
    }
    // **The slice of Lua a turn is worth** (0.174.7): the one number that decides how long a finger on the glass and a
    // key of the board wait for the app to give the turn back. It is applied at once —the next turn of the app already
    // uses it— and kept in the store for the next boot, like every other setting of this door (docs/20, docs/75).
    if (strcmp(words[0], "turn") == 0) {
      uint32_t instructions = 0;
      if (!cfg::parse_turn(words[1], instructions)) {
        out("cfg: a turn is ");
        out_num(cfg::turn_default());
        out(" instructions by default, and a slice goes from ");
        out_num(luabind::turn_budget_min());
        out(" to ");
        out_num(luabind::turn_budget_max());
        out("\r\n");
        return false;
      }
      const bool kept = cfg::set_turn_instructions(instructions);
      out("settings: turn ");
      out_num(cfg::turn_instructions());
      out(" instructions per turn\r\n");
      if (!kept) out("cfg: the choice lives until the board restarts (there is no key-value store)\r\n");
      return true;
    }
    if (strcmp(words[0], "keyboard") != 0 && strcmp(words[0], "tz") != 0) {
      usage_out("usage: cfg [auto|sd|littlefs|keyboard [<id>]|boot [<word>]|home [<id>|none]|tz [<offset>]|"
                "leap [on|off]|week [monday|sunday]|turn [<instructions>]]\r\n");
      return false;
    }
    // **The hour of the person** (0.125.0): the chip counts UTC and this is the one number that joins them, because a
    // time clock that says "07:00 to 19:00" has to mean the seven of the person and not the seven of the quartz
    // (docs/30, docs/60). `utc` and `none` put it back to zero, which is a real setting and not a cleared one.
    if (strcmp(words[0], "tz") == 0) {
      int32_t minutes = 0;
      if (strcmp(words[1], "utc") == 0 || strcmp(words[1], "none") == 0) {
        minutes = 0;
      } else if (!cfg::parse_tz(words[1], minutes)) {
        out("cfg: '");
        out(words[1]);
        out("' is not an offset; write +01:00, -03:30 or a number of minutes\r\n");        return false;
      }
      const bool kept = cfg::set_tz_minutes(minutes);
      char text[8] = {0};
      cfg::tz_text(text, sizeof(text), minutes);
      out("settings: tz ");
      out(text);
      newline();
      if (!kept) out("cfg: the choice lives until the board restarts (there is no key-value store)\r\n");
      return true;
    }
    if (!ui::keyboard_select(words[1])) {
      out("cfg: no keyboard '");
      out(words[1]);
      out("'\r\n");
      say_keyboards();
      return false;
    }
    const bool saved = cfg::set_keyboard(ui::keyboard_current());
    out("settings: keyboard ");
    out(ui::keyboard_current());
    newline();
    if (!saved) out("cfg: the choice lives until the board restarts (there is no key-value store)\r\n");
    return true;
  }

  if (count == 1) {
    if (strcmp(words[0], "boot") == 0) {
      say_boot_pictures();
      return true;
    }
    // **The hour the person lives in, and whether it is the chip's own** (0.125.0): the offset is the setting and UTC is
    // what the clock counts, and the line says which of the two the glass is showing (docs/30, docs/60).
    if (strcmp(words[0], "tz") == 0) {
      char text[8] = {0};
      cfg::tz_text(text, sizeof(text), cfg::tz_minutes());
      out("settings: tz ");
      out(text);
      out((cfg::tz_minutes() == 0) ? " (the clock counts UTC)\r\n" : "\r\n");
      return true;
    }
    // **The app that comes up with the board, and what this board's build says** (0.119.0): the two numbers are the
    // setting and its default, because the person wants to know whether they are looking at what they chose or at
    // what the board came with (docs/30, docs/40).
    if (strcmp(words[0], "home") == 0) {
      out("settings: home ");
      out((cfg::home_app()[0] != '\0') ? cfg::home_app() : "none");
      out(" (this board's default is ");
      out((cfg::home_default()[0] != '\0') ? cfg::home_default() : "none");
      out(")\r\n");
      return true;
    }
    if (strcmp(words[0], "keyboard") == 0) {
      out("settings: keyboard ");
      out(ui::keyboard_current());
      newline();
      say_keyboards();
      return true;
    }
    // **The slice of a turn, and the default of this build** (0.174.7): the two numbers a person wants to tell apart,
    // like the home app and its default (docs/20, docs/75).
    if (strcmp(words[0], "turn") == 0) {
      out("settings: turn ");
      out_num(cfg::turn_instructions());
      out(" instructions per turn (this build's default is ");
      out_num(cfg::turn_default());
      out(")\r\n");
      return true;
    }
    // **The calendar, read in the same words it is written** (0.170.0).
    if (strcmp(words[0], "leap") == 0) {
      out("settings: leap ");
      out(cfg::leap_years() ? "on" : "off");
      newline();
      return true;
    }
    if (strcmp(words[0], "week") == 0) {
      out("settings: week ");
      out((cfg::week_start() == 7) ? "sunday" : "monday");
      newline();
      return true;
    }
    if (!cfg::set_unit(words[0])) {
      // The reason is named: a person who asked for the card needs to know whether the
      // word was wrong or the card is missing.
      if (strcmp(words[0], "sd") == 0) {
        out("cfg: there is no card\r\n");
      } else if (strcmp(words[0], "littlefs") == 0) {
        out("cfg: the flash is not mounted\r\n");
      } else {
        out("cfg: auto, sd or littlefs\r\n");
        say_keyboards();
      }
      return false;
    }
  }

  if (cfg::unit()[0] == '\0') {
    out("settings: nowhere (neither the flash nor a card)\r\n");
    out("  keyboard: ");
    out(ui::keyboard_current());
    newline();
    out("  home: ");
    out((cfg::home_app()[0] != '\0') ? cfg::home_app() : "none");
    newline();
    return true;
  }
  out("settings: ");
  out(cfg::unit());
  out(" (asked for ");
  out(cfg::preference());
  out(")\r\n");
  out("  keyboard: ");
  out(ui::keyboard_current());
  newline();
  out("  boot: ");
  out(cfg::boot_splash());
  newline();
  out("  home: ");
  out((cfg::home_app()[0] != '\0') ? cfg::home_app() : "none");
  newline();
  out("  leap: ");
  out(cfg::leap_years() ? "on" : "off");
  newline();
  out("  week: ");
  out((cfg::week_start() == 7) ? "sunday" : "monday");
  newline();
  out("  turn: ");
  out_num(cfg::turn_instructions());
  out(" instructions of Lua\r\n");
  char dir[64];
  if (cfg::dir_for_app(dir, sizeof(dir), "<id>")) {
    out("  an app's: ");
    out(dir);
    newline();
  }
  // **Where the apps that are installed end up**: `repo install` downloads them to `apps/` of the disk above, and the
  // system looks for them on the card first and on the flash after (0.86.0, docs/40, docs/70).
  out("  apps install into: ");
  out((strcmp(cfg::unit(), "sd") == 0) ? "/sd/apps" : "/littlefs/apps");
  newline();
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

  // **A line that ends with `&` asks for the background** (0.160.0): the command starts and the prompt comes back at
  // once, with its three streams —the lines in, the answers out, the complaints out— detached from this console. It
  // is **only an app** that can go there: every other command of this shell is over before a person could type the
  // next one, so a `&` on one of them is said out loud and not obeyed in silence (docs/30).
  bool background = false;
  if (len > 0 && line[len - 1] == '&') {
    line[--len] = '\0';
    while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t')) {
      line[--len] = '\0';
    }
    background = true;
  }
  if (*line == '\0') return true;
  const bool starts_an_app = (strncmp(line, "run", 3) == 0 && (line[3] == '\0' || line[3] == ' ')) ||
                             (strncmp(line, "open", 4) == 0 && (line[4] == '\0' || line[4] == ' '));
  if (background && !starts_an_app) {
    error_out("only an app goes to the background: `run <id> &`\r\n");
    return false;
  }

  // Inside a session the prompt is the interpreter and nothing else: `apps` is a
  // Lua expression there, and the way out is `exit` (docs/30).
  if (g_lua) return lua_line(line);

  // **`exit` and `logout` close the session of the console** (0.158.0, asked for by the owner): they are one verb
  // with two names —a serial session is a session and a person types whichever their habits give them— and what
  // they close is the prompt and not the board. `exit` inside the interpreter is the one of `lua_line`, above: a
  // session of Lua is a smaller thing and it ends before this line is reached (docs/30, docs/50).
  //
  // **And in a session of an app they close that session** (0.159.0): the app asked for a shell and `exit` is how a
  // shell is left, so the door is its own. The block is not given back here —the caller is running inside it— so the
  // line is marked and `session_write` closes it when the command has returned (docs/30).
  if (strcmp(line, "exit") == 0 || strcmp(line, "logout") == 0) {
    if (g_sink != 0) {
      if (Session* session = session_of(g_sink)) session->close_asked = true;
      return true;
    }
    return close_console_session();
  }

  // **`help` with a command is the lines of that command**: `help repo` is the three lines of `repo`, and a word
  // that is the beginning of a command is not the command (0.88.1, docs/30).
  if (strncmp(line, "help", 4) == 0 && (line[4] == '\0' || line[4] == ' ')) {
    // **The words of the question, as they were typed and with one space between them**: `help repo install` asks for
    // the one line of `repo install`, and a run of spaces is not another command (0.88.1, docs/30).
    char asked[64];
    size_t used = 0;
    for (const char* c = line + 4; *c != '\0' && used + 1 < sizeof(asked); ++c) {
      if (*c == ' ') {
        if (used == 0) continue;                  // the air before the first word and after the last one is not a word
        if (asked[used - 1] == ' ') continue;     // a run of spaces is one space
        asked[used++] = ' ';
        continue;
      }
      asked[used++] = *c;
    }
    if (used > 0 && asked[used - 1] == ' ') --used;
    asked[used] = '\0';
    print_help((used > 0) ? asked : nullptr);
    return true;
  }
  if (strcmp(line, "?") == 0) {
    print_help(nullptr);
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
  if (strncmp(line, "apps", 4) == 0 && (line[4] == '\0' || line[4] == ' ')) {
    char words[2][kWordCap];
    size_t count = 0;
    if (!read_words(line + 4, words, 2, count)) return false;
    if (count == 0) {
      print_apps();
      return true;
    }
    // **`info` is the manifest of an app of this disk** (0.120.0): the same card `repo info` shows for one of the
    // repository, with the keys the manifests of PaperOS carry —author, description, icon, opensArgs— (docs/30, docs/40).
    if (strcmp(words[0], "info") == 0 && count == 2) return apps_info(words[1]);
    // **`remove` is a whole word and not a beginning** (0.89.0): `apps remove <id>` deletes the app of that name —the
    // name the listing shows, so `squares` or `squares-SD`— and anything else after `apps` is the usage line instead of a
    // listing that quietly ignored what a person typed (docs/30, docs/40).
    if (strcmp(words[0], "remove") != 0 || count < 2) {
      usage_out("usage: apps [info <id> | remove <id>]\r\n");
      return false;
    }
    return apps_remove(words[1]);
  }
  if (strcmp(line, "lua") == 0) {
    // **The interpreter is the console's and a session cannot take it** (0.159.0): `lua` makes the prompt of the
    // console the interpreter and paints a view of its own, which is not a thing an app's pipe can hold. Said and
    // not swallowed, because a session that asks for it has to know (docs/30).
    if (g_sink != 0) {
      error_out("a session cannot take the interpreter of the console\r\n");
      return false;
    }
    return command_lua();
  }
  if (strcmp(line, "mounts") == 0) {
    print_mounts();
    return true;
  }
  if (strncmp(line, "splash", 6) == 0 && (line[6] == '\0' || line[6] == ' ')) {
    return command_splash(line + 6);
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
    // **`&` is read by `run_app` and not here** (0.160.0): the flag lives for one start, so an app that asks for
    // another one from its own turns is not detached by accident.
    g_start_detached = background;
    const bool started = run_app(words[0], (count > 1) ? words[1] : nullptr);
    g_start_detached = false;
    return started;
  }
  if (strncmp(line, "open", 4) == 0 && (line[4] == '\0' || line[4] == ' ')) {
    g_start_detached = background;
    const bool opened = open_file(line + 4);
    g_start_detached = false;
    return opened;
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
    // **A command that opens a file says which file** (0.164.4; the owner: *«si cat demana <path> perqué hexdump te
    // <path> per defecte?»*): `hexdump` answered with the 7 168 B of `/rtc` and nobody had asked for them, which is the
    // one path a person is most likely to want and the one thing this prompt must not guess. Same line as `cat`
    // (docs/30, docs/70).
    if (count == 0) {
      usage_out("usage: hexdump <path>\r\n");
      return false;
    }
    return hexdump_file(words[0]);
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
    if (!reboot(nullptr)) {
      error_out("reboot: this board cannot restart itself\r\n");
      return false;
    }
    return true;
  }
  // Cutting the power is not a sleep: nothing is armed and the button is the only way back in. The
  // board that can do it does not come back from the call at all, so the only line that can follow
  // is the one of a board that cannot —the ATOMs hold nothing to switch— and the person is told
  // instead of being left in front of a board that did nothing (docs/50).
  if (strcmp(line, "poweroff") == 0) {
    // **The picture of the moment, and then the supply**: the line is written after
    // the wash so that what is left on the glass is the goodbye of the system and not
    // the last page of somebody's app (docs/50).
    splash::show(splash::kPowerOff);
    out("powering off...\r\n");
    pal::IConsole* console = pal::system().console();
    if (console != nullptr) console->flush();  // the line has to be out before the supply goes
    // **And the glass has the goodbye on it before the supply goes** (0.165.1): the picture is a `gc16` of the whole
    // panel and cutting the power in the middle of it leaves the last page of somebody's app (docs/50).
    splash::wait();
    if (!platform().power()->power_off()) {
      error_out("this board cannot cut its own power: it holds nothing to switch off\r\n");
      return false;
    }
    shutdown();  // on the emulator it is a process that ends; on the glass the line below is never read
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
  if (strncmp(line, "repo", 4) == 0 && (line[4] == '\0' || line[4] == ' ')) {
    return command_repo(line + 4);
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
  // **A command that is not there is a complaint** (0.159.0): the session of an app reads it on its own pipe, and a
  // person at the console sees the same three lines as ever (docs/30). One call per line, because a newline outside
  // it would go to the other pipe.
  char complaint[kWordCap + 32];
  snprintf(complaint, sizeof(complaint), "unknown command: %s\r\n", line);
  error_out(complaint);
  error_out("try 'help'.\r\n");
  return false;
}

void feed(const char* data, size_t len) {
  // While an app runs the keyboard is the app's: it echoes, it edits, and it is
  // the one that gets the lines (docs/30). The shell sees nothing of this.
  // **Unless the app is detached** (0.160.0): `run x &` gave the console back to the shell, so the lines are the
  // shell's and the app never sees them (docs/30).
  if (g_app_alive && !g_app_detached) {
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

    // **A closed session opens again only with the Enter of a terminal** (0.158.0): a serial port cannot be hung
    // up, so the session is not closed at all —the board says so and starts again when the person presses Enter—
    // and nothing else counts as a line: a letter typed while it is closed is not the first letter of anything,
    // and an escape is not a key (docs/30, docs/50).
    if (!g_session_open) {
      if (c != '\r' && c != '\n') continue;
      g_session_open = true;
      g_editor.reset();
      newline();
      out("login: a new session\r\n");
      prompt();
      continue;
    }

    // **Ctrl+D is `exit`** (0.158.0): in the interpreter it leaves the session of Lua, which is what it has always
    // done, and at the prompt of the shell it closes the session of the console, because the end of the input of a
    // terminal and the four letters `exit` are the same wish. The line being typed is discarded and not run
    // (docs/30, docs/85).
    if (c == 0x04) {
      g_editor.reset();
      newline();
      if (g_lua) {
        leave_lua();
      } else {
        close_console_session();
      }
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
      // **And a detached app does not own the prompt**: it is the shell's, which is what `&` bought (0.160.0).
      if ((!g_app_alive || g_app_detached) && !g_choosing && !native_running()) prompt();
    } else if (c == 0x7f || c == 0x08) {
      if (g_editor.backspace()) redraw_line();
    } else if (c >= 0x20 && c < 0x7f) {
      g_esc = 0;
      if (g_editor.insert(c)) redraw_line();
    }
  }
}

// ---------------------------------------------------------- the sessions (0.159.0)
//
// **The shell is the system's and not the wire's**: whoever calls `open_session` gets a handle and talks to a shell
// through three pipes —the lines in, the answers out, the complaints out— with no console in the middle, no prompt
// and no echo. The work of the line is the same `execute` the console uses, which is the whole point: one shell, two
// doors (docs/30, docs/75).

uint8_t open_session(char* why, size_t why_cap) {
  for (uint8_t i = 0; i < kMaxSessions; ++i) {
    Session& session = g_sessions[i];
    if (session.in_use) continue;
    session.in_use = true;
    session.line_used = 0;
    session.close_asked = false;
    session.out = Ring();
    session.err = Ring();
    session.out.bytes = static_cast<uint8_t*>(malloc(kSessionOutBytes));
    session.err.bytes = static_cast<uint8_t*>(malloc(kSessionErrBytes));
    if (session.out.bytes == nullptr || session.err.bytes == nullptr) {
      // **A session without its pipes is not half a session**: what was asked for goes back and the caller is told,
      // because an app that got a handle it cannot read from is an app that hangs (docs/30, docs/75).
      free(session.out.bytes);
      free(session.err.bytes);
      reset_session(session);
      if (why != nullptr && why_cap > 0) {
        snprintf(why, why_cap, "no room for the pipes of a session");
      }
      return 0;
    }
    session.out.cap = static_cast<uint16_t>(kSessionOutBytes);
    session.err.cap = static_cast<uint16_t>(kSessionErrBytes);
    return static_cast<uint8_t>(i + 1);  // ids are one based: zero is the console
  }
  if (why != nullptr && why_cap > 0) {
    snprintf(why, why_cap, "there is already a session and one at a time is what this system has");
  }
  return 0;
}

void close_session(uint8_t id) {
  Session* session = session_of(id);
  if (session == nullptr) return;
  free(session->out.bytes);
  free(session->err.bytes);
  reset_session(*session);
}

bool session_open(uint8_t id) { return session_of(id) != nullptr; }

// **One line of a session runs as the console's would**, with the shell's words going to its own pipe: the sink is
// the one number that says it, and it is asked and given back around the call so a command that prints by another
// road still prints to the console (docs/30).
void run_session_line(uint8_t id, Session& session) {
  char line[kSessionLineBytes];
  const uint16_t used = session.line_used;
  memcpy(line, session.line, used);
  line[used] = '\0';
  session.line_used = 0;
  if (used == 0) return;

  const uint8_t was = g_sink;
  g_sink = id;
  execute(line);
  g_sink = was;
  if (session.close_asked) close_session(id);  // `exit` or `logout`, asked by the line itself
}

bool session_write(uint8_t id, const char* data, size_t len) {
  Session* session = session_of(id);
  if (session == nullptr || data == nullptr) return false;
  for (size_t i = 0; i < len; ++i) {
    const char c = data[i];
    // A pipe carries `\n` and a terminal `\r\n`: one Enter and not two (docs/30).
    if (c == '\r') continue;
    if (c == '\n') {
      run_session_line(id, *session);
      // The line may have closed the session, and the pipes with it.
      if (!session_open(id)) return true;
      continue;
    }
    if (static_cast<size_t>(session->line_used) + 1 >= sizeof(session->line)) {
      // **A line that does not fit is said and not grown**: the block of a session is fixed and a name of a command
      // is a hundred and twenty-eight bytes (docs/30, docs/75).
      session->err.put("line too long\r\n", 15);
      session->line_used = 0;
      continue;
    }
    session->line[session->line_used++] = c;
  }
  return true;
}

size_t session_read(uint8_t id, char* out, size_t cap) {
  Session* session = session_of(id);
  return (session == nullptr) ? 0 : session->out.get(out, cap);
}

size_t session_read_error(uint8_t id, char* out, size_t cap) {
  Session* session = session_of(id);
  return (session == nullptr) ? 0 : session->err.get(out, cap);
}

uint32_t session_dropped(uint8_t id) {
  Session* session = session_of(id);
  return (session == nullptr) ? 0 : (session->out.dropped + session->err.dropped);
}

// **The table the interpreter reads** (0.159.0): the kernel is the only one that knows how a session is opened and
// given back, and `luabind` never includes the kernel, so the six calls arrive as six function pointers
// (docs/20, docs/30).
void install_shell_ops() {
  luabind::set_shell_ops(luabind::ShellOps{&open_session, &close_session, &session_write, &session_read,
                                           &session_read_error, &session_dropped});
}

}  // namespace shell
}  // namespace kernel
