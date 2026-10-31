// A battery on an ADC pin behind a divider: the chip asks the pin, the board says which.
#include "esp32/battery.h"

#include <driver/adc.h>
#include <esp_adc_cal.h>

namespace hal {
namespace {

// The pins of ADC1 on this chip, by the number the board writes (docs/50). ADC2 is not
// here on purpose: the radio uses it, and a battery that cannot be read while the radio
// is up is a battery that lies exactly when it matters.
bool channel_of(int pin, adc1_channel_t& out) {
  switch (pin) {
    case 36:
      out = ADC1_CHANNEL_0;
      return true;
    case 37:
      out = ADC1_CHANNEL_1;
      return true;
    case 38:
      out = ADC1_CHANNEL_2;
      return true;
    case 39:
      out = ADC1_CHANNEL_3;
      return true;
    case 32:
      out = ADC1_CHANNEL_4;
      return true;
    case 33:
      out = ADC1_CHANNEL_5;
      return true;
    case 34:
      out = ADC1_CHANNEL_6;
      return true;
    case 35:
      out = ADC1_CHANNEL_7;
      return true;
    default:
      return false;
  }
}

esp_adc_cal_characteristics_t g_chars;
bool g_chars_ready = false;

// 12 bits and 11 dB: the range that reaches 3,1 V at the pin, which is 6,2 V behind the
// x2 divider of a battery that never goes past 4,2. The characterisation is done once
// and it reads the eFuse, which is what turns counts into millivolts instead of into a
// guess (docs/50).
bool characterise() {
  if (g_chars_ready) return true;
  if (adc1_config_width(ADC_WIDTH_BIT_12) != ESP_OK) return false;
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &g_chars);
  g_chars_ready = true;
  return true;
}

}  // namespace

bool esp32_battery(const BatteryPins& pins, int& mv) {
  adc1_channel_t channel;
  if (!channel_of(pins.pin, channel)) return false;
  if (!characterise()) return false;
  if (adc1_config_channel_atten(channel, ADC_ATTEN_DB_11) != ESP_OK) return false;

  // Four reads averaged: one conversion on this ADC is worth a percent or two and the
  // battery is in no hurry. A read that fails is not a zero: the whole answer is false.
  int raw = adc1_get_raw(channel);
  if (raw < 0) return false;
  for (int i = 0; i < 3; ++i) {
    const int again = adc1_get_raw(channel);
    if (again >= 0) raw = (raw + again) / 2;
  }

  const uint32_t pin_mv = esp_adc_cal_raw_to_voltage(static_cast<uint32_t>(raw), &g_chars);
  const uint32_t factor = (pins.factor > 0) ? pins.factor : 1;
  const int32_t full_mv = static_cast<int32_t>(pin_mv) * static_cast<int32_t>(factor);

  // A single cell lives between 3,0 and 4,3 V. Anything under a volt is a divider that is
  // not there —a pin left floating reads near zero and that is not a flat battery— and
  // anything over six is a factor that is wrong (docs/50).
  if (full_mv < 1000 || full_mv > 6000) return false;
  mv = full_mv;
  return true;
}

}  // namespace hal
