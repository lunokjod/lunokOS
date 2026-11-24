// **El teclat de Bluetooth d'aquest xip** (0.190.179). The stack is NimBLE and the HID profile is written here, because `esp_hid` in
// this IDF only has a Bluedroid transport and Bluedroid does not fit in a chip without PSRAM (684 KB of flash and 50-70 KB of heap,
// measured on 31/10, docs/75). What a keyboard needs is a handful of GATT characteristics, and they are all below (docs/50).
//
// **El que aquest fitxer no fa**: no connecta enlloc, no desa res de la persona i no té pàgina. És una porta per escriure caràcters
// en un altre aparell, i prou (docs/30, docs/50).
//
// **I tot el ràdio va sota el guard de la configuració** (0.190.179): les bandes que no porten el Bluetooth al seu `sdkconfig` no tenen
// ni les capçaleres de NimBLE ni el component enllaçat, i el que han de poder fer és compilar i dir que no en saben (docs/50).
//
// **I `sdkconfig.h` s'ha de demanar** (0.190.179): a una llibreria d'aquest projecte no li arriba inclòs, i el guard d'aquest fitxer en
// depèn —sense aquesta línia el fitxer compila sempre el «no en sé» i el teclat no existeix mai, encara que el ràdio sigui al binari—
// (docs/50, docs/80).
#include "sdkconfig.h"

#include "esp32/board_name.h"

#include "esp32/bluetooth.h"
#include "esp32/mesh_node.h"
#include "esp32/nvs.h"

#if defined(CONFIG_BT_ENABLED) && CONFIG_BT_ENABLED

#include <cstring>

#include "esp_bt.h"
#include "esp_mac.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "hal/log.h"

// The bond store of the host, which is what makes a pairing survive a power cycle (declared by the host's own `ble_store_config.c`).
extern "C" void ble_store_config_init(void);

#endif  // CONFIG_BT_ENABLED

