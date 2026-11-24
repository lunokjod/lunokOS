// The energy policy: the board can stop its clocks, and this is the one place that
// says when, what is said to the app first and what is read afterwards (docs/90).
#include "board/identity.h"
#include "kernel/meter.h"
#include "kernel/panel.h"
#include "kernel/power.h"

#include "kernel/cfg.h"
#include "kernel/led.h"
#include "kernel/lock.h"
#include "kernel/splash.h"
#include "kernel/ui.h"
#include "kernel/vfs.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/shell.h"
#include "kernel/led.h"
#include "kernel/log.h"
#include "kernel/session.h"
#include "kernel/serial.h"
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
// **I qui tenia el cable** (0.190.278): an app that went to sleep in the background has to come back in the background, or a board that
// wakes with the launcher on the glass leaves the prompt mute —the app in front owns the console and the person is not typing at it—.
// The console travels with the chain, i el son profund és l'única cosa que trenca la cadena (docs/30, docs/60, docs/90).
const char kDetachedKey[] = "detached";
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
// **Les dues decisions d'una persona sobre la velocitat, desades** (0.190.85, l'amo: *«He desactivat l'scaling de CPU, perqué
// surt aquest missatge? ... Tot ha de passar per l'scaling, si no, la cpu va al màxim per defecte, o la velocitat marcada a
// Settings -> CPU -> Speed»*). Fins avui el commutador i el pas triat vivien **només a la RAM**: al primer reinici el sistema
// tornava a l'escala automàtica i a 240, o sigui que la persona apagava l'escala i la placa la tornava a encendre sola (docs/50,
// docs/60, docs/95).
const char kCpuAutoKey[] = "cpuauto";
const char kCpuSpeedKey[] = "cpuspeed";

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
// **El fet que la política no pot canviar, dit una vegada** (0.190.88): viu aquí dalt perquè `boot()` i el commutador l'esborren, i
// el que diu és que aquesta placa no es baixarà sola en aquest run (docs/60).
const char* g_idle_fact_said = nullptr;
// **Qui ha tocat la placa des d'aquest boot, i si el vidre té una pàgina que no és la del son** (0.190.18). The first is what makes a
// wake for an alarm or a stage of a long wait go straight back down **and** what keeps it from going down under a finger; the second is
// the window that painted over the picture (docs/30, docs/60).
bool g_touched_since_boot = false;
bool g_a_page_on_the_glass = false;
int64_t g_last_activity_ms = 0;
// **El comptador de la quietud té dos rellotges i un testimoni** (0.190.77, l'amo del 20/11: *«Quan arriba el DeepSleep? … el LightSleep fa
// que el bucle sigui molt mes lent i no compta les voltes o has fet malament el conteig dels 5 minuts o esborres el idle counter del usuari
// quan entres en light Sleep»*). The countdown before a deep sleep is one number and it was kept with the SoC's monotonic alone, o sigui que
// **qualsevol cosa que se la mengi —una becaina que no la compta, un rellotge que torna enrere— deixa la placa desperta per sempre i sense
// dir res**. Ara es desen **les dues** marques (el monòton i l'hora) i la quietud és **la més llarga de les dues**, i cada moviment porta el
// seu nom: qui l'ha mogut i quan. Un comptador que es reinicia sol i no ho diu és el que no es pot depurar (docs/30, docs/60, docs/95).
int64_t g_last_activity_epoch_ms = 0;
power::Hand g_last_hand = power::Hand::kBoot;
int64_t g_last_hand_ms = 0;
// **El que l'hora deia l'última vegada que es va mirar, i el que deia el monòton** (0.190.79): dues marques per saber si el que s'ha mogut
// entre dues mirades és el temps o el rellotge. Una hora posada a mà, una sincronització per xarxa o el canvi d'horari **salten** endavant o
// enrere, i un salt no és quietud: llegit com a quietud, un rellotge posat endavant adorm la placa a sota de la mà que l'acaba de posar. El
// llindar és de cinc segons perquè una becaina mou l'hora un segon com a molt i el monòton no la compta (docs/60, docs/95).
int64_t g_seen_epoch_ms = 0;
int64_t g_seen_mono_ms = 0;
const int64_t kClockStepMs = 5000;

// **La velocitat de repòs** (0.179.38): the step a person chose, which is what the board runs at when the automatic scaling is
// off and what the range is made of when it is on. The window that used to be here —`g_cpu_busy_until_ms`, the clock of a
// burst— **marxa amb la 0.190.175**: the chip's top-step lock counts its own takes, o sigui que la finestra és un comptador del
// xip i no un rellotge del nucli (docs/50, docs/60).
uint32_t g_cpu_rest_mhz = kRestingMhz;
// **El rang que el xip té ara**, dit una vegada: `esp_pm_configure` is not a call to make on every turn, and a range that has
// not moved is not configured again (docs/50).
// **Si aquesta placa escala el rellotge** (0.190.227, l'amo: *«Desactiva el Dynamic Frequency Scaling per l'M5 Atom Lite i l'M5 Atom Matrix.
// Només per aquests dos models. Que vagin sempre a 240Mhz»*): ho diu la placa a l'arrencada i no canvia mai més en un run. Amb això fals, el
// rang és un sol valor —el cim— i el commutador de la pàgina i el `power auto` ho diuen i no fan res (docs/50, docs/60).
bool g_fixed_clock = false;
uint32_t g_range_low = 0;
uint32_t g_range_high = 0;
// **La política i el terra i el sostre, en un sol byte** (0.189.94, 0.189.107): the automatic policy is the fourth bit and the
// steps the person allows are the low three. **Des de la 0.190.175 el del mig no s'usa** (l'amo: *«el toggle del mig, fora»*):
// amb el DFS del xip el que hi ha és un terra i un sostre, i una freqüència entre els dos la tria el xip i no el nostre mask.
// **The two used to be a bool and a byte**, which is eight bytes of `.bss` with their padding in a build whose `dram0.bss`
// overflowed by eight the day the second one arrived (docs/75).
const uint8_t kAllowedMask = 0x05;  // 80 and 240: the floor and the ceiling the chip is allowed to move between
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

// **El terra i el sostre del mask** (0.190.175): the two steps that are still a setting, and the middle one that the chip decides.
uint32_t allowed_low() {
  const uint8_t mask = allowed_bits();
  if ((mask & 0x01) != 0) return kIdleMhz;
  if ((mask & 0x04) != 0) return kBusyMhz;
  return kRestingMhz;
}

uint32_t allowed_high() {
  const uint8_t mask = allowed_bits();
  if ((mask & 0x04) != 0) return kBusyMhz;
  if ((mask & 0x01) != 0) return kIdleMhz;
  return kRestingMhz;
}
// **La tendència i el governador de càrrega marxen** (0.190.175): thirty samples of two seconds, the mean of one half against
// the other and the two levels were this system guessing what the chip now knows —`esp_pm` mira si hi ha tasques llestes a cada
// canvi de tick i baixa o puja el rellotge ell mateix—. El que el DFS no pot saber és **la intenció**: que una espera de la
// targeta no és feina i que una imatge sí, i això ho continua dient `work_cpu`/`wait_cpu` amb el lock del sostre (docs/50, docs/60).
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

// **L'hora de la placa, que és l'altre rellotge de la quietud** (0.190.77): the hour of the RTC —the chip of the Paper, the `gettimeofday`
// of the core— keeps counting while the chip naps and while it is off, and zero is a board that has never been told the hour (docs/60).
int64_t epoch_now() {
  meter::Scope scope(meter::kClockNow);
  return (g_clock != nullptr) ? g_clock->epoch_ms() : 0;
}

