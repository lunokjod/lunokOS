// The IT8951E of the Paper: the frames, the commands, the image and the rectangle that changed
// (docs/50).
#include "esp32/it8951.h"

#include <stdio.h>
#include <string.h>

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace hal {
namespace {

// The frames, from the maker's driver (`M5EPD_Driver.cpp`): a command word is preceded by `0x6000`, a
// data word by `0x0000`, and a read by `0x1000` and one dummy word (docs/50).
const uint16_t kPrefixCommand = 0x6000;
const uint16_t kPrefixData = 0x0000;
const uint16_t kPrefixRead = 0x1000;

// The commands this driver uses. The numbers are the maker's: `M5EPD_Driver.cpp` writes these exact
// values and M5GFX's panel does the same (docs/50).
const uint16_t kCmdSysRun = 0x0001;
const uint16_t kCmdStandby = 0x0002;
const uint16_t kCmdSleep = 0x0003;
const uint16_t kCmdRegWrite = 0x0011;
const uint16_t kCmdRegRead = 0x0010;
const uint16_t kCmdGetDeviceInfo = 0x0302;
const uint16_t kCmdLoadImageArea = 0x0021;
const uint16_t kCmdLoadImageEnd = 0x0022;
const uint16_t kCmdDisplayArea = 0x0034;
// The com voltage of the glass. The maker's own init writes it with two arguments after turning the
// controller on (`M5EPD_Driver::begin`), and it is part of what must happen before the first paint
// (docs/50).
const uint16_t kCmdVcom = 0x0039;
const uint16_t kArgsVcom[2] = {0x0001, 2300};  // -2.30 V, the value the maker writes for this glass

// Two registers of the controller, **at the addresses its own defines give them** and not by analogy
// with one another (`IT8951_Defines.h`, in the maker's library, is the only place these numbers are
// written down):
//
//   LISAR    = IT8951_MCSR_BASE_ADDR (0x0200) + 0x0008 = 0x0208, low word, and 0x020A the high one
//   LUTAFSR  = IT8951_DISPLAY_REG_BASE (0x1000) + 0x0224 = 0x1224
//
// The first send reads **0x0200, which is `IT8951_MCSR`**, a status register of the memory converter:
// the image address went into it, the image landed wherever the controller had it by default, and the
// panel painted that memory as if it were ours. That is a **skewed page** on the glass and it cost a
// flash to find (measured on the board, 1/11).
const uint16_t kRegLisLow = 0x0208;
const uint16_t kRegLisHigh = 0x020A;
const uint16_t kRegLutAfsr = 0x1224;
const uint16_t kRegI80Cpcr = 0x0004;
// The first argument of the area command says how the image is written: the two pixels of a byte in
// the order the controller expects (`B_ENDIAN`), **four bits per pixel** (the value 2 at bits 7:4) and
// no rotation, because the surface is already in the panel's own frame (docs/50).
const uint16_t kPixelFormat = static_cast<uint16_t>((1 << 8) | (2 << 4));

// **10 MHz, the speed of the maker's own driver**, and slower than the 20 the bus allows on purpose:
// while the handshake does not work, the bus clock is one variable too many. It goes up when a
// `GetDeviceInfo` has come back at this speed (docs/95).
const int kSpiHz = 10000000;

// 4 bits per pixel: two pixels in a byte, the left one in the high nibble. **Unproven on the glass**:
// the maker's own driver keeps its canvas with white as zero and inverts it back before writing, so
// the convention of the panel is 15 = white, which is what this driver uses and what the first test
// on the board has to confirm (docs/95).
const uint8_t kWhite = 15;

// A block of words goes to the bus in one transaction: per-word transactions would spend more time in
// the driver than on the wire.
const uint32_t kBlockWords = 256;

// A stretch of image that goes out in one transaction. **4000 bytes is the ceiling of the bus this
// card mounted** (`sd_card.cpp` asks for `max_transfer_sz = 4000`) and the driver refuses a longer
// transaction, so a stretch stays below it. The image lives in PSRAM, which the SPI driver cannot
// hand to DMA; it copies the stretch into a DMA buffer itself, which is why this stays in one piece
// instead of staging a copy (docs/50).
const uint32_t kPixelChunkBytes = 2000;

void pin_output(int pin, int level) {
  gpio_config_t out = {};
  out.pin_bit_mask = 1ULL << pin;
  out.mode = GPIO_MODE_OUTPUT;
  out.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&out);
  gpio_set_level(static_cast<gpio_num_t>(pin), level);
}

void pin_input(int pin) {
  gpio_config_t in = {};
  in.pin_bit_mask = 1ULL << pin;
  in.mode = GPIO_MODE_INPUT;
  in.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&in);
}

}  // namespace

