// **El lector del recompte d'usos** (0.189.14): one place that turns the session `usage` of the page that counts into an
// ordered table, so the prompt's `apps`, the launcher's grid and anything that comes later read the same numbers
// (docs/30, docs/40, docs/75).
#include "kernel/usage.h"

#include <stdio.h>
#include <string.h>

#include "kernel/session.h"

namespace kernel {
namespace usage {
namespace {

// **The key of the page that counts the uses**, and the whole convention of this module: the counts are a session named
// `usage`, whoever keeps it. It is a name of the system even though the block is the app's, like `state` is
// (docs/30, docs/45).
const char kKey[] = "usage";

// **The order of the page**: the most opened first, and the name when two are equal, so that the list a person reads today
// is the list they read tomorrow (docs/40). An insertion sort, which is the right size for a table of two dozen rows and
// needs no memory of its own.
void sort_by_count(Table& table) {
  for (uint8_t i = 1; i < table.rows; ++i) {
    char id[kIdBytes];
    memcpy(id, table.ids[i], kIdBytes);
    const uint32_t count = table.count[i];
    uint8_t at = i;
    while (at > 0) {
      const bool goes_before = (table.count[at - 1] < count) ||
                               (table.count[at - 1] == count && strcmp(table.ids[at - 1], id) > 0);
      if (!goes_before) break;
      memcpy(table.ids[at], table.ids[at - 1], kIdBytes);
      table.count[at] = table.count[at - 1];
      --at;
    }
    memcpy(table.ids[at], id, kIdBytes);
    table.count[at] = count;
  }
}

// **One line is one app**: `id<TAB>count` and nothing else, which is the format the launcher writes (docs/40). Anything
// else in the block —a tail cut by a smaller read, the zero the kernel leaves, a line a future version writes— is skipped
// instead of guessed.
void take_line(Table& table, const char* line, size_t length) {
  if (table.rows >= kMaxRows || length < 3) return;
  const char* tab = static_cast<const char*>(memchr(line, '\t', length));
  if (tab == nullptr) return;
  const size_t id_length = static_cast<size_t>(tab - line);
  if (id_length == 0 || id_length >= kIdBytes) return;
  // **And the count is digits**: a sign, a space or a unit is not a count, and a count that is not read is a count of zero,
  // which is the right answer for a line this module does not understand (docs/40).
  uint32_t count = 0;
  size_t digits = 0;
  for (const char* p = tab + 1; p < line + length; ++p, ++digits) {
    if (*p < '0' || *p > '9') return;
    if (count > 429496729U) return;  // it would not fit in the number this table keeps
    count = count * 10 + static_cast<uint32_t>(*p - '0');
  }
  if (digits == 0) return;
  // **An id that is already in the table is not a second row**: the page writes one line an app, and a block with a repeat
  // is a block being written when it was read, so the first line is the one that counts.
  for (uint8_t i = 0; i < table.rows; ++i) {
    if (strncmp(table.ids[i], line, id_length) == 0 && table.ids[i][id_length] == '\0') return;
  }
  memcpy(table.ids[table.rows], line, id_length);
  table.ids[table.rows][id_length] = '\0';
  table.count[table.rows] = count;
  ++table.rows;
}

// **Which app keeps the count** (0.189.36): the session whose key is `usage` and not a name written here, so a launcher
// called anything at all is read the same. It is a visitor and not a loop over the rows of an array, because the sessions
// of this board are however many the apps have asked for: the first row with that key stops the walk, and the app it names
// is the one whose block is read (docs/30, docs/75).
struct TheCount {
  char app[session::kNameBytes] = {};
  bool found = false;
};

bool take_the_count(const session::Entry& row, void* user) {
  TheCount* mine = static_cast<TheCount*>(user);
  if (strcmp(row.key, kKey) != 0) return true;
  snprintf(mine->app, sizeof(mine->app), "%s", row.app);
  mine->found = true;
  return false;
}

}  // namespace

uint8_t read(Table& out) {
  out.rows = 0;

  TheCount mine;
  (void)session::each(take_the_count, &mine);
  if (!mine.found) return 0;

  char text[kReadBytes];
  const size_t got = session::copy(mine.app, kKey, text, sizeof(text));
  if (got == 0) return 0;
  // **A text cut in the middle is not read to the end**: a line that lost its last digit would be a count that is
  // smaller than the real one, and the page writes the lines with a newline after each of them (docs/40).
  size_t length = got;
  while (length > 0 && text[length - 1] != '\n') --length;
  if (length == 0) return 0;

  size_t start = 0;
  for (size_t at = 0; at < length; ++at) {
    if (text[at] != '\n') continue;
    take_line(out, text + start, at - start);
    start = at + 1;
  }
  sort_by_count(out);
  return out.rows;
}

uint32_t count_of(const Table& table, const char* id) {
  if (id == nullptr) return 0;
  for (uint8_t i = 0; i < table.rows; ++i) {
    if (strcmp(table.ids[i], id) == 0) return table.count[i];
  }
  return 0;
}

}  // namespace usage
}  // namespace kernel
