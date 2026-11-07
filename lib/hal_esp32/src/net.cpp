// The radio of the ESP32: association, scans, provisioning, NTP and a page.
//
// The driver is this file's and not a library's. arduino-esp32 2.0.14 answers every
// scan-done event with `WiFiScanClass::_scanDone()`, which calls
// `esp_wifi_scan_get_ap_records` and **frees the driver's list** while its count stays
// (WiFiGeneric.cpp:1041 and WiFiScan.cpp:109, read on disk). That is how a scan asked
// for here and read here answered `scan: 0 records of 23 (ESP_OK)` (measured on the
// board, 30/10), and the prebuilt provisioning manager the phone talks to reads
// **that same list**: whoever arrives second gets nothing. With the Arduino WiFi class
// out of the way, the driver's list is the system's and nobody takes it first
// (docs/50).
#include <cstdio>
#include <cstring>

#include <esp_crt_bundle.h>
#include <esp_event.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_netif.h>
#include <esp_sntp.h>
#include <esp_system.h>
#include <esp_tls_errors.h>
#include <esp_wifi.h>
#include <mbedtls/platform.h>
#include <mdns.h>
#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_ble.h>
#include <wifi_provisioning/scheme_softap.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esp32/clock.h"
#include "esp32/net.h"
#include "esp32/nvs.h"
#include "hal/hal.h"
#include "hal/log.h"
#include "pal/pal.h"

// Arduino releases the memory of the Bluetooth controller at boot **unless**
// `btInUse()` says somebody wants it (`cores/esp32/esp32-hal-misc.c`:
// `if (!btInUse()) esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);`). That function
// has **two** definitions in the core and both are weak (`return false` in
// esp32-hal-misc.c, `return true` in esp32-hal-bt.c), so which one ends up in the
// binary is a coin flip of the link order. In this build it was the one that says
// no: the memory went away and `esp_bt_controller_init` answered
// `ESP_ERR_INVALID_STATE` (measured on the board, 31/10, and checked in the
// firmware: `btInUse` is `movi.n a2, 0`). This is the hook the core documents for
// exactly this case, and it is strong on purpose.
extern "C" bool btInUse() { return true; }

