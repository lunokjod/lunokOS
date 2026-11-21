// The sleep of the ESP32: the RTC timer, the two depths, and the frequency of the
// processor.
#include <cstdio>

#include <esp_sleep.h>
#include <esp_timer.h>

#include <driver/gpio.h>
#include <hal/gpio_ll.h>
#include <soc/gpio_struct.h>

// The core's own speed control: it is the one that also moves the APB and the
// FreeRTOS tick when the frequency changes (read on disk, docs/50).
// **I sense el nucli d'Arduino la porta és la de l'IDF** (0.190.90): `rtc_clk_cpu_freq_*` és exactament el que fa servir el nucli per
// dins, o sigui que el comportament és el mateix i el que canvia és qui ho diu (docs/50).
#if defined(ARDUINO)
#include <esp32-hal-cpu.h>
#else
#include <soc/rtc.h>
#endif

#include "hal/log.h"
#include "esp32/clock.h"
#include "esp32/power.h"

namespace hal {

Esp32Power::Esp32Power(int wake_pin, int second_wake_pin, int glass_pin, int more_pin)
    : wake_pin_(wake_pin), second_wake_pin_(second_wake_pin), glass_pin_(glass_pin), more_pin_(more_pin) {}

namespace {

// **El xip ha refusat la becaina, i quantes vegades** (0.190.69): a light sleep that the chip does not take is a call that returns
// at once and leaves nothing slept. Asking once and finding out is information; asking on every turn of a board that has said no is
// a system paying for a question whose answer is known. El comptador és del run i el que fa és deixar de preguntar després de tres
// respostes iguals, dient-ho una vegada (docs/50, docs/95).
uint8_t g_nap_refusals = 0;
bool g_nap_refused = false;
const uint8_t kNapRefusalsMost = 3;

// **Si el vidre ha despertat mai una becaina** (0.190.72): no es declara, es veu. El xip torna d'un son lleuger dient per què, i si el
// que l'ha tornat és el GPIO del panell amb la línia del dit baixa, aleshores aquesta línia **és** una porta i una espera llarga ja no
// deixa el tàctil mort. Fins que no s'ha vist això, la resposta és que no i el nucli no dorma més que una engruna (docs/50, docs/95).
bool g_glass_is_a_door = false;

}  // namespace

// **Dues portes alhora, quan el xip en sap** (0.190.29): `ext1` on a chip after the ESP32 has an «any of them low»
// mode, and the two lines a board like the T5 E-Paper S3 Pro wires to it are exactly that —a button to ground and the
// open-drain INT of a clock chip—, so one call arms both. **On the ESP32 that mode does not exist**: its two are «all
// low», which would ask for the key *and* the alarm at once, and «any high», which is the idle state of both lines;
// o sigui que allà es queda la porta d'`ext0` i la segona no s'arma (docs/50, docs/60).
bool Esp32Power::arm_the_doors() {
#if !defined(CONFIG_IDF_TARGET_ESP32) || !CONFIG_IDF_TARGET_ESP32
  if (wake_pin_ >= 0 && second_wake_pin_ >= 0 && wake_pin_ < 32 && second_wake_pin_ < 32) {
    const uint64_t mask = (1ULL << wake_pin_) | (1ULL << second_wake_pin_);
    if (esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW) == ESP_OK) return true;
    hal::logf(pal::kLogWarn, "POWER", "the chip did not take the two doors (%d and %d); the key alone is armed",
              wake_pin_, second_wake_pin_);
  }
#endif
  if (wake_pin_ < 0) return false;
  return esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(wake_pin_), 0) == ESP_OK;
}

