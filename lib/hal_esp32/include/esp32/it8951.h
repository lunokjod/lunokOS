// The panel of the M5Paper: a 960x540 e-ink glass with an **IT8951E** in front of it (docs/50).
//
// The controller is what makes this board different from the parallel panels of FastEPD or EPDiy:
// the waveform and the VCOM live **in the controller's own flash**, it has 1 MB of memory of its own
// where the image sits, and the SoC only has to push bits over SPI and ask for a rectangle to be
// painted. What can be made fast here is the **transfer** —20 MHz, which is the ceiling of a
// full-duplex bus through the GPIO matrix on these pins, and the dirty rectangle that keeps a page
// small— because the glass takes 120 to 450 ms to move its ink whatever we do (docs/50).
//
// Everything below comes from the maker's own driver for this board (`M5EPD_Driver.cpp`) and from
// M5GFX's `Panel_IT8951`, which agree on the frames and the commands, and from the update-mode table
// that M5GFX carries (docs/50).
//
// **What is not proven yet, and is said here**: the packing of a 4-bit pixel and the order of the two
// pixels of a byte are the maker's, but the board has not drawn a pixel yet. The first thing to check
// on the glass is a `clear(white)` followed by one `flush(kInit)`: if the page comes out black, the
// levels are the other way round and it is one constant (docs/95).
#ifndef LUNOK_ESP32_IT8951_H
#define LUNOK_ESP32_IT8951_H

#include <stdint.h>

#include "hal/hal.h"

namespace hal {

// The wires of the panel, from the maker's pin map. The four of them are **the card's own wires**: the
// panel joins the bus the card uses and does not own one (docs/50).
struct It8951Pins {
  int cs;
  int sck;
  int mosi;
  int miso;
  int busy;
  // The switched supply of the panel. High powers it, and the maker leaves a **whole second** between
  // the rail going up and the first word, because the glass is still ramping (docs/50).
  int power;
};

// The panel as a surface: a 4-bit image in the board's own memory, a rectangle that remembers what
// changed, and a `flush` that sends **only that rectangle** to the controller (docs/50).
class It8951 : public ISurface {
 public:
  explicit It8951(const It8951Pins& pins) : pins_(pins) {}
  ~It8951() override;

  // Powers the panel, waits for it, asks it who it is (`GetDeviceInfo`: its memory and its size) and
  // writes the VCOM the maker's driver writes for this glass (docs/50).
  bool begin();
  bool ready() const { return ready_; }
  uint16_t vcom() const { return vcom_; }
  // What the controller answered, for the boot line: size, memory and VCOM.
  const char* profile() const { return profile_; }

