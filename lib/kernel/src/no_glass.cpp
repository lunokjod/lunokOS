// **A board without a glass, said once for every call the rest of the system makes** (0.179.9).
//
// The four boards of the project with no panel —the two ATOMs, the bare ESP32 and the T-Koala— build none of the page:
// `ui.cpp`, `font.cpp`, `book.cpp`, `splash.cpp`, `display.cpp`, `bar.cpp`, `touch.cpp` and `orientation.cpp` are all
// inside `#if LUNOK_GLASS`. What is left is the handful of calls the shell and the kernel make **whatever the board is** —a
// start asks for a view, a close gives it back, a reboot says goodbye on the glass, a log line names the moment of the
// boot— and those have to answer something. This file is that answer, and it is the whole of it: the app gets a page that
// owes nothing, the bar says nothing, the finger is not there and the pictures of the moments are not either.
//
// **It is a stub and not a page that pretends.** `ui::attach()` leaves the system in the state a board with a glass leaves
// it —an app in front and nothing owed— so nothing above this file has to ask which board it is; the shell keeps its
// `splash`, `cfg keyboard` and `lua` commands and they answer `no glass`, which is the truth of the build and not a
// command that disappeared from under a person's fingers (docs/30, docs/50, docs/75).
#include "board/identity.h"

#if !LUNOK_GLASS

#include "kernel/app_error.h"
#include "kernel/log.h"
#include "kernel/bar.h"
#include "kernel/splash.h"
#include "kernel/touch.h"
#include "kernel/ui.h"
#include "pal/pal.h"

