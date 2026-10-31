// Board: the M5 Paper v1.0 and v1.1 (ESP32-D0WDQ6-V3, 16 MB of flash, 8 MB of
// PSRAM, a 4.7" e-ink panel of 960 by 540 with an IT8951E controller, GT911 touch,
// BM8563 clock, SHT30 sensor, FM24C02 EEPROM, microSD and a 1150 mAh battery).
// See docs/50-hal-i-plaques.md for the sheet and for what is deliberately absent.
//
// What this file has today, in the order the kernel needs it: the power of the
// board (which has to be held by software or the board switches itself off), the
// SoC clock, the serial console, LittleFS and the key-value store. The panel, the
// touch, the RTC chip, the sensor, the SD card, the three keys, the battery gauge
// and the radio are **not** here yet and the capabilities say so instead of
// pretending (docs/90).
//
// No <Arduino.h> here: this file talks to the IDF directly, like the ATOM's HAL,
// and the one thing it needs from the core is the speed of the processor, which
// has a header of its own (docs/80).
#include <cstdio>
#include <cstring>

#include <esp_sleep.h>
#include <esp_timer.h>
#include <sys/time.h>

#include <driver/gpio.h>

#include <esp32-hal-cpu.h>

#include "board/identity.h"
#include "hal/hal.h"
#include "paper_nvs.h"
#include "paper_storage.h"

namespace hal {
namespace {

// The pins this file touches, from the maker's own library (docs/50). They are
// outputs of the board and not of the chip:
//   - the main power has to be held high within a second of the button being
//     pressed, or the board cuts itself off, which looks exactly like a crash;
//   - the EPD supply and the one of the three HY2.0 ports are switched, so that a
//     board that does not use them does not pay for them.
const int kMainPowerPin = 2;
const int kExtPowerPin = 5;
const int kEpdPowerPin = 23;

// Held high for as long as the board is on, and set before anything else can fail.
void hold_power() {
  gpio_config_t out = {};
  out.pin_bit_mask = 1ULL << kMainPowerPin;
  out.mode = GPIO_MODE_OUTPUT;
  out.pull_up_en = GPIO_PULLUP_DISABLE;
  out.pull_down_en = GPIO_PULLDOWN_DISABLE;
  out.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&out);
  gpio_set_level(static_cast<gpio_num_t>(kMainPowerPin), 1);
}

// The SoC clock: it counts while there is power and is lost when the power goes.
// The RTC keeps counting through a deep sleep, and the epoch survives in the RTC
// memory: without that flag a board that woke from a deep sleep would say it has
// never known the date, which is the one moment it does (docs/60).
//
// The board also carries a BM8563 with a coin cell behind it, which is the clock
// that survives the cable being pulled. It is on the internal I2C bus and it is
// not read yet, so `time.caps().source` says "soc" and never "rtc" (docs/90).
int64_t wall_ms() {
  struct timeval tv;
  if (gettimeofday(&tv, nullptr) != 0) return 0;
  return static_cast<int64_t>(tv.tv_sec) * 1000 + static_cast<int64_t>(tv.tv_usec) / 1000;
}

// Survives a deep sleep: it is the RTC memory and nothing else (docs/50).
RTC_DATA_ATTR bool g_epoch_known = false;
RTC_DATA_ATTR int64_t g_armed_epoch_ms = 0;

class PaperClock : public IClock {
 public:
  // Until somebody sets it, this board's gettimeofday counts from the boot, not
  // from 1970: reporting that number as an epoch would be a lie. Without an epoch
  // the answer is 0 and the source is "none" (docs/60).
  int64_t epoch_ms() override { return (known_ || g_epoch_known) ? wall_ms() : 0; }

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

  // The RTC timer is the only wake this board has today: the three keys would be
  // the other one and they are not read yet (docs/50).
  bool arm_wake(int64_t at_epoch_ms) override {
    int64_t left = at_epoch_ms - wall_ms();
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
        return WakeCause::kOther;
    }
  }

  bool can_wake() const override { return true; }

