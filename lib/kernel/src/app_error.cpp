// **La pàgina d'una app que s'ha mort** (0.189.162, l'amo: *«Si peta una app, que menys que saber el perqué amb la pantalla i
// els dits. Un modal amb el missatge d'error? vull detalls del perqué una app peta»*).
//
// The error of an app used to be a line of the log and the console, and a person with the glass in front of them saw the page
// under it —the launcher, the prompt— as if nothing had happened. This file paints the why: the glass washed whole, the name of
// the app, the message the interpreter left, the traceback of where it happened, and one line at the bottom that says how to go
// on. **The drawing is the system's own**: whatever `hal::ISurface` and the face of the system give, the same two things the bar
// and the splash use, so a board with no disk still shows its error (docs/30, docs/50).
//
// **I el que la pàgina espera** (0.190.1, l'amo: *«ha de tenir un botó "Dismiss" i fins que no es polsi no se surt de l'app. Ara per
// algun motiu no dona temps a llegir el missatge, ja que Home torna a arrencar-se de cop»*): **el botó i prou**. Abans qualsevol toc
// al vidre la treia —i el dit que havia fet petar l'app encara era a la cua quan la pàgina es pintava, o sigui que la pàgina marxava
// amb la mà que l'havia causada— i a més es tancava sola al cap de mig minut. Ara un toc fora del botó no és res, una tecla només
// compta en una placa sense dit, i el número de segons és **zero**, que vol dir esperar tant com calgui (docs/30, docs/50, docs/90).
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
// **Quant espera la pàgina: tot el que calgui** (0.190.1): zero is no limit —the page is the person's and it goes when the button
// goes— and it is the system's number. A case of the suite that runs a page nobody is going to touch sets a short one, perquè una
// suite amb una pàgina que no marxa no acaba mai (docs/80).
const int64_t kWaitMs = 0;
int64_t g_wait_ms = kWaitMs;
// **El botó**: the one door of the page. A word of the system's own —`Dismiss`— inside its frame, at the bottom and in the middle,
// where a thumb reaches it (docs/30, docs/50).
const uint16_t kButtonW = 176;
const uint16_t kButtonH = 52;
// **Les mides d'una tecla d'aquest sistema** (0.190.4): the fourteen pixels of corner of `ui`'s buttons —never more than a third of
// the smaller side— and the four of face that stay off the bottom of the box, which is the shadow the button sinks into (docs/90).
const int32_t kCorner = 14;
const int32_t kLift = 4;

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
  const uint8_t room = static_cast<uint8_t>((height - kMargin - kButtonH - 24 - y) / (line_h + 2));
  Count counter(kLines);
  walk_text(text, static_cast<uint16_t>(width - 2 * (kMargin + 12)), scale, counter);
  const uint8_t take = (counter.lines < room) ? counter.lines : room;
  if (take > 0) {
    Paint painter(surface, left, y, scale, take, counter.lines);
    walk_text(text, static_cast<uint16_t>(width - 2 * (kMargin + 12)), scale, painter);
  }

  // **El botó, que és l'única cosa que treu la pàgina del vidre** (0.190.1), pintat com el botó que és (0.190.4): una tecla amb la
  // cara aixecada damunt de la seva ombra, on arriba el polze i on el cas de `test_apps` busca la tinta (docs/30, docs/50, docs/80).
  paint_the_button(surface, dismiss_button(width, height), false, scale);
}

// **Qui mira el host mentre la pàgina espera** (0.190.3): the turn of the kernel is stopped here —the app that died was
// inside it— and on a PC the window of the emulator is looked at by that turn, so a page that waits for ever was a window that
// stopped answering its window manager and a mouse that never reached the glass (*«es veu l'error al log i després l'emulator
// es penja»*, docs/85). The host is asked once per turn of this page, and the answer that matters is the last one: **a person
// who closes the window is asking the run to end**, and a page waiting for a button on a screen that is no longer there is a run
// nobody can stop (docs/30).
bool the_host_is_alive() { return pal::pump(); }

// **How far a corner pushes the ink in on a row** (0.190.4): a quarter of a circle, which is a square root of integers — no floating
// point, no table, the same rule `ui` uses for the buttons of a page (docs/90).
int32_t corner_inset(int32_t radius, int32_t down) {
  if (radius <= 0 || down >= radius) return 0;
  const int32_t left = radius * radius - down * down;
  int32_t root = 0;
  while ((root + 1) * (root + 1) <= left) ++root;
  return radius - root;
}