namespace kernel {

namespace {

// **The one sentence this build has about the page**, answered by every door that would have painted one: a person who
// types `splash` on a board with no screen reads why there is nothing to paint instead of a failure with no reason.
const char kNoGlass[] = "no glass on this board";

}  // namespace

namespace bar {

void demo(bool) {}

void title(const char*) {}

void refresh() {}

bool flush() { return false; }

void poll() {}

}  // namespace bar

namespace splash {

const char* word(Kind) { return kNoGlass; }

Kind kind_of(const char*) { return kNone; }

uint8_t count() { return 0; }

Kind at(uint8_t) { return kNone; }

bool picture_of(Kind, char*, size_t) { return false; }

bool show(Kind) { return false; }

bool show(const char*) { return false; }

bool hide() { return false; }

const char* last_error() { return kNoGlass; }

bool wait() { return true; }

}  // namespace splash

namespace touch {

bool take_gesture_of(const char*, Gesture&) { return false; }

bool take_pinch_in(int16_t, Gesture&) { return false; }

// **No finger, so nothing to count** (0.188.8): `stats` prints the three numbers of the glass on every board, and on one
// with no panel they are three zeros —the same sentence as `no glass`, said as arithmetic because that is what the row is
// (docs/30, docs/50).
void counters(uint32_t& asks, uint32_t& frames, uint32_t& false_warnings) {
  asks = 0;
  frames = 0;
  false_warnings = 0;
}

// **A page cannot begin before a contact of a page that does not exist** (0.189.100): the shell calls this on the way in and
// out of every app, and a board with no touch has no queue to forget (docs/30).
void forget_the_page_before() {}

}  // namespace touch

namespace ui {

void attach() {}
void attach_under_a_picture() {}

void give_back_the_glass() {}

void owe_the_page() {}

uint8_t keyboard_count() { return 0; }

const char* keyboard_at(uint8_t) { return kNoGlass; }

// **`none` and not the sentence**: the line `cfg keyboard` prints is `settings: keyboard none`, which is what a board with
// no band of keys has, and a person typing `cfg keyboard qwerty` reads the refusal of the name (docs/30, docs/70).
const char* keyboard_current() { return "none"; }

bool keyboard_select(const char*) { return false; }

// **No band, so it is never up, never comes down and it types nothing** (0.180.0, 0.189.21): the shell asks these on every
// board —the wire keyboard is a keyboard of the page, and a board without one has no box for its bytes to land in— and the
// honest answer to all three is the one this build has (docs/30, docs/50).
bool keyboard_hide() { return false; }

bool keyboard_visible() { return false; }

bool keyboard_type_the_wire(const char*, size_t) { return false; }

// **Els tres tocs són de la pàgina** (0.189.101): the gesture that closes the app in front belongs to the glass, so a board
// without one has nothing to take away and nothing to refuse; `true` is what `three_taps_close()` answers on a glass, which is
// the state this build is always in (docs/30, docs/90).
bool three_taps_close() { return true; }

bool set_three_taps_close(bool) { return false; }

}  // namespace ui

// **The rectangle of a paint is a question for a glass that is not here** (0.179.12): `panel` at the prompt asks
// these three and answers `no glass on this board`, the same sentence the four boards without a panel give to
// `splash` (docs/30, docs/50).
namespace display {

bool available() { return false; }

// **No glass, so no surface to hand anybody** (0.189.141): `shell::start_the_home_app` asks for it to know whether a board has a
// page to come back to —a board with no screen keeps its prompt, which is where a person wants to be— and the four boards of
// this build answered with a symbol that did not exist: the 0.189.139 added the question to `shell.cpp` and `display.cpp` is
// all inside `#if LUNOK_GLASS`, o sigui que l'enllaç de les quatre es va quedar sense ella i el CI les hauria refusat
// (docs/50, docs/75).
hal::ISurface* surface() { return nullptr; }

bool partial_from_the_box() { return false; }

bool set_partial_from_the_box(bool) { return false; }

// **Els píxels no surten de cap placa** (0.189.66): `panel flush` prints the style the glass has and `panel bus` its clock, so
// these answer what a board with no panel can: the way that every controller has, at the clock the desk says the bus has, and
// a change of either refused with the sentence of this build (docs/50).
hal::FlushStyle flush_style() { return hal::FlushStyle::kRows; }

bool set_flush_style(hal::FlushStyle) { return false; }

const char* flush_style_name(hal::FlushStyle) { return kNoGlass; }

bool flush_style_of(const char*, hal::FlushStyle&) { return false; }

uint32_t bus_mhz() { return 0; }

bool set_bus_mhz(uint32_t) { return false; }

// **Una placa sense vidre no té res a deixar dormir** (0.190.19): the switch exists and says nothing, so the kernel and the shell can ask it
// on any board without a `#if` of their own (docs/50, docs/75).
void set_the_glass_asleep(bool) {}

bool the_glass_is_asleep() { return false; }

// **I el mateix per a la porta de la fila i la VCOM** (0.190.61): the two knobs of the wave (`panel gate` and `panel vcom`) are asked
// by the same command on every board, and a glass with no controller answers zero and refuses the change —which is what this build
// says about a panel it does not have— instead of a link that does not close (docs/50, docs/75). El que falta aquí és el que va
// deixar les quatre bandes sense vidre sense enllaçar el 19/11: `display.cpp` sencer viu dins `#if LUNOK_GLASS` i aquest fitxer és
// l'única resposta que tenen.
uint16_t vcom_mv() { return 0; }

bool set_vcom_mv(uint16_t) { return false; }

uint32_t gate_ns() { return 0; }

bool set_gate_ns(uint32_t) { return false; }

// **I una pintada que no va enlloc diu que sí** (0.189.162): a board with no glass has nothing to send and nothing to refuse, and the
// callers of `present` are the ones that paint a page —`panel bars`, the card of the system—, which such a board does not have.
bool present(hal::UpdateMode) { return false; }

}  // namespace display

// **La pàgina d'una app que s'ha mort no té on pintar-se** (0.189.162): the page of `app_error.cpp` is inside `LUNOK_GLASS` with the
// rest of the page of this system, so a board with no glass answers here —and says where the why is, which is the log and the
// console, because that is the whole of what such a board has— instead of a build that does not link (docs/30, docs/50).
namespace app_error {

void show(const char* id, const char* text) {
  (void)text;
  log_printf(pal::kLogError, "ERROR", "the app %s has stopped; the why is on the console", (id != nullptr) ? id : "(no app)");
}

void set_wait_ms(int64_t) {}

}  // namespace app_error

}  // namespace kernel

#endif  // !LUNOK_GLASS
