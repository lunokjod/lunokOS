#include "alarms/alarms.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hal/civil.h"

namespace alarms {
namespace {

Entry g_entries[kMaxAlarms];
uint32_t g_count = 0;
uint32_t g_last_id = 0;
uint32_t g_revision = 0;

void copy_text(char* dst, size_t cap, const char* src) {
  if (cap == 0) return;
  size_t i = 0;
  if (src != nullptr) {
    for (; src[i] != '\0' && i + 1 < cap; ++i) dst[i] = src[i];
  }
  dst[i] = '\0';
}

// Writes the entry at `index` and keeps the list ordered by deadline. The list
// is short on purpose: an insertion that shifts a few structs costs less than
// the bookkeeping of a tree, and only the first element is ever read per turn.
void insert_at(uint32_t index, const Entry& entry) {
  if (g_count >= kMaxAlarms) return;
  const uint32_t last = (index < g_count) ? index : g_count;
  for (uint32_t i = g_count; i > last; --i) g_entries[i] = g_entries[i - 1];
  g_entries[last] = entry;
  ++g_count;
}

uint32_t insertion_point(int64_t on_mono_ms) {
  uint32_t i = 0;
  while (i < g_count && g_entries[i].on_mono_ms <= on_mono_ms) ++i;
  return i;
}

void remove_at(uint32_t index) {
  if (index >= g_count) return;
  for (uint32_t i = index; i + 1 < g_count; ++i) g_entries[i] = g_entries[i + 1];
  --g_count;
}

int32_t find_id(uint32_t id) {
  for (uint32_t i = 0; i < g_count; ++i) {
    if (g_entries[i].id == id) return static_cast<int32_t>(i);
  }
  return -1;
}

}  // namespace

void reset() {
  g_count = 0;
  g_last_id = 0;
  ++g_revision;
}

uint32_t max() { return kMaxAlarms; }

uint32_t count() { return g_count; }

// **I només les que sonen** (0.189.154, l'amo: *«Veig l'icona d'alarma a la barra de sistema, està activa per algun motiu?»*):
// the queue of the system is not only the alarms of a person —`battery` arms a repeating alarm every ten minutes to take a
// sample, and it is a `persist` one, o sigui que era a la cua sempre i la campana de la barra deia que hi havia una alarma posada
// quan no n'hi havia cap—. The mark of the strip is for the alarms that will **come to the glass**: `raise_to_front` is what the
// app asked for when it wants to be opened, and an alarm of a person asks for it (the clock's daily one, the countdown) while
// the housekeeping of an app does not (docs/30, docs/60).
bool has_repeating() {
  for (uint32_t i = 0; i < g_count; ++i) {
    if (g_entries[i].repeat_ms != 0 && g_entries[i].raise_to_front) return true;
  }
  return false;
}

bool has_once() {
  for (uint32_t i = 0; i < g_count; ++i) {
    if (g_entries[i].repeat_ms == 0 && g_entries[i].raise_to_front) return true;
  }
  return false;
}

const Entry* at(uint32_t index) {
  if (index >= g_count) return nullptr;
  return &g_entries[index];
}

uint32_t add(const Spec& spec) {
  if (g_count >= kMaxAlarms) return 0;
  if (spec.app == nullptr || spec.app[0] == '\0') return 0;
  if (strlen(spec.app) >= kAppBytes) return 0;
  if (spec.payload != nullptr && strlen(spec.payload) >= kPayloadBytes) return 0;

  Entry entry;
  ++g_last_id;
  if (g_last_id == 0) ++g_last_id;  // 0 means "no alarm" everywhere else
  entry.id = g_last_id;
  copy_text(entry.app, sizeof(entry.app), spec.app);
  entry.on_mono_ms = spec.on_mono_ms;
  entry.repeat_ms = (spec.repeat_ms > 0) ? spec.repeat_ms : 0;
  entry.raise_to_front = spec.raise_to_front;
  entry.persist = spec.persist;
  entry.payload_kind = (spec.payload != nullptr) ? spec.payload_kind : Payload::kNone;
  copy_text(entry.payload, sizeof(entry.payload), spec.payload);
  entry.missed = 0;

  insert_at(insertion_point(entry.on_mono_ms), entry);
  ++g_revision;
  return entry.id;
}

bool cancel(const char* app, uint32_t id) {
  if (app == nullptr) return false;
  for (uint32_t i = 0; i < g_count; ++i) {
    if (g_entries[i].id != id) continue;
    if (strcmp(g_entries[i].app, app) != 0) return false;  // not yours
    remove_at(i);
    ++g_revision;
    return true;
  }
  return false;
}

// **Les alarmes d'una app que marxa** (0.189.157, l'amo: *«i que passa amb les alarmes que ha deixat l'app al sistema si es
// desinstala?»*): a deleted app's alarms are no longer anybody's —the queue is ordered and only the nearest is armed, o sigui
// que una alarma d'una app que ja no hi és **despertava la placa** per a res i el sistema la descartava amb una línia al
// registre—. El que les ha de treure és qui esborra l'app (`apps::remove`) i no l'execució, perquè una alarma que es queda
// és una alarma que encara farà sonar el xip del rellotge (docs/30, docs/40, docs/60).
uint32_t remove_of(const char* app) {
  if (app == nullptr || app[0] == '\0') return 0;
  uint32_t gone = 0;
  for (uint32_t i = 0; i < g_count;) {
    if (strcmp(g_entries[i].app, app) != 0) {
      ++i;
      continue;
    }
    remove_at(i);
    ++gone;
  }
  if (gone > 0) ++g_revision;
  return gone;
}

bool remove_id(uint32_t id) {
  const int32_t index = find_id(id);
  if (index < 0) return false;
  remove_at(static_cast<uint32_t>(index));
  ++g_revision;
  return true;
}

int64_t next_due_mono_ms() {
  if (g_count == 0) return -1;
  return g_entries[0].on_mono_ms;
}

bool take_due(int64_t now_mono_ms, Entry& out) {
  if (g_count == 0) return false;
  if (g_entries[0].on_mono_ms > now_mono_ms) return false;

  out = g_entries[0];
  remove_at(0);

  if (out.repeat_ms > 0) {
    // Back into the list at its next occurrence, even when it is already late:
    // an alarm that was missed three days running fires once and stays armed.
    int64_t next = out.on_mono_ms + out.repeat_ms;
    uint32_t missed = 0;
    while (next <= now_mono_ms) {
      next += out.repeat_ms;
      ++missed;
    }
    out.missed = missed;  // the copy that fired says how many it lost
    Entry again = out;
    again.on_mono_ms = next;
    insert_at(insertion_point(again.on_mono_ms), again);
  }
  ++g_revision;
  return true;
}

uint32_t revision() { return g_revision; }

size_t describe(uint32_t index, int64_t mono_now_ms, int64_t epoch_now_ms, int32_t offset_minutes, char* buf,
                size_t cap) {
  if (buf == nullptr || cap == 0 || index >= g_count) return 0;
  buf[0] = '\0';
  const Entry& entry = g_entries[index];

  // **Quant li falta, i quan serà a l'hora de la persona**: the deadline is a monotonic one —it is what the board counts—
  // and a person reads an hour, so the epoch comes from the difference between the two clocks, with the offset the system
  // shows everywhere (docs/30, docs/60).
  char when[48] = {0};
  const int64_t left_ms = entry.on_mono_ms - mono_now_ms;
  if (left_ms >= 0) {
    snprintf(when, sizeof(when), "in %lld s", static_cast<long long>((left_ms + 999) / 1000));
  } else {
    snprintf(when, sizeof(when), "%lld s late", static_cast<long long>((-left_ms + 999) / 1000));
  }
  char at[32] = {0};
  if (epoch_now_ms > 0) {
    int year = 1970;
    int month = 1;
    int day = 1;
    int hour = 0;
    int minute = 0;
    int second = 0;
    // The wall clock of the alarm: now plus what is left, in the hour the board shows.
    const int64_t at_ms = epoch_now_ms + left_ms + static_cast<int64_t>(offset_minutes) * 60000;
    hal::epoch_ms_to_civil(at_ms, year, month, day, hour, minute, second);
    snprintf(at, sizeof(at), ", at %02d:%02d:%02d", hour, minute, second);
  }

  char how[64] = {0};
  if (entry.repeat_ms > 0) {
    if (entry.repeat_ms == static_cast<int32_t>(kDailyMs)) {
      snprintf(how, sizeof(how), ", repeats daily");
    } else {
      snprintf(how, sizeof(how), ", repeats every %d s", static_cast<int>(entry.repeat_ms / 1000));
    }
  }

  char extra[64] = {0};
  size_t used = 0;
  if (entry.persist) {
    used += static_cast<size_t>(snprintf(extra + used, sizeof(extra) - used, ", persist"));
  }
  if (entry.raise_to_front) {
    used += static_cast<size_t>(snprintf(extra + used, sizeof(extra) - used, ", raises the app"));
  }
  if (entry.payload_kind != Payload::kNone) {
    snprintf(extra + used, sizeof(extra) - used, ", payload %s %u B",
             (entry.payload_kind == Payload::kNumber) ? "number" : "text",
             static_cast<unsigned>(strlen(entry.payload)));
  }

  const int written = snprintf(buf, cap, "#%u %s: %s%s%s%s", static_cast<unsigned>(entry.id), entry.app, when, at, how,
                               extra);
  if (written <= 0 || static_cast<size_t>(written) >= cap) {
    buf[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(written);
}

size_t dump(int64_t mono_now_ms, int64_t epoch_now_ms, char* buf, size_t cap) {
  if (buf == nullptr || cap == 0) return 0;
  buf[0] = '\0';
  // A wall-clock deadline cannot be written down when there is no epoch: the
  // numbers would mean nothing tomorrow (docs/60).
  if (epoch_now_ms <= 0) return 0;

  size_t used = 0;
  for (uint32_t i = 0; i < g_count; ++i) {
    const Entry& entry = g_entries[i];
    if (!entry.persist) continue;

    char payload[kPayloadBytes];
    copy_text(payload, sizeof(payload), entry.payload);
    for (size_t c = 0; payload[c] != '\0'; ++c) {
      if (payload[c] == '\n' || payload[c] == '\r') payload[c] = ' ';  // one line each
    }

    char line[192];
    const int written = snprintf(
        line, sizeof(line), "%s;%lld;%d;%d;%d;%s\n", entry.app,
        static_cast<long long>(entry.on_mono_ms - mono_now_ms + epoch_now_ms),
        entry.repeat_ms, (entry.raise_to_front ? 1 : 0) | (entry.persist ? 2 : 0),
        static_cast<int>(entry.payload_kind), payload);
    if (written <= 0) continue;
    const size_t len = static_cast<size_t>(written);
    if (used + len + 1 > cap) break;
    memcpy(buf + used, line, len);
    used += len;
  }
  buf[used] = '\0';
  return used;
}

namespace {

bool load_line(char* line, int64_t mono_now_ms, int64_t epoch_now_ms) {
  char* parts[6];
  char* cursor = line;
  for (int i = 0; i < 5; ++i) {
    char* sep = strchr(cursor, ';');
    if (sep == nullptr) return false;
    *sep = '\0';
    parts[i] = cursor;
    cursor = sep + 1;
  }
  parts[5] = cursor;  // the payload is the rest of the line, semicolons and all

  Spec spec;
  spec.app = parts[0];
  spec.on_mono_ms = mono_now_ms + (atoll(parts[1]) - epoch_now_ms);
  spec.repeat_ms = atoi(parts[2]);
  const int flags = atoi(parts[3]);
  spec.raise_to_front = (flags & 1) != 0;
  spec.persist = (flags & 2) != 0;
  const int kind = atoi(parts[4]);
  spec.payload_kind = (kind == 1) ? Payload::kText
                                 : ((kind == 2) ? Payload::kNumber : Payload::kNone);
  spec.payload = (parts[5][0] != '\0') ? parts[5] : nullptr;
  return add(spec) != 0;
}

}  // namespace

bool load(int64_t mono_now_ms, int64_t epoch_now_ms, const char* text) {
  if (text == nullptr || epoch_now_ms <= 0) return false;
  reset();

  bool any = false;
  const char* cursor = text;
  while (*cursor != '\0') {
    const char* end = strchr(cursor, '\n');
    const size_t len = (end != nullptr) ? static_cast<size_t>(end - cursor) : strlen(cursor);
    if (len > 0 && len < 192) {
      char line[192];
      memcpy(line, cursor, len);
      line[len] = '\0';
      if (load_line(line, mono_now_ms, epoch_now_ms)) any = true;
    }
    if (end == nullptr) break;
    cursor = end + 1;
  }
  return any;
}

}  // namespace alarms
