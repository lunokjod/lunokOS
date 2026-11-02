#include <chrono>

#include "hal/hal.h"

#include "board/identity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emu/options.h"
#include "emu/gt911.h"
#include "emu/memories.h"
#include "emu/model.h"
#include "emu/surface.h"
#include "emu_nvs.h"
#include "emu_storage.h"
#include "pal/pal.h"

namespace emu {
namespace {

Options g_options;

}  // namespace

Options::Options()
    : state_dir(nullptr),
      flash_dir(nullptr),
      fs_dir("emulator"),
      board(nullptr),
      sd_dir(nullptr),
      battery_mv(0),
      battery_from_host(false),
      script(nullptr),
      run_app(nullptr),
      now_epoch(0),
      mem_cap_kb(0),
      wake_early_ms(0),
      baud(-1),
      time_scale(1),
      seed(0),
      verbose(false),
      // **The three fields of the glass that were left to whatever the stack held**: `display` and `gui`
      // have been read before ever being written, and the ink is off here —a test wants a paint that is
      // instant and exact— and turned on by `main`, which is what a person wants (docs/85).
      display(false),
      gui(false),
      gui_scale(0),
      ink(0),
      // **The panel of the bench is off unless the run asked for it**: a bench nobody is touching does
      // not read a bus once a turn, and a test wants a board that answers nothing here (docs/85).
      touch(false),
      touch_scan_ms(0),
      // **The times of the hardware are not paid unless the run asks for them**: a test wants the model
      // and not the clock, and `main` turns them on for the binary —`--no-delay` leaves them off (docs/85).
      delays(false),
      // **The app's own code is CPU time and it is not free on the glass**: sixty nanoseconds an
      // instruction at 160 MHz is the model's number, and `main` turns it on with the rest of the times
      // (-1 is "the bench's own number" and 0 is "free") (docs/85).
      lua_ns_160(-1),
      wifi_ms(300),
      wifi_absent(false),
      wifi_fail(false),
      scan_count(1),
      prov_ms(2000),
      prov_ssid("casa"),
      prov_pass("secret"),
      prov_never(false),
      prov_fail(false),
      http_ms(50),
      http_status(200),
      http_body(nullptr),
      http_fail(false),
      imu_fail(false),
      // Flat on a table, which is the reading an app that starts must survive.
      imu_ax(0.0f),
      imu_ay(0.0f),
      imu_az(1.0f),
      imu_gx(0.0f),
      imu_gy(0.0f),
      imu_gz(0.0f),
      imu_celsius(25.0f),
      env_fail(false),
      // A room, which is the reading an app that starts must survive.
      env_celsius(25.0f),
      env_humidity(50.0f) {}

const Options& options() { return g_options; }

void set_options(const Options& opts) { g_options = opts; }

const char* active_board() {
  const char* asked = g_options.board;
  if (asked == nullptr || asked[0] == '\0') return board::default_model();
  // A name that is not one of the models is not a board this bench can be, and the
  // option is checked when it is parsed; this is the belt for a caller that set the
  // struct by hand, and the default is the honest answer there too.
  for (uint32_t i = 0; i < board::models_count(); ++i) {
    const char* name = board::model(i);
    if (name != nullptr && strcmp(name, asked) == 0) return name;
  }
  return board::default_model();
}

namespace {
// The bench has three keys: key 0 is the ATOM's only button and the Paper's
// middle one, and keys 1 and 2 are the Paper's sides (docs/50, docs/85).
const int kEmuKeys = 3;
bool g_key_down[kEmuKeys] = {false, false, false};
}  // namespace

void press_button(bool down) { g_key_down[0] = down; }

void press_key(int key, bool down) {
  if (key < 0 || key >= kEmuKeys) return;
  g_key_down[key] = down;
}

bool button_down() { return g_key_down[0]; }

bool key_down(int key) {
  if (key < 0 || key >= kEmuKeys) return false;
  return g_key_down[key];
}

}  // namespace emu