namespace hal {

#if defined(CONFIG_BT_ENABLED) && CONFIG_BT_ENABLED

namespace {

const char* kTag = "BT";
// **La marca que diu si aquesta placa ha aparellat mai res** (0.190.203): un byte a la NVS que el sistema ja té, i no una lectura de la
// botiga del ràdio —que vol la pila a dalt i és exactament el que la marca estalvia—. Es desa quan una lligadura s'acaba de debò (el
// `ENC_CHANGE` sense error) i s'esborra a `bt host forget`, perquè una marca que sobreviu al vincle que descrivia torna a encendre el
// ràdio per a un teclat que ja no hi és (docs/50, docs/70).
const char kMarkNamespace[] = "bt";
const char kMarkKey[] = "paired";

bool this_board_has_paired() {
  hal::IKeyValue* store = esp32_nvs();
  if (store == nullptr) return true;  // sense magatzem no hi ha marca i el que toca és intentar-ho, com abans
  char one = 0;
  size_t len = 0;
  if (!store->get(kMarkNamespace, kMarkKey, &one, sizeof one, len)) return false;
  return len == 1 && one == '1';
}

void remember_that_it_paired() {
  hal::IKeyValue* store = esp32_nvs();
  if (store == nullptr) return;
  if (store->set(kMarkNamespace, kMarkKey, "1", 1)) store->commit();
}

void forget_that_it_paired() {
  hal::IKeyValue* store = esp32_nvs();
  if (store == nullptr) return;
  if (store->erase(kMarkNamespace, kMarkKey)) store->commit();
}


// **Un sol mapa, i és el dels dos reports** (0.191.0, l'amo: *«i no podem anunciar els dos dispositius sempre?»*): the keyboard's six-key
// report and the mouse's five bytes in one collection with report IDs 1 and 2, o sigui que el que l'amfitrió llegeix quan aparella és el
// que aquesta placa és sempre i no el que una app hagi triat després (docs/50). **Els dos mapes d'un sol report ja no hi són**: eren el
// que es publicava quan la placa triava, i una tria que es paga amb un vincle esborrat no val una col·lecció de menys.
//
// **El preu és el boot protocol**: un aparell amb report IDs no el pot servir, i el que el volia era el teclat d'un PC abans del sistema
// operatiu (docs/50).
// **El mapa de les dues coses alhora** (0.190.204, l'amo: *«una app... amb un teclat i un mouse»*): the same two collections in one
// map, and the only difference that matters is the two `Report ID` items —the keyboard is 1 and the mouse is 2—, perquè un mapa amb
// dues col·leccions d'entrada **ha** de portar IDs. És el mapa que es llegeix quan la placa és `kBoth`, i el que se'n perd és el boot
// protocol: un aparell amb IDs no el pot servir, i un mòbil o un ordinador no el demanen (el llegeixen el mapa i prou) (docs/50).
const uint8_t kBothReportMap[] = {
    0x05, 0x01,  // Usage Page (Generic Desktop)
    0x09, 0x06,  // Usage (Keyboard)
    0xA1, 0x01,  // Collection (Application)
    0x85, 0x01,  //   Report ID (1)
    0x05, 0x07,  //   Usage Page (Keyboard/Keypad)
    0x19, 0xE0,  //   Usage Minimum (Left Control)
    0x29, 0xE7,  //   Usage Maximum (Right GUI)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x01,  //   Logical Maximum (1)
    0x75, 0x01,  //   Report Size (1)
    0x95, 0x08,  //   Report Count (8)
    0x81, 0x02,  //   Input (Data,Var,Abs) — the modifiers
    0x95, 0x01,  //   Report Count (1)
    0x75, 0x08,  //   Report Size (8)
    0x81, 0x01,  //   Input (Const) — reserved
    0x95, 0x05,  //   Report Count (5)
    0x75, 0x01,  //   Report Size (1)
    0x05, 0x08,  //   Usage Page (LEDs)
    0x19, 0x01,  //   Usage Minimum (Num Lock)
    0x29, 0x05,  //   Usage Maximum (Kana)
    0x91, 0x02,  //   Output (Data,Var,Abs) — the LEDs
    0x95, 0x01,  //   Report Count (1)
    0x75, 0x03,  //   Report Size (3)
    0x91, 0x01,  //   Output (Const) — padding
    0x95, 0x06,  //   Report Count (6)
    0x75, 0x08,  //   Report Size (8)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x65,  //   Logical Maximum (101)
    0x05, 0x07,  //   Usage Page (Keyboard)
    0x19, 0x00,  //   Usage Minimum (0)
    0x29, 0x65,  //   Usage Maximum (101)
    0x81, 0x00,  //   Input (Data,Array) — the six keys
    0xC0,        // End Collection
    0x05, 0x01,  // Usage Page (Generic Desktop)
    0x09, 0x02,  // Usage (Mouse)
    0xA1, 0x01,  // Collection (Application)
    0x85, 0x02,  //   Report ID (2)
    0x09, 0x01,  //   Usage (Pointer)
    0xA1, 0x00,  //   Collection (Physical)
    0x05, 0x09,  //     Usage Page (Buttons)
    0x19, 0x01,  //     Usage Minimum (1)
    0x29, 0x03,  //     Usage Maximum (3)
    0x15, 0x00,  //     Logical Minimum (0)
    0x25, 0x01,  //     Logical Maximum (1)
    0x95, 0x03,  //     Report Count (3)
    0x75, 0x01,  //     Report Size (1)
    0x81, 0x02,  //     Input (Data,Var,Abs)
    0x95, 0x01,  //     Report Count (1)
    0x75, 0x05,  //     Report Size (5)
    0x81, 0x03,  //     Input (Cnst,Var,Abs)
    0x05, 0x01,  //     Usage Page (Generic Desktop)
    0x09, 0x30,  //     Usage (X)
    0x09, 0x31,  //     Usage (Y)
    0x09, 0x38,  //     Usage (Wheel)
    0x15, 0x81,  //     Logical Minimum (-127)
    0x25, 0x7F,  //     Logical Maximum (127)
    0x75, 0x08,  //     Report Size (8)
    0x95, 0x03,  //     Report Count (3)
    0x81, 0x06,  //     Input (Data,Var,Rel)
    0xC0,        //   End Collection
    0xC0,        // End Collection
};

// **Aquesta placa és un teclat i un ratolí, sempre i alhora** (0.191.0, l'amo: *«i no podem anunciar els dos dispositius sempre? després
// que hi hagi una app de teclat o de mouse és indiferent»*): hi havia hagut un mode que es triava i es desava, i el mode era un problema
// de l'amfitrió —un telèfon llegeix el mapa quan aparella i no el torna a llegir mai, o sigui que canviar-lo volia dir esborrar el vincle
// i tornar a aparellar—. Amb un sol mapa, el que una app faci amb la placa no canvia res del que s'ha publicat (docs/50).
//
// **El preu és el boot protocol**: un mapa amb report IDs no el pot servir, i el que el volia era el teclat d'un PC abans del sistema
// operatiu (docs/50).
const uint8_t kKeyboardReportId = 0x01;
const uint8_t kMouseReportId = 0x02;

// HID Information: version 1.11, no country code, and the two flags a keyboard has —remote wake and normally connectable—.
const uint8_t kHidInformation[] = {0x11, 0x01, 0x00, 0x03};
// Report Reference of an input report: el seu ID i el tipus 1 (input). **L'ID depèn del mode** —zero al report únic, 1 i 2 al
// compost—, o sigui que el descriptor el llegeix una funció i no una taula fixa (0.190.204, docs/50).
//
// **Si l'altre cap ha encès els reports** (0.190.208): un amfitrió es connecta i tot seguit escriu el CCCD de cada característica
// d'entrada que vol rebre, i fins que no ho fa les notificacions no arriben enlloc. Fins ara això no es mirava i la placa podia dir
// «connectat» mentre l'altre aparell encara no rebia res —que és exactament el que una persona veu com «connecta però no envia»—.
bool g_report_subscribed = false;
bool g_mouse_subscribed = false;
// **El que s'ha dit del camí d'un report, perquè no es digui a cada fotograma** (0.190.259): a mouse sends a report per finger frame, o
// sigui que una línia per enviament seria un registre il·legible i una passada de consola per fotograma. Es diu una vegada que l'altre
// cap encara no ha demanat res, i una vegada que el primer report ha sortit: són les dues frases que diuen si un ratolí funciona, i
// l'única cosa que una persona pot llegir d'aquesta banda sense un analitzador (docs/50).
bool g_said_nowhere_to_go = false;
bool g_said_one_went_out = false;

uint16_t g_report_handle = 0;
// **La segona característica de report, la del ratolí** (0.190.204): existeix sempre i el mapa del mode diu si algú l'ha de fer
// servir; engegar-la i apagar-la a cada canvi de mode voldria tornar a registrar el servei, i el servei és el mateix (docs/50).
uint16_t g_mouse_report_handle = 0;
uint16_t g_boot_handle = 0;
uint16_t g_connection = BLE_HS_CONN_HANDLE_NONE;
// **Els botons del ratolí d'aquesta placa** (0.190.193): el que està premut ara mateix, que és el que el report diu a cada moviment.
uint8_t g_mouse_buttons = 0;
// **Si el costat amfitrió s'ha demanat** (0.190.194): la ràdio pot ser engegada pel dispositiu i això no vol dir que algú escolti.
bool g_host_wanted = false;
uint8_t g_own_addr_type = 0;
uint8_t g_protocol_mode = 1;  // 1 is Report protocol, 0 is Boot protocol
bool g_started = false;
// **El nom d'aquesta placa, amb la seva adreça** (0.190.197, l'amo: *«El nom de la máquina, usa lunokOS-XXYY (els dos últims digits de la
// mac) si no ca "lunokXXYY"»*): a name that is the same on every board is a list of keyboards nobody can tell apart, o sigui que els dos
// últims bytes de l'adreça del ràdio hi van. **I si el nom no hi cap** —la resposta de l'escaneig són trenta-un bytes i el nom hi va amb
// dos de capçalera— es fa servir el curt, que sí (docs/50). Buit fins que algú el demani: el primer que encén la ràdio el construeix.
char g_name[32] = "";

void make_the_name(char* out, size_t cap) {
  // **El nom és d'un sol lloc des de la 0.190.215**: the keyboard, the mesh node and the mDNS name say the same `lunokOS-XXYY` with the same
  // address, o sigui que el que hi ha aquí és el nom de la placa i prou (docs/50).
  the_board_name(out, cap);
}

// **Els llums del teclat, que els diu l'altre aparell** (0.190.191): a HID host writes an output report with the five lights —num
// lock, caps lock, scroll lock, compose, kana— every time one of them changes, o sigui que el que aquesta casa en fa és llegir-los i
// dir-los al registre: la placa no té teclat físic, i saber que el Caps està encès és l'única cosa que en pot fer (docs/50).
uint8_t g_leds = 0;

// **El que s'envia**: vuit bytes —modificadors, un de reservat i sis tecles—, que és el mateix report sempre (docs/50).
int write_to_client(const uint8_t* bytes, size_t len, struct ble_gatt_access_ctxt* ctxt) {
  return os_mbuf_append(ctxt->om, bytes, len) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

// **Els cinc llums d'un teclat, en un byte** (0.190.191): bit 0 is num lock, 1 caps lock, 2 scroll lock, 3 compose, 4 kana. The host
// writes them; this board remembers them and says it, because a keyboard that a person cannot see cannot show them any other way.
int take_the_lights(struct ble_gatt_access_ctxt* ctxt) {
  if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR && ctxt->op != BLE_GATT_ACCESS_OP_WRITE_DSC) {
    return write_to_client(&g_leds, 1, ctxt);
  }
  uint8_t leds = 0;
  if (OS_MBUF_PKTLEN(ctxt->om) < 1 || os_mbuf_copydata(ctxt->om, 0, 1, &leds) != 0) {
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  }
  // **El report de sortida tampoc no porta l'ID** (0.191.3): the keyboard's output report is the five bits of the lights and nothing else, o
  // sigui que el que compta és el primer byte —el mateix que la característica del boot— i no el segon (docs/50).
  if (leds == g_leds) return 0;
  g_leds = leds;
  hal::logf(pal::kLogInfo, "BT", "the host set the lights of the keyboard: caps %s, num %s, scroll %s",
            (leds & 0x02) != 0 ? "on" : "off", (leds & 0x01) != 0 ? "on" : "off", (leds & 0x04) != 0 ? "on" : "off");
  return 0;
}

// **Els UUIDs del perfil, com els vol NimBLE**: a descriptor and a characteristic point at a `ble_uuid_t`, so each one is a small
// struct of its own and the callback reads which one it is from the value it is handed (docs/50).
ble_uuid16_t g_hid_information_uuid = BLE_UUID16_INIT(0x2A4A);
ble_uuid16_t g_report_map_uuid = BLE_UUID16_INIT(0x2A4B);
ble_uuid16_t g_control_point_uuid = BLE_UUID16_INIT(0x2A4C);
ble_uuid16_t g_protocol_mode_uuid = BLE_UUID16_INIT(0x2A4E);
ble_uuid16_t g_report_uuid = BLE_UUID16_INIT(0x2A4D);
ble_uuid16_t g_boot_input_uuid = BLE_UUID16_INIT(0x2A22);
ble_uuid16_t g_boot_output_uuid = BLE_UUID16_INIT(0x2A32);
ble_uuid16_t g_report_reference_uuid = BLE_UUID16_INIT(0x2908);
// **Els mateixos valors i adreces diferents** (0.190.204): the callbacks are handed the `arg` pointer and not the value, o sigui que la
// segona característica de report i el seu descriptor es distingeixen per l'adreça i no per l'UUID (docs/50).
ble_uuid16_t g_mouse_report_uuid = BLE_UUID16_INIT(0x2A4D);
ble_uuid16_t g_mouse_reference_uuid = BLE_UUID16_INIT(0x2908);
// **I la del report de sortida** (0.191.3): la característica on l'amfitrió escriu les cinc llums és un Report com qualsevol altre i el perfil
// vol que en declari la referència —l'ID i que és una sortida—, o sigui que el que hi havia (cap descriptor) era una característica que el
// perfil no sap llegir (docs/50).
ble_uuid16_t g_output_reference_uuid = BLE_UUID16_INIT(0x2908);
ble_uuid16_t g_hid_service_uuid = BLE_UUID16_INIT(0x1812);

int hid_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg) {
  (void)conn_handle;
  (void)attr_handle;
  const uint16_t uuid = reinterpret_cast<const ble_uuid16_t*>(arg)->value;
  // **Cada pregunta de l'altre cap, dita** (0.190.211): an interrupted pairing is a conversation that stops somewhere, i el registre ha de
  // dir on. És nivell `debug` perquè són poques línies i perquè el que una persona mira és el motiu de la desconnexió (docs/50, docs/95).
  hal::logf(pal::kLogDebug, "BT", "the other side asks 0x%04X (%s)", static_cast<unsigned>(uuid),
            (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) ? "write" : "read");
  switch (uuid) {
    case 0x2A4A:
      return write_to_client(kHidInformation, sizeof kHidInformation, ctxt);
    case 0x2A4B:
      // **El mapa d'aquesta placa, que és sempre el mateix** (0.191.0): the keyboard and the mouse in one collection with report IDs, o
      // sigui que el que l'amfitrió llegeix quan aparella no depèn de quina app s'obri després (docs/50).
      return write_to_client(kBothReportMap, sizeof kBothReportMap, ctxt);
    case 0x2A4E:
      if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (OS_MBUF_PKTLEN(ctxt->om) < 1) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        uint8_t mode = 0;
        if (os_mbuf_copydata(ctxt->om, 0, 1, &mode) != 0) return BLE_ATT_ERR_UNLIKELY;
        g_protocol_mode = (mode == 0) ? 0 : 1;
        return 0;
      }
      return write_to_client(&g_protocol_mode, 1, ctxt);
    case 0x2A4C:  // HID Control Point: suspend and resume are both "nothing to do here"
      return 0;
    case 0x2A32:  // Boot Keyboard Output: the five lights, written by the host
    case 0x2A4D:  // and the same thing as a report, for a host that speaks the report protocol
      return take_the_lights(ctxt);
    case 0x2908: {
      // **La referència de cada Report: el seu ID i si és d'entrada (1) o de sortida (2)** (0.191.3): les tres característiques del servei
      // són tres Reports i cadascuna diu el seu; **l'ID no viatja dins del valor** i és aquest descriptor qui el porta (docs/50).
      uint8_t reference[2] = {kKeyboardReportId, 0x01};
      if (arg == &g_mouse_reference_uuid.u) {
        reference[0] = kMouseReportId;
      } else if (arg == &g_output_reference_uuid.u) {
        reference[1] = 0x02;
      }
      return write_to_client(reference, sizeof reference, ctxt);
    }
    default:
      return BLE_ATT_ERR_UNLIKELY;
  }
}

// The input report reads as zeroes: a host that asks before subscribing gets a well-formed empty report instead of an error.
int report_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg) {
  (void)conn_handle;
  (void)attr_handle;
  // **La mida la diu el mapa, i no la característica** (0.190.211): un amfitrió llegeix el valor d'un report abans de subscriure-s'hi i el
  // compara amb el mapa; una mida que no és la de cap report fa que un de seriós tanqui l'enllaç. **I la mida no compta l'ID** (0.191.3):
  // el teclat és de vuit bytes i el ratolí de quatre, que és el que el mapa declara de debò (docs/50).
  static const uint8_t zeros[8] = {0};
  const bool mouse = (arg == &g_mouse_report_uuid.u);
  return write_to_client(zeros, mouse ? 4 : 8, ctxt);
}


