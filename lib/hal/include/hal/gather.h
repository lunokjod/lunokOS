// **Del full que l'app ha dibuixat a les files que el vidre demana** (0.189.70): the image of a paint lives in the frame
// the app drew in —five hundred and forty wide by nine hundred and sixty tall on the Paper— and the panel wants rows of its
// own frame, which is the same page turned a quarter. Doing that turn is what a paint spends its time on: the board
// measured four hundred and thirty milliseconds for a page, and the arithmetic says why —at ninety degrees a row of the
// glass is a **column** of the image, so the loop walked it with a jump of two hundred and seventy bytes, and **one cache
// line of the PSRAM per pixel** is a hundred cycles a pixel (docs/50, docs/95, E-11).
//
// **The turn is done in bands**: the rows of a band are neighbours in x, so one read of the image feeds all of them and the
// thirty-two bytes of a cache line carry thirty-two pixels instead of one. The result is the same bytes as gathering the
// rows one by one —the test of this says so, comparing both— and it is the only thing that changes.
//
// No bus, no panel, no PSRAM: a pure function, so the emulator and the tests can check it against the slow path.
#ifndef LUNOK_HAL_GATHER_H
#define LUNOK_HAL_GATHER_H

#include <stddef.h>
#include <stdint.h>

namespace hal {

// **Files del vidre en un sol pas** (0.189.70), which is the most rows a band may carry: the driver gathers into a stretch
// of the bus, and sixteen is what fits there with the narrowest format. The function takes any `h` up to this.
const uint16_t kGatherBand = 16;

// The bytes of one row of the glass, whose pixels are four bits each, or two when the format is the narrow one.
inline uint16_t band_row_bytes(uint16_t w, bool two_bit) {
  return two_bit ? static_cast<uint16_t>((w + 3u) / 4u) : static_cast<uint16_t>(w / 2u);
}

// **The four bits of a pixel of the image**, or the white of the panel when the pixel falls outside it —a rectangle that
// runs past the edge of the page is paper and not a read of nobody's memory (docs/50).
inline uint8_t image_level(const uint8_t* image, uint16_t width, uint16_t height, int32_t x, int32_t y,
                           uint8_t white) {
  if (x < 0 || y < 0 || x >= static_cast<int32_t>(width) || y >= static_cast<int32_t>(height)) return white;
  const uint32_t stride = static_cast<uint32_t>(width) / 2u;
  const uint8_t byte = image[static_cast<uint32_t>(y) * stride + static_cast<uint32_t>(x) / 2u];
  return ((x & 1) == 0) ? static_cast<uint8_t>(byte >> 4) : static_cast<uint8_t>(byte & 0x0Fu);
}

// **Un tros del vidre, girat i empaquetat.** `out` carries `h` rows of `band_row_bytes(w, two_bit)` one after another, the
// left pixel of a byte in its high bits, and the last byte of a row padded with zeros in the low ones. The pixel of the
// glass `(nx0 + i, ny0 + r)` is read from the image according to `rotation`, which is the quarter turn the page has with
// the glass (0, 90, 180 or 270), exactly as this project's driver has always done it.
inline void gather_band(uint8_t* out, const uint8_t* image, uint16_t width, uint16_t height, int rotation,
                        uint16_t nx0, uint16_t ny0, uint16_t w, uint16_t h, bool two_bit, uint8_t white) {
  if (out == nullptr || image == nullptr || w == 0 || h == 0 || h > kGatherBand) return;
  const uint16_t row_bytes = band_row_bytes(w, two_bit);
  const uint8_t bits = two_bit ? 2 : 4;
  const uint8_t per_byte = two_bit ? 4 : 2;

  // **A row of the glass that is a line of the image**, which is the page that is not turned: the reads are already in
  // order and a band is only a loop over rows.
  if (rotation != 90 && rotation != 270) {
    for (uint16_t r = 0; r < h; ++r) {
      uint8_t acc = 0;
      uint8_t have = 0;
      uint16_t at = 0;
      for (uint16_t i = 0; i < w; ++i) {
        const int32_t nx = static_cast<int32_t>(nx0) + i;
        const int32_t ny = static_cast<int32_t>(ny0) + r;
        const int32_t x = (rotation == 180) ? static_cast<int32_t>(width) - 1 - nx : nx;
        const int32_t y = (rotation == 180) ? static_cast<int32_t>(height) - 1 - ny : ny;
        const uint8_t level = image_level(image, width, height, x, y, white);
        acc = static_cast<uint8_t>((acc << bits) | (two_bit ? (level >> 2) : level));
        if (++have == per_byte) {
          out[static_cast<uint32_t>(r) * row_bytes + at] = acc;
          ++at;
          acc = 0;
          have = 0;
        }
      }
      if (have != 0) out[static_cast<uint32_t>(r) * row_bytes + at] = static_cast<uint8_t>(acc << (bits * (per_byte - have)));
    }
    return;
  }

  // **I el cas que costa: la fila del vidre és una columna de la imatge.** The `h` rows of the band are `h` neighbours in
  // x, so the bytes of one read of the image are shared by all of them — one cache line for eight rows with the wide
  // format and sixteen with the narrow one, which is the whole of the win (docs/50).
  const int32_t x_first =
      (rotation == 90) ? static_cast<int32_t>(ny0) : static_cast<int32_t>(width) - 1 - static_cast<int32_t>(ny0);
  const int32_t dx = (rotation == 90) ? 1 : -1;
  const int32_t y_first = (rotation == 90) ? static_cast<int32_t>(height) - 1 - static_cast<int32_t>(nx0)
                                           : static_cast<int32_t>(nx0);
  const int32_t dy = (rotation == 90) ? -1 : 1;

  // **Un byte de la fila del vidre són `per_byte` píxels seguits**, so the byte is built in a register and written once: the
  // first version of this kept an accumulator, a count and a place per row of the band, and those three arrays indexed by a
  // variable were more memory traffic than the pixels they carried —three hundred and twenty milliseconds of a page, which
  // the board measured before this was written (docs/50, docs/95).
  const uint16_t whole = static_cast<uint16_t>(w / per_byte);
  for (uint16_t b = 0; b < whole; ++b) {
    const uint16_t i = static_cast<uint16_t>(b * per_byte);
    for (uint16_t r = 0; r < h; ++r) {
      const int32_t x = x_first + dx * static_cast<int32_t>(r);
      uint8_t byte = 0;
      int32_t y = y_first + dy * static_cast<int32_t>(i);
      for (uint8_t k = 0; k < per_byte; ++k) {
        const uint8_t level = image_level(image, width, height, x, y, white);
        byte = static_cast<uint8_t>((byte << bits) | (two_bit ? (level >> 2) : level));
        y += dy;
      }
      out[static_cast<uint32_t>(r) * row_bytes + b] = byte;
    }
  }
  // **I la cua**, when a row of the glass does not end on a whole byte: the low bits are zeros, as the slow path pads them.
  const uint16_t rest = static_cast<uint16_t>(w - whole * per_byte);
  if (rest != 0) {
    for (uint16_t r = 0; r < h; ++r) {
      const int32_t x = x_first + dx * static_cast<int32_t>(r);
      uint8_t byte = 0;
      int32_t y = y_first + dy * static_cast<int32_t>(whole * per_byte);
      uint8_t k = 0;
      for (; k < rest; ++k) {
        const uint8_t level = image_level(image, width, height, x, y, white);
        byte = static_cast<uint8_t>((byte << bits) | (two_bit ? (level >> 2) : level));
        y += dy;
      }
      out[static_cast<uint32_t>(r) * row_bytes + whole] = static_cast<uint8_t>(byte << (bits * (per_byte - k)));
    }
  }
}

}  // namespace hal

#endif  // LUNOK_HAL_GATHER_H
