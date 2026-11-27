// **Les frases del cel, enteses** (0.192.90): el perquè és a la capçalera; aquí hi ha el segell, els camps i les cinc frases que un
// receptor dona de fàbrica (docs/50).
#include "hal/nmea.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace hal {
namespace nmea {
namespace {

const size_t kLineBytes = sizeof(State::line);

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

// **El segell de la frase**: la XOR de tot el que hi ha entre el `$` i l'asterisc, en hexadecimal. Una frase que no quadra no es
// llegeix: un caràcter perdut en un cable llarg dona un número que sembla una latitud (docs/50).
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
  if (text == nullptr || text[0] == '\0') return;
  const int value = atoi(text);
  time.hour = static_cast<uint8_t>(value / 10000);
  time.minute = static_cast<uint8_t>((value / 100) % 100);
  time.second = static_cast<uint8_t>(value % 100);
  const char* dot = strchr(text, '.');
  time.hundredths = (dot != nullptr) ? static_cast<uint8_t>(atoi(dot + 1)) : 0;
}

void read_date(const char* text, GpsTime& time) {
  if (text == nullptr || strlen(text) < 6) return;
  for (int i = 0; i < 6; ++i) {
    if (text[i] < '0' || text[i] > '9') return;
  }
  time.day = static_cast<uint8_t>((text[0] - '0') * 10 + (text[1] - '0'));
  time.month = static_cast<uint8_t>((text[2] - '0') * 10 + (text[3] - '0'));
  // **El segle, que el receptor no diu** (0.192.90): NMEA porta dos dígits d'any i la convenció de tothom —la del `TinyGPS++` que el
  // fabricant porta al seu exemple i la de qualsevol lector de NMEA— és que de vuitanta en amunt és el segle passat. Sense això una
  // data de 1994 es llegia com a 2094 i el rellotge de la placa s'hauria posat un segle endavant (docs/50, docs/60).
  const int year = (text[4] - '0') * 10 + (text[5] - '0');
  time.year = static_cast<uint16_t>((year >= 80) ? (1900 + year) : (2000 + year));
}

// Els décims d'una xifra de text (`0.9` → 9), que és com el receptor dona la dilució i la força del senyal.
uint16_t tenths_of(const char* text) {
  if (text == nullptr || text[0] == '\0') return 0;
  const double value = atof(text);
  return static_cast<uint16_t>(value * 10.0 + 0.5);
}

// **Si aquest número de satèl·lit és dels que la posició fa servir**: el GSA i el GSV arriben en moments diferents del cicle i el
// que compta els compta el GSA (docs/50).
bool is_a_used_id(const State& state, uint8_t id) {
  for (uint8_t i = 0; i < state.used_count; ++i) {
    if (state.used_ids[i] == id) return true;
  }
  return false;
}

void mark_used(State& state, uint8_t id) {
  if (id == 0 || is_a_used_id(state, id)) return;
  if (state.used_count >= sizeof(state.used_ids)) return;
  state.used_ids[state.used_count++] = id;
  for (uint8_t i = 0; i < state.sky_count; ++i) {
    if (state.sky[i].id == id) state.sky[i].used = true;
  }
}

bool parse_rmc(State& state, const char* body) {
  char work[kLineBytes];
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
  read_time(time, state.fix.time);
  read_date(date, state.fix.time);
  // **L'estat és el que diu si la posició serveix**: `A` vol dir vàlida i `V` que el receptor encara no la té (docs/50).
  const bool ok = (status[0] == 'A');
  state.fix.valid = ok;
  // **L'hora és vàlida si la frase la porta**, i si és d'aquest segle ho decideix qui se la creu: el nucli no posa el rellotge amb una
  // data d'abans del 2024 (`believe_the_sky`), i una pàgina que ensenya el que diu el receptor ha de poder ensenyar una data vella
  // d'un simulador sense que el parser la titlli de mentida (0.192.90, docs/30, docs/60).
  state.fix.time.valid = ok && state.fix.time.year != 0;
  if (!ok) return true;
  state.fix.lat_e7 = degrees_e7(lat, (north != nullptr && north[0] != '\0') ? north[0] : 'N');
  state.fix.lon_e7 = degrees_e7(lon, (east != nullptr && east[0] != '\0') ? east[0] : 'E');
  // **Els nusos a centímetres per segon**: un nus són 51,444 cm/s, i el receptor dona els nusos amb un decimal (docs/50).
  if (knots != nullptr && knots[0] != '\0') state.fix.speed_cm_s = static_cast<int32_t>(atof(knots) * 51.444 + 0.5);
  if (course != nullptr && course[0] != '\0') state.fix.course_deg10 = static_cast<int32_t>(atof(course) * 10.0 + 0.5);
  return true;
}

bool parse_gga(State& state, const char* body) {
  char work[kLineBytes];
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
  read_time(time, state.fix.time);
  state.fix.quality = static_cast<uint8_t>(atoi(quality));
  if (sats != nullptr && sats[0] != '\0') state.fix.satellites_used = static_cast<uint8_t>(atoi(sats));
  state.fix.hdop_x10 = tenths_of(hdop);
  if (altitude != nullptr && altitude[0] != '\0') state.fix.altitude_cm = static_cast<int32_t>(atof(altitude) * 100.0);
  if (geoid != nullptr && geoid[0] != '\0') state.fix.geoid_cm = static_cast<int32_t>(atof(geoid) * 100.0);
  // **Un GGA amb qualitat zero no és una posició**: el receptor hi posa la latitud i la longitud de l'última que va tenir, o sigui que
  // creure-se-la seria ensenyar un lloc on la placa no és (docs/50).
  if (state.fix.quality == 0) return true;
  if (lat != nullptr && lat[0] != '\0') {
    state.fix.lat_e7 = degrees_e7(lat, (north != nullptr && north[0] != '\0') ? north[0] : 'N');
    state.fix.lon_e7 = degrees_e7(lon, (east != nullptr && east[0] != '\0') ? east[0] : 'E');
  }
  return true;
}

bool parse_gsa(State& state, const char* body) {
  char work[kLineBytes];
  snprintf(work, sizeof(work), "%s", body);
  char* at = work;
  (void)next_field(at);  // el tipus
  (void)next_field(at);  // el mode (`A` automàtic o `M` manual)
  char* fixtype = next_field(at);
  if (fixtype != nullptr && fixtype[0] != '\0') {
    // **El tipus de posició** (1 cap, 2 de dues dimensions, 3 de tres dimensions): l'1 no és cap posició i el 2 i el 3 sí que ho són
    // —el 2 no té altitud i la latitud i la longitud són tan bones com les del 3—. Abans això deia que el 2 i el 3 **no** eren una
    // posició, o sigui que un receptor amb tres satèl·lits ensenyava «no position yet» amb la posició a la mà, i com que el GSA arriba
    // després del RMC i del GGA dins del mateix cicle, el que ensenyava la pàgina depenia del moment del cicle en què es llegís
    // (0.192.90, docs/50).
    const int type = atoi(fixtype);
    if (type <= 1) state.fix.valid = false;
  }
  // **Els satèl·lits que aquesta posició fa servir**, que és el que marca el cel: una llista nova per a cada GSA (docs/50).
  state.used_count = 0;
  uint8_t used = 0;
  for (int i = 0; i < 12; ++i) {
    char* prn = next_field(at);
    if (prn == nullptr) break;
    if (prn[0] == '\0') continue;
    ++used;
    const uint8_t id = static_cast<uint8_t>(atoi(prn));
    if (id != 0) mark_used(state, id);
  }
  state.fix.satellites_used = used;
  char* pdop = next_field(at);
  char* hdop = next_field(at);
  char* vdop = next_field(at);
  if (pdop != nullptr) state.fix.pdop_x10 = tenths_of(pdop);
  if (hdop != nullptr) state.fix.hdop_x10 = tenths_of(hdop);
  if (vdop != nullptr) state.fix.vdop_x10 = tenths_of(vdop);
  return true;
}

bool parse_gsv(State& state, const char* body) {
  char work[kLineBytes];
  snprintf(work, sizeof(work), "%s", body);
  char* at = work;
  (void)next_field(at);  // el tipus
  char* messages = next_field(at);
  char* number = next_field(at);
  char* in_view = next_field(at);
  if (messages == nullptr || number == nullptr) return false;
  const int total = atoi(messages);
  const int which = atoi(number);
  // **Quants en veu el receptor**: el número de la frase i no el d'una llista que creix i no baixa mai, o sigui que quan un satèl·lit
  // se'n va el número baixa (docs/50).
  if (in_view != nullptr && in_view[0] != '\0') state.fix.satellites_seen = static_cast<uint8_t>(atoi(in_view));
  // **La primera frase d'una tanda escombra el cel**: les GSV venen en grups de fins a quatre satèl·lits i la primera diu quants n'hi
  // ha en total. El que **no** s'escombra és la marca dels que compta la posició, que és del GSA i s'ha de tornar a posar a cada
  // satèl·lit (abans s'esborrava amb la llista i el mapa del cel no ensenyava mai quins compten) (0.192.90, docs/50).
  if (which <= 1 || which > total) {
    state.sky_count = 0;
    state.filling = 0;
  }
  for (int slot = 0; slot < 4; ++slot) {
    char* prn = next_field(at);
    char* elevation = next_field(at);
    char* azimuth = next_field(at);
    char* snr = next_field(at);
    if (prn == nullptr || prn[0] == '\0') break;
    if (state.filling >= sizeof(state.sky) / sizeof(state.sky[0])) break;
    GpsSatellite& sat = state.sky[state.filling];
    sat.id = static_cast<uint8_t>(atoi(prn));
    sat.elevation = (elevation != nullptr) ? static_cast<uint8_t>(atoi(elevation)) : 0;
    sat.azimuth = (azimuth != nullptr) ? static_cast<uint16_t>(atoi(azimuth)) : 0;
    sat.snr = (snr != nullptr) ? static_cast<uint8_t>(atoi(snr)) : 0;
    sat.used = is_a_used_id(state, sat.id);
    ++state.filling;
    if (state.filling > state.sky_count) state.sky_count = state.filling;
  }
  return true;
}

bool parse_vtg(State& state, const char* body) {
  char work[kLineBytes];
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
  if (course != nullptr && course[0] != '\0') state.fix.course_deg10 = static_cast<int32_t>(atof(course) * 10.0 + 0.5);
  if (kmh != nullptr && kmh[0] != '\0') {
    state.fix.speed_cm_s = static_cast<int32_t>(atof(kmh) * 27.778 + 0.5);
  } else if (knots != nullptr && knots[0] != '\0') {
    state.fix.speed_cm_s = static_cast<int32_t>(atof(knots) * 51.444 + 0.5);
  }
  return true;
}

}  // namespace

