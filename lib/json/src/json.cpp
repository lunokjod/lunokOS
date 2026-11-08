#include "json/json.h"

#include <string.h>

namespace lunok {
namespace json {
namespace {

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// Position of the value that follows "key" : , or len when the key is not there.
// A key only counts when it is a quoted string immediately followed by ':', so a
// key name appearing inside a value cannot be mistaken for a key.
size_t find_value(const char* text, size_t len, const char* key) {
  const size_t key_len = strlen(key);
  size_t i = 0;
  while (i < len) {
    if (text[i] != '"') {
      ++i;
      continue;
    }
    const size_t start = i + 1;
    if (start + key_len < len && memcmp(text + start, key, key_len) == 0 &&
        text[start + key_len] == '"') {
      size_t j = start + key_len + 1;
      while (j < len && is_space(text[j])) ++j;
      if (j < len && text[j] == ':') {
        ++j;
        while (j < len && is_space(text[j])) ++j;
        return j;
      }
    }
    // Not our key: skip to the closing quote so a value cannot match.
    ++i;
    while (i < len && text[i] != '"') {
      if (text[i] == '\\' && i + 1 < len) ++i;
      ++i;
    }
    ++i;
  }
  return len;
}

}  // namespace

bool get_string(const char* text, size_t len, const char* key, char* out, size_t out_cap) {
  if (text == nullptr || key == nullptr || out == nullptr || out_cap == 0) return false;
  size_t i = find_value(text, len, key);
  if (i >= len || text[i] != '"') return false;
  ++i;

  size_t used = 0;
  while (i < len && text[i] != '"') {
    char c = text[i];
    if (c == '\\' && i + 1 < len) {
      ++i;
      switch (text[i]) {
        case 'n':
          c = '\n';
          break;
        case 't':
          c = '\t';
          break;
        case 'r':
          c = '\r';
          break;
        default:
          c = text[i];
          break;
      }
    }
    if (used + 1 < out_cap) out[used++] = c;
    ++i;
  }
  out[used] = '\0';
  return true;
}

Strings get_strings(const char* text, size_t len, const char* key, char out[][kStringItemCap], size_t max,
                    uint32_t& count, uint32_t* total) {
  count = 0;
  if (total != nullptr) *total = 0;
  if (text == nullptr || key == nullptr || out == nullptr) return Strings::kAbsent;
  size_t i = find_value(text, len, key);
  if (i >= len) return Strings::kAbsent;
  if (text[i] != '[') return Strings::kNotArray;
  ++i;

  // **The list is walked whole even when it does not fit** (0.88.11): past `max` the items are counted and thrown away
  // instead of ending the walk there, because how many there are is the difference between "this manifest does not say
  // which boards it runs on" and "it names nine and this system carries eight". **The first thing that goes wrong is
  // what is said**, in the order of the document (docs/40, docs/75).
  uint32_t seen = 0;
  Strings reason = Strings::kOk;
  while (i < len) {
    while (i < len && (is_space(text[i]) || text[i] == ',')) ++i;
    if (i >= len) break;
    if (text[i] == ']') {
      count = (seen < max) ? seen : static_cast<uint32_t>(max);
      if (total != nullptr) *total = seen;
      return reason;
    }
    if (text[i] != '"') return Strings::kUnclosed;
    ++i;

    // One item, with the same escapes as a plain string. An item that does not fit refuses the whole list: a board name
    // cut in half would never match.
    char item[kStringItemCap];
    size_t used = 0;
    bool cut = false;
    while (i < len && text[i] != '"') {
      char c = text[i];
      if (c == '\\' && i + 1 < len) {
        ++i;
        switch (text[i]) {
          case 'n':
            c = '\n';
            break;
          case 't':
            c = '\t';
            break;
          case 'r':
            c = '\r';
            break;
          default:
            c = text[i];
            break;
        }
      }
      if (used + 1 >= sizeof(item)) {
        cut = true;
      } else {
        item[used++] = c;
      }
      ++i;
    }
    if (i >= len) return (reason != Strings::kOk) ? reason : Strings::kUnclosed;  // the item was never closed
    item[used] = '\0';
    if (cut && reason == Strings::kOk) reason = Strings::kItemTooLong;
    if (used == 0 && reason == Strings::kOk) reason = Strings::kEmptyItem;
    if (seen < max) {
      memcpy(out[seen], item, used + 1);
    } else if (reason == Strings::kOk) {
      reason = Strings::kTooMany;
    }
    ++seen;
    ++i;  // past the closing quote
  }
  return Strings::kUnclosed;  // the array was never closed
}

bool get_int(const char* text, size_t len, const char* key, long& out) {
  if (text == nullptr || key == nullptr) return false;
  size_t i = find_value(text, len, key);
  if (i >= len) return false;

  bool negative = false;
  if (text[i] == '-') {
    negative = true;
    ++i;
  }
  if (i >= len || text[i] < '0' || text[i] > '9') return false;

  long value = 0;
  while (i < len && text[i] >= '0' && text[i] <= '9') {
    value = value * 10 + (text[i] - '0');
    ++i;
  }
  out = negative ? -value : value;
  return true;
}

// **A word and not a prefix of one** (0.153.0): `trueish` is not `true`, and a value that is not a boolean is refused
// instead of being read as one — the caller that wants to know whether a manifest says it is a launcher has to be able
// to tell "it says false" from "it says nothing this layer understands" (docs/40).
bool ends_the_word(const char* text, size_t len, size_t i) {
  if (i >= len) return true;
  const char c = text[i];
  return c == ',' || c == '}' || c == ']' || c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool has_key(const char* text, size_t len, const char* key) {
  if (text == nullptr || key == nullptr) return false;
  return find_value(text, len, key) < len;
}

bool get_bool(const char* text, size_t len, const char* key, bool& out) {
  if (text == nullptr || key == nullptr) return false;
  const size_t i = find_value(text, len, key);
  if (i >= len) return false;
  if (i + 4 <= len && memcmp(text + i, "true", 4) == 0 && ends_the_word(text, len, i + 4)) {
    out = true;
    return true;
  }
  if (i + 5 <= len && memcmp(text + i, "false", 5) == 0 && ends_the_word(text, len, i + 5)) {
    out = false;
    return true;
  }
  return false;
}

}  // namespace json
}  // namespace lunok
