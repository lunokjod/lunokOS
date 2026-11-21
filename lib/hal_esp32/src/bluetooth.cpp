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

#include "esp32/bluetooth.h"
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


// **El descriptor del teclat, que és el que l'altre aparell llegeix per saber què és això**: the standard six-key keyboard report with
// its modifiers, its five LEDs and eight bytes of report. It is written out because it is the one thing a host trusts, and a byte
// wrong here is a keyboard that pairs and writes nothing (docs/50).
const uint8_t kReportMap[] = {
    0x05, 0x01,  // Usage Page (Generic Desktop)
    0x09, 0x06,  // Usage (Keyboard)
    0xA1, 0x01,  // Collection (Application)
    0x05, 0x07,  //   Usage Page (Keyboard/Keypad)
    0x19, 0xE0,  //   Usage Minimum (Left Control)
    0x29, 0xE7,  //   Usage Maximum (Right GUI)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x01,  //   Logical Maximum (1)
    0x75, 0x01,  //   Report Size (1)
    0x95, 0x08,  //   Report Count (8)
    0x81, 0x02,  //   Input (Data, Variable, Absolute) — the modifiers
    0x95, 0x01,  //   Report Count (1)
    0x75, 0x08,  //   Report Size (8)
    0x81, 0x01,  //   Input (Constant) — reserved
    0x95, 0x05,  //   Report Count (5)
    0x75, 0x01,  //   Report Size (1)
    0x05, 0x08,  //   Usage Page (LEDs)
    0x19, 0x01,  //   Usage Minimum (Num Lock)
    0x29, 0x05,  //   Usage Maximum (Kana)
    0x91, 0x02,  //   Output (Data, Variable, Absolute) — the LEDs
    0x95, 0x01,  //   Report Count (1)
    0x75, 0x03,  //   Report Size (3)
    0x91, 0x01,  //   Output (Constant) — padding
    0x95, 0x06,  //   Report Count (6)
    0x75, 0x08,  //   Report Size (8)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x65,  //   Logical Maximum (101)
    0x05, 0x07,  //   Usage Page (Keyboard)
    0x19, 0x00,  //   Usage Minimum (0)
    0x29, 0x65,  //   Usage Maximum (101)
    0x81, 0x00,  //   Input (Data, Array) — the six keys
    0xC0,        // End Collection
};

// **El descriptor d'un ratolí** (0.190.193, l'amo: *«opció B»*): the standard three-button boot mouse —three bits of buttons, five of
// padding and the two relative movements—, que és el que un mòbil entén sense llegir res. **La placa és un teclat o un ratolí i no
// les dues coses alhora**: dues col·leccions en un sol servei volen report ids i renunciar al boot protocol, que és el que fa que
// aquest teclat escrigui a qualsevol lloc (docs/50).
const uint8_t kMouseReportMap[] = {
    0x05, 0x01,  // Usage Page (Generic Desktop)
    0x09, 0x02,  // Usage (Mouse)
    0xA1, 0x01,  // Collection (Application)
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
    0x15, 0x81,  //     Logical Minimum (-127)
    0x25, 0x7F,  //     Logical Maximum (127)
    0x75, 0x08,  //     Report Size (8)
    0x95, 0x02,  //     Report Count (2)
    0x81, 0x06,  //     Input (Data,Var,Rel)
    0xC0,        //   End Collection
    0xC0,        // End Collection
};

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
    0x15, 0x81,  //     Logical Minimum (-127)
    0x25, 0x7F,  //     Logical Maximum (127)
    0x75, 0x08,  //     Report Size (8)
    0x95, 0x02,  //     Report Count (2)
    0x81, 0x06,  //     Input (Data,Var,Rel)
    0xC0,        //   End Collection
    0xC0,        // End Collection
};

// **Què és aquesta placa ara mateix**: a keyboard (the default) or a mouse. El que canvia és el report map que el host llegeix, el
// report que s'envia i l'aparença de l'anunci; el servei GATT és el mateix, o sigui que el canvi no demana reinicialitzar la pila (docs/50).
using DeviceMode = hal::IBluetooth::Device;
DeviceMode g_device_mode = DeviceMode::kKeyboard;

// **Els report IDs del mode compost**: el teclat és l'1 i el ratolí el 2, i en els altres dos modes no n'hi ha cap (docs/50).
const uint8_t kKeyboardReportId = 0x01;
const uint8_t kMouseReportId = 0x02;

