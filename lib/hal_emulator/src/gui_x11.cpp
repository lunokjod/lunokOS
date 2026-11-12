#if defined(LUNOK_GUI_X11)

#include "emu/gui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
// The scale the run asked for (`--gui-scale`) or zero for the one that fits: a window that has to reopen
// after a turn of the glass asks for the same one again (docs/85).
uint8_t g_requested_scale = 0;
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
// **The stage**: the part of the window the page and the band of lights share, which is the window without the strip of
// the keys. The page starts where it starts and the lights are centred in what is left (0.179.17, docs/85).
uint16_t g_stage_w = 0;
uint16_t g_stage_h = 0;
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

// **The lights of the board, which is the face of an ATOM** (0.179.17): the colours of the last frame the strip
// latched, kept so an expose paints them again, and the colour of a light that is off —a very dark grey and not the
// case— so the five by five of the Matrix is a panel a person can see even when it is dark (docs/85).
const uint8_t kMaxLights = 25;
uint8_t g_lights_count = 0;
// The ceiling of one channel on this board, which is the exposure the band is drawn with (0.179.20).
uint8_t g_light_max = 255;
LightColour g_lights[kMaxLights] = {};
unsigned long g_light_off = 0;

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

// **The band of lights** (0.179.17), under the page and above the row of keys: one dot per light, in the colour the
// strip was given, and a dark dot for one that is off so the panel reads as a panel. A light that is lit is drawn
// slightly smaller than its box (two pixels of air on each side), which is what makes two neighbours of the same
// colour two lights and not one long bar (docs/85).
void draw_lights() {
  if (g_display == nullptr || g_window == 0 || g_lights_count == 0) return;
  const int band_h = static_cast<int>(gui_lights_height(g_lights_count));
  if (band_h <= 0) return;
  XSetForeground(g_display, g_gc, g_case);
  XFillRectangle(g_display, g_window, g_gc, static_cast<int>(g_page_x), static_cast<int>(g_page_h),
                 static_cast<unsigned>(g_stage_w), static_cast<unsigned>(band_h));
  for (uint8_t light = 0; light < g_lights_count; ++light) {
    uint16_t x = 0;
    uint16_t y = 0;
    if (!gui_light_place(g_lights_count, light, g_page_x, g_stage_w, g_page_h, x, y)) continue;
    const LightColour colour = g_lights[light];
    // **A light that is off is the colour of the band and not the one of its neighbour**: the colour of the window and not
    // of a palette entry, because a light of any of the sixteen million colours is what the strip writes and asking the
    // colour map for each of them would run out of cells (docs/85).
    unsigned long pixel = g_light_off;
    if ((colour.red | colour.green | colour.blue) != 0) {
      // **The exposure is the ceiling of the board** (0.179.20): twenty out of twenty is a full colour, which is the light
      // a person sees in a dark room, and it is the only way the panel of the Matrix is not a grid of almost black dots.
      const unsigned red = static_cast<unsigned>(colour.red) * 255u / g_light_max;
      const unsigned green = static_cast<unsigned>(colour.green) * 255u / g_light_max;
      const unsigned blue = static_cast<unsigned>(colour.blue) * 255u / g_light_max;
      pixel = ((red > 255u ? 255u : red) << 16) | ((green > 255u ? 255u : green) << 8) | (blue > 255u ? 255u : blue);
    }
    XSetForeground(g_display, g_gc, pixel);
    XFillRectangle(g_display, g_window, g_gc, x + 2, y + 2, static_cast<unsigned>(kLightDot - 4),
                   static_cast<unsigned>(kLightDot - 4));
  }
  XFlush(g_display);
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
    // **The sign of the key and not its word** (0.85.1): the runs come from the shared geometry, so the two windows
    // draw the same arrow and the same circle (docs/85).
    KeyRun runs[kKeyIconRuns];
    const uint8_t drawn =
        gui_key_icon(g_keys_count, key, kKeyButtonWidth, kKeyButtonHeight, runs, kKeyIconRuns);
    if (drawn > 0) {
      XSetForeground(g_display, g_gc, down ? g_key_pressed_text : g_key_text);
      for (uint8_t run = 0; run < drawn; ++run) {
        XFillRectangle(g_display, g_window, g_gc, x + runs[run].x, y + runs[run].y, runs[run].w, runs[run].h);
      }
    } else if (g_font != nullptr) {
      const char* label = gui_key_label(g_keys_count, key);
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

// **A mouse on the window is a finger on the glass**: the glass of this emulator is the panel of the model,
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
              "gui: the page is %ux%u and the touch panel counts in %ux%u, so the mouse does not reach it\n",
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

bool gui_open(uint16_t width, uint16_t height, uint8_t keys, KeySide side, uint8_t scale, uint8_t lights,
              uint8_t light_max) {
  if (g_window != 0) return true;
  // **A page of nothing is a board with no glass**: its window is the keys and the lights, which is the window an
  // ATOM gets (docs/85).
  if (width == 0 || height == 0) {
    width = 0;
    height = 0;
  }
  if (width == 0 && keys == 0 && lights == 0) return false;
  if (!ensure_display()) return false;
  g_requested_scale = scale;

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
  // **An unlit light is darker than the case and not lighter** (0.179.20): the old #242424 was a dark grey of thirty-six
  // while the ceiling of the Matrix's panel is twenty, so every light the board can show was darker than the "off" of the
  // window and the five by five came out as a grid of holes (docs/85).
  g_light_off = colour_of("#0a0a0a", g_case);
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
  // **The whole thing has to fit on the screen**, unless the run asked for a size: a window taller than the
  // display is a window the window manager clips, and two thirds of a page that looks complete is worse than
  // a page at half; `--gui-scale` wins over that, because a run that asked for a scale wants that scale even
  // when the page would fit bigger (docs/85). The draw is every `g_scale` pixels either way.
  const int screen_w = DisplayWidth(g_display, screen);
  const int screen_h = DisplayHeight(g_display, screen);
  g_scale = gui_fit_scale(width, height, keys, side, screen_w, screen_h, scale);
  g_page_w = (width == 0) ? 0 : static_cast<uint16_t>((width + g_scale - 1) / g_scale);
  g_page_h = (height == 0) ? 0 : static_cast<uint16_t>((height + g_scale - 1) / g_scale);
  g_page_x = static_cast<uint16_t>((side == KeySide::kRight) ? 0 : strip_w);
  g_page_y = 0;
  // **The band of lights is the third part of the window** (0.179.17): under the page and above the row of keys, so the
  // face of the board reads as the board —the panel of lights, then the button— and a model with no glass has it at the
  // top (docs/85). The page and the band share the **stage**, which is the window without the strip of the keys: a column
  // on a side makes the window wider and the lights stay in the part the page would have had, instead of being drawn over
  // the buttons.
  g_lights_count = (lights > kMaxLights) ? kMaxLights : lights;
  // **The exposure of the band** (0.179.20): the ceiling of one channel on this board, so a light at its own top is a
  // full colour in the window and not a byte of twenty out of two hundred and fifty-five. Zero is a board that says
  // nothing and is taken as a full range, which is what a light of 255 already is.
  g_light_max = (light_max == 0) ? 255 : light_max;
  const int lights_w = static_cast<int>(gui_lights_width(g_lights_count));
  const int lights_h = static_cast<int>(gui_lights_height(g_lights_count));
  const int row_w = (side == KeySide::kBottom) ? static_cast<int>(gui_keys_width(keys)) : 0;
  g_stage_w = static_cast<uint16_t>(g_page_w);
  if (lights_w > static_cast<int>(g_stage_w)) g_stage_w = static_cast<uint16_t>(lights_w);
  if (row_w > static_cast<int>(g_stage_w)) g_stage_w = static_cast<uint16_t>(row_w);
  // **And the stage is at least as tall as a column of keys**: a board of one light and one key on its side would
  // otherwise open a window of twenty-four pixels with a button of forty hanging out of it (found on the window, 12/11).
  const int column_h = (side == KeySide::kBottom) ? 0 : static_cast<int>(gui_keys_height(keys));
  g_stage_h = static_cast<uint16_t>(g_page_h + lights_h);
  if (column_h > static_cast<int>(g_stage_h)) g_stage_h = static_cast<uint16_t>(column_h);
  g_win_w = static_cast<uint16_t>(strip_w + g_stage_w);
  g_win_h = static_cast<uint16_t>(strip_h + g_stage_h);
  // A row of keys under a stage of nothing is still a window, and so is a panel of lights with no key.
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
  draw_lights();
  draw_keys();
  return true;
}

void gui_show(const uint8_t* packed, uint16_t width, uint16_t height) {
  if (g_display == nullptr || packed == nullptr) return;
  if (width != g_width || height != g_height) {
    // **The glass turned and the page is another rectangle**: the window follows it instead of keeping
    // the last frame for ever. The keys and the scale the run asked for do not change, and the new page
    // gets the scale that fits it, which is what a person would see after turning the board (docs/85).
    const uint8_t keys = g_keys_count;
    const KeySide side = g_side;
    const uint8_t requested = g_requested_scale;
    // **The colours of the lights outlive the window**: the strip is still latched and a window made again for a turned
    // page shows the frame that is on the board, not a dark panel (docs/85).
    const uint8_t lights = g_lights_count;
    gui_close();
    if (!gui_open(width, height, keys, side, requested, lights, g_light_max)) return;
  }
  if (g_image == nullptr) return;
  gui_expand(packed, width, height, g_pixels, g_scale);
  redraw();
}

void gui_show_lights(const LightColour* lights, uint8_t count) {
  if (g_display == nullptr || g_window == 0 || lights == nullptr) return;
  const uint8_t kept = (count > g_lights_count) ? g_lights_count : count;
  for (uint8_t light = 0; light < kept; ++light) g_lights[light] = lights[light];
  draw_lights();
}

void gui_pump() {
  if (g_display == nullptr) return;
  while (XPending(g_display) > 0) {
    XEvent event;
    XNextEvent(g_display, &event);
    if (event.type == Expose) {
      // The three parts of the window: the page, the band of lights and the strip of the keys, because an expose has
      // taken them all away (docs/85).
      redraw();
      draw_lights();
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
          fprintf(stderr, "gui: key %d (%s) down\n", key, gui_key_label(g_keys_count, key));
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
          fprintf(stderr, "gui: key %d (%s) up\n", g_key_held, gui_key_label(g_keys_count, g_key_held));
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
  // A key the pointer was holding is let go: a window that goes away with a key down would leave the emulator
  // with a key nobody can lift, and the same goes for a finger the pointer was dragging (docs/85).
  if (g_key_held >= 0) {
    press_key(g_key_held, false);
    g_key_held = -1;
  }
  if (g_button_down) {
    lift_the_finger();
    g_button_down = false;
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


#endif  // LUNOK_GUI_X11
