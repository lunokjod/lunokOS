// **El cel, llegit i cregut** (0.192.63): el perquè és a la capçalera; aquí hi ha el torn que llegeix el receptor, el comptador
// del que costa i les dues decisions que no són del driver —quan s'ha de llegir i si l'hora del cel mana— (docs/30, docs/50).
#include "kernel/gps.h"

#include <math.h>
#include <string.h>

#include "hal/civil.h"
#include "hal/log.h"
#include "kernel/cfg.h"
#include "kernel/meter.h"
#include "pal/pal.h"

#include "luabind/luabind.h"

namespace kernel {
namespace gps {
namespace {

// **El vigilant del cable** (0.192.90, l'amo: *«L'app de GPS fa alguna cosa quan no troba senyal? per exemple, reintentar-ho a cada
// pas amb una alarma o el torn? o el sistema ja s'en ocupa d'anar escoltant?»*). El que el sistema ja fa és **escoltar a cada torn**:
// `poll` llegeix el que la UART tingui i no espera ningú. El que no feia era **tornar a lligar el cable**: si el mòdul no va contestar
// quan es va encendre —el rail encara pujava, el ritme era l'altre, el cable s'havia mogut—, `g_on` es quedava fals per sempre i la
// pàgina deia «the receiver is off» fins a reiniciar; i si contestava i després callava (una baixada del rail, un reset del mòdul), la
// UART es quedava buida i ningú se n'adonava.
//
// Ara: si fa `kSilentMs` que no arriba cap frase i el mòdul és volgut, es tanca la UART i es torna a sondar; i si el mòdul no hi és,
// es reintenta amb una espera que dobla, per no pagar un sondeig de dos segons i mig cada deu segons en una placa sense receptor
// (docs/30, docs/50).
const int64_t kSilentMs = 8000;
const int64_t kRetryFirstMs = 30000;
const int64_t kRetryMaxMs = 480000;

hal::IGps* g_gps = nullptr;
// **El que s'ha llegit l'última vegada**, i si el mòdul és encès (0.192.63). El capejament es recorda aquí i no al driver perquè
// és una decisió del sistema: el mòdul es pot apagar quan la pàgina marxa i el driver no ha de saber qui el mira (docs/50).
hal::GpsFix g_fix;
bool g_on = false;
bool g_begun = false;
// **L'última hora que s'ha cregut i quant s'ha mogut el rellotge** (docs/30): `-1` vol dir mai.
int64_t g_sync_ms = -1;
// **Quan es va veure una posició per última vegada** (0.192.63): és el que fa que una pàgina pugui dir «fa vint segons que no en
// té» en comptes d'ensenyar l'última que va veure com si fos d'ara (docs/30).
int64_t g_last_fix_ms = -1;
int64_t g_shift_ms = 0;
// **El vigilant** (0.192.90): quan es va sentir una frase per última vegada, quan toca tornar a provar i amb quina espera, i si algú
// vol el mòdul (una pàgina l'ha demanat o el rellotge se'l creu). `wanted` no el toca el vigilant: apagar-lo és una decisió d'una
// persona i tornar-lo a encendre sol seria desfer-la (docs/30, docs/50).
bool g_wanted = false;
int64_t g_heard_ms = -1;
uint32_t g_heard_sentences = 0;
int64_t g_next_try_ms = 0;
int64_t g_retry_ms = kRetryFirstMs;
// **Una sincronització per minut com a molt** (0.192.63): the sky says the time every second and the clock only needs to be told
// once; a `set_epoch` is a write to the chip's RTC and to the wall clock, i fer-ho seixanta vegades per minut seria gastar el
// rellotge per res (docs/50, docs/60).
const int64_t kSyncEveryMs = 60000;
// **I el que ha de córrer el rellotge per creure-la**: a fix that says the same second as the clock is not news. Two seconds is
// the resolution of the sentences (docs/50).
const int64_t kSyncThresholdMs = 2000;

int64_t mono_ms() { return static_cast<int64_t>(pal::system().millis()); }

// **El desfasament que el cel proposa**: la longitud és l'hora del sol, i quinze graus són una hora. Un receptor no diu la zona
// horària —NMEA no en porta—, o sigui que el que es pot fer és arrodonir la longitud a l'hora més propera, que és cert a gairebé
// tot el món i no ho és a les zones que un país s'inventa (l'Índia, la Xina, l'Argentina). **Per això es diu al registre**: la
// persona ha de saber que la zona que té és la del sol i no la del seu govern (docs/30, docs/60).
int32_t the_offset_of_the_sun(int32_t lon_e7) {
  const double degrees = static_cast<double>(lon_e7) / 10000000.0;
  int32_t hours = static_cast<int32_t>(lround(degrees / 15.0));
  if (hours > 14) hours = 14;
  if (hours < -14) hours = -14;
  return hours * 60;
}

// **Creure el cel** (0.192.63, l'amo: *«En cas d'activa el GPS, es sincronitzarà el rellotge, tant intern com l'RTC»*). El que
// s'escriu és l'època del rellotge, que és la mateixa que la de la RTC del xip —el HAL la hi desa—, i el desfasament de la zona
// quan la persona ha demanat que el cel també la digui. Les dues coses es diuen al registre amb el número que s'han mogut, perquè
// una placa que salta dos minuts cada minut és una cosa que s'ha de poder veure (docs/30, docs/60).
void believe_the_sky(const hal::GpsFix& fix) {
  if (!fix.time.valid || fix.time.year < 2024) return;
  hal::IClock* clock = hal::platform().clock();
  if (clock == nullptr) return;
  const int64_t mono = mono_ms();
  if (g_sync_ms >= 0 && mono - g_sync_ms < kSyncEveryMs) return;

  const int64_t epoch = hal::civil_to_epoch_ms(fix.time.year, fix.time.month, fix.time.day, fix.time.hour, fix.time.minute,
                                              fix.time.second);
  if (epoch <= 0) return;
  const int64_t now = clock->epoch_ms();
  const int64_t shift = now - epoch;
  // **Una diferència que no val la pena no es toca**: els centèsims i el segon de la frase fan que el rellotge i el cel mai no
  // siguin iguals al mil·lisegon, i escriure la RTC per mig segon és desgastar-la per res (docs/50, docs/60).
  if (now > 0 && shift < kSyncThresholdMs && shift > -kSyncThresholdMs) {
    g_sync_ms = mono;
    g_shift_ms = 0;
    return;
  }
  if (!clock->set_epoch_ms(epoch + static_cast<int64_t>(fix.time.hundredths) * 10)) {
    hal::logf(pal::kLogWarn, "GPS", "the sky says %04u-%02u-%02u %02u:%02u:%02u and the clock did not take it", fix.time.year,
         fix.time.month, fix.time.day, fix.time.hour, fix.time.minute, fix.time.second);
    return;
  }
  g_sync_ms = mono;
  g_shift_ms = (now > 0) ? shift : 0;
  hal::logf(pal::kLogInfo, "GPS", "the clock is the sky's: %04u-%02u-%02u %02u:%02u:%02u UTC (%+lld ms)", fix.time.year,
       fix.time.month, fix.time.day, fix.time.hour, fix.time.minute, fix.time.second,
       static_cast<long long>(g_shift_ms));

  // **I la zona, que el cel no diu**: NMEA no porta cap zona horària, o sigui que el que es fa és la del sol —la longitud
  // arrodonida a l'hora— i es diu que és això i no el que un govern ha decidit (docs/30, docs/60).
  if (!fix.valid) return;
  const int32_t minutes = the_offset_of_the_sun(fix.lon_e7);
  if (minutes == cfg::tz_minutes()) return;
  if (cfg::set_tz_minutes(minutes)) {
    hal::logf(pal::kLogInfo, "GPS", "and the hour of the person is the sun's: longitude %0.4f° is UTC%+d:%02d",
         static_cast<double>(fix.lon_e7) / 10000000.0, static_cast<int>(minutes / 60),
         static_cast<int>((minutes < 0) ? -minutes % 60 : minutes % 60));
  }
}

// **El vigilant, a cada torn** (0.192.90): el que el sistema ja fa és escoltar —aquest `poll` llegeix el que la UART tingui i no espera
// ningú—, i el que faltava era tornar a lligar el cable quan no hi ha res a escoltar. Dues maneres de no tenir-lo lligat: el mòdul no
// va contestar quan es va encendre, o va contestar i després ha callat (docs/30, docs/50).
void watch_the_wire() {
  const int64_t now = mono_ms();
  if (g_on) {
    if (g_heard_ms < 0) g_heard_ms = now;
    // **Un mòdul que parla cada segon i fa vuit que calla no parla**: es tanca la UART i es torna a sondar, i es diu una vegada perquè
    // una línia cada vuit segons en un receptor mort és el registre que l'amo no vol (docs/50).
    if (now - g_heard_ms >= kSilentMs) {
      hal::logf(pal::kLogWarn, "GPS", "the receiver has said nothing for %d ms: looking for it again",
                static_cast<int>(now - g_heard_ms));
      (void)the_wire_gone();
      g_next_try_ms = now;
    }
    return;
  }
  if (!g_wanted) return;
  if (now < g_next_try_ms) return;
  // **Tornar-lo a provar**: si no hi és, l'espera dobla fins a vuit minuts, perquè una placa sense receptor no ha de pagar un sondeig de
  // dos segons i mig cada mig minut (docs/50).
  if (turn_on()) return;
  g_next_try_ms = now + g_retry_ms;
  g_retry_ms = (g_retry_ms * 2 > kRetryMaxMs) ? kRetryMaxMs : g_retry_ms * 2;
}

}  // namespace

bool available() { return g_gps != nullptr; }

bool powered() { return g_on; }

bool turn_on() {
  if (g_gps == nullptr) return false;
  g_wanted = true;
  if (!g_begun) {
    meter::Scope scope(meter::kGpsOn);
    g_begun = g_gps->on();
    if (!g_begun) {
      hal::logf(pal::kLogWarn, "GPS", "the receiver does not answer: %s", g_gps->last_error());
      return false;
    }
  }
  if (!g_on) {
    g_on = true;
    g_heard_ms = mono_ms();
    g_heard_sentences = 0;
    g_retry_ms = kRetryFirstMs;
    g_next_try_ms = 0;
    const hal::GpsCaps caps = g_gps->caps();
    hal::logf(pal::kLogInfo, "GPS", "the receiver is on: %s at %u bauds", caps.chip, static_cast<unsigned>(caps.baud));
  }
  return true;
}

// **Una persona l'ha apagat** (0.192.90): `turn_off` és la porta de Lua i del commutador, o sigui que el vigilant no el torna a
// encendre; el tancament que fa el vigilant és `the_wire_gone`, que deixa `wanted` com estava (docs/30, docs/50).
bool turn_off() {
  if (g_gps == nullptr) return false;
  g_wanted = false;
  return the_wire_gone();
}

// **Tancar el cable i tornar-lo a provar després**: el que fa el vigilant quan el mòdul calla, sense dir que la persona l'hagi apagat
// (docs/50).
bool the_wire_gone() {
  if (g_gps == nullptr) return false;
  const bool was = g_on;
  g_on = false;
  g_begun = false;
  (void)g_gps->off();
  if (was) hal::logf(pal::kLogInfo, "GPS", "the receiver is off: it draws nothing from the rail");
  return was;
}

// **El torn**: llegir el que hi hagi i creure l'hora si la persona ho ha demanat. **No espera res del cel** —si no hi ha cap frase
// nova, el que es queda és el que ja se sabia— i no diu res al registre a cada volta, perquè un receptor que parla cada segon
// ompliria el registre de dues mil línies per hora (docs/30, docs/50).
void poll() {
  if (g_gps == nullptr) return;
  watch_the_wire();
  if (!g_on) return;
  meter::Scope scope(meter::kGpsPoll);
  hal::GpsFix fresh = g_fix;
  if (g_gps->poll(fresh)) {
    // **Les frases i els errors els compta el driver** (0.192.63): qui llegeix el cable és qui sap què ha arribat i què s'ha
    // perdut, i el nucli només hi afegeix el que sap ell —quant fa de l'última posició (docs/30, docs/50).
    g_fix = fresh;
    // **Una frase que ha arribat és el cable lligat**: el comptador el porten les frases i no les preguntes, o sigui que si creix és que
    // el mòdul parla (0.192.90, docs/50).
    if (fresh.sentences != g_heard_sentences) {
      g_heard_sentences = fresh.sentences;
      g_heard_ms = mono_ms();
    }
    if (cfg::gps_sync()) believe_the_sky(g_fix);
  }
  if (g_fix.valid) {
    g_fix.ms_since_a_fix = 0;
    g_last_fix_ms = mono_ms();
  } else if (g_last_fix_ms >= 0) {
    g_fix.ms_since_a_fix = static_cast<uint32_t>(mono_ms() - g_last_fix_ms);
  }
}

bool fix(hal::GpsFix& out) {
  out = g_fix;
  return g_fix.valid;
}

const hal::GpsSatellite* satellites(uint8_t& count) {
  count = 0;
  if (g_gps == nullptr) return nullptr;
  return g_gps->satellites(count);
}

bool caps(hal::GpsCaps& out) {
  if (g_gps == nullptr) return false;
  out = g_gps->caps();
  out.powered = g_on;
  return true;
}

const char* last_error() { return (g_gps != nullptr) ? g_gps->last_error() : "this board does not look at the sky"; }

// **Quan fa, i no quan**: el número que una pàgina ensenya és una edat —«fa vint segons»— i el que el sistema té a mà és una marca del
// rellotge de funcionament, o sigui que la resta es fa aquí i la pàgina no ha de saber quan va arrencar la placa (docs/30, docs/60).
int64_t last_sync_ms() { return (g_sync_ms < 0) ? -1 : (mono_ms() - g_sync_ms); }

int64_t last_shift_ms() { return g_shift_ms; }

bool boot(hal::IPlatform& platform) {
  g_gps = platform.gps();
  // **El que Lua pot veure**: les portes i res més, i la taula absent quan la placa no té receptor —una crida que no hi és no és
  // una crida que menteix (docs/30).
  luabind::GpsOps ops = {};
  if (g_gps != nullptr) {
    ops.available = &available;
    ops.on = &turn_on;
    ops.off = &turn_off;
    ops.powered = &powered;
    ops.fix = &fix;
    ops.satellites = &satellites;
    ops.caps = &caps;
    ops.last_sync = &last_sync_ms;
    ops.last_shift = &last_shift_ms;
    ops.last_error = &last_error;
  }
  luabind::set_gps_ops(ops);
  if (g_gps == nullptr) return false;
  // **I si la persona va dir que el rellotge es creu el cel, el mòdul s'encén a l'arrencada** (0.192.63): una placa que ha de
  // preguntar l'hora no ha d'esperar que algú obri una pàgina (docs/30, docs/60).
  if (cfg::gps_sync()) (void)turn_on();
  return true;
}

}  // namespace gps
}  // namespace kernel
