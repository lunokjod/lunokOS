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

bool raster(const Glyph& g, Grid& out) {
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
  // **Un toc no és una lletra**: a glyph with every point in the same place is a finger that pressed and nothing else.
  if (width <= 1 && height <= 1) return false;
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
  return true;
}

bool raster(const Point* points, uint16_t count, Grid& out) {
  Glyph g;
  if (points == nullptr || count == 0) return false;
  for (uint16_t i = 0; i < count; ++i) {
    if (!g.add(points[i].x, points[i].y)) break;
  }
  g.lift();
  return raster(g, out);
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

// Four cells in a byte, two bits each: thirteen bytes for a 7x7 grid, the last one with three bits spare.
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
    if (one.text != '\0') model.examples[model.count++] = one;
    at += kExampleBytes;
  }
  return true;
}

}  // namespace ink
