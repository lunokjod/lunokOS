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
#include <stdio.h>
#include <string.h>

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp32/spiram.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "board/identity.h"
#include "esp32/battery.h"
#include "esp32/bm8563.h"
#include "esp32/caps.h"
#include "esp32/clock.h"
#include "esp32/fm24c02.h"
#include "esp32/gpio_keys.h"
#include "esp32/i2c.h"
#include "esp32/net.h"
#include "esp32/nvs.h"
#include "esp32/power.h"
#include "esp32/rtc_clock.h"
#include "esp32/rtc_memory.h"
#include "esp32/sd_card.h"
#include "esp32/sht30.h"
#include "esp32/storage.h"
#include "hal/hal.h"

namespace hal {
namespace {

// The pins this file touches, from the maker's own library (docs/50). The main power
// has to be held high within a second of the button being pressed, or the board cuts
// itself off, which looks exactly like a crash.
//
// Two more are switched: the supply of the panel (GPIO23) and the one of the three HY2.0 ports
// (GPIO5). Nothing here turns them on —the panel is not driven yet and a port that is powered is
// current nobody asked for— but they are **turned off** when the board is (docs/50).
const int kMainPowerPin = 2;
const int kPanelPowerPin = 23;
const int kExtPowerPin = 5;

// A switched supply taken down: the pin becomes an output held low. It is set as an output first,
// because a pin left as an input would not switch anything off.
void turn_supply_off(int pin) {
  gpio_config_t out = {};
  out.pin_bit_mask = 1ULL << pin;
  out.mode = GPIO_MODE_OUTPUT;
  out.pull_up_en = GPIO_PULLUP_DISABLE;
  out.pull_down_en = GPIO_PULLDOWN_DISABLE;
  out.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&out);
  gpio_set_level(static_cast<gpio_num_t>(pin), 0);
}

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

// The battery comes to GPIO35 through a divider that halves it, so the pin never sees more
// than 2,1 V from a full cell (docs/50). The factor is what turns the pin back into the
// cell, and it is the one number here that a meter can prove wrong.
const BatteryPins kBatteryPins = {35, 2};

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

// The internal I2C of this board: the clock chip, the temperature sensor and the touch panel
// hang on these two wires, so a driver of any of them asks for the one bus (docs/50).
const int kI2cSdaPin = 21;
const int kI2cSclPin = 22;
// The BM8563, which is the board's real-time clock: the one with a coin cell behind it.
const uint8_t kBm8563Address = 0x51;

// The clock this board hands over: the ESP32 counts, the chip remembers. The chip is read
// once, here, on the first ask —which is during boot, before anything can look at the
// hour— and from then on it is the keeper (docs/60).
//
// The same chip is what the power of this board asks for, because its alarm is wired to the
// power circuit and not to a GPIO (docs/50): one object, two users.
Bm8563* paper_chip() {
  static Bm8563 chip(esp32_i2c(kI2cSdaPin, kI2cSclPin), kBm8563Address);
  return &chip;
}

IClock* paper_clock() {
  static RtcClock clock(paper_chip(), esp32_clock());
  static bool adopted = false;
  if (!adopted) {
    adopted = true;
    clock.adopt();
  }
  return &clock;
}

// How long a wait this board will hand to the clock chip: any. Its countdown reaches 255
// minutes and its clock alarm the next match of an hour and a minute, so a wait longer than a
// day is armed in stages —the board comes back early and arms the rest— instead of being left
// to the RC timer of the chip the way it would be on a board without a clock (docs/60).

// The energy of this board: the chip's sleeps and speed, which are Esp32Power's, plus the
// one thing the chip cannot know —how much battery is left—. The millivolts are a
// measurement and the percentage is a curve (hal/battery.h, docs/50).
//
// `charging` is false because **this board has no pin to ask**: it is the one field that
// says nothing, and docs/30 says so instead of letting an app read it as "on the battery".
class PaperPower : public Esp32Power {
 public:
  PaperPower() : Esp32Power(-1) {}

