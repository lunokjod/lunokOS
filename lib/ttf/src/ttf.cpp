// A TrueType font read the way a page needs it: see `ttf.h` for the shape of the calls.
//
// The file is a directory of tables and nothing else: every table says where it starts and how long it is, so
// this reader finds seven of them once, remembers their offsets, and never copies the file. A glyph is a list
// of contours of points in font units, some of them on the curve and some of them between two on-curve points
// —that is what a quadratic curve is here— and drawing it is flattening those curves to lines and filling the
// rows of the block that the caller gave, one pixel at a time.
#include "ttf/ttf.h"

#include <math.h>
#include <string.h>

namespace ttf {
namespace {

uint32_t u16(const uint8_t* p) { return (static_cast<uint32_t>(p[0]) << 8) | p[1]; }

int32_t s16(const uint8_t* p) {
  return static_cast<int32_t>(static_cast<int16_t>((static_cast<uint32_t>(p[0]) << 8) | p[1]));
}

uint32_t u32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

// **A point of an outline**: where it is, in font units, and whether the curve goes through it or is bent by
// it (TrueType has quadratic curves and no other kind).
struct Point {
  float x;
  float y;
  bool on;
};

// **An edge of a glyph that is being drawn**, in pixels, already on the page: two ends and the direction the
// walk goes down, which is what the winding rule counts.
struct Edge {
  float x0;
  float y0;
  float x1;
  float y1;
};

// The blocks of this reader, static and never on the stack: a glyph with more points or more edges than these
// is refused with a reason instead of being drawn wrong (docs/75).
Point g_points[256];
Edge g_edges[kMaxEdges];
float g_crossing[kMaxEdges];
int8_t g_direction[kMaxEdges];
uint32_t g_used_edges = 0;
uint32_t g_used_points = 0;
// **A glyph with more edges than the block holds is refused and not drawn with the ones that fitted**: half a
// letter is worse than none, and a page would not say which of the two it got (docs/30).
bool g_overflow = false;

// How many straight pieces a quadratic curve is cut into: six is smooth at the sizes a page is read at and
// keeps the number of edges bounded (docs/30).
const uint32_t kCurveSteps = 6;

Point midpoint(const Point& a, const Point& b) {
  Point m;
  m.x = (a.x + b.x) * 0.5f;
  m.y = (a.y + b.y) * 0.5f;
  m.on = true;
  return m;
}

void add_edge(float x0, float y0, float x1, float y1) {
  if (g_used_edges >= kMaxEdges) {
    g_overflow = true;
    return;
  }
  Edge& edge = g_edges[g_used_edges++];
  edge.x0 = x0;
  edge.y0 = y0;
  edge.x1 = x1;
  edge.y1 = y1;
}

// A quadratic curve from `a` to `b` bent by `control`, cut into straight pieces.
void add_quad(const Point& a, const Point& control, const Point& b) {
  float last_x = a.x;
  float last_y = a.y;
  for (uint32_t step = 1; step <= kCurveSteps; ++step) {
    const float t = static_cast<float>(step) / static_cast<float>(kCurveSteps);
    const float u = 1.0f - t;
    const float x = u * u * a.x + 2.0f * u * t * control.x + t * t * b.x;
    const float y = u * u * a.y + 2.0f * u * t * control.y + t * t * b.y;
    add_edge(last_x, last_y, x, y);
    last_x = x;
    last_y = y;
  }
}

void add_line(const Point& a, const Point& b) { add_edge(a.x, a.y, b.x, b.y); }

// **One contour, walked**: the pen starts on a point that the curve goes through (a contour of nothing but
// controls starts in the middle of its first two) and every control bends the line into the next point, or
// into the middle of itself and the next control, which is the point TrueType leaves out (docs/30).
void flatten_contour(const Point* points, uint32_t count) {
  uint32_t start = 0;
  bool found = false;
  for (uint32_t i = 0; i < count; ++i) {
    if (points[i].on) {
      start = i;
      found = true;
      break;
    }
  }
  const Point begin = found ? points[start] : midpoint(points[0], points[1]);
  Point pen = begin;
  Point control;
  bool bent = false;
  uint32_t walked = found ? 1 : 0;
  while (walked < count) {
    const Point next = points[(start + walked) % count];
    ++walked;
    if (next.on) {
      if (bent) {
        add_quad(pen, control, next);
        bent = false;
      } else {
        add_line(pen, next);
      }
      pen = next;
    } else if (bent) {
      const Point middle = midpoint(control, next);
      add_quad(pen, control, middle);
      pen = middle;
      control = next;
    } else {
      control = next;
      bent = true;
    }
  }
  if (bent) {
    add_quad(pen, control, begin);
  } else {
    add_line(pen, begin);
  }
}

// **The outline of a glyph at a size, as edges in pixels**: the pen is at the origin, `y` goes up in the file
// and down here, and `scale` is how many pixels a unit is worth.
Status outline(const Face& face, uint16_t glyph, uint16_t px, float& min_x, float& min_y, float& max_x,
               float& max_y) {
  const uint32_t at = face.loca;
  uint32_t begin = 0;
  uint32_t end = 0;
  if (face.long_loca) {
    if (at + 4 * (static_cast<uint32_t>(glyph) + 1) > face.length) return kBadData;
    begin = u32(face.data + at + 4 * glyph);
    end = u32(face.data + at + 4 * (static_cast<uint32_t>(glyph) + 1));
  } else {
    if (at + 2 * (static_cast<uint32_t>(glyph) + 1) > face.length) return kBadData;
    begin = u16(face.data + at + 2 * glyph) * 2u;
    end = u16(face.data + at + 2 * (static_cast<uint32_t>(glyph) + 1)) * 2u;
  }
  if (end <= begin) return kNoGlyph;  // an empty glyph, like a space
  if (begin > face.length || end > face.length || end - begin < 10) return kBadData;
  const uint8_t* p = face.data + face.glyf + begin;
  const int32_t contours = s16(p);
  if (contours < 0) return kComposite;
  if (contours == 0) return kNoGlyph;
  if (contours > 64) return kBadData;
  const uint32_t ends_at = 10;
  if (ends_at + static_cast<uint32_t>(contours) * 2 + 2 > end - begin) return kBadData;
  uint32_t points = 0;
  for (int32_t i = 0; i < contours; ++i) {
    const uint32_t last = u16(p + ends_at + static_cast<uint32_t>(i) * 2);
    if (i == contours - 1) points = last + 1;
  }
  if (points == 0 || points > 256) return kBadData;
  uint32_t cursor = ends_at + static_cast<uint32_t>(contours) * 2;
  const uint32_t instructions = u16(p + cursor);
  cursor += 2 + instructions;
  if (cursor > end - begin) return kBadData;
  // **The flags**, which say for every point whether the curve goes through it and how wide its two numbers
  // are, and a flag that repeats itself as many times as the next byte says.
  static uint8_t flags[256];
  uint32_t got = 0;
  while (got < points) {
    if (cursor >= end - begin) return kBadData;
    const uint8_t flag = p[cursor++];
    flags[got++] = flag;
    if ((flag & 0x08) != 0) {
      if (cursor >= end - begin) return kBadData;
      uint32_t repeat = p[cursor++];
      while (repeat > 0 && got < points) {
        flags[got++] = flag;
        --repeat;
      }
    }
  }
  // The x of every point, then the y, each as a delta that may be a byte or two.
  float x = 0.0f;
  for (uint32_t i = 0; i < points; ++i) {
    const uint8_t flag = flags[i];
    if ((flag & 0x02) != 0) {
      if (cursor >= end - begin) return kBadData;
      const int32_t delta = p[cursor++];
      x += ((flag & 0x10) != 0) ? static_cast<float>(delta) : -static_cast<float>(delta);
    } else if ((flag & 0x10) == 0) {
      if (cursor + 2 > end - begin) return kBadData;
      x += static_cast<float>(s16(p + cursor));
      cursor += 2;
    }
    g_points[i].x = x;
    g_points[i].on = (flag & 0x01) != 0;
  }
  float y = 0.0f;
  for (uint32_t i = 0; i < points; ++i) {
    const uint8_t flag = flags[i];
    if ((flag & 0x04) != 0) {
      if (cursor >= end - begin) return kBadData;
      const int32_t delta = p[cursor++];
      y += ((flag & 0x20) != 0) ? static_cast<float>(delta) : -static_cast<float>(delta);
    } else if ((flag & 0x20) == 0) {
      if (cursor + 2 > end - begin) return kBadData;
      y += static_cast<float>(s16(p + cursor));
      cursor += 2;
    }
    g_points[i].y = y;
  }
  g_used_points = points;
  // **To pixels, with the pen at the origin and the y the other way round**, which is the only place this
  // reader decides what a size means: `px` pixels of body for `units_per_em` units of the file.
  const float scale = static_cast<float>(px) / static_cast<float>(face.units_per_em == 0 ? 1 : face.units_per_em);
  min_x = 1e9f;
  min_y = 1e9f;
  max_x = -1e9f;
  max_y = -1e9f;
  for (uint32_t i = 0; i < points; ++i) {
    g_points[i].x *= scale;
    g_points[i].y = -g_points[i].y * scale;
    if (g_points[i].x < min_x) min_x = g_points[i].x;
    if (g_points[i].x > max_x) max_x = g_points[i].x;
    if (g_points[i].y < min_y) min_y = g_points[i].y;
    if (g_points[i].y > max_y) max_y = g_points[i].y;
  }
  g_used_edges = 0;
  g_overflow = false;
  uint32_t first = 0;
  for (int32_t i = 0; i < contours; ++i) {
    const uint32_t last = u16(p + ends_at + static_cast<uint32_t>(i) * 2);
    flatten_contour(g_points + first, last - first + 1);
    first = last + 1;
  }
  if (g_overflow) return kNoRoom;
  return kOk;
}

}  // namespace

const char* open(const uint8_t* data, uint32_t length, Face& out) {
  if (data == nullptr || length < 12) return "there is nothing to read";
  const uint32_t version = u32(data);
  if (version == 0x4F54544Fu) return "a font of CFF outlines is not read";
  if (version != 0x00010000u && version != 0x74727565u) return "not a truetype font";
  const uint32_t tables = u16(data + 4);
  if (tables == 0 || 12 + tables * 16 > length) return "the table directory is cut";
  out.data = data;
  out.length = length;
  out.cmap = 0;
  out.glyf = 0;
  out.loca = 0;
  out.hmtx = 0;
  out.maxp = 0;
  uint32_t head = 0;
  uint32_t hhea = 0;
  for (uint32_t i = 0; i < tables; ++i) {
    const uint8_t* entry = data + 12 + i * 16;
    const uint32_t tag = u32(entry);
    const uint32_t offset = u32(entry + 8);
    const uint32_t size = u32(entry + 12);
    if (offset > length || offset + size > length) return "a table starts past the end of the file";
    // The four bytes of a tag are its name, read as a number: `cmap` is 0x636D6170.
    if (tag == 0x636D6170u) {
      out.cmap = offset;
    } else if (tag == 0x676C7966u) {
      out.glyf = offset;
    } else if (tag == 0x6C6F6361u) {
      out.loca = offset;
    } else if (tag == 0x686D7478u) {
      out.hmtx = offset;
    } else if (tag == 0x6D617870u) {
      out.maxp = offset;
    } else if (tag == 0x68656164u) {
      head = offset;
    } else if (tag == 0x68686561u) {
      hhea = offset;
    }
  }
  if (out.cmap == 0 || out.glyf == 0 || out.loca == 0 || out.hmtx == 0 || out.maxp == 0 || head == 0 ||
      hhea == 0) {
    return "a font needs its head, its hhea, its hmtx, its maxp, its loca, its glyf and its cmap";
  }
  out.units_per_em = static_cast<uint16_t>(u16(data + head + 18));
  out.long_loca = s16(data + head + 50) != 0;
  out.glyphs = static_cast<uint16_t>(u16(data + out.maxp + 4));
  out.ascent = static_cast<int16_t>(s16(data + hhea + 4));
  out.descent = static_cast<int16_t>(s16(data + hhea + 6));
  out.line_gap = static_cast<int16_t>(s16(data + hhea + 8));
  out.metrics = static_cast<uint16_t>(u16(data + hhea + 34));
  if (out.units_per_em == 0 || out.glyphs == 0 || out.metrics == 0) return "a font with no size or no glyph";
  // **The character map that a text uses**: the segment map (format 4) of the unicode subtable.
  const uint32_t count = u16(data + out.cmap + 2);
  uint32_t chosen = 0;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t platform = u16(data + out.cmap + 4 + i * 8);
    const uint32_t encoding = u16(data + out.cmap + 6 + i * 8);
    const uint32_t offset = out.cmap + u32(data + out.cmap + 8 + i * 8);
    if (offset + 8 > length) return "a character map starts past the end of the file";
    if (u16(data + offset) != 4) continue;
    if ((platform == 3 && encoding == 1) || (platform == 0)) {
      chosen = offset;
      if (platform == 3) break;
    }
  }
  if (chosen == 0) return "a font without a character map of format 4 is not read";
  out.format4 = true;
  out.cmap = chosen;
  return nullptr;
}

