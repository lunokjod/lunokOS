// The BM8563, register by register (docs/50).
//
// The hour lives at 0x02..0x08 in the order a person reads it —seconds first—, in binary
// coded decimal, with two bits that are not digits: bit 7 of the seconds is **VL** and says
// the chip lost its supply, and bit 7 of the month is the century. Everything else is the
// control registers, which are only touched to stop and start the oscillator.
#include "esp32/bm8563.h"

#include "hal/alarm.h"
#include "hal/civil.h"

namespace hal {
namespace {

const uint8_t kControl1 = 0x00;
const uint8_t kControl2 = 0x01;
const uint8_t kFirstTimeRegister = 0x02;  // seconds, minutes, hours, day, weekday, month, year
const uint8_t kTimeRegisters = 7;

// Control 1: TEST and STOP. Stopping the oscillator is what the chip does when it is out of
// power, and starting it is the first thing that has to happen after a write.
const uint8_t kStop = 0x20;
// Control 2: the interrupt and alarm flags. Cleared on start so a stale alarm does not fire
// on the first boot with a fresh cell.
const uint8_t kControl2Clear = 0x00;
// Control 2, bit by bit, and the two enable bits are not where a person would guess: the
// **timer** is bit 0 and the **alarm** is bit 1, the timer flag is bit 2 and the alarm flag
// is bit 3 (docs/50, and the maker's own BM8563 driver does the same).
const uint8_t kTimerInterrupt = 0x01;
const uint8_t kAlarmInterrupt = 0x02;
const uint8_t kTimerFlag = 0x04;
const uint8_t kAlarmFlag = 0x08;
// The alarm registers: minute, hour, day and weekday, in that order (docs/50).
const uint8_t kFirstAlarmRegister = 0x09;
const uint8_t kAlarmRegisters = 4;
// The countdown: Timer_control 0x0E picks the source and starts it, Timer 0x0F is the count.
// Bit 7 is TE, and the two low bits pick the tick: 10 is one second, 11 is one minute.
const uint8_t kTimerControl = 0x0E;
const uint8_t kTimerValue = 0x0F;
const uint8_t kTimerSecondsSource = 0x82;
const uint8_t kTimerMinutesSource = 0x83;

}  // namespace

Bm8563::Bm8563(II2c* bus, uint8_t address) : bus_(bus), address_(address) {}

bool Bm8563::start() {
  if (bus_ == nullptr) return false;
  const uint8_t clear[2] = {kControl1, static_cast<uint8_t>(kControl1 & ~kStop)};
  if (!bus_->write(address_, clear, sizeof(clear))) return false;
  const uint8_t clear2[2] = {kControl2, kControl2Clear};
  return bus_->write(address_, clear2, sizeof(clear2));
}

bool Bm8563::read_epoch_ms(int64_t& epoch_ms) {
  if (bus_ == nullptr) return false;
  uint8_t raw[kTimeRegisters] = {0};
  if (!bus_->read(address_, kFirstTimeRegister, raw, sizeof(raw))) return false;

  // The chip lost its supply and its hour is whatever the registers said when it stopped.
  // Starting it again does not make the number true, so this is not a reading at all.
  if ((raw[0] & 0x80) != 0) return false;

  const int second = bcd_to_bin(static_cast<uint8_t>(raw[0] & 0x7F));
  const int minute = bcd_to_bin(static_cast<uint8_t>(raw[1] & 0x7F));
  const int hour = bcd_to_bin(static_cast<uint8_t>(raw[2] & 0x3F));
  const int day = bcd_to_bin(static_cast<uint8_t>(raw[3] & 0x3F));
  const int month = bcd_to_bin(static_cast<uint8_t>(raw[5] & 0x1F));
  const int year = 2000 + bcd_to_bin(raw[6]) - ((raw[5] & 0x80) != 0 ? 100 : 0);

  // A date outside the calendar is a chip that is not saying anything usable: month 13 is
  // not a month and a day 0 is not a day (docs/50).
  if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 ||
      second > 59) {
    return false;
  }
  epoch_ms = civil_to_epoch_ms(year, month, day, hour, minute, second);
  return true;
}

