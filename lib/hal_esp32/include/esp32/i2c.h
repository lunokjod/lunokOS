// The I2C of the chip: one bus, two pins, and the two transactions the devices of this
// family speak.
//
// It is an interface because a device on the bus is worth testing without the bus: the
// driver of a clock chip takes one of these, and a test hands it a fake that answers the
// frames (docs/50).
#ifndef LUNOK_ESP32_I2C_H
#define LUNOK_ESP32_I2C_H

#include <stddef.h>
#include <stdint.h>

namespace hal {

class II2c {
 public:
  virtual ~II2c() {}
  // Writes `len` bytes at the device's own pointer (a register address and its values go
  // in the same buffer). False on a timeout, or when nobody answers at that address.
  virtual bool write(uint8_t address, const uint8_t* data, size_t len) = 0;
  // Writes one register address and reads `len` bytes back, as one transaction: another
  // master cannot slip in between and a device that keeps a pointer needs it that way.
  virtual bool read(uint8_t address, uint8_t first_register, uint8_t* out, size_t len) = 0;
  // Reads `len` bytes and writes **nothing** first: a device that was given its command and is
  // holding the bus while it measures does not want a byte of address, and sending one would be a
  // different command (docs/50). It is a default so an old bus does not have to grow one.
  virtual bool read_only(uint8_t address, uint8_t* out, size_t len) {
    (void)address;
    (void)out;
    (void)len;
    return false;
  }
};

// The bus of this chip on two pins, at 100 kHz. The same pair of pins gives the same bus,
// because a bus is one controller and not one per device (docs/50: the Paper has the clock,
// the temperature sensor and the touch on the same two wires).
//
// nullptr when the controller does not come up, or when a different pair was asked for
// first: this board has one internal bus, and pretending to have two would be a driver that
// works until somebody adds a second device.
II2c* esp32_i2c(int sda_pin, int scl_pin);

}  // namespace hal

#endif  // LUNOK_ESP32_I2C_H
