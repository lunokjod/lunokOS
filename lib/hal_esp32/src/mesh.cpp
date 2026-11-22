// **El node de Bluetooth Mesh, la part de C++** (0.190.212): the stack of Espressif lives in `mesh_node.c` —its macros are C and do not
// compile as C++—, i el que hi ha aquí és el que la resta del sistema veu: `IMesh`. La ràdio la porta `esp32_radio_for`, perquè la pila de
// NimBLE és una i el mesh no és el teclat (docs/45, docs/50).
#include "esp32/mesh.h"

#include "sdkconfig.h"

#include "esp32/bluetooth.h"
#include "esp32/board_name.h"
#include "esp32/mesh_node.h"
#include "hal/log.h"

#include <cstdio>
#include <cstring>

#include "esp_mac.h"

namespace hal {
namespace {

class Esp32Mesh : public IMesh {
 public:
  // **`CONFIG_BLE_MESH` no existeix quan val zero** (0.190.213): `sdkconfig.h` només defineix els símbols que estan encesos, o sigui que
  // preguntar-ho en una expressió és un error de compilació a tota banda que no porta el mesh —i el `#if` del fitxer del costat no ho veu,
  // perquè allà un símbol que no hi és val zero i prou— (docs/45).
#if defined(CONFIG_BLE_MESH)
  bool can_mesh() const override { return true; }
#else
  bool can_mesh() const override { return false; }
#endif

  bool start(const char* name) override {
    (void)name;
    if (!can_mesh()) return false;
    if (is_up()) return true;
    // **Dos anunciadors no hi caben**: the mesh sends its own beacons and the keyboard sends its own advertisement, o sigui que el que es
    // demana aquí és que la ràdio estigui lliure, i si no ho està es diu en lloc de trencar la connexió de qui hi hagi (docs/50).
    IBluetooth* radio = esp32_bluetooth();
    if (radio != nullptr && radio->wants_the_chip_awake()) {
      hal::logf(pal::kLogWarn, "MESH", "the Bluetooth keyboard is up: turn it off before the mesh (cfg bt off)");
      return false;
    }
    if (!esp32_radio_for("the mesh")) {
      hal::logf(pal::kLogWarn, "MESH", "the radio did not come up, so there is no mesh");
      return false;
    }
    // **L'UUID porta l'adreça d'aquesta placa**: the first six bytes are the radio's, o sigui que tres plaques al davant d'un provisionador
    // no són el mateix node (docs/50). `esp_read_mac` es pot preguntar abans que la pila s'hagi sincronitzat, que és quan es necessita.
    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_BT) != ESP_OK) {
      hal::logf(pal::kLogWarn, "MESH", "this board has no address to present");
      return false;
    }
    mesh_node_set_uuid(mac);
    // **El nom, amb els dos últims bytes de l'adreça** (0.190.215): `lunokOS-XXYY`, que és exactament el que aquesta placa diu quan és un
    // teclat de Bluetooth —la mateixa adreça de l'eFuse i el mateix format—, o sigui que un provisionador i un telèfon que aparellen veuen
    // el mateix nom de la mateixa placa (docs/50). El nom del node no té el límit de trenta-un bytes de l'anunci del Bluetooth.
    char mine[32];
    the_board_name(mine, sizeof mine);
    mesh_node_set_name(mine);
    if (!mesh_node_init()) return false;
    return mesh_node_offer();
  }

  void stop() override { mesh_node_withdraw(); }
  bool is_up() const override { return mesh_node_address() != 0 || mesh_node_is_provisioned(); }
  bool provisioned() const override { return mesh_node_is_provisioned(); }
  uint16_t address() const override { return mesh_node_address(); }
};

}  // namespace

IMesh* esp32_mesh() {
  static Esp32Mesh node;
  return &node;
}

}  // namespace hal
