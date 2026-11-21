// **Una llum de fons, amb el PWM del xip** (0.190.99): GPIO11 de la T5 E-Paper S3 Pro va a l'`EN` de l'elevador PT4103B23F, i el full del
// fabricant demana que el senyal de regulació no passi d'1 kHz. El perifèric és el `ledc`, que és el mateix que fa servir el nucli
// d'Arduino per a `analogWrite` i que aquí es demana directament: el codi és el mateix als dos entorns (docs/50).
#include "esp32/backlight.h"

#include <driver/ledc.h>

#include "hal/log.h"

namespace hal {
namespace {

// Un canal i prou: una placa té una llum de fons, i el canal és d'aquest fitxer perquè cap app no l'ha de triar (docs/50).
constexpr ledc_channel_t kChannel = LEDC_CHANNEL_0;
constexpr ledc_timer_t kTimer = LEDC_TIMER_0;
constexpr ledc_mode_t kMode = LEDC_LOW_SPEED_MODE;
constexpr uint32_t kBits = 8;
constexpr uint32_t kSteps = 1u << kBits;
// La resolució és de vuit bits i el deure del `ledc` és sobre 2^8: el que una app demana (0–255) es converteix en un deure d'aquesta
// escala i no en una segona corba (docs/50).
constexpr uint32_t kDutyMax = kSteps - 1;

}  // namespace

Backlight::Backlight(int pin, uint32_t hz) : pin_(pin), hz_(hz) {
  if (pin_ < 0) return;
  ledc_timer_config_t timer = {};
  timer.speed_mode = kMode;
  timer.duty_resolution = static_cast<ledc_timer_bit_t>(kBits);
  timer.timer_num = kTimer;
  timer.freq_hz = hz_;
  timer.clk_cfg = LEDC_AUTO_CLK;
  if (ledc_timer_config(&timer) != ESP_OK) {
    logf(pal::kLogWarn, "LED", "the timer of the light did not come up");
    return;
  }
  ledc_channel_config_t channel = {};
  channel.gpio_num = pin_;
  channel.speed_mode = kMode;
  channel.channel = kChannel;
  channel.timer_sel = kTimer;
  channel.duty = 0;  // **neix apagada**: una llum que s'encén sola és corrent que ningú no ha demanat
  channel.hpoint = 0;
  channel.intr_type = LEDC_INTR_DISABLE;
  if (ledc_channel_config(&channel) != ESP_OK) {
    logf(pal::kLogWarn, "LED", "the light on %d did not come up", pin_);
    return;
  }
  ready_ = true;
}

uint8_t Backlight::luminance_of(uint8_t r, uint8_t g, uint8_t b) {
  // **El pes de l'ull** (Rec. 601): the three channels do not say the same thing about brightness, and a backlight is one number.
  const uint32_t luma = (static_cast<uint32_t>(r) * 299u + static_cast<uint32_t>(g) * 587u + static_cast<uint32_t>(b) * 114u) / 1000u;
  return static_cast<uint8_t>((luma > 255u) ? 255u : luma);
}

bool Backlight::fill(uint8_t r, uint8_t g, uint8_t b) {
  if (!ready_) return false;
  level_ = luminance_of(r, g, b);
  return true;
}

bool Backlight::pixel(uint32_t index, uint8_t r, uint8_t g, uint8_t b) {
  if (!ready_ || index != 0) return false;
  return fill(r, g, b);
}

void Backlight::show() {
  if (!ready_ || level_ == shown_) return;
  shown_ = level_;
  put(level_);
}

void Backlight::put(uint8_t level) {
  const uint32_t duty = (static_cast<uint32_t>(level) * kDutyMax) / 255u;
  if (ledc_set_duty(kMode, kChannel, duty) != ESP_OK) return;
  (void)ledc_update_duty(kMode, kChannel);
}

}  // namespace hal
