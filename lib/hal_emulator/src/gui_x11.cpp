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
// **El gir del vidre** (0.190.283): what the last paint said the page is turned by, which is what a click on a turned page
// needs to land where it was aimed (docs/85).
uint16_t g_turn = kChipPageDegrees;
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
// **Els botons de la caixa, en una còpia** (0.192.7): `emu::model()` torna el model per valor, o sigui que un punter a la seva taula
// quedaria penjat; el que es desa és la taula sencera.
Button g_buttons[kMostButtons];
uint8_t g_button_count = 0;
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
// **The key under the mouse, and how long it stays down** (0.179.21): a click that is shorter than a turn of the
// kernel is a press nobody reads, so the window keeps it down for `kClickHoldMs` (docs/10, docs/85).
KeyClick g_click;

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

// **The release the pump owes** (0.179.21): a click is kept down for `kClickHoldMs` so that a turn of the kernel reads it,
// and this is the pump that puts it back up —the mouse has already been lifted and no more events are coming— (docs/85).
// **Un botó de la finestra, cadascun amb la seva feina** (0.192.7): només el que és una tecla del nucli passa pel camí de les tecles; el
// `RESET` reinicia la placa, el `BOOT` compta com una pulsació (desperta un xip adormit) i el que no és res no fa res (docs/50, docs/85).
void press_button_of(uint8_t at) {
  if (at >= g_button_count) return;
  const Button& one = g_buttons[at];
  switch (one.role) {
    case ButtonRole::kKey:
      if (one.key == 0xFF) return;
      press_key(one.key, true);
      gui_key_click_down(g_click, one.key, gui_now_ms());
      return;
    case ButtonRole::kReset:
      press_reset();
      return;
    case ButtonRole::kBoot:
      press_boot();
      return;
    default:
      return;
  }
}

void draw_keys();
void release_the_click_if_due() {
  const int key = gui_key_click_held(g_click);
  if (key < 0) return;
  if (!gui_key_click_due(g_click, gui_now_ms())) return;
  press_key(key, false);
  draw_keys();
}

