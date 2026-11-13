// The glass of the board: the fingers that are down and the gestures the kernel makes out of them.
//
// A finger is a **state and not an event**: the chip measures by itself and keeps its last frame
// until somebody takes it, so the kernel reads the glass in its own turn and keeps that frame. What
// an app gets is the frame of the last turn —twenty milliseconds at most— and never a second read of
// the chip, because a frame that is taken is cleared and a second read would answer "nobody" while a
// finger is still down (docs/30, docs/50).
//
// The gestures are shapes of that stream and they are made **here** and not in the chip: a tap, a
// long press, a drag and two fingers mean the same on any glass, they can be tested on the PC with a
// fake, and they cost nothing (docs/50).
#ifndef LUNOK_KERNEL_TOUCH_H
#define LUNOK_KERNEL_TOUCH_H

#include <stdint.h>

#include "hal/hal.h"

namespace kernel {
namespace touch {

// How many points a frame can hold: the chips here report five at most, and the kernel keeps that
// many so a frame is never cut (docs/50).
const uint8_t kMaxPoints = 5;

// Reads the board's glass. False when this board has none (docs/50).
bool boot(hal::IPlatform& platform);
bool available();

// Asked in every turn of the kernel: this is where a frame becomes a gesture.
void poll();

// The points that were down in the last turn, up to `capacity`.
uint8_t points(hal::TouchPoint* out, uint8_t capacity);

// A gesture, as the kernel says it: where it happened and how long it took.
struct Gesture {
  const char* kind = nullptr;  // "tap", "double", "triple", "long", "drag" (one finger or two),
                               // "two", "pinch", or nullptr for none
  uint16_t x = 0;              // where the finger first landed, or the middle of the two
  uint16_t y = 0;
  int16_t dx = 0;  // for a drag: how far it went. For a pinch: how far its middle went
  int16_t dy = 0;
  // For a pinch: how much **closer or further apart** the two fingers ended, in pixels. Positive is
  // apart, which is how every map and every photo app reads it (docs/30).
  int16_t spread = 0;
  int32_t held_ms = 0;
  uint8_t fingers = 0;
  // **Whether the finger landed on the bar of the system** (0.179.22): the bar is not inside the view of an app, so a finger
  // on it arrives at the top rows of the view and the numbers cannot tell the two apart. This flag is the chip's own y kept
  // before the frame was turned, and it is what makes a drag that comes down **from the titlebar** the system's and a drag
  // the same size that starts on the page the app's (docs/30).
  bool on_the_bar = false;
};

// **Taken once**: a second call answers false, so a gesture is a report and not something that
// repeats for ever in a loop that reads it twice (docs/30).
bool take_gesture(Gesture& out);

// **One gesture of a kind, and the others stay in their place** (0.136.0): the system hears the three taps that take an
// app out of the whole glass and the app never hears them, so that one gesture is taken out of the queue and the ones
// before it keep the order they had (docs/30, docs/46 D1).
bool take_gesture_of(const char* kind, Gesture& out);

// **A pinch that comes from big to small** (0.163.0): the system hears this one because it means «close» in every
// app —a page that is left has to be heard by the system, or every app has to implement the gesture and one of them
// will forget— so it is taken out of the queue the way the three taps are. **The other pinches stay where they are**:
// one that pulls the fingers apart, or one that comes back by less than `min_px`, is the app's and it reads it with
// `touch.gesture()` (docs/30, docs/46 D1).
bool take_pinch_in(int16_t min_px, Gesture& out);

// **A drag that comes down from the bar of the system** (0.179.22): the first `drag` whose finger landed on the bar and that
// went down by at least `min_px` and more than it went sideways —a shade is pulled down and not drawn sideways—. The ones
// before it keep the order they had, the same rule the three taps and the pinches follow (0.136.0) (docs/30).
bool take_drag_down_from_the_bar(int16_t min_px, Gesture& out);

// **What the page does not hear** (0.179.22): the card of the system is over the page, so the gestures nobody took are
// nobody's —an app that reacted under a panel would be acting on a page the person cannot see— (docs/30).
void forget_gestures();

}  // namespace touch
}  // namespace kernel

#endif  // LUNOK_KERNEL_TOUCH_H
