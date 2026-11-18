// Where the settings of an app are kept, and which unit is behind `/cfg`.
#include "kernel/meter.h"
#include "kernel/cfg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/calendar.h"
#include "kernel/log.h"
#include "kernel/power.h"
#include "kernel/splash.h"
#include "kernel/vfs.h"
#include "luabind/luabind.h"
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

// **The calendar a person wants** (0.170.0): the leap day and the first day of the week, kept as the words a person
// reads with `nvs` —`leap on`/`leap off` and `week monday`/`week sunday`— and not as numbers nobody can look up
// (docs/60, docs/70).
const char kLeapKey[] = "leap";
// **The hour of summer, as the two words a person reads in the store** (0.182.0, docs/30, docs/70).
const char kDstKey[] = "dst";
const char kWeekKey[] = "week";

// **The slice of a turn of Lua, which is the latency of the system** (0.174.7): how many instructions of an app a turn
// is worth before the kernel takes the turn back for the glass, the keys and the radio. It is kept as the decimal text a
// person reads with `nvs` —`20000`— for the same reason the hour is kept as `+01:00`, and the number itself lives in
// `luabind`, which is the module that counts it: this door keeps the choice and puts it there at boot (docs/20, docs/75).
const char kTurnKey[] = "turn";
const size_t kTurnCap = 8;  // "200000" and the null
// **El parpalleig del cursor** (0.189.34): the key of the store and the room the number has in it, which is a number of
// milliseconds and nothing else —the option is called `cursor delay` at the prompt, and the store keeps the one word of the
// key, because a key of a key-value store is one word (docs/30, docs/70).
const char kCursorKey[] = "cursor";
const size_t kCursorCap = 8;  // "5000" and the null
// **Si el llançador ordena les apps pels usos** (0.189.13): kept as the words a person reads in the store —`on` and
// `off`— like the calendar and the hour of summer, and off until somebody asks for it (docs/30, docs/40).
const char kSortKey[] = "sort";
// **L'app dels ajustos** (0.189.109): the name the gear of the panel runs, kept as a name like the app of the start and not as a
// path, for the same reason —a path in the settings is a way to make a board open any file at its first breath— (docs/30, docs/40).
const char kSettingsKey[] = "settings";
// **Les velocitats permeses** (0.189.107): the three bits of the steps the governor may use, kept as the list a person reads
// —`80,160,240`— and never as a number, for the same reason the hour is `+01:00` (docs/50, docs/70).
const char kFreqKey[] = "freq";
const size_t kFreqCap = 12;  // "80,160,240" and the null
// **El bloqueig de la pantalla** (0.189.150): l'app que tapa la pàgina, si s'usa a l'arrencada i al despertar, i quants minuts
// sense tocar res la fan sortir. Tres claus i les tres amb les paraules que una persona llegeix al `nvs` (docs/30, docs/60,
// docs/70).
const char kLockKey[] = "lock";
const char kLockOnKey[] = "lockon";
const char kSleepKey[] = "sleep";
const char kLockAfterKey[] = "lockafter";
const size_t kLockCap = 40;
const uint8_t kLockAfterDefault = 10;
const char kOn[] = "on";
const char kOff[] = "off";
const char kMonday[] = "monday";
const char kSunday[] = "sunday";

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
// **L'app que obre el botó dels ajustos** (0.189.109): `settings` unless the store says another name, and empty when a person said
// `none` —a gear that does nothing is a choice and the panel says so before it asks for anything— (docs/30, docs/40).
char g_settings[kNameCap] = "settings";
// **El bloqueig de la pantalla** (0.189.150): the page, the switch and the minutes. The page starts as `device_lock` —the one
// this system carries for exactly this—, the switch off, and ten minutes, which is the middle of the three a person can pick.
char g_lock[kLockCap] = "device_lock";
bool g_lock_on = false;
// **El son sol de la placa** (0.190.5): on by default, which is the rule of law 9; the page of the settings turns it off and this is what
// remembers the answer (docs/60).
bool g_sleep_on = true;
uint8_t g_lock_after = kLockAfterDefault;
// The minutes the local hour is ahead of UTC, which is zero until somebody says otherwise (0.125.0).
int16_t g_tz_minutes = 0;
// What the store said, which is also what the calendar is using: the two are written together.
bool g_leap_years = true;
// **The hour of summer is off until somebody says otherwise**: a clock that adds an hour on its own is a clock that lies in
// the winter (0.182.0, docs/60).
bool g_dst = false;
uint8_t g_week_start = 1;
// **A launcher shows the door's order until a person asks for another** (0.189.13): the count of uses is the launcher's
// and this is only the switch that says whether it sorts by it (docs/30, docs/40).
bool g_sort_by_usage = false;
// **Les tres velocitats viuen a `power` i no aquí** (0.189.107): the mask is the policy's own number —it is the governor that
// has to obey it— and this door reads and writes it there. A byte here and a byte there were eight of `.bss` in a build whose
// `dram0.bss` is exactly full (docs/75).

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
bool remember_leap() {
  if (g_nvs == nullptr) return false;
  const char* word = g_leap_years ? kOn : kOff;
  meter::Scope scope(meter::kKvSet);
  return g_nvs->set(kNamespace, kLeapKey, word, strlen(word));
}

