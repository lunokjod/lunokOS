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
// **La capçalera del nucli d'Arduino, només quan el nucli hi és** (0.190.90): aquest fitxer no en fa servir res —la consola és del
// PAL i el panell d'epdiy— i l'entorn d'ESP-IDF no la té (docs/50).
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <esp_timer.h>
// **La mida de la PSRAM, dita per qui la sap** (0.190.90): el nucli d'Arduino la porta a `esp32/spiram.h` i la IDF a `esp_psram.h`; el
// número és el mateix i el que canvia és el nom de la crida (docs/50).
#include <esp_psram.h>

#include <cstdio>
#include <cstring>

#include <driver/spi_master.h>
#include <memory>

#include "board/identity.h"
#include "esp32/backlight.h"
#include "esp32/bm8563.h"
#include "esp32/caps.h"
#include "esp32/clock.h"
#include "esp32/gpio_keys.h"
#include "esp32/gt911.h"
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
#include "esp32/storage.h"
#include "hal/hal.h"
#include "hal/log.h"
#include "t5s3/epd47.h"
#include "t5s3/expander.h"
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

// **L'expansor de ports, que és la meitat del panell** (0.190.33): the sixteen pins of a PCA9535 carry the panel's output
// enable and mode, the four gates of its supply and the button of the board, o sigui que sense aquest xip el vidre no
// existeix. Its inputs are the supply's `POWER_GOOD` and interrupt and the button (docs/50).
const uint8_t kExpanderAddress = 0x20;
// **La interrupció de l'expansor** (0.190.99): the PCA9535 answers on GPIO38 and it goes low when any of its inputs moves —the board's
// button `S3`, the supply's `POWER_GOOD` and the supply's `TPS_INT`—. És la manera de no preguntar-li res per I²C quan no ha passat
// res, i, com que no és una RTC GPIO, serveix per despertar del son lleuger i no del profund (docs/50).
const int kExpanderIntPin = 38;
// **La llum de fons**: GPIO11 va a l'`EN` de l'elevador PT4103B23F, i el PWM és com es regula (docs/50).
const int kBacklightPin = 11;
// **Els dos resets que la placa té i que el son ha de subjectar** (0.190.272, els pins del fabricant: `BOARD_TOUCH_RST` 9 i
// `BOARD_LORA_RST` 1): en un son profund el domini digital s'apaga i els pads queden flotants, i el fabricant els deixa baixos
// i subjectats al seu `ui_sleep()`. El tàctil és el mateix que el driver no governa (només en toca la interrupció) i el LoRa ja
// es queda fora del bus a l'arrencada (`kSdQuietPins`), o sigui que el que faltava era subjectar-los (docs/50).
const int kTouchResetPin = 9;
const int kLoraResetPin = 1;
// **Què és entrada i què és sortida, en el registre de configuració del propi xip** (0.190.34, i és la segona trampa
// d'aquesta placa): en un PCA9535 un `1` en aquest registre vol dir **entrada**, o sigui que `0xC400` són les tres
// entrades de la placa —IO1_2 el botó `S3`, IO1_6 el `PWR_GOOD` de la font i IO1_7 la interrupció de la font— i tota la
// resta sortides. **Aquesta màscara s'escriu tal com és**: the first version of this file negated it at the call site
// («the mask is the config register» in one place and «the mask is the inputs» in the other), o sigui que el xip va
// quedar amb els cinc pins de l'alimentació com a entrades i amb el `PWR_GOOD` com a sortida —el panell no despertava
// mai i la font no contestava a 0x68— i és el que va fer el log de la primera llum (docs/50, docs/95).
const uint16_t kExpanderInputs = 0xC400;

// **La segona porta**: the PCF8563's interrupt line, on GPIO2. A PCF8563 pulls it low when its alarm
// or its countdown fires and lets it go when the flags are cleared, o sigui que és una porta del son
// profund exactament com la tecla —una línia oberta que baixa— i el xip d'aquesta placa les pot vigilar
// totes dues alhora amb `ext1` en el seu mode «qualsevol baixa» (docs/50, docs/60).
const int kClockIntPin = 2;

// **El rellotge del xip del rellotge**: the PCF8563 has a coin cell behind it and keeps the hour
// across the cable, and its registers are the ones the BM8563 driver already speaks (both are
// PCF8563 clones). Its own interrupt line is GPIO2, which is a wake pin of the chip and the door
// this board will have when the sleep is taught to arm it (docs/50, docs/60).
const uint8_t kClockAddress = 0x51;

// **Els dos xips que comparteixen SPI2 amb la targeta** (0.190.83, i el seu exemple ho diu: *«LORA、SD use the same spi, in
// order to avoid mutual influence; before powering on, all CS signals should be pulled high and in an unselected state»*): el
// CS del LoRa i el seu reset, i tots dos han de quedar fora del bus abans de la primera transferència de la targeta. Sense
// això el mòdul de ràdio, amb el CS flotant, responia per MISO i el muntatge fallava amb `sdmmc_io_reset: unexpected return:
// 0x104` — un arrencada sí i una no (docs/50, docs/70).
const int kSdQuietPins[] = {46, 1};        // LORA_CS, LORA_RST
const uint8_t kSdQuietLevels[] = {1, 0};   // el CS alt i el reset baix: la ràdio no hi és
// The card slot, on its own SPI bus —the panel is parallel and does not use it— (docs/50, docs/70).
const SdPins kSdPins = {12, 14, 13, 21, SPI2_HOST, 10000, kSdQuietPins, kSdQuietLevels,
                        sizeof(kSdQuietPins) / sizeof(kSdQuietPins[0])};

