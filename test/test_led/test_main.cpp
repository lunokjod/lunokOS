// The light of the board, effect by effect: what each one says at a given millisecond,
// what it does when its cycles run out, and what happens when an app stops it or the
// system has something of its own to say.
//
// The clock is the emulator's and the test moves it by hand (`emu::advance_ms`), so a
// five-second breath is watched in no time at all and the frames are the ones the board
// would paint. The light is read from the band and not from the policy, so an effect that
// is computed and never painted is caught here (docs/30, docs/85).
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <unity.h>

#include "emu/options.h"
#include "hal/hal.h"
#include "kernel/kernel.h"
#include "kernel/led.h"
#include "kernel/log.h"
#include "pal/pal.h"

// One name for the policy, so the tests read like the document (docs/30).
namespace led = kernel::led;

void setUp(void) {}

void tearDown(void) {}

namespace {

char g_root[64];
bool g_booted = false;

const char* root() {
  if (g_root[0] == '\0') {
    snprintf(g_root, sizeof(g_root), "/tmp/lunok-led-tests-%d", static_cast<int>(getpid()));
  }
  return g_root;
}

void make_dirs(const char* path) {
  char partial[256];
  const size_t len = strlen(path);
  if (len >= sizeof(partial)) return;
  memcpy(partial, path, len + 1);
  for (size_t i = 1; i <= len; ++i) {
    if (partial[i] == '/' || partial[i] == '\0') {
      const char saved = partial[i];
      partial[i] = '\0';
      ::mkdir(partial, 0777);  // already there is fine
      partial[i] = saved;
    }
  }
}

// The kernel boots on a disk of its own and with the ATOM's hardware, which is one light
// and no limit on a channel (docs/85).
bool boot_once() {
  if (g_booted) return true;
  char fs_dir[96];
  snprintf(fs_dir, sizeof(fs_dir), "%s/littlefs", root());
  make_dirs(root());
  make_dirs(fs_dir);
  emu::Options opts{};
  opts.fs_dir = root();
  emu::set_options(opts);
  // The boot reads the light before anything else can fail: a disk that is not there is a
  // line in the log and, since this version, a red SOS (docs/30).
  if (!pal::init()) return false;
  kernel::boot(hal::platform());
  g_booted = true;
  return true;
}

bool reboot() {
  kernel::boot(hal::platform());
  return kernel::led::available();
}

// The board the light lives in: the Matrix has twenty-five lights and a ceiling, the ATOM
// has one (docs/50, docs/85).
bool be_board(const char* model) {
  emu::Options opts = emu::options();
  opts.board = model;
  emu::set_options(opts);
  return reboot() && kernel::led::available();
}

// The brightest channel the light is showing: one number for "how lit it is", which is what
// an effect is about, whatever the colour (docs/30).
uint8_t level() {
  uint8_t brightest = emu::led_red();
  if (emu::led_green() > brightest) brightest = emu::led_green();
  if (emu::led_blue() > brightest) brightest = emu::led_blue();
  return brightest;
}

// Moves the emulator's clock and paints, which is exactly what a turn does when the light has
// something to say (docs/30).
void step(uint32_t ms) {
  emu::advance_ms(static_cast<int64_t>(ms));
  kernel::led::poll();
}

void play(led::EffectKind kind, uint8_t r, uint8_t g, uint8_t b, uint32_t period_ms,
          uint32_t cycles, uint8_t limit) {
  kernel::led::Effect effect;
  effect.kind = kind;
  effect.red = r;
  effect.green = g;
  effect.blue = b;
  effect.period_ms = period_ms;
  effect.cycles = cycles;
  effect.level = limit;
  TEST_ASSERT_TRUE(kernel::led::play(effect));
}

// Taking the light is also how a test gets rid of a system moment that is running: a note
// is a note and not a state, and the app's word is the light while it lasts (docs/30).
void take_the_light() {
  kernel::led::set_colour(0, 0, 0);
  kernel::led::clear_colour();
  kernel::led::poll();
}

// ---- the four effects ---------------------------------------------------

// The breath goes up and comes down over its period, and it is the same one every time:
// an effect is a function of the time since it started and not of the frames it has
// painted (docs/30).
void test_a_breath_goes_up_and_down(void) {
  TEST_ASSERT_TRUE(boot_once());
  take_the_light();
  // Ten and not two hundred: the emulator is a panel and its ceiling is 20, so a level has to be
  // **below** it to be seen as a level and not as the ceiling. And the colour is at full scale,
  // because `level` is a dimmer over the colour and not a second colour (docs/30, docs/85).
  play(kernel::led::EffectKind::kBreathe, 0, 0, 255, 1000, 0, 10);
  step(0);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, level(), "a breath that starts at the top is not a breath");
  step(250);
  const uint8_t quarter = level();
  TEST_ASSERT_TRUE_MESSAGE(quarter > 0 && quarter < 10, "a quarter of a breath is in between");
  step(250);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(10, level(), "half a period is the top of the breath");
  step(500);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, level(), "one period is back to nothing");
  step(500);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(10, level(), "and it comes up again");
}

