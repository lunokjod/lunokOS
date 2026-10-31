// What the light says. The board can paint it and this is the one place that
// decides what colour it is, because a light is a thing the system says about
// itself and not a thing an app owns (docs/30, docs/50).
//
// Two layers, and only two:
//
//   - the **system** layer, which is what the board is doing: it boots, the radio is
//     working, an alarm went off, something failed. Four moments, four effects, and an
//     app may redefine any of them (`set_system_effect`), because the light of a machine
//     is not a taste the kernel gets to keep (docs/30);
//   - the **app** layer: `play` while an app is in front. It wins over the system while
//     it lasts, and `stop` gives the light back — with a fade when the app asks for one.
//
// **No thread, no blocking, no drift.** An effect is a pure function of the time since
// it started: `poll` is called once at the end of every turn, asks "what should the light
// be at this millisecond", and paints it only if it changed. A turn that arrives late
// paints the frame of *that* moment instead of a queue of frames it owes, so a busy app
// cannot make the light lag or stutter — and an app that never yields starves the light
// the same way it starves everything else (docs/20).
//
// It is written so that a board with a strip of twenty-five grows nothing: an effect says
// "the whole light" or "this one light of the strip", and the driver is what knows how
// many there are (docs/50).
#ifndef LUNOK_KERNEL_LED_H
#define LUNOK_KERNEL_LED_H

#include <stdint.h>

#include "hal/hal.h"

