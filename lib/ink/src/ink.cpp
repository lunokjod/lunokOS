// **El glif, la quadrícula i el model** (docs/30, docs/90): the three pieces of a handwriting keyboard that learns the hand it
// is given, with no allocation and no floating point —the whole thing has to run in a turn of this loop, and a division per
// point is already the most expensive thing here—.
#include "ink/ink.h"

#include <string.h>

namespace ink {

namespace {

int32_t absolute(int32_t v) { return (v < 0) ? -v : v; }

int32_t clamped(int32_t v, int32_t low, int32_t high) { return (v < low) ? low : ((v > high) ? high : v); }

}  // namespace

// ------------------------------------------------------------------ el glif

void Glyph::clear() {
  count = 0;
  strokes = 0;
  drawing = false;
  for (uint8_t i = 0; i < kMaxStrokes; ++i) start[i] = 0;
}

bool Glyph::add(int16_t x, int16_t y) {
  if (!drawing) {
    // **El primer punt d'un traç l'obre**: a stroke that begins where there is no room for its first point is a stroke that
    // does not exist, and the glyph is what it was.
    if (strokes >= kMaxStrokes || count >= kMaxPoints) return false;
    start[strokes] = count;
    ++strokes;
    drawing = true;
  }
  if (count >= kMaxPoints) return false;
  point[count].x = x;
  point[count].y = y;
  ++count;
  return true;
}

bool Glyph::lift() {
  if (!drawing) return false;
  drawing = false;
  return true;
}

// ------------------------------------------------------------------ la quadrícula

uint8_t Grid::inked() const {
  uint8_t n = 0;
  for (uint8_t i = 0; i < kCells; ++i) {
    if (cell[i] != kQuiet) ++n;
  }
  return n;
}

namespace {

// Where a point of the page lands on the grid, in eighths of a cell. The glyph is scaled by **its longest side** and centred,
// which is what keeps `-` a line and `1` a stick instead of stretching both into a square (docs/90).
int32_t on_the_grid(int32_t at, int32_t centre_twice, int32_t longest) {
  return (kSide * 8) / 2 + ((at * 2 - centre_twice) * kSide * 8) / (2 * longest);
}

// Walks one segment in eighths of a cell and counts how many times the path enters each cell. `last` is the cell of the
// previous step and it is carried across the segments of one stroke, so a straight line is one pass and not one per sample.
void walk(int32_t ax, int32_t ay, int32_t bx, int32_t by, uint8_t* cell, int32_t& last) {
  const int32_t dx = bx - ax;
  const int32_t dy = by - ay;
  int32_t steps = (absolute(dx) > absolute(dy)) ? absolute(dx) : absolute(dy);
  if (steps <= 0) steps = 1;
  for (int32_t i = 0; i <= steps; ++i) {
    const int32_t px = ax + (dx * i) / steps;
    const int32_t py = ay + (dy * i) / steps;
    const int32_t cx = clamped(px / 8, 0, kSide - 1);
    const int32_t cy = clamped(py / 8, 0, kSide - 1);
    const int32_t idx = cy * kSide + cx;
    if (idx == last) continue;
    last = idx;
    if (cell[idx] < kTwice) ++cell[idx];
  }
}

}  // namespace

namespace {

// **El lloc dins del camp, en vuitens** (0.195.0): eighths and not quarters, perquè és el pas que separa una `-` del mig d'una `_`
// del peu (tres vuitens, mesurats) i alhora un pèl de traç no mou el número més d'un; el pes de la distància és el que decideix quant
// compta, i qui dibuixa el mateix traç una mica més amunt té el seu exemple a prop (docs/30, docs/90).
uint8_t eighth_of(int32_t at, int32_t field) {
  if (field <= 0) return kWhereUnknown;
  int32_t eighth = (at * 8) / field;
  if (eighth < 0) eighth = 0;
  if (eighth > 8) eighth = 8;
  return static_cast<uint8_t>(eighth);
}

}  // namespace

bool raster(const Glyph& g, Grid& out) { return raster(g, Field(), out); }

bool raster(const Glyph& g, const Field& where, Grid& out) {
  out = Grid();
  if (g.count == 0 || g.strokes == 0) return false;
  int16_t x0 = g.point[0].x;
  int16_t x1 = x0;
  int16_t y0 = g.point[0].y;
  int16_t y1 = y0;
  for (uint16_t i = 1; i < g.count; ++i) {
    if (g.point[i].x < x0) x0 = g.point[i].x;
    if (g.point[i].x > x1) x1 = g.point[i].x;
    if (g.point[i].y < y0) y0 = g.point[i].y;
    if (g.point[i].y > y1) y1 = g.point[i].y;
  }
  const int32_t width = static_cast<int32_t>(x1) - x0 + 1;
  const int32_t height = static_cast<int32_t>(y1) - y0 + 1;
  // **Un toc és un punt i no una negativa** (0.195.0): every point in the same place is a finger that pressed, i un `.` és això
  // mateix; abans es refusava i el punt no es podia escriure. El que el separa d'una ratlla és la `size`, i per això aquesta porta
  // vol el camp (docs/30, docs/90).
  const int32_t longest = (width > height) ? width : height;
  const int32_t centre_x2 = static_cast<int32_t>(x0) + x1;
  const int32_t centre_y2 = static_cast<int32_t>(y0) + y1;

  for (uint8_t s = 0; s < g.strokes; ++s) {
    const uint16_t from = g.start[s];
    const uint16_t to = (s + 1 < g.strokes) ? g.start[s + 1] : g.count;
    if (from >= to) continue;
    int32_t last = -1;
    if (to - from == 1) {
      // **Un traç d'un sol punt és el punt d'una `i`**: it still inks its cell, and it is what tells `i` from `l`.
      const int32_t cx = clamped(on_the_grid(g.point[from].x, centre_x2, longest) / 8, 0, kSide - 1);
      const int32_t cy = clamped(on_the_grid(g.point[from].y, centre_y2, longest) / 8, 0, kSide - 1);
      const int32_t idx = cy * kSide + cx;
      if (out.cell[idx] < kTwice) ++out.cell[idx];
      continue;
    }
    for (uint16_t i = from; i + 1 < to; ++i) {
      walk(on_the_grid(g.point[i].x, centre_x2, longest), on_the_grid(g.point[i].y, centre_y2, longest),
           on_the_grid(g.point[i + 1].x, centre_x2, longest), on_the_grid(g.point[i + 1].y, centre_y2, longest),
           out.cell, last);
    }
  }
  out.strokes = g.strokes;
  // **La proporció del requadre, en vuitens**: 8 is square or wider, 1 is a hair. Un `-` i un `|` són la mateixa tinta i no són
  // la mateixa lletra.
  int32_t aspect = (width * 8 + longest / 2) / longest;
  if (aspect < 1) aspect = 1;
  if (aspect > 8) aspect = 8;
  out.aspect = static_cast<uint8_t>(aspect);
  // **On i quant, si el camp es diu**: the centre of the ink in quarters of the field, and the longest side of the ink over the
  // longest side of the field. A `-` in the middle and a `_` at the bottom differ here and nowhere else, i un `.` és `size` zero
  // mentre una ratlla que travessa el paper és tres (docs/30, docs/90).
  if (where.known()) {
    // **El centre de la tinta, en coordenades del camp**: the points come in the coordinates of the page and the field is a box
    // inside it, o sigui que primer es treu l'origen i després es mira on cau (docs/30, docs/90).
    const int32_t centre_x = (static_cast<int32_t>(x0) + x1) / 2 - where.x;
    const int32_t centre_y = (static_cast<int32_t>(y0) + y1) / 2 - where.y;
    const int32_t field_longest = (where.w > where.h) ? where.w : where.h;
    out.at_x = eighth_of(centre_x, where.w);
    out.at_y = eighth_of(centre_y, where.h);
    out.size = eighth_of(longest, field_longest);
  }
  return true;
}

bool raster(const Point* points, uint16_t count, Grid& out) { return raster(points, count, Field(), out); }

bool raster(const Point* points, uint16_t count, const Field& where, Grid& out) {
  Glyph g;
  if (points == nullptr || count == 0) return false;
  for (uint16_t i = 0; i < count; ++i) {
    if (!g.add(points[i].x, points[i].y)) break;
  }
  g.lift();
  return raster(g, where, out);
}

// ------------------------------------------------------------------ el model

uint16_t distance(const Grid& a, const Grid& b) {
  uint16_t total = 0;
  for (uint8_t i = 0; i < kCells; ++i) {
    total = static_cast<uint16_t>(total + static_cast<uint16_t>(absolute(a.cell[i] - b.cell[i]) * 3));
  }
  total = static_cast<uint16_t>(total + static_cast<uint16_t>(absolute(a.aspect - b.aspect) * 3));
  // **Els traços pesen més que una cel·la**: `l` i `i` són la mateixa línia i una té un punt, o sigui que comptar-los és el que
  // les separa quan la quadrícula ja no pot (docs/90).
  total = static_cast<uint16_t>(total + static_cast<uint16_t>(absolute(a.strokes - b.strokes) * 9));
  // **I el lloc i la mida, quan les dues en saben** (0.195.0): a `-` and a `_` are the same cells and the same aspect, o sigui que
  // aquests tres números són l'única cosa que les separa; la mida pesa més perquè un `.` i una `-` també són la mateixa tinta. Una
  // quadrícula sense camp no compta en cap dels tres, que és el que fa que un dibuix sense camp es pugui comparar amb un model vell
  // (docs/30, docs/90).
  // **Quant compta cada un, i per què no pesen igual**: `at_y` és la línia d'escriptura —el mig i el peu de la pàgina són dues coses—
  // i pesa **12** per vuitè, que és el que fa que un `-` del mig i un `_` del peu (dos vuitens) estiguin a 24 i no a 16, és a dir per
  // sobre de `kNear` i no només per sobre de `kMargin` (mesurat: vegeu el cas de `test_ink`); `size` és el que separa un punt d'una
  // ratlla i pesa 10; i `at_x` pesa **2** i no més perquè el paper d'entrenament és ample i **on, d'ample, dibuixa una persona és
  // soroll**: el mateix `-` al mig del paper i a l'esquerra no han de ser dues ratlles (docs/30, docs/90).
  if (a.at_x != kWhereUnknown && b.at_x != kWhereUnknown) {
    total = static_cast<uint16_t>(total + static_cast<uint16_t>(absolute(a.at_x - b.at_x) * 2));
    total = static_cast<uint16_t>(total + static_cast<uint16_t>(absolute(a.at_y - b.at_y) * 12));
    total = static_cast<uint16_t>(total + static_cast<uint16_t>(absolute(a.size - b.size) * 10));
  }
  return total;
}

bool Model::add(const Grid& grid, char text) {
  if (!usable() || text == '\0') return false;
  if (grid.inked() == 0) return false;
  for (uint16_t i = 0; i < count; ++i) {
    if (examples[i].text != text) continue;
    if (distance(examples[i].grid, grid) != 0) continue;
    return false;  // the same drawing of the same letter is one example and not two
  }
  if (count >= capacity) return false;
  examples[count].grid = grid;
  examples[count].text = text;
  ++count;
  return true;
}

namespace {

// Keeps the three best of what has been seen, one per character and not one per example: the band shows three candidates and
// three times the same letter would be a strip with a hole in it (docs/90).
void note(Answer& out, char text, uint16_t d) {
  for (uint8_t i = 0; i < out.count; ++i) {
    if (out.top[i].text != text) continue;
    if (d < out.top[i].distance) out.top[i].distance = d;
    for (uint8_t k = i; k > 0 && out.top[k].distance < out.top[k - 1].distance; --k) {
      const Guess swap = out.top[k];
      out.top[k] = out.top[k - 1];
      out.top[k - 1] = swap;
    }
    return;
  }
  uint8_t slot = out.count;
  if (slot >= kCandidates) {
    if (d >= out.top[kCandidates - 1].distance) return;
    slot = kCandidates - 1;
  } else {
    ++out.count;
  }
  out.top[slot].text = text;
  out.top[slot].distance = d;
  for (uint8_t k = slot; k > 0 && out.top[k].distance < out.top[k - 1].distance; --k) {
    const Guess swap = out.top[k];
    out.top[k] = out.top[k - 1];
    out.top[k - 1] = swap;
  }
}

}  // namespace

Answer Model::guess(const Grid& grid) const {
  Answer out;
  if (!usable() || grid.inked() == 0) return out;
  for (uint16_t i = 0; i < count; ++i) {
    const uint16_t d = distance(grid, examples[i].grid);
    if (d > kFar) continue;
    note(out, examples[i].text, d);
  }
  out.any = out.count > 0;
  // **Quan es pot escriure sense preguntar**: the first has to be near and it has to be ahead of the second. A tie between two
  // letters is exactly the case the three candidates are for, i el toc d'un d'ells és un exemple nou (docs/90).
  out.confident = out.any && out.top[0].distance <= kNear &&
                  (out.count == 1 || (out.top[1].distance - out.top[0].distance) >= kMargin);
  return out;
}

uint16_t Model::remove_of(char text) {
  uint16_t removed = 0;
  uint16_t at = 0;
  for (uint16_t i = 0; i < count; ++i) {
    if (examples[i].text == text) {
      ++removed;
      continue;
    }
    if (at != i) examples[at] = examples[i];
    ++at;
  }
  count = at;
  return removed;
}

uint16_t Model::examples_of(char text) const {
  uint16_t n = 0;
  for (uint16_t i = 0; i < count; ++i) {
    if (examples[i].text == text) ++n;
  }
  return n;
}

// ------------------------------------------------------------------ el fitxer

size_t blob_bytes(const Model& model) { return kHeaderBytes + static_cast<size_t>(model.count) * kExampleBytes; }

namespace {

// Four cells in a byte, two bits each: thirteen bytes for a 7x7 grid, the last one with three bits spare. The place and the size of
// the example come after the label, the strokes and the aspect, and they are the three bytes this file grew by in the 0.195.0.
void pack(const Grid& grid, uint8_t* out) {
  memset(out, 0, 13);
  for (uint8_t i = 0; i < kCells; ++i) {
    const uint8_t level = (grid.cell[i] > kTwice) ? kTwice : grid.cell[i];
    out[i / 4] = static_cast<uint8_t>(out[i / 4] | static_cast<uint8_t>(level << ((i % 4) * 2)));
  }
}

Grid unpack(const uint8_t* in) {
  Grid grid;
  for (uint8_t i = 0; i < kCells; ++i) {
    grid.cell[i] = static_cast<uint8_t>((in[i / 4] >> ((i % 4) * 2)) & 3);
  }
  return grid;
}

}  // namespace

size_t save(const Model& model, uint8_t* out, size_t cap) {
  if (out == nullptr) return 0;
  const size_t need = blob_bytes(model);
  if (need > cap) return 0;
  memcpy(out, kMagic, sizeof(kMagic));
  out[4] = kSide;
  out[5] = kLevels;
  out[6] = static_cast<uint8_t>(model.count & 0xFF);
  out[7] = static_cast<uint8_t>((model.count >> 8) & 0xFF);
  uint8_t* at = out + kHeaderBytes;
  for (uint16_t i = 0; i < model.count; ++i) {
    pack(model.examples[i].grid, at);
    at[13] = static_cast<uint8_t>(model.examples[i].text);
    at[14] = model.examples[i].grid.strokes;
    at[15] = model.examples[i].grid.aspect;
    at[16] = model.examples[i].grid.at_x;
    at[17] = model.examples[i].grid.at_y;
    at[18] = model.examples[i].grid.size;
    at += kExampleBytes;
  }
  return need;
}

bool load(Model& model, const uint8_t* in, size_t len) {
  if (!model.usable()) return false;
  model.count = 0;
  if (in == nullptr || len < kHeaderBytes) return false;
  if (memcmp(in, kMagic, sizeof(kMagic)) != 0) return false;
  // **Una altra quadrícula és un altre model**: the cells of a 5x5 read as a 7x7 are a picture of nothing, so the blob is
  // refused with its own two numbers instead of being loaded as rubbish (docs/70).
  if (in[4] != kSide || in[5] != kLevels) return false;
  size_t n = static_cast<size_t>(in[6]) | (static_cast<size_t>(in[7]) << 8);
  const size_t there = (len - kHeaderBytes) / kExampleBytes;
  if (n > there) n = there;
  if (n > model.capacity) n = model.capacity;
  const uint8_t* at = in + kHeaderBytes;
  for (size_t i = 0; i < n; ++i) {
    Example one;
    one.grid = unpack(at);
    one.text = static_cast<char>(at[13]);
    one.grid.strokes = at[14];
    one.grid.aspect = at[15];
    one.grid.at_x = at[16];
    one.grid.at_y = at[17];
    one.grid.size = at[18];
    if (one.text != '\0') model.examples[model.count++] = one;
    at += kExampleBytes;
  }
  return true;
}

}  // namespace ink