// **La becaina** (0.190.63, l'amo: *«[X] Light sleep instead of wait. Aplica lightSleep allá on puguis»*): the chip stops its clocks
// and the RTC brings it back with a timer of `max_ms` —o abans, si una tecla, el dit o la consola el desperten—, i el que hi ha a la
// RAM és el mateix en tornar perquè un son lleuger no és un reset. És el que fa servir el sistema en lloc d'esperar (docs/50, docs/60,
// docs/95).
//
// **Cada porta s'ha d'armar aquí i desarmar-se en sortir**: l'IDF guarda els despertars en una estructura estàtica que el son profund
// també llegeix, o sigui que una becaina que deixi la consola armada acabaria despertant un son profund per una tecla que ja no és
// (docs/50).
bool Esp32Power::can_nap() const {
  // Una tecla o el vidre: una placa sense cap de les dues només tindria el temporitzador i la consola, i el que s'hi guanya no paga
  // el que costa perdre-hi una tecla (docs/95).
  //
  // **I un xip que ha dit que no ja no en pren en aquest run** (0.190.70): el nucli fa servir aquesta resposta per decidir si la volta pot
  // esperar la becaina o si ha de mirar la mà cada deu mil·lisegons, o sigui que dir que sí a una placa que refusa el son seria deixar una
  // tecla sense llegir fins a un segon (docs/50, docs/30).
  return (wake_pin_ >= 0 || glass_pin_ >= 0) && !g_nap_refused;
}

// **Qui ha despertat la becaina, en una paraula** (0.190.64): el temporitzador és el cas normal i no diu res, però una becaina
// que s'ha acabat abans d'hora és exactament el que una persona vol llegir quan el toc li sembla lent o quan una tecla no ha
// arribat —o sigui que la línia diu quina porta s'ha obert i quants mil·lisegons ha durat (docs/50, docs/95).
const char* nap_cause_word(esp_sleep_wakeup_cause_t cause) {
  switch (cause) {
    case ESP_SLEEP_WAKEUP_TIMER:
      return "its own timer";
    case ESP_SLEEP_WAKEUP_GPIO:
      return "a key or the glass";
    case ESP_SLEEP_WAKEUP_UART:
      return "the console";
    default:
      return "something else";
  }
}

