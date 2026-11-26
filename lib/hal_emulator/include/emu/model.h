// What the emulated model is made of, in **one** place (docs/50, docs/85).
//
// The emulator used to answer for whatever hardware the option said; now the model decides, and the
// model is the one `--board` names. The rest of the emulator asks here instead of testing board
// names again, because two lists of what a board has are two truths and one of them ages.
#ifndef LUNOK_EMU_MODEL_H
#define LUNOK_EMU_MODEL_H

#include <stdint.h>

#include "hal/hal.h"

namespace emu {

// **Què fa un botó de la caixa** (0.192.7, l'amo: *«les definicions de botons son: Part dreta: RESET, POWER, ranura SD; part esquerra: BOOT, IO48
// … Defineix sempre els botons i mostra'ls a l'emulador. Tot i que son botons que no serveixin per programar l'ESP32, son útils per emular el
// funcionament.»*). Els botons d'una placa no són tots tecles del sistema, i la diferència és de maquinari:
//
//   - **`kKey`**: el firmware el llegeix com una tecla (`IInput`), amb el seu número. És el `POWER` de la T5S3 (el `IO1_2` de l'expansor, que
//     el HAL en diu `S3`) i el dial del Paper.
//   - **`kReset`**: el `RESET` de la caixa és el pin `EN` del xip. No el llegeix ningú: **reinicia la placa**, i a l'emulador és el mateix
//     que un `reboot` (docs/50).
//   - **`kBoot`**: el `BOOT` del mòdul és **GPIO0**. No és cap tecla del sistema —el xip el mira en arrencar per entrar al mode de
//     descàrrega— però **sí que és una porta de la becaina i del son profund**: a l'emulador, prémer-lo amb la placa adormida la desperta, i
//     amb la placa desperta no fa res (docs/50, docs/60).
//   - **`kNothing`**: hi és a la caixa i no és res per al firmware. El cas de debò és **`IO48`** de la T5S3: al mapa del fabricant
//     (`H752-01`) és **`EP_CKV`**, el rellotge de files del panell, o sigui que allà no hi cap cap botó (docs/50).
enum class ButtonRole : uint8_t { kKey = 0, kReset, kBoot, kNothing };

// **Un botó de la caixa, amb el nom que hi ha escrit** (0.192.7): the name is the word on the board —`RESET`, `POWER`, `BOOT`, `IO48`— and it
// is what the window draws, perquè una fila de quadrats sense nom no diu quina placa s'està emulant.
struct Button {
  const char* name = "";
  hal::Face face = hal::Face::kRight;
  hal::Spot spot = hal::Spot::kCentre;
  ButtonRole role = ButtonRole::kNothing;
  // L'índex de `IInput` que prem, o `0xFF` quan no és cap tecla del sistema.
  uint8_t key = 0xFF;
};

// Els botons que un model pot tenir (la T5S3 és la que més en té: quatre).
constexpr uint8_t kMostButtons = 5;

struct Model {
  const char* name;      // the name it answers to, the same one `sys.board` gives
  uint32_t lights;       // 0 is a board with no light at all
  uint32_t keys;         // one button, or the Paper's three
  bool imu;              // a sensor
  bool env;              // an ambient sensor: temperature and humidity (docs/50)
  // **La ràdio de Bluetooth al build d'aquesta placa** (0.190.200): l'ATOM —i el Matrix, que és el mateix firmware— la porta, i el
  // Paper i el T-Koala no. No hi ha cap `kCap` per a ella, perquè una placa pot ser un teclat i no un amfitrió: el que es pregunta
  // és `IBluetooth::can_bluetooth`, i aquí només es diu si hi ha ràdio (docs/50, docs/85).
  bool bt;
  // **Si el xip d'aquest model escala el rellotge sol** (0.190.227): els dos ATOM no —l'amo els vol sempre a 240— i el Paper i el T-Koala
  // sí. És el que respon `IPower::scales_the_clock` quan l'emulador fa aquesta placa, perquè les proves puguin veure les dues bandes
  // (docs/50, docs/85).
  bool dfs;
  // **The screen it draws on**: the IT8951 panel of the Paper, 960x540 and four bits per pixel. The
  // ATOMs of this family have no screen at all, and an emulator of one with a glass would be a board
  // nobody can buy (docs/50, docs/85).
  bool glass;
  // **A touch panel**: the glass that a finger goes through, which on this board is a GT911 that
  // counts in the page's own frame and reports two fingers (docs/50, docs/85).
  bool touch;
  bool gauge;            // a battery it can read
  bool slot;             // a card slot
  bool fram;             // an FRAM chip, which is non-volatile memory
  bool keeps_the_hour;   // a clock chip with a coin cell behind it
  // **El model d'aquell xip, quan n'hi ha** (0.190.153): the registers of a PCF8563 and of a BM8563 are the same and the model
  // is not, o sigui que el nom el dona el model de la placa i no el codi que els parla. Buit on no hi ha xip (docs/50, docs/85).
  const char* clock_chip = "";
  // **La cel·la que el model porta soldada, en mAh** (0.190.155): the capacity of the battery, which is what a gauge counts
  // against. Zero is a board whose cell nobody has written down, and the page then says nothing about mAh (docs/50, docs/85).
  int cell_mah = 0;
  // A board that can cut its own supply: the Paper holds its power on a pin and letting it go is
  // the board stopping. The ATOMs have no hold and no way to switch themselves off between the cell
  // and the chip, so they answer no (docs/50, docs/85).
  bool powers_off;
  // **Un board whose keys can wake it from a sleep that keeps the supply** (0.189.25): the Paper has a key of the RTC domain
  // wired to the dial; the ATOMs do not, and say no (docs/50, docs/85).
  bool keys_wake;
  // The memory the board really has for the system, in KB. **320** on every ESP32 of this family:
  // the chip's 520 KB of SRAM are not all heap, and 320 is what a program can use (docs/75). The
  // emulator may be told a smaller ceiling, never a bigger one: an emulator that hands out a megabyte is
  // an emulator that lies about the glass (docs/85).
  uint32_t heap_kb;
  // The **extra** memory of the board: what the chip says it has (`psram_chip_kb`) and how much of it
  // the CPU can address (`psram_kb`, which is all the system can hand out). On this family the second
  // is 4 MB even when the chip carries 8, because that is the size of the ESP32's window for external
  // RAM (docs/50, docs/75).
  uint32_t psram_chip_kb;
  uint32_t psram_kb;
  // **What the board has free at the prompt, and what its extra memory can hand out when nothing of the
  // system is in it**, in bytes, measured on the glass (docs/75). The emulator counts everything the system
  // and the apps take through `pal::extra_memory()` against these two, so the ceiling of a page and of an
  // app is the board's and not the PC's (docs/85). Zero is a home the board does not have.
  uint32_t free_heap_bytes;
  uint32_t extra_free_bytes;
  // What the board answers as the ceiling of an app (`IPlatform::heap_limit_kb`): its PSRAM on the
  // Paper, and **0 on a board that does not say**, which is the kernel's own ceiling. The emulator
  // answers this and not a number of its own (docs/75, docs/85).
  uint32_t app_limit_kb;
  // The data partition of the board, in bytes, from its own partition table: 0x0E0000 on the 4 MB
  // boards and 0xBE0000 on the Paper. It is what `fs.space` has to answer, because a directory of the
  // PC is not an answer an app can use (docs/70, docs/85).
  uint32_t disk_bytes;
  // **What the bootloader reads out of the flash before the kernel exists**, in bytes: the image this
  // board boots, as the build of the 3/11 left it (`-t size`: 2 097 008 B on the ATOM and 2 182 672 B on
  // the Paper). It is a number of the build and not of the chip, and it is here because turning the board
  // on is not free: the ROM reads the image from the SPI nor and the PSRAM test of ESP-IDF writes and
  // reads every byte the chip maps (docs/75, docs/85).
  uint32_t boot_image_bytes;
  // **El son profund d'aquesta placa, i què és** (0.192.4, l'amo: *«Afegeix a l'emulador la placa T5S3 dels collons i parem de fer
  // l'imbècil»*). Tres preguntes que el model ha de poder contestar perquè el nucli faci el mateix que a la placa i no una versió
  // semblant:
  //
  //   - **si la baixada és una apagada de debò** (`deep_powers_off`): el Paper talla el corrent i el rellotge el torna a engegar; la T5S3
  //     fa el son profund del propi xip amb la cel·la alimentant la placa, i la resposta és que no (docs/50).
  //   - **si el son profund és una manera d'estalvi quan no hi ha ningú** (`can_sleep_on_idle`): la T5S3 hi baixa igualment perquè té
  //     portes per tornar-ne; els ATOM i el T-Koala no en tenen cap (docs/50, docs/60).
  //   - **si el repòs del panell és la seva font o el seu son** (`rest_is_the_supply`): al TPS65185 de la T5S3 `rest()` baixa els rails i
  //     la pintada següent els torna a pujar (`epd_poweron`), mentre que el controlador del Paper s'adorm i refusa el que vingui després
  //     (docs/50, docs/85).
  bool deep_sleep_is_a_power_off = false;
  bool sleeps_on_idle = false;
  bool rest_is_the_supply = false;
  // **La becaina i la consola com a porta** (0.192.4): la T5S3 no fa becaines i el seu cable no desperta res, perquè la consola és el USB
  // del propi xip i un son lleuger l'atura. El Paper sí que escolta la consola (docs/50, docs/85).
  bool naps = true;
  bool console_wakes = true;
  // **El cercle de sota el vidre** (0.192.4): la T5S3 en té un de debò i el model ho diu sense que calgui `--swbutton` (docs/50, docs/85).
  bool sw_button = false;
  // **Un carregador que sap si hi ha cable** (0.192.4): el BQ25896 sosté la placa amb VBUS, o sigui que endollada **no es pot tallar el
  // corrent** i la clau d'apagar no s'ha de pintar. El Paper no porta carregador (docs/50, docs/85).
  bool charger = false;
  // **Si pintar és calcular** (0.192.4): a la T5S3 no hi ha controlador de panell —les files s'han de reunir al processador— i el nucli ho
  // pregunta (`IPlatform::a_paint_is_work`) per no baixar el rellotge enmig d'una pintada (docs/50, docs/95).
  bool paint_is_work = false;
  // **Si el vidre es renta sencer a cada arrencada** (0.192.10, l'amo: *«Que aquest model SEMPRE al boot netegi la pantalla amb splash
  // hide (que neteja al màxim)»*): el panell de la T5S3 guarda el fantasma del que hi havia i el dibuix d'un son és tinta de ningú, o
  // sigui que la placa ho diu amb `kCapWashAtBoot` i el boot hi fa un `gc16` del vidre sencer passi el que passi. El Paper i els ATOM
  // diuen que no i el boot fa el de sempre (docs/50, docs/60, docs/85).
  bool wash_at_boot = false;
  // **Per on torna, amb les paraules d'aquest banc** (0.192.4): the board's own sentence is long and lives in the board's HAL; aquí el que
  // es diu és el que aquest emulador imita de debò (docs/50, docs/85).
  const char* wakes = "timer";
  // **Les tecles, tal com les declara la placa** (0.192.6, l'amo: *«l'emulador de T5S3 te els botons malement, quina definició de botons
  // té?»*): `keys` era el comptador i prou, i la cara, el lloc i la mena de cada tecla eren **una taula fixa del dial del Paper** —tres
  // tecles compostes a la dreta—, o sigui que qualsevol model amb una tecla en sortia amb la forma equivocada. El que diu la placa és el
  // que hi ha aquí: el Paper té tres tecles compostes a la cara dreta (`Up`, `Centre`, `Down`), l'ATOM una de simple a dalt, la T5S3 una
  // de simple a la dreta i baixa (el `S3` de l'expansor) i el T-Koala cap (docs/50, docs/85).
  //
  // **I `keys_for_the_page` és el que el dial mou**: la T5S3 contesta que no (el seu únic botó no serveix per caminar un anell), i la
  // resta de models hereten el sí de sèrie del HAL. Un model de tres tecles fa servir `key_spot` només per a la primera i la resta surten
  // com el dial (docs/30, docs/50).
  hal::Face key_face = hal::Face::kRight;
  hal::Spot key_spot = hal::Spot::kCentre;
  hal::KeyKind key_kind = hal::KeyKind::kCompound;
  bool keys_for_the_page = true;
  // **I els botons de la caixa, tots** (0.192.7): the keys above are what the *system* reads; this is what a person can press on the board,
  // including the ones the firmware cannot use. The window draws one box per button, with its name, on the face and the spot it is really
  // on (docs/50, docs/85).
  Button buttons[kMostButtons];
  uint8_t button_count = 0;
};

// The model this emulator is.
Model model();

// The hardware of a model, by name. It is what the command line asks before the options are
// installed, because `--mem-cap` has to be checked against the board that was asked for and not
// against the one in effect (docs/85).
Model model_of(const char* name);

// Whether this emulator is that model, by the name it is written with in `platformio.ini`.
bool model_is(const char* name);

}  // namespace emu

#endif  // LUNOK_EMU_MODEL_H
