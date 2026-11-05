// Options the emulator accepts on the command line. See docs/85-emulador.md.
#ifndef LUNOK_EMU_OPTIONS_H
#define LUNOK_EMU_OPTIONS_H

#include <stdint.h>

// **The disk this binary was built to read**, given by its environment (`platformio.ini`): every build has its
// own, `build/disk/<env>`, which `tools/make_disk.py` assembles from `data/` and the apps of `apps/` (docs/80).
// The bench and the tests are two builds and do not share a disk, so one cannot write over what the other is
// about to read (docs/85).
#ifndef LUNOK_DISK_DIR
#define LUNOK_DISK_DIR "build/disk"
#endif

namespace emu {

struct Options {
  // Where the emulator keeps the memories that are **not a volume**: the key-value store
  // (`<state_dir>/nvs`, one file per key) and, when they exist, the RTC RAM and the FRAM. nullptr
  // means "the same directory as the disk", which is what a test wants: everything of one run in
  // one place it can wipe (docs/85).
  const char* state_dir;
  // The directory that **is** the emulated flash volume, or nullptr for `<fs_dir>/littlefs`. **nullptr is
  // the default here and the disk of the build is the binary's default** (`main_host`, `LUNOK_DISK_DIR`):
  // `data/` plus the apps this build carries, assembled by `tools/make_disk.py`, so what a person edits is
  // what the bench runs and nothing has to be copied. A test builds its own options and wants the volume
  // next to `fs_dir`, which is where it copied its fixtures — with the build's disk as the default of this
  // struct, every suite read the repository's disk and its own copy was never looked at (found the 4/11
  // with `test_img`: five pictures that were there and a bench that said they were not) (docs/85).
  const char* flash_dir;
  const char* fs_dir;    // where the emulated disks live
  // Which board this bench is, by the name the board has in platformio.ini. nullptr is
  // the default (`board::default_model()`, the ATOM): an app asks `sys.board` and gets a
  // board that exists, never a name invented by the system (docs/50, docs/85).
  const char* board;
  // The card in the slot: the directory that is its volume, or nullptr for a board with
  // an empty slot —which is a board with no `/sd` at all, the same as the real one
  // (docs/70, docs/85). A card's data is somebody's, so the bench never invents one.
  const char* sd_dir;
  // What the fake battery reads, in millivolts, or 0 for a board with no battery to
  // report. The percentage is the curve of a cell and not a second number to set, so the
  // bench cannot say 3,9 V and 100 % at once (docs/50, docs/85).
  int32_t battery_mv;
  // **A battery lent by the machine the bench runs on** (`--battery-host`): its percentage on the curve
  // of the board's cell, which is 1150 mAh and full at 4,2 V (docs/85). Off by default, because a test
  // that changed its answer with the hardware of whoever runs it is not a test.
  bool battery_from_host;
  const char* script;    // batch file, or nullptr
  const char* run_app;   // app to start, or nullptr
  int64_t now_epoch;     // 0 = no epoch at all (source "none"), like a board that
                         //     never synced; anything else is the start time
  int32_t mem_cap_kb;    // 0 = board default
  // How much earlier than its hour the bench comes back from a sleep, in milliseconds: zero is the
  // honest clock and anything else is a sleep that was cut short —a finger on the button, a cable
  // that blinked— so the case where the board never reaches its hour can be tried on the PC
  // (docs/60, docs/85).
  int32_t wake_early_ms;
  // The speed of the serial port, in baud: the console is stdin/stdout, but the board's UART is a
  // wire at 115200 and a screen that appears instantly here is a screen a person waits a third of a
  // second for there (docs/85). -1 is the board's speed, 0 is no wait at all, which is what a test
  // wants — its console is a file, not a wire.
  int32_t baud;
  uint32_t time_scale;   // 1 = real time
  uint32_t seed;         // 0 = seed from the system
  bool verbose;          // mirror the log to the console
  // A board with a glass. **Off in `Options()`, which is what a test builds**: the binary turns it on
  // when the model has one (`Model::glass`, the Paper) and `--no-display` takes it away, because a bench
  // of a Paper with no screen is a board nobody can buy (docs/85). The glass is the Paper's: 960x540
  // and four bits per pixel.
  bool display;
  // **A window with the glass in it**, for the runs that are looked at instead of read: it needs the
  // glass (`--display` is turned on with it) and it changes nothing of what the bench simulates —the
  // window is a way to look, and the ink is still not there (docs/85).
  bool gui;
  // **How big the page is in the window** (`--gui-scale`): zero is the smallest scale that fits the
  // screen, which is what a window nobody sized wants, and 1 to 4 is the size the run asked for even when
  // the page would fit bigger — 2 is the page at half (docs/85).
  uint8_t gui_scale;
  // **A picture of the glass on this machine's disk** (`--dump <file>`, a PGM of eight bits per pixel): every
  // paint writes the whole page over the file, so the last one is the screen the run ended with. It is how the
  // look of this system is looked at from a session that has no window, and it changes nothing of what the
  // bench simulates (docs/85).
  const char* dump;
  // **Whether the glass moves ink or switches pixels.** On —one— a paint takes what the mode takes, a
  // `du` keeps a quarter of the page before it and the traces add up; off —zero— a paint is instant and
  // exact, which is what a test wants and what the bench was before this (docs/85). `main` turns it on.
  int32_t ink;
  // **A board with a touch panel**: the same as the glass —off in `Options()` and on in the binary when
  // the model has one (`Model::touch`, the Paper)— and `--no-touch` takes it away. Without it the model's
  // panel is a chip that does not answer, and `sys.caps().touch` says so (docs/50, docs/85).
  bool touch;
  // How often the panel reports, in milliseconds: **0 is the chip's own** (10 ms, the datasheet's and
  // a measurement the board still owes, docs/95 E-6). A larger number is a chip slower than the turn of
  // the kernel, which is the hardware problem a caller that polls too fast has to survive (docs/85).
  int32_t touch_scan_ms;
  // **Whether the bench pays the time the hardware takes.** On —the binary— a read of the disk sleeps
  // what the flash sleeps, a paint sleeps the bus and the ink, a read of the panel sleeps its three
  // transactions and the console goes out at the speed of the UART: that is what makes a run with
  // `--gui` cost what the board costs. Off —`--no-delay`, and every test— the model is the same one and
  // only the clock is skipped, which is what a test that is not about time wants (docs/85).
  bool delays;
  // **A board that was never unplugged** (`--persist`): the run starts where the run before it left
  // the board. The key-value store is flash and is kept either way, the FRAM is not volatile and is
  // kept too, but the **RTC RAM** dies with the power —so a model without a coin cell starts it at
  // zero— and the hour of a board without a clock chip dies with it as well. With this on, the RTC
  // RAM and the hour are the ones of the run before, which is what a reset does to the chip and what
  // a cold start does not (docs/60, docs/85).
  bool persist;
  // **What one instruction of the app costs at 160 MHz**, in nanoseconds: zero is a bench that does not
  // price the app's own code. It is the number that makes a loop cost here what it costs on the glass,
  // and it is the model's —a measurement of this board's interpreter is still owed (docs/95)—
  // (docs/85).
  int32_t lua_ns_160;
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
  // **The page of this band is the one the address really answers**: the bench hands the URL to
  // `curl` and the body and the status come back from the wire, with the certificate checked
  // against the roots of this machine (docs/85). `http_fake` puts the canned page back — and the
  // other four options are that same canned page: after http_ms it answers http_status with
  // http_body (or a line naming the address when nothing was set), and `http_fail` is a server
  // that never answers.
  bool http_fake;
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
  // The air, for the model that has an SHT30: the two numbers a reading returns, and
  // `env_fail` for a chip that does not answer at all (docs/85).
  bool env_fail;
  float env_celsius;
  float env_humidity;