It8951::~It8951() {
  if (image_ != nullptr) heap_caps_free(image_);
  if (device_ != nullptr) spi_bus_remove_device(static_cast<spi_device_handle_t>(device_));
}

void It8951::power_on() {
  // The maker's own bring-up, and **the number that was missing**: `M5EPD::begin` switches the rail of
  // the panel on and then waits a whole second before the first word. The 100 ms this driver used at
  // first were not enough —the glass is still ramping, the line of BUSY reads low while it is, and the
  // handshake gave up at its very first command ("SYS_RUN did not go out", measured on the board,
  // 1/11).
  pin_output(pins_.power, 0);
  vTaskDelay(pdMS_TO_TICKS(20));
  pin_output(pins_.power, 1);
  vTaskDelay(pdMS_TO_TICKS(1000));
}

bool It8951::wait_busy(uint32_t timeout_ms) {
  const uint32_t started = static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
  // **High is ready** on this line: the maker's driver reads it that way and the panel holds it low
  // while it works (docs/50).
  while (gpio_get_level(static_cast<gpio_num_t>(pins_.busy)) == 0) {
    const uint32_t now = static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
    if ((now - started) > timeout_ms) {
      why_ = "busy stayed low";
      return false;
    }
    vTaskDelay(1);
  }
  return true;
}

bool It8951::take_bus() {
  // **Locked, not just busy**: ESP-IDF answers `ESP_ERR_INVALID_ARG` to a `CS_KEEP_ACTIVE`
  // transaction sent by a device that has not locked the bus first (`spi_device_queue_trans`, in
  // `spi_master.c` of v4.4.6). That was the whole of "bus refused the prefix": the panel was up —
  // busy read 1 — and the driver would not hold its CS down (measured on the board, 1/11).
  if (spi_device_acquire_bus(static_cast<spi_device_handle_t>(device_), portMAX_DELAY) != ESP_OK) {
    why_ = "bus did not lock";
    return false;
  }
  return true;
}

void It8951::give_bus() {
  spi_device_release_bus(static_cast<spi_device_handle_t>(device_));
}

bool It8951::word(uint16_t prefix, uint16_t value) {
  // **The pause between the prefix and the word is part of the protocol.** Sending them back to back
  // is what made `GetDeviceInfo` answer zeros: the controller takes the prefix, is busy for a moment,
  // and only then reads the word that follows (the maker's `WriteCommand` and `ReadWords` both wait
  // there, docs/50).
  spi_device_handle_t device = static_cast<spi_device_handle_t>(device_);
  if (!wait_busy(2000)) return false;
  if (!take_bus()) return false;
  uint8_t head[2] = {static_cast<uint8_t>(prefix >> 8), static_cast<uint8_t>(prefix & 0xFF)};
  spi_transaction_t first = {};
  first.length = 16;
  first.tx_buffer = head;
  first.flags = SPI_TRANS_CS_KEEP_ACTIVE;
  if (spi_device_transmit(device, &first) != ESP_OK) {
    why_ = "bus refused the prefix";
    give_bus();
    return false;
  }
  const bool ready = wait_busy(2000);
  bool sent = false;
  if (ready) {
    uint8_t body[2] = {static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value & 0xFF)};
    spi_transaction_t second = {};
    second.length = 16;
    second.tx_buffer = body;
    sent = spi_device_transmit(device, &second) == ESP_OK;
    if (!sent) why_ = "bus refused the word";
  }
  give_bus();
  return ready && sent;
}

