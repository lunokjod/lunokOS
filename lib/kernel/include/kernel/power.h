// The energy policy of the system: when the board sleeps, what it writes down
// before it goes, and what comes back when it wakes (docs/90, M6).
//
// The board knows how to stop the clocks (hal::IPower) and the kernel is the one
// that decides when that is allowed, what has to be said to the app first and what
// has to be read afterwards (docs/30).
#ifndef LUNOK_KERNEL_POWER_H
#define LUNOK_KERNEL_POWER_H

#include <stddef.h>
#include <stdint.h>

#include "hal/hal.h"
#include "kernel/splash.h"

namespace kernel {
namespace power {

// Remembers the board's power, reports what the last sleep cost and brings back
// the app that was in front when the board went down. Runs at boot.
//
// **`for_an_alarm` és la tornada que no és per a ningú** (0.189.182): the page of the session record **is not started** —the glass
// keeps the picture of the sleep and the record waits for the person— and `idle_sleep` takes the board back down at the first
// turn. El que ha vingut a fer-ho és el ganxo de l'alarma, que ja ha corregut a la finestra (docs/60, docs/90).
void boot(hal::IPlatform& platform, bool for_an_alarm = false);

// True when this board has a power interface and can sleep at all.
bool available();

// True when the board can do this kind of sleep: a band that only knows a light
// sleep says so, and `power.caps()` repeats it (docs/30).
bool can_sleep(hal::SleepMode mode);

// Where it comes back from, in the board's own words ("timer, button").
const char* wakes();

// How far the last sleep was from the hour it was given, in a line a person can read:
// `+1234 ms (timer)`. False when nothing has been measured yet. It is kept in the
// key-value store because the console line is one boot older and gone by the time
// somebody wonders, and because this number is what decides whether the wake has to move
// to the board's clock chip (docs/50, docs/60, docs/90).
bool last_wake_report(char* out, size_t cap);

// The sleep itself, done now: it is what the shell command uses, when there is no
// app in the way. A **deep** sleep does not come back: the session is written, the
// app in front is closed with its goodbye hooks and the timer is set for
// `at_epoch_ms`; the board restarts and `boot()` brings the app back (docs/30,
// docs/90). A light sleep keeps everything and returns true.
bool sleep(hal::SleepMode mode, int64_t at_epoch_ms);

// The same, asked for while the system is turning (an app, or a window): closing
// the app and stopping the chip happen **between turns**, never inside a Lua call,
// because closing the interpreter from the inside is not a thing (docs/30).
bool request_sleep(hal::SleepMode mode, int64_t at_epoch_ms);

// **La baixada d'una persona** (0.189.25, l'amo: *«Fes la B … l'afegeixes al panell desplegable del sistema entre el botó de
// poweroff i el de reboot»*): the same way down the idle policy takes —one preparation, one call to the board— with no hour asked,
// and the nearest alarm filling it in when there is one (0.190.15). The app in front is closed with its goodbye hooks and written
// down, because **a deep sleep is a reset**: `boot()` brings the app back with its page (docs/30, docs/50, docs/90).
// False is a board with nowhere to come back from —no hour and no key the chip can watch while it sleeps—; a board that can does
// not come back from here.
bool deep_sleep_with_the_keys();

// True when this board can do that sleep at all —a key, or an alarm and a clock to wake it—, asked **before** the way down spends
// its picture and its margin.
bool can_deep_sleep();

// **La porta de darrere d'una placa de tecles** (0.189.25): whether a key pressed by a person wakes this chip. It is a capability
// of the board and not a question about the queue, which is why `can_deep_sleep` is the one that decides (docs/50, docs/60).
bool can_deep_sleep_with_the_keys();

// **El dit com a porta d'un son profund** (0.190.16): the switch that turns it on, which **every board keeps** —a board that cannot hear a
// finger accepts it and does nothing with it—, and the question of whether this board can (0.190.17). Off is the default and the cheap way
// down: the panel's rail is cut, i el que això encén és una tercera porta que costa corrent (docs/50, docs/60).
bool wake_on_touch();
bool set_wake_on_touch(bool on);
bool can_wake_on_touch();

// Does the pending request, if there is one, and then applies the **idle policy**: on a
// board whose deep sleep is a real power off (a clock chip keeps the hour while the power
// is gone) and with an alarm pending, the board goes to sleep by itself until that alarm
// instead of staying awake waiting for it (`next_alarm_epoch_ms`, 0 when there is none).
// The kernel calls it in every turn, after the alarms and before the app's turn (docs/60).
void poll(int64_t next_alarm_epoch_ms);

// A person did something (a byte arrived on the console): the board is being used and
// does not go to sleep yet. The kernel says it, and so does anything that turns the
// policy back on (docs/60).
void note_activity();

// The idle policy is a switch a person can turn off while working at the prompt:
// `power idle off` keeps the board on the whole time (docs/30).
bool idle_sleep_allowed();
void set_idle_sleep_allowed(bool on);

// **Quant de silenci vol la política** (0.189.182): ten seconds without a byte on the console are what tells the board that
// nobody is using it. It is a number a case can move —one that had to wait the ten seconds would be a suite ten seconds slower
// for one line of a log— and it goes back to where it was (docs/60, docs/80).
// **Quanta estona fa que ningú no toca la placa** (0.190.9, l'amo: *«el comptador d'idle es el mateix que per el que fa saltar el
// device lock… No malgastis temps i memoria en varis comptadors»*): **un sol comptador**, el d'aquesta casa, i el llegeixen les
// dues coses que esperen una mà: el son profund (aquest fitxer) i la pàgina del bloqueig (`lock::poll`, que ja no en té cap de
// seu). El mouen les tres coses que són una persona a la placa —un byte de consola, un dit al vidre i una tecla del dial— i una
// arrencada el posa a zero (docs/30, docs/60).
int64_t quiet_ms();

int64_t idle_quiet_ms();
void set_idle_quiet_ms(int64_t ms);

// "Do not sleep on your own while this is set." The idle policy reads it before it puts the
// board down, and an app that is doing something that must not be interrupted says it here
// (docs/30, docs/60).
bool keep_awake(bool on);
bool keeping_awake();

// The battery, when the board has a gauge. False is "this board cannot tell", and
// that is not "empty" (docs/75).
bool battery(int& mv, int& pct, bool& charging);

// **Under this mark a board says it is over** (0.183.0): one number, in one place, so that the boot, the way down and the
// console cannot disagree about what "low" is. Five percent of a cell of this glass is minutes and not hours (docs/50).
constexpr uint8_t kLowBatteryPercent = 5;

// **The battery a board has left, said as a yes or a no**: true when it can be read, there is no cable in the slot and
// the charge is at or under the mark. The cell is the board's and the number is the gauge's, so both a board without one
// and a gauge that answers nothing say `false` —"nothing to say"— and never "empty" (docs/50).
bool battery_is_low(int& pct);

// **The picture of a moment that the battery decides** (0.183.0). The boot asks this before this module has been booted,
// so the reading comes in as an argument —`known` is whether the gauge answered at all—: a board that comes up with a
// cell under the mark says `lowbatt` instead of its logo, because the picture of the boot is the one moment where the
// person can still be told, and the chosen drawing is what a board with a battery comes up with.
splash::Kind boot_picture(const char* chosen, bool known, int pct, bool charging);

// **The picture of the way down**: `lowbatt` when the battery is what is over, `sleep` for a deep sleep and `poweroff`
// for a board that is being asked to stop. A light sleep paints nothing at all (docs/50).
splash::Kind going_down_picture(bool deep);

// The speed of the processor. It is the same decision as the sleep —energy— and it
// is taken here and not inside an app: the board says which steps it has, and a
// step it does not have is refused instead of rounded (docs/30, docs/50).
uint32_t cpu_mhz();
bool set_cpu_mhz(uint32_t mhz);

// **La velocitat de repòs és 160 i les estones de feina són finestres** (0.179.38): the system works at the resting step
// and only a moment whose work is known to be heavy —the boot and the start of an app— asks for the top one, because a
// step that is never given back is a board burning its battery at two hundred and forty for nothing. A window is a
// number of milliseconds and the system is the one that sees it end (`poll`); a step a person asks for with `power cpu`
// is the one the system goes back to. **Every move is a line of the log** (docs/50, docs/60).
const uint32_t kIdleMhz = 80;
const uint32_t kRestingMhz = 160;
const uint32_t kBusyMhz = 240;
// **Els segons d'una càrrega** (0.179.38): an app reads its source, builds an interpreter and lays its first page out,
// and three seconds is what it takes on this board with room to spare.
const int32_t kStartBusyMs = 3000;
uint32_t resting_cpu_mhz();
// To a step now, and back to the resting one after `hold_ms` — `0` leaves no window and the caller steps it down.
void hold_cpu(uint32_t mhz, int32_t hold_ms, const char* why);
// The resting step now, and no window is owed any more.
void rest_cpu(const char* why);

// ---- la càrrega del sistema, en tres paraules (0.189.93, docs/60) ----
//
// **Una càrrega no és un número instantani**: it is the share of a window of time that the kernel spent *working* and not
// waiting, and it is asked for to decide the speed of the processor —80, 160 or 240 MHz— and not to be printed for its own
// sake. **El que compta com a feina és el torn menys l'espera**: a board whose turns are all `queue.wait` is a board that is
// idle with its meter running, and its load is zero however many turns it took (l'amo: *«podem saber la càrrega del sistema
// en un moment concret?»*, docs/95).
//
// The three words are the three steps of that decision. **The boundaries keep a band of hysteresis** —five points— so a
// board that sits on one of them does not move its clock at every turn, which is the same reason a thermostat has one.
enum class Load : uint8_t { kLow = 0, kNormal = 1, kHigh = 2 };

// **The window of the load**, in milliseconds: the smallest span in which a turn of this system is more than noise. It is
// asked for by `load()` and it is not a promise to the caller —a call inside the window answers the last sample— (docs/50).
const uint32_t kLoadWindowMs = 250;

// The word, for a line of the console and for whoever reads it.
const char* load_name(Load load);

// The load of the window that ends now, or the one before it when the window has not run yet: it costs two reads of the
// meters and no clock of its own, so an app, the shell and the governor ask it as often as they like.
Load load();

// The number behind the word, in tenths of a percent: what a person wants to see beside it, and what the next step of the
// policy will be tuned against (docs/95).
uint32_t load_percent10();

// **The thresholds alone and pure**, which is what `load()` decides with, so a suite can hold them without a board: below
// twenty per cent is low, over sixty is high, and five points of band between the two (docs/50).
Load load_of(uint32_t percent10, Load current);

// **La tendència d'una sèrie de mostres** (0.189.95): the mean of the last half of the samples less the mean of the first
// half, in tenths of a percent. Positive is a board that is doing more than it was, negative one that is doing less and zero
// one that is doing the same. It is the whole of what the governor looks at, and it is pure so a suite can hold it
// (docs/50, docs/95).
int32_t trend_of(const uint8_t* samples, uint8_t count);

// **I el pas que la tendència i el nivell demanen**: one step in the direction of the trend and never two at once; a flat
// trend leaves the level alone unless the half minute it comes from is clearly idle —under ten per cent, the bottom step— or
// clearly busy —over fifty, the top one—, and then it goes there in one move. `mhz` itself when nothing is to be changed
// (docs/60).
uint32_t trend_step(uint32_t mhz, int32_t trend10, uint8_t recent_pct);

// **La política automàtica, encesa de sèrie** (0.189.94): the three words are the three steps —**low is 80, normal is 160
// and high is 240**— and the system moves between them by itself, on every turn, from `load()`. A window that somebody asked
// for (the boot, the start of an app, a paint) wins over it, because those are moments whose work is known in advance and a
// clock that moves in the middle of one is a clock that makes it slower. **`power cpu <mhz>` la para i diu com tornar-la a
// encendre** (`power auto on`): a person who fixed a step did it to measure something, and a policy that undoes it is a
// policy nobody can measure against (docs/50, docs/60).
bool cpu_auto();
void set_cpu_auto(bool on);

// **Les velocitats que la política pot fer servir** (0.189.107): a mask of the three steps —bit 0 is 80, bit 1 is 160 and
// bit 2 is 240— and **every step this module takes is clamped to it**, the ones the system asks for with a window and the
// one a person types too: a board whose owner said `cfg freq 240 off` must not go to 240 even for a paint, or the setting
// would be a suggestion (docs/50, docs/60). A mask of zero is read as the three, because a governor with no step is not a
// government; `allowed_step` is the one arithmetic that decides, and it goes to **the smallest allowed step that is not
// under what was asked**, or the biggest allowed one when nothing is above it.
void set_allowed_steps(uint8_t mask);
uint8_t allowed_steps();
uint32_t allowed_step(uint32_t want);

// The app that was in front when the board went to sleep comes back, with
// `app.args.restore` in its args (docs/30). Runs at boot; exposed so a test can do
// a reboot without one.
void restore_session();

// Forgets what a sleep left behind: what a person means by "do not come back
// here".
void forget_session();

// **D'on ve aquesta arrencada** (0.189.180, l'amo: *«Abans de tocar la pantalla. s'ha de saber si es ve d'un deepsleep, una
// arrencada nova o un reinici. Segons d'on es vé es farà un arranc d'una manera o unaltre»*): three kinds, and each one boots its
// own way. The chip cannot tell a power-on from a reset —both answer `ESP_SLEEP_WAKEUP_UNDEFINED`—, i el que els distingeix és
// **la memòria que només se n'emporta el corrent**: la RAM del RTC (`/rtc`) sobreviu a un reinici i mor amb la pila. El son
// profund té la seva pròpia marca al magatzem, que és l'única que diu que la tirada continua (docs/50, docs/60, docs/70).
enum class BootKind : uint8_t {
  kNew = 0,  // the power came back: a run that starts
  kRestart,  // the chip restarted with the power on: the button, a panic, a `reboot`
  kSleep,    // it came back from a deep sleep: the run goes on
};

// What this boot is; `note_the_boot` decides it and the rest of the boot reads it.
BootKind boot_kind();

// **Si el que ha despertat la placa és una alarma i no una persona** (0.189.182): the chip says what brought it back, and the two
// answers that are an alarm are its timer and the clock chip's quartz —a button, the console or the power are a person—. El que
// contesta és el que `note_the_boot` va llegir, que és la primera cosa d'una arrencada que té la plataforma a mà (docs/50, docs/60).
bool woken_by_an_alarm();

// **La placa ha vingut per una alarma i prou** (0.189.182, l'amo: *«un gestor que permeti arrencar sense canviar res de la
// pantalla, executar l'app amb les dades de l'alarma, esperar a que acabi i tornar al deepsleep»*): the boot of a return that was
// the chip's timer, with nothing in front after the alarms have been delivered. El vidre no es toca —no hi ha pàgina, ni pany, ni
// casa—, el registre de sessió **no es consumeix** (la persona que vingui després ha de trobar la seva pàgina) i el bucle torna a
// dormir al primer torn (docs/60, docs/90).
bool came_back_for_an_alarm();

// The word the log and a person use: `new`, `restart` or `sleep`.
const char* boot_kind_word(BootKind kind);

// **Es decideix una vegada, abans que res no pinti** (0.189.180): it reads the note of a deep sleep —and consumes it— and the
// mark of the RTC RAM —and leaves it for the next boot—, o sigui que la següent arrencada sabrà si el corrent ha marxat. The
// answer goes to the log with its own word, que és la primera cosa que una persona que llegeix un registre vol saber
// (docs/50, docs/60, docs/75).
BootKind note_the_boot(hal::IPlatform& platform);

// **Tot el que pot quedar a mig fer, acabat, i un repòs abans que el corrent es talli** (0.189.18, l'amo: *«Haurà donat
// temps a gravar-ho tot al SD?? s'ha apagat tot molt ràpid. Fes flush a tots els dispositius que es puguin abans d'iniciar
// el procés d'apagada. I fes flush, tant de l'sd, com la flash, com l'nvs com el serial. I preferiblement dona un parell de
// segons de marge abans d'iniciar el procés final d'apagada o reinici»*): the console, every volume the VFS has mounted and
// the store of the board are asked to write what they may be holding, and then the board **rests** —the margin is the
// part no call can do for it: the controller of a card copies its own buffer into the flash some milliseconds after the
// last sector arrived, and cutting the supply in the middle of that is how a card loses a folder— (docs/70, docs/75).
struct Leaving {
  bool console = false;    // the serial port drained what it had to send
  uint8_t volumes = 0;     // the volumes it asked, once each however many doors point at them
  uint8_t volumes_ok = 0;  // the ones that answered they are written
  bool store = false;      // the key-value store of the board
  uint32_t margin_ms = 0;  // the rest it gave the medium before allowing the current to go
};
Leaving settle_before_leaving(const char* why);

// The rest, in milliseconds: **two seconds**, which is the number the owner asked for and longer than any card of this
// board needs to finish writing. It is one settable number because a test that cuts the supply does not have to pay it and
// because a board that one day wants another number should not need to find it in a corner of this file (docs/75).
const uint32_t kLeavingMs = 2000;
uint32_t leaving_ms();
void set_leaving_ms(uint32_t ms);

}  // namespace power
}  // namespace kernel

#endif  // LUNOK_KERNEL_POWER_H
