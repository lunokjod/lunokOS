// The serial shell: line editing, the command table, and the app console once
// there is an app to run. It is the only user interface there is, because the
// board has no screen (docs/10 and docs/50).
//
// **The sections of this file, in order**:
//   - the console: output, the prompt, the words of a line and the help;
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
#include "kernel/app_error.h"
#include "kernel/apps.h"
#include "kernel/bluetooth.h"
#include "kernel/button.h"
#include "kernel/cfg.h"
#include "kernel/display.h"
#include "kernel/kernel.h"
#include "kernel/led.h"
#include "kernel/line_editor.h"
#include "kernel/log.h"
#include "kernel/mesh.h"
#include "kernel/meter.h"
#include "kernel/lock.h"
#include "kernel/net.h"
#include "kernel/orders.h"
#include "kernel/serial.h"
#include "kernel/usage.h"
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

// **Els colors d'una resposta** (0.190.238): les sis que aquesta casa fa servir per a una taula, i són aquí i no dins de l'anonimat perquè les
// taules que no escriuen pel shell —el `stats`, que va per `meter.cpp`— han de poder dir el mateix. La paleta és la de sempre: el cian clar
// del prompt, el gris del mig del vidre, el verd i el vermell del registre (docs/30, docs/50).
const char* const kStyleTitle = "\033[1;4;38;5;51m";   // bold + underline + the light cyan of the system
const char* const kStyleHead = "\033[1;38;5;51m";      // bold, for the head of a table
const char* const kStyleKey = "\033[38;5;250m";        // what a row of a table is about
const char* const kStyleQuiet = "\033[38;5;244m";      // a unit, a dash, an aside
const char* const kStyleYes = "\033[38;5;41m";
const char* const kStyleNo = "\033[38;5;196m";
const char* const kStyleWarn = "\033[38;5;214m";      // the amber of a warning: the worst call of a meter
const char* const kStyleOn = "\033[0m";

// **The page that asked for the one that is ending** (0.153.0), said here because a start that ends in its own first turn
// asks for it from inside `run_app` (0.179.48) and its body is with the rest of the ways out, further down (docs/30).
bool start_the_one_that_asked();

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
// **Els torns que es donen a una pàgina pujada per una alarma abans de dir-li què ha sonat** (0.190.7): they are turns
// until the app waits on a timer —one or two for every page of this tree— and the ceiling is for a page that never
// settles, which must not stop a start (docs/30, docs/60).
const uint8_t kSettlingTurns = 12;
// A word of a command line is 127 bytes at most, and a path with spaces goes between quotes:
// `cat "res/el meu fitxer.txt"`. There is no escaping, because a quote is a quote and nothing in a file
// system of this size needs more.
const size_t kWordCap = 128;

line::Editor g_editor;
// The prompt is kept because redrawing the line after a cursor move has to put it back: the prompt of
// the `lua` session and the question of the app selector are not the same line (docs/30, docs/85).
char g_prompt[40] = "lunok> ";
size_t g_prompt_len = 7;
// **I quants bytes fa el text del prompt**, codis inclosos: `g_prompt_len` són les lletres que es veuen i això és el que s'escriu (0.190.231).
size_t g_prompt_bytes = 7;
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

// **`log follow` és un mode del prompt** (0.190.185, l'amo: *«Afegeix a log el paràmetre "follow". log follow mostra el log a temps
// real fins que es polsa <Control>+<C> o <esc> (un sol cop)»*): while it lasts the wire is the log's and not the line editor's, which
// is why the flag is here and not in the editor: the prompt has to stop asking for a line and the two stop keys have to be caught
// before the escape parser sees them (docs/30).
bool g_log_follow = false;
// **El color d'aquesta línia** (0.190.226): el que hi ha desat a `cfg/ansi` i el que el terminal contesta, junts. És el que llegeix el
// filtre de la consola, i **no** el que hi ha desat: un monitor que no contesta la sonda deixa la sessió sense codis encara que la
// preferència digui que sí, i això no ha de tocar el que la persona va triar (docs/30, docs/50).
bool g_ansi = true;

// **La sessió del cable tanca sola, i mentre és oberta el xip no dorm** (0.190.184, l'amo: *«La shell del lunokOS te un temporitzador
// d'inactivitat. Si fa 5 minuts que no teclejes res o es reb igna cosa la sessió es tanca […] En aquest moment s'allibera el pany o el que
// sigui perquè idf pugui seguir fent lightsleeps. Si l'usuari vol entrar, polsa <enter> i es torna a obtenir el bloqueig de lightsleep»*).
// Les dues meitats ja hi eren —`close_console_session()` és de la 0.158.0 i una sessió tancada es reobre amb un Enter— i el que faltava era
// **el temps i el pany**: mentre hi ha una sessió oberta en un cable el xip no fa becaines, perquè un son lleuger es menja els caràcters que
// arriben i fa esperar el despertar (l'amo: *«es perden tecles perqué l'esp32 està mig adormida o tornant»*) (docs/30, docs/50, docs/60).
const int64_t kConsoleSessionIdleMs = 300000;  // cinc minuts
int64_t g_last_wire_activity_ms = 0;

// **Només el cable**: `virtual_terminal()` és cert a l'emulador i a la consola del vidre, que guarden els bytes en una cua i no en perden cap;
// i el port sèrie d'una placa no és cap d'aquestes dues coses (docs/50).
bool a_wire_console() {
  pal::IConsole* console = pal::system().console();
  return console != nullptr && !console->virtual_terminal();
}

// El pany, agafat o deixat en un sol lloc, o hi hauria dos camins que el toquen i un que se n'oblidaria.
void sync_the_wire_lock() {
  hal::IPower* power = hal::platform().power();
  if (power == nullptr) return;
  (void)power->hold_no_light_sleep(g_session_open && a_wire_console());
}
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
  // **Mentre el port és cedit a una app, el sistema no hi escriu** (0.189.20): `serial::console()` answers nullptr and
  // every line of the prompt, of an error and of a table goes nowhere. The app's own words do not come through here —they
  // go through the interpreter's console— and that is why the app that has the port keeps it (docs/30, docs/50).
  pal::IConsole* console = serial::console();
  if (console == nullptr) return;
  // **I el filtre, aquí i enlloc més** (0.190.226): aquesta és l'única porta del nucli cap a la consola —la taula, l'avís, el prompt i el
  // que una comanda respon—, o sigui que filtrar aquí vol dir que cap línia s'escapa del color que la sessió no vol. Es fa a trossos i
  // sense reservar res: una línia del registre o un `cat` d'un fitxer gran hi passen igual (docs/30, docs/50, docs/75).
  if (g_ansi) {
    console->write(data, len);
    return;
  }
  const char* in = static_cast<const char*>(data);
  char block[128];
  size_t used = 0;
  size_t at = 0;
  while (at < len) {
    if (static_cast<unsigned char>(in[at]) == 0x1b) {
      at = skip_the_escape(in, len, at);
      continue;
    }
    block[used++] = in[at++];
    if (used < sizeof(block)) continue;
    console->write(block, used);
    used = 0;
  }
  if (used > 0) console->write(block, used);
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

// **Ni color ni detecció** (0.190.92, l'amo: *«esborra la comanda "ansi" i treu els textos ANSI del codi»*): el que escriu aquesta casa
// és text pla, i el mòdul `term` de Lua es queda amb l'amplada i l'alçada (0.190.187). **Des de la 0.190.222 hi ha dues excepcions i són
// del terminal, no del registre**: el prompt amb el seu color i el `clear` que neteja la pantalla, i només quan la consola té **editor de
// línia** —el `console` de la IDF al cable—; sense terminal (l'emulador, la consola del vidre) el que s'escriu és exactament el que hi havia
// abans, lletra a lletra. **I la sonda**: el `␛[6n` que la 0.190.92 va fer treure del codi d'aquesta casa el pregunta ara el component, un
// sol cop i abans d'encendre res, i només si el monitor de l'altre cap és un terminal de debò que hi contesta (docs/30, docs/50, docs/85).

// **Netejar és fer pujar el paper** (0.190.188, l'amo: *«igual que amb "clear" passes unes linies (passa'n mes!) fes el mateix amb
// logout/exit»*): quaranta salts de línia són més que la pantalla de qualsevol terminal —24 és el que aquest sistema promet i 40 el
// que en té un de debò— i el que hi havia a dalt marxa sol, sense cap codi que només entengui un emulador. Una constant i no dos números,
// perquè `clear` i el comiat no poden netejar amb mesures diferents (docs/30, docs/85).
const int kWipeLines = 40;

void wipe_the_paper() {
  // **I amb el terminal a sobre, netejar és netejar** (0.190.222): `clear` pujava quaranta línies perquè el prompt no parlava codis i el
  // paper era l'única manera de fer marxar el que hi havia a dalt. Un terminal que mou el cursor sí que en parla, o sigui que el que es
  // demana és el que demana qualsevol terminal —`ESC [ 2 J` i el cursor a dalt de tot— i la pantalla queda neta de debò. Sense terminal,
  // quaranta salts de línia segueixen essent el camí (docs/30, docs/85).
  // **Netejar amb codis depèn del color i no de l'editor** (0.190.223, i des de la 0.190.226 el color és el commutador): el que ha
  // d'entendre `ESC [ 2 J` és el que hi ha a l'altre cap de la línia, i qui edita la línia no hi té res a veure. **Quan el filtre treu els
  // codis, la pantalla no es pot netejar amb ells** —el filtre se'ls menjaria i el `clear` no faria res—, o sigui que el paper puja amb
  // quaranta salts de línia, que és el que entén tothom (docs/30, docs/50, docs/85).
  if (g_ansi) {
    out("\033[2J\033[H");
    g_visible = 0;
    return;
  }
  for (int i = 0; i < kWipeLines; ++i) out("\r\n");
}

void error_out(const char* text) {
  // **A complaint is the other pipe of a session** (0.159.0): the flag lives for one line and `out_bytes` reads it,
  // so a session sees what went wrong on its own pipe and an app does not have to guess it from the answers
  // (docs/30).
  const bool was = g_to_error;
  g_to_error = true;
  out(text);
  g_to_error = was;
}

void good_out(const char* text) {
  out(text);
}

void usage_out(const char* text) {
  // **A usage line is a complaint and not an answer** (0.159.0): a session has a pipe for each, so what went wrong is
  // asked for where it went, and `execute` answering false is the same thing said to the caller (docs/30).
  const bool was = g_to_error;
  g_to_error = true;
  out(text);
  g_to_error = was;
}

// **El prompt és el text i prou** (0.190.92): el nom en verd i el `>` en gris eren codis d'escapament, i aquesta casa ja no n'escriu cap.
// Els bytes de `g_prompt` sempre van ser el text visible —un redibuix compta caràcters i no codis—, o sigui que el que marxa és només el
// color (docs/30).
void write_prompt() { out_bytes(g_prompt, g_prompt_bytes); }

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

// **Les respostes d'aquesta casa també es poden llegir** (0.190.238, l'amo: *«fes la sortida de les comandes mes agradable amb colors, format de
// text, espais, columnes, negretes, subratllats... help podría ser ben bonic de consultar. O stats. mem necessita que arrodonim els valors a
// bytes,Kilobytes, Megabytes....»*).
//
// **Els codis els porta el codi i qui decideix si surten és la porta única d'escriptura** (`out`): amb `ansi off` el filtre els treu i el que
// queda és el mateix text, alineat igual, sense color. O sigui que una resposta no ha de tenir dues cares: n'hi ha prou que els codis no
// comptin com a caràcters —i no compten, perquè el filtre i el terminal els mengen— (docs/30, docs/85).
//
// **La paleta és la de la casa i no una de nova**: el cian clar (51) del prompt per als títols, el gris del mig del vidre (244) per al que
// no és el que una persona ha vingut a llegir —unitats, guions, avisos—, el blanc de sempre per als valors, i el verd i el vermell que el
// registre ja fa servir per a un sí i un no (41 i 196, `log.cpp`). El subratllat és per als títols i prou: una columna subratllada sencera és
// una regla i la regla ja la fan les columnes (docs/30, docs/50).
// **A title says what the table below is, and it is one line**: a blank line before it, the words in bold and underlined, and nothing
// after it. The blank line is there because a table glued to the command that asked for it reads as if the command had answered with a
// word of its own (docs/30).
void title(const char* text) {
  out("\r\n");
  out(kStyleTitle);
  out(text);
  out(kStyleOn);
  newline();
}

// **A row of a table**: a key of at most eighteen columns, a value, and the key coloured so that the eye can skip the left column. The
// padding is counted on the visible characters —the codes are not letters— and a key longer than the column pushes its value one space to
// the right instead of cutting it (docs/30).
const size_t kRowKeyColumns = 18;
void row(const char* key, const char* value) {
  out("  ");
  out(kStyleKey);
  out(key);
  const size_t len = strlen(key);
  spaces((len < kRowKeyColumns) ? (kRowKeyColumns - len) : 1);
  out(kStyleOn);
  out("  ");
  out(value);
  newline();
}

// **La meitat d'una fila, quan la part de l'esquerra la dibuixa qui crida**: `mounts` ja ha escrit el camí i el tipus amb el seu color, i el
// que queda és alinear el número a la columna de sempre. `label` hi va en el color del que la fila és, i buit vol dir que només hi ha valor
// (0.190.238, docs/30).
void row_inline(const char* label, const char* measure, const char* value) {
  out(kStyleQuiet);
  out(label);
  if (measure[0] != '\0') {
    out(" ");
    out(measure);
  }
  out(kStyleOn);
  out("  ");
  out(value);
}