namespace hal {
namespace {

// Provisioning, the same shape Espressif ships: the board opens its own access point
// (PROV_xxxxxx) and protocomm talks to the ESP SoftAP Provisioning app. The callback
// runs in the provisioning task, so it only writes a flag: the kernel reads it in its
// own turn (docs/50).
volatile ProvState g_prov_state = ProvState::kOff;
// The manager keeps one scheme for its whole life: which one is remembered so a change
// can bring it down first (docs/50).
bool g_manager_ready = false;
ProvTransport g_manager_scheme = ProvTransport::kSoftAp;
volatile bool g_scan_done = true;

// What the system itself recorded. The station config of the driver lives in a store
// that anything on this board may have written (a previous firmware, another sketch):
// believing it made the board say "stored network: MOVISTAR_B7DC" on a board nobody
// had ever provisioned (measured on the board, 30/10). What counts is what **this**
// system wrote, so it keeps its own record (docs/50).
const char kNetNamespace[] = "lunoknet";
const char kNetSsidKey[] = "ssid";
const size_t kNetSsidCap = 33;

// What is left where a FreeRTOS stack can come from. A task's stack is taken from the
// internal RAM and **never** from PSRAM, so the total free heap says nothing about
// whether the next task fits: on the Paper the provisioning manager aborted because it
// could not create its own stop task with 4 MB of "free" heap (measured on the board,
// 31/10). The number to look at is the largest internal block (docs/75).
void say_internal_ram(const char* when) {
  const uint32_t free_bytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const uint32_t largest =
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  hal::logf(pal::kLogInfo, "HEAP", "at %s: internal %u B free, largest block %u B", when,
            static_cast<unsigned>(free_bytes), static_cast<unsigned>(largest));
}

bool own_record(char* out, size_t cap) {
  hal::IKeyValue* kv = esp32_nvs();
  if (kv == nullptr || out == nullptr || cap == 0) return false;
  size_t len = 0;
  if (!kv->get(kNetNamespace, kNetSsidKey, out, cap, len)) return false;
  if (len == 0 || len >= cap) return false;
  out[len] = '\0';
  return true;
}

bool own_record_write(const char* ssid) {
  hal::IKeyValue* kv = esp32_nvs();
  if (kv == nullptr || ssid == nullptr || ssid[0] == '\0') return false;
  return kv->set(kNetNamespace, kNetSsidKey, ssid, strlen(ssid));
}

bool own_record_erase() {
  hal::IKeyValue* kv = esp32_nvs();
  return (kv != nullptr) && kv->erase(kNetNamespace, kNetSsidKey);
}

// The station: associated, addressed, or dropped. The callback runs in the system
// event task, so it writes flags and nothing else; the kernel reads them in its
// own turn (docs/50).
volatile bool g_sta_got_ip = false;
volatile bool g_sta_failed = false;
volatile int32_t g_sta_reason = 0;
// Somebody has asked to be on a network. It is what makes the station connect on
// `WIFI_EVENT_STA_START`, which is the only moment the request is not lost (docs/50).
volatile bool g_want_station = false;
// True when the drop that is coming is one we asked for: `wifi stop` prints
// `radio: stopped` and then the driver's line saying the station left, which reads
// like a fault and is not (measured on the board, 31/10).
volatile bool g_expect_drop = false;
// The connection of this attempt has already been asked. Without it the request
// went twice — once here and once on STA_START — and the driver answered the
// second one with an error in the console (`E wifi: sta is connecting`) for a
// call that was correct (measured on the board, 31/10).
volatile bool g_connect_asked = false;

esp_netif_t* g_sta_netif = nullptr;
esp_netif_t* g_ap_netif = nullptr;

// Bluetooth is the transport the maker of the chip recommends for provisioning,
// and **the application does not touch it**. `simple_ble_start`, inside the
// prebuilt protocomm, initialises the controller with this build's own mode
// (BTDM: `CONFIG_BTDM_CTRL_MODE_BTDM=y`), enables it, and brings Bluedroid up;
// the scheme releases the classic memory on INIT and frees the whole stack on
// END. Doing any of it here is being in its way, and the board said so twice:
// `ESP_ERR_INVALID_ARG` from our `enable(BLE)` against a BTDM configuration, and
// then `bt_mem_release of classic BT failed 259` followed by
// `simple_ble_start enable controller failed 259`, because the controller was
// already ours with another mode (measured on the board, 31/10). The one thing
// that has to stay is `btInUse()` above: without it Arduino frees the BTDM memory
// at boot and nothing can start afterwards.

// The scheme frees Bluetooth when provisioning ends (as the header of scheme_ble
// says to do), and this wraps it to remember that it did (docs/75).
void bt_event_cb(void* user_data, wifi_prov_cb_event_t event, void* event_data) {
  wifi_prov_scheme_ble_event_cb_free_btdm(user_data, event, event_data);
  if (event == WIFI_PROV_END || event == WIFI_PROV_DEINIT) {
    hal::logf(pal::kLogInfo, "PROV", "bluetooth freed");
  }
}

void wifi_event_cb(void* user_data, esp_event_base_t base, int32_t id, void* data) {
  (void)user_data;
  (void)base;
  switch (id) {
    case WIFI_EVENT_SCAN_DONE:
      g_scan_done = true;
      break;
    case WIFI_EVENT_STA_START:
      // A mode change restarts the driver, and a connection asked before that
      // restart is thrown away without an error and without a single event: it
      // was measured on the board (31/10), `wifi: connecting to ...` and then
      // twenty seconds of nothing. Asking here is asking at the right time.
      if (g_want_station && !g_connect_asked) {
        g_connect_asked = true;
        const esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
          hal::logf(pal::kLogWarn, "WIFI", "the radio refused to connect (%s)", esp_err_to_name(err));
        }
      }
      break;
    case WIFI_EVENT_STA_CONNECTED:
      g_sta_got_ip = false;  // associated and waiting for the address
      break;
    case WIFI_EVENT_STA_DISCONNECTED:
      if (data != nullptr) {
        g_sta_reason = static_cast<int32_t>(
            reinterpret_cast<const wifi_event_sta_disconnected_t*>(data)->reason);
      }
      g_sta_got_ip = false;
      if (g_expect_drop) {
        // Nobody to tell: we are the ones who asked for it.
        g_expect_drop = false;
        g_sta_failed = false;
        break;
      }
      g_sta_failed = true;
      // One line per drop. Without it a board that never associates looks like a
      // board that is thinking (measured on the board, 31/10).
      hal::logf(pal::kLogWarn, "WIFI", "dropped, reason %d", static_cast<int>(g_sta_reason));
      break;
    default:
      break;
  }
}

// Time from the network. The client of lwip sets the system clock itself; this
// flag is only so the kernel knows it happened (docs/60).
volatile bool g_ntp_synced = false;

void ntp_sync_cb(struct timeval* tv) {
  (void)tv;
  g_ntp_synced = true;
}

void ip_event_cb(void* user_data, esp_event_base_t base, int32_t id, void* data) {
  (void)user_data;
  (void)base;
  if (id != IP_EVENT_STA_GOT_IP || data == nullptr) return;
  const ip_event_got_ip_t* event = static_cast<const ip_event_got_ip_t*>(data);
  g_sta_got_ip = true;
  hal::logf(pal::kLogInfo, "WIFI", "address " IPSTR "\n", IP2STR(&event->ip_info.ip));
}

// The list of networks the phone sees is built by the prebuilt provisioning
// manager, which reads the driver's scan list in its **own** SCAN_DONE handler.
// This probe is registered **after** the manager is started, so in the same task
// it runs after it: what is left to read here says who took the list. Zero means
// the manager took it and the problem is on the way out to the phone; N means the
// manager never looked, and the problem is on this side. It only acts while
// provisioning is running, so it never takes a list from a scan of our own
// (docs/50).
wifi_ap_record_t g_probe_records[4];
esp_event_handler_instance_t g_probe_instance = nullptr;

void prov_scan_probe_cb(void* user_data, esp_event_base_t base, int32_t id, void* data) {
  (void)user_data;
  (void)base;
  if (id != WIFI_EVENT_SCAN_DONE || g_prov_state != ProvState::kRunning) return;
  const wifi_event_sta_scan_done_t* event =
      static_cast<const wifi_event_sta_scan_done_t*>(data);
  uint16_t seen = 0;
  if (esp_wifi_scan_get_ap_num(&seen) != ESP_OK) seen = 0;
  uint16_t read = 0;
  esp_err_t err = ESP_OK;
  if (seen > 0) {
    read = (seen > 4) ? 4 : seen;
    err = esp_wifi_scan_get_ap_records(&read, g_probe_records);
    if (err != ESP_OK) read = 0;
  }
  // What is left to read says who took the list, and the heap goes with it
  // because the reply to the phone is built with one small allocation per network
  // (docs/50).
  hal::logf(pal::kLogInfo, "PROV", "scan done (status %d, %u ap in the event, %u left for us, %s)",
            (event != nullptr) ? static_cast<int>(event->status) : -1,
            (event != nullptr) ? static_cast<unsigned>(event->number) : 0u,
            static_cast<unsigned>(read), esp_err_to_name(err));
  say_internal_ram("the end of a provisioning scan");
}

void prov_event_cb(void* user_data, wifi_prov_cb_event_t event, void* event_data) {
  (void)user_data;
  (void)event_data;
  // Every event goes out on the console: when the phone app shows an empty list
  // of networks, this is what tells us how far it got (docs/50).
  static const char* const kNames[] = {"INIT", "START", "CRED_RECV", "CRED_FAIL",
                                       "CRED_SUCCESS", "END", "DEINIT"};
  const uint32_t index = static_cast<uint32_t>(event);
  hal::logf(pal::kLogInfo, "PROV", "%s", (index < 7) ? kNames[index] : "?");
  // Every one of these is printed with what is left in the internal RAM, because the
  // manager stops provisioning by creating a task of its own on CRED_SUCCESS, and that
  // is the moment a board with too little internal RAM aborts (measured on the Paper,
  // 31/10).
  if (event == WIFI_PROV_START || event == WIFI_PROV_CRED_RECV ||
      event == WIFI_PROV_CRED_SUCCESS || event == WIFI_PROV_END) {
    say_internal_ram((index < 7) ? kNames[index] : "?");
  }
  switch (event) {
    case WIFI_PROV_START:
      g_prov_state = ProvState::kRunning;
      break;
    case WIFI_PROV_CRED_RECV:
      // The person has typed them in the app; the manager checks them next and
      // WIFI_PROV_END comes when it is over. This is where the system writes
      // **its own record**, because that is what makes the board know it has a
      // network (docs/50).
      //
      // The name comes from the event itself: asking the driver for its config
      // here reads it **before** the manager has written it, and the record came
      // out empty while the board was on the network (`network: none` with
      // `state: up`, measured on the board, 31/10).
      {
        const wifi_sta_config_t* given = static_cast<const wifi_sta_config_t*>(event_data);
        if (given != nullptr && given->ssid[0] != '\0') {
          own_record_write(reinterpret_cast<const char*>(given->ssid));
        } else {
          wifi_config_t conf;
          memset(&conf, 0, sizeof(conf));
          if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK && conf.sta.ssid[0] != '\0') {
            own_record_write(reinterpret_cast<const char*>(conf.sta.ssid));
          } else {
            hal::logf(pal::kLogWarn, "PROV", "the credentials came with no name");
          }
        }
      }
      break;
    case WIFI_PROV_CRED_FAIL:
      g_prov_state = ProvState::kFailed;
      break;
    case WIFI_PROV_END:
      // The credentials are in and the manager has nothing left to do. With the
      // access point it is given back here, which is what the examples of
      // Espressif do: a manager left alive keeps the radio in AP+STA and the
      // access point up, and the station is the one that pays for it (measured
      // on the board, 31/10: the prompt never came back). With Bluetooth the
      // scheme frees the stack itself and the manager keeps nothing alive.
      if (g_manager_scheme == ProvTransport::kSoftAp) {
        wifi_prov_mgr_deinit();
        g_manager_ready = false;
      }
      if (g_prov_state == ProvState::kRunning) g_prov_state = ProvState::kDone;
      hal::logf(pal::kLogInfo, "PROV", "the radio is ours again");
      break;
    default:
      break;
  }
}

