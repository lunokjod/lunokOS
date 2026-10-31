// The civil calendar: a date, an hour and the epoch, with no library and no hardware.
//
// A clock chip counts years, months, days, hours, minutes and seconds; the rest of this
// system counts milliseconds since 1970. Turning one into the other is the part worth
// testing on a PC, because a leap year done wrong is a clock that is a day off once every
// four years and nobody notices until they do (docs/50).
#ifndef LUNOK_HAL_CIVIL_H
#define LUNOK_HAL_CIVIL_H

#include <stdint.h>

namespace hal {

// Binary-coded decimal, which is how the RTC chips of this family keep their registers: a
// nibble per digit, so 25 is 0x25 and not 37.
inline uint8_t bcd_to_bin(uint8_t bcd) {
  return static_cast<uint8_t>((bcd >> 4) * 10 + (bcd & 0x0F));
}

inline uint8_t bin_to_bcd(uint8_t bin) {
  return static_cast<uint8_t>(((bin / 10) << 4) | (bin % 10));
}

// Days since 1970-01-01 for a civil date (month 1..12) and back. This is the algorithm
// everybody uses, and it is chosen for the same reason: no tables —in a chip with 320 KB of
// RAM a table of days is a luxury— and exact for the whole range of a 64-bit count.
inline int64_t days_from_civil(int y, int m, int d) {
  const int64_t year = static_cast<int64_t>(y) - ((m <= 2) ? 1 : 0);
  const int64_t era = (year >= 0 ? year : year - 399) / 400;
  const uint32_t yoe = static_cast<uint32_t>(year - era * 400);              // [0, 399]
  const uint32_t doy = (153u * static_cast<uint32_t>(m + (m > 2 ? -3 : 9)) + 2u) / 5u +
                       static_cast<uint32_t>(d) - 1u;                        // [0, 365]
  const uint32_t doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;             // [0, 146096]
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

inline void civil_from_days(int64_t z, int& y, int& m, int& d) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const uint32_t doe = static_cast<uint32_t>(z - era * 146097);              // [0, 146096]
  const uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
  const int64_t year = static_cast<int64_t>(yoe) + era * 400;
  const uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);           // [0, 365]
  const uint32_t mp = (5u * doy + 2u) / 153u;                                // [0, 11]
  const uint32_t day = doy - (153u * mp + 2u) / 5u + 1u;                     // [1, 31]
  const uint32_t month = mp + (mp < 10u ? 3u : static_cast<uint32_t>(-9));   // [1, 12]
  y = static_cast<int>(year + ((month <= 2u) ? 1 : 0));
  m = static_cast<int>(month);
  d = static_cast<int>(day);
}

// The six fields a clock chip holds, in UTC, and back. UTC on purpose: the chip has no idea
// of a time zone and this project shows local time by adding an offset where it shows it,
// never by storing a shifted hour (docs/30, docs/60).
inline int64_t civil_to_epoch_ms(int y, int mo, int d, int h, int mi, int s) {
  const int64_t seconds = days_from_civil(y, mo, d) * 86400 +
                          static_cast<int64_t>(h) * 3600 + static_cast<int64_t>(mi) * 60 +
                          static_cast<int64_t>(s);
  return seconds * 1000;
}

inline void epoch_ms_to_civil(int64_t ms, int& y, int& mo, int& d, int& h, int& mi, int& s) {
  const int64_t seconds = ms / 1000;
  int64_t days = seconds / 86400;
  int64_t rest = seconds % 86400;
  if (rest < 0) {  // before 1970: the split above rounds towards zero and not down
    rest += 86400;
    days -= 1;
  }
  civil_from_days(days, y, mo, d);
  h = static_cast<int>(rest / 3600);
  mi = static_cast<int>((rest % 3600) / 60);
  s = static_cast<int>(rest % 60);
}

}  // namespace hal

#endif  // LUNOK_HAL_CIVIL_H
