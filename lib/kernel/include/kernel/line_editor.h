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
// **The history is lent and it is as many commands as fit** (0.190.262, l'amo: *«si l'històric de les comandes ocupa massa és
// perquè és una llista fixa, no? no trobes que és millor que es guardin lletra per lletra en el seu propi psmalloc»*): eight
// slots of 128 bytes were 1 024 B of `.bss` for at most eight commands, and `mem` —three letters— wasted a hundred and
// twenty-four bytes of its slot. The editor keeps the letters back to back in a block the caller lends —the shell asks
// `pal::extra_memory()` for it once— with the entries written `[length][letters]`, the oldest first (docs/30, docs/75).
const size_t kHistoryBytes = 2048;  // what the shell asks for: some hundred and twenty commands of sixteen letters

class Editor {
 public:
  Editor();
  // **Where the commands are kept**, given and not taken: the editor allocates nothing of its own. `nullptr` is a prompt
  // with the line and no history, which is what a board that can give no block gets (docs/30, docs/75).
  void history_in(char* bytes, size_t cap);
  bool has_history() const { return history_ != nullptr; }
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
  uint16_t history_count() const { return history_count_; }

 private:
  // The letters of an entry, walking back from the one that was written last: `len` is how many there are, because they
  // are not terminated —the length byte in front of them is the terminator— and the block is not asked to waste a byte
  // per command on a zero that nobody reads.
  bool entry_from_end(uint16_t back, const char*& letters, uint8_t& len) const;
  void write_the_entry(const char* letters, size_t len);

  char line_[kLineBytes];
  size_t len_ = 0;
  size_t cursor_ = 0;
  char draft_[kLineBytes];
  // **[length][letters] back to back, the oldest first**, in the block the caller lent (0.190.262).
  char* history_ = nullptr;
  size_t history_cap_ = 0;
  size_t history_used_ = 0;
  uint16_t history_count_ = 0;
  int16_t history_offset_ = -1;  // -1 is the line being typed, 0 the newest entry
};

}  // namespace line
}  // namespace kernel

#endif  // LUNOK_KERNEL_LINE_EDITOR_H
