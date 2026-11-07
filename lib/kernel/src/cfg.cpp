// Where the settings of an app are kept, and which unit is behind `/cfg`.
#include "kernel/meter.h"
#include "kernel/cfg.h"

#include <stdio.h>
#include <string.h>

#include "kernel/log.h"
#include "kernel/splash.h"
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
// **The picture of the start is a word of the splash** (0.115.0): the two drawings of the logo are two moments, and
// this only keeps the name of the one the person chose. A word that is not one of them is refused where it is typed,
// so the boot never paints a guess (docs/30, docs/50).
const char kBootKey[] = "boot";
const size_t kNameCap = 12;

// **The app the board starts when it comes up** (0.119.0): the key of the store, and the name the build carries.
// A board that says nothing gets no home app, which is the prompt. `LUNOK_HOME_APP` is what `platformio.ini` writes
// per board (`-D LUNOK_HOME_APP=\"<id>\"`) and the fallback here is for a build that says nothing at all.
const char kHomeKey[] = "home";
#ifndef LUNOK_HOME_APP
#define LUNOK_HOME_APP ""
#endif
// The longest id the door presents is 39 characters plus the null (see `apps::Entry`), so the setting has room for
// the whole name of either half of the door —`squares` and `squares-SD`— and not one byte more.
const size_t kHomeCap = 40;

hal::IStorage* g_card = nullptr;
hal::IStorage* g_flash = nullptr;
hal::IKeyValue* g_nvs = nullptr;
// What the person asked for, and it is what is saved.
char g_preference[kNameCap] = "auto";
// What is really behind `/cfg` after looking at what is mounted.
const char* g_unit = "";
// The keyboard the person chose, the id of one of the children of `ui`.
char g_keyboard[kNameCap] = "qwerty";
// The drawing of the logo shown when the board comes up.
char g_boot[kNameCap] = "boot";
// The app the board starts by itself, empty for none, and it starts as the build's default (0.119.0).
char g_home[kHomeCap] = LUNOK_HOME_APP;

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

bool remember_boot() {
  if (g_nvs == nullptr) return false;
  meter::Scope scope(meter::kKvSet);
  return g_nvs->set(kNamespace, kBootKey, g_boot, strlen(g_boot));
}

// **None is a word and not an empty value** (0.119.0): the store keeps a string, and a key that was cleared has to
// read back as "no home app" on the next boot. `none` is therefore the one name an app cannot have as its home id,
// and the command says so where it is typed (docs/30, docs/70).
bool remember_home() {
  if (g_nvs == nullptr) return false;
  const char* value = (g_home[0] != '\0') ? g_home : "none";
  meter::Scope scope(meter::kKvSet);
  return g_nvs->set(kNamespace, kHomeKey, value, strlen(value));
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
  // **The home app starts at the name the build carries** (0.119.0) and only the store can move it: a second boot
  // of the same object —the tests boot the kernel again— must not inherit the name of the run before.
  snprintf(g_home, sizeof(g_home), "%s", LUNOK_HOME_APP);

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

  // **The drawing of the logo, if the store says one that this build knows**: a word left by an older build —or by
  // a build with another drawing— falls back to the word `boot` and says so, because a boot that paints nothing out
  // of a setting nobody can read is a boot that hides a broken setting (docs/30, docs/50, docs/70).
  char raw_boot[kNameCap] = {0};
  size_t boot_len = 0;
  if (g_nvs != nullptr) {
    meter::Scope scope(meter::kKvGet);
    if (g_nvs->get(kNamespace, kBootKey, raw_boot, sizeof(raw_boot) - 1, boot_len) && boot_len > 0 &&
        boot_len < sizeof(g_boot)) {
      raw_boot[boot_len] = '\0';
      if (is_a_boot_word(raw_boot)) {
        snprintf(g_boot, sizeof(g_boot), "%s", raw_boot);
      } else {
        log_printf(pal::kLogWarn, "CFG", "no boot picture '%s' in this build; boot stays", raw_boot);
      }
    }
  }
  // **The app the board starts by itself, if the store says one** (0.119.0): the word `none` is a board that comes
  // up at the prompt, which is what a person gets when they clear it. A name that is not on the disk right now is
  // kept and not refused: the card that carries the app may be out of the slot this morning, and a boot that had to
  // argue about it would be a boot that cannot be told anything (docs/30, docs/40, docs/70).
  char raw_home[kHomeCap] = {0};
  size_t home_len = 0;
  if (g_nvs != nullptr) {
    meter::Scope scope(meter::kKvGet);
    if (g_nvs->get(kNamespace, kHomeKey, raw_home, sizeof(raw_home) - 1, home_len) && home_len > 0 &&
        home_len < sizeof(g_home)) {
      raw_home[home_len] = '\0';
      if (strcmp(raw_home, "none") == 0) {
        g_home[0] = '\0';
      } else {
        snprintf(g_home, sizeof(g_home), "%s", raw_home);
      }
    }
  }
  apply();
  log_printf(pal::kLogInfo, "CFG", "/cfg on %s (asked for %s), keyboard %s, boot %s, home %s",
             (g_unit[0] != '\0') ? g_unit : "nowhere", g_preference, g_keyboard, g_boot,
             (g_home[0] != '\0') ? g_home : "none");
}

const char* unit() { return g_unit; }

const char* preference() { return g_preference; }

const char* keyboard() { return g_keyboard; }

bool set_keyboard(const char* id) {
  if (id == nullptr || id[0] == '\0' || strlen(id) >= sizeof(g_keyboard)) return false;
  snprintf(g_keyboard, sizeof(g_keyboard), "%s", id);
  return remember_keyboard();
}

const char* boot_splash() { return g_boot; }

// **The two words that are drawings of the logo**: the splash is the one that says which moments exist, so the
// question is asked there and not repeated here as a list that could drift (docs/30, docs/50).
bool is_a_boot_word(const char* word) {
  if (word == nullptr) return false;
  const splash::Kind kind = splash::kind_of(word);
  return kind == splash::kBoot || kind == splash::kBootAlt;
}

bool set_boot_splash(const char* word) {
  // **A word that is not a drawing of the logo is refused where it is typed**: the boot has no room to ask anything
  // and a setting that lands nowhere would be a boot that lies about what it will paint (docs/30, docs/50).
  if (!is_a_boot_word(word) || strlen(word) >= sizeof(g_boot)) return false;
  snprintf(g_boot, sizeof(g_boot), "%s", word);
  return remember_boot();
}

const char* home_app() { return g_home; }

const char* home_default() { return LUNOK_HOME_APP; }

bool set_home_app(const char* id) {
  // **No name and the word `none` are the same setting**: a board that comes up at the prompt (docs/30).
  if (id == nullptr || id[0] == '\0' || same(id, "none")) {
    g_home[0] = '\0';
    return remember_home();
  }
  if (strlen(id) >= sizeof(g_home)) return false;
  snprintf(g_home, sizeof(g_home), "%s", id);
  return remember_home();
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
