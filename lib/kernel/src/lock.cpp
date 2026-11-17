// **Quan es tapa la pantalla** (0.189.150, l'amo: *«El Toggle de la pestanya "Privacy" activa que es llenci l'app escollida
// com lock al inici del sistema i al despertar d'un sleep. Afegeix també un selector de temps al panell Privacy "Lock after:"
// 5, 10, 15 minutes, per defecte 10 minutes. Aquest comptador torna a començar quant l'usuari deixa d'interactuar amb la
// placa, no toca botons físics ni pantalla durant aquest temps, llavors salta l'app lock»*).
//
// Three moments and one rule:
//   - **el començament de la placa** i **el retorn d'un son**: the page goes up if `cfg::lock_on` says so;
//   - **els minuts sense mans**: the counter starts again with every key, every finger and every byte of the console, and
//     when it runs out the page comes to the front \u2014which is the only thing this module measures\u2014.
//
// **La p\u00e0gina \u00e9s una app del disc** i aquest fitxer no en dibuixa ni una l\u00ednia: obre la que `cfg` nomena i prou. A board with
// no glass has nothing to cover and a board whose `cfg` names no page has nothing to open, and the two cases say so once and
// then stay quiet, perqu\u00e8 una l\u00ednia per torn \u00e9s un registre que no es pot llegir (docs/30, docs/40, docs/60).
#include "kernel/lock.h"

#include <string.h>

#include "kernel/apps.h"
#include "kernel/cfg.h"
#include "kernel/display.h"
#include "kernel/log.h"
#include "kernel/shell.h"
#include "pal/pal.h"

namespace kernel {
namespace lock {
namespace {

// When a hand was last on the board, on the monotonic clock; zero is "nobody has touched it since this boot", which is the
// boot itself and not 1970 (docs/60).
int64_t g_last_ms = 0;
// A sleep has come back and the page is owed at the next turn.
bool g_a_wake_is_owed = false;
// What was said about a page that cannot be used, so the line is said once and not once a turn.
const char* g_complained = nullptr;

int64_t now_ms() {
  // **El rellotge del sistema i no el de l'hora**: the minutes of a lock are a duration, and a duration is the monotonic
  // counter \u2014the one that does not jump when the hour is set\u2014 (docs/30, docs/60).
  return static_cast<int64_t>(pal::system().millis());
}

// The page the switch asks for, or nullptr when there is nothing to run, and the reason said once.
const char* the_page() {
  if (!cfg::lock_on()) return nullptr;
  const char* id = cfg::lock_app();
  if (id == nullptr || id[0] == '\0') {
    if (g_complained != cfg::lock_app()) {
      g_complained = id;
      log_printf(pal::kLogWarn, "LOCK", "the screen is set to lock and no lock app is chosen (`cfg lock <id>`)");
    }
    return nullptr;
  }
  return id;
}

// **The page comes to the front**: the app in front is closed first, which is what the cross of the card and an alarm with
// `raise` already do, and the one that was there comes back when the lock is opened \u2014the shell remembers it\u2014 (docs/30,
// docs/90).
void bring_it_up(const char* why) {
  const char* id = the_page();
  if (id == nullptr) return;
  if (locked()) return;
  apps::Entry entry;
  if (!apps::read(id, entry)) {
    log_printf(pal::kLogWarn, "LOCK", "%s: there is no app '%s' on this board", why, id);
    return;
  }
  if (!shell::raise_app(entry.id)) {
    log_printf(pal::kLogWarn, "LOCK", "%s: '%s' did not come up", why, entry.id);
    return;
  }
  log_printf(pal::kLogInfo, "LOCK", "%s: %s is in front", why, entry.id);
}

}  // namespace

void boot() {
  g_last_ms = now_ms();
  g_a_wake_is_owed = false;
  g_complained = nullptr;
  // **Un tauler sense vidre no tapa res**: the four boards of this build with no screen keep their prompt, which is all they
  // ever had, and a lock over a console nobody sees is an app started for nothing (docs/50).
  if (display::surface() == nullptr) return;
  bring_it_up("the board has just started");
}

void note_activity() {
  g_last_ms = now_ms();
  // A hand on the board also says that the wake has been seen: a person who is already pressing keys does not want the page
  // of the lock coming up under the finger a turn later (docs/30).
  g_a_wake_is_owed = false;
}

void note_a_wake() { g_a_wake_is_owed = true; }

void poll() {
  if (!cfg::lock_on()) return;
  if (display::surface() == nullptr) return;
  if (locked()) return;
  if (g_a_wake_is_owed) {
    g_a_wake_is_owed = false;
    bring_it_up("the board is back from a sleep");
    return;
  }
  const uint8_t minutes = cfg::lock_after_minutes();
  if (minutes == 0) return;
  const int64_t quiet_from = (g_last_ms == 0) ? now_ms() : g_last_ms;
  if (g_last_ms == 0) g_last_ms = quiet_from;
  if (now_ms() - quiet_from < static_cast<int64_t>(minutes) * 60000LL) return;
  bring_it_up("nobody has touched the board for the minutes of the setting");
  // **I el comptador torna a comen\u00e7ar**: a page that is already up is not asked for twice and the minutes of the setting are
  // counted again from now, or the next turn would ask for it again (docs/60).
  g_last_ms = now_ms();
}

bool locked() {
  const char* id = cfg::lock_app();
  if (id == nullptr || id[0] == '\0') return false;
  return shell::app_running() && strcmp(shell::app_id(), id) == 0;
}

const char* app() { return cfg::lock_app(); }

}  // namespace lock
}  // namespace kernel
