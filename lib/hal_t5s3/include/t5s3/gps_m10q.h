// **El receptor del cel d'aquesta placa** (0.192.63).
//
// Pins del fabricant (`docs/pinmap.md` del repositori de la placa, §GPS, i el seu `docs/pin_define.md`):
//
//   ESP32 TX (GPIO43, `U0TXD`) -> GPS_RX · ESP32 RX (GPIO44, `U0RXD`) <- GPS_TX · rail `LORA_EN` a l'expansor PCA9535 `IO0_0`
//
// **I no hi ha cap línia d'interrupció**: el pinmap del fabricant només hi porta la UART, o sigui que el mòdul no avisa quan té una
// frase i algú l'ha de llegir (l'amo: *«veurás qué no tenim interrupcions per el GPS, així que haurem de llegir-lo manualment»*).
// Això és el que fa `poll()`: llegir el que hi hagi a la UART i tornar, sense esperar ningú (docs/50).
//
// **Dos mòduls possibles i el mateix peu** (docs/pinmap.md): la `MIA-M10Q` i la `L76K`, i l'únic que els distingeix és preguntar-ho,
// perquè parlen a bauds diferents —38 400 la u-blox i 9 600 la Quectel—. El driver prova la u-blox primer i, si no hi ha ni una
// frase, canvia de ritme: una placa muntada amb l'altre mòdul no es queda muda (docs/50).
//
// **El que aquest fitxer no fa**: no configura el receptor. Un M10Q surt de fàbrica amb les frases NMEA que aquest driver llegeix
// (RMC, GGA, GSA, GSV, VTG), o sigui que no cal cap missatge binari de configuració —UBX— i no se n'hi envia cap: escriure al mòdul
// amb una trama que no s'ha pogut provar a la placa és la mena de cosa que deixa un receptor en un estat que ningú sap desfer
// (docs/50, docs/95). **El que sí que fa el fabricant** (`examples/GPS/main/main.ino` del repositori de la placa) és parlar-hi: al
// L76K li envia `$PCAS03`/`$PCAS04`/`$PCAS11` i al M10Q trames UBX de recuperació. O sigui que la comunicació **és de dues
// direccions** i un receptor de fàbrica no la necessita (docs/50).
//
// **I el que entén les frases no és aquí**: `hal::nmea` les llegeix, i aquest fitxer només porta el cable (docs/50, docs/85).
#ifndef LUNOK_T5S3_GPS_M10Q_H
#define LUNOK_T5S3_GPS_M10Q_H

#include <stdint.h>

#include "hal/hal.h"
#include "hal/nmea.h"
#include "t5s3/expander.h"

namespace hal {

// **Els pins, dits pel fabricant** (docs/pinmap.md de la placa): les dues de la UART del mòdul, que són les de la UART0 del xip i que
// aquesta placa pot fer servir perquè la seva consola és el USB natiu i no la UART (docs/50).
inline constexpr int kGpsTxPin = 43;
inline constexpr int kGpsRxPin = 44;

class GpsM10Q : public IGps {
 public:
  // **El rail és de l'expansor i és compartit amb el LoRa** (docs/pinmap.md: `LORA_EN` encén els dos): el driver el rep en comptes de
  // tocar el pin, i **no l'apaga mai**, perquè apagar-lo callaria també la ràdio (docs/50).
  explicit GpsM10Q(Pca9535* rail);
  ~GpsM10Q() override;

  GpsCaps caps() const override;
  bool on() override;
  bool off() override;
  bool poll(GpsFix& out) override;
  const GpsSatellite* satellites(uint8_t& count) const override;
  const char* last_error() const override { return error_; }

 private:
  bool open_the_wire(uint32_t baud);
  void close_the_wire();
  // **Una frase sencera del cable, i qui la mira és `hal::nmea`** (0.192.90): the driver reads bytes and hands them over; the
  // sentences, the checksum and the fix are in a file that knows nothing about a UART and can be tested on the PC (docs/50, docs/85).
  bool hear_a_sentence(uint32_t ms);

  Pca9535* rail_;
  nmea::State nmea_;
  uint32_t baud_ = 0;
  bool open_ = false;
  const char* error_ = "";
};

// **El receptor d'aquesta placa, una vegada** (0.192.63): the rail is the same object that opens the panel, and a board without it has
// no receiver either. Nothing is opened here —asking a board what it has must not touch a pin—, o sigui que la UART i el sondeig
// arriben amb el primer `on()` (docs/50).
GpsM10Q* t5s3_gps(Pca9535* rail);

}  // namespace hal

#endif  // LUNOK_T5S3_GPS_M10Q_H
