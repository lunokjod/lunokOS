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
//
// **The words are the ones a person types** (0.114.0): `boot`, `poweroff`, `reboot`, `charging`, `sleep`, `lowbatt` and
// `oops`, which are the ones of the command and the ones a manifest may name. **`boot-alt` is gone**
// (0.178.5, the owner: *«boot-alt.png i boot.png no han d'estar a la llista del boot»*): the boot is not
// one drawing but the drawings the disk carries —`boot-2.bmp`, `boot-3.bmp`, `boot-5.bmp`— and one of
// them is drawn at every switch-on (`picture_of`), so a second word for a second logo is a switch of the
// paint that the pictures themselves already make.
enum Kind : uint8_t {
  kNone = 0,
  kBoot,       // coming up, before the first page: one of the boot drawings of the disk
  kPowerOff,   // going down: a deep sleep, the power off, the board that stops
  kReboot,     // the board is being restarted under a person's hand
  kCharging,   // on the cable, waiting
  kSleep,      // a light sleep, where the board keeps what it has
  // **La bateria que s'ha acabat** (0.183.0): the picture of a board that has almost nothing left —it is the boot's when
  // the cell is under the mark, and the way down's when the battery is the reason— so a person finds `lowbatt` instead of
  // a board that went dark on its own (docs/50).
  kLowBattery,
  // **El sistema que no pot donar cap pàgina** (0.183.0): the face of a board whose own home app did not come up. It is the
  // one error this system can show and go on living with —a panic cannot be painted, the chip that panics is not driving
  // the glass— and it is a picture and not a frozen logo (docs/50).
  kOops,
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
// **Which picture of a moment would be drawn**: a moment may carry several —`boot-2.bmp`, `boot-3.bmp`— and then one
// of them is picked at random, so that the picture of the boot is not the same on every switch-on. `path_of` is the
// plain name and this is the one `show` asks for; with no drawings of its own it answers what `path_of` answers.
bool picture_of(Kind kind, char* out, size_t cap);
// Whether there is a glass to paint on and a picture for that moment.
bool available(Kind kind);

// **The picture of a moment on the glass**, washed whole first. False when there is no glass, no
// picture, or the file cannot be read, and then the glass is left as it was.
bool show(Kind kind);
// The same, for a picture the caller names.
bool show(const char* path);
// **Gives the glass back**: paper, washed whole, and no picture of any moment.
bool hide();
// **Waits until the ink of the last picture is over** (0.165.1): `show` waits for the ink that was moving **before** its
// picture and then asks for a `gc16` of the whole glass, which is half a second of ink — and a caller that cuts the power
// or restarts the board in the middle of it shows the page of before and not the picture. A restart, a power-off and a
// deep sleep ask this **before they go**. True when the glass says the ink is over, false when there is no glass or the
// ceiling of the wait ran out, because a panel that never answers must not keep a board from restarting (docs/50).
bool wait();
// The moment that is on the glass, or `kNone` when the last thing that happened was `hide` or a failure.
Kind showing();
// Why the last call that answered false did, in words for a log or a line of the console.
const char* last_error();

}  // namespace splash
}  // namespace kernel

#endif  // LUNOK_KERNEL_SPLASH_H
