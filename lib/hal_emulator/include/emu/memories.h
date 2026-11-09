// The memories of the emulated board that are not a volume (docs/85).
//
// They are **binary files of the size of the real thing**, and they are kept between runs the way
// the memory of a device is:
//
//   - the **RTC RAM** (8 KB, the RTC slow memory of the ESP32). It survives a reset and a deep
//     sleep, and it dies with the power —unless the board carries a clock chip with a coin cell
//     behind it, which is the difference between the ATOM and the Paper (docs/50);
//   - the **FRAM** (256 B, the FM24C02 of the Paper). Ferroelectric memory is not volatile: it
//     survives the power going away, which is why it is there at all.
//
// A file that is not in the state directory is created at its exact size, so what a person sees on
// the PC is the memory the board has and not a log of it.
#ifndef LUNOK_EMU_MEMORIES_H
#define LUNOK_EMU_MEMORIES_H

#include <stddef.h>
#include <stdint.h>

namespace emu {

// The RTC memory a program can own, in bytes: 8 KB in the chip and 7 KB that the ROM and the IDF
// leave to the application (measured on the ATOM, 31/10: all 8 KB overflowed by 536 bytes). The emulator
// gives the same number the board gives, so a write that fits on the PC fits on the glass (docs/85).
const size_t kRtcRamBytes = 7 * 1024;
// The FM24C02 of the Paper, in bytes (docs/50).
const size_t kFramBytes = 256;

// The bytes of each memory, loaded from `<state_dir>/rtc.bin` and `<state_dir>/fram.bin` and created
// at the right size when they are not there. `fram()` is **nullptr** on a board with no FRAM, the
// same as a board that has no such chip: the emulator does not hand out hardware the model lacks
// (docs/50, docs/85). A memory that cannot be opened is a blank one, and the system says so instead
// of crashing (the same way a chip that does not answer is a chip with no hour).
uint8_t* rtc_ram();
uint8_t* fram();

// Writes back what the model has. It is called when the emulator leaves, and it is what a person
// reads with a hex viewer to see what the board kept.
void flush_memories();

// **The hour the board keeps**, in milliseconds since the epoch, or 0 when the emulator never knew one.
// A board whose hour lives in a chip with a coin cell behind it comes back knowing the hour, and so
// does a run that said `--persist` —which is a board that was not unplugged—, so the number lives
// next to the memories as `<state_dir>/clock.bin` and not in the RTC RAM: the chip keeps it even when
// the RAM of the chip is gone (docs/50, docs/60, docs/85).
int64_t load_clock_ms();
void save_clock_ms(int64_t ms);

// What a power cycle does to the memories, following the hardware: the RTC RAM starts at zero again
// unless the board keeps its hour in a chip with a battery behind it, and the FRAM is never wiped
// because it is not volatile — it is exactly the memory that is not. A model with no FRAM gets no
// `fram.bin` at all.
void power_cycle();

}  // namespace emu

#endif  // LUNOK_EMU_MEMORIES_H