bool remember_dst() {
  if (g_nvs == nullptr) return false;
  const char* word = g_dst ? kOn : kOff;
  meter::Scope scope(meter::kKvSet);
  return g_nvs->set(kNamespace, kDstKey, word, strlen(word));
}

bool remember_week() {
  if (g_nvs == nullptr) return false;
  const char* word = (g_week_start == 7) ? kSunday : kMonday;
  meter::Scope scope(meter::kKvSet);
  return g_nvs->set(kNamespace, kWeekKey, word, strlen(word));
}

bool remember_turn() {
  if (g_nvs == nullptr) return false;
  char text[kTurnCap] = {0};
  snprintf(text, sizeof(text), "%u", static_cast<unsigned>(luabind::turn_budget()));
  meter::Scope scope(meter::kKvSet);
  return g_nvs->set(kNamespace, kTurnKey, text, strlen(text));
}

bool remember_cursor() {
  if (g_nvs == nullptr) return false;
  char text[kCursorCap] = {0};
  snprintf(text, sizeof(text), "%d", static_cast<int>(luabind::caret_blink_ms()));
  meter::Scope scope(meter::kKvSet);
  return g_nvs->set(kNamespace, kCursorKey, text, strlen(text));
}

bool remember_sort() {
  if (g_nvs == nullptr) return false;
  const char* word = g_sort_by_usage ? kOn : kOff;
  meter::Scope scope(meter::kKvSet);
  return g_nvs->set(kNamespace, kSortKey, word, strlen(word));
}

bool remember_settings() {
  if (g_nvs == nullptr) return false;
  const char* value = (g_settings[0] != '\0') ? g_settings : "none";
  meter::Scope scope(meter::kKvSet);
  return g_nvs->set(kNamespace, kSettingsKey, value, strlen(value));
}

bool remember_lock() {
  if (g_nvs == nullptr) return false;
  const char* value = (g_lock[0] != '\0') ? g_lock : "none";
  meter::Scope scope(meter::kKvSet);
  return g_nvs->set(kNamespace, kLockKey, value, strlen(value));
}

bool remember_sleep_on() {
  if (g_nvs == nullptr) return false;
  const char* word = g_sleep_on ? kOn : kOff;
  meter::Scope scope(meter::kKvSet);
  return g_nvs->set(kNamespace, kSleepKey, word, strlen(word));
}

bool remember_lock_on() {
  if (g_nvs == nullptr) return false;
  const char* word = g_lock_on ? kOn : kOff;
  meter::Scope scope(meter::kKvSet);
  return g_nvs->set(kNamespace, kLockOnKey, word, strlen(word));
}

bool remember_lock_after() {
  if (g_nvs == nullptr) return false;
  char text[8];
  snprintf(text, sizeof(text), "%u", static_cast<unsigned>(g_lock_after));
  meter::Scope scope(meter::kKvSet);
  return g_nvs->set(kNamespace, kLockAfterKey, text, strlen(text));
}

