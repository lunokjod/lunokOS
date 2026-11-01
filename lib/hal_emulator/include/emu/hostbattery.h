// **The battery of the machine this bench runs on**, which is a battery the board does not have
// (docs/85).
//
// The Paper carries a cell of 1150 mAh at 3,7 V and reads it as millivolts behind a divider on GPIO35;
// a laptop carries whatever it carries and the kernel tells a percentage. What is lent is the
// **percentage and not the voltage**: 63 % of a laptop is 63 % of the Paper's cell, which on that curve
// is 3 847 mV, and it is the curve of `hal/battery.h` that says so (docs/50, docs/85).
#ifndef LUNOK_EMU_HOSTBATTERY_H
#define LUNOK_EMU_HOSTBATTERY_H

namespace emu {

struct HostBattery {
  bool present = false;   // the machine has a battery and it says where it is
  int percent = 0;        // what it says it has left, 0 to 100
  bool on_cable = false;  // it is charging or full, as far as the kernel knows
};

// The battery of this machine, from `/sys/class/power_supply`: the first entry of the kernel's own list
// that says it is a battery and has a percentage.
HostBattery host_battery();

// **The same read from a directory of files**, so a test can be a laptop with a battery of its own and
// the reading is proof without one (docs/85).
HostBattery host_battery_in(const char* dir);

// Where the cell of a board that has a gauge starts when the run typed no voltage: the nominal charge of a
// cell that has been on a shelf, which is the 3 700 mV row of the curve of `hal/battery.h`. A bench on a
// desk is still a Paper, and a Paper with no battery at all is a board nobody can buy (docs/85).
const int kNominalCellPct = 42;

// **What the board reads when no run typed a voltage**: the charge of this machine through the curve of the
// board's cell, or the nominal charge of the cell when there is no battery to lend. It is a function of its
// own so that a test can be a laptop, and a laptop with no battery, without a command line (docs/85).
int lent_cell_mv(const HostBattery& host);

}  // namespace emu

#endif  // LUNOK_EMU_HOSTBATTERY_H
