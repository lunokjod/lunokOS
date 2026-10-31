// Board: the M5 ATOM family (ESP32-PICO-D4, 4 MB flash, 320 KB RAM, no PSRAM, no
// screen): the Lite and the Matrix, which differ in the number of lights and in
// nothing this file decides. The identity header says which one is being built
// (docs/50). See docs/50-hal-i-plaques.md for the sheet and for what is
// deliberately absent.
#include <cstdio>
#include <cstring>

#include <esp_crt_bundle.h>
#include <esp_event.h>
#include <esp_http_client.h>
#include <esp_netif.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <esp_tls_errors.h>
#include <mdns.h>
#include <sys/time.h>

#include <driver/gpio.h>
// The IMU of the Matrix is on the board's own I2C bus, and the IDF driver is what
// reads it: two pins, one address and a burst of registers, no library needed
// (docs/50).
#include <driver/i2c.h>

// The core's own speed control: it is the one that also moves the APB and the
// FreeRTOS tick when the frequency changes (read on disk, docs/50).
#include <esp32-hal-cpu.h>
// The WS2812 writer of the core: RMT with a 100 ns tick, 0.4 us and 0.8 us, in GRB
// order. Writing another one would be writing the same thing (docs/50).
#include <esp32-hal-rgb-led.h>
// And the RMT itself, for the board whose strip has more than one light: same tick
// and same two durations, written in one go instead of one chip at a time.
#include <esp32-hal-rmt.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_sntp.h>
#include <esp_wifi.h>
#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_ble.h>
#include <wifi_provisioning/scheme_softap.h>

#include "atom_nvs.h"
#include "atom_storage.h"
#include "board/identity.h"
#include "hal/hal.h"

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

// The SoC clock: it counts while there is power and is lost when the power goes.
// The RTC keeps counting through a deep sleep, and the epoch survives in the RTC
// memory: without that flag a board that woke from a deep sleep would say it has
// never known the date, which is the one moment it does (docs/60, docs/90).
int64_t wall_ms() {
  struct timeval tv;
  if (gettimeofday(&tv, nullptr) != 0) return 0;
  return static_cast<int64_t>(tv.tv_sec) * 1000 + static_cast<int64_t>(tv.tv_usec) / 1000;
}

// Survives a deep sleep: it is the RTC memory and nothing else (docs/50).
RTC_DATA_ATTR bool g_epoch_known = false;
// The deadline the kernel armed, so `last_wake()` can say whether the timer is
// what brought the board back.
RTC_DATA_ATTR int64_t g_armed_epoch_ms = 0;

class AtomClock : public IClock {
 public:
  // Until somebody sets it, this board's gettimeofday counts from the boot, not
  // from 1970: reporting that number as an epoch made `run clock` say "17 seconds
  // past the epoch, from soc", which is a lie. Without an epoch the answer is 0
  // and the source is "none" (docs/60).
  int64_t epoch_ms() override { return (known_ || g_epoch_known) ? wall_ms() : 0; }

  bool set_epoch_ms(int64_t ms) override {
    if (ms < 0) return false;
    struct timeval tv;
    tv.tv_sec = static_cast<time_t>(ms / 1000);
    tv.tv_usec = static_cast<suseconds_t>((ms % 1000) * 1000);
    if (settimeofday(&tv, nullptr) != 0) return false;
    known_ = true;
    g_epoch_known = true;
    return true;
  }

  int64_t monotonic_ms() override { return static_cast<int64_t>(esp_timer_get_time() / 1000); }

  // The RTC timer is the wake this board has, and it is the only one the kernel
  // needs to program: only the nearest alarm is ever armed (docs/60).
  bool arm_wake(int64_t at_epoch_ms) override {
    int64_t left = at_epoch_ms - wall_ms();
    if (left < 1) left = 1;
    g_armed_epoch_ms = at_epoch_ms;
    return esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(left) * 1000) == ESP_OK;
  }

  void disarm_wake() override {
    g_armed_epoch_ms = 0;
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  }

  WakeCause last_wake() override {
    switch (esp_sleep_get_wakeup_cause()) {
      case ESP_SLEEP_WAKEUP_TIMER:
        return WakeCause::kTimer;
      case ESP_SLEEP_WAKEUP_EXT0:
      case ESP_SLEEP_WAKEUP_EXT1:
        return WakeCause::kOther;
      case ESP_SLEEP_WAKEUP_UNDEFINED:
        return WakeCause::kPowerOn;
      default:
        return WakeCause::kOther;
    }
  }

  bool can_wake() const override { return true; }

  const char* source() const override {
    return (known_ || g_epoch_known) ? "soc" : "none";
  }

 private:
  bool known_ = false;
};

