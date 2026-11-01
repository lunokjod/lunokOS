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
size_t log_bytes();

// printf-style, **with the tag of whoever writes it**: the line comes out as `[I][FS] text`, the same
// shape the drivers use, so a log can be read and grepped as one thing (docs/30, docs/50).
void log_printf(pal::LogLevel level, const char* tag, const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

void log_dump();

}  // namespace kernel

#endif  // LUNOK_KERNEL_LOG_H
