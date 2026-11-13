// The keys of the board: the one thing a person can press. The kernel counts
// the presses it needs for itself (three taps close the app, like three ESC on the
// console) and hands the same press to the app, as a promise if it is waiting for
// one and as a hook if it is not (docs/30, docs/50).
#ifndef LUNOK_KERNEL_BUTTON_H
#define LUNOK_KERNEL_BUTTON_H

#include <stdint.h>

#include "hal/hal.h"

namespace kernel {
namespace button {

// Reads the board's button. False when this board has none (docs/50).
bool boot(hal::IPlatform& platform);
bool available();

// Asked in every turn of the kernel: this is where a press becomes an event.
void poll();

// What it is doing right now, for the shell: the same reading the kernel gets in
// every turn, so a person can see the finger before blaming the app (docs/50).
uint32_t count();
bool down(uint32_t key);
int64_t held_ms(uint32_t key);

// Where a key is and how it is built, as the board described it: an app gives a
// function to a position and not to a number (docs/30, docs/50).
struct Info {
  uint32_t key;
  hal::Face face;
  hal::Spot spot;
  hal::KeyKind kind;
  uint32_t group;
};

bool info(uint32_t key, Info& out);

// An app asked for the next press. False when the board has no button, or when one
// is already on its way: an app has one promise slot and a second wait would be a
// promise nobody settles (docs/30).
bool wait_start(uint32_t timeout_ms);
void cancel();
bool waiting();

// How many taps in a row close the app, and the window they have to happen in. It
// is the same gesture the console has with three ESC, and the same window (docs/30).
uint32_t taps_to_close();

// ---- the queue of subscribers (0.180.0) ----
//
// **The keys emit events whether or not anybody listens**: `poll` is the only reader of the board and it always reads it,
// and what happens to the event is decided by whoever is in the queue at that moment (docs/30, docs/50).
//
// **Cascade and exclusive**: a cascade subscriber is called with the others, in the order it registered; an exclusive one
// is called **alone** —nobody else hears the event, the system's own delivery included— until it gives its handle back.
// The last exclusive to register is the one that answers, so a control that takes the keys sits on top of the app's queue
// the way a dialog sits on top of a page (docs/30, docs/90).
//
// **A listener is a plain function and an owner**, because the interpreter has to be able to hand one of its own without
// this module knowing that Lua exists (docs/10, docs/30).
typedef void (*Listener)(const char* kind, uint32_t key, int64_t held_ms, void* owner);

enum class Mode : uint8_t {
  // Called with the others, in the order it registered.
  kCascade = 0,
  // Called alone: the rest of the queue does not hear the event until it goes.
  kExclusive = 1,
};

// One event, as a caller that is not a listener sees it (the shell, the tests).
struct Event {
  const char* kind;  // "press" or "long"
  uint32_t key;
  int64_t held_ms;
};

// **A handle above zero, or zero when the queue is full**: a subscriber that could not register is told so and not given
// a number it cannot give back. `owner` is any pointer the listener gets back, and it is what `unsubscribe_owner` uses to
// take a whole owner out of the queue at once (docs/30, docs/75).
uint16_t subscribe(Listener listener, void* owner, Mode mode);
void unsubscribe(uint16_t handle);
// **An owner that goes takes its subscriptions with it**: an app that ends —or that ends early, on an error— is
// unregistered by the system and not by itself (docs/30).
void unsubscribe_owner(void* owner);
// How many are in the queue right now, for the shell and the tests (docs/30).
uint16_t subscribers();
// **The event goes to the queue and not to a function of this module**: the board calls it once per edge and the shell
// and the tests use it to put one through the same road (docs/30).
void emit(const char* kind, uint32_t key, int64_t held_ms);
// **Buida la cua i hi torna a deixar el sistema**: `boot` ho fa abans de llegir la placa i les proves ho fan entre cas, perquè
// una prova que hereta la cua d'una altra no prova res (docs/30, docs/85).
void reset();

}  // namespace button
}  // namespace kernel

#endif  // LUNOK_KERNEL_BUTTON_H
