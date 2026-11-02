// The line editor of the serial prompt: one line, a cursor inside it, and the last commands the person
// typed. It is pure on purpose —no console, no clock, no allocation— so the shell can own the terminal
// codes and the suite can prove the editing without a terminal (docs/30, docs/85).
#ifndef LUNOK_KERNEL_LINE_EDITOR_H
#define LUNOK_KERNEL_LINE_EDITOR_H

#include <stddef.h>
#include <stdint.h>

namespace kernel {
namespace line {

const size_t kLineBytes = 128;
// Eight commands is what a person can walk back through without a screen; sixteen would be another
// kilobyte of static RAM for a history nobody reads at a serial prompt (docs/75).
const uint8_t kHistoryEntries = 8;

class Editor {
 public:
  Editor();
  // Empties the line and the cursor, but not the history: the history belongs to the session.
  void reset();
  // Empties everything, including the history. Used when the shell itself starts.
  void clear();

  bool insert(char c);
  bool backspace();
  bool erase_forward();
  bool left();
  bool right();
  bool home();
  bool end();

  // **Walking the history**: up is older, down is newer. The line that was being typed is kept
  // separately and comes back when the newest entry is passed, which is what a person expects.
  bool previous();
  bool next();
  // Puts the current line in the history and starts a new one. Empty lines and a line equal to the
  // newest one do not add an entry.
  void remember();

  const char* text() const { return line_; }
  char* data() { return line_; }
  size_t length() const { return len_; }
  size_t cursor() const { return cursor_; }
  bool empty() const { return len_ == 0; }
  uint8_t history_count() const { return history_count_; }

 private:
  char line_[kLineBytes];
  size_t len_ = 0;
  size_t cursor_ = 0;
  char draft_[kLineBytes];
  char history_[kHistoryEntries][kLineBytes];
  uint8_t history_count_ = 0;
  uint8_t history_next_ = 0;  // where the next entry goes
  int8_t history_offset_ = -1;  // -1 is the line being typed, 0 the newest entry
};

}  // namespace line
}  // namespace kernel

#endif  // LUNOK_KERNEL_LINE_EDITOR_H
