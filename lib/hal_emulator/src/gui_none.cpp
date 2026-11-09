// An emulator with no window library: the emulator still boots, still runs apps and still says that this
// build has no window instead of failing at the first `--gui` (docs/85). The pure part of the GUI lives
// in `gui_common.cpp` and is always compiled; this file is only the answer of the window functions.
#if !defined(LUNOK_GUI_X11) && !defined(LUNOK_GUI_SDL)

#include "emu/gui.h"

namespace emu {

bool gui_available() { return false; }

bool gui_open(uint16_t width, uint16_t height, uint8_t keys, KeySide side, uint8_t scale) {
  (void)width;
  (void)height;
  (void)keys;
  (void)side;
  (void)scale;
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

#endif  // no window backend
