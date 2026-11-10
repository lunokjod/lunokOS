// Entry point of the emulator.
//
// PlatformIO defines UNIT_TEST (and PIO_UNIT_TESTING) while running `pio test`;
// then the test framework brings its own main() and this one steps aside.
#if !defined(UNIT_TEST) && !defined(PIO_UNIT_TESTING)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "emu/gui.h"
#include "emu/surface.h"
#include "emu/hostbattery.h"
#include "emu/options.h"
#include "emu/memories.h"
#include "emu/model.h"
#include "hal/hal.h"
#include "kernel/kernel.h"
#include "kernel/shell.h"
#include "luabind/luabind.h"
#include "pal/pal.h"

#include "board/identity.h"

namespace {

const size_t kLineBytes = 128;

// **What the heap of the emulator costs**: one node of the allocator's free-list walk, in nanoseconds —a
// pointer chase and a compare at 160 MHz— and what the PSRAM moves in a microsecond, which is **twenty
// bytes**: quad SPI at forty megahertz is 20 MB/s, and a block of the extra memory that has to change
// home is a copy through that bus (docs/75, docs/85).
const uint32_t kHeapStepNs = 100;
const uint32_t kPsramBytesPerUs = 20;
// **What a deadline that expired costs before it is delivered**: a turn of the loop of the board, which
// is where its extra millisecond comes from (docs/75).
const uint32_t kDispatchUs = 1000;

// **What the board does before the kernel's first line** (docs/75, docs/85): the second-stage bootloader
// reads the image out of the SPI nor —**ten megabytes a second** in the dual mode it starts with, two
// bits a clock at forty megahertz— and then ESP-IDF's PSRAM test writes and reads every byte the chip
// maps (`CONFIG_SPIRAM_MEMTEST`, on by default in this IDF), which on the Paper's four megabytes is
// eight megabytes at the **twenty** of quad SPI. Turning a board on is not a syscall and the system does
// not pay it: the emulator does, so the prompt arrives when it arrives on the glass.
uint32_t boot_us(const emu::Model& hw) {
  const uint32_t image_us = hw.boot_image_bytes / 10;          // 10 B/us = 10 MB/s, dual at 40 MHz
  const uint32_t psram_us = (hw.psram_kb * 1024u * 2u) / 20u;  // 20 B/us = 20 MB/s, quad at 40 MHz
  return image_us + psram_us;
}

// **The cell of the model**, which is what an M5Paper carries: 1150 mAh at 3,7 V, full at 4,2 V
// (docs/50). It is here for the line the emulator says when it lends the battery of the machine it runs
// on: the voltage itself is the curve's business (`hal/battery.h`).
const uint32_t kBoardMilliampHours = 1150;
const int kCellFullMv = 4200;

// **What one instruction of the interpreter costs at 160 MHz**, in nanoseconds: the model's number for
// an ESP32 running Lua, and the one the emulator pays so that a loop costs here what it costs on the glass.
// It is not a measurement of this unit, and docs/95 says how to take one (docs/85).
const int32_t kLuaInstructionNs = 60;

// **What a turn of the kernel costs on the board and not here**, in microseconds: a turn of the loop is
// **1 ms** on the ATOM and **0.15 ms** on the PC (measured, docs/75: a hundred awaits of one
// millisecond take 199 ms on the glass and 115 ms here). The difference is paid after every turn while
// the emulator is paying the times of the hardware, so a loop of awaits takes here what it takes on the
// board. It is the **turn** that is paced, and not one instruction of it (docs/85).
const int32_t kTurnOverheadUs = 850;
// **How often a run with a window comes back to it while it has nothing else to do**: the glass of the board
// is scanned every ten milliseconds, and a window that is looked at twice as often as that never holds two
// taps in one batch (docs/85, docs/90).
const uint32_t kGuiSliceMs = 20;

// What a turn pays: the constant above while the emulator is paying the times of the hardware, and nothing
// at all when it is not, because `--no-delay` is a run about what the code does and not about what the
// board costs (docs/85).
int32_t g_turn_us = 0;

// The UART of the board, in baud: the console of the emulator is a terminal and not a wire, but the
// speed is part of what a person sees —a screen of text is a third of a second on the glass— so the
// emulator waits the wire out instead of being faster than the board (docs/85).
const int32_t kUartBaud = 115200;

// A card in the slot, as a directory: a board that has a slot gets one when the emulator starts, the
// same as a card being in it, and it is always the same directory (docs/85).
void make_slot(const char* path) {
  if (path == nullptr) return;
  char partial[256];
  const size_t len = strlen(path);
  if (len == 0 || len >= sizeof(partial)) return;
  memcpy(partial, path, len + 1);
  for (size_t i = 1; i <= len; ++i) {
    if (partial[i] == '/' || partial[i] == '\0') {
      const char saved = partial[i];
      partial[i] = '\0';
      ::mkdir(partial, 0777);  // it fails when it is already there, which is fine
      partial[i] = saved;
    }
  }
}

// How long a batch run waits for one app before giving up on it: an app reading
// a key that never arrives must not hang the script for ever.
const uint32_t kBatchWaitMs = 60000;

void usage() {
  printf("lunokOS emulator\n");
  printf("  --flash <dir>     the directory that IS the flash volume (default: %s, the disk\n", LUNOK_DISK_DIR);
  printf("                    this build was made for, assembled by tools/make_disk.py)\n");
  printf("  --state <dir>     where the memories that are not a volume live: the key-value store\n");
  printf("                    and, when they exist, the RTC RAM and the FRAM (default: emulator)\n");
  printf("  --fs <dir>        working disk for an emulator that copies its own (default: emulator)\n");
  printf("  --sd <dir>        the card in the slot, as a directory (default: emulator/sd, and a\n");
  printf("                    card is a directory that exists: without it the slot is empty)\n");
  printf("  --battery-mv <mv> the cell the board reads, in millivolts; it wins over this machine's\n");
  printf("  --battery-host    say it aloud: a board with a gauge lends this machine's battery by itself\n");
  printf("  --board <name>    which board this emulator is (required): %s", board::model(0));
  for (uint32_t i = 1; i < board::models_count(); ++i) printf(", %s", board::model(i));
  printf("\n");
  printf("  --mem-cap <KB>    Lua heap ceiling, never above the board's memory\n");
  printf("  --baud <n>        speed of the serial port in baud, as the board's UART has it (%d;\n",
         kUartBaud);
  printf("                    0 = no wait, which is a console that is a file and not a wire)\n");
  printf("  --wake-early <ms> come back this much before the hour a sleep was given (0, never)\n");
  printf("  --now <epoch>     fixed start time\n");
  printf("  --time-scale <N>  speed up time N times\n");
  printf("  --seed <n>        random seed\n");
  printf("  --wifi <ms>       how long the fake network takes to come up (300)\n");
  printf("  --no-wifi         a board with no radio at all\n");
  printf("  --wifi-fail       a network that refuses to connect\n");
  printf("  --wifi-scan <n>   how many networks the fake radio sees (1; 0 is the middle of a field)\n");
  printf("  --prov-ms <ms>    how long the fake provisioning AP stays up (2000)\n");
  printf("  --prov-ssid <s>   network the fake provisioning hands over (casa)\n");
  printf("  --prov-pass <s>   password it hands over (secret)\n");
  printf("  --prov-never      a board nobody ever provisions\n");
  printf("  --prov-fail       provisioning that fails\n");
  printf("  --http-fake       the canned page: no address leaves this machine\n");
  printf("  --http-ms <ms>    how long that canned page takes (50, and it is a fake)\n");
  printf("  --http-status <n> the status the canned page answers (200, and it is a fake)\n");
  printf("  --http-body <s>   the body the canned page answers (and it is a fake)\n");
  printf("  --http-fail       a server that never answers (and it is a fake)\n");
  printf("  --imu-ax <g>      what the fake sensor reads on x (0)\n");
  printf("  --imu-ay <g>      what it reads on y (0)\n");
  printf("  --imu-az <g>      what it reads on z (1, flat on a table)\n");
  printf("  --imu-temp <C>    the temperature it reports (25)\n");
  printf("  --imu-fail        a sensor that does not answer\n");
  printf("  --env-celsius <C> what the fake air reads in temperature (25)\n");
  printf("  --env-humidity <pct>  and in humidity (50)\n");
  printf("  --env-fail        an ambient sensor that does not answer\n");
  printf("  --run <app>       start an app\n");
  printf("  --script <file>   batch mode: run and exit\n");
  printf("  --display         the glass of the board, which is there by itself: 960x540, four bits\n");
  printf("  --no-display      no glass at all: the console only, and no buffer of the page\n");
#ifdef LUNOK_GUI_BY_DEFAULT
  printf("  --gui             that glass in a window (SDL2 or X11), which this build opens by itself\n");
#else
  printf("  --gui             that glass in a window (SDL2 or X11), painted as the panel is painted\n");
#endif
  printf("  --no-gui          no window at all: the console and, with --dump, the page in a file\n");
  printf("  --gui-scale <1..4>  page pixels per pixel of the window: 2 is the page at half\n");
  printf("                    (without it, the smallest scale that fits the screen)\n");
  printf("  --no-ink          a glass that switches pixels: instant, clean, and not the panel\n");
  printf("  --touch           the finger of the board, which is there by itself: a GT911 on the bus\n");
  printf("  --no-touch        no finger at all: the panel is not asked\n");
  printf("  --touch-scan <ms> how often that panel reports, and so how often the kernel asks it (0 is the chip's own, 10 ms)\n");
  printf("  --lua-cpu <ns>    what one instruction of an app costs at 160 MHz (%d; 0 = free)\n",
         kLuaInstructionNs);
  printf("  --no-delay        no waiting at all: the model keeps the times, the clock does not pay them\n");
  printf("  --persist         a board that was never unplugged: the RTC RAM and the hour of the\n");
  printf("                    run before are the ones this run starts with\n");
  printf("  --verbose         mirror the log to the console too\n");
  printf("  -h, --help        this text\n");
}

bool next_value(int& i, int argc, char** argv, const char*& value) {
  if (i + 1 >= argc) return false;
  value = argv[++i];
  return true;
}

bool parse_number(const char* text, long long& out) {
  char* end = nullptr;
  const long long value = strtoll(text, &end, 10);
  if (end == nullptr || *end != '\0') return false;
  out = value;
  return true;
}

bool parse_real(const char* text, float& out) {
  char* end = nullptr;
  const float value = strtof(text, &end);
  if (end == nullptr || *end != '\0') return false;
  out = value;
  return true;
}

bool parse(int argc, char** argv, emu::Options& opts, bool& want_help) {
  // **The switch of every wait in this emulator**: one flag and not one per device, because a run either
  // pays the hardware's time or it is a test, and a test that pays half of it measures nothing
  // (docs/85).
  bool no_delay = false;
  // The hardware of the model comes on by itself and these take it away: an emulator that is a Paper and
  // has no glass and no fingers is a board nobody can buy (docs/85).
  bool no_display = false;
  bool no_touch = false;
  bool ask_display = false;
  bool ask_touch = false;
  // **The window is what this build comes up with** (0.155.0): `LUNOK_GUI_BY_DEFAULT` in `platformio.ini` makes a
  // run open the glass in a window without being asked, and `--no-gui` is how the run that wants the console alone
  // takes it away. **A demand and a default are not the same thing**: what `--gui` asked for refuses what cannot be
  // (`--no-delay`, `--no-ink`, a board with no glass and no key), and the default, which nobody asked for, steps
  // aside with a line so that a script that says `--no-delay` still runs (docs/85).
  bool gui_asked = false;
  bool no_gui = false;
  // **No picture of the page unless the run asks for one**, which is what the rest of these start as well: a
  // binary that writes files nobody asked for is an emulator that surprises whoever runs it (docs/85).
  opts.dump = nullptr;
  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    const char* value = nullptr;
    long long number = 0;

    if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
      want_help = true;
      return true;
    }
    if (strcmp(arg, "--verbose") == 0) {
      opts.verbose = true;
      continue;
    }
    if (strcmp(arg, "--no-delay") == 0) {
      no_delay = true;
      continue;
    }
    if (strcmp(arg, "--persist") == 0) {
      // A board that was not unplugged: the state directory is read as the board left it and the RTC
      // memory is not wiped, whatever the model has behind it (docs/85).
      opts.persist = true;
      continue;
    }
    if (strcmp(arg, "--no-ink") == 0) {
      // The emulator of the tests: a paint is instant and exact, and the page is whatever was painted with
      // nothing left of the one before (docs/85).
      opts.ink = -1;  // asked for: a glass that switches pixels
      continue;
    }
    if (strcmp(arg, "--ink-refuse") == 0) {
      // **The panel that says no** (0.118.2): a paint that arrives while the ink of the one before is still moving is
      // refused, which is what the driver of the Paper does, and the caller that waits for `idle()` is the one that
      // keeps its page (docs/50, docs/85).
      opts.ink_refuse = true;
      continue;
    }
    if (strcmp(arg, "--lua-cpu") == 0) {
      // What one instruction of an app costs, in nanoseconds at 160 MHz: zero is an emulator that does not
      // price the app's own code at all (docs/85).
      if (!next_value(i, argc, argv, value) || !parse_number(value, number) || number < 0) return false;
      opts.lua_ns_160 = static_cast<int32_t>(number);
      continue;
    }
    if (strcmp(arg, "--battery-host") == 0) {
      // **The battery of the machine this emulator runs on**, lent to the board: what is lent is the charge
      // and not the voltage, because 63 % of a laptop is 63 % of the Paper's cell (docs/85).
      opts.battery_from_host = true;
      continue;
    }
    if (strcmp(arg, "--touch") == 0) {
      // **A finger goes through the glass**: the panel of the model is there unless the run says it is
      // not, because a Paper always has one (docs/85).
      ask_touch = true;
      continue;
    }
    if (strcmp(arg, "--no-touch") == 0) {
      // A glass nobody touches: no bus read per turn, which is what a run about something else wants
      // (docs/85).
      no_touch = true;
      continue;
    }
    if (strcmp(arg, "--touch-scan") == 0) {
      // How often the chip reports. A number larger than the turn of the kernel is a glass that answers
      // the kernel with nothing every other turn, which is the case worth trying (docs/85).
      if (!next_value(i, argc, argv, value) || !parse_number(value, number) || number < 0) return false;
      opts.touch_scan_ms = static_cast<int32_t>(number);
      continue;
    }
    if (strcmp(arg, "--dump") == 0) {
      // **The page written to a file after every paint**: a PGM of eight bits per pixel, one level of the
      // panel per value, which is what a session with no window can look at (docs/85).
      if (!next_value(i, argc, argv, value)) return false;
      opts.dump = value;
      opts.display = true;
      continue;
    }
    if (strcmp(arg, "--gui") == 0) {
      // **A window with the glass in it**, and the glass with it: a window of nothing is not a window
      // (docs/85). It is a demand, and the default of the build is not (see `gui_asked`).
      opts.gui = true;
      opts.display = true;
      gui_asked = true;
      continue;
    }
    if (strcmp(arg, "--no-gui") == 0) {
      // **No window at all**: the console, and the page in a file when `--dump` asks for one. It is the run of a
      // measure, and what a build that comes up with a window needs to be quiet again (docs/85).
      no_gui = true;
      continue;
    }
    if (strcmp(arg, "--gui-scale") == 0) {
      // **How big the page is in the window**: 1 is the page as it is and 2 is the page at half, and a
      // run that asks for it gets it even when the screen would hold the page whole. There is no 0 on
      // the command line: the absence of the flag is the scale that fits (docs/85).
      if (!next_value(i, argc, argv, value) || !parse_number(value, number) || number < 1 || number > 4) {
        fprintf(stderr, "--gui-scale: the scale is 1, 2, 3 or 4\n");
        return false;
      }
      opts.gui_scale = static_cast<uint8_t>(number);
      continue;
    }
    if (strcmp(arg, "--display") == 0) {
      // A board with a glass: 960x540 and four bits per pixel, kept in a buffer on this side. It is
      // the glass of the model and it is there by itself; asking for it is only how a script says it
      // needs one (docs/85).
      ask_display = true;
      continue;
    }
    if (strcmp(arg, "--no-display") == 0) {
      // A board with no screen, with everything else of the model: it is what a run that only wants the
      // console wants, and it does not pay the four hundred kilobytes of buffers (docs/85).
      no_display = true;
      continue;
    }
    if (strcmp(arg, "--fs") == 0) {
      if (!next_value(i, argc, argv, value)) return false;
      opts.fs_dir = value;
      continue;
    }
    if (strcmp(arg, "--flash") == 0) {
      if (!next_value(i, argc, argv, value)) return false;
      opts.flash_dir = value;
      continue;
    }
    if (strcmp(arg, "--state") == 0) {
      if (!next_value(i, argc, argv, value)) return false;
      opts.state_dir = value;
      continue;
    }
    if (strcmp(arg, "--sd") == 0) {
      if (!next_value(i, argc, argv, value)) return false;
      opts.sd_dir = value;
      continue;
    }
    if (strcmp(arg, "--battery-mv") == 0) {
      if (!next_value(i, argc, argv, value) || !parse_number(value, number) || number < 0) {
        return false;
      }
      opts.battery_mv = static_cast<int32_t>(number);
      continue;
    }
    if (strcmp(arg, "--board") == 0) {
      if (!next_value(i, argc, argv, value)) return false;
      // Refused here and not at boot: an emulator told to be a board that does not exist
      // would answer a name no manifest could ever match, and the person would see an
      // app refused by a board they never asked for (docs/85).
      bool known = false;
      for (uint32_t m = 0; m < board::models_count(); ++m) {
        if (strcmp(board::model(m), value) == 0) known = true;
      }
      if (!known) {
        fprintf(stderr, "unknown board: %s\n", value);
        return false;
      }
      opts.board = value;
      continue;
    }
    if (strcmp(arg, "--script") == 0) {
      if (!next_value(i, argc, argv, value)) return false;
      opts.script = value;
      continue;
    }
    if (strcmp(arg, "--run") == 0) {
      if (!next_value(i, argc, argv, value)) return false;
      opts.run_app = value;
      continue;
    }
    if (strcmp(arg, "--mem-cap") == 0) {
      if (!next_value(i, argc, argv, value) || !parse_number(value, number)) return false;
      opts.mem_cap_kb = static_cast<int32_t>(number);
      continue;
    }
    if (strcmp(arg, "--wake-early") == 0) {
      if (!next_value(i, argc, argv, value) || !parse_number(value, number) || number < 0) {
        return false;
      }
      opts.wake_early_ms = static_cast<int32_t>(number);
      continue;
    }
    // The speed of the serial port, which is a wire at 115200 baud on the board and a terminal here.
    if (strcmp(arg, "--baud") == 0) {
      if (!next_value(i, argc, argv, value) || !parse_number(value, number) || number < 0) {
        return false;
      }
      opts.baud = static_cast<int32_t>(number);
      continue;
    }
    if (strcmp(arg, "--now") == 0) {
      if (!next_value(i, argc, argv, value) || !parse_number(value, number)) return false;
      opts.now_epoch = static_cast<int64_t>(number);
      continue;
    }
    if (strcmp(arg, "--time-scale") == 0) {
      if (!next_value(i, argc, argv, value) || !parse_number(value, number) || number < 1) {
        return false;
      }
      opts.time_scale = static_cast<uint32_t>(number);
      continue;
    }
    if (strcmp(arg, "--no-wifi") == 0) {
      opts.wifi_absent = true;
      continue;
    }
    if (strcmp(arg, "--wifi-fail") == 0) {
      opts.wifi_fail = true;
      continue;
    }
    if (strcmp(arg, "--wifi") == 0) {
      if (!next_value(i, argc, argv, value) || !parse_number(value, number) || number < 0) {
        return false;
      }
      opts.wifi_ms = static_cast<int32_t>(number);
      continue;
    }
    // How many networks the fake radio sees: zero is a board in a field, and it is
    // the case where a scan has to answer 0 and not `true` (docs/85).
    if (strcmp(arg, "--wifi-scan") == 0) {
      if (!next_value(i, argc, argv, value) || !parse_number(value, number) || number < 0) {
        return false;
      }
      opts.scan_count = static_cast<int32_t>(number);
      continue;
    }
    if (strcmp(arg, "--prov-never") == 0) {
      opts.prov_never = true;
      continue;
    }
    if (strcmp(arg, "--prov-fail") == 0) {
      opts.prov_fail = true;
      continue;
    }
    // **The page of this band is the one the address answers**, and these options put the canned
    // one back: a server that answers http_status with http_body after http_ms, or one that never
    // answers. Asking for any of them is asking for the fake, so the URL never leaves this machine
    // (docs/85).
    if (strcmp(arg, "--http-fake") == 0) {
      opts.http_fake = true;
      continue;
    }
    if (strcmp(arg, "--http-ms") == 0) {
      if (!next_value(i, argc, argv, value) || !parse_number(value, number) || number < 0) {
        return false;
      }
      opts.http_ms = static_cast<int32_t>(number);
      opts.http_fake = true;
      continue;
    }
    if (strcmp(arg, "--http-status") == 0) {
      if (!next_value(i, argc, argv, value) || !parse_number(value, number)) return false;
      opts.http_status = static_cast<int32_t>(number);
      opts.http_fake = true;
      continue;
    }
    if (strcmp(arg, "--http-body") == 0) {
      if (!next_value(i, argc, argv, value)) return false;
      opts.http_body = value;
      opts.http_fake = true;
      continue;
    }
    if (strcmp(arg, "--http-fail") == 0) {
      opts.http_fail = true;
      opts.http_fake = true;
      continue;
    }
    // The sensor of this band is a fake too, and it answers what these say: the PC
    // has no MPU6886 to read (docs/85). A board shaken on the desk is a number the
    // person writes here.
    if (strcmp(arg, "--imu-fail") == 0) {
      opts.imu_fail = true;
      continue;
    }
    if (strncmp(arg, "--imu-a", 7) == 0 || strcmp(arg, "--imu-temp") == 0) {
      if (!next_value(i, argc, argv, value)) return false;
      float g = 0;
      if (!parse_real(value, g)) return false;
      // --imu-ax, --imu-ay, --imu-az say one axis each; anything else that starts
      // with --imu-a would be a typo, so it is refused instead of taken as z.
      if (strcmp(arg, "--imu-ax") == 0) {
        opts.imu_ax = g;
      } else if (strcmp(arg, "--imu-ay") == 0) {
        opts.imu_ay = g;
      } else if (strcmp(arg, "--imu-az") == 0) {
        opts.imu_az = g;
      } else if (strcmp(arg, "--imu-temp") == 0) {
        opts.imu_celsius = g;
      } else {
        return false;
      }
      continue;
    }
    // The air of this band, for a model with a sensor: the two numbers a person writes, because a
    // PC has no SHT30 and a room is not a test (docs/85).
    if (strcmp(arg, "--env-fail") == 0) {
      opts.env_fail = true;
      continue;
    }
    if (strcmp(arg, "--env-celsius") == 0 || strcmp(arg, "--env-humidity") == 0) {
      if (!next_value(i, argc, argv, value)) return false;
      float g = 0;
      if (!parse_real(value, g)) return false;
      if (strcmp(arg, "--env-celsius") == 0) {
        opts.env_celsius = g;
      } else {
        opts.env_humidity = g;
      }
      continue;
    }
    if (strcmp(arg, "--prov-ms") == 0) {
      if (!next_value(i, argc, argv, value) || !parse_number(value, number) || number < 0) {
        return false;
      }
      opts.prov_ms = static_cast<int32_t>(number);
      continue;
    }
    if (strcmp(arg, "--prov-ssid") == 0) {
      if (!next_value(i, argc, argv, value)) return false;
      opts.prov_ssid = value;
      continue;
    }
    if (strcmp(arg, "--prov-pass") == 0) {
      if (!next_value(i, argc, argv, value)) return false;
      opts.prov_pass = value;
      continue;
    }
    if (strcmp(arg, "--seed") == 0) {
      if (!next_value(i, argc, argv, value) || !parse_number(value, number)) return false;
      opts.seed = static_cast<uint32_t>(number);
      continue;
    }
    fprintf(stderr, "unknown option: %s\n", arg);
    return false;
  }

  // Which board this emulator is has no default: a board that is not named is a board nobody chose,
  // and the whole point of the emulator is that what it answers is the model asked for (docs/85).
  if (opts.board == nullptr) {
    fprintf(stderr, "which board is this emulator? --board <name>, one of: %s", board::model(0));
    for (uint32_t i = 1; i < board::models_count(); ++i) fprintf(stderr, ", %s", board::model(i));
    fprintf(stderr, "\n");
    return false;
  }

  // The memory of the model is a wall: an emulator asked for more than the board has is an emulator that
  // would pass an app the glass would refuse (docs/75, docs/85). The PSRAM counts: the Paper has 8 MB
  // of it and that is where an app's big blocks go.
  const emu::Model memory = emu::model_of(opts.board);
  const uint32_t most_kb = (memory.heap_kb > memory.psram_kb) ? memory.heap_kb : memory.psram_kb;
  if (opts.mem_cap_kb > static_cast<int32_t>(most_kb)) {
    fprintf(stderr, "--mem-cap %d KB: this board has %u KB of memory and no more\n", opts.mem_cap_kb,
            static_cast<unsigned>(most_kb));
    return false;
  }

  // **The glass and the fingers are the model's, and they come on by themselves**: an emulator of a Paper
  // with no screen and no touch panel is a board nobody can buy, and the flags to take them away exist
  // for the run that only wants the console. A test builds its own options and pays neither (docs/85).
  if (ask_display && !memory.glass) {
    fprintf(stderr, "--display: %s has no glass, and an emulator of it has no screen\n", memory.name);
    return false;
  }
  if (ask_touch && !memory.touch) {
    fprintf(stderr, "--touch: %s has no touch panel, and an emulator of it has no finger\n", memory.name);
    return false;
  }
  // **A window of what the board has**: a model with a glass shows it, and every model of this family has
  // at least one key, so `--gui` on a board with no screen is the window of its button and not an error —
  // the emulator of an ATOM still has the button a person presses (docs/85).
  opts.display = memory.glass && !no_display;
  opts.touch = memory.touch && !no_touch;
