// **El port sèrie cedit** (0.189.20): the flag, the owner and the one door the system's writers ask (docs/30, docs/50).
#include "kernel/serial.h"

#include <string.h>

#include "kernel/log.h"
#include "pal/pal.h"

namespace kernel {
namespace serial {
namespace {

// **Qui el té, i no dos** (0.189.20): an id of an app or `"the prompt"`, cut like every name of this system is; empty is
// "the system's". One owner and not a count, because a wire is one wire: the second app that asks hears who has it.
char g_owner[24] = {};
bool g_claimed = false;

void name_it(char* out, const char* by) {
  size_t at = 0;
  if (by != nullptr) {
    for (; by[at] != '\0' && at + 1 < sizeof(g_owner); ++at) out[at] = by[at];
  }
  out[at] = '\0';
}

}  // namespace

bool claim(const char* by, const char* why) {
  if (by == nullptr || by[0] == '\0') return false;
  if (g_claimed) {
    // **El mateix que el demana dues vegades no és dos amos** (0.189.21): the keyboard of the wire and the app that asked for
    // the port itself can both be the same name, and the second ask is the same wire.
    if (strcmp(g_owner, by) == 0) return true;
    log_printf(pal::kLogWarn, "SERIAL", "%s asks for the port and it is %s's", by, g_owner);
    return false;
  }
  name_it(g_owner, by);
  g_claimed = true;
  log_printf(pal::kLogInfo, "SERIAL", "the port is %s's: the system writes nothing to it (%s)", g_owner,
             (why != nullptr && why[0] != '\0') ? why : "no reason");
  return true;
}

bool release(const char* why) {
  if (!g_claimed) return false;
  log_printf(pal::kLogInfo, "SERIAL", "the port is the system's again (%s)",
             (why != nullptr && why[0] != '\0') ? why : "asked");
  g_owner[0] = '\0';
  g_claimed = false;
  return true;
}

bool release_owner(const char* by, const char* why) {
  if (!g_claimed || by == nullptr) return false;
  char mine[sizeof(g_owner)];
  name_it(mine, by);
  if (strcmp(mine, g_owner) != 0) return false;
  return release(why);
}

bool claimed() { return g_claimed; }

const char* owner() { return g_claimed ? g_owner : ""; }

// **Mentre és cedit, el sistema no hi escriu** (0.189.20): the writers of the system ask here and get no port, and the ones
// that already checked for `nullptr` get the silence for free.
pal::IConsole* console() {
  if (g_claimed) return nullptr;
  return pal::system().console();
}

// **El port, cedit o no** (0.199.2): the reader of the system needs it even when it is somebody's —the bytes of a person who
// is typing into a box (the keyboard of the wire) arrive here, and the console the system writes with is a different
// question— (docs/30, docs/50).
pal::IConsole* port() { return pal::system().console(); }

}  // namespace serial
}  // namespace kernel
