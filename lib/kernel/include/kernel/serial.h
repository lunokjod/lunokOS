// **El port sèrie cedit a qui el vol** (0.189.20, l'amo: *«Crea una crida i comanda per tancar i obrir el port serie al
// sistema (l'app pren el control del port serie) i unaltra crida/comanda per alliberar el serial. Al tancar les apps s'ha
// de comprovar per alliberar-lo, com es fa amb els leds dels m5atom. Quan el port serie està reclamat per una app el
// sistema no hi escriu ni llegeix, deixant el canal de comunicacions disponible per el que l'usuari necessiti»*).
//
// The serial port of a board is two things at once: the way into a board with no glass —the prompt, the log, the echo— and
// a wire that an app may need whole. This module is the switch between the two, and it is **one flag and one owner**:
// while the port is ceded, **the system writes nothing to it** (no prompt, no log echo, no table of `stats`), so an app
// that talks to something on the other end of the cable has a channel that nobody interrupts; and the app's own words —
// `console.print`, `sys.read_line`, the `term` escapes— still go there, because the app is exactly who has it.
//
// **Who reads**: the system reads the port once per turn (`kernel::poll`) and `shell::feed` hands what arrives to the app
// that is in front, which takes nothing for itself (0.160.0). That is already "the system does not read": one port and one
// reader, and while an app is in front the bytes are the app's. What the claim adds is the silence on the way back. A
// person who cedes the port from the prompt keeps a keyboard —and a line editor that echoes nothing— precisely so that
// `serial release` can be typed, because a board whose only way in has gone quiet with no way back is a board that has to
// be unplugged (docs/30, docs/50).
#ifndef LUNOK_KERNEL_SERIAL_H
#define LUNOK_KERNEL_SERIAL_H

#include <stddef.h>
#include <stdint.h>

#include "pal/pal.h"

namespace kernel {
namespace serial {

// **El port passa a ser d'un altre** (0.189.20): `by` is the id of the app that asked —or `"the prompt"` for a person— and
// it is remembered so that only that one can give it back. Answers false when somebody else has it, and then the caller
// says who: two owners of one wire is exactly the mess this door exists to avoid (docs/30).
bool claim(const char* by, const char* why);

// **I torna al sistema**: anything that holds it may give it back, and `why` is what the log says. Answers whether it was
// ceded at all, because giving back what nobody took is not a fault (docs/30).
bool release(const char* why);

// **I quan una app s'acaba, com el llum** (0.189.20): the claim of an app goes with it —a wire nobody is using but nobody
// else may have is the fault the LED rule exists for— and the claim of a person, or of another app, is not touched. It is
// what `close_app` asks, and the id is the one the interpreter had (docs/30, docs/90).
bool release_owner(const char* by, const char* why);

// True while the port is ceded to somebody.
bool claimed();

// Who has it: the empty string when it is the system's, `"the prompt"` or the id of an app.
const char* owner();

// **La consola que el SISTEMA pot fer servir** (0.189.20): the port itself, or **nullptr while it is ceded**. It is the one
// question every writer of the system asks before it writes —the prompt's words, the log's echo, the tables of a command—
// and the `!= nullptr` that was already there is what makes the silence free: an app's own writes do not come through
// here, they come through the interpreter's console, which keeps the port (docs/30, docs/50).
pal::IConsole* console();

}  // namespace serial
}  // namespace kernel

#endif  // LUNOK_KERNEL_SERIAL_H
