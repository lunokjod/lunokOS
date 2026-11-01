// The quarter turn between the frame of the fingers and the frame of the glass (docs/90).
//
// It is arithmetic and nothing else, so it is tried on the PC: a screen and a chip that disagree by a
// quarter turn show up here as a corner that lands in the wrong place, instead of as a page an app has
// to guess its way around on the board.
#include <stddef.h>

#include <unity.h>

#include "kernel/orientation.h"

namespace orientation = kernel::orientation;

void setUp(void) {}

void tearDown(void) {}

namespace {

// The Paper's two frames: the chip counts in 540×960 and the glass is 960×540.
const orientation::Frame kChip = {540, 960};
const orientation::Frame kGlass = {960, 540};

orientation::Point at(uint16_t x, uint16_t y) {
  orientation::Point point = {x, y};
  return point;
}

}  // namespace

// A quarter turn is the only one that makes the two frames the same shape.
static void test_only_a_quarter_turn_fits_the_paper() {
  TEST_ASSERT_TRUE(orientation::fits(kChip, kGlass, 90));
  TEST_ASSERT_TRUE(orientation::fits(kChip, kGlass, 270));
  TEST_ASSERT_FALSE(orientation::fits(kChip, kGlass, 0));
  TEST_ASSERT_FALSE(orientation::fits(kChip, kGlass, 180));
  TEST_ASSERT_FALSE(orientation::fits(kChip, kGlass, 45));
}

// Clockwise: the top-left corner of the chip's frame lands on the top-right of the glass.
static void test_a_quarter_turn_clockwise_takes_the_corners() {
  orientation::Frame turned;
  orientation::Point out;

  TEST_ASSERT_TRUE(orientation::turn(at(0, 0), kChip, 90, turned, out));
  TEST_ASSERT_EQUAL_UINT16(960, turned.width);
  TEST_ASSERT_EQUAL_UINT16(540, turned.height);
  TEST_ASSERT_EQUAL_UINT16(959, out.x);
  TEST_ASSERT_EQUAL_UINT16(0, out.y);

  TEST_ASSERT_TRUE(orientation::turn(at(539, 0), kChip, 90, turned, out));
  TEST_ASSERT_EQUAL_UINT16(959, out.x);
  TEST_ASSERT_EQUAL_UINT16(539, out.y);

  TEST_ASSERT_TRUE(orientation::turn(at(0, 959), kChip, 90, turned, out));
  TEST_ASSERT_EQUAL_UINT16(0, out.x);
  TEST_ASSERT_EQUAL_UINT16(0, out.y);
}

// The other way: the same corners, the other way round.
static void test_a_quarter_turn_the_other_way_takes_the_corners() {
  orientation::Frame turned;
  orientation::Point out;

  TEST_ASSERT_TRUE(orientation::turn(at(0, 0), kChip, 270, turned, out));
  TEST_ASSERT_EQUAL_UINT16(0, out.x);
  TEST_ASSERT_EQUAL_UINT16(539, out.y);

  TEST_ASSERT_TRUE(orientation::turn(at(0, 959), kChip, 270, turned, out));
  TEST_ASSERT_EQUAL_UINT16(959, out.x);
  TEST_ASSERT_EQUAL_UINT16(539, out.y);
}

// Half a turn and no turn keep the frame as it is, which on this board is not the glass' shape.
static void test_a_half_turn_keeps_the_frame() {
  orientation::Frame turned;
  orientation::Point out;
  TEST_ASSERT_TRUE(orientation::turn(at(10, 20), kChip, 180, turned, out));
  TEST_ASSERT_EQUAL_UINT16(540, turned.width);
  TEST_ASSERT_EQUAL_UINT16(960, turned.height);
  TEST_ASSERT_EQUAL_UINT16(529, out.x);
  TEST_ASSERT_EQUAL_UINT16(939, out.y);

  TEST_ASSERT_TRUE(orientation::turn(at(10, 20), kChip, 0, turned, out));
  TEST_ASSERT_EQUAL_UINT16(10, out.x);
  TEST_ASSERT_EQUAL_UINT16(20, out.y);
}

