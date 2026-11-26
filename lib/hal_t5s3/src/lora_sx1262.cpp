// **El SX1262 d'aquesta placa per SPI, i només escoltar** (0.192.49). El perquè i els pins són a la capçalera; aquí hi ha
// el fil: les comandes del fabricant (SX1261/2 datasheet, rev 1.2) i les lectures que fan falta per mesurar un canal i per
// treure'n un paquet (docs/50).
#include "t5s3/lora_sx1262.h"

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_timer.h>
#include <string.h>

#include <new>

#include "hal/log.h"
#include "pal/pal.h"

namespace hal {
namespace {

// **Els opcodes del xip** (SX126x datasheet §11): els mateixos números que el fabricant documenta, que és l'única cosa
// que es pot escriure aquí sense inventar-se res (docs/50).
const uint8_t kSetSleep = 0x84;
const uint8_t kSetStandby = 0x80;
const uint8_t kSetRx = 0x82;
const uint8_t kSetPacketType = 0x8A;
const uint8_t kSetRfFrequency = 0x86;
const uint8_t kSetModulationParams = 0x8B;
const uint8_t kSetPacketParams = 0x8C;
const uint8_t kSetDioIrqParams = 0x08;
const uint8_t kSetBufferBaseAddress = 0x8F;
const uint8_t kSetDio2AsRfSwitch = 0x9D;
const uint8_t kCalibrateImage = 0x98;
const uint8_t kGetIrqStatus = 0x12;
const uint8_t kClearIrqStatus = 0x02;
const uint8_t kGetRxBufferStatus = 0x13;
const uint8_t kGetPacketStatus = 0x14;
const uint8_t kGetRssiInst = 0x15;
const uint8_t kReadBuffer = 0x1E;
const uint8_t kWriteRegister = 0x0D;

// **Els bits de la interrupció** (SX126x datasheet §11.4): només els que aquest driver mira. La resta s'esborren sense
// llegir-los, perquè qui no fa servir el temps límit ni el CAD no n'ha de fer res (docs/50).
const uint16_t kIrqRxDone = 0x0002;
const uint16_t kIrqCrcErr = 0x0040;
const uint16_t kIrqTimeout = 0x0200;

// **On s'escriu la paraula de sincronisme** (SX126x datasheet §13.1.12.4): dos registres, la paraula i la seva còpia de
// recepció, i el valor no és el número que l'app dona sinó el parell que el xip espera —`0x12` és `0x1424` i `0x34` és
// `0x3444`, que és el que el fabricant anomena privat i públic (docs/50).
const uint16_t kSyncWordRegister = 0x0740;

// **El rellotge del bus**: el xip accepta fins a setze megahertz i la placa comparteix el cable amb la targeta, o sigui que
// vuit és el que dona marge amb el cable curt del mòdul i no fa esperar una escriptura de 256 bytes (docs/50).
const int kSpiHz = 8000000;

// **El temps que es deixa reposar el xip abans de llegir el nivell del canal**: `GetRssiInst` contesta el que l'AGC acaba
// de mesurar, i tot just començada la recepció encara és la mesura del canal anterior. Tres mil·lisegons és el que costa
// un canal d'un escombrat i el que fa que la barra es mogui quan hi ha alguna cosa (mesurat a l'aire, docs/95).
const uint32_t kSettleMs = 3;

uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

}  // namespace

Sx1262::Sx1262(Pca9535* rail) : rail_(rail) {}

Sx1262::~Sx1262() {
  if (device_ != nullptr) spi_bus_remove_device(static_cast<spi_device_handle_t>(device_));
}

LoraCaps Sx1262::caps() const {
  LoraCaps out;
  out.present = began_;
  // **El rang del xip, no el del mòdul** (SX1262 datasheet): 150 a 960 MHz, i és el que el fabricant dona. Un mòdul soldat
  // per a 868 no hauria de rebre a 433, i això ho diu la persona que el fa anar, no el driver (docs/50).
  out.min_hz = 150000000;
  out.max_hz = 960000000;
  out.min_sf = 5;
  out.max_sf = 12;
  out.chip = "SX1262";
  return out;
}

void Sx1262::fail(const char* text) { error_ = (text != nullptr) ? text : ""; }

void Sx1262::say(const char* text) { logf(pal::kLogInfo, "LORA", "%s", text); }

// **La línia BUSY**: alta mentre el xip pensa. És una entrada de debò i no una espera fixa, perquè el que triga una
// comanda depèn del que se li ha demanat (un reset, una calibració o res) (docs/50).
bool Sx1262::wait_busy(uint32_t timeout_ms) {
  const uint32_t from = now_ms();
  while (gpio_get_level(static_cast<gpio_num_t>(kLoraBusyPin)) != 0) {
    if (now_ms() - from > timeout_ms) return false;
  }
  return true;
}

bool Sx1262::add_the_device() {
  if (device_ != nullptr) return true;
  // **El CS alt abans de res**: el mòdul no ha de contestar mentre la targeta parla, i aquesta és la lliçó que aquesta placa
  // ja va pagar una vegada amb un `sdmmc_io_reset` (docs/50, docs/70).
  gpio_config_t io = {};
  io.pin_bit_mask = (1ULL << kLoraCsPin) | (1ULL << kLoraResetPin);
  io.mode = GPIO_MODE_OUTPUT;
  io.pull_up_en = GPIO_PULLUP_DISABLE;
  io.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io.intr_type = GPIO_INTR_DISABLE;
  if (gpio_config(&io) != ESP_OK) {
    fail("the pins of the radio did not come up");
    return false;
  }
  gpio_set_level(static_cast<gpio_num_t>(kLoraCsPin), 1);
  gpio_set_level(static_cast<gpio_num_t>(kLoraResetPin), 0);
  // La línia d'ocupat, com a entrada.
  gpio_config_t busy = {};
  busy.pin_bit_mask = 1ULL << kLoraBusyPin;
  busy.mode = GPIO_MODE_INPUT;
  busy.pull_up_en = GPIO_PULLUP_ENABLE;
  busy.pull_down_en = GPIO_PULLDOWN_DISABLE;
  busy.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&busy);

