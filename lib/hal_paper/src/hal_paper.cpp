// Board: the M5 Paper v1.0 and v1.1 (ESP32-D0WDQ6-V3, 16 MB of flash, 8 MB of
// PSRAM, a 4.7" e-ink panel of 960 by 540 with an IT8951E controller, GT911 touch,
// BM8563 clock, SHT30 sensor, FM24C02 EEPROM, microSD and a 1150 mAh battery).
// See docs/50-hal-i-plaques.md for the sheet and for what is deliberately absent.
//
// What belongs to the chip is not written here: the filesystem, the key-value store,
// the clock, the sleep and the radio are the ESP32's, and the reader for the pins the
// dial is wired to is shared too, all of it in lib/hal_esp32 (docs/50). What this file
// keeps is the one thing the board cannot do without —the main power hold, which has to
// be set before anything else can fail— and where its three keys are.
//
// The panel, the touch, the RTC chip, the sensor, the SD card, the battery gauge and
// the radio are **not** here yet and the capabilities say so instead of pretending
// (docs/90). The radio is the same code the ATOM has, already shared; what is missing
// is handing it to this board and trying it on the hardware.
//
// No <Arduino.h> here: this file talks to the IDF directly, like the ATOM's HAL.
#include <stdio.h>
#include <string.h>

#include <driver/gpio.h>
#include <driver/spi_master.h>

#include <esp_heap_caps.h>
#include <esp_sleep.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "board/identity.h"
#include "esp32/battery.h"
#include "esp32/bm8563.h"
#include "esp32/caps.h"
#include "esp32/clock.h"
#include "esp32/extra_memory.h"
#include "esp32/fm24c02.h"
#include "esp32/gpio_keys.h"
#include "esp32/i2c.h"
#include "esp32/bluetooth.h"
#include "esp32/mesh.h"
#include "esp32/net.h"
#include "esp32/nvs.h"
#include "esp32/power.h"
#include "esp32/reset.h"
#include "esp32/rtc_clock.h"
#include "esp32/rtc_memory.h"
#include "esp32/sd_card.h"
#include "esp32/gt911.h"
#include "esp32/it8951.h"
#include "esp32/sht30.h"
#include "esp32/storage.h"
#include "hal/hal.h"

namespace hal {
namespace {

// The pins this file touches, from the maker's own library (docs/50). The main power
// has to be held high within a second of the button being pressed, or the board cuts
// itself off, which looks exactly like a crash.
//
// Two more are switched: the supply of the panel (GPIO23) and the one of the three HY2.0 ports
// (GPIO5). Nothing here turns them on —the panel is not driven yet and a port that is powered is
// current nobody asked for— but they are **turned off** when the board is (docs/50).
const int kMainPowerPin = 2;
const int kPanelPowerPin = 23;
const int kExtPowerPin = 5;

// The wires of the panel, from the maker's pin map. SCK, MOSI and MISO are the card's own wires: the
// two devices share one bus and the panel is just another device on it (docs/50).
const int kPanelSckPin = 14;
const int kPanelMosiPin = 12;
const int kPanelMisoPin = 13;
const int kPanelBusyPin = 27;

// A switched supply taken down: the pin becomes an output held low. It is set as an output first,
// because a pin left as an input would not switch anything off.
void turn_supply_off(int pin) {
  gpio_config_t out = {};
  out.pin_bit_mask = 1ULL << pin;
  out.mode = GPIO_MODE_OUTPUT;
  out.pull_up_en = GPIO_PULLUP_DISABLE;
  out.pull_down_en = GPIO_PULLDOWN_DISABLE;
  out.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&out);
  gpio_set_level(static_cast<gpio_num_t>(pin), 0);
}

// Held high for as long as the board is on, and set before anything else can fail.
void hold_power() {
  gpio_config_t out = {};
  out.pin_bit_mask = 1ULL << kMainPowerPin;
  out.mode = GPIO_MODE_OUTPUT;
  out.pull_up_en = GPIO_PULLUP_DISABLE;
  out.pull_down_en = GPIO_PULLDOWN_DISABLE;
  out.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&out);
  gpio_set_level(static_cast<gpio_num_t>(kMainPowerPin), 1);
}

// The three keys of the dial. They are inputs with no pull configured here: the board
// has the resistors and pressing a key closes it to ground (docs/50).
//
// The order is the one of the maker's own library (M5EPD.h), where the middle key
// —the one that is also the power button— is the main key, which is the one the
// system counts three taps on (docs/30). Its README says the opposite about left and
// right, so which of the two is the top of the dial is the one thing about this board
// that has to be checked by pressing it (docs/90).
//
// **The dial is on the right of the page as an app draws it** (1/11): whoever has the board in their
// hands says the three keys are on the right of the portrait page, not on the left, and that is what
// `Face::kRight` says. It used to say `kLeft`, which put the emulator's buttons on the wrong side of the
// page (docs/50, docs/85).
// **La tecla del dial que també és el botó d'encesa** (0.189.24): the centre key closes GPIO38 to ground, and that is the one
// a person presses to start the board and the one this file arms as the wake of a deep sleep that the cable is holding up
// (docs/50).
const int kPowerKeyPin = 38;