#ifdef LUNOK_GUI_BY_DEFAULT
  if (!no_gui) opts.gui = true;
#endif
  if (opts.gui && !opts.display && memory.keys == 0) {
    if (gui_asked) {
      fprintf(stderr, "--gui: %s has no glass and no key, so there is no window to open\n", memory.name);
      return false;
    }
    fprintf(stderr, "gui: %s has no glass and no key, so this run has no window\n", memory.name);
    opts.gui = false;
  }

  // The defaults of the **binary**, and only of the binary: a test builds its own options and
  // points everything at one directory it will wipe, which is why these are set here and not in
  // `Options()` (docs/85).
  //
  // The flash is **the disk this build was made for**, `build/disk/<env>`: `data/` is what the repository
  // holds, `apps/` is where the apps live and `tools/make_disk.py` is what puts the two together for the
  // boards and the emulator that can hold them (docs/80). It is a tree of links to those two, so what a person
  // edits is what the emulator runs and nothing has to be copied. The key-value store and the memories that are
  // not a volume go to `emulator`, next to the card, which is always the same directory: a card is a
  // directory that exists, so making it puts a card in the slot and removing it empties it (docs/85).
  if (opts.flash_dir == nullptr) opts.flash_dir = LUNOK_DISK_DIR;
  if (opts.state_dir == nullptr) opts.state_dir = "emulator";
  if (opts.sd_dir == nullptr) opts.sd_dir = "emulator/sd";
  // **What the disk costs is the disk's business**: every volume goes through the table of
  // `emu/disk.h` —a block of the flash, a sector of the card, a byte of the FRAM— so there is no one
  // number for a read any more (docs/85).
  // And the serial port goes at the speed of the board's UART, so what a person sees arrive arrives
  // at the rate it would over the cable (docs/85).
  if (opts.baud < 0) opts.baud = kUartBaud;
  // **The battery of the machine, lent to the board, and by itself**: a board with a gauge reads a cell,
  // and the only cell the emulator has to offer is this machine's. What is lent is the percentage, because a
  // laptop speaks in percentages and the Paper in millivolts, and the curve of the board's cell turns it
  // into a voltage. It happens without asking because a Paper with no battery is a board nobody has, and
  // a run that wants another one types it with `--battery-mv` (docs/85).
  if (opts.battery_from_host && opts.battery_mv > 0) {
    fprintf(stderr, "--battery-host and --battery-mv are two different batteries; pick one\n");
    return false;
  }
  if (opts.battery_mv <= 0 && memory.gauge) {
    const emu::HostBattery host = emu::host_battery();
    opts.battery_mv = emu::lent_cell_mv(host);
    if (!host.present) {
      // A desk has no cell to lend and the board still has one: the cell starts at its nominal charge and
      // the emulator says the number it made up, because a number nobody typed has to be announced
      // (docs/85).
      fprintf(stderr,
              "battery: this machine has no battery to lend; the cell of the board starts at %d mV (%d %%)\n",
              opts.battery_mv, emu::kNominalCellPct);
    } else {
      fprintf(stderr, "battery: %d %% of this machine is %d mV of the cell of the board (%u mAh, %d full)%s\n",
              host.percent, opts.battery_mv, kBoardMilliampHours, kCellFullMv,
              host.on_cable ? ", and this machine is on a cable, which the Paper cannot tell" : "");
    }
  } else if (opts.battery_from_host) {
    // A board of its own with no gauge has nothing to lend it to: the ATOMs have no cell to read, and
    // saying so is cheaper than a run that wonders why the battery never shows up (docs/50, docs/85).
    fprintf(stderr, "battery: %s has no gauge of its own, so there is nothing to lend it\n", memory.name);
  }
  // **The app's own code is CPU time**: the interpreter is priced at what it would cost on the board, and
  // `--lua-cpu` is where the model's number is changed or taken away (0) (docs/85).
  if (opts.lua_ns_160 < 0) opts.lua_ns_160 = kLuaInstructionNs;
  // **The times of the hardware: paid by default, and off with one flag.** A window is for watching
  // what a board costs, so it needs both the waits and the ink: a glass that switches pixels and
  // charges nothing is a picture of a board that does not exist (docs/85).
  opts.delays = !no_delay;
  // **What cannot be a window goes, in the order a run asked for it**: a run that says `--no-delay` and not `--gui`
  // is a measure of the model and keeps its console; a run that says `--gui` and `--no-delay` is a contradiction and
  // is told so (docs/85).
  if (opts.gui && no_delay) {
    if (gui_asked) {
      fprintf(stderr, "--gui needs the times of the hardware: a window is for watching what a board costs\n");
      return false;
    }
    fprintf(stderr, "gui: no window, --no-delay is an emulator without the times of the hardware\n");
    opts.gui = false;
  }
  if (opts.gui && opts.ink < 0 && opts.display) {
    if (gui_asked) {
      fprintf(stderr, "--gui needs the ink of the glass: --no-ink paints a page no panel has\n");
      return false;
    }
    fprintf(stderr, "gui: no window, --no-ink paints a page no panel has\n");
    opts.gui = false;
  }
  // **The glass of a run moves ink unless the run says it does not**: a paint takes what the quality
  // takes and leaves what it leaves. Instant and perfect is the emulator of the tests, which is where the
  // option starts (docs/85).
  opts.ink = (opts.ink < 0) ? 0 : 1;
  return true;
}

