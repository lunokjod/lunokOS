// **One shape for every line the system writes**, whoever writes it (docs/30, docs/85):
//
//     [W][PANEL] GetDeviceInfo did not come back (2 words, ffff)
//      ^   ^      ^
//      |   |      the message, and a message says numbers: how many, how long, which address
//      |   the tag: the driver or the part of the system, in capitals and one word
//      the level: D debug, I info, W warn, E error
//
// A line that has to be read with the board in front of you is a line that costs a flash every time it
// is missing something, so the shape is fixed and the tag is obligatory.
#ifndef LUNOK_HAL_LOG_H
#define LUNOK_HAL_LOG_H

#include <stdarg.h>
#include <stddef.h>

#include "pal/pal.h"

namespace hal {

// Where a formatted line goes. **The kernel installs one** that appends to its ring and echoes it when
// echoing is on; before that —the first lines of the boot, when the kernel is not up yet— each line goes
// straight to the console (docs/30, docs/85).
using LogSink = void (*)(const char* line, size_t len);

void set_log_sink(LogSink sink);

// The line with the level and the tag in front. It is cut at the buffer and never split in two.
size_t format_log(char* out, size_t cap, pal::LogLevel level, const char* tag, const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 5, 6)))
#endif
    ;
size_t vformat_log(char* out, size_t cap, pal::LogLevel level, const char* tag, const char* fmt,
                   va_list args);

// A line from a driver, which does not depend on the kernel and must not: it formats and hands the line
// to whoever is listening (docs/50).
void logf(pal::LogLevel level, const char* tag, const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

// The letter of a level, for a place that builds its own line.
char level_letter(pal::LogLevel level);

}  // namespace hal

#endif  // LUNOK_HAL_LOG_H
