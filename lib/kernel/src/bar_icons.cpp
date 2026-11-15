// **Els dibuixos de les icones de la barra, portats número per número del `PaperOS/src/ui/title_bar.cpp`** (0.189.56): the
// wifi fan, the Bluetooth rune, the USB mark lying down, the hourglass of a clock being set and the battery figure with its
// bolt. They are the paper project's drawings because they were looked at on that glass and corrected by its owner three
// times —24 px, 32 px, 40 px, the arrow of the USB turned, the clock face read as an alarm— and what a person has already
// learned to read does not have to be learned twice (docs/50, docs/92).
//
// **Per què es dibuixen i no són un PNG**: the icons of the interface's font are built on a box of 32 and the bar's icons
// need 40 × 28; a table of bits per icon would be four files to keep in step with a drawing that is five straight lines and
// three arcs. The paper project wrote them the same way and for the same reason: a shape of lines can be checked by reading
// it (docs/30).
//
// **Cada cosa se suporta en la caixa que rep** —`kIconW`/`kIconH` and `kBatteryW`/`kBatteryH` are the boxes the drawings were
// measured in, and every constant below is scaled by the box's width or height and never by a number of the glass—, so the
// bar of this system paints them at the size of the paper project's and a shorter bar only decides where they sit.
#include "bar_icons_art.h"
#include "kernel/bar_icons.h"

#include <string.h>

