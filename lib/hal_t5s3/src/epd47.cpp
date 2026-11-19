// The ED047TC1 of the T5 E-Paper S3 Pro, driven by the chip (docs/50).
//
// **El protocol és el de la placa i no una invenció**: the supply sequence of the TPS65185 and the four edges of a row
// are the ones the vendor's own firmware uses —its factory example is epdiy's `epd_board_v7`, whose pins are exactly
// this board's— and the shapes of the LUT come from FastEPD, which publishes them under Apache-2.0 (see
// `t5s3/epd47_waveform.h` for the attribution of the table itself).
#include "t5s3/epd47.h"

#include <Arduino.h>

#include <esp32/clk.h>
#include <esp_cpu.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_io_interface.h>
#include <esp_lcd_types.h>

#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>

#include "hal/log.h"
#include "t5s3/epd47_waveform.h"

namespace hal {
namespace {

// **Els catorze fils del panell** (docs/pinmap.md del repositori de la placa): the eight data lines in the order the
// nibble travels —D0 first—, the clock, and the four that say where a row begins and ends (docs/50).
const int kCkh = 4;   // the clock of the data (the vendor calls it EP_CKH, the i80 peripheral calls it WR)
const int kData[8] = {5, 6, 7, 15, 16, 17, 18, 8};
const int kStv = 45;  // starts a frame, once per phase
const int kCkv = 48;  // the vertical clock
const int kSth = 41;  // **the horizontal start**, polsat a cada fila (vegeu el bus, avall)
const int kLe = 42;   // latches the row that has just been clocked
// **El pin de DC que el perifèric exigeix i el panell no té** (0.190.33, la trampa d'aquest xip): the i80 bus of the
// ESP32-S3 **refuses to be created without a `dc_gpio_num`** (`lcd_i80_bus_configure_gpio` checks `>= 0`), and this panel
// has no data/command line at all. It has to be a real pin, so it is **GPIO46**, the chip select of the LoRa module: the
// driver holds it at the data level —high— all the time, which is exactly the idle state of a chip select, and that
// module's rail is off and this system does not talk to it. **El dia que la LoRa es faci servir, això s'ha de moure**
// (docs/50).
const int kDummyDc = 46;
// **I el llum frontal, apagat** (docs/50): the EN of the LED boost of this board is a GPIO of its own and a panel of ink
// does not need it; a pin left floating is a light that may be on.
const int kFrontLight = 11;

// **El TPS65185, que és qui dona les tensions del panell** (docs/50): three registers and the good-power one, which is
// what says the panel can be painted at all.
const uint8_t kTpsAddress = 0x68;
const uint8_t kTpsEnable = 0x01;   // 0x3F turns every rail on
const uint8_t kTpsVcomLow = 0x03;
const uint8_t kTpsVcomHigh = 0x04;
const uint8_t kTpsPowerGood = 0x0F;  // 0xFA is the maker's own «all good»
// **La VCOM d'aquest panell**, in millivolts: **1560**, which is the value the board ships with —el firmware de fàbrica
// el desa a la seva NVS com a `eps_vcom` i el posa amb `epd_set_vcom()` a cada arrencada— i no els 1600 que el codi
// d'epdiy porta de sèrie per a la seva pròpia placa. És el número que decideix el contrast i la vida del panell, o sigui
// que no s'inventa: és el d'aquesta placa (docs/50, docs/95).
const uint16_t kVcomMillivolts = 1560;

// **El rellotge del bus ja no és una constant** (0.190.38): the vendor's panel table gives twenty megahertz for this
// panel and that is the default (`pclk_hz_`), but the number is worth measuring on the board, o sigui que es pot canviar
// en calent (`panel bus 26`) i el perifèric es torna a obrir amb el nou (docs/50, docs/80).

// **El fila de dades d'un passatge**: 960 pixels of two bits are 240 bytes, and the DMA buffer is one row.
const size_t kRowBytes = 960 / 4;
// **El temps de repòs entre dos passatges** és el de l'original (230 µs), i és el que dona a la tinta el temps de
// moure's abans del següent (docs/50).
const uint32_t kBetweenPhasesUs = 230;
// How long a row is allowed to take before the driver calls it a failure: a row is 240 bytes at twenty megahertz, o
// sigui que cent mil·lisegons és un panell que no contesta i no un que va lent (docs/50).
const uint32_t kRowTimeoutMs = 100;

bool s_dma_done = false;

bool on_color_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*) {
  s_dma_done = true;
  return false;
}

}  // namespace

Epd47::Epd47(Pca9535* expander, II2c* bus)
    : expander_(expander), bus_(bus), bus_handle_(nullptr), io_(nullptr), bits_(nullptr), row_(nullptr),
      row2_(nullptr), previous_(nullptr), started_(false), powered_(false), busy_(false) {}

uint16_t Epd47::width() const { return kPageW; }
uint16_t Epd47::height() const { return kPageH; }
uint32_t Epd47::bus_mhz() const { return pclk_hz_ / 1000000; }

