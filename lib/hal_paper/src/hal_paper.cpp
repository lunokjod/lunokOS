// Board: the M5 Paper v1.0 and v1.1 (ESP32-D0WDQ6-V3, 16 MB of flash, 8 MB of
// PSRAM, a 4.7" e-ink panel of 960 by 540 with an IT8951E controller, GT911 touch,
// BM8563 clock, SHT30 sensor, FM24C02 EEPROM, microSD and a 1150 mAh battery).
// See docs/50-hal-i-plaques.md for the sheet and for what is deliberately absent.
//
// What belongs to the chip is not written here: the filesystem, the key-value store,
// the clock, the sleep and the radio are the ESP32's, and the reader for the pins the
// dial is wired to is shared too, all of it in lib/hal_esp32 (docs/50). What this file
// keeps is the one thing the board cannot do without —the main power hold, which has to
// be set before anything else can fail— and where its three keys are.
//
// The panel, the touch, the RTC chip, the sensor, the SD card, the battery gauge and
// the radio are **not** here yet and the capabilities say so instead of pretending
// (docs/90). The radio is the same code the ATOM has, already shared; what is missing
// is handing it to this board and trying it on the hardware.
//
// No <Arduino.h> here: this file talks to the IDF directly, like the ATOM's HAL.
#include <driver/gpio.h>
#include <driver/spi_master.h>

#include "board/identity.h"
#include "esp32/caps.h"
#include "esp32/clock.h"
#include "esp32/gpio_keys.h"
#include "esp32/net.h"
#include "esp32/nvs.h"
#include "esp32/power.h"
#include "esp32/sd_card.h"
#include "esp32/storage.h"
#include "hal/hal.h"

namespace hal {
namespace {

// The pins this file touches, from the maker's own library (docs/50). The main power
// has to be held high within a second of the button being pressed, or the board cuts
// itself off, which looks exactly like a crash.
//
// Two more are switched and are not used yet, so they stay off: the supply of the
// panel (GPIO23) and the one of the three HY2.0 ports (GPIO5). They belong to the
// steps that use them (docs/90).
const int kMainPowerPin = 2;

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

// The three keys of the dial. They are inputs with no pull configured here: the board
// has the resistors and pressing a key closes it to ground (docs/50).
//
// The order is the one of the maker's own library (M5EPD.h), where the middle key
// —the one that is also the power button— is the main key, which is the one the
// system counts three taps on (docs/30). Its README says the opposite about left and
// right, so which of the two is the top of the dial is the one thing about this board
// that has to be checked by pressing it (docs/90).
const KeySpot kKeys[] = {
    {38, Face::kLeft, Spot::kCentre, KeyKind::kCompound, 0},  // centre (PWR)
    {37, Face::kLeft, Spot::kUp, KeyKind::kCompound, 0},
    {39, Face::kLeft, Spot::kDown, KeyKind::kCompound, 0},
};

// The label of the data partition in partitions_16mb.csv (docs/70).
const char kDataPartition[] = "littlefs";

// The e-ink panel shares SCK, MOSI and MISO with the card, and its **CS is GPIO15**: a
// strapping pin that this board pulls down at boot, which means the IT8951E comes up
// *selected* —listening to every byte the card exchanges and free to drive MISO when the
// card should be the only one—. Deselecting it before the bus is used is what makes the
// card reliable (measured on the board, 31/10: `mounts` read the FAT fine and then
// `ls /sd` failed with `sdmmc_send_cmd returned 0x108` and `Check status failed 0x107`,
// which is what a bus with two listeners looks like).
const int kPanelCsPin = 15;

void deselect_panel() {
  gpio_config_t out = {};
  out.pin_bit_mask = 1ULL << kPanelCsPin;
  out.mode = GPIO_MODE_OUTPUT;
  out.pull_up_en = GPIO_PULLUP_DISABLE;
  out.pull_down_en = GPIO_PULLDOWN_DISABLE;
  out.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&out);
  gpio_set_level(static_cast<gpio_num_t>(kPanelCsPin), 1);
}

// The card slot: CS on GPIO4, and SCK, MOSI and MISO are the panel's own wires, which is
// why the two devices share one bus and the panel will join this host (docs/50). 10 MHz
// and not the 20 MHz default: the wires are shared, nothing here is measured yet, and a
// card driven too fast is a card that mounts and then times out.
const SdPins kSdPins = {4, 14, 12, 13, SPI2_HOST, 10000};

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
      // The panel is on the same three wires as the card and comes up selected (see
      // above): out of the way before anything touches the bus (docs/50).
      deselect_panel();
      // Everything the chip gives —the radio with its provisioning and a page
      // included— and then only what this board has of its own (docs/50).
      caps_ = esp32_caps();
      // The one thing the chip cannot answer for: the filesystem is a partition of
      // this board's table, and it is advertised only when it really mounts.
      if (esp32_storage("/littlefs", kDataPartition) != nullptr) caps_.add(kCapLittlefs);
      // The card slot: advertised only when a card really mounts, the same rule as the
      // filesystem of the flash (docs/70).
      if (esp32_sd("/sd", kSdPins) != nullptr) caps_.add(kCapSd);
      // Three keys, read in every turn of the kernel like the ATOM's one. The panel
      // and the touch are still absent (docs/90).
      caps_.add(kCapInput);
    }
    return caps_;
  }

  // Zero until the ceiling of this board is measured, which makes the kernel use its
  // own default (docs/75).
  uint32_t heap_limit_kb() const override { return 0; }

  IClock* clock() override { return esp32_clock(); }
  // Two filesystems on this board: the flash and, when there is a card in the slot, the
  // card. Each one answers for its own mount and says no to the other (docs/70).
  IStorage* storage(const char* mount) override {
    // Whoever mounts first can come before `caps()`, and the panel has to be out of the
    // way before a single byte goes down those wires (see above).
    deselect_panel();
    IStorage* flash = esp32_storage(mount, kDataPartition);
    return (flash != nullptr) ? flash : esp32_sd(mount, kSdPins);
  }
  IKeyValue* nvs() override { return esp32_nvs(); }
  // The same radio the ATOM has, because it is the chip's and not the board's: what is
  // missing here is trying it on this hardware, not the code (docs/90).
  INet* net() override { return esp32_net(); }
  IPower* power() override { return &power_; }
  IDisplay* display() override { return nullptr; }
  ILed* led() override { return nullptr; }
  IInput* input() override { return &keys_; }
  IImu* imu() override { return nullptr; }

 private:
  mutable Caps caps_;
  mutable bool built_ = false;
  // No key of this board wakes the chip: they switch the main power off instead of
  // waking it, so the timer is the only way back (docs/50).
  Esp32Power power_{-1};
  // The dial is this board's; the reading of its three pins is not (docs/50).
  GpioKeys keys_{kKeys, sizeof(kKeys) / sizeof(kKeys[0])};
};

PaperPlatform g_platform;

}  // namespace

IPlatform& platform() { return g_platform; }

}  // namespace hal
