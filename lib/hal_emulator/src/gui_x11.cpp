#include "emu/gui.h"

#include <stdio.h>

// **X11 and nothing else.** The bench is a development tool of this machine, and a window needs a
// library: Xlib is the one that is on any desktop with a compiler and it costs one `-lX11`, where SDL
// would be a whole dependency of the emulator for a rectangle of pixels. The code below is behind a
// flag so a machine without X11 still builds the emulator and says that this bench has no window
// (docs/85).
#include <stdlib.h>
#include <string.h>

namespace emu {

// **An e-ink panel is not black and white**: its paper is warm —a little yellow, a little grey— and its
// darkest level is not the absence of light but the blackest the pigment gets. The window shows that and
// not a perfect monochrome, because a page read on a perfect monochrome is a page that looks better than
// the one the person will hold (docs/85). The four bits still mean the same sixteen levels; only their
// colour changes.
const uint8_t kInkBlackR = 0x1A;
const uint8_t kInkBlackG = 0x1B;
const uint8_t kInkBlackB = 0x18;
const uint8_t kPaperR = 0xEF;
const uint8_t kPaperG = 0xEA;
const uint8_t kPaperB = 0xDC;

uint8_t mix_channel(uint8_t dark, uint8_t light, uint8_t level) {
  // level 0 is the ink and 15 is the paper, and the steps in between are straight: the waveform of this
  // glass is not linear, but a window that draws a curve nobody measured would be inventing physics
  // (docs/85).
  const int32_t span = static_cast<int32_t>(light) - static_cast<int32_t>(dark);
  const int32_t value = static_cast<int32_t>(dark) + (span * static_cast<int32_t>(level & 0x0F) + 7) / 15;
  return static_cast<uint8_t>((value < 0) ? 0 : ((value > 255) ? 255 : value));
}

uint32_t gui_level_colour(uint8_t level) {
  const uint8_t r = mix_channel(kInkBlackR, kPaperR, level);
  const uint8_t g = mix_channel(kInkBlackG, kPaperG, level);
  const uint8_t b = mix_channel(kInkBlackB, kPaperB, level);
  return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
}

// The four bits of two neighbours become two pixels, **the left one in the high nibble**, which is the
// packing of the panel itself (docs/50).
void gui_expand(const uint8_t* packed, uint16_t width, uint16_t height, uint32_t* out, uint8_t scale) {
  if (packed == nullptr || out == nullptr || scale == 0) return;
  const uint16_t out_width = static_cast<uint16_t>((width + scale - 1) / scale);
  const uint16_t out_height = static_cast<uint16_t>((height + scale - 1) / scale);
  for (uint16_t oy = 0; oy < out_height; ++oy) {
    const uint16_t y = static_cast<uint16_t>(oy * scale);
    const uint8_t* row = packed + static_cast<size_t>(y) * (width / 2);
    uint32_t* line = out + static_cast<size_t>(oy) * out_width;
    for (uint16_t ox = 0; ox < out_width; ++ox) {
      const uint16_t x = static_cast<uint16_t>(ox * scale);
      if (x >= width) {
        // A glass of an odd width ends on a nibble nobody wrote: it is white, like the panel's memory.
        line[ox] = gui_level_colour(15);
        continue;
      }
      const uint8_t byte = row[x / 2];
      line[ox] = gui_level_colour(((x & 1) == 0) ? static_cast<uint8_t>(byte >> 4)
                                                 : static_cast<uint8_t>(byte & 0x0F));
    }
  }
}

// **The way back**: the pixel of the window to the pixel of the page under it, which is what makes the
// mouse a finger. It is outside the X11 flag because a test asks for it without a screen (docs/85).
void gui_page_point(int window_x, int window_y, uint8_t scale, uint16_t page_width, uint16_t page_height,
                    uint16_t& out_x, uint16_t& out_y) {
  const int step = (scale == 0) ? 1 : static_cast<int>(scale);
  int page_x = window_x * step + step / 2;
  int page_y = window_y * step + step / 2;
  if (page_x < 0) page_x = 0;
  if (page_y < 0) page_y = 0;
  if (page_x > static_cast<int>(page_width) - 1) page_x = static_cast<int>(page_width) - 1;
  if (page_y > static_cast<int>(page_height) - 1) page_y = static_cast<int>(page_height) - 1;
  out_x = static_cast<uint16_t>(page_x);
  out_y = static_cast<uint16_t>(page_y);
}

// **The geometry of the keys**, which is the one the descriptor of the input asks for: a dial on a side face
// is a **column** down that side —up, centre and down from the top, the way the wheel of the board is— and a
// key on the top or the front is a **row** under the page. The buttons are measured in window pixels and not
// in page pixels: how big a page is scaled to is the page's business, and a button that shrinks with it is a
// button a person cannot hit (docs/50, docs/85).
const uint16_t kKeyMargin = 8;
const uint16_t kKeyButtonWidth = 80;
const uint16_t kKeyButtonHeight = 54;
const uint16_t kKeyGap = 8;
// A column takes a strip as wide as a button and its two margins; a row takes one as tall.
const uint16_t kKeyStripWidth = static_cast<uint16_t>(kKeyButtonWidth + 2 * kKeyMargin);
const uint16_t kKeyRowHeight = static_cast<uint16_t>(kKeyButtonHeight + 2 * kKeyMargin);

uint16_t gui_key_strip_width() { return kKeyStripWidth; }
uint16_t gui_key_row_height() { return kKeyRowHeight; }

uint16_t gui_keys_height(uint8_t count) {
  if (count == 0) return 0;
  return static_cast<uint16_t>(count * kKeyButtonHeight + (count - 1) * kKeyGap);
}

uint16_t gui_keys_width(uint8_t count) {
  if (count == 0) return 0;
  return static_cast<uint16_t>(count * kKeyButtonWidth + (count - 1) * kKeyGap);
}

// The column is **centred** in the window, which is where the wheel of the board is on its face.
int gui_keys_top(uint16_t window_height, uint8_t count) {
  const int height = static_cast<int>(gui_keys_height(count));
  if (height == 0 || static_cast<int>(window_height) <= height) return 0;
  return (static_cast<int>(window_height) - height) / 2;
}

// And the row is centred too, under the page.
int gui_keys_left(uint16_t window_width, uint8_t count) {
  const int width = static_cast<int>(gui_keys_width(count));
  if (width == 0 || static_cast<int>(window_width) <= width) return 0;
  return (static_cast<int>(window_width) - width) / 2;
}

int gui_key_of_slot(uint8_t count, uint8_t slot) {
  if (count == 0 || slot >= count) return -1;
  // One key is the middle one, which is the only button an ATOM has and the centre of the Paper's dial.
  if (count == 1) return 0;
  // Two are the sides with no middle to be the middle of.
  if (count == 2) return (slot == 0) ? 1 : 2;
  // **The wheel**: up first, the centre in the middle and down last, which is the order the Paper's `Spot`
  // gives the three keys (docs/50).
  if (slot == 0) return 1;
  if (slot == 1) return 0;
  return 2;
}

int gui_key_at(int window_x, int window_y, uint8_t count, uint16_t window_width, uint16_t window_height,
               KeySide side) {
  if (count == 0) return -1;
  if (side == KeySide::kBottom) {
    // **A row under the page**: the gap between two buttons is horizontal here.
    const int top = static_cast<int>(window_height) - static_cast<int>(kKeyRowHeight - kKeyMargin);
    if (window_y < top || window_y >= top + static_cast<int>(kKeyButtonHeight)) return -1;
    const int from_the_left = window_x - gui_keys_left(window_width, count);
    if (from_the_left < 0) return -1;
    const int pitch = static_cast<int>(kKeyButtonWidth + kKeyGap);
    const int slot = from_the_left / pitch;
    if (slot >= static_cast<int>(count)) return -1;
    if (from_the_left - slot * pitch >= static_cast<int>(kKeyButtonWidth)) return -1;
    return gui_key_of_slot(count, static_cast<uint8_t>(slot));
  }
  // **A column down a side**: the gap is vertical, and which side is the descriptor's to say.
  const int left = (side == KeySide::kRight)
                       ? static_cast<int>(window_width) - static_cast<int>(kKeyStripWidth) +
                             static_cast<int>(kKeyMargin)
                       : static_cast<int>(kKeyMargin);
  if (window_x < left || window_x >= left + static_cast<int>(kKeyButtonWidth)) return -1;
  const int pitch = static_cast<int>(kKeyButtonHeight + kKeyGap);
  const int below_the_top = window_y - gui_keys_top(window_height, count);
  if (below_the_top < 0) return -1;
  const int slot = below_the_top / pitch;
  if (slot >= static_cast<int>(count)) return -1;
  // The space between two buttons is not a button: a click there presses nothing, the same as a finger on
  // the case (docs/85).
  if (below_the_top - slot * pitch >= static_cast<int>(kKeyButtonHeight)) return -1;
  return gui_key_of_slot(count, static_cast<uint8_t>(slot));
}

}  // namespace emu

