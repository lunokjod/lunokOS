// The sixteen pins of this board, on a PCA9535 (docs/50).
#include "t5s3/expander.h"

namespace hal {
namespace {

// **The four registers of a PCA9535 and nothing else** (PCA9535 datasheet, SCPS174): the inputs are read-only and
// come first, the outputs are what a write changes, the polarity is left alone, and the configuration says which
// pin is an input —a `1`— because this chip is wired to ground through a switch or pulled up by the board.
const uint8_t kInputPort0 = 0x00;
const uint8_t kOutputPort0 = 0x02;
const uint8_t kConfigPort0 = 0x06;

uint8_t low_byte(uint16_t value) { return static_cast<uint8_t>(value & 0xFF); }
uint8_t high_byte(uint16_t value) { return static_cast<uint8_t>((value >> 8) & 0xFF); }

}  // namespace

Pca9535::Pca9535(II2c* bus, uint8_t address) : bus_(bus), address_(address) {}

bool Pca9535::configure(uint16_t inputs_mask) {
  if (bus_ == nullptr) {
    error_ = "there is no bus";
    return false;
  }
  inputs_ = inputs_mask;
  present_ = bus_->present(address_);
  if (!present_) {
    error_ = "the expander does not answer";
    return false;
  }
  // **The outputs first and then the configuration** (docs/50): a pin that becomes an output holding a stale `1`
  // would raise a gate of the supply for a moment, o sigui que el que s'escriu abans és el nivell de repòs.
  if (!write_outputs()) return false;
  const uint8_t config[3] = {kConfigPort0, low_byte(inputs_mask), high_byte(inputs_mask)};
  if (!bus_->write(address_, config, sizeof(config))) {
    error_ = "the configuration did not go in";
    return false;
  }
  // **I la lectura que diu que s'ha quedat** (0.190.34): a chip that answers and keeps its old configuration is a panel
  // whose gates are still inputs, and that failure is invisible from the outside —there is no error anywhere, only a
  // supply that never wakes—, o sigui que es llegeix de tornada i es compara (docs/50).
  uint8_t back[2] = {0, 0};
  if (!bus_->read(address_, kConfigPort0, back, sizeof(back))) {
    error_ = "the configuration did not come back";
    return false;
  }
  if (back[0] != low_byte(inputs_mask) || back[1] != high_byte(inputs_mask)) {
    error_ = "the configuration did not stay";
    return false;
  }
  configured_ = true;
  return true;
}

bool Pca9535::write_outputs() {
  if (bus_ == nullptr) return false;
  const uint8_t ports[3] = {kOutputPort0, low_byte(outputs_), high_byte(outputs_)};
  if (!bus_->write(address_, ports, sizeof(ports))) {
    error_ = "the outputs did not go out";
    return false;
  }
  return true;
}

bool Pca9535::write(Expander pin, bool level) {
  if (!configured_) {
    error_ = "the expander is not configured";
    return false;
  }
  const uint16_t bit = static_cast<uint16_t>(1u << static_cast<uint8_t>(pin));
  const uint16_t before = outputs_;
  outputs_ = level ? static_cast<uint16_t>(outputs_ | bit) : static_cast<uint16_t>(outputs_ & ~bit);
  if (outputs_ == before) return true;
  return write_outputs();
}

bool Pca9535::read(Expander pin, bool& level) {
  level = false;
  if (bus_ == nullptr) {
    error_ = "there is no bus";
    return false;
  }
  uint8_t ports[2] = {0, 0};
  if (!bus_->read(address_, kInputPort0, ports, sizeof(ports))) {
    error_ = "the inputs did not come back";
    return false;
  }
  const uint16_t value = static_cast<uint16_t>(ports[0] | (ports[1] << 8));
  level = (value & (1u << static_cast<uint8_t>(pin))) != 0;
  return true;
}

}  // namespace hal
