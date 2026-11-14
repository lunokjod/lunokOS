// The glass of the M5Paper: a Goodix **GT911** on the internal I2C, next to the clock chip, the
// FRAM and the air sensor (docs/50).
//
// What is known about it, and where it comes from: the chip's register map, as the two vendor code
// bases use it —M5's own `M5GFX` (`Touch_GT911`) and Espressif's `esp_lcd_touch_gt911`:
//
// - the registers are **two bytes wide** (`0x814E` is a status, not a byte), the product id is at
//   `0x8140` and reads the three characters `911`, and the configuration block starts at `0x8047`
//   and is **186 bytes** long with the number of fingers at `0x804C` and the two maxima at `0x8048`
//   and `0x804A`;
// - a frame is one status byte plus up to **five** points of **eight** bytes: track id, x, y, the
//   size of the contact and one reserved byte, the first point at `0x814F`;
// - in the status byte `0x80` means "there is a frame" and the low nibble is how many fingers, and
//   **it is cleared by writing a zero to it**: a frame that is not cleared comes back for ever as
//   if the finger were still there;
// - the address is `0x14` or `0x5D`, chosen by the level of the INT line while the chip resets.
//   On this board that line is **GPIO36, which is an input**, so the firmware cannot choose it and
//   this driver asks both.
//
// **Gestures are not the chip's business here.** The GT911 has a gesture feature of its own and
// neither vendor driver reads it; neither does this one. A tap, a long press, a drag or two fingers
// are shapes of the point stream, they mean the same on any glass, and they can be tested on the PC
// with a fake — which a gesture the chip invents could not be (docs/50, docs/85).
#ifndef LUNOK_ESP32_GT911_H
#define LUNOK_ESP32_GT911_H

#include <stdint.h>

#include "esp32/i2c.h"
#include "hal/hal.h"

namespace hal {

// The two addresses the chip can be strapped to, and the one it answers on when nobody drives the
// pin that chooses (docs/50).
const uint8_t kGt911Address = 0x14;
const uint8_t kGt911AddressBackup = 0x5D;
// Five fingers at once and no more: the frame has room for five and both vendor drivers stop there.
const uint8_t kGt911MaxPoints = 5;
// **How often the chip scans the glass**: ten milliseconds, the period both vendor drivers work to and the one a finger
// of a hundred milliseconds is always seen in. It is what the kernel is told so that it asks once per scan and not once
// per turn (docs/50, docs/95).
const uint32_t kGt911ScanMs = 10;

class Gt911 : public ITouch {
 public:
  explicit Gt911(II2c* bus) : bus_(bus) {}

  // Who is there, in which frame it counts and how many fingers it will report. Asked **once**, at
  // boot: a glass that is promised and never answers is worse than one that is not announced.
  bool probe();

  // What the chip said, for the boot line: the address, the product id, the version of its
  // configuration and the frame. It is the only way to know what this unit is without a scope.
  const char* profile() const { return profile_; }

  // The chip can report five and this one's configuration decides how many it actually will: this
  // unit says two (config 66, measured on the Paper, 1/11), and a number the chip will never exceed
  // is the honest one to give an app (docs/50, docs/95).
  uint8_t max_points() const override { return points_; }
  uint16_t width() const override { return width_; }
  uint16_t height() const override { return height_; }
  uint32_t scan_ms() const override { return kGt911ScanMs; }
  uint8_t read(TouchPoint* out, uint8_t capacity) override;
  // **El que diu el darrer frame que el xip ha lliurat** (0.183.2): a frame with points is a finger on the glass and a frame
  // with none is the lift. The driver used to throw the second one away —`read` answered zero for it, the same word as “no
  // frame yet”—, and the system had to **guess** a lift from a silence of thirty milliseconds: a chip that reports slower
  // than that, or a paint that holds the turns, turned one finger into a run of contacts —and a contact that ends under a
  // finger is a tap, which is a row clicked at the end of a swipe (docs/50, docs/90).
  bool finger_still_down() const override { return down_; }

 private:
  // Tells the chip that the frame has been taken. Without it the same finger is read for ever.
  void release();

  II2c* bus_;
  uint8_t address_ = kGt911Address;
  uint8_t points_ = kGt911MaxPoints;
  uint16_t width_ = 0;
  uint16_t height_ = 0;
  // Whether the last frame the chip handed over had a finger on it (0.183.2). It starts as `true` because a chip that has
  // said nothing has said nothing about a lift either, and a contact is the safe answer (docs/50).
  bool down_ = true;
  char profile_[64] = {0};
};

// The glass of this board, or nullptr when the chip does not answer (docs/50).
ITouch* esp32_gt911(II2c* bus);

}  // namespace hal

#endif  // LUNOK_ESP32_GT911_H
