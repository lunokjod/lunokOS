// **El receptor del cel d'aquesta placa, pel seu cable** (0.192.63): el perquè i els pins són a la capçalera; aquí hi ha la UART,
// el sondeig de bauds i les cinc frases NMEA que es llegeixen (NMEA 0183 v4, §RMC/§GGA/§GSA/§GSV/§VTG, que és el que un M10Q dona
// de fàbrica) (docs/50).
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

// **El següent camp d'una frase** (NMEA 0183: comes i un asterisc abans del segell): torna el tros i deixa el punter al següent, o
// `nullptr` quan la frase s'ha acabat. Els camps buits són cadenes buides i no un salt, perquè un GGA sense altitud és un camp buit i
// no una frase més curta (docs/50).
char* next_field(char*& at) {
  if (at == nullptr) return nullptr;
  char* start = at;
  char* comma = strchr(start, ',');
  if (comma == nullptr) {
    char* star = strchr(start, '*');
    if (star != nullptr) *star = '\0';
    at = nullptr;
    return start;
  }
  *comma = '\0';
  at = comma + 1;
  return start;
}

// **El segell de la frase**: la XOR de tot el que hi ha entre el `$` i l'asterisc, en hexadecimal. Una frase que no quadra no es llegeix:
// un caràcter perdut en un cable llarg dona un número que sembla una latitud (docs/50).
bool checksum_ok(const char* sentence) {
  const char* star = strrchr(sentence, '*');
  if (star == nullptr || star[1] == '\0' || star[2] == '\0') return false;
  uint8_t sum = 0;
  for (const char* at = sentence + 1; at < star; ++at) sum ^= static_cast<uint8_t>(*at);
  char hex[3] = {star[1], star[2], '\0'};
  return static_cast<uint8_t>(strtoul(hex, nullptr, 16)) == sum;
}

// **Una coordenada NMEA** (`ddmm.mmmm` o `dddmm.mmmm`) a graus per deu milions, amb el seu quadrant.
int32_t degrees_e7(const char* text, char hemi) {
  if (text == nullptr || text[0] == '\0') return 0;
  const double raw = atof(text);
  const int degrees = static_cast<int>(raw / 100.0);
  const double minutes = raw - degrees * 100.0;
  double value = degrees + minutes / 60.0;
  if (hemi == 'S' || hemi == 'W') value = -value;
  return static_cast<int32_t>(value * 10000000.0 + ((value < 0) ? -0.5 : 0.5));
}

// **L'hora d'una frase** (`hhmmss.ss`) i la data (`ddmmyy`), que van en frases diferents: el RMC les porta totes dues i el GGA només
// l'hora (docs/50).
void read_time(const char* text, GpsTime& time) {
  if (text == nullptr) return;
  const int value = atoi(text);
  time.hour = static_cast<uint8_t>(value / 10000);
  time.minute = static_cast<uint8_t>((value / 100) % 100);
  time.second = static_cast<uint8_t>(value % 100);
  const char* dot = strchr(text, '.');
  time.hundredths = (dot != nullptr) ? static_cast<uint8_t>(atoi(dot + 1)) : 0;
}

void read_date(const char* text, GpsTime& time) {
  if (text == nullptr || strlen(text) < 6) return;
  time.day = static_cast<uint8_t>((text[0] - '0') * 10 + (text[1] - '0'));
  time.month = static_cast<uint8_t>((text[2] - '0') * 10 + (text[3] - '0'));
  const int year = (text[4] - '0') * 10 + (text[5] - '0');
  time.year = static_cast<uint16_t>(2000 + year);
}

