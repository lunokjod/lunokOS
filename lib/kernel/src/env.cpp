// The air of the board: a thin layer, and deliberately a thin one. There is no policy here —a reading
// is a reading, and the chip measures on request and keeps nothing— but an app must still ask it the
// way the rest of the API is asked, and a board without a sensor must answer that it has none
// (docs/30, docs/50).
#include "kernel/env.h"

#include "kernel/meter.h"

#include "luabind/luabind.h"

namespace kernel {
namespace env {
namespace {

hal::ISensor* g_sensor = nullptr;

}  // namespace

bool boot(hal::IPlatform& platform) {
  g_sensor = platform.sensor();
  // Without a sensor the operation stays absent and env.read() answers E_UNSUPPORTED instead of
  // pretending (docs/30).
  luabind::EnvOps ops = {};
  if (g_sensor != nullptr) ops.read = &read;
  luabind::set_env_ops(ops);
  return g_sensor != nullptr;
}

bool available() { return g_sensor != nullptr; }

bool read(float& celsius, float& humidity) {
  meter::Scope scope(meter::kEnvRead);
  if (g_sensor == nullptr) return false;
  return g_sensor->read(celsius, humidity);
}

}  // namespace env
}  // namespace kernel
