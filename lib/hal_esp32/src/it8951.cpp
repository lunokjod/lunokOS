// The IT8951E of the Paper: the frames, the commands, the image and the rectangle that changed
// (docs/50).
#include "hal/log.h"
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

// The bytes of a row of the glass: 960 pixels at four bits each. The `flush` gathers a row here and sends
// it, so this is the largest row the panel is expected to have (docs/50).
const uint16_t kMaxRowBytes = 512;

// A block of words goes to the bus in one transaction: per-word transactions would spend more time in
// the driver than on the wire.
const uint32_t kBlockWords = 256;

// A stretch of image that goes out in one transaction. **4000 bytes is the ceiling of the bus this
// card mounted** (`sd_card.cpp` asks for `max_transfer_sz = 4000`) and the driver refuses a longer
// transaction, so a stretch stays below it. The image lives in PSRAM, which the SPI driver cannot
// hand to DMA; it copies the stretch into a DMA buffer itself, which is why this stays in one piece
// instead of staging a copy (docs/50).
const uint32_t kPixelChunkBytes = 2000;

// How long a paint may take before the driver says so: the maker's own driver gives it three seconds,
// which is more than the two of a `init` (docs/50).
const uint32_t kPaintTimeoutMs = 3000;

// **A zero in `LUTAFSR` is not an answer until the engine has had time to mark itself busy** (0.151.7, the guard of
// 0.19.2 on the register instead of on the BUSY pin). The board read the register as zero a millisecond after a
// `DPY_AREA` —`engine status 0000 after the paint`— with more than a second of ink still to come, and a paint that
// believes that zero sends the next page on top of a running engine. It is a floor and not a measurement: nobody has
// timed how long the engine takes to say it is busy, and it can only make the answer later (docs/50).
const uint32_t kEngineStartMs = 50;

// **How many times a panel that stopped answering is brought back before the driver gives up on it**
// (0.118.9). Three and not one, because the rail cycle of a `power_on` takes a second of the driver's
// time and a controller that is deaf for a moment is not a controller that is gone; and not ten,
// because a panel that has not come back in three seconds of rail is not coming back (docs/95).
const uint8_t kReviveTries = 3;

// **How long the line of busy may stay low before a word is given up on**, which is the maker's own
// three seconds and not two: while a paint of `init` runs, the controller holds that line low for two
// whole seconds, and a command that arrives in the middle of it must wait rather than fail
// (measured on the board, 1/11: `busy stayed low` right after the boot) (docs/50).
const uint32_t kBusyTimeoutMs = 3000;

// **The biggest glass this driver will believe** (0.151.2, docs/95): a device that answers `ffff` is a
// MISO nobody drives, and read as a size it is a glass of 65535 by 65535 with its image at 0xffffffff.
// Nothing on this board is wider than four thousand pixels, and a geometry past this is a controller that
// is not there (measured on the board, 8/11).
const uint16_t kMaxGlassSide = 4096;

// **How many more times a `ffff` is asked about before the rail is cycled, and how long apart** (0.151.3). A controller
// that is running a paint and one that is gone answer the same thing on MISO, and the answer to the second —cut the
// power— is what kills the first one for good. Two questions, a quarter of a second each, and a panel that is only
// busy is back (measured on the board, 8/11, docs/50).
const uint8_t kSilentTries = 2;
const uint32_t kSilentPauseMs = 250;

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
  if (!wait_busy(kBusyTimeoutMs)) return false;
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
  const bool ready = wait_busy(kBusyTimeoutMs);
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
  if (!wait_busy(kBusyTimeoutMs)) return false;
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
  if (!wait_busy(kBusyTimeoutMs)) return false;
  uint8_t dummy[2] = {0, 0};
  spi_transaction_t skip = {};
  skip.length = 16;
  skip.tx_buffer = dummy;
  skip.flags = SPI_TRANS_CS_KEEP_ACTIVE;
  if (spi_device_transmit(device, &skip) != ESP_OK) {
    why_ = "bus refused the dummy word";
    return false;
  }
  if (!wait_busy(kBusyTimeoutMs)) return false;

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