// Els décims d'una xifra de text (`0.9` → 9), que és com el receptor dona la dilució i la força del senyal.
uint16_t tenths_of(const char* text) {
  if (text == nullptr || text[0] == '\0') return 0;
  const double value = atof(text);
  return static_cast<uint16_t>(value * 10.0 + 0.5);
}

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
  used_ = 0;
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
  // **Un mòdul, no dos**: es prova el ritme de la u-blox i, si en mil dos-cents mil·lisegons no ha dit ni una lletra, el de la
  // Quectel. El primer que parla és el que hi ha gravat a la placa (docs/pinmap.md, docs/50).
  const uint32_t rates[2] = {kBaudM10Q, kBaudL76K};
  for (uint32_t baud : rates) {
    if (!open_the_wire(baud)) return false;
    uart_flush_input(kPort);
    const uint32_t from = now_ms();
    while (now_ms() - from < kProbeMs) {
      uint8_t byte = 0;
      if (uart_read_bytes(kPort, &byte, 1, 0) == 1) {
        // **El que ha parlat és aquest**: es buida el que hi hagi i es torna bo, i la frase que s'ha llegit mentre es mirava es
        // descarta perquè no comença per cap `$` (docs/50).
        uart_flush_input(kPort);
        used_ = 0;
        hal::logf(pal::kLogInfo, "GPS", "the receiver answers at %u bauds (%u ms)", static_cast<unsigned>(baud),
                  static_cast<unsigned>(now_ms() - started));
        return true;
      }
    }
    close_the_wire();
  }
  error_ = "the receiver says nothing at 38400 or at 9600 bauds";
  return false;
}

bool GpsM10Q::off() {
  if (!open_) return false;
  close_the_wire();
  // **El rail es queda com és** i es diu: apagar-lo apagaria el LoRa, que el comparteix, i aquest driver no és l'amo de la ràdio
  // (docs/pinmap.md, docs/50).
  return true;
}

void GpsM10Q::take_a_line(char* line, size_t length) {
  // **Del `$` fins al final i res més**: el que arribi abans del `$` és el que quedava d'una frase trencada o el que el mòdul diu en
  // arrencar (un M10Q de fàbrica no diu res, però un de configurat pot dir-ho) (docs/50).
  const char* dollar = static_cast<const char*>(memchr(line, '$', length));
  if (dollar == nullptr) return;
  const size_t skip = static_cast<size_t>(dollar - line);
  if (length <= skip) return;
  ++sentences_;
  const char* body = dollar + 1;
  if (!checksum_ok(dollar)) {
    ++bad_;
    fix_.bad_sentences = bad_;
    return;
  }
  // **Quina frase és**: les dues lletres del tipus van després del parladora (`GNRMC`, `GPRMC`, `GLGGA`…), o sigui que el que compta
  // són les tres últimes lletres abans de la coma (docs/50).
  char kind[4] = {0};
  if (strlen(body) < 5) return;
  kind[0] = body[2];
  kind[1] = body[3];
  kind[2] = body[4];
  kind[3] = '\0';
  // Una còpia perquè els camps es tallen a lloc.
  char work[112];
  snprintf(work, sizeof(work), "%s", body);
  if (strcmp(kind, "RMC") == 0) {
    parse_rmc(work);
  } else if (strcmp(kind, "GGA") == 0) {
    parse_gga(work);
  } else if (strcmp(kind, "GSA") == 0) {
    parse_gsa(work);
  } else if (strcmp(kind, "GSV") == 0) {
    parse_gsv(work);
  } else if (strcmp(kind, "VTG") == 0) {
    parse_vtg(work);
  }
  fix_.sentences = sentences_;
  fix_.bad_sentences = bad_;
}

bool GpsM10Q::parse_rmc(const char* body) {
  char work[112];
  snprintf(work, sizeof(work), "%s", body);
  char* at = work;
  // El primer camp és el tipus (`GNRMC`) i el segon l'hora.
  (void)next_field(at);
  char* time = next_field(at);
  char* status = next_field(at);
  char* lat = next_field(at);
  char* north = next_field(at);
  char* lon = next_field(at);
  char* east = next_field(at);
  char* knots = next_field(at);
  char* course = next_field(at);
  char* date = next_field(at);
  if (status == nullptr) return false;
  read_time(time, fix_.time);
  read_date(date, fix_.time);
  // **L'estat és el que diu si la posició serveix**: `A` vol dir vàlida i `V` que el receptor encara no la té (docs/50).
  const bool ok = (status[0] == 'A');
  fix_.valid = ok;
  fix_.time.valid = ok && fix_.time.year >= 2024;
  if (!ok) return true;
  fix_.lat_e7 = degrees_e7(lat, (north != nullptr && north[0] != '\0') ? north[0] : 'N');
  fix_.lon_e7 = degrees_e7(lon, (east != nullptr && east[0] != '\0') ? east[0] : 'E');
  // **Els nusos a centímetres per segon**: un nus són 51,444 cm/s, i el receptor dona els nusos amb un decimal (docs/50).
  if (knots != nullptr && knots[0] != '\0') fix_.speed_cm_s = static_cast<int32_t>(atof(knots) * 51.444 + 0.5);
  if (course != nullptr && course[0] != '\0') fix_.course_deg10 = static_cast<int32_t>(atof(course) * 10.0 + 0.5);
  return true;
}