// Brings the manager up for one scheme. The scheme is chosen by the person with the
// command (`provision` is Bluetooth, `provision softap` is the access point), and
// switching brings the old one down first because the manager keeps only one
// (docs/50).
bool manager_for(ProvTransport transport) {
  const bool ble = (transport == ProvTransport::kBle);
  if (g_manager_ready && g_manager_scheme == transport) return true;
  if (g_manager_ready) {
    wifi_prov_mgr_deinit();
    g_manager_ready = false;
  }
  wifi_prov_mgr_config_t config;
  config.scheme = ble ? wifi_prov_scheme_ble : wifi_prov_scheme_softap;
  config.scheme_event_handler.event_cb = ble ? &bt_event_cb : nullptr;
  config.scheme_event_handler.user_data = nullptr;
  config.app_event_handler.event_cb = &prov_event_cb;
  config.app_event_handler.user_data = nullptr;
  if (wifi_prov_mgr_init(config) != ESP_OK) {
    hal::logf(pal::kLogWarn, "PROV", "the manager did not init");
    return false;
  }
  g_manager_scheme = transport;
  g_manager_ready = true;
  return true;
}

// ------------------------------------------------------------------ a page
// The download runs in a task of its own, because `esp_http_client` waits for the
// server and everything else on this board has to keep turning: the prompt comes
// back and the alarms are not delayed by a page (docs/30). One job at a time,
// because the kernel has one waiter, and the handover is a few words behind a
// critical section.
// **The big blocks of a TLS session go to the extra memory of the board** (7/11, measured on the M5Paper by its owner).
// mbedTLS asks for the memory of a session through the platform hooks of the library —this core has
// `MBEDTLS_PLATFORM_MEMORY` on—, and a handshake needs **two buffers of sixteen kilobytes** plus the session: with the
// board's own RAM at **28 500 B free (26 612 in one piece)** it answered `mbedtls_ssl_setup returned -0x7F00`, which is
// `MBEDTLS_ERR_SSL_ALLOC_FAILED`, while **3 650 839 B** of extra memory sat unused beside it. A block of this many bytes
// or more comes from the extra memory and the small ones stay home, which is the rule the interpreter already follows
// (docs/75). The extra memory of this chip is not DMA, and that is fine: these buffers are walked by the processor and
// the radio has buffers of its own (docs/50, docs/85).
const size_t kTlsExtraBytes = 4096;

void* tls_calloc(size_t count, size_t size) {
  if (count == 0 || size == 0 || count > (0xFFFFFFFFu / size)) return nullptr;
  const size_t bytes = count * size;
  void* block = nullptr;
  if (bytes >= kTlsExtraBytes) block = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (block == nullptr) block = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (block == nullptr) return nullptr;
  memset(block, 0, bytes);
  return block;
}

void tls_free(void* block) {
  if (block != nullptr) heap_caps_free(block);
}

const uint32_t kHttpUrlCap = 192;
const uint32_t kHttpStack = 10240;  // the TLS handshake runs on this stack

