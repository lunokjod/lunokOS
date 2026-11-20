// **El xip adormit, que és el que faltava per poder provar el son** (0.190.66, l'amo: *«L'emulador ha de poder simular LightSleep i
// DeepSleep de l'esp32 per poder provar l'rtc o els gpio dels botons físics o el panell táctil enviant interrupcions al SoC
// adormit»*).
//
// Fins ara una baixada d'aquest emulador **no dormia**: `FakePower::sleep` movia el rellotge fins a l'hora demanada i tornava de
// seguida, o sigui que el nucli seguia fent voltes i el que es provava era el camí del despert, no el son. Amb aquesta porta, el
// xip **s'atura de debò**: el nucli no fa cap volta, el rellotge de la placa segueix comptant i el que el torna és una
// **interrupció** —el temporitzador del RTC, una tecla, o la línia del panell tàctil—, exactament com al maquinari.
//
// **I no és un fil d'espera del PC**: el que es busca (`gui_pump`) és el que fa que la finestra continuï viva mentre el SoC dorm, i
// que un clic a un botó de la finestra sigui una tecla premuda **al xip adormit**. Un run sense finestra i sense hora no espera
// ningú: no hi ha manera de prémer res, i la resposta ho diu (`kNothing`) en lloc de penjar el procés (docs/50, docs/85).
#ifndef LUNOK_EMU_SLEEP_H
#define LUNOK_EMU_SLEEP_H

#include <stdint.h>

#include "hal/hal.h"

namespace emu {

// **Per què s'ha despertat el xip**, que és el que `last_wake()` respondrà després i el que el registre dirà.
enum class WakeBy : uint8_t {
  kTimer = 0,   // l'hora que se li va donar: el temporitzador del RTC, o el xip del rellotge en una placa que en té
  kKey = 1,     // una tecla de la placa
  kGlass = 2,   // la línia del panell tàctil, quan el dit és una porta
  kSupply = 3,  // abans d'hora i sense cap porta: el corrent que torna (`--wake-early`, un cable que s'ha mogut)
  kClosing = 4, // la persona ha tancat la finestra: el run s'acaba, no és un despert del xip
  kNothing = 5, // no hi havia ningú que el pogués despertar aquí (ni hora, ni finestra): no s'ha dormit
};

// **El SoC dorm fins a `at_epoch_ms`** (0 quan no hi ha hora: aleshores només el tornen les portes), i la resposta és què l'ha
// despertat. `wake_early_ms` és el mateix número de `--wake-early`: el xip torna abans d'hora perquè el corrent ha tornat, que és
// el que fa una placa a la qual es toca el cable (docs/60, docs/85). `the_glass_is_a_door` és el commutador del dit: sense ell el
// panell tàctil no és cap porta i un dit no desperta res.
WakeBy wait_while_asleep(int64_t at_epoch_ms, int32_t wake_early_ms, bool the_glass_is_a_door);

// **I la mateixa espera comptada pel rellotge de la placa i no per una hora**: el que fa servir una becaina, que no sap quina hora
// és (una placa sense hora) i només vol que passin `ms` (docs/50).
WakeBy wait_for_ms(int32_t ms, bool the_glass_is_a_door);

// **Quantes voltes ha fet l'espera**: zero vol dir que el xip no ha dormit (no hi havia porta ni hora) i qualsevol altre número és
// el que ha costat el son en voltes d'un mil·lisegon. És el número que diu si això funciona quan no hi ha finestra per mirar-s'ho.
uint32_t sleep_turns();

// **L'estat en què el xip s'ha arrencat** (0.190.68, l'amo: *«recorda que l'esp32 te estats al arrencar, així el firmware sabrà si ha
// despertat normal o d'un deep sleep»*). A la placa ho diu `esp_reset_reason()`; aquí ho diu el model: el primer arrencament és el
// corrent que arriba, un son profund deixa `kDeepSleep` per a la propera arrencada, i un reinici demanat pel sistema deixa `kSoftware`.
// És el que fa que el nucli pugui dir la veritat encara que la marca desada s'hagi perdut (docs/50, docs/60, docs/85).
// **Es pot tornar a posar**, i és el que ha de fer un run que vulgui un xip nou: a la placa el motiu es reinicia sol a cada
// arrencada, i aquí el procés segueix viu —o sigui que una prova que ha modelat un despert deixa `kPowerOn` per a la que vingui
// darrere, com deixa les altres opcions (docs/85)—.
void note_the_reset(hal::ResetCause cause);
hal::ResetCause the_reset();

// **Si el darrer son ha estat un reinici del xip** (0.190.68): el model, en tornar d'un son profund amb els temps de debò, marca que
// el que toca és una arrencada. El nucli ho pregunta una vegada (`IPower::take_the_restart`) i `main_host` fa el `kernel::boot` al
// final del torn, que és el que faria el xip sol (docs/60, docs/85).
void note_the_restart();
// **I la pregunta que la buida**: true una vegada si el darrer son ha estat un reinici del xip.
bool take_the_restart();

}  // namespace emu

#endif  // LUNOK_EMU_SLEEP_H
