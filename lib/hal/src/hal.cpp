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
    {"env", kCapEnv},
};

const uint32_t kCapCount = static_cast<uint32_t>(sizeof(kCaps) / sizeof(kCaps[0]));

}  // namespace

const char* face_name(Face face) {
  switch (face) {
    case Face::kFront:
      return "front";
    case Face::kBack:
      return "back";
    case Face::kLeft:
      return "left";
    case Face::kRight:
      return "right";
    case Face::kTop:
      return "top";
    case Face::kBottom:
      return "bottom";
  }
  return "?";
}

const char* spot_name(Spot spot) {
  switch (spot) {
    case Spot::kCentre:
      return "centre";
    case Spot::kUp:
      return "up";
    case Spot::kDown:
      return "down";
    case Spot::kLeft:
      return "left";
    case Spot::kRight:
      return "right";
  }
  return "?";
}

const char* kind_name(KeyKind kind) {
  switch (kind) {
    case KeyKind::kSimple:
      return "simple";
    case KeyKind::kCompound:
      return "compound";
  }
  return "?";
}

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