// The blink is half the period on and half off, which is what makes it read as a blink and
// not as a dimmer light (docs/30).
void test_a_blink_is_on_and_off(void) {
  TEST_ASSERT_TRUE(boot_once());
  take_the_light();
  play(kernel::led::EffectKind::kBlink, 0, 255, 0, 1000, 0, 10);
  step(100);
  TEST_ASSERT_EQUAL_UINT8(10, level());
  step(400);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, level(), "the second half of the period is dark");
  step(500);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(10, level(), "and the next period is on again");
}

// The SOS is Morse and not a decoration: three dots, three dashes, three dots, with the
// gaps that Morse asks for, which is 34 dots for one round (docs/30).
void test_the_sos_is_the_morse_of_sos(void) {
  TEST_ASSERT_TRUE(boot_once());
  take_the_light();
  play(kernel::led::EffectKind::kSos, 255, 0, 0, 200, 0, 10);
  step(100);  // unit 0: the first dot
  TEST_ASSERT_EQUAL_UINT8(10, level());
  step(200);  // unit 1: the gap between dots
  TEST_ASSERT_EQUAL_UINT8(0, level());
  step(200);  // unit 2: the second dot
  TEST_ASSERT_EQUAL_UINT8(10, level());
  // Units 5 to 7 are the gap after the S, and unit 8 is the first dash.
  step(600);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, level(), "the gap between two letters is three dots");
  step(500);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(10, level(), "and the O is three dashes");
  // Unit 27 is the long gap of seven dots after the last S.
  step(3900);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, level(), "the end of the message is seven dots of silence");
}

// A ball that bounces faster and faster: every bounce reaches the top and takes less time
// than the one before, and then the round starts over (docs/30).
void test_a_bounce_is_quicker_every_time(void) {
  TEST_ASSERT_TRUE(boot_once());
  take_the_light();
  play(kernel::led::EffectKind::kBounce, 0, 200, 0, 400, 0, 200);

  // One round of bounces with a first bounce of 400 ms is 1219 ms: 400 + 300 + 225 + 168 +
  // 126. The tops have to come closer together (docs/30).
  uint32_t tops[8];
  uint32_t found = 0;
  uint8_t last = 0;
  for (uint32_t t = 5; t <= 1219 && found < 8; t += 5) {
    step(5);
    const uint8_t now = level();
    // A crossing of a high level and not a sample exactly at the top: the first bounce is
    // 400 ms and the last one 126, so no single sample time is the top of all of them
    // (docs/30).
    if (now >= 15 && last < 15) {
      tops[found] = t;
      ++found;
    }
    last = now;
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(5, found, "the ball has to hit the top once per bounce");
  const uint32_t first = tops[1] - tops[0];
  const uint32_t second = tops[2] - tops[1];
  const uint32_t third = tops[3] - tops[2];
  TEST_ASSERT_TRUE_MESSAGE(first > second, "the second bounce is not quicker than the first");
  TEST_ASSERT_TRUE_MESSAGE(second > third, "the third bounce is not quicker than the second");
}

// ---- the app's layer -----------------------------------------------------

// An effect with a number of cycles ends by itself, and then the light is the system's
// again — which here means off, because nothing else is happening (docs/30).
void test_an_effect_ends_after_its_cycles(void) {
  TEST_ASSERT_TRUE(boot_once());
  take_the_light();
  play(kernel::led::EffectKind::kBlink, 200, 200, 0, 200, 3, 200);
  kernel::led::Effect still;
  uint32_t elapsed = 0;
  TEST_ASSERT_TRUE(kernel::led::current_effect(still, elapsed));
  step(200);  // the first cycle is over
  TEST_ASSERT_TRUE(kernel::led::current_effect(still, elapsed));
  step(400);  // the third one
  TEST_ASSERT_FALSE_MESSAGE(kernel::led::current_effect(still, elapsed),
                            "an effect with three cycles is still playing after three");
  step(0);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, level(), "the light has to be off after the last cycle");
}

