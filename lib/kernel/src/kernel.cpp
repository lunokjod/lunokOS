#include "kernel/kernel.h"

#include <string.h>

#include "alarms/alarms.h"
#include "kernel/apps.h"
#include "kernel/button.h"
#include "kernel/cfg.h"
#include "kernel/fs_ops.h"
#include "kernel/env.h"
#include "kernel/display.h"
#include "kernel/touch.h"
#include "kernel/imu.h"
#include "kernel/led.h"
#include "kernel/log.h"
#include "kernel/net.h"
#include "kernel/power.h"
#include "kernel/shell.h"
#include "kernel/version.h"
#include "kernel/vfs.h"
#include "luabind/luabind.h"
#include "pal/pal.h"

namespace kernel {
namespace {

// How long the queue may block before the loop wakes up and looks around.
const int32_t kIdleTimeoutMs = 1000;

// With an app alive that waits for something without a deadline, the loop wakes
// up this often so the app gets its turn.
const int32_t kAppIdleMs = 20;

// Longest command line the batch mode accepts.
const size_t kCommandBytes = 128;

// How long the console is allowed to keep throwing bytes away before the system
// says anything. The board starts its UART before us and the first bytes after
// a reset are junk; without this the shell answers bytes nobody typed and prints
// a prompt for each newline it finds in the noise.
const int32_t kConsoleSettleMs = 250;

hal::IPlatform* g_platform = nullptr;
bool g_running = false;

void settle_console() {
  pal::IConsole* console = pal::system().console();
  if (console == nullptr) return;

  char sink[64];
  pal::Event ev;
  const int64_t deadline = pal::system().millis() + kConsoleSettleMs;
  for (;;) {
    while (console->read(sink, sizeof(sink)) > 0) {
    }
    const int64_t left = deadline - pal::system().millis();
    if (left <= 0) break;
    // Sleeping through the queue keeps the CPU idle instead of spinning.
    pal::system().queue()->pop(ev, static_cast<int32_t>(left));
  }
  while (console->read(sink, sizeof(sink)) > 0) {
  }
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
  // On the PC both are true: the name is the board the bench was told to be, and the
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
    log_printf(pal::kLogWarn, "alarms: cannot write the queue to nvs");
    return;
  }
  g_alarms_stored = true;
}

// Only the nearest alarm is ever armed, and only when the nearest one changed:
// that is what makes dozens of alarms cost one wake (docs/60).
void arm_nearest_alarm() {
  if (g_platform == nullptr) return;
  hal::IClock* clock = g_platform->clock();
  if (clock == nullptr || !clock->can_wake()) return;

  const int64_t next_mono = alarms::next_due_mono_ms();
  if (next_mono == g_armed_at) return;
  g_armed_at = next_mono;
  if (next_mono < 0) {
    clock->disarm_wake();
    return;
  }
  const int64_t at_epoch = clock->epoch_ms() + (next_mono - clock->monotonic_ms());
  if (!clock->arm_wake(at_epoch)) {
    log_printf(pal::kLogWarn, "clock: cannot arm the wake for %lld",
               static_cast<long long>(at_epoch));
  }
}

// An alarm that is due is delivered here, between two turns of the app, so the
// app's coroutine is always suspended when its on_alarm runs.
void deliver_alarms() {
  if (g_platform == nullptr) return;
  alarms::Entry entry;
  int64_t now = clock_mono();
  while (alarms::take_due(now, entry)) {
    // A light says an alarm went off even when the app's answer is a line on a console
    // nobody is looking at. It runs for its own cycles and then the light goes back to
    // what it was saying (docs/30).
    led::note(led::Trigger::kAlarm);
    const int64_t late = now - entry.on_mono_ms;
    const bool owner_running =
        luabind::running() && (strcmp(luabind::app_id(), entry.app) == 0);
    if (owner_running && !luabind::call_alarm(entry, late)) {
      log_printf(pal::kLogWarn, "alarm %u for %s: the app has no on_alarm",
                 static_cast<unsigned>(entry.id), entry.app);
    } else if (!owner_running && entry.raise_to_front) {
      // raise: the app of the alarm takes the foreground, and whatever was in
      // front of it is closed first (docs/60).
      log_printf(pal::kLogInfo, "alarm %u raises %s", static_cast<unsigned>(entry.id),
                 entry.app);
      shell::raise_app(entry.app);
    } else if (!owner_running) {
      // Nobody has the app loaded: it gets a window, and the foreground does not
      // notice anything but a line in the log.
      if (!shell::run_window(entry.app, entry, late)) {
        log_printf(pal::kLogWarn, "alarm %u for %s: no window (app gone or no on_alarm)",
                   static_cast<unsigned>(entry.id), entry.app);
      } else {
        log_printf(pal::kLogInfo, "alarm %u delivered to %s in a window (%lld ms late)",
                   static_cast<unsigned>(entry.id), entry.app,
                   static_cast<long long>(late));
      }
    }
    now = clock_mono();
  }
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
    default:
      log_printf(pal::kLogDebug, "unhandled event type %u", static_cast<unsigned>(ev.type));
      break;
  }
}

}  // namespace