namespace hal {
namespace {

// The hardware of this bench is `emu::model()`, which is the model `--board` named: one list and
// not two, because a bench that answers for another board teaches the wrong thing (docs/85).
int64_t mono_ms() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

// The PC has one clock and it is the host's. The epoch is the one `--now` says,
// and **only** that one: without `--now` the emulator is a board that never
// synced, so the epoch is 0 and the source is "none", the same as the real board
// (docs/85). The monotonic counter is the host's either way.
class HostClock : public IClock {
 public:
  HostClock() : mono_start_(mono_ms()), epoch_base_(0), scale_(1), known_(false) {
    const emu::Options& opts = emu::options();
    scale_ = (opts.time_scale > 0) ? opts.time_scale : 1;
    if (opts.now_epoch > 0) {
      epoch_base_ = opts.now_epoch * 1000;
      known_ = true;
    }
  }

  int64_t epoch_ms() override { return known_ ? (epoch_base_ + elapsed_ms()) : 0; }
  int64_t monotonic_ms() override { return elapsed_ms(); }

  bool set_epoch_ms(int64_t ms) override {
    if (ms < 0) return false;
    epoch_base_ = ms - elapsed_ms();
    known_ = true;
    return true;
  }

  // The wake is a promise this band can keep: the fake power moves the clock
  // instead of stopping the PC (docs/85).
  bool arm_wake(int64_t at_epoch_ms) override {
    armed_ = at_epoch_ms;
    return true;
  }

  void disarm_wake() override { armed_ = 0; }

  WakeCause last_wake() override {
    // On the Paper the alarm is the chip's, so a wake that was armed is the clock's; on the ATOMs
    // there is no chip and the timer is the only thing that can have brought the board back
    // (docs/50, docs/60). A sleep that stopped before its hour is neither: nothing says what ended
    // it, which is exactly what a board whose supply blinked answers (docs/85).
    const int64_t at = (slept_ > 0) ? slept_ : armed_;
    if (at > 0 && epoch_ms() >= at) {
      return emu::model().keeps_the_hour ? WakeCause::kClock : WakeCause::kTimer;
    }
    return WakeCause::kPowerOn;
  }

  // The sleep this band just did, told by the fake power: it is what makes the wake cause the timer's
  // when the hour was reached, and nothing's when it was not (docs/85).
  void note_sleep(int64_t at_epoch_ms) { slept_ = at_epoch_ms; }

  bool can_wake() const override { return true; }

  const char* source() const override {
    if (!known_) return "none";
    // `rtc` is an hour that a chip with a coin cell keeps; `soc` is the board's own counter. The
    // Paper has the chip and the ATOMs do not (docs/50, docs/60).
    return emu::model().keeps_the_hour ? "rtc" : "soc";
  }

  // Both clocks jump: the epoch and the monotonic one, because the alarm queue is
  // on the monotonic side and a sleep that does not move it would leave every
  // alarm in the past (docs/60).
  void advance_ms(int64_t delta) {
    if (delta <= 0) return;
    mono_start_ -= delta / static_cast<int64_t>(scale_);
  }

 private:
  int64_t elapsed_ms() const { return (mono_ms() - mono_start_) * static_cast<int64_t>(scale_); }

  int64_t mono_start_;
  int64_t epoch_base_;
  uint32_t scale_;
  bool known_;
  int64_t armed_ = 0;
  int64_t slept_ = 0;
};

// One clock for the whole band: the power has to move the same one the kernel
// reads, and it is built on first use because `--now` and `--time-scale` are set
// after the first static of this file runs (docs/85).
HostClock& host_clock() {
  static HostClock instance;
  return instance;
}

}  // namespace

// The bench's clock, moved by hand. It is the only way a test can watch a five-second breath in a
// millisecond, and it moves **the same clock the kernel reads**, so an effect and the turn that
// paints it agree (docs/30, docs/85). It is here and not in `emu` because the clock is this
// translation unit's secret and the anonymous namespace is not reachable from outside.
void bench_advance_clock(int64_t delta) { host_clock().advance_ms(delta); }

namespace {

// The board sleeps and this one does not: the virtual clock jumps to the hour it
// was told, and the wake is the timer's. There is no gauge here either, and the
// emulator says so instead of inventing a voltage (docs/85).
// The panel of the Matrix, and the colours it is showing: the fake records them
// instead of shining (docs/85). It has the twenty-five lights of the biggest light
// of the family, so an app written for the panel runs here the same as on the
// board, and a strip that is painted light by light can be checked without
// hardware.
const uint32_t kEmuLights = 25;

class FakeLed : public ILed {
 public:
  uint32_t count() override { return emu::model().lights; }
  // It imitates the panel of the Matrix, ceiling included: a PC that let 255
  // through would hide the limit that the board has (docs/85).
  uint32_t max_value() override { return board::led_max(); }

