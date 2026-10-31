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

bool get_strings(const char* text, size_t len, const char* key,
                 char out[][kStringItemCap], size_t max, uint32_t& count) {
  count = 0;
  if (text == nullptr || key == nullptr || out == nullptr) return false;
  size_t i = find_value(text, len, key);
  if (i >= len || text[i] != '[') return false;
  ++i;

  while (i < len) {
    while (i < len && (is_space(text[i]) || text[i] == ',')) ++i;
    if (i >= len) break;
    if (text[i] == ']') return true;
    if (text[i] != '"' || count >= max) return false;
    ++i;

    // One item, with the same escapes as a plain string. An item that does not fit
    // refuses the whole list: a board name cut in half would never match.
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
      if (used + 1 >= kStringItemCap) return false;
      out[count][used++] = c;
      ++i;
    }
    if (i >= len) return false;  // the item was never closed
    out[count][used] = '\0';
    if (used == 0) return false;  // an empty board name names nothing
    ++count;
    ++i;  // past the closing quote
  }
  return false;  // the array was never closed
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

}  // namespace json
}  // namespace lunok
