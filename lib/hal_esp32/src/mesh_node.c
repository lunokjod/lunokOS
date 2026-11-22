// **El mesh d'aquesta placa: node i provisionador** (0.190.212, i el provisionador des de la 0.190.218). La composició és la mínima que la
// pila demana —un element amb el servidor de configuració i, quan hi ha provisionador, el client de configuració, que és l'únic que pot
// parlar-li a un node—, i el que fa de més és **tenir la xarxa i donar-la**: les claus les posa aquí, els nodes que s'hi afegeixen reben la
// NetKey i una adreça d'aquesta placa, i tot seguit la **AppKey lligada als seus models** —sense això un node queda a la xarxa i no diu res
// (docs/50).
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
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_log.h"
#include "esp_random.h"

#include <string.h>

static const char *kTag = "MESH";

// **L'adreça d'aquesta placa dins la xarxa** i **la primera que dóna**: 0x0001 és la del provisionador (el primer element és sempre el seu) i
// de 0x0005 ençà van els altres, que és el que fa l'exemple d'Espressif quan el provisionador no té models propis a l'element zero (docs/50).
#define OWN_ADDRESS 0x0001
#define FIRST_GIVEN 0x0005

// **Quants aparells sense xarxa s'escolten alhora**: vuit és el que cap en una llista que es llegeix d'un cop al prompt, i el que no hi cap
// es torna a sentir en la propera passa (docs/30).
#define MOST_HEARD 8
// L'index amb què la placa desa les dues claus seves: el valor de la clau és el que tria l'amo, l'index el tria ella (docs/50).
#define kNetIndex 0x0000
#define kAppIndex 0x0000

static uint8_t g_uuid[16] = {0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd,
                             0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd};

// El servidor de configuració, que tot node ha de tenir. Els camps s'omplen a `mesh_node_init` i no a la declaració, perquè l'inicialitzador
// d'una estructura d'aquestes amb els seus agregats a dins és una font de `-Wmissing-braces` que no diu res del que el node fa (docs/45).
static esp_ble_mesh_cfg_srv_t g_config_server;
// **I el client de configuració, que és el que pot parlar-li a un node** (0.190.218): un provisionador que només prové deixa els nodes amb la
// clau de xarxa i sense clau d'aplicació, o sigui que no parlen. El client de configuració és l'únic que pot manar missatges a un node amb la
// seva clau de dispositiu, i és per on van la clau d'aplicació i el lligam als models (docs/50).
#if CONFIG_BLE_MESH_PROVISIONER
static esp_ble_mesh_client_t g_config_client;
#endif
static esp_ble_mesh_model_t g_root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&g_config_server),
#if CONFIG_BLE_MESH_PROVISIONER
    ESP_BLE_MESH_MODEL_CFG_CLI(&g_config_client),
#endif
};
static esp_ble_mesh_elem_t g_elements[] = {
    ESP_BLE_MESH_ELEMENT(0, g_root_models, ESP_BLE_MESH_MODEL_NONE),
};
static esp_ble_mesh_comp_t g_composition;
// **La identitat del provisionador es declara i no s'assigna**: `prov_uuid` i `prov_unicast_addr` són **const** a l'estructura d'Espressif —o
// sigui que l'adreça d'aquesta placa dins la xarxa no pot canviar després—, i això és el que fa que es pugui declarar aquí amb l'adreça bona en
// lloc de confiar que ningú la toqui més tard (docs/50).
#if CONFIG_BLE_MESH_PROVISIONER
static esp_ble_mesh_prov_t g_provision = {
    .prov_uuid = g_uuid,
    .prov_unicast_addr = OWN_ADDRESS,
    .prov_start_address = FIRST_GIVEN,
    .prov_attention = 0x00,
    .prov_algorithm = 0x00,
    .prov_pub_key_oob = 0x00,
    .prov_static_oob_val = NULL,
    .prov_static_oob_len = 0x00,
    .flags = 0x00,
    .iv_index = 0x00,
};
#else
static esp_ble_mesh_prov_t g_provision;
#endif

static bool g_up = false;

// **La xarxa d'aquesta placa**: la clau que reparteix i la que fan servir les aplicacions, i si ja hi són.
static uint8_t g_net_key[16];
static uint8_t g_app_key[16];
static bool g_has_the_network = false;

// **El que el provisionador ha sentit**: la UUID que l'altre aparell anuncia, el portador pel qual s'ha sentit i la força. És el que la
// comanda `mesh find` ensenya i el que `mesh prov <n>` fa servir (docs/30, docs/50).
struct Heard {
  uint8_t uuid[16];
  uint8_t addr[6];
  uint8_t addr_type;
  uint8_t bearer;
  int8_t rssi;
};
static struct Heard g_heard[MOST_HEARD];
static uint8_t g_heard_count = 0;

// **I el que ha quedat fet**: l'adreça del darrer node provisionat, que és el que la comanda contesta.
static uint16_t g_last_provisioned = 0;