const KeySpot kKeys[] = {
    {kPowerKeyPin, Face::kRight, Spot::kCentre, KeyKind::kCompound, 0},  // centre (PWR)
    {37, Face::kRight, Spot::kUp, KeyKind::kCompound, 0},
    {39, Face::kRight, Spot::kDown, KeyKind::kCompound, 0},
};

// The label of the data partition in partitions_16mb.csv (docs/70).
const char kDataPartition[] = "littlefs";

// The battery comes to GPIO35 through a divider that halves it, so the pin never sees more
// than 2,1 V from a full cell (docs/50). The factor is what turns the pin back into the
// cell, and it is the one number here that a meter can prove wrong.
const BatteryPins kBatteryPins = {35, 2};

// The e-ink panel shares SCK, MOSI and MISO with the card, and its **CS is GPIO15**: a
// strapping pin that this board pulls down at boot, which means the IT8951E comes up
// *selected* —listening to every byte the card exchanges and free to drive MISO when the
// card should be the only one—. Deselecting it before the bus is used is what makes the
// card reliable (measured on the board, 31/10: `mounts` read the FAT fine and then
// `ls /sd` failed with `sdmmc_send_cmd returned 0x108` and `Check status failed 0x107`,
// which is what a bus with two listeners looks like).
const int kPanelCsPin = 15;

void deselect_panel() {
  gpio_config_t out = {};
  out.pin_bit_mask = 1ULL << kPanelCsPin;
  out.mode = GPIO_MODE_OUTPUT;
  out.pull_up_en = GPIO_PULLUP_DISABLE;
  out.pull_down_en = GPIO_PULLDOWN_DISABLE;
  out.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&out);
  gpio_set_level(static_cast<gpio_num_t>(kPanelCsPin), 1);
}

// The card slot: CS on GPIO4, and SCK, MOSI and MISO are the panel's own wires, which is
// why the two devices share one bus and the panel will join this host (docs/50). 10 MHz
// and not the 20 MHz default: the wires are shared, nothing here is measured yet, and a
// card driven too fast is a card that mounts and then times out.
// **La targeta del Paper no comparteix el bus amb cap xip que calgui callar**: el panell d'aquesta placa hi és, però el seu CS
// el governa el driver de l'IT8951 i no queda flotant (0.190.83, docs/50).
const SdPins kSdPins = {4, 14, 12, 13, SPI2_HOST, 10000, nullptr, nullptr, 0};

// The internal I2C of this board: the clock chip, the temperature sensor and the touch panel
// hang on these two wires, so a driver of any of them asks for the one bus (docs/50).
const int kI2cSdaPin = 21;
const int kI2cSclPin = 22;
// **La línia per la qual el GT911 diu que té un frame** (0.188.0): GPIO36, la mateixa que el xip mira en sortir del reset per
// triar la seva adreça —i que en aquesta placa no es pot conduir, perquè és d'entrada: el que sí que es pot és escoltar-la—.
// És el número que fa servir el codi del fabricant (`M5EPD.cpp`, `TP.begin(21, 22, 36)`) i el que aquesta `hal` li passa al
// driver perquè no pregunti el registre d'estat a cada torn (docs/50, docs/95).
const int kTouchIntPin = 36;
// **Quant es pot esperar que el GT911 deixi anar la línia** (M5Unified #91): a frame that nobody has taken is flushed in one
// scan —fifty milliseconds—, and a finger that is really there keeps it low; a sleep asked for by a person does not wait for
// ever on a hand that is using the board (docs/50).
const uint32_t kTouchReleaseMostMs = 1000;
// The BM8563, which is the board's real-time clock: the one with a coin cell behind it.
const uint8_t kBm8563Address = 0x51;

// The clock this board hands over: the ESP32 counts, the chip remembers. The chip is read
// once, here, on the first ask —which is during boot, before anything can look at the
// hour— and from then on it is the keeper (docs/60).
//
// The same chip is what the power of this board asks for, because its alarm is wired to the
// power circuit and not to a GPIO (docs/50): one object, two users.
Bm8563* paper_chip() {
  static Bm8563 chip(esp32_i2c(kI2cSdaPin, kI2cSclPin), kBm8563Address);
  return &chip;
}

IClock* paper_clock() {
  static RtcClock clock(paper_chip(), esp32_clock(), "BM8563");
  static bool adopted = false;
  if (!adopted) {
    adopted = true;
    clock.adopt();
  }
  return &clock;
}

