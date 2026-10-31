// The sensor of the board: a thin layer, and deliberately a thin one. There is no
// policy here —a reading is a reading— but an app must still ask it the way the rest
// of the API is asked, and a board without a sensor must answer that it has none
// (docs/30, docs/50).
#include "kernel/imu.h"

#include "luabind/luabind.h"

namespace kernel {
namespace imu {
namespace {

hal::IImu* g_imu = nullptr;

}  // namespace

bool boot(hal::IPlatform& platform) {
  g_imu = platform.imu();
  // Without a sensor the operations stay absent and imu.read() answers
  // E_UNSUPPORTED instead of pretending (docs/30).
  luabind::ImuOps ops = {};
  if (g_imu != nullptr) ops.read = &read;
  luabind::set_imu_ops(ops);
  return g_imu != nullptr;
}

bool available() { return g_imu != nullptr; }

bool read(float& ax, float& ay, float& az, float& gx, float& gy, float& gz,
          float& celsius) {
  if (g_imu == nullptr) return false;
  return g_imu->read(ax, ay, az, gx, gy, gz, celsius);
}

}  // namespace imu
}  // namespace kernel
