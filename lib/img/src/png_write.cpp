// **Un PNG de vuit bits de gris, escrit a trossos** (0.192.51, l'amo: *«Si exportar la imatge a png es massa feixuc per l'app lua, que
// ho faci el sistema (li passes el canvas i ell guarda)»*).
//
// **Per què no es comprimeix**: a board has a deflate in `lib/inflate` to *read* a PNG and nothing to write one, and a compressor is
// a whole other thing than a decompressor. The zlib stream of a PNG may be **stored** —blocks of raw bytes with their length—, o sigui
// que el fitxer és més gran i és exactament tan vàlid: qualsevol programa l'obre, i el que costa és el fil del bus i no la CPU
// (docs/30, docs/75).
//
// **El filtre de cada fila és zero**: a filter that predicts with the row above would need the row above, and the picture arrives here
// as a buffer that is already two pixels a byte. Zero is no prediction and the reader rebuilds the row byte for byte (PNG spec §9).
#include "img/img.h"

#include <string.h>

namespace img {
namespace {

// **Els quatre bytes de la signatura** (PNG spec §5.2): els mateixos a tots els PNG del món.
const uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

// **El tros de sortida**: a block of raw rows with its five bytes of header goes out together, o sigui que una foto de mig megabyte són
// quatre-centes escriptures i no mig milió. Mil vint-i-quatre bytes és el que cap en un bufet estàtic sense menjar la RAM d'una pàgina
// (docs/75).
const uint32_t kPayload = 1024;
uint8_t g_block[5 + kPayload];

// **El CRC del PNG, bit a bit** (PNG spec §5.5, el polinomi de sempre): les dades que el demanen són el nom del tros i els seus bytes, i
// el tros gros —el de la imatge— el demana sobre tot el que s'ha escrit, o sigui que es va acumulant mentre s'escriu (docs/75).
uint32_t crc_step(uint32_t crc, const uint8_t* data, uint32_t len) {
  static const uint32_t kPolynomial = 0xEDB88320u;
  for (uint32_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 1u) ? ((crc >> 1) ^ kPolynomial) : (crc >> 1);
    }
  }
  return crc;
}

// **L'Adler-32 del zlib** (RFC 1950 §9): dos comptes módulo 65521, el del byte i el de la suma de bytes.
uint32_t adler_step(uint32_t adler, const uint8_t* data, uint32_t len) {
  const uint32_t kMod = 65521u;
  uint32_t a = adler & 0xFFFFu;
  uint32_t b = (adler >> 16) & 0xFFFFu;
  for (uint32_t i = 0; i < len; ++i) {
    a += data[i];
    if (a >= kMod) a -= kMod;
    b += a;
    if (b >= kMod) b -= kMod;
  }
  return (b << 16) | a;
}