// **A read that did not happen is not a value** (0.151.8). This function used to answer with a number whatever happened,
// and its callers had two questions to tell apart —"the engine is idle" and "the panel is not answering"— with one
// answer: a failed read was `ffff`, `ffff` was read as silence, and silence was answered by **cutting the rail of the
// panel**. That is how a bus that was merely busy ended up killing the glass (measured 8/11). Now the caller is told
// whether there is a value at all, and `why_` says why there is not (docs/50).
bool It8951::read_register(uint16_t reg, uint16_t& value) {
  const uint16_t args[1] = {reg};
  uint16_t answer[1] = {0};
  if (!command(kCmdRegRead, args, 1)) return false;
  if (!read_words(answer, 1)) return false;
  value = answer[0];
  return true;
}

bool It8951::begin() {
  if (ready_) return true;
  power_on();
  pin_input(pins_.busy);
  if (!join_bus()) return false;
  return arm();
}

bool It8951::join_bus() {
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
  return true;
}

bool It8951::arm() {
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
  // **All ones is a MISO nobody drives, and it was taken for a panel that had come back** (0.151.2). The log of the
  // board says it word for word: after the glass stopped answering, `revive()` cycled the rail, `read_words` came back
  // "well", and this function believed `glass 65535x65535` with its image at `0xffffffff` — `arm()` returned true,
  // `ready_` stayed up, and the driver went on loading every page into an address nobody has, with the paint accepted
  // and the glass never changed. That is the frozen board of the reader and of the splash: *«no pinta res de res al
  // eink i després ningú pot tocar la pantalla fins al reinici»* (measured on the board, 8/11, docs/50, docs/95).
  // A controller that is not on the bus has to say so here, so that the rail is tried again and the driver stops
  // painting where nothing is listening. **The four words are checked before they are believed**: what is read is a
  // proposal, and the geometry of this panel and the address of its image only become the driver's when they are a
  // panel at all.
  const uint16_t glass_w = info[0];
  const uint16_t glass_h = info[1];
  const uint32_t image = (static_cast<uint32_t>(info[3]) << 16) | info[2];
  if ((glass_w == 0xFFFF && glass_h == 0xFFFF) || glass_w == 0 || glass_h == 0 || glass_w > kMaxGlassSide ||
      glass_h > kMaxGlassSide || image == 0xFFFFFFFFu) {
    // One sentence for the two readings that mean "no panel": all ones (nobody drives the line) and zeros (the
    // controller answered nothing). The rest of the words are printed because they are the only fingerprint left.
    snprintf(profile_, sizeof(profile_), "no panel: words %04x %04x %04x %04x, %ux%u at %08x", info[0], info[1],
             info[2], info[3], glass_w, glass_h, static_cast<unsigned>(image));
    return false;
  }
  native_width_ = glass_w;
  native_height_ = glass_h;
  memory_ = image;
  // The image is made **in the frame an app draws in**, which is the glass turned by `rotation_` —90 by
  // default, so the page is a portrait one on a landscape glass (docs/50). **It is made once**: a panel
  // that is armed again keeps the page an app drew, because an arming that threw it away would leave
  // the glass blank every time the controller came back (0.118.9).
  if (image_ == nullptr && !allocate_image()) return false;
  ready_ = true;
  // The twenty words are in the line: with them the controller's map stops being a deduction (docs/50,
  // docs/95). The first four are the size and the image address; the rest identify the firmware.
  snprintf(profile_, sizeof(profile_),
           "%ux%u turned %u (glass %ux%u), image at 0x%08x, words %04x %04x %04x %04x",
           width_, height_, static_cast<unsigned>(rotation_), native_width_, native_height_,
           static_cast<unsigned>(memory_), info[0], info[1], info[2], info[3]);
  return true;
}