bool remember_freq() {
  if (g_nvs == nullptr) return false;
  char text[kFreqCap] = {0};
  if (!freq_text(text, sizeof(text), freq_mask())) return false;
  meter::Scope scope(meter::kKvSet);
  return g_nvs->set(kNamespace, kFreqKey, text, strlen(text));
}

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
  // **I el terra de les apps, que és la mateixa tria** (0.190.8, l'amo: *«perqué es perden les dades d'historic de Battery i
  // TimeClock al pujar un firmware nou? les dades no es guarden a l'SD ? … He perdut totes les dades d'avui»*): `/data/<id>/`
  // era el flaix sempre —`vfs::init` el muntava sobre littlefs i prou—, o sigui que `pio run -t uploadfs`, que reescriu la
  // partició littlefs sencera des de `build/disk/<entorn>`, s'enduia amb cada firmware el registre de la bateria, les marques
  // del rellotge, els events de l'agenda i les mostres del temps. Amb la targeta triada —o amb `auto`, que és la targeta quan
  // n'hi ha— `/data` és a la targeta, i això un flaix no ho toca (docs/30, docs/70, docs/80).
  //
  // **No és un ajust nou**: és el «disc preferit» que ja hi era (`cfg auto|sd|littlefs`, la fila *Preferred disk* de la
  // pestanya Disk) i que decidia només `/cfg`. Una segona preferència per a la mateixa pregunta seria dues portes que poden
  // dir coses diferents i vint bytes de DRAM que la Paper no té (mesurat el 18/11: `dram0_0_seg` overflowed by 24 bytes).
  if (g_unit[0] != '\0' && storage != nullptr) {
    storage->mkdir("data");
    vfs::set_data_storage(storage, g_unit);
    log_printf(pal::kLogInfo, "CFG", "the ground of the apps is the %s (/data is %s/data)",
               same(g_unit, "sd") ? "card" : "flash", same(g_unit, "sd") ? "/sd" : "/littlefs");
  }
}

}  // namespace

