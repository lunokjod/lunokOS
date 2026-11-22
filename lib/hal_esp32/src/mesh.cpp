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
    // **El mesh registra els seus serveis GATT i arrenca el servidor dins d'aquest `init`** (0.190.217): per això la ràdio s'ha portat fins
    // aquí sense el servei del teclat, i la tasca de l'amfitrió (`esp32_radio_start`) s'arrenca **després**, quan els serveis del mesh ja hi
    // són. Amb l'ordre d'abans, l'app del telèfon deia *«Discover device services failed»* perquè el 0x1827 de la provisió no hi era (docs/50).
    if (!mesh_node_init()) return false;
    esp32_radio_start("the mesh");
    // **El nom, amb els dos últims bytes de l'adreça** (0.190.215): `lunokOS-XXYY`, que és exactament el que aquesta placa diu quan és un
    // teclat de Bluetooth —la mateixa adreça de l'eFuse i el mateix format—, o sigui que un provisionador i un telèfon que aparellen veuen
    // el mateix nom de la mateixa placa. El nom del node no té el límit de trenta-un bytes de l'anunci del Bluetooth.
    //
    // **I va DESPRÉS de `mesh_node_init` i no abans** (0.190.216, i la placa ho va dir amb un assert de debò: `osi_thread_post thread.c:332
    // (thread != NULL)`): totes les crides del mesh d'Espressif són missatges a la tasca `btc`, i aquesta tasca **la crea
    // `esp_ble_mesh_init`** (`btc_init`). Posar el nom abans és demanar-li un missatge a una tasca que encara no existeix. I ha de ser abans de
    // `mesh_node_offer`, perquè el nom viatja a la resposta de l'escaneig de la provisió (docs/50).
    char mine[32];
    the_board_name(mine, sizeof mine);
    mesh_node_set_name(mine);
    // **Un provisionador sense claus té la pila enlaire i cap xarxa** (0.190.218): això no és un fracàs sinó el primer pas, i el que falta és
    // `mesh key`. Un build que només és node sí que falla aquí, perquè el que se li demanava era oferir-se (docs/30, docs/50).
    if (!mesh_node_offer()) {
      if (!can_keep_the_keys()) {
        hal::logf(pal::kLogWarn, "MESH", "this node could not offer itself, and the reason is in the log above");
        return false;
      }
      hal::logf(pal::kLogWarn, "MESH", "the stack is up and this board has no network yet: `mesh key` makes one");
    }
    return true;
  }

  void stop() override { mesh_node_withdraw(); }
  bool is_up() const override { return mesh_node_is_up(); }
  bool provisioned() const override { return mesh_node_is_provisioned(); }
  uint16_t address() const override { return mesh_node_address(); }

  // ---- la xarxa que aquesta placa té ----
  // **El provisionador és un build i prou**: `CONFIG_BLE_MESH_PROVISIONER` porta el codi que reparteix les claus, i una banda sense això
  // continua essent un node que s'ofereix (el T5S3 i el Paper, si no hi caben) i ho diu (docs/45, docs/50).
#if defined(CONFIG_BLE_MESH_PROVISIONER)
  bool can_keep_the_keys() const override { return true; }
#else
  bool can_keep_the_keys() const override { return false; }
#endif

  bool has_the_network() const override { return mesh_node_has_the_network(); }

  bool make_keys(uint8_t net_key[16], uint8_t app_key[16]) override {
    if (!can_keep_the_keys()) return false;
    return mesh_node_make_keys(net_key, app_key);
  }

  bool own_the_network(const uint8_t net_key[16], const uint8_t app_key[16]) override {
    if (!can_keep_the_keys()) return false;
    return mesh_node_own_the_network(net_key, app_key);
  }

  bool network_keys(uint8_t net_key[16], uint8_t app_key[16]) const override {
    return mesh_node_network_keys(net_key, app_key);
  }

  int watch(uint32_t ms) override { return static_cast<int>(mesh_node_watch(ms)); }
  int heard_count() const override { return static_cast<int>(mesh_node_heard_count()); }

  bool heard_at(int index, uint8_t uuid[16], int* rssi) const override {
    if (index < 0 || index > 255) return false;
    uint8_t bearer = 0;
    int8_t strength = 0;
    if (!mesh_node_heard_at(static_cast<uint8_t>(index), uuid, &bearer, &strength)) return false;
    if (rssi != nullptr) *rssi = strength;
    return true;
  }

  bool provision(int index, uint16_t address, uint16_t* given) override {
    if (index < 0 || index > 255) return false;
    return mesh_node_provision(static_cast<uint8_t>(index), address, given);
  }

  int known(uint16_t* addresses, int cap) const override {
    if (cap <= 0 || cap > 255) return 0;
    return static_cast<int>(mesh_node_known(addresses, static_cast<uint8_t>(cap)));
  }
};

}  // namespace

IMesh* esp32_mesh() {
  static Esp32Mesh node;
  return &node;
}

}  // namespace hal
