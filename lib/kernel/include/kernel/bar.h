// The bar of the system: the strip at the top of the glass that belongs to nobody's app (docs/30).
//
// It is **C++ and it is the system's**, like the shell is: the hour on the left, the title of the app in
// front in the middle and the battery on the right, on a white background with a black line under it.
// An app never draws here —its view starts below the bar, and the system makes that view smaller— so a
// page that goes wrong cannot take the hour with it.
//
// **It paints itself when something of it changes**, and never otherwise: a new minute, a new title, a
// new charge. Each of those is its own small region of the glass and its own paint, with the quality it
// deserves: `du` for a digit, `gc16` when the whole bar has to look clean again (docs/50).
#ifndef LUNOK_KERNEL_BAR_H
#define LUNOK_KERNEL_BAR_H

#include <stdint.h>

#include "hal/hal.h"

namespace kernel {
namespace bar {

// The rows the system keeps at the top of the glass. Everything an app can draw starts below them.
uint16_t height();

// Makes the bar and paints it once, white. Needs the panel: `display::boot` goes first (docs/50).
bool boot(hal::IPlatform& platform);

// The title of the app in front, or "" when there is none. The bar repaints itself on the next turn.
void title(const char* text);

// The glass changed under the bar —a turn, a new size—: paint the strip again whole (docs/50).
void refresh();

// One turn of the bar: if the minute, the title or the charge changed, it paints **its own strip** and
// asks for it. Nothing else on the glass is touched (docs/30).
void poll();

}  // namespace bar
}  // namespace kernel

#endif  // LUNOK_KERNEL_BAR_H
