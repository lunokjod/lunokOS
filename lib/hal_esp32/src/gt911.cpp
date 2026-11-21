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
  // **I el nucli es desperta aquí i no al proper torn** (0.190.119): the edge is the only moment this panel has news, and a turn that is
  // asleep for its ten milliseconds —or its forty, on a board that naps— is a tap the person has already made and cannot see. The frame
  // is still read from the turn and not from the interrupt: a bus is not an interrupt's business (docs/50, docs/95).
  pal::IQueue* queue = pal::system().queue();
  if (queue != nullptr) {
    pal::Event ev = {};
    ev.type = pal::kEventTouch;
    queue->push_from_isr(ev);
  }
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
// **Els comandaments i els dos bytes que fan seva una configuració** (0.190.105, el full del GT911 i el driver del fabricant,
// `TouchDrvGT911::reloadConfig`): the command register is `0x8040` —`0x05` is the chip's own sleep, `SensorLib` `writeCommand(0x05)`—
// and a block that has been changed is only taken when the checksum at `0x80FF` and the «fresh» byte at `0x8100` are written after
// it. The checksum is the sum of the first 184 bytes of the block, negated (docs/50).
const uint16_t kRegCommand = 0x8040;
const uint16_t kRegConfigFresh = 0x8100;
const uint8_t kCommandSleep = 0x05;
// Inside the block: `0x804D` is the module switch whose two low bits are the interrupt mode, i `0x8047` + 6 = `0x804D`.
const uint8_t kConfigModuleSwitch = 6;
// **Els quatre modes de la línia** (`GT911_MODULE_SWITCH_1`, `setInterruptMode` del fabricant): 0 és vora de pujada, 1 vora de baixada,
// 2 **nivell baix** i 3 nivell alt. El de nivell baix és el que el seu exemple en diu el de menys consum: *«Keep high level when idle, and
// switch to low level when touched»* —la línia es queda baixa mentre hi ha un dit i el host no ha de preguntar res— (docs/50).
const uint8_t kInterruptLowLevel = 0x02;
// **El terra i el sostre del ritme** (0.190.105): the byte at `0x8056` holds five to twenty hertz and `kGt911QuietHz` is where this
// driver leaves it while nobody is on the glass. Ten and not five because the first touch of a quiet glass waits for a scan: at five
// hertz that is two hundred milliseconds before a finger is even noticed, i a hundred is the most a page that answers with the ink
// (half a second a pass) can hide (docs/50, docs/95).
const uint8_t kGt911QuietHz = 10;
// **I el sostre amb la llum encesa** (0.190.119, l'amo: *«triga tant en respondre? sembla polling»*): la taula del `0x8056` arriba a vint
// hertzs i aquesta unitat ve a deu, o sigui que «el ritme de la unitat» i «el terra» eren el mateix número i el burst no tenia res a apujar.
// Amb el vidre il·luminat el xip va a vint —un marc cada cinquanta mil·lisegons i no cada cent— i el que paga són microampers al costat
// d'una retroil·luminació encesa; quan la llum s'apaga (el dim), el terra torna a manar (docs/50, docs/95).
const uint8_t kGt911BriskHz = 20;
// **Cinc segons de ritme ràpid després de l'últim dit**: the burst is what a swipe and a keyboard need, and a finger that comes back
// inside those five seconds finds the glass already awake and does not pay a second configuration of the chip (docs/50).
const uint32_t kGt911HoldFastMs = 5000;

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

// **Qui hi ha, a les dues adreces possibles** (0.190.105): this was the body of `probe()` and it is a function of its own because the probe
// asks it twice —the second time after waking a controller that went down asleep— (docs/50).
bool Gt911::find_it() {
  const uint8_t addresses[2] = {kGt911Address, kGt911AddressBackup};
  uint8_t id[4] = {0};
  for (uint8_t i = 0; i < 2; ++i) {
    // **Ask who is there before reading anything.** This unit answers at one of the two addresses and
    // the other one is nobody: reading its product id from the empty one is what put
    // `i2c: read of 14:8140 failed (ESP_FAIL)` in the boot log of a board where nothing was wrong
    // (measured 1/11, docs/50).
    if (!bus_->present(addresses[i])) continue;
    address_ = addresses[i];
    if (!bus_->read16(address_, kRegProductId, id, sizeof(id))) continue;
    // The product id is the three characters `911` and a zero: a chip that answers something else
    // is another device on the same two wires and not ours (docs/50).
    if (id[0] == '9' && id[1] == '1' && id[2] == '1') return true;
  }
  return false;
}

