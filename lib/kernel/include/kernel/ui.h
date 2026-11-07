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

#include "epub/page.h"
#include "hal/hal.h"
// **A canvas draws a text with an ink** (0.129.0): the bold, the italic and the step off the baseline live in
// `kernel/font.h`, and this header names them instead of repeating the three fields (docs/46, A4).
#include "kernel/font.h"

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

// **The number of controls is what the memory holds, and not a number in this file** (0.102.0). The table of
// canvases is taken from the board's extra memory at boot —the PSRAM of the Paper, which has three and a half
// megabytes idle— and `canvas_count()` says how many slots there are; a hundred and ninety-two bytes each, so
// thousands. The buffers are the ones that cost: a view is four bits per pixel, a quarter of a megabyte for a
// page of this size, and **that** is the ceiling that a page meets first (docs/75, docs/90).
const uint16_t kMaxCanvases = 8;
// A slot is a number in the table and it is an `int16_t`: the type is the only ceiling left on how many controls a
// page may have, and it is thirty-two thousand. **`-1` is "no slot" and it is signed**, or a comparison with a slot
// would be a comparison of 65535 with -1 (docs/90).
const int16_t kNoSlot = -1;

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
  int16_t parent = kNoSlot;
  // **The children are a list and not an array** (0.102.0): the first one and the next of each of them, so a
  // control with two hundred children costs the same table as one with two, and there is no ceiling to hit.
  int16_t first_child = kNoSlot;
  int16_t next_sibling = kNoSlot;
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
const Canvas* at(int16_t slot);

// **The face behind a handle of `font_load`**, or the face of the system for a negative one: it is what the reader
// of a book needs to know which typeface an app asked for, and it is the same table `canvas_set_font` uses
// (docs/30, docs/46 C13).
const font::Face& font_face(int8_t handle);
// **How many controls this board can hold**, which is how many slots the table of canvases has: it is asked of the
// extra memory at boot and not written in this file (0.102.0, docs/75).
uint16_t canvas_count();
// A control of its own inside `parent` (`-1` is the view). **With a buffer or without one**: a control with
// a buffer (`ui.canvas`) is composed onto its father as a layer of its own; a control without one
// (`ui.control`) paints into the first canvas that holds it, cut at its own frame, and costs no pixels at
// all. False when there is no slot left, when there is no room for another buffer and one was asked for,
// or when the extra memory does not take it.
bool create(uint16_t width, uint16_t height, int16_t parent, bool with_view, int16_t& slot);
// Gives the buffer back and moves the slot's generation on: an object of an app that still points here
// is refused instead of writing into whoever took the slot (docs/90).
bool release(int16_t slot);
// Where the canvas sits in its parent, in the parent's pixels.
bool place(int16_t slot, uint16_t x, uint16_t y);
// **Puts a canvas inside another one**, which is what a control holding a control is: it becomes the
// last child of `parent` —so it is in front of the ones already there— and its place is now read in the
// new parent's pixels. The view of the app is the one canvas an app cannot move into another (docs/90).
bool reparent(int16_t slot, int16_t parent);
// The father of a canvas, or -1 (the view of the app has none).
int16_t parent_of(int16_t slot);

// ---- redrawing, and only when it is needed (docs/90) ----

