// **El node de Bluetooth Mesh** (0.190.212, l'amo: *«pots posar algun mesh?»*). El que hi ha aquí és el mínim que la pila d'Espressif
// demana per ser un node: un element amb el servidor de configuració, que és l'únic model que tot node ha de tenir, i res més. **El que no
// hi és, i és a posta**: cap model de més —ni un llum ni un relé que la placa no té—, perquè un model és una promesa i el que aquest node
// sap fer és existir, tenir adreça i respondre al provisionador (docs/50).
//
// **Aquest fitxer és C i no C++** (0.190.212): els macros dels models d'Espressif fan servir `[0 ... N] = valor`, que en C és un designador
// de rang i en C++ és una lambda, o sigui que el mesh d'aquesta pila **no compila com a C++**. La frontera amb la resta del sistema és
// `mesh_node.h`, que no ensenya cap capçalera del mesh (docs/45).
#include "esp32/mesh_node.h"

#include "sdkconfig.h"

#if CONFIG_BLE_MESH

#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_local_data_operation_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_log.h"

#include <string.h>

static const char *kTag = "MESH";

// **L'UUID amb què aquest node es presenta** (el primer que el provisionador llegeix): setze bytes de la forma que la pila espera, amb
// l'adreça de la ràdio a dins, o sigui que dues plaques de la mateixa casa no són el mateix node. La forma `dddd…` és la dels exemples
// d'Espressif i no una convenció de la xarxa: el que importa és que sigui estable i únic (docs/50).
static uint8_t g_uuid[16] = {0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd,
                             0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd};

// **El servidor de configuració, que tot node ha de tenir**: el que el provisionador llegeix i escriu per saber com es comporta aquest
// node dins la xarxa. El relé, apagat —un node que no té res a repetir no gasta aire repetint—, i el proxy encès, que és el que permet que
// un telèfon arribi a la xarxa per una connexió (docs/50).
//
// **Els camps s'omplen a `mesh_node_init` i no a la declaració** (0.190.212): l'inicialitzador d'una estructura d'aquestes, amb els seus
// agregats a dins, és una font de `-Wmissing-braces` que no diu res del que el node fa; una taula a zero i vuit assignacions es llegeixen
// millor i no enganyen el compilador (docs/45).
static esp_ble_mesh_cfg_srv_t g_config_server;

static esp_ble_mesh_model_t g_root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&g_config_server),
};

static esp_ble_mesh_elem_t g_elements[] = {
    ESP_BLE_MESH_ELEMENT(0, g_root_models, ESP_BLE_MESH_MODEL_NONE),
};

static esp_ble_mesh_comp_t g_composition;

static esp_ble_mesh_prov_t g_provision;

static bool g_up = false;

// **El que el provisionador fa, dit** (0.190.212): every step —the link it opens, the address it gives, the reset that takes it away— is a
// line in the log, perquè una cosa que es queda a mitges en una xarxa no es pot mirar enlloc més (docs/50).
static void prov_callback(esp_ble_mesh_prov_cb_event_t event, esp_ble_mesh_prov_cb_param_t *param) {
  switch (event) {
    case ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT:
      ESP_LOGI(kTag, "a provisioner is talking to this node (%s bearer)",
               (param->node_prov_link_open.bearer == 1) ? "the GATT one" : "the advertising one");
      break;
    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
      ESP_LOGI(kTag, "provisioned: this node is 0x%04X in the network 0x%04X",
               (unsigned)param->node_prov_complete.addr, (unsigned)param->node_prov_complete.net_idx);
      break;
    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
      ESP_LOGW(kTag, "the mesh forgot this node: it is unprovisioned again");
      break;
    default:
      break;
  }
}

static void config_callback(esp_ble_mesh_cfg_server_cb_event_t event, esp_ble_mesh_cfg_server_cb_param_t *param) {
  // **El que el provisionador ha canviat d'aquest node**: the configuration server speaks for itself (the relay, the proxy, the beacon,
  // the address…), o sigui que el que es diu és quin estat ha canviat i de quin node, i els camps que no es poden llegir sense
  // endevinar-los no s'inventen (docs/50).
  if (event != ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT) return;
  ESP_LOGI(kTag, "a configuration message from 0x%04X changed this node", (unsigned)param->ctx.addr);
}

void mesh_node_set_uuid(const uint8_t mac[6]) {
  if (mac != NULL) memcpy(&g_uuid[0], mac, 6);
}

bool mesh_node_init(void) {
  // **El que aquest node és, dit una vegada**: the relay off, the proxy on, the beacon on and a TTL of seven, que és el que fa un node
  // petit que només vol que el trobin (docs/50).
  g_config_server.net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20);
  g_config_server.relay = ESP_BLE_MESH_RELAY_DISABLED;
  g_config_server.relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20);
  g_config_server.beacon = ESP_BLE_MESH_BEACON_ENABLED;
  g_config_server.gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED;
  g_config_server.friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED;
  g_config_server.default_ttl = 7;
  // **Cap empresa**: aquest node només porta models de la SIG —el de configuració—, i el CID d'una composició sense models de fabricant és
  // el valor de «cap» (0xFFFF). El dia que hi hagi un model propi, aquí hi anirà el de la casa.
  g_composition.cid = ESP_BLE_MESH_CID_NVAL;
  g_composition.element_count = sizeof(g_elements) / sizeof(g_elements[0]);
  g_composition.elements = g_elements;
  g_provision.uuid = g_uuid;
  g_provision.output_size = 0;
  g_provision.output_actions = 0;
  if (esp_ble_mesh_register_prov_callback(prov_callback) != ESP_OK) {
    ESP_LOGW(kTag, "the provisioner could not be registered");
    return false;
  }
  if (esp_ble_mesh_register_config_server_callback(config_callback) != ESP_OK) {
    ESP_LOGW(kTag, "the configuration server could not be registered");
    return false;
  }
  const esp_err_t err = esp_ble_mesh_init(&g_provision, &g_composition);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "the mesh did not come up (%d)", (int)err);
    return false;
  }
  g_up = true;
  return true;
}

bool mesh_node_offer(void) {
  if (!g_up) return false;
  if (esp_ble_mesh_node_is_provisioned()) {
    ESP_LOGI(kTag, "this node is already in a network at 0x%04X", (unsigned)esp_ble_mesh_get_primary_element_address());
    return true;
  }
  // **S'ofereix pels dos portadors**, que és el que fa que un telèfon el trobi: l'anunciant (PB-ADV) i el GATT (PB-GATT) (docs/50).
  const esp_err_t err = esp_ble_mesh_node_prov_enable(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "this node is not offering itself (%d)", (int)err);
    return false;
  }
  ESP_LOGI(kTag, "the mesh is up and this node is waiting for a provisioner");
  return true;
}

void mesh_node_withdraw(void) {
  if (!g_up) return;
  (void)esp_ble_mesh_node_prov_disable(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT);
  ESP_LOGI(kTag, "this node is no longer offering itself");
}

bool mesh_node_is_provisioned(void) { return g_up && esp_ble_mesh_node_is_provisioned(); }

uint16_t mesh_node_address(void) { return g_up ? esp_ble_mesh_get_primary_element_address() : 0; }

#else  // this build has no mesh in it: the door answers, and says it cannot

void mesh_node_set_uuid(const uint8_t mac[6]) { (void)mac; }
bool mesh_node_init(void) { return false; }
bool mesh_node_offer(void) { return false; }
void mesh_node_withdraw(void) {}
bool mesh_node_is_provisioned(void) { return false; }
uint16_t mesh_node_address(void) { return 0; }

#endif  // CONFIG_BLE_MESH
