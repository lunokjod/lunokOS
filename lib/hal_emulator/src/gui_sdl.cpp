// The window of the emulator through SDL2, which is the portable one: X11 is a Linux/Unix detail and SDL
// talks to X11, Wayland, Quartz and the Windows desktop with the same calls (docs/85).
#if defined(LUNOK_GUI_SDL)

#include "emu/gui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "emu/gt911.h"
#include "emu/options.h"

namespace emu {
namespace {

SDL_Window* g_window = nullptr;
SDL_Renderer* g_renderer = nullptr;
SDL_Texture* g_texture = nullptr;
uint32_t* g_pixels = nullptr;
bool g_close_requested = false;
uint16_t g_width = 0;
uint16_t g_height = 0;
uint8_t g_scale = 1;
uint8_t g_requested_scale = 0;
bool g_button_down = false;
uint8_t g_keys_count = 0;
KeySide g_side = KeySide::kLeft;
uint16_t g_win_w = 0;
uint16_t g_win_h = 0;
uint16_t g_page_w = 0;
uint16_t g_page_h = 0;
uint16_t g_page_x = 0;
uint16_t g_page_y = 0;
uint16_t g_stage_w = 0;
uint16_t g_stage_h = 0;
// **The key under the mouse, and how long it stays down** (0.179.21): a click that is shorter than a turn of the
// kernel is a press nobody reads, so the window keeps it down for `kClickHoldMs` (docs/10, docs/85).
KeyClick g_click;

// **The lights of the board, which is the face of an ATOM** (0.179.17): the colours of the last frame the strip latched,
// kept so a redraw paints them again, and the colour of a light that is off (docs/85).
//
// **The off of the window has to be darker than the dimmest light of the board** (0.179.20): it was #242424 —thirty-six—
// while the ceiling of the Matrix's panel is *twenty*, so every colour that panel can show was darker than an unlit light
// and the five by five came out as a grid of holes (the owner: *«no veig que l'app rainbow funcioni»*) (docs/50, docs/85).
const uint8_t kMaxLights = 25;
uint8_t g_lights_count = 0;
LightColour g_lights[kMaxLights] = {};
// The ceiling of one channel on this board, which is the exposure the band is drawn with (0.179.20).
uint8_t g_light_max = 255;
const uint8_t kLightOffR = 0x0A;
const uint8_t kLightOffG = 0x0A;
const uint8_t kLightOffB = 0x0A;

const uint8_t kCaseR = 0x1B;
const uint8_t kCaseG = 0x1B;
const uint8_t kCaseB = 0x1B;
const uint8_t kKeyFaceR = 0x2F;
const uint8_t kKeyFaceG = 0x2F;
const uint8_t kKeyFaceB = 0x2F;
const uint8_t kKeyEdgeR = 0x6E;
const uint8_t kKeyEdgeG = 0x6E;
const uint8_t kKeyEdgeB = 0x6E;
const uint8_t kKeyTextR = 0xE8;
const uint8_t kKeyTextG = 0xE8;
const uint8_t kKeyTextB = 0xE8;
const uint8_t kKeyPressedR = 0xEF;
const uint8_t kKeyPressedG = 0xEA;
const uint8_t kKeyPressedB = 0xDE;
const uint8_t kKeyPressedTextR = 0x1A;
const uint8_t kKeyPressedTextG = 0x1B;
const uint8_t kKeyPressedTextB = 0x18;

void set_colour(uint8_t r, uint8_t g, uint8_t b) { SDL_SetRenderDrawColor(g_renderer, r, g, b, 255); }

// **A five by seven face for the words on the keys**, so a portable window needs no font library: the
// labels are four short words and the buttons are the only text in the window (docs/85).
struct Glyph {
  char letter;
  uint8_t rows[7];
};

const Glyph kGlyphs[] = {
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0F, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0F}},
    {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}},
};

const Glyph* glyph_of(char letter) {
  for (size_t i = 0; i < sizeof(kGlyphs) / sizeof(kGlyphs[0]); ++i) {
    if (kGlyphs[i].letter == letter) return &kGlyphs[i];
  }
  return nullptr;
}

int text_width(const char* text, int scale) {
  const int length = static_cast<int>(strlen(text));
  if (length == 0) return 0;
  return (length * 5 + (length - 1)) * scale;
}