// **The window, once per turn.** The glass is copied into it only when it has something new to show —a
// paint, or one of the blinks of an `a2`, which is a frame without a paint— and the events of the window
// are taken without waiting, because an emulator that blocks on the keyboard of a window is an emulator that
// stopped (docs/85).
void pump_gui(uint32_t& shown_frames) {
  emu::gui_pump();
  // **An emulator without a glass has no page to show**: the panel is not even made, and the window of a
  // model with keys is the window of its keys. Making the panel here also gave an ATOM a page it does
  // not have and paid its buffers on every headless run (docs/85).
  if (!emu::options().display) return;
  emu::MemorySurface* glass = emu::memory_surface();
  if (glass == nullptr) return;
  // **A flash is walked here too**: the app may not ask `idle()` for a while, and a blink nobody draws is
  // a blink nobody sees (docs/85).
  glass->step_ink();
  const uint32_t frames = glass->flushes() + glass->ink_steps();
  if (frames == shown_frames) return;
  shown_frames = frames;
  // **What the glass shows and not what was painted**: the ink of a `du` keeps part of the page before
  // it, and that is exactly what a person is looking at (docs/85).
  emu::gui_show(glass->shown_bits(), glass->width(), glass->height());
}

// **The glass while it waits.** A paint holds its caller for the wires and for the ink, and the panel
// keeps blinking through both; this is `pump_gui` reached from inside those waits, which is the only way
// the blinks of a `gc16` are frames of a window instead of a sleep nobody draws (docs/85).
uint32_t* g_shown_frames = nullptr;

