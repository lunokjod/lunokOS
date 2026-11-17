// **The memory of a session, kept by the kernel and named by the app** (0.179.0): a cache an app builds once and finds
// again, which is what makes the second page of a launcher cheaper than the first (docs/30, docs/75).
//
// **I des de la 0.189.7 no es perd en apagar la placa** (0.189.11): the block lives in the extra memory while the app is on
// the glass and it has a file of its own in `/session/<app>/<key>`; the allocator takes the block when something else does
// not fit —writing it first—, and a board that is restarted or switched off **parks every block before it goes**. The keys
// are **of the app and not of the system**: `"apps"` is one block for `home_eink` and another for the app that comes after
// it.
#ifndef LUNOK_KERNEL_SESSION_H
#define LUNOK_KERNEL_SESSION_H

#include <stddef.h>
#include <stdint.h>

namespace kernel {
namespace session {

// **There is no ceiling on the number of blocks** (0.189.36, the owner: *«el número... ja haviem parlat que no vull límits
// artificials. Les apps han de poder fer totes les sesions que vulguin. fins que la ram o el disc rebenti!»*): a block is a
// node of a list and not a row of a fixed table, so an app asks for one more and gets it while the extra memory has room —
// the only limit is the memory itself, and a board that runs out says it once in its log instead of answering "the table is
// full" with three megabytes free (docs/30, docs/75). A node costs 48 bytes of the board's own RAM, which is what eight rows
// of the old table cost (docs/75).
const size_t kNameBytes = 16;

// **The block of `key` for `app`, which is made the first time and found after that**: `bytes` is what is asked for and
// `got` is what it really has —the block that was already there, or what the allocator gave—. A block that is made is
// **zeroed**, so an app can tell "nothing was cached" from "this is my cache" with a word of its own. nullptr is "there
// is no room", which is not an error of the app: it goes on without a cache (docs/30).
void* acquire(const char* app, const char* key, size_t bytes, size_t& got);

// **The block is given back** by the app that owns it, for the case of an app that wants the memory back before it ends.
// The system does not call this when an app stops: the whole point of a session memory is that the next run finds it.
bool release(const char* app, const char* key);

// **Every block goes**, which is what the allocator asks when an ask has failed and there is no app in front to protect. It
// is one switch on the relief of the PAL and it is also a door of the prompt (`mem` says how many are held).
//
// **`keep` is the block the failed ask is about**, and it stays: it is the one an app is growing, and giving it back here
// would leave the allocator moving a block that is not there any more (`pal::set_memory_relief`, 0.179.6). `bytes` is what
// the ask wanted, which is what the warning of the log says so a person can tell why the memory was short. The prompt and
// a test call it with nothing, and then every block goes.
void release_all(void* keep = nullptr, size_t bytes = 0);

// **The memory is short and this is the order a person expects** (0.189.11): the caches of the apps that are **not** in
// front go first —they are the ones nobody is looking at— and only if that is not enough does the board take the caches of
// `current`, the app on the glass, which will ask for them again when it needs them and finds them on the disk. Each block
// is written before it is given back, so nothing is lost by being short of memory (docs/30, docs/75).
//
// `keep` is the block the ask is about and it is never taken (see `release_all`); an empty `current` is a board with no app
// in front and then every block is somebody else's.
void relieve(const char* current, void* keep, size_t asked);

// What the list holds, for `mem` and for a test. `uint32_t` and not a `uint8_t`: the number of blocks is the memory's and
// not a constant of this header (0.189.36).
uint32_t count();
size_t bytes_held();

// **I la caché també viu al disc** (0.189.7): the block of a session is written to `/session/<app>/<key>` —the card's
// folder when there is a card and the flash's when there is not— and read back the next time the app asks for it. That is
// what makes a cache outlive the board: the launcher of this system is the app that pays for a cold start, and a board
// that is switched off once a day started cold every morning (docs/30, docs/75).
//
// **The memory is given back only when it is short**: the block of the app that has just ended stays where it is —the next
// run of that app finds it without touching the card— and the allocator takes it when something else does not fit, writing
// it first. One app at a time is on this board, so nothing is competing for the room (docs/75).
const char kDoor[] = "/session";

// **One row of a listing, for the prompt and for a test**: who asked, under which key, how many bytes, whether the block is
// in the memory now, whether the file of it is on the disk, and **where that file is** —the path of the VFS, which is what
// a person needs to go and look at a session that is only a file— (docs/30, docs/70, docs/75). The names are copied here
// because a cold row comes from a listing and a listing's names are only alive during the call.
struct Entry {
  char app[kNameBytes] = {};
  char key[kNameBytes] = {};
  size_t size = 0;
  bool in_memory = false;
  bool on_disk = false;
  char path[48] = {};
};

// **Cada fila del llistat, una a la vegada** (0.189.36): the prompt reads the sessions the board holds **as they come**, so a
// board with twenty of them says twenty and a board with a hundred says a hundred. It is a visitor and not an array of rows
// because an array of rows is a ceiling dressed as a caller —the prompt has eight kilobytes of stack and a session that did
// not fit in it was a session a person could not see— (docs/30, docs/75). `visit` answers false to stop, and `nullptr` is a
// call that only wants the count.
using RowSink = bool (*)(const Entry& row, void* user);
uint32_t each(RowSink visit, void* user);

// **The same list into the caller's own array**, which is what a test reads: `cap` is the room the caller has and not a
// ceiling of the system — the answer is how many rows were written, and `each(nullptr, nullptr)` is how many there are—.
// The blocks of the memory go first, with the path of the file each one has or would have, and then the files of `/session`
// that no block of the memory is holding —the cold sessions of the apps that have not run since the board came up—.
uint32_t entries(Entry* out, uint32_t cap);

// **Els bytes d'un bloc, per a qui no n'és l'amo** (0.189.14): the memory if it is there —it is the newest copy, because an
// app writes into it and only the disk gets the flushed one— and the file of `/session/<app>/<key>` when it is not. It is
// what the prompt reads a page's `usage` with, so the order the text list shows and the order the grid draws come from the
// same numbers, and not from two counts that drift apart (docs/30, docs/40, docs/75). Answers how many bytes were copied;
// zero when there is no such block and no such file.
size_t copy(const char* app, const char* key, char* out, size_t cap);

// **El que un desament ha fet, i el que no ha calgut fer** (0.189.53): `written` is a file whose bytes went to the disk now
// and `unchanged` is a block whose bytes the disk already has —the flash is what a session would wear out, and a park whose
// content has not moved is a park that writes nothing (l'amo: *«Si es el mateix bloc, no cal gravar-lo, només farem malbé
// abans la flash»*)—. The two numbers are apart because «no he escrit res» and «no hi havia res a escriure» are two things a
// person reads differently in a log (docs/30, docs/75).
struct Parked {
  uint32_t written = 0;
  uint32_t unchanged = 0;
};

// **Les cachés d'una app van al disc quan la seva app s'acaba**: it is what a board does when an app ends —the cache that
// app built is written to `/session/<id>/<key>` so the next run of it starts warm even if the board was switched off in
// between— and the block **stays in the memory**, because giving it back is the allocator's business and only when it is
// short (docs/30, docs/75). A block whose bytes are the ones of its file is **not written again** (0.189.53).
Parked flush(const char* app);

// **Everything the memory holds goes to the disk, and it says so one block at a time** (0.189.11): this is the door of a
// board that is being restarted or switched off, and of a run that ends. The blocks stay where they are —the board is
// going, and the next one comes up with the files— and the log of each one says **who, under which key, how many bytes and
// at what path**, which is what makes a session that did not come back explainable (docs/30, docs/75). Answers how many
// files were written and how many were already on the disk (0.189.53).
Parked park_all(const char* why);

// **El que s'ha escrit, comptat al disc** (0.189.18, l'amo: *«Haurà donat temps a gravar-ho tot al SD?? s'ha apagat tot
// molt ràpid»*): every block of the memory is looked for as a file of `/session/<app>/<key>` and its size compared with the
// block's. A count and not a `bool`, because what a person wants to read is «2 of 2» and what a log has to show is the
// number that did not land. It is a `stat` per block and it is asked once, on the way out (docs/75).
struct Checked {
  uint32_t files = 0;
  uint32_t ok = 0;
};
Checked check_parked();

// **`sessions destroy [<id> [<key>]]`**: les sessions d'una app —o de totes, o una de sola— van de la memòria **i** del disc,
// i és l'única porta que llença una caché a posta. Buidar la memòria no n'hi ha prou: el fitxer de la propera arrencada
// tornaria la mateixa caché que una persona ha demanat oblidar (docs/30, docs/75).
struct Forgotten {
  uint32_t blocks = 0;
  uint32_t files = 0;
  size_t bytes = 0;
};
Forgotten forget(const char* app);

// **Una sola sessió: `<app> <clau>`** (0.189.16): the memory of that block and its file, and nothing else of that app. It
// is what `sessions destroy <id> <key>` asks and what `sys.session_destroy(key)` asks from Lua with its own id, because
// an app that throws away one cache and keeps the others is a page a person can make start over without losing what it
// learned about everything (docs/30, docs/75). A block that is not there is not an error: the answer says zero.
Forgotten forget(const char* app, const char* key);

// **Una clau, a totes les apps** (0.189.16): `sessions destroy <clau>` when no app is called like that. The keys of a
// session are the app's, but a word like `apps` or `usage` is the name of a thing of the system and a person who types it
// means that thing —a `destroy` that answers "0 block(s)" to a word that is on the screen of the listing is a command
// that looks broken— (docs/30).
//
// **`say` és per a qui no vol les paraules de la comanda** (0.189.167): the boot throws `state` away on its own, and a line
// that says `sessions destroy 'state' (the session, of every app): 0 block(s) and 0 file(s), 0 B` at every start is a line
// that reads as if a person had asked for it. False and the work is done in silence; the caller says its own sentence, and
// only when there was something to forget (docs/30, docs/75).
Forgotten forget_key(const char* key, bool say = true);

// **A number that only goes up when a block goes back** (0.179.0): an object of an app that holds a block tells with it
// whether the block is still the one it was given, or the relief of the allocator has taken it (docs/75, docs/90).
uint32_t generation();

}  // namespace session
}  // namespace kernel

#endif  // LUNOK_KERNEL_SESSION_H
