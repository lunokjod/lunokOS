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

// **Un tick no és una alarma: no arma el despertar, no es desa i no compta les voltes perdudes** (0.192.60). The queue is one
// and the three promises are the difference: `next_due_mono_ms` is the next thing that has to be served —by the turn of the
// loop— and `next_wake_mono_ms` is the next thing that can bring the board back, which a tick never is. `dump` writes it
// nowhere (a tick is the app's, and the app asks again when it opens) and a night of sleep is not a tick that was lost.
void test_a_tick_is_served_by_the_loop_and_never_arms_the_wake(void) {
  alarms::Spec tick = make("stats", 1000, 60000);
  tick.awake_only = true;
  const uint32_t id = alarms::add(tick);
  TEST_ASSERT_TRUE(id != 0);
  TEST_ASSERT_EQUAL_UINT32(1, alarms::count());

  // S'ha de servir, però no és cap hora a la qual despertar-se.
  TEST_ASSERT_EQUAL_INT64(1000, alarms::next_due_mono_ms());
  TEST_ASSERT_EQUAL_INT64(-1, alarms::next_wake_mono_ms());

  // Una alarma de debò al darrere és la que mana al despertar, i el tick encara no hi és.
  TEST_ASSERT_TRUE(alarms::add(make("clock", 5000)) != 0);
  TEST_ASSERT_EQUAL_INT64(1000, alarms::next_due_mono_ms());
  TEST_ASSERT_EQUAL_INT64(5000, alarms::next_wake_mono_ms());

  // La volta que ha perdut la placa adormida no és una volta perduda, i la cua no ha canviat per a la NVS.
  const uint32_t revision = alarms::revision();
  alarms::Entry out;
  TEST_ASSERT_TRUE(alarms::take_due(1000 + 5 * 60000, out));
  TEST_ASSERT_TRUE(out.awake_only);
  TEST_ASSERT_EQUAL_UINT32(0, out.missed);
  TEST_ASSERT_EQUAL_UINT32(revision, alarms::revision());
  // El tick ha tornat a la cua a la seva volta següent, i el que mana ara és l'alarma de debò, que és abans.
  TEST_ASSERT_EQUAL_INT64(5000, alarms::next_due_mono_ms());
  TEST_ASSERT_TRUE(alarms::take_due(1000 + 5 * 60000, out));
  TEST_ASSERT_FALSE(out.awake_only);
  TEST_ASSERT_EQUAL_INT64(1000 + 6 * 60000, alarms::next_due_mono_ms());
  // I el que queda no pot despertar la placa: un tick no ho fa mai.
  TEST_ASSERT_EQUAL_INT64(-1, alarms::next_wake_mono_ms());

  // I el desament no se l'emporta.
  char blob[256];
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(alarms::dump(0, 1700000000000LL, blob, sizeof(blob))));
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