bool It8951::write_pixels(void* handle, const uint8_t* bytes, uint32_t count) {
  if (handle == nullptr || bytes == nullptr || count == 0) return false;
  spi_device_handle_t device = static_cast<spi_device_handle_t>(handle);

  // **One preamble for the whole frame, and then the surface as it is.** This is the difference between
  // a page and a skew: the maker's `WritePartGram4bpp` puts a `0x0000` before every word, but it lifts
  // the CS around each 32-bit pair, and the controller resynchronises on that edge. With the CS held
  // down —one frame per row, which is what this driver does— a second preamble is read as two pixels,
  // the row overflows into the next and the page slides sideways (`docs/50`, measured 1/11: the first
  // page on the glass came out sheared for exactly this).
  const uint8_t preamble[2] = {static_cast<uint8_t>(kPrefixData >> 8),
                               static_cast<uint8_t>(kPrefixData & 0xFF)};
  spi_transaction_t start = {};
  start.length = 16;
  start.tx_buffer = preamble;
  start.flags = SPI_TRANS_CS_KEEP_ACTIVE;
  if (spi_device_transmit(device, &start) != ESP_OK) {
    why_ = "bus refused the data preamble";
    return false;
  }

  uint32_t done = 0;
  while (done < count) {
    uint32_t chunk = count - done;
    if (chunk > kPixelChunkBytes) chunk = kPixelChunkBytes;
    chunk &= ~1u;  // whole words of two pixels
    spi_transaction_t trans = {};
    trans.length = chunk * 8;  // the bytes of the surface, as they are
    trans.tx_buffer = bytes + done;
    trans.flags = (done + chunk < count) ? SPI_TRANS_CS_KEEP_ACTIVE : 0;
    if (spi_device_transmit(device, &trans) != ESP_OK) {
      why_ = "bus refused the image";
      return false;
    }
    done += chunk;
  }
  return true;
}

bool It8951::command(uint16_t cmd, const uint16_t* args, uint8_t count) {
  if (device_ == nullptr) return false;
  if (!word(kPrefixCommand, cmd)) return false;
  for (uint8_t i = 0; i < count; ++i) {
    if (!word(kPrefixData, args[i])) return false;
  }
  return true;
}

bool It8951::read_words(uint16_t* words, uint32_t count) {
  if (device_ == nullptr || words == nullptr || count == 0) return false;
  spi_device_handle_t device = static_cast<spi_device_handle_t>(device_);
  // The maker's `ReadWords`, word for word: the preamble, **a busy wait**, one dummy word, **another
  // busy wait**, and only then the answer. The two pauses are the protocol and not politeness (docs/50).
  if (!wait_busy(2000)) return false;
  if (!take_bus()) return false;
  if (!read_while_holding(device, words, count)) {
    give_bus();
    return false;
  }
  give_bus();
  return true;
}

bool It8951::read_while_holding(void* handle, uint16_t* words, uint32_t count) {
  spi_device_handle_t device = static_cast<spi_device_handle_t>(handle);
  uint8_t preamble[2] = {static_cast<uint8_t>(kPrefixRead >> 8),
                         static_cast<uint8_t>(kPrefixRead & 0xFF)};
  spi_transaction_t pre = {};
  pre.length = 16;
  pre.tx_buffer = preamble;
  pre.flags = SPI_TRANS_CS_KEEP_ACTIVE;
  if (spi_device_transmit(device, &pre) != ESP_OK) {
    why_ = "bus refused the read prefix";
    return false;
  }
  if (!wait_busy(2000)) return false;
  uint8_t dummy[2] = {0, 0};
  spi_transaction_t skip = {};
  skip.length = 16;
  skip.tx_buffer = dummy;
  skip.flags = SPI_TRANS_CS_KEEP_ACTIVE;
  if (spi_device_transmit(device, &skip) != ESP_OK) {
    why_ = "bus refused the dummy word";
    return false;
  }
  if (!wait_busy(2000)) return false;

  uint8_t buffer[kBlockWords * 2];
  uint32_t done = 0;
  while (done < count) {
    const uint32_t chunk = (count - done < kBlockWords) ? (count - done) : kBlockWords;
    spi_transaction_t read = {};
    read.length = chunk * 16;
    // **Receive only**: a transmit buffer here would have to be as long as the read itself, and the
    // driver would walk past a shorter one (docs/50).
    read.rx_buffer = buffer;
    read.flags = (done + chunk < count) ? SPI_TRANS_CS_KEEP_ACTIVE : 0;
    if (spi_device_transmit(device, &read) != ESP_OK) {
      why_ = "bus refused the read";
      return false;
    }
    for (uint32_t i = 0; i < chunk; ++i) {
      words[done + i] = static_cast<uint16_t>((static_cast<uint16_t>(buffer[i * 2]) << 8) |
                                              buffer[i * 2 + 1]);
    }
    done += chunk;
  }
  return true;
}