// **A controller that has stopped answering is brought back with its supply, and not with a command**
// (0.118.9). The board said the shape of it (7/11): the logo of the boot goes up, and from that moment
// `LUTAFSR` reads `ffff` —a MISO nobody drives— for ever, while every `DPY_AREA` is accepted and the
// glass keeps the page it had. A panel that is not on the bus does not take a command, so the one
// handle this driver has on it is the rail: down, up, and the second the maker waits for the glass to
// ramp. What the controller comes back with is a memory that knows nothing of the page, so the whole
// image goes to it again and not the rectangle that changed (docs/50, docs/95).
bool It8951::revive() {
  const bool was_ready = ready_;
  ready_ = false;
  // The bus stays where it is: the device does not need adding again, only the controller does.
  power_on();
  const bool back = arm();
  // A failure here must not make the driver mute for the rest of the session: the sentence is said in
  // the log and the next paint asks again, because a panel that is deaf for a moment is not a panel
  // that is gone (docs/95).
  ready_ = was_ready;
  if (!back) return false;
  dirty_ = false;
  mark(0, 0, width_, height_);
  // A panel that has just been re-armed has no paint of ours in flight (0.151.7).
  painted_ms_ = 0;
  last_wide_ = false;
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
  // **The two ends are written a nibble at a time.** Two pixels share a byte, so a rectangle that
  // starts or ends at an odd pixel splits one; painting the whole byte painted the pixel next to it,
  // which belongs to whatever was drawn there before —the bar draws its glyphs this way, one run at a
  // time— (docs/50).
  const uint16_t end = static_cast<uint16_t>(x + w);
  for (uint16_t row = 0; row < h; ++row) {
    uint8_t* line = image_ + static_cast<uint32_t>(y + row) * (width_ / 2);
    uint16_t at = x;
    if ((at & 1) != 0) {
      line[at / 2] = static_cast<uint8_t>((line[at / 2] & 0xF0) | (level & 0x0F));
      ++at;
    }
    const uint16_t whole = static_cast<uint16_t>((end - at) / 2);
    if (whole > 0) memset(line + at / 2, packed, whole);
    at = static_cast<uint16_t>(at + whole * 2);
    if (at < end) line[at / 2] = static_cast<uint8_t>((line[at / 2] & 0x0F) | ((level & 0x0F) << 4));
  }
  mark(x, y, w, h);
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

void It8951::blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t* packed,
                  uint16_t stride) {
  if (!ready_ || packed == nullptr || image_ == nullptr || w == 0 || h == 0) return;
  if (x >= width_ || y >= height_) return;
  if (x + w > width_) w = static_cast<uint16_t>(width_ - x);
  if (y + h > height_) h = static_cast<uint16_t>(height_ - y);
  const uint32_t panel_stride = width_ / 2;
  if ((x & 1) != 0) {
    // An odd x puts the two pixels of a source byte in two different bytes of the panel, so this goes
    // pixel by pixel. **The source is indexed by its own column**: `packed` starts at the first pixel
    // of the region, and using the panel's x here read a byte that belonged to another part of the
    // image (found while reviewing the path, 1/11).
    for (uint16_t row = 0; row < h; ++row) {
      const uint8_t* line = packed + static_cast<uint32_t>(row) * stride;
      for (uint16_t column = 0; column < w; ++column) {
        const uint8_t byte = line[column / 2];
        const uint8_t level = ((column & 1) == 0) ? (byte >> 4) : (byte & 0x0F);
        pixel(static_cast<uint16_t>(x + column), static_cast<uint16_t>(y + row), level);
      }
    }
    return;
  }
  // Byte for byte: the packing of the source is the packing of the glass (docs/50).
  const uint16_t bytes = static_cast<uint16_t>(w / 2);
  for (uint16_t row = 0; row < h; ++row) {
    uint8_t* to = image_ + static_cast<uint32_t>(y + row) * panel_stride + x / 2;
    memcpy(to, packed + static_cast<uint32_t>(row) * stride, bytes);
  }
  mark(x, y, static_cast<uint16_t>(w & ~1u), h);
  if ((w & 1) != 0) mark(static_cast<uint16_t>(x + w - 1), y, 1, h);
  hal::logf(pal::kLogDebug, "PANEL", "blit %u,%u %ux%u of a line of %u B", static_cast<unsigned>(x),
            static_cast<unsigned>(y), static_cast<unsigned>(w), static_cast<unsigned>(h),
            static_cast<unsigned>(stride));
}