// The label of the data partition in partitions_16mb.csv: the filesystem is found by it, never by
// the core's default (docs/70).
const char kDataPartition[] = "littlefs";

// **On és el botó de debò, i no on el posa un full de dades** (0.190.104, l'amo: *«el botó físic de veritat que té la placa, està
// físicament a la cara dreta, a 1/3 d'alçada comptant des de baix»*). El descriptor és el mateix que fa servir el Paper per al seu dial
// —una **cara** de la caixa, un **lloc** dins d'aquella cara, com està fet i de quin grup—, i serveix per a dues coses que no són la
// pintada: una app dona una funció a una posició (`button.list()`, `face` i `spot`), i l'emulador posa la columna de tecles al costat
// que la cara diu —una fila sota la pàgina si la cara és la de dalt, una columna a la dreta si és la dreta— (docs/30, docs/50, docs/85).
//
// **I el botó de debò no és cap pin del xip** (0.190.104, l'amo: *«la S3 de l'expansor és el botó de debò; el BOOT només serveix per al
// mode de descàrrega»*). El que una persona prem en aquesta placa penja del **PCA9535**, i el descriptor —el mateix vocabulari que el
// dial del Paper: una **cara** de la caixa, un **lloc** dins d'aquella cara, com està fet i de quin grup— el posa on és de veritat:
// *«el botó físic de veritat que té la placa, està físicament a la cara dreta, a 1/3 d'alçada comptant des de baix»*. Serveix per a dues
// coses que no són la pintada: una app dona una funció a una posició (`button.list()`) i l'emulador posa la columna de tecles al costat
// que la cara diu —a la dreta, i no una fila sota la pàgina— (docs/30, docs/50, docs/85).
//
// **`kRight` i `kDown` i no `kTop` i `kCentre`**, que és el que hi havia: el botó no és a la cara de dalt —el costat pel qual s'endolla
// el cable— sinó a la dreta, i no és al mig de la caixa sinó **a un terç de l'alçada des de baix**, que és el lloc de baix dels tres que
// el vocabulari té en una cara vertical (el dial del Paper és, exactament, amunt, centre i avall sobre la dreta).
const KeySpot* const kNoKeysThere = nullptr;

// **El vidre, per a qui ha de dormir-lo** (0.190.105): the way down needs the touch controller —to put it to sleep when nobody is going to
// listen for a finger, and to clean its line when somebody is— and the driver is the singleton the platform's own `touch()` returns.
// Es declara abans de la classe de potència perquè el seu `arm_the_doors` el demana (docs/50).
ITouch* t5s3_touch() { return esp32_gt911(esp32_i2c(kI2cSdaPin, kI2cSclPin), kTouchIntPin); }

// **Una porta que ja és oberta desperta el xip a l'instant** (0.190.121, l'amo: *«Al activar l' sleep amb el táctil connectat (settings->battery), el
// dispositiu es desperta de seguida»*). El son profund d'aquesta placa s'arma amb `ext1` i **`ANY_LOW`**, o sigui que la pregunta no és «hi ha un dit»
// sinó «alguna d'aquestes línies és baixa ara mateix»: el dit que acaba de prémer el commutador a la pàgina dels ajustos, o el marc que el controlador
// encara no ha lliurat, fan que el xip torni abans d'haver-se adormit —una vegada i una altra—. El que lleva la línia és **llegir el marc i
// tornar-li l'estat** (`flush`), i el que diu si ja és alta és el pin; per això això no és una crida sinó un bucle: s'allibera, es mira, i si el dit
// encara hi és s'espera una mica més, amb un topall que és el que una persona triga a aixecar-lo. Si no puja, el tàctil **no és una porta
// d'aquest son**: val més tornar pel rellotge i per la tecla que no dormir gens (docs/50, docs/60).
// **Un segon és el que es dona a un dit per aixecar-se**, i és el mateix número que la Paper (0.190.16, `kTouchReleaseMostMs`): dues bandes, un sol
// criteri per a la mateixa pregunta (docs/50).
const uint32_t kTouchReleaseMostMs = 1000;

// **El que la placa recorda d'una dormida** (0.190.149): the RTC RAM is the only memory that survives a deep sleep —the domain is left on
// for the doors—, and this is the one place where the answer to «why did it come back at once?» can wait for the person who plugs the
// console back in. Un bit per porta, armada o trobada oberta; es buida quan s'ha dit (docs/50, docs/60).
RTC_DATA_ATTR static uint8_t g_doors_armed = 0;
RTC_DATA_ATTR static uint8_t g_doors_open = 0;
// **Tot el que una sola prova ha de dir** (0.190.151, l'amo: *«posa tot el que necessiti en aquesta prova, no en vull fer més»*): the raw
// level of the three lines at the moment of arming —a line that is low is a door already open and the wake is immediate—, how long the board
// had been up, and how many sleeps it has done in a row. Amb això i la causa del reset, una sola dormida diu si el xip no s'ha adormit, si
// s'ha adormit i ha tornat per una línia, o si l'alimentació l'ha deixat anar (docs/50, docs/60, docs/95).
RTC_DATA_ATTR static uint8_t g_lines_low = 0;   // bit 0 clock, 1 key, 2 glass: 1 = the line was low
RTC_DATA_ATTR static uint32_t g_up_at_arming = 0;
RTC_DATA_ATTR static uint32_t g_sleeps_in_a_row = 0;

const uint8_t kDoorClock = 1;
const uint8_t kDoorKey = 2;
const uint8_t kDoorGlass = 4;