void boot(hal::IPlatform& platform) {
  g_nvs = platform.nvs();
  g_card = platform.storage("/sd");
  g_flash = platform.storage("/littlefs");
  // **The home app starts at the name the build carries** (0.119.0) and only the store can move it: a second boot
  // of the same object —the tests boot the kernel again— must not inherit the name of the run before.
  snprintf(g_home, sizeof(g_home), "%s", LUNOK_HOME_APP);
  // The hour starts at UTC for the same reason: an emulator that boots twice must not inherit the offset of the run before.
  g_tz_minutes = 0;
  // **And the calendar starts as this system was born**: the leap day on and the week from Monday, put in the calendar
  // before the store is read so that a second boot of the same object —the tests— does not inherit the run before
  // (docs/60).
  g_leap_years = true;
  g_dst = false;
  g_week_start = 1;
  calendar::set_leap_years(true);
  calendar::set_week_start(1);

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
  // **The calendar, if the store says one** (0.170.0): a word this build does not know —a value left by an older
  // build, a typo in the store— falls back to the calendar of the board and says so, because a calendar that silently
  // hides a day is worse than one that never moved (docs/60).
  char raw_leap[kNameCap] = {0};
  size_t leap_len = 0;
  if (g_nvs != nullptr) {
    meter::Scope scope(meter::kKvGet);
    if (g_nvs->get(kNamespace, kLeapKey, raw_leap, sizeof(raw_leap) - 1, leap_len) && leap_len > 0 &&
        leap_len < sizeof(raw_leap)) {
      raw_leap[leap_len] = '\0';
      if (same(raw_leap, kOn) || same(raw_leap, kOff)) {
        g_leap_years = same(raw_leap, kOn);
      } else {
        log_printf(pal::kLogWarn, "CFG", "leap '%s' is not on or off; the leap day stays", raw_leap);
      }
    }
  }
  char raw_dst[kNameCap] = {0};
  size_t dst_len = 0;
  if (g_nvs != nullptr) {
    meter::Scope scope(meter::kKvGet);
    if (g_nvs->get(kNamespace, kDstKey, raw_dst, sizeof(raw_dst) - 1, dst_len) && dst_len > 0 &&
        dst_len < sizeof(raw_dst)) {
      raw_dst[dst_len] = '\0';
      if (same(raw_dst, kOn) || same(raw_dst, kOff)) {
        g_dst = same(raw_dst, kOn);
      } else {
        log_printf(pal::kLogWarn, "CFG", "dst '%s' is not on or off; the hour of summer stays off", raw_dst);
      }
    }
  }
  char raw_week[kNameCap] = {0};
  size_t week_len = 0;
  if (g_nvs != nullptr) {
    meter::Scope scope(meter::kKvGet);
    if (g_nvs->get(kNamespace, kWeekKey, raw_week, sizeof(raw_week) - 1, week_len) && week_len > 0 &&
        week_len < sizeof(raw_week)) {
      raw_week[week_len] = '\0';
      if (same(raw_week, kMonday) || same(raw_week, kSunday)) {
        g_week_start = same(raw_week, kSunday) ? 7 : 1;
      } else {
        log_printf(pal::kLogWarn, "CFG", "week '%s' is not monday or sunday; the week starts on Monday", raw_week);
      }
    }
  }
  calendar::set_leap_years(g_leap_years);
  calendar::set_week_start(g_week_start);
  // **The slice of a turn, if the store says one** (0.174.7): a number that a slice cannot be —an older build's, a
  // typo, a zero— falls back to the one this build ships and says so, because an app that is never cut is a board that
  // stops answering the hand and nobody would know why (docs/20, docs/75).
  luabind::set_turn_budget(luabind::turn_budget_default());
  // **I el parpalleig del cursor torna al del build abans de llegir el disc** (0.189.34): the same rule the slice and the
  // offset follow —a second boot of the same object does not inherit what the run before wrote— (docs/30, docs/90).
  luabind::set_caret_blink_ms(luabind::caret_blink_default_ms());
  char raw_cursor[kCursorCap] = {0};
  size_t cursor_len = 0;
  if (g_nvs != nullptr) {
    meter::Scope scope(meter::kKvGet);
    if (g_nvs->get(kNamespace, kCursorKey, raw_cursor, sizeof(raw_cursor) - 1, cursor_len) && cursor_len > 0 &&
        cursor_len < sizeof(raw_cursor)) {
      raw_cursor[cursor_len] = '\0';
      int32_t delay = 0;
      if (parse_cursor_delay(raw_cursor, delay)) {
        luabind::set_caret_blink_ms(delay);
      } else {
        log_printf(pal::kLogWarn, "CFG", "cursor delay '%s' is not a blink of %d to %d ms; the cursor blinks at %d",
                   raw_cursor, static_cast<int>(luabind::caret_blink_min_ms()),
                   static_cast<int>(luabind::caret_blink_max_ms()), static_cast<int>(luabind::caret_blink_ms()));
      }
    }
  }
  char raw_turn[kTurnCap] = {0};
  size_t turn_len = 0;
  if (g_nvs != nullptr) {
    meter::Scope scope(meter::kKvGet);
    if (g_nvs->get(kNamespace, kTurnKey, raw_turn, sizeof(raw_turn) - 1, turn_len) && turn_len > 0 &&
        turn_len < sizeof(raw_turn)) {
      raw_turn[turn_len] = '\0';
      uint32_t instructions = 0;
      if (parse_turn(raw_turn, instructions)) {
        luabind::set_turn_budget(instructions);
      } else {
        log_printf(pal::kLogWarn, "CFG", "turn '%s' is not a slice of %u to %u instructions; turn stays at %u",
                   raw_turn, static_cast<unsigned>(luabind::turn_budget_min()),
                   static_cast<unsigned>(luabind::turn_budget_max()),
                   static_cast<unsigned>(luabind::turn_budget()));
      }
    }
  }
  // **L'app dels ajustos, si la botiga en diu una** (0.189.109): `none` is the word that puts the gear back to nothing, exactly like
  // the app of the start, and a name longer than an app of this system has is not a name and is said (docs/30, docs/40).
  snprintf(g_settings, sizeof(g_settings), "%s", "settings");
  {
    char raw_settings[kNameCap] = {0};
    size_t settings_len = 0;
    if (g_nvs != nullptr) {
      meter::Scope scope(meter::kKvGet);
      if (g_nvs->get(kNamespace, kSettingsKey, raw_settings, sizeof(raw_settings) - 1, settings_len) && settings_len > 0 &&
          settings_len < sizeof(raw_settings)) {
        raw_settings[settings_len] = '\0';
        if (same(raw_settings, "none")) {
          g_settings[0] = '\0';
        } else {
          snprintf(g_settings, sizeof(g_settings), "%s", raw_settings);
        }
      }
    }
  }
  // **El bloqueig de la pantalla, si la botiga en diu res** (0.189.150): the page, the switch and the minutes. A `lockon` that
  // is neither `on` nor `off` leaves the switch off and says so —a switch that reads half a word is a board that locks
  // itself or does not for a reason nobody can see— and the minutes only take the three the page offers.
  {
    char raw_lock[kLockCap] = {0};
    size_t lock_len = 0;
    if (g_nvs != nullptr) {
      meter::Scope scope(meter::kKvGet);
      if (g_nvs->get(kNamespace, kLockKey, raw_lock, sizeof(raw_lock) - 1, lock_len) && lock_len > 0 &&
          lock_len < sizeof(raw_lock)) {
        raw_lock[lock_len] = '\0';
        if (same(raw_lock, "none")) {
          g_lock[0] = '\0';
        } else {
          snprintf(g_lock, sizeof(g_lock), "%s", raw_lock);
        }
      }
      char raw_sleep[8] = {0};
      size_t sleep_len = 0;
      if (g_nvs->get(kNamespace, kSleepKey, raw_sleep, sizeof(raw_sleep) - 1, sleep_len) && sleep_len > 0 &&
          sleep_len < sizeof(raw_sleep)) {
        raw_sleep[sleep_len] = '\0';
        if (same(raw_sleep, kOn) || same(raw_sleep, kOff)) {
          g_sleep_on = same(raw_sleep, kOn);
        } else {
          log_printf(pal::kLogWarn, "CFG", "sleep '%s' is not on or off; the board keeps its own policy", raw_sleep);
        }
      }
      char raw_on[8] = {0};
      size_t on_len = 0;
      if (g_nvs->get(kNamespace, kLockOnKey, raw_on, sizeof(raw_on) - 1, on_len) && on_len > 0 &&
          on_len < sizeof(raw_on)) {
        raw_on[on_len] = '\0';
        if (same(raw_on, kOn) || same(raw_on, kOff)) {
          g_lock_on = same(raw_on, kOn);
        } else {
          log_printf(pal::kLogWarn, "CFG", "lock '%s' is not on or off; the screen is not locked", raw_on);
        }
      }
      char raw_after[8] = {0};
      size_t after_len = 0;
      if (g_nvs->get(kNamespace, kLockAfterKey, raw_after, sizeof(raw_after) - 1, after_len) && after_len > 0 &&
          after_len < sizeof(raw_after)) {
        raw_after[after_len] = '\0';
        const int minutes = atoi(raw_after);
        if (minutes == 5 || minutes == 10 || minutes == 15) {
          g_lock_after = static_cast<uint8_t>(minutes);
        } else {
          log_printf(pal::kLogWarn, "CFG", "lock after '%s' is not 5, 10 or 15 minutes; %u stays", raw_after,
                     static_cast<unsigned>(g_lock_after));
        }
      }
    }
  }
  // **Les velocitats permeses, llegides abans d'aplicar res**: the mask is what the governor may use, so it has to be in
  // `power` before the first paint of the boot asks for the top step (0.189.107, docs/50, docs/60).
  power::set_allowed_steps(kFreqAll);
  {
    char raw_freq[kFreqCap] = {0};
    size_t freq_len = 0;
    if (g_nvs != nullptr) {
      meter::Scope scope(meter::kKvGet);
      if (g_nvs->get(kNamespace, kFreqKey, raw_freq, sizeof(raw_freq) - 1, freq_len) && freq_len > 0 &&
          freq_len < sizeof(raw_freq)) {
        raw_freq[freq_len] = '\0';
        uint8_t mask = 0;
        if (parse_freq(raw_freq, mask)) {
          power::set_allowed_steps(mask);
        } else {
          log_printf(pal::kLogWarn, "CFG", "freq '%s' is not a list of 80, 160 and 240; the board may use all three",
                     raw_freq);
        }
      }
    }
  }
  apply();
  // **I si el llançador ordena les apps pels usos** (0.189.13): a word that is neither `on` nor `off` leaves the switch
  // where it was —off— and says so, like the calendar does with a week that is not a week (docs/30, docs/40).
  char raw_sort[kNameCap] = {0};
  size_t sort_len = 0;
  if (g_nvs != nullptr) {
    meter::Scope scope(meter::kKvGet);
    if (g_nvs->get(kNamespace, kSortKey, raw_sort, sizeof(raw_sort) - 1, sort_len) && sort_len > 0 &&
        sort_len < sizeof(raw_sort)) {
      raw_sort[sort_len] = '\0';
      if (same(raw_sort, kOn) || same(raw_sort, kOff)) {
        g_sort_by_usage = same(raw_sort, kOn);
      } else {
        log_printf(pal::kLogWarn, "CFG", "sort '%s' is not on or off; the launcher keeps the door's order", raw_sort);
      }
    }
  }
  char freq_words[kFreqCap] = {0};
  freq_text(freq_words, sizeof(freq_words), freq_mask());
  // **Dues línies i no una** (0.189.150): the summary had grown past the hundred and ninety-two bytes of a line of the
  // registry, o sigui que la cua s'escrivia i no es veia —el valor de `settings app` feia versions que no es podia llegir i els
  // tres del bloqueig hi queien de ple—. La primera línia és la placa (el magatzem, el teclat, la imatge, el calendari, la
  // marxa i el processador) i la segona **les apps que una persona ha triat**, que és la pestanya System llegida en veu alta
  // (docs/30, docs/45).
  log_printf(pal::kLogInfo, "CFG",
             "/cfg on %s (asked for %s), keyboard %s, boot %s, tz %+03d:%02d, leap %s, dst %s, week %s, turn %u, "
             "sort apps by usage %s, cursor delay %d ms, cpu allows %s",
             (g_unit[0] != '\0') ? g_unit : "nowhere", g_preference, g_keyboard, g_boot, g_tz_minutes / 60,
             (g_tz_minutes < 0 ? -g_tz_minutes : g_tz_minutes) % 60, g_leap_years ? kOn : kOff, g_dst ? kOn : kOff,
             (g_week_start == 7) ? kSunday : kMonday, static_cast<unsigned>(luabind::turn_budget()),
             g_sort_by_usage ? kOn : kOff, static_cast<int>(luabind::caret_blink_ms()), freq_words);
  log_printf(pal::kLogInfo, "CFG", "the apps a person chose: home %s, settings %s, lock %s; the lock is %s and waits %u min",
             (g_home[0] != '\0') ? g_home : "none", (g_settings[0] != '\0') ? g_settings : "none",
             (g_lock[0] != '\0') ? g_lock : "none", g_lock_on ? kOn : kOff, static_cast<unsigned>(g_lock_after));
}

