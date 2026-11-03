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

}  // namespace calendar
}  // namespace kernel

#endif  // LUNOK_KERNEL_CALENDAR_H