// `stop` with no fade gives the light back at once, and with a fade it goes down to black
// first and then gives it back: a light that goes out in one frame at night is a slap
// (docs/30).
void test_a_stop_is_immediate_or_fades_to_black(void) {
  TEST_ASSERT_TRUE(boot_once());
  take_the_light();
  play(kernel::led::EffectKind::kBlink, 0, 0, 255, 200, 0, 10);
  step(50);
  TEST_ASSERT_EQUAL_UINT8(10, level());
  kernel::led::stop(0);
  step(0);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, level(), "a stop with no fade is immediate");

  play(kernel::led::EffectKind::kBlink, 0, 0, 255, 200, 0, 10);
  step(50);
  TEST_ASSERT_EQUAL_UINT8(10, level());
  kernel::led::stop(300);
  step(150);
  const uint8_t half = level();
  TEST_ASSERT_TRUE_MESSAGE(half > 0 && half < 10, "half of the fade is half the light");
  step(200);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, level(), "a fade ends in black");
}

// One light of the strip and the rest dark, for the panel of the Matrix; and a light that
// is not there is refused instead of painting somewhere else (docs/30, docs/50).
void test_one_light_of_the_strip_and_the_rest_is_dark(void) {
  TEST_ASSERT_TRUE(boot_once());
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, kernel::led::count(), "the emulator is the ATOM here");
  kernel::led::Effect effect;
  effect.kind = kernel::led::EffectKind::kBlink;
  effect.red = 255;
  effect.period_ms = 200;
  effect.cycles = 0;
  effect.level = 255;
  effect.pixel = 3;
  TEST_ASSERT_FALSE_MESSAGE(kernel::led::play(effect),
                            "a board with one light has no light number three");

  TEST_ASSERT_TRUE(be_board("m5stack-atom-matrix"));
  TEST_ASSERT_EQUAL_UINT32(25, kernel::led::count());
  take_the_light();
  effect.pixel = 3;
  TEST_ASSERT_TRUE(kernel::led::play(effect));
  step(50);  // the first half of a 200 ms blink is on
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  TEST_ASSERT_TRUE(emu::led_pixel(3, r, g, b));
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(20, r, "the third light is on, at the panel's ceiling");
  TEST_ASSERT_TRUE(emu::led_pixel(0, r, g, b));
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, r, "and the first light of the strip is dark");

  // Back to the ATOM for the tests that come after: one emulator, one board (docs/85).
  TEST_ASSERT_TRUE(be_board("m5stack-atom"));
}

// ---- the system's layer --------------------------------------------------

// The board breathes white when it comes up, and the breath **ends**: a light left
// breathing on a desk all day is power spent by nobody (docs/30, docs/75).
void test_the_board_breathes_white_when_it_boots(void) {
  TEST_ASSERT_TRUE(boot_once());
  TEST_ASSERT_TRUE(reboot());
  step(2500);  // half of the five-second breath
  // Twenty out of 255: the maker's dim level, which is where a breath stops (docs/30, docs/75).
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(20, level(), "half a breath is the top of the dim level");
  // The breath ramps from white towards a pale blue, so the blue end leads (docs/30).
  TEST_ASSERT_TRUE_MESSAGE(emu::led_blue() >= emu::led_red(), "the boot's ramp goes bluewards");
  // Four breaths and it is over.
  step(18000);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, level(), "the boot's light goes out by itself");
}

// A failure in the log is a red SOS on the board, asked for in one place: every site that
// logs one would otherwise have to remember to say it (docs/30).
void test_an_error_in_the_log_is_a_red_sos(void) {
  TEST_ASSERT_TRUE(boot_once());
  TEST_ASSERT_TRUE(reboot());
  kernel::log_printf(pal::kLogError, "TEST", "the test failed on purpose");
  step(100);  // the first dot of the sos
  // The system's effects are dim on purpose: twenty out of 255 (docs/30, docs/75).
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(20, emu::led_red(), "the sos is red at the maker's dim level");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, emu::led_blue(), "and it has no blue in it");
}

// The radio says it is working in blue, and it says it while it works and not for ever: it
// is the one state of the system layer, and it is the app that gives the light back
// (docs/30).
void test_the_radio_blinks_blue_while_it_works(void) {
  TEST_ASSERT_TRUE(boot_once());
  TEST_ASSERT_TRUE(reboot());
  take_the_light();
  kernel::led::set_state(kernel::led::State::kRadio);
  step(100);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(20, emu::led_blue(), "the first half of the blink is on");
  step(500);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, level(), "and the second half is off");
  kernel::led::set_state(kernel::led::State::kOff);
  step(0);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, level(), "with the radio down there is nothing to say");
}

