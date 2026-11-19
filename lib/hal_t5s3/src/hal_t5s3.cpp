// Board: the LilyGO T5 E-Paper S3 Pro (`H752-01`; the repository calls it T5S3-4.7-e-paper-PRO).
// The identity header says which board is being built (docs/50). See docs/50-hal-i-plaques.md for
// the sheet and for what is deliberately absent.
//
// What belongs to the chip is not written here: the filesystem, the key-value store, the sleep, the
// radio, the key reader and the card are the ESP32's, and on this chip that is the ESP32-**S3**'s —
// `lib/hal_esp32` is written against the family and this is the first board of the project that is
// not the D0WDQ6— (docs/50). What is left here is what only this board can say: which identity it
// is, the pins of its bus, its gauge, and the panel that is still to come.
//
// **El vidre no hi és encara** (docs/50, docs/90): l'ED047TC1 d'aquesta placa no té controlador —el
// xip l'ha de pilotar en paral·lel, amb les taules de forma d'ona del panell— i això és la peça que
// falta. Fins que hi sigui, aquesta placa és una consola: el prompt, les alarmes, la targeta, l'hora
// i el tàctil, i cap pàgina.
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp32/spiram.h>

#include <cstdio>
#include <cstring>

#include <driver/spi_master.h>

#include "board/identity.h"
#include "esp32/bm8563.h"
#include "esp32/caps.h"
#include "esp32/clock.h"
#include "esp32/gpio_keys.h"
#include "esp32/gt911.h"
#include "esp32/i2c.h"
#include "esp32/net.h"
#include "esp32/nvs.h"
#include "esp32/power.h"
#include "esp32/rtc_clock.h"
#include "esp32/rtc_memory.h"
#include "esp32/sd_card.h"
#include "esp32/storage.h"
#include "hal/hal.h"
#include "t5s3/gauge.h"

namespace hal {
namespace {

// **Els dos fils de l'I2C d'aquesta placa** (docs/pinmap.md del repositori de la placa): tot el que
// no és el panell ni la targeta és aquí —el rellotge PCF8563, el tàctil GT911, l'indicador BQ27220,
// el carregador BQ25896 i l'expansor PCA9535 que encara no es toca— (docs/50).
const int kI2cSdaPin = 39;
const int kI2cSclPin = 40;

// The touch controller: the same GT911 as the Paper, its warning line on GPIO3 and its reset on
// GPIO9 (docs/50).
const int kTouchIntPin = 3;

// **La tecla que una persona pot prémer**: the BOOT key of the module, on GPIO0, which is one of the
// pins the chip can watch while it sleeps (an RTC pin). The board's own function button `S3` is not a
// GPIO —it hangs from the PCA9535— and it will arrive with the expander (docs/50).
const int kBootKeyPin = 0;

// **I la segona porta**: the PCF8563's interrupt line, on GPIO2. A PCF8563 pulls it low when its alarm
// or its countdown fires and lets it go when the flags are cleared, o sigui que és una porta del son
// profund exactament com la tecla —una línia oberta que baixa— i el xip d'aquesta placa les pot vigilar
// totes dues alhora amb `ext1` en el seu mode «qualsevol baixa» (docs/50, docs/60).
const int kClockIntPin = 2;

// **El rellotge del xip del rellotge**: the PCF8563 has a coin cell behind it and keeps the hour
// across the cable, and its registers are the ones the BM8563 driver already speaks (both are
// PCF8563 clones). Its own interrupt line is GPIO2, which is a wake pin of the chip and the door
// this board will have when the sleep is taught to arm it (docs/50, docs/60).
const uint8_t kClockAddress = 0x51;

// The card slot, on its own SPI bus —the panel is parallel and does not use it— (docs/50, docs/70).
const SdPins kSdPins = {12, 14, 13, 21, SPI2_HOST, 10000};

// The label of the data partition in partitions_16mb.csv: the filesystem is found by it, never by
// the core's default (docs/70).
const char kDataPartition[] = "littlefs";

// **Una tecla i prou**, i és la del mòdul: `kSimple` vol dir que no és part d'un dial, i el centre
// perquè no n'hi ha d'altra (docs/50).
const KeySpot kKeys[] = {
    {kBootKeyPin, Face::kTop, Spot::kCentre, KeyKind::kSimple, 0},
};

// The sleep of the chip, with the BOOT key as the way back, and the gauge of this board in place of
// the divider the Paper reads (docs/50).
class T5Power : public Esp32Power {
 public:
  T5Power(int wake_pin, int second_wake_pin) : Esp32Power(wake_pin, second_wake_pin) {}

