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

// **El bloc de l'històric** (0.190.262): the editor keeps no bytes of its own any more, so every editor that is proved with
// a history gets the same block lent, which is what the shell does with two kilobytes of the extra memory.
char g_history[line::kHistoryBytes];

void give_the_history(line::Editor& editor) { editor.history_in(g_history, sizeof g_history); }

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
  give_the_history(editor);
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
  TEST_ASSERT_EQUAL_UINT16(2, editor.history_count());
}

// **The block decides how many commands are kept** (0.190.262): one is dropped only when the new one does not fit, so the
// block is spent on the letters that were really typed and not on a slot per command (docs/30, docs/75).
static void test_the_block_decides_how_many_commands_are_kept() {
  line::Editor editor;
  char small[12];
  editor.history_in(small, sizeof small);
  // A one-letter command is two bytes —the length and the letter—, so six of them are exactly the block.
  for (int i = 1; i <= 7; ++i) {
    char text[8];
    text[0] = static_cast<char>('0' + i);
    text[1] = '\0';
    remember(editor, text);
  }
  TEST_ASSERT_EQUAL_UINT16(6, editor.history_count());
  for (int i = 7; i >= 2; --i) {
    TEST_ASSERT_TRUE(editor.previous());
    char expected[8];
    expected[0] = static_cast<char>('0' + i);
    expected[1] = '\0';
    TEST_ASSERT_EQUAL_STRING(expected, editor.text());
  }
  // The oldest one went out: walking back stops there.
  TEST_ASSERT_FALSE(editor.previous());
  editor.clear();
  TEST_ASSERT_EQUAL_UINT16(0, editor.history_count());
  TEST_ASSERT_FALSE(editor.previous());
}

// **Two kilobytes hold far more than the eight slots did**, which is the reason the block is not a ring of fixed rows.
static void test_a_bigger_block_keeps_more_than_eight_commands() {
  line::Editor editor;
  give_the_history(editor);
  for (int i = 0; i < 40; ++i) {
    char text[8];
    text[0] = 'c';
    text[1] = static_cast<char>('0' + (i / 10));
    text[2] = static_cast<char>('0' + (i % 10));
    text[3] = '\0';
    remember(editor, text);
  }
  TEST_ASSERT_EQUAL_UINT16(40, editor.history_count());
  for (int i = 0; i < 40; ++i) TEST_ASSERT_TRUE(editor.previous());
  TEST_ASSERT_EQUAL_STRING("c00", editor.text());
  TEST_ASSERT_FALSE(editor.previous());
}

// **A prompt with no block is a prompt with no history**, and not a crash: the line, the cursor and the arrows of the line
// all work, and up and down do nothing (docs/30, docs/75).
static void test_a_prompt_without_a_block_has_a_line_and_no_history() {
  line::Editor editor;
  TEST_ASSERT_FALSE(editor.has_history());
  remember(editor, "one");
  TEST_ASSERT_EQUAL_UINT16(0, editor.history_count());
  TEST_ASSERT_FALSE(editor.previous());
  TEST_ASSERT_FALSE(editor.next());
}

// **A command that does not fit the block is dropped whole** and the history stays as it was: half a command is worse than
// none, and a command longer than the block is not a command that can be walked back to (docs/30, docs/75).
static void test_a_command_that_does_not_fit_the_block_is_dropped_whole() {
  line::Editor editor;
  char tiny[4];
  editor.history_in(tiny, sizeof tiny);
  remember(editor, "abcd");
  TEST_ASSERT_EQUAL_UINT16(0, editor.history_count());
  remember(editor, "abc");
  TEST_ASSERT_EQUAL_UINT16(1, editor.history_count());
  TEST_ASSERT_TRUE(editor.previous());
  TEST_ASSERT_EQUAL_STRING("abc", editor.text());
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
  RUN_TEST(test_the_block_decides_how_many_commands_are_kept);
  RUN_TEST(test_a_bigger_block_keeps_more_than_eight_commands);
  RUN_TEST(test_a_prompt_without_a_block_has_a_line_and_no_history);
  RUN_TEST(test_a_command_that_does_not_fit_the_block_is_dropped_whole);
  RUN_TEST(test_the_line_does_not_grow_past_its_buffer);
  return UNITY_END();
}
