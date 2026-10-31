// A QR code that can be any QR: every version of the standard (1 to 40), every
// error correction level, and the three modes that carry text (numeric,
// alphanumeric and byte). Kanji is the one mode that is not here, because it
// needs a Shift-JIS conversion and this project has no use for it yet; asking for
// it is refused instead of writing a symbol that lies (docs/50).
//
// It is written here and not taken from a library for the same reason the rest of
// this project is: it can be read, and the tables are the ones the standard
// publishes and nothing else.
#ifndef LUNOK_QR_QR_H
#define LUNOK_QR_QR_H

#include <stdint.h>

namespace qr {

// Version 40 is 177 modules a side. A caller says how much room it has and this
// library never allocates.
const uint32_t kMaxSide = 177;
const uint32_t kMaxModules = kMaxSide * kMaxSide;

enum class Level : uint8_t { kL = 0, kM = 1, kQ = 2, kH = 3 };
enum class Mode : uint8_t { kAuto = 0, kNumeric = 1, kAlnum = 2, kByte = 3 };

struct Info {
  uint32_t side = 0;     // modules a side
  uint32_t version = 0;  // 1 to 40
  uint32_t mask = 0;     // 0 to 7, the one the standard chose
  Level level = Level::kM;
  Mode mode = Mode::kByte;  // the one that was really used
};

// The side of the smallest symbol that fits the text, or 0 when it does not fit
// at that level and mode. It is a question without an answer buffer, so a caller
// can decide whether it can afford the room before asking for it.
uint32_t side_for(const char* text, Level level, Mode mode);

// Encodes the text into out, one byte per module (1 dark, 0 light), row by row.
// out must hold side*side bytes; the side is in info. False when the text does
// not fit, when the mode asked for cannot carry it, or when out is too small.
bool encode(const char* text, Level level, Mode mode, uint8_t* out, uint32_t out_cap, Info& info);

// How many modules of the symbol carry data (the rest are the finders, the timing
// lines, the alignment patterns, the format and the version). Exposed because it
// is the number the standard's table of total codewords can be checked against.
uint32_t data_modules(uint32_t version);

}  // namespace qr

#endif  // LUNOK_QR_QR_H