void mesh_node_set_uuid(const uint8_t mac[6]) {
  if (mac != NULL) memcpy(&g_uuid[0], mac, 6);
}

static void remember_heard(const uint8_t uuid[16], const uint8_t addr[6], uint8_t addr_type, uint8_t bearer, int8_t rssi) {
  // **Un aparell es torna a sentir moltes vegades**: el que es queda és l'última oïda de cada UUID, o sigui que la llista és dels aparells i
  // no dels anuncis (docs/50).
  for (uint8_t i = 0; i < g_heard_count; ++i) {
    if (memcmp(g_heard[i].uuid, uuid, 16) == 0) {
      g_heard[i].rssi = rssi;
      g_heard[i].bearer = bearer;
      return;
    }
  }
  if (g_heard_count >= MOST_HEARD) return;
  struct Heard *at = &g_heard[g_heard_count++];
  memcpy(at->uuid, uuid, 16);
  if (addr != NULL) memcpy(at->addr, addr, 6);
  at->addr_type = addr_type;
  at->bearer = bearer;
  at->rssi = rssi;
}

#if CONFIG_BLE_MESH_PROVISIONER && !CONFIG_BLE_MESH_CFG_CLI
// **Un provisionador sense client de configuració és un enllaç trencat, i val més dir-ho aquí** (0.190.218): `esp_ble_mesh_config_client_*` no
// existeix al binari si `CONFIG_BLE_MESH_CFG_CLI` no és al `sdkconfig`, o sigui que el que es veia era un `undefined reference` a l'enllaç que
// no deia què faltava. I aquesta opció **no l'encén** `CONFIG_BLE_MESH_PROVISIONER`: és un model de la capa de fundació i es diu a part
// (`sdkconfig.defaults.<banda>`, docs/50).
#error "a provisioner needs CONFIG_BLE_MESH_CFG_CLI=y: it is how the application key reaches the node"
#endif

#if CONFIG_BLE_MESH_PROVISIONER
// **La clau d'aplicació, que és el que fa que la provisió serveixi d'alguna cosa** (0.190.218). La provisió dóna al node **la clau de xarxa i
// una adreça, i prou**: els seus models no parlen fins que tenen una AppKey **lligada**, i això no ho fa la provisió. Ho fa el provisionador
// amb el client de configuració, que és l'únic que pot manar-li coses amb la **clau de dispositiu** que van intercanviar durant la provisió.
//
// El camí és el de l'exemple d'Espressif, i són tres passos: demanar-li la **composició** (per saber quins models té i a quins elements),
// donar-li la **clau d'aplicació**, i **lligar-la a cada model**. Cada pas espera la resposta de l'anterior; una resposta que no arriba es
// torna a provar i, si no, es deixa estar i es diu al log, perquè una provisió a mitges que no ho diu és pitjor que una que falla (docs/50).
#define kMostBinds 12
#define kCfgTries 3

struct Bind {
  uint16_t element_addr;
  uint16_t model_id;
};
static struct Bind g_to_bind[kMostBinds];
static uint8_t g_bind_count = 0;
static uint8_t g_bind_at = 0;
static uint8_t g_cfg_tries = 0;
// Si el node tenia més models dels que aquesta placa enlliga: es diu, perquè un model sense clau és un model mut i no un error d'escriptura.
static bool g_bind_full = false;
// El node que s'està configurant: el que s'acaba de provisionar, i a qui van els tres passos (docs/50).
static uint16_t g_cfg_node = 0;

// **El context de tot missatge de configuració**: la clau que hi posa la pila és la de dispositiu, no la d'aplicació —és el que fa quan el
// model és el de configuració—, o sigui que el que s'hi escriu a `app_idx` no decideix res (docs/50).
static void cfg_common(esp_ble_mesh_client_common_param_t *common, uint32_t opcode, uint16_t addr) {
  memset(common, 0, sizeof(*common));
  common->opcode = opcode;
  common->model = g_config_client.model;
  common->ctx.net_idx = kNetIndex;
  common->ctx.app_idx = kAppIndex;
  common->ctx.addr = addr;
  common->ctx.send_ttl = 7;
  common->ctx.send_rel = false;
  common->msg_timeout = 0;
  common->msg_role = ROLE_PROVISIONER;
}

// **Pas 1: quins models té.** Sense això no se sap què lligar ni a quin element viu (docs/50).
static void ask_the_composition(uint16_t addr) {
  esp_ble_mesh_client_common_param_t common;
  esp_ble_mesh_cfg_client_get_state_t get;
  memset(&get, 0, sizeof(get));
  cfg_common(&common, ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET, addr);
  get.comp_data_get.page = 0x00;
  if (esp_ble_mesh_config_client_get_state(&common, &get) != ESP_OK) {
    ESP_LOGW(kTag, "0x%04X did not take the question about its models", (unsigned)addr);
  }
}

