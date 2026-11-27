// **El receptor del cel d'aquesta placa, pel seu cable** (0.192.63): el perquè i els pins són a la capçalera; aquí hi ha **el
// cable** —la UART, el rail i el sondeig dels dos bauds— i res més. Les frases NMEA i el que volen dir són a `hal::nmea`
// (`lib/hal/src/nmea.cpp`), que no sap què és una UART i es prova al PC amb frases de debò (0.192.90, docs/50, docs/85).
#include "t5s3/gps_m10q.h"

#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_timer.h>
#include <stdlib.h>
#include <string.h>

#include <new>

#include "hal/log.h"
#include "pal/pal.h"

namespace hal {
namespace {

// **La UART que aquest driver fa servir**: la 1 i no la 0, perquè la 43 i la 44 són les potes de la UART0 i aquesta placa no la fa
// servir per a res (la consola és el USB natiu: `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG`). Una UART qualsevol es pot portar a qualsevol
// pota amb la matriu del xip, o sigui que la 1 amb les potes de la 0 és el camí que no toca res del que el sistema ja té (docs/50).
const uart_port_t kPort = UART_NUM_1;
// El bufet de recepció: un M10Q a 38 400 bauds dona uns deu caràcters cada mil·lisegon i les cinc frases d'un cicle són uns quatre-cents
// caràcters, o sigui que dos kilobytes són una segona sencera de marge per a un torn que s'encalli (docs/50).
const int kRxBuffer = 2048;
// **Els dos ritmes de la placa** (docs/pinmap.md): la u-blox a 38 400 i la Quectel a 9 600. El primer que parla és el que hi ha.
const uint32_t kBaudM10Q = 38400;
const uint32_t kBaudL76K = 9600;
// **Quan s'espera una frase abans de canviar de ritme**: mil dos-cents mil·lisegons són unes dotze frases de NMEA al ritme lent, o sigui
// que un mòdul que hi és ha parlat de sobres (docs/50).
const uint32_t kProbeMs = 1200;

uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

// **Els bauds de la placa i el marge per dir que no** (docs/pinmap.md de la placa, docs/50): the manufacturer's own example
// (`examples/GPS/main/main.ino`) opens the L76K's 9 600 first and only then the u-blox 38 400, i el que compta per dir «aquest
// ritme és el bo» és **una frase amb el segell bé**, no un byte qualsevol: a un ritme equivocat la UART també dona bytes —el que
// dona és escombraries—, i abans això es creia que el mòdul parlava i es quedava enganxat al ritme dolent per sempre (0.192.90,
// docs/50).

}  // namespace

GpsM10Q::GpsM10Q(Pca9535* rail) : rail_(rail) {}

GpsM10Q::~GpsM10Q() { close_the_wire(); }

GpsCaps GpsM10Q::caps() const {
  GpsCaps out;
  // **El mòdul hi és** perquè el pinmap de la placa diu que hi és; **quin dels dos** només ho diu el cable, o sigui que el nom és
  // `unknown` fins que el sondeig ha sentit parlar algú (docs/pinmap.md, docs/50).
  out.present = true;
  out.chip = (baud_ == kBaudL76K) ? "L76K" : ((baud_ == kBaudM10Q) ? "MIA-M10Q" : "unknown");
  out.baud = baud_;
  out.powered = open_;
  return out;
}

bool GpsM10Q::open_the_wire(uint32_t baud) {
  close_the_wire();
  uart_config_t config = {};
  config.baud_rate = static_cast<int>(baud);
  config.data_bits = UART_DATA_8_BITS;
  config.parity = UART_PARITY_DISABLE;
  config.stop_bits = UART_STOP_BITS_1;
  config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  config.source_clk = UART_SCLK_DEFAULT;
  if (uart_driver_install(kPort, kRxBuffer, 0, 0, nullptr, 0) != ESP_OK) {
    error_ = "the UART of the receiver did not come up";
    return false;
  }
  if (uart_param_config(kPort, &config) != ESP_OK) {
    error_ = "the UART did not take 8N1";
    uart_driver_delete(kPort);
    return false;
  }
  // **Les potes del mòdul**, amb el comentari del pinmap: el TX del xip va al RX del receptor i a l'inrevés (docs/pinmap.md).
  if (uart_set_pin(kPort, kGpsTxPin, kGpsRxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
    error_ = "the pins of the receiver's UART are taken";
    uart_driver_delete(kPort);
    return false;
  }
  baud_ = baud;
  open_ = true;
  return true;
}

void GpsM10Q::close_the_wire() {
  if (!open_) return;
  uart_driver_delete(kPort);
  open_ = false;
  // **Un cable que s'obre a un altre ritme no hereta la meitat d'una frase** (0.192.90): the state of `hal::nmea` goes with the wire
  // it was reading, o sigui que el que s'ha llegit es llença sencer (docs/50).
  nmea::reset(nmea_);
}

bool GpsM10Q::on() {
  if (open_) return true;
  if (rail_ == nullptr) {
    error_ = "there is no expander, so there is no rail for the receiver";
    return false;
  }
  // **El rail és compartit amb el LoRa** (docs/pinmap.md: `LORA_EN` encén els dos): encendre'l és el que fa que el mòdul visqui, i
  // apagar-lo callaria també la ràdio, o sigui que aquest driver només l'encén (docs/50).
  (void)rail_->write(Expander::kLoraEnable, true);
  const uint32_t started = now_ms();
  // **Un mòdul, no dos**: es prova el ritme de la Quectel —el que obre el fabricant— i, si en mil dos-cents mil·lisegons no ha dit
  // cap frase sencera, el de la u-blox. El primer que **parla** és el que hi ha gravat a la placa (docs/pinmap.md, docs/50).
  const uint32_t rates[2] = {kBaudL76K, kBaudM10Q};
  for (uint32_t baud : rates) {
    if (!open_the_wire(baud)) return false;
    if (hear_a_sentence(kProbeMs)) {
      hal::logf(pal::kLogInfo, "GPS", "the receiver answers at %u bauds (%u baud test, %u ms)",
                static_cast<unsigned>(baud), static_cast<unsigned>(baud), static_cast<unsigned>(now_ms() - started));
      return true;
    }
    close_the_wire();
  }
  error_ = "the receiver says nothing at 9600 or at 38400 bauds (or it says it with the checksum broken)";
  return false;
}

// **Esperar una frase de debò**: el cable s'escolta durant `ms` i el que compta és que `hal::nmea` hagi entès una frase —segell bé i
// una de les cinc—, no que hagi arribat un byte. A un ritme equivocat la UART dona bytes d'un marc trencat, o sigui que un byte no
// vol dir res; una frase sencera sí (0.192.90, docs/50).
bool GpsM10Q::hear_a_sentence(uint32_t ms) {
  nmea::reset(nmea_);
  const uint32_t from = now_ms();
  uint8_t chunk[64];
  while (now_ms() - from < ms) {
    const int got = uart_read_bytes(kPort, chunk, sizeof(chunk), 0);
    for (int i = 0; i < got; ++i) {
      (void)nmea::feed(nmea_, static_cast<char>(chunk[i]));
    }
    if (nmea::heard_a_sentence(nmea_)) return true;
  }
  return nmea::heard_a_sentence(nmea_);
}

bool GpsM10Q::off() {
  if (!open_) return false;
  close_the_wire();
  // **El rail es queda com és** i es diu: apagar-lo apagaria el LoRa, que el comparteix, i aquest driver no és l'amo de la ràdio
  // (docs/pinmap.md, docs/50).
  return true;
}

bool GpsM10Q::poll(GpsFix& out) {
  if (!open_) {
    error_ = "the receiver is off";
    return false;
  }
  // **El que hi hagi, i prou** (0.192.63): this plate has no interrupt line of the module, so the read is a poll and the buffer of the
  // UART is what says how much has arrived. No `uart_read_bytes` with a timeout: waiting here would be waiting in the turn of the
  // whole board (docs/30, docs/50). **I el que s'ha llegit no es queda aquí**: les frases les entén `hal::nmea`, que és el que
  // sap què vol dir un `$GNRMC` (0.192.90, docs/50).
  uint8_t chunk[128];
  for (int round = 0; round < 8; ++round) {
    const int got = uart_read_bytes(kPort, chunk, sizeof(chunk), 0);
    if (got <= 0) break;
    for (int i = 0; i < got; ++i) {
      (void)nmea::feed(nmea_, static_cast<char>(chunk[i]));
    }
  }
  out = nmea_.fix;
  return true;
}

const GpsSatellite* GpsM10Q::satellites(uint8_t& count) const {
  count = nmea_.sky_count;
  return (nmea_.sky_count > 0) ? nmea_.sky : nullptr;
}

// **El receptor d'aquesta placa, una vegada** (0.192.63): el rail és el mateix objecte que obre el panell, i si no hi és el receptor
// tampoc. **No s'obre el cable aquí**: preguntar les capacitats no ha de tocar cap pota, o sigui que el sondeig i la UART arriben amb
// el primer `on()` (docs/50).
GpsM10Q* t5s3_gps(Pca9535* rail) {
  static GpsM10Q* the_one = nullptr;
  static bool tried = false;
  if (!tried) {
    tried = true;
    if (rail == nullptr) {
      logf(pal::kLogWarn, "GPS", "there is no expander, so there is no rail for the receiver");
      return nullptr;
    }
    the_one = new (std::nothrow) GpsM10Q(rail);
  }
  return the_one;
}

}  // namespace hal
