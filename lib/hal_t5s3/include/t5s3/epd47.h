// **El vidre d'aquesta placa** (docs/50): an ED047TC1 of 4.7 inches, 960 × 540 and sixteen levels, and the thing that
// makes this board different from the Paper is that **there is no controller between the chip and the panel**. On the
// Paper an IT8951 is told «paint this rectangle with this mode» and it does; here the chip has to clock every row,
// every phase of the panel's waveform, itself.
//
// What that means for the system, said once: an update is **`kPhases` scans of the whole glass** —thirty-eight—, o
// sigui que refrescar costa segons i no mil·lisegons, i la pàgina es pinta quan ha de quedar quieta i no a cada tecla
// (docs/50, docs/95). És el preu de no tenir controlador, i no una decisió d'aquest driver.
//
// **La forma d'ona és de l'ED047TC1 i no meva** (0.190.33): `epd47_waveform.h`, amb la seva atribució (FastEPD,
// Apache-2.0, BitBank Software). Here is the rest: the bus, the supply and the frame.
#ifndef LUNOK_T5S3_EPD47_H
#define LUNOK_T5S3_EPD47_H

#include <stdint.h>

#include "hal/hal.h"
#include "t5s3/epd47_waveform.h"
#include "t5s3/expander.h"

namespace hal {

// The driver of the panel, built once: it owns the parallel bus, the frame and the supply sequence of the TPS65185
// (docs/50).
class Epd47 : public ISurface {
 public:
  // **Els dos xips que el panell necessita**: the expander that opens its gates and the bus the TPS65185 answers on —
  // the same two wires as the clock chip and the touch controller (docs/50).
  Epd47(Pca9535* expander, II2c* bus);

  // **The panel, brought up**: the bus, its pins and the frame in the PSRAM. False is a panel that is not there, and
  // `last_error()` says which of them it was —a board whose BUS does not come up must be told apart from one whose
  // PSRAM has no room, because they are two different repairs—. **Les tensions no són aquí**: l'alimentació es demana
  // a cada pintada i el que la contesta és el `flush` (docs/50).
  bool begin();