void draw_text(const char* text, int x, int y, int scale) {
  int at = x;
  for (const char* p = text; *p != '\0'; ++p) {
    const char letter = (*p >= 'a' && *p <= 'z') ? static_cast<char>(*p - 'a' + 'A') : *p;
    const Glyph* glyph = glyph_of(letter);
    if (glyph != nullptr) {
      for (int row = 0; row < 7; ++row) {
        for (int column = 0; column < 5; ++column) {
          if ((glyph->rows[row] & (1U << (4 - column))) == 0) continue;
          SDL_Rect pixel = {at + column * scale, y + row * scale, scale, scale};
          SDL_RenderFillRect(g_renderer, &pixel);
        }
      }
    }
    at += 6 * scale;
  }
}

// **The band of lights** (0.179.17), under the page and above the row of keys: one dot per light, in the colour the
// strip was given, and a dark dot for one that is off so the panel reads as a panel. A light is drawn two pixels inside
// its box, which is what makes two neighbours of the same colour two lights and not one long bar (docs/85).
void draw_lights() {
  if (g_renderer == nullptr || g_lights_count == 0) return;
  const int band_h = static_cast<int>(gui_lights_height(g_lights_count));
  if (band_h <= 0) return;
  set_colour(kCaseR, kCaseG, kCaseB);
  SDL_Rect band = {g_page_x, g_page_h, g_stage_w, band_h};
  SDL_RenderFillRect(g_renderer, &band);
  for (uint8_t light = 0; light < g_lights_count; ++light) {
    uint16_t x = 0;
    uint16_t y = 0;
    if (!gui_light_place(g_lights_count, light, g_page_x, g_stage_w, g_page_h, x, y)) continue;
    const LightColour colour = g_lights[light];
    if ((colour.red | colour.green | colour.blue) != 0) {
      // **The exposure is the ceiling of the board** (0.179.20): twenty out of twenty is a full colour, which is the light
      // a person sees in a dark room, and it is the only way the panel of the Matrix is not a grid of almost black dots.
      const unsigned red = static_cast<unsigned>(colour.red) * 255u / g_light_max;
      const unsigned green = static_cast<unsigned>(colour.green) * 255u / g_light_max;
      const unsigned blue = static_cast<unsigned>(colour.blue) * 255u / g_light_max;
      set_colour(static_cast<uint8_t>(red > 255u ? 255u : red), static_cast<uint8_t>(green > 255u ? 255u : green),
                 static_cast<uint8_t>(blue > 255u ? 255u : blue));
    } else {
      set_colour(kLightOffR, kLightOffG, kLightOffB);
    }
    SDL_Rect dot = {x + 2, y + 2, kLightDot - 4, kLightDot - 4};
    SDL_RenderFillRect(g_renderer, &dot);
  }
}

// **The release the pump owes** (0.179.21): a click is kept down for `kClickHoldMs` so that a turn of the kernel reads it,
// and this is the pump that puts it back up —the mouse has already been lifted and no more events are coming— (docs/85).
void draw_keys();
void release_the_click_if_due() {
  const int key = gui_key_click_held(g_click);
  if (key < 0) return;
  if (!gui_key_click_due(g_click, gui_now_ms())) return;
  press_key(key, false);
  draw_keys();
}

void draw_keys() {
  if (g_renderer == nullptr || g_keys_count == 0) return;
  const bool row = (g_side == KeySide::kBottom);
  const int top = gui_keys_top(g_win_h, g_keys_count);
  const int left = gui_keys_left(g_win_w, g_keys_count);
  for (uint8_t slot = 0; slot < g_keys_count; ++slot) {
    const int key = gui_key_of_slot(g_keys_count, slot);
    const int x = row ? left + slot * static_cast<int>(kKeyButtonWidth + kKeyGap)
                      : ((g_side == KeySide::kRight)
                             ? static_cast<int>(g_win_w) - static_cast<int>(kKeyStripWidth) +
                                   static_cast<int>(kKeyMargin)
                             : static_cast<int>(kKeyMargin));
    const int y = row ? static_cast<int>(g_win_h) - static_cast<int>(kKeyRowHeight) +
                            static_cast<int>(kKeyMargin)
                      : top + slot * static_cast<int>(kKeyButtonHeight + kKeyGap);
    const bool down = (key >= 0) && key_down(key);
    SDL_Rect face = {x, y, kKeyButtonWidth, kKeyButtonHeight};
    if (down) {
      set_colour(kKeyPressedR, kKeyPressedG, kKeyPressedB);
    } else {
      set_colour(kKeyFaceR, kKeyFaceG, kKeyFaceB);
    }
    SDL_RenderFillRect(g_renderer, &face);
    if (down) {
      set_colour(kKeyPressedTextR, kKeyPressedTextG, kKeyPressedTextB);
    } else {
      set_colour(kKeyEdgeR, kKeyEdgeG, kKeyEdgeB);
    }
    SDL_RenderDrawRect(g_renderer, &face);
    if (down) {
      set_colour(kKeyPressedTextR, kKeyPressedTextG, kKeyPressedTextB);
    } else {
      set_colour(kKeyTextR, kKeyTextG, kKeyTextB);
    }
    // **The sign of the key and not its word** (0.85.1): the runs come from the shared geometry, so the two windows
    // draw the same arrow and the same circle (docs/85).
    KeyRun runs[kKeyIconRuns];
    const uint8_t drawn =
        gui_key_icon(g_keys_count, key, kKeyButtonWidth, kKeyButtonHeight, runs, kKeyIconRuns);
    if (drawn > 0) {
      for (uint8_t run = 0; run < drawn; ++run) {
        SDL_Rect part = {x + runs[run].x, y + runs[run].y, runs[run].w, runs[run].h};
        SDL_RenderFillRect(g_renderer, &part);
      }
    } else {
      const char* label = gui_key_label(g_keys_count, key);
      const int scale = 1;
      draw_text(label, x + (kKeyButtonWidth - text_width(label, scale)) / 2,
                y + (kKeyButtonHeight - 7 * scale) / 2, scale);
    }
  }
}

