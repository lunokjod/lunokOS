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

// **The clock of the chip is UTC and a person reads their own hour** (0.125.0): the system keeps the one number that
// joins them —how many minutes the local hour is ahead of UTC— and `time.local()` applies it, so an app that says
// "07:00 to 19:00" is saying it in the hour the person is living in and not in the one the quartz counts (docs/30,
// docs/60). It is a setting of the system and not of an app: `cfg tz +01:00` writes it, `time.tz()` does the same
// from Lua, and the two read the same value.
//
// **It is an offset and not a zone**: this system has no table of the world's zones and does not pretend to, so a
// board that lives where the hour changes twice a year has to be told the new offset —one command, twice a year—.
int16_t tz_minutes();
// Minutes east of UTC, from -14:00 to +14:00. False when the number is not a zone this clock could have.
bool set_tz_minutes(int32_t minutes);
// "+01:00", "-03:30" or "+00:00": the way the setting is written in the store and on the glass (seven characters and
// the null, so `cap` has to be eight).
bool tz_text(char* out, size_t cap, int32_t minutes);
// **Reads the two ways a person writes an offset**: `+01:00`, `-0330`, `+1`, `01:00` or a number of minutes on its
// own (`60`, `-90`). True when `text` is one of them, and then `minutes` holds it.
bool parse_tz(const char* text, int32_t& minutes);

// **The calendar a person wants** (0.170.0, docs/60, docs/90): whether February has its twenty-ninth day and which
// weekday a week begins on. Both are settings of the system and not of an app —`ui.month`, `time.utc`/`date` and any
// page that draws a month ask the same rule— and both survive a reboot like the offset of the hour does. The rule
// itself is `kernel/calendar.h`; this door keeps the choice, puts it in the calendar at boot, and answers the same
// value the calendar is using.
bool leap_years();
bool set_leap_years(bool on);
// **1 is Monday and 7 is Sunday**: Monday is the default of this system since its first calendar, and Sunday is the
// week a person may ask for with `cfg week sunday` or `time.week(7)` (docs/60).
uint8_t week_start();
bool set_week_start(uint8_t first);

// **The slice of a turn of Lua, which is the latency of the whole board** (0.174.7, docs/20, docs/75): how many
// instructions an app runs before the kernel takes the turn back for the glass, the keys and the radio. It is a setting
// and not a constant because it is a trade between the app's throughput and how long a finger waits, and that trade is
// this board's to make: `cfg turn 20000` writes it, `sys.turn()` reads it from Lua, and the boot puts it in the module
// that counts it. The number in force is the block ceiling of what was asked (the hook counts in blocks of 2000), which
// is why the door answers a number and not the text it was given.
uint32_t turn_instructions();
// What this build ships with, so `cfg turn` can say which of the two a person is looking at.
uint32_t turn_default();
// A slice is a count of digits: true when `text` is one of the numbers a slice may be. `instructions` holds it then.
bool parse_turn(const char* text, uint32_t& instructions);
// Applies the number now —the next turn of the app uses it— and saves it. False when the number is not a slice or when
// there is nowhere to keep it.
bool set_turn_instructions(uint32_t instructions);

// The real path of one app's settings, for a person to read: "/sd/cfg/<id>".
bool dir_for_app(char* out, size_t cap, const char* app);

}  // namespace cfg
}  // namespace kernel

#endif  // LUNOK_KERNEL_CFG_H