  bool fill(uint8_t r, uint8_t g, uint8_t b) override {
    for (uint32_t light = 0; light < kEmuLights; ++light) pixel(light, r, g, b);
    return true;
  }

  bool pixel(uint32_t index, uint8_t r, uint8_t g, uint8_t b) override {
    if (index >= kEmuLights) return false;
    lights_[index].red = r;
    lights_[index].green = g;
    lights_[index].blue = b;
    return true;
  }

  void show() override { ++frames_; }

  // The first light, which is all a board of one light has: the tests that ask the
  // band for "the colour" keep asking the same question (docs/85).
  uint8_t red() const { return lights_[0].red; }
  uint8_t green() const { return lights_[0].green; }
  uint8_t blue() const { return lights_[0].blue; }
  uint32_t frames() const { return frames_; }

  bool light(uint32_t index, uint8_t& r, uint8_t& g, uint8_t& b) const {
    if (index >= kEmuLights) return false;
    r = lights_[index].red;
    g = lights_[index].green;
    b = lights_[index].blue;
    return true;
  }

 private:
  struct Rgb {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
  };

  Rgb lights_[kEmuLights] = {};
  uint32_t frames_ = 0;
};

// The sensor the PC does not have: it answers what the options say, so an app that
// reads it can be tried without a board, and `imu_fail` is a chip that is not there
// (docs/85).
class FakeImu : public IImu {
 public:
  bool read(float& ax, float& ay, float& az, float& gx, float& gy, float& gz,
            float& celsius) override {
    const emu::Options& opts = emu::options();
    if (opts.imu_fail) return false;
    ax = opts.imu_ax;
    ay = opts.imu_ay;
    az = opts.imu_az;
    gx = opts.imu_gx;
    gy = opts.imu_gy;
    gz = opts.imu_gz;
    celsius = opts.imu_celsius;
    return true;
  }
};

// The air of this band, which is what the options say it is: a PC has no SHT30 and a room is not a
// test, and `env_fail` is the chip that did not answer (docs/85).
class FakeSensor : public ISensor {
 public:
  bool read(float& celsius, float& humidity) override {
    const emu::Options& opts = emu::options();
    if (opts.env_fail) return false;
    celsius = opts.env_celsius;
    humidity = opts.env_humidity;
    return true;
  }
};

// The button is what the test says it is, and how long it has been held is the
// host's clock, the same one the kernel counts with (docs/85).
class FakeInput : public IInput {
 public:
  uint32_t count() override { return emu::model().keys; }

  bool pressed(uint32_t key) override {
    if (key >= kKeys) return false;
    const bool down = emu::key_down(static_cast<int>(key));
    if (down && !down_[key]) started_ms_[key] = mono_ms();
    down_[key] = down;
    return down;
  }

  int64_t held_ms(uint32_t key) override {
    if (key >= kKeys || !down_[key]) return 0;
    return mono_ms() - started_ms_[key];
  }

  // The bench imitates the band of the Paper: one compound dial of three keys on
  // the right face of the page, so an app that reads the positions can be tried on the PC
  // (docs/50, docs/85).
  Face face(uint32_t) override { return Face::kRight; }
  Spot spot(uint32_t key) override {
    // The bench has the Paper's shape: a vertical dial of three keys (docs/85).
    if (key == 1) return Spot::kUp;
    if (key == 2) return Spot::kDown;
    return Spot::kCentre;
  }
  KeyKind kind(uint32_t) override { return KeyKind::kCompound; }
  uint32_t group(uint32_t) override { return 0; }

