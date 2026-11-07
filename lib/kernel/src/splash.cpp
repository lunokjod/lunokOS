// **A picture on the glass at the moments of the system**, painted on the panel and not on a page
// (docs/30, docs/50, docs/92).
//
// The picture is decoded by `lib/img`, which hands the rows over already four bits a pixel, and each row
// is copied into a block of the extra memory the size of what fits the glass. **Nothing is scaled**: an
// image is a picture and a resize of four bits is a blur, so an image bigger than the glass is cut at
// its centre and one that fits is left as it came. The wash is `gc16` over the whole panel and it is the
// same paint as the picture: one flush, and the glass is clean and says what this moment is.
#include "kernel/splash.h"

#include <stdio.h>
#include <string.h>

#include "hal/hal.h"
#include "img/img.h"
#include "kernel/display.h"
#include "kernel/log.h"
#include "kernel/vfs.h"
#include "pal/pal.h"

namespace kernel {
namespace splash {
namespace {

// **The words of the moments**, in the order of the enum: a moment is here and nowhere else.
struct Moment {
  Kind kind;
  const char* word;
};

const Moment kMoments[] = {
    {kBoot, "boot"},     {kBootAlt, "boot-alt"}, {kPowerOff, "poweroff"},
    {kReboot, "reboot"}, {kCharging, "charging"}, {kSleep, "sleep"},
};
const uint8_t kMomentCount = static_cast<uint8_t>(sizeof(kMoments) / sizeof(kMoments[0]));

// **Where a moment looks and with which extensions**: the decoder reads more, and a splash is a picture a
// person draws, so the three of a drawing program are the ones it is worth asking the disk for. The order
// is the one that matters when two pictures of the same moment are there: PNG first (a page of the glass
// is a PNG), then BMP (what a script writes) and then JPEG (a photograph) (docs/30, docs/50).
const char* const kFolder = "/res/splash";
const char* const kCommon = "/res/splash";
const char* const kExtensions[] = {"png", "bmp", "jpg"};
const uint8_t kExtensionCount = static_cast<uint8_t>(sizeof(kExtensions) / sizeof(kExtensions[0]));

// **The biggest file this will read**, which is a page of this glass with the air of a photograph: the
// block comes from the extra memory and a file of ten megabytes is a file that eats the pool of every app
// that comes after it (docs/75).
const uint32_t kMostBytes = 2u * 1024u * 1024u;

Kind g_showing = kNone;
const char* g_error = "";

// **One nibble of a packed row**, which is the packing of the glass and of the decoder: one byte per two
// pixels and the left one in the high nibble (docs/50).
uint8_t nibble(const uint8_t* packed, uint32_t at) {
  const uint8_t byte = packed[at / 2];
  return ((at & 1u) == 0u) ? static_cast<uint8_t>(byte >> 4) : static_cast<uint8_t>(byte & 0x0Fu);
}

void put_nibble(uint8_t* packed, uint32_t at, uint8_t level) {
  uint8_t& byte = packed[at / 2];
  if ((at & 1u) == 0u) {
    byte = static_cast<uint8_t>((byte & 0x0Fu) | static_cast<uint8_t>(level << 4));
  } else {
    byte = static_cast<uint8_t>((byte & 0xF0u) | (level & 0x0Fu));
  }
}

// **What the decoder is filling**: the block that goes to the glass and the window of the picture that
// lands on it, which is the middle of the image when it is bigger than the panel.
struct Sink {
  uint8_t* pixels;
  uint16_t stride;
  uint16_t width;
  uint16_t height;
  uint16_t skip_x;
  uint16_t skip_y;
};

void on_row(void* context, uint16_t y, const uint8_t* packed) {
  Sink& sink = *static_cast<Sink*>(context);
  if (y < sink.skip_y || static_cast<uint32_t>(y) - sink.skip_y >= sink.height) return;
  uint8_t* row = sink.pixels + static_cast<uint32_t>(y - sink.skip_y) * sink.stride;
  for (uint16_t x = 0; x < sink.width; ++x) {
    put_nibble(row, x, nibble(packed, static_cast<uint32_t>(sink.skip_x) + x));
  }
}

void release(void* block) {
  if (block != nullptr) pal::extra_memory().release(block);
}

}  // namespace

const char* word(Kind kind) {
  for (uint8_t i = 0; i < kMomentCount; ++i) {
    if (kMoments[i].kind == kind) return kMoments[i].word;
  }
  return "";
}

Kind kind_of(const char* name) {
  if (name == nullptr) return kNone;
  for (uint8_t i = 0; i < kMomentCount; ++i) {
    if (strcmp(kMoments[i].word, name) == 0) return kMoments[i].kind;
  }
  return kNone;
}

uint8_t count() { return kMomentCount; }

Kind at(uint8_t index) { return (index < kMomentCount) ? kMoments[index].kind : kNone; }

bool path_of(Kind kind, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return false;
  out[0] = '\0';
  const char* name = word(kind);
  if (name[0] == '\0') return false;
  hal::Stat info;
  // **The picture of the moment first and the one of every moment after it**: a build that carries one
  // picture is the common one, and a moment with a picture of its own is the exception it allows.
  for (uint8_t i = 0; i < kExtensionCount; ++i) {
    snprintf(out, cap, "%s/%s.%s", kFolder, name, kExtensions[i]);
    if (vfs::stat(out, info) && info.size > 0) return true;
  }
  for (uint8_t i = 0; i < kExtensionCount; ++i) {
    snprintf(out, cap, "%s.%s", kCommon, kExtensions[i]);
    if (vfs::stat(out, info) && info.size > 0) return true;
  }
  out[0] = '\0';
  return false;
}

bool available(Kind kind) {
  char path[96];
  return display::surface() != nullptr && path_of(kind, path, sizeof(path));
}

bool show(Kind kind) {
  g_error = "";
  if (kind == kNone) {
    g_error = "no such moment";
    return false;
  }
  char path[96];
  if (!path_of(kind, path, sizeof(path))) {
    g_error = "no picture for that moment";
    return false;
  }
  if (!show(path)) return false;
  g_showing = kind;
  return true;
}

bool show(const char* path) {
  g_error = "";
  g_showing = kNone;
  hal::ISurface* glass = display::surface();
  if (glass == nullptr) {
    g_error = "this board has no glass";
    return false;
  }
  if (path == nullptr || path[0] == '\0') {
    g_error = "no picture was named";
    return false;
  }
  hal::Stat info;
  if (!vfs::stat(path, info) || info.size == 0) {
    g_error = "the picture is not there";
    return false;
  }
  if (info.size > kMostBytes) {
    g_error = "the picture is bigger than this will read";
    return false;
  }
  void* block = pal::extra_memory().allocate(info.size, true);
  if (block == nullptr) {
    g_error = "no room for the file of the picture";
    return false;
  }
  char* data = static_cast<char*>(block);
  size_t read = 0;
  if (!vfs::read_file(path, data, info.size, read) || read != info.size) {
    release(block);
    g_error = "the picture could not be read";
    return false;
  }
  img::Info picture;
  if (img::read_info(reinterpret_cast<const uint8_t*>(data), static_cast<uint32_t>(read), picture) !=
          img::kOk ||
      picture.width == 0 || picture.height == 0) {
    release(block);
    g_error = "the file is not a picture this system reads";
    return false;
  }
  // **What fits the glass, and the middle of the picture when it does not**: the wash is the whole panel
  // either way, and an image bigger than the panel is cut and not shrunk (docs/50, docs/92).
  const uint16_t glass_w = glass->width();
  const uint16_t glass_h = glass->height();
  const uint16_t width = (picture.width < glass_w) ? picture.width : glass_w;
  const uint16_t height = (picture.height < glass_h) ? picture.height : glass_h;
  const uint16_t stride = static_cast<uint16_t>((width + 1u) / 2u);
  Sink sink = {};
  sink.stride = stride;
  sink.width = width;
  sink.height = height;
  sink.skip_x = static_cast<uint16_t>((picture.width - width) / 2u);
  sink.skip_y = static_cast<uint16_t>((picture.height - height) / 2u);
  sink.pixels = static_cast<uint8_t*>(
      pal::extra_memory().allocate(static_cast<size_t>(stride) * height, true));
  if (sink.pixels == nullptr) {
    release(block);
    g_error = "no room for the picture on the glass";
    return false;
  }
  const uint32_t scratch_bytes = img::scratch_bytes(reinterpret_cast<const uint8_t*>(data),
                                                    static_cast<uint32_t>(read));
  void* scratch = (scratch_bytes > 0) ? pal::extra_memory().allocate(scratch_bytes, true) : nullptr;
  if (scratch_bytes > 0 && scratch == nullptr) {
    release(sink.pixels);
    release(block);
    g_error = "no room to decode the picture";
    return false;
  }
  const img::Status status =
      img::decode(reinterpret_cast<const uint8_t*>(data), static_cast<uint32_t>(read),
                  img::RowSink(&on_row, &sink), static_cast<uint8_t*>(scratch), scratch_bytes);
  release(scratch);
  release(block);
  if (status != img::kOk) {
    release(sink.pixels);
    g_error = "the picture is not one this system can draw";
    return false;
  }
  // **One wash and one paint**: the glass is cleaned whole, the picture goes in the middle, and the panel
  // is asked for a `gc16` of everything — the mode that takes the ghost of the page before with it
  // (docs/50, docs/92).
  const uint16_t at_x = static_cast<uint16_t>((glass_w - width) / 2u);
  const uint16_t at_y = static_cast<uint16_t>((glass_h - height) / 2u);
  glass->clear(15);
  glass->blit(at_x, at_y, width, height, sink.pixels, stride);
  const bool painted = glass->flush(hal::UpdateMode::kGc16);
  release(sink.pixels);
  if (!painted) {
    g_error = "the panel did not take the picture";
    return false;
  }
  log_printf(pal::kLogInfo, "SPLASH", "%s, %ux%u at %u,%u", path, static_cast<unsigned>(width),
             static_cast<unsigned>(height), static_cast<unsigned>(at_x), static_cast<unsigned>(at_y));
  return true;
}

bool hide() {
  g_error = "";
  g_showing = kNone;
  hal::ISurface* glass = display::surface();
  if (glass == nullptr) {
    g_error = "this board has no glass";
    return false;
  }
  // **Paper, washed whole**: the page that comes next is composed over a glass that says nothing, and a
  // picture left under it would come out at its edges (docs/30, docs/50).
  glass->clear(15);
  return glass->flush(hal::UpdateMode::kA2);
}

Kind showing() { return g_showing; }

const char* last_error() { return g_error; }

}  // namespace splash
}  // namespace kernel