void redraw() {
  if (g_renderer == nullptr) return;
  set_colour(kCaseR, kCaseG, kCaseB);
  SDL_RenderClear(g_renderer);
  // A frame of the strip may have painted the band since the last redraw, so the page is copied over it and not the
  // other way round (docs/85).
  if (g_texture != nullptr) {
    SDL_Rect page = {g_page_x, g_page_y, g_page_w, g_page_h};
    SDL_RenderCopy(g_renderer, g_texture, nullptr, &page);
  }
  draw_lights();
  draw_keys();
  SDL_RenderPresent(g_renderer);
}

// **A mouse on the window is a finger on the glass**, the same way it is in the X11 backend: a press
// inside the page is a finger down, a move while held is a finger moving, and a release lifts it (docs/50,
// docs/85).
void put_the_finger(int window_x, int window_y, bool moving) {
  if (!options().touch) return;
  Gt911Panel* panel = touch_panel();
  if (panel == nullptr) return;
  if (g_width != Gt911Panel::kFrameWidth || g_height != Gt911Panel::kFrameHeight) {
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

bool on_the_page(int x, int y) {
  return x >= g_page_x && x < g_page_x + g_page_w && y >= g_page_y && y < g_page_y + g_page_h;
}

}  // namespace

bool gui_available() { return true; }

bool gui_open(uint16_t width, uint16_t height, uint8_t keys, KeySide side, uint8_t scale, uint8_t lights,
              uint8_t light_max) {
  if (g_window != nullptr) return true;
  if (width == 0 || height == 0) {
    width = 0;
    height = 0;
  }
  if (width == 0 && keys == 0 && lights == 0) return false;
  if (SDL_Init(SDL_INIT_VIDEO) != 0) return false;

  SDL_DisplayMode mode;
  if (SDL_GetDesktopDisplayMode(0, &mode) != 0) {
    SDL_Quit();
    return false;
  }

  g_requested_scale = scale;
  g_width = width;
  g_height = height;
  g_keys_count = keys;
  g_side = side;
  const int strip_w = (keys == 0) ? 0
                                  : ((side == KeySide::kBottom) ? 0
                                                                : static_cast<int>(gui_key_strip_width()));
  const int strip_h = (keys == 0) ? 0
                                  : ((side == KeySide::kBottom) ? static_cast<int>(gui_key_row_height()) : 0);
  g_scale = gui_fit_scale(width, height, keys, side, mode.w, mode.h, scale);
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

  g_window = SDL_CreateWindow("lunokOS", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, g_win_w,
                              g_win_h, SDL_WINDOW_SHOWN);
  if (g_window == nullptr) {
    SDL_Quit();
    return false;
  }
  g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED);
  if (g_renderer == nullptr) g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE);
  if (g_renderer == nullptr) {
    gui_close();
    return false;
  }

  if (g_page_w > 0 && g_page_h > 0) {
    g_pixels = static_cast<uint32_t*>(calloc(static_cast<size_t>(g_page_w) * g_page_h, sizeof(uint32_t)));
    if (g_pixels == nullptr) {
      gui_close();
      return false;
    }
    for (size_t i = 0; i < static_cast<size_t>(g_page_w) * g_page_h; ++i) g_pixels[i] = gui_level_colour(15);
    g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                  g_page_w, g_page_h);
    if (g_texture == nullptr) {
      gui_close();
      return false;
    }
    SDL_SetTextureBlendMode(g_texture, SDL_BLENDMODE_NONE);
    SDL_UpdateTexture(g_texture, nullptr, g_pixels, g_page_w * static_cast<int>(sizeof(uint32_t)));
  }
  redraw();
  return true;
}

