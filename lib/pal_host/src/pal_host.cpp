// PAL for POSIX, used by the emulator and by the native tests.
//
// There is no threading here on purpose: the emulator is single threaded, and
// "block until an event or a deadline" becomes select() on stdin.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "pal/pal.h"

namespace {

// How many times the console was flushed. It lives outside `pal` because the tests ask for
// it by name (`pal_host::console_flushes`) and the object of the PAL is private (docs/85).
uint32_t g_flushes = 0;

}  // namespace

namespace pal {
namespace {

const uint16_t kQueueLength = 16;

class HostSystem : public ISystem, public IQueue, public IConsole {
 public:
  bool bring_up() {
    if (isatty(STDIN_FILENO) != 0 && tcgetattr(STDIN_FILENO, &saved_) == 0) {
      struct termios raw = saved_;
      raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
      raw.c_cc[VMIN] = 0;
      raw.c_cc[VTIME] = 0;
      if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) raw_active_ = true;
    }
    const struct timespec ts = now_ts();
    seed_ = static_cast<uint32_t>(ts.tv_nsec) ^ static_cast<uint32_t>(getpid() << 8);
    if (seed_ == 0) seed_ = 0x9e3779b9u;
    return true;
  }

  void restore_terminal() {
    if (!raw_active_) return;
    tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
    raw_active_ = false;
  }

  // ISystem -----------------------------------------------------------------

  IQueue* queue() override { return this; }
  IConsole* console() override { return this; }

  int64_t millis() const override {
    const struct timespec ts = now_ts();
    return static_cast<int64_t>(ts.tv_sec) * 1000 + static_cast<int64_t>(ts.tv_nsec / 1000000);
  }

  void reboot() override { reboot_ = true; }
  bool reboot_requested() const override { return reboot_; }

  uint32_t random() override {
    // xorshift32: deterministic when the seed is, and good enough for delays.
    seed_ ^= seed_ << 13;
    seed_ ^= seed_ >> 17;
    seed_ ^= seed_ << 5;
    return seed_;
  }

  HeapInfo heap() const override {
    HeapInfo info;
    info.valid = false;
    info.free_bytes = 0;
    info.min_free_bytes = 0;
    return info;
  }

  // IQueue ------------------------------------------------------------------

  bool push(const Event& ev, uint32_t timeout_ms) override {
    (void)timeout_ms;  // never full at the sizes we use; see capacity below
    if (count_ == kQueueLength) return false;
    ring_[tail_] = ev;
    tail_ = static_cast<uint16_t>((tail_ + 1) % kQueueLength);
    ++count_;
    return true;
  }

  bool pop(Event& out, int32_t timeout_ms) override {
    if (count_ == 0) wait_for_input(timeout_ms);
    if (count_ == 0) return false;
    out = ring_[head_];
    head_ = static_cast<uint16_t>((head_ + 1) % kQueueLength);
    --count_;
    return true;
  }

  uint16_t size() const override { return count_; }
  uint16_t capacity() const override { return kQueueLength; }

  // IConsole ----------------------------------------------------------------

  size_t write(const void* data, size_t len) override {
    ssize_t n = ::write(STDOUT_FILENO, data, len);
    return (n < 0) ? 0 : static_cast<size_t>(n);
  }

  // A pipe to a person's terminal has no buffer of ours to wait for, but the count is
  // kept so a test can see that the kernel did ask before sleeping (docs/85).
  void flush() override {
    ::fflush(stdout);
    ++g_flushes;
  }

  size_t read(void* data, size_t len) override {
    if (input_eof_ || len == 0) return 0;
    // A terminal in raw mode has VMIN=0: read() returns 0 when there is simply
    // no data yet, and that is NOT end of file. Ask select() first and only
    // treat a 0 that comes after "readable" as EOF. Getting this wrong declares
    // the console dead on the first empty poll and loses every later keystroke,
    // which is exactly what happened once.
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    const int ready = select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);
    if (ready <= 0) return 0;
    if (FD_ISSET(STDIN_FILENO, &fds) == 0) return 0;
    const ssize_t n = ::read(STDIN_FILENO, data, len);
    if (n < 0) return 0;
    if (n == 0) {
      input_eof_ = true;  // the pipe is closed or the terminal went away
      return 0;
    }
    return static_cast<size_t>(n);
  }

  bool ansi() const override {
    return isatty(STDOUT_FILENO) != 0 && getenv("TERM") != nullptr;
  }

 private:
  static struct timespec now_ts() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts;
  }

  void wait_for_input(int32_t timeout_ms) {
    if (input_eof_) {
      // Nothing can arrive any more: still honour the deadline, without spinning.
      if (timeout_ms > 0) {
        struct timespec ts;
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = static_cast<long>(timeout_ms % 1000) * 1000000L;
        nanosleep(&ts, nullptr);
      }
      return;
    }
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);

    struct timeval tv;
    struct timeval* ptv = nullptr;
    if (timeout_ms >= 0) {
      tv.tv_sec = timeout_ms / 1000;
      tv.tv_usec = (timeout_ms % 1000) * 1000;
      ptv = &tv;
    }

    const int ready = select(STDIN_FILENO + 1, &fds, nullptr, nullptr, ptv);
    if (ready > 0 && FD_ISSET(STDIN_FILENO, &fds) != 0) {
      Event ev;
      ev.type = kEventConsoleRx;
      ev.a = 0;
      ev.b = 0;
      push(ev, 0);
    }
  }

  Event ring_[kQueueLength];
  uint16_t head_ = 0;
  uint16_t tail_ = 0;
  uint16_t count_ = 0;

  struct termios saved_;
  bool raw_active_ = false;
  bool input_eof_ = false;
  bool reboot_ = false;
  uint32_t seed_ = 1;
};

HostSystem g_system;

}  // namespace

bool init() { return g_system.bring_up(); }

void shutdown() { g_system.restore_terminal(); }

ISystem& system() { return g_system; }

}  // namespace pal

namespace pal_host {

uint32_t console_flushes() { return g_flushes; }

}  // namespace pal_host
