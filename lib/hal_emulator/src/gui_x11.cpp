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

}  // namespace emu

#if defined(LUNOK_GUI_X11)

#include <X11/Xlib.h>
#include <X11/Xutil.h>

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

void redraw() {
  if (g_display == nullptr || g_image == nullptr) return;
  XPutImage(g_display, g_window, g_gc, g_image, 0, 0, 0, 0,
            static_cast<unsigned>((g_width + g_scale - 1) / g_scale),
            static_cast<unsigned>((g_height + g_scale - 1) / g_scale));
  XFlush(g_display);
}

}  // namespace

bool gui_available() { return true; }

bool gui_open(uint16_t width, uint16_t height) {
  if (g_display != nullptr) return true;
  if (width == 0 || height == 0) return false;
  g_display = XOpenDisplay(nullptr);
  if (g_display == nullptr) return false;

  const int screen = DefaultScreen(g_display);
  g_width = width;
  g_height = height;
  // **The whole page has to fit on the screen.** A window taller than the display is a window the window
  // manager clips, and two thirds of a page that looks complete is worse than a page at half: the scale
  // is the smallest that fits, and the draw is every `g_scale` pixels (docs/85).
  const int screen_w = DisplayWidth(g_display, screen);
  const int screen_h = DisplayHeight(g_display, screen);
  g_scale = 1;
  while (g_scale < 4 && (width / g_scale > screen_w - 40 || height / g_scale > screen_h - 80)) ++g_scale;
  const uint16_t win_w = static_cast<uint16_t>((width + g_scale - 1) / g_scale);
  const uint16_t win_h = static_cast<uint16_t>((height + g_scale - 1) / g_scale);
  g_window = XCreateSimpleWindow(g_display, RootWindow(g_display, screen), 0, 0, win_w, win_h, 0,
                                 BlackPixel(g_display, screen), WhitePixel(g_display, screen));
  if (g_window == 0) {
    XCloseDisplay(g_display);
    g_display = nullptr;
    return false;
  }
  XStoreName(g_display, g_window, "lunokOS");
  XSelectInput(g_display, g_window, ExposureMask | KeyPressMask | StructureNotifyMask);
  // The corner that closes it. A window that cannot be closed from the window manager is a process
  // somebody has to kill (docs/85).
  g_wm_delete = XInternAtom(g_display, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(g_display, g_window, &g_wm_delete, 1);
  g_gc = XCreateGC(g_display, g_window, 0, nullptr);

  // **One pixel per glass pixel**, in the colour of the display: the image is built here and handed to
  // X as it is, so a paint costs one conversion of the rectangle and not a round trip per pixel
  // (docs/85).
  g_pixels = static_cast<uint32_t*>(calloc(static_cast<size_t>(win_w) * win_h, sizeof(uint32_t)));
  if (g_pixels == nullptr) {
    gui_close();
    return false;
  }
  g_image = XCreateImage(g_display, DefaultVisual(g_display, screen),
                         static_cast<unsigned>(DefaultDepth(g_display, screen)), ZPixmap, 0,
                         reinterpret_cast<char*>(g_pixels), win_w, win_h, 32, 0);
  if (g_image == nullptr) {
    gui_close();
    return false;
  }
  // The glass starts white, which is what the panel's own memory does (docs/50).
  for (size_t i = 0; i < static_cast<size_t>(win_w) * win_h; ++i) g_pixels[i] = gui_level_colour(15);
  XMapWindow(g_display, g_window);
  redraw();
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
      redraw();
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

bool gui_open(uint16_t width, uint16_t height) {
  (void)width;
  (void)height;
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
