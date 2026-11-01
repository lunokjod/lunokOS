// One I2C bus of the ESP32, on the legacy driver of this IDF (4.4): it is the one that has
// `i2c_master_write_read_device`, which is exactly the frame every device of this family
// wants —the register address, then the bytes— in a single transaction (docs/50).
#include "esp32/i2c.h"

#include <cstdio>

#include <driver/i2c.h>

namespace hal {
namespace {

// The board's internal bus is short and slow, and 100 kHz is what every chip on it asks
// for. A transaction that takes longer than this is a chip that is not there, and the
// answer is a value and not a hang (docs/50).
const uint32_t kTimeoutMs = 50;

// **How long a chip may hold the clock low**, which is not the same thing as the line above. The
// driver of this IDF (v4.4.6, `i2c_ll_cal_bus_clk`) sets the hardware ceiling to *ten bus cycles*:
// at 100 kHz that is 8 000 APB cycles, **100 µs**, and a chip that stretches longer is reported as
// ESP_ERR_TIMEOUT and its bus is reset. The SHT30 holds the clock for up to **six milliseconds**
// while it measures, measured on the Paper as `i2c: read of 44 failed (ESP_ERR_TIMEOUT)` with the
// sensor not announced at all, so the ceiling goes to the register's 20-bit maximum: 1 048 575 APB
// cycles, **13.1 ms at 80 MHz**, twice the longest measurement the sensor asks for (docs/50).
const int kStretchCycles = 0xFFFFF;

class Esp32I2c : public II2c {
 public:
  Esp32I2c(i2c_port_t port, int sda_pin, int scl_pin) : port_(port) {
    i2c_config_t config = {};
    config.mode = I2C_MODE_MASTER;
    config.sda_io_num = sda_pin;
    config.scl_io_num = scl_pin;
    // The board has the resistors and the pull-up here is the belt: a bus that reads zero
    // because nobody holds the line high is a bug that looks like a dead chip.
    config.sda_pullup_en = GPIO_PULLUP_ENABLE;
    config.scl_pullup_en = GPIO_PULLUP_ENABLE;
    config.master.clk_speed = 100000;
    config.clk_flags = 0;
    ready_ = (i2c_param_config(port_, &config) == ESP_OK) &&
             (i2c_driver_install(port_, I2C_MODE_MASTER, 0, 0, 0) == ESP_OK);
    // After the driver, never before: installing the driver and setting the pins is what puts the
    // ten-cycle ceiling there, and this line is the one that lifts it (docs/50). Said out loud if it
    // does not take, because the next symptom —a sensor that is there and never answers— reads as a
    // dead chip and this line is the reason.
    if (ready_ && i2c_set_timeout(port_, kStretchCycles) != ESP_OK) {
      printf("i2c: the bus on %d/%d kept its default clock limit\n", sda_pin, scl_pin);
    }
    if (!ready_) printf("i2c: the bus on %d/%d did not come up\n", sda_pin, scl_pin);
  }

  bool ready() const { return ready_; }

  bool write(uint8_t address, const uint8_t* data, size_t len) override {
    if (!ready_ || data == nullptr || len == 0) return false;
    const esp_err_t err = i2c_master_write_to_device(
        port_, address, data, len, pdMS_TO_TICKS(kTimeoutMs));
    if (err != ESP_OK) {
      printf("i2c: write to %02x failed (%s)\n", address, esp_err_to_name(err));
      return false;
    }
    return true;
  }

  bool read(uint8_t address, uint8_t first_register, uint8_t* out, size_t len) override {
    if (!ready_ || out == nullptr || len == 0) return false;
    const esp_err_t err = i2c_master_write_read_device(port_, address, &first_register, 1, out,
                                                       len, pdMS_TO_TICKS(kTimeoutMs));
    if (err != ESP_OK) {
      printf("i2c: read of %02x failed (%s)\n", address, esp_err_to_name(err));
      return false;
    }
    return true;
  }

  // The same read without the byte in front of it, for the chips that speak in commands. The
  // timeout is the same fifty milliseconds and it is also the wait: a chip that measures while
  // holding the clock low holds this call —up to the 13 ms the bus allows it— and a chip that has
  // nothing to say lets it fail (docs/50).
  bool read_only(uint8_t address, uint8_t* out, size_t len) override {
    if (!ready_ || out == nullptr || len == 0) return false;
    const esp_err_t err = i2c_master_read_from_device(port_, address, out, len,
                                                      pdMS_TO_TICKS(kTimeoutMs));
    if (err != ESP_OK) {
      printf("i2c: read of %02x failed (%s)\n", address, esp_err_to_name(err));
      return false;
    }
    return true;
  }

  // The same read with a two-byte register address, which is how the touch controller of the Paper
  // asks to be spoken to: the two bytes and the data are one transaction (docs/50).
  bool read16(uint8_t address, uint16_t reg, uint8_t* out, size_t len) override {
    if (!ready_ || out == nullptr || len == 0) return false;
    const uint8_t reg_bytes[2] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg & 0xFF)};
    const esp_err_t err = i2c_master_write_read_device(port_, address, reg_bytes, sizeof(reg_bytes),
                                                       out, len, pdMS_TO_TICKS(kTimeoutMs));
    if (err != ESP_OK) {
      printf("i2c: read of %02x:%04x failed (%s)\n", address, reg, esp_err_to_name(err));
      return false;
    }
    return true;
  }

  // A single byte read and no word about it: an address with nobody on it answers with a NACK, and
  // that NACK **is** the answer. It is how the touch controller is looked for without writing an
  // error to the boot log for the address it is not at (measured on the board, 1/11).
  bool present(uint8_t address) override {
    if (!ready_) return false;
    uint8_t ignored = 0;
    return i2c_master_read_from_device(port_, address, &ignored, 1, pdMS_TO_TICKS(kTimeoutMs)) ==
           ESP_OK;
  }

 private:
  i2c_port_t port_;
  bool ready_ = false;
};

// The bus of this board, made on first use and kept for as long as the board is on.
struct Bus {
  bool used = false;
  int sda = -1;
  int scl = -1;
  Esp32I2c* instance = nullptr;
};

Bus g_bus;

}  // namespace

II2c* esp32_i2c(int sda_pin, int scl_pin) {
  if (g_bus.used) {
    if (g_bus.sda != sda_pin || g_bus.scl != scl_pin) {
      // Said out loud: a second bus is a board this driver does not have, and a silent
      // return of the first one would put a device's bytes on the wrong wires.
      printf("i2c: the bus is already on %d/%d\n", g_bus.sda, g_bus.scl);
      return nullptr;
    }
    return g_bus.instance;
  }
  g_bus.used = true;
  g_bus.sda = sda_pin;
  g_bus.scl = scl_pin;
  static Esp32I2c bus(I2C_NUM_0, sda_pin, scl_pin);
  g_bus.instance = &bus;
  return bus.ready() ? &bus : nullptr;
}

}  // namespace hal