struct ble_gatt_dsc_def kReportDescriptors[] = {
    {
        .uuid = &g_report_reference_uuid.u,
        .att_flags = BLE_ATT_F_READ,
        .min_key_size = 0,
        .access_cb = hid_access,
        .arg = &g_report_reference_uuid.u,
    },
    {},
};

// El descriptor de la característica del ratolí: el mateix 0x2908 i la seva pròpia adreça, que és el que diu a `hid_access` de qui és.
struct ble_gatt_dsc_def kMouseReportDescriptors[] = {
    {
        .uuid = &g_mouse_reference_uuid.u,
        .att_flags = BLE_ATT_F_READ,
        .min_key_size = 0,
        .access_cb = hid_access,
        .arg = &g_mouse_reference_uuid.u,
    },
    {},
};

// La referència del report de sortida: el mateix 0x2908 i la seva pròpia adreça, que és el que diu a `hid_access` de qui és.
struct ble_gatt_dsc_def kOutputReportDescriptors[] = {
    {
        .uuid = &g_output_reference_uuid.u,
        .att_flags = BLE_ATT_F_READ,
        .min_key_size = 0,
        .access_cb = hid_access,
        .arg = &g_output_reference_uuid.u,
    },
    {},
};

const struct ble_gatt_chr_def kHidCharacteristics[] = {
    {
        .uuid = &g_hid_information_uuid.u,
        .access_cb = hid_access,
        .arg = &g_hid_information_uuid.u,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_READ,
        .min_key_size = 0,
        .val_handle = nullptr,
    },
    {
        .uuid = &g_report_map_uuid.u,
        .access_cb = hid_access,
        .arg = &g_report_map_uuid.u,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_READ,
        .min_key_size = 0,
        .val_handle = nullptr,
    },
    {
        .uuid = &g_report_uuid.u,
        .access_cb = report_access,
        .arg = &g_report_uuid.u,
        .descriptors = kReportDescriptors,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .min_key_size = 0,
        .val_handle = &g_report_handle,
    },
    {
        // **La del ratolí** (0.190.204): the second input report, with its own handle and its own subscription. Its value is four
        // bytes in the composite map and three in the mouse-only map, i el mapa és qui diu quina de les dues coses s'hi espera.
        .uuid = &g_mouse_report_uuid.u,
        .access_cb = report_access,
        .arg = &g_mouse_report_uuid.u,
        .descriptors = kMouseReportDescriptors,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .min_key_size = 0,
        .val_handle = &g_mouse_report_handle,
    },
    {
        .uuid = &g_boot_input_uuid.u,
        .access_cb = report_access,
        .arg = &g_boot_input_uuid.u,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .min_key_size = 0,
        .val_handle = &g_boot_handle,
    },
    {
        .uuid = &g_boot_output_uuid.u,
        .access_cb = hid_access,
        .arg = &g_boot_output_uuid.u,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP,
        .min_key_size = 0,
        .val_handle = nullptr,
    },
    {
        // **El mateix UUID i una altra direcció** (0.190.191): the report of a keyboard goes in (input) and the lights go out (output),
        // i un host que parla el protocol de report els escriu aquí; la que hi havia era la del boot, i aquesta és la de debò (docs/50).
        .uuid = &g_report_uuid.u,
        .access_cb = hid_access,
        .arg = &g_report_uuid.u,
        .descriptors = kOutputReportDescriptors,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP,
        .min_key_size = 0,
        .val_handle = nullptr,
    },
    {
        .uuid = &g_control_point_uuid.u,
        .access_cb = hid_access,
        .arg = &g_control_point_uuid.u,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
        .min_key_size = 0,
        .val_handle = nullptr,
    },
    {
        .uuid = &g_protocol_mode_uuid.u,
        .access_cb = hid_access,
        .arg = &g_protocol_mode_uuid.u,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP,
        .min_key_size = 0,
        .val_handle = nullptr,
    },
    {},
};

const struct ble_gatt_svc_def kGattServices[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &g_hid_service_uuid.u,
        .includes = nullptr,
        .characteristics = kHidCharacteristics,
    },
    {},
};

void advertise();

int gap_event(struct ble_gap_event* event, void* arg) {
  (void)arg;
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      if (event->connect.status == 0) {
        g_connection = event->connect.conn_handle;
        g_said_nowhere_to_go = false;
        g_said_one_went_out = false;
        // **Amb quins paràmetres ha vingut** (0.190.211): the interval, the latency and the supervision timeout are the three numbers a
        // person never sees and that decide whether a link lives, o sigui que es diuen quan arriba i no quan ja ha marxat (docs/50).
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->connect.conn_handle, &desc) == 0) {
          hal::logf(pal::kLogInfo, "BT", "a device is connected: every %u.%02u ms, latency %u, timeout %u ms",
                    static_cast<unsigned>(desc.conn_itvl * 125 / 100 / 10),
                    static_cast<unsigned>((desc.conn_itvl * 125 / 100) % 10), static_cast<unsigned>(desc.conn_latency),
                    static_cast<unsigned>(desc.supervision_timeout * 10));
        } else {
          hal::logf(pal::kLogInfo, "BT", "a device is connected: the keyboard has someone to write to");
        }
        // **I la memòria que li queda a la ràdio, en el moment en què la necessita** (0.191.4, l'amo: `E BLE_INIT: Malloc failed` just
        // després de la lectura del mapa): el controlador demana els seus búfers d'ACL **en intern i amb DMA** quan ha de transmetre, i si
        // no n'hi ha el que es veu és una línia que no diu ni la mida ni el perquè. Aquests quatre números són els que ho diuen: el que compta
        // no és el total sinó **el bloc més gran**, i encara més el més gran que és DMA (docs/50, docs/75).
        hal::logf(pal::kLogInfo, "BT",
                  "internal memory: %u B free (%u B in the biggest block), of the DMA ones %u B free (%u B in one block)",
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)));
      } else {
        hal::logf(pal::kLogWarn, "BT", "the connection did not happen (%d)", event->connect.status);
        advertise();
      }
      return 0;
    case BLE_GAP_EVENT_DISCONNECT:
      g_connection = BLE_HS_CONN_HANDLE_NONE;
      g_report_subscribed = false;
      g_mouse_subscribed = false;
      g_said_nowhere_to_go = false;
      g_said_one_went_out = false;
      // **Per què se n'ha anat** (0.190.211): a link that falls on its own leaves a reason in the event and we were throwing it away, o sigui
      // que el registre deia «the device went away» i prou. `0x08` is a timeout, `0x13` the other side ending it, `0x3D`/`0x3E` are MIC
      // failures (a bond that does not match) i `0x16`/`0x05` are the host refusing the parameters or the security (docs/50).
      hal::logf(pal::kLogWarn, "BT", "the device went away (%d): the keyboard is being offered again",
                static_cast<int>(event->disconnect.reason));
      advertise();
      return 0;
    // **El moment en què l'altre aparell comença a escoltar** (0.190.208): the host writes the CCCD of each input report it wants,
    // and this is the only way to know which one it chose —which is what decides whether a mouse report has anywhere to go—. It is
    // said in the log because it is the line that tells a person «connected» from «connected and heard» (docs/50).
    case BLE_GAP_EVENT_SUBSCRIBE: {
      const bool on = event->subscribe.cur_notify != 0;
      if (event->subscribe.attr_handle == g_report_handle) {
        g_report_subscribed = on;
        hal::logf(pal::kLogInfo, "BT", "the other side %s the keyboard report", on ? "asked for" : "left");
      } else if (event->subscribe.attr_handle == g_mouse_report_handle) {
        g_mouse_subscribed = on;
        hal::logf(pal::kLogInfo, "BT", "the other side %s the mouse report", on ? "asked for" : "left");
      }
      // **Una subscripció nova torna a començar les dues frases**: the lines say what happened on *this* link, o sigui que el que s'ha
      // dit del vincle anterior no compta (docs/50).
      if (on) g_said_nowhere_to_go = false;
      return 0;
    }
    case BLE_GAP_EVENT_ADV_COMPLETE:
      advertise();
      return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
      // **Un aparell que torna i vol tornar-se a aparellar**: the old bond is dropped and the new one is made, which is what a person
      // expects when the pairing went wrong on the other side (docs/50).
      struct ble_gap_conn_desc desc;
      if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
        ble_store_util_delete_peer(&desc.peer_id_addr);
      }
      return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    default:
      return 0;
  }
}

void advertise() {
  if (!g_started) return;
  struct ble_hs_adv_fields fields = {};
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  // **L'aparença és el que fa que un mòbil ensenyi un teclat i no un «dispositiu desconegut»** (0x03C1): és el que diu, abans de
  // connectar, què és això. **I és la del teclat encara que aquesta placa sigui les dues coses** (0.191.0), perquè la llista d'aparences
  // del Bluetooth no té cap paraula per a «les dues» i el que fa sobretot és escriure (docs/50).
  fields.appearance = 0x03C1;
  fields.appearance_is_present = 1;
  // **I el servei HID a l'anunci**: a filtering host looks for it before it connects, and an advertisement without it is a device
  // that never shows up in the list of keyboards (docs/50).
  fields.uuids16 = &g_hid_service_uuid;
  fields.num_uuids16 = 1;
  fields.uuids16_is_complete = 1;
  int rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
    hal::logf(pal::kLogWarn, "BT", "the advertisement was not taken (%d)", rc);
    return;
  }
  struct ble_hs_adv_fields response = {};
  response.name = reinterpret_cast<const uint8_t*>(g_name);
  response.name_len = static_cast<uint8_t>(strlen(g_name));
  response.name_is_complete = 1;
  ble_gap_adv_rsp_set_fields(&response);

  struct ble_gap_adv_params params = {};
  params.conn_mode = BLE_GAP_CONN_MODE_UND;
  params.disc_mode = BLE_GAP_DISC_MODE_GEN;
  rc = ble_gap_adv_start(g_own_addr_type, nullptr, BLE_HS_FOREVER, &params, gap_event, nullptr);
  if (rc != 0 && rc != BLE_HS_EALREADY) {
    hal::logf(pal::kLogWarn, "BT", "the keyboard is not being offered (%d)", rc);
  }
}

