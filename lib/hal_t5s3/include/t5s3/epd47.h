// **El vidre d'aquesta placa, conduit per epdiy** (0.190.59, i és la decisió de l'amo: *«integra la llibreria i abandona el driver
// propi»*). This file used to declare a driver written from scratch in this project. It is now a thin adapter: the panel is an
// ED047TC1 of 4.7 inches, 960 × 540 and sixteen levels, with no controller between the chip and the glass, and the library that
// the board's own repository ships (`lib/epdiy`, version 2.0.0, LGPL-3) is the one that clocks it — rows, phases, waveform,
// temperature, supply and all.
//
// **What this file still owns** is the page: the mapping between the portrait page the kernel draws (540 × 960) and the
// landscape glass, the level of a pixel, and the rectangle that has changed. Nothing of the wire.
#ifndef LUNOK_T5S3_EPD47_H
#define LUNOK_T5S3_EPD47_H

#include <stdint.h>

#include "hal/hal.h"
#include "t5s3/expander.h"

namespace hal {

// The glass of this board: a page of 540 × 960 that lives in epdiy's framebuffer, and a paint that is a call into epdiy
// (docs/50).
class Epd47 : public ISurface {
 public:
  // **Els dos xips que la placa té al costat del panell** (the expander and the bus of the supply): epdiy obre el PCA9535 i el
  // TPS65185 ell mateix, o sigui que aquests dos no s'usen aquí i es queden a la signatura perquè la placa els té (docs/50).
  Epd47(Pca9535* expander, II2c* bus);

  // **El panell, en marxa**: epdiy s'encarrega del bus, dels pins, de la forma d'ona i de les tensions. False és un panell que no
  // hi és, i `last_error()` diu per què (docs/50).
  bool begin();

  uint16_t width() const override;
  uint16_t height() const override;
  void clear(uint8_t level) override;
  void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t level) override;
  void pixel(uint16_t x, uint16_t y, uint8_t level) override;
  void blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t* packed, uint16_t stride) override;

  // **Una pintada és una crida a epdiy**: the quality the kernel asks for is the draw mode the library takes, and the library
  // decides how many phases that is, with its own transition matrix and its own temperature (docs/50, docs/95).
  bool flush(UpdateMode mode) override;
  bool idle() override { return true; }

  // **El vidre descansa** (0.183.0): epdiy baixa les tensions del TPS65185 i la imatge es queda perquè és tinta (docs/50).
  bool rest() override;

  // Which way the page lies on the glass: the glass is landscape and the page is a portrait page of 540 × 960, o sigui que el
  // gir és d'aquest fitxer i no del nucli. 90 and 270 are the two that fit (docs/50).
  bool set_rotation(uint16_t degrees) override;
  uint16_t rotation() const override { return rotation_; }

  // **El bus i la porta ja no són d'aquest projecte**: epdiy els té al seu `epd_board_v7` (vint megahertz, porta de sis
  // microsegons en una línia de dotze, CKV per RMT), o sigui que aquí no hi ha res a respondre i el prompt ho diu (docs/50).
  const char* last_error() const override { return error_; }

 private:
  // The glass's own frame: 960 × 540, four bits a pixel, and it is **epdiy's buffer**, not a copy (docs/50).
  static const uint16_t kGlassW = 960;
  static const uint16_t kGlassH = 540;
  static const uint16_t kGlassStride = kGlassW / 2;
  static const uint16_t kPageW = 540;
  static const uint16_t kPageH = 960;

  void to_glass(uint16_t px, uint16_t py, uint16_t& gx, uint16_t& gy) const;
  void put(uint16_t gx, uint16_t gy, uint8_t level);
  uint8_t get(uint16_t gx, uint16_t gy) const;
  bool nothing_dirty() const { return dirty_x0_ > dirty_x1_ || dirty_y0_ > dirty_y1_; }
  void mark(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
  void forget_the_dirty();
  // **El rectangle marcat, portat al vidre** (0.190.40): les quatre cantonades, perquè el gir de noranta graus inverteix un dels
  // dos eixos i el mínim de les files no és el mínim de les columnes (docs/50).
  void dirty_glass(uint16_t& gx0, uint16_t& gy0, uint16_t& gx1, uint16_t& gy1) const;

  Pca9535* expander_;
  II2c* bus_;
  uint8_t* frame_ = nullptr;
  bool begun_ = false;
  bool powered_ = false;
  uint16_t rotation_ = 270;
  uint16_t dirty_x0_ = 0xFFFF;
  uint16_t dirty_y0_ = 0xFFFF;
  uint16_t dirty_x1_ = 0;
  uint16_t dirty_y1_ = 0;
  const char* error_ = "";
};

// The panel of this board, built once (docs/50).
ISurface* t5s3_epd47(Pca9535* expander, II2c* bus);

}  // namespace hal

#endif  // LUNOK_T5S3_EPD47_H