// **A number of bytes that a person can read** (0.190.238): bytes below a kilobyte, and one decimal above it —kilobytes, megabytes,
// gigabytes— because `917504 B` is a number nobody counts and what the eye wants is `896.0 KB`. **La rodonesa és a la unitat que es diu**:
// el número es parteix en dècimes i s'arrodoneix a la dècima més propera, i el que una persona llegeix és el que hi ha, no el que hi havia
// (docs/30, docs/75).
//
// **Sense comes flotants**: `%f` pot no existir en aquest build (`CONFIG_NEWLIB_NANO_FORMAT`) i el càlcul de les dècimes és una divisió
// entera; un número per a una persona no ha de costar una llibreria de coma flotant (docs/45, docs/75).
void human_bytes(uint64_t bytes, char* out_text, size_t capacity) {
  const uint64_t kilo = 1024ULL;
  const uint64_t mega = 1024ULL * 1024ULL;
  const uint64_t giga = 1024ULL * 1024ULL * 1024ULL;
  if (bytes < kilo) {
    snprintf(out_text, capacity, "%u B", static_cast<unsigned>(bytes));
    return;
  }
  const char* unit = "KB";
  uint64_t scale = kilo;
  if (bytes >= giga) {
    unit = "GB";
    scale = giga;
  } else if (bytes >= mega) {
    unit = "MB";
    scale = mega;
  }
  // Tenths, rounded to the nearest one: `(bytes * 10 + scale / 2) / scale`. With a megabyte of room to spare the multiplication does
  // not overflow a sixty-four bit number (the largest thing this board can count is a card of some gigabytes).
  const uint64_t tenths = (bytes * 10ULL + scale / 2ULL) / scale;
  snprintf(out_text, capacity, "%u.%u %s", static_cast<unsigned>(tenths / 10ULL), static_cast<unsigned>(tenths % 10ULL), unit);
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

// **Quantes lletres es veuen d'un text amb codis** (0.190.231): el prompt pot portar color —i des d'avui el porta també quan l'editor és el
// d'aquesta casa—, i el dibuix de la línia compta caràcters visibles mentre que el que s'escriu és el text sencer. `filter_ansi` no serveix per
// a això perquè respecta el commutador: aquí els codis es treuen sempre, que és el que fa el terminal en pintar-los (docs/30, docs/50).
size_t visible_bytes(const char* text, size_t len) {
  size_t seen = 0;
  size_t at = 0;
  while (at < len) {
    if (static_cast<unsigned char>(text[at]) == 0x1b) {
      at = skip_the_escape(text, len, at);
      continue;
    }
    ++at;
    ++seen;
  }
  return seen;
}

// The prompt that a redraw has to put back. The selector asks its own question and the interpreter says
// `lua>`, so the text is a value and not a call to `prompt()` (docs/30).
void set_prompt_text(const char* text, bool write) {
  size_t len = strlen(text);
  if (len >= sizeof(g_prompt)) len = sizeof(g_prompt) - 1;
  memcpy(g_prompt, text, len);
  g_prompt[len] = '\0';
  // **Els bytes que s'escriuen i les lletres que es veuen són dos números** (0.190.231): el prompt es desa amb els seus codis —és el que
  // s'envia a la línia i el que es dona a l'editor— i `g_prompt_len` és el que compta el cursor del dibuix d'aquesta casa (docs/30).
  g_prompt_bytes = len;
  g_prompt_len = visible_bytes(g_prompt, len);
  // **Qui dibuixa el prompt és qui edita la línia** (0.190.222): amb el terminal del cable, el text se li deixa a ell —que el posa amb el
  // cursor al darrere i amb el color del sistema— i no s'escriu aquí, o n'hi hauria dos i el que escrivís el nucli quedaria a sota de la
  // línia que s'està editant. Sense terminal, aquesta és la línia de sempre (docs/30, docs/50).
  pal::IConsole* console = serial::console();
  if (console != nullptr && console->has_a_line_editor()) {
    console->set_the_prompt(g_prompt);
    g_visible = g_prompt_len;
    return;
  }
  if (write) write_prompt();
  g_visible = g_prompt_len;
}

// **The line is redrawn from the prompt and the cursor is put back where it belongs**, with spaces to wash the old tail and backspaces
// to reach the cursor: that is the picture the shell has always drawn for a terminal that does not speak ANSI, i des de la 0.190.92 és
// l'única que dibuixa. The line is short, so a redraw per key is cheaper than keeping a second copy of the screen (docs/30).
void redraw_line() {
  const size_t len = g_editor.length();
  const size_t cursor = g_editor.cursor();
  const size_t old_line = (g_visible > g_prompt_len) ? g_visible - g_prompt_len : 0;
  out("\r");
  write_prompt();
  out_bytes(g_editor.text(), len);
  const size_t pad = (old_line > len) ? old_line - len : 0;
  spaces(pad);
  const size_t back = len + pad - cursor;
  for (size_t i = 0; i < back; ++i) out("\b");
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

// **La sintaxi d'una comanda, amb les dues coses que una persona ha de saber distingir** (0.190.240, l'amo: *«Decora mes el help, les
// [opcionals] i les <requereides>»*): el nom en el cian de la casa, **el que s'ha d'escriure** (`<app>`, `<path>`) en blanc i negreta, i **el
// que es pot afegir** (`[on|off]`, `[<ms>]`) en el gris del mig del vidre. Els claudàtors es llegeixen un a un perquè n'hi ha de niuats
// —`sessions destroy [<id> [<key>]]`—, i el que mana és el que s'ha obert més a dins: un `<>` dins d'un `[]` és obligatori **si vols
// l'opció**, i pintar-lo gris com el parèntesi seria dir que es pot no escriure (docs/30).
const char* const kStyleRequired = "\033[1;97m";   // bold bright white: what has to be written

void help_syntax_styled(const char* text, size_t len) {
  int optional = 0;
  size_t at = 0;
  while (at < len) {
    const char c = text[at];
    if (c == '[') {
      ++optional;
      out(kStyleQuiet);
      out("[");
      ++at;
      continue;
    }
    if (c == ']') {
      if (optional > 0) --optional;
      out(kStyleQuiet);
      out("]");
      ++at;
      continue;
    }
    if (c == '<') {
      const char* close = static_cast<const char*>(memchr(text + at, '>', len - at));
      const size_t piece = (close != nullptr) ? static_cast<size_t>(close - (text + at)) + 1 : (len - at);
      out(kStyleRequired);
      out_bytes(text + at, piece);
      out(kStyleOn);
      at += piece;
      continue;
    }
    size_t end = at;
    while (end < len && text[end] != '[' && text[end] != ']' && text[end] != '<') ++end;
    out((optional > 0) ? kStyleQuiet : kStyleHead);
    out_bytes(text + at, end - at);
    out(kStyleOn);
    at = end;
  }
}

// **One line of the list, or nothing when the person asked for another command.** The syntax goes in bold and in the colour of the
// system —it is the part a person copies— and the description stays plain, which is what makes a list of ninety lines readable at a
// glance (0.190.238, l'amo: *«help podría ser ben bonic de consultar»*). The advice lines at the end have no syntax and they go quiet:
// they are not a command and they do not have to look like one (docs/30).
void help_line(const char* text, const char* only) {
  const size_t len = help_syntax_len(text);
  g_help_previous = (only == nullptr) || (len > 0 && help_matches(text, only));
  if (!g_help_previous) return;
  ++g_help_written;
  if (len == 0) {
    out(kStyleQuiet);
    out(help_syntax(text));
    out(kStyleOn);
    out("\r\n");
    return;
  }
  out("  ");
  help_syntax_styled(help_syntax(text), len);
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
  // **I un títol quan el que s'ha demanat és la llista sencera**: una persona que ha escrit `help` vol saber què hi ha, i el que hi ha és
  // una taula de comandes amb el nom a l'esquerra i el que fa a la dreta (0.190.238, docs/30).
  if (only == nullptr) title("the commands of this shell");
  help_line("  help [command]    this list, or the lines of one command", only);
  help_line("  version           version and board", only);
  help_line("  caps              what this board has", only);
  help_line("  mem               free memory and buffers", only);
  help_line("  sessions          the memory of a session each app is holding, and where it is", only);
  help_line("  sessions destroy [<id> [<key>]]  forget every session, all of one app, or one of them, in the memory", only);
  help_continuation("                    and on the disk", only);
  help_line("  apps              the apps of the flash and of the card", only);
  help_continuation("                    (with `cfg sort on`, from the most used to the least, with the count of each,", only);
  help_continuation("                    and the ones nobody has opened after them in the door's order)", only);
  help_line("  apps info <id>    the manifest of an app of this disk, key by key", only);
  help_line("  apps remove <id>  delete an app, on the disk it lives on (settings and data stay)", only);
  help_line("  mounts            the mounted file systems and their room", only);
  help_line("  cfg idle [5|10|15]  the quiet before the board sleeps: five minutes (the", only);
  help_continuation("                      default), ten or fifteen. With no alarm, a key wakes it", only);
  help_line("  splash [<moment>|hide|<path>]  a picture on the glass, washed whole (boot,", only);
  help_continuation("                    poweroff, reboot, charging, sleep, or the path of a file)", only);
  help_line("  panel [edge|box]  where a partial paint starts: the edge of the image or the change", only);
  help_line("  cfg [auto|sd|littlefs|keyboard [<id>]|boot [<word>]|home [<id>]]  settings: where the apps' cfg", only);
  help_continuation(
      "                    lives (and `/data`, which a flash does not keep), the keyboard, the picture of the boot and", only);
  help_continuation("                    the app that comes up with the board;", only);
  help_continuation(
      "                    `cfg tz [<offset>]`, `cfg leap [on|off]`, `cfg dst [on|off]`, `cfg week [monday|sunday]` and", only);
  help_continuation("                    `cfg sort [on|off]`, whether the launcher draws the apps by how many times they",
                    only);
  help_continuation("                    have been opened, and `cfg turn [<instructions>]`, the slice of Lua a turn is worth",
                    only);
  help_continuation("                    and `cfg cursor delay [<ms>]`, how long the block of ink lasts before it turns", only);
  help_continuation(
      "                    and `cfg freq [<mhz> on|off|all]`, the floor and the ceiling of the processor", only);
  help_continuation("                    and `cfg settings [<id>|none]`, the app the gear of the panel opens", only);
  help_line("  run <id> [path] [&]  run an app, optionally pointed at a file; `&` leaves it in the background", only);
  help_line("  kill              close the app that is in front of the glass", only);
  help_line("  home              bring the page of the start (cfg home) with no card, and keep the prompt", only);
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
  help_line("  alarm [list]      the queue of alarms, the nearest first, and where the wake is armed", only);
  help_line("  cfg lock [<id>|none]  the page that covers the screen, and `cfg lockon on|off` with `cfg lockafter 5|10|15`", only);
  help_line("  cfg sleep on|off      whether the board sleeps until its next alarm when nobody uses it", only);
  help_line("  cfg touchwake on|off  whether a finger on the glass wakes the board from a deep sleep", only);
  help_line("  cfg nap on|off        whether the waits of the board are spent in a light sleep", only);
  help_line("  mesh              the board as a network of Bluetooth Mesh, and this board is its", only);
  help_line("                    provisioner: it keeps the keys and it hands out the addresses", only);
  help_line("  mesh key          a network of its own with new keys, `mesh key show` which ones, and", only);
  help_line("                    `mesh key <net> <app>` the keys you bring, thirty-two hex digits each", only);
  help_line("  mesh find [s]     who is near and in no network, and `mesh prov <n> [<addr>]` gives one", only);
  help_line("                    of them an address; `mesh nodes` says who is in this network", only);
  help_line("  cfg wifi on|off   the wifi radio, on or off: it is the switch of the card of the bar,", only);
  help_line("                    and turning it on starts the radio if it was off", only);
  help_line("  cfg btmode [keyboard|mouse|both]  what this board is to another device, and it is", only);
  help_line("                    kept: a mouse makes the glass the pointer of the other one", only);
  help_line("  power [cpu [mhz]]  what this board can do about energy", only);
  help_line("  power last        how far the last sleep was from the hour it was given", only);
  help_line("  power idle [on|off]  whether the board may sleep on its own to the next alarm", only);
  help_line("  led [rrggbb|off]  the light of the board", only);
  help_line("  serial [claim|release]  cede the serial port to an app, or take it back: while it is ceded the", only);
  help_continuation("                    system writes nothing here and the log stays in the ring", only);
  help_line("  button            the button, right now", only);
  help_line("  sleep [light|deep] <ms>  sleep, and come back at a time", only);
  help_line("  sleep at <epoch seconds>  the same, at a time of the clock", only);
  help_line("  open [app] <path>  open a file with the app that handles it", only);
  help_line("  ls [path]         list a directory or one file (default /)", only);
  help_line("  cat <path>        print a file", only);
  help_line("  hexdump <path>    the bytes of a file in hex and in visible letters", only);
  help_line("  lua               the prompt becomes the interpreter (`exit` or Ctrl+D leaves)", only);
  help_line("  log               last lines of the log ring", only);
  help_line("  log follow        the log as it is written; a line stops it (Control+C or Escape on a wire", only);
  help_line("                    without a terminal)", only);
  help_line("  stats [reset]     what each device cost: calls, time, last call", only);
  help_line("  verbose on|off    mirror the log to the console too", only);
  help_line("  clear             clear the screen", only);
  help_line("  terminal          ask the other end again whether it is a terminal, and take its cursor, its", only);
  help_line("                    colour and its line editor if it answers", only);
  help_line("  ansi [on|off]     the escape codes of this session: `off` filters them out before the wire,", only);
  help_line("                    and the boot puts it off by itself when the console is not a terminal", only);
  help_line("  reboot            restart", only);
  help_line("  poweroff          cut the power of the board, if it can (docs/50)", only);
  help_line("  deepsleep         sleep with the supply still on, and come back when a key is pressed", only);
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
    return;
  }
  // **I el peu, quan la llista és tota**: quantes comandes hi ha i com es pregunta per una, que és el que una persona que ha arribat fins
  // aquí necessita saber (0.190.238, docs/30).
  if (only == nullptr) {
    out("\r\n");
    out(kStyleQuiet);
    out("  `help <command>` for one of them, and `help` with no word for this list");
    out(kStyleOn);
    newline();
  }
}

void print_version() {
  out("lunokOS ");
  out(kVersion);
  out("  board ");
  out(platform().id());
  // The board and the emulator are both true at once on the PC (docs/85).
  if (platform().emulator()) {
    out(" (emulator)");
  }
  newline();
}

void print_caps() {
  title("what this board has");
  const hal::Caps& caps = platform().caps();
  for (uint32_t i = 0; i < hal::cap_count(); ++i) {
    const bool has = caps.has(static_cast<hal::Capability>(hal::cap_bit(i)));
    // **El sí i el no amb el color del registre** (0.190.238): el verd i el vermell que `log.cpp` fa servir per a un nivell, o sigui que
    // una persona el llegeix sense comptar les lletres (docs/30, docs/50).
    out("  ");
    out(kStyleKey);
    out(hal::cap_name(i));
    const size_t len = strlen(hal::cap_name(i));
    spaces((len < kRowKeyColumns) ? (kRowKeyColumns - len) : 1);
    out(kStyleOn);
    out("  ");
    out(has ? kStyleYes : kStyleNo);
    out(has ? "yes" : "no");
    out(kStyleOn);
    newline();
  }
}

// **`sessions`: les memòries de sessió que hi ha a la PSRAM ara mateix, i les que només són un fitxer al disc** (0.189.7):
// who holds one, under which key, how many bytes, and **the path of the VFS of the cold copy** —which is the difference
// between a cache this board has today and a cache it will still have after a restart, and the thing a person needs to see
// to know that a session was parked— (docs/30, docs/70, docs/75). The rows come from the same table the allocator gives back
// when the memory is short, so what a person reads is what the board is really holding.
// **One row of `sessions` on the glass, and the two numbers its tail needs** (0.189.36): a visitor and not an array of rows,
// because the sessions of this board are however many the apps have asked for and the stack of the task that prints them is
// eight kilobytes —an array of rows was a ceiling dressed as a caller, and a session that did not fit in it was a session a
// person could not see— (docs/30, docs/75).
struct SessionRows {
  uint32_t rows = 0;
  uint32_t in_memory = 0;
};

bool print_one_session(const session::Entry& row, void* user) {
  SessionRows* mine = static_cast<SessionRows*>(user);
  ++mine->rows;
  if (row.in_memory) ++mine->in_memory;
  const char* place = row.in_memory ? (row.on_disk ? "memory+disk" : "memory    ") : "disk only  ";
  // **Les cometes van enganxades al nom** (0.189.16): it was `'%-12s'`, so a key of four letters came out as `'apps        '`
  // and the column that a person reads was padded inside the quotes; the padding is now outside them.
  char quoted[24];
  snprintf(quoted, sizeof(quoted), "'%s'", row.key);
  char line[120];
  snprintf(line, sizeof(line), "  %-14s %-14s %7u B  %s %s\r\n", row.app, quoted, static_cast<unsigned>(row.size), place,
           row.path);
  out(line);
  return true;
}

void print_sessions() {
  title("the memories a session is holding");
  char value[64];
  char held[24];
  human_bytes(session::bytes_held(), held, sizeof held);
  snprintf(value, sizeof value, "%u block(s), %s", static_cast<unsigned>(session::count()), held);
  row("session memory", value);
  SessionRows mine;
  (void)session::each(print_one_session, &mine);
  if (mine.rows == 0) out("  (none: no app is holding a cache)\r\n");
  if (mine.rows > 0 && mine.in_memory < mine.rows) {
    out("  ");
    out_num(mine.rows - mine.in_memory);
    out(" of them are only a file, at ");
    out(session::kDoor);
    out("\r\n");
  }
  // **I quants viuen a la memòria del banc** (0.190.174): two rooms and not one —the PSRAM of the board and the four
  // megabytes behind the chip's switcher—, and a person looking at a board that is short of memory needs to know which one
  // is full (docs/30, docs/75).
  if (session::banked() > 0) {
    out("  ");
    out_num(session::banked());
    out(" of them are in the banked memory\r\n");
  }
}

void print_mem() {
  title("memory");
  char value[64];
  char other[24];
  const pal::HeapInfo heap = pal::system().heap();
  if (heap.valid) {
    human_bytes(heap.free_bytes, value, sizeof value);
    row("free heap", value);
    human_bytes(heap.min_free_bytes, value, sizeof value);
    row("min free heap", value);
    // **What a page could still take**: the free bytes can be in pieces, and a block that does not fit in
    // any of them fails on a heap with room to spare. The board has the number and so does the emulator
    // (docs/75, docs/85).
    human_bytes(heap.largest_free_bytes, value, sizeof value);
    row("largest block", value);
  } else {
    row("heap", "not available on this build");
  }
  // **What is left of the stack of this task**: a page of controls, a font of outlines and the interpreter
  // share it, and an overflow does not say so —it corrupts a backtrace and reboots— so the number is here
  // (docs/75).
  const int32_t stack_left = pal::system().stack_free();
  if (stack_left >= 0) {
    human_bytes(static_cast<uint64_t>(stack_left), value, sizeof value);
    row("stack left", value);
  }
  // The extra memory of the board (the PSRAM of the Paper), with the three numbers that are not the
  // same: what the chip says it has, what the CPU can address, and what is left. It was one number
  // and it read as a bug —`4067 of 8192 KB`— because a pool of 4 MB was counted against an 8 MB chip
  // (docs/75). On a board with none, the line says so and that is not an error.
  const hal::ExtraMemory extra = platform().extra_memory();
  if (extra.chip_bytes > 0) {
    human_bytes(extra.free_bytes, value, sizeof value);
    human_bytes(extra.total_bytes, other, sizeof other);
    snprintf(value + strlen(value), sizeof value - strlen(value), " of %s usable", other);
    row("extra memory", value);
    human_bytes(extra.chip_bytes, other, sizeof other);
    snprintf(value + strlen(value), sizeof value - strlen(value), ", of %s on the chip (psram)", other);
    row("", value);
  } else {
    row("extra memory", "none on this board");
  }
  // **Els megabytes que el xip té darrere el banc de commutació** (0.190.173): the other half of the
  // Paper's and the Koala's 8 MB, which cannot be handed to an app as a pointer —the chip moves them
  // in and out of a window— and for that reason they were not in the line above. `mem himem` proves
  // that they hold what is written there (docs/50, docs/75).
  if (extra.himem_bytes > 0) {
    human_bytes(extra.himem_free_bytes, value, sizeof value);
    human_bytes(extra.himem_bytes, other, sizeof other);
    snprintf(value + strlen(value), sizeof value - strlen(value), " free of %s", other);
    row("himem", value);
    human_bytes(extra.himem_window_bytes, other, sizeof other);
    snprintf(value, sizeof value, "%s of window (bank switching)", other);
    row("", value);
  }
  // The last app, if one has run: what it asked for and what it was allowed.
  if (luabind::limit_bytes() > 0) {
    human_bytes(static_cast<uint64_t>(luabind::peak_bytes()), value, sizeof value);
    human_bytes(static_cast<uint64_t>(luabind::limit_bytes()), other, sizeof other);
    snprintf(value + strlen(value), sizeof value - strlen(value), " of %s allowed", other);
    row("lua peak / limit", value);
  }
  // **The memories of a session an app is keeping** (0.179.0): the first thing the allocator gives back when a block does not
  // fit, so a person looking at a board that is short of memory sees what is being held and by whom it is held (docs/30,
  // docs/75).
  if (session::count() > 0) {
    human_bytes(session::bytes_held(), value, sizeof value);
    snprintf(value + strlen(value), sizeof value - strlen(value), " in %u block(s)", static_cast<unsigned>(session::count()));
    row("session memory", value);
  }
  pal::IQueue* queue = pal::system().queue();
  snprintf(value, sizeof value, "%u of %u", static_cast<unsigned>(queue->size()), static_cast<unsigned>(queue->capacity()));
  row("event queue", value);
  human_bytes(static_cast<uint64_t>(log_bytes()), value, sizeof value);
  row("log ring", value);
  // **I els llibres, en una línia** (0.190.0): `mem blocks` els ensenya un a un; aquí n'hi ha prou de saber que hi són i **què no
  // hi és**, perquè una llista que no és tota ho ha de dir on es mira (docs/75).
  if (pal::memory_slots() == 0) {
    row("memory books", "none on this build");
  } else {
    snprintf(value, sizeof value, "%u block(s) of %u rows, %u ask(s) not written down",
             static_cast<unsigned>(pal::memory_live()), static_cast<unsigned>(pal::memory_slots()),
             static_cast<unsigned>(pal::memory_lost()));
    row("memory books", value);
  }
}

// **La prova del banc de commutació** (0.190.173): un bloc de 32 KB —la unitat que el banc mapeja— escrit i tornat a
// llegir a la memòria que queda darrere el commutador del xip, que és la meitat de la PSRAM de la Paper i del Koala que no
// es pot donar a ningú com un punter. Si el que torna no és el que hi va anar, el que falla és el maquinari i no la comanda
// (docs/30, docs/50, docs/75).
void print_mem_himem() {
  const hal::ExtraMemory extra = platform().extra_memory();
  if (extra.himem_bytes == 0) {
    out("  himem              none on this board: this chip has no bank switcher\r\n");
    return;
  }
  char line[128];
  snprintf(line, sizeof(line),
           "  himem              %u KB behind the bank switcher, %u KB free, %u KB of window\r\n",
           static_cast<unsigned>(extra.himem_bytes / 1024), static_cast<unsigned>(extra.himem_free_bytes / 1024),
           static_cast<unsigned>(extra.himem_window_bytes / 1024));
  out(line);
  out("  32 KB written and read back: ");
  out(platform().extra_memory_round_trip(hal::kExtraMemoryBlockBytes) ? "yes\r\n" : "no\r\n");
}

// **Què és un aparell de Bluetooth, pel que ell mateix declara** (0.190.196, l'amo: *«No em crec que no pesqui ni un sol nom! ni identificar
// el tipus, ni cap info adicional (senyal batería… no sé alguna dada mes)»*). L'anunci porta l'**aparença** —una paraula de setze bits del
// registre del Bluetooth— i els **serveis** que ofereix, i són la resposta a «què és això» quan l'aparell no diu com es diu. Les taules són
// curtes a propòsit: el que no hi és no s'inventa (docs/50, docs/85).
// **La paraula del mode, en una frase** (0.190.204): `bt::device_word()` answers `keyboard`, `mouse` or `both` perquè és el que una
// pàgina llegeix i el que es compara; el que una persona llegeix al prompt és una frase, i «on, a both» no ho és.
const char* device_phrase() {
  const char* word = bt::device_word();
  if (strcmp(word, "mouse") == 0) return "a mouse";
  if (strcmp(word, "both") == 0) return "a keyboard and a mouse";
  return "a keyboard";
}

const char* appearance_name(uint16_t appearance) {
  switch (appearance) {
    case 0x0040: return "phone";
    case 0x0080: return "computer";
    case 0x00C0: return "watch";
    case 0x0140: return "display";
    case 0x0180: return "remote";
    case 0x02C0: return "scanner";
    case 0x0340: return "heart rate";
    case 0x03C1: return "keyboard";
    case 0x03C2: return "mouse";
    case 0x03C3: return "joystick";
    case 0x03C4: return "gamepad";
    case 0x0900: return "pulse oximeter";
    case 0x0940: return "weight scale";
    case 0x0C41: return "television";
    case 0x0C42: return "set-top box";
    default: return nullptr;
  }
}

const char* service_name(uint16_t service) {
  switch (service) {
    case 0x1800: return "access";
    case 0x1801: return "gatt";
    case 0x180A: return "device info";
    case 0x180D: return "heart rate";
    case 0x180F: return "battery";
    case 0x1812: return "HID";
    case 0x181A: return "environment";
    case 0x1108: return "headset";
    case 0x110A: return "audio source";
    case 0x110B: return "audio sink";
    case 0x111E: return "hands-free";
    case 0x111F: return "phone";
    default: return nullptr;
  }
}

// **Els llibres de la memòria** (0.190.0, l'amo: *«podríem fer una llista de punter, tamany i qui l'ha demanat?»*): the blocks of the
// books, **the biggest first**, with the module and the app that asked for each one and the sum per owner —perquè una fuita és un
// nom i un número i no una xifra sola— (docs/75). The two arrays are of the call and not of the system: the shell runs at the prompt,
// where the stack is the only thing that is free, and a person who wants more runs the command again after freeing something.
const uint32_t kShownBlocks = 12;
const uint32_t kShownOwners = 8;

void print_mem_blocks() {
  if (pal::memory_slots() == 0) {
    out("  the books are off on this build: no block is written down\r\n");
    return;
  }
  pal::MemoryBlock rows[kShownBlocks] = {};
  const uint32_t total = pal::memory_blocks(rows, kShownBlocks);
  char line[96];
  snprintf(line, sizeof(line), "  %u block(s) written down in %u rows, %u ask(s) did not fit\r\n", static_cast<unsigned>(total),
           static_cast<unsigned>(pal::memory_slots()), static_cast<unsigned>(pal::memory_lost()));
  out(line);
  pal::MemoryBill bills[kShownOwners] = {};
  const uint32_t owners = pal::memory_bills(bills, kShownOwners);
  if (owners > 0) out("  who                 blocks        bytes\r\n");
  for (uint32_t i = 0; i < owners; ++i) {
    char who[32];
    if (bills[i].app[0] != '\0') {
      snprintf(who, sizeof(who), "%s (%s)", pal::memory_owner_word(static_cast<pal::MemoryOwner>(bills[i].owner)), bills[i].app);
    } else {
      snprintf(who, sizeof(who), "%s", pal::memory_owner_word(static_cast<pal::MemoryOwner>(bills[i].owner)));
    }
    snprintf(line, sizeof(line), "  %-18s %6u  %11u B\r\n", who, static_cast<unsigned>(bills[i].blocks),
             static_cast<unsigned>(bills[i].bytes));
    out(line);
  }
  if (total > 0) out("  the blocks, the biggest first:\r\n");
  for (uint32_t i = 0; i < total && i < kShownBlocks; ++i) {
    char who[32];
    if (rows[i].app[0] != '\0') {
      snprintf(who, sizeof(who), "%s (%s)", pal::memory_owner_word(static_cast<pal::MemoryOwner>(rows[i].owner)), rows[i].app);
    } else {
      snprintf(who, sizeof(who), "%s", pal::memory_owner_word(static_cast<pal::MemoryOwner>(rows[i].owner)));
    }
    snprintf(line, sizeof(line), "  %11u B  %p  %s\r\n", static_cast<unsigned>(rows[i].bytes), rows[i].block, who);
    out(line);
  }
  if (total > kShownBlocks) {
    snprintf(line, sizeof(line), "  ... and %u more; the smallest is %u B\r\n", static_cast<unsigned>(total - kShownBlocks),
             static_cast<unsigned>(rows[kShownBlocks - 1].bytes));
    out(line);
  }
}

// The app list is not stored anywhere: it is walked and printed as it is found,
// one manifest at a time (docs/40).
// **And the count of the uses rides along when the page that counts is read** (0.189.14): `context` is a `PrintApps` or
// nothing, and with it a line says how many times the app has been opened and an app that was already printed in that
// order is not printed twice.
struct PrintApps {
  const usage::Table* uses = nullptr;  // the count of the uses, or nullptr when the listing is not in that order
  bool skip_used = false;              // true for the pass that prints the apps the count does not name
  uint32_t printed = 0;                // the lines that went to the glass, so a board with no app says it
};

bool print_app(const apps::Entry& entry, void* context) {
  PrintApps* print = static_cast<PrintApps*>(context);
  const uint32_t opened = (print != nullptr && print->uses != nullptr) ? usage::count_of(*print->uses, entry.id) : 0;
  if (print != nullptr && print->skip_used && opened > 0) return true;
  const size_t id_len = strlen(entry.id);
  const size_t version_len = strlen(entry.version);
  out("  ");
  out(entry.id);
  spaces((id_len < 16) ? (16 - id_len) : 1);
  out(entry.version);
  spaces((version_len < 10) ? (10 - version_len) : 1);
  out(entry.title);
  // **The app of the card says so** (0.87.1): the same id may sit on the flash and on the card, and the two are presented
  // apart —`<id>` and `<id>-SD`— so a person who is trying a version can read which of the two copies each line is
  // (0.88.20, docs/40, docs/70).
  if (entry.on_card) {
    out("  [card]");
  }
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
  // **Why this line is above the one under it** (0.189.14): the listing is ordered by how many times an app has been
  // opened, and a list in an order nobody can see the reason of is a list that looks shuffled. The number is only said
  // when the person asked for that order with `cfg sort on` (docs/30, docs/40).
  if (opened > 0) {
    out("  [");
    out_num(opened);
    out(opened == 1 ? " use]" : " uses]");
  }
  newline();
  if (print != nullptr) ++print->printed;
  return true;
}

// **The apps of the disk, in the order the person asked for** (0.189.14): with `cfg sort on` the list goes from the app
// that has been opened most to the least and then the ones nobody has opened yet, and without it the list is the door's,
// which is what it always was (docs/30, docs/40, docs/75).
//
// **The counted apps are read one by one** (`apps::read`) and the rest by a single walk, which is what keeps the listing
// what it was: no table of apps in memory, no ceiling on how many there may be, and the same walk as before for the ones
// the count does not name.
void print_apps() {
  usage::Table uses;
  const uint8_t counted = cfg::sort_by_usage() ? usage::read(uses) : 0;

  PrintApps print;
  if (counted > 0) {
    print.uses = &uses;
    // **From the most used to the least**, and an id the disk no longer has is skipped: the count of an app that was
    // removed stays in the block, because the day it comes back its number comes with it (docs/40).
    for (uint8_t i = 0; i < uses.rows; ++i) {
      if (uses.count[i] == 0) continue;
      apps::Entry entry;
      if (!apps::read(uses.ids[i], entry)) continue;
      print_app(entry, &print);
    }
    // **And the ones that have never been opened come after**, in the door's order: an app that has just been installed
    // has no count yet and it belongs on the list, after the ones the person uses (docs/40).
    print.skip_used = true;
  }

  (void)apps::for_each(nullptr, &print_app, &print);
  if (print.printed == 0) out("  no apps\r\n");
}


// **`panel`**: which rectangle a partial paint asks the glass for, and whether it takes one that does not start at the
// edge of its own image (0.179.12). The rule of the 0.118.7 costs the pixels between that edge and the change on every
// partial paint —**the whole height of the page for a cell at the bottom of a launcher**— and it was decided on a night
// where the panel was never selected again after the splash (0.118.9), so it is a question a person can put to the
// glass and read in the log: `paint 24,60 108x242` instead of `paint 0,274 168x242` (docs/30, docs/50).
bool command_panel(const char* rest) {
  char both[2][kWordCap];
  size_t many = 0;
  if (!read_words(rest, both, 2, many)) return false;
  if (many == 0) {
    if (!display::available()) {
      out("panel: no glass on this board\r\n");
      return true;
    }
    out(display::partial_from_the_box() ? "panel: a partial paint starts at the box of the change\r\n"
                                        : "panel: a partial paint starts at the left edge of the image\r\n");
    out("panel: the pixels go out as ");
    out(display::flush_style_name(display::flush_style()));
    out("\r\n");
    out("panel [edge|box|bus [mhz]] | panel bench | panel wave [name] | panel flush [rows|burst|2bit]\r\n");
    return true;
  }
  // **`panel bars`** (0.190.45): the sixteen levels of the glass, drawn as sixteen vertical bars, black on the left and white
  // on the right. **No és una comanda per a una persona que llegeix un manual: és la mesura** —a panel whose mid greys are
  // missing looks like a handful of wide bars, i el que diu si el driver, la forma d'ona o la VCOM són on toca és comptar
  // quants esglaons es veuen— (docs/50, docs/95).
  if (strcmp(both[0], "bars") == 0) {
    hal::ISurface* glass = display::surface();
    if (glass == nullptr) {
      out("panel: this board has no glass\r\n");
      return true;
    }
    const uint16_t wide = glass->width();
    const uint16_t tall = glass->height();
    const uint16_t each = static_cast<uint16_t>(wide / 16u);
    for (uint16_t i = 0; i < 16; ++i) {
      glass->fill_rect(static_cast<uint16_t>(i * each), 0, (i == 15) ? static_cast<uint16_t>(wide - i * each) : each, tall,
                       static_cast<uint8_t>(i));
    }
    if (!display::present(hal::UpdateMode::kGc16)) {
      out("panel: the glass did not take the bars\r\n");
      return false;
    }
    out("panel: sixteen bars, black on the left\r\n");
    return true;
  }
  // **`panel wave`** (0.190.134 → 0.190.136, l'amo: *«pots canviar `epdiy_ED047TC2.h` i si provem la C1? Els colors a la C2 no semblen
  // correctes del tot... els 2,4 i 8 colors es veuen bé, pero els 16 no tant»*, i després *«podries afegir tots els waveforms?»* i *«que al
  // posar "panel wave" digui l'actual i doni la llista»*). Sense argument diu quina es fa servir i **llista les vuit que el build porta**, amb
  // els modes i els rangs de temperatura de cada taula; amb un nom la canvia. El que decideix quina es fa servir és un punter de l'estat
  // d'epdiy, o sigui que el canvi és en calent i la pintada següent consulta la taula nova (docs/50, docs/95).
  if (strcmp(both[0], "wave") == 0) {
    if (many < 2) {
      out("panel: the waveform is ");
      out(display::waveform());
      out("\r\n");
      for (uint8_t i = 0;; ++i) {
        uint8_t modes = 0;
        uint8_t ranges = 0;
        const char* name = display::waveform_at(i, &modes, &ranges);
        if (name == nullptr) break;
        char said[96];
        snprintf(said, sizeof(said), "%s %-9s %u mode(s), %u temperature range(s)\r\n",
                 (strcmp(name, display::waveform()) == 0) ? "  *" : "   ", name, static_cast<unsigned>(modes),
                 static_cast<unsigned>(ranges));
        out(said);
      }
      out("panel wave <name>\r\n");
      return true;
    }
    if (!display::set_waveform(both[1])) {
      out("panel: this build does not carry that waveform\r\n");
      return false;
    }
    out("panel: the waveform is ");
    out(display::waveform());
    out("\r\n");
    return true;
  }
  // **`panel bench`** (0.190.123, l'amo: *«Vull que facis possible això dels 60hz»*). Tres qualitats i tres àrees, i el que costa cada una de debò:
  // el que triga una pintada d'aquest panell és **l'àrea i el mode** —la forma d'ona d'aquest vidre té vint-i-dues fases pel `du` i trenta-vuit pel
  // `gc16`— o sigui que el número que decideix si una animació és possible és aquest, i no un de memòria. La pintada de mesura **gira l'àrea** perquè
  // epdiy hi vegi un canvi de debò i la gira un nombre parell de vegades, o sigui que la imatge acaba on era; el primer cop es descarta perquè no és el
  // règim, i es dona la mitjana de tres. L'últim número és el sostre d'imatges per segon que aquell mode permet a aquella àrea (docs/50, docs/95).
  if (strcmp(both[0], "bench") == 0) {
    hal::ISurface* glass = display::surface();
    if (glass == nullptr) {
      out("panel: this board has no glass\r\n");
      return true;
    }
    // **Les mides del vidre i no les de la pàgina** (0.190.124): the page of this glass is 540×960 and the glass is 960×540, o sigui que preguntar-ho
    // amb `width()` demanava una àrea que no hi cap i la pàgina sencera es descartava en silenci —que és el que va passar la primera vegada— (docs/50).
    const uint16_t wide = glass->glass_width();
    const uint16_t tall = glass->glass_height();
    struct Shape {
      const char* name;
      uint16_t x;
      uint16_t y;
      uint16_t w;
      uint16_t h;
    };
    // **Les tres àrees que aquesta casa pinta de debò**: la pàgina sencera, la franja de la barra i una cel·la del llançador amb el seu nom (docs/50).
    const Shape shapes[3] = {
        {"the whole page", 0, 0, wide, tall},
        {"a strip of 48 rows", 0, 0, wide, static_cast<uint16_t>((tall > 48) ? 48 : tall)},
        {"a cell of 120x80", 0, 0, static_cast<uint16_t>((wide > 120) ? 120 : wide),
         static_cast<uint16_t>((tall > 80) ? 80 : tall)},
    };
    const hal::UpdateMode modes[3] = {hal::UpdateMode::kDu, hal::UpdateMode::kGc16, hal::UpdateMode::kGl16};
    {
      char said[96];
      snprintf(said, sizeof(said), "panel: counting at %u MHz, and the glass flips while it does (it ends where it started)\r\n",
               static_cast<unsigned>(display::bus_mhz()));
      out(said);
    }
    // **I el processador al cim mentre dura** (0.190.138, el registre de l'amo: `error flag is set: 400` i `line buffer underrun occurred!`): the
    // render of epdiy prepares the lines in two tasks while the DMA eats them, o sigui que **necessita el processador sencer**: a vuitanta
    // megahertz les cues s'esgoten i la pintada surt a mitges. El nucli ja ho demana quan pinta una pàgina; una comanda que pinta trenta-sis cops
    // sense passar pel nucli ho ha de demanar ella (docs/50, docs/95).
    power::hold_cpu(power::kBusyMhz, 0, "a bench of the panel");
    bool any = false;
    for (const hal::UpdateMode mode : modes) {
      for (const Shape& shape : shapes) {
        uint32_t most = 0;
        uint32_t least = 0xFFFFFFFFu;
        uint32_t total = 0;
        bool took = true;
        // **Un nombre parell de pintades deixa el vidre com era** (0.190.124): cada crida gira l'àrea i la següent la desgira —és el seu propi
        // desfer—, o sigui que una de més i la imatge que mira la persona es queda girada fins que una altra cosa la repinta (docs/50).
        int calls = 0;
        int measures = 0;
        for (int turn = 0; turn < 4; ++turn) {
          const uint32_t took_ms =
              glass->paint_for_the_count(shape.x, shape.y, shape.w, shape.h, mode);
          if (took_ms == 0) break;
          ++calls;
          if (turn == 0) continue;  // the first one is the rails coming up, not the paint
          if (took_ms > most) most = took_ms;
          if (took_ms < least) least = took_ms;
          total += took_ms;
          ++measures;
        }
        if (calls == 0) continue;
        if ((calls % 2) != 0) {
          // **I si el compte ha quedat senar, es desgira** (0.190.124): count is not a measurement, it is the way back (docs/50).
          (void)glass->paint_for_the_count(shape.x, shape.y, shape.w, shape.h, mode);
          took = false;
        }
        if (!took) continue;
        any = true;
        const uint32_t mean = (measures > 0) ? (total / static_cast<uint32_t>(measures)) : 0;
        char said[128];
        snprintf(said, sizeof(said), "panel: %-4s %-18s %ux%u: %u ms (from %u to %u), %u a second at best\r\n",
                 hal::mode_name(mode), shape.name, static_cast<unsigned>(shape.w), static_cast<unsigned>(shape.h),
                 static_cast<unsigned>(mean), static_cast<unsigned>(least), static_cast<unsigned>(most),
                 static_cast<unsigned>((mean == 0) ? 0 : (1000u / mean)));
        out(said);
      }
    }
    power::rest_cpu("the bench of the panel is over");
    if (!any) out("panel: this glass cannot be told to paint for a count\r\n");
    return true;
  }
  // **`panel flush`** (0.189.66): three ways of sending the same picture, and the one that wins is the one measured on this
  // board. Nothing is remembered across a boot yet: the style lives as long as the run, because a setting that survives and
  // makes every page wrong is a board that has to be reflashed to come back (docs/50).
  if (strcmp(both[0], "flush") == 0) {
    if (many < 2) {
      out("panel: a picture goes out as ");
      out(display::flush_style_name(display::flush_style()));
      out("\r\n");
      return true;
    }
    hal::FlushStyle wanted = hal::FlushStyle::kRows;
    if (!display::flush_style_of(both[1], wanted)) {
      usage_out("usage: panel flush [rows|burst|2bit]\r\n");
      return false;
    }
    if (!display::set_flush_style(wanted)) {
      out("panel: this glass does not have that way\r\n");
      return false;
    }
    out("panel: ");
    out(display::flush_style_name(display::flush_style()));
    out("\r\n");
    return true;
  }
  // **`panel gate <us>`** (0.190.57): the time the gate of a line is open, which is what cuts the road from white to black into
  // steps and therefore decides how many greys the glass separates. **Es demana en microsegons i s'admet la coma** (`panel gate
  // 0.9`) perquè el pas d'aquest vidre és més fi que un microsegon; endins es compta en nanosegons. Canviar-ho és una pintada de
  // prova, i un flaish per valor és una sessió per valor (docs/50, docs/95).
  if (strcmp(both[0], "gate") == 0) {
    if (many < 2) {
      if (display::gate_ns() == 0) {
        out("panel: this glass has no gate of its own\r\n");
        return true;
      }
      char said[64];
      snprintf(said, sizeof(said), "panel: the gate is open %u ns per line\r\n",
               static_cast<unsigned>(display::gate_ns()));
      out(said);
      return true;
    }
    const uint32_t whole = static_cast<uint32_t>(atoi(both[1]));
    uint32_t frac = 0;
    const char* dot = strchr(both[1], '.');
    if (dot != nullptr) {
      uint32_t scale = 100;
      for (const char* p = dot + 1; *p >= '0' && *p <= '9' && scale > 0; p++, scale /= 10) {
        frac += static_cast<uint32_t>(*p - '0') * scale;
      }
    }
    if (!display::set_gate_ns(whole * 1000u + frac * 10u)) {
      out("panel: the glass did not take that gate\r\n");
      return false;
    }
    char said[64];
    snprintf(said, sizeof(said), "panel: the gate is open %u ns per line\r\n",
             static_cast<unsigned>(display::gate_ns()));
    out(said);
    return true;
  }
  // **`panel vcom [mv]`** (0.190.60): the common voltage of the ink in millivolts, which is the only electrical number a driver can
  // turn and the one the factory firmware lets a person calibrate. Un valor heretat d'una altra unitat és una pàgina cremada: si la
  // tinta satura de seguida, aquest és el número que s'ha de moure (docs/50, docs/95).
  if (strcmp(both[0], "vcom") == 0) {
    if (many < 2) {
      if (display::vcom_mv() == 0) {
        out("panel: this glass has no VCOM of its own\r\n");
        return true;
      }
      char said[56];
      snprintf(said, sizeof(said), "panel: VCOM is %u mV\r\n", static_cast<unsigned>(display::vcom_mv()));
      out(said);
      return true;
    }
    const uint32_t mv = static_cast<uint32_t>(atoi(both[1]));
    if (!display::set_vcom_mv(static_cast<uint16_t>(mv))) {
      out("panel: the supply did not take that VCOM\r\n");
      return false;
    }
    char said[56];
    snprintf(said, sizeof(said), "panel: VCOM is %u mV\r\n", static_cast<unsigned>(display::vcom_mv()));
    out(said);
    return true;
  }
  // **`panel bus <mhz>`** (0.189.77): the clock of the bus the pixels travel on, which is the last piece of a paint that is
  // still worth moving. It takes the place of a flash and a session per speed.
  if (strcmp(both[0], "bus") == 0) {
    if (many < 2) {
      if (display::bus_mhz() == 0) {
        out("panel: this glass has no bus of its own\r\n");
        return true;
      }
      char said[48];
      snprintf(said, sizeof(said), "panel: the pixels travel at %u MHz\r\n",
               static_cast<unsigned>(display::bus_mhz()));
      out(said);
      return true;
    }
    const uint32_t mhz = static_cast<uint32_t>(atoi(both[1]));
    if (!display::set_bus_mhz(mhz)) {
      out("panel: the bus did not take that clock\r\n");
      return false;
    }
    char said[48];
    snprintf(said, sizeof(said), "panel: %u MHz\r\n", static_cast<unsigned>(display::bus_mhz()));
    out(said);
    return true;
  }
  if (strcmp(both[0], "edge") != 0 && strcmp(both[0], "box") != 0) {
    usage_out("usage: panel [edge|box|bars|bench|wave [name]|vcom [mv]] | panel flush [rows|burst|2bit]\r\n");
    return false;
  }
  if (!display::set_partial_from_the_box(strcmp(both[0], "box") == 0)) {
    out("panel: this glass does not have the choice\r\n");
    return false;
  }
  out(strcmp(both[0], "box") == 0 ? "panel: box\r\n" : "panel: edge\r\n");
  return true;
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
    // **A build with no pictures of the moments has no glass to paint them on** (0.179.9): it says so instead of a usage
    // line that reads as a command that forgot its arguments (`LUNOK_GLASS`, docs/30, docs/50).
    if (moments == 0) {
      out("splash: no glass on this board\r\n");
      return true;
    }
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
  title("the file systems of this board");
  const uint32_t count = vfs::mount_count();
  char value[64];
  for (uint32_t i = 0; i < count; ++i) {
    const char* path = vfs::mount_path(i);
    const char* kind = vfs::mount_kind(i);
    out("  ");
    out(kStyleHead);
    out(path);
    const size_t path_len = strlen(path);
    spaces((path_len < 12) ? (12 - path_len) : 1);
    out(kStyleOn);
    out(kStyleQuiet);
    out(kind);
    spaces((strlen(kind) < 10) ? (10 - strlen(kind)) : 1);
    out(kStyleOn);
    // **Escrivible o no, amb el color del registre**: `rw` verd i `ro` vermell, perquè és el que una persona mira abans de desar-hi res
    // (0.190.238, docs/70).
    const bool writable = vfs::mount_writable(i);
    out(writable ? kStyleYes : kStyleNo);
    out(writable ? "rw" : "ro");
    out(kStyleOn);
    out("  ");

    uint64_t total = 0;
    uint64_t free_bytes = 0;
    if (vfs::space(path, total, free_bytes)) {
      // **Els bytes que es poden llegir** (0.190.238): `free 11.9 MB of 15.8 MB` és el que el codi ja feia a mà per sobre del megabyte i
      // ara ho fa per a les tres unitats, també per a una targeta de setze gigabytes, que en bytes és un número que ningú compta
      // (docs/70).
      char free_text[24];
      human_bytes(free_bytes, free_text, sizeof free_text);
      human_bytes(total, value, sizeof value);
      snprintf(value + strlen(value), sizeof value - strlen(value), " in all");
      row_inline("free", free_text, value);
    } else {
      // Not "no room": the values of the NVS are not files and there is no byte count to
      // give. What there is, is a number of entries, and that is the number a person needs
      // before the next thing that has to be saved does not fit (docs/70).
      uint32_t used_entries = 0;
      uint32_t total_entries = 0;
      hal::IKeyValue* kv = platform().nvs();
      if (kv != nullptr && kv->stats(used_entries, total_entries)) {
        if (total_entries > 0) {
          snprintf(value, sizeof value, "%u of %u entries", static_cast<unsigned>(used_entries),
                   static_cast<unsigned>(total_entries));
        } else {
          snprintf(value, sizeof value, "%u entries", static_cast<unsigned>(used_entries));
        }
        row_inline("entries", "", value);
      } else {
        row_inline("space", "", "unknown");
      }
    }
    newline();
  }
}

class ListPrinter : public hal::ListSink {
 public:
  // **A name is the answer and the rest is the frame round it** (0.190.240, l'amo: *«ls està igual, li cal una mica d'estil»*): el tipus i la
  // mida en el gris del mig del vidre, **el nom d'una carpeta en el cian de la casa i el d'un fitxer en el blanc de sempre**, i la mida en les
  // unitats que es llegeixen (`1.4 KB` i no `1433`). És el primer ús del color que hi ha —un nom que mana sobre el que l'envolta— i el
  // comentari de sempre ja ho deia; el que faltava era fer-ho (docs/30, docs/85).
  bool on_entry(const hal::ListEntry& entry) override {
    const bool folder = (entry.type == hal::NodeType::kDir);
    char size[24];
    human_bytes(entry.size, size, sizeof size);
    out("  ");
    out(kStyleQuiet);
    out(folder ? "<dir>   " : "file    ");
    out(kStyleOn);
    out(folder ? kStyleHead : kStyleOn);
    out(entry.name);
    out(kStyleOn);
    if (!folder) {
      out("  ");
      out(kStyleQuiet);
      out(size);
      out(kStyleOn);
    }
    newline();
    ++rows;
    return true;
  }

  // **I quantes coses hi ha**, que és el que una persona compta quan la llista és llarga i el que li diu si la carpeta és buida (docs/30).
  void summary() {
    out("  ");
    out(kStyleQuiet);
    if (rows == 0) {
      out("(nothing here)");
    } else {
      char line[32];
      snprintf(line, sizeof line, "%u entr%s", static_cast<unsigned>(rows), (rows == 1) ? "y" : "ies");
      out(line);
    }
    out(kStyleOn);
    newline();
  }

  uint32_t rows = 0;
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
    title(path);
    ListPrinter printer;
    printer.on_entry(one);
    return;
  }
  // **El camí, a dalt, i quantes coses hi ha, a baix** (0.190.240): una llista de trenta noms sense dir de quina carpeta són és una llista
  // que una persona ha de reconstruir de memòria, i una carpeta buida i una carpeta que no s'ha pogut llegir es veuen igual (docs/30, docs/70).
  title(path);
  ListPrinter printer;
  if (!vfs::list(path, printer)) {
    error_out("cannot list: ");
    out(path);
    newline();
    return;
  }
  printer.summary();
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
  // **Si l'app és d'administrador** (0.189.99): `"admin": true` del manifest, que és el que fa que la seva màquina de Lua
  // tingui la taula `admin` i cap altra. Es copia aquí perquè l'arrencada demana el manifest una vegada i la màquina es fa
  // després (docs/30, docs/40).
  bool admin;
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
  target->admin = entry.admin;
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

// **The app another app asked for** (0.153.0): `app.run` writes it and the end of the turn takes it. It is one name and
// not a queue: a launcher starts one app per press, and a second wish in the same turn is refused where it is asked, in
// the words a person reads (docs/30, docs/90).
char g_run_wanted[40] = {0};
// **Els commutadors del desig** (0.189.161): the switches the app that asked carried, given to the next start as `app.args.flags`
// and spent with the wish —a string of the wish and not of the manifest, which is what `run appinfo -d battery` says (docs/30).
char g_run_flags[48] = {0};
// **Els commutadors que el proper arrencada ha de rebre** (0.189.161): filled by the consumer of the wish —or by the prompt— and
// read by the start that follows, which is the one the wish was about.
char g_start_flags[48] = {0};
// **The app to come back to** (0.153.0): the launcher that asked for another one and wants it back when that one ends,
// which is what makes a launcher usable on a board whose only way out of an app is three presses of a key. Empty when
// the app that asked handed the glass over for good (docs/30, docs/90).
char g_run_back[40] = {0};
// Whether the app that asked wants the glass back when the other one ends.
bool g_run_wanted_back = false;

// **Els commutadors d'aquest arrencada, copiats d'una vegada** (0.189.163, i el parany trobat en el repàs del 17/11): the
// string of a wish or of a `run` is taken here and the global is cleared **before anything can go wrong**, perquè un
// arrencada que falla —una app que no és al disc— deixava els commutadors posats i el següent app, triada d'on fos, els
// rebia (vist amb `run appinfo -d battery` en una placa sense `appinfo`). It is a static and not the stack because the
// machine is built further down and the text has to be there when it is (docs/30, docs/40).
static char g_switches_of_this_start[48] = {0};

bool run_app(const char* id, const char* open_path, bool restore = false, bool splash = true,
             const alarms::Entry* alarm = nullptr, int64_t late_ms = 0) {
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

  // **Els commutadors es prenen aquí i es gasten amb l'arrencada**: the wish's own string, or the manifest's `opensArgs`
  // when there is none; a switch of a wish is not a switch of the app, and the next start of it —from the launcher, from the
  // prompt— is a start without them (docs/30, docs/40).
  snprintf(g_switches_of_this_start, sizeof(g_switches_of_this_start), "%s",
           (g_start_flags[0] != '\0') ? g_start_flags : target.opens_arg);
  g_start_flags[0] = '\0';

  // **Una app que arrenca demana el topall una estona** (0.179.38): reading its source, building an interpreter and laying
  // its first page out is the heaviest thing this system does on purpose and it is foreseeable, so the processor is at
  // the top step **before** any of it begins and the window ends alone —`power::poll` sees it, not the app— (docs/50,
  // docs/60). A step that were never given back is a battery burnt on a page that is already on the glass.
  power::hold_cpu(power::kBusyMhz, power::kStartBusyMs, "an app is starting");

  // **EL TÍTOL PRIMER, ABANS DE NETEJAR RES** (0.179.27): the name of the app that is coming goes to the middle of the
  // bar of the system —which is the system's and not the app's— and it is painted **here**, with the page of the app
  // before still on the glass and before the clean that comes next. A person who presses an app sees which one it is
  // while the glass is being cleaned and not after, and the clean does not take the name away: the wash of `ui::attach`
  // is the rows of the view and never the strip (docs/30, docs/50).
  //
  // **The one start that cannot have it here is the one that shows a picture**: the splash washes the whole glass —the
  // bar with it—, so a name painted before it is a name nobody sees (the rule of 0.118.1, docs/40). That one gets the
  // title again after the picture, below, and `flush()` there paints only what the splash took.
  //
  // **I LA PINTURA ÉS DEL QUI ARRENCA I NO DE L'APP** (0.179.33): `splash` is the caller's word. The boot of a home app
  // does not want it —the glass already has the picture of the boot— and the way back to a launcher does (docs/30,
  // docs/40).
  const bool shows_a_picture = splash && (target.splash[0] != '\0') && !restore;
  // **I una app que puja demana el vidre** (0.190.19): a wake for an alarm keeps the glass exactly as the sleep left it, i el que el
  // desperta de debò és una pàgina que algú ha de veure —un `raise` d'una alarma, o la pàgina de la persona—. El que s'ha pintat mentre
  // dormia no s'ha enviat, o sigui que la pàgina es deu sencera i la barra es pinta amb el títol de l'app que arriba (docs/30, docs/60).
#if LUNOK_GLASS
  if (display::the_glass_is_asleep()) {
    display::set_the_glass_asleep(false);
    ui::owe_the_page();
  }
#endif
  bar::title(target.title);
  if (!shows_a_picture) bar::flush();

  // The app gets a view of its own, white and with nothing pending; the glass is
  // not touched until it presents (docs/30).
  //
  // **I QUAN HI HA UNA IMATGE AL DARRERE, EL RENTAT ENTRE DUES APPS NO ES PAGA** (0.189.39, l'amo: *«Revisa a veure quants
  // refrescos es fan desde que es tanca l'app que t'ha cridat, l'splash i després la nostra app»*): the picture washes the
  // whole panel a few lines below, so the clean page of the app that has just gone was a whole `gc16` and its 290 to 450 ms
  // painting a page that nobody was going to see. The view is the same white one and the page the app draws is owed whole
  // and clean by `owe_the_page()`, which is the pass that takes the picture away (docs/30, docs/50).
  if (shows_a_picture) {
    ui::attach_under_a_picture();
  } else {
    ui::attach();
  }
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
  if (shows_a_picture) {
    char root[96];
    char picture[160];
    const int made = apps::root_of(id, root, sizeof(root))
                         ? snprintf(picture, sizeof(picture), "%s/%s", root, target.splash)
                         : -1;
    if (made > 0 && static_cast<size_t>(made) < sizeof(picture)) {
      if (!splash::show(picture)) {
        log_printf(pal::kLogDebug, "SPLASH", "%s: %s", id, splash::last_error());
        // **La pàgina es deu igualment** (0.189.39): the clean page between two apps was not paid and the picture that was
        // going to cover it is not on the glass, so the first page of this app is the one that has to arrive whole and clean
        // or it lands on the ink of the app before (docs/30, docs/50).
        ui::owe_the_page();
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
  // **I el nom una altra vegada, per al que no ha pogut tenir-lo abans** (0.179.27): with a picture the strip was washed
  // under the title, so it is painted whole here —which is what `bar::refresh()` asked for—; without one the title is
  // already on the glass and this paints nothing at all, because the flag went with the paint above.
  if (shows_a_picture) {
    bar::title(target.title);
    bar::flush();
  }

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
  // **Els commutadors són els que es van copiar en entrar** (0.189.163): a start that is not a wish has the manifest's own,
  // and the start that asked for them took them away already (docs/30, docs/40).
  boot.args_flags = g_switches_of_this_start;
  // True when the app is being brought back from a sleep: it is how an app knows
  // to read what it saved instead of starting from nothing (docs/90).
  boot.args_restore = restore;
  // **I els poders d'administrador, que són del manifest i d'ingú més** (0.189.99): the machine is built with them or
  // without them, and it says so once in the log —an app that changes the system should be possible to find in a log
  // without reading its own page— (docs/30, docs/40).
  boot.admin = target.admin;
  if (boot.admin) log_printf(pal::kLogInfo, "APPS", "%s is an administrator", id);
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
  // **Obrir una app, mesurada**: the interpreter comes up and the chunk is compiled here, o sigui que el que una persona espera
  // després de prémer una cel·la és aquesta fila i no el metre de l'app, que encara no corre (docs/30, docs/75).
  bool the_interpreter;
  {
    meter::Scope metering(meter::kAppStart);
    the_interpreter = luabind::start(boot, limit);
  }
  if (!the_interpreter) {
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
  // **I el tàctil de la pàgina que hi havia no és d'aquesta** (0.189.100, l'amo: *«en tornar al Home se m'han obert apps totes
  // soles … és simplement que un event de la vista anterior arriba a la vista nova?»*): the finger that was down when the app
  // changed clicked the control of the new page under the pixel it landed on —a cell of the launcher, which starts an app—,
  // and the tap that the queue still owed (a tap is said three hundred and fifty milliseconds after the finger leaves) was
  // read by an app that did not exist when it happened. An app hears only the contacts that begin after it is here
  // (docs/30, docs/50).
  touch::forget_the_page_before();
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
    // **Una app que es mor al primer torn també té el seu perquè** (0.189.162): an app whose first line is an error —which is what a
    // page that was never tested looks like— ended here and not in `poll_app`, so the page of the error had to be asked for in this
    // path too, or the owner's example app would have died in silence (docs/30, docs/50).
    char who[40];
    char why[520];
    const bool died = luabind::failed();
    who[0] = '\0';
    why[0] = '\0';
    if (died) {
      snprintf(who, sizeof(who), "%s", id);
      snprintf(why, sizeof(why), "%s", luabind::last_error_text());
    }
    g_app_alive = false;
    g_app_detached = false;
    luabind::set_console_attached(true);
    const int code = luabind::exit_code();
    luabind::stop();
    if (died && why[0] != '\0') {
      ui::give_back_the_glass();
      bar::title("");
      app_error::show(who, why);
    }
    // **I la pàgina que el va demanar torna a agafar el vidre** (0.179.48): an app that ends in its first turn is an app
    // that ended, and the promise of `app.run` is the same one for both halves —the ended path of `poll_app` is the other
    // half—: the page that asked for this one comes back, with the card its manifest names. Without this the wish stayed
    // in `g_run_back` and **the next app that ended went back to a page nobody had asked for**, while the launcher whose
    // app had already ended —a page that only prints and leaves, which an app is allowed to be— never came back at all
    // (found by the case of the launcher's card, docs/30, docs/40).
    if (g_run_back[0] != '\0') {
      ui::give_back_the_glass();
      bar::title("");
      start_the_one_that_asked();
      return true;
    }
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
  //
  // **I el que ha fet pujar l'app se li diu quan la pàgina és dempeus** (0.190.7): a raise is a window that stays —the same
  // entry, the same hook, the same `meta`— and the only difference is that the machine is not thrown away when the hook
  // returns.
  //
  // **Però no al primer torn**: el primer torn corre el bloc fins al primer `await`, que pot ser al mig del `build()`
  // —l'`Agenda` i el `Clock` en tenen—, i un ganxo que hi arriba escriu en una pàgina que encara no ha acabat de fer-se i
  // després el propi bloc li esborra la bandera: vist a l'emulador el 18/11, el rellotge pujat per una alarma ensenyava
  // l'esfera. El que s'espera és el que el sistema ja sap d'una app que és al davant: **que esperi un temporitzador**, o
  // sigui que el seu bucle ha demanat el torn següent. `until_next_ms() > 0` és exactament això; el sostre de torns és per
  // a una pàgina que no s'atura mai, que no ha de penjar l'arrencada (docs/30, docs/60).
  if (alarm != nullptr && g_app_alive) {
    for (uint8_t turn = 0; turn < kSettlingTurns; ++turn) {
      const int32_t wait = luabind::until_next_ms();
      if (wait > 0) break;
      if (!luabind::poll()) break;
    }
    if (!g_app_alive) return true;
    if (!luabind::call_alarm(*alarm, late_ms)) {
      log_printf(pal::kLogWarn, "ALARM", "%u for %s: the app has no on_alarm", static_cast<unsigned>(alarm->id), id);
    } else {
      log_printf(pal::kLogInfo, "ALARM", "%u told to %s in the page it raised (%lld ms late)",
                 static_cast<unsigned>(alarm->id), id, static_cast<long long>(late_ms));
    }
  }
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
  // **Els dos senyals de debò que un manifest porta i la targeta no deia** (0.189.99): `launcher` i `admin`. Són booleans i
  // la targeta és de textos, o sigui que es diuen amb la paraula que tenen —`true` o `false`— i amb la ratlla quan el
  // manifest no els diu, que és la mateixa regla que la resta de la targeta (docs/30, docs/40).
  bool launcher = false;
  const bool has_launcher = lunok::json::get_bool(text, len, "launcher", launcher);
  bool admin = false;
  const bool has_admin = lunok::json::get_bool(text, len, "admin", admin);
  const char* launcher_text = launcher ? "true" : "false";
  const char* admin_text = admin ? "true" : "false";

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
      {"launcher", launcher_text, has_launcher},
      {"admin", admin_text, has_admin},
      {"icon", icon, icon[0] != '\0'},
      {"description", description, description[0] != '\0'},
  };
  for (const Field& field : fields) {
    out("  ");
    out(field.name);
    for (size_t pad = strlen(field.name); pad < 12; ++pad) out(" ");
    if (!field.known) {
      out("--");
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
  // the task of the console, so a block of its own is better than a kilobyte of the stack that an app shares with
  // everything else. **I no és un bloc estàtic** (0.190.206): was a `.bss` kilobyte of the board for a command a person
  // types once; now it is `pal::Scratch`, que el pren de la memòria extra i el torna al sortir, amb una sola sortida per
  // camí i sense res a recordar (docs/75).
  pal::Scratch manifest(1024, pal::kMemoryShell);
  char* text = static_cast<char*>(manifest.get());
  if (text == nullptr) {
    error_out("apps: no memory to read a manifest\r\n");
    return false;
  }
  size_t len = 0;
  if (!vfs::read_file(path, text, 1023, len)) {
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
//
// **I la recursió és una de sola** (0.189.157): `vfs::drop_tree` és la que fa servir l'esborrada d'una app i la que mou una
// carpeta, o sigui que aquesta shell només hi passa el compte (docs/70).
struct Dropped {
  uint32_t files;
  uint32_t folders;
  uint32_t bytes;
};

void drop_tree(const char* path, uint8_t depth, Dropped* counted = nullptr) {
  (void)depth;
  vfs::Dropped gone;
  if (!vfs::drop_tree(path, gone)) return;
  if (counted == nullptr) return;
  counted->files += gone.files;
  counted->folders += gone.folders;
  counted->bytes += gone.bytes;
}

// **`apps remove <id>`: the app goes, and only the app** (0.89.0). The id is the name `apps` and `run` show —`squares`
// for the copy of the flash and `squares-SD` for the copy of the card— so one of two copies leaves the disk without the
// other, which is the whole reason the door presents them apart. **Its settings and its data stay** (`/cfg/<id>` and
// `/data/<id>`): those are what the app left behind and not the app, and a person who wants them gone says so with
// another path, not with this one (docs/30, docs/40, docs/70).
bool apps_remove(const char* id) {
  // **La mateixa porta que `admin.uninstall`** (0.189.157): the folder, the alarms the app had armed and the sessions it had
  // written, all in `apps::remove`; the prompt only says what went. **Els seus ajustos i la seva dada es queden** (docs/30,
  // docs/40, docs/70).
  if (id == nullptr || id[0] == '\0') {
    usage_out("usage: apps remove <id>\r\n");
    return false;
  }
  apps::Removing gone;
  if (!apps::remove(id, gone)) {
    // **Els dos errors són dos i es diuen diferent** (docs/40): a folder that is not there and a folder that is there but
    // carries no manifest are not the same mistake, and a person typing a name has to know which one they made.
    if (gone.missing == apps::Removing::Missing::kNotDropped) {
      error_out("apps: ");
      out(id);
      out(" is still there (a read-only disk, or a file the disk would not remove)\r\n");
    } else if (gone.missing == apps::Removing::Missing::kNoManifest) {
      error_out("not an app: ");
      out(id);
      out(" has no manifest.json\r\n");
    } else {
      error_out("no such app: ");
      out(id);
      out("\r\n");
    }
    return false;
  }
  good_out("apps: ");
  out(id);
  out(" removed from ");
  out(gone.disk);
  out(", ");
  out_num(gone.gone.files);
  out((gone.gone.files == 1) ? " file" : " files");
  if (gone.gone.folders > 0) {
    out(", ");
    out_num(gone.gone.folders);
    out((gone.gone.folders == 1) ? " folder" : " folders");
  }
  out(", ");
  out_num(gone.gone.bytes);
  out(" bytes");
  if (gone.alarms > 0) {
    out(", ");
    out_num(gone.alarms);
    out((gone.alarms == 1) ? " alarm" : " alarms");
  }
  if (gone.session_blocks > 0) {
    out(", ");
    out_num(gone.session_blocks);
    out(" session block(s)");
  }
  if (gone.session_files > 0) {
    out(", ");
    out_num(gone.session_files);
    out(" file(s) of cache");
  }
  out("\r\n");
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
    // **I el llançador ho ha de saber** (0.189.23): the list of apps changed in this very command, and the app that draws
    // it —the one `cfg home` names— is told between two turns. Here and not after the answer below, because the id of the
    // app is cleared at the end of this function (docs/30, docs/40).
    apps::note_a_change("install", g_install_app);
    // **A copy that lands beside one the flash already has is run by its redirected id** (0.164.0): the door presents
    // the flash's copy as `<id>` and the card's as `*<id>` —whose name says the unit, `Editor-SD`—, so a person who
    // installed an app to try a version has to know which name runs what they have just downloaded. The question is the
    // presenter's own —does the flash carry an app of that name?— and not a stat of `/apps/<id>`, which the card would
    // answer too (docs/40, docs/70).
    if (strncmp(g_install_root, "/sd/", 4) == 0 && apps::is_an_app("/littlefs/apps", g_install_app)) {
      out("  the flash has a ");
      out(g_install_app);
      out(" too: run *");
      out(g_install_app);
      out(" to run this copy\r\n");
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
// **`alarm`: la cua de les alarmes, la més propera al davant** (0.189.149, l'amo: *«Vull que el sistema mostri la llista
// d'alarmes que te i quina es la mes propera quan s'afegeixi o tregui una alarma»*). The name was in the list of `help` and
// it had no body: a person who typed it got the usage of nothing. The words of each line are the library's
// (`alarms::describe`), which is the same formatter the log uses, i el que aquesta comanda afegeix és **on és el despertar**,
// que és la meitat de la pregunta que el registre contesta quan arma (docs/30, docs/60).
bool command_alarm(const char* argument) {
  char words[2][kWordCap];
  size_t count = 0;
  if (!read_words(argument, words, 2, count)) return false;
  if (count > 0 && strcmp(words[0], "list") != 0) {
    usage_out("usage: alarm [list]\r\n");
    return false;
  }
  hal::IClock* clock = platform().clock();
  const uint32_t total = alarms::count();
  out("alarms: ");
  out_num(total);
  out(" of ");
  out_num(alarms::max());
  out("\r\n");
  if (total == 0) {
    out("  the queue is empty\r\n");
  } else {
    const int64_t mono_now = (clock != nullptr) ? clock->monotonic_ms() : pal::system().millis();
    const int64_t epoch_now = (clock != nullptr) ? clock->epoch_ms() : 0;
    for (uint32_t i = 0; i < total; ++i) {
      char line[160];
      if (alarms::describe(i, mono_now, epoch_now, cfg::offset_minutes(), line, sizeof(line)) == 0) continue;
      out("  ");
      out(line);
      out("\r\n");
    }
  }
  // **On és el despertar, que és el que la cua no pot dir de si mateixa**: la cua sap quina alarma és la propera i no
  // sap en quin xip s'ha quedat, perquè allò és del rellotge de la placa (docs/50, docs/60).
  if (clock == nullptr) {
    out("wake: this board has no clock to arm\r\n");
    return true;
  }
  out("wake: ");
  if (!clock->can_wake()) {
    out("the turn of the loop (this board cannot wake itself)\r\n");
    return true;
  }
  const char* keeper = clock->wake_keeper();
  if (strcmp(keeper, "chip") == 0) {
    out("the clock chip\r\n");
  } else if (strcmp(keeper, "timer") == 0) {
    out("the RTC timer of the SoC\r\n");
  } else {
    out("nothing armed\r\n");
  }
  return true;
}

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
      // **I en una placa que no escala només n'hi ha un** (0.190.227).
      out("cpu: this board does not run at ");
      out_int(asked);
      if (power::scales_the_clock()) {
        out(" MHz (80, 160 or 240)\r\n");
      } else {
        out(" MHz: it does not scale its clock and runs at 240 always\r\n");
      }
      return false;
    }
    out("cpu: ");
    out_num(power::cpu_mhz());
    out(" MHz\r\n");
    return true;
  }

  // **`power auto [on|off]`**: la política automàtica de la velocitat (0.189.94). `power cpu <mhz>` la para i la deixa
  // parada; aquesta la torna a encendre i el board es posa al pas que diu la càrrega en la mateixa línia (docs/50, docs/60).
  if (count > 0 && strcmp(words[0], "auto") == 0) {
    if (count == 1) {
      out("cpu: the automatic scaling is ");
      out(power::cpu_auto() ? "on" : "off");
      out("\r\n");
      // **I en una placa que no escala no hi ha res a engegar** (0.190.227): el rellotge hi és sempre al cim, i això s'ha de dir quan es
      // pregunta i no quan es prem (docs/50, docs/60).
      if (!power::scales_the_clock()) {
        out("cpu: this board does not scale its clock; it runs at ");
        out_num(power::cpu_mhz());
        out(" MHz always\r\n");
      }
      return true;
    }
    if (count != 2 || (strcmp(words[1], "on") != 0 && strcmp(words[1], "off") != 0)) {
      usage_out("usage: power auto [on|off]\r\n");
      return false;
    }
    const bool on = strcmp(words[1], "on") == 0;
    power::set_cpu_auto(on);
    if (!power::scales_the_clock()) {
      // **El commutador no canvia res aquí, i el que es diu és la veritat de la placa** (0.190.227): no que s'hagi apagat el que s'ha
      // demanat, sinó que aquest xip no té escala (docs/50, docs/60).
      out("cpu: this board does not scale its clock; it runs at ");
      out_num(power::cpu_mhz());
      out(" MHz always\r\n");
      return true;
    }
    out("cpu: the automatic scaling is ");
    out(on ? "on" : "off");
    out(", ");
    out_num(power::cpu_mhz());
    out(" MHz\r\n");
    return true;
  }

  // **`power nap [ms]`**: una becaina i el que l'ha tornada (0.190.71). És l'instrument que diu si aquesta placa pot dormir de debò: es
  // demana un son lleuger d'un segon, es toca el vidre o es prem una tecla, i el registre diu quina porta s'ha obert i quants mil·lisegons
  // ha durat —o que el xip no l'ha presa— (docs/50, docs/95).
  // **`power last`**: **què va fer la darrera dormida** (0.190.151, l'amo: *«posa tot el que necessiti en aquesta prova, no en vull fer més»*).
  // A deep sleep is a reset and the console does not wait for it: this is the one question that answers the whole thing after the cable is
  // back —which doors were armed, which were already open, and the raw level of the three lines (docs/50, docs/60, docs/95).
  if (count > 0 && strcmp(words[0], "last") == 0) {
    power::say_the_last_sleep();
    return true;
  }
  if (count > 0 && strcmp(words[0], "nap") == 0) {
    int32_t ms = 1000;
    if (count == 2) {
      const int32_t asked = static_cast<int32_t>(strtoll(words[1], nullptr, 10));
      if (asked > 0) ms = asked;
    }
    if (!power::can_nap()) {
      out("nap: this board does not take a light sleep\r\n");
      return false;
    }
    const bool slept = power::nap_now(ms);
    out("nap: ");
    out(slept ? "slept " : "did not sleep ");
    out_num(static_cast<uint32_t>(slept ? ms : 0));
    out(" ms");
    if (!slept) out(" (the chip refused it, or there was no door to come back from)");
    out("\r\n");
    return slept;
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
    // **I la quietud, que és el número de debò** (0.190.77): the switch says whether the policy may sleep and this says whether it
    // is about to: quants segons fa que ningú no toca la placa, quants en vol la política i qui va ser l'última mà. És la línia que
    // converteix «no s'adorm» en un subjecte, i el que l'amo va haver de preguntar el 20/11 amb dues mil sis-centes becaines al
    // registre i cap son profund (docs/30, docs/60).
    char report[128];
    if (power::idle_report(report, sizeof(report))) {
      out(report);
      out("\r\n");
    }
    return true;
  }

  if (count != 0) {
    usage_out("usage: power | power cpu [mhz] | power idle [on|off] | power last\r\n");
    return false;
  }
  // **`power` a seques diu també la quietud** (0.190.77): the first thing a person asks when the board does not go to sleep is how
  // long it has been quiet, and the answer has to be one word away (docs/30, docs/60).
  {
    char report[128];
    if (power::idle_report(report, sizeof(report))) {
      out(report);
      newline();
    }
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

// **Una clau del mesh, escrita com tothom les escriu** (0.190.218): trenta-dos dígits hexadecimals, els setze bytes i cap checksum —el mesh
// no en porta—, o sigui que el que es copia d'aquí és literalment la clau. El text el posa i el llegeix `kernel::mesh`, perquè el prompt, la
// botiga i el registre no escriguin la mateixa clau de tres maneres (docs/45, docs/50).
void print_key(const char* label, const uint8_t key[16]) {
  char text[33];
  mesh::key_to_text(key, text);
  out("  ");
  out(label);
  out(" ");
  out(text);
  out("\r\n");
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

// **heapguard [bytes|off]**: els canaris de la memòria, dits al prompt (0.190.101, l'amo: *«millor fem les proves que volies fer amb els
// canaris i tota la pesca»*). This is the tool of a build that fell inside the `tlsf` with a `StoreProhibited` and no name: the guards
// of the extra memory say **which block and how big** the moment it is given back, and the watchpoint says **who** wrote past it, with
// its own `PC` and stack. `heapguard check` camina els dos munts i diu on és el forat (docs/75, docs/95).
bool command_heapguard(const char* argument) {
  char words[1][kWordCap];
  size_t count = 0;
  if (!read_words(argument, words, 1, count)) return false;
  const uint32_t guards = pal::debug_guards_on();
  if (count == 0) {
    if (guards == 0) {
      out("heapguard: this build has no guards (rebuild with LUNOK_MEMORY_GUARDS)\r\n");
      return true;
    }
    out("heapguard: guards on, nothing broken yet or something was (see the log)\r\n");
    out("  heapguard <bytes>   watch the tail of the next block of that size\r\n");
    out("  heapguard off       forget the watch\r\n");
    out("  heapguard check     walk both heaps and say where the hole is\r\n");
    return true;
  }
  if (strcmp(words[0], "off") == 0) {
    pal::debug_watch_off();
    out("heapguard: the watch is off\r\n");
    return true;
  }
  if (strcmp(words[0], "check") == 0) {
    const bool whole = pal::debug_heap_check();
    out(whole ? "heapguard: both heaps are whole\r\n" : "heapguard: a heap is broken, see the log\r\n");
    return true;
  }
  const long asked = strtol(words[0], nullptr, 10);
  if (asked <= 0) {
    usage_out("usage: heapguard [bytes|off|check]\r\n");
    return false;
  }
  pal::debug_watch_a_block_of(static_cast<size_t>(asked));
  out("heapguard: the next block of ");
  out_num(static_cast<uint32_t>(asked));
  out(" B will be watched\r\n");
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
  // **I la botiga, escrita quan la sessió de Lua es tanca** (0.190.116): `cfg.sort(true)` from a session is the same as the row of a page,
  // o sigui que tancar la sessió és el mateix moment d'escriure-ho a la flash (docs/30, docs/70).
  if (hal::IKeyValue* store = platform().nvs()) store->commit();
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
// **On viu l'historial del terminal** (0.190.236): el disc preferit de la persona, i no un altre. `/data` és la porta que ja el segueix
// (0.190.8), però `linenoise` escriu amb `fopen` i el que `fopen` entén és el camí del VFS de la IDF —`/littlefs` i `/sd`, els dos discs
// que hi són muntats—, o sigui que aquí es tradueix la porta al camí: `/<unitat>/data/history.txt`. Amb una targeta el fitxer neix a la
// targeta i el flaix no s'escriu; sense targeta, al flaix, que és l'únic que hi ha (docs/30, docs/50, docs/70).
bool history_path(char* out, size_t cap) {
  const char* unit = cfg::unit();
  if (unit == nullptr || unit[0] == '\0' || out == nullptr || cap < 24) return false;
  snprintf(out, cap, "/%s/data/history.txt", unit);
  return true;
}

// this is `close_console_session` and the other one is `close_session` of `kernel/shell.h`.
bool close_console_session() {
  // **I el que s'ha escrit es desa abans de tancar** (0.190.236): una sessió que marxa no s'endú les comandes que la persona ha escrit; es
  // fa aquí i no a cada línia, perquè una escriptura de disc per tecla premuda no és un preu que una placa hagi de pagar (docs/30, docs/70).
  keep_the_history();
  g_editor.reset();
  // **I el follow no sobreviu a la sessió** (0.190.185): a mirror left open is a wire that starts getting the log again after the
  // logout, which is the very thing this console stopped doing. The mirror goes back to what the console is, not to zero.
  g_log_follow = false;
  log_echo(!a_wire_console());
  // **I el comiat deixa la pantalla com la deixa el `clear`** (0.190.188, l'amo: *«igual que amb "clear" passes unes linies (passa'n
  // mes!) fes el mateix amb logout/exit»*): el paper puja abans del missatge, o sigui que l'últim que una persona llegeix és que la
  // sessió s'ha tancat i com tornar-hi, i no el que hi havia a sota. Passa igual a les dues consoles —la del cable i la que es penja—
  // perquè totes dues entenen els salts de línia (docs/30, docs/85).
  wipe_the_paper();
  pal::IConsole* console = pal::system().console();
  if (console != nullptr && console->virtual_terminal()) {
    // **La sessió de consola s'acaba i les cachés es desen abans** (0.189.11): the run goes away with the program, and a
    // cache that was in the memory and not on the card would go with it. The lines of what is parked are the ones of
    // `park_all`, one per block, with the app, the key, the bytes and the path (docs/30, docs/75).
    session::park_all("the session of the console was closed");
    out("logout: the session is closed\r\n");
    console->flush();  // the line has to be out before the run stops, or it is a word cut in half
    shutdown();        // on the emulator it is the program that ends, which is the session going away
    return true;
  }

  g_session_open = false;
  out("logout: the session is closed; press Enter to open a new one\r\n");
  // **I el terminal es queda sense prompt** (0.190.222): una sessió tancada que encara ensenya `lunok> ` és un prompt que demana una cosa
  // que no es pot fer. El que demana és un Enter, i és el que el missatge de dalt diu (docs/30).
  if (console != nullptr) {
    if (console->has_a_line_editor()) console->set_the_prompt("");
    console->flush();
  }
  // **I el pany es deixa anar aquí** (0.190.184): la sessió tancada és el que permet que el xip torni a fer becaines, i el despertar per la
  // consola el torna a agafar quan una persona prem Enter (docs/60).
  sync_the_wire_lock();
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

// **I les dues portes de l'historial, fora de l'anonimat** (0.190.236): les declara `kernel/shell.h` i les crida el nucli, i una funció dins
// de l'anonimat té enllaç intern i el que queda és un `undefined reference` a l'enllaç — que és exactament el que va passar quan aquesta
// casa va escriure per primer cop una funció així dins d'un bloc anònim (docs/30).
void load_the_history() {
  pal::IConsole* console = serial::console();
  char path[48];
  if (console == nullptr || !history_path(path, sizeof path)) return;
  if (console->load_the_history(path)) log_printf(pal::kLogInfo, "CONSOLE", "the history of the session came back from %s", path);
}

void keep_the_history() {
  pal::IConsole* console = serial::console();
  char path[48];
  if (console == nullptr || !history_path(path, sizeof path)) return;
  (void)console->save_the_history(path);
}

// **El color del prompt** (0.190.222, l'amo: *«Poder moure el cursor, fer efectes, i colors dona molta vidilla»*): el terminal del cable
// entén codis d'escapament i és el que posa el prompt a la pantalla, o sigui que la paraula del shell hi va amb el seu color —el cian
// del que aquesta casa fa servir per al que és del sistema— i la resta del port continua essent text pla (docs/30, docs/50).
// **I amb la paleta de 256 colors, que és el que un terminal de debò té** (0.190.229, l'amo: *«vull un terminal monitor ANSI de 256 colors i
// que es vegi dins del puto vscode»*): `38;5;N` és el color N de la taula de 256 —no els vuit de sempre— i el 51 és el cian clar, el 220
// l'ambre i el 45 el cian del rellotge. **Una sola forma per a tots els colors d'aquesta casa**: el prompt, el registre i el que vingui
// després surten de la mateixa taula (docs/30, docs/50, docs/85).
//
// Cada un és `ESC [ 38 ; 5 ; N m` … `ESC [ 0 m` i prou: el filtre (`ansi off`) els treu sencers, i l'amplada visible del prompt la mesura
// el terminal quan contesta (`ESC[6n`), o sigui que els codis no compten com a caràcters ni descol·loquen el cursor (docs/30).
const char* const kTerminalPrompt = "\033[38;5;51mlunok>\033[0m ";
const char* const kTerminalLuaPrompt = "\033[38;5;220mlua>\033[0m ";

void prompt() {
  // **A closed session does not ask**: the prompt is what says the console is listening, and every caller keeps
  // asking for it —the end of an app, a new line, a command— because it is one place and not ten (docs/30).
  if (!g_session_open) return;
  // **And a session of an app is a pipe and not a terminal**: no prompt and no echo, so what the app reads is what
  // the command printed and nothing else (docs/30).
  if (g_sink != 0) return;
  g_esc = 0;
  g_escape = Escape::kNone;
  // **La paraula del prompt la diu el terminal, no el camí** (0.190.222, i són dues preguntes des de la 0.190.223): **el color** depèn que
  // l'altre cap entengui els codis (`speaks_ansi`) i **qui l'escriu** depèn que hi hagi editor de línia (`has_a_line_editor`). Amb el cable
  // solen venir junts i no sempre: un driver del USB que no arrenca deixa el terminal sense editor i amb els codis, i aleshores el prompt
  // l'ha d'escriure aquesta casa —amb el color igualment, que per això el terminal els entén—. I un monitor que no és terminal no en parla,
  // o sigui que el prompt hi arriba amb lletres i prou (docs/30, docs/50, docs/85).
  // **I el color el decideix el commutador i prou** (0.190.226, i des de la 0.190.231 sense preguntar res més): la paraula del prompt porta els
  // codis sempre que `g_ansi` sigui encès, i qui la dibuixa és l'editor si n'hi ha i aquesta casa si no n'hi ha. **Són dues preguntes i no se
  // separen per gust**: l'editor necessita que el terminal contesti (mesura l'amplada i el prompt), el color només necessita que el terminal
  // pinti el que li arriba — i el de dins del VS Code pinta i no contesta (docs/30, docs/50, docs/85).
  pal::IConsole* console = serial::console();
  const bool terminal = console != nullptr && console->has_a_line_editor();
  const char* const word = g_lua ? (g_ansi ? kTerminalLuaPrompt : "lua> ") : (g_ansi ? kTerminalPrompt : "lunok> ");
  set_prompt_text(word, !terminal);
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
  // **I el reinici també tanca l'app abans** (0.189.54): the same hole as the way off and the same reason —a restart swaps the
  // PSRAM for a boot and an app that was not closed never said goodbye—, and the picture of the restart is painted after it
  // so the glass ends on the picture (docs/30, docs/40, docs/75).
  if (g_app_alive) close_app();
  splash::show(splash::kReboot);
  out("restarting...\r\n");
  splash::wait();
  // **I les cachés, al disc abans que la PSRAM desaparegui** (0.189.11): a restart is the same loss of memory as a power
  // off, and the picture is already on the glass, so the card is written while a person looks at it. One line per block
  // with the app, the key, the bytes and the path of the file (docs/30, docs/75).
  session::park_all("the board is restarting");
  // **I el mateix que a l'apagada** (0.189.18): the file that is really on the disk, the console, the volumes, the store
  // and the rest of the card —a restart also cuts the boards of the volumes for a moment, and a card that is written and
  // then restarted without its pause is a card that loses the folder it was writing— (docs/70, docs/75).
  const session::Checked parked = session::check_parked();
  log_printf(parked.ok == parked.files ? pal::kLogInfo : pal::kLogWarn, "MEMORY", "%u of %u session file(s) are on the disk",
             static_cast<unsigned>(parked.ok), static_cast<unsigned>(parked.files));
  power::settle_before_leaving("the board is restarting");
  pal::system().reboot();
  return true;
}

bool native_running() { return g_job != RadioJob::kNone || g_install_pending; }

const char* app_id() { return g_app_alive ? luabind::app_id() : ""; }

bool ask_run(const char* id, bool back, const char* flags, char* why, size_t why_cap) {
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
  if (flags != nullptr && strlen(flags) >= sizeof(g_run_flags)) {
    snprintf(why, why_cap, "%s", "switches longer than an app of this system takes");
    return false;
  }
  snprintf(g_run_wanted, sizeof(g_run_wanted), "%s", id);
  g_run_wanted_back = back;
  snprintf(g_run_flags, sizeof(g_run_flags), "%s", (flags != nullptr) ? flags : "");
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
  // **I la pàgina que torna porta la seva targeta** (0.179.33): the launcher arrives with the card of its manifest, because
  // this is the moment it was drawn for —an app just ended and the person is going back to it— and not a boot the board has
  // already shown a picture for (docs/30, docs/40).
  return run_app(back_to, nullptr, false, true);
}

bool app_run_wanted(char* out, size_t cap, bool& back, char* flags, size_t flags_cap) {
  if (g_run_wanted[0] == '\0') return false;
  if (out != nullptr && cap > 0) snprintf(out, cap, "%s", g_run_wanted);
  if (flags != nullptr && flags_cap > 0) snprintf(flags, flags_cap, "%s", g_run_flags);
  back = g_run_wanted_back;
  g_run_wanted[0] = '\0';
  g_run_flags[0] = '\0';
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

void close_app(bool say) {
  if (!g_app_alive) return;
  // **I el que la línia del prompt diu, que és el que aquest paràmetre vol dir** (0.189.175): a page that steps aside for an alarm window
  // is not gone, and the window's line is the one the log gets, o sigui que `say == false` és el camí de l'alarma. **La línia és callada des
  // del 22/11** (l'amo la va deixar comentada perseguint d'on sortia `app closed`), i el paràmetre es queda perquè el camí de l'alarma el
  // passa i el dia que la línia torni ha de tornar amb el mateix significat (docs/30, docs/50).
  (void)say;
  // **I el port sèrie que l'app tenia torna al sistema** (0.189.20, la mateixa regla que el llum): a wire ceded to an app that
  // is not there any more is a port a person cannot get back, and giving it back here —before anything is written— is what
  // makes `app closed` readable at the prompt (docs/30, docs/50).
  (void)serial::release_owner(app_id(), "the app that had it is gone");
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
  // **La caché de l'app va al disc abans que la memòria es torni** (0.189.7): the app id is read **before** the interpreter
  // goes, because the id is the interpreter's, and the blocks of this app are written to `/session/<id>/<key>` so the next
  // run of the app starts warm. **The block stays in the memory**: what a person sees in `sessions` after this is the
  // cache the app built, and the allocator takes it away only when something else does not fit (docs/30, docs/75).
  char parked_id[24];
  snprintf(parked_id, sizeof(parked_id), "%s", app_id());
  // **I el teclat baixa amb la pàgina que el va aixecar** (0.189.21): the band's object is the system's and does not outlive the page
  // that asked for it (`ui::attach` already says it for the next app); with the `serial` keyboard it is not a picture but **a
  // claim on the port**, and one left behind would make the prompt that comes after it deaf. It goes with a live interpreter,
  // so the box it was typing into is forgotten the way it should be (docs/30, docs/50).
  ui::keyboard_hide();
  luabind::stop();
  // **I les dues xifres del desament, separades** (0.189.53): a block whose bytes the disk already has is not written again
  // —the flash is what a session would wear out—, so a close that changed nothing says zero written and a close that saved
  // something says what it saved. Without the second number, «0 block(s)» would read as a failure (docs/30, docs/75).
  const session::Parked flushed = session::flush(parked_id);
  if (flushed.written > 0 || flushed.unchanged > 0) {
    log_printf(pal::kLogInfo, "MEMORY", "%s ended: %u session block(s) written to /session, %u already there", parked_id,
               static_cast<unsigned>(flushed.written), static_cast<unsigned>(flushed.unchanged));
  }
  g_app_alive = false;
  // **I la botiga de claus s'escriu quan una app s'acaba** (0.190.116, l'amo: *«[I][NVS] the store was read at the boot: 0 setting(s)»*).
  // Des de la 0.190.109 els ajustos s'aguanten a la RAM i baixen a la flash a la baixada —que és el que estalvia les pàgines de 4 KB—, però
  // això deixava el cas dolent: canviar mitja dotzena de coses a Settings, reiniciar la placa amb el botó i trobar-se la botiga buida. Tancar
  // una app és el moment en què una persona ha acabat el que feia amb aquella pàgina, o sigui que és **aquí** on el que s'ha canviat passa a la
  // flash: una escriptura per sessió d'ajustos i no una per toc, i res perdut per un reinici (docs/30, docs/70, docs/75).
  if (hal::IKeyValue* store = platform().nvs()) store->commit();
  // **I el dit que hi havia al vidre, o el toc que la cua encara devia, no és de la pàgina que ve** (0.189.100): the same
  // rule as a start, said at the other end —the page that is gone leaves nothing of its own behind for the one that comes,
  // not a finger and not a tap (docs/30, docs/50).
  touch::forget_the_page_before();
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
  // **I la línia del prompt és només per a la pàgina que se'n va de debò** (0.189.175): a page that steps aside for an alarm
  // window is not gone, and the window's line is the one the log gets (docs/30, docs/60).
  //if (say) out("app closed\r\n");
}

bool restore_app(const char* id, const char* path) { return run_app(id, path, true); }

bool start_app(const char* id, bool splash) { return run_app(id, nullptr, false, splash); }

bool raise_app(const char* id, const alarms::Entry* alarm, int64_t late_ms) {
  close_app();
  return run_app(id, nullptr, false, true, alarm, late_ms);
}

// A window of an alarm (docs/60): the app is loaded for one call and thrown
// away. It never runs on top of another app, and its console writes are at the
// foreground prompt because there is no separate log console yet.
bool run_window(const char* id, const alarms::Entry& entry, int64_t late_ms) {
  // **El perquè no s'ha pogut, dit de veritat** (0.189.175): the single sentence `no window (app gone or no on_alarm)` said two
  // things and neither of them was the one that happened —the owner's log of the 18th had the page of the launcher in front
  // and the app well and truly on the disk—, o sigui que cada porta diu la seva i el qui la demana no hi afegeix res
  // (docs/30, docs/60).
  if (g_app_alive) {
    log_printf(pal::kLogWarn, "ALARM", "%u for %s: no window (the page of %s is in front)",
               static_cast<unsigned>(entry.id), id, app_id());
    return false;
  }
  RunTarget target;
  size_t len = 0;
  if (!load_source(id, target, len)) {
    log_printf(pal::kLogWarn, "ALARM", "%u for %s: no window (the app is not on the disk)",
               static_cast<unsigned>(entry.id), id);
    return false;
  }

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
  // **Una finestra d'alarma també sap si l'app és d'administrador** (0.189.99): the machine is built the same way whatever
  // the window is for, and a manifest that says `"admin": true` says it in a window too (docs/30, docs/40, docs/60).
  boot.admin = target.admin;

  const uint32_t limit_kb = platform().heap_limit_kb();
  const size_t limit = (limit_kb > 0) ? (static_cast<size_t>(limit_kb) * 1024) : kDefaultHeapBytes;
  // **I la finestra diu que s'obre** (0.190.65, l'amo: *«Perqué no es veu l'app? l'app no hauría de dir alguna cosa? (lua
  // carregant-lo, la crida, el tancament…) No veig que l'app sigui avisada de res!»*). A window ran a whole app —loaded the text,
  // built the machine, called the hook and destroyed it— and the log only said `1 delivered to battery in a window`: el que la
  // persona necessita és veure **la vida de la finestra** i el que l'app hi diu, i les dues coses es diuen aquí (docs/30, docs/60).
  log_printf(pal::kLogInfo, "ALARM", "a window opens for %s (%lld ms late): loading it", id,
             static_cast<long long>(late_ms));
  // **Obrir una app, mesurada**: the interpreter comes up and the chunk is compiled here, o sigui que el que una persona espera
  // després de prémer una cel·la és aquesta fila i no el metre de l'app, que encara no corre (docs/30, docs/75).
  bool the_interpreter;
  {
    meter::Scope metering(meter::kAppStart);
    the_interpreter = luabind::start(boot, limit);
  }
  if (!the_interpreter) {
    drop_source();  // the text was read and nobody is going to compile it
    log_printf(pal::kLogWarn, "ALARM", "%u for %s: no window (no room for another interpreter)",
               static_cast<unsigned>(entry.id), id);
    return false;
  }
  meter::note_app_start(boot.app_id);
  // **Una finestra no renta el vidre** (0.189.175): the page of the app that stepped aside is still on the glass, and a window
  // that asks for a clean page pays a whole wash —the `a2` the emulator showed— to paint nothing on it. The view is the same
  // and the glass is not touched, which is the rule the window already has: what the hook does not do is draw (docs/30, docs/60).
  ui::attach_under_a_picture();
  // **I la finestra no hereta el tàctil de la pàgina de sota** (0.189.100): the same rule as a start —a finger that is down
  // and the tap the queue still owed are the page's that is being covered, not this one's (docs/30, docs/50).
  touch::forget_the_page_before();
  bar::title(boot.app_title);
  // **And the name before the window costs anything**, the same rule as a start (0.179.27): the title of an alarm
  // window is on the strip while the text of it is being compiled, and not after its page (docs/30, docs/60).
  bar::flush();

  // **I el que l'app diu va al registre i no al prompt** (0.190.65): dins una finestra no hi ha ningú llegint la consola —el
  // prompt és de la persona i el que hi hagués s'hi barrejaria—, i tirar les seves paraules és pitjor que portar-les al registre
  // del sistema, que és el que aquesta casa fa amb una app que no és al davant des de la 0.160.0 (`console_away`, docs/30).
  luabind::set_console_attached(false);
  bool ok = luabind::run_source(target.path, g_source, len);
  // **The text is the interpreter's now**, which has compiled it into its own state: the block goes back before
  // the app runs a single line (docs/20, docs/75).
  drop_source();
  if (ok) {
    // One turn: the entry runs to its first wait or to its end. A window is not a
    // session, and it does not get the keyboard.
    luabind::poll();
    ok = luabind::call_alarm(entry, late_ms);
    // **I la finestra ha pintat el vidre** (0.190.18): the page of an app that is not there any more is what a person would find when the
    // board goes back down to wait, o sigui que el sistema ho apunta per tornar-hi la imatge del son. **Amb el vidre dormit no ha pintat
    // res** (0.190.19): un despert per una alarma de fons deixa la pantalla exactament com era, i apuntar-hi una pàgina faria que la
    // baixada hi tornés a pintar una imatge per res (docs/30, docs/60).
    if (ok && !display::the_glass_is_asleep()) power::note_a_page_on_the_glass();
    log_printf(ok ? pal::kLogInfo : pal::kLogWarn, "ALARM", "%s %s the alarm", id,
               ok ? "answered" : "did not answer");
  } else {
    log_printf(pal::kLogWarn, "ALARM", "%s did not load: the window has no hook to call", id);
  }
  luabind::stop();
  // **I la consola torna a ser del prompt**: the next thing that writes to it is the person (docs/30).
  luabind::set_console_attached(true);
  // **A window is an app too, for the glass** (0.136.0, docs/46 D1): whatever it asked for is given back when it goes,
  // because the page of a window is not a page anybody asked for and the bar is not its (docs/30, docs/60). **I el vidre no
  // es toca**: the page under it is the one of the app that stepped aside, and `give_back` frees the view without painting
  // (0.189.175).
  ui::give_back_the_glass();
  log_printf(pal::kLogInfo, "ALARM", "the window of %s closes", id);
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

// **The same question the road out asks, asked before it is taken** (0.179.25): the panel of the system draws its cross only when
// there is a page that can be left, and a button that would answer «is a launcher» is a button nobody should be offered (docs/30,
// docs/45).
bool the_page_in_front_can_be_left() { return g_app_alive && !app_in_front_is_a_launcher(); }

// **Una placa amb pàgina de casa no es queda mai sense pàgina** (0.189.139): l'amo, amb la placa a la mà —*«al tornar del Sleep
// amb una app en primer pla que no sigui Home, al polsar Back del panell no torna al home, i el sistema es queda sense app al
// foreground i sense possibilitat de continuar fent res, només el reset»*—. The two roads out (○) closed the app, found that nobody had
// asked for another one and left the **console prompt** in front, which on a board with no cable is a glass with a page nobody
// painted: the bar still answers, the card still opens, and every key of it says there is no app to ask —and the only way back was
// the reset. A board that has a page of the start comes back to it, exactly as the boot does (docs/30, docs/40, docs/90).
bool start_the_home_app() {
  const char* home = cfg::home_app();
  if (home == nullptr || home[0] == '\0') return false;
  // A board with no glass has no page to come back to and its prompt is where a person wants to be (docs/50).
  if (display::surface() == nullptr) return false;
  if (!start_app(home, /*splash=*/true)) {
    log_printf(pal::kLogWarn, "HOME", "%s is the page of the start and it did not come up", home);
    return false;
  }
  log_printf(pal::kLogInfo, "HOME", "%s: the page of the start takes the glass back", home);
  return true;
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
  if (start_the_home_app()) return LeaveOutcome::kClosedHome;
  prompt();
  return LeaveOutcome::kClosed;
}

// **Tancar l'app de debò** (0.185.0, l'amo: *«"Close" ha de tancar l'app, no anar enrere, per aixó ja està "Back"»*): the road out
// of a page gives **one page** back and the app stays in front, which is what a pinch means. The cross of the card says «close»
// and closes the **app**, whatever pages it has on top: the views go with it and the page that asked for it takes the glass
// back, which is the return the last page already has. A launcher is refused, because a board with no page at all is a board a
// person has to reach with a cable (docs/30, docs/45).
LeaveOutcome close_the_app() {
  if (!g_app_alive) return LeaveOutcome::kNothing;
  if (app_in_front_is_a_launcher()) {
    log_printf(pal::kLogInfo, "APPS", "%s is a launcher: it is the one page that is not left", app_id());
    return LeaveOutcome::kLauncher;
  }
  log_printf(pal::kLogInfo, "APPS", "%s is closed with %u view(s) on top", app_id(),
             static_cast<unsigned>(apps::views()));
  close_app();
  if (start_the_one_that_asked()) return LeaveOutcome::kClosedHome;
  if (start_the_home_app()) return LeaveOutcome::kClosedHome;
  prompt();
  return LeaveOutcome::kClosed;
}

// **La baixada del sistema, una vegada** (0.185.0): the `poweroff` line of the prompt did this in its own body and the card of
// the panel needs the same four steps —the picture of the moment, the line out, the wait for the glass, and the supply—, so it is
// a function and not a second copy: a power off that skips the wait leaves the last page of somebody's app on the glass (docs/50).
bool power_off(const char* reason) {
  if (reason != nullptr && reason[0] != '\0') log_printf(pal::kLogInfo, "POWER", "power off: %s", reason);
  // **L'app que és al vidre es tanca amb els seus comiats abans que el corrent es talli** (0.189.54): the way off parked the
  // sessions and cut the supply **without closing the app**, so an `on_pause`/`on_save`/`on_close` never ran —the hour of
  // `tz` is written there, and the shape of a page and whatever a page wants to write are too— while
  // `power::deep_sleep_with_the_keys` did close it (0.189.25). The prompt that says `poweroff` has no app in front and this
  // is nothing there. It goes **before the picture**, so the last ink on the glass is still the picture of the moment, and
  // before the margin, because a hook may write (docs/30, docs/40, docs/75).
  if (g_app_alive) close_app();
  // **Una placa que no es pot tallar ho diu abans de gastar el repòs** (0.189.18): the picture, the wait and the margin are
  // for a board that is really going to lose its supply; one that is powered by its cable says so and goes on running.
  const bool can_go = platform().power() != nullptr && platform().power()->can_power_off();
  splash::show(power::going_down_picture(/*deep=*/false));
  pal::IConsole* console = pal::system().console();
  if (console != nullptr) console->flush();  // the line has to be out before the supply goes
  splash::wait();
  // **I totes les sessions van al disc abans que el corrent es talli** (0.189.11): this is the last moment the memory of the
  // board can be read, and a cache that is not written here is a cache lost for ever. The picture is up, so the card is
  // written while a person sees that the board is going down (docs/30, docs/75).
  session::park_all("the board is going off");
  // **I es comprova, i es renta tot el que es pugui rentar, i es reposa** (0.189.18): the number of session files that are
  // really on the disk, the console and the two volumes, the store, and the two seconds a card needs after the last sector
  // —which is the part no call of the filesystem can do for it—. All of it before the supply goes, and only if the supply
  // can go (docs/70, docs/75).
  if (can_go) {
    const session::Checked parked = session::check_parked();
    log_printf(parked.ok == parked.files ? pal::kLogInfo : pal::kLogWarn, "MEMORY",
               "%u of %u session file(s) are on the disk", static_cast<unsigned>(parked.ok),
               static_cast<unsigned>(parked.files));
    power::settle_before_leaving("the board is going off");
  }
  if (!can_go || platform().power() == nullptr || !platform().power()->power_off()) return false;
  shutdown();  // on the emulator it is a process that ends; on the glass the line below is never read
  return true;
}

// **El son que deixa la placa encesa i les tecles vives** (0.189.25, l'amo: *«Fes la B … l'afegeixes al panell desplegable del
// sistema entre el botó de poweroff i el de reboot»*): what a person asks for when they want the page to stay on the glass and
// the board to be there when a key is pressed —no `poweroff`, which leaves a board that only a key of the supply brings back,
// and no `sleep`, which on this board is the same thing with an hour—. The way down is `power::deep_sleep_with_the_keys`, which
// closes the app with its goodbye hooks, writes the sessions, paints the picture of the sleep and puts the chip down with the
// supply **held**; the app comes back at the next boot, because a deep sleep is a reset and not a resume (docs/30, docs/50,
// docs/90).
bool deep_sleep(const char* reason) {
  if (reason != nullptr && reason[0] != '\0') log_printf(pal::kLogInfo, "POWER", "deep sleep: %s", reason);
  // **Una placa que no pot despertar-se amb una tecla ho diu abans de gastar la imatge i el repòs** (0.189.25): the same rule
  // the way off follows with `can_power_off` (0.189.18), so a board without a key of the RTC domain answers now and not after
  // two seconds of a picture that promised a sleep nobody can leave (docs/50, docs/75).
  if (!power::can_deep_sleep()) return false;
  if (!power::deep_sleep_with_the_keys()) return false;
  shutdown();  // on the emulator it is a process that ends; on the glass the line below is never read
  return true;
}

// **Les comandes es fan aquí i no a la tasca de la shell** (0.190.245, l'amo: *«l'app va per el seu fil d'execució i la shell terminal al port
// serie va en un altre. Per això tota execució amb "run ...." o "comanda" ha d'executar-se al fil de sistema»*): the shell of the board only
// reads and echoes, o sigui que el que una persona ha escrit és una línia en una cua i el que la fa és el torn, on viuen el vidre,
// l'intèrpret i l'app. Es buida sencera cada volta i abans de res: una comanda que espera un torn més és una tecla que sembla no haver fet
// res (docs/30, docs/50). `take_a_line` és exactament el camí d'una línia del terminal, o sigui que el que es fa aquí és el que sempre s'hi
// ha fet —inclòs el prompt del final— i no una segona manera de fer comandes.
// **La pàgina de casa és una comanda com qualsevol altra** (0.190.247, l'amo: *«Mira que és fàcil tu!, shell fa "run home_eink &" només entrar i
// santes pàsques!»*): fins avui la començava l'arrencada mateixa (`shell::start_app`), o sigui que la pàgina del sistema i el seu primer dibuix
// passaven dins del `boot` i el cable es quedava sense prompt fins que el vidre havia acabat —segons, en una T5S3—. Ara s'escriu a la cua de
// comandes i el torn la fa: el camí d'una línia escrita a mà, amb el mateix `run_app` i els mateixos comiats.
//
// **I no és `run <id> &`, és `home`** (0.190.249): `run` no té cap manera de dir «sense targeta» i la pàgina de l'arrencada no en vol —el vidre
// ja té la imatge del boot (0.179.33)—, o sigui que la comanda `home` és la que arrenca l'app de `cfg home` **sense targeta i al darrere del
// prompt**, que és exactament el que l'arrencada feia amb `start_app(home, /*splash=*/false)` i el que un `run … &` no sabia dir (docs/30, docs/40).
bool ask_for_the_home_app() {
  const char* home = cfg::home_app();
  if (home == nullptr || home[0] == '\0') return false;
  if (orders::post_a_line("home")) return true;
  // **I si la cua no hi és, es fa aquí mateix**: una arrencada sense pàgina és pitjor que una pàgina sense cua (docs/30).
  g_start_flags[0] = '\0';
  g_start_detached = true;
  const bool started = run_app(home, nullptr, false, /*splash=*/false);
  g_start_detached = false;
  return started;
}

void carry_out_the_orders() {
  char line[orders::kLineBytes];
  while (orders::take_a_line(line, sizeof(line))) take_a_line(line);
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
  // **Amb la banda a la pantalla no es pren** (0.181.4): the owner: *«es segueix tancant als 3 tocs sobre el teclat desplegat»* —
  // three taps of a finger on the keyboard are three letters and not a way out, and the gesture was leaving the app in the middle
  // of a note. The band is the system's own keyboard, so this is the system not taking its way out while it is there; a page with
  // no band keeps it, and the three taps of the middle **key** are a different road and untouched by this one (docs/30).
  // **I una app d'administrador els pot demanar per a ella** (0.189.101, l'amo: *«admin.triple_tap_close(false) — desactiva els 3
  // tocs perque el sistema no li envii app.exit()»*): with the gesture left in the queue the page's `touch.gesture()` gives it
  // a `triple`, which is what a lock screen counts to open; the road out of that page is then its own, and the three presses
  // of the key are still the system's (docs/30, docs/46 D1).
  if (ui::three_taps_close()) {
    touch::Gesture triple{};
    // The gesture is taken out of the queue in both cases: three taps on a keyboard are not an app's gesture either, and the
    // triple that arrives after the band is a gesture of its own and not a leftover of the ones typed on the keyboard (docs/30).
    if (touch::take_gesture_of("triple", triple) && !ui::keyboard_visible()) {
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
    char switches[sizeof(g_run_flags)];
    bool back = false;
    if (app_run_wanted(wanted, sizeof(wanted), back, switches, sizeof(switches))) {
      // **Els commutadors del desig, i només d'aquest arrencada** (0.189.161): the start of the app that was asked for reads
      // them where the manifest's `opensArgs` go, and a wish without any leaves the manifest's own in place (docs/30).
      snprintf(g_start_flags, sizeof(g_start_flags), "%s", switches);
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
  // **El perquè, copiat abans que la màquina desaparegui** (0.189.162): the interpreter that said the error is destroyed two lines
  // below, so the words and the traceback are taken here —and the name of the app with them, because `app_id()` answers nothing
  // once the app is no longer in front— (docs/30, docs/50).
  char who[40];
  char why[520];  // the size of the text the interpreter keeps (kErrorTextCap and the traceback)
  who[0] = '\0';
  why[0] = '\0';
  if (died) {
    snprintf(who, sizeof(who), "%s", app_id());
    snprintf(why, sizeof(why), "%s", luabind::last_error_text());
  }
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
  // **I el perquè al vidre, que és el que una persona té davant** (0.189.162, l'amo: *«Si peta una app, que menys que saber el
  // perqué amb la pantalla i els dits»*): a board with a glass paints the page of the error —the app, the message and the
  // traceback— and waits for a finger or a key; a board with no glass says the line and goes on, which is all it ever had
  // (docs/30, docs/50).
  if (died && why[0] != '\0') {
    ui::give_back_the_glass();
    app_error::show(who, why);
  }
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

// **El temporitzador de la sessió** (0.190.184, l'amo: *«si no s'utilitza durant 5 minuts el shell es tanca (logout/exit) i si volen el shell
// haurán de polsar <enter>»*): el torn la mira, i només al cable. Una app al davant no compta com a inactivitat —el prompt no és el que la
// persona està fent servir—, i una sessió que ja és tancada no té res a tancar (docs/30, docs/60).
void poll_idle() {
  if (!g_session_open || !a_wire_console() || g_app_alive) return;
  if (g_last_wire_activity_ms == 0) {
    g_last_wire_activity_ms = pal::system().millis();
    return;
  }
  if (pal::system().millis() - g_last_wire_activity_ms < kConsoleSessionIdleMs) return;
  close_console_session();
}

void start() {
  g_editor.clear();
  g_last_was_cr = false;
  g_app_alive = false;
  // **A start is a session** (0.158.0): a board that has just booted has its console open, whatever a session
  // before it did —an emulator that re-boots in a test would otherwise stay closed— (docs/30, docs/85).
  //
  // **I en un cable no, i això és el que fa que el pany serveixi** (0.190.184, l'amo: *«El parany: «sessió oberta» és sempre <- Fes que al
  // iniciar faci lo del <enter>»*). Si una placa arrenca amb la sessió oberta, el prompt existeix des del primer segon i el xip no podria fer
  // mai una becaina: el pany es quedaria agafat per sempre i el temporitzador de cinc minuts només s'encendria per tornar-lo a agafar. O sigui
  // que **el cable arrenca tancat** —el xip pot dormir de seguida, que és el que una placa amb el cable posat i ningú al davant ha de fer— i
  // qui vol el prompt prem Enter. A l'emulador i a la consola del vidre no: no hi ha cable, no es perd cap caràcter i un run de prova no ha de
  // començar prement Enter (docs/30, docs/50, docs/85).
  g_session_open = !a_wire_console();
  // **El color no depèn de la sonda, i això és un canvi de criteri** (0.190.231, l'amo: *«NI UN PUTO COLOR»* amb el `pio device monitor
  // --filter direct` dins del VS Code). La 0.190.226 posava `ansi off` quan la sonda callava, amb el raonament que un monitor que no contesta
  // no pot rebre codis — i és fals: **contestar la sonda i pintar els codis són dues coses diferents**, i el monitor de `pio` no pot contestar
  // mai perquè es menja les respostes del terminal (`getkey()` les llegeix com si fossin fletxes). El que decideix el color és **el que la
  // persona va triar** (`cfg/ansi`, encès de sèrie) i prou; `ansi off` és la porta per a un monitor que escriu els codis com a lletres
  // (docs/30, docs/50, docs/85).
  {
    pal::IConsole* console = pal::system().console();
    g_ansi = cfg::ansi_on() && console != nullptr && console->speaks_ansi();
    if (g_ansi && !console->has_a_line_editor()) {
      log_printf(pal::kLogInfo, "CONSOLE",
                 "the other end does not answer as a terminal (no cursor, no history); the codes still go out -- `ansi off` filters them");
    }
  }
  // **I el mirall del registre, al que és la consola i no al que és el cable** (0.190.185, l'amo: *«fes que el log no s'envii al serie»*).
  // Es decideix aquí i no a `log_init()` perquè aquest és el moment en què hi ha consola de debò —un run de prova de l'emulador en té i una
  // placa també— i perquè un `start()` que no deixés el mirall com toca el deixaria encès d'un run anterior (docs/30, docs/50).
  g_log_follow = false;
  log_echo(!a_wire_console());
  g_last_wire_activity_ms = pal::system().millis();
  sync_the_wire_lock();
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
  // **No es pregunta res al terminal** (0.190.92): la línia es redibuixa amb espais i retrocessos i el registre va en text pla, o sigui
  // que el que hi hagi a l'altra banda no canvia el que aquesta casa escriu (docs/85).
  newline();
  if (g_session_open) {
    prompt();
  } else {
    // **I es diu com s'entra** (0.190.184): the same sentence as a logout, perquè una consola en silenci que no contesta és el pitjor que li
    // pot passar a una placa que només es pot fer servir per aquest cable (docs/30, docs/50).
    out("the session is closed: press Enter to open one\r\n");
  }
}

// cfg: the settings of the system and where the settings of the apps are kept. `/cfg` is a door and not a disk, so the
// person has to be able to see which unit is behind it and to move it; the choice is saved in the key-value store and
// survives a reboot. **The keyboard is the other setting** (0.93.0): the id of one of the children of the UI, chosen
// here and read at boot (docs/30, docs/70).
void say_keyboards() {
  // **A board with no glass has no band of keys and says so** (0.179.9): `cfg keyboard` on one of the four boards without a
  // panel used to end its sentence with the word `are` and nothing after it, which reads as a list that failed to load
  // (`LUNOK_GLASS`, docs/30, docs/75).
  if (ui::keyboard_count() == 0) {
    out("cfg: this build carries no keyboard\r\n");
    return;
  }
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
  // **A board with no glass paints no picture at the boot**, so there is no word to list and the sentence says that
  // instead of ending in a colon with nothing behind it (`LUNOK_GLASS`, docs/30, docs/50).
  if (splash::count() == 0) {
    out("cfg: this build paints nothing at the boot: the board has no glass\r\n");
    return;
  }
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
  // **Una llista d'ús i no dues** (0.190.186): the settings door had two, and the one that answers a line too long had lost `idle` and `tz`
  // while the other kept them — una llista que es contradiu amb ella mateixa i que la persona llegeix precisament quan la comanda no ha
  // entès el que li han dit (docs/30).
  static const char* const kUsage =
      "usage: cfg [auto|sd|littlefs|keyboard [<id>]|boot [<word>]|home [<id>|none]|tz [<offset>]|lock [<id>|none]|"
      "leap [on|off]|dst [on|off]|week [monday|sunday]|sort [on|off]|turn [<instructions>]|freq [<mhz> on|off]|"
      "settings [<id>|none]|lockon [on|off]|lockafter [5|10|15]|idle [5|10|15]|nap [on|off]|wifi [on|off]|"
      "bt [on|off]|cursor delay [<ms>]]\r\n";
  char words[3][kWordCap];
  size_t count = 0;
  if (!read_words(argument, words, 3, count)) return false;
  if (count > 3) {
    usage_out(kUsage);
    return false;
  }
  // **El parpalleig del cursor** (0.189.34, l'amo: *«Crea una opcio nova de cfg anomenada "cursor delay"… Aquest valor es
  // comú a tots els cursors del sistema»*): the option is two words and the number is a third, so it is asked before the
  // rest of the door and with the whole argument in hand —`cfg cursor delay` says what it is, `cfg cursor delay 850` writes
  // it, and a number that is not a blink is refused with the two ends of the range— (docs/30, docs/90).
  if (strcmp(words[0], "cursor") == 0) {
    if (count >= 2 && strcmp(words[1], "delay") != 0) {
      usage_out("usage: cfg cursor delay [<ms>]\r\n");
      return false;
    }
    if (count == 3) {
      int32_t delay = 0;
      if (!cfg::parse_cursor_delay(words[2], delay)) {
        out("cfg: cursor delay is ");
        out_num(cfg::cursor_delay_default());
        out(" ms by default, and a blink goes from ");
        out_num(luabind::caret_blink_min_ms());
        out(" to ");
        out_num(luabind::caret_blink_max_ms());
        out(" ms\r\n");
        return false;
      }
      const bool kept = cfg::set_cursor_delay(delay);
      out("settings: cursor delay ");
      out_num(cfg::cursor_delay());
      out(" ms\r\n");
      if (!kept) out("cfg: the choice lives until the board restarts (there is no key-value store)\r\n");
      return true;
    }
    out("settings: cursor delay ");
    out_num(cfg::cursor_delay());
    out(" ms (this build's default is ");
    out_num(cfg::cursor_delay_default());
    out(")\r\n");
    return true;
  }

  // **`cfg btmode [keyboard|mouse|both]`** (0.190.208, l'amo: *«com escullo el mouse? vull usar el panell tàctil com a mouse bluetooth»*):
  // què és aquesta placa per a un altre aparell, dit pel prompt i **desat**, perquè és una tria de la placa i no d'una tarda. És el que fan
  // els tres botons de l'app `bthid`, i ho mana el mateix lloc: `cfg::set_bt_mode` (docs/30, docs/50). **Va aquí i no dins del bloc de
  // dues paraules** perquè preguntar-ho és una paraula sola, i un `cfg btmode` que no digués res seria una comanda que no contesta.
  if (strcmp(words[0], "btmode") == 0 && count <= 2) {
    if (count == 1) {
      if (cfg::bt_mode()[0] == '\0') {
        out("settings: nothing has been chosen for this board yet (btmode keyboard|mouse|both)\r\n");
        return true;
      }
      out("settings: this board is ");
      out(device_phrase());
      out(" (btmode ");
      out(cfg::bt_mode());
      out(")\r\n");
      return true;
    }
    if (!bt::available()) {
      out("cfg: this board has no radio, and the choice is kept all the same\r\n");
    }
    if (cfg::set_bt_mode(words[1]) == nullptr) {
      out("cfg: this board is a keyboard, a mouse or both\r\n");
      return false;
    }
    out("settings: this board is ");
    out(device_phrase());
    newline();
    out("  pair it again from the other side: it read the map of the other one\r\n");
    return true;
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
    // **L'app que obre el botó dels ajustos del panell** (0.189.109, l'amo: *«Enllaça el botó de Settings del panell desplegable de
    // sistema amb l'app <id: settings> Si no està l'app no passsa res»*): the same door as the app of the start —the name is checked
    // against the apps of this board, so a typo is refused here and not discovered by a gear that opens nothing— and `none` is the
    // word that says the gear does nothing at all (docs/30, docs/40).
    if (strcmp(words[0], "settings") == 0) {
      if (strcmp(words[1], "none") == 0) {
        const bool kept = cfg::set_settings_app(nullptr);
        out("settings: the gear of the panel opens nothing\r\n");
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
      const bool kept = cfg::set_settings_app(entry.id);
      out("settings: the gear of the panel opens ");
      out(cfg::settings_app());
      newline();
      if (!kept) out("cfg: the choice lives until the board restarts (there is no key-value store)\r\n");
      return true;
    }
    // **La pàgina que tapa el vidre** (0.189.150): the same door as the app of the start and the gear —the name is checked
    // against the apps of this board, so a typo is refused where it is typed— and `none` is the word that leaves the screen
    // with no lock at all, which is also what the panel key of Settings does when nothing is chosen (docs/30, docs/40).
    if (strcmp(words[0], "lock") == 0) {
      if (strcmp(words[1], "none") == 0) {
        const bool kept = cfg::set_lock_app(nullptr);
        out("settings: the screen has no lock page\r\n");
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
      const bool kept = cfg::set_lock_app(entry.id);
      out("settings: the screen locks with ");
      out(cfg::lock_app());
      newline();
      if (!kept) out("cfg: the choice lives until the board restarts (there is no key-value store)\r\n");
      return true;
    }
    // **`cfg lockon on|off`**: the switch of the Privacy tab, which is what makes the page come up at the start and after a
    // sleep and what arms the idle counter (0.189.150, docs/30, docs/60).
    if (strcmp(words[0], "lockon") == 0) {
      bool on = false;
      if (strcmp(words[1], "on") == 0) {
        on = true;
      } else if (strcmp(words[1], "off") != 0) {
        out("cfg: the lock of the screen is on or off\r\n");
        return false;
      }
      const bool kept = cfg::set_lock_on(on);
      out("settings: lock ");
      out(on ? "on" : "off");
      newline();
      if (!kept) out("cfg: the choice lives until the board restarts (there is no key-value store)\r\n");
      return true;
    }
    // **`cfg sleep on|off`**: the switch of the System tab, which is whether the board powers itself off until its next alarm when
    // nobody has touched it (0.190.5, l'amo: *«On es l'opció a Settings per desactivar-ho?»*). It is kept, or the answer would not
    // survive the boot that the same policy is about, and `power idle` (a switch of the run) is the other door: this one is what a
    // board remembers (docs/30, docs/60).
    if (strcmp(words[0], "sleep") == 0) {
      bool on = false;
      if (strcmp(words[1], "on") == 0) {
        on = true;
      } else if (strcmp(words[1], "off") != 0) {
        out("cfg: the sleep of the board is on or off\r\n");
        return false;
      }
      const bool kept = cfg::set_sleep_on(on);
      out("settings: sleep ");
      out(on ? "on" : "off");
      newline();
      if (!kept) out("cfg: the choice lives until the board restarts (there is no key-value store)\r\n");
      return true;
    }
    // **`cfg touchwake on|off`**: the third door of a deep sleep —a finger on the glass wakes the board—, which is off by default because
    // hearing a finger means keeping the panel's rail and its touch controller awake for the whole sleep (0.190.16, docs/30, docs/60).
    if (strcmp(words[0], "touchwake") == 0) {
      bool on = false;
      if (strcmp(words[1], "on") == 0) {
        on = true;
      } else if (strcmp(words[1], "off") != 0) {
        out("cfg: the glass of the sleep is on or off\r\n");
        return false;
      }
      const bool kept = cfg::set_wake_on_touch(on);
      out("settings: touch to wake ");
      out(cfg::wake_on_touch() ? "on" : "off");
      newline();
      // **I si aquesta placa no hi sent, es diu una vegada i no és cap error** (0.190.17): the setting is kept like on any other board and the
      // board does nothing with it, which is the difference between ignoring a switch and refusing it (docs/30, docs/50).
      if (!power::can_wake_on_touch()) out("cfg: this board has no glass to wake it, and the setting is kept all the same\r\n");
      if (!kept) out("cfg: the choice lives until the board restarts (there is no key-value store)\r\n");
      return true;
    }
    // **`cfg loglevel debug|info|warn|error`** (0.190.89, l'amo: *«Afegeix "Log level" a Settings->System Amb els nivells de log, per
    // defecte Debug (com la comanda log)»*): the same setting as the row of the System tab, said by the prompt i aplicada en el mateix
    // moment; el que no és cap de les quatre paraules es refusa i el registre es queda com era (docs/30, docs/50).
    if (strcmp(words[0], "loglevel") == 0) {
      pal::LogLevel at = pal::kLogDebug;
      if (!log_level_from_name(words[1], &at)) {
        out("cfg: the log is debug, info, warn or error\r\n");
        return false;
      }
      const bool kept = cfg::set_log_level(words[1]);
      out("settings: the log is ");
      out(cfg::log_level());
      newline();
      if (!kept) out("cfg: the choice lives until the board restarts (there is no key-value store)\r\n");
      return true;
    }
    // **`cfg swbutton on|off`** (0.190.110, l'amo: *«Afegeix a Settings->Input un Toggle per habilitar-lo o deshabilitar-lo "Use software
    // button"»*): el botó de sota el vidre, dit pel prompt. Encès, el tàctil en fa una tecla més; apagat, aquella zona torna a ser de la pàgina
    // (docs/30, docs/50).
    if (strcmp(words[0], "swbutton") == 0) {
      bool on = false;
      if (strcmp(words[1], "on") == 0) {
        on = true;
      } else if (strcmp(words[1], "off") != 0) {
        out("cfg: the button under the glass is on or off\r\n");
        return false;
      }
      const bool kept = cfg::set_software_button(on);
      out("settings: the button under the glass ");
      out(cfg::software_button() ? "on" : "off");
      newline();
      if (!button::software_button_exists()) out("cfg: this case has no button under the glass, and the setting is kept all the same\r\n");
      if (!kept) out("cfg: the choice lives until the board restarts (there is no key-value store)\r\n");
      return true;
    }
    // **`cfg dimidle on|off`** (0.190.118, l'amo: *«Aquest en 1 minut que no es toqui l apantalla el backlight s'apaga i es torna a encendre quan
    // toquen el panell»*): the switch of the System tab, said by the prompt. El minut no es toca des d'aquí —és del sistema— i apagar el
    // commutador amb el vidre fosc el torna a encendre a l'instant (docs/30, docs/50).
    if (strcmp(words[0], "dimidle") == 0) {
      bool on = false;
      if (strcmp(words[1], "on") == 0) {
        on = true;
      } else if (strcmp(words[1], "off") != 0) {
        out("cfg: the light goes out when nobody looks, on or off\r\n");
        return false;
      }
      const bool kept = cfg::set_dim_when_idle(on);
      if (!on) led::wake_the_light();
      out("settings: the light goes out when nobody looks ");
      out(cfg::dim_when_idle() ? "on" : "off");
      newline();
      if (!led::is_a_backlight()) out("cfg: this board has no light for the glass, and the setting is kept all the same\r\n");
      if (!kept) out("cfg: the choice lives until the board restarts (there is no key-value store)\r\n");
      return true;
    }
    // **`cfg breathe on|off`** (0.190.162, l'amo: *«Afegeix un Toggle a l'app Settings->Battery que digui "Breathe while charging"»*): el
    // mateix ajust que la fila de la pestanya Battery, dit pel prompt. El respir mateix no es toca des d'aquí —el carregador és qui diu si hi
    // ha corrent i el nucli qui el fa—, o sigui que el que aquesta línia canvia és si el vidre el fa o no (docs/30, docs/50).
    if (strcmp(words[0], "breathe") == 0) {
      bool on = false;
      if (strcmp(words[1], "on") == 0) {
        on = true;
      } else if (strcmp(words[1], "off") != 0) {
        out("cfg: the glass breathes on a cable, on or off\r\n");
        return false;
      }
      const bool kept = cfg::set_breathe_while_charging(on);
      out("settings: the glass breathes while charging ");
      out(cfg::breathe_while_charging() ? "on" : "off");
      newline();
      if (!led::is_a_backlight()) out("cfg: this board has no light for the glass, and the setting is kept all the same\r\n");
      if (!kept) out("cfg: the choice lives until the board restarts (there is no key-value store)\r\n");
      return true;
    }
    // **`cfg announce on|off`** (0.190.165, l'amo: *«un toggle a Settings->System "Announce charge"»*): la imatge del cable quan s'endolla, dita
    // pel prompt. El flanc el mira el nucli cada dos segons i el que aquesta línia canvia és si la placa l'ensenya (docs/30, docs/50).
    if (strcmp(words[0], "announce") == 0) {
      bool on = false;
      if (strcmp(words[1], "on") == 0) {
        on = true;
      } else if (strcmp(words[1], "off") != 0) {
        out("cfg: the glass says the charge when a cable comes in, on or off\r\n");
        return false;
      }
      const bool kept = cfg::set_announce_charge(on);
      out("settings: the glass says the charge ");
      out(cfg::announce_charge() ? "on" : "off");
      newline();
      if (!kept) out("cfg: the choice lives until the board restarts (there is no key-value store)\r\n");
      return true;
    }
    // **`cfg touchscan on|off`** (0.190.107, l'amo: *«afegeix a Settings->Input un toggle que posi "Adaptative touch scan"»*): el mateix
    // ajust que la fila de la pestanya Input, dit pel prompt i aplicat al vidre en el mateix moment. Una placa sense vidre ho diu i el valor
    // es queda desat igualment (docs/30, docs/50).
    if (strcmp(words[0], "touchscan") == 0) {
      bool on = false;
      if (strcmp(words[1], "on") == 0) {
        on = true;
      } else if (strcmp(words[1], "off") != 0) {
        out("cfg: the scan of the glass is adaptive or at its top speed (on or off)\r\n");
        return false;
      }
      const bool kept = cfg::set_touch_adaptive(on);
      touch::set_adaptive_scan(on);
      out("settings: adaptive touch scan ");
      out(touch::adaptive_scan() ? "on" : "off");
      newline();
      if (!touch::available()) out("cfg: this board has no glass, and the setting is kept all the same\r\n");
      if (!kept) out("cfg: the choice lives until the board restarts (there is no key-value store)\r\n");
      return true;
    }
    // **`cfg backlight on|off` i `cfg backlight <1..max>`** (0.190.106, l'amo: *«Afegeix a Settings->System un toggle amb "Use backlight" i un
    // selector desplegable amb tots els valors possibles del backlight»*): the same two settings as the rows of the System tab, said by the
    // prompt and applied on the spot —the level goes to the light of the glass and not to a light that says things—. Una placa sense cap
    // llum declarada com a retroil·luminació ho diu i el valor es queda desat igualment (docs/30, docs/50).
    if (strcmp(words[0], "backlight") == 0) {
      if (strcmp(words[1], "on") == 0 || strcmp(words[1], "off") == 0) {
        const bool kept = cfg::set_backlight(strcmp(words[1], "on") == 0);
        led::apply_the_backlight();
        out("settings: the light of the glass is ");
        out(cfg::backlight() ? "on" : "off");
        newline();
        if (!led::is_a_backlight()) out("cfg: this board has no light for the glass, and the setting is kept all the same\r\n");
        if (!kept) out("cfg: the choice lives until the board restarts (there is no key-value store)\r\n");
        return true;
      }
      const long asked = strtol(words[1], nullptr, 10);
      // **El sostre d'una placa sense llum és dos-cents cinquanta-cinc** (0.190.111): the setting is kept on every board —it is a preference and
      // not a promise—, o sigui que una placa que no té cap llum no ha de refusar el número que una persona escriu; el que ha de dir és que no
      // hi ha res a il·luminar, i això es diu a sota (docs/30, docs/50).
      const long ceiling = (led::max_value() > 0) ? static_cast<long>(led::max_value()) : 255;
      if (asked < 1 || asked > ceiling) {
        out("cfg: the light of the glass is a level from 1 to ");
        out_num(static_cast<uint32_t>(ceiling));
        newline();
        return false;
      }
      const bool kept = cfg::set_backlight_level(static_cast<uint8_t>(asked));
      led::apply_the_backlight();
      out("settings: the light of the glass is at ");
      out_num(cfg::backlight_level());
      newline();
      if (!led::is_a_backlight()) out("cfg: this board has no light for the glass, and the setting is kept all the same\r\n");
      if (!kept) out("cfg: the choice lives until the board restarts (there is no key-value store)\r\n");
      return true;
    }
    // **`cfg nap on|off`**: les esperes del sistema, passades dormint o despert (0.190.63, l'amo: *«[X] Light sleep instead of wait. Aplica
    // lightSleep allá on puguis»*). És el mateix ajust que la fila de la pestanya Battery, i la resposta diu també què en fa aquesta placa:
    // una que no en sap espera com sempre (docs/30, docs/50).
    if (strcmp(words[0], "nap") == 0) {
      bool on = false;
      if (strcmp(words[1], "on") == 0) {
        on = true;
      } else if (strcmp(words[1], "off") != 0) {
        out("cfg: the nap of the waits is on or off\r\n");
        return false;
      }
      const bool kept = cfg::set_nap_on(on);
      out("settings: light sleep instead of wait ");
      out(cfg::nap_on() ? "on" : "off");
      newline();
      if (!power::can_nap()) out("cfg: this board does not take a light sleep, and the setting is kept all the same\r\n");
      if (!kept) out("cfg: the choice lives until the board restarts (there is no key-value store)\r\n");
      return true;
    }
    // **`cfg bt on|off`**: el teclat de Bluetooth (0.190.180). És el mateix ajust que la fila de Settings, i el que fa és obrir o tancar el
    // ràdio de debò; **mentre és encès la placa no fa becaines**, o sigui que la resposta ho diu (docs/30, docs/50, docs/60).
    if (strcmp(words[0], "bt") == 0) {
      bool on = false;
      if (strcmp(words[1], "on") == 0) {
        on = true;
      } else if (strcmp(words[1], "off") != 0) {
        out("cfg: the keyboard of the board is on or off\r\n");
        return false;
      }
      const bool kept = cfg::set_bt_on(on);
      out("settings: bluetooth keyboard ");
      out(cfg::bt_on() ? "on" : "off");
      newline();
      if (!bt::available()) out("cfg: this board has no radio, and the setting is kept all the same\r\n");
      if (!kept) out("cfg: the choice lives until the board restarts (there is no key-value store)\r\n");
      return true;
    }
    // **`cfg wifi on|off`** (0.190.221, l'amo: *«Es ben senzill... igual que el cfg/bt cfg/wifi on/off. Si actives la wifi (passes a "on") està
    // apagada en aquell moment, s'inicialitza»*): el commutador de la ràdio del wifi, el mateix que la clau de la targeta de la barra. Encendre
    // vol dir el driver enlaire i l'estació començada —no connectat: aquest sistema usa la xarxa i la deixa anar quan la feina s'acaba— i el
    // que es diu és el que la ràdio ha fet i no el que se li ha demanat (docs/30, docs/50).
    if (strcmp(words[0], "wifi") == 0) {
      bool on = false;
      if (strcmp(words[1], "on") == 0) {
        on = true;
      } else if (strcmp(words[1], "off") != 0) {
        out("cfg: the wifi radio is on or off\r\n");
        return false;
      }
      const bool kept = cfg::set_wifi_on(on);
      out("settings: wifi radio ");
      out(cfg::wifi_on() ? "on" : "off");
      newline();
      if (!net::available()) out("cfg: this board has no wifi radio, and the setting is kept all the same\r\n");
      if (!kept) out("cfg: the choice lives until the board restarts (there is no key-value store)\r\n");
      return true;
    }
    // **`cfg uartwake on|off`**: si la consola del darrere desperta la becaina (0.190.147). És el mateix ajust que la fila `Wake from
    // UART` de la pestanya Battery, i la resposta diu també què en fa aquesta placa (docs/30, docs/50).
    if (strcmp(words[0], "uartwake") == 0) {
      bool on = false;
      if (strcmp(words[1], "on") == 0) {
        on = true;
      } else if (strcmp(words[1], "off") != 0) {
        out("cfg: wake from the console is on or off\r\n");
        return false;
      }
      const bool kept = cfg::set_uart_wake(on);
      out("settings: wake from the console ");
      out(cfg::uart_wake() ? "on" : "off");
      newline();
      if (!kept) out("cfg: the choice lives until the board restarts (there is no key-value store)\r\n");
      return true;
    }
    // **`cfg lockafter <5|10|15>`**: the minutes without a hand on the board. The three of the list and nothing else, perquè
    // és una tria d'una pàgina i no un número que es pugui inventar (0.189.150, docs/30, docs/60).
    if (strcmp(words[0], "lockafter") == 0) {
      const int minutes = atoi(words[1]);
      if (!cfg::set_lock_after_minutes(static_cast<uint8_t>(minutes))) {
        out("cfg: lock after is 5, 10 or 15 minutes (now ");
        out_num(cfg::lock_after_minutes());
        out(")\r\n");
        return false;
      }
      out("settings: lock after ");
      out_num(cfg::lock_after_minutes());
      out(" minutes\r\n");
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
    // **I l'hora d'estiu** (0.182.0): the box of the Time zone page has the same setting and the same two words, so the prompt and
    // the app never disagree about whether the board is in summer (docs/30, docs/60).
    if (strcmp(words[0], "dst") == 0) {
      bool on = false;
      if (strcmp(words[1], "on") == 0) {
        on = true;
      } else if (strcmp(words[1], "off") != 0) {
        out("cfg: the hour of summer is on or off\r\n");
        return false;
      }
      const bool kept = cfg::set_dst(on);
      out("settings: dst ");
      out(cfg::dst() ? "on" : "off");
      newline();
      if (!kept) out("cfg: the choice lives until the board restarts (there is no key-value store)\r\n");
      return true;
    }
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
    // **L'ordre de les apps d'una pàgina d'inici** (0.189.13): the launcher draws the apps in the order the door gives
    // them, and this switch is the person's word for it to draw them by how many times they have been opened. The setting
    // is read by the page (`sys.settings()`, docs/30) and the counting is the page's own session (docs/75); here it is
    // only the switch, and `cfg` stays the one door that writes a setting of the system.
    if (strcmp(words[0], "sort") == 0) {
      bool on = false;
      if (!cfg::parse_switch(words[1], on)) {
        out("cfg: sort apps by usage is on or off\r\n");
        return false;
      }
      const bool kept = cfg::set_sort_by_usage(on);
      out("settings: sort apps by usage ");
      out(cfg::sort_by_usage() ? "on" : "off");
      newline();
      if (!kept) out("cfg: the choice lives until the board restarts (there is no key-value store)\r\n");
      return true;
    }
    // **Les velocitats que la política pot fer servir** (0.189.107, l'amo: *«[X] Frequency scaling allowed: [X] 80Mhz
    // [X] 160Mhz [X] 240Mhz»*): the three steps of this chip and the person's choice of which ones the governor may move
    // between. `cfg freq` says them, `cfg freq 240 off` takes the ceiling down to 160 and `cfg freq all` gives it back; the
    // step that is not allowed is refused here, so a `power cpu 240` that lands on 160 is not a mystery (docs/50, docs/60).
    if (strcmp(words[0], "freq") == 0) {
      char text[12] = {0};
      cfg::freq_text(text, sizeof(text), cfg::freq_mask());
      if (count == 1) {
        out("settings: cpu allows ");
        out(text);
        out(" MHz (the running step is ");
        out_num(power::cpu_mhz());
        out(")\r\n");
        return true;
      }
      if (count == 2 && (strcmp(words[1], "all") == 0 || strcmp(words[1], "auto") == 0)) {
        uint8_t mask = 0;
        cfg::parse_freq(words[1], mask);
        // **Dos i no tres** (0.190.175): el terra i el sostre; el mig el decideix el xip (docs/50, docs/60).
        const bool kept = cfg::set_freq_allowed(80, true) && cfg::set_freq_allowed(240, true);
        cfg::freq_text(text, sizeof(text), cfg::freq_mask());
        out("settings: cpu allows ");
        out(text);
        out(" MHz\r\n");
        if (!kept) out("cfg: the choice lives until the board restarts (there is no key-value store)\r\n");
        return true;
      }
      if (count == 2) {
        uint8_t mask = 0;
        if (!cfg::parse_freq(words[1], mask)) {
          out("cfg: the steps of this board are 80, 160 and 240 MHz, or `all`\r\n");
          return false;
        }
        // **Una llista el lleva tots i deixa aquests**, que és el que una persona vol dir amb `cfg freq 160` (docs/50).
        for (uint8_t step = 0; step < 3; ++step) {
          const uint32_t mhz = (step == 0) ? power::kIdleMhz : ((step == 1) ? power::kRestingMhz : power::kBusyMhz);
          cfg::set_freq_allowed(mhz, (mask & (1u << step)) != 0);
        }
        cfg::freq_text(text, sizeof(text), cfg::freq_mask());
        out("settings: cpu allows ");
        out(text);
        out(" MHz\r\n");
        return true;
      }
      // `cfg freq <mhz> on|off`
      bool on = false;
      if (!cfg::parse_switch(words[2], on)) {
        out("cfg: freq takes on or off for a step\r\n");
        return false;
      }
      uint8_t mask = 0;
      if (!cfg::parse_freq(words[1], mask)) {
        out("cfg: the steps of this board are 80, 160 and 240 MHz\r\n");
        return false;
      }
      // **Una llista de dues paraules és un sol pas**: `cfg freq 80 240 on` no vol dir res, i es refusa en lloc d'endevinar-ho.
      uint32_t mhz = 0;
      for (uint8_t step = 0; step < 3; ++step) {
        if ((mask & (1u << step)) == 0) continue;
        if (mhz != 0) {
          out("cfg: one step at a time, like `cfg freq 240 off`\r\n");
          return false;
        }
        mhz = (step == 0) ? power::kIdleMhz : ((step == 1) ? power::kRestingMhz : power::kBusyMhz);
      }
      if (mhz == 0 || !cfg::set_freq_allowed(mhz, on)) {
        out("cfg: that would leave the processor with no step at all\r\n");
        return false;
      }
      cfg::freq_text(text, sizeof(text), cfg::freq_mask());
      out("settings: cpu allows ");
      out(text);
      out(" MHz\r\n");
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
    // **Els tres temps, en minuts i sense traducció** (0.190.186, l'amo: el help deia *«in seconds: five minutes»*, o sigui que la porta demanava
    // una cosa i el text n'explicava una altra): the three are the five, ten and fifteen of the row of Battery and the same three of `cfg
    // lockafter`, and a person says them in minutes. **Els segons encara s'hi accepten** perquè són el que escriu `cfg.idle` des de Lua i
    // el que ja tenen als dits qui van llegir el help vell (docs/30, docs/60).
    if (strcmp(words[0], "idle") == 0) {
      const int asked = atoi(words[1]);
      const int seconds = (asked == 5 || asked == 10 || asked == 15) ? asked * 60 : asked;
      if (seconds != 300 && seconds != 600 && seconds != 900) {
        out("cfg: the board sleeps after 5, 10 or 15 minutes of quiet\r\n");
        return false;
      }
      const bool kept = cfg::set_idle_seconds(static_cast<uint16_t>(seconds));
      out("settings: the board sleeps after ");
      out_num(static_cast<uint32_t>(cfg::idle_seconds() / 60));
      out(" minutes of quiet\r\n");
      if (!kept) out("cfg: the choice lives until the board restarts (there is no key-value store)\r\n");
      return true;
    }
    if (strcmp(words[0], "keyboard") != 0 && strcmp(words[0], "tz") != 0) {
      usage_out(kUsage);
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
    // **L'app del botó dels ajustos, i què diu aquest build** (0.189.109): the two answers a person wants to tell apart, like the
    // app of the start (docs/30, docs/40).
    if (strcmp(words[0], "settings") == 0) {
      out("settings: the gear of the panel opens ");
      out((cfg::settings_app()[0] != '\0') ? cfg::settings_app() : "none");
      out(" (this board's default is ");
      out(cfg::settings_default());
      out(")\r\n");
      return true;
    }
    // **Quanta quietud abans del son** (0.190.9, l'amo: *«afegir selector de temps per l'idle de settings->battery (per fer el
    // DeepSleep)»*): the seconds of the three the page offers, with the one that stands marked, i el número que la política llegeix
    // abans d'apagar la placa (docs/30, docs/60).
    if (strcmp(words[0], "idle") == 0) {
      out("settings: idle sleep after ");
      out_num(cfg::idle_seconds());
      out(" s (this build's default is ");
      out_num(cfg::idle_default());
      out(")\r\n");
      return true;
    }
    // **El bloqueig de la pantalla, les tres coses** (0.189.150): the page, the switch and the minutes, with the default of
    // this build beside the page for the reason `cfg home` says it (docs/30, docs/40).
    if (strcmp(words[0], "lock") == 0) {
      out("settings: the screen locks with ");
      out((cfg::lock_app()[0] != '\0') ? cfg::lock_app() : "none");
      out(" (this board's default is ");
      out(cfg::lock_default());
      out("), lock ");
      out(cfg::lock_on() ? "on" : "off");
      out(", after ");
      out_num(cfg::lock_after_minutes());
      out(" minutes\r\n");
      return true;
    }
    if (strcmp(words[0], "lockon") == 0) {
      out("settings: lock ");
      out(cfg::lock_on() ? "on" : "off");
      newline();
      return true;
    }
    if (strcmp(words[0], "lockafter") == 0) {
      out("settings: lock after ");
      out_num(cfg::lock_after_minutes());
      out(" minutes\r\n");
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
    if (strcmp(words[0], "sort") == 0) {
      out("settings: sort apps by usage ");
      out(cfg::sort_by_usage() ? "on" : "off");
      newline();
      return true;
    }
    if (strcmp(words[0], "dst") == 0) {
      out("settings: dst ");
      out(cfg::dst() ? "on" : "off");
      newline();
      return true;
    }
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
    out("  idle sleep after: ");
  out_num(cfg::idle_seconds());
  out(" s\r\n");
  out("  cursor delay: ");
    out_num(cfg::cursor_delay());
    out(" ms\r\n");
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
  // **I el terra de les apps, que és aquesta mateixa tria** (0.190.8): one setting and one line, because two rows for the same
  // question are two answers that can disagree —and a flash is where the data of the apps is lost— (docs/30, docs/70).
  out("  the data of the apps: ");
  out((cfg::unit()[0] != '\0') ? (strcmp(cfg::unit(), "sd") == 0 ? "the card (/sd/data)\r\n" : "the flash (/littlefs/data)\r\n")
                                : "nowhere\r\n");
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
  out("  sort apps by usage: ");
  out(cfg::sort_by_usage() ? "on" : "off");
  newline();
  out("  turn: ");
  out_num(cfg::turn_instructions());
  out(" instructions of Lua\r\n");
  // **I el parpalleig del cursor, que és de tots els cursors del sistema** (0.189.34): one number for the box of one line,
  // the box of several lines and the terminal of the shell, said here so a person sees the one the board is using (docs/30).
  out("  cursor delay: ");
  out_num(cfg::cursor_delay());
  out(" ms\r\n");
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
  // **Obrir una app, mesurada**: the interpreter comes up and the chunk is compiled here, o sigui que el que una persona espera
  // després de prémer una cel·la és aquesta fila i no el metre de l'app, que encara no corre (docs/30, docs/75).
  bool the_interpreter;
  {
    meter::Scope metering(meter::kAppStart);
    the_interpreter = luabind::start(boot, limit);
  }
  if (!the_interpreter) {
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
  // **El teclat de Bluetooth, des del prompt** (0.190.179, l'amo: *«afegeix bluetooth al projecte, anem a fer un experiment»*): the
  // radio is the one part of this system that has no page and no app, and the prompt is the way to turn it on, see whether a phone has
  // found it, and write into it without a keyboard that works yet (docs/30, docs/50).
  //
  //   bt                 what this board knows about the radio, in a line
  //   bt on [name]       offer the keyboard under that name (lunokOS by default)
  //   bt off             stop offering it and give the memory back
  //   bt type <text>     write that text into the device on the other end
  // **El Bluetooth, amb els seus dos costats separats** (0.190.192, l'amo: *«si poso bt on només veig el teclat … Fem-ho bé: bt dev on/off,
  // bt dev keyboard on/off, bt dev mouse on/off, bt host on/off, bt host pair <num>»*). The radio of this board has two ends and they are
  // not the same thing: **`dev`** is what this board *is* for somebody else —a keyboard— and **`host`** is what it *listens to*
  // —somebody else's keyboard and mouse—. Abans hi havia un sol `bt on` que ho barrejava, i el que sortia era un teclat que no deia res
  // de l'altra meitat (docs/30, docs/50).
  //
  //   bt                       the two ends, in a few lines
  //   bt dev [on|off]          this board as a device, whatever it offers
  //   bt dev keyboard on|off   and the keyboard on its own
  //   bt dev mouse on|off      and a mouse of its own (not written yet: it says so)
  //   bt host [on|off]         this board as a host: the radio up and nothing offered
  //   bt host scan [seconds]   who is there
  //   bt host pair <n>         stay with one of them
  //   bt host reconnect        go back to the one that was paired here
  //   bt host forget           forget the bonds
  //   bt type <text>           write into the device on the other end
  // **`mesh` …** (0.190.212, i des de la 0.190.218 el provisionador): la placa com a xarxa de Bluetooth Mesh. El primer que hi havia era
  // ser **node** d'una xarxa que algú altre manava —el telèfon amb nRF Mesh—; ara aquesta placa pot ser-ne el **provisionador** (l'amo: *«no
  // puc provisionar jo manualment la clau del mesh per linia de comandaments, com amb el wifi?»*), o sigui que és qui té les claus i qui les
  // reparteix. Les ordres són les del wifi: veure quines claus hi ha, fer-ne o posar-hi les que vulguis, veure qui no és enlloc, donar-li una
  // adreça, i qui hi ha dins (docs/30, docs/50).
  //
  //   mesh                     on és i què té
  //   mesh on|off              la pila enlaire, o fora
  //   mesh key                 unes claus noves, fetes aquí
  //   mesh key show            quines són
  //   mesh key <net> <app>     les que diguis tu (trenta-dos dígits hexadecimals cada una)
  //   mesh find [seconds]      escoltar qui no és enlloc (deu segons si no es diu res)
  //   mesh prov <n> [addr]     donar-li una adreça d'aquesta xarxa
  //   mesh nodes               qui és dins la xarxa d'aquesta placa
  if (strncmp(line, "mesh", 4) == 0 && (line[4] == '\0' || line[4] == ' ')) {
    char words[4][kWordCap];
    size_t count = 0;
    if (!read_words(line + 4, words, 4, count)) return false;
    if (!mesh::available()) {
      out("mesh: this build has no Bluetooth Mesh in it\r\n");
      return true;
    }
    if (count == 0) {
      out(mesh::is_up() ? "mesh: the stack is up\r\n" : "mesh: off\r\n");
      if (mesh::provisioned()) {
        char address[64];
        snprintf(address, sizeof(address), "  this node is 0x%04X of a network\r\n",
                 static_cast<unsigned>(mesh::address()));
        out(address);
      } else if (mesh::is_up()) {
        out("  and it has no network of its own yet: `mesh key` makes one\r\n");
      } else {
        out("  mesh on brings it up; `mesh key` gives it a network of its own\r\n");
      }
      if (mesh::can_keep_the_keys()) {
        out(mesh::has_the_network() ? "  this board is the provisioner of that network (`mesh key show`)\r\n"
                                    : "  and this build can keep the keys of a network: `mesh key`\r\n");
      } else {
        out("  this build is a node only: a phone has to provision it\r\n");
      }
      return true;
    }
    if (strcmp(words[0], "on") == 0 && count == 1) {
      if (mesh::is_up()) {
        out("mesh: the stack is already up\r\n");
        return true;
      }
      if (bt::is_on()) {
        out("mesh: the Bluetooth keyboard has the radio; turn it off first (cfg bt off)\r\n");
        return true;
      }
      if (!mesh::start()) {
        out("mesh: the stack did not come up, and the reason is in the log\r\n");
        return true;
      }
      // **Una pila enlaire sense claus és un provisionador esperant-les** (0.190.218), i això no és un error: és el primer pas.
      out(mesh::provisioned() ? "mesh: the provisioner is up with its network, watching for devices\r\n"
                              : "mesh: the stack is up and it has no network yet: `mesh key` makes one\r\n");
      return true;
    }
    if (strcmp(words[0], "off") == 0 && count == 1) {
      mesh::stop();
      out("mesh: the stack is down\r\n");
      return true;
    }
    if (strcmp(words[0], "key") == 0 && count <= 3) {
      if (!mesh::can_keep_the_keys()) {
        out("mesh: this build cannot keep the keys of a network (it has no provisioner in it)\r\n");
        return true;
      }
      if (count == 2 && strcmp(words[1], "show") == 0) {
        uint8_t net[16] = {0};
        uint8_t app[16] = {0};
        if (!mesh::network_keys(net, app)) {
          out("mesh: this board has no keys yet (`mesh key` makes one, or `mesh key <net> <app>`)\r\n");
          return true;
        }
        out("mesh: the network of this board\r\n");
        print_key("net", net);
        print_key("app", app);
        out("  sixteen bytes each with no checksum: what is written here is the key, and it is the whole network\r\n");
        return true;
      }
      // **Les claus necessiten la pila enlaire** (0.190.218): desar una clau és un missatge a la tasca `btc`, que neix amb la pila (docs/50).
      if (!mesh::is_up()) {
        if (bt::is_on()) {
          out("mesh: the Bluetooth keyboard has the radio; turn it off first (cfg bt off)\r\n");
          return true;
        }
        if (!mesh::start()) {
          out("mesh: the stack did not come up, and the reason is in the log\r\n");
          return true;
        }
      }
      uint8_t net[16] = {0};
      uint8_t app[16] = {0};
      if (count == 1) {
        if (!mesh::make_keys(net, app)) {
          out("mesh: the board did not take the keys (the log says why)\r\n");
          return true;
        }
        out("mesh: this board has a network of its own, and the keys are new\r\n");
      } else {
        // **Les claus que dóna l'amo, o les que ja hi ha** (0.190.218): una clau sola no fa xarxa —el mesh en vol una de xarxa i una
        // d'aplicació—, o sigui que si només se'n dóna una es queda la que ja hi era i si no n'hi ha es diu què falta (docs/30).
        if (!mesh::key_from_text(words[1], net)) {
          usage_out("usage: mesh key [show|<net> <app>]   (thirty-two hex digits each)\r\n");
          return false;
        }
        if (count == 3) {
          if (!mesh::key_from_text(words[2], app)) {
            usage_out("usage: mesh key [show|<net> <app>]   (thirty-two hex digits each)\r\n");
            return false;
          }
        } else {
          uint8_t there[16] = {0};
          if (!mesh::network_keys(there, app)) {
            out("mesh: a network needs both keys and this board has no application key to keep\r\n");
            usage_out("usage: mesh key <net> <app>   (thirty-two hex digits each)\r\n");
            return false;
          }
        }
        if (!mesh::own_the_network(net, app)) {
          out("mesh: the board did not take the keys (the log says why)\r\n");
          return true;
        }
        out("mesh: this board has the network those keys make, and it is watching for devices\r\n");
      }
      print_key("net", net);
      print_key("app", app);
      out("  write them down: a key lost is a network to provision again\r\n");
      return true;
    }
    if (strcmp(words[0], "find") == 0 && count <= 2) {
      if (!mesh::can_keep_the_keys()) {
        out("mesh: this build cannot look for devices (it has no provisioner in it)\r\n");
        return true;
      }
      if (!mesh::has_the_network()) {
        out("mesh: this board has no network yet: `mesh key` first\r\n");
        return true;
      }
      if (!mesh::is_up()) {
        out("mesh: the stack is not up: `mesh on` first\r\n");
        return true;
      }
      // **Deu segons de séquia, i fins a dos minuts si es demana** (0.190.218): qui escolta espera, i el prompt ho diu abans en lloc de
      // quedar-se mut (docs/30).
      uint32_t seconds = 10;
      if (count == 2) {
        const long asked = strtol(words[1], nullptr, 0);
        if (asked > 0 && asked <= 120) seconds = static_cast<uint32_t>(asked);
      }
      char said[96];
      snprintf(said, sizeof(said), "mesh: listening for %u second(s) to the devices that are in no network ...\r\n",
               static_cast<unsigned>(seconds));
      out(said);
      const int heard = mesh::watch(seconds * 1000);
      if (heard == 0) {
        out("mesh: nothing was heard; bring it closer, or listen for longer (mesh find 30)\r\n");
        return true;
      }
      snprintf(said, sizeof(said), "mesh: %d device(s) said they are in no network\r\n", heard);
      out(said);
      for (int i = 0; i < heard; ++i) {
        uint8_t uuid[16] = {0};
        int rssi = 0;
        if (!mesh::heard_at(i, uuid, &rssi)) continue;
        char label[24];
        snprintf(label, sizeof(label), "#%d %d dBm", i, rssi);
        print_key(label, uuid);
      }
      out("  `mesh prov <n>` gives one of them an address of this network\r\n");
      return true;
    }
    if (strcmp(words[0], "prov") == 0 && count >= 2 && count <= 3) {
      if (!mesh::can_keep_the_keys()) {
        out("mesh: this build cannot provision (it has no provisioner in it)\r\n");
        return true;
      }
      if (!mesh::has_the_network()) {
        out("mesh: this board has no network yet: `mesh key` first\r\n");
        return true;
      }
      if (!mesh::is_up()) {
        out("mesh: the stack is not up: `mesh on` first\r\n");
        return true;
      }
      const int index = atoi(words[1]);
      const uint16_t address = (count == 3) ? static_cast<uint16_t>(strtoul(words[2], nullptr, 0)) : 0;
      uint16_t given = 0;
      if (!mesh::provision(index, address, &given)) {
        out("mesh: that device was not taken; the index is the one `mesh find` printed\r\n");
        return true;
      }
      char said[112];
      snprintf(said, sizeof(said), "mesh: device #%d gets 0x%04X; it takes a moment, and the log says when it is in\r\n", index,
               static_cast<unsigned>(given));
      out(said);
      return true;
    }
    if (strcmp(words[0], "nodes") == 0 && count == 1) {
      if (!mesh::can_keep_the_keys()) {
        out("mesh: this build keeps no devices (it has no provisioner in it)\r\n");
        return true;
      }
      uint16_t addresses[16];
      const int have = mesh::known(addresses, 16);
      if (have == 0) {
        out("mesh: no device is in this board's network yet (`mesh find`, then `mesh prov`)\r\n");
        return true;
      }
      char said[64];
      snprintf(said, sizeof(said), "mesh: %d device(s) in this board's network\r\n", have);
      out(said);
      for (int i = 0; i < have; ++i) {
        snprintf(said, sizeof(said), "  0x%04X\r\n", static_cast<unsigned>(addresses[i]));
        out(said);
      }
      return true;
    }
    usage_out("usage: mesh [on|off] | mesh key [show|<net> <app>] | mesh find [seconds] | mesh prov <n> [<addr>] | mesh nodes\r\n");
    return false;
  }
  if (strncmp(line, "bt", 2) == 0 && (line[2] == '\0' || line[2] == ' ')) {
    char words[5][kWordCap];
    size_t count = 0;
    if (!read_words(line + 2, words, 5, count)) return false;
    char buf[kPrintCap];

    // ---- l'estat dels dos costats, que és el que `bt` sol demana ----
    if (count == 0 || strcmp(words[0], "status") == 0) {
      // **Tres línies i prou** (0.190.195, l'amo: *«Podries ser mes clar: bluetooth: on/off, device: on/off, host: on/off»*): the whole
      // radio first and then each end, que és el que una persona vol saber d'un cop d'ull; el detall de cadascun va darrere de la seva
      // paraula, i no en una frase que barreja els dos costats (docs/30, docs/50).
      if (!bt::available()) {
        out("bluetooth: this board has no radio\r\n");
        out("  device: off\r\n");
        out("  host: off\r\n");
        return true;
      }
      const bool device = bt::is_on();
      const bool host = bt::host_is_up();
      out(device || host ? "bluetooth: on\r\n" : "bluetooth: off\r\n");
      out("  device: ");
      out(device ? "on" : "off");
      if (device) {
        out(", ");
        out(device_phrase());
        out(bt::connected() ? ", and a device is connected" : ", offered");
      }
      newline();
      out("  host: ");
      out(host ? "on" : "off");
      if (!bt::can_host()) {
        out(" (this build has no host side)");
      } else if (host) {
        out(bt::host_connected() ? ", a keyboard is writing into this console" : ", free (bt host scan)");
      }
      newline();
      // **Els detalls del que hi ha a l'altre cap**: the lights the host asked for and the mouse that is moving, which are two things a
      // person cannot see any other way (docs/30, docs/50).
      if (host && bt::host_connected()) {
        if (bt::leds() != 0) {
          snprintf(buf, sizeof(buf), "  the lights it was told: caps %s, num %s, scroll %s",
                   (bt::leds() & 0x02) != 0 ? "on" : "off", (bt::leds() & 0x01) != 0 ? "on" : "off",
                   (bt::leds() & 0x04) != 0 ? "on" : "off");
          out(buf);
          newline();
        }
        if (bt::mouse_seen()) {
          snprintf(buf, sizeof(buf), "  a mouse: %d, %d and button(s) %u", static_cast<int>(bt::mouse_x()),
                   static_cast<int>(bt::mouse_y()), static_cast<unsigned>(bt::mouse_buttons()));
          out(buf);
          newline();
        }
      }
      return true;
    }

    // **`bt on` no encén res** (0.190.194, l'amo: *«bt on no encén ni dev ni host, que s'han d'activar a ma cadascun»*): the radio has
    // two ends and turning it on is a decision of each one —sigues un teclat per a algú, o escolta algú—, o sigui que aquesta paraula el
    // que fa és dir les dues que hi ha (docs/30).
    if (strcmp(words[0], "on") == 0) {
      out("bluetooth: this board has two ends and each one is asked for itself:\r\n");
      out("  bt dev on    be something for another device (a keyboard, or a mouse)\r\n");
      out("  bt host on   listen to somebody else's keyboard or mouse\r\n");
      return true;
    }
    // **I `bt off` els apaga tots dos** (l'amo: *«bt off ho apaga tot … Si es posen a off tant dev com host, bt ha de ser off»*).
    if (strcmp(words[0], "off") == 0) {
      bt::stop();
      bt::host_off();
      out("bluetooth: off\r\n");
      return true;
    }

    // ---- el dispositiu: el que aquesta placa és per a un altre ----
    if (strcmp(words[0], "dev") == 0) {
      const char* which = (count > 1) ? words[1] : nullptr;
      // **`bt dev keyboard on|off`**, que és el teclat i prou.
      if (which != nullptr && strcmp(which, "keyboard") == 0) {
        const char* how = (count > 2) ? words[2] : nullptr;
        if (how == nullptr) {
          out(bt::is_on() ? "bluetooth: the keyboard is on\r\n" : "bluetooth: the keyboard is off\r\n");
          return true;
        }
        if (strcmp(how, "on") == 0) {
          if (!bt::available()) {
            out("bluetooth: this board has no radio\r\n");
            return true;
          }
          if (bt::start(nullptr)) {
            snprintf(buf, sizeof(buf), "bluetooth: the keyboard is offered as \"%s\"\r\n", bt::name());
            out(buf);
            out("  the waits of this board are spent awake while it is on\r\n");
          } else {
            out("bluetooth: the radio did not come up, and the reason is in the log\r\n");
          }
          return true;
        }
        if (strcmp(how, "off") == 0) {
          bt::stop();
          out("bluetooth: the keyboard is off\r\n");
          return true;
        }
        out("bluetooth: bt dev keyboard on|off\r\n");
        return true;
      }
      // **El ratolí d'aquesta placa** (0.190.193, l'amo: *«opció B»*): the board is a keyboard **or** a mouse and never both, o sigui que
      // el que fa `on` és canviar de cara —el report map, el report i l'aparença de l'anunci— i demanar que es torni a aparellar des de
      // l'altre cap, que va llegir el mapa d'un teclat (docs/50).
      if (which != nullptr && strcmp(which, "mouse") == 0) {
        const char* how = (count > 2) ? words[2] : nullptr;
        if (how == nullptr) {
          snprintf(buf, sizeof(buf), "bluetooth: this board is %s\r\n", device_phrase());
          out(buf);
          return true;
        }
        if (strcmp(how, "on") == 0 || strcmp(how, "off") == 0) {
          const bool mouse = (strcmp(how, "on") == 0);
          if (!bt::available()) {
            out("bluetooth: this board has no radio\r\n");
            return true;
          }
          if (mouse && !bt::is_on() && !bt::start(nullptr)) {
            out("bluetooth: the radio did not come up, and the reason is in the log\r\n");
            return true;
          }
          // **I la tria es desa** (0.190.208): `bt dev` i els botons de l'app `bthid` són la mateixa decisió i han de deixar el mateix
          // rastre a la botiga, o el que es tria amb una mà es perd quan la placa s'arrenca (docs/30, docs/50).
          (void)cfg::set_bt_mode(mouse ? "mouse" : "keyboard");
          snprintf(buf, sizeof(buf), "bluetooth: this board is %s, offered as \"%s\"\r\n", device_phrase(), bt::name());
          out(buf);
          out("  pair it again from the other side: it read the map of the other one\r\n");
          return true;
        }
        if (strcmp(how, "move") == 0) {
          if (count < 5) {
            out("bluetooth: bt dev mouse move <dx> <dy>\r\n");
            return true;
          }
          if (!bt::has_a_mouse()) {
            out("bluetooth: this board has no mouse (bt dev mouse on, or bt dev both on)\r\n");
            return true;
          }
          if (!bt::connected()) {
            out("bluetooth: nobody is connected, so there is nowhere to move\r\n");
            return true;
          }
          int dx = atoi(words[3]);
          int dy = atoi(words[4]);
          // **El report d'un ratolí de boot és de cent vint-i-set ençà**: un moviment més gran s'ha de fer a trossos, i el que es fa
          // aquí és dir-ho en lloc de deixar que el signe es desbordi (docs/50).
          if (dx < -127 || dx > 127 || dy < -127 || dy > 127) {
            out("bluetooth: a report carries -127 to 127; ask for it in more than one move\r\n");
            return true;
          }
          snprintf(buf, sizeof(buf), "bluetooth: moved by %d, %d\r\n", dx, dy);
          out(buf);
          bt::move(static_cast<int8_t>(dx), static_cast<int8_t>(dy));
          return true;
        }
        if (strcmp(how, "click") == 0) {
          if (!bt::has_a_mouse()) {
            out("bluetooth: this board has no mouse (bt dev mouse on, or bt dev both on)\r\n");
            return true;
          }
          if (!bt::connected()) {
            out("bluetooth: nobody is connected, so there is nothing to click\r\n");
            return true;
          }
          const char* button = (count > 3) ? words[3] : "left";
          uint8_t bits = 0x01;
          if (strcmp(button, "right") == 0) bits = 0x02;
          if (strcmp(button, "middle") == 0) bits = 0x04;
          if (bits == 0x01 && strcmp(button, "left") != 0) {
            out("bluetooth: bt dev mouse click [left|right|middle]\r\n");
            return true;
          }
          bt::click(bits);
          snprintf(buf, sizeof(buf), "bluetooth: clicked %s\r\n", button);
          out(buf);
          return true;
        }
        out("bluetooth: bt dev mouse [on|off|move <dx> <dy>|click [left|right|middle]]\r\n");
        return true;
      }
      // **Les dues coses alhora** (0.190.204, l'amo: *«una app lua... amb un teclat i un mouse»*): the composite mode, with report ids
      // and without the boot protocol. És el que una pàgina necessita per tenir un ratolí sota el dit i una caixa de text al costat, i
      // el preu és el que la línia diu (docs/50).
      if (which != nullptr && strcmp(which, "both") == 0) {
        const char* how = (count > 2) ? words[2] : nullptr;
        if (how == nullptr) {
          snprintf(buf, sizeof(buf), "bluetooth: this board is a %s\r\n", bt::device_word());
          out(buf);
          return true;
        }
        if (strcmp(how, "on") == 0 || strcmp(how, "off") == 0) {
          if (!bt::available()) {
            out("bluetooth: this board has no radio\r\n");
            return true;
          }
          if (strcmp(how, "on") == 0) {
            if (!bt::is_on() && !bt::start(nullptr)) {
              out("bluetooth: the radio did not come up, and the reason is in the log\r\n");
              return true;
            }
            (void)cfg::set_bt_mode("both");
            snprintf(buf, sizeof(buf), "bluetooth: a keyboard and a mouse, offered as \"%s\"\r\n", bt::name());
            out(buf);
            out("  pair it again from the other side: it read the map of the one before\r\n");
            out("  and this mode has report ids: the boot protocol is not served\r\n");
          } else {
            (void)cfg::set_bt_mode("keyboard");
            snprintf(buf, sizeof(buf), "bluetooth: a keyboard again, offered as \"%s\"\r\n", bt::name());
            out(buf);
            out("  pair it again from the other side: the map has changed\r\n");
          }
          return true;
        }
        out("bluetooth: bt dev both on|off\r\n");
        return true;
      }
      if (which != nullptr && (strcmp(which, "on") == 0 || strcmp(which, "off") == 0)) {
        if (strcmp(which, "off") == 0) {
          bt::stop();
          out("bluetooth: this board offers nothing\r\n");
          return true;
        }
        if (!bt::available()) {
          out("bluetooth: this board has no radio\r\n");
          return true;
        }
        if (bt::start(nullptr)) {
          snprintf(buf, sizeof(buf), "bluetooth: this board is a keyboard, offered as \"%s\"\r\n", bt::name());
          out(buf);
          out("  the waits of this board are spent awake while it is on\r\n");
        } else {
          out("bluetooth: the radio did not come up, and the reason is in the log\r\n");
        }
        return true;
      }
      if (which == nullptr) {
        out(bt::is_on() ? "bluetooth: this board is a keyboard, offered\r\n" : "bluetooth: this board offers nothing\r\n");
        return true;
      }
      out("bluetooth: bt dev [on|off|keyboard on|off|mouse [on|off|move <dx> <dy>|click]]\r\n");
      return true;
    }

    // ---- l'amfitrió: el que aquesta placa escolta ----
    if (strcmp(words[0], "host") == 0) {
      if (!bt::can_host()) {
        out("bluetooth: this board cannot be a host: no radio, or no central role in this build\r\n");
        return true;
      }
      const char* which = (count > 1) ? words[1] : nullptr;
      if (which == nullptr) {
        snprintf(buf, sizeof(buf), "bluetooth: the host is %s",
                 bt::host_connected() ? "a keyboard writing into this console" : "up and free");
        out(buf);
        newline();
        return true;
      }
      if (strcmp(which, "on") == 0) {
        out(bt::host_on() ? "bluetooth: the host side is up; `bt host scan` looks for a keyboard\r\n"
                          : "bluetooth: the radio did not come up\r\n");
        return true;
      }
      if (strcmp(which, "off") == 0) {
        bt::host_off();
        out("bluetooth: the host side is down\r\n");
        return true;
      }
      // **Quants segons, i tres per defecte** (0.190.191, l'amo: *«Fes que l'scan bluetooth sigui una mica mes llarg afegeix un paràmetre
      // a bt scan [segons]»*): three seconds is enough for a device that is advertising and not for one that is asleep, o sigui que el
      // número és de la persona. Seixanta és el sostre (docs/30, docs/50).
      if (strcmp(which, "scan") == 0) {
        uint32_t seconds = 5;
        if (count > 2) {
          const int asked = atoi(words[2]);
          if (asked < 1 || asked > 60) {
            out("bluetooth: bt host scan [seconds], from one to sixty\r\n");
            return true;
          }
          seconds = static_cast<uint32_t>(asked);
        }
        snprintf(buf, sizeof(buf), "bluetooth: listening for %u second(s)...\r\n", static_cast<unsigned>(seconds));
        out(buf);
        bt::Peer peers[8] = {};
        const uint8_t found = bt::host_scan(seconds * 1000, peers, 8);
        if (found == 0) {
          out("bluetooth: nobody answered\r\n");
          return true;
        }
        for (uint8_t i = 0; i < found; ++i) {
          const bt::Peer& peer = peers[i];
          // **Què és i amb quin senyal** (0.190.196): the appearance if it declared one, the service if not, and the signal always —una
          // llista d'adreces no diu si el teclat és a mig metre o a l'altra punta de la casa, i el que no es diu no s'inventa (docs/50).
          const char* what = (peer.appearance != 0) ? appearance_name(peer.appearance) : nullptr;
          if (what == nullptr) what = (peer.service != 0) ? service_name(peer.service) : nullptr;
          if (what == nullptr && peer.hid) what = "HID";
          snprintf(buf, sizeof(buf), "  %u  %-24s %02X:%02X:%02X:%02X:%02X:%02X  %4d dBm  %s", static_cast<unsigned>(i),
                   (peer.name[0] != '\0') ? peer.name : "(no name)", peer.addr[0], peer.addr[1], peer.addr[2],
                   peer.addr[3], peer.addr[4], peer.addr[5], static_cast<int>(peer.rssi),
                   (what != nullptr) ? what : "other");
          out(buf);
          newline();
          // **I el que l'anunci no diu amb paraules**: the appearance and the service are printed as numbers when the tables above do not
          // know them, perquè una dada que no s'entén és pitjor que una que no hi és (docs/50).
          if ((peer.appearance != 0 && appearance_name(peer.appearance) == nullptr) || peer.service != 0) {
            snprintf(buf, sizeof(buf), "     appearance 0x%04X, service 0x%04X%s", static_cast<unsigned>(peer.appearance),
                     static_cast<unsigned>(peer.service), peer.hid ? ", and it announces HID" : "");
            out(buf);
            newline();
          }
        }
        out("bluetooth: bt host pair <n> takes one of them\r\n");
        return true;
      }
      if (strcmp(which, "pair") == 0) {
        if (count < 3) {
          out("bluetooth: bt host pair <n>, with the number of the list\r\n");
          return true;
        }
        const int index = atoi(words[2]);
        if (index < 0 || index >= 8) {
          out("bluetooth: that is not a device of the list\r\n");
          return true;
        }
        out("bluetooth: pairing...\r\n");
        if (!bt::host_pair(static_cast<uint8_t>(index))) {
          out("bluetooth: the keyboard did not take it; the reason is in the log\r\n");
          return true;
        }
        out("bluetooth: the keyboard is yours, and what it writes comes to this console\r\n");
        return true;
      }
      if (strcmp(which, "reconnect") == 0) {
        if (bt::host_reconnect()) {
          out("bluetooth: going back to the one that was paired here; `bt` says when it answers\r\n");
        } else {
          out("bluetooth: there is no bond to go back to (bt host scan)\r\n");
        }
        return true;
      }
      if (strcmp(which, "forget") == 0) {
        bt::host_forget();
        out("bluetooth: the bonds are gone\r\n");
        return true;
      }
      out("bluetooth: bt host [on|off|scan [seconds]|pair <n>|reconnect|forget]\r\n");
      return true;
    }

    // ---- el teclat virtual escrivint a l'altre aparell ----
    if (strcmp(words[0], "type") == 0) {
      // Everything after the verb, spaces included, is the text: a person writing into a keyboard writes a line, not a word.
      const char* text = line + 2;
      while (*text == ' ') ++text;
      if (strncmp(text, "type", 4) == 0) text += 4;
      while (*text == ' ') ++text;
      if (*text == '\0') {
        out("bluetooth: nothing to write (bt type <text>)\r\n");
        return true;
      }
      if (!bt::connected()) {
        out("bluetooth: nobody is connected, so there is nowhere to write\r\n");
        return true;
      }
      snprintf(buf, sizeof(buf), "bluetooth: %u character(s) written", static_cast<unsigned>(bt::type(text)));
      out(buf);
      newline();
      return true;
    }
    out("bluetooth: bt [dev [on|off|keyboard on|off|mouse on|off]|host [on|off|scan [seconds]|pair <n>|reconnect|forget]|type <text>]\r\n");
    return true;
  }
  // **I els blocs, un a un** (0.190.0): `mem` diu els totals de cada casa i qui té quant; `mem blocks` baixa a la llista de
  // punters, tamanys i amos, que és la pregunta que es fa quan una xifra no quadra (docs/30, docs/75).
  if (strncmp(line, "mem blocks", 10) == 0 && (line[10] == '\0' || line[10] == ' ')) {
    print_mem_blocks();
    return true;
  }
  // **`mem himem`, la prova del banc de commutació** (0.190.173, l'amo: *«Anem a activar el himem»*): the IDF's own
  // `system/himem` example writes and reads back the memory that the chip keeps behind its bank switcher, and this is the
  // same question asked from the prompt (docs/30, docs/50).
  if (strncmp(line, "mem himem", 9) == 0 && (line[9] == '\0' || line[9] == ' ')) {
    print_mem_himem();
    return true;
  }
  if (strncmp(line, "sessions", 8) == 0 && (line[8] == '\0' || line[8] == ' ')) {
    char words[3][kWordCap];
    size_t count = 0;
    if (!read_words(line + 8, words, 3, count)) return false;
    if (count == 0) {
      print_sessions();
      return true;
    }
    // **`sessions destroy [<id> [<key>]]`** (0.189.17): the only door that throws a cache away on purpose, and the three
    // forms are the three questions a person asks in front of the listing —everything, all of one app, or that one
    // session—. It has to do it in the memory **and** on the disk: a file that stays is the very cache a person asked to
    // forget, and the next run of the app would read it back (docs/30, docs/75).
    //
    // **El mot d'un sol argument pot ser una app o una clau** (0.189.16): la llista de `sessions` ensenya les dues coses,
    // `home_eink` **i** `apps`, i un mot que és a la pantalla no es pot quedar sense efecte. Es prova com a app —totes les
    // sessions d'aquella pàgina— i, si no hi ha cap app amb aquell nom, com a clau —aquella sessió, de l'app que la
    // tingui—; la línia diu quina de les dues ha estat.
    if (strcmp(words[0], "destroy") == 0 && count <= 3) {
      session::Forgotten gone;
      bool by_key = false;
      if (count == 1) {
        gone = session::forget(nullptr);
      } else if (count == 2) {
        gone = session::forget(words[1]);
        if (gone.blocks == 0 && gone.files == 0) {
          const session::Forgotten named = session::forget_key(words[1]);
          if (named.blocks > 0 || named.files > 0) {
            gone = named;
            by_key = true;
          }
        }
      } else {
        gone = session::forget(words[1], words[2]);
      }

      out("sessions destroy");
      if (count == 1) {
        out(" (every app)");
      } else if (by_key) {
        out(" (the session '");
        out(words[1]);
        out("', of every app)");
      } else {
        out(" ");
        out(words[1]);
        if (count == 3) {
          out(" '");
          out(words[2]);
          out("'");
        }
      }
      out(": ");
      out_num(gone.blocks);
      out(" block(s) and ");
      out_num(gone.files);
      out(" file(s) forgotten, ");
      out_num(static_cast<uint32_t>(gone.bytes));
      out(" B of memory\r\n");
      // **I un mot que no és res es diu**, amb la llista a sota: un destroy que contesta zeros a seques deixa una persona
      // preguntant-se si ha escrit malament el nom o si la comanda no fa res (docs/30).
      if (count == 2 && !by_key && gone.blocks == 0 && gone.files == 0) {
        out("sessions: nothing is called '");
        out(words[1]);
        out("'; `sessions` lists the apps and the keys\r\n");
      }
      return true;
    }
    error_out("sessions: use `sessions` or `sessions destroy [<id> [<key>]]`\r\n");
    return false;
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
  if (strncmp(line, "panel", 5) == 0 && (line[5] == '\0' || line[5] == ' ')) {
    return command_panel(line + 5);
  }
  if (strncmp(line, "run", 3) == 0 && (line[3] == '\0' || line[3] == ' ')) {
    // A second word is the file the app is pointed at: `run txt res/notes.txt` is the app and what
    // it has to look at, which is the same thing `open txt res/notes.txt` says. The app reads it from
    // `app.args.path` and nothing else changes (docs/30, docs/40).
    char words[2][kWordCap];
    size_t count = 0;
    // **Els commutadors no són mots que es comptin** (0.189.163, trobat en el repàs del 17/11): `read_words` amb dos mots
    // refusava `run appinfo -d battery` —tres mots— amb «too many words», o sigui que la comanda que docs/40 ensenya no
    // arrencava res. El primer mot es llegeix sol i, si el segon comença per guió, **la resta de la línia és el que l'app
    // rep** i no es parteix (docs/30, docs/40).
    const char* rest = line + 3;
    while (*rest == ' ') ++rest;
    const char* after_id = rest;
    while (*after_id != '\0' && *after_id != ' ') ++after_id;
    const char* tail = after_id;
    while (*tail == ' ') ++tail;
    // **Un segon mot que comença per `-` són commutadors i no un camí** (0.189.161, l'amo: *«run <id_app_info> -d <id_app>»*): el
    // que es passa a l'app és `app.args.flags`, que és on va l'`opensArgs` d'un manifest, i un camí que comenci per guió curt
    // no existeix en aquest sistema.
    const bool switches_here = (*tail == '-');
    if (switches_here) {
      const size_t id_len = static_cast<size_t>(after_id - rest);
      if (id_len == 0 || id_len >= kWordCap) {
        usage_out("usage: run <id> [path | -switch ...]\r\n");
        return false;
      }
      memcpy(words[0], rest, id_len);
      words[0][id_len] = '\0';
      count = 1;
    } else if (!read_words(rest, words, 2, count)) {
      return false;
    }
    if (count == 0) {
      usage_out("usage: run <id> [path | -switch ...]\r\n");
      return false;
    }
    // **Els commutadors d'un arrencada no són els de la següent**: the string is set on every `run`, empty when the command
    // has none, so a start that fails half way cannot leave the switches of another one behind (docs/30).
    snprintf(g_start_flags, sizeof(g_start_flags), "%s", switches_here ? tail : "");
    const char* open_path = (count > 1) ? words[1] : nullptr;
    // **`&` is read by `run_app` and not here** (0.160.0): the flag lives for one start, so an app that asks for
    // another one from its own turns is not detached by accident.
    g_start_detached = background;
    const bool started = run_app(words[0], open_path);
    g_start_detached = false;
    return started;
  }
  if (strncmp(line, "open", 4) == 0 && (line[4] == '\0' || line[4] == ' ')) {
    g_start_detached = background;
    const bool opened = open_file(line + 4);
    g_start_detached = false;
    return opened;
  }
  if (strncmp(line, "kill", 4) == 0 && (line[4] == '\0' || line[4] == ' ')) {
    // **Matar una app des del prompt** (0.190.245, l'amo: *«Per matar una app el mateix. S'envía a la tasca del torn i allà es mata o s'arrenca
    // l'app»*): the command itself runs in the turn —tota comanda hi corre des de la 0.190.245—, o sigui que aquí no hi ha cap cua i el que
    // hi ha és la veritat del moment: si no hi ha cap app, es diu (docs/30, docs/50).
    if (!app_running()) {
      out("no app is running\r\n");
      return false;
    }
    close_app();
    prompt();
    return true;
  }
  if (strncmp(line, "home", 4) == 0 && (line[4] == '\0' || line[4] == ' ')) {
    // **La pàgina de casa, com l'arrencada la vol** (0.190.249): the boot used to start it with `start_app(home, /*splash=*/false)` —the glass
    // already has the picture of the boot, so the app's own card is a card nobody asked for (0.179.33)— and as a command that was lost: un
    // `run <id> &` ensenya la targeta de l'app, perquè `run` no té cap manera de dir «sense targeta». Aquesta és la porta que sí, i és la que
    // fa servir `ask_for_the_home_app` per demanar la pàgina de l'arrencada (docs/30, docs/40).
    const char* home = cfg::home_app();
    if (home == nullptr || home[0] == '\0') {
      out("home: this board has no page of the start (cfg home)\r\n");
      return false;
    }
    // **I al darrere del prompt, com el `&`**: la persona que escriu això vol la pàgina al vidre, no perdre el cable (docs/30).
    g_start_detached = true;
    const bool started = start_app(home, /*splash=*/false);
    g_start_detached = false;
    if (!started) out("home: it did not come up\r\n");
    return started;
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
  // **El registre en directe, i només quan es demana** (0.190.185, l'amo: *«fes que el log no s'envii al serie. per això está la comanda
  // log»*, i *«log follow mostra el log a temps real»*): the mirror that a wire no longer gets is opened by hand, and the prompt stops
  // taking lines until the two keys that stop anything here stop this —Control+C and one Escape— because a line typed in the middle
  // of a stream is a line lost to the stream (docs/30).
  if (strcmp(line, "log follow") == 0) {
    log_echo(true);
    g_log_follow = true;
    // **Qui atura el registre depèn de qui llegeix les tecles** (0.190.226, l'amo: *«la comanda "log follow" està trencada degut a console»*):
    // amb el terminal del cable, `Control+C` i `Escape` els menja l'editor —són seus— i el que arriba és **una línia**, o sigui que allò que
    // atura el registre és Enter; sense terminal, els bytes arriben al shell i és la tecla de sempre. **I el prompt marxa mentre dura**: el
    // registre s'escriu a sobre d'una línia que ningú no està escrivint, i un `lunok> ` al davant de cada línia seria el registre amb brossa
    // (docs/30, docs/50).
    pal::IConsole* console = serial::console();
    const bool terminal = console != nullptr && console->has_a_line_editor();
    if (terminal) {
      set_prompt_text("", false);
      // **I el cursor a l'inici de la línia**: l'editor ja ha dibuixat el prompt d'abans i no el pot esborrar; el primer log hi cauria al
      // darrere. Un retorn de carro i prou (docs/30, docs/50).
      out("\r");
    }
    out(terminal ? "log: following. Enter (a line) stops it\r\n" : "log: following. Control+C or Escape stops it\r\n");
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
  // **`terminal`: tornar a preguntar si l'altre cap és un terminal** (0.190.230, l'amo amb el registre a la mà: *«the other end of the cable
  // does not answer escape codes (no terminal)»* i el monitor obert dins del VS Code). La sonda es fa **un cop, al segon d'arrencar la placa**,
  // i si el monitor encara no hi era la sessió es quedava sense cursor per sempre més. Aquesta és la porta: es torna a preguntar ara, amb el
  // monitor ja escoltant, i si contesta s'encén l'editor i el color de la sessió (docs/30, docs/50, docs/85).
  if (strcmp(line, "terminal") == 0) {
    pal::IConsole* console = serial::console();
    if (console == nullptr) {
      error_out("terminal: the port is ceded to an app\r\n");
      return false;
    }
    if (console->has_a_line_editor()) {
      out("terminal: the line is edited by the console component and speaks ANSI already\r\n");
      return true;
    }
    uint8_t names_count = 0;
    const char* const* names = command_names(names_count);
    if (!console->start_the_terminal(names, names_count)) {
      // **I es diu què ha fallat**, que és el mateix que diu el registre a l'arrencada: el que no contesta no és un terminal, i el que no
      // contesta és el monitor, no la placa (docs/30, docs/85).
      out("terminal: the other end of the cable did not answer (the monitor is not a terminal, or it is filtered)\r\n");
      out("terminal: the shell keeps its own line editor\r\n");
      return false;
    }
    // **I el color es torna a decidir, com a l'arrencada**: la preferència és la mateixa (`cfg/ansi`) i el que ha canviat és que ara el
    // terminal contesta (docs/30, docs/50).
    g_ansi = cfg::ansi_on() && console->speaks_ansi();  // el cable pot portar-los; qui decideix és el commutador
    out("terminal: the line is edited by the console component");
    if (g_ansi) {
      out(", with cursor, colour and history");
    }
    newline();
    if (!g_ansi) out("terminal: and it does not answer as ANSI, so the codes are filtered out\r\n");
    prompt();
    return true;
  }
  // **`ansi on|off`** (0.190.226, l'amo: *«afegeix la comanda ansi on/off (totes les sortides tenen ansi, però amb aquesta opció es filtra
  // abans d'enviar a la línia)»*): el codi porta els codis sempre i aquest commutador diu si el filtre els deixa passar. És **desat**, perquè
  // una sessió que ha de ser de text pla ho és a cada arrencada, i és el que l'arrencada mira abans de decidir si el terminal en pot tenir
  // (docs/30, docs/50, docs/85).
  if (strncmp(line, "ansi", 4) == 0 && (line[4] == '\0' || line[4] == ' ')) {
    const char* word = line + 4;
    while (*word == ' ') ++word;
    if (*word == '\0') {
      out("ansi: ");
      out(g_ansi ? "on" : "off");
      out(" (escape codes in what this session writes), and the setting is ");
      out(cfg::ansi_on() ? "on" : "off");
      newline();
      return true;
    }
    const bool on = (strcmp(word, "on") == 0);
    if (!on && strcmp(word, "off") != 0) {
      usage_out("usage: ansi [on|off]\r\n");
      return false;
    }
    g_ansi = on;
    (void)cfg::set_ansi(on);
    out("ansi: ");
    out(on ? "on" : "off");
    newline();
    // **I el prompt es torna a dibuixar amb el color nou** (0.190.231): canviar el commutador i no veure'n l'efecte fins a la propera línia
    // seria endevinar si ha funcionat (docs/30).
    prompt();
    // **I si el terminal no els entén, es diu**: un `on` en un monitor que no contesta no és un error —el filtre fa el que se li demana—,
    // però el que una persona veurà seran els codis escrits, i això no és cap sorpresa que s'hagi d'endevinar (docs/30, docs/85).
    pal::IConsole* console = serial::console();
    if (on && (console == nullptr || !console->has_a_line_editor())) {
      out("ansi: the codes go out, but this console does not answer as a terminal (no cursor, no history);"
          " if your monitor writes them as letters, `ansi off`\r\n");
    }
    return true;
  }
  if (strcmp(line, "clear") == 0) {
    // **Un terminal sense codis es neteja fent pujar el paper** (0.190.187, l'amo: *«Encara queden coses amb ansi! Busca per tot el codi
    // restes d'ansi i treu-lo»*): `ESC [ 2 J` només ho entén un terminal i és brossa a qualsevol altre lloc, i el que entén tothom —el
    // cable, el vidre, una canonada— són els salts de línia (docs/30, docs/85).
    wipe_the_paper();
    return true;
  }
  if (strcmp(line, "reboot") == 0) {
    if (!reboot(nullptr)) {
      error_out("reboot: this board cannot restart itself\r\n");
      return false;
    }
    return true;
  }
  // **El son amb les tecles vives** (0.189.25): the board keeps its power and the chip sleeps until a key is pressed; the page
  // stays on the glass and the app comes back at the next boot. It is the middle way between `sleep` —which waits for an hour—
  // and `poweroff`, which leaves a board nothing but the key of the supply brings back (docs/30, docs/50).
  if (strcmp(line, "deepsleep") == 0) {
    out("deep sleep...\r\n");
    if (!deep_sleep("the prompt")) {
      error_out("this board cannot sleep with its keys awake: no key of it can wake the chip\r\n");
      return false;
    }
    return true;
  }
  // Cutting the power is not a sleep: nothing is armed and the button is the only way back in. The
  // board that can do it does not come back from the call at all, so the only line that can follow
  // is the one of a board that cannot —the ATOMs hold nothing to switch— and the person is told
  // instead of being left in front of a board that did nothing (docs/50).
  if (strcmp(line, "poweroff") == 0) {
    // **The picture of the moment, and then the supply**: the sequence is `power_off`, the same one the card of the panel
    // asks for, so the prompt and a finger do exactly the same thing to the glass (0.185.0, docs/50).
    out("powering off...\r\n");
    if (!power_off("the prompt")) {
      error_out("this board cannot cut its own power: it holds nothing to switch off\r\n");
      return false;
    }
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
  if (strncmp(line, "alarm", 5) == 0 && (line[5] == '\0' || line[5] == ' ')) {
    return command_alarm(line + 5);
  }
  if (strncmp(line, "power", 5) == 0 && (line[5] == '\0' || line[5] == ' ')) {
    return command_power(line + 5);
  }
  // **`load`**: la càrrega del sistema en tres paraules (0.189.93, docs/60). It is the share of the last quarter of a second
  // that the kernel spent working and not waiting, and it is the number the policy of the speed of the processor will be
  // built on: **low, normal or high**, with a band between them so a clock does not move at every turn. The percentage goes
  // beside the word because a word with no number under it is a word nobody can tune (l'amo: *«fes una comanda que doni el
  // valor low, normal o high i usarem aquest valor per ajustar les 3 velocitats de la CPU 80, 160, 240»*, docs/50).
  if (strncmp(line, "load", 4) == 0 && (line[4] == '\0' || line[4] == ' ')) {
    const power::Load value = power::load();
    out(power::load_name(value));
    out(" (");
    out_num(power::load_percent10() / 10);
    out(".");
    out_num(power::load_percent10() % 10);
    out(" %)\r\n");
    return true;
  }
  if (strncmp(line, "cfg", 3) == 0 && (line[3] == '\0' || line[3] == ' ')) {
    return command_cfg(line + 3);
  }
  if (strncmp(line, "led", 3) == 0 && (line[3] == '\0' || line[3] == ' ')) {
    return command_led(line + 3);
  }
  if (strncmp(line, "heapguard", 9) == 0 && (line[9] == '\0' || line[9] == ' ')) {
    return command_heapguard(line + 9);
  }
  // **`serial`**: el port sèrie cedit a una app —o a una persona— i el silenci del sistema mentre ho és (0.189.20). Three
  // words and nothing else: with no word it says who has the port, `claim` takes it and `release` gives it back, and the
  // one who took it is the one who can give it back (docs/30, docs/50).
  if (strncmp(line, "serial", 6) == 0 && (line[6] == '\0' || line[6] == ' ')) {
    char words[1][kWordCap];
    size_t count = 0;
    if (!read_words(line + 6, words, 1, count)) return false;
    if (count == 0) {
      if (serial::claimed()) {
        // **I mentre és cedit no es pot contestar**: the system writes nothing to the port, so this line goes nowhere and
        // is written anyway —it costs nothing and it is the log that keeps it (docs/30).
        out("serial: ceded to ");
        out(serial::owner());
        out("\r\n");
      } else {
        out("serial: the system's (nothing is ceded)\r\n");
      }
      return true;
    }
    if (strcmp(words[0], "claim") == 0) {
      if (serial::claimed()) {
        error_out("serial: the port is already ");
        error_out(serial::owner());
        error_out("'s\r\n");
        return false;
      }
      // **La resposta s'escriu abans de tancar el port**, perquè si no el silenci s'enduria la pròpia confirmació.
      out("serial: the port is the prompt's, and the system writes nothing to it until `serial release`\r\n");
      return serial::claim("the prompt", "a person asked for it");
    }
    if (strcmp(words[0], "release") == 0) {
      // **I el retorn s'anuncia després de recuperar-lo**, que és quan es pot llegir: mentres era cedit la línia hauria
      // anat al no-res igualment.
      const bool had = serial::release("a person asked for it");
      out(had ? "serial: the port is the system's again\r\n" : "serial: nothing was ceded\r\n");
      return true;
    }
    error_out("usage: serial [claim|release]\r\n");
    return false;
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

// **El banner d'una sessió són les lletres de l'amo, i sóc aquí dins** (0.190.232, l'amo: *«El text que tinc seleccionat es el banner que has
// de mostrar després d'iniciar una sessió a la placa per serie»*, i tot seguit *«Canvia l'ansi per el que tinc seleccionat a l'editor»*).
// L'art és el seu `data/res/logo2.ans` —té les provatures al costat, `logo1.ans` i `logo3.ans`, i aquesta és la que va triar—, i **el que hi
// ha aquí són els seus bytes, escapatge a escapatge**: els codis de color (`ESC[0;97m`, `ESC[0;94m`, `ESC[0;34m`) i les caixes de dibuix
// (UTF-8) són del fitxer i no d'aquesta casa, que només els ensenya (docs/30, docs/50).
//
// **Per què els bytes i no el fitxer** (i no és gust): `data/res` viu a la partició `littlefs`, i canviar-hi una lletra vol dir
// `pio run -t uploadfs`, que **reescriu la partició sencera** — i al ATOM, que no té targeta, això s'endú `/data` amb el que hi hagi (el
// registre de la bateria, les marques del rellotge). El que es veu amb **un sol flaix de firmware** és aquest literal, i el `.ans` és
// l'original des d'on es torna a copiar si l'art canvia (docs/30, docs/50, docs/70).
//
// **I el salt de línia ja hi és fet**: un `.ans` porta el salt sol (`LF`), i aquí cada línia acaba en `CRLF` com tota la resta del que aquest
// sistema escriu a la consola, perquè **és el que el terminal necessita per baixar de línia** (la documentació de la IDF ho diu a *Console →
// Configuring UART*: el terminal envia CR en prémer Enter i espera CR LF per tornar al principi de la línia següent). El driver del VFS, en
// mode `LF`, **no toca el que s'escriu** (0.190.237, `vfs_uart.c`), i el camí de bytes l'escriu tal qual (docs/30, docs/50).
const char* const kBanner[] = {
    "\x1b[0;97m┌─\x1b[0;94m──\x1b[0;97m┐\x1b[0;94m \x1b[0;97m┌─\x1b[0;94m──\x1b[0;34m┐\x1b[0;94m───\x1b[0;34m┐\x1b[0;97m┌─\x1b[0;94m─┐ ┌──\x1b[0;34m┐\x1b[0;97m┌─\x1b[0;94m──────\x1b[0;34m┐\x1b[0;97m┌─\x1b[0;94m─┐┌──\x1b[0;34m┐\x1b[0;97m┌─\x1b[0;94m──────\x1b[0;34m┐\x1b[0;97m┌─\x1b[0;94m─────\x1b[0;34m┐\x1b[0;94m \x1b[0m",
    "\x1b[0;94m│   \x1b[0;34m│\x1b[0;94m │   │   \x1b[0;34m│\x1b[0;94m│  └\x1b[0;34m┐\x1b[0;94m│  \x1b[0;34m│\x1b[0;94m│   \x1b[0;34m┬\x1b[0;94m   \x1b[0;34m│\x1b[0;94m│  └\x1b[0;97m┘\x1b[0;94m  \x1b[0;34m│\x1b[0;94m│   \x1b[0;34m┬\x1b[0;94m   \x1b[0;34m│\x1b[0;94m│   \x1b[0;97m──\x1b[0;94m─┴\x1b[0;34m┐\x1b[0m",
    "\x1b[0;94m│   \x1b[0;34m│\x1b[0;94m \x1b[0;34m│\x1b[0;94m   \x1b[0;97m┘\x1b[0;94m   \x1b[0;34m││\x1b[0;94m  \x1b[0;34m┌\x1b[0;94m└\x1b[0;97m┤\x1b[0;94m  \x1b[0;34m││\x1b[0;94m   │   \x1b[0;34m││\x1b[0;94m    \x1b[0;34m┌─\x1b[0;94m┘\x1b[0;34m│\x1b[0;94m   │   \x1b[0;34m│├─\x1b[0;94m───\x1b[0;34m┐\x1b[0;94m  \x1b[0;34m│\x1b[0m",
    "\x1b[0;34m│\x1b[0;94m   \x1b[0;34m└┐│\x1b[0;94m       \x1b[0;34m││\x1b[0;94m  \x1b[0;34m│└\x1b[0;94m┐  \x1b[0;34m││\x1b[0;94m   \x1b[0;97m┴\x1b[0;94m   \x1b[0;34m││\x1b[0;94m  ┌┐  \x1b[0;34m││\x1b[0;94m   \x1b[0;97m┴\x1b[0;94m   \x1b[0;34m││\x1b[0;94m   \x1b[0;34m─┘\x1b[0;94m  \x1b[0;34m│\x1b[0m",
    "\x1b[0;34m└────┘└───────┘└─\x1b[0;94m─┘ └\x1b[0;34m──┘└───────┘└──┘\x1b[0;97m└\x1b[0;34m──┘└───────┘└───────┘\x1b[0m",
};
const size_t kBannerLines = sizeof(kBanner) / sizeof(kBanner[0]);
// **El banner és del cable i no de qualsevol consola**: la consola que es penja (el vidre, l'emulador del PC) té la seva línia i la seva
// mida, i cinc línies d'art allà són soroll. `a_wire_console()` és la mateixa resposta que fa servir el pany del son (docs/50).
//
// **L'art va alineat a la dreta, i va a la línia de la sessió** (0.190.234, l'amo: *«posala alinada a la dreta a dalt, el més alt possible i
// el més a la dreta possible»*, i després, amb la placa a la mà: *«massa alt, es perd amunt amb el retorn de carro i també veig com salta de
// la dreta a l'esquerra abans de pujar»*).
//
// **Per què no es fixa a la primera fila de la pantalla**: un terminal que fa scroll no té cantonada. Posar el cursor a `ESC[1;1H` i pintar-hi
// el dibuix el deixa a la fila u **de la vista**, i el primer salt de línia de la sessió —el de la línia en blanc que hi havia darrere— fa
// pujar el paper i se n'endú la primera línia de l'art, i el segon se n'endú la segona. O sigui que l'art **va amb la sessió i no a la
// cantonada**: es pinta en el seu lloc, línia a línia, i puja amb el que escriu la persona com qualsevol altra línia, que és el que fa que no
// es perdi mai (docs/30, docs/50).
//
// **I la dreta es troba amb el cursor, sense preguntar la mida**: a cada línia, `ESC[999C` deixa el cursor **clavat al marge dret** —el
// terminal no el deixa passar, sigui quina sigui l'amplada— i `ESC[58D` el torna enrere l'amplada del dibuix menys un (les cinc línies fan
// cinquanta-nou columnes), o sigui que l'última caixa cau a l'última columna. És el mètode amb què `linenoise` mesura l'amplada (`ESC[999C`
// i preguntar on ha quedat), però aquí no es pregunta on ha quedat: n'hi ha prou amb tornar enrere. **I no depèn de cap resposta**: el primer
// intent posava el cursor amb la columna que el terminal havia contestat, i amb `pio device monitor` al mig aquesta resposta no arriba mai —
// el monitor es menja el que el terminal contesta, com amb la sonda de la 0.190.223 (docs/50, docs/85).
//
// **I el cursor s'amaga mentre es col·loca**: els salts al marge i enrere es veurien com un cursor que corre per la pantalla abans de pujar,
// que és exactament el que l'amo va veure. `ESC[?25l` abans i `ESC[?25h` després, i el que es veu és el dibuix, no el camí (docs/30, docs/85).
//
// **La mida, quan la diu, només serveix per no dibuixar de més**: un terminal que declara menys de 59 columnes o menys de 5 files no rep
// l'art, perquè el que quedaria seria el dibuix doblegat i trencat. Si no la diu, es dibuixa igualment — la dreta no en depèn (docs/50).
//
// **I sense ANSI no hi ha art** (l'amo: *«Si no hi ha ansii, no mostris l'art»*): el dibuix són codis de color i de cursor, i amb el
// commutador apagat el filtre els trauria i el que quedaria seria un garbuix de caixes sense cap sentit (docs/30, docs/85).
// **I sota el dibuix, centrat, qui és aquesta placa i quina versió hi corre** (0.190.239, l'amo: *«Sota el banner de l'entrada posa centrat
// (sota el banner) en text blanc i negreta el numero de versiò i la placa..»*). El blanc i la negreta són el que fa que es llegeixi com una
// llegenda i no com una línia més de la sessió —`ESC [ 1 ; 9 7 m`, el blanc clar—, i **el centrat és dins de les cinquanta-nou columnes del
// dibuix i no de la pantalla**: la llegenda és del dibuix, i un text centrat a la pantalla es veuria desplaçat respecte del que té a sobre.
// La columna es troba amb el mateix truc que les línies de l'art —el marge dret, que és el que el terminal sap, menys el que sobra del
// dibuix— o sigui que **tampoc aquí es pregunta cap mida** (docs/30, docs/50).
const char* const kStyleLegend = "\033[1;97m";

void say_the_banner() {
  if (!a_wire_console() || !g_ansi) return;
  const int kBannerColumns = 59;   // the width of every line of the art, colour codes not counted (data/res/logo2.ans)
  const int kFewestRows = 5;
  int rows = 0;
  int columns = 0;
  pal::IConsole* console = serial::console();
  const bool knows = (console != nullptr) && console->the_size(rows, columns);
  if (knows && columns >= 1 && columns < kBannerColumns) return;
  if (knows && rows >= 1 && rows < kFewestRows) return;
  out("\x1b[?25l");   // the cursor out of sight: what is seen is the drawing, not the path
  for (size_t i = 0; i < kBannerLines; ++i) {
    out("\x1b[999C");
    out("\x1b[58D");
    out(kBanner[i]);
    out("\r\n");
  }
  // **La llegenda, centrada sota el dibuix**: el número i la placa, que és el que una persona mira quan la sessió s'obre i no li cal
  // preguntar-ho amb `version` (docs/30).
  char legend[64];
  const int written = snprintf(legend, sizeof legend, "%s  %s", kVersion, platform().id());
  if (written > 0) {
    const int columns = (written < kBannerColumns) ? written : kBannerColumns;
    int back = (kBannerColumns - 1) - (kBannerColumns - columns) / 2;
    if (back < 0) back = 0;
    char move[24];
    snprintf(move, sizeof move, "\x1b[999C\x1b[%dD", back);
    out(move);
    out(kStyleLegend);
    out(legend);
    out(kStyleOn);
    out("\r\n");
  }
  out("\r\n");
  out("\x1b[?25h");
}

// **Una línia sencera, que és el que arriba quan el cable té terminal** (0.190.222, l'amo: *«Vull una migració completa de la shell a aquest
// terminal serie»*). El camí de bytes i aquest fan la mateixa feina i per això el que mana és el mateix `execute`: el que canvia és **qui
// ha dibuixat la línia** —l'editor del `console`, amb el cursor i els colors— i per això aquí no s'escriu ni la línia ni el salt, que ja
// són a la pantalla. El que sí que es fa és tot el que el camí de bytes fa abans d'executar: la sessió, el registre seguit i una app amb
// la consola (docs/30, docs/50).
// **Una línia que s'ha acabat va a la cua i no a `execute`** (0.190.245): the shell reads and echoes; the command is the turn's. Quan la cua és
// plena es diu aquí mateix —una tecla que desapareix en silenci és pitjor que una tecla refusada— i la persona pot tornar a prémer Enter, que
// tornarà a enviar la mateixa línia (docs/30, docs/50).
namespace {
void hand_the_line_over(const char* text) {
  lock::note_activity(power::Hand::kConsole);
  if (orders::post_a_line(text)) return;
  error_out("the turn already has four commands waiting: nothing queued\r\n");
}
}  // namespace

// **El servei de la consola, que és el que la tasca de la shell fa** (0.190.244): look at the console once —a line if the terminal has an
// editor, bytes if it does not— and execute what came. `wait_ms` is how long the wait for a line may last, and zero is the turn asking
// whether there is one and going on with its work, which is the PC and the emulator (docs/30, docs/50, docs/85).
void serve_once(int32_t wait_ms) {
  pal::IConsole* console = serial::console();
  if (console == nullptr) return;
  // **Dues maneres d'arribar una línia i un sol shell** (0.190.222): amb el terminal del cable, el que arriba és la línia que l'editor ha
  // acabat —amb els colors i el cursor ja dibuixats— i el que se'n fa és el mateix; sense terminal (l'emulador, la consola del vidre,
  // una placa on el controlador no arrenca) arriben bytes i el shell té el seu editor. **Un sol camí per volta** i mai tots dos, o la
  // meitat de les tecles anirien a un editor i l'altra meitat a l'altre (docs/30, docs/50).
  if (console->has_a_line_editor()) {
    char line[orders::kLineBytes];
    if (wait_ms > 0) {
      if (console->wait_for_a_line(line, sizeof(line), wait_ms)) hand_the_line_over(line);
      return;
    }
    while (console->take_a_line(line, sizeof(line))) hand_the_line_over(line);
    return;
  }
  char buf[64];
  size_t n = console->read(buf, sizeof(buf));
  while (n > 0) {
    // A person is at the keyboard: the board is being used and does not put itself to sleep until the prompt has been quiet for a while
    // (docs/60). **Un comptador i prou**: `lock::note_activity` mou el de `power` des de la 0.190.9 (docs/30, docs/60).
    lock::note_activity(power::Hand::kConsole);
    // **L'eco sí que és d'aquesta tasca, i per això el pany del torn** (0.190.245): `feed` escriu la lletra que s'ha premut i, si l'app és al
    // davant, la dona al seu intèrpret —que viu al torn—, o sigui que aquest tros és l'únic d'aquesta tasca que toca el que és del torn. La
    // comanda que en surti no es fa aquí: va a la cua com la del terminal (docs/30, docs/50).
    take_the_turn();
    feed(buf, n);
    give_the_turn();
    n = console->read(buf, sizeof(buf));
  }
}

void serve() {
  // **Una tasca que espera i no gira** (0.190.244): with a line editor the wait is the queue's and twenty milliseconds is the pause of a
  // person between two keypresses, o sigui que la tecla es llegeix de seguida i la tasca dorm. **I sense editor no hi ha res que esperi**
  // —el camí de bytes llegeix el que hi ha i torna—, i una tasca a prioritat de la shell que girés sense parar es menjaria el torn sencer;
  // aleshores la volta és la de sempre, deu mil·lisegons, que és el que el torn feia amb la consola abans de la 0.190.244 i el que deixa
  // que la placa segueixi fent becaines (docs/30, docs/50, docs/60, docs/95).
  for (;;) {
    pal::IConsole* console = serial::console();
    const bool editor = console != nullptr && console->has_a_line_editor();
    serve_once(editor ? 20 : 0);
    if (!editor) pal::delay_ms(10);
  }
}

namespace {
void serve_task(void*) { serve(); }
}  // namespace

bool start_the_task() {
  // **Sis kilobytes, perquè aquesta tasca ja no executa cap comanda** (0.190.245): it reads a line, echoes the bytes and leaves the line to
  // the turn, o sigui que el que necessita és el que costen la línia, l'eco i el conductor del port —`splash`, `panel` i `wipe` es tornen a
  // fer a la pila del torn, que és on es feien abans de la 0.190.244—. La prioritat és per damunt del torn perquè una tecla no esperi un
  // quadre, i la tasca passa la vida esperant, o sigui que no li lleva res (docs/30, docs/50, docs/75).
  return pal::start_a_task("lunok_shell", 6 * 1024, 3, &serve_task, nullptr);
}

void take_a_line(const char* text) {
  g_last_wire_activity_ms = pal::system().millis();
  const size_t len = (text != nullptr) ? strlen(text) : 0;
  // **Una sessió tancada s'obre amb el primer Enter**, com al cable: el terminal no pot fer-hi res més, i la línia que s'hi escriu no és
  // la primera lletra de res (docs/30).
  if (!g_session_open) {
    g_session_open = true;
    g_editor.reset();
    out("login: a new session\r\n");
    say_the_banner();
    sync_the_wire_lock();
    prompt();
    return;
  }
  // **`log follow` té el teclat mentre dura**: qualsevol línia l'atura, que és el mateix que feia qualsevol tecla (docs/30).
  if (g_log_follow) {
    g_log_follow = false;
    log_echo(!a_wire_console());
    out("log: not following any more\r\n");
    prompt();
    return;
  }
  // **Una app que viu al cable rep les línies**, i la seva consola és qui ha de dir què en fa (docs/30).
  if (g_app_alive && !g_app_detached) {
    luabind::feed_console(text, len);
    luabind::feed_console("\n", 1);
    prompt();
    return;
  }
  // **La línia es copia i s'executa**: `execute` la talla i l'escriu, i el que arriba del terminal és d'ell i no es toca (docs/45).
  char line[128];
  snprintf(line, sizeof line, "%s", (text != nullptr) ? text : "");
  if (line[0] != '\0') execute(line);
  if ((!g_app_alive || g_app_detached) && !g_choosing && !native_running() && !g_log_follow) prompt();
}

void feed(const char* data, size_t len) {
  // **Un byte és una persona al cable** (0.190.184): the clock of the session starts again with every byte, and what the turn asks is whether
  // five minutes have gone by since this one (docs/60).
  g_last_wire_activity_ms = pal::system().millis();
  // **Amb el teclat del cable, el que arriba pel port és de la caixa amb el focus** (0.189.21): the `serial` keyboard is a
  // claim on the wire and nothing on the glass, and every byte that arrives while a box that takes keys has the focus is a
  // key of that box —the same door the band uses—. Neither the prompt nor the console of the app sees it, which is the whole
  // point: `cat /dev/ttyUSB0` and type (docs/30, docs/50, docs/90).
  if (ui::keyboard_type_the_wire(data, len)) return;
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
      say_the_banner();
      prompt();
      // **I el pany torna** (0.190.184): el despertar per la consola i aquest Enter són el mateix moment, o sigui que el xip no ha de poder
      // tornar a adormir-se mentre la persona és al prompt (docs/60).
      sync_the_wire_lock();
      continue;
    }

    // **`log follow` té el teclat mentre dura** (0.190.185): the bytes of a person who is watching a stream are not the first
    // letters of a line —the prompt is not asking for one—, so the only two that mean anything are the two that stop it:
    // Control+C and **one** Escape, caught here before the escape parser turns them into a key of a sequence (docs/30).
    if (g_log_follow) {
      if (c == 0x03 || c == 0x1b) {
        g_log_follow = false;
        log_echo(!a_wire_console());
        newline();
        out("log: not following any more\r\n");
        prompt();
      }
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
      // **La comanda la fa el torn i aquí només es llegeix** (0.190.245): the line goes to the queue the same way the one the terminal edits
      // does, i el prompt del final el posa qui l'executa. Una línia buida també hi va: és com s'obre una sessió tancada i com torna el
      // prompt, o sigui que no és res a fer aquí (docs/30, docs/50).
      if (!g_editor.empty()) {
        hand_the_line_over(g_editor.data());
        g_editor.remember();
      } else {
        g_editor.reset();
        hand_the_line_over("");
      }
    } else if (c == 0x7f || c == 0x08) {
      // **Al final de la línia només marxa el caràcter** (0.190.190, l'amo: *«podem fer mes responsiu el teclat al port serie? … a vegades
      // costa de teclejar»*): reescrivint la línia sencera per cada tecla, escriure una ordre llarga costa set bytes de prompt més tota la
      // línia **a cada tecla**, i això creix amb el que has escrit. Amb el cursor a la dreta de tot, un `\b \b` és el mateix dibuix i
      // tres bytes. Quan el cursor és al mig —o hi ha espais a esborrar— es reescriu, que és quan el redibuix és el que cal (docs/30, docs/85).
      const bool at_the_end = (g_editor.cursor() == g_editor.length());
      if (g_editor.backspace()) {
        if (at_the_end) {
          out("\b \b");
          if (g_visible > g_prompt_len) --g_visible;
        } else {
          redraw_line();
        }
      }
    } else if (c >= 0x20 && c < 0x7f) {
      g_esc = 0;
      const bool at_the_end = (g_editor.cursor() == g_editor.length());
      if (g_editor.insert(c)) {
        if (at_the_end) {
          // **Un byte i prou**: the line grows to the right and the terminal already has the rest, o sigui que el que s'escriu és el
          // caràcter que s'ha premut i no la línia (docs/30, docs/85).
          out_bytes(&c, 1);
          ++g_visible;
        } else {
          redraw_line();
        }
      }
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

// **Les comandes del prompt, en una sola llista** (0.189.9): the words a person can type, in the order `help` says them.
// It is the list the app of the shell offers in its menu (`sys.commands()`), so a person picks a command instead of typing
// it, and it is the same list `help` walks for its lines —one list and not two, because two lists of the same commands is
// one list that will disagree with itself (docs/30).
const char* const kCommandNames[] = {
    "help",     "version",  "caps",     "mem",      "sessions", "apps",     "mounts",   "splash",
    "panel",    "cfg",      "run",      "kill",     "stop",     "home",     "repo",     "lua",      "keys",
    "fonts",    "wifi",     "provision", "ntp",     "tz",       "date",     "alarm",    "sleep",
    "bt",
    "load",
    "poweroff", "deepsleep", "reboot",   "battery",  "led",      "serial",   "imu",      "touch",    "buttons",  "log",
    // **I el que no hi és, no s'ofereix** (0.190.238): `free` i `history` eren en aquesta taula i no tenen cap mà que els contesti —el
    // tabulador els oferia i la línia queia a `unknown command`—, o sigui que surten. La taula del tabulador i la llista de `help` han de
    // dir el que aquesta casa sap fer de debò (docs/30, docs/45).
    "panic",    "stats",    "echo",     "cat",      "ls",       "cd",       "pwd",
    "heapguard",
    "mkdir",    "rm",       "mv",       "cp",       "hexdump",  "fs",       "clear",
    "ansi",    "terminal",
};

// **Els noms de les comandes per al tabulador del terminal** (0.190.222): la taula de dalt és la de sempre i no se'n fa una còpia, perquè el
// que el terminal completa i el que `help` llista no poden discrepar mai (docs/30, docs/45).
const char* const* command_names(uint8_t& count) {
  count = static_cast<uint8_t>(sizeof(kCommandNames) / sizeof(kCommandNames[0]));
  return kCommandNames;
}

// **What the app of the shell asks for** (0.189.9): the names, and how many there are. The pointers are this file's and they
// outlive the call, which is what a caller needs to print or list them (docs/30).
uint8_t shell_commands(const char** names, uint8_t cap) {
  const uint8_t total = static_cast<uint8_t>(sizeof(kCommandNames) / sizeof(kCommandNames[0]));
  if (names == nullptr || cap == 0) return total;
  const uint8_t copied = (total < cap) ? total : cap;
  for (uint8_t i = 0; i < copied; ++i) names[i] = kCommandNames[i];
  return copied;
}

// **The table the interpreter reads** (0.159.0): the kernel is the only one that knows how a session is opened and
// given back, and `luabind` never includes the kernel, so the six calls arrive as six function pointers
// (docs/20, docs/30).
void install_shell_ops() {
  luabind::set_shell_ops(luabind::ShellOps{&open_session, &close_session, &session_write, &session_read,
                                           &session_read_error, &session_dropped, &shell_commands});
}



// **`filtra_ansi`: el codi porta els codis i aquest filtre decideix si arriben a la línia** (0.190.226, l'amo: *«totes les sortides tenen
// ansi, però amb aquesta opció es filtra abans d'enviar a la línia»*). Amb el color encès, el text passa tal com és; amb el color apagat,
// cada seqüència `ESC [ … <lletra final>` marxa sencera —i un `ESC` sol també—, i el que queda és el mateix text sense format. És l'única
// manera que una sessió sense terminal rebi el que s'ha de llegir i no el que s'ha de dibuixar (docs/30, docs/50, docs/85).
//
// **I la mesura de la seqüència viu en una sola funció**, perquè el filtre sencer i el que escriu a trossos han de comptar-la igual: una
// `ESC [` sense final es pren la resta —una línia trencada no pot deixar mitja seqüència a la pantalla d'algú que no la pot llegir— i el
// que aquest codi no fa servir (`ESC ] … BEL`, els jocs de caràcters) també marxa (docs/30, docs/50).
size_t skip_the_escape(const char* text, size_t len, size_t at) {
  ++at;
  if (at >= len) return at;
  const char kind = text[at];
  if (kind == '[') {
    ++at;
    while (at < len) {
      const unsigned char d = static_cast<unsigned char>(text[at]);
      ++at;
      if (d >= 0x40 && d <= 0x7e) break;
    }
    return at;
  }
  if (kind == ']' || kind == '(' || kind == ')') {
    ++at;
    while (at < len && static_cast<unsigned char>(text[at]) != 0x07) ++at;
    if (at < len) ++at;
    return at;
  }
  return at;
}

bool ansi_on() { return g_ansi; }

size_t filter_ansi(const char* text, size_t len, char* out, size_t cap) {
  if (text == nullptr || out == nullptr || cap == 0) return 0;
  size_t used = 0;
  size_t at = 0;
  while (at < len && used + 1 < cap) {
    if (static_cast<unsigned char>(text[at]) == 0x1b && !g_ansi) {
      at = skip_the_escape(text, len, at);
      continue;
    }
    out[used++] = text[at++];
  }
  out[used] = '\0';
  return used;
}

}  // namespace shell
}  // namespace kernel
