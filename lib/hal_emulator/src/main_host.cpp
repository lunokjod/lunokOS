// Entry point of the emulator.
//
// PlatformIO defines UNIT_TEST (and PIO_UNIT_TESTING) while running `pio test`;
// then the test framework brings its own main() and this one steps aside.
#if !defined(UNIT_TEST) && !defined(PIO_UNIT_TESTING)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emu/options.h"
#include "hal/hal.h"
#include "kernel/kernel.h"
#include "kernel/shell.h"
#include "pal/pal.h"

namespace {

const size_t kLineBytes = 128;

// How long a batch run waits for one app before giving up on it: an app reading
// a key that never arrives must not hang the script for ever.
const uint32_t kBatchWaitMs = 30000;

void usage() {
  printf("lunokOS emulator\n");
  printf("  --fs <dir>        working disk (default: emulator)\n");
  printf("  --mem-cap <KB>    Lua heap ceiling\n");
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
  printf("  --run <app>       start an app\n");
  printf("  --script <file>   batch mode: run and exit\n");
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
    if (strcmp(arg, "--fs") == 0) {
      if (!next_value(i, argc, argv, value)) return false;
      opts.fs_dir = value;
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

  if (!pal::init()) {
    fprintf(stderr, "cannot initialise the console\n");
    return 2;
  }

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
