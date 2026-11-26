// The calendar arithmetic of the system: the days of a month —February with its leap day— and the weekday of a date,
// which are the two numbers a calendar control needs and the two a second copy of the rule would get wrong (docs/60, docs/90).
#include <unity.h>

#include "kernel/calendar.h"

void setUp(void) {}

void tearDown(void) {}

namespace {

using namespace kernel::calendar;

// **A month has the days it has**: February is the whole of the rule, and the two centuries of the rule are in the
// test because the rule is four lines and one of them is easy to lose (docs/60).
void test_a_month_has_the_days_it_has(void) {
  TEST_ASSERT_EQUAL_UINT8(31, days_in_month(2026, 1));
  TEST_ASSERT_EQUAL_UINT8(28, days_in_month(2026, 2));
  TEST_ASSERT_EQUAL_UINT8(31, days_in_month(2026, 3));
  TEST_ASSERT_EQUAL_UINT8(30, days_in_month(2026, 4));
  TEST_ASSERT_EQUAL_UINT8(31, days_in_month(2026, 12));

  // **A leap year every four, except the centuries, except every four hundred**: 2024 was, 1900 was not, 2000 was.
  TEST_ASSERT_EQUAL_UINT8(29, days_in_month(2024, 2));
  TEST_ASSERT_EQUAL_UINT8(29, days_in_month(2000, 2));
  TEST_ASSERT_EQUAL_UINT8(28, days_in_month(1900, 2));
  TEST_ASSERT_EQUAL_UINT8(28, days_in_month(2100, 2));

  // A month that does not exist has no days, and not thirty-one.
  TEST_ASSERT_EQUAL_UINT8(0, days_in_month(2026, 0));
  TEST_ASSERT_EQUAL_UINT8(0, days_in_month(2026, 13));
}

// **Monday is one and Sunday is seven**: a week of this system begins on Monday, and the numbers come from dates a
// calendar already printed (docs/60).
void test_the_week_begins_on_monday(void) {
  TEST_ASSERT_EQUAL_UINT8(2, weekday_of(2026, 11, 3));  // a Tuesday
  TEST_ASSERT_EQUAL_UINT8(7, weekday_of(2026, 11, 1));  // the first of that month is a Sunday
  TEST_ASSERT_EQUAL_UINT8(7, weekday_of(2026, 2, 1));
  TEST_ASSERT_EQUAL_UINT8(4, weekday_of(2024, 2, 29));  // the day a leap year adds, a Thursday
  TEST_ASSERT_EQUAL_UINT8(5, weekday_of(2024, 3, 1));
  TEST_ASSERT_EQUAL_UINT8(6, weekday_of(2000, 1, 1));  // a Saturday, in a century that is a leap year

  // A date that does not exist is no weekday at all.
  TEST_ASSERT_EQUAL_UINT8(0, weekday_of(2026, 2, 30));
  TEST_ASSERT_EQUAL_UINT8(0, weekday_of(2026, 13, 1));
  TEST_ASSERT_EQUAL_UINT8(0, weekday_of(2026, 4, 0));
}

// **El dia de traspàs no es pot apagar** (0.192.57, l'amo: *«sabem com calcular si es any de taspàs o no ho es… no cal un
// toggle manual»*): a leap year is a fact of the Gregorian calendar and there is no door that hides the twenty-ninth of
// February any more —the two calls that did it are gone from `kernel/calendar.h`—, so what is proved here is that the
// rule is the one of the sky for the years that matter and that a date that does not exist has no weekday (docs/60, docs/90).
void test_the_leap_day_is_the_calendar_s_and_nobody_turns_it_off(void) {
  TEST_ASSERT_EQUAL_UINT8(29, days_in_month(2024, 2));
  TEST_ASSERT_EQUAL_UINT8(29, days_in_month(2000, 2));
  TEST_ASSERT_EQUAL_UINT8(28, days_in_month(2026, 2));
  TEST_ASSERT_EQUAL_UINT8(28, days_in_month(2100, 2));
  // The sky does not move: the twenty-ninth of February 2024 was a Thursday, and it is not a day at all in 2026.
  TEST_ASSERT_EQUAL_UINT8(4, weekday_of(2024, 2, 29));
  TEST_ASSERT_EQUAL_UINT8(0, weekday_of(2026, 2, 29));
  // And March is the same month it was.
  TEST_ASSERT_EQUAL_UINT8(31, days_in_month(2024, 3));
}

// **A week begins on a day of the week and not on nothing** (0.170.0): this is the mechanism —any of the seven— and
// the setting a person has is Monday or Sunday, which is `cfg`'s to refuse (docs/60).
void test_a_week_begins_on_a_day_of_the_week(void) {
  TEST_ASSERT_EQUAL_UINT8(1, week_start());

  TEST_ASSERT_TRUE(set_week_start(7));
  TEST_ASSERT_EQUAL_UINT8(7, week_start());

  TEST_ASSERT_TRUE(set_week_start(3));
  TEST_ASSERT_EQUAL_UINT8(3, week_start());
  TEST_ASSERT_FALSE(set_week_start(0));
  TEST_ASSERT_FALSE(set_week_start(8));
  TEST_ASSERT_EQUAL_UINT8(3, week_start());

  TEST_ASSERT_TRUE(set_week_start(1));
  TEST_ASSERT_EQUAL_UINT8(1, week_start());
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_a_month_has_the_days_it_has);
  RUN_TEST(test_the_week_begins_on_monday);
  RUN_TEST(test_the_leap_day_is_the_calendar_s_and_nobody_turns_it_off);
  RUN_TEST(test_a_week_begins_on_a_day_of_the_week);
  return UNITY_END();
}
