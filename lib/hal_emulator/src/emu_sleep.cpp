// **El SoC adormit d'aquest emulador** (0.190.66): the chip stops making turns, the clock of the board keeps counting, and the
// only things that end the wait are the ones the hardware has —the RTC timer, a key of the board and the line of the touch panel—
// more the two that are this band's: the person closing the window and a run that has nobody to press anything.
//
// **El nucli no corre mentre dorm**: `FakePower::sleep` no torna fins que hi ha un motiu, o sigui que el bucle del nucli, les
// alarmes i l'app queden aturats exactament com en un xip aturat. El que sí que continua és el que mira la finestra (`gui_pump`),
// perquè una finestra que no respon no és una finestra on es pugui prémer una tecla, i el rellotge de la placa, que és el que ha
// de disparar el temporitzador (docs/50, docs/85).
#include "emu/sleep.h"

#include "emu/gt911.h"
#include "emu/gui.h"
#include "emu/model.h"
#include "emu/options.h"
#include "hal/hal.h"
#include "pal/pal.h"

namespace emu {
namespace {

uint32_t g_sleep_turns = 0;
// **L'estat de l'arrencada** (0.190.68): el primer cop és el corrent que arriba i, des d'aleshores, el que deixi escrit el que ha
// passat —un son profund o un reinici demanat— és el que el nucli llegirà a la propera arrencada.
hal::ResetCause g_reset = hal::ResetCause::kPowerOn;
bool g_a_restart_is_due = false;

// **El rellotge de la placa, que és el que mana**: una hora de debò quan n'hi ha (`--now`, una pila, un xip de rellotge) i el
// comptador monòton per a la becaina, que no en depèn.
int64_t the_epoch_ms() {
  hal::IClock* clock = hal::platform().clock();
  return (clock != nullptr) ? clock->epoch_ms() : 0;
}

int64_t the_mono_ms() {
  hal::IClock* clock = hal::platform().clock();
  return (clock != nullptr) ? clock->monotonic_ms() : static_cast<int64_t>(pal::system().millis());
}

// **Una espera amb les portes del maquinari**, que és el cor del model. `deadline_ms` és en el rellotge que diu `by_epoch`: l'hora
// de paret per a una baixada (`at_epoch_ms`) i el monòton per a una becaina, que no sap quina hora és.
WakeBy wait_until(int64_t deadline_ms, bool by_epoch, bool the_glass_is_a_door) {
  g_sleep_turns = 0;
  // Les marques del que pot despertar: una tecla que ja era premuda abans d'adormir-se no compta, i un dit que hi era tampoc.
  const uint32_t keys_before = key_presses();
  // **El panell només hi és si aquest run en té** (0.190.66): `touch_panel()` fabrica el xip i en diu la línia del registre, o sigui que
  // una placa sense tàctil no se'l pot demanar per saber si un dit l'ha despertada (docs/50, docs/85).
  Gt911Panel* panel = (model().touch && options().touch) ? touch_panel() : nullptr;
  const uint32_t glass_before = (panel != nullptr) ? panel->contacts() : 0;
  // **Sense cap porta i sense finestra no s'hi pot fer res**: no hi ha ni hora ni ningú que premi res, i esperar seria penjar el
  // run. Es diu i el que ho ha demanat segueix el seu camí (docs/85).
  if (deadline_ms <= 0 && !gui_showing()) return WakeBy::kNothing;

  for (;;) {
    // La finestra es mira primer: un clic és una tecla premuda i una finestra tancada és el final del run.
    gui_pump();
    if (gui_close_requested()) return WakeBy::kClosing;
    // **El RESET no és una porta del son, és el pin que atura el xip** (0.192.7): el que ve després no és el despert sinó una
    // arrencada, i per això la resposta ho diu abans que res del rellotge —l'hora que s'esperava ja no té cap paper— (docs/50).
    if (take_reset()) return WakeBy::kReset;
    if (key_presses() != keys_before) return WakeBy::kKey;
    if (the_glass_is_a_door && panel != nullptr && panel->contacts() != glass_before) return WakeBy::kGlass;
    if (deadline_ms > 0) {
      const int64_t now = by_epoch ? the_epoch_ms() : the_mono_ms();
      if (now >= deadline_ms) return WakeBy::kTimer;
      // **I una hora que no arriba mai perquè el rellotge no la sap**: una placa sense època que dorm fins a una hora no té
      // temporitzador que la torni, i el que quedaria és un procés penjat. Es tracta com el que és, una espera sense porta.
      if (by_epoch && now <= 0 && !gui_showing()) return WakeBy::kNothing;
    }
    // Un mil·lisegon per volta: prou fi perquè un clic no es noti, prou gruixut perquè el PC no giri en buit.
    ++g_sleep_turns;
    pal::delay_us(1000);
  }
}

}  // namespace

WakeBy wait_while_asleep(int64_t at_epoch_ms, int32_t wake_early_ms, bool the_glass_is_a_door) {
  int64_t deadline = at_epoch_ms;
  // **El corrent que torna abans d'hora** (`--wake-early`): la placa no arriba a l'hora que se li va donar, i el motiu és el cable
  // i no el temporitzador. És la mateixa opció que el model de salt, aquí amb el xip adormit de debò (docs/85).
  if (deadline > 0 && wake_early_ms > 0 && deadline - wake_early_ms > 0) {
    const WakeBy why = wait_until(deadline - wake_early_ms, /*by_epoch=*/true, the_glass_is_a_door);
    return (why == WakeBy::kTimer) ? WakeBy::kSupply : why;
  }
  return wait_until(deadline, /*by_epoch=*/true, the_glass_is_a_door);
}

WakeBy wait_for_ms(int32_t ms, bool the_glass_is_a_door) {
  if (ms <= 0) return WakeBy::kNothing;
  return wait_until(the_mono_ms() + ms, /*by_epoch=*/false, the_glass_is_a_door);
}

uint32_t sleep_turns() { return g_sleep_turns; }

void note_the_reset(hal::ResetCause cause) { g_reset = cause; }

hal::ResetCause the_reset() { return g_reset; }

void note_the_restart() { g_a_restart_is_due = true; }

// **I la pregunta que buida la marca**, que la fa el nucli des del camí de la baixada (`IPower::take_the_restart`).
bool take_the_restart() {
  const bool due = g_a_restart_is_due;
  g_a_restart_is_due = false;
  return due;
}

}  // namespace emu
