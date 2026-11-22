#ifndef LUNOK_KERNEL_KERNEL_H
#define LUNOK_KERNEL_KERNEL_H

#include "hal/hal.h"
#include "pal/pal.h"

namespace kernel {

// Brings the system up on top of a board. Call after pal::init().
void boot(hal::IPlatform& platform);

// One turn of the main loop: wait for an event, dispatch it, drain the console.
void poll();

// **Si el darrer son ha estat una arrencada del xip, i es pregunta una vegada per torn** (0.190.68): a la placa un son profund no torna
// —el xip es reinicia i qui ve després és un `boot`—, i el model de l'emulador no pot reiniciar el procés, o sigui que `poll` deixa el
// torn quan això és cert i **qui porta l'executable fa el `boot` que el xip faria sol** (`main_host`; el flag el neteja el mateix `boot`).
// El que això arregla és el forat que l'amo va assenyalar: un emulador que seguia amb el run d'abans d'un son profund no tornava a
// llegir la marca del son, ni el rellotge, ni la sessió desada, i la pàgina de la persona no tornava mai (docs/50, docs/60, docs/85).
bool the_chip_restarted();

// **Què ha despertat la volta** (0.189.85): the two counts `stats` prints beside `kernel.loop`. A board that woke up twenty
// times was doing something; one that woke up five thousand was asking five thousand times whether anything had happened
// (docs/95).
void turns(uint32_t& with_event, uint32_t& on_timeout);

bool running();
void shutdown();

// The board the kernel is running on. Valid after boot().
hal::IPlatform& platform();

// **El pany del torn** (0.190.244): the glass, the interpreter and the app belong to the turn, and since the shell is a task of its own a line
// can arrive in the middle of a turn. This lock makes the two take turns instead of touching the same panel: the turn holds it for the whole
// turn and the shell holds it around the line it executes, o sigui que el que era cert quan la shell vivia dins del torn continua essent cert
// (docs/30, docs/50). On the PC there is no task and the lock is the null one the host PAL makes.
pal::Lock the_turn();
void take_the_turn();
void give_the_turn();

// Runs one command line as if it had been typed. Used by the emulator's batch
// mode and by the tests. Returns false when the command failed or was unknown.
bool execute(const char* line);

// True while an app started from the shell is alive (it may be waiting for a
// promise between turns). Whoever drives the loop by hand has to keep calling
// poll() until this says false.
bool app_running();

// Reads the persistent alarms from the NVS and puts them back in the queue. It
// runs at boot, and the ones whose time has passed are fired by the next turns
// with their late_ms (docs/60). Exposed so a test can do a "reboot" without one.
void alarms_restore();

}  // namespace kernel

#endif  // LUNOK_KERNEL_KERNEL_H