// **The way back for a controller that answers `ffff`, and only for that** (0.151.3): a panel that is busy and one that
// is gone answer the same thing on MISO, and cutting the power of the first is what left the board with a glass nobody
// could paint. So it is asked twice more, a quarter of a second apart, and only a panel that is still silent is brought
// back with its rail (docs/50).
bool It8951::bring_back() {
  if (dead_) {
    why_ = "the panel does not answer";
    return false;
  }
  uint16_t status = 0;
  bool read = read_register(kRegLutAfsr, status);
  for (uint8_t again = 0; again < kSilentTries && read && status == 0xFFFF; ++again) {
    vTaskDelay(pdMS_TO_TICKS(kSilentPauseMs));
    read = read_register(kRegLutAfsr, status);
  }
  // **A read that failed is not a panel that is gone**: nothing is cycled for it, the paint is simply refused and the
  // next one asks again (0.151.8).
  if (!read || status != 0xFFFF) return true;
  if (!revive()) {
    ++revive_fails_;
    if (revive_fails_ >= kReviveTries) {
      dead_ = true;
      hal::logf(pal::kLogWarn, "PANEL", "the panel stopped answering and did not come back after %u tries",
                static_cast<unsigned>(revive_fails_));
    }
    why_ = "the panel stopped answering";
    return false;
  }
  revive_fails_ = 0;
  hal::logf(pal::kLogWarn, "PANEL", "the panel had stopped answering; the rail was cycled and it is back: %s", profile_);
  uint16_t after = 0;
  if (!read_register(kRegLutAfsr, after)) return true;  // it answered a question, and that is a panel that is there
  return after != 0xFFFF;
}

