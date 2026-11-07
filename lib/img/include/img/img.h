// **The pictures a page can carry**, read the way this project reads everything (docs/30): the file is the
// caller's bytes, the rows come out four bits a pixel in the packing the glass takes, and there is no
// allocation of its own —a decoder that needs memory is given a block by the caller and says so when it
// does not fit—.
//
// What is here is what a board can use: a **BMP** of the kinds every paint program writes, a **PNG** with
// its filters and its palette, a **GIF** —the first frame, which is what an e-ink page shows—, a **JPEG** of
// the baseline kind that a camera and a phone write, an **ICO** (which is a BMP or a PNG inside a
// directory), and the **netpbm** family (`PBM`, `PGM`, `PPM`) that is what a script writes without a
// library. The list of formats a page thinks of as pictures is in docs/30; what is **not** read is refused
// with a reason and never guessed.
//
// The output is **one byte per two pixels, the left one in the high nibble**, which is the glass, the
// canvas of an app and the blob of a font's glyphs: a picture drawn on a page is a copy and not a
// conversion (docs/50, docs/75). Colour becomes grey with the luminance of the sRGB primaries and an
// ordered dither, because a photograph in sixteen levels without a dither is a poster and a photograph
// with one is a photograph (docs/30, docs/85).
#ifndef LUNOK_IMG_IMG_H
#define LUNOK_IMG_IMG_H

#include <stdint.h>

namespace img {

// What came out of it, and the reason when nothing did. A plain enum like the HAL's: the answer is asked
// for on nearly every line of a decoder and `Status::kBadData` three hundred times hides the tables it
// sits between.
enum Status : uint8_t {
  kOk = 0,
  kUnknown,      // the first bytes are not a signature this reader knows
  kBadData,      // a number that does not fit, a table that points nowhere
  kUnsupported,  // a picture of this family that this reader does not read yet (an interlaced PNG, a
                 // progressive JPEG, a JPEG in CMYK), said instead of drawn wrong
  kTruncated,    // the file ended in the middle of the picture
  kNoRoom,       // the scratch the caller gave is smaller than this picture needs
};

// What the header says, without decoding a pixel. `format` is a word of four letters and it lives in the
// decoder, not in the caller's memory.
struct Info {
  const char* format = "";
  uint16_t width = 0;
  uint16_t height = 0;
  uint8_t depth = 0;  // bits per pixel of the file, which is not the eight of a grey scale
};

// **A picture comes out a row at a time**, in order and from the top, and the caller decides what to do
// with each one: a page copies it, a test reads it, a thumbnail shrinks it as it goes. The bytes are
// `(width + 1) / 2` and the last nibble of an odd row is zero, so a row is a whole number of bytes and a
// caller may copy it entire (docs/30, docs/50).
typedef void (*RowFn)(void* ctx, uint16_t y, const uint8_t* packed);

struct RowSink {
  RowSink(RowFn row_writer, void* context) : write(row_writer), ctx(context) {}
  RowFn write;
  void* ctx;
};

// **What the decoder needs besides the file**, in bytes: a PNG has to be inflated into something, a JPEG
// keeps a band of samples for each component, and every family builds the row it hands to the sink. **The
// state of a decoder lives in this block and not on the stack** (0.118.0): the table of a GIF is twelve
// kilobytes, the header of a JPEG three and a half, and the task that runs an app has six or seven —the
// board answered a page of pictures with a stack overflow in `loopTask`—. It is asked before the file is
// read, so the caller allocates once and knows the price of the picture it is about to load; a block
// smaller than this number is refused with `kNoRoom` and never written past (docs/75, docs/85).
uint32_t scratch_bytes(const uint8_t* data, uint32_t len);

// Reads the header only: the size, the depth and the family, for an app that wants to lay a picture out
// before decoding it (docs/30).
Status read_info(const uint8_t* data, uint32_t len, Info& out);

// Decodes the whole picture. `scratch` is the block `scratch_bytes` asked for; a decoder that needs more
// than it has says `kNoRoom` and not a wrong picture. The sink is called once per row from zero to
// `height - 1`, and a picture whose rows are all read is `kOk` (docs/30).
Status decode(const uint8_t* data, uint32_t len, const RowSink& sink, uint8_t* scratch, uint32_t cap);

}  // namespace img

#endif  // LUNOK_IMG_IMG_H
