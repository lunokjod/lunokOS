#include "kernel/log.h"

#include <stdarg.h>
#include <stdio.h>

namespace kernel {
namespace {

const size_t kRingBytes = 2048;
const size_t kLineBytes = 160;

char g_ring[kRingBytes];
size_t g_head = 0;   // next byte to write
size_t g_count = 0;  // bytes currently stored
bool g_echo = false;

char level_tag(pal::LogLevel level) {
  switch (level) {
    case pal::kLogDebug:
      return 'D';
    case pal::kLogInfo:
      return 'I';
    case pal::kLogWarn:
      return 'W';
    case pal::kLogError:
      return 'E';
  }
  return '?';
}

void append(const char* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    g_ring[g_head] = data[i];
    g_head = (g_head + 1) % kRingBytes;
    if (g_count < kRingBytes) ++g_count;
  }
}

}  // namespace

void log_init() {
  g_head = 0;
  g_count = 0;
  g_echo = false;
}

void log_echo(bool on) { g_echo = on; }

bool log_echo_enabled() { return g_echo; }

size_t log_bytes() { return g_count; }

void log_printf(pal::LogLevel level, const char* fmt, ...) {
  char line[kLineBytes];
  size_t used = 0;
  line[used++] = level_tag(level);
  line[used++] = ' ';

  va_list args;
  va_start(args, fmt);
  const int written = vsnprintf(line + used, sizeof(line) - used - 1, fmt, args);
  va_end(args);

  if (written > 0) {
    used += static_cast<size_t>(written);
    if (used > sizeof(line) - 2) used = sizeof(line) - 2;
  }
  line[used++] = '\n';

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
