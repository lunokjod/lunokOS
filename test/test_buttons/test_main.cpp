// The queue of the keys on its own: no Lua, no kernel, no hardware. Cascade and exclusive, the owner that goes with its
// subscriptions, the handle that gives the place back and the ceiling (docs/30, docs/50).
#include <unity.h>

#include "kernel/button.h"

// **The system is in the queue from the first moment** (`reset()`), because the dial, the promise of `button.wait` and
// `on_button` are one subscriber in cascade: every count in this file is that one plus the ones a case adds (docs/30).
void setUp(void) { button::reset(); }

void tearDown(void) {}

namespace {

// What the listeners saw, in the order they saw it: the case says who is who with the number of the listener.
int g_calls = 0;
int g_order[8] = {0};

void note(int who) {
  if (g_calls < 8) g_order[g_calls] = who;
  ++g_calls;
}

void first(const char*, uint32_t, int64_t, void*) { note(1); }
void second(const char*, uint32_t, int64_t, void*) { note(2); }

// The event itself, so a listener is proved to get what the key said and not only to be called.
const char* g_kind = nullptr;
uint32_t g_key = 0;
int64_t g_held = 0;

void spy(const char* kind, uint32_t key, int64_t held_ms, void*) {
  g_kind = kind;
  g_key = key;
  g_held = held_ms;
}

// Two owners that are not the same thing.
char g_one = 0;
char g_two = 0;

}  // namespace

// **Cascada és tots, i en l'ordre en què es van registrar**: a listener that takes the event is not the one that stops the
// others, which is what a chain of responsibility would do and what the page does not want: the app sees the key and the
// system does its work with the same key (docs/30).
void test_cascade_calls_everyone_in_order(void) {
  TEST_ASSERT_TRUE(button::subscribe(&first, &g_one, button::Mode::kCascade) > 0);
  TEST_ASSERT_TRUE(button::subscribe(&second, &g_two, button::Mode::kCascade) > 0);
  TEST_ASSERT_EQUAL_UINT16(3, button::subscribers());

  button::emit("press", 2, 0);
  TEST_ASSERT_EQUAL_INT(2, g_calls);
  TEST_ASSERT_EQUAL_INT(1, g_order[0]);
  TEST_ASSERT_EQUAL_INT(2, g_order[1]);
}

// **Un exclusiu és un exclusiu**: nobody else hears the event, and that includes the system's own delivery —the system is
// the first subscriber and it is not called either— (docs/30, docs/90).
void test_an_exclusive_subscriber_is_alone(void) {
  TEST_ASSERT_TRUE(button::subscribe(&first, &g_one, button::Mode::kCascade) > 0);
  TEST_ASSERT_TRUE(button::subscribe(&second, &g_two, button::Mode::kExclusive) > 0);

  button::emit("press", 1, 0);
  TEST_ASSERT_EQUAL_INT(1, g_calls);
  TEST_ASSERT_EQUAL_INT(2, g_order[0]);
}

// **L'últim exclusiu és el que respon**: a control that takes the keys sits on top of the queue the way a dialog sits on
// top of a page, and giving its place back gives the keys to the one below (docs/30).
void test_the_last_exclusive_answers_and_giving_it_back_returns_to_the_other(void) {
  const uint16_t outer = button::subscribe(&first, &g_one, button::Mode::kExclusive);
  const uint16_t inner = button::subscribe(&second, &g_two, button::Mode::kExclusive);
  TEST_ASSERT_TRUE(outer > 0);
  TEST_ASSERT_TRUE(inner > 0);

  button::emit("press", 0, 0);
  TEST_ASSERT_EQUAL_INT(1, g_calls);
  TEST_ASSERT_EQUAL_INT(2, g_order[0]);

  button::unsubscribe(inner);
  g_calls = 0;
  button::emit("press", 0, 0);
  TEST_ASSERT_EQUAL_INT(1, g_calls);
  TEST_ASSERT_EQUAL_INT(1, g_order[0]);
}