const char* unit() { return g_unit; }

// **The slice of a turn: read in the module that counts it and written here** (0.174.7). The door answers the number in
// force —the block ceiling of what was asked— and not the one that was typed, so what `cfg turn` prints is what the hook
// compares with (docs/20, docs/75).
uint32_t turn_instructions() { return luabind::turn_budget(); }

// **The launcher's switch** (0.189.13): off until the store says `on`, and it is a word and not a bit for the same reason
// the calendar's is —a person reads the store with `nvs` (docs/30, docs/70).
bool sort_by_usage() { return g_sort_by_usage; }

// **I el son sol** (0.190.5): the value of the page is the policy's (`power`), so writing it here turns the board's rule off in the same
// moment a person moves the switch instead of on the next boot (docs/60).
bool sleep_on() { return g_sleep_on; }

bool set_sleep_on(bool on) {
  g_sleep_on = on;
  power::set_idle_sleep_allowed(on);
  return remember_sleep_on();
}

bool set_sort_by_usage(bool on) {
  g_sort_by_usage = on;
  return remember_sort();
}

// **The six words a switch takes** (0.189.13): the person writes the one their head reaches for and the store keeps the
// two the prompt prints, so `cfg sort yes` and `cfg sort on` end in the same line of the key-value store (docs/30).
bool parse_switch(const char* text, bool& on) {
  if (text == nullptr) return false;
  if (same(text, "on") || same(text, "true") || same(text, "yes")) {
    on = true;
    return true;
  }
  if (same(text, "off") || same(text, "false") || same(text, "no")) {
    on = false;
    return true;
  }
  return false;
}

