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

  // What a test asks: the pixels themselves, and how the panel was asked to work.
  const uint8_t* bits() const { return bits_; }
  uint8_t level_at(uint16_t x, uint16_t y) const;
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
  uint8_t* bits_ = nullptr;
  uint32_t blits_ = 0;
  uint32_t flushes_ = 0;
  uint32_t paint_count_ = 0;
  Paint paints_[kMaxPaints] = {};  hal::UpdateMode last_mode_ = hal::UpdateMode::kDu;
  // **The rectangle that changed since the last paint**, which is what the panel is told to send: every
  // write to the surface marks it and a flush takes it away, exactly as `It8951::mark` does (docs/50).
  // Keeping only the last `blit` made a paint carry another copy's region: the bar writes with
  // `fill_rect`, so its paint was recorded with the last thing an app had composed.
  void mark(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
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

// The panel of this bench, made the first time it is asked for. It is 960x540 —the Paper's glass— and
// it exists only when the run asked for one (`--display`), because a board without a screen is the
// default and the one most tests want (docs/85).
MemorySurface* memory_surface();

}  // namespace emu

#endif  // LUNOK_EMU_SURFACE_H
