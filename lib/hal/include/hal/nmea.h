// **El que un receptor GNSS diu pel cable, llegit i entès** (0.192.90).
//
// Un receptor GNSS no és un registre: és un aparell que **parla** —una llista de frases de text, NMEA 0183, per una UART— i que
// vomita el mateix cicle cada segon sense que ningú li pregunti res. La part que llegeix el cable és del driver de cada placa
// (`lib/hal_t5s3/src/gps_m10q.cpp`), i **el que entén les frases és això**, que no sap res de cap UART i es pot provar al PC amb
// frases de debò (docs/50, docs/85).
//
// Les frases que llegeix són les cinc que un M10Q dona de fàbrica (§RMC, §GGA, §GSA, §GSV, §VTG de NMEA 0183 v4), amb el **segell
// XOR comprovat**: una frase que no quadra no es llegeix, perquè un caràcter perdut en un cable llarg dona un número que sembla una
// latitud (docs/50).
#ifndef LUNOK_HAL_NMEA_H
#define LUNOK_HAL_NMEA_H

#include <stddef.h>
#include <stdint.h>

#include "hal/gps.h"

namespace hal {
namespace nmea {

// **El que es va aprenent del cable**: el que se sap del món (`GpsFix`), el cel de satèl·lits, la frase que s'està llegint i els
// comptadors de salut. És tot el que cal i no hi ha res d'aquesta placa a dins (docs/50).
struct State {
  GpsFix fix;
  // **El cel de l'últim GSV**, per satèl·lit: el número, on és i amb quanta força. Un receptor en pot veure fins a trenta-dos.
  GpsSatellite sky[32];
  uint8_t sky_count = 0;
  // **Els que aquesta posició fa servir, segons el GSA**: el GSA i el GSV són frases diferents i arriben en moments diferents del
  // cicle, o sigui que la marca es guarda per número i el GSV la torna a posar (docs/50).
  uint8_t used_ids[32];
  uint8_t used_count = 0;
  // **La frase que s'està llegint**, que ha de cabre la més llarga de les NMEA (82 caràcters per l'estàndard i les de u-blox en són
  // 80; cent dotze és el marge d'una línia que arriba amb escombraries (docs/50).
  char line[112];
  size_t used = 0;
  uint8_t filling = 0;  // quin punt del cel s'està omplint dins del GSV
  // **La salut del cable**: quantes frases han arribat amb `$`, quantes han passat el segell i quantes no (docs/30, docs/50).
  uint32_t sentences = 0;
  uint32_t good = 0;
  uint32_t bad = 0;
};

// **Un estat que no sap res**, per a un cable que s'acaba d'obrir o que s'ha tornat a obrir a un altre ritme (docs/50).
void reset(State& state);

// **Un byte del cable**: torna `true` quan la línia s'ha acabat i s'ha mirat (bé o malament), que és el moment en què el que se sap
// pot haver canviat. Un salt de línia o un retorn de carro tanquen la frase (docs/50).
bool feed(State& state, char byte);

// **Si el cable ha dit alguna cosa que quadri**: una frase amb el segell bé. És el senyal de «aquest és el ritme bo» quan el driver
// prova dos bauds —un ritme equivocat dona bytes, però no en dona cap de sencer (docs/50).
bool heard_a_sentence(const State& state);

// **Una frase sencera, mirada**: la meitat de `feed` que el driver pot provar sense un cable. Es fa servir a les proves i a qui
// llegeixi una frase d'una altra manera (docs/85).
bool take(State& state, const char* sentence, size_t length);

}  // namespace nmea
}  // namespace hal

#endif  // LUNOK_HAL_NMEA_H