bool Gt911::probe() {
  if (bus_ == nullptr) return false;
  bool found = find_it();
  // **Un xip que va dormir amb la placa no contesta** (0.190.105): the sleep command (`0x8040 = 0x05`) survives a reset of the ESP32
  // —the controller keeps its own supply— so a board that went down with «Wake on touch» off comes back to a glass that is asleep and
  // silent. The manufacturer's wake is a pulse of eight milliseconds on the line (`TouchDrvGT911::wakeup`), i es fa **només** quan el
  // xip no ha contestat: un xip despert no s'ha de tocar (docs/50).
  if (!found && int_pin_ >= 0) {
    hold_the_line_as_output(true);
    pal::delay_us(8000);
    rearm();
    asleep_ = false;
    pal::delay_us(10000);
    found = find_it();
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
  // **I el xip es queda en el mode de menys consum des del primer moment** (0.190.105): the rate the unit came with is remembered as the
  // fast one of a burst, the line is put in the chip's own `LOW_LEVEL_QUERY` mode —low while a finger is down, which is the mode the
  // manufacturer's example calls the low-power one— and the scans are dropped to `kGt911QuietHz` until somebody touches the glass.
  unit_rate_ = static_cast<uint8_t>(config[kConfigScanRate] & 0x0Fu);
  quiet_rate_ = static_cast<uint8_t>((kGt911QuietHz > 5u) ? (kGt911QuietHz - 5u) : 0u);
  hertz_ = 5u + unit_rate_;
  set_interrupt_mode(kInterruptLowLevel);
  if (quiet_rate_ < unit_rate_) set_rate_byte(quiet_rate_);
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
  snprintf(profile_, sizeof(profile_), "GT911 at 0x%02x, config %u, %ux%u, %u fingers, %u Hz quiet, %u Hz awake",
           address_, config[0], static_cast<unsigned>(width_), static_cast<unsigned>(height_),
           static_cast<unsigned>(points_), static_cast<unsigned>(5u + quiet_rate_),
           static_cast<unsigned>(5u + unit_rate_));
  return true;
}

// **La línia, tornada a posar com era** (0.190.74): a light sleep of the chip arms the GPIO of the panel as a **level** wake
// (`gpio_ll_wakeup_enable` writes the pin's `int_type`) and disarming it does not put the falling edge back. The consequence was measured by
// the owner on the Paper: after the first nap the glass stopped answering and the board crawled. This is the same configuration `probe()`
// installs —an input with its pull-up and a falling edge— and the handler is already the core's, so it is not added twice (docs/50, docs/95).
void Gt911::rearm() {
  if (int_pin_ < 0) return;
  gpio_config_t line = {};
  line.pin_bit_mask = 1ULL << int_pin_;
  line.mode = GPIO_MODE_INPUT;
  line.pull_up_en = GPIO_PULLUP_ENABLE;
  line.pull_down_en = GPIO_PULLDOWN_DISABLE;
  line.intr_type = GPIO_INTR_NEGEDGE;
  gpio_config(&line);
}

// **Un byte de la configuració, i el xip se'l creu** (0.190.105): the block is read, the byte is changed, the checksum of the first 184
// bytes is written at `0x80FF` and `0x8100` is set to one —the sequence of the manufacturer's `reloadConfig`—. It costs a read of 186
// bytes and three writes, o sigui que es fa quan canvia el règim i no a cada frame (docs/50).
bool Gt911::set_config_byte(uint8_t offset, uint8_t value) {
  if (bus_ == nullptr || offset >= kConfigBytes - 2) return false;
  uint8_t config[kConfigBytes] = {0};
  if (!bus_->read16(address_, kRegConfig, config, sizeof(config))) return false;
  if (config[offset] == value) return true;  // nothing to say, nothing to pay
  config[offset] = value;
  uint8_t sum = 0;
  for (uint8_t i = 0; i < kConfigBytes - 2; ++i) sum = static_cast<uint8_t>(sum + config[i]);
  const uint8_t checksum = static_cast<uint8_t>(~sum + 1);
  uint8_t ask[3] = {static_cast<uint8_t>(kRegConfig >> 8),
                    static_cast<uint8_t>((kRegConfig & 0xFF) + offset), value};
  if (!bus_->write(address_, ask, sizeof(ask))) return false;
  ask[1] = 0xFF;  // 0x80FF, the checksum, which is the last byte of the block before the fresh flag
  ask[2] = checksum;
  if (!bus_->write(address_, ask, sizeof(ask))) return false;
  const uint8_t fresh[3] = {0x81, 0x00, 0x01};
  if (!bus_->write(address_, fresh, sizeof(fresh))) return false;
  return true;
}

bool Gt911::set_rate_byte(uint8_t value) {
  if (value > 15) value = 15;
  if (!set_config_byte(kConfigScanRate, value)) return false;
  hertz_ = 5u + value;
  scan_ms_ = gt911_scan_ms(value);
  return true;
}

bool Gt911::set_interrupt_mode(uint8_t mode) {
  if (bus_ == nullptr) return false;
  uint8_t config[kConfigBytes] = {0};
  if (!bus_->read16(address_, kRegConfig, config, sizeof(config))) return false;
  const uint8_t wanted = static_cast<uint8_t>((config[kConfigModuleSwitch] & 0xFCu) | (mode & 0x03u));
  return set_config_byte(kConfigModuleSwitch, wanted);
}

// **La línia, com la mou el fabricant** (0.190.105): `TouchDrvGT911::sleep` drives the line low as an output before the command and
// `wakeup` pulses it high for eight milliseconds and gives it back as an input. El xip escolta el pin que és la seva entrada de
// despertar, i en aquesta placa és el mateix que el seu avís (docs/50).
void Gt911::hold_the_line_as_output(bool level) {
  if (int_pin_ < 0) return;
  gpio_config_t line = {};
  line.pin_bit_mask = 1ULL << int_pin_;
  line.mode = GPIO_MODE_OUTPUT;
  line.pull_up_en = GPIO_PULLUP_DISABLE;
  line.pull_down_en = GPIO_PULLDOWN_DISABLE;
  line.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&line);
  gpio_set_level(static_cast<gpio_num_t>(int_pin_), level ? 1 : 0);
}

