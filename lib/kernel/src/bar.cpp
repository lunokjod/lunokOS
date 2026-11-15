// The strip of the system at the top of the glass: the hour, the title and the charge (docs/30).
#include "kernel/meter.h"
#include "kernel/bar.h"

#include <stdio.h>
#include <string.h>

#include "kernel/bar_icons.h"
#include "kernel/display.h"
#include "kernel/font.h"
#include "kernel/net.h"
#include "kernel/power.h"
#include "kernel/serial.h"

// **Nothing of this file exists on a board without a glass** (0.179.9): this is the bar of the system —the hour,
// the title of the app in front and the charge, on the top rows— and a board with no screen can never run a line
// of it. The question is asked of the board and not of a name in this file: `LUNOK_GLASS` is one line of its
// `board/identity.h`, and it is what decides which translation units exist at all. The four boards of the project
// without a panel —the two ATOMs, the bare ESP32 and the T-Koala— carry neither this code nor the libraries it
// pulls (docs/50, docs/75).
#include "board/identity.h"

#if LUNOK_GLASS

namespace kernel {
namespace bar {
namespace {

// **The height of the strip, and the size of its letters.** It was 28 rows —a glyph doubled to fourteen and
// air— and 28 rows with a doubled glyph is a bar that reads only from close up. It is **48 rows** now, and the
// letters are the ones the paper project gives its bar (docs/50, docs/92): **a line of 33 px** —its `fontSmall`—
// with six rows of air above, six below and the black line of two at the bottom, which is 6 + 33 + 7 + 2 = 48.
const uint16_t kBarHeight = 48;

// **`scale` 3 of the face of the system is a line of 33 px**, which is the line the paper project's bar has: `kOutlineLine`
// is eleven pixels and a title bar is not the place for the text of a page (docs/92).
const uint8_t kScale = 3;
// Where the text starts, above and to the left, leaving air at the edges.
const uint16_t kPaddingX = 12;
// The air between the hour, the title and the charge: two things that touch are two things nobody can
// tell apart (docs/30).
const uint16_t kTitleGap = 8;
// **L'aire entre les icones i la xifra** (0.189.56): the four icons and the battery figure come from the paper project at
// 40 × 28 and 46 × 26 —their own size, not scaled to a bar that is 48 rows— and they are separated by this, which is the
// `gap` the paper project's right column has.
const uint16_t kIconGap = 10;
// Where the icons sit: the middle of the strip above its line, and **the bar does not square them down**: 28 rows in 46
// (48 minus the two of the line) leaves nine of air above and below, which is what makes the drawing sit on the line of
// the text and not on the paper.
const uint16_t kIconTop = (48 - 2 - bar_icons::kIconH) / 2;
const uint16_t kFigureTop = (48 - 2 - bar_icons::kBatteryH) / 2;
const uint16_t kTextTop = 6;
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
  // **I si la placa s'està alimentant** (0.189.56): the bolt of the battery figure is drawn from this and the four bars are
  // not, which is the paper project's rule (docs/50).
  bool charging = false;
};

// **Els quatre estats de les icones** (0.189.56): the radio is on (the fan), a provisioning is running over Bluetooth (the
// rune), the cable is charging or an app has the port (the USB mark) and the clock is being set from the network (the
// hourglass). They are read every turn —they are four words of the kernel's own memory and not a device— and they are
// compared, because a bar that paints an icon that did not change is spent ink (docs/30, docs/50).
struct Signs {
  bool wifi = false;
  bool bluetooth = false;
  bool serial = false;
  bool ntp = false;

  bool operator==(const Signs& other) const {
    return wifi == other.wifi && bluetooth == other.bluetooth && serial == other.serial && ntp == other.ntp;
  }
  bool operator!=(const Signs& other) const { return !(*this == other); }
};

hal::IPlatform* g_platform = nullptr;
char g_title[48] = {0};
bool g_title_changed = true;
// **The glass under the bar changed** —a turn, a new size, the boot— and the whole strip is owed, not one box of it.
bool g_whole_changed = false;
bool g_drawn = false;
// **Whether the strip is on the glass** (0.136.0): false while an app has asked for the whole glass, and then no turn
// of the bar paints anything at all (docs/30, docs/46 D1).
bool g_visible = true;

// What is on the glass, and the last values read, which may be newer than it.
Status g_shown;
Status g_read;
// **Les icones que hi ha al vidre** (0.189.56): the four signs of the last paint, which is what the turn compares with the
// four of now to know whether an icon has to go up or down (docs/50).
Signs g_shown_signs;
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
    status.charging = charging;
  }
  return status;
}

