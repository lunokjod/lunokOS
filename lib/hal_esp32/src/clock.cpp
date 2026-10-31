// The clock of the ESP32: the SoC's own counter, kept through a deep sleep.
//
// The epoch lives in the RTC memory: without that flag a board that woke from a deep
// sleep would say it has never known the date, which is the one moment it does
// (docs/60). Until somebody sets it, gettimeofday counts from the boot and not from
// 1970, so reporting that number as an epoch would be a lie: without an epoch the
// answer is 0 and the source is "none".
#include <esp_sleep.h>
#include <esp_timer.h>
#include <sys/time.h>

#include "esp32/clock.h"

namespace hal {
namespace {

// Survives a deep sleep: it is the RTC memory and nothing else (docs/50).
RTC_DATA_ATTR bool g_epoch_known = false;
// The deadline the kernel armed, so `last_wake()` can say whether the timer is what
// brought the board back.
RTC_DATA_ATTR int64_t g_armed_epoch_ms = 0;

class Esp32Clock : public IClock {
 public:
  int64_t epoch_ms() override { return (known_ || g_epoch_known) ? esp32_wall_ms() : 0; }

  bool set_epoch_ms(int64_t ms) override {
    if (ms < 0) return false;
    struct timeval tv;
    tv.tv_sec = static_cast<time_t>(ms / 1000);
    tv.tv_usec = static_cast<suseconds_t>((ms % 1000) * 1000);
    if (settimeofday(&tv, nullptr) != 0) return false;
    known_ = true;
    g_epoch_known = true;
    return true;
  }

  int64_t monotonic_ms() override { return static_cast<int64_t>(esp_timer_get_time() / 1000); }

  // The RTC timer is the wake the kernel programs: only the nearest alarm is ever
  // armed, and a key that brings the board back is the board's business (docs/60).
  bool arm_wake(int64_t at_epoch_ms) override {
    int64_t left = at_epoch_ms - esp32_wall_ms();
    if (left < 1) left = 1;
    g_armed_epoch_ms = at_epoch_ms;
    return esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(left) * 1000) == ESP_OK;
  }

  void disarm_wake() override {
    g_armed_epoch_ms = 0;
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  }

  WakeCause last_wake() override {
    switch (esp_sleep_get_wakeup_cause()) {
      case ESP_SLEEP_WAKEUP_TIMER:
        return WakeCause::kTimer;
      case ESP_SLEEP_WAKEUP_UNDEFINED:
        return WakeCause::kPowerOn;
      default:
        // A key or a pin of the board: this chip cannot tell which one from here.
        return WakeCause::kOther;
    }
  }

  bool can_wake() const override { return true; }

  const char* source() const override { return (known_ || g_epoch_known) ? "soc" : "none"; }

 private:
  bool known_ = false;
};

}  // namespace

int64_t esp32_wall_ms() {
  struct timeval tv;
  if (gettimeofday(&tv, nullptr) != 0) return 0;
  return static_cast<int64_t>(tv.tv_sec) * 1000 + static_cast<int64_t>(tv.tv_usec) / 1000;
}

IClock* esp32_clock() {
  static Esp32Clock clock;
  return &clock;
}

}  // namespace hal
