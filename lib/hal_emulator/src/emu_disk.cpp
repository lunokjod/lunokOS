#include "emu/disk.h"


#include "emu/options.h"
#include "pal/pal.h"

namespace emu {
namespace {

// **The flash of the chip.** A SPI nor at **40 MHz** —the data clock of this chip—, littlefs in blocks
// of **4 KB**, page programmes of 256 B at **0.4 ms** and a sector erase of **45 ms**, which is the
// maker's typical for a W25Q. A block reads in 819 us of wire; a block is written as sixteen page
// programmes and needs its sector erased first, and there is where the seconds of writing a file come
// from. **littlefs does not write over what is there**: a commit takes a fresh block, so even a small
// write pays a sector (docs/50, docs/85).
const DiskTiming kFlash = {60, 4096, 819, 6400, 45000};
// **The card in the slot.** FATFS over the SPI bus at **10 MHz** (docs/50): a sector of 512 B is 410 us
// of wire, a command the card answers in about 200 us, and writing a sector is the card's own time,
// which is slower than reading it.
const DiskTiming kCard = {200, 512, 410, 1500, 0};
// **The FRAM.** An I2C chip at **100 kHz** and nine bits a byte: ninety microseconds a byte, and no wait
// after a write, because a ferroelectric cell takes it as fast as the bus can send it (docs/50).
const DiskTiming kFram = {40, 1, 90, 90, 0};
// The RAM of the chip. A byte of it is a byte of RAM, and the bench does not charge for the CPU (docs/85).
const DiskTiming kMemory = {0, 0, 0, 0, 0};

uint32_t blocks_of(uint32_t bytes, uint32_t block) {
  if (block == 0 || bytes == 0) return 0;
  return (bytes + block - 1) / block;
}

}  // namespace

DiskTiming disk_timing(Disk disk) {
  switch (disk) {
    case Disk::kFlash:
      return kFlash;
    case Disk::kCard:
      return kCard;
    case Disk::kFram:
      return kFram;
    case Disk::kNone:
      break;
  }
  return kMemory;
}

void disk_pay(Disk disk, uint32_t bytes, bool writing) {
  if (!options().delays) return;
  const DiskTiming timing = disk_timing(disk);
  const uint32_t blocks = blocks_of(bytes, timing.block_bytes);
  uint32_t us = timing.command_us;
  us += blocks * (writing ? timing.write_block_us : timing.read_block_us);
  if (writing && blocks > 0 && timing.erase_us > 0) us += timing.erase_us;
  if (us == 0) return;
  pal::delay_us(us);
}

}  // namespace emu