void reset(State& state) { state = State(); }

bool take(State& state, const char* sentence, size_t length) {
  // **Del `$` fins al final i res més**: el que arribi abans del `$` és el que quedava d'una frase trencada o el que el mòdul diu en
  // arrencar (docs/50).
  if (sentence == nullptr || length == 0) return false;
  const char* dollar = static_cast<const char*>(memchr(sentence, '$', length));
  if (dollar == nullptr) return false;
  const size_t skip = static_cast<size_t>(dollar - sentence);
  if (length <= skip) return false;
  ++state.sentences;
  if (!checksum_ok(dollar)) {
    ++state.bad;
    state.fix.sentences = state.sentences;
    state.fix.bad_sentences = state.bad;
    return true;
  }
  ++state.good;
  const char* body = dollar + 1;
  // **Quina frase és**: les dues lletres del tipus van després del parladora (`GNRMC`, `GPRMC`, `GLGGA`…), o sigui que el que compta
  // són les tres últimes lletres abans de la coma (docs/50).
  if (strlen(body) < 5) return false;
  char kind[4] = {0};
  kind[0] = body[2];
  kind[1] = body[3];
  kind[2] = body[4];
  if (strcmp(kind, "RMC") == 0) {
    parse_rmc(state, body);
  } else if (strcmp(kind, "GGA") == 0) {
    parse_gga(state, body);
  } else if (strcmp(kind, "GSA") == 0) {
    parse_gsa(state, body);
  } else if (strcmp(kind, "GSV") == 0) {
    parse_gsv(state, body);
  } else if (strcmp(kind, "VTG") == 0) {
    parse_vtg(state, body);
  }
  state.fix.sentences = state.sentences;
  state.fix.bad_sentences = state.bad;
  return true;
}

bool feed(State& state, char byte) {
  if (byte == '\n' || byte == '\r') {
    if (state.used == 0) return false;
    state.line[state.used] = '\0';
    const bool examined = take(state, state.line, state.used);
    state.used = 0;
    return examined;
  }
  // **Una frase que no cap és una frase que es llença sencera** i es compta, perquè una línia trencada que s'enganxa amb la següent és
  // un segell que falla i una posició que no arriba mai sense que ningú sàpiga per què (docs/50).
  if (state.used + 1 >= sizeof(state.line)) {
    state.used = 0;
    ++state.bad;
    state.fix.bad_sentences = state.bad;
    return false;
  }
  state.line[state.used++] = byte;
  return false;
}

bool heard_a_sentence(const State& state) { return state.good > 0; }

}  // namespace nmea
}  // namespace hal
