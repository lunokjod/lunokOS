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

#include <stddef.h>
#include <stdint.h>

#include "hal/hal.h"

namespace kernel {
namespace ui {

// One region a canvas painted and the quality it asked for, **in that canvas' own coordinates**. Each
// canvas keeps a short list, merges what it can, and a present paints each quality once (docs/30).
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

// **Two ceilings and not one.** A control is cheap —a frame and its lists— and there can be many; a view
// is four bits per pixel and there can be few. It is the difference between a page with twenty labels and
// a page with eight (docs/90).
const uint8_t kMaxControls = 32;
// How many of those controls may own a buffer at once, the root included.
const uint8_t kMaxCanvases = 8;

// **A control is a frame, and a frame may or may not have a view.** The frame is the size and the place it
// occupies inside the one that holds it, its father, its ordered children, what it owes and what it draws;
// the view is a buffer of four bits per pixel —one byte per two, the left one in the high nibble, the same
// packing the glass takes—, and **it is optional**: a `UICanvas` has one, a grid or a box or a spacer does
// not and paints into the first canvas that holds it, cut at its own frame. The app's view is the root and
// it always has one. At a present each view is composed into the first canvas that holds it, in the order
// the controls were added, until the tree is one picture on the glass (docs/90).
struct Canvas {
  uint16_t width = 0;
  uint16_t height = 0;
  uint16_t stride = 0;  // bytes of a line: half the width, because a canvas is a whole number of bytes
  // The view of this control, or nullptr when it is a control that only holds children and paints nothing
  // (a box, a grid, a spacer). **`bits == nullptr` is not "a free slot"**: that is `in_use` (docs/90).
  uint8_t* bits = nullptr;
  uint32_t bytes = 0;
  bool in_use = false;
  // Where this canvas sits in its parent, in the parent's pixels. For the root, where the view sits on
  // the glass. The system decides it and an app only reads it (docs/30).
  uint16_t x = 0;
  uint16_t y = 0;
  // What it painted since the last composition: **that is all a parent copies**, which is why a text
  // over a picture is transparent without a mask and a layer costs what it changes (docs/90).
  Damage damage[kMaxDamage];
  uint8_t damage_count = 0;
  // **One father and ordered children.** The canvases inside this one are here in the order they were
  // added, and that order is the order of the layers: the one added later is in front (docs/90).
  int8_t parent = -1;
  int8_t children[kMaxControls];
  uint8_t child_count = 0;
  // A number that changes every time the slot is handed out again: an app that keeps an object past the
  // moment the view was made again holds a stale slot and has to be told, not write into whatever took
  // its place (docs/90).
  uint16_t generation = 0;
  // **What this canvas has to redraw itself.** A control with nothing changed is a control that does
  // nothing at all: no `on_draw`, no pixels, no paint. It is set when the canvas is made, when it moves,
  // when it changes father, and when the app says so, and **it goes up to the fathers**, because a child
  // that moved left its pixels inside its father's buffer and the father is the one that has to paint
  // them over (docs/90).
  bool needs_draw = true;
  // **A control on its way out.** A control with a view cannot be freed the moment the app says so: its
  // pixels are still in the canvas that holds it, and they have to be painted over first. It is marked,
  // the painter is told the rectangle it leaves, its buffer is wiped white and only **after the clean
  // paint** is the slot given back —no ghost on the paper and nothing stale left in a buffer somebody else
  // will get— (docs/90).
  bool pending_release = false;
  // **What it owes, and while it is drawing itself it may paint nothing else.** A control that has to be
  // redrawn because a child moved owes the rectangle that child left behind, not the page: the box is
  // kept here and every primitive of the canvas is cut against it while the control draws (docs/30,
  // docs/90).
  int32_t redraw_x = 0;
  int32_t redraw_y = 0;
  int32_t redraw_w = 0;
  int32_t redraw_h = 0;
  bool in_draw = false;
  // **The frame of the control that is drawing on this canvas**, when the drawing comes from a control
  // without a view of its own: a control cannot paint outside its frame, and its children cannot either.
  // It is the same cut as the redraw box and it is applied first (docs/90).
  bool clipped = false;
  int32_t clip_x = 0;
  int32_t clip_y = 0;
  int32_t clip_x1 = 0;
  int32_t clip_y1 = 0;
  // **The face its text is drawn with**, as a handle (`ui::font_load`), and -1 is the font of the system.
  // It is a number and not a pointer because the face lives in a table that an app may give back: the
  // canvas that used it draws with the system's again instead of pointing at a freed block (docs/30).
  int8_t face = -1;
};

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

// ---- the canvases inside the view: a control is one of these (docs/90) ----

// The canvas at that slot, or nullptr. Slot 0 is the view of the app, which the system owns.
const Canvas* at(int8_t slot);
// A control of its own inside `parent` (`-1` is the view). **With a buffer or without one**: a control with
// a buffer (`ui.canvas`) is composed onto its father as a layer of its own; a control without one
// (`ui.control`) paints into the first canvas that holds it, cut at its own frame, and costs no pixels at
// all. False when there is no slot left, when there is no room for another buffer and one was asked for,
// or when the extra memory does not take it.
bool create(uint16_t width, uint16_t height, int8_t parent, bool with_view, int8_t& slot);
// Gives the buffer back and moves the slot's generation on: an object of an app that still points here
// is refused instead of writing into whoever took the slot (docs/90).
bool release(int8_t slot);
// Where the canvas sits in its parent, in the parent's pixels.
bool place(int8_t slot, uint16_t x, uint16_t y);
// **Puts a canvas inside another one**, which is what a control holding a control is: it becomes the
// last child of `parent` —so it is in front of the ones already there— and its place is now read in the
// new parent's pixels. The view of the app is the one canvas an app cannot move into another (docs/90).
bool reparent(int8_t slot, int8_t parent);
// The father of a canvas, or -1 (the view of the app has none).
int8_t parent_of(int8_t slot);

// ---- redrawing, and only when it is needed (docs/90) ----

// **Marks this canvas and every canvas that holds it** as needing a redraw. A child that changed is a
// father whose buffer is not what the child has on it any more. Without a rectangle it is the whole
// canvas; with one, **that** is what has to be painted, which is what keeps a repaint cheap (docs/90).
bool invalidate(int8_t slot);
bool invalidate_rect(int8_t slot, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
// The turn of a redraw: the system opens the box before asking a control to draw itself and closes it
// after, and only while it is open is the drawing cut at the box (docs/90).
bool begin_draw(int8_t slot);
bool end_draw(int8_t slot);
// Whether it has to be drawn. False is "nothing changed": the control is not asked to draw at all.
bool needs_draw(int8_t slot);
// It has been drawn: nothing more to do until something changes it again.
bool mark_drawn(int8_t slot);
// How many times the slot has been handed out (`at(slot)->generation`), for the object of an app that has
// to know whether the canvas it holds is still the one it made.
uint16_t generation_of(int8_t slot);

// Drawing into a canvas that is not the view. The same calls the view has, and the same rules: a region
// is kept in whole nibbles and nothing is painted outside the canvas (docs/30).
bool canvas_clear(int8_t slot, uint8_t level, const char* mode);
bool canvas_fill_rect(int8_t slot, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t level,
                      const char* mode);
bool canvas_pixel(int8_t slot, uint16_t x, uint16_t y, uint8_t level, const char* mode);
uint8_t canvas_damage(int8_t slot);

// ---- the primitives of a canvas, and the text (docs/30, docs/90) ----
//
// Each one paints **inside the canvas and nowhere else** and leaves **one** region of damage —the box the
// shape covers— instead of one per pixel: a line must not look like two hundred separate pixels, or the
// list of eight fills and the whole canvas becomes one `gc16` paint (docs/50).
//
// `level` is the colour, and on this glass the colour is a level from 0 (black) to 15 (white); `mode` is
// the quality, and `nullptr` is `du`. A `frame` is a rectangle of one pixel drawn round the outside of
// the box and a `rect` is the box filled; a `circle` is the line round and a `disc` is the inside.
bool canvas_frame(int8_t slot, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t level,
                  const char* mode);
bool canvas_line(int8_t slot, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t level,
                 const char* mode);
bool canvas_circle(int8_t slot, int16_t center_x, int16_t center_y, int16_t radius, uint8_t level,
                   const char* mode);
bool canvas_disc(int8_t slot, int16_t center_x, int16_t center_y, int16_t radius, uint8_t level,
                 const char* mode);
// A text drawn into the canvas with **the face the canvas was given**, or with the font of the system when
// it was given none, `scale` times its size and **cut at the border of the canvas**: that is what a label is
// made of (docs/30, docs/90).
// **A picture on the canvas**, read from a file of the volume and turned into rows of the canvas as it is
// decoded (docs/30): `w` or `h` zero is the size of the picture, and a size that is not the picture's is a
// nearest neighbour scale. `reason` says why when it is false.
bool canvas_image(int8_t slot, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char* path,
                  const char* mode, const char*& code, const char*& reason);

// The same on the app's own view, which is what `ui.image` is (docs/30).
bool image(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char* path, const char* mode,
           const char*& code, const char*& reason);

// **What a picture is**, without drawing it: the family as four letters, the size, and the depth of the file
// (docs/30).
bool image_info(const char* path, char* format, size_t format_cap, uint16_t& width, uint16_t& height,
                const char*& code, const char*& reason);

bool canvas_text(int8_t slot, int16_t x, int16_t y, const char* text, uint8_t scale, uint8_t level,
                 const char* mode);

// ---- a label: one text in the frame of a control (docs/30, docs/90) ----
//
// A label is a control without a buffer: its frame is the border that cuts the text, and drawing it is a
// background and one line. The face is the one the canvas was given (`canvas_set_font`), and the three
// numbers that place the line are the horizontal alignment, the vertical one and the size of the body.
enum class LabelAlign : uint8_t { kLeft = 0, kCentre = 1, kRight = 2 };
enum class LabelVAlign : uint8_t { kTop = 0, kMiddle = 1, kBottom = 2 };

// **The text a label of that width draws**: the whole one when it fits, or the longest prefix that with
// `"..."` fits. `out` is always NUL-terminated and the return is the width in pixels of what it holds, which
// is what centring it needs. When even the three dots do not fit, they are what is drawn and the frame cuts
// them (docs/30, docs/90).
uint16_t label_text(int8_t face, const char* text, uint16_t width, uint8_t scale, char* out, size_t cap);

// **Draws a label into its frame**, with its own face and colours: the background first —a control without
// a buffer owes the pixels it covers— and then the text, aligned and cut at the frame. The app's `present`
// is what calls it, and only when the control owes a draw (docs/30, docs/90).
bool label_draw(int8_t slot, const char* text, uint8_t color, uint8_t background, uint8_t align,
                uint8_t valign, uint8_t scale);

// **Draws a button into its frame**: a background and a border, a second inner border while it is pressed
// and another one two pixels in while it has the focus. A button is a control without a buffer, so what it
// paints is the frame it covers and nothing else; its children —a label, for instance— are composed after
// it and clipped to the same frame (docs/30, docs/90).
bool button_draw(int8_t slot, uint8_t background, uint8_t border, bool pressed, bool focused = false);

// ---- the faces a text can be drawn with (docs/30) ----
//
// The system has one font and it is in the flash; an app may load another from a **portable font file**
// (`res/fonts/*.lfont`, plain text, one glyph per block) and use it on any canvas it owns. The block of
// glyphs is the app's —it comes from the extra memory and counts against its budget— and it lives until the
// app gives it back or the app ends (docs/30, docs/75).
//
// A handle is a number; -1 is the face of the system. `reason` says why a file is not a face, and it is the
// string an app shows.
const uint8_t kMaxFaces = 3;
int8_t font_load(const char* path, const char*& reason);
bool font_release(int8_t handle);
// What a face says of itself: how big a glyph is, the space between two, and how many codes it carries. **A
// face of outlines has no cell**, so `width` is zero and the count is what the file says it has —a font of a
// thousand letters does not fit a byte and is not made to fit one— (docs/30).
bool font_info(int8_t handle, uint8_t& width, uint8_t& height, uint8_t& gap, uint16_t& glyphs);
// **The face of a canvas**, and the face of the app's view for the shorthand calls (`ui.text`). -1 is the
// font of the system. A canvas whose face is given back draws with the system's again, which is why the
// number is checked every time and never trusted (docs/30).
bool canvas_set_font(int8_t slot, int8_t handle);
bool app_set_font(int8_t handle);
// How wide a text is with that face, in pixels, which is what centring it needs (docs/30).
uint16_t font_text_width(int8_t handle, const char* text, uint8_t scale);

// ---- what it takes to leave the page clean (docs/50, docs/90) ----
//
// Between two apps the system owes a clean screen, and **what that costs is the system's to choose**: a page
// of text —levels 0 and 15 and nothing else— is washed with `a2`, which is the two-level wash; a page with a
// few grays takes `gl16`, which washes sixteen levels without the black flash; a photograph takes `gc16`, and
// so does any page that has taken many quick paints, because what it leaves is ghost and only a full wash
// takes ghost away. On this glass that is 290 ms against 450, per app change (docs/50).
hal::UpdateMode cleaner_for(uint16_t levels, uint16_t paints);
// Which levels the page has shown since it was last cleaned, as a bit per level, and how many boxes of
// pixels have been put down: what `cleaner_for` reads, and what a test asks (docs/90).
uint16_t levels_shown();
uint16_t paints_since_clean();

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