bool It8951::flush(UpdateMode mode) {
  // **A panel that has stopped answering is not asked again, and above all it is not waited for** (0.151.5): the wait of
  // a paint is up to three seconds, and a glass that is gone was costing the whole system three seconds of every turn
  // —the board ran, and the shell crawled— while the answer could not change (measured on the board, 8/11).
  if (!ready_ || image_ == nullptr) {
    why_ = "the panel is not up";
    return false;
  }
  if (dead_) {
    why_ = "the panel does not answer";
    return false;
  }
  // **What is sent is what changed, and nothing else.** With nothing marked there is nothing to paint and
  // the whole page is left alone: a page nobody asked to be repainted is exactly what this system must not
  // do (docs/30, docs/50). And it is asked **first**, because a present with no damage must not wait for the ink of
  // somebody else's page (0.151.3).
  if (!dirty_) {
    // Nothing was marked, so nothing is sent: it is said because a page that nobody painted and a page with no damage
    // look the same on the glass (0.118.6, docs/50).
    hal::logf(pal::kLogDebug, "PANEL", "a paint was asked with nothing marked");
    return true;
  }
  // **The engine is waited for twice, and the first one is what the maker cannot teach** (0.151.8): the maker loads the
  // image and *then* waits, which is safe on an emulator where the next page comes from a person's finger. Here the next page
  // can arrive **one millisecond** after the last one —the reader paints the whole glass twice when it starts—, and a
  // load of 259 200 bytes into an image the running engine is reading is what this controller does not survive. So the
  // wait is asked **before the load** as well, with the floor below it (`kEngineStartMs`), and it is asked again before
  // `DPY_BUF_AREA` the way the maker asks it (docs/50).
  if (!wait_paint(kPaintTimeoutMs)) {
    if (!silent_ || !bring_back()) return false;
  }
  // The rectangle that changed, **in the frame the app drew in**, in whole nibbles.
  uint16_t x0 = static_cast<uint16_t>(dirty_x0_ & ~1u);
  uint16_t x1 = static_cast<uint16_t>((dirty_x1_ + 1) & ~1u);
  uint16_t y0 = dirty_y0_;
  uint16_t y1 = dirty_y1_;
  if (x1 > width_) x1 = width_;
  if (y1 > height_) y1 = height_;
  if (x1 <= x0 || y1 <= y0) {
    dirty_ = false;
    return true;
  }

  // And the same rectangle **on the glass**, which is where the pixels are written: the turn is done
  // here, in software, because the controller is only ever told about its own frame (docs/50).
  uint16_t nx0 = 0;
  uint16_t nx1 = 0;
  uint16_t ny0 = 0;
  uint16_t ny1 = 0;
  logical_to_native(x0, y0, x1, y1, nx0, nx1, ny0, ny1);
  // The controller wants a width in whole groups of four pixels, so a rectangle that starts at 5 and is
  // 3 wide goes as 4..8 (docs/50).
  nx0 = static_cast<uint16_t>(nx0 & ~3u);
  nx1 = static_cast<uint16_t>((nx1 + 3) & ~3u);
  if (nx1 > native_width_) nx1 = native_width_;
  if (ny1 > native_height_) ny1 = native_height_;
  if (nx1 <= nx0 || ny1 <= ny0) {
    dirty_ = false;
    return true;
  }
  // **The area is loaded and painted from the left edge of the glass, never from its own left column** (0.118.7). The
  // board said it with the log of 0.118.6: the splash (`0,0 960x540`) and the bar at boot (`0,0 48x540`) go up, and
  // every paint that starts anywhere else —the page of `images` at `48,0 912x540`, its four controls at `56,12`,
  // `484,12`, `616,12` and `876,12`, the clock of the bar at `4,104`— is accepted by the controller and shows **nothing**:
  // the image it loads is not the one it shows, and what comes back to the glass is the page before. A rectangle that
  // starts at zero covers the whole row of the image it belongs to, so the load walks the memory in the order it is
  // written; this costs the pixels between the edge and the change and nothing else (docs/50, docs/92 §33).
  //
  // **And that night was not a clean measurement** (0.179.12): the same boot had a second fault, found a day later —the
  // panel was never selected again after the splash (0.118.9)—, and every paint after the first one failed whatever
  // its rectangle was. So the rule stays as the default and the other one is a question anyone can ask the glass with
  // `panel box`: with it the area is the box of the change, the load takes the columns it needs and the page is left
  // alone (docs/30, docs/50).
  if (!from_box_) nx0 = 0;
  // And **a paint that cleans cleans the whole glass**: a `gc16` of a region leaves the ghost of everything outside it,
  // which is exactly what a change of page is, and it costs one whole page of ink for the pages that ask to be cleaned
  // —the splash and the first page of an app— and not for the ones that only move a number, which ask for `du`
  // (docs/50).
  const bool cleans = mode == hal::UpdateMode::kGc16 || mode == hal::UpdateMode::kGl16 ||
                      mode == hal::UpdateMode::kGlr16 || mode == hal::UpdateMode::kGld16;
  if (cleans &&
      static_cast<uint32_t>(nx1) * (ny1 - ny0) >= (static_cast<uint32_t>(native_width_) * native_height_) / 2u) {
    nx1 = native_width_;
    ny0 = 0;
    ny1 = native_height_;
  }
  const uint16_t w = static_cast<uint16_t>(nx1 - nx0);
  const uint16_t h = static_cast<uint16_t>(ny1 - ny0);

  const uint16_t area[5] = {kPixelFormat, nx0, ny0, w, h};
  // The controller is told **where** its image is before being told where to write: the two words of
  // the target address, **high and then low**, which is the order of `SetTargetMemoryAddr` in the
  // maker's driver (docs/50).
  if (!write_register(kRegLisHigh, static_cast<uint16_t>((memory_ >> 16) & 0xFFFF)) ||
      !write_register(kRegLisLow, static_cast<uint16_t>(memory_ & 0xFFFF)) ||
      !command(kCmdLoadImageArea, area, 5)) {
    return false;
  }

  // Row by row of the glass, gathered from the image, and **the bus is held for the whole rectangle**:
  // locking it per row would spend more time in the driver than on the wire (docs/50).
  uint8_t row[kMaxRowBytes];
  const uint16_t row_bytes = static_cast<uint16_t>(w / 2);
  if (!take_bus()) return false;
  bool sent = true;
  for (uint16_t row_index = 0; row_index < h && sent; ++row_index) {
    gather_row(row, nx0, static_cast<uint16_t>(ny0 + row_index), w);
    sent = write_pixels(device_, row, row_bytes);
  }
  give_bus();
  if (!sent) return false;
  if (!command(kCmdLoadImageEnd, nullptr, 0)) return false;

  // Asking for the paint and **not waiting for it**: the ink takes 120 to 450 ms and the kernel has
  // work to do. `idle()` is the question for a later turn (docs/50).
  uint16_t display[7] = {nx0, ny0, w, h, static_cast<uint16_t>(mode),
                         static_cast<uint16_t>(memory_ & 0xFFFF),
                         static_cast<uint16_t>((memory_ >> 16) & 0xFFFF)};
  // **The engine, exactly where the maker asks it** (0.151.7): `M5EPD_Driver::UpdateArea` calls `CheckAFSR` after the
  // image has been loaded and `LD_IMG_END` has closed it, immediately before `DPY_BUF_AREA`. **Where it is asked is the
  // whole point**: `LUTAFSR` read a millisecond after a `DPY_AREA` is a **zero**, because the engine has not marked
  // itself busy yet —the board says it in its own log, `engine status 0000 after the paint`— so a wait that is asked at
  // the top of the paint does not wait at all, and the second page goes out on top of the first one, which is what the
  // controller of this panel does not survive. The load of a page is two hundred milliseconds of SPI, and by the end of
  // it the engine before has said what it is (docs/50).
  if (!wait_paint(kPaintTimeoutMs)) {
    // A panel that did not answer at all is the one case with a way back —the rail— and it is not taken while the panel
    // is merely painting (0.151.3).
    if (!silent_ || !bring_back()) return false;
  }
  if (!command(kCmdDisplayArea, display, 7)) return false;
  painted_ms_ = static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
  asked_ = false;
  // **A paint of half the glass or more is a page**, and only a page raises the floor of the next wait (0.151.8).
  last_wide_ = (static_cast<uint32_t>(w) * h >= (static_cast<uint32_t>(native_width_) * native_height_) / 2u);
  dirty_ = false;
  // **Every paint says what it sent** (0.118.6): the splash of the boot goes up and the page of an app does not, and the
  // two go through this same call — so the only way to tell them apart is to write down the five words of `DPY_AREA`
  // and the frame they were in. A driver that sends a rectangle nobody sees is a driver that has to say which one.
  // **A whole page says it at info and a blink at debug**: the caret of a box blinks twice a second and would fill the
  // console, and a page nobody sees is the thing being chased (0.118.6, docs/50).
  // **And what the engine says of itself** (0.118.8): the register right after `DPY_BUF_AREA`. A controller that accepts
  // the command answers with a status that is not zero; one that ignores it leaves it at zero, and a page nobody sees is
  // told apart from a page that is coming (docs/50, docs/92 §33).
  uint16_t status_after = 0;
  const bool status_read = read_register(kRegLutAfsr, status_after);
  hal::logf((static_cast<uint32_t>(w) * h >= 10000u) ? pal::kLogInfo : pal::kLogDebug, "PANEL",
            "engine status %s after the paint", status_read ? "read" : "not read");
  hal::logf((static_cast<uint32_t>(w) * h >= 10000u) ? pal::kLogInfo : pal::kLogDebug, "PANEL",
            "paint %u,%u %ux%u mode %u %s at 0x%06x (page %u,%u %ux%u, %u rows of %u B)",
            static_cast<unsigned>(nx0), static_cast<unsigned>(ny0), static_cast<unsigned>(w),
            static_cast<unsigned>(h), static_cast<unsigned>(mode), hal::mode_name(mode),
            static_cast<unsigned>(memory_), static_cast<unsigned>(x0), static_cast<unsigned>(y0),
            static_cast<unsigned>(x1 - x0), static_cast<unsigned>(y1 - y0), static_cast<unsigned>(h),
            static_cast<unsigned>(row_bytes));
  return true;
}