// **Pas 2: la clau.** El valor és el d'aquesta xarxa i l'índex el mateix que fa servir aquesta placa (docs/50).
static void give_the_app_key(uint16_t addr) {
  esp_ble_mesh_client_common_param_t common;
  esp_ble_mesh_cfg_client_set_state_t set;
  memset(&set, 0, sizeof(set));
  cfg_common(&common, ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD, addr);
  set.app_key_add.net_idx = kNetIndex;
  set.app_key_add.app_idx = kAppIndex;
  memcpy(set.app_key_add.app_key, g_app_key, 16);
  if (esp_ble_mesh_config_client_set_state(&common, &set) != ESP_OK) {
    ESP_LOGW(kTag, "0x%04X did not take the application key", (unsigned)addr);
  }
}

// **Pas 3: lligar-la, model per model**, un darrere l'altre: cada lligam espera el seu estat, perquè dotze missatges seguits a un node que
// acaba de néixer és demanar-li més del que pot (docs/50).
static void bind_the_next(void) {
  // **Un bucle i no una crida a si mateixa**: el que no s'ha pogut manar es diu i es passa al següent, sense apilar una crida per model (que
  // amb dotze models i una tasca que ja porta la pila del mesh és demanar problemes on no n'hi ha) (docs/45).
  while (g_bind_at < g_bind_count) {
    const struct Bind at = g_to_bind[g_bind_at++];
    esp_ble_mesh_client_common_param_t common;
    esp_ble_mesh_cfg_client_set_state_t set;
    memset(&set, 0, sizeof(set));
    cfg_common(&common, ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND, g_cfg_node);
    set.model_app_bind.element_addr = at.element_addr;
    set.model_app_bind.model_app_idx = kAppIndex;
    set.model_app_bind.model_id = at.model_id;
    set.model_app_bind.company_id = ESP_BLE_MESH_CID_NVAL;
    if (esp_ble_mesh_config_client_set_state(&common, &set) == ESP_OK) return;
    ESP_LOGW(kTag, "model 0x%04X of element 0x%04X was not bound", (unsigned)at.model_id, (unsigned)at.element_addr);
  }
  ESP_LOGI(kTag, "0x%04X is in the network: %u model(s) bound to the application key", (unsigned)g_cfg_node,
           (unsigned)g_bind_count);
}

// **La composició, llegida** (0.190.218). La pàgina zero és una capçalera de cinc parells de bytes —CID, PID, VID, CRPL i features, cap dels
// quals diu res aquí— i després **un element darrere l'altre**: la ubicació (dos bytes, que no es fa servir), quants models de la SIG (**un**
// byte) i quants de fabricant (**un** byte), i els identificadors de cadascun (dos bytes per model de la SIG, quatre per als de fabricant,
// que porten el `company_id` a sobre).
//
// **I el recompte d'elements no hi és a les plaques d'Espressif**, i això no és una opinió: `comp_get_page_0` del seu codi
// (`mesh_core/cfg_srv.c`) escriu els cinc parells i crida `comp_add_elem` per a cada element, sense cap comptador al mig —i `comp_add_elem`
// fa la ubicació amb `add_le16` i els dos comptadors amb `add_u8`—, o sigui que el final de la composició es coneix per la **llargada** i no
// per un número. Un node de fora sí que el porta (és el primer camp després dels cinc parells, i mai no val zero, perquè un node sense
// elements no és un node), i per això es mira **el primer byte**: si no és zero és el comptador i es passa de llarg, i si és zero és la
// ubicació d'un element d'Espressif (que és 0x0000). Amb les dues coses la lectura no depèn de qui ha escrit la composició (docs/50).
static void plan_the_binds(const uint8_t *data, uint32_t len, uint16_t node_addr) {
  g_bind_count = 0;
  g_bind_at = 0;
  g_bind_full = false;
  if (data == NULL || len < 14) return;
  uint32_t at = 10;
  if (data[at] != 0x00) ++at;
  uint16_t element = 0;
  while (at + 4 <= len) {
    at += 2;  // la ubicació de l'element
    const uint8_t sig = data[at++];
    const uint8_t vendor = data[at++];
    if (at + (uint32_t)sig * 2 + (uint32_t)vendor * 4 > len) return;
    const uint16_t element_addr = (uint16_t)(node_addr + element);
    for (uint8_t m = 0; m < sig; ++m) {
      const uint16_t id = (uint16_t)(data[at] | ((uint16_t)data[at + 1] << 8));
      at += 2;
      if (g_bind_count < kMostBinds) {
        g_to_bind[g_bind_count].element_addr = element_addr;
        g_to_bind[g_bind_count].model_id = id;
        ++g_bind_count;
      } else if (!g_bind_full) {
        g_bind_full = true;
        ESP_LOGW(kTag, "this node has more models than the %d this board binds: the rest keep no key", kMostBinds);
      }
    }
    at += (uint32_t)vendor * 4;  // els models de fabricant, que el lligam voldria amb el seu company_id
    ++element;
  }
}