// **Un buffer per crida i no una estàtica** (0.190.149): the line says the doors it armed **and** the ones it found open, and a static
// shared by the two calls would print the second one twice —a line that lies about the very thing it exists to explain (docs/50).
void doors_words(uint8_t bits, char* said, size_t cap) {
  if (said == nullptr || cap == 0) return;
  if (bits == 0) {
    snprintf(said, cap, "nothing");
    return;
  }
  said[0] = '\0';
  size_t used = 0;
  const struct {
    uint8_t bit;
    const char* word;
  } kWords[] = {{kDoorClock, "clock"}, {kDoorKey, "key"}, {kDoorGlass, "glass"}};
  for (const auto& one : kWords) {
    if ((bits & one.bit) == 0) continue;
    const size_t len = strlen(one.word);
    if (used + len + 2 >= cap) break;
    if (used > 0) said[used++] = '+';
    memcpy(said + used, one.word, len);
    used += len;
    said[used] = '\0';
  }
}

bool the_line_is_free(ITouch* glass, int pin) {
  const int64_t deadline = pal::system().millis() + static_cast<int64_t>(kTouchReleaseMostMs);
  for (;;) {
    glass->flush();
    if (gpio_get_level(static_cast<gpio_num_t>(pin)) != 0) return true;
    if (pal::system().millis() >= deadline) {
      logf(pal::kLogWarn, "POWER",
           "the line of the glass (%d) is still low: this deep sleep wakes on the clock and the button only", pin);
      return false;
    }
    pal::delay_us(10000);
  }
}

// The sleep of the chip, with the BOOT key as the way back, and the gauge of this board in place of
// the divider the Paper reads (docs/50).
class T5Power : public Esp32Power {
 public:
  // **Quatre portes del son lleuger** (0.190.99): the BOOT of the chip —que no és una tecla del sistema però sí un botó de debò—, the
  // clock's interrupt, and the line of the expander —the button `S3` and the supply's `POWER_GOOD`—, which is not an RTC pin and o sigui
  // que no pot despertar d'un son profund (docs/50, docs/60).
  T5Power(int wake_pin, int second_wake_pin, int glass_pin, int more_pin)
      : Esp32Power(wake_pin, second_wake_pin, glass_pin, more_pin) {}

  // **A deep sleep here does not cut the board's own supply**: there is no latch to let go, the
  // cell feeds the chip and the panel's rails are the TPS65185's business. So `deep_powers_off` is
  // false, and the policy does not treat this board as one that is powered off between two alarms
  // (docs/50, docs/60).
  bool deep_powers_off() const override { return false; }
  // **I la tecla que torna d'un son profund és el BOOT del xip** (0.190.108, l'amo: *«El botó fisic no desperta de deepsleep. Revisa els
  // exemples del fabricant»*). El botó de debò de la placa —el `S3` de l'expansor— **no pot** ser una porta: la seva línia és `PCA_INT` a
  // GPIO38 i GPIO38 no és cap GPIO de l'RTC; el fabricant ho va provar i ho va deixar comentat al seu exemple (`// esp_sleep_enable_ext1_wakeup((1UL <<
  // KEY_BTN), ...)`), i el que va deixar armat és això: `esp_sleep_enable_ext0_wakeup(BOARD_BOOT_BTN, 0)`, o sigui **GPIO0, nivell baix**.
  // Aquest fitxer fa el mateix, amb el rellotge i el dit al mateix `ext1` (docs/50, docs/60).
  bool can_sleep_with_the_keys() const override { return true; }


  // **Les portes del son profund, i què té cadascuna** (0.190.102 i 0.190.108, l'amo: *«segur? no desperta mai»*). Waking from a deep
  // sleep is a **reset**, i el xip torna a mirar els seus *strapping* en aquell reset: GPIO0 és el BOOT, o sigui que la mateixa tecla que
  // demana tornar el deixa **baix** just quan el xip decideix què arrencar, i una premuda llarga pot fer que la placa arrenqui en **mode
  // de descàrrega de la ROM** —la pantalla no canvia, la consola calla i la placa sembla morta fins que un gravador hi parla—. No és cap
  // error de la IDF ni del codi: és el pin.
  //
  // **I tot i així el BOOT s'arma**, i això és una decisió i no un descuit: és el que arma el fabricant al seu `ui_sleep()`
  // (`esp_sleep_enable_ext0_wakeup(BOARD_BOOT_BTN, 0)`), i sense ell una placa sense alarma i amb «Wake on touch» apagat **no tindria
  // cap porta** i es negaria a dormir (`arm_the_doors` amb la màscara a zero). Les altres dues són les línies que la placa cableja a GPIOs
  // de l'RTC i que es queden **baixes mentre volen dir alguna cosa**: l'alarma del rellotge (GPIO2) i el controlador del tàctil mentre un
  // dit és a sobre del vidre (GPIO3, que en repòs és alt i baixa amb el contacte). O sigui que aquesta placa torna amb un toc al vidre, amb
  // l'alarma, o amb el BOOT —i el BOOT és l'última, i demana un toc curt (docs/50, docs/60).
  size_t describe_the_last_sleep(char* out, size_t cap) override {
    if (out == nullptr || cap == 0) return 0;
    if (g_doors_armed == 0 && g_doors_open == 0 && g_up_at_arming == 0) return 0;  // a boot that did not come from a sleep
    char armed[32] = {0};
    char open[32] = {0};
    doors_words(g_doors_armed, armed, sizeof(armed));
    doors_words(g_doors_open, open, sizeof(open));
    snprintf(out, cap, "last sleep #%u after %u ms up: armed %s, open %s, low lines c%d k%d g%d",
             static_cast<unsigned>(g_sleeps_in_a_row), static_cast<unsigned>(g_up_at_arming), armed, open,
             (g_lines_low & kDoorClock) ? 1 : 0, (g_lines_low & kDoorKey) ? 1 : 0, (g_lines_low & kDoorGlass) ? 1 : 0);
    return strlen(out);
  }

