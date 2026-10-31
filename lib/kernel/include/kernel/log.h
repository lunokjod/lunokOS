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

// printf-style. Lines longer than the internal buffer are cut, never split in two.
void log_printf(pal::LogLevel level, const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

void log_dump();

}  // namespace kernel

#endif  // LUNOK_KERNEL_LOG_H
