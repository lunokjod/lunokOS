// The RTC memory of the ESP32 as one file (docs/50, docs/70).
#include "esp32/rtc_memory.h"

#include <esp_attr.h>

#include "hal/memory_file.h"

namespace hal {
namespace {

// `RTC_DATA_ATTR` puts this in the RTC slow memory: the IDF keeps that memory alive while the rest
// of the chip sleeps, and it dies the moment the power does. That is exactly the promise of `/rtc`,
// which is why the file is this array and not a piece of the flash.
RTC_DATA_ATTR uint8_t g_rtc_memory[kRtcMemoryBytes];

}  // namespace

uint8_t* esp32_rtc_bytes() { return g_rtc_memory; }

IStorage* esp32_rtc_memory() {
  static MemoryFile file(g_rtc_memory, sizeof(g_rtc_memory));
  return &file;
}

}  // namespace hal