  bool arm_the_doors() override {
    // **I les línies han de tenir el seu alt mentre el xip dorm** (0.190.102): the PCF8563's INT is **open drain** and the GT911's is
    // driven, i en un son profund els pads del domino digital ja no hi són — el que compta és la resistència de l'RTC, que ve apagada de
    // fàbrica—. Sense això una línia que flota es pot llegir com un zero i la placa es desperta tota sola; i per això el domini de l'RTC
    // es queda encès, que és el preu de tenir portes (docs/50, docs/60).
    rtc_gpio_pullup_en(static_cast<gpio_num_t>(kClockIntPin));
    rtc_gpio_pulldown_dis(static_cast<gpio_num_t>(kClockIntPin));
    // **El dit és una porta només si una persona ho ha demanat** (0.190.105, l'amo: *«Revisa la funció de Settings->Battery->Wake on
    // touch»*): listening for a finger means keeping the touch controller scanning and powered for the whole sleep, which is exactly the
    // current a deep sleep exists to stop —the switch of the Battery tab is that decision and it says so in its own words—. Amb el
    // commutador apagat **el controlador s'adorm** (`writeCommand(0x05)`, el mateix que el driver del fabricant) i la placa només torna
    // pel rellotge; amb el commutador encès es queda en el seu règim de menys consum (deu escombrats per segon, la línia en nivell baix) i
    // la seva línia és la porta (docs/30, docs/50, docs/60).
    // **Les tres portes d'aquesta placa, i la del fabricant** (0.190.108): the clock's alarm, the chip's BOOT key —`ext0` at level low in the
    // maker's own `ui_sleep()`, que és l'única tecla d'aquesta placa que arriba a un pin de l'RTC—, and the glass when «Wake on touch» says so.
    // El BOOT té la resistència de pujada de la placa (és un *strapping*), i s'hi posa també la de l'RTC perquè en el son el domini digital no
    // hi és (docs/50, docs/60).
    rtc_gpio_pullup_en(static_cast<gpio_num_t>(kBootKeyPin));
    rtc_gpio_pulldown_dis(static_cast<gpio_num_t>(kBootKeyPin));
    // **I una porta que ja és oberta no s'arma** (0.190.148, l'amo: *«l'Sleep continua despertant inmediatament... però només el primer
    // cop... el segon dorm bé»*). `ANY_LOW` amb una línia que ja és baixa no és una porta: és un despertar immediat, i la placa es
    // reiniciava a l'instant la primera vegada que s'adormia amb una d'elles al terra —el flag d'una alarma que el rellotge encara no
    // havia deixat anar, o una tecla premuda— i la segona anava bé perquè el reinici havia netejat el flag. Això és el que fa la
    // becaina des de la 0.190.69 (docs/50, docs/60).
    uint64_t mask = 0;
    g_doors_armed = 0;
    g_doors_open = 0;
    if (gpio_get_level(static_cast<gpio_num_t>(kClockIntPin)) != 0) {
      mask |= 1ULL << kClockIntPin;
      g_doors_armed |= kDoorClock;
    } else {
      g_doors_open |= kDoorClock;
    }
    if (gpio_get_level(static_cast<gpio_num_t>(kBootKeyPin)) != 0) {
      mask |= 1ULL << kBootKeyPin;
      g_doors_armed |= kDoorKey;
    } else {
      g_doors_open |= kDoorKey;
    }
    ITouch* glass = t5s3_touch();
    if (wake_on_touch() && glass != nullptr) {
      // **El vidre baixa al terra abans de dormir** (0.190.107): whether the glass is adaptive or at its top speed while the board is awake,
      // a deep sleep that is only waiting for a line to fall does not need a hundredth of a second of resolution (docs/50, docs/60).
      glass->quiet_now();
      if (the_line_is_free(glass, kTouchIntPin)) {
        rtc_gpio_pullup_en(static_cast<gpio_num_t>(kTouchIntPin));
        rtc_gpio_pulldown_dis(static_cast<gpio_num_t>(kTouchIntPin));
        mask |= 1ULL << kTouchIntPin;
        g_doors_armed |= kDoorGlass;
      } else {
        g_doors_open |= kDoorGlass;
      }
    }
    // **I les altres dues portes, dites quan ja són obertes** (0.190.121): `ANY_LOW` despertaria el xip per qualsevol de les tres línies, o sigui que
    // una alarma que el rellotge encara no ha deixat anar —o un BOOT que ja és premut— té el mateix efecte que el dit d'abans i no es veu enlloc.
    // La línia només surt quan és baixa, que és quan explica alguna cosa (docs/50, docs/60).
    if (gpio_get_level(static_cast<gpio_num_t>(kClockIntPin)) == 0) {
      logf(pal::kLogWarn, "POWER", "the clock's line (%d) is low before the sleep: it is a door that is already open", kClockIntPin);
    }
    if (gpio_get_level(static_cast<gpio_num_t>(kBootKeyPin)) == 0) {
      logf(pal::kLogWarn, "POWER", "the key's line (%d) is low before the sleep: it is a door that is already open", kBootKeyPin);
    }
    // **I sense cap porta no es dorm** (0.190.148): a deep sleep that nothing can undo is a board that a person has to unplug, i el
    // que s'ha de veure és per què les tres són obertes i no una placa que es reinicia sola en bucle (docs/50, docs/60).
    if (mask == 0) {
      logf(pal::kLogWarn, "POWER",
           "every door of the deep sleep is already open: the board stays awake instead of coming back at once");
      return false;
    }
    // **I l'estat cru de les tres línies, que és el que una sola prova necessita** (0.190.151): the level of each line **before** anything is
    // armed, so a wake that comes at once can be told from a sleep that was never taken without asking for a second run.
    g_lines_low = 0;
    if (gpio_get_level(static_cast<gpio_num_t>(kClockIntPin)) == 0) g_lines_low |= kDoorClock;
    if (gpio_get_level(static_cast<gpio_num_t>(kBootKeyPin)) == 0) g_lines_low |= kDoorKey;
    if (gpio_get_level(static_cast<gpio_num_t>(kTouchIntPin)) == 0) g_lines_low |= kDoorGlass;
    g_up_at_arming = static_cast<uint32_t>(pal::system().millis());
    ++g_sleeps_in_a_row;
    // **El son profund no hereta res de la becaina** (0.190.152, l'amo: *«Segur que no es continua enganxant a l'UART per despertar?»*). La
    // becaina arma la UART (`esp_sleep_enable_uart_wakeup`) i aquest camí només afegia el seu `ext1`: o sigui que un xip que havia fet una
    // becaina abans tornava per activitat al port —el cable, el monitor, un DTR— en lloc de dormir, i l'endemà la segona dormida anava bé
    // perquè la becaina ja estava refusada i no armava res. Es neteja **tot** i s'arma només el que aquest fitxer vol (docs/50, docs/60).
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    const bool armed = esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW) == ESP_OK;
    // **El controlador s'adorm només si la placa dorm de debò** (0.190.105): sleeping the chip and then staying awake —a door that did
    // not take— would leave a board whose glass answers nothing, which is worse than one that draws a milliamp. So the order is the
    // arming first and the controller after it, i si el xip no les pren, es diu (docs/50).
    if (!armed) {
      logf(pal::kLogWarn, "POWER", "the chip did not take the doors of the deep sleep (%d and the glass)", kClockIntPin);
      return false;
    }
    if (!wake_on_touch() && glass != nullptr) glass->sleep_now();
    return true;
  }

  // **El commutador de la pestanya Battery té feina en aquesta placa** (0.190.105): the board can hear a finger while its chip sleeps
  // —the glass's line is on an RTC pin and the controller stays powered at its lowest rate— and that is what `can_wake_on_touch` says.
  // El que decideix si s'arma és el commutador (`wake_on_touch()`, que el nucli hi posa des de `cfg.touch_wake`) (docs/30, docs/50).
  bool can_wake_on_touch() const override { return true; }
  // **I el corrent sí que se'l pot tallar** (0.190.33, la trampa d'aquesta placa): the charger's `BATFET_DIS` bit is the
  // only switch between its cell and the board —the maker's `LilyGoT5_Shutdown` example is that one write—, o sigui que
  // `poweroff` aquí és una apagada de debò i no un son. **Amb el cable a dins no fa res**, perquè la placa no es pot
  // desconnectar d'una font que no és la seva (docs/50).
  //
  // **Amb el cable a dins no es pot, i el fabricant ho diu**: `PPM.shutdown()` és el mateix bit i el seu exemple de la fàbrica
  // comença preguntant `battery_25896_is_vbus_in()` i, si hi ha cable, **no apaga res i ho escriu al vidre** —*«The shutdown
  // function can only be used when the battery is connected alone, and cannot be shut down when connected to USB»*—. Sense
  // aquesta pregunta, `poweroff` amb el cable escrivia el bit, no passava res i el nucli cridava `kernel::shutdown()`: la placa
  // es quedava viva, muda i amb l'última imatge, i només en sortia amb el QON o traient el cable (0.190.272, l'amo: *«aquesta
  // versió no sembla fer-ho bé»*) (docs/50, docs/60).
  bool can_power_off() const override { return !gauge_.on_a_cable(); }

  // **La baixada d'aquesta placa, que no és la del xip i prou** (0.190.272). El camí comú d'un son profund d'ESP32 arma les
  // portes i atura els rellotges, i aquí això deixa tres coses enceses que el fabricant apaga a mà al seu `ui_sleep()`:
  //
  //   - **la llum del vidre**, que és un PWM i que en el son queda en el que la persona va triar —el fabricant hi escriu un
  //     zero (`analogWrite(BOARD_BL_EN, 0)`) i després subjecta els pins—;
  //   - **els rails del panell**, que el TPS65185 té amunt perquè s'acaba de pintar la imatge del son i `Epd47::poll` —el
  //     torn dels dos segons— ja no tornarà a córrer: el fabricant hi crida `epd_poweroff()` i la imatge es queda igual,
  //     perquè la tinta és pigment i no memòria (docs/50);
  //   - **els resets del tàctil i del LoRa**, que sense cap hold queden flotants mentre el domini digital és apagat, i el
  //     fabricant els subjecta amb `gpio_hold_en` i `gpio_deep_sleep_hold_en()`.
  //
  // **I el que es pinta a la pantalla no es toca quan la baixada és d'una alarma** (0.190.19): `keep_the_panel` vol dir que
  // algú tornarà a aquesta imatge sense ningú al davant, o sigui que els rails es queden on són i el despert següent no ha de
  // reinicialitzar el panell ni repintar res (docs/50, docs/60).
  bool sleep(hal::SleepMode mode, int64_t at_epoch_ms, bool keep_the_panel) override {
    if (mode == hal::SleepMode::kDeep) {
      // **La llum, a zero, abans que res**: the pins lose their state with the chip and a floating `EN` of the boost is a
      // backlight nobody decided about (docs/50).
      if (ILed* light = hal::platform().led()) {
        light->fill(0, 0, 0);
        light->show();
      }
      if (!keep_the_panel) {
        if (ISurface* glass = hal::platform().display()) {
          if (glass->rest()) logf(pal::kLogDebug, "PANEL", "the panel's supply is down for the sleep");
        }
      }
      // **Els dos resets que el fabricant subjecta**: el del tàctil (GPIO9) i el del LoRa (GPIO1), tots dos baixos, i el hold
      // del son profund perquè el que s'ha escrit no es perdi quan el domini digital s'apaga (docs/50).
      gpio_set_direction(static_cast<gpio_num_t>(kTouchResetPin), GPIO_MODE_OUTPUT);
      gpio_set_level(static_cast<gpio_num_t>(kTouchResetPin), 0);
      gpio_hold_en(static_cast<gpio_num_t>(kTouchResetPin));
      gpio_set_direction(static_cast<gpio_num_t>(kLoraResetPin), GPIO_MODE_OUTPUT);
      gpio_set_level(static_cast<gpio_num_t>(kLoraResetPin), 0);
      gpio_hold_en(static_cast<gpio_num_t>(kLoraResetPin));
      gpio_deep_sleep_hold_en();
    }
    return Esp32Power::sleep(mode, at_epoch_ms, keep_the_panel);
  }
  bool power_off() override {
    if (gauge_.on_a_cable()) {
      logf(pal::kLogWarn, "POWER",
           "the board cannot switch itself off on a cable: the charger's supply keeps it up (the maker's own rule)");
      return false;
    }
    return gauge_.shut_down();
  }
  bool battery(int& mv, int& pct, bool& charging) override { return gauge_.read(mv, pct, charging); }
  // **I el que el comptador de coulombs sap de més** (0.190.155): the current going in or out of the cell and the capacity that is
  // left, which a board with a divider on an ADC cannot say. The gauge answers false when the bus is not there (docs/50, docs/60).
  bool battery_detail(BatteryDetail& out) override { return gauge_.detail(out); }
  // **I el carregador** (0.190.156): the other half of the battery's picture, which the same board and the same two wires answer.
  bool charger_detail(ChargerDetail& out) override { return gauge_.charger(out); }
  // **El que de debò la desperta** (0.190.108): la tecla del xip (el BOOT), el rellotge i —si una persona ha encès «Wake on touch»— el dit.
  // El temporitzador del xip és el quart, quan hi ha una hora (docs/50, docs/60).
  const char* wakes() override { return wake_on_touch() ? "timer, button, clock, glass" : "timer, button, clock"; }
  // **Aquesta placa no fa becaines, i és la consola qui ho diu** (0.190.63): its console is the chip's own USB (`ARDUINO_USB_CDC_ON_BOOT`),
  // and a light sleep stops that peripheral: the prompt would go deaf for every nap and the bytes typed meanwhile are gone. Until the USB
  // side of a nap is worked out, the honest answer is that this board waits awake (docs/50, docs/95).
  bool can_nap() const override { return false; }
  // **I la consola no és una porta d'aquesta placa** (0.190.152): the console is the chip's USB, which a light sleep stops, o sigui que
  // armar `esp_sleep_enable_uart_wakeup` només serviria per despertar el xip per un pin que no hi és (docs/50, docs/60).
  bool the_console_can_wake() const override { return false; }

 private:
  T5Gauge gauge_{esp32_i2c(kI2cSdaPin, kI2cSclPin)};
};