bool Esp32Power::nap(int32_t max_ms) {
  // Per sota d'aquest temps, entrar i sortir del son costa més que esperar despert.
  const int32_t kLeastMs = 20;
  if (max_ms < kLeastMs || !can_nap()) return false;
  // **Una placa que ha dit que no no es torna a preguntar** (0.190.69, el registre del 20/11: *«el xip no ha pres la becaina (259)»*):
  // 259 és `ESP_ERR_SLEEP_REJECT`, i el que fa és que el sistema pagui una crida que no dormeix a cada volta (docs/50, docs/95).
  if (g_nap_refused) return false;

  // **I un dit que ja és a sobre no espera un segon** (0.190.70): si la línia del vidre ja és baixa, no s'arma —no pot despertar res que
  // ja és despert— i el que torna el xip és el temporitzador; o sigui que la becaina s'escurça perquè el contacte es llegeixi sovint i el
  // que una persona nota no sigui un dit que va a salts (docs/50, docs/30).
  const int32_t kContactMs = 50;
  if (glass_pin_ >= 0 && gpio_get_level(static_cast<gpio_num_t>(glass_pin_)) == 0 && max_ms > kContactMs) {
    max_ms = kContactMs;
  }

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(max_ms) * 1000ULL);

  // **Les tecles i el vidre, pel nivell**: en un son lleuger el `gpio_wakeup_enable` val per a qualsevol GPIO i no només pels del
  // domini RTC, que és el que fa possible escoltar la línia del tàctil (docs/50).
  //
  // **I només s'arma una porta que ara és tancada** (0.190.69): a level door that is already low makes the chip **refuse** the whole
  // sleep (`ESP_ERR_SLEEP_REJECT`), and a touch line left low by the contact that has just ended —or a key still held— would turn
  // every nap of the run into a refusal. La porta s'arma quan es pot obrir, que és quan és tancada; la que està oberta ja ha despertat
  // el sistema i la propera volta la trobarà (docs/50).
  int lines[4] = {wake_pin_, second_wake_pin_, glass_pin_, more_pin_};
  int armed[4] = {-1, -1, -1, -1};
  // **El tipus d'interrupció que la línia tenia abans** (0.190.74): `gpio_wakeup_enable` escriu el `int_type` del pin —l'arma pel **nivell**— i
  // `gpio_wakeup_disable` no el torna enrere. El que hi havia era una vora de baixada, o sigui que després de la primera becaina la línia del
  // panell deixava de ser una vora: el tàctil del Paper va quedar mort dues vegades per això, i el que es desa aquí és el que es torna a posar
  // en sortir (docs/50, docs/95).
  gpio_dev_t* hw = &GPIO;
  int saved_type[4] = {-1, -1, -1, -1};
  bool a_button = false;
  for (int i = 0; i < 4; ++i) {
    if (lines[i] < 0) continue;
    if (gpio_get_level(static_cast<gpio_num_t>(lines[i])) == 0) continue;
    saved_type[i] = static_cast<int>(hw->pin[lines[i]].int_type);
    if (gpio_wakeup_enable(static_cast<gpio_num_t>(lines[i]), GPIO_INTR_LOW_LEVEL) != ESP_OK) {
      saved_type[i] = -1;
      continue;
    }
    armed[i] = lines[i];
    a_button = true;
  }
  if (a_button) esp_sleep_enable_gpio_wakeup();

  // **I la consola**, que és la porta que no es pot perdre: aquesta placa no té teclat i el prompt és l'única manera de parlar-hi. Si
  // el xip no la pot prendre, no es dorm: una becaina que es menja el que una persona escriu és pitjor que no estalviar res (docs/50).
  // **I només si la persona la vol** (0.190.147): with the switch off the console is not armed at all and the wait is a sleep that
  // only a key or the glass undoes; with it on, a console that cannot be taken refuses the nap, which is what has always been here.
  // **I només si la placa té una consola que un son lleuger no apagui** (0.190.152): the switch says whether the person wants the console
  // to be a door, and the board says whether a console can be one at all (docs/50, docs/30).
  const bool the_console = wake_from_uart() && the_console_can_wake();
  bool ok = !the_console || esp_sleep_enable_uart_wakeup(0) == ESP_OK;
  // **I si el xip no pren la consola com a porta, es diu** (0.190.71): aquest `no` era mut, i una becaina que no es demana perquè la seva
  // porta no s'ha pogut armar és exactament el que una persona no pot distingir d'una placa que no dorm (docs/50).
  if (!ok) {
    ++g_nap_refusals;
    if (g_nap_refusals == 1) {
      hal::logf(pal::kLogWarn, "POWER", "the console cannot be a door of the light sleep, so there is none");
    }
    g_nap_refused = true;
  }
  if (ok) {
    fflush(stdout);
    const int64_t before_us = esp_timer_get_time();
    const esp_err_t asked = esp_light_sleep_start();
    const int64_t slept_ms = (esp_timer_get_time() - before_us) / 1000;
    // **El que diu el xip, i no el que torna la crida** (0.190.69): el valor de tornada d'`esp_light_sleep_start` no és un
    // `ESP_OK`/error sinó el motiu del despert —el 259 del registre del 20/11 era un `ESP_ERR_SLEEP_REJECT`, o sigui una becaina
    // que el xip no havia pres, però una becaina bona també en torna un número—. La pregunta honesta la contesta
    // `esp_sleep_get_wakeup_cause()`: si diu que no sap per què s'ha despertat, és que no ha dormit (docs/50, docs/95).
    // **I el temps viscut ho diu igual**: una becaina que ha durat un mil·lisegon no ha dormit ningú, i el motiu que hi hagi al
    // registre seria el d'una altra (docs/95).
    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_UNDEFINED || slept_ms < 2) {
      ok = false;
      ++g_nap_refusals;
      if (g_nap_refusals == 1) {
        hal::logf(pal::kLogWarn, "POWER",
                  "the chip did not take the light sleep (%d): a door was already open, or this board cannot take one",
                  static_cast<int>(asked));
      }
      if (g_nap_refusals >= kNapRefusalsMost) {
        g_nap_refused = true;
        hal::logf(pal::kLogWarn, "POWER", "no more light sleeps in this run: the waits are spent awake");
      }
    } else {
      g_nap_refusals = 0;
      // **I el que ha tornat el xip es mira per aprendre'n una cosa** (0.190.72): si ha estat el GPIO del panell i la línia del dit és
      // baixa, el vidre és una porta de debò i el nucli pot deixar d'esperar en engrunes. Es diu un cop, perquè és el que canvia el
      // comportament de la placa (docs/50, docs/95).
      if (!g_glass_is_a_door && glass_pin_ >= 0 && cause == ESP_SLEEP_WAKEUP_GPIO &&
          gpio_get_level(static_cast<gpio_num_t>(glass_pin_)) == 0) {
        g_glass_is_a_door = true;
        hal::logf(pal::kLogInfo, "POWER", "the glass woke the chip from a light sleep: it is a door, and the waits can be long");
      }
      // **I una becaina que no ha acabat el seu temps també es diu**: la que el temporitzador ha deixat anar és el cas normal i
      // no omple el registre, però la que ha tallat una tecla, el dit o la consola és la línia que explica per què la placa és
      // desperta abans d'hora (docs/50, docs/95).
      if (cause != ESP_SLEEP_WAKEUP_TIMER) {
        hal::logf(pal::kLogDebug, "POWER", "a nap of %lld ms ended by %s", static_cast<long long>(slept_ms),
                  nap_cause_word(cause));
      }
    }
  }
  // El que hagi quedat armat es desfà: el son profund arma el que vol i no ha d'heretar res d'aquí (docs/50). I el compte del
  // temporitzador es posa a zero amb ell, o el proper `disarm` demanaria a l'IDF que tragués un despertar que ja no hi és.
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp32_note_wake_cleared();
  for (int i = 0; i < 4; ++i) {
    if (armed[i] < 0) continue;
    gpio_wakeup_disable(static_cast<gpio_num_t>(armed[i]));
    // **I la línia torna a ser el que era** (0.190.74): sense això, el pin queda amb una interrupció de nivell que va tornar boig el registre
    // del panell —la vora de baixada del GT911— i el tàctil no responia més (docs/50, docs/95).
    if (saved_type[i] >= 0) {
      gpio_ll_set_intr_type(hw, static_cast<gpio_num_t>(armed[i]), static_cast<gpio_int_type_t>(saved_type[i]));
    }
  }
  return ok;
}