// **Els quatre estats, llegits del nucli** (0.189.56): the fan says the radio is on —whatever it is doing—, the rune says a
// provisioning is running over Bluetooth, the mark of the port says there is a cable (the board is being fed or an app has
// the wire) and the hourglass says the clock is being set from the network. They are four words of the kernel's own memory
// and not a device, so asking them every turn costs nothing (docs/30, docs/50).
Signs read_signs(const Status& status) {
  Signs signs;
  // **El ventall diu que la ràdio és encesa, sigui què estigui fent** (the paper project's rule): a scan, an association and
  // a page being downloaded are all the radio on, and the state of the station alone would leave the fan dark during a scan
  // (docs/30, docs/50).
  signs.wifi = net::available() && (net::state() != 0 || net::busy());
  signs.bluetooth = net::over_bluetooth();
  signs.serial = serial::claimed() || status.charging;
  signs.ntp = net::syncing_clock();
  return signs;
}

// **El nivell de la figura, de zero a quatre barres**, arrodonit al quart més proper: the paper project's `batteryLevelOf`,
// and the number comes from the same reading as the percentage beside it (docs/50).
int battery_level_of(int percent) {
  if (percent < 0) return 0;
  const int level = (percent * 4 + 50) / 100;
  return (level > 4) ? 4 : level;
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
//
// **I el dibuix és de dos nivells** (0.179.40): the paper of the strip, the line under it and every run of a letter are
// `fill_rect` of level fifteen or level zero —`font::draw` puts one level down and not four shades—, which is what lets
// the wash that does not blink land **on** the level instead of close to it (docs/50).
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
  // **I les cinc caixes de la dreta** (0.189.56): the battery figure against the margin and, to its left —in the paper
  // project's order, from the margin inwards: the figure, the number, the fan, the port, the rune and the hourglass— the
  // four icons. **Totes hi són sempre encara que no es pintin**: a box that appears and disappears moves the title and the
  // number at every change, and a bar whose middle jumps is worse than a bar with an empty box (docs/50).
  Box figure;
  Box icons[4];
};

Layout layout_of(uint16_t panel_w) {
  // **The height of a line is the height of the face** (0.96.0): a face of outlines says eight pixels of body for every
  // step of `scale` and the table of the flash says seven, so a bar that counted seven would draw its text four pixels
  // past the box it just cleaned (docs/30, docs/50).
  const uint16_t glyph_h = static_cast<uint16_t>(font::system_face().height * kScale);
  const uint16_t time_w = font::width("00:00", kScale);
  const uint16_t charge_w = font::width("100%", kScale);
  Layout out = {};
  out.panel_w = panel_w;
  out.time = Box{kPaddingX, kTextTop, time_w, glyph_h};
  const int32_t title_left = static_cast<int32_t>(kPaddingX + time_w + kTitleGap);
  // **La dreta del full, de marge cap endins** (0.189.56): the figure of the battery against the margin —it is a readout with
  // a number beside it— and then the number, the fan, the port, the rune and the hourglass. **A box that does not fit a
  // narrow glass is left out from the left**: the hourglass is the first to go, and the figure and the number, which are
  // what a person looks at, are the last.
  int32_t right = static_cast<int32_t>(panel_w) - kPaddingX;
  const int32_t figure_x = right - static_cast<int32_t>(bar_icons::kBatteryW);
  out.figure = Box{static_cast<uint16_t>((figure_x > 0) ? figure_x : 0), kFigureTop, bar_icons::kBatteryW,
                   bar_icons::kBatteryH};
  if (figure_x > 0) right = figure_x;
  const int32_t charge_x = right - kIconGap - charge_w;
  out.charge = Box{static_cast<uint16_t>((charge_x > 0) ? charge_x : 0), kTextTop, charge_w, glyph_h};
  if (charge_x > 0) right = charge_x;
  // **Tres caixes i no quatre** (0.189.56): the paper project's column has an hourglass too —the clock being set from the
  // network— and it is drawn in `bar_icons`, but **a bar of 540 px cannot hold four boxes of 40 and a title**: the four cost
  // 200 px of the 540 and this strip also writes the hour on the left and the charge on the right, which leaves a title of
  // 73 px —«Ho», «Tim»— where the box of the hourglass alone is 50 of those. The hour is already written at the left of this
  // bar, so the hourglass is the one that waits for a bar with more room (docs/50).
  static const bar_icons::Kind kFromRight[3] = {bar_icons::kWifi, bar_icons::kSerial, bar_icons::kBluetooth};
  for (uint8_t i = 0; i < 3; ++i) {
    const int32_t icon_x = right - kIconGap - static_cast<int32_t>(bar_icons::kIconW);
    const int32_t floor_x = static_cast<int32_t>(kPaddingX + time_w);
    if (icon_x > floor_x) {
      out.icons[kFromRight[i]] = Box{static_cast<uint16_t>(icon_x), kIconTop, bar_icons::kIconW, bar_icons::kIconH};
      right = icon_x;
    } else {
      out.icons[kFromRight[i]] = Box{0, 0, 0, 0};
    }
  }
  const int32_t title_right = right - kTitleGap;
  out.title = Box{static_cast<uint16_t>(title_left), kTextTop,
                  static_cast<uint16_t>((title_right > title_left) ? (title_right - title_left) : 0), glyph_h};
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
  // **El títol es centra a la SEVA caixa i no al vidre** (0.189.56): with the four icons of the paper project on the right the
  // box between the hour and them is not the middle of the glass, and a title centred on the glass landed under the fan and
  // was cut whole —an empty middle is what the first dump of the port showed—. The box is the room the title has, so it is
  // the box the title is centred in.
  const int32_t centred = static_cast<int32_t>(at.title.x) +
                          (static_cast<int32_t>(at.title.w) - static_cast<int32_t>(title_w)) / 2;
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
  // **I la xifra s'arrepenja al final de la seva caixa**, que des de la 0.189.56 és on comença la figura de la bateria i no el
  // marge del vidre: a number against the margin would be painted over the figure (the first dump of the port cut the `98%`
  // down to an `8`).
  const int32_t x = static_cast<int32_t>(at.charge.x + at.charge.w) - static_cast<int32_t>(text_w);
  if (x >= static_cast<int32_t>(at.charge.x)) {
    font::draw(surface, static_cast<uint16_t>(x), at.charge.y, text, kScale, 0);
  }
}

