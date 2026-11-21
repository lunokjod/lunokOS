#include "kernel/kernel.h"

#include <stdio.h>
#include <string.h>

#include "alarms/alarms.h"
#include "hal/civil.h"
#include "kernel/apps.h"
#include "kernel/button.h"
#include "kernel/cfg.h"
#include "kernel/fs_ops.h"
#include "kernel/env.h"
#include "kernel/imu.h"
#include "kernel/led.h"
#include "kernel/lock.h"
#include "kernel/log.h"
#include "kernel/meter.h"
#include "kernel/net.h"
#include "kernel/power.h"
#include "kernel/session.h"
#include "kernel/serial.h"
#include "kernel/shell.h"
#include "kernel/version.h"
#include "kernel/vfs.h"
// **The page, its reader and the faces, only where there is a glass to put them on** (0.179.9): these six headers are what
// pulls the whole display stack —and with it `epub`, `ttf` and `img`— into a build, and a board without a screen carries
// none of it (docs/50, docs/75).
#if LUNOK_GLASS
#include "kernel/bar.h"
#include "kernel/book.h"
#include "kernel/panel.h"
#include "kernel/display.h"
#include "kernel/splash.h"
#include "kernel/touch.h"
#include "kernel/ui.h"
#endif
#include "luabind/luabind.h"
#include "pal/pal.h"

