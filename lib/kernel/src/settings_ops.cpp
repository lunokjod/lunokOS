// **Els ajustos del sistema, llegits per una app** (0.189.13, docs/30, docs/70): `sys.settings()` answers the same values
// `cfg` prints at the prompt, and this file is the one thing that knows how to put the two together.
//
// It is a **read** and it is deliberate that there is no write: a page that changed a setting would change the board for
// every other page, and the settings are the person's —`cfg` is the door that writes them, and an app that needs a switch
// of its own keeps it in its own session (docs/30, docs/75). What the launcher asked for is the first reader: whether it
// sorts the apps by how many times they have been opened (docs/40).
#include "kernel/cfg.h"
#include "kernel/kernel.h"
#include "kernel/ui.h"
#include "luabind/luabind.h"

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
  return true;
}

}  // namespace

void install_settings_ops() {
  luabind::SettingsOps ops;
  ops.read = &read_settings;
  luabind::set_settings_ops(ops);
}

}  // namespace kernel