// What a person sees and what a person presses (docs/50).
//
// The light is a strip of WS2812 chips on GPIO27: one on the Lite, twenty-five on
// the Matrix, and `board::led_count()` is how many. A frame is 24 bits per light at
// 1,25 us each — 30 us for one light, 750 us for the twenty-five — and the two
// buffers below are the size of this board's strip and no bigger, because the count
// is a constant of the identity header and not a number read at run time (docs/75).
//
// How it goes on the wire is what the core already does for one light: a 100 ns RMT
// tick, 0,8 us high for a one and 0,4 us for a zero, three colours per light in GRB
// order. The Lite keeps the core's own writer, which is the path measured at 30 us;
// the strip is the same frame written in one go, because a light at a time is a
// frame of its own and the strip would latch every one of them (docs/50).
const uint8_t kLedPin = 27;
constexpr float kLedTickNs = 100.0f;   // one RMT tick, the same the core sets
constexpr uint32_t kLedTicksOne = 8;   // a one: 0,8 us high...
constexpr uint32_t kLedTicksZero = 4;  // ...and 0,4 us low
// The button is GPIO39: input only (34 to 39 have no pull-up inside the chip), the
// board carries the resistor and pressing takes it to ground. Ten milliseconds is
// what the maker's own library debounces with (`Button(39, true, 10)` in M5Atom),
// and it is repeated here so that the answer the kernel gets is one it can trust.
const uint8_t kButtonPin = 39;
const uint8_t kButtonDebounceMs = 10;

class AtomLed : public ILed {
 public:
  uint32_t count() override { return board::led_count(); }
  uint32_t max_value() override { return board::led_max(); }

  bool fill(uint8_t r, uint8_t g, uint8_t b) override {
    for (uint32_t light = 0; light < count(); ++light) pixel(light, r, g, b);
    return true;
  }

  bool pixel(uint32_t index, uint8_t r, uint8_t g, uint8_t b) override {
    if (index >= count()) return false;
    colours_[index * 3 + 0] = r;
    colours_[index * 3 + 1] = g;
    colours_[index * 3 + 2] = b;
    return true;
  }

  // One frame on the wire, and only when somebody asks for it: a lit light is
  // current this board spends, and the kernel is what decides when it changes
  // (docs/75).
  void show() override {
    if (board::led_count() == 1) {
      // The core's writer for one chip: 24 bits, 30 us, blocking, and there is
      // nothing better to do with those 30 us (docs/50).
      neopixelWrite(kLedPin, colours_[0], colours_[1], colours_[2]);
      return;
    }
    if (!ensure_rmt()) return;
    for (uint32_t light = 0; light < count(); ++light) {
      // GRB is the order this family's chips take, and it is the order the core
      // writes in (docs/50).
      const uint8_t channel[3] = {colours_[light * 3 + 1], colours_[light * 3 + 0],
                                  colours_[light * 3 + 2]};
      for (uint32_t colour = 0; colour < 3; ++colour) {
        for (uint32_t bit = 0; bit < 8; ++bit) {
          rmt_data_t& item = wire_[(light * 24) + (colour * 8) + bit];
          const bool one = (channel[colour] & (0x80u >> bit)) != 0;
          item.level0 = 1;
          item.duration0 = one ? kLedTicksOne : kLedTicksZero;
          item.level1 = 0;
          item.duration1 = one ? kLedTicksZero : kLedTicksOne;
        }
      }
    }
    // Blocking: 750 us on the twenty-five lights, and it is one frame at a time on
    // one wire. The kernel paints at most once per turn and only when it changes
    // (docs/75).
    if (!rmtWriteBlocking(rmt_, wire_, board::led_count() * 24)) {
      printf("led: the RMT channel refused the frame; the strip is not painted\n");
    }
  }

