// **The touch panel of the bench**: a GT911 that scans by itself, hands a frame over once, and costs
// what the bus of the board costs (docs/50, docs/85).
//
// The chip is checked here and not on the board because what it has to say is arithmetic and timing: a
// frame that is taken cannot be taken twice, a finger that lands is not there until the chip scans, and
// a read is three transactions on a hundred-kilohertz bus. What the kernel makes of a frame —a tap, a
// long press, a pinch— is tried through the same panel, in the frame of the view.
#include <stddef.h>
#include <string.h>

#include <unity.h>

#include "emu/gt911.h"
#include "emu/options.h"
#include "emu/surface.h"
#include "hal/hal.h"
#include "kernel/bar.h"
#include "kernel/display.h"
#include "kernel/touch.h"
#include "kernel/ui.h"
#include "pal/pal.h"

namespace ui = kernel::ui;

void setUp(void) {}

void tearDown(void) {}

namespace {

bool g_booted = false;

// A bench that is the Paper with its panel on and the factory page up: the chip counts in 540x960 and
// that is the page's own frame, so a finger lands where it looks like it lands (docs/50, docs/95 E-6).
void boot_with_a_touch_panel() {
  if (g_booted) return;
  emu::Options opts = emu::options();
  // **The board that has the panel**, and the frame it counts in: a GT911 is the Paper's and the
  // default model of the bench is the ATOM, which has no glass to put a finger on (docs/50, docs/85).
  opts.board = "m5stack-paper";
  opts.display = true;
  opts.touch = true;
  opts.read_delay_us = 0;
  emu::set_options(opts);
  kernel::display::boot(hal::platform());
  kernel::display::set_rotation(270);
  kernel::bar::boot(hal::platform());
  ui::boot(hal::platform(), kernel::bar::height());
  // **Where the app's view is**: the page under the bar of the system, which is where a finger of the
  // page has to land after the offset (docs/30, docs/50).
  ui::place(0, kernel::bar::height());
  ui::attach();
  TEST_ASSERT_TRUE(kernel::touch::boot(hal::platform()));
  g_booted = true;
}

emu::Gt911Panel& chip() { return *emu::touch_panel(); }

// **A turn of the kernel as far as the glass is concerned**: the queue waits and the touch module reads
// a frame, which is what the kernel does once per turn (docs/30, docs/85).
void turn(uint32_t ms) {
  pal::Event event;
  pal::system().queue()->pop(event, static_cast<int32_t>(ms));
  kernel::touch::poll();
}

void wait_ms(int32_t ms) {
  pal::Event event;
  pal::system().queue()->pop(event, ms);
}

}  // namespace

// **A read costs what it puts on the wire**: the status, the frame of the points and the zero that
// releases them, at the hundred kilohertz of this board and nine bits a byte (docs/50). Nobody on the
// glass is two transactions; one finger is eight more bytes and another transaction; two are sixteen.
static void test_a_read_costs_the_bus_it_uses() {
  TEST_ASSERT_EQUAL_UINT32(800, emu::Gt911Panel::read_us(0));
  TEST_ASSERT_EQUAL_UINT32(1830, emu::Gt911Panel::read_us(1));
  TEST_ASSERT_EQUAL_UINT32(2550, emu::Gt911Panel::read_us(2));
  // **Seven hundred and twenty of those are the wire alone** at ninety microseconds a byte: eight of
  // them for the status and the release.
  TEST_ASSERT_EQUAL_UINT32(720, 8 * 90);
  // The probe of the boot is the product id and the 186 bytes of configuration: seventeen milliseconds
  // on a bus this slow, which is why it happens once (docs/50).
  TEST_ASSERT_UINT32_WITHIN(200u, 17850u, emu::Gt911Panel::probe_us());
}

