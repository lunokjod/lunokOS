// **The glass of the emulator, in a window** (docs/85).
//
// The emulator's glass is a buffer of four bits per pixel that remembers the rectangle of every paint.
// Without this it is a buffer nobody can look at; with `--gui` the same buffer is copied into a window
// whenever the panel is painted, so what an app draws on the PC is seen instead of guessed.
//
// The backend is SDL2 when the build has it and X11 when it only has Xlib; the pure geometry and colour
// functions live in gui_common.cpp and are the part a test can prove without a screen (docs/85).
//
// It is a window and not a picture: no ink, no ghost, no flicker, and the quality of a paint is not
// drawn any differently because the emulator has no waveform. The window is a way to *look*, and every
// number of how it looks is still the board's (docs/50, docs/85).
#ifndef LUNOK_EMU_GUI_H
#define LUNOK_EMU_GUI_H

#include <stdint.h>

namespace emu {

// True when this emulator was built with a window in it. A build without a window backend says so
// instead of failing at the first `--gui` (docs/85).
bool gui_available();

// Opens a window of that size. False when there is no display to open it on, which is not an error of
// the run: the emulator carries on without it.
// (The signature with the keys is at the end of this header, next to the geometry of the strip.)

// **The glass in the colours of the display, which is the part that can be proved.** The buffer is four
// bits per pixel with the left pixel in the high nibble, and the window wants one 32-bit pixel each:
// `gui_expand` is that translation and nothing else, so a test can check it without a screen (docs/50,
// docs/85).
uint32_t gui_level_colour(uint8_t level);
// **`scale` is how many glass pixels one pixel of the window stands for**: 1 is the page as it is, and 2
// is the page at half, which is what a screen smaller than the page gets so that the whole of it fits
// instead of a corner of it (docs/85).
void gui_expand(const uint8_t* packed, uint16_t width, uint16_t height, uint32_t* out, uint8_t scale);

// **Where a pixel of the window lands on the page**, which is the way back from `gui_expand` and the
// only part of the mouse that can be proved without a screen: a window pixel stands for a block of
// `scale` × `scale` page pixels and the finger lands in the middle of it, and a point outside the page is
// pinned to its edge instead of being a finger on no glass (docs/85).
void gui_page_point(int window_x, int window_y, uint8_t scale, uint16_t page_width, uint16_t page_height,
                    uint16_t& out_x, uint16_t& out_y);

// **The glass, as it is now.** The buffer is the packed four-bit image the surface keeps, so the window
// costs one conversion per paint and not one per turn (docs/85).
void gui_show(const uint8_t* packed, uint16_t width, uint16_t height);

// What the window has to say, without waiting for it: an expose means draw again, and the corner that
// closes it means the run is over.
void gui_pump();

// True once the person closed the window: the run ends as if three ESC had been typed.
bool gui_close_requested();

// **How many page pixels one pixel of the window stands for**: 1 when the page fitted the screen, 2 or
// more when it had to be cut down to fit, so the run can say what a person is looking at (docs/85).
uint8_t gui_scale();

// **Which side of the page the keys go on, and it is the descriptor of the input that says it**
// (`IInput::face`): a dial on the left or the right face is a column down that side, and a key on the top or
// the front is a row under the page. The emulator places the buttons where the board says they are and does not
// pick a side of its own (docs/50, docs/85).
enum class KeySide : uint8_t { kLeft = 0, kRight = 1, kBottom = 2 };

// **The scale of the window, chosen once**: the one the run asked for (`--gui-scale`, 1 to 4) or, when it
// asked for none —zero—, the smallest that fits a screen of that size with the strip of the keys. It is 1
// when the page fits, 2 when it has to be halved, and the window and the mouse both use this one number.
// The page of the model is 540x960 and does not change; this is only how big it is drawn (docs/85).
uint8_t gui_fit_scale(uint16_t page_width, uint16_t page_height, uint8_t keys, KeySide side,
                      int screen_width, int screen_height, uint8_t requested);

// The buttons are measured in window pixels and not in page pixels: how big a page is scaled to is the
// page's business, and a button that shrinks with it is a button a person cannot hit. Both backends draw
// the same rectangle and the mouse reads the same one (docs/85).
//
// **Small and square** (0.85.1): the mouse of an emulator is a pointer of one pixel and a finger of eighty by fifty was a
// key for a hand that is not there. Forty is enough for the aim of a mouse and it leaves the page the room it had.
const uint16_t kKeyMargin = 6;
const uint16_t kKeyButtonWidth = 40;
const uint16_t kKeyButtonHeight = 40;
const uint16_t kKeyGap = 6;
const uint16_t kKeyStripWidth = static_cast<uint16_t>(kKeyButtonWidth + 2 * kKeyMargin);
const uint16_t kKeyRowHeight = static_cast<uint16_t>(kKeyButtonHeight + 2 * kKeyMargin);

// **The sign on a key** (0.85.1): the dial of the Paper is a wheel and its three spots are up, down and the middle, so
// the buttons draw an arrow, an arrow and a full circle instead of two words and a third. The shapes are here, as runs
// of one pixel, and not in a backend: the two windows draw the same and a test can say where the ink of a key is
// without a screen (docs/85).
const uint8_t kKeyIconRuns = 40;

struct KeyRun {
  uint16_t x;
  uint16_t y;
  uint16_t w;
  uint16_t h;
};

// How many runs the key has: zero for a board of one button, which keeps its word.
uint8_t gui_key_icon(uint8_t count, int key, uint16_t width, uint16_t height, KeyRun* out, uint8_t capacity);

// **One window with the glass and the keys in it** (0.34.0): the page is not covered by anything and the
// buttons are where the board has them. Two windows were two things to place and to look at, and the window
// manager put them where it liked.
//
// `keys` is how many buttons there are —zero is a board with no key— and a click on one is a press and a
// release of that key through the same `press_key` a test uses (docs/50, docs/85).
//
// `scale` is `--gui-scale`: 1 to 4 draws the page at that many page pixels per window pixel, 2 being the
// page at half, even when the screen would hold it whole; zero is the smallest that fits, which is what a
// window nobody sized wants (docs/85).
bool gui_open(uint16_t width, uint16_t height, uint8_t keys, KeySide side, uint8_t scale);

// **Which key a point of the window is on**, and which key a slot of the column or the row is: the dial goes
// up, centre and down from the top, a board of one key has the middle of it, and `-1` is no key at all —
// the gaps between the buttons, the margins of the strip and everything on the page. Both are pure and have
// proof in `test_ui`, because a button that presses the wrong key is a bug nobody sees (docs/85).
int gui_key_at(int window_x, int window_y, uint8_t count, uint16_t window_width, uint16_t window_height,
               KeySide side);
int gui_key_of_slot(uint8_t count, uint8_t slot);
// The word on a key: one button is the whole board, and a dial is up, centre and down from the top.
const char* gui_key_label(uint8_t count, int key);
// The geometry that the drawing and the test agree on: what a column and a row take, where they start in a
// window of that size (both are centred, which is where the keys of the board are on its face).
uint16_t gui_key_strip_width();
uint16_t gui_key_row_height();
uint16_t gui_keys_height(uint8_t count);
uint16_t gui_keys_width(uint8_t count);
int gui_keys_top(uint16_t window_height, uint8_t count);
int gui_keys_left(uint16_t window_width, uint8_t count);

void gui_close();

}  // namespace emu

#endif  // LUNOK_EMU_GUI_H