bool Bm8563::write_epoch_ms(int64_t epoch_ms) {
  if (bus_ == nullptr) return false;
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  epoch_ms_to_civil(epoch_ms, year, month, day, hour, minute, second);

  // Two digits of a year and a century bit, which is all the chip has: a date before 2000 or
  // after 2099 is not something this driver writes (docs/50).
  if (year < 2000 || year > 2099) return false;

  uint8_t frame[1 + kTimeRegisters] = {0};
  frame[0] = kFirstTimeRegister;
  // Seconds with VL cleared: writing the hour is what makes the chip trustworthy again.
  frame[1] = bin_to_bcd(static_cast<uint8_t>(second));
  frame[2] = bin_to_bcd(static_cast<uint8_t>(minute));
  frame[3] = bin_to_bcd(static_cast<uint8_t>(hour));
  frame[4] = bin_to_bcd(static_cast<uint8_t>(day));
  frame[5] = 0;  // the weekday: the chip wants one and nothing in this system uses it
  frame[6] = bin_to_bcd(static_cast<uint8_t>(month));  // the century bit stays 0: 20xx
  frame[7] = bin_to_bcd(static_cast<uint8_t>(year - 2000));

  if (!start()) return false;
  if (!bus_->write(address_, frame, sizeof(frame))) return false;
  return true;
}

bool Bm8563::arm_at(int64_t epoch_ms) {
  if (bus_ == nullptr) return false;
  // The chip has to know the hour even for the countdown: a board woken by a timer still
  // comes back to a system that has to know when it is (docs/60).
  int64_t now_ms = 0;
  if (!read_epoch_ms(now_ms)) return false;

  const WakeFields wake = wake_for(now_ms, epoch_ms);
  switch (wake.plan) {
    case WakePlan::kTimerSeconds:
    case WakePlan::kTimerMinutes: {
      // The count before the control: the countdown starts the moment the source is picked,
      // so the other order would start it on the count of the alarm before this one.
      const uint8_t count[2] = {kTimerValue, wake.ticks};
      if (!bus_->write(address_, count, sizeof(count))) return false;
      const uint8_t source[2] = {kTimerControl,
                                 (wake.plan == WakePlan::kTimerSeconds) ? kTimerSecondsSource
                                                                        : kTimerMinutesSource};
      if (!bus_->write(address_, source, sizeof(source))) return false;
      const uint8_t enable[2] = {kControl2, kTimerInterrupt};
      return bus_->write(address_, enable, sizeof(enable));
    }
    case WakePlan::kAlarmClock: {
      uint8_t frame[1 + kAlarmRegisters] = {0};
      frame[0] = kFirstAlarmRegister;
      frame[1] = wake.minute;
      frame[2] = wake.hour;
      frame[3] = wake.day;
      frame[4] = wake.weekday;
      if (!bus_->write(address_, frame, sizeof(frame))) return false;
      // The interrupt after the fields: the other order would let the chip fire on the
      // fields of the alarm before this one.
      const uint8_t enable[2] = {kControl2, kAlarmInterrupt};
      return bus_->write(address_, enable, sizeof(enable));
    }
    default:
      // Further away than this chip can be told, or already past: nothing is armed and the
      // board must stay on (docs/50).
      return false;
  }
}

bool Bm8563::disarm() {
  if (bus_ == nullptr) return false;
  // Everything at once: the two enables, the two flags and the pulse bit. Writing zero
  // takes the interrupt back down and stops the countdown.
  const uint8_t clear[2] = {kControl2, kControl2Clear};
  const uint8_t stop[2] = {kTimerControl, 0x00};
  if (!bus_->write(address_, stop, sizeof(stop))) return false;
  return bus_->write(address_, clear, sizeof(clear));
}

bool Bm8563::interrupt_fired() {
  if (bus_ == nullptr) return false;
  uint8_t control2 = 0;
  if (!bus_->read(address_, kControl2, &control2, 1)) return false;
  // Either one: the alarm and the countdown both pull the same line, and both mean the chip
  // is what brought the board back (docs/60).
  return (control2 & (kAlarmFlag | kTimerFlag)) != 0;
}

}  // namespace hal