void put_be32(uint8_t* out, uint32_t value) {
  out[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
  out[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
  out[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
  out[3] = static_cast<uint8_t>(value & 0xFF);
}

void put_le16(uint8_t* out, uint16_t value) {
  out[0] = static_cast<uint8_t>(value & 0xFF);
  out[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

// **Un tros sencer**: la llargada, el nom, el que porta i el seu CRC (PNG spec §5.3). El CRC del tros gros no pot passar per aquí perquè
// el que porta no és a la memòria, i per això hi ha la versió que rep el CRC ja fet.
bool write_chunk(PngSink& sink, const char* name, const uint8_t* data, uint32_t len) {
  uint8_t head[8];
  put_be32(head, len);
  memcpy(head + 4, name, 4);
  uint32_t crc = crc_step(0xFFFFFFFFu, head + 4, 4);
  crc = crc_step(crc, data, len);
  crc ^= 0xFFFFFFFFu;
  uint8_t tail[4];
  put_be32(tail, crc);
  return sink.write(head, sizeof(head)) && (len == 0 || sink.write(data, len)) && sink.write(tail, sizeof(tail));
}

}  // namespace

// **Quant fa el fitxer**: la signatura, el capçal, la imatge i el comiat. És el número que una pàgina mira abans d'escriure per saber si
// hi cap al disc (docs/70).
uint32_t png_bytes(uint16_t width, uint16_t height) {
  if (width == 0 || height == 0) return 0;
  const uint32_t raw = static_cast<uint32_t>(height) * (1u + width);
  const uint32_t blocks = (raw + kPayload - 1) / kPayload;
  const uint32_t zlib = 2u + raw + blocks * 5u + 4u;
  return 8u + (12u + 13u) + (12u + zlib) + 12u;
}

namespace {

// **El bloc sencer, donat com una font de files**: it is the `png_write` of always, with the pointer the caller has.
class BlockSource : public RowSource {
 public:
  BlockSource(const uint8_t* packed, uint32_t stride) : packed_(packed), stride_(stride) {}
  bool row(uint16_t y, const uint8_t*& packed, uint32_t& stride) override {
    if (packed_ == nullptr) return false;
    packed = packed_ + static_cast<uint32_t>(y) * stride_;
    stride = stride_;
    return true;
  }

 private:
  const uint8_t* packed_;
  uint32_t stride_;
};

}  // namespace

bool png_write(uint16_t width, uint16_t height, const uint8_t* packed, uint32_t stride, PngSink& sink) {
  if (width == 0 || height == 0 || packed == nullptr) return false;
  if (stride < static_cast<uint32_t>((width + 1) / 2)) return false;
  BlockSource source(packed, stride);
  return png_write_rows(width, height, source, sink);
}

bool png_write_rows(uint16_t width, uint16_t height, RowSource& source, PngSink& sink) {
  if (width == 0 || height == 0) return false;
  const uint32_t line = static_cast<uint32_t>((width + 1) / 2);
  if (!sink.write(kSignature, sizeof(kSignature))) return false;

  // **El capçal de la imatge** (PNG spec §11.2.2): vuit bits, gris, sense entrelaçament.
  uint8_t ihdr[13];
  put_be32(ihdr, width);
  put_be32(ihdr + 4, height);
  ihdr[8] = 8;   // bits per sample
  ihdr[9] = 0;   // colour type 0: grey
  ihdr[10] = 0;  // compression
  ihdr[11] = 0;  // filter method
  ihdr[12] = 0;  // no interlace
  if (!write_chunk(sink, "IHDR", ihdr, sizeof(ihdr))) return false;

  // **El tros de la imatge**: la seva llargada no es coneix fins al final —el que porta és el que s'està escrivint—, o sigui que el
  // que es fa és comptar-lo i escriure'l al cap: el tros gros s'escriu a trossos i el seu CRC s'acumula per aquí (docs/75).
  const uint32_t raw_total = static_cast<uint32_t>(height) * (1u + width);
  uint32_t adler = 1u;
  uint32_t crc = crc_step(0xFFFFFFFFu, reinterpret_cast<const uint8_t*>("IDAT"), 4);
  uint32_t written = 0;
  const uint32_t zlib_len = 2u + raw_total + ((raw_total + kPayload - 1) / kPayload) * 5u + 4u;

  // El cap del tros gros: llargada i nom, i l'inici del zlib stream.
  {
    uint8_t head[8];
    put_be32(head, zlib_len);
    memcpy(head + 4, "IDAT", 4);
    if (!sink.write(head, sizeof(head))) return false;
    // **El nom del tros ja és dins del CRC** (PNG spec §5.3): it went in when `crc` was started, o sigui que aquí no s'hi torna —un
    // "IDAT" comptat dues vegades és un fitxer que cap lector obre, i el que el lector diu és `bad header checksum` (vist amb Pillow).
    const uint8_t zhead[2] = {0x78, 0x01};  // deflate, finestra de 32K, sense diccionari: (0x7801 % 31) == 0
    if (!sink.write(zhead, sizeof(zhead))) return false;
    crc = crc_step(crc, zhead, sizeof(zhead));
  }

  // **Les files, de dalt a baix**: el filtre de cada una és zero i darrere seu van els grisos de la fila, el nivell del vidre vegades
  // disset, que és el que un ull llegeix del panell (docs/30).
  uint32_t at = 0;  // quantes dades crues s'han escrit
  for (uint16_t y = 0; y < height; ++y) {
    const uint8_t* packed = nullptr;
    uint32_t stride = 0;
    if (!source.row(y, packed, stride) || packed == nullptr || stride < line) return false;
    // Una fila es parteix entre dos blocs si cal, i el que es fa és omplir el bloc amb les dades crues i buidar-lo quan és ple o quan
    // s'ha acabat la imatge.
    uint32_t column = 0;
    while (column <= width) {
      const uint32_t room = kPayload - at;
      const uint32_t left = (1u + width) - column;
      const uint32_t take = (left < room) ? left : room;
      for (uint32_t i = 0; i < take; ++i) {
        uint8_t value = 0;
        if (column + i == 0) {
          value = 0;  // el filtre de la fila
        } else {
          const uint32_t x = column + i - 1;
          const uint8_t byte = packed[x / 2];
          const uint8_t level = static_cast<uint8_t>(((x & 1u) == 0) ? (byte >> 4) : (byte & 0x0F));
          value = static_cast<uint8_t>(level * 17);
        }
        g_block[5 + at + i] = value;
      }
      adler = adler_step(adler, g_block + 5 + at, take);
      at += take;
      column += take;
      if (at == kPayload) {
        const bool last = (written + at) == raw_total;
        g_block[0] = static_cast<uint8_t>(last ? 0x01 : 0x00);  // BFINAL, BTYPE 00: stored
        put_le16(g_block + 1, static_cast<uint16_t>(at));
        put_le16(g_block + 3, static_cast<uint16_t>(~at & 0xFFFF));
        if (!sink.write(g_block, 5 + at)) return false;
        crc = crc_step(crc, g_block, 5 + at);
        written += at;
        at = 0;
      }
    }
  }
  // El que queda del darrer bloc, que és el darrer de debò.
  if (at > 0) {
    g_block[0] = 0x01;
    put_le16(g_block + 1, static_cast<uint16_t>(at));
    put_le16(g_block + 3, static_cast<uint16_t>(~at & 0xFFFF));
    if (!sink.write(g_block, 5 + at)) return false;
    crc = crc_step(crc, g_block, 5 + at);
    written += at;
  }

  // **L'Adler del zlib, i el CRC del tros sencer** (RFC 1950, PNG spec §5.3).
  uint8_t tail[8];
  put_be32(tail, adler);
  crc = crc_step(crc, tail, 4);
  crc ^= 0xFFFFFFFFu;
  put_be32(tail + 4, crc);
  if (!sink.write(tail, sizeof(tail))) return false;
  return write_chunk(sink, "IEND", nullptr, 0);
}

}  // namespace img