  // **El bus que la targeta ja té**: si ja és a dalt, això no és cap error i el que es fa és afegir-hi un dispositiu, que és
  // exactament el que fa la ranura amb una segona targeta al mateix bus (docs/50).
  spi_bus_config_t bus = {};
  bus.mosi_io_num = 13;
  bus.miso_io_num = 21;
  bus.sclk_io_num = 14;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = 260;
  if (!bus_ready_) {
    const esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      logf(pal::kLogWarn, "LORA", "the SPI bus of the card did not come up (%s)", esp_err_to_name(err));
      fail("the SPI bus did not come up");
      return false;
    }
    bus_ready_ = true;
  }
  spi_device_interface_config_t dev = {};
  dev.clock_speed_hz = kSpiHz;
  dev.mode = 0;
  dev.spics_io_num = kLoraCsPin;
  dev.queue_size = 1;
  spi_device_handle_t handle = nullptr;
  const esp_err_t err = spi_bus_add_device(SPI2_HOST, &dev, &handle);
  if (err != ESP_OK) {
    logf(pal::kLogWarn, "LORA", "the radio did not get a place on the bus (%s)", esp_err_to_name(err));
    fail("the radio did not get a place on the bus");
    return false;
  }
  device_ = handle;
  return true;
}

// **Una transacció i prou**: el primer byte és l'opcode, els següents el que la comanda porta, i el que el xip contesta
// arriba al mateix buffer de sortida (el `full duplex` de l'SPI). Un `Get` és la mateixa crida amb el buffer buit i es
// llegeix el que hi ha darrere l'opcode (docs/50).
bool Sx1262::command(uint8_t opcode, const uint8_t* params, size_t count) {
  uint8_t frame[260];
  if (count > sizeof(frame) - 1) {
    fail("a command that does not fit");
    return false;
  }
  if (!wait_busy(100)) {
    fail("the radio is still busy");
    return false;
  }
  frame[0] = opcode;
  if (count > 0 && params != nullptr) memcpy(frame + 1, params, count);
  spi_transaction_t t = {};
  t.length = (1 + count) * 8;
  t.tx_buffer = frame;
  const esp_err_t err = spi_device_transmit(static_cast<spi_device_handle_t>(device_), &t);
  if (err != ESP_OK) {
    fail("the radio did not take the command");
    return false;
  }
  return true;
}

