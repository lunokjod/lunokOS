// **El vidre d'aquesta placa el porta epdiy** (0.190.59). This file used to be a driver written from scratch: the i80 bus, the
// row sequence, the waveform table, the gate window, the supply and the measurement of a paint. All of that is gone, because the
// board's own repository ships the driver that does it — `lib/epdiy`, version 2.0.0, LGPL-3, the same one its factory firmware
// runs (`epd_init(&epd_board_v7, &ED047TC1, EPD_LUT_64K)`, `epd_hl_init(EPD_BUILTIN_WAVEFORM)`, `epd_hl_update_screen(...,
// MODE_GC16, ...)`) — and the owner asked for it plainly: *«integra la llibreria i abandona el driver propi»*.
//
// **What is left here is the page**: the portrait page of 540 × 960 over a landscape glass, the level of a pixel, the rectangle
// that changed, and the translation from the kernel's four qualities to epdiy's draw modes. Nothing of the wire, and nothing of
// the ink (docs/50, docs/95).
#include "t5s3/epd47.h"

#include <cstring>

#include "epd_highlevel.h"
#include "epdiy.h"
#include "hal/log.h"

namespace hal {
namespace {

// **El pont entre el nucli i epdiy** (0.190.59): epdiy keeps one state per panel and the whole framebuffer of the page lives in it,
// o sigui que aquest driver no té cap quadre propi i el que dibuixa el nucli és el seu (docs/50).
EpdiyHighlevelState g_state;
bool g_ready = false;

// **La VCOM d'aquest vidre**, in millivolts: **1560**, which is the value every example of the board sets (`epd_set_vcom(1560)`)
// and the one the board ships with (docs/50, docs/95).
const uint16_t kVcomMillivolts = 1560;

// **La temperatura que epdiy demana per triar la forma d'ona**: the board has no sensor of its own —`epd_board_v7` answers its own
// twenty degrees— o sigui que el número surt del propi panell i no s'inventa aquí (docs/50).
int the_temperature() { return static_cast<int>(epd_ambient_temperature()); }

// **Les quatre qualitats del nucli són els seus modes** (docs/50): the flashing one for a page that has to be clean, the
// non-flashing one for what is already on the glass, and the two-level one for the quick updates. `du4` and `a2` no existeixen a
// la forma d'ona d'aquest panell i cauen al `du`, que és el que epdiy té (docs/50, docs/95).
enum EpdDrawMode mode_of(UpdateMode mode) {
  switch (mode) {
    case UpdateMode::kDu:
    case UpdateMode::kDu4:
    case UpdateMode::kA2:
      return MODE_DU;
    case UpdateMode::kGl16:
    case UpdateMode::kGlr16:
    case UpdateMode::kGld16:
      return MODE_GL16;
    case UpdateMode::kInit:
    case UpdateMode::kGc16:
    default:
      return MODE_GC16;
  }
}

}  // namespace

Epd47::Epd47(Pca9535* expander, II2c* bus) : expander_(expander), bus_(bus) {}

uint16_t Epd47::width() const { return kPageW; }
uint16_t Epd47::height() const { return kPageH; }

bool Epd47::begin() {
  if (begun_) return true;
  // **L'arrencada és la del seu firmware de fàbrica** (0.190.59): the board, the panel, the 64k lookup table, the VCOM of this
  // glass and the built-in waveform. Epdiy owns the I2C, the PCA9535 and the TPS65185 from here on (docs/50).
  epd_init(&epd_board_v7, &ED047TC1, EPD_LUT_64K);
  epd_set_vcom(vcom_mv_);
  // **I la forma d'ona és la del fabricant d'aquest vidre, no la genèrica** (0.190.78). Two files exist in the board's own repository
  // and they are not the same thing:
  //
  //   - `waveforms/epdiy_ED047TC1.h`, 37 KB: the generic table, **one** temperature range (20-30 °C) and five modes. It is what
  //     `ED047TC1` selects by default, and it is what we have been painting with.
  //   - `waveforms/epdiy_ED047TC2.h`, 266 KB of text: **byte for byte** the file LilyGO ships in this board's older branch as
  //     `lib/LilyGo-EPD47-esp32s3/Waveform_header/ED047TC2.h` (48 192 bytes of table, compared value by value), with **seven**
  //     temperature ranges from 0 to 48 °C. Epdiy's own `builtin_waveforms.c` says where it comes from: *«Alternative Waveform added
  //     by Lilygo on Oct 2021»*.
  //
  // O sigui que **la forma d'ona calibrada per a aquest panell és la TC2**, i epdiy ja la porta compilada: l'única cosa que faltava
  // era triar-la. És el que fa aquesta línia, i és el que els seus exemples acaben fent servir quan el panell es declara com el que
  // és (docs/50, docs/95).
  g_state = epd_hl_init(&epdiy_ED047TC2);
  frame_ = epd_hl_get_framebuffer(&g_state);
  if (frame_ == nullptr) {
    error_ = "epdiy has no framebuffer for this panel";
    return false;
  }
  g_ready = true;
  begun_ = true;
  // **I el corrent no es queda obert** (0.190.60): their own firmware powers the supply **around every update** —`epd_poweron()`
  // … `epd_hl_update_screen()` … `epd_poweroff()`— i les tensions enceses tot el dia són una tinta que es mou quan no toca
  // (docs/50, docs/95).
  logf(pal::kLogInfo, "PANEL", "epdiy %s, %ux%u of glass, the waveform of the maker (TC2, seven temperatures)", "2.0.0",
       static_cast<unsigned>(epd_width()), static_cast<unsigned>(epd_height()));
  return true;
}

void Epd47::to_glass(uint16_t px, uint16_t py, uint16_t& gx, uint16_t& gy) const {
  if (rotation_ == 90) {
    gx = static_cast<uint16_t>(kPageH - 1 - py);
    gy = px;
    return;
  }
  gx = py;
  gy = static_cast<uint16_t>(kPageW - 1 - px);
}

void Epd47::put(uint16_t gx, uint16_t gy, uint8_t level) {
  if (frame_ == nullptr || gx >= kGlassW || gy >= kGlassH) return;
  uint8_t* at = frame_ + static_cast<uint32_t>(gy) * kGlassStride + (gx >> 1);
  const uint8_t nibble = static_cast<uint8_t>(level & 0x0F);
  if ((gx & 1u) != 0) {
    *at = static_cast<uint8_t>((*at & 0xF0) | nibble);
  } else {
    *at = static_cast<uint8_t>((*at & 0x0F) | (nibble << 4));
  }
}

uint8_t Epd47::get(uint16_t gx, uint16_t gy) const {
  if (frame_ == nullptr || gx >= kGlassW || gy >= kGlassH) return 15;
  const uint8_t at = frame_[static_cast<uint32_t>(gy) * kGlassStride + (gx >> 1)];
  return ((gx & 1u) != 0) ? static_cast<uint8_t>(at & 0x0F) : static_cast<uint8_t>((at >> 4) & 0x0F);
}

void Epd47::mark(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  if (w == 0 || h == 0 || x >= kPageW || y >= kPageH) return;
  const uint16_t x1 = static_cast<uint16_t>(x + w - 1 >= kPageW ? kPageW - 1 : x + w - 1);
  const uint16_t y1 = static_cast<uint16_t>(y + h - 1 >= kPageH ? kPageH - 1 : y + h - 1);
  if (x < dirty_x0_) dirty_x0_ = x;
  if (y < dirty_y0_) dirty_y0_ = y;
  if (x1 > dirty_x1_) dirty_x1_ = x1;
  if (y1 > dirty_y1_) dirty_y1_ = y1;
}

void Epd47::forget_the_dirty() {
  dirty_x0_ = 0xFFFF;
  dirty_y0_ = 0xFFFF;
  dirty_x1_ = 0;
  dirty_y1_ = 0;
}

void Epd47::dirty_glass(uint16_t& gx0, uint16_t& gy0, uint16_t& gx1, uint16_t& gy1) const {
  gx0 = 0;
  gy0 = 0;
  gx1 = kGlassW - 1;
  gy1 = kGlassH - 1;
  if (nothing_dirty()) return;
  // **Les quatre cantonades**, perquè un gir de noranta graus inverteix un dels dos eixos i el mínim de les files no és el
  // mínim de les columnes (docs/50).
  gx0 = kGlassW - 1;
  gy0 = kGlassH - 1;
  gx1 = 0;
  gy1 = 0;
  const uint16_t xs[2] = {dirty_x0_, dirty_x1_};
  const uint16_t ys[2] = {dirty_y0_, dirty_y1_};
  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < 2; ++j) {
      uint16_t gx = 0;
      uint16_t gy = 0;
      to_glass(xs[i], ys[j], gx, gy);
      if (gx < gx0) gx0 = gx;
      if (gy < gy0) gy0 = gy;
      if (gx > gx1) gx1 = gx;
      if (gy > gy1) gy1 = gy;
    }
  }
}