void on_sync() {
  if (ble_hs_util_ensure_addr(0) != 0) return;
  if (ble_hs_id_infer_auto(0, &g_own_addr_type) != 0) return;
  advertise();
}

void on_reset(int reason) {
  (void)reason;
  g_connection = BLE_HS_CONN_HANDLE_NONE;
  g_started = g_started;  // the host will sync again and advertise from there
}

void host_task(void* param) {
  (void)param;
  nimble_port_run();
  nimble_port_freertos_deinit();
}

// **Un report va a una característica i no a «la del report»** (0.190.204): the keyboard's eight bytes and the mouse's three are two
// subscriptions on the host's side when the board is both, o sigui que qui envia diu per quina porta (docs/50).
bool send_report(uint16_t handle, const uint8_t* report, uint8_t len) {
  if (g_connection == BLE_HS_CONN_HANDLE_NONE || handle == 0 || report == nullptr || len == 0) return false;
  // **Ningú no rep el que no ha demanat** (0.190.211): a notification on a characteristic whose CCCD is off is a protocol violation and some
  // hosts answer it by closing the link. La subscripció la diu l'esdeveniment del CCCD, i la del report de boot no compta perquè per allà no
  // s'hi envia mai (docs/50).
  if (handle == g_report_handle && !g_report_subscribed) {
    if (!g_said_nowhere_to_go) {
      g_said_nowhere_to_go = true;
      hal::logf(pal::kLogInfo, "BT", "this board has something to send and the other side has not asked for the reports yet");
    }
    return false;
  }
  if (handle == g_mouse_report_handle && !g_mouse_subscribed) {
    if (!g_said_nowhere_to_go) {
      g_said_nowhere_to_go = true;
      hal::logf(pal::kLogInfo, "BT", "this board has something to send and the other side has not asked for the reports yet");
    }
    return false;
  }
  struct os_mbuf* om = ble_hs_mbuf_from_flat(report, len);
  if (om == nullptr) return false;
  const int rc = ble_gatts_notify_custom(g_connection, handle, om);
  if (rc == 0 && !g_said_one_went_out) {
    g_said_one_went_out = true;
    hal::logf(pal::kLogInfo, "BT", "the first report went out: %u bytes on 0x%04X", static_cast<unsigned>(len),
              static_cast<unsigned>(handle));
  }
  return rc == 0;
}


// --------------------------------------------------------------- el costat amfitrió (0.190.188)
//
// **La mateixa ràdio, l'altra direcció** (l'amo: *«Ja tenim bluetooth HID device, fem el Bluetooth HID Host»*). Aquí la placa és
// l'**amfitriona** i el teclat és d'algú altre. `esp_hid` té host, i és el de Bluedroid, que en aquest xip no hi cap; o sigui que el
// que hi ha és GATT a mà, com el device: explorar, connectar, descobrir el servei `0x1812`, demanar el **boot protocol** —que és el
// report de vuit bytes de tota la vida i estalvia el parser del report map— i subscriure-s'hi (docs/30, docs/50).
//
// **NimBLE és asíncrona i el prompt no**: the callbacks run in the host task and the person waits at the prompt, o sigui que cada
// operació té un estat i una espera amb sostre, i el que fa el callback és el que la funció llegeix quan torna. És el mateix pacte
// que `wait_quiet` té amb la consola (docs/30, docs/85).

const uint16_t kHidServiceUuid = 0x1812;
const uint16_t kReportCharUuid = 0x2A4D;
const uint16_t kProtocolCharUuid = 0x2A4E;
const uint16_t kCccdUuid = 0x2902;
const uint8_t kHostPeers = 8;
const int64_t kHostWaitUs = 20000;  // el gra de l'espera del prompt: vint mil·lisegons

struct HostState {
  uint16_t conn = BLE_HS_CONN_HANDLE_NONE;
  uint16_t svc_start = 0;
  uint16_t svc_end = 0;
  uint16_t report = 0;
  uint16_t protocol = 0;
  uint16_t cccd = 0;
  bool scanning = false;
  bool busy = false;
  bool asked_security = false;
  int status = 0;  // zero mentre tot va bé, l'error de NimBLE quan no
  uint8_t count = 0;
  IBluetooth::Peer peers[kHostPeers];
  IBluetooth::HostSink sink = nullptr;
};

HostState g_host;

bool host_lives() { return g_host.conn != BLE_HS_CONN_HANDLE_NONE || g_host.scanning; }

void host_end_the_wait(int status) {
  g_host.status = status;
  g_host.busy = false;
}

bool host_wait(uint32_t ms) {
  const int64_t end = esp_timer_get_time() + static_cast<int64_t>(ms) * 1000;
  while (esp_timer_get_time() < end) {
    if (!g_host.busy) return g_host.status == 0;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return false;
}

void host_write_the_cccd(uint16_t conn);

// **Una lligadura que demana seguretat** (0.190.188): a keyboard that wants a bond refuses the subscription until the pairing is
// done, i el que s'ha de fer és començar-la i esperar l'event —no tornar-ho a provar en bucle, que és el que fa un host que no
// acaba d'entendre el que li diuen (docs/50).
void host_ask_for_security(uint16_t conn, int status) {
  if (g_host.asked_security || status != BLE_ATT_ERR_INSUFFICIENT_AUTHEN) {
    host_end_the_wait(status);
    hal::logf(pal::kLogWarn, "BT", "the keyboard refused the subscription (%d)", status);
    return;
  }
  g_host.asked_security = true;
  const int rc = ble_gap_security_initiate(conn);
  if (rc != 0) {
    host_end_the_wait(rc);
    hal::logf(pal::kLogWarn, "BT", "the pairing did not even start (%d)", rc);
    return;
  }
  hal::logf(pal::kLogInfo, "BT", "the keyboard asks for a bond: pairing, and the subscription after it");
}

int host_after_the_cccd(uint16_t conn_handle, const struct ble_gatt_error* error,
                        struct ble_gatt_attr* attr, void* arg) {
  (void)conn_handle;
  (void)attr;
  (void)arg;
  if (error->status != 0) {
    host_ask_for_security(conn_handle, error->status);
    return 0;
  }
  host_end_the_wait(0);
  hal::logf(pal::kLogInfo, "BT", "the keyboard is on: its keys come to the console");
  return 0;
}

void host_write_the_cccd(uint16_t conn) {
  if (g_host.cccd == 0) {
    host_end_the_wait(BLE_HS_ENOTSUP);
    hal::logf(pal::kLogWarn, "BT", "this keyboard has no report subscription to ask for");
    return;
  }
  // **Notificacions i prou**: una escriptura pel report només té sentit cap a un teclat (els LEDs), i això és una altra feina.
  const uint8_t notify[2] = {0x01, 0x00};
  const int rc = ble_gattc_write_flat(conn, g_host.cccd, notify, sizeof notify, host_after_the_cccd, nullptr);
  if (rc != 0) {
    host_end_the_wait(rc);
    hal::logf(pal::kLogWarn, "BT", "the subscription did not go out (%d)", rc);
  }
}

int host_after_the_protocol(uint16_t conn_handle, const struct ble_gatt_error* error,
                            struct ble_gatt_attr* attr, void* arg) {
  (void)attr;
  (void)arg;
  if (error->status != 0) {
    // **El boot protocol és un voler, no un deure**: a keyboard that does not take it still sends reports, o sigui que es prova la
    // subscripció igualment i el que arribi es tradueix com pugui (docs/50).
    hal::logf(pal::kLogInfo, "BT", "this keyboard does not take the boot protocol (%d): its reports come as they are",
              error->status);
  }
  host_write_the_cccd(conn_handle);
  return 0;
}

void host_write_the_protocol(uint16_t conn) {
  if (g_host.protocol == 0) {
    host_write_the_cccd(conn);
    return;
  }
  const uint8_t boot = 0x00;
  const int rc = ble_gattc_write_flat(conn, g_host.protocol, &boot, 1, host_after_the_protocol, nullptr);
  if (rc != 0) {
    hal::logf(pal::kLogInfo, "BT", "the boot protocol did not go out (%d): its reports come as they are", rc);
    host_write_the_cccd(conn);
  }
}

int host_dsc_seen(uint16_t conn_handle, const struct ble_gatt_error* error, uint16_t chr_val_handle,
                  const struct ble_gatt_dsc* dsc, void* arg) {
  (void)chr_val_handle;
  (void)arg;
  if (error->status == BLE_HS_EDONE) {
    host_write_the_protocol(conn_handle);
    return 0;
  }
  if (error->status != 0 || dsc == nullptr) return 0;
  if (ble_uuid_u16(&dsc->uuid.u) == kCccdUuid) g_host.cccd = dsc->handle;
  return 0;
}

int host_chr_seen(uint16_t conn_handle, const struct ble_gatt_error* error, const struct ble_gatt_chr* chr, void* arg) {
  (void)arg;
  if (error->status == BLE_HS_EDONE) {
    if (g_host.report == 0) {
      host_end_the_wait(BLE_HS_ENOTSUP);
      hal::logf(pal::kLogWarn, "BT", "the HID service of this keyboard has no report");
      return 0;
    }
    // **El descriptor de la subscripció**: és el que diu a quin handle s'escriu el `01 00` de les notificacions, i no sempre és
    // el de després del valor (docs/50).
    const int rc = ble_gattc_disc_all_dscs(conn_handle, g_host.report, g_host.svc_end, host_dsc_seen, nullptr);
    if (rc != 0) {
      host_end_the_wait(rc);
      hal::logf(pal::kLogWarn, "BT", "the descriptors of the report did not come (%d)", rc);
    }
    return 0;
  }
  if (error->status != 0 || chr == nullptr) return 0;
  const uint16_t which = ble_uuid_u16(&chr->uuid.u);
  if (which == kReportCharUuid && g_host.report == 0) g_host.report = chr->val_handle;
  if (which == kProtocolCharUuid && g_host.protocol == 0) g_host.protocol = chr->val_handle;
  return 0;
}

int host_service_seen(uint16_t conn_handle, const struct ble_gatt_error* error, const struct ble_gatt_svc* service,
                      void* arg) {
  (void)arg;
  if (error->status == BLE_HS_EDONE) {
    if (g_host.svc_end == 0) {
      host_end_the_wait(BLE_HS_ENOTSUP);
      hal::logf(pal::kLogWarn, "BT", "this device is not a keyboard: it has no HID service");
      return 0;
    }
    const int rc = ble_gattc_disc_all_chrs(conn_handle, g_host.svc_start, g_host.svc_end, host_chr_seen, nullptr);
    if (rc != 0) {
      host_end_the_wait(rc);
      hal::logf(pal::kLogWarn, "BT", "the characteristics of the keyboard did not come (%d)", rc);
    }
    return 0;
  }
  if (error->status != 0 || service == nullptr) return 0;
  if (ble_uuid_u16(&service->uuid.u) != kHidServiceUuid) return 0;
  g_host.svc_start = service->start_handle;
  g_host.svc_end = service->end_handle;
  return 0;
}

void host_begin_the_service(uint16_t conn) {
  g_host.svc_start = 0;
  g_host.svc_end = 0;
  g_host.report = 0;
  g_host.protocol = 0;
  g_host.cccd = 0;
  const int rc = ble_gattc_disc_all_svcs(conn, host_service_seen, nullptr);
  if (rc != 0) {
    host_end_the_wait(rc);
    hal::logf(pal::kLogWarn, "BT", "the services of the keyboard did not come (%d)", rc);
  }
}

// **El que l'anunci porta** (0.190.188): the name and the service are in the advertisement and not behind a connection, o sigui que
// la llista del prompt diu qui és qui abans de lligar-se a ningú; el que no duu nom es queda amb l'adreça i prou (docs/50).
// **El nom arriba en un segon event, i això és el que aquesta funció ha de saber** (0.190.189): an advertisement carries what fits in
// thirty-one bytes and the name usually does not fit, so the scanner asks (`SCAN_REQ`) and the device answers in a **scan response**,
// which is another `BLE_GAP_EVENT_DISC` with the same address and other fields. El que hi havia descartava tota adreça ja vista, o
// sigui que la llista sortia sense cap nom. Ara la fila s'**actualitza** i el que arriba després afegeix el que faltava.
void host_keep_peer(const struct ble_gap_disc_desc* disc) {
  if (disc == nullptr) return;
  struct ble_hs_adv_fields fields = {};
  if (disc->data == nullptr || ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data) != 0) return;
  IBluetooth::Peer* peer = nullptr;
  for (uint8_t i = 0; i < g_host.count; ++i) {
    if (memcmp(g_host.peers[i].addr, disc->addr.val, 6) == 0) {
      peer = &g_host.peers[i];
      break;
    }
  }
  if (peer == nullptr) {
    if (g_host.count >= kHostPeers) return;
    peer = &g_host.peers[g_host.count++];
    memset(peer, 0, sizeof *peer);
    memcpy(peer->addr, disc->addr.val, 6);
    peer->addr_type = disc->addr.type;
  }
  // **El senyal i el que el fabricant declara** (0.190.196): the RSSI comes with every report and the appearance is what the maker says
  // this thing is —a keyboard, a phone, a television—, o sigui que són les dues dades que responen «què és això» quan no diu com es diu.
  peer->rssi = static_cast<int8_t>(disc->rssi);
  if (fields.appearance_is_present) peer->appearance = fields.appearance;
  if (peer->service == 0 && fields.num_uuids16 > 0) peer->service = ble_uuid_u16(&fields.uuids16[0].u);
  if (fields.name != nullptr && fields.name_len > 0) {
    const uint8_t room = (fields.name_len < sizeof peer->name - 1) ? fields.name_len : sizeof peer->name - 1;
    memcpy(peer->name, fields.name, room);
    peer->name[room] = '\0';
  }
  for (uint8_t i = 0; i < fields.num_uuids16; ++i) {
    if (ble_uuid_u16(&fields.uuids16[i].u) == kHidServiceUuid) peer->hid = true;
  }
}

