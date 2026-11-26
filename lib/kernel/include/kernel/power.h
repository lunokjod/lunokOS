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
// **Si aquesta placa pot tallar-se el corrent** (0.191.9, l'amo: *«el poweroff no funciona? està endollat i es queda amb el botó poweroff
// activat, però no veig l'splash de poweroff»*): la resposta és de la placa i no del sistema —el Paper té un pany a la seva font, la T5S3
// no pot tallar-se amb el cable a dins perquè qui la alimenta és el carregador, i l'ATOM i el Koala no tenen cap pany—. La pregunta es fa
// **dues vegades i per a dues coses**: la targeta del sistema no pinta una clau que no pot fer res (la mateixa regla que la lluna i la creu),
// i `power_off` la pregunta **abans** de tancar cap app i abans de la primera tinta (docs/30, docs/50, docs/60).
bool can_power_off();

// **La porta de darrere d'una placa de tecles** (0.189.25): whether a key pressed by a person wakes this chip. It is a capability
// of the board and not a question about the queue, which is why `can_deep_sleep` is the one that decides (docs/50, docs/60).
bool can_deep_sleep_with_the_keys();

// **El dit com a porta d'un son profund** (0.190.16): the switch that turns it on, which **every board keeps** —a board that cannot hear a
// finger accepts it and does nothing with it—, and the question of whether this board can (0.190.17). Off is the default and the cheap way
// down: the panel's rail is cut, i el que això encén és una tercera porta que costa corrent (docs/50, docs/60).
//
// **La becaina** (0.190.63, l'amo: *«[X] Light sleep instead of wait. Aplica lightSleep allá on puguis»*): el sistema passa mitja vida
// esperant i esperar despert és corrent que no cal gastar. Amb el commutador encès, cada espera prou llarga dorm el xip amb un son
// lleuger —la RAM i l'intèrpret hi són en tornar— que el temporitzador del RTC desfà, o una tecla, el dit o la consola abans d'hora.
// `nap(wait_ms)` és el que crida qui espera: torna sempre, i diu si ha dormit. **I quant pot durar l'espera d'un torn ho diu
// `nap_slice_ms()`**, quaranta mil·lisegons, que és el que un dit no nota: **l'escala que creixia fins a un segon ja no hi és** (0.190.181),
// perquè des de la 0.190.178 la dormida la fa la gestió de potència del xip i el que la limita és el `wait` del torn i el seu proper
// temporitzador (docs/50, docs/60, docs/95).
bool nap_on();
bool set_nap_on(bool on);
bool can_nap();
bool nap(int32_t wait_ms);
int32_t nap_ms();
// **El pany, i el xip dorma sol** (0.190.178, l'amo: *«activem el lightsleep automàtic d'idf»*): `open_the_sleep` fa les mateixes
// preguntes que `nap` —el commutador, el dit, el port cedit, el silenci de la consola i la placa que no en sap— i, si totes diuen que
// sí, obre les portes de la placa i **deixa que la gestió de potència adormi el xip mentre el torn espera a la cua**. Torna quants
// mil·lisegons es poden esperar (zero vol dir que aquesta volta no es dorma i el torn espera despert com sempre), i `close_the_sleep`
// les tanca i apunta al registre el que ha durat. `nap` és per a les dormides que es demanen de debò (`power nap`) (docs/50, docs/60).
int32_t open_the_sleep(int32_t wait_ms);
void close_the_sleep(int64_t waited_ms);
// **Una becaina demanada, per mirar-la** (0.190.71): one sleep of `ms` with no ladder and no switch, which is what `power nap` runs to
// say how long it lasted and what brought the chip back. És com se sap si una tecla o un dit desperten un son lleuger en una placa
// (docs/50, docs/95).
bool nap_now(int32_t ms);
// **Quants mil·lisegons pot esperar un dit** (0.190.74): quaranta, que és el que no es nota ni quan el vidre no pot tornar el xip. És el
// que el torn del nucli espera mentre la placa dorma entre voltes (docs/50, docs/95).
int32_t nap_slice_ms();

// **Un dit damunt del vidre** (0.190.87, l'amo: *«En qualsevol cas: mentre el dit toqui la pantalla també has de desactivar
// temporalment el LightSleep per evitar tenir problemes amb les lectures fins que el dit aixequi»*). Mentre una persona té el dit
// al vidre la placa **no fa cap becaina**: un son lleuger atura el xip, el tret de la línia del tàctil i la lectura del marc, i
// una lectura que s'ha perdut amb un dit a sobre és un toc que no arriba o que arriba doble. **És una resposta de la política i no
// del maquinari**: val amb l'escala de la CPU encesa i apagada, i la lleva el dit quan s'aixeca (docs/30, docs/50, docs/60).
void note_a_finger(bool down);
bool a_finger_is_down();

