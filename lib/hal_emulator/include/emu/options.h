// Options the emulator accepts on the command line. See docs/85-emulador.md.
#ifndef LUNOK_EMU_OPTIONS_H
#define LUNOK_EMU_OPTIONS_H

#include <stdint.h>

namespace emu {

struct Options {
  const char* fs_dir;    // where the emulated disks live
  const char* script;    // batch file, or nullptr
  const char* run_app;   // app to start, or nullptr
  int64_t now_epoch;     // 0 = no epoch at all (source "none"), like a board that
                         //     never synced; anything else is the start time
  int32_t mem_cap_kb;    // 0 = board default
  uint32_t time_scale;   // 1 = real time
  uint32_t seed;         // 0 = seed from the system
  bool verbose;          // mirror the log to the console
  // The network of this band is a fake and it says so (docs/85): connecting
  // takes wifi_ms of simulated time and goes nowhere. `wifi_absent` is a board
  // with no radio at all, `wifi_fail` a network that refuses.
  int32_t wifi_ms;
  bool wifi_absent;
  bool wifi_fail;
  // How many networks the fake radio sees in a scan. Zero is a board in a field
  // and it is the case that broke the promise: an empty scan answers 0 (docs/85).
  int32_t scan_count;
  // Provisioning of this band is a fake too: the access point is up for prov_ms,
  // then the network it "receives" is the one these options say unless
  // prov_never is set, which is a board nobody ever provisions (docs/85).
  int32_t prov_ms;
  const char* prov_ssid;
  const char* prov_pass;
  bool prov_never;
  bool prov_fail;
  // The page of this band is a fake too: after http_ms it answers http_status with
  // http_body (or a line naming the address when nothing was set), and `http_fail`
  // is a server that never answers (docs/85).
  int32_t http_ms;
  int32_t http_status;
  const char* http_body;
  bool http_fail;
  // The IMU of this band is a fake like the rest: it answers what these options say,
  // and `imu_fail` is a chip that does not answer at all (docs/85). Acceleration in
  // g, turn rate in degrees per second, temperature in degrees Celsius.
  bool imu_fail;
  float imu_ax;
  float imu_ay;
  float imu_az;
  float imu_gx;
  float imu_gy;
  float imu_gz;
  float imu_celsius;

  Options();
};

const Options& options();
void set_options(const Options& opts);

// The button of this band is the test's to press: there is no finger on a PC, and a
// scripted press is what makes the path through the kernel runnable (docs/85).
void press_button(bool down);
bool button_down();

// What the fake light is showing. A test asks the band and not the kernel, so a
// policy that never paints is caught (docs/85).
uint8_t led_red();
uint8_t led_green();
uint8_t led_blue();
uint32_t led_frames();
// One light of the fake panel, for the tests of a board with more than one. False
// when there is no such light (docs/85).
bool led_pixel(uint32_t index, uint8_t& r, uint8_t& g, uint8_t& b);

}  // namespace emu

#endif  // LUNOK_EMU_OPTIONS_H
