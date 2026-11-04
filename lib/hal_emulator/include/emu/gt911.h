// **The touch panel of the bench, as a chip and not as a function** (docs/50, docs/85).
//
// A GT911 does not answer "where is the finger". It scans the glass by itself, keeps the frame of the
// last scan, and hands that frame over **once** —the driver empties it by writing a zero to the status
// register— and everything of it worth simulating is in that sentence: the time between reports, a
// frame that a caller polling faster than the glass misses, and a read that costs what three
// transactions cost on the board's bus. What is not here is a finger: on a PC there is none, and the
// fingers are the test's, the same way the keys are `press_key`'s (docs/85).
#ifndef LUNOK_EMU_GT911_H
#define LUNOK_EMU_GT911_H

#include <stdint.h>

#include "hal/hal.h"

namespace emu {

class Gt911Panel : public hal::ITouch {
 public:
  // **The configuration of this unit, measured on the board on 1/11**: the frame it counts in and the
  // two fingers it will report, from `touch: GT911 at 0x5d, config 66, 540x960, 2 fingers`
  // (docs/95, E-6). Five is what the frame holds and two is what this chip was told to report.
  static const uint8_t kMaxFingers = 2;
  static const uint16_t kFrameWidth = 540;
  static const uint16_t kFrameHeight = 960;

  uint8_t max_points() const override { return kMaxFingers; }
  uint16_t width() const override { return kFrameWidth; }
  uint16_t height() const override { return kFrameHeight; }
  uint8_t read(hal::TouchPoint* out, uint8_t capacity) override;
  // **A frame the chip kept after the finger left says so**: the bench knows if a finger is on the glass right now,
  // and it is what tells a tap that is over from a key held down (docs/50, docs/85).
  bool finger_still_down() const override { return fingers_down() > 0; }

  // **The fingers are the test's.** Slot 0 or 1, in the chip's own frame, which on this board is the
  // page's own frame and the right way up (docs/95, E-6). False when the slot does not exist, when the
  // point is not on the glass, or when a finger that is not down is moved.
  bool finger_down(uint8_t slot, uint16_t x, uint16_t y);
  bool finger_move(uint8_t slot, uint16_t x, uint16_t y);
  bool finger_up(uint8_t slot);
  bool finger_at(uint8_t slot, uint16_t& x, uint16_t& y) const;
  uint8_t fingers_down() const;

  // **What a read costs, in microseconds, for that many fingers**: the status, the frame of the points
  // and the zero that releases them, on the bus of the board (100 kHz, nine bits a byte, docs/50).
  static uint32_t read_us(uint8_t fingers);
  // What asking who is there costs: the product id and the 186 bytes of configuration, once per boot
  // (docs/50).
  static uint32_t probe_us();

  // **How many frames the chip has handed over, and how many reads found the frame already taken.** A
  // second number that is not zero is a caller polling faster than the glass scans, which is the thing
  // this model exists to make visible (docs/85).
  uint32_t frames() const { return frames_; }
  uint32_t missed() const { return missed_; }
  // The gap between two reports of the chip, in milliseconds.
  uint32_t scan_ms() const;
  // A power cycle: no finger on the glass and no frame pending.
  void reset();

 private:
  struct Finger {
    bool down = false;
    uint16_t x = 0;
    uint16_t y = 0;
  };
  Finger fingers_[kMaxFingers];
  // **A finger that was on the glass since the last frame was taken**, which is the frame the chip keeps:
  // it is set by a finger coming down or moving, and only a read takes it away (docs/50, docs/85).
  bool latched_ = false;
  // The slot of the finger that touched the glass last, which is the one the latched frame is about.
  uint8_t latched_slot_ = 0;
  // The scan whose frame has already been given away, and the counters of the caller's behaviour.
  uint32_t taken_ms_ = 0xFFFFFFFFu;
  uint32_t frames_ = 0;
  uint32_t missed_ = 0;
};

// The panel of this bench, made the first time it is asked for (`--touch`), like the glass. The first
// call pays the probe and says the line the board says before the prompt (docs/85).
Gt911Panel* touch_panel();

}  // namespace emu

#endif  // LUNOK_EMU_GT911_H
