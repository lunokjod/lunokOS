// The light of the board: the policy of what it says, and nothing of how it is
// painted (docs/30, docs/50).
#include "kernel/led.h"

#include <stddef.h>

#include "luabind/luabind.h"

namespace kernel {
namespace led {
namespace {

hal::ILed* g_led = nullptr;
State g_state = State::kOff;

// The colour an app asked for. It wins over the state while the app is in front.
bool g_has_colour = false;
uint8_t g_red = 0;
uint8_t g_green = 0;
uint8_t g_blue = 0;

// An app that painted the panel light by light owns a **frame** and not a colour.
// The frame itself lives in the board, where the strip is: a copy here would be
// another buffer to keep in step with the one that goes on the wire (docs/50,
// docs/75). The kernel only has to know whose it is and whether it is painted yet.
bool g_painted = false;
bool g_dirty = false;

// What the board is showing right now, so a frame is only sent when it changes.
uint8_t g_shown_red = 0;
uint8_t g_shown_green = 0;
uint8_t g_shown_blue = 0;

struct Rgb {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
};

// Dim on purpose: a light that says something does not have to be the brightest
// thing on the desk, and every milliamp counts on a board that wants to sleep
// (docs/75).
Rgb colour_of(State state) {
  switch (state) {
    case State::kRadio:
      return {0, 24, 0};
    case State::kOff:
    default:
      return {0, 0, 0};
  }
}

}  // namespace

bool boot(hal::IPlatform& platform) {
  g_led = platform.led();
  g_state = State::kOff;
  g_has_colour = false;
  g_painted = false;
  g_dirty = false;
  g_shown_red = 0;
  g_shown_green = 0;
  g_shown_blue = 0;

  // What an app may ask for. Without a light the operations stay absent and led.*
  // answers E_UNSUPPORTED instead of pretending (docs/30).
  luabind::LedOps ops = {};
  if (g_led != nullptr) {
    ops.count = &count;
    ops.max_value = &max_value;
    ops.set_colour = &set_colour;
    ops.set_pixel = &set_pixel;
    ops.clear_colour = &clear_colour;
    ops.has_colour = &has_colour;
  }
  luabind::set_led_ops(ops);

  if (g_led == nullptr) return false;
  // The light starts off: a chip that restarted with a lit light would be saying
  // something the new boot has not decided (docs/50).
  g_led->fill(0, 0, 0);
  g_led->show();
  return true;
}

bool available() { return g_led != nullptr; }

uint32_t count() { return (g_led != nullptr) ? g_led->count() : 0; }

uint32_t max_value() { return (g_led != nullptr) ? g_led->max_value() : 0; }

namespace {

// A channel of this board's light. The ceiling is a hardware limit and the policy
// is what enforces it: an app that asks for 255 on the panel of the Matrix gets the
// maker's number and not an LED hot enough to damage the case (docs/30, docs/50).
uint8_t channel(uint8_t value) {
  const uint32_t ceiling = max_value();
  return (value > ceiling) ? static_cast<uint8_t>(ceiling) : value;
}

}  // namespace

void set_state(State state) { g_state = state; }

bool set_colour(uint8_t r, uint8_t g, uint8_t b) {
  if (g_led == nullptr) return false;
  g_has_colour = true;
  // One colour is the whole light again, even if the app had painted a frame
  // before: the last thing it asked for is what it gets (docs/30).
  g_painted = false;
  g_dirty = true;
  g_red = channel(r);
  g_green = channel(g);
  g_blue = channel(b);
  return true;
}

bool set_pixel(uint32_t index, uint8_t r, uint8_t g, uint8_t b) {
  if (g_led == nullptr) return false;
  if (!g_led->pixel(index, channel(r), channel(g), channel(b))) return false;
  g_has_colour = true;
  g_painted = true;
  g_dirty = true;
  return true;
}

void clear_colour() {
  // A board with no light has nothing to give back, and this is the step that has to
  // cost nothing there (docs/30).
  if (g_led == nullptr) return;
  g_has_colour = false;
  g_painted = false;
  // Also a painted frame has to be given back: leaving the app's lights on would be
  // a light that outlives its app (docs/75).
  g_dirty = true;
}

bool has_colour() { return g_has_colour; }

void poll() {
  if (g_led == nullptr) return;
  // A frame the app painted light by light goes out when it changed, and the state's
  // colours do not overwrite it while the app is in front (docs/30).
  if (g_has_colour && g_painted) {
    if (!g_dirty) return;
    g_led->show();
    g_dirty = false;
    return;
  }
  // The state's colours go through the same ceiling as an app's: the radio says the
  // same thing on every board, and what a board can take is not the radio's to
  // know (docs/30, docs/50).
  const Rgb asked = g_has_colour ? Rgb{g_red, g_green, g_blue} : colour_of(g_state);
  const Rgb want = Rgb{channel(asked.red), channel(asked.green), channel(asked.blue)};
  const bool same = want.red == g_shown_red && want.green == g_shown_green &&
                    want.blue == g_shown_blue;
  if (same && !g_dirty) return;
  g_led->fill(want.red, want.green, want.blue);
  g_led->show();
  g_shown_red = want.red;
  g_shown_green = want.green;
  g_shown_blue = want.blue;
  g_dirty = false;
}

void current(uint8_t& r, uint8_t& g, uint8_t& b) {
  r = g_shown_red;
  g = g_shown_green;
  b = g_shown_blue;
}

}  // namespace led
}  // namespace kernel