// An app may redefine any of the four moments, and put the maker's back with one word: the
// light of a machine is not a taste the kernel keeps (docs/30).
void test_an_app_may_change_the_system_effects(void) {
  TEST_ASSERT_TRUE(boot_once());
  kernel::led::Effect was;
  TEST_ASSERT_TRUE(kernel::led::system_effect(kernel::led::Trigger::kAlarm, was));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(kernel::led::EffectKind::kBounce),
                          static_cast<uint8_t>(was.kind));

  kernel::led::Effect mine;
  mine.kind = kernel::led::EffectKind::kBlink;
  mine.red = 1;
  mine.green = 2;
  mine.blue = 3;
  mine.period_ms = 500;
  mine.cycles = 2;
  mine.level = 100;
  TEST_ASSERT_TRUE(kernel::led::set_system_effect(kernel::led::Trigger::kAlarm, mine));
  kernel::led::Effect now;
  TEST_ASSERT_TRUE(kernel::led::system_effect(kernel::led::Trigger::kAlarm, now));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(kernel::led::EffectKind::kBlink),
                          static_cast<uint8_t>(now.kind));
  TEST_ASSERT_EQUAL_UINT8(3, now.blue);

  kernel::led::restore_system_effect(kernel::led::Trigger::kAlarm);
  TEST_ASSERT_TRUE(kernel::led::system_effect(kernel::led::Trigger::kAlarm, now));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(kernel::led::EffectKind::kBounce),
                          static_cast<uint8_t>(now.kind));
}

// The names are the kernel's and the interpreter's at once: one list, so an app cannot ask
// for an effect that the light does not have (docs/30).
void test_the_names_of_the_effects_and_the_moments(void) {
  led::EffectKind kind = led::EffectKind::kNone;
  TEST_ASSERT_TRUE(led::effect_from_name("breathe", kind));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(led::EffectKind::kBreathe),
                          static_cast<uint8_t>(kind));
  TEST_ASSERT_TRUE(led::effect_from_name("sos", kind));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(led::EffectKind::kSos), static_cast<uint8_t>(kind));
  TEST_ASSERT_FALSE(led::effect_from_name("disco", kind));
  TEST_ASSERT_EQUAL_STRING("bounce", led::effect_name(led::EffectKind::kBounce));

  led::Trigger trigger = led::Trigger::kBoot;
  TEST_ASSERT_TRUE(led::trigger_from_name("error", trigger));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(led::Trigger::kError),
                          static_cast<uint8_t>(trigger));
  TEST_ASSERT_FALSE(led::trigger_from_name("disco", trigger));
  TEST_ASSERT_EQUAL_STRING("radio", led::trigger_name(led::Trigger::kRadio));
}

// A period too short to see is clamped and not obeyed: a millisecond of light is a seizure
// and not a message, and the floor is the kernel's (docs/30).
void test_a_period_below_the_floor_is_clamped(void) {
  TEST_ASSERT_TRUE(boot_once());
  take_the_light();
  play(kernel::led::EffectKind::kBlink, 0, 0, 200, 1, 0, 200);
  kernel::led::Effect effect;
  uint32_t elapsed = 0;
  TEST_ASSERT_TRUE(kernel::led::current_effect(effect, elapsed));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(120, effect.period_ms, "the floor is 120 ms");
}

// A moment of the system **interrupts** the app: the light it had is kept as it was, the moment is
// seen alone, and when it ends the light comes back and the app is told so it can redraw (docs/30).
void test_a_moment_keeps_the_light_and_gives_it_back(void) {
  TEST_ASSERT_TRUE(boot_once());
  TEST_ASSERT_TRUE(reboot());
  // The app's colour, with the light free: the boot's breath is idle and gives way without a word.
  TEST_ASSERT_TRUE(kernel::led::set_colour(0, 0, 15));
  step(0);
  TEST_ASSERT_EQUAL_UINT8(15, level());

  kernel::led::note(kernel::led::Trigger::kError);
  step(0);
  TEST_ASSERT_TRUE_MESSAGE(kernel::led::busy(), "a failure is a moment and the light is busy");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(20, emu::led_red(), "the moment is what is shown");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, emu::led_blue(), "and the app's blue is not");

  // The whole message: two rounds of 34 dots of 200 ms.
  step(2 * 34 * 200 + 10);
  TEST_ASSERT_FALSE_MESSAGE(kernel::led::busy(), "the moment has ended");
  TEST_ASSERT_TRUE_MESSAGE(kernel::led::released(), "the app has to be told");
  TEST_ASSERT_FALSE_MESSAGE(kernel::led::released(), "and told once");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(15, emu::led_blue(), "the app's light is back as it was");
}

