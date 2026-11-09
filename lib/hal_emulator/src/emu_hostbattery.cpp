// The battery of the PC, read from the kernel's own files (docs/85).
#include "emu/hostbattery.h"

#if defined(__linux__)
#include <dirent.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hal/battery.h"

namespace emu {
namespace {

#if defined(__linux__)
const char* kKernelList = "/sys/class/power_supply";
#endif

// One line of a file of the kernel's, without the newline: `capacity` says `63` and `status` says
// `Discharging`, and both end with a line break that is not part of the answer.
bool read_line(const char* dir, const char* name, char* out, size_t cap) {
  char path[320];
  snprintf(path, sizeof(path), "%s/%s", dir, name);
  FILE* file = fopen(path, "r");
  if (file == nullptr) return false;
  if (fgets(out, static_cast<int>(cap), file) == nullptr) {
    fclose(file);
    return false;
  }
  fclose(file);
  size_t len = strlen(out);
  while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r' || out[len - 1] == ' ')) {
    out[--len] = '\0';
  }
  return len > 0;
}

bool read_number(const char* dir, const char* name, int& out) {
  char text[32];
  if (!read_line(dir, name, text, sizeof(text))) return false;
  char* end = nullptr;
  const long value = strtol(text, &end, 10);
  if (end == nullptr || *end != '\0') return false;
  out = static_cast<int>(value);
  return true;
}

}  // namespace

HostBattery host_battery_in(const char* dir) {
  HostBattery out;
  if (dir == nullptr) return out;
  // **A percentage is the whole answer or there is no battery**: a battery is not a voltage to guess
  // from, and a kernel that says 150 is a kernel this emulator does not believe (docs/85).
  int percent = 0;
  if (!read_number(dir, "capacity", percent)) return out;
  if (percent < 1 || percent > 100) return out;
  out.present = true;
  out.percent = percent;
  char status[32];
  if (read_line(dir, "status", status, sizeof(status))) {
    out.on_cable = (strcmp(status, "Charging") == 0) || (strcmp(status, "Full") == 0);
  }
  return out;
}

HostBattery host_battery() {
#if defined(__linux__)
  DIR* list = opendir(kKernelList);
  if (list == nullptr) return HostBattery();
  HostBattery found;
  for (struct dirent* item = readdir(list); item != nullptr; item = readdir(list)) {
    if (item->d_name[0] == '.') continue;
    char dir[320];
    snprintf(dir, sizeof(dir), "%s/%s", kKernelList, item->d_name);
    // The kernel lists the charger and the battery side by side and only one of them has a percentage:
    // asking for the file is the whole test, and it needs no names of its own (docs/85).
    const HostBattery battery = host_battery_in(dir);
    if (!battery.present) continue;
    found = battery;
    break;
  }
  closedir(list);
  return found;
#else
  // `/sys/class/power_supply` is Linux's. The other desktops have no battery to lend here, and the
  // emulator says so by returning none instead of looking for a file that cannot exist (docs/85).
  return HostBattery();
#endif
}

int lent_cell_mv(const HostBattery& host) {
  // A machine with no cell to lend is still a board with one: the cell starts at its nominal charge and
  // the run says the number it made up (docs/85).
  if (!host.present) return hal::battery_mv(static_cast<uint32_t>(kNominalCellPct));
  return hal::battery_mv(static_cast<uint32_t>(host.percent));
}

}  // namespace emu