static void config_client_callback(esp_ble_mesh_cfg_client_cb_event_t event, esp_ble_mesh_cfg_client_cb_param_t *param) {
  if (param == NULL || param->params == NULL) return;
  const uint32_t opcode = param->params->opcode;
  if (param->error_code != 0) {
    ESP_LOGW(kTag, "the configuration message 0x%08X did not go out (%d)", (unsigned)opcode, param->error_code);
    return;
  }
  switch (event) {
    case ESP_BLE_MESH_CFG_CLIENT_GET_STATE_EVT:
      if (opcode == ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET && param->status_cb.comp_data_status.composition_data != NULL) {
        const struct net_buf_simple *data = param->status_cb.comp_data_status.composition_data;
        plan_the_binds(data->data, data->len, g_cfg_node);
        g_cfg_tries = 0;
        ESP_LOGI(kTag, "0x%04X answered with its models: %u to bind", (unsigned)g_cfg_node, (unsigned)g_bind_count);
        give_the_app_key(g_cfg_node);
      }
      break;
    case ESP_BLE_MESH_CFG_CLIENT_SET_STATE_EVT:
      if (opcode == ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD) {
        g_cfg_tries = 0;
        ESP_LOGI(kTag, "0x%04X took the application key (status 0x%02X)", (unsigned)g_cfg_node,
                 param->status_cb.appkey_status.status);
        bind_the_next();
      } else if (opcode == ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND) {
        g_cfg_tries = 0;
        bind_the_next();
      }
      break;
    case ESP_BLE_MESH_CFG_CLIENT_TIMEOUT_EVT:
      // **Una resposta que no arriba no atura la feina**: es torna a provar el mateix pas fins a tres vegades i, si no, es passa al següent,
      // perquè un node que no contesta és una cosa que s'ha de saber i no una que s'ha d'esperar per sempre (docs/50). I el lligam és l'únic
      // pas que pot saltar-se un model i continuar: la resta de models no té cap culpa que un no hi sigui (docs/50).
      if (opcode == ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND) {
        if (g_cfg_tries++ >= kCfgTries) {
          ESP_LOGW(kTag, "a model of 0x%04X did not answer, and the others go on", (unsigned)g_cfg_node);
          g_cfg_tries = 0;
        } else {
          --g_bind_at;
        }
        bind_the_next();
      } else if (g_cfg_tries++ < kCfgTries) {
        if (opcode == ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET) {
          ask_the_composition(g_cfg_node);
        } else if (opcode == ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD) {
          give_the_app_key(g_cfg_node);
        }
      } else {
        ESP_LOGW(kTag, "0x%04X stopped answering (0x%08X), and what is done is done", (unsigned)g_cfg_node, (unsigned)opcode);
        g_cfg_tries = 0;
      }
      break;
    default:
      break;
  }
}
#endif  // CONFIG_BLE_MESH_PROVISIONER

