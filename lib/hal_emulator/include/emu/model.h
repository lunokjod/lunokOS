// What the emulated model is made of, in **one** place (docs/50, docs/85).
//
// The emulator used to answer for whatever hardware the option said; now the model decides, and the
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
  // **The screen it draws on**: the IT8951 panel of the Paper, 960x540 and four bits per pixel. The
  // ATOMs of this family have no screen at all, and an emulator of one with a glass would be a board
  // nobody can buy (docs/50, docs/85).
  bool glass;
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
  // emulator may be told a smaller ceiling, never a bigger one: an emulator that hands out a megabyte is
  // an emulator that lies about the glass (docs/85).
  uint32_t heap_kb;
  // The **extra** memory of the board: what the chip says it has (`psram_chip_kb`) and how much of it
  // the CPU can address (`psram_kb`, which is all the system can hand out). On this family the second
  // is 4 MB even when the chip carries 8, because that is the size of the ESP32's window for external
  // RAM (docs/50, docs/75).
  uint32_t psram_chip_kb;
  uint32_t psram_kb;
  // **What the board has free at the prompt, and what its extra memory can hand out when nothing of the
  // system is in it**, in bytes, measured on the glass (docs/75). The emulator counts everything the system
  // and the apps take through `pal::extra_memory()` against these two, so the ceiling of a page and of an
  // app is the board's and not the PC's (docs/85). Zero is a home the board does not have.
  uint32_t free_heap_bytes;
  uint32_t extra_free_bytes;
  // What the board answers as the ceiling of an app (`IPlatform::heap_limit_kb`): its PSRAM on the
  // Paper, and **0 on a board that does not say**, which is the kernel's own ceiling. The emulator
  // answers this and not a number of its own (docs/75, docs/85).
  uint32_t app_limit_kb;
  // The data partition of the board, in bytes, from its own partition table: 0x0E0000 on the 4 MB
  // boards and 0xBE0000 on the Paper. It is what `fs.space` has to answer, because a directory of the
  // PC is not an answer an app can use (docs/70, docs/85).
  uint32_t disk_bytes;
  // **What the bootloader reads out of the flash before the kernel exists**, in bytes: the image this
  // board boots, as the build of the 3/11 left it (`-t size`: 2 097 008 B on the ATOM and 2 182 672 B on
  // the Paper). It is a number of the build and not of the chip, and it is here because turning the board
  // on is not free: the ROM reads the image from the SPI nor and the PSRAM test of ESP-IDF writes and
  // reads every byte the chip maps (docs/75, docs/85).
  uint32_t boot_image_bytes;
};

// The model this emulator is.
Model model();

// The hardware of a model, by name. It is what the command line asks before the options are
// installed, because `--mem-cap` has to be checked against the board that was asked for and not
// against the one in effect (docs/85).
Model model_of(const char* name);

// Whether this emulator is that model, by the name it is written with in `platformio.ini`.
bool model_is(const char* name);

}  // namespace emu

#endif  // LUNOK_EMU_MODEL_H