  uint16_t width() const override;
  uint16_t height() const override;
  void clear(uint8_t level) override;
  void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t level) override;
  void pixel(uint16_t x, uint16_t y, uint8_t level) override;
  void blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t* packed, uint16_t stride) override;

  // **One update, and the whole glass**: the panel has no partial engine, so a paint is every row and every phase
  // (docs/50, docs/95).
  bool flush(UpdateMode mode) override;
  bool idle() override { return !busy_; }

  // **The panel goes to rest** (0.183.0): the rails of the TPS are taken down after the last paint of a way down, and
  // the image stays because it is ink (docs/50).
  bool rest() override;

  // Which way the page lies on the glass: the glass is landscape and the page is a portrait page of 540 × 960, o sigui
  // que el gir és del driver i no del nucli. 90 and 270 are the two that fit (docs/50).
  bool set_rotation(uint16_t degrees) override;
  uint16_t rotation() const override { return rotation_; }

  // The bus of this panel is the parallel one and it is not an SPI: what the kernel prints about the wire is the clock
  // of the i80 peripheral (docs/50).
  uint32_t bus_mhz() const override;
  // **I es canvia en calent** (0.190.38): a row is 240 bytes of cable and the cable is the largest piece of a paint, so
  // the speed of the wire is the one knob worth measuring on the board and not by flashing —`panel bus 26` at the prompt
  // is the whole test—. The peripheral is created again with the new clock and the frame, the panel and the supply stay
  // where they are; a speed this chip cannot give is refused and never rounded (docs/50, docs/80).
  bool set_bus_mhz(uint32_t mhz) override;
  // **I la finestra de la porta, en microsegons** (0.190.49): el número que decideix quants grisos separa aquest vidre, i es
  // mesura en calent perquè cada valor és una pintada de prova (docs/50, docs/95).
  uint32_t gate_us() const override { return gate_us_; }
  bool set_gate_us(uint32_t us) override;
  const char* last_error() const override { return error_; }

 private:
  // **The frame lives in the glass's own frame** (960 × 540, four bits a pixel, 259 200 B in the PSRAM): the page is
  // turned into it once per write, and the update is then a straight walk of its rows, which is the hot path (docs/50).
  static const uint16_t kGlassW = 960;
  static const uint16_t kGlassH = 540;
  static const uint16_t kGlassStride = kGlassW / 2;
  // The page an app draws in: a portrait page on a landscape glass (docs/50).
  static const uint16_t kPageW = 540;
  static const uint16_t kPageH = 960;

  void to_glass(uint16_t px, uint16_t py, uint16_t& gx, uint16_t& gy) const;
  void put(uint16_t gx, uint16_t gy, uint8_t level);
  uint8_t get(uint16_t gx, uint16_t gy) const;
  // **El rectangle que ha canviat, en coordenades de pàgina** (0.190.37): the contract of `ISurface` is that a flush sends
  // the rectangle the writes marked and forgets it, i en un vidre que costa dos segons i mig per passada això no és un
  // detall. Les cantonades són totes dues dins del rectangle i `nothing_dirty()` és una pintada que no surt (docs/50).
  void mark(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
  bool nothing_dirty() const { return dirty_x1_ < dirty_x0_ || dirty_y1_ < dirty_y0_; }
  void forget_the_dirty();
  // **El rectangle marcat, en coordenades del vidre** (0.190.40): the page is turned, o sigui que el que a la pàgina és una
  // caixa és una caixa al vidre, però les files no són les de la pàgina i s'han de treure de les quatre cantonades. El que
  // mana una pintada és aquest rectangle: **les files de fora no surten i, dins d'una fila, només es dona codi a les
  // columnes de dins**. El que queda a zero és el codi que no fa res (docs/50, docs/95).
  void dirty_glass(uint16_t& gx0, uint16_t& gy0, uint16_t& gx1, uint16_t& gy1) const;

  // The supply of the panel, in the order the maker's own driver gives it (TPS65185 + PCA9535).
  bool power(bool on);
  // **Les portes de tornada a baix**, and it is not the error path's private business: a supply that came halfway up
  // is closed with the same writes as one that came all the way (0.190.34).
  void gates_down();
  // **Un passatge sencer**: the STV start, the rows of the marked rectangle with the gate advancing one line at a time,
  // and the latch of the last one. `upper`/`lower` are the two halves of the table for this phase, and a null pair is the
  // discharge pass, que és la que deixa els drivers del panell en repòs (docs/50).
  // **El que fa una passada, i sobre quins píxels**: `only_changed` is the difference engine of the maker's own driver
  // (`pPrevious` in FastEPD): a pixel whose level is the same as the frame that is already on the glass is given the code
  // that does nothing, o sigui que la seva tinta no es mou (docs/50, docs/95).
  // `codes` is four tables of sixteen —one per corner of the 2×2 dither— and a null pointer is the discharge pass.
  bool scan_a_pass(uint16_t first, uint16_t last, uint16_t b0, uint16_t b1, bool only_changed, const uint8_t* codes);
  // **El bus i el perifèric, oberts i tornats a obrir** quan canvia el rellotge (0.190.38 → 0.190.44): the clock is state
  // of the bus, o sigui que els dos es fan de nou i en aquest ordre.
  bool open_the_bus();
  bool open_the_io();
  // The four edges of a row: `start` opens a pass, and the step latches a row that has just been clocked.
  void row_control(bool start);
  // **Una fila es llança i es recull en dos temps** (0.190.40): `start_row` hands the row to the peripheral and
  // `wait_row` waits for the one that was already travelling, o sigui que la fila següent es construeix mentre l'anterior
  // va pel cable i el que queda és el més gran dels dos temps i no la seva suma (docs/50, docs/95).
  bool start_row(const uint8_t* row);
  bool wait_row();
  bool update_the_glass(uint16_t first, uint16_t last, uint16_t b0, uint16_t b1, bool fast, bool only_changed);

  Pca9535* expander_;
  II2c* bus_;
  void* bus_handle_;
  void* io_;
  uint8_t* bits_;
  uint8_t* row_;
  uint8_t* row2_;
  // **El quadre que ja és al vidre** (0.190.41): 259 200 B more of PSRAM for the whole point of a partial update —without
  // it a paint cannot know which pixels are already right, i el que fa és empenyeu tots— (docs/50, docs/95).
  uint8_t* previous_;
  bool previous_is_known_ = false;
  volatile bool dma_done_;
  bool started_;
  bool powered_;
  bool busy_;
  // The rectangle the writes marked, in page coordinates and both corners inside it (0.190.37).
  uint16_t dirty_x0_ = 0xFFFF;
  uint16_t dirty_y0_ = 0xFFFF;
  uint16_t dirty_x1_ = 0;
  uint16_t dirty_y1_ = 0;
  uint16_t rotation_ = 270;
  // **Vint megahertz, que és el que la taula del fabricant dona per aquest panell** i el que fa servir el seu firmware, i
  // es pot canviar en calent des del prompt (`panel bus 26`) (0.190.38, docs/50).
  uint32_t pclk_hz_ = 20000000;
  // **Sis microsegons de dotze**, que és el que epdiy dona a aquest mateix panell (`ckv_high_time = 60`, en dècimes).
  uint32_t gate_us_ = epd47::kRefGateUs;
  // **I les passades que la porta d'aquest moment demana** (0.190.50): la càrrega que mou la tinta és porta per temps, o sigui
  // que baixar la porta sense apujar les passades treu contrast en lloc de guanyar nivells. Es tornen a comptar a cada canvi de
  // `gate_us_` i mai no són zero (docs/50, docs/95).
  uint8_t black_passes_ = epd47::kRefPasses;
  void scale_the_passes();
  // **El que ha costat la pintada, partit en dos** (0.190.38): `bus_us_` is the wire and the peripheral —the row that has
  // gone out— and `cpu_us_` is the turning of the frame into codes, which is the only part a faster processor or a
  // different loop can take away (docs/95).
  uint32_t bus_us_ = 0;
  uint32_t cpu_us_ = 0;
  // Whether a row is still travelling through the peripheral (0.190.40).
  bool pending_ = false;
  const char* error_ = "";
};

// The panel of this board, built once (docs/50).
ISurface* t5s3_epd47(Pca9535* expander, II2c* bus);

}  // namespace hal

#endif  // LUNOK_T5S3_EPD47_H