namespace kernel {
namespace bar_icons {
namespace {

// **Un rectangle dins del vidre**: the glyphs count in signed pixels —an arc can land on -1— and the surface wants unsigned
// ones, so every primitive comes through here and what falls outside is dropped instead of wrapped (docs/50).
struct Box {
  int x;
  int y;
  int w;
  int h;
};

void fill(hal::ISurface& surface, int x, int y, int w, int h, uint8_t ink) {
  int left = x;
  int top = y;
  int right = x + w;
  int bottom = y + h;
  if (left < 0) left = 0;
  if (top < 0) top = 0;
  if (right > static_cast<int>(surface.width())) right = static_cast<int>(surface.width());
  if (bottom > static_cast<int>(surface.height())) bottom = static_cast<int>(surface.height());
  if (right <= left || bottom <= top) return;
  surface.fill_rect(static_cast<uint16_t>(left), static_cast<uint16_t>(top), static_cast<uint16_t>(right - left),
                    static_cast<uint16_t>(bottom - top), ink);
}

void point(hal::ISurface& surface, int x, int y, uint8_t ink) { fill(surface, x, y, 1, 1, ink); }

void vline(hal::ISurface& surface, int x, int y, int h, uint8_t ink) { fill(surface, x, y, 1, h, ink); }

// The integer square root of the paper project's shapes (`gfx/shapes.cpp`), which is the Newton walk and not a libm call:
// a bar of the system does not pull a floating point library for three arcs.
int isqrt(int value) {
  if (value <= 0) return 0;
  int x = value;
  int y = (x + 1) / 2;
  while (y < x) {
    x = y;
    y = (x + value / x) / 2;
  }
  return x;
}

// A filled disc, column by column (`fillDisc` of the paper project): the round ends of the USB mark and its lower branch.
void disc(hal::ISurface& surface, int cx, int cy, int radius, uint8_t ink) {
  if (radius < 1) return;
  for (int dx = -radius; dx <= radius; ++dx) {
    const int dy = isqrt(radius * radius - dx * dx);
    vline(surface, cx + dx, cy - dy, 2 * dy + 1, ink);
  }
}

// **A line with a body** (`drawThickLine` of the paper project): Bresenham walks the line and each step is a square of
// `thickness`, which is what makes a stroke grow with the box instead of staying a hair.
void thick_line(hal::ISurface& surface, int x0, int y0, int x1, int y1, int thickness, uint8_t ink) {
  if (thickness < 1) thickness = 1;
  const int half = thickness / 2;
  const int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
  const int dy = (y1 > y0) ? (y0 - y1) : (y1 - y0);  // the paper project's sign, kept on purpose
  int sx = (x0 < x1) ? 1 : -1;
  int sy = (y0 < y1) ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    fill(surface, x0 - half, y0 - half, thickness, thickness, ink);
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

// **A triangle lying on its base and pointing right** (`fillTriangleRight` of the paper project): the height is counted from
// the tip, because growing it the other way draws a funnel and the arrow points into the stem —which is the fault the owner
// of the paper project caught on the glass (18/10: *«la fletxa està girada!»*).
void triangle_right(hal::ISurface& surface, int cy, int base_x, int tip_x, int half_h, uint8_t ink) {
  const int length = tip_x - base_x;
  for (int x = base_x; x <= tip_x; ++x) {
    int h = (length > 0) ? (half_h * (tip_x - x)) / length : half_h;
    if (h < 1) h = 1;  // a one-pixel tip reads as a dot on this glass
    fill(surface, x, cy - h, 1, 2 * h + 1, ink);
  }
}

// **EL VENTALL DEL WIFI**: three concentric arcs and a dot, the arcs going up from it.
//
// The glyph is centred **by its ink and not by its box**: the ink spans `outerR` above the dot and `dotR` below it, so a
// glyph centred by its box comes out low —which is what the owner of the paper project saw (11/10).
void wifi(hal::ISurface& surface, const Box& r, uint8_t ink) {
  if (r.w < 12 || r.h < 8) return;
  static const int kBaseRadii[3] = {10, 16, 22};  // the radii of a box of 40
  static const int kDotR = 4;
  static const int kStroke = 4;

  const int stroke = (kStroke * r.w) / kIconW;
  const int dot_r = (kDotR * r.w) / kIconW;
  const int cx = r.x + r.w / 2;
  const int outer = (kBaseRadii[2] * r.w) / kIconW;
  const int cy = r.y + r.h / 2 + (outer - dot_r) / 2;

  for (int i = 0; i < 3; ++i) {
    const int radius = (kBaseRadii[i] * r.w) / kIconW;
    const int span = (radius * 7) / 10;
    for (int dx = -span; dx <= span; ++dx) {
      const int dy = isqrt(radius * radius - dx * dx);
      vline(surface, cx + dx, cy - dy, stroke, ink);
    }
  }
  // The dot, rounded: a square of (2*dot+1) with its corners cut.
  for (int dy = -dot_r; dy <= dot_r; ++dy) {
    for (int dx = -dot_r; dx <= dot_r; ++dx) {
      if (dx * dx + dy * dy <= dot_r * dot_r + 1) point(surface, cx + dx, cy + dy, ink);
    }
  }
}

// **LA RUNA DEL BLUETOOTH**: a stem, three steps down its right making the two loops of a "B", and a chevron pointing at
// the stem's middle from the left. It is a bind rune and not a letter: the two right points sit a third of the stem from its
// ends, the loops close on a point just outside the stem so the middle stays open, and the chevron stops short of it.
void bluetooth(hal::ISurface& surface, const Box& r, uint8_t ink) {
  if (r.w < 12 || r.h < 8) return;
  const int stroke = (3 * r.h) / kIconH;
  const int cx = r.x + r.w / 2;
  const int top = r.y + 2;
  const int bot = r.y + r.h - 3;
  const int step = (r.h - 5) / 3;  // the height of each of the three slopes: the one number the rune is measured in
  const int upper = top + step;
  const int lower = bot - step;
  const int mid = (top + bot) / 2;
  const int reach = cx + step / 3;  // where each loop closes, outside the stem
  const int left = cx - step - 1;

  thick_line(surface, cx, top, cx, bot, stroke, ink);
  thick_line(surface, cx, top, cx + step, upper, stroke, ink);
  thick_line(surface, cx + step, upper, reach, mid, stroke, ink);
  thick_line(surface, reach, mid, cx + step, lower, stroke, ink);
  thick_line(surface, cx + step, lower, cx, bot, stroke, ink);
  thick_line(surface, left, upper, cx - step / 3, mid, stroke, ink);
  thick_line(surface, cx - step / 3, mid, left, lower, stroke, ink);
}

// **LA MARCA DE L'USB, TOMBADA**: the arrow, the stem behind it, the disc at the cable's end and the two branches with their
// square and their round terminal. Lying down it is 38 x 21 and fills its box the way the fan does; upright it used 27 of
// the 40 and left a third of the box empty on each side (the paper project, 18/10).
void serial(hal::ISurface& surface, const Box& r, uint8_t ink) {
  if (r.w < 12 || r.h < 8) return;
  const int stroke = (3 * r.h) / kIconH;
  const int arrow_half = (5 * r.h) / kIconH;
  const int arrow_w = (8 * r.w) / kIconW;
  const int arm_x = (20 * r.w) / kIconW;
  const int arm = (8 * r.h) / kIconH;
  const int rise = (5 * r.w) / kIconW;
  const int base_r = (3 * r.h) / kIconH;
  const int square = (5 * r.h) / kIconH;

  const int cy = r.y + r.h / 2;
  const int base_x = r.x + 1 + base_r;  // the cable's end
  const int tip = r.x + r.w - 2;        // the arrow's point
  const int arrow_base = tip - arrow_w;
  const int jx = r.x + arm_x;  // where the branches leave

  disc(surface, base_x, cy, base_r, ink);
  if (arrow_base > base_x) fill(surface, base_x, cy - stroke / 2, arrow_base - base_x, stroke, ink);
  triangle_right(surface, cy, arrow_base, tip, arrow_half, ink);

  fill(surface, jx, cy - arm, stroke, arm + stroke / 2, ink);
  fill(surface, jx, cy - stroke / 2, stroke, arm + stroke / 2, ink);
  fill(surface, jx, cy - arm, rise, stroke, ink);
  fill(surface, jx + rise, cy - arm - square / 2, square, square, ink);
  if (rise > 2) fill(surface, jx + 2, cy + arm, rise - 2, stroke, ink);
  disc(surface, jx + rise, cy + arm, 2, ink);
}

// **EL RELLOTGE DE SORRA DE L'NTP**: a request for the network's hour is in flight and the board is waiting for it. It was a
// clock face with two hands first, and that is a clock or an alarm and there is no third reading (the paper project,
// 18/10: *«Alarma?»*), so the glyph says what the bar is telling: sand falling. Four straight lines.
void ntp(hal::ISurface& surface, const Box& r, uint8_t ink) {
  if (r.w < 12 || r.h < 8) return;
  const int stroke = (3 * r.h) / kIconH;
  const int half_w = (9 * r.w) / kIconW;
  const int top = r.y + (3 * r.h) / kIconH;
  const int bot = r.y + r.h - (4 * r.h) / kIconH;
  const int cx = r.x + r.w / 2;
  if (half_w < 4 || bot - top < 8) return;

  fill(surface, cx - half_w, top, 2 * half_w, stroke, ink);
  fill(surface, cx - half_w, bot - stroke, 2 * half_w, stroke, ink);
  thick_line(surface, cx - half_w + 1, top + stroke, cx + half_w - 1, bot - stroke, stroke, ink);
  thick_line(surface, cx + half_w - 1, top + stroke, cx - half_w + 1, bot - stroke, stroke, ink);
}

// **EL LLAMP D'UNA BATERIA QUE S'ESTÀ ALIMENTANT**: a filled polygon scanned row by row, its six points in percentages of the
// box so it is the same bolt in a figure of 46 x 26 and in one of 20 x 14. The step between the two halves is what makes it
// read as a bolt instead of as one thick diagonal (measured on the paper project's glass at 34 x 18).
void bolt(hal::ISurface& surface, const Box& box, uint8_t ink) {
  static const int kPoints[6][2] = {
      {70, 0}, {22, 52}, {48, 52}, {30, 100}, {78, 48}, {52, 48},
  };
  if (box.w < 4 || box.h < 4) return;
  for (int y = box.y; y < box.y + box.h; ++y) {
    // The centre of the row and not its top edge, or the last row of the bolt is lost.
    const int py = ((2 * (y - box.y) + 1) * 100) / (2 * box.h);
    int crossings[6];
    int count = 0;
    for (int i = 0; i < 6; ++i) {
      const int j = (i + 1) % 6;
      const int y1 = kPoints[i][1];
      const int y2 = kPoints[j][1];
      if (y1 == y2) continue;  // a horizontal edge cuts no row
      if (py < y1 && py < y2) continue;
      if (py >= y1 && py >= y2) continue;
      const int t = ((py - y1) * 100) / (y2 - y1);
      crossings[count++] = kPoints[i][0] + (t * (kPoints[j][0] - kPoints[i][0])) / 100;
    }
    // From left to right, so a pair is a piece of the row.
    for (int i = 1; i < count; ++i) {
      const int value = crossings[i];
      int j = i - 1;
      while (j >= 0 && crossings[j] > value) {
        crossings[j + 1] = crossings[j];
        --j;
      }
      crossings[j + 1] = value;
    }
    for (int i = 0; i + 1 < count; i += 2) {
      const int from = box.x + (crossings[i] * box.w) / 100;
      const int to = box.x + (crossings[i + 1] * box.w) / 100;
      fill(surface, from, y, to - from + 1, 1, ink);
    }
  }
}

// **EL DESPERTADOR D'UNA ALARMA POSADA** (0.189.57): the drawing is this project's and not the paper project's, made of the
// same lines as the others —a ring for the face, two bells on top, a hammer between them, two hands and the two feet— so it
// reads beside the hourglass without a second language. The ink is 26 x 26 of the box of 40 x 28, which is what the bar
// places it by (docs/50, docs/92).
void alarm(hal::ISurface& surface, const Box& r, uint8_t ink) {
  // **El rellotge de dormitori és una icona i no un dibuix de línies** (0.189.58): l'amo el va demanar dues vegades —*«jo
  // esperava allà un rellotge despertador dels antics de manilles i dues campanes al damunt»* i després *«aixó es una campana!
  // vull un rellotge de dormitori»*— i tres intents de dibuixar-lo amb línies a vint píxels van sortir una cella i un smiley.
  // `U+23F0 ALARM CLOCK` de Noto té les dues campanes, el martell, l'esfera i les manilles, i a vint-i-un píxels es llegeix
  // d'un cop. Ve com a taula de bits perquè és una corba composta, i l'escriu `tools/make_bar_icon.py` (SIL OFL 1.1 per a la
  // font, Apache 2.0 per al codi de Noto; l'atribució viu al fitxer generat i a docs/50).
  const int x0 = r.x + (r.w - static_cast<int>(art::kAlarmW)) / 2;
  const int y0 = r.y + (r.h - static_cast<int>(art::kAlarmH)) / 2;
  for (uint8_t y = 0; y < art::kAlarmH; ++y) {
    for (uint8_t x = 0; x < art::kAlarmW; ++x) {
      const uint16_t at = static_cast<uint16_t>(y) * art::kAlarmW + x;
      if ((art::kAlarmBits[at / 8] & (0x80u >> (at % 8))) != 0) point(surface, x0 + x, y0 + y, ink);
    }
  }
}

}  // namespace

Ink ink_of(Kind kind) {
  switch (kind) {
    case kWifi:
      return Ink{31, 27};  // the fan of the paper project, measured on its glass (18/10)
    case kSerial:
      return Ink{38, 21};  // the mark of the port, lying down
    case kBluetooth:
      return Ink{18, 26};  // the rune
    case kHourglass:
      return Ink{19, 21};  // the hourglass
    case kAlarm:
      return Ink{art::kAlarmW, art::kAlarmH};  // the bell of FontAwesome, as wide as the table it comes in
  }
  return Ink{static_cast<uint8_t>(kIconW), static_cast<uint8_t>(kIconH)};
}

void glyph(hal::ISurface& surface, Kind kind, uint16_t x, uint16_t y, uint8_t ink) {
  const Box box{static_cast<int>(x), static_cast<int>(y), kIconW, kIconH};
  switch (kind) {
    case kWifi:
      wifi(surface, box, ink);
      break;
    case kBluetooth:
      bluetooth(surface, box, ink);
      break;
    case kSerial:
      serial(surface, box, ink);
      break;
    case kHourglass:
      ntp(surface, box, ink);
      break;
    case kAlarm:
      alarm(surface, box, ink);
      break;
  }
}

void battery(hal::ISurface& surface, uint16_t x, uint16_t y, int level, bool charging, uint8_t ink) {
  const Box r{static_cast<int>(x), static_cast<int>(y), kBatteryW, kBatteryH};
  if (r.w < 12 || r.h < 8) return;
  // **Les parets s'engreixen amb la caixa**: on a box of 26 rows a wall of two is a hair, and a figure that is bigger but
  // thinner reads worse than the small one (the paper project, 12/10).
  const int wall = (r.h >= 24) ? 3 : 2;
  const int body_w = r.w - (wall + 1);  // the nub takes the rest, on the right
  const int body_h = r.h;

  fill(surface, r.x + body_w, r.y + body_h / 4, wall + 1, body_h / 2, ink);
  fill(surface, r.x, r.y, body_w, wall, ink);
  fill(surface, r.x, r.y + r.h - wall, body_w, wall, ink);
  fill(surface, r.x, r.y, wall, body_h, ink);
  fill(surface, r.x + body_w - wall, r.y, wall, body_h, ink);

  const int inner_x = r.x + wall + 1;
  const int inner_y = r.y + wall + 1;
  const int inner_w = body_w - 2 * wall - 2;
  const int inner_h = body_h - 2 * wall - 2;
  if (inner_w < 8 || inner_h < 4) return;

  // **Amb el cable a dins és un llamp i ni una barra**: a bolt over four black bars is a stain, and the level is not drawn
  // while the board is being fed (the paper project, 28/10, literal: *«Quan el dispositiu carregui vull que l'icona de la
  // batería es vegi amb un llamp al mig, sense cárrega perque el llamp es vegi bé»*).
  if (charging) {
    bolt(surface, Box{inner_x, inner_y, inner_w, inner_h}, ink);
    return;
  }
  if (level <= 0) return;  // an empty box IS the empty state
  if (level > 4) level = 4;
  const int bar_w = (inner_w - 3 * 2) / 4;  // four bars and three gaps of two, and they fit by construction
  if (bar_w < 1) return;
  for (int i = 0; i < level; ++i) {
    fill(surface, inner_x + i * (bar_w + 2), inner_y, bar_w, inner_h, ink);
  }
}

}  // namespace bar_icons
}  // namespace kernel
