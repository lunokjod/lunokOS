#ifndef LUNOK_KERNEL_CALENDAR_H
#define LUNOK_KERNEL_CALENDAR_H

#include <stdint.h>

namespace kernel {
namespace calendar {

// **The days of a month in the Gregorian calendar**: February has its twenty-ninth day when the year is a leap one,
// always, perquè un any de traspàs és un fet del calendari i no una tria d'aquesta placa (0.192.57). The rule itself is
// `hal::is_leap_year`, one copy for the whole system; a second one written in an app is a second answer, and the old
// system learned that a calendar with two of them disagrees with itself twice a year (OLD/PaperOS/src/ui/month_grid.h:120,
// docs/50, docs/60, docs/90).
//
// `month` is 1 to 12 and anything else is zero days: a month that does not exist has no days and not thirty-one.
uint8_t days_in_month(uint16_t year, uint8_t month);

// **Which weekday a date falls on**: **1 is Monday and 7 is Sunday**, which is how a week is written in a calendar
// here —the week starts on Monday and the weekend is the two last columns— and it is the one number the grid needs
// to know where the first day of a month goes. Zero when the date does not exist.
uint8_t weekday_of(uint16_t year, uint8_t month, uint8_t day);

// **L'única decisió d'un calendari que una persona pot moure** (0.170.0, docs/60, docs/90): which weekday a week
// begins on. **La del dia de traspàs va marxar** (0.192.57, l'amo: *«sabem com calcular si es any de taspàs o no ho es… no
// cal un toggle manual»*): a leap year is a fact of the Gregorian calendar and not a preference, i el que es pregunta es
// calcula de la data i prou. `week_start()` is 1 —Monday— because that is the week this calendar was born with, and 7 is
// the Sunday a person may ask for. **It does not change which day a date falls on**: `weekday_of` is a fact of the sky
// and this only turns the seven columns of a week.
uint8_t week_start();
bool set_week_start(uint8_t first);

}  // namespace calendar
}  // namespace kernel

#endif  // LUNOK_KERNEL_CALENDAR_H
