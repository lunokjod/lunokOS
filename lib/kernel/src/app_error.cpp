// **La pàgina d'una app que s'ha mort** (0.189.162, l'amo: *«Si peta una app, que menys que saber el perqué amb la pantalla i
// els dits. Un modal amb el missatge d'error? vull detalls del perqué una app peta»*).
//
// The error of an app used to be a line of the log and the console, and a person with the glass in front of them saw the page
// under it —the launcher, the prompt— as if nothing had happened. This file paints the why: the glass washed whole, the name of
// the app, the message the interpreter left, the traceback of where it happened, and one line at the bottom that says how to go
// on. **The drawing is the system's own**: whatever `hal::ISurface` and the face of the system give, the same two things the bar
// and the splash use, so a board with no disk still shows its error (docs/30, docs/50).
//
// **I el que la pàgina espera**: a finger on the glass, a key, or the seconds she waits alone —a board with nobody in front of it
// must not stay on the error forever—, and while she waits the touch panel and the queue keep being read, or the finger would
// arrive to a system that is not looking (docs/30, docs/50, docs/90).
#include "board/identity.h"

#if LUNOK_GLASS

#include "kernel/app_error.h"

#include <stdio.h>
#include <string.h>

#include "hal/hal.h"
#include "kernel/apps.h"
#include "kernel/button.h"
#include "kernel/display.h"
#include "kernel/font.h"
#include "kernel/log.h"
#include "kernel/touch.h"
#include "pal/pal.h"

namespace kernel {
namespace app_error {
namespace {

// The air between the frame and the words, and the rows the frame is thick.
const uint16_t kMargin = 16;
const uint16_t kFrame = 2;
// **Els segons que la pàgina espera sola**: a person reading a traceback needs more than a glance, and a board with nobody in
// front of it —a shelf, a test— must go on by itself. A case of the suite asks for a shorter one (docs/80).
const int64_t kWaitMs = 30000;
int64_t g_wait_ms = kWaitMs;
// What one line of the message may say before it is cut: a line of a traceback is long, and a page that runs off the glass says
// nothing at all (docs/30).
const uint16_t kLineBytes = 96;
const uint8_t kLines = 24;

// **El text en línies, dites a un destí** (0.189.162): the page splits the message by the same rules whether it is counting the
// lines or painting them, and it keeps none of them —the board with the least RAM of this project does not have the 2.3 kB a table
// of twenty-four lines costs, and the link of the Paper said so (docs/50, docs/90)—.
class Lines {
 public:
  virtual ~Lines() {}
  // Torna fals quan ja no en vol cap més i el passeig s'atura.
  virtual bool take(const char* line) = 0;
};

int64_t since(int64_t began) { return static_cast<int64_t>(pal::system().millis()) - began; }

// **Una línia partida per l'amplada de la caixa**, word by word: the rule a label of this system follows, done here because a
// surface has no labels (docs/30, docs/90).
bool add_line(const char* text, size_t len, uint16_t width, uint8_t scale, Lines& out) {
  if (len == 0) return true;
  char line[kLineBytes];
  if (len >= sizeof(line)) len = sizeof(line) - 1;
  memcpy(line, text, len);
  line[len] = '\0';

  // A line that fits is a line: the measuring is the face's and not an estimate of characters.
  if (font::width(line, scale) <= width) return out.take(line);

  // **I la que no hi cap es parteix pels espais**: a word wider than the box on its own is cut where it has to be, because a
  // traceback carries paths and names with no spaces in them (docs/30).
  size_t from = 0;
  while (from < len) {
    size_t take = len - from;
    while (take > 1) {
      memcpy(line, text + from, take);
      line[take] = '\0';
      if (font::width(line, scale) <= width) break;
      size_t cut = take;
      while (cut > 1 && text[from + cut - 1] != ' ') --cut;
      take = (cut > 1) ? (cut - 1) : (take / 2);
    }
    if (take == 0) break;
    memcpy(line, text + from, take);
    line[take] = '\0';
    if (!out.take(line)) return false;
    from += take;
    while (from < len && text[from] == ' ') ++from;
  }
  return true;
}

// **El text, en les línies de la pàgina**: the newlines of the traceback are the paragraphs and the width is the box.
bool walk_text(const char* text, uint16_t width, uint8_t scale, Lines& out) {
  if (text == nullptr || text[0] == '\0') return out.take("(the interpreter said nothing)");
  const char* at = text;
  while (*at != '\0') {
    const char* end = at;
    while (*end != '\0' && *end != '\n' && *end != '\r') ++end;
    if (!add_line(at, static_cast<size_t>(end - at), width, scale, out)) return false;
    at = end;
    while (*at == '\n' || *at == '\r') ++at;
  }
  return true;
}

// Quantes línies vol el text, aturades a les que la pàgina accepta.
class Count : public Lines {
 public:
  explicit Count(uint8_t stop_at) : limit(stop_at), lines(0) {}
  bool take(const char*) override {
    ++lines;
    return lines < limit;
  }
  uint8_t limit;
  uint8_t lines;
};

// I les que es pinten, amb el que no hi cap dit a l'última.
class Paint : public Lines {
 public:
  Paint(hal::ISurface& surface, uint16_t left, uint16_t& y, uint8_t scale, uint8_t take, uint8_t wanted)
      : surface_(surface), left_(left), y_(y), scale_(scale), take_(take), wanted_(wanted), drawn_(0) {}
  bool take(const char* line) override {
    if (drawn_ + 1 == take_ && wanted_ > take_) {
      font::draw(surface_, left_, y_, "...", scale_, 0);
      ++drawn_;
      return false;
    }
    font::draw(surface_, left_, y_, line, scale_, 0);
    y_ = static_cast<uint16_t>(y_ + font::system_face().height * scale_ + 2);
    ++drawn_;
    return drawn_ < take_;
  }

