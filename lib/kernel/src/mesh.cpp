// **El mesh del nucli** (0.190.212): three questions to the board and nothing of its own. The whole of what this system knows about
// Bluetooth Mesh is here, and it is a door and not a copy of the stack: el dia que hi hagi models, el que creixi serà el HAL i les
// ordres, no aquest fitxer (docs/20, docs/50).
#include "kernel/mesh.h"

#include "hal/hal.h"
#include "kernel/cfg.h"
#include "kernel/log.h"

#include <cstdio>
#include <cstring>

namespace kernel {
namespace mesh {
namespace {

hal::IMesh* the_node() { return hal::platform().mesh(); }

// Les dues claus, a la botiga en hexadecimal: el registre les diu llegibles i una persona les pot copiar (docs/70). Que la botiga no les
// prengui no desfà el que la pila ja ha pres —la xarxa funciona igual—, o sigui que es diu i es continua (docs/50).
void keep_the_keys(const uint8_t net_key[16], const uint8_t app_key[16]) {
  char net[33];
  char app[33];
  key_to_text(net_key, net);
  key_to_text(app_key, app);
  if (!cfg::set_mesh_keys(net, app)) {
    log_printf(pal::kLogWarn, "MESH", "the keys are in, and this board could not keep them for the next start");
  }
}

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
  if (!node->start(nullptr)) return false;
  // **I la xarxa que aquesta placa tenia torna amb ella** (0.190.218): el que es va desar amb `mesh key` es torna a posar aquí, perquè una
  // xarxa que s'ha de tornar a fer a cada arrencada no és una xarxa. Si no hi ha claus desades, la pila queda enlaire i el prompt dirà que el
  // primer és `mesh key` (docs/30, docs/50, docs/70).
  if (node->can_keep_the_keys() && !node->has_the_network()) {
    uint8_t net[16] = {0};
    uint8_t app[16] = {0};
    if (key_from_text(cfg::mesh_net_key(), net) && key_from_text(cfg::mesh_app_key(), app)) {
      if (!node->own_the_network(net, app)) {
        log_printf(pal::kLogWarn, "MESH", "the keys this board had were not taken by the stack");
      }
    }
  }
  return true;
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

bool can_keep_the_keys() {
  hal::IMesh* node = the_node();
  return node != nullptr && node->can_keep_the_keys();
}

bool has_the_network() {
  hal::IMesh* node = the_node();
  return node != nullptr && node->has_the_network();
}

// **Fer-les o posar-les desa la parella** (0.190.218): el que el HAL ha pres és el que es queda a la botiga, o sigui que qui les ha vistes al
// prompt les torna a tenir a la propera arrencada (docs/50, docs/70).
bool make_keys(uint8_t net_key[16], uint8_t app_key[16]) {
  hal::IMesh* node = the_node();
  if (node == nullptr || !node->make_keys(net_key, app_key)) return false;
  keep_the_keys(net_key, app_key);
  return true;
}

bool own_the_network(const uint8_t net_key[16], const uint8_t app_key[16]) {
  hal::IMesh* node = the_node();
  if (node == nullptr || !node->own_the_network(net_key, app_key)) return false;
  keep_the_keys(net_key, app_key);
  return true;
}

void key_to_text(const uint8_t key[16], char text[33]) {
  const char digits[] = "0123456789abcdef";
  for (int i = 0; i < 16; ++i) {
    text[i * 2] = digits[(key[i] >> 4) & 0xF];
    text[i * 2 + 1] = digits[key[i] & 0xF];
  }
  text[32] = '\0';
}

bool key_from_text(const char* text, uint8_t key[16]) {
  if (text == nullptr || strlen(text) != 32) return false;
  for (int i = 0; i < 16; ++i) {
    uint8_t value = 0;
    for (int half = 0; half < 2; ++half) {
      const char c = text[i * 2 + half];
      uint8_t digit = 0;
      if (c >= '0' && c <= '9') {
        digit = static_cast<uint8_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        digit = static_cast<uint8_t>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        digit = static_cast<uint8_t>(c - 'A' + 10);
      } else {
        return false;
      }
      value = static_cast<uint8_t>((value << 4) | digit);
    }
    key[i] = value;
  }
  return true;
}

bool network_keys(uint8_t net_key[16], uint8_t app_key[16]) {
  hal::IMesh* node = the_node();
  if (node != nullptr && node->network_keys(net_key, app_key)) return true;
  // **Una clau desada és una clau que aquesta placa té** (0.190.218), encara que la pila encara no s'hagi enlairat: és el que es va desar amb
  // `mesh key`, o sigui que `mesh key show` el pot dir abans de `mesh on` (docs/30, docs/50).
  return key_from_text(cfg::mesh_net_key(), net_key) && key_from_text(cfg::mesh_app_key(), app_key);
}

int watch(uint32_t ms) {
  hal::IMesh* node = the_node();
  return node != nullptr ? node->watch(ms) : 0;
}

int heard_count() {
  hal::IMesh* node = the_node();
  return node != nullptr ? node->heard_count() : 0;
}

bool heard_at(int index, uint8_t uuid[16], int* rssi) {
  hal::IMesh* node = the_node();
  return node != nullptr && node->heard_at(index, uuid, rssi);
}

bool provision(int index, uint16_t address, uint16_t* given) {
  hal::IMesh* node = the_node();
  return node != nullptr && node->provision(index, address, given);
}

int known(uint16_t* addresses, int cap) {
  hal::IMesh* node = the_node();
  return node != nullptr ? node->known(addresses, cap) : 0;
}

}  // namespace mesh
}  // namespace kernel