Status glyph_of(const Face& face, uint8_t code, uint16_t& glyph) {
  glyph = 0;
  if (!face.format4 || face.cmap + 16 > face.length) return kBadData;
  const uint8_t* p = face.data + face.cmap;
  const uint32_t segments = u16(p + 6) / 2;
  if (segments == 0) return kBadData;
  const uint32_t ends = 14;
  const uint32_t starts = ends + segments * 2 + 2;
  const uint32_t deltas = starts + segments * 2;
  const uint32_t ranges = deltas + segments * 2;
  if (face.cmap + ranges + segments * 2 > face.length) return kBadData;
  for (uint32_t i = 0; i < segments; ++i) {
    const uint32_t last = u16(p + ends + i * 2);
    if (code > last) continue;
    const uint32_t first = u16(p + starts + i * 2);
    if (code < first) return kNoGlyph;
    const int32_t delta = s16(p + deltas + i * 2);
    const uint32_t range = u16(p + ranges + i * 2);
    if (range == 0) {
      if (delta == 0) return kNoGlyph;
      glyph = static_cast<uint16_t>((code + delta) & 0xFFFF);
      return glyph == 0 ? kNoGlyph : kOk;
    }
    // The offset of a range is counted from itself, in words, which is the one awkward thing in the format.
    const uint32_t at = ranges + i * 2 + range + (code - first) * 2;
    if (face.cmap + at + 2 > face.length) return kBadData;
    const uint32_t found = u16(p + at);
    if (found == 0) return kNoGlyph;
    glyph = static_cast<uint16_t>((found + delta) & 0xFFFF);
    return glyph == 0 ? kNoGlyph : kOk;
  }
  return kNoGlyph;
}

