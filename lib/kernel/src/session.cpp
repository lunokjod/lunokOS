// The memory of a session (docs/30, docs/75): one block per (app, key), made once, kept while the app is on the glass and
// written to `/session/<app>/<key>` when it is given back, so the next run of the app finds it (0.189.7).
#include "kernel/session.h"

#include <stdio.h>
#include <string.h>

#include "hal/hal.h"
#include "kernel/log.h"
#include "kernel/power.h"
#include "kernel/meter.h"
#include "kernel/vfs.h"
#include "pal/pal.h"

namespace kernel {
namespace session {
namespace {

// **A block is a node of a list and not a row of a table** (0.189.36, the owner: *«el número... ja haviem parlat que no vull
// límits artificials. Les apps han de poder fer totes les sesions que vulguin. fins que la ram o el disc rebenti!»*): the
// table of eight rows that this module was born with said "the table of 8 is full" with megabytes of extra memory free,
// which is a ceiling that has nothing to do with the memory of the board. A node is made when an app asks for a block and it
// goes when the block goes, so the only limit is what the allocator answers —and it answers `nullptr`, which is an app
// without a cache and not a page that cannot run— (docs/30, docs/75).
// **Un bloc pot viure en dos llocs** (0.190.174): the extra memory —the PSRAM of the Paper—, where it is a pointer and a
// `memcpy`, or the memory behind the chip's bank switcher, where it is a region that is read and written by offset because
// the chip only has a 32 KB window of it mapped at a time. `bytes` and `banked` are never both: the first is the block and
// the second is the region, and the rest of this file asks `store_*` for the bytes and does not care which (docs/50,
// docs/75).
struct Block {
  Block* next = nullptr;
  char app[kNameBytes];
  char key[kNameBytes];
  void* bytes = nullptr;
  hal::IBankedMemory* banked = nullptr;
  size_t size = 0;
  // **La petjada dels bytes que el disc té d'aquest bloc** (0.189.53): the number of what was read from the file —or of the
  // zeros, when the file is not there— and of what was last written. A park whose bytes have this same number is a park
  // that has nothing to do, and the flash is the thing a session would wear out (l'amo: *«Si es el mateix bloc, no cal
  // gravar-lo, només farem malbé abans la flash»*, docs/30, docs/75).
  uint32_t seen = 0;
};

// **The node of a block lives in the board's own RAM and the bytes it names live where the app's memory lives**: 48 bytes
// per session against the sixteen kilobytes of a picture, so a hundred sessions cost what one face of a font costs —and the
// bytes go to the extra memory through `allocate(..., true)` as they always did— (docs/75).
Block* g_blocks = nullptr;
uint32_t g_held = 0;
uint32_t g_generation = 1;

// **A partir de quants bytes una caché va a la memòria del banc** (0.190.174): the caches of this system are between a few
// kilobytes and a couple of megabytes, and the ones that matter are the launcher's `apps` (32 KB) and the index of an app
// with a long list. From 32 KB up —the block the bank maps, and the size the launcher already asks for— the block goes to
// the four megabytes behind the switcher, and the PSRAM is left for the faces, the fonts and the pictures, which is what it
// was put there for (docs/50, docs/75).
const size_t kBankFromBytes = 32 * 1024;

// **Una petjada dels bytes del bloc, i prou** (0.189.53): FNV-1a of 32 bits over what the block holds, which is a
// multiplication and an exclusive-or a byte —a kilobyte is a few microseconds and a thirty-two-kilobyte cache of the
// launcher is less than a millisecond— against the eleven of a page program and the forty-five thousand of the sector an
// erase costs (docs/75, docs/95). **I per què una petjada i no un senyal de «algú ha escrit»**: every app of this system
// saves its shape with a `clear()` and a `write()` (docs/30), so a flag would say "written" every time even when the page
// is the one the disk has. The number is what the bytes are, and two writes that end in the same bytes end in the same
// number. What a collision would cost is one park that is not made over bytes that are not the block's —one in four
// thousand million—, and the thing it would keep is a cache or a page that is already stale by a hair, which the next park
// fixes.
// **Els bytes d'un bloc, per la porta que hi hagi** (0.190.174): a block of the extra memory is copied with a `memcpy` and
// a block of the bank asks its region for the piece. Nothing outside this file sees the difference, which is what let the
// session memory keep its offsets —`block:read(offset, n)` and `block:write(offset, text)` were built that way on the first
// day— while the place the bytes live changed (docs/30, docs/50, docs/75).
const uint32_t kStoreChunk = 4096;

// The footprint of a block of the extra memory is the same number a park compares (0.189.53); the door is defined below.
uint32_t hash_of(const void* bytes, size_t size);

uint32_t store_read(const Block& block, uint32_t offset, void* out, uint32_t bytes) {
  if (offset >= block.size || bytes == 0) return 0;
  if (bytes > block.size - offset) bytes = static_cast<uint32_t>(block.size - offset);
  if (block.banked != nullptr) return block.banked->read(offset, out, bytes);
  memcpy(out, static_cast<const uint8_t*>(block.bytes) + offset, bytes);
  return bytes;
}

uint32_t store_write(Block& block, uint32_t offset, const void* in, uint32_t bytes) {
  if (offset >= block.size || bytes == 0) return 0;
  if (bytes > block.size - offset) bytes = static_cast<uint32_t>(block.size - offset);
  if (block.banked != nullptr) return block.banked->write(offset, in, bytes);
  memcpy(static_cast<uint8_t*>(block.bytes) + offset, in, bytes);
  return bytes;
}

// **Els bytes que el bloc no cobreix són paper** (0.189.7): a region of the bank has no `memset` to call, so the zeros are
// written in pieces like everything else (docs/75).
void store_zero(Block& block, uint32_t from) {
  if (from >= block.size) return;
  if (block.banked == nullptr) {
    memset(static_cast<uint8_t*>(block.bytes) + from, 0, block.size - from);
    return;
  }
  static const uint8_t zeros[kStoreChunk] = {};
  uint32_t at = from;
  while (at < block.size) {
    const uint32_t take = (block.size - at < kStoreChunk) ? static_cast<uint32_t>(block.size - at) : kStoreChunk;
    if (block.banked->write(at, zeros, take) != take) break;
    at += take;
  }
}

// **I la regió es torna com es va demanar**: a block of the extra memory goes back to the allocator and a region of the bank
// is closed, which is the only way the next block can have it (docs/75).
void store_free(Block& block) {
  if (block.banked != nullptr) {
    hal::platform().close_banked_memory(block.banked);
    block.banked = nullptr;
  }
  if (block.bytes != nullptr) {
    pal::extra_memory().release(block.bytes);
    block.bytes = nullptr;
  }
}

// The footprint of a block, over its bytes wherever they are: the park that decides not to write asks this and not a flag
// (0.189.53, docs/75).
uint32_t store_hash(const Block& block) {
  if (block.banked == nullptr) return hash_of(block.bytes, block.size);
  uint8_t chunk[kStoreChunk];
  uint32_t hash = 2166136261u;
  uint32_t at = 0;
  while (at < block.size) {
    uint32_t take = (block.size - at < kStoreChunk) ? static_cast<uint32_t>(block.size - at) : kStoreChunk;
    take = block.banked->read(at, chunk, take);
    if (take == 0) break;
    for (uint32_t i = 0; i < take; ++i) {
      hash ^= chunk[i];
      hash *= 16777619u;
    }
    at += take;
  }
  return hash;
}

uint32_t hash_of(const void* bytes, size_t size) {
  const uint8_t* it = static_cast<const uint8_t*>(bytes);
  uint32_t hash = 2166136261u;  // the offset basis of FNV-1a
  for (size_t i = 0; i < size; ++i) {
    hash ^= it[i];
    hash *= 16777619u;  // the prime of FNV-1a
  }
  return hash;
}

// **A name is copied and cut**: the app is the id of a folder and the key is a word of the app's, and neither is a
// pointer into an interpreter that dies (docs/30).
void copy_name(char* out, const char* text) {
  size_t at = 0;
  if (text != nullptr) {
    for (; text[at] != '\0' && at + 1 < kNameBytes; ++at) out[at] = text[at];
  }
  out[at] = '\0';
}

Block* find(const char* app, const char* key) {
  for (Block* mine = g_blocks; mine != nullptr; mine = mine->next) {
    if (strcmp(mine->app, app) == 0 && strcmp(mine->key, key) == 0) return mine;
  }
  return nullptr;
}

// **The node goes back the moment its block does**, so a board that has given every cache back holds no table at all: it is
// what makes the count of a session cost nothing on a board that is not using them (docs/75).
void free_node(Block* mine) {
  if (mine == nullptr) return;
  Block** link = &g_blocks;
  while (*link != nullptr && *link != mine) link = &(*link)->next;
  if (*link == nullptr) return;
  *link = mine->next;
  if (g_held > 0) --g_held;
  pal::extra_memory().release(mine);
}

// **A name of a file and not a word of Lua** (0.189.7): the id of an app and the key of a session are strings an app
// writes, and a string with a slash in it is a folder and not a name —`../../cfg` would be a session that writes over the
// settings of the board—. Everything that is not a letter, a digit, a dot, a dash or an underscore becomes an underscore,
// and a name that would end up being `.` or `..` is refused. A name the app asked for and a name on the card can
// therefore differ, and the log says the file's (docs/70).
bool safe_name(const char* in, char* out, size_t cap) {
  if (in == nullptr || out == nullptr || cap < 2) return false;
  size_t at = 0;
  for (; in[at] != '\0' && at + 1 < cap; ++at) {
    const char c = in[at];
    const bool fine = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                      c == '-' || c == '_';
    out[at] = fine ? c : '_';
  }
  out[at] = '\0';
  if (at == 0) return false;
  if (strcmp(out, ".") == 0 || strcmp(out, "..") == 0) return false;
  return true;
}

// `/session/<app>`, which is a folder this module makes when it writes into it.
bool folder_of(const char* app, char* out, size_t cap) {
  char safe[kNameBytes];
  if (!safe_name(app, safe, sizeof(safe))) return false;
  const int written = snprintf(out, cap, "%s/%s", kDoor, safe);
  return written > 0 && static_cast<size_t>(written) < cap;
}

// `/session/<app>/<key>`, which is the file of one session.
bool file_of(const char* app, const char* key, char* out, size_t cap) {
  char safe_app[kNameBytes];
  char safe_key[kNameBytes];
  if (!safe_name(app, safe_app, sizeof(safe_app)) || !safe_name(key, safe_key, sizeof(safe_key))) return false;
  const int written = snprintf(out, cap, "%s/%s/%s", kDoor, safe_app, safe_key);
  return written > 0 && static_cast<size_t>(written) < cap;
}

// **The bytes of a block are written where the next run of the app will look for them**: the folder is made (twice is not
// an error), the file is opened for writing —which is what cuts it, so a cache smaller than the one before does not keep
// the tail of the other— and the bytes go in one write. The answer is what a person needs to know: it happened or it did
// not, and how long the card took (docs/30, docs/70).
//
// **I la línia diu qui, sota quina clau, quants bytes i a quin camí** (0.189.11): the reason is the end of the line, because
// a session that goes to the disk for four different motives —the app has ended, the memory is short, the board is going
// down, a person asked with `sessions destroy`— is the same session and a log that cannot tell them apart cannot explain why
// one came back and another did not (docs/30, docs/75).
bool write_block(const Block& block, const char* why) {
  char folder[96];
  char path[128];
  if (!folder_of(block.app, folder, sizeof(folder)) || !file_of(block.app, block.key, path, sizeof(path))) return false;

  hal::IStorage* storage = nullptr;
  char rel[128];
  if (!vfs::resolve(folder, storage, rel, sizeof(rel)) || storage == nullptr) return false;
  const int64_t started = pal::system().millis();
  (void)storage->mkdir(rel);  // it is there or it is made; a folder that cannot be made is said by the open
  if (!vfs::resolve(path, storage, rel, sizeof(rel)) || storage == nullptr) return false;

  hal::FileSlot slot;
  if (!storage->open(rel, hal::OpenMode::kWrite, slot)) {
    log_printf(pal::kLogWarn, "MEMORY", "%s cannot park '%s' on %s (%s)", block.app, block.key, path, why);
    return false;
  }
  // **A trossos, perquè el bloc pot ser a la memòria que no té punter** (0.190.174): a region of the bank gives a piece at a
  // time and the file takes it the same way, so a cache of two megabytes never needs two megabytes of the board's own RAM
  // (docs/50, docs/70, docs/75).
  uint8_t chunk[kStoreChunk];
  bool ok = true;
  uint32_t at = 0;
  while (at < block.size) {
    uint32_t take = (block.size - at < kStoreChunk) ? static_cast<uint32_t>(block.size - at) : kStoreChunk;
    take = store_read(block, at, chunk, take);
    if (take == 0) {
      ok = false;
      break;
    }
    if (storage->write(slot, chunk, take) != static_cast<int32_t>(take)) {
      ok = false;
      break;
    }
    at += take;
  }
  storage->close(slot);
  log_printf(ok ? pal::kLogInfo : pal::kLogWarn, "MEMORY", "%s parks '%s': %u B to %s in %d ms (%s)", block.app,
             block.key, static_cast<unsigned>(block.size), path,
             static_cast<int>(pal::system().millis() - started), why);
  return ok;
}

// **And back, the same way**: what the card has of a session, written into a block that the caller has already made. The
// answer is how many bytes the block took, and a block bigger than the file keeps its zeros (docs/30, docs/75).
size_t restore_block(const char* app, const char* key, Block& block) {
  char path[128];
  if (!file_of(app, key, path, sizeof(path))) return 0;
  hal::IStorage* storage = nullptr;
  char rel[128];
  if (!vfs::resolve(path, storage, rel, sizeof(rel)) || storage == nullptr) return 0;

  hal::FileSlot slot;
  if (!storage->open(rel, hal::OpenMode::kRead, slot)) return 0;
  uint8_t chunk[kStoreChunk];
  size_t done = 0;
  while (done < block.size) {
    const uint32_t room = (block.size - done < kStoreChunk) ? static_cast<uint32_t>(block.size - done) : kStoreChunk;
    const int32_t got = storage->read(slot, chunk, room);
    if (got <= 0) break;
    const uint32_t wrote = store_write(block, static_cast<uint32_t>(done), chunk, static_cast<uint32_t>(got));
    done += wrote;
    if (wrote < static_cast<uint32_t>(got)) break;
  }
  storage->close(slot);
  return done;
}

// The size of the file of a session, which is written below with the reader: a park asks it before it decides that it has
// nothing to do (0.189.53).
size_t disk_size(const char* app, const char* key);

// **UN BLOC QUE NO HA CANVIAT NO ES TORNA A ESCRIURE** (0.189.53, l'amo: *«quan l'app intenta guardar l'status fent un park a
// la sessió… Si es el mateix bloc, no cal gravar-lo, només farem malbé abans la flash»*): a park is asked for four ways of
// the system —the app has ended (every close!), the board is going down, the memory is short, the app gives the block
// back— and until now all four wrote the file whether or not a byte of it had moved. An app that saves its shape on every
// focus change and closes without touching anything (every launcher, every time an app ends) was writing the same kilobyte
// to the disk over and over.
//
// **The question is answered by the bytes and not by a flag**: a flag raised from `l_session_write` would say «algú ha
// escrit» even when the app wrote back the very same text, which is exactly what a `clear()` and a `write()` of the shape
// do (docs/30) — the number is what the bytes are.
//
// **I el disc ha de tenir el fitxer, no només els mateixos bytes al cap** (0.189.53): a `stat` is asked before the skip,
// perquè un fitxer esborrat amb `sessions destroy` o una targeta que ha canviat no es pot confondre amb un desament fet.
// Un `stat` costa molt menys que una pàgina i el que compra és que «ja hi és» sigui cert.
//
// `wrote` is what the caller counts: `false` is «el disc ja els tenia» and it is not a failure (docs/30, docs/75).
bool park_block(Block& block, const char* why, bool& wrote) {
  wrote = false;
  const uint32_t now = store_hash(block);
  if (now == block.seen && disk_size(block.app, block.key) == block.size) {
    log_printf(pal::kLogInfo, "MEMORY", "%s does not park '%s': the %u B on the disk are the ones it has (%s)", block.app,
               block.key, static_cast<unsigned>(block.size), why);
    return true;
  }
  if (!write_block(block, why)) return false;
  block.seen = now;
  wrote = true;
  return true;
}

// **And back**: what the card holds of a session, into a block the caller has already made. A file bigger than the block
// is cut —the app asked for a block of a size and a cache of another one is a cache the app cannot read— and the answer
// says how many bytes were read (docs/30, docs/75).
size_t read_block(const char* app, const char* key, void* into, size_t cap) {
  char path[128];
  if (!file_of(app, key, path, sizeof(path))) return 0;
  hal::IStorage* storage = nullptr;
  char rel[128];
  if (!vfs::resolve(path, storage, rel, sizeof(rel)) || storage == nullptr) return 0;

  hal::Stat info;
  if (!storage->stat(rel, info) || info.type != hal::NodeType::kFile) return 0;

  hal::FileSlot slot;
  if (!storage->open(rel, hal::OpenMode::kRead, slot)) return 0;
  const int32_t got = storage->read(slot, into, static_cast<uint32_t>(cap));
  storage->close(slot);
  return (got > 0) ? static_cast<size_t>(got) : 0;
}

// **What the card holds**, without reading it: the size of the file of a session, or zero when there is none. It is asked
// before the block is made, because a cache of another size is a block of the wrong size (docs/75).
size_t disk_size(const char* app, const char* key) {
  char path[128];
  if (!file_of(app, key, path, sizeof(path))) return 0;
  hal::IStorage* storage = nullptr;
  char rel[128];
  if (!vfs::resolve(path, storage, rel, sizeof(rel)) || storage == nullptr) return 0;

  hal::Stat info;
  if (!storage->stat(rel, info) || info.type != hal::NodeType::kFile) return 0;
  return info.size;
}

// **One name of a folder, copied**: a listing hands names that live only as long as the call, and a caller that removes a
// file after finding it is a caller that must own the name (0.189.11, docs/70).
struct Listed {
  char name[kNameBytes];
  bool is_dir;
  uint32_t size;
};

// **The listing of a folder grows with the folder** (0.189.36): the eight rows of this sink were the eight blocks of the old
// table, and a folder of `/session` with nine sessions in it was a listing that stopped at eight without saying so —a
// `sessions destroy` that answered "8 files" over a folder of twenty—. It copies names into the stack while they fit there
// (the common case pays nothing) and past that into the home heap, and a memory that cannot give the room is said by `full()`.
class Names : public hal::ListSink {
 public:
  Names() = default;
  ~Names() override { give_back(); }
  Names(const Names&) = delete;
  Names& operator=(const Names&) = delete;