int host_gap_event(struct ble_gap_event* event, void* arg);

void host_connect(uint8_t index, uint32_t ms) {
  (void)ms;
  if (index >= g_host.count) {
    g_host.busy = false;
    g_host.status = BLE_HS_ENOENT;
    return;
  }
  ble_addr_t addr = {};
  memcpy(addr.val, g_host.peers[index].addr, 6);
  addr.type = g_host.peers[index].addr_type;
  struct ble_gap_conn_params params = {};
  params.scan_itvl = 16;
  params.scan_window = 16;
  params.itvl_min = BLE_GAP_INITIAL_CONN_ITVL_MIN;
  params.itvl_max = BLE_GAP_INITIAL_CONN_ITVL_MAX;
  params.latency = 0;
  params.supervision_timeout = 0x01F4;  // cinc segons: el que tarda un teclat a adonar-se'n
  params.min_ce_len = 0;
  params.max_ce_len = 0;
  g_host.asked_security = false;
  g_host.status = 0;
  g_host.busy = true;
  const int rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &addr, 10000, &params, host_gap_event, nullptr);
  if (rc != 0) {
    host_end_the_wait(rc);
    hal::logf(pal::kLogWarn, "BT", "the connection did not go out (%d)", rc);
    return;
  }
}

int host_gap_event(struct ble_gap_event* event, void* arg) {
  (void)arg;
  switch (event->type) {
    case BLE_GAP_EVENT_DISC:
      host_keep_peer(&event->disc);
      return 0;
    case BLE_GAP_EVENT_DISC_COMPLETE:
      g_host.scanning = false;
      return 0;
    case BLE_GAP_EVENT_CONNECT:
      if (event->connect.status != 0) {
        host_end_the_wait(event->connect.status);
        hal::logf(pal::kLogWarn, "BT", "the keyboard did not answer (%d)", event->connect.status);
        return 0;
      }
      g_host.conn = event->connect.conn_handle;
      hal::logf(pal::kLogInfo, "BT", "a keyboard answered: looking for its HID service");
      host_begin_the_service(g_host.conn);
      return 0;
    case BLE_GAP_EVENT_DISCONNECT:
      g_host.conn = BLE_HS_CONN_HANDLE_NONE;
      g_host.report = 0;
      g_host.protocol = 0;
      g_host.cccd = 0;
      host_end_the_wait(BLE_HS_ECONTROLLER);
      hal::logf(pal::kLogInfo, "BT", "the keyboard went away");
      return 0;
    case BLE_GAP_EVENT_NOTIFY_RX: {
      if (event->notify_rx.conn_handle != g_host.conn) return 0;
      uint8_t report[8] = {};
      uint16_t len = 0;
      if (ble_hs_mbuf_to_flat(event->notify_rx.om, report, sizeof report, &len) != 0 || len == 0) return 0;
      // **El report arriba cru i amb la seva mida**: eight bytes of modifiers and keys for a keyboard, three or four for a mouse, and
      // what they mean is the kernel's business (docs/30, docs/50).
      if (g_host.sink != nullptr) g_host.sink(report, static_cast<uint8_t>(len));
      return 0;
    }
    case BLE_GAP_EVENT_ENC_CHANGE:
      // **La lligadura feta**: the subscription that the keyboard refused before the pairing is asked for now, and no other way round.
      if (event->enc_change.status == 0 && event->enc_change.conn_handle == g_host.conn) {
        hal::logf(pal::kLogInfo, "BT", "the keyboard is bonded: asking for the reports again");
        remember_that_it_paired();
        host_write_the_cccd(g_host.conn);
      }
      return 0;
    default:
      return 0;
  }
}


// **La ràdio, engegada una vegada i per als dos costats** (0.190.188): the device and the host live in the same stack, o sigui que el
// que `keyboard_start` feia ha de poder-ho demanar qualsevol dels dos; el que és del teclat —anunciar-se— es queda allà i el que és
// del telèfon —explorar, connectar— a cada operació. **El que la pila costa es diu una vegada i aquí**, que és on es paga (docs/50, docs/75).
bool g_radio_up = false;

// **L'arrencada de la ràdio, en tres passos i no en un** (0.190.217): the stack of NimBLE is one and the GATT server is one, and who
// registers its services first is who gets them served. El mesh d'Espressif registra **els seus** serveis (provisió i proxy) i crida
// `ble_gatts_start()` dins de `esp_ble_mesh_init`, o sigui que si nosaltres ja hem registrat el HID i ja hem arrencat l'amfitrió, el que
// el telèfon busca no hi és: l'app deixa anar *«Discover device services failed»*. Amb els tres passos, cada cosa entra al seu moment.
bool g_the_stack_is_up = false;
// **Quanta memòria hi havia abans de la pila**, perquè el registre digui el que costa de debò encara que l'amfitrió s'arrenqui més tard
// (0.190.217): el mesh engega la pila i la tasca en dos moments, i el número que importa és el d'abans de tots dos (docs/75).
uint32_t g_heap_before_the_stack = 0;

