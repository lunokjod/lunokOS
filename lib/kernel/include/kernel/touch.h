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

// **Tornar-li la línia al panell després que el xip hagi dormit** (0.190.74): a light sleep arms that GPIO as a level wake and the edge the
// driver installed does not come back by itself; this puts it back, and a glass with no line does nothing (docs/50, docs/95).
void rearm();

// **El règim dels escombrats, aplicat a l'instant** (0.190.107): the switch of the Input tab says whether the glass drops its scans when
// nobody is on it, and this is the door that tells the glass. Cert és el règim adaptatiu i fals el de sempre al màxim; un vidre que no en sap
// no fa res (docs/30, docs/50).
void set_adaptive_scan(bool on);
bool adaptive_scan();

// **Quantes vegades s'ha preguntat i quantes ha respost** (0.189.85): the glass is asked once a turn and answers a frame only
// when there is a finger. The two numbers apart are the difference between «the interrupt is doing its job» and «the driver
// asks the bus fifty times a second for nothing», and that is the first question the energy work has to answer (docs/50,
// docs/95).
void counters(uint32_t& asks, uint32_t& frames, uint32_t& false_warnings);

// The points that were down in the last turn, up to `capacity`.
uint8_t points(hal::TouchPoint* out, uint8_t capacity);

// **On cau un punt de la vista sobre el vidre** (0.190.4). Everything this layer hands over —the frames of `points` and the
// points of every gesture— is in the frame of the **view**: an app draws in the page left over the bar of the system, and
// `turn_frame` takes the offset of the view out of what the chip measured. A page of the system —the page of an app that has
// died, which paints the whole glass— lays itself out on the **glass**, so the two frames are one offset apart and this is the
// way back: x and y, given in the view's frame, are left in the glass's.
//
// It is exact for every finger that is on the page, which is every finger that can press something of it. A finger that landed
// on the bar of the system keeps its own y (`turn_frame` says why), so the point it gives is the bar's and the one this returns
// is off by the bar's height — the bar is not a control of any page and 48 pixels are not a button (docs/30, docs/50, docs/90).
void on_the_glass(uint16_t& x, uint16_t& y);

// **El que la pàgina ha sentit de l'últim contacte** (0.189.0, l'amo: *«fes un mètode per retardar els events que
// s'envien als UIControl. Si es decideix que és un gest no s'envien mai els events i s'envia el gest»*): the page is told
// **nothing** while a finger is down —the kernel decides first— and it hears exactly one thing when the finger leaves: the
// press and the release of a point that did not walk, or one gesture with the travel of the whole contact. This is that
// decision, the one the log says line by line and the tests measure. `kNothing` is a contact that was the system's or the
// keyboard's, which tell their own things (docs/30, docs/50, docs/90).
enum class Told : uint8_t { kNothing = 0, kAPress, kAGesture };

struct LastContact {
  Told told = Told::kNothing;
  uint16_t x = 0;  // where the finger landed, which is the point the page was told about
  uint16_t y = 0;
  int16_t dx = 0;  // how far it walked, which is the gesture when there is one
  int16_t dy = 0;
  uint8_t frames = 0;
  uint16_t ms = 0;
};

const LastContact& last_contact();

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

// **I el que és de la pàgina que se'n va no és de la que arriba** (0.189.100): an app that is not on the glass any more
// leaves two things behind that would be read as if they were the new page's —the finger that is down (its click is
// delivered at the point it landed, which on the new page is whatever control is under that pixel) and the gestures the
// queue still owes (a tap is said three hundred and fifty milliseconds after the finger leaves)—. This is asked by the
// shell when an app goes and when one comes, and the finger that is on the glass is nobody's until it lifts (docs/30).
void forget_the_page_before();

}  // namespace touch
}  // namespace kernel

#endif  // LUNOK_KERNEL_TOUCH_H
