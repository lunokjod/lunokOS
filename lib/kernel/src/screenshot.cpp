// **La captura de pantalla, escrita mentre es llegeix** (0.192.59, l'amo: *«Una comanda per fer captura de la pantalla i escollir format
// de sortida (per defecte ics) "screenshoot /sd/capture.ics"»*).
//
// **El vidre té els píxels i aquesta porta no en fa cap còpia**: `hal::ISurface::read_row` dona una fila al marc de la pàgina —dos
// píxels un byte, el de l'esquerra al nibble de dalt— i el fitxer s'escriu a mesura que les files arriben, o sigui que una captura de
// 540 × 960 no demana ni un bloc de dos-cents seixanta kilobytes (docs/50, docs/75).
//
// **Els dos formats són els dos que aquesta casa sap escriure**: `ICS1` (les tres crides de `lib/img`, que és el format d'un cau i el
// que `img::decode` llegeix) i un `PNG` de vuit bits de gris (`img::png_write_rows`). El format el diu l'extensió del camí, i el que
// no és cap dels dos es diu amb una frase i no s'escriu: un fitxer amb l'extensió d'un format i els bytes d'un altre és pitjor que cap
// fitxer (docs/30, docs/46, docs/70).
#include "kernel/screenshot.h"

#include <stdio.h>
#include <string.h>

#include "hal/hal.h"
#include "hal/log.h"
#include "img/img.h"
#include "kernel/display.h"
#include "kernel/vfs.h"
#include "pal/pal.h"

namespace kernel {
namespace screenshot {
namespace {

// **Mitja pàgina de la placa més ampla que hi ha** (1 024 píxels): the row of a screenshot is read into this and no block of the
// heap is asked for it. A glass wider than this says so instead of writing half a picture (docs/75).
const uint32_t kRowBytes = 512;

// **El destí**: el fitxer que el VFS ha resolt, i una escriptura que no hi cap és una captura que es queda a mitges i que el qui
// l'ha demanada ha de saber (docs/70).
class FileSink : public img::PngSink {
 public:
  FileSink(hal::IStorage* storage, hal::FileSlot& slot) : storage_(storage), slot_(slot) {}
  bool write(const uint8_t* data, uint32_t len) override {
    if (storage_ == nullptr) return false;
    return storage_->write(slot_, data, static_cast<int32_t>(len)) == static_cast<int32_t>(len);
  }

 private:
  hal::IStorage* storage_;
  hal::FileSlot& slot_;
};

// **Les files del vidre com una font per a l'escriptor del PNG**: una fila a la vegada, sense cap bloc de la pàgina.
class GlassSource : public img::RowSource {
 public:
  explicit GlassSource(hal::ISurface* surface) : surface_(surface) {}
  bool row(uint16_t y, const uint8_t*& packed, uint32_t& stride) override {
    if (surface_ == nullptr || !surface_->read_row(y, row_, sizeof(row_))) return false;
    packed = row_;
    stride = sizeof(row_);
    return true;
  }

