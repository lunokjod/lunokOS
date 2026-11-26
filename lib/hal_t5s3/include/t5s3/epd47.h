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
  uint16_t glass_width() const override { return kGlassW; }
  uint16_t glass_height() const override { return kGlassH; }
  void clear(uint8_t level) override;
  void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t level) override;
  void pixel(uint16_t x, uint16_t y, uint8_t level) override;
  void blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t* packed, uint16_t stride) override;

  // **Una pintada és una crida a epdiy**: the quality the kernel asks for is the draw mode the library takes, and the library
  // decides how many phases that is, with its own transition matrix and its own temperature (docs/50, docs/95).
  bool flush(UpdateMode mode) override;
  // **El rentat de debò d'aquest panell** (0.192.11): epdiy neix creient que el vidre és **blanc** —`epd_hl_init` posa el buffer que diu «què
  // hi ha al vidre» a `0xFF`—, o sigui que un `clear(15)` seguit d'un `gc16` és un diff buit: no s'hi envia ni un píxel i el dibuix del son es
  // queda on era mentre el registre diu que s'ha rentat (l'amo, 25/11: *«ENCARA VEIG LA IMATGE D'SLEEP SOTA EL GRID DEL HOME»*). El que sí que
  // esborra és el cicle del propi panell —`epd_clear()`: tres voltes de negre/blanc a tot el vidre, sense cap buffer pel mig—, i això és el
  // màxim que aquesta placa sap netejar (docs/50, docs/95).
  bool wash_the_glass() override;
  bool idle() override { return true; }
  // **I el torn del panell**: les tensions es queden enceses dos segons després de l'última pintada i després baixen (docs/50).
  void poll() override;
  // **I una pintada de mesura** (0.190.123): the same path as `flush` without the dirty rectangle of the page, so `panel bench` can say what a
  // mode costs on this panel —with the rails coming up and going down, que és el que una pintada de debò paga— without writing over the page
  // somebody is looking at (docs/50, docs/95).
  uint32_t paint_for_the_count(uint16_t x, uint16_t y, uint16_t w, uint16_t h, UpdateMode mode) override;
  // **Aquí l'àrea no és al preu**: epdiy envia les 540 files sempre i les que no han canviat hi van en blanc, o sigui que una franja de 48 files
  // costa el mateix que la pàgina sencera (docs/95). Els números són els mesurats a la placa i no els del full de dades (docs/95).
  uint32_t paint_cost_ms(uint16_t w, uint16_t h, UpdateMode mode) const override {
    (void)w;
    (void)h;
    switch (mode) {
      case UpdateMode::kDu:
      case UpdateMode::kDu4:
        return 128;
      case UpdateMode::kA2:
        return 290;
      case UpdateMode::kGl16:
      case UpdateMode::kGlr16:
        return 415;
      case UpdateMode::kGc16:
      case UpdateMode::kGld16:
        return 414;
      case UpdateMode::kInit:
        return 0;
    }
    return 0;
  }

  // **El vidre descansa** (0.183.0): epdiy baixa les tensions del TPS65185 i la imatge es queda perquè és tinta (docs/50).
  bool rest() override;

  // Which way the page lies on the glass: the glass is landscape and the page is a portrait page of 540 × 960, o sigui que el
  // gir és d'aquest fitxer i no del nucli. 90 and 270 are the two that fit (docs/50).
  bool set_rotation(uint16_t degrees) override;
  uint16_t rotation() const override { return rotation_; }

  // **La VCOM del vidre** (0.190.60): the number the TPS65185 is told before a paint, in millivolts. És l'únic paràmetre elèctric
  // que un driver pot moure i el firmware de fàbrica el deixa calibrar per unitat, o sigui que no és cap invent exposar-lo
  // (docs/50, docs/95).
  // **La forma d'ona, triada pel nom** (0.190.134): `tc2` és la que aquest build porta de sèrie —la que LilyGO va calibrar per a aquest
  // panell— i `tc1` la genèrica dels exemples, que es posa en calent amb `panel wave tc1`. Canviar-la és canviar el punter de l'estat
  // d'epdiy, o sigui que la pàgina que hi ha al vidre no es toca (docs/50).
  const char* waveform() const override { return waveform_; }
  const char* waveform_at(uint8_t index, uint8_t* modes = nullptr, uint8_t* ranges = nullptr) const override;
  bool set_waveform(const char* name) override;

  uint16_t vcom_mv() const override { return vcom_mv_; }
  bool set_vcom_mv(uint16_t mv) override;

  // **El rellotge del bus, que sí que és una palanca d'aquest projecte** (0.190.84): epdiy llegeix el rellotge del panell a la inicialització
  // i el baixa si la línia de la memòria cau és de 32 bytes (que és el cas de l'Arduino d'aquesta placa); el que decideix quant triga una
  // fila és aquest número, i per això es pot canviar en calent amb `panel bus <mhz>` i veure si la imatge aguanta (docs/50, docs/95).
  uint32_t bus_mhz() const override;
  bool set_bus_mhz(uint32_t mhz) override;

  // **El bus i la porta ja no són d'aquest projecte**: epdiy els té al seu `epd_board_v7` (vint megahertz, porta de sis
  // microsegons en una línia de dotze, CKV per RMT), o sigui que aquí no hi ha res a respondre i el prompt ho diu (docs/50).
  const char* last_error() const override { return error_; }

 private:
  // **Girar una àrea del framebuffer és el desfer d'una pintada de mesura** (0.190.124): `paint_for_the_count` la gira perquè hi hagi un canvi que
  // epdiy vulgui pintar, i la crida següent la torna a girar. It lives here and not in the kernel because the framebuffer is epdiy's (docs/50).
  void flip_the_area(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

  // The glass's own frame: 960 × 540, four bits a pixel, and it is **epdiy's buffer**, not a copy (docs/50).
  // **La VCOM d'aquest vidre**, in millivolts: **1560**, which is the value both examples of the board set (`epd_set_vcom(1560)`)
  // and the one the board ships with (docs/50, docs/95). It lives here, next to the member that keeps it, because a number written
  // twice is a number that can drift: the driver reads `vcom_mv_` and this is what it starts as.
  static constexpr uint16_t kVcomMillivolts = 1560;
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
  uint16_t vcom_mv_ = kVcomMillivolts;
  // **Quina taula és a l'estat d'epdiy ara mateix** (0.190.134): el nom, perquè el prompt pugui dir quina hi ha.
  const char* waveform_ = "tc2";
  // **Quan es va pintar per última vegada** (0.190.140): és el que decideix quan les tensions poden baixar.
  uint32_t last_paint_ms_ = 0;

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
