// Board: the M5 ATOM family (ESP32-PICO-D4, 4 MB flash, 320 KB RAM, no PSRAM, no
// screen): the Lite and the Matrix, which differ in the number of lights and in
// nothing this file decides. The identity header says which one is being built
// (docs/50). See docs/50-hal-i-plaques.md for the sheet and for what is
// deliberately absent.
//
// What belongs to the chip is not written here: the filesystem, the key-value store, the
// clock, the sleep and the radio are the ESP32's, and the **readers** for the things a
// board puts on a pin —keys, a strip of WS2812— are shared as well, all of it in
// lib/hal_esp32 (docs/50). What is left is what only this board can say: which identity
// it is, which pin carries the light and which the button, and the MPU6886 the Matrix
// has.
#include <cstdio>

// The IMU of the Matrix is on the board's own I2C bus, and the IDF driver is what
// reads it: two pins, one address and a burst of registers, no library needed
// (docs/50).
#include <driver/i2c.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "board/identity.h"
#include "esp32/caps.h"
#include "esp32/clock.h"
#include "esp32/gpio_keys.h"
#include "esp32/net.h"
#include "esp32/nvs.h"
#include "esp32/power.h"
#include "esp32/rtc_memory.h"
#include "esp32/storage.h"
#include "esp32/ws2812.h"
#include "hal/hal.h"

namespace hal {
namespace {

// The light is a strip of WS2812 chips on GPIO27: one on the Lite, twenty-five on the
// Matrix, and the identity header says how many and up to which channel (docs/50).
const uint8_t kLedPin = 27;
// The button is GPIO39: input only (34 to 39 have no pull-up inside the chip), the
// board carries the resistor and pressing takes it to ground.
const uint8_t kButtonPin = 39;

// One button and no more on this family: the Lite and the Matrix have the same one,
// and it is the main key by being the only one. It is on the top face, in the middle,
// and it is a key of its own (docs/50).
const KeySpot kKeys[] = {
    {kButtonPin, Face::kTop, Spot::kCentre, KeyKind::kSimple, 0},
};

// The label of the data partition in partitions.csv: the filesystem is found by it,
// never by the core's default (docs/70).
const char kDataPartition[] = "littlefs";

// The inertial sensor of the Matrix: a MPU6886 on the board's own I2C bus (SDA G25,
// SCL G21 at 100 kHz, address 0x68). The two ranges are set once and the numbers are
// the datasheet's: ±2 g is 16384 counts per g, ±250 °/s is 131 counts per degree per
// second, and the temperature is counts/326.8 + 25 (docs/50).
//
// Only a board that says it has one ever reaches this (board::has_imu()), and the
// bus is taken the first time somebody reads: the Lite pays nothing for any of it
// (docs/75).
const uint8_t kImuSda = 25;
const uint8_t kImuScl = 21;
const uint8_t kImuAddress = 0x68;
const uint32_t kImuClockHz = 100000;
const uint8_t kImuWhoAmI = 0x75;
const uint8_t kImuId = 0x19;  // what a MPU6886 answers there
const uint8_t kImuPwrMgmt1 = 0x6B;
const uint8_t kImuGyroConfig = 0x1B;
const uint8_t kImuAccelConfig = 0x1C;
const uint8_t kImuAccelXoutH = 0x3B;  // the first of fourteen bytes

class AtomImu : public IImu {
 public:
  bool read(float& ax, float& ay, float& az, float& gx, float& gy, float& gz,
            float& celsius) override {
    if (!ready_ && !begin()) return false;
    uint8_t raw[14];
    if (!read_registers(kImuAccelXoutH, raw, sizeof(raw))) return false;
    // The chip answers big-endian, high byte first, and the order is the datasheet's:
    // accel x, y, z, temperature, gyro x, y, z.
    const float per_g = 16384.0f;      // ±2 g
    const float per_dps = 131.0f;      // ±250 °/s
    ax = be16(raw + 0) / per_g;
    ay = be16(raw + 2) / per_g;
    az = be16(raw + 4) / per_g;
    celsius = be16(raw + 6) / 326.8f + 25.0f;
    gx = be16(raw + 8) / per_dps;
    gy = be16(raw + 10) / per_dps;
    gz = be16(raw + 12) / per_dps;
    return true;
  }

 private:
  static int16_t be16(const uint8_t* p) {
    return static_cast<int16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
  }

  bool write_register(uint8_t reg, uint8_t value) {
    const uint8_t buffer[2] = {reg, value};
    return i2c_master_write_to_device(I2C_NUM_0, kImuAddress, buffer, sizeof(buffer),
                                      pdMS_TO_TICKS(50)) == ESP_OK;
  }