bool Esp32Power::battery(int& mv, int& pct, bool& charging) {
  (void)mv;
  (void)pct;
  (void)charging;
  return false;
}

void Esp32Power::say_the_way_down(SleepMode mode, int64_t at_epoch_ms) {
  if (at_epoch_ms > 0) {
    hal::logf(pal::kLogInfo, "POWER", "%s sleep until %lld", (mode == SleepMode::kDeep) ? "deep" : "light",
              static_cast<long long>(at_epoch_ms));
  } else {
    hal::logf(pal::kLogInfo, "POWER", "%s sleep until a key of the board",
              (mode == SleepMode::kDeep) ? "deep" : "light");
  }
}

bool Esp32Power::sleep(SleepMode mode, int64_t at_epoch_ms, bool keep_the_panel) {
  // A board with no panel of its own has nothing to keep: the whole of it is the chip (docs/50).
  (void)keep_the_panel;
  // A key that wakes the chip is the one thing a person can press, and it works from
  // either sleep. A board without one and without a deadline would be a board that is
  // gone until the cable is pulled, and the kernel refuses that on its own as well
  // (docs/60).
  // **I el son profund no hereta cap porta de la becaina** (0.190.152, l'amo: *«Segur que no es continua enganxant a l'UART per
  // despertar?»*): les fonts de despertar viuen en una estructura estàtica de l'IDF, així que un xip que ha fet una becaina abans en
  // tornava per activitat al port en lloc de dormir. Es neteja **abans** d'armar, o el que s'esborraria serien les portes de debò
  // (docs/50, docs/60).
  if (mode == SleepMode::kDeep) esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  const bool a_door = arm_the_doors();
  if (at_epoch_ms > 0) {
    // The timer is armed in one place only, so that taking it down again knows whether it is
    // ours (docs/60).
    esp32_arm_timer_wake(at_epoch_ms);
  } else if (!a_door) {
    return false;
  }
  // **La consola és una porta del son lleuger, sigui quin sigui el camí que hi porta** (0.190.152, l'amo: *«l'UART si que ha de
  // despertar el lightSleep, si no no es pot usar la shell per serial (es menja caràcters) pero no de DeepSleep»*). `nap()` l'armava i
  // aquest camí no, o sigui que el son lleuger demanat de debò era l'únic que no escoltava el cable; i el son profund no la pren mai,
  // perquè `arm_the_doors()` de cada placa neteja totes les fonts abans d'armar les seves (docs/50, docs/60).
  if (mode == SleepMode::kLight && wake_from_uart() && the_console_can_wake()) {
    if (esp_sleep_enable_uart_wakeup(0) != ESP_OK) {
      hal::logf(pal::kLogWarn, "POWER", "the console cannot be a door of this light sleep");
    }
  }
  say_the_way_down(mode, at_epoch_ms);
  // The chip goes quiet with the clocks: what is still in the UART has to leave
  // before, or the line that says why it sleeps never arrives.
  fflush(stdout);

  if (mode == SleepMode::kDeep) {
    esp_deep_sleep_start();  // does not return: the board restarts (docs/90)
    return false;
  }
  const int64_t before = esp32_wall_ms();
  esp_light_sleep_start();
  hal::logf(pal::kLogInfo, "POWER", "awake after %lld ms (%d)",
            static_cast<long long>(esp32_wall_ms() - before),
            static_cast<int>(esp_sleep_get_wakeup_cause()));
  return true;
}