// What the task works on. The flags live outside it so that the whole request can
// be copied in one statement and the task can go on with its own copy.
struct HttpJob {
  char url[kHttpUrlCap] = {0};
  uint32_t timeout_ms = 0;
  uint32_t max_bytes = 0;
  // **Where a page that does not fit goes**: the unit the kernel resolved and the path inside it, and
  // `saved` says the page is there and not in the heap (docs/30).
  hal::IStorage* sink = nullptr;
  char sink_path[96] = {0};
  int status = 0;
  HttpError error = HttpError::kNone;
  char* body = nullptr;
  uint32_t body_len = 0;
  bool saved = false;
};

portMUX_TYPE g_http_mux = portMUX_INITIALIZER_UNLOCKED;
HttpJob g_http;  // the job of the moment, guarded by g_http_mux
volatile uint32_t g_http_op = 0;
volatile bool g_http_finished = false;
volatile bool g_http_cancel = false;
uint32_t g_http_next_op = 1;

bool http_wanted() {
  taskENTER_CRITICAL(&g_http_mux);
  const bool wanted = !g_http_cancel;
  taskEXIT_CRITICAL(&g_http_mux);
  return wanted;
}

// Why a download failed, in the words of the HAL: everything at or above 0x8000
// comes from the TLS stack (esp_tls_errors.h), and the four that are about the
// socket are not about a certificate (docs/50).
HttpError http_error_for(esp_err_t err) {
  const int code = static_cast<int>(err);
  if (code < 0x8000) return HttpError::kConnect;
  if (code == ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME ||
      code == ESP_ERR_ESP_TLS_CANNOT_CREATE_SOCKET ||
      code == ESP_ERR_ESP_TLS_FAILED_CONNECT_TO_HOST ||
      code == ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT) {
    return HttpError::kConnect;
  }
  return HttpError::kTls;
}

// **A page goes to the extra memory of the board when there is one** (7/11, measured on the M5Paper by the owner):
// sixteen kilobytes taken from the RAM the chip keeps for itself is exactly what the TLS handshake that comes next does
// not have to spare. On the board the page was `malloc`ed from the internal memory and the handshake then failed with
// `mbedtls_ssl_setup returned -0x7F00` —`MBEDTLS_ERR_SSL_ALLOC_FAILED`, which names the allocator and says nothing
// about the page or about this line—, while the same command in the bench worked because the bench has all the memory of
// a PC. The policy is the one of the interpreter: a big block goes where there is room and the small ones stay home
// (docs/75, docs/85).
char* page_block(uint32_t bytes) {
  if (bytes == 0) return nullptr;
  return static_cast<char*>(pal::extra_memory().allocate(bytes, true));
}

void page_block_free(char* block) {
  if (block == nullptr) return;
  pal::extra_memory().release(block);
}

