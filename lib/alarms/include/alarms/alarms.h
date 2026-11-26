// The alarm queue: one list, ordered from the nearest deadline to the furthest,
// and only its first element is ever armed on the hardware (docs/60).
//
// It lives outside the interpreter on purpose: an alarm outlives the VM that
// created it, and it has to be possible to fire one for an app that is not
// loaded. Nothing here knows about Lua, the console or the scheduler.
#ifndef LUNOK_ALARMS_ALARMS_H
#define LUNOK_ALARMS_ALARMS_H

#include <stddef.h>
#include <stdint.h>

namespace alarms {

// How many alarms fit at once. docs/60 says the number is `alarm.caps().max`
// until NVS is measured; the payload of each one is what has to fit there.
const uint32_t kMaxAlarms = 8;
const size_t kAppBytes = 32;
const size_t kPayloadBytes = 48;
const int64_t kDailyMs = 24 * 60 * 60 * 1000;
// **Dues alarmes d'un sol cop són la mateixa promesa si demanen el mateix instant al segon** (0.189.172): the tolerance of
// `find_same`, and it is a second because that is the step of a call and the width of what is being compared (docs/60).
const int64_t kSameShotMs = 1000;

// What an alarm carries besides its deadline. A whole JSON value is promised by
// docs/30; until there is an encoder, a string or a number is what fits, and
// anything else is refused where it is asked for.
enum class Payload : uint8_t { kNone = 0, kText = 1, kNumber = 2 };

struct Spec {
  const char* app = nullptr;    // whose alarm it is; copied
  int64_t on_mono_ms = 0;       // when it is due, on the monotonic clock
  int32_t repeat_ms = 0;        // 0 = once, kDailyMs = every day at that time
  bool raise_to_front = false;  // the app goes to the front when it fires
  bool persist = false;         // it has to survive a reboot (M3, NVS)
  // **Un `tick` i no una alarma** (0.192.60, l'amo: *«M'agradaría que l'app Stats rebés una crida cada minut, però només si
  // la placa està desperta. No vull que vaigi a l'RTC i desperti la placa. Una crida periódica, demanada per l'app i que el
  // sistema cridi sempre que estigui despert (deixant-ho de fer si cau en sleeep, i tornant quan es desperta)»*): **no arma mai
  // el despertar de la placa**, o sigui que la seva hora només la mira el torn del bucle, i si la placa dorm aquell tick es
  // perd —no es compta com una repetició perduda ni es recupera en despertar— (docs/30, docs/60).
  bool awake_only = false;
  Payload payload_kind = Payload::kNone;
  const char* payload = nullptr;
};

struct Entry {
  uint32_t id = 0;
  char app[kAppBytes] = {0};
  int64_t on_mono_ms = 0;  // the occurrence that fires, not the next one
  int32_t repeat_ms = 0;
  bool raise_to_front = false;
  bool persist = false;
  // **Un tick no es desa i no arma res**: viu el que viu la placa encesa, i l'app el torna a demanar quan s'obre (docs/60).
  bool awake_only = false;
  Payload payload_kind = Payload::kNone;
  char payload[kPayloadBytes] = {0};
  // Repetitions that were missed while the system was off. The alarm fires
  // once, however many days went by (docs/60).
  uint32_t missed = 0;
};

void reset();

uint32_t max();
uint32_t count();

// **Les dues preguntes de la barra** (0.189.57): hi ha una alarma que es repeteix —el despertador de cada dia— o una que
// sona un sol cop —el temporitzador que algú ha deixat? The queue is eight at most and the answer is one boolean, so the
// strip can ask it on every turn without walking the list (docs/50, docs/60).
//
// **I només compten les que sonen** (0.189.154): la cua també porta la feina de fons d'una app —`battery` hi posa una
// alarma que es repeteix cada deu minuts per prendre una mostra— i una campana que s'encés per ella deia que hi havia una
// alarma posada quan no n'hi havia cap. Les dues responen `raise_to_front`: l'alarma d'una persona vol el vidre, la de fons
// no (docs/30, docs/60).
bool has_repeating();
bool has_once();
// The i-th alarm of the ordered list, or nullptr. The pointer is only valid
// until the next call that changes the list.
const Entry* at(uint32_t index);

// Adds an alarm and gives it an id; 0 when there is no room or the spec cannot
// be used (no app, no name, a name that does not fit).
uint32_t add(const Spec& spec);

// **La mateixa promesa, si ja hi és** (0.189.172, l'amo: *«les alarmes de les apps es multipliquen, fes que el sistema pugui
// avisar a l'app de que ja te una alarma amb aquesta configuració»*): the id of the alarm of this app that is **the same
// configuration** as `spec`, or 0. `add` does not look at it: who asks for an alarm asks here first and the answer is what
// the app hears (docs/30, docs/60).
//
// The configuration is the payload, the period and the two flags. **Una alarma que es repeteix no té hora pròpia** —torna a la
// cua a la seva ocurrència següent— i per això el seu període i el seu payload són tota la configuració; una d'un sol cop
// és la seva hora, i es compara **al segon**: dues crides que demanen el mateix instant amb un segon de diferència són la
// mateixa promesa i no dues.
uint32_t find_same(const Spec& spec);

// Only the owner of an alarm may cancel it: an app cannot reach into another.
bool cancel(const char* app, uint32_t id);

// The deadline of the first alarm, or -1 when the list is empty.
int64_t next_due_mono_ms();

// **Els ticks d'una app, tots fora** (0.192.63, l'amo: *«es repeteixen els ticks a la cua? Només un tick per app. Si una app en
// demana unaltre, s'esborra l'anterior i es fa la que acaben de demanar»*): the queue of this system is one per app —a page has one
// clock and not a collection of them— and the door of `tick.every` calls this before it adds the new one, so asking twice for a
// minute and then for ten seconds leaves one tick and not two. Answers how many went, and **does not move the revision**: a tick is
// not written anywhere, o sigui que treure'l no ha de fer escriure la NVS (docs/30, docs/60).
uint32_t remove_awake_only_of(const char* app);

// **La propera que pot despertar la placa** (0.192.60): the deadline of the first alarm that is **not** a `tick`, or -1 when
// there is none. It is what the kernel arms on the hardware —a tick never does, o sigui que una app que demana una crida cada
// minut no encén el rellotge del xip ni un cop— i és la raó per la qual `next_due_mono_ms` no és prou per a `arm_nearest_alarm`
// (docs/30, docs/60).
int64_t next_wake_mono_ms();

// Takes the first alarm when its time has come. A repeating one goes back into
// the list at its next occurrence, and the copy in `out` says which occurrence
// fired and how many were missed. False when nothing is due.
bool take_due(int64_t now_mono_ms, Entry& out);

// Removes the alarm with that id, whoever owns it: for the window and for the
// list of missed alarms, which belong to the system.
bool remove_id(uint32_t id);

// **Les alarmes d'una app, totes** (0.189.157): the door an app's uninstall walks, because an alarm of an app that is gone
// is a board that wakes up for nothing (the queue shoots the nearest one at the clock chip). Returns how many went, so the
// line the log writes says a number instead of "some" (docs/30, docs/40, docs/60).
uint32_t remove_of(const char* app);

// A number that changes every time the list does. The kernel saves the
// persistent alarms when it moves, so nobody has to remember to save.
uint32_t revision();

// **Una alarma dita amb les paraules d'una persona** (0.189.149): `#3 clock: in 600 s, at 14:32:10, repeats every 600 s,
// persist, raises the app, payload text 4 B`. It is the line the log writes when the queue changes —the owner: *«Vull que
// el sistema mostri la llista de alarmes que te i quina es la mes propera quan s'afegeixi o tregui una alarma»*— and it
// lives here and not in a caller because these are this struct's fields: a caller that rebuilt the line from `at()` would
// be a second reader of the same struct, and the day a field arrived the two would disagree (docs/60).
//
// It is **pure** like the rest of the file: the two clocks and the offset come as numbers, exactly as they do in `dump()`.
// `epoch_now_ms <= 0` is a board with no hour, and then the line has no `at`. Returns the characters written, or 0 when
// there is no such alarm or the text does not fit.
size_t describe(uint32_t index, int64_t mono_now_ms, int64_t epoch_now_ms, int32_t offset_minutes, char* buf,
                size_t cap);

// Writes the alarms that have `persist` as text, one per line, with the deadline
// as an epoch like every other time the system shows (docs/60). Returns the bytes
// written, 0 when there is nothing to keep (or when the epoch is not known, which
// is when a wall-clock deadline cannot be written down).
size_t dump(int64_t mono_now_ms, int64_t epoch_now_ms, char* buf, size_t cap);

// Replaces the list with what the text says. The monotonic clock restarts at
// every boot, so the lines carry epochs and they are turned back into deadlines
// here; an alarm that is already late stays late, and the next take_due() fires
// it with its late_ms (docs/60). False when the text cannot be used at all.
bool load(int64_t mono_now_ms, int64_t epoch_now_ms, const char* text);

}  // namespace alarms

#endif  // LUNOK_ALARMS_ALARMS_H
