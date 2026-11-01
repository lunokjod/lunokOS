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

// The height of the strip. It leaves room for a glyph of seven rows doubled to fourteen, with four rows
// of air above and below and a black line of two at the bottom: 4 + 14 + 6 + 2 + 2 = 28 (docs/50).
const uint16_t kBarHeight = 28;

// Glyphs at twice their size: five by seven scaled to ten by fourteen, which is the smallest that reads
// on an e-ink glass from a normal distance (docs/50).
const uint8_t kScale = 2;
// Where the text starts, above and to the left, leaving three glyph columns of air at the edges.
const uint16_t kPaddingX = 8;
// The air between the hour, the title and the charge: two things that touch are two things nobody can
// tell apart (docs/30).
const uint16_t kTitleGap = 6;
const uint16_t kTextTop = 5;
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

// Every so many minutes the bar is painted with `gc16` instead of `du`: `du` is quick and leaves its
// own history, and twelve repaints of a digit on the same spot is a digit that never goes away
// (measured on the board: `du` does not clean, docs/95).
const uint32_t kCleanEveryMinutes = 10;

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

// One paint of the whole strip, of **the values it was given**: it reads nothing, so what is on the
// glass is exactly what the decision compared (docs/30).
void paint(hal::UpdateMode mode, const Status& status) {
  hal::ISurface* surface = display::surface();
  if (surface == nullptr) return;
  const uint16_t panel_w = surface->width();
  char time_text[8] = {0};
  char charge_text[8] = {0};
  format_time(status.minute, time_text, sizeof(time_text));
  format_charge(status.has_battery, status.percent, charge_text, sizeof(charge_text));

  // The background and the line first: what the texts have to sit on.
  surface->fill_rect(0, 0, panel_w, kBarHeight, 15);
  surface->fill_rect(0, static_cast<uint16_t>(kBarHeight - kLineHeight), panel_w, kLineHeight, 0);

  const uint16_t time_w = font::width(time_text, kScale);
  const uint16_t charge_w = font::width(charge_text, kScale);

  // The hour, on the left.
  font::draw(*surface, kPaddingX, kTextTop, time_text, kScale, 0);

  // **The title sits in the box between the hour and the charge, and the box cuts it.** It is centred on
  // the bar and what hangs off that box is not painted, so a long title loses its ends instead of
  // running over the hour or under the charge: the border is what cuts a text, not the caller counting
  // margins (docs/30, docs/90). This is the bar's stand-in for a label until the control layer exists.
  if (g_title[0] != '\0') {
    const uint16_t title_w = font::width(g_title, kScale);
    const int32_t at = (static_cast<int32_t>(panel_w) - static_cast<int32_t>(title_w)) / 2;
    font::Clip box;
    box.x0 = static_cast<int32_t>(kPaddingX + time_w + kTitleGap);
    box.x1 =
        static_cast<int32_t>(panel_w) - static_cast<int32_t>(kPaddingX + charge_w + kTitleGap);
    box.y0 = static_cast<int32_t>(kTextTop);
    box.y1 = static_cast<int32_t>(kTextTop + font::kGlyphHeight * kScale);
    if (box.x1 > box.x0) {
      font::draw(*surface, static_cast<uint16_t>((at < 0) ? 0 : at), kTextTop, g_title, kScale, 0,
                 box);
    }
  }

  // The charge, on the right.
  if (charge_w + kPaddingX <= panel_w) {
    font::draw(*surface, static_cast<uint16_t>(panel_w - kPaddingX - charge_w), kTextTop,
               charge_text, kScale, 0);
  }

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
  g_title_changed = true;
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
  if (!clock_due && !battery_due && !g_title_changed) return;

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
  if (!g_title_changed && !minute_changed && !charge_changed) return;

  // **The quality of this repaint.** A digit that changed wants `du`, which is quick; a bar that has
  // been repainted ten times wants `gc16`, which is the one that takes the history away, and a new
  // title wants it too because the whole middle of the bar changes at once (docs/50).
  const bool clean = g_title_changed || (g_read.minute % kCleanEveryMinutes) == 0;
  paint(clean ? hal::UpdateMode::kGc16 : hal::UpdateMode::kDu, g_read);
}

}  // namespace bar
}  // namespace kernel