void wait_pump(void*) {
  if (g_shown_frames != nullptr) pump_gui(*g_shown_frames);
}

// **One turn of the system, and what a turn costs on the board**: FreeRTOS switches, the drivers get
// their look-in and the loop comes back a millisecond later, where this emulator comes back in a tenth of
// one. The difference is paid here —after the turn, because a turn that has painted has already spent
// its time (docs/75, docs/85). What is **not** modelled is the instruction: that is the interpreter's
// business and `--lua-cpu` prices it one block of instructions at a time (docs/85).
void turn(uint32_t& shown_frames) {
  kernel::poll();
  pump_gui(shown_frames);
  if (g_turn_us > 0) pal::delay_us(static_cast<uint32_t>(g_turn_us));
}

int run_script(const char* path, uint32_t& shown_frames) {
  FILE* file = fopen(path, "r");
  if (file == nullptr) {
    fprintf(stderr, "cannot open the script: %s\n", path);
    return 2;
  }
  char line[kLineBytes];
  int rc = 0;
  while (fgets(line, sizeof(line), file) != nullptr) {
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
      line[--len] = '\0';
    }
    if (len == 0 || line[0] == '#') continue;
    if (!kernel::execute(line)) rc = 1;
    // A board that is off does not read the next line: `poweroff` ends the kernel, and a script that
    // went on would be a script running on a board that is not there (docs/85).
    if (!kernel::running()) break;
    // An app may be waiting for a promise, and a command may be waiting for the
    // radio: both have to finish before the next line, or the script would read
    // as if nothing had happened (`wifi add` followed by `http` found the radio
    // busy, measured on the PC, 31/10). One waiting for a key that never comes
    // would hang the batch, so there is a budget.
    const uint32_t started = pal::system().millis();
    while ((kernel::app_running() || kernel::shell::native_running()) &&
           (pal::system().millis() - started) < kBatchWaitMs) {
      turn(shown_frames);
    }
    if (kernel::app_running() || kernel::shell::native_running()) {
      fprintf(stderr, "the line did not finish\n");
      rc = 1;
    }
    // **An app that failed is a line that failed**: it is the same answer the shell gives a person,
    // and the batch has no other code to say it with (0, 1 and 2, docs/85).
    if (luabind::failed() || luabind::exit_code() != 0) rc = 1;
    // Two turns more, always: what the end of a turn does —the light, for one— has
    // to happen before the next line, or a script would see a board that never
    // paints anything (measured on the PC, 31/10).
    for (int more = 0; more < 2; ++more) {
      turn(shown_frames);
    }
  }
  fclose(file);
  return rc;
}

}  // namespace

