#include "kernel/orders.h"

#include <cstdio>
#include <cstring>

#include "pal/pal.h"

namespace kernel::orders {

namespace {

// **La cua, la seva protecció i res més** (0.190.245): a ring of lines and the lock that makes it safe to touch from two tasks. The lock is made
// once, because a lock made per line would be a lock nobody shares (docs/50, docs/85).
struct Queue {
  // **Si la cua existeix és una pregunta i el pany n'és una altra**: on the PC the lock is the null one and this flag is what says that the
  // queue was made anyway, o sigui que les comandes es poden provar a la suite i no només a la placa (docs/85).
  bool made = false;
  pal::Lock lock = nullptr;
  char lines[kWaiting][kLineBytes];
  Who who[kWaiting];
  size_t head = 0;   // where the oldest waiting line is
  size_t count = 0;  // how many are in it
};

Queue g_queue;

}  // namespace

void make_the_queue() {
  if (g_queue.made) return;
  g_queue.lock = pal::make_lock();
  g_queue.made = true;
  g_queue.head = 0;
  g_queue.count = 0;
}

bool post_a_line(const char* text, Who who) {
  pal::take_lock(g_queue.lock);
  bool posted = false;
  if (g_queue.made && g_queue.count < kWaiting) {
    const size_t at = (g_queue.head + g_queue.count) % kWaiting;
    snprintf(g_queue.lines[at], kLineBytes, "%s", (text == nullptr) ? "" : text);
    g_queue.who[at] = who;
    ++g_queue.count;
    posted = true;
  }
  pal::give_lock(g_queue.lock);
  return posted;
}

bool take_a_line(char* out, size_t capacity, Who& who) {
  if (out == nullptr || capacity == 0) return false;
  pal::take_lock(g_queue.lock);
  bool taken = false;
  if (g_queue.made && g_queue.count > 0) {
    snprintf(out, capacity, "%s", g_queue.lines[g_queue.head]);
    who = g_queue.who[g_queue.head];
    g_queue.head = (g_queue.head + 1) % kWaiting;
    --g_queue.count;
    taken = true;
  }
  pal::give_lock(g_queue.lock);
  return taken;
}

size_t waiting() {
  pal::take_lock(g_queue.lock);
  const size_t count = g_queue.made ? g_queue.count : 0;
  pal::give_lock(g_queue.lock);
  return count;
}

}  // namespace kernel::orders
