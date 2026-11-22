// **El nom d'aquesta placa, una vegada per a tothom** (0.190.215): the string the Bluetooth keyboard already used, here so that the mesh
// node, the mDNS name and whatever comes next do not invent a second one (docs/50).
#include "esp32/board_name.h"

#include <cstdio>
#include <cstring>

#include "esp_mac.h"

namespace hal {

void the_board_name(char* out, size_t cap) {
  if (out == nullptr || cap == 0) return;
  out[0] = '\0';
  // **L'adreça ve de l'eFuse i no de la pila** (0.190.197): `esp_read_mac` es pot preguntar abans que res no s'hagi sincronitzat —que és
  // exactament quan es necessita, perquè el nom ha d'estar posat abans de l'anunci—, i és la del Bluetooth perquè és la que el nom fa
  // servir des del primer dia (docs/50).
  uint8_t addr[6] = {};
  if (esp_read_mac(addr, ESP_MAC_BT) != ESP_OK) {
    snprintf(out, cap, "lunokOS");
    return;
  }
  char long_name[32];
  snprintf(long_name, sizeof long_name, "lunokOS-%02X%02X", addr[4], addr[5]);
  // **La resposta de l'escaneig d'un teclat són trenta-un bytes i el nom hi va amb dos de capçalera**: o sigui que vint-i-nou és el que hi
  // cap de debò, i el curt és el que hi entra quan el lloc és aquest (docs/50).
  const size_t kMostInAScanResponse = 29;
  if (strlen(long_name) <= kMostInAScanResponse && strlen(long_name) < cap) {
    snprintf(out, cap, "%s", long_name);
    return;
  }
  char short_name[16];
  snprintf(short_name, sizeof short_name, "lunok%02X%02X", addr[4], addr[5]);
  if (strlen(short_name) < cap) {
    snprintf(out, cap, "%s", short_name);
    return;
  }
  out[0] = '\0';
}

}  // namespace hal
