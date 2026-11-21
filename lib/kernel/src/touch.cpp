// The glass: the frame of the last turn and the gestures made out of it (docs/30, docs/50).
#include "kernel/touch.h"

#include "kernel/led.h"
#include "kernel/lock.h"
#include "kernel/power.h"

#include <string.h>

#include "kernel/display.h"
#include "kernel/log.h"
#include "kernel/meter.h"
#include "kernel/button.h"
#include "kernel/cfg.h"
#include "kernel/orientation.h"
#include "kernel/ui.h"
#include "luabind/luabind.h"

// **Nothing of this file exists on a board without a glass** (0.179.9): this is the finger —a panel turned into
// taps, long presses and drags in the turns of the loop— and a board with no screen can never run a line of it.
// The question is asked of the board and not of a name in this file: `LUNOK_GLASS` is one line of its
// `board/identity.h`, and it is what decides which translation units exist at all. The four boards of the project
// without a panel —the two ATOMs, the bare ESP32 and the T-Koala— carry neither this code nor the libraries it
// pulls (docs/50, docs/75).
#include "board/identity.h"

#if LUNOK_GLASS

namespace kernel {
namespace touch {
namespace {

hal::ITouch* g_glass = nullptr;
hal::IClock* g_clock = nullptr;

// The frame of the last turn. It is kept because the chip gives a frame **once**: it hands it over
// and forgets it, so the kernel takes it in the turn and both the gestures and the app read this
// copy (docs/50).
hal::TouchPoint g_frame[kMaxPoints];
uint8_t g_count = 0;

// A finger that lands and leaves without moving is a `tap`, and **the four hundred milliseconds that used to be its
// ceiling are gone** (0.179.29): a press held for six hundred and lifted was neither a tap nor a `long`, so the app heard
// nothing at all while the control under it clicked. `held_ms` says how long it was, which is the number an app needs.
const int32_t kLongMs = 1000;
// How far a finger may move and still be the same press, in the chip's own units, which are pixels
// of the panel (docs/50).
const int32_t kSlopPx = 20;

bool g_down = false;
// **L'AMO DEL CONTACTE** (0.179.34): who the finger that is on the glass belongs to, decided on the frame it landed and
// not asked again. The three that exist are the page (its controls), the band of the keyboard, and the system —the strip of
// the bar and a card over the page, which deliver to nobody here because the panel and the bar read the queue—. It is what
// makes a swipe that ends on a button nobody's: the owner was decided where the finger began (docs/30, docs/90).
// **I un quart que no és ningú** (0.189.100): the contact that was on the glass when the page went away. What it would have
// said —a press and a release at the point it landed, or a gesture— belongs to a page that is not here any more, and the
// page that is here now has not seen a finger at all (docs/30, docs/50).
enum : uint8_t { kNoContact = 0, kThePage = 1, kTheKeyboard = 2, kTheSystem = 3, kNobodys = 4, kTheButton = 5 };
uint8_t g_contact = kNoContact;
// **I la pàgina nova encara no ha vist el vidre buit** (0.189.100): the chip scans by itself and **keeps the frame of the last
// scan that found somebody until a read takes it**, and the kernel is not turning while an app is read and compiled. A tap a
// person made over an empty glass —impatience, or the hand that was already there— is therefore handed over **after** the new
// page is on it, and the whole press and release of it is a click on the cell of the launcher that happens to be under the
// finger: an app that opens by itself. Until the glass has been seen with nobody on it, no contact read is this page's
// (docs/30, docs/50).
bool g_page_wants_an_empty_glass = false;
int32_t g_down_ms = 0;
uint16_t g_start_x = 0;
uint16_t g_start_y = 0;
// **The bar is not in the frame of an app** (0.179.22): a finger that lands on the strip of the system keeps its own y —the
// view's frame only subtracts the bar from the fingers that are below it—, so "above the view" is the one thing the turned
// frame cannot say and this flag is what the drag of the shade reads (docs/30).
bool g_on_the_bar = false;
bool g_start_on_the_bar = false;
uint16_t g_last_x = 0;
uint16_t g_last_y = 0;
bool g_moved = false;
bool g_long_said = false;

// Two fingers are a gesture of their own and they are tracked as a **pair**: the middle of the two
// and how far apart they are. A pinch is the change of that distance, and it is nothing the chip
// tells us —Goodix's own gestures are not read on this board— so it is arithmetic over two points
// (docs/30, docs/50).
bool g_pair = false;
int32_t g_pair_ms = 0;
int32_t g_pair_start_mid_x = 0;
int32_t g_pair_start_mid_y = 0;
int32_t g_pair_last_mid_x = 0;
int32_t g_pair_last_mid_y = 0;
int32_t g_pair_start_gap = 0;
int32_t g_pair_last_gap = 0;
// A pair can do two things, and both are said in pixels: it can move **together** (a drag of two
// fingers, a pan) or it can come **closer or further apart** (a pinch). Under this the pair did
// neither, and it was a two-finger tap, which `two` already said (docs/30).
const int32_t kPairPx = 15;

// Gestures wait their turn in a small queue and not in one slot: a tap is **held back** for the length
// of a double-tap window —it is not known to be a single tap until that window closes— and something
// else may happen while it waits (docs/30).
const uint8_t kQueueLength = 4;
Gesture g_queue[kQueueLength];
uint8_t g_queue_count = 0;

// A tap is counted and not said: two taps are a `double`, three are a `triple`, and until the window
// closes a tap is not known to be alone. **The cost is said here**: a `tap` arrives **350 ms after**
// the finger leaves. The other way —saying it at once and amending it later— would make every app undo
// what it already did with the first tap, and that is worse than a third of a second (docs/30).
const int32_t kMultiTapMs = 350;
uint8_t g_taps = 0;
int32_t g_last_tap_ms = 0;
uint16_t g_tap_x = 0;
uint16_t g_tap_y = 0;
int32_t g_tap_held_ms = 0;

int32_t now_ms() {
  return (g_clock != nullptr) ? static_cast<int32_t>(g_clock->monotonic_ms()) : 0;
}

// **Els tocs que esperen la finestra, dits d'una vegada** (0.179.29): it is the same sentence `settle_taps` says when the
// window closes, and `emit` asks for it as well when **another gesture** arrives, because a drag between two taps is not a
// double.
void say_the_taps();

void emit(const char* kind, uint16_t x, uint16_t y, int16_t dx, int16_t dy, int16_t spread,
          int32_t held_ms, uint8_t fingers) {
  // **Un altre gest tanca la finestra dels tocs** (0.179.29): a tap waits three hundred and fifty milliseconds to know
  // whether it is alone, and a person who does something else in that time has answered for it —a tap, a short drag and
  // another tap were said as a `drag` and a `double`, and the page heard two taps where there were two taps but also a
  // drag between them—. What was waiting goes in the queue **before** the gesture that closed the window, which is the
  // order it happened in (docs/30, docs/90).
  if (kind != nullptr && strcmp(kind, "tap") != 0 && strcmp(kind, "double") != 0 &&
      strcmp(kind, "triple") != 0) {
    say_the_taps();
  }
  if (g_queue_count == kQueueLength) {
    // Full: the oldest goes. Losing a gesture is bad; losing the **newest** is worse, because the last
    // thing a person did is the one they are waiting for (docs/30).
    for (uint8_t i = 1; i < kQueueLength; ++i) g_queue[i - 1] = g_queue[i];
    --g_queue_count;
  }
  Gesture& slot = g_queue[g_queue_count++];
  slot.kind = kind;
  slot.x = x;
  slot.y = y;
  slot.dx = dx;
  slot.dy = dy;
  slot.spread = spread;
  slot.held_ms = held_ms;
  slot.fingers = fingers;
  // **A gesture made by one finger also says where it landed** (0.179.22): a pair's middle is not on the bar, and the
  // gestures of one finger are the ones the shade is made of (docs/30).
  slot.on_the_bar = (fingers < 2) && g_start_on_the_bar;
  // **I cada gest diu què és, on i amb quants dits** (0.179.28): every gesture of this system is made here and nowhere
  // else, so this one line is the whole of what a person with the board in their hand cannot see —the chip gives points
  // and the names are the kernel's—. It is `kLogInfo` and not a debug line because a gesture is **one event and not a
  // frame**: a drag is said once, on the lift, and so is a tap when the window of the multi-tap closes, so a panel moved
  // all over costs a handful of lines and not one a turn (docs/30, docs/85).
  log_printf(pal::kLogInfo, "TOUCH", "%s %u,%u dx %d dy %d spread %d, %u finger(s), %d ms%s", kind,
             static_cast<unsigned>(x), static_cast<unsigned>(y), static_cast<int>(dx),
             static_cast<int>(dy), static_cast<int>(spread), static_cast<unsigned>(fingers),
             static_cast<int>(held_ms), slot.on_the_bar ? " on the bar" : "");
}

// A tap that is alone becomes a `tap`; two in a row inside the window are a `double` and three are a
// `triple`, which is said at the third one and does not wait for a fourth (docs/30).
void say_the_taps() {
  if (g_taps == 0) return;
  const char* kind = (g_taps >= 3) ? "triple" : ((g_taps == 2) ? "double" : "tap");
  g_taps = 0;
  emit(kind, g_tap_x, g_tap_y, 0, 0, 0, g_tap_held_ms, 1);
}

void settle_taps(int32_t now) {
  if (g_taps == 0) return;
  if ((now - g_last_tap_ms) <= kMultiTapMs) return;
  say_the_taps();
}

// The distance between two points, without floating point: the kernel has no maths library and this
// is the one root it needs. It is a Newton step from the bit length downward, which is exact for the
// numbers a glass of this size gives (docs/50).
uint32_t isqrt(uint32_t value) {
  if (value == 0) return 0;
  uint32_t x = value;
  uint32_t y = (x + 1) / 2;
  while (y < x) {
    x = y;
    y = (x + value / x) / 2;
  }
  return x;
}

// **The one place where the fingers are turned into the app's frame** (docs/90): the chip counts in one
// frame, an app draws in a view that starts under the bar of the system, and after this the frame the
// kernel keeps is in the view's coordinates.
//
// **The chip speaks in the page's own frame, and it is the same way round**: there is no quarter turn
// between the fingers and the glass and no mirror either. Measured on the board (1/11, docs/95 E-6) by
// touching the four corners of the portrait page —the top left is the clock— and reading what the
// chip said: 46,28 · 515,49 · 31,926 · 500,920. `x` grows to the right and `y` downwards, exactly
// as the page is drawn, so the only turn left is the one between the page the chip was measured in and
// the page the app asks for.
const uint16_t kChipPage = 270;

// **La zona del botó de sota el vidre** (0.190.110, l'amo: *«sota el panell hi ha un cercle que sembla ser un botó tàctil, i es fals... es al
// frontal, just sota la pantalla eink, centrat horitzontalment, com un botó d'iphone»*). The zone is the **bottom strip of the view**, centred:
// the circle is under the visible glass, so the finger that presses it lands on the last rows of the panel, i el que es mesura a la placa ho
// diu —un toc a `270,905` d'una vista de 540×912— (docs/30, docs/50).
//
// **S'estira i no s'apunta**: the circle is small and a thumb is not, so the strip is a hundred and twenty pixels wide and eighty tall —from
// `h - 80` to the last row— which is the smallest zone that a person can hit without looking; a zone of the size of the circle would be a
// button that works sometimes (docs/30).
const uint16_t kButtonHalfWidth = 60;
const uint16_t kButtonRows = 80;

bool a_finger_on_the_button(uint16_t x, uint16_t y) {
  if (button::software_button_key() < 0) return false;
  const ui::Canvas& view = ui::canvas();
  if (view.width == 0 || view.height == 0) return false;
  const uint16_t middle = static_cast<uint16_t>(view.width / 2);
  const uint16_t top = (view.height > kButtonRows) ? static_cast<uint16_t>(view.height - kButtonRows) : 0;
  const uint16_t left = (middle > kButtonHalfWidth) ? static_cast<uint16_t>(middle - kButtonHalfWidth) : 0;
  const uint16_t right = static_cast<uint16_t>(middle + kButtonHalfWidth);
  return x >= left && x <= right && y >= top;
}

void turn_frame() {
  if (g_count == 0) return;
  const orientation::Frame chip = {g_glass->width(), g_glass->height()};
  // **The frame the finger is clamped into is the canvas and not the page left over the keyboard** (0.84.0): `ui.height()`
  // is what an app draws in, and the band of the keyboard is still part of the glass —a finger that lands on it has to
  // arrive at the keyboard with its own y and not squeezed into the last row of the page— (docs/50, docs/90).
  const orientation::Frame view = {ui::canvas().width, ui::canvas().height};
  const uint16_t off_x = ui::canvas().x;
  const uint16_t off_y = ui::canvas().y;
  // Turning the page by 180 degrees turns the finger by 180 degrees too; at `kChipPage` the page and the
  // chip are the same frame and the turn is nothing.
  const uint16_t page_turn =
      static_cast<uint16_t>((kChipPage + 360 - (display::rotation() % 360)) % 360);
  for (uint8_t i = 0; i < g_count; ++i) {
    orientation::Frame turned;
    orientation::Point page = {g_frame[i].x, g_frame[i].y};
    const orientation::Point raw = page;
    const bool placed = orientation::turn(raw, chip, page_turn, turned, page);
    if (placed) {
      // **A finger above the view landed on the bar of the system** (0.179.22): it keeps its own y instead of being moved
      // into the view —the bar is not part of any app— and this is the one place that can tell it apart from a finger on the
      // first rows of the page (docs/30).
      const bool landed = (i == 0) && (page.y < off_y);
      if (i == 0) g_on_the_bar = landed;
      // **And the finger that landed there keeps the bar's rows for the whole gesture**: subtracting the offset from the
      // frames that follow but not from the one it landed on would make a pull of a hundred pixels a pull of fifty-two on a
      // bar of forty-eight, and a finger that never left the strip would be a finger that went up (docs/30).
      const bool on_the_strip = (i == 0) && (g_down ? g_start_on_the_bar : landed);
      if (page.x >= off_x) page.x = static_cast<uint16_t>(page.x - off_x);
      if (page.y >= off_y && !on_the_strip) page.y = static_cast<uint16_t>(page.y - off_y);
      // A finger that lands on the bar belongs to the system, but it is still a finger: it is clamped to
      // the edge of the view instead of vanishing. **A point is never dropped for not fitting what this
      // code expects** — dropping the ones that fell outside was a touch that answered nothing at all
      // (measured on the board, 1/11).
      if (view.width > 0 && page.x >= view.width) page.x = static_cast<uint16_t>(view.width - 1);
      if (view.height > 0 && page.y >= view.height) page.y = static_cast<uint16_t>(view.height - 1);
    }
    g_frame[i].x = page.x;
    g_frame[i].y = page.y;
  }
}

// What the interpreter asked for: **the frame of the view of the app**, which is the frame the fingers
// are given in. The chip counts in its own frame and that one is not an app's business (docs/30, docs/90).
bool caps_for_lua(luabind::TouchInfo& out) {
  if (g_glass == nullptr) return false;
  out.max_points = g_glass->max_points();
  out.width = ui::width();
  out.height = ui::height();
  return true;
}

uint8_t points_for_lua(luabind::TouchPointInfo* out, uint8_t capacity) {
  if (out == nullptr) return 0;
  const uint8_t count = (g_count < capacity) ? g_count : capacity;
  for (uint8_t i = 0; i < count; ++i) {
    out[i].id = g_frame[i].id;
    out[i].x = g_frame[i].x;
    out[i].y = g_frame[i].y;
    out[i].size = g_frame[i].size;
  }
  return count;
}

bool gesture_for_lua(const char** kind, uint16_t& x, uint16_t& y, int16_t& dx, int16_t& dy,
                     int16_t& spread, int32_t& held_ms, uint8_t& fingers) {
  Gesture mine;
  if (!take_gesture(mine)) return false;
  *kind = mine.kind;
  x = mine.x;
  y = mine.y;
  dx = mine.dx;
  dy = mine.dy;
  spread = mine.spread;
  held_ms = mine.held_ms;
  fingers = mine.fingers;
  return true;
}

}  // namespace

// **A frame that is not there yet is not a finger that left.** The chip hands the frame of a scan over once and the
// driver takes it away, so a caller that asks faster than the glass reads "nobody" between two frames of the same
// finger. Reading those gaps as a lift turned one finger held down into a run of presses, releases and taps —a tap on
// a menu picked two options, and a held button let go of itself— (found on the emulator, 3/11). Only a glass that has said
// nothing for a whole scan is a finger that went away; a scan of this panel is ten milliseconds and the window is
// three of them.
const int32_t kQuietMs = 30;
// When a frame was last handed over. `-1` is a glass that has said nothing since the app started or since the last
// finger left.
int32_t g_seen_ms = -1;
// **Quants marcs ha lliurat el vidre des que el dit va caure, i quan va caure** (0.188.8): the two numbers that tell a swipe
// the driver measured nothing from a glass that said nothing —and the owner asked the question in those words:
// *«segur que no estem perdent events del tàctil?»*— (docs/50, docs/95).
uint16_t g_contact_frames = 0;
int32_t g_contact_ms = 0;
// **El que la pàgina ha sentit de l'últim contacte** (0.189.0): filled once, when the finger leaves, and left as it is while no
// finger is down —so a page that asks reads the decision of the contact that just ended— (docs/30, docs/50).
LastContact g_last_contact;
// When the chip was last asked. `-1` is a glass nobody has asked yet, and the gate below is what keeps a turn of a
// millisecond from asking a chip that scans every ten (0.174.4, docs/50, docs/95).
int32_t g_asked_ms = -1;
// **El règim dels escombrats que hi ha posat** (0.190.107): the glass answers it, i aquesta còpia és el que contesta quan el vidre no hi és.
bool g_adaptive_scan = true;

// **The chip is asked once per scan and not once per turn** (0.174.4): the glass scans the panel by itself and keeps
// the frame of its last scan until a read takes it, so a turn that asks again inside the same scan can only be told
// what the last read already said. The emulator counts those reads as `missed` —and a model whose caller asks ten
// times a turn would make the cost of asking disappear, which is the opposite of what it is for— and on the board
// each one is the status of the chip on a hundred-kilohertz bus it shares with the clock, the FRAM and the sensor:
// 400 microseconds of a turn that can be one millisecond (docs/50, docs/95). A glass that does not say how often it
// scans (`scan_ms() == 0`) is asked on every turn, which is what every caller did before.
bool ask_the_chip(int32_t now) {
  const uint32_t scan = g_glass->scan_ms();
  if (scan == 0) return true;
  if (g_asked_ms >= 0 && static_cast<uint32_t>(now - g_asked_ms) < scan) return false;
  g_asked_ms = now;
  return true;
}

// **Quan s'acaba un contacte: quan el vidre diu que no hi ha dit, o quan ha callat tres escaneigs seus** (0.183.2). The
// window was thirty milliseconds flat, which was three scans of a chip that reports every ten —the number this build
// assumes for the GT911— and it is a window **of the glass that is there**: a panel that reports every twenty or thirty,
// with the same thirty assumed, splits one finger into a run of contacts, and a contact that ends under a finger is a
// **lift**: the row under it is clicked and the gesture that was a page never happens. `kQuietMs` stays as the floor for a
// glass that does not say how often it scans, and **the lift of the panel is not a timeout any more**: `finger_still_down`
// is the chip's own word about the frame it just handed over, so a frame with no points ends the contact in that turn
// (docs/30, docs/50, docs/90).
int32_t quiet_window_ms() {
  const uint32_t scan = (g_glass != nullptr) ? g_glass->scan_ms() : 0;
  if (scan == 0) return kQuietMs;
  const int32_t three_scans = static_cast<int32_t>(scan) * 3;
  return (three_scans > kQuietMs) ? three_scans : kQuietMs;
}

bool boot(hal::IPlatform& platform) {
  g_glass = platform.touch();
  g_clock = platform.clock();
  g_count = 0;
  g_down = false;
  g_contact = kNoContact;
  g_queue_count = 0;
  g_taps = 0;
  g_seen_ms = -1;
  g_asked_ms = -1;
  if (g_glass == nullptr) return false;
  // **I el règim que la persona ha triat** (0.190.107): the switch of the Input tab decides whether the glass drops its scans when nobody is
  // on it; el vidre ho aplica ara, i el son ja el configurarà a cada mode (docs/30, docs/50, docs/60).
  g_adaptive_scan = cfg::touch_adaptive();
  g_glass->set_adaptive_scan(g_adaptive_scan);
  luabind::set_touch_ops(luabind::TouchOps{caps_for_lua, points_for_lua, gesture_for_lua});
  return true;
}

bool available() { return g_glass != nullptr; }

// **El règim del vidre, dit i aplicat** (0.190.107): `cfg` el desa i aquesta porta el posa al controlador, que és l'únic que sap com canviar-lo
// (docs/30, docs/50).
void set_adaptive_scan(bool on) {
  g_adaptive_scan = on;
  if (g_glass != nullptr) g_glass->set_adaptive_scan(on);
}

bool adaptive_scan() { return g_adaptive_scan; }

namespace {
uint32_t g_asks = 0;
uint32_t g_frames = 0;
}  // namespace

void counters(uint32_t& asks, uint32_t& frames, uint32_t& false_warnings) {
  asks = g_asks;
  frames = g_frames;
  false_warnings = (g_glass != nullptr) ? g_glass->false_warnings() : 0;
}

// **I després d'una becaina, la línia del panell es torna a posar** (0.190.74): the chip arms that pin as a level wake and leaves it that way;
// the glass is asked to put its own falling edge back, which is what keeps a touch alive across a light sleep (docs/50, docs/95).
void rearm() {
  if (g_glass != nullptr) g_glass->rearm();
}

void poll() {
  if (g_glass == nullptr) return;
  const int32_t now = now_ms();
  // **A frame that did not come keeps the one that did**: the finger is still there until the glass has been quiet for
  // a whole scan (docs/30, docs/50). And the chip is asked at the rate it can have news and not at the rate the kernel
  // turns (0.174.4): what is measured as `touch.read` is the reads of the chip and not the turns that had nothing to
  // ask it (docs/30, docs/95).
  // **I el panell va al ritme de la llum** (0.190.119, l'amo: *«triga tant en respondre? sembla polling»*): with the backlight on somebody is
  // looking at the glass, so the chip scans at its top speed and a tap pays half a scan instead of a whole one; when the dim puts the light
  // out —or the board has no light at all— the floor of the panel is enough. El driver només escriu al xip quan el règim canvia (docs/30, docs/50).
  g_glass->set_brisk(led::glass_is_lit());
  uint8_t taken = 0;
  // **Un vidre que avisa es pregunta a cada torn** (0.188.0): a glass with its interrupt line wired answers a turn that has
  // nothing to hand over without touching the bus, so asking it is free and the frame is taken on the turn the line says it
  // is there —instead of up to a scan later, which is what the gate of 0.174.4 costs—. A glass that has to be asked pays the
  // status of the chip on every read, and that one is asked at the rate it can have news (docs/50, docs/95).
  if (g_glass->warns() || ask_the_chip(now)) {
    meter::Scope scope(meter::kTouchRead);
    ++g_asks;
    taken = g_glass->read(g_frame, kMaxPoints);
    if (taken > 0) ++g_frames;
  }
  if (taken > 0) {
    // **Un dit al vidre és una mà a la placa** (0.189.150): every frame with a contact on it starts the counter of the lock
    // again, perquè el que la persona fa amb el dit és el que la pàgina de bloqueig ha d'esperar, i un dit que s'hi està
    // quiet també és un dit (docs/30, docs/60).
    //
    // **I és la placa en ús, que és una altra cosa** (0.190.5, l'amo: *«sembla que s'adorm sola quasi d'inmediat... perqué?»*):
    // the idle policy measured "nobody is using it" by the console only, so a person who spent the policy's time touching the
    // glass —the panel of the system, a page of the launcher, the band of the keyboard— was a person the board could not see,
    // and the board powered off under the finger. **El comptador és un i el té `power`** (0.190.9): `lock::note_activity` ja el
    // mou, o sigui que aquí no s'ha de dir dues vegades (docs/30, docs/60).
    lock::note_activity(power::Hand::kGlass);
    // **Un dit damunt del vidre ho mana tot** (0.190.87, l'amo: *«Si el dit està tocant la pantalla i hi ha cpu scaling => 240mhz. En
    // quaslevol cas: mentre el dit toqui la pantalla també has de desactivar temporalment el LightSleep per evitar tenir problemes amb
    // les lectures fins que el dit aixequi»*). Un sol fet i dues respostes: **la velocitat al cim** —a finger on the glass is the one
    // moment this board has work that a person is waiting for: the frame has to reach a control, the control has to answer and the
    // page has to be painted, and at eighty megahertz that wait is the one a hand can feel— i **cap becaina** —a light sleep stops the
    // chip, the line of the panel and the scan with it, o sigui que una lectura perduda amb un dit a sobre és un toc que no arriba o
    // que arriba dos cops—. Passa per `power`, que és qui té les dues decisions, i **no és una finestra de rellotge**: el que ho lleva
    // és el dit quan s'aixeca, i amb l'escala apagada no mou la velocitat (docs/30, docs/50, docs/60).
    power::note_a_finger(true);
    // **A frame of a finger that is already gone is a tap that is over**, and it carries its own release: the chip
    // hands over the frame of its last scan and a finger that came and went between two reads is not on the glass any
    // more. The keyboard hears that release before the press, or the tap after one whose release was missed —while a
    // paint held the turns— lands on a finger that is still held and nobody types it (found on the emulator, 4/11,
    // docs/50, docs/90).
    if (!g_glass->finger_still_down()) ui::keyboard_touch(0, 0, false);
    g_count = taken;
    ++g_contact_frames;
    g_seen_ms = now;
    // **The turn is for the frame that just came and for nothing else**: `turn_frame` writes the view's coordinates over
    // the chip's in the same buffer, so turning a frame that is being kept turned it again on every poll and the finger
    // walked away on its own (found on the emulator, 3/11).
    turn_frame();
  } else if (g_seen_ms < 0 || !g_glass->finger_still_down() || (now - g_seen_ms) >= quiet_window_ms()) {
    g_count = 0;
    g_seen_ms = -1;
    // **I el dit s'ha aixecat** (0.190.87): the glass has been quiet for a whole scan, which is what "no finger" means here; the
    // frame of the release has already been handed over, o sigui que la becaina i el pas de repòs tornen a ser possibles
    // (docs/30, docs/50).
    power::note_a_finger(false);
  }
  // **QUI REP L'EVENT ES DECIDEIX EN COMENÇAR I NO ES TORNA A MIRAR** (0.179.34): a contact has **one owner** from its
  // first frame —the strip of the system, the band of the keyboard, or the page— and every frame that comes after goes to
  // that one. It is the rule the owner wrote down: *«l'event, tant gest com toc(s) l'ha de rebre l'objecte UI que hi ha
  // sota quan comença l'acció»*, and both of its halves are needed: a swipe that begins on the bar is the bar's, **and a
  // swipe that ENDS on a button is nobody's**, because the finger began somewhere else and a button that answers the end
  // of a gesture is a button acting on something it never heard (docs/30, docs/90).
  //
  // **The keyboard is why the owner is remembered and not asked again in every frame**: `keyboard_touch` is given the
  // point of the finger, so a drag that began on the page and crossed the band used to type the key it happened to pass
  // over —the band taking a contact that was never its own—. With one owner the band is asked **once**, with the point
  // the finger landed on (docs/90).
  // **I si aquest contacte ve d'una pàgina que ja no hi és, no és de ningú** (0.189.100): the finger that was on the glass
  // when the app changed is remembered as nobody's (`forget_the_page_before`) and it says nothing at all until the glass
  // says it is gone: no tap, no drag, no pair, and nothing in the queue of the app that came in its place.
  // **I la primera cosa que es llegeixi del vidre després canviar la pàgina no és d'aquesta** (0.189.100): an empty frame is
  // what says that the contact that comes after it began under this page; anything already on the glass —or already gone from
  // it— is what the page before left behind.
  if (g_page_wants_an_empty_glass) {
    if (g_count > 0) g_contact = kNobodys;
    g_page_wants_an_empty_glass = false;
  }
  const bool nobody_s_contact = (g_contact == kNobodys);
  if (g_count == 0) {
    // **El contacte s'ha acabat**: the owner hears the lift, and the next contact decides its own.
    if (g_contact != kNoContact) {
      // **Quants marcs ha vist aquest contacte** (0.188.8): the question *«segur que no estem perdent events del tàctil?»*
      // cannot be answered from a log that says the travel was zero: a contact of twelve hundred milliseconds over a glass
      // of twenty hertz is **twenty-four frames** and a contact of one is a lost line. The count is said here, with the time
      // it lasted, so a swipe that measured nothing says whether the glass was silent or the driver was (docs/50, docs/95).
      log_printf(pal::kLogDebug, "TOUCH", "the contact is over (%d was the owner), %u frames in %d ms",
                 static_cast<int>(g_contact), static_cast<unsigned>(g_contact_frames),
                 static_cast<int>(now_ms() - g_contact_ms));
    }
    if (g_contact == kTheKeyboard) ui::keyboard_touch(0, 0, false);
    // **El contacte d'una pàgina que ja no hi és es diu i no es lliura a ningú** (0.189.100): the numbers are the contact's,
    // so the log and the tests can see it, and `told` stays `kNothing` because the page that is here now has not heard a
    // finger of its own (docs/30, docs/50).
    if (nobody_s_contact) {
      g_last_contact = LastContact();
      g_last_contact.x = g_start_x;
      g_last_contact.y = g_start_y;
      g_last_contact.frames = static_cast<uint8_t>((g_contact_frames > 255) ? 255 : g_contact_frames);
      g_last_contact.ms = static_cast<uint16_t>((now_ms() - g_contact_ms > 65535) ? 65535 : (now_ms() - g_contact_ms));
    }
    // **I la pàgina que hi ha ara no rep el contacte d'una que ja no hi és** (0.189.100): the press of this point is the
    // click on the control of the page before, and delivered here it clicked the control of the new page that happened to
    // be under the same pixel —the launcher opening an app nobody asked for— (docs/30, docs/50).
    if (g_contact == kTheButton) {
      // **Què ha estat aquest contacte** (0.190.110): the walk and the time decide between a press —the system's back—, a `long` —the key saying
      // something to the app— and a swipe that happened to start there, which is nothing (docs/30, docs/90).
      const int32_t walked_x = static_cast<int32_t>(g_last_x) - static_cast<int32_t>(g_start_x);
      const int32_t walked_y = static_cast<int32_t>(g_last_y) - static_cast<int32_t>(g_start_y);
      const bool walked = (walked_x * walked_x + walked_y * walked_y) > (16 * 16);
      const uint32_t held = static_cast<uint32_t>(now_ms() - g_contact_ms);
      button::software_button_went(held, walked);
      log_printf(pal::kLogDebug, "TOUCH", "the button under the glass: %u ms, walked %d,%d", static_cast<unsigned>(held),
                 static_cast<int>(walked_x), static_cast<int>(walked_y));
      g_last_contact = LastContact();
      g_last_contact.x = g_start_x;
      g_last_contact.y = g_start_y;
      g_last_contact.frames = static_cast<uint8_t>((g_contact_frames > 255) ? 255 : g_contact_frames);
      g_last_contact.ms = static_cast<uint16_t>((held > 65535) ? 65535 : held);
    }
    if (g_contact == kThePage && !nobody_s_contact) {
      // **I el contacte es decideix aquí i només aquí** (0.189.0, l'amo: *«fes un mètode per retardar els events que s'envien
      // als UIControl. Si es decideix que és un gest no s'envien mai els events i s'envia el gest»*). The page has heard
      // **nothing** of this contact —no frame went to it— and it hears one thing now:
      //
      //  - **un toc**: the press and the release of the point the finger landed on, in the same turn, which is what a still
      //    finger of any length did before this version;
      //  - **un gest**: `ui_gesture` with the landing point and the travel of the whole contact, and **no press at all** —so a
      //    swipe paints nothing, focuses nothing and clicks nothing, which is what the row of a list was doing wrong—.
      //
      // A long press is a gesture here and not a tap: an app hears it at `touch.gesture()` as it always did, i **el control que
      // el vol el senta a `on_long`** (0.189.161) —el dit llarg sobre una cel·la del llançador és el que obre la fitxa d'aquella
      // app—, i un control que no el porta no fa res amb ell (docs/30, docs/50, docs/90).
      const int16_t walked_x = static_cast<int16_t>(static_cast<int32_t>(g_last_x) - static_cast<int32_t>(g_start_x));
      const int16_t walked_y = static_cast<int16_t>(static_cast<int32_t>(g_last_y) - static_cast<int32_t>(g_start_y));
      g_last_contact = LastContact();
      g_last_contact.x = g_start_x;
      g_last_contact.y = g_start_y;
      g_last_contact.frames = static_cast<uint8_t>((g_contact_frames > 255) ? 255 : g_contact_frames);
      g_last_contact.ms = static_cast<uint16_t>((now_ms() - g_contact_ms > 65535) ? 65535 : (now_ms() - g_contact_ms));
      if (!g_moved && !g_long_said) {
        g_last_contact.told = Told::kAPress;
        luabind::ui_touch_progress(g_start_x, g_start_y, true);
        luabind::ui_touch_progress(0, 0, false);
      } else {
        g_last_contact.told = Told::kAGesture;
        g_last_contact.dx = walked_x;
        g_last_contact.dy = walked_y;
        luabind::ui_gesture(g_start_x, g_start_y, walked_x, walked_y, g_long_said);
      }
    }
    // **El compte es buida quan el contacte s'ha acabat i no abans** (0.189.43): clearing it before the line above copied
    // it into `last_contact` made every contact say `frames 0` —the number the owner asked for to tell a lost line from a
    // still finger— (docs/50, docs/95).
    g_contact_frames = 0;
    g_contact = kNoContact;
  } else if (g_contact == kNoContact) {
    // **El contacte comença aquí, i el seu compte arrenca de zero** (0.188.8): the same two numbers, so the line at the end
    // says how many frames this contact was and how long it took. **The count is cleared when the last one ended and not
    // here**, because the first frame of a contact is counted before this branch is reached and clearing it now would take
    // that frame away from the account (docs/50, docs/95).
    g_contact_ms = now_ms();
    // **Un contacte nou vol dir que el dit de la banda de abans ja no hi és** (0.179.45): the band keeps `held_` between
    // frames so a finger that stays on a key types once, and a lift that the glass never reported —a frame lost while a
    // paint held the turns, which this panel does by the hundred— left it set for ever; from that moment the band answered
    // `true` to every finger and typed **none** of them, and the dial was refused with the same word (`held_`). A contact
    // that begins is a contact that is not the one before, so the band is told its finger is over before it is asked whose
    // this one is (docs/30, docs/90).
    ui::keyboard_touch(0, 0, false);
    // **El primer dit sobre un vidre apagat només l'encén** (0.190.118, l'amo: *«el backlight s'apaga i es torna a encendre quan toquen el panell»*).
    // El contacte és de ningú: ni la barra, ni la pàgina, ni el botó de sota el vidre el senten, perquè el que hi ha sota un vidre fosc no s'ha vist i
    // prémer a cegues no és prémer (docs/30, docs/50). **Sense `dimidle` això no passa mai**: el llum no era apagat i `wake_the_light` diu que no.
    if (led::wake_the_light()) {
      g_contact = kNobodys;
      log_printf(pal::kLogDebug, "TOUCH", "the contact at %u,%u lit the glass and clicks nothing",
                 static_cast<unsigned>(g_frame[0].x), static_cast<unsigned>(g_frame[0].y));
      return;
    }
    // **La primera passada del contacte, i el propietari es decideix aquí**: `on_the_bar` is the strip of the system,
    // which is not part of any view —the first rows of the bar and the first rows of the page are the same numbers—; then
    // the band of the keyboard, asked with the row the finger landed on, so a page under it cannot be pressed through it;
    // and everything else is the page. **A card of the system over the page** (0.179.22) is the system's too: a button
    // under a card that acts is a button acting on a page nobody is looking at (docs/30, docs/50).
    if (g_on_the_bar || ui::page_held()) {
      g_contact = kTheSystem;
      log_printf(pal::kLogDebug, "TOUCH", "the contact at %u,%u is the system's: bar %d, page held %d",
                 static_cast<unsigned>(g_frame[0].x), static_cast<unsigned>(g_frame[0].y), g_on_the_bar ? 1 : 0,
                 ui::page_held() ? 1 : 0);
    } else if (ui::keyboard_touch(g_frame[0].x, g_frame[0].y, true)) {
      g_contact = kTheKeyboard;
    } else if (a_finger_on_the_button(g_frame[0].x, g_frame[0].y)) {
      // **El botó de sota el vidre és una tecla i no una part de la pàgina** (0.190.110): the contact belongs to the system from its first frame,
      // o sigui que la pàgina no en rep ni el toc ni el gest —el que hi hagi dibuixat a sota del cercle no s'ha de prémer— i el botó actua quan
      // el dit s'aixeca (docs/30, docs/50).
      g_contact = kTheButton;
      // **Els números del contacte, presos aquí perquè el camí del gest no els prendrà** (0.190.110): the button's contact leaves `poll` before
      // the machine that records where a finger landed, o sigui que la primera i l'última posició es guarden amb el propietari (docs/30).
      g_start_x = g_frame[0].x;
      g_start_y = g_frame[0].y;
      g_last_x = g_start_x;
      g_last_y = g_start_y;
      button::software_button_held(true);
      log_printf(pal::kLogDebug, "TOUCH", "the contact at %u,%u is the button under the glass",
                 static_cast<unsigned>(g_frame[0].x), static_cast<unsigned>(g_frame[0].y));
    } else {
      g_contact = kThePage;
      // **I el gir amb què s'ha llegit, a la mateixa línia** (0.188.5): the page turn between the chip's frame and the page's
      // is the number that says whether a hand that goes sideways will be read as sideways, and the owner asked for the
      // sides only — *«recorda que la pantalla està girada»* —, so the number that decides it is said with the first contact
      // of a run instead of being guessed (docs/50, docs/95).
      log_printf(pal::kLogDebug, "TOUCH", "the contact at %u,%u is the page's (page turn %u)",
                 static_cast<unsigned>(g_frame[0].x), static_cast<unsigned>(g_frame[0].y),
                 static_cast<unsigned>((kChipPage + 360 - (display::rotation() % 360)) % 360));
    }
  } else if (g_contact == kThePage) {
    // **La pàgina no rep res mentre el contacte és viu** (0.189.0): the decision is taken when the finger leaves (see above),
    // so a frame that arrives here is kept by the gesture machine and told to nobody. This is what makes a swipe cost no ink
    // and a tap cost one press and one release (docs/30, docs/50).
  } else if (g_contact == kTheKeyboard) {
    ui::keyboard_touch(g_frame[0].x, g_frame[0].y, true);
  } else if (g_contact == kTheButton) {
    // **I on és ara el dit, que és el que decideix si ha caminat** (0.190.110): a finger that slides away from the circle is not a press.
    g_last_x = g_frame[0].x;
    g_last_y = g_frame[0].y;
  }
  // **El botó de sota el vidre tampoc no és cap gest** (0.190.110): while the contact is its, the machine of taps and pairs does not run —un
  // dit que es queda quiet sobre el cercle no ha de comptar tocs per a la pàgina, que ni tan sols el veu— (docs/30).
  if (g_contact == kTheButton) {
    // I si la tecla deixa d'existir a mig contacte (el commutador s'apaga), el dit es deixa anar i el contacte s'acaba en aquest torn.
    if (button::software_button_key() < 0) {
      button::software_button_held(false);
      g_contact = kNoContact;
    }
    return;
  }
  // `kTheSystem` goes to nobody here: the panel and the bar read the gestures of the queue and the page hears neither the
  // press nor the click (docs/30, docs/50).
  // **I el contacte d'una pàgina que ja no hi és tampoc** (0.189.100): the finger is not a tap, not a drag and not a pair
  // for the page that is here now, and the taps it was counting are not the beginning of a double of this one (docs/30).
  if (nobody_s_contact) return;

  // A tap waiting for its window: when the window closes it is a tap, and if another one arrived inside
  // it, it is a double (docs/30).
  settle_taps(now);

  // Two fingers are a gesture of their own, and while there are two of them the one-finger state
  // machine is **off**: a pinch is not a drag of the finger that happens to be listed first, and a
  // long press of a pair is not a long press (measured on the Paper, 1/11: a two-finger gesture came
  // out as a drag of the first finger, docs/50).
  if (g_count >= 2) {
    const int32_t mid_x = (static_cast<int32_t>(g_frame[0].x) + g_frame[1].x) / 2;
    const int32_t mid_y = (static_cast<int32_t>(g_frame[0].y) + g_frame[1].y) / 2;
    const int32_t gap_x = static_cast<int32_t>(g_frame[0].x) - static_cast<int32_t>(g_frame[1].x);
    const int32_t gap_y = static_cast<int32_t>(g_frame[0].y) - static_cast<int32_t>(g_frame[1].y);
    if (!g_pair) {
      g_pair = true;
      g_pair_ms = now;
      g_pair_start_mid_x = mid_x;
      g_pair_start_mid_y = mid_y;
      g_pair_start_gap = gap_x * gap_x + gap_y * gap_y;
      emit("two", static_cast<uint16_t>(mid_x), static_cast<uint16_t>(mid_y), 0, 0, 0, 0, g_count);
    }
    g_pair_last_mid_x = mid_x;
    g_pair_last_mid_y = mid_y;
    g_pair_last_gap = gap_x * gap_x + gap_y * gap_y;
    g_down = false;
    g_moved = false;
    return;
  }

  if (g_pair) {
    // The pair came apart, or went down to one finger. A pair has **two** things it can do and it is
    // named after the bigger one: move together is a `drag` of two fingers —a pan—, and come closer
    // or further apart is a `pinch`. **Both numbers go in the payload anyway**, so an app that wants
    // the whole story reads `dx`/`dy` and `spread` and not only the name. A pair that did neither
    // was a two-finger tap, which `two` already said (docs/30).
    g_pair = false;
    const int32_t spread = isqrt(g_pair_last_gap) - isqrt(g_pair_start_gap);
    const int32_t moved_x = g_pair_last_mid_x - g_pair_start_mid_x;
    const int32_t moved_y = g_pair_last_mid_y - g_pair_start_mid_y;
    const int32_t abs_spread = (spread < 0) ? -spread : spread;
    const int32_t abs_x = (moved_x < 0) ? -moved_x : moved_x;
    const int32_t abs_y = (moved_y < 0) ? -moved_y : moved_y;
    const int32_t moved = (abs_x > abs_y) ? abs_x : abs_y;
    const char* kind = nullptr;
    if (abs_spread >= kPairPx && abs_spread >= moved) {
      kind = "pinch";
    } else if (moved >= kPairPx) {
      kind = "drag";
    }
    if (kind != nullptr) {
      emit(kind, static_cast<uint16_t>(g_pair_start_mid_x),
           static_cast<uint16_t>(g_pair_start_mid_y), static_cast<int16_t>(moved_x),
           static_cast<int16_t>(moved_y), static_cast<int16_t>(spread), now - g_pair_ms, 2);
    }
  }

  if (g_count == 0) {
    if (!g_down) return;
    // The finger left: still is a tap, moved is a drag with the way it went (docs/30).
    const int32_t held = now - g_down_ms;
    if (g_moved) {
      emit("drag", g_start_x, g_start_y, static_cast<int16_t>(static_cast<int32_t>(g_last_x) -
                                                             static_cast<int32_t>(g_start_x)),
           static_cast<int16_t>(static_cast<int32_t>(g_last_y) -
                                static_cast<int32_t>(g_start_y)),
           0, held, 1);
    } else if (!g_long_said) {
      // **Un dit quiet que s'aixeca és un toc, encara que s'hagi pres amb calma** (0.179.29): the ceiling of four hundred
      // milliseconds this file had (`kTapMs`, now gone) was the definition of a tap, and a finger pressed for six hundred
      // and lifted without moving was **neither a tap nor a long**: the control under it clicked —because the controls work
      // on the frame and not on the name— and the app heard nothing at all, so a page of Lua that reads `touch.gesture()`
      // lost the press a person had made on purpose. `held_ms` already says how long it was, which is the number the app
      // needed. **A `long` is said while the finger is still there** and it is not a tap as well (docs/30, docs/90).
      // Counted and not said yet: see `settle_taps`. A third one is a `triple` at once, because
      // nothing longer than three exists and waiting would only add delay (docs/30).
      g_taps = (g_taps > 0 && (now - g_last_tap_ms) <= kMultiTapMs)
                   ? static_cast<uint8_t>(g_taps + 1)
                   : 1;
      g_last_tap_ms = now;
      g_tap_x = g_start_x;
      g_tap_y = g_start_y;
      g_tap_held_ms = held;
      if (g_taps >= 3) {
        emit("triple", g_tap_x, g_tap_y, 0, 0, 0, g_tap_held_ms, 1);
        g_taps = 0;
      }
    }
    g_down = false;
    g_moved = false;
    return;
  }

  if (!g_down) {
    g_down = true;
    g_down_ms = now;
    g_start_x = g_frame[0].x;
    g_start_y = g_frame[0].y;
    g_start_on_the_bar = g_on_the_bar;
    g_last_x = g_start_x;
    g_last_y = g_start_y;
    g_moved = false;
    g_long_said = false;
    return;
  }
  g_last_x = g_frame[0].x;
  g_last_y = g_frame[0].y;
  const int32_t dx = static_cast<int32_t>(g_last_x) - static_cast<int32_t>(g_start_x);
  const int32_t dy = static_cast<int32_t>(g_last_y) - static_cast<int32_t>(g_start_y);
  if (dx > kSlopPx || dx < -kSlopPx || dy > kSlopPx || dy < -kSlopPx) g_moved = true;
  if (!g_long_said && (now - g_down_ms) >= kLongMs) {
    g_long_said = true;
    emit("long", g_start_x, g_start_y, 0, 0, 0, now - g_down_ms, 1);
  }
}

// **El camí de tornada de `turn_frame`** (0.190.4): the offsets it takes out of the chip's frame are the view's and they go back on
// the point of a caller that draws on the glass. The bar is the one place where the subtraction had a branch and a finger that
// lands on it keeps its y, so the point of a finger on the bar comes back low by the bar's height —and the bar is not a control of
// any page, o sigui que cap botó no es pot polsar 48 píxels més avall del que es veu— (docs/30, docs/50).
void on_the_glass(uint16_t& x, uint16_t& y) {
  const uint16_t off_x = ui::canvas().x;
  const uint16_t off_y = ui::canvas().y;
  if (off_x > 0) x = static_cast<uint16_t>(x + off_x);
  if (off_y > 0) y = static_cast<uint16_t>(y + off_y);
}

uint8_t points(hal::TouchPoint* out, uint8_t capacity) {
  if (out == nullptr) return 0;
  const uint8_t count = (g_count < capacity) ? g_count : capacity;
  for (uint8_t i = 0; i < count; ++i) out[i] = g_frame[i];
  return count;
}

const LastContact& last_contact() { return g_last_contact; }

bool take_gesture(Gesture& out) {
  if (g_queue_count == 0) return false;
  out = g_queue[0];
  for (uint8_t i = 1; i < g_queue_count; ++i) g_queue[i - 1] = g_queue[i];
  --g_queue_count;
  return true;
}

bool take_gesture_of(const char* kind, Gesture& out) {
  if (kind == nullptr) return false;
  for (uint8_t i = 0; i < g_queue_count; ++i) {
    if (g_queue[i].kind == nullptr || strcmp(g_queue[i].kind, kind) != 0) continue;
    out = g_queue[i];
    // **The ones before it keep the order they had** (0.136.0): a gesture that a person made before the three taps is
    // still theirs, and taking the whole queue would throw away what the app has not read yet (docs/30).
    for (uint8_t j = static_cast<uint8_t>(i + 1); j < g_queue_count; ++j) g_queue[j - 1] = g_queue[j];
    --g_queue_count;
    return true;
  }
  return false;
}

bool take_drag_down_from_the_bar(int16_t min_px, Gesture& out) {
  if (min_px <= 0) return false;
  for (uint8_t i = 0; i < g_queue_count; ++i) {
    const Gesture& mine = g_queue[i];
    if (mine.kind == nullptr || strcmp(mine.kind, "drag") != 0) continue;
    if (!mine.on_the_bar) continue;
    // **Mostly down and not sideways**: a finger that comes down the bar but walks along the page is a page gesture that
    // began one row too high, and a shade that opens on it would be a shade that opens by accident (docs/30).
    const int32_t down = mine.dy;
    const int32_t sideways = (mine.dx < 0) ? -static_cast<int32_t>(mine.dx) : mine.dx;
    if (down < min_px || down < sideways) continue;
    out = mine;
    for (uint8_t j = static_cast<uint8_t>(i + 1); j < g_queue_count; ++j) g_queue[j - 1] = g_queue[j];
    --g_queue_count;
    return true;
  }
  return false;
}

void forget_gestures() { g_queue_count = 0; }

// **I tot el que la pàgina de abans no havia acabat de dir, quan n'arriba una de nova** (0.189.100, l'amo: *«en tornar al Home
// se m'han obert apps totes soles … és simplement que un event de la vista anterior arriba a la vista nova?»* — i ho és, per
// tres camins, i aquest els tanca tots tres):
//
//  - **el dit que hi ha al vidre**: the owner of a contact is decided on the frame it lands and the click of a control is
//    delivered when the finger leaves, **at the point it landed**. A finger that was on the glass when the app changed
//    clicked whatever control of the new page was under that pixel, which in a launcher is a cell and a cell starts an app;
//  - **el que la cua encara no havia dit**: a tap is said three hundred and fifty milliseconds after the finger leaves, so
//    the tap of the page before arrived to an app that did not exist when it happened —and the second tap of a double that
//    the page before was counting was not the first of a double of this one— (docs/30, docs/50);
//  - **el que el xip encara té a dins**: it scans by itself and keeps the frame of the last scan that found somebody until a
//    read takes it, and nothing is read while an app is loaded and compiled, so a tap over an empty glass is handed over
//    after the new page is on it —and it clicked the cell of the launcher under the finger—.
void forget_the_page_before() {
  g_queue_count = 0;
  g_taps = 0;
  g_tap_x = 0;
  g_tap_y = 0;
  g_tap_held_ms = 0;
  g_last_tap_ms = 0;
  // A pair of fingers that was pinching the page before has nothing to say to this one either.
  g_pair = false;
  // **I el dit que hi és és de ningú fins que s'aixequi**: it is not a press of the new page and it is not a tap —no tap, no
  // drag and nothing in the queue— the moment it lifts (`poll` reads `kNobodys` and returns before all of it).
  if (g_contact != kNoContact) g_contact = kNobodys;
  g_down = false;
  g_moved = false;
  g_long_said = true;
  // **I la pàgina nova ha de veure el vidre buit abans que res sigui seu** (0.189.100): everything the chip still has from
  // before the change —a tap that came and went over an empty glass, a finger that was already resting on it— is read after
  // the page is here, and none of it is this page's (docs/30, docs/50).
  g_page_wants_an_empty_glass = true;
}

bool take_pinch_in(int16_t min_px, Gesture& out) {
  if (min_px <= 0) return false;
  for (uint8_t i = 0; i < g_queue_count; ++i) {
    if (g_queue[i].kind == nullptr || strcmp(g_queue[i].kind, "pinch") != 0) continue;
    // **Big to small is the one that closes**: positive is apart —it is how every map and every photo app reads
    // it—, so the gesture that comes back is the one whose `spread` is under zero by `min_px` pixels. A pinch that
    // does not come back that far, or that opens, is left in the queue for the app (docs/30).
    if (g_queue[i].spread > static_cast<int16_t>(-min_px)) continue;
    out = g_queue[i];
    for (uint8_t j = static_cast<uint8_t>(i + 1); j < g_queue_count; ++j) g_queue[j - 1] = g_queue[j];
    --g_queue_count;
    return true;
  }
  return false;
}

}  // namespace touch
}  // namespace kernel

#endif  // LUNOK_GLASS