// **El que el provisionador fa, dit** (0.190.218): every step —the device it hears, the address it gives, the node that is in the network— is
// a line in the log, perquè una cosa que es queda a mitges en una xarxa no es pot mirar enlloc més (docs/50).
static void prov_callback(esp_ble_mesh_prov_cb_event_t event, esp_ble_mesh_prov_cb_param_t *param) {
  switch (event) {
    case ESP_BLE_MESH_PROVISIONER_RECV_UNPROV_ADV_PKT_EVT:
      remember_heard(param->provisioner_recv_unprov_adv_pkt.dev_uuid, param->provisioner_recv_unprov_adv_pkt.addr,
                     (uint8_t)param->provisioner_recv_unprov_adv_pkt.addr_type,
                     (uint8_t)param->provisioner_recv_unprov_adv_pkt.bearer,
                     param->provisioner_recv_unprov_adv_pkt.rssi);
      break;
    case ESP_BLE_MESH_PROVISIONER_PROV_COMPLETE_EVT:
      g_last_provisioned = param->provisioner_prov_complete.unicast_addr;
      ESP_LOGI(kTag, "provisioned: the node is 0x%04X with %u element(s) (net key index 0x%04X)", (unsigned)g_last_provisioned,
               (unsigned)param->provisioner_prov_complete.element_num, (unsigned)param->provisioner_prov_complete.netkey_idx);
      // **I tot seguit la clau d'aplicació** (0.190.218): la provisió deixa el node amb la clau de xarxa i una adreça, i un node així no
      // parla amb ningú. Els tres passos comencen aquí i el log diu com van (docs/50).
#if CONFIG_BLE_MESH_PROVISIONER
      g_cfg_node = g_last_provisioned;
      g_bind_count = 0;
      g_bind_at = 0;
      g_cfg_tries = 0;
      ask_the_composition(g_cfg_node);
#endif
      break;
    case ESP_BLE_MESH_PROVISIONER_PROV_LINK_OPEN_EVT:
      ESP_LOGI(kTag, "a link is open with a device (%s bearer)",
               (param->provisioner_prov_link_open.bearer == ESP_BLE_MESH_PROV_GATT) ? "the GATT one" : "the advertising one");
      break;
    case ESP_BLE_MESH_PROVISIONER_PROV_LINK_CLOSE_EVT:
      ESP_LOGI(kTag, "the link is closed (%s)", param->provisioner_prov_link_close.reason == 0 ? "it worked" : "it did not");
      break;
    case ESP_BLE_MESH_PROVISIONER_PROV_ENABLE_COMP_EVT:
      // **Aquí és on es diu la veritat** (0.190.218): l'API d'aquesta funció només contesta si el missatge s'ha pogut posar a la cua de la
      // tasca `btc`, i el resultat de debò arriba en aquest event. I hi ha un error que una placa d'aquesta casa es trobarà segur: `-EIO`,
      // el paper que la botiga del mesh va desar quan aquesta placa era un **node** (0.190.212) —`bt_mesh_provisioner_enable` el refusa si el
      // paper desat no és el de provisionador—, i per això `mesh_node_init` el neteja abans (docs/50).
      if (param->provisioner_prov_enable_comp.err_code == 0) {
        ESP_LOGI(kTag, "this board is a provisioner and it is watching for devices");
      } else {
        ESP_LOGW(kTag, "this board could not watch for devices (err %d)", param->provisioner_prov_enable_comp.err_code);
      }
      break;
    case ESP_BLE_MESH_PROVISIONER_PROV_DISABLE_COMP_EVT:
      ESP_LOGI(kTag, "this board is no longer watching for devices (err %d)", param->provisioner_prov_disable_comp.err_code);
      break;
    case ESP_BLE_MESH_PROVISIONER_ADD_LOCAL_NET_KEY_COMP_EVT:
      ESP_LOGI(kTag, "the network key is in (err %d)", param->provisioner_add_net_key_comp.err_code);
      break;
    case ESP_BLE_MESH_PROVISIONER_ADD_LOCAL_APP_KEY_COMP_EVT:
      ESP_LOGI(kTag, "the application key is in (err %d)", param->provisioner_add_app_key_comp.err_code);
      break;
    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
      ESP_LOGI(kTag, "this board is in the network at 0x%04X", (unsigned)param->node_prov_complete.addr);
      break;
    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
      ESP_LOGW(kTag, "the mesh forgot this board");
      break;
    default:
      break;
  }
}

static void config_callback(esp_ble_mesh_cfg_server_cb_event_t event, esp_ble_mesh_cfg_server_cb_param_t *param) {
  if (event != ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT) return;
  ESP_LOGI(kTag, "a configuration message from 0x%04X changed this board", (unsigned)param->ctx.addr);
}

void mesh_node_set_name(const char *name) {
  if (name == NULL || name[0] == '\0') return;
  if (esp_ble_mesh_set_unprovisioned_device_name(name) != ESP_OK) {
    ESP_LOGW(kTag, "this node has no name to present");
  }
}

bool mesh_node_init(void) {
  if (g_up) return true;
  // **El que aquesta placa és, dit una vegada**: relay off (no té res a repetir), proxy on (que un telèfon hi arribi), beacons on i un TTL de
  // set, que és el que fa un node petit que només vol que el trobin (docs/50).
  g_config_server.net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20);
  g_config_server.relay = ESP_BLE_MESH_RELAY_DISABLED;
  g_config_server.relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20);
  g_config_server.beacon = ESP_BLE_MESH_BEACON_ENABLED;
  g_config_server.gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED;
  g_config_server.friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED;
  g_config_server.default_ttl = 7;
  // **Cap empresa**: els models d'aquesta composició són de la SIG i el CID de «cap» és 0xFFFF. El dia que hi hagi un model propi, aquí hi
  // anirà el de la casa.
  g_composition.cid = ESP_BLE_MESH_CID_NVAL;
  g_composition.element_count = sizeof(g_elements) / sizeof(g_elements[0]);
  g_composition.elements = g_elements;
  // **L'adreça d'aquesta placa i la primera que dóna**: el provisionador s'ha de poder situar dins la xarxa que ell mateix crea, o no pot
  // donar res a ningú (docs/50).
#if !CONFIG_BLE_MESH_PROVISIONER
  // **Un node sol, que s'ofereix a qui el vulgui provisionar** (el build sense provisionador): aquí l'adreça i les claus vénen de fora.
  g_provision.uuid = g_uuid;
  g_provision.output_size = 0;
  g_provision.output_actions = 0;
#endif
  if (esp_ble_mesh_register_prov_callback(prov_callback) != ESP_OK) {
    ESP_LOGW(kTag, "the provisioner could not be registered");
    return false;
  }
  if (esp_ble_mesh_register_config_server_callback(config_callback) != ESP_OK) {
    ESP_LOGW(kTag, "the configuration server could not be registered");
    return false;
  }
