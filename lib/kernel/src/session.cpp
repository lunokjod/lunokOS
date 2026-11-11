// The memory of a session (docs/30, docs/75): one block per (app, key), made once and kept while the board is on.
#include "kernel/session.h"

#include <stdio.h>
#include <string.h>

#include "kernel/log.h"
#include "kernel/meter.h"
#include "pal/pal.h"

namespace kernel {
namespace session {
namespace {

struct Block {
  char app[kNameBytes];
  char key[kNameBytes];
  void* bytes = nullptr;
  size_t size = 0;
  bool used = false;
};

Block g_blocks[kMaxBlocks];
uint32_t g_generation = 1;

// **A name is copied and cut**: the app is the id of a folder and the key is a word of the app's, and neither is a
// pointer into an interpreter that dies (docs/30).
void copy_name(char* out, const char* text) {
  size_t at = 0;
  if (text != nullptr) {
    for (; text[at] != '\0' && at + 1 < kNameBytes; ++at) out[at] = text[at];
  }
  out[at] = '\0';
}

int16_t find(const char* app, const char* key) {
  for (uint8_t i = 0; i < kMaxBlocks; ++i) {
    if (!g_blocks[i].used) continue;
    if (strcmp(g_blocks[i].app, app) == 0 && strcmp(g_blocks[i].key, key) == 0) return static_cast<int16_t>(i);
  }
  return -1;
}

int16_t free_slot() {
  for (uint8_t i = 0; i < kMaxBlocks; ++i) {
    if (!g_blocks[i].used) return static_cast<int16_t>(i);
  }
  return -1;
}

}  // namespace

void* acquire(const char* app, const char* key, size_t bytes, size_t& got) {
  got = 0;
  if (app == nullptr || key == nullptr || app[0] == '\0' || key[0] == '\0') return nullptr;
  char wanted_app[kNameBytes];
  char wanted_key[kNameBytes];
  copy_name(wanted_app, app);
  copy_name(wanted_key, key);

  const int16_t held = find(wanted_app, wanted_key);
  if (held >= 0) {
    Block& block = g_blocks[held];
    // **A block that is there and is smaller than what is asked for grows**, and a resize that the memory refuses leaves
    // the block where it is: the app gets what it had and decides (docs/75).
    const size_t was = block.size;
    if (bytes > block.size) {
      void* bigger = pal::extra_memory().resize(block.bytes, bytes, true);
      if (bigger != nullptr) {
        block.bytes = bigger;
        // **The bytes that were not there before are zero**, so a cache that grows does not read the heap's past.
        memset(static_cast<uint8_t*>(block.bytes) + block.size, 0, bytes - block.size);
        block.size = bytes;
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
    return block.bytes;
  }

  const int16_t slot = free_slot();
  if (slot < 0) {
    // **A table that has no free row is not a memory that is full**: the app is told the same null and the log says which
    // of the two it was (docs/75).
    log_printf(pal::kLogWarn, "MEMORY", "%s asks for '%s': %u B, and the table of %u is full", wanted_app, wanted_key,
               static_cast<unsigned>(bytes), static_cast<unsigned>(kMaxBlocks));
    return nullptr;
  }
  void* made = nullptr;
  {
    meter::Scope scope(meter::kExtraAlloc);
    made = pal::extra_memory().allocate(bytes, true);
  }
  // **A block that does not fit is an app without a cache** and not a fault: the table is not touched, and the app is
  // told by the null it gets (docs/30).
  if (made == nullptr) {
    log_printf(pal::kLogWarn, "MEMORY", "%s asks for '%s': %u B, and there is no room", wanted_app, wanted_key,
               static_cast<unsigned>(bytes));
    return nullptr;
  }
  memset(made, 0, bytes);
  Block& block = g_blocks[slot];
  copy_name(block.app, wanted_app);
  copy_name(block.key, wanted_key);
  block.bytes = made;
  block.size = bytes;
  block.used = true;
  log_printf(pal::kLogInfo, "MEMORY", "%s asks for '%s': %u B, made", wanted_app, wanted_key,
             static_cast<unsigned>(bytes));
  got = bytes;
  return made;
}

bool release(const char* app, const char* key) {
  if (app == nullptr || key == nullptr) return false;
  const int16_t held = find(app, key);
  if (held < 0) return false;
  Block& block = g_blocks[held];
  // **The app gives its own block back, and the log says how big it was**: the reason a block goes is part of what makes a
  // session memory explainable —this one is the app's choice, the one below is the allocator's— (docs/75).
  log_printf(pal::kLogInfo, "MEMORY", "%s gives back '%s': %u B", block.app, block.key,
             static_cast<unsigned>(block.size));
  pal::extra_memory().release(block.bytes);
  block = Block();
  ++g_generation;
  return true;
}

void release_all(void* keep, size_t asked) {
  bool any = false;
  size_t given = 0;
  uint8_t blocks = 0;
  for (uint8_t i = 0; i < kMaxBlocks; ++i) {
    if (!g_blocks[i].used) continue;
    // **The block the failed ask is about stays** (0.179.6): it is the one an app is growing, and the allocator is going to
    // move it right after this —giving it back here would be a block moved after it was freed—. It is not a cache that is
    // worth less than the ask: it is the ask (docs/75).
    if (keep != nullptr && g_blocks[i].bytes == keep) {
      log_printf(pal::kLogInfo, "MEMORY", "%s keeps '%s': %u B, it is the block that asked", g_blocks[i].app,
                 g_blocks[i].key, static_cast<unsigned>(g_blocks[i].size));
      continue;
    }
    // **Each one says who it was**, because the block that goes is the app's and a person reading the log wants to know
    // which page it was (docs/75).
    log_printf(pal::kLogInfo, "MEMORY", "%s gives back '%s': %u B, the memory is short", g_blocks[i].app,
               g_blocks[i].key, static_cast<unsigned>(g_blocks[i].size));
    given += g_blocks[i].size;
    pal::extra_memory().release(g_blocks[i].bytes);
    g_blocks[i] = Block();
    any = true;
    ++blocks;
  }
  // **The objects that pointed at those blocks are stale from now on**, which is what the generation says to the bridge.
  if (any) ++g_generation;
  // **And the line that says it was the allocator and not an app** (0.179.3): it is a warning because it is the moment a
  // board decides that a cache is worth less than the block that asked, and it is one line per relief and not one per
  // block when there was nothing to give (docs/75).
  if (any) {
    log_printf(pal::kLogWarn, "MEMORY", "the memory is short: %u block(s), %u B given back for an ask of %u B",
               static_cast<unsigned>(blocks), static_cast<unsigned>(given), static_cast<unsigned>(asked));
  }
}

uint8_t count() {
  uint8_t held = 0;
  for (uint8_t i = 0; i < kMaxBlocks; ++i) {
    if (g_blocks[i].used) ++held;
  }
  return held;
}

uint32_t generation() { return g_generation; }

size_t bytes_held() {
  size_t bytes = 0;
  for (uint8_t i = 0; i < kMaxBlocks; ++i) {
    if (g_blocks[i].used) bytes += g_blocks[i].size;
  }
  return bytes;
}

}  // namespace session
}  // namespace kernel