bool It8951::write_register(uint16_t reg, uint16_t value) {
  const uint16_t args[2] = {reg, value};
  return command(kCmdRegWrite, args, 2);
}

uint16_t It8951::read_register(uint16_t reg) {
  const uint16_t args[1] = {reg};
  uint16_t answer[1] = {0};
  if (!command(kCmdRegRead, args, 1)) return 0;
  if (!read_words(answer, 1)) return 0;
  return answer[0];
}

bool It8951::begin() {
  if (ready_) return true;
  power_on();
  pin_input(pins_.busy);

  // The panel joins **the bus of the card** (docs/50): the same three wires and a CS of its own. It is
  // tried **as a device first**, because a bus that is already up is the normal case —the card mounted
  // it— and asking for it again only fills the log with an error that is not one (measured on the
  // board, 1/11).
  spi_device_interface_config_t device = {};
  device.clock_speed_hz = kSpiHz;
  device.mode = 0;
  device.spics_io_num = pins_.cs;
  device.queue_size = 4;
  esp_err_t joined = spi_bus_add_device(SPI2_HOST, &device,
                                        reinterpret_cast<spi_device_handle_t*>(&device_));
  if (joined != ESP_OK) {
    spi_bus_config_t bus = {};
    bus.mosi_io_num = pins_.mosi;
    bus.miso_io_num = pins_.miso;
    bus.sclk_io_num = pins_.sck;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 8192;
    if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) == ESP_OK) bus_ = true;
    joined = spi_bus_add_device(SPI2_HOST, &device,
                                reinterpret_cast<spi_device_handle_t*>(&device_));
  }
  if (joined != ESP_OK) {
    snprintf(profile_, sizeof(profile_), "the panel did not join the bus at %d MHz", kSpiHz / 1000000);
    return false;
  }

  // The controller is told to run, and to take the image in the packed form: without this the image
  // goes over the wire word by word and the transfer triples (docs/50).
  //
  // **Every step says its own name when it fails**, instead of one line for the whole handshake: the
  // board has already shown that "no device information" can mean four different things and guessing
  // between them costs a flash each time (measured on the board, 1/11).
  if (!command(kCmdSysRun, nullptr, 0)) {
    // The level of the line as it is now, too: a busy that stays low from the first moment is the
    // panel not being powered, not a protocol that went wrong (docs/95).
    snprintf(profile_, sizeof(profile_), "SYS_RUN did not go out: %s (busy reads %d)", why_,
             gpio_get_level(static_cast<gpio_num_t>(pins_.busy)));
    return false;
  }
  if (!write_register(kRegI80Cpcr, 0x0001)) {
    snprintf(profile_, sizeof(profile_), "the pack-write register did not take: %s", why_);
    return false;
  }
  if (!command(kCmdVcom, kArgsVcom, 2)) {
    snprintf(profile_, sizeof(profile_), "the VCOM did not go out: %s", why_);
    return false;
  }

  uint16_t info[20] = {0};
  if (!command(kCmdGetDeviceInfo, nullptr, 0)) {
    snprintf(profile_, sizeof(profile_), "GetDeviceInfo did not go out: %s", why_);
    return false;
  }
  if (!read_words(info, 20)) {
    // Whatever the read left behind is printed too: `ffff` is a MISO that stays high, `0000` one that
    // stays low, and anything else is a controller answering something we did not expect (docs/95).
    snprintf(profile_, sizeof(profile_),
             "GetDeviceInfo did not come back (%s; words %04x %04x %04x %04x)", why_, info[0], info[1],
             info[2], info[3]);
    return false;
  }
  width_ = info[0];
  height_ = info[1];
  memory_ = (static_cast<uint32_t>(info[3]) << 16) | info[2];
  if (width_ == 0 || height_ == 0) {
    // A controller that answers zeros has not been read: the size of this glass is not a guess, it is
    // the panel the board carries, and a zero here would make a surface of nothing (docs/50).
    snprintf(profile_, sizeof(profile_), "the controller answered no size");
    return false;
  }

  const uint32_t bytes = static_cast<uint32_t>(width_) * (height_ / 2);
  image_ = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (image_ == nullptr) {
    snprintf(profile_, sizeof(profile_), "%ux%u but no memory for the image", width_, height_);
    return false;
  }
  memset(image_, static_cast<uint8_t>((kWhite << 4) | kWhite), bytes);
  dirty_ = false;
  ready_ = true;
  // The twenty words are in the line: with them the controller's map stops being a deduction (docs/50,
  // docs/95). The first four are the size and the image address; the rest identify the firmware.
  snprintf(profile_, sizeof(profile_), "%ux%u, image at 0x%08x, words %04x %04x %04x %04x %04x %04x",
           width_, height_, static_cast<unsigned>(memory_), info[0], info[1], info[2], info[3], info[4],
           info[5]);
  return true;
}