// **Les icones, cada una a la seva caixa i només si diu alguna cosa** (0.189.56): a box that is cleaned and left empty is the
// icon that is not lit, which is how a bar of two tones says "this one is off" —an e-ink glass has no grey to dim it with—,
// and the box is in the layout whether the icon is or not, so the title and the number do not move when one comes or goes.
void draw_icons(hal::ISurface& surface, const Layout& at, const Signs& signs) {
  static const bar_icons::Kind kKinds[4] = {bar_icons::kWifi, bar_icons::kSerial, bar_icons::kBluetooth,
                                            bar_icons::kNtp};
  const bool lit[4] = {signs.wifi, signs.serial, signs.bluetooth, signs.ntp};
  for (uint8_t i = 0; i < 4; ++i) {
    const Box& box = at.icons[kKinds[i]];
    if (box.w == 0) continue;
    surface.fill_rect(box.x, box.y, box.w, box.h, 15);
    if (!lit[i]) continue;
    bar_icons::glyph(surface, kKinds[i], box.x, box.y, 0);
  }
}

// **La figura de la bateria**, que és una lectura i no un estat: it is drawn whenever the board has a gauge, with its four
// bars or with the bolt of a board that is being fed (0.189.56, docs/50).
void draw_figure(hal::ISurface& surface, const Layout& at, const Status& status) {
  const Box& box = at.figure;
  if (box.w == 0) return;
  surface.fill_rect(box.x, box.y, box.w, box.h, 15);
  if (!status.has_battery) return;
  bar_icons::battery(surface, box.x, box.y, battery_level_of(status.percent), status.charging, 0);
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
  const Signs signs = read_signs(status);
  draw_time(*surface, at, status);
  draw_title(*surface, at);
  draw_charge(*surface, at, status);
  draw_figure(*surface, at, status);
  draw_icons(*surface, at, signs);
  display::present(mode);
  g_shown = status;
  g_shown_signs = signs;
  g_title_changed = false;
  g_whole_changed = false;
  g_drawn = true;
}

// **A box of the strip and not the strip** (0.85.2): a title that changes is the middle of the bar, and the hour is the
// hour. The quality is **`gld16`, which cleans and does not blink**, and not `gc16`, which is two blinks over the whole
// strip for a word that changed; `du` is not used at all, because it paints and does not clean and a digit of a minute
// ago would stay on the glass. It is the rule the project already had written down: nothing that flickers reaches the
// glass, and cleaning is `gld16` (docs/50, docs/92, docs/95).
void paint_boxes(hal::UpdateMode mode, const Status& status, bool time, bool title, bool charge, bool signs_dirty) {
  hal::ISurface* surface = display::surface();
  if (surface == nullptr) return;
  const Layout at = layout_of(surface->width());
  const Signs signs = read_signs(status);
  if (time) draw_time(*surface, at, status);
  if (title) draw_title(*surface, at);
  if (charge) {
    draw_charge(*surface, at, status);
    draw_figure(*surface, at, status);
  }
  if (signs_dirty) draw_icons(*surface, at, signs);
  display::present(mode);
  g_shown = status;
  g_shown_signs = signs;
  g_title_changed = false;
  g_drawn = true;
}

}  // namespace