// **One frame per scan**: the chip hands over the frame of a scan once and the driver takes it away, so
// a second read inside the same window finds nothing while a finger is on the glass (docs/50). The
// window is made long here so that "straight away" is a fact and not a race with the clock.
static void test_the_panel_hands_over_one_frame_per_scan() {
  boot_with_a_touch_panel();
  emu::Options opts = emu::options();
  opts.touch_scan_ms = 200;
  emu::set_options(opts);
  chip().reset();
  TEST_ASSERT_EQUAL_UINT32(200, chip().scan_ms());
  hal::TouchPoint frame[kernel::touch::kMaxPoints];
  TEST_ASSERT_TRUE(chip().finger_down(0, 100, 200));
  TEST_ASSERT_EQUAL_UINT8(1, chip().read(frame, kernel::touch::kMaxPoints));
  TEST_ASSERT_EQUAL_UINT16(100, frame[0].x);
  TEST_ASSERT_EQUAL_UINT16(200, frame[0].y);
  TEST_ASSERT_EQUAL_UINT32(1, chip().frames());

  // The same window again, twice: the frame was taken and the chip has not scanned since.
  TEST_ASSERT_EQUAL_UINT8(0, chip().read(frame, kernel::touch::kMaxPoints));
  TEST_ASSERT_EQUAL_UINT8(0, chip().read(frame, kernel::touch::kMaxPoints));
  TEST_ASSERT_EQUAL_UINT32(2, chip().missed());
  TEST_ASSERT_EQUAL_UINT32(1, chip().frames());

  // One scan later there is another one, and it is the same finger: this is what makes a press a press.
  wait_ms(220);
  TEST_ASSERT_EQUAL_UINT8(1, chip().read(frame, kernel::touch::kMaxPoints));
  TEST_ASSERT_EQUAL_UINT32(2, chip().frames());
  opts = emu::options();
  opts.touch_scan_ms = 0;
  emu::set_options(opts);
}

// **The configuration of this unit**: two fingers and not the five the frame holds, and the contact the
// chip measured on the board, 28 and 30 (docs/95, E-6).
static void test_the_panel_reports_the_two_fingers_of_its_configuration() {
  boot_with_a_touch_panel();
  chip().reset();
  hal::TouchPoint frame[kernel::touch::kMaxPoints];
  TEST_ASSERT_EQUAL_UINT8(2, chip().max_points());
  TEST_ASSERT_EQUAL_UINT16(540, chip().width());
  TEST_ASSERT_EQUAL_UINT16(960, chip().height());

  TEST_ASSERT_TRUE(chip().finger_down(0, 10, 20));
  TEST_ASSERT_TRUE(chip().finger_down(1, 300, 400));
  TEST_ASSERT_FALSE_MESSAGE(chip().finger_down(2, 0, 0), "this panel was told to report two fingers");
  TEST_ASSERT_EQUAL_UINT8(2, chip().read(frame, kernel::touch::kMaxPoints));
  TEST_ASSERT_EQUAL_UINT16(10, frame[0].x);
  TEST_ASSERT_EQUAL_UINT16(20, frame[0].y);
  TEST_ASSERT_EQUAL_UINT16(300, frame[1].x);
  TEST_ASSERT_EQUAL_UINT16(28, frame[0].size);
  TEST_ASSERT_EQUAL_UINT16(30, frame[1].size);

  // A point that is not on the glass is not a finger, and a finger that is not down cannot move.
  TEST_ASSERT_FALSE(chip().finger_down(0, 540, 10));
  TEST_ASSERT_FALSE(chip().finger_move(1, 540, 10));
  TEST_ASSERT_TRUE(chip().finger_move(1, 320, 420));
  uint16_t x = 0;
  uint16_t y = 0;
  TEST_ASSERT_TRUE(chip().finger_at(1, x, y));
  TEST_ASSERT_EQUAL_UINT16(320, x);
  TEST_ASSERT_EQUAL_UINT16(420, y);
}

// **A finger that leaves is gone**: after a scan the chip has no frame to hand over, which is the
// "nobody is touching" a caller has to read as a finger that went up (docs/50).
static void test_a_finger_that_leaves_is_gone_after_a_scan() {
  boot_with_a_touch_panel();
  chip().reset();
  hal::TouchPoint frame[kernel::touch::kMaxPoints];
  TEST_ASSERT_TRUE(chip().finger_down(0, 50, 60));
  TEST_ASSERT_EQUAL_UINT8(1, chip().read(frame, kernel::touch::kMaxPoints));
  TEST_ASSERT_TRUE(chip().finger_up(0));
  TEST_ASSERT_FALSE_MESSAGE(chip().finger_up(0), "a finger that is not down is not lifted twice");
  wait_ms(15);
  TEST_ASSERT_EQUAL_UINT8(0, chip().read(frame, kernel::touch::kMaxPoints));
  TEST_ASSERT_EQUAL_UINT32(1, chip().frames());
  TEST_ASSERT_EQUAL_UINT8(0, chip().fingers_down());
}

