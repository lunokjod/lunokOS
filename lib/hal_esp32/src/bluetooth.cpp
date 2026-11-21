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

#if defined(CONFIG_BT_ENABLED) && CONFIG_BT_ENABLED

#include <cstring>

#include "esp_bt.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "host/ble_hs.h"
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

// HID Information: version 1.11, no country code, and the two flags a keyboard has —remote wake and normally connectable—.
const uint8_t kHidInformation[] = {0x11, 0x01, 0x00, 0x03};
// Report Reference of the input report: report 0, type 1 (input).
const uint8_t kReportReference[] = {0x00, 0x01};

uint16_t g_report_handle = 0;
uint16_t g_boot_handle = 0;
uint16_t g_connection = BLE_HS_CONN_HANDLE_NONE;
uint8_t g_own_addr_type = 0;
uint8_t g_protocol_mode = 1;  // 1 is Report protocol, 0 is Boot protocol
bool g_started = false;
char g_name[32] = "lunokOS";

// **El que s'envia**: vuit bytes —modificadors, un de reservat i sis tecles—, que és el mateix report sempre (docs/50).
int write_to_client(const uint8_t* bytes, size_t len, struct ble_gatt_access_ctxt* ctxt) {
  return os_mbuf_append(ctxt->om, bytes, len) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

int hid_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg) {
  (void)conn_handle;
  (void)attr_handle;
  const uint16_t uuid = reinterpret_cast<const ble_uuid16_t*>(arg)->value;
  switch (uuid) {
    case 0x2A4A:
      return write_to_client(kHidInformation, sizeof kHidInformation, ctxt);
    case 0x2A4B:
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
    case 0x2A32:  // Boot Keyboard Output: the LEDs, which this keyboard does not have
      return 0;
    case 0x2908:
      return write_to_client(kReportReference, sizeof kReportReference, ctxt);
    default:
      return BLE_ATT_ERR_UNLIKELY;
  }
}

// The input report reads as zeroes: a host that asks before subscribing gets a well-formed empty report instead of an error.
int report_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg) {
  (void)conn_handle;
  (void)attr_handle;
  (void)arg;
  static const uint8_t empty[8] = {0};
  return write_to_client(empty, sizeof empty, ctxt);
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
ble_uuid16_t g_hid_service_uuid = BLE_UUID16_INIT(0x1812);

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
        hal::logf(pal::kLogInfo, "BT", "a device is connected: the keyboard has someone to write to");
      } else {
        advertise();
      }
      return 0;
    case BLE_GAP_EVENT_DISCONNECT:
      g_connection = BLE_HS_CONN_HANDLE_NONE;
      hal::logf(pal::kLogInfo, "BT", "the device went away: the keyboard is being offered again");
      advertise();
      return 0;
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
  // **L'aparença és el que fa que un mòbil ensenyi un teclat i no un «dispositiu desconegut»** (0x03C1, docs/50).
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

bool send_report(const uint8_t report[8]) {
  if (g_connection == BLE_HS_CONN_HANDLE_NONE || g_report_handle == 0) return false;
  struct os_mbuf* om = ble_hs_mbuf_from_flat(report, 8);
  if (om == nullptr) return false;
  const int rc = ble_gatts_notify_custom(g_connection, g_report_handle, om);
  return rc == 0;
}

}  // namespace

bool Esp32Bluetooth::can_bluetooth() const { return true; }

bool Esp32Bluetooth::keyboard_start(const char* name) {
  if (g_started) return true;
  if (name != nullptr && name[0] != '\0') {
    strncpy(g_name, name, sizeof g_name - 1);
    g_name[sizeof g_name - 1] = '\0';
  }
  // **El que costava el ràdio es diu abans i després**, perquè en un xip sense PSRAM aquesta és la xifra que decideix si la funció es
  // pot quedar (docs/75, docs/95).
  const uint32_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  esp_err_t err = nimble_port_init();
  if (err != ESP_OK) {
    hal::logf(pal::kLogWarn, "BT", "the radio did not come up (%d): there is no keyboard", static_cast<int>(err));
    return false;
  }
  ble_hs_cfg.reset_cb = on_reset;
  ble_hs_cfg.sync_cb = on_sync;
  // **Just Works**: a keyboard has no screen to show a number on, and a pairing that asks for one is a pairing nobody finishes (docs/50).
  ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_sc = 1;
  ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC;
  ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC;
  ble_svc_gap_init();
  ble_svc_gatt_init();
  const int rc = ble_gatts_count_cfg(kGattServices);
  if (rc != 0) {
    hal::logf(pal::kLogWarn, "BT", "the HID service was not counted (%d)", rc);
    return false;
  }
  if (ble_gatts_add_svcs(kGattServices) != 0) {
    hal::logf(pal::kLogWarn, "BT", "the HID service was not added");
    return false;
  }
  ble_svc_gap_device_name_set(g_name);
  ble_svc_gap_device_appearance_set(0x03C1);
  ble_store_config_init();
  g_started = true;
  nimble_port_freertos_init(host_task);
  const uint32_t after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  hal::logf(pal::kLogInfo, "BT", "the keyboard is up as \"%s\": %u B of internal memory before, %u B after (%d B spent)",
            g_name, static_cast<unsigned>(before), static_cast<unsigned>(after),
            static_cast<int>(before) - static_cast<int>(after));
  return true;
}

void Esp32Bluetooth::keyboard_stop() {
  if (!g_started) return;
  g_started = false;
  g_connection = BLE_HS_CONN_HANDLE_NONE;
  nimble_port_stop();
  nimble_port_deinit();
  hal::logf(pal::kLogInfo, "BT", "the keyboard is down: %u B of internal memory are back",
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
}

bool Esp32Bluetooth::keyboard_connected() const { return g_connection != BLE_HS_CONN_HANDLE_NONE; }

bool Esp32Bluetooth::keyboard_tap(uint8_t mods, uint8_t usage) {
  if (!g_started) return false;
  uint8_t report[8] = {0};
  report[0] = mods;
  report[2] = usage;
  if (!send_report(report)) return false;
  // **I es deixa anar la tecla**, o l'altre aparell la veu premuda per sempre: vint mil·lisegons és el que un teclat de debò hi posa
  // (docs/50).
  vTaskDelay(pdMS_TO_TICKS(12));
  uint8_t released[8] = {0};
  send_report(released);
  return true;
}

bool Esp32Bluetooth::wants_the_chip_awake() const { return g_started; }

#else  // nothing of the radio is in this build: the platform still answers, and says it cannot

bool Esp32Bluetooth::can_bluetooth() const { return false; }
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

}  // namespace hal
