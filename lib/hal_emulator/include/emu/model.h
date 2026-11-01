// What the emulated model is made of, in **one** place (docs/50, docs/85).
//
// The bench used to answer for whatever hardware the option said; now the model decides, and the
// model is the one `--board` names. The rest of the emulator asks here instead of testing board
// names again, because two lists of what a board has are two truths and one of them ages.
#ifndef LUNOK_EMU_MODEL_H
#define LUNOK_EMU_MODEL_H

#include <stdint.h>

namespace emu {

struct Model {
  const char* name;      // the name it answers to, the same one `sys.board` gives
  uint32_t lights;       // 0 is a board with no light at all
  uint32_t keys;         // one button, or the Paper's three
  bool imu;              // a sensor
  bool env;              // an ambient sensor: temperature and humidity (docs/50)
  // **A touch panel**: the glass that a finger goes through, which on this board is a GT911 that
  // counts in the page's own frame and reports two fingers (docs/50, docs/85).
  bool touch;
  bool gauge;            // a battery it can read
  bool slot;             // a card slot
  bool fram;             // an FRAM chip, which is non-volatile memory
  bool keeps_the_hour;   // a clock chip with a coin cell behind it
  // A board that can cut its own supply: the Paper holds its power on a pin and letting it go is
  // the board stopping. The ATOMs have no hold and no way to switch themselves off between the cell
  // and the chip, so they answer no (docs/50, docs/85).
  bool powers_off;
  // The memory the board really has for the system, in KB. **320** on every ESP32 of this family:
  // the chip's 520 KB of SRAM are not all heap, and 320 is what a program can use (docs/75). The
  // bench may be told a smaller ceiling, never a bigger one: a bench that hands out a megabyte is a
  // bench that lies about the glass (docs/85).
  uint32_t heap_kb;
  // The **extra** memory of the board: what the chip says it has (`psram_chip_kb`) and how much of it
  // the CPU can address (`psram_kb`, which is all the system can hand out). On this family the second
  // is 4 MB even when the chip carries 8, because that is the size of the ESP32's window for external
  // RAM (docs/50, docs/75).
  uint32_t psram_chip_kb;
  uint32_t psram_kb;
  // What the board answers as the ceiling of an app (`IPlatform::heap_limit_kb`): its PSRAM on the
  // Paper, and **0 on a board that does not say**, which is the kernel's own ceiling. The bench
  // answers this and not a number of its own (docs/75, docs/85).
  uint32_t app_limit_kb;
  // The data partition of the board, in bytes, from its own partition table: 0x0E0000 on the 4 MB
  // boards and 0xBE0000 on the Paper. It is what `fs.space` has to answer, because a directory of the
  // PC is not an answer an app can use (docs/70, docs/85).
  uint32_t disk_bytes;
};

// The model this bench is.
Model model();

// The hardware of a model, by name. It is what the command line asks before the options are
// installed, because `--mem-cap` has to be checked against the board that was asked for and not
// against the one in effect (docs/85).
Model model_of(const char* name);

// Whether this bench is that model, by the name it is written with in `platformio.ini`.
bool model_is(const char* name);

}  // namespace emu

#endif  // LUNOK_EMU_MODEL_H
