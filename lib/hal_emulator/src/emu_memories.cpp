// The RTC RAM and the FRAM of the emulated board, as binary files of the exact size (docs/85).
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include "emu/memories.h"
#include "emu/model.h"
#include "emu/options.h"

namespace emu {
namespace {

const size_t kPathCap = 320;

uint8_t g_rtc[kRtcRamBytes];
uint8_t g_fram[kFramBytes];
bool g_loaded = false;
bool g_rtc_kept = false;  // whether this run started from a memory that was already there

// Creates the **directory** a memory lives in. The last part of the path is the file itself, so it
// is not made: a `mkdir` of `rtc.bin` gives a directory that is not a memory, which is what the first
// run of this did (docs/85).
void make_parent(const char* path) {
  char partial[kPathCap];
  const size_t len = strlen(path);
  if (len >= sizeof(partial)) return;
  memcpy(partial, path, len + 1);
  // From the end: the first separator is where the file name stops.
  size_t end = len;
  while (end > 0 && partial[end - 1] != '/') --end;
  for (size_t i = 1; i < end; ++i) {
    if (partial[i] == '/') {
      partial[i] = '\0';
      ::mkdir(partial, 0777);  // it fails when it is already there, which is fine
      partial[i] = '/';
    }
  }
  if (end > 1) {
    partial[end - 1] = '\0';
    ::mkdir(partial, 0777);
  }
}

// `<state_dir>/<name>`: the memories that are not a volume live with the key-value store, and an
// emulator that was given only a working disk keeps them in it (docs/85).
void memory_path(const char* name, char* out, size_t cap) {
  const Options& o = options();
  const char* base = (o.state_dir != nullptr) ? o.state_dir : o.fs_dir;
  snprintf(out, cap, "%s/%s", (base != nullptr) ? base : "", name);
}

// Reads the file into `bytes`, or zeroes it. A file of the wrong size is a different memory and it
// is started again: the size is part of what the memory is.
void read_memory(const char* name, uint8_t* bytes, size_t size) {
  memset(bytes, 0, size);
  char path[kPathCap];
  memory_path(name, path, sizeof(path));
  FILE* file = fopen(path, "rb");
  if (file == nullptr) return;
  const size_t got = fread(bytes, 1, size, file);
  fclose(file);
  if (got != size) memset(bytes, 0, size);
}

void write_memory(const char* name, const uint8_t* bytes, size_t size) {
  char path[kPathCap];
  memory_path(name, path, sizeof(path));
  make_parent(path);
  FILE* file = fopen(path, "wb");
  if (file == nullptr) return;
  fwrite(bytes, 1, size, file);
  fclose(file);
}

void load_once() {
  if (g_loaded) return;
  g_loaded = true;
  read_memory("rtc.bin", g_rtc, sizeof(g_rtc));
  read_memory("fram.bin", g_fram, sizeof(g_fram));
  // Anything but a full run of zeros is a memory that came from the run before, and that is only
  // true if the file was there. `read_memory` cannot tell; this can.
  char path[kPathCap];
  memory_path("rtc.bin", path, sizeof(path));
  FILE* file = fopen(path, "rb");
  if (file != nullptr) {
    g_rtc_kept = true;
    fclose(file);
  }
}

}  // namespace

uint8_t* rtc_ram() {
  load_once();
  return g_rtc;
}

uint8_t* fram() {
  load_once();
  // A board with no FRAM chip has no memory to hand out: the same nullptr a board without the chip
  // would give, and not an array nobody can reach (docs/50, docs/85).
  if (!model().fram) return nullptr;
  return g_fram;
}

void flush_memories() {
  if (!g_loaded) return;
  write_memory("rtc.bin", g_rtc, sizeof(g_rtc));
  if (model().fram) write_memory("fram.bin", g_fram, sizeof(g_fram));
}

void power_cycle() {
  load_once();
  // The hardware decides, not the person: a board whose hour lives in a chip with a coin cell behind
  // it comes back knowing the hour, and one without it is a board nobody has set (docs/50, docs/85).
  // The FRAM is left alone because it is not volatile, which is the whole point of it, and a model
  // that has none never gets the file. **`--persist` is a board that was not unplugged**: a reset
  // leaves the RTC domain powered, so the memory is there whatever the model has behind it (docs/85).
  const bool kept = options().persist || model().keeps_the_hour;
  if (!kept || !g_rtc_kept) memset(g_rtc, 0, sizeof(g_rtc));
  flush_memories();
}

// **The hour is one number and it is not a memory of the chip**: it goes in a file of its own, with the
// wall clock of this machine next to it, because a board that is off still counts —a coin cell is there
// for that— and the time between two runs is time the clock kept. A file that is not there is a board
// that was never set (docs/85).
struct ClockFile {
  int64_t epoch_ms;
  int64_t host_s;  // what this machine's clock said when the file was written
};

int64_t load_clock_ms() {
  char path[kPathCap];
  memory_path("clock.bin", path, sizeof(path));
  FILE* file = fopen(path, "rb");
  if (file == nullptr) return 0;
  ClockFile kept = {};
  const size_t got = fread(&kept, 1, sizeof(kept), file);
  fclose(file);
  if (got != sizeof(kept) || kept.epoch_ms <= 0) return 0;
  // **The time it was off**: the run before wrote the hour and when it wrote it, so how long the board
  // has been unplugged is one subtraction and the hour comes back where it should be (docs/60, docs/85).
  const int64_t now_s = static_cast<int64_t>(time(nullptr));
  if (now_s > kept.host_s) kept.epoch_ms += (now_s - kept.host_s) * 1000;
  return kept.epoch_ms;
}

void save_clock_ms(int64_t ms) {
  if (ms <= 0) return;
  char path[kPathCap];
  memory_path("clock.bin", path, sizeof(path));
  make_parent(path);
  FILE* file = fopen(path, "wb");
  if (file == nullptr) return;
  const ClockFile out = {ms, static_cast<int64_t>(time(nullptr))};
  fwrite(&out, 1, sizeof(out), file);
  fclose(file);
}

}  // namespace emu
