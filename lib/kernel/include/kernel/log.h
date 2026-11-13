// Log ring. Lives in RAM, never on flash: writing logs to a littlefs partition
// wears it out for nothing (docs/70).
#ifndef LUNOK_KERNEL_LOG_H
#define LUNOK_KERNEL_LOG_H

#include <stddef.h>

#include "pal/pal.h"

namespace kernel {

void log_init();
void log_echo(bool on);
bool log_echo_enabled();
// **The colour of a line follows the terminal and not the line**: the shell asks the terminal once and tells
// the ring, because a line does not know where it is going. Echoed and dumped alike, an error is red and a
// warning is yellow; everything else is the paper (docs/30, docs/85).
void log_colour(bool on);
bool log_colour_enabled();
size_t log_bytes();

// printf-style, **with the tag of whoever writes it**: the line comes out as `[I][FS] text`, the same
// shape the drivers use, so a log can be read and grepped as one thing (docs/30, docs/50).
void log_printf(pal::LogLevel level, const char* tag, const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

// **A line of the system that is a sentence and not a record** (0.179.55): the shape of this log is `[I][TAG] text`, and a
// handful of lines of this tree are written for a person and not for a reader of tags —`button: three taps, closing the
// app`, `power: woke on the timer, +2 ms from the hour it was given`—. **They go to the ring like everything else**,
// because the serial port is a copy of the ring and a line that is only on the copy is a line the `log` command cannot
// show; they keep their words, which is what a person reads. The shell and the apps are the ones that do not come here:
// they use the port, and what they print is theirs (docs/30, docs/50).
void log_say(const char* text);

void log_dump();

}  // namespace kernel

#endif  // LUNOK_KERNEL_LOG_H
