// **El cel inventat** (0.192.63): el perquè és a la capçalera; aquí hi ha el camí, el cel i l'hora, que són dades d'aquest fitxer
// i de ningú més (docs/85).
#include "emu/gps.h"

#include <stdio.h>
#include "emu/model.h"
#include "pal/pal.h"

namespace emu {
namespace {

// **El lloc on camina**: la plaça de Catalunya de Barcelona —41,387 °N i 2,170 °E—, que és un lloc que existeix i que queda a
// tocar de casa. No és una mesura de ningú: és un número d'aquest fitxer, com el nom dels nodes de l'aire inventat (docs/85).
const int32_t kStartLatE7 = 413870000;
const int32_t kStartLonE7 = 21700000;
// **Quan triga a tenir posició**: un M10Q fred en té en uns trenta segons i un d'escalfat en un o dos. La del model són **sis
// segons**, que és el mínim que fa que una prova vegi les dues cares —la pàgina amb el cel i encara sense posició, i la pàgina
// amb posició— sense esperar mitja hora (docs/85).
const uint32_t kWakeUpMs = 6000;
// **I com camina**: un metre i mig per segon, o sigui un passeig, i un rumb que gira a poc a poc. El que ha de poder veure una
// pàgina és que els números es mouen i que es mouen de manera coherent (docs/85).
const int32_t kStepCmPerSecond = 140;
// L'hora del cel: el model comença al migdia d'un dia que existeix i compta amb el rellotge de la placa.
const uint16_t kYear = 2026;
const uint8_t kMonth = 11;
const uint8_t kDay = 26;
const uint8_t kHour = 13;
const uint8_t kMinute = 30;

// Un cel de deu satèl·lits amb elevacions i azimuts repartits, que és el que un receptor veu quan té cel obert. L'SNR puja quan el
// satèl·lit és amunt i baixa quan és arran d'horitzó, que és el que fa que la llista tingui sentit i no sigui un número inventat
// per fila (docs/50, docs/85).
struct SkyEntry {
  uint8_t id;
  uint8_t elevation;
  uint16_t azimuth;
  uint8_t snr;
};

const SkyEntry kSky[] = {
    {2, 61, 118, 44}, {5, 43, 253, 39}, {12, 34, 76, 35}, {15, 27, 187, 31}, {19, 19, 311, 27},
    {23, 14, 44, 22},  {24, 9, 143, 17}, {25, 6, 226, 12}, {29, 4, 337, 9},  {31, 2, 95, 6},
};

}  // namespace

hal::GpsCaps FakeGps::caps() const {
  hal::GpsCaps out;
  out.present = powered_;
  // **El nom del mòdul de debò**, que és el que la placa en porta (docs/pinmap.md): una pàgina que digui el xip que és dona una
  // pista de per què parla a 38 400 bauds (docs/50).
  out.chip = powered_ ? "MIA-M10Q" : "unknown";
  out.baud = 38400;
  out.powered = powered_;
  return out;
}

bool FakeGps::on() {
  if (powered_) return true;
  powered_ = true;
  on_ms_ = pal::system().millis();
  spoken_ = 0;
  fix_ = hal::GpsFix();
  for (uint8_t i = 0; i < 12; ++i) sky_[i] = hal::GpsSatellite();
  sky_count_ = 0;
  error_ = "";
  return true;
}

bool FakeGps::off() {
  powered_ = false;
  spoken_ = 0;
  return true;
}

void FakeGps::wake_up_if_needed() {
  const uint32_t ms = pal::system().millis();
  if (spoken_ == 0) spoken_ = ms;
  const uint32_t awake = ms - on_ms_;
  // **El cel hi és des del primer moment** (un receptor fred ja veu satèl·lits abans de tenir posició), i la posició arriba quan
  // el model ha passat el seu temps d'encesa. Aquesta és la diferència que una pàgina ha de saber ensenyar (docs/30, docs/85).
  const uint8_t count = static_cast<uint8_t>(sizeof(kSky) / sizeof(kSky[0]));
  for (uint8_t i = 0; i < count; ++i) {
    sky_[i].id = kSky[i].id;
    sky_[i].elevation = kSky[i].elevation;
    sky_[i].azimuth = kSky[i].azimuth;
    // **El primer segon el receptor encara no mesura**: un SNR de zero és el que dona un mòdul acabat d'encendre (docs/50).
    sky_[i].snr = (awake > 1000) ? kSky[i].snr : 0;
    // **I els que compta per a la posició** són els que tenen cel: quan el receptor ha acabat de buscar, tot el que és per damunt
    // de deu graus hi entra. Això es recalcula a cada volta perquè un receptor triga a triar-los (docs/50, docs/85).
    sky_[i].used = (awake > kWakeUpMs) && (kSky[i].elevation > 10);
  }
  sky_count_ = count;
}

void FakeGps::walk_to(uint32_t ms) {
  // **El camí**: un metre i mig per segon cap al nord-est, que és el que fa que la latitud i la longitud es moguin les dues i que
  // la velocitat i el rumb tinguin un número cert. Els centímetres es mouen amb el temps encès i no amb el rellotge, o sigui que
  // dues voltes del mateix run donen el mateix camí (docs/85).
  const uint32_t awake = (ms > on_ms_) ? (ms - on_ms_) : 0;
  const int32_t done_cm = static_cast<int32_t>(awake) * kStepCmPerSecond / 1000;
  // Un grau de latitud són 111 320 m i un de longitud, a aquesta latitud, uns 83 600 m: el que cal per convertir els centímetres
  // del camí en dècimes de grau (docs/50).
  const int32_t lat_e7 = kStartLatE7 + static_cast<int32_t>(static_cast<int64_t>(done_cm) * 10000 / 11132000);
  const int32_t lon_e7 = kStartLonE7 + static_cast<int32_t>(static_cast<int64_t>(done_cm) * 10000 / 8360000);
  fix_.lat_e7 = lat_e7;
  fix_.lon_e7 = lon_e7;
  // L'altitud de la plaça, que és el que dona un mapa: 12 m sobre el nivell del mar, i el geoide d'aquesta zona, 51 m.
  fix_.altitude_cm = 1200;
  fix_.geoid_cm = 5100;
  fix_.speed_cm_s = kStepCmPerSecond;
  // El rumb del camí: 45 graus, que és cap al nord-est.
  fix_.course_deg10 = 450;
  fix_.hdop_x10 = 9;
  fix_.vdop_x10 = 12;
  fix_.pdop_x10 = 15;
  fix_.quality = 1;  // GPS
  fix_.satellites_seen = sky_count_;
  uint8_t used = 0;
  for (uint8_t i = 0; i < sky_count_; ++i) {
    if (sky_[i].used) ++used;
  }
  fix_.satellites_used = used;
  fix_.valid = (awake > kWakeUpMs) && (used >= 4);
  if (fix_.valid) fix_.ms_since_a_fix = 0;
  // **L'hora del cel**: la del model, que compta amb el rellotge de la placa i va en UTC. El receptor la dona amb centèsims.
  fix_.time.valid = fix_.valid;
  fix_.time.year = kYear;
  fix_.time.month = kMonth;
  fix_.time.day = kDay;
  fix_.time.hour = kHour;
  fix_.time.minute = kMinute;
  fix_.time.second = static_cast<uint8_t>((awake / 1000) % 60);
  fix_.time.hundredths = static_cast<uint8_t>((awake / 10) % 100);
}

bool FakeGps::poll(hal::GpsFix& out) {
  if (!powered_) {
    error_ = "the receiver is off";
    return false;
  }
  wake_up_if_needed();
  const uint32_t ms = pal::system().millis();
  // **Una frase i prou a cada volta**: el que fa el driver de debò és llegir el que la UART tingui; aquí el que es compta són les
  // preguntes, perquè una pàgina que crida dotze vegades per segon no es cregui que el cable parla doze vegades (docs/50, docs/85).
  ++fix_.sentences;
  ++spoken_;
  walk_to(ms);
  out = fix_;
  return true;
}

const hal::GpsSatellite* FakeGps::satellites(uint8_t& count) const {
  count = sky_count_;
  return (sky_count_ > 0) ? sky_ : nullptr;
}

FakeGps* emu_gps() {
  if (!emu::model().gps) return nullptr;
  static FakeGps the_one;
  return &the_one;
}

}  // namespace emu