bool Sx1262::query(uint8_t opcode, uint8_t* out, size_t count) {
  uint8_t frame[260];
  if (count + 2 > sizeof(frame)) {
    fail("an answer that does not fit");
    return false;
  }
  if (!wait_busy(100)) {
    fail("the radio is still busy");
    return false;
  }
  memset(frame, 0, count + 2);
  frame[0] = opcode;
  spi_transaction_t t = {};
  t.length = (count + 2) * 8;
  t.tx_buffer = frame;
  t.rx_buffer = frame;
  const esp_err_t err = spi_device_transmit(static_cast<spi_device_handle_t>(device_), &t);
  if (err != ESP_OK) {
    fail("the radio did not answer");
    return false;
  }
  // **El primer byte de tornada és l'estat del xip** i la resposta comença al segon (SX126x datasheet §11): un `GetStatus`
  // i un `GetIrqStatus` comparteixen la mateixa forma, i comptar-ho malament desplaça totes les lectures un byte.
  memcpy(out, frame + 1, count);
  return true;
}

bool Sx1262::write_register(uint16_t address, const uint8_t* bytes, size_t count) {
  uint8_t params[16];
  if (count + 2 > sizeof(params)) {
    fail("a register value that does not fit");
    return false;
  }
  params[0] = static_cast<uint8_t>(address >> 8);
  params[1] = static_cast<uint8_t>(address & 0xFF);
  memcpy(params + 2, bytes, count);
  return command(kWriteRegister, params, count + 2);
}

bool Sx1262::reset_the_chip() {
  gpio_set_level(static_cast<gpio_num_t>(kLoraResetPin), 0);
  // **Cent microsegons de baixada és el que el full del fabricant demana com a mínim** (SX126x datasheet §9.3), i el marge
  // és el que fa que un xip fred arrenqui igual que un de calent (docs/50).
  pal::delay_us(200);
  gpio_set_level(static_cast<gpio_num_t>(kLoraResetPin), 1);
  // **I el xip triga a dir que hi és**: BUSY puja en sortir del reset i baixa quan ha acabat la seva càrrega interna. El
  // termini és deu mil·lisegons perquè el full en dona 3,5 com a màxim i una placa freda pot trigar més (docs/50).
  pal::delay_us(2000);
  if (!wait_busy(50)) {
    fail("the radio did not come out of reset");
    return false;
  }
  return true;
}

bool Sx1262::standby() {
  const uint8_t mode = 0x00;  // STDBY_RC
  if (!command(kSetStandby, &mode, 1)) return false;
  return wait_busy(50);
}

bool Sx1262::calibrate_the_image(uint32_t hz) {
  // **Les cinc bandes del fabricant** (SX126x datasheet §9.5): cada parell és el que el xip vol per calibrar la imatge
  // d'aquella banda, i una freqüència que no cau en cap s'aproxima a la més propera, que és el que fa el fabricant mateix
  // al seu codi (docs/50).
  uint8_t band[2];
  if (hz < 446000000) {
    band[0] = 0x6B;
    band[1] = 0x6F;
  } else if (hz < 734000000) {
    band[0] = 0x75;
    band[1] = 0x81;
  } else if (hz < 828000000) {
    band[0] = 0xC1;
    band[1] = 0xC5;
  } else if (hz < 877000000) {
    band[0] = 0xD7;
    band[1] = 0xDB;
  } else {
    band[0] = 0xE1;
    band[1] = 0xE9;
  }
  if (!command(kCalibrateImage, band, 2)) return false;
  return wait_busy(50);
}

