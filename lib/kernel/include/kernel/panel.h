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

// How many rows the card takes under the bar, which is a third of what the bar leaves: a card that covers everything
// is a page of the system and not a panel (docs/30).
uint16_t height();

// **The hand of the panel**, asked in every turn before the app's: a drag that comes down from the bar opens the card,
// a drag that goes up or a tap puts it away, and while the card is out the gestures the panel did not take are nobody's
// (docs/30).
void poll();

}  // namespace panel
}  // namespace kernel

#endif  // LUNOK_KERNEL_PANEL_H
