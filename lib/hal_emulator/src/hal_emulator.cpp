#include <chrono>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include "hal/hal.h"

#include "board/identity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emu/options.h"
#include "emu/gt911.h"
#include "emu/gui.h"
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

// **The canned answers of the fake wire**, one per address (0.88.6): a page is one answer and a chain of downloads is
// many, and the first key that appears in the address is the one that answers. The bodies are copies, so a test can
// build them in a local or in a static buffer of its own (docs/85).
namespace {
struct Answer {
  char key[96];
  char body[kHttpAnswerCap];
  int32_t status;
  bool used;
};
Answer g_answers[kMaxHttpAnswers];
uint8_t g_answer_count = 0;

const Answer* answer_for(const char* url) {
  if (url == nullptr) return nullptr;
  for (uint8_t i = 0; i < g_answer_count; ++i) {
    if (g_answers[i].used && strstr(url, g_answers[i].key) != nullptr) return &g_answers[i];
  }
  return nullptr;
}

// **An answer that does not fit is refused and not cut**: half a listing would be an install that fails for a reason
// that is not the one the test is about, and a body cut in the middle is a page that looks real (docs/85).
bool copy_into(char* out, size_t cap, const char* in) {
  const size_t len = strlen(in);
  if (len >= cap) return false;
  memcpy(out, in, len + 1);
  return true;
}
}  // namespace

bool http_canned(const char* key, const char* body, int32_t status) {
  if (key == nullptr || body == nullptr || g_answer_count >= kMaxHttpAnswers) return false;
  Answer answer = {};
  if (!copy_into(answer.key, sizeof(answer.key), key)) return false;
  if (!copy_into(answer.body, sizeof(answer.body), body)) return false;
  answer.status = status;
  answer.used = true;
  g_answers[g_answer_count] = answer;
  ++g_answer_count;
  return true;
}

void http_canned_clear() {
  for (uint8_t i = 0; i < kMaxHttpAnswers; ++i) g_answers[i] = Answer();
  g_answer_count = 0;
}

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
      // **The panel of the emulator is off unless the run asked for it**: an emulator nobody is touching does
      // not read a bus once a turn, and a test wants a board that answers nothing here (docs/85).
      touch(false),
      touch_scan_ms(0),
      // **The times of the hardware are not paid unless the run asks for them**: a test wants the model
      // and not the clock, and `main` turns them on for the binary —`--no-delay` leaves them off (docs/85).
      delays(false),
      // **A cold start unless the run says otherwise**: the RTC RAM of a model without a coin cell is
      // wiped, which is what unplugging the board does (docs/85).
      persist(false),
      // **The app's own code is CPU time and it is not free on the glass**: sixty nanoseconds an
      // instruction at 160 MHz is the model's number, and `main` turns it on with the rest of the times
      // (-1 is "the emulator's own number" and 0 is "free") (docs/85).
      lua_ns_160(-1),
      wifi_ms(300),
      wifi_absent(false),
      wifi_fail(false),
      refuse_why(nullptr),
      scan_count(1),
      prov_ms(2000),
      prov_ssid("casa"),
      prov_pass("secret"),
      prov_never(false),
      prov_fail(false),
      // **The page is the real one unless the run asks for the canned one** (docs/85).
      http_fake(false),
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
  // A name that is not one of the models is not a board this emulator can be, and the
  // option is checked when it is parsed; this is the belt for a caller that set the
  // struct by hand, and the default is the honest answer there too.
  for (uint32_t i = 0; i < board::models_count(); ++i) {
    const char* name = board::model(i);
    if (name != nullptr && strcmp(name, asked) == 0) return name;
  }
  return board::default_model();
}