  bool battery(int& mv, int& pct, bool& charging) override {
    int read_mv = 0;
    if (!esp32_battery(kBatteryPins, read_mv)) return false;
    mv = read_mv;
    pct = static_cast<int>(battery_percent(read_mv));
    charging = false;
    return true;
  }

  // A deep sleep of this board is not a sleep: the board stops being powered. That is what
  // the coin cell behind the clock chip is for and what the main power hold is for —the
  // chip's alarm line reaches the power circuit, not a GPIO (docs/50)—, and it is the least
  // current this board can draw, which is what the energy rule asks for (README, law 9).
  //
  // The chip's hour is **aligned first**: the deadline being asked for is expressed in the
  // system's hour, and a chip counting from a stale one would keep the board off for the wrong
  // length of time. The kernel's own arming does the same (`RtcClock::arm_wake`); this is the
  // path that goes straight to the hardware and it does it too (docs/60).
  //
  // Three things are checked before the power is dropped, and each one of them is a board
  // that would not come back if it were skipped: that there is an hour to write (a board that
  // has never been set cannot arm anything, and a chip with a dead cell would never fire), that
  // the deadline is still to come, and that the chip **accepted** it —a wait past four hours goes
  // to the clock alarm, and one past a day is armed in stages by coming back early (docs/60)—.
  //
  // With the cable in the slot the board **cannot switch itself off** (the maker says so),
  // the power does not drop, and the call runs on into the chip's own deep sleep, which
  // wakes on its own timer as usual. Nothing is lost either way: the hour is in the chip.
  //
  // The main power hold is put **back up** when the board turns out to be still alive, and that is
  // not cosmetic: left down, the board would be hanging on the USB cable alone for the whole sleep,
  // and a cable that blinks is a board that comes back by itself and never reaches its hour
  // (measured on the glass, 31/10: a deep sleep of 300 s came back at 56 s with the wake cause
  // unset, which is a supply and not a clock).
  bool sleep(SleepMode mode, int64_t at_epoch_ms) override {
    if (mode == SleepMode::kDeep && at_epoch_ms > 0) {
      Bm8563* chip = paper_chip();
      const int64_t now_ms = esp32_wall_ms();
      if (now_ms > 0 && at_epoch_ms > now_ms && chip->write_epoch_ms(now_ms) &&
          chip->arm_at(at_epoch_ms)) {
        gpio_set_level(static_cast<gpio_num_t>(kMainPowerPin), 0);
        // Still here: the cable is in. Take the alarm back down —it would fire while the board is
        // awake and mean nothing— and hold the power up again, because this board stays on for the
        // sleep that follows and it must not depend on somebody's USB cable to keep doing it.
        gpio_set_level(static_cast<gpio_num_t>(kMainPowerPin), 1);
        chip->disarm();
      }
    }
    return Esp32Power::sleep(mode, at_epoch_ms);
  }

  bool deep_powers_off() const override { return true; }

  // Power off. It is the maker's own sequence, from the factory firmware of this board
  // (`M5Paper_FactoryTest`, `Shutdown()` in `global_setting.cpp`, docs/50): wait for what is pending
  // to finish, take the **panel supply** down, then the **ports supply**, then the **main power
  // hold**, and put the chip into deep sleep right after —because with the cable in the board does
  // not stop being powered, and the deep sleep is what stops it instead of leaving a board awake
  // with nothing to do.
  //
  // The clock chip is disarmed **before** all that, and that is the one thing added to the maker's
  // plain `M5EPD::shutdown()`, which does not look at the alarm: one armed by an earlier sleep would
  // fire on a board somebody asked to be off and the SY7088 would turn it back on. Off means off,
  // and the library's own timed `shutdown(seconds)` clears the alarm first as well.
  //
  // This does not return. The `false` is for the compiler and never reaches the glass.
  bool power_off() override {
    if (Bm8563* chip = paper_chip()) chip->disarm();
    // The maker waits 600 ms before the supplies go, so that whatever was still on its way out has
    // finished. This board has no panel painting to wait for, and the number is still theirs.
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(600));
    turn_supply_off(kPanelPowerPin);
    turn_supply_off(kExtPowerPin);
    gpio_set_level(static_cast<gpio_num_t>(kMainPowerPin), 0);
    // Nothing brings the board back but the button, or the cable being plugged again: a timer wake
    // armed by an earlier sleep would be a board that turns itself on.
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_deep_sleep_start();  // does not return
    return false;
  }