  bool read_registers(uint8_t reg, uint8_t* out, size_t len) {
    return i2c_master_write_read_device(I2C_NUM_0, kImuAddress, &reg, 1, out, len,
                                        pdMS_TO_TICKS(50)) == ESP_OK;
  }

  // The bus and the chip, once. Every failure says so on the console: a board whose
  // sensor does not answer is a board to look at, not a mystery (docs/50).
  bool begin() {
    i2c_config_t config = {};
    config.mode = I2C_MODE_MASTER;
    config.sda_io_num = static_cast<gpio_num_t>(kImuSda);
    config.scl_io_num = static_cast<gpio_num_t>(kImuScl);
    config.sda_pullup_en = GPIO_PULLUP_ENABLE;
    config.scl_pullup_en = GPIO_PULLUP_ENABLE;
    config.master.clk_speed = kImuClockHz;
    if (i2c_param_config(I2C_NUM_0, &config) != ESP_OK ||
        i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0) != ESP_OK) {
      printf("imu: the I2C bus could not be taken; this board has no sensor\n");
      return false;
    }
    uint8_t id = 0;
    if (!read_registers(kImuWhoAmI, &id, 1) || id != kImuId) {
      printf("imu: no MPU6886 answered (who am i 0x%02x)\n", static_cast<unsigned>(id));
      return false;
    }
    // Out of reset, awake, with the gyro's oscillator as the clock; the ranges the
    // numbers above assume; and every axis on (the reset does that).
    if (!write_register(kImuPwrMgmt1, 0x80)) return false;
    vTaskDelay(pdMS_TO_TICKS(10));
    if (!write_register(kImuPwrMgmt1, 0x01) ||
        !write_register(kImuAccelConfig, 0x00) || !write_register(kImuGyroConfig, 0x00)) {
      printf("imu: the MPU6886 did not take its configuration\n");
      return false;
    }
    ready_ = true;
    return true;
  }

  bool ready_ = false;
};

class AtomPlatform : public IPlatform {
 public:
  const char* id() const override { return board::id(); }
  const char* family() const override { return board::family(); }
  bool emulator() const override { return board::emulator(); }

  // Built on first use, never in the constructor: mounting LittleFS before
  // Arduino's setup() runs is not allowed, and the constructor runs much earlier.
  const Caps& caps() const override {
    if (!built_) {
      built_ = true;
      // Everything the chip gives, and then only what this board has of its own
      // (docs/50).
      caps_ = esp32_caps();
      // The one thing the chip cannot answer for: the filesystem is a partition of
      // this board's table, and it is advertised only when it really mounts.
      if (esp32_storage("/littlefs", kDataPartition) != nullptr) caps_.add(kCapLittlefs);
      // One RGB light on GPIO27 and the button on GPIO39: the two things this board
      // does with a person (docs/50).
      caps_.add(kCapLed);
      caps_.add(kCapInput);
      // The Matrix carries the MPU6886; the Lite does not, and the identity says
      // which of the two this build is (docs/50).
      if (board::has_imu()) caps_.add(kCapImu);
    }
    return caps_;
  }
  // 0 until measured: docs/75 says a number that is not measured is not written.
  uint32_t heap_limit_kb() const override { return 0; }
  IClock* clock() override { return esp32_clock(); }
  IStorage* storage(const char* mount) override {
    // The RTC memory is a file of the chip and not of the board (docs/50, docs/70).
    if (mount != nullptr && strcmp(mount, "/rtc") == 0) return esp32_rtc_memory();
    return esp32_storage(mount, kDataPartition);
  }
  IKeyValue* nvs() override { return esp32_nvs(); }
  INet* net() override { return esp32_net(); }
  IPower* power() override { return &power_; }
  IDisplay* display() override { return nullptr; }
  ILed* led() override { return &led_; }
  IInput* input() override { return &input_; }
  IImu* imu() override { return board::has_imu() ? &imu_ : nullptr; }

 private:
  mutable Caps caps_;
  mutable bool built_ = false;
  // The sleep of the chip with this board's button as the way back (docs/50).
  Esp32Power power_{kButtonPin};
  // The strip this board carries, and the reading of the pin its button is on: the pin
  // and how many chips are the board's, the driver is not (docs/50).
  Ws2812Strip<kLedPin, board::led_count(), board::led_max()> led_;
  GpioKeys input_{kKeys, sizeof(kKeys) / sizeof(kKeys[0])};
  AtomImu imu_;
};

AtomPlatform g_platform;

}  // namespace

IPlatform& platform() { return g_platform; }

}  // namespace hal