bool Sx1262::begin() {
  if (began_) return true;
  if (rail_ == nullptr) {
    fail("this board has no rail for the radio");
    return false;
  }
  // **El rail del LoRa i del GPS** (el fabricant: `io_extend_lora_gps_power_on`): sense això el xip no té alimentació i el
  // que contesta pel MISO és el que hi hagi al cable. El fabricant espera un segon i mig; aquí no es pot bloquejar tant i el
  // que es fa és donar-li el marge de la línia BUSY, que és la que diu quan el xip hi és de debò (docs/50).
  if (!rail_->write(Expander::kLoraEnable, true)) {
    logf(pal::kLogWarn, "LORA", "the rail of the radio did not come up: %s", rail_->last_error());
    fail("the rail of the radio did not come up");
    return false;
  }
  if (!add_the_device()) return false;
  if (!reset_the_chip()) return false;
  if (!standby()) return false;
  // **El DIO2 com el commutador de la ràdio**: la majoria de mòduls SX1262 porten un commutador d'antena governat per aquesta
  // línia, i posar-lo així és el que fa la llibreria del fabricant. En un mòdul que no la connecta no passa res (docs/50).
  const uint8_t on = 0x01;
  if (!command(kSetDio2AsRfSwitch, &on, 1)) return false;
  // **Escoltar i prou**: el tipus de paquet és LoRa, i no s'hi posa cap `SetTx` enlloc d'aquest fitxer (docs/30).
  const uint8_t lora_packet = 0x01;
  if (!command(kSetPacketType, &lora_packet, 1)) return false;
  // El buffer de recepció comença a zero, que és el que el xip espera del seu propi exemple.
  const uint8_t base[2] = {0x00, 0x00};
  if (!command(kSetBufferBaseAddress, base, 2)) return false;
  // **La interrupció, només la de rebre i la del CRC**: les dues es llegeixen i la resta no es demana (docs/50).
  const uint16_t mask = kIrqRxDone | kIrqCrcErr | kIrqTimeout;
  uint8_t irq[8] = {static_cast<uint8_t>(mask >> 8), static_cast<uint8_t>(mask & 0xFF),
                    static_cast<uint8_t>(mask >> 8), static_cast<uint8_t>(mask & 0xFF), 0, 0, 0, 0};
  if (!command(kSetDioIrqParams, irq, 8)) return false;
  if (!calibrate_the_image(config_.hz)) return false;
  if (!set_reception(config_)) return false;
  began_ = true;
  logf(pal::kLogInfo, "LORA", "the SX1262 listens: %u Hz, SF%u, BW %u, CR 4/%u",
       static_cast<unsigned>(config_.hz), static_cast<unsigned>(config_.sf), static_cast<unsigned>(config_.bw),
       static_cast<unsigned>(config_.cr + 4));
  return true;
}

bool Sx1262::set_frequency(uint32_t hz) {
  if (device_ == nullptr) {
    fail("the radio is not there");
    return false;
  }
  if (hz < 150000000 || hz > 960000000) {
    fail("a frequency outside the range of the chip");
    return false;
  }
  // **La calibració només quan canvia de banda**: un escombrat de cinquanta canals dins dels 868 no pot pagar cinquanta
  // calibracions, i el fabricant només en demana una per banda (docs/50).
  const bool crossed = (hz < 446000000) != (config_.hz < 446000000) ||
                       (hz < 734000000) != (config_.hz < 734000000) ||
                       (hz < 828000000) != (config_.hz < 828000000) ||
                       (hz < 877000000) != (config_.hz < 877000000);
  if (!standby()) return false;
  if (crossed && !calibrate_the_image(hz)) return false;
  // **Xtal 32 MHz i 2^25 passos** (SX126x datasheet §9.4): el número que s'escriu és `hz * 2^25 / 32e6`, amb aritmètica de
  // seixanta-quatre bits perquè el producte no cap en un enter de trenta-dos (docs/50).
  const uint64_t steps = (static_cast<uint64_t>(hz) << 25) / 32000000ULL;
  const uint8_t freq[4] = {static_cast<uint8_t>((steps >> 24) & 0xFF), static_cast<uint8_t>((steps >> 16) & 0xFF),
                           static_cast<uint8_t>((steps >> 8) & 0xFF), static_cast<uint8_t>(steps & 0xFF)};
  if (!command(kSetRfFrequency, freq, 4)) return false;
  config_.hz = hz;
  return true;
}