void Epd47::clear(uint8_t level) {
  if (frame_ == nullptr) return;
  const uint8_t nibble = static_cast<uint8_t>(level & 0x0F);
  memset(frame_, static_cast<int>((nibble << 4) | nibble), static_cast<size_t>(kGlassW) * kGlassStride);
  mark(0, 0, kPageW, kPageH);
}

void Epd47::fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t level) {
  mark(x, y, w, h);
  for (uint16_t j = 0; j < h; ++j) {
    const uint16_t py = static_cast<uint16_t>(y + j);
    if (py >= kPageH) return;
    for (uint16_t i = 0; i < w; ++i) {
      const uint16_t px = static_cast<uint16_t>(x + i);
      if (px >= kPageW) break;
      uint16_t gx = 0;
      uint16_t gy = 0;
      to_glass(px, py, gx, gy);
      put(gx, gy, level);
    }
  }
}

void Epd47::pixel(uint16_t x, uint16_t y, uint8_t level) {
  if (x >= kPageW || y >= kPageH) return;
  mark(x, y, 1, 1);
  uint16_t gx = 0;
  uint16_t gy = 0;
  to_glass(x, y, gx, gy);
  put(gx, gy, level);
}

void Epd47::blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t* packed, uint16_t stride) {
  if (packed == nullptr) return;
  mark(x, y, w, h);
  for (uint16_t j = 0; j < h; ++j) {
    const uint16_t py = static_cast<uint16_t>(y + j);
    if (py >= kPageH) return;
    const uint8_t* line = packed + static_cast<uint32_t>(j) * stride;
    for (uint16_t i = 0; i < w; ++i) {
      const uint16_t px = static_cast<uint16_t>(x + i);
      if (px >= kPageW) break;
      // The left pixel of a byte is the high nibble (docs/50): the source is indexed by its own column and not by the
      // column it lands on, which is what makes an odd `x` read the right pixel.
      const uint8_t pair = line[i >> 1];
      const uint8_t level = ((i & 1u) != 0) ? static_cast<uint8_t>(pair & 0x0F) : static_cast<uint8_t>((pair >> 4) & 0x0F);
      uint16_t gx = 0;
      uint16_t gy = 0;
      to_glass(px, py, gx, gy);
      put(gx, gy, level);
    }
  }
}