void It8951::logical_to_native(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t& nx0,
                                 uint16_t& nx1, uint16_t& ny0, uint16_t& ny1) const {
  switch (rotation_) {
    case 90:
      // Clockwise: the top-left of the page is the top-right of the glass.
      nx0 = static_cast<uint16_t>(height_ - y1);
      nx1 = static_cast<uint16_t>(height_ - y0);
      ny0 = x0;
      ny1 = x1;
      return;
    case 270:
      nx0 = y0;
      nx1 = y1;
      ny0 = static_cast<uint16_t>(width_ - x1);
      ny1 = static_cast<uint16_t>(width_ - x0);
      return;
    case 180:
      nx0 = static_cast<uint16_t>(width_ - x1);
      nx1 = static_cast<uint16_t>(width_ - x0);
      ny0 = static_cast<uint16_t>(height_ - y1);
      ny1 = static_cast<uint16_t>(height_ - y0);
      return;
    default:
      nx0 = x0;
      nx1 = x1;
      ny0 = y0;
      ny1 = y1;
      return;
  }
}

void It8951::gather_row(uint8_t* out, uint16_t nx0, uint16_t ny, uint16_t w) const {
  // **The turn, pixel by pixel**: a row of the glass is a line of the page that is read in another
  // direction, and the four bits of two neighbours of the glass may come from two different lines of the
  // page. It is the price of turning in software, and it is paid only for what changed (docs/50).
  const uint32_t stride = width_ / 2;
  for (uint16_t i = 0; i + 1 < w; i += 2) {
    uint8_t packed = 0;
    for (uint8_t half = 0; half < 2; ++half) {
      const uint16_t nx = static_cast<uint16_t>(nx0 + i + half);
      int32_t x = 0;
      int32_t y = 0;
      switch (rotation_) {
        case 90:
          x = ny;
          y = static_cast<int32_t>(height_) - 1 - nx;
          break;
        case 270:
          x = static_cast<int32_t>(width_) - 1 - ny;
          y = nx;
          break;
        case 180:
          x = static_cast<int32_t>(width_) - 1 - nx;
          y = static_cast<int32_t>(height_) - 1 - ny;
          break;
        default:
          x = nx;
          y = ny;
          break;
      }
      uint8_t level = kWhite;
      if (x >= 0 && y >= 0 && x < width_ && y < height_) {
        const uint8_t byte = image_[static_cast<uint32_t>(y) * stride + static_cast<uint32_t>(x) / 2];
        level = ((x & 1) == 0) ? static_cast<uint8_t>(byte >> 4) : static_cast<uint8_t>(byte & 0x0F);
      }
      packed = static_cast<uint8_t>((packed << 4) | (level & 0x0F));
    }
    out[i / 2] = packed;
  }
}

