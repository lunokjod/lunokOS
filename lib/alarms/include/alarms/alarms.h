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
  Payload payload_kind = Payload::kNone;
  char payload[kPayloadBytes] = {0};
  // Repetitions that were missed while the system was off. The alarm fires
  // once, however many days went by (docs/60).
  uint32_t missed = 0;
};

void reset();

uint32_t max();
uint32_t count();
// The i-th alarm of the ordered list, or nullptr. The pointer is only valid
// until the next call that changes the list.
const Entry* at(uint32_t index);

// Adds an alarm and gives it an id; 0 when there is no room or the spec cannot
// be used (no app, no name, a name that does not fit).
uint32_t add(const Spec& spec);

// Only the owner of an alarm may cancel it: an app cannot reach into another.
bool cancel(const char* app, uint32_t id);

// The deadline of the first alarm, or -1 when the list is empty. This is what
// the kernel arms on the hardware, and the only thing it arms.
int64_t next_due_mono_ms();

// Takes the first alarm when its time has come. A repeating one goes back into
// the list at its next occurrence, and the copy in `out` says which occurrence
// fired and how many were missed. False when nothing is due.
bool take_due(int64_t now_mono_ms, Entry& out);

// Removes the alarm with that id, whoever owns it: for the window and for the
// list of missed alarms, which belong to the system.
bool remove_id(uint32_t id);

// A number that changes every time the list does. The kernel saves the
// persistent alarms when it moves, so nobody has to remember to save.
uint32_t revision();

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