// **Every line this file says to a person goes through the log** (0.179.55): the serial port is a copy of the ring, so a
// line written straight to the port is a line the `log` command never shows —and these are the lines a person wants
// afterwards, the wake the clock made and the sleep that was cut short—. `printf` is still not used, because it is
// buffered and its lines come out in another order than the shell's (measured on the PC, 31/10, docs/30, docs/50).
//
// **I amb la forma de tota la resta del registre** (0.190.289, l'amo: *«També hi ha alguns missatges al log que no estàn en el format del
// sistema»*): these lines went through `log_say`, which keeps the words and leaves them without the `[I][TAG]` prefix, and a log line that is
// not shaped like the others cannot be grepped with them. The words are the same and the prefix is `[I][POWER]`, o sigui que el `power: `
// del començament ja no cal —la etiqueta el diu— (docs/30, docs/50).
void sayf(const char* format, ...) {
  char line[128];
  va_list args;
  va_start(args, format);
  const int written = vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  if (written <= 0) return;
  size_t len = strlen(line);
  while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n')) line[--len] = '\0';
  log_printf(pal::kLogInfo, "POWER", "%s", line);
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
    write_text(kDetachedKey, nullptr);
    return;
  }
  write_text(kAppKey, id);
  write_text(kPathKey, luabind::app_path());
  // **La consola viatja amb la sessió** (0.190.278): two letters and not a flag, because what a board that restarts reads is text.
  write_text(kDetachedKey, shell::app_detached() ? "on" : "off");
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
    sayf("woke on the %s, %+lld ms from the hour it was given", cause,
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
    sayf("the clock woke the board early to arm again, %+lld ms to go",
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
    sayf("%s woke the board, %lld ms before the hour it was given", how,
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
  sayf("the sleep was cut short by %s, %lld ms before the hour it was given", how,
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

// **El rang de velocitat, dit al xip** (0.190.175, l'amo: *«la cpu es pot escalar sola a esp_idf... activa la opció i adapta el
// que tenim fet manualment d'scaling»*): with the automatic scaling on, the floor and the ceiling are the lowest and the highest
// step the person allowed and the chip moves between them by its own load; with it off, the two are the speed the person chose.
// **The chip keeps the tick and the PLLs in step when it moves**, i els drivers del vidre (SPI), del tàctil (I2C), del panell de la
// T5S3 (I2S) i de la targeta (SDMMC) agafen el seu lock d'APB durant la transacció, o sigui que el que abans es trencava pel canvi de
// baud i pel tick ja no es trenca (docs/50, docs/60).
//
// **I la línia del port encara s'buida abans**, perquè els bytes que ja són al FIFO surten al ritme d'abans: el motiu de 0.179.43 no
// ha desaparegut, només ha deixat de ser nostre (docs/92).
void apply_the_range(const char* why) {
  if (g_power == nullptr) return;
  uint32_t low = g_cpu_rest_mhz;
  uint32_t high = g_cpu_rest_mhz;
  if (policy_is_auto()) {
    low = allowed_low();
    high = allowed_high();
  }
  if (low == g_range_low && high == g_range_high) return;
  empty_the_line();
  const uint32_t before = g_power->cpu_mhz();
  if (!g_power->set_cpu_range(low, high)) {
    log_printf(pal::kLogWarn, "POWER", "the board did not take %u to %u MHz (%s), it runs at %u", static_cast<unsigned>(low),
               static_cast<unsigned>(high), (why != nullptr) ? why : "?", static_cast<unsigned>(before));
    return;
  }
  g_range_low = low;
  g_range_high = high;
  log_printf(pal::kLogInfo, "POWER", "cpu %u to %u MHz, at %u now (%s)", static_cast<unsigned>(low), static_cast<unsigned>(high),
             static_cast<unsigned>(g_power->cpu_mhz()), (why != nullptr) ? why : "?");
}

// **Les tres velocitats que la política pot fer servir** (0.189.107): the mask is the person's ceiling and `allowed_step` is
// the one arithmetic that decides which step a request lands on. It is here, just before `move_to`, because that is the
// function that calls it for every move of this module (docs/50, docs/60).
bool can_sleep_op(uint8_t mode) { return can_sleep(mode_of(mode)); }

bool sleep_op(uint8_t mode, int64_t at_epoch_ms) { return request_sleep(mode_of(mode), at_epoch_ms); }

// **La darrera dormida, dita una sola vegada i per un sol camí** (0.190.152): the board is the one that knows which doors were armed
// and which one was already open when the chip went down, and the shop is the only memory that survives the power button. `keep_it`
// is true at the boot —the text is fresh and has to be stored before anything else resets the diagnosis— and false for `power last`,
// which must not write the shop for a question (docs/50, docs/60, docs/70).
bool say_the_last_sleep_of_the_board(bool keep_it) {
  if (g_power != nullptr) {
    char note[192] = {0};
    if (g_power->describe_the_last_sleep(note, sizeof(note)) > 0 && note[0] != '\0') {
      log_printf(pal::kLogInfo, "POWER", "%s", note);
      if (keep_it) cfg::keep_the_last_sleep(note);
      return true;
    }
  }
  const char* stored = cfg::the_last_sleep();
  if (stored != nullptr && stored[0] != '\0') {
    log_printf(pal::kLogInfo, "POWER", "%s", stored);
    return true;
  }
  return false;
}

}  // namespace

// **Les tres velocitats i el pas on cau una demanda** (0.189.107): the mask is the person's ceiling and `allowed_step` is the
// one arithmetic that decides which step a request lands on —the smallest allowed step that is not under what was asked,
// or the biggest allowed one when nothing is above it— (docs/50, docs/60).
void set_allowed_steps(uint8_t mask) {
  // **El terra i el sostre que algú tria en una placa que no escala no existeixen** (0.190.227): el rang és un sol valor i canviar-li el mask
  // només serviria perquè la pàgina ensenyés una cosa que el xip no fa (docs/50, docs/60).
  if (g_fixed_clock) return;
  g_cpu_policy = static_cast<uint8_t>((g_cpu_policy & ~kAllowedMask) | ((mask == 0) ? kAllowedMask : (mask & kAllowedMask)));
  // **I el rang nou es diu al xip ara mateix**: a ceiling that only applies to the next move would leave the board at a step the
  // person has just forbidden, which is the one thing this setting is for (docs/50, docs/60).
  apply_the_range("the steps the person allows");
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
  g_idle_fact_said = nullptr;
  // **Un boot torna a la seva política** (0.179.38): the resting step is the system's again and no window of the boot
  // before is owed, or a board that reboots would carry the two hundred and forty of the last app it ran.
  //
  // **I el que una persona va triar es torna a posar** (0.190.85): `Speed:` i el commutador de l'escala automàtica viuen al
  // magatzem des d'avui, i una arrencada els ha de respectar; si no hi són, mana el que hi havia (escala encesa, pas de repòs). El
  // que no pot passar és el que passava: apagar l'escala, reiniciar, i trobar-se la placa a 240 per una pàgina de text (docs/50).
  g_cpu_rest_mhz = kRestingMhz;
  g_range_low = 0;
  g_range_high = 0;
  // **Una placa sense DFS no llegeix ni la velocitat ni la política** (0.190.227): el que hi hagi desat és el que la persona va triar en una
  // placa que sí que escala, i aquí no hi ha res a triar —el rellotge va al cim i s'hi queda—. El que no es fa és esborrar el que hi ha desat,
  // per si la mateixa targeta NVS acaba en una placa que sí que escala (docs/50, docs/60).
  g_fixed_clock = (g_power != nullptr) && !g_power->scales_the_clock();
  if (g_fixed_clock) {
    g_cpu_rest_mhz = kBusyMhz;
    set_policy(false);
    log_printf(pal::kLogInfo, "POWER",
               "this board does not scale its clock: the cpu is always at %u MHz, and the light sleep is untouched",
               static_cast<unsigned>(kBusyMhz));
  } else {
    char kept[8] = {0};
    if (read_in(kNs, kCpuSpeedKey, kept, sizeof(kept))) {
      const unsigned speed = static_cast<unsigned>(atoi(kept));
      if (speed == kIdleMhz || speed == kRestingMhz || speed == kBusyMhz) g_cpu_rest_mhz = speed;
    }
    char scaling[8] = {0};
    const bool it_was_said = read_in(kNs, kCpuAutoKey, scaling, sizeof(scaling));
    const bool scaling_on = !it_was_said || strcmp(scaling, "off") != 0;
    set_policy(scaling_on);
  }
  // **I el rang es diu al xip a l'arrencada** (0.190.175): with the scaling on it is the floor and the ceiling the person allows
  // and the chip starts moving; with it off it is one step, because a clock of an ESP32-S3 boots at 240 i no n'hi ha prou de
  // deixar de pujar-lo: s'hi ha de baixar (docs/50, docs/60).
  g_range_low = 0;
  g_range_high = 0;
  apply_the_range(g_fixed_clock ? "this board does not scale its clock"
                               : (policy_is_auto() ? "the boot, the steps a person allows" : "the speed a person chose"));
  // **El comptador de la quietud arrenca amb aquesta arrencada** (0.190.9): nothing has been typed at it yet, and the person
  // who chose five minutes gets five minutes from the boot and not from the first turn that happened to look. **I una arrencada no és
  // una mà** (0.190.18): the flag is what tells «nobody has touched it since it came back» —the wake for an alarm and the stage of a long
  // wait go straight down because of it— from «somebody is using it», which is the counter (docs/60).
  g_last_activity_ms = mono_now();
  g_last_activity_epoch_ms = epoch_now();
  g_last_hand = Hand::kBoot;
  g_last_hand_ms = g_last_activity_ms;
  // A stage of a longer wait belongs to one boot: `report_wake()` is what sets it again
  // (docs/60).
  g_coming_back_at = 0;
  g_for_an_alarm = for_an_alarm;
  // **I la porta del dit, que és un ajust i no una política** (0.190.16): the setting of the Battery tab is pushed into the board at every
  // boot, because the board is built before the settings are read (docs/30, docs/50).
  if (g_power != nullptr) g_power->set_wake_on_touch(cfg::wake_on_touch());
  // **I la consola, que és el mateix camí** (0.190.147): el commutador de la pestanya Battery es llegeix aquí i baixa a la placa abans
  // del primer torn, com el del vidre (docs/30, docs/50).
  if (g_power != nullptr) g_power->set_wake_from_uart(cfg::uart_wake());
  // **I si la placa no té la consola per porta, el commutador no hi té res a fer** (0.190.152): a switch that promises something the wiring
  // cannot do is worse than no switch, perquè la persona el mou i no passa res (docs/30, docs/50).
  if (g_power != nullptr && cfg::uart_wake() && !g_power->the_console_can_wake()) {
    log_printf(pal::kLogInfo, "POWER",
               "the console of this board is not a door of a nap: the switch has nothing to arm here");
  }
  // **I per què s'ha reiniciat el xip, que ningú no ho preguntava** (0.190.150): `IPower::reset_cause()` va arribar a la 0.190.68 perquè
  // una placa distingeixi un pànicon d'una caiguda de tensió i d'un despertar de debò —`esp_reset_reason()` és un registre i no una opinió—
  // i no hi havia ni una crida: una placa que es reiniciava sola deia «el corrent ha tornat» i s'acabava la investigació. Aquesta línia és
  // la que diu si el problema és el codi o l'alimentació (l'amo, amb una dormida tallada 325 s abans de l'hora: *«No ho trolunok>»*).
  log_printf(pal::kLogInfo, "POWER", "this boot is %s", hal::reset_cause_word(hal::platform().reset_cause()));
  // **I el que la placa ha de dir d'una dormida que es va menjar el registre** (0.190.149, l'amo: *«quan entra en sleep perdo el serial
  // ... no podrè veure mai el log»*): a deep sleep is a reset, so the line that says which door was already open dies with the boot that
  // armed it. It is said here, before anything else can write over it, and it is **handed to the shop** (0.190.152) because the power
  // button of this board takes the supply away and the RTC RAM goes with it (l'amo: *«el botó de power fa un reset i tot s'envà a la
  // merda»*): `power last` has to answer after that, and only a stored line can (docs/50, docs/60).
  say_the_last_sleep_of_the_board(true);
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
    ops.battery_detail = &battery_detail;
    ops.charger_detail = &charger_detail;
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
    // **Una alarma que ja toca no és una hora** (0.190.77): an alarm whose time has come is delivered in this same turn —`deliver_alarms`
    // runs before the policy— o sigui que la que encara és a la cua amb l'hora passada és una promesa sense amo, i prendre-la per a l'hora
    // de la baixada deixava la política mirant un instant que ja no arribarà: `hour - now <= 0`, que vol dir «l'alarma és a tocar», i la
    // placa no s'adorm mai. És el mateix parany que el de la 0.190.14 i es tanca igual: només compta el que és al futur (docs/60).
    if (next > g_clock->monotonic_ms()) {
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

// **I la pregunta d'una persona, que no escriu res** (0.190.152): `power last` has to answer after the power button has emptied the
// RTC RAM, so the stored line is the answer and the board's own is only the fresher one (docs/50, docs/60).
void say_the_last_sleep() {
  // **I una pregunta es contesta sempre** (0.190.152): a board that has never been to a deep sleep has nothing to say, but a prompt that
  // answers with silence is indistinguishable from a command that does not exist.
  if (!say_the_last_sleep_of_the_board(false)) {
    log_printf(pal::kLogInfo, "POWER", "no sleep has been recorded on this board yet");
  }
}

bool wake_from_uart() { return (g_power != nullptr) ? g_power->wake_from_uart() : false; }
bool set_wake_from_uart(bool on) { return (g_power != nullptr) ? g_power->set_wake_from_uart(on) : false; }

bool can_wake_on_touch() { return (g_power != nullptr) && g_power->can_wake_on_touch(); }

// ---------------------------------------------------------------- la becaina

// **L'escala de la becaina ja no hi és** (0.190.181, l'amo veient el registre de l'ATOM: *«No haviem quedat que aixó era cosa del
// pasat? […] Que ara esp_idf gestionava el lightSleep?»* — i tenia raó). La 0.190.63 va néixer amb una escala —quaranta mil·lisegons i
// doblar fins a un segon— perquè llavors era el nucli qui demanava la dormida i havia de triar-ne la durada. Des de la 0.190.178 la
// dormida la fa la gestió de potència del xip dins del `pop`, i la durada la donen dues coses que no són aquesta escala: **el `wait`
// del torn** i el proper temporitzador que la IDF mira. O sigui que l'escala no decidia res i continuava pujant, i la seva línia del
// sostre deia «mil mil·lisegons per torn» en una placa que fa voltes de quaranta: **una mentida que el registre de l'amo va ensenyar
// en dues línies seguides** (163 becaines, 5026 ms, i el sostre dient mil) (docs/60, docs/95).
// **El que pot esperar un dit** (0.190.74): quaranta mil·lisegons és el que no es nota, tant si el vidre desperta el xip com si el contacte
// es llegeix al torn següent. El torn del nucli no espera més que això mentre la placa dorma entre voltes (docs/50, docs/95).
const int32_t kNapSliceMs = 40;
// **I amb una persona a la consola no es dorm** (0.190.75, l'amo: *«El port sèrie és inutilitzable si fa LightSleeps… Si el port sèrie està
// en ús no usar lightSleep»*). Un son lleuger atura el UART i el desperta el primer byte: els que arriben mentre el xip es lleva es perden,
// i una línia escrita a mà arriba tallada. Deu segons de silenci a la línia volen dir que no hi ha ningú escrivint; per sota d'això el que
// mana és el port (docs/50, docs/95).
const int64_t kConsoleInUseMs = 10000;
#if defined(UNIT_TEST) || defined(PIO_UNIT_TESTING)
// **Els deu segons, moguts per una prova** (0.190.77): a case that had to wait them would be a suite ten seconds slower for a nap that
// the case is not about, i el que es mira aquí és el comptador de la quietud amb becaines de debò (docs/60, docs/80).
int64_t g_console_in_use_ms = kConsoleInUseMs;
#endif

// **El que la placa ha de tenir de quietud per fer res** (0.190.75, 0.190.77): deu segons sense ningú a la consola és el que diu que el
// port no s'està fent servir, i és també la quietud que ja val la pena dir quan una mà la trenca.
int64_t console_gate_ms() {
#if defined(UNIT_TEST) || defined(PIO_UNIT_TESTING)
  return g_console_in_use_ms;
#else
  return kConsoleInUseMs;
#endif
}

// El que s'ha de poder dormir perquè valgui la pena entrar-hi: per sota, el camí de sortida costa més que l'espera.
const int32_t kNapLeastMs = 20;

bool g_nap_on = true;     // encès de sèrie, que és el que demana el commutador

// **Quantes becaines seguides i quants mil·lisegons s'hi ha estat** (0.190.64, l'amo: *«al log es veu quan hi ha light sleep?»*):
// el registre és l'únic instrument que hi ha per saber si la placa dorm de debò, i una línia per becaina ompliria el registre
// del sistema a raó d'una per segon. El que es compta és la tanda, i s'escriu **quan s'acaba** —quan una mà, una tecla o la
// consola tornen el xip— i quan l'escala arriba al sostre (docs/60, docs/75, docs/95).
uint32_t g_nap_run = 0;
int64_t g_nap_run_ms = 0;
// **I cada deu segons de son es diu sol** (0.190.71): la línia de la tanda només sortia quan una mà la tancava, o sigui que una placa
// quieta —la que dorm de debò— no en deia mai cap. El que es compara és el comptador amb l'últim que es va dir, perquè la línia
// surti cada deu segons i no a cada becaina (docs/60, docs/95).
int64_t g_nap_said_ms = 0;
const int64_t kNapSayEveryMs = 5000;
// **Si el darrer `nap` ha estat un son de debò** (0.190.70): l'escala de la becaina només ha de créixer quan el xip ha dormit, i el
// que la feia créixer era el torn i no el son —la línia del sostre deia «mil mil·lisegons per torn» en una placa que no havia dormit
// mai— (docs/95).
bool g_napped = false;
// **I si el pany del torn és obert** (0.190.178): el que separa «aquesta volta s'ha deixat dormir el xip» de «aquesta volta s'ha
// esperat despert», que és la diferència entre comptar una becaina i comptar una espera (docs/50, docs/95).
bool g_sleep_is_open = false;
// El que s'havia demanat, per saber si la becaina s'ha acabat abans d'hora i qui l'ha tallada (0.190.178) (docs/95).
int64_t g_sleep_asked_ms = 0;
// **I un dit al vidre, que mana sobre la becaina i sobre el governador** (0.190.87): mentres és cert no es dorma i la velocitat no
// baixa; el dit el lleva quan s'aixeca (docs/30, docs/50).
bool g_a_finger_down = false;
const int32_t kNapRunSayMs = 1000;  // una tanda d'un segon ja diu alguna cosa; per sota, no val una línia

bool nap_on() { return g_nap_on; }

bool set_nap_on(bool on) {
  g_nap_on = on;
  return true;
}

bool can_nap() { return (g_power != nullptr) && g_power->can_nap(); }

int32_t nap_slice_ms() { return kNapSliceMs; }

#if defined(UNIT_TEST) || defined(PIO_UNIT_TESTING)
void set_console_in_use_ms(int64_t ms) { g_console_in_use_ms = (ms < 0) ? 0 : ms; }
#endif
int64_t console_in_use_ms() { return console_gate_ms(); }


// **Una becaina demanada des del prompt** (0.190.71): the ladder and the switch are for the turn, and what a person wants to see is one
// sleep with a number —how long it lasted and what brought the chip back—. És l'instrument que diu si la tecla o el vidre desperten un
// son lleuger en aquesta placa, que és el que decideix si la volta pot esperar-lo (docs/50, docs/95).
bool nap_now(int32_t ms) {
  if (g_power == nullptr) return false;
  g_napped = g_power->nap(ms);
  if (g_napped) {
    ++g_nap_run;
    g_nap_run_ms += ms;
  }
  return g_napped;
}

// **Quant pot durar l'espera d'un torn**, que és el que aquesta funció responia quan la durada la triava el nucli: ara és la meitat que
// no nota un dit (`nap_slice_ms`) i el que la placa demana a `open_the_sleep`, o sigui que la resposta és aquesta i no una escala
// (docs/60).
int32_t nap_ms() { return kNapSliceMs; }

bool nap(int32_t wait_ms) {
  g_napped = false;
  if (!g_nap_on || g_power == nullptr || wait_ms < kNapLeastMs) return false;
  // **Amb un dit a sobre no es dorma** (0.190.87): the chip stops, the line of the glass and the scan stop with it, and a frame
  // read that is lost under a finger is a tap that never arrives —or two that do— (docs/30, docs/50).
  if (g_a_finger_down) return false;
  // **I si el port sèrie s'està fent servir, no es dorm** (0.190.75): una becaina atura el UART i el primer byte la desperta, però els que
  // arriben mentre el xip es lleva es perden — una línia escrita a mà surt tallada i el port és inutilitzable. Deu segons de silenci volen
  // dir que no hi ha ningú; i un port cedit a una app no dorma mai (docs/30, docs/50, docs/95).
  if (serial::claimed()) return false;
  if (quiet_ms() < console_gate_ms()) return false;
  // **La becaina dura el que qui espera ha demanat**, ni més ni menys: el temporitzador del xip és el que ha de tornar la placa, i una
  // espera curta no s'ha de convertir en una de llarga (docs/50, docs/60).
  if (!g_power->nap(wait_ms)) return false;
  g_napped = true;
  ++g_nap_run;
  g_nap_run_ms += wait_ms;
  return true;
}

// **El pany del torn, i el xip que dorma sol** (0.190.178, l'amo: *«activem el lightsleep automàtic d'idf»*): les preguntes són les de
// sempre —el commutador, el dit, el port cedit, els deu segons de silenci de la consola i una placa que en sàpiga—, però el que es fa
// amb la resposta no és dormir: és **obrir les portes de la placa i marxar a la cua**. La gestió de potència del xip mira el pany abans
// de cada son i, si és obert, atura els rellotges fins al proper despertar, compensant el tick que el son s'ha menjat (docs/50, docs/60).
//
// **I això protegeix el que abans s'havia de protegir a mà**: la tinta d'una pintada, un port que no ha callat prou i una app amb el port
// cedit dormen fora del torn, quan el pany és tancat, o sigui que no hi ha cap lloc del sistema que hagi de saber que el son existeix
// (docs/50, docs/95).
int32_t open_the_sleep(int32_t wait_ms) {
  g_napped = false;
  g_sleep_is_open = false;
  g_sleep_asked_ms = 0;
  if (!g_nap_on || g_power == nullptr || wait_ms < kNapLeastMs) return 0;
  // **I el ràdio mana sobre la becaina** (0.190.179): un son lleuger atura els rellotges que viu el link del teclat de Bluetooth, o
  // sigui que mentre és engegat la placa espera desperta i ho diu el ràdio, no una llista de condicions d'aquí (docs/50, docs/60).
  {
    hal::IBluetooth* radio = hal::platform().bluetooth();
    if (radio != nullptr && radio->wants_the_chip_awake()) return 0;
  }
  if (g_a_finger_down) return 0;
  if (serial::claimed()) return 0;
  if (quiet_ms() < console_gate_ms()) return 0;
  if (!can_nap()) return 0;
  // **El temps el dona qui espera** (0.190.181): el `pop` del torn ja ve retallat pel que un dit no nota i pel que falta per la propera
  // alarma, i la gestió de potència el torna a retallar contra el seu temporitzador. Una espera curta no s'ha de convertir en una de
  // llarga i una de llarga no s'ha de convertir en una de curta (docs/50, docs/60).
  const int32_t asked = wait_ms;
  if (!g_power->open_the_nap_doors()) {
    // **Una placa que no adorm el xip sola encara pot demanar la becaina** (0.190.178): el pany és la porta de la dormida automàtica, i una
    // placa que no en té —l'emulador, una banda sense el tickless, un xip que ha dit que no— fa la becaina com l'ha feta sempre, demanada i
    // esperada aquí mateix. El que torna és zero perquè el temps ja s'ha gastat, i el que qui espera llegeix a la cua és el que hagi passat
    // mentre dormia (docs/50, docs/85).
    if (!g_power->nap(asked)) return 0;
    g_napped = true;
    ++g_nap_run;
    g_nap_run_ms += asked;
    return 0;
  }
  g_sleep_is_open = true;
  g_sleep_asked_ms = asked;
  return asked;
}

void close_the_sleep(int64_t waited_ms) {
  if (!g_sleep_is_open) return;
  g_sleep_is_open = false;
  g_power->close_the_nap_doors();
  // Una espera que no arriba al mínim no ha dormit ningú, i comptar-la seria fer créixer l'escala d'una placa que no ha dormit mai
  // (0.190.70) (docs/95).
  if (waited_ms < kNapLeastMs) return;
  // **I una becaina que no ha acabat el seu temps també es diu** (0.190.64, 0.190.178): la que el temporitzador ha deixat anar és el cas
  // normal i no omple el registre, però la que ha tallat una tecla, el dit o la consola és la línia que explica per què la placa és
  // desperta abans d'hora —i el toc que sembla lent—. El motiu el contesta la placa, que és qui l'ha viscut (docs/50, docs/95).
  if (waited_ms + 5 < g_sleep_asked_ms) {
    const char* why = g_power->the_word_of_the_last_nap();
    if (why != nullptr) {
      log_printf(pal::kLogDebug, "POWER", "a nap of %lld ms ended by %s", static_cast<long long>(waited_ms), why);
    }
  }
  g_napped = true;
  ++g_nap_run;
  g_nap_run_ms += waited_ms;
}

// **La tanda que s'acaba es diu** (0.190.64): quan una mà, una tecla o la consola tornen el xip, el registre escriu quantes
// becaines seguides hi ha hagut i quants mil·lisegons ha estat adormida la placa. És el número que una persona mira a la placa
// per saber si el commutador fa alguna cosa, i per sota d'un segon no val una línia (docs/60, docs/95).
void note_the_nap_end(bool something_happened) {
  if (something_happened) {
    if (g_nap_run_ms >= kNapRunSayMs) {
      log_printf(pal::kLogDebug, "POWER", "%u light sleep(s) in a row, %lld ms in all, and now something has happened",
                 static_cast<unsigned>(g_nap_run), static_cast<long long>(g_nap_run_ms));
    }
    g_nap_run = 0;
    g_nap_run_ms = 0;
    g_nap_said_ms = 0;
    return;
  }
  if (!g_napped) return;
  // **I una tanda llarga de dormides es diu sola** (0.190.71, l'amo: *«CONTINUO SENSE VEURE AVISOS DE LIGHTSLEEP»*): fins ara l'única
  // línia d'una tanda sortia quan passava alguna cosa, i una placa quieta —que és la que dorm— no ho feia mai. Cada cinc segons de son es
  // diu quant n'hi ha hagut i en quantes dormides, que és el que converteix «no en veig cap» en un número (docs/95).
  //
  // **I la línia diu el que ha passat i no una escala** (0.190.181): el xip s'ha aturat `g_nap_run` cops i el torn ha estat esperant
  // `g_nap_run_ms` mil·lisegons amb el pany obert. El número és del torn i no del rellotge del xip —el que compta és el temps que el
  // nucli ha passat a la cua—, o sigui que «en total» i no «dormit» (docs/60, docs/95).
  if (g_nap_run_ms - g_nap_said_ms >= kNapSayEveryMs) {
    g_nap_said_ms = g_nap_run_ms;
    /*
    log_printf(pal::kLogDebug, "POWER", "%u light sleep(s), %lld ms in all, and nothing has happened",
               static_cast<unsigned>(g_nap_run), static_cast<long long>(g_nap_run_ms));
    */
  }
}

bool can_deep_sleep() {
  if (g_power == nullptr || !can_sleep(hal::SleepMode::kDeep)) return false;
  if (g_power->can_sleep_with_the_keys()) return true;
  // **I el dit, si és una porta i una persona l'ha demanat** (0.190.105): a board that keeps its touch controller powered while the
  // chip sleeps comes back with a finger on the glass, and that is a third way back —the one the T5 E-Paper S3 Pro has, because its
  // only button cannot be a door of a deep sleep— (docs/50, docs/60).
  if (g_power->can_wake_on_touch() && g_power->wake_on_touch()) return true;
  return the_hour_of_a_sleep(0) > 0;
}

const char* hand_name(Hand hand) {
  switch (hand) {
    case Hand::kBoot:
      return "the boot";
    case Hand::kConsole:
      return "a byte at the console";
    case Hand::kKey:
      return "a key of the dial";
    case Hand::kGlass:
      return "a finger on the glass";
    case Hand::kPolicy:
      return "the policy itself";
  }
  return "something";
}

// **Cada mà que mou el comptador deixa el seu nom i quants segons hi havia** (0.190.77): a countdown that starts again is the one thing
// that can keep a board awake for ever, o sigui que **es diu**. Es diu quan la quietud que es perd ja era llarga (deu segons, que és el que
// la placa ha de tenir per dormir una becaina) i com a molt un cop per mà i minut, perquè una línia a cada toc no la llegeix ningú
// (docs/60, docs/95).
void note_activity_with(Hand hand) {
  const int64_t was = quiet_ms();
  const int64_t now = mono_now();
  // Un cop per mà i minut: la línia només serveix si es pot llegir, i una placa quieta pot rebre una mà cada deu mil·lisegons. El
  // «ja s'ha dit» és una bandera i no un zero, perquè una placa que acaba d'arrencar té el rellotge a zero i la primera mà ha de sortir
  // igual (docs/60, docs/80).
  static bool g_said[5] = {false, false, false, false, false};
  static int64_t g_said_ms[5] = {0, 0, 0, 0, 0};
  const size_t at = static_cast<size_t>(hand);
  if (was >= console_gate_ms() && at < 5 && (!g_said[at] || (now - g_said_ms[at]) >= 60000)) {
    g_said[at] = true;
    g_said_ms[at] = now;
    log_printf(pal::kLogInfo, "POWER", "idle: the quiet starts again: %s (it had %lld s of the %lld the policy asks)",
               hand_name(hand), static_cast<long long>(was / 1000),
               static_cast<long long>(idle_quiet_ms() / 1000));
  }
  g_last_activity_ms = now;
  g_last_activity_epoch_ms = epoch_now();
  g_last_hand = hand;
  g_last_hand_ms = now;
  g_touched_since_boot = true;
}

void note_activity() { note_activity_with(Hand::kPolicy); }
void note_activity(Hand hand) { note_activity_with(hand); }

Hand last_hand() { return g_last_hand; }
int64_t last_hand_ms() { return g_last_hand_ms; }

// **La línia de la quietud** (0.190.77): el número que decideix el son profund, el que la persona va triar, i el nom de l'última mà
// amb l'estona que fa. És el que `power` escriu i el que una prova mira, perquè «no s'adorm» ha de poder ser una frase amb un subjecte
// (docs/30, docs/60).
bool idle_report(char* out, size_t cap) {
  if (out == nullptr || cap == 0) return false;
  const int64_t quiet = quiet_ms();
  const int64_t now = mono_now();
  const int64_t hand_age = (g_last_hand_ms > 0) ? (now - g_last_hand_ms) : -1;
  // **I la propera alarma, que és l'altra meitat de la resposta** (0.190.77): a policy that says no says it because of the hour it
  // would be woken at, o sigui que una línia que només digués la quietud deixaria la meitat del cas sense dir (docs/60).
  char alarm[64] = {0};
  const int64_t next = alarms::next_due_mono_ms();
  if (next >= 0) {
    const int64_t left = next - now;
    snprintf(alarm, sizeof(alarm), ", and the next alarm is %lld s away", static_cast<long long>((left > 0) ? left / 1000 : 0));
  } else {
    snprintf(alarm, sizeof(alarm), ", and no alarm is waiting");
  }
  const int written = snprintf(out, cap, "idle: %lld s of the %lld the policy asks, the last hand was %s %lld s ago%s",
                               static_cast<long long>(quiet / 1000),
                               static_cast<long long>(idle_quiet_ms() / 1000), hand_name(g_last_hand),
                               static_cast<long long>(hand_age / 1000), alarm);
  return written > 0 && static_cast<size_t>(written) < cap;
}

void note_a_page_on_the_glass() {
  g_a_page_on_the_glass = true;
  // **I la pàgina encén la llum** (0.190.161): a wake for an alarm leaves the glass dark until there is something to show, i el
  // moment que hi és és aquest —el que s'ha pintat és una pàgina que algú ha demanat, un `raise` o una finestra que ha dibuixat—,
  // o sigui que la llum l'ha de seguir (docs/30, docs/50).
  led::the_glass_is_wanted();
}

// **Quanta quietud mana** (0.190.9): the setting a person chose in Settings -> Battery —five, ten or fifteen minutes—, i una
// suite que hagués d'esperar-los seria una suite de minuts més lenta, o sigui que on hi ha algú que mogui el número de la prova
// mana el de la prova (docs/60, docs/80).
// **L'única pregunta de la quietud, i amb dos rellotges** (0.190.9, 0.190.77): seconds-ish since a person last did anything, and zero
// for the first turn of a boot that has not stamped itself (which is a board that has just come up and not a board that has been quiet
// for ever).
//
// **El compteig del son profund va ser l'únic que es va quedar amb un sol rellotge**, i el son lleuger el va deixar en dubte: la placa
// del 20/11 va fer 2 600 becaines i el `Sleep when idle` de cinc minuts no va arribar mai (l'amo). El monòton d'aquest xip **sí** que
// avança durant una becaina (`esp_light_sleep_start` crida `esp_timer_private_advance`), però això és una propietat de l'IDF i no
// d'aquest sistema, i una placa no s'ha de quedar desperta perquè una crida de sota hagi canviat de parer. L'hora —el BM8563 de la
// Paper, el `gettimeofday` del nucli— **compta a través de tot**, i quan la placa en té una es fa servir ella. La quietud és **la més
// llarga de les dues**, que és la decisió honesta: una placa només s'ha de quedar desperta si les dues rellotges diuen que hi ha hagut
// una mà, i un que s'atura (o que es corregeix enrere) no allarga mai la vetlla (docs/60, docs/95).
int64_t quiet_ms() {
  const int64_t now = mono_now();
  const int64_t epoch = epoch_now();
  if (g_last_activity_ms <= 0) {
    g_last_activity_ms = now;
    g_last_activity_epoch_ms = epoch;
    g_seen_epoch_ms = epoch;
    g_seen_mono_ms = now;
    return 0;
  }
  const int64_t mono_quiet = now - g_last_activity_ms;
  int64_t quiet = (mono_quiet > 0) ? mono_quiet : 0;
  if (epoch <= 0) return quiet;
  // **Una hora que acaba d'arribar no es pot llegir com una quietud de dècades** (0.190.79, i és el cas de la placa de l'amo): una placa que
  // arrenca sense hora —una pila esgotada, cap xarxa— té la marca de l'hora a zero, i sense això la meitat de l'hora del comptador quedava
  // morta per sempre i el que sobrevivia a una becaina era només el monòton, que en aquest xip no la compta. La marca es posa **continuant
  // la quietud que ja hi havia**, que és el que ha passat de debò (docs/60, docs/95).
  if (g_last_activity_epoch_ms <= 0) {
    g_last_activity_epoch_ms = epoch - quiet;
  } else if (g_seen_epoch_ms > 0) {
    // **I si el que s'ha mogut és el rellotge, el comptador es torna a basar en ell sense perdre el que tenia** (0.190.79).
    const int64_t stepped = (epoch - g_seen_epoch_ms) - (now - g_seen_mono_ms);
    if (stepped > kClockStepMs || stepped < -kClockStepMs) {
      g_last_activity_epoch_ms = epoch - quiet;
    }
  }
  g_seen_epoch_ms = epoch;
  g_seen_mono_ms = now;
  const int64_t epoch_quiet = epoch - g_last_activity_epoch_ms;
  if (epoch_quiet > quiet) quiet = epoch_quiet;
  return quiet;
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
  // **I un commutador que canvia torna a dir la seva** (0.190.88): el que s'ha dit una vegada és el que val per a aquell estat.
  g_idle_fact_said = nullptr;
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
// **I les que no poden canviar en aquest run es diuen una vegada i sense compte enrere** (0.190.88, l'amo: *«si la opció no està
// activada, perqué molestes amb missatges irrellevants?»*). Un commutador apagat o un vidre que no és una baixada no són condicions
// que es puguin complir més tard: comptar-hi els cent cinquanta segons de tres-cents és un número que no porta enlloc, i repetir-lo
// cada minut omple el registre d'una cosa que la placa ja ha dit (docs/30, docs/60).
void say_the_verdict(int64_t quiet, const char* why) {
  const int64_t now = mono_now();
  if (why == g_idle_said && (now - g_idle_said_ms) < 60000) return;
  g_idle_said = why;
  g_idle_said_ms = now;
  log_printf(pal::kLogInfo, "POWER", "idle: %lld s of quiet (the policy asks %lld) and the board stays awake: %s",
             static_cast<long long>(quiet / 1000), static_cast<long long>(idle_quiet_ms() / 1000), why);
}

// **El fet, una vegada, i no a la vista de tothom** (0.190.88, l'amo: *«si la opció no està activada, perqué molestes amb missatges
// irrellevants?»*). The policy is not going to put this board down in this run and that does not depend on how long it has been quiet,
// o sigui que **no és una notícia**: és la resposta a una pregunta que algú fa una vegada. Va a `debug` i es diu quan canvia la
// resposta; el registre normal no la torna a veure mai més (docs/30, docs/60).
void say_the_fact(const char* why) {
  if (why == g_idle_fact_said) return;
  g_idle_fact_said = why;
  log_printf(pal::kLogDebug, "POWER", "idle: this board is not put down by the policy: %s", why);
}

void idle_sleep(int64_t next_alarm_epoch_ms) {
  if (g_power == nullptr || g_clock == nullptr) return;
  // **Les dues portes que callaven** (0.190.77): the switch of the policy and a board whose deep sleep is not a power off were two
  // `return`s with no line, o sigui que una placa amb el commutador apagat —o amb un son profund que no és una baixada— es veia
  // exactament igual que una placa que s'havia d'adormir i no ho feia. Ara diuen el seu nom com les altres (docs/30, docs/60).
  if (!g_idle_allowed) {
    say_the_fact("the switch of the idle sleep is off (Settings, Sleep when idle)");
    return;
  }
  // **I la pregunta és si aquesta placa pot dormir, no com baixa** (0.190.272, l'amo: *«l'M5Paper funciona genial i s'apaga/deepsleep,
  // aquesta versió no sembla fer-ho bé. Tampoc sembla que els temporizadors d'apagar backlight i entrar en deepSleep quan hi ha
  // inactivitat»*). Aquí hi havia `deep_powers_off()`, que diu que la baixada és una apagada de debò —el Paper, amb el rellotge del xip
  // de rellotge engegant-lo—, i la T5 E-Paper S3 Pro hi contestava que no perquè el seu son profund és un son de l'ESP32 i no una
  // apagada: o sigui que **la seva política d'oci tornava aquí a cada torn i no dormia mai**. El que mana és si baixar estalvia la placa
  // i si hi ha una porta per tornar-ne, i les dues coses es pregunten més avall (docs/50, docs/60).
  if (!g_power->can_sleep_on_idle()) {
    say_the_fact("this board's deep sleep is not a way to put it down");
    return;
  }
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
    sayf("nothing to do for %lld s, sleeping until a key", static_cast<long long>(quiet / 1000));
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
  sayf("nothing to do for %lld s, sleeping until the next alarm",
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

// **El cable, anunciat una vegada** (0.190.165, l'amo: *«Afegeix un toggle a Settings->System "Announce charge" (per defecte true) que mostrarà
// l' splash de "charging" quan es detecti que s'endolla. Fins que no es desendolla no es pot tornar a mostrar l'splash charging»*): the charger
// is asked every two seconds while the switch says so, i el que es mira és **el flanc** —la placa que no tenia cable i ara en té— i no l'estat,
// o sigui que la imatge surt una vegada per cable i no a cada torn. La primera lectura del run només recorda com està la placa: un despert amb
// el cable ja posat no és un cable que s'ha endollat (docs/30, docs/50, docs/60).
bool g_cable_known = false;
bool g_cable_has_current = false;
int64_t g_announce_asked_ms = -1;
const int64_t kAnnounceEveryMs = 2000;

void announce_the_cable() {
  if (!cfg::announce_charge()) {
    g_cable_known = false;
    g_announce_asked_ms = -1;
    return;
  }
  const int64_t now = mono_now();
  if (g_cable_known && g_announce_asked_ms >= 0 && (now - g_announce_asked_ms) < kAnnounceEveryMs) return;
  g_announce_asked_ms = now;
  int mv = 0;
  int pct = 0;
  bool charging = false;
  // **Un gauge que no contesta no és un cable que arriba**: el que no s'ha pogut llegir no canvia el que la placa creu (docs/50).
  if (!battery(mv, pct, charging)) return;
  const bool first = !g_cable_known;
  const bool was = g_cable_has_current;
  g_cable_known = true;
  g_cable_has_current = charging;
  if (first || !charging || was) return;
  // **I la imatge, amb la pàgina de la persona al darrere**: the splash paints the whole glass, o sigui que el que hi havia es torna a deure
  // sencer i el torn del nucli el paga (`ui::owes_paint` → `ui::idle`), perquè una app quieta no ha de fer res per tornar a la seva pàgina (docs/30).
  if (splash::show(splash::kCharging)) {
    ui::owe_the_page();
    log_printf(pal::kLogInfo, "POWER", "the cable is in: the glass says the charge");
  } else {
    log_printf(pal::kLogDebug, "SPLASH", "no picture of the charge: %s", splash::last_error());
  }
}

void poll(int64_t next_alarm_epoch_ms) {
  // **Aquí hi havia la finestra i el governador** (0.179.38, 0.189.95): a heavy piece of work took the top step for a few
  // seconds and the load of a minute decided the next one. Des de la 0.190.175 el xip compta els que volen el sostre (el seu
  // lock) i decideix ell mateix la freqüència entre el terra i el sostre, o sigui que aquest torn no té res a dir de la
  // velocitat (docs/50, docs/60).
  // **I el cable, que és una foto i no una llum** (0.190.165): two seconds of lag on a cable that a person has just put in, i el preu d'aquesta
  // línia és una lectura del gauge cada dos segons i només amb el commutador encès (docs/30, docs/50).
  announce_the_cable();
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

// **El que el comptador de coulombs sap de més** (0.190.155): the current and the capacities, which the page of the battery asks
// for when it draws the volts and the cable. A board without a part that counts answers false and the page says nothing, que és
// el que ha de fer una placa que no ho sap (docs/30, docs/50).
bool battery_detail(hal::IPower::BatteryDetail& out) {
  meter::Scope scope(meter::kPowerBattery);
  if (g_power == nullptr) return false;
  return g_power->battery_detail(out);
}

// **I el carregador, que és el mateix bus i la mateixa pregunta** (0.190.156): what the cable is and what is going into the cell.
bool charger_detail(hal::IPower::ChargerDetail& out) {
  meter::Scope scope(meter::kPowerBattery);
  if (g_power == nullptr) return false;
  return g_power->charger_detail(out);
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

// **El que la placa ha contestat a l'arrencada** (0.190.227): un run no canvia de xip, o sigui que aquesta pregunta es fa una vegada i la
// resta del sistema la llegeix d'aquí (docs/50, docs/60).
bool scales_the_clock() { return !g_fixed_clock; }

// **El dit, dit per qui el veu** (0.190.87): `touch::poll` és l'únic que sap si hi ha un contacte, i aquest és el lloc on es tradueix
// a les dues decisions que en depenen —la becaina i la velocitat— perquè el nucli no hagi de preguntar-ho a cada volta (docs/30).
void note_a_finger(bool down) {
  if (down == g_a_finger_down) return;
  g_a_finger_down = down;
  if (down) {
    // Un dit és una feina que una persona espera: el cim, i sense finestra, perquè el que el lleva és el dit i no el rellotge.
    hold_cpu(kBusyMhz, 0, "a finger on the glass");
    return;
  }
  rest_cpu("the finger is up");
}

bool a_finger_is_down() { return g_a_finger_down; }

void set_cpu_auto(bool on) {
  // **I en una placa sense DFS el commutador no fa res, i ho diu** (0.190.227): el rellotge hi és sempre al cim, o sigui que encendre-hi
  // l'escala automàtica seria un interruptor que es mou i no canvia res (docs/50, docs/60).
  if (g_fixed_clock) {
    log_printf(pal::kLogInfo, "POWER", "this board does not scale its clock: the switch changes nothing (cpu %u MHz)",
               static_cast<unsigned>(kBusyMhz));
    return;
  }
  set_policy(on);
  // **I es desa** (0.190.85): una decisió que no sobreviu a un reinici no és una decisió, és una casualitat del run.
  write_in(kNs, kCpuAutoKey, on ? "on" : "off");
  // **Tornar-la a encendre és una decisió que es pren ara i no al proper torn** (0.190.175): the range is said to the chip in the
  // same line that turns the policy on, or a person would see the answer of a policy that has not run (docs/60).
  apply_the_range(on ? "the automatic scaling is on" : "the automatic scaling is off");
}

// ---- la velocitat, amb el PM del xip (0.190.175) ----
//
// *«No facis el canvi de freqüència tant sovint... Guarda mostres, durant un minut: si la tendència és a més, puja la CPU; si és a
// menys, la baixes; si és semblant, et quedes com estàs.»* (l'amo, 0.189.95)
//
// **Aquestes tres funcions es van esborrar aquí**: `take_a_sample`, `trend_of`, `trend_step` i `govern_cpu` were this system
// guessing the load of a minute with thirty bytes and two means, i el que han deixat és la part que el xip no pot saber —la
// intenció de qui treballa—, que viu a `hold_cpu`, `work_cpu` i `wait_cpu` amb el lock del sostre (docs/50, docs/60).

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
  // **I aquí no hi ha passos on triar** (0.190.227): una placa que no escala va sempre al cim, i demanar-li vuitanta no és un número que no
  // faci, és una cosa que no existeix en aquesta placa (docs/50, docs/60).
  if (g_fixed_clock && mhz != kBusyMhz) return false;
  // **I des de la 0.190.175 no s'acota al mask**: `Speed:` i `power cpu N` són una decisió de la persona sobre el terra i el
  // sostre alhora (un rang d'un sol valor), i el mask només mana quan l'escala automàtica és encesa —on el que hi ha és un terra i
  // un sostre i no tres passos— (docs/50, docs/60).
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
  g_range_low = mhz;
  g_range_high = mhz;
  // **I el pas triat també es desa** (0.190.85): `power cpu 160` o la fila `Speed:` de Settings són una elecció de la persona i
  // la placa ha d'arrengar-se-hi la propera vegada (docs/50, docs/60).
  {
    char number[8];
    snprintf(number, sizeof(number), "%u", static_cast<unsigned>(mhz));
    write_in(kNs, kCpuSpeedKey, number);
  }
  // **Un pas demanat a mà para la política automàtica** (0.189.94): a person who fixed a step did it to measure something,
  // and a governor that undoes it two hundred and fifty milliseconds later is a governor nobody can measure against.
  set_policy(false);
  log_printf(pal::kLogInfo, "POWER", "cpu %u MHz from %u (asked, the automatic scaling is off)", static_cast<unsigned>(g_power->cpu_mhz()),
             static_cast<unsigned>(before));
  return true;
}

// **Demanar el sostre i deixar-lo anar** (0.190.175): `mhz` is the step the caller has in mind and the chip only has two —the
// floor and the ceiling of the range—, so a request at or above the top takes the lock of `ESP_PM_CPU_FREQ_MAX` and a request at
// or below the bottom gives it back. **`hold_ms` ja no fa res**: the lock counts its takes, i una finestra que s'acabés en un torn
// del nucli és el que el xip ja sap fer sol (docs/50, docs/60).
void hold_cpu(uint32_t mhz, int32_t hold_ms, const char* why) {
  (void)hold_ms;
  if (g_power == nullptr || mhz < 80 || mhz > 240) return;
  // **I amb l'escala apagada, una empenta no mou la placa** (0.190.85): `hold_cpu` era l'única porta de la velocitat que no
  // preguntava res, i per ella hi passaven l'arrencada (`kernel::boot`) i l'inici d'una app —o sigui que amb l'escala apagada la
  // CPU pujava a 240 igualment i el missatge del log era la resposta d'un camí que havia de callar—. El que mana una persona amb
  // `Speed:` és la velocitat i prou; el que ve després es queda allà on ella l'ha posat (docs/50, docs/60).
  if (!policy_is_auto()) return;
  const bool top = (mhz >= kBusyMhz);
  const uint32_t before = g_power->cpu_mhz();
  if (!g_power->hold_cpu_top(top)) return;
  const uint32_t now = g_power->cpu_mhz();
  // **La línia és la mateixa d'abans i només surt quan el número s'ha mogut**: a board whose speed changed without a line is a board
  // nobody can measure, and a line that says the same number says nothing (docs/50, docs/60).
  if (now != before) {
    log_printf(pal::kLogInfo, "POWER", "cpu %u MHz from %u (%s)", static_cast<unsigned>(now), static_cast<unsigned>(before),
               (why != nullptr) ? why : "?");
  }
}

void rest_cpu(const char* why) {
  if (g_power == nullptr) return;
  // **I un dit a sobre no baixa la velocitat** (0.190.87): a finger held still on a panel that only reports a change keeps the top
  // without asking for it again, which is the lock's own count and no longer a window that has to be renewed (docs/30, docs/50).
  if (g_a_finger_down) return;
  if (!policy_is_auto()) return;
  const uint32_t before = g_power->cpu_mhz();
  if (!g_power->hold_cpu_top(false)) return;
  const uint32_t now = g_power->cpu_mhz();
  if (now != before) {
    log_printf(pal::kLogInfo, "POWER", "cpu %u MHz from %u (%s)", static_cast<unsigned>(now), static_cast<unsigned>(before),
               (why != nullptr) ? why : "?");
  }
}

// **Una feina vol el cim** (0.190.26): the lock of the top step, agafat, i el deixa anar el `wait_cpu` o el `rest_cpu` que ve
// després. **Això és el que el DFS del xip no pot saber**: that a decode which starts slow and ends fast is one piece of work and
// not a load that climbed, i que una espera de la targeta no és feina (docs/50, docs/60).
void work_cpu(const char* why) {
  if (!policy_is_auto()) return;
  hold_cpu(kBusyMhz, 0, why);
}

// **I una espera no vol res** (0.190.26): the bottom step, and **no window**, perquè el que ve després no és una espera i el
// governador ha de poder tornar-lo a pujar quan la mostra ho digui. It is the card, the wire and the ink of the panel: time the
// chip spends with nothing to compute (docs/50, docs/60).
void wait_cpu(const char* why) {
  if (!policy_is_auto()) return;
  // **I una espera amb un dit a sobre no existeix** (0.190.87, trobat al run sencer): the kernel waits between turns and a wait asks
  // for the bottom step, però mentre un dit toca el vidre la placa està treballant per a una persona —el marc que s'ha de llegir i la
  // pàgina que s'ha de pintar— i baixar-li el rellotge a cada espera deixava el cim en una sola volta (docs/30, docs/50).
  if (g_a_finger_down) return;
  hold_cpu(kIdleMhz, 0, why);
}

void restore_session() {
  if (g_nvs == nullptr) return;
  char id[kIdCap];
  if (!read_text(kAppKey, id, sizeof(id))) return;
  char path[kPathCap];
  const bool has_path = read_text(kPathKey, path, sizeof(path));
  // **La consola que tenia quan va quedar-se a la memòria** (0.190.278): una app que dormia al fons torna al fons, i una que era al cable
  // torna al cable —perquè el que la va aparcar és el son i no una decisió de ningú (docs/30, docs/90).
  char was_detached[8] = {0};
  const bool detached = read_text(kDetachedKey, was_detached, sizeof(was_detached)) && was_detached[0] == 'o' &&
                        was_detached[1] == 'n';
  // Read once: an app that cannot start must not make the board boot into it for
  // ever, and a boot that comes back to the same app is the same as a boot loop
  // nobody asked for (docs/90).
  write_text(kAppKey, nullptr);
  write_text(kPathKey, nullptr);
  write_text(kDetachedKey, nullptr);

  // **La línia diu el que passa i no el que s'intentava** (0.190.24): it was printed before the two guards, so a boot that
  // came back for an alarm said `home_eink comes back where it was` and then, in the next line, `home_eink does not come
  // back: clock is in front` —two lines that contradict each other, of an app that did not move—. One line per outcome
  // (docs/60, docs/90).
  //
  // **Una app que ha pujat al davant es queda amb el vidre** (0.189.175): a `raise` alarm delivered at boot has already put
  // its app in front, and a second `run_app` on top of it is refused by the interpreter.
  if (shell::app_running()) {
    sayf("%s does not come back: %s is in front", id, shell::app_id());
    return;
  }
  // **Una pàgina que torna d'una sessió** (0.190.251): `come_back` és el que la fa llegir el seu `state` i no rentar el vidre (docs/90).
  shell::AStart start;
  start.id = id;
  start.path = has_path ? path : nullptr;
  start.come_back = true;
  // **I el vidre es renta abans que la pàgina torni** (0.190.294): el que hi ha al vidre és la imatge del son, i la primera pàgina de
  // l'app pagava el deute amb el seu mode i la deixava a sota (l'amo: *«la pantalla queda bruta del dibuix d'sleep i es queda al tornar
  // a home»*). El rentat és el de la vista sencera i el paga el sistema abans que l'app dibuixi (docs/30, docs/50, docs/90).
  start.wash = true;
  start.wire = detached ? shell::Wire::kDetached : shell::Wire::kAttached;
  if (!shell::start_a_page(start)) {
    sayf("%s could not come back (is it still there?)", id);
    return;
  }
  sayf("%s comes back where it was", id);
}

void forget_session() {
  write_text(kAppKey, nullptr);
  write_text(kPathKey, nullptr);
  write_text(kDetachedKey, nullptr);
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
  // **I la paraula de la placa mana sobre la del xip** (0.191.5): `esp_sleep_get_wakeup_cause()` només sap dir que ha estat una línia, i
  // en una placa amb tres línies armades alhora aquesta resposta és «un dit al vidre» tant si ha estat un dit com si ha estat el BOOT o
  // l'alarma del rellotge. La placa sí que ho sap, i aquesta línia és la que una persona llegeix per entendre per què s'ha despertat la
  // seva placa (docs/50, docs/60).
  const char* board_door = (g_power != nullptr) ? g_power->the_door_that_opened() : nullptr;
  const char* by = (board_door != nullptr) ? board_door : "the power coming back";
  if (board_door == nullptr) {
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

  // **I si el disc de les claus no ho ha pres, es diu** (0.190.109): the store keeps the settings in the RAM until this moment —that is what
  // saves the flash— and this is the one moment where a page that cannot be written means a setting that is lost. Abans la línia deia «són
  // escrites» passés el que passés (docs/70, docs/75). **I és una marca** (0.190.114): és l'única línia que diu que els ajustos han arribat a la
  // flash, o sigui que no ha de desaparèixer per un nivell de registre que parla de traces (docs/30, docs/70).
  log_markf("NVS", "the store %s", done.store ? "is written" : "did NOT take everything it had");
  log_printf(done.store ? pal::kLogInfo : pal::kLogWarn, "POWER",
             "%s: the console, %u of %u volume(s) and the store %s", reason,
             static_cast<unsigned>(done.volumes_ok), static_cast<unsigned>(done.volumes),
             done.store ? "are written" : "did NOT take everything it had");

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
