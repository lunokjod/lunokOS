// The sleep of the ESP32: the RTC timer, the two depths, and the frequency of the
// processor.
#include <cstdio>

#include <esp_sleep.h>

#include <driver/gpio.h>

// The core's own speed control: it is the one that also moves the APB and the
// FreeRTOS tick when the frequency changes (read on disk, docs/50).
#include <esp32-hal-cpu.h>

#include "esp32/clock.h"
#include "esp32/power.h"

namespace hal {

Esp32Power::Esp32Power(int wake_pin) : wake_pin_(wake_pin) {}

bool Esp32Power::battery(int& mv, int& pct, bool& charging) {
  (void)mv;
  (void)pct;
  (void)charging;
  return false;
}

bool Esp32Power::sleep(SleepMode mode, int64_t at_epoch_ms) {
  // A key that wakes the chip is the one thing a person can press, and it works from
  // either sleep. A board without one and without a deadline would be a board that is
  // gone until the cable is pulled, and the kernel refuses that on its own as well
  // (docs/60).
  if (wake_pin_ >= 0) esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(wake_pin_), 0);
  if (at_epoch_ms > 0) {
    // The timer is armed in one place only, so that taking it down again knows whether it is
    // ours (docs/60).
    esp32_arm_timer_wake(at_epoch_ms);
  } else if (wake_pin_ < 0) {
    return false;
  }
  printf("power: %s sleep until %lld\n", (mode == SleepMode::kDeep) ? "deep" : "light",
         static_cast<long long>(at_epoch_ms));
  // The chip goes quiet with the clocks: what is still in the UART has to leave
  // before, or the line that says why it sleeps never arrives.
  fflush(stdout);

  if (mode == SleepMode::kDeep) {
    esp_deep_sleep_start();  // does not return: the board restarts (docs/90)
    return false;
  }
  const int64_t before = esp32_wall_ms();
  esp_light_sleep_start();
  printf("power: awake after %lld ms (%d)\n",
         static_cast<long long>(esp32_wall_ms() - before),
         static_cast<int>(esp_sleep_get_wakeup_cause()));
  return true;
}

const char* Esp32Power::wakes() { return (wake_pin_ >= 0) ? "timer, button" : "timer"; }

uint32_t Esp32Power::cpu_mhz() { return getCpuFrequencyMhz(); }

// 240, 160 and 80 and nothing else. Below 80 the APB follows the CPU down and **no
// peripheral of the core reconfigures itself** (there is not one
// onApbChangeCallback registered anywhere in cores/esp32), so the serial port would
// come out at the wrong baud: the board's only interface would be garbage (docs/50).
// A value the board does not do is refused and not rounded.
bool Esp32Power::set_cpu_mhz(uint32_t mhz) {
  if (mhz != 80 && mhz != 160 && mhz != 240) return false;
  if (!setCpuFrequencyMhz(mhz)) return false;
  // A chip rated for 160 only answers 160 when asked for 240: the answer is what the
  // board is really running at, not what it was asked for.
  return getCpuFrequencyMhz() == mhz;
}

}  // namespace hal
