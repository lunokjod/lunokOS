// **Guardar el que hi ha en un canvas, fet pel sistema** (0.192.51, l'amo: *«Si exportar la imatge a png es massa feixuc per l'app lua,
// que ho faci el sistema (li passes el canvas i ell guarda)»*).
//
// L'app dona la ranura del canvas i el camí, i no dona ni un píxel: els que hi ha al canvas ja són al nucli, i passar-los a Lua per
// tornar-los a baixar seria mig megabyte de viatge per a res (docs/30, docs/75). El format és un **PNG de vuit bits de gris** —el que
// qualsevol programa obre i el que aquesta casa ja sap llegir (`lib/img`)—, i l'escriptor és de la llibreria d'imatges: aquí només hi
// ha el camí i el fitxer (docs/30, docs/50).
#include "kernel/canvas_export.h"

#include "img/img.h"
#include "kernel/ui.h"
#include "kernel/vfs.h"
#include "hal/log.h"
#include "pal/pal.h"

namespace kernel {
namespace {

// **El destí de l'escriptor**: escriu on el VFS ha dit que és el camí, i una escriptura que no hi cap és un fitxer que es queda a mig
// fer i que el qui l'ha demanat ha de saber (docs/70).
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

}  // namespace

bool export_a_png(int16_t slot, const char* path, const char*& code, const char*& reason) {
  code = "E_IO";
  reason = "";
  if (path == nullptr || path[0] == '\0') {
    code = "E_INVAL";
    reason = "a file needs a path";
    return false;
  }
  uint16_t width = 0;
  uint16_t height = 0;
  uint32_t stride = 0;
  const uint8_t* pixels = ui::canvas_pixels(slot, width, height, stride);
  if (pixels == nullptr) {
    reason = "this canvas has no pixels of its own";
    return false;
  }
  // **El disc ha de tenir lloc abans d'escriure-hi**: a file that fills the card in the middle leaves the beginning of a picture there
  // and no way to tell it from a whole one, o sigui que el que no hi cap es diu abans i no es toca res (docs/70, docs/75).
  hal::IStorage* storage = nullptr;
  char rel[96];
  if (!vfs::resolve(path, storage, rel, sizeof(rel)) || storage == nullptr) {
    code = "E_INVAL";
    reason = "there is no ground at that path";
    return false;
  }
  const uint32_t bytes = img::png_bytes(width, height);
  if (bytes == 0) {
    code = "E_INVAL";
    reason = "this canvas has no size";
    return false;
  }
  uint64_t total = 0;
  uint64_t left = 0;
  if (storage->space(total, left) && left < bytes) {
    code = "E_NOSPC";
    reason = "there is not enough room on that ground for the picture";
    return false;
  }
  hal::FileSlot file;
  if (!storage->open(rel, hal::OpenMode::kWrite, file)) {
    reason = "the file would not open for writing";
    return false;
  }
  FileSink sink(storage, file);
  const int64_t began = pal::system().millis();
  const bool wrote = img::png_write(width, height, pixels, stride, sink);
  storage->close(file);
  if (!wrote) {
    reason = "the picture did not finish";
    // **Mig fitxer no és cap fitxer**: the row that did not go in leaves a PNG with no end, and a page that opened it would draw half a
    // drawing and believe the other half was paper (docs/46).
    storage->remove(rel);
    return false;
  }
  code = "";
  hal::logf(pal::kLogInfo, "EXPORT", "%s: %ux%u, %u B in %lld ms", path, static_cast<unsigned>(width),
            static_cast<unsigned>(height), static_cast<unsigned>(bytes),
            static_cast<long long>(pal::system().millis() - began));
  return true;
}

}  // namespace kernel
