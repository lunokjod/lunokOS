#include "hal/hal.h"

namespace hal {
namespace {

struct CapEntry {
  const char* name;
  Capability bit;
};

// The order here is the order the shell prints and the order sys.caps() uses
// when it builds its table.
const CapEntry kCaps[] = {
    {"console", kCapConsole},
    {"ansi", kCapAnsi},
    {"display", kCapDisplay},
    {"input", kCapInput},
    {"led", kCapLed},
    {"littlefs", kCapLittlefs},
    {"sd", kCapSd},
    {"nvs", kCapNvs},
    {"rtc", kCapRtc},
    {"rtc_wake", kCapRtcWake},
    {"net_wifi", kCapWifi},
    {"net_http", kCapHttp},
    {"battery", kCapBattery},
    {"sleep_light", kCapSleepLight},
    {"sleep_deep", kCapSleepDeep},
    {"net_prov", kCapProvisioning},
    {"imu", kCapImu},
};

const uint32_t kCapCount = static_cast<uint32_t>(sizeof(kCaps) / sizeof(kCaps[0]));

}  // namespace

uint32_t cap_count() { return kCapCount; }

const char* cap_name(uint32_t index) {
  if (index >= kCapCount) return "";
  return kCaps[index].name;
}

uint32_t cap_bit(uint32_t index) {
  if (index >= kCapCount) return 0;
  return static_cast<uint32_t>(kCaps[index].bit);
}

}  // namespace hal
