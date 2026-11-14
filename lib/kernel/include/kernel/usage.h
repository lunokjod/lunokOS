// **El recompte d'usos: quantes vegades s'ha obert cada app** (0.189.14, docs/30, docs/40, docs/75): the number is the
// page's own —`home_eink` writes it into a session named `usage`, one `id<TAB>count` per line, on every tap— and this is
// **the one reader**, so the grid of the launcher and the text list of the prompt come from the same numbers and cannot
// disagree about the order of the same apps.
//
// **The key is the whole convention**: a page that counts the uses keeps them under `usage`, and this module looks for that
// key among the sessions of the board instead of knowing the id of the launcher. A board whose launcher is called another
// thing works all the same, and a board with no page counting anything answers zero rows and the prompt lists the apps in
// the door's order, which is what it did before the switch existed.
#ifndef LUNOK_KERNEL_USAGE_H
#define LUNOK_KERNEL_USAGE_H

#include <stddef.h>
#include <stdint.h>

namespace kernel {
namespace usage {

// **How many apps a count of uses may name**: the block the page keeps holds a line of about twenty bytes an app, and its
// first ask is a kilobyte, so a board with a crowd of apps fits with room to spare; twenty-four rows of the longest id is
// what that kilobyte holds at worst, and a page with more apps than that has a page whose counts are cut, which the log of
// the page says (docs/75).
const uint8_t kMaxRows = 24;
// **The id of an app and not a path**: the door's longest name is the id of an app folder with the redirect's mark on top,
// which is forty bytes including the terminator, so this is that and no more.
const size_t kIdBytes = 40;
// **How much of the block is read**: the page asks for a kilobyte and falls back to 512 and 256 B, so this is the first ask
// and not a second ceiling of this module.
const size_t kReadBytes = 1024;

// **What a count of uses says, in the order it says it**: the most opened first, and the name breaks a tie so that two apps
// with the same count —which is every app the first day— keep an order a person can predict (docs/40).
struct Table {
  char ids[kMaxRows][kIdBytes] = {};
  uint32_t count[kMaxRows] = {};
  uint8_t rows = 0;
};

// **The count of the uses of this board**, read from the session `usage` of the page that keeps it and sorted. Answers how
// many rows it filled: zero when no page counts anything here, which is not an error and means "the door's order"
// (docs/40).
uint8_t read(Table& out);

// **How many times this app has been opened**, and zero when the count does not name it —an app that has never been
// opened, or one that is on the disk and not in the block—.
uint32_t count_of(const Table& table, const char* id);

}  // namespace usage
}  // namespace kernel

#endif  // LUNOK_KERNEL_USAGE_H
