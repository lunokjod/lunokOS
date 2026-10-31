// The FM24C02 of the M5Paper, byte by byte over the I2C of the chip (docs/50).
#include "esp32/fm24c02.h"

#include <string.h>

namespace hal {
namespace {

// One transaction of the file. The chip has no page buffer, so a write of several bytes is a write
// of several bytes —the address counts up by itself and wraps at 256— but the bus of the chip copies
// the buffer into its own before it goes out, so it is kept small. 32 bytes is the whole of a small
// app's state and eight transactions for the whole memory (docs/75).
const uint32_t kChunk = 32;

}  // namespace

int32_t Fm24c02::read(FileSlot& slot, void* buf, uint32_t len) {
  Open* open = slot_of(slot);
  if (open == nullptr || buf == nullptr) return -1;
  if (open->pos >= kFramBytes) return 0;  // the end of the memory is the end of the file
  const uint32_t room = kFramBytes - open->pos;
  const uint32_t take = (len < room) ? len : room;

  uint8_t* out = static_cast<uint8_t*>(buf);
  uint32_t done = 0;
  while (done < take) {
    const uint32_t piece = ((take - done) < kChunk) ? (take - done) : kChunk;
    if (!bus_->read(kFramAddress, static_cast<uint8_t>(open->pos + done), out + done, piece)) {
      // What already came back is given; a read that could not happen at all is -1 and not a zero
      // the caller would take for the end of the file (docs/70).
      return (done > 0) ? static_cast<int32_t>(done) : -1;
    }
    done += piece;
  }
  open->pos += done;
  return static_cast<int32_t>(done);
}

int32_t Fm24c02::write(FileSlot& slot, const void* buf, uint32_t len) {
  Open* open = slot_of(slot);
  if (open == nullptr || buf == nullptr) return -1;
  if (open->pos >= kFramBytes) return 0;  // full: the memory does not grow
  const uint32_t room = kFramBytes - open->pos;
  const uint32_t put = (len < room) ? len : room;

  const uint8_t* in = static_cast<const uint8_t*>(buf);
  uint8_t frame[kChunk + 1];  // the word address and then the bytes
  uint32_t done = 0;
  while (done < put) {
    const uint32_t piece = ((put - done) < kChunk) ? (put - done) : kChunk;
    frame[0] = static_cast<uint8_t>(open->pos + done);
    memcpy(frame + 1, in + done, piece);
    if (!bus_->write(kFramAddress, frame, piece + 1)) {
      return (done > 0) ? static_cast<int32_t>(done) : -1;
    }
    done += piece;
  }
  open->pos += done;
  return static_cast<int32_t>(done);
}

IStorage* esp32_fram(II2c* bus) {
  if (bus == nullptr) return nullptr;
  // The chip answers or there is no mount: one byte read at the start of the memory, which is what
  // tells a soldered FM24C02 from an empty footprint. It costs one transaction and it is done once,
  // when the mounts are built at boot (docs/70, docs/85).
  uint8_t probe = 0;
  if (!bus->read(kFramAddress, 0, &probe, 1)) return nullptr;
  static Fm24c02 file(bus);
  return &file;
}

}  // namespace hal