 private:
  // One channel of the RMT, taken the first time a frame has to go out. The core's
  // own writer does the same for the single light (docs/50).
  bool ensure_rmt() {
    if (rmt_ != nullptr) return true;
    rmt_ = rmtInit(kLedPin, RMT_TX_MODE, RMT_MEM_64);
    if (rmt_ == nullptr) {
      printf("led: no RMT channel for the strip; the panel stays dark\n");
      return false;
    }
    rmtSetTick(rmt_, kLedTickNs);
    return true;
  }

  // What the strip has to show, three bytes per light, and how it goes on the wire:
  // 24 entries of four bytes per light. On the Lite the strip path is never taken
  // and neither buffer grows past one light (docs/75).
  uint8_t colours_[board::led_count() * 3] = {};
  rmt_data_t wire_[board::led_count() * 24] = {};
  rmt_obj_t* rmt_ = nullptr;
};

class AtomInput : public IInput {
 public:
  bool pressed() override {
    ensure_ready();
    const bool raw = gpio_get_level(static_cast<gpio_num_t>(kButtonPin)) == 0;
    const int64_t now = esp_timer_get_time() / 1000;
    if (raw != raw_) {
      // It moved: the clock of the debounce starts again.
      raw_ = raw;
      changed_ms_ = now;
      return stable_;
    }
    if (now - changed_ms_ < static_cast<int64_t>(kButtonDebounceMs)) return stable_;
    if (raw != stable_) {
      stable_ = raw;
      if (stable_) pressed_since_ms_ = changed_ms_;
    }
    return stable_;
  }

  int64_t held_ms() override {
    if (!stable_) return 0;
    return (esp_timer_get_time() / 1000) - pressed_since_ms_;
  }

 private:
  void ensure_ready() {
    if (ready_) return;
    // Input only, and no pull: the pin cannot pull and the board has the resistor.
    gpio_config_t config = {};
    config.pin_bit_mask = 1ULL << kButtonPin;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&config);
    raw_ = gpio_get_level(static_cast<gpio_num_t>(kButtonPin)) == 0;
    stable_ = raw_;
    changed_ms_ = esp_timer_get_time() / 1000;
    pressed_since_ms_ = changed_ms_;
    ready_ = true;
  }

  bool ready_ = false;
  bool raw_ = false;
  bool stable_ = false;
  int64_t changed_ms_ = 0;
  int64_t pressed_since_ms_ = 0;
};

// The power of this board. The cable is the power and there is no gauge, so the
// battery says it cannot tell. What it does have is both sleeps, the RTC timer to
// come back at an hour, and one button on top of it to come back by hand
// (docs/50).
class AtomPower : public IPower {
 public:
  bool battery(int&, int&, bool&) override { return false; }

  bool sleep(SleepMode mode, int64_t at_epoch_ms) override {
    // The button is the one thing a person can press, and it is the wake that
    // works from either sleep: without it a deep sleep with no deadline would be
    // a board that is gone until the cable is pulled.
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_39, 0);
    if (at_epoch_ms > 0) {
      int64_t left = at_epoch_ms - wall_ms();
      if (left < 1) left = 1;
      esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(left) * 1000);
    }
    printf("power: %s sleep until %lld\n", (mode == SleepMode::kDeep) ? "deep" : "light",
           static_cast<long long>(at_epoch_ms));
    // The chip goes quiet with the clocks: what is still in the UART has to leave
    // before, or the line that says why it sleeps never arrives.
    fflush(stdout);

    if (mode == SleepMode::kDeep) {
      esp_deep_sleep_start();  // does not return: the board restarts (docs/90)
      return false;
    }
    const int64_t before = wall_ms();
    esp_light_sleep_start();
    printf("power: awake after %lld ms (%d)\n",
           static_cast<long long>(wall_ms() - before),
           static_cast<int>(esp_sleep_get_wakeup_cause()));
    return true;
  }

  // Said by the board because it is the wiring that decides it: the RTC timer for
  // the alarms and the button on top (docs/50).
  const char* wakes() override { return "timer, button"; }

  // 240, 160 and 80 and nothing else. Below 80 the APB follows the CPU down and
  // **no peripheral of the core reconfigures itself** (there is not one
  // onApbChangeCallback registered anywhere in cores/esp32), so the serial port
  // would come out at the wrong baud: the board's only interface would be garbage
  // (docs/50). A value the board does not do is refused and not rounded.
  uint32_t cpu_mhz() override { return getCpuFrequencyMhz(); }

  bool set_cpu_mhz(uint32_t mhz) override {
    if (mhz != 80 && mhz != 160 && mhz != 240) return false;
    if (!setCpuFrequencyMhz(mhz)) return false;
    // A chip rated for 160 only answers 160 when asked for 240: the answer is
    // what the board is really running at, not what it was asked for.
    return getCpuFrequencyMhz() == mhz;
  }
};