void boot(hal::IPlatform& platform) {
  g_platform = &platform;
  g_running = true;
  log_init();
  // The settle is for the board: the UART bridge leaves a run of bytes behind
  // when the port opens. On the PC there is no such noise, and the bytes that do
  // arrive (a pipe, a script, a hand on the keyboard) are for the person.
  if (!platform.emulator()) settle_console();
  print_banner();

  // The filesystem is mandatory: it is where the apps live. If it does not mount,
  // the system still boots, says so in the log, and the prompt is what is left.
  vfs::init(platform);

  // `/cfg` is not a disk but a door, and this says which unit is behind it: the card when
  // there is one, the flash otherwise, or whatever the person asked for (docs/70). It
  // goes right after the mounts because it is one, and before any app can write a setting.
  // The count is said after both, so the log and `mounts` cannot disagree.
  cfg::boot(platform);
  log_printf(pal::kLogInfo, "vfs: %u mounts", static_cast<unsigned>(vfs::mount_count()));

  // The alarms that survived the power cut come back before anything else can
  // look at the queue.
  alarms_restore();

  // From here an app may read files: the interpreter gets the VFS through these
  // four operations and never depends on it (docs/70).
  install_fs_ops();
  net::boot(platform);
  // Apps are not kept in any list: this only counts them for the log. Walking
  // /apps is what the commands do, and only when they are asked.
  log_printf(pal::kLogInfo, "apps: %u found", static_cast<unsigned>(apps::count()));

  uint32_t present = 0;
  uint32_t total = 0;
  count_caps(present, total);
  log_printf(pal::kLogInfo, "boot board=%s caps=%u/%u", platform.id(),
             static_cast<unsigned>(present), static_cast<unsigned>(total));

  // Where the epoch comes from, said once at boot: a board with no epoch is not
  // broken, it just cannot answer `time.now()` until somebody sets it (docs/60).
  hal::IClock* clock = platform.clock();
  if (clock == nullptr) {
    log_printf(pal::kLogWarn, "clock: none");
  } else {
    log_printf(pal::kLogInfo, "clock: source %s epoch %lld", clock->source(),
               static_cast<long long>(clock->epoch_ms()));
  }

  shell::start();
  // The light and the button: what this board does with a person. Both are optional
  // and both say so, so a board without them boots the same (docs/50).
  led::boot(platform);
  button::boot(platform);
  // The sensor of the board, on the band that has one: the Lite says it has none and
  // imu.read() answers that, instead of the API being absent (docs/50).
  imu::boot(platform);
  // The air, the same way: the Paper has an SHT30 and the ATOMs have nothing, and env.read() answers
  // which of the two this is (docs/30, docs/50).
  env::boot(platform);
  // The glass, when the board has one: its frames become taps, long presses and drags in the turns
  // of this loop (docs/50).
  touch::boot(platform);
  // The panel, when the board has one. It is booted and **not polled**: nothing of the system paints,
  // so there is no turn of this loop that has anything to say to the glass (docs/30, docs/50).
  display::boot(platform);
  // Last, because it speaks to the console and may bring an app back: a board
  // that wakes from a deep sleep is a board that boots, and the session record is
  // what makes the app come back where it was (docs/90).
  power::boot(platform);
}