class T5Platform : public IPlatform {
 public:
  // **I en aquesta placa una pintada és feina** (0.190.39, l'amo: *«el driver no ha de tocar la velocitat, cada dispositiu
  // decideix»*): l'ED047TC1 no té controlador, o sigui que el xip **és** el driver —converteix el quadre en els codis del
  // panell fila a fila i alimenta el i80 a cada fila— i la mateixa pàgina sencera mesurada aquí costa **2,67 s a 80 MHz i
  // 1,2 s a 240**. La resposta és de la placa i no del vidre ni del driver: dues plaques poden portar el mateix panell i el
  // mateix driver i no costar el mateix, perquè el xip, el bus i el perifèric són d'ella (docs/50, docs/95).
  bool a_paint_is_work() const override { return true; }
  const char* id() const override { return board::id(); }
  const char* family() const override { return board::family(); }
  bool emulator() const override { return board::emulator(); }

  // **Per què s'ha reiniciat el xip** (0.190.68, l'amo: *«recorda que l'esp32 te estats al arrencar, així el firmware sabrà si ha
  // despertat normal o d'un deep sleep»*): ho respon el maquinari i no una marca desada, que és el que fa que una arrencada es
  // pugui dir encara que la NVS s'hagi perdut (docs/50, docs/60).
  ResetCause reset_cause() const override { return esp32_reset_cause(); }

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
      // **El vidre**: the panel answers —the supply says its power is good and the bus is there— and from that moment an
      // app can draw on it (docs/50). It is the last one because it is the most expensive question of this function: it
      // brings up the parallel bus, the supply and a frame of 259 200 bytes (docs/75).
      if (const_cast<T5Platform*>(this)->display() != nullptr) caps_.add(kCapDisplay);
      // **I la llum de fons hi és** (0.190.99): the supply took the PWM, o sigui que una app pot demanar-ne la intensitat com demana la
      // d'una tira. Es pregunta després del vidre perquè una placa sense vidre tampoc no té res a il·luminar (docs/30, docs/50).
      if (const_cast<T5Platform*>(this)->led() != nullptr) caps_.add(kCapLed);
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
    out.chip_bytes = static_cast<uint32_t>(esp_psram_get_size());
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
  // **I la ràdio que no és una xarxa** (0.190.179): the Bluetooth keyboard, which is an object that says it cannot on a
  // band whose build has no radio in it (docs/50).
  IBluetooth* bluetooth() override { return esp32_bluetooth(); }
  // **I el mesh d'aquesta ràdio** (0.190.212): the node of Bluetooth Mesh, which is the same chip's and another way of using the radio.
  IMesh* mesh() override { return esp32_mesh(); }
  IPower* power() override { return &power_; }
  // **El vidre** (0.190.33): l'expansor obre les portes del panell i el TPS65185 li dona les tensions, o sigui que el
  // driver dels dos és el que el construeix. **I es pregunta de debò** (0.190.34): a panel whose bus or frame did not
  // come up answers nothing and this says `nullptr` with the reason in the log, perquè una placa sense vidre ha de
  // sortir com una placa sense vidre i no com una placa que pinta en un lloc on no hi ha res (docs/50).
  ISurface* display() override {
    ISurface* panel = t5s3_epd47(expander(), esp32_i2c(kI2cSdaPin, kI2cSclPin));
    auto* glass = static_cast<Epd47*>(panel);
    if (glass == nullptr || !glass->begin()) {
      logf(pal::kLogWarn, "PANEL", "%s", glass != nullptr ? glass->last_error() : "there is no panel");
      return nullptr;
    }
    return panel;
  }
  // **La llum de fons, que és un LED** (0.190.99, l'amo: *«tenim interficie per leds a lunokOS, ja saps com fer-ho»*): this board's
  // panel is lit from behind by one white LED on GPIO11 and the supply regulates it with a PWM, o sigui que **no és una tira** —un sol
  // punt, sense color— i el que la interfície d'`ILed` hi troba és la intensitat que el color demana (docs/30, docs/50). Es construeix
  // la primera vegada que es demana i no abans: un perifèric que ningú no ha demanat no ha de ser engegat a l'arrencada.
  ILed* led() override {
    if (!backlight_tried_) {
      backlight_tried_ = true;
      backlight_.reset(new Backlight(kBacklightPin));
      if (!backlight_->ready()) backlight_.reset();
    }
    return backlight_.get();
  }
  // **I el botó `S3` de la placa, que no és cap GPIO** (0.190.98, l'amo: *«exposa els botons!»*): el segon botó d'aquesta placa
  // penja del PCA9535, que ja el té declarat com a entrada al costat de `POWER_GOOD` i de la interrupció de la font, i fins ara
  // no arribava al sistema. Es presenta com una segona tecla, **darrere del BOOT** perquè el número de les que ja hi havia no
  // canviï. **I la línia d'interrupció de l'expansor sí que hi és** (0.190.99): GPIO38 baixa quan una de les seves entrades es mou,
  // o sigui que el botó es llegeix quan ha passat alguna cosa i no cada vint mil·lisegons, i la mateixa línia és una porta del son
  // lleuger —no del profund, perquè GPIO38 no és una RTC GPIO— (docs/50, docs/60).
  IInput* input() override {
    // **Una vegada i prou**: the register of the extra key is the same in every turn and `add` answers false when the room is
    // gone, o sigui que una segona crida no fa res (docs/50).
    if (!s3_added_) {
      arm_the_expander_line_once();
      KeyElsewhere s3;
      // **La cara i el lloc, dits per qui té la placa a la mà** (0.190.104): right, a third of the height from the bottom (docs/50).
      s3.face = Face::kRight;
      s3.spot = Spot::kDown;
      s3.kind = KeyKind::kSimple;
      s3.group = 0;
      s3.pressed = &T5Platform::read_the_s3;
      s3.context = this;
      s3_added_ = keys_.add(s3);
    }
    return &keys_;
  }
  // **I aquí les tecles no són per a la pàgina** (0.190.103, l'amo: *«en aquesta placa només tenim un botó físic real, no serveix per el
  // focus. En aquesta placa no hi ha opció a focus per botons»*). Un dial de tres tecles camina l'anell i la del mig prem el que hi ha
  // sota, i això demana un joc de tecles pensat per a això; aquí hi ha **un botó de debò** —el BOOT del xip— i la segona tecla és la de
  // l'expansor, totes dues al mateix lloc i cap de les dues feta per moure's per una pàgina. O sigui que cada pulsació va a l'app com
  // anava abans que hi hagués un anell: la promesa de `button.wait`, la cua de qui l'hagi presa o l'`on_button` (docs/30, docs/50).
  bool keys_are_for_the_page() const override { return false; }