int main(int argc, char** argv) {
  emu::Options opts{};
  bool want_help = false;
  if (!parse(argc, argv, opts, want_help)) {
    fprintf(stderr, "bad options; try --help\n");
    return 2;
  }
  if (want_help) {
    usage();
    return 0;
  }

  emu::set_options(opts);

  // The hardware of the model is there when the emulator starts, exactly as on the board: a card in the
  // slot of a board that has a slot, and the memories the model is made of, created at the size they
  // really are (docs/50, docs/85).
  const emu::Model hw = emu::model();
  if (hw.slot) make_slot(opts.sd_dir);
  emu::power_cycle();
  atexit(emu::flush_memories);

  if (!pal::init()) {
    fprintf(stderr, "cannot initialise the console\n");
    return 2;
  }
  // The console is a terminal, but the board it stands for talks over a UART at 115200 baud: the
  // bytes are handed over at that speed, not all at once (docs/85).
  pal::set_console_baud(static_cast<uint32_t>(opts.delays ? opts.baud : 0));
  // **This console is a virtual terminal and a board's is a serial port** (0.158.0): a program on a PC is a session
  // somebody started, and `exit`, `logout` and Ctrl+D end it; a UART cannot be hung up and there the session says so
  // and waits for an Enter (docs/30, docs/85).
  pal::set_virtual_terminal(true);
  // **A block of the extra memory is not free either**: the board's allocator walks its free lists and
  // the PC's answers at once, and a block that changes home is copied through the PSRAM's bus, so the
  // emulator is told what both cost when it is paying the times of the hardware (docs/85).
  pal::set_heap_timing(opts.delays ? kHeapStepNs : 0, opts.delays ? kPsramBytesPerUs : 0);
  // **A deadline is delivered on the turn after it expires**, which is the extra millisecond the board
  // pays for a promise of one (docs/75, docs/85).
  pal::set_dispatch_us(opts.delays ? kDispatchUs : 0);
  // **Which emulator this is, said out loud**: a run that pays the times of the hardware and one that does
  // not look the same until somebody times a screen, and that is exactly the doubt this line answers
  // (docs/85). It goes to stderr because it is this band talking about itself.
  if (opts.delays) {
    fprintf(stderr,
            "emulator: the times of the hardware are paid (console at %u baud both ways, flash and card, "
            "PSRAM, ink %s, a turn of %d us, a boot of %u ms)\n",
            static_cast<unsigned>(opts.baud), (opts.ink > 0) ? "on" : "off",
            static_cast<int>(kTurnOverheadUs), static_cast<unsigned>(boot_us(hw) / 1000));
  } else {
    fprintf(stderr,
            "emulator: the times of the hardware are NOT paid (--no-delay): the console answers at once, "
            "no bus, instant ink\n");
  }
  // **The page of this emulator is the one the address answers, and that is said once**: a person who does
  // not expect a request to leave the machine has to know it, and `--http-fake` is the way back (docs/85).
  if (!opts.http_fake) {
    fprintf(stderr,
            "emulator: `http` fetches the real page through `curl` (--http-fake answers a canned one)\n");
  }
  // **The memory of the board, and not the PC's** (docs/75): every block the system and the apps take —
  // a page of canvas, a font, the interpreter's heap— is counted against what the model really has free
  // at the prompt and against the extra memory it can hand out, and one that does not fit is refused
  // here the same way it is on the glass. It goes before the boot because it starts the books, and the
  // boot is the first thing that takes a block (docs/85).
  pal::set_memory_bytes(hw.free_heap_bytes, hw.extra_free_bytes);
  g_turn_us = opts.delays ? kTurnOverheadUs : 0;
  // **A run with a window looks at it twenty times a second even when the app is asleep in a promise**: the
  // queue of a PC waits on the console for as long as the nearest deadline says, and everything that is not
  // the console waits with it —the window and the finger on its glass— so two clicks poured into the panel
  // together are one tap (docs/85, docs/90). It is the chip's own scan period, twice.
  pal::set_wait_slice_ms(opts.gui ? kGuiSliceMs : 0);

  // **The boot of the board and not of this process** (docs/75, docs/85): the banner is not the first
  // thing a board says, it is the first thing it says after the image has come out of the flash and the
  // PSRAM has been tested. It happens before the kernel so the prompt arrives when it arrives there.
  if (opts.delays) pal::delay_us(boot_us(hw));

  kernel::boot(hal::platform());

  // **The glass moves ink unless the run asked for a glass that switches pixels** (`--no-ink`): the model
  // was parsed, printed and never switched on, so every run painted a page no panel has ever shown —no
  // blink of a `gc16`, no trace of a `du`— while the line at the top said the ink was on (found on the
  // window, 3/11). It is turned on here, once the panel exists and before the first paint (docs/85).
  if (opts.display) {
    if (emu::MemorySurface* glass = emu::memory_surface()) {
      if (!glass->set_ink(opts.ink > 0) && opts.ink > 0) {
        fprintf(stderr, "the glass has no memory for the ink model: it paints a page no panel has\n");
      }
    }
  }

  // **The window, when the run asked for one.** If there is no display to open it on, the emulator says so
  // and carries on without it: a run is not lost because a screen is missing (docs/85).
  uint32_t shown_frames = 0xFFFFFFFFu;
  if (opts.gui) {
    // **Where the keys go is the descriptor's to say**, not this emulator's: the board's input names the face a
    // key is on and the spot it holds, and a dial on a side face is a column down that side while a key on
    // the top is a row under the page (docs/50, docs/85).
    emu::KeySide side = emu::KeySide::kBottom;
    hal::IInput* input = hal::platform().input();
    if (input != nullptr && input->count() > 0) {
      const hal::Face face = input->face(0);
      if (face == hal::Face::kLeft) side = emu::KeySide::kLeft;
      if (face == hal::Face::kRight) side = emu::KeySide::kRight;
    }
    if (!emu::gui_available()) {
      fprintf(stderr, "this emulator was built without a window backend: no window\n");
    } else if (opts.display && (hal::platform().display() == nullptr ||
                                !emu::gui_open(emu::memory_surface()->width(),
                                               emu::memory_surface()->height(),
                                               static_cast<uint8_t>(hw.keys), side, opts.gui_scale))) {
      fprintf(stderr, "no display to open the window on\n");
    } else if (!opts.display &&
               !emu::gui_open(0, 0, static_cast<uint8_t>(hw.keys), side, opts.gui_scale)) {
      // **A board with no glass has its button and nothing else**: the window is the keys, and an ATOM still
      // gets the button a person presses (docs/85).
      fprintf(stderr, "no display to open the window on\n");
    } else {
      if (opts.display) {
        const uint8_t scale = emu::gui_scale();
        if (scale <= 1) {
          printf("gui: the page %ux%u is in a window, one pixel of the page per pixel of the screen\n",
                 emu::memory_surface()->width(), emu::memory_surface()->height());
        } else {
          const char* part = (scale == 2) ? "half" : ((scale == 3) ? "a third" : "a quarter");
          printf("gui: the page %ux%u is in a window at %s, one pixel of the window is %u of the page\n",
                 emu::memory_surface()->width(), emu::memory_surface()->height(), part,
                 static_cast<unsigned>(scale));
        }
      }
      if (hw.keys > 0) {
        const char* where = (side == emu::KeySide::kLeft)    ? "a column on the left of the page"
                            : (side == emu::KeySide::kRight) ? "a column on the right of the page"
                                                             : "a row under the page";
        printf("gui: the %u keys of the board are %s, where the descriptor of the board puts them\n",
               static_cast<unsigned>(hw.keys), where);
      }
      fflush(stdout);
    }
  }

  // **The panel asks the window from inside its own waits** (see `wait_and_pump`): a paint is eight
  // hundred milliseconds over the bus and four hundred and fifty of ink for a full `gc16`, and a window
  // redrawn once per turn is a window that shows the page before and the page after (docs/85).
  if (emu::MemorySurface* glass = emu::memory_surface()) {
    g_shown_frames = &shown_frames;
    glass->set_wait_pump(&wait_pump, nullptr);
  }

  int rc = 0;
  if (opts.run_app != nullptr) {
    // --run is the very sentence the shell understands, nothing more, and it
    // leaves: the useful thing about it is running one app from a script.
    char command[kLineBytes];
    snprintf(command, sizeof(command), "run %s", opts.run_app);
    if (!kernel::execute(command)) rc = 1;
    while (kernel::app_running() && !emu::gui_close_requested()) {
      turn(shown_frames);
    }
    // The result of the app, and not only of the sentence that started it: an app that died with an
    // error is not a run that went well (docs/85).
    if (luabind::failed() || luabind::exit_code() != 0) rc = 1;
  }
  if (opts.script != nullptr) {
    if (rc == 0) rc = run_script(opts.script, shown_frames);
  } else if (opts.run_app == nullptr) {
    while (kernel::running() && !emu::gui_close_requested()) {
      turn(shown_frames);
    }
  }

  emu::gui_close();
  // **What the board keeps when this run ends** (docs/85): a chip with a coin cell behind it has the
  // hour for the next power-on, and a run with `--persist` has it too because it was never unplugged.
  // It is written here and not in an `atexit` handler: the platform is a static of this run and a
  // handler that reads it after the program is tearing down is a handler that reads nothing.
  hal::IClock* clock = hal::platform().clock();
  if (clock != nullptr && clock->epoch_ms() > 0) emu::save_clock_ms(clock->epoch_ms());
  pal::shutdown();
  return rc;
}

#endif  // !UNIT_TEST && !PIO_UNIT_TESTING