uint32_t turn_default() { return luabind::turn_budget_default(); }

bool parse_turn(const char* text, uint32_t& instructions) {
  if (text == nullptr || text[0] == '\0') return false;
  // **Digits and nothing else**: a slice is a count of instructions, so a sign, a space or a unit is not one.
  uint32_t value = 0;
  for (const char* c = text; *c != '\0'; ++c) {
    if (*c < '0' || *c > '9') return false;
    value = value * 10u + static_cast<uint32_t>(*c - '0');
    if (value > luabind::turn_budget_max()) return false;
  }
  if (value < luabind::turn_budget_min() || value > luabind::turn_budget_max()) return false;
  instructions = value;
  return true;
}

bool set_turn_instructions(uint32_t instructions) {
  if (!luabind::set_turn_budget(instructions)) return false;
  return remember_turn();
}

const char* settings_app() { return g_settings; }

const char* settings_default() { return "settings"; }

bool set_settings_app(const char* id) {
  if (id == nullptr || id[0] == '\0' || same(id, "none")) {
    g_settings[0] = '\0';
    return remember_settings();
  }
  if (strlen(id) >= sizeof(g_settings)) return false;
  snprintf(g_settings, sizeof(g_settings), "%s", id);
  return remember_settings();
}

const char* lock_app() { return g_lock; }