// **Els deu segons de silenci del port** (0.190.75): the quiet a board needs before it naps or before a hand that breaks a countdown
// is worth a line (docs/60).
int64_t console_in_use_ms();
#if defined(UNIT_TEST) || defined(PIO_UNIT_TESTING)
// I el mateix número mogut per una prova (0.190.77): a case that had to wait ten seconds would be a suite ten seconds slower for a
// nap that the case is not about (docs/60, docs/80).
void set_console_in_use_ms(int64_t ms);
#endif
void note_the_nap_end(bool something_happened);
// **Quantes becaines ha fet la placa en aquest run** (0.192.31): the number only goes up, so a hand that keeps the one it saw last knows
// whether the board has slept since —which is the only question `touch` has to answer before it believes the contact it had— (docs/50,
// docs/95).
uint32_t naps_seen();
bool wake_on_touch();
bool set_wake_on_touch(bool on);
// **La consola com a porta de la becaina** (0.190.147): the switch of the Battery tab is a setting of the system, so it goes down to the
// board from here like the glass's (docs/30, docs/50).
// **El que la placa sap de la darrera dormida** (0.190.151): printed at the boot and again whenever a person asks, because a deep sleep is a
// reset and the log of the arming goes with it (docs/50, docs/60).
void say_the_last_sleep();
bool wake_from_uart();
bool set_wake_from_uart(bool on);
bool can_wake_on_touch();

// **Un quadre que no és la imatge del son** (0.190.18). A wake for an alarm comes back with the picture of the way down on the glass and
// goes back down without painting it again —the glass already says it—; but an alarm that runs in a **window** paints the app's page, and
// a board that went down there would leave it for the person to find, over a run that is over. This is the word the window says when it
// ends, and `sleep` is what puts the picture back (docs/30, docs/60, docs/85).
void note_a_page_on_the_glass();

// Does the pending request, if there is one, and then applies the **idle policy**: on a
// board whose deep sleep is a real power off (a clock chip keeps the hour while the power
// is gone) and with an alarm pending, the board goes to sleep by itself until that alarm
// instead of staying awake waiting for it (`next_alarm_epoch_ms`, 0 when there is none).
// The kernel calls it in every turn, after the alarms and before the app's turn (docs/60).
void poll(int64_t next_alarm_epoch_ms);

// **Qui ha mogut el comptador de la quietud** (0.190.77, l'amo del 20/11: *«Quan arriba el DeepSleep? … esborres el idle counter del
// usuari quan entres en light Sleep?»*). A countdown that starts again is the one thing that can keep a board awake for ever, o sigui
// que cada mà que el mou deixa el seu nom i el registre el diu, i `power` el pot ensenyar en el moment (docs/30, docs/60).
enum class Hand : uint8_t {
  kBoot = 0,    // a boot: the counter starts here
  kConsole = 1, // a byte at the console
  kKey = 2,     // a key of the dial
  kGlass = 3,   // a finger on the glass
  kPolicy = 4,  // the policy itself: after a sleep, or when the switch is turned back on
};

// A person did something (a byte arrived on the console, a key went down, a finger is on the glass): the board is being used
// and does not go to sleep yet. The kernel says it, and so does anything that turns the policy back on (docs/60).
void note_activity();
void note_activity(Hand hand);

// **El nom de qui va moure el comptador l'última vegada** (0.190.77): the word of the log and of the `power` command, i el número que
// diu quanta estona fa. És el que converteix «la placa no s'adorm» en «una cosa mou el comptador cada mig minut» (docs/30, docs/60).
const char* hand_name(Hand hand);
Hand last_hand();
int64_t last_hand_ms();

// **L'estat de la quietud en una línia** (0.190.77): la que escriu `power` i la que una prova llegeix. Diu quants segons fa que
// ningú no toca la placa, quants en vol la política, i qui va ser l'última mà —o sigui el que cal per saber si el son arribarà i,
// si no arriba, per què (docs/30, docs/60).
bool idle_report(char* out, size_t cap);

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
// **I el que un indicador de càrrega sap de més** (0.190.155): the current and the capacities, which a board with a divider on an
// ADC cannot say. `hal::IPower::kUnknown` in a field is «this part does not say» and never a zero (docs/30, docs/50).
bool battery_detail(hal::IPower::BatteryDetail& out);
// **I el carregador** (0.190.156): the cable, what kind it is, and what is going into the cell. False on a board whose battery is
// an ADC and nothing else (docs/30, docs/50).
bool charger_detail(hal::IPower::ChargerDetail& out);

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

// **El que la feina demana i el que l'espera demana** (0.190.26, l'amo: *«Si està habilitat el Automatic scaling: al carregar una
// imatge, puja a 240; al carregar un epub, 240; al carregar una font, 240; al enviar dades al eink usa 80»*). Two doors and not one,
// because the two halves of a turn cost the opposite: `work_cpu` is a decode, a layout, the glyphs of a page —the chip has
// something to compute and the top step is what makes it shorter— and `wait_cpu` is a card, a wire, the ink of the panel —the chip
// has nothing to compute and the bottom step is what makes it cheaper—. **Les dues només manen amb l'escalat automàtic encès**: a
// person who fixed a step fixed it on purpose, and a window that undoes it two hundred milliseconds later is the thing `power cpu`
// exists to stop (docs/50, docs/60).
//
// **I el «quant» de la feina ja no és una estona** (0.190.175): el sostre s'agafa amb el lock del xip i es deixa amb el
// `wait_cpu` o el `rest_cpu` que ve després, i el xip compta els que el tenen. Una finestra era un rellotge del nucli que havia de
// caducar bé: prou gran per no baixar a mig decode i prou petita per no quedar-se a dalt per res (docs/50, docs/60).
void work_cpu(const char* why);
void wait_cpu(const char* why);

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

