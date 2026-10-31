// Where an app keeps its settings.
//
// `/cfg` is not a disk: it is a door to one. Behind it there is `cfg/<id>/` on a unit, and
// which unit is the person's to choose — the card, because a card can be taken out and put
// in another board, or the flash, because a card can also be lost. The preference lives in
// the key-value store so it survives a reboot, and an app never learns which unit answered:
// it writes `/cfg/settings.json` and that is all (docs/70).
#ifndef LUNOK_KERNEL_CFG_H
#define LUNOK_KERNEL_CFG_H

#include <stddef.h>

#include "hal/hal.h"

namespace kernel {
namespace cfg {

// Reads the preference, decides the unit (the card first, unless the person said the
// flash), and points `/cfg` at it. A preference the board cannot honour falls back and
// says so in the log: a person who asked for the card has to know where the settings went.
void boot(hal::IPlatform& platform);

// The unit behind `/cfg`: "sd", "littlefs", or "" when neither is there.
const char* unit();

// What the person asked for: "auto", "sd" or "littlefs".
const char* preference();

// Asks for a unit and persists it. `auto` prefers the card and falls back to the flash.
// False when the name is not one of the three, or when that unit is not mounted.
bool set_unit(const char* name);

// The real path of one app's settings, for a person to read: "/sd/cfg/<id>".
bool dir_for_app(char* out, size_t cap, const char* app);

}  // namespace cfg
}  // namespace kernel

#endif  // LUNOK_KERNEL_CFG_H
