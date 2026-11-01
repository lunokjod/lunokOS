// The view of an app: **its own canvas**, the damage it leaves on it, and the moment it asks the
// system to put that damage on the glass (docs/30).
//
// The glass has one buffer and it is the system's (`display.h`). An app draws here, in a canvas that
// is created when the app starts and can be made a different size while it runs. Every draw records
// **the region it touched and the quality it asked for**; `ui.present` translates those regions to
// their place on the glass, composes them and asks for each quality to be painted once. That is the
// whole cost model of an e-ink panel: you pay for the pixels that moved and for how good they have to
// look, and nothing else (docs/50).
//
// The view is **not** the glass. A canvas has a size and a place, and the two are the system's to
// choose: a second view next to this one, a strip the system keeps, or a glass that is rotated means a
// canvas that is smaller than the panel and somewhere else on it. Nothing in this file assumes the
// canvas covers the panel.
//
// **Nothing here draws by itself.** No splash, no bar, no clearing when an app closes: the canvas is
// white when the app attaches, and the glass changes only when an app presents.
#ifndef LUNOK_KERNEL_UI_H
#define LUNOK_KERNEL_UI_H

#include <stdint.h>

#include "hal/hal.h"

namespace kernel {
namespace ui {

// The app's canvas: four bits per pixel, one byte per two of them and the left one in the high
// nibble, so it is the same packing the glass takes and a present is a copy (docs/50).
struct Canvas {
  uint16_t width = 0;
  uint16_t height = 0;
  uint16_t stride = 0;  // bytes of a line: half the width, rounded up to a whole byte
  uint8_t* bits = nullptr;
  uint32_t bytes = 0;
  // Where the view sits on the glass, in panel pixels. The system decides it and an app only reads it
  // (docs/30).
  uint16_t x = 0;
  uint16_t y = 0;
};

// One region the app drew and the quality it asked for, **in the canvas' own coordinates**. The system
// keeps a short list, merges what it can, and paints each quality once per present (docs/30).
struct Damage {
  uint16_t x = 0;
  uint16_t y = 0;
  uint16_t w = 0;
  uint16_t h = 0;
  hal::UpdateMode mode = hal::UpdateMode::kDu;
};

// How many regions the list holds. Past this, everything pending becomes one region with the quality
// that cannot look wrong (`gc16`), and `damage_count()` is how an app sees it happening (docs/30).
const uint8_t kMaxDamage = 8;

// Makes the canvas, at the size of the glass **minus the rows the system keeps at the top** and just
// below them. Called once, after `display::boot` (docs/30).
bool boot(hal::IPlatform& platform, uint16_t top_rows);
bool available();
const Canvas& canvas();
uint16_t width();
uint16_t height();
uint8_t levels();

// A fresh view for the app that is starting: white, nothing drawn and no damage. **The glass is not
// touched**: the panel keeps what it held until the app presents (docs/30).
void attach();

// Drawing. `mode` is the quality the app wants for this region; the default is `du`, the one a menu or
// a line of text wants. Each call records its region and its quality and nothing reaches the glass.
bool clear(uint8_t level, const char* mode);
bool fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t level, const char* mode);
bool pixel(uint16_t x, uint16_t y, uint8_t level, const char* mode);

// What is pending, for a test or for an app that wants to know how much it is about to pay for.
uint8_t damage_count();
const Damage& damage(uint8_t index);

// Composes the damage onto the board's surface —translated to where the view sits— and asks for each
// region to be painted with the quality it was drawn with. Returns **before the ink has moved**
// (docs/30, docs/50).
bool present();
// The same, with one quality for everything pending: what an app asks for when it wants the whole page
// to look the same, or when it is about to leave a page behind (docs/30).
bool present_all(hal::UpdateMode mode);

bool idle();

// The title of this app in the bar of the system. The bar is the system's and the title on it is the
// app's: that is the one thing about the chrome an app may say (docs/30).
bool title(const char* text);

// Where the view sits on the glass and how big it is. **The system's, not an app's**: it is what a
// second view, a strip of the system or a rotated glass will move (docs/30).
bool place(uint16_t x, uint16_t y);
bool resize(uint16_t width, uint16_t height);

// **Turns the page**: 0, 90, 180 or 270 degrees, and the whole system goes with it —the glass, the bar and
// the fingers, which read it from the same place— because the frame of an app is made again at the new
// size and the app is told with `on_resize` (docs/30, docs/50).
bool set_rotation(uint16_t degrees);
uint16_t rotation();

}  // namespace ui
}  // namespace kernel

#endif  // LUNOK_KERNEL_UI_H