// A point that is not in the frame, and an angle that is not a quarter, are both refused: a coordinate
// nobody can place is not worth turning into another one.
static void test_a_point_outside_the_frame_and_an_angle_that_is_not_a_quarter() {
  orientation::Frame turned;
  orientation::Point out;
  TEST_ASSERT_FALSE(orientation::turn(at(540, 0), kChip, 90, turned, out));
  TEST_ASSERT_FALSE(orientation::turn(at(0, 960), kChip, 90, turned, out));
  TEST_ASSERT_FALSE(orientation::turn(at(0, 0), kChip, 45, turned, out));
  TEST_ASSERT_TRUE(orientation::turn(at(539, 959), kChip, 90, turned, out));
  TEST_ASSERT_EQUAL_UINT16(0, out.x);
  TEST_ASSERT_EQUAL_UINT16(539, out.y);
}

// **A chained turn may use the same object as the frame it came from and the frame it goes to**, and the
// kernel does exactly that: `turn(p, f, a, f, p)` is one frame both ways. The frame is then written before
// the point has been turned against it, so without a copy the point is turned against a frame that has
// already moved under it. Measured on the board (1/11, 0.18.0): a tap on the clock, raw 20,31, came out at
// the bottom right corner of the page, 539,900, and the touch answered nothing.
static void test_a_chained_turn_does_not_read_its_own_output() {
  orientation::Frame frame = kChip;
  orientation::Point point = at(20, 31);
  TEST_ASSERT_TRUE(orientation::turn(point, frame, 90, frame, point));
  // The same numbers as a turn into a frame of its own would give: the column of the chip's frame becomes
  // the row of the glass's.
  TEST_ASSERT_EQUAL_UINT16(960, frame.width);
  TEST_ASSERT_EQUAL_UINT16(540, frame.height);
  TEST_ASSERT_EQUAL_UINT16(959 - 31, point.x);
  TEST_ASSERT_EQUAL_UINT16(20, point.y);
}

// And the page of today needs no turn at all: the chip counts in the page's frame, so the finger of the
// clock comes back to where the clock is (docs/50, measured on the board the 1/11).
static void test_no_turn_leaves_the_finger_where_it_was() {
  // **The four corners of the page as the board gave them** (1/11, docs/95 E-6): a finger on the top left
  // —the clock—, on the top right, on the bottom left and on the bottom right, and the chip's own
  // numbers for each. No turn means these come back unchanged, which is what was measured: `x` to the right
  // and `y` downwards, no quarter turn and no mirror.
  const orientation::Point corners[4] = {at(46, 28), at(515, 49), at(31, 926), at(500, 920)};
  orientation::Frame turned;
  orientation::Point out;
  for (int i = 0; i < 4; ++i) {
    TEST_ASSERT_TRUE(orientation::turn(corners[i], kChip, 0, turned, out));
    TEST_ASSERT_EQUAL_UINT16(540, turned.width);
    TEST_ASSERT_EQUAL_UINT16(960, turned.height);
    TEST_ASSERT_EQUAL_UINT16(corners[i].x, out.x);
    TEST_ASSERT_EQUAL_UINT16(corners[i].y, out.y);
  }
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_only_a_quarter_turn_fits_the_paper);
  RUN_TEST(test_a_quarter_turn_clockwise_takes_the_corners);
  RUN_TEST(test_a_quarter_turn_the_other_way_takes_the_corners);
  RUN_TEST(test_a_half_turn_keeps_the_frame);
  RUN_TEST(test_a_point_outside_the_frame_and_an_angle_that_is_not_a_quarter);
  RUN_TEST(test_a_chained_turn_does_not_read_its_own_output);
  RUN_TEST(test_no_turn_leaves_the_finger_where_it_was);
  return UNITY_END();
}
