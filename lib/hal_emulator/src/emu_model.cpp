// The hardware of each emulated model, and the only list of it (docs/50, docs/85).
#include <string.h>

#include "board/identity.h"
#include "emu/model.h"

namespace emu {

bool model_is(const char* name) { return strcmp(board::id(), name) == 0; }

Model model() { return model_of(board::id()); }

// **Els botons, posats d'una tirada** (0.192.7): a table per model, amb el nom de la caixa i el que fa cadascun. La cara i el lloc són els
// de debò —el `RESET` i el `POWER` a la dreta, el `BOOT` i l'`IO48` a l'esquerra— perquè la finestra els posa on són i una persona que els
// prem sap què està prement (docs/50, docs/85).
void put_a_button(Model& out, const char* name, hal::Face face, hal::Spot spot, ButtonRole role, uint8_t key) {
  if (out.button_count >= kMostButtons) return;
  Button& one = out.buttons[out.button_count++];
  one.name = name;
  one.face = face;
  one.spot = spot;
  one.role = role;
  one.key = key;
}

Model model_of(const char* name) {
  Model out = {};
  out.name = name;
  const bool paper = (name != nullptr) && strcmp(name, "m5stack-paper") == 0;
  const bool matrix = (name != nullptr) && strcmp(name, "m5stack-atom-matrix") == 0;
  const bool tkoala = (name != nullptr) && strcmp(name, "ttgo-t-koala") == 0;
  if (paper) {
    // Three keys on the dial, no light, no sensor, and the extra memory the board really carries:
    // the gauge, the card slot, the FRAM and the clock chip with a coin cell behind it (docs/50).
    out.lights = 0;
    out.keys = 3;
    out.imu = false;
    // The SHT30 of this board, on the internal I2C next to the clock and the FRAM (docs/50).
    out.env = true;
    // The panel of this board, and the GT911 on top of it, on the same bus as the clock and the FRAM
    // (docs/50).
    out.glass = true;
    out.touch = true;
    // **The Paper's build carries the radio too** (0.190.201): its `sdkconfig.defaults` asks for NimBLE now, o sigui que aquest
    // emulador ha de dir el mateix que la placa i no el que deia ahir (docs/50, docs/85).
    out.bt = true;
    // **El Paper escala el rellotge** (0.190.227): és l'S3 que sempre l'ha escalat, i el que l'amo ha tret és només dels dos ATOM.
    out.dfs = true;
    out.gauge = true;
    out.slot = true;
    out.fram = true;
    out.keeps_the_hour = true;
    out.clock_chip = "BM8563";
    // **The cell of the model**, which is what an M5Paper carries: 1150 mAh at 3,7 V, full at 4,2 V (docs/50, docs/85).
    out.cell_mah = 1150;
    out.powers_off = true;
    // **I el seu son és una apagada de debò, amb el rellotge de tornada** (0.192.4): el model ho diu aquí i no ho dedueix de
    // `keeps_the_hour`, perquè la T5S3 també té xip de rellotge i el seu son no és cap apagada. El repòs del panell és el son del seu
    // controlador i el cable no és cap porta (docs/50, docs/85).
    out.deep_sleep_is_a_power_off = true;
    out.sleeps_on_idle = true;
    out.wakes = "timer, clock";
    // **El dial de tres tecles, a la cara dreta** (0.192.6): the same three the board declares, in the order that leaves key 0 in the
    // middle (`hal_paper`'s table, docs/50).
    out.key_face = hal::Face::kRight;
    out.key_spot = hal::Spot::kCentre;
    out.key_kind = hal::KeyKind::kCompound;
    out.keys_for_the_page = true;
    // **El dial, amb els noms que porta la caixa** (0.192.7): `PWR` al mig i les dues fletxes als costats, que és la mateixa taula que
    // declara `hal_paper` (docs/50, docs/85).
    put_a_button(out, "UP", hal::Face::kRight, hal::Spot::kUp, ButtonRole::kKey, 1);
    put_a_button(out, "PWR", hal::Face::kRight, hal::Spot::kCentre, ButtonRole::kKey, 0);
    put_a_button(out, "DOWN", hal::Face::kRight, hal::Spot::kDown, ButtonRole::kKey, 2);
    // **La tecla del dial desperta un son que aguanta el corrent** (0.189.25): GPIO38 is a pin of the RTC domain and the chip
    // can watch it while it sleeps (docs/50, docs/85).
    out.keys_wake = true;
    out.heap_kb = 320;
    // The chip has 8 MB of PSRAM and the ESP32 maps **4** of them: what the system counts is the
    // mapped region, because the other half needs the bank-switched `himem` nobody uses (docs/50,
    // docs/75).
    out.psram_chip_kb = 8192;
    out.psram_kb = 4096;
    // **The free heap the board reports at the prompt and the PSRAM it can hand out**, measured on the
    // glass on 31/10 (docs/75): the Paper pays the card, the I2C, the clock and its three keys, so it
    // has half the RAM an ATOM has, and its 8 MB of PSRAM are **4 095** usable and not 4 096 because
    // the allocator eats the region's own bookkeeping. An app that asks for the whole 4 MB dies at
    // 4 095, which is what happens on the board and not a rounded number (docs/75, docs/85).
    out.free_heap_bytes = 162728;
    out.extra_free_bytes = 4095u * 1024u;
    out.app_limit_kb = 4096;  // what the board answers as the ceiling of an app
    out.disk_bytes = 0xBE0000;  // the littlefs partition of partitions_16mb.csv
    out.boot_image_bytes = 2182672;  // the image of the paper environment, 3/11 (docs/85)
    return out;
  }
  // **La LilyGO T5 E-Paper S3 Pro** (0.192.4, l'amo: *«Afegeix a l'emulador la placa T5S3 dels collons i parem de fer l'imbécil»*): the board
  // this house has spent its nights on, and the one whose defects could not be tried anywhere but on it. What the emulator imitates is the
  // hardware the kernel talks to —a 960x540 glass of four bits with a GT911 on top, a BQ27220 that counts coulombs, a BQ25896 that knows
  // when there is a cable, a PCF8563 with a coin cell and a deep sleep with three doors— i **no els seus drivers**: `lib/hal_t5s3` no corre
  // aquí. El que sí que corre és tot el que hi ha a sobre, que és on eren els defectes del son i del rentat (docs/50, docs/85).
  const bool t5s3 = (name != nullptr) && strcmp(name, "lilygo-t5s3-epaper-pro") == 0;
  if (t5s3) {
    // **Una llum i és la del vidre** (GPIO11 cap a l'`EN` de l'elevador): no hi ha cap RGB en aquesta placa (docs/50).
    out.lights = 1;
    // **Una tecla, i és la de l'expansor** (0.192.6): el BOOT del mòdul no és a la llista de tecles del HAL —és el pin d'arrencada del
    // xip— i el cercle de sota el vidre no és cap tecla, és un tret de la caixa (`a_button_under_the_glass`) (docs/50).
    out.keys = 1;
    out.imu = false;
    out.env = false;
    out.bt = true;  // el seu `sdkconfig` porta NimBLE des de la 0.191.4, amb els búfers del controlador reservats
    out.dfs = true;
    out.glass = true;  // ED047TC1, 960x540 i quatre bits, sense controlador: la forma d'ona la paga el processador
    out.touch = true;  // el GT911 de sota, a 0x5D
    out.gauge = true;  // el BQ27220, que compta els coulombs i sap els mAh que queden
    out.charger = true;  // el BQ25896: VBUS, el corrent que entra i el bit que talla la cel·la
    out.slot = true;
    out.fram = false;
    out.keeps_the_hour = true;
    out.clock_chip = "PCF8563";
    // **La cel·la del model**: ningú no ha llegit la capacitat d'aquest aparell encara —la del xip la sap el BQ27220 i la diu
    // `battery_detail`—, o sigui que aquest número és el que es posa quan el run no en diu cap i es canvia el dia que hi sigui
    // (`lua` → `power.battery_detail()`, docs/95, docs/85).
    out.cell_mah = 1500;
    out.powers_off = true;
    out.keys_wake = true;
    out.heap_kb = 320;
    out.psram_chip_kb = 8192;
    // **L'S3 mapeja tota la PSRAM que porta**: vuit megabytes i no els quatre del Paper, que és el que respon `heap_limit_kb`
    // (`heap_caps_get_total_size(MALLOC_CAP_SPIRAM)`, docs/50).
    out.psram_kb = 8192;
    // **El número mesurat a la placa** (docs/75): la línia de l'arrencada de l'amo, abans del panell i amb la ràdio apagada.
    out.free_heap_bytes = 162728;
    out.extra_free_bytes = 8191u * 1024u;  // l'allocator es menja la seva comptabilitat, com al Paper
    out.app_limit_kb = 8192;
    out.disk_bytes = 0xBE0000;       // littlefs de partitions_16mb.csv, la mateixa taula que el Paper
    out.boot_image_bytes = 2510401;  // la imatge d'aquest build, `-t size` (docs/85)
    // **I el que fa aquesta placa i el Paper no** (0.192.4): el seu son profund és el del xip amb la cel·la encesa, hi baixa quan no hi
    // ha ningú, no fa becaines, el cable no desperta res, el cercle del davant hi és, el carregador sosté la placa i el repòs del panell
    // és la seva font (docs/50, docs/60, docs/85).
    out.deep_sleep_is_a_power_off = false;
    out.sleeps_on_idle = true;
    out.naps = false;
    out.console_wakes = false;
    out.sw_button = true;
    out.rest_is_the_supply = true;
    out.paint_is_work = true;
    // **I el paper es renta sencer a cada arrencada** (0.192.10): the ED047TC1 of this board keeps the ghost of its last picture —the one of
    // a sleep included— and the boot of the board washes the whole glass with a `gc16` whatever the reason it came back for
    // (l'amo: *«Que aquest model SEMPRE al boot netegi la pantalla amb splash hide (que neteja al màxim)»*, docs/50, docs/85).
    out.wash_at_boot = true;
    out.wakes = "timer, clock, key and glass";
    // **La forma d'aquella tecla, com la declara la placa** (0.192.6, l'amo: *«l'emulador de T5S3 te els botons malement»*): simple, a la
    // cara dreta i baixa (el `S3` de l'expansor), i **no serveix per caminar una pàgina** (docs/50).
    out.key_face = hal::Face::kRight;
    out.key_spot = hal::Spot::kDown;
    out.key_kind = hal::KeyKind::kSimple;
    out.keys_for_the_page = false;
    // **Els quatre botons de la caixa** (0.192.7, l'amo: *«Part dreta: RESET, POWER, ranura SD. Part esquerra: BOOT, IO48»*): the SD slot is
    // not a button and has no box. Només el `POWER` és una tecla del sistema (el `IO1_2` de l'expansor): el `RESET` és l'`EN` del xip i
    // **reinicia**, el `BOOT` és GPIO0 i no el llegeix ningú mentre la placa corre però **desperta el son**, i l'`IO48` és el `EP_CKV` del
    // panell, o sigui que no fa res (docs/50).
    put_a_button(out, "RESET", hal::Face::kRight, hal::Spot::kUp, ButtonRole::kReset, 0xFF);
    put_a_button(out, "POWER", hal::Face::kRight, hal::Spot::kDown, ButtonRole::kKey, 0);
    put_a_button(out, "BOOT", hal::Face::kLeft, hal::Spot::kUp, ButtonRole::kBoot, 0xFF);
    put_a_button(out, "IO48", hal::Face::kLeft, hal::Spot::kDown, ButtonRole::kNothing, 0xFF);
    return out;
  }
  if (tkoala) {
    // The LilyGO TTGO T-Koala: one plain green LED, a card slot and the WROVER-B's 8 MB of PSRAM of which
    // the ESP32 maps 4. No glass, no touch, no sensor, no gauge and no key: its only button is RST (docs/50).
    out.lights = 1;
    out.keys = 0;
    out.imu = false;
    out.env = false;
    out.glass = false;
    out.touch = false;
    out.bt = false;
    // El T-Koala no és cap dels dos ATOM: escala com sempre (0.190.227).
    out.dfs = true;
    out.gauge = false;
    out.slot = true;
    out.fram = false;
    out.keeps_the_hour = false;
    out.powers_off = false;
    out.heap_kb = 320;
    out.psram_chip_kb = 8192;
    out.psram_kb = 4096;
    // **The ATOM's free heap and the Paper's mapped PSRAM**: the same chip and the same external window,
    // but nobody has taken the T-Koala's own numbers on the board yet. docs/95 has the command and this
    // pair is replaced the day they exist (docs/75, docs/85).
    out.free_heap_bytes = 338864;
    out.extra_free_bytes = 4095u * 1024u;
    out.app_limit_kb = 4096;  // what the board answers as the ceiling of an app
    out.disk_bytes = 0x160000;  // the littlefs partition of partitions.csv
    out.boot_image_bytes = 2286000;  // the image of the tkoala environment, 5/11 (docs/85)
    return out;
  }
  if (matrix) {
    // The panel of twenty-five and the sensor, one button, no cell and no slot (docs/50).
    out.lights = 25;
    out.keys = 1;
    // **La tecla del Matrix és la de l'ATOM** (0.192.6): the same simple button on the top face, because the panel of twenty-five is
    // the only thing that changes (docs/50, docs/85).
    out.key_face = hal::Face::kTop;
    out.key_spot = hal::Spot::kCentre;
    out.key_kind = hal::KeyKind::kSimple;
    out.imu = true;
    out.env = false;
    // **The Matrix is the ATOM's firmware with a panel on it**, o sigui que hi porta el mateix ràdio (docs/50, docs/85).
    out.bt = true;
    // **El Matrix no escala: sempre a 240** (0.190.227, l'amo: *«Que vagin sempre a 240Mhz»*).
    out.dfs = false;
    out.glass = false;
    out.touch = false;
    out.gauge = false;
    out.slot = false;
    out.fram = false;
    out.keeps_the_hour = false;
    out.powers_off = false;
    out.heap_kb = 320;
    out.psram_chip_kb = 0;
    out.psram_kb = 0;
    // The ATOM's free heap with the prompt and Lua in the binary (docs/75): the Matrix is the same chip
    // and the same firmware with a panel of twenty-five on it, and nobody has taken its number on its
    // own, so the ATOM's is used and docs/75 says that it is (docs/85).
    out.free_heap_bytes = 338864;
    out.extra_free_bytes = 0;  // no PSRAM on this board
    out.app_limit_kb = 0;  // the board does not say: the kernel's own ceiling, 64 KB
    out.disk_bytes = 0x0E0000;  // the littlefs partition of partitions.csv
    // The Matrix is the ATOM's chip and the ATOM's image with a panel of twenty-five in it: nobody has
    // taken its size on its own, so the ATOM's is used and docs/85 says that it is.
    out.boot_image_bytes = 2097008;  // the image of the atom environment, 3/11 (docs/85)
    return out;
  }
  // The ATOM: one light, one button, and nothing else of its own. A name that is not one of the
  // models is checked when `--board` is parsed, so this is the honest answer for an emulator built by
  // hand as well (docs/85).
  out.lights = 1;
  out.keys = 1;
  // **I la seva tecla, com la declara la placa** (0.192.6): one simple key on the **top** face, which is the row under the page in the
  // window and not a dial on a side (docs/50, docs/85).
  out.key_face = hal::Face::kTop;
  out.key_spot = hal::Spot::kCentre;
  out.key_kind = hal::KeyKind::kSimple;
  out.keys_for_the_page = true;
  // **La tecla de l'ATOM, i prou** (0.192.7): the one button of the module, on the top face; its reset is a pin of the header and not a
  // button of the case (docs/50, docs/85).
  put_a_button(out, "BUTTON", hal::Face::kTop, hal::Spot::kCentre, ButtonRole::kKey, 0);
  out.imu = false;
  out.env = false;
  // **The ATOM's build carries the radio**: NimBLE and the HID device are in its `sdkconfig.defaults`, and this is the board the
  // keyboard and the host are written for (docs/50, docs/85).
  out.bt = true;
  // **I l'ATOM Lite tampoc** (0.190.227): els dos ATOM van sempre al cim i la resta de models escalen com sempre.
  out.dfs = false;
  out.glass = false;
  out.touch = false;
  out.gauge = false;
  out.slot = false;
  out.fram = false;
  out.keeps_the_hour = false;
  out.powers_off = false;
  out.heap_kb = 320;
  out.psram_chip_kb = 0;
  out.psram_kb = 0;
  out.free_heap_bytes = 338864;  // the ATOM's, measured the 30/10 (docs/75)
  out.extra_free_bytes = 0;
  out.app_limit_kb = 0;
  out.disk_bytes = 0x0E0000;
  out.boot_image_bytes = 2097008;  // the image of the atom environment, 3/11 (docs/85)
  return out;
}

}  // namespace emu