  // **I té un botó de debò a sota del vidre** (0.190.110, l'amo: *«sota el panell hi ha un cercle que sembla ser un botó tàctil, i es fals... es al
  // frontal, just sota la pantalla eink, centrat horitzontalment, com un botó d'iphone»*): the circle is moulded into the case and pressing it
  // lands a finger on the last rows of the panel. És una propietat d'aquesta caixa i no del GT911, o sigui que es declara aquí (docs/30, docs/50).
  bool a_button_under_the_glass() const override { return true; }
  IImu* imu() override { return nullptr; }
  ITouch* touch() const override { return esp32_gt911(esp32_i2c(kI2cSdaPin, kI2cSclPin), kTouchIntPin); }

 private:
  // **L'expansor, construït una vegada i configurat a la primera pregunta** (docs/50): the panel and the board's own
  // button live on it, and configuring a chip is an I2C transaction that must happen once.
  // **El botó, llegit quan la línia de l'expansor diu que hi ha novetats** (0.190.99): the kernel asks every key on every turn and this
  // one costs an I²C transaction of two bytes, o sigui que la resposta es guarda fins que la línia torna a baixar, i el debat de
  // `GpioKeys` és el que decideix que allò és una premuda després (docs/50).
  bool s3_button_pressed() {
    // **Es pregunta quan la línia diu que hi ha hagut un canvi, i prou** (0.190.99): the `PCA_INT` of this board goes low when any of the
    // expander's inputs moves —the button, the supply's `POWER_GOOD`, the supply's interrupt— and it stays low until the input ports are
    // read, o sigui que **la línia mateixa és el senyal** i no cal cap gestor d'interrupció per tenir-lo. El bus només es toca quan és
    // baixa, i una lectura de fons cada dos-cents mil·lisegons queda per a una placa on la línia no vingui: un botó que només respon
    // quan una interrupció arriba és un botó que deixa de funcionar en silenci (docs/30, docs/50).
    const int64_t now = esp_timer_get_time() / 1000;
    // **Vint mil·lisegons de sòl sempre** (0.190.99): si una lectura falla, el latch de l'expansor no es neteja i la línia es queda
    // baixa, o sigui que sense aquesta porta el fracàs es convertiria en una lectura d'I²C a cada torn —el que el driver feia justament
    // per no fer-ho— (docs/50).
    if (now - s3_read_ms_ < 20) return s3_pressed_;
    const bool news = gpio_get_level(static_cast<gpio_num_t>(kExpanderIntPin)) == 0;
    if (!news && now - s3_read_ms_ < 200) return s3_pressed_;
    s3_read_ms_ = now;
    bool level = true;
    // **Es tanca a terra**: the board pulls the pin up and the switch closes it, o sigui que un zero és el dit.
    s3_pressed_ = expander()->read(Expander::kButton, level) && !level;
    return s3_pressed_;
  }