namespace {
// The emulator has three keys: key 0 is the ATOM's only button and the Paper's
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

// **What a byte of the internal I2C costs**: the bus runs at 100 kHz and a byte is nine bits on it —the
// eight of the data and the acknowledge—, so ninety microseconds, which is the number the GT911's
// transactions and the FRAM's pages already come from (docs/50). The address, the start and the stop are
// part of the count of the bytes a transaction moves.
const uint32_t kI2cByteUs = 90;

// What a transaction of that bus costs, in microseconds.
uint32_t i2c_us(uint32_t bytes) { return bytes * kI2cByteUs; }

// **What the BM8563 moves when the hour is read or written**: the register and the seven bytes of the
// time, at ninety microseconds a byte (docs/50, docs/60).
const uint32_t kRtcBusBytes = 10;

// The hardware of this emulator is `emu::model()`, which is the model `--board` named: one list and
// not two, because an emulator that answers for another board teaches the wrong thing (docs/85).
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
  HostClock()
      : mono_start_(mono_ms()),
        epoch_base_(0),
        scale_(1),
        known_(false),
        pinned_epoch_(emu::options().now_epoch) {
    const emu::Options& opts = emu::options();
    scale_ = (opts.time_scale > 0) ? opts.time_scale : 1;
    if (opts.now_epoch > 0) {
      epoch_base_ = opts.now_epoch * 1000;
      known_ = true;
      return;
    }
    // **The hour a board comes back with**: a chip with a coin cell behind it kept it while the board
    // was off, and `--persist` is a board that was never unplugged at all, so the run starts at the
    // hour the run before left. A board with neither starts at zero —a board nobody has set— which is
    // what the emulator did before and what the hardware is (docs/50, docs/60, docs/85).
    if (opts.persist || emu::model().keeps_the_hour) {
      const int64_t kept = emu::load_clock_ms();
      if (kept > 0) {
        epoch_base_ = kept - elapsed_ms();
        known_ = true;
        // **The hour is read from a chip over the I2C**: seven registers behind an address, which is
        // what the board pays once at boot (docs/50, docs/60).
        if (emu::model().keeps_the_hour && opts.delays) pal::delay_us(i2c_us(kRtcBusBytes));
      }
    }
  }

  // **The hour of the emulator may be set after this clock exists** (0.152.5): the board has one clock and so has the
  // emulator —this object is built on the first turn of the process, which the comment on `host_clock` below explains—,
  // so a case of the suite that asks for nine in the morning asked *after* the first boot had already built it with
  // the default options, and its board "never synced": the time clock opened with no day at all. The options are read
  // here, where the hour is read, and only when they have changed, because an emulator moved with `emulator_advance_clock`
  // has to keep the hour it was moved to.
  int64_t epoch_ms() override {
    const int64_t wanted = emu::options().now_epoch;
    if (wanted != pinned_epoch_) {
      pinned_epoch_ = wanted;
      known_ = (wanted > 0);
      epoch_base_ = (wanted > 0) ? (wanted * 1000 - elapsed_ms()) : 0;
    }
    return known_ ? (epoch_base_ + elapsed_ms()) : 0;
  }
  int64_t monotonic_ms() override { return elapsed_ms(); }