// Reads the page into a buffer of `max_bytes` and leaves it in the job, or —when the caller gave a
// volume and the page grows past that room— **into the file, from its first byte**, because a file
// with the end of a page is a file nobody can read (docs/30). Called in the task and nowhere else, so
// the client and the volume are only touched there.
void http_fetch(HttpJob& job) {
  char* body = nullptr;
  if (job.max_bytes > 0) {
    body = page_block(job.max_bytes + 1);
    if (body == nullptr) {
      job.error = HttpError::kNoMemory;
      return;
    }
  }
  // **The numbers before the handshake**, because a TLS session asks for some forty kilobytes of the chip's own memory
  // in pieces and a board that cannot give them answers with the allocator's error and nothing a person can act on. This
  // line is where the question "how much is there?" is answered, and the next run of the board says whether the page of
  // the block above is what was missing (docs/75, docs/85).
  const pal::HeapInfo inside = pal::system().heap();
  const hal::ExtraMemory outside = hal::platform().extra_memory();
  hal::logf(pal::kLogInfo, "HTTP", "%u B for the page, %u B of RAM free (%u in one piece), %u B of extra",
            static_cast<unsigned>(job.max_bytes), static_cast<unsigned>(inside.free_bytes),
            static_cast<unsigned>(inside.largest_free_bytes), static_cast<unsigned>(outside.free_bytes));
  // **The hooks of mbedTLS are installed before the first client of this run**, because a session that starts before them
  // would take its sixteen kilobytes from the chip's own RAM and the handshake would fail exactly as it did on the board
  // (docs/75, docs/85). Installing them twice is harmless and the job is one at a time (docs/30).
  static bool hooks_ready = false;
  if (!hooks_ready) {
    if (mbedtls_platform_set_calloc_free(&tls_calloc, &tls_free) == 0) hooks_ready = true;
  }
  esp_http_client_config_t config = {};
  config.url = job.url;
  config.timeout_ms = static_cast<int>(job.timeout_ms);
  config.buffer_size = 1024;
  config.buffer_size_tx = 512;
  config.keep_alive_enable = false;
  // The roots this build carries (CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y): a
  // certificate that is not under them is refused, which is the point of having
  // them. No certificate is written in this repository (docs/50).
  config.crt_bundle_attach = esp_crt_bundle_attach;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    page_block_free(body);
    job.error = HttpError::kConnect;
    return;
  }
  uint32_t len = 0;    // what is in the heap
  uint32_t saved = 0;  // what is in the file
  hal::FileSlot slot;
  bool file_open = false;
  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    // **And the numbers again when it fails**, because the reason it fails is almost always a block that is not there:
    // a TLS handshake asks the chip's own memory for some forty kilobytes in pieces, and `-0x7F00` is
    // `MBEDTLS_ERR_SSL_ALLOC_FAILED` said in the words of the library (docs/75, docs/85).
    const pal::HeapInfo left = pal::system().heap();
    hal::logf(pal::kLogWarn, "HTTP", "could not open %s (%s), %u B of RAM free (%u in one piece)", job.url,
              esp_err_to_name(err), static_cast<unsigned>(left.free_bytes),
              static_cast<unsigned>(left.largest_free_bytes));
    job.error = http_error_for(err);
  } else {
    esp_http_client_fetch_headers(client);
    job.status = esp_http_client_get_status_code(client);
    // **A page the volume cannot hold is not written at all**: when the page is only wanted on the
    // volume, the server says how long it is, and a card with less room than that would take the
    // beginning and fail at the end — a file that is not the page (docs/30, docs/75).
    if (job.sink != nullptr && job.max_bytes == 0) {
      const int64_t content = esp_http_client_get_content_length(client);
      uint64_t total = 0;
      uint64_t free_bytes = 0;
      if (content > 0 && job.sink->space(total, free_bytes) &&
          static_cast<uint64_t>(content) > free_bytes) {
        job.error = HttpError::kStorage;
      }
    }
    // A page that only a volume wants opens the file before the first byte: an empty page is still a
    // file and not one that was never made (docs/30).
    if (job.error == HttpError::kNone && job.max_bytes == 0 && job.sink != nullptr) {
      if (job.sink->open(job.sink_path, hal::OpenMode::kWrite, slot)) {
        file_open = true;
      } else {
        job.error = HttpError::kStorage;
      }
    }
    while (job.error == HttpError::kNone && http_wanted()) {
      if (!file_open) {
        const int got =
            esp_http_client_read(client, body + len, static_cast<int>(job.max_bytes - len));
        if (got < 0) {
          // The socket stopped talking. The cause is the driver's to say and not
          // this file's to invent, so the message carries the count and no verdict.
          hal::logf(pal::kLogWarn, "HTTP", "the page stopped after %u bytes",
                    static_cast<unsigned>(len));
          job.error = HttpError::kConnect;
          break;
        }
        if (got == 0) break;  // nothing more to read
        len += static_cast<uint32_t>(got);
        if (len < job.max_bytes) continue;
        // The room is full: the page may end exactly on the last byte, or go on, and one more byte
        // answers which. A page that goes on goes **whole** to the file, the beginning included, or
        // it is refused when there is no file to go to (docs/30).
        char extra = 0;
        const int more = esp_http_client_read(client, &extra, 1);
        if (more < 0) {
          job.error = HttpError::kConnect;
          break;
        }
        if (more == 0) break;
        if (job.sink == nullptr) {
          job.error = HttpError::kTooBig;
          break;
        }
        if (!job.sink->open(job.sink_path, hal::OpenMode::kWrite, slot)) {
          job.error = HttpError::kStorage;
          break;
        }
        file_open = true;
        if (job.sink->write(slot, body, len) != static_cast<int32_t>(len) ||
            job.sink->write(slot, &extra, 1) != 1) {
          job.error = HttpError::kStorage;
          break;
        }
        saved = len + 1;
        page_block_free(body);
        body = nullptr;
        len = 0;
      } else {
        char chunk[512];
        const int got = esp_http_client_read(client, chunk, static_cast<int>(sizeof(chunk)));
        if (got < 0) {
          hal::logf(pal::kLogWarn, "HTTP", "the page stopped after %u bytes",
                    static_cast<unsigned>(saved));
          job.error = HttpError::kConnect;
          break;
        }
        if (got == 0) break;
        if (job.sink->write(slot, chunk, static_cast<uint32_t>(got)) != got) {
          job.error = HttpError::kStorage;
          break;
        }
        saved += static_cast<uint32_t>(got);
      }
    }
    if (job.error == HttpError::kNone && !http_wanted()) job.error = HttpError::kStopped;
    if (file_open) {
      const bool closed = job.sink->close(slot);
      if (!closed && job.error == HttpError::kNone) job.error = HttpError::kStorage;
      if (job.error != HttpError::kNone) {
        // Half a page is a lie about what the server sent: the file goes with it (docs/30).
        job.sink->remove(job.sink_path);
        saved = 0;
      }
    }
  }
  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  if (!file_open) {
    if (job.error == HttpError::kNone || job.error == HttpError::kTooBig) {
      body[len] = '\0';
      job.body = body;
      job.body_len = len;
    } else {
      page_block_free(body);
    }
  } else {
    page_block_free(body);
    job.saved = (job.error == HttpError::kNone);
    job.body_len = saved;
  }
}

// The task: take the job, do it, and publish the answer. It publishes nothing when
// nobody wants it any more, and then the buffers are its own to free (docs/75).
void http_task(void* argument) {
  const uint32_t op = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(argument));
  taskENTER_CRITICAL(&g_http_mux);
  HttpJob job = g_http;
  taskEXIT_CRITICAL(&g_http_mux);

  http_fetch(job);

  bool published = false;
  taskENTER_CRITICAL(&g_http_mux);
  if (g_http_op == op && !g_http_cancel) {
    g_http = job;
    g_http_finished = true;
    published = true;
  }
  if (!published) g_http_op = 0;
  taskEXIT_CRITICAL(&g_http_mux);

  if (!published) free(job.body);
  vTaskDelete(nullptr);
}

