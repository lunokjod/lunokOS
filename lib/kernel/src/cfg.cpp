// Where the settings of an app are kept, and which unit is behind `/cfg`.
#include "kernel/meter.h"
#include "kernel/cfg.h"

#include <stdio.h>
#include <string.h>

#include "kernel/log.h"
#include "kernel/vfs.h"
#include "pal/pal.h"

namespace kernel {
namespace cfg {
namespace {

const char kNamespace[] = "cfg";
const char kKey[] = "unit";
// **The keyboard is a setting of the system and not of an app** (0.93.0): it is chosen with `cfg keyboard <id>` and it
// survives a reboot like the unit does. The name is the id of one of the children of `ui`, and `cfg` keeps only the
// string: which keyboards exist is the UI's to say (docs/30, docs/70).
const char kKeyboardKey[] = "keyboard";
const size_t kNameCap = 12;

hal::IStorage* g_card = nullptr;
hal::IStorage* g_flash = nullptr;
hal::IKeyValue* g_nvs = nullptr;
// What the person asked for, and it is what is saved.
char g_preference[kNameCap] = "auto";
// What is really behind `/cfg` after looking at what is mounted.
const char* g_unit = "";
// The keyboard the person chose, the id of one of the children of `ui`.
char g_keyboard[kNameCap] = "qwerty";

bool same(const char* a, const char* b) { return a != nullptr && b != nullptr && strcmp(a, b) == 0; }

void remember() {
  if (g_nvs == nullptr) return;
  // **The settings live in the key-value store**, which is a write to the flash and not a variable
  // (docs/70, docs/75).
  meter::Scope scope(meter::kKvSet);
  g_nvs->set(kNamespace, kKey, g_preference, strlen(g_preference));
}

bool remember_keyboard() {
  if (g_nvs == nullptr) return false;
  meter::Scope scope(meter::kKvSet);
  return g_nvs->set(kNamespace, kKeyboardKey, g_keyboard, strlen(g_keyboard));
}

// Picks the unit and tells the VFS where `/cfg` points. The fallback is said out loud:
// asking for the card and getting the flash in silence would be the settings moving
// without anybody deciding it.
void apply() {
  const bool card_first = !same(g_preference, "littlefs");
  if (card_first && g_card != nullptr) {
    g_unit = "sd";
  } else if (g_flash != nullptr) {
    g_unit = "littlefs";
  } else if (g_card != nullptr) {
    g_unit = "sd";
  } else {
    g_unit = "";
  }

  if (same(g_preference, "sd") && g_card == nullptr) {
    log_printf(pal::kLogWarn, "CFG", "no card; the settings go to the flash");
  }
  hal::IStorage* storage = same(g_unit, "sd") ? g_card : g_flash;
  vfs::set_cfg_storage((g_unit[0] != '\0') ? storage : nullptr, g_unit);
}

}  // namespace

void boot(hal::IPlatform& platform) {
  g_nvs = platform.nvs();
  g_card = platform.storage("/sd");
  g_flash = platform.storage("/littlefs");

  char raw[kNameCap] = {0};
  size_t len = 0;
  // **The setting is read from the key-value store**, which is a chip and not a variable, and the
  // meter counts it like any other door (docs/70).
  bool found = false;
  if (g_nvs != nullptr) {
    meter::Scope scope(meter::kKvGet);
    found = g_nvs->get(kNamespace, kKey, raw, sizeof(raw) - 1, len) && len > 0;
  }
  if (found) {
    raw[len] = '\0';
    if (same(raw, "auto") || same(raw, "sd") || same(raw, "littlefs")) {
      snprintf(g_preference, sizeof(g_preference), "%s", raw);
    }
  }

  char raw_keyboard[kNameCap] = {0};
  size_t keyboard_len = 0;
  if (g_nvs != nullptr) {
    meter::Scope scope(meter::kKvGet);
    if (g_nvs->get(kNamespace, kKeyboardKey, raw_keyboard, sizeof(raw_keyboard) - 1, keyboard_len) &&
        keyboard_len > 0 && keyboard_len < sizeof(g_keyboard)) {
      raw_keyboard[keyboard_len] = '\0';
      snprintf(g_keyboard, sizeof(g_keyboard), "%s", raw_keyboard);
    }
  }
  apply();
  log_printf(pal::kLogInfo, "CFG", "/cfg on %s (asked for %s), keyboard %s",
             (g_unit[0] != '\0') ? g_unit : "nowhere", g_preference, g_keyboard);
}

const char* unit() { return g_unit; }

const char* preference() { return g_preference; }

const char* keyboard() { return g_keyboard; }

bool set_keyboard(const char* id) {
  if (id == nullptr || id[0] == '\0' || strlen(id) >= sizeof(g_keyboard)) return false;
  snprintf(g_keyboard, sizeof(g_keyboard), "%s", id);
  return remember_keyboard();
}

bool set_unit(const char* name) {
  if (name == nullptr) return false;
  if (!same(name, "auto") && !same(name, "sd") && !same(name, "littlefs")) return false;
  // Asking for a unit that is not there is refused instead of accepted and ignored: the
  // person has to know that the card is not mounted before trusting it with settings.
  if (same(name, "sd") && g_card == nullptr) return false;
  if (same(name, "littlefs") && g_flash == nullptr) return false;

  snprintf(g_preference, sizeof(g_preference), "%s", name);
  apply();
  remember();
  return true;
}

bool dir_for_app(char* out, size_t cap, const char* app) {
  if (out == nullptr || cap == 0) return false;
  if (g_unit[0] == '\0') return false;
  const char* prefix = same(g_unit, "sd") ? "/sd" : "/littlefs";
  const int written = (app != nullptr && app[0] != '\0')
                          ? snprintf(out, cap, "%s/cfg/%s", prefix, app)
                          : snprintf(out, cap, "%s/cfg", prefix);
  return written > 0 && static_cast<size_t>(written) < cap;
}

}  // namespace cfg
}  // namespace kernel