// **A slow chip against a fast caller**: a panel that reports every forty milliseconds and a kernel
// that reads every twenty finds nothing on every other turn, which is the hardware problem this model
// exists to make visible (docs/85). The kernel must survive it, and the chip must count it.
static void test_a_panel_slower_than_the_turn_answers_nothing_half_the_time() {
  boot_with_a_touch_panel();
  emu::Options opts = emu::options();
  opts.touch_scan_ms = 40;
  emu::set_options(opts);
  chip().reset();
  TEST_ASSERT_EQUAL_UINT32(40, chip().scan_ms());
  hal::TouchPoint frame[kernel::touch::kMaxPoints];
  TEST_ASSERT_TRUE(chip().finger_down(0, 20, 30));
  uint8_t seen = 0;
  for (int i = 0; i < 8; ++i) {
    seen = static_cast<uint8_t>(seen + chip().read(frame, kernel::touch::kMaxPoints));
    wait_ms(20);
  }
  // Eight turns and fewer frames than that: the glass scans less often than the kernel reads, and the
  // turns that find nothing are the hardware saying nobody while the finger is there.
  TEST_ASSERT_TRUE_MESSAGE(seen < 8, "a panel that reports every forty milliseconds cannot give eight frames");
  TEST_ASSERT_TRUE(seen >= 2);
  TEST_ASSERT_TRUE(chip().missed() > 0);
  opts = emu::options();
  opts.touch_scan_ms = 0;
  emu::set_options(opts);
}

// **The whole path**: a finger on the panel, the kernel reading it in its turns, and the gesture coming
// out in the frame of the view —the page starts under the bar of the system— with the length of the
// press. The threshold is a second, so this is a second of the bench's own time (docs/30, docs/50).
static void test_the_kernel_turns_a_finger_into_a_long_press() {
  boot_with_a_touch_panel();
  chip().reset();
  TEST_ASSERT_EQUAL_UINT16(28, ui::canvas().y);
  TEST_ASSERT_TRUE(chip().finger_down(0, 100, 200));
  kernel::touch::Gesture gesture;
  bool said_long = false;
  int32_t waited = 0;
  while (waited < 1500 && !said_long) {
    turn(20);
    waited += 20;
    kernel::touch::Gesture now;
    if (kernel::touch::take_gesture(now) && now.kind != nullptr) {
      if (strcmp(now.kind, "long") == 0) {
        gesture = now;
        said_long = true;
      }
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(said_long, "a finger held for a second is not a long press");
  // **In the view's frame**: the finger went down at 100,200 of the page and the view starts at row 28.
  TEST_ASSERT_EQUAL_UINT16(100, gesture.x);
  TEST_ASSERT_EQUAL_UINT16(172, gesture.y);
  TEST_ASSERT_TRUE(gesture.held_ms >= 1000);
  TEST_ASSERT_EQUAL_UINT8(1, gesture.fingers);
  TEST_ASSERT_TRUE(chip().finger_up(0));
}

// **One switch for every wait of this bench**: with the times on, a read of the panel sleeps the bus it
// uses; with them off —`--no-delay`, and every test— the same read is the model and asks the clock for
// nothing (docs/85).
static void test_the_waits_are_one_switch() {
  boot_with_a_touch_panel();
  chip().reset();
  hal::TouchPoint frame[kernel::touch::kMaxPoints];
  emu::Options opts = emu::options();
  opts.delays = false;
  emu::set_options(opts);
  int64_t started = pal::system().micros();
  (void)chip().read(frame, kernel::touch::kMaxPoints);
  const int64_t quick = pal::system().micros() - started;
  TEST_ASSERT_TRUE_MESSAGE(quick < 500, "a read with the waits off asked the clock for time");

  opts.delays = true;
  emu::set_options(opts);
  started = pal::system().micros();
  (void)chip().read(frame, kernel::touch::kMaxPoints);
  const int64_t paid = pal::system().micros() - started;
  TEST_ASSERT_TRUE_MESSAGE(paid >= 800, "a read with the waits on did not pay the bus");

  opts.delays = false;
  emu::set_options(opts);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_a_read_costs_the_bus_it_uses);
  RUN_TEST(test_the_panel_hands_over_one_frame_per_scan);
  RUN_TEST(test_the_panel_reports_the_two_fingers_of_its_configuration);
  RUN_TEST(test_a_finger_that_leaves_is_gone_after_a_scan);
  RUN_TEST(test_a_panel_slower_than_the_turn_answers_nothing_half_the_time);
  RUN_TEST(test_the_waits_are_one_switch);
  RUN_TEST(test_the_kernel_turns_a_finger_into_a_long_press);
  return UNITY_END();
}