  uint16_t width() const override { return width_; }
  uint16_t height() const override { return height_; }
  void clear(uint8_t level) override;
  void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t level) override;
  void pixel(uint16_t x, uint16_t y, uint8_t level) override;
  void blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t* packed,
            uint16_t stride) override;
  // Sends what changed and asks for it to be painted. It returns **before the glass has finished**:
  // the ink takes its time and the kernel cannot wait for it. `idle()` is how a later turn asks
  // whether the panel is done (docs/50).
  bool flush(UpdateMode mode) override;
  // **A paint is not over before it has had time to start.** The engine does not mark itself busy in the
  // same instant the paint is asked for, so a question asked a millisecond later reads "nothing in
  // flight" while the ink has not begun (measured on the board, 1/11: `ui.idle()` said 0 ms with the
  // page still moving). `idle` and the wait for a previous paint both answer with that in mind (docs/50).
  bool idle() override;
  // **The turn between the frame an app draws in and the frame of the glass**, in software: the image
  // this driver keeps is in the app's frame and the pixels are turned as they are sent, which is what
  // lets a 540-wide page live on a 960-wide glass without the controller having an opinion (docs/50).
  bool set_rotation(uint16_t degrees) override;
  uint16_t rotation() const override { return rotation_; }
  // **Whether a partial paint may start where the change is** (0.179.16): **yes**, which is what the paper project has
  // been doing on this panel for weeks (`src/plat/m5epd_hal.cpp` in that repository: `LD_IMG_AREA` and `UpdateArea`
  // with the real rectangle and nothing but the rounding to four). The rule that forced the left edge came from a
  // night with another fault in it (0.118.7 read on 0.118.6, with 0.118.9 waiting), and it costs a column of the page
  // for every partial paint (docs/50).
  bool partial_from_the_box() const override { return from_box_; }
  bool set_partial_from_the_box(bool on) override;
  // Why the last step that failed failed, for the console: a paint that is refused without a reason is
  // a flash to find out which of five things it was (docs/50).
  const char* last_error() const override { return why_; }
  // **The panel's own sleep, and the last word it is given** (0.183.0): `IT8951_TCON_SLEEP`, which is what the maker's
  // `M5EPD_Driver::Sleep` writes and what its shutdown does before the supplies go. The controller stops answering
  // until a boot writes `SYS_RUN` again, so a caller paints nothing after it (docs/50).
  bool rest() override;
  // Waits —with a ceiling— until the paint engine of the controller has nothing in flight. A present
  // with two qualities is two paints and the second one cannot start on top of the first (docs/50).
  bool wait_paint(uint32_t timeout_ms);

 private:
  // A prefix and its word, with the busy line looked at in between: the controller needs that pause
  // and the maker's driver takes it, one cycle per word (docs/50).
  bool word(uint16_t prefix, uint16_t value);
  // **The CS stays low between two transactions only while the bus is locked**, and ESP-IDF refuses
  // a `CS_KEEP_ACTIVE` transaction otherwise (docs/50).
  bool take_bus();
  void give_bus();
  bool command(uint16_t cmd, const uint16_t* args, uint8_t count);
  // Image data, with the bus already locked: **one** data preamble and then the bytes of the surface
  // as they are, with the CS held for the whole stretch. The maker's driver can put a preamble before
  // every word because it lifts the CS around each one and the controller resynchronises there; inside
  // a single frame the extra preambles are read as pixels and the page comes out sheared (docs/50).
  bool write_pixels(void* device, const uint8_t* bytes, uint32_t count);
  bool read_words(uint16_t* words, uint32_t count);
  // The read itself, with the bus already locked by the caller (docs/50).
  bool read_while_holding(void* device, uint16_t* words, uint32_t count);
  bool write_register(uint16_t reg, uint16_t value);
  // **Whether the register was read at all** (0.151.8): a failed read is not a value, and a caller that cannot tell them
  // apart cuts the rail of a panel that is only busy (docs/50).
  bool read_register(uint16_t reg, uint16_t& value);
  bool wait_busy(uint32_t timeout_ms);
  // Whether the engine of the controller has nothing in flight **and the last paint has had the time to
  // start**: the one question `idle()` and `wait_paint` both ask (docs/50).
  bool engine_over();
  void power_on();
  // **Joining the bus and arming the controller are two steps and not one** (0.118.9): the first one
  // happens once in the life of the driver, and the second one is what a panel that has stopped
  // answering needs again, over the same device and without being added to the bus twice (docs/50).
  bool join_bus();
  bool arm();
  // **The one way back for a controller that answers `ffff`**: the rail down and up, the handshake
  // again, and the whole image sent to a memory that came up empty. It keeps the page an app drew,
  // because the image belongs to the app and not to the controller (0.118.9, docs/95).
  bool revive();
  // How many times in a row it has been tried and failed, and whether the driver has given up on it.
  // Whether the last wait saw the panel answer nothing at all (`ffff`), which is the one thing a rail cycle is for.
  bool silent_ = false;
  // **When the last paint of ours went out** (0.151.7): a `LUTAFSR` read a moment after it is a zero that means nothing yet
  // —the engine has not marked itself busy—, and it is the only thing this number is for (docs/50).
  uint32_t painted_ms_ = 0;
  // Whether the register has been asked once for this paint, so the one line that says its value and its age is not
  // repeated in every poll of the wait (0.151.8).
  bool asked_ = false;
  // **How long a page of this glass takes**, measured by this driver on this board: 1051 ms and 1052 ms, twice, on 8/11
  // (the splash page and the one after it). It is the floor the paint after a page is waited for, so a page that arrives a
  // millisecond after another one cannot load its image into an engine that is still reading the one before (0.151.8).
  uint32_t measured_ms_ = 1050;
  // Whether the paint being waited for covered half the glass or more, which is what makes it a page (0.151.8).
  bool last_wide_ = false;
  // **Whether the glass takes a rectangle that starts where the change is** (0.179.16): **true**, the box of the change
  // and nothing more, which is what the paper project proves this controller takes; `panel edge` puts back the left
  // edge of the image, the rule of the 0.118.7, and it is the way back if a paint ever shows nothing.
  bool from_box_ = true;
  // **The way back for a controller that answers `ffff` and for nothing else** (0.151.3).
  bool bring_back();
  uint8_t revive_fails_ = 0;
  bool dead_ = false;
  // The rectangle the app drew, in the frame of the glass it belongs to (docs/50).
  void logical_to_native(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t& nx0,
                         uint16_t& nx1, uint16_t& ny0, uint16_t& ny1) const;
  // One row of the glass, packed, taken from the image an app drew in.
  void gather_row(uint8_t* out, uint16_t nx0, uint16_t ny, uint16_t w) const;
  // Makes the image again for the turn it has now, and marks the whole of it to be sent.
  bool allocate_image();
  void mark(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

  uint8_t* image_ = nullptr;
  It8951Pins pins_;
  bool ready_ = false;
  // **Whether the controller has been told to sleep** (0.183.0): the last word it is given, and a paint asked after it is
  // a paint that would have gone to a controller that answers nothing. It is refused with the reason instead of being
  // sent and lost (docs/50).
  bool resting_ = false;
  bool bus_ = false;
  void* device_ = nullptr;  // spi_device_handle_t, kept out of the header
  uint32_t memory_ = 0;     // where the controller keeps the image, from `GetDeviceInfo`
  // The frame of the glass itself, from `GetDeviceInfo`, and the frame an app draws in, which is the same
  // one turned by `rotation_` (docs/50).
  uint16_t native_width_ = 0;
  uint16_t native_height_ = 0;
  uint16_t rotation_ = 270;  // the driver starts where the system's default page is, so the image is
                            // made once at boot (docs/50)
  uint16_t width_ = 0;
  uint16_t height_ = 0;
  uint16_t vcom_ = 0;
  // What changed since the last flush. A panel that is sent the whole frame every time spends 100 ms
  // of SPI on a page where only a clock moved (docs/50).
  uint16_t dirty_x0_ = 0;
  uint16_t dirty_y0_ = 0;
  uint16_t dirty_x1_ = 0;
  uint16_t dirty_y1_ = 0;
  bool dirty_ = false;
  // Why the last step that failed did fail. The handshake already reported four different faults
  // under one sentence and each one cost a flash to tell apart (measured on the board, 1/11).
  const char* why_ = "";
  // Room for the longest handshake line, size and words included (measured: 87 characters).
  char profile_[96] = {0};
};

// The panel of this board, or nullptr when the controller does not answer (docs/50).
ISurface* esp32_it8951(const It8951Pins& pins);

}  // namespace hal

#endif  // LUNOK_ESP32_IT8951_H
