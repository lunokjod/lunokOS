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

// **`always_inline` i no `inline`** (0.189.73): this project is built with `-Os`, which is the Arduino default, and there an
// `inline` function of this size is **a call** —the board measured eighty-six cycles for every pixel of a page, and a call
// with its prologue, its comparisons and its return is most of them—. It is the hot loop of the whole panel path and it is
// asked to be a few instructions and nothing else (docs/50, docs/95).
#if defined(__GNUC__)
#define LUNOK_GATHER_INLINE inline __attribute__((always_inline))
#else
#define LUNOK_GATHER_INLINE inline
#endif

// **The four bits of a pixel of the image**, or the white of the panel when the pixel falls outside it —a rectangle that
// runs past the edge of the page is paper and not a read of nobody's memory (docs/50).
LUNOK_GATHER_INLINE uint8_t image_level(const uint8_t* image, uint16_t width, uint16_t height, int32_t x, int32_t y,
                           uint8_t white) {
  if (x < 0 || y < 0 || x >= static_cast<int32_t>(width) || y >= static_cast<int32_t>(height)) return white;
  const uint32_t stride = static_cast<uint32_t>(width) / 2u;
  const uint8_t byte = image[static_cast<uint32_t>(y) * stride + static_cast<uint32_t>(x) / 2u];
  return ((x & 1) == 0) ? static_cast<uint8_t>(byte >> 4) : static_cast<uint8_t>(byte & 0x0Fu);
}

// **El camí bo del gir: tot a locals i els dos casos desenrotllats** (0.189.75). `objdump` del firmware deia el que costava
// de debò una pàgina: **trenta-cinc instruccions i tretze càrregues per píxel**, vuit de les quals eren `l32i a2, N` —les
// variables capturades per una lambda, que a cada píxel tornen a passar per un punter—. Aquí no hi ha lambda ni paràmetre de
// compte: `PerByte` i `Bits` són constants de compilació, la imatge es camina amb punters que s'avancen i el byte de cada fila
// s'escriu una vegada. **Només val quan tota la banda cau dins la imatge**, que és el cas de cada pintada normal; si no, hi
// ha el camí general, que comprova els límits píxel a píxel (docs/50, docs/95).
template <uint8_t PerByte, uint8_t Bits>
inline void column_walk_inside(uint8_t* out, const uint8_t* image, uint32_t stride, uint16_t w, uint16_t h,
                               uint16_t row_bytes, int32_t x_first, int32_t dx, int32_t y_first, int32_t dy) {
  const uint16_t whole = static_cast<uint16_t>(w / PerByte);
  const int32_t step = dy * static_cast<int32_t>(stride);
  // **Els quatre nivells de dos bits** surten de tallar el nibble, i el tall és una constant del format (0.189.75).
  const uint8_t cut = (Bits == 2) ? 2 : 0;
  const uint8_t* from[kGatherBand];
  for (uint16_t r = 0; r < h; ++r) {
    const int32_t x = x_first + dx * static_cast<int32_t>(r);
    from[r] = image + static_cast<uint32_t>(y_first) * stride + static_cast<uint32_t>(x) / 2u;
  }
  const uint16_t rest = static_cast<uint16_t>(w - whole * PerByte);
  for (uint16_t b = 0; b < whole; ++b) {
    for (uint16_t r = 0; r < h; ++r) {
      const uint8_t* q = from[r];
      const int32_t x = x_first + dx * static_cast<int32_t>(r);
      uint8_t byte = 0;
      if ((x & 1) == 0) {
        for (uint8_t k = 0; k < PerByte; ++k) {
          byte = static_cast<uint8_t>((byte << Bits) | static_cast<uint8_t>((*q >> 4) >> cut));
          q += step;
        }
      } else {
        for (uint8_t k = 0; k < PerByte; ++k) {
          byte = static_cast<uint8_t>((byte << Bits) | static_cast<uint8_t>((*q & 0x0Fu) >> cut));
          q += step;
        }
      }
      out[static_cast<uint32_t>(r) * row_bytes + b] = byte;
      from[r] = q;
    }
  }
  if (rest != 0) {
    for (uint16_t r = 0; r < h; ++r) {
      const uint8_t* q = from[r];
      const int32_t x = x_first + dx * static_cast<int32_t>(r);
      uint8_t byte = 0;
      uint8_t k = 0;
      for (; k < rest; ++k) {
        const uint8_t level =
            ((x & 1) == 0) ? static_cast<uint8_t>((*q >> 4) >> cut) : static_cast<uint8_t>((*q & 0x0Fu) >> cut);
        byte = static_cast<uint8_t>((byte << Bits) | level);
        q += step;
      }
      out[static_cast<uint32_t>(r) * row_bytes + whole] = static_cast<uint8_t>(byte << (Bits * (PerByte - k)));
    }
  }
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

  // **Un byte de la fila del vidre són `per_byte` píxels seguits**, and the byte is built in a register and written once. The
  // two formats are two pieces of code and not one with a count in a variable, and when the whole band falls inside the image
  // —which is every normal paint— the walk is the one with pointers and no checks (docs/50, docs/95).
  const int32_t x_last = x_first + dx * static_cast<int32_t>(h - 1);
  const int32_t y_last = y_first + dy * static_cast<int32_t>(w - 1);
  const int32_t lo = (dx > 0) ? x_first : x_last;
  const int32_t hi_x = (dx > 0) ? x_last : x_first;
  const int32_t lo_y = (dy > 0) ? y_first : y_last;
  const int32_t hi_y = (dy > 0) ? y_last : y_first;
  if (lo >= 0 && lo_y >= 0 && hi_x < static_cast<int32_t>(width) && hi_y < static_cast<int32_t>(height)) {
    const uint32_t stride = static_cast<uint32_t>(width) / 2u;
    if (two_bit) {
      column_walk_inside<4, 2>(out, image, stride, w, h, row_bytes, x_first, dx, y_first, dy);
    } else {
      column_walk_inside<2, 4>(out, image, stride, w, h, row_bytes, x_first, dx, y_first, dy);
    }
    return;
  }

  // **I el camí de sempre**, for a rectangle that runs past the edge of the page: every pixel asks where it is.
  auto fill = [&](uint8_t per_byte, uint8_t bits) {
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
  };
  if (two_bit) {
    fill(4, 2);
  } else {
    fill(2, 4);
  }
}

}  // namespace hal

#endif  // LUNOK_HAL_GATHER_H
