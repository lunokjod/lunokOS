// The clock of a board that carries a chip with a battery on top of the chip's own RTC
// domain: the Paper's BM8563, and any board that adds a clock like it (docs/50).
//
// The counting stays with the ESP32 —it is the monotonic, the wake source and the thing that
// keeps running without an I2C transaction per second— and the chip is the **memory**: the
// hour is read from it once, when the board boots, and written to it every time somebody sets
// the epoch, which is what `ntp` does. That is what makes the hour survive the cable.
#ifndef LUNOK_ESP32_RTC_CLOCK_H
#define LUNOK_ESP32_RTC_CLOCK_H

#include "esp32/bm8563.h"
#include "hal/hal.h"

namespace hal {

class RtcClock : public IClock {
 public:
  RtcClock(Bm8563* chip, IClock* inner) : chip_(chip), inner_(inner) {}

  // Reads the chip, once, at boot. A chip that lost its cell does not make the board believe
  // a number: the epoch stays unset and `source()` says so (docs/60).
  void adopt();

  // The hour, back from the chip. After a sleep that came back, the SoC's counter is the one
  // that drifted while the quartz did not, so the chip corrects it instead of the other way
  // round (docs/60).
  void resync() override;

  int64_t epoch_ms() override { return inner_->epoch_ms(); }
  int64_t monotonic_ms() override { return inner_->monotonic_ms(); }

  // **One alarm, one keeper.** The queue is ordered and only its nearest entry is ever armed, and
  // on this board the nearest is the chip's: its line reaches the power circuit, so it is what
  // brings a powered-off board back, and its quartz is what counts the wait. When the alarm fires
  // the chip is disarmed at boot (`adopt`) and the kernel arms the next one, which is the same
  // dance the SoC's timer did before there was a chip.
  //
  // The SoC's timer is used **only** when the chip cannot be told —no answer on the bus, no hour to
  // write, or a deadline the chip cannot express—, because arming both would be two truths about
  // one deadline and one of them would be wrong (docs/60).
  bool arm_wake(int64_t at_epoch_ms) override {
    const int64_t now_ms = inner_->epoch_ms();
    if (chip_ != nullptr && now_ms > 0 && chip_->write_epoch_ms(now_ms) &&
        chip_->arm_at(at_epoch_ms)) {
      // Nothing else keeps this deadline: the SoC's timer is taken back down.
      inner_->disarm_wake();
      return true;
    }
    // Whatever the chip had armed is not this deadline any more.
    if (chip_ != nullptr) chip_->disarm();
    return inner_->arm_wake(at_epoch_ms);
  }

  void disarm_wake() override {
    if (chip_ != nullptr) chip_->disarm();
    inner_->disarm_wake();
  }
  WakeCause last_wake() override {
    // The chip's alarm is a better answer than what the SoC's reset reason says: after a power
    // off there is no reset reason to read, only this flag (docs/60).
    if (woke_by_alarm_) return WakeCause::kClock;
    return inner_->last_wake();
  }
  bool can_wake() const override { return inner_->can_wake(); }
  bool set_epoch_ms(int64_t epoch_ms) override;
  const char* source() const override { return source_; }

  // Whether the chip's alarm is what started this boot —the board was powered off and the
  // clock brought it back—, said by the chip and not guessed: the flag is read once, at
  // boot, before the alarm is taken down (docs/60).
  bool woke_by_alarm() const { return woke_by_alarm_; }

 private:
  // Reads the chip and seeds the SoC with it, or says what the hour really is when the chip
  // cannot be believed (docs/60).
  void take_hour();

  Bm8563* chip_;
  IClock* inner_;
  bool woke_by_alarm_ = false;
  // "rtc" while a battery-backed chip is the keeper of the hour, "soc" when the chip could
  // not be written and the hour is only in the RAM of the chip, "none" while nobody has set
  // it (docs/60).
  const char* source_ = "none";
};

}  // namespace hal

#endif  // LUNOK_ESP32_RTC_CLOCK_H
