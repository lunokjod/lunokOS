// **La bateria d'aquesta placa no és un divisor en un ADC** (docs/50): és un **BQ27220** (0x55) que
// compta els coulombs i ja sap el percentatge, i un **BQ25896** (0x6B) que carrega i diu si hi ha
// cable. Cap dels dos és el que el Paper porta, o sigui que aquest fitxer és el que la placa té i
// no el que el xip té.
//
// The board is the LilyGO T5 E-Paper S3 Pro and the two chips are on the shared I2C bus of
// GPIO39/GPIO40, the same two wires as the clock chip and the touch controller (docs/50).
#ifndef LUNOK_T5S3_GAUGE_H
#define LUNOK_T5S3_GAUGE_H

#include <stdint.h>

#include "esp32/i2c.h"

namespace hal {

class T5Gauge {
 public:
  // `bus` may be nullptr: that is a board whose bus did not come up, and every answer here is then
  // false instead of a crash (docs/50).
  explicit T5Gauge(II2c* bus) : bus_(bus) {}

  // Millivolts of the cell, the percentage the gauge has counted and whether a cable is charging it.
  // **False is «this board cannot say»**, which is the honest answer of a gauge that does not reply:
  // zero volts and zero per cent are a board that is off, not one that cannot tell (docs/30, docs/50).
  bool read(int& mv, int& pct, bool& charging);

  // **Tallar el corrent de debò** (0.190.33, el codi del fabricant: `LilyGoT5_Shutdown`): the only way to switch this
  // board off is the charger's `BATFET_DIS` bit —register 9, bit 0x20—, which opens the field-effect transistor between
  // the cell and the board. **It only works on the battery**: with the cable in, the board stays up and the bit does
  // nothing (the maker's own note), o sigui que una placa endollada que diu que s'apaga és una placa que no s'ha apagat
  // (docs/50).
  bool shut_down();

 private:
  II2c* bus_;
};

}  // namespace hal

#endif  // LUNOK_T5S3_GAUGE_H
