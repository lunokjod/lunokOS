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

#include <esp_timer.h>
#include <string.h>

#include <cstring>

#include "epd_highlevel.h"
#include "epdiy.h"

// **Les dues funcions del rellotge de la llibreria** (0.190.84): `lcd_driver.c` és C i la seva capçalera no té `extern "C"`, o sigui
// que des d'aquí s'han de declarar amb l'enllaç del C o el nom no coincideix i no enllaça (docs/50, docs/95).
extern "C" {
int epd_lcd_pixel_clock_MHz();
void epd_lcd_set_pixel_clock_MHz(int frequency);
}
#include "hal/log.h"

namespace hal {
namespace {

// **El pont entre el nucli i epdiy** (0.190.59): epdiy keeps one state per panel and the whole framebuffer of the page lives in it,
// o sigui que aquest driver no té cap quadre propi i el que dibuixa el nucli és el seu (docs/50).
EpdiyHighlevelState g_state;
bool g_ready = false;

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

// **La pàgina segueix el gir** (0.190.281): the glass is 960 × 540 and the page is the glass frame turned, o sigui que un quart de
// volta dona la pàgina de 540 × 960 que aquesta casa ha fet servir sempre i **cap volta dona el vidre mateix**, 960 × 540. És el
// que demana el teclat de `btkbd` per tenir les tecles del doble d'amples, i fins avui el driver ho refusava dient que «la pàgina
// només hi cap a 90 o 270», que era cert de la pàgina que hi havia i no del maquinari (docs/50).
uint16_t Epd47::width() const { return (rotation_ == 90 || rotation_ == 270) ? kPageW : kGlassW; }
uint16_t Epd47::height() const { return (rotation_ == 90 || rotation_ == 270) ? kPageH : kGlassH; }

bool Epd47::begin() {
  if (begun_) return true;
  // **L'arrencada és la del seu firmware de fàbrica** (0.190.59): the board, the panel, the 64k lookup table, the VCOM of this
  // glass and the built-in waveform. Epdiy owns the I2C, the PCA9535 and the TPS65185 from here on (docs/50).
  epd_init(&epd_board_v7, &ED047TC1, EPD_LUT_64K);
  epd_set_vcom(vcom_mv_);
  // **La forma d'ona amb què arrenca el build és la TC2** (0.190.286, l'amo: *«deixa la C2 al final»*, després de la 0.190.284 que
  // va posar la C1 per provar-la). Les dues que aquest panell té compilades no són el mateix:
  //
  //   - `waveforms/epdiy_ED047TC1.h`, 37 KB: la genèrica, **un** rang de temperatura (20-30 °C) i **cinc** modes —els tres de la C2
  //     més `WHITE_TO_GL16` i `BLACK_TO_GL16`. És la que els dos exemples del fabricant fan servir de debò (`ED047TC1` amb
  //     `EPD_BUILTIN_WAVEFORM`) i també la que `ED047TC1` tria per defecte.
  //   - `waveforms/epdiy_ED047TC2.h`, 266 KB de text: **byte for byte** el fitxer que LilyGO porta a la branca vella d'aquesta placa
  //     com a `lib/LilyGo-EPD47-esp32s3/Waveform_header/ED047TC2.h` (48 192 bytes de taula, comparats valor a valor), amb **set**
  //     rangs de 0 a 48 °C i tres modes. `builtin_waveforms.c` d'epdiy diu d'on surt: *«Alternative Waveform added by Lilygo on Oct
  //     2021»*.
  //
  // O sigui que **la taula calibrada per a aquest panell és la C2** i és la que es queda. La C1 no marxa: és un punter de l'estat
  // d'epdiy i `panel wave tc1` la posa en calent, sense reiniciar (docs/50, docs/95).
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
  // **I el rellotge del bus, a vint-i-quatre i no a vint** (0.190.139, l'amo: *«panel bench peta la máquina...»* i, després de tornar-ho a
  // mesurar, els números de 446 ms que eren els de vint megahertz). El panell aguanta fins a vint-i-sis —el fabricant va a 26,6 al seu exemple—
  // i la placa ha pintat a vint-i-quatre sense cap línia partida: `panel bus 24` era una comanda que no es desa, o sigui que cada flaish tornava
  // a vint i ningú no ho sabia fins que els mil·lisegons no quadraven. El que és una tria d'una persona es queda a `panel bus`; el que és un
  // paràmetre del driver, aquí (docs/50, docs/95).
  const uint32_t kGlassMhz = 24;
  if (epd_lcd_pixel_clock_MHz() != static_cast<int>(kGlassMhz)) epd_lcd_set_pixel_clock_MHz(static_cast<int>(kGlassMhz));
  logf(pal::kLogInfo, "PANEL", "epdiy %s, %ux%u of glass at %u MHz, the waveform is the %s", "2.0.0",
       static_cast<unsigned>(epd_width()), static_cast<unsigned>(epd_height()),
       static_cast<unsigned>(epd_lcd_pixel_clock_MHz()), waveform_);
  return true;
}

// **Els quatre girs, i cadascun és el mapa de la pàgina al marc del xip** (0.190.281): amb la pàgina de 540 × 960 els dos talls
// creuen els eixos; amb la pàgina apaïsada de 960 × 540 el gir zero és la identitat i el de cent vuitanta és la volta sencera —
// els dos costats menys un— (docs/50).
void Epd47::to_glass(uint16_t px, uint16_t py, uint16_t& gx, uint16_t& gy) const {
  switch (rotation_) {
    case 90:
      gx = static_cast<uint16_t>(kPageH - 1 - py);
      gy = px;
      return;
    case 180:
      gx = static_cast<uint16_t>(kGlassW - 1 - px);
      gy = static_cast<uint16_t>(kGlassH - 1 - py);
      return;
    case 0:
      gx = px;
      gy = py;
      return;
    default:  // 270, the turn the system has always used
      gx = py;
      gy = static_cast<uint16_t>(kPageW - 1 - px);
      return;
  }
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

bool Epd47::read_row(uint16_t y, uint8_t* packed, uint16_t stride) const {
  if (packed == nullptr || frame_ == nullptr) return false;
  const uint16_t page_w = width();
  const uint16_t page_h = height();
  if (y >= page_h || page_w == 0) return false;
  const uint16_t line = static_cast<uint16_t>((page_w + 1) / 2);
  if (stride < line) return false;

  for (uint16_t x = 0; x < page_w; ++x) {
    uint16_t gx = 0;
    uint16_t gy = 0;
    to_glass(x, y, gx, gy);
    const uint8_t level = get(gx, gy);
    if ((x & 1u) == 0) {
      packed[x >> 1] = static_cast<uint8_t>(level << 4);
    } else {
      packed[x >> 1] = static_cast<uint8_t>(packed[x >> 1] | level);
    }
  }
  // **La mitja parella d'una amplada senar és paper**: the absent pixel behind the last column is not somebody else's nibble.
  if ((page_w & 1u) != 0) packed[line - 1] = static_cast<uint8_t>(packed[line - 1] | 0x0F);

  return true;
}

uint8_t Epd47::get(uint16_t gx, uint16_t gy) const {
  if (frame_ == nullptr || gx >= kGlassW || gy >= kGlassH) return 15;
  const uint8_t at = frame_[static_cast<uint32_t>(gy) * kGlassStride + (gx >> 1)];
  return ((gx & 1u) != 0) ? static_cast<uint8_t>(at & 0x0F) : static_cast<uint8_t>((at >> 4) & 0x0F);
}

void Epd47::mark(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  const uint16_t page_w = width();
  const uint16_t page_h = height();
  if (w == 0 || h == 0 || x >= page_w || y >= page_h) return;
  const uint16_t x1 = static_cast<uint16_t>(x + w - 1 >= page_w ? page_w - 1 : x + w - 1);
  const uint16_t y1 = static_cast<uint16_t>(y + h - 1 >= page_h ? page_h - 1 : y + h - 1);
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
  mark(0, 0, width(), height());
}

void Epd47::fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t level) {
  mark(x, y, w, h);
  for (uint16_t j = 0; j < h; ++j) {
    const uint16_t py = static_cast<uint16_t>(y + j);
    if (py >= height()) return;
    for (uint16_t i = 0; i < w; ++i) {
      const uint16_t px = static_cast<uint16_t>(x + i);
      if (px >= width()) break;
      uint16_t gx = 0;
      uint16_t gy = 0;
      to_glass(px, py, gx, gy);
      put(gx, gy, level);
    }
  }
}

void Epd47::pixel(uint16_t x, uint16_t y, uint8_t level) {
  if (x >= width() || y >= height()) return;
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
    if (py >= height()) return;
    const uint8_t* line = packed + static_cast<uint32_t>(j) * stride;
    for (uint16_t i = 0; i < w; ++i) {
      const uint16_t px = static_cast<uint16_t>(x + i);
      if (px >= width()) break;
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
  // **Els quatre, i el que decideix és la forma de la pàgina** (0.190.281): a quarter turn gives the portrait page of 540 × 960
  // and no turn at all gives the glass frame, 960 × 540; el que no és cap volta es refusa (docs/50).
  if (degrees != 0 && degrees != 90 && degrees != 180 && degrees != 270) {
    error_ = "a turn is 0, 90, 180 or 270";
    return false;
  }
  rotation_ = degrees;
  // **I la pàgina torna a ser el que cal pintar**: el que hi ha al framebuffer es va dibuixar en el marc de la pàgina d'abans, o
  // sigui que el vidre sencer es torna a enviar quan algú pinta (docs/50, docs/95).
  mark(0, 0, width(), height());
  return true;
}

// **Dos segons de tensions enceses després de l'última pintada** (0.190.140): el temps que una persona triga a canviar de línia o a fer un
// scroll curt, que és quan el TPS ja calent estalvia els cent mil·lisegons del `POWER_GOOD`.
const uint32_t kKeepWarmMs = 2000;

bool Epd47::wash_the_glass() {
  if (!begin()) return false;
  if (!powered_) {
    epd_poweron();
    powered_ = true;
  }
  // **La pàgina i la idea del vidre, totes dues a paper** (0.192.11): the caller's buffer is left white —that is what the glass is about to
  // be— and epdiy's own idea of the panel too, so the first page that comes does not carry a diff against a picture that is no longer there
  // (docs/50).
  memset(frame_, 0xFF, static_cast<size_t>(kGlassW) * kGlassStride);
  epd_hl_set_all_white(&g_state);
  const uint32_t started = static_cast<uint32_t>(esp_timer_get_time());
  // **I el vidre, amb el cicle de la seva forma d'ona**: `epd_clear()` pushes the whole screen through three rounds of dark and white —the
  // panel's own timing, no LUT, no diff—, which is what leaves paper on a glass that was showing something this driver never sent
  // (docs/50, docs/95).
  epd_clear();
  const uint32_t took_ms = (static_cast<uint32_t>(esp_timer_get_time()) - started) / 1000u;
  // Nothing is owed to the page: the glass is paper and the buffer says paper (docs/30).
  forget_the_dirty();
  epd_poweroff();
  powered_ = false;
  logf(pal::kLogInfo, "PANEL", "%s: the whole glass cleared by its own cycle, %u ms, paper", "epdiy",
       static_cast<unsigned>(took_ms));
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
  // **Les tensions es queden, i això és el que estalvia cent mil·lisegons per pintada** (0.190.140, l'amo: *«sí, deixa-les enceses 2 s després de
  // l'última pintada»*). El fabricant les puja i les baixa a cada update, o sigui que cada pintada paga el `POWER_GOOD` i la VCOM senceres; en una
  // UI que pinta deu vegades seguides això era un segon de tensions i no de tinta. Les baixa `poll` quan el vidre fa dos segons que calla, i
  // `rest` abans de dormir (docs/50, docs/95).
  last_paint_ms_ = static_cast<uint32_t>(esp_timer_get_time() / 1000);
  if (err != EPD_DRAW_SUCCESS) {
    error_ = "epdiy did not finish that paint";
    return false;
  }
  return true;
}

// **Una àrea girada del dret i del revés, que és el seu propi desfer** (0.190.124): the two nibbles of a byte are two pixels and flipping them both is
// the same as flipping the picture, o sigui que invertir-la dues vegades deixa el framebuffer exactament com era. L'àrea s'arrodoneix a bytes, que vol
// dir que com a molt s'inverteix un píxel de més a cada punta —dins del vidre i tornat a girar tot seguit— (docs/50).
void Epd47::flip_the_area(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  if (frame_ == nullptr || w == 0 || h == 0) return;
  uint32_t left = x & ~1u;
  uint32_t right = (static_cast<uint32_t>(x) + w + 1u) & ~1u;
  if (right > kGlassW) right = kGlassW;
  if (left >= right) return;
  for (uint32_t row = y; row < static_cast<uint32_t>(y) + h && row < kGlassH; ++row) {
    uint8_t* at = frame_ + row * kGlassStride + (left >> 1);
    const uint32_t bytes = (right - left) >> 1;
    for (uint32_t i = 0; i < bytes; ++i) at[i] = static_cast<uint8_t>(~at[i]);
  }
}

// **Una pintada que només compta** (0.190.123): la mateixa via que `flush` —les tensions, la forma d'ona de la temperatura que el panell diu i la
// baixada de les tensions— però amb l'àrea que demana qui mesura. **I hi ha d'haver un canvi de debò** (0.190.124): the first version of this asked
// the panel to paint an area that was already painted, epdiy compared the two framebuffers, found nothing to do and answered **1 ms** —the owner's
// `panel bench` said a thousand pictures a second for a strip of forty-eight rows—, i el que això mesura no era cap pintada. Ara l'àrea es gira, es
// pinta i es torna a girar a la crida següent: cada crida té el seu canvi i la imatge del vidre acaba on va començar. Zero és un panell que no hi és
// o una àrea que no cap (docs/50, docs/95).
uint32_t Epd47::paint_for_the_count(uint16_t x, uint16_t y, uint16_t w, uint16_t h, UpdateMode mode) {
  if (!begin()) return 0;
  if (w == 0 || h == 0) return 0;
  if (static_cast<uint32_t>(x) + w > kGlassW || static_cast<uint32_t>(y) + h > kGlassH) return 0;
  flip_the_area(x, y, w, h);
  if (!powered_) {
    epd_poweron();
    powered_ = true;
  }
  EpdRect area = {};
  area.x = static_cast<int>(x);
  area.y = static_cast<int>(y);
  area.width = static_cast<int>(w);
  area.height = static_cast<int>(h);
  const uint32_t started = static_cast<uint32_t>(esp_timer_get_time());
  (void)epd_hl_update_area(&g_state, static_cast<enum EpdDrawMode>(mode_of(mode)), the_temperature(), area);
  const uint32_t took = static_cast<uint32_t>(esp_timer_get_time()) - started;
  epd_poweroff();
  powered_ = false;
  return took / 1000u;
}

// **Les vuit taules d'epdiy, totes al binari** (0.190.135, l'amo: *«podries afegir tots els waveforms? els vull provar»*). `builtin_waveforms.c`
// inclou cada taula que la llibreria porta i aquí només cal el nom per triar-ne una: el que decideix quina es fa servir és un punter de l'estat
// (`state->waveform`), o sigui que **es canvien en calent** —sense reiniciar i sense tocar el que hi ha pintat— i la pintada següent consulta la
// nova. Nomes dues són d'aquest panell (les ED047, de 4,7"); les altres sis són de vidres de sis, nou i tretze polzades i **no són per a aquesta
// tinta**: es proven perquè el que es busca és entendre els grisos, i el que faran és pintar amb la forma d'ona d'un altre panell (docs/50, docs/95).
extern "C" {
extern const EpdWaveform epdiy_ED047TC1;
extern const EpdWaveform epdiy_ED047TC2;
extern const EpdWaveform epdiy_ED060SC4;
extern const EpdWaveform epdiy_ED060SCT;
extern const EpdWaveform epdiy_ED060XC3;
extern const EpdWaveform epdiy_ED097OC4;
extern const EpdWaveform epdiy_ED097TC2;
extern const EpdWaveform epdiy_ED133UT2;
}

namespace {

struct WaveformChoice {
  const char* name;
  const EpdWaveform* table;
};

// **Els noms del prompt, i el primer és l'àlies curt dels dos d'aquest panell**: `panel wave tc1` i `panel wave ed047tc1` són el mateix.
const WaveformChoice kWaveforms[] = {
    {"tc1", &epdiy_ED047TC1},
    {"tc2", &epdiy_ED047TC2},
    {"ed047tc1", &epdiy_ED047TC1},
    {"ed047tc2", &epdiy_ED047TC2},
    {"ed060sc4", &epdiy_ED060SC4},
    {"ed060sct", &epdiy_ED060SCT},
    {"ed060xc3", &epdiy_ED060XC3},
    {"ed097oc4", &epdiy_ED097OC4},
    {"ed097tc2", &epdiy_ED097TC2},
    {"ed133ut2", &epdiy_ED133UT2},
};
const uint8_t kWaveformCount = static_cast<uint8_t>(sizeof(kWaveforms) / sizeof(kWaveforms[0]));
// **Els noms que es poden demanar, sense repetir les dues àlies** (el llistat del prompt les ha de dir totes una vegada).
const char* const kListed[] = {"ed047tc1", "ed047tc2", "ed060sc4", "ed060sct", "ed060xc3", "ed097oc4", "ed097tc2", "ed133ut2"};
const uint8_t kListedCount = static_cast<uint8_t>(sizeof(kListed) / sizeof(kListed[0]));

}  // namespace

const char* Epd47::waveform_at(uint8_t index, uint8_t* modes, uint8_t* ranges) const {
  if (index >= kListedCount) return nullptr;
  const char* name = kListed[index];
  const EpdWaveform* table = nullptr;
  for (uint8_t i = 0; i < kWaveformCount; ++i) {
    if (strcmp(name, kWaveforms[i].name) == 0) {
      table = kWaveforms[i].table;
      break;
    }
  }
  if (table != nullptr) {
    if (modes != nullptr) *modes = static_cast<uint8_t>(table->num_modes);
    if (ranges != nullptr) *ranges = static_cast<uint8_t>(table->num_temp_ranges);
  }
  return name;
}

bool Epd47::set_waveform(const char* name) {
  if (name == nullptr) return false;
  for (uint8_t i = 0; i < kWaveformCount; ++i) {
    if (strcmp(name, kWaveforms[i].name) != 0) continue;
    g_state.waveform = kWaveforms[i].table;
    waveform_ = (strncmp(name, "tc", 2) == 0) ? ((name[2] == '1') ? "ed047tc1" : "ed047tc2") : name;
    logf(pal::kLogInfo, "PANEL", "the waveform is the %s (%d modes, %d temperature range(s))", waveform_,
         g_state.waveform->num_modes, g_state.waveform->num_temp_ranges);
    return true;
  }
  error_ = "this build carries the eight waveforms of epdiy and that is not one of them";
  return false;
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
  // fa és baixar-les, escriure-hi i tornar-les a pujar; la imatge no es mou perquè és tinta (docs/50, docs/95).
  //
  // **I es deixen com estaven** (0.190.285): un vidre que dormia no s'ha de quedar amb el corrent obert només per haver-li dit una
  // VCOM nova —el que xiuxiueja a la tinta tot el dia és precisament el que `begin()` promet no fer—, o sigui que el que es recorda
  // és si eren amunt i, si no hi eren, es tornen a baixar (docs/50, docs/95).
  const bool was_powered = powered_;
  if (powered_) {
    epd_poweroff();
    powered_ = false;
  }
  epd_set_vcom(mv);
  epd_poweron();
  powered_ = true;
  if (!was_powered) {
    epd_poweroff();
    powered_ = false;
  }
  logf(pal::kLogInfo, "PANEL", "the supply now holds VCOM at %u mV, and the rails are %s", static_cast<unsigned>(mv),
       powered_ ? "up" : "down");
  return true;
}

uint32_t Epd47::bus_mhz() const {
  // **El número de debò i no el que es va demanar**: epdiy baixa el rellotge sol quan la línia de la memòria cau no li agrada, o sigui
  // que el que compta és el que el perifèric té posat (docs/50, docs/95).
  return begun_ ? static_cast<uint32_t>(epd_lcd_pixel_clock_MHz()) : 0u;
}

bool Epd47::set_bus_mhz(uint32_t mhz) {
  // **Els límits són del panell**: vint megahertz és el que la seva taula dona i el que el seu firmware fa servir, i res per sota de dos
  // no paga el viatge; entremig, el que la placa aguanti és el que es veu (docs/50).
  if (mhz < 2 || mhz > 26) {
    error_ = "the bus of this panel does not take that clock";
    return false;
  }
  if (!begun_) {
    error_ = "the glass is not up yet";
    return false;
  }
  epd_lcd_set_pixel_clock_MHz(static_cast<int>(mhz));
  logf(pal::kLogInfo, "PANEL", "the pixels travel at %u MHz (asked for %u)", static_cast<unsigned>(bus_mhz()),
       static_cast<unsigned>(mhz));
  return true;
}

// **El torn del panell** (0.190.140): si les tensions són amunt i fa dos segons que ningú no pinta, avall. El número és el d'una persona que
// llegeix una pàgina i canvia de línia: prou llarg perquè les pintades d'un scroll es trobin el TPS ja calent i prou curt perquè el repòs no
// pagui el corrent d'una font encesa (docs/50, docs/60, docs/95).
void Epd47::poll() {
  if (!powered_ || last_paint_ms_ == 0) return;
  const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000);
  if ((now - last_paint_ms_) < kKeepWarmMs) return;
  epd_poweroff();
  powered_ = false;
  last_paint_ms_ = 0;
  // **I es diu**, perquè aquesta línia és la que separa una placa en repòs d'una que gasta una font encesa que ningú no ha demanat (docs/60).
  logf(pal::kLogDebug, "PANEL", "the supply is down again: %u ms without a paint", static_cast<unsigned>(kKeepWarmMs));
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