bool Sx1262::set_reception(const LoraConfig& config) {
  if (device_ == nullptr) {
    fail("the radio is not there");
    return false;
  }
  config_ = config;
  // **La paraula de sincronisme, al registre que el fabricant diu**: `0x12` és la privada i la seva parella escrita és
  // `0x1424`; la pública (`0x34`) és `0x3444`. Un valor que no és cap de les dues s'escriu tal com ve, perquè un monitor
  // ha de poder provar el que vulgui (docs/50).
  uint8_t pair[2] = {0x14, 0x24};
  if (config.sync == 0x34) {
    pair[0] = 0x34;
    pair[1] = 0x44;
  } else if (config.sync != 0x12) {
    pair[0] = static_cast<uint8_t>(config.sync << 4);
    pair[1] = 0x44;
  }
  if (!write_register(kSyncWordRegister, pair, 2)) return false;
  // **La modulació**: SF, amplada de banda, codificació i l'optimització de velocitat baixa, que ha de ser certa per sota de
  // 125 kHz amb SF11 i SF12 i el xip no la calcula tot sol (SX126x datasheet §9.6).
  const uint8_t low_rate = (config.sf >= 11 && config.bw == 7) ? 0x01 : 0x00;
  const uint8_t modulation[4] = {config.sf, config.bw, config.cr, low_rate};
  if (!command(kSetModulationParams, modulation, 4)) return false;
  // **El paquet, tal com s'espera rebre'l**: el preàmbulo, la capçalera explícita (la que diu la llargada a dins), cap
  // llargada prefixada, el CRC segons l'app i el capgirament de l'IQ (SX126x datasheet §9.7).
  const uint8_t packet[6] = {static_cast<uint8_t>(config.preamble >> 8), static_cast<uint8_t>(config.preamble & 0xFF),
                             0x00,
                             0xFF,  // s'espera el màxim i la llargada de debò la diu el paquet
                             static_cast<uint8_t>(config.crc ? 0x00 : 0x01),
                             static_cast<uint8_t>(config.invert_iq ? 0x01 : 0x00)};
  if (!command(kSetPacketParams, packet, 6)) return false;
  return true;
}

bool Sx1262::listen(uint32_t timeout_ms) {
  if (!began_) {
    fail("the radio is not up");
    return false;
  }
  if (!wait_busy(100)) {
    fail("the radio is still busy");
    return false;
  }
  // **El xip rep quan algú l'hi diu**: `SetRx` amb un temps límit de tres bytes, i el màxim (`0xFFFFFF`) vol dir «fins que
  // arribi alguna cosa» (SX126x datasheet §11). El termini de l'app no és el del xip: qui mira és `poll()`, i així una
  // pàgina no es queda esperant dins d'una crida del sistema (docs/30).
  uint32_t ticks = 0xFFFFFF;
  if (timeout_ms > 0) {
    ticks = static_cast<uint32_t>((static_cast<uint64_t>(timeout_ms) << 6) / 1000ULL);  // 15,625 µs per tick
    if (ticks == 0) ticks = 1;
    if (ticks > 0xFFFFFE) ticks = 0xFFFFFE;
  }
  const uint8_t timeout[3] = {static_cast<uint8_t>((ticks >> 16) & 0xFF), static_cast<uint8_t>((ticks >> 8) & 0xFF),
                              static_cast<uint8_t>(ticks & 0xFF)};
  return command(kSetRx, timeout, 3);
}

bool Sx1262::sniff() {
  // Un escombrat escolta sense esperar cap paquet: la mateixa recepció contínua, i el que es llegeix després és el nivell.
  return listen(0);
}

int32_t Sx1262::rssi_dbm10() {
  if (device_ == nullptr) {
    fail("the radio is not there");
    return 0;
  }
  // **La mesura del canal, que el xip dona en mitges dècimes**: el byte és `-2 x dBm`, o sigui que mig dB és un pas i el
  // que es torna és `-byte * 5` dècimes. El marge de repòs és el que fa que el número sigui del canal d'ara i no del
  // d'abans (docs/50, docs/95).
  pal::delay_us(kSettleMs * 1000);
  uint8_t raw = 0;
  if (!query(kGetRssiInst, &raw, 1)) return 0;
  return -static_cast<int32_t>(raw) * 5;
}

