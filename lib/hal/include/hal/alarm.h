// How a clock chip is told to wake the board, using everything it has.
//
// The chips of this family are not one alarm with a minute hand: they have **two** ways to
// pull the interrupt line, and picking the right one is the difference between a wake that is
// on the second and a wake that is a minute late (docs/50).
//
//   - The **countdown timer** (Timer_control 0x0E, Timer 0x0F) counts down from 1 to 255
//     ticks of a source the control register picks. With one tick a second it reaches 255 s
//     with **one-second resolution**; with one tick a minute it reaches 255 min (4 h 15 min)
//     with one-minute resolution. It is relative, so it does not care what the hour is.
//   - The **clock alarm** (0x09..0x0C) compares the minute, the hour, the day and the weekday
//     and fires when every field that is enabled matches. It has **no seconds**, so it fires
//     on the minute, and it is the one that reaches beyond four hours.
#ifndef LUNOK_HAL_ALARM_H
#define LUNOK_HAL_ALARM_H

#include <stdint.h>

#include "hal/civil.h"

namespace hal {

// A field of the clock alarm that is not compared. Bit 7 is the only way to say "I do not
// care", and it is what makes an alarm repeat every day instead of once a month.
const uint8_t kAlarmAny = 0x80;

// The timer's reach, in ticks: the register is one byte.
const uint8_t kMaxTimerTicks = 255;
const int64_t kTimerSecondMs = 1000;
const int64_t kTimerMinuteMs = 60000;
// 255 ticks of one second, and of one minute.
const int64_t kMaxTimerSecondsMs = static_cast<int64_t>(kMaxTimerTicks) * kTimerSecondMs;
const int64_t kMaxTimerMinutesMs = static_cast<int64_t>(kMaxTimerTicks) * kTimerMinuteMs;
// Beyond four hours only the clock alarm is left, and it cannot look further than the next
// match of its hour and minute, which is at most a day away. That is not a limit of the
// system: a deadline further away is armed **in stages** —the chip fires on the first match,
// the board finds nothing due and arms again— and it is still the quartz that counts, at the
// price of one spare boot per day (docs/60).

// Which of the two the chip is going to use, if either.
enum class WakePlan : uint8_t {
  kNone = 0,  // this chip cannot be told to wake the board that far away
  kTimerSeconds = 1,
  kTimerMinutes = 2,
  kAlarmClock = 3
};

struct WakeFields {
  WakePlan plan;
  uint8_t ticks;    // 1..255, for the two timer plans
  uint8_t minute;   // binary coded decimal, for the clock alarm
  uint8_t hour;     // binary coded decimal
  uint8_t day;      // kAlarmAny: the day of the month is not compared
  uint8_t weekday;  // kAlarmAny: this system does not count weekdays
};

struct AlarmFields {
  uint8_t minute;   // BCD, compared
  uint8_t hour;     // BCD, compared
  uint8_t day;      // kAlarmAny: the day of the month is not compared
  uint8_t weekday;  // kAlarmAny: this system does not count weekdays
};

// The fields that make the chip fire at `epoch_ms`: **the hour and the minute, and nothing
// else**, so the chip fires at the next time the clock reads them —today if that moment is
// still to come, tomorrow if not—. That is right for any sleep shorter than a day and it is
// the only thing a chip without a year register can promise; a sleep longer than a day is
// not this board's to take (docs/50).
//
// The chip has no seconds in its alarm, so a deadline that is not on the minute is **rounded
// up**: a board woken before it is due finds nothing to do, arms the same hour and minute
// again, and that is the next day —a morning lost—. Waking up to 59 s late costs nothing and
// the app is told how late it was (`late_ms`, docs/30).
inline AlarmFields alarm_fields(int64_t epoch_ms) {
  const int64_t minute_ms = 60LL * 1000;
  int64_t at = epoch_ms;
  if (at % minute_ms != 0) at += minute_ms - (at % minute_ms);

  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  epoch_ms_to_civil(at, year, month, day, hour, minute, second);
  AlarmFields out;
  out.minute = bin_to_bcd(static_cast<uint8_t>(minute));
  out.hour = bin_to_bcd(static_cast<uint8_t>(hour));
  out.day = kAlarmAny;
  out.weekday = kAlarmAny;
  return out;
}

// The ticks a countdown of `wait_ms` needs: rounded **up**, never down, because a tick short
// is a board woken before its hour.
inline uint8_t timer_ticks(int64_t wait_ms, int64_t tick_ms) {
  if (wait_ms <= 0 || tick_ms <= 0) return 0;
  int64_t ticks = (wait_ms + tick_ms - 1) / tick_ms;
  if (ticks < 1) ticks = 1;
  if (ticks > static_cast<int64_t>(kMaxTimerTicks)) return 0;
  return static_cast<uint8_t>(ticks);
}

// The best way this chip can be told to wake the board at `at_ms`, being now `now_ms`: the
// shortest and most exact thing it has that reaches that far. A wait it cannot express comes
// back as `kNone`, and the board must not be powered off on that (docs/50).
inline WakeFields wake_for(int64_t now_ms, int64_t at_ms) {
  WakeFields out = {};
  out.plan = WakePlan::kNone;
  out.day = kAlarmAny;
  out.weekday = kAlarmAny;
  const int64_t wait = at_ms - now_ms;
  if (wait <= 0) return out;

  // Seconds first: it is the most exact the chip gets.
  if (wait <= kMaxTimerSecondsMs) {
    const uint8_t ticks = timer_ticks(wait, kTimerSecondMs);
    if (ticks > 0) {
      out.plan = WakePlan::kTimerSeconds;
      out.ticks = ticks;
      return out;
    }
  }
  if (wait <= kMaxTimerMinutesMs) {
    const uint8_t ticks = timer_ticks(wait, kTimerMinuteMs);
    if (ticks > 0) {
      out.plan = WakePlan::kTimerMinutes;
      out.ticks = ticks;
      return out;
    }
  }
  // Everything else, however far: the clock alarm on its next match, and the board comes back
  // early to arm the rest. Only a moment already gone is not a wake at all.
  const AlarmFields fields = alarm_fields(at_ms);
  out.plan = WakePlan::kAlarmClock;
  out.minute = fields.minute;
  out.hour = fields.hour;
  out.day = fields.day;
  out.weekday = fields.weekday;
  return out;
}

// How far from the hour it was given a sleep came back, in milliseconds: positive is late.
//
// `uptime_ms` is how long the board has been running since it started, and it is **taken out**
// when the sleep ended in a restart. A board that restarts does not report at the instant it woke:
// the bootloader, the banner, the flash, the card and the settings are about a second of firmware,
// and counting that second as the clock's error is measuring the wrong thing —it made a 20 s sleep
// look like a 6 % error on the glass (measured the 31/10, docs/60)—. What is left is the error of
// the sleep itself.
inline int64_t wake_error_ms(int64_t now_epoch_ms, int64_t uptime_ms, int64_t at_epoch_ms,
                             bool restarted) {
  const int64_t at_the_wake = restarted ? (now_epoch_ms - uptime_ms) : now_epoch_ms;
  return at_the_wake - at_epoch_ms;
}

}  // namespace hal

#endif  // LUNOK_HAL_ALARM_H