void It8951::mark(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  if (w == 0 || h == 0) return;
  const uint16_t x1 = static_cast<uint16_t>(x + w);
  const uint16_t y1 = static_cast<uint16_t>(y + h);
  if (!dirty_) {
    dirty_ = true;
    dirty_x0_ = x;
    dirty_y0_ = y;
    dirty_x1_ = x1;
    dirty_y1_ = y1;
    return;
  }
  if (x < dirty_x0_) dirty_x0_ = x;
  if (y < dirty_y0_) dirty_y0_ = y;
  if (x1 > dirty_x1_) dirty_x1_ = x1;
  if (y1 > dirty_y1_) dirty_y1_ = y1;
}

void It8951::clear(uint8_t level) {
  if (!ready_) return;
  const uint8_t packed = static_cast<uint8_t>(((level & 0x0F) << 4) | (level & 0x0F));
  memset(image_, packed, static_cast<uint32_t>(width_) * (height_ / 2));
  mark(0, 0, width_, height_);
}

void It8951::fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t level) {
  if (!ready_ || x >= width_ || y >= height_) return;
  if (x + w > width_) w = static_cast<uint16_t>(width_ - x);
  if (y + h > height_) h = static_cast<uint16_t>(height_ - y);
  const uint8_t packed = static_cast<uint8_t>(((level & 0x0F) << 4) | (level & 0x0F));
  // A whole pair of pixels at a time, so the two nibbles of a byte are never split between two
  // rectangles of different colours (docs/50).
  const uint16_t first = static_cast<uint16_t>(x & ~1u);
  const uint16_t last = static_cast<uint16_t>((x + w + 1) & ~1u);
  for (uint16_t row = 0; row < h; ++row) {
    uint8_t* line = image_ + static_cast<uint32_t>(y + row) * (width_ / 2);
    memset(line + first / 2, packed, static_cast<uint32_t>(last - first) / 2);
  }
  mark(first, y, static_cast<uint16_t>(last - first), h);
}