void gui_show(const uint8_t* packed, uint16_t width, uint16_t height) {
  if (g_window == nullptr || packed == nullptr) return;
  if (width != g_width || height != g_height) {
    const uint8_t keys = g_keys_count;
    const KeySide side = g_side;
    const uint8_t requested = g_requested_scale;
    // **The colours of the lights outlive the window**: the strip is still latched and a window made again for a turned
    // page shows the frame that is on the board (docs/85).
    const uint8_t lights = g_lights_count;
    gui_close();
    if (!gui_open(width, height, keys, side, requested, lights, g_light_max)) return;
  }
  if (g_texture == nullptr) return;
  gui_expand(packed, width, height, g_pixels, g_scale);
  SDL_UpdateTexture(g_texture, nullptr, g_pixels, g_page_w * static_cast<int>(sizeof(uint32_t)));
  redraw();
}

void gui_show_lights(const LightColour* lights, uint8_t count) {
  if (g_renderer == nullptr || lights == nullptr) return;
  const uint8_t kept = (count > g_lights_count) ? g_lights_count : count;
  for (uint8_t light = 0; light < kept; ++light) g_lights[light] = lights[light];
  draw_lights();
  SDL_RenderPresent(g_renderer);
}

void gui_pump() {
  if (g_window == nullptr) return;
  release_the_click_if_due();
  SDL_Event event;
  while (SDL_PollEvent(&event) != 0) {
    if (event.type == SDL_QUIT) {
      g_close_requested = true;
    } else if (event.type == SDL_WINDOWEVENT &&
               (event.window.event == SDL_WINDOWEVENT_CLOSE ||
                event.window.event == SDL_WINDOWEVENT_EXPOSED)) {
      if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
        g_close_requested = true;
      } else {
        redraw();
      }
    } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
      const int key = gui_key_at(event.button.x, event.button.y, g_keys_count, g_win_w, g_win_h, g_side);
      if (key >= 0) {
        press_key(key, true);
        gui_key_click_down(g_click, key, gui_now_ms());
        redraw();
        if (options().verbose) {
          fprintf(stderr, "gui: key %d (%s) down\n", key, gui_key_label(g_keys_count, key));
        }
      } else if (on_the_page(event.button.x, event.button.y)) {
        put_the_finger(event.button.x - static_cast<int>(g_page_x),
                       event.button.y - static_cast<int>(g_page_y), false);
        g_button_down = true;
      }
    } else if (event.type == SDL_MOUSEMOTION && g_button_down) {
      put_the_finger(event.motion.x - static_cast<int>(g_page_x),
                     event.motion.y - static_cast<int>(g_page_y), true);
    } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
      if (gui_key_click_held(g_click) >= 0) {
        gui_key_click_up(g_click, gui_now_ms());
        if (options().verbose) {
          fprintf(stderr, "gui: key %d (%s) up\n", gui_key_click_held(g_click), gui_key_label(g_keys_count, gui_key_click_held(g_click)));
        }
        release_the_click_if_due();
        redraw();
      } else if (g_button_down) {
        g_button_down = false;
        lift_the_finger();
      }
    }
  }
}

bool gui_close_requested() { return g_close_requested; }

uint8_t gui_scale() { return (g_window != nullptr) ? g_scale : 0; }

void gui_close() {
  // A key the pointer was holding is let go: a window that goes away with a key down would leave the emulator with a key
  // nobody can lift, and the same goes for a finger the pointer was dragging (docs/85).
  if (gui_key_click_held(g_click) >= 0) press_key(gui_key_click_held(g_click), false);
  g_click = KeyClick();
  if (g_button_down) {
    lift_the_finger();
    g_button_down = false;
  }
  if (g_texture != nullptr) {
    SDL_DestroyTexture(g_texture);
    g_texture = nullptr;
  }
  free(g_pixels);
  g_pixels = nullptr;
  if (g_renderer != nullptr) {
    SDL_DestroyRenderer(g_renderer);
    g_renderer = nullptr;
  }
  if (g_window != nullptr) {
    SDL_DestroyWindow(g_window);
    g_window = nullptr;
  }
  SDL_Quit();
}

}  // namespace emu

#endif  // LUNOK_GUI_SDL