// How long a wait this board will hand to the clock chip: any. Its countdown reaches 255
// minutes and its clock alarm the next match of an hour and a minute, so a wait longer than a
// day is armed in stages —the board comes back early and arms the rest— instead of being left
// to the RC timer of the chip the way it would be on a board without a clock (docs/60).

// The energy of this board: the chip's sleeps and speed, which are Esp32Power's, plus the
// one thing the chip cannot know —how much battery is left—. The millivolts are a
// measurement and the percentage is a curve (hal/battery.h, docs/50).
//
// `charging` is false because **this board has no pin to ask**: it is the one field that
// says nothing, and docs/30 says so instead of letting an app read it as "on the battery".
class PaperPower : public Esp32Power {
 public:
  // **La tecla PWR del dial, que és el GPIO38, va amb tots els sons d'aquesta placa** (0.190.11, l'amo: *«posa el deep sleep amb
  // alarma… Molt bé, però has tret l'int dels botons físics!!! Vull ambdues opcions»*). Aquest objecte naixia amb `-1` des de la
  // 0.7.0, o sigui que el camí del son profund **amb hora** armava el temporitzador i el rellotge i la tecla no la tocava ningú:
  // amb el cable al slot (on la placa no es pot apagar, docs/50) la placa només tornava per l'hora. `Esp32Power::sleep` ja fa
  // les dues coses quan sap quina tecla és —`ext0` sobre el pin i el temporitzador—, i amb això el son de la placa torna a tenir
  // les dues portes: **l'hora que li van demanar i la tecla d'una persona**. Amb la bateria el corrent se'n va abans i l'arma
  // d'`ext0` no vol dir res; la tecla PWR és la del xip de potència i la placa torna com ha tornat sempre (docs/50, docs/60).
  // **I la línia del tàctil com a porta de la becaina** (0.190.63): `kTouchIntPin` cau mentre un dit és al vidre i puja quan el
  // marc s'ha pres, o sigui que és exactament el flanc que un son lleuger necessita; no és una porta del son profund perquè aquell
  // vol un nivell i aquesta línia va a polsos (docs/50).
  PaperPower() : Esp32Power(kPowerKeyPin, -1, kTouchIntPin) {}

  bool battery(int& mv, int& pct, bool& charging) override {
    int read_mv = 0;
    if (!esp32_battery(kBatteryPins, read_mv)) return false;
    mv = read_mv;
    pct = static_cast<int>(battery_percent(read_mv));
    charging = false;
    return true;
  }

  // A deep sleep of this board is not a sleep: the board stops being powered. That is what
  // the coin cell behind the clock chip is for and what the main power hold is for —the
  // chip's alarm line reaches the power circuit, not a GPIO (docs/50)—, and it is the least
  // current this board can draw, which is what the energy rule asks for (README, law 9).
  //
  // The chip's hour is **aligned first**: the deadline being asked for is expressed in the
  // system's hour, and a chip counting from a stale one would keep the board off for the wrong
  // length of time. The kernel's own arming does the same (`RtcClock::arm_wake`); this is the
  // path that goes straight to the hardware and it does it too (docs/60).
  //
  // Three things are checked before the power is dropped, and each one of them is a board
  // that would not come back if it were skipped: that there is an hour to write (a board that
  // has never been set cannot arm anything, and a chip with a dead cell would never fire), that
  // the deadline is still to come, and that the chip **accepted** it —a wait past four hours goes
  // to the clock alarm, and one past a day is armed in stages by coming back early (docs/60)—.
  //
  // With the cable in the slot the board **cannot switch itself off** (the maker says so),
  // the power does not drop, and the call runs on into the chip's own deep sleep, which
  // wakes on its own timer as usual. Nothing is lost either way: the hour is in the chip.
  //
  // The main power hold is put **back up** when the board turns out to be still alive, and that is
  // not cosmetic: left down, the board would be hanging on the USB cable alone for the whole sleep,
  // and a cable that blinks is a board that comes back by itself and never reaches its hour
  // (measured on the glass, 31/10: a deep sleep of 300 s came back at 56 s with the wake cause
  // unset, which is a supply and not a clock).


  bool deep_powers_off() const override { return true; }

  // **The glass of this board, for the way down to put to sleep** (0.183.0): the maker's own shutdown writes
  // `IT8951_TCON_SLEEP` before the supplies go, and that command belongs to the panel and not to the power. The board hands
  // it over here when the panel joins the bus, and a build without a glass wakes up with nothing to put to sleep.
  void keep_glass(ISurface* glass) { glass_ = glass; }

