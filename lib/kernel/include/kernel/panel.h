// The panel of the system: the card that hangs from the bar (0.179.22, docs/30).
//
// **The bar is the handle.** A finger that lands on the strip of the system and comes down pulls out a card of the
// system's own, and the page under it does not hear the hand: the app never learns that the panel exists, so an app
// written before the panel did keeps working the same —and one that draws `du` over the rows of the bar would paint
// over the card if it heard the finger and repainted— (docs/30, docs/50).
//
// **It is empty on purpose.** What goes in it —the radio, the powers, the settings— comes later, and a panel that
// starts with its contents is a panel whose shape was chosen by the first thing that went in. What it has today is
// paper, the line that closes it and the handle of the top, which is enough to see that it opened.
//
// **The ink is the system's ink**: white paper, two rows of black under the card and the same handle the strip of the
// bar suggests, in `gc16` and only when it opens and closes —a panel is two paints and not an animation, because an
// e-ink glass has no animation to give— (docs/50, docs/92).
#ifndef LUNOK_KERNEL_PANEL_H
#define LUNOK_KERNEL_PANEL_H

#include <stdint.h>

namespace kernel {
namespace panel {

// Forgets everything a boot of before left: the panel is closed, no card is on the glass and the page is nobody's
// hostage (docs/30).
void boot();

// Whether the card is out. An app does not read this; the system does, and the panel that grew contents would say it
// with a call of its own (docs/30).
bool open();

// How many rows the card takes under the bar: **the rows its keys need and no more** (0.192.25, l'amo: *«Ara els panells son molt grans per el
// contingut que tenen, no?»*). El terç del vidre que era la seva forma des de la 0.179.22 era la mida d'una targeta de tres files; amb dues, el
// que mana és el contingut, i el terç només hi torna en un vidre més baix que les claus (docs/30, docs/92).
uint16_t height();

// **Les quatre claus que naveguen són a la targeta de baix** (0.192.24, l'amo: *«Tots els botons de fila de Settings han d'anar abaix:
// Settings, Home, Close i Back al panell de sistema inferior»*): la roda dels ajustos, la casa, la creu del camí de sortida i la fletxa
// d'una pàgina enrere les pinta `panel_low`, i **el que saben i el que fan continua sent d'aquí**, perquè les preguntes —quines apps hi
// ha al disc, quina pàgina es pot deixar, quantes vistes té l'app— les contesta aquest mòdul amb el portal i el shell, i dues targetes no
// poden tenir dues idees de la mateixa clau (docs/30, docs/45).
enum class PageKey : uint8_t { kSettings = 0, kHome = 1, kClose = 2, kBack = 3 };

// Si la clau hi és: la creu sobre un llançador, la fletxa en una sola vista, la casa sobre la casa i la roda sense app no es pinten.
bool a_page_key_is_there(PageKey which);

// I el que fa, amb la targeta que sigui a fora tancada i la pàgina tornada o demanada.
void press_a_page_key(PageKey which);

// **El disc, preguntat una vegada per cada obertura**: la targeta de baix ha de saber quines apps de `cfg` hi són abans de pintar la
// roda o la casa, i la resposta és la mateixa que la de dalt (docs/30, docs/40).
void look_at_what_is_there();

// **The hand of the panel**, asked in every turn before the app's: a drag that comes down from the bar opens the card,
// **a tap outside the card** puts it away, and while the card is out the gestures the panel did not take are nobody's
// (docs/30).
//
// **And it is the only way out** (0.179.42): the card used to go with a drag up as well, and with a tap anywhere, and the
// owner took both away —*«Ningú ha de poder tancar el panell excepte l'usuari, picant fora de la zona desplegada. Es deixa
// com estava i es resumeix l'execució de l'app lua»*—. A drag up is the scroll of the page under it, and the rows of the
// card are the card's. **The app does not run while the card is out** (`kernel.cpp` skips its turn), which is what makes the
// card unclosable by anything but a person.
void poll();

}  // namespace panel
}  // namespace kernel

#endif  // LUNOK_KERNEL_PANEL_H