namespace kernel {
namespace led {

// What the light says when no app has taken it. **Two states and no more**, because a
// light that is on costs milliamps and this board spends the rest of its life trying
// not to (docs/75):
//
//  - `kRadio`: the radio is working. It is the one thing a person cannot see from the
//    console, and it lasts as long as the request does;
//  - `kOff`: everything else.
enum class State : uint8_t {
  kOff = 0,   // nothing to say, and nothing being spent
  kRadio = 1  // the radio is working
};

// The four effects of the light, and there are no more: everything a light can say is
// one of these, with a colour and a speed (docs/30).
enum class EffectKind : uint8_t {
  kNone = 0,
  // The intensity goes up and down at the rate of a breath. `period_ms` is one whole
  // breath, in and out.
  kBreathe = 1,
  // On and off, half of the period each: the light that says "look here".
  kBlink = 2,
  // The Morse code of SOS, a dot at a time. `period_ms` is one dot, and one round of the
  // message is 34 dots with their gaps (docs/30).
  kSos = 3,
  // A ball that bounces faster and faster: every bounce is shorter than the one before
  // and reaches the top again, and when it cannot be quicker it starts over.
  kBounce = 4
};

// The four moments the system has a light for, in the order of who wins: a failure says
// more than an alarm, an alarm more than the radio, and the boot is what is left when
// nothing else is happening (docs/30).
enum class Trigger : uint8_t {
  kError = 0,
  kAlarm = 1,
  kRadio = 2,
  kBoot = 3
};

// One effect, as an app asks for it and as the system keeps it. Small on purpose: this
// is the whole of what the light knows and it lives in the kernel with no allocation
// (docs/75).
struct Effect {
  EffectKind kind = EffectKind::kNone;
  // The colour at the start of a cycle and at its end: an effect **ramps** from one to the
  // other across every cycle, which is what makes a breath drift and a bounce change tone
  // as it gets quicker. The two being equal is a light of one colour (docs/30).
  uint8_t red = 0;
  uint8_t green = 0;
  uint8_t blue = 0;
  uint8_t red2 = 0;
  uint8_t green2 = 0;
  uint8_t blue2 = 0;
  // One cycle of the effect: a breath in and out, an on and an off, one dot of the sos,
  // the first bounce. Clamped to a floor when it is too short to see.
  uint32_t period_ms = 0;
  // How many cycles before the light is given back. **Zero is forever**, until `stop` or
  // until the app goes (docs/30).
  uint32_t cycles = 0;
  // Which light of a strip, counted from zero, or -1 for the whole light. A board with
  // one light only has the whole light, and asking for another one is refused (docs/30).
  int32_t pixel = -1;
  // The brightest a channel of this effect may get. **Twenty by default**: a light that
  // says something does not have to be the brightest thing on the desk, and every milliamp
  // counts on a board that wants to sleep. An app that wants more asks for it, and the
  // board's ceiling is still what stops it (docs/30, docs/75).
  uint8_t level = 20;
};

// How many triggers there are, for an array and for the interpreter.
constexpr uint32_t kTriggerCount = 4;

// Reads the board's light. False when this board has none, and then every call here does
// nothing (docs/50).
bool boot(hal::IPlatform& platform);
bool available();

// How many lights the board has: one on the ATOM Lite, twenty-five on the Matrix.
uint32_t count();

// The highest a channel may be driven at on this board —255 where there is no limit, and
// the maker's number on a panel that heats up—. Nothing asked of the light goes past it:
// the panel is not the app's to burn (docs/30, docs/50).
uint32_t max_value();

// The radio is working, or it is not. This is the one state the rest of the kernel sets,
// and the one that lasts (docs/30).
void set_state(State state);

// The colour an app asked for while it is in front. It wins over the system layer and it
// is given back when the app goes — and given back means **off**, not "the colour it
// had": a light left on after its app has gone is power spent by nobody (docs/75).
// Asking for a colour while an effect plays stops the effect: the last thing asked for is
// what the light does (docs/30).
bool set_colour(uint8_t r, uint8_t g, uint8_t b);
void clear_colour();
bool has_colour();

// One light of the strip, for a board that has more than one. False when this board has
// no light or has no light at that index, and then nothing is painted (docs/30, docs/50).
bool set_pixel(uint32_t index, uint8_t r, uint8_t g, uint8_t b);

// An app's effect. It runs on the turn clock and never blocks; false when this board has
// no light, and then nothing is painted (docs/30).
bool play(const Effect& effect);

// Gives the app's layer back. Zero is now; anything else fades from what the light is
// showing to black over that many milliseconds and then hands it back, because a light
// that goes out in one frame in the middle of the night is a slap (docs/30).
void stop(uint32_t fade_ms);

// The effect the app is playing, and how long it has been playing for. False when the
// app's layer is empty (docs/30).
bool current_effect(Effect& out, uint32_t& elapsed_ms);

// Whether an app is playing one, which is what `led.caps().taken` answers together with
// the app's colour (docs/30).
bool playing();

// The system's four effects: what one is now, and how to change it or put the maker's
// back. A light that an app redefines is the app's business until it says otherwise
// (docs/30).
bool system_effect(Trigger trigger, Effect& out);
bool set_system_effect(Trigger trigger, const Effect& effect);
void restore_system_effect(Trigger trigger);

// A system moment: an alarm went off, or something failed. The light is taken **away from the
// app**, which is what an interruption is: the app's colour, its frame or its effect is kept
// exactly as it was, the moment says its piece, and then the light comes back and the app is
// told with `on_light()` so it can redraw — during the moment, whatever the app asked for went
// nowhere, because the light was not its own (docs/30).
//
// While a moment runs, `set_colour`, `set_pixel` and `play` are refused with `E_BUSY`: to touch
// the light by hand, nothing has to be animating it. A **person** at the prompt is the
// exception, and `take` is what the shell uses (docs/30).
void note(Trigger trigger);

// Whether a moment is being said right now, which is what makes the light busy (docs/30).
bool busy();

// True once, right after a moment ended and the app's light came back: it is the kernel's cue
// to call `on_light()` on the app, and it is a queue of one (docs/30).
bool released();

// The light, taken by a person: whatever the system or an app was saying stops, nothing is
// restored, and the colour is the one asked for. It is the shell's `led` command and it is
// how a person outranks a machine (docs/30).
bool take(uint8_t r, uint8_t g, uint8_t b);

// Milliseconds until the next frame is worth painting, or -1 when the light has nothing
// to say. The kernel cuts its wait to this, so an animation is as smooth as the turns
// allow and a light that is not moving does not keep the board awake (docs/30, docs/60).
int32_t next_frame_ms();

// Whether anything is being said right now: an effect playing, a fade, or a system
// moment. The power policy asks before it powers the board off, because a board that
// shuts down in the middle of an animation is a light that lies (docs/60).
bool animating();

// Paints when what has to be shown has changed, and not before: a frame is a write on
// the wire and the light is also a cost (docs/75).
void poll();

// What is on the light now, for the shell to show it.
void current(uint8_t& r, uint8_t& g, uint8_t& b);

// The names of the effects and of the triggers, for the interpreter and for the shell;
// nullptr when the number is not one of them (docs/30).
const char* effect_name(EffectKind kind);
bool effect_from_name(const char* name, EffectKind& out);
const char* trigger_name(Trigger trigger);
bool trigger_from_name(const char* name, Trigger& out);

}  // namespace led
}  // namespace kernel

#endif  // LUNOK_KERNEL_LED_H
