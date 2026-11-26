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

}  // namespace

bool available() { return g_gps != nullptr; }

bool powered() { return g_on; }

bool turn_on() {
  if (g_gps == nullptr) return false;
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
    const hal::GpsCaps caps = g_gps->caps();
    hal::logf(pal::kLogInfo, "GPS", "the receiver is on: %s at %u bauds", caps.chip, static_cast<unsigned>(caps.baud));
  }
  return true;
}

bool turn_off() {
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
  if (g_gps == nullptr || !g_on) return;
  meter::Scope scope(meter::kGpsPoll);
  hal::GpsFix fresh = g_fix;
  if (g_gps->poll(fresh)) {
    // **Les frases i els errors els compta el driver** (0.192.63): qui llegeix el cable és qui sap què ha arribat i què s'ha
    // perdut, i el nucli només hi afegeix el que sap ell —quant fa de l'última posició (docs/30, docs/50).
    g_fix = fresh;
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
