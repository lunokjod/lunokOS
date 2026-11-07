// **A GIF** (87a and 89a): the colour table, the LZW stream of the first picture and the transparency of its
// graphic control block. An e-ink page shows a picture and not an animation, so **the first frame is the
// picture** and the rest of the file is walked past, which is also what makes a GIF of a flipbook cost the
// same as a GIF of one page (docs/30).
//
// A frame may sit anywhere in the logical screen and be smaller than it: the block the caller gave is the
// whole screen, white before the first pixel is drawn, and every pixel of the frame lands where the file says.
// An interlaced frame is stored in four passes and is written to that block where it belongs, so the rows that
// come out are in order whatever the file did (docs/30).
#include <new>
#include <string.h>

#include "img_private.h"

namespace img {
namespace detail {
namespace {

// The largest table of codes a GIF can build: twelve bits of code (89a, appendix F).
const uint32_t kMaxCodes = 4096;

struct Header {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t table_at = 0;
  uint32_t table_len = 0;  // entries
};

bool read_header(const uint8_t* data, uint32_t len, Header& out, uint32_t& at) {
  Cursor cursor{data, len, 0};
  if (!cursor.skip(6)) return false;  // "GIF87a" or "GIF89a", which the dispatcher has already seen
  uint16_t width = 0;
  uint16_t height = 0;
  uint8_t packed = 0;
  uint8_t background = 0;
  uint8_t aspect = 0;
  if (!cursor.u16le(width) || !cursor.u16le(height) || !cursor.u8(packed) || !cursor.u8(background) ||
      !cursor.u8(aspect)) {
    return false;
  }
  (void)background;  // the colour of the screen a frame does not cover is the paper here, not the table's
  (void)aspect;
  out.width = width;
  out.height = height;
  if ((packed & 0x80) != 0) {
    out.table_len = 1u << ((packed & 0x07) + 1);
    out.table_at = cursor.at;
    if (!cursor.skip(out.table_len * 3)) return false;
  }
  at = cursor.at;
  return true;
}

// The colour of the table, clamped to what it holds: a picture that names a colour it does not carry gets the
// first one instead of whatever follows the table in the file (docs/30).
void colour_at(const uint8_t* data, const Header& header, uint32_t index, uint8_t& r, uint8_t& g,
               uint8_t& b) {
  if (header.table_len == 0) {
    r = g = b = 255;  // a file with no table at all: the paper, and not a colour read from beyond the header
    return;
  }
  if (index >= header.table_len) index = 0;
  const uint32_t at = header.table_at + index * 3;
  r = data[at];
  g = data[at + 1];
  b = data[at + 2];
}

// Where a row of a stored picture goes, which is the order the file wrote it in: an interlaced frame is four
// passes of eight, eight, four and two rows (89a, appendix E).
uint32_t stored_row(uint32_t index, uint32_t height, bool interlaced) {
  if (!interlaced) return index;
  static const uint32_t kPasses[4][2] = {{0, 8}, {4, 8}, {2, 4}, {1, 2}};
  uint32_t left = index;
  for (uint32_t pass = 0; pass < 4; ++pass) {
    const uint32_t start = kPasses[pass][0];
    const uint32_t step = kPasses[pass][1];
    const uint32_t count = (height > start) ? (height - start + step - 1) / step : 0;
    if (left < count) return start + left * step;
    left -= count;
  }
  return height;  // past the picture: never written
}

// **The screen of the picture**, which is what the caller's block holds: white paper with the frame written
// where the file says, in whichever order the file stored it (docs/30).
struct Screen {
  uint8_t* bits = nullptr;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t left = 0;
  uint32_t top = 0;
  uint32_t frame_width = 0;
  uint32_t frame_height = 0;
  uint32_t row = 0;
  uint32_t x = 0;
  bool interlaced = false;
  int32_t transparent = -1;

