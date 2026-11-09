// **The touch panel of the emulator**: a GT911 that scans by itself, hands a frame over once, and costs
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

// An emulator that is the Paper with its panel on and the factory page up: the chip counts in 540x960 and
// that is the page's own frame, so a finger lands where it looks like it lands (docs/50, docs/95 E-6).
void boot_with_a_touch_panel() {
  if (g_booted) {
    TEST_ASSERT_TRUE(kernel::touch::boot(hal::platform()));
    return;
  }
  emu::Options opts = emu::options();
  // **The board that has the panel**, and the frame it counts in: a GT911 is the Paper's and the
  // default model of the emulator is the ATOM, which has no glass to put a finger on (docs/50, docs/85).
  opts.board = "m5stack-paper";
  opts.display = true;
  opts.touch = true;
  emu::set_options(opts);
  kernel::display::boot(hal::platform());
  kernel::display::set_rotation(270);
  kernel::bar::boot(hal::platform());
  ui::boot(hal::platform(), kernel::bar::height());
  // **Where the app's view is**: the page under the bar of the system, which is where a finger of the
  // page has to land after the offset (docs/30, docs/50).
  ui::place(0, kernel::bar::height());
  ui::attach();
  // **The module of the fingers is booted by every case and not once for the file**: it keeps a queue of
  // gestures, the finger it is following and the count of the taps of a triple, and a case that inherits the
  // finger of the case before it is a case measuring that one —the lift of the long press arrived inside the
  // three taps and the triple was never counted (found red on the 8/11, docs/30).
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

// **A tap that begins and ends between two reads is still the frame of the last scan**: the chip scans the glass by
// itself and holds what it found until a read takes it, so a quick click of the window —which the emulator pours into
// the panel whole, a press and a release inside one turn of the kernel— is a press and not nothing at all (found on
// the emulator, 4/11, docs/50, docs/85).
static void test_a_tap_between_two_reads_is_still_a_frame() {
  boot_with_a_touch_panel();
  chip().reset();
  hal::TouchPoint frame[kernel::touch::kMaxPoints];
  TEST_ASSERT_TRUE(chip().finger_down(0, 30, 40));
  TEST_ASSERT_TRUE(chip().finger_up(0));
  TEST_ASSERT_EQUAL_UINT8(0, chip().fingers_down());
  TEST_ASSERT_EQUAL_UINT8(1, chip().read(frame, kernel::touch::kMaxPoints));
  TEST_ASSERT_EQUAL_UINT16(30, frame[0].x);
  TEST_ASSERT_EQUAL_UINT16(40, frame[0].y);
  TEST_ASSERT_EQUAL_UINT32(1, chip().frames());
  // **One frame and not two**: the tap was taken, and the scans that follow find nobody.
  wait_ms(15);
  TEST_ASSERT_EQUAL_UINT8(0, chip().read(frame, kernel::touch::kMaxPoints));
  wait_ms(15);
  TEST_ASSERT_EQUAL_UINT8(0, chip().read(frame, kernel::touch::kMaxPoints));
  TEST_ASSERT_EQUAL_UINT32(1, chip().frames());
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
// press. The threshold is a second, so this is a second of the emulator's own time (docs/30, docs/50).
static void test_the_kernel_turns_a_finger_into_a_long_press() {
  boot_with_a_touch_panel();
  chip().reset();
  // The page starts under the bar of the system, which is forty-eight rows since 0.52.0 (docs/50).
  TEST_ASSERT_EQUAL_UINT16(48, ui::canvas().y);
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
  // **In the view's frame**: the finger went down at 100,200 of the page and the view starts at row 48, so
  // what the app is told is 152 and not 200.
  TEST_ASSERT_EQUAL_UINT16(100, gesture.x);
  TEST_ASSERT_EQUAL_UINT16(152, gesture.y);
  TEST_ASSERT_TRUE(gesture.held_ms >= 1000);
  TEST_ASSERT_EQUAL_UINT8(1, gesture.fingers);
  TEST_ASSERT_TRUE(chip().finger_up(0));
}

// **Three taps are the system's, and on the whole glass they are the way back to the bar** (0.136.0, docs/46 D1). The
// gesture is taken out of the queue before the app reads it —the kernel asks it right after `touch::poll` and before the
// app's turn— so a page that covers everything cannot take away the way out, and what the app hears is not the three
// taps but `on_resize` with the view it has now (docs/30).
static void test_three_taps_take_the_app_out_of_the_whole_glass() {
  boot_with_a_touch_panel();
  chip().reset();
  TEST_ASSERT_TRUE(ui::full_screen(true));
  TEST_ASSERT_EQUAL_UINT16(0, ui::canvas().y);
  TEST_ASSERT_EQUAL_UINT16(960, ui::canvas().height);

  // **Three taps of a hand**, each one inside the window of the one before, with the turn of the kernel after each of
  // them. Twenty milliseconds a turn because the panel of the emulator scans every ten and a read inside a window whose
  // frame was taken finds nobody (docs/50, docs/85).
  for (int i = 0; i < 3; ++i) {
    TEST_ASSERT_TRUE(chip().finger_down(0, 200, 300));
    for (int t = 0; t < 4; ++t) {
      turn(20);
      kernel::ui::poll_full_screen();
    }
    TEST_ASSERT_TRUE(chip().finger_up(0));
    // **A lift is a glass that has been quiet for a whole scan**: the chip hands the frame of the finger over once more
    // and only the turns after it are nobody (docs/50).
    for (int t = 0; t < 4; ++t) {
      turn(20);
      kernel::ui::poll_full_screen();
    }
  }

  // The bar is back and the view is the one of every app: the page of this emulator is **960 high** because it
  // is turned a quarter (the panel is 540x960 and the test sets the rotation to 270), so the bar takes the
  // first forty-eight rows off the top and the view is the rest. The case said `540 - 48`, which was the
  // page of before the turn.
  TEST_ASSERT_FALSE(ui::is_full_screen());
  TEST_ASSERT_EQUAL_UINT16(48, ui::canvas().y);
  TEST_ASSERT_EQUAL_UINT16(960 - 48, ui::canvas().height);
  // **And the app never heard them**: there is no gesture of three taps in the queue it reads (docs/30).
  kernel::touch::Gesture gesture;
  while (kernel::touch::take_gesture(gesture)) {
    TEST_ASSERT_TRUE_MESSAGE(gesture.kind == nullptr || strcmp(gesture.kind, "triple") != 0,
                             "the app heard the three taps of the system");
  }
}

// **One switch for every wait of this emulator**: with the times on, a read of the panel sleeps the bus it
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
  RUN_TEST(test_a_tap_between_two_reads_is_still_a_frame);
  RUN_TEST(test_a_panel_slower_than_the_turn_answers_nothing_half_the_time);
  RUN_TEST(test_the_waits_are_one_switch);
  RUN_TEST(test_the_kernel_turns_a_finger_into_a_long_press);
  RUN_TEST(test_three_taps_take_the_app_out_of_the_whole_glass);
  return UNITY_END();
}
