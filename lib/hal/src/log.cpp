#include "hal/log.h"

#include <stdio.h>

namespace hal {
namespace {

// Long enough for a line of the boot with its words and an address, and short enough that a driver can
// keep one on the stack (docs/30).
//
// **Cent noranta-dos i no cent seixanta** (0.189.139): the line of a paint of the panel is a hundred and sixty characters to the
// letter —the rectangle, the page, the rows, the two millisecond counts and the style— and the cap of a hundred and sixty cut it
// at `... ms of ink before i`, so the owner read a line that ended in the middle of a word and asked whether the `(` of the
// message had ever been closed. It had: what was missing was the room. Twenty more characters than the longest line this system
// writes, and a line that still does not fit says so with three dots instead of a cut word (docs/30, docs/50).
const size_t kLogLineBytes = 192;

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
  const size_t prefix = static_cast<size_t>(used);
  const size_t room = cap - prefix - 1;  // the message, and the byte of its NUL
  const int written = vsnprintf(out + prefix, room, fmt, args);
  size_t len = prefix;
  bool cut = written < 0;
  if (written > 0) {
    const size_t asked = static_cast<size_t>(written);
    if (asked < room) {
      len = prefix + asked;
    } else {
      // What fits, and one byte kept for the newline: `vsnprintf` cut the message where the room ended.
      len = cap - 2;
      cut = true;
    }
  }
  // **Una línia que no hi cap ho diu** (0.189.139): three dots and not a word cut in the middle, because a line that ends in
  // `... of ink before i` is a line a person cannot tell from a message that is missing a bracket (docs/30, docs/50).
  if (cut && len >= 3) {
    out[len - 3] = '.';
    out[len - 2] = '.';
    out[len - 1] = '.';
  }
  out[len++] = '\n';
  out[len] = '\0';
  return len;
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