bool Epd47::set_rotation(uint16_t degrees) {
  // **Only the two that fit**: the glass is 960 × 540 and the page is 540 × 960, o sigui que girar-la noranta o dues-
  // centes setanta és el que la fa cabre; els altres dos demanarien una pàgina de la mateixa forma que el vidre.
  if (degrees != 90 && degrees != 270) {
    error_ = "the page only fits on this glass at 90 or 270 degrees";
    return false;
  }
  rotation_ = degrees;
  return true;
}

bool Epd47::flush(UpdateMode mode) {
  if (!begin()) return false;
  // **Les tensions, a cada pintada** (0.190.60), com el seu `screen_update()`: they come up, the page is written and they go down
  // again, o sigui que el vidre no es queda amb les vies obertes entre dues hores (docs/50).
  if (!powered_) {
    epd_poweron();
    powered_ = true;
  }
  const enum EpdDrawMode draw = mode_of(mode);
  // **I aquí NO hi ha cap rentat previ** (0.190.82, i la 0.190.60 el va posar malament). Their `grayscale_test` calls
  // `epd_clear()` before `MODE_GC16`, i el que vaig fer va ser copiar-ho sense mirar què fa: **`epd_clear()` neteja el vidre
  // SENCER**, i després el que es tornava a pintar era només la finestra marcada — o sigui que cada pintada que neteja es pagava
  // com una pantalla sencera (vint-i-cinc passades de tot el vidre) i deixava blanc tot el que quedava fora de la finestra. Era
  // exactament el que l'amo va veure: *«el redibuixat és un desastre, tota la pantalla sencera, lentíssim»*.
  //
  // **I no cal**: la forma d'ona d'aquest panell per al `MODE_GC16` és una matriu de transició **16 × 16** —cada nivell té codi
  // per a cada nivell de sortida, comprovat als 38 fotogrames de `epd_wp_ED047TC2_2_9_data`—, o sigui que **sap portar un píxel de
  // qualsevol gris a qualsevol altre** i no compta des de paper blanc. El rentat només fa falta amb els modes que sí que compten des
  // de blanc (`MODE_EPDIY_WHITE_TO_GL16`), que aquest sistema no demana (docs/50, docs/95).
  // **El rectangle que ha canviat és el que es pinta** (0.190.40): epdiy can update an area, o sigui que una hora que canvia no ha
  // de costar la pàgina; i si no ha canviat res no es pinta (docs/50, docs/95).
  const bool nothing = (dirty_x0_ > dirty_x1_ || dirty_y0_ > dirty_y1_);
  const uint32_t started = static_cast<uint32_t>(esp_timer_get_time());
  if (nothing) {
    const uint32_t took = static_cast<uint32_t>(esp_timer_get_time()) - started;
    logf(pal::kLogDebug, "PANEL", "%s: nothing changed, no paint (%u ms)", "epdiy", static_cast<unsigned>(took / 1000u));
    epd_poweroff();
    powered_ = false;
    return true;
  }
  uint16_t gx0 = 0;
  uint16_t gy0 = 0;
  uint16_t gx1 = 0;
  uint16_t gy1 = 0;
  dirty_glass(gx0, gy0, gx1, gy1);
  EpdRect area = {};
  area.x = gx0;
  area.y = gy0;
  area.width = static_cast<int>(gx1 - gx0 + 1);
  area.height = static_cast<int>(gy1 - gy0 + 1);
  const enum EpdDrawError err = epd_hl_update_area(&g_state, static_cast<enum EpdDrawMode>(draw), the_temperature(), area);
  const uint32_t took_ms = (static_cast<uint32_t>(esp_timer_get_time()) - started) / 1000u;
  // **I el que ha costat, que ara és el seu número i no el meu** (docs/95): the mode, the area and the milliseconds, which is what
  // the log has always said about a paint (docs/50).
  logf(pal::kLogDebug, "PANEL", "%s: %d,%d %dx%d of the glass, mode %d, %u ms, %s", "epdiy", static_cast<int>(area.x),
       static_cast<int>(area.y), area.width, area.height, static_cast<int>(draw), static_cast<unsigned>(took_ms),
       err == EPD_DRAW_SUCCESS ? "ok" : "with an error");
  forget_the_dirty();
  // **I les tensions, avall** (0.190.60): the image stays because it is ink, and the rails do not (docs/50).
  epd_poweroff();
  powered_ = false;
  if (err != EPD_DRAW_SUCCESS) {
    error_ = "epdiy did not finish that paint";
    return false;
  }
  return true;
}

