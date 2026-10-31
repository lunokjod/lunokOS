// The SHT30 driver: ask, check the two CRCs, and turn two counted numbers into a temperature and a
// humidity. Nothing is kept: the chip is the memory and it has none (docs/50).
#include "esp32/sht30.h"

namespace hal {
namespace {

// The datasheet's conversion, in counted units: the chip answers with a number from 0 to 65535 that
// is the measurement scaled to the whole range, and these are the two formulas with the constants
// the datasheet gives (docs/50).
float celsius_of(uint16_t raw) { return -45.0f + (175.0f * static_cast<float>(raw)) / 65535.0f; }

float humidity_of(uint16_t raw) { return 100.0f * static_cast<float>(raw) / 65535.0f; }

}  // namespace

uint8_t sht30_crc(uint8_t msb, uint8_t lsb) {
  uint8_t crc = 0xFF;
  const uint8_t data[2] = {msb, lsb};
  for (int i = 0; i < 2; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = ((crc & 0x80) != 0) ? static_cast<uint8_t>((crc << 1) ^ 0x31)
                                : static_cast<uint8_t>(crc << 1);
    }
  }
  return crc;
}

bool Sht30::read(float& celsius, float& humidity) {
  if (bus_ == nullptr) return false;
  const uint8_t command[2] = {kSht30CommandHigh, kSht30CommandLow};
  if (!bus_->write(kSht30Address, command, sizeof(command))) return false;
  // No byte in front of this read: the command has been given and the chip is holding the bus while
  // it measures (docs/50).
  uint8_t data[6] = {0};
  if (!bus_->read_only(kSht30Address, data, sizeof(data))) return false;
  // Two values, two CRCs. A crossed one is a reading that did not happen, and no number is better
  // than a wrong one (docs/30).
  if (sht30_crc(data[0], data[1]) != data[2]) return false;
  if (sht30_crc(data[3], data[4]) != data[5]) return false;
  celsius = celsius_of(static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) | data[1]));
  humidity = humidity_of(static_cast<uint16_t>((static_cast<uint16_t>(data[3]) << 8) | data[4]));
  return true;
}

bool Sht30::probe() {
  float celsius = 0;
  float humidity = 0;
  if (!read(celsius, humidity)) return false;
  // A measurement that arrives has to be believable: the chip's range is -40..125 °C and 0..100 %,
  // and a number outside it is a bus that answered something (docs/50).
  if (celsius < -40.0f || celsius > 125.0f) return false;
  if (humidity < 0.0f || humidity > 100.0f) return false;
  return true;
}

ISensor* esp32_sht30(II2c* bus) {
  if (bus == nullptr) return nullptr;
  static Sht30 chip(bus);
  // Asked **once**: `sensor()` is called from the capabilities and from every read, and a probe is a
  // measurement of six milliseconds (docs/50).
  static const bool there = chip.probe();
  return there ? &chip : nullptr;
}

}  // namespace hal
