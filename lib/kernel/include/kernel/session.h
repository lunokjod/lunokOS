// **The memory of a session, kept by the kernel and named by the app** (0.179.0): a cache an app builds once and finds
// again, which is what makes the second page of a launcher cheaper than the first (docs/30, docs/75).
//
// The block lives until the board is switched off, restarted, or the memory runs short: it is **the first thing the
// allocator gives up when a block does not fit** (`pal::set_memory_relief`), because it is the only memory of the system
// that can be made again. The keys are **of the app and not of the system**: `"apps"` is one block for `home_eink` and
// another for the app that comes after it.
#ifndef LUNOK_KERNEL_SESSION_H
#define LUNOK_KERNEL_SESSION_H

#include <stddef.h>
#include <stdint.h>

namespace kernel {
namespace session {

// **How many blocks the table holds**: an app is expected to keep one or two —a cache of what it read and a picture—, and
// eight leaves room for the launcher and a handful of apps without a table that grows (docs/75).
const uint8_t kMaxBlocks = 8;
const size_t kNameBytes = 16;

// **The block of `key` for `app`, which is made the first time and found after that**: `bytes` is what is asked for and
// `got` is what it really has —the block that was already there, or what the allocator gave—. A block that is made is
// **zeroed**, so an app can tell "nothing was cached" from "this is my cache" with a word of its own. nullptr is "there
// is no room", which is not an error of the app: it goes on without a cache (docs/30).
void* acquire(const char* app, const char* key, size_t bytes, size_t& got);

// **The block is given back** by the app that owns it, for the case of an app that wants the memory back before it ends.
// The system does not call this when an app stops: the whole point of a session memory is that the next run finds it.
bool release(const char* app, const char* key);

// **Every block goes**, which is what the allocator asks when a block does not fit. It is one switch on the relief of the
// PAL and it is also a door of the prompt (`mem` says how many are held).
void release_all();

// What the table holds, for `mem` and for a test.
uint8_t count();
size_t bytes_held();

// **A number that only goes up when a block goes back** (0.179.0): an object of an app that holds a block tells with it
// whether the block is still the one it was given, or the relief of the allocator has taken it (docs/75, docs/90).
uint32_t generation();

}  // namespace session
}  // namespace kernel

#endif  // LUNOK_KERNEL_SESSION_H