  bool on_entry(const hal::ListEntry& entry) override {
    if (entry.name == nullptr) return false;
    if (count_ >= cap_ && !grow()) {
      full_ = true;
      return false;
    }
    snprintf(out_[count_].name, sizeof(out_[count_].name), "%s", entry.name);
    out_[count_].is_dir = (entry.type == hal::NodeType::kDir);
    out_[count_].size = entry.size;
    ++count_;
    return true;
  }
  uint32_t count() const { return count_; }
  const Listed& operator[](uint32_t at) const { return out_[at]; }
  bool full() const { return full_; }

 private:
  bool grow() {
    const uint32_t wanted = (cap_ < 8) ? 8 : cap_ * 2;
    Listed* bigger = static_cast<Listed*>(pal::extra_memory().allocate(wanted * sizeof(Listed), false));
    if (bigger == nullptr) return false;
    for (uint32_t i = 0; i < count_; ++i) bigger[i] = out_[i];
    give_back();
    out_ = bigger;
    cap_ = wanted;
    return true;
  }
  void give_back() {
    if (out_ != small_) pal::extra_memory().release(out_);
    out_ = small_;
    cap_ = kOnTheStack;
  }

  static const uint32_t kOnTheStack = 8;
  Listed small_[kOnTheStack];
  Listed* out_ = small_;
  uint32_t cap_ = kOnTheStack;
  uint32_t count_ = 0;
  bool full_ = false;
};
// **`sessions destroy <id>` on the disk, which is the half that makes the command mean what it says**: the files of a
// session are removed and then the folder that held them, and every file says its name so a person reading the log knows
// what went (0.189.11, docs/30, docs/70, docs/75).
uint32_t remove_a_folder(const char* app) {
  char folder[96];
  if (!folder_of(app, folder, sizeof(folder))) return 0;
  hal::IStorage* storage = nullptr;
  char rel[128];
  if (!vfs::resolve(folder, storage, rel, sizeof(rel)) || storage == nullptr) return 0;
  char base[128];
  snprintf(base, sizeof(base), "%s", rel);

  Names sink;
  uint32_t removed = 0;
  if (vfs::list(folder, sink)) {
    for (uint32_t i = 0; i < sink.count(); ++i) {
      if (sink[i].is_dir) continue;
      char child[160];
      const int written = snprintf(child, sizeof(child), "%s/%s", base, sink[i].name);
      if (written <= 0 || static_cast<size_t>(written) >= sizeof(child)) continue;
      if (!storage->remove(child)) continue;
      ++removed;
      log_printf(pal::kLogInfo, "MEMORY", "%s forgets '%s': %s removed", app, sink[i].name, folder);
    }
  }
  // The folder goes too, and an empty one that is not there is not a fault.
  (void)storage->remove(base);
  return removed;
}

// What every folder of the door holds, thrown away: the other half of `sessions destroy` with no id (0.189.11).
uint32_t remove_every_folder() {
  Names sink;
  if (!vfs::list(kDoor, sink)) {
    log_printf(pal::kLogInfo, "MEMORY", "sessions destroy: there is nothing at %s", kDoor);
    return 0;
  }
  // **Les carpetes són les de les apps** (0.189.43): the door holds one folder per app and nothing else, so what is walked
  // is the folders and the files are skipped —the condition was the other way round, and `sessions destroy` over the whole
  // board removed nothing while saying it had— (docs/30, docs/70).
  uint32_t removed = 0;
  for (uint32_t i = 0; i < sink.count(); ++i) {
    if (!sink[i].is_dir) continue;
    removed += remove_a_folder(sink[i].name);
  }
  return removed;
}

// **Un sol fitxer, i la carpeta si s'ha quedat buida** (0.189.16): the disk half of `sessions destroy`, which is the same
// check the folder's is —the path is the door, the app and the key— and the folder goes when nothing is left in it,
// because an empty folder is litter a person sees in `ls` and in the next listing (docs/30, docs/70).
uint32_t remove_a_file(const char* app, const char* key) {
  char folder[96];
  char path[128];
  if (!folder_of(app, folder, sizeof(folder)) || !file_of(app, key, path, sizeof(path))) return 0;
  hal::IStorage* storage = nullptr;
  char rel[128];
  if (!vfs::resolve(path, storage, rel, sizeof(rel)) || storage == nullptr) return 0;
  hal::Stat info;
  if (!storage->stat(rel, info) || info.type != hal::NodeType::kFile) return 0;
  if (!storage->remove(rel)) return 0;
  log_printf(pal::kLogInfo, "MEMORY", "%s forgets '%s': %s removed", app, key, path);

  char folder_rel[128];
  hal::IStorage* folder_storage = nullptr;
  if (!vfs::resolve(folder, folder_storage, folder_rel, sizeof(folder_rel)) || folder_storage == nullptr) return 1;
  Names sink;
  if (vfs::list(folder, sink) && sink.count() == 0) (void)folder_storage->remove(folder_rel);
  return 1;
}

}  // namespace

void* acquire(const char* app, const char* key, size_t bytes, size_t& got) {
  const bool there = open_session(app, key, bytes, got);
  if (!there) return nullptr;
  Block* held = find(app, key);
  return (held != nullptr) ? held->bytes : nullptr;
}

bool open_session(const char* app, const char* key, size_t bytes, size_t& got) {
  // **L'amo d'un bloc de sessió és l'app que el demana**, que és el primer argument i no el que hi ha al vidre (docs/30, docs/75).
  pal::MemoryTag who_asks(pal::kMemorySession, app);
  got = 0;
  if (app == nullptr || key == nullptr || app[0] == '\0' || key[0] == '\0') return false;
  char wanted_app[kNameBytes];
  char wanted_key[kNameBytes];
  copy_name(wanted_app, app);
  copy_name(wanted_key, key);

  Block* held = find(wanted_app, wanted_key);
  if (held != nullptr) {
    Block& block = *held;
    // **A block that is there and is smaller than what is asked for grows**, and a resize that the memory refuses leaves
    // the block where it is: the app gets what it had and decides (docs/75).
    const size_t was = block.size;
    if (bytes > block.size) {
      if (block.banked != nullptr) {
        // **Una regió del banc no creix** (0.190.174): the chip gives one window and this block already has it, so the block
        // stays the size it is and the app decides with the bytes it got —the same answer the allocator gives when it
        // cannot move a block— (docs/75).
      } else {
        void* bigger = pal::extra_memory().resize(block.bytes, bytes, true);
        if (bigger != nullptr) {
          block.bytes = bigger;
          // **The bytes that were not there before are zero**, so a cache that grows does not read the heap's past.
          memset(static_cast<uint8_t*>(block.bytes) + block.size, 0, bytes - block.size);
          block.size = bytes;
        }
      }
    }
    // **And it says so** (0.179.3): an app that keeps memory for the session is the first thing that goes when the board
    // is short, and a log that does not say who asked for what is a log that cannot explain a page that came back empty.
    if (block.size > was) {
      log_printf(pal::kLogInfo, "MEMORY", "%s asks for '%s': %u B, grown from %u", wanted_app, wanted_key,
                 static_cast<unsigned>(block.size), static_cast<unsigned>(was));
    } else {
      log_printf(pal::kLogInfo, "MEMORY", "%s asks for '%s': %u B, and it has them", wanted_app, wanted_key,
                 static_cast<unsigned>(block.size));
    }
    got = block.size;
    return true;
  }

  // **A session is a node and there is no table to be full of** (0.189.36): the only answer that is not a block is the
  // memory's own. It is asked for the board's own RAM first —forty-eight bytes, and `kHomeReserveBytes` keeps the drivers
  // their room— and a board with no room says so once, with the number that was asked, and the app goes on without a cache
  // (docs/30, docs/75).
  Block* node = nullptr;
  {
    meter::Scope scope(meter::kExtraAlloc);
    node = static_cast<Block*>(pal::extra_memory().allocate(sizeof(Block), false));
  }
  if (node == nullptr) {
    log_printf(pal::kLogWarn, "MEMORY", "%s asks for '%s': %u B, and there is no room for the row of a session",
               wanted_app, wanted_key, static_cast<unsigned>(bytes));
    return false;
  }
  // **I EL QUE HI HAVIA AL DISC ÉS EL QUE L'APP VA DESAR-HI L'ÚLTIMA VEGADA** (0.189.7): the file of this session is
  // asked for its size before the block is made, so a cache that grew last run comes back whole and one that asks for
  // less than the file gets the file's size. The block is still zeroed first: what the file does not cover is paper and
  // not the heap's past.
  const size_t on_disk = disk_size(wanted_app, wanted_key);
  const size_t want = (on_disk > bytes) ? on_disk : bytes;
  // **Els blocs grans van a la memòria que no té punter** (0.190.174, l'amo: *«usa himem per les sessions»*): a cache of
  // `kBankFromBytes` or more is the memory the PSRAM should keep for the faces, the fonts and the pictures, and it is the
  // one shape the bank can hold —read and written by offset, which is what a session already was—. A board with no bank, or
  // a bank that is already taken, answers `nullptr` and the block goes to the PSRAM as it always did (docs/50, docs/75).
  hal::IBankedMemory* banked = nullptr;
  if (want >= kBankFromBytes) {
    banked = hal::platform().open_banked_memory(static_cast<uint32_t>(want));
    if (banked != nullptr) {
      log_printf(pal::kLogInfo, "MEMORY", "%s asks for '%s': %u B, and it is in the banked memory", wanted_app,
                 wanted_key, static_cast<unsigned>(want));
    }
  }
  void* made = nullptr;
  if (banked == nullptr) {
    meter::Scope scope(meter::kExtraAlloc);
    made = pal::extra_memory().allocate(want, true);
  }
  // **A block that does not fit is an app without a cache** and not a fault: the node goes back and the app is told by the
  // null it gets (docs/30).
  if (banked == nullptr && made == nullptr) {
    pal::extra_memory().release(node);
    log_printf(pal::kLogWarn, "MEMORY", "%s asks for '%s': %u B, and there is no room", wanted_app, wanted_key,
               static_cast<unsigned>(want));
    return false;
  }
  *node = Block();
  node->next = g_blocks;
  g_blocks = node;
  ++g_held;
  Block& block = *node;
  copy_name(block.app, wanted_app);
  copy_name(block.key, wanted_key);
  block.bytes = made;
  block.banked = banked;
  block.size = want;
  store_zero(block, 0);
  const size_t restored = (on_disk > 0) ? restore_block(wanted_app, wanted_key, block) : 0;
  // **El que el disc té d'aquest bloc, comptat ara** (0.189.53): the bytes just restored, or the zeros when there was no
  // file, are the state the disk is in —and the first park compares against them—.
  block.seen = store_hash(block);
  if (restored > 0) {
    log_printf(pal::kLogInfo, "MEMORY", "%s asks for '%s': %u B, restored %u B from the disk", wanted_app,
               wanted_key, static_cast<unsigned>(want), static_cast<unsigned>(restored));
  } else {
    log_printf(pal::kLogInfo, "MEMORY", "%s asks for '%s': %u B, made", wanted_app, wanted_key,
               static_cast<unsigned>(want));
  }
  got = want;
  return true;
}

bool release(const char* app, const char* key) {
  if (app == nullptr || key == nullptr) return false;
  Block* held = find(app, key);
  if (held == nullptr) return false;
  Block& block = *held;
  // **The app gives its own block back, and the log says how big it was**: the reason a block goes is part of what makes a
  // session memory explainable —this one is the app's choice, the one below is the allocator's— (docs/75).
  // **I ABANS DE DONAR-LO, EL QUE HI HA VA AL DISC** (0.189.7): a block that an app lets go is a cache the app asked for
  // and did not throw away, and the point of the card is that the next run of the app finds it (docs/30, docs/75).
  bool wrote = false;
  park_block(block, "the app gave it back", wrote);
  log_printf(pal::kLogInfo, "MEMORY", "%s gives back '%s': %u B", block.app, block.key,
             static_cast<unsigned>(block.size));
  store_free(block);
  free_node(held);
  ++g_generation;
  return true;
}

void release_all(void* keep, size_t asked) {
  // **A board with no app in front protects nobody**: every block is somebody else's and they all go (0.189.11).
  relieve("", keep, asked);
}

// **Qui va primer quan la memòria no arriba** (0.189.11): the caches of the apps that are **not** in front, and only then
// the one of the app on the glass. The reason a person expects is the one the owner asked for: a cache nobody is looking at
// costs nothing to park, and the app that is in front is the one that would have to build its page again. If parking the
// others is not enough for the ask, the app in front pays too —it will ask for its block again when it needs it and find it
// on the disk, which is exactly what a session file is for— (docs/30, docs/75).
//
// Every block is **written before it is given back**, so being short of memory never loses a cache; and the one the ask is
// about (`keep`) is never taken, because the allocator is moving it (0.179.6).
void relieve(const char* current, void* keep, size_t asked) {
  char in_front[kNameBytes] = {};
  if (current != nullptr) copy_name(in_front, current);

  uint32_t others = 0;
  size_t freed_others = 0;
  for (Block** link = &g_blocks; *link != nullptr;) {
    Block* block = *link;
    if (keep != nullptr && block->bytes == keep) {
      log_printf(pal::kLogInfo, "MEMORY", "%s keeps '%s': %u B, it is the block that asked", block->app, block->key,
                 static_cast<unsigned>(block->size));
      link = &block->next;
      continue;
    }
    if (in_front[0] != '\0' && strcmp(block->app, in_front) == 0) {  // the app in front is the second pass
      link = &block->next;
      continue;
    }
    bool wrote = false;
    park_block(*block, "the memory is short, another app's cache", wrote);
    log_printf(pal::kLogInfo, "MEMORY", "%s gives back '%s': %u B, the memory is short", block->app, block->key,
               static_cast<unsigned>(block->size));
    freed_others += block->size;
    store_free(*block);
    *link = block->next;
    pal::extra_memory().release(block);
    if (g_held > 0) --g_held;
    ++others;
  }
  if (others > 0) ++g_generation;

  // **I si encara no n'hi ha prou, l'app que és al vidre** (0.189.11). It goes last and it is said as what it is: the page
  // of the app that is in front is the one that will have to read its cache back, and that is a price the board pays only
  // when the others did not cover the ask.
  uint32_t mine = 0;
  size_t freed_mine = 0;
  if (freed_others < asked) {
    for (Block** link = &g_blocks; *link != nullptr;) {
      Block* block = *link;
      // **I el bloc que la petició està movent no es toca tampoc aquí** (0.189.43): the first pass protects it and the
      // second did not, so a memory that is short gave back the very block the allocator was moving —a pointer that is
      // about to be freed— (docs/30, docs/75).
      if (keep != nullptr && block->bytes == keep) {
        link = &block->next;
        continue;
      }
      if (in_front[0] == '\0' || strcmp(block->app, in_front) != 0) {
        link = &block->next;
        continue;
      }
      bool mine_wrote = false;
      park_block(*block, "the memory is short, the app in front's own cache", mine_wrote);
      log_printf(pal::kLogWarn, "MEMORY", "%s gives back '%s': %u B, its own cache, the memory is short", block->app,
                 block->key, static_cast<unsigned>(block->size));
      freed_mine += block->size;
      store_free(*block);
      *link = block->next;
      pal::extra_memory().release(block);
      if (g_held > 0) --g_held;
      ++mine;
    }
    if (mine > 0) ++g_generation;
  }

  // **And the line that says it was the allocator and not an app** (0.179.3): a warning because it is the moment a board
  // decides that a cache is worth less than the block that asked, with the two numbers of the two passes so a log answers
  // "why did this one go and not that one" — the first pass is the board being polite and the second is it being out of
  // room (docs/75).
  if (others > 0 || mine > 0) {
    log_printf(pal::kLogWarn, "MEMORY",
               "the memory is short: %u block(s) of other apps (%u B) and %u of the app in front (%u B) given back for "
               "an ask of %u B",
               static_cast<unsigned>(others), static_cast<unsigned>(freed_others), static_cast<unsigned>(mine),
               static_cast<unsigned>(freed_mine), static_cast<unsigned>(asked));
  }
}

uint32_t count() { return g_held; }

// **Les files de la llista, per a qui les vol veure** (0.189.7): the prompt asks who is holding what and the test asks the
// same, so what is printed and what is checked are the same numbers (docs/30, docs/75).
//
// **I les que només són un fitxer també hi són** (0.189.11): a session of an app that has not run since the board came up is
// a file of `/session` and nothing else, and a person asking `sessions` wants to see it —with its key, its size and **the
// path of the VFS**— because that is the cache that will come back the moment the app starts, and the one `sessions destroy`
// would throw away. The rows of the memory go first, so what is held right now is the first thing read (docs/30, docs/70).
//
// **I el llistat no té sostre** (0.189.36): the rows are handed over one at a time, so a board that holds thirty sessions
// says thirty. What the caller does with them is the caller's business: the prompt prints each row as it comes —its stack
// is eight kilobytes and a row is ninety-six bytes— and a test copies them into its own array, which is what `entries`
// below is (docs/30, docs/75).
uint32_t each(RowSink visit, void* user) {
  uint32_t seen = 0;

  for (const Block* block = g_blocks; block != nullptr; block = block->next) {
    Entry row;
    snprintf(row.app, sizeof(row.app), "%s", block->app);
    snprintf(row.key, sizeof(row.key), "%s", block->key);
    row.size = block->size;
    row.in_memory = true;
    (void)file_of(block->app, block->key, row.path, sizeof(row.path));
    row.on_disk = disk_size(block->app, block->key) == block->size;
    ++seen;
    if (visit != nullptr && !visit(row, user)) return seen;
  }

  Names folders;
  if (!vfs::list(kDoor, folders)) return seen;
  for (uint32_t f = 0; f < folders.count(); ++f) {
    if (!folders[f].is_dir) continue;
    char folder[96];
    snprintf(folder, sizeof(folder), "%s/%s", kDoor, folders[f].name);
    Names files;
    if (!vfs::list(folder, files)) continue;
    for (uint32_t k = 0; k < files.count(); ++k) {
      if (files[k].is_dir) continue;
      // **A file the memory is holding is not a second row**: it is the same session and it is already up there, with the
      // path of this very file beside it.
      if (find(folders[f].name, files[k].name) != nullptr) continue;
      Entry row;
      snprintf(row.app, sizeof(row.app), "%s", folders[f].name);
      snprintf(row.key, sizeof(row.key), "%s", files[k].name);
      row.size = files[k].size;
      row.in_memory = false;
      row.on_disk = true;
      // **The path is the door, the app and the key**: the three of them are what a person types to look at it, and
      // `/session/<app>/<key>` is 9 + 15 + 1 + 15 bytes at the most, which is what the row has (docs/70).
      snprintf(row.path, sizeof(row.path), "%s/%s/%s", kDoor, folders[f].name, files[k].name);
      ++seen;
      if (visit != nullptr && !visit(row, user)) return seen;
    }
  }
  return seen;
}

namespace {

// The visitor of `entries`: a caller that wants the rows in an array of its own instead of one at a time (0.189.36). It
// never says stop, so `each` still walks the whole list —a row that did not fit the caller's room is a row the caller chose
// not to have, not a row the board kept back— and the answer of `entries` is how many were written.
struct Shelf {
  Entry* out = nullptr;
  uint32_t cap = 0;
  uint32_t shown = 0;
};

bool put_row(const Entry& row, void* user) {
  Shelf* shelf = static_cast<Shelf*>(user);
  if (shelf->shown < shelf->cap) {
    shelf->out[shelf->shown] = row;
    ++shelf->shown;
  }
  return true;
}

}  // namespace

uint32_t entries(Entry* out, uint32_t cap) {
  if (out == nullptr || cap == 0) return 0;
  Shelf shelf;
  shelf.out = out;
  shelf.cap = cap;
  (void)each(put_row, &shelf);
  return shelf.shown;
}

// **Els bytes d'un bloc, per a qui no n'és l'amo** (0.189.14): the memory first —an app writes into the block and the file
// only gets the copy that was flushed, so the block is the newest of the two— and the file after. A reader that is not the
// owner never gets a pointer into the block: it gets a copy, because the relief of the allocator can take the block in the
// same turn and a pointer into memory that has gone back is the fault this module was built to avoid (docs/30, docs/75).
size_t copy(const char* app, const char* key, char* out, size_t cap) {
  if (app == nullptr || key == nullptr || out == nullptr || cap == 0) return 0;
  const Block* held = find(app, key);
  if (held != nullptr) return store_read(*held, 0, out, static_cast<uint32_t>(cap));
  return read_block(app, key, out, cap);
}

// **Els bytes d'un bloc, per al qui n'és l'amo i per a l'app que ho és** (0.190.174): the two doors of a block that is read
// and written with an offset. They are what the interpreter calls now —`block:read(at, n)`, `block:write(at, text)`,
// `block:clear()`— because a block can live where no pointer reaches, and they are also the honest shape of a memory that
// only maps a window at a time (docs/30, docs/50, docs/75).
uint32_t read_at(const char* app, const char* key, uint32_t offset, void* out, uint32_t cap) {
  if (app == nullptr || key == nullptr || out == nullptr || cap == 0) return 0;
  const Block* held = find(app, key);
  if (held == nullptr) return 0;
  return store_read(*held, offset, out, cap);
}

uint32_t write_at(const char* app, const char* key, uint32_t offset, const void* in, uint32_t bytes) {
  if (app == nullptr || key == nullptr || in == nullptr || bytes == 0) return 0;
  Block* held = find(app, key);
  if (held == nullptr) return 0;
  return store_write(*held, offset, in, bytes);
}

bool clear(const char* app, const char* key) {
  if (app == nullptr || key == nullptr) return false;
  Block* held = find(app, key);
  if (held == nullptr) return false;
  store_zero(*held, 0);
  return true;
}

// **Quants blocs viuen a la memòria del banc** (0.190.174): the prompt says it in the `sessions` summary, because where a
// cache is answers a different question than how big it is —the PSRAM and the bank are two rooms and a person looking at a
// board that is short of memory needs to know which one is full— (docs/30, docs/75).
uint32_t banked() {
  uint32_t held = 0;
  for (const Block* block = g_blocks; block != nullptr; block = block->next) {
    if (block->banked != nullptr) ++held;
  }
  return held;
}

// **Les cachés d'una app s'escriuen al disc quan la seva app s'acaba** (0.189.7): one app at a time is on this board, and
// the cache that app built is the one a person expects to find when they open it again —after a restart too—. The block
// **stays in the memory**: giving it back is the allocator's business and it does it when it is short, writing it first
// (docs/30, docs/75).
Parked flush(const char* app) {
  Parked done;
  if (app == nullptr || app[0] == '\0') return done;
  char wanted[kNameBytes];
  copy_name(wanted, app);
  for (Block* block = g_blocks; block != nullptr; block = block->next) {
    if (strcmp(block->app, wanted) != 0) continue;
    bool wrote = false;
    if (!park_block(*block, "the app has ended", wrote)) continue;
    if (wrote) {
      ++done.written;
    } else {
      ++done.unchanged;
    }
  }
  return done;
}

// **Abans d'apagar o de reiniciar, tot el que hi ha a la memòria va al disc** (0.189.11): a board that is switched off
// loses the PSRAM and keeps the card, so this is the last thing the system does with a session. A block stays where it is
// —the run is over— and the log says one line per block with **who, under which key, how many bytes and the path of the
// VFS**, which is what a person needs to know that the cache of their launcher will be there tomorrow (docs/30, docs/75).
Parked park_all(const char* why) {
  const char* reason = (why != nullptr && why[0] != '\0') ? why : "the board is going down";
  // **Escriure a la targeta és esperar i no calcular** (0.190.26): a block is a kilobyte that the card takes in its own time, and
  // the processor has nothing to do with it while it happens (docs/50, docs/60, docs/70).
  power::wait_cpu("the card");
  Parked done;
  uint32_t held = 0;
  uint32_t failed = 0;
  size_t bytes = 0;
  for (Block* block = g_blocks; block != nullptr; block = block->next) {
    ++held;
    bytes += block->size;
    bool wrote = false;
    if (!park_block(*block, reason, wrote)) {
      ++failed;
    } else if (wrote) {
      ++done.written;
    } else {
      ++done.unchanged;
    }
  }
  if (held > 0) {
    // **I la línia diu les dues coses** (0.189.53): «2 of 3» was a lie that read as a failure —the blocks whose bytes the disk
    // already had are not written and are not lost, and a person who reads the log of a poweroff has to be able to tell one
    // from the other (docs/30, docs/75).
    log_printf((failed == 0) ? pal::kLogInfo : pal::kLogWarn, "MEMORY",
               "sessions parked (%s): %u written, %u already there, %u B in %u block(s), at %s", reason,
               static_cast<unsigned>(done.written), static_cast<unsigned>(done.unchanged),
               static_cast<unsigned>(bytes), static_cast<unsigned>(held), kDoor);
  } else {
    log_printf(pal::kLogInfo, "MEMORY", "sessions parked (%s): the memory held none", reason);
  }
  return done;
}

// **El que s'ha escrit, comptat** (0.189.18): `park_all` says how many writes it made and this asks the disk whether they
// are there —the size of the file against the size of the block—, because the doubt that opened this door was exactly that
// writing and having written are not the same thing on a card that is about to lose its supply (docs/75).
Checked check_parked() {
  Checked done;
  for (const Block* block = g_blocks; block != nullptr; block = block->next) {
    ++done.files;
    if (disk_size(block->app, block->key) == block->size) ++done.ok;
  }
  return done;
}

// **`sessions destroy [<id>]`** (0.189.11, i 0.189.17 amb les tres formes): la memòria **i** el disc, perquè un destroy que
// només buida la memòria és un destroy que torna a la següent arrencada de l'app —el fitxer és el que l'app llegeix
// primer— (docs/30, docs/75).
Forgotten forget(const char* app) {
  Forgotten gone;
  char wanted[kNameBytes] = {};
  const bool all = (app == nullptr || app[0] == '\0');
  if (!all) copy_name(wanted, app);

  for (Block** link = &g_blocks; *link != nullptr;) {
    Block* block = *link;
    if (!all && strcmp(block->app, wanted) != 0) {
      link = &block->next;
      continue;
    }
    log_printf(pal::kLogInfo, "MEMORY", "%s forgets '%s': %u B, cleared from the memory", block->app, block->key,
               static_cast<unsigned>(block->size));
    gone.bytes += block->size;
    ++gone.blocks;
    store_free(*block);
    *link = block->next;
    pal::extra_memory().release(block);
    if (g_held > 0) --g_held;
  }
  if (gone.blocks > 0) ++g_generation;

  gone.files = all ? remove_every_folder() : remove_a_folder(wanted);
  log_printf(pal::kLogInfo, "MEMORY", "sessions destroy %s: %u block(s) and %u file(s), %u B", all ? "(every app)" : wanted,
             static_cast<unsigned>(gone.blocks), static_cast<unsigned>(gone.files), static_cast<unsigned>(gone.bytes));
  return gone;
}

// **Una sola sessió** (0.189.16): the block of that key for that app and the file of it, and the other sessions of the app
// are not touched. Three answers are what a person reads: how many blocks of the memory went, how many files, and how many
// bytes were held —and zero of everything is a session that was not there, which is not a fault (docs/30, docs/75).
Forgotten forget(const char* app, const char* key) {
  Forgotten gone;
  if (app == nullptr || key == nullptr || app[0] == '\0' || key[0] == '\0') return gone;
  char wanted_app[kNameBytes];
  char wanted_key[kNameBytes];
  copy_name(wanted_app, app);
  copy_name(wanted_key, key);

  Block* held = find(wanted_app, wanted_key);
  if (held != nullptr) {
    gone.bytes = held->size;
    gone.blocks = 1;
    log_printf(pal::kLogInfo, "MEMORY", "%s forgets '%s': %u B, cleared from the memory", wanted_app, wanted_key,
               static_cast<unsigned>(held->size));
    store_free(*held);
    free_node(held);
    ++g_generation;
  }
  gone.files = remove_a_file(wanted_app, wanted_key);
  log_printf(pal::kLogInfo, "MEMORY", "sessions destroy %s '%s': %u block(s) and %u file(s), %u B", wanted_app,
             wanted_key, static_cast<unsigned>(gone.blocks), static_cast<unsigned>(gone.files),
             static_cast<unsigned>(gone.bytes));
  return gone;
}

// **Una clau, a totes les apps** (0.189.16): `sessions destroy <clau>` for the word a person reads in the listing when no app
// is called like that. It is the same pair of halves —the memory and the files— and it names the key so the log cannot be
// confused with the destroy of an app (docs/30).
Forgotten forget_key(const char* key, bool say) {
  Forgotten gone;
  if (key == nullptr || key[0] == '\0') return gone;
  char wanted[kNameBytes];
  copy_name(wanted, key);

  for (Block** link = &g_blocks; *link != nullptr;) {
    Block* block = *link;
    if (strcmp(block->key, wanted) != 0) {
      link = &block->next;
      continue;
    }
    log_printf(pal::kLogInfo, "MEMORY", "%s forgets '%s': %u B, cleared from the memory", block->app, block->key,
               static_cast<unsigned>(block->size));
    gone.bytes += block->size;
    ++gone.blocks;
    store_free(*block);
    *link = block->next;
    pal::extra_memory().release(block);
    if (g_held > 0) --g_held;
  }
  if (gone.blocks > 0) ++g_generation;

  // **I el mateix nom a cada carpeta del disc**, because a key without an app is every app that keeps one under that name.
  Names sink;
  if (vfs::list(kDoor, sink)) {
    for (uint32_t i = 0; i < sink.count(); ++i) {
      if (!sink[i].is_dir) continue;
      gone.files += remove_a_file(sink[i].name, wanted);
    }
  }
  if (say) {
    log_printf(pal::kLogInfo, "MEMORY",
               "sessions destroy '%s' (the session, of every app): %u block(s) and %u file(s), %u B", wanted,
               static_cast<unsigned>(gone.blocks), static_cast<unsigned>(gone.files),
               static_cast<unsigned>(gone.bytes));
  }
  return gone;
}

uint32_t generation() { return g_generation; }

size_t bytes_held() {
  size_t bytes = 0;
  for (const Block* block = g_blocks; block != nullptr; block = block->next) bytes += block->size;
  return bytes;
}

}  // namespace session
}  // namespace kernel