  // Power off. It is the maker's own sequence, from the factory firmware of this board
  // (`M5Paper_FactoryTest`, `Shutdown()` in `global_setting.cpp`, docs/50): wait for what is pending
  // to finish, put the **panel to sleep**, take the **panel supply** down, then the **ports supply**,
  // then the **main power hold**, and put the chip into deep sleep right after —because with the
  // cable in the board does not stop being powered, and the deep sleep is what stops it instead of
  // leaving a board awake with nothing to do.
  //
  // The clock chip is disarmed **before** all that, and that is the one thing added to the maker's
  // plain `M5EPD::shutdown()`, which does not look at the alarm: one armed by an earlier sleep would
  // fire on a board somebody asked to be off and the SY7088 would turn it back on. Off means off,
  // and the library's own timed `shutdown(seconds)` clears the alarm first as well.
  //
  // This does not return. The `false` is for the compiler and never reaches the glass.
  // **Aquesta placa té el corrent a la mà** (0.189.18): the hold of the supply is the SY7088 and the pins below, so the
  // shutdown can spend the margin of a card's rest knowing that the current is really going to be cut (docs/50, docs/75).
  bool can_power_off() const override { return true; }
  bool can_sleep_with_the_keys() const override { return true; }

  // **La baixada del Paper, i n'hi ha una de sola** (0.190.15, l'amo: *«el codi d'apagada per deepsleep ha de ser el mateix a tot arreu,
  // tant si salta per timeout d'inactivitat com si l'usuari l'activa manualment… Recoerda que vull despertar per botons físics i per
  // alarma»*). Hi havia **tres** camins per baixar en aquesta placa i no feien el mateix: la política amb una alarma baixava el corrent
  // (`kMainPowerPin` avall, el xip del rellotge armat) però **no reposava el vidre ni baixava els rails del panell i de l'extensió**; la
  // política sense alarma i el botó de la lluna del panell anaven per `sleep_with_the_keys`, que reposava el vidre i baixava els rails
  // però **desarmava el rellotge** —o sigui que una son demanada per una persona s'enduia les alarmes i la placa només tornava amb el dit—;
  // i el cas del cable, on la placa no es pot apagar, queia al xip i prou.
  //
  // Ara tot passa per aquí i el que canvia entre un cas i l'altre és **l'hora, no el camí**: qui demana la baixada diu quan vol tornar
  // (la propera alarma, o zero) i la placa arma **les tres portes que té**:
  //  - **la tecla del dial, GPIO38**: `esp_sleep_enable_ext1_wakeup` prendria diversos pins però els seus dos modes són **tots baixos**
  //    —que demanaria els tres alhora— o **qualsevol alt** —que és el que fa la línia quan ningú la prem, perquè la placa la puja—, o
  //    sigui que un despert de «qualsevol dels tres, premut» no és cap dels seus dos (docs/50). `ext0` és un nivell en un pin del domini
  //    RTC i GPIO38 és aquest pin: és la tecla que la pròpia placa fa servir per engegar-se;
  //  - **el corrent que se'n va**, quan hi ha una hora: el xip del rellotge es queda armat i el seu `INT` —que va al circuit
  //    d'alimentació— torna a engegar la placa. Amb el cable al slot la placa no es pot apagar (ho diu el fabricant), o sigui que es torna
  //    a aguantar el corrent, es desarma el rellotge —una alarma que saltaria amb la placa desperta no vol dir res— i el que la torna és el
  //    temporitzador del xip;
  //  - **el temporitzador del xip**, quan hi ha una hora, que és el que la torna a l'hora dita amb el corrent aguantat.
  //
  // I el que fa tota baixada d'aquesta placa sigui quina sigui l'hora: el vidre reposa, els rails del panell i de l'extensió baixen, el
  // corrent es queda aguantat (`kMainPowerPin` alt) perquè una tecla pugui despertar un xip que si no seria mort, i la línia del registre
  // és la mateixa (docs/50, docs/60).
  bool sleep(SleepMode mode, int64_t at_epoch_ms, bool keep_the_panel = false) override {
    if (mode != SleepMode::kDeep) return Esp32Power::sleep(mode, at_epoch_ms, keep_the_panel);
    const bool has_an_hour = at_epoch_ms > 0;
    // **L'hora que la baixada s'ha menjat no s'arma** (19/11): the margin, the picture and the glass's line are spent before
    // this call, and an hour already gone is a timer of one millisecond with the cable in —a boot for nothing— and a board
    // with no door at all on the battery. The kernel refuses the close ones; this is the last guard, and it says so.
    if (has_an_hour) {
      const int64_t now_ms = esp32_wall_ms();
      if (now_ms > 0 && at_epoch_ms <= now_ms) {
        fprintf(stdout, "power: the hour is already past; the board stays awake\r\n");
        return false;
      }
    }
    Bm8563* chip = paper_chip();
    // **I el dit, si és una porta, mana sobre el corrent** (0.190.16): to hear a finger the touch controller has to stay powered, so the
    // rail of the panel is kept and the board does not cut its own current —the way down is the one that keeps the supply, like the key
    // sleep—. El xip del rellotge es desarma com al cas del cable: una alarma que saltaria amb la placa encesa i amb el panell despert
    // no vol dir res, i qui la torna a l'hora és el temporitzador del xip (docs/50, docs/60).
    const bool the_finger_is_a_door = wake_on_touch_;
    if (the_finger_is_a_door && chip != nullptr) chip->disarm();
    if (!the_finger_is_a_door && chip != nullptr) {
      if (has_an_hour) {
        const int64_t now_ms = esp32_wall_ms();
        if (now_ms > 0 && at_epoch_ms > now_ms && chip->write_epoch_ms(now_ms) && chip->arm_at(at_epoch_ms)) {
          gpio_set_level(static_cast<gpio_num_t>(kMainPowerPin), 0);
          // Still here: the cable is in. Take the alarm back down —it would fire while the board is awake and mean
          // nothing— and hold the power up again, because this board stays on for the sleep that follows and it must not
          // depend on somebody's USB cable to keep doing it.
          gpio_set_level(static_cast<gpio_num_t>(kMainPowerPin), 1);
          chip->disarm();
        }
      } else {
        chip->disarm();  // nobody asked this board to come back by itself
      }
    }
    // Nothing is left in flight when the chip stops its clocks: the line out, and the maker's own wait before the supplies go.
    say_the_way_down(mode, at_epoch_ms);
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));
    if (glass_ != nullptr) glass_->rest();
    // **El rail del panell és el del tàctil** (0.190.16): with the finger as a door it is not cut, because the GT911 lives on it and a
    // controller with no current is a glass that cannot hear anybody. It is the current this option costs, and the reason it is off.
    //
    // **I una baixada que no ha tocat el vidre tampoc no el baixa** (0.190.19, l'amo: *«No baixis els rails, així no es veuen canvis ni es
    // gasta energia»*): the controller keeps its state and the image it is showing, o sigui que el despert següent no ha de reinicialitzar
    // ni repintar res (docs/50, docs/60).
    if (!the_finger_is_a_door && !keep_the_panel) turn_supply_off(kPanelPowerPin);
    turn_supply_off(kExtPowerPin);
    // **El corrent no es toca**: kMainPowerPin stays high, and that is the difference between this sleep and being off when the chip
    // kept the hour (`power_off` is the one that lets it go).
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    // **Les dues portes de sempre**: the key of the dial, always, and the chip's timer when there is an hour.
    esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(kPowerKeyPin), 0);
    if (has_an_hour) esp32_arm_timer_wake(at_epoch_ms);
    // **I la tercera, el dit** (0.190.16): the GT911 pulls its line **low** when a scan has a frame and lets it go between frames —the
    // board pulls it up—, o sigui que la condició és «GPIO36 baix». `ext1` d'un ESP32 només sap «tots baixos» o «qualsevol alt»; amb un
    // sol pin, «tots baixos» **és** «aquest baix», que és exactament el que el xip del tàctil fa (docs/50, docs/95).
    //
    // **I no s'arma fins que la línia s'ha deixat anar** (M5Unified #91, 19/11): the GT911 holds INT low while it has a frame
    // nobody has taken —and it pulses it again while the host does not read—, and it also holds it low in standby; a sleep armed
    // on «GPIO36 low» with the line already low is a board that wakes at once, and again on every attempt. The maker's fix is to
    // take what the controller has, so the line is high, and only then arm the door. A line that does not go high in a second is
    // a finger that is really there: the key and the hour are still doors, and this one is not armed (docs/50, docs/60).
    if (the_finger_is_a_door) {
      ITouch* touch = esp32_gt911(esp32_i2c(kI2cSdaPin, kI2cSclPin), kTouchIntPin);
      const TickType_t started = xTaskGetTickCount();
      while (gpio_get_level(static_cast<gpio_num_t>(kTouchIntPin)) == 0 &&
             (xTaskGetTickCount() - started) < pdMS_TO_TICKS(kTouchReleaseMostMs)) {
        if (touch != nullptr) (void)touch->flush();
        vTaskDelay(pdMS_TO_TICKS(10));
      }
      const char* why_not = nullptr;
      if (gpio_get_level(static_cast<gpio_num_t>(kTouchIntPin)) == 0) {
        why_not = "the glass did not let its line go";
      } else if (esp_sleep_enable_ext1_wakeup(1ULL << kTouchIntPin, ESP_EXT1_WAKEUP_ALL_LOW) != ESP_OK) {
        why_not = "the chip did not take the line";
      }
      if (why_not != nullptr) {
        // **Una porta que no s'ha armat no ha de costar corrent**: the panel's rail was kept for the finger (0.190.16), and a
        // controller that cannot wake the board is current nobody asked for —except on a wake for an alarm, where the rail
        // is kept for the image and not for the finger (0.190.19)—. The line and the arming are said, because a door that
        // nobody armed and the log keeps quiet is a person waiting for a finger that will wake nothing (docs/50).
        if (!keep_the_panel) turn_supply_off(kPanelPowerPin);
        fprintf(stdout, "power: %s; the finger is not a door of this sleep\r\n", why_not);
      }
    }
    esp_deep_sleep_start();  // does not return: the chip restarts on the key, on its timer, on the current or on the glass
    return false;
  }

  bool power_off() override {
    if (Bm8563* chip = paper_chip()) chip->disarm();    // The maker waits 600 ms before the supplies go, so that whatever was still on its way out has
    // finished. This board has no panel painting to wait for, and the number is still theirs.
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(600));
    // **El panell reposa abans que el seu rail baixi** (0.183.0): the maker's `Shutdown()` starts with `M5.EPD.Sleep()`,
    // which is `IT8951_TCON_SLEEP` written to the controller, and this file went straight from the wait to the rails. A
    // controller left awake on a supply that is going away is a controller asking for what it no longer has; the image on
    // the glass stays, because it is pigment and not memory (docs/50).
    if (glass_ != nullptr) glass_->rest();
    turn_supply_off(kPanelPowerPin);
    turn_supply_off(kExtPowerPin);
    gpio_set_level(static_cast<gpio_num_t>(kMainPowerPin), 0);
    // **I el que queda és el son, amb la tecla PWR per tornar** (0.189.24, l'amo: *«…deixant els botons físics actius per
    // despertar l'esp32»*): with the cable in the board **cannot switch itself off** —the maker says so and the hold going low
    // is not enough— so the deep sleep below is what stops it, and a sleep with every wake source disabled is a board only a
    // reset brings back. GPIO38 is a pin of the RTC domain, which is what `ext0` asks for, and the key closes it to ground, so
    // a person who presses PWR for the second the board has always asked for gets it back. On the battery the power is gone
    // before this line and the arming means nothing (docs/50).
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(kPowerKeyPin), 0);
    esp_deep_sleep_start();  // does not return
    return false;
  }

  // The chip's timer while the board is powered, and the clock chip's alarm when it is not:
  // the two ways this board comes back (docs/50).
  const char* wakes() override { return "timer, clock, button"; }

  // **El dit com a porta** (0.190.16): the GT911 is powered from the panel's rail and it scans by itself, o sigui que el que cal per
  // despertar-se amb un dit és **deixar aquell rail ences** i escoltar la seva línia mentre el xip dorm. És un ajust i no el de sèrie
  // perquè tot això és corrent: el panell i el controlador del tàctil encesos tota la son (docs/50, docs/60). **El commutador el guarda
  // la classe base** (0.190.17): aquí només es diu que aquesta placa sí que hi sent.
  bool can_wake_on_touch() const override { return true; }

 private:
  // The glass, for the way down to put to sleep first (0.183.0).
  ISurface* glass_ = nullptr;
};

