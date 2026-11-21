#ifndef LUNOK_KERNEL_BLUETOOTH_H
#define LUNOK_KERNEL_BLUETOOTH_H

#include <cstddef>
#include <cstdint>

namespace kernel {
namespace bt {

// **El teclat de Bluetooth** (0.190.179, l'amo: *«afegeix bluetooth al projecte, anem a fer un experiment»*). The board owns the radio;
// this is the policy: whether the board has one at all, what a person writes being turned into HID usage IDs, and the fact that while it
// is on the chip does not nap. It is not a network and it is not a page: there is nothing to connect to from here, there are characters
// to send (docs/30, docs/50).
bool available();
// **Encendre'l amb el nom que veurà l'altre aparell** i apagar-lo. Engegar-lo dues vegades no és un error.
bool start(const char* name);
void stop();
bool is_on();
// Si algú hi és connectat: un teclat que no ho diu envia caràcters al buit.
bool connected();
// **Una tecla**, pel seu Usage ID del HID i amb les de modificació (`0x02` és Shift). `false` vol dir que no hi ha ningú a l'altre cap.
bool tap(uint8_t mods, uint8_t usage);
// **Un text sencer**, caràcter a caràcter. Torna quants n'ha enviat dels que ha rebut, que és el que una app vol saber.
size_t type(const char* text);
// **El que sap aquest sistema del ràdio, en una frase** per al prompt i per al registre.
const char* state_word();

}  // namespace bt
}  // namespace kernel

#endif  // LUNOK_KERNEL_BLUETOOTH_H
