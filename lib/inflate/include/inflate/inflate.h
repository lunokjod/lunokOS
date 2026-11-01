// **Deflate, read the other way** (RFC 1951) and the two checksums a container needs (docs/70).
//
// A zip —and an epub is a zip— keeps its entries compressed with deflate, so reading one is reading this.
// It is written the way this project does these things: **one block of output the caller owns and no
// allocation of its own**, because the window a deflate stream looks back into is the output it has already
// written, and 32 KB of window is 32 KB of the buffer the caller was going to need anyway. A stream whose
// distance reaches further back than what has been written is a broken stream, and it is refused (docs/70).
//
// What is here is only the decompressor: `deflate` itself is not needed on a board that reads.
#ifndef LUNOK_INFLATE_INFLATE_H
#define LUNOK_INFLATE_INFLATE_H

#include <stdint.h>

namespace inflate {

// **What came out of it**, and the reason when nothing did (docs/70). A plain enum and not an `enum class`
// because the code that reads a stream answers with one of these on nearly every line, and `Status::kBadData`
// three hundred times hides the tables it sits between (the rest of the HAL's enums are plain too).
enum Status : uint8_t {
  kOk = 0,
  kTruncated,  // the stream ended in the middle of a block or of a code
  kBadData,    // a code that the tables of the stream do not contain, or a distance past the start
  kNoRoom,     // the output does not fit in what the caller gave
  kAdler,      // the zlib wrapper's checksum does not match
};

// **Raw deflate** (what a zip entry holds): `in_len` bytes into `out`, at most `cap`, and `written` says how
// many there are. `in` may be the same block as `out`? No — they are two buffers, and the caller is the one
// who knows how big the second has to be (an entry of a zip says it in its header) (docs/70).
Status raw(const uint8_t* in, uint32_t in_len, uint8_t* out, uint32_t cap, uint32_t& written);

// **The zlib wrapper** (RFC 1950), which is not what a zip holds but is what a `.woff` holds: two header
// bytes, the deflate stream, and an Adler-32 that has to be the one of what came out (docs/70).
Status zlib(const uint8_t* in, uint32_t in_len, uint8_t* out, uint32_t cap, uint32_t& written);

// The checksum of the woff and of the zlib wrapper: `seed` is 1 for a whole block and the value of the last
// call when it is done in pieces.
uint32_t adler32(const uint8_t* data, uint32_t len, uint32_t seed = 1);

// The checksum of a zip entry's name and of its bytes: `seed` is 0 for a whole block.
uint32_t crc32(const uint8_t* data, uint32_t len, uint32_t seed = 0);

}  // namespace inflate

#endif  // LUNOK_INFLATE_INFLATE_H
