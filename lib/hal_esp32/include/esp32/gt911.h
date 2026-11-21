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
  // **Tornar-li la línia** (0.190.74): the nap of the chip arms GPIO36 as a level wake, and the interrupt type this driver installed —a falling
  // edge— is what the chip leaves behind. This puts the same configuration back, with its pull-up and its edge, which is what makes the
  // glass answer again after a light sleep (docs/50, docs/95).
  void rearm() override;

  uint8_t read(TouchPoint* out, uint8_t capacity) override;

  // **El consum del xip, en tres estats** (0.190.105, l'amo: *«Estalvía el maxim d'energia possible al panell... Manté el panell en un
  // consum baix fins a detectar un dit, llavors pujes la poténcia del panell durant uns segons i la tornes a deixar en LOW_LEVEL_QUERY»*).
  // The controller scans the glass by itself and that is what it costs: **how many scans a second** is a byte of its own configuration
  // (`0x8056`, `5 + (byte & 0x0F)` hertz) and the **interrupt mode** is two bits of `0x804D` (rising, falling, low level, high level).
  // The driver keeps it at the floor of the table —`kGt911QuietHz`, and the mode the manufacturer's example calls the low-power one,
  // `LOW_LEVEL_QUERY`: the line stays low while a finger is there and the host does not have to ask— and **raises it to the rate the
  // unit itself came with** for `kGt911HoldFastMs` after a finger, then puts it back (docs/50, docs/60, docs/95).
  bool sleep_now() override;
  bool wake_up() override;
  // **Adaptatiu o sempre al màxim** (0.190.107): with the switch on —the default— the chip drops to `kGt911QuietHz` when nobody is on the
  // glass and goes to the rate it came with while a finger is there; with it off it **never leaves that rate** while the board is awake. The
  // mode that is in force is said on the boot line (docs/30, docs/50).
  void set_adaptive_scan(bool on) override;
  void quiet_now() override;
  // **Vint hertzs mentre la llum del vidre és encesa** (0.190.119): the panel of this board is set to ten hertz by the maker and the floor
  // this driver keeps is ten too, so the burst after a finger had nothing to raise —the boot line said it before the owner did: *«10 Hz
  // quiet, 10 Hz awake»*—. A glass that is lit is a glass somebody is looking at, and half a scan is half a tap (docs/50, docs/95).
  void set_brisk(bool on) override;
  // What the chip is doing with its scans, for the boot line and `stats`: the hertz it is set to and whether it is asleep.
  uint32_t hertz() const { return hertz_; }
  bool asleep() const { return asleep_; }
  // **La línia es deixa anar sense lliurar cap frame** (M5Unified #91): the GT911 holds INT low while it has a frame
  // nobody has taken, and this is the read that takes it and writes the status back, which is what lifts the line. The
  // power asks it before arming a deep sleep on that line.
  bool flush() override;
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
  // **El ritme que el xip té ara i el que portava de fàbrica** (0.190.105): the value of the byte at `0x8056` (the hertz is five more)
  // and the one this unit came with, which is the fast one of the burst (docs/50).
  uint8_t quiet_rate_ = 0;
  uint8_t unit_rate_ = 0;
  // **El vidre està il·luminat i el dit no ha d'esperar** (0.190.119): while this is set the chip stays at `kGt911BriskHz` and `settle`
  // does not take it down; it goes back when the light goes out, which on this board is the dim of the backlight (docs/30, docs/50).
  bool brisk_ = false;
  uint32_t hertz_ = 0;
  // **Quan s'ha vist un dit per última vegada** (0.190.105): the fast rate is kept this long after it, perquè un dit que s'aixeca i
  // torna no pagui dues configuracions del xip en mig segon (docs/50).
  uint32_t last_finger_ms_ = 0;
  // **Si el règim baixa sol o no** (0.190.107): true is the adaptive glass of `kGt911QuietHz`, false is the one that stays at the rate of
  // the unit while the board is awake (docs/30, docs/50).
  bool adaptive_ = true;
  bool asleep_ = false;
  // **Escriure un byte de la configuració i fer-lo seu** (0.190.105): the block is read, one byte is changed, the checksum is written
  // and `0x8100` is set to one, which is the sequence the manufacturer's driver uses (`reloadConfig`) (docs/50).
  bool set_config_byte(uint8_t offset, uint8_t value);
  bool set_rate_byte(uint8_t value);
  bool set_interrupt_mode(uint8_t mode);
  void hold_the_line_as_output(bool level);
  void settle(uint32_t now_ms);
  bool find_it();
  // **Ninety-six and not sixty-four** (0.188.1): the boot line grew the rate of a scan, and the widest numbers a chip could
  // answer (a frame of 65535 and a rate of 4294967295) have to fit or the build refuses the `snprintf` (docs/50).
  char profile_[96] = {0};
};

// The glass of this board, or nullptr when the chip does not answer (docs/50). `int_pin` is the line the chip warns on.
ITouch* esp32_gt911(II2c* bus, int int_pin = -1);

}  // namespace hal

#endif  // LUNOK_ESP32_GT911_H