// **El número dona la plaça i la plaça dona el número**: a handle that is given back takes nothing else out of the queue,
// and the count goes back to what it was (docs/30).
void test_a_handle_gives_its_place_back(void) {
  const uint16_t handle = button::subscribe(&first, &g_one, button::Mode::kCascade);
  TEST_ASSERT_TRUE(handle > 0);
  TEST_ASSERT_EQUAL_UINT16(2, button::subscribers());

  button::unsubscribe(handle);
  TEST_ASSERT_EQUAL_UINT16(1, button::subscribers());

  button::emit("press", 0, 0);
  TEST_ASSERT_EQUAL_INT(0, g_calls);

  // A number that is not in the queue is not an error: it is nothing to take down.
  button::unsubscribe(handle);
  TEST_ASSERT_EQUAL_UINT16(1, button::subscribers());
}

// **Un amo que se'n va s'emporta totes les seves places**: this is the road an app that ended early takes, and it has to
// work with the subscriptions it does not know the numbers of (docs/30, docs/75).
void test_an_owner_that_goes_takes_its_subscriptions(void) {
  TEST_ASSERT_TRUE(button::subscribe(&first, &g_one, button::Mode::kCascade) > 0);
  TEST_ASSERT_TRUE(button::subscribe(&second, &g_one, button::Mode::kExclusive) > 0);
  TEST_ASSERT_TRUE(button::subscribe(&first, &g_two, button::Mode::kCascade) > 0);
  TEST_ASSERT_EQUAL_UINT16(4, button::subscribers());

  button::unsubscribe_owner(&g_one);
  TEST_ASSERT_EQUAL_UINT16(2, button::subscribers());

  // The exclusive of that owner is gone too, so the one that is left hears the event.
  button::emit("press", 0, 0);
  TEST_ASSERT_EQUAL_INT(1, g_calls);
  TEST_ASSERT_EQUAL_INT(1, g_order[0]);
}

// **La cua té un sostre i ho diu**: eight places —the system's among them— and the ninth answer is zero and not a number
// that means nothing (docs/30, docs/75).
void test_the_queue_has_a_ceiling(void) {
  uint16_t handles[8] = {0};
  int taken = 0;
  for (int i = 0; i < 8; ++i) {
    handles[i] = button::subscribe(&first, &g_one, button::Mode::kCascade);
    if (handles[i] != 0) ++taken;
  }
  TEST_ASSERT_EQUAL_INT(7, taken);
  TEST_ASSERT_EQUAL_UINT16(8, button::subscribers());
  TEST_ASSERT_EQUAL_UINT16(0, button::subscribe(&second, &g_two, button::Mode::kCascade));

  for (int i = 0; i < 8; ++i) button::unsubscribe(handles[i]);
  TEST_ASSERT_EQUAL_UINT16(1, button::subscribers());
}

// **El listener rep l'event i no un número**: the kind, the key and how long it was held arrive whole, which is what an
// app's function needs to say what happened (docs/30).
void test_the_listener_gets_the_event(void) {
  TEST_ASSERT_TRUE(button::subscribe(&spy, &g_one, button::Mode::kCascade) > 0);
  button::emit("long", 2, 1234);
  TEST_ASSERT_EQUAL_STRING("long", g_kind);
  TEST_ASSERT_EQUAL_UINT32(2, g_key);
  TEST_ASSERT_EQUAL_INT64(1234, g_held);
}

// **Un listener sense funció no entra a la cua**: a null listener is a place that would never answer and a turn wasted on
// every press (docs/30).
void test_a_listener_is_needed(void) {
  TEST_ASSERT_EQUAL_UINT16(0, button::subscribe(nullptr, &g_one, button::Mode::kCascade));
  TEST_ASSERT_EQUAL_UINT16(1, button::subscribers());
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_cascade_calls_everyone_in_order);
  RUN_TEST(test_an_exclusive_subscriber_is_alone);
  RUN_TEST(test_the_last_exclusive_answers_and_giving_it_back_returns_to_the_other);
  RUN_TEST(test_a_handle_gives_its_place_back);
  RUN_TEST(test_an_owner_that_goes_takes_its_subscriptions);
  RUN_TEST(test_the_queue_has_a_ceiling);
  RUN_TEST(test_the_listener_gets_the_event);
  RUN_TEST(test_a_listener_is_needed);
  return UNITY_END();
}
