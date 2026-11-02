// The line editor of the serial prompt: a cursor inside one line, and the history the up and down keys
// walk. It is pure on purpose, so a terminal is not needed to prove what the arrows do (docs/30, docs/85).
#include <stddef.h>
#include <string.h>

#include <unity.h>

#include "kernel/line_editor.h"

void setUp(void) {}

void tearDown(void) {}

namespace {

namespace line = kernel::line;

void type(line::Editor& editor, const char* text) {
  for (const char* p = text; *p != '\0'; ++p) TEST_ASSERT_TRUE(editor.insert(*p));
}

void remember(line::Editor& editor, const char* text) {
  type(editor, text);
  editor.remember();
  TEST_ASSERT_EQUAL_STRING("", editor.text());
  TEST_ASSERT_EQUAL_UINT32(0, editor.length());
  TEST_ASSERT_EQUAL_UINT32(0, editor.cursor());
}

}  // namespace

// **A cursor is not an end**: a letter goes where the cursor is, and a backspace takes the letter before
// it, not the last one of the line (docs/30).
static void test_a_letter_goes_at_the_cursor_and_backspace_takes_the_one_before_it() {
  line::Editor editor;
  type(editor, "abc");
  TEST_ASSERT_EQUAL_STRING("abc", editor.text());
  TEST_ASSERT_EQUAL_UINT32(3, editor.cursor());
  TEST_ASSERT_TRUE(editor.left());
  TEST_ASSERT_EQUAL_UINT32(2, editor.cursor());
  TEST_ASSERT_TRUE(editor.insert('X'));
  TEST_ASSERT_EQUAL_STRING("abXc", editor.text());
  TEST_ASSERT_EQUAL_UINT32(3, editor.cursor());
  TEST_ASSERT_TRUE(editor.backspace());
  TEST_ASSERT_EQUAL_STRING("abc", editor.text());
  TEST_ASSERT_EQUAL_UINT32(2, editor.cursor());
  TEST_ASSERT_TRUE(editor.erase_forward());
  TEST_ASSERT_EQUAL_STRING("ab", editor.text());
  TEST_ASSERT_FALSE(editor.end());
  TEST_ASSERT_EQUAL_UINT32(2, editor.cursor());
  TEST_ASSERT_TRUE(editor.home());
  TEST_ASSERT_EQUAL_UINT32(0, editor.cursor());
  TEST_ASSERT_FALSE(editor.left());
  TEST_ASSERT_FALSE(editor.home());
  TEST_ASSERT_TRUE(editor.end());
  TEST_ASSERT_EQUAL_UINT32(2, editor.cursor());
  TEST_ASSERT_FALSE(editor.right());
}

// **Up is older and down is newer**, and the words that were being typed come back when the newest entry
// is passed; an empty line does not become a command (docs/30).
static void test_history_up_and_down_keep_the_line_that_was_being_typed() {
  line::Editor editor;
  remember(editor, "one");
  remember(editor, "two");
  type(editor, "draft");

  TEST_ASSERT_TRUE(editor.previous());
  TEST_ASSERT_EQUAL_STRING("two", editor.text());
  TEST_ASSERT_EQUAL_UINT32(3, editor.cursor());
  TEST_ASSERT_TRUE(editor.previous());
  TEST_ASSERT_EQUAL_STRING("one", editor.text());
  TEST_ASSERT_FALSE(editor.previous());
  TEST_ASSERT_TRUE(editor.next());
  TEST_ASSERT_EQUAL_STRING("two", editor.text());
  TEST_ASSERT_TRUE(editor.next());
  TEST_ASSERT_EQUAL_STRING("draft", editor.text());
  TEST_ASSERT_FALSE(editor.next());

  // The same command twice is one entry, and an empty line is no entry at all.
  editor.reset();
  remember(editor, "two");
  remember(editor, "");
  TEST_ASSERT_EQUAL_UINT8(2, editor.history_count());
}

// The history is a ring of eight: the ninth command pushes the first one out, and walking back stops at
// the oldest one that is still there (docs/75).
static void test_the_history_keeps_the_last_eight_commands() {
  line::Editor editor;
  for (int i = 1; i <= 9; ++i) {
    char text[8];
    text[0] = static_cast<char>('0' + i);
    text[1] = '\0';
    remember(editor, text);
  }
  TEST_ASSERT_EQUAL_UINT8(line::kHistoryEntries, editor.history_count());
  for (int i = 9; i >= 2; --i) {
    TEST_ASSERT_TRUE(editor.previous());
    char expected[8];
    expected[0] = static_cast<char>('0' + i);
    expected[1] = '\0';
    TEST_ASSERT_EQUAL_STRING(expected, editor.text());
  }
  TEST_ASSERT_FALSE(editor.previous());
  editor.clear();
  TEST_ASSERT_EQUAL_UINT8(0, editor.history_count());
  TEST_ASSERT_FALSE(editor.previous());
}

// The line has the same ceiling as the shell had: 127 printable bytes and one terminator, and a byte
// that does not fit is refused instead of writing past the buffer (docs/30).
static void test_the_line_does_not_grow_past_its_buffer() {
  line::Editor editor;
  for (size_t i = 0; i + 1 < line::kLineBytes; ++i) TEST_ASSERT_TRUE(editor.insert('x'));
  TEST_ASSERT_EQUAL_UINT32(line::kLineBytes - 1, editor.length());
  TEST_ASSERT_FALSE(editor.insert('x'));
  TEST_ASSERT_EQUAL_UINT32(line::kLineBytes - 1, editor.length());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_letter_goes_at_the_cursor_and_backspace_takes_the_one_before_it);
  RUN_TEST(test_history_up_and_down_keep_the_line_that_was_being_typed);
  RUN_TEST(test_the_history_keeps_the_last_eight_commands);
  RUN_TEST(test_the_line_does_not_grow_past_its_buffer);
  return UNITY_END();
}
