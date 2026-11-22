// The light of the board: the policy of what it says, and nothing of how it is
// painted (docs/30, docs/50).
#include "kernel/meter.h"
#include "kernel/led.h"

#include <stddef.h>
#include <string.h>

#include "kernel/bluetooth.h"
#include "kernel/cfg.h"
#include "kernel/log.h"
#include "kernel/power.h"
#include "luabind/luabind.h"

namespace kernel {
namespace led {
namespace {

hal::IPlatform* g_platform = nullptr;
hal::ILed* g_led = nullptr;

// **El vidre que s'ha apagat sol** (0.190.118): cert vol dir que el negre que hi ha al vidre és d'aquest mòdul i no de ningú més, o sigui que el
// primer dit només ha d'encendre i no ha de clicar (docs/30, docs/50).
bool g_dimmed = false;
// **Un llum que espera una pàgina** (0.190.161): the boot says it —a return for an alarm has nobody in front— and it goes down the
// moment the board has something to show or a finger arrives (docs/30, docs/60).
bool g_glass_waits = false;
// **El vidre que respira amb el cable posat** (0.190.162): the switch of the Battery tab, the charge the gauge reports and the two moments
// that decide it —when it was last asked and when the breath began—. The breath is the house's own (`EffectKind::kBreath`), o sigui que el
// gest és el mateix que el de l'arrencada i el de la ràdio en una llum d'estat (docs/30, docs/50).
bool g_breathe_on_a_cable = false;
int64_t g_breathe_start = 0;
int64_t g_cable_asked_ms = -1;
const int64_t kCableEveryMs = 20000;   // how often the charger is asked, because a register per turn is a bus nobody else can use
// **I cada quant mentre el vidre respira** (0.190.163, l'amo: *«El breathe ha de parar en quant la placa perdi el cable»*): a breath that goes
// on for twenty seconds after the cable is out is the board saying a lie, o sigui que mentre el respir dura el carregador es pregunta cada
// segon i el que costa és una lectura d'un registre i prou (docs/30, docs/50).
const int64_t kCableWhileBreathingMs = 1000;
// **I el vidre no es toca** (0.190.234): aquest respir és el de la **retroil·luminació** i no el d'un LED, i el seu ritme és el que era —
// quatre segons, que des de la 0.190.234 ja **no és** el de l'arrencada, que en fa quinze—. El gest és el mateix dibuix i la mida del pany
// no: el que l'amo ha demanat és el dels leds (docs/30, docs/50).
const uint32_t kBreathePeriodMs = 4000;

// ---- the app layer -------------------------------------------------------
// The colour an app asked for. It wins over the system while the app is in front.
bool g_has_colour = false;
uint8_t g_red = 0;
uint8_t g_green = 0;
uint8_t g_blue = 0;

// An app that painted the panel light by light owns a **frame** and not a colour.
// The frame itself lives in the board, where the strip is: a copy here would be
// another buffer to keep in step with the one that goes on the wire (docs/50,
// docs/75). The kernel only has to know whose it is and whether it is painted yet.
bool g_painted = false;

// The effect an app is playing, and when it started. An effect is a function of the
// time since it started and nothing else, so there is no queue to fall behind and no
// frame to owe anybody (docs/30).
Effect g_effect;
int64_t g_effect_start = 0;

// A fade to black on the way out. It is the only thing here that is not a function of
// time since start: it starts from the colour the light happened to show (docs/30).
bool g_fading = false;
int64_t g_fade_start = 0;
uint32_t g_fade_ms = 0;
uint8_t g_fade_red = 0;
uint8_t g_fade_green = 0;
uint8_t g_fade_blue = 0;

// ---- the system layer ----------------------------------------------------
// The four effects the board has of its own, the radio's state, the boot's idle breath and the
// moment that is happening.
Effect g_system[kTriggerCount];
bool g_radio = false;
int64_t g_radio_start = 0;
// **El llum de repòs** (0.190.106, l'amo: *«Els leds es poden declarar com a "backlight", així el sistema pot identificar quin led usar-lo per
// il·luminar la pantalla»*): what the board leaves when it has nothing to say. It is black on every board —a light that says nothing is a
// light off— i en una placa amb retroil·luminació és el nivell que una persona ha triat a la pàgina dels ajustos. The effects and the app
// keep winning over it while they last, i en acabar-se la llum torna aquí i no al negre (docs/30, docs/50).
uint8_t g_rest_red = 0;
uint8_t g_rest_green = 0;
uint8_t g_rest_blue = 0;

// The boot's breath is an **idle** effect and not a moment: an app that starts while it is still
// going takes the light without waiting to be allowed and without being told, because it was never
// the app's (docs/30).
bool g_idle = false;
int64_t g_idle_start = 0;
int64_t g_idle_end = 0;
// A moment that is happening: an alarm or an error. It **interrupts** the app (docs/30).
bool g_has_note = false;
Trigger g_note = Trigger::kAlarm;
int64_t g_note_start = 0;
int64_t g_note_end = 0;
// What the app had when the light was taken from it, so it can be given back exactly as it was:
// the colour, whether it was a frame of the app's own, and the effect with the moment it started
// (docs/30).
struct SavedLight {
  bool valid;
  bool has_colour;
  bool painted;
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  Effect effect;
  // Where the app's effect was when the light was taken, so it comes back where it was and not at
  // the end of the interruption: a moment of the system does not move an app's clock (docs/30).
  uint32_t effect_elapsed;
};
SavedLight g_saved;
// Set when a moment ends and the app's light is back. The kernel turns it into `on_light()` on the
// turn after it, which is why it is one flag and not a queue: the app redraws once (docs/30).
bool g_released = false;

// ---- what the light is showing -------------------------------------------
uint8_t g_shown_red = 0;
uint8_t g_shown_green = 0;
uint8_t g_shown_blue = 0;
bool g_dirty = false;

// A frame a third of a hundred times a second: faster than a person reads a light and
// slow enough that the strip's own write (30 us on one chip, 750 us on the panel) is not
// what the board spends its time on (docs/75).
const uint32_t kFrameMs = 33;

// Below this a period is a blur and not a message. It is a floor and not a preference:
// an app that asks for one millisecond gets the shortest light a person can see, and
// saying so is better than a seizure (docs/30).
const uint32_t kMinPeriodMs = 120;

// Five anas and four dits and the gaps between them: 34 dots, which is one round of
// SOS. Alternating on and off, starting on (docs/30).
const uint8_t kSos[] = {1, 1, 1, 1, 1, 3, 3, 1, 3, 1, 3, 3, 1, 1, 1, 1, 1, 7};
const uint32_t kSosUnits = 34;

// How many bounces the ball takes before it is quick enough to start over: twelve is
// more than a person can count and it bounds the loop (docs/30).
const uint32_t kBounceLimit = 12;

int64_t now_ms() {
  if (g_platform == nullptr) return 0;
  hal::IClock* clock = g_platform->clock();
  meter::Scope scope(meter::kClockMono);
  return (clock != nullptr) ? clock->monotonic_ms() : 0;
}

// A period below the floor is a blur and above the ceiling is a wait nobody watches; both
// ends are clamped so a number an app invents cannot overflow the arithmetic of the levels
// (docs/30).
const uint32_t kMaxPeriodMs = 600000;

uint32_t period_of(const Effect& effect) {
  if (effect.period_ms < kMinPeriodMs) return kMinPeriodMs;
  if (effect.period_ms > kMaxPeriodMs) return kMaxPeriodMs;
  return effect.period_ms;
}

// An ease in and out over 0..255, in integers: a breath with corners is a triangle and
// a triangle is not a breath (docs/30). Smoothstep, and it is exact at the ends.
uint32_t ease(uint32_t u) {
  if (u > 255) u = 255;
  return (u * u * (765 - (2 * u))) / (255u * 255u);
}

// One stretch of the sos, in dots.
bool sos_on(uint32_t unit) {
  uint32_t at = 0;
  for (uint32_t i = 0; i < sizeof(kSos); ++i) {
    at += kSos[i];
    if (unit < at) return (i % 2) == 0;
  }
  return false;
}

// How long one round of bounces lasts: each one is three quarters of the one before,
// and the floor stops it (docs/30).
uint32_t bounce_round(uint32_t period) {
  uint32_t p = period;
  uint32_t total = 0;
  for (uint32_t i = 0; i < kBounceLimit && p >= kMinPeriodMs; ++i) {
    total += p;
    p = (p * 3) / 4;
  }
  return (total == 0) ? kMinPeriodMs : total;
}

// One cycle of an effect: a breath, an on and an off, a dot, a whole ball.
uint32_t cycle_length(const Effect& effect) {
  const uint32_t p = period_of(effect);
  switch (effect.kind) {
    case EffectKind::kSos:
      return p * kSosUnits;
    case EffectKind::kBounce:
      return bounce_round(p);
    case EffectKind::kNone:
    case EffectKind::kBreathe:
    case EffectKind::kBlink:
    default:
      return p;
  }
}

uint8_t scale(uint8_t value, uint32_t level) {
  return static_cast<uint8_t>((static_cast<uint32_t>(value) * level) / 255);
}

// One channel of a ramp: from `from` to `to` as `t` goes from 0 to 255. Every effect carries two
// colours and this is what makes them a **ramp** and not two settings: a breath drifts, a bounce
// changes tone as it gets quicker, the sos goes from red to orange along the message (docs/30).
uint8_t blend(uint8_t from, uint8_t to, uint32_t t) {
  if (to >= from) return static_cast<uint8_t>(from + (((to - from) * t) / 255));
  return static_cast<uint8_t>(from - (((from - to) * t) / 255));
}

// Where in its cycle an effect is, from 0 to 255. It is the phase of the ramp (docs/30).
uint32_t cycle_phase(const Effect& effect, uint32_t elapsed) {
  const uint32_t cycle = cycle_length(effect);
  if (cycle == 0) return 0;
  return (elapsed % cycle) * 255 / cycle;
}

// A channel of this board's light. The ceiling is a hardware limit and the policy is what
// enforces it: an app that asks for 255 on the panel of the Matrix gets the maker's number
// and not an LED hot enough to damage the case (docs/30, docs/50).
uint8_t channel(uint8_t value) {
  const uint32_t ceiling = max_value();
  return (value > ceiling) ? static_cast<uint8_t>(ceiling) : value;
}

// What the effect is saying at `elapsed`, from 0 to its own ceiling (docs/30).
uint8_t level_at(const Effect& effect, uint32_t elapsed) {
  const uint32_t p = period_of(effect);
  const uint32_t ceiling = effect.level;
  const uint32_t half = (p / 2 == 0) ? 1 : p / 2;
  switch (effect.kind) {
    case EffectKind::kBreathe: {
      const uint32_t phase = elapsed % p;
      const uint32_t u = (phase < half) ? (phase * 255 / half) : ((p - phase) * 255 / half);
      return scale(static_cast<uint8_t>(ease(u)), ceiling);
    }
    case EffectKind::kBlink:
      return ((elapsed % p) < half) ? static_cast<uint8_t>(ceiling) : 0;
    case EffectKind::kSos: {
      const uint32_t unit = (elapsed % (p * kSosUnits)) / p;
      return sos_on(unit) ? static_cast<uint8_t>(ceiling) : 0;
    }
    case EffectKind::kBounce: {
      uint32_t bounce = p;
      uint32_t left = elapsed % bounce_round(p);
      for (uint32_t i = 0; i < kBounceLimit && bounce >= kMinPeriodMs; ++i) {
        if (left < bounce) {
          // Up and back down inside one bounce, and the next one is shorter: the light is a
          // ball that hits the floor faster and faster, and when it cannot be quicker the
          // round starts over (docs/30).
          const uint32_t half = (bounce / 2 == 0) ? 1 : bounce / 2;
          const uint32_t u = (left < half) ? (left * 255 / half) : ((bounce - left) * 255 / half);
          return scale(static_cast<uint8_t>(ease(u)), ceiling);
        }
        left -= bounce;
        bounce = (bounce * 3) / 4;
      }
      return 0;
    }
    case EffectKind::kNone:
    default:
      return 0;
  }
}

// The two ends of the ramp, blended by where the cycle is: what the effect is **saying** at this
// millisecond, before the level is applied (docs/30).
void shade_at(const Effect& effect, uint32_t elapsed, uint8_t& r, uint8_t& g, uint8_t& b) {
  const uint32_t t = cycle_phase(effect, elapsed);
  r = blend(effect.red, effect.red2, t);
  g = blend(effect.green, effect.green2, t);
  b = blend(effect.blue, effect.blue2, t);
}

// One frame, and only when it says something new: a frame is a write on the wire and
// the light is also a cost (docs/75).
void paint(uint8_t r, uint8_t g, uint8_t b, int32_t pixel) {
  if (g_led == nullptr) return;
  const uint8_t cr = channel(r);
  const uint8_t cg = channel(g);
  const uint8_t cb = channel(b);
  const bool same = cr == g_shown_red && cg == g_shown_green && cb == g_shown_blue;
  if (same && !g_dirty) return;
  // The frame on the wire, which is the only part of the light that costs a device call (docs/75).
  meter::Scope scope(meter::kLedShow);
  if (pixel >= 0) {
    // A strip shows one light and the rest is dark: the frame is the whole strip, so the
    // rest of it has to be cleared or the light of last frame stays on (docs/30).
    g_led->fill(0, 0, 0);
    g_led->pixel(static_cast<uint32_t>(pixel), cr, cg, cb);
  } else {
    g_led->fill(cr, cg, cb);
  }
  g_led->show();
  g_shown_red = cr;
  g_shown_green = cg;
  g_shown_blue = cb;
  g_dirty = false;
}

// The maker's four effects. Dim on purpose —**twenty out of 255**— because a light that says
// something does not have to be the brightest thing on the desk, and every milliamp counts on a
// board that wants to sleep; an app that wants more asks for it, and the board's own ceiling is
// still what stops an LED from cooking (docs/30, docs/50, docs/75). Each one **ramps** to a second
// colour across its cycle, which is what makes a colour say something and not just be a colour.
// The boot is a white breath of four and it **ends**: a light left breathing on a desk all day is
// power spent by nobody (docs/30).
Effect default_effect(Trigger trigger) {
  Effect e;
  e.level = 20;
  switch (trigger) {
    case Trigger::kBoot:
      e.kind = EffectKind::kBreathe;
      e.red = 255;
      e.green = 255;
      e.blue = 255;
      e.red2 = 120;
      e.green2 = 180;
      e.blue2 = 255;
      // **Un respir tres vegades més llarg** (0.190.234, l'amo: *«fes que l'animació dels leds de breath sigui el triple de llarga»*): cinc
      // segons era el ritme d'una persona asseguda i ara en són quinze, o sigui un cicle de quinze segons per volta i quatre voltes —un minut
      // d'arrencada—, i el gest és el mateix: el que canvia és el temps que triga a fer-se (docs/30, docs/50).
      e.period_ms = 15000;
      e.cycles = 4;
      break;
    case Trigger::kRadio:
      // **Un respir blau i no un parpelleig** (0.190.182, l'amo: *«mentre el bluetooth estigui actiu al M5Atom, vull que el led sigui blau i amb
      // la sequencia "breathe", si el bluetooth es para, el led s'apaga»*): the radio working is a **state** and not an event —it lasts as long
      // as the keyboard does— and a state that lasts is said with a breath; the blink is for something that has to be noticed, and a light
      // that blinks for hours is a light nobody looks at any more. **I és el moment de les dues ràdies**: el Wi-Fi i el teclat diuen el mateix
      // amb el mateix dibuix, perquè el que una persona llegeix és «la ràdio treballa» i no quina de les dues (docs/30, docs/50).
      e.kind = EffectKind::kBreathe;
      e.red = 0;
      e.green = 0;
      e.blue = 255;
      e.red2 = 0;
      e.green2 = 55;
      e.blue2 = 170;
      // **I el de la ràdio, també** (0.190.234): tres mil dos-cents eren vuit segons de cicle amb la corba de desacceleració del respir, i
      // ara són nou mil sis-cents —el triple— perquè el gest de la casa és un i no dos (docs/30, docs/50).
      e.period_ms = 9600;
      e.cycles = 0;  // as long as the radio is up
      // **Una mica més que els altres i encara fluix**: aquest llum és l'única cosa que diu que el teclat és engegat —no hi ha pàgina ni
      // consola que ho digui— i vint de dos-cents cinquanta-cinc no es veu en un LED de debò. Quaranta-vuit és una cinquena part de la
      // brillantor i continua sent un llum de senyal i no una làmpada (docs/30, docs/75).
      e.level = 48;
      break;
    case Trigger::kAlarm:
      e.kind = EffectKind::kBounce;
      e.red = 0;
      e.green = 200;
      e.blue = 170;
      e.red2 = 0;
      e.green2 = 255;
      e.blue2 = 60;
      e.period_ms = 700;
      e.cycles = 3;
      break;
    case Trigger::kError:
    default:
      e.kind = EffectKind::kSos;
      e.red = 255;
      e.green = 0;
      e.blue = 0;
      e.red2 = 255;
      e.green2 = 120;
      e.blue2 = 0;
      e.period_ms = 200;
      e.cycles = 2;
      break;
  }
  return e;
}

void defaults() {
  for (uint32_t i = 0; i < kTriggerCount; ++i) {
    g_system[i] = default_effect(static_cast<Trigger>(i));
  }
}

// The layer the light is showing, if any: a moment wins over the radio, the radio wins over the
// boot's idle breath, and the idle breath wins over nothing at all. `elapsed` comes out as the time
// since that layer began, so the effect is painted where it is and not where it would have been
// (docs/30).
//
// Giving the app its light back is **not** done here: `poll` does it before it paints anything, so
// the frame of that turn is the app's and not black (docs/30).
const Effect* system_now(int64_t now, uint32_t& elapsed) {
  // **En una retroil·luminació el sistema no diu res** (0.190.112): it is one line in the one place that decides whether the board is saying
  // something, o sigui que cap camí —el moment, la ràdio, el respir— no pot saltar-se-la (docs/30, docs/50).
  if (g_led != nullptr && g_led->role() == hal::LedRole::kBacklight) return nullptr;
  if (g_has_note) {
    if (now < g_note_end) {
      const Effect& e = g_system[static_cast<uint32_t>(g_note)];
      if (e.kind != EffectKind::kNone) {
        elapsed = static_cast<uint32_t>(now - g_note_start);
        return &e;
      }
    }
  }
  if (g_radio) {
    const Effect& e = g_system[static_cast<uint32_t>(Trigger::kRadio)];
    if (e.kind != EffectKind::kNone) {
      elapsed = static_cast<uint32_t>(now - g_radio_start);
      return &e;
    }
  }
  // **I el Bluetooth és la mateixa ràdio que treballa** (0.190.182): the kernel asks the radio itself instead of the board telling it,
  // o sigui que el `net` no ha de saber que el teclat existeix ni hi ha dos comptadors que puguin desquadrar-se. **Quan el Bluetooth es para
  // aquesta capa desapareix i el llum s'apaga sol**, que és exactament el que l'amo ha demanat: cap crida d'apagada, cap color que quedi (docs/30,
  // docs/50).
  //
  // **I els dos costats compten** (0.190.195, l'amo: *«perquè només salta el led de kRadio si està dev on… kRadio ha de saltar amb bt on/off i
  // prou»*): being a keyboard for somebody and listening to somebody are both the radio working, i el llum no ha de distingir qui dels dos ho
  // va demanar (docs/30).
  if (bt::radio_is_working()) {
    const Effect& e = g_system[static_cast<uint32_t>(Trigger::kRadio)];
    if (e.kind != EffectKind::kNone) {
      elapsed = static_cast<uint32_t>(now - bt::on_since_ms());
      return &e;
    }
  }
  if (g_idle) {
    if (now < g_idle_end) {
      const Effect& e = g_system[static_cast<uint32_t>(Trigger::kBoot)];
      if (e.kind != EffectKind::kNone) {
        elapsed = static_cast<uint32_t>(now - g_idle_start);
        return &e;
      }
    } else {
      g_idle = false;
    }
  }
  return nullptr;
}

// ---- the interpreter's side of the light ---------------------------------
// The interpreter speaks plain numbers (`luabind::LedEffectSpec`) so nobody has to share
// a header for one message, and these are the two translations, in one place (docs/30).

luabind::LedEffectSpec to_spec(const Effect& effect) {
  luabind::LedEffectSpec spec = {};
  spec.kind = static_cast<uint8_t>(effect.kind);
  spec.red = effect.red;
  spec.green = effect.green;
  spec.blue = effect.blue;
  spec.red2 = effect.red2;
  spec.green2 = effect.green2;
  spec.blue2 = effect.blue2;
  spec.period_ms = effect.period_ms;
  spec.cycles = effect.cycles;
  spec.pixel = effect.pixel;
  spec.level = effect.level;
  return spec;
}

Effect to_effect(const luabind::LedEffectSpec& spec) {
  Effect effect;
  effect.kind = static_cast<EffectKind>(spec.kind);
  effect.red = spec.red;
  effect.green = spec.green;
  effect.blue = spec.blue;
  effect.red2 = spec.red2;
  effect.green2 = spec.green2;
  effect.blue2 = spec.blue2;
  effect.period_ms = spec.period_ms;
  effect.cycles = spec.cycles;
  effect.pixel = spec.pixel;
  effect.level = spec.level;
  return effect;
}

bool op_play(const luabind::LedEffectSpec& spec) { return play(to_effect(spec)); }

void op_stop(uint32_t fade_ms) { stop(fade_ms); }

bool op_effect(luabind::LedEffectSpec& out, uint32_t& elapsed_ms) {
  Effect effect;
  if (!current_effect(effect, elapsed_ms)) return false;
  out = to_spec(effect);
  return true;
}

bool op_system_effect(uint8_t trigger, luabind::LedEffectSpec& out) {
  Effect effect;
  if (!system_effect(static_cast<Trigger>(trigger), effect)) return false;
  out = to_spec(effect);
  return true;
}

bool op_set_system_effect(uint8_t trigger, const luabind::LedEffectSpec& spec) {
  return set_system_effect(static_cast<Trigger>(trigger), to_effect(spec));
}

void op_restore_system_effect(uint8_t trigger) {
  restore_system_effect(static_cast<Trigger>(trigger));
}

const char* op_effect_name(uint8_t kind) { return effect_name(static_cast<EffectKind>(kind)); }

bool op_effect_from_name(const char* name, uint8_t& kind) {
  EffectKind found;
  if (!effect_from_name(name, found)) return false;
  kind = static_cast<uint8_t>(found);
  return true;
}

const char* op_trigger_name(uint8_t trigger) { return trigger_name(static_cast<Trigger>(trigger)); }

bool op_trigger_from_name(const char* name, uint8_t& trigger) {
  Trigger found;
  if (!trigger_from_name(name, found)) return false;
  trigger = static_cast<uint8_t>(found);
  return true;
}

}  // namespace

const char* effect_name(EffectKind kind) {
  switch (kind) {
    case EffectKind::kNone:
      return "none";
    case EffectKind::kBreathe:
      return "breathe";
    case EffectKind::kBlink:
      return "blink";
    case EffectKind::kSos:
      return "sos";
    case EffectKind::kBounce:
      return "bounce";
  }
  return nullptr;
}

bool effect_from_name(const char* name, EffectKind& out) {
  if (name == nullptr) return false;
  if (strcmp(name, "none") == 0 || strcmp(name, "off") == 0) {
    out = EffectKind::kNone;
    return true;
  }
  for (uint32_t i = 0; i <= static_cast<uint32_t>(EffectKind::kBounce); ++i) {
    const EffectKind kind = static_cast<EffectKind>(i);
    const char* candidate = effect_name(kind);
    if (candidate != nullptr && strcmp(name, candidate) == 0) {
      out = kind;
      return true;
    }
  }
  return false;
}

const char* trigger_name(Trigger trigger) {
  switch (trigger) {
    case Trigger::kError:
      return "error";
    case Trigger::kAlarm:
      return "alarm";
    case Trigger::kRadio:
      return "radio";
    case Trigger::kBoot:
      return "boot";
  }
  return nullptr;
}

bool trigger_from_name(const char* name, Trigger& out) {
  if (name == nullptr) return false;
  for (uint32_t i = 0; i < kTriggerCount; ++i) {
    const Trigger trigger = static_cast<Trigger>(i);
    if (strcmp(name, trigger_name(trigger)) == 0) {
      out = trigger;
      return true;
    }
  }
  return false;
}

// **El nom del paper de la llum, que el xip de la placa diu** (0.190.106): a board without a light answers `light` and not nothing, perquè
// una pàgina que pregunta ho fa amb una resposta i no amb un nul (docs/30).
const char* role_name() { return (g_led != nullptr) ? hal::led_role_name(g_led->role()) : "light"; }

bool boot(hal::IPlatform& platform, bool the_glass_waits) {
  g_platform = &platform;
  g_led = platform.led();
  g_glass_waits = the_glass_waits;
  g_breathe_on_a_cable = false;
  g_breathe_start = 0;
  g_cable_asked_ms = -1;
  if (the_glass_waits) {
    // **Dit, perquè un vidre fosc que no s'explica és un vidre trencat** (0.190.161, l'amo: *«surt al log l'apagada i l'encesa del
    // backlight?»*): a wake for an alarm has nobody in front, o sigui que la llum espera una pàgina i la línia ho diu (docs/30, docs/50).
    log_markf("LED", "the glass's light waits: this boot came back for an alarm and has nothing to show yet");
  }
  g_has_colour = false;
  g_painted = false;
  g_effect = Effect();
  g_effect_start = 0;
  g_fading = false;
  g_fade_ms = 0;
  g_radio = false;
  g_radio_start = 0;
  g_idle = false;
  g_idle_start = 0;
  g_idle_end = 0;
  g_has_note = false;
  g_saved = SavedLight();
  g_released = false;
  g_dirty = true;
  g_shown_red = 0;
  g_shown_green = 0;
  g_shown_blue = 0;
  defaults();

  // What an app may ask for. Without a light the operations stay absent and led.*
  // answers E_UNSUPPORTED instead of pretending (docs/30).
  luabind::LedOps ops = {};
  if (g_led != nullptr) {
    ops.count = &count;
    ops.max_value = &max_value;
    ops.set_colour = &set_colour;
    ops.set_pixel = &set_pixel;
    ops.clear_colour = &clear_colour;
    ops.has_colour = &has_colour;
    ops.play = &op_play;
    ops.stop = &op_stop;
    ops.effect = &op_effect;
    ops.system_effect = &op_system_effect;
    ops.set_system_effect = &op_set_system_effect;
    ops.restore_system_effect = &op_restore_system_effect;
    ops.effect_name = &op_effect_name;
    ops.effect_from_name = &op_effect_from_name;
    ops.trigger_name = &op_trigger_name;
    ops.trigger_from_name = &op_trigger_from_name;
    // **I què és aquesta llum, dit per la placa** (0.190.106): `led.caps().role` i `.backlight` surten d'aquí (docs/30, docs/50).
    ops.role = &role_name;
  }
  luabind::set_led_ops(ops);

  if (g_led == nullptr) return false;
  // The light starts off: a chip that restarted with a lit light would be saying
  // something the new boot has not decided (docs/50). The white breath of the boot says
  // it on the turn after this one (docs/30).
  // **I en una retroil·luminació aquesta passa no cal** (0.190.112): the driver of the glass's light is born at zero duty, o sigui que el negre
  // ja hi és i el que ha de dir el primer frame és el nivell triat i no dues coses (docs/50, docs/75).
  if (!is_a_backlight()) {
    // One frame of the wire, the same cost as any other (docs/75).
    meter::Scope scope(meter::kLedShow);
    g_led->fill(0, 0, 0);
    g_led->show();
  }
  // **I en una retroil·luminació el respir de l'arrencada no es diu** (0.190.112): the light of the glass does not talk, o sigui que el que
  // hi ha en encendre's és el nivell que una persona ha triat i res més (docs/30, docs/50).
  if (!is_a_backlight()) note(Trigger::kBoot);
  // **I la llum del vidre, si la placa en té una** (0.190.106): the rest is the level a person chose, so the panel comes up lit when the
  // switch says so (docs/30, docs/50). **I no s'encén si aquesta arrencada és una alarma** (0.190.161, l'amo: *«Quan es desperta d'un
  // deepsleep per alarma no s'ha d'encendre el backlight i tornar a dormir»*): the board came back to take a sample and goes down
  // again, o sigui que un llum que s'encén i s'apaga en dos segons és exactament el parpelleig que una alarma no ha de fer.
  apply_the_backlight();
  return true;
}

bool available() { return g_led != nullptr; }

uint32_t count() { return (g_led != nullptr) ? g_led->count() : 0; }

uint32_t max_value() { return (g_led != nullptr) ? g_led->max_value() : 0; }

bool playing() { return g_effect.kind != EffectKind::kNone; }

bool has_colour() { return g_has_colour || playing(); }

// A moment is being said right now. It is what "the light is busy" means, and it is asked of the
// clock and not of the flag, because a flag that says yes and a clock that says it is over are the
// same thing one turn apart (docs/30).
bool busy() { return g_led != nullptr && g_has_note && now_ms() < g_note_end; }

bool released() {
  const bool was = g_released;
  g_released = false;
  return was;
}

void set_state(State state) {
  // **I la ràdio tampoc no parpelleja en una retroil·luminació** (0.190.112): the blink of the radio is the board saying something, and the
  // light of the glass says nothing —el que fa és il·luminar el que hi ha pintat— (docs/30, docs/50).
  const bool radio = (state == State::kRadio) && g_led != nullptr && g_led->role() != hal::LedRole::kBacklight;
  if (radio && !g_radio) g_radio_start = now_ms();
  g_radio = radio;
  g_dirty = true;
}

bool set_colour(uint8_t r, uint8_t g, uint8_t b) {
  if (g_led == nullptr) return false;
  // To touch the light by hand nothing has to be animating it: while the system is saying an alarm
  // or a failure the app waits for its `on_light()` and asks again, and while its own effect plays
  // it stops that first (docs/30).
  if (busy() || playing()) return false;
  // The background —the radio's blink, the boot's breath— is not an animation to wait for: an app
  // that asks for a colour simply says something else, and the boot's breath is a message that has
  // been said and does not come back (docs/30).
  g_idle = false;
  g_has_colour = true;
  // One colour is the whole light again, even if the app had painted a frame
  // before: the last thing it asked for is what it gets (docs/30).
  g_painted = false;
  g_dirty = true;
  g_red = channel(r);
  g_green = channel(g);
  g_blue = channel(b);
  return true;
}

bool set_pixel(uint32_t index, uint8_t r, uint8_t g, uint8_t b) {
  if (g_led == nullptr) return false;
  if (busy() || playing()) return false;
  if (!g_led->pixel(index, channel(r), channel(g), channel(b))) return false;
  g_idle = false;
  g_has_colour = true;
  g_painted = true;
  g_dirty = true;
  return true;
}

void clear_colour() {
  // A board with no light has nothing to give back, and this is the step that has to
  // cost nothing there (docs/30).
  if (g_led == nullptr) return;
  g_has_colour = false;
  g_painted = false;
  g_effect = Effect();
  g_effect_start = 0;
  g_fading = false;
  // The app is gone: there is nothing of it to give back when a moment ends, and nothing to tell
  // (docs/30).
  g_saved = SavedLight();
  g_released = false;
  // Also a painted frame has to be given back: leaving the app's lights on would be
  // a light that outlives its app (docs/75).
  g_dirty = true;
}

bool play(const Effect& effect) {
  if (g_led == nullptr) return false;
  if (effect.kind == EffectKind::kNone) {
    stop(0);
    return true;
  }
  // The light is busy with a moment: an app that wants it back waits for `on_light()` (docs/30).
  if (busy()) return false;
  if (effect.pixel >= 0 && static_cast<uint32_t>(effect.pixel) >= g_led->count()) {
    return false;
  }
  g_effect = effect;
  // An effect whose second colour was not set is a light of one colour and not a ramp to black:
  // the ramp is between two ends and the plain case is both ends equal (docs/30).
  if (effect.red2 == 0 && effect.green2 == 0 && effect.blue2 == 0) {
    g_effect.red2 = effect.red;
    g_effect.green2 = effect.green;
    g_effect.blue2 = effect.blue;
  }
  g_effect.period_ms = period_of(effect);
  g_effect_start = now_ms();
  g_idle = false;
  g_has_colour = false;
  g_painted = false;
  g_fading = false;
  g_dirty = true;
  return true;
}

bool take(uint8_t r, uint8_t g, uint8_t b) {
  if (g_led == nullptr) return false;
  // A person outranks a machine: whatever was being said stops, nothing is put back and nothing is
  // announced, because the person is looking at the light (docs/30).
  g_has_note = false;
  g_idle = false;
  g_saved = SavedLight();
  g_released = false;
  g_effect = Effect();
  g_effect_start = 0;
  g_fading = false;
  g_has_colour = true;
  g_painted = false;
  g_red = channel(r);
  g_green = channel(g);
  g_blue = channel(b);
  g_dirty = true;
  return true;
}

void stop(uint32_t fade_ms) {
  if (g_led == nullptr) return;
  g_effect = Effect();
  g_effect_start = 0;
  if (fade_ms > 0) {
    g_fading = true;
    g_fade_start = now_ms();
    g_fade_ms = fade_ms;
    g_fade_red = g_shown_red;
    g_fade_green = g_shown_green;
    g_fade_blue = g_shown_blue;
    g_dirty = true;
    return;
  }
  g_fading = false;
  g_fade_ms = 0;
  // Giving it back is giving it back: the app's colour goes too, and whatever the system
  // is saying shows again (docs/30).
  g_has_colour = false;
  g_painted = false;
  g_dirty = true;
}

bool current_effect(Effect& out, uint32_t& elapsed_ms) {
  if (g_effect.kind == EffectKind::kNone) return false;
  out = g_effect;
  elapsed_ms = static_cast<uint32_t>(now_ms() - g_effect_start);
  return true;
}

bool system_effect(Trigger trigger, Effect& out) {
  if (static_cast<uint32_t>(trigger) >= kTriggerCount) return false;
  out = g_system[static_cast<uint32_t>(trigger)];
  return true;
}

bool set_system_effect(Trigger trigger, const Effect& effect) {
  if (static_cast<uint32_t>(trigger) >= kTriggerCount) return false;
  Effect copy = effect;
  copy.period_ms = period_of(effect);
  // Same rule as `play`: one colour unless a second one is given (docs/30).
  if (copy.red2 == 0 && copy.green2 == 0 && copy.blue2 == 0) {
    copy.red2 = copy.red;
    copy.green2 = copy.green;
    copy.blue2 = copy.blue;
  }
  // A light of the strip that is not there is not a light: the whole light instead of a
  // frame that paints nothing (docs/30).
  if (copy.pixel >= 0 && (g_led == nullptr || static_cast<uint32_t>(copy.pixel) >= g_led->count())) {
    copy.pixel = -1;
  }
  g_system[static_cast<uint32_t>(trigger)] = copy;
  return true;
}

void restore_system_effect(Trigger trigger) {
  if (static_cast<uint32_t>(trigger) >= kTriggerCount) return;
  g_system[static_cast<uint32_t>(trigger)] = default_effect(trigger);
}

void note(Trigger trigger) {
  if (g_led == nullptr) return;
  // **Una llum de fons no diu coses** (0.190.112, l'amo: *«Al iniciar una placa amb led es fa lo del SOS per errors i la respiració... No la
  // facis en aquesta placa, al declarar-se backlight no es toca a primeres»*). A light the board declares as a **backlight** is the glass's
  // illumination: the breath of the boot and the SOS of a failure are a **language** and this one has no language to speak with —it would
  // light the whole screen to say something nobody asked to read—. O sigui que cap moment del sistema la toca: ni el respir, ni l'SOS, ni la
  // ràdio, ni l'alarma. El que sí que la governa és el nivell que una persona tria (*Use backlight*) i, si una app en demana un color, el color
  // (docs/30, docs/50).
  if (g_led->role() == hal::LedRole::kBacklight) return;
  if (static_cast<uint32_t>(trigger) >= kTriggerCount) return;
  const Effect& e = g_system[static_cast<uint32_t>(trigger)];
  if (e.kind == EffectKind::kNone) return;
  const int64_t now = now_ms();
  // A moment always ends, so a system effect with no count says its piece once: a light that stays
  // on for ever is a light nobody asked for (docs/30, docs/75).
  const uint32_t rounds = (e.cycles == 0) ? 1 : e.cycles;
  const int64_t span = static_cast<int64_t>(cycle_length(e)) * rounds;

  // The boot is an **idle** effect and not a moment: it takes the light from nobody and an app that
  // comes along replaces it without waiting or being told (docs/30).
  if (trigger == Trigger::kBoot) {
    g_idle = true;
    g_idle_start = now;
    g_idle_end = now + span;
    g_dirty = true;
    return;
  }

  // A moment that is running is only interrupted by one that says more: an alarm does not talk
  // over a failure, and the light that is already busy does not save itself twice (docs/30).
  if (g_has_note && now < g_note_end &&
      static_cast<uint32_t>(trigger) > static_cast<uint32_t>(g_note)) {
    return;
  }

  // The interruption: what the app had is written down and cleared, so the moment is seen alone and
  // can be given back exactly as it was (docs/30).
  if (!g_saved.valid && (g_has_colour || g_effect.kind != EffectKind::kNone)) {
    g_saved.valid = true;
    g_saved.has_colour = g_has_colour;
    g_saved.painted = g_painted;
    g_saved.red = g_red;
    g_saved.green = g_green;
    g_saved.blue = g_blue;
    g_saved.effect = g_effect;
    g_saved.effect_elapsed =
        (g_effect.kind != EffectKind::kNone) ? static_cast<uint32_t>(now - g_effect_start) : 0;
  }
  g_has_colour = false;
  g_painted = false;
  g_effect = Effect();
  g_effect_start = 0;
  g_fading = false;
  g_has_note = true;
  g_note = trigger;
  g_note_start = now;
  g_note_end = now + span;
  g_dirty = true;
}

int32_t next_frame_ms() {
  if (g_led == nullptr) return -1;
  if (g_effect.kind != EffectKind::kNone || g_fading) return static_cast<int32_t>(kFrameMs);
  if (g_has_colour) return g_dirty ? 0 : -1;
  if (g_has_note || g_radio || g_idle || g_breathe_on_a_cable) return static_cast<int32_t>(kFrameMs);
  return -1;
}

bool animating() {
  if (g_led == nullptr) return false;
  if (g_effect.kind != EffectKind::kNone) return true;
  if (g_fading) return true;
  if (g_painted) return false;  // the app's own frame is still, and it is the app's
  if (g_has_colour) return false;
  return g_has_note || g_radio || g_idle || g_breathe_on_a_cable;
}

// A moment that ended hands the app its light back, exactly as it was and with its effect where it
// had got to: the interruption is the system's business and it does not move the app's clock
// (docs/30). The flag is the cue for `on_light()`.
void restore_saved(int64_t now) {
  if (!g_saved.valid) return;
  g_has_colour = g_saved.has_colour;
  g_painted = g_saved.painted;
  g_red = g_saved.red;
  g_green = g_saved.green;
  g_blue = g_saved.blue;
  g_effect = g_saved.effect;
  g_effect_start = now - static_cast<int64_t>(g_saved.effect_elapsed);
  g_saved = SavedLight();
  g_released = true;
  g_dirty = true;
}

// **I el vidre torna on era quan el respir s'acaba** (0.190.164, l'amo: *«Al acabar el breathe, si l'usuari te backlight, anar al seu nivell de
// llum escollit. Si no, apagues i llestos»*): the level a person chose or black, o sigui que el repòs es torna a calcular i es pinta aquí mateix
// —el darrer frame del respir és una llum qualsevol i no el que la placa diu quan no diu res— (docs/30, docs/50).
bool no_one_is_talking();  // the app's colour, an effect, a fade, a note, the radio or a moment keep the light

void back_to_the_rest() {
  apply_the_backlight();
  // **I si l'app té la llum, no se li treu**: the breath was the system talking and it has stopped, o sigui que el que mana és el que hi havia
  // abans del cable i el repòs només s'hi posa quan no hi ha ningú més (docs/30).
  if (!no_one_is_talking()) return;
  paint(g_rest_red, g_rest_green, g_rest_blue, -1);
}

// **El cable, preguntat pel seu compte** (0.190.162): only on a board whose light is the glass's, with the switch on **i amb el vidre il·luminat**,
// o sigui que una placa sense retroil·luminació no toca el bus per res, un commutador apagat tampoc i un vidre que la persona ha deixat a zero encara
// menys: un respir que encén el que ella ha apagat és el sistema fent el contrari del que li han dit (docs/30, docs/50).
void ask_the_cable(int64_t now) {
  if (!is_a_backlight() || !cfg::breathe_while_charging() || !cfg::backlight()) {
    if (g_breathe_on_a_cable) {
      g_breathe_on_a_cable = false;
      g_dirty = true;
      back_to_the_rest();
      log_markf("LED", "the glass stops breathing: the light of the glass is off");
    }
    return;
  }
  // **Un vidre que espera una pàgina no respira**: the alarm woke the board and there is nothing to show, o sigui que el cable espera que el
  // vidre sigui el de la persona (docs/30, docs/60).
  if (g_glass_waits || g_dimmed) return;
  const int64_t every = g_breathe_on_a_cable ? kCableWhileBreathingMs : kCableEveryMs;
  if (g_cable_asked_ms >= 0 && (now - g_cable_asked_ms) < every) return;
  g_cable_asked_ms = now;
  int mv = 0;
  int pct = 0;
  bool charging = false;
  const bool known = power::battery(mv, pct, charging);
  const bool on_a_cable = known && charging;
  if (on_a_cable == g_breathe_on_a_cable) return;
  g_breathe_on_a_cable = on_a_cable;
  g_breathe_start = now;
  g_dirty = true;
  log_markf("LED", on_a_cable ? "the glass breathes: the cell is on a cable" : "the glass stops breathing: no current into the cell");
  // **I el nivell torna de seguida**: sinó el vidre es quedaria amb el mig del respir, que no és ni el nivell ni el negre (docs/30).
  if (!on_a_cable) back_to_the_rest();
}

void poll() {
  if (g_led == nullptr) return;
  // **I el llum del vidre, que s'apaga sol** (0.190.118): the check is a comparison and it only paints on the turn it changes, o sigui que
  // el preu de tenir-ho encès és una resta per torn i dos fotogrames per minut (docs/30, docs/50).
  // **Qui pregunta pel cable, i cada quant** (0.190.162): a register of the charger is a call on the bus the touch panel and the clock also
  // use, o sigui que es pregunta cada vint segons i no a cada torn; la resposta només pinta el torn que canvia (docs/30, docs/50).
  ask_the_cable(now_ms());
  dim_when_idle();
  // **Mentre el negre és meu, cap altra cosa pinta** (0.190.118): the turn leaves without touching the glass, i el torn que hi hagi una
  // pàgina que anima, un efecte o una nota el desfà `dim_when_idle` abans d'arribar aquí (docs/30, docs/50).
  if (g_dimmed) return;
  const int64_t now = now_ms();

  // A moment that is over gives the light back **before** anything is painted, so the frame of this
  // turn is the app's and not black (docs/30).
  if (g_has_note && now >= g_note_end) {
    g_has_note = false;
    restore_saved(now);
  }

  // The app's effect first: it is the app's while it lasts (docs/30).
  if (g_effect.kind != EffectKind::kNone) {
    const uint32_t elapsed = static_cast<uint32_t>(now - g_effect_start);
    const uint32_t cycle = cycle_length(g_effect);
    const bool done =
        (g_effect.cycles > 0) &&
        (static_cast<uint64_t>(elapsed) >= (static_cast<uint64_t>(cycle) * g_effect.cycles));
    if (!done) {
      const uint8_t level = level_at(g_effect, elapsed);
      uint8_t r = 0;
      uint8_t g = 0;
      uint8_t b = 0;
      shade_at(g_effect, elapsed, r, g, b);
      paint(scale(r, level), scale(g, level), scale(b, level), g_effect.pixel);
      return;
    }
    g_effect = Effect();
    g_effect_start = 0;
    g_dirty = true;
  }

  // A fade is the app's last word, and it ends in black and then hands the light back
  // (docs/30).
  if (g_fading) {
    const uint32_t elapsed = static_cast<uint32_t>(now - g_fade_start);
    if (g_fade_ms > 0 && elapsed < g_fade_ms) {
      const uint32_t level = 255 - (elapsed * 255 / g_fade_ms);
      paint(scale(g_fade_red, level), scale(g_fade_green, level), scale(g_fade_blue, level), -1);
      return;
    }
    g_fading = false;
    g_fade_ms = 0;
    g_dirty = true;
  }

  // A frame the app painted light by light goes out when it changed, and the system's
  // colours do not overwrite it while the app is in front (docs/30).
  if (g_has_colour && g_painted) {
    if (!g_dirty) return;
    g_led->show();
    g_dirty = false;
    return;
  }

  // The app's colour, if it asked for one (docs/30).
  if (g_has_colour) {
    paint(g_red, g_green, g_blue, -1);
    return;
  }

  // **I el cable, que el fa respirar** (0.190.162, l'amo: *«vull el efecte "breathe" al backight fins que deixa de carregar»*): the light of the
  // glass goes up and down while the charger says there is current into the cell, amb el nivell que la persona va triar, i s'atura quan el
  // cable marxa o la cel·la diu que ja està plena —el carregador posa el seu estat a `done` i el respir s'acaba igual— (docs/30, docs/50).
  if (g_breathe_on_a_cable) {
    Effect breath;
    breath.kind = EffectKind::kBreathe;
    breath.red = 255;
    breath.green = 255;
    breath.blue = 255;
    breath.level = cfg::backlight_level();
    breath.pixel = -1;
    breath.period_ms = kBreathePeriodMs;
    breath.cycles = 0;  // no end: the cable is what ends it
    const uint8_t level = level_at(breath, static_cast<uint32_t>(now - g_breathe_start));
    paint(level, level, level, -1);
    return;
  }

  // What the board is doing says the rest (docs/30).
  uint32_t elapsed = 0;
  const Effect* e = system_now(now, elapsed);
  if (e == nullptr) {
    paint(g_rest_red, g_rest_green, g_rest_blue, -1);
    return;
  }
  const uint8_t level = level_at(*e, elapsed);
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  shade_at(*e, elapsed, r, g, b);
  paint(scale(r, level), scale(g, level), scale(b, level), e->pixel);
}

// **Què és la llum d'aquesta placa, i què ha de fer el repòs** (0.190.106): the board answers the role and the setting answers the level; a
// board whose light is a light of the board never comes through here and keeps its black (docs/30, docs/50).
bool is_a_backlight() { return g_led != nullptr && g_led->role() == hal::LedRole::kBacklight; }

// **Un minut de quietud, i la llum baixa** (0.190.118): the second is the system's and the switch is the person's. The quietness is
// `power::quiet_ms()` —el comptador únic d'aquesta casa, el mateix que fa saltar el bloqueig i el son—, o sigui que **qualsevol** cosa que
// compti com una persona (un dit, una tecla, un byte de consola) torna a encendre el vidre sense haver-hi de dir res més (docs/30, docs/60).
const int64_t kDimAfterMs = 60000;

bool is_dimmed() { return g_dimmed; }

// **Un vidre que espera no és un vidre encès**: the panel is dark because this boot has nothing to show yet and not because the
// quiet took the light, i el tacte del panell no ha de córrer per un vidre que no il·lumina res (docs/30, docs/50).
bool glass_is_lit() {
  return is_a_backlight() && cfg::backlight() && cfg::backlight_level() > 0 && !g_dimmed && !g_glass_waits;
}

bool the_glass_waits() { return g_glass_waits; }

void the_glass_is_wanted() {
  if (!g_glass_waits) return;
  g_glass_waits = false;
  // **I la quietud no té res a dir aquí**: the light was waiting and not dimmed, o sigui que el negre del comptador tampoc no hi és
  // (un comptador que hi fos voldria dir que la placa s'ha estat un minut amb una pàgina i ningú, i aleshores el que mana és ell).
  g_dimmed = false;
  apply_the_backlight();
  log_markf("LED", "the glass's light is back at %u: the board has a page to show",
            static_cast<unsigned>(g_rest_red));
}

// **El negre passa pel comptable del que s'ha pintat** (0.190.122, l'amo: *«No veig que el backlight es recuperi al tornar a tocar la pantalla»*).
// Això pintava el negre pel darrere —`fill` i `show` a pèl— i deixava `g_shown` dient el nivell d'abans: en tornar, `paint` veia el mateix color que
// creia que hi havia, no veia res a fer i **no encenia res**, fins que una altra cosa del sistema posava el flamenc de brut —i això és el que trigava—.
// El nivell triat continua sense tocar-se: el que es pinta és un zero que el comptable apunta (docs/30, docs/75).
void paint_the_dark() { paint(0, 0, 0, -1); }

// **La llum és meva només quan ningú no la vol** (0.190.106): an app's colour, an effect, a fade, a note, the radio or a moment of the system
// keep the light, i el que fa això és apartar-se'n —i deixar-la com era— fins que tornin a manar ells (docs/30).
bool no_one_is_talking() {
  return !g_has_colour && g_effect.kind == EffectKind::kNone && !g_fading && !g_has_note && !g_radio && !g_idle;
}

void dim_when_idle() {
  if (g_led == nullptr) return;
  const bool it_is_mine = is_a_backlight() && cfg::backlight();
  const int64_t quiet = power::quiet_ms();
  // **I el cable guanya al comptador** (0.190.162): a board that is charging is a board saying something, o sigui que el minut de quietud no
  // apaga el respir que la persona ha demanat (docs/30, docs/50).
  const bool dark =
      it_is_mine && cfg::dim_when_idle() && no_one_is_talking() && !g_breathe_on_a_cable && quiet >= kDimAfterMs;
  if (dark == g_dimmed) return;
  g_dimmed = dark;
  if (g_dimmed) {
    paint_the_dark();
    // **L'apagada, dita** (0.190.122): és la resposta a «surt al log l'apagada i l'encesa del backlight?» —no hi sortia— i el número que fa falta per
    // saber si el minut és el que una persona creu. Com les línies dels ajustos, és una marca i no una traça: no la filtra el nivell (docs/30, docs/75).
    log_markf("LED", "the glass's light is out: %lld s with nobody on the board", static_cast<long long>(quiet / 1000));
    return;
  }
  // **Tornar a encendre és tornar al nivell que la persona va triar**, que el negre no havia tocat (docs/30).
  apply_the_backlight();
  log_markf("LED", "the glass's light is back at %u: the board is in use again", static_cast<unsigned>(g_rest_red));
}

bool wake_the_light() {
  // **Un vidre que espera s'encén amb el primer dit i el dit no prem res** (0.190.161): the same rule as the glass the quiet took,
  // perquè la persona que arriba a una placa que s'ha despertat per una alarma es troba el mateix vidre fosc (docs/30, docs/50).
  if (g_glass_waits) {
    the_glass_is_wanted();
    return true;
  }
  if (!g_dimmed) return false;
  g_dimmed = false;
  apply_the_backlight();
  // **I el dit que l'ha encès, dit**: la línia d'abans diu l'hora de l'apagada i aquesta el que l'ha desfeta, que és el parell que es busca quan
  // una persona diu que la llum no torna (docs/30, docs/50).
  log_markf("LED", "the glass's light is back at %u: a finger on the glass", static_cast<unsigned>(g_rest_red));
  return true;
}

void apply_the_backlight() {
  if (g_led == nullptr) return;
  // **I una llum d'estat no s'encén per un ajust que no és seu**: a board that has a light and no backlight keeps the black of always, i el
  // commutador de la pàgina no li fa res (0.190.17, docs/30).
  const uint8_t level =
      (is_a_backlight() && cfg::backlight() && !g_glass_waits) ? cfg::backlight_level() : 0;
  g_rest_red = level;
  g_rest_green = level;
  g_rest_blue = level;
  // **Si el llum ja diu una altra cosa, no se li treu** (0.190.106): an effect that is playing, a moment of the system, a fade or a colour an
  // app has taken keep the light; the new rest is what it goes back to, i el proper `poll` la hi deixa (docs/30).
  const bool silent = !g_has_colour && g_effect.kind == EffectKind::kNone && !g_fading && !g_has_note && !g_radio && !g_idle;
  if (!silent) {
    g_dirty = true;
    return;
  }
  {
    meter::Scope scope(meter::kLedShow);
    paint(g_rest_red, g_rest_green, g_rest_blue, -1);
  }
}

void current(uint8_t& r, uint8_t& g, uint8_t& b) {
  r = g_shown_red;
  g = g_shown_green;
  b = g_shown_blue;
}

}  // namespace led
}  // namespace kernel