#if defined(LUNOK_GUI_X11)

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "emu/gt911.h"
#include "emu/options.h"

namespace emu {
namespace {

Display* g_display = nullptr;
Window g_window = 0;
GC g_gc = nullptr;
XImage* g_image = nullptr;
uint32_t* g_pixels = nullptr;
bool g_close_requested = false;
Atom g_wm_delete = 0;
uint16_t g_width = 0;
uint16_t g_height = 0;
uint8_t g_scale = 1;
// The button of the window is held down, which is what tells a finger moving from a pointer passing by.
bool g_button_down = false;

// **The keys**, where the descriptor of the input says they are: a column down a side or a row under the
// page, one button per key, centred, and the one that is down filled in, so a click has an answer on the
// screen and not only in the log (docs/85). It is the same window as the glass, which is what makes a click
// here and a click on the page the same gesture with two meanings, told apart by where the pointer is.
uint8_t g_keys_count = 0;
KeySide g_side = KeySide::kLeft;
uint16_t g_win_w = 0;    // the window, in window pixels
uint16_t g_win_h = 0;
uint16_t g_page_w = 0;   // the page inside it, in window pixels
uint16_t g_page_h = 0;
uint16_t g_page_x = 0;   // where the page starts in it, which is what the keys leave
uint16_t g_page_y = 0;
XFontStruct* g_font = nullptr;
unsigned long g_black = 0;
unsigned long g_white = 0;
// **The case around the page is dark and so are the keys** (0.35.0): the page is the e-ink panel and it is
// warm and light, so anything that is not the page is drawn as the dark box it comes in —the strip of the keys
// and the rest of the window— and a button flashes to the colour of the paper when it is pressed, which is
// the only way to see that a click arrived (docs/85).
unsigned long g_case = 0;
unsigned long g_key_face = 0;
unsigned long g_key_edge = 0;
unsigned long g_key_text = 0;
unsigned long g_key_pressed = 0;
unsigned long g_key_pressed_text = 0;
// The key the pointer is holding down, or -1: a press and its release are two events of the same button
// and the release has to lift the one that was pressed even if the pointer moved off it (docs/85).
int g_key_held = -1;

// A colour by name, and the fallback when the colour map has nothing to say.
unsigned long colour_of(const char* name, unsigned long fallback) {
  XColor exact;
  XColor wanted;
  if (XAllocNamedColor(g_display, DefaultColormap(g_display, DefaultScreen(g_display)), name, &wanted,
                       &exact) == 0) {
    return fallback;
  }
  return wanted.pixel;
}

const char* label_of_key(uint8_t count, int key) {
  if (count == 1) return "Button";
  if (key == 1) return "Up";
  if (key == 2) return "Down";
  return "Centre";
}

void draw_keys() {
  if (g_display == nullptr || g_window == 0 || g_keys_count == 0) return;
  const bool row = (g_side == KeySide::kBottom);
  const int top = gui_keys_top(g_win_h, g_keys_count);
  const int left = gui_keys_left(g_win_w, g_keys_count);
  for (uint8_t slot = 0; slot < g_keys_count; ++slot) {
    const int key = gui_key_of_slot(g_keys_count, slot);
    // A column grows downwards and a row grows to the right; which of the two is the descriptor's to say.
    const int x = row ? left + slot * static_cast<int>(kKeyButtonWidth + kKeyGap)
                      : ((g_side == KeySide::kRight)
                             ? static_cast<int>(g_win_w) - static_cast<int>(kKeyStripWidth) +
                                   static_cast<int>(kKeyMargin)
                             : static_cast<int>(kKeyMargin));
    const int y = row ? static_cast<int>(g_win_h) - static_cast<int>(kKeyRowHeight) +
                            static_cast<int>(kKeyMargin)
                      : top + slot * static_cast<int>(kKeyButtonHeight + kKeyGap);
    const bool down = (key >= 0) && key_down(key);
    XSetForeground(g_display, g_gc, down ? g_key_pressed : g_key_face);
    XFillRectangle(g_display, g_window, g_gc, x, y, static_cast<unsigned>(kKeyButtonWidth),
                   static_cast<unsigned>(kKeyButtonHeight));
    XSetForeground(g_display, g_gc, down ? g_key_pressed : g_key_edge);
    XDrawRectangle(g_display, g_window, g_gc, x, y, static_cast<unsigned>(kKeyButtonWidth - 1),
                   static_cast<unsigned>(kKeyButtonHeight - 1));
    if (g_font != nullptr) {
      const char* label = label_of_key(g_keys_count, key);
      const int len = static_cast<int>(strlen(label));
      XSetFont(g_display, g_gc, g_font->fid);
      XSetForeground(g_display, g_gc, down ? g_key_pressed_text : g_key_text);
      const int text_x = x + static_cast<int>(kKeyButtonWidth) / 2 - XTextWidth(g_font, label, len) / 2;
      const int text_y = y + static_cast<int>(kKeyButtonHeight) / 2 + (g_font->ascent - g_font->descent) / 2;
      XDrawString(g_display, g_window, g_gc, text_x, text_y, label, len);
    }
  }
  XFlush(g_display);
}

// **A mouse on the window is a finger on the glass**: the glass of this bench is the panel of the model,
// and an app that draws with a finger has no way to be tried on a machine that has no touch screen. A
// press is the finger coming down, the move while the button is held is the finger moving, and letting go
// is the finger coming up — in the frame the chip counts in, which on this board is the page's own
// (docs/50, docs/85).
void put_the_finger(int window_x, int window_y, bool moving) {
  // `--no-touch`: the panel of the model is there and nobody reads it, so the mouse has nothing to say.
  if (!options().touch) return;
  Gt911Panel* panel = touch_panel();
  if (panel == nullptr) return;
  if (g_width != Gt911Panel::kFrameWidth || g_height != Gt911Panel::kFrameHeight) {
    // **The window and the chip do not count in the same frame**, which is a turned glass: saying it once
    // is better than a mouse that silently does nothing (docs/85).
    static bool said = false;
    if (!said) {
      said = true;
      fprintf(stderr,
              "gui: the window is %ux%u and the touch panel counts in %ux%u, so the mouse does not reach it\n",
              static_cast<unsigned>(g_width), static_cast<unsigned>(g_height),
              static_cast<unsigned>(Gt911Panel::kFrameWidth),
              static_cast<unsigned>(Gt911Panel::kFrameHeight));
    }
    return;
  }
  uint16_t x = 0;
  uint16_t y = 0;
  gui_page_point(window_x, window_y, g_scale, g_width, g_height, x, y);
  if (moving) {
    panel->finger_move(0, x, y);
  } else {
    panel->finger_down(0, x, y);
  }
}

void lift_the_finger() {
  if (!options().touch) return;
  Gt911Panel* panel = touch_panel();
  if (panel != nullptr) panel->finger_up(0);
}

void redraw() {
  if (g_display == nullptr || g_image == nullptr) return;
  // The page goes where the keys left it and the keys are left alone: a paint of the page never takes the
  // buttons with it (docs/85).
  XPutImage(g_display, g_window, g_gc, g_image, 0, 0, static_cast<int>(g_page_x), static_cast<int>(g_page_y),
            static_cast<unsigned>(g_page_w), static_cast<unsigned>(g_page_h));
  XFlush(g_display);
}

}  // namespace

bool gui_available() { return true; }

namespace {

// **One display for both windows**: a board with no glass has a button and no page, and its window still
// needs a connection to the server (docs/85).
bool ensure_display() {
  if (g_display != nullptr) return true;
  g_display = XOpenDisplay(nullptr);
  return g_display != nullptr;
}

}  // namespace

bool gui_open(uint16_t width, uint16_t height, uint8_t keys, KeySide side) {
  if (g_window != 0) return true;
  // **A page of nothing is a board with no glass**: its window is the keys and only them, which is the window
  // an ATOM gets (docs/85).
  if (width == 0 || height == 0) {
    width = 0;
    height = 0;
  }
  if (width == 0 && keys == 0) return false;
  if (!ensure_display()) return false;

  const int screen = DefaultScreen(g_display);
  g_width = width;
  g_height = height;
  g_black = BlackPixel(g_display, screen);
  g_white = WhitePixel(g_display, screen);
  // **The case of the board, in the dark colours it is in**: what is not the page is not white, and the keys
  // are grey on it so that the page is what the eye goes to (docs/85).
  g_case = colour_of("#1b1b1b", g_black);
  g_key_face = colour_of("#2f2f2f", g_black);
  g_key_edge = colour_of("#6e6e6e", g_white);
  g_key_text = colour_of("#e8e8e8", g_white);
  g_key_pressed = colour_of("#efeade", g_white);  // the paper of the panel, which is what a key looks like down
  g_key_pressed_text = colour_of("#1a1b18", g_black);
  // **The keys go where the descriptor of the input says they are**: a dial on a side face is a column down
  // that side and a key on the top or the front is a row under the page. The window is the page plus that
  // strip: one window is one thing to place and one thing to look at, and two windows were two (docs/85).
  g_keys_count = keys;
  g_side = side;
  const int strip_w = (keys == 0) ? 0
                                  : ((side == KeySide::kBottom) ? 0
                                                                : static_cast<int>(gui_key_strip_width()));
  const int strip_h = (keys == 0) ? 0
                                  : ((side == KeySide::kBottom) ? static_cast<int>(gui_key_row_height()) : 0);
  // **The whole thing has to fit on the screen.** A window taller than the display is a window the window
  // manager clips, and two thirds of a page that looks complete is worse than a page at half: the scale is
  // the smallest that fits, and the draw is every `g_scale` pixels (docs/85).
  const int screen_w = DisplayWidth(g_display, screen);
  const int screen_h = DisplayHeight(g_display, screen);
  g_scale = 1;
  while (g_scale < 4 && (strip_w + static_cast<int>(width) / g_scale > screen_w - 40 ||
                         strip_h + static_cast<int>(height) / g_scale > screen_h - 80)) {
    ++g_scale;
  }
  g_page_w = (width == 0) ? 0 : static_cast<uint16_t>((width + g_scale - 1) / g_scale);
  g_page_h = (height == 0) ? 0 : static_cast<uint16_t>((height + g_scale - 1) / g_scale);
  g_page_x = static_cast<uint16_t>((side == KeySide::kRight) ? 0 : strip_w);
  g_page_y = 0;
  g_win_w = static_cast<uint16_t>(strip_w + g_page_w);
  g_win_h = static_cast<uint16_t>(strip_h + g_page_h);
  // A row of keys under a page of nothing is still a window.
  if (g_win_w == 0) g_win_w = static_cast<uint16_t>(gui_keys_width(keys) + 2 * kKeyMargin);
  if (g_win_h == 0) g_win_h = static_cast<uint16_t>(gui_keys_height(keys) + 2 * kKeyMargin);
  g_window = XCreateSimpleWindow(g_display, RootWindow(g_display, screen), 0, 0, g_win_w, g_win_h, 0,
                                 g_black, g_case);
  if (g_window == 0) {
    XCloseDisplay(g_display);
    g_display = nullptr;
    return false;
  }
  XStoreName(g_display, g_window, "lunokOS");
  // **The pointer is an input of this window**: the glass has a finger on the board, and the mouse is how
  // a person has one here (docs/85).
  XSelectInput(g_display, g_window, ExposureMask | KeyPressMask | StructureNotifyMask | ButtonPressMask |
                                        ButtonReleaseMask | PointerMotionMask);
  // The corner that closes it. A window that cannot be closed from the window manager is a process
  // somebody has to kill (docs/85).
  g_wm_delete = XInternAtom(g_display, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(g_display, g_window, &g_wm_delete, 1);
  g_gc = XCreateGC(g_display, g_window, 0, nullptr);

  // **One pixel per glass pixel**, in the colour of the display: the image is built here and handed to
  // X as it is, so a paint costs one conversion of the rectangle and not a round trip per pixel. The image
  // is the page and nothing else: the strip of the keys is drawn with the window's own primitives and a
  // paint never has to redraw a button (docs/85).
  if (g_page_w > 0 && g_page_h > 0) {
    g_pixels = static_cast<uint32_t*>(calloc(static_cast<size_t>(g_page_w) * g_page_h, sizeof(uint32_t)));
    if (g_pixels == nullptr) {
      gui_close();
      return false;
    }
    g_image = XCreateImage(g_display, DefaultVisual(g_display, screen),
                           static_cast<unsigned>(DefaultDepth(g_display, screen)), ZPixmap, 0,
                           reinterpret_cast<char*>(g_pixels), g_page_w, g_page_h, 32, 0);
    if (g_image == nullptr) {
      gui_close();
      return false;
    }
    // The glass starts white, which is what the panel's own memory does (docs/50).
    for (size_t i = 0; i < static_cast<size_t>(g_page_w) * g_page_h; ++i) g_pixels[i] = gui_level_colour(15);
  }
  if (g_keys_count > 0 && g_font == nullptr) g_font = XLoadQueryFont(g_display, "fixed");
  XMapWindow(g_display, g_window);
  redraw();
  draw_keys();
  return true;
}

void gui_show(const uint8_t* packed, uint16_t width, uint16_t height) {
  if (g_display == nullptr || packed == nullptr || g_image == nullptr) return;
  if (width != g_width || height != g_height) return;
  gui_expand(packed, width, height, g_pixels, g_scale);
  redraw();
}

void gui_pump() {
  if (g_display == nullptr) return;
  while (XPending(g_display) > 0) {
    XEvent event;
    XNextEvent(g_display, &event);
    if (event.type == Expose) {
      // Both halves of the window: the page and the strip of the keys, because an expose has taken them
      // both away (docs/85).
      redraw();
      draw_keys();
    } else if (event.type == ButtonPress && event.xbutton.button == Button1) {
      // **A click on a button is a key and a click on the page is a finger**: the same button of the mouse
      // and two meanings, told apart by where the pointer is (docs/85).
      const int key = gui_key_at(event.xbutton.x, event.xbutton.y, g_keys_count, g_win_w, g_win_h, g_side);
      const bool on_the_page = static_cast<uint16_t>(event.xbutton.x) >= g_page_x &&
                               static_cast<uint16_t>(event.xbutton.x) < g_page_x + g_page_w &&
                               static_cast<uint16_t>(event.xbutton.y) >= g_page_y &&
                               static_cast<uint16_t>(event.xbutton.y) < g_page_y + g_page_h;
      if (key >= 0) {
        press_key(key, true);
        g_key_held = key;
        draw_keys();
        // **A click that arrives is a line when the run is verbose**: a button that does nothing has to be
        // told apart from a button nobody pressed, and this is the cheapest way (docs/85).
        if (options().verbose) {
          fprintf(stderr, "gui: key %d (%s) down\n", key, label_of_key(g_keys_count, key));
        }
      } else if (on_the_page) {
        put_the_finger(event.xbutton.x - static_cast<int>(g_page_x), event.xbutton.y - static_cast<int>(g_page_y),
                       false);
        g_button_down = true;
      }
    } else if (event.type == MotionNotify) {
      // A pointer that passes over the glass with no button pressed is not a finger: a mouse hovering is
      // not a person touching a panel. And a key that is down does not move with the pointer (docs/85).
      if (g_button_down) {
        put_the_finger(event.xmotion.x - static_cast<int>(g_page_x), event.xmotion.y - static_cast<int>(g_page_y),
                       true);
      }
    } else if (event.type == ButtonRelease && event.xbutton.button == Button1) {
      if (g_key_held >= 0) {
        press_key(g_key_held, false);
        if (options().verbose) {
          fprintf(stderr, "gui: key %d (%s) up\n", g_key_held, label_of_key(g_keys_count, g_key_held));
        }
        g_key_held = -1;
        draw_keys();
      } else if (g_button_down) {
        g_button_down = false;
        lift_the_finger();
      }
    } else if (event.type == ClientMessage &&
               static_cast<Atom>(event.xclient.data.l[0]) == g_wm_delete) {
      g_close_requested = true;
    }
  }
}

bool gui_close_requested() { return g_close_requested; }

uint8_t gui_scale() { return (g_display != nullptr) ? g_scale : 0; }

void gui_close() {
  if (g_display == nullptr) return;
  // A key the pointer was holding is let go: a window that goes away with a key down would leave the bench
  // with a key nobody can lift (docs/85).
  if (g_key_held >= 0) {
    press_key(g_key_held, false);
    g_key_held = -1;
  }
  if (g_font != nullptr) {
    XFreeFont(g_display, g_font);
    g_font = nullptr;
  }
  if (g_image != nullptr) {
    // XDestroyImage frees the pixels it was given, so they are not freed here (docs/85).
    XDestroyImage(g_image);
    g_image = nullptr;
    g_pixels = nullptr;
  } else {
    free(g_pixels);
    g_pixels = nullptr;
  }
  if (g_gc != nullptr) {
    XFreeGC(g_display, g_gc);
    g_gc = nullptr;
  }
  if (g_window != 0) {
    XDestroyWindow(g_display, g_window);
    g_window = 0;
  }
  XCloseDisplay(g_display);
  g_display = nullptr;
}

}  // namespace emu

#else  // no X11 in this build

namespace emu {

bool gui_available() { return false; }

bool gui_open(uint16_t width, uint16_t height, uint8_t keys, KeySide side) {
  (void)width;
  (void)height;
  (void)keys;
  (void)side;
  return false;
}

void gui_show(const uint8_t* packed, uint16_t width, uint16_t height) {
  (void)packed;
  (void)width;
  (void)height;
}

void gui_pump() {}

bool gui_close_requested() { return false; }

uint8_t gui_scale() { return 0; }

void gui_close() {}

}  // namespace emu

#endif  // LUNOK_GUI_X11