  const char* source() const override {
    return (known_ || g_epoch_known) ? "soc" : "none";
  }

 private:
  bool known_ = false;
};

class PaperPower : public IPower {
 public:
  // The board has a 1150 mAh cell and a divider on GPIO35 to read it, and neither
  // is calibrated here: saying "I cannot tell" is the truth until somebody puts a
  // meter on it (docs/90). `power.battery()` says nil, which is not zero volts.
  bool battery(int& mv, int& pct, bool& charging) override {
    (void)mv;
    (void)pct;
    (void)charging;
    return false;
  }

  // A sleep with no deadline is a board that is gone: the console does not wake
  // it and the keys are not read yet. The kernel refuses one on its own as well
  // (docs/60).
  bool sleep(SleepMode mode, int64_t at_epoch_ms) override {
    if (at_epoch_ms <= 0) return false;
    int64_t left = at_epoch_ms - wall_ms();
    if (left < 1) left = 1;
    if (esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(left) * 1000) != ESP_OK) {
      return false;
    }
    if (mode == SleepMode::kDeep) {
      esp_deep_sleep_start();  // does not return
      return false;
    }
    return esp_light_sleep_start() == ESP_OK;
  }

  // The keys of this board (`PWR` included) switch the main power off instead of
  // waking the chip, so the timer is what brings it back (docs/50).
  const char* wakes() override { return "timer"; }

  uint32_t cpu_mhz() override { return getCpuFrequencyMhz(); }

  bool set_cpu_mhz(uint32_t mhz) override {
    if (mhz != 80 && mhz != 160 && mhz != 240) return false;
    if (!setCpuFrequencyMhz(mhz)) return false;
    // A chip rated for 160 only answers 160 when asked for 240: the answer is
    // what the board is really running at, not what it was asked for.
    return getCpuFrequencyMhz() == mhz;
  }
};

class PaperPlatform : public IPlatform {
 public:
  const char* id() const override { return board::id(); }
  const char* family() const override { return board::family(); }
  bool emulator() const override { return board::emulator(); }

  // Built on first use and never in the constructor: this object is constructed
  // before setup() runs, and the power hold wants the GPIO driver awake. Mounting
  // LittleFS that early is not allowed either (the ATOM learned the same thing).
  const Caps& caps() const override {
    if (!built_) {
      built_ = true;
      hold_power();
      caps_.add(kCapConsole);
      caps_.add(kCapAnsi);
      // /littlefs is mandatory: it holds the apps. It is advertised only when the
      // partition really mounts.
      if (paper_storage("/littlefs") != nullptr) caps_.add(kCapLittlefs);
      // The SoC keeps the epoch while there is power; the BM8563 is not read yet.
      caps_.add(kCapRtc);
      if (paper_nvs() != nullptr) caps_.add(kCapNvs);
      // Both depths of sleep, and a clock that can be armed: the RTC timer of the
      // chip is the wake (docs/50).
      caps_.add(kCapSleepLight);
      caps_.add(kCapSleepDeep);
      caps_.add(kCapRtcWake);
    }
    return caps_;
  }

  // Zero until the ceiling of this board is measured, which makes the kernel use
  // its own default (docs/75).
  uint32_t heap_limit_kb() const override { return 0; }

  IClock* clock() override { return &clock_; }
  IStorage* storage(const char* mount) override { return paper_storage(mount); }
  IKeyValue* nvs() override { return paper_nvs(); }
  // The radio, the panel, the keys, the sensor and the gauge are not here yet:
  // nullptr, and the capability stays off (docs/50, docs/90).
  INet* net() override { return nullptr; }
  IPower* power() override { return &power_; }
  IDisplay* display() override { return nullptr; }
  ILed* led() override { return nullptr; }
  IInput* input() override { return nullptr; }
  IImu* imu() override { return nullptr; }

 private:
  mutable Caps caps_;
  mutable bool built_ = false;
  PaperClock clock_;
  PaperPower power_;
};

PaperPlatform g_platform;

}  // namespace

IPlatform& platform() { return g_platform; }

}  // namespace hal