bool GpsM10Q::parse_gga(const char* body) {
  char work[112];
  snprintf(work, sizeof(work), "%s", body);
  char* at = work;
  (void)next_field(at);  // el tipus
  char* time = next_field(at);
  char* lat = next_field(at);
  char* north = next_field(at);
  char* lon = next_field(at);
  char* east = next_field(at);
  char* quality = next_field(at);
  char* sats = next_field(at);
  char* hdop = next_field(at);
  char* altitude = next_field(at);
  (void)next_field(at);  // la lletra de l'altitud (`M`)
  char* geoid = next_field(at);
  if (quality == nullptr) return false;
  read_time(time, fix_.time);
  fix_.quality = static_cast<uint8_t>(atoi(quality));
  if (sats != nullptr && sats[0] != '\0') fix_.satellites_used = static_cast<uint8_t>(atoi(sats));
  fix_.hdop_x10 = tenths_of(hdop);
  if (altitude != nullptr && altitude[0] != '\0') fix_.altitude_cm = static_cast<int32_t>(atof(altitude) * 100.0);
  if (geoid != nullptr && geoid[0] != '\0') fix_.geoid_cm = static_cast<int32_t>(atof(geoid) * 100.0);
  // **Un GGA amb qualitat zero no és una posició**: el receptor hi posa la latitud i la longitud de l'última que va tenir, o sigui que
  // creure-se-la seria ensenyar un lloc on la placa no és (docs/50).
  if (fix_.quality == 0) return true;
  if (lat != nullptr && lat[0] != '\0') {
    fix_.lat_e7 = degrees_e7(lat, (north != nullptr && north[0] != '\0') ? north[0] : 'N');
    fix_.lon_e7 = degrees_e7(lon, (east != nullptr && east[0] != '\0') ? east[0] : 'E');
  }
  return true;
}

bool GpsM10Q::parse_gsa(const char* body) {
  char work[112];
  snprintf(work, sizeof(work), "%s", body);
  char* at = work;
  (void)next_field(at);  // el tipus
  (void)next_field(at);  // el mode (`A` automàtic o `M` manual)
  char* fixtype = next_field(at);
  if (fixtype != nullptr) {
    // **El tipus de posició** (1 no fix, 2 no fix, 3 fix 2D, 4 fix 3D): el 2 i el 3 no són una posició de debò i el 4 sí.
    const int type = atoi(fixtype);
    if (type < 4) fix_.valid = false;
  }
  // Els dotze números de satèl·lit que aquesta posició fa servir: és el que marca `used` al cel (docs/50).
  uint8_t used = 0;
  for (int i = 0; i < 12; ++i) {
    char* prn = next_field(at);
    if (prn == nullptr) break;
    if (prn[0] == '\0') continue;
    ++used;
    const uint8_t id = static_cast<uint8_t>(atoi(prn));
    for (uint8_t s = 0; s < sky_count_; ++s) {
      if (sky_[s].id == id) sky_[s].used = true;
    }
  }
  fix_.satellites_used = used;
  char* pdop = next_field(at);
  char* hdop = next_field(at);
  char* vdop = next_field(at);
  if (pdop != nullptr) fix_.pdop_x10 = tenths_of(pdop);
  if (hdop != nullptr) fix_.hdop_x10 = tenths_of(hdop);
  if (vdop != nullptr) fix_.vdop_x10 = tenths_of(vdop);
  return true;
}

