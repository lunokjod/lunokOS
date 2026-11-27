// **Les frases del cel, llegides** (0.192.90): el parser NMEA viu a `lib/hal/src/nmea.cpp` i **no sap què és una UART**, o sigui que
// es pot provar aquí amb frases de debò —les del manual de NMEA i les del receptor d'aquesta placa— sense un cable ni un satèl·lit
// (docs/50, docs/85).
//
// El que es mira aquí és el que una pàgina ensenya i el que no: que una posició de dues dimensions **és** una posició, que el GSA i el
// GSV arriben en moments diferents del cicle i no s'esborren la feina, que una frase amb el segell malament no es creu, i que el que
// diu «aquest ritme és el bo» és **una frase sencera** i no un byte qualsevol (docs/50).
#include <string.h>

#include <unity.h>

#include "hal/nmea.h"

void setUp(void) {}

void tearDown(void) {}

namespace {

// Les frases d'un receptor de debò, amb el seu segell XOR calculat: el GGA i el RMC d'un cicle, el GSA que diu quins satèl·lits
// compten, les dues meitats del GSV del cel i el VTG del moviment (NMEA 0183 v4, docs/50).
const char* const kRmc = "$GNRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*74";
const char* const kGga = "$GNGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*59";
const char* const kGsa3d = "$GNGSA,A,3,04,05,,09,12,,,24,,,,,2.5,1.3,2.1*27";
const char* const kGsaNoFix = "$GPGSA,A,1,,,,,,,,,,,,,,,*1E";
const char* const kGsvOne = "$GPGSV,2,1,08,01,40,083,46,02,17,308,41,12,07,344,39,14,22,228,45*75";
const char* const kGsvTwo = "$GPGSV,2,2,08,15,15,011,40,16,20,060,43,18,30,300,42,21,10,100,30*7E";
const char* const kVtg = "$GPVTG,084.4,T,,M,022.4,N,041.5,K,A*01";

void say(hal::nmea::State& state, const char* sentence) {
  (void)hal::nmea::take(state, sentence, strlen(sentence));
}

}  // namespace

// **Una posició sencera**: el que el receptor diu d'un cicle —on és, amb quina qualitat, a quina altura, cap a on va i a quina hora—
// surt del que el parser ha llegit, amb les coordenades en graus per deu milions i els centímetres tal com els dona (docs/50).
static void test_a_couple_of_sentences_make_a_fix() {
  hal::nmea::State state;
  hal::nmea::reset(state);
  say(state, kGga);
  say(state, kRmc);
  say(state, kVtg);
  const hal::GpsFix& fix = state.fix;
  // 48° 07.038′ N són 48,1173° i 011° 31.000′ E són 11,5166°.
  TEST_ASSERT_EQUAL_INT32(481173000, fix.lat_e7);
  TEST_ASSERT_EQUAL_INT32(115166667, fix.lon_e7);
  TEST_ASSERT_EQUAL_UINT8(1, fix.quality);
  TEST_ASSERT_EQUAL_UINT8(8, fix.satellites_used);
  TEST_ASSERT_EQUAL_UINT16(9, fix.hdop_x10);
  TEST_ASSERT_EQUAL_INT32(54540, fix.altitude_cm);
  TEST_ASSERT_EQUAL_INT32(4690, fix.geoid_cm);
  // 22,4 nusos són 1 152 cm/s i el VTG diu 41,5 km/h, que són 1 153: el que mana és l'últim que ha parlat.
  TEST_ASSERT_EQUAL_INT32(1153, fix.speed_cm_s);
  TEST_ASSERT_EQUAL_INT32(844, fix.course_deg10);
  // L'hora del RMC: el 23 de març de 1994 a les 12:35:19, i el segle és el que el receptor no diu (docs/50).
  TEST_ASSERT_TRUE(fix.time.valid);
  TEST_ASSERT_EQUAL_UINT16(1994, fix.time.year);
  TEST_ASSERT_EQUAL_UINT8(3, fix.time.month);
  TEST_ASSERT_EQUAL_UINT8(23, fix.time.day);
  TEST_ASSERT_EQUAL_UINT8(12, fix.time.hour);
  TEST_ASSERT_EQUAL_UINT8(35, fix.time.minute);
  TEST_ASSERT_EQUAL_UINT8(19, fix.time.second);
  TEST_ASSERT_EQUAL_UINT32(3, state.good);
  TEST_ASSERT_EQUAL_UINT32(0, state.bad);
  TEST_ASSERT_TRUE(hal::nmea::heard_a_sentence(state));
}