const char* lock_default() { return "device_lock"; }

bool set_lock_app(const char* id) {
  if (id == nullptr || id[0] == '\0' || same(id, "none")) {
    g_lock[0] = '\0';
    return remember_lock();
  }
  if (strlen(id) >= sizeof(g_lock)) return false;
  snprintf(g_lock, sizeof(g_lock), "%s", id);
  return remember_lock();
}

bool lock_on() { return g_lock_on; }

bool set_lock_on(bool on) {
  g_lock_on = on;
  return remember_lock_on();
}

uint8_t lock_after_minutes() { return g_lock_after; }

bool set_lock_after_minutes(uint8_t minutes) {
  // **Cinc, deu o quinze i res més**: the list of the page is these three, and a number that is not one of them would be a
  // setting the page cannot show and a person cannot change back (docs/30, docs/60).
  if (minutes != 5 && minutes != 10 && minutes != 15) return false;
  g_lock_after = minutes;
  return remember_lock_after();
}

uint32_t freq_step_of(uint8_t bit) {
  switch (bit) {
    case 0x01: return power::kIdleMhz;
    case 0x02: return power::kRestingMhz;
    case 0x04: return power::kBusyMhz;
    default: return 0;
  }
}

uint8_t freq_mask() { return power::allowed_steps(); }

bool freq_allowed(uint32_t mhz) {
  const uint8_t mask = freq_mask();
  for (uint8_t bit = 0x01; bit <= 0x04; bit = static_cast<uint8_t>(bit << 1)) {
    if ((mask & bit) != 0 && freq_step_of(bit) == mhz) return true;
  }
  return false;
}

bool set_freq_allowed(uint32_t mhz, bool on) {
  uint8_t bit = 0;
  for (uint8_t candidate = 0x01; candidate <= 0x04; candidate = static_cast<uint8_t>(candidate << 1)) {
    if (freq_step_of(candidate) == mhz) bit = candidate;
  }
  if (bit == 0) return false;
  const uint8_t wanted = on ? static_cast<uint8_t>(freq_mask() | bit) : static_cast<uint8_t>(freq_mask() & ~bit);
  // **L'última no se'n pot anar**: a mask with no bit is a processor with no speed, and a governor that cannot choose is a
  // board that does not come back from the answer (docs/50).
  if (wanted == 0) return false;
  power::set_allowed_steps(wanted);
  return remember_freq();
}

bool freq_text(char* out, size_t cap, uint8_t mask) {
  if (out == nullptr || cap < 4) return false;
  out[0] = '\0';
  size_t used = 0;
  for (uint8_t bit = 0x01; bit <= 0x04; bit = static_cast<uint8_t>(bit << 1)) {
    if ((mask & bit) == 0) continue;
    char one[8] = {0};
    snprintf(one, sizeof(one), "%s%u", (used == 0) ? "" : ",", static_cast<unsigned>(freq_step_of(bit)));
    const size_t more = strlen(one);
    if (used + more + 1 > cap) return false;
    memcpy(out + used, one, more + 1);
    used += more;
  }
  return used > 0;
}

