// The strip of the system at the top of the glass: the hour, the title and the charge (docs/30).
#include "kernel/meter.h"
#include "kernel/bar.h"

#include <stdio.h>
#include <string.h>

#include "kernel/display.h"
#include "kernel/font.h"
#include "kernel/power.h"

namespace kernel {
namespace bar {
namespace {

// **The height of the strip, and the size of its letters.** It was 28 rows —a glyph doubled to fourteen and
// air— and 28 rows with a doubled glyph is a bar that reads only from close up. It is **48 rows with the
// glyphs at four times** now: nine rows of air, twenty-eight of glyph, nine more and the black line of two at
// the bottom, which is 9 + 28 + 9 + 2 = 48 (docs/50).
const uint16_t kBarHeight = 48;

// Glyphs at four times their size: five by seven scaled to twenty by twenty-eight, which is what reads on an
// e-ink glass from an arm's length (docs/50).
const uint8_t kScale = 4;
// Where the text starts, above and to the left, leaving air at the edges.
const uint16_t kPaddingX = 12;
// The air between the hour, the title and the charge: two things that touch are two things nobody can
// tell apart (docs/30).
const uint16_t kTitleGap = 8;
const uint16_t kTextTop = 9;
// The black line under the bar.
const uint16_t kLineHeight = 2;

// The charge is read from the board at most this often: it moves in minutes, not in turns, and a
// reading is an ADC conversion (docs/50). Half a minute is soon enough for a number that changes once
// in an hour, and it keeps the reading off the turn a person is typing in.
const int64_t kBatteryEveryMs = 30000;
// And the hour, at most this often. **A turn of the loop is not the place to read hardware**: the
// console echoes one character per turn and an app draws one frame per turn, and an I2C read of the
// clock chip on each of them is a keyboard that does not keep up (measured on the board, 1/11).
const int64_t kClockEveryMs = 2000;

// **The charge has to move by this much to be a change.** The gauge is an ADC behind a divider and a
// reading of 83 comes back as 84 on the next one; the bar shows the number but it does not repaint for
// a pixel of electricity, because a repaint is ink and the ink is not free (docs/50).
const int kChargeDeadBand = 2;

// What the bar knows: the minute and the charge. One read makes one of these, and one paint draws one
// of them — **the values are read once and carried**, so what is drawn is exactly what was compared
// (measured on the board, 1/11: reading again inside the paint made the bar repaint itself every half
// minute with nothing changed) (docs/30).
struct Status {
  uint32_t minute = 0;
  bool has_battery = false;
  int percent = 0;
};

hal::IPlatform* g_platform = nullptr;
char g_title[48] = {0};
bool g_title_changed = true;
// **The glass under the bar changed** —a turn, a new size, the boot— and the whole strip is owed, not one box of it.
bool g_whole_changed = false;
bool g_drawn = false;

// What is on the glass, and the last values read, which may be newer than it.
Status g_shown;
Status g_read;
int64_t g_clock_read_ms = 0;
int64_t g_battery_read_ms = 0;

// The only place the bar asks the board anything, and **it asks only what is due**: reading the gauge
// because the clock is due would be an ADC conversion every two seconds for a number that moves twice
// an hour (docs/50).
Status read_status(bool clock_due, bool battery_due) {
  Status status = g_read;
  if (clock_due) {
    hal::IClock* clock = (g_platform != nullptr) ? g_platform->clock() : nullptr;
    int64_t epoch_ms = 0;
    if (clock != nullptr) {
      // The hour, from the chip, on every turn of the bar (docs/30, docs/50).
      meter::Scope scope(meter::kClockNow);
      epoch_ms = clock->epoch_ms();
    }
    status.minute = (epoch_ms < 0) ? 0 : static_cast<uint32_t>(epoch_ms / 60000);
  }
  if (battery_due) {
    int mv = 0;
    int percent = 0;
    bool charging = false;
    status.has_battery = power::battery(mv, percent, charging);
    status.percent = percent;
  }
  return status;
}

void format_time(uint32_t minute, char* out, size_t cap) {
  // **The hour of the clock, in UTC.** The system has one epoch and no zone: a zone is a setting, and
  // the day there is one it is one line here and not a guess (docs/60).
  snprintf(out, cap, "%02u:%02u", static_cast<unsigned>((minute / 60) % 24),
           static_cast<unsigned>(minute % 60));
}

void format_charge(bool has_battery, int percent, char* out, size_t cap) {
  if (!has_battery || percent < 0) {
    snprintf(out, cap, "--");
    return;
  }
  if (percent > 100) percent = 100;
  snprintf(out, cap, "%d%%", percent);
}

// **The three boxes of the strip** (0.85.2): the hour on the left, the title in the middle and the charge on the right.
// Each one is measured from the widest text it can carry —"00:00" and "100%"— so a box is where it was on every paint
// and one of them can be cleaned and drawn again on its own. The boxes do not touch, so a repaint of one is a repaint of
// one (docs/50, docs/85).
struct Box {
  uint16_t x;
  uint16_t y;
  uint16_t w;
  uint16_t h;
};

struct Layout {
  uint16_t panel_w;
  Box time;
  Box title;
  Box charge;
};

Layout layout_of(uint16_t panel_w) {
  const uint16_t glyph_h = static_cast<uint16_t>(font::kGlyphHeight * kScale);
  const uint16_t time_w = font::width("00:00", kScale);
  const uint16_t charge_w = font::width("100%", kScale);
  Layout out = {};
  out.panel_w = panel_w;
  out.time = Box{kPaddingX, kTextTop, time_w, glyph_h};
  const int32_t title_left = static_cast<int32_t>(kPaddingX + time_w + kTitleGap);
  const int32_t title_right = static_cast<int32_t>(panel_w) - kPaddingX - charge_w - kTitleGap;
  out.title = Box{static_cast<uint16_t>(title_left), kTextTop,
                  static_cast<uint16_t>((title_right > title_left) ? (title_right - title_left) : 0), glyph_h};
  const int32_t charge_x = static_cast<int32_t>(panel_w) - kPaddingX - charge_w;
  out.charge = Box{static_cast<uint16_t>((charge_x > 0) ? charge_x : 0), kTextTop, charge_w, glyph_h};
  return out;
}

// **A box goes back to paper and its text is drawn over it**, cut at the box: what the bar draws is ink of two levels
// and paper, and the whole of what a box does is clean itself (docs/50).
void draw_time(hal::ISurface& surface, const Layout& at, const Status& status) {
  char text[8] = {0};
  format_time(status.minute, text, sizeof(text));
  surface.fill_rect(at.time.x, at.time.y, at.time.w, at.time.h, 15);
  font::draw(surface, at.time.x, at.time.y, text, kScale, 0);
}

void draw_title(hal::ISurface& surface, const Layout& at) {
  if (at.title.w == 0) return;
  surface.fill_rect(at.title.x, at.title.y, at.title.w, at.title.h, 15);
  if (g_title[0] == '\0') return;
  // **The title sits in the box between the hour and the charge, and the box cuts it.** It is centred on the strip and
  // what hangs off that box is not painted, so a long title loses its ends instead of running over the hour or under
  // the charge: the border is what cuts a text, not the caller counting margins (docs/30, docs/90).
  const uint16_t title_w = font::width(g_title, kScale);
  const int32_t centred = (static_cast<int32_t>(at.panel_w) - static_cast<int32_t>(title_w)) / 2;
  font::Clip box;
  box.x0 = static_cast<int32_t>(at.title.x);
  box.x1 = static_cast<int32_t>(at.title.x + at.title.w);
  box.y0 = static_cast<int32_t>(at.title.y);
  box.y1 = static_cast<int32_t>(at.title.y + at.title.h);
  font::draw(surface, static_cast<uint16_t>((centred < 0) ? 0 : centred), at.title.y, g_title, kScale, 0, box);
}

void draw_charge(hal::ISurface& surface, const Layout& at, const Status& status) {
  char text[8] = {0};
  format_charge(status.has_battery, status.percent, text, sizeof(text));
  surface.fill_rect(at.charge.x, at.charge.y, at.charge.w, at.charge.h, 15);
  // The number is against the right padding and not against the left of its box: a charge of three digits and one of
  // two are the same number of the same corner (docs/30).
  const uint16_t text_w = font::width(text, kScale);
  const int32_t x = static_cast<int32_t>(at.panel_w) - kPaddingX - static_cast<int32_t>(text_w);
  if (x >= static_cast<int32_t>(at.charge.x)) {
    font::draw(surface, static_cast<uint16_t>(x), at.charge.y, text, kScale, 0);
  }
}

// The paper of the strip, and the line under it, which belong to all three boxes and are painted with the strip.
void paint_ground(hal::ISurface& surface) {
  const uint16_t panel_w = surface.width();
  surface.fill_rect(0, 0, panel_w, kBarHeight, 15);
  surface.fill_rect(0, static_cast<uint16_t>(kBarHeight - kLineHeight), panel_w, kLineHeight, 0);
}

// One paint of the whole strip, of **the values it was given**: it reads nothing, so what is on the glass is exactly
// what the decision compared (docs/30).
void paint(hal::UpdateMode mode, const Status& status) {
  hal::ISurface* surface = display::surface();
  if (surface == nullptr) return;
  paint_ground(*surface);
  const Layout at = layout_of(surface->width());
  draw_time(*surface, at, status);
  draw_title(*surface, at);
  draw_charge(*surface, at, status);
  display::present(mode);
  g_shown = status;
  g_title_changed = false;
  g_whole_changed = false;
  g_drawn = true;
}

// **A box of the strip and not the strip** (0.85.2): a title that changes is the middle of the bar, and the hour is the
// hour. The quality is **`gld16`, which cleans and does not blink**, and not `gc16`, which is four blinks over the whole
// strip for a word that changed; `du` is not used at all, because it paints and does not clean and a digit of a minute
// ago would stay on the glass. It is the rule the project already had written down: nothing that flickers reaches the
// glass, and cleaning is `gld16` (docs/50, docs/92, docs/95).
void paint_boxes(hal::UpdateMode mode, const Status& status, bool time, bool title, bool charge) {
  hal::ISurface* surface = display::surface();
  if (surface == nullptr) return;
  const Layout at = layout_of(surface->width());
  if (time) draw_time(*surface, at, status);
  if (title) draw_title(*surface, at);
  if (charge) draw_charge(*surface, at, status);
  display::present(mode);
  g_shown = status;
  g_title_changed = false;
  g_drawn = true;
}

}  // namespace

uint16_t height() { return kBarHeight; }

bool boot(hal::IPlatform& platform) {
  if (!display::available()) return false;
  g_platform = &platform;
  g_title[0] = '\0';
  // The first paint of the bar is **`gc16` and not `init`**: `init` is the two-second beginning of the
  // glass, and while it runs the controller holds its busy line low for two whole seconds — which is
  // the ceiling of the driver, so an app that paints right after the boot found the panel refusing
  // (measured on the board, 1/11). `gc16` leaves the bar clean in half a second (docs/50).
  g_read = read_status(true, true);
  paint(hal::UpdateMode::kGc16, g_read);
  hal::IClock* clock = platform.clock();
  int64_t mono = 0;
  if (clock != nullptr) {
    meter::Scope scope(meter::kClockMono);
    mono = clock->monotonic_ms();
  }
  g_clock_read_ms = mono;
  g_battery_read_ms = mono;
  return true;
}

void title(const char* text) {
  const char* wanted = (text != nullptr) ? text : "";
  if (strncmp(g_title, wanted, sizeof(g_title)) == 0) return;
  snprintf(g_title, sizeof(g_title), "%s", wanted);
  g_title_changed = true;
}

void refresh() {
  // The glass changed under the bar —a turn, a new size— and what it has on it is not the bar any more:
  // it is painted again whole, with the quality that leaves no history (docs/50).
  g_whole_changed = true;
}

void poll() {
  if (!g_drawn || g_platform == nullptr) return;
  hal::IClock* clock = g_platform->clock();
  int64_t mono = 0;
  if (clock != nullptr) {
    meter::Scope scope(meter::kClockMono);
    mono = clock->monotonic_ms();
  }
  const bool clock_due = (mono - g_clock_read_ms) >= kClockEveryMs;
  const bool battery_due = (mono - g_battery_read_ms) >= kBatteryEveryMs;
  // **Nothing due and nothing said: no read and no paint.** This is the whole turn of a bar that has not
  // changed, and it is two comparisons (docs/30).
  if (!clock_due && !battery_due && !g_title_changed && !g_whole_changed) return;

  if (clock_due || battery_due) {
    g_read = read_status(clock_due, battery_due);
    if (clock_due) g_clock_read_ms = mono;
    if (battery_due) g_battery_read_ms = mono;
  }

  const bool minute_changed = (g_read.minute != g_shown.minute);
  const bool charge_changed =
      (g_read.has_battery != g_shown.has_battery) ||
      (g_read.has_battery && (g_read.percent - g_shown.percent >= kChargeDeadBand)) ||
      (g_read.has_battery && (g_shown.percent - g_read.percent >= kChargeDeadBand));
  // **A bar with nothing new paints nothing.** Not on a turn, not every five seconds, not at all.
  if (!g_whole_changed && !g_title_changed && !minute_changed && !charge_changed) return;

  // **The strip is whole when the glass under it moved**, and only then: a page that turned took the bar with it and
  // there is no box to clean, there is a strip to draw (docs/50).
  if (g_whole_changed) {
    paint(hal::UpdateMode::kGc16, g_read);
    return;
  }
  // **And otherwise the boxes that changed, at the wash of two levels** (0.85.2): the hour is a digit and the title is
  // a word, and neither of them is worth four blinks of the whole strip (docs/50, docs/85).
  paint_boxes(hal::UpdateMode::kGld16, g_read, minute_changed, g_title_changed, charge_changed);
}

}  // namespace bar
}  // namespace kernel