bool Epd47::set_bus_mhz(uint32_t mhz) {
  // **Els límits són del xip i no del panell**: the i80 takes its clock from the 160 MHz PLL, o sigui que el que es pot
  // demanar és un divisor seu, i res per sota d'un megahertz ni per sobre del que el perifèric accepta. A speed that does
  // not divide is refused, because a wire that lies about its clock is a paint that fails for no reason (docs/50).
  if (mhz < 1 || mhz > 80) {
    error_ = "the bus of this panel does not take that clock";
    return false;
  }
  const uint32_t hz = mhz * 1000000u;
  if (hz == pclk_hz_) return true;
  if (busy_) {
    error_ = "the bus cannot change its clock in the middle of a paint";
    return false;
  }
  const uint32_t was = pclk_hz_;
  pclk_hz_ = hz;
  if (!started_) return true;
  // **I el perifèric es torna a obrir**: the clock is a property of the io, not of a transfer, o sigui que el que es fa
  // és tancar-lo i tornar-lo a crear amb el rellotge nou —el bus, el quadre i el panell es queden on són—. Si el xip no
  // dona aquest rellotge es torna al que hi havia, perquè un vidre que es queda sense io és un vidre que no pinta mai més
  // (docs/50).
  if (!open_the_bus()) {
    pclk_hz_ = was;
    if (open_the_bus()) return false;
    error_ = "the bus did not come back at the old clock";
    return false;
  }
  // **I es mesura, perquè un commutador que no commuta és pitjor que cap commutador** (0.190.44): a row is 240 bytes, o
  // sigui que cent noranta-dues mil nou-cents vint bits, i a M megahertz no pot costar menys de 1920/M microsegons de
  // cable. Si el número no ha baixat, el perifèric s'ha quedat amb el rellotge d'abans —que és exactament el que va fer la
  // 0.190.38— i el que es fa és tornar enrere i dir-ho, en lloc de deixar un vidre que no pinta (docs/50, docs/80).
  memset(row_, 0, kRowBytes);
  const uint32_t probe_started = static_cast<uint32_t>(esp_timer_get_time());
  if (!start_row(row_) || !wait_row()) {
    pclk_hz_ = was;
    (void)open_the_bus();
    error_ = "the row did not come back after the clock change";
    return false;
  }
  const uint32_t took_us = static_cast<uint32_t>(esp_timer_get_time()) - probe_started;
  const uint32_t wire_us = 1920u / mhz + 1u;
  logf(pal::kLogDebug, "PANEL", "asked for %u MHz: one row took %u us and the wire is %u", static_cast<unsigned>(mhz),
       static_cast<unsigned>(took_us), static_cast<unsigned>(wire_us));
  // **I no es refusa res pel número**: the i80 adds its own gaps at slow clocks —a row that should be 48 µs at 5 MHz was
  // measured at 174 on the board— o sigui que el número és per llegir-lo i no per decidir-hi; el que sí que no es pot
  // permetre és que el bus quedi mort, i això ja ho diu el fet que la fila hagi tornat (docs/50, docs/80).
  (void)wire_us;
  return true;
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
  if (bits_ == nullptr || gx >= kGlassW || gy >= kGlassH) return;
  uint8_t* at = bits_ + static_cast<uint32_t>(gy) * kGlassStride + (gx >> 1);
  const uint8_t nibble = static_cast<uint8_t>(level & 0x0F);
  if ((gx & 1u) != 0) {
    *at = static_cast<uint8_t>((*at & 0xF0) | nibble);
  } else {
    *at = static_cast<uint8_t>((*at & 0x0F) | (nibble << 4));
  }
}

