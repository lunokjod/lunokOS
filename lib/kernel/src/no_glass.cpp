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

#include "kernel/bar.h"
#include "kernel/splash.h"
#include "kernel/touch.h"
#include "kernel/ui.h"

namespace kernel {

namespace {

// **The one sentence this build has about the page**, answered by every door that would have painted one: a person who
// types `splash` on a board with no screen reads why there is nothing to paint instead of a failure with no reason.
const char kNoGlass[] = "no glass on this board";

}  // namespace

namespace bar {

void title(const char*) {}

void refresh() {}

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

}  // namespace touch

namespace ui {

void attach() {}

void give_back_the_glass() {}

void owe_the_page() {}

uint8_t keyboard_count() { return 0; }

const char* keyboard_at(uint8_t) { return kNoGlass; }

// **`none` and not the sentence**: the line `cfg keyboard` prints is `settings: keyboard none`, which is what a board with
// no band of keys has, and a person typing `cfg keyboard qwerty` reads the refusal of the name (docs/30, docs/70).
const char* keyboard_current() { return "none"; }

bool keyboard_select(const char*) { return false; }

bool keyboard_key(const char*, uint32_t) { return false; }

}  // namespace ui

}  // namespace kernel

#endif  // !LUNOK_GLASS