  bool set_epoch_ms(int64_t ms) override {
    if (ms < 0) return false;
    epoch_base_ = ms - elapsed_ms();
    known_ = true;
    // **Writing the hour is writing a chip**: the board's `RtcClock` puts it in the BM8563 over the I2C
    // on every change of the epoch, and that is a transaction of ten bytes (docs/50, docs/60).
    if (emu::model().keeps_the_hour && emu::options().delays) {
      pal::delay_us(i2c_us(kRtcBusBytes));
    }
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
  // The `now_epoch` of the options this clock is holding, so a change of them is noticed in `epoch_ms` (0.152.5).
  int64_t pinned_epoch_;
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

// The emulator's clock, moved by hand. It is the only way a test can watch a five-second breath in a
// millisecond, and it moves **the same clock the kernel reads**, so an effect and the turn that
// paints it agree (docs/30, docs/85). It is here and not in `emu` because the clock is this
// translation unit's secret and the anonymous namespace is not reachable from outside.
void emulator_advance_clock(int64_t delta) { host_clock().advance_ms(delta); }

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

// **What writing a strip costs**: a WS2812 takes twenty-four bits at 1.25 us each —thirty microseconds a
// light— and then the line is held low so the chips latch the frame. The Atom has one light, the Matrix
// twenty-five, and a person sees the light appear when the strip has finished, not when the call was made
// (docs/50, docs/85).
const uint32_t kWs2812LightUs = 30;
const uint32_t kWs2812LatchUs = 80;

// **What the MPU6886 of the Matrix moves over the bus for one measurement**: the address of the chip and
// the register it is asked for, the address again and the fourteen bytes that come back (docs/50).
const uint32_t kImuReadBytes = 17;
// **What the SHT30 of the Paper takes to answer**: medium repeatability with clock stretching is four and
// a half milliseconds typical and six at most (SHT3x datasheet, tMEAS,m), and ten bytes of the bus —the
// command and the six that come back— are on top (docs/50, docs/85).
const uint32_t kSht30MeasureUs = 4500;
const uint32_t kSht30BusBytes = 10;
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

  // **A strip is written a bit at a time and nobody is talking to it while that happens**: twenty-four
  // bits in thirty microseconds, one light after another, and the line held low after the last one so
  // the chips latch it. It is the RMT of the chip doing it with interrupts off, which is why a person
  // sees the light appear at once and the system is a millisecond older (docs/50, docs/85).
  //
  // **And what latches is what the window shows** (0.179.17): the frame goes to the face of the board in the window
  // here and not when a pixel is written, because that is when a person sees it on the board (docs/85).
  void show() override {
    ++frames_;
    const uint32_t lights = emu::model().lights;
    emu::LightColour frame[kEmuLights] = {};
    for (uint32_t light = 0; light < lights && light < kEmuLights; ++light) {
      frame[light] = {lights_[light].red, lights_[light].green, lights_[light].blue};
    }
    emu::gui_show_lights(frame, static_cast<uint8_t>((lights > kEmuLights) ? kEmuLights : lights));
    if (!emu::options().delays) return;
    pal::delay_us(lights * kWs2812LightUs + kWs2812LatchUs);
  }

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
    // **The chip is behind a bus and the bus is not free**: the register address, the address of the
    // chip twice and the fourteen bytes of the measurement, at ninety microseconds a byte — a hundred
    // kilohertz, which is what this board's internal bus runs at (docs/50).
    if (opts.delays) pal::delay_us(i2c_us(kImuReadBytes));
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
    // **The chip measures while the bus waits for it**: medium repeatability with clock stretching is
    // 4.5 ms typical and 6 ms at most (SHT3x datasheet, tMEAS,m), and the command and the six bytes that
    // come back are ten bytes of a hundred-kilohertz bus more (docs/50, docs/85).
    if (opts.delays) pal::delay_us(kSht30MeasureUs + i2c_us(kSht30BusBytes));
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

  // The emulator imitates the band of the Paper: one compound dial of three keys on
  // the right face of the page, so an app that reads the positions can be tried on the PC
  // (docs/50, docs/85).
  Face face(uint32_t) override { return Face::kRight; }
  Spot spot(uint32_t key) override {
    // The emulator has the Paper's shape: a vertical dial of three keys (docs/85).
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
    // The emulator is not on a cable any more than the Paper can tell: the same false and the
    // same warning in docs/30.
    charging = false;
    return true;
  }

  bool sleep(SleepMode mode, int64_t at_epoch_ms) override {
    host_clock().note_sleep(at_epoch_ms);
    if (at_epoch_ms > 0) {
      const int64_t now = host_clock().epoch_ms();
      int64_t jump = (now > 0) ? (at_epoch_ms - now) : 0;
      // An emulator told to come back early does it here, which is the button of the glass and the supply
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

  // A board whose supply is cut stops being a board, and an emulator whose supply is cut stops being a
  // process: the answer is yes, the kernel leaves its loop, and the memories that are not a volume
  // go to their files on the way out, the same as they do after a deep sleep (docs/85). It is said
  // to stderr because it is this band talking about itself and not the system talking to a person.
  bool power_off() override {
    // A board that cannot cut its own supply says no here as well: the emulator answers what the model
    // does and not what the PC could do (docs/85).
    if (!emu::model().powers_off) return false;
    fprintf(stderr, "power: off (the emulator ends; rtc.bin and fram.bin are written)\n");
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
// milliseconds of simulated time to come up and then goes nowhere. The page is the
// exception: it is the one the address really answers, fetched by `curl`, because a
// page of this machine's network is exactly what a person wants to see on the glass
// (docs/85). `--http-fake` puts the canned page back.
class HostNet : public INet {
 public:
  ~HostNet() override {
    if (http_op_ != 0) http_release(http_op_);
  }

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

  // Connects with what provisioning left behind, which is the board's own store: the pair is in the
  // key-value store and not in this object, so a run that starts again on the same state directory
  // remembers the network it was told (docs/70, docs/85).
  bool wifi_start_stored() override {
    if (!provisioned_ && !recall()) return false;
    return wifi_start(stored_ssid_, stored_pass_);
  }

  bool wifi_scan_start() override {
    if (emu::options().wifi_absent) return false;
    // A radio that turns the scan down and says why, for the emulator of a board whose radio did not come
    // up (0.118.10, docs/85).
    if (emu::options().refuse_why != nullptr) return false;
    scanning_ = true;
    scan_started_ms_ = mono_ms();
    return true;
  }

  // The words of the hardware, when it has them (docs/30): empty is a radio that is really busy.
  const char* last_error() const override {
    return (emu::options().refuse_why != nullptr) ? emu::options().refuse_why : "";
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
    if (out == nullptr || cap == 0) return false;
    if (!provisioned_ && !recall()) return false;
    if (stored_ssid_[0] == '\0') return false;
    snprintf(out, cap, "%s", stored_ssid_);
    return true;
  }

  // **The credentials go to the key-value store, as the board's do**: the driver keeps the pair in its
  // own namespace and the system keeps the name it was given, so `wifi` on the next run answers the
  // network that was added in this one (docs/50, docs/70, docs/85).
  bool wifi_store(const char* ssid, const char* pass) override {
    if (ssid == nullptr || ssid[0] == '\0') return false;
    snprintf(stored_ssid_, sizeof(stored_ssid_), "%s", ssid);
    snprintf(stored_pass_, sizeof(stored_pass_), "%s", (pass != nullptr) ? pass : "");
    provisioned_ = true;
    remember();
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
    if (prov_running_ || prov_provisioned()) return false;
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
    // What provisioning received is what a person expects to find after a restart, and on the board
    // the driver writes it to the flash for the next boot (docs/50, docs/85).
    remember();
    return ProvState::kDone;
  }

  bool prov_stop() override {
    prov_running_ = false;
    return true;
  }

  // **A board with a network in its store has been provisioned**, whatever run put it there: on the
  // glass the question is answered by the record in the flash and not by a flag of the run (docs/50,
  // docs/85).
  bool prov_provisioned() override { return provisioned_ || recall(); }

  bool prov_forget() override {
    provisioned_ = false;
    stored_ssid_[0] = '\0';
    stored_pass_[0] = '\0';
    state_ = NetState::kOff;
    // **The name goes, the driver keeps its blob**: that is what `wifi forget` does on the board —
    // `own_record_erase()` and nothing else— and what the system trusts is the name, so the network is
    // forgotten even though the password is still in the flash (docs/50, docs/85).
    hal::IKeyValue* kv = hal::emu_nvs();
    if (kv != nullptr) kv->erase(wifi_namespace(), wifi_ssid_key());
    return true;
  }

  // **The page is fetched for real unless the run asked for the canned one**: `http_get_start`
  // hands the URL to a child of this emulator and returns at once, and the body and the status are
  // read when that child is done. The child is `curl` and not the board: what the kernel measures
  // is still the queue, the deadline and the codes (docs/85).
  uint32_t http_get_start(const char* url, const HttpOpts& opts) override {
    if (emu::options().wifi_absent || url == nullptr || url[0] == '\0') return 0;
    if (opts.max_bytes == 0 && opts.sink == nullptr) return 0;
    if (strlen(url) >= sizeof(http_url_)) return 0;
    if (http_op_ != 0) return 0;
    snprintf(http_url_, sizeof(http_url_), "%s", url);
    http_max_bytes_ = opts.max_bytes;
    http_sink_ = opts.sink;
    http_sink_path_[0] = '\0';
    if (opts.sink != nullptr && opts.sink_path != nullptr) {
      snprintf(http_sink_path_, sizeof(http_sink_path_), "%s", opts.sink_path);
    }
    if (http_sink_ != nullptr && http_sink_path_[0] == '\0') return 0;
    http_started_ms_ = mono_ms();
    http_real_ = !emu::options().http_fake;
    if (http_real_) {
      // **An address the board's client does not speak is a request that fails and not a request
      // that is never made**: `esp_http_client` opens and comes back with an error, so this does
      // too — `E_HTTP` and not a radio that refuses the task (docs/85).
      http_bad_scheme_ = !web_url(url);
      if (!http_bad_scheme_ && !real_start(url, opts)) {
        http_real_ = false;
        return 0;
      }
    }
    http_op_ = 1;
    return http_op_;
  }

  bool http_poll(uint32_t op, bool& done, HttpResult& out) override {
    if (op == 0 || op != http_op_) return false;
    if (http_real_) return real_poll(done, out);
    if (mono_ms() - http_started_ms_ < emu::options().http_ms) return true;
    done = true;
    if (emu::options().http_fail) {
      out.error = HttpError::kConnect;
      return true;
    }
    char text[512];
    const char* body = emu::options().http_body;
    // **A canned answer of its own wins over the one page of the options**: it is what lets a test walk a chain of
    // downloads, where the listing of the app, the listing of a folder and every file are four different answers from
    // the same wire (docs/85).
    const emu::Answer* answered = emu::answer_for(http_url_);
    int32_t status = emu::options().http_status;
    if (answered != nullptr) {
      body = answered->body;
      status = answered->status;
    }
    if (body == nullptr) {
      snprintf(text, sizeof(text), "page for %s", http_url_);
      body = text;
    }
    const size_t len = strlen(body);
    // **A page that does not fit goes to the volume the kernel picked**: the whole body, from its
    // first byte, which is what makes the file worth opening afterwards (docs/30, docs/85).
    if (http_sink_ != nullptr && (http_max_bytes_ == 0 || len > http_max_bytes_)) {
      out.status = status;
      if (!sink_write(body, static_cast<uint32_t>(len))) {
        out.error = HttpError::kStorage;
        return true;
      }
      out.saved = true;
      out.body_len = static_cast<uint32_t>(len);
      return true;
    }
    // **A page that goes on and has no file to go to is kept up to the last byte that fits**, which is
    // what the board does with the same buffer (`http_fetch` in lib/hal_esp32/src/net.cpp): the caller
    // gets the status, the beginning of the page and `kTooBig`, and not an empty answer (docs/30).
    const size_t keep = (len > http_max_bytes_) ? http_max_bytes_ : len;
    http_body_ = static_cast<char*>(malloc(keep + 1));
    if (http_body_ == nullptr) {
      out.error = HttpError::kNoMemory;
      return true;
    }
    memcpy(http_body_, body, keep);
    http_body_[keep] = '\0';
    out.status = status;
    out.body = http_body_;
    out.body_len = static_cast<uint32_t>(keep);
    if (len > http_max_bytes_) out.error = HttpError::kTooBig;
    return true;
  }

  void http_release(uint32_t op) override {
    if (op == 0 || op != http_op_) return;
    free(http_body_);
    http_body_ = nullptr;
    // **The kernel gave up on this page, or the run is over**: the download is the emulator's and it
    // stops with the answer, or a `curl` nobody reads would go on writing a file. SIGTERM first and
    // SIGKILL if the tool is stuck, because a turn of the emulator never waits on a dead page (docs/85).
    if (http_pid_ > 0) {
      kill(http_pid_, SIGTERM);
      int status = 0;
      for (int tries = 0; tries < 20; ++tries) {
        if (waitpid(http_pid_, &status, WNOHANG) != 0) {
          http_pid_ = -1;  // reaped, or gone without a word: either way there is nobody to wait for
          break;
        }
        usleep(10000);
      }
      if (http_pid_ > 0) {
        kill(http_pid_, SIGKILL);
        while (waitpid(http_pid_, &status, 0) < 0 && errno == EINTR) {
        }
        http_pid_ = -1;
      }
    }
    if (http_body_path_[0] != '\0') unlink(http_body_path_);
    if (http_meta_path_[0] != '\0') unlink(http_meta_path_);
    http_body_path_[0] = '\0';
    http_meta_path_[0] = '\0';
    http_real_ = false;
    http_bad_scheme_ = false;
    http_sink_ = nullptr;
    http_sink_path_[0] = '\0';
    http_op_ = 0;
  }

 private:
  // **The page is bigger than any page a board carries**: the file `curl` writes is capped so a
  // link to a film does not fill this machine's temporary directory, and the bytes the caller asked
  // for are the ones that are read out of it. **A page that goes to a volume is bounded by the volume**
  // and not by a number of this file: the board refuses it when it does not fit where it was going, and
  // the emulator asks the unit how much room it has —with a gigabyte as a lid, because a filesystem reports
  // its free space in blocks and a link to a film is not a page (docs/85).
  static const unsigned kRealBodyCeiling = 4u << 20;
  static const unsigned long long kRealFileCeiling = 1ull << 30;
  // What `curl` exits with when it is not on this machine at all.
  static const int kCurlMissing = 127;

  // The temporary file of one download, made by `mkstemp` so two runs do not share a name.
  static bool make_temp(char* path, size_t cap) {
    const char* dir = getenv("TMPDIR");
    if (dir == nullptr || dir[0] == '\0') dir = "/tmp";
    const int written = snprintf(path, cap, "%s/lunok-http-XXXXXX", dir);
    if (written <= 0 || static_cast<size_t>(written) >= cap) {
      path[0] = '\0';
      return false;
    }
    const int fd = mkstemp(path);
    if (fd < 0) {
      path[0] = '\0';
      return false;
    }
    close(fd);
    return true;
  }

  // **The request is a process of this machine's and the board waits for it in its own time**:
  // `curl` writes the body to one file and the status code to another, so nothing of the download
  // has to fit in this process and the poll is a `waitpid` (docs/85).
  bool real_start(const char* url, const HttpOpts& opts) {
    if (!make_temp(http_body_path_, sizeof(http_body_path_))) return false;
    if (!make_temp(http_meta_path_, sizeof(http_meta_path_))) {
      unlink(http_body_path_);
      http_body_path_[0] = '\0';
      return false;
    }
    char seconds[16];
    snprintf(seconds, sizeof(seconds), "%u", static_cast<unsigned>((opts.timeout_ms + 999) / 1000));
    unsigned long long limit = kRealBodyCeiling;
    if (opts.sink != nullptr) {
      uint64_t total = 0;
      uint64_t free_bytes = 0;
      limit = (opts.sink->space(total, free_bytes) && free_bytes > 0) ? free_bytes : kRealFileCeiling;
      if (limit > kRealFileCeiling) limit = kRealFileCeiling;
    }
    char ceiling[24];
    snprintf(ceiling, sizeof(ceiling), "%llu", limit);
    const int meta_fd = open(http_meta_path_, O_WRONLY | O_TRUNC);
    const int null_fd = open("/dev/null", O_WRONLY);
    if (meta_fd < 0 || null_fd < 0) {
      if (meta_fd >= 0) close(meta_fd);
      if (null_fd >= 0) close(null_fd);
      unlink(http_body_path_);
      unlink(http_meta_path_);
      http_body_path_[0] = '\0';
      http_meta_path_[0] = '\0';
      return false;
    }
    const pid_t pid = fork();
    if (pid < 0) {
      close(meta_fd);
      close(null_fd);
      unlink(http_body_path_);
      unlink(http_meta_path_);
      http_body_path_[0] = '\0';
      http_meta_path_[0] = '\0';
      return false;
    }
    if (pid == 0) {
      // Between `fork` and `exec` only what a signal handler may call: no allocation, no stdio.
      // `-q` is the first argument so a `~/.curlrc` of this machine cannot change the answer.
      dup2(meta_fd, STDOUT_FILENO);
      dup2(null_fd, STDERR_FILENO);
      close(meta_fd);
      close(null_fd);
      // **The address goes in through `--url`**, so a string that begins with a dash is a page and
      // not an option of the tool (docs/85).
      execlp("curl", "curl", "-q", "-sS", "-L", "--globoff", "--max-redirs", "10", "--max-time",
             seconds, "--max-filesize", ceiling, "-o", http_body_path_, "-w", "%{http_code}",
             "--url", url, static_cast<char*>(nullptr));
      _exit(kCurlMissing);
    }
    close(meta_fd);
    close(null_fd);
    http_pid_ = pid;
    return true;
  }

  // The cause, said as the board says it and not as `curl` says it (docs/30): a certificate that is
  // not under the roots of this machine is `kTls`, a server that is not there is `kConnect`, and a page
  // that went past the room is `kTooBig` when it was to be held in memory and `kStorage` when it was
  // going to a volume — the board says the same two things (docs/30, docs/85).
  static HttpError curl_cause(int status, bool to_file) {
    if (!WIFEXITED(status)) return HttpError::kConnect;
    switch (WEXITSTATUS(status)) {
      case 35: case 51: case 53: case 54: case 58: case 59: case 60: case 64: case 66: case 77:
      case 80: case 82: case 83: case 90: case 91:
        return HttpError::kTls;
      case 63:
        return to_file ? HttpError::kStorage : HttpError::kTooBig;
      default:
        return HttpError::kConnect;
    }
  }

  // The status code `curl` was told to leave in its `-w`: zero when the child never got one.
  static int read_code(const char* path) {
    FILE* file = fopen(path, "r");
    if (file == nullptr) return 0;
    char text[16] = {0};
    const size_t got = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[got] = '\0';
    return atoi(text);
  }

  // http and https are what the board's client speaks, and `curl` would happily read a file of this
  // machine: the scheme is what is asked, and the tool is never given a URL that is not a page.
  static bool web_url(const char* url) {
    return strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0;
  }

  bool real_poll(bool& done, HttpResult& out) {
    if (http_bad_scheme_) {
      http_bad_scheme_ = false;
      done = true;
      out.error = HttpError::kConnect;
      return true;
    }
    int status = 0;
    pid_t got = 0;
    do {
      got = waitpid(http_pid_, &status, WNOHANG);
    } while (got < 0 && errno == EINTR);
    if (got == 0) return true;  // still on the wire
    http_pid_ = -1;
    done = true;
    if (got < 0) {
      out.error = HttpError::kConnect;
      return true;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      out.error = curl_cause(status, http_sink_ != nullptr);
      if (out.error == HttpError::kTooBig) {
        // The page went past the ceiling, so there is no first part to give: the bytes above the
        // ceiling were never written anywhere (docs/85).
        out.status = read_code(http_meta_path_);
      } else if (WIFEXITED(status) && WEXITSTATUS(status) == kCurlMissing) {
        warn_missing_curl();
      }
      return true;
    }
    const int code = read_code(http_meta_path_);
    if (code < 100) {
      out.error = HttpError::kConnect;
      return true;
    }
    out.status = code;
    FILE* file = fopen(http_body_path_, "rb");
    if (file == nullptr) {
      out.error = HttpError::kConnect;
      return true;
    }
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    const uint32_t total = (size > 0) ? static_cast<uint32_t>(size) : 0;
    // **The page goes to the volume when it does not fit, and not a byte of it stays here**: the file
    // the caller will open has to have the page from its first byte, or it is a file nobody asked for
    // (docs/30, docs/85).
    if (http_sink_ != nullptr && (http_max_bytes_ == 0 || total > http_max_bytes_)) {
      fclose(file);
      uint32_t written = 0;
      if (!sink_copy(http_body_path_, written)) {
        out.error = HttpError::kStorage;
        return true;
      }
      out.saved = true;
      out.body_len = written;
      return true;
    }
    const bool too_big = total > http_max_bytes_;
    const uint32_t keep = too_big ? http_max_bytes_ : total;
    char* body = static_cast<char*>(malloc(keep + 1));
    if (body == nullptr) {
      fclose(file);
      out.error = HttpError::kNoMemory;
      return true;
    }
    const size_t read = (keep > 0) ? fread(body, 1, keep, file) : 0;
    fclose(file);
    body[read] = '\0';
    http_body_ = body;
    out.body = body;
    out.body_len = static_cast<uint32_t>(read);
    if (too_big) out.error = HttpError::kTooBig;
    return true;
  }

  // **The page goes whole into the volume the kernel resolved**: one open, chunks of four kilobytes —
  // the write that the volume's timing is charged for — and a close that says whether it all went in.
  // A write that fails removes the file, because half a page is a lie about what the server sent
  // (docs/30, docs/75).
  bool sink_write(const char* data, uint32_t len) {
    hal::FileSlot slot;
    if (!http_sink_->open(http_sink_path_, hal::OpenMode::kWrite, slot)) return false;
    const int32_t wrote = (len > 0) ? http_sink_->write(slot, data, len) : 0;
    const bool closed = http_sink_->close(slot);
    const bool whole = (len == 0) || (wrote == static_cast<int32_t>(len));
    if (!closed || !whole) {
      http_sink_->remove(http_sink_path_);
      return false;
    }
    return true;
  }

  bool sink_copy(const char* path, uint32_t& written) {
    FILE* file = fopen(path, "rb");
    if (file == nullptr) return false;
    hal::FileSlot slot;
    if (!http_sink_->open(http_sink_path_, hal::OpenMode::kWrite, slot)) {
      fclose(file);
      return false;
    }
    bool ok = true;
    written = 0;
    size_t got = 0;
    while ((got = fread(http_chunk_, 1, sizeof(http_chunk_), file)) > 0) {
      if (http_sink_->write(slot, http_chunk_, static_cast<uint32_t>(got)) !=
          static_cast<int32_t>(got)) {
        ok = false;
        break;
      }
      written += static_cast<uint32_t>(got);
    }
    if (ferror(file)) ok = false;
    fclose(file);
    if (!http_sink_->close(slot)) ok = false;
    if (!ok) {
      http_sink_->remove(http_sink_path_);
      written = 0;
    }
    return ok;
  }

  static void warn_missing_curl() {
    static bool said = false;
    if (said) return;
    said = true;
    fprintf(stderr,
            "lunokOS emulator: `http` needs `curl` on this machine and it was not found: install it, "
            "or pass --http-fake for the canned page\n");
  }
  // **The name is the system's record and the pair is the driver's**, exactly as on the glass: the
  // system trusts `lunoknet/ssid` and the password lives in the blob of the driver, which on the board
  // is another namespace of the same flash (docs/50, docs/70, docs/85).
  static const char* wifi_namespace() { return "lunoknet"; }
  static const char* wifi_ssid_key() { return "ssid"; }
  static const char* driver_namespace() { return "wifidrv"; }
  static const char* driver_pass_key() { return "pass"; }

  void remember() {
    hal::IKeyValue* kv = hal::emu_nvs();
    if (kv == nullptr) return;
    kv->set(wifi_namespace(), wifi_ssid_key(), stored_ssid_, strlen(stored_ssid_));
    kv->set(driver_namespace(), driver_pass_key(), stored_pass_, strlen(stored_pass_));
  }

  // The pair the run before left, or false when the store has no name in it. A password that is not
  // there is an open network, which is a pair with one half and not an error.
  bool recall() {
    hal::IKeyValue* kv = hal::emu_nvs();
    if (kv == nullptr) return false;
    size_t len = 0;
    char ssid[sizeof(stored_ssid_)] = {0};
    if (!kv->get(wifi_namespace(), wifi_ssid_key(), ssid, sizeof(ssid) - 1, len) || len == 0) {
      return false;
    }
    ssid[len] = '\0';
    size_t pass_len = 0;
    char pass[sizeof(stored_pass_)] = {0};
    if (kv->get(driver_namespace(), driver_pass_key(), pass, sizeof(pass) - 1, pass_len)) {
      pass[pass_len] = '\0';
    }
    snprintf(stored_ssid_, sizeof(stored_ssid_), "%s", ssid);
    snprintf(stored_pass_, sizeof(stored_pass_), "%s", pass);
    provisioned_ = true;
    return true;
  }

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
  // The real fetch: the child of this emulator that is asking for the page, and the two files it
  // leaves behind (docs/85).
  bool http_real_ = false;
  bool http_bad_scheme_ = false;
  // Where a page that does not fit goes, and the chunk the copy goes through (docs/30, docs/85).
  hal::IStorage* http_sink_ = nullptr;
  char http_sink_path_[160] = {0};
  char http_chunk_[4096] = {0};
  pid_t http_pid_ = -1;
  char http_body_path_[256] = {0};
  char http_meta_path_[256] = {0};
};

class EmulatorPlatform : public IPlatform {
 public:
  const char* id() const override { return board::id(); }
  const char* family() const override { return board::family(); }
  bool emulator() const override { return board::emulator(); }

  // One board, and it is the one this emulator was told to be: an app is accepted or
  // refused here exactly as it would be on that board, which is the only way a manifest
  // tried on the PC means anything on the glass (docs/40, docs/85). The default of
  // IPlatform is already this answer, so there is nothing to override.

  const Caps& caps() const override {
    // The emulator can be told to be another board between two boots —`use_board` in the tests does
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
      // line an app that asks `sys.caps().display` was told no on an emulator that had one, while the view
      // behind it was there and the boot line said so (found running `layers` on the emulator, 1/11).
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
  // What the model's board carries as extra memory, and **what is left of it**: the blocks come from
  // the PC's heap, but their books are the board's —the ceilings the run set from the model— so `mem`
  // and `sys.heap()` say what the glass would say and a block that does not fit is refused (docs/75,
  // docs/85). A run with no ceilings counts nothing and answers the total, which is what it did before
  // the books existed.
  ExtraMemory extra_memory() const override {
    ExtraMemory out;
    out.chip_bytes = emu::model().psram_chip_kb * 1024u;
    out.total_bytes = emu::model().psram_kb * 1024u;
    const pal::MemoryLeft left = pal::memory_left(true);
    const uint32_t counted = static_cast<uint32_t>(left.free_bytes);
    out.free_bytes = left.counted ? ((counted > out.total_bytes) ? out.total_bytes : counted)
                                  : out.total_bytes;
    return out;
  }

  IClock* clock() override { return &host_clock(); }
  IStorage* storage(const char* mount) override { return emu_storage(mount); }
  IKeyValue* nvs() override { return emu_nvs(); }
  INet* net() override {
    if (emu::options().wifi_absent) return nullptr;
    static HostNet instance;
    return &instance;
  }
  IPower* power() override {
    static FakePower instance;
    return &instance;
  }
  ISurface* display() override {
    // **The glass of the model**: the Paper draws on a panel and the ATOMs have no screen, so an option
    // cannot make one appear — an emulator of an ATOM with a glass is a board nobody can buy. The binary
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
    // **The instructions of the app at the frequency the board is set to**: an emulator that is not paying the
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
  // Which model the cached capabilities are about. A board is a board and the emulator changes which
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

// The emulator's clock, moved by hand: a test that watches a five-second breath does not wait
// five seconds for it (docs/30, docs/85).
void advance_ms(int64_t delta) { hal::emulator_advance_clock(delta); }

}  // namespace emu