// **A box with the corners rounded off, row by row**: a surface has four rectangles and nothing else, so a rounded corner is a row at
// a time and the button of a page is fifty-two of them (docs/50, docs/90).
void round_fill(hal::ISurface& surface, int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius, uint8_t level) {
  if (w <= 0 || h <= 0) return;
  const int32_t most = ((w < h) ? w : h) / 2;
  if (radius > most) radius = most;
  if (radius < 0) radius = 0;
  for (int32_t row = 0; row < h; ++row) {
    int32_t down = radius;
    if (row < radius) {
      down = radius - row - 1;
    } else if (row >= h - radius) {
      down = row - (h - radius);
    }
    const int32_t inset = corner_inset(radius, down);
    if (w - 2 * inset <= 0) continue;
    surface.fill_rect(static_cast<uint16_t>(x + inset), static_cast<uint16_t>(y + row),
                      static_cast<uint16_t>(w - 2 * inset), 1, level);
  }
}

// **Si un punt és dins del botó**, which is the question of the wait and of every gesture of the glass (0.190.1).
bool inside_the_button(const Button& button, int32_t x, int32_t y) {
  return x >= button.x && x < static_cast<int32_t>(button.x) + button.w && y >= button.y &&
         y < static_cast<int32_t>(button.y) + button.h;
}

bool any_key_down() {
  const uint32_t keys = button::count();
  for (uint32_t i = 0; i < keys; ++i) {
    if (button::down(i)) return true;
  }
  return false;
}

// **El que s'espera: el botó** (0.190.1). The panel and the queue are read here because the kernel's turn is the one that is stopped
// —the app that died was inside it— so nobody else is going to read them (docs/30, docs/50).
void wait_for_the_person(hal::ISurface& surface, uint16_t width, uint16_t height, uint8_t scale) {
  const bool has_a_finger = touch::available();
  // **El dit que va fer petar l'app no és la resposta**: the tap that pressed the button that died is still in the queue of a board
  // that reads the glass slowly, and a page that goes away with the hand that caused it is a page nobody reads. Everything the finger
  // did before the page is thrown away, and **a finger that is still down is waited out**: the gesture of it comes when it lifts, and
  // by then this page is the one that is on the glass (docs/30, docs/50).
  if (has_a_finger) {
    touch::forget_gestures();
    const int64_t settle = (g_wait_ms > 0 && g_wait_ms < 3000) ? g_wait_ms : 3000;
    const int64_t began = static_cast<int64_t>(pal::system().millis());
    while (touch::points(nullptr, 0) > 0 && since(began) < settle) {
      if (the_host_is_alive()) return;
      pal::Event event;
      pal::system().queue()->pop(event, 20);
    }
    touch::forget_gestures();
  }
  bool was_down = any_key_down();
  // **El botó respon mentre el dit hi és a sobre** (0.190.4): a key of this panel sinks and turns over the moment a finger lands on it,
  // and a button that says nothing until the finger is lifted is a *drawing* of a button (*«El botó dismiss no es un botó, es un
  // dibuix, PERQUE?»*, l'amo). The button is painted again only when the answer changes —a finger that stays is one paint— and the paint
  // is the box of the button and nothing else, which is a `kA2` of a key and not a page (docs/30, docs/50).
  bool showing_the_press = false;
  const int64_t began = static_cast<int64_t>(pal::system().millis());
  for (;;) {
    // **Zero vol dir sempre**: the page of an error is the person's, and a page that closes itself hides the reason (docs/30).
    if (g_wait_ms > 0 && since(began) >= g_wait_ms) return;
    // **I el host es manté viu mentre s'espera** (0.190.3): the window of a run is looked at here, because the turn that
    // would look at it is the one this page has stopped. Closing that window is leaving the page (docs/85).
    if (the_host_is_alive()) return;
    if (has_a_finger) {
      touch::poll();
      touch::Gesture gesture;
      // **El gest arriba en el marc de la vista i la pàgina és el vidre** (0.190.4): the fingers and the gestures of the kernel are
      // the view's —the bar of the system is not part of any app— and this page paints the whole glass, so the point of the answer
      // is the glass's. Without this the button answered a finger forty-eight pixels below the one it draws (docs/30, docs/50).
      while (touch::take_gesture(gesture)) {
        uint16_t gx = static_cast<uint16_t>(gesture.x);
        uint16_t gy = static_cast<uint16_t>(gesture.y);
        touch::on_the_glass(gx, gy);
        if (it_is_an_answer(width, height, gesture.kind, gx, gy)) return;
      }
      // Whichever finger is on the glass right now, and whether it is on the button.
      hal::TouchPoint fingers[2];
      const uint8_t down_now = touch::points(fingers, 2);
      bool is_on_it = false;
      const Button where = dismiss_button(width, height);
      for (uint8_t i = 0; i < down_now; ++i) {
        uint16_t fx = fingers[i].x;
        uint16_t fy = fingers[i].y;
        touch::on_the_glass(fx, fy);
        if (inside_the_button(where, fx, fy)) is_on_it = true;
      }
      if (is_on_it != showing_the_press) {
        showing_the_press = is_on_it;
        paint_the_button(surface, where, showing_the_press, scale);
        surface.flush(hal::UpdateMode::kA2);
      }
    }
    // **Una tecla, només en una placa que no té dit**: a board with no glass a person can touch —the ATOMs— would have no way out of
    // this page at all, and the dial is what it has (docs/30, docs/50).
    const bool down = any_key_down();
    if (down && !was_down && !has_a_finger) return;
    was_down = down;
    pal::Event event;
    pal::system().queue()->pop(event, 20);
  }
}

}  // namespace

