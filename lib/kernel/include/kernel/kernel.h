#ifndef LUNOK_KERNEL_KERNEL_H
#define LUNOK_KERNEL_KERNEL_H

#include "hal/hal.h"

namespace kernel {

// Brings the system up on top of a board. Call after pal::init().
void boot(hal::IPlatform& platform);

// One turn of the main loop: wait for an event, dispatch it, drain the console.
void poll();

bool running();
void shutdown();

// The board the kernel is running on. Valid after boot().
hal::IPlatform& platform();

// Runs one command line as if it had been typed. Used by the emulator's batch
// mode and by the tests. Returns false when the command failed or was unknown.
bool execute(const char* line);

// True while an app started from the shell is alive (it may be waiting for a
// promise between turns). Whoever drives the loop by hand has to keep calling
// poll() until this says false.
bool app_running();

// Reads the persistent alarms from the NVS and puts them back in the queue. It
// runs at boot, and the ones whose time has passed are fired by the next turns
// with their late_ms (docs/60). Exposed so a test can do a "reboot" without one.
void alarms_restore();

}  // namespace kernel

#endif  // LUNOK_KERNEL_KERNEL_H