  void put(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    if (row >= frame_height || x >= frame_width) return;
    const uint32_t screen_x = left + x;
    const uint32_t screen_y = top + stored_row(row, frame_height, interlaced);
    if (screen_x < width && screen_y < height) {
      // A transparent pixel shows the paper and not the frame before it (docs/30).
      const bool clear = (transparent >= 0 && index == static_cast<uint8_t>(transparent));
      const uint8_t level =
          clear ? 15
                : level_of(grey_of(r, g, b), static_cast<uint16_t>(screen_x), static_cast<uint16_t>(screen_y));
      uint8_t& byte = bits[screen_y * row_bytes(width) + screen_x / 2];
      if ((screen_x & 1) == 0) {
        byte = static_cast<uint8_t>((byte & 0x0F) | (level << 4));
      } else {
        byte = static_cast<uint8_t>((byte & 0xF0) | level);
      }
    }
    if (++x >= frame_width) {
      x = 0;
      ++row;
    }
  }
};

// **The LZW of a GIF**: nine bits of code that grow to twelve, a clear code and an end code, and the one
// trick of the format —a code that names the string being built right now— (89a, appendix F).
struct Lzw {
  uint16_t prefix[kMaxCodes];
  uint8_t suffix[kMaxCodes];
  uint16_t next = 0;  // the first code that is not in the table yet
  uint16_t size = 0;  // how many bits a code takes now
  uint16_t clear = 0;
  uint16_t end = 0;
  uint32_t previous = 0;
  bool started = false;
};

void lzw_reset(Lzw& lzw, uint8_t min_size) {
  lzw.clear = static_cast<uint16_t>(1u << min_size);
  lzw.end = static_cast<uint16_t>(lzw.clear + 1);
  lzw.next = static_cast<uint16_t>(lzw.end + 1);
  lzw.size = static_cast<uint16_t>(min_size + 1);
  lzw.previous = 0;
  lzw.started = false;
}

// The bytes of the sub-blocks, which say when there are none left (a stream that ends without its end code is
// `kTruncated`, and the caller is the one that knows it).
struct SubBlocks {
  SubBlocks(const uint8_t* bytes, uint32_t bytes_len, uint32_t start)
      : data(bytes), len(bytes_len), at(start) {}
  const uint8_t* data;
  uint32_t len;
  uint32_t at;
  uint32_t left = 0;  // bytes left of the sub-block being read

