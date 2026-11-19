// **L'expansor de ports d'aquesta placa** (docs/50): a PCA9535 (I2C 0x20) with sixteen pins, and it is not a
// convenience —the panel cannot be driven without it—. Its outputs are the panel's `EP_OE` and `EP_MODE`, the
// four gates of the TPS65185 (`PWRUP`, `WAKEUP`, `VCOM_CTRL`) and the rail of the LoRa/GPS module; its inputs are
// the TPS's `POWER_GOOD` and its interrupt, and the board's own function button.
//
// **The register map is the PCA9535's and not the MCP23017's** (0.190.33): the two chips look the same from the
// outside and are not —the MCP has direction registers at 0x00 and its inputs at 0x12, the PCA has the inputs at
// 0x00, the outputs at 0x02 and the configuration at 0x06—, and the vendor's own library talks to `0x20` as if it
// were an MCP. Writing the wrong map is a panel that never gets its `OE` and a `POWER_GOOD` that reads as a `1`
// for ever, o sigui que aquest fitxer diu el mapa que aquest xip té (docs/50, docs/95).
#ifndef LUNOK_T5S3_EXPANDER_H
#define LUNOK_T5S3_EXPANDER_H

#include <stdint.h>

#include "esp32/i2c.h"

namespace hal {

// The pins this board wires, by the number the vendor's pin map gives them: `IO1_0` is 8, `IO1_7` is 15.
enum class Expander : uint8_t {
  kLoraEnable = 0,   // IO0_0: the shared 3.3 V rail of the LoRa and the GPS
  kPanelOe = 8,      // IO1_0: the panel's output enable
  kPanelMode = 9,    // IO1_1: the panel's mode line (the vendor calls it GMOD)
  kButton = 10,      // IO1_2: the board's function button, an input
  kTpsPowerUp = 11,  // IO1_3: the TPS65185's power-up sequence
  kTpsVcomCtrl = 12, // IO1_4: the panel's VCOM
  kTpsWakeUp = 13,   // IO1_5: wakes the TPS65185
  kTpsPowerGood = 14,// IO1_6: an input, the supply is good
  kTpsInterrupt = 15 // IO1_7: an input
};

class Pca9535 {
 public:
  // `bus` may be nullptr: a board whose bus did not come up answers false everywhere instead of crashing (docs/50).
  explicit Pca9535(II2c* bus, uint8_t address = 0x20);

  // **The sixteen pins as outputs or inputs, once** (docs/50): the mask is the PCA's own configuration register
  // semantics —a `1` is an input— so the panel can say what it needs in one call.
  bool configure(uint16_t inputs_mask);

  // One pin, and the whole port is written: the chip has no per-pin write, so the driver keeps a copy of the
  // outputs and writes the two registers it changed (docs/50).
  bool write(Expander pin, bool level);
  bool read(Expander pin, bool& level);

  // Whether the chip is there at all: answered once, at the first configuration, and said by the log.
  bool present() const { return present_; }
  bool configured() const { return configured_; }

  // What the driver last failed at, in words, for the line of the caller (docs/50).
  const char* last_error() const { return error_; }

 private:
  bool write_outputs();
  II2c* bus_;
  uint8_t address_;
  uint16_t outputs_ = 0;
  uint16_t inputs_ = 0xFFFF;
  bool present_ = false;
  bool configured_ = false;
  const char* error_ = "";
};

}  // namespace hal

#endif  // LUNOK_T5S3_EXPANDER_H