namespace kernel {
namespace {

// The three lines that connect the interpreter's meters to this module's table: a call into the system,
// a stretch of the interpreter running, and a block of the extra memory taken or given back.
void app_call_cost(uint32_t us) { meter::add(meter::kLuaCall, us); }
void app_run_cost(uint32_t us) { meter::add(meter::kLuaRun, us); }
void app_own_cost(uint32_t us) { meter::add(meter::kLuaOwn, us); }
uint64_t lua_calls_total() { return meter::us(meter::kLuaCall); }
void app_extra_cost(uint32_t us, bool allocated) {
  meter::add(allocated ? meter::kExtraAlloc : meter::kExtraRelease, us);
}

}  // namespace

namespace {

// How long the queue may block before the loop wakes up and looks around.
const int32_t kIdleTimeoutMs = 1000;

// With an app alive that waits for something without a deadline, the loop wakes
// up this often so the app gets its turn.
const int32_t kAppIdleMs = 20;

// **The cadence of a hand** (0.179.21): the keys and the glass are **polled in the turn** and neither of them pushes an
// event into the queue, so a wait longer than this one is a press that nobody reads until the wait ends. It was a real
// cliff and not a millisecond: the prompt waited a whole second, and an app awaiting a promise waited for **that
// promise's deadline** —`button.wait()` with its default of twenty seconds was a board that did not answer its own
// button for twenty seconds, and a `net.connect` of eight seconds was eight seconds of a page that does not feel the
// finger—. The number is the glass's own: the GT911 of the Paper scans the panel every **ten milliseconds** and keeps
// the frame of that scan until a read takes it, so a faster turn can only be told what the last read already said, and
// a slower one is a finger the board owes (docs/50, docs/95).
const int32_t kInputPeriodMs = 10;

// Longest command line the batch mode accepts.
const size_t kCommandBytes = 128;

// **Un caràcter i mig a 115200** (0.190.21): 126 symbols is the longest RX timeout the UART can count
// (~12 ms at 115200), and a line that has been high and still for that long has nothing of this boot
// left in it (docs/50).
const uint32_t kConsoleQuietMs = 12;

// How long the console is allowed to keep throwing bytes away before the system
// says anything. The board starts its UART before us and the first bytes after
// a reset are junk; without this the shell answers bytes nobody typed and prints
// a prompt for each newline it finds in the noise. **It is the ceiling now**
// (0.190.21): a quiet line is done at `kConsoleQuietMs`, and only a line that
// never stops talking spends the quarter of a second (docs/50).
const uint32_t kConsoleSettleMs = 250;

hal::IPlatform* g_platform = nullptr;
bool g_running = false;

// **The session memory of an app, asked from the interpreter** (0.179.0): the kernel is the one that knows which app is
// running, so the key that arrives from Lua is paired with that id here and nowhere else —`"apps"` is one block for a
// launcher and another for the app after it (docs/30, docs/75).
bool session_acquire(const char* key, size_t bytes, size_t& got) {
  return session::open_session(luabind::app_id(), key, bytes, got);
}

// **Els bytes d'un bloc, amb el nom de l'app posat aquí** (0.190.174): the same rule as `acquire` —the key is the app's and
// the kernel is the one that knows which app is running— and the three doors the interpreter writes and reads a cache with,
// which is the only shape a block of the bank can have (docs/30, docs/50, docs/75).
uint32_t session_bytes_read(const char* key, uint32_t offset, char* out, uint32_t cap) {
  return session::read_at(luabind::app_id(), key, offset, out, cap);
}

uint32_t session_bytes_write(const char* key, uint32_t offset, const char* in, uint32_t bytes) {
  return session::write_at(luabind::app_id(), key, offset, in, bytes);
}

bool session_bytes_clear(const char* key) { return session::clear(luabind::app_id(), key); }

bool session_release(const char* key) { return session::release(luabind::app_id(), key); }

// **I una sessió seva es pot llençar de debò** (0.189.16): the key arrives from Lua and the id of the app is the kernel's,
// here and nowhere else, so `sys.session_destroy('apps')` throws away the launcher's own `apps` and never another app's
// (docs/30, docs/75).
bool session_destroy(const char* key, uint32_t& blocks, uint32_t& files, size_t& bytes) {
  const session::Forgotten gone = session::forget(luabind::app_id(), key);
  blocks = gone.blocks;
  files = gone.files;
  bytes = gone.bytes;
  return true;
}

// **El port sèrie cedit des de Lua** (0.189.20): the id of the running app is put here —or `"the prompt"` when it is a person
// at the interpreter’s prompt, which is the same door and the same silence— so a wire never belongs to a name nobody can see
// (docs/30, docs/50).
const char* serial_owner() {
  const char* id = luabind::app_id();
  return (id != nullptr && id[0] != '\0') ? id : "the prompt";
}

bool serial_claim(const char* by) {
  (void)by;
  return serial::claim(serial_owner(), "an app asked for it");
}

bool serial_release(const char* by) {
  (void)by;
  return serial::release_owner(serial_owner(), "the app that had it let it go");
}

// **The relief of the allocator, with the app that is in front** (0.189.11): `pal` gives one function pointer and no room
// for a second argument, so the id of the running app is read here —the kernel is the only one that knows it— and the
// session module is told which cache is the one of the page a person is looking at. That is what lets the memory be given
// back in the order a person expects: the caches of the other apps first, and the cache of the app on the glass only when
// that was not enough (docs/30, docs/75).
void relieve_memory(void* keep, size_t asked) { session::relieve(luabind::app_id(), keep, asked); }

void settle_console() {
  pal::IConsole* console = pal::system().console();
  if (console == nullptr) return;

  const int64_t started = pal::system().millis();
  // **La línia diu quan ha callat** (0.190.21): the board's UART counts the edges of its RX pin, so the
  // quarter of a second that used to be spent throwing away noise is now the ceiling of a line that never
  // goes quiet; a quiet line is done in about one character time (docs/50, docs/95).
  const bool quiet = console->wait_quiet(kConsoleQuietMs, kConsoleSettleMs);
  // The reader task of the PAL leaves one event per look at the line; they are no-ops and the old wait
  // drained them. Leaving them would fill the queue the alarms use before the first turn runs.
  pal::Event ev;
  while (pal::system().queue()->pop(ev, 0)) {
  }
  log_printf(pal::kLogDebug, "SYS", "console %s after %lld ms", quiet ? "quiet" : "at the ceiling",
             static_cast<long long>(pal::system().millis() - started));
}

void out(const char* text) {
  pal::IConsole* console = pal::system().console();
  if (console != nullptr) console->write(text, strlen(text));
}

void print_banner() {
  out("\r\nlunokOS ");
  out(kVersion);
  out("  ");
  out(g_platform->id());
  // On the PC both are true: the name is the board the emulator was told to be, and the
  // word says the hardware under it is a fake (docs/85).
  if (g_platform->emulator()) out(" (emulator)");
  out("\r\n");
  out("type 'help' and Enter.\r\n");
}

void count_caps(uint32_t& present, uint32_t& total) {
  const hal::Caps& caps = g_platform->caps();
  total = hal::cap_count();
  present = 0;
  for (uint32_t i = 0; i < total; ++i) {
    if (caps.has(static_cast<hal::Capability>(hal::cap_bit(i)))) ++present;
  }
}

// The clock the alarms are measured with. The monotonic one never jumps, which
// is what a deadline needs; the wall clock is only for what a person reads.
int64_t clock_mono() {
  hal::IClock* clock = (g_platform != nullptr) ? g_platform->clock() : nullptr;
  // **The monotonic count is also a read of the RTC chip** on this board, and the button, the light and
  // the radio ask for it several times a turn: it is a device call like any other (docs/30, docs/50).
  meter::Scope scope(meter::kClockMono);
  if (clock != nullptr) return clock->monotonic_ms();
  return pal::system().millis();
}

int64_t g_armed_at = -1;  // what the hardware was last asked to wake us at

// The alarms that have `persist` live in the NVS between boots. The queue writes
// itself as text with an epoch per line, because the monotonic clock starts again
// at every boot and means nothing tomorrow (docs/60).
const char kAlarmNs[] = "alarms";
const char kAlarmKey[] = "queue";
const size_t kAlarmBlobBytes = 768;
uint32_t g_saved_revision = 0;
// True when the NVS holds an alarms key. Until something is written there is
// nothing to erase, and erasing a key that does not exist is a write to the flash
// for nothing (docs/70 and docs/75).
bool g_alarms_stored = false;

int64_t clock_epoch() {
  hal::IClock* clock = (g_platform != nullptr) ? g_platform->clock() : nullptr;
  // **The hour is a chip on a bus**, and the bar asks for it on every turn: it is a call and it is
  // counted like any other (docs/30, docs/50).
  meter::Scope scope(meter::kClockNow);
  return (clock != nullptr) ? clock->epoch_ms() : 0;
}

// Written when the queue changed and never otherwise: the revision counter is
// what says it changed, so nobody has to remember to save.
void save_alarms() {
  if (g_platform == nullptr) return;
  hal::IKeyValue* nvs = g_platform->nvs();
  if (nvs == nullptr) return;
  g_saved_revision = alarms::revision();

  char blob[kAlarmBlobBytes];
  const size_t len = alarms::dump(clock_mono(), clock_epoch(), blob, sizeof(blob));
  if (len == 0) {
    // Nothing worth keeping: forget the key, but only if it is there.
    if (g_alarms_stored) {
      nvs->erase(kAlarmNs, kAlarmKey);
      g_alarms_stored = false;
    }
    return;
  }
  if (!nvs->set(kAlarmNs, kAlarmKey, blob, len)) {
    log_printf(pal::kLogWarn, "ALARM", "cannot write the queue to nvs");
    return;
  }
  g_alarms_stored = true;
}

// Only the nearest alarm is ever armed, and only when the nearest one changed:
// that is what makes dozens of alarms cost one wake (docs/60).
// **On ha anat l'alarma, i per què** (0.189.149, l'amo: *«quan es programa una alarma també ho vull veure i a quin
// dispositiu s'envia o quin métode (si es un comptador intern o un RTC...)»*). A board keeps one deadline and it keeps it
// in one place: the chip of the clock when there is one behind the power circuit, the SoC's timer when there is not, and
// **the turn of the loop** when the board cannot wake itself at all —which is the answer that was never written down and it
// is the one that matters, because such an alarm only fires while somebody is watching the board— (docs/50, docs/60).
const char* keeper_word(const char* keeper) {
  if (keeper == nullptr) return "nothing";
  if (strcmp(keeper, "chip") == 0) return "the clock chip";
  if (strcmp(keeper, "timer") == 0) return "the RTC timer of the SoC";
  return "nothing";
}

// The wall clock of an epoch, in the hour this system shows everywhere, or an empty text when there is no hour yet.
void hour_text(int64_t epoch_ms, char* out, size_t cap) {
  out[0] = '\0';
  if (epoch_ms <= 0 || cap == 0) return;
  int year = 1970;
  int month = 1;
  int day = 1;
  int hour = 0;
  int minute = 0;
  int second = 0;
  hal::epoch_ms_to_civil(epoch_ms + static_cast<int64_t>(cfg::offset_minutes()) * 60000, year, month, day, hour, minute,
                         second);
  snprintf(out, cap, "%02d:%02d:%02d on %d-%02d-%02d", hour, minute, second, year, month, day);
}

void arm_nearest_alarm() {
  if (g_platform == nullptr) return;
  hal::IClock* clock = g_platform->clock();
  if (clock == nullptr) return;

  const int64_t next_mono = alarms::next_due_mono_ms();
  if (next_mono == g_armed_at) return;
  // **Arming a wake is a write to the RTC**, the chip that has to keep counting with the board asleep
  // (docs/30, docs/50).
  meter::Scope scope(meter::kClockWake);
  g_armed_at = next_mono;
  const uint32_t id = (alarms::count() > 0 && alarms::at(0) != nullptr) ? alarms::at(0)->id : 0;
  if (!clock->can_wake()) {
    // No timer and no chip that can bring the board back: the deadline is only seen between turns.
    if (next_mono < 0) return;
    log_printf(pal::kLogWarn, "ALARM",
               "#%u is kept by the turn of the loop: this board cannot wake itself, so it fires only while it is on",
               static_cast<unsigned>(id));
    return;
  }
  if (next_mono < 0) {
    clock->disarm_wake();
    log_printf(pal::kLogInfo, "ALARM", "no alarm is pending: the wake is disarmed");
    return;
  }
  const int64_t left = next_mono - clock->monotonic_ms();
  const int64_t at_epoch = clock->epoch_ms() + left;
  if (!clock->arm_wake(at_epoch)) {
    log_printf(pal::kLogWarn, "CLOCK", "#%u: cannot arm the wake for %lld", static_cast<unsigned>(id),
               static_cast<long long>(at_epoch));
    return;
  }
  // **Una hora de debò i no una de 1970** (0.189.149): a board whose epoch nobody has set still counts its own monotonic
  // clock —the alarm is real— but the hour it would print is the first of January of 1970, which is a number a person would
  // read as a date. Without an hour the line says only what is left (docs/30, docs/60).
  char when[64] = {0};
  if (clock->epoch_ms() > 0) {
    char at[48];
    hour_text(at_epoch, at, sizeof(at));
    snprintf(when, sizeof(when), " for %s", at);
  }
  log_printf(pal::kLogInfo, "ALARM", "#%u armed on %s%s (%lld s from now)", static_cast<unsigned>(id),
             keeper_word(clock->wake_keeper()), when, static_cast<long long>((left + 999) / 1000));
}

// An alarm that is due is delivered here, between two turns of the app, so the
// app's coroutine is always suspended when its on_alarm runs.
// **La cua sencera, amb la més propera al davant** (0.189.149): the same list the door of an app writes when it adds or
// cancels one, so a board that comes back from the nvs says what it came back with —the formatter is the library's and this
// only puts it on the console, which is the one thing the alarm queue does not do for itself (docs/60).
void note_the_queue(const char* why) {
  const uint32_t count = alarms::count();
  if (count == 0) {
    log_printf(pal::kLogInfo, "ALARM", "%s: the queue is empty", why);
    return;
  }
  const int64_t mono_now = clock_mono();
  const int64_t epoch_now = clock_epoch();
  const int32_t offset = cfg::offset_minutes();
  const alarms::Entry* first = alarms::at(0);
  log_printf(pal::kLogInfo, "ALARM", "%s: %u of %u, the nearest is #%u (%s) in %lld s", why,
             static_cast<unsigned>(count), static_cast<unsigned>(alarms::max()),
             static_cast<unsigned>(first->id), first->app,
             static_cast<long long>((first->on_mono_ms - mono_now + 999) / 1000));
  for (uint32_t i = 0; i < count; ++i) {
    char line[160];
    if (alarms::describe(i, mono_now, epoch_now, offset, line, sizeof(line)) == 0) continue;
    log_printf(pal::kLogInfo, "ALARM", "  %s", line);
  }
}

void deliver_alarms() {
  if (g_platform == nullptr) return;
  alarms::Entry entry;
  int64_t now = clock_mono();
  // **La pàgina del davant s'aparta per la finestra i torna quan les finestres han acabat** (0.189.175, l'amo: *«la gràcia de
  // les alarmes és avisar a les apps quan toca»*). Amb un sol intèrpret, una app que no és al davant no es pot despertar si n'hi
  // ha una altra de viva, i la placa d'aquest sistema té una pàgina al vidre sempre (el llançador de `cfg home`): el registre de
  // l'amo del 18/11 deia `no window (app gone or no on_alarm)` a cada deu minuts, o sigui que la `battery` i el `meteo` no
  // prenien cap mostra de fons. La pàgina es tanca com la tanca un `raise` —comiats i sessió al disc— i torna amb `restore`
  // quan l'última finestra s'ha tancat, o sigui que la persona la troba com la va deixar (docs/30, docs/60, docs/75).
  //
  // **Una pàgina i no un munt**: s'aparta una vegada i les alarmes que vencen al mateix torn comparteixen l'apartada. Un `raise`
  // que caigui entremig es queda amb el vidre i la pàgina apartada no torna, que és el que ha demanat (docs/60).
  char parked[40];
  parked[0] = '\0';
  while (alarms::take_due(now, entry)) {
    // A light says an alarm went off even when the app's answer is a line on a console
    // nobody is looking at. It runs for its own cycles and then the light goes back to
    // what it was saying (docs/30).
    led::note(led::Trigger::kAlarm);
    const int64_t late = now - entry.on_mono_ms;
    const bool owner_running =
        luabind::running() && (strcmp(luabind::app_id(), entry.app) == 0);
    // **I que ha sonat es diu sempre** (0.189.149): a delivered alarm used to leave no line at all when the app was in front
    // and had its hook —the commonest case—, o sigui que el registre callava justament el cas que funciona i només parlava
    // dels tres que fallen. La línia diu de qui és, quant tard arriba, si es repeteix i quantes voltes s'ha perdut, i amb
    // quanta cua es queda el sistema després (docs/30, docs/60).
    log_printf(pal::kLogInfo, "ALARM", "#%u fired for %s, %lld ms late%s%s, %u left in the queue",
               static_cast<unsigned>(entry.id), entry.app, static_cast<long long>(late),
               (entry.repeat_ms > 0) ? ", repeating" : "", (entry.missed > 0) ? " (missed)" : "",
               static_cast<unsigned>(alarms::count()));
    if (owner_running && !luabind::call_alarm(entry, late)) {
      log_printf(pal::kLogWarn, "ALARM", "%u for %s: the app has no on_alarm",
                 static_cast<unsigned>(entry.id), entry.app);
    } else if (!owner_running && entry.raise_to_front) {
      // raise: the app of the alarm takes the foreground, and whatever was in
      // front of it is closed first (docs/60). **I se li diu què ha sonat**: the entry goes with the start, so the app's
      // `on_alarm` runs with the payload and the `meta` the window would have given it (0.190.7, docs/30, docs/60).
      log_printf(pal::kLogInfo, "ALARM", "%u raises %s", static_cast<unsigned>(entry.id),
                 entry.app);
      shell::raise_app(entry.app, &entry, late);
      // **I el que s'havia apartat no torna**: el vidre és de l'app que ha pujat (docs/60).
      parked[0] = '\0';
    } else if (!owner_running && entry.missed > 0) {
      // **Una tanda de mostres perduda no obre cap pàgina** (0.190.62, l'amo: *«veig que al sortir de sleep manualment passen les
      // dues apps com si tinguessin alarmes atrassades… Se suposa que el DeepSleep està atent a interrupcions d'rtc, de botons
      // físics, i opcionalment de tàctil»*). A repeating alarm that lost **whole** periods is the housekeeping of an app —a
      // sample of the battery, one of the weather— and `take_due` has already put it back at its next occurrence, o sigui que el
      // que queda és una finestra: tancar la pàgina de la persona, carregar l'app, llegir el sensor i tornar-la a obrir, per una
      // mostra que val el mateix d'aquí a deu minuts. **La placa va estar callada i el que ha de fer és tornar-se a posar al dia
      // en silenci**; l'app pren la mostra igualment la propera vegada que s'obre (les dues ho fan a l'arrencar la pàgina), i el
      // registre diu quantes voltes s'ha perdut. **Una alarma d'una persona no és això**: porta `raise`, i una que s'ha perdut
      // s'ensenya encara que la placa hagi estat dies parada (docs/30, docs/60).
      log_printf(pal::kLogInfo, "ALARM",
                 "#%u for %s was missed %u time(s) while the board was down: it is re-armed and no page is opened",
                 static_cast<unsigned>(entry.id), entry.app, static_cast<unsigned>(entry.missed));
    } else if (!owner_running) {
      // Nobody has the app loaded: it gets a window, and the page in front steps
      // aside for it and comes back when the windows are done (docs/30, docs/60).
      if (parked[0] == '\0' && shell::app_running()) {
        snprintf(parked, sizeof(parked), "%s", shell::app_id());
        shell::close_app(/*say=*/false);
        log_printf(pal::kLogInfo, "ALARM", "the page of %s steps aside for the window", parked);
      }
      if (shell::run_window(entry.app, entry, late)) {
        log_printf(pal::kLogInfo, "ALARM", "%u delivered to %s in a window (%lld ms late)",
                   static_cast<unsigned>(entry.id), entry.app,
                   static_cast<long long>(late));
      }
    }
    now = clock_mono();
  }
  // **I la pàgina torna com la va deixar la persona**: the app is started the way a wake starts it, with `restore`, so it
  // reads the `state` it parked a moment ago and its page is the one that was on the glass (docs/30, docs/60).
  if (parked[0] != '\0' && !shell::app_running()) {
    if (shell::restore_app(parked, nullptr)) {
      log_printf(pal::kLogInfo, "ALARM", "the page of %s is back", parked);
    } else {
      log_printf(pal::kLogWarn, "ALARM", "the page of %s did not come back", parked);
    }
  }
}

// **I el canvi de la llista d'apps, que només sent l'app que `cfg home` nomena** (0.189.23): `repo install` i `apps
// remove` deixen el canvi a la porta (`apps::note_a_change`) i es lliura aquí, entre dos torns, com una alarma —la coroutine
// de l'app és sempre suspesa quan el ganxo corre—. Si l'app que hi ha al vidre no és el llançador registrat, el canvi es
// llença i no es perd res: la propera arrencada de la pàgina torna a preguntar la llista a la porta i la veu sencera
// (docs/30, docs/40).
void deliver_the_apps_change() {
  if (g_platform == nullptr) return;
  uint32_t generation = 0;
  bool installed = true;
  while (apps::take_the_change(generation, installed)) {
    const char* home = cfg::home_app();
    const bool the_launcher = luabind::running() && home != nullptr && home[0] != '\0' &&
                              strcmp(luabind::app_id(), home) == 0;
    if (!the_launcher) continue;
    const char* what = installed ? "install" : "remove";
    if (!luabind::call_apps_event(what, generation)) {
      log_printf(pal::kLogWarn, "APPS", "the list changed (%s) and %s has no on_event", what, luabind::app_id());
    } else {
      log_printf(pal::kLogInfo, "APPS", "%s hears the change (%s), generation %u", luabind::app_id(), what,
                 static_cast<unsigned>(generation));
    }
  }
}

// **Where the time of the interpreter's instructions is spent**: the band's own wait, which on the PC
// is a sleep and on the board is never asked for, because a board's instruction already takes the time
// it takes (docs/85).
void spend_the_cpu(uint32_t us) { pal::delay_us(us); }

// **What one instruction costs on the band being run**: asked once per block of instructions, and zero on
// a board, where the time is the real one (docs/85).
uint32_t instruction_ns() {
  return (g_platform != nullptr) ? g_platform->simulated_instruction_ns() : 0;
}

void dispatch(const pal::Event& ev) {
  switch (ev.type) {
    case pal::kEventSystem:
      if (ev.a == 1) g_running = false;
      break;
    case pal::kEventConsoleRx:
      // Nothing to do here: the console is drained by the caller.
      break;
    case pal::kEventTimer:
      break;
    // **La línia del vidre només ha despertat el torn** (0.190.119): the frame is read by `touch::poll` below, which asks the chip every turn
    // it hears a line; l'event no porta res i no ha de fer res (docs/50).
    case pal::kEventTouch:
      break;
    default:
      log_printf(pal::kLogDebug, "SYS", "unhandled event type %u", static_cast<unsigned>(ev.type));
      break;
  }
}

}  // namespace

void boot(hal::IPlatform& platform) {
  // **El corrent de la placa, abans de qualsevol altra cosa** (0.189.24, l'amo: *«El poweroff deixa la placa tonta? no puc
  // arrencar-la sense punxarla a un usb?!»*): a board whose supply is held by a pin —the Paper, with the SY7088— gives up
  // about a second after the key, and this boot spends it in the console, the mounts and the splash of the glass. It was
  // asked for inside `caps()`, which comes after the splash, so on the battery the board switched itself off half way
  // through its own boot; the maker's own `M5EPD::begin()` enables the main power as its very first instruction (docs/50).
  // The board already did it in its own entry point, and doing it twice costs a call on a pin that is already high.
  platform.hold_the_power();
  // **The clock of the meters starts with the boot**, so the uptime of `stats` covers the boot too and
  // the doors can never add up to more than the life of the system (docs/30).
  (void)meter::uptime_ms();
  // **How long the system takes to come up**: from here to the prompt, which is the number a person
  // feels every time the board is switched on (docs/30, docs/95).
  meter::Scope boot_time(meter::kBoot);
  g_platform = &platform;
  g_running = true;
  // **El run nou comença net** (0.190.68): un son profund que ha estat una arrencada del xip deixa el flag posat perquè el torn d'abans
  // s'aturi, i aquesta línia és el que el torna a baixar —si no, el torn següent el llegiria i es tornaria a arrencar el nucli per sempre
  // més— (docs/60, docs/85).
  power::clear_the_restart();
  log_init();
  // **A call from an app into the system is counted from the first line an app runs**: the interpreter
  // asks for the clock and for a way to add, and knows nothing else about the kernel (docs/30).
  luabind::set_meter_ops(
      luabind::MeterOps{&meter::now_us, &app_call_cost, &app_run_cost, &app_extra_cost,
                        &lua_calls_total, &app_own_cost, &instruction_ns, &spend_the_cpu});
  // The settle is for the board: the UART bridge leaves a run of bytes behind
  // when the port opens. On the PC there is no such noise, and the bytes that do
  // arrive (a pipe, a script, a hand on the keyboard) are for the person.
  if (!platform.emulator()) settle_console();
  print_banner();

  // The filesystem is mandatory: it is where the apps live. If it does not mount,
  // the system still boots, says so in the log, and the prompt is what is left.
  vfs::init(platform);
  // **What the door of the apps presents is the system's to say** (0.88.20): a folder of the flash or of the card that
  // carries no `manifest.json` is not an app, and from here it is not listed, not run and not walked by anyone looking for
  // a handler. The door is the one entry point of the apps —the `run`, the listing and the files of an app all go through
  // it— so a folder the system does not present is a folder nothing reaches (docs/40, docs/70).
  vfs::set_apps_presenter(&apps::is_an_app);
  // **And the faces of the app that runs are in `/fonts`** (0.174.1): the door joins three places and the third is the
  // app's own `apps/<id>/fonts`, which is the one the hook answers (docs/30, docs/70).
  vfs::set_fonts_app(&luabind::app_id);

  // `/cfg` is not a disk but a door, and this says which unit is behind it: the card when
  // there is one, the flash otherwise, or whatever the person asked for (docs/70). It
  // goes right after the mounts because it is one, and before any app can write a setting.
  // The count is said after both, so the log and `mounts` cannot disagree.
  cfg::boot(platform);
  log_printf(pal::kLogInfo, "VFS", "%u mounts", static_cast<unsigned>(vfs::mount_count()));

  // **D'on ve aquesta arrencada, decidit abans que res no pinti** (0.189.180, l'amo: *«Abans de tocar la pantalla. s'ha de saber
  // si es ve d'un deepsleep, una arrencada nova o un reinici. Segons d'on es vé es farà un arranc d'una manera o unaltre»*): the
  // power, a restart of a chip that never lost it, or a deep sleep that goes on. La resposta es diu al registre amb la seva
  // paraula i la resta de l'arrencada la llegeix —el `state` de les pàgines, la targeta del començament i el que vingui— en lloc
  // de desfer el camí cadascú pel seu compte (docs/50, docs/60, docs/75).
  const power::BootKind booted = power::note_the_boot(platform);

  // **Un despert per una alarma no toca el vidre** (0.190.19, l'amo: *«Al despertar de DeepSleep, si es una alarma no has de tocar la
  // pantalla per res … el procés es realitza i tornem a deepsleep sense molestar ni gastar pantalla»*): the glass keeps the image the sleep
  // left on it, and this switch is what makes every paint of this run stay in memory instead of going to the panel —la barra del boot,
  // la pàgina d'una finestra, el que sigui—. **L'app que demana el vidre el desperta** (`shell::run_app`), que és la meitat que l'amo vol:
  // una alarma que només treballa de fons no canvia ni un píxel de la pantalla (docs/30, docs/50, docs/60).
  // **I s'apaga també explícitament**, perquè el que decideix és aquest boot i no el de la prova d'abans (docs/80): the switch is the
  // state of a run and a run that is not a wake for an alarm leaves it where a glass is: awake (docs/30, docs/50).
#if LUNOK_GLASS
  display::set_the_glass_asleep(booted == power::BootKind::kSleep && power::woken_by_an_alarm());
#endif

  // **La forma de cada pàgina comença de zero a cada arrencada de debò** (0.189.167, l'amo: *«el session state de totes les
  // apps s'ha d'esborrar al iniciar el sistema»*), **i no quan la placa torna d'un son profund** (0.189.176, el mateix amo:
  // *«Al tornar d'un deep sleep no has d'esborrar les sessions state»*). The `state` block is the shape of the page a person
  // left, and a board that starts starts at the beginning: a tab, a page of a list and a ring of yesterday are a view of a run
  // that is over —and of an app that may not even have that shape any more—. But a deep sleep is **not** the end of a run: the
  // board goes down with the page of a person and comes back to it, o sigui que el que hi havia al disc és exactament el que
  // `restore_session` ha de trobar (docs/30, docs/45, docs/60, docs/75).
  if (booted == power::BootKind::kSleep) {
    log_printf(pal::kLogInfo, "MEMORY", "the pages come back with the board: the 'state' of the apps stays");
  } else {
    // **I amb les paraules de l'arrencada i no les de la comanda**: `say` és fals perquè una línia de «0 file(s)» a cada
    // arrencada és un registre que no es pot llegir, i només es diu quan hi havia alguna cosa a oblidar (docs/30).
    const session::Forgotten old_shapes = session::forget_key("state", /*say=*/false);
    if (old_shapes.files > 0 || old_shapes.blocks > 0) {
      log_printf(pal::kLogInfo, "MEMORY", "the pages start from the beginning: %u file(s) of 'state' forgotten",
                 static_cast<unsigned>(old_shapes.files));
    }
  }

  // **The panel and the logo, as early as this system can put them there** (0.115.0): the filesystem is mounted
  // (the picture is a file) and the settings have been read (the picture is the one the person chose), and
  // everything that comes after this —the alarms that survived the power cut, the radio, the sensors, the bar—
  // runs while the glass is already saying lunokOS. It is the same call the command `splash boot` makes, and a
  // board without a glass answers `no glass` and goes on with the log, which is the only place that sentence can
  // go (docs/30, docs/50).
  // **A board with no glass skips the page whole** (0.179.9): the panel, the picture of the boot and the wash the first
  // page owes are one thing, and a board without a screen has neither the code nor the settings that name a drawing
  // (`LUNOK_GLASS`, docs/50, docs/75). What follows —the alarms, the radio, the sensors— is the same on both.
#if LUNOK_GLASS
  display::boot(platform);
  // **La bateria decideix la imatge de l'arrencada** (0.183.0): the gauge is asked here and not by `power::boot`, which has
  // not run yet —it is after the shell— because this is the one page a person sees before anything else can tell them, and
  // a board with a cell under the mark has to say it before it says its name. `power::boot_picture` is the rule and this is
  // the reading (docs/50).
  int boot_mv = 0;
  int boot_pct = 0;
  bool boot_charging = false;
  hal::IPower* gauge = platform.power();
  const bool battery_known = gauge != nullptr && gauge->battery(boot_mv, boot_pct, boot_charging);
  const splash::Kind boot_moment =
      power::boot_picture(cfg::boot_splash(), battery_known, boot_pct, boot_charging);
  if (boot_moment == splash::kLowBattery) {
    log_printf(pal::kLogWarn, "POWER", "the battery is at %d %% and there is no cable: the picture says so",
               boot_pct);
  }
  // **I la placa que torna d'un son no torna a ensenyar la seva targeta** (0.189.180): el vidre té la imatge del son i el que ve
  // després és la pàgina de la persona —el camí de tornada el sap `restore_session`—; una targeta de més són dos parpelleigs per
  // res. La pàgina que arriba sí que deu el rentat, perquè el que hi ha al vidre és una altra cosa. Una bateria baixa és
  // l'excepció: és una cosa que la persona ha de saber i no una targeta de cortesia (docs/30, docs/50, docs/60).
  // **I un vidre que no s'ha de tocar no rep cap imatge** (0.190.19): a wake for an alarm runs with the panel exactly as the sleep left it,
  // o sigui que ni la targeta del boot ni la de la bateria baixa s'hi pinten —la bateria baixa es diu al registre i la pàgina de la persona
  // la trobarà quan hi sigui— (docs/30, docs/50, docs/60).
  const bool show_it =
      ((booted != power::BootKind::kSleep) || (boot_moment == splash::kLowBattery)) && !display::the_glass_is_asleep();
  if (!show_it) {
    ui::owe_the_page();
  } else if (!splash::show(boot_moment)) {
    log_printf(pal::kLogDebug, "SPLASH", "no boot picture: %s", splash::last_error());
  } else {
    // **The first page of the system washes the picture away** (0.179.5): the splash paints the panel whole and none of its
    // ink passed through the page, so the page that covers it is owed whole and clean —which was the `gc16` every launcher
    // asked for at the top of its first page, said once here where the system is the one who knows what is on the glass—
    // (docs/30, docs/50).
    ui::owe_the_page();
  }
#endif  // LUNOK_GLASS

  // The alarms that survived the power cut come back before anything else can
  // look at the queue.
  alarms_restore();

  // From here an app may read files: the interpreter gets the VFS through these
  // four operations and never depends on it (docs/70).
  install_fs_ops();
  // **And the hour of the person beside the disk** (0.125.0): an app that reads a clock reads a date and an hour, and
  // which hour is the person's is a setting of the system (docs/30, docs/60).
  install_time_ops();
  // **I els ajustos, llegits per una app** (0.189.13): a page that has to obey a switch —the launcher and `sort apps by
  // usage`— reads what `cfg` prints, and the prompt is the only door that writes one (docs/30, docs/40).
  install_settings_ops();
  // **I la taula de comptadors, per a una pàgina** (0.189.108): `sys.stats()` is how the panel of the settings says what a turn
  // of the interpreter costs on this board instead of guessing it, and the numbers are the ones `stats` prints (docs/30, docs/75).
  luabind::set_stats_ops(luabind::StatsOps{&meter::stats_count, &meter::stats_name, &meter::stats_read});
  // **The door of the apps, for a launcher** (0.153.0): `apps.list` and `app.run` are the two calls a launcher is made
  // of, and they are the door's and not a second reader of the manifests (docs/30, docs/40).
  apps::install_apps_ops();
  // **And the shell is a thing an app can ask for** (0.159.0): `sys.shell()` gives it a session of the system's own
  // shell with the three pipes of one, so the prompt is not the only way in and an app can run a command and read
  // what it said. The table is handed over here because `luabind` never includes the kernel (docs/20, docs/30).
  shell::install_shell_ops();
  // **And the memory of a session** (0.179.0): `sys.session(key, bytes)` is a block the kernel keeps for the app that asks
  // —the key is the app's and not the system's— until the board goes off, and **it is the first thing the allocator gives
  // up when a block does not fit**, because it is the only memory of the system that can be made again (docs/30, docs/75).
  // The relief is one function pointer and the block is freed from inside the allocator that refused (docs/75).
  pal::set_memory_relief(&relieve_memory);
  luabind::SessionOps session_ops = {};
  session_ops.acquire = &session_acquire;
  session_ops.release = &session_release;
  session_ops.destroy = &session_destroy;
  session_ops.generation = &session::generation;
  session_ops.read = &session_bytes_read;
  session_ops.write = &session_bytes_write;
  session_ops.clear = &session_bytes_clear;
  luabind::set_session_ops(session_ops);
  // **I el port sèrie, que una app pot demanar per a ella** (0.189.20): `serial.claim()`, `serial.release()` i
  // `serial.claimed()` des de Lua, amb el nom de l'app posat aquí (docs/30, docs/50).
  luabind::SerialOps serial_ops = {};
  serial_ops.claim = &serial_claim;
  serial_ops.release = &serial_release;
  serial_ops.claimed = &serial::claimed;
  luabind::set_serial_ops(serial_ops);
  net::boot(platform);
  // **The apps are not looked at here on purpose**: `/apps` is walked when a command asks for it
  // (`apps`, `ls`, `run`), so the boot does not depend on how many apps the disk carries (docs/10, docs/40).

  uint32_t present = 0;
  uint32_t total = 0;
  count_caps(present, total);
  log_printf(pal::kLogInfo, "BOOT", "board=%s caps=%u/%u", platform.id(),
             static_cast<unsigned>(present), static_cast<unsigned>(total));

  // Where the epoch comes from, said once at boot: a board with no epoch is not
  // broken, it just cannot answer `time.now()` until somebody sets it (docs/60).
  hal::IClock* clock = platform.clock();
  if (clock == nullptr) {
    log_printf(pal::kLogWarn, "CLOCK", "none");
  } else {
    const int64_t boot_epoch = [&]() {
      meter::Scope scope(meter::kClockNow);
      return clock->epoch_ms();
    }();
    log_printf(pal::kLogInfo, "CLOCK", "source %s epoch %lld", clock->source(),
               static_cast<long long>(boot_epoch));
  }

  // The light and the button: what this board does with a person. Both are optional
  // and both say so, so a board without them boots the same (docs/50).
  // **I la llum del vidre no s'encén si aquesta arrencada és una alarma** (0.190.161, l'amo: *«Quan es desperta d'un deepsleep per alarma
  // no s'ha d'encendre el backlight i tornar a dormir ... Menys que l'app demani "raise"»*): the page of the sleep stays on the glass and
  // the board goes down again, o sigui que el llum espera una pàgina que algú hagi demanat (docs/30, docs/50, docs/60).
  led::boot(platform, (booted == power::BootKind::kSleep) && power::woken_by_an_alarm());
  button::boot(platform);
  // The sensor of the board, on the band that has one: the Lite says it has none and
  // imu.read() answers that, instead of the API being absent (docs/50).
  imu::boot(platform);
  // The air, the same way: the Paper has an SHT30 and the ATOMs have nothing, and env.read() answers
  // which of the two this is (docs/30, docs/50).
  env::boot(platform);
  // The glass, when the board has one: its frames become taps, long presses and drags in the turns
  // of this loop (docs/50).
  // **And the page, which a board without a glass does not have** (0.179.9): the finger, the bar, the canvas of the app,
  // the reader of a book and the keyboard are one system and the same `LUNOK_GLASS` decides all of them, so the four
  // boards without a panel do not carry a line of any of it (docs/30, docs/50, docs/75).
#if LUNOK_GLASS
  touch::boot(platform);
  // The bar of the system, which owns the top rows of the glass: the hour, the title of the app in front
  // and the charge (docs/30).
  bar::boot(platform);
  // **The card of the system that hangs from the bar** (0.179.22): empty for now, and with nothing to boot but the fact that
  // no boot of before left it out (docs/30).
  panel::boot();
  // The canvas an app draws on, at the size of the glass **under the bar**, made once and re-made only
  // when the system asks for another size. The board's buffer stays the system's (docs/30).
  ui::boot(platform, bar::height());
  // **La franja es torna a pintar quan la lletra del disc ja hi és** (0.189.57): `bar::boot` painted before `ui::boot` read the
  // face of the disk, so the first strip was drawn with the table of the flash and every paint after it with the face of the
  // system —and the hour, which is the one box of the strip that waits for its own minute, stayed five rows above the title
  // beside it (found by the owner on the glass, 15/11). A strip owed whole is a strip painted whole at the first turn.
  bar::refresh();
  // **The book of the disk is given to the interpreter** (0.132.0): the door of Lua asks the kernel what a mount, a
  // zip and a page are, and it asks through a table of calls, the same way the canvases are (docs/20, docs/46).
  book::bind();
  // **The keyboard the settings chose** (0.93.0): the UI says which children it carries, and a name that is not one of
  // them falls back to qwerty with a line, the same way a unit that is not there does; the boot does not stop for a
  // setting (docs/30, docs/70).
  if (!ui::keyboard_select(cfg::keyboard())) {
    log_printf(pal::kLogWarn, "CFG", "no keyboard '%s' in this build; qwerty stays", cfg::keyboard());
    ui::keyboard_select("qwerty");
  }
#else
  // **Said once, and in the log**: a board with no page is a board whose apps are words and keys, and a line that says so
  // is the difference between a system that is small on purpose and one that lost its screen (docs/50).
  log_printf(pal::kLogInfo, "UI", "no glass on this board: no page, no reader and no faces");
#endif  // LUNOK_GLASS
  // **The prompt is the last thing the boot writes**, and it is not a detail: the lines of the hardware
  // say what this board is, and one of them after the prompt is a line that looks like an answer to
  // something the person has not typed (measured on the board and on the emulator, 1/11: `ui: view ...`
  // came out after `lunok> `).
  shell::start();
  // **Les alarmes que van tocar mentre la placa no hi era es fan abans que cap pàgina torni** (0.189.175): a board that wakes
  // from a deep sleep is a board that boots, and the session app used to come back *before* the queue was looked at, o sigui
  // que la finestra de la mostra es refusava i la nit sencera es perdia. Amb el vidre buit la finestra és la de sempre, i
  // l'app que la persona hi tenia torna després (docs/60, docs/90).
  deliver_alarms();
  // **I un despert per una alarma no és un despert per a una persona** (0.189.182, l'amo: *«un gestor que permeti arrencar sense
  // canviar res de la pantalla, executar l'app amb les dades de l'alarma, esperar a que acabi i tornar al deepsleep»*): si el que
  // ha despertat la placa és el rellotge del xip —i l'alarma no ha demanat ningú amb `raise`—, el que ha vingut a fer-ho ja està
  // fet (les finestres de dalt), el vidre té la imatge del son i la pàgina de la persona espera. El pany i la casa no s'obren, i
  // el bucle la torna a dormir al primer torn (docs/60, docs/90).
  const bool for_an_alarm = (power::boot_kind() == power::BootKind::kSleep) && power::woken_by_an_alarm() &&
                            !shell::app_running();
  if (for_an_alarm) {
    log_printf(pal::kLogInfo, "POWER", "nothing to paint: the board came back for an alarm and goes down again");
  } else {
    // **I quan hi ha pàgina, la llum sí** (0.190.161): a `raise` has put an app in front, the session is coming back or a person is
    // here, i el vidre s'il·lumina amb el nivell que la persona va triar —que la 0.190.161 li havia deixat a zero perquè venia d'una
    // alarma— (docs/30, docs/60).
    led::the_glass_is_wanted();
  }
  // Last, because it speaks to the console and may bring an app back: a board that wakes from a deep
  // sleep is a board that boots, and the session record is what makes the app come back where it was
  // (docs/90).
  power::boot(platform, for_an_alarm);

  // **L'ARRENÇADA DEMANA EL TOPALL I EL SISTEMA CARREGAT EL DEIXA ANAR** (0.179.38): the mount, the panel, the settings,
  // the picture of the boot and the shell are the heaviest second of this system, and nothing of it is worth a processor
  // at a hundred and sixty. The window is the boot's own and has no clock: the line below the home app is what steps it
  // down, because only here is it known that the system is loaded (docs/50, docs/60).
  power::hold_cpu(power::kBusyMhz, 0, "boot");

  // **The app the settings call home** (0.119.0): a board that has one comes up showing that page instead of the
  // prompt —a launcher, a clock, the page a person wants to see—. It goes last and only when nothing came back,
  // because the session record is the more precise answer (a board that went to sleep inside an app returns to that
  // app and not to the launcher) and a home app never starts on top of anything. A name that is not on the disk is a
  // line in the log and the prompt: a boot that insists on an app that is not there is a boot loop nobody asked for
  // (docs/30, docs/40, docs/90).
  // **I la pàgina del pany és la primera** (0.189.159, l'amo: *«Arrenca abans el lock i després si hi ha home definida l'arrenques
  // desde device_lock»*): el commutador de `cfg.lock_on` i l'app que `cfg lock` nomena decideixen si el vidre s'obre tapat. Si
  // sí, **la targeta del pany és la del començament** i la casa no s'arrenca aquí —la demana la mateixa pàgina del pany quan
  // s'obre (`device_lock` llegeix `cfg home`)—, perquè una casa que s'arrenca darrere la cortina és una casa que no hi és quan
  // la cortina s'aixeca (docs/30, docs/40, docs/60).
  const char* lock_page = (for_an_alarm) ? nullptr : lock::boot();

  const char* home = cfg::home_app();
  if (lock_page == nullptr && home[0] != '\0' && !for_an_alarm && !app_running()) {
    // **I ARRENCA SENSE LA SEVA TARGETA** (0.179.33, l'amo: *«en launcher no ha de mostrar l'splash rera el boot, tenim
    // l'splash del sistema»*): the board has just painted the picture of its boot on that glass, so the home app comes up
    // with the page and nothing else. Its card is for the way back —a Lua app ends and the launcher arrives with it—
    // which is the moment it was drawn for (docs/30, docs/40).
    if (shell::start_app(home, /*splash=*/false)) {
      log_printf(pal::kLogInfo, "HOME", "%s", home);
    } else {
      // **La cara del sistema que no té pàgina** (0.183.0): the home app is the page this board is supposed to present, and a
      // boot that cannot present it is a system error and not a line in a log nobody is reading. The picture goes up on the
      // glass with the reason under the stamp of the build, and the prompt is still the console's, which is the only door a
      // board like this has left. **A panic cannot be painted** —the chip that panics is not driving the glass— so this is
      // the one error of the system a person can be told about (docs/50).
      log_printf(pal::kLogWarn, "HOME", "no app '%s' on this disk; the prompt is yours", home);
      // **I la imatge és de la pàgina** (0.189.112): `splash.h` and `ui.h` are the two headers this build does not carry
      // without a glass —`no_glass.cpp` gives the shell the doors it asks in spite of that, and this one is a picture and
      // not a door—, so a board with no screen says the line and stays at the prompt, which is all it ever had (docs/50).
#if LUNOK_GLASS
      if (!splash::show(splash::kOops)) {
        log_printf(pal::kLogDebug, "SPLASH", "no picture of the error: %s", splash::last_error());
      } else {
        ui::owe_the_page();
      }
#endif
    }
  }

  // **I el sistema carregat treballa a la velocitat de repòs** (0.179.38): the boot is one window and the home app is part
  // of it —its first page is part of what the boot lays down, and a launcher that is arriving is not worth a second burst—,
  // so this is where the board goes back to a hundred and sixty. An app started after the boot —`run`, another app, an
  // alarm— asks for its own window (docs/50, docs/60).
  power::rest_cpu("the system is loaded");
}

void alarms_restore() {
  if (g_platform == nullptr) return;
  hal::IKeyValue* nvs = g_platform->nvs();
  if (nvs == nullptr) return;

  const int64_t epoch = clock_epoch();
  if (epoch <= 0) {
    // Without an epoch the stored lines cannot be placed on the monotonic clock,
    // and firing them at the wrong time is worse than saying nothing (docs/60).
    log_printf(pal::kLogInfo, "ALARM", "no epoch yet, nothing restored");
    return;
  }

  char blob[kAlarmBlobBytes];
  size_t len = 0;
  if (!nvs->get(kAlarmNs, kAlarmKey, blob, sizeof(blob) - 1, len)) return;
  blob[len] = '\0';

  if (alarms::load(clock_mono(), epoch, blob)) {
    log_printf(pal::kLogInfo, "ALARM", "%u restored from nvs",
               static_cast<unsigned>(alarms::count()));
    note_the_queue("the queue of the board");
    g_alarms_stored = true;
  } else {
    log_printf(pal::kLogWarn, "ALARM", "the nvs queue could not be used");
    nvs->erase(kAlarmNs, kAlarmKey);
    g_alarms_stored = false;
  }
  // Loading is not a change to save back; the queue as it is is what the next
  // change will write.
  g_saved_revision = alarms::revision();
}

// **Els dos comptes de la volta** (0.189.85), llistats per `stats`: quants torns venen d'un esdeveniment i quants de l'espera
// que s'ha esgotat.
uint32_t g_turns_with_event = 0;
uint32_t g_turns_on_timeout = 0;
void turns(uint32_t& with_event, uint32_t& on_timeout) {
  with_event = g_turns_with_event;
  on_timeout = g_turns_on_timeout;
}

void poll() {
  if (!g_running) return;
  // **The turn of the kernel, counted whole**: what it costs to keep the system going, with the waiting
  // between turns left out because the wait is not the kernel working (docs/30).
  meter::Scope turn(meter::kLoop);

  // An app that waits for a promise must not wait for the next event: the sleep
  // is cut down to its next deadline, or it would never wake up on time.
  int32_t wait = kIdleTimeoutMs;
  // A command that waits for the radio needs the same short turns an app does:
  // its answer is checked in every turn, never waited for (docs/30).
  if (shell::native_running()) wait = kAppIdleMs;
  if (shell::app_running()) {
    const int32_t app_wait = shell::app_wait_ms();
    if (app_wait < 0) {
      wait = kAppIdleMs;
    } else if (app_wait < wait) {
      wait = app_wait;
    }
  }
  // The nearest alarm is the only thing besides the app that can make the loop
  // wake up sooner than its idle timeout (docs/60).
  const int64_t next_alarm = alarms::next_due_mono_ms();
  if (next_alarm >= 0) {
    const int64_t left = next_alarm - clock_mono();
    const int32_t alarm_wait = (left <= 0) ? 0 : static_cast<int32_t>(left);
    if (alarm_wait < wait) wait = alarm_wait;
  }
  // The same alarm as an hour, so the power policy can sleep to it without knowing what an
  // alarm is. Zero when there is none to wait for, or when nobody has set the hour (docs/60).
  int64_t next_alarm_epoch = 0;
  if (next_alarm >= 0 && g_platform != nullptr) {
    hal::IClock* clock = g_platform->clock();
    int64_t epoch = 0;
    if (clock != nullptr) {
      meter::Scope scope(meter::kClockNow);
      epoch = clock->epoch_ms();
    }
    if (epoch > 0) next_alarm_epoch = epoch + (next_alarm - clock_mono());
  }

  // The light is painted at the end of every turn, so a turn that arrives late paints the
  // frame of *that* moment: cutting the wait is all an animation needs. No thread, no queue
  // of frames it owes, and nothing that can fall behind (docs/30).
  const int32_t frame_wait = led::next_frame_ms();
  if (frame_wait >= 0 && frame_wait < wait) wait = frame_wait;

  // **And the hand comes before all of it** (0.179.21): an alarm, an app's deadline or a second of nothing are all worth
  // less than a press that arrives now. A board with no key and no glass —the T-Koala— keeps the long wait, because there
  // is no hand to miss (docs/30, docs/50).
  //
  // **I la mà mana sobre la becaina** (0.190.71): the turn is ten milliseconds when the board has a key or a glass, i el que pot esperar
  // és **el que un dit no nota**: quaranta mil·lisegons si la placa dorma entre voltes, deu si no. La 0.190.70 va deixar l'espera en un
  // segon i el tàctil es va morir de latència; la 0.190.72 la va deixar en quaranta i es va morir igual —perquè el que el matava no era
  // l'espera sinó la línia del panell que la becaina deixava girada (0.190.74, `touch::rearm`)— (docs/30, docs/50, docs/95).
  bool hand = button::available();
#if LUNOK_GLASS
  hand = hand || touch::available();
#endif
  if (hand) {
    const int32_t most = (power::nap_on() && power::can_nap()) ? power::nap_slice_ms() : kInputPeriodMs;
    if (wait > most) wait = most;
  }

  // **Què ha despertat la volta** (0.189.85): a turn that came back because something happened and a turn that came back
  // because its ten milliseconds were over are two different lives for the board, and the wait is measured apart from the
  // work so that «the loop costs eighteen milliseconds» stops being a number nobody can take apart (docs/95).
  pal::Event ev;
  bool woke = false;
  {
    meter::Scope waiting(meter::kQueueWait);
    // **Esperar, o deixar que el xip dormi?** (0.190.63, l'amo: *«[X] Light sleep instead of wait. Aplica lightSleep allá on puguis»*;
    // 0.190.178, *«activem el lightsleep automàtic d'idf»*). La volta del nucli espera pel que hi hagi a la cua fins a un segon quan no
    // hi ha cap app, i tenir el processador despert per fer-ho és el corrent més fàcil d'estalviar: `open_the_sleep` obre les portes de
    // la placa —una tecla, el dit i la consola— i **deixa que la gestió de potència adormi el xip dins del `pop`**, que és qui sap
    // quant falta pel proper temporitzador i qui compensa el tick quan el xip s'ha aturat. Torna zero quan aquesta volta no s'ha de
    // dormir —el commutador apagat, un dit a sobre, el port en ús o cedit, una placa que no en sap— i llavors l'espera és exactament la
    // d'abans (docs/50, docs/60, docs/95).
    const int32_t napping = power::open_the_sleep(wait);
    const int64_t waited_from_ms = pal::system().millis();
    woke = pal::system().queue()->pop(ev, (napping > 0) ? napping : wait);
    power::close_the_sleep(pal::system().millis() - waited_from_ms);
    power::note_the_nap_end(woke);
#if LUNOK_GLASS
    // **I la línia del panell torna a ser una vora de baixada** (0.190.74): the nap arms that GPIO as a level wake and the chip leaves it
    // that way; without this the glass answers nothing after the first nap —which is exactly what happened twice on the Paper— (docs/50).
    // **Però només si el tàctil és una porta de la becaina** (0.190.167, el registre de la M5 Paper del 21/11: `I (39632) gpio:
    // GPIO[36]| InputEn: 1 …` set vegades en quatre-cents mil·lisegons): amb *Wake on touch* apagat —que és com ve de sèrie— el nap no toca
    // aquell pin i el que cal no és refer-lo sinó no fer-hi res, o sigui que el que era una crida per torn és una crida al torn que en té
    // (docs/50, docs/60, docs/95).
    if (napping > 0 && power::wake_on_touch()) touch::rearm();
#endif
  }
  if (woke) {
    ++g_turns_with_event;
    dispatch(ev);
  } else {
    ++g_turns_on_timeout;
  }

  // An alarm that is due is not an event that waits in a queue: it is checked
  // here, between turns, so the wait above had to be cut to its deadline.
  // The network is asked about in every turn, never waited for: an association
  // that is taking its time must not stop the prompt or the alarms (docs/30).
  net::poll();

  deliver_alarms();
  arm_nearest_alarm();

  // The button is asked in every turn, and its press is turned into an event right
  // here: same place as the alarms, same reason (docs/30).
  button::poll();
  // The glass, in the same turn: a frame becomes a tap, a long press, a drag or two fingers here,
  // and not in the app, so every board makes the same gestures out of the same points (docs/50).
#if LUNOK_GLASS
  touch::poll();
  // **And the three taps of an app that asked for the whole glass are the system's** (0.136.0, docs/46 D1): the gesture
  // is taken out of the queue here, before the app's turn below, so the app neither handles it nor hears it and the bar
  // always has a way back (docs/30).
  //
  // **And they are not asked while the card of the system is out** (0.179.42): a triple tap below the card is a tap outside
  // it, and the way out it asks for is not the system's while the card is the page's (docs/30).
  if (!ui::page_held()) ui::poll_full_screen();
  // **And the hand of the panel** (0.179.22): a drag down from the bar pulls the card of the system out, and while it is out
  // the gestures the panel did not take are nobody's —the app under it never learns that the panel exists— (docs/30).
  panel::poll();
#endif  // LUNOK_GLASS

  // **I la pantalla que es tapa, entre torns** (0.189.150): the counter of the idle minutes and the page a sleep owes. It goes
  // here and not inside the sleep or inside a hook because starting an app closes the one in front, and that is a thing the
  // kernel does between turns and never from the middle of somebody's call (docs/30, docs/60).
  lock::poll();

  // **L'APP NO CORRE MENTRE LA TARGETA DEL SISTEMA ÉS A SOBRE** (0.179.42): the card hangs from the bar, the fingers on it
  // are the panel's and the ink of the page under it is held, so the turn of the app is a turn under a page that nobody is
  // looking at —and the one way an app could end the card by itself—. The owner asked for it in one sentence: *«un cop el
  // desplegable es a la pantalla l'app es queda en pausa … Es deixa com estava i es resumeix l'execució de l'app lua»*. The
  // coroutine of the app is suspended between turns anyway, so what a pause costs is its next turn and nothing else: the
  // alarms, the light, the network and the prompt keep running, and the app goes on from where it was the moment the card
  // goes (docs/30, docs/50).
#if LUNOK_GLASS
  const bool the_card_is_out = panel::open();
#else
  const bool the_card_is_out = false;
#endif  // LUNOK_GLASS

  // A sleep an app asked for happens here, between turns, with the app's coroutine
  // suspended: the interpreter is not closed from inside its own call (docs/30).
  // The queue is written down **before** the board may go: a queue that changes in the same
  // turn the board powers off would be an alarm nobody kept (docs/60).
  if (alarms::revision() != g_saved_revision) save_alarms();
  power::poll(next_alarm_epoch);

  // **I un son profund que ha estat una arrencada del xip acaba el torn aquí mateix** (0.190.68): a la placa la crida no torna mai, o
  // sigui que res del que ve després d'aquest punt s'executaria; a l'emulador el procés segueix viu i el que queda del torn seria feina
  // d'un run que ja no hi és —una barra pintada, el cursor d'una app tancada, un byte de consola donat a un intèrpret que ha deixat de
  // ser-hi—. El `kernel::boot` el fa qui porta l'executable, que és el que fa el xip sol (docs/50, docs/60, docs/85).
  if (power::the_chip_restarted()) return;

  pal::IConsole* console = pal::system().console();
  if (console != nullptr) {
    char buf[64];
    size_t n = console->read(buf, sizeof(buf));
    while (n > 0) {
      // A person is at the keyboard: the board is being used and does not put itself to
      // sleep until the prompt has been quiet for a while (docs/60). **Un comptador i prou**: `lock::note_activity` mou el de
      // `power` des de la 0.190.9 (docs/30, docs/60).
      lock::note_activity(power::Hand::kConsole);
      shell::feed(buf, n);
      n = console->read(buf, sizeof(buf));
    }
  }
  // **I un `deepsleep` escrit al prompt és el mateix que la baixada de la política** (0.190.68): la línia ha adormit el xip, o sigui que
  // el que queda del torn —l'app, el cursor, la barra— seria feina d'un run que ja s'ha acabat i el torn s'acaba aquí (docs/50, docs/60).
  if (power::the_chip_restarted()) return;

  // The app gets its turn after the events and the console, so a byte that has
  // just arrived is already queued when it wakes up. **And it does not get it while the card of the system is out**
  // (0.179.42, the pause above): the coroutine waits, the hooks wait, and the page under the card is left as it was.
  if (!the_card_is_out) shell::poll_app();

  // **The caret of the keyboard blinks without the app** (0.92.0): the block of the box the keys go to is turned here,
  // between turns, and the box is painted with a `du`; an app that only waits still has a cursor (docs/30, docs/90).
  // **And a card over the page means no caret** (0.179.42): the ink belongs to a page nobody is reading.
  if (!the_card_is_out) luabind::caret_poll();

  // **Y EL QUE DEU LA BANDA TAMPOC NO ESPERA L'APP** (0.179.36): `MAY` and `123` change the band and nothing of the page,
  // so an app that only waits never presents them —and since the caret settles after its six blinks there may be no
  // present at all—. The band is a picture of the system and it is painted here, in the turn its key was pressed
  // (docs/30, docs/50). **Under the card it waits too**: the band is the page of the app, and the frame it owes is the
  // frame the card is covering (0.179.42).
#if LUNOK_GLASS
  if (!the_card_is_out && ui::keyboard_owes_paint()) ui::present();
#endif  // LUNOK_GLASS

  // What the light has to say is painted at the end of the turn, once, and only if
  // it changed: a frame is a write on the wire (docs/75).
  led::poll();

  // **I el torn del vidre** (0.190.140): un panell que ha de baixar les seves tensions quan ningú no hi pinta. És una línia per volta i el que
  // fa és mirar el rellotge (docs/50).
#if LUNOK_GLASS
  display::poll();
#endif  // LUNOK_GLASS

  // The bar of the system: if the minute, the title or the charge changed, it paints its own strip and
  // nothing else. It is the only thing of the system that draws on the glass, and only its own rows
  // (docs/30).
#if LUNOK_GLASS
  bar::poll();

  // **A page the panel refused is painted as soon as it can take it** (0.118.2): the glass of the Paper says no to a
  // paint that arrives while another one is moving its ink, and an app that presents once —`run images` does— would
  // otherwise leave the screen of the boot on the glass for ever: it runs and shows nothing. The question is only paid
  // for when something is owed, so a turn that owes nothing costs nothing (docs/50, docs/92 §33).
  if (ui::owes_paint()) ui::idle();
#endif  // LUNOK_GLASS

  // A moment of the system that ended gave the app its light back, and the app may have changed
  // its mind while the light was not its own: `on_light()` is the one turn it gets to redraw
  // (docs/30). It runs between turns, with the app's coroutine suspended, like every hook.
  if (led::released() && luabind::running()) luabind::call_hook("on_light");

  // **I la llista d'apps que ha canviat** (0.189.23): the same place as `on_light`, and for the same reason — it is a hook
  // and it runs between turns — but only after the app has had its own turn, so the app never hears a knock from inside one
  // of its calls (docs/30, docs/40).
  deliver_the_apps_change();

  if (pal::system().reboot_requested()) g_running = false;
}

bool running() { return g_running; }

bool the_chip_restarted() { return power::the_chip_restarted(); }

void shutdown() { g_running = false; }

hal::IPlatform& platform() { return *g_platform; }

bool execute(const char* line) {
  char buf[kCommandBytes];
  size_t len = strlen(line);
  if (len >= sizeof(buf)) len = sizeof(buf) - 1;
  memcpy(buf, line, len);
  buf[len] = '\0';
  return shell::execute(buf);
}

bool app_running() { return shell::app_running(); }

}  // namespace kernel
