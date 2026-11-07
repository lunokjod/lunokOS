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

// **The keyboard chosen in the settings** (0.93.0): the id of a child of `ui`, `"qwerty"` by default. The UI says
// which ids exist; this door only keeps the name alive across a reboot (docs/30, docs/70).
const char* keyboard();
bool set_keyboard(const char* id);

// **The drawing of the logo the system shows when it comes up** (0.115.0): the word of one of the two boot moments
// of the splash —`boot` or `boot-alt`—, chosen with `cfg boot <word>` and kept across a reboot like the unit and the
// keyboard. It is **a word and not a path**: the boot paints before anything else of this system exists, and a path
// in the settings would be a way to make the board open any file at its first breath (docs/30, docs/50, docs/70).
const char* boot_splash();
bool set_boot_splash(const char* word);
// Whether the word is one of the drawings of the logo this build carries, which is the question both the command
// and the boot ask and neither answers on its own (docs/30, docs/50).
bool is_a_boot_word(const char* word);

// **The app the board starts by itself when it comes up** (0.119.0): the "home" app —a launcher, a clock, the
// page a person wants to see when the board has just been switched on—. The empty name means **no home app**, and
// that is what every board did until now: the board comes up at the prompt and the person says what to run.
//
// The name **comes from the build**: `platformio.ini` gives each board its own with `-D LUNOK_HOME_APP="<id>"`,
// and `cfg home <id>` overrides it on that board's own store, the same way the keyboard and the picture of the boot
// are overridden. It is a name and not a path for the same reason the picture is a word: a path in the settings is a
// way to make a board open any file at its first breath (docs/30, docs/40, docs/70).
const char* home_app();
// What this build says, so `cfg home` can tell the person what the default of their board is.
const char* home_default();
// Keeps the name (`nullptr` or "none" clears it) and saves it. False when it could not be written.
bool set_home_app(const char* id);

// Asks for a unit and persists it. `auto` prefers the card and falls back to the flash.
// False when the name is not one of the three, or when that unit is not mounted.
bool set_unit(const char* name);

// The real path of one app's settings, for a person to read: "/sd/cfg/<id>".
bool dir_for_app(char* out, size_t cap, const char* app);

}  // namespace cfg
}  // namespace kernel

#endif  // LUNOK_KERNEL_CFG_H