  // The chip's timer while the board is powered, and the clock chip's alarm when it is not:
  // the two ways this board comes back (docs/50).
  const char* wakes() override { return "timer, clock"; }
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
      // The battery, advertised only when the divider gives a number that could be a
      // cell: the same rule as the card and the disk (docs/50).
      int probe_mv = 0;
      if (esp32_battery(kBatteryPins, probe_mv)) caps_.add(kCapBattery);
      // The air: the SHT30 of the internal bus, advertised only when a whole measurement
      // arrived with its two CRCs good. A sensor that is promised and never answers is
      // worse than one that is not announced (docs/50).
      if (sensor() != nullptr) caps_.add(kCapEnv);
    }
    return caps_;
  }

  // The ceiling of an app on this board is its **PSRAM**: the interpreter puts its big blocks there
  // (one from 4 KB up, docs/75) while the small ones stay in the board's own RAM, and the system keeps
  // its own memory for itself. It is **4 096 KB and not the 8 MB the chip has**, because that is what
  // the ESP32 can map: its external RAM window is 4 MB, and the other half of the chip needs the
  // bank-switched `himem` that nothing here uses (measured on the glass, 31/10: `mem` said
  // `extra memory 4067 of 8192 KB free`, which is a 4 MB pool counted against an 8 MB chip).
  uint32_t heap_limit_kb() const override { return 4096; }

  // The extra memory of this board, with the three numbers (docs/75): what the chip says it has
  // (8 MB), what the ESP32 can address of it (4 MB, the size of its window for external RAM) and
  // what is left of that. A number an app can never use is not room, so `total` is the mapped one and
  // the other stays as the honest answer to "what does the datasheet say" (docs/50).
  ExtraMemory extra_memory() const override {
    ExtraMemory out;
    out.chip_bytes = static_cast<uint32_t>(esp_spiram_get_size());
    out.total_bytes = static_cast<uint32_t>(heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    out.free_bytes = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return out;
  }

  IClock* clock() override { return paper_clock(); }
  // Two filesystems on this board: the flash and, when there is a card in the slot, the
  // card. Each one answers for its own mount and says no to the other (docs/70).
  IStorage* storage(const char* mount) override {
    // The RTC memory is a file of the chip and not of the board: it is the same 8 KB on every
    // ESP32, and its content dies with the power (docs/50, docs/70).
    if (mount != nullptr && strcmp(mount, "/rtc") == 0) return esp32_rtc_memory();
    // The FRAM is this board's own memory: 256 bytes on the internal I2C that **do not need the
    // power**, so what an app writes there is still there after the battery has gone. It is one
    // file like `/rtc`, and a board whose chip does not answer has no mount at all (docs/50,
    // docs/70).
    if (mount != nullptr && strcmp(mount, "/fram") == 0) {
      return esp32_fram(esp32_i2c(kI2cSdaPin, kI2cSclPin));
    }
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
  // The air around the board, on the internal I2C it shares with the clock, the FRAM and the
  // touch panel. The chip is asked once whether it is there, because a probe is a measurement
  // (docs/50). `const` because the capabilities ask it too, and a capability is a question about a
  // board that is not changed by asking.
  ISensor* sensor() const override { return esp32_sht30(esp32_i2c(kI2cSdaPin, kI2cSclPin)); }

 private:
  mutable Caps caps_;
  mutable bool built_ = false;
  // No key of this board wakes the chip: they switch the main power off instead of
  // waking it, so the timer is the only way back (docs/50).
  PaperPower power_;
  // The dial is this board's; the reading of its three pins is not (docs/50).
  GpioKeys keys_{kKeys, sizeof(kKeys) / sizeof(kKeys[0])};
};

PaperPlatform g_platform;

}  // namespace

IPlatform& platform() { return g_platform; }

}  // namespace hal