uint16_t height() { return kBarHeight; }

bool boot(hal::IPlatform& platform) {
  if (!display::available()) return false;
  g_platform = &platform;
  g_title[0] = '\0';
  // **A boot is a bar and no app**: the strip is on the glass again whatever the app of before asked for (0.136.0).
  g_visible = true;
  // The first paint of the bar is **`gld16` and not `init`**: `init` is the two-second beginning of the
  // glass, and while it runs the controller holds its busy line low for two whole seconds — which is
  // the ceiling of the driver, so an app that paints right after the boot found the panel refusing
  // (measured on the board, 1/11). **I no és `gc16`** (0.179.40): two blinks of the whole strip to erase what the picture
  // of the boot left under it is a flash a person sees every time the board comes up, and the strip is two tones, so the
  // wash that keeps two sixteenths lands on the level (docs/50, docs/92).
  g_read = read_status(true, true);
  paint(hal::UpdateMode::kGld16, g_read);
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
  // The glass changed under the bar —a turn, a new size, a picture that washed it— and what it has on it is not the bar
  // any more: it is painted again whole. **Sense parpelleig** (0.179.40): the strip is two tones, so the whole of it goes
  // up at `gld16`, the wash that keeps two sixteenths and does not blink, and what the page under it left is a level
  // thirteen and not a black band (docs/50, docs/92).
  g_whole_changed = true;
}

// **Lo que la barra deu, pintat aquí i no al final del torn** (0.179.27): the same two decisions `poll()` makes, taken
// without reading the clock, because a start that is waiting for its title does not want a read of the RTC on top. A
// strip whose glass changed under it is painted whole; a strip that only changed its title is painted in the box of the
// title, at the wash of two levels —the hour and the charge are not touched and the strip does not blink (docs/50).
bool flush() {
  if (!g_drawn || g_platform == nullptr || !g_visible) return false;
  if (!g_title_changed && !g_whole_changed) return false;
  if (g_whole_changed) {
    paint(hal::UpdateMode::kGld16, g_read);
    return true;
  }
  paint_boxes(hal::UpdateMode::kGld16, g_read, false, g_title_changed, false, false);
  return true;
}

void show(bool on) {
  if (g_visible == on) return;
  g_visible = on;
  // **A strip that comes back is a strip owed whole** (0.136.0): while it was away the glass under it was somebody
  // else's page and it has its own ink, so what is on it now is not the bar (docs/50, docs/46 D1).
  if (on) g_whole_changed = true;
}

bool visible() { return g_visible; }

void poll() {
  if (!g_drawn || g_platform == nullptr || !g_visible) return;
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
  // **I les quatre icones, que poden canviar en qualsevol torn** (0.189.56): the fan goes up when the radio comes on, the
  // rune while a provisioning runs, the mark of the port when the cable is in and the hourglass while the clock is being
  // set. The four are words of the kernel's memory, so they are asked every turn and compared —what is not compared is an
  // icon that stays on the glass after the thing it says is over (docs/50).
  const Signs signs = read_signs(g_read);
  const bool signs_changed = (signs != g_shown_signs);
  // **A bar with nothing new paints nothing.** Not on a turn, not every five seconds, not at all.
  if (!g_whole_changed && !g_title_changed && !minute_changed && !charge_changed && !signs_changed) return;

  // **The strip is whole when the glass under it moved**, and only then: a page that turned took the bar with it and
  // there is no box to clean, there is a strip to draw. **I es pinta sense parpelleig** (0.179.40): `gld16` over the two
  // tones of the strip, which is the same wash the boxes use (docs/50).
  if (g_whole_changed) {
    paint(hal::UpdateMode::kGld16, g_read);
    return;
  }
  // **And otherwise the boxes that changed, at the wash of two levels** (0.85.2): the hour is a digit and the title is
  // a word, and neither of them is worth two blinks of the whole strip (docs/50, docs/85).
  paint_boxes(hal::UpdateMode::kGld16, g_read, minute_changed, g_title_changed, charge_changed, signs_changed);
}

}  // namespace bar
}  // namespace kernel

#endif  // LUNOK_GLASS