uint8_t Epd47::get(uint16_t gx, uint16_t gy) const {
  if (bits_ == nullptr || gx >= kGlassW || gy >= kGlassH) return 15;
  const uint8_t at = bits_[static_cast<uint32_t>(gy) * kGlassStride + (gx >> 1)];
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
  if (bits_ == nullptr) return;
  const uint8_t nibble = static_cast<uint8_t>(level & 0x0F);
  memset(bits_, static_cast<int>((nibble << 4) | nibble), static_cast<size_t>(kGlassW) * kGlassStride);
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

bool Epd47::begin() {
  if (started_) return true;
  if (expander_ == nullptr || !expander_->configured()) {
    error_ = "the expander of the panel is not there";
    return false;
  }
  // **Els pins que el xip ha de moure ell mateix**: the clock, the frame start, the vertical clock, the latch and the
  // data lines. **I el `EP_STH`, que només es posa al seu repòs aquí**: the i80 takes it over as the strobe of a row a
  // few lines below, o sigui que aquest `1` és el nivell que el panell veu entre dues files (docs/50).
  gpio_config_t out = {};
  out.mode = GPIO_MODE_OUTPUT;
  out.intr_type = GPIO_INTR_DISABLE;
  out.pull_down_en = GPIO_PULLDOWN_DISABLE;
  out.pull_up_en = GPIO_PULLUP_DISABLE;
  for (size_t i = 0; i < 8; ++i) out.pin_bit_mask |= 1ULL << kData[i];
  out.pin_bit_mask |= 1ULL << kStv;
  out.pin_bit_mask |= 1ULL << kCkv;
  out.pin_bit_mask |= 1ULL << kSth;
  out.pin_bit_mask |= 1ULL << kLe;
  // **El rellotge no és un GPIO d'aquest fitxer**: the i80 peripheral owns CKH, o sigui que no entra a la màscara.
  // **I el llum frontal tampoc**: it is a pin of its own and it is turned off once, here, because an EN that floats is a
  // backlight nobody asked for (docs/50).
  out.pin_bit_mask |= 1ULL << kFrontLight;
  if (gpio_config(&out) != ESP_OK) {
    error_ = "the pins of the panel did not take";
    return false;
  }
  gpio_set_level(static_cast<gpio_num_t>(kSth), 1);
  gpio_set_level(static_cast<gpio_num_t>(kFrontLight), 0);

  if (!open_the_bus()) return false;

  // **El quadre, a la PSRAM** (docs/75): 259 200 bytes is twice the RAM of the chip and there is no version of this that
  // fits anywhere else.
  bits_ = static_cast<uint8_t*>(heap_caps_malloc(static_cast<size_t>(kGlassW) * kGlassStride, MALLOC_CAP_SPIRAM));
  row_ = static_cast<uint8_t*>(heap_caps_malloc(kRowBytes, MALLOC_CAP_DMA));
  // **Dues memòries de fila** (0.190.40): la que viatja i la que s'està construint, perquè el cable i la conversió es tapin.
  row2_ = static_cast<uint8_t*>(heap_caps_malloc(kRowBytes, MALLOC_CAP_DMA));
  // **I el quadre que ja és al vidre** (0.190.41): l'altra meitat d'una actualització parcial és saber què hi ha sota, i
  // això són els mateixos 259 200 B del quadre una altra vegada —a la PSRAM, que en té vuit mega— (docs/50, docs/75).
  previous_ = static_cast<uint8_t*>(heap_caps_malloc(static_cast<size_t>(kGlassW) * kGlassStride, MALLOC_CAP_SPIRAM));
  if (bits_ == nullptr || row_ == nullptr || row2_ == nullptr || previous_ == nullptr) {
    error_ = "there is no room for the frame of the panel";
    return false;
  }
  clear(15);
  started_ = true;
  return true;
}

// **El bus i el perifèric, de dues en dues** (0.190.44, i era un error greu de la 0.190.38): the clock of the i80 is
// hardware state of the **bus** and not of the io, o sigui que tancar i tornar a obrir només el io deixava el rellotge com
// estava —el log d'aquella versió deia `452 ms on the wire` a 5 MHz i a 20 MHz, el mateix número— i a més deixava el
// panell sense rebre cap fila: `panel bus` matava el vidre fins al reinici. What the prompt changes is rebuilt from the
// bottom: el bus i el io, amb els pins del perifèric inclosos (docs/50, docs/80).
bool Epd47::open_the_bus() {
  if (io_ != nullptr) {
    esp_lcd_panel_io_del(reinterpret_cast<esp_lcd_panel_io_handle_t>(io_));
    io_ = nullptr;
  }
  if (bus_handle_ != nullptr) {
    esp_lcd_del_i80_bus(reinterpret_cast<esp_lcd_i80_bus_handle_t>(bus_handle_));
    bus_handle_ = nullptr;
  }
  // **El bus paral·lel, amb el perifèric del xip** (docs/50): eight data lines and the clock, a data/command line that
  // does not exist —the panel has no registers— and **un `EP_STH` que sí que hi és** (vegeu avall).
  esp_lcd_i80_bus_config_t bus = {};
  bus.dc_gpio_num = kDummyDc;
  bus.wr_gpio_num = kCkh;
  bus.clk_src = LCD_CLK_SRC_PLL160M;
  bus.bus_width = 8;
  bus.max_transfer_bytes = 1024;
  for (size_t i = 0; i < 8; ++i) bus.data_gpio_nums[i] = kData[i];
  if (esp_lcd_new_i80_bus(&bus, reinterpret_cast<esp_lcd_i80_bus_handle_t*>(&bus_handle_)) != ESP_OK) {
    bus_handle_ = nullptr;
    error_ = "the parallel bus did not come up";
    return false;
  }
  return open_the_io();
}

// **El perifèric del bus** (0.190.33 → 0.190.38): it is a function of its own because the clock lives in its config, o
// sigui que `panel bus 26` al prompt ha de poder-lo tornar a obrir (docs/50, docs/80).
bool Epd47::open_the_io() {
  if (bus_handle_ == nullptr) {
    error_ = "the parallel bus did not come up";
    return false;
  }
  if (io_ != nullptr) {
    esp_lcd_panel_io_del(reinterpret_cast<esp_lcd_panel_io_handle_t>(io_));
    io_ = nullptr;
  }
  esp_lcd_panel_io_i80_config_t io = {};
  // **I `EP_STH` és el «chip select» d'aquest bus, i això és el que va deixar el vidre en blanc la segona vegada**
  // (0.190.36): the panel's *horizontal start* has to strobe **once per row** —low while the 240 bytes of a row go out
  // and high between them—, and the maker's own driver for this board hands GPIO41 to the i80 as exactly that
  // (`panel_config.cs_gpio_num = kCsGpio`, which is `PIN_EPD_STH`, with `cs_active_high = 0`), while epdiy wires it to
  // the peripheral's data-enable. Aquesta versió el tenia com una sortida quieta a u: el registre de desplaçament
  // horitzontal del panell no arrencava mai, o sigui que el bus treballava els seus 2,7 s i **no hi havia cap píxel que
  // rebés el codi** — ni un parpelleig, que és exactament el que es veia (docs/50, docs/95).
  io.cs_gpio_num = kSth;
  io.pclk_hz = pclk_hz_;
  io.trans_queue_depth = 2;
  // **Cap byte d'ordre** (0.190.33): `lcd_cmd_bits` at eight asks the driver for a command cycle this panel does not have,
  // and the transfer must be the row and nothing else. Zero bits means zero command cycles (docs/50).
  io.lcd_cmd_bits = 0;
  io.lcd_param_bits = 0;
  // Un DC que no commuta: alt sempre, i el pin que hi ha és el del mòdul de ràdio (vegeu `kDummyDc`).
  io.dc_levels.dc_idle_level = 1;
  io.dc_levels.dc_cmd_level = 1;
  io.dc_levels.dc_dummy_level = 1;
  io.dc_levels.dc_data_level = 1;
  // **I el pols actiu del `EP_STH` és baix** (0.190.36), que és el que el fabricant posa a `cs_active_high`.
  io.flags.cs_active_high = 0;
  io.flags.pclk_active_neg = 0;
  io.flags.pclk_idle_low = 0;
  if (esp_lcd_new_panel_io_i80(reinterpret_cast<esp_lcd_i80_bus_handle_t>(bus_handle_), &io,
                               reinterpret_cast<esp_lcd_panel_io_handle_t*>(&io_)) != ESP_OK) {
    io_ = nullptr;
    error_ = "the panel's io did not come up";
    return false;
  }
  esp_lcd_panel_io_callbacks_t callbacks = {};
  callbacks.on_color_trans_done = on_color_done;
  esp_lcd_panel_io_register_event_callbacks(reinterpret_cast<esp_lcd_panel_io_handle_t>(io_), &callbacks, nullptr);
  return true;
}

// **La seqüència de l'alimentació, en l'ordre del fabricant** (docs/50): the panel's output enable and its mode, the
// four gates of the TPS —wake, power-up, VCOM—, its own power-good pin, the rails and the VCOM, and then the good-power
// register which is the only thing that says the panel can be painted.
bool Epd47::power(bool on) {
  if (expander_ == nullptr) {
    error_ = "there is no expander";
    return false;
  }
  // **Una baixada sempre es fa** (0.190.34): a supply that failed halfway leaves its gates up, o sigui que el camí de
  // baixada no es pot saltar pel fet que `powered_` ja sigui fals (docs/50).
  if (on && powered_) return true;
  if (on) {
    // **El `EP_MODE` va ALT, i això és el que va deixar el vidre en blanc la primera llum** (0.190.35): the maker's own
    // firmware for this board sets it high when it powers the supply —`bbepPCA9535DigitalWrite(9, 1); // GMOD on` in
    // FastEPD's `EPDiyV7EinkPower`, which is the code the board ships with— and epdiy's `epd_board_v7` leaves it low
    // because that is a **different** board. Amb el GMOD baix el panell no pren els codis de dos bits de cada passada:
    // la font puja, el registre de bo diu `fa`, la fila surt pel bus i la tinta no es mou gens (docs/50, docs/95).
    expander_->write(Expander::kPanelMode, true);
    expander_->write(Expander::kPanelOe, true);
    expander_->write(Expander::kTpsWakeUp, true);
    expander_->write(Expander::kTpsPowerUp, true);
    expander_->write(Expander::kTpsVcomCtrl, true);
    gpio_set_level(static_cast<gpio_num_t>(kStv), 1);
    vTaskDelay(pdMS_TO_TICKS(2));
    if (bus_ == nullptr) {
      error_ = "there is no bus for the supply of the panel";
      gates_down();
      return false;
    }
    // **El `POWER_GOOD` del xip de la font és el primer que ha de dir que sí** (docs/50): the maker's driver waits on
    // this pin before it enables a rail, and a board that never sees it is a board whose supply is not wired as the
    // schematic says. It is waited for with a limit and said out loud, not for ever.
    bool good = false;
    // **Quatre-cents mil·lisegons, que és el que espera el fabricant** (its own `wait_panel_power_good(400)`): the
    // number is the board's and not this file's, i un `POWER_GOOD` que no arriba és una font que no s'ha despertat.
    for (int i = 0; i < 200; ++i) {
      if (expander_->read(Expander::kTpsPowerGood, good) && good) break;
      vTaskDelay(pdMS_TO_TICKS(2));
    }
    if (!good) {
      logf(pal::kLogWarn, "PANEL", "the power good of the supply never came up; the panel is asked anyway");
    }
    // **Les tres tensions i la VCOM** (docs/50): the enable register takes all of them at once, and the VCOM is written
    // in millivolts by tens —the maker's own encoding— because that is the number its driver uses.
    const uint8_t enable[2] = {kTpsEnable, 0x3F};
    if (!bus_->write(kTpsAddress, enable, sizeof(enable))) {
      error_ = "the supply did not take the enable";
      gates_down();
      return false;
    }
    const uint16_t vcom = static_cast<uint16_t>(kVcomMillivolts / 10);
    const uint8_t vcom_high[2] = {kTpsVcomHigh, static_cast<uint8_t>((vcom >> 8) & 0x01)};
    const uint8_t vcom_low[2] = {kTpsVcomLow, static_cast<uint8_t>(vcom & 0xFF)};
    (void)bus_->write(kTpsAddress, vcom_high, sizeof(vcom_high));
    (void)bus_->write(kTpsAddress, vcom_low, sizeof(vcom_low));
    // **I el registre que diu que tot hi és**: `0xFA` is every rail good, and a supply that never says it is a panel
    // that would paint nothing (or worse, half a page). It is waited for, and the number that came back is said.
    uint8_t status = 0;
    for (int i = 0; i < 250; ++i) {
      if (bus_->read(kTpsAddress, kTpsPowerGood, &status, 1) && (status & 0xFA) == 0xFA) {
        // **I el número que ho diu, per si el que falla és una sola via** (0.190.34): `0xFA` is every rail good, i un
        // registre amb un bit a zero és una tensió que no hi és i que es pot dir pel nom (docs/50).
        logf(pal::kLogDebug, "PANEL", "the supply is up, pg %02x", status);
        powered_ = true;
        return true;
      }
      vTaskDelay(pdMS_TO_TICKS(2));
    }
    error_ = "the supply never said the power was good";
    logf(pal::kLogWarn, "PANEL", "the supply answered %02x and not fa; the panel stays down", status);
    gates_down();
    return false;
  }
  gates_down();
  return true;
}

// **I la baixada, al revés**: the output enable first, then the gates, and the wake last, which is the order that
// leaves the panel discharged instead of holding a rail up with no output enabled (docs/50).
void Epd47::gates_down() {
  if (expander_ == nullptr) return;
  expander_->write(Expander::kPanelOe, false);
  expander_->write(Expander::kPanelMode, false);
  expander_->write(Expander::kTpsPowerUp, false);
  expander_->write(Expander::kTpsVcomCtrl, false);
  vTaskDelay(pdMS_TO_TICKS(2));
  expander_->write(Expander::kTpsWakeUp, false);
  powered_ = false;
}

void Epd47::row_control(bool start) {
  const gpio_num_t ckv = static_cast<gpio_num_t>(kCkv);
  const gpio_num_t stv = static_cast<gpio_num_t>(kStv);
  const gpio_num_t le = static_cast<gpio_num_t>(kLe);
  if (start) {
    // **El començament d'un passatge**, amb els seus temps: the four edges are the maker's and the numbers are the ones
    // its driver waits (7, 10, 8, 10, 18, 18 microseconds), o sigui que no són d'aquest fitxer sinó del panell.
    gpio_set_level(ckv, 1);
    delayMicroseconds(7);
    gpio_set_level(stv, 0);
    delayMicroseconds(10);
    gpio_set_level(ckv, 0);
    gpio_set_level(ckv, 1);
    delayMicroseconds(8);
    gpio_set_level(stv, 1);
    delayMicroseconds(10);
    gpio_set_level(ckv, 0);
    gpio_set_level(ckv, 1);
    delayMicroseconds(18);
    gpio_set_level(ckv, 0);
    gpio_set_level(ckv, 1);
    delayMicroseconds(18);
    gpio_set_level(ckv, 0);
    gpio_set_level(ckv, 1);
    return;
  }
  // **I el tancament d'una fila**: the data has been clocked, so the latch takes it.
  gpio_set_level(ckv, 0);
  gpio_set_level(le, 1);
  gpio_set_level(le, 0);
}

bool Epd47::start_row(const uint8_t* row) {
  if (io_ == nullptr) return false;
  s_dma_done = false;
  if (esp_lcd_panel_io_tx_color(reinterpret_cast<esp_lcd_panel_io_handle_t>(io_), -1, row, kRowBytes) != ESP_OK) {
    error_ = "the bus did not take the row";
    return false;
  }
  pending_ = true;
  return true;
}

bool Epd47::wait_row() {
  if (!pending_) return true;
  const uint32_t started = millis();
  while (!s_dma_done) {
    if ((millis() - started) > kRowTimeoutMs) {
      error_ = "the row never finished";
      pending_ = false;
      return false;
    }
  }
  pending_ = false;
  return true;
}

// **Una neteja és N passades del mateix codi a tota la finestra** (0.190.56), que és `bbepClear` del fabricant: no depèn del
// que hi hagi dibuixat, i per això és l'única manera de deixar el paper en un lloc conegut abans de la taula (docs/50).
bool Epd47::clear_the_window(uint16_t first, uint16_t last, uint16_t b0, uint16_t b1, uint8_t code, uint8_t passes) {
  uint8_t codes[16];
  memset(codes, code, sizeof(codes));
  for (uint8_t i = 0; i < passes; ++i) {
    if (!scan_a_pass(first, last, b0, b1, false, (code == epd47::kCodeNeutral) ? nullptr : codes)) return false;
    delayMicroseconds(kBetweenPhasesUs);
  }
  return true;
}

bool Epd47::update_the_glass(uint16_t first, uint16_t last, uint16_t b0, uint16_t b1, bool fast, bool only_changed) {
  if (!begin()) return false;
  // **Un passatge és una passada de tota la forma d'ona** (docs/50): for each phase the glass is scanned once, and what
  // each pixel is told to do comes from the panel's own table indexed by the level it has to end at. **Les files de
  // sobre del rectangle surten igualment** (with the code that does nothing) perquè la porta del panell s'ha de recórrer
  // des de la primera línia, i les de sota no surten: no reben cap impuls i es queden com estaven, que és exactament el
  // que vol una actualització d'una part del vidre (docs/50, docs/95).
  // **Quantes passades i amb quina taula** (0.190.55): the maker's eighteen for sixteen greys, and the six of
  // `kFastPhases` for a `du`, que és de dues tintes i no necessita divuit escombrades; the table and the count are decided
  // once, here, and the rest of the loop is the same (docs/50, docs/95).
  const uint8_t phases = fast ? epd47::kFastPhases : epd47::kGrayPhases;
  const uint8_t* const before = only_changed ? previous_ : nullptr;
  // **I la neteja del fabricant abans de la taula** (0.190.56): their `fullUpdate()` defaults to `CLEAR_SLOW`, which is four
  // passes towards black, eight towards white, four towards black and eight towards white, and one neutral —the page ends
  // **white**—, and only then the eighteen passes of the matrix. La matriu compta des d'un paper blanc; sense això, el que hi
  // hagi al vidre es queda a sota i la pàgina surt bruta i fosca, que és exactament el que passava (docs/50, docs/95).
  if (!fast && !only_changed) {
    if (!clear_the_window(first, last, b0, b1, epd47::kCodeBlack, epd47::kClearDarkPasses)) return false;
    if (!clear_the_window(first, last, b0, b1, epd47::kCodeWhite, epd47::kClearLightPasses)) return false;
    if (!clear_the_window(first, last, b0, b1, epd47::kCodeBlack, epd47::kClearDarkPasses)) return false;
    if (!clear_the_window(first, last, b0, b1, epd47::kCodeWhite, epd47::kClearLightPasses)) return false;
    if (!scan_a_pass(first, last, b0, b1, false, nullptr)) return false;
  }
  for (uint8_t phase = 0; phase < phases; ++phase) {
    // **El codi de cada nivell en aquesta passada, tal com el fabricant el té escrit** (0.190.55): `kGrayMatrix` és la seva
    // taula de setze per divuit i no s'hi toca res; el `du` fa servir la curta d'aquest projecte (docs/50, docs/95).
    uint8_t codes[16];
    for (uint8_t level = 0; level < 16; ++level) {
      codes[level] = fast ? ((level < 8) ? epd47::kFastToBlack[phase] : epd47::kFastToWhite[phase])
                          : epd47::kGrayMatrix[level][phase];
    }
    if (!scan_a_pass(first, last, b0, b1, before != nullptr, &codes[0])) return false;
    delayMicroseconds(kBetweenPhasesUs);
  }
  // **I la descàrrega**, que és el que deixa els drivers del panell en repòs i no amb una tensió a sobre: one pass of
  // «do nothing» codes, which is the maker's `BB_CLEAR_NEUTRAL` (docs/50).
  return scan_a_pass(first, last, b0, b1, false, nullptr);
}

bool Epd47::scan_a_pass(uint16_t first, uint16_t last, uint16_t b0, uint16_t b1, bool only_changed,
                        const uint8_t* codes) {
  const gpio_num_t ckv = static_cast<gpio_num_t>(kCkv);
  // **El començament del passatge**, que deixa el rellotge vertical a dalt.
  // **Els dos miralls d'aquest passatge** (0.190.56): un byte del quadre són dos píxels i un byte de la fila en són quatre, o
  // sigui que les dues meitats d'un byte de sortida surten de dos bytes d'entrada i una taula de 256 ho resol d'un cop.
  uint8_t hi[256];
  uint8_t lo[256];
  for (uint32_t v = 0; v < 256; ++v) {
    const uint8_t high = (codes != nullptr) ? codes[v >> 4] : 0;
    const uint8_t low = (codes != nullptr) ? codes[v & 0x0F] : 0;
    hi[v] = static_cast<uint8_t>((high << 6) | (low << 4));
    lo[v] = static_cast<uint8_t>((high << 2) | low);
  }
  row_control(true);
  uint8_t* buffers[2] = {row_, row2_};
  bool pending = false;
  for (uint16_t y = 0; y <= last && y < kGlassH; ++y) {
    // **La porta es tanca ABANS de fabricar la fila** (0.190.56), i això és el que fa que la tinta rebi el que ha de rebre: a
    // `bbepWriteRow` el `ROW_STEP` —`CKV` baix i pols de `LE`— passa abans de la transferència, però la fabricació de la fila
    // següent la fa amb la porta encara oberta. Aquí la fabricació és molt més lenta que la seva —el quadre és a la PSRAM i es
    // llegeix byte a byte—, o sigui que la porta quedava oberta vuit vegades més estona que la línia del panell i la tinta rebia
    // vuit vegades més càrrega per passada. Tancar-la primer deixa el temps de porta en el de la transferència, que és el que el
    // panell espera (docs/50, docs/95).
    if (pending) {
      if (!wait_row()) return false;
      row_control(false);
    }
    uint8_t* row = buffers[y & 1u];
    const uint32_t before_the_row = static_cast<uint32_t>(esp_timer_get_time());
    // **Només la finestra marcada porta codi** (0.190.40): a byte outside `b0..b1` is left at the code that does nothing, o
    // sigui que la tinta només es mou dins del rectangle que el nucli ha marcat (docs/50, docs/95).
    memset(row, 0, kRowBytes);
    if (codes != nullptr && y >= first) {
      const uint32_t at = static_cast<uint32_t>(y) * kGlassStride;
      const uint8_t* src = bits_ + at;
      const uint8_t* was = only_changed ? (previous_ + at) : nullptr;
      // **Dos miralls de 256 entrades i una fila feta de paraules** (0.190.56): the same two lookups the maker does
      // (`pGrayUpper`/`pGrayLower` in FastEPD), o sigui que una fila de 240 bytes surt amb 240 parells de lectures i un OR en
      // lloc de quatre cerques per byte. La fabricació passa de ~70 µs a uns pocs microsegons per fila, que és el que fa que
      // divuit passades més la neteja siguin un segon i no quatre (docs/50, docs/95).
      for (uint16_t i = b0; i <= b1; ++i) {
        const uint8_t a = src[2 * i];
        const uint8_t b = src[2 * i + 1];
        uint8_t out = static_cast<uint8_t>(hi[a] | lo[b]);
        if (was != nullptr) {
          // **El motor de la diferència** (0.190.41): a pixel whose level is the one the glass has gets no code at all.
          const uint8_t pa = was[2 * i];
          const uint8_t pb = was[2 * i + 1];
          uint8_t keep = 0xFF;
          if ((pa >> 4) == (a >> 4)) keep = static_cast<uint8_t>(keep & 0x3Fu);
          if ((pa & 0x0F) == (a & 0x0F)) keep = static_cast<uint8_t>(keep & 0xCFu);
          if ((pb >> 4) == (b >> 4)) keep = static_cast<uint8_t>(keep & 0xF3u);
          if ((pb & 0x0F) == (b & 0x0F)) keep = static_cast<uint8_t>(keep & 0xFCu);
          out = static_cast<uint8_t>(out & keep);
        }
        row[i] = out;
      }
    }
    cpu_us_ += static_cast<uint32_t>(esp_timer_get_time()) - before_the_row;
    gpio_set_level(ckv, 1);
    // **I la fila que ja viatjava es recull ara**, quan la d'aquesta ja està construïda: és el que fa que el temps de la
    // conversió i el del cable es tapin l'un a l'altre (docs/50, docs/95).
    const uint32_t before_the_wire = static_cast<uint32_t>(esp_timer_get_time());
    if (!start_row(row)) return false;
    pending = true;
    bus_us_ += static_cast<uint32_t>(esp_timer_get_time()) - before_the_wire;
  }
  if (pending && !wait_row()) return false;
  // **I l'última fila també es tanca** (docs/50).
  row_control(false);
  return true;
}

bool Epd47::flush(UpdateMode mode) {
  if (!started_ && !begin()) return false;
  // **Res marcat és una pintada que no surt** (0.190.37): the contract of `ISurface` —a page nobody asked to be repainted
  // is a page this system does not touch— i en aquest vidre una pintada buida no és gratis, són dos segons i mig.
  if (nothing_dirty()) return true;
  // **El rectangle marcat, al vidre** (0.190.40): the rows say where the scan stops and the columns say what is worth
  // driving; el que queda fora de les dues coses no surt o surt amb el codi que no fa res (docs/50, docs/95).
  uint16_t gx0 = 0;
  uint16_t gy0 = 0;
  uint16_t gx1 = static_cast<uint16_t>(kGlassW - 1);
  uint16_t gy1 = static_cast<uint16_t>(kGlassH - 1);
  dirty_glass(gx0, gy0, gx1, gy1);
  uint16_t first = gy0;
  uint16_t last = gy1;
  // **Dues unitats i no una** (0.190.43, i era un error de comptar): el **quadre** del vidre és de quatre bits per píxel, o
  // sigui que un byte seu són **dos** píxels; però la **fila que surt pel bus** és de dos bits per píxel, o sigui que un
  // byte seu en són **quatre**. Confondre-les pintava cada finestra al doble d'ample i desplaçada —la banda del sistema
  // quedava tallada a l'esquerra i el rentat de l'splash començava vint columnes enllà— (docs/50, docs/95).
  uint16_t fb0 = static_cast<uint16_t>(gx0 >> 1);
  uint16_t fb1 = static_cast<uint16_t>(gx1 >> 1 < (kGlassStride - 1) ? gx1 >> 1 : (kGlassStride - 1));
  // **El mode que demana el nucli és el que decideix la forma d'ona** (0.190.38): `du`, `du4` and `a2` are the two-level
  // ones and they get the six phases of `kFastPhases`; every other one —and `gc16` above all— gets the panel's own
  // thirty-eight, que és la que neteja el fantasma que les curtes deixen (docs/50).
  const bool fast = mode == UpdateMode::kDu || mode == UpdateMode::kDu4 || mode == UpdateMode::kA2;
  // **I si la pintada ha de moure tots els píxels o només els que han canviat** (0.190.41, i és el vocabulari del vidre):
  // `gc16` and `init` are the **cleaning** ones —they drive the whole rectangle on purpose, which is what takes the ghost
  // away—; every other quality is a paint that must not touch what is already right, i `gld16`/`gl16` volen dir
  // exactament això al controlador del Paper («els setze grisos sense esborrar el que ja hi és»). **I la primera pintada
  // d'un arrencada no es pot comparar amb res**, perquè el vidre pot tenir la imatge d'abans: aquella mou tot el rectangle
  // i és la que deixa `previous_` sabut (docs/50, docs/95).
  const bool cleans = mode == UpdateMode::kInit || mode == UpdateMode::kGc16;
  const bool only_changed = !cleans && previous_is_known_;
  // **El número de passades que es paguen de debò**, que quan la pintada neteja inclou la neteja del fabricant (0.190.56).
  const uint8_t clear_phases = static_cast<uint8_t>(2 * epd47::kClearDarkPasses + 2 * epd47::kClearLightPasses + 1);
  uint8_t phases = fast ? epd47::kFastPhases
                        : static_cast<uint8_t>(epd47::kGrayPhases + (only_changed ? 0 : clear_phases));
  // **I la finestra s'encongeix al que de debò s'ha mogut** (0.190.41): the rectangle the kernel marks is what it *drew*,
  // and a page that is redrawn whole is a rectangle of five hundred and forty rows for two digits that changed. Comparing
  // the two frames —a `memcmp` a row, which is cheap and in the PSRAM— gives the box of what is not the same, o sigui que
  // la porta del panell camina fins a la primera fila que ha canviat i prou, i una pintada on no ha canviat res no es gasta.
  // **Els modes que netegen no s'encongeixen**: `gc16` vol passar per tot el rectangle i és el que treu el fantasma
  // (docs/50, docs/95).
  if (only_changed) {
    uint16_t near_row = 0xFFFF;
    uint16_t far_row = 0;
    uint16_t left = 0xFFFF;
    uint16_t right = 0;
    const uint16_t count = static_cast<uint16_t>(fb1 - fb0 + 1);
    for (uint16_t y = first; y <= last && y < kGlassH; ++y) {
      const uint32_t at = static_cast<uint32_t>(y) * kGlassStride;
      const uint8_t* now = bits_ + at + fb0;
      const uint8_t* was = previous_ + at + fb0;
      if (memcmp(now, was, count) == 0) continue;
      uint16_t i = 0;
      while (i + 1 < count && now[i] == was[i]) ++i;
      uint16_t j = static_cast<uint16_t>(count - 1);
      while (j > i && now[j] == was[j]) --j;
      if (y < near_row) near_row = y;
      far_row = y;
      if (static_cast<uint16_t>(fb0 + i) < left) left = static_cast<uint16_t>(fb0 + i);
      if (static_cast<uint16_t>(fb0 + j) > right) right = static_cast<uint16_t>(fb0 + j);
    }
    if (near_row == 0xFFFF) {
      // **No hi ha res a moure**: el rectangle es deixa anar i no es paga cap passada.
      forget_the_dirty();
      logf(pal::kLogDebug, "PANEL", "%s: nothing changed in %u,%u %ux%u, no paint", mode_name(mode),
           static_cast<unsigned>(gx0), static_cast<unsigned>(first), static_cast<unsigned>(gx1 - gx0 + 1),
           static_cast<unsigned>(last - first + 1));
      return true;
    }
    first = near_row;
    last = far_row;
    fb0 = left;
    fb1 = right;
    gx0 = static_cast<uint16_t>(fb0 * 2);
    gx1 = static_cast<uint16_t>(fb1 * 2 + 1);
  }
  // **I la fila del bus, que és l'altra unitat**: un byte seu són quatre columnes del vidre.
  const uint16_t b0 = static_cast<uint16_t>(gx0 >> 2);
  const uint16_t b1 = static_cast<uint16_t>((gx1 >> 2) < (kRowBytes - 1) ? (gx1 >> 2) : (kRowBytes - 1));
  bus_us_ = 0;
  cpu_us_ = 0;
  const uint32_t started = static_cast<uint32_t>(esp_timer_get_time());
  busy_ = true;
  const bool ok = power(true) && update_the_glass(first, last, b0, b1, fast, only_changed);
  busy_ = false;
  const uint32_t took_ms = (static_cast<uint32_t>(esp_timer_get_time()) - started) / 1000;
  // **I el rectangle que s'ha pintat passa a ser el que hi ha al vidre** (0.190.41): la comparació de la propera pintada
  // és contra el que hem donat al panell, o sigui que el que es copia és el rectangle i no el quadre sencer —les files i
  // els bytes que han sortit— i només si la pintada ha arribat al final (docs/50).
  if (ok) {
    for (uint16_t y = first; y <= last && y < kGlassH; ++y) {
      const uint32_t at = static_cast<uint32_t>(y) * kGlassStride + fb0;
      const size_t len = static_cast<size_t>(fb1 - fb0 + 1);
      if (!fast) {
        memcpy(previous_ + at, bits_ + at, len);
        continue;
      }
      // **Un `du` deixa dues tintes, i el que s'apunta és el que hi ha al vidre i no el que volia el nucli** (0.190.41):
      // after a six-phase pass the ink of that window is black or white, o sigui que el quadre anterior es guarda
      // arrodonit —si no, la propera comparació diria que un gris ha canviat quan al vidre no hi ha cap gris (docs/50).
      const uint8_t* from = bits_ + at;
      uint8_t* to = previous_ + at;
      for (size_t i = 0; i < len; ++i) {
        const uint8_t src = from[i];
        const uint8_t hi = ((src >> 4) & 0x0F) < 8 ? 0x00 : 0xF0;
        const uint8_t lo = (src & 0x0F) < 8 ? 0x00 : 0x0F;
        to[i] = static_cast<uint8_t>(hi | lo);
      }
    }
    previous_is_known_ = true;
    forget_the_dirty();
  }
  logf(pal::kLogDebug, "PANEL",
       "%s: %u,%u %ux%u of the glass, %s, %u phases, %u ms (%u on the wire and %u elsewhere) at %u MHz", mode_name(mode),
       static_cast<unsigned>(gx0), static_cast<unsigned>(first),
       static_cast<unsigned>(gx1 - gx0 + 1), static_cast<unsigned>(last - first + 1),
       only_changed ? "only what changed" : "the whole window", static_cast<unsigned>(phases),
       static_cast<unsigned>(took_ms), static_cast<unsigned>(bus_us_ / 1000),
       static_cast<unsigned>(cpu_us_ / 1000), static_cast<unsigned>(pclk_hz_ / 1000000));
  return ok;
}

bool Epd47::rest() {
  if (!started_) return false;
  return power(false);
}

ISurface* t5s3_epd47(Pca9535* expander, II2c* bus) {
  static Epd47 panel(expander, bus);
  return &panel;
}

}  // namespace hal
