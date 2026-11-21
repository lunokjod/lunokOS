// Log ring. Lives in RAM, never on flash: writing logs to a littlefs partition
// wears it out for nothing (docs/70).
#ifndef LUNOK_KERNEL_LOG_H
#define LUNOK_KERNEL_LOG_H

#include <stddef.h>

#include "pal/pal.h"

namespace kernel {

void log_init();
void log_echo(bool on);
bool log_echo_enabled();
size_t log_bytes();

// printf-style, **with the tag of whoever writes it**: the line comes out as `[I][FS] text`, the same
// shape the drivers use, so a log can be read and grepped as one thing (docs/30, docs/50).
void log_printf(pal::LogLevel level, const char* tag, const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

// **Una línia que una persona ha demanat i que no es filtra** (0.190.114, l'amo: *«Perqué no veig missatges d'NVS al log, sobretot a
// settings?»*). The level of the log is a person's choice about **traces**; it is not a choice about the answer to something that person has
// just done. A setting that is saved is that answer, o sigui que la seva línia —`[I][NVS] cfg/sort = on`— **no passa pel filtre** i es veu
// amb qualsevol nivell, com les frases de `log_say` però amb la forma de les traces perquè es pugui buscar la clau (docs/30, docs/70).
void log_markf(const char* tag, const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

// **A line of the system that is a sentence and not a record** (0.179.55): the shape of this log is `[I][TAG] text`, and a
// handful of lines of this tree are written for a person and not for a reader of tags —`button: three taps, closing the
// app`, `power: woke on the timer, +2 ms from the hour it was given`—. **They go to the ring like everything else**,
// because the serial port is a copy of the ring and a line that is only on the copy is a line the `log` command cannot
// show; they keep their words, which is what a person reads. The shell and the apps are the ones that do not come here:
// they use the port, and what they print is theirs (docs/30, docs/50).
void log_say(const char* text);

// **El llindar del registre** (0.190.89, l'amo: *«Afegeix "Log level" a Settings->System Amb els nivells de log, per defecte Debug (com la
// comanda log)»*). Una línia per sota del llindar **no existeix**: no arriba al ring ni al mirall del port sèrie, perquè el que una persona
// tria amb aquesta fila és quantes línies vol veure i no quines vol que existeixin. El de sèrie és `Debug`, que és el que el sistema ha
// fet sempre —tot—; `Info` deixa fora les línies de feina (`[D]`), `Warn` les que diuen el que passa i `Error` només les que fallen. **El
// filtre és a la porta única del registre** (`emit`), o sigui que val igual per a un driver, per a `log_printf` i per a una frase: els
// tres camins arriben al mateix lloc (docs/30, docs/50).
void log_set_level(pal::LogLevel level);
pal::LogLevel log_level();
// Les paraules de la fila, que són les del `nvs` i no els números del codi.
const char* log_level_name(pal::LogLevel level);
bool log_level_from_name(const char* name, pal::LogLevel* out);

void log_dump();

}  // namespace kernel

#endif  // LUNOK_KERNEL_LOG_H