  Options();
};

const Options& options();
void set_options(const Options& opts);

// **A canned answer for one address** (0.88.6): `http_fake` puts the same page back for every URL, which is a page and
// not a chain —an install of an app asks for the listing of the app, the listing of each of its folders and every one of
// its files, and each of those is its own answer—. The answers are asked in the order they were given and the first
// whose `key` appears in the address wins, body and status included; with none of them the canned page of the options is
// what answers. A body of up to `kHttpAnswerCap` bytes is copied, so a test can build it in a local. Cleared with
// `http_canned_clear`, which is what a test does before its own chain (docs/85).
// **A body of the fake wire fits a listing of a real app**: the answer of the contents API for an app of ten folders is
// about six hundred bytes, and the cap used to be five hundred and twelve —a listing that does not fit is refused and
// not cut, so the cap is the one that has to grow (docs/30, docs/85).
const uint16_t kHttpAnswerCap = 1024;
// **As many answers as an app needs to be installed**: the listing of the app, a listing per folder and a body per file.
// The app of the test of the whole chain has ten folders and twelve files, which is twenty-three, and the list is of
// twenty-four so that the test is not the ceiling (docs/85).
const uint8_t kMaxHttpAnswers = 24;
bool http_canned(const char* key, const char* body, int32_t status);
void http_canned_clear();

// The keys of this band are the test's to press: there is no finger on a PC, and a
// scripted press is what makes the path through the kernel runnable (docs/85).
// Key 0 is the main one —the ATOM's only button, the Paper's middle key— and the
// bench has three, so a board with more than one can be tried on the PC.
void press_button(bool down);            // key 0
void press_key(int key, bool down);      // 0, 1 or 2
bool button_down();                      // key 0
bool key_down(int key);

// What the fake light is showing. A test asks the band and not the kernel, so a
// policy that never paints is caught (docs/85).
uint8_t led_red();
uint8_t led_green();
uint8_t led_blue();
uint32_t led_frames();
// One light of the fake panel, for the tests of a board with more than one. False
// when there is no such light (docs/85).
bool led_pixel(uint32_t index, uint8_t& r, uint8_t& g, uint8_t& b);

// Moves the bench's clock forward without waiting. It is the only way a test can watch a
// five-second breath in a millisecond, and it moves **the same clock the kernel reads**,
// so the effect and the turn that paints it agree (docs/30, docs/85).
void advance_ms(int64_t delta);

}  // namespace emu

#endif  // LUNOK_EMU_OPTIONS_H
