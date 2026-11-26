// **La ràdio de LoRa: escoltar, i només escoltar** (0.192.49).
//
// A LoRa interface is not a network: there is no address, no association and no acknowledgement, o sigui que el que
// una app hi pot fer és **mirar l'aire** —mesurar quanta energia hi ha en una freqüència i, quan n'hi ha, provar de
// treure'n un paquet—. Això és el que fa un monitor i és el que aquesta interfície sap fer; **no hi ha cap manera de
// transmetre-la**, i no n'hi ha perquè no cal: un aparell que només escolta es pot deixar engegat sense permís de ningú
// i no pot tapar la veu de ningú altre (docs/30, docs/50).
//
// **El xip és del HAL i la política és de l'app**: qui tria la freqüència, l'SF i el que es fa amb el que s'ha sentit és
// la pàgina; el HAL només diu com es posa el xip a escoltar i com se'n llegeix el que ha arribat (docs/50).
#ifndef LUNOK_HAL_LORA_H
#define LUNOK_HAL_LORA_H

#include <stdint.h>

namespace hal {

// **Una configuració de recepció**, i no de transmissió: la freqüència en hertz, el factor d'expansió (5 a 12), el codi
// d'amplada de banda (el número del xip: 7 és 125 kHz, 8 és 250 i 9 és 500), la taxa de codificació (1 a 4, o sigui
// 4/5 a 4/8), la paraula de sincronisme i el preàmbulo que s'espera. **Els números són els del xip** perquè una app que
// ho ensenya ha de poder dir el que ha demanat, i la taula de correspondències és a docs/50 (docs/30).
struct LoraConfig {
  uint32_t hz = 868100000;
  uint8_t sf = 9;
  uint8_t bw = 7;
  uint8_t cr = 1;
  // **`0x12` és la paraula privada i `0x34` la pública** (la de LoRaWAN): un sniffer no sap quina fa servir el que hi ha
  // a l'aire, i per això això és un camp i no una constant del driver (docs/50).
  uint8_t sync = 0x12;
  uint16_t preamble = 8;
  // **CRC a la cua**: un paquet amb el CRC malament és una senyal que hi és i que no s'ha pogut llegir, i això una
  // pàgina ho ha de poder dir en comptes de fer veure que no ha passat res (docs/30).
  bool crc = true;
  // El capgirament de l'IQ, que només fa servir la xarxa que parla LoRaWAN cap avall: el driver el deixa com l'app diu
  // perquè un monitor el pugui provar (docs/50).
  bool invert_iq = false;
};

// **El que arriba**: els bytes, i el que el xip ha mesurat del paquet. L'RSSI va en **dècimes de dB** i l'SNR en
// **quarts de dB**, que són les unitats que el xip dona i que no perden res; qui ho ensenya ho divideix (docs/30).
struct LoraPacket {
  uint8_t data[256];
  uint16_t length = 0;
  int16_t rssi_dbm10 = 0;
  int16_t snr_db4 = 0;
  int32_t freq_offset_hz = 0;
  uint32_t hz = 0;
  uint32_t ms = 0;
  bool crc_ok = true;
};

// **El que una app necessita saber abans de tocar res.** El rang del xip és el del fabricant i no el de la placa: el
// mòdul pot estar soldat per a 868 o per a 915, i qui decideix què és legal en cada país és la persona que la fa anar
// (docs/50).
struct LoraCaps {
  bool present = false;
  uint32_t min_hz = 150000000;
  uint32_t max_hz = 960000000;
  uint8_t min_sf = 5;
  uint8_t max_sf = 12;
  // **El nom del xip**, perquè la pàgina digui `SX1262` i no «la ràdio» (docs/50).
  const char* chip = "SX1262";
};

class ILora {
 public:
  virtual ~ILora() {}
  virtual LoraCaps caps() const = 0;
  // **Engege el xip**: el rail, el reset, l'estat de repòs i la calibració de la imatge. Es pot cridar dues vegades i la
  // segona no fa res, que és el que aquesta casa demana de tot el que toca un registre (docs/50).
  virtual bool begin() = 0;
  // **On s'escolta**, i la calibració només es repeteix quan es canvia de banda —el xip en té cinc de declarades— perquè
  // un escombrat de cinquanta canals no pot pagar una calibració per canal (docs/50).
  virtual bool set_frequency(uint32_t hz) = 0;
  virtual uint32_t frequency() const = 0;
  // La modulació amb què s'escolta; sense això el xip no rep res (docs/50).
  virtual bool set_reception(const LoraConfig& config) = 0;
  virtual LoraConfig reception() const = 0;
  // **Escoltar**: la ràdio es queda en recepció fins que arriba un paquet o passa el termini (`0` és per sempre), i qui
  // pregunta no es bloqueja: `poll()` és el que diu si n'hi ha un (docs/30, docs/50).
  virtual bool listen(uint32_t timeout_ms) = 0;
  // **Mirar quanta energia hi ha**, sense esperar cap paquet: recepció contínua i el nivell instantani del canal. És el
  // que fa un escombrat, i el que no es pot fer amb `listen` perquè un canal buit no dona cap paquet mai (docs/50).
  virtual bool sniff() = 0;
  // **El nivell del canal, en dècimes de dB** (`-1234` és −123,4 dBm). És una lectura i no una mitjana: qui vulgui una
  // mitjana la fa ell amb les mostres que vulgui (docs/30).
  virtual int32_t rssi_dbm10() = 0;
  // **Si hi ha un paquet a punt**: l'omple i el treu de la cua del xip. Fals vol dir «encara no», i no un error (docs/30).
  virtual bool poll(LoraPacket& packet) = 0;
  // Repòs i son: el xip deixa d'escoltar i el rail es pot quedar com és. **Un son del xip no és el son de la placa**:
  // aquesta és la ràdio que calla, no la que apaga el processador (docs/50).
  virtual bool idle() = 0;
  virtual bool sleep() = 0;
  // El que el driver va fallar, en paraules, per a la línia de registre del qui ho va demanar (docs/50).
  virtual const char* last_error() const = 0;
};

}  // namespace hal

#endif  // LUNOK_HAL_LORA_H
