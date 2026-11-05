// Board: the LilyGO TTGO T-Koala (ESP32-WROVER-B, 4 MB of flash, 8 MB of PSRAM of which the chip maps 4,
// a green LED and a microSD slot, and no key the firmware can read). See docs/50-hal-i-plaques.md for the sheet and for what is
// deliberately absent.
//
// What belongs to the chip is not written here: the filesystem, the key-value store, the clock, the sleep,
// the radio and the reader of a key are the ESP32's, all of it in lib/hal_esp32 (docs/50). What is left is
// what only this board can say: which pin carries its light, where the card slot is, and how
// much of the PSRAM the chip can address.
//
// No <Arduino.h> here: this file talks to the IDF directly, like every board HAL of the project.
#include <cstdio>
#include <cstring>

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp32/spiram.h>
#include <esp_heap_caps.h>

#include "board/identity.h"
#include "esp32/caps.h"
#include "esp32/clock.h"
#include "esp32/net.h"
#include "esp32/nvs.h"
#include "esp32/power.h"
#include "esp32/rtc_memory.h"
#include "esp32/sd_card.h"
#include "esp32/storage.h"
#include "hal/hal.h"

namespace hal {
namespace {

// **The green LED is a plain LED on GPIO5**, not an addressable chip: the board drives the pin and there is
// no colour to mix (docs/50).
const uint8_t kLedPin = 5;
const char kDataPartition[] = "littlefs";

// **The card slot is on the HSPI wires the board exposes**: CS GPIO15, SCK GPIO14, MOSI GPIO13 and MISO
// GPIO12. The four are printed as the HSPI group on the maker's pinout next to the slot; the card is the
// device that group is for (docs/50). 10 MHz and not the 20 MHz default: nothing here has been measured on
// a card yet, and a card driven too fast mounts and then times out on the first long read (docs/90).
const SdPins kSdPins = {15, 14, 13, 12, SPI2_HOST, 10000};

// A light with one channel, driven by a GPIO: the painted frame becomes on or off. The colour is lost
// because the hardware has no colour to lose, and `max_value` stays 255 so the system's own effects keep
// the range every page already paints in (docs/50).
class GpioLed : public ILed {
 public:
  explicit GpioLed(uint8_t pin) : pin_(pin) {}

  uint32_t count() override { return 1; }
  uint32_t max_value() override { return 255; }

  bool fill(uint8_t r, uint8_t g, uint8_t b) override { return pixel(0, r, g, b); }

  bool pixel(uint32_t index, uint8_t r, uint8_t g, uint8_t b) override {
    if (index != 0) return false;
    on_ = (r != 0 || g != 0 || b != 0);
    return true;
  }

  void show() override {
    if (!ready_) {
      gpio_config_t out = {};
      out.pin_bit_mask = 1ULL << pin_;
      out.mode = GPIO_MODE_OUTPUT;
      out.pull_up_en = GPIO_PULLUP_DISABLE;
      out.pull_down_en = GPIO_PULLDOWN_DISABLE;
      out.intr_type = GPIO_INTR_DISABLE;
      gpio_config(&out);
      ready_ = true;
    }
    gpio_set_level(static_cast<gpio_num_t>(pin_), on_ ? 1 : 0);
  }

 private:
  uint8_t pin_;
  bool on_ = false;
  bool ready_ = false;
};

class TKoalaPlatform : public IPlatform {
 public:
  const char* id() const override { return board::id(); }
  const char* family() const override { return board::family(); }
  bool emulator() const override { return board::emulator(); }

  const Caps& caps() const override {
    if (!built_) {
      built_ = true;
      // Everything the chip gives, and then only what this board has of its own (docs/50).
      caps_ = esp32_caps();
      // The filesystem is advertised only when it really mounts, the same rule as the card (docs/70).
      if (esp32_storage("/littlefs", kDataPartition) != nullptr) caps_.add(kCapLittlefs);
      if (esp32_sd("/sd", kSdPins) != nullptr) caps_.add(kCapSd);
      // The green LED is the one thing this board does with a person: it has a RST button and no key
      // the firmware can read (docs/50).
      caps_.add(kCapLed);
    }
    return caps_;
  }

  // **The mapped PSRAM is the ceiling of an app**, and a WROOM-32 version of the board that answers zero
  // here keeps the kernel's own ceiling instead of promising memory the chip does not have (docs/75).
  uint32_t heap_limit_kb() const override {
    return (esp_spiram_get_size() > 0) ? 4096 : 0;
  }

  // The three numbers of the extra memory: what the chip says it has, what the ESP32 can address of it,
  // and what is left (docs/50, docs/75).
  ExtraMemory extra_memory() const override {
    ExtraMemory out;
    out.chip_bytes = static_cast<uint32_t>(esp_spiram_get_size());
    out.total_bytes = static_cast<uint32_t>(heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    out.free_bytes = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return out;
  }

  IClock* clock() override { return esp32_clock(); }

  // Two filesystems on this board: the flash and, when there is a card in the slot, the card. The RTC
  // memory is a file of the chip and not of the board (docs/50, docs/70).
  //
  // **Each mount is the one that is named, and no other** (0.94.1): the card answers `/sd` and nothing else. The
  // Paper can fall back on the card for whatever it does not know because it answers `/fram` itself, but this board
  // has **no FRAM**, and a fallback said "not the flash, then the card" —so the `/fram` the VFS asks for at boot got
  // the whole card, a raw memory that is not there with somebody's files under a name that promises 256 bytes—
  // (found while reviewing the board, 5/11, docs/50, docs/70).
  IStorage* storage(const char* mount) override {
    if (mount == nullptr || mount[0] != '/') return nullptr;
    if (strcmp(mount, "/rtc") == 0) return esp32_rtc_memory();
    if (strcmp(mount, "/sd") == 0) return esp32_sd(mount, kSdPins);
    return esp32_storage(mount, kDataPartition);
  }

  IKeyValue* nvs() override { return esp32_nvs(); }
  INet* net() override { return esp32_net(); }
  IPower* power() override { return &power_; }
  // No screen: this board has a serial port and a light, and that is its way in and out (docs/50).
  ISurface* display() override { return nullptr; }
  ILed* led() override { return &led_; }
  // No key: the only button on the board is RST, which resets the chip and is not an input (docs/50).
  IInput* input() override { return nullptr; }
  IImu* imu() override { return nullptr; }

 private:
  mutable Caps caps_;
  mutable bool built_ = false;
  // No key and no wake pin: the board has no button wired to the chip, so the timer is the only wake
  // (docs/50, docs/60).
  Esp32Power power_{-1};
  GpioLed led_{kLedPin};
};

TKoalaPlatform g_platform;

}  // namespace

IPlatform& platform() { return g_platform; }

}  // namespace hal