 private:
  static const uint32_t kKeys = 3;
  bool down_[kKeys] = {false, false, false};
  int64_t started_ms_[kKeys] = {0, 0, 0};
};

// **What reading the battery costs on this chip**: four conversions of a twelve-bit ADC averaged, at
// about a hundred microseconds each, plus the characterisation of the channel the first time, which
// reads the eFuse (docs/50, docs/85). The numbers are the datasheet's and the driver's, not a
// measurement of this unit.
const uint32_t kAdcConversionUs = 100;
const uint32_t kAdcSetupUs = 1000;

class FakePower : public IPower {
 public:
  bool battery(int& mv, int& pct, bool& charging) override {
    const int32_t set = emu::options().battery_mv;
    // Zero is a board that cannot say, which is what `power.battery()` answers with nil:
    // a fake battery always at 100 % would hide every path of the real one (docs/85). And a board
    // without a gauge cannot say even when the person put a number in: the ATOMs have no cell to
    // read (docs/50).
    if (set <= 0 || !emu::model().gauge) return false;
    // **The reading is an ADC behind a divider and it is not free**: four conversions of the chip's ADC
    // at about a hundred microseconds each, and the characterisation of the channel once, which reads
    // the eFuse (docs/50, docs/85). It is paid only when the run is paying the times of the hardware.
    if (emu::options().delays) {
      static bool characterised = false;
      uint32_t us = 4 * kAdcConversionUs;
      if (!characterised) {
        characterised = true;
        us += kAdcSetupUs;
      }
      pal::delay_us(us);
    }
    mv = static_cast<int>(set);
    pct = static_cast<int>(battery_percent(mv));
    // The bench is not on a cable any more than the Paper can tell: the same false and the
    // same warning in docs/30.
    charging = false;
    return true;
  }

  bool sleep(SleepMode mode, int64_t at_epoch_ms) override {
    host_clock().note_sleep(at_epoch_ms);
    if (at_epoch_ms > 0) {
      const int64_t now = host_clock().epoch_ms();
      int64_t jump = (now > 0) ? (at_epoch_ms - now) : 0;
      // A bench told to come back early does it here, which is the button of the glass and the supply
      // that blinked: the board lands before its hour and the kernel has to say so (docs/85).
      const int64_t early = emu::options().wake_early_ms;
      if (early > 0 && jump > early) jump -= early;
      host_clock().advance_ms(jump);
    }
    // To stderr and not to the console: it is this band talking about itself and
    // it must not be mixed with what the system says to a person (docs/85).
    fprintf(stderr, "power: %s sleep until %lld (the clock jumps; the PC does not sleep)\n",
            (mode == SleepMode::kDeep) ? "deep" : "light",
            static_cast<long long>(at_epoch_ms));
    return true;
  }

  // The Paper comes back on the chip's alarm when it is powered off, and on its own timer when it
  // is not; the ATOMs have no chip and only know the timer (docs/50).
  const char* wakes() override { return emu::model().keeps_the_hour ? "timer, clock" : "timer"; }

  // A board whose supply is cut stops being a board, and a bench whose supply is cut stops being a
  // process: the answer is yes, the kernel leaves its loop, and the memories that are not a volume
  // go to their files on the way out, the same as they do after a deep sleep (docs/85). It is said
  // to stderr because it is this band talking about itself and not the system talking to a person.
  bool power_off() override {
    // A board that cannot cut its own supply says no here as well: the bench answers what the model
    // does and not what the PC could do (docs/85).
    if (!emu::model().powers_off) return false;
    fprintf(stderr, "power: off (the bench ends; rtc.bin and fram.bin are written)\n");
    return true;
  }

  // The PC does not change its speed, and this band says so instead of pretending:
  // the value is remembered so the call and its refusals can be used and tested,
  // and it starts at the 160 MHz a board boots with (docs/85).
  uint32_t cpu_mhz() override { return cpu_mhz_; }
  bool set_cpu_mhz(uint32_t mhz) override {
    if (mhz != 80 && mhz != 160 && mhz != 240) return false;
    cpu_mhz_ = mhz;
    return true;
  }