bool parse_freq(const char* text, uint8_t& mask) {
  if (text == nullptr || text[0] == '\0') return false;
  if (same(text, "all") || same(text, "auto")) {
    mask = kFreqAll;
    return true;
  }
  uint8_t found = 0;
  const char* at = text;
  while (*at != '\0') {
    while (*at == ',' || *at == ' ' || *at == ';') ++at;
    if (*at == '\0') break;
    // **A step is 80, 160 or 240 and nothing else**: the three numbers of the chip, so a typo is refused here and not
    // discovered by a governor that has no step to go to (docs/50).
    uint32_t value = 0;
    int digits = 0;
    while (*at >= '0' && *at <= '9') {
      value = value * 10u + static_cast<uint32_t>(*at - '0');
      ++at;
      ++digits;
    }
    if (digits == 0) return false;
    bool known = false;
    for (uint8_t bit = 0x01; bit <= 0x04; bit = static_cast<uint8_t>(bit << 1)) {
      if (freq_step_of(bit) != value) continue;
      found = static_cast<uint8_t>(found | bit);
      known = true;
    }
    if (!known) return false;
  }
  if (found == 0) return false;
  mask = found;
  return true;
}

// **El parpalleig del cursor, que és una preferència i no una constant** (0.189.34): the number lives in `luabind`, which
// owns the clock of the caret, and this door keeps what the person chose and puts it there at the boot — the same shape the
// slice of a turn has, and for the same reason: an app cannot change the cursor of every other page (docs/30, docs/90).
int32_t cursor_delay() { return static_cast<int32_t>(luabind::caret_blink_ms()); }

int32_t cursor_delay_default() { return static_cast<int32_t>(luabind::caret_blink_default_ms()); }

bool parse_cursor_delay(const char* text, int32_t& delay) {
  if (text == nullptr || text[0] == '\0') return false;
  // **Digits and nothing else**: a blink is a count of milliseconds, so a sign, a space, a unit or a `ms` is not one.
  int32_t value = 0;
  for (const char* c = text; *c != '\0'; ++c) {
    if (*c < '0' || *c > '9') return false;
    value = value * 10 + static_cast<int32_t>(*c - '0');
    if (value > static_cast<int32_t>(luabind::caret_blink_max_ms())) return false;
  }
  if (value < static_cast<int32_t>(luabind::caret_blink_min_ms()) ||
      value > static_cast<int32_t>(luabind::caret_blink_max_ms())) {
    return false;
  }
  delay = value;
  return true;
}

bool set_cursor_delay(int32_t delay) {
  if (!luabind::set_caret_blink_ms(delay)) return false;
  return remember_cursor();
}

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

// **The leap day and the first day of the week** (0.170.0): the same shape as the offset —the number in this file,
// the word in the store, and the calendar told every time so that the page and the call cannot disagree (docs/60).
bool leap_years() { return g_leap_years; }

bool set_leap_years(bool on) {
  g_leap_years = on;
  calendar::set_leap_years(on);
  return remember_leap();
}

bool dst() { return g_dst; }

bool set_dst(bool on) {
  g_dst = on;
  return remember_dst();
}

int16_t offset_minutes() {
  return static_cast<int16_t>(g_tz_minutes + (g_dst ? 60 : 0));
}

uint8_t week_start() { return g_week_start; }

bool set_week_start(uint8_t first) {
  // **The week of this system is Monday's or Sunday's and nothing else is written**: a week that begins on a Wednesday
  // is not a calendar a person asks for, and the store keeps the two words it has to be able to read back (docs/60).
  if (first != 1 && first != 7) return false;
  g_week_start = first;
  calendar::set_week_start(first);
  return remember_week();
}

const char* boot_splash() { return g_boot; }
// **The two words that are drawings of the logo**: the splash is the one that says which moments exist, so the
// question is asked there and not repeated here as a list that could drift (docs/30, docs/50).
bool is_a_boot_word(const char* word) {
  if (word == nullptr) return false;
  const splash::Kind kind = splash::kind_of(word);
  return kind == splash::kBoot;
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
