// **L'aire de mentida** (0.192.49): el perquè és a la capçalera; aquí hi ha l'espectre i els paquets, que són dades d'aquest
// fitxer i de ningú més (docs/85).
#include "emu/lora.h"

#include <stdio.h>
#include <string.h>

#include "emu/model.h"
#include "emu/options.h"
#include "pal/pal.h"

namespace emu {
namespace {

// **Un node de l'aire inventat**: on és, amb quin factor d'expansió parla, cada quant dona senyal i què diu. Els tres són a la
// banda de 868 —la que el mòdul d'aquesta placa espera— i **cada un amb un SF diferent**, que és el que fa que un monitor els
// hagi de buscar un per un i el que dona sentit al recorregut de la pàgina (docs/50, docs/85).
struct Node {
  uint32_t hz;
  uint8_t sf;
  uint32_t period_ms;
  uint32_t offset_ms;
  int32_t power_dbm10;
  const char* words;
};

const Node kNodes[] = {
    // El terme de dalt, la potència del qual arriba forta: és el que un escombrat ha de veure primer. **Els períodes són
    // curts a posta**: un monitor que prova cada SF dos segons només atrapa un paquet si el node parla més sovint que la volta
    // sencera del recorregut, i un emulador que parla cada vint segons seria una prova de vint minuts (docs/85).
    {868100000, 9, 2000, 1200, -670, "n1 temp 21.4 hum 48"},
    // El del mig, fluix i amb un SF baix: sense SF7 no se sent.
    {868300000, 7, 2500, 5300, -1040, "n2 batt 87% rssi -104"},
    // El de baix, amb SF12, que és el més lent i el que arriba més lluny.
    {868500000, 12, 3000, 9100, -880, "n3 counter 4172"},
};

const size_t kNodeCount = sizeof(kNodes) / sizeof(kNodes[0]);

// **El polsador del garatge**: una portadora a 433,92 que no diu res —ni preàmbulo, ni capçalera, ni CRC—, o sigui que un
// monitor l'ha de veure com una barra i **no** com un paquet. És el que distingeix un detector d'energia d'un receptor, i el
// que fa que aquesta pàgina no sigui un llistat de números bonics (docs/50).
const uint32_t kBeaconHz = 433920000;
const int32_t kBeaconDbm10 = -720;

// **El terra de soroll**, en dècimes de dB, i el que es mou: un canal buit no és un número fix i una barra que no tremola és
// una barra que no s'ha mesurat (docs/85).
const int32_t kNoiseDbm10 = -1210;
const int32_t kNoiseSwing = 30;

// **L'amplada de banda del canal en hertz, pels codis del xip** (7 és 125 kHz, 8 és 250 i 9 és 500): és el que decideix si
// una portadora cau dins del que s'escolta (docs/50).
int32_t bandwidth_hz(uint8_t code) {
  switch (code) {
    case 0: return 7800;
    case 1: return 10400;
    case 2: return 15600;
    case 3: return 20800;
    case 4: return 31250;
    case 5: return 41700;
    case 6: return 62500;
    case 8: return 250000;
    case 9: return 500000;
    case 7:
    default: return 125000;
  }
}

// **Un número que tremola sense repetició**: un generador lineal sobre l'instant, que dona el mateix a la mateixa hora i no
// guarda cap estat —l'emulador no ha de tenir memòria que la placa no té (docs/85).
uint32_t wobble(uint32_t seed_ms, uint32_t hz, uint32_t salt) {
  uint32_t x = seed_ms ^ (hz * 2654435761u) ^ (salt * 40503u);
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return x;
}

uint32_t now_ms() { return static_cast<uint32_t>(pal::system().millis()); }

}  // namespace

hal::LoraCaps FakeLora::caps() const {
  hal::LoraCaps out;
  out.present = true;
  out.min_hz = 150000000;
  out.max_hz = 960000000;
  out.min_sf = 5;
  out.max_sf = 12;
  out.chip = "SX1262 (emulated air)";
  return out;
}

bool FakeLora::begin() {
  if (emu::options().delays) pal::delay_us(1500);
  return true;
}

bool FakeLora::set_frequency(uint32_t hz) {
  if (hz < 150000000 || hz > 960000000) {
    error_ = "a frequency outside the range of the chip";
    return false;
  }
  config_.hz = hz;
  return true;
}

bool FakeLora::set_reception(const hal::LoraConfig& config) {
  config_ = config;
  return true;
}

bool FakeLora::listen(uint32_t timeout_ms) {
  (void)timeout_ms;
  listening_ = true;
  sniffing_ = false;
  return true;
}

bool FakeLora::sniff() {
  listening_ = true;
  sniffing_ = true;
  return true;
}

// **El nivell del canal**: el terra de soroll, el que hi cau a prop i una mica de tremolor. **La potència de cada portadora
// és la del node des del lloc on s'escolta**, i la que cau fora de l'amplada de banda del receptor no compta —és el que fa
// que un escombrat amb BW de 125 kHz no vegi les tres alhora— (docs/50, docs/85).
int32_t FakeLora::rssi_dbm10() {
  const uint32_t at = now_ms();
  const int32_t half = bandwidth_hz(config_.bw) / 2;
  int32_t loudest = kNoiseDbm10 + static_cast<int32_t>(wobble(at, config_.hz, 1) % (2 * kNoiseSwing)) - kNoiseSwing;
  for (size_t i = 0; i < kNodeCount; ++i) {
    const int32_t distance = static_cast<int32_t>(kNodes[i].hz) - static_cast<int32_t>(config_.hz);
    if (distance < -half || distance > half) continue;
    if (kNodes[i].power_dbm10 > loudest) loudest = kNodes[i].power_dbm10;
  }
  // **L'interferent de 433,92**, que és el que un monitor ha de distingir d'una senyal: una portadora que hi és sempre i que no
  // diu res —ni preàmbulo, ni capçalera, ni CRC—. Abans s'encenia una estona de cada cinc i una passada d'escombrat el trobava
  // una vegada de cada cinc intents, que és una prova que falla sense dir per què; una portadora contínua és el que fa un
  // transformador dolent o un router a prop, i és el que aquest aire té (docs/50, docs/85).
  const int32_t beacon = static_cast<int32_t>(kBeaconHz) - static_cast<int32_t>(config_.hz);
  if (beacon >= -half && beacon <= half && kBeaconDbm10 > loudest) loudest = kBeaconDbm10;
  return loudest;
}

bool FakeLora::poll(hal::LoraPacket& packet) {
  if (!listening_ || sniffing_) return false;
  const uint32_t at = now_ms();
  const int32_t half = bandwidth_hz(config_.bw) / 2;
  for (size_t i = 0; i < kNodeCount; ++i) {
    const Node& node = kNodes[i];
    const int32_t distance = static_cast<int32_t>(node.hz) - static_cast<int32_t>(config_.hz);
    if (distance < -half || distance > half) continue;
    // **El node parla amb el seu SF i la seva paraula**: un receptor amb un altre SF no el sent, i això és LoRa i no un
    // detector d'energia (docs/50).
    if (node.sf != config_.sf) continue;
    if (config_.sync != 0x34) continue;
    // **El paquet surt al seu torn**, i el que arriba és el que ha sortit en aquesta finestra: la finestra és de vuit-cents
    // mil·lisegons perquè un monitor que dona dos segons a cada SF ha de poder atrapar el que hi ha, i cada paquet s'entrega
    // **una vegada** (docs/85).
    const uint32_t phase = (at + 60000u - node.offset_ms) % node.period_ms;
    const bool fresh = phase < 800u;
    if (!fresh) continue;
    const uint32_t serial = (at + 60000u - node.offset_ms) / node.period_ms;
    if (serial == last_serial_[i]) continue;
    last_serial_[i] = serial;
    // **El CRC d'un de cada vuit arriba malament**: un aire de debò té paquets que es perden, i una pàgina que no ho ensenya
    // no es pot provar amb una pèrdua (docs/85).
    const bool crc_ok = (serial % 8) != 3;
    snprintf(reinterpret_cast<char*>(packet.data), sizeof(packet.data), "%s #%u", node.words, static_cast<unsigned>(serial));
    packet.length = static_cast<uint16_t>(strlen(reinterpret_cast<const char*>(packet.data)));
    packet.rssi_dbm10 = node.power_dbm10 + static_cast<int16_t>(wobble(at, node.hz, 3) % 21) - 10;
    packet.snr_db4 = static_cast<int16_t>(static_cast<int32_t>((node.power_dbm10 + 1210) / 12) +
                                          static_cast<int32_t>(wobble(at, node.hz, 5) % 17) - 8);
    packet.freq_offset_hz = static_cast<int32_t>(wobble(at, node.hz, 9) % 2401) - 1200;
    packet.hz = config_.hz;
    packet.ms = at;
    packet.crc_ok = crc_ok;
    return true;
  }
  return false;
}

bool FakeLora::idle() {
  listening_ = false;
  sniffing_ = false;
  return true;
}

bool FakeLora::sleep() {
  listening_ = false;
  sniffing_ = false;
  return true;
}

FakeLora* emu_lora() {
  if (!emu::model().lora) return nullptr;
  static FakeLora the_one;
  return &the_one;
}

}  // namespace emu
