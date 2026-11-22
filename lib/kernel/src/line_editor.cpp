#include "kernel/line_editor.h"

#include <string.h>

namespace kernel {
namespace line {

Editor::Editor() { clear(); }

void Editor::history_in(char* bytes, size_t cap) {
  history_ = bytes;
  history_cap_ = (bytes != nullptr) ? cap : 0;
  history_used_ = 0;
  history_count_ = 0;
  history_offset_ = -1;
  if (history_ != nullptr) memset(history_, 0, history_cap_);
}

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
  history_used_ = 0;
  if (history_ != nullptr) memset(history_, 0, history_cap_);
}

// **The n-th command counted from the newest**: 0 is the last one written, and the walk starts at the oldest because
// that is the order the block is written in (docs/30, docs/75).
bool Editor::entry_from_end(uint16_t back, const char*& letters, uint8_t& len) const {
  if (history_ == nullptr || back >= history_count_) return false;
  size_t at = 0;
  for (size_t skip = static_cast<size_t>(history_count_ - 1 - back); skip > 0; --skip) {
    at += 1 + static_cast<uint8_t>(history_[at]);
  }
  len = static_cast<uint8_t>(history_[at]);
  letters = history_ + at + 1;
  return true;
}

// **A command goes in and the oldest comes out only if it has to**: a block that is not full is not touched, which is what
// makes the same two kilobytes hold eight commands of a hundred and twenty letters or a hundred and twenty of six
// (docs/30, docs/75).
void Editor::write_the_entry(const char* letters, size_t len) {
  if (len > kLineBytes - 1) len = kLineBytes - 1;
  if (len == 0) return;
  while (history_used_ + 1 + len > history_cap_ && history_count_ > 0) {
    const size_t oldest = 1 + static_cast<uint8_t>(history_[0]);
    memmove(history_, history_ + oldest, history_used_ - oldest);
    history_used_ -= oldest;
    --history_count_;
  }
  // A command longer than the whole block is not a command that can be walked back to, and half of one is worse than
  // none: it is dropped and the history stays as it was.
  if (1 + len > history_cap_) return;
  history_[history_used_] = static_cast<char>(len);
  memcpy(history_ + history_used_ + 1, letters, len);
  history_used_ += 1 + len;
  if (history_count_ < 0xffff) ++history_count_;
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
  } else if (history_offset_ + 1 < static_cast<int16_t>(history_count_)) {
    ++history_offset_;
  } else {
    return false;
  }
  const char* letters = nullptr;
  uint8_t len = 0;
  if (!entry_from_end(static_cast<uint16_t>(history_offset_), letters, len)) return false;
  memcpy(line_, letters, len);
  line_[len] = '\0';
  len_ = len;
  cursor_ = len_;
  return true;
}

bool Editor::next() {
  if (history_offset_ < 0) return false;
  if (history_offset_ > 0) {
    --history_offset_;
    const char* letters = nullptr;
    uint8_t len = 0;
    if (!entry_from_end(static_cast<uint16_t>(history_offset_), letters, len)) return false;
    memcpy(line_, letters, len);
    line_[len] = '\0';
    len_ = len;
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
  if (len > 0 && history_ != nullptr) {
    const char* newest = nullptr;
    uint8_t newest_len = 0;
    const bool same = history_count_ > 0 && entry_from_end(0, newest, newest_len) && newest_len == len &&
                      memcmp(newest, line_, len) == 0;
    if (!same) write_the_entry(line_, len);
  }
  reset();
}

}  // namespace line
}  // namespace kernel
