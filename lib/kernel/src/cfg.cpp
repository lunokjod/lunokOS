// Where the settings of an app are kept, and which unit is behind `/cfg`.
#include "kernel/meter.h"
#include "kernel/cfg.h"

#include <stdio.h>
#include <stdlib.h>
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

// **The hour of the person, which is an offset from the chip's** (0.125.0): the key of the store, and the number the
// whole system asks for. It is kept as the text the command wrote —`+01:00`— because a person who reads the store
// with `nvs` has to be able to read what is there (docs/30, docs/60, docs/70).
const char kTzKey[] = "tz";
const size_t kTzCap = 8;  // "+01:00" and the null
// **Fourteen hours either way**, which is every zone the Earth has and a little more: a clock told to be sixteen hours
// ahead is a typo and not a place.
const int32_t kTzMaxMinutes = 14 * 60;

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
// The minutes the local hour is ahead of UTC, which is zero until somebody says otherwise (0.125.0).
int16_t g_tz_minutes = 0;

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

// The offset is written as the text the person typed, and never as a binary number: the store is a place people read
// with the `nvs` command, and a setting that reads as four unreadable bytes is a setting that hides itself (docs/70).
bool remember_tz() {
  if (g_nvs == nullptr) return false;
  char text[kTzCap] = {0};
  tz_text(text, sizeof(text), g_tz_minutes);
  meter::Scope scope(meter::kKvSet);
  return g_nvs->set(kNamespace, kTzKey, text, strlen(text));
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
  // The hour starts at UTC for the same reason: a bench that boots twice must not inherit the offset of the run before.
  g_tz_minutes = 0;

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
  // **The hour of the person, if the store says one** (0.125.0): a value that does not parse —a text an older build
  // wrote, a zone this clock cannot have— falls back to UTC and says so, because a clock silently twelve hours off is
  // worse than one that never moved (docs/30, docs/60).
  char raw_tz[kTzCap] = {0};
  size_t tz_len = 0;
  if (g_nvs != nullptr) {
    meter::Scope scope(meter::kKvGet);
    if (g_nvs->get(kNamespace, kTzKey, raw_tz, sizeof(raw_tz) - 1, tz_len) && tz_len > 0 &&
        tz_len < sizeof(raw_tz)) {
      raw_tz[tz_len] = '\0';
      int32_t minutes = 0;
      if (parse_tz(raw_tz, minutes)) {
        g_tz_minutes = static_cast<int16_t>(minutes);
      } else {
        log_printf(pal::kLogWarn, "CFG", "tz '%s' is not an offset; the clock stays on UTC", raw_tz);
      }
    }
  }
  apply();
  log_printf(pal::kLogInfo, "CFG", "/cfg on %s (asked for %s), keyboard %s, boot %s, home %s, tz %+03d:%02d",
             (g_unit[0] != '\0') ? g_unit : "nowhere", g_preference, g_keyboard, g_boot,
             (g_home[0] != '\0') ? g_home : "none", g_tz_minutes / 60, (g_tz_minutes < 0 ? -g_tz_minutes : g_tz_minutes) % 60);
}

const char* unit() { return g_unit; }

const char* preference() { return g_preference; }

const char* keyboard() { return g_keyboard; }

bool set_keyboard(const char* id) {
  if (id == nullptr || id[0] == '\0' || strlen(id) >= sizeof(g_keyboard)) return false;
  snprintf(g_keyboard, sizeof(g_keyboard), "%s", id);
  return remember_keyboard();
}

// **The offset of the hour, and the three calls that read and write it** (0.125.0). The number is kept in the store as
// text, and the boot puts the default of the build in front of the reading so that a second boot of the same object —the
// tests — does not inherit what the run before set.
int16_t tz_minutes() { return g_tz_minutes; }

bool tz_text(char* out, size_t cap, int32_t minutes) {
  if (out == nullptr || cap < kTzCap) return false;
  if (minutes > kTzMaxMinutes || minutes < -kTzMaxMinutes) return false;
  const int32_t magnitude = (minutes < 0) ? -minutes : minutes;
  snprintf(out, cap, "%c%02d:%02d", (minutes < 0) ? '-' : '+', static_cast<int>(magnitude / 60),
           static_cast<int>(magnitude % 60));
  return true;
}

bool parse_tz(const char* text, int32_t& minutes) {
  if (text == nullptr || text[0] == '\0') return false;
  const char* at = text;
  int32_t sign = 1;
  if (*at == '+' || *at == '-') {
    if (*at == '-') sign = -1;
    ++at;
  }
  if (*at == '\0') return false;

  // **A number on its own is a number of minutes, always**: `60` is one hour east, `-90` an hour and a half west and
  // `-0330` is three hundred and thirty minutes west —printed back as `-05:30`, so a person sees what was understood—.
  // The `±HH:MM` form is the one with a colon, and a bare number never means hours, because `0130` could be either
  // and a setting that can be read two ways is a setting that will be.
  bool digits = true;
  if (*at == '\0') return false;
  for (const char* c = at; *c != '\0'; ++c) {
    if (*c < '0' || *c > '9') {
      digits = false;
      break;
    }
  }
  if (digits) {
    if (strlen(at) > 5) return false;
    const int32_t value = static_cast<int32_t>(atoi(at));
    if (value > kTzMaxMinutes) return false;
    minutes = sign * value;
    return true;
  }

  // `H:MM` or `HH:MM`, and the colon is what says the first number is hours.
  int hours = 0;
  int mins = 0;
  if (sscanf(at, "%2d:%2d", &hours, &mins) != 2) return false;
  if (hours < 0 || hours > 14 || mins < 0 || mins > 59) return false;
  const int32_t total = hours * 60 + mins;
  if (total > kTzMaxMinutes) return false;
  minutes = sign * total;
  return true;
}

bool set_tz_minutes(int32_t minutes) {
  if (minutes > kTzMaxMinutes || minutes < -kTzMaxMinutes) return false;
  g_tz_minutes = static_cast<int16_t>(minutes);
  return remember_tz();
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