bool Gt911::sleep_now() {
  if (bus_ == nullptr || asleep_) return false;
  hold_the_line_as_output(false);
  const uint8_t command[3] = {static_cast<uint8_t>(kRegCommand >> 8), static_cast<uint8_t>(kRegCommand & 0xFF), kCommandSleep};
  if (!bus_->write(address_, command, sizeof(command))) return false;
  asleep_ = true;
  g_frame_ready = false;
  return true;
}

bool Gt911::wake_up() {
  if (bus_ == nullptr || !asleep_) return false;
  hold_the_line_as_output(true);
  pal::delay_us(8000);
  asleep_ = false;
  // **I la línia torna a ser un avís** (0.190.105): the driver's own configuration —an input with its pull-up and a falling edge—, which
  // is the same `rearm` a light sleep uses. The chip keeps the configuration it had, o sigui que el règim de menys consum no es toca.
  rearm();
  g_frame_ready = false;
  g_last_read_ms = static_cast<int32_t>(pal::system().millis());
  return true;
}

void Gt911::release() {
  // Writing a zero to the status is what tells the chip the frame has been taken; it is called for a frame that is
  // there and for nothing else (0.174.4, docs/50).
  const uint8_t frame[3] = {0x81, 0x4E, 0x00};
  bus_->write(address_, frame, sizeof(frame));
}