  // **La línia de l'expansor, com a entrada** (0.190.99): an input with its pull-up and no interrupt of ours. **No n'hi pot haver un**:
  // `epd_board_v7` d'epdiy ja en té un d'enganxat a aquest mateix GPIO38 —el seu `interrupt_handler` aixeca un `interrupt_done` que
  // ningú no llegeix mai— i el servei d'interrupcions del xip accepta **un sol gestor per pin**, o sigui que afegir-nos-hi seria trencar
  // el panell o no rebre res. La línia és una entrada i prou: el nivell ja diu tot el que l'ISR hauria de dir (docs/50).
  void arm_the_expander_line_once() {
    if (expander_line_armed_) return;
    expander_line_armed_ = true;
    gpio_config_t config = {};
    config.pin_bit_mask = 1ULL << kExpanderIntPin;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_ENABLE;  // és una sortida de drenador obert: la resistència és la que la manté alta
    config.intr_type = GPIO_INTR_DISABLE;    // la interrupció de la becaina es demana a `arm_wake`, i prou
    if (gpio_config(&config) != ESP_OK) logf(pal::kLogWarn, "BOARD", "the line of the expander did not come up");
  }

  static bool read_the_s3(void* context) {
    return static_cast<T5Platform*>(context)->s3_button_pressed();
  }

