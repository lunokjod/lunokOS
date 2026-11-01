// **The battery of the bench**: the cell's curve read the other way, the battery of the machine the
// bench runs on, and what it costs to ask the chip (docs/50, docs/85).
//
// The board carries a cell of 1150 mAh that says millivolts behind a divider on GPIO35, and the machine
// this runs on says a percentage: what the bench lends is the percentage and not the voltage, and it is
// the curve of `hal/battery.h` that turns one into the other. The curve is tried here in both
// directions, and the reading of the kernel's files is tried on a directory of a test, so it is proof
// without a laptop.
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <unity.h>

#include "emu/hostbattery.h"
#include "emu/options.h"
#include "hal/hal.h"
#include "pal/pal.h"

void setUp(void) {}

void tearDown(void) {}

namespace {

const char* kRoot = "/tmp/lunokos_battery_test";
// The directory of one battery, which is what the kernel would list under `power_supply`.
const char* kBatteryDir = "/tmp/lunokos_battery_test/BAT0";

// A battery of our own: a directory with the two files the kernel would have written.
void write_text(const char* name, const char* text) {
  char path[256];
  snprintf(path, sizeof(path), "%s/BAT0/%s", kRoot, name);
  FILE* file = fopen(path, "w");
  if (file == nullptr) return;
  fprintf(file, "%s\n", text);
  fclose(file);
}

void make_a_battery(const char* capacity, const char* status) {
  char dir[256];
  snprintf(dir, sizeof(dir), "%s", kRoot);
  ::mkdir(dir, 0777);
  snprintf(dir, sizeof(dir), "%s/BAT0", kRoot);
  ::mkdir(dir, 0777);
  write_text("capacity", capacity);
  write_text("status", status);
}

}  // namespace

// **The curve goes both ways**: 4 200 mV is full and 3 300 is empty, every row of the table comes back
// as the percentage it started from, and a percentage between two rows lands between their millivolts
// (docs/50). It is what says that 63 % of a laptop is 3 847 mV of the Paper's cell (docs/85).
static void test_the_curve_goes_both_ways() {
  TEST_ASSERT_EQUAL_INT(4200, hal::battery_mv(100));
  TEST_ASSERT_EQUAL_INT(3300, hal::battery_mv(0));
  TEST_ASSERT_EQUAL_INT(3700, hal::battery_mv(42));
  // Every row of the curve, out and back: the two functions are the same table and cannot drift apart.
  const uint32_t percents[10] = {100, 90, 80, 70, 57, 42, 27, 14, 6, 0};
  for (uint32_t i = 0; i < 10; ++i) {
    TEST_ASSERT_EQUAL_UINT32(percents[i], hal::battery_percent(hal::battery_mv(percents[i])));
  }
  // Between two rows, straight and inside them.
  const int half = hal::battery_mv(50);
  TEST_ASSERT_TRUE(half > 3700 && half < 3800);
  TEST_ASSERT_UINT32_WITHIN(1, 50, hal::battery_percent(half));
  // And the voltage only goes up with the charge, row after row.
  int previous = 0;
  for (uint32_t pct = 0; pct <= 100; pct += 5) {
    const int mv = hal::battery_mv(pct);
    TEST_ASSERT_TRUE_MESSAGE(mv >= previous, "the curve of the cell goes down as the charge goes up");
    previous = mv;
  }
}

// **The battery of the machine**, read from the two files the kernel writes: the percentage and whether
// it is on a cable. A machine without them has no battery to lend, and a kernel that says 150 % is not
// believed (docs/85).
static void test_the_host_battery_is_read_from_the_kernel_files() {
  make_a_battery("63", "Discharging");
  emu::HostBattery host = emu::host_battery_in(kBatteryDir);
  TEST_ASSERT_TRUE(host.present);
  TEST_ASSERT_EQUAL_INT(63, host.percent);
  TEST_ASSERT_FALSE(host.on_cable);

  write_text("status", "Charging");
  host = emu::host_battery_in(kBatteryDir);
  TEST_ASSERT_TRUE(host.present);
  TEST_ASSERT_TRUE(host.on_cable);

  write_text("status", "Full");
  TEST_ASSERT_TRUE(emu::host_battery_in(kBatteryDir).on_cable);

  // A number that is not a percentage of anything: no battery, and the board stays without a gauge.
  write_text("capacity", "150");
  TEST_ASSERT_FALSE(emu::host_battery_in(kBatteryDir).present);
  write_text("capacity", "0");
  TEST_ASSERT_FALSE(emu::host_battery_in(kBatteryDir).present);
  write_text("capacity", "not a number");
  TEST_ASSERT_FALSE(emu::host_battery_in(kBatteryDir).present);

  // A directory that is not there is a machine with no battery, not a crash.
  TEST_ASSERT_FALSE(emu::host_battery_in("/tmp/lunokos_there_is_no_battery_here").present);
}

