// The air around the board, as the system hands it to an app: what the board has is here and not in
// the interpreter (docs/30, docs/50).
#ifndef LUNOK_KERNEL_ENV_H
#define LUNOK_KERNEL_ENV_H

#include "hal/hal.h"

namespace kernel {
namespace env {

// Installs what Lua may ask for. False when this board has no sensor, and then env.read() answers
// E_UNSUPPORTED instead of inventing numbers (docs/30).
bool boot(hal::IPlatform& platform);

bool available();

// Temperature in degrees Celsius and relative humidity in percent. False when the chip is not there
// or did not answer, which are two different things said with two different errors (docs/30).
bool read(float& celsius, float& humidity);

}  // namespace env
}  // namespace kernel

#endif  // LUNOK_KERNEL_ENV_H