// **Marks this canvas and every canvas that holds it** as needing a redraw. A child that changed is a
// father whose buffer is not what the child has on it any more. Without a rectangle it is the whole
// canvas; with one, **that** is what has to be painted, which is what keeps a repaint cheap (docs/90).
bool invalidate(int16_t slot);
bool invalidate_rect(int16_t slot, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
// The turn of a redraw: the system opens the box before asking a control to draw itself and closes it
// after, and only while it is open is the drawing cut at the box (docs/90).
bool begin_draw(int16_t slot);
bool end_draw(int16_t slot);
// Whether it has to be drawn. False is "nothing changed": the control is not asked to draw at all.
bool needs_draw(int16_t slot);
// It has been drawn: nothing more to do until something changes it again.
bool mark_drawn(int16_t slot);
// How many times the slot has been handed out (`at(slot)->generation`), for the object of an app that has
// to know whether the canvas it holds is still the one it made.
uint16_t generation_of(int16_t slot);

// Drawing into a canvas that is not the view. The same calls the view has, and the same rules: a region
// is kept in whole nibbles and nothing is painted outside the canvas (docs/30).
bool canvas_clear(int16_t slot, uint8_t level, const char* mode);
bool canvas_fill_rect(int16_t slot, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t level,
                      const char* mode);
bool canvas_pixel(int16_t slot, uint16_t x, uint16_t y, uint8_t level, const char* mode);
uint8_t canvas_damage(int16_t slot);

// ---- the primitives of a canvas, and the text (docs/30, docs/90) ----
//
// Each one paints **inside the canvas and nowhere else** and leaves **one** region of damage —the box the
// shape covers— instead of one per pixel: a line must not look like two hundred separate pixels, or the
// list of eight fills and the whole canvas becomes one `gc16` paint (docs/50).
//
// `level` is the colour, and on this glass the colour is a level from 0 (black) to 15 (white); `mode` is
// the quality, and `nullptr` is `du`. A `frame` is a rectangle of one pixel drawn round the outside of
// the box and a `rect` is the box filled; a `circle` is the line round and a `disc` is the inside.
bool canvas_frame(int16_t slot, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t level,
                  const char* mode);
bool canvas_line(int16_t slot, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t level,
                 const char* mode);
bool canvas_circle(int16_t slot, int16_t center_x, int16_t center_y, int16_t radius, uint8_t level,
                   const char* mode);
bool canvas_disc(int16_t slot, int16_t center_x, int16_t center_y, int16_t radius, uint8_t level,
                 const char* mode);
// A text drawn into the canvas with **the face the canvas was given**, or with the font of the system when
// it was given none, `scale` times its size and **cut at the border of the canvas**: that is what a label is
// made of (docs/30, docs/90).
// **A picture on the canvas**, read from a file of the volume and turned into rows of the canvas as it is
// decoded (docs/30): `w` or `h` zero is the size of the picture, and a size that is not the picture's is a
// nearest neighbour scale. `reason` says why when it is false.
bool canvas_image(int16_t slot, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char* path,
                  const char* mode, const char*& code, const char*& reason);

// The same on the app's own view, which is what `ui.image` is (docs/30).
bool image(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char* path, const char* mode,
           const char*& code, const char*& reason);

// **What a picture is**, without drawing it: the family as four letters, the size, and the depth of the file
// (docs/30).
bool image_info(const char* path, char* format, size_t format_cap, uint16_t& width, uint16_t& height,
                const char*& code, const char*& reason);

bool canvas_text(int16_t slot, int16_t x, int16_t y, const char* text, uint8_t scale, uint8_t level,
                 const char* mode);

// **A text with the ink a book puts inside a line** (0.129.0, docs/46 A4): a bold is the same word one pixel more
// to the right, an italic is a slant row by row, and a `sup`/`sub` is a step off the baseline. It is the primitive
// the reader of a book composes its pages with, and it is here and not in a call of Lua because what needs it is
// the system and not an app (docs/30, docs/46 D5).
//
// **It says which face** (0.132.0): a page of a book has three —the words, the titles and the caption— and a canvas
// has one, so the call names the one it wants; `-1` is the face the canvas draws with, which is what the other
// primitives use (docs/30).
bool canvas_text_styled(int16_t slot, int8_t font, int16_t x, int16_t y, const char* text, uint8_t scale,
                        uint8_t level, const char* mode, const font::Ink& ink);

// **A page of a book, painted** (0.132.0, docs/46 A5 and D2): walks the plan the paginator made —the words with
// their face, their step and their ink, the rules and the boxes of the pictures— and paints it in the canvas. The
// faces are handed in by number (`ui.load_font`), one per role, and `-1` is the face of the system.
struct PageFonts {
  int8_t body = -1;
  int8_t title = -1;
  int8_t small = -1;
};
bool canvas_book_page(int16_t slot, const epub::Item* items, const char* words, int16_t count,
                      const PageFonts& fonts, uint8_t level, const char* mode);

// **A block of pixels into a canvas** (0.133.0, docs/46 D4): four bits a pixel, two of them in a byte with the left
// one in the high nibble —the packing of the glass, the canvas and `img::decode`—, copied row by row and cut at the
// canvas. It is what the pictures of a book are painted with, and a picture is a copy and not a conversion.
//
// **`dark` is the page with its ink and its paper swapped** (0.137.0, docs/46 C9): every level is copied the other way
// round, because a plate whose paper is white is a hole in a page that is black. The invention is done while the copy is
// made, so a picture that comes from the cache of the book costs the same pass and no second decode (docs/46, A5).
bool canvas_bitmap(int16_t slot, int16_t x, int16_t y, uint16_t w, uint16_t h, const uint8_t* bits,
                   uint16_t stride, const char* mode, bool dark = false);

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
bool label_draw(int16_t slot, const char* text, uint8_t color, uint8_t background, uint8_t align,
                uint8_t valign, uint8_t scale);

// **Draws a button into its frame, with the drawing of the paper project** (0.97.0): a box in the colour of its shadow
// with the face lifted four pixels off the bottom of it —**the strip that is left in sight under the face is what makes it
// a key and not a rectangle**—, a rounded corner of fourteen, and the face **inverted** while the button is the one that
// will answer: black face and paper text, because the focus and the finger have to say the same thing. A primary button
// wears a frame of three pixels and the rest one, and the frame goes on the **face** and not on the box, or it would stay
// where it was while the button goes down. `title` is the word inside —drawn by the control, so an inverted button has an
// inverted word— and `tag` is a small word in an outlined box at its left that says what kind of thing the row is. A
// child added with `add` is still composed after the box and cut at the frame, and it is **not** inverted: a control the
// app paints keeps the colours the app gave it (docs/30, docs/90, docs/92).
bool button_draw(int16_t slot, uint8_t background, uint8_t border, bool pressed, bool focused = false,
                 bool primary = false, const char* title = nullptr, const char* tag = nullptr,
                 uint8_t color = 0, uint8_t scale = 1);

// ---- a checkbox: a square with a tick, and the state the click changes (docs/30, docs/90) ----
//
// **A checkbox is a row**: a square box at its left and the words that say what is being checked, which is a
// child control the app adds, exactly as a button's text is. The frame is the row, and the box inside it is
// what this control draws; the pixels between the box and the right edge are the app's.

// **The side of the box this control draws**, in pixels: a square that leaves two pixels of frame above and
// below and is never thinner than six, or zero when the frame is too small to hold one. An app asks for it to
// place the label that names the setting —`label:place(cb:box() + 8, 0)`— instead of guessing the geometry
// (docs/30, docs/90).
int16_t checkbox_side(int16_t slot);

// **Draws a checkbox into its frame**: the row in its background colour, the box with a border and a body, the
// bevel of a thing a finger can press —swapped and darker while it is held—, the tick when it is checked, and
// the ring of the focus. The tick is `mark`'s level and the app chooses it, because on a dark checkbox a black
// tick is a page with nothing on it (docs/30, docs/90).
bool checkbox_draw(int16_t slot, uint8_t background, uint8_t border, uint8_t mark, bool checked,
                   bool pressed, bool focused);

// ---- a month: seven columns and six weeks, and the days in their place (docs/30, docs/90) ----
//
// **A calendar is a grid that never changes size**: seven columns, the weekdays on top and **six weeks always**, so
// the row under it does not move in February. Which day of the week the first of the month falls on comes from
// `kernel/calendar.h`, which is the one copy of the leap year rule in the whole system. `selected` is inverted and
// `today` wears a frame: two signals, because on this glass there is no colour for two things (docs/90).
//
// `weekdays` is an array of seven short names from the app, because the names of the days are a language and a
// language is not the kernel's.
bool month_draw(int16_t slot, uint16_t year, uint8_t month, uint8_t selected, uint8_t today,
                const char* const* weekdays, uint8_t text, uint8_t dim, uint8_t background, uint8_t scale,
                bool focused);

// **Which day of the month a point of the grid is on**, or zero: the same cells the paint uses, so a finger and a
// drawing cannot disagree (docs/90).
uint8_t month_hit(int16_t slot, uint16_t year, uint8_t month, uint16_t x, uint16_t y);

// ---- a sparkline and a pie: the shape of some numbers (docs/30, docs/90) ----
//
// **A sparkline is the shape of the last samples and not their value**: one bar per sample, oldest on the left,
// **the height proportional to the biggest one of the window** —a fixed scale would show the numbers, and what
// these charts are for is the shape: up, down, flat— and two series when the second is a part of the same total,
// the bottom one solid and the top one an outline (docs/90).
const uint8_t kMaxSparkSamples = 24;
bool sparkline_draw(int16_t slot, const uint16_t* samples, const uint16_t* top, uint8_t count, uint8_t color,
                    uint8_t background);

// **A pie is four slices at most and its tones are the control's**: the app says the darkest one and the control
// walks four steps of four from it, so the legend an app draws beside it can say the same numbers and not almost
// the same (docs/30, docs/90). `slices` are the values and the angles come from their sum.
const uint8_t kMaxPieSlices = 4;
bool pie_draw(int16_t slot, const uint16_t* slices, uint8_t count, uint8_t color, uint8_t background);

// ---- a dialog: a card over the page, words and two doors (docs/30, docs/90) ----
//
// **A dialog is a card and two buttons**, and the card is opaque: the page under it stays where it is and the card
// is what a person reads. The words are a title, a message that **wraps** —a message cut with "..." is a message
// half read— and a note under the message in the dim colour. `emphasis` is the door that is inverted (0 none, 1
// the cancel, 2 the accept) and `choice` is the one the dial is on, which wears the focus; on this glass there is
// no colour to say "this is the one", so the two signals are an inverted box and a ring (docs/90).
bool dialog_draw(int16_t slot, const char* title, const char* text, const char* note, const char* accept,
                 const char* cancel, uint8_t emphasis, uint8_t choice, uint8_t text_level, uint8_t dim,
                 uint8_t background, uint8_t scale, bool focused);

// **Which door of a dialog a point of its own frame is on**: 0 for the card, 1 for the left and 2 for the right.
// The geometry is one function, so a finger and the paint cannot disagree (docs/90).
uint8_t dialog_hit(int16_t slot, uint16_t x, uint16_t y);

// ---- a menu: a card of options, and the card is the system's (docs/30, docs/90) ----
//
// **A menu is a card with one option per row**, and it is the control that appears over the page when a person asks
// what can be done with a thing. Its options are **words and not icons** —a word says what it does and does not have
// to be guessed— and it carries no "close" row: the finger outside the card and the back key are already the way out,
// and a row of "Close" in a menu of three is a fourth option (docs/90).
//
// **An option the app cannot do now is taken off the card and not shown dead**: a card is the list of what can be
// done now, and a dead row in a card of three is a row a person tries. The glass can draw a lighter row —the level
// `dim` exists for it and `disabled` is the state of a control since 0.100.0—, and a menu that changes shape is
// honest in a way a menu of dead rows is not; the bar of a menu keeps its entries and greys them (docs/30, docs/92). `primary` is the option of the moment and it is the one
// that is inverted; `tag` is a short word in front of the label —"sys" for an app of the system, "lua" for a
// script— and it is empty when the app has nothing to say there (docs/30, docs/90).
const uint8_t kMaxMenuOptions = 12;

struct MenuOption {
  const char* label;
  const char* tag;
  bool hidden;
  bool primary;
};

// **The card of a menu**: opaque, with the weight of a card, the title centred at the top and one box per option.
// The words fill a column before the next one —a list of words is read downwards— and **a row is never under
// fifty-two pixels**, which is the least this system allows for anything a fingertip presses, while the rows grow
// into the room that is left: three options in a tall card are three tall doors. `index` is the option the dial is
// on, from 1, and the ring is drawn there, inside the box and in the tone that contrasts with it (docs/90).
bool menu_draw(int16_t slot, const char* title, const MenuOption* items, uint8_t count, uint8_t index,
               uint8_t color, uint8_t dim, uint8_t background, uint8_t scale, bool focused);

// **Which option a point of the card is on**, from 1, or 0 for the card itself and the gap between two boxes. The
// geometry is one function, so the box that is painted is the box that answers the finger (docs/90).
uint8_t menu_hit(int16_t slot, const char* title, const MenuOption* items, uint8_t count, uint8_t scale,
                 uint16_t x, uint16_t y);

// ---- a bar of menus: the titles and the one that is unfolded (docs/30, docs/90) ----
//
// **A bar is a row of words with a line under it and one menu that hangs from the word that was touched.** The bar is
// five words at most —540 px of paper shared between five leaves 108 px for each, which is room for the longest word
// a bar says— and it is **one level deep**: an entry carries no submenu, because where a submenu opens is a drawing
// decision this system has not taken (the old one made the same choice) (docs/90).
//
// **An entry that is a line and not a word is a separator**: it has its own row, the finger goes past it and the dial
// does not land on it. **An entry that cannot be used now is drawn in the dim tone and does not fire**: the ink is the
// only level a control may spend on "no", and a word that looks like the others and does nothing is worse than a word
// that says it cannot (docs/90). `checkable` is a small box in front of the words and `checked` the tick inside it,
// which is what says "this one is on" —and the empty box is what says "this one can be turned on"—.
const uint8_t kMaxMenuBarTitles = 5;
const uint8_t kMaxMenuBarEntries = 16;

struct MenuBarEntry {
  const char* label;
  bool separator;
  bool checked;
  // **An entry that is a setting carries its box even when it is off**: an empty square is a switch a finger can turn
  // and no square at all is a command, so the two kinds of entry are told apart without a word (docs/90).
  bool checkable;
  bool enabled;
};

struct MenuBarTitle {
  const char* label;
  const MenuBarEntry* entries;
  uint8_t count;
};

// **The row and the menu under it**: the open title is inverted, the menu is a card with the weight of a card, and the
// entry the dial is on wears the ring. `open` is the title whose menu is unfolded, from 1, or 0. `bar_height` is the
// height of the row —never under forty-four pixels, because a word is a thing a finger presses— and the rest of the
// frame is the room the open menu has, which is why a bar is given the height of the page and not the height of a row
// (docs/90).
bool menu_bar_draw(int16_t slot, const MenuBarTitle* titles, uint8_t count, uint8_t open, uint8_t index,
                   uint16_t bar_height, uint8_t color, uint8_t dim, uint8_t background, uint8_t scale,
                   bool focused);

// **Which title of the row a point is on**, from 1, or 0; and **which entry of the open menu**, from 1, or 0 for the
// card, a separator and the space between two rows. One geometry, so the box that is painted is the box that answers
// (docs/90).
uint8_t menu_bar_title_at(int16_t slot, const MenuBarTitle* titles, uint8_t count, uint16_t bar_height,
                          uint8_t scale, uint16_t x, uint16_t y);
uint8_t menu_bar_entry_at(int16_t slot, const MenuBarTitle* titles, uint8_t count, uint8_t open,
                          uint16_t bar_height, uint8_t scale, uint16_t x, uint16_t y);

// ---- a progress bar: how much of something is done (docs/30, docs/90) ----
//
// **A bar is not a control a finger lands on**: no focus, no ring, and a frame of one pixel, because a bar
// with the weight of a button reads as something to press. `value` is 0 to 100 and **a negative value is
// "nothing to say yet"**: the track is drawn and nothing is filled, which is what a system that does not
// know the answer shows (docs/90).
bool progress_draw(int16_t slot, uint8_t background, uint8_t border, uint8_t track, uint8_t fill,
                   int16_t value);

// ---- a slider: a track, a knob and the number (docs/30, docs/90) ----
//
// **The number is drawn by the control and the moving of it is the system's**: the arrows of the dial change
// `value` by `step` while the slider has the focus, and the app is told with `on_change`. The value is text
// with the face the control was given, `scale` times its size, and `suffix` is what comes after it —a `%`, a
// ` C`—, because a number without its unit is a number that means something else (docs/30).
bool slider_draw(int16_t slot, uint8_t background, uint8_t border, uint8_t track, uint8_t fill,
                 uint8_t text, int16_t value, int16_t minimum, int16_t maximum, const char* suffix,
                 uint8_t scale, bool pressed, bool focused);

// ---- a row of a list: one line and the two columns of it (docs/30, docs/90) ----
//
// **A title on the left, a detail on the right and a subtitle under it**, each one cut with `"..."` at its
// own column, and the line under the row when `separator` asks for it. A **marked** row is the chosen one and
// it is **inverted** —the words in the paper colour and the row in the ink one—, which is what a person can
// read at once on this glass (docs/90).
bool row_draw(int16_t slot, const char* title, const char* detail, const char* subtitle, uint8_t text,
              uint8_t dim, uint8_t background, uint8_t scale, bool marked, bool separator);

// ---- an entry of a list: a row the app builds (docs/30, docs/90) ----
//
// **A list of data is the shortcut and a list of entries is the full thing** (0.104.0). An entry is a control: it
// draws the paper of its row —ink when it is the marked one or the one with the ring— and the line of the separator,
// and **the app puts inside it whatever it wants** (a label, a row of two cells, a box with a picture and a name). It
// is what makes a list of things that are not a title and a detail (docs/30, docs/90).
bool entry_draw(int16_t slot, uint8_t background, uint8_t border, bool separator, bool marked,
                bool focused);

// ---- a panel: the room that arranges what it holds (docs/30, docs/90) ----
//
// **A page is a declaration and not a list of places**: a panel takes the controls added to it and gives each
// one its frame —a column with the words one under the other, a row with them side by side, or everything on
// top of everything— so an app that adds a line does not have to move the five under it. It is a control
// without a buffer: it holds children and paints nothing, so it costs no pixels (docs/90).
enum class PanelFlow : uint8_t { kVertical = 0, kHorizontal = 1, kOverlay = 2 };

// One child of a panel: the canvas, **how much of the free room it takes** and **what share of the box it claims**.
// Three ways to be laid out, and each cell says which one it wants:
//
//   - `share` 1 to 100: **that percentage of the inside of the box**, asked as a percentage and not as a weight, so a
//     cell that says 50 is half the box whether its brothers are fixed, weighted or not there at all (0.103.0);
//   - `weight` above 0: a part of the **free room** —what the shares and the cells of weight 0 left—, split in
//     proportion to the numbers the app gave (`1` and `1` are half each, `1` and `3` are a quarter and three);
//   - neither: the size the control was made with, stretched across the flow —a row of a column is as wide as the
//     column (docs/30, docs/90).
struct PanelChild {
  int16_t slot;
  uint8_t weight;
  uint8_t share;
};

// **Arranges the children of a panel** and returns how many of them moved: padding round the inside, `gap`
// between two, the children of weight 0 at the size they have, and the room that is left split by weight with
// the last one taking the remainder. Moving a child is a change of the glass and it is declared here, so the
// app does not have to know which of them landed somewhere new (docs/30, docs/90).
uint16_t panel_layout(int16_t slot, PanelFlow flow, uint16_t padding, uint16_t gap,
                      const PanelChild* children, uint16_t count);

// **A new size for a control without a buffer**: its frame is a rectangle and nothing else, so a panel that
// gives it a share of the room only writes the two numbers down. A canvas with a buffer of its own is refused,
// because its pixels were handed out with a size and a size that changes is a release and a new canvas
// (docs/30, docs/90).
bool canvas_resize(int16_t slot, uint16_t width, uint16_t height);

// ---- a strip of tabs: the chosen one has no line under it (docs/30, docs/90) ----
//
// **Equal columns across the frame**, one word in each, a line of two pixels under all of them but the chosen
// one —the missing line is what says "you are here" on a glass of two tones— and the words in the ink of the
// page. `index` is 1 to `count`, and 0 is "none chosen", which leaves the line whole (docs/90).
//
// **`lit` is the tab the dial is on**, 1 to `count`, and 0 when the ring is on another control: that tab is
// drawn inverted —the face takes the ink and the word takes the paper, and the bookmark and the line go the other
// way round— because a strip is one control with one stop per tab and the ring has to be somewhere a person can
// see (0.99.0, docs/92).
bool tabs_draw(int16_t slot, const char* const* labels, uint8_t count, uint8_t index, uint8_t lit,
               uint8_t text, uint8_t background, uint8_t scale);

// ---- a list: rows on pages and never a scroll (docs/30, docs/90) ----
//
// **A list is the rows of one page and the band that says which page it is.** On this glass a scroll is a
// hundred small paints and a page is one, so a list pages: the arrows of the dial turn it while it has the
// focus, and the app hears it at `on_page(self, page)` (docs/90).
//
// **How many rows fit and how many pages there are is one arithmetic and not two**: a list that says "2 / 3"
// and shows a row of the fourth page is a list that lies, so the app asks the same function the drawing asks
// (docs/90).
struct ListPage {
  uint8_t rows;   // how many rows a page holds
  uint8_t pages;  // how many pages the items make
};
ListPage list_page_of(uint16_t height, uint16_t row_height, uint16_t items);

// **The rows of one page**, with the footer that says which page it is and a chevron on the side that has
// somewhere to go. `marked` is the row of this page that is the chosen one, from 1, or 0 for none. The words
// come one page at a time, so a list of a thousand rows costs what one page costs (docs/30, docs/90).
bool list_draw(int16_t slot, uint8_t count, uint8_t page, uint8_t pages, uint8_t marked,
               const char* const* titles, const char* const* details, const char* const* subtitles,
               uint16_t row_height, uint8_t text, uint8_t dim, uint8_t background, uint8_t scale,
               bool separator, bool focused);

// ---- a text box: a line of letters, the window of it, and the caret (docs/30, docs/90) ----
//
// **The caret is the important half of a text box**: it is a block as wide as the letter under it, it is drawn
// in the ink and the letter under it is drawn in the paper —so the block shows the letter it is on and not a
// black square—, and the text is a **window** that drops leading letters until the caret is on the glass, or a
// person typing at the end of a long word types into a place nobody can see (docs/90).
//
// `cursor` is how many bytes of the text are before the caret; `caret` says whether the block is drawn, because
// the blinking is the app's clock and not the control's; `mask` draws a dot for every letter, which is what a
// password is (docs/30).
bool textbox_draw(int16_t slot, const char* text, uint16_t cursor, bool caret, bool mask, uint8_t color,
                  uint8_t background, uint8_t border, uint8_t scale);

// How wide the inside of a text box is with that scale, which is what an app needs to place a hint of its own in
// the empty box (docs/90).
uint16_t textbox_inside(int16_t slot, uint8_t scale);

// ---- a box of several lines: the same box, with room to fall (docs/30, docs/90) ----
//
// **The sibling of `textbox` and the same trade with the keyboard**: the same frame that says "something can be
// written here", the same block cursor, the same byte offset of the cursor. What changes is what the box does with a
// text that does not fit: **it shows the rows that fit and the window moves so that the cursor is always inside**,
// which is the one thing a field of several lines has to do or a person types into a place nobody can see (docs/90).
//
// **`wrap` is a way of painting and not a change of the text**: with it off a row is a line and a line longer than the
// box is cut at the border; with it on a row is broken at the last space that fits. No byte is written into the text
// either way, and the two decisions —where a row ends and where the cursor is— are made by one function so the caret
// of the paint and the caret of the finger cannot disagree (docs/90).
//
// **The window belongs to the cursor and, if the app names another one, to the app** (0.88.0): `top` is the first row
// that is seen —`-1` means "whichever the caret asks for", which is what a box nobody has moved does—, and when the
// text does not fit the right side of the box is a **bar**: the track says how much text there is and the thumb where a
// person is. The bar moves with the finger the app has registered (`on_swipe`) and the thumb is worked out again from
// the window, so the paint keeps no state of the gesture (docs/30, docs/90).
bool textbox_multiline_draw(int16_t slot, const char* text, uint16_t cursor, bool caret, bool wrap, uint8_t color,
                            uint8_t background, uint8_t border, uint8_t scale, int16_t top = -1);

// **Where a point of the box puts the cursor**: the row from the y and the letter from the x, both measured with the
// font, which is the only thing that knows how wide a letter comes out. A byte offset of the text, for a finger that
// went past the end of a row (docs/90).
uint16_t textbox_multiline_hit(int16_t slot, const char* text, uint16_t cursor, bool wrap, uint8_t scale,
                               uint16_t x, uint16_t y, int16_t top = -1);

// How many rows the box shows with that scale, which is what an app needs to know whether its text fits and how much
// of the page the keyboard leaves it (docs/90).
uint8_t textbox_multiline_rows(int16_t slot, uint8_t scale);

// **How many rows the whole text has** and **how many pixels one row is**: the first is what an app says in its status
// line ("row 12 of 40") and the second is what a finger has to travel to move the window one row (0.88.0,
// docs/30, docs/90).
uint16_t textbox_multiline_total(int16_t slot, const char* text, bool wrap, uint8_t scale);
uint8_t textbox_multiline_line(int16_t slot, uint8_t scale);

// **Where the window is left when a finger moves it**: `rows` rows added to the one that is seen now —the cursor's
// when `top` is `-1`—, clamped to `[0, total - fit]`. The app writes it in `top` and the system writes it when the
// gesture is its own (0.88.0, docs/90).
uint16_t textbox_multiline_scroll(int16_t slot, const char* text, uint16_t cursor, bool wrap, uint8_t scale,
                                  int16_t top, int16_t rows);

// ---- the keys of the board inside a box of text (0.116.0) ----
//
// **A box a person has entered reads the two keys of the dial as the caret**: the call that moves it is here and not
// in the app for the same reason the flip of a checkbox is —the arithmetic of a row belongs to the font and the window,
// and an app that kept its own copy would draw a block somewhere the paint does not— (docs/30, docs/90).
//
// `textbox_caret` is the box of one line: **a letter is a code point and not a byte** —the accent of "el meu fitxer"
// takes two— so a step that landed between them would be a caret inside a letter. The ends are the ends and the
// answer is always a cursor the box can honour.
uint16_t textbox_caret(int16_t slot, const char* text, uint16_t cursor, int16_t steps);
// The box of several lines: **`rows` rows up or down**, counting the rows the glass shows —wrapped or not— and keeping
// the column as near as the row allows, which is what a person expects of a key that goes down a line. At the first
// and the last row the caret stays where it is.
uint16_t textbox_multiline_row(int16_t slot, const char* text, uint16_t cursor, bool wrap, uint8_t scale,
                               int16_t top, int16_t rows);

// ---- a selector: a field that is chosen and not typed (docs/30, docs/90) ----
//
// **A value of a known set does not want a keyboard, it wants a list**, and the list is what a finger is good at. The
// field shows the question on the left in the dim tone at the small size and the value on the right in the ink, and it
// carries a column with a triangle on its right: **that column is the door**. Typing a whole date and a time on this
// glass is brutal —the keyboard takes the view down to 581 pixels, the digits are small and a wrong digit is a value
// that looks right— and that is the reason this control exists (docs/30, docs/90).
//
// **An open field is paper**: the only thing that turns to ink while the list is over the app is the column of the
// triangle, because a field that goes black from end to end reads as the whole page going dark (the owner said so of the
// old one, 17/10). And **the empty value is a state of its own**: `value` is what the app says "nothing chosen" looks
// like in its own language, and it is drawn exactly where a value would be.
bool selector_draw(int16_t slot, const char* label, const char* value, bool choosing, uint8_t color, uint8_t dim,
                   uint8_t background, uint8_t border, uint8_t scale, bool focused);

// ---- a band of notifications: the cards and how they are painted (docs/30, docs/90) ----
//
// **A band from edge to edge with one card per notice**, stacked upwards: **the newest is the one at the bottom**, which is
// the one nearest the finger and the one that goes away first, and a card is a title, a text and a cross to dismiss it
// (docs/90). Three cards at most on the glass: what does not fit is not lost, and the band **says how many are waiting**
// with a `+N` over them, because a notice that disappears without saying so is a notice somebody has lost.
//
// **The band is the room of the control and it paints only the cards it holds**: the rest of the frame is the page's, so a
// band can be given the height of the whole page and it does not erase what the app drew (docs/90).
const uint8_t kMaxNotifications = 3;

struct Notification {
  const char* title;
  const char* text;
};

// `items` are the ones to draw, oldest first and **the newest last**, and `total` is how many the app really has: the
// difference is what the `+N` says.
bool notification_bar_draw(int16_t slot, const Notification* items, uint8_t shown, uint16_t total, uint8_t color,
                           uint8_t dim, uint8_t background, uint8_t border, uint8_t scale, bool focused);

// **Which card a point is on, counted from the bottom** (1 is the newest), and **whether it is on the cross of that card**:
// two questions because a finger on the cross dismisses and a finger on the card opens it. Zero is no card at all, which is
// the space between two of them and the line that says how many are waiting (docs/90). `total` is the same number the paint
// was given, because the `+N` line takes room from the cards and a finger has to land on a card that is there.
uint8_t notification_bar_card_at(int16_t slot, uint8_t shown, uint16_t total, uint8_t scale, uint16_t x, uint16_t y);
bool notification_bar_close_at(int16_t slot, uint8_t shown, uint16_t total, uint8_t scale, uint16_t x, uint16_t y);

// ---- a grid of cells: an icon with its name under it, on pages (docs/30, docs/90) ----
//
// **A menu of things is picked with the eye before it is read**: the mark in the box is what is recognised at a glance and
// the name under it is what confirms it. The grid is a list of cells —the same paging, the same footer that says which page
// it is— and what it adds is the cell (docs/30, docs/90).
//
// **The icon is a box with a short mark in it**, drawn by the system at the size the cell can hold: this kernel has no
// bitmap of its own and an app that wants a real picture has `ui.image` for a canvas of its own. `marked` is the cell that is
// chosen and it is inverted; the cell the dial is on wears the ring. **The page follows the cursor**, so a grid never sits on
// a page where the cell a person is on is not.
const uint8_t kMaxGridItems = 48;
const int32_t kGridLeastCell = 96;

struct GridItem {
  const char* label;
  const char* mark;
};

// **`cells_of_the_kernel` false is a grid whose cells are controls the app built** (0.105.0): this control paints the
// paper of its page and the footer that says which page it is, and the cells paint themselves (docs/30, docs/90).
bool icon_grid_draw(int16_t slot, const GridItem* items, uint8_t count, uint8_t index, uint8_t marked,
                    uint8_t columns, uint8_t color, uint8_t dim, uint8_t background, uint8_t scale,
                    bool focused, bool cells_of_the_kernel);

// **Where a cell is**, in the frame of the grid, or false when that cell is not on the page the grid is on: the bridge
// asks this to put the control of the app where the kernel would have drawn the cell, and the answer is the one the
// paint and the finger use (0.105.0, docs/90).
bool icon_grid_cell(int16_t slot, uint8_t count, uint8_t index, uint8_t columns, uint8_t scale,
                    uint16_t which, uint16_t& x, uint16_t& y, uint16_t& w, uint16_t& h);

// **Which cell a point of the frame is on**, from 1, or 0 for the gaps, the footer and a place where there is no cell. The
// same geometry the paint uses, page included (docs/90).
uint8_t icon_grid_hit(int16_t slot, const GridItem* items, uint8_t count, uint8_t index, uint8_t columns,
                      uint8_t scale, uint16_t x, uint16_t y);

// ---- the keyboard: the system's, not a control (docs/30, docs/90) ----
//
// **A keyboard is chrome**: it is a band at the bottom of the view that the system paints over the page and that
// takes the finger and the dial before the page's controls do. An app does not draw it and does not handle a
// key: it says which text box it types into, asks how much of the page the band takes, and moves its own layout
// into what is left. What crosses back is one number per key —the letter, or 8 for the delete and 13 for the
// enter— and the box is edited by the same layer that owns every other control's state (docs/90).
const uint8_t kKeyboardAlpha = 0;
const uint8_t kKeyboardNumeric = 1;

bool keyboard_show(uint8_t kind);
bool keyboard_hide();
bool keyboard_visible();
// The band the keyboard takes at the bottom of the view, zero when it is not on the glass: an app that wants its
// page above it decides its own layout in `height - keyboard_height()` (docs/90).
uint16_t keyboard_height();
// The finger, in the view's coordinates. True when the keyboard took it and the page must not see it.
bool keyboard_touch(uint16_t x, uint16_t y, bool down);
// The dial: the arrows walk the keys and the centre presses the one they are on. True when the keyboard took the
// key (docs/90).
bool keyboard_key(const char* kind, uint32_t key);

// **The keyboards the system carries** (0.93.0): the band's common behaviour is one parent class and each keyboard is
// one child that says its name and its rows. The settings choose one by name; `keyboard_count` and `keyboard_at` are
// what a `cfg keyboard` listing reads (docs/30, docs/70, docs/90).
uint8_t keyboard_count();
const char* keyboard_at(uint8_t index);
const char* keyboard_current();
bool keyboard_select(const char* id);

// ---- the dots of a page: where this page is among the others (docs/30, docs/90) ----
//
// **`pages` balls in a row and `current` the bigger one**, threaded on a rod: the ball of the page that is open is a
// solid disc and the others are **rings with paper inside**, so the strip is two tones and no more —it is drawn and
// erased with the binary waveform, and a grey one smeared on this glass—. It takes no focus and it draws nothing at all
// when there is only one page: one dot says nothing, and it costs a pass of the panel to say it (0.94.1, docs/90).
//
// **When the pages do not fit**, the strip shows as many balls as its box holds and the window of them that contains the
// open page, so the counter is inside the box and the page a person is on is always one of the balls. The whole strip
// owes **one** damaged region and not one per ball (docs/30, docs/90).
bool dots_draw(int16_t slot, uint8_t pages, uint8_t current, uint8_t level, uint8_t background);

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
bool canvas_set_font(int16_t slot, int8_t handle);
bool app_set_font(int8_t handle);
// How wide a text is with that face, in pixels, which is what centring it needs (docs/30).
uint16_t font_text_width(int8_t handle, const char* text, uint8_t scale);
// **How tall a line is with that face, in pixels** (0.121.0): the other half of `font_text_width`, and what
// centring a text **vertically** needs — `ui.text` and `cv:text` put the top of the line at `y`, so a glyph in
// the middle of a box is `y + (box_h - line_height) / 2`. It is the height of the face and not of the text: a
// line is one height whatever it says (docs/30).
uint16_t font_line_height(int8_t handle, uint8_t scale);

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

// Whether the paint that was asked for has finished moving ink. **And the moment a paint the panel refused is asked
// for again** (0.118.2): the glass says no to a page that arrives while another one is moving its ink, and that page is
// kept and painted here, when the panel can take it (docs/30, docs/50).
bool idle();
// **Whether the glass is owed a page** (0.118.2): the kernel asks this every turn and only pays for `idle()` when it is
// true, so a turn that owes nothing costs nothing (docs/50, docs/92 §33).
bool owes_paint();

// The title of this app in the bar of the system. The bar is the system's and the title on it is the
// app's: that is the one thing about the chrome an app may say (docs/30).
bool title(const char* text);

// Where the view sits on the glass and how big it is. **The system's, not an app's**: it is what a
// second view, a strip of the system or a rotated glass will move (docs/30).
bool place(uint16_t x, uint16_t y);
bool resize(uint16_t width, uint16_t height);

// ---- the whole glass for an app that asks for it (0.136.0, docs/46 D1) ----
//
// An app may ask the system to keep **the whole glass**: the strip of the bar goes away, the view is made the size of
// the panel and placed at its corner, and the app is told with `on_resize`, which is the hook a turn already gives it.
// Asking for the glass back —or being given it back— makes the view the one of every app again and the bar returns.
//
// **The way out is the system's and not the app's**: three taps on the glass are heard by the kernel, taken out of the
// queue before the app reads it, and they give the glass back, so a page that covers everything cannot take away the
// way back to the bar. And **an app that ends gives back what it took** —by its own will or by an error—, because the
// view of the next app and the bar of the system are not the app's to keep.
bool full_screen(bool on);
bool is_full_screen();
// **The three taps of the system**, asked in the turn of the kernel right after `touch::poll` and before the app runs:
// any later and the app would read the gesture first (docs/30). True when the glass was given back.
bool poll_full_screen();
// **The view of every app again**, said to nobody: the app that took the whole glass is gone (0.136.0).
void give_back_the_glass();

// **Turns the page**: 0, 90, 180 or 270 degrees, and the whole system goes with it —the glass, the bar and
// the fingers, which read it from the same place— because the frame of an app is made again at the new
// size and the app is told with `on_resize` (docs/30, docs/50).
bool set_rotation(uint16_t degrees);
uint16_t rotation();

}  // namespace ui
}  // namespace kernel

#endif  // LUNOK_KERNEL_UI_H
