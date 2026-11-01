// The panel and the interpreter: what an app can draw and what it can ask to be painted. No policy of
// its own — no redraw, no clearing, no timer — because the app is the one that knows what is on the
// glass (docs/30, docs/50).
#include "kernel/display.h"

#include <string.h>

#include "luabind/luabind.h"

namespace kernel {
namespace display {
namespace {

hal::ISurface* g_surface = nullptr;

// The only place a mode name is turned into what the controller does: the names of the panel's own
// table, and nothing else. An unknown name is refused instead of being rounded to a default that would
// look like it worked (docs/30).
bool mode_from_name(const char* name, hal::UpdateMode& out) {
  if (name == nullptr) return false;
  struct Entry {
    const char* name;
    hal::UpdateMode mode;
  };
  static const Entry kModes[] = {
      {"init", hal::UpdateMode::kInit},   {"du", hal::UpdateMode::kDu},
      {"gc16", hal::UpdateMode::kGc16},   {"gl16", hal::UpdateMode::kGl16},
      {"glr16", hal::UpdateMode::kGlr16}, {"gld16", hal::UpdateMode::kGld16},
      {"du4", hal::UpdateMode::kDu4},     {"a2", hal::UpdateMode::kA2},
  };
  for (const Entry& entry : kModes) {
    if (strcmp(entry.name, name) == 0) {
      out = entry.mode;
      return true;
    }
  }
  return false;
}

uint8_t clamped(uint8_t level) { return (level > 15) ? 15 : level; }

bool caps_for_lua(luabind::DisplayInfo& out) {
  if (g_surface == nullptr) return false;
  out.width = g_surface->width();
  out.height = g_surface->height();
  out.levels = 16;
  return true;
}

void clear_for_lua(uint8_t level) {
  if (g_surface != nullptr) g_surface->clear(clamped(level));
}

void rect_for_lua(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t level) {
  if (g_surface != nullptr) g_surface->fill_rect(x, y, w, h, clamped(level));
}

void pixel_for_lua(uint16_t x, uint16_t y, uint8_t level) {
  if (g_surface != nullptr) g_surface->pixel(x, y, clamped(level));
}

bool flush_for_lua(const char* mode) {
  if (g_surface == nullptr) return false;
  hal::UpdateMode chosen = hal::UpdateMode::kDu;
  if (mode != nullptr && !mode_from_name(mode, chosen)) return false;
  return g_surface->flush(chosen);
}

bool idle_for_lua() { return (g_surface != nullptr) && g_surface->idle(); }

}  // namespace

bool boot(hal::IPlatform& platform) {
  g_surface = platform.display();
  if (g_surface == nullptr) return false;
  luabind::set_display_ops(
      luabind::DisplayOps{caps_for_lua, clear_for_lua, rect_for_lua, pixel_for_lua, flush_for_lua,
                          idle_for_lua});
  return true;
}

bool available() { return g_surface != nullptr; }
uint16_t width() { return (g_surface != nullptr) ? g_surface->width() : 0; }
uint16_t height() { return (g_surface != nullptr) ? g_surface->height() : 0; }
uint8_t levels() { return 16; }

}  // namespace display
}  // namespace kernel
