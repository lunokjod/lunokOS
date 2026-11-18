// **La pàgina d'una app que s'ha mort** (0.189.162, l'amo: *«Si peta una app, que menys que saber el perqué amb la pantalla i
// els dits. Un modal amb el missatge d'error? vull detalls del perqué una app peta»*).
//
// Fins ara, una app que es trencava deixava el seu error **al registre i a la consola**, i el que veia una persona amb el vidre
// era la pàgina de sota, com si no hagués passat res: la lletra del perquè hi era, però calia un cable per llegir-la. Aquest
// fitxer és el que la posa al vidre: el vidre sencer rentat, el nom de l'app, el missatge, el rastre de les crides i una línia
// que diu com continuar. **I s'espera el dit o una tecla** —o els segons que la pàgina espera sola— perquè una pàgina d'error
// que marxa abans que ningú la llegeixi és una pàgina que no explica res (docs/30, docs/50).
#ifndef LUNOK_KERNEL_APP_ERROR_H
#define LUNOK_KERNEL_APP_ERROR_H

#include <stdint.h>

namespace hal {
class ISurface;
}  // namespace hal

namespace kernel {
namespace app_error {

// **El que s'ensenya**, amb l'app que ha petat i el text que el intèrpret ha deixat: el missatge i el rastre, amb els salts de
// línia que tingui. No fa res en una placa sense vidre. **No torna fins que el botó de la pàgina es polsa** (0.190.1, l'amo: *«el
// modal ... ha de tenir un botó "Dismiss" i fins que no es polsi no se surt de l'app»*): el dit que la va fer petar i un toc a
// qualsevol altre lloc del vidre no compten, i una placa sense dit surt amb una tecla, que és l'única porta que li queda (docs/30).
void show(const char* id, const char* text);

// **El botó de la pàgina**: where it is **on the glass** of this size, and **if a gesture is the answer** —a tap or a long press on
// the button, and nothing else: not a tap on the message, not a drag, not a tap on the bar of the system—. The point it is asked
// about is a point of the **glass**: the fingers of the kernel come in the frame of the view and the page turns them with
// `touch::on_the_glass` before asking (0.190.4, docs/30). Són les dues preguntes que fa
// l'espera i les dues que un cas pot fer sense tocar res (docs/30, docs/80).
struct Button {
  uint16_t x = 0;
  uint16_t y = 0;
  uint16_t w = 0;
  uint16_t h = 0;
};
Button dismiss_button(uint16_t width, uint16_t height);
bool it_is_an_answer(uint16_t width, uint16_t height, const char* kind, int32_t x, int32_t y);

// **El botó, pintat com el botó que és** (0.190.4, l'amo: *«El botó dismiss no es un botó, es un dibuix, PERQUE?»*). The page paints its
// button with the system's own rule and not with a rectangle: the box is the shadow, the face is lifted off the bottom of it, and a
// finger on the face sinks it and **turns it over** —ink face, paper word—, which is what every key of this panel does (docs/30,
// docs/50, docs/90). **No és un `ui.button`** perquè un control d'aquells és d'una app: viu en una ranura del canvas de l'app, el
// dibuixa el seu `ui.present` i respon amb el seu `on_click`, i aquesta pàgina existeix justament perquè l'app ja no hi és (docs/30,
// docs/90). El que es pot demanar, i és el que fa aquesta funció, és que **sembli i es comporti** com un botó del sistema.
//
// Pinta **només dins del requadre del botó**, o sigui que el que canvia és la seva caixa i prou: així una premsa costa una `flush`
// de la mida de la tecla i no una pàgina sencera. El cas de la suite la crida dues vegades —aixecat i premut— i mira la tinta de la
// cara, que és el que una persona ve (docs/80).
void paint_the_button(hal::ISurface& surface, Button button, bool pressed, uint8_t scale);

// **Quant temps espera la pàgina, i zero vol dir sempre** (0.190.1): the page of an error is for the person in front of it and it
// waits as long as it takes to read it —a crash page that closes itself is a page that hides the reason—, so the system's own number
// is **no limit**. A case of the suite that runs a page nobody is going to touch sets a short one and puts it back with zero
// (docs/80, docs/85).
void set_wait_ms(int64_t ms);

}  // namespace app_error
}  // namespace kernel

#endif  // LUNOK_KERNEL_APP_ERROR_H
