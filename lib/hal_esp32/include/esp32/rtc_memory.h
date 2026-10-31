// The RTC memory of the chip, as a file at `/rtc` (docs/50, docs/70).
//
// It is the only memory that survives a deep sleep and dies with the power going away: a board can
// keep a few bytes between two runs there without touching the flash —which wears— and without a
// chip behind it. On the Paper the hour that must also survive the power is the BM8563's business
// and not this one's (docs/50).
#ifndef LUNOK_ESP32_RTC_MEMORY_H
#define LUNOK_ESP32_RTC_MEMORY_H

#include <stdint.h>

#include "hal/hal.h"

namespace hal {

// The RTC memory of the ESP32, which is what `RTC_DATA_ATTR` lands in: **8 KB in the chip**, of which
// the ROM and the IDF keep a part. A build that asked for all 8 KB overflowed the region by 536
// bytes (measured, 31/10, `region 'rtc_slow_seg' overflowed`), so the file is the 7 KB that a program
// can really own (docs/50, docs/70).
const uint32_t kRtcMemoryBytes = 7 * 1024;

// The bytes, for whoever has to look at them.
uint8_t* esp32_rtc_bytes();

// The memory as one file of its own size, mounted at `/rtc`.
IStorage* esp32_rtc_memory();

}  // namespace hal

#endif  // LUNOK_ESP32_RTC_MEMORY_H