// 1) la pila, sense res a dins: els serveis del teclat no hi són i la tasca de l'amfitrió encara no corre.
bool the_stack_up(const char* why) {
  if (g_the_stack_is_up) return true;
  // **Un controlador que ja hi és no s'inicialitza dues vegades** (0.190.263): `esp_bt_controller_init` contesta
  // `ESP_ERR_INVALID_STATE` —259— i la línia que arriba a una persona és *«the radio did not come up (259)»*, que no diu res de
  // res. Dues coses deixen el controlador encès quan la nostra pila no hi és: **el mesh**, que corre el seu amfitrió sobre el
  // mateix controlador, i **un `nimble_port_deinit()` que va fallar a mig camí** —`esp_nimble_deinit()` torna abans d'hora quan
  // `esp_nimble_hci_deinit()` diu que no, i llavors `esp_bt_controller_disable`/`deinit` no s'arriben a fer mai—. El mesh es diu i
  // no es toca; un controlador que no és de ningú es desfà aquí i la ràdio es torna a provar, perquè una ràdio que cap crida no pot
  // encendre és pitjor que una que triga un segon més (docs/50).
  const esp_bt_controller_status_t status = esp_bt_controller_get_status();
  if (status != ESP_BT_CONTROLLER_STATUS_IDLE) {
    if (mesh_node_is_up()) {
      hal::logf(pal::kLogWarn, "BT",
                "the mesh has the radio (controller status %d), and `mesh off` does not give it back yet: %s has to wait "
                "for the next start of the board",
                static_cast<int>(status), why);
      return false;
    }
    (void)esp_bt_controller_disable();
    const esp_err_t taken_down = esp_bt_controller_deinit();
    hal::logf(pal::kLogWarn, "BT",
              "the controller of the radio was still up (status %d) and no stack of ours was using it: taken down (%d)",
              static_cast<int>(status), static_cast<int>(taken_down));
    if (taken_down != ESP_OK) {
      hal::logf(pal::kLogWarn, "BT", "the radio did not come up (%d): %s has nothing to use",
                static_cast<int>(taken_down), why);
      return false;
    }
  }
  g_heap_before_the_stack = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  const uint32_t largest_before = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  const esp_err_t err = nimble_port_init();
  if (err != ESP_OK) {
    // **`-1` no és un codi d'error: és el que `nimble_port_init` contesta sempre que el host falla** (0.190.264). Dins seu, el primer
    // que fa el host és demanar els seus blocs de treball (`os_msys_buf_alloc`), i quan un `calloc` no cap **torna `ESP_FAIL`** —el
    // mateix `-1`— o sigui que l'únic que es veu des de fora és un número que no diu res. El que sí que diu alguna cosa és la memòria que
    // hi havia en el moment de demanar-los, i per això va aquí (docs/50, docs/75).
    hal::logf(pal::kLogWarn, "BT",
              "the radio did not come up (%d): %s has nothing to use. Internal memory before the stack: %u B free, largest "
              "block %u B",
              static_cast<int>(err), why, static_cast<unsigned>(g_heap_before_the_stack),
              static_cast<unsigned>(largest_before));
    if (err == ESP_FAIL) {
      hal::logf(pal::kLogWarn, "BT",
                "the host of NimBLE could not take its pools, or its HCI callback was already taken: on a board with the extra "
                "memory, `CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL` is the setting that gives them a home");
    }
    return false;
  }
  ble_hs_cfg.reset_cb = on_reset;
  ble_hs_cfg.sync_cb = on_sync;
  // **Just Works**: a keyboard has no screen to show a number on, and a pairing that asks for one is a pairing nobody finishes (docs/50).
  ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_sc = 1;
  // **Encara que resolgui adreces**: a phone pairs with the identity key as well, and a bond that only has the encryption key is a bond a
  // phone may decide it cannot keep (0.190.211). És el que fa un teclat de debò (docs/50).
  ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_svc_gap_init();
  ble_svc_gatt_init();
  // **Si ningú no li ha posat nom, el de la placa**: the host side can bring the radio up before a keyboard is asked for, o sigui que
  // el nom s'ha de construir aquí i no només a `keyboard_start` (docs/50).
  if (g_name[0] == '\0') make_the_name(g_name, sizeof g_name);
  ble_svc_gap_device_name_set(g_name);
  ble_svc_gap_device_appearance_set(0x03C1);
  ble_store_config_init();
  g_the_stack_is_up = true;
  return true;
}

// 2) el servei del teclat, que és el que fa que aquesta placa es pugui aparellar com un HID.
bool the_keyboard_services() {
  const int rc = ble_gatts_count_cfg(kGattServices);
  if (rc != 0) {
    hal::logf(pal::kLogWarn, "BT", "the HID service was not counted (%d)", rc);
    return false;
  }
  if (ble_gatts_add_svcs(kGattServices) != 0) {
    hal::logf(pal::kLogWarn, "BT", "the HID service was not added");
    return false;
  }
  return true;
}

// 3) la tasca de l'amfitrió, i a partir d'aquí la ràdio viu de debò (l'anunci, les connexions, el sync).
void the_host_start(const char* why) {
  const uint32_t before = g_heap_before_the_stack;
  nimble_port_freertos_init(host_task);
  g_radio_up = true;
  const uint32_t after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  hal::logf(pal::kLogInfo, "BT", "the radio is up for %s: %u B of internal memory before, %u B after (%d B spent)", why,
            static_cast<unsigned>(before), static_cast<unsigned>(after), static_cast<int>(before) - static_cast<int>(after));
}

bool bring_up_the_radio(const char* why) {
  if (g_radio_up) return true;
  if (!the_stack_up(why)) return false;
  if (!the_keyboard_services()) {
    nimble_port_deinit();
    g_the_stack_is_up = false;
    return false;
  }
  the_host_start(why);
  return true;
}

}  // namespace

bool Esp32Bluetooth::can_bluetooth() const { return true; }

// **La pregunta de l'arrencada** (0.190.203): la marca, i no la botiga, perquè la botiga vol la pila a dalt.
bool Esp32Bluetooth::host_remembers() const { return this_board_has_paired(); }

bool Esp32Bluetooth::keyboard_start(const char* name) {
  if (g_started) return true;
  if (name != nullptr && name[0] != '\0') {
    strncpy(g_name, name, sizeof g_name - 1);
    g_name[sizeof g_name - 1] = '\0';
  } else if (g_name[0] == '\0') {
    make_the_name(g_name, sizeof g_name);
  }
  // **El registre diu què és aquesta placa i no el nom d'aquesta funció** (0.190.264, l'amo: *«“the keyboard” potser estem
  // confonent-nos?»*): la porta es diu `keyboard_start` des que tot això era un teclat i el registre ho deia igual en una placa que havia
  // passat a ratolí. Des de la 0.191.0 la resposta és sempre la mateixa i no depèn de cap tria (docs/50).
  if (!bring_up_the_radio("the keyboard and the mouse")) return false;
  g_started = true;
  advertise();
  hal::logf(pal::kLogInfo, "BT", "the device is up as \"%s\" (a keyboard and a mouse)", g_name);
  return true;
}