bool It8951::allocate_image() {
  if (image_ != nullptr) {
    heap_caps_free(image_);
    image_ = nullptr;
  }
  if (native_width_ == 0 || native_height_ == 0) return false;
  // The frame an app draws in is the glass turned: the sides swap on a quarter turn.
  if (rotation_ == 90 || rotation_ == 270) {
    width_ = native_height_;
    height_ = native_width_;
  } else {
    width_ = native_width_;
    height_ = native_height_;
  }
  if ((width_ / 2) > kMaxRowBytes) {
    snprintf(profile_, sizeof(profile_), "a row of %u pixels is wider than this driver carries", width_);
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
  return true;
}

// **The other rectangle a partial paint can ask for** (0.179.12): the box of the change instead of the edge of the
// image, which is a question about this controller and about the fault that was in the way when the edge was decided
// (0.118.9). It says what it is doing because the glass is what has to answer it, and a paint that shows nothing is
// told apart from a paint that shows the wrong thing by the box in the log (docs/50).
bool It8951::set_partial_from_the_box(bool on) {
  from_box_ = on;
  hal::logf(pal::kLogInfo, "PANEL", "a partial paint starts %s",
            on ? "at the box of the change" : "at the left edge of the image");
  return true;
}

bool It8951::set_rotation(uint16_t degrees) {
  if (degrees != 0 && degrees != 90 && degrees != 180 && degrees != 270) {
    why_ = "a turn is 0, 90, 180 or 270";
    return false;
  }
  if (native_width_ == 0) {
    // Before the glass has said what it is: it is kept and applied when the image is made.
    rotation_ = degrees;
    return true;
  }
  rotation_ = degrees;
  if (!allocate_image()) return false;
  // Everything the app drew in the old frame is meaningless in the new one, and the glass shows the old
  // one: the whole page is sent again (docs/50).
  mark(0, 0, width_, height_);
  return true;
}

// **The engine says whether it is working, and it says it in a register** (0.151.6): `LUTAFSR` reads zero when no LUT
// engine of the controller is running. The maker's own driver asks exactly this before every `DPY_BUF_AREA`
// (`M5EPD_Driver::CheckAFSR`, a loop on that register with a three second cap), and **the BUSY pin of the panel is not
// the engine**: measured on the board (8/11), "busy" said a page took 1052 ms, then 3000, then 2613 — three different
// numbers for the same page, and two of them a timeout. The register is an answer and the pin is a guess.
bool It8951::engine_over() {
  if (!ready_) return false;
  // A glass that is gone has nothing in flight, and saying `no` for ever keeps the kernel asking every turn (0.151.5).
  if (dead_) return true;
  // **The same floor the paint waits for** (0.151.8): a page that went out a moment ago is not over because the register
  // has not been marked yet, and `idle()` is asked by the kernel every turn (docs/50).
  if (painted_ms_ != 0) {
    const uint32_t now = static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
    if ((now - painted_ms_) < (last_wide_ ? measured_ms_ : kEngineStartMs)) return false;
  }
  uint16_t status = 0;
  // A read that failed says nothing about the engine, and saying "over" is the safe answer for a question that only
  // decides whether to look again (0.151.8).
  if (!read_register(kRegLutAfsr, status)) return true;
  return status == 0;
}

bool It8951::idle() {
  // The controller says whether the last paint is over. It is asked and not waited on (docs/50).
  return engine_over();
}

bool It8951::wait_paint(uint32_t timeout_ms) {
  if (!ready_) return false;
  const uint32_t started = static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
  silent_ = false;
  asked_ = false;
  // **How long the paint of ours that is being waited for cannot be over yet** (0.151.8): a small one is the floor of the
  // engine starting, and a **page** is the measured time of a page —the board said 1051 and 1052 ms, twice— because a
  // register read 50 ms after a whole page has started is a zero that means nothing, and the page before is still moving
  // ink. The number only ever grows, and it only grows from a page (docs/50).
  const uint32_t floor = last_wide_ ? measured_ms_ : kEngineStartMs;
  for (;;) {
    const uint32_t now = static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
    if (painted_ms_ != 0 && (now - painted_ms_) < floor) {
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    uint16_t status = 0;
    const bool first = (painted_ms_ != 0) && !asked_;
    asked_ = true;
    if (!read_register(kRegLutAfsr, status)) {
      // **The bus did not take the question**, which is not the same as a panel that is not answering: the ink of a
      // whole page keeps the line of busy low for a second and a register read has to wait for it, and a read that came
      // back empty is asked again (0.151.8).
      if ((now - started) > timeout_ms) {
        // **The reason the read gave is said before it is thrown away** (0.151.13): the message here used to overwrite it
        // and the board was left with "the register was not read" for a panel whose line of busy had been low for three
        // seconds. `why_` is the read's own sentence —"busy stayed low", "bus refused the prefix"— and the level of the
        // line is the other half of it (measured 8/11, docs/50).
        const char* read_why = why_;
        hal::logf(pal::kLogWarn, "PANEL", "LUTAFSR was not read in %u ms (%s; busy reads %d)",
                  static_cast<unsigned>(now - started), read_why,
                  gpio_get_level(static_cast<gpio_num_t>(pins_.busy)));
        // **And a panel that has held its line of busy low for three seconds is not painting, it is stuck**: the way back
        // for a controller that does not answer is the rail, the same one `revive()` gives a panel that answers `ffff`.
        silent_ = true;
        why_ = "the register of the engine was not read";
        return false;
      }
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    if (first) {
      // **What the register says, and how long after our own paint it said it** (0.151.8): the board has only ever shown
      // `0000` there, read a millisecond after `DPY_BUF_AREA`, and whether the engine ever marks itself busy is the one
      // number this driver has never seen (docs/50).
      hal::logf(pal::kLogDebug, "PANEL", "LUTAFSR %04x %u ms after the paint", static_cast<unsigned>(status),
                static_cast<unsigned>(now - painted_ms_));
    }
    if (status == 0) {
      // **The panel measured itself again**: how long this paint held the engine, which is what the next page of the same
      // size will be waited for (0.151.8).
      if (last_wide_ && painted_ms_ != 0) {
        const uint32_t took = now - painted_ms_;
        if (took < kPaintTimeoutMs && took > measured_ms_) {
          measured_ms_ = took;
          hal::logf(pal::kLogInfo, "PANEL", "a page held the engine %u ms", static_cast<unsigned>(took));
        }
      }
      return true;
    }
    // **`ffff` is a panel that is not answering**, and it is said here so the paint above can bring the rail back
    // (0.151.2, docs/95).
    if (status == 0xFFFF) {
      silent_ = true;
      why_ = "the panel stopped answering";
      return false;
    }
    if ((now - started) > timeout_ms) {
      // **And if this ever happens it is said**: it is an engine that has not stopped after three seconds, which no mode
      // of this panel takes, and the maker's own driver gives up at the same number (docs/50).
      why_ = "the previous paint did not finish";
      hal::logf(pal::kLogWarn, "PANEL", "the previous paint did not finish in %u ms (LUTAFSR still %04x)",
                static_cast<unsigned>(now - started), static_cast<unsigned>(status));
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
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
  // The level of the line of busy at the moment the controller was armed, which is the only other thing that can be said
  // about a panel that does not answer (0.151.13, docs/50).
  hal::logf(pal::kLogInfo, "PANEL", "busy reads %d %s the handshake",
            gpio_get_level(static_cast<gpio_num_t>(pins.busy)), there ? "after" : "without");
  // Said out loud once, like the sensor and the glass: it is the only way to know what this controller
  // answered without a scope (docs/50, docs/95).
  hal::logf(pal::kLogInfo, "PANEL", "%s%s", chip.profile(), there ? "" : " (no panel)");
  if (there) panel = &chip;
  return panel;
}

}  // namespace hal