// While a moment runs nothing touches the light by hand and no effect starts; a person's own hand is
// the exception and it stops the moment (docs/30).
void test_the_light_is_busy_while_a_moment_runs(void) {
  TEST_ASSERT_TRUE(boot_once());
  TEST_ASSERT_TRUE(reboot());
  kernel::led::note(kernel::led::Trigger::kAlarm);
  TEST_ASSERT_TRUE(kernel::led::busy());
  TEST_ASSERT_FALSE_MESSAGE(kernel::led::set_colour(0, 200, 0), "a colour cannot be taken now");
  kernel::led::Effect effect;
  effect.kind = kernel::led::EffectKind::kBlink;
  effect.period_ms = 200;
  TEST_ASSERT_FALSE_MESSAGE(kernel::led::play(effect), "nor can an effect start");

  TEST_ASSERT_TRUE(kernel::led::take(0, 15, 0));
  TEST_ASSERT_FALSE_MESSAGE(kernel::led::busy(), "a person's hand ends the moment");
  step(0);
  TEST_ASSERT_EQUAL_UINT8(15, level());
}

// An effect can be told where its colour goes: the second colour is the other end of a ramp that runs
// along every cycle (docs/30).
void test_the_second_colour_is_a_ramp(void) {
  TEST_ASSERT_TRUE(boot_once());
  TEST_ASSERT_TRUE(reboot());
  kernel::led::Effect effect;
  effect.kind = kernel::led::EffectKind::kBlink;
  effect.red = 20;
  effect.blue2 = 20;
  effect.period_ms = 1000;
  effect.level = 255;
  TEST_ASSERT_TRUE(kernel::led::play(effect));
  step(0);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(20, emu::led_red(), "a cycle starts at the first colour");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, emu::led_blue(), "and not a drop of the second");
  step(300);
  TEST_ASSERT_TRUE_MESSAGE(emu::led_blue() > 0, "the second colour has begun");
  TEST_ASSERT_TRUE_MESSAGE(emu::led_red() < 20, "and the first has given way");
}

// The ceiling of an effect is **twenty out of 255** unless the app asks for more: a light that says
// something does not have to be the brightest thing on the desk (docs/30, docs/75).
void test_the_default_level_is_twenty(void) {
  TEST_ASSERT_TRUE(boot_once());
  TEST_ASSERT_TRUE(reboot());
  kernel::led::Effect effect;
  effect.kind = kernel::led::EffectKind::kBlink;
  effect.red = 255;
  effect.green = 255;
  effect.blue = 255;
  effect.period_ms = 1000;
  TEST_ASSERT_TRUE(kernel::led::play(effect));
  kernel::led::Effect now;
  uint32_t elapsed = 0;
  TEST_ASSERT_TRUE(kernel::led::current_effect(now, elapsed));
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(20, now.level, "twenty unless the app asks for more");
  step(100);
  TEST_ASSERT_EQUAL_UINT8(20, level());
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_a_breath_goes_up_and_down);
  RUN_TEST(test_a_blink_is_on_and_off);
  RUN_TEST(test_the_sos_is_the_morse_of_sos);
  RUN_TEST(test_a_bounce_is_quicker_every_time);
  RUN_TEST(test_an_effect_ends_after_its_cycles);
  RUN_TEST(test_a_stop_is_immediate_or_fades_to_black);
  RUN_TEST(test_one_light_of_the_strip_and_the_rest_is_dark);
  RUN_TEST(test_the_board_breathes_white_when_it_boots);
  RUN_TEST(test_an_error_in_the_log_is_a_red_sos);
  RUN_TEST(test_the_radio_blinks_blue_while_it_works);
  RUN_TEST(test_an_app_may_change_the_system_effects);
  RUN_TEST(test_the_names_of_the_effects_and_the_moments);
  RUN_TEST(test_a_period_below_the_floor_is_clamped);
  RUN_TEST(test_a_moment_keeps_the_light_and_gives_it_back);
  RUN_TEST(test_the_light_is_busy_while_a_moment_runs);
  RUN_TEST(test_the_second_colour_is_a_ramp);
  RUN_TEST(test_the_default_level_is_twenty);
  return UNITY_END();
}
