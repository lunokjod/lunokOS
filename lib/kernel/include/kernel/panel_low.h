// **La targeta que penja de la franja de baix** (0.192.23, l'amo: *«Vull un panell desplegable a la barra inferior que es desplegui
// igual que el superior però a l'inversa»*): the same card as `panel`, mirrored —it rises from the bottom strip instead of hanging from
// the bar, it is pulled up instead of down, and it goes with a drag down and a tap outside it— i, des de la 0.192.24, **amb les quatre
// claus que naveguen**: Settings, Home, Close i Back, que és on l'amo les ha demanades.
//
// **Només una targeta pot ser fora**: obrir-ne una amb l'altra a sobre seria dues targetes i una pàgina que no es veu. I el gest que
// tanca una targeta **el consumeix ella**: el mateix arròs no pot tancar la de dalt i obrir la de baix, perquè el que la persona
// està dient quan arrossega és que la que mira marxi. `kernel::poll` crida aquest mòdul **abans** que `panel::poll`, o sigui que la
// targeta que és fora té la primera paraula sobre el gest que la tanca.
#ifndef LUNOK_KERNEL_PANEL_LOW_H
#define LUNOK_KERNEL_PANEL_LOW_H

#include <stdint.h>

namespace kernel {
namespace panel_low {

// A boot is a glass nobody has touched: the card of the run before is not on it.
void boot();

// Whether the card is out, which is what stops the turn of the app behind it.
bool open();

// The rows it takes from the glass, and where its first row is.
uint16_t height();
uint16_t top_row();

// **Les quatre claus que naveguen viuen aquí** (0.192.24, l'amo: *«Tots els botons de fila de Settings han d'anar abaix: Settings, Home,
// Close i Back al panell de sistema inferior»*): the gear, the house, the cross and the arrow, at the bottom edge of the card and just
// over the strip, que és on el dit hi és. El que fan és de `panel` (`press_a_page_key`), i d'aquí només en surt el dibuix i el toc.

// **Tancar la targeta per a qui la fa servir des de fora**: una clau que arrenca una app o tanca la pàgina deixa anar el hold de la
// pàgina a `panel` —que és qui sap si la rentada la paga el que ve darrere—, i aquí només queda deixar de dir que la targeta és fora.
// No pinta: el que ve després pinta el vidre sencer (docs/30, docs/50).
void close_the_card();

// The gestures of the card: a drag up from the strip pulls it out, a drag down or a tap outside it puts it away.
void poll();

}  // namespace panel_low
}  // namespace kernel

#endif  // LUNOK_KERNEL_PANEL_LOW_H
