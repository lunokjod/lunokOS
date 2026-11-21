// **El banc de proves del vidre, i no és el sistema** (0.190.127, l'amo: *«Mes cosetes per la pantalla eink! Vull que facis
// possible això dels 60hz»*). Aquest fitxer no arrenca lunokOS: és un `app_main` solt que es compila amb el seu propi entorn
// (`pio run -e t5s3-epd-video`) i es flasheja quan hom vol mirar el panell sense el nucli a sobre. Serveix per respondre una
// pregunta que el sistema no pot respondre sol —què costa de debò enviar un frame sencer pel bus d'aquest panell, i quantes
// vegades per segon se'n poden enviar— i per créixer, si cal, cap al motor de vídeo del fabricant (I80 + DMA + comptadors per
// píxel), que és l'única via que dona imatge contínua en una tinta electrònica (docs/50, docs/95).
//
// El que fa ara: engega l'I²C de la placa, mira si l'expansor (PCA9535) i el TPS65185 hi són i què diuen, obre el bus
// paral·lel amb els pins d'aquesta placa i mesura l'enviament de frames sencers amb el DMA. Res de tot això toca la flash del
// sistema ni la seva configuració: el disc i la NVS de lunokOS no es llegeixen ni s'escriuen.
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

const char kTag[] = "epd_probe";

// **Els catorze fils del panell** (docs/pinmap.md del repositori de la placa, i els mateixos que el nucli fa servir): les vuit
// línies de dades en l'ordre en què viatja el nibble, el rellotge, i les quatre que diuen on comença i on acaba una fila.
const int kI2cSda = 39;
const int kI2cScl = 40;
const int kCkh = 4;                              // the clock of the data (EP_CKH; the i80 calls it WR)
const int kData[8] = {5, 6, 7, 15, 16, 17, 18, 8};
const int kStv = 45;                             // starts a frame
const int kCkv = 48;                             // the vertical clock
const int kSth = 41;                             // the horizontal start of a row (the i80's "chip select")
const int kLe = 42;                              // latches the row that has just been clocked
const int kDummyDc = 46;                         // a pin the peripheral demands and the panel does not have

// **Els dos xips que donen vida al panell**: l'expansor que porta l'`OE`, el `MODE` i els quatre comandaments de la font, i el
// TPS65185 que dona les tensions (docs/50).
const uint8_t kExpander = 0x20;
const uint8_t kTps = 0x68;
const uint8_t kTpsEnable = 0x01;
const uint8_t kTpsVcomLow = 0x03;
const uint8_t kTpsVcomHigh = 0x04;
const uint8_t kTpsPowerGood = 0x0F;
const uint16_t kVcomMillivolts = 1560;           // el número que la placa porta de fàbrica

// **Una fila del bus**: 960 píxels de dos bits són 240 bytes, i el buffer del DMA és una fila (docs/50).
const size_t kRowBytes = 960 / 4;
const uint16_t kRows = 540;
const uint32_t kBusHz = 24000000;                // el panell aguanta fins a vint-i-sis; vint-i-quatre és el que s'ha mesurat

i2c_port_t g_i2c = I2C_NUM_0;

bool i2c_up() {
  i2c_config_t config = {};
  config.mode = I2C_MODE_MASTER;
  config.sda_io_num = kI2cSda;
  config.sda_pullup_en = GPIO_PULLUP_ENABLE;
  config.scl_io_num = kI2cScl;
  config.scl_pullup_en = GPIO_PULLUP_ENABLE;
  config.master.clk_speed = 400000;
  if (i2c_param_config(g_i2c, &config) != ESP_OK) return false;
  return i2c_driver_install(g_i2c, I2C_MODE_MASTER, 0, 0, 0) == ESP_OK;
}

bool i2c_write(uint8_t address, const uint8_t* data, size_t size) {
  return i2c_master_write_to_device(g_i2c, address, data, size, pdMS_TO_TICKS(50)) == ESP_OK;
}

bool i2c_read(uint8_t address, uint8_t reg, uint8_t* out, size_t size) {
  return i2c_master_write_read_device(g_i2c, address, &reg, 1, out, size, pdMS_TO_TICKS(50)) == ESP_OK;
}

// **L'expansor, amb el registre de configuració del propi xip**: en un PCA9535 un `1` vol dir entrada, o sigui que `0xC400`
// són les tres entrades de la placa i tota la resta sortides —les mateixes que el nucli escriu— (docs/50).
bool expander_up() {
  const uint8_t config[3] = {0x06, 0x00, 0xC4};
  return i2c_write(kExpander, config, sizeof(config));
}

