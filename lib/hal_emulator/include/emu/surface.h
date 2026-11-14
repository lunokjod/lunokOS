// A panel that is a buffer: what the emulator shows a board with a glass, and what a native test
// checks the system against (docs/85).
//
// It is the same contract the Paper's IT8951 implements (`hal::ISurface`), so the canvas of an app,
// the composition and the dirty rectangle are all tried on the PC — where a breakage is a failed
// assertion and not a page nobody can read.
#ifndef LUNOK_EMU_SURFACE_H
#define LUNOK_EMU_SURFACE_H

#include <stdint.h>

#include "hal/hal.h"

namespace emu {

class MemorySurface : public hal::ISurface {
 public:
  MemorySurface(uint16_t width, uint16_t height);

  uint16_t width() const override { return width_; }
  uint16_t height() const override { return height_; }
  bool set_rotation(uint16_t degrees) override;
  uint16_t rotation() const override { return rotation_; }

  void clear(uint8_t level) override;
  void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t level) override;
  void pixel(uint16_t x, uint16_t y, uint8_t level) override;
  void blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t* packed,
            uint16_t stride) override;
  bool flush(hal::UpdateMode mode) override;
  bool idle() override;
  // **The rectangle of a partial paint** (0.179.12): the glass of the emulator has no controller and no image walk,
  // so it takes any box —and it says so, which is what lets the prompt's `panel` be a question with two answers
  // here and one on the board (docs/50, docs/85).
  bool partial_from_the_box() const override { return from_box_; }
  bool set_partial_from_the_box(bool on) override {
    from_box_ = on;
    return true;
  }
  // **Why a paint was refused**, in the words of the driver of the Paper (`--ink-refuse`), for the line the caller
  // writes (0.118.2, docs/50, docs/85).
  const char* last_error() const override { return why_; }
  // **The panel of the emulator goes to sleep like the board's** (0.183.0): it says so to the console and refuses the paints
  // that come after, which is what the controller of the Paper does with the command. A test that asks the glass for a
  // picture after the way down would otherwise get a page, and the board gets nothing (docs/50, docs/85).
  bool rest() override;
  // Whether the sleep has been asked for, which is what a test reads.
  bool resting() const { return resting_; }

  // What a test asks: the pixels themselves, and how the panel was asked to work.
  const uint8_t* bits() const { return bits_; }
  uint8_t level_at(uint16_t x, uint16_t y) const;

  // **What the glass shows, which is not always what was painted** (docs/85). `level_at` is the level
  // the paint asked for —the contract of the driver— and `shown_at` is where the pigment ended up: a
  // `du` keeps a quarter of the page before it and the traces add up, a `gc16` cleans. With the ink off
  // —tests— the two are the same buffer.
  const uint8_t* shown_bits() const { return (shown_ != nullptr) ? shown_ : bits_; }
  uint8_t shown_at(uint16_t x, uint16_t y) const;
  // The dirt of one pixel, in sixteenths: what the paints that do not clean left behind (docs/85).
  uint8_t dirt_at(uint16_t x, uint16_t y) const;
  // How many pixels of the glass do not show the level they were painted with.
  uint32_t ink_errors() const;
  // True when this emulator paints with the model of the glass instead of instantly and perfectly.
  bool ink_on() const { return shown_ != nullptr; }
  // **Turn the model on or off for this run.** A test that is about the ink says so and turns it on; the
  // rest of them run with a paint that is instant and exact, and do not pay half a megabyte for a
  // picture they are not looking at (docs/85).
  bool set_ink(bool on);
  // **The flash of an `a2`, one stage at a time**: the page goes white, then black, then the image, and
  // this is what moves it from one stage to the next. It is asked by `idle()`, which is what an app that
  // waits for the ink calls, and by the window, which is what a person looks at (docs/85).
  void step_ink();
  // **How many times the glass changed on its own**, without a paint: the two stages of a flash. A
  // window that only redrew on `flushes()` would miss them, and a flash nobody sees is not a flash
  // (docs/85).
  uint32_t ink_steps() const { return ink_steps_; }
  // **What to do while the glass waits.** A paint blocks its caller for the wires and for the ink, and
  // the panel does not: it keeps blinking through both. The emulator that has a window installs this and is
  // called every few milliseconds of every wait, which is how a flash reaches a person's eyes instead of
  // happening inside a delay nobody draws (docs/85).
  using WaitPump = void (*)(void* user);
  void set_wait_pump(WaitPump pump, void* user) {
    pump_ = pump;
    pump_user_ = user;
  }
  uint32_t blits() const { return blits_; }
  uint32_t flushes() const { return flushes_; }
  hal::UpdateMode last_mode() const { return last_mode_; }
  uint16_t last_x() const { return last_x_; }
  uint16_t last_y() const { return last_y_; }
  uint16_t last_w() const { return last_w_; }
  uint16_t last_h() const { return last_h_; }

  // One paint asked of the glass: the region that had just been copied, and the quality it was asked
  // for. **The last eight are kept**, because a present with several qualities is several paints and a
  // test has to be able to see each one of them (docs/30, docs/85).
  struct Paint {
    hal::UpdateMode mode;
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
  };
  static const uint32_t kMaxPaints = 8;
  uint32_t paint_count() const { return paint_count_; }
  // The i-th paint of the whole run, or nullptr when it is older than the ones kept.
  const Paint* paint_at(uint32_t index) const;

 private:
  uint16_t width_;
  uint16_t height_;
  uint16_t stride_;
  uint16_t rotation_ = 0;
  // Whether a partial paint asks for the box of the change, which on this glass is always a yes (0.179.16).
  bool from_box_ = true;
  uint8_t* bits_ = nullptr;
  // **The two buffers of the ink**: what the glass shows, and how dirty each pixel is, in four bits
  // each. They exist only when the model is on, because a test wants a paint that is instant and exact
  // and 500 KB more of buffer to read a pixel is not what it is testing (docs/85).
  uint8_t* shown_ = nullptr;
  uint8_t* dirt_ = nullptr;
  // When the ink of the last paint will have finished moving. `idle()` is the question for it.
  uint32_t busy_until_ms_ = 0;
  // The sentence of the last refusal, as the driver of the board says it (docs/50).
  const char* why_ = "";
  // Whether the panel has been told to sleep (0.183.0).
  bool resting_ = false;
  // **A flash that is running**: the region, the quality that asked for it, when it started and when it
  // is over. Zero in `flash_until_ms_` is no flash, and `flash_stage_` says which of the three moments
  // the glass is in (white, black, the image) (docs/85).
  uint32_t flash_start_ms_ = 0;
  uint32_t flash_until_ms_ = 0;
  uint8_t flash_stage_ = 0;
  uint16_t flash_x_ = 0;
  uint16_t flash_y_ = 0;
  uint16_t flash_w_ = 0;
  uint16_t flash_h_ = 0;
  hal::UpdateMode flash_mode_ = hal::UpdateMode::kA2;
  uint32_t ink_steps_ = 0;
  uint32_t blits_ = 0;
  uint32_t flushes_ = 0;
  uint32_t paint_count_ = 0;
  Paint paints_[kMaxPaints] = {};  hal::UpdateMode last_mode_ = hal::UpdateMode::kDu;
  // **The rectangle that changed since the last paint**, which is what the panel is told to send: every
  // write to the surface marks it and a flush takes it away, exactly as `It8951::mark` does (docs/50).
  // Keeping only the last `blit` made a paint carry another copy's region: the bar writes with
  // `fill_rect`, so its paint was recorded with the last thing an app had composed.
  void mark(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
  // What the old frame owed is not owed by the new one.
  void reset_damage();
  // **The paint, pixel by pixel**: the region that was asked for is moved towards what it was painted
  // with, according to the quality and to the dirt it already had (docs/85).
  void apply_ink(uint16_t x, uint16_t y, uint16_t w, uint16_t h, hal::UpdateMode mode);
  // What a flash writes on the glass while it is running, and the end of it: the image and the ink of
  // the paint (docs/85).
  void flash_fill(uint8_t level);
  // **The image arriving after the last blink**, in `steps` pieces: the region moves from the level of
  // the blink to the level the paint asked for, and the dirt of the paint is added on the last one
  // (docs/85).
  void settle_flash(uint8_t step, uint8_t steps);
  // The blink the paint is in, and the piece of the settle: `0xFF` is "no step yet".
  uint8_t ink_phases_ = 0;
  uint8_t ink_settle_steps_ = 0;
  uint8_t ink_stage_ = 0xFF;
  // A quick paint whose image has not landed yet: it lands at the end of its time and not at the start.
  bool ink_pending_ = false;
  // **A wait of the glass, walked in slices**: the pump of the window is called between them, so the
  // blinks of a paint nobody's code is looking at still get drawn (docs/85).
  void wait_and_pump(uint32_t us);
  WaitPump pump_ = nullptr;
  void* pump_user_ = nullptr;
  void alloc_ink();
  bool make_ink();
  bool dirty_ = false;
  uint16_t dirty_x0_ = 0;
  uint16_t dirty_y0_ = 0;
  uint16_t dirty_x1_ = 0;
  uint16_t dirty_y1_ = 0;
  uint16_t last_x_ = 0;
  uint16_t last_y_ = 0;
  uint16_t last_w_ = 0;
  uint16_t last_h_ = 0;
};

// The panel of this emulator, made the first time it is asked for. It is 960x540 —the Paper's glass— and
// it exists when `Options::display` says so, which is what the binary does for a model that has one
// (`Model::glass`) and what a test leaves off unless it is about the panel (docs/85).
MemorySurface* memory_surface();

}  // namespace emu

#endif  // LUNOK_EMU_SURFACE_H