  // **A deep sleep here does not cut the board's own supply**: there is no latch to let go, the
  // cell feeds the chip and the panel's rails are the TPS65185's business. So `deep_powers_off` is
  // false, and the policy does not treat this board as one that is powered off between two alarms
  // (docs/50, docs/60).
  bool deep_powers_off() const override { return false; }
  bool can_power_off() const override { return false; }
  bool battery(int& mv, int& pct, bool& charging) override { return gauge_.read(mv, pct, charging); }
  // The chip answers this one itself, amb les dues portes que la placa li ha cablejat (docs/50).
  const char* wakes() override { return Esp32Power::wakes(); }

 private:
  T5Gauge gauge_{esp32_i2c(kI2cSdaPin, kI2cSclPin)};
};

class T5Platform : public IPlatform {
 public:
  const char* id() const override { return board::id(); }
  const char* family() const override { return board::family(); }
  bool emulator() const override { return board::emulator(); }

  const Caps& caps() const override {
    if (!built_) {
      built_ = true;
      // Everything the chip gives —the radio with its provisioning and a page included— and then
      // only what this board has of its own (docs/50).
      caps_ = esp32_caps();
      if (esp32_storage("/littlefs", kDataPartition) != nullptr) caps_.add(kCapLittlefs);
      if (esp32_sd("/sd", kSdPins) != nullptr) caps_.add(kCapSd);
      // La tecla del mòdul, llegida a cada torn com la de l'ATOM (docs/50).
      caps_.add(kCapInput);
      // **La bateria només si l'indicador contesta**: a promised gauge that never answers is worse
      // than one that is not announced (docs/50).
      int probe_mv = 0;
      int probe_pct = 0;
      bool probe_charging = false;
      if (const_cast<T5Power&>(power_).battery(probe_mv, probe_pct, probe_charging)) {
        caps_.add(kCapBattery);
      }
      // El tàctil: el xip contesta i el nucli en treu tocs (docs/50).
      if (touch() != nullptr) caps_.add(kCapTouch);
      // **I el vidre, quan hi sigui**: `kCapDisplay` el posarà el driver del panell el dia que
      // l'ED047TC1 es pugui pintar (docs/50, docs/90).
    }
    return caps_;
  }

  // **El sostre d'una app és la PSRAM d'aquest xip, i en aquest és tota mapejable**: the S3 maps its
  // 8 MB of OPI PSRAM through the MMU, o sigui que no hi ha la finestra de 4 MB que limita el Paper
  // (measured there, 31/10, docs/75). It is the room the interpreter may take, and the number is the
  // chip's so that `mem` says the truth about this board and not about the other one (docs/50).
  uint32_t heap_limit_kb() const override { return heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024; }

  ExtraMemory extra_memory() const override {
    ExtraMemory out;
    out.chip_bytes = static_cast<uint32_t>(esp_spiram_get_size());
    out.total_bytes = static_cast<uint32_t>(heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    out.free_bytes = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return out;
  }

  IClock* clock() override { return t5s3_clock(); }
  IStorage* storage(const char* mount) override {
    if (mount != nullptr && strcmp(mount, "/rtc") == 0) return esp32_rtc_memory();
    IStorage* flash = esp32_storage(mount, kDataPartition);
    return (flash != nullptr) ? flash : esp32_sd(mount, kSdPins);
  }
  IKeyValue* nvs() override { return esp32_nvs(); }
  INet* net() override { return esp32_net(); }
  IPower* power() override { return &power_; }
  ISurface* display() override { return nullptr; }
  ILed* led() override { return nullptr; }
  IInput* input() override { return &keys_; }
  IImu* imu() override { return nullptr; }
  ITouch* touch() const override { return esp32_gt911(esp32_i2c(kI2cSdaPin, kI2cSclPin), kTouchIntPin); }

 private:
  // The hour: the PCF8563 is the memory and the chip's counter is the clock, exactly as on the Paper
  // —the chip is adopted once, when somebody first asks for the clock, which is before the boot reads
  // the epoch— (docs/50, docs/60).
  static IClock* t5s3_clock() {
    static Bm8563 chip(esp32_i2c(kI2cSdaPin, kI2cSclPin), kClockAddress);
    static RtcClock clock(&chip, esp32_clock());
    static bool adopted = false;
    if (!adopted) {
      adopted = true;
      clock.adopt();
    }
    return &clock;
  }

  mutable Caps caps_;
  mutable bool built_ = false;
  T5Power power_{kBootKeyPin, kClockIntPin};
  GpioKeys keys_{kKeys, sizeof(kKeys) / sizeof(kKeys[0])};
};

T5Platform g_platform;

}  // namespace

IPlatform& platform() { return g_platform; }

}  // namespace hal
