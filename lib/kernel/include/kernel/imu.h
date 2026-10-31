// The inertial sensor, as the system hands it to an app: what the board has is here
// and not in the interpreter (docs/30, docs/50).
#ifndef LUNOK_KERNEL_IMU_H
#define LUNOK_KERNEL_IMU_H

#include "hal/hal.h"

namespace kernel {
namespace imu {

// Installs what Lua may ask for. False when this board has no sensor, and then
// imu.read() answers E_UNSUPPORTED instead of inventing numbers (docs/30).
bool boot(hal::IPlatform& platform);

bool available();

// Acceleration in g on three axes, turn rate in degrees per second, and the chip's
// temperature in degrees Celsius. False when the sensor is not there or did not
// answer, which are two different things said with two different errors (docs/30).
bool read(float& ax, float& ay, float& az, float& gx, float& gy, float& gz,
          float& celsius);

}  // namespace imu
}  // namespace kernel

#endif  // LUNOK_KERNEL_IMU_H