void alarms_restore() {
  if (g_platform == nullptr) return;
  hal::IKeyValue* nvs = g_platform->nvs();
  if (nvs == nullptr) return;

  const int64_t epoch = clock_epoch();
  if (epoch <= 0) {
    // Without an epoch the stored lines cannot be placed on the monotonic clock,
    // and firing them at the wrong time is worse than saying nothing (docs/60).
    log_printf(pal::kLogInfo, "alarms: no epoch yet, nothing restored");
    return;
  }

  char blob[kAlarmBlobBytes];
  size_t len = 0;
  if (!nvs->get(kAlarmNs, kAlarmKey, blob, sizeof(blob) - 1, len)) return;
  blob[len] = '\0';

  if (alarms::load(clock_mono(), epoch, blob)) {
    log_printf(pal::kLogInfo, "alarms: %u restored from nvs",
               static_cast<unsigned>(alarms::count()));
    g_alarms_stored = true;
  } else {
    log_printf(pal::kLogWarn, "alarms: the nvs queue could not be used");
    nvs->erase(kAlarmNs, kAlarmKey);
    g_alarms_stored = false;
  }
  // Loading is not a change to save back; the queue as it is is what the next
  // change will write.
  g_saved_revision = alarms::revision();
}

void poll() {
  if (!g_running) return;

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
    const int64_t epoch = (clock != nullptr) ? clock->epoch_ms() : 0;
    if (epoch > 0) next_alarm_epoch = epoch + (next_alarm - clock_mono());
  }

  // The light is painted at the end of every turn, so a turn that arrives late paints the
  // frame of *that* moment: cutting the wait is all an animation needs. No thread, no queue
  // of frames it owes, and nothing that can fall behind (docs/30).
  const int32_t frame_wait = led::next_frame_ms();
  if (frame_wait >= 0 && frame_wait < wait) wait = frame_wait;

  pal::Event ev;
  if (pal::system().queue()->pop(ev, wait)) dispatch(ev);

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
  touch::poll();

  // A sleep an app asked for happens here, between turns, with the app's coroutine
  // suspended: the interpreter is not closed from inside its own call (docs/30).
  // The queue is written down **before** the board may go: a queue that changes in the same
  // turn the board powers off would be an alarm nobody kept (docs/60).
  if (alarms::revision() != g_saved_revision) save_alarms();
  power::poll(next_alarm_epoch);

  pal::IConsole* console = pal::system().console();
  if (console != nullptr) {
    char buf[64];
    size_t n = console->read(buf, sizeof(buf));
    while (n > 0) {
      // A person is at the keyboard: the board is being used and does not put itself to
      // sleep until the prompt has been quiet for a while (docs/60).
      power::note_activity();
      shell::feed(buf, n);
      n = console->read(buf, sizeof(buf));
    }
  }

  // The app gets its turn after the events and the console, so a byte that has
  // just arrived is already queued when it wakes up.
  shell::poll_app();

  // What the light has to say is painted at the end of the turn, once, and only if
  // it changed: a frame is a write on the wire (docs/75).
  led::poll();

  // A moment of the system that ended gave the app its light back, and the app may have changed
  // its mind while the light was not its own: `on_light()` is the one turn it gets to redraw
  // (docs/30). It runs between turns, with the app's coroutine suspended, like every hook.
  if (led::released() && luabind::running()) luabind::call_hook("on_light");

  if (pal::system().reboot_requested()) g_running = false;
}

bool running() { return g_running; }

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
