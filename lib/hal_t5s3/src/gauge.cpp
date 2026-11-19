// The gauge of the T5 E-Paper S3 Pro: a BQ27220 that counts and a BQ25896 that charges (docs/50).
#include "t5s3/gauge.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace hal {
namespace {

// **Les dues adreces i les tres ordres d'aquest xip** (BQ27220, SLUUBE7): `Voltage()` a 0x08 i
// `StateOfCharge()` a 0x2C, totes dues de dos bytes i el primer el més baix —little endian, com
// tota la família—, i un byte d'ordre que es queda al registre del xip entre lectures.
const uint8_t kGaugeAddress = 0x55;
const uint8_t kVoltage = 0x08;
const uint8_t kStateOfCharge = 0x2C;

// And the charger's one register: REG0B, where bits 4:3 are `CHRG_STAT` —00 not charging, 01
// pre-charge, 10 fast charge, 11 done— and bit 5 is `PG_STAT`, the power that is good (BQ25896,
// SLUSCQ1). What a person sees is the bolt: a cable that is feeding the cell.
const uint8_t kChargerAddress = 0x6B;
const uint8_t kChargerStatus = 0x0B;

// **El BQ27220 es desperta quan veu activitat al bus** (0.190.31, el log del 19/11): the gauge of this board answered
// `ESP_ERR_TIMEOUT` on a bus where the touch controller (0x5D) and the clock chip (0x51) answered at once, and the
// datasheet's own answer to that is that the chip leaves SHUTDOWN when something talks to it —the first transaction is
// the one it can lose—. So a read that fails is asked again a couple of times with a few milliseconds between, and that
// is not a blind loop: it is the wake the chip describes (docs/50).
const uint8_t kTries = 3;
const uint32_t kWaitMs = 3;

bool read_with_a_wake(II2c* bus, uint8_t command, uint8_t* out, size_t len) {
  for (uint8_t attempt = 0; attempt < kTries; ++attempt) {
    if (bus->read(kGaugeAddress, command, out, len)) return true;
    vTaskDelay(pdMS_TO_TICKS(kWaitMs));
  }
  return false;
}

uint16_t little_endian(const uint8_t* bytes, size_t len) {
  uint16_t value = 0;
  for (size_t i = 0; i < len && i < 2; ++i) value = static_cast<uint16_t>(value | (bytes[i] << (8 * i)));
  return value;
}

}  // namespace

bool T5Gauge::read(int& mv, int& pct, bool& charging) {
  if (bus_ == nullptr) return false;
  uint8_t raw[2] = {0, 0};
  if (!read_with_a_wake(bus_, kVoltage, raw, sizeof(raw))) return false;
  const uint16_t millivolts = little_endian(raw, sizeof(raw));
  // A number outside a single cell is not a measurement: under a volt is a gauge that is not there
  // and over five is a bus that answered something else (docs/50).
  if (millivolts < 1000 || millivolts > 5000) return false;
  if (!read_with_a_wake(bus_, kStateOfCharge, raw, sizeof(raw))) return false;
  const uint16_t percent = little_endian(raw, sizeof(raw));
  if (percent > 100) return false;
  mv = static_cast<int>(millivolts);
  pct = static_cast<int>(percent);
  // **I el cable**: a register that does not answer is «not charging» and not a failure —the cell's
  // two numbers are already a good answer, and a board on a cable that cannot be read is a board
  // whose bolt is missing, not a board without a battery (docs/50).
  charging = false;
  uint8_t status = 0;
  if (bus_->read(kChargerAddress, kChargerStatus, &status, 1)) {
    const uint8_t charge_state = static_cast<uint8_t>((status >> 3) & 0x03);
    charging = (charge_state == 1 || charge_state == 2);
  }
  return true;
}

}  // namespace hal