  bool next_byte(uint8_t& value) {
    while (left == 0) {
      if (at >= len) return false;
      left = data[at++];
      if (left == 0) return false;  // the block terminator
    }
    value = data[at++];
    --left;
    return true;
  }
};

}  // namespace

Status gif_info(const uint8_t* data, uint32_t len, Info& out) {
  Header header;
  uint32_t at = 0;
  if (!read_header(data, len, header, at)) return kUnknown;
  if (header.width == 0 || header.height == 0) return kBadData;
  if (header.width > kMaxSide || header.height > kMaxSide) return kUnsupported;
  out.format = "GIF";
  out.width = static_cast<uint16_t>(header.width);
  out.height = static_cast<uint16_t>(header.height);
  out.depth = 8;
  return kOk;
}

// **The table of the LZW and the string it builds are in the block of the caller** (0.118.0): twelve kilobytes of
// table on the stack of the task that runs an app is the overflow the board answered with, and the block is the
// extra memory of the board —the PSRAM of the Paper— where a picture of a megapixel already lives (docs/75). The
// state goes after the screen and starts on a word, which is what the two thousand `uint16_t` of the table need.
uint32_t gif_state_at(uint32_t screen_bytes) { return aligned_at(screen_bytes); }

uint32_t gif_state_bytes() { return static_cast<uint32_t>(sizeof(Lzw) + kMaxCodes); }

uint32_t gif_scratch(const uint8_t* data, uint32_t len) {
  Header header;
  uint32_t at = 0;
  if (!read_header(data, len, header, at)) return 0;
  // The screen of the picture, white before anything is drawn on it, because a frame may be smaller than the
  // screen and what shows around it is paper (docs/30).
  const uint64_t screen = static_cast<uint64_t>(row_bytes(header.width)) * header.height;
  const uint64_t bytes = static_cast<uint64_t>(gif_state_at(static_cast<uint32_t>(screen))) + gif_state_bytes();
  return (bytes > 0xFFFFFFFFu) ? 0 : static_cast<uint32_t>(bytes);
}

Status decode_gif(const uint8_t* data, uint32_t len, const RowSink& sink, uint8_t* scratch, uint32_t cap) {
  Header header;
  uint32_t at = 0;
  if (!read_header(data, len, header, at)) return kUnknown;
  if (header.width == 0 || header.height == 0) return kBadData;
  if (header.width > kMaxSide || header.height > kMaxSide) return kUnsupported;
  const uint32_t screen_bytes = row_bytes(header.width) * header.height;
  const uint32_t state_at = gif_state_at(screen_bytes);
  if (scratch == nullptr || cap < state_at + gif_state_bytes()) return kNoRoom;
  for (uint32_t i = 0; i < screen_bytes; ++i) scratch[i] = 0xFF;  // the paper

  int32_t transparent = -1;
  bool have_frame = false;
  Cursor cursor{data, len, at};
  while (cursor.has(1)) {
    uint8_t block = 0;
    if (!cursor.u8(block)) break;
    if (block == 0x3B) break;  // the trailer: there is no more picture in this file
    if (block == 0x21) {
      uint8_t label = 0;
      if (!cursor.u8(label)) return kTruncated;
      if (label == 0xF9) {
        // The graphic control: a size, the flags and the delay, the index of the transparent colour, and a
        // terminator (89a, 23).
        uint8_t size = 0;
        if (!cursor.u8(size)) return kTruncated;
        if (size < 4) return kBadData;
        uint8_t flags = 0;
        uint8_t delay_lo = 0;
        uint8_t delay_hi = 0;
        uint8_t index = 0;
        if (!cursor.u8(flags) || !cursor.u8(delay_lo) || !cursor.u8(delay_hi) || !cursor.u8(index)) {
          return kTruncated;
        }
        transparent = ((flags & 0x01) != 0) ? static_cast<int32_t>(index) : -1;
        if (!cursor.skip(size - 4u)) return kTruncated;
      }
      // Whatever the extension was, its body is a run of sub-blocks that ends with a zero (89a, 15..26).
      while (cursor.has(1)) {
        uint8_t size = 0;
        if (!cursor.u8(size)) return kTruncated;
        if (size == 0) break;
        if (!cursor.skip(size)) return kTruncated;
      }
      continue;
    }
    if (block != 0x2C) return kBadData;  // not a block this format defines

    // An image descriptor: where the frame sits, how big it is, and whether it carries a table of its own.
    uint16_t left = 0;
    uint16_t top = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t packed = 0;
    if (!cursor.u16le(left) || !cursor.u16le(top) || !cursor.u16le(width) || !cursor.u16le(height) ||
        !cursor.u8(packed)) {
      return kTruncated;
    }
    Header local = header;
    if ((packed & 0x80) != 0) {
      local.table_len = 1u << ((packed & 0x07) + 1);
      local.table_at = cursor.at;
      if (!cursor.skip(local.table_len * 3)) return kTruncated;
    }
    uint8_t min_size = 0;
    if (!cursor.u8(min_size)) return kTruncated;
    if (min_size < 2 || min_size > 8) return kBadData;

    Screen screen;
    screen.bits = scratch;
    screen.width = header.width;
    screen.height = header.height;
    screen.left = left;
    screen.top = top;
    screen.frame_width = width;
    screen.frame_height = height;
    screen.interlaced = (packed & 0x40) != 0;
    screen.transparent = transparent;

    // **The table and the string are the block's and not the stack's** (0.118.0): `gif_scratch` counted them, so a
    // caller that gave the number it was asked for has them, and one that gave less gets `kNoRoom` above (docs/75).
    // **The table is made where it stands**: the block is the caller's and it was counted for the table, and a
    // `memset` of the area is all the construction a table of words needs (docs/75).
    uint8_t* state = scratch + state_at;
    memset(state, 0, sizeof(Lzw));
    Lzw& lzw = *reinterpret_cast<Lzw*>(state);
    lzw_reset(lzw, min_size);
    SubBlocks blocks{data, len, cursor.at};
    uint32_t bits = 0;
    uint32_t bit_count = 0;
    uint8_t* string = scratch + state_at + sizeof(Lzw);
    while (true) {
      while (bit_count < lzw.size) {
        uint8_t byte = 0;
        if (!blocks.next_byte(byte)) return kTruncated;  // the stream ended without its end code
        bits |= static_cast<uint32_t>(byte) << bit_count;
        bit_count += 8;
      }
      const uint16_t code = static_cast<uint16_t>(bits & ((1u << lzw.size) - 1u));
      bits >>= lzw.size;
      bit_count -= lzw.size;
      if (code == lzw.clear) {
        lzw_reset(lzw, min_size);
        continue;
      }
      if (code == lzw.end) break;
      uint32_t length = 0;
      if (code < lzw.next) {
        uint32_t walk = code;
        while (walk >= lzw.clear) {
          if (length >= kMaxCodes) return kBadData;
          string[length++] = lzw.suffix[walk];
          walk = lzw.prefix[walk];
        }
        string[length++] = static_cast<uint8_t>(walk);
      } else if (code == lzw.next && lzw.started) {
        // The code that is being built right now: the string of the code before it and, behind it, its own
        // first byte (89a, appendix F).
        uint32_t walk = lzw.previous;
        while (walk >= lzw.clear) {
          if (length + 1 >= kMaxCodes) return kBadData;
          string[length++] = lzw.suffix[walk];
          walk = lzw.prefix[walk];
        }
        const uint8_t first_byte = static_cast<uint8_t>(walk);
        string[length++] = first_byte;
        string[length++] = first_byte;  // the string is read backwards below, so the last slot is its first
                                        // byte
      } else {
        return kBadData;
      }
      // **The string is filled backwards**: `string[length - 1]` is its first byte, which is what the table
      // needs for the entry it is about to add (89a, appendix F).
      const uint8_t first_byte = string[length - 1];
      // The string comes out of the stack backwards.
      for (uint32_t i = length; i > 0; --i) {
        uint8_t r = 0;
        uint8_t g = 0;
        uint8_t b = 0;
        const uint8_t index = string[i - 1];
        colour_at(data, local, index, r, g, b);
        screen.put(index, r, g, b);
      }
      // The table grows by the string that has just been read —**but not after a clear**, where the first
      // code is a literal and there is no string before it to add to— (89a, appendix F).
      if (lzw.started && lzw.next < kMaxCodes) {
        lzw.prefix[lzw.next] = static_cast<uint16_t>(lzw.previous);
        lzw.suffix[lzw.next] = first_byte;
        ++lzw.next;
        if (lzw.next == (1u << lzw.size) && lzw.size < 12) ++lzw.size;
      }
      lzw.previous = code;
      lzw.started = true;
    }
    // The picture is one frame: the rest of the file is walked past by not being read at all (docs/30).
    have_frame = true;
    break;
  }
  if (!have_frame) return kBadData;

  // The screen is the picture: the rows go out in order, whatever the frame did inside (docs/30). **The screen is
  // already the shape the sink takes** —a row of `(width + 1) / 2` bytes, one after the other— so the row is given
  // where it is and not copied: the copy was two kilobytes of stack for a `memcpy` per row (0.118.0).
  for (uint32_t y = 0; y < header.height; ++y) {
    sink.write(sink.ctx, static_cast<uint16_t>(y), scratch + static_cast<uint32_t>(y) * row_bytes(header.width));
  }
  return kOk;
}

}  // namespace detail
}  // namespace img