 private:
  uint32_t cpu_mhz_ = 160;
};

// The network of this band is a **fake** and docs/85 says so: it takes `--wifi`
// milliseconds of simulated time to come up and then goes nowhere. It exists so
// that the rule "the kernel never blocks on the radio" can be measured on a PC;
// the real radio is on the board.
class FakeNet : public INet {
 public:
  bool wifi_start(const char* ssid, const char* pass) override {
    if (emu::options().wifi_absent) return false;
    if (ssid == nullptr || ssid[0] == '\0') return false;
    (void)pass;
    started_ms_ = mono_ms();
    state_ = NetState::kConnecting;
    return true;
  }

  NetState wifi_state() override {
    if (state_ != NetState::kConnecting) return state_;
    const int64_t wait = emu::options().wifi_ms;
    if (mono_ms() - started_ms_ < wait) return state_;
    state_ = emu::options().wifi_fail ? NetState::kFailed : NetState::kUp;
    return state_;
  }

  bool wifi_disconnect() override {
    state_ = NetState::kOff;
    started_ms_ = 0;
    return true;
  }

  // Connects with what provisioning left behind, which is a pair of strings in
  // this band and the board's own store on the real thing (docs/85).
  bool wifi_start_stored() override {
    if (!provisioned_) return false;
    return wifi_start(stored_ssid_, stored_pass_);
  }

  bool wifi_scan_start() override {
    if (emu::options().wifi_absent) return false;
    scanning_ = true;
    scan_started_ms_ = mono_ms();
    return true;
  }

  bool wifi_scan_done() override {
    if (scanning_ && mono_ms() - scan_started_ms_ >= 50) scanning_ = false;
    return !scanning_;
  }

  uint32_t wifi_scan_count() override {
    if (scanning_) return 0;
    return (emu::options().scan_count > 0) ? static_cast<uint32_t>(emu::options().scan_count) : 0;
  }

  bool wifi_scan_record(uint32_t index, char* ssid, size_t cap, int32_t& rssi) override {
    if (scanning_ || ssid == nullptr || cap == 0) return false;
    if (emu::options().scan_count <= 0) return false;
    if (index >= static_cast<uint32_t>(emu::options().scan_count)) return false;
    // The first one is the network of this band; the others are names that only
    // exist to make a list of more than one line (docs/85).
    snprintf(ssid, cap, "%s", (index == 0) ? emu::options().prov_ssid : "neighbour");
    rssi = -50 - static_cast<int32_t>(index);
    return true;
  }

  bool wifi_stored_ssid(char* out, size_t cap) override {
    if (out == nullptr || cap == 0 || !provisioned_ || stored_ssid_[0] == '\0') return false;
    snprintf(out, cap, "%s", stored_ssid_);
    return true;
  }

  bool wifi_store(const char* ssid, const char* pass) override {
    if (ssid == nullptr || ssid[0] == '\0') return false;
    snprintf(stored_ssid_, sizeof(stored_ssid_), "%s", ssid);
    snprintf(stored_pass_, sizeof(stored_pass_), "%s", (pass != nullptr) ? pass : "");
    provisioned_ = true;
    return true;
  }

  // The provisioning here is a fake and docs/85 says so: the access point is up
  // for `prov_ms`, then the network it "received" is the one the options name.
  bool prov_start(const char* pop, char* service_name, size_t cap,
                  ProvTransport transport) override {
    (void)pop;
    (void)transport;  // the fake board has no Bluetooth and does not need one
    if (service_name != nullptr && cap > 0) snprintf(service_name, cap, "%s", "PROV_000000");
    if (emu::options().wifi_absent) return false;
    if (provisioned_ || prov_running_) return false;
    prov_running_ = true;
    prov_started_ms_ = mono_ms();
    return true;
  }

  // The time server of this band is a fake too: it answers after `wifi_ms`, and
  // the time it gives is the one the band started with (docs/85).
  bool ntp_start(const char* server) override {
    (void)server;
    if (emu::options().wifi_absent) return false;
    ntp_started_ms_ = mono_ms();
    ntp_running_ = true;
    return true;
  }

  bool ntp_synced(int64_t& epoch_ms) override {
    if (!ntp_running_) return false;
    if (mono_ms() - ntp_started_ms_ < emu::options().wifi_ms) return false;
    ntp_running_ = false;
    epoch_ms = (emu::options().now_epoch > 0) ? emu::options().now_epoch * 1000
                                              : 1700000000LL * 1000;
    return true;
  }