// Provisioning, the same shape Espressif ships: the board opens its own access
// point (PROV_xxxxxx) and protocomm talks to the ESP SoftAP Provisioning app.
// The callback runs in the provisioning task, so it only writes a flag: the
// kernel reads it in its own turn (docs/50).
volatile ProvState g_prov_state = ProvState::kOff;
// The manager keeps one scheme for its whole life: which one is remembered so a
// change can bring it down first (docs/50).
bool g_manager_ready = false;
ProvTransport g_manager_scheme = ProvTransport::kSoftAp;
volatile bool g_scan_done = true;

// What the system itself recorded. The station config of the driver lives in a
// store that anything on this board may have written (a previous firmware, another
// sketch): believing it made the board say "stored network: MOVISTAR_B7DC" on a
// board nobody had ever provisioned (measured on the board, 30/10). What counts is
// what **this** system wrote, so it keeps its own record (docs/50).
const char kNetNamespace[] = "lunoknet";
const char kNetSsidKey[] = "ssid";
const size_t kNetSsidCap = 33;

bool own_record(char* out, size_t cap) {
  hal::IKeyValue* kv = atom_nvs();
  if (kv == nullptr || out == nullptr || cap == 0) return false;
  size_t len = 0;
  if (!kv->get(kNetNamespace, kNetSsidKey, out, cap, len)) return false;
  if (len == 0 || len >= cap) return false;
  out[len] = '\0';
  return true;
}

bool own_record_write(const char* ssid) {
  hal::IKeyValue* kv = atom_nvs();
  if (kv == nullptr || ssid == nullptr || ssid[0] == '\0') return false;
  return kv->set(kNetNamespace, kNetSsidKey, ssid, strlen(ssid));
}