bool Epd47::set_vcom_mv(uint16_t mv) {
  // **Els límits són els del xip i no els d'aquest fitxer**: el TPS65185 dona de zero a cinc mil cent milivolts en passos de deu, i
  // res fora d'això no és una VCOM sinó un registre inventat (docs/50).
  if (mv == 0 || mv > 5100) {
    error_ = "that is not a VCOM this supply can make";
    return false;
  }
  vcom_mv_ = mv;
  if (!begun_) return true;
  // **I s'ha de tornar a encendre perquè l'agafi**: the value is written to the supply when the rails come up, o sigui que el que es
  // fa és baixar-les, dir-ho i tornar-les a pujar; la imatge no es mou perquè és tinta (docs/50, docs/95).
  if (powered_) {
    epd_poweroff();
    powered_ = false;
  }
  epd_set_vcom(mv);
  epd_poweron();
  powered_ = true;
  logf(pal::kLogInfo, "PANEL", "the supply now holds VCOM at %u mV", static_cast<unsigned>(mv));
  return true;
}

bool Epd47::rest() {
  if (!begun_ || !powered_) return false;
  epd_poweroff();
  powered_ = false;
  return true;
}

ISurface* t5s3_epd47(Pca9535* expander, II2c* bus) {
  static Epd47 glass(expander, bus);
  return &glass;
}

}  // namespace hal