  ProvState prov_state() override {
    if (!prov_running_) return provisioned_ ? ProvState::kDone : ProvState::kOff;
    if (emu::options().prov_never) return ProvState::kRunning;
    if (mono_ms() - prov_started_ms_ < emu::options().prov_ms) return ProvState::kRunning;
    prov_running_ = false;
    if (emu::options().prov_fail) return ProvState::kFailed;
    snprintf(stored_ssid_, sizeof(stored_ssid_), "%s", emu::options().prov_ssid);
    snprintf(stored_pass_, sizeof(stored_pass_), "%s", emu::options().prov_pass);
    provisioned_ = true;
    return ProvState::kDone;
  }

  bool prov_stop() override {
    prov_running_ = false;
    return true;
  }

  bool prov_provisioned() override { return provisioned_; }

  bool prov_forget() override {
    provisioned_ = false;
    stored_ssid_[0] = '\0';
    stored_pass_[0] = '\0';
    state_ = NetState::kOff;
    return true;
  }

  // The page of this band is a fake and docs/85 says so: after `http_ms` the
  // address that was asked for comes back with the status and the body the options
  // name, or nothing when the server is meant to be down. The URL is in the body
  // so a test can see that what it asked for is what came back.
  uint32_t http_get_start(const char* url, const HttpOpts& opts) override {
    if (emu::options().wifi_absent || url == nullptr || url[0] == '\0') return 0;
    if (opts.max_bytes == 0 || strlen(url) >= sizeof(http_url_)) return 0;
    if (http_op_ != 0) return 0;
    snprintf(http_url_, sizeof(http_url_), "%s", url);
    http_max_bytes_ = opts.max_bytes;
    http_started_ms_ = mono_ms();
    http_op_ = 1;
    return http_op_;
  }

  bool http_poll(uint32_t op, bool& done, HttpResult& out) override {
    if (op == 0 || op != http_op_) return false;
    if (mono_ms() - http_started_ms_ < emu::options().http_ms) return true;
    done = true;
    if (emu::options().http_fail) {
      out.error = HttpError::kConnect;
      return true;
    }
    char text[512];
    const char* body = emu::options().http_body;
    if (body == nullptr) {
      snprintf(text, sizeof(text), "page for %s", http_url_);
      body = text;
    }
    const size_t len = strlen(body);
    if (len > http_max_bytes_) {
      out.error = HttpError::kTooBig;
      return true;
    }
    http_body_ = static_cast<char*>(malloc(len + 1));
    if (http_body_ == nullptr) {
      out.error = HttpError::kNoMemory;
      return true;
    }
    memcpy(http_body_, body, len + 1);
    out.status = emu::options().http_status;
    out.body = http_body_;
    out.body_len = static_cast<uint32_t>(len);
    return true;
  }

  void http_release(uint32_t op) override {
    if (op == 0 || op != http_op_) return;
    free(http_body_);
    http_body_ = nullptr;
    http_op_ = 0;
  }

 private:
  NetState state_ = NetState::kOff;
  int64_t started_ms_ = 0;
  bool scanning_ = false;
  int64_t scan_started_ms_ = 0;
  bool prov_running_ = false;
  int64_t prov_started_ms_ = 0;
  bool ntp_running_ = false;
  int64_t ntp_started_ms_ = 0;
  bool provisioned_ = false;
  char stored_ssid_[33];
  char stored_pass_[65];
  uint32_t http_op_ = 0;
  int64_t http_started_ms_ = 0;
  uint32_t http_max_bytes_ = 0;
  char http_url_[192];
  char* http_body_ = nullptr;
};

class EmulatorPlatform : public IPlatform {
 public:
  const char* id() const override { return board::id(); }
  const char* family() const override { return board::family(); }
  bool emulator() const override { return board::emulator(); }

  // One board, and it is the one this bench was told to be: an app is accepted or
  // refused here exactly as it would be on that board, which is the only way a manifest
  // tried on the PC means anything on the glass (docs/40, docs/85). The default of
  // IPlatform is already this answer, so there is nothing to override.

