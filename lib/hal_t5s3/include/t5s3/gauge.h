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
#include "hal/hal.h"

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

  // **El que aquest indicador sap de més** (0.190.155): the BQ27220 counts coulombs, so it knows the current that is going in or
  // out of the cell and the capacity it has left. The maker's own example prints all three (`getCurrent`, `getRemainingCapacity`,
  // `getFullChargeCapacity`), o sigui que el que falta no és el xip sinó preguntar-l'hi. Cada camp que no passi el seu rang queda a
  // `kUnknown` en lloc de fer-se passar per una mesura (docs/50, docs/95).
  bool detail(IPower::BatteryDetail& out);

  // **L'altre xip de la bateria** (0.190.156): the BQ25896 charges and is the one that knows whether a cable is in, what kind it is,
  // how much of it is going into the cell and what the system rail is doing. El mapa és el del driver del fabricant
  // (`PowersBQ25896.tpp`: REG0B per l'estat, REG0F el VSYS, REG11 el VBUS i REG12 el corrent), no una deducció (docs/50, docs/95).
  bool charger(IPower::ChargerDetail& out);

  // **Tallar el corrent de debò** (0.190.33, el codi del fabricant: `LilyGoT5_Shutdown`): the only way to switch this
  // board off is the charger's `BATFET_DIS` bit —register 9, bit 0x20—, which opens the field-effect transistor between
  // the cell and the board. **It only works on the battery**: with the cable in, the board stays up and the bit does
  // nothing (the maker's own note), o sigui que una placa endollada que diu que s'apaga és una placa que no s'ha apagat
  // (docs/50).
  bool shut_down();

  // **El carregador, posat com el fabricant el posa** (0.192.13, l'amo: *«La batería no carrrga mai al 100%, perqué?»*): the chip charges with
  // whatever it brought from the factory until somebody configures it, i el fabricant sí que el configura al seu firmware de fàbrica
  // (`examples/factory/main/main.cpp`, `bq25896_init`): entrada a 3250 mA (el seu comentari diu que el valor de sèrie és **500**), el pin `ILIM`
  // desactivat (si no, el corrent de càrrega segueix el pin i no el registre), 4208 mV de tensió objectiu, 1024 mA de càrrega ràpida, 64 mA de
  // pre-càrrega, el rail del sistema a 3300 mV, l'ADC engegat (sense ell no hi ha mesures de VSYS/VBUS/IBAT) i el watchdog desactivat. **Això és
  // el que fa que una cel·la arribi al 100 %**: la fase de tensió constant ha d'acabar amb la caiguda de corrent que el comptador de coulombs
  // necessita per declarar-la plena (docs/50, docs/95).
  bool set_the_charger_as_the_maker_does();

  // **I el model de la bateria, que és l'altra meitat del mateix problema** (0.192.14): el `StateOfCharge()` del BQ27220 surt d'un model CEDV
  // que viu al seu *data memory*, i aquesta placa va sortir amb un model de **3000 mAh** en una cel·la de **1500** — o sigui que el número que
  // ensenya la barra és la meitat del que és (l'amo: *«the cell says 0 mA, 1136 of 3000 mAh of 3000»* sobre una cel·la a 4158 mV). Aquest mètode
  // escriu el perfil del firmware de fàbrica del fabricant (`bq27220_data_memory.c`), amb la seva seqüència: número de xip, obrir el pany,
  // comparar, i **només si no hi quadra** reiniciar, accés complet, mode de configuració, escriure, sortir reiniciant i tancar el pany
  // (docs/50, docs/95).
  bool write_the_maker_profile();

  // **Si hi ha un cable a dins** (0.190.272): REG11 bit 7 is the charger's own `VBUS_GD`, i és la pregunta que el fabricant fa
  // abans d'apagar res —el seu exemple de `shutdown` ho diu amb totes les paraules: *«The shutdown function can only be used
  // when the battery is connected alone, and cannot be shut down when connected to USB»*—. **False també vol dir que no s'ha
  // pogut preguntar**, o sigui que una placa que no contesta no es declara endollada (docs/50, docs/95).
  bool on_a_cable() const;

 private:
  II2c* bus_;
};

}  // namespace hal

#endif  // LUNOK_T5S3_GAUGE_H
