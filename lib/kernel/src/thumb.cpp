#include "kernel/thumb.h"

#include <stdio.h>
#include <string.h>

#include "hal/hal.h"
#include "img/img.h"
#include "kernel/log.h"
#include "kernel/meter.h"
#include "kernel/shell.h"
#include "kernel/vfs.h"
#include "pal/pal.h"

namespace kernel {
namespace thumb {
namespace {

// **El sostre d'una foto és el de la vista** (`ui::kImageFileBytes`): el que una pàgina pot dibuixar és el que aquest mòdul pot
// escalar, i dos números diferents serien dues respostes a la mateixa pregunta (docs/30, docs/75).
const uint32_t kPictureFileBytes = 2u << 20;
// **Per sota de vuit kilobytes el flaix té altres coses a fer** (el mateix que el cau d'un llibre, docs/46 C11): un cau de 256
// per 256 són trenta-dos kilobytes i el d'una foto de seixanta-quatre, uns quatre.
const uint64_t kRoom = 8u * 1024u;

// **El camí del cau, al costat del fitxer** (docs/46, C11): `.<nom del fitxer>.cache.ics` a la carpeta on és. És el nom que el
// cau d'un llibre fa servir des de la 0.146.0 i el que `apps.opener` ja reconeix com a miniatura, o sigui que una sola convenció
// per a tots els cau d'aquest sistema.
bool the_path_beside(const char* path, char* out, size_t cap) {
  if (path == nullptr || out == nullptr || cap == 0) return false;
  const char* slash = strrchr(path, '/');
  if (slash == nullptr) return false;
  const size_t folder = static_cast<size_t>(slash - path) + 1;
  const int written = snprintf(out, cap, "%.*s.%s.cache.ics", static_cast<int>(folder), path, slash + 1);
  return written > 0 && static_cast<size_t>(written) < cap;
}

bool ends_with(const char* text, const char* tail) {
  if (text == nullptr || tail == nullptr) return false;
  const size_t a = strlen(text);
  const size_t b = strlen(tail);
  return b <= a && memcmp(text + a - b, tail, b) == 0;
}

// **Un cau que hi és i és sencer és el que estalvia l'escriptura**: la mida de fila i la del fitxer surten de les dues paraules de
// la mateixa capçalera, o sigui que un fitxer de la mida que diu és un que aquest sistema va acabar d'escriure —i un que es va
// quedar a mitges per una corrent que se'n va anar es torna a escriure— (docs/46, C11).
bool cache_is_whole(hal::IStorage* storage, const char* rel, uint32_t size) {
  if (storage == nullptr) return false;
  hal::FileSlot slot;
  if (!storage->open(rel, hal::OpenMode::kRead, slot)) return false;
  uint8_t head[8];
  const int32_t got = storage->read(slot, head, sizeof(head));
  storage->close(slot);
  if (got != static_cast<int32_t>(sizeof(head)) || memcmp(head, "ICS1", 4) != 0) return false;
  const uint16_t width = static_cast<uint16_t>(head[4] | (head[5] << 8));
  const uint16_t height = static_cast<uint16_t>(head[6] | (head[7] << 8));
  if (width == 0 || height == 0 || (width & 1u) != 0) return false;
  return img::ics_bytes(width, height) == size;
}

// **El cau mentre s'escriu**: the decoder hands the picture a row at a time and this fits it into the cache's size, packing the
// columns the way the ICS wants them and writing each output row as soon as it is whole. **Una foto que s'ha de fer més gran es
// repeteix** (una vintena de píxels escalada a seixanta-quatre): the rows between the one that was just mapped and the one before
// go out with the same bytes, or the picture would have stripes where nothing was written (docs/46, C11, docs/75).
struct Fitted {
  hal::IStorage* storage = nullptr;
  hal::FileSlot slot;
  int32_t width = 0;         // the cache's, which is the picture's box
  int32_t height = 0;
  int32_t source_width = 0;
  int32_t source_height = 0;
  int32_t last_row = -1;
  bool ok = true;
  uint8_t line[kMaxSide / 2 + 1];
  uint8_t row[kMaxSide / 2 + 1];
};

void write_the_row(Fitted& fit) {
  const uint32_t bytes = img::ics_row(static_cast<uint16_t>(fit.width), fit.line, fit.row,
                                      static_cast<uint32_t>(sizeof(fit.row)));
  if (bytes == 0) {
    fit.ok = false;
    return;
  }
  fit.ok = fit.storage->write(fit.slot, fit.row, static_cast<int32_t>(bytes)) == static_cast<int32_t>(bytes);
}

void fit_a_row(void* ctx, uint16_t y, const uint8_t* packed) {
  Fitted* fit = static_cast<Fitted*>(ctx);
  if (fit == nullptr || packed == nullptr || !fit->ok || fit->source_height <= 0) return;
  int32_t row = static_cast<int32_t>(y) * fit->height / fit->source_height;
  if (row < 0) row = 0;
  if (row >= fit->height) row = fit->height - 1;
  if (row <= fit->last_row) return;

  // Les columnes, una a una i amb el veí més proper: `ui.image` no interpola i un cau no ha de fer una cosa que la pàgina no fa.
  memset(fit->line, 0, sizeof(fit->line));
  for (int32_t column = 0; column < fit->width; ++column) {
    const int32_t at = column * fit->source_width / fit->width;
    const uint8_t byte = packed[at / 2];
    const uint8_t level = static_cast<uint8_t>(((at & 1) == 0) ? (byte >> 4) : (byte & 0x0F));
    uint8_t& target = fit->line[column / 2];
    if ((column & 1) == 0) {
      target = static_cast<uint8_t>((target & 0x0F) | (level << 4));
    } else {
      target = static_cast<uint8_t>((target & 0xF0) | level);
    }
  }
  for (int32_t r = fit->last_row + 1; r <= row && fit->ok; ++r) write_the_row(*fit);
  fit->last_row = row;
}

// **L'espai de la foto i el del cau, en un sol lloc**: la foto s'encongeix fins a cabre al quadre de `max` pel costat més llarg i
// mai s'engrandeix més enllà d'ella mateixa (un cau de 64 d'una foto de 20 píxels és la foto de 20 píxels ampliada, que no diu res
// de bo de la foto).
void the_size_of_the_cache(int32_t source_w, int32_t source_h, int32_t max, int32_t& width, int32_t& height) {
  width = source_w;
  height = source_h;
  if (max > 0 && width > max) {
    height = height * max / width;
    width = max;
  }
  if (max > 0 && height > max) {
    width = width * max / height;
    height = max;
  }
  if (width <= 0) width = 1;
  if (height <= 0) height = 1;
}

void fail(const char*& why, const char* text) { why = text; }

}  // namespace

bool the_cache_beside(const char* path, char* out, size_t out_cap) {
  char cache[200];
  if (!the_path_beside(path, cache, sizeof(cache))) return false;
  hal::Stat there;
  if (!vfs::stat(cache, there) || there.type != hal::NodeType::kFile || there.size < 9) return false;
  hal::IStorage* storage = nullptr;
  char rel[128];
  if (!vfs::resolve(cache, storage, rel, sizeof(rel))) return false;
  if (!cache_is_whole(storage, rel, there.size)) return false;
  if (out != nullptr && out_cap > 0) {
    const int written = snprintf(out, out_cap, "%s", cache);
    if (written <= 0 || static_cast<size_t>(written) >= out_cap) return false;
  }
  return true;
}

bool of_the_picture(const char* path, int32_t max, char* out, size_t out_cap, const char*& why) {
  pal::MemoryTag who_asks(pal::kMemoryThumb, shell::app_id());
  why = nullptr;
  if (path == nullptr || path[0] == '\0' || out == nullptr || out_cap == 0) {
    fail(why, "no picture was asked for");
    return false;
  }
  if (max <= 0) max = kSide;
  if (max > kMaxSide) max = kMaxSide;
  // **Un cau no és una foto**: preguntar per la cara d'un `ICS1` seria fer-ne un cau del cau, i la carpeta s'ompliria de fitxers
  // que diuen el mateix (docs/46, C11).
  if (ends_with(path, ".cache.ics")) {
    fail(why, "a cache is already a face and it is not a picture to make one of");
    return false;
  }

  char cache[200];
  if (!the_path_beside(path, cache, sizeof(cache))) {
    fail(why, "the name of that file has no room for a cache beside it");
    return false;
  }

  hal::IStorage* storage = nullptr;
  char rel[128];
  hal::Stat info;
  if (!vfs::stat(path, info) || info.type != hal::NodeType::kFile) {
    fail(why, "there is no such picture");
    return false;
  }
  if (!vfs::resolve(path, storage, rel, sizeof(rel)) || storage == nullptr) {
    fail(why, "that path is not on a volume");
    return false;
  }
  // **El cau que ja hi és es torna a dir i no s'escriu** (docs/46, C11): és un fitxer al flaix i la foto no ha canviat.
  if (the_cache_beside(path, out, out_cap)) return true;

  if (info.size == 0 || info.size > kPictureFileBytes) {
    fail(why, "a picture is up to two megabytes");
    return false;
  }
  if (!vfs::is_writable(path)) {
    fail(why, "this volume cannot carry a cache");
    return false;
  }
  // **L'espai es pregunta abans d'escalar** (docs/46, C11): una foto que no cap al flaix es deixa estar i no es deixa a mitges.
  uint64_t total = 0;
  uint64_t free_bytes = 0;
  if (storage->space(total, free_bytes) && free_bytes < kRoom) {
    fail(why, "no room on this volume for a face of it");
    return false;
  }

  // **El cap del fitxer primer, i el fitxer sencer només si la firma és d'una foto** (0.192.38): every family this system reads
  // starts with its own signature, o sigui que un text, un llibre o una targeta de so es refusen amb mig kilobyte llegit i no amb
  // els seus megabytes a la PSRAM. **El cap no és prou per a la mida** —el lector d'un PNG camina els seus trossos i un cap tallat
  // és `kTruncated` i no `kOk`—, i per això el que es mira aquí és només si la firma és d'algú que aquest sistema llegeix: el que
  // no ho és contesta `kUnknown` i s'acaba aquí (docs/30, docs/46, docs/75).
  char head[512];
  size_t got = 0;
  if (!vfs::read_file(path, head, sizeof(head), got)) {
    fail(why, "the picture could not be read");
    return false;
  }
  img::Info picture;
  if (img::read_info(reinterpret_cast<const uint8_t*>(head), static_cast<uint32_t>(got), picture) == img::kUnknown) {
    fail(why, "the file is not a picture this system reads");
    return false;
  }

  void* file = nullptr;
  {
    meter::Scope scope(meter::kExtraAlloc);
    file = pal::extra_memory().allocate(static_cast<size_t>(info.size) + 1, true);
  }
  if (file == nullptr) {
    fail(why, "there is no memory for the file");
    return false;
  }
  size_t read = 0;
  char* bytes = static_cast<char*>(file);
  if (!vfs::read_file(path, bytes, static_cast<size_t>(info.size), read)) {
    pal::extra_memory().release(file);
    fail(why, "the picture could not be read");
    return false;
  }
  const uint8_t* data = reinterpret_cast<const uint8_t*>(bytes);
  if (img::read_info(data, static_cast<uint32_t>(read), picture) != img::kOk || picture.width == 0 ||
      picture.height == 0) {
    pal::extra_memory().release(file);
    fail(why, "the file is not a picture this system reads");
    return false;
  }

  int32_t width = 0;
  int32_t height = 0;
  the_size_of_the_cache(picture.width, picture.height, max, width, height);
  // **Una foto que ja hi cap és la seva pròpia cara**: un PNG de quaranta-vuit per quaranta-vuit no necessita un cau del mateix
  // dibuix, o sigui que la resposta és el fitxer i no s'escriu res al flaix.
  if (width == picture.width && height == picture.height) {
    pal::extra_memory().release(file);
    const int written = snprintf(out, out_cap, "%s", path);
    if (written <= 0 || static_cast<size_t>(written) >= out_cap) {
      fail(why, "the name of that picture has no room for an answer");
      return false;
    }
    return true;
  }

  const uint32_t scratch_bytes = img::scratch_bytes(data, static_cast<uint32_t>(read));
  void* scratch = nullptr;
  if (scratch_bytes > 0) {
    meter::Scope scope(meter::kExtraAlloc);
    scratch = pal::extra_memory().allocate(scratch_bytes, true);
  }
  if (scratch_bytes > 0 && scratch == nullptr) {
    pal::extra_memory().release(file);
    fail(why, "there is no memory to decode the picture");
    return false;
  }

  char cache_rel[160];
  hal::IStorage* cache_storage = nullptr;
  if (!vfs::resolve(cache, cache_storage, cache_rel, sizeof(cache_rel))) {
    if (scratch != nullptr) pal::extra_memory().release(scratch);
    pal::extra_memory().release(file);
    fail(why, "the cache of that picture is not on a volume");
    return false;
  }

  Fitted fit;
  fit.storage = cache_storage;
  fit.width = width;
  fit.height = height;
  fit.source_width = picture.width;
  fit.source_height = picture.height;
  fit.last_row = -1;

  bool written_ok = cache_storage->open(cache_rel, hal::OpenMode::kWrite, fit.slot);
  const bool opened = written_ok;
  if (written_ok) {
    uint8_t header[8];
    img::ics_header(static_cast<uint16_t>(width), static_cast<uint16_t>(height), header);
    written_ok = cache_storage->write(fit.slot, header, static_cast<int32_t>(sizeof(header))) ==
                 static_cast<int32_t>(sizeof(header));
  }
  const int64_t began = pal::system().millis();
  if (written_ok) {
    written_ok = img::decode(data, static_cast<uint32_t>(read), img::RowSink(fit_a_row, &fit),
                             static_cast<uint8_t*>(scratch), scratch_bytes) == img::kOk &&
                 fit.ok;
  }
  if (opened) cache_storage->close(fit.slot);
  if (scratch != nullptr) pal::extra_memory().release(scratch);
  pal::extra_memory().release(file);

  if (!written_ok) {
    // **Mig cau no és un cau**: a flash that filled up in the middle of the write leaves the beginning of a picture there, and it
    // would be read as an `ICS1` that is cut. It goes, and the next ask tries again (docs/46, C11).
    cache_storage->remove(cache_rel);
    log_printf(pal::kLogWarn, "THUMB", "the face of %s cannot be written at %s", path, cache);
    fail(why, "the picture could not be scaled and written");
    return false;
  }

  log_printf(pal::kLogInfo, "THUMB", "the face of %s is written at %s, %ldx%ld, %lld ms", path, cache,
             static_cast<long>(width), static_cast<long>(height),
             static_cast<long long>(pal::system().millis() - began));
  const int written = snprintf(out, out_cap, "%s", cache);
  if (written <= 0 || static_cast<size_t>(written) >= out_cap) {
    fail(why, "the name of that cache has no room for an answer");
    return false;
  }
  return true;
}

}  // namespace thumb
}  // namespace kernel
