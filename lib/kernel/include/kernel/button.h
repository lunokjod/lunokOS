// The button: the one thing a person can press on this board. The kernel counts
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
bool down();
int64_t held_ms();

// An app asked for the next press. False when the board has no button, or when one
// is already on its way: an app has one promise slot and a second wait would be a
// promise nobody settles (docs/30).
bool wait_start(uint32_t timeout_ms);
void cancel();
bool waiting();

// How many taps in a row close the app, and the window they have to happen in. It
// is the same gesture the console has with three ESC, and the same window (docs/30).
uint32_t taps_to_close();

}  // namespace button
}  // namespace kernel

#endif  // LUNOK_KERNEL_BUTTON_H