 private:
  hal::ISurface* surface_;
  uint8_t row_[kRowBytes];
};

// **El format que diu el camí**: the last dot of the last name, and nothing when the name has none.
bool format_of(const char* path, char* out, size_t cap) {
  out[0] = '\0';
  if (path == nullptr) return false;
  const char* slash = strrchr(path, '/');
  const char* name = (slash != nullptr) ? slash + 1 : path;
  const char* dot = strrchr(name, '.');
  if (dot == nullptr || dot[1] == '\0') return false;
  snprintf(out, cap, "%s", dot + 1);
  for (char* at = out; *at != '\0'; ++at) {
    if (*at >= 'A' && *at <= 'Z') *at = static_cast<char>(*at - 'A' + 'a');
  }
  return true;
}

// **El fitxer amb l'extensió posada**, que és el nom que la resposta diu.
bool with_the_extension(const char* path, const char* extension, char* out, size_t cap) {
  const int wrote = snprintf(out, cap, "%s.%s", path, extension);
  return wrote > 0 && static_cast<size_t>(wrote) < cap;
}

}  // namespace

bool capture(const char* path, char* written, size_t cap, const char*& code, const char*& reason) {
  code = "E_IO";
  reason = "";
  if (written != nullptr && cap > 0) written[0] = '\0';
  if (path == nullptr || path[0] == '\0') {
    code = "E_INVAL";
    reason = "a picture needs a path";
    return false;
  }
  hal::ISurface* surface = display::surface();
  if (surface == nullptr) {
    code = "E_UNSUPPORTED";
    reason = "this board has no glass to photograph";
    return false;
  }
  const uint16_t width = surface->width();
  const uint16_t height = surface->height();
  if (width == 0 || height == 0) {
    code = "E_UNAVAIL";
    reason = "the glass has no size yet";
    return false;
  }
  const uint32_t line = (static_cast<uint32_t>(width) + 1) / 2;
  if (line > kRowBytes) {
    code = "E_UNSUPPORTED";
    reason = "this glass is wider than a row of this command";
    return false;
  }

  // **El format, i la foto que en surt**: `ics` quan el camí no en diu cap —el d'aquest sistema i el més barat—, `png` quan ho diu,
  // i res més. Un camí que ja porta l'extensió no se li posa una segona vegada.
  char format[8] = {0};
  char target[128];
  const bool named = format_of(path, format, sizeof(format));
  if (!named) {
    if (!with_the_extension(path, "ics", target, sizeof(target))) {
      code = "E_INVAL";
      reason = "that path is too long";
      return false;
    }
  } else if (strcmp(format, "ics") == 0 || strcmp(format, "png") == 0) {
    snprintf(target, sizeof(target), "%s", path);
  } else {
    code = "E_INVAL";
    reason = "a picture goes out as .ics or as .png";
    return false;
  }
  const bool as_png = named && strcmp(format, "png") == 0;

  // **El disc ha de tenir lloc abans d'escriure-hi**: a capture that fills the card in the middle leaves half a picture there and no way
  // to tell it from a whole one, o sigui que el que no hi cap es diu abans i no es toca res (docs/70, docs/75).
  hal::IStorage* storage = nullptr;
  char rel[96];
  if (!vfs::resolve(target, storage, rel, sizeof(rel)) || storage == nullptr) {
    code = "E_INVAL";
    reason = "there is no ground at that path";
    return false;
  }
  const uint32_t bytes = as_png ? img::png_bytes(width, height) : img::ics_bytes(width, height);
  if (bytes == 0) {
    code = "E_INVAL";
    reason = "the glass has no picture";
    return false;
  }
  uint64_t total = 0;
  uint64_t left = 0;
  if (storage->space(total, left) && left < bytes) {
    code = "E_NOSPC";
    reason = "there is not enough room on that ground for the picture";
    return false;
  }

  hal::FileSlot file = {};
  if (!storage->open(rel, hal::OpenMode::kWrite, file)) {
    reason = "the file would not open for writing";
    return false;
  }
  const int64_t began = pal::system().millis();
  bool wrote = true;

  if (as_png) {
    FileSink sink(storage, file);
    GlassSource source(surface);
    wrote = img::png_write_rows(width, height, source, sink);
  } else {
    // **El capçal i una fila darrere l'altra**: the header of the `ICS1` and then the rows, with the odd picture's last nibble put to
    // paper —the same three calls a cache of a book is written with, so the file this writes is one `img::decode` reads back (docs/46).
    uint8_t head[8];
    img::ics_header(width, height, head);
    wrote = storage->write(file, head, sizeof(head)) == static_cast<int32_t>(sizeof(head));
    uint8_t row[kRowBytes];
    uint8_t packed[kRowBytes];
    for (uint16_t y = 0; wrote && y < height; ++y) {
      if (!surface->read_row(y, row, sizeof(row))) {
        wrote = false;
        break;
      }
      const uint32_t put = img::ics_row(width, row, packed, sizeof(packed));
      if (put == 0) {
        wrote = false;
        break;
      }
      if (storage->write(file, packed, static_cast<int32_t>(put)) != static_cast<int32_t>(put)) wrote = false;
    }
  }

  storage->close(file);
  if (!wrote) {
    reason = "the picture did not finish";
    // **Mig fitxer no és cap fitxer**: the row that did not go in leaves a picture that no reader opens, i el que s'ha escrit fins aquí
    // s'esborra perquè no quedi per a ningú (docs/46, docs/70).
    storage->remove(rel);
    return false;
  }
  if (written != nullptr && cap > 0) snprintf(written, cap, "%s", target);
  code = "";
  const int64_t spent = pal::system().millis() - began;
  hal::logf(pal::kLogInfo, "SHOT", "%s: %ux%u, %u B in %lld ms", target, static_cast<unsigned>(width),
            static_cast<unsigned>(height), static_cast<unsigned>(bytes), static_cast<long long>(spent));
  return true;
}

}  // namespace screenshot
}  // namespace kernel