// HID Information: version 1.11, no country code, and the two flags a keyboard has —remote wake and normally connectable—.
const uint8_t kHidInformation[] = {0x11, 0x01, 0x00, 0x03};
// Report Reference of an input report: el seu ID i el tipus 1 (input). **L'ID depèn del mode** —zero als dos modes d'un sol report,
// 1 i 2 al compost—, o sigui que el descriptor el llegeix una funció i no una taula fixa (0.190.204, docs/50).
uint8_t report_id_of(bool mouse) {
  if (g_device_mode != DeviceMode::kBoth) return 0;
  return mouse ? kMouseReportId : kKeyboardReportId;
}

// **Si l'altre cap ha encès els reports** (0.190.208): un amfitrió es connecta i tot seguit escriu el CCCD de cada característica
// d'entrada que vol rebre, i fins que no ho fa les notificacions no arriben enlloc. Fins ara això no es mirava i la placa podia dir
// «connectat» mentre l'altre aparell encara no rebia res —que és exactament el que una persona veu com «connecta però no envia»—.
bool g_report_subscribed = false;
bool g_mouse_subscribed = false;

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
  // **L'adreça del ràdio ve de l'eFuse i no de la pila** (0.190.197): `esp_bt_dev_get_address` és de Bluedroid, i amb NimBLE el que hi
  // ha és `esp_read_mac(…, ESP_MAC_BT)`, que es pot preguntar abans que la pila s'hagi sincronitzat —que és exactament quan es necessita,
  // perquè el nom ha d'estar posat abans de l'anunci (docs/50).
  uint8_t addr[6] = {};
  if (esp_read_mac(addr, ESP_MAC_BT) != ESP_OK) {
    snprintf(out, cap, "lunokOS");
    return;
  }
  char long_name[32];
  snprintf(long_name, sizeof long_name, "lunokOS-%02X%02X", addr[4], addr[5]);
  const size_t kMostInAScanResponse = 29;
  if (strlen(long_name) <= kMostInAScanResponse) {
    snprintf(out, cap, "%s", long_name);
  } else {
    snprintf(out, cap, "lunok%02X%02X", addr[4], addr[5]);
  }
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
  // **Al mode compost el report de sortida porta l'ID al davant** (0.190.204): the keyboard's output report is `[id, leds]`, o sigui que
  // el que compta és el segon byte i el primer només hi és per dir de qui és (docs/50).
  if (g_device_mode == DeviceMode::kBoth && OS_MBUF_PKTLEN(ctxt->om) >= 2) {
    if (os_mbuf_copydata(ctxt->om, 1, 1, &leds) != 0) return BLE_ATT_ERR_UNLIKELY;
  }
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
      // **El mapa del que aquesta placa és**, i es llegeix nou a cada petició: és el que permet canviar de teclat a ratolí sense
      // reinicialitzar el servei (docs/50).
      if (g_device_mode == DeviceMode::kMouse) return write_to_client(kMouseReportMap, sizeof kMouseReportMap, ctxt);
      if (g_device_mode == DeviceMode::kBoth) return write_to_client(kBothReportMap, sizeof kBothReportMap, ctxt);
      return write_to_client(kReportMap, sizeof kReportMap, ctxt);
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
      const uint8_t reference[2] = {report_id_of(arg == &g_mouse_reference_uuid.u), 0x01};
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
  // **Cada característica té la mida del report d'ara, i no la del mode compost** (0.190.211): a keyboard-only el mapa declara vuit bytes
  // i aquesta lectura en tornava nou —l'ID del mode compost— perquè les taules de zeros eren fixes; un amfitrió que llegeix el valor abans
  // de subscriure-s'hi i el compara amb el mapa veu una mida que no és la de cap report, i un de seriós tanca l'enllaç (docs/50). La mida la
  // diu `report_id_of` —que és qui sap si el mapa porta IDs— i no una taula.
  static const uint8_t zeros[9] = {0};
  const bool both = (g_device_mode == DeviceMode::kBoth);
  const bool mouse = (arg == &g_mouse_report_uuid.u);
  uint8_t len = 8;
  if (mouse) {
    len = both ? 4 : 3;
  } else if (both) {
    len = 9;
  }
  return write_to_client(zeros, len, ctxt);
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
        .descriptors = nullptr,
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
      } else {
        hal::logf(pal::kLogWarn, "BT", "the connection did not happen (%d)", event->connect.status);
        advertise();
      }
      return 0;
    case BLE_GAP_EVENT_DISCONNECT:
      g_connection = BLE_HS_CONN_HANDLE_NONE;
      g_report_subscribed = false;
      g_mouse_subscribed = false;
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
  // **L'aparença és el que fa que un mòbil ensenyi un teclat i no un «dispositiu desconegut»** (0x03C1), i la d'un ratolí és la
  // 0x03C2: és el que diu, abans de connectar, què és això (docs/50).
  fields.appearance = (g_device_mode == DeviceMode::kMouse) ? 0x03C2 : 0x03C1;
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
  if (handle == g_report_handle && !g_report_subscribed) return false;
  if (handle == g_mouse_report_handle && !g_mouse_subscribed) return false;
  struct os_mbuf* om = ble_hs_mbuf_from_flat(report, len);
  if (om == nullptr) return false;
  const int rc = ble_gatts_notify_custom(g_connection, handle, om);
  return rc == 0;
}