uint16_t advance(const Face& face, uint16_t glyph, uint16_t px) {
  const uint32_t at = face.hmtx;
  const uint32_t which = (glyph < face.metrics) ? glyph : (face.metrics == 0 ? 0 : face.metrics - 1);
  if (at + which * 4 + 2 > face.length) return 1;
  const uint32_t units = u16(face.data + at + which * 4);
  const uint32_t body = face.units_per_em == 0 ? 1 : face.units_per_em;
  const uint32_t pixels = (units * px + body / 2) / body;
  return static_cast<uint16_t>(pixels == 0 ? 1 : pixels);
}

Status render(const Face& face, uint16_t glyph, uint16_t px, uint8_t* rows, uint16_t row_bytes,
              uint16_t max_width, uint16_t max_height, Bitmap& box) {
  box.width = 0;
  box.height = 0;
  box.left = 0;
  box.top = 0;
  if (rows == nullptr || px == 0) return kBadData;
  g_used_edges = 0;
  float min_x = 0.0f;
  float min_y = 0.0f;
  float max_x = 0.0f;
  float max_y = 0.0f;
  const Status status = outline(face, glyph, px, min_x, min_y, max_x, max_y);
  if (status != kOk) return status;
  if (g_used_edges == 0) return kNoGlyph;
  // **The box of the ink**, rounded out to whole pixels: where the pen has to go so the ink lands on the grid.
  const int32_t left = static_cast<int32_t>(floorf(min_x));
  const int32_t top = static_cast<int32_t>(floorf(min_y));
  const int32_t right = static_cast<int32_t>(ceilf(max_x));
  const int32_t bottom = static_cast<int32_t>(ceilf(max_y));
  const uint32_t width = static_cast<uint32_t>(right - left);
  const uint32_t height = static_cast<uint32_t>(bottom - top);
  if (width == 0 || height == 0) return kNoGlyph;
  if (width > max_width || height > max_height) return kNoRoom;
  if (static_cast<uint32_t>(row_bytes) * height > 4096) return kNoRoom;
  memset(rows, 0, static_cast<size_t>(row_bytes) * height);
  box.width = static_cast<uint16_t>(width);
  box.height = static_cast<uint16_t>(height);
  box.left = static_cast<int16_t>(left);
  box.top = static_cast<int16_t>(top);
  // **One row of pixels at a time, by the winding rule**: every edge of the glyph that crosses the middle of
  // the row says which way it goes, and a pixel is ink when the count of them to its left is not zero. That is
  // what makes a hole in a letter a hole and not a second blob (docs/30).
  for (uint32_t row = 0; row < height; ++row) {
    const float y = static_cast<float>(top) + static_cast<float>(row) + 0.5f;
    uint32_t crossings = 0;
    for (uint32_t i = 0; i < g_used_edges; ++i) {
      const Edge& edge = g_edges[i];
      const bool down = (edge.y0 <= y && edge.y1 > y);
      const bool up = (edge.y1 <= y && edge.y0 > y);
      if (!down && !up) continue;
      const float span = edge.y1 - edge.y0;
      if (span == 0.0f) continue;
      const float t = (y - edge.y0) / span;
      if (crossings >= kMaxEdges) break;
      g_crossing[crossings] = edge.x0 + t * (edge.x1 - edge.x0);
      g_direction[crossings] = down ? 1 : -1;
      ++crossings;
    }
    // The crossings in the order they are met, which is what the walk below needs.
    for (uint32_t i = 1; i < crossings; ++i) {
      const float x = g_crossing[i];
      const int8_t direction = g_direction[i];
      uint32_t j = i;
      while (j > 0 && g_crossing[j - 1] > x) {
        g_crossing[j] = g_crossing[j - 1];
        g_direction[j] = g_direction[j - 1];
        --j;
      }
      g_crossing[j] = x;
      g_direction[j] = direction;
    }
    int32_t wind = 0;
    float span_start = 0.0f;
    for (uint32_t i = 0; i < crossings; ++i) {
      const int32_t before = wind;
      wind += g_direction[i];
      if (before == 0 && wind != 0) {
        span_start = g_crossing[i];
      } else if (before != 0 && wind == 0) {
        // The pixels whose middle is inside the span, from the first of them to the last.
        int32_t first = static_cast<int32_t>(ceilf(span_start - 0.5f)) - left;
        int32_t last = static_cast<int32_t>(ceilf(g_crossing[i] - 0.5f)) - 1 - left;
        if (first < 0) first = 0;
        if (last >= static_cast<int32_t>(width)) last = static_cast<int32_t>(width) - 1;
        for (int32_t x = first; x <= last; ++x) {
          rows[row * row_bytes + (x >> 3)] |= static_cast<uint8_t>(0x80u >> (x & 7));
        }
      }
    }
  }
  return kOk;
}

uint32_t edges_used() { return g_used_edges; }

}  // namespace ttf
