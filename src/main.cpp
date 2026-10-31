// Arduino entry point for the board.
//
// This file only exists for the board. On the host builds (emulator and tests)
// the entry point is lib/hal_emulator/src/main_host.cpp, so here it compiles to
// a single stub. It stays in the build on purpose: PlatformIO refuses to build
// an environment whose src/ folder ends up empty.
#if defined(ARDUINO)

#include <Arduino.h>

#include "hal/hal.h"
#include "kernel/kernel.h"
#include "pal/pal.h"

void setup() {
  if (!pal::init()) {
    // Without a console there is nothing to say and nothing to do.
    for (;;) {
      delay(1000);
    }
  }
  kernel::boot(hal::platform());
}

void loop() {
  kernel::poll();
}

#else

// Keeps the translation unit non-empty on host builds.
extern "C" void lunok_board_entry_stub() {}

#endif  // ARDUINO
