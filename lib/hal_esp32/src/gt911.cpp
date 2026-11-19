// The GT911 driver: ask who is there, wait to be told a frame is ready, and turn eight bytes into one
// finger. The chip keeps its own state, so this driver keeps none: what is read is what the glass has now
// (docs/50).
#include "hal/log.h"
#include "esp32/gt911.h"

#include "driver/gpio.h"
#include "esp_attr.h"

#include <stdio.h>

namespace hal {
namespace {

// **El flamenc de la línia** (0.188.0): the chip pulls INT down when a scan has something to hand over, and the interrupt
// only says «look» —the frame is read outside it, over I2C, because a bus is not an interrupt's business—. It is one flag
// because this board has one panel, and `volatile` because it is written by the interrupt and read by the turn.
volatile bool g_frame_ready = false;

// **Quan el driver va preguntar el xip per última vegada** (0.188.7): perquè una línia que no torni a caure —un flamenc
// perdut, un xip que arrenca amb la línia baixa— no deixi el tàctil sense ànima, el registre d'estat es llegeix igualment un
// cop cada dos escombrats. El camí ràpid és la línia; això és la xarxa de seguretat, i costa 400 µs cada cent mil·lisegons
// (docs/50, docs/95).
int32_t g_last_read_ms = -1000;

// **Els avisos que no eren de ningú** (0.189.86): a line that fires with no frame behind it costs the twenty milliseconds of
// the wait below, and with a floating input that happened fifteen times a second. The number is counted here and `stats`
// prints it, so that the next time this driver wonders whether the line is noise or a finger there is a number and not a
// suspicion (docs/50, docs/95).
uint32_t g_warnings_without_frame = 0;

// The handler of an edge, which does one thing: raise the flag. It lives in IRAM because an interrupt can arrive while the
// flash is being read —a paint of the glass, a page of a font— and a handler out of the flash would wait for the flash.
void IRAM_ATTR on_the_line(void* arg) {
  (void)arg;
  g_frame_ready = true;
}

// The four registers this driver touches, all of them two bytes wide (docs/50).
const uint16_t kRegProductId = 0x8140;
const uint16_t kRegConfig = 0x8047;
const uint16_t kRegStatus = 0x814E;
const uint16_t kRegPoint = 0x814F;

// Inside the configuration block, which starts at `0x8047`: the version is its first byte, the two
// maxima are the frame the chip counts in, and `0x804C` is how many fingers it will report at once.
const uint8_t kConfigXMax = 1;
const uint8_t kConfigYMax = 3;
const uint8_t kConfigTouchNum = 5;
// **The byte of the scan rate** (0.188.1): `0x8056` of the chip's map, which is fifteen bytes after the start of the block
// (`0x8047`) this file reads (docs/50).
const uint8_t kConfigScanRate = 15;
// 186 bytes, which is the length both vendor drivers read and write back with a fresh checksum
// (docs/50).
const uint8_t kConfigBytes = 186;

// A point is eight bytes: track id, x, y, the size of the contact, and one reserved byte.
const uint8_t kPointBytes = 8;
const uint8_t kFrameBytes = kGt911MaxPoints * kPointBytes;

// The values are little-endian in the frame, which is the opposite of the two-byte register
// addresses: the chip counts this way and both vendor drivers read it this way (docs/50).
uint16_t word_at(const uint8_t* data, uint8_t offset) {
  return static_cast<uint16_t>((static_cast<uint16_t>(data[offset + 1]) << 8) |
                               static_cast<uint16_t>(data[offset]));
}

}  // namespace

bool Gt911::probe() {
  if (bus_ == nullptr) return false;
  const uint8_t addresses[2] = {kGt911Address, kGt911AddressBackup};
  uint8_t id[4] = {0};
  bool found = false;
  for (uint8_t i = 0; i < 2 && !found; ++i) {
    // **Ask who is there before reading anything.** This unit answers at one of the two addresses and
    // the other one is nobody: reading its product id from the empty one is what put
    // `i2c: read of 14:8140 failed (ESP_FAIL)` in the boot log of a board where nothing was wrong
    // (measured 1/11, docs/50).
    if (!bus_->present(addresses[i])) continue;
    address_ = addresses[i];
    if (!bus_->read16(address_, kRegProductId, id, sizeof(id))) continue;
    // The product id is the three characters `911` and a zero: a chip that answers something else
    // is another device on the same two wires and not ours (docs/50).
    if (id[0] == '9' && id[1] == '1' && id[2] == '1') found = true;
  }
  if (!found) {
    snprintf(profile_, sizeof(profile_), "no answer at 0x%02x or 0x%02x", kGt911Address,
             kGt911AddressBackup);
    return false;
  }

  uint8_t config[kConfigBytes] = {0};
  if (!bus_->read16(address_, kRegConfig, config, sizeof(config))) {
    snprintf(profile_, sizeof(profile_), "GT911 at 0x%02x, no configuration", address_);
    return false;
  }
  width_ = word_at(config, kConfigXMax);
  height_ = word_at(config, kConfigYMax);
  const uint8_t points = config[kConfigTouchNum];  // The configuration of the unit is what it will report: it says two on the Paper measured on 1/11,
  // and anything the chip cannot give is a number an app would draw a gesture for and never see. A
  // value outside the frame's room is the frame's room.
  points_ = (points >= 1 && points <= kGt911MaxPoints) ? points : kGt911MaxPoints;
  // **I cada quant escombra, que també ho diu ell** (0.188.1): the byte at `0x8056` is the scan rate, and until today the
  // driver told the kernel ten milliseconds from a datasheet assumption. This unit says twenty hertz —one scan every
  // fifty milliseconds— and the kernel's window of silence is three of those, so a finger that walks is not declared gone
  // in the middle of a swipe (docs/50, docs/95).
  scan_ms_ = gt911_scan_ms(config[kConfigScanRate]);
  // A frame left over from before the reset is dropped here: the first read of the session must be
  // a finger that is on the glass now and not one from the boot (docs/50).
  release();
  // **I la línia que avisa, escoltada** (0.188.0): the chip drives INT low when a scan has a frame, and this is the same
  // line the board uses to pick the address during a reset —GPIO36, an input, so listening to it is the only thing an
  // input-only pin can do and the only thing this driver wants from it—. M5's own `M5EPD` arms it the same way
  // (`attachInterrupt(pin_int, ..., FALLING)`), and the frame is read outside the interrupt because a bus is not an
  // interrupt's business (docs/50, docs/95).
  if (int_pin_ >= 0) {
    // **The line is an input that the chip drives**: no pull of ours on it, and a falling edge is «hay un frame» —the level
    // is low until the status register is written back— (docs/50).
    gpio_config_t line = {};
    line.pin_bit_mask = 1ULL << int_pin_;
    line.mode = GPIO_MODE_INPUT;
    // **Amb pull-up, i això no és un detall** (0.189.86): between frames the chip lets the line go and GPIO36 —an input with no
    // pull of its own— picks up whatever the board is doing. The board measured what that costs: **fifteen warnings a second
    // with no finger on the glass**, each one twenty milliseconds of waiting for a frame bit that never came, o sigui **el
    // 31 % del temps despert** preguntant un xip que no tenia res a dir (docs/50, docs/95).
    line.pull_up_en = GPIO_PULLUP_ENABLE;
    line.pull_down_en = GPIO_PULLDOWN_DISABLE;
    line.intr_type = GPIO_INTR_NEGEDGE;
    gpio_config(&line);
    // **The service of interrupts is the core's**: the Arduino of this board has one already installed, and asking for a
    // second one is an error that says so and changes nothing (docs/50).
    gpio_install_isr_service(0);
    gpio_isr_handler_add(static_cast<gpio_num_t>(int_pin_), on_the_line, nullptr);
  }
  snprintf(profile_, sizeof(profile_), "GT911 at 0x%02x, config %u, %ux%u, %u fingers, %u ms a scan", address_,
           config[0], static_cast<unsigned>(width_), static_cast<unsigned>(height_),
           static_cast<unsigned>(points_), static_cast<unsigned>(scan_ms_));
  return true;
}

void Gt911::release() {
  // Writing a zero to the status is what tells the chip the frame has been taken; it is called for a frame that is
  // there and for nothing else (0.174.4, docs/50).
  const uint8_t frame[3] = {0x81, 0x4E, 0x00};
  bus_->write(address_, frame, sizeof(frame));
}

uint8_t Gt911::read(TouchPoint* out, uint8_t capacity) {
  if (bus_ == nullptr || out == nullptr || capacity == 0) return 0;
  // **Sense avís no hi ha res a demanar** (0.188.0): the line is the chip's own word that a scan has a frame, so a turn
  // that nobody warned about answers before touching the bus —not even the status— and the frame is taken on the turn the
  // interrupt arrives instead of up to a scan later (docs/50, docs/95).
  if (int_pin_ >= 0 && !g_frame_ready) {
    // **La xarxa de seguretat** (0.188.7): a line that never comes back is a glass that answers nothing for ever, and the
    // frame a finger is making stays in the chip. Two scans of patience and the status is read anyway (docs/50, docs/95).
    const int32_t fallback_ms = static_cast<int32_t>(2 * scan_ms());
    const int32_t now_ms = static_cast<int32_t>(pal::system().millis());
    if ((now_ms - g_last_read_ms) < fallback_ms) return 0;
  }
  g_last_read_ms = static_cast<int32_t>(pal::system().millis());
  uint8_t status = 0;
  // **L'avís arriba abans que el marc, i això no és un avís fals** (0.188.7): the line falls when the chip *starts* to have
  // something, and the status bit that says «there is a frame» is set a few milliseconds later —the datasheet says about ten
  // and the driver of Linux waits **twenty** for exactly this reason—. Reading once at the instant of the edge found bit 7
  // clear, and the frame was thrown away **and the flag with it**: the line stays low until the status is written back, so
  // no second edge ever came and the rest of the contact was invisible. That is a tap that works once and a swipe that never
  // exists — *«al dispositiu real NI UN [drag]»* —. The wait is bounded and only happens on the turn the line warned, which
  // is the one turn a frame is on its way (docs/50, docs/95).
  // **Es mira dues vegades i no es gira vint** (0.189.87): the line falls when the chip *starts* to have something and the
  // frame bit is set a few milliseconds later, so waiting was right — but the wait was twenty tries of a millisecond **and then
  // the flag was dropped**, which made an edge with nothing behind it cost thirty milliseconds. The board measured it: three
  // and a half edges a second with no finger, and a tenth of the uptime gone. Now the wait is two milliseconds and the flag
  // **stays up** when the frame is not there yet: the next turn is ten milliseconds away and the frame, which is three
  // milliseconds behind its edge, is caught then. A real finger is never lost, and a wire that fires for nothing costs one
  // read of three bytes (docs/50, docs/95).
  bool has_a_frame = false;
  for (int tries = 0; tries < 2 && !has_a_frame; ++tries) {
    if (!bus_->read16(address_, kRegStatus, &status, 1)) {
      // The chip did not answer: the warning was for a frame this turn cannot take, and the next scan warns again.
      g_frame_ready = false;
      return 0;
    }
    has_a_frame = (status & 0x80) != 0;
    if (!has_a_frame && tries == 0) pal::delay_us(2000);
  }
  // **I si encara no hi és, es deixa la línia neta** (0.189.87): the line stays low until the status is written back, so a flag
  // that is dropped without that write is a chip that never warns again —the tap that works once of 0.188.7—. Writing the zero
  // lifts the line, and a finger that is still down has a new scan in fifty milliseconds and warns again with a new edge. Així
  // un avís que no portava res costa dues lectures i una escriptura, i no vint intents de mil·lisegon (docs/50, docs/95).
  if (!has_a_frame) {
    release();
    g_frame_ready = false;
    ++g_warnings_without_frame;
    return 0;
  }
  const uint8_t points = static_cast<uint8_t>(status & 0x0F);
  // **Un frame sense punts és el dit que s'aixeca** (0.183.2): it is the chip's own word about the glass and not a silence to
  // be guessed from a timeout, so it is written down before the frame is released (docs/50, docs/90).
  down_ = (points > 0 && points <= kGt911MaxPoints);
  if (points == 0 || points > kGt911MaxPoints) {
    release();
    g_frame_ready = false;
    return 0;
  }
  uint8_t frame[kFrameBytes] = {0};
  if (!bus_->read16(address_, kRegPoint, frame, static_cast<size_t>(points) * kPointBytes)) {
    release();
    g_frame_ready = false;
    return 0;
  }
  release();
  // **El flamenc s'abaixa quan el frame ja és a dins** (0.188.0): the flag is cleared after the status is written back, which
  // is what makes the chip drop the line —so a scan that lands while this frame is being read warns again instead of being
  // swallowed— (docs/50).
  g_frame_ready = false;
  const uint8_t count = (points < capacity) ? points : capacity;
  for (uint8_t i = 0; i < count; ++i) {
    const uint8_t* point = frame + static_cast<uint16_t>(i) * kPointBytes;
    out[i].id = point[0];
    out[i].x = word_at(point, 1);
    out[i].y = word_at(point, 3);
    out[i].size = word_at(point, 5);
  }
  return count;
}

bool Gt911::flush() {
  if (bus_ == nullptr) return false;
  uint8_t status = 0;
  // The read is the question and the write is the answer that lifts the line. Both even when the status says there is
  // no frame: the chip keeps INT low until 0x814E is written back (docs/50, and M5Unified #91).
  const bool answered = bus_->read16(address_, kRegStatus, &status, 1);
  release();
  g_frame_ready = false;
  return answered;
}

uint32_t Gt911::false_warnings() const { return g_warnings_without_frame; }

ITouch* esp32_gt911(II2c* bus, int int_pin) {
  if (bus == nullptr) return nullptr;
  static Gt911 chip(bus, int_pin);
  // Asked once: `touch()` is called from the capabilities and from every read of a frame, and a
  // probe is a configuration of 186 bytes (docs/50).
  static const bool there = chip.probe();
  // Said out loud once, because it is the only way to know which of the two addresses this unit
  // answers on and in which frame it counts, without a scope (docs/50, docs/95).
  static bool said = false;
  if (!said) {
    said = true;
    hal::logf(pal::kLogInfo, "TOUCH", "%s", chip.profile());
  }
  return there ? &chip : nullptr;
}

}  // namespace hal