bool Sx1262::poll(LoraPacket& packet) {
  if (device_ == nullptr) return false;
  uint8_t irq[2] = {0, 0};
  if (!query(kGetIrqStatus, irq, 2)) return false;
  const uint16_t flags = static_cast<uint16_t>((irq[0] << 8) | irq[1]);
  if ((flags & (kIrqRxDone | kIrqTimeout)) == 0) return false;
  // **La interrupció es neteja abans de llegir el paquet**: si s'esborra després, el que arribi entremig es perd; i si no
  // s'esborra, el xip no torna a aixecar la línia (docs/50).
  const uint8_t clear[2] = {static_cast<uint8_t>((kIrqRxDone | kIrqCrcErr | kIrqTimeout) >> 8),
                            static_cast<uint8_t>((kIrqRxDone | kIrqCrcErr | kIrqTimeout) & 0xFF)};
  if (!command(kClearIrqStatus, clear, 2)) return false;
  if ((flags & kIrqRxDone) == 0) {
    // Un temps límit que ha vençut no és cap paquet: la recepció s'ha de tornar a armar i el qui pregunta no ha de rebre
    // un paquet buit que sembli una senyal (docs/30).
    listen(0);
    return false;
  }
  uint8_t status[2] = {0, 0};
  if (!query(kGetRxBufferStatus, status, 2)) return false;
  const uint16_t length = status[0];
  const uint8_t start = status[1];
  if (length == 0 || length > 255) return false;
  // **El paquet, llegit del buffer del xip**: `ReadBuffer` amb el punter d'inici que el xip acaba de dir i tants bytes com
  // diu la seva capçalera (SX126x datasheet §11).
  uint8_t params[3] = {start, 0x00, 0x00};
  if (!wait_busy(100)) return false;
  uint8_t frame[260];
  memset(frame, 0, length + 2);
  frame[0] = kReadBuffer;
  memcpy(frame + 1, params, 2);
  spi_transaction_t t = {};
  t.length = (length + 3) * 8;
  t.tx_buffer = frame;
  t.rx_buffer = frame;
  if (spi_device_transmit(static_cast<spi_device_handle_t>(device_), &t) != ESP_OK) {
    fail("the packet did not come off the radio");
    return false;
  }
  packet.length = length;
  memcpy(packet.data, frame + 3, length);  // opcode, estat, inici, i les dades
  // **El que el xip ha sentit del paquet** (SX126x datasheet §11.5): tres bytes —l'RSSI del paquet, l'SNR i l'RSSI del
  // senyal—, i el primer és el nivell del preàmbulo, que és el que un monitor ensenya (docs/50).
  uint8_t measured[3] = {0, 0, 0};
  if (query(kGetPacketStatus, measured, 3)) {
    packet.rssi_dbm10 = -static_cast<int16_t>(measured[0]) * 5;
    packet.snr_db4 = static_cast<int16_t>(static_cast<int8_t>(measured[1]));
    // **El desplaçament en freqüència del senyal**, que el xip dona en unitats de l'amplada de banda entre l'ample de
    // símbols: `-rssi/2` és el del paquet i `-rssi/2` el del senyal, i la diferència és el que el cristall de l'emissor
    // està desviat. Es dona en hertz perquè una pàgina no ha de saber l'aritmètica del xip (docs/50).
    const int32_t signal = -static_cast<int32_t>(measured[2]) * 5;
    packet.freq_offset_hz = (packet.rssi_dbm10 - signal) / 10;
  }
  packet.crc_ok = (flags & kIrqCrcErr) == 0;
  packet.hz = config_.hz;
  packet.ms = now_ms();
  // **I es torna a escoltar**: la recepció és contínua i qui ha pres el paquet no ha de tornar a engegar la ràdio (docs/30).
  listen(0);
  return true;
}

bool Sx1262::idle() {
  if (device_ == nullptr) return true;
  return standby();
}

bool Sx1262::sleep() {
  if (device_ == nullptr) return true;
  // **El son del xip i no el de la placa** (`SetSleep`, warm start i el mode d'on surt quan se'l desperta): la ràdio
  // calla i el processador segueix. El rail es queda encès, perquè encendre'l costa un segon i mig del fabricant (docs/50).
  const uint8_t sleep_config = 0x04;  // warm start from configuration
  return command(kSetSleep, &sleep_config, 1);
}

// **El SX1262 d'aquesta placa, una vegada**: el rail és el mateix objecte que obre el panell, i si no hi és el xip tampoc
// (docs/50).
Sx1262* t5s3_lora(Pca9535* rail) {
  static Sx1262* the_one = nullptr;
  static bool tried = false;
  if (!tried) {
    tried = true;
    if (rail == nullptr) {
      logf(pal::kLogWarn, "LORA", "there is no expander, so there is no rail for the radio");
      return nullptr;
    }
    the_one = new (std::nothrow) Sx1262(rail);
    if (the_one != nullptr && !the_one->begin()) {
      logf(pal::kLogWarn, "LORA", "%s", the_one->last_error());
      delete the_one;
      the_one = nullptr;
    }
  }
  return the_one;
}

}  // namespace hal
