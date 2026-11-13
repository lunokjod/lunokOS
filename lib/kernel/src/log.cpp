#include "kernel/log.h"

#include <stdarg.h>
#include <stdio.h>

#include <string.h>

#include "hal/log.h"
#include "kernel/colour.h"
#include "kernel/led.h"

namespace kernel {
namespace {

const size_t kRingBytes = 2048;
const size_t kLineBytes = 160;

char g_ring[kRingBytes];
size_t g_head = 0;   // next byte to write
size_t g_count = 0;  // bytes currently stored
bool g_echo = false;
bool g_colour = false;

// **The level of a line is its second byte** (`[I]`, `[W]`, `[E]`), which is the shape `vformat_log` gives
// every line of this system and what makes one painter enough for the echo and for the dump (docs/30).
const char* level_colour(const char* line, size_t len) {
  if (line == nullptr || len < 3 || line[0] != '[' || line[2] != ']') return nullptr;
  if (line[1] == 'E') return kColourError;
  if (line[1] == 'W') return kColourWarn;
  return nullptr;
}

void write_line(pal::IConsole* console, const char* line, size_t len) {
  if (console == nullptr) return;
  const char* colour = g_colour ? level_colour(line, len) : nullptr;
  if (colour == nullptr) {
    console->write(line, len);
    return;
  }
  console->write(colour, strlen(colour));
  console->write(line, len);
  console->write(kColourOff, sizeof(kColourOff) - 1);
}

void append(const char* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    g_ring[g_head] = data[i];
    g_head = (g_head + 1) % kRingBytes;
    if (g_count < kRingBytes) ++g_count;
  }
}

// **The ring is where a line from a driver lands too.** The drivers do not depend on the kernel —they
// cannot, the arrow goes the other way— so they format their line and hand it to this sink, installed
// when the log comes up (docs/30, docs/50).
// **La línia va al ring i, si el mirall és obert, al port sèrie** (0.179.55): un sol lloc per a les tres portes —el
// driver, el `log_printf` i la frase—, perquè el port sèrie és una còpia del log i no una segona sortida. L'ordre és
// aquest i no el contrari: primer el ring i després la còpia, així una pàgina que el port refusa no deixa el log sense
// la línia (docs/50).
void emit(const char* line, size_t len) {
  append(line, len);
  if (g_echo) write_line(pal::system().console(), line, len);
}

void sink(const char* line, size_t len) { emit(line, len); }

}  // namespace

void log_init() {
  g_head = 0;
  g_count = 0;
  // **The log is mirrored to the console while the system comes up**, because the lines that say what
  // the board found —the panel, the card, the finger— are the ones a person reads with the board in
  // front of them and they cost nothing on a wire that is idle anyway. `verbose off` silences it
  // (docs/30, docs/50).
  g_echo = true;
  hal::set_log_sink(&sink);
}

void log_echo(bool on) { g_echo = on; }

bool log_echo_enabled() { return g_echo; }

void log_say(const char* text) {
  if (text == nullptr) return;
  const size_t len = strlen(text);
  if (len == 0) return;
  emit(text, len);
}

void log_colour(bool on) { g_colour = on; }

bool log_colour_enabled() { return g_colour; }

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

void log_dump() {
  pal::IConsole* console = pal::system().console();
  if (console == nullptr) return;

  if (g_count == 0) {
    static const char kEmpty[] = "(empty log)\r\n";
    console->write(kEmpty, sizeof(kEmpty) - 1);
    return;
  }

  // **One line at a time**, because the colour of a line is decided by its first three bytes and a chunk of
  // sixty-four does not know where a line begins. The buffer is a line of the ring plus its newline.
  const size_t start = (g_head + kRingBytes - g_count) % kRingBytes;
  char line[kLineBytes + 2];
  size_t used = 0;
  for (size_t done = 0; done < g_count; ++done) {
    const char byte = g_ring[(start + done) % kRingBytes];
    line[used++] = byte;
    if (byte == '\n' || used == sizeof(line)) {
      write_line(console, line, used);
      used = 0;
    }
  }
  if (used > 0) write_line(console, line, used);
}

}  // namespace kernel