void It8951::pixel(uint16_t x, uint16_t y, uint8_t level) {
  if (!ready_ || x >= width_ || y >= height_) return;
  uint8_t& byte = image_[static_cast<uint32_t>(y) * (width_ / 2) + x / 2];
  if ((x & 1) == 0) {
    byte = static_cast<uint8_t>((byte & 0x0F) | ((level & 0x0F) << 4));
  } else {
    byte = static_cast<uint8_t>((byte & 0xF0) | (level & 0x0F));
  }
  mark(x, y, 1, 1);
}

bool It8951::flush(UpdateMode mode) {
  if (!ready_ || image_ == nullptr) return false;
  // The controller wants the area and its width in whole groups of four pixels, so a rectangle that
  // starts at 5 and is 3 wide goes as 4..8 (docs/50).
  uint16_t x0 = dirty_ ? dirty_x0_ : 0;
  uint16_t y0 = dirty_ ? dirty_y0_ : 0;
  uint16_t x1 = dirty_ ? dirty_x1_ : width_;
  uint16_t y1 = dirty_ ? dirty_y1_ : height_;
  x0 = static_cast<uint16_t>(x0 & ~3u);
  x1 = static_cast<uint16_t>((x1 + 3) & ~3u);
  if (x1 > width_) x1 = width_;
  if (y1 > height_) y1 = height_;
  const uint16_t w = static_cast<uint16_t>(x1 - x0);
  const uint16_t h = static_cast<uint16_t>(y1 - y0);
  if (w == 0 || h == 0) {
    dirty_ = false;
    return true;
  }

  const uint16_t area[5] = {kPixelFormat, x0, y0, w, h};
  // The controller is told **where** its image is before being told where to write: the two words of
  // the target address, **high and then low**, which is the order of `SetTargetMemoryAddr` in the
  // maker's driver (docs/50).
  if (!write_register(kRegLisHigh, static_cast<uint16_t>((memory_ >> 16) & 0xFFFF)) ||
      !write_register(kRegLisLow, static_cast<uint16_t>(memory_ & 0xFFFF)) ||
      !command(kCmdLoadImageArea, area, 5)) {
    return false;
  }

  // Row by row, because a row of the surface is not contiguous with the next when the rectangle does
  // not take the whole width, and **the bus is held for the whole rectangle**: locking it per row would
  // spend more time in the driver than on the wire (docs/50).
  const uint16_t row_bytes = static_cast<uint16_t>(w / 2);
  if (!take_bus()) return false;
  bool sent = true;
  for (uint16_t row = 0; row < h && sent; ++row) {
    const uint8_t* line = image_ + static_cast<uint32_t>(y0 + row) * (width_ / 2) + x0 / 2;
    sent = write_pixels(device_, line, row_bytes);
  }
  give_bus();
  if (!sent) return false;
  if (!command(kCmdLoadImageEnd, nullptr, 0)) return false;

  // Asking for the paint and **not waiting for it**: the ink takes 120 to 450 ms and the kernel has
  // work to do. `idle()` is the question for a later turn (docs/50).
  uint16_t display[7] = {x0, y0, w, h, static_cast<uint16_t>(mode),
                         static_cast<uint16_t>(memory_ & 0xFFFF),
                         static_cast<uint16_t>((memory_ >> 16) & 0xFFFF)};
  if (!command(kCmdDisplayArea, display, 7)) return false;
  dirty_ = false;
  return true;
}

bool It8951::idle() {
  // The controller says whether the last paint is over. It is asked and not waited on (docs/50).
  return ready_ && read_register(kRegLutAfsr) == 0;
}

ISurface* esp32_it8951(const It8951Pins& pins) {
  static It8951* panel = nullptr;
  // Asked **once**: a controller that does not answer is not going to answer on the next `caps()`, and
  // retrying turned one failure into a page of SPI errors on the board (measured 1/11).
  static bool asked = false;
  if (asked) return panel;
  asked = true;
  static It8951 chip(pins);
  const bool there = chip.begin();
  // Said out loud once, like the sensor and the glass: it is the only way to know what this controller
  // answered without a scope (docs/50, docs/95).
  printf("panel: %s%s\n", chip.profile(), there ? "" : " (no panel)");
  if (there) panel = &chip;
  return panel;
}

}  // namespace hal