class PaperPlatform : public IPlatform {
 public:
  const char* id() const override { return board::id(); }
  const char* family() const override { return board::family(); }
  bool emulator() const override { return board::emulator(); }

  // **El corrent d'aquesta placa s'aguanta el primer, i no quan algú pregunta què sap fer** (0.189.24): the SY7088 keeps the
  // board alive while the pin is high and gives up about a second after the key is pressed. This used to happen inside
  // `caps()`, which the boot asks for **after** the splash —a `gc16` of the whole panel—, so on the battery the board switched
  // itself off half way through its own boot and only a cable could start it; the maker's own `M5EPD::begin()` enables the
  // main power as its very first instruction (docs/50, docs/75).
  void hold_the_power() override { hold_power(); }

  // **Per què s'ha reiniciat el xip** (0.190.68, l'amo: *«recorda que l'esp32 te estats al arrencar, així el firmware sabrà si ha
  // despertat normal o d'un deep sleep»*): ho respon el maquinari i no una marca desada, que és el que fa que una arrencada es
  // pugui dir encara que la NVS s'hagi perdut (docs/50, docs/60).
  ResetCause reset_cause() const override { return esp32_reset_cause(); }

  // Built on first use and never in the constructor: this object is constructed
  // before setup() runs, and the power hold wants the GPIO driver awake. Mounting
  // LittleFS that early is not allowed either (the ATOM learned the same thing).
  const Caps& caps() const override {
    if (!built_) {
      built_ = true;
      hold_power();
      // The panel is on the same three wires as the card and comes up selected (see
      // above): out of the way before anything touches the bus (docs/50). **And only while it is not
      // a device of that bus yet** (0.118.9): this writes GPIO15 as a plain output, and doing it once
      // the driver has added the panel as an SPI device (its CS is 15) takes the pad away from the
      // controller —from that moment the panel is never selected again, its reads come back `ffff`
      // and every paint is accepted and shown nowhere. The boot asks `caps()` right after the splash,
      // which is why the logo of the boot was the last page this board ever drew (*«les apps
      // s'executen però no mostren res»*, measured on the board, 7/11) (docs/50, docs/95).
      if (!panel_joined_) deselect_panel();
      // Everything the chip gives —the radio with its provisioning and a page
      // included— and then only what this board has of its own (docs/50).
      caps_ = esp32_caps();
      // The one thing the chip cannot answer for: the filesystem is a partition of
      // this board's table, and it is advertised only when it really mounts.
      if (esp32_storage("/littlefs", kDataPartition) != nullptr) caps_.add(kCapLittlefs);
      // The card slot: advertised only when a card really mounts, the same rule as the
      // filesystem of the flash (docs/70).
      if (esp32_sd("/sd", kSdPins) != nullptr) caps_.add(kCapSd);
      // Three keys, read in every turn of the kernel like the ATOM's one.
      caps_.add(kCapInput);
      // The battery, advertised only when the divider gives a number that could be a
      // cell: the same rule as the card and the disk (docs/50).
      int probe_mv = 0;
      if (esp32_battery(kBatteryPins, probe_mv)) caps_.add(kCapBattery);
      // The air: the SHT30 of the internal bus, advertised only when a whole measurement
      // arrived with its two CRCs good. A sensor that is promised and never answers is
      // worse than one that is not announced (docs/50).
      if (sensor() != nullptr) caps_.add(kCapEnv);
      // The glass, and the capability with it: the chip answers and the kernel turns its frames into
      // taps, long presses and drags, which is what an app needs to choose a path (docs/50).
      if (touch() != nullptr) caps_.add(kCapTouch);
      // The panel: the controller answers and an app can draw on it, so the capability is announced.
      // **The system paints the bar and nothing else** (28 rows at the top): an app's page is the
      // app's, and what it leaves on the glass is what stays there (docs/30, docs/50). Its supply
      // stays on until the power policy of step 8 says otherwise (docs/50), and `display()` is the one
      // accessor here that is not `const`, which is why this one ask needs the cast.
      if (const_cast<PaperPlatform*>(this)->display() != nullptr) caps_.add(kCapDisplay);

    }
    return caps_;
  }

