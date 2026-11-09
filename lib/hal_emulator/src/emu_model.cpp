// The hardware of each emulated model, and the only list of it (docs/50, docs/85).
#include <string.h>

#include "board/identity.h"
#include "emu/model.h"

namespace emu {

bool model_is(const char* name) { return strcmp(board::id(), name) == 0; }

Model model() { return model_of(board::id()); }

Model model_of(const char* name) {
  Model out = {};
  out.name = name;
  const bool paper = (name != nullptr) && strcmp(name, "m5stack-paper") == 0;
  const bool matrix = (name != nullptr) && strcmp(name, "m5stack-atom-matrix") == 0;
  const bool tkoala = (name != nullptr) && strcmp(name, "ttgo-t-koala") == 0;
  if (paper) {
    // Three keys on the dial, no light, no sensor, and the extra memory the board really carries:
    // the gauge, the card slot, the FRAM and the clock chip with a coin cell behind it (docs/50).
    out.lights = 0;
    out.keys = 3;
    out.imu = false;
    // The SHT30 of this board, on the internal I2C next to the clock and the FRAM (docs/50).
    out.env = true;
    // The panel of this board, and the GT911 on top of it, on the same bus as the clock and the FRAM
    // (docs/50).
    out.glass = true;
    out.touch = true;
    out.gauge = true;
    out.slot = true;
    out.fram = true;
    out.keeps_the_hour = true;
    out.powers_off = true;
    out.heap_kb = 320;
    // The chip has 8 MB of PSRAM and the ESP32 maps **4** of them: what the system counts is the
    // mapped region, because the other half needs the bank-switched `himem` nobody uses (docs/50,
    // docs/75).
    out.psram_chip_kb = 8192;
    out.psram_kb = 4096;
    // **The free heap the board reports at the prompt and the PSRAM it can hand out**, measured on the
    // glass on 31/10 (docs/75): the Paper pays the card, the I2C, the clock and its three keys, so it
    // has half the RAM an ATOM has, and its 8 MB of PSRAM are **4 095** usable and not 4 096 because
    // the allocator eats the region's own bookkeeping. An app that asks for the whole 4 MB dies at
    // 4 095, which is what happens on the board and not a rounded number (docs/75, docs/85).
    out.free_heap_bytes = 162728;
    out.extra_free_bytes = 4095u * 1024u;
    out.app_limit_kb = 4096;  // what the board answers as the ceiling of an app
    out.disk_bytes = 0xBE0000;  // the littlefs partition of partitions_16mb.csv
    out.boot_image_bytes = 2182672;  // the image of the paper environment, 3/11 (docs/85)
    return out;
  }
  if (tkoala) {
    // The LilyGO TTGO T-Koala: one plain green LED, a card slot and the WROVER-B's 8 MB of PSRAM of which
    // the ESP32 maps 4. No glass, no touch, no sensor, no gauge and no key: its only button is RST (docs/50).
    out.lights = 1;
    out.keys = 0;
    out.imu = false;
    out.env = false;
    out.glass = false;
    out.touch = false;
    out.gauge = false;
    out.slot = true;
    out.fram = false;
    out.keeps_the_hour = false;
    out.powers_off = false;
    out.heap_kb = 320;
    out.psram_chip_kb = 8192;
    out.psram_kb = 4096;
    // **The ATOM's free heap and the Paper's mapped PSRAM**: the same chip and the same external window,
    // but nobody has taken the T-Koala's own numbers on the board yet. docs/95 has the command and this
    // pair is replaced the day they exist (docs/75, docs/85).
    out.free_heap_bytes = 338864;
    out.extra_free_bytes = 4095u * 1024u;
    out.app_limit_kb = 4096;  // what the board answers as the ceiling of an app
    out.disk_bytes = 0x160000;  // the littlefs partition of partitions.csv
    out.boot_image_bytes = 2286000;  // the image of the tkoala environment, 5/11 (docs/85)
    return out;
  }
  if (matrix) {
    // The panel of twenty-five and the sensor, one button, no cell and no slot (docs/50).
    out.lights = 25;
    out.keys = 1;
    out.imu = true;
    out.env = false;
    out.glass = false;
    out.touch = false;
    out.gauge = false;
    out.slot = false;
    out.fram = false;
    out.keeps_the_hour = false;
    out.powers_off = false;
    out.heap_kb = 320;
    out.psram_chip_kb = 0;
    out.psram_kb = 0;
    // The ATOM's free heap with the prompt and Lua in the binary (docs/75): the Matrix is the same chip
    // and the same firmware with a panel of twenty-five on it, and nobody has taken its number on its
    // own, so the ATOM's is used and docs/75 says that it is (docs/85).
    out.free_heap_bytes = 338864;
    out.extra_free_bytes = 0;  // no PSRAM on this board
    out.app_limit_kb = 0;  // the board does not say: the kernel's own ceiling, 64 KB
    out.disk_bytes = 0x0E0000;  // the littlefs partition of partitions.csv
    // The Matrix is the ATOM's chip and the ATOM's image with a panel of twenty-five in it: nobody has
    // taken its size on its own, so the ATOM's is used and docs/85 says that it is.
    out.boot_image_bytes = 2097008;  // the image of the atom environment, 3/11 (docs/85)
    return out;
  }
  // The ATOM: one light, one button, and nothing else of its own. A name that is not one of the
  // models is checked when `--board` is parsed, so this is the honest answer for an emulator built by
  // hand as well (docs/85).
  out.lights = 1;
  out.keys = 1;
  out.imu = false;
  out.env = false;
  out.glass = false;
  out.touch = false;
  out.gauge = false;
  out.slot = false;
  out.fram = false;
  out.keeps_the_hour = false;
  out.powers_off = false;
  out.heap_kb = 320;
  out.psram_chip_kb = 0;
  out.psram_kb = 0;
  out.free_heap_bytes = 338864;  // the ATOM's, measured the 30/10 (docs/75)
  out.extra_free_bytes = 0;
  out.app_limit_kb = 0;
  out.disk_bytes = 0x0E0000;
  out.boot_image_bytes = 2097008;  // the image of the atom environment, 3/11 (docs/85)
  return out;
}

}  // namespace emu