void Esp32Bluetooth::keyboard_stop() {
  if (!g_started) return;
  g_started = false;
  ble_gap_adv_stop();
  g_connection = BLE_HS_CONN_HANDLE_NONE;
  // **I la pila baixa només si no hi ha ningú a l'altra banda** (0.190.188): l'amfitrió i el device viuen a la mateixa pila, o sigui que
  // apagar-la perquè el teclat virtual s'apaga seria matar una connexió que la persona no ha demanat tancar (docs/50).
  if (host_lives() || g_host.busy) {
    hal::logf(pal::kLogInfo, "BT", "the device is down: the radio stays up for the host");
    return;
  }
  g_radio_up = false;
  // **I la marca de la pila baixa amb ella** (0.190.263): abans es quedava a cert i el següent `bt on` es pensava que la pila ja hi
  // era, o sigui que es saltava `nimble_port_init` i anava a registrar serveis en un amfitrió mort. El que es va veure a la placa és
  // la cara de l'altra banda del mateix forat: `controller init failed` (259) a la represa (docs/50).
  g_the_stack_is_up = false;
  nimble_port_stop();
  nimble_port_deinit();
  hal::logf(pal::kLogInfo, "BT", "the radio is down: %u B of internal memory are back",
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
}

bool Esp32Bluetooth::keyboard_connected() const { return g_connection != BLE_HS_CONN_HANDLE_NONE; }

// **Escoltat, que no és el mateix que connectat** (0.190.208): a report only reaches the other side once it has written the CCCD of
// the characteristic it wants, and the two can be a second apart (docs/50). **I des de la 0.191.0 es pregunten un a un**, perquè la
// placa en publica dos i l'altre cap pot encendre'n un i no l'altre: un dit que mou un punter vol saber del ratolí (docs/50, docs/30).
bool Esp32Bluetooth::reports_enabled() const { return g_report_subscribed || g_mouse_subscribed; }

bool Esp32Bluetooth::report_enabled(hal::IBluetooth::Report which) const {
  return (which == hal::IBluetooth::Report::kMouse) ? g_mouse_subscribed : g_report_subscribed;
}

// La porta que esborra els vincles, definida més avall amb `host_forget` perquè les dues bandes la comparteixin.
int forget_the_bonds();

// **Oblidar l'altre cap per la banda d'aquesta placa** (0.191.2, l'amo: *«el Mouse no va bé… només es mou verticalment i horitzontalment…
// tenia pinta d'estar en mode rodeta… i m'ha penjat el pc»*). El que s'ha vist a l'altre cap és exactament un report llegit **un byte
// desplaçat**: els botons són l'ID (2, el botó dret sempre premut), la `x` són els botons, la `y` és la `x` i la roda és la `y` —o sigui un
// punter que només camina d'un eix i una roda que es mou amb el dit—, que és el que passa quan l'amfitrió parseja els cinc bytes amb el mapa
// de **quatre** que va llegir quan aquesta placa era un ratolí sol. La placa no pot esborrar el mapa que l'altre cap té desat; el que sí que
// pot és deixar de ser el seu vincle i tancar l'enllaç, i això és el que fa aquesta porta (docs/50).
void Esp32Bluetooth::device_forget() {
  if (!g_started) return;
  // Es tanca primer, perquè el vincle que s'esborra és el d'un enllaç que encara existeix i l'altre cap ha de veure'l marxar.
  if (g_connection != BLE_HS_CONN_HANDLE_NONE) {
    ble_gap_terminate(g_connection, BLE_ERR_REM_USER_CONN_TERM);
    g_connection = BLE_HS_CONN_HANDLE_NONE;
  }
  g_report_subscribed = false;
  g_mouse_subscribed = false;
  g_said_nowhere_to_go = false;
  g_said_one_went_out = false;
  (void)forget_the_bonds();
  forget_that_it_paired();
  advertise();
  hal::logf(pal::kLogInfo, "BT", "this board forgot the other side: remove it there too, or it keeps the map it read");
}

bool Esp32Bluetooth::keyboard_tap(uint8_t mods, uint8_t usage) {
  if (!g_started) return false;
  // **EL REPORT NO PORTA L'ID** (0.191.3, l'amo: *«en quan emparello tot s'en va a la merda, em deixa el pc mig penjat»*): vuit bytes
  // —modificadors, un de reservat i sis tecles— i prou. A HID-over-GATT **el Report ID viatja al descriptor *Report Reference* de la
  // característica i no dins del valor**: qui ho diu és el mateix exemple que aquesta casa va copiar (Zephyr, `Report ID (1)` al mapa i una
  // notificació amb l'`sizeof` de la seva estructura de botons), i és el que fa que un amfitrió que llegeix vuit bytes en trobi vuit. Amb el
  // byte de més, l'amfitrió llegeix `[1, mods, 0, tecla…]` com a `[mods=1, reservat=mods, tecles=[0, tecla…]]`: **Ctrl esquerre premut per
  // sempre** i una tecla més al joc de les sis —o sigui dreceres de teclat a dojo, que és el que va deixar el PC mig penjat— (docs/50).
  uint8_t report[8] = {0};
  report[0] = mods;
  report[2] = usage;
  if (!send_report(g_report_handle, report, sizeof report)) return false;
  // **I es deixa anar la tecla**, o l'altre aparell la veu premuda per sempre: vint mil·lisegons és el que un teclat de debò hi posa
  // (docs/50).
  vTaskDelay(pdMS_TO_TICKS(12));
  const uint8_t released[8] = {0};
  send_report(g_report_handle, released, sizeof released);
  return true;
}

// ---- el costat amfitrió (0.190.188) ----

bool Esp32Bluetooth::can_host() const { return true; }

// **La ràdio per a l'amfitrió i prou** (0.190.192): the stack comes up without offering anything, o sigui que `bt host on` no fa que la
// placa sigui un teclat de ningú. **I apagar-lo baixa la ràdio només si el dispositiu també és fora**: les dues meitats comparteixen
// pila i matar-la per una banda seria tancar l'altra (docs/50).
bool Esp32Bluetooth::host_on() {
  const bool up = bring_up_the_radio("the host");
  if (up) g_host_wanted = true;
  return up;
}

// **Si algú escolta per aquest costat**: la ràdio engegada no vol dir això —el dispositiu també la pot encendre—, o sigui que el que
// compta és que s'hagi demanat i no s'hagi apagat (docs/30).
bool Esp32Bluetooth::host_is_up() const { return g_radio_up && g_host_wanted; }

void Esp32Bluetooth::host_off() {
  g_host_wanted = false;
  if (g_host.conn != BLE_HS_CONN_HANDLE_NONE) {
    ble_gap_terminate(g_host.conn, BLE_ERR_REM_USER_CONN_TERM);
    g_host.conn = BLE_HS_CONN_HANDLE_NONE;
  }
  if (g_host.scanning) {
    ble_gap_disc_cancel();
    g_host.scanning = false;
  }
  host_end_the_wait(BLE_HS_ECONTROLLER);
  if (g_started || !g_radio_up) return;
  g_radio_up = false;
  g_the_stack_is_up = false;  // la marca baixa amb la pila (0.190.263): vegeu `keyboard_stop`
  nimble_port_stop();
  nimble_port_deinit();
  hal::logf(pal::kLogInfo, "BT", "the host is down: %u B of internal memory are back",
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
}

// **Explorar i esperar**: the scan is NimBLE's and the waiting is the caller's, o sigui que aquesta funció no torna fins que el
// controlador ha acabat (el temps que li han demanat, més mig segon de marge) i el que torna és el que ha sentit (docs/50).
uint8_t Esp32Bluetooth::host_scan(uint32_t ms, Peer* out, uint8_t cap) {
  if (!bring_up_the_radio("the scan")) return 0;
  g_host_wanted = true;
  g_host.count = 0;
  g_host.scanning = true;
  struct ble_gap_disc_params params = {};
  // **Sense filtre de duplicats**: with it on, the controller answers the advertisement once and the scan response —the one that
  // carries the name— never reaches this side, o sigui que la llista sortiria sense noms (docs/50).
  params.filter_duplicates = 0;
  // **I actiu, que és el que pregunta**: a passive scanner only listens and the name never comes (docs/50).
  params.passive = 0;
  const int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, static_cast<int32_t>(ms), &params, host_gap_event, nullptr);
  if (rc != 0) {
    g_host.scanning = false;
    hal::logf(pal::kLogWarn, "BT", "the scan did not start (%d)", rc);
    return 0;
  }
  const int64_t end = esp_timer_get_time() + (static_cast<int64_t>(ms) + 500) * 1000;
  while (g_host.scanning && esp_timer_get_time() < end) vTaskDelay(pdMS_TO_TICKS(20));
  if (g_host.scanning) {
    ble_gap_disc_cancel();
    g_host.scanning = false;
  }
  const uint8_t count = (g_host.count < cap) ? g_host.count : cap;
  if (out != nullptr) {
    for (uint8_t i = 0; i < count; ++i) out[i] = g_host.peers[i];
  }
  hal::logf(pal::kLogInfo, "BT", "%u device(s) answered in %u ms", static_cast<unsigned>(g_host.count),
            static_cast<unsigned>(ms));
  return count;
}

// **Lligar-se a un dels que s'han sentit**: connect, discover the HID service, ask for the boot protocol and subscribe, i tot això
// amb el prompt esperant aquí i un sostre de vint segons. El que no es fa és tornar-ho a provar en bucle: un aparell que no contesta
// és un aparell que no contesta (docs/30, docs/50).
bool Esp32Bluetooth::host_pair(uint8_t index) {
  if (!bring_up_the_radio("the pairing")) return false;
  g_host_wanted = true;
  if (g_host.conn != BLE_HS_CONN_HANDLE_NONE) {
    ble_gap_terminate(g_host.conn, BLE_ERR_REM_USER_CONN_TERM);
    g_host.conn = BLE_HS_CONN_HANDLE_NONE;
  }
  host_connect(index, 10000);
  if (!g_host.busy) return g_host.status == 0;
  return host_wait(20000);
}

// **Tornar-hi sol** (0.190.191): the bonds live in the store, o sigui que el que s'ha de fer és preguntar-li quins aparells hi ha i
// connectar al primer. Llança el link i **no espera**: això corre a l'arrencada, i mig segon de prompt no s'hi pot gastar; si el teclat
// hi és, el seu report arribarà quan arribi (docs/50).
bool Esp32Bluetooth::host_reconnect() {
  if (!bring_up_the_radio("the keyboard that was here")) return false;
  if (g_host.conn != BLE_HS_CONN_HANDLE_NONE || g_host.busy) {
    g_host_wanted = true;
    return true;
  }
  ble_addr_t bonded[kHostPeers] = {};
  int count = 0;
  // **I si la botiga diu que no, aquest ràdio no s'ha demanat** (0.190.203): abans aquest camí sortia deixant `g_host_wanted` a cert,
  // o sigui que `bt` deia `host: on` sense que ningú ho hagués demanat. **El que no es fa és baixar la pila aquí**: hi ha una prova a
  // la placa que això peta —`Guru Meditation` a `ble_hs_start` amb `npl_freertos_callout_is_active` i `EXCVADDR 0x0`—, perquè un
  // `nimble_port_deinit()` just després de `nimble_port_freertos_init()` deixa la tasca del host corrent amb les seves estructures
  // ja alliberades, i el registre n'és el testimoni (`NimBLE: Host not enabled. Dropping the packet!` cada dos segons). El que es fa
  // és no haver-la pujada: `host_remembers()` és la pregunta que estalvia l'encesa quan aquesta placa no ha aparellat mai res, i aquí
  // el ràdio es queda a dalt i en silenci, que el baixa el `bt off` del prompt quan la pila ja ha acabat de néixer (docs/50, docs/60).
  if (ble_store_util_bonded_peers(bonded, &count, kHostPeers) != 0 || count <= 0) {
    // La marca deia que sí i la botiga que no: l'altre cap ens ha oblidat, i la marca ha de dir la veritat una altra vegada.
    forget_that_it_paired();
    return false;
  }
  g_host_wanted = true;
  g_host.count = 0;
  for (int i = 0; i < count && g_host.count < kHostPeers; ++i) {
    IBluetooth::Peer& peer = g_host.peers[g_host.count++];
    memset(&peer, 0, sizeof peer);
    memcpy(peer.addr, bonded[i].val, sizeof peer.addr);
    peer.addr_type = bonded[i].type;
  }
  hal::logf(pal::kLogInfo, "BT", "going back to the keyboard that was paired here (%d in the store)",
            static_cast<int>(g_host.count));
  host_connect(0, 10000);
  return true;
}

bool Esp32Bluetooth::host_connected() const { return g_host.conn != BLE_HS_CONN_HANDLE_NONE; }

uint8_t Esp32Bluetooth::keyboard_leds() const { return g_leds; }

const char* Esp32Bluetooth::device_name() const {
  static char made[32];
  if (g_name[0] != '\0') return g_name;
  make_the_name(made, sizeof made);
  return made;
}

// **Els vincles s'esborren per la porta que els esborra tots** (0.190.296, l'amo: *«bt dev mouse on → Guru Meditation Error … LoadProhibited»*):
// `ble_store_util_delete_all(<type>, nullptr)` **no accepta una clau nul·la** — NimBLE's `ble_store_config_delete` hands the key straight to
// `ble_store_config_find_sec`, which does `ble_addr_cmp(&key_sec->peer_addr, BLE_ADDR_ANY)` and reads address zero—, i el forat només
// s'obre quan la botiga **té** una entrada (the loop never runs on an empty store, que és per què una placa sense vincles no el va ensenyar
// mai). `ble_store_clear()` és la porta documentada: esborra els quatre tipus amb una clau a zero i torna `0`. **I com que la clau a zero
// vol dir «la primera que hi hagi», també se'n va el registre de CCCD**, que és el que fa que l'altre cap torni a subscriure's.
int forget_the_bonds() {
  int bonds = 0;
  (void)ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &bonds);
  const int rc = ble_store_clear();
  hal::logf(pal::kLogInfo, "BT", "the bonds are gone (%d entry/entries in the store, the clear says %d)", bonds, rc);
  return bonds;
}