bool send_report_to_every_char(const uint8_t* report, uint8_t len) {
  // **Les dues característiques d'un mode d'un sol report són el mateix report** (0.190.208), i `send_report` ja deixa fora la que l'altre cap
  // no ha encès (0.190.211): si no ha demanat res encara, res no surt —una tecla que no s'envia es pot tornar a demanar; una notificació que
  // no tocava és una connexió que es perd (docs/50).
  const bool first = send_report(g_report_handle, report, len);
  const bool second = send_report(g_mouse_report_handle, report, len);
  return first || second;
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

bool bring_up_the_radio(const char* why) {
  if (g_radio_up) return true;
  const uint32_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  const esp_err_t err = nimble_port_init();
  if (err != ESP_OK) {
    hal::logf(pal::kLogWarn, "BT", "the radio did not come up (%d): %s has nothing to use", static_cast<int>(err), why);
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
  const int rc = ble_gatts_count_cfg(kGattServices);
  if (rc != 0) {
    hal::logf(pal::kLogWarn, "BT", "the HID service was not counted (%d)", rc);
    nimble_port_deinit();
    return false;
  }
  if (ble_gatts_add_svcs(kGattServices) != 0) {
    hal::logf(pal::kLogWarn, "BT", "the HID service was not added");
    nimble_port_deinit();
    return false;
  }
  // **Si ningú no li ha posat nom, el de la placa**: the host side can bring the radio up before a keyboard is asked for, o sigui que
  // el nom s'ha de construir aquí i no només a `keyboard_start` (docs/50).
  if (g_name[0] == '\0') make_the_name(g_name, sizeof g_name);
  ble_svc_gap_device_name_set(g_name);
  ble_svc_gap_device_appearance_set(0x03C1);
  ble_store_config_init();
  nimble_port_freertos_init(host_task);
  g_radio_up = true;
  const uint32_t after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  hal::logf(pal::kLogInfo, "BT", "the radio is up for %s: %u B of internal memory before, %u B after (%d B spent)", why,
            static_cast<unsigned>(before), static_cast<unsigned>(after), static_cast<int>(before) - static_cast<int>(after));
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
  if (!bring_up_the_radio("the keyboard")) return false;
  g_started = true;
  advertise();
  hal::logf(pal::kLogInfo, "BT", "the keyboard is up as \"%s\"", g_name);
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
    hal::logf(pal::kLogInfo, "BT", "the keyboard is down: the radio stays up for the host");
    return;
  }
  g_radio_up = false;
  nimble_port_stop();
  nimble_port_deinit();
  hal::logf(pal::kLogInfo, "BT", "the radio is down: %u B of internal memory are back",
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
}

bool Esp32Bluetooth::keyboard_connected() const { return g_connection != BLE_HS_CONN_HANDLE_NONE; }

// **Escoltat, que no és el mateix que connectat** (0.190.208): a report only reaches the other side once it has written the CCCD of
// the characteristic it wants, and the two can be a second apart (docs/50).
bool Esp32Bluetooth::reports_enabled() const { return g_report_subscribed || g_mouse_subscribed; }

bool Esp32Bluetooth::keyboard_tap(uint8_t mods, uint8_t usage) {
  if (!g_started) return false;
  // **El report d'un teclat és de vuit bytes, i de nou al mode compost** (0.190.204): amb report IDs el primer byte és l'ID i els vuit
  // del teclat van darrere; sense IDs, els vuit de sempre (docs/50).
  const bool both = (g_device_mode == DeviceMode::kBoth);
  uint8_t report[9] = {0};
  uint8_t* keys = report;
  uint8_t len = 8;
  if (both) {
    report[0] = kKeyboardReportId;
    keys = report + 1;
    len = 9;
  }
  keys[0] = mods;
  keys[2] = usage;
  const bool sent = both ? send_report(g_report_handle, report, len) : send_report_to_every_char(report, len);
  if (!sent) return false;
  // **I es deixa anar la tecla**, o l'altre aparell la veu premuda per sempre: vint mil·lisegons és el que un teclat de debò hi posa
  // (docs/50).
  vTaskDelay(pdMS_TO_TICKS(12));
  uint8_t released[9] = {0};
  if (both) released[0] = kKeyboardReportId;
  if (both) {
    send_report(g_report_handle, released, len);
  } else {
    send_report_to_every_char(released, len);
  }
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
  // s'ha oblidat és el vincle i no la meitat d'un vincle (docs/50).
  const int peer = ble_store_util_delete_all(BLE_STORE_OBJ_TYPE_PEER_SEC, nullptr);
  const int record = ble_store_util_delete_all(BLE_STORE_OBJ_TYPE_PEER_DEV_REC, nullptr);
  const int ours = ble_store_util_delete_all(BLE_STORE_OBJ_TYPE_OUR_SEC, nullptr);
  forget_that_it_paired();
  hal::logf(pal::kLogInfo, "BT", "the bonds are gone (%d, %d, %d)", peer, record, ours);
}

void Esp32Bluetooth::host_sink(HostSink sink) { g_host.sink = sink; }

// **I la ràdio mana sobre la son pels dos costats**: a link that sleeps is not a link, o sigui que el pany del torn ha de saber que
// hi ha un teclat lligat encara que el teclat virtual d'aquesta placa estigui apagat (docs/50, docs/60).
// ---- el que aquesta placa és: un teclat o un ratolí (0.190.193) ----

hal::IBluetooth::Device Esp32Bluetooth::device_is() const { return g_device_mode; }

// **Canviar de teclat a ratolí, o al revés** (0.190.193, l'amo: *«opció B»*): the GATT service is the same and the report map is read
// again on every request, o sigui que el que cal és canviar el mode, tornar a anunciar-se amb l'aparença nova i no tocar res més.
// **El vincle del costat que hi havia s'ha de refer**: el host que va llegir el mapa d'un teclat no endevina el d'un ratolí (docs/50).
bool Esp32Bluetooth::device_be(DeviceMode mode) {
  if (mode != DeviceMode::kKeyboard && mode != DeviceMode::kMouse && mode != DeviceMode::kBoth) return false;
  if (g_device_mode == mode) return true;
  g_device_mode = mode;
  if (!g_started) return true;
  // **Un mapa nou no cap en un vincle vell** (0.190.211): the host read the map it found when it paired, and a bond whose HID state belongs
  // to another map is a link that pairs, encrypts and then falls —*«the device went away»* una vegada i una altra, que és exactament el que
  // es veia—. El que es fa és el que la frase ja demanava: esborrar el vincle i **tancar l'enllaç**, perquè l'altre cap torni i aparelli de
  // nou amb el mapa que hi ha. Amb un mòbil això és un segon; sense això, el bucle no s'acaba mai.
  const int peer = ble_store_util_delete_all(BLE_STORE_OBJ_TYPE_PEER_SEC, nullptr);
  const int record = ble_store_util_delete_all(BLE_STORE_OBJ_TYPE_PEER_DEV_REC, nullptr);
  const int ours = ble_store_util_delete_all(BLE_STORE_OBJ_TYPE_OUR_SEC, nullptr);
  forget_that_it_paired();
  if (g_connection != BLE_HS_CONN_HANDLE_NONE) {
    ble_gap_terminate(g_connection, BLE_ERR_REM_USER_CONN_TERM);
    hal::logf(pal::kLogInfo, "BT", "the link is closed so the other side pairs again (%d, %d, %d bonds gone)", peer, record, ours);
  }
  // **A anunciar-se de nou, amb la cara nova**: l'aparença viatja a l'anunci, i un anunci que diu «teclat» per a un ratolí és una
  // llista d'aparells que menteix. **El compost s'anuncia com un teclat** (0x03C1): el que fa sobretot és escriure, i l'aparença del
  // Bluetooth no té cap paraula per a «les dues coses» (docs/50).
  ble_gap_adv_stop();
  ble_svc_gap_device_appearance_set((mode == DeviceMode::kMouse) ? 0x03C2 : 0x03C1);
  advertise();
  hal::logf(pal::kLogInfo, "BT", "this board is now %s: pair it again from the other side",
            (mode == DeviceMode::kBoth) ? "a keyboard and a mouse"
                                        : ((mode == DeviceMode::kMouse) ? "a mouse" : "a keyboard"));
  return true;
}

// **Moure el ratolí**: the boot report is three bytes —the buttons, x and y, both relative and signed— and the light is put back to
// rest right after, o sigui que un moviment no deixa el cursor anant-se'n sol (docs/50).
// **El report del ratolí, amb l'ID al davant si el mapa en té** (0.190.204): three bytes of buttons and movement, four with the id.
static uint8_t a_mouse_report(const uint8_t* buttons_and_movement, uint8_t* out) {
  if (g_device_mode == DeviceMode::kBoth) {
    out[0] = kMouseReportId;
    memcpy(out + 1, buttons_and_movement, 3);
    return 4;
  }
  memcpy(out, buttons_and_movement, 3);
  return 3;
}

bool Esp32Bluetooth::mouse_move(int8_t dx, int8_t dy) {
  // **El ratolí hi és sol o acompanyat del teclat** (0.190.204): the two modes are the same report with a different header.
  if (g_device_mode == DeviceMode::kKeyboard) return false;
  const uint8_t body[3] = {g_mouse_buttons, static_cast<uint8_t>(dx), static_cast<uint8_t>(dy)};
  uint8_t report[4];
  const uint8_t len = a_mouse_report(body, report);
  const bool both = (g_device_mode == DeviceMode::kBoth);
  if (both ? !send_report(g_mouse_report_handle, report, len) : !send_report_to_every_char(report, len)) return false;
  const uint8_t still_body[3] = {g_mouse_buttons, 0, 0};
  uint8_t still[4];
  const uint8_t still_len = a_mouse_report(still_body, still);
  if (both) {
    send_report(g_mouse_report_handle, still, still_len);
  } else {
    send_report_to_every_char(still, still_len);
  }
  return true;
}

// **I un botó**, que es queda premut fins que es deixa anar: és el que fa un ratolí de debò.
bool Esp32Bluetooth::mouse_press(uint8_t buttons) {
  if (g_device_mode == DeviceMode::kKeyboard) return false;
  g_mouse_buttons = static_cast<uint8_t>(buttons & 0x07);
  const uint8_t body[3] = {g_mouse_buttons, 0, 0};
  uint8_t report[4];
  const uint8_t len = a_mouse_report(body, report);
  if (g_device_mode == DeviceMode::kBoth) return send_report(g_mouse_report_handle, report, len);
  return send_report_to_every_char(report, len);
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
// **The two doors of the device, answered by a board with no radio** (0.190.206): the names here had been left behind by the
// composite mode of 0.190.204 —`device_is_a_mouse` and a bare `DeviceMode`— and this branch is the one no board with the radio
// compiles, o sigui que el trencament era invisible per a totes les bandes que sí que tenen ràdio. A stub is a door like any
// other and it compiles with the same names as the real one (docs/50, docs/80).
hal::IBluetooth::Device Esp32Bluetooth::device_is() const { return hal::IBluetooth::Device::kKeyboard; }
bool Esp32Bluetooth::device_be(hal::IBluetooth::Device what) {
  (void)what;
  return false;
}
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
bool esp32_radio_for(const char* why) { return bring_up_the_radio(why); }
bool esp32_radio_is_up() { return g_radio_up; }
#else
bool esp32_radio_for(const char* why) {
  (void)why;
  return false;
}
bool esp32_radio_is_up() { return false; }
#endif

}  // namespace hal
