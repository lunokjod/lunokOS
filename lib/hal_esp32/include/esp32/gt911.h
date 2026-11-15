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
// - **and the same line is the chip's own word that it has a frame** (0.188.0): the GT911 drives it low when a scan has
//   something to hand over, which is how M5's `M5EPD` works —`TP.begin(21, 22, 36)` and `attachInterrupt(..., FALLING)`—
//   instead of asking the status register on every turn. This driver listens to it when it is given the pin, and then a
//   read that nobody warned about answers without touching the bus at all (docs/50, docs/95).
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
// **How often the chip scans the glass is the chip's own number** (0.188.1): the byte at `0x8056` of its configuration
// says it, and the vendor driver reads it the same way (`5 + (byte & 0x0F)` hertz, `M5GFX`). **Ten milliseconds was an
// assumption and it was the wrong one**: the configuration of this unit says twenty hertz —one scan every fifty—, and the
// kernel was told ten, so its window of silence was thirty milliseconds and **a finger that was moving was declared gone
// in the middle of a swipe**, which is a page that turns sometimes and sometimes not (docs/50, docs/95).
const uint32_t kGt911ScanMsFallback = 10;

// The period of a scan, from the byte the chip keeps at `0x8056` (offset 15 of the block that starts at `0x8047`):
// **twenty hertz is fifty milliseconds and that is this unit**, measured in its own configuration and not on a scope yet.
// A byte outside the table's five to twenty hertz is the fallback, because a number that makes the kernel wait for ever is
// worse than one that is a little off (docs/50, docs/95).
inline uint32_t gt911_scan_ms(uint8_t rate_byte) {
  const uint32_t hz = 5u + (rate_byte & 0x0Fu);
  const uint32_t ms = 1000u / hz;
  return (ms < 1u) ? 1u : ((ms > 200u) ? 200u : ms);
}

class Gt911 : public ITouch {
 public:
  // `int_pin` is the line the chip pulls down when it has a frame —GPIO36 on the Paper— and **-1 is a board that does not
  // say**, which leaves the driver asking the status register at the rate of `scan_ms` (docs/50).
  Gt911(II2c* bus, int int_pin = -1) : bus_(bus), int_pin_(int_pin) {}

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
  uint32_t scan_ms() const override { return scan_ms_; }
  // **El vidre avisa** (0.188.0): the line is wired and listened to, so a read with nothing behind it costs nothing and the
  // kernel may ask on every turn (docs/50).
  bool warns() const override { return int_pin_ >= 0; }
  // **Quants avisos no tenien cap frame darrere** (0.189.86): the line is a wire that picks up whatever the board is doing when
  // the chip lets it go, and a warning that finds no frame costs the driver twenty milliseconds of waiting. Zero is a line
  // that only falls for a finger (docs/50, docs/95).
  uint32_t false_warnings() const override;
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
  // The line the chip warns on, or -1 on a board where nobody told the driver about it (0.188.0).
  int int_pin_ = -1;
  uint8_t address_ = kGt911Address;
  uint8_t points_ = kGt911MaxPoints;
  uint16_t width_ = 0;
  uint16_t height_ = 0;
  // **El ritme que el xip diu** (0.188.1): a zero until the configuration is read at the boot, which is when the number
  // becomes this unit's and not the fallback (docs/50).
  uint32_t scan_ms_ = kGt911ScanMsFallback;
  // Whether the last frame the chip handed over had a finger on it (0.183.2). It starts as `true` because a chip that has
  // said nothing has said nothing about a lift either, and a contact is the safe answer (docs/50).
  bool down_ = true;
  // **Ninety-six and not sixty-four** (0.188.1): the boot line grew the rate of a scan, and the widest numbers a chip could
  // answer (a frame of 65535 and a rate of 4294967295) have to fit or the build refuses the `snprintf` (docs/50).
  char profile_[96] = {0};
};

// The glass of this board, or nullptr when the chip does not answer (docs/50). `int_pin` is the line the chip warns on.
ITouch* esp32_gt911(II2c* bus, int int_pin = -1);

}  // namespace hal

#endif  // LUNOK_ESP32_GT911_H