  // The ceiling of an app on this board is its **PSRAM**: the interpreter puts its big blocks there
  // (one from 4 KB up, docs/75) while the small ones stay in the board's own RAM, and the system keeps
  // its own memory for itself. It is **4 096 KB and not the 8 MB the chip has**, because that is what
  // the ESP32 can map: its external RAM window is 4 MB, and the other half of the chip needs the
  // bank-switched `himem` that nothing here uses (measured on the glass, 31/10: `mem` said
  // `extra memory 4067 of 8192 KB free`, which is a 4 MB pool counted against an 8 MB chip).
  uint32_t heap_limit_kb() const override { return 4096; }

  // The extra memory of this board, with the three numbers (docs/75): what the chip says it has
  // (8 MB), what the ESP32 can address of it (4 MB, the size of its window for external RAM) and
  // what is left of that. A number an app can never use is not room, so `total` is the mapped one and
  // the other stays as the honest answer to "what does the datasheet say" (docs/50).
  ExtraMemory extra_memory() const override {
    ExtraMemory out;
    // **La mida del xip i la que s'adreça no són la mateixa** (0.190.173): `esp_psram_get_size()` answers the eight
    // megabytes the chip carries and the heap answers the four that fit in the window; the two were the same number
    // since 0.190.166, when the Arduino header went away and the heap was asked instead, and `mem` said 4096 where the
    // datasheet says 8192 (docs/50, docs/75).
    out.chip_bytes = esp32_chip_psram_bytes();
    out.total_bytes = static_cast<uint32_t>(heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    out.free_bytes = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    if (out.chip_bytes == 0) out.chip_bytes = out.total_bytes;
    // **Els altres quatre megabytes, darrere el banc de commutació** (0.190.173): they cannot be handed
    // out as a pointer —the chip moves them— so they are counted apart, and `mem` says both numbers
    // (docs/50, docs/75).
    const ExtraMemory bank = esp32_extra_memory();
    out.himem_bytes = bank.himem_bytes;
    out.himem_free_bytes = bank.himem_free_bytes;
    out.himem_window_bytes = bank.himem_window_bytes;
    return out;
  }

  bool extra_memory_round_trip(uint32_t bytes) override { return esp32_extra_memory_round_trip(bytes); }

  // **I les sessions hi poden viure** (0.190.174): the caches of the apps are the memory that is read and written in pieces,
  // which is the one shape this memory has (docs/50, docs/75).
  IBankedMemory* open_banked_memory(uint32_t bytes) override { return esp32_open_banked_memory(bytes); }
  void close_banked_memory(IBankedMemory* block) override { esp32_close_banked_memory(block); }

  IClock* clock() override { return paper_clock(); }
  // Two filesystems on this board: the flash and, when there is a card in the slot, the
  // card. Each one answers for its own mount and says no to the other (docs/70).
  IStorage* storage(const char* mount) override {
    // The RTC memory is a file of the chip and not of the board: it is the same 8 KB on every
    // ESP32, and its content dies with the power (docs/50, docs/70).
    if (mount != nullptr && strcmp(mount, "/rtc") == 0) return esp32_rtc_memory();
    // The FRAM is this board's own memory: 256 bytes on the internal I2C that **do not need the
    // power**, so what an app writes there is still there after the battery has gone. It is one
    // file like `/rtc`, and a board whose chip does not answer has no mount at all (docs/50,
    // docs/70).
    if (mount != nullptr && strcmp(mount, "/fram") == 0) {
      return esp32_fram(esp32_i2c(kI2cSdaPin, kI2cSclPin));
    }
    // Whoever mounts first can come before `caps()`, and the panel has to be out of the way before a single byte goes down
    // those wires (see above). **And once the panel is a device of the bus it is not deselected again** (0.151.13): this
    // line wrote GPIO15 as a plain output, which takes the pad away from the SPI controller —the panel is never selected
    // again, its reads come back `ffff` and every paint is accepted and shown nowhere—. `caps()` was guarded against
    // exactly this in 0.118.9 and this call was not: `epub.books()` asks for the storage of the flash **and** of the card,
    // so opening the shelf of the reader left the glass dead for ever (measured on the board, 8/11, docs/50).
    if (!panel_joined_) deselect_panel();
    IStorage* flash = esp32_storage(mount, kDataPartition);
    return (flash != nullptr) ? flash : esp32_sd(mount, kSdPins);
  }
  IKeyValue* nvs() override { return esp32_nvs(); }
  // The same radio the ATOM has, because it is the chip's and not the board's: what is
  // missing here is trying it on this hardware, not the code (docs/90).
  INet* net() override { return esp32_net(); }
  // **I la ràdio que no és una xarxa** (0.190.179): the Bluetooth keyboard, which is an object that says it cannot on a
  // band whose build has no radio in it (docs/50).
  IBluetooth* bluetooth() override { return esp32_bluetooth(); }
  // **I el mesh d'aquesta ràdio** (0.190.212): the node of Bluetooth Mesh, which is the same chip's and another way of using the radio.
  IMesh* mesh() override { return esp32_mesh(); }
  IPower* power() override { return &power_; }
  ILed* led() override { return nullptr; }
  IInput* input() override { return &keys_; }
  IImu* imu() override { return nullptr; }
  // The air around the board, on the internal I2C it shares with the clock, the FRAM and the
  // touch panel. The chip is asked once whether it is there, because a probe is a measurement
  // (docs/50). `const` because the capabilities ask it too, and a capability is a question about a
  // board that is not changed by asking.
  ISensor* sensor() const override { return esp32_sht30(esp32_i2c(kI2cSdaPin, kI2cSclPin)); }

  // The glass of this board, on the same two wires as the clock chip, the FRAM and the air sensor
  // (docs/50).
ITouch* touch() const override { return esp32_gt911(esp32_i2c(kI2cSdaPin, kI2cSclPin), kTouchIntPin); }

  // The panel and its controller, on the wires the card uses (docs/50). The driver powers it and
  // says what it answered; **nothing paints yet**, because a paint needs something to paint and the
  // drawing surface is the next step (docs/90).
  ISurface* display() override {
    ISurface* glass = esp32_it8951(It8951Pins{kPanelCsPin, kPanelSckPin, kPanelMosiPin, kPanelMisoPin,
                                               kPanelBusyPin, kPanelPowerPin});
    // **From the moment it is a device of the bus, GPIO15 belongs to the driver** (0.118.9): the
    // deselect of the boot (`capabilities`) must not take it back, or the panel stops being selected
    // for the rest of the session (see there).
    panel_joined_ = glass != nullptr;
    // **I el power sap on és el vidre** (0.183.0): the way down puts the panel to sleep before the rails go, which is the
    // maker's own order (docs/50).
    power_.keep_glass(glass);
    return glass;
  }

 private:
  mutable Caps caps_;
  mutable bool built_ = false;
  // Whether the panel is already a device of the SPI bus it shares with the card, which is what says
  // if GPIO15 is still this file's to park or already the driver's (0.118.9).
  mutable bool panel_joined_ = false;
  // No key of this board wakes the chip: they switch the main power off instead of
  // waking it, so the timer is the only way back (docs/50).
  PaperPower power_;
  // The dial is this board's; the reading of its three pins is not (docs/50).
  GpioKeys keys_{kKeys, sizeof(kKeys) / sizeof(kKeys[0])};
};

PaperPlatform g_platform;

}  // namespace

IPlatform& platform() { return g_platform; }

}  // namespace hal
