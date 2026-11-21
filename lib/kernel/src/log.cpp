#include "kernel/log.h"

#include <stdarg.h>
#include <stdio.h>

#include <string.h>

#include "hal/log.h"
#include "kernel/led.h"
#include "kernel/serial.h"

#include "pal/pal.h"

namespace kernel {
namespace {

const size_t kRingBytes = 2048;
// **The line of the ring is the line of the formatter** (0.189.139): a hundred and ninety-two, the one `hal::logf` writes, or a line
// that fits there would be cut here a second time (docs/30).
const size_t kLineBytes = 192;

// **El registre viu a la memòria extra i no a la de tota la vida** (0.190.166, l'amo: *«segur coses estàtiques que no necessitem perqué ho podem
// fer dinàmic amb psram»*): deux kilobytes de DRAM per a un text que una persona llegeix i que ningú recorre amb presses, o sigui que el bloc
// es demana al mateix `pal::extra_memory()` que les cares i les imatges —PSRAM a les plaques que en tenen— i el que queda aquí és un anella de
// reserva per a una placa sense memòria extra. El que la placa guanya és **1536 bytes de DRAM** i el que no perd és cap línia: on hi ha PSRAM
// l'anella és la de sempre (docs/30, docs/75).
const size_t kRingFallbackBytes = 512;
char g_fallback[kRingFallbackBytes];
char* g_ring = g_fallback;
size_t g_ring_bytes = sizeof(g_fallback);
size_t g_head = 0;   // next byte to write
size_t g_count = 0;  // bytes currently stored
bool g_echo = false;
// **El llindar, i el de sèrie és el de sempre** (0.190.89): `Debug`, o sigui tot el que el sistema ha escrit mai.
pal::LogLevel g_level = pal::kLogDebug;

// **El registre és text pla** (0.190.92, l'amo: *«treu els textos ANSI del codi»*): la línia va al port tal com l'ha formatada el
// formatter, sense cap codi d'escapament. El nivell hi és —`[E]`, `[W]`, `[I]`, `[D]`—, que és el que una persona llegeix i el que una
// cerca troba (docs/30).
void write_line(pal::IConsole* console, const char* line, size_t len) {
  if (console == nullptr) return;
  console->write(line, len);
}

void append(const char* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    g_ring[g_head] = data[i];
    g_head = (g_head + 1) % g_ring_bytes;
    if (g_count < g_ring_bytes) ++g_count;
  }
}

// **The ring is where a line from a driver lands too.** The drivers do not depend on the kernel —they
// cannot, the arrow goes the other way— so they format their line and hand it to this sink, installed
// when the log comes up (docs/30, docs/50).
// **La línia va al ring i, si el mirall és obert, al port sèrie** (0.179.55): un sol lloc per a les tres portes —el
// driver, el `log_printf` i la frase—, perquè el port sèrie és una còpia del log i no una segona sortida. L'ordre és
// aquest i no el contrari: primer el ring i després la còpia, així una pàgina que el port refusa no deixa el log sense
// la línia (docs/50).
// **Una línia per sota del llindar no arriba ni al ring** (0.190.89): the level is the second byte of the line (`[D]`, `[I]`, `[W]`,
// `[E]`), que és la forma que `vformat_log` dóna a totes les línies del sistema; una línia que no té aquesta forma és una frase de
// `log_say` i passa sempre, perquè una frase és una resposta a una persona i no una traça (docs/30, docs/50).
bool below_the_level(const char* line, size_t len) {
  if (line == nullptr || len < 3 || line[0] != '[' || line[2] != ']') return false;
  uint8_t at = 0;
  switch (line[1]) {
    case 'D': at = pal::kLogDebug; break;
    case 'I': at = pal::kLogInfo; break;
    case 'W': at = pal::kLogWarn; break;
    case 'E': at = pal::kLogError; break;
    default: return false;
  }
  return at < static_cast<uint8_t>(g_level);
}

// **El cable només rep el que és una fallada** (0.190.185, l'amo: *«fes que el log no s'envii al serie. per això está la
// comanda log»*): the mirror is shut on the wire, so the lines of a session are read with `log` or followed with `log
// follow`. A `[E]` still goes out, and out of turn: the board that cannot reach its prompt has no shell to ask, and the
// wire is the only window left (docs/30, docs/60).
bool a_failure(const char* line, size_t len) {
  return line != nullptr && len >= 3 && line[0] == '[' && line[1] == 'E' && line[2] == ']';
}

void emit(const char* line, size_t len) {
  if (below_the_level(line, len)) return;
  append(line, len);
  // **I mentre el port és cedit, el mirall calla** (0.189.20): `serial::console()` is nullptr and `write_line` already knows
  // what to do with a console that is not there; the ring keeps the line, so `log` still has it when the port comes back
  // (docs/30, docs/50).
  if (g_echo || a_failure(line, len)) write_line(serial::console(), line, len);
}

// **I la porta de les línies que no es filtren** (0.190.114): the ring and the mirror, without the level in the middle —el que una persona
// acaba de demanar no és una traça (docs/30, docs/50).
void emit_anyway(const char* line, size_t len) {
  append(line, len);
  if (g_echo) write_line(serial::console(), line, len);
}

void sink(const char* line, size_t len) { emit(line, len); }

}  // namespace

