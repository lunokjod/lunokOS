// The chip with a battery on top of the chip's clock: it keeps the hour, it hands it back at
// every boot and after every sleep that came back, and it is the one that arms the wake that
// reaches the power circuit (docs/50, docs/60).
#include "esp32/rtc_clock.h"

namespace hal {

void RtcClock::adopt() {
  // The alarm did its job if it fired, and the flag that says so is lost the moment it is
  // cleared: read first, clear after, and a boot that came from the power button is told
  // apart from one the clock asked for (docs/60).
  if (chip_ != nullptr) {
    woke_by_alarm_ = chip_->interrupt_fired();
    chip_->disarm();
  }
  take_hour();
}

void RtcClock::resync() { take_hour(); }

void RtcClock::take_hour() {
  int64_t kept = 0;
  if (chip_ == nullptr || !chip_->read_epoch_ms(kept)) {
    // No chip, no answer, or a cell that died: the board keeps counting with what the SoC
    // has, and the source says that instead of promising a chip that is not there.
    source_ = (inner_->epoch_ms() > 0) ? "soc" : "none";
    return;
  }
  if (!inner_->set_epoch_ms(kept)) return;
  source_ = "rtc";
}

bool RtcClock::set_epoch_ms(int64_t epoch_ms) {
  if (!inner_->set_epoch_ms(epoch_ms)) return false;
  // The chip keeps it too. If the write fails the hour is only in the RAM of the chip —lost
  // on the next unplug— and `source()` has to say that, not what was hoped for.
  source_ = (chip_ != nullptr && chip_->write_epoch_ms(epoch_ms)) ? "rtc" : "soc";
  return true;
}

}  // namespace hal
