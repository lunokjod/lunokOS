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

// The title of the app in front, or "" when there is none. The bar repaints itself on the next turn —and **at once** if
// the caller asks for it with `flush()` below, which is what a start does so its name is on the glass before the app is
// even read (0.179.27).
void title(const char* text);

// **Whether the strip is on the glass at all** (0.136.0): an app that asked for the whole glass has it away, and the
// system paints no strip over a page that is not its own until the glass is given back (docs/30, docs/46 D1). A bar
// that comes back is painted whole, because what is under it is not the bar any more.
void show(bool on);
bool visible();

// The glass changed under the bar —a turn, a new size—: paint the strip again whole (docs/50).
void refresh();

// **Lo que la barra deu, ara i no al final del torn** (0.179.27): `title()` and `refresh()` only raise a flag, and the
// strip is painted by `poll()` —which the kernel calls **after** the app's turn—, so the title of a start used to reach
// the glass after the app's first page and after the whole of the start (`malloc`, the read, the machine and the
// compile). This paints it where it was asked for. It reads nothing: the hour and the charge are the last ones the
// turn read, and what it costs is one pass of the strip (docs/30, docs/50).
//
// It answers whether it painted anything, which is false for a bar that is hidden (an app with the whole glass), one
// that was never painted, or one with nothing owing.
bool flush();

// One turn of the bar: if the minute, the title or the charge changed, it paints **its own strip** and
// asks for it. Nothing else on the glass is touched (docs/30).
void poll();

}  // namespace bar
}  // namespace kernel

#endif  // LUNOK_KERNEL_BAR_H
