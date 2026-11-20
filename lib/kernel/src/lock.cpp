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
#include "kernel/power.h"
#include "kernel/shell.h"
#include "pal/pal.h"

namespace kernel {
namespace lock {
namespace {

// When a hand was last on the board, on the monotonic clock; zero is "nobody has touched it since this boot", which is the
// boot itself and not 1970 (docs/60).
// **El comptador de la quietud NO és d'aquest fitxer** (0.190.9, l'amo: *«el comptador d'idle es el mateix que per el que fa
// saltar el device lock… No malgastis temps i memoria en varis comptadors»*): the minutes without a hand are the board's quiet,
// which `power` counts once for the deep sleep and for this page, and here there is only the question (docs/30, docs/60).
// A sleep has come back and the page is owed at the next turn.
bool g_a_wake_is_owed = false;
// What was said about a page that cannot be used, so the line is said once and not once a turn.
const char* g_complained = nullptr;

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

const char* boot() {
  // **Una arrencada no és una mà** (0.190.18): `power::boot` ja ha arrencat el comptador de la quietud, i dir-ho aquí tornava a comptar
  // com a ús el que només és el començament d'un boot —i trencava el «ningú l'ha tocada» de la tornada per una alarma— (docs/30, docs/60).
  g_a_wake_is_owed = false;
  g_complained = nullptr;
  // **Un tauler sense vidre no tapa res**: the four boards of this build with no screen keep their prompt, which is all they
  // ever had, and a lock over a console nobody sees is an app started for nothing (docs/50).
  if (display::surface() == nullptr) return nullptr;
  const char* id = the_page();
  if (id == nullptr) return nullptr;
  // **I una app que torna d'una sessió no es trepitja** (docs/90): a board that went to sleep inside an app comes back into
  // that app, and the lock goes **over** it —the shell gives it back when the lock opens, which is the session the person
  // left— and no home app is owed, because the page under the lock is already theirs.
  if (shell::app_running()) {
    bring_it_up("the board has just started");
    return nullptr;
  }
  apps::Entry entry;
  if (!apps::read(id, entry)) {
    log_printf(pal::kLogWarn, "LOCK", "the board has just started: there is no app '%s' on this board", id);
    return nullptr;
  }
  // **La pàgina abans que la casa** (0.189.159, l'amo: *«Arrenca abans el lock i després si hi ha home definida l'arrenques
  // desde device_lock»*): the lock is started as the page of the boot and the app it names is what answers for the way back;
  // the kernel starts no home app behind it (docs/30, docs/40).
  if (!shell::start_app(entry.id, /*splash=*/false)) {
    log_printf(pal::kLogWarn, "LOCK", "the board has just started: '%s' did not come up", entry.id);
    return nullptr;
  }
  log_printf(pal::kLogInfo, "LOCK", "the board has just started: %s is the page of this boot", entry.id);
  // **El nom que es torna és el de la porta i no el d'una fitxa local**: the kernel only asks whether the glass was covered, and
  // a pointer into a struct of this frame would be a string that is not there by the next turn (docs/30, docs/90).
  return id;
}

void note_activity() { kernel::lock::note_activity(power::Hand::kPolicy); }

void note_activity(power::Hand hand) {
  power::note_activity(hand);
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
  // **Els minuts d'aquesta pàgina són la quietud de la placa** (0.190.9): the same counter the deep sleep reads, so a board cannot
  // be "idle" for one of the two and "in use" for the other (docs/30, docs/60).
  if (power::quiet_ms() < static_cast<int64_t>(minutes) * 60000LL) return;
  bring_it_up("nobody has touched the board for the minutes of the setting");
  // **I el comptador torna a comen\u00e7ar**: a page that is already up is not asked for twice and the minutes of the setting are
  // counted again from now, or the next turn would ask for it again (docs/60).
  power::note_activity();
}

bool locked() {
  const char* id = cfg::lock_app();
  if (id == nullptr || id[0] == '\0') return false;
  return shell::app_running() && strcmp(shell::app_id(), id) == 0;
}

const char* app() { return cfg::lock_app(); }

}  // namespace lock
}  // namespace kernel
