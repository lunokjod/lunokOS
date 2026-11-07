// **A picture on the glass at the moments of the system**: the boot, the way down, the charge.
//
// This is the one thing of the system that paints the panel **outside a page**: it writes on the surface
// of the board (`hal::ISurface`) and not on the canvas an app draws on, because the moments it exists
// for are the ones where no app is in front — a board that is coming up has no shell yet, and one that
// is going down has just closed its app— (docs/30, docs/50).
//
// **Two calls and one rule**: the panel is washed **whole** —one `gc16` of the entire glass, so the
// ghost of what was there is gone and not only the rectangle of the picture— and the image sits **in the
// middle**, at its own size and never scaled: an image bigger than the glass is cut at its borders, and
// one that fits is left whole. A moment whose picture is not on the disk paints nothing and says why,
// because a system that paints a guess over a broken file is a system that hides a broken file
// (docs/30, docs/50, docs/92).
//
// **What a moment looks for**, in this order, with the extensions the decoder reads:
//   `/res/splash/<word>.png` (and `.bmp`, `.jpg`)   the picture of that moment
//   `/res/splash.png` (and `.bmp`, `.jpg`)           the picture of every moment
// An image is passed by path with `show(path)`, which is what an app or a command uses.
#ifndef LUNOK_KERNEL_SPLASH_H
#define LUNOK_KERNEL_SPLASH_H

#include <stddef.h>
#include <stdint.h>

namespace kernel {
namespace splash {

// **The moments this system has a picture for.** The word of each one is what names its file, and the
// list is the one place a moment is added: a picture called `charging.png` is the charging moment of a
// build that knows the word, and a file of a word nobody knows is a file nothing looks for (docs/30).
enum Kind : uint8_t {
  kNone = 0,
  kBoot,       // coming up, before the first page
  kShutdown,   // going down: a deep sleep or a power off
  kCharging,   // on the cable, waiting
  kSleep,      // a light sleep, where the board keeps what it has
};

// The word of a moment, or "" for one that is not one.
const char* word(Kind kind);
// The moment a word names, or `kNone`.
Kind kind_of(const char* word);
// **Every moment, in the order above**, for a listing or a command that offers them all.
uint8_t count();
Kind at(uint8_t index);

// Where the picture of a moment is, if the disk carries one: the file of the moment first and the one
// of every moment after it. False is "this build has no picture for that", which is not an error.
bool path_of(Kind kind, char* out, size_t cap);
// Whether there is a glass to paint on and a picture for that moment.
bool available(Kind kind);

// **The picture of a moment on the glass**, washed whole first. False when there is no glass, no
// picture, or the file cannot be read, and then the glass is left as it was.
bool show(Kind kind);
// The same, for a picture the caller names.
bool show(const char* path);
// **Gives the glass back**: paper, washed whole, and no picture of any moment.
bool hide();
// The moment that is on the glass, or `kNone` when the last thing that happened was `hide` or a failure.
Kind showing();
// Why the last call that answered false did, in words for a log or a line of the console.
const char* last_error();

}  // namespace splash
}  // namespace kernel

#endif  // LUNOK_KERNEL_SPLASH_H