bool expander_write(int pin, bool high) {
  uint8_t port = 0;
  const uint8_t reg = (pin < 8) ? 0x02 : 0x03;
  if (!i2c_read(kExpander, reg, &port, 1)) return false;
  const uint8_t bit = static_cast<uint8_t>(1u << (pin % 8));
  port = high ? static_cast<uint8_t>(port | bit) : static_cast<uint8_t>(port & ~bit);
  const uint8_t out[2] = {reg, port};
  return i2c_write(kExpander, out, sizeof(out));
}

bool expander_read_io1(uint8_t& value) { return i2c_read(kExpander, 0x01, &value, 1); }

bool tps_write(uint8_t reg, const uint8_t* data, size_t size) {
  uint8_t buffer[4] = {reg, 0, 0, 0};
  if (size > 3) return false;
  memcpy(buffer + 1, data, size);
  return i2c_write(kTps, buffer, size + 1);
}

// **La seqüència d'encesa del panell**, copiada del nucli, que és la que aquesta placa ha provat: el `MODE` alt (sense això la
// tinta no es mou gens), l'`OE`, el despertar i el `POWER_UP` de la font, la VCOM i, finalment, que el TPS digui que tot hi és
// (docs/50, docs/95).
bool panel_power_on() {
  if (!expander_write(9, true)) return false;    // IO1_1: GMOD
  if (!expander_write(8, true)) return false;    // IO1_0: OE
  if (!expander_write(13, true)) return false;   // IO1_5: TPS wake
  if (!expander_write(11, true)) return false;   // IO1_3: TPS power up
  if (!expander_write(12, true)) return false;   // IO1_4: VCOM control
  gpio_set_level(static_cast<gpio_num_t>(kStv), 1);
  vTaskDelay(pdMS_TO_TICKS(2));
  // El `POWER_GOOD` del xip de la font és el primer que ha de dir que sí, i s'espera amb un topall (docs/50).
  for (int i = 0; i < 200; ++i) {
    uint8_t io = 0;
    if (expander_read_io1(io) && (io & 0x40) != 0) break;
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  const uint8_t enable = 0x3F;
  if (!tps_write(kTpsEnable, &enable, 1)) return false;
  const uint16_t vcom = static_cast<uint16_t>(kVcomMillivolts / 10);
  const uint8_t high = static_cast<uint8_t>((vcom >> 8) & 0x01);
  const uint8_t low = static_cast<uint8_t>(vcom & 0xFF);
  (void)tps_write(kTpsVcomHigh, &high, 1);
  (void)tps_write(kTpsVcomLow, &low, 1);
  for (int i = 0; i < 250; ++i) {
    uint8_t status = 0;
    if (i2c_read(kTps, kTpsPowerGood, &status, 1) && (status & 0xFA) == 0xFA) {
      ESP_LOGI(kTag, "the supply is up (0x%02x), VCOM %u mV", status, static_cast<unsigned>(kVcomMillivolts));
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  ESP_LOGE(kTag, "the supply never said the power was good");
  return false;
}

esp_lcd_i80_bus_handle_t g_bus = nullptr;
esp_lcd_panel_io_handle_t g_io = nullptr;

bool bus_up() {
  esp_lcd_i80_bus_config_t config = {};
  config.dc_gpio_num = kDummyDc;
  config.wr_gpio_num = kCkh;
  config.clk_src = LCD_CLK_SRC_PLL160M;
  config.bus_width = 8;
  config.max_transfer_bytes = 1024;
  for (size_t i = 0; i < 8; ++i) config.data_gpio_nums[i] = kData[i];
  if (esp_lcd_new_i80_bus(&config, &g_bus) != ESP_OK) return false;

  esp_lcd_panel_io_i80_config_t io = {};
  io.cs_gpio_num = kSth;                         // el STH del panell: polsat un cop per fila
  io.pclk_hz = kBusHz;
  io.trans_queue_depth = 2;
  io.lcd_cmd_bits = 0;                           // aquest panell no té cap byte d'ordre
  io.lcd_param_bits = 8;
  io.dc_levels.dc_idle_level = 1;
  io.dc_levels.dc_cmd_level = 1;
  io.dc_levels.dc_data_level = 1;
  io.dc_levels.dc_dummy_level = 1;
  return esp_lcd_new_panel_io_i80(g_bus, &io, &g_io) == ESP_OK;
}

void row_control(bool start) {
  const gpio_num_t ckv = static_cast<gpio_num_t>(kCkv);
  const gpio_num_t stv = static_cast<gpio_num_t>(kStv);
  const gpio_num_t le = static_cast<gpio_num_t>(kLe);
  if (start) {
    gpio_set_level(ckv, 1);
    esp_rom_delay_us(7);
    gpio_set_level(stv, 0);
    esp_rom_delay_us(10);
    gpio_set_level(ckv, 0);
    gpio_set_level(ckv, 1);
    esp_rom_delay_us(8);
    gpio_set_level(stv, 1);
    esp_rom_delay_us(10);
    gpio_set_level(ckv, 0);
    for (int i = 0; i < 3; ++i) {
      gpio_set_level(ckv, 1);
      esp_rom_delay_us(18);
      gpio_set_level(ckv, 0);
    }
    gpio_set_level(ckv, 1);
    return;
  }
  gpio_set_level(ckv, 0);
  gpio_set_level(le, 1);
  gpio_set_level(le, 0);
  // **I la porta, que és el que deixa entrar la càrrega a la tinta** (0.190.128): closing the row is not enough —the source driver
  // holds the codes, but the charge only reaches the ink while the gate of that line is open—, o sigui que després del latch el
  // rellotge vertical es torna a obrir el temps que aquest vidre necessita per donar un esglaó. El número és el del nostre driver:
  // nou-cents nanosegons per fila, i quinze files fan els tretze microsegons i mig del camí de blanc a negre.
  gpio_set_level(ckv, 1);
  // **Dos microsegons i no un** (0.190.130): fifteen of these are the road from white to black in this panel and the road is 13,5 µs, o
  // sigui que un microsegons per frame hi arriba just i la vora queda a mig camí. Amb dos, cada cicle satura i el que queda a la vora és
  // tinta que ha rebut de debò (docs/50, docs/95).
  esp_rom_delay_us(2);
  gpio_set_level(ckv, 0);
}

void control_pins_up() {
  gpio_config_t config = {};
  config.mode = GPIO_MODE_OUTPUT;
  config.pull_up_en = GPIO_PULLUP_DISABLE;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  config.pin_bit_mask = (1ULL << kStv) | (1ULL << kCkv) | (1ULL << kLe);
  gpio_config(&config);
  gpio_set_level(static_cast<gpio_num_t>(kLe), 0);
  gpio_set_level(static_cast<gpio_num_t>(kStv), 1);
  gpio_set_level(static_cast<gpio_num_t>(kCkv), 1);
}

// ---- el motor de vídeo -------------------------------------------------------------------------------------------------
// **El que fa que una tinta es mogui a imatge contínua** (0.190.128): the panel takes two bits per pixel and applies one pulse per
// frame to whatever they say —`0` nothing, `1` towards black, `2` towards white— so a picture sent again and again moves the ink a
// little on every frame instead of waiting for a whole waveform.
//
// **La primera versió comptava les empentes de cada píxel i era deu vegades més lenta** (0.190.129, el registre de l'amo: `10 frames
// a second with the bar on the glass` contra els `85 a second at best` del cable): un byte per píxel són cinc-cents divuit mil
// accessos a la PSRAM per frame, o sigui setanta-vuit mil·lisegons resolent a mà un compte que el panell ja sap fer. Ara no hi ha
// estat per píxel: cada frame diu **cap a on ha d'anar cada píxel** i prou, una taula de 256 ho converteix de vuit píxels a dos bytes,
// i el que es paga per frame són seixanta-cinc mil cerques en lloc de mig milió d'accessos.
const uint8_t kCodeBlack = 1;
const uint8_t kCodeWhite = 2;
const uint16_t kWidth = 960;
const uint16_t kHeight = 540;
const size_t kWantBytes = static_cast<size_t>(kWidth / 8) * kHeight;

uint8_t* g_want = nullptr;      // what the page should show, one bit a pixel
uint8_t* g_shown = nullptr;     // what we believe the ink has, one bit a pixel
// **Dues taules i no una** (0.190.130): the codes of eight pixels are sixteen bits, and the two directions are two masks —one where
// the pixel has to go towards black and one where it has to go towards white— that are put together with an OR. Dos cops de taula
// per byte de vuit píxels, que és el que fa que el motor no costi més que el cable (docs/50).
uint16_t g_to_black[256];
uint16_t g_to_white[256];

// **El frame, construït una vegada i reenviat** (0.190.131, l'amo: *«Vols que retalli els 25,6 ms de CPU»*): the codes of the page are
// the same on every frame of a cycle —`want` and `shown` only change when the picture does—, o sigui que tornar-los a construir quinze
// cops és pagar quinze vegades la mateixa feina. Es construeixen un cop, es desen a la PSRAM i cada frame el que fa el motor és una
// còpia de 240 bytes per fila cap a la memòria del DMA (que no pot llegir la PSRAM) i l'enviament.
const size_t kFrameBytes = static_cast<size_t>(kRowBytes) * kHeight;
uint8_t* g_frame = nullptr;      // the codes of the whole page, built once a cycle
// **El reset, per als píxels que canvien i per a ningú més** (0.190.133, l'amo: *«parpalleja la barra negra»*): the first version reset the
// whole glass to white at the top of every cycle, o sigui que la imatge desapareixia dos frames cada cicle —el parpelleig que va veure—.
// The reset is per pixel: a pixel that has to move gets two pushes towards the opposite rail first, i el que no ha de canviar no rep res.
uint8_t* g_reset = nullptr;      // the two pushes towards the opposite rail, for the pixels that move

bool the_picture_is_up() {
  // **Un bit encès vol dir negre, i això és el que tot el motor suposa** (0.190.132): the first version of the pattern had the bar at zero
  // and the page at one —the opposite of the engine's convention—, o sigui que amb la pàgina ja blanca el motor empenyia la barra cap al
  // blanc i no es veia res. `0` és blanc i `1` és negre, d'aquí en avall (docs/50).
  g_want = static_cast<uint8_t*>(heap_caps_malloc(kWantBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  g_shown = static_cast<uint8_t*>(heap_caps_malloc(kWantBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  g_reset = static_cast<uint8_t*>(heap_caps_malloc(kFrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  // **El frame que s'envia quinze cops va a la RAM interna i amb el DMA a sobre** (0.190.133): this probe has no lunokOS in it, o sigui que la
  // RAM interna li sobra; a PSRAM el DMA no hi pot llegir i cada frame pagava cent vint-i-nou kilobytes de còpia (7,5 ms dels 20 que costava).
  g_frame = static_cast<uint8_t*>(heap_caps_malloc(kFrameBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
  if (g_frame == nullptr) {
    ESP_LOGW(kTag, "no room for the frame in internal memory: falling back to the PSRAM");
    g_frame = static_cast<uint8_t*>(heap_caps_malloc(kFrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (g_want == nullptr || g_shown == nullptr || g_reset == nullptr || g_frame == nullptr) return false;
  memset(g_want, 0x00, kWantBytes);
  memset(g_shown, 0x00, kWantBytes);
  memset(g_reset, 0x00, kFrameBytes);
  for (int v = 0; v < 256; ++v) {
    uint16_t black = 0;
    uint16_t white = 0;
    for (int bit = 0; bit < 8; ++bit) {
      const bool on = ((v >> (7 - bit)) & 1u) != 0;
      black = static_cast<uint16_t>((black << 2) | (on ? kCodeBlack : 0));
      white = static_cast<uint16_t>((white << 2) | (on ? kCodeWhite : 0));
    }
    g_to_black[v] = black;
    g_to_white[v] = white;
  }
  return true;
}

// **El patró que es mou**: a white page with a black bar that walks. És el que fa visible el ghosting i el que diu, a ull, si els
// frames arriben o no (docs/95).
void the_bar_walks(int frame) {
  memset(g_want, 0x00, kWantBytes);   // la pàgina, blanca
  const int bar = 120;
  const int walk = (frame * 6) % (kWidth + bar);
  const int left = walk - bar;
  for (int y = 0; y < static_cast<int>(kHeight); ++y) {
    for (int x = 0; x < bar; ++x) {
      const int at = left + x;
      if (at < 0 || at >= static_cast<int>(kWidth)) continue;
      const size_t pixel = static_cast<size_t>(y) * kWidth + static_cast<size_t>(at);
      g_want[pixel >> 3] = static_cast<uint8_t>(g_want[pixel >> 3] | (0x80u >> (pixel & 7)));
    }
  }
}

// **Una fila de codis, i només per als píxels que han de moure's** (0.190.130, l'amo: *«La barra es veu amb vores una mica difoses»*).
// The eight pixels of a byte are compared with what the ink is believed to have: a pixel that is already where it has to be gets the
// code that does nothing, and the page's white stops being pushed frame after frame —que és el que deixava el fons brut i les vores
// toves— (docs/50, docs/95).
void build_the_row(uint16_t y, uint8_t* row) {
  const size_t at = static_cast<size_t>(y) * (kWidth / 8);
  const uint8_t* want = g_want + at;
  const uint8_t* shown = g_shown + at;
  uint8_t* out = row;
  for (uint16_t b = 0; b < kWidth / 8; ++b) {
    const uint8_t differs = static_cast<uint8_t>(want[b] ^ shown[b]);
    const uint16_t codes = static_cast<uint16_t>(g_to_black[static_cast<uint8_t>(differs & want[b])] |
                                                 g_to_white[static_cast<uint8_t>(differs & ~want[b])]);
    *out++ = static_cast<uint8_t>((codes >> 8) & 0xFF);
    *out++ = static_cast<uint8_t>(codes & 0xFF);
  }
}

// **La fila del reset**: els píxels que han de canviar reben, primer, dues empentes cap al rail del qual venen —el mateix que el nostre
// `kFastToBlack = {2, 2, 1, 1, 1, 1}`— i la resta no rep res. És el que treu el fantasma sense esborrar la pàgina (docs/50, docs/95).
void build_the_reset_row(uint16_t y, uint8_t* row) {
  const size_t at = static_cast<size_t>(y) * (kWidth / 8);
  const uint8_t* want = g_want + at;
  const uint8_t* shown = g_shown + at;
  uint8_t* out = row;
  for (uint16_t b = 0; b < kWidth / 8; ++b) {
    const uint8_t differs = static_cast<uint8_t>(want[b] ^ shown[b]);
    // El contrari del destí: el que ha d'anar a negre s'empeny cap al blanc, i al revés.
    const uint16_t codes = static_cast<uint16_t>(g_to_white[static_cast<uint8_t>(differs & want[b])] |
                                                 g_to_black[static_cast<uint8_t>(differs & ~want[b])]);
    *out++ = static_cast<uint8_t>((codes >> 8) & 0xFF);
    *out++ = static_cast<uint8_t>(codes & 0xFF);
  }
}

void build_the_frame() {
  for (uint16_t y = 0; y < kHeight; ++y) {
    build_the_row(y, g_frame + static_cast<size_t>(y) * kRowBytes);
    build_the_reset_row(y, g_reset + static_cast<size_t>(y) * kRowBytes);
  }
}

// **Un frame construït, pel bus**: one copy of 240 bytes a row from the PSRAM to the DMA's memory, and the choreography of the signals
// that a real paint does. És el mateix camí per al dibuix i per a la neteja (docs/50).
bool send_the_frame(const uint8_t* frame, bool staged, uint8_t* row) {
  row_control(true);
  for (uint16_t y = 0; y < kHeight; ++y) {
    // **El DMA llegeix d'on li diguin, i la RAM interna no necessita còpia** (0.190.133): the frame that goes out fifteen times a cycle
    // lives in internal memory, o sigui que la fila es passa tal com és. El reset, que només surt dos cops, viu a la PSRAM i es copia.
    const uint8_t* from = frame + static_cast<size_t>(y) * kRowBytes;
    if (staged) {
      memcpy(row, from, kRowBytes);
      from = row;
    }
    if (esp_lcd_panel_io_tx_color(g_io, -1, from, kRowBytes) != ESP_OK) return false;
    if (y + 1 < kHeight) row_control(false);
  }
  row_control(false);
  return true;
}

// **El frame de mesura**: 540 files de zeros pel bus, amb la mateixa coreografia de senyals que una pintada de debò. El que
// mesura és el cable i el DMA, que és la part que el nostre sistema ja paga i la que diu si 60 imatges per segon hi caben.
uint32_t send_a_frame(uint8_t* row) {
  const uint32_t started = static_cast<uint32_t>(esp_timer_get_time());
  row_control(true);
  for (uint16_t y = 0; y < kRows; ++y) {
    if (esp_lcd_panel_io_tx_color(g_io, -1, row, kRowBytes) != ESP_OK) return 0;
    if (y + 1 < kRows) row_control(false);
  }
  row_control(false);
  return static_cast<uint32_t>(esp_timer_get_time()) - started;
}

}  // namespace

extern "C" void app_main() {
  // **El monitor s'enganxa tard** (0.190.128): the first version of this returned after a second and a half, and the owner saw a single line —
  // `I (885) main_task: Returned from app_main()`— because everything the probe had said went out before the port was being read. Un banc de
  // proves no ha de tornar: espera, mesura, ho diu, i torna a començar (docs/80, docs/95).
  vTaskDelay(pdMS_TO_TICKS(2000));
  // **El watchdog, aturat**: el bucle de sota no espera mai i la tasca IDLE no ha de córrer per pintar una tinta (docs/95).
  (void)esp_task_wdt_deinit();
  ESP_LOGI(kTag, "the glass probe, and lunokOS is not here: this build is its own");
  if (!i2c_up()) {
    ESP_LOGE(kTag, "the I2C did not come up");
    return;
  }
  if (!expander_up()) {
    ESP_LOGE(kTag, "the expander did not answer at 0x%02x", kExpander);
    return;
  }
  ESP_LOGI(kTag, "the expander answers at 0x%02x", kExpander);
  if (!panel_power_on()) return;
  control_pins_up();
  if (!bus_up()) {
    ESP_LOGE(kTag, "the parallel bus did not come up");
    return;
  }
  uint8_t* row = static_cast<uint8_t*>(heap_caps_malloc(kRowBytes, MALLOC_CAP_DMA));
  if (row == nullptr) {
    ESP_LOGE(kTag, "no memory for a row");
    return;
  }
  memset(row, 0, kRowBytes);
  // **Primer el cable, mesurat** (0.190.127): what a frame costs with nothing in it, which is the number that decides whether a
  // moving picture is possible at all.
  (void)send_a_frame(row);
  uint32_t total = 0;
  for (int i = 0; i < 10; ++i) total += send_a_frame(row);
  const uint32_t mean_us = total / 10;
  ESP_LOGI(kTag, "a frame of %u rows is %u us over the wire at %u MHz: %u a second at best",
           static_cast<unsigned>(kRows), static_cast<unsigned>(mean_us), static_cast<unsigned>(kBusHz / 1000000),
           static_cast<unsigned>(mean_us == 0 ? 0 : 1000000u / mean_us));

  if (!the_picture_is_up()) {
    ESP_LOGE(kTag, "no memory for the picture (the PSRAM is not there?)");
    return;
  }
  // **I el motor, que no torna mai**: every frame the bar moves, the panel gets the codes of where every pixel has to go, and el
  // bucle cedeix el processador perquè el watchdog d'aquesta placa no es mengi la tasca (docs/95).
  ESP_LOGI(kTag, "the engine is on: a black bar walking on a white page, two pushes of reset a cycle");
  uint32_t frames = 0;
  uint32_t pictures = 0;
  uint32_t started = static_cast<uint32_t>(esp_timer_get_time());
  // **Un cicle = dos frames de neteja i quinze d'empenta** (0.190.131): the fifteen are what the ink needs to cross, and the two of white
  // are what takes away the ghost the cycle before left —el mateix reset que el `du` d'aquest projecte— (docs/50, docs/95).
  const uint32_t kCycle = 17;
  for (;;) {
    if ((frames % kCycle) == 0) {
      // **El que la tinta té és el dibuix d'abans, i el nou es compara amb això** (0.190.133): the reset and the drive of a cycle are both
      // built from the same difference, o sigui que els píxels que es mouen són els que canvien de debò i la pàgina no parpelleja.
      memcpy(g_shown, g_want, kWantBytes);
      the_bar_walks(static_cast<int>(pictures));
      ++pictures;
      build_the_frame();
      // Els dos frames de reset, per als píxels que han de moure's.
      (void)send_the_frame(g_reset, true, row);
      (void)send_the_frame(g_reset, true, row);
    }
    (void)send_the_frame(g_frame, false, row);
    ++frames;
    const uint32_t now = static_cast<uint32_t>(esp_timer_get_time());
    if ((now - started) >= 1000000u) {
      ESP_LOGI(kTag, "%u frames a second with the bar on the glass, %u pictures so far",
               static_cast<unsigned>((frames * 1000000u) / (now - started)), static_cast<unsigned>(pictures));
      frames = 0;
      started = now;
    }
    // **Sense espera, i amb el watchdog aturat a propòsit** (0.190.129): `vTaskDelay(1)` és un tick d'aquest sistema, o sigui **deu
    // mil·lisegons**, i amb el frame del cable a dotze el bucle cauria a quaranta-cinc imatges per segon per una espera que no vol
    // ningú. Un banc de proves que vol el màxim de la placa atura el watchdog de tasca i no espera: és el que es fa a l'altre costat
    // del vídeo d'una tinta (docs/95).
  }
}
