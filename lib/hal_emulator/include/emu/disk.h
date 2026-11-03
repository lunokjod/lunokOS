// **What each storage device of the bench costs, in microseconds** (docs/50, docs/85).
//
// On the board nothing of the disk is free: a flash read is a block out of the SPI nor, a card read is a
// sector at ten megahertz after a command the card has to answer, and the FRAM is an I2C chip where **a
// byte is ninety microseconds**. One flat millisecond a read was the bench's guess; this is the table of
// what each device really does, and one function that pays it —or does not, when the run asked for no
// waits (docs/85).
#ifndef LUNOK_EMU_DISK_H
#define LUNOK_EMU_DISK_H

#include <stdint.h>

namespace emu {

enum class Disk : uint8_t {
  kNone = 0,  // memory: the RTC RAM of the chip
  kFlash,     // the littlefs partition: SPI nor, reads of 4 KB, pages of 256 B, sectors of 4 KB
  kCard,      // the card in the slot: FATFS over SPI at 10 MHz, sectors of 512 B
  kFram,      // the FM24C02: I2C at 100 kHz, and a byte is nine bits on the wire
};

struct DiskTiming {
  uint32_t command_us;       // one transaction: the command, the header, the driver's own frames
  uint32_t block_bytes;      // the unit a read moves
  uint32_t read_block_us;    // and what moving one of them costs
  uint32_t write_unit_bytes; // the unit a write programmes: a flash page of 256 B, a card sector
  uint32_t write_unit_us;    // and the maker's typical for one of them
  uint32_t erase_us;         // the sector a written block needs, which only flash has
};

DiskTiming disk_timing(Disk disk);

// **The time of one operation, paid here and nowhere else**: the transaction and the blocks of its
// bytes and, when it writes, the programming and the sector it took. Nothing is paid when the run is not
// paying the times of the hardware (`--no-delay`, every test), which is the same switch as the rest of
// the bench (docs/85).
void disk_pay(Disk disk, uint32_t bytes, bool writing);

// **A write that does not take the sector it writes into**: the NVS appends thirty-two-byte entries to a
// page that is already blank and erases the page only when it fills, which is not what littlefs does with
// its commits. `erasing` says which of the two this is, and the rest of the price is the same (docs/70,
// docs/85).
void disk_pay_write(Disk disk, uint32_t bytes, bool erasing);

}  // namespace emu

#endif  // LUNOK_EMU_DISK_H