// **Una frase amb el segell malament no es creu**: un caràcter perdut en un cable llarg dona un número que sembla una latitud, o sigui
// que la frase es compta com a dolenta i el que se sabia es queda com estava (docs/50).
static void test_a_sentence_with_a_broken_checksum_is_not_believed() {
  hal::nmea::State state;
  hal::nmea::reset(state);
  say(state, kGga);
  const int32_t lat = state.fix.lat_e7;
  // La mateixa frase amb el segell canviat: la frase es llegeix i no es creu.
  say(state, "$GNGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*58");
  TEST_ASSERT_EQUAL_UINT32(2, state.sentences);
  TEST_ASSERT_EQUAL_UINT32(1, state.good);
  TEST_ASSERT_EQUAL_UINT32(1, state.bad);
  TEST_ASSERT_EQUAL_INT32(lat, state.fix.lat_e7);
}

// **Una posició de dues dimensions és una posició** (0.192.90): el GSA de tipus 3 —el que dona un receptor amb tres satèl·lits— no té
// altitud i la latitud i la longitud són tan bones com les del 3D; abans aquest parser deia que el 2 i el 3 no eren una posició, o
// sigui que la pàgina ensenyava «no position yet» amb la posició a la mà. I com que el GSA arriba **després** del RMC dins del mateix
// cicle, el que es veia depenia del moment en què es llegís (docs/50).
static void test_a_two_dimensional_fix_is_still_a_position() {
  hal::nmea::State state;
  hal::nmea::reset(state);
  say(state, kRmc);
  TEST_ASSERT_TRUE(state.fix.valid);
  say(state, kGsa3d);
  TEST_ASSERT_TRUE_MESSAGE(state.fix.valid, "the GSA of a two dimensional fix threw the position away");
  // **I un GSA sense cap posició sí que la lleva**: el receptor ha perdut els satèl·lits (docs/50).
  say(state, kGsaNoFix);
  TEST_ASSERT_FALSE(state.fix.valid);
  // I la posició del RMC es queda a la memòria: el que ha canviat és que no és d'ara.
  TEST_ASSERT_EQUAL_INT32(481173000, state.fix.lat_e7);
}

