// **The run-length of this system** (docs/30, docs/75): a stream of bytes packed as runs, and back.
//
// A picture of this panel is mostly paper: four bits a pixel, one nibble a pixel, and a cell of a face of bits
// is a hundred equal nibbles in a row. That is what a run-length is for, and it is the one compressor that costs
// nothing to write and nothing to trust: **no table, no window, no dictionary**, and unpacking is a copy of runs.
// A deflate would do better on a page of text and would need `lib/inflate` and its window; an icon is not a page of
// text.
//
// **The frame is seven bytes**: `R`, `L`, `E` and the length the stream unpacks to, little endian. The magic is what
// tells a blob of this system from a blob that is not one —an app that reads a cache from a card has to know whether
// what it found is a stream of runs or a file somebody else wrote— and the length is what lets the caller ask for the
// memory **before** unpacking instead of guessing.
//
// **The runs are PackBits**, which is the one everybody knows and the one a byte of control can say on its own:
//   - a control byte `0..127` is a **literal run** of `control + 1` bytes that follow as they are;
//   - a control byte `128..255` is a **repeat**: the next byte, repeated `257 - control` times (two to a hundred and
//     twenty-nine).
// A run of three or more equal bytes is a repeat and everything else is a literal, which is what keeps the packed
// stream from being bigger than the original by more than a byte every hundred and twenty-eight.
//
// **Nothing here allocates**: the caller hands the input, the output and how much room the output has, and gets the
// number of bytes written or zero. The reason is the same one `lib/zip` gives: this runs under the interpreter of an
// app and under a loop with eight kilobytes of stack, and a library that decides how much memory to take is a library
// that cannot be told no (docs/46, A7).
#ifndef LUNOK_RLE_RLE_H
#define LUNOK_RLE_RLE_H

#include <stdint.h>

namespace rle {

// The seven bytes every packed stream starts with: the magic and the length it unpacks to.
const uint32_t kHeader = 7;

// **The most bytes a stream of `len` can pack to**, which is what the caller has to make room for: a literal run of a
// hundred and twenty-eight bytes carries one control byte, so the ceiling is a byte every hundred and twenty-eight plus
// the header. A packed stream that comes out at this size is a stream the data did not compress at all.
uint32_t packed_capacity(uint32_t len);

// **Packs `len` bytes of `in` into `out`**. Answers the bytes written, header included, or 0 when the output has no
// room for it (`cap` under `packed_capacity`), or when there is nothing to pack.
uint32_t pack(const uint8_t* in, uint32_t len, uint8_t* out, uint32_t cap);

// **What a packed stream unpacks to**, read from its header, or 0 when it is not a stream of runs (a wrong magic, a
// stream shorter than a header, or one that says it unpacks to nothing). This is the call a reader makes to know how
// much room to ask for.
uint32_t unpacked_size(const uint8_t* in, uint32_t len);

// **Unpacks a stream of `len` bytes into `out`**, which has room for `cap`. Answers the bytes written —which is always
// what the header promised— or 0 when the output has no room, the stream is cut short, or a run says more than the
// header did. A stream that unpacks to something other than its header said is not half a picture: it is a stream that
// failed.
uint32_t unpack(const uint8_t* in, uint32_t len, uint8_t* out, uint32_t cap);

}  // namespace rle

#endif  // LUNOK_RLE_RLE_H
