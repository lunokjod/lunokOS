// The alarm queue on its own: no Lua, no kernel, no hardware. It is the part of
// M3 that has to be right before anything above it can be (docs/60).
#include <string.h>

#include <unity.h>

#include "alarms/alarms.h"

void setUp(void) { alarms::reset(); }

void tearDown(void) {}

namespace {

alarms::Spec make(const char* app, int64_t on_mono_ms, int32_t repeat_ms = 0) {
  alarms::Spec spec;
  spec.app = app;
  spec.on_mono_ms = on_mono_ms;
  spec.repeat_ms = repeat_ms;
  return spec;
}

}  // namespace

// The list is ordered by deadline: only the first element is what the kernel
// arms, so the order is the feature.
void test_the_queue_is_ordered_by_deadline(void) {
  TEST_ASSERT_TRUE(alarms::add(make("a", 5000)) != 0);
  TEST_ASSERT_TRUE(alarms::add(make("b", 1000)) != 0);
  TEST_ASSERT_TRUE(alarms::add(make("c", 3000)) != 0);

  TEST_ASSERT_EQUAL_UINT32(3, alarms::count());
  TEST_ASSERT_EQUAL_INT64(1000, alarms::next_due_mono_ms());
  TEST_ASSERT_EQUAL_STRING("b", alarms::at(0)->app);
  TEST_ASSERT_EQUAL_STRING("c", alarms::at(1)->app);
  TEST_ASSERT_EQUAL_STRING("a", alarms::at(2)->app);
}

// Nothing is due before its time, and what is due comes out with its id.
void test_take_due_only_takes_what_is_due(void) {
  const uint32_t id = alarms::add(make("a", 2000));
  alarms::Entry out;

  TEST_ASSERT_FALSE(alarms::take_due(1999, out));
  TEST_ASSERT_EQUAL_UINT32(1, alarms::count());

  TEST_ASSERT_TRUE(alarms::take_due(2000, out));
  TEST_ASSERT_EQUAL_UINT32(id, out.id);
  TEST_ASSERT_EQUAL_UINT32(0, alarms::count());
  TEST_ASSERT_EQUAL_INT64(-1, alarms::next_due_mono_ms());
}

// A repeating alarm goes back into the list at its next occurrence, and the copy
// that fired says how many it lost. A daily alarm that was missed three days
// arrives once (docs/60).
void test_a_repeating_alarm_moves_to_its_next_occurrence(void) {
  TEST_ASSERT_TRUE(alarms::add(make("a", 1000, static_cast<int32_t>(alarms::kDailyMs))) != 0);
  alarms::Entry out;

  TEST_ASSERT_TRUE(alarms::take_due(1000, out));
  TEST_ASSERT_EQUAL_UINT32(0, out.missed);
  TEST_ASSERT_EQUAL_INT64(1000 + alarms::kDailyMs, alarms::next_due_mono_ms());

  TEST_ASSERT_TRUE(alarms::take_due(1000 + 3 * alarms::kDailyMs, out));
  TEST_ASSERT_EQUAL_UINT32(2, out.missed);
  TEST_ASSERT_EQUAL_INT64(1000 + 4 * alarms::kDailyMs, alarms::next_due_mono_ms());
  TEST_ASSERT_EQUAL_UINT32(1, alarms::count());
}

// An app only cancels its own alarms.
void test_only_the_owner_cancels(void) {
  const uint32_t id = alarms::add(make("mine", 1000));

  TEST_ASSERT_FALSE(alarms::cancel("other", id));
  TEST_ASSERT_EQUAL_UINT32(1, alarms::count());

  TEST_ASSERT_TRUE(alarms::cancel("mine", id));
  TEST_ASSERT_EQUAL_UINT32(0, alarms::count());
  TEST_ASSERT_FALSE(alarms::cancel("mine", id));
}

// The app name and the payload are copies: the caller's text can go away.
void test_the_alarm_keeps_its_own_copy(void) {
  char app[16];
  char payload[16];
  strcpy(app, "copyapp");
  strcpy(payload, "hello");

  alarms::Spec spec = make(app, 1000);
  spec.payload_kind = alarms::Payload::kText;
  spec.payload = payload;
  TEST_ASSERT_TRUE(alarms::add(spec) != 0);

  app[0] = 'X';
  payload[0] = 'X';
  TEST_ASSERT_EQUAL_STRING("copyapp", alarms::at(0)->app);
  TEST_ASSERT_EQUAL_STRING("hello", alarms::at(0)->payload);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(alarms::Payload::kText),
                           static_cast<uint32_t>(alarms::at(0)->payload_kind));
}

