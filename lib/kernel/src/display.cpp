// The panel of the board, as the system owns it: the surface, its size, and the one place a mode name
// is turned into what the controller does. **Nothing here draws and nothing here is reachable from
// Lua**: an app draws on its own canvas (`kernel/ui.h`) and asks for it to be presented, which is what
// keeps the board's buffer the system's (docs/30, docs/50).
#include "kernel/display.h"

#include <string.h>

#include "kernel/log.h"
#include "kernel/meter.h"
#include "kernel/power.h"
#include "pal/pal.h"

// **Nothing of this file exists on a board without a glass** (0.179.9): this is the buffer the glass is painted
// from —what a paint is asked for and what the panel says back— and a board with no screen can never run a line of
// it. The question is asked of the board and not of a name in this file: `LUNOK_GLASS` is one line of its
// `board/identity.h`, and it is what decides which translation units exist at all. The four boards of the project
// without a panel —the two ATOMs, the bare ESP32 and the T-Koala— carry neither this code nor the libraries it
// pulls (docs/50, docs/75).
#include "board/identity.h"

#if LUNOK_GLASS

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
// It is the system's decision and not the driver's, so it is one line and the emulator takes the same turn as
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

// **A turn is the most expensive call this module has**: the image of the board is made again and the
// whole page is sent to the controller (docs/50).
bool set_rotation(uint16_t degrees) {
  meter::Scope scope(meter::kGlassFlush);
  return (g_surface != nullptr) && g_surface->set_rotation(degrees);
}

hal::ISurface* surface() { return g_surface; }

// **The rectangle of a partial paint is the glass's business** (0.179.12): two calls that pass the question down and
// nothing else, so a board that cannot answer says so and the kernel has no constant of its own to keep (docs/50).
bool partial_from_the_box() { return (g_surface != nullptr) && g_surface->partial_from_the_box(); }

bool set_partial_from_the_box(bool on) {
  return (g_surface != nullptr) && g_surface->set_partial_from_the_box(on);
}

hal::FlushStyle flush_style() {
  return (g_surface != nullptr) ? g_surface->flush_style() : hal::FlushStyle::kRows;
}

bool set_flush_style(hal::FlushStyle style) {
  return (g_surface != nullptr) && g_surface->set_flush_style(style);
}

const char* flush_style_name(hal::FlushStyle style) {
  switch (style) {
    case hal::FlushStyle::kRows:
      return "rows";
    case hal::FlushStyle::kBurst:
      return "burst";
    case hal::FlushStyle::kBits2:
      return "2bit";
  }
  return "rows";
}

bool flush_style_of(const char* name, hal::FlushStyle& out) {
  if (name == nullptr) return false;
  if (strcmp(name, "rows") == 0) {
    out = hal::FlushStyle::kRows;
    return true;
  }
  if (strcmp(name, "burst") == 0) {
    out = hal::FlushStyle::kBurst;
    return true;
  }
  if (strcmp(name, "2bit") == 0) {
    out = hal::FlushStyle::kBits2;
    return true;
  }
  return false;
}

// **Two meters, because they answer two different questions**: how long a paint costs to ask for —which
// is the transfer of the rectangle and the command, and is paid on the spot— and how often the system
// asks whether the ink is over, which is a register read per turn (docs/30, docs/50).
uint32_t bus_mhz() { return (g_surface != nullptr) ? g_surface->bus_mhz() : 0u; }

bool set_bus_mhz(uint32_t mhz) { return (g_surface != nullptr) && g_surface->set_bus_mhz(mhz); }

uint32_t gate_ns() { return (g_surface != nullptr) ? g_surface->gate_ns() : 0u; }

bool set_gate_ns(uint32_t ns) { return (g_surface != nullptr) && g_surface->set_gate_ns(ns); }

