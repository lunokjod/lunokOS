// **La llum de fons d'un vidre, que és un LED i no una tira** (0.190.99, l'amo: *«tenim interficie per leds a lunokOS, ja saps com
// fer-ho»*). A board may carry one white LED behind its panel and drive it with a PWM: there is no colour and no address in it, o
// sigui que el que `ILed` demana —un color per llum— s'ha de traduir a **una intensitat**, que és el que el LED entén.
//
// El mapa és la lluminància i no la mitjana: `led.fill(255, 255, 255)` és el màxim, un `fill(0, 0, 0)` és apagat, i un to que una app
// demana es veu amb el pes que l'ull li dóna (0,299 del vermell, 0,587 del verd i 0,114 del blau). Així una app que pinta la seva llum
// amb els mateixos colors de sempre obté una llum de fons que respon, sense una segona API que digui el mateix d'una altra manera
// (docs/30, docs/50).
//
// **El PWM va a un quilohertz i prou** (0.190.99): el full del fabricant de l'PT4103 d'aquesta placa demana que el senyal de regulació
// no passi d'1 kHz, i per sota de la vista no es veu parpellejar. Vuit bits de resolució són 256 graons, que és molt més del que l'ull
// distingeix en una llum de fons (docs/50).
//
// **I el deure és una recta, i s'hi queda** (0.190.102, l'amo: *«lo del led ha estat una mala idea, deixa'l com abans»*). Between them
// there was a perceptual curve over twelve bits with a hardware fade (0.190.100) and it was **not** better: it made the same steps and
// the light went out sooner, o sigui que el que una persona llegeix pitjor és una llum que no fa el que el número diu. The straight line
// is what the hardware understands and what the prompt's number means, and the sizes are what they are (docs/30, docs/50).
#ifndef LUNOK_ESP32_BACKLIGHT_H
#define LUNOK_ESP32_BACKLIGHT_H

#include <stdint.h>

#include "hal/hal.h"

namespace hal {

class Backlight : public ILed {
 public:
  // `pin` is the board's, and the channel is this file's: a board has one backlight and it is the only thing it drives with a PWM
  // unless it says otherwise (docs/50).
  explicit Backlight(int pin, uint32_t hz = 1000);

  // **I no és una llum qualsevol: és la del vidre** (0.190.106, l'amo: *«El led d'aquesta placa es un backlight»*): the system asks this and
  // knows that this one is the panel's light —the level a person chooses— and not a light that says things (docs/30, docs/50).
  LedRole role() const override { return LedRole::kBacklight; }
  // **Una llum**, i el sostre és el del PWM: una app que demana 255 demana el màxim que aquesta placa pot donar, i una que en demana
  // més no en treu res de nou (docs/30).
  uint32_t count() override { return 1; }
  uint32_t max_value() override { return 255; }

  bool fill(uint8_t r, uint8_t g, uint8_t b) override;
  bool pixel(uint32_t index, uint8_t r, uint8_t g, uint8_t b) override;
  // **Res no surt cap al maquinari fins que ho demanen**: el que es pinta es queda al registre i `show` és el que mou el deure, que és
  // la mateixa regla que la tira de l'ATOM (docs/75).
  void show() override;

  // El que hi ha posat, de 0 a 255, per a qui ho vulgui dir (`led` diu el que la placa té, no el que l'app ha demanat).
  uint8_t level() const { return level_; }

  bool ready() const { return ready_; }

 private:
  static uint8_t luminance_of(uint8_t r, uint8_t g, uint8_t b);
  void put(uint8_t level);

  int pin_;
  uint32_t hz_;
  uint8_t level_ = 0;
  uint8_t shown_ = 0;
  bool ready_ = false;
};

}  // namespace hal

#endif  // LUNOK_ESP32_BACKLIGHT_H
