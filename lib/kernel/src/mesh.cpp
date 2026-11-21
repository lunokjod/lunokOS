// **El mesh del nucli** (0.190.212): three questions to the board and nothing of its own. The whole of what this system knows about
// Bluetooth Mesh is here, and it is a door and not a copy of the stack: el dia que hi hagi models, el que creixi serà el HAL i les
// ordres, no aquest fitxer (docs/20, docs/50).
#include "kernel/mesh.h"

#include "hal/hal.h"
#include "kernel/log.h"

namespace kernel {
namespace mesh {
namespace {

hal::IMesh* the_node() { return hal::platform().mesh(); }

}  // namespace

bool available() {
  hal::IMesh* node = the_node();
  return node != nullptr && node->can_mesh();
}

bool is_up() {
  hal::IMesh* node = the_node();
  return node != nullptr && node->is_up();
}

bool start() {
  hal::IMesh* node = the_node();
  if (node == nullptr || !node->can_mesh()) return false;
  return node->start(nullptr);
}

void stop() {
  hal::IMesh* node = the_node();
  if (node != nullptr) node->stop();
}

bool provisioned() {
  hal::IMesh* node = the_node();
  return node != nullptr && node->provisioned();
}

uint16_t address() {
  hal::IMesh* node = the_node();
  return node != nullptr ? node->address() : 0;
}

}  // namespace mesh
}  // namespace kernel
