// The sleep of the ESP32: the RTC timer, the two depths, and the frequency of the
// processor.
#include <cstdio>

#include <esp_sleep.h>

#include <driver/gpio.h>

// The core's own speed control: it is the one that also moves the APB and the
// FreeRTOS tick when the frequency changes (read on disk, docs/50).
#include <esp32-hal-cpu.h>

#include "hal/log.h"
#include "esp32/clock.h"
#include "esp32/power.h"

namespace hal {

Esp32Power::Esp32Power(int wake_pin, int second_wake_pin)
    : wake_pin_(wake_pin), second_wake_pin_(second_wake_pin) {}

// **Dues portes alhora, quan el xip en sap** (0.190.29): `ext1` on a chip after the ESP32 has an «any of them low»
// mode, and the two lines a board like the T5 E-Paper S3 Pro wires to it are exactly that —a button to ground and the
// open-drain INT of a clock chip—, so one call arms both. **On the ESP32 that mode does not exist**: its two are «all
// low», which would ask for the key *and* the alarm at once, and «any high», which is the idle state of both lines;
// o sigui que allà es queda la porta d'`ext0` i la segona no s'arma (docs/50, docs/60).
bool Esp32Power::arm_the_doors() {
#if !defined(CONFIG_IDF_TARGET_ESP32) || !CONFIG_IDF_TARGET_ESP32
  if (wake_pin_ >= 0 && second_wake_pin_ >= 0 && wake_pin_ < 32 && second_wake_pin_ < 32) {
    const uint64_t mask = (1ULL << wake_pin_) | (1ULL << second_wake_pin_);
    if (esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW) == ESP_OK) return true;
    hal::logf(pal::kLogWarn, "POWER", "the chip did not take the two doors (%d and %d); the key alone is armed",
              wake_pin_, second_wake_pin_);
  }
#endif
  if (wake_pin_ < 0) return false;
  return esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(wake_pin_), 0) == ESP_OK;
}

bool Esp32Power::battery(int& mv, int& pct, bool& charging) {
  (void)mv;
  (void)pct;
  (void)charging;
  return false;
}

void Esp32Power::say_the_way_down(SleepMode mode, int64_t at_epoch_ms) {
  if (at_epoch_ms > 0) {
    hal::logf(pal::kLogInfo, "POWER", "%s sleep until %lld", (mode == SleepMode::kDeep) ? "deep" : "light",
              static_cast<long long>(at_epoch_ms));
  } else {
    hal::logf(pal::kLogInfo, "POWER", "%s sleep until a key of the board",
              (mode == SleepMode::kDeep) ? "deep" : "light");
  }
}

bool Esp32Power::sleep(SleepMode mode, int64_t at_epoch_ms, bool keep_the_panel) {
  // A board with no panel of its own has nothing to keep: the whole of it is the chip (docs/50).
  (void)keep_the_panel;
  // A key that wakes the chip is the one thing a person can press, and it works from
  // either sleep. A board without one and without a deadline would be a board that is
  // gone until the cable is pulled, and the kernel refuses that on its own as well
  // (docs/60).
  const bool a_door = arm_the_doors();
  if (at_epoch_ms > 0) {
    // The timer is armed in one place only, so that taking it down again knows whether it is
    // ours (docs/60).
    esp32_arm_timer_wake(at_epoch_ms);
  } else if (!a_door) {
    return false;
  }
  say_the_way_down(mode, at_epoch_ms);
  // The chip goes quiet with the clocks: what is still in the UART has to leave
  // before, or the line that says why it sleeps never arrives.
  fflush(stdout);

  if (mode == SleepMode::kDeep) {
    esp_deep_sleep_start();  // does not return: the board restarts (docs/90)
    return false;
  }
  const int64_t before = esp32_wall_ms();
  esp_light_sleep_start();
  hal::logf(pal::kLogInfo, "POWER", "awake after %lld ms (%d)",
            static_cast<long long>(esp32_wall_ms() - before),
            static_cast<int>(esp_sleep_get_wakeup_cause()));
  return true;
}

const char* Esp32Power::wakes() {
  if (wake_pin_ >= 0 && second_wake_pin_ >= 0) return "timer, button, clock";
  return (wake_pin_ >= 0) ? "timer, button" : "timer";
}

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
