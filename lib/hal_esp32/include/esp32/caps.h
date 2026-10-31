// What the ESP32 gives, before the board says a word.
//
// Every board of this chip inherits this and adds only what it has of its own —the
// filesystem it mounts, a light, keys, a screen— or takes one away when its hardware
// really lacks it, with the reason. The alternative, which is what happened until
// today, is that a board library lists `wifi` and `nvs` again and forgets one: the
// Paper was saying it had no radio while the code for it was the same as the ATOM's
// (docs/50, docs/90).
#ifndef LUNOK_ESP32_CAPS_H
#define LUNOK_ESP32_CAPS_H

#include "hal/hal.h"

namespace hal {

// The console, the ANSI of the console, the SoC clock and its wake, the key-value
// store, both depths of sleep, and the radio with provisioning and a page.
//
// It says the code is in the build, not that the hardware answered: whether the access
// point answers, or whether a page arrives, is the call's to say (docs/30).
Caps esp32_caps();

}  // namespace hal

#endif  // LUNOK_ESP32_CAPS_H
