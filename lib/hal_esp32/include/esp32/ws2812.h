// A strip of WS2812 chips that a board wired to one of its pins.
//
// **The ESP32 has no light.** What the chip has is RMT, and a WS2812 chip is written
// with it (100 ns tick, 0,8 us for a one and 0,4 us for a zero, three colours per light
// in GRB order). The board says the pin and how many lights there are; the writing is
// here, so the driver lives once and the hardware stays where it is (docs/50).
//
// The counts are template parameters and not run-time numbers on purpose: the two
// buffers below are the size of this board's strip and no bigger, and a buffer sized
// for the biggest board would be paid for by the smallest one (docs/75).
//
// How it goes on the wire is what the core already does for one light: a light is one
// WS2812 and the core's writer is 24 bits in 30 us. A strip is the same frame written
// in one go, because a light at a time is a frame of its own and the strip would latch
// every one of them (docs/50).
#ifndef LUNOK_ESP32_WS2812_H
#define LUNOK_ESP32_WS2812_H

#include <cstdio>
#include <stdint.h>

#include <esp32-hal-rgb-led.h>
#include <esp32-hal-rmt.h>

#include "hal/hal.h"

namespace hal {

constexpr float kWs2812TickNs = 100.0f;   // one RMT tick, the same the core sets
constexpr uint32_t kWs2812TicksOne = 8;   // a one: 0,8 us high...
constexpr uint32_t kWs2812TicksZero = 4;  // ...and 0,4 us low

// `kPin` is the board's, `kCount` is how many chips it put in a row, and `kMax` is the
// highest value a channel of those chips takes (docs/50).
template <uint8_t kPin, uint32_t kCount, uint32_t kMax>
class Ws2812Strip : public ILed {
 public:
  uint32_t count() override { return kCount; }
  uint32_t max_value() override { return kMax; }

  bool fill(uint8_t r, uint8_t g, uint8_t b) override {
    for (uint32_t light = 0; light < kCount; ++light) pixel(light, r, g, b);
    return true;
  }

  bool pixel(uint32_t index, uint8_t r, uint8_t g, uint8_t b) override {
    if (index >= kCount) return false;
    colours_[index * 3 + 0] = r;
    colours_[index * 3 + 1] = g;
    colours_[index * 3 + 2] = b;
    return true;
  }

  // One frame on the wire, and only when somebody asks for it: a lit light is current
  // this board spends, and the kernel is what decides when it changes (docs/75).
  void show() override {
    if (kCount == 0) return;
    if (kCount == 1) {
      // The core's writer for one chip: 24 bits, 30 us, blocking, and there is
      // nothing better to do with those 30 us (docs/50).
      neopixelWrite(kPin, colours_[0], colours_[1], colours_[2]);
      return;
    }
    if (!ensure_rmt()) return;
    for (uint32_t light = 0; light < kCount; ++light) {
      // GRB is the order this family's chips take, and it is the order the core
      // writes in (docs/50).
      const uint8_t channel[3] = {colours_[light * 3 + 1], colours_[light * 3 + 0],
                                  colours_[light * 3 + 2]};
      for (uint32_t colour = 0; colour < 3; ++colour) {
        for (uint32_t bit = 0; bit < 8; ++bit) {
          rmt_data_t& item = wire_[(light * 24) + (colour * 8) + bit];
          const bool one = (channel[colour] & (0x80u >> bit)) != 0;
          item.level0 = 1;
          item.duration0 = one ? kWs2812TicksOne : kWs2812TicksZero;
          item.level1 = 0;
          item.duration1 = one ? kWs2812TicksZero : kWs2812TicksOne;
        }
      }
    }
    // Blocking: 750 us on the twenty-five lights, and it is one frame at a time on one
    // wire. The kernel paints at most once per turn and only when it changes
    // (docs/75).
    if (!rmtWriteBlocking(rmt_, wire_, kCount * 24)) {
      printf("led: the RMT channel refused the frame; the strip is not painted\n");
    }
  }

 private:
  // One channel of the RMT, taken the first time a frame has to go out. The core's
  // own writer does the same for the single light (docs/50).
  bool ensure_rmt() {
    if (rmt_ != nullptr) return true;
    rmt_ = rmtInit(kPin, RMT_TX_MODE, RMT_MEM_64);
    if (rmt_ == nullptr) {
      printf("led: no RMT channel for the strip; the panel stays dark\n");
      return false;
    }
    rmtSetTick(rmt_, kWs2812TickNs);
    return true;
  }

  // What the strip has to show, three bytes per light, and how it goes on the wire:
  // 24 entries of four bytes per light.
  uint8_t colours_[kCount * 3] = {};
  rmt_data_t wire_[kCount * 24] = {};
  rmt_obj_t* rmt_ = nullptr;
};

}  // namespace hal

#endif  // LUNOK_ESP32_WS2812_H