bool GpsM10Q::parse_gsv(const char* body) {
  char work[112];
  snprintf(work, sizeof(work), "%s", body);
  char* at = work;
  (void)next_field(at);  // el tipus
  char* messages = next_field(at);
  char* number = next_field(at);
  char* in_view = next_field(at);
  if (messages == nullptr || number == nullptr) return false;
  const int total = atoi(messages);
  const int which = atoi(number);
  if (in_view != nullptr && in_view[0] != '\0') fix_.satellites_seen = static_cast<uint8_t>(atoi(in_view));
  // **La primera frase d'una tanda escombra el cel**: les GSV venen en grups de fins a quatre satèl·lits i la primera diu quants n'hi ha
  // en total, o sigui que és el moment de començar la llista de nou (docs/50).
  if (which <= 1 || which > total) {
    sky_count_ = 0;
    gsv_next_ = 0;
  }
  for (int slot = 0; slot < 4; ++slot) {
    char* prn = next_field(at);
    char* elevation = next_field(at);
    char* azimuth = next_field(at);
    char* snr = next_field(at);
    if (prn == nullptr || prn[0] == '\0') break;
    if (gsv_next_ >= 32) break;
    GpsSatellite& sat = sky_[gsv_next_];
    sat.id = static_cast<uint8_t>(atoi(prn));
    sat.elevation = (elevation != nullptr) ? static_cast<uint8_t>(atoi(elevation)) : 0;
    sat.azimuth = (azimuth != nullptr) ? static_cast<uint16_t>(atoi(azimuth)) : 0;
    sat.snr = (snr != nullptr) ? static_cast<uint8_t>(atoi(snr)) : 0;
    // **La marca de «el comptes» ve del GSA**, que és una altra frase: es conserva la que ja hi havia per a aquest número (docs/50).
    sat.used = false;
    for (uint8_t s = 0; s < sky_count_; ++s) {
      if (sky_[s].id == sat.id) sat.used = sky_[s].used;
    }
    ++gsv_next_;
    if (gsv_next_ > sky_count_) sky_count_ = gsv_next_;
  }
  return true;
}

bool GpsM10Q::parse_vtg(const char* body) {
  char work[112];
  snprintf(work, sizeof(work), "%s", body);
  char* at = work;
  (void)next_field(at);  // el tipus
  char* course = next_field(at);
  (void)next_field(at);  // la lletra del rumb (`T` de debò, `M` magnètic)
  (void)next_field(at);
  (void)next_field(at);  // la lletra del rumb magnètic
  char* knots = next_field(at);
  (void)next_field(at);
  char* kmh = next_field(at);
  if (course != nullptr && course[0] != '\0') fix_.course_deg10 = static_cast<int32_t>(atof(course) * 10.0 + 0.5);
  if (kmh != nullptr && kmh[0] != '\0') {
    fix_.speed_cm_s = static_cast<int32_t>(atof(kmh) * 27.778 + 0.5);
  } else if (knots != nullptr && knots[0] != '\0') {
    fix_.speed_cm_s = static_cast<int32_t>(atof(knots) * 51.444 + 0.5);
  }
  return true;
}

bool GpsM10Q::poll(GpsFix& out) {
  if (!open_) {
    error_ = "the receiver is off";
    return false;
  }
  // **El que hi hagi, i prou** (0.192.63): this plate has no interrupt line of the module, so the read is a poll and the buffer of the
  // UART is what says how much has arrived. No `uart_read_bytes` with a timeout: waiting here would be waiting in the turn of the
  // whole board (docs/30, docs/50).
  uint8_t chunk[128];
  for (int round = 0; round < 8; ++round) {
    const int got = uart_read_bytes(kPort, chunk, sizeof(chunk), 0);
    if (got <= 0) break;
    for (int i = 0; i < got; ++i) {
      const char c = static_cast<char>(chunk[i]);
      if (c == '\n' || c == '\r') {
        if (used_ > 0) {
          line_[used_] = '\0';
          take_a_line(line_, used_);
        }
        used_ = 0;
        continue;
      }
      // **Una frase que no cap és una frase que es llença sencera** i es diu al comptador, perquè una línia trencada que s'enganxa
      // amb la següent és un segell que falla i una posició que no arriba mai sense que ningú sàpiga per què (docs/50).
      if (used_ + 1 >= sizeof(line_)) {
        used_ = 0;
        ++bad_;
        fix_.bad_sentences = bad_;
        continue;
      }
      line_[used_++] = c;
    }
  }
  fix_.satellites_seen = (fix_.satellites_seen > sky_count_) ? fix_.satellites_seen : sky_count_;
  out = fix_;
  return true;
}

const GpsSatellite* GpsM10Q::satellites(uint8_t& count) const {
  count = sky_count_;
  return (sky_count_ > 0) ? sky_ : nullptr;
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