 private:
  hal::ISurface& surface_;
  uint16_t left_;
  uint16_t& y_;
  uint8_t scale_;
  uint8_t take_;
  uint8_t wanted_;
  uint8_t drawn_;
};

// **El vidre sencer, rentat i amb la targeta a sobre**: a wash of the whole panel so the ghost of the page that died goes with it,
// a frame, the title and the lines.
void paint(hal::ISurface& surface, const char* id, const char* title, const char* text, uint8_t scale,
           uint8_t title_scale) {
  surface.clear(15);
  const uint16_t width = surface.width();
  const uint16_t height = surface.height();
  // The frame: four rectangles, which is all a surface has (docs/50).
  surface.fill_rect(kMargin, kMargin, static_cast<uint16_t>(width - 2 * kMargin), kFrame, 0);
  surface.fill_rect(kMargin, static_cast<uint16_t>(height - kMargin - kFrame),
                    static_cast<uint16_t>(width - 2 * kMargin), kFrame, 0);
  surface.fill_rect(kMargin, kMargin, kFrame, static_cast<uint16_t>(height - 2 * kMargin), 0);
  surface.fill_rect(static_cast<uint16_t>(width - kMargin - kFrame), kMargin, kFrame,
                    static_cast<uint16_t>(height - 2 * kMargin), 0);

  const uint16_t left = static_cast<uint16_t>(kMargin + 12);
  const uint16_t line_h = static_cast<uint16_t>(font::system_face().height * scale);
  uint16_t y = static_cast<uint16_t>(kMargin + 14);

  // **El títol, dues vegades i un píxel a la dreta** (docs/30): the face of the system has one weight and this is the bold of the
  // pages, the same trick a book's bold uses.
  const char* heading = "An application has stopped";
  font::draw(surface, left, y, heading, title_scale, 0);
  font::draw(surface, static_cast<uint16_t>(left + 1), y, heading, title_scale, 0);
  y = static_cast<uint16_t>(y + font::system_face().height * title_scale + 10);

  // The app: its name and its title, which is what says which page is not there any more.
  char who[kLineBytes];
  snprintf(who, sizeof(who), "%s (%s)", id, (title != nullptr && title[0] != '\0') ? title : "no title");
  font::draw(surface, left, y, who, scale, 0);
  y = static_cast<uint16_t>(y + line_h + 10);

  // **I les línies del perquè**, tallades a la caixa i amb el que no hi cap dit: a page that ends in a full stop nobody asked for
  // is a page that hides the end of the traceback (docs/30, docs/90).
  const uint8_t room = static_cast<uint8_t>((height - kMargin - 12 - y - 2 * line_h) / (line_h + 2));
  Count counter(kLines);
  walk_text(text, static_cast<uint16_t>(width - 2 * (kMargin + 12)), scale, counter);
  const uint8_t take = (counter.lines < room) ? counter.lines : room;
  if (take > 0) {
    Paint painter(surface, left, y, scale, take, counter.lines);
    walk_text(text, static_cast<uint16_t>(width - 2 * (kMargin + 12)), scale, painter);
  }

  // **Com continuar**: the one thing a page of an error owes the person in front of it.
  const char* how = "Tap the glass or press a key";
  const uint16_t how_w = font::width(how, scale);
  font::draw(surface, static_cast<uint16_t>((width - how_w) / 2),
             static_cast<uint16_t>(height - kMargin - 10 - line_h), how, scale, 0);
}

bool any_key_down() {
  const uint32_t keys = button::count();
  for (uint32_t i = 0; i < keys; ++i) {
    if (button::down(i)) return true;
  }
  return false;
}

// **El que s'espera**: a finger, a key, or the seconds of the page. The panel and the queue are read here because the kernel's
// turn is the one that is stopped —the app that died was inside it— so nobody else is going to read them (docs/30, docs/50).
void wait_for_the_person() {
  const int64_t began = static_cast<int64_t>(pal::system().millis());
  // **A finger that is already on the glass is not the answer**: the tap that pressed the button that died is still there on a
  // board that reads the panel slowly, and dismissing the page with the hand that caused it is a page nobody reads.
  const int64_t settle = (g_wait_ms < 3000) ? g_wait_ms : 3000;
  while (touch::points(nullptr, 0) > 0 && since(began) < settle) {
    pal::Event event;
    pal::system().queue()->pop(event, 20);
  }
  bool was_down = any_key_down();
  while (since(began) < g_wait_ms) {
    touch::poll();
    touch::Gesture gesture;
    // The gestures of the contact are taken and thrown: a finger on this page is the whole of what it says (docs/30).
    while (touch::take_gesture(gesture)) {
      if (gesture.kind != nullptr && (strcmp(gesture.kind, "tap") == 0 || strcmp(gesture.kind, "long") == 0)) return;
    }
    const bool down = any_key_down();
    if (down && !was_down) return;
    was_down = down;
    pal::Event event;
    pal::system().queue()->pop(event, 20);
  }
}

}  // namespace

void set_wait_ms(int64_t ms) { g_wait_ms = (ms > 0) ? ms : kWaitMs; }

void show(const char* id, const char* text) {
  hal::ISurface* surface = display::surface();
  if (surface == nullptr) return;
  const uint16_t width = surface->width();
  if (width <= 2 * (kMargin + 12)) return;

  // **La lletra**: the body at two steps of the face and the title at three, which is what fits a 540-wide glass with the lines of
  // a traceback (docs/95).
  const uint8_t scale = 2;
  const uint8_t title_scale = 3;

  // The title of the app is asked of the door of the apps: the id is the shell's and the page is not.
  char title[48];
  title[0] = '\0';
  apps::Entry entry;
  if (id != nullptr && apps::read(id, entry)) snprintf(title, sizeof(title), "%s", entry.title);

  paint(*surface, (id != nullptr) ? id : "(no app)", title, text, scale, title_scale);
  log_printf(pal::kLogInfo, "ERROR", "the page of %s is on the glass", (id != nullptr) ? id : "(no app)");
  surface->flush(hal::UpdateMode::kGc16);
  wait_for_the_person();
}

}  // namespace app_error
}  // namespace kernel

#endif  // LUNOK_GLASS
