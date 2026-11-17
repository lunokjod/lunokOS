// **La pantalla que es tapa** (0.189.150): the lock app of the system, when it runs and who counts the minutes.
//
// The page itself is an app of the disk —`device_lock` of series— and this module only decides **when** it comes to the
// front: at the start of the board, when it comes back from a sleep, and after a while with nobody touching it. It is a
// module of its own and not a corner of `power` because what it measures is the hand of a person (a key, a finger, a byte
// on the console) and what it does is start an app, which is the shell's business (docs/30, docs/40, docs/60).
#ifndef LUNOK_KERNEL_LOCK_H
#define LUNOK_KERNEL_LOCK_H

#include <stdint.h>

namespace kernel {
namespace lock {

// **The start of the board**: the counter is set to nothing-has-happened-yet and the page goes up if the switch of
// `cfg::lock_on` says so. It is called at the end of the boot, after the home app, so that opening the lock and closing it
// gives the person the page they had and not the prompt (docs/30, docs/90).
void boot();

// **A hand on the board**: a key that goes down, a finger on the glass, a byte on the console. Every one of them starts the
// counter of `lock after` again, which is the whole rule the owner asked for: *«Aquest comptador torna a començar quant
// l'usuari deixa d'interactuar amb la placa, no toca botons físics ni pantalla durant aquest temps»* (docs/30, docs/60).
void note_activity();

// **A sleep has come back**: the page goes up at the next turn. It is a flag and not a call because a light sleep returns
// inside `power::sleep`, with the kernel in the middle of a turn, and starting an app there would close the one in front
// from inside its own pause (docs/30, docs/60).
void note_a_wake();

// The turn: the flag of the wake and the counter of the idle minutes, one comparison each while the lock is off.
void poll();

// Whether the page of the lock is the one in front, which is what keeps the counter from asking for it twice.
bool locked();

// The page this module runs, or an empty text when `cfg` names none.
const char* app();

}  // namespace lock
}  // namespace kernel

#endif  // LUNOK_KERNEL_LOCK_H
