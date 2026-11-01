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

#include "emu/options.h"
#include "emu/memories.h"
#include "emu/model.h"
#include "hal/hal.h"
#include "kernel/kernel.h"
#include "kernel/shell.h"
#include "pal/pal.h"

#include "board/identity.h"

namespace {

const size_t kLineBytes = 128;

// What a read of the flash costs on the board, in microseconds: the block out of SPI plus the few
// metadata hops LittleFS does for a small file. It is an approximation until somebody measures it
// with a stopwatch, and it is one number to change (docs/85).
const int32_t kFlashReadUs = 1000;

// The UART of the board, in baud: the console of the bench is a terminal and not a wire, but the
// speed is part of what a person sees —a screen of text is a third of a second on the glass— so the
// bench waits the wire out instead of being faster than the board (docs/85).
const int32_t kUartBaud = 115200;

// A card in the slot, as a directory: a board that has a slot gets one when the bench starts, the
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
const uint32_t kBatchWaitMs = 30000;

void usage() {
  printf("lunokOS emulator\n");
  printf("  --flash <dir>     the directory that IS the flash volume (default: data, the real one)\n");
  printf("  --state <dir>     where the memories that are not a volume live: the key-value store\n");
  printf("                    and, when they exist, the RTC RAM and the FRAM (default: emulator)\n");
  printf("  --fs <dir>        working disk for a bench that copies its own (default: emulator)\n");
  printf("  --sd <dir>        the card in the slot, as a directory (default: emulator/sd, and a\n");
  printf("                    card is a directory that exists: without it the slot is empty)\n");
  printf("  --battery-mv <mv> what the fake battery reads (default: no battery)\n");
  printf("  --board <name>    which board this bench is (required): %s", board::model(0));
  for (uint32_t i = 1; i < board::models_count(); ++i) printf(", %s", board::model(i));
  printf("\n");
  printf("  --mem-cap <KB>    Lua heap ceiling, never above the board's memory\n");
  printf("  --read-delay <us> what one read costs, in microseconds (1000 = a millisecond of flash,\n");
  printf("                    and twice that on the card; 0 = no wait)\n");
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
  printf("  --prov-ms <ms>    how long the fake provisioning AP stays up (2000)\n");
  printf("  --prov-ssid <s>   network the fake provisioning hands over (casa)\n");
  printf("  --prov-pass <s>   password it hands over (secret)\n");
  printf("  --prov-never      a board nobody ever provisions\n");
  printf("  --prov-fail       provisioning that fails\n");
  printf("  --http-ms <ms>    how long the fake page takes (50)\n");
  printf("  --http-status <n> the status the fake page answers (200)\n");
  printf("  --http-body <s>   the body the fake page answers\n");
  printf("  --http-fail       a server that never answers\n");
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
  printf("  --display         a board with a glass: 960x540 and four bits per pixel\n");
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
    if (strcmp(arg, "--display") == 0) {
      // A board with a glass: 960x540 and four bits per pixel, kept in a buffer on this side
      // (docs/85).
      opts.display = true;
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
      // Refused here and not at boot: a bench told to be a board that does not exist
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
    if (strcmp(arg, "--read-delay") == 0) {
      if (!next_value(i, argc, argv, value) || !parse_number(value, number) || number < 0) {
        return false;
      }
      opts.read_delay_us = static_cast<int32_t>(number);
      continue;
    }
    // How early the bench comes back from a sleep: the sleep that never reaches its hour.
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
    // The page of this band is a fake: a server that answers http_status with
    // http_body after http_ms, or one that never answers (docs/85).
    if (strcmp(arg, "--http-ms") == 0) {
      if (!next_value(i, argc, argv, value) || !parse_number(value, number) || number < 0) {
        return false;
      }
      opts.http_ms = static_cast<int32_t>(number);
      continue;
    }
    if (strcmp(arg, "--http-status") == 0) {
      if (!next_value(i, argc, argv, value) || !parse_number(value, number)) return false;
      opts.http_status = static_cast<int32_t>(number);
      continue;
    }
    if (strcmp(arg, "--http-body") == 0) {
      if (!next_value(i, argc, argv, value)) return false;
      opts.http_body = value;
      continue;
    }
    if (strcmp(arg, "--http-fail") == 0) {
      opts.http_fail = true;
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

  // Which board this bench is has no default: a board that is not named is a board nobody chose,
  // and the whole point of the bench is that what it answers is the model asked for (docs/85).
  if (opts.board == nullptr) {
    fprintf(stderr, "which board is this bench? --board <name>, one of: %s", board::model(0));
    for (uint32_t i = 1; i < board::models_count(); ++i) fprintf(stderr, ", %s", board::model(i));
    fprintf(stderr, "\n");
    return false;
  }

  // The memory of the model is a wall: a bench asked for more than the board has is a bench that
  // would pass an app the glass would refuse (docs/75, docs/85). The PSRAM counts: the Paper has 8 MB
  // of it and that is where an app's big blocks go.
  const emu::Model memory = emu::model_of(opts.board);
  const uint32_t most_kb = (memory.heap_kb > memory.psram_kb) ? memory.heap_kb : memory.psram_kb;
  if (opts.mem_cap_kb > static_cast<int32_t>(most_kb)) {
    fprintf(stderr, "--mem-cap %d KB: this board has %u KB of memory and no more\n", opts.mem_cap_kb,
            static_cast<unsigned>(most_kb));
    return false;
  }

  // The defaults of the **binary**, and only of the binary: a test builds its own options and
  // points everything at one directory it will wipe, which is why these are set here and not in
  // `Options()` (docs/85).
  //
  // The flash is the repository's own `data`, so what a person edits is what the bench runs and
  // nothing has to be copied. The key-value store and the memories that are not a volume go to
  // `emulator`, next to the card, which is always the same directory: a card is a directory that
  // exists, so making it puts a card in the slot and removing it empties it (docs/85).
  if (opts.flash_dir == nullptr) opts.flash_dir = "data";
  if (opts.state_dir == nullptr) opts.state_dir = "emulator";
  if (opts.sd_dir == nullptr) opts.sd_dir = "emulator/sd";
  // And a read costs what it costs on the board: a millisecond of flash, and twice that for a card.
  // Without this a bench with the PC's page cache would let an app that reads a file per turn look
  // fine here and crawl on the glass (docs/85).
  if (opts.read_delay_us < 0) opts.read_delay_us = kFlashReadUs;
  // And the serial port goes at the speed of the board's UART, so what a person sees arrive arrives
  // at the rate it would over the cable (docs/85).
  if (opts.baud < 0) opts.baud = kUartBaud;
  return true;
}

int run_script(const char* path) {
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
      kernel::poll();
    }
    if (kernel::app_running() || kernel::shell::native_running()) {
      fprintf(stderr, "the line did not finish\n");
    }
    // Two turns more, always: what the end of a turn does —the light, for one— has
    // to happen before the next line, or a script would see a board that never
    // paints anything (measured on the PC, 31/10).
    for (int turn = 0; turn < 2; ++turn) kernel::poll();
  }
  fclose(file);
  return rc;
}

}  // namespace

int main(int argc, char** argv) {
  emu::Options opts;
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

  // The hardware of the model is there when the bench starts, exactly as on the board: a card in the
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
  pal::set_console_baud(static_cast<uint32_t>(opts.baud));

  kernel::boot(hal::platform());

  int rc = 0;
  if (opts.run_app != nullptr) {
    // --run is the very sentence the shell understands, nothing more, and it
    // leaves: the useful thing about it is running one app from a script.
    char command[kLineBytes];
    snprintf(command, sizeof(command), "run %s", opts.run_app);
    if (!kernel::execute(command)) rc = 1;
    while (kernel::app_running()) kernel::poll();
  }
  if (opts.script != nullptr) {
    if (rc == 0) rc = run_script(opts.script);
  } else if (opts.run_app == nullptr) {
    while (kernel::running()) kernel::poll();
  }

  pal::shutdown();
  return rc;
}

#endif  // !UNIT_TEST && !PIO_UNIT_TESTING
