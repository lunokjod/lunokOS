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

}  // namespace

uint8_t days_in_month(uint16_t year, uint8_t month) {
  if (month < 1 || month > 12) return 0;
  if (month != 2) return kMonthDays[month - 1];
  // **A year is a leap year when four divides it, except the centuries, except every four hundred**: 2000 was and
  // 1900 was not, which is the whole of it (docs/60).
  const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
  return static_cast<uint8_t>(leap ? 29 : 28);
}

uint8_t weekday_of(uint16_t year, uint8_t month, uint8_t day) {
  if (month < 1 || month > 12 || day < 1 || day > days_in_month(year, month)) return 0;
  uint32_t y = year;
  if (month < 3) --y;  // January and February belong to the year before, which is what makes February's length fit
  const uint32_t w = (y + y / 4 - y / 100 + y / 400 + kSakamotoShift[month - 1] + day) % 7;
  // Zero is Sunday in that count and seven is Sunday here, because a week of this system begins on Monday.
  return static_cast<uint8_t>((w == 0) ? 7 : w);
}

}  // namespace calendar
}  // namespace kernel