  Pca9535* expander() {
    static Pca9535 chip(esp32_i2c(kI2cSdaPin, kI2cSclPin), kExpanderAddress);
    if (!chip.configured()) {
      if (chip.configure(kExpanderInputs)) {
        // **El rail de la ràdio i el GPS comença apagat** (docs/50): the board says it is not always enabled, and a
        // module that nobody uses does not have to draw current.
        chip.write(Expander::kLoraEnable, false);
      } else {
        logf(pal::kLogWarn, "BOARD", "the expander is not usable: %s", chip.last_error());
      }
    }
    return &chip;
  }

  // The hour: the PCF8563 is the memory and the chip's counter is the clock, exactly as on the Paper
  // —the chip is adopted once, when somebody first asks for the clock, which is before the boot reads
  // the epoch— (docs/50, docs/60).
  static IClock* t5s3_clock() {
    static Bm8563 chip(esp32_i2c(kI2cSdaPin, kI2cSclPin), kClockAddress);
    // **El model del xip el diu la placa** (0.190.153): el fabricant l'anomena PCF8563 a l'exemple de la seva pròpia placa
    // (`examples/rtc_pcf8563`, SDA 39, SCL 40, IRQ 2, `0x51`), i el codi que el llegeix és el mateix que el del BM8563 perquè
    // tots dos parlen els mateixos registres (docs/50).
    static RtcClock clock(&chip, esp32_clock(), "PCF8563");
    static bool adopted = false;
    if (!adopted) {
      adopted = true;
      clock.adopt();
    }
    return &clock;
  }

  mutable Caps caps_;
  mutable bool built_ = false;
  // **Quatre portes**: la tecla BOOT, la interrupció del rellotge, i la de l'expansor, que és la que fa que el botó `S3` i el
  // `POWER_GOOD` tornin la placa d'una becaina (0.190.99). El vidre no hi és com a porta perquè la línia del dit no és d'aquesta
  // placa; el quart lloc és el de l'expansor (docs/50, docs/60).
  T5Power power_{kBootKeyPin, kClockIntPin, -1, kExpanderIntPin};
  // **Cap tecla en un pin del xip** (0.190.104): the one button of this board is read through the expander, i el BOOT es queda per al
  // mode de descàrrega i per a la becaina, que és el que és (docs/50).
  GpioKeys keys_{kNoKeysThere, 0};
  int64_t s3_read_ms_ = -1000;
  bool s3_pressed_ = false;
  bool s3_added_ = false;
  bool expander_line_armed_ = false;
  std::unique_ptr<Backlight> backlight_;
  bool backlight_tried_ = false;
};

T5Platform g_platform;

}  // namespace

IPlatform& platform() { return g_platform; }

}  // namespace hal
