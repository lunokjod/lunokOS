// The memory of a session (docs/30, docs/75): one block per (app, key), made once and kept while the board is on.
#include "kernel/session.h"

#include <stdio.h>
#include <string.h>

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
    if (bytes > block.size) {
      void* bigger = pal::extra_memory().resize(block.bytes, bytes, true);
      if (bigger != nullptr) {
        block.bytes = bigger;
        // **The bytes that were not there before are zero**, so a cache that grows does not read the heap's past.
        memset(static_cast<uint8_t*>(block.bytes) + block.size, 0, bytes - block.size);
        block.size = bytes;
      }
    }
    got = block.size;
    return block.bytes;
  }

  const int16_t slot = free_slot();
  if (slot < 0) return nullptr;
  void* made = nullptr;
  {
    meter::Scope scope(meter::kExtraAlloc);
    made = pal::extra_memory().allocate(bytes, true);
  }
  // **A block that does not fit is an app without a cache** and not a fault: the table is not touched, and the app is
  // told by the null it gets (docs/30).
  if (made == nullptr) return nullptr;
  memset(made, 0, bytes);
  Block& block = g_blocks[slot];
  copy_name(block.app, wanted_app);
  copy_name(block.key, wanted_key);
  block.bytes = made;
  block.size = bytes;
  block.used = true;
  got = bytes;
  return made;
}

bool release(const char* app, const char* key) {
  if (app == nullptr || key == nullptr) return false;
  const int16_t held = find(app, key);
  if (held < 0) return false;
  Block& block = g_blocks[held];
  pal::extra_memory().release(block.bytes);
  block = Block();
  ++g_generation;
  return true;
}

void release_all() {
  bool any = false;
  for (uint8_t i = 0; i < kMaxBlocks; ++i) {
    if (!g_blocks[i].used) continue;
    pal::extra_memory().release(g_blocks[i].bytes);
    g_blocks[i] = Block();
    any = true;
  }
  // **The objects that pointed at those blocks are stale from now on**, which is what the generation says to the bridge.
  if (any) ++g_generation;
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
