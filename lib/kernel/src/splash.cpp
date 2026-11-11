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
#include "kernel/font.h"
#include "kernel/log.h"
#include "kernel/version.h"
#include "kernel/vfs.h"
#include "pal/pal.h"

// **Nothing of this file exists on a board without a glass** (0.179.9): this is a picture of a moment —the one the
// boot, the reboot and the way down paint— and a board with no screen can never run a line of it. The question is
// asked of the board and not of a name in this file: `LUNOK_GLASS` is one line of its `board/identity.h`, and it
// is what decides which translation units exist at all. The four boards of the project without a panel —the two
// ATOMs, the bare ESP32 and the T-Koala— carry neither this code nor the libraries it pulls (docs/50, docs/75).
#include "board/identity.h"

#if LUNOK_GLASS

namespace kernel {
namespace splash {
namespace {

// **The words of the moments**, in the order of the enum: a moment is here and nowhere else.
struct Moment {
  Kind kind;
  const char* word;
};

const Moment kMoments[] = {
    {kBoot, "boot"}, {kPowerOff, "poweroff"}, {kReboot, "reboot"}, {kCharging, "charging"}, {kSleep, "sleep"},
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

// **A moment may carry more than one picture, and then the board draws one of them at every show** (0.178.5): three
// drawings named `boot-2.bmp`, `boot-3.bmp` and `boot-5.bmp` are three pictures of the boot, and which one a person
// sees comes from `ISystem::random()` —the number of the chip on the board and the seeded one of the emulator, so a run
// with `--seed` shows the same picture twice—. The tail of the name is free: `boot-night.bmp` is a picture of the boot
// like `boot-2.bmp` is, and the extension has to be one this system decodes or the file is not a picture at all.
const uint8_t kMostVariants = 8;
const uint8_t kNameBytes = 48;

// **A name that is another moment's word is that moment's picture** (0.178.5): `boot-alt.png` was a word of this
// system until the boot stopped having a second logo, and a folder that still carries it is not a drawing of the boot
// with a strange tail —it is the picture of a moment that no longer exists, and the boot does not draw it—.
// **And the words this system used to have are not drawings either**: `boot-alt` was a moment of its own until 0.178.5 and a
// disk that still carries `boot-alt.png` —the picture of a boot that the owner had taken out— must not see it drawn as one
// of the three drawings by the accident of a tail (docs/50, docs/92).
const char* const kRetired[] = {"boot-alt"};
const uint8_t kRetiredCount = static_cast<uint8_t>(sizeof(kRetired) / sizeof(kRetired[0]));

bool is_a_moment_word(const char* text, size_t length) {
  for (uint8_t i = 0; i < kMomentCount; ++i) {
    const char* name = kMoments[i].word;
    if (strlen(name) == length && strncmp(name, text, length) == 0) return true;
  }
  for (uint8_t i = 0; i < kRetiredCount; ++i) {
    const char* name = kRetired[i];
    if (strlen(name) == length && strncmp(name, text, length) == 0) return true;
  }
  return false;
}

// **The names of the moment that are there**, taken from one listing of the folder: the names of a listing are the
// backend's and live only during the call, so they are copied and the pick is made afterwards.
struct Variants : hal::ListSink {
  const char* prefix = nullptr;
  char names[kMostVariants][kNameBytes];
  uint8_t count = 0;

  bool on_entry(const hal::ListEntry& entry) override {
    if (entry.type != hal::NodeType::kFile || entry.name == nullptr) return true;
    const size_t tail = strlen(prefix);
    if (strncmp(entry.name, prefix, tail) != 0) return true;
    const char* dot = strrchr(entry.name, '.');
    if (dot == nullptr) return true;
    const size_t base = static_cast<size_t>(dot - entry.name);
    if (is_a_moment_word(entry.name, base)) return true;
    bool known = false;
    for (uint8_t i = 0; i < kExtensionCount; ++i) {
      if (strcmp(dot + 1, kExtensions[i]) == 0) known = true;
    }
    if (!known) return true;
    // **A folder with more drawings than this keeps the first of them**: a pick out of eight is a pick, and the block
    // is the stack of the turn that boots the board (docs/75).
    if (count >= kMostVariants) return false;
    snprintf(names[count], kNameBytes, "%s", entry.name);
    ++count;
    return true;
  }
};

Kind g_showing = kNone;
const char* g_error = "";
// **The words of a failure that is the panel's**, kept here because `g_error` points at them (docs/50).
char g_failure[192];

// **A failure says what the panel said** (0.118.2): `the panel did not take the picture` alone is a flash to find out
// which of the five reasons of the driver it was —the panel not up, a paint that arrived while another was moving ink,
// a command that the controller refused— and the HAL already keeps that sentence (docs/50).
void note_failure(const char* what, const char* why) {
  if (why == nullptr || why[0] == '\0') {
    snprintf(g_failure, sizeof(g_failure), "%s", what);
  } else {
    snprintf(g_failure, sizeof(g_failure), "%s: %s", what, why);
  }
  g_error = g_failure;
}

// **How long the ink of the paint before is given, in turns of twenty milliseconds** (0.118.2): a page of this glass
// moves its ink for one to three seconds —the rows are what costs— and a picture goes up once, so waiting for it is
// free and dropping it is not. Scanned on the board: a `splash <moment>` typed two seconds after the boot answered
// `the panel did not take the picture` when it arrived in the middle of the wash of the boot (docs/50, docs/92).
const int32_t kSettleTurnMs = 20;
const int32_t kSettleMostMs = 6000;

// **The ink that was moving is waited for and not argued with**: `idle()` is the HAL's question, and the driver refuses
// a paint that arrives while another one is moving ink (the maker's own driver asks for the previous one to be over
// before sending the next). Nobody is waiting on a page while a logo goes up, so this is the one place where the glass
// may be waited on (docs/50).
void settle(hal::ISurface* glass) {
  const int64_t deadline = pal::system().millis() + kSettleMostMs;
  for (;;) {
    if (glass->idle()) return;
    const int64_t left = deadline - pal::system().millis();
    if (left <= 0) return;
    // Sleeping through the queue keeps the CPU idle instead of spinning, the same wait the console settle uses.
    pal::Event event;
    pal::system().queue()->pop(event, static_cast<int32_t>((left < kSettleTurnMs) ? left : kSettleTurnMs));
  }
}

// **The stamp of the build**: `lunokOS v<version>` in the bottom right corner of the glass, in black with a margin, at
// scale two of the face the system draws with —the five by seven of the flash at the boot, the bold face of the disk
// later, which is the same face the bar and the prompt use— (0.165.0, docs/30, docs/50).
void draw_version(hal::ISurface& surface) {
  char text[40];
  snprintf(text, sizeof(text), "lunokOS v%s", kVersion);
  const uint8_t scale = 2;
  const uint16_t text_w = font::width(text, scale);
  const uint16_t line_h = static_cast<uint16_t>(font::system_face().height * scale);
  const uint16_t margin = 16;
  if (surface.width() <= text_w + margin || surface.height() <= line_h + margin) return;
  font::draw(surface, static_cast<uint16_t>(surface.width() - text_w - margin),
             static_cast<uint16_t>(surface.height() - line_h - margin), text, scale, 0);
}

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

bool picture_of(Kind kind, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return false;
  out[0] = '\0';
  const char* name = word(kind);
  if (name[0] == '\0') return false;
  // **The drawings of the moment come first** (0.178.5): a disk that carries three of them says «this moment is
  // drawn», and the plain name is what a disk with one picture has. A folder with none of either is not an error
  // here; it falls to the picture of every moment through `path_of`.
  char prefix[16];
  snprintf(prefix, sizeof(prefix), "%s-", name);
  Variants variants;
  variants.prefix = prefix;
  hal::Stat info;
  if (vfs::list(kFolder, variants) && variants.count > 0) {
    const uint8_t pick = static_cast<uint8_t>(pal::system().random() % variants.count);
    snprintf(out, cap, "%s/%s", kFolder, variants.names[pick]);
    if (vfs::stat(out, info) && info.size > 0) return true;
    out[0] = '\0';
  }
  return path_of(kind, out, cap);
}

bool available(Kind kind) {
  char path[96];
  return display::surface() != nullptr && picture_of(kind, path, sizeof(path));
}

bool show(Kind kind) {
  g_error = "";
  if (kind == kNone) {
    g_error = "no such moment";
    return false;
  }
  char path[96];
  if (!picture_of(kind, path, sizeof(path))) {
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
  // **The version of the build, in a corner and in the face of the system** (0.165.0): a board on a table has no console,
  // and the one thing a person asks of the picture at the boot is which build it is. Black letters, a margin of sixteen
  // pixels, the bottom right corner and **scale two** —a stamp and not a text— over whatever the picture has there, which
  // is the owner's wish: discreet (docs/30, docs/50).
  draw_version(*glass);
  // **The wash before is over before this one is asked for** (0.118.2): the panel refuses a paint that arrives while
  // it is moving ink, and the boot's own wash of the whole page is still running when a person types quickly.
  settle(glass);
  const bool painted = glass->flush(hal::UpdateMode::kGc16);
  release(sink.pixels);
  if (!painted) {
    note_failure("the panel did not take the picture", glass->last_error());
    return false;
  }
  log_printf(pal::kLogInfo, "SPLASH", "%s, %ux%u at %u,%u", path, static_cast<unsigned>(width),
             static_cast<unsigned>(height), static_cast<unsigned>(at_x), static_cast<unsigned>(at_y));
  return true;
}

bool wait() {
  hal::ISurface* glass = display::surface();
  if (glass == nullptr) {
    g_error = "this board has no glass";
    return false;
  }
  // **The first turn is not asked of the glass but given to it** (0.165.1): the driver takes the command of the paint on
  // its next turn, so a board that asked "is the ink over?" in the same millisecond as `show` was answered "yes" about the
  // ink of before. One turn of the queue and then the glass is asked, and `settle` is the same wait the picture already
  // uses before it, with its own ceiling (docs/50).
  pal::Event event;
  pal::system().queue()->pop(event, kSettleTurnMs);
  settle(glass);
  return glass->idle();
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

#endif  // LUNOK_GLASS