#if CONFIG_BLE_MESH_PROVISIONER
  if (esp_ble_mesh_register_config_client_callback(config_client_callback) != ESP_OK) {
    ESP_LOGW(kTag, "the configuration client could not be registered");
    return false;
  }
#endif
  const esp_err_t err = esp_ble_mesh_init(&g_provision, &g_composition);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "the mesh did not come up (%d)", (int)err);
    return false;
  }
  g_up = true;
#if CONFIG_BLE_MESH_PROVISIONER
  // **El paper vell, esborrat** (0.190.218): la botiga del mesh desa si aquesta placa era NODE o PROVISIONADOR, i
  // `bt_mesh_provisioner_enable` refusa (`-EIO`, «Mismatch role») si el que hi ha desat no és el seu. Una placa que va córrer el build de
  // node (0.190.212) té NODE desat, o sigui que **el desactiva el node primer** —que és el que esborra el paper (`bt_mesh_prov_disable` crida
  // `bt_mesh_clear_role`)— i després encén el provisionador. Si la placa no havia estat mai node, això no fa res (docs/50).
  (void)esp_ble_mesh_node_prov_disable((esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
#endif
  return true;
}

bool mesh_node_is_up(void) { return g_up; }

bool mesh_node_is_provisioned(void) {
#if CONFIG_BLE_MESH_PROVISIONER
  // **El provisionador és dins la seva pròpia xarxa per definició**: ell la crea, o sigui que el que es pregunta és si la clau ja hi és.
  return g_has_the_network;
#else
  return g_up && esp_ble_mesh_node_is_provisioned();
#endif
}

uint16_t mesh_node_address(void) {
#if CONFIG_BLE_MESH_PROVISIONER
  return g_has_the_network ? OWN_ADDRESS : 0;
#else
  return g_up ? esp_ble_mesh_get_primary_element_address() : 0;
#endif
}

bool mesh_node_make_keys(uint8_t net_key[16], uint8_t app_key[16]) {
  if (net_key == NULL || app_key == NULL) return false;
  esp_fill_random(net_key, 16);
  esp_fill_random(app_key, 16);
  return mesh_node_own_the_network(net_key, app_key);
}

bool mesh_node_own_the_network(const uint8_t net_key[16], const uint8_t app_key[16]) {
#if CONFIG_BLE_MESH_PROVISIONER
  if (!g_up || net_key == NULL || app_key == NULL) return false;
  memcpy(g_net_key, net_key, 16);
  memcpy(g_app_key, app_key, 16);
  // **Les dues claus entren a la xarxa d'aquesta placa**: la de xarxa és la que els nodes rebran a la provisió, i la d'aplicació és la que fa
  // que els models es puguin parlar. La pila les desa a la NVS (docs/50).
  // Índex 0 per a totes dues: les claus les posa l'amo amb `mesh key`, i el que ell decideix és el **valor**, no l'index. L'índex 0 és el
  // primari de la xarxa i és el que l'exemple d'Espressif fa servir (`ESP_BLE_MESH_KEY_PRIMARY`).
  //
  // **I si ja hi era, no és un error: és la clau que canvia** (0.190.218). La pila contesta `EEXIST` quan l'índex 0 ja té una clau amb un
  // altre valor —que és exactament el que passa quan l'amo fa `mesh key` dues vegades—, i el camí per a això és l'**actualització**, que és
  // el procediment de refresc de claus del mesh i no una escriptura a mà (docs/50).
  esp_err_t err = esp_ble_mesh_provisioner_add_local_net_key(g_net_key, kNetIndex);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "the network key is already there (err %d): it is updated, and the nodes take it in a key refresh", (int)err);
    err = esp_ble_mesh_provisioner_update_local_net_key(g_net_key, kNetIndex);
  }
  if (err != ESP_OK) return false;
  err = esp_ble_mesh_provisioner_add_local_app_key(g_app_key, kNetIndex, kAppIndex);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "the application key is already there (err %d): it is updated", (int)err);
    err = esp_ble_mesh_provisioner_update_local_app_key(g_app_key, kNetIndex, kAppIndex);
  }
  if (err != ESP_OK) return false;
  g_has_the_network = true;
  // **Amb les claus a dins, aquesta placa comença a repartir-les** (0.190.218): `mesh key` és una ordre i prou, com `wifi ssid pass`, i no
  // caldria un segon `mesh on` perquè el provisionador comenci a escoltar (docs/30).
  (void)mesh_node_offer();
  return true;
#else
  (void)net_key;
  (void)app_key;
  return false;
#endif
}

bool mesh_node_has_the_network(void) { return g_has_the_network; }

