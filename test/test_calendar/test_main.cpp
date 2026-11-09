// The calendar arithmetic of the system: the leap years and the weekday of a date, which are the two numbers a
// calendar control needs and the two a second copy of the rule would get wrong (docs/60, docs/90).
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

// **The leap day is a setting and the weekday of a date is not** (0.170.0): a calendar told to leave the twenty-ninth
// of February out says February has twenty-eight days in every year, and the day of the week a date falls on does not
// move because a page hides one day (docs/60, docs/90).
void test_the_leap_day_is_a_setting_and_the_weekday_is_not(void) {
  TEST_ASSERT_TRUE(leap_years());
  TEST_ASSERT_EQUAL_UINT8(29, days_in_month(2024, 2));

  set_leap_years(false);
  TEST_ASSERT_FALSE(leap_years());
  TEST_ASSERT_EQUAL_UINT8(28, days_in_month(2024, 2));
  TEST_ASSERT_EQUAL_UINT8(28, days_in_month(2000, 2));
  // The sky does not move: the twenty-ninth of February 2024 was a Thursday with the day and without it.
  TEST_ASSERT_EQUAL_UINT8(4, weekday_of(2024, 2, 29));
  // And March is the same month it was.
  TEST_ASSERT_EQUAL_UINT8(31, days_in_month(2024, 3));

  set_leap_years(true);
  TEST_ASSERT_EQUAL_UINT8(29, days_in_month(2024, 2));
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
  RUN_TEST(test_the_leap_day_is_a_setting_and_the_weekday_is_not);
  RUN_TEST(test_a_week_begins_on_a_day_of_the_week);
  return UNITY_END();
}
