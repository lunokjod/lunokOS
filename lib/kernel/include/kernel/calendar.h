#ifndef LUNOK_KERNEL_CALENDAR_H
#define LUNOK_KERNEL_CALENDAR_H

#include <stdint.h>

namespace kernel {
namespace calendar {

// **The days of a month in the Gregorian calendar, and the only place that knows the leap years**: a second rule
// written in an app is a second answer, and the old system learned that a calendar with two of them disagrees with
// itself twice a year (OLD/PaperOS/src/ui/month_grid.h:120, docs/60, docs/90).
//
// `month` is 1 to 12 and anything else is zero days: a month that does not exist has no days and not thirty-one.
uint8_t days_in_month(uint16_t year, uint8_t month);

// **Which weekday a date falls on**: **1 is Monday and 7 is Sunday**, which is how a week is written in a calendar
// here —the week starts on Monday and the weekend is the two last columns— and it is the one number the grid needs
// to know where the first day of a month goes. Zero when the date does not exist.
uint8_t weekday_of(uint16_t year, uint8_t month, uint8_t day);

// **The two decisions of a calendar that a person can move** (0.170.0, docs/60, docs/90): whether February has
// twenty-nine days, and which weekday a week begins on. They live here and not in an app because the rule is this
// file's and a second answer is a calendar that disagrees with itself —and because `ui.month`, `time.utc`/`date` and
// an app's own grid have to say the same thing (docs/30, docs/60).
//
// `leap_years()` is true until somebody turns it off, which is the Gregorian calendar this system was born with;
// `week_start()` is 1 —Monday— for the same reason, and 7 is the Sunday a person may ask for. **The weekday of a
// date does not change with either**: `weekday_of` is a fact of the sky and `days_in_month` is the setting.
bool leap_years();
void set_leap_years(bool on);
uint8_t week_start();
bool set_week_start(uint8_t first);

}  // namespace calendar
}  // namespace kernel

#endif  // LUNOK_KERNEL_CALENDAR_H