bool mesh_node_network_keys(uint8_t net_key[16], uint8_t app_key[16]) {
  if (!g_has_the_network || net_key == NULL || app_key == NULL) return false;
  memcpy(net_key, g_net_key, 16);
  memcpy(app_key, g_app_key, 16);
  return true;
}

bool mesh_node_offer(void) {
  if (!g_up) return false;
#if CONFIG_BLE_MESH_PROVISIONER
  if (!g_has_the_network) {
    ESP_LOGW(kTag, "this board has no network yet: mesh key first");
    return false;
  }
  // **El provisionador escolta els aparells que no són enlloc**, pels dos portadors, que és el que fa que un telèfon o una altra placa se li
  // puguin presentar (docs/50). I **el que contesta aquesta crida no és si ha funcionat**: l'API només diu si el missatge s'ha pogut posar a la
  // cua de la tasca `btc`, i el resultat arriba a `PROVISIONER_PROV_ENABLE_COMP_EVT`, que és on es diu (0.190.218).
  const esp_err_t err = esp_ble_mesh_provisioner_prov_enable((esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "the order to watch for devices did not reach the stack (%d)", (int)err);
    return false;
  }
  ESP_LOGI(kTag, "this board is a provisioner: it has the keys, and it is watching for devices");
  return true;
#else
  if (esp_ble_mesh_node_is_provisioned()) {
    ESP_LOGI(kTag, "this node is already in a network at 0x%04X", (unsigned)esp_ble_mesh_get_primary_element_address());
    return true;
  }
  const esp_err_t err = esp_ble_mesh_node_prov_enable((esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "this node is not offering itself (%d)", (int)err);
    return false;
  }
  ESP_LOGI(kTag, "the mesh is up and this node is waiting for a provisioner");
  return true;
#endif
}