void set_wait_ms(int64_t ms) { g_wait_ms = (ms > 0) ? ms : kWaitMs; }

// **On és el botó d'una pàgina d'aquesta mida** (0.190.1): centred at the bottom, with the frame above it and the air of the page
// around it, and cut to the glass when the glass is narrower than the word (docs/30).
Button dismiss_button(uint16_t width, uint16_t height) {
  Button button;
  const uint16_t widest = static_cast<uint16_t>(width - 2 * kMargin);
  button.w = (kButtonW < widest) ? kButtonW : widest;
  button.h = kButtonH;
  button.x = static_cast<uint16_t>((width - button.w) / 2);
  button.y = static_cast<uint16_t>(height - kMargin - kFrame - 12 - kButtonH);
  return button;
}

// **El botó, pintat com una tecla del sistema** (0.190.4). The box is the shadow —ink under the face while the button is up— and the face
// is lifted off the bottom of it, so those four pixels are what a finger sees the button sink into. **Amb el dit a sobre la cara baixa
// fins al fons i es gira sencera** —cara de tinta i paraula de paper—, que és el que fa tota tecla d'aquest sistema i el que fa que
// sembli que la premsa ha arribat (docs/30, docs/50, docs/90). The face wears a one-pixel frame when it is the paper one: on a washed
// page that line is what says that this is a thing to press, and it is drawn **on the face** and not on the box, or it would stay
// where it was while the button goes down.
void paint_the_button(hal::ISurface& surface, Button button, bool pressed, uint8_t scale) {
  if (button.w == 0 || button.h == 0) return;
  const int32_t x = button.x;
  const int32_t y = button.y;
  const int32_t w = button.w;
  const int32_t h = button.h;
  int32_t radius = kCorner;
  const int32_t most = ((w < h) ? w : h) / 3;
  if (radius > most) radius = most;
  if (radius < 1) radius = 1;
  int32_t lift = kLift;
  const int32_t lift_cap = h / 3;
  if (lift > lift_cap) lift = lift_cap;
  const uint8_t paper = 15;
  const uint8_t ink = 0;
  const int32_t face_y = y + (pressed ? lift : 0);
  const int32_t face_h = h - lift;
  round_fill(surface, x, y, w, h, radius, pressed ? paper : ink);
  // **La cara, amb el seu marc d'un píxel**: the face is filled in ink and its inside is the paper, so what is left is a frame of one
  // pixel —the rule of every button of this system—, and a pressed face stays in ink and is the frame that goes away with it (docs/92).
  round_fill(surface, x, face_y, w, face_h, radius, ink);
  if (!pressed) round_fill(surface, x + 1, face_y + 1, w - 2, face_h - 2, radius - 1, paper);
  const char* word = "Dismiss";
  const uint16_t word_w = font::width(word, scale);
  const uint16_t word_h = static_cast<uint16_t>(font::system_face().height * scale);
  font::draw(surface, static_cast<uint16_t>(x + (w - static_cast<int32_t>(word_w)) / 2),
             static_cast<uint16_t>(face_y + (face_h - static_cast<int32_t>(word_h)) / 2), word, scale,
             pressed ? paper : ink);
}

// **I què és la resposta** (0.190.1): a tap or a long press **on the button**, and nothing else. The wait asks this of every gesture of
// the glass —a tap in the middle of the message, a drag out of it or a tap on the bar of the system are gestures that have not read
// the page— and a case asks it of the ones that matter without touching anything (docs/30, docs/80).
bool it_is_an_answer(uint16_t width, uint16_t height, const char* kind, int32_t x, int32_t y) {
  if (kind == nullptr) return false;
  if (strcmp(kind, "tap") != 0 && strcmp(kind, "long") != 0) return false;
  if (x < 0 || y < 0) return false;
  return inside_the_button(dismiss_button(width, height), x, y);
}

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
  wait_for_the_person(*surface, width, surface->height(), scale);
}

}  // namespace app_error
}  // namespace kernel

#endif  // LUNOK_GLASS