  const Caps& caps() const override {
    // The bench can be told to be another board between two boots —`use_board` in the tests does
    // exactly that— and a cached answer from the model before would be a lie about the hardware: the
    // cache is keyed by the model, not by "already built once" (docs/85).
    const char* model = emu::model().name;
    if (!built_ || built_for_ != model) {
      built_ = true;
      built_for_ = model;
      caps_ = Caps();
      caps_.add(kCapConsole);
      pal::IConsole* console = pal::system().console();
      if (console != nullptr && console->ansi()) caps_.add(kCapAnsi);
      // /littlefs is mandatory: it is the directory <fs_dir>/littlefs.
      if (emu_storage("/littlefs") != nullptr) caps_.add(kCapLittlefs);
      // The card is advertised only when one is in the slot, the same rule as the board
      // and the same as the flash: a capability that is not there is not promised
      // (docs/50, docs/70). The ATOMs have no slot at all, so a directory in the state does not
      // give them one.
      if (emu::model().slot && emu_storage("/sd") != nullptr) caps_.add(kCapSd);
      // The battery is a number the person sets, and only on a board that has a gauge (docs/85).
      if (emu::model().gauge && emu::options().battery_mv > 0) caps_.add(kCapBattery);
      caps_.add(kCapRtc);
      // The key-value store is one file per key under <state_dir>/nvs (docs/70).
      caps_.add(kCapNvs);
      // A fake radio, not the PC's network: it connects to nothing, and the
      // provisioning access point is a fake too (docs/85).
      if (!emu::options().wifi_absent) {
        caps_.add(kCapWifi);
        caps_.add(kCapProvisioning);
        // A page: this band serves a fake one and docs/85 says so (docs/30).
        caps_.add(kCapHttp);
      }
      // Both sleeps are a fake and they say so: the clock jumps instead of the PC
      // stopping (docs/85).
      caps_.add(kCapSleepLight);
      caps_.add(kCapSleepDeep);
      caps_.add(kCapRtcWake);
      // **The glass and the finger are the model's** (`Model::glass`, `Model::touch`): the Paper draws
      // and a finger goes through it, and the binary turns both on unless the run says no. Without this
      // line an app that asks `sys.caps().display` was told no on a bench that had one, while the view
      // behind it was there and the boot line said so (found running `layers` on the bench, 1/11).
      if (emu::model().glass && emu::options().display) caps_.add(kCapDisplay);
      if (emu::model().touch && emu::options().touch) caps_.add(kCapTouch);
      // The light and the sensor are the hardware of the model and of nobody else (docs/50).
      if (emu::model().lights > 0) caps_.add(kCapLed);
      caps_.add(kCapInput);
      if (emu::model().imu) caps_.add(kCapImu);
      // The air: the model's, and only the model's (docs/50, docs/85).
      if (emu::model().env) caps_.add(kCapEnv);
    }
    return caps_;
  }

  uint32_t heap_limit_kb() const override {
    // What the model's board answers, and only that: the Paper says its PSRAM (8 MB) and the ATOMs say
    // nothing, which is the kernel's own ceiling. `--mem-cap` may lower it and never raise it, because
    // the firmware on the glass is the one being emulated (docs/75, docs/85).
    const uint32_t board = emu::model().app_limit_kb;
    const int32_t kb = emu::options().mem_cap_kb;
    if (kb <= 0) return board;
    const uint32_t asked = static_cast<uint32_t>(kb);
    const uint32_t most = (board > 0) ? board : emu::model().heap_kb;
    return (asked > most) ? most : asked;
  }

  // Built on first use: the options that say which clock it is (`--now`,
  // `--time-scale`) are set by main() after this object exists.
  // What the model's board carries as extra memory. The bench **cannot account for it** —nothing is
  // really taken from a PSRAM on a PC, the blocks come from the process's heap— so free is the total
  // and the number is the board's, not the PC's (docs/85).
  ExtraMemory extra_memory() const override {
    ExtraMemory out;
    out.chip_bytes = emu::model().psram_chip_kb * 1024u;
    out.total_bytes = emu::model().psram_kb * 1024u;
    out.free_bytes = out.total_bytes;
    return out;
  }