// **Desfer-se'n**: the link, the handles and the bonds —the two ends have to forget each other, o un teclat que ja no es vol és un
// teclat que torna a trucar a cada arrencada (docs/50).
void Esp32Bluetooth::host_forget() {
  if (g_host.conn != BLE_HS_CONN_HANDLE_NONE) {
    ble_gap_terminate(g_host.conn, BLE_ERR_REM_USER_CONN_TERM);
    g_host.conn = BLE_HS_CONN_HANDLE_NONE;
  }
  g_host.count = 0;
  g_host.report = 0;
  g_host.protocol = 0;
  g_host.cccd = 0;
  // **Les dues bandes de la lligadura**: the keys the keyboard keeps for us and the record this board keeps of it, o sigui que el que
  // s'ha oblidat és el vincle i no la meitat d'un vincle (docs/50). Una porta i no tres: `forget_the_bonds()`.
  (void)forget_the_bonds();
  forget_that_it_paired();
}

void Esp32Bluetooth::host_sink(HostSink sink) { g_host.sink = sink; }

// **I la ràdio mana sobre la son pels dos costats**: a link that sleeps is not a link, o sigui que el pany del torn ha de saber que
// hi ha un teclat lligat encara que el teclat virtual d'aquesta placa estigui apagat (docs/50, docs/60).
// ---- el que aquesta placa és, que no es tria (0.191.0) ----
//
// **Ja no hi ha mode** (l'amo: *«i no podem anunciar els dos dispositius sempre? després que hi hagi una app de teclat o de mouse és
// indiferent»*): el mapa és el dels dos reports amb els seus IDs, l'aparença és sempre la del teclat i el servei és el mateix, o sigui que
// no hi ha res a canviar, res a tornar a anunciar i —el que més val— **cap vincle a esborrar**. El que abans era un mode era, de fet, un
// problema de l'amfitrió: un telèfon llegeix el mapa quan aparella i no el torna a llegir mai (docs/50).

// **El report del ratolí, i sense l'ID** (0.190.259, i sense el byte de més des de la 0.191.3): **quatre bytes** —botons, x, y i la
// roda—, que és exactament el que l'exemple `esp_hid_device` de la IDF envia amb `esp_hidd_dev_input_set(dev, 0, 0, buffer, 4)` i el que el
// mapa declara. Amb l'ID al davant l'amfitrió llegia `[2, botons, x, y, roda]` com a `[botons=2, x=botons, y=x, roda=y]`: el botó dret
// premut per sempre, un punter que només camina d'un eix i una roda que es mou amb el dit (docs/50).
static uint8_t a_mouse_report(const uint8_t* buttons_movement_wheel, uint8_t* out) {
  memcpy(out, buttons_movement_wheel, 4);
  return 4;
}

// **On va el report d'un ratolí**: a la seva característica i prou, perquè `send_report` ja deixa fora la que l'altre cap no ha encès
// (docs/50).
static bool send_a_mouse_report(const uint8_t* buttons_movement_wheel) {
  uint8_t report[4];
  const uint8_t len = a_mouse_report(buttons_movement_wheel, report);
  return send_report(g_mouse_report_handle, report, len);
}

bool Esp32Bluetooth::mouse_move(int8_t dx, int8_t dy) {
  const uint8_t body[4] = {g_mouse_buttons, static_cast<uint8_t>(dx), static_cast<uint8_t>(dy), 0};
  if (!send_a_mouse_report(body)) return false;
  // **I el repòs darrere del moviment**: a relative report is applied once, o sigui que el de zero no mou res i el que fa és deixar el
  // darrer report que l'altre cap té **quiet** —un cursor que es queda amb l'últim delta a la mà és el que fa que un dit que s'atura
  // segueixi caminant en alguns amfitrions— (docs/50).
  const uint8_t still[4] = {g_mouse_buttons, 0, 0, 0};
  (void)send_a_mouse_report(still);
  return true;
}

// **I un botó**, que es queda premut fins que es deixa anar: és el que fa un ratolí de debò.
bool Esp32Bluetooth::mouse_press(uint8_t buttons) {
  g_mouse_buttons = static_cast<uint8_t>(buttons & 0x07);
  const uint8_t body[4] = {g_mouse_buttons, 0, 0, 0};
  return send_a_mouse_report(body);
}

// **Un clic**: es prem, s'espera el que un dit hi posa i es deixa anar, o l'altre cap veu un botó premut per sempre (docs/50).
bool Esp32Bluetooth::mouse_click(uint8_t buttons) {
  if (!mouse_press(buttons)) return false;
  vTaskDelay(pdMS_TO_TICKS(30));
  return mouse_press(0);
}

uint8_t Esp32Bluetooth::mouse_buttons() const { return g_mouse_buttons; }

bool Esp32Bluetooth::wants_the_chip_awake() const { return g_started || host_lives(); }

#else  // nothing of the radio is in this build: the platform still answers, and says it cannot

bool Esp32Bluetooth::can_bluetooth() const { return false; }
// **I els dos costats de la ràdio diuen que no** (0.190.190): aquesta banda no té ni el device ni l'amfitrió, i una banda sense la
// definició d'un `override` no compila —la vtable el demana i no el troba—, o sigui que el que ha de fer una placa sense ràdio és
// contestar que no a tot i deixar el nucli dir-ho (docs/50).
bool Esp32Bluetooth::can_host() const { return false; }
uint8_t Esp32Bluetooth::host_scan(uint32_t ms, Peer* out, uint8_t cap) {
  (void)ms;
  (void)out;
  (void)cap;
  return 0;
}
bool Esp32Bluetooth::host_pair(uint8_t index) {
  (void)index;
  return false;
}
bool Esp32Bluetooth::host_connected() const { return false; }
void Esp32Bluetooth::host_forget() {}
void Esp32Bluetooth::host_sink(HostSink sink) { (void)sink; }
// **I si aquesta placa s'havia aparellat mai** (0.190.206): the mark of `bt/paired` is written by the radio, o sigui que una banda
// sense ràdio no en té cap i contesta que no; la vtable el demana i sense aquesta resposta l'enllaç trenca (docs/50).
bool Esp32Bluetooth::host_remembers() const { return false; }
bool Esp32Bluetooth::reports_enabled() const { return false; }
bool Esp32Bluetooth::host_reconnect() { return false; }
bool Esp32Bluetooth::host_on() { return false; }
void Esp32Bluetooth::host_off() {}
bool Esp32Bluetooth::host_is_up() const { return false; }
// **I el report de cada cosa, preguntat a una placa sense ràdio**: cap dels dos no hi és, i la resposta és la mateixa que la de
// `reports_enabled()` (docs/50, docs/80).
bool Esp32Bluetooth::report_enabled(hal::IBluetooth::Report which) const {
  (void)which;
  return false;
}
void Esp32Bluetooth::device_forget() {}
bool Esp32Bluetooth::mouse_move(int8_t dx, int8_t dy) {
  (void)dx;
  (void)dy;
  return false;
}
bool Esp32Bluetooth::mouse_press(uint8_t buttons) {
  (void)buttons;
  return false;
}
bool Esp32Bluetooth::mouse_click(uint8_t buttons) {
  (void)buttons;
  return false;
}
uint8_t Esp32Bluetooth::mouse_buttons() const { return 0; }
uint8_t Esp32Bluetooth::keyboard_leds() const { return 0; }
const char* Esp32Bluetooth::device_name() const { return "lunokOS"; }
bool Esp32Bluetooth::keyboard_start(const char* name) {
  (void)name;
  return false;
}
void Esp32Bluetooth::keyboard_stop() {}
bool Esp32Bluetooth::keyboard_connected() const { return false; }
bool Esp32Bluetooth::keyboard_tap(uint8_t mods, uint8_t usage) {
  (void)mods;
  (void)usage;
  return false;
}
bool Esp32Bluetooth::wants_the_chip_awake() const { return false; }

#endif  // CONFIG_BT_ENABLED

// The one instance of this band, asked for by every board of it: a board that has no radio in its build gets the object that says so
// instead of a null that every caller would have to remember (docs/50).
IBluetooth* esp32_bluetooth() {
  static Esp32Bluetooth bt;
  return &bt;
}

// **La ràdio per a una cosa que no és el teclat** (0.190.212): the mesh needs the same NimBLE host and nothing offered, o sigui que aquesta
// porta és `bring_up_the_radio` sense `keyboard_start` i sense anunciar-se com a teclat (docs/50).
#if CONFIG_BT_ENABLED
// **Per a qui porta els seus propis serveis** (0.190.217): the stack up and nothing in it, o sigui que el mesh pot registrar els seus GATT i
// arrencar el servidor sense que el servei del teclat li faci nosa. Qui crida això ha d'acabar amb `esp32_radio_start`.
bool esp32_radio_for(const char* why) { return the_stack_up(why); }

void esp32_radio_start(const char* why) {
  if (g_radio_up) return;
  the_host_start(why);
}

bool esp32_radio_is_up() { return g_radio_up; }
#else
bool esp32_radio_for(const char* why) {
  (void)why;
  return false;
}
void esp32_radio_start(const char* why) { (void)why; }
bool esp32_radio_is_up() { return false; }
#endif

}  // namespace hal
