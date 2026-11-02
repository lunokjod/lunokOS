#include "kernel/line_editor.h"

#include <string.h>

namespace kernel {
namespace line {

Editor::Editor() { clear(); }

void Editor::reset() {
  len_ = 0;
  cursor_ = 0;
  line_[0] = '\0';
  draft_[0] = '\0';
  history_offset_ = -1;
}

void Editor::clear() {
  reset();
  history_count_ = 0;
  history_next_ = 0;
  memset(history_, 0, sizeof(history_));
}

bool Editor::insert(char c) {
  if (len_ + 1 >= kLineBytes) return false;
  for (size_t i = len_; i > cursor_; --i) line_[i] = line_[i - 1];
  line_[cursor_] = c;
  ++cursor_;
  ++len_;
  line_[len_] = '\0';
  return true;
}

bool Editor::backspace() {
  if (cursor_ == 0) return false;
  for (size_t i = cursor_ - 1; i < len_; ++i) line_[i] = line_[i + 1];
  --cursor_;
  --len_;
  line_[len_] = '\0';
  return true;
}

bool Editor::erase_forward() {
  if (cursor_ >= len_) return false;
  for (size_t i = cursor_; i < len_; ++i) line_[i] = line_[i + 1];
  --len_;
  line_[len_] = '\0';
  return true;
}

bool Editor::left() {
  if (cursor_ == 0) return false;
  --cursor_;
  return true;
}

bool Editor::right() {
  if (cursor_ >= len_) return false;
  ++cursor_;
  return true;
}

bool Editor::home() {
  if (cursor_ == 0) return false;
  cursor_ = 0;
  return true;
}

bool Editor::end() {
  if (cursor_ == len_) return false;
  cursor_ = len_;
  return true;
}

bool Editor::previous() {
  if (history_count_ == 0) return false;
  if (history_offset_ < 0) {
    memcpy(draft_, line_, len_ + 1);
    history_offset_ = 0;
  } else if (history_offset_ + 1 < static_cast<int8_t>(history_count_)) {
    ++history_offset_;
  } else {
    return false;
  }
  const uint8_t newest =
      static_cast<uint8_t>((history_next_ + kHistoryEntries - 1) % kHistoryEntries);
  const uint8_t at = static_cast<uint8_t>((newest + kHistoryEntries - history_offset_) % kHistoryEntries);
  len_ = strlen(history_[at]);
  memcpy(line_, history_[at], len_ + 1);
  cursor_ = len_;
  return true;
}

bool Editor::next() {
  if (history_offset_ < 0) return false;
  if (history_offset_ > 0) {
    --history_offset_;
    const uint8_t newest =
        static_cast<uint8_t>((history_next_ + kHistoryEntries - 1) % kHistoryEntries);
    const uint8_t at =
        static_cast<uint8_t>((newest + kHistoryEntries - history_offset_) % kHistoryEntries);
    len_ = strlen(history_[at]);
    memcpy(line_, history_[at], len_ + 1);
  } else {
    history_offset_ = -1;
    len_ = strlen(draft_);
    memcpy(line_, draft_, len_ + 1);
  }
  cursor_ = len_;
  return true;
}

void Editor::remember() {
  // **The shell may have trimmed the line in place** before it asks to remember it, and the editor's
  // length is not the shell's: the string itself is the truth (docs/30).
  const size_t len = strlen(line_);
  if (len > 0) {
    const uint8_t newest =
        static_cast<uint8_t>((history_next_ + kHistoryEntries - 1) % kHistoryEntries);
    const bool same = history_count_ > 0 && strcmp(history_[newest], line_) == 0;
    if (!same) {
      memcpy(history_[history_next_], line_, len + 1);
      history_next_ = static_cast<uint8_t>((history_next_ + 1) % kHistoryEntries);
      if (history_count_ < kHistoryEntries) ++history_count_;
    }
  }
  reset();
}

}  // namespace line
}  // namespace kernel