void draw_keys() {
  if (g_display == nullptr || g_window == 0 || g_button_count == 0) return;
  for (uint8_t i = 0; i < g_button_count; ++i) {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    if (!gui_button_rect(g_buttons, g_button_count, i, g_win_w, g_win_h, x, y, w, h)) continue;
    const Button& one = g_buttons[i];
    // **Un botó que no és una tecla no s'apaga mai** (0.192.7): el `RESET` reinicia, el `BOOT` no el llegeix ningú mentre la placa corre i
    // l'`IO48` no és res; només el que és una tecla del nucli té un estat que ensenyar (docs/50, docs/85).
    const bool down = (one.role == ButtonRole::kKey) && one.key != 0xFF && key_down(one.key);
    XSetForeground(g_display, g_gc, down ? g_key_pressed : g_key_face);
    XFillRectangle(g_display, g_window, g_gc, x, y, static_cast<unsigned>(kKeyButtonWidth),
                   static_cast<unsigned>(kKeyButtonHeight));
    XSetForeground(g_display, g_gc, down ? g_key_pressed : g_key_edge);
    XDrawRectangle(g_display, g_window, g_gc, x, y, static_cast<unsigned>(kKeyButtonWidth - 1),
                   static_cast<unsigned>(kKeyButtonHeight - 1));
    // **El nom que hi ha escrit a la caixa** (0.192.7): `RESET`, `POWER`, `BOOT`, `IO48`, `PWR`, `UP`, `DOWN` o `BUTTON` — el nom del
    // botó i no una paraula que el codi s'inventi, perquè qui mira la finestra ha de saber què prem (docs/85).
    if (g_font != nullptr) {
      const char* label = one.name;
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
// is the finger coming up — in the frame the chip counts in, which is the panel's own and not the page's when
// the glass is turned (0.190.283, docs/50, docs/85).
void put_the_finger(int window_x, int window_y, bool moving) {
  // `--no-touch`: the panel of the model is there and nobody reads it, so the mouse has nothing to say.
  if (!options().touch) return;
  Gt911Panel* panel = touch_panel();
  if (panel == nullptr) return;
  uint16_t x = 0;
  uint16_t y = 0;
  gui_page_point(window_x, window_y, g_scale, g_width, g_height, x, y);
  // **A turned page is walked back to the frame of the chip** (0.190.283): the mouse counts in the page's own frame and the
  // GT911 of the model in 540×960, so a click on a landscape page used to land nowhere and now lands where it was aimed
  // (docs/50, docs/85).
  uint16_t chip_x = 0;
  uint16_t chip_y = 0;
  gui_chip_point(x, y, g_width, g_height, gui_page_turn(g_turn), chip_x, chip_y);
  if (moving) {
    panel->finger_move(0, chip_x, chip_y);
  } else {
    panel->finger_down(0, chip_x, chip_y);
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

bool gui_open(uint16_t width, uint16_t height, const Button* buttons, uint8_t count, uint8_t scale,
              uint8_t lights, uint8_t light_max) {
  if (g_window != 0) return true;
  // **A page of nothing is a board with no glass**: its window is the keys and the lights, which is the window an
  // ATOM gets (docs/85).
  if (width == 0 || height == 0) {
    width = 0;
    height = 0;
  }
  if (width == 0 && count == 0 && lights == 0) return false;
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
  // **Els botons de la caixa, i poden ser a les dues cares** (0.192.7): a strip on the left, a strip on the right and a row under the page,
  // segons la cara de cada botó, i la pàgina es queda el que hi ha entremig (docs/50, docs/85).
  g_button_count = (count > kMostButtons) ? kMostButtons : count;
  for (uint8_t i = 0; i < g_button_count; ++i) g_buttons[i] = buttons[i];
  const bool has_left = gui_buttons_on(g_buttons, g_button_count, KeySide::kLeft) > 0;
  const bool has_right = gui_buttons_on(g_buttons, g_button_count, KeySide::kRight) > 0;
  const bool has_row = gui_buttons_on(g_buttons, g_button_count, KeySide::kBottom) > 0;
  const uint8_t most_in_a_column = (gui_buttons_on(g_buttons, g_button_count, KeySide::kLeft) >
                                    gui_buttons_on(g_buttons, g_button_count, KeySide::kRight))
                                       ? gui_buttons_on(g_buttons, g_button_count, KeySide::kLeft)
                                       : gui_buttons_on(g_buttons, g_button_count, KeySide::kRight);
  const int strip_w = (has_left ? static_cast<int>(gui_key_strip_width()) : 0) +
                      (has_right ? static_cast<int>(gui_key_strip_width()) : 0);
  const int strip_h = has_row ? static_cast<int>(gui_key_row_height()) : 0;
  // **The whole thing has to fit on the screen**, unless the run asked for a size: a window taller than the
  // display is a window the window manager clips, and two thirds of a page that looks complete is worse than
  // a page at half; `--gui-scale` wins over that, because a run that asked for a scale wants that scale even
  // when the page would fit bigger (docs/85). The draw is every `g_scale` pixels either way.
  const int screen_w = DisplayWidth(g_display, screen);
  const int screen_h = DisplayHeight(g_display, screen);
  g_scale = gui_fit_scale_for(width, height, g_buttons, g_button_count, screen_w, screen_h, scale);
  g_page_w = (width == 0) ? 0 : static_cast<uint16_t>((width + g_scale - 1) / g_scale);
  g_page_h = (height == 0) ? 0 : static_cast<uint16_t>((height + g_scale - 1) / g_scale);
  g_page_x = static_cast<uint16_t>(has_left ? gui_key_strip_width() : 0);
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
  const int row_w =
      has_row ? static_cast<int>(gui_keys_width(gui_buttons_on(g_buttons, g_button_count, KeySide::kBottom))) : 0;
  g_stage_w = static_cast<uint16_t>(g_page_w);
  if (lights_w > static_cast<int>(g_stage_w)) g_stage_w = static_cast<uint16_t>(lights_w);
  if (row_w > static_cast<int>(g_stage_w)) g_stage_w = static_cast<uint16_t>(row_w);
  // **And the stage is at least as tall as a column of keys**: a board of one light and one key on its side would
  // otherwise open a window of twenty-four pixels with a button of forty hanging out of it (found on the window, 12/11).
  const int column_h = static_cast<int>(gui_keys_height(most_in_a_column));
  g_stage_h = static_cast<uint16_t>(g_page_h + lights_h);
  if (column_h > static_cast<int>(g_stage_h)) g_stage_h = static_cast<uint16_t>(column_h);
  g_win_w = static_cast<uint16_t>(strip_w + g_stage_w);
  g_win_h = static_cast<uint16_t>(strip_h + g_stage_h);
  // A row of keys under a stage of nothing is still a window, and so is a panel of lights with no key.
  if (g_win_w == 0) g_win_w = static_cast<uint16_t>(gui_keys_width(most_in_a_column) + 2 * kKeyMargin);
  if (g_win_h == 0) g_win_h = static_cast<uint16_t>(gui_keys_height(most_in_a_column) + 2 * kKeyMargin);
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
  if (g_button_count > 0 && g_font == nullptr) g_font = XLoadQueryFont(g_display, "fixed");
  XMapWindow(g_display, g_window);
  redraw();
  draw_lights();
  draw_keys();
  return true;
}

// Declared before the turn needs it and defined below, with `gui_close`: **el que és de la finestra i el que és de la connexió**
// són dues coses des de la 0.190.282 (docs/85).
namespace {
void forget_the_window();
}

void gui_show(const uint8_t* packed, uint16_t width, uint16_t height, uint16_t degrees) {
  g_turn = degrees;
  if (g_display == nullptr || packed == nullptr) return;
  if (width != g_width || height != g_height) {
    // **The glass turned and the page is another rectangle**: the window follows it instead of keeping
    // the last frame for ever. The keys and the scale the run asked for do not change, and the new page
    // gets the scale that fits it, which is what a person would see after turning the board (docs/85).
    const uint8_t requested = g_requested_scale;
    // **The colours of the lights outlive the window**: the strip is still latched and a window made again for a turned
    // page shows the frame that is on the board, not a dark panel (docs/85).
    const uint8_t lights = g_lights_count;
    forget_the_window();
    if (!gui_open(width, height, g_buttons, g_button_count, requested, lights, g_light_max)) return;
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
  release_the_click_if_due();
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
      // **Cada botó fa el que fa a la placa** (0.192.7): el `POWER` és una tecla del nucli, el `RESET` reinicia la placa, el `BOOT` compta com
      // una pulsació (desperta un xip adormit) i l'`IO48` no fa res (docs/50, docs/85).
      const int at = gui_button_at(event.xbutton.x, event.xbutton.y, g_buttons, g_button_count, g_win_w, g_win_h);
      const bool on_the_page = static_cast<uint16_t>(event.xbutton.x) >= g_page_x &&
                               static_cast<uint16_t>(event.xbutton.x) < g_page_x + g_page_w &&
                               static_cast<uint16_t>(event.xbutton.y) >= g_page_y &&
                               static_cast<uint16_t>(event.xbutton.y) < g_page_y + g_page_h;
      if (at >= 0) {
        press_button_of(static_cast<uint8_t>(at));
        draw_keys();
        // **A click that arrives is a line when the run is verbose**: a button that does nothing has to be
        // told apart from a button nobody pressed, and this is the cheapest way (docs/85).
        if (options().verbose) {
          fprintf(stderr, "gui: button %d (%s) down\n", at, g_buttons[at].name);
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
      if (gui_key_click_held(g_click) >= 0) {
        gui_key_click_up(g_click, gui_now_ms());
        if (options().verbose) {
          fprintf(stderr, "gui: key %d up\n", gui_key_click_held(g_click));
        }
        release_the_click_if_due();
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

bool gui_showing() { return g_display != nullptr; }

uint8_t gui_scale() { return (g_display != nullptr) ? g_scale : 0; }

namespace {

// **Una finestra que marxa, i la connexió que es queda** (0.190.282): a turn of the page is not the end of the run —the window
// is made again for the new rectangle and the connection to the X server is the same one—, i tancar la connexió i tornar-la a
// obrir al mig d'un gir deixava el vidre **sense finestra** de tant en tant: la segona `XOpenDisplay` podia dir que no i
// `gui_open` tornava sense dir res (vist en un run amb `--gui`, l'1 de cada tres cops, amb la pàgina a 960 × 540 i sense cap
// error a la sortida). El que s'ha de tornar quan canvia la mida és el que és de la finestra: el dibuix, el context, la lletra
// i la finestra (docs/85).
void forget_the_window() {
  if (g_display == nullptr) return;
  // A key the pointer was holding is let go: a window that goes away with a key down would leave the emulator
  // with a key nobody can lift, and the same goes for a finger the pointer was dragging (docs/85).
  if (gui_key_click_held(g_click) >= 0) press_key(gui_key_click_held(g_click), false);
  g_click = KeyClick();
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
}

}  // namespace

void gui_close() {
  if (g_display == nullptr) return;
  forget_the_window();
  XCloseDisplay(g_display);
  g_display = nullptr;
}

}  // namespace emu


#endif  // LUNOK_GUI_X11
