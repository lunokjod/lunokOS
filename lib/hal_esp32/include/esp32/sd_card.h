// A card in the board's SD slot, mounted as a filesystem.
//
// The ESP32 has an SD host and a SPI host; what it does not have is a slot. The board
// says the four pins and which SPI host, and the mounting and the file calls are here
// (docs/50).
//
// On the Paper the slot **shares SCK, MOSI and MISO with the e-ink panel**: one bus and
// two devices. This file takes the bus when nobody has it and leaves it up, so the panel
// driver adds its own device to the same host; the transfers that must not interleave
// take a lock between the two (docs/90, step 7).
#ifndef LUNOK_ESP32_SD_CARD_H
#define LUNOK_ESP32_SD_CARD_H

#include "hal/hal.h"

namespace hal {

// What a slot needs: the four wires, the SPI host they belong to (`SPI2_HOST` or
// `SPI3_HOST`, from driver/spi_master.h), and the fastest clock the board's wiring takes.
struct SdPins {
  int cs;
  int sck;
  int mosi;
  int miso;
  int host;
  // 0 means SDMMC_FREQ_DEFAULT (20 MHz). The board lowers it when the wires are shared or
  // long: a card that mounts and then times out on a read is a card being driven too
  // fast, not a broken card (docs/90).
  int max_khz;
};

// The card at `mount` (the VFS asks for "/sd"), or nullptr when there is no card, it
// does not answer, or its filesystem is not one this build reads. One attempt per boot:
// the slot has no card-detect line here, and a mount that failed leaves FATFS in a state
// that is not reused without unmounting (docs/70).
IStorage* esp32_sd(const char* mount, const SdPins& pins);

}  // namespace hal

#endif  // LUNOK_ESP32_SD_CARD_H
