// Apps are not kept in a list. /apps is walked when someone asks, and one
// manifest is in memory at a time, so there is no limit on how many apps there
// may be. See docs/40.
#ifndef LUNOK_KERNEL_APPS_H
#define LUNOK_KERNEL_APPS_H

#include <stddef.h>
#include <stdint.h>

namespace kernel {
namespace apps {

struct Entry {
  char id[32];
  char title[48];
  char version[16];
  char entry_file[48];
  char opens[16];  // the extension this app handles, lowercase and without the dot

  // The boards this app says it runs on, as the manifest writes them. A name is a
  // board (`m5stack-atom-matrix`) or a chip family (`esp32`), and every board of
  // that family accepts an app that says the family. An app that declares none runs
  // nowhere: the list is not optional (docs/40).
  static const uint32_t kMaxBoards = 8;
  char boards[kMaxBoards][24];
  uint32_t board_count;
  // False when the list above does not name this board nor its family: the app is
  // still an app of the disk —`apps` lists it and says what it needs— but it is not
  // one this board can run (docs/40).
  bool works_here;
};

// True when the app says it runs on this board: because it names the board itself
// or the family the board belongs to. An app with no list runs nowhere. It is a
// function so that the kernel and the tests ask the question the same way (docs/40).
bool compatible(const Entry& entry, const char* board_id, const char* family);

// Return false to stop the walk.
typedef bool (*Visitor)(const Entry& entry, void* context);

// **On viuen les apps d'aquesta placa** (0.86.0): la flash, on les posa el firmware, i la targeta, on les posa la
// persona amb `repo install`. Una app de la targeta **mana sobre la de la flash** —el mateix id instal·lat a la
// targeta és el que s'executa, i treure la targeta torna la de la flash—, i el disc que no hi és no compta
// (docs/40, docs/70).
bool root_of(const char* id, char* out, size_t cap);

// Walks the app roots and loads one manifest at a time. When only_id is not null it stops
// after that app. Returns how many apps loaded correctly.
uint32_t for_each(const char* only_id, Visitor visitor, void* context);

// How many apps load. Used by the boot log.
uint32_t count();

// Normalises an extension the way a manifest writes it: "TXT", ".txt" and "txt"
// are the same thing. Writes at most cap bytes and returns false when there is
// nothing usable.
bool normalise_extension(const char* text, char* out, size_t cap);

// The extension of a path, normalised. False when the name has none.
bool extension_of(const char* path, char* out, size_t cap);

// Reads the manifest of one app. False when it is not an app of this build: no
// title, no entry, an entry file that is not there, no `boards` list, or a list
// that does not name this board. A caller that does not need the answer can use
// `for_each`, which walks the directory and skips those (docs/40).
bool read(const char* id, Entry& out);

// The first app (in directory order) that declares "opens" for ext. When more
// than one does, matches says how many; the caller decides what to say.
bool find_for_extension(const char* ext, Entry& out, uint32_t& matches);

}  // namespace apps
}  // namespace kernel

#endif  // LUNOK_KERNEL_APPS_H