// **El cel i el que compta la posició** (0.192.90): el GSA i el GSV són frases diferents i arriben en moments diferents del cicle. El
// GSA diu quins números compten, el GSV on són; abans el GSV escombrava el cel i **s'enduia les marques**, o sigui que el mapa del cel
// no ensenyava mai quins satèl·lits fan la posició.
static void test_the_sky_keeps_the_marks_of_the_gsa() {
  hal::nmea::State state;
  hal::nmea::reset(state);
  say(state, kGsa3d);
  say(state, kGsvOne);
  say(state, kGsvTwo);
  TEST_ASSERT_EQUAL_UINT8(8, state.sky_count);
  TEST_ASSERT_EQUAL_UINT8(8, state.fix.satellites_seen);
  // Els quatre del GSA (04, 05, 09, 12) són els que compten, i el 12 és al cel de la primera meitat del GSV.
  uint8_t used_in_sky = 0;
  bool twelve = false;
  for (uint8_t i = 0; i < state.sky_count; ++i) {
    if (state.sky[i].used) ++used_in_sky;
    if (state.sky[i].id == 12) twelve = state.sky[i].used;
  }
  // Dels quatre del GSA (04, 05, 09, 12) només el 12 surt al cel d'aquest GSV, o sigui que la marca que ha de sobreviure és una.
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, used_in_sky, "the marks of the GSA did not survive the GSV");
  TEST_ASSERT_TRUE_MESSAGE(twelve, "the satellite 12 of the sky is not marked as used");
  // I el cicle següent amb menys cel: la llista baixa, que és el que abans no passava mai (era un màxim).
  say(state, "$GPGSV,1,1,03,02,17,308,41,05,43,253,39,12,07,344,39*41");
  TEST_ASSERT_EQUAL_UINT8(3, state.sky_count);
  TEST_ASSERT_EQUAL_UINT8(3, state.fix.satellites_seen);
  TEST_ASSERT_EQUAL_UINT8(2, state.sky[0].id);
  TEST_ASSERT_TRUE(state.sky[1].used);  // el 05, que el GSA del cicle abans comptava
}

// **Un cable que arriba a trossos** (0.192.90): the receiver hands its bytes over as they come, i una frase partida entre dues
// lectures és la meitat d'una frase i no dues; el que diu que s'ha sentit alguna cosa és **una frase amb el segell bé**, que és el
// senyal que el driver fa servir per dir «aquest és el ritme bo» (docs/50).
static void test_bytes_one_at_a_time_are_one_sentence() {
  hal::nmea::State state;
  hal::nmea::reset(state);
  const char* sentence = kRmc;
  for (size_t i = 0; i < strlen(sentence); ++i) {
    TEST_ASSERT_FALSE(hal::nmea::heard_a_sentence(state));
    (void)hal::nmea::feed(state, sentence[i]);
  }
  (void)hal::nmea::feed(state, '\r');
  (void)hal::nmea::feed(state, '\n');
  TEST_ASSERT_TRUE(hal::nmea::heard_a_sentence(state));
  TEST_ASSERT_EQUAL_UINT32(1, state.good);
  TEST_ASSERT_TRUE(state.fix.valid);
}

// **I un cable que només fa soroll no ha sentit res**: a un ritme equivocat la UART dona bytes d'un marc trencat —o sigui que un byte
// no vol dir res— i el que el driver ha de saber és si hi ha **una frase**; si no n'hi ha cap, aquest no és el ritme del mòdul
// (0.192.90, docs/50).
static void test_noise_never_says_it_heard_a_sentence() {
  hal::nmea::State state;
  hal::nmea::reset(state);
  // Un flux com el que dona una UART a un ritme que no és el del mòdul: bytes alts i, de tant en tant, un `$` que no vol dir res.
  const uint8_t noise[] = {0xFC, 0x00, 0x7F, '$', 0x13, 0x88, 0x00, '$', 0xFE, 0x01, 0xE0, 0x00};
  for (size_t i = 0; i < sizeof(noise); ++i) {
    (void)hal::nmea::feed(state, static_cast<char>(noise[i]));
  }
  (void)hal::nmea::feed(state, '\n');
  TEST_ASSERT_FALSE_MESSAGE(hal::nmea::heard_a_sentence(state), "a noise stream was taken for a sentence");
  TEST_ASSERT_EQUAL_UINT32(0, state.good);
  TEST_ASSERT_FALSE(state.fix.valid);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_a_couple_of_sentences_make_a_fix);
  RUN_TEST(test_a_sentence_with_a_broken_checksum_is_not_believed);
  RUN_TEST(test_a_two_dimensional_fix_is_still_a_position);
  RUN_TEST(test_the_sky_keeps_the_marks_of_the_gsa);
  RUN_TEST(test_bytes_one_at_a_time_are_one_sentence);
  RUN_TEST(test_noise_never_says_it_heard_a_sentence);
  return UNITY_END();
}