// **Enviar els píxels no és calcular** (0.190.26, l'amo: *«al enviar dades al eink usa 80»*): the burst is a DMA of the bus and
// the ink of the panel is the panel's own time —a page costs 783 ms of `flush` of which a third is the wire and the rest is waiting
// for the controller—, o sigui que el processador no hi té res a fer i el pas de baix és el que li toca. **El preu és el
// `gather`**: les files que es llegeixen de la imatge són feina de debò i a 80 costen unes tres vegades més (99 ms contra ~300 en
// una pàgina); el que mana és que el panell triga 783 ms igualment, i per això el que es guanya en corrent no es perd en temps
// (docs/50, docs/60, docs/95).
//
// **I la placa és qui ho diu** (0.190.39): el paràgraf de dalt és cert en una placa amb controlador, i aquest sistema ja en té una
// sense —`a_paint_is_work` és la pregunta i la T5 E-Paper S3 Pro és la que contesta que sí—. **No ho decideix el driver del
// vidre**: dues plaques poden portar el mateix panell i el mateix driver i no costar el mateix, o sigui que el que mana la
// velocitat és la placa, i aquí només s'obre la porta que ella diu (docs/50, docs/95).
void hold_for_a_paint() {
  if (hal::platform().a_paint_is_work()) {
    power::work_cpu("a paint that computes");
    return;
  }
  power::wait_cpu("a paint");
}

// **El vidre que no s'ha de tocar** (0.190.19): the switch and its two doors. A paint that arrives while the glass is asleep is written into
// the buffer and **not sent**: the damage stays marked, o sigui que el primer `present` després de despertar-lo hi posa el que hi hagi
// (docs/30, docs/50).
bool g_the_glass_is_asleep = false;

void set_the_glass_asleep(bool on) { g_the_glass_is_asleep = on; }

bool the_glass_is_asleep() { return g_the_glass_is_asleep; }

bool present(hal::UpdateMode mode) {
  if (g_surface == nullptr) return false;
  // **Ningú no ha tocat la pantalla**: a wake for an alarm runs its windows and goes down again with the glass exactly as the sleep left it
  // (docs/60).
  if (g_the_glass_is_asleep) return false;
  hold_for_a_paint();
  meter::Scope whole(meter::kGlassFlush);
  // **Not every paint is the same paint.** `gc16` and its family clean the ghost of what was there and
  // are the slow ones; `du` and `du4` are quick and leave a trace. How many of each a page costs is the
  // difference between a page that flickers and one that does not, so they are two rows (docs/50, docs/95).
  const bool cleans = (mode == hal::UpdateMode::kGc16 || mode == hal::UpdateMode::kGl16 ||
                       mode == hal::UpdateMode::kGlr16 || mode == hal::UpdateMode::kGld16);
  // **Què ha costat aquesta pintada, al log** (0.189.67, l'amo: *«ho testejem i recollim dades amb el log»*): the number a
  // person comparing two ways of sending the pixels needs is **the milliseconds the call itself took** —the transfer of the
  // rectangle— and not the ink, which comes later and is asked for by `idle()`. It goes out on every paint: a frame of an
  // animation is one line and the three styles are three different numbers on the same page (docs/50, docs/95).
  const int64_t started_ms = pal::system().millis();
  const bool ok = (cleans) ? [&] {
    meter::Scope slow(meter::kGlassGc16);
    return g_surface->flush(mode);
  }() : g_surface->flush(mode);
  const int64_t spent = pal::system().millis() - started_ms;
  log_printf(pal::kLogDebug, "GLASS", "flush %s %lld ms (%s)", mode_name(mode), static_cast<long long>(spent),
            flush_style_name(flush_style()));
  return ok;
}

bool idle() {
  meter::Scope scope(meter::kGlassIdle);
  return (g_surface != nullptr) && g_surface->idle();
}

const char* last_error() { return (g_surface != nullptr) ? g_surface->last_error() : ""; }

}  // namespace display
}  // namespace kernel

#endif  // LUNOK_GLASS