const char* Esp32Power::wakes() {
  if (wake_pin_ >= 0 && second_wake_pin_ >= 0) return "timer, button, clock";
  return (wake_pin_ >= 0) ? "timer, button" : "timer";
}

uint32_t Esp32Power::cpu_mhz() {
#if defined(ARDUINO)
  return getCpuFrequencyMhz();
#else
  rtc_cpu_freq_config_t config;
  rtc_clk_cpu_freq_get_config(&config);
  return config.freq_mhz;
#endif
}

// 240, 160 and 80 and nothing else. Below 80 the APB follows the CPU down and **no
// peripheral of the core reconfigures itself** (there is not one
// onApbChangeCallback registered anywhere in cores/esp32), so the serial port would
// come out at the wrong baud: the board's only interface would be garbage (docs/50).
// A value the board does not do is refused and not rounded.
bool Esp32Power::set_cpu_mhz(uint32_t mhz) {
  if (mhz != 80 && mhz != 160 && mhz != 240) return false;
#if defined(ARDUINO)
  if (!setCpuFrequencyMhz(mhz)) return false;
#else
  rtc_cpu_freq_config_t config;
  if (!rtc_clk_cpu_freq_mhz_to_config(mhz, &config)) return false;
  rtc_clk_cpu_freq_set_config(&config);
#endif
  // A chip rated for 160 only answers 160 when asked for 240: the answer is what the
  // board is really running at, not what it was asked for.
  return cpu_mhz() == mhz;
}

}  // namespace hal