// The radio. `esp_wifi_connect` does not wait: it starts the association and
// returns, and the state is asked from the kernel in every turn, so the prompt and
// the alarms never wait for the access point (docs/30, docs/50).
class Esp32Net : public INet {
 public:
  static bool espWifiReady() {
    static bool ready = false;
    if (ready) return true;
    const esp_err_t netif = esp_netif_init();
    if (netif != ESP_OK && netif != ESP_ERR_INVALID_STATE) return false;
    const esp_err_t loop = esp_event_loop_create_default();
    if (loop != ESP_OK && loop != ESP_ERR_INVALID_STATE) return false;
    // Both interfaces are made here, before anything asks for them: the station
    // for the network a person gives, the access point for provisioning. The
    // return of the second call is the existing one when it is already there.
    if (g_sta_netif == nullptr) g_sta_netif = esp_netif_create_default_wifi_sta();
    if (g_ap_netif == nullptr) g_ap_netif = esp_netif_create_default_wifi_ap();
    if (g_sta_netif == nullptr || g_ap_netif == nullptr) return false;
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&config) != ESP_OK) return false;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_cb, nullptr,
                                        nullptr);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_cb, nullptr,
                                        nullptr);
    // The stored network is this system's business and it keeps its own record, so
    // the driver is not asked to remember anything for us (docs/50).
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) return false;
    if (esp_wifi_start() != ESP_OK) return false;
    ready = true;
    return true;
  }

  bool ntp_start(const char* server) override {
    if (!espWifiReady()) return false;
    g_ntp_synced = false;
    // A second ask with another server is allowed: the client is stopped and
    // started again with the new name, because `esp_sntp_init` returns early when
    // it is already running and would keep the old one.
    if (esp_sntp_enabled()) esp_sntp_stop();
    const char* name = (server != nullptr && server[0] != '\0') ? server : "pool.ntp.org";
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, name);
    esp_sntp_set_time_sync_notification_cb(&ntp_sync_cb);
    esp_sntp_init();
    hal::logf(pal::kLogInfo, "NTP", "asking %s", name);
    return true;
  }

  bool ntp_synced(int64_t& epoch_ms) override {
    if (!g_ntp_synced) return false;
    epoch_ms = esp32_wall_ms();
    return true;
  }

  bool wifi_start(const char* ssid, const char* pass) override {
    if (ssid == nullptr || ssid[0] == '\0') return false;
    if (started_) return false;  // one association at a time, the kernel enforces it
    if (!espWifiReady()) return false;
    wifi_config_t conf;
    memset(&conf, 0, sizeof(conf));
    snprintf(reinterpret_cast<char*>(conf.sta.ssid), sizeof(conf.sta.ssid), "%s", ssid);
    snprintf(reinterpret_cast<char*>(conf.sta.password), sizeof(conf.sta.password), "%s",
             (pass != nullptr) ? pass : "");
    conf.sta.threshold.authmode = WIFI_AUTH_OPEN;
    g_sta_got_ip = false;
    g_sta_failed = false;
    g_sta_reason = 0;
    if (esp_wifi_set_config(WIFI_IF_STA, &conf) != ESP_OK) return false;
    if (!ask_station()) return false;
    started_ = true;
    return true;
  }

  // Asks for the station once and only once: now when the driver is already the
  // station (nothing is going to restart it), and on `WIFI_EVENT_STA_START` when
  // the mode has to change, because that restarts the driver and takes the
  // request with it (docs/50).
  static bool ask_station() {
    g_want_station = true;
    g_connect_asked = false;
    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) != ESP_OK) return false;
    if (mode != WIFI_MODE_STA) {
      if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
        g_want_station = false;
        hal::logf(pal::kLogWarn, "WIFI", "the radio did not take the station mode");
        return false;
      }
      esp_wifi_start();
      return true;  // the connection is asked on STA_START
    }
    esp_wifi_start();
    g_connect_asked = true;
    const esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
      g_want_station = false;
      hal::logf(pal::kLogWarn, "WIFI", "the radio refused to connect (%s)", esp_err_to_name(err));
      return false;
    }
    return true;
  }

  NetState wifi_state() override {
    if (!started_) return NetState::kOff;
    // Up means associated **and** addressed: a station without an address cannot
    // read anything, and saying "up" there was a lie (docs/30).
    if (g_sta_got_ip) return NetState::kUp;
    if (g_sta_failed) {
      // The reason goes to the console: a failed association that says only
      // "failed" is a failed investigation (docs/50).
      hal::logf(pal::kLogWarn, "WIFI", "association failed, reason %d", static_cast<int>(g_sta_reason));
      // The attempt is over, so the next one can start: without this the board
      // answered E_BUSY for ever after one failure (measured on the board).
      started_ = false;
      return NetState::kFailed;
    }
    return NetState::kConnecting;
  }

  bool wifi_disconnect() override {
    // It disconnects and keeps the credentials: wiping them here made a board
    // forget the network it had just been given (docs/50).
    g_want_station = false;
    g_connect_asked = false;
    g_expect_drop = true;
    esp_wifi_disconnect();
    started_ = false;
    g_sta_got_ip = false;
    g_sta_failed = false;
    return true;
  }

  // One asynchronous scan; the kernel owns the waiting and nobody blocks the
  // prompt (docs/30).
  bool wifi_scan_start() override {
    if (!espWifiReady()) return false;
    fetched_ = false;
    record_count_ = 0;
    g_scan_done = false;
    wifi_scan_config_t config;
    memset(&config, 0, sizeof(config));
    if (esp_wifi_scan_start(&config, false) != ESP_OK) {
      g_scan_done = true;
      return false;
    }
    return true;
  }

  bool wifi_scan_done() override {
    if (fetched_) return true;
    if (!g_scan_done) return false;
    // The list is taken here, once, and it is the driver's: nothing else on this
    // board reads it (docs/50).
    uint16_t seen = 0;
    if (esp_wifi_scan_get_ap_num(&seen) == ESP_OK && seen > 0) {
      uint16_t wanted = (seen > kMaxRecords) ? static_cast<uint16_t>(kMaxRecords) : seen;
      if (esp_wifi_scan_get_ap_records(&wanted, g_records) == ESP_OK) record_count_ = wanted;
    }
    fetched_ = true;
    hal::logf(pal::kLogInfo, "WIFI", "scan: %u records of %u",
              static_cast<unsigned>(record_count_), static_cast<unsigned>(seen));
    return true;
  }

  // How many networks of the last scan **can be read**: always the copy, so the
  // number a caller loops over is the number of records that answer (docs/50).
  uint32_t wifi_scan_count() override { return record_count_; }

  bool wifi_scan_record(uint32_t index, char* ssid, size_t cap, int32_t& rssi) override {
    if (ssid == nullptr || cap == 0 || index >= record_count_) return false;
    snprintf(ssid, cap, "%s", reinterpret_cast<const char*>(g_records[index].ssid));
    rssi = g_records[index].rssi;
    return true;
  }

  bool wifi_store(const char* ssid, const char* pass) override {
    if (ssid == nullptr || ssid[0] == '\0') return false;
    wifi_config_t conf;
    memset(&conf, 0, sizeof(conf));
    snprintf(reinterpret_cast<char*>(conf.sta.ssid), sizeof(conf.sta.ssid), "%s", ssid);
    snprintf(reinterpret_cast<char*>(conf.sta.password), sizeof(conf.sta.password), "%s",
             (pass != nullptr) ? pass : "");
    conf.sta.threshold.authmode = WIFI_AUTH_OPEN;
    // Stored for the next boot, which is what the person expects when they add a
    // network by hand (docs/50).
    if (esp_wifi_set_storage(WIFI_STORAGE_FLASH) != ESP_OK) return false;
    if (esp_wifi_set_config(WIFI_IF_STA, &conf) != ESP_OK) return false;
    return own_record_write(ssid);
  }

  // The network the board has stored is already in the driver, so connecting is
  // asking the driver to connect. **Not** `WiFi.begin()` with no arguments: that
  // one refuses on the spot ("SSID too long or missing", WiFiSTA.cpp) and left the
  // board answering E_NONET with a network stored (measured on the board, 30/10).
  bool wifi_start_stored() override {
    if (started_) {
      hal::logf(pal::kLogInfo, "WIFI", "an attempt of our own is still open");
      return false;
    }
    if (!espWifiReady()) return false;
    wifi_config_t conf;
    memset(&conf, 0, sizeof(conf));
    if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK) {
      hal::logf(pal::kLogWarn, "WIFI", "the driver has no station config");
      return false;
    }
    if (conf.sta.ssid[0] == '\0') {
      hal::logf(pal::kLogWarn, "WIFI", "the driver has no name stored");
      return false;
    }
    // The provisioning manager puts the station on the network itself to check
    // the credentials, so when its end arrives the board may already be there
    // with an address. Nothing is restarted then: restarting it was what dropped
    // the address the phone had just proved (measured on the board, 31/10).
    if (g_sta_got_ip && !g_sta_failed) {
      hal::logf(pal::kLogInfo, "WIFI", "already on %s", reinterpret_cast<const char*>(conf.sta.ssid));
      started_ = true;
      return true;
    }
    hal::logf(pal::kLogInfo, "WIFI", "connecting to %s", reinterpret_cast<const char*>(conf.sta.ssid));
    // A new attempt starts clean: a failure or an address from the previous one
    // would answer for this one (docs/50).
    g_sta_got_ip = false;
    g_sta_failed = false;
    g_sta_reason = 0;
    g_expect_drop = false;
    if (!ask_station()) return false;
    started_ = true;
    return true;
  }

  // Which network the board has, so a person can see what it thinks it knows.
  // The one this system recorded, not whatever is in the driver.
  bool wifi_stored_ssid(char* out, size_t cap) override {
    if (out == nullptr || cap == 0) return false;
    out[0] = '\0';
    return own_record(out, cap);
  }

  // Starts the access point and the protocomm service, and returns. Whether the
  // person gets there in time is the kernel's business (docs/30).
  bool prov_start(const char* pop, char* service_name, size_t cap,
                  ProvTransport transport) override {
    if (g_prov_state == ProvState::kRunning) return false;
    if (prov_provisioned()) return false;
    if (!espWifiReady()) return false;

    // The manager keeps one scheme for its whole life, so the scheme is chosen
    // here and a change brings the old one down first (docs/50).
    const bool ble = (transport == ProvTransport::kBle);
    if (!manager_for(transport)) return false;

    // The provisioning service announces itself with mDNS: the phone app finds
    // /proto-ver, /prov-session, /prov-config and /prov-scan there. Without mDNS
    // the scheme only writes a warning, the phone joins the access point and
    // finds nothing: an empty list of networks and a refresh that does nothing
    // (read in wifi_providing/scheme_softap.c, 30/10).
    // The name the phone has to look for. It is this board's, from its own MAC, and
    // it is written back so the QR says the same thing the radio announces: a QR
    // with a name nobody advertises is a QR that does not work (docs/50).
    if (service_name == nullptr || cap == 0) return false;
    if (service_name[0] == '\0') {
      uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
      esp_wifi_get_mac(WIFI_IF_STA, mac);
      snprintf(service_name, cap, "PROV_%02X%02X%02X", mac[3], mac[4], mac[5]);
    }

    g_prov_state = ProvState::kOff;
    // There is no `esp_log_level_set` here on purpose: the components of the
    // framework are built with `CONFIG_LOG_MAXIMUM_LEVEL 1` (errors only), so
    // their LOGI/LOGD calls do not exist in the binary and asking for them is
    // asking for nothing (docs/50).

    // Security 1 is the scheme the Android app knows as "proof of possession":
    // the person types the POP in the app, and without it nobody provisions the
    // board, even on an open access point (docs/50).
    const char* proof = (pop != nullptr && pop[0] != '\0') ? pop : "lunokos";
    if (wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, proof, service_name, nullptr) !=
        ESP_OK) {
      return false;
    }

    // mDNS is how the phone finds the service **over the access point**, and it
    // is announced with the access point already up: announcing it before leaves
    // the name on an interface that does not exist yet. Over Bluetooth the phone
    // finds the board by its advertisement, and mDNS would be a name nobody looks
    // for (docs/50).
    if (!ble) {
      if (mdns_init() != ESP_OK) {
        hal::logf(pal::kLogWarn, "PROV", "mdns did not start");
      } else {
        mdns_hostname_set("lunokos");
        hal::logf(pal::kLogInfo, "PROV", "mdns up");
      }
    }

    // The probe goes in after the manager so that it reports on it and never in
    // front of it. It is taken out again when provisioning stops, because a
    // second session registers the manager again, and then "after" is only true
    // if the probe is the last one in (docs/50).
    if (g_probe_instance == nullptr) {
      esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, &prov_scan_probe_cb,
                                          nullptr, &g_probe_instance);
    }

    // No scan of our own is started here. It used to be, as a diagnostic, and it
    // was in the way: two scans of the same radio cancel each other and the phone
    // asks for one the moment it opens its list (docs/50).
    //
    // AP+STA is the access point's business: a board that is only an access point
    // cannot scan, which is what made the phone's list empty. Over Bluetooth the
    // station is the only thing needed, and there is no access point to add.
    if (ble) {
      hal::logf(pal::kLogInfo, "PROV", "bluetooth, name %s", service_name);
      return true;
    }
    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) == ESP_OK) {
      hal::logf(pal::kLogInfo, "PROV", "mode %d (1 STA, 2 AP, 3 APSTA)", static_cast<int>(mode));
      if (mode != WIFI_MODE_APSTA) {
        if (esp_wifi_set_mode(WIFI_MODE_APSTA) == ESP_OK) {
          hal::logf(pal::kLogInfo, "PROV", "mode forced to APSTA");
        } else {
          hal::logf(pal::kLogWarn, "PROV", "could not set APSTA");
        }
      }
    }
    return true;
  }

  // The state, and nothing else: this is asked on every turn while provisioning
  // runs. It used to print a line every three seconds with the number of stations
  // and the driver's count. It read like the board was scanning in a loop, the
  // radio was working for nothing, and it is gone (docs/50, docs/75).
  ProvState prov_state() override { return g_prov_state; }

  bool prov_stop() override {
    if (g_prov_state != ProvState::kRunning) return false;
    wifi_prov_mgr_stop_provisioning();
    // Out with the manager: it will register again if it starts again, and the
    // probe has to be behind it (docs/50).
    if (g_probe_instance != nullptr) {
      esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, g_probe_instance);
      g_probe_instance = nullptr;
    }
    g_prov_state = ProvState::kOff;
    return true;
  }

  // Only what this system recorded counts. The driver's station config may be
  // left over from a previous firmware on the same board, and a board that says
  // "I already have a network" when nobody gave it one is lying (docs/50).
  bool prov_provisioned() override {
    char ssid[kNetSsidCap];
    return own_record(ssid, sizeof(ssid));
  }

  bool prov_forget() override {
    own_record_erase();
    wifi_config_t conf;
    memset(&conf, 0, sizeof(conf));
    if (esp_wifi_set_config(WIFI_IF_STA, &conf) != ESP_OK) return false;
    g_want_station = false;
    g_connect_asked = false;
    g_expect_drop = true;
    esp_wifi_disconnect();
    started_ = false;
    g_prov_state = ProvState::kOff;
    return true;
  }

  // A page: the task takes it, this only hands it over. The address and what it
  // may cost come from the kernel, which is the one that knows the deadline
  // (docs/30).
  uint32_t http_get_start(const char* url, const HttpOpts& opts) override {
    if (url == nullptr || url[0] == '\0') return 0;
    if (strlen(url) >= kHttpUrlCap) return 0;
    if (opts.max_bytes == 0 && opts.sink == nullptr) return 0;
    if (opts.sink != nullptr && (opts.sink_path == nullptr || strlen(opts.sink_path) >= 96)) return 0;

    taskENTER_CRITICAL(&g_http_mux);
    const bool taken = (g_http_op != 0);
    if (!taken) {
      HttpJob job;
      snprintf(job.url, sizeof(job.url), "%s", url);
      job.timeout_ms = opts.timeout_ms;
      job.max_bytes = opts.max_bytes;
      job.sink = opts.sink;
      if (opts.sink != nullptr) snprintf(job.sink_path, sizeof(job.sink_path), "%s", opts.sink_path);
      g_http = job;
      g_http_finished = false;
      g_http_cancel = false;
      g_http_op = g_http_next_op;
      if (++g_http_next_op == 0) g_http_next_op = 1;
    }
    const uint32_t op = g_http_op;
    taskEXIT_CRITICAL(&g_http_mux);
    if (taken) return 0;

    const BaseType_t made = xTaskCreate(
        &http_task, "http", kHttpStack,
        reinterpret_cast<void*>(static_cast<uintptr_t>(op)), 5, nullptr);
    if (made != pdPASS) {
      taskENTER_CRITICAL(&g_http_mux);
      g_http_op = 0;
      taskEXIT_CRITICAL(&g_http_mux);
      hal::logf(pal::kLogWarn, "HTTP", "no room for the download task");
      return 0;
    }
    return op;
  }

  bool http_poll(uint32_t op, bool& done, HttpResult& out) override {
    bool known = false;
    taskENTER_CRITICAL(&g_http_mux);
    if (op != 0 && g_http_op == op) {
      known = true;
      if (g_http_finished) {
        done = true;
        out.status = g_http.status;
        out.body = g_http.body;
        out.body_len = g_http.body_len;
        out.saved = g_http.saved;
        out.error = g_http.error;
      }
    }
    taskEXIT_CRITICAL(&g_http_mux);
    return known;
  }

  void http_release(uint32_t op) override {
    char* to_free = nullptr;
    taskENTER_CRITICAL(&g_http_mux);
    if (op != 0 && g_http_op == op) {
      if (g_http_finished) {
        to_free = g_http.body;
        g_http.body = nullptr;
        g_http.body_len = 0;
        g_http.saved = false;
        g_http.sink = nullptr;
        g_http.sink_path[0] = '\0';
        g_http_finished = false;
        g_http_op = 0;
      } else {
        // Nothing has come back yet and nobody wants it: the task drops the page
        // it was building when it sees this (docs/75).
        g_http_cancel = true;
      }
    }
    taskEXIT_CRITICAL(&g_http_mux);
    if (to_free != nullptr) free(to_free);
  }

 private:
  bool started_ = false;
  // The records of the last scan, and how many. Kept here so that a list can be
  // asked for index by index instead of one big allocation (docs/75).
  static const uint32_t kMaxRecords = 32;
  wifi_ap_record_t g_records[kMaxRecords];
  uint32_t record_count_ = 0;
  bool fetched_ = false;
};

}  // namespace

INet* esp32_net() {
  static Esp32Net net;
  return &net;
}

}  // namespace hal
