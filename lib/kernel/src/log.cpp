#include "kernel/log.h"

#include <stdarg.h>
#include <stdio.h>

#include "hal/log.h"
#include "kernel/led.h"

namespace kernel {
namespace {

const size_t kRingBytes = 2048;
const size_t kLineBytes = 160;

char g_ring[kRingBytes];
size_t g_head = 0;   // next byte to write
size_t g_count = 0;  // bytes currently stored
bool g_echo = false;

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
void sink(const char* line, size_t len) {
  if (g_echo) {
    pal::IConsole* console = pal::system().console();
    if (console != nullptr) console->write(line, len);
  }
  append(line, len);
}

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

  if (g_echo) {
    pal::IConsole* console = pal::system().console();
    if (console != nullptr) console->write(line, used);
  }
  append(line, used);
}

void log_dump() {
  pal::IConsole* console = pal::system().console();
  if (console == nullptr) return;

  if (g_count == 0) {
    static const char kEmpty[] = "(empty log)\r\n";
    console->write(kEmpty, sizeof(kEmpty) - 1);
    return;
  }

  const size_t start = (g_head + kRingBytes - g_count) % kRingBytes;
  char chunk[64];
  size_t done = 0;
  while (done < g_count) {
    size_t n = g_count - done;
    if (n > sizeof(chunk)) n = sizeof(chunk);
    for (size_t i = 0; i < n; ++i) {
      chunk[i] = g_ring[(start + done + i) % kRingBytes];
    }
    console->write(chunk, n);
    done += n;
  }
}

}  // namespace kernel