  IClock* clock() override { return &host_clock(); }
  IStorage* storage(const char* mount) override { return emu_storage(mount); }
  IKeyValue* nvs() override { return emu_nvs(); }
  INet* net() override {
    if (emu::options().wifi_absent) return nullptr;
    static FakeNet instance;
    return &instance;
  }
  IPower* power() override {
    static FakePower instance;
    return &instance;
  }
  ISurface* display() override {
    // **The glass of the model**: the Paper draws on a panel and the ATOMs have no screen, so an option
    // cannot make one appear — a bench of an ATOM with a glass is a board nobody can buy. The binary
    // turns `Options::display` on for a model that has one (docs/85).
    if (!emu::model().glass || !emu::options().display) return nullptr;
    return emu::memory_surface();
  }
  ILed* led() override {
    // A board with no light has no light to return, the same as the real one (docs/50).
    if (emu::model().lights == 0) return nullptr;
    static FakeLed instance;
    return &instance;
  }
  IImu* imu() override {
    if (!emu::model().imu) return nullptr;
    static FakeImu instance;
    return &instance;
  }
  ISensor* sensor() const override {
    if (!emu::model().env) return nullptr;
    static FakeSensor instance;
    return &instance;
  }
  IInput* input() override {
    static FakeInput instance;
    return &instance;
  }
  uint32_t simulated_instruction_ns() override {
    // **The instructions of the app at the frequency the board is set to**: a bench that is not paying the
    // times of the hardware does not pay these either, and a board is not asked because its instruction
    // already takes the time it takes (docs/85). 160 MHz is the frequency every board of this family
    // starts at, and the number the option is given is the cost at that speed.
    const emu::Options& opts = emu::options();
    if (!opts.delays || opts.lua_ns_160 <= 0) return 0;
    IPower* supply = power();
    const uint32_t mhz = (supply != nullptr && supply->cpu_mhz() > 0) ? supply->cpu_mhz() : 160;
    return static_cast<uint32_t>(opts.lua_ns_160) * 160u / mhz;
  }
  ITouch* touch() const override {
    // **The panel of the model**, when the run has not taken it away (`--no-touch`): a GT911 in the frame
    // of this unit that costs what the bus costs and hands a frame over once. The ATOMs have no panel at
    // all (docs/85).
    if (!emu::model().touch || !emu::options().touch) return nullptr;
    return emu::touch_panel();
  }

 private:
  mutable Caps caps_;
  mutable bool built_ = false;
  // Which model the cached capabilities are about. A board is a board and the bench changes which
  // one it is, so the answer has a name attached (docs/85).
  mutable const char* built_for_ = nullptr;
};

EmulatorPlatform g_platform;

}  // namespace

IPlatform& platform() { return g_platform; }

}  // namespace hal


// What the fake light shows, for the tests: the band answers for itself and not
// through the kernel, so a policy that decides a colour and never paints it is
// caught (docs/85).
namespace emu {

uint8_t led_red() {
  hal::ILed* light = hal::platform().led();
  return (light != nullptr) ? static_cast<hal::FakeLed*>(light)->red() : 0;
}

uint8_t led_green() {
  hal::ILed* light = hal::platform().led();
  return (light != nullptr) ? static_cast<hal::FakeLed*>(light)->green() : 0;
}

uint8_t led_blue() {
  hal::ILed* light = hal::platform().led();
  return (light != nullptr) ? static_cast<hal::FakeLed*>(light)->blue() : 0;
}

uint32_t led_frames() {
  hal::ILed* light = hal::platform().led();
  return (light != nullptr) ? static_cast<hal::FakeLed*>(light)->frames() : 0;
}

bool led_pixel(uint32_t index, uint8_t& r, uint8_t& g, uint8_t& b) {
  hal::ILed* light = hal::platform().led();
  if (light == nullptr) return false;
  return static_cast<hal::FakeLed*>(light)->light(index, r, g, b);
}

// The bench's clock, moved by hand: a test that watches a five-second breath does not wait
// five seconds for it (docs/30, docs/85).
void advance_ms(int64_t delta) { hal::bench_advance_clock(delta); }

}  // namespace emu
