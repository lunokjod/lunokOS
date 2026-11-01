// The one turn between the frame of the fingers and the frame of the glass (docs/90).
#include "kernel/orientation.h"

namespace kernel {
namespace orientation {
namespace {

bool quarter(uint16_t degrees) {
  return degrees == 0 || degrees == 90 || degrees == 180 || degrees == 270;
}

// The frame a quarter turn produces. Turning 90 or 270 swaps the two sides; 0 and 180 do not.
Frame frame_after(const Frame& from, uint16_t degrees) {
  Frame out = from;
  if (degrees == 90 || degrees == 270) {
    out.width = from.height;
    out.height = from.width;
  }
  return out;
}

}  // namespace

bool fits(const Frame& from, const Frame& to, uint16_t degrees) {
  if (!quarter(degrees)) return false;
  const Frame after = frame_after(from, degrees);
  return after.width == to.width && after.height == to.height;
}

bool turn(const Point& point, const Frame& from, uint16_t degrees, Frame& turned_frame, Point& out) {
  if (!quarter(degrees)) return false;
  // **The frame and the point may each be handed in twice**: `from` and `turned_frame` can be the same
  // object, and so can `point` and `out` — a caller that chains one turn after another does both. Written
  // straight out, the result of the first coordinate then feeds the second (a tap on the clock, raw 20,31,
  // came out at the bottom right corner of the page, measured on the board the 1/11). Both are copied
  // first, and the copies are what the maths reads.
  const Frame base = from;
  const Point src = point;
  if (src.x >= base.width || src.y >= base.height) return false;
  turned_frame = frame_after(base, degrees);
  switch (degrees) {
    case 0:
      out = src;
      return true;
    case 90:
      // **Clockwise.** The column of `base` becomes the row of the result and the frame turns with it:
      // the top-left of the old frame lands on the top-right of the new one.
      out.x = static_cast<uint16_t>(base.height - 1 - src.y);
      out.y = src.x;
      return true;
    case 180:
      out.x = static_cast<uint16_t>(base.width - 1 - src.x);
      out.y = static_cast<uint16_t>(base.height - 1 - src.y);
      return true;
    case 270:
      out.x = src.y;
      out.y = static_cast<uint16_t>(base.width - 1 - src.x);
      return true;
    default:
      return false;
  }
}

}  // namespace orientation
}  // namespace kernel
