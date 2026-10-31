// Keys read from the pins a board wired them to: active low, sampled in every turn,
// debounced by time.
#include <esp_timer.h>

#include <driver/gpio.h>

#include "esp32/gpio_keys.h"

namespace hal {
namespace {

int64_t now_ms() { return static_cast<int64_t>(esp_timer_get_time() / 1000); }

}  // namespace

GpioKeys::GpioKeys(const KeySpot* table, uint32_t count, uint32_t debounce_ms)
    : table_(table), count_(count), debounce_ms_(debounce_ms) {
  if (count_ > kMaxKeys) count_ = kMaxKeys;
  if (debounce_ms_ == 0) debounce_ms_ = 1;
}

bool GpioKeys::pressed(uint32_t key) {
  ensure_ready();
  if (key >= count_) return false;
  sample();
  return stable_[key];
}

int64_t GpioKeys::held_ms(uint32_t key) {
  if (key >= count_ || !stable_[key]) return 0;
  return now_ms() - pressed_since_ms_[key];
}

Face GpioKeys::face(uint32_t key) { return (key < count_) ? table_[key].face : Face::kTop; }

Spot GpioKeys::spot(uint32_t key) {
  return (key < count_) ? table_[key].spot : Spot::kCentre;
}

KeyKind GpioKeys::kind(uint32_t key) {
  return (key < count_) ? table_[key].kind : KeyKind::kSimple;
}

uint32_t GpioKeys::group(uint32_t key) { return (key < count_) ? table_[key].group : 0; }

void GpioKeys::ensure_ready() {
  if (ready_) return;
  // Input only and no pull: these pins may not be able to pull (34 to 39 cannot) and
  // the boards carry the resistors and close the key to ground (docs/50).
  gpio_config_t config = {};
  for (uint32_t key = 0; key < count_; ++key) {
    config.pin_bit_mask |= 1ULL << table_[key].pin;
  }
  config.mode = GPIO_MODE_INPUT;
  config.pull_up_en = GPIO_PULLUP_DISABLE;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&config);

  const int64_t now = now_ms();
  for (uint32_t key = 0; key < count_; ++key) {
    raw_[key] = read_pin(key);
    // A key that is already down when the system boots is not a press that nobody
    // made: it starts the clock from here (docs/30).
    stable_[key] = raw_[key];
    changed_ms_[key] = now;
    pressed_since_ms_[key] = now;
  }
  ready_ = true;
}

bool GpioKeys::read_pin(uint32_t key) const {
  return gpio_get_level(static_cast<gpio_num_t>(table_[key].pin)) == 0;
}

// A reading that does not hold for the debounce is not a press. The clock of the
// debounce starts again every time the pin moves.
void GpioKeys::sample() {
  const int64_t now = now_ms();
  for (uint32_t key = 0; key < count_; ++key) {
    const bool raw = read_pin(key);
    if (raw != raw_[key]) {
      raw_[key] = raw;
      changed_ms_[key] = now;
      continue;
    }
    if (now - changed_ms_[key] < static_cast<int64_t>(debounce_ms_)) continue;
    if (raw != stable_[key]) {
      stable_[key] = raw;
      if (stable_[key]) pressed_since_ms_[key] = changed_ms_[key];
    }
  }
}

}  // namespace hal