bool own_record_erase() {
  hal::IKeyValue* kv = atom_nvs();
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
// that has to stay is `btInUse()` below: without it Arduino frees the BTDM memory
// at boot and nothing can start afterwards.

// The scheme frees Bluetooth when provisioning ends (as the header of scheme_ble
// says to do), and this wraps it to remember that it did (docs/75).
void bt_event_cb(void* user_data, wifi_prov_cb_event_t event, void* event_data) {
  wifi_prov_scheme_ble_event_cb_free_btdm(user_data, event, event_data);
  if (event == WIFI_PROV_END || event == WIFI_PROV_DEINIT) {
    printf("prov: bluetooth freed\n");
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
          printf("wifi: the radio refused to connect (%s)\n", esp_err_to_name(err));
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
      printf("wifi: dropped, reason %d\n", static_cast<int>(g_sta_reason));
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
  printf("wifi: address " IPSTR "\n", IP2STR(&event->ip_info.ip));
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
  printf("prov: scan done (status %d, %u ap in the event, %u left for us, %s, heap %u)\n",
         (event != nullptr) ? static_cast<int>(event->status) : -1,
         (event != nullptr) ? static_cast<unsigned>(event->number) : 0u,
         static_cast<unsigned>(read), esp_err_to_name(err),
         static_cast<unsigned>(esp_get_free_heap_size()));
}

void prov_event_cb(void* user_data, wifi_prov_cb_event_t event, void* event_data) {
  (void)user_data;
  (void)event_data;
  // Every event goes out on the console: when the phone app shows an empty list
  // of networks, this is what tells us how far it got (docs/50).
  static const char* const kNames[] = {"INIT", "START", "CRED_RECV", "CRED_FAIL",
                                       "CRED_SUCCESS", "END", "DEINIT"};
  const uint32_t index = static_cast<uint32_t>(event);
  printf("prov: %s\n", (index < 7) ? kNames[index] : "?");
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
            printf("prov: the credentials came with no name\n");
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
      printf("prov: the radio is ours again\n");
      break;
    default:
      break;
  }
}

// Brings the manager up for one scheme. The scheme is chosen by the person with
// the command (`provision` is Bluetooth, `provision softap` is the access point),
// and switching brings the old one down first because the manager keeps only one
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
    printf("prov: the manager did not init\n");
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
const uint32_t kHttpUrlCap = 192;
const uint32_t kHttpStack = 10240;  // the TLS handshake runs on this stack

// What the task works on. The flags live outside it so that the whole request can
// be copied in one statement and the task can go on with its own copy.
struct HttpJob {
  char url[kHttpUrlCap] = {0};
  uint32_t timeout_ms = 0;
  uint32_t max_bytes = 0;
  int status = 0;
  HttpError error = HttpError::kNone;
  char* body = nullptr;
  uint32_t body_len = 0;
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

// Reads the page into a buffer of `max_bytes` and leaves it in the job. Called in
// the task and nowhere else, so the client is only touched there.
void http_fetch(HttpJob& job) {
  char* body = static_cast<char*>(malloc(job.max_bytes + 1));
  if (body == nullptr) {
    job.error = HttpError::kNoMemory;
    return;
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
    free(body);
    job.error = HttpError::kConnect;
    return;
  }

  uint32_t len = 0;
  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    printf("http: could not open %s (%s)\n", job.url, esp_err_to_name(err));
    job.error = http_error_for(err);
  } else {
    esp_http_client_fetch_headers(client);
    job.status = esp_http_client_get_status_code(client);
    while (http_wanted()) {
      if (len >= job.max_bytes) {
        // The page may end exactly on the last byte, or go on: one more byte
        // answers which, and a page that goes on is refused instead of cut
        // (a body that is silently cut is a lie about what the server sent).
        char extra = 0;
        if (esp_http_client_read(client, &extra, 1) > 0) job.error = HttpError::kTooBig;
        break;
      }
      const int got =
          esp_http_client_read(client, body + len, static_cast<int>(job.max_bytes - len));
      if (got < 0) {
        // The socket stopped talking. The cause is the driver's to say and not
        // this file's to invent, so the message carries the count and no verdict.
        printf("http: the page stopped after %u bytes\n", static_cast<unsigned>(len));
        job.error = HttpError::kConnect;
        break;
      }
      if (got == 0) break;  // nothing more to read
      len += static_cast<uint32_t>(got);
    }
    if (!http_wanted()) job.error = HttpError::kStopped;
  }
  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  if (job.error == HttpError::kNone || job.error == HttpError::kTooBig) {
    body[len] = '\0';
    job.body = body;
    job.body_len = len;
  } else {
    free(body);
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
class AtomNet : public INet {
 public:
  // The radio is this file's, not a library's. arduino-esp32 2.0.14 answers every
  // scan-done event with `WiFiScanClass::_scanDone()`, which calls
  // `esp_wifi_scan_get_ap_records` and **frees the driver's list** while its count
  // stays (WiFiGeneric.cpp:1041 and WiFiScan.cpp:109, read on disk). That is how a
  // scan asked for here and read here answered `scan: 0 records of 23 (ESP_OK)`
  // (measured on the board, 30/10), and the prebuilt provisioning manager the
  // phone talks to reads **that same list**: whoever arrives second gets nothing.
  // With the Arduino WiFi class out of the way, the driver's list is the system's
  // and nobody takes it first (docs/50).
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
    printf("ntp: asking %s\n", name);
    return true;
  }

  bool ntp_synced(int64_t& epoch_ms) override {
    if (!g_ntp_synced) return false;
    epoch_ms = wall_ms();
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
        printf("wifi: the radio did not take the station mode\n");
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
      printf("wifi: the radio refused to connect (%s)\n", esp_err_to_name(err));
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
      printf("wifi: association failed, reason %d\n", static_cast<int>(g_sta_reason));
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
    printf("scan: %u records of %u\n", static_cast<unsigned>(record_count_),
           static_cast<unsigned>(seen));
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
      printf("wifi: an attempt of our own is still open\n");
      return false;
    }
    if (!espWifiReady()) return false;
    wifi_config_t conf;
    memset(&conf, 0, sizeof(conf));
    if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK) {
      printf("wifi: the driver has no station config\n");
      return false;
    }
    if (conf.sta.ssid[0] == '\0') {
      printf("wifi: the driver has no name stored\n");
      return false;
    }
    // The provisioning manager puts the station on the network itself to check
    // the credentials, so when its end arrives the board may already be there
    // with an address. Nothing is restarted then: restarting it was what dropped
    // the address the phone had just proved (measured on the board, 31/10).
    if (g_sta_got_ip && !g_sta_failed) {
      printf("wifi: already on %s\n", reinterpret_cast<const char*>(conf.sta.ssid));
      started_ = true;
      return true;
    }
    printf("wifi: connecting to %s\n", reinterpret_cast<const char*>(conf.sta.ssid));
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
        printf("prov: mdns did not start\n");
      } else {
        mdns_hostname_set("lunokos");
        printf("prov: mdns up\n");
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
      printf("prov: bluetooth, name %s\n", service_name);
      return true;
    }
    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) == ESP_OK) {
      printf("prov: mode %d (1 STA, 2 AP, 3 APSTA)\n", static_cast<int>(mode));
      if (mode != WIFI_MODE_APSTA) {
        if (esp_wifi_set_mode(WIFI_MODE_APSTA) == ESP_OK) {
          printf("prov: mode forced to APSTA\n");
        } else {
          printf("prov: could not set APSTA\n");
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
    if (strlen(url) >= kHttpUrlCap || opts.max_bytes == 0) return 0;

    taskENTER_CRITICAL(&g_http_mux);
    const bool taken = (g_http_op != 0);
    if (!taken) {
      HttpJob job;
      snprintf(job.url, sizeof(job.url), "%s", url);
      job.timeout_ms = opts.timeout_ms;
      job.max_bytes = opts.max_bytes;
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
      printf("http: no room for the download task\n");
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
  int64_t last_report_ms_ = 0;
};

// The inertial sensor of the Matrix: a MPU6886 on the board's own I2C bus (SDA G25,
// SCL G21 at 100 kHz, address 0x68). The two ranges are set once and the numbers are
// the datasheet's: ±2 g is 16384 counts per g, ±250 °/s is 131 counts per degree per
// second, and the temperature is counts/326.8 + 25 (docs/50).
//
// Only a board that says it has one ever reaches this (board::has_imu()), and the
// bus is taken the first time somebody reads: the Lite pays nothing for any of it
// (docs/75).
const uint8_t kImuSda = 25;
const uint8_t kImuScl = 21;
const uint8_t kImuAddress = 0x68;
const uint32_t kImuClockHz = 100000;
const uint8_t kImuWhoAmI = 0x75;
const uint8_t kImuId = 0x19;  // what a MPU6886 answers there
const uint8_t kImuPwrMgmt1 = 0x6B;
const uint8_t kImuGyroConfig = 0x1B;
const uint8_t kImuAccelConfig = 0x1C;
const uint8_t kImuAccelXoutH = 0x3B;  // the first of fourteen bytes

class AtomImu : public IImu {
 public:
  bool read(float& ax, float& ay, float& az, float& gx, float& gy, float& gz,
            float& celsius) override {
    if (!ready_ && !begin()) return false;
    uint8_t raw[14];
    if (!read_registers(kImuAccelXoutH, raw, sizeof(raw))) return false;
    // The chip answers big-endian, high byte first, and the order is the datasheet's:
    // accel x, y, z, temperature, gyro x, y, z.
    const float per_g = 16384.0f;      // ±2 g
    const float per_dps = 131.0f;      // ±250 °/s
    ax = be16(raw + 0) / per_g;
    ay = be16(raw + 2) / per_g;
    az = be16(raw + 4) / per_g;
    celsius = be16(raw + 6) / 326.8f + 25.0f;
    gx = be16(raw + 8) / per_dps;
    gy = be16(raw + 10) / per_dps;
    gz = be16(raw + 12) / per_dps;
    return true;
  }

 private:
  static int16_t be16(const uint8_t* p) {
    return static_cast<int16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
  }

  bool write_register(uint8_t reg, uint8_t value) {
    const uint8_t buffer[2] = {reg, value};
    return i2c_master_write_to_device(I2C_NUM_0, kImuAddress, buffer, sizeof(buffer),
                                      pdMS_TO_TICKS(50)) == ESP_OK;
  }

  bool read_registers(uint8_t reg, uint8_t* out, size_t len) {
    return i2c_master_write_read_device(I2C_NUM_0, kImuAddress, &reg, 1, out, len,
                                        pdMS_TO_TICKS(50)) == ESP_OK;
  }

  // The bus and the chip, once. Every failure says so on the console: a board whose
  // sensor does not answer is a board to look at, not a mystery (docs/50).
  bool begin() {
    i2c_config_t config = {};
    config.mode = I2C_MODE_MASTER;
    config.sda_io_num = static_cast<gpio_num_t>(kImuSda);
    config.scl_io_num = static_cast<gpio_num_t>(kImuScl);
    config.sda_pullup_en = GPIO_PULLUP_ENABLE;
    config.scl_pullup_en = GPIO_PULLUP_ENABLE;
    config.master.clk_speed = kImuClockHz;
    if (i2c_param_config(I2C_NUM_0, &config) != ESP_OK ||
        i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0) != ESP_OK) {
      printf("imu: the I2C bus could not be taken; this board has no sensor\n");
      return false;
    }
    uint8_t id = 0;
    if (!read_registers(kImuWhoAmI, &id, 1) || id != kImuId) {
      printf("imu: no MPU6886 answered (who am i 0x%02x)\n", static_cast<unsigned>(id));
      return false;
    }
    // Out of reset, awake, with the gyro's oscillator as the clock; the ranges the
    // numbers above assume; and every axis on (the reset does that).
    if (!write_register(kImuPwrMgmt1, 0x80)) return false;
    vTaskDelay(pdMS_TO_TICKS(10));
    if (!write_register(kImuPwrMgmt1, 0x01) ||
        !write_register(kImuAccelConfig, 0x00) || !write_register(kImuGyroConfig, 0x00)) {
      printf("imu: the MPU6886 did not take its configuration\n");
      return false;
    }
    ready_ = true;
    return true;
  }

  bool ready_ = false;
};

class AtomPlatform : public IPlatform {
 public:
  const char* id() const override { return board::id(); }
  const char* family() const override { return board::family(); }
  bool emulator() const override { return board::emulator(); }

  // Built on first use, never in the constructor: mounting LittleFS before
  // Arduino's setup() runs is not allowed, and the constructor runs much earlier.
  const Caps& caps() const override {
    if (!built_) {
      built_ = true;
      caps_.add(kCapConsole);
      caps_.add(kCapAnsi);
      // /littlefs is mandatory: it holds the apps. It is advertised only when the
      // partition really mounts.
      if (atom_storage("/littlefs") != nullptr) caps_.add(kCapLittlefs);
      // The SoC keeps the epoch while there is power; it is not a chip with a
      // battery of its own.
      caps_.add(kCapRtc);
      // The nvs partition holds the small things that have to survive a reboot.
      caps_.add(kCapNvs);
      // The radio is part of this board; whether an access point answers is
      // another question, and that is `net.connect`'s to answer.
      caps_.add(kCapWifi);
      // The board can be handed a network from the Espressif app (docs/50).
      caps_.add(kCapProvisioning);
      // A page: esp_http_client with the certificate bundle of this build, in a
      // task of its own (docs/30).
      caps_.add(kCapHttp);
      // Both sleeps, and a clock that can be armed: the RTC timer of the board is
      // the wake the alarm queue programs (docs/60, docs/90).
      caps_.add(kCapSleepLight);
      caps_.add(kCapSleepDeep);
      caps_.add(kCapRtcWake);
      // One RGB light on GPIO27 and the button on GPIO39: the two things this board
      // does with a person (docs/50).
      caps_.add(kCapLed);
      caps_.add(kCapInput);
      // The Matrix carries the MPU6886; the Lite does not, and the identity says
      // which of the two this build is (docs/50).
      if (board::has_imu()) caps_.add(kCapImu);
    }
    return caps_;
  }
  // 0 until measured: docs/75 says a number that is not measured is not written.
  uint32_t heap_limit_kb() const override { return 0; }
  IClock* clock() override { return &clock_; }
  IStorage* storage(const char* mount) override { return atom_storage(mount); }
  IKeyValue* nvs() override { return atom_nvs(); }
  INet* net() override { return &net_; }
  IPower* power() override { return &power_; }
  IDisplay* display() override { return nullptr; }
  ILed* led() override { return &led_; }
  IInput* input() override { return &input_; }
  IImu* imu() override { return board::has_imu() ? &imu_ : nullptr; }

 private:
  mutable Caps caps_;
  mutable bool built_ = false;
  AtomClock clock_;
  AtomNet net_;
  AtomPower power_;
  AtomLed led_;
  AtomInput input_;
  AtomImu imu_;
};

AtomPlatform g_platform;

}  // namespace

IPlatform& platform() { return g_platform; }

}  // namespace hal