// **`trend_of` i `trend_step` no hi són** (0.190.187): the governor stopped guessing the load of a minute from thirty samples
// —the functions were deleted from `power.cpp`— and their declarations stayed here until a suite that still called them made the
// link fail, o sigui que les proves de `test_shell` no corrien des d'aleshores. El que ha quedat és el que el xip no pot saber i
// que la persona diu: `hold_cpu`, `work_cpu` i `wait_cpu` (docs/50, docs/60, docs/95).
// **La política automàtica, encesa de sèrie** (0.189.94): the three words are the three steps —**low is 80, normal is 160
// and high is 240**— and the system moves between them by itself, on every turn, from `load()`. A window that somebody asked
// for (the boot, the start of an app, a paint) wins over it, because those are moments whose work is known in advance and a
// clock that moves in the middle of one is a clock that makes it slower. **`power cpu <mhz>` la para i diu com tornar-la a
// encendre** (`power auto on`): a person who fixed a step did it to measure something, and a policy that undoes it is a
// policy nobody can measure against (docs/50, docs/60).
bool cpu_auto();
void set_cpu_auto(bool on);

// **Si aquesta placa escala el rellotge tota sola** (0.190.227, l'amo: *«Desactiva el Dynamic Frequency Scaling per l'M5 Atom Lite i l'M5 Atom
// Matrix. Només per aquests dos models. Que vagin sempre a 240Mhz»*). Ho contesta la placa (`IPower::scales_the_clock`) i es pregunta un cop a
// l'arrencada: amb això fals, el rang és **un sol valor, 240**, la política queda apagada per sempre en aquest run, i el commutador de la
// pàgina i `power auto` ho diuen i no fan res. **El son lleuger no s'hi toca**: el xip continua dormint entre torns (docs/50, docs/60).
bool scales_the_clock();

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

// **Un son profund que ha estat una arrencada del xip, i que el nucli ha de tornar a començar** (0.190.68). A la placa la crida de
// `sleep` no torna: el xip es reinicia i el que ve després és un boot sencer —la marca del son, el rellotge i la sessió desada es
// llegeixen i la pàgina de la persona torna—. El model de l'emulador no pot reiniciar el procés, o sigui que el que fa és dir-ho:
// `sleep` marca el flag, el torn del nucli s'acaba aquí mateix (res més no s'executa en nom d'un run que ja no hi és) i **qui porta
// l'executable** —`main_host` a l'emulador, el xip a la placa— fa el `kernel::boot` que el xip faria sol. `the_chip_restarted` no
// esborra res: el flag el neteja `kernel::boot`, que és on comença el run nou (docs/50, docs/60, docs/85).
bool the_chip_restarted();
void clear_the_restart();

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

// **Quant porta la cel·la i quina hora és, dit abans de baixar** (0.192.0, l'amo: *«Vull que el sistema posi al log la carrega de la batería i
// l'hora abans d'entrar en poweroff o DeepSleep, així si et despertes molt ràpid ho podrem saber. També comptar el consum de batería mentre
// dorm/apagat.»*). Dues meitats d'una mateixa mesura: la baixada deixa una fila a la botiga amb la càrrega i l'hora, i **l'arrencada que ve
// després la llegeix, la compara i la diu** —quant ha durat la baixada i què ha costat, en mV, en punts i, a les plaques que saben comptar
// el corrent, en mAh i amb la mitjana de mA—. És l'única manera de saber si una placa que sembla adormida ho està de debò, i el que l'amo vol
// veure quan una tornada és massa ràpida (docs/50, docs/60, docs/95).
//
// **La fila es desa amb la resta d'ajustos i la comenta `settle_before_leaving`**: una escriptura després d'aquell punt es quedaria a la RAM
// i el xip se l'enduria, o sigui que `note_the_load_before_the_way_down` s'ha de cridar **abans** del repòs (docs/70).
enum class WayDown : uint8_t { kDeepSleep, kPowerOff };
void note_the_load_before_the_way_down(WayDown what);
void say_what_the_way_down_spent();

// The rest, in milliseconds: **two seconds**, which is the number the owner asked for and longer than any card of this
// board needs to finish writing. It is one settable number because a test that cuts the supply does not have to pay it and
// because a board that one day wants another number should not need to find it in a corner of this file (docs/75).
const uint32_t kLeavingMs = 2000;
uint32_t leaving_ms();
void set_leaving_ms(uint32_t ms);

}  // namespace power
}  // namespace kernel

#endif  // LUNOK_KERNEL_POWER_H