// **A machine with no battery still has a cell to lend**: the charge of this machine through the curve of
// the board's cell, and the nominal charge of the cell when there is no battery at all — because a Paper
// always has one, and a bench whose Paper says it does not know its battery is a board nobody can buy
// (docs/85). It is the number `run battery` reads on a Paper with no flags.
static void test_the_lent_cell_is_the_charge_of_this_machine() {
  emu::HostBattery host;
  host.present = true;
  host.percent = 100;
  TEST_ASSERT_EQUAL_INT(4200, emu::lent_cell_mv(host));
  host.percent = 63;
  TEST_ASSERT_EQUAL_INT(3847, emu::lent_cell_mv(host));
  TEST_ASSERT_UINT32_WITHIN(1, 63, hal::battery_percent(emu::lent_cell_mv(host)));
  host.percent = 1;
  TEST_ASSERT_TRUE(emu::lent_cell_mv(host) > 3300 && emu::lent_cell_mv(host) < 3400);

  // A desk with no battery: the cell starts at the row of its nominal charge, and not at zero or at nil.
  const emu::HostBattery none;
  TEST_ASSERT_EQUAL_INT(3700, emu::lent_cell_mv(none));
  TEST_ASSERT_EQUAL_INT(hal::battery_mv(static_cast<uint32_t>(emu::kNominalCellPct)), emu::lent_cell_mv(none));
}

// **Asking the chip is not free**: four conversions of the ADC and, the first time, the characterisation
// of the channel; and none of it is paid when the run is not paying the times of the hardware (docs/85).
static void test_the_gauge_costs_the_adc() {
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.battery_mv = 3700;
  opts.delays = true;
  emu::set_options(opts);
  hal::IPower* power = hal::platform().power();
  TEST_ASSERT_NOT_NULL(power);
  int mv = 0;
  int pct = 0;
  bool charging = true;
  // The first read of the session also characterises the channel: it costs more and it is paid once.
  const int64_t first_started = pal::system().micros();
  TEST_ASSERT_TRUE(power->battery(mv, pct, charging));
  const int64_t first = pal::system().micros() - first_started;
  TEST_ASSERT_EQUAL_INT(3700, mv);
  TEST_ASSERT_EQUAL_UINT32(42, static_cast<uint32_t>(pct));
  TEST_ASSERT_FALSE(charging);
  TEST_ASSERT_TRUE_MESSAGE(first >= 1400, "the first read of the gauge did not pay the ADC");
  // And every read after it is the four conversions.
  const int64_t started = pal::system().micros();
  TEST_ASSERT_TRUE(power->battery(mv, pct, charging));
  const int64_t after = pal::system().micros() - started;
  TEST_ASSERT_TRUE_MESSAGE(after >= 400, "a read of the gauge did not pay the ADC");

  opts.delays = false;
  emu::set_options(opts);
  const int64_t free_started = pal::system().micros();
  TEST_ASSERT_TRUE(power->battery(mv, pct, charging));
  const int64_t free_read = pal::system().micros() - free_started;
  TEST_ASSERT_TRUE_MESSAGE(free_read < 200, "a read with the waits off asked the clock for the ADC");
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_the_curve_goes_both_ways);
  RUN_TEST(test_the_host_battery_is_read_from_the_kernel_files);
  RUN_TEST(test_the_lent_cell_is_the_charge_of_this_machine);
  RUN_TEST(test_the_gauge_costs_the_adc);
  return UNITY_END();
}