// There is room for max() alarms and not one more.
void test_the_list_has_a_ceiling(void) {
  for (uint32_t i = 0; i < alarms::max(); ++i) {
    TEST_ASSERT_TRUE(alarms::add(make("a", 1000 + static_cast<int64_t>(i))) != 0);
  }
  TEST_ASSERT_EQUAL_UINT32(0, alarms::add(make("a", 9999)));
  TEST_ASSERT_EQUAL_UINT32(alarms::max(), alarms::count());
}

// An alarm with no owner is refused where it is made, not when it fires.
void test_an_alarm_without_an_app_is_refused(void) {
  TEST_ASSERT_EQUAL_UINT32(0, alarms::add(make("", 1000)));
  TEST_ASSERT_EQUAL_UINT32(0, alarms::add(make(nullptr, 1000)));
  TEST_ASSERT_EQUAL_UINT32(0, alarms::count());
}

// The text form carries an epoch per line, because the monotonic clock starts
// again at every boot: loading it back has to give the same deadline.
void test_dump_and_load_keep_the_persistent_alarms(void) {
  alarms::Spec kept = make("keep", 5000);
  kept.persist = true;
  kept.repeat_ms = static_cast<int32_t>(alarms::kDailyMs);
  kept.raise_to_front = true;
  kept.payload_kind = alarms::Payload::kText;
  kept.payload = "ring";
  TEST_ASSERT_TRUE(alarms::add(kept) != 0);
  // One without `persist` is not written: it was meant to live in the RAM.
  TEST_ASSERT_TRUE(alarms::add(make("gone", 7000)) != 0);

  char blob[256];
  const size_t len = alarms::dump(1000, 1767225600000LL, blob, sizeof(blob));
  TEST_ASSERT_TRUE(len > 0);

  // A different monotonic clock, as after a reboot: the deadline moves with it.
  TEST_ASSERT_TRUE(alarms::load(2000, 1767225600000LL, blob));
  TEST_ASSERT_EQUAL_UINT32(1, alarms::count());

  const alarms::Entry* entry = alarms::at(0);
  TEST_ASSERT_NOT_NULL(entry);
  TEST_ASSERT_EQUAL_STRING("keep", entry->app);
  TEST_ASSERT_EQUAL_INT64(6000, entry->on_mono_ms);  // 5000 - 1000 + 2000
  TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(alarms::kDailyMs), entry->repeat_ms);
  TEST_ASSERT_TRUE(entry->persist);
  TEST_ASSERT_TRUE(entry->raise_to_front);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(alarms::Payload::kText),
                           static_cast<uint32_t>(entry->payload_kind));
  TEST_ASSERT_EQUAL_STRING("ring", entry->payload);
}

// Without an epoch nothing is written: a wall-clock deadline that cannot be
// written down must not be invented (docs/60).
void test_dump_without_an_epoch_writes_nothing(void) {
  alarms::Spec kept = make("keep", 5000);
  kept.persist = true;
  TEST_ASSERT_TRUE(alarms::add(kept) != 0);

  char blob[64];
  TEST_ASSERT_EQUAL_UINT32(0, alarms::dump(1000, 0, blob, sizeof(blob)));
  TEST_ASSERT_FALSE(alarms::load(1000, 0, blob));
}

// The revision moves every time the queue does: it is what tells the kernel that
// there is something new to write.
void test_the_revision_moves_when_the_queue_changes(void) {
  const uint32_t start = alarms::revision();
  const uint32_t id = alarms::add(make("a", 1000));
  TEST_ASSERT_TRUE(alarms::revision() != start);

  const uint32_t after_add = alarms::revision();
  alarms::Entry out;
  TEST_ASSERT_TRUE(alarms::take_due(1000, out));
  TEST_ASSERT_TRUE(alarms::revision() != after_add);

  const uint32_t after_take = alarms::revision();
  alarms::add(make("b", 2000));
  TEST_ASSERT_TRUE(alarms::cancel("b", alarms::at(0)->id));
  TEST_ASSERT_TRUE(alarms::revision() != after_take);
  (void)id;
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_the_queue_is_ordered_by_deadline);
  RUN_TEST(test_take_due_only_takes_what_is_due);
  RUN_TEST(test_a_repeating_alarm_moves_to_its_next_occurrence);
  RUN_TEST(test_only_the_owner_cancels);
  RUN_TEST(test_the_alarm_keeps_its_own_copy);
  RUN_TEST(test_the_list_has_a_ceiling);
  RUN_TEST(test_an_alarm_without_an_app_is_refused);
  RUN_TEST(test_dump_and_load_keep_the_persistent_alarms);
  RUN_TEST(test_dump_without_an_epoch_writes_nothing);
  RUN_TEST(test_the_revision_moves_when_the_queue_changes);
  return UNITY_END();
}
