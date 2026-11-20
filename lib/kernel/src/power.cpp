// The energy policy: the board can stop its clocks, and this is the one place that
// says when, what is said to the app first and what is read afterwards (docs/90).
#include "board/identity.h"
#include "kernel/meter.h"
#include "kernel/panel.h"
#include "kernel/power.h"

#include "kernel/cfg.h"
#include "kernel/lock.h"
#include "kernel/splash.h"
#include "kernel/vfs.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/shell.h"
#include "kernel/led.h"
#include "kernel/log.h"
#include "kernel/session.h"
#include "alarms/alarms.h"
#include "hal/alarm.h"
#include "luabind/luabind.h"
#include "pal/pal.h"

namespace kernel {
namespace power {
namespace {

hal::IPower* g_power = nullptr;
hal::IClock* g_clock = nullptr;
hal::IKeyValue* g_nvs = nullptr;
uint32_t g_caps = 0;
bool g_keep_awake = false;

// A sleep that an app asked for, waiting for the next turn: what is running cannot
// be closed from inside its own call (docs/30).
bool g_pending = false;
bool g_pending_from_app = false;
hal::SleepMode g_pending_mode = hal::SleepMode::kLight;
int64_t g_pending_at = 0;

// Where a sleep leaves its note. One namespace and three short keys: the app in
// front, the file it was opened with, and the hour it was told to come back.
const char kNs[] = "session";
const char kAppKey[] = "app";
const char kPathKey[] = "path";
const char kWakeKey[] = "wake_at";
const size_t kIdCap = 32;
const size_t kPathCap = 128;

// Where the last wake leaves its number: how far it was from the hour it was given, and
// what brought it back. The console line is one boot earlier and gone, so the number that
// decides whether the wake has to move to the clock chip has to be readable afterwards
// (docs/50, docs/60, docs/90).
const char kPowerNs[] = "power";
const char kDriftKey[] = "drift";
const char kCauseKey[] = "cause";
// **La marca d'un son profund que aquesta arrencada encara no ha consumit** (0.189.176): the `state` of the pages has to survive
// the boot that comes back from a sleep and not the next one, and this is what tells the two apart. It is written on the way
// down and read —and erased— by `coming_back_from_a_sleep` (docs/60, docs/75).
const char kSleptKey[] = "slept";

// The idle policy. Two numbers, and both are a decision rather than a measurement:
//
//   - the console has to be **quiet** before the board puts itself down, or a person would
//     be typing at a board that keeps powering off between two commands;
//   - a wait shorter than **twenty seconds** is not worth a power cycle. It was ten when a boot was the
//     only cost, and the way down is not: it writes the session, waits the two seconds of the card and
//     paints the picture of the sleep —the board's own log measured 2.0 s and 3.6 s on the 19/11— and a
//     wake from a power off is a boot. Ten was a sleep the board spent going down and coming back.
const int64_t kIdleQuietMs = 10000;
const int64_t kMinIdleSleepMs = 20000;
// **I el número de la quietud és una constant a la placa i una variable a la suite** (0.189.182): the DRAM of a Paper is full to
// sixteen bytes and a case that had to wait the ten seconds would be a suite ten seconds slower, o sigui que el que es pot moure
// només existeix on hi ha algú que el mogui (docs/60, docs/80).
#if defined(UNIT_TEST) || defined(PIO_UNIT_TESTING)
int64_t g_idle_quiet_ms = kIdleQuietMs;
#else
constexpr int64_t g_idle_quiet_ms = kIdleQuietMs;
#endif

bool g_idle_allowed = true;
// **Qui ha tocat la placa des d'aquest boot, i si el vidre té una pàgina que no és la del son** (0.190.18). The first is what makes a
// wake for an alarm or a stage of a long wait go straight back down **and** what keeps it from going down under a finger; the second is
// the window that painted over the picture (docs/30, docs/60).
bool g_touched_since_boot = false;
bool g_a_page_on_the_glass = false;
int64_t g_last_activity_ms = 0;

// **La velocitat de repòs i la finestra d'una feina pesada** (0.179.38): the step the system goes back to and the hour a
// burst ends. `g_cpu_busy_until_ms` at zero is "no window is owed", which is the state of a system that is only working.
// The step a person asks for becomes the resting one, so `power cpu 80` is not undone by the next app that starts.
uint32_t g_cpu_rest_mhz = kRestingMhz;
int64_t g_cpu_busy_until_ms = 0;
// **La política i el sostre de les velocitats, en un sol byte** (0.189.94, 0.189.107): the automatic policy is the fourth
// bit and the three steps the person allows are the low three. **The two used to be a bool and a byte**, which is eight bytes
// of `.bss` with their padding in a build whose `dram0.bss` overflowed by eight the day the second one arrived (docs/75), and
// a bit is a bit: the policy is on and the three steps are allowed until somebody says otherwise, which is the state this
// byte starts in.
const uint8_t kAllowedMask = 0x07;
const uint8_t kAutoBit = 0x08;
uint8_t g_cpu_policy = static_cast<uint8_t>(kAllowedMask | kAutoBit);

bool policy_is_auto() { return (g_cpu_policy & kAutoBit) != 0; }

void set_policy(bool on) {
  g_cpu_policy = on ? static_cast<uint8_t>(g_cpu_policy | kAutoBit)
                    : static_cast<uint8_t>(g_cpu_policy & ~kAutoBit);
}

// **The steps the policy may use**, and a byte with none of them is read as the three: a governor with no step is not a
// government (docs/50).
uint8_t allowed_bits() {
  const uint8_t mask = static_cast<uint8_t>(g_cpu_policy & kAllowedMask);
  return (mask != 0) ? mask : kAllowedMask;
}
// **Un minut de mostres i la meitat contra l'altra** (0.189.95): a sample every two seconds —thirty of them, thirty bytes—
// and the comparison is the mean of the last fifteen against the mean of the fifteen before.
const uint8_t kTrendSamples = 30;
const uint32_t kTrendEveryMs = 2000;
// **Deu punts és una tendència**: a mean that moved ten points between the two halves of the minute is the board doing
// something else; less than that is the noise of a page that scrolls (docs/95).
const uint32_t kTrendBandPct10 = 100;
// **Els dos nivells d'una tendència plana** (0.189.95): under ten per cent of a half minute the board is idle and the bottom
// step is enough; over fifty it is working and it may as well have the top one.
const uint8_t kIdleMeanPct = 10;
const uint8_t kBusyMeanPct = 50;

// **Les mostres de la tendència** (0.189.95): thirty per cent of load, one every two seconds, which is a minute. They are
// here and not beside the governor because the switch that turns the policy on is the one that throws them away.
uint8_t g_trend[kTrendSamples];
uint8_t g_trend_count = 0;
uint32_t g_trend_ms = 0;
// The hour the board was woken early for. The clock reaches a day at most, so a longer wait is
// armed in stages, and a stage is **not** the wake: the app must not come back until its hour
// (docs/60).
int64_t g_coming_back_at = 0;

// **La tornada que era per a una alarma** (0.189.182): set by the boot and read by the two places that would otherwise paint the
// glass or keep the board awake, i per `sleep` quan torna a baixar (docs/60).
bool g_for_an_alarm = false;

// **Què ha despertat la placa, llegit una vegada** (0.189.182): the chip's answer belongs to the boot that reads it —and it is read
// in `note_the_boot`, which is the first thing a boot does and the only one that has the platform before anything paints—,
// perquè `power::boot` arriba després i la pregunta és de l'arrencada i no de la porta (docs/50, docs/60).
bool g_woke_by_an_alarm = false;

// **Si el darrer son ha estat una arrencada del xip** (0.190.68): el posa `sleep` quan la placa diu que la crida de dalt no ha tornat
// —el model de l'emulador ho sap i una placa de debò no arriba mai aquí— i el neteja `kernel::boot`, que és on comença el run nou.
// És al costat de les altres coses d'aquest fitxer que són d'una arrencada i no d'un torn.
bool g_the_chip_restarted = false;

int64_t mono_now() {
  // The RTC chip, read once a turn (docs/30, docs/50).
  meter::Scope scope(meter::kClockMono);
  return (g_clock != nullptr) ? g_clock->monotonic_ms() : 0;
}

// **Every line this file says to a person goes through the log** (0.179.55): the serial port is a copy of the ring, so a
// line written straight to the port is a line the `log` command never shows —and these are the lines a person wants
// afterwards, the wake the clock made and the sleep that was cut short—. `log_say` keeps the words and puts them in the
// ring; `printf` is still not used, because it is buffered and its lines come out in another order than the shell's
// (measured on the PC, 31/10, docs/30, docs/50).
void say(const char* text) { log_say(text); }

void sayf(const char* format, ...) {
  char line[128];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  say(line);
}

void write_in(const char* ns, const char* key, const char* text) {
  if (g_nvs == nullptr) return;
  // **One row for every write to the store**, erasing included: an erase is a write to the flash like
  // any other and it is the one that wears it (docs/70, docs/75).
  meter::Scope scope(meter::kKvSet);
  if (text == nullptr || text[0] == '\0') {
    g_nvs->erase(ns, key);
    return;
  }
  if (!g_nvs->set(ns, key, text, strlen(text) + 1)) {
    log_printf(pal::kLogWarn, "POWER", "the nvs did not take '%s'", key);
  }
}

bool read_in(const char* ns, const char* key, char* out, size_t cap) {
  if (g_nvs == nullptr || out == nullptr || cap == 0) return false;
  size_t len = 0;
  if (!g_nvs->get(ns, key, out, cap - 1, len) || len == 0) return false;
  out[(len < cap) ? len : cap - 1] = '\0';
  return out[0] != '\0';
}

// The number of the last sleep, kept where a person can ask for it later. It is written
// every time it is measured, so what `power last` shows is the last one and not the best
// one (docs/60).
void keep_measurement(int64_t drift_ms, const char* cause) {
  char number[24];
  snprintf(number, sizeof(number), "%+lld", static_cast<long long>(drift_ms));
  write_in(kPowerNs, kDriftKey, number);
  write_in(kPowerNs, kCauseKey, cause);
}

void write_text(const char* key, const char* text) { write_in(kNs, key, text); }

bool read_text(const char* key, char* out, size_t cap) { return read_in(kNs, key, out, cap); }

// What a deep sleep has to write down, because the RAM does not survive it: the
// app in front and the file it was opened with (docs/90).
void save_session() {
  const char* id = shell::app_id();
  if (id == nullptr || id[0] == '\0') {
    write_text(kAppKey, nullptr);
    write_text(kPathKey, nullptr);
    return;
  }
  write_text(kAppKey, id);
  write_text(kPathKey, luabind::app_path());
}

// The hour this sleep was told to come back at, written where a board that
// restarted can still read it.
void save_deadline(int64_t at_epoch_ms) {
  char text[24];
  if (at_epoch_ms <= 0) {
    write_text(kWakeKey, nullptr);
    return;
  }
  snprintf(text, sizeof(text), "%lld", static_cast<long long>(at_epoch_ms));
  write_text(kWakeKey, text);
}

// The number M6 asks for: whether the board came back when it said it would. It is
// printed because it is measured and not estimated (docs/90). The note of the hour
// is the only thing that can say it, because a light sleep does not restart the
// clock and the wake cause of the chip is only read after a real sleep.
//
// `restarted` says the board came back from a boot and not from a light sleep, and it is what makes
// the number about the sleep and not about the firmware: after a restart the clock is read once the
// bootloader, the banner, the flash, the card and the settings have had their second, and counting
// that second as the clock's error is measuring the wrong thing (measured on the Paper, 31/10: a
// 20 s sleep came out 6 % off until the boot was taken out; docs/60).
void report_wake(bool restarted) {
  char text[24];
  const bool armed = read_text(kWakeKey, text, sizeof(text));
  const int64_t at = armed ? strtoll(text, nullptr, 10) : 0;
  write_text(kWakeKey, nullptr);
  if (g_clock == nullptr) return;

  // What brought the board back. Which of the two clocks armed the wake decides the name: the
  // chip's own timer on a board that kept the power on, or the clock chip's quartz on a board that
  // was off (docs/50, docs/60).
  const hal::WakeCause woke = g_clock->last_wake();
  const bool by_clock = (woke == hal::WakeCause::kClock);
  const char* how = "the power coming back";
  const char* cause = "power";
  switch (woke) {
    case hal::WakeCause::kTimer:
      how = "the timer";
      cause = "timer";
      break;
    case hal::WakeCause::kConsole:
      how = "the console";
      cause = "console";
      break;
    case hal::WakeCause::kOther:
      how = "a button";
      cause = "button";
      break;
    case hal::WakeCause::kTouch:
      how = "the glass";
      cause = "touch";
      break;
    case hal::WakeCause::kClock:
      how = "the clock";
      cause = "clock";
      break;
    default:
      break;
  }

  const int64_t epoch = g_clock->epoch_ms();
  // The boot is only taken out of a wake that a sleep caused: a plain boot has no hour to compare
  // and the numbers there are the "cut short" ones, which say how early and not how precise.
  const bool slept = restarted && (woke != hal::WakeCause::kPowerOn);
  const int64_t uptime = slept ? g_clock->monotonic_ms() : 0;
  const int64_t error = hal::wake_error_ms(epoch, uptime, at, slept);
  if (at > 0 && epoch >= at) {
    keep_measurement(error, cause);
    sayf("power: woke on the %s, %+lld ms from the hour it was given\r\n", cause,
         static_cast<long long>(error));
    // Picked up by the log ring as well, so `log` shows it after the console has
    // scrolled.
    log_printf(pal::kLogInfo, "POWER", "woke %+lld ms from the hour it was given",
               static_cast<long long>(error));
    return;
  }

  // No note and nothing to check: a plain boot says nothing, because the banner
  // already said the board is up.
  if (at <= 0) return;
  // A deadline further away than the clock can express is armed in stages: the chip fires on
  // the first match of its hour and minute, the board finds nothing due, and it arms the rest.
  // That is the clock doing its job and it is not a drift, so it is not measured —but the hour
  // has to be put back where the next stage will look for it, and the app must stay closed, or
  // it would come back a day early and never know (docs/60).
  if (by_clock) {
    save_deadline(at);
    g_coming_back_at = at;
    sayf("power: the clock woke the board early to arm again, %+lld ms to go\r\n",
         static_cast<long long>(epoch - at));
    return;
  }
  // **Una persona que torna no és un son tallat** (0.190.67, el registre de l'emulador del 20/11: *«power: the sleep was cut short by
  // the glass, 69347 ms before the hour it was given»*). A key or a finger on the glass is the board doing exactly what it was told
  // to do —the door a person has—, and calling that a sleep that was cut short is a sentence about a fault for the one case that is
  // not one. **No es mesura res**: the clock had nothing to prove, because the board was not waiting for its hour.
  if (woke == hal::WakeCause::kOther || woke == hal::WakeCause::kTouch) {
    log_printf(pal::kLogInfo, "POWER", "%s woke the board %lld ms before its hour, and nothing is measured",
               how, static_cast<long long>(at - epoch));
    sayf("power: %s woke the board, %lld ms before the hour it was given\r\n", how,
         static_cast<long long>(at - epoch));
    return;
  }
  // Everything else that comes back before its hour **did not run its sleep to the end**: a cable
  // plugged in, a supply that blinked. The number is how early it was cut short, and calling it a
  // drift was a lie about the clock —the board never got to prove anything— so it is said as what it
  // is and the last measurement keeps the word `short` (docs/60).
  const int64_t early = at - epoch;
  char marked[24];
  snprintf(marked, sizeof(marked), "%s, cut short", cause);
  keep_measurement(-early, marked);
  sayf("power: the sleep was cut short by %s, %lld ms before the hour it was given\r\n", how,
       static_cast<long long>(early));
  log_printf(pal::kLogInfo, "POWER", "sleep cut short by %s, %lld ms before its hour", cause,
             static_cast<long long>(early));
}

// The interpreter keeps knowing nothing about the HAL, so the mode travels as a
// number and is turned back into the board's word here (docs/30).
hal::SleepMode mode_of(uint8_t mode) {
  return (mode == 1) ? hal::SleepMode::kDeep : hal::SleepMode::kLight;
}

// **La línia del port s'buida abans de moure el processador** (0.179.43): the serial rate is derived from the clock of the
// bus, and the chip re-derives it when the frequency changes. The bytes that are already in the FIFO and in the shift
// register of the UART go out at the rate of before, and the port shows them as garbage — the owner read it in a log of
// the board: `[D][PANEL] paint 60,356 72x76 … (page 108lx␘␂����2, 76 rows of 36 B)`, with `[I][POWER] cpu 160 MHz from
// 240` on the very next line. It is the same reason the sleep of this file empties the line before the clocks stop, said
// for a step instead of a stop (docs/50, docs/60, docs/92).
void empty_the_line() {
  pal::IConsole* console = pal::system().console();
  if (console != nullptr) console->flush();
}

// **Cada canvi de velocitat es diu a la consola i al registre** (0.179.38): a step of the processor is energy and time,
// and a board whose speed changed without a line is a board nobody can measure. The line says what was asked, what the
// board answered and why, so a step the chip does not have reads as itself and not as a silence. **Every call is a
// line**, even the one that asks for the step it is already on: the boot at two hundred and forty is a fact of the
// policy and not of the chip's default, and a log that hides it says the system booted at the resting step.
//
// **I el pas que es demana s'acota a les velocitats permeses** (0.189.107): `hold_cpu` and `rest_cpu` ask for the three
// constants of this file and the person may have forbidden one of them, so the clamp is here and not in the callers
// (docs/50, docs/60).
void move_to(uint32_t mhz, const char* why) {
  mhz = allowed_step(mhz);
  if (g_power == nullptr) return;
  const uint32_t before = g_power->cpu_mhz();
  // **Un pas que ja és el que hi ha no es mou i no es diu** (0.189.95): `hold_cpu` is asked for the top step by every paint and
  // every app that starts, and half of those asks find the board already there —a line of the log that says `cpu 240 MHz from
  // 240` is a line that says nothing and a call to the board for nothing (docs/50).
  if (before == mhz) return;
  empty_the_line();
  if (!g_power->set_cpu_mhz(mhz)) {
    log_printf(pal::kLogWarn, "POWER", "the board did not take %u MHz (%s), it runs at %u", static_cast<unsigned>(mhz),
               (why != nullptr) ? why : "?", static_cast<unsigned>(before));
    return;
  }
  log_printf(pal::kLogInfo, "POWER", "cpu %u MHz from %u (%s)", static_cast<unsigned>(g_power->cpu_mhz()),
             static_cast<unsigned>(before), (why != nullptr) ? why : "?");
}

// **Les tres velocitats que la política pot fer servir** (0.189.107): the mask is the person's ceiling and `allowed_step` is
// the one arithmetic that decides which step a request lands on. It is here, just before `move_to`, because that is the
// function that calls it for every move of this module (docs/50, docs/60).
bool can_sleep_op(uint8_t mode) { return can_sleep(mode_of(mode)); }

bool sleep_op(uint8_t mode, int64_t at_epoch_ms) { return request_sleep(mode_of(mode), at_epoch_ms); }

}  // namespace

// **Les tres velocitats i el pas on cau una demanda** (0.189.107): the mask is the person's ceiling and `allowed_step` is the
// one arithmetic that decides which step a request lands on —the smallest allowed step that is not under what was asked,
// or the biggest allowed one when nothing is above it— (docs/50, docs/60).
void set_allowed_steps(uint8_t mask) {
  g_cpu_policy = static_cast<uint8_t>((g_cpu_policy & ~kAllowedMask) | ((mask == 0) ? kAllowedMask : (mask & kAllowedMask)));
  // **I el que ja estava demanat s'hi acota ara mateix**: a ceiling that only applies to the next move would leave the board
  // at a step the person has just forbidden, which is the one thing this setting is for (docs/50).
  if (g_power == nullptr) return;
  const uint32_t now = g_power->cpu_mhz();
  const uint32_t step = allowed_step(now);
  if (step != now) move_to(step, "the steps the person allows");
}

uint8_t allowed_steps() { return allowed_bits(); }

uint32_t allowed_step(uint32_t want) {
  const uint8_t mask = allowed_bits();
  uint32_t above = 0;
  uint32_t biggest = 0;
  for (uint8_t bit = 0x01; bit <= 0x04; bit = static_cast<uint8_t>(bit << 1)) {
    if ((mask & bit) == 0) continue;
    const uint32_t step = (bit == 0x01) ? kIdleMhz : ((bit == 0x02) ? kRestingMhz : kBusyMhz);
    if (step > biggest) biggest = step;
    if (step >= want && above == 0) above = step;
  }
  if (above != 0) return above;
  return (biggest != 0) ? biggest : want;
}

void boot(hal::IPlatform& platform, bool for_an_alarm) {
  g_power = platform.power();
  g_clock = platform.clock();
  g_nvs = platform.nvs();
  g_caps = platform.caps().bits;
  g_keep_awake = false;
  // **La política és una ajust i no una llei** (0.190.5): a person who turned the sleep off in the System tab gets a board that stays awake,
  // and the answer is read here so that a boot does not undo it (docs/30, docs/60).
  g_idle_allowed = cfg::sleep_on();
  // **Un boot torna a la seva política** (0.179.38): the resting step is the system's again and no window of the boot
  // before is owed, or a board that reboots would carry the two hundred and forty of the last app it ran.
  g_cpu_rest_mhz = kRestingMhz;
  g_cpu_busy_until_ms = 0;
  // **El comptador de la quietud arrenca amb aquesta arrencada** (0.190.9): nothing has been typed at it yet, and the person
  // who chose five minutes gets five minutes from the boot and not from the first turn that happened to look. **I una arrencada no és
  // una mà** (0.190.18): the flag is what tells «nobody has touched it since it came back» —the wake for an alarm and the stage of a long
  // wait go straight down because of it— from «somebody is using it», which is the counter (docs/60).
  g_last_activity_ms = mono_now();
  // A stage of a longer wait belongs to one boot: `report_wake()` is what sets it again
  // (docs/60).
  g_coming_back_at = 0;
  g_for_an_alarm = for_an_alarm;
  // **I la porta del dit, que és un ajust i no una política** (0.190.16): the setting of the Battery tab is pushed into the board at every
  // boot, because the board is built before the settings are read (docs/30, docs/50).
  if (g_power != nullptr) g_power->set_wake_on_touch(cfg::wake_on_touch());
  // **I la becaina, que és de la política i no del maquinari** (0.190.63): the switch is read here so that the board's answer
  // (`can_nap`) and the person's wish are both in force from the first turn (docs/30, docs/60).
  // **I es diu al registre, que és l'únic que ho pot dir** (0.190.64, l'amo: *«al log es veu quan hi ha light sleep?»*): una placa
  // que dorm a trossos no ho ensenya enlloc més, i el commutador encès en una placa que no en fa és exactament el que una persona
  // ha de poder veure d'una línia (docs/60, docs/95).
  set_nap_on(cfg::nap_on());
  if (!nap_on()) {
    log_printf(pal::kLogInfo, "POWER", "light sleep instead of wait is off: every wait of the board is spent awake");
  } else if (can_nap()) {
    log_printf(pal::kLogInfo, "POWER",
               "light sleep instead of wait: the waits are spent asleep, and a key, the glass or the console wakes the chip");
  } else {
    log_printf(pal::kLogInfo, "POWER",
               "light sleep instead of wait is on, and this board does not take one: the waits are spent awake");
  }

  // What an app may ask for. Without a power interface the operations stay
  // absent, and power.* answers E_UNSUPPORTED instead of pretending (docs/30).
  luabind::PowerOps ops = {};
  if (g_power != nullptr) {
    ops.can_sleep = &can_sleep_op;
    ops.sleep = &sleep_op;
    ops.battery = &battery;
    ops.cpu_mhz = &cpu_mhz;
    ops.set_cpu_mhz = &set_cpu_mhz;
    ops.cpu_auto = &cpu_auto;
    ops.set_cpu_auto = &set_cpu_auto;
    ops.keep_awake = &keep_awake;
    ops.keeping_awake = &keeping_awake;
    ops.wakes = &wakes;
    // **El son de la lluna, per a una app** (0.189.25): `sys.deepsleep()` is the same sequence the prompt's `deepsleep` and the
    // key of the card run —the app says goodbye, the sessions are written and the picture of the sleep goes on the glass— so
    // the three ways down of this system are one and not three (docs/30, docs/50).
    ops.sleep_with_the_keys = &shell::deep_sleep;
  }
  // **I el comptador de la quietud, que és d'aquesta casa i no de cada pàgina** (0.190.9).
  ops.idle_ms = &quiet_ms;
  luabind::set_power_ops(ops);

  if (g_power == nullptr) return;
  report_wake(true);  // this is a boot: the board came back from a restart or from nothing at all
  // A stage of a longer wait is not the wake: the app stays closed until its hour. If the wait
  // left is down to a few seconds, that is close enough to be the wake and the app comes back
  // (docs/60).
  if (g_coming_back_at > 0 && g_clock != nullptr &&
      g_coming_back_at - g_clock->epoch_ms() < kMinIdleSleepMs) {
    g_coming_back_at = 0;
  }
  // **I la pàgina de la persona espera** (0.189.182): a board that came back for an alarm has nobody in front of it, o sigui que el
  // registre de sessió no es consumeix i l'app que hi havia torna quan hi hagi algú —el despert d'una tecla, la propera encesa—
  // (docs/60, docs/90).
  if (g_coming_back_at <= 0 && !g_for_an_alarm) restore_session();
}

bool available() { return g_power != nullptr; }

bool can_sleep(hal::SleepMode mode) {
  if (g_power == nullptr) return false;
  const uint32_t bit = (mode == hal::SleepMode::kDeep) ? hal::kCapSleepDeep : hal::kCapSleepLight;
  return (g_caps & static_cast<uint32_t>(bit)) != 0;
}

const char* wakes() { return (g_power != nullptr) ? g_power->wakes() : "none"; }

bool last_wake_report(char* out, size_t cap) {
  if (out == nullptr || cap == 0) return false;
  char drift[24] = {0};
  // The cause is a word and, since a sleep can be cut short, a marker after it (`timer, cut short`):
  // sixteen bytes was enough for the word alone and cut the marker in half (docs/60).
  char cause[24] = {0};
  if (!read_in(kPowerNs, kDriftKey, drift, sizeof(drift))) return false;
  if (!read_in(kPowerNs, kCauseKey, cause, sizeof(cause))) cause[0] = '\0';
  const int written = snprintf(out, cap, "%s ms (%s)", drift, (cause[0] != '\0') ? cause : "?");
  return written > 0 && static_cast<size_t>(written) < cap;
}

// **A quina hora torna la placa, i és una sola pregunta per a tots els sons** (0.190.15, l'amo: *«en tots els casos que fas deepsleep ha
// de ser la mateixa crida en ambdós punts i usar un codi comú per la preparació»*). The hour a sleep is told is **the earlier of the one
// that was asked for and the nearest alarm** —an alarm belongs to the system and must not be made to wait for a page that is asleep, and
// a person who asks the board to sleep with an alarm armed wants to be woken by that alarm—, and with **nothing asked** —zero, which is
// what the card of the system, the `deepsleep` of the prompt and the idle policy with an empty queue ask for— the nearest alarm **is**
// the hour. Zero is not an hour: it is the key (docs/50, docs/60).
//
// The hour that is written down is the one the board was really told, or `power last` would measure a difference nobody asked for.
int64_t the_hour_of_a_sleep(int64_t at_epoch_ms) {
  int64_t alarm_epoch = 0;
  if (g_clock != nullptr) {
    const int64_t next = alarms::next_due_mono_ms();
    if (next >= 0) {
      const int64_t epoch = g_clock->epoch_ms();
      if (epoch > 0) alarm_epoch = epoch + (next - g_clock->monotonic_ms());
    }
  }
  if (at_epoch_ms <= 0) return (alarm_epoch > 0) ? alarm_epoch : 0;
  if (alarm_epoch > 0 && alarm_epoch < at_epoch_ms) return alarm_epoch;
  return at_epoch_ms;
}

bool sleep(hal::SleepMode mode, int64_t at_epoch_ms) {
  // **A sleep is a call that does not come back until the board wakes**, so it is measured whole
  // (docs/30, docs/50).
  meter::Scope scope(meter::kSleep);
  if (g_power == nullptr) return false;
  // **I un run que ja ha reiniciat el xip no torna a baixar** (0.190.68): el flag vol dir que aquest run s'ha acabat i que el que ve
  // després és un `kernel::boot`; una segona crida —la política d'oci mirant el torn des de dins, el pany demanant la seva hora— no ha
  // de dormir una placa que ja no hi és, perquè el que faria és encadenar sons abans que el nucli torni a arrencar (docs/60, docs/85).
  if (g_the_chip_restarted) return false;
  if (!can_sleep(mode)) return false;
  // **L'hora de la baixada és la que la placa ha de saber**, i es decideix aquí i enlloc més: la més propera entre la que s'ha demanat i la
  // propera alarma (0.190.15, vegeu `the_hour_of_a_sleep`).
  const int64_t until = the_hour_of_a_sleep(at_epoch_ms);
  // **L'hora que ja ha passat no es dorm** (19/11): the margin, the picture and the glass's line are spent between the
  // policy's look and this line, i una hora que era futura pot ser passada quan el xip la rep. A sleep to a gone hour is a
  // timer of one millisecond with the cable in —a boot for nothing— and a board with no door but the key on the battery; the
  // alarm is delivered awake on the next turn instead. The close ones are the policy's to refuse (`idle_sleep`); an app that
  // asks for a short sleep is answered here and not argued with (docs/50, docs/60).
  if (mode == hal::SleepMode::kDeep && until > 0 && g_clock != nullptr) {
    const int64_t now = g_clock->epoch_ms();
    if (now > 0 && until <= now) {
      log_printf(pal::kLogInfo, "POWER", "the hour is already past: the board stays awake");
      return false;
    }
  }
  // **I una son fonda sense cap porta sí que es refusa**: sense hora i sense una tecla que desperti aquesta placa, el que baixa no torna.
  // És una decisió i no un accident, i es diu aquí perquè el qui la demana pugui dir quina volia (docs/50, docs/90).
  if (mode == hal::SleepMode::kDeep && until <= 0 && !g_power->can_sleep_with_the_keys()) return false;

  if (mode == hal::SleepMode::kDeep) {
    // What was in front is written down **before** it is closed: closing stops the
    // interpreter and forgets which app it was (docs/90).
    // **I una placa que torna a dormir perquè ha vingut per una alarma no té res a escriure**: no hi ha cap app al davant i el
    // registre que hi ha és el de la persona que espera, o sigui que s'hi queda (0.189.182, docs/60).
    const bool keep_the_record = g_for_an_alarm && !shell::app_running();
    if (!keep_the_record) save_session();
    if (shell::app_running()) shell::close_app();
    // **I la marca del son** (0.189.176): the hour is written after this one and it is not there at all when the board comes
    // back on a key, o sigui que el que diu que aquesta arrencada és una tornada és la marca i no l'hora (docs/60, docs/75).
    write_in(kPowerNs, kSleptKey, "1");
  }
  save_deadline(until);

  // **I el disc, amb el que es pugui rentar i el repòs de la targeta** (0.189.18, i una sola vegada per a totes les baixades des de la
  // 0.190.15): a chip stopping is not the supply going, but an SPI that stops in the middle of a sector is a card that has to be told so
  // —the same step for the way off, for the card of the system and for the policy— (docs/70, docs/75).
  if (mode == hal::SleepMode::kDeep) {
    const char* why = (until > 0) ? "the board sleeps until its hour" : "the board sleeps with its keys awake";
    // **I la baixada és esperar**: the sessions go to the card and the card takes its two seconds, i el processador no hi té res
    // a fer-hi (0.190.26, docs/50, docs/60, docs/70).
    wait_cpu("the card of the way down");
    session::park_all(why);
    const session::Checked parked = session::check_parked();
    log_printf(parked.ok == parked.files ? pal::kLogInfo : pal::kLogWarn, "MEMORY",
               "%u of %u session file(s) are on the disk", static_cast<unsigned>(parked.ok),
               static_cast<unsigned>(parked.files));
    settle_before_leaving(why);
  }

  // The line is emptied before the board stops: the UART keeps bytes in flight after a
  // write returns, and both sleeps stop its clock, so the last thing a person reads would
  // be a word cut in half. It is done here and not in each board's sleep because the
  // console belongs to the system and not to the chip (docs/50, docs/60).
  //
  // **I es diu abans, perquè aquesta és la línia on la placa es va quedar el 20/11** (0.190.69): a board that hangs on its way down
  // leaves a log that ends in the middle, and that night the last line was the margin —so the stop was in the console, in the picture
  // or in the chip, and nobody could tell which—. Dues línies de depuració són el que converteix «s'ha penjat» en «s'ha penjat aquí»
  // (docs/50, docs/60).
  if (mode == hal::SleepMode::kDeep) log_printf(pal::kLogDebug, "POWER", "the way down: the console first");
  pal::IConsole* console = pal::system().console();
  if (console != nullptr) console->flush();

  // **The picture of the way down** (0.111.0): a board that is going to stop its clocks says so on the glass
  // first —after the app is closed and before the chip stops— so a person finds the reason it went dark instead of
  // the last page of an app that is no longer there. A light sleep is a nap and paints nothing: the board keeps what
  // it has and is back in a moment (docs/30, docs/50, docs/92).
  // **I el motiu és el que diu la imatge** (0.183.0): a deep sleep is `sleep`, and a board whose battery is under the mark
  // says `lowbatt` —the reason it is going down is the one a person has to know—, which is one function and not a branch
  // here (docs/50).
  // **I la imatge del son ja hi és**: a board that comes back for an alarm and goes down again does not paint it twice (0.189.182).
  // **I el vidre ha de dir el son, no el que hi hagi pintat algú** (0.190.18): a wake for an alarm keeps the picture it found, pero una
  // finestra que ha corregut hi ha deixat la pàgina de l'app —i l'app ja no hi és quan la placa baixa—, o sigui que la imatge es torna a
  // pintar quan el vidre no és el que havia deixat el son (docs/30, docs/60).
  // **I una app que puja no arriba aquí** (0.190.23, revisat): `for_an_alarm` exigeix `!shell::app_running()` a l'arrencada, o sigui que
  // un despert que aixeca una app —el `raise` del Clock— **no és un despert per a una alarma** per a aquesta política: baixa pel camí de
  // sempre, amb la imatge pintada i el rail tallat. És el que el cas del `raise` mira, i per això no cal una segona porta aquí (docs/60).
  if (mode == hal::SleepMode::kDeep && (!g_for_an_alarm || g_a_page_on_the_glass)) {
    splash::show(going_down_picture(/*deep=*/true));
    g_a_page_on_the_glass = false;
  }
  // **And the ink of the picture is over before the chip stops** (0.165.1): the deep sleep cuts the clocks where the call
  // found them, so a picture asked for and not waited on is a picture that nobody sees (docs/50).
  if (mode == hal::SleepMode::kDeep) splash::wait();
  if (mode == hal::SleepMode::kDeep) log_printf(pal::kLogDebug, "POWER", "the way down: the picture is on the glass, and now the chip");
  // **Una baixada que no ha tocat el vidre no baixa els rails del panell** (0.190.19): a wake for an alarm runs with nobody in front and
  // the glass keeps the picture it had, o sigui que el controlador ha de quedar-se amb el seu estat i la imatge que ensenya (docs/50).
  const bool back = g_power->sleep(mode, until, g_for_an_alarm);
  if (!back) {
    // **Una baixada que la placa refusa no ha deixat res fet** (0.190.23): l'única porta que pot dir que no quan ja s'ha preparat tot és
    // l'hora —she went past while the session was being written and the picture painted—, i llavors aquest run **no s'ha adormit**. La
    // marca del son i l'hora desada son dues coses que la propera arrencada llegiria com una tornada que no ha existit, així que
    // s'esborren aquí i el que queda és la veritat (docs/60, docs/75).
    if (mode == hal::SleepMode::kDeep) {
      write_in(kPowerNs, kSleptKey, nullptr);
      save_deadline(0);
    }
    return false;
  }
  // **I un son que ha estat una arrencada no té camí de tornada** (0.190.68): a la placa el que ve després és un `kernel::boot` i no
  // aquesta línia, o sigui que aquí no es mesura res, no es diu cap despert i no es toca el pany —el run que hi havia s'ha acabat i el
  // que el substitueix és un de nou, amb el seu propi `power::boot` (docs/60, docs/85).
  if (mode == hal::SleepMode::kDeep && g_power->take_the_restart()) {
    g_the_chip_restarted = true;
    return true;
  }
  // **Sense hora no hi ha res a mesurar** (0.190.15): a board that came back on a key was not early and not late, and a drift written
  // for it would be a number `power last` shows and nobody asked for (docs/60).
  if (mode == hal::SleepMode::kDeep && until <= 0) return true;
  // A board that answers slept lightly (or is the emulator): what it was told is
  // checked against what happened, and the note is not kept for a restart that is
  // not coming.
  //
  // The hour is taken back from wherever the board keeps it properly **before** it is
  // measured: a sleep is exactly when the chip's own counter drifts, and a board with a clock
  // chip has a better hour than its counter does (docs/60).
  if (g_clock != nullptr) g_clock->resync();
  report_wake(false);
  // **I el despertar tapa la pantalla si la persona ho ha demanat** (0.189.150): a flag and not a call, perquè som a dins
  // d'un torn i obrir una app tanca la que hi ha, que és una cosa que el nucli fa entre torns i no des de dins de la crida
  // d'algú (docs/30, docs/60).
  lock::note_a_wake();
  return true;
}

// **El son que deixa la placa encesa i una tecla per tornar** (0.189.25): it is a deep sleep in everything the system has to
// do —the app says goodbye, is written down and comes back at the next boot, because a deep sleep is a reset and not a
// resume— and the one thing that changes is the board: it keeps its supply instead of cutting it, so a key can wake it. The
// hour is none, and that is why it is not `sleep(kDeep, 0)`, which the system refuses on purpose (docs/50, docs/90).
bool can_deep_sleep_with_the_keys() {
  return g_power != nullptr && g_power->can_sleep_with_the_keys();
}

// **La baixada d'una persona és exactament la de la política** (0.190.15, l'amo: *«el codi d'apagada per deepsleep ha de ser el mateix a
// tot arreu… En tots els casos que fas deepsleep ha de ser la mateixa crida en ambdós punts i usar un codi comú per la preparació»*). Abans
// d'això hi havia **dues** baixades: aquesta, amb la seva pròpia preparació (la sessió, la imatge, el disc i el repòs de la targeta) i una
// crida al xip que **desarmava el rellotge** —o sigui que una son demanada per una persona s'enduia les alarmes—, i la de `sleep(kDeep,
// at)`, que no pintava la imatge ni feia reposar el disc. Ara n'hi ha una: `sleep(kDeep, 0)`, amb la mateixa preparació i la mateixa
// crida, i `the_hour_of_a_sleep` hi posa **la propera alarma** quan n'hi ha una. Una persona que demana a la placa que s'adormi amb una
// alarma armada té les dues portes: l'hora i la tecla (docs/50, docs/60).
bool deep_sleep_with_the_keys() {
  if (g_power == nullptr) return false;
  return sleep(hal::SleepMode::kDeep, 0);
}

// **Si aquesta placa pot baixar del tot** (0.190.15): a deep sleep is possible when the board can keep a clock (or has a battery-backed
// chip) **and** it has a door back —the hour of an alarm or a key that wakes the chip—. A board with neither is a board that goes dark
// for ever, and the card of the system and the prompt ask this before spending the picture of the way down (docs/50, docs/60).
// **El dit** (0.190.16): the board is the one that knows whether it can hear a finger while its chip sleeps, o sigui que la resposta és
// la seva i el sistema només la hi demana (docs/50). **I el commutador el guarda tota placa** (0.190.17): una que no hi sent l'accepta i
// no en fa res, que és el que una persona demana quan el toca en una placa que no té el maquinari (docs/30).
bool wake_on_touch() { return (g_power != nullptr) ? g_power->wake_on_touch() : false; }

bool set_wake_on_touch(bool on) { return (g_power != nullptr) ? g_power->set_wake_on_touch(on) : false; }

bool can_wake_on_touch() { return (g_power != nullptr) && g_power->can_wake_on_touch(); }

// ---------------------------------------------------------------- la becaina

// **Quaranta mil·lisegons i doblar mentre no passi res, fins a un segon** (0.190.63): the first nap is the shortest that pays the entry
// and the exit of a light sleep, and the ladder is what makes an idle board cost hundreds of times less without making a busy one pay
// for every turn: a hand on the glass, a key or a byte at the prompt puts it back to the bottom, o sigui que la resposta a una persona
// és la de sempre i el que s'estalvia és l'estona en què no hi havia ningú (docs/60, docs/95).
const int32_t kNapFirstMs = 40;
const int32_t kNapMostMs = 1000;
// El que s'ha de poder dormir perquè valgui la pena entrar-hi: per sota, el camí de sortida costa més que l'espera.
const int32_t kNapLeastMs = 20;

bool g_nap_on = true;     // encès de sèrie, que és el que demana el commutador
int32_t g_nap_ms = kNapFirstMs;
// **Quantes becaines seguides i quants mil·lisegons s'hi ha estat** (0.190.64, l'amo: *«al log es veu quan hi ha light sleep?»*):
// el registre és l'únic instrument que hi ha per saber si la placa dorm de debò, i una línia per becaina ompliria el registre
// del sistema a raó d'una per segon. El que es compta és la tanda, i s'escriu **quan s'acaba** —quan una mà, una tecla o la
// consola tornen el xip— i quan l'escala arriba al sostre (docs/60, docs/75, docs/95).
uint32_t g_nap_run = 0;
int64_t g_nap_run_ms = 0;
const int32_t kNapRunSayMs = 1000;  // una tanda d'un segon ja diu alguna cosa; per sota, no val una línia

bool nap_on() { return g_nap_on; }

bool set_nap_on(bool on) {
  g_nap_on = on;
  // El que dorm no es recorda entre dues vegades: una placa a la qual se li encén el commutador comença per la becaina curta.
  g_nap_ms = kNapFirstMs;
  return true;
}

bool can_nap() { return (g_power != nullptr) && g_power->can_nap(); }

int32_t nap_ms() { return g_nap_ms; }

bool nap(int32_t wait_ms) {
  if (!g_nap_on || g_power == nullptr || wait_ms < kNapLeastMs) return false;
  // **La becaina no pot durar més del que qui espera ha demanat** i no creix més enllà del sostre: el temporitzador del xip és el que
  // ha de tornar la placa, i una espera curta no s'ha de convertir en una de llarga (docs/50, docs/60).
  int32_t asked = (wait_ms < g_nap_ms) ? wait_ms : g_nap_ms;
  if (asked > kNapMostMs) asked = kNapMostMs;
  if (!g_power->nap(asked)) return false;
  ++g_nap_run;
  g_nap_run_ms += asked;
  return true;
}

// **La tanda que s'acaba es diu** (0.190.64): quan una mà, una tecla o la consola tornen el xip, el registre escriu quantes
// becaines seguides hi ha hagut i quants mil·lisegons ha estat adormida la placa. És el número que una persona mira a la placa
// per saber si el commutador fa alguna cosa, i per sota d'un segon no val una línia (docs/60, docs/95).
void note_the_nap_end(bool something_happened) {
  if (something_happened) {
    if (g_nap_run_ms >= kNapRunSayMs) {
      log_printf(pal::kLogDebug, "POWER", "%u nap(s) in a row, %lld ms asleep, and now something has happened",
                 static_cast<unsigned>(g_nap_run), static_cast<long long>(g_nap_run_ms));
    }
    g_nap_run = 0;
    g_nap_run_ms = 0;
    g_nap_ms = kNapFirstMs;
    return;
  }
  if (g_nap_ms < kNapMostMs && g_nap_ms >= kNapMostMs / 2) {
    // **L'escala ha arribat al sostre**: una línia i prou, que és el que diu que la placa es pot passar hores dormint a trossos
    // d'un segon (docs/95).
    log_printf(pal::kLogDebug, "POWER", "the nap is at its ceiling: %d ms a turn while nothing happens", kNapMostMs);
  }
  g_nap_ms = (g_nap_ms >= kNapMostMs / 2) ? kNapMostMs : g_nap_ms * 2;
}

bool can_deep_sleep() {
  if (g_power == nullptr || !can_sleep(hal::SleepMode::kDeep)) return false;
  if (g_power->can_sleep_with_the_keys()) return true;
  return the_hour_of_a_sleep(0) > 0;
}

void note_activity() {
  g_last_activity_ms = mono_now();
  g_touched_since_boot = true;
}

void note_a_page_on_the_glass() { g_a_page_on_the_glass = true; }

// **Quanta quietud mana** (0.190.9): the setting a person chose in Settings -> Battery —five, ten or fifteen minutes—, i una
// suite que hagués d'esperar-los seria una suite de minuts més lenta, o sigui que on hi ha algú que mogui el número de la prova
// mana el de la prova (docs/60, docs/80).
// **L'única pregunta de la quietud** (0.190.9): seconds-ish of mono time since a person last did anything, and zero for the
// first turn of a boot that has not stamped itself (which is a board that has just come up and not a board that has been quiet
// for ever).
int64_t quiet_ms() {
  const int64_t now = mono_now();
  if (g_last_activity_ms <= 0) {
    g_last_activity_ms = now;
    return 0;
  }
  const int64_t quiet = now - g_last_activity_ms;
  return (quiet > 0) ? quiet : 0;
}

int64_t idle_quiet_ms() {
#if defined(UNIT_TEST) || defined(PIO_UNIT_TESTING)
  return g_idle_quiet_ms;
#else
  return cfg::idle_quiet_ms();
#endif
}

void set_idle_quiet_ms(int64_t ms) {
#if defined(UNIT_TEST) || defined(PIO_UNIT_TESTING)
  g_idle_quiet_ms = (ms < 0) ? 0 : ms;
#else
  (void)ms;  // a board does not move the number of its own policy: it is the one a person chose in Settings -> Battery (docs/60).
#endif
}

bool idle_sleep_allowed() { return g_idle_allowed; }

void set_idle_sleep_allowed(bool on) {
  g_idle_allowed = on;
  // Turning the policy back on does not put the board down in the same millisecond: the
  // quiet count starts again here.
  note_activity();
}

// The idle policy: **a board that keeps the hour in a chip can stop being powered and let
// the quartz count**, which is the whole reason the rule exists (README, law 9). It happens
// only when every one of these holds, and each one is a board that would sleep through
// something:
//
//   - this board's deep sleep is a real power off, which is how the system knows there is a
//     chip behind the hour. On a board whose deep sleep keeps the chip on, the software
//     queue is already free and a restart would be a cost for nothing (that is the ATOM);
//   - an alarm is pending: without a deadline there is nothing to come back to, and a board
//     that sleeps with no hour waits for a finger on the button;
//   - nobody is using it: the console has been quiet, no app is running, no app has said
//     `keep_awake`, and the light is not saying anything —it is an effect that has to be
//     painted every turn, and a board that powers off in the middle of a breath is a light
//     that lies (docs/30);
//   - the wait is long enough that a boot is cheaper than staying awake.
// **La línia que diu per què no** (0.190.12, l'amo: *«No aconsegueixo que saltin. Revisa el codi que fa l'activació i com es suma
// temps al user_idle»*). Una política que calla quan el temps ja ha passat és una política que no es pot depurar: el comptador pot
// estar creixent i el que falla ser una altra condició, i sense la línia les dues coses es veuen igual. Es diu **una vegada per
// motiu** i es torna a dir al minut, perquè un registre que es repeteix a cada torn no el llegeix ningú (docs/30, docs/60).
const char* g_idle_said = nullptr;
int64_t g_idle_said_ms = 0;

void say_the_verdict(int64_t quiet, const char* why) {
  const int64_t now = mono_now();
  if (why == g_idle_said && (now - g_idle_said_ms) < 60000) return;
  g_idle_said = why;
  g_idle_said_ms = now;
  log_printf(pal::kLogInfo, "POWER", "idle: %lld s of quiet (the policy asks %lld) and the board stays awake: %s",
             static_cast<long long>(quiet / 1000), static_cast<long long>(idle_quiet_ms() / 1000), why);
}

void idle_sleep(int64_t next_alarm_epoch_ms) {
  if (!g_idle_allowed || g_power == nullptr || g_clock == nullptr) return;
  if (!g_power->deep_powers_off()) return;
  // A stage of a longer wait has its hour already written down, and it is not in the queue, so
  // it is the one that counts (docs/60).
  const bool staging = g_coming_back_at > 0;
  const int64_t deadline = staging ? g_coming_back_at : next_alarm_epoch_ms;
  // **Una represa per una alarma no espera el comptador** (0.189.182): the hook has already run in the window and there is nobody
  // in front of the glass, o sigui que cada segon despert és un segon de l'espera llençat. **Per a tota la resta mana el comptador**,
  // que és l'únic que hi ha (0.190.9), i la placa només s'apaga quan la quietud **arriba** al temps que la persona va triar: amb cinc
  // minuts al desplegable, cinc minuts i no deu segons (docs/30, docs/60).
  // **Una represa per una alarma i una tanda d'una espera llarga no esperen el comptador** (0.190.18): the board came back on its own and
  // there is nobody in front of the glass, o sigui que cada segon despert és un segon de l'espera llençat. **I només mentre ningú no l'ha
  // tocada**: el comptador mana a qui hi és a sobre, i una mà en el primer torn no ha de veure la placa apagar-se-li a sota (docs/60).
  // **I la regla de la 0.190.12 —«un despert per una alarma no torna a baixar sense res a la cua»— ja no hi és, i no hi ha de ser**
  // (0.190.19): quan el vidre va dir el son, un despert per una alarma **no ha pintat res**, o sigui que quedar-se despert no ensenyava
  // cap pàgina —el que s'hi veia era la imatge del son i la placa gastant corrent—. El que sí que queda d'aquella regla és el **primer
  // torn**: `straight_back_down` només és cert si ningú no ha tocat la placa. **I una alarma que ha de veure's no és un cas d'aquesta
  // política**: si demana el primer pla (`raise`), `for_an_alarm` de l'arrencada ja és fals perquè hi ha una app al davant, i mentre hi
  // sigui la política no dorm (`an app is in front`, a baix) (docs/30, docs/60).
  const bool straight_back_down = (staging || g_for_an_alarm) && !g_touched_since_boot;
  const int64_t quiet = quiet_ms();
  if (!straight_back_down && quiet < idle_quiet_ms()) return;

  // **I ara, pot anar-se'n?** Each of these is a board that would sleep through something, and each one says its name.
  const int64_t now_epoch = g_clock->epoch_ms();
  // **L'hora que s'armarà és la que compta** (19/11): `sleep()` takes the earlier of the deadline and the nearest alarm in
  // the queue, so the guard has to look at the same hour; otherwise an alarm two seconds away passes a guard that only
  // looked at a staging deadline an hour away (docs/60).
  const int64_t hour = the_hour_of_a_sleep(deadline);
  // **Una pàgina que és la de casa no és ningú treballant** (0.189.182): the idle policy refused to sleep while any app was in
  // front, and the board of this system has the launcher there always —o sigui que una placa amb `cfg home` no dormia mai i la
  // seva alarma es quedava esperant una son que no arribava—. La casa es tanca amb els seus comiats i la seva sessió, com qualsevol
  // altra pàgina (docs/60, docs/90).
  const bool the_home_page =
      shell::app_running() && cfg::home_app()[0] != '\0' && strcmp(shell::app_id(), cfg::home_app()) == 0;
  // **La targeta del sistema només existeix on hi ha vidre** (0.190.13): `panel.cpp` sencer és `#if LUNOK_GLASS`, o sigui que les
  // quatre plaques sense pantalla —els dos ATOM, el devkit i el T-Koala— no tenen ni el símbol `panel::open()`, i la crida sense
  // guarda deixava aquelles quatre **sense enllaçar** des de la 0.190.5 (trobat compilant les sis bandes del CI el 18/11). És la
  // mateixa guarda que fa servir `kernel.cpp` per a la targeta (docs/50, docs/75).
  bool the_card_is_out = false;
#if LUNOK_GLASS
  the_card_is_out = panel::open();
#endif
  const char* why = nullptr;
  if ((shell::app_running() && !the_home_page) || keeping_awake()) {
    why = "an app is in front, or an app asked to stay awake";
  } else if (the_card_is_out) {
    // **I la targeta del sistema, que és el sistema en ús** (0.190.5): while the card of the panel is out the app does not run
    // (`kernel.cpp` skips its turn), so the board is the system's and a person is reading it (docs/30, docs/60, docs/90).
    why = "the card of the system is out";
  } else if (led::animating()) {
    // An effect of the light is the board saying something right now, and it is painted once a turn: powering off would cut it.
    why = "the light has a frame to paint";
  } else if (hour > 0 && now_epoch <= 0) {
    why = "the board has no hour to wake at";
  } else if (hour > 0 && (hour - now_epoch) < kMinIdleSleepMs) {
    why = "the alarm is closer than the way down and a boot are worth";
  } else if (hour <= 0 && !can_deep_sleep_with_the_keys()) {
    why = "there is no alarm, and no key wakes this board";
  }
  if (why != nullptr) {
    say_the_verdict(quiet, why);
    return;
  }

  g_coming_back_at = 0;
  // **Sense cap alarma, la porta és la tecla** (0.190.12, l'amo: *«el Toggle per activar el DeepSleep i un selector desplegable del
  // temps»*): the policy did nothing at all when the queue was empty —a `return` with no line—, o sigui que una placa sense alarma no
  // s'adormia mai i el commutador de Battery no feia res. Ara baixa **fins a la tecla**: la sessió es desa, la imatge del son es pinta
  // i la placa torna on era quan algú prem el dial, que és el que aquest commutador vol dir (docs/50, docs/60, docs/90).
  // **I el camí de la tecla és el mateix tant si aquesta arrencada ha vingut per una alarma com si no** (0.190.14). **Aquí no hi ha
  // cap hora** —la cua és buida—, i el comptador de la quietud s'ha esgotat de debò llevat d'una represa que ha arribat fins aquí, o
  // sigui que baixar a la tecla és el que la persona va triar. **Tenir-ho lligat a `g_for_an_alarm` era una porta tancada per dins**:
  // aquella bandera és certa per a tota la run d'un despert per alarma, o sigui que quan la cua es buidava el codi queia al camí de
  // l'alarma amb `deadline` zero, deia `-1767225 s to the next alarm`, `power::sleep` el refusava i el comptador tornava a començar:
  // una placa que havia despertat per una alarma **no tornava a adormir-se mai** fins a reiniciar-se (trobat llegint el codi, 19/11).
  // **I una represa que arriba aquí sense haver-se adormit al primer torn és una mà**: `straight_back_down` només salta el comptador
  // si ningú no ha tocat la placa, i la línia de dalt ja diu quants segons de quietud hi ha hagut (docs/60).
  if (hour <= 0) {
    sayf("power: nothing to do for %lld s, sleeping until a key\r\n", static_cast<long long>(quiet / 1000));
    log_printf(pal::kLogInfo, "POWER",
               "idle deep sleep: no alarm to come back for, %lld s of quiet (the policy asks %lld), a key wakes the board",
               static_cast<long long>(quiet / 1000), static_cast<long long>(idle_quiet_ms() / 1000));
    note_activity();
    // **I la porta és la del sistema i no la del xip** (0.190.14): `shell::deep_sleep` és el mateix camí que el botó de la lluna
    // del panell i el `deepsleep` del prompt, i és qui **acaba la run** quan el que baixa és un emulador (`kernel::shutdown`). Amb
    // la crida al xip i prou, l'emulador es quedava amb la imatge del son al vidre, l'app tancada i el procés viu —i la potència de
    // mentida fins i tot deia «the emulator ends» mentre no s'acabava— (docs/50, docs/60, docs/85).
    (void)shell::deep_sleep("the board is idle and there is no alarm");
    return;
  }
  g_idle_said = nullptr;
  // **La línia del per què** (0.190.5, l'amo: *«posa log quan hagi de saltar el deepsleep automátic»*). A sleep that comes from the policy
  // and not from a person is a line a board should be able to explain, o sigui que la línia diu **les tres xifres de la decisió**: quant
  // falta per a la propera alarma, quanta estona fa que la placa està quieta i quants segons de quietud li demana la política —i si és una
  // represa d'una alarma ho diu, perquè aquesta és l'única decisió que no espera el comptador— (docs/60, docs/90).
  const int64_t wait = hour - now_epoch;
  sayf("power: nothing to do for %lld s, sleeping until the next alarm\r\n",
       static_cast<long long>(wait / 1000));
  log_printf(pal::kLogInfo, "POWER",
             "idle deep sleep: %lld s to the next alarm, %lld s of quiet (the policy asks %lld), %s",
             static_cast<long long>(wait / 1000), static_cast<long long>(quiet / 1000),
             static_cast<long long>(idle_quiet_ms() / 1000),
             (!staging && g_for_an_alarm) ? "a wake for an alarm goes straight back down" : "nobody has used the board");
  // Does not come back: on this board the power goes and the clock chip brings it back.
  // If it does come back (a cable in the slot), the board is being used again.
  sleep(hal::SleepMode::kDeep, deadline);
  note_activity();
}

bool request_sleep(hal::SleepMode mode, int64_t at_epoch_ms) {
  if (g_power == nullptr || !can_sleep(mode)) return false;
  if (mode == hal::SleepMode::kDeep && at_epoch_ms <= 0) return false;
  // One sleep at a time: two answers would be two promises for one wake.
  if (g_pending) return false;
  g_pending = true;
  g_pending_from_app = true;
  g_pending_mode = mode;
  g_pending_at = at_epoch_ms;
  return true;
}

static void govern_cpu();

void poll(int64_t next_alarm_epoch_ms) {
  // **La finestra d'una feina pesada s'acaba aquí** (0.179.38): the boot and the start of an app take the top step for a
  // few seconds, and the step back is not the caller's to watch —an app cannot be trusted with the clock of its own
  // burst— so it is a turn of this system like the sleep is (docs/50, docs/60).
  if (g_cpu_busy_until_ms > 0 && pal::system().millis() >= g_cpu_busy_until_ms) {
    g_cpu_busy_until_ms = 0;
    // **I el final d'una finestra no mou res** (0.189.95): with the automatic policy on, a window is a loan of the top step
    // for as long as the work lasts and the governor is what decides after it, at its own samples —a step back to the
    // resting one in the same turn the window closes is two moves for a paint and the third line of a log that a person
    // reads as a board that cannot make up its mind— (the owner, with the log: *«és massa ràpid»*, docs/50, docs/60).
    if (!policy_is_auto()) move_to(g_cpu_rest_mhz, "the load is over");
  }
  // **I a cada torn, la càrrega** (0.189.94): `load()` samples one window of 250 ms and answers the last one inside it, so
  // this costs two reads of the meters on the turns that do not end a window, and the move happens only when the word
  // changes (docs/30, docs/95).
  govern_cpu();
  if (g_pending) {
    const bool from_app = g_pending_from_app;
    const hal::SleepMode mode = g_pending_mode;
    const int64_t at = g_pending_at;
    g_pending = false;
    g_pending_from_app = false;

    const bool back = sleep(mode, at);
    // The promise of an app is kept when the board is back. A deep sleep closes the
    // app, so there is nobody left to tell (docs/30).
    if (from_app && mode != hal::SleepMode::kDeep) luabind::power_progress(back);
    return;
  }
  idle_sleep(next_alarm_epoch_ms);
}

bool keep_awake(bool on) {
  g_keep_awake = on;
  return g_keep_awake;
}

bool keeping_awake() { return g_keep_awake; }

bool battery(int& mv, int& pct, bool& charging) {
  // The gauge answers over the I2C and the bar asks for it on every turn (docs/50).
  meter::Scope scope(meter::kPowerBattery);
  if (g_power == nullptr) return false;
  return g_power->battery(mv, pct, charging);
}

// **La bateria que s'ha acabat** (0.183.0): one question with one rule —a gauge that answers, no cable in the slot, and a
// charge at or under the mark— so that the boot, the way down and `power` cannot disagree about a board that is over. A
// board whose gauge says nothing is not a flat board, and the answer says so by being false (docs/50).
bool battery_is_low(int& pct) {
  pct = 0;
  int mv = 0;
  bool charging = false;
  if (!battery(mv, pct, charging)) return false;
  return !charging && pct <= static_cast<int>(kLowBatteryPercent);
}

// **The picture of the boot when the battery is what it is** (0.183.0): the drawing the person chose, unless the cell is
// under the mark, and then `lowbatt` — a board that comes up with nothing left says it once, on the one page nobody has to
// ask for (docs/50).
splash::Kind boot_picture(const char* chosen, bool known, int pct, bool charging) {
  if (known && !charging && pct <= static_cast<int>(kLowBatteryPercent)) return splash::kLowBattery;
  const splash::Kind moment = splash::kind_of(chosen);
  return (moment == splash::kNone) ? splash::kBoot : moment;
}

// **The picture of the way down** (0.183.0): the reason the board stops is what the glass says. A deep sleep is `sleep`
// —the board comes back by itself and the picture is the promise of it—, a board whose battery has gone is `lowbatt`, and
// everything else is the goodbye of `poweroff`. A light sleep is a nap and paints nothing (docs/50).
splash::Kind going_down_picture(bool deep) {
  int pct = 0;
  if (battery_is_low(pct)) return splash::kLowBattery;
  return deep ? splash::kSleep : splash::kPowerOff;
}

// ---- la càrrega del sistema (0.189.93, docs/50, docs/60) ----
//
// **Dues lectures del comptador i cap rellotge propi**: the load of a window is the difference of the meters between two
// asks —the turn of the kernel and the wait inside it—, so this costs nothing when nobody asks and it cannot drift from the
// numbers `stats` prints. What is kept is the last sample and when it was taken (docs/30, docs/95).
namespace {

const uint32_t kLoadLowPct10 = 200;    // under twenty per cent the board is reading a page and nothing else
const uint32_t kLoadHighPct10 = 600;   // over sixty it is painting, starting an app or turning a page of a book
const uint32_t kLoadBandPct10 = 50;    // five points of band, or a clock that moves at every turn

uint32_t g_load_at_ms = 0;
uint64_t g_load_loop_us = 0;
uint64_t g_load_wait_us = 0;
uint32_t g_load_pct10 = 0;
Load g_load = Load::kNormal;

}  // namespace

Load load_of(uint32_t percent10, Load current) {
  switch (current) {
    case Load::kLow:
      return (percent10 >= kLoadLowPct10 + kLoadBandPct10) ? Load::kNormal : Load::kLow;
    case Load::kHigh:
      return (percent10 + kLoadBandPct10 <= kLoadHighPct10) ? Load::kNormal : Load::kHigh;
    case Load::kNormal:
    default:
      if (percent10 >= kLoadHighPct10) return Load::kHigh;
      if (percent10 + kLoadBandPct10 < kLoadLowPct10) return Load::kLow;
      return Load::kNormal;
  }
}

const char* load_name(Load load) {
  switch (load) {
    case Load::kLow:
      return "low";
    case Load::kHigh:
      return "high";
    case Load::kNormal:
    default:
      return "normal";
  }
}

Load load() {
  const uint32_t now = pal::system().millis();
  const uint64_t loop_us = meter::us(meter::kLoop);
  const uint64_t wait_us = meter::us(meter::kQueueWait);
  // **La primera pregunta contesta des de l'arrencada i obre la finestra**: a board that has just booted has no window of
  // its own yet, and a first answer of "normal, zero per cent" is a word that is not the truth —the boot is the heaviest
  // thing this board does—. Division by nothing is what a first draft did, and it said a hundred per cent for ever (vist a
  // l'emulador, 15/11).
  if (g_load_at_ms == 0) {
    if (now > 0) {
      const uint64_t busy = (loop_us > wait_us) ? (loop_us - wait_us) : 0;
      g_load_pct10 = static_cast<uint32_t>((busy * 1000ULL) / (static_cast<uint64_t>(now) * 1000ULL));
      g_load = load_of(g_load_pct10, g_load);
    }
    g_load_at_ms = now;
    g_load_loop_us = loop_us;
    g_load_wait_us = wait_us;
    return g_load;
  }
  const uint32_t span = now - g_load_at_ms;
  if (span < kLoadWindowMs) return g_load;
  const uint64_t turned = (loop_us > g_load_loop_us) ? (loop_us - g_load_loop_us) : 0;
  const uint64_t waited = (wait_us > g_load_wait_us) ? (wait_us - g_load_wait_us) : 0;
  const uint64_t busy = (turned > waited) ? (turned - waited) : 0;
  g_load_pct10 = static_cast<uint32_t>((busy * 1000ULL) / (static_cast<uint64_t>(span) * 1000ULL));
  g_load = load_of(g_load_pct10, g_load);
  g_load_at_ms = now;
  g_load_loop_us = loop_us;
  g_load_wait_us = wait_us;
  return g_load;
}

uint32_t load_percent10() { return g_load_pct10; }

bool cpu_auto() { return policy_is_auto(); }

void set_cpu_auto(bool on) {
  set_policy(on);
  if (on) {
    // **Tornar-la a encendre comença un minut nou**: the samples of before are the samples of a board that was being told what
    // to do by a person, and a trend measured across that boundary is a trend of nothing (docs/60).
    g_trend_count = 0;
    g_trend_ms = 0;
    // **Tornar-la a encendre és una decisió que es pren ara i no al proper torn**: the board goes to the step its load says
    // in the same line that turns the policy on, or a person would see the answer of a policy that has not run (docs/60).
    govern_cpu();
  }
}

// ---- la tendència de la càrrega (0.189.95, l'amo) ----
//
// *«No facis el canvi de freqüència tant sovint... Guarda mostres, durant un minut: si la tendència és a més, puja la CPU; si
// és a menys, la baixes; si és semblant, et quedes com estàs.»*
//
// **Un minut de mostres i la meitat contra l'altra**: a sample every two seconds —thirty of them, thirty bytes— and the
// comparison is the mean of the last fifteen against the mean of the fifteen before. That is a **trend** and not a level: a
// board that has been at ten per cent for a minute and a board that has just fallen from forty to ten read the same
// instantaneous load and are not the same board, and the clock of the processor is a thing that costs more to move than to
// leave where it is (docs/50, docs/60).
void take_a_sample() {
  const uint32_t now = pal::system().millis();
  if (g_trend_ms == 0) g_trend_ms = now;
  if (now - g_trend_ms < kTrendEveryMs) return;
  g_trend_ms = now;
  const uint32_t pct = load_percent10() / 10;
  if (g_trend_count < kTrendSamples) {
    g_trend[g_trend_count++] = static_cast<uint8_t>((pct > 100) ? 100 : pct);
    return;
  }
  for (uint8_t i = 1; i < kTrendSamples; ++i) g_trend[i - 1] = g_trend[i];
  g_trend[kTrendSamples - 1] = static_cast<uint8_t>((pct > 100) ? 100 : pct);
}

int32_t trend_of(const uint8_t* samples, uint8_t count) {
  if (samples == nullptr || count < 2) return 0;
  const uint8_t half = static_cast<uint8_t>(count / 2);
  uint32_t was = 0;
  uint32_t goes = 0;
  for (uint8_t i = 0; i < half; ++i) was += samples[i];
  for (uint8_t i = static_cast<uint8_t>(count - half); i < count; ++i) goes += samples[i];
  return static_cast<int32_t>(goes - was) * 10 / static_cast<int32_t>(half);
}

uint32_t trend_step(uint32_t mhz, int32_t trend10, uint8_t recent_pct) {
  // **La tendència mana**: a minute that went up is a board that is doing more, and it goes one step up —the minute after is
  // the one that says whether it was worth going to the top—.
  if (trend10 >= static_cast<int32_t>(kTrendBandPct10)) {
    if (mhz < kRestingMhz) return kRestingMhz;
    if (mhz < kBusyMhz) return kBusyMhz;
    return mhz;
  }
  if (trend10 + static_cast<int32_t>(kTrendBandPct10) <= 0) {
    if (mhz > kRestingMhz) return kRestingMhz;
    if (mhz > kIdleMhz) return kIdleMhz;
    return mhz;
  }
  // **I si la tendència és plana, el nivell**: a board that has done nothing for half a minute is idle and it goes to the
  // bottom, and one that has been busy all along goes to the top, in one move and not two —the trend is the shape of the
  // minute and this is what the minute is— (docs/60, docs/95).
  if (recent_pct <= kIdleMeanPct && mhz > kIdleMhz) return kIdleMhz;
  if (recent_pct >= kBusyMeanPct && mhz < kBusyMhz) return kBusyMhz;
  return mhz;
}

// **El governador** (0.189.95): the trend of the load turned into one step of the processor, and nothing else. **low is 80,
// normal 160, high 240**, and it moves **one step at a time in the direction of the trend**: a board that has been climbing a
// minute goes from 80 to 160 and does not jump to 240 on the same turn it climbs, because the minute after that is what says
// whether it was a page turning or a book being searched. A window somebody asked for —the boot, an app starting, a paint— is
// not its business (docs/50, docs/60).
static void govern_cpu() {
  if (g_power == nullptr || !policy_is_auto()) return;
  const uint8_t before = g_trend_count;
  take_a_sample();
  // **Una decisió per mostra i no per torn**: the samples are two seconds apart, so the clock can move at most once every two
  // seconds however many turns the board takes in between, and a page that paints does not change its mind in the middle
  // (docs/50, docs/60).
  if (g_trend_count == before || g_trend_count == 0) return;
  if (g_cpu_busy_until_ms != 0) return;
  const uint32_t mhz = g_power->cpu_mhz();
  const uint8_t half = static_cast<uint8_t>(g_trend_count / 2);
  uint32_t recent = 0;
  const uint8_t count = (half > 0) ? half : g_trend_count;
  for (uint8_t i = static_cast<uint8_t>(g_trend_count - count); i < g_trend_count; ++i) recent += g_trend[i];
  const uint32_t mean = (count > 0) ? (recent / count) : 0;
  const int32_t trend = trend_of(g_trend, g_trend_count);
  const uint32_t want = trend_step(mhz, trend, static_cast<uint8_t>(mean));
  if (want == mhz) return;
  const char* why = (trend >= static_cast<int32_t>(kTrendBandPct10)) ? "the load is climbing"
                    : (trend + static_cast<int32_t>(kTrendBandPct10) <= 0) ? "the load is falling"
                    : (want < mhz) ? "the load is low" : "the load is high";
  move_to(want, why);
}

uint32_t cpu_mhz() { return (g_power != nullptr) ? g_power->cpu_mhz() : 0; }

uint32_t resting_cpu_mhz() { return g_cpu_rest_mhz; }

bool set_cpu_mhz(uint32_t mhz) {
  if (g_power == nullptr) return false;
  // The steps are the board's, and the kernel does not invent them: 80 is the
  // floor because below it the APB goes down with the CPU and the serial port
  // stops making sense, and 240 is the ceiling of this chip (docs/50).
  if (mhz < 80 || mhz > 240) return false;
  // **El que no és un dels tres passos es refusa i no s'arrodoneix** (0.179.38): a rounded speed is a lie about what it costs,
  // and the prompt says the number back (`power cpu 200` is not 240). The clamp of 0.189.107 answers the other question —a
  // step the person forbade— and it was turning that refusal into a silent round up, which is what 0.189.112 undoes here
  // (docs/50, docs/60).
  if (mhz != kIdleMhz && mhz != kRestingMhz && mhz != kBusyMhz) return false;
  // **El pas que demana una persona també s'acota** (0.189.107): asking for 240 on a board whose owner forbade it is an ask
  // that lands on the biggest step allowed, and the line of the move says which one (docs/50).
  mhz = allowed_step(mhz);
  const uint32_t before = g_power->cpu_mhz();
  // The processor changes market: it is a call to the board and not a variable (docs/30). **And the port is emptied
  // before it**, or the line in flight comes out garbled (0.179.43, `empty_the_line`).
  meter::Scope scope(meter::kCpuSet);
  empty_the_line();
  if (!g_power->set_cpu_mhz(mhz)) return false;
  // **La velocitat que demana algú és la de repòs** (0.179.38): a person at the prompt or an app that asked for a step is
  // choosing what the system works at, so the step is not undone by the next start that ends; and a window that was owed
  // is not owed any more, because the next step to take is this one.
  g_cpu_rest_mhz = mhz;
  g_cpu_busy_until_ms = 0;
  // **Un pas demanat a mà para la política automàtica** (0.189.94): a person who fixed a step did it to measure something,
  // and a governor that undoes it two hundred and fifty milliseconds later is a governor nobody can measure against.
  set_policy(false);
  log_printf(pal::kLogInfo, "POWER", "cpu %u MHz from %u (asked, the automatic scaling is off)", static_cast<unsigned>(g_power->cpu_mhz()),
             static_cast<unsigned>(before));
  return true;
}

void hold_cpu(uint32_t mhz, int32_t hold_ms, const char* why) {
  if (g_power == nullptr || mhz < 80 || mhz > 240) return;
  move_to(mhz, why);
  // **The window is the caller's and its end is the system's**: nothing has to keep a clock for it, because `poll` is a
  // turn of this system and a turn that is not taken is a board that is off.
  g_cpu_busy_until_ms = (hold_ms > 0) ? (pal::system().millis() + hold_ms) : 0;
}

void rest_cpu(const char* why) {
  if (g_power == nullptr) return;
  g_cpu_busy_until_ms = 0;
  if (policy_is_auto()) {
    govern_cpu();
    return;
  }
  move_to(g_cpu_rest_mhz, why);
}

// **Una feina vol el cim** (0.190.26): the top step for the length of the window, and the governor is told to stay out of it —
// `hold_cpu` with a window is exactly that— because a decode that starts slow and ends fast is one piece of work and not a load
// that climbed (docs/50, docs/60).
void work_cpu(const char* why) {
  if (!policy_is_auto()) return;
  hold_cpu(kBusyMhz, kWorkWindowMs, why);
}

// **I una espera no vol res** (0.190.26): the bottom step, and **no window**, perquè el que ve després no és una espera i el
// governador ha de poder tornar-lo a pujar quan la mostra ho digui. It is the card, the wire and the ink of the panel: time the
// chip spends with nothing to compute (docs/50, docs/60).
void wait_cpu(const char* why) {
  if (!policy_is_auto()) return;
  hold_cpu(kIdleMhz, 0, why);
}

void restore_session() {
  if (g_nvs == nullptr) return;
  char id[kIdCap];
  if (!read_text(kAppKey, id, sizeof(id))) return;
  char path[kPathCap];
  const bool has_path = read_text(kPathKey, path, sizeof(path));
  // Read once: an app that cannot start must not make the board boot into it for
  // ever, and a boot that comes back to the same app is the same as a boot loop
  // nobody asked for (docs/90).
  write_text(kAppKey, nullptr);
  write_text(kPathKey, nullptr);

  // **La línia diu el que passa i no el que s'intentava** (0.190.24): it was printed before the two guards, so a boot that
  // came back for an alarm said `home_eink comes back where it was` and then, in the next line, `home_eink does not come
  // back: clock is in front` —two lines that contradict each other, of an app that did not move—. One line per outcome
  // (docs/60, docs/90).
  //
  // **Una app que ha pujat al davant es queda amb el vidre** (0.189.175): a `raise` alarm delivered at boot has already put
  // its app in front, and a second `run_app` on top of it is refused by the interpreter.
  if (shell::app_running()) {
    sayf("power: %s does not come back: %s is in front\r\n", id, shell::app_id());
    return;
  }
  if (!shell::restore_app(id, has_path ? path : nullptr)) {
    sayf("power: %s could not come back (is it still there?)\r\n", id);
    return;
  }
  sayf("power: %s comes back where it was\r\n", id);
}

void forget_session() {
  write_text(kAppKey, nullptr);
  write_text(kPathKey, nullptr);
  write_text(kWakeKey, nullptr);
}

// **La marca que la RAM del RTC es queda** (0.189.180): four bytes at the front of `/rtc`, which is the one memory the power takes
// away and a reset does not. Nothing else in this system writes that file (docs/70).
const uint8_t kBootMagic[4] = {'L', 'U', 'N', 'K'};
BootKind g_boot_kind = BootKind::kNew;

// The note a deep sleep leaves, read and consumed (docs/60).
bool coming_back_from_a_sleep(hal::IKeyValue& nvs) {
  char text[8];
  size_t len = 0;
  const bool slept = nvs.get(kPowerNs, kSleptKey, text, sizeof(text) - 1, len);
  if (slept) nvs.erase(kPowerNs, kSleptKey);
  return slept;
}

// **El que la RAM del RTC recorda**: the mark is four bytes and it is not a filesystem, so a file that does not open is a memory
// that is not there and a memory that is not there is the power coming back (docs/70).
bool the_rtc_remembers(hal::IStorage* rtc) {
  if (rtc == nullptr) return false;
  hal::FileSlot slot;
  if (!rtc->open("", hal::OpenMode::kRead, slot)) return false;
  uint8_t head[sizeof(kBootMagic)] = {0, 0, 0, 0};
  const int32_t got = rtc->read(slot, head, sizeof(head));
  rtc->close(slot);
  if (got != static_cast<int32_t>(sizeof(head))) return false;
  return memcmp(head, kBootMagic, sizeof(head)) == 0;
}

void leave_the_mark(hal::IStorage* rtc) {
  if (rtc == nullptr) return;
  hal::FileSlot slot;
  if (!rtc->open("", hal::OpenMode::kReadWrite, slot)) return;
  rtc->write(slot, kBootMagic, sizeof(kBootMagic));
  rtc->close(slot);
}

BootKind boot_kind() { return g_boot_kind; }

bool came_back_for_an_alarm() { return g_for_an_alarm; }

bool woken_by_an_alarm() { return g_woke_by_an_alarm; }

const char* boot_kind_word(BootKind kind) {
  switch (kind) {
    case BootKind::kRestart:
      return "restart";
    case BootKind::kSleep:
      return "sleep";
    default:
      return "new";
  }
}

BootKind note_the_boot(hal::IPlatform& platform) {
  hal::IKeyValue* nvs = platform.nvs();
  hal::IStorage* rtc = platform.storage("/rtc");
  // **I què l'ha despertada, aquí i no a `power::boot`** (0.189.182): the clock is a door of the platform and this is the one call
  // of the boot that has it before anything else has been touched; the two answers of a chip that came back are its own timer and
  // the clock chip's quartz, and a button, the console or the power are a person (docs/50, docs/60).
  hal::IClock* clock = platform.clock();
  const hal::WakeCause woke = (clock != nullptr) ? clock->last_wake() : hal::WakeCause::kOther;
  g_woke_by_an_alarm = (woke == hal::WakeCause::kTimer || woke == hal::WakeCause::kClock);
  // **Un boot nou, i dues coses que són d'aquest boot** (0.190.18): nobody has touched the board yet —which is what makes a wake for an
  // alarm and a stage of a long wait go straight down— and the glass still says what the sleep left on it. Es posa aquí i no a
  // `power::boot` perquè les finestres de les alarmes corren **abans** (`kernel::boot`), i una finestra que ha pintat és un vidre que ja no
  // diu el son (docs/30, docs/60).
  g_touched_since_boot = false;
  g_a_page_on_the_glass = false;
  // **I les dues pistes de què és aquesta arrencada, creuades** (0.190.68, l'amo: *«recorda que l'esp32 te estats al arrencar, així el
  // firmware sabrà si ha despertat normal o d'un deep sleep»*): la marca que el sistema va deixar al magatzem abans de baixar i el motiu
  // que el xip porta al registre de reset. **La marca mana quan hi és** —el Paper talla el corrent per dormir, o sigui que la seva
  // tornada és un `power on` de manual i el xip no pot distingir-la d'un endollament—, i **el xip mana quan la marca s'ha perdut**: una
  // NVS que no va prendre l'escriptura no és una arrencada nova, perquè el que hi ha al vidre i al disc és el run de la persona
  // (docs/50, docs/60, docs/75).
  const hal::ResetCause reset = platform.reset_cause();
  const bool the_mark_says_a_sleep = (nvs != nullptr) && coming_back_from_a_sleep(*nvs);
  const bool the_chip_says_a_sleep = (reset == hal::ResetCause::kDeepSleep);
  if (the_mark_says_a_sleep || the_chip_says_a_sleep) {
    g_boot_kind = BootKind::kSleep;
  } else if (the_rtc_remembers(rtc)) {
    g_boot_kind = BootKind::kRestart;
  } else {
    g_boot_kind = BootKind::kNew;
  }
  // **I quan les dues pistes no diuen el mateix, es diu** (0.190.68): una marca sense un xip que vingui d'un son és el Paper fent la
  // seva feina i no té res a explicar; un xip que ve d'un son **sense** marca sí, perquè vol dir que l'escriptura de la baixada no hi
  // era i que aquesta arrencada s'ha salvat pel maquinari (docs/50, docs/60).
  if (the_chip_says_a_sleep && !the_mark_says_a_sleep) {
    log_printf(pal::kLogWarn, "BOOT",
               "the chip came back from a deep sleep and the mark of the sleep is not in the store: this boot is a return anyway");
  }
  // **I la marca es deixa per a la propera arrencada**, sigui quina sigui aquesta: una RAM que ja la té vol dir que el corrent no
  // ha marxat, i una de buida que la pila s'ha desconnectat (docs/50, docs/70).
  leave_the_mark(rtc);
  // **El motiu del despert, dit clar i en una línia** (0.190.19, l'amo: *«Posa ben clar al despertar de DeepSleep quin es el motiu del
  // wake»*): the board knows which of its doors opened —the clock chip, the chip's timer, the dial's key or the glass— and this is the line
  // that says it, with what it means: an alarm is nobody in front and the glass stays as it was; a person is the page coming back.
  const char* by = "the power coming back";
  switch (woke) {
    case hal::WakeCause::kClock:
      by = "the clock chip";
      break;
    case hal::WakeCause::kTimer:
      by = "the timer of the chip";
      break;
    case hal::WakeCause::kTouch:
      by = "a finger on the glass";
      break;
    case hal::WakeCause::kOther:
      by = "the key of the dial";
      break;
    case hal::WakeCause::kConsole:
      by = "the console";
      break;
    default:
      break;
  }
  switch (g_boot_kind) {
    case BootKind::kSleep:
      if (g_woke_by_an_alarm) {
        log_printf(pal::kLogInfo, "BOOT", "back from a deep sleep, woken by %s: an alarm, nobody in front, the glass is not touched",
                   by);
      } else {
        log_printf(pal::kLogInfo, "BOOT", "back from a deep sleep, woken by %s: a person, the page comes back", by);
      }
      break;
    case BootKind::kRestart:
      // **I amb el motiu que el xip hi ha posat, quan en té un de propi** (0.190.68): a restart with the power on is a button, a
      // `reboot` or a fault, and the difference between a person and a panic is exactly what a person reading the log wants to know.
      // Un xip que diu que el corrent ha tornat o que ve d'un son no hi afegeix res —el primer és una contradicció amb la marca que
      // aquesta mateixa línia ja explica, i el segon seria una tornada i no un reinici—, o sigui que la línia es queda com era (docs/60).
      if (reset == hal::ResetCause::kPowerOn || reset == hal::ResetCause::kDeepSleep) {
        log_printf(pal::kLogInfo, "BOOT", "the chip restarted and the power never went away");
      } else {
        log_printf(pal::kLogInfo, "BOOT", "the chip restarted and the power never went away: %s",
                   hal::reset_cause_word(reset));
      }
      break;
    default:
      log_printf(pal::kLogInfo, "BOOT", "a new run: the power came back");
      break;
  }
  return g_boot_kind;
}

bool the_chip_restarted() { return g_the_chip_restarted; }

void clear_the_restart() { g_the_chip_restarted = false; }

// **El repòs abans de tallar el corrent** (0.189.18): the number is one and it starts at the two seconds the owner asked
// for. A test that cuts the supply sets it to zero and a board that one day wants another number changes it here.
uint32_t g_leaving_ms = kLeavingMs;

uint32_t leaving_ms() { return g_leaving_ms; }

void set_leaving_ms(uint32_t ms) { g_leaving_ms = ms; }

// **Tot el que pot quedar a mig fer, acabat** (0.189.18): four devices and one rest, in the order that matters. The
// console first, because what a person reads is written line by line and the port has a buffer of its own; the volumes
// after, once each and however many doors of the VFS point at them (`/apps`, `/cfg`, `/fonts`, `/session` and `/nvs` are
// rows over the same two disks); the store of the board last, and then the margin (docs/70, docs/75).
Leaving settle_before_leaving(const char* why) {
  Leaving done;
  const char* reason = (why != nullptr && why[0] != '\0') ? why : "the board is going down";

  if (pal::IConsole* console = pal::system().console()) {
    console->flush();
    done.console = true;
  }

  // **Un disc compta una vegada encara que hi apuntin cinc portes**: `/apps`, `/cfg`, `/fonts`, `/session` i `/nvs` són
  // files de la mateixa taula sobre dos discs, i rentar el mateix volum cinc cops seria dir-li a la targeta cinc vegades
  // que escrigui el que ja ha escrit. `kVolumes` és el que la taula del VFS pot tenir.
  const uint8_t kVolumes = 8;
  hal::IStorage* seen[kVolumes];
  for (uint32_t i = 0; i < vfs::mount_count() && done.volumes < kVolumes; ++i) {
    hal::IStorage* storage = nullptr;
    char rel[8];
    if (!vfs::resolve(vfs::mount_path(i), storage, rel, sizeof(rel)) || storage == nullptr) continue;
    bool already = false;
    for (uint8_t j = 0; j < done.volumes; ++j) {
      if (seen[j] == storage) already = true;
    }
    if (already) continue;
    seen[done.volumes] = storage;
    ++done.volumes;
    if (storage->flush()) ++done.volumes_ok;
  }

  if (hal::IKeyValue* store = hal::platform().nvs()) done.store = store->commit();

  log_printf(pal::kLogInfo, "POWER", "%s: the console, %u of %u volume(s) and the store are written", reason,
             static_cast<unsigned>(done.volumes_ok), static_cast<unsigned>(done.volumes));

  // **I el repòs, que és el que cap crida pot fer per la targeta**: the controller of a card writes its own buffer into the
  // flash some milliseconds after the last sector has arrived, and there is no call of the filesystem that waits for it
  // (docs/75).
  done.margin_ms = g_leaving_ms;
  if (done.margin_ms > 0) {
    log_printf(pal::kLogInfo, "POWER", "a margin of %u ms before the current goes (a card finishes writing after the last sector)",
               static_cast<unsigned>(done.margin_ms));
    // **I aquesta estona no es dorm** (0.190.69): the margin is the last thing before the current goes and it must not depend on a
    // mechanism that is still being proven. La nit del 20/11, al Paper, la becaina que s'hi va intentar va deixar la placa sense
    // consola i sense arribar a baixar mai; el que s'hi guanya són dos segons cada moltes hores i el que s'hi pot perdre és la baixada
    // sencera (docs/50, docs/75).
    pal::delay_us(static_cast<uint32_t>(done.margin_ms) * 1000U);
  }
  return done;
}

}  // namespace power
}  // namespace kernel
