#include "hal/log.h"

#include <stdio.h>

namespace hal {
namespace {

// Long enough for a line of the boot with its words and an address, and short enough that a driver can
// keep one on the stack (docs/30).
const size_t kLogLineBytes = 160;

LogSink g_sink = nullptr;

}  // namespace

char level_letter(pal::LogLevel level) {
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

void set_log_sink(LogSink sink) { g_sink = sink; }

size_t vformat_log(char* out, size_t cap, pal::LogLevel level, const char* tag, const char* fmt,
                   va_list args) {
  if (out == nullptr || cap < 8) return 0;
  // `[I][TAG] ` and then the message: the two brackets are what makes a log line greppable, and the tag
  // is never left out (docs/30).
  int used = snprintf(out, cap, "[%c][%s] ", level_letter(level), (tag != nullptr) ? tag : "?");
  if (used < 0) return 0;
  if (static_cast<size_t>(used) >= cap - 2) return cap - 2;
  const int written = vsnprintf(out + used, cap - static_cast<size_t>(used) - 1, fmt, args);
  if (written > 0) used += written;
  if (static_cast<size_t>(used) > cap - 2) used = static_cast<int>(cap - 2);
  out[used++] = '\n';
  out[used] = '\0';
  return static_cast<size_t>(used);
}

size_t format_log(char* out, size_t cap, pal::LogLevel level, const char* tag, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  const size_t len = vformat_log(out, cap, level, tag, fmt, args);
  va_end(args);
  return len;
}

void logf(pal::LogLevel level, const char* tag, const char* fmt, ...) {
  char line[kLogLineBytes];
  va_list args;
  va_start(args, fmt);
  const size_t len = vformat_log(line, sizeof(line), level, tag, fmt, args);
  va_end(args);
  if (len == 0) return;
  if (g_sink != nullptr) {
    g_sink(line, len);
    return;
  }
  // Before the kernel has a ring: the console, and flushed, so the order of the boot's lines is the
  // order they were written in (docs/85).
  fwrite(line, 1, len, stdout);
  fflush(stdout);
}

}  // namespace hal
