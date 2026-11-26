// **El GNSS de la placa: el que un receptor diu del món** (0.192.63, l'amo: *«Fes un app de GPS … Mostrar tota l'info que aquest
// MIA-M10Q GNSS ens pugui donar … Crea la part del sistema, les potes lua i finalment l'app.»*).
//
// Un receptor GNSS no és un sensor de dues xifres: és un aparell que **parla** —una llista de frases de text, NMEA 0183, per un
// cable sèrie— i que triga el que trigui el cel a donar-li quatre satèl·lits. Per això aquesta interfície té dues meitats: **què
// se sap ara** (`GpsFix`, que és el que una pàgina ensenya) i **com es pregunta** (`poll()`, que és el que algú ha de cridar cada
// estona, perquè aquesta placa **no té la línia d'interrupció del mòdul connectada** i no hi ha cap manera de saber que ha arribat
// una frase si no és llegint (docs/50)).
//
// **No hi ha res de la política aquí**: si el GPS s'engega de seguida o quan una pàgina ho demana, si l'hora del cel es creu o no,
// i quina zona horària en surt, és del nucli i de l'app (docs/30, docs/50).
#ifndef LUNOK_HAL_GPS_H
#define LUNOK_HAL_GPS_H

#include <stdint.h>

namespace hal {

// **La data i l'hora del cel, en UTC i amb la precisió que el receptor dona** (NMEA 0183 §RMC/§ZDA): els centèsims només hi
// són quan la frase els porta. `valid` vol dir que el receptor ha dit que la data i l'hora són de debò —una hora sense data
// (GGA sol) és l'hora d'un rellotge que no se sap si és certa— (docs/50).
struct GpsTime {
  bool valid = false;
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t second = 0;
  uint8_t hundredths = 0;
};

// **Un satèl·lit del cel, tal com el receptor el veu** (NMEA §GSV): el número que li toca, per on surt (elevació i azimut, en
// graus sencers) i amb quanta força arriba (SNR en dB, `0` vol dir que el receptor no l'ha mesurat). És el que es dibuixa en un
// mapa del cel, que és la manera que una persona entengui per què el receptor encara no té posició (docs/30).
struct GpsSatellite {
  uint8_t id = 0;        // el número del satèl·lit (el PRN)
  uint8_t elevation = 0; // 0–90 graus sobre l'horitzó
  uint16_t azimuth = 0;  // 0–359 graus des del nord
  uint8_t snr = 0;       // dB; 0 és «el receptor no l'ha mesurat»
  bool used = false;     // si aquest receptor el compta per a la posició
};

// **El que se sap del món en un moment** (docs/30): la posició i la qualitat, l'hora del cel, el moviment i l'edat del que s'ha
// llegit. Les coordenades van en **graus per deu milions** (un `int32_t`, que és el que dona el receptor sense perdre decimals) i
// les distàncies en centímetres, perquè una app no hagi de fer aritmètica de coma flotant per pintar un número (docs/50).
struct GpsFix {
  // **Si hi ha posició i de quina mena**: `quality` és la del receptor (0 cap, 1 GPS, 2 GPS diferencial, 4 RTK fix, 5 RTK flotant,
  // 6 estimada) i `valid` vol dir que la posició d'aquest moment serveix (docs/50).
  bool valid = false;
  uint8_t quality = 0;
  uint8_t satellites_used = 0;
  uint8_t satellites_seen = 0;
  GpsTime time;
  int32_t lat_e7 = 0;       // latitud, graus × 1e7 (negativa al sud)
  int32_t lon_e7 = 0;       // longitud, graus × 1e7 (negativa a l'oest)
  int32_t altitude_cm = 0;  // sobre el nivell del mar (GGA: l'altitud ortomètrica)
  int32_t geoid_cm = 0;     // la separació del geoide, que el receptor dona amb l'altitud
  int32_t speed_cm_s = 0;   // velocitat sobre el terra (RMC/VTG)
  int32_t course_deg10 = 0; // rumb en dècimes de grau
  uint16_t hdop_x10 = 0;    // la dilució de la precisió, en dècimes (9999 = no ho sap)
  uint16_t vdop_x10 = 0;
  uint16_t pdop_x10 = 0;
  // **I la salut del cable**, que és el que distingeix «no hi ha cel» de «el mòdul no contesta»: quantes frases s'han llegit,
  // quantes han arribat amb el segell malament i quantes de seguides sense posició (docs/30, docs/50).
  uint32_t sentences = 0;
  uint32_t bad_sentences = 0;
  uint32_t ms_since_a_fix = 0;
};

// **El que la placa té i el que el mòdul és.** El nom importa: la T5S3 en porta dos de possibles —el `MIA-M10Q` i el `L76K`— amb
// el mateix peu i bauds diferents, i l'únic que els distingeix és preguntar-ho (docs/pinmap.md de la placa, docs/50).
struct GpsCaps {
  bool present = false;
  // **El nom del mòdul, quan se sap**: `"MIA-M10Q"`, `"L76K"` o `"unknown"` mentres no hagi contestat res.
  const char* chip = "unknown";
  uint32_t baud = 0;     // a quants bauds s'ha acabat obrint
  bool powered = false;  // si el rail del mòdul és encès
};

class IGps {
 public:
  virtual ~IGps() {}
  virtual GpsCaps caps() const = 0;
  // **Engegar i apagar el mòdul** (0.192.63): el rail de 3,3 V que comparteix amb el LoRa i el cable sèrie. Es pot cridar dues
  // vegades i la segona no fa res; `off()` deixa el rail apagat, que és el que fa que el GPS no gasti res quan la placa va amb
  // bateria (docs/50).
  virtual bool on() = 0;
  virtual bool off() = 0;
  // **Llegir el que hi hagi i contestar el que se sap**: no espera res de ningú —llegeix el que la UART tingui i torna—, i és el
  // que cal cridar sovint perquè aquesta placa **no té la interrupció del mòdul** (docs/50).
  virtual bool poll(GpsFix& out) = 0;
  // **I el cel**: els satèl·lits de l'últim GSV. El punter és del driver i viu fins al `poll()` següent (docs/30).
  virtual const GpsSatellite* satellites(uint8_t& count) const = 0;
  virtual const char* last_error() const = 0;
};

}  // namespace hal

#endif  // LUNOK_HAL_GPS_H