void log_init() {
  // **I l'anella gran, si la placa en té** (0.190.166): the block is asked once, o sigui que una placa sense PSRAM es queda amb la de reserva i
  // no ho diu dues vegades; la que en té, la de sempre (docs/30, docs/75).
  if (g_ring == g_fallback) {
    if (void* block = pal::extra_memory().allocate(kRingBytes, true)) {
      g_ring = static_cast<char*>(block);
      g_ring_bytes = kRingBytes;
    }
  }
  g_head = 0;
  g_count = 0;
  // **El mirall és de la consola que no és el cable** (0.190.185, l'amo: *«fes que el log no s'envii al serie»*): on la consola
  // és un emulador o el vidre, les línies que diuen què ha trobat la placa són el diagnòstic i s'hi veuen soles; al port sèrie
  // són soroll al mig del que una persona escriu i es demanen (`log`, `log follow`). El filtre és el de sempre —el que la
  // consola diu d'ella mateixa— i no una llista de plaques (docs/30, docs/60).
  pal::IConsole* console = pal::system().console();
  g_echo = console != nullptr && console->virtual_terminal();
  hal::set_log_sink(&sink);
}

void log_echo(bool on) { g_echo = on; }

bool log_echo_enabled() { return g_echo; }

// **Les quatre paraules del llindar** (0.190.89): les del `nvs`, les de la fila de Settings i les del prompt, que són les mateixes perquè
// una persona no ha de traduir res. El que no és cap de les quatre no canvia res i ho diu (docs/30, docs/50).
void log_set_level(pal::LogLevel level) { g_level = level; }

pal::LogLevel log_level() { return g_level; }

const char* log_level_name(pal::LogLevel level) {
  switch (level) {
    case pal::kLogInfo: return "info";
    case pal::kLogWarn: return "warn";
    case pal::kLogError: return "error";
    case pal::kLogDebug:
    default: return "debug";
  }
}

bool log_level_from_name(const char* name, pal::LogLevel* out) {
  if (name == nullptr || out == nullptr) return false;
  static const struct {
    const char* word;
    pal::LogLevel level;
  } kWords[] = {{"debug", pal::kLogDebug}, {"info", pal::kLogInfo}, {"warn", pal::kLogWarn},
                {"warning", pal::kLogWarn}, {"error", pal::kLogError}};
  for (size_t i = 0; i < sizeof(kWords) / sizeof(kWords[0]); ++i) {
    const char* a = name;
    const char* b = kWords[i].word;
    size_t j = 0;
    for (; a[j] != '\0' && b[j] != '\0'; ++j) {
      char c = a[j];
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
      if (c != b[j]) break;
    }
    if (a[j] == '\0' && b[j] == '\0') {
      *out = kWords[i].level;
      return true;
    }
  }
  return false;
}

void log_say(const char* text) {
  if (text == nullptr) return;
  const size_t len = strlen(text);
  if (len == 0) return;
  emit(text, len);
}

size_t log_bytes() { return g_count; }

void log_printf(pal::LogLevel level, const char* tag, const char* fmt, ...) {
  char line[kLineBytes];

  // A failure is also a light. The SOS is asked for here and not at every place that logs
  // one, because a place that has to remember to say it is a place that will forget, and
  // the light of the board is the only thing a person sees when the console is closed
  // (docs/30). It does nothing on a board with no light, and nothing before the light is
  // up, which is why it can be called from the first line of the boot.
  if (level == pal::kLogError) led::note(led::Trigger::kError);

  va_list args;
  va_start(args, fmt);
  const size_t used = hal::vformat_log(line, sizeof(line), level, tag, fmt, args);
  va_end(args);
  if (used == 0) return;

  emit(line, used);
}

void log_markf(const char* tag, const char* fmt, ...) {
  char line[kLineBytes];
  va_list args;
  va_start(args, fmt);
  const size_t used = hal::vformat_log(line, sizeof(line), pal::kLogInfo, tag, fmt, args);
  va_end(args);
  if (used == 0) return;
  emit_anyway(line, used);
}

void log_dump() {
  pal::IConsole* console = serial::console();
  if (console == nullptr) return;

  if (g_count == 0) {
    static const char kEmpty[] = "(empty log)\r\n";
    console->write(kEmpty, sizeof(kEmpty) - 1);
    return;
  }

  // **One line at a time**, because the colour of a line is decided by its first three bytes and a chunk of
  // sixty-four does not know where a line begins. The buffer is a line of the ring plus its newline.
  const size_t start = (g_head + g_ring_bytes - g_count) % g_ring_bytes;
  char line[kLineBytes + 2];
  size_t used = 0;
  for (size_t done = 0; done < g_count; ++done) {
    const char byte = g_ring[(start + done) % g_ring_bytes];
    line[used++] = byte;
    if (byte == '\n' || used == sizeof(line)) {
      write_line(console, line, used);
      used = 0;
    }
  }
  if (used > 0) write_line(console, line, used);
}

}  // namespace kernel
