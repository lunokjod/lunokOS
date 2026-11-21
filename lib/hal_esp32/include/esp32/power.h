// The sleep of an ESP32 and the speed of its processor.
//
// Both are the chip's and not the board's: the RTC timer, the two depths and the
// three frequencies are the same on every ESP32 of this family. What the board
// decides is whether a key is wired to a wake source, and that is the one thing
// asked for here (docs/50, docs/60).
#ifndef LUNOK_ESP32_POWER_H
#define LUNOK_ESP32_POWER_H

#include <stdint.h>

#include "hal/hal.h"

namespace hal {

class Esp32Power : public IPower {
 public:
  // `wake_pin` is the GPIO the board wires to EXT0, or a negative number when the
  // board has no key that wakes the chip (the Paper's keys switch the power off
  // instead, docs/50).
  //
  // **I una segona porta, per a la placa que en té dues** (0.190.29): the T5 E-Paper S3 Pro wires the BOOT key
  // and the line of its clock chip (a PCF8563 whose INT goes low when the alarm fires) to two pins, and both are
  // ways back from a deep sleep. A chip **after** the ESP32 watches several pins at once with `ext1` in its «any
  // of them low» mode, which is exactly what a button to ground and an open-drain line are; on the ESP32 that mode
  // does not exist —`ALL_LOW` asks for every pin at the same time— so there the one door is `wake_pin` and the
  // second is not armed. `-1` is the default and keeps every board that came before this one exactly as it was
  // (docs/50, docs/60).
  // **I una quarta porta, que no és per al son profund** (0.190.99): the expander of the T5 E-Paper S3 Pro has an interrupt line of its
  // own (GPIO38) and it goes low when any of its inputs moves —the board's button, the supply's good line and the supply's interrupt—.
  // GPIO38 is not an RTC pin, o sigui que **no pot despertar el xip d'un son profund**, però sí d'un son lleuger, que és on aquesta
  // placa passa la vida esperant. És la mateixa porta que la tecla i el vidre pel camí del `gpio_wakeup` (docs/50, docs/60).
  explicit Esp32Power(int wake_pin, int second_wake_pin = -1, int glass_pin = -1, int more_pin = -1);

  // The chip has no fuel gauge of its own, and no board of this project reads one yet
  // —the Paper has the divider on GPIO35 and it is not calibrated—: what is not
  // measured is not invented, and `power.battery()` says nil, which is not zero volts
  // (docs/90).
  bool battery(int& mv, int& pct, bool& charging) override;

  bool sleep(SleepMode mode, int64_t at_epoch_ms, bool keep_the_panel = false) override;

  // **La becaina** (0.190.63): a light sleep of at most `max_ms` that comes back by its timer **or before**, quan algú prem una
  // tecla, toca el vidre o escriu a la consola. És un son lleuger i no un reset: la RAM, l'intèrpret i la pàgina hi són en tornar,
  // o sigui que és el que el sistema fa en lloc d'esperar. Una placa que no pot escoltar res mentre dorm contesta que no a
  // `can_nap` (docs/50, docs/60, docs/95).
  bool can_nap() const override;
  bool nap(int32_t max_ms) override;
  // **El pany de la becaina que fa el xip sol** (0.190.178): obrir les portes permet que la gestió de potència adormi el xip mentre el
  // nucli espera a la cua; tancar-les ho prohibeix i deixa el panell com era. Si obrir-les falla, no s'ha de dormir (docs/50, docs/60).
  bool open_the_nap_doors() override;
  void close_the_nap_doors() override;
  // **Qui ha despertat la darrera becaina, si algú ho pregunta** (0.190.178): la becaina del torn ja no la dorm `nap()` sinó la gestió de
  // potència, o sigui que el motiu no es pot llegir on es llegia. El registre continua dient quina porta ha tallat una espera —que és el que
  // explica un toc que sembla lent— i aquesta és la paraula que li falta a la línia. Null vol dir que el xip no sap per què s'ha despertat,
  // i aleshores no ha dormit ningú (docs/50, docs/95).
  const char* the_word_of_the_last_nap() override;

  // Said by the wiring: the RTC timer for the alarms, and the key when there is one.
  const char* wakes() override;

  uint32_t cpu_mhz() override;
  bool set_cpu_mhz(uint32_t mhz) override;
  // **El PM del xip, que és qui mou el rellotge des de la 0.190.175** (docs/50, docs/60): the range the chip may move between and the
  // top-step lock. A build without `CONFIG_PM_ENABLE` falls back to `rtc_clk_cpu_freq_set_config`, which is what the two
  // above do, so a band that has not been migrated keeps working.
  bool set_cpu_range(uint32_t min_mhz, uint32_t max_mhz) override;
  bool hold_cpu_top(bool on) override;
  // **El lock de la son** (0.190.184): el `ESP_PM_NO_LIGHT_SLEEP`, agafat mentre la consola del cable té una sessió oberta, perquè un xip
  // que s'adorm a trossos es menja els caràcters que arriben i fa esperar el despertar (docs/50, docs/60).
  bool hold_no_light_sleep(bool on) override;

 private:
  // The chip's top-step lock, made the first time it is asked for: a board that never asks the processor for the top has no lock
  // and no bytes spent on it.
  void* top_lock_ = nullptr;
  bool top_taken_ = false;
  // **I el de la son** (0.190.184): el `ESP_PM_NO_LIGHT_SLEEP` de la consola del cable, amb el mateix patró (es fa quan cal) (docs/60).
  void* sleep_lock_ = nullptr;
  bool sleep_taken_ = false;

 public:

 protected:
  // **La línia de la baixada, la mateixa a totes les baixades** (0.190.15): the idle policy, the card of the system and the `deepsleep`
  // of the prompt are three questions and **one** answer, and a person who compares two logs has to read the same line in both. Zero is
  // not an hour: it is the key of a board that keeps its supply.
  void say_the_way_down(SleepMode mode, int64_t at_epoch_ms);

  // **Les portes del despert, armades com la placa les té** (0.190.29): the one pin of `ext0`, the two of `ext1`, or
  // neither. True when at least one of them took the arming, which is what says whether a board with no hour has
  // anywhere to come back from (docs/50).
  //
  // **I és de la placa i no del xip** (0.190.102, l'amo: *«segur? no desperta mai»*): waking from a deep sleep is a **reset**, i un
  // pin que el xip mira en arrencar (un *strapping*) no pot ser la porta que el desperti si el dit que la desperta el deixa baix —
  // arrencaria en un altre mode. The chip does not know which of its pins the board put a button on and which of them it also reads
  // at boot, o sigui que la llista és de la placa (docs/50, docs/60).
  virtual bool arm_the_doors();

 private:
  int wake_pin_;
  // The second door, or -1: the line of a clock chip that goes low when its alarm fires (docs/50, docs/60).
  int second_wake_pin_;
  // **I la del vidre, que no és una porta d'un son profund però sí d'una becaina** (0.190.63): the INT line of the touch
  // controller falls while a finger is on the glass and rises when the frame is taken, so it is exactly the edge a light sleep
  // needs; it is not a door of the deep sleep because that one needs a level and this line pulses (docs/50).
  int glass_pin_;
  int more_pin_;
};

}  // namespace hal

#endif  // LUNOK_ESP32_POWER_H
