// The calendar arithmetic of the system, and the only copy of it (docs/60, docs/90).
#include "kernel/calendar.h"

namespace kernel {
namespace calendar {
namespace {

// The days of the twelve months, February with twenty-eight and the leap year added below: a table is what a
// person reads and a rule of four lines is what a chip should have, and both are here once (docs/60).
const uint8_t kMonthDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

// **Sakamoto's shift**: the offset of each month for the weekday count, which is the whole of the calendar rule and
// costs twelve bytes. It is integer arithmetic with no table of days per year, and it holds for every year the
// Gregorian calendar has (docs/60).
const uint8_t kSakamotoShift[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};

// **The two settings, with the calendar this system was born with as their value** (0.170.0): a build that has read
// nothing yet answers the Gregorian calendar of the board's own rule, and `cfg::boot` puts these back before it reads
// the store so that a second boot of the same object —the tests— does not inherit the run before (docs/60).
bool g_leap_years = true;
uint8_t g_week_start = 1;

// **A year is a leap year when four divides it, except the centuries, except every four hundred**: 2000 was and 1900
// was not, which is the whole of it (docs/60).
bool gregorian_leap(uint16_t year) { return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0); }

// The month as the Gregorian rule has it, which is what the weekday of a date is checked against: the setting of the
// leap day may hide the 29th of February from a page and cannot move the day of the week a date falls on (docs/60).
uint8_t month_length(uint16_t year, uint8_t month) {
  if (month < 1 || month > 12) return 0;
  if (month != 2) return kMonthDays[month - 1];
  return static_cast<uint8_t>(gregorian_leap(year) ? 29 : 28);
}

}  // namespace

uint8_t days_in_month(uint16_t year, uint8_t month) {
  if (month < 1 || month > 12) return 0;
  // **The leap day is the setting**: a calendar with it turned off says February has twenty-eight days in every year,
  // and everything that counts days of a month —the grid of `ui.month`, an app's own page— asks here (docs/60).
  if (month == 2 && !g_leap_years) return 28;
  return month_length(year, month);
}

bool leap_years() { return g_leap_years; }

void set_leap_years(bool on) { g_leap_years = on; }

uint8_t week_start() { return g_week_start; }

bool set_week_start(uint8_t first) {
  if (first < 1 || first > 7) return false;
  g_week_start = first;
  return true;
}

uint8_t weekday_of(uint16_t year, uint8_t month, uint8_t day) {
  // **The weekday of a date is not the setting**: February the 29th falls on the day it falls, and a calendar that
  // hides the day does not move the sky. The check is the Gregorian month, always (docs/60).
  if (month < 1 || month > 12 || day < 1 || day > month_length(year, month)) return 0;
  uint32_t y = year;
  if (month < 3) --y;  // January and February belong to the year before, which is what makes February's length fit
  const uint32_t w = (y + y / 4 - y / 100 + y / 400 + kSakamotoShift[month - 1] + day) % 7;
  // Zero is Sunday in that count and seven is Sunday here, because a week of this system begins on Monday.
  return static_cast<uint8_t>((w == 0) ? 7 : w);
}

}  // namespace calendar
}  // namespace kernel
