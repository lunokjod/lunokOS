// What the light says. The board can paint it and this is the one place that
// decides what colour it is, because a light is a thing the system says about
// itself and not a thing an app owns (docs/30, docs/50).
//
// It is written so that a board with a strip of twenty-five grows nothing: the
// policy works in "the whole light" and the driver is what knows how many there
// are (docs/50).
#ifndef LUNOK_KERNEL_LED_H
#define LUNOK_KERNEL_LED_H

#include <stdint.h>

#include "hal/hal.h"

namespace kernel {
namespace led {

// What the light says when no app has taken it. **Two states and no more**: a light
// that is on costs milliamps and this board spends the rest of its life trying not
// to (docs/30, docs/75).
//
//  - `kRadio`: the radio is working. It is the one thing a person cannot see from
//    the console, and it lasts as long as the request does.
//  - `kOff`: everything else. An app in front, an app that died and the board
//    sitting there are all the console's business and not the light's.
enum class State : uint8_t {
  kOff = 0,   // nothing to say, and nothing being spent
  kRadio = 1  // the radio is working
};

// Reads the board's light. False when this board has none, and then every call
// here does nothing (docs/50).
bool boot(hal::IPlatform& platform);
bool available();

// How many lights the board has: one on the ATOM Lite, twenty-five on the Matrix.
uint32_t count();

// The highest a channel may be driven at on this board —255 where there is no
// limit, and the maker's number on a panel that heats up—. Nothing an app asks for
// goes past it, and that is the whole point: the panel is not the app's to burn
// (docs/30, docs/50).
uint32_t max_value();

void set_state(State state);

// The colour an app asked for while it is in front. It wins over the state and it
// is given back when the app goes — and given back means **off**, not "the colour
// it had": a light left on after its app has gone is power spent by nobody
// (docs/30, docs/75).
bool set_colour(uint8_t r, uint8_t g, uint8_t b);
void clear_colour();
bool has_colour();

// One light of the strip, for a board that has more than one. The app owns the
// whole light from then on — a frame of its own and not a colour — and `set_colour`
// paints all of them again. False when this board has no light or has no light at
// that index, and then nothing is painted (docs/30, docs/50).
bool set_pixel(uint32_t index, uint8_t r, uint8_t g, uint8_t b);

// Paints when what has to be shown has changed, and not before: a frame is a write
// on the wire and the light is also a cost (docs/75).
void poll();

// What is on the light now, for the shell to show it.
void current(uint8_t& r, uint8_t& g, uint8_t& b);

}  // namespace led
}  // namespace kernel

#endif  // LUNOK_KERNEL_LED_H
