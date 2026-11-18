// **Els ajustos del sistema, llegits per una app** (0.189.13, docs/30, docs/70): `sys.settings()` answers the same values
// `cfg` prints at the prompt, and this file is the one thing that knows how to put the two together.
//
// It is a **read** and it is deliberate that there is no write: a page that changed a setting would change the board for
// every other page, and the settings are the person's —`cfg` is the door that writes them, and an app that needs a switch
// of its own keeps it in its own session (docs/30, docs/75). What the launcher asked for is the first reader: whether it
// sorts the apps by how many times they have been opened (docs/40).
#include "kernel/apps.h"
#include "kernel/cfg.h"
#include "kernel/kernel.h"
#include "kernel/ui.h"
#include "luabind/luabind.h"

#include <string.h>

namespace kernel {
namespace {

bool read_settings(luabind::Settings& into) {
  into.unit = cfg::unit();
  into.keyboard = ui::keyboard_current();
  into.boot = cfg::boot_splash();
  into.home = cfg::home_app();
  into.week = (cfg::week_start() == 7) ? "sunday" : "monday";
  // **The hour, written the way the prompt writes it**: `+01:00` and not a number of minutes, because an app shows it to
  // a person and the two doors have to say the same thing about the same clock (docs/30, docs/60).
  static char hour[8] = {0};
  cfg::tz_text(hour, sizeof(hour), cfg::tz_minutes());
  into.tz = hour;
  into.leap = cfg::leap_years();
  into.dst = cfg::dst();
  into.sort_by_usage = cfg::sort_by_usage();
  into.turn = cfg::turn_instructions();
  into.cursor_delay = cfg::cursor_delay();
  into.settings_app = cfg::settings_app();
  return true;
}

// ---- the doors that write (0.189.106) ----
//
// **El panell de Settings és la mà de la persona**: the panel that the owner asked for is not an app deciding things behind
// a person's back —it is the person, on a page— and the rules of what a value may be are the command's and not a second copy.
// `cfg` does exactly these two steps per setting (check it with the door that knows, keep it in the store), so this file
// calls the same doors and answers false when either of them says no.

bool write_keyboard(const char* id) {
  if (id == nullptr || !ui::keyboard_select(id)) return false;
  return cfg::set_keyboard(ui::keyboard_current());
}

// **L'app del botó dels ajustos, amb la mateixa comprovació que la de l'inici** (0.189.109): a name that is not an app of this
// board is not a value of this setting —the gear looks it up before it asks for anything and does nothing if it is not there—
// so the door refuses it here and the store keeps only a name that works. `none` is the word that clears it, and so is the
// empty string, which is what the door answers for a gear that does nothing (docs/30, docs/40).
bool write_settings_app(const char* id) {
  if (id == nullptr) return false;
  if (strcmp(id, "none") == 0 || id[0] == '\0') return cfg::set_settings_app(nullptr);
  apps::Entry entry;
  if (!apps::read(id, entry)) return false;
  return cfg::set_settings_app(entry.id);
}

// **La pàgina del bloqueig, amb la mateixa comprovació** (0.189.150): a name that is not an app of this board is not a value of
// this setting —the lock has to find a page to open when the minutes run out, and a typo would be a screen that never gets
// covered— so the door refuses it here and the store keeps only a name that works. `none` and the empty string are the words
// that leave the screen with no lock (docs/30, docs/40, docs/60).
bool write_lock_app(const char* id) {
  if (id == nullptr) return false;
  if (strcmp(id, "none") == 0 || id[0] == '\0') return cfg::set_lock_app(nullptr);
  apps::Entry entry;
  if (!apps::read(id, entry)) return false;
  return cfg::set_lock_app(entry.id);
}

bool write_boot(const char* word) {
  if (word == nullptr || !cfg::is_a_boot_word(word)) return false;
  return cfg::set_boot_splash(word);
}

bool write_home(const char* id) {
  if (id == nullptr) return false;
  if (strcmp(id, "none") == 0 || id[0] == '\0') return cfg::set_home_app(nullptr);
  // **El nom que torna la porta** (0.119.0): the app's door answers the id with the `-SD` suffix when it is the card's copy,
  // and the boot has to start the one the person chose (docs/40).
  apps::Entry entry;
  if (!apps::read(id, entry)) return false;
  return cfg::set_home_app(entry.id);
}

bool write_sort(bool on) { return cfg::set_sort_by_usage(on); }
bool write_turn(uint32_t instructions) { return cfg::set_turn_instructions(instructions); }
bool write_cursor(int32_t ms) { return cfg::set_cursor_delay(ms); }

// **Les llistes d'una tria**: the keyboards the UI carries and the boot words the disk has, which is what a page needs to
// offer a choice of. A word with no picture is a boot that paints nothing, so the list is the ones that can be chosen (docs/30).
uint8_t list_keyboards(const char** out, uint8_t capacity) {
  const uint8_t total = ui::keyboard_count();
  if (out == nullptr || capacity == 0) return total;
  uint8_t copied = 0;
  for (uint8_t i = 0; i < total && copied < capacity; ++i) {
    const char* id = ui::keyboard_at(i);
    if (id != nullptr) out[copied++] = id;
  }
  return copied;
}

uint8_t list_boots(const char** out, uint8_t capacity) {
  static const char* const kWords[] = {"boot", "sleep", "poweroff", "lowbatt"};
  if (out == nullptr || capacity == 0) return static_cast<uint8_t>(sizeof(kWords) / sizeof(kWords[0]));
  uint8_t copied = 0;
  for (const char* word : kWords) {
    if (copied < capacity && cfg::is_a_boot_word(word)) out[copied++] = word;
  }
  return copied;
}

}  // namespace

void install_settings_ops() {
  luabind::SettingsOps ops;
  ops.read = &read_settings;
  luabind::set_settings_ops(ops);

  luabind::CfgOps doors;
  doors.keyboard = &ui::keyboard_current;
  doors.set_keyboard = &write_keyboard;
  doors.boot = &cfg::boot_splash;
  doors.set_boot = &write_boot;
  doors.home = &cfg::home_app;
  doors.set_home = &write_home;
  doors.sort_by_usage = &cfg::sort_by_usage;
  doors.set_sort_by_usage = &write_sort;
  doors.turn = &cfg::turn_instructions;
  doors.set_turn = &write_turn;
  doors.cursor_delay = &cfg::cursor_delay;
  doors.set_cursor_delay = &write_cursor;
  doors.keyboards = &list_keyboards;
  doors.boots = &list_boots;
  // **Les velocitats permeses** (0.189.107): the mask is read where it is kept and the write goes through `cfg`, which is
  // what puts it in the governor (docs/50).
  doors.settings_app = &cfg::settings_app;
  doors.set_settings_app = &write_settings_app;
  doors.freq_mask = &cfg::freq_mask;
  doors.set_freq_allowed = &cfg::set_freq_allowed;
  // **El bloqueig de la pantalla** (0.189.150): the page through the door of the apps, the switch and the minutes as `cfg`
  // keeps them.
  doors.lock_app = &cfg::lock_app;
  doors.set_lock_app = &write_lock_app;
  doors.lock_on = &cfg::lock_on;
  doors.set_lock_on = &cfg::set_lock_on;
  doors.lock_after_minutes = &cfg::lock_after_minutes;
  doors.set_lock_after_minutes = &cfg::set_lock_after_minutes;
  // **El son sol de la placa** (0.190.5): the switch of the System tab, kept where the policy is.
  doors.sleep_on = &cfg::sleep_on;
  doors.set_sleep_on = &cfg::set_sleep_on;
  luabind::set_cfg_ops(doors);
}

}  // namespace kernel
