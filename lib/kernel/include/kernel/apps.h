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
  // **The id is the name the door presents** (0.88.20): `squares` for the app of the flash and `squares-SD` for the copy
  // of the card, so it has room for the suffix on top of the longest folder the disks carry.
  char id[40];
  char title[48];
  char version[16];
  char entry_file[48];
  // **The extensions this app handles** (0.120.0): one name or several, lowercase and without the dot. The
  // manifests of the apps of PaperOS wrote a list from the first day —`"opens": "txt md lua json"`, fifteen of
  // them in its Editor— and this system read a single one; now a manifest may say a list and every one of them
  // goes to the same handler (docs/40). An app that handles none is the same as before: it opens nothing.
  static const uint32_t kMaxOpens = 8;
  char opens[kMaxOpens][12];
  uint32_t opens_count;
  // **The switches the app wants when it is handed a file** (0.120.0): `opensArgs`, which PaperOS's Editor
  // writes as `-ro`. The system does not read them: it hands them to the app in `app.args.flags`, because only
  // the app knows what its own switches mean (docs/30, docs/40).
  char opens_arg[32];
  // **The picture the app wants on the glass before it starts** (0.114.0): a file inside the app, written in
  // the manifest as `"splash": "res/logo.png"`, which the system paints washed whole and in the middle with
  // the same call the moments use and takes away with the first page the app presents. It is decoration and
  // not a requirement: an app without one, or with a name that is not a picture, runs all the same (docs/40).
  char splash[48];
  // **The badge of the app** (0.120.0): `icon`, the picture a listing would paint next to the name —the app
  // folders of PaperOS carry an `icon.icn`—. There is no launcher on the glass yet, so this system reads it,
  // checks it as it checks the splash and says it in `apps info`; it paints nothing with it (docs/40).
  char icon[48];
  // **Who wrote the app and what it is** (0.120.0): forty-eight and forty-nine of the sixty-one apps of
  // `apps/` write these two and the system read neither. They are decoration —a manifest without them is still
  // an app— and `apps info` is where they are said (docs/40).
  char author[32];
  char description[160];

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
  // **An app that is a launcher of its own** (0.153.0): the manifest says `"launcher": true` and a launcher's grid does
  // not list it. It is still an app —`apps` shows it, `run` starts it, it opens the files it handles— and the system
  // does not guess it from a title or an id: the app says it (docs/30, docs/40).
  bool launcher;
  // **Which half of the door answered** (0.88.20): the same id may sit on both disks and the door presents them apart, so
  // the listing says which of the two it read (docs/40).
  bool on_card;
};

// True when the app says it runs on this board: because it names the board itself
// or the family the board belongs to. An app with no list runs nowhere. It is a
// function so that the kernel and the tests ask the question the same way (docs/40).
bool compatible(const Entry& entry, const char* board_id, const char* family);

// Return false to stop the walk.
typedef bool (*Visitor)(const Entry& entry, void* context);

// **The one door of the apps** (0.88.20): `/apps` is the virtual directory that puts the flash's `apps/` and the card's
// `apps/` together, and it is the only place the system looks —the listing, the `run` and the files of an app all go
// through it—. A name the flash has and the card also has is presented **once as `name` and once as `name-SD`**, so the
// two can be read and run apart, and what the door presents is what the system decides to present: a folder that is not
// an app is not in it, and a disk the system did not offer is never read (docs/40, docs/70).
extern const char kAppsDoor[];

// **Whether there is a card in the slot**: the mount says so and not a folder, because a card the system could not
// mount is not a place to look for anything in (docs/50, docs/70).
bool card_here();

// **The id of the door of an app, `name` or `name-SD`**: `/apps/<id>`, which the filesystem answers from the disk that
// has that app (docs/40, docs/70).
bool root_of(const char* id, char* out, size_t cap);

// **What the door of the apps presents** (0.88.20): the kernel hands this to the VFS, which asks it for every name of
// `/apps` and lists only the ones it accepts. `disk` is the half of the door the name came from (`/littlefs/apps` or
// `/sd/apps`) and `name` is the folder on that disk: a folder that carries a `manifest.json` is an app there. An app that
// names another board is still presented —the listing says what it needs— because hiding it would leave a person with an
// app on their disk and no trace of it (docs/40, docs/70).
bool is_an_app(const char* disk, const char* name);

// Walks the door of the apps and loads one manifest at a time. When only_id is not null it stops
// after that app. Returns how many apps loaded correctly.
uint32_t for_each(const char* only_id, Visitor visitor, void* context);

// Normalises an extension the way a manifest writes it: "TXT", ".txt" and "txt"
// are the same thing. Writes at most cap bytes and returns false when there is
// nothing usable.
bool normalise_extension(const char* text, char* out, size_t cap);

// True when the app says it opens that extension (already normalised). An app
// that declares none opens nothing, which is not the same as opening everything
// (docs/40).
bool handles(const Entry& entry, const char* ext);

// The whole list of an app, written the way a manifest writes it —`"txt md lua"`—, which is what
// `app.info().opens` answers and what `apps info` shows. 96 B is the most it can be: eight names of
// eleven bytes and the seven spaces between them. Returns how many bytes it wrote.
size_t opens_text(const Entry& entry, char* out, size_t cap);

// The extension of a path, normalised. False when the name has none.
bool extension_of(const char* path, char* out, size_t cap);

// Reads the manifest of one app. False when it is not an app of this build: no
// title, no entry, an entry file that is not there, no `boards` list, or a list
// that does not name this board. A caller that does not need the answer can use
// `for_each`, which walks the directory and skips those (docs/40).
bool read(const char* id, Entry& out);

// **The door as the interpreter sees it** (0.153.0): the list `apps.list()` draws and the wish `app.run` makes. It is
// installed once, at boot, and it lives here because the door is what knows what an app is: the walk merges the two
// disks, skips a folder that is not an app and reads the manifest, and a launcher doing that from Lua would be a second
// reader of the same file (docs/30, docs/40).
void install_apps_ops();

// The first app (in directory order) that declares "opens" for ext. When more
// than one does, matches says how many; the caller decides what to say.
bool find_for_extension(const char* ext, Entry& out, uint32_t& matches);
}  // namespace apps
}  // namespace kernel

#endif  // LUNOK_KERNEL_APPS_H
