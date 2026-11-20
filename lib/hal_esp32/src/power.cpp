// The sleep of the ESP32: the RTC timer, the two depths, and the frequency of the
// processor.
#include <cstdio>

#include <esp_sleep.h>
#include <esp_timer.h>

#include <driver/gpio.h>

// The core's own speed control: it is the one that also moves the APB and the
// FreeRTOS tick when the frequency changes (read on disk, docs/50).
#include <esp32-hal-cpu.h>

#include "hal/log.h"
#include "esp32/clock.h"
#include "esp32/power.h"

namespace hal {

Esp32Power::Esp32Power(int wake_pin, int second_wake_pin, int glass_pin)
    : wake_pin_(wake_pin), second_wake_pin_(second_wake_pin), glass_pin_(glass_pin) {}

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
  return wake_pin_ >= 0 || glass_pin_ >= 0;
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

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(max_ms) * 1000ULL);

  // **Les tecles i el vidre, pel nivell**: en un son lleuger el `gpio_wakeup_enable` val per a qualsevol GPIO i no només pels del
  // domini RTC, que és el que fa possible escoltar la línia del tàctil (docs/50).
  int lines[3] = {wake_pin_, second_wake_pin_, glass_pin_};
  int armed[3] = {-1, -1, -1};
  bool a_button = false;
  for (int i = 0; i < 3; ++i) {
    if (lines[i] < 0) continue;
    if (gpio_wakeup_enable(static_cast<gpio_num_t>(lines[i]), GPIO_INTR_LOW_LEVEL) != ESP_OK) continue;
    armed[i] = lines[i];
    a_button = true;
  }
  if (a_button) esp_sleep_enable_gpio_wakeup();

  // **I la consola**, que és la porta que no es pot perdre: aquesta placa no té teclat i el prompt és l'única manera de parlar-hi. Si
  // el xip no la pot prendre, no es dorm: una becaina que es menja el que una persona escriu és pitjor que no estalviar res (docs/50).
  bool ok = esp_sleep_enable_uart_wakeup(0) == ESP_OK;
  if (ok) {
    fflush(stdout);
    const int64_t before_us = esp_timer_get_time();
    const esp_err_t slept = esp_light_sleep_start();
    const int64_t slept_ms = (esp_timer_get_time() - before_us) / 1000;
    // **Un son que el xip no ha pres es diu**: la volta que ve el sistema no el tornaria a demanar i el que quedaria és una placa que
    // diu que estalvia i no estalvia (docs/50).
    if (slept != ESP_OK) {
      hal::logf(pal::kLogWarn, "POWER", "the chip did not take the nap (%d)", static_cast<int>(slept));
      ok = false;
    } else {
      // **I una becaina que no ha acabat el seu temps també es diu**: la que el temporitzador ha deixat anar és el cas normal i
      // no omple el registre, però la que ha tallat una tecla, el dit o la consola és la línia que explica per què la placa és
      // desperta abans d'hora (docs/50, docs/95).
      const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
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
  for (int i = 0; i < 3; ++i) {
    if (armed[i] >= 0) gpio_wakeup_disable(static_cast<gpio_num_t>(armed[i]));
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
  const bool a_door = arm_the_doors();
  if (at_epoch_ms > 0) {
    // The timer is armed in one place only, so that taking it down again knows whether it is
    // ours (docs/60).
    esp32_arm_timer_wake(at_epoch_ms);
  } else if (!a_door) {
    return false;
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

uint32_t Esp32Power::cpu_mhz() { return getCpuFrequencyMhz(); }

// 240, 160 and 80 and nothing else. Below 80 the APB follows the CPU down and **no
// peripheral of the core reconfigures itself** (there is not one
// onApbChangeCallback registered anywhere in cores/esp32), so the serial port would
// come out at the wrong baud: the board's only interface would be garbage (docs/50).
// A value the board does not do is refused and not rounded.
bool Esp32Power::set_cpu_mhz(uint32_t mhz) {
  if (mhz != 80 && mhz != 160 && mhz != 240) return false;
  if (!setCpuFrequencyMhz(mhz)) return false;
  // A chip rated for 160 only answers 160 when asked for 240: the answer is what the
  // board is really running at, not what it was asked for.
  return getCpuFrequencyMhz() == mhz;
}

}  // namespace hal
