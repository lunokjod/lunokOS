// The memory of the chip that is behind its bank switcher, which the IDF calls `himem` (docs/50, docs/75).
//
// The ESP32's window for external RAM is four megabytes, so a board that carries eight of PSRAM —the
// Paper, the T-Koala— cannot address half of them. That half is not lost: the chip can swap ranges of
// it into a stretch of the same window reserved for that, and that is what `esp_himem_*` drives (the
// IDF's own `system/himem` example). What it cannot do is hand out a pointer that stays valid, so it is
// **not a heap**: it is a place where a large thing is kept and read back a block at a time. Giving it
// to `pal::IExtraMemory` would be giving an app a pointer that moves under it (docs/50).
#ifndef LUNOK_ESP32_EXTRA_MEMORY_H
#define LUNOK_ESP32_EXTRA_MEMORY_H

#include <stdint.h>

#include "hal/hal.h"

namespace hal {

// The chip's own bank switcher, as the board has it. Every field is zero on a chip that has none —the
// ESP32-S3 has no such window— or has no PSRAM at all, which is the same answer to the question of how
// much of that is there (docs/50, docs/75).
ExtraMemory esp32_extra_memory();

// **La mida que el xip diu que té**, que és la del datasheet i no la que el ESP32 pot adreçar: 8 MB on the Paper and
// the Koala, of which only four fit in the window (docs/50, docs/75). Zero when there is no PSRAM at all.
uint32_t esp32_chip_psram_bytes();

// **Escriu i torna a llegir** `bytes` de dalt de tot, de bloc en bloc de 32 KB, i diu si el que torna és
// el que hi va anar. `bytes` ha de ser un múltiple de `kExtraMemoryBlockBytes` i no passar-se del que hi ha
// lliure; `false` vol dir que aquest xip no en té o que no n'hi ha prou —la mateixa resposta per a qui ho
// pregunta (docs/75).
bool esp32_extra_memory_round_trip(uint32_t bytes);

// **Una regió del banc per a qui la demani** (0.190.174): the kernel keeps the caches of the sessions there, and it needs a
// door and not a pointer. **One at a time**, because the chip has one window and a second region would mean a second window
// that the chip does not have; the ask that comes second answers `nullptr` and stays in the PSRAM, which is the right price
// while one app is on the glass (docs/50, docs/75).
IBankedMemory* esp32_open_banked_memory(uint32_t bytes);
void esp32_close_banked_memory(IBankedMemory* block);

}  // namespace hal

#endif  // LUNOK_ESP32_EXTRA_MEMORY_H