// **El règim del xip, segons el que hi ha al vidre** (0.190.105): a finger raises the scans to the rate the unit came with, and a glass
// that has been quiet for `kGt911HoldFastMs` goes back to the floor. It happens on a read and not on a timer because a glass that nobody
// reads has nothing to raise the rate for, i el que paga el canvi són dues configuracions del xip (docs/50, docs/95).
void Gt911::settle(uint32_t now_ms) {
  if (asleep_) return;
  // **Amb la llum encesa el terra és el sostre** (0.190.119): no hi ha res a deixar baixar mentre una persona mira el vidre (docs/30).
  if (brisk_) {
    if (hertz_ != kGt911BriskHz) set_rate_byte(static_cast<uint8_t>(kGt911BriskHz - 5u));
    return;
  }
  if (!adaptive_) {
    if (hertz_ != 5u + unit_rate_) set_rate_byte(unit_rate_);
    return;
  }
  if (hertz_ == 5u + unit_rate_) {
    if (now_ms - last_finger_ms_ < kGt911HoldFastMs) return;
    if (quiet_rate_ < unit_rate_) set_rate_byte(quiet_rate_);
  }
}

// **El commutador, aplicat ara** (0.190.107): someone who turns *Adaptive touch scan* off wants the glass at its top speed from that moment
// and not five seconds later, i qui l'encén vol veure'l baixar quan el dit marxi. Cap dels dos demana tocar el xip si el règim ja és el que
// toca, i per això el canvi només escriu quan el número és diferent (docs/30, docs/50).
void Gt911::set_adaptive_scan(bool on) {
  adaptive_ = on;
  if (asleep_) return;  // the rate of a sleeping chip is the sleep's business (docs/60)
  if (!on) {
    const uint8_t rate = brisk_ ? static_cast<uint8_t>(kGt911BriskHz - 5u) : unit_rate_;
    if (hertz_ != 5u + rate) set_rate_byte(rate);
    return;
  }
  // **Adaptatiu encès** (0.190.107): a glass that has had nobody on it for the hold of a burst drops to the floor now, i un que acaba de
  // tenir un dit s'hi queda fins que el burst s'acabi (docs/50).
  last_finger_ms_ = static_cast<uint32_t>(pal::system().millis()) - kGt911HoldFastMs;
  settle(last_finger_ms_);
}

// **El vidre, amb la llum encesa o sense** (0.190.119): the light of the glass is the one thing that says whether a person is looking at it, i
// el règim del panell es lliga a ella en lloc de a un rellotge. `settle` fa la resta —pujar si cal i tornar al terra quan la llum s'apaga—, i un
// xip adormit no es toca: el seu ritme és cosa del son (docs/30, docs/50, docs/60).
void Gt911::set_brisk(bool on) {
  if (brisk_ == on) return;
  brisk_ = on;
  if (asleep_) return;
  settle(static_cast<uint32_t>(pal::system().millis()));
  // **El canvi de règim, al registre** (0.190.119): és el número que diu per què un toc triga el que triga, i surt dues vegades per minut com a
  // molt —quan la llum s'encén i quan el dim l'apaga— (docs/50, docs/95).
  logf(pal::kLogDebug, "TOUCH", "the panel scans at %u Hz: the glass is %s", static_cast<unsigned>(hertz_),
       on ? "lit" : "dark");
}

// **El règim més baix, per a qui ha de dormir** (0.190.107): a deep sleep that listens for a finger does not need a hundredth of a second
// of resolution —it needs the line to fall— i el que paga el vidre desperta no ho ha de pagar dormit (docs/50, docs/60).
void Gt911::quiet_now() {
  if (asleep_) return;
  if (quiet_rate_ < unit_rate_) set_rate_byte(quiet_rate_);
  last_finger_ms_ = 0;
}

uint8_t Gt911::read(TouchPoint* out, uint8_t capacity) {
  if (bus_ == nullptr || out == nullptr || capacity == 0) return 0;
  settle(static_cast<uint32_t>(pal::system().millis()));
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
  // **El dit puja la potència del panell** (0.190.105, l'amo: *«fins a detectar un dit, llavors pujes la potència del panell durant uns
  // segons»*): the rate goes to the one this unit came with and the clock of the hold starts again on every frame of the contact.
  if (down_) {
    last_finger_ms_ = static_cast<uint32_t>(pal::system().millis());
    if (!brisk_ && hertz_ != 5u + unit_rate_) set_rate_byte(unit_rate_);
  }
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
