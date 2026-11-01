// The panel of the board, as the system owns it: the surface, its size, and the one place a mode name
// is turned into what the controller does. **Nothing here draws and nothing here is reachable from
// Lua**: an app draws on its own canvas (`kernel/ui.h`) and asks for it to be presented, which is what
// keeps the board's buffer the system's (docs/30, docs/50).
#include "kernel/display.h"

#include <string.h>

namespace kernel {
namespace display {
namespace {

hal::ISurface* g_surface = nullptr;

}  // namespace

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

// **The page of this system is a portrait one by default**, whatever the glass is: 540 wide and 960 tall,
// and turned a quarter **the other way** from the first attempt —the one the board showed upside down—.
// It is the system's decision and not the driver's, so it is one line and the bench takes the same turn as
// the board (docs/50).
const uint16_t kDefaultRotation = 270;

bool boot(hal::IPlatform& platform) {
  g_surface = platform.display();
  if (g_surface == nullptr) return false;
  g_surface->set_rotation(kDefaultRotation);
  return true;
}

bool available() { return g_surface != nullptr; }
uint16_t width() { return (g_surface != nullptr) ? g_surface->width() : 0; }
uint16_t height() { return (g_surface != nullptr) ? g_surface->height() : 0; }
uint8_t levels() { return 16; }

uint16_t rotation() { return (g_surface != nullptr) ? g_surface->rotation() : 0; }

bool set_rotation(uint16_t degrees) { return (g_surface != nullptr) && g_surface->set_rotation(degrees); }

hal::ISurface* surface() { return g_surface; }

bool present(hal::UpdateMode mode) {
  if (g_surface == nullptr) return false;
  return g_surface->flush(mode);
}

bool idle() { return (g_surface != nullptr) && g_surface->idle(); }

const char* last_error() { return (g_surface != nullptr) ? g_surface->last_error() : ""; }

}  // namespace display
}  // namespace kernel