// **Cada alarma, dita amb les paraules d'una persona** (0.189.149, l'amo: *«quan es programa una alarma també ho vull veure i a
// quin dispositiu s'envia o quin métode»* / *«Vull que el sistema mostri la llista de alarmes que te i quina es la mes
// propera»*): the library formats the line because these are its fields —a caller that rebuilt it from `at()` would be a
// second reader of the same struct— and the two clocks arrive as numbers, exactly as in `dump`. The tests are the three
// shapes the log and the prompt show: an alarm that is armed, one that repeats every day, and one whose hour is not known
// (docs/30, docs/60).
void test_an_alarm_describes_itself_with_a_person_s_words(void) {
  alarms::Spec once;
  once.app = "clock";
  once.on_mono_ms = 10000;
  once.payload_kind = alarms::Payload::kText;
  once.payload = "ring";
  const uint32_t id = alarms::add(once);
  TEST_ASSERT_TRUE(id != 0);

  // **Amb hora**: the monotonic deadline and the epoch are the two clocks, so the hour of the line is the one a person reads
  // on the glass —the offset of the system is the third number and it is the caller's, as everywhere (docs/30)—.
  char line[160];
  // 2026-11-16 14:00:00 UTC with five seconds left, in an hour of +01:00: 15:00:05.
  const int64_t epoch_now = 1794837600000LL;
  TEST_ASSERT_TRUE(alarms::describe(0, 5000, epoch_now, 60, line, sizeof(line)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(line, "#"));
  TEST_ASSERT_NOT_NULL(strstr(line, " clock"));
  TEST_ASSERT_NOT_NULL(strstr(line, "in 5 s"));
  TEST_ASSERT_NOT_NULL(strstr(line, "at 15:00:05"));
  TEST_ASSERT_NOT_NULL(strstr(line, "payload text 4 B"));
  TEST_ASSERT_NULL(strstr(line, "repeats"));

  // **Sense hora**: a board whose epoch nobody has set still has a real deadline —the monotonic one— and the line says what
  // is left and nothing else. A `1970` would be a date a person reads as a fact (docs/30, docs/85).
  TEST_ASSERT_TRUE(alarms::describe(0, 5000, 0, 60, line, sizeof(line)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(line, "in 5 s"));
  TEST_ASSERT_NULL(strstr(line, " at "));

  // **I una que es repeteix cada dia, amb el que li queda i el que fa quan sona**: the daily one names the day and not the
  // number of seconds, because that is how the page of the alarms says it (docs/60).
  alarms::reset();
  alarms::Spec daily = make("clock", 90000000, static_cast<int32_t>(alarms::kDailyMs));
  daily.persist = true;
  daily.raise_to_front = true;
  TEST_ASSERT_TRUE(alarms::add(daily) != 0);
  TEST_ASSERT_TRUE(alarms::describe(0, 0, 0, 0, line, sizeof(line)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(line, "repeats daily"));
  TEST_ASSERT_NOT_NULL(strstr(line, "persist"));
  TEST_ASSERT_NOT_NULL(strstr(line, "raises the app"));

  // **I una que ja era a punt de sonar diu quant tard arriba**, que és el que el registre escriu quan la lliura.
  alarms::reset();
  TEST_ASSERT_TRUE(alarms::add(make("clock", 1000)) != 0);
  TEST_ASSERT_TRUE(alarms::describe(0, 4000, 0, 0, line, sizeof(line)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(line, "3 s late"));
}

// **El que no hi és no es descriu i el que no hi cap no es parteix** (0.189.149): an index past the queue and a buffer of four
// bytes are the two ways this formatter can be asked for something it cannot give, and both have to answer zero instead of
// a line cut in the middle —the log reads the answer, and a half line is worse than no line (docs/30).
void test_describe_refuses_what_it_cannot_say(void) {
  char line[160];
  TEST_ASSERT_EQUAL_UINT32(0, alarms::describe(0, 0, 0, 0, line, sizeof(line)));
  TEST_ASSERT_TRUE(alarms::add(make("clock", 1000)) != 0);
  TEST_ASSERT_EQUAL_UINT32(0, alarms::describe(1, 0, 0, 0, line, sizeof(line)));

  char tiny[4];
  TEST_ASSERT_EQUAL_UINT32(0, alarms::describe(0, 0, 0, 0, tiny, sizeof(tiny)));
  TEST_ASSERT_EQUAL_UINT8('\0', static_cast<uint8_t>(tiny[0]));
}

// **Una app desinstal·lada no deixa res armat** (0.189.157, l'amo: *«I ja que estem, que passa amb les alarmes que ha deixat
// l'app al sistema si es desinstala?»*): `remove_of` takes the entries of that app —the one that is due first and the one far
// away, the single and the repeating— and leaves every other app's alarm where it was, in the order it was. An empty name is
// not an app and takes nothing with it, because a bug that passed `NULL` would empty the queue of everyone (docs/60).
void test_remove_of_takes_the_alarms_of_one_app(void) {
  TEST_ASSERT_TRUE(alarms::add(make("battery", 1000, 600000)) != 0);
  TEST_ASSERT_TRUE(alarms::add(make("clock", 2000)) != 0);
  TEST_ASSERT_TRUE(alarms::add(make("battery", 3000)) != 0);
  TEST_ASSERT_TRUE(alarms::add(make("battery", 4000, static_cast<int32_t>(alarms::kDailyMs))) != 0);
  TEST_ASSERT_EQUAL_UINT32(4, alarms::count());

  TEST_ASSERT_EQUAL_UINT32(3, alarms::remove_of("battery"));
  TEST_ASSERT_EQUAL_UINT32(1, alarms::count());
  TEST_ASSERT_EQUAL_STRING("clock", alarms::at(0)->app);
  TEST_ASSERT_EQUAL_INT64(2000, alarms::next_due_mono_ms());

  // Nothing of that app is left to take, and asking again is not an error.
  TEST_ASSERT_EQUAL_UINT32(0, alarms::remove_of("battery"));
  TEST_ASSERT_EQUAL_UINT32(0, alarms::remove_of(""));
  TEST_ASSERT_EQUAL_UINT32(0, alarms::remove_of(nullptr));
  TEST_ASSERT_EQUAL_UINT32(1, alarms::count());
}

// **I el número de la cua canvia, perquè això és el que fa que el despertar es torni a armar**: a queue that lost an entry
// without moving its revision is a clock still armed for an alarm nobody is waiting for (docs/60).
void test_remove_of_moves_the_revision(void) {
  TEST_ASSERT_TRUE(alarms::add(make("gone", 5000)) != 0);
  TEST_ASSERT_TRUE(alarms::add(make("here", 6000)) != 0);
  const uint32_t before = alarms::revision();

  TEST_ASSERT_EQUAL_UINT32(1, alarms::remove_of("gone"));
  TEST_ASSERT_TRUE(alarms::revision() != before);
}

// **Els ticks d'una app, tots fora i els dels altres quiets** (0.192.63, l'amo: *«Només un tick per app. Si una app en demana
// unaltre, s'esborra l'anterior i es fa la que acaben de demanar»*): la porta de `tick.every` esborra els de la pròpia app abans
// d'afegir-hi el nou, i el que no toca és una alarma de debò ni el tick d'una altra app. **La revisió no es mou**: un tick no
// s'escriu a la NVS i treure'l no ha de fer escriure cap clau (docs/30, docs/60).
void test_remove_awake_only_of_takes_the_ticks_of_one_app_and_nothing_else(void) {
  alarms::Spec a_tick = make("stats", 60000, 60000);
  a_tick.awake_only = true;
  alarms::Spec another_tick = make("stats", 70000, 30000);
  another_tick.awake_only = true;
  alarms::Spec other_tick = make("gps", 80000, 60000);
  other_tick.awake_only = true;
  TEST_ASSERT_TRUE(alarms::add(a_tick) != 0);
  TEST_ASSERT_TRUE(alarms::add(another_tick) != 0);
  TEST_ASSERT_TRUE(alarms::add(other_tick) != 0);
  // Una alarma de debò de la mateixa app: no és un tick i no se l'emporta.
  TEST_ASSERT_TRUE(alarms::add(make("stats", 90000)) != 0);
  const uint32_t before = alarms::revision();

  TEST_ASSERT_EQUAL_UINT32(2, alarms::remove_awake_only_of("stats"));
  TEST_ASSERT_EQUAL_UINT32(2, alarms::count());
  TEST_ASSERT_EQUAL_UINT32(before, alarms::revision());

  // El que queda és l'alarma de debò i el tick de l'altra app.
  bool found_alarm = false;
  bool found_other = false;
  for (uint32_t i = 0; i < alarms::count(); ++i) {
    const alarms::Entry* entry = alarms::at(i);
    TEST_ASSERT_TRUE(entry != nullptr);
    if (entry->awake_only) {
      found_other = true;
    } else {
      found_alarm = true;
    }
  }
  TEST_ASSERT_TRUE(found_alarm);
  TEST_ASSERT_TRUE(found_other);

  // I una app que no en té cap no mou res.
  TEST_ASSERT_EQUAL_UINT32(0, alarms::remove_awake_only_of("nobody"));
  TEST_ASSERT_EQUAL_UINT32(2, alarms::count());
}

// **La mateixa promesa no s'hi pot posar dues vegades** (0.189.172, l'amo: *«les alarmes de les apps es multipliquen»*): una
// alarma que es repeteix no té hora pròpia —torna a la cua a la seva ocurrència següent—, o sigui que el seu període i el
// seu payload són tota la configuració, i una altra app amb les mateixes paraules és una altra promesa (docs/30, docs/60).
void test_the_same_configuration_is_found_and_only_for_its_app(void) {
  alarms::Spec spec = make("mine", 5000, 600000);
  spec.payload_kind = alarms::Payload::kText;
  spec.payload = "sample";
  spec.persist = true;
  const uint32_t id = alarms::add(spec);
  TEST_ASSERT_TRUE(id != 0);

  // Asked again at another minute: the same promise, because a repeating alarm has no hour of its own.
  alarms::Spec later = spec;
  later.on_mono_ms = 900000;
  TEST_ASSERT_EQUAL_UINT32(id, alarms::find_same(later));

  // Another app, another word, another period, another signal: four different promises.
  alarms::Spec other_app = later;
  other_app.app = "yours";
  TEST_ASSERT_EQUAL_UINT32(0, alarms::find_same(other_app));
  alarms::Spec other_word = later;
  other_word.payload = "other";
  TEST_ASSERT_EQUAL_UINT32(0, alarms::find_same(other_word));
  alarms::Spec other_period = later;
  other_period.repeat_ms = 1200000;
  TEST_ASSERT_EQUAL_UINT32(0, alarms::find_same(other_period));
  alarms::Spec other_keep = later;
  other_keep.persist = false;
  TEST_ASSERT_EQUAL_UINT32(0, alarms::find_same(other_keep));
}

// **Una alarma d'un sol cop és la seva hora, comparada al segon**: two calls that ask for the same instant are one promise
// and asking for another instant is another one, which is what keeps the next event of an agenda from being doubled by a
// page that was opened twice (docs/30, docs/60).
void test_a_one_shot_is_the_same_instant_to_the_second(void) {
  alarms::Spec once = make("mine", 60000);
  once.payload_kind = alarms::Payload::kText;
  once.payload = "q";
  const uint32_t id = alarms::add(once);
  TEST_ASSERT_TRUE(id != 0);

  alarms::Spec half_a_second_later = once;
  half_a_second_later.on_mono_ms = 60000 + alarms::kSameShotMs;
  TEST_ASSERT_EQUAL_UINT32(id, alarms::find_same(half_a_second_later));

  alarms::Spec another_instant = once;
  another_instant.on_mono_ms = 60000 + alarms::kSameShotMs + 1;
  TEST_ASSERT_EQUAL_UINT32(0, alarms::find_same(another_instant));

  // And a cancel makes room for the new one, which is the recipe an app that wants a fresh deadline follows.
  TEST_ASSERT_TRUE(alarms::cancel("mine", id));
  TEST_ASSERT_EQUAL_UINT32(0, alarms::find_same(once));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_the_queue_is_ordered_by_deadline);
  RUN_TEST(test_take_due_only_takes_what_is_due);
  RUN_TEST(test_a_repeating_alarm_moves_to_its_next_occurrence);
  RUN_TEST(test_a_tick_is_served_by_the_loop_and_never_arms_the_wake);
  RUN_TEST(test_only_the_owner_cancels);
  RUN_TEST(test_the_alarm_keeps_its_own_copy);
  RUN_TEST(test_the_list_has_a_ceiling);
  RUN_TEST(test_an_alarm_without_an_app_is_refused);
  RUN_TEST(test_dump_and_load_keep_the_persistent_alarms);
  RUN_TEST(test_dump_without_an_epoch_writes_nothing);
  RUN_TEST(test_the_revision_moves_when_the_queue_changes);
  RUN_TEST(test_an_alarm_describes_itself_with_a_person_s_words);
  RUN_TEST(test_describe_refuses_what_it_cannot_say);
  RUN_TEST(test_remove_of_takes_the_alarms_of_one_app);
  RUN_TEST(test_remove_of_moves_the_revision);
  RUN_TEST(test_remove_awake_only_of_takes_the_ticks_of_one_app_and_nothing_else);
  RUN_TEST(test_the_same_configuration_is_found_and_only_for_its_app);
  RUN_TEST(test_a_one_shot_is_the_same_instant_to_the_second);
  return UNITY_END();
}