void mesh_node_withdraw(void) {
  if (!g_up) return;
#if CONFIG_BLE_MESH_PROVISIONER
  (void)esp_ble_mesh_provisioner_prov_disable((esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
  ESP_LOGI(kTag, "this board is no longer watching for devices");
#else
  (void)esp_ble_mesh_node_prov_disable((esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
  ESP_LOGI(kTag, "this node is no longer offering itself");
#endif
}

uint8_t mesh_node_watch(uint32_t ms) {
#if CONFIG_BLE_MESH_PROVISIONER
  if (!g_up || !g_has_the_network) return 0;
  // **La llista es comença de zero** cada vegada que es mira: el que es vol és el que hi ha ara, i el que ja s'ha provisionat no hi ha de ser
  // (docs/30).
  g_heard_count = 0;
  // **Escoltar no vol dir tornar a encendre**: si el provisionador ja hi era, la pila contesta `-EALREADY` i el que es vol no és encendre'l
  // sinó esperar (0.190.218). Abans això tornava zero i `mesh find` deia que no havia sentit ningú quan el que passava és que no havia
  // escoltat gens (docs/30).
  if (esp_ble_mesh_provisioner_prov_enable((esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT)) != ESP_OK) {
    ESP_LOGI(kTag, "the provisioner was already watching, so this is only a wait");
  }
  vTaskDelay(pdMS_TO_TICKS(ms));
  return g_heard_count;
#else
  (void)ms;
  return 0;
#endif
}

uint8_t mesh_node_heard_count(void) { return g_heard_count; }

bool mesh_node_heard_at(uint8_t index, uint8_t uuid[16], uint8_t *bearer, int8_t *rssi) {
  if (index >= g_heard_count) return false;
  if (uuid != NULL) memcpy(uuid, g_heard[index].uuid, 16);
  if (bearer != NULL) *bearer = g_heard[index].bearer;
  if (rssi != NULL) *rssi = g_heard[index].rssi;
  return true;
}

#if CONFIG_BLE_MESH_PROVISIONER
// **Si una adreça ja és d'algú** (0.190.218): es mira la taula dels nodes i el que cada un ocupa, que és `unicast_addr` fins a
// `unicast_addr + element_num - 1` (docs/50). Va sota el `#if` perquè la taula de nodes d'Espressif **només existeix als builds amb
// provisionador** (`esp_ble_mesh_networking_api.c` la té dins del seu `#if CONFIG_BLE_MESH_PROVISIONER`), o sigui que en un build de només
// node això no compilaria ni enllaçaria (docs/45).
static bool address_is_taken(uint16_t address) {
  const esp_ble_mesh_node_t **table = esp_ble_mesh_provisioner_get_node_table_entry();
  if (table == NULL) return false;
  for (uint16_t i = 0; i < CONFIG_BLE_MESH_MAX_PROV_NODES; ++i) {
    if (table[i] == NULL) continue;
    const uint16_t start = table[i]->unicast_addr;
    uint16_t elements = table[i]->element_num;
    if (elements == 0) elements = 1;
    if (address >= start && address < (uint16_t)(start + elements)) return true;
  }
  return false;
}
#endif  // CONFIG_BLE_MESH_PROVISIONER

bool mesh_node_provision(uint8_t index, uint16_t address, uint16_t *given) {
#if CONFIG_BLE_MESH_PROVISIONER
  if (!g_up || !g_has_the_network || index >= g_heard_count) return false;
  uint16_t want = address;
  if (want == 0) {
    // **La propera lliure de debò, comptant quants elements ocupa cada node** (0.190.218): un node amb tres elements es menja 0x0005, 0x0006 i
    // 0x0007, i donar el 0x0006 a un altre és dues plaques amb la mateixa adreça —una xarxa trencada que no ho diu (docs/30, docs/50).
    want = FIRST_GIVEN;
    while (want < 0x8000 && (want == OWN_ADDRESS || address_is_taken(want))) {
      ++want;
    }
    if (want >= 0x8000) return false;
  }
  // **Una sola porta, i la que diu l'adreça** (0.190.218): el full d'Espressif d'aquesta funció diu, a la nota 4, que un provisionador no ha
  // de fer servir alhora `provisioner_add_unprov_dev` i `provisioner_prov_device_with_addr` —abans les cridàvem totes dues, que és demanar dues
  // provisions del mateix aparell (docs/50)—, i la nota 2 diu que el portador ha de ser exactament el que va arribar a
  // `RECV_UNPROV_ADV_PKT_EVT`, que és el que hi ha a la llista. I **l'adreça la diu aquesta placa**: 0x0005, 0x0006, …, perquè una placa
  // d'aquesta casa sàpiga sempre on és sense dependre del que un telèfon va decidir l'última vegada (docs/30, docs/50).
  if (esp_ble_mesh_provisioner_prov_device_with_addr(g_heard[index].uuid, g_heard[index].addr, (esp_ble_mesh_addr_type_t)g_heard[index].addr_type,
                                                    (esp_ble_mesh_prov_bearer_t)g_heard[index].bearer, 0, want) != ESP_OK) {
    return false;
  }
  if (given != NULL) *given = want;
  return true;
#else
  (void)index;
  (void)address;
  (void)given;
  return false;
#endif
}

uint8_t mesh_node_known(uint16_t *addresses, uint8_t cap) {
#if CONFIG_BLE_MESH_PROVISIONER
  if (addresses == NULL || cap == 0) return 0;
  const esp_ble_mesh_node_t **table = esp_ble_mesh_provisioner_get_node_table_entry();
  if (table == NULL) return 0;
  // **La taula es camina sencera i es miren els buits** (0.190.218): el full d'Espressif d'aquesta funció diu que s'ha de recórrer fins a
  // `CONFIG_BLE_MESH_MAX_PROV_NODES` comprovant si cada entrada és NULL, i no parar-se a la primera que ho sigui —un node es pot esborrar del
  // mig de la taula, i parar-s'hi amagaria els que vénen després (docs/50).
  uint8_t found = 0;
  for (uint16_t i = 0; i < CONFIG_BLE_MESH_MAX_PROV_NODES && found < cap; ++i) {
    if (table[i] != NULL) addresses[found++] = table[i]->unicast_addr;
  }
  return found;
#else
  (void)addresses;
  (void)cap;
  return 0;
#endif
}

#else  // this build has no mesh in it: the doors answer, and say they cannot

void mesh_node_set_uuid(const uint8_t mac[6]) { (void)mac; }
void mesh_node_set_name(const char *name) { (void)name; }
bool mesh_node_init(void) { return false; }
bool mesh_node_is_up(void) { return false; }
bool mesh_node_is_provisioned(void) { return false; }
uint16_t mesh_node_address(void) { return 0; }
bool mesh_node_make_keys(uint8_t net_key[16], uint8_t app_key[16]) {
  (void)net_key;
  (void)app_key;
  return false;
}
bool mesh_node_own_the_network(const uint8_t net_key[16], const uint8_t app_key[16]) {
  (void)net_key;
  (void)app_key;
  return false;
}
bool mesh_node_has_the_network(void) { return false; }
bool mesh_node_network_keys(uint8_t net_key[16], uint8_t app_key[16]) {
  (void)net_key;
  (void)app_key;
  return false;
}
uint8_t mesh_node_watch(uint32_t ms) {
  (void)ms;
  return 0;
}
uint8_t mesh_node_heard_count(void) { return 0; }
bool mesh_node_heard_at(uint8_t index, uint8_t uuid[16], uint8_t *bearer, int8_t *rssi) {
  (void)index;
  (void)uuid;
  (void)bearer;
  (void)rssi;
  return false;
}
bool mesh_node_provision(uint8_t index, uint16_t address, uint16_t *given) {
  (void)index;
  (void)address;
  (void)given;
  return false;
}
uint8_t mesh_node_known(uint16_t *addresses, uint8_t cap) {
  (void)addresses;
  (void)cap;
  return 0;
}
bool mesh_node_offer(void) { return false; }
void mesh_node_withdraw(void) {}

#endif  // CONFIG_BLE_MESH
