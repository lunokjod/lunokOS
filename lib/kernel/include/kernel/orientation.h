// One frame for the glass and for the fingers (docs/90).
//
// The touch controller counts in a frame of its own —540 wide and 960 tall on the Paper, which is what
// its configuration says— and the glass is painted in another —960 by 540—. A finger is only useful in
// the frame the app drew in, so the turn between the two is made **once**, here, and neither the apps
// nor the gestures ever see a coordinate of the chip.
//
// A quarter turn is all there is: the rotation is 0, 90, 180 or 270 degrees, and the sizes of the two
// frames say which of them can be true.
#ifndef LUNOK_KERNEL_ORIENTATION_H
#define LUNOK_KERNEL_ORIENTATION_H

#include <stdint.h>

namespace kernel {
namespace orientation {

struct Frame {
  uint16_t width;
  uint16_t height;
};

struct Point {
  uint16_t x;
  uint16_t y;
};

// True when a quarter turn of `degrees` takes a point of `from` into a frame of the same shape as `to`.
// It is how the system knows whether the rotation it has been given can be true at all: a turn of 180
// on the Paper would answer a frame of 540 by 960 and the glass is not that.
bool fits(const Frame& from, const Frame& to, uint16_t degrees);

// Turns `point` from the frame `from` into the frame it becomes, which is filled in `turned_frame`, and
// answers the point in it. **Clockwise**: 90 takes the top-left corner of `from` to the top-right of the
// turned frame, which is the turn a page makes when the board is turned to its left (docs/90).
bool turn(const Point& point, const Frame& from, uint16_t degrees, Frame& turned_frame, Point& out);

}  // namespace orientation
}  // namespace kernel

#endif  // LUNOK_KERNEL_ORIENTATION_H
