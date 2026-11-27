// Where the settings of an app are kept, and which unit is behind `/cfg`.
#include "kernel/meter.h"
#include "kernel/bluetooth.h"
#include "kernel/cfg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/calendar.h"
#include "kernel/log.h"
#include "kernel/net.h"
#include "kernel/power.h"
#include "kernel/splash.h"
#include "kernel/vfs.h"
#include "luabind/luabind.h"
#include "pal/pal.h"

namespace kernel {
namespace cfg {
namespace {

const char kNamespace[] = "cfg";
const char kKey[] = "unit";
// **The keyboard is a setting of the system and not of an app** (0.93.0): it is chosen with `cfg keyboard <id>` and it
// survives a reboot like the unit does. The name is the id of one of the children of `ui`, and `cfg` keeps only the
// string: which keyboards exist is the UI's to say (docs/30, docs/70).
const char kKeyboardKey[] = "keyboard";
// **The picture of the start is a word of the splash** (0.115.0): the two drawings of the logo are two moments, and
// this only keeps the name of the one the person chose. A word that is not one of them is refused where it is typed,
// so the boot never paints a guess (docs/30, docs/50).
const char kBootKey[] = "boot";
const size_t kNameCap = 12;

// **The app the board starts when it comes up** (0.119.0): the key of the store, and the name the build carries.
// A board that says nothing gets no home app, which is the prompt. `LUNOK_HOME_APP` is what `platformio.ini` writes
// per board (`-D LUNOK_HOME_APP=\"<id>\"`) and the fallback here is for a build that says nothing at all.
const char kHomeKey[] = "home";
#ifndef LUNOK_HOME_APP
#define LUNOK_HOME_APP ""
#endif
// The longest id the door presents is 39 characters plus the null (see `apps::Entry`), so the setting has room for
// the whole name of either half of the door —`squares` and `squares-SD`— and not one byte more.
const size_t kHomeCap = 40;

// **The hour of the person, which is an offset from the chip's** (0.125.0): the key of the store, and the number the
// whole system asks for. It is kept as the text the command wrote —`+01:00`— because a person who reads the store
// with `nvs` has to be able to read what is there (docs/30, docs/60, docs/70).
const char kTzKey[] = "tz";
const size_t kTzCap = 8;  // "+01:00" and the null
// **Fourteen hours either way**, which is every zone the Earth has and a little more: a clock told to be sixteen hours
// ahead is a typo and not a place.
const int32_t kTzMaxMinutes = 14 * 60;

// **El dia de traspàs ja no és un ajust** (0.192.57, l'amo: *«sabem com calcular si es any de taspàs o no ho es… no cal un
// toggle manual»*): a leap year is a fact of the Gregorian calendar, so `cfg/leap` is not read by anything any more and the
// key only exists here **to be erased from a store an older build wrote** (docs/60, docs/70). The first day of the week is
// the setting that is left, kept as the word a person reads with `nvs` —`week monday`/`week sunday`— (docs/60, docs/70).
const char kLeapKey[] = "leap";
// **The hour of summer, as the two words a person reads in the store** (0.182.0, docs/30, docs/70).
const char kDstKey[] = "dst";
const char kWeekKey[] = "week";

// **The slice of a turn of Lua, which is the latency of the system** (0.174.7): how many instructions of an app a turn
// is worth before the kernel takes the turn back for the glass, the keys and the radio. It is kept as the decimal text a
// person reads with `nvs` —`20000`— for the same reason the hour is kept as `+01:00`, and the number itself lives in
// `luabind`, which is the module that counts it: this door keeps the choice and puts it there at boot (docs/20, docs/75).
const char kTurnKey[] = "turn";
const size_t kTurnCap = 8;  // "200000" and the null
// **El sostre de memòria de l'intèrpret** (0.192.94): the key of the store and the room the number has, which is kilobytes —`auto` is the
// word of the value of sèrie i el número és el que un llegeix amb `nvs`— (docs/20, docs/30, docs/70).
const char kMemKey[] = "mem";
const size_t kMemCap = 12;  // "auto" or "65535" and the null, with room for what a snprintf may write
// **El parpalleig del cursor** (0.189.34): the key of the store and the room the number has in it, which is a number of
// milliseconds and nothing else —the option is called `cursor delay` at the prompt, and the store keeps the one word of the
// key, because a key of a key-value store is one word (docs/30, docs/70).
const char kCursorKey[] = "cursor";
const size_t kCursorCap = 8;  // "5000" and the null
// **Si el llançador ordena les apps pels usos** (0.189.13): kept as the words a person reads in the store —`on` and
// `off`— like the calendar and the hour of summer, and off until somebody asks for it (docs/30, docs/40).
const char kSortKey[] = "sort";
// **L'app dels ajustos** (0.189.109): the name the gear of the panel runs, kept as a name like the app of the start and not as a
// path, for the same reason —a path in the settings is a way to make a board open any file at its first breath— (docs/30, docs/40).
const char kSettingsKey[] = "settings";
// **Les velocitats permeses** (0.189.107): the three bits of the steps the governor may use, kept as the list a person reads
// —`80,160,240`— and never as a number, for the same reason the hour is `+01:00` (docs/50, docs/70).
const char kFreqKey[] = "freq";
const size_t kFreqCap = 12;  // "80,160,240" and the null
// **El bloqueig de la pantalla** (0.189.150): l'app que tapa la pàgina, si s'usa a l'arrencada i al despertar, i quants minuts
// sense tocar res la fan sortir. Tres claus i les tres amb les paraules que una persona llegeix al `nvs` (docs/30, docs/60,
// docs/70).
const char kLockKey[] = "lock";
const char kLockOnKey[] = "lockon";
// **El commutador de la pàgina de Settings→Clock** (0.192.63, l'amo: *«afegir l'opció a l'app Settings->Clock un ToggleBox "Sync with
// GPS" … En cas d'activa el GPS, es sincronitzarà el rellotge, tant intern com l'RTC»*): el que la placa recorda és si ha de creure
// l'hora del cel quan en tingui (docs/30, docs/60).
const char kGpsSyncKey[] = "gpssync";
const char kSleepKey[] = "sleep";
const char kTouchWakeKey[] = "touchwake";
// **La becaina** (0.190.63): the switch of the Battery tab that turns the light sleep of the waits on and off.
const char kNapKey[] = "nap";
// **El teclat de Bluetooth, que és una altra porta i un altre commutador** (0.190.180) (docs/30, docs/50).
const char kBtKey[] = "bt";
// **La clau `btmode` ja no es llegeix** (0.191.0): hi havia hagut un mode a triar —`keyboard`, `mouse` o `both`— i la placa publica sempre
// les dues coses, o sigui que la paraula que hi hagi a la botiga no és de ningú. No s'esborra perquè no cal tocar el que ja hi és, i una
// placa que ve d'una versió anterior no ha de fer res (docs/30, docs/50).
// **La ràdio del wifi, encesa o apagada** (0.190.221, l'amo: *«Si actives la wifi (passes a "on") està apagada en aquell moment,
// s'inicialitza»*): el mateix parell de coses que `cfg bt` —una paraula desada i la ràdio de debò—, perquè el commutador de la targeta de la
// barra no ha de ser una cosa i el prompt una altra (docs/30, docs/50).
const char kWifiKey[] = "wifi";
// **El color i el cursor, encesos o apagats** (0.190.226, l'amo: *«afegeix la comanda ansi on/off (totes les sortides tenen ansi, però amb
// aquesta opció es filtra abans d'enviar a la línia)»*): el codi porta els codis sempre i aquesta paraula decideix si passen pel filtre que
// els treu. Neix **encesa** —la font té els codis i el que no els entén els perd— i l'arrencada la posa a `off` sola quan el terminal de
// l'altre cap no els contesta (docs/30, docs/50).
const char kAnsiKey[] = "ansi";
// **Les dues claus del mesh** (0.190.218): trenta-dos dígits hexadecimals cada una, perquè el registre les ha de poder dir i una persona les ha
// de poder copiar a un altre lloc. Sense això, una placa que s'ha fet la seva xarxa la perdria a cada reiniciada i tornaria a demanar `mesh
// key` —i una xarxa que canvia de clau a cada arrencada no és una xarxa (docs/30, docs/50, docs/70).
const char kMeshNetKey[] = "meshnet";
const char kMeshAppKey[] = "meshapp";
// Trenta-dos dígits, el zero del final i prou: `char[33]` és exactament una clau escrita (docs/45).
constexpr size_t kKeyTextCap = 33;

// **I les dues funcions que la llegeixen i la desen són més avall**: la capçalera `kernel/cfg.h` les declara, o sigui que el codi de dalt
// les pot cridar sense una declaració pròpia (docs/45).
const char kUartWakeKey[] = "uartwake";
const char kLastSleepKey[] = "lastsleep";
const char kIdleKey[] = "idle";
// **El nivell del registre** (0.190.89): the word of the row of the System tab, kept like the keyboard's and read by the ring.
const char kLogLevelKey[] = "loglevel";
// **La llum del vidre** (0.190.106, l'amo: *«Afegeix a Settings->System un toggle amb "Use backlight" i un selector desplegable amb tots els
// valors possibles del backlight»*): two keys and not one, perquè són dues decisions diferents —si la placa il·lumina el vidre, i amb quanta
// llum— i una persona vol poder apagar-la sense perdre el nivell que havia triat (docs/30, docs/50).
// **El règim del tàctil** (0.190.107, l'amo: *«afegeix un toggle que posi "Adaptative touch scan", aquesta opciò activa o desactiva el
// comportament del panell de baixar la sensibilitat i pujar-la quan hi ha dit»*): a switch of the Input tab and a key of its own, perquè el
// que decideix és si el vidre escombra deu vegades per segon quan no hi ha ningú o sempre al màxim (docs/30, docs/50).
const char kTouchScanKey[] = "touchscan";
// **El botó de sota el vidre** (0.190.110, l'amo: *«Afegeix a Settings->Input un Toggle per habilitar-lo o deshabilitar-lo "Use software
// button"»*): un commutador i prou, perquè el botó no té nivell ni mida —el lloc és el de la placa i la zona la calcula el tàctil— (docs/30).
const char kSoftwareButtonKey[] = "swbutton";
// **El llum que s'apaga quan ningú mira** (0.190.118, l'amo: *«unaltre Toggle "Dim when idle" igual que el Sleep when idle o el Lock the
// screen. Aquest en 1 minut que no es toqui l apantalla el backlight s'apaga i es torna a encendre quan toquen el panell»*): una clau i prou,
// perquè el minut és del sistema i el que una persona tria és si el vol (docs/30, docs/50).
const char kDimIdleKey[] = "dimidle";
// **El respir mentre carrega** (0.190.162, l'amo: *«Si el T5S3 està carregant batería vull el efecte "breathe" al backight fins que deixa de
// carregar. Afegeix un Toggle a l'app Settings->Battery que digui "Breathe while charging"»*): una clau i prou —el gest és el mateix que el
// del llum que s'apaga quan ningú mira—, i ve **encesa**, perquè un cable que no diu res és el que la persona demanava deixar de tenir (docs/30, docs/50).
const char kBreatheKey[] = "breathe";
// **L'avís del cable** (0.190.165, l'amo: *«Afegeix un toggle a Settings->System "Announce charge" (per defecte true) que mostrarà l' splash de
// "charging" quan es detecti que s'endolla»*): una clau i prou, i ve **encesa** perquè un cable que no diu res és el que la persona vol deixar
// de tenir; qui no el vol el treu a la pàgina dels ajustos (docs/30, docs/50).
const char kAnnounceChargeKey[] = "announcecharge";
const char kBacklightKey[] = "backlight";
const char kBacklightLevelKey[] = "backlightlevel";
// **El gir del vidre** (0.190.281, l'amo: *«Afegeix comanda»*): the turn a person chose with `rotate 90`, desat com qualsevol altre
// gust del vidre. `270` és el de sempre —la pàgina vertical sobre el vidre apaïsat— i el que no és cap dels quatre girs no és un gir
// (docs/30, docs/50).
const char kRotateKey[] = "rotate";
const uint16_t kRotationDefault = 270;
const size_t kLogLevelCap = 8;
const char kLockAfterKey[] = "lockafter";
const size_t kLockCap = 40;
const uint8_t kLockAfterDefault = 10;
// **Els tres temps de quietud que una persona pot triar** (0.190.13, l'amo: *«Rectifica els temps del DeepSleep, posa de 5,10,15
// minuts»*): five minutes, ten and fifteen —la llista de la pàgina és aquesta i un número que no hi sigui seria un ajust que la
// pàgina no sap ensenyar ni desfer—. I són els mateixos tres del pany, o sigui que una persona pot triar que el pany arribi
// abans que el son (5 de pany i 15 de son), que és l'ordre que té sentit (docs/30, docs/60).
const uint16_t kIdleSeconds[] = {300, 600, 900};
const uint16_t kIdleDefault = 300;
const char kOn[] = "on";
const char kOff[] = "off";
const char kMonday[] = "monday";
const char kSunday[] = "sunday";

hal::IStorage* g_card = nullptr;
hal::IStorage* g_flash = nullptr;
hal::IKeyValue* g_nvs = nullptr;
// What the person asked for, and it is what is saved.
char g_preference[kNameCap] = "auto";
// What is really behind `/cfg` after looking at what is mounted.
const char* g_unit = "";
// The keyboard the person chose, the id of one of the children of `ui`.
char g_keyboard[kNameCap] = "qwerty";
// The drawing of the logo shown when the board comes up.
char g_boot[kNameCap] = "boot";
// The app the board starts by itself, empty for none, and it starts as the build's default (0.119.0).
char g_home[kHomeCap] = LUNOK_HOME_APP;
// **L'app que obre el botó dels ajustos** (0.189.109): `settings` unless the store says another name, and empty when a person said
// `none` —a gear that does nothing is a choice and the panel says so before it asks for anything— (docs/30, docs/40).
char g_settings[kNameCap] = "settings";
// **El sostre de memòria que una persona ha triat** (0.192.94): zero és `auto`, o sigui la meitat del que la placa dona. És un número de
// kilobytes i no un punter: el que mana es decideix a l'arrencada de cada app, amb el que la placa diu en aquell moment (docs/20, docs/75).
uint32_t g_interpreter_mem_kb = 0;
// **El bloqueig de la pantalla** (0.189.150): the page, the switch and the minutes. The page starts as `device_lock` —the one
// this system carries for exactly this—, the switch off, and ten minutes, which is the middle of the three a person can pick.
char g_lock[kLockCap] = "device_lock";
bool g_lock_on = false;
// **I l'hora del cel, apagada de sèrie** (0.192.63): creure el GPS vol dir que el rellotge de la placa salta quan el cel ho diu, i això
// és una decisió de la persona —una placa que canvia d'hora tota sola quan el GPS té un mal dia és pitjor que una que va dos segons
// tard— (docs/30, docs/60).
bool g_gps_sync = false;
// **El son sol de la placa** (0.190.5): on by default, which is the rule of law 9; the page of the settings turns it off and this is what
// remembers the answer (docs/60).
bool g_sleep_on = true;
// **El dit, apagat de sèrie** (0.190.16): the board hears a finger only when a person asks for it (docs/50, docs/60).
bool g_wake_on_touch = false;
// **La becaina, encesa de sèrie** (0.190.63, l'amo: *«[X] Light sleep instead of wait»*): waiting awake is current nobody asked for and a
// light sleep keeps everything the system needs; a board that cannot nap ignores it, which is what `power::can_nap` says (docs/50, docs/60).
bool g_nap_on = true;
// **Apagat de sèrie** (0.190.180): encès, la placa és un teclat i no fa becaines; o sigui que el que no es demana no es paga (docs/60).
bool g_bt_on = false;
// **I la ràdio del wifi** (0.190.221): encesa vol dir el driver enlaire, que no és el mateix que estar connectat —una cosa que aquest
// sistema fa servir i deixa anar quan la feina s'acaba— (docs/30, docs/50).
bool g_wifi_on = false;
// **El valor de sortida d'`ansi`** (0.190.226): neix encès perquè el que hi ha escrit al codi són codis, i el que els ha de treure és el
// filtre, no l'escriptor (docs/30, docs/50).
bool g_ansi = true;
// **La xarxa d'aquesta placa, tal com es va desar**: dues claus en hexadecimal i res més, perquè el que mana la xarxa és la clau de xarxa i el
// que fa parlar els models és la d'aplicació (0.190.218, docs/50).
char g_mesh_net[kKeyTextCap] = "";
char g_mesh_app[kKeyTextCap] = "";
bool g_uart_wake = true;
// **Una frase i prou** (0.190.152): the report of the last deep sleep, as the board wrote it. It is a note for a person and not a setting, o
// sigui que no té valor per defecte ni es valida: el que hi hagi és el que la placa va dir (docs/50, docs/70).
char g_last_sleep[160] = {0};
// **I el nivell, que arrenca en `debug`** (0.190.89): the default is what this system has always written —everything—, i el fila el canvia.
char g_log_level[kLogLevelCap] = "debug";
// **El llum del vidre, de sèrie apagat i al mig** (0.190.106): off because a light nobody asked for is current nobody asked for, and half
// because a level that is picked for the first time should be worth looking at (docs/30, docs/50).
// **Adaptatiu de sèrie** (0.190.107): the board comes out of the box saving what can be saved —ten scans a second while nobody is on the
// glass— and a person who wants the glass at its top speed turns the switch off (docs/30, docs/50).
bool g_touch_adaptive = true;
// **Encès de sèrie** (0.190.110): el cercle de sota el vidre hi és a la placa i el que hi ha sota és un fals botó que només funciona perquè el
// dit toca el panell; o sigui que el sistema el fa servir si no li diuen el contrari (docs/30, docs/50).
bool g_software_button = true;
// **Encès de sèrie** (0.190.118): una llum de fons encesa en una placa que no mira ningú és el corrent que aquest sistema estalvia; si una
// persona vol la pàgina il·luminada tota l'estona, ho apaga aquí (docs/30, docs/50).
bool g_dim_when_idle = true;
// **El respir del vidre amb el cable posat** (0.190.162): encesa de sèrie —un cable que es veu sense mirar la barra— i la persona la pot treure
// si vol un vidre quiet mentre la placa es carrega (docs/30, docs/50).
bool g_breathe_on = true;
bool g_announce_charge = true;
bool g_backlight = false;
uint8_t g_backlight_level = 128;
// **El gir del vidre, i neix on el sistema sempre ha començat** (0.190.281): the portrait page of 540 × 960 over the landscape glass.
uint16_t g_rotation = kRotationDefault;
uint8_t g_lock_after = kLockAfterDefault;
// **Quant de quietud abans del son profund** (0.190.9, l'amo: *«afegir selector de temps per l'idle de settings->battery (per fer el
// DeepSleep)»*): segons, i la política de `power` el llegeix d'aquí en lloc de la constant de deu (docs/30, docs/60).
uint16_t g_idle_seconds = kIdleDefault;
// The minutes the local hour is ahead of UTC, which is zero until somebody says otherwise (0.125.0).
int16_t g_tz_minutes = 0;
// **The hour of summer is off until somebody says otherwise**: a clock that adds an hour on its own is a clock that lies in
// the winter (0.182.0, docs/60).
bool g_dst = false;
uint8_t g_week_start = 1;
// **A launcher shows the door's order until a person asks for another** (0.189.13): the count of uses is the launcher's
// and this is only the switch that says whether it sorts by it (docs/30, docs/40).
bool g_sort_by_usage = false;
// **Les tres velocitats viuen a `power` i no aquí** (0.189.107): the mask is the policy's own number —it is the governor that
// has to obey it— and this door reads and writes it there. A byte here and a byte there were eight of `.bss` in a build whose
// `dram0.bss` is exactly full (docs/75).

bool same(const char* a, const char* b) { return a != nullptr && b != nullptr && strcmp(a, b) == 0; }

// **Cada ajust que es desa, dit amb la seva clau** (0.190.111, l'amo: *«Quan a settings es seleccioni alguna cosa o canvii de valor, ho vull
// veure al log, amb la clau nvs que es pretén utilitzar»*). There is **one** door that writes a setting and it is this one: nobody in this file
// calls the store directly any more, o sigui que la línia del registre no es pot oblidar en un ajust i recordar en un altre —que és exactament
// com es perd una clau—. El valor es diu tal com queda desat, i el nom sencer `cfg/<clau>` és el que una persona busca amb `nvs` o amb `mem`
// (docs/30, docs/70).
//
// **I la botiga respon**: `set` contesta que sí quan ha pres el valor —i amb les escriptures aguantades vol dir «apuntat», no «a la flash»—, o
// sigui que la línia diu les dues coses: el que s'ha demanat i el que la botiga n'ha fet (docs/70, docs/75).
bool keep(const char* key, const char* value) {
  if (g_nvs == nullptr) {
    log_markf("NVS", "%s/%s = %s, and this board has no store to keep it", kNamespace, key, value);
    return false;
  }
  meter::Scope scope(meter::kKvSet);
  const bool taken = g_nvs->set(kNamespace, key, value, strlen(value));
  // **I amb l'etiqueta `NVS`, que és el que una persona busca** (0.190.114): the key of this system is `cfg/<key>` and who has it is the store;
  // la línia es diu com la cosa que és i no com el mòdul que la demana (docs/30, docs/70).
  log_markf("NVS", "%s/%s = %s%s", kNamespace, key, value, taken ? "" : " (the store refused it)");
  return taken;
}

// **The settings live in the key-value store**, which is a write to the flash and not a variable (docs/70, docs/75).
void remember() { (void)keep(kKey, g_preference); }

bool remember_keyboard() {
  if (g_nvs == nullptr) return false;
  return keep(kKeyboardKey, g_keyboard);
}

bool remember_boot() {
  if (g_nvs == nullptr) return false;
  return keep(kBootKey, g_boot);
}

// **None is a word and not an empty value** (0.119.0): the store keeps a string, and a key that was cleared has to
// read back as "no home app" on the next boot. `none` is therefore the one name an app cannot have as its home id,
// and the command says so where it is typed (docs/30, docs/70).
bool remember_home() {
  if (g_nvs == nullptr) return false;
  const char* value = (g_home[0] != '\0') ? g_home : "none";
  return keep(kHomeKey, value);
}

bool remember_dst() {
  if (g_nvs == nullptr) return false;
  const char* word = g_dst ? kOn : kOff;
  return keep(kDstKey, word);
}

bool remember_week() {
  if (g_nvs == nullptr) return false;
  const char* word = (g_week_start == 7) ? kSunday : kMonday;
  return keep(kWeekKey, word);
}

bool remember_turn() {
  if (g_nvs == nullptr) return false;
  char text[kTurnCap] = {0};
  snprintf(text, sizeof(text), "%u", static_cast<unsigned>(luabind::turn_budget()));
  return keep(kTurnKey, text);
}

bool remember_interpreter_mem() {
  if (g_nvs == nullptr) return false;
  char text[kMemCap] = {0};
  if (g_interpreter_mem_kb == 0) {
    snprintf(text, sizeof(text), "%s", "auto");
  } else {
    snprintf(text, sizeof(text), "%u", static_cast<unsigned>(g_interpreter_mem_kb));
  }
  return keep(kMemKey, text);
}

bool remember_cursor() {
  if (g_nvs == nullptr) return false;
  char text[kCursorCap] = {0};
  snprintf(text, sizeof(text), "%d", static_cast<int>(luabind::caret_blink_ms()));
  return keep(kCursorKey, text);
}

bool remember_sort() {
  if (g_nvs == nullptr) return false;
  const char* word = g_sort_by_usage ? kOn : kOff;
  return keep(kSortKey, word);
}

bool remember_settings() {
  if (g_nvs == nullptr) return false;
  const char* value = (g_settings[0] != '\0') ? g_settings : "none";
  return keep(kSettingsKey, value);
}

bool remember_lock() {
  if (g_nvs == nullptr) return false;
  const char* value = (g_lock[0] != '\0') ? g_lock : "none";
  return keep(kLockKey, value);
}

bool remember_wake_on_touch() {
  if (g_nvs == nullptr) return false;
  const char* word = g_wake_on_touch ? kOn : kOff;
  return keep(kTouchWakeKey, word);
}

bool remember_dim_idle() {
  if (g_nvs == nullptr) return false;
  const char* word = g_dim_when_idle ? kOn : kOff;
  return keep(kDimIdleKey, word);
}

bool remember_software_button() {
  if (g_nvs == nullptr) return false;
  const char* word = g_software_button ? kOn : kOff;
  return keep(kSoftwareButtonKey, word);
}

bool remember_touch_adaptive() {
  if (g_nvs == nullptr) return false;
  const char* word = g_touch_adaptive ? kOn : kOff;
  return keep(kTouchScanKey, word);
}

bool remember_backlight() {
  if (g_nvs == nullptr) return false;
  const char* word = g_backlight ? kOn : kOff;
  // **La llum són dues claus i les dues es diuen** (0.190.106): the switch and the level; una persona que en canvia una ha de veure les dues
  // línies, perquè el que es desa és el parell (docs/30, docs/70).
  char level[8] = {0};
  snprintf(level, sizeof(level), "%u", static_cast<unsigned>(g_backlight_level));
  const bool on_kept = keep(kBacklightKey, word);
  const bool level_kept = keep(kBacklightLevelKey, level);
  return on_kept && level_kept;
}

bool remember_rotation() {
  if (g_nvs == nullptr) return false;
  char word[8] = {0};
  snprintf(word, sizeof(word), "%u", static_cast<unsigned>(g_rotation));
  return keep(kRotateKey, word);
}

bool remember_log_level() {
  if (g_nvs == nullptr) return false;
  return keep(kLogLevelKey, g_log_level);
}

bool remember_wifi_on() { return keep(kWifiKey, g_wifi_on ? kOn : kOff); }

bool remember_ansi() { return keep(kAnsiKey, g_ansi ? kOn : kOff); }

// **Les dues claus es desen juntes**: una clau de xarxa sense la d'aplicació és una xarxa on ningú no es pot parlar, o sigui que el que es
// desa és la parella i el que no s'ha pogut desar es diu (docs/70).
bool remember_the_keys() {
  if (g_nvs == nullptr) return false;
  const bool net = keep(kMeshNetKey, g_mesh_net);
  const bool app = keep(kMeshAppKey, g_mesh_app);
  return net && app;
}

bool remember_announce_charge() {
  if (g_nvs == nullptr) return false;
  const char* word = g_announce_charge ? kOn : kOff;
  return keep(kAnnounceChargeKey, word);
}

bool remember_breathe() {
  if (g_nvs == nullptr) return false;
  const char* word = g_breathe_on ? kOn : kOff;
  return keep(kBreatheKey, word);
}

bool remember_uart_wake() {
  if (g_nvs == nullptr) return false;
  const char* word = g_uart_wake ? kOn : kOff;
  return keep(kUartWakeKey, word);
}

bool remember_nap_on() {
  if (g_nvs == nullptr) return false;
  const char* word = g_nap_on ? kOn : kOff;
  return keep(kNapKey, word);
}

bool remember_bt_on() {
  if (g_nvs == nullptr) return false;
  const char* word = g_bt_on ? kOn : kOff;
  return keep(kBtKey, word);
}

bool remember_sleep_on() {
  if (g_nvs == nullptr) return false;
  const char* word = g_sleep_on ? kOn : kOff;
  return keep(kSleepKey, word);
}

bool remember_idle() {
  if (g_nvs == nullptr) return false;
  char text[8];
  snprintf(text, sizeof(text), "%u", static_cast<unsigned>(g_idle_seconds));
  return keep(kIdleKey, text);
}

bool remember_gps_sync() {
  if (g_nvs == nullptr) return false;
  return keep(kGpsSyncKey, g_gps_sync ? kOn : kOff);
}

bool remember_lock_on() {
  if (g_nvs == nullptr) return false;
  const char* word = g_lock_on ? kOn : kOff;
  return keep(kLockOnKey, word);
}

bool remember_lock_after() {
  if (g_nvs == nullptr) return false;
  char text[8];
  snprintf(text, sizeof(text), "%u", static_cast<unsigned>(g_lock_after));
  return keep(kLockAfterKey, text);
}

bool remember_freq() {
  if (g_nvs == nullptr) return false;
  char text[kFreqCap] = {0};
  // **I el que es desa són el terra i el sostre** (0.190.175): a mask written by an older build can carry the middle bit, and a rule that
  // reads it back as if it were still a setting is a rule that lies about what the board does (docs/50, docs/60).
  if (!freq_text(text, sizeof(text), static_cast<uint8_t>(freq_mask() & 0x05))) return false;
  return keep(kFreqKey, text);
}

bool remember_tz() {
  if (g_nvs == nullptr) return false;
  char text[kTzCap] = {0};
  tz_text(text, sizeof(text), g_tz_minutes);
  return keep(kTzKey, text);
}

// Picks the unit and tells the VFS where `/cfg` points. The fallback is said out loud:
// asking for the card and getting the flash in silence would be the settings moving
// without anybody deciding it.
void apply() {
  const bool card_first = !same(g_preference, "littlefs");
  if (card_first && g_card != nullptr) {
    g_unit = "sd";
  } else if (g_flash != nullptr) {
    g_unit = "littlefs";
  } else if (g_card != nullptr) {
    g_unit = "sd";
  } else {
    g_unit = "";
  }

  if (same(g_preference, "sd") && g_card == nullptr) {
    log_printf(pal::kLogWarn, "CFG", "no card; the settings go to the flash");
  }
  hal::IStorage* storage = same(g_unit, "sd") ? g_card : g_flash;
  vfs::set_cfg_storage((g_unit[0] != '\0') ? storage : nullptr, g_unit);
  // **I el terra de les apps, que és la mateixa tria** (0.190.8, l'amo: *«perqué es perden les dades d'historic de Battery i
  // TimeClock al pujar un firmware nou? les dades no es guarden a l'SD ? … He perdut totes les dades d'avui»*): `/data/<id>/`
  // era el flaix sempre —`vfs::init` el muntava sobre littlefs i prou—, o sigui que `pio run -t uploadfs`, que reescriu la
  // partició littlefs sencera des de `build/disk/<entorn>`, s'enduia amb cada firmware el registre de la bateria, les marques
  // del rellotge, els events de l'agenda i les mostres del temps. Amb la targeta triada —o amb `auto`, que és la targeta quan
  // n'hi ha— `/data` és a la targeta, i això un flaix no ho toca (docs/30, docs/70, docs/80).
  //
  // **No és un ajust nou**: és el «disc preferit» que ja hi era (`cfg auto|sd|littlefs`, la fila *Preferred disk* de la
  // pestanya Disk) i que decidia només `/cfg`. Una segona preferència per a la mateixa pregunta seria dues portes que poden
  // dir coses diferents i vint bytes de DRAM que la Paper no té (mesurat el 18/11: `dram0_0_seg` overflowed by 24 bytes).
  if (g_unit[0] != '\0' && storage != nullptr) {
    storage->mkdir("data");
    vfs::set_data_storage(storage, g_unit);
    log_printf(pal::kLogInfo, "CFG", "the ground of the apps is the %s (/data is %s/data)",
               same(g_unit, "sd") ? "card" : "flash", same(g_unit, "sd") ? "/sd" : "/littlefs");
  }
}

}  // namespace

// **La darrera dormida, desada a la botiga** (0.190.151, l'amo: *«el botó de power fa un reset i tot s'envà a la merda»*): the RTC RAM of
// this board goes away with the supply, so the line the board can say at the boot that follows a deep sleep is written here while it
// is fresh, and `power last` answers from here when the power button has emptied everything else (docs/50, docs/70).
// **Fora de l'anonimat, que és el que fa que es pugui cridar**: a symbol inside it is private to this file and the link would not find
// it, which is exactly what happened when it was first written here (docs/95).
bool keep_the_last_sleep(const char* text) {
  if (text == nullptr || text[0] == '\0') return false;
  snprintf(g_last_sleep, sizeof(g_last_sleep), "%s", text);
  return keep(kLastSleepKey, g_last_sleep);
}

const char* the_last_sleep() { return g_last_sleep; }

void boot(hal::IPlatform& platform) {
  g_nvs = platform.nvs();
  g_card = platform.storage("/sd");
  g_flash = platform.storage("/littlefs");
  // **The home app starts at the name the build carries** (0.119.0) and only the store can move it: a second boot
  // of the same object —the tests boot the kernel again— must not inherit the name of the run before.
  snprintf(g_home, sizeof(g_home), "%s", LUNOK_HOME_APP);
  // The hour starts at UTC for the same reason: an emulator that boots twice must not inherit the offset of the run before.
  g_tz_minutes = 0;
  g_idle_seconds = kIdleDefault;
  // **And the calendar starts as this system was born**: the week from Monday, put in the calendar before the store is
  // read so that a second boot of the same object —the tests— does not inherit the run before (docs/60).
  g_dst = false;
  g_week_start = 1;
  calendar::set_week_start(1);

  char raw[kNameCap] = {0};
  size_t len = 0;
  // **The setting is read from the key-value store**, which is a chip and not a variable, and the
  // meter counts it like any other door (docs/70).
  bool found = false;
  if (g_nvs != nullptr) {
    meter::Scope scope(meter::kKvGet);
    found = g_nvs->get(kNamespace, kKey, raw, sizeof(raw) - 1, len) && len > 0;
  }
  if (found) {
    raw[len] = '\0';
    if (same(raw, "auto") || same(raw, "sd") || same(raw, "littlefs")) {
      snprintf(g_preference, sizeof(g_preference), "%s", raw);
    }
  }
  char raw_keyboard[kNameCap] = {0};
  size_t keyboard_len = 0;
  if (g_nvs != nullptr) {
    meter::Scope scope(meter::kKvGet);
    if (g_nvs->get(kNamespace, kKeyboardKey, raw_keyboard, sizeof(raw_keyboard) - 1, keyboard_len) &&
        keyboard_len > 0 && keyboard_len < sizeof(g_keyboard)) {
      raw_keyboard[keyboard_len] = '\0';
      snprintf(g_keyboard, sizeof(g_keyboard), "%s", raw_keyboard);
    }
  }

  // **The drawing of the logo, if the store says one that this build knows**: a word left by an older build —or by
  // a build with another drawing— falls back to the word `boot` and says so, because a boot that paints nothing out
  // of a setting nobody can read is a boot that hides a broken setting (docs/30, docs/50, docs/70).
  char raw_boot[kNameCap] = {0};
  size_t boot_len = 0;
  if (g_nvs != nullptr) {
    meter::Scope scope(meter::kKvGet);
    if (g_nvs->get(kNamespace, kBootKey, raw_boot, sizeof(raw_boot) - 1, boot_len) && boot_len > 0 &&
        boot_len < sizeof(g_boot)) {
      raw_boot[boot_len] = '\0';
      if (is_a_boot_word(raw_boot)) {
        snprintf(g_boot, sizeof(g_boot), "%s", raw_boot);
      } else {
        log_printf(pal::kLogWarn, "CFG", "no boot picture '%s' in this build; boot stays", raw_boot);
      }
    }
  }
  // **The app the board starts by itself, if the store says one** (0.119.0): the word `none` is a board that comes
  // up at the prompt, which is what a person gets when they clear it. A name that is not on the disk right now is
  // kept and not refused: the card that carries the app may be out of the slot this morning, and a boot that had to
  // argue about it would be a boot that cannot be told anything (docs/30, docs/40, docs/70).
  char raw_home[kHomeCap] = {0};
  size_t home_len = 0;
  if (g_nvs != nullptr) {
    meter::Scope scope(meter::kKvGet);
    if (g_nvs->get(kNamespace, kHomeKey, raw_home, sizeof(raw_home) - 1, home_len) && home_len > 0 &&
        home_len < sizeof(g_home)) {
      raw_home[home_len] = '\0';
      if (strcmp(raw_home, "none") == 0) {
        g_home[0] = '\0';
      } else {
        snprintf(g_home, sizeof(g_home), "%s", raw_home);
      }
    }
  }
  // **The hour of the person, if the store says one** (0.125.0): a value that does not parse —a text an older build
  // wrote, a zone this clock cannot have— falls back to UTC and says so, because a clock silently twelve hours off is
  // worse than one that never moved (docs/30, docs/60).
  char raw_tz[kTzCap] = {0};
  size_t tz_len = 0;
  if (g_nvs != nullptr) {
    meter::Scope scope(meter::kKvGet);
    if (g_nvs->get(kNamespace, kTzKey, raw_tz, sizeof(raw_tz) - 1, tz_len) && tz_len > 0 &&
        tz_len < sizeof(raw_tz)) {
      raw_tz[tz_len] = '\0';
      int32_t minutes = 0;
      if (parse_tz(raw_tz, minutes)) {
        g_tz_minutes = static_cast<int16_t>(minutes);
      } else {
        log_printf(pal::kLogWarn, "CFG", "tz '%s' is not an offset; the clock stays on UTC", raw_tz);
      }
    }
  }
  // **La clau del dia de traspàs d'una placa vella s'esborra** (0.192.57): a key nobody reads is a key a person sees with
  // `nvs` and cannot explain, i el que aquest build ha de deixar és un magatzem sense l'opció que ja no existeix. L'esborrar
  // d'una clau que no hi és no escriu res, o sigui que el preu d'això és una crida per arrencada —i es compta com el que és,
  // una escriptura, perquè un esborrar desgasta la flash com qualsevol altra (docs/60, docs/70, docs/75).
  if (g_nvs != nullptr) {
    meter::Scope scope(meter::kKvSet);
    if (g_nvs->erase(kNamespace, kLeapKey)) {
      log_printf(pal::kLogInfo, "CFG", "cfg/%s is gone: the leap day is counted from the date and not set", kLeapKey);
    }
  }
  char raw_dst[kNameCap] = {0};
  size_t dst_len = 0;
  if (g_nvs != nullptr) {
    meter::Scope scope(meter::kKvGet);
    if (g_nvs->get(kNamespace, kDstKey, raw_dst, sizeof(raw_dst) - 1, dst_len) && dst_len > 0 &&
        dst_len < sizeof(raw_dst)) {
      raw_dst[dst_len] = '\0';
      if (same(raw_dst, kOn) || same(raw_dst, kOff)) {
        g_dst = same(raw_dst, kOn);
      } else {
        log_printf(pal::kLogWarn, "CFG", "dst '%s' is not on or off; the hour of summer stays off", raw_dst);
      }
    }
  }
  char raw_week[kNameCap] = {0};
  size_t week_len = 0;
  if (g_nvs != nullptr) {
    meter::Scope scope(meter::kKvGet);
    if (g_nvs->get(kNamespace, kWeekKey, raw_week, sizeof(raw_week) - 1, week_len) && week_len > 0 &&
        week_len < sizeof(raw_week)) {
      raw_week[week_len] = '\0';
      if (same(raw_week, kMonday) || same(raw_week, kSunday)) {
        g_week_start = same(raw_week, kSunday) ? 7 : 1;
      } else {
        log_printf(pal::kLogWarn, "CFG", "week '%s' is not monday or sunday; the week starts on Monday", raw_week);
      }
    }
  }
  calendar::set_week_start(g_week_start);
  // **The slice of a turn, if the store says one** (0.174.7): a number that a slice cannot be —an older build's, a
  // typo, a zero— falls back to the one this build ships and says so, because an app that is never cut is a board that
  // stops answering the hand and nobody would know why (docs/20, docs/75).
  luabind::set_turn_budget(luabind::turn_budget_default());
  // **I el parpalleig del cursor torna al del build abans de llegir el disc** (0.189.34): the same rule the slice and the
  // offset follow —a second boot of the same object does not inherit what the run before wrote— (docs/30, docs/90).
  luabind::set_caret_blink_ms(luabind::caret_blink_default_ms());
  char raw_cursor[kCursorCap] = {0};
  size_t cursor_len = 0;
  if (g_nvs != nullptr) {
    meter::Scope scope(meter::kKvGet);
    if (g_nvs->get(kNamespace, kCursorKey, raw_cursor, sizeof(raw_cursor) - 1, cursor_len) && cursor_len > 0 &&
        cursor_len < sizeof(raw_cursor)) {
      raw_cursor[cursor_len] = '\0';
      int32_t delay = 0;
      if (parse_cursor_delay(raw_cursor, delay)) {
        luabind::set_caret_blink_ms(delay);
      } else {
        log_printf(pal::kLogWarn, "CFG", "cursor delay '%s' is not a blink of %d to %d ms; the cursor blinks at %d",
                   raw_cursor, static_cast<int>(luabind::caret_blink_min_ms()),
                   static_cast<int>(luabind::caret_blink_max_ms()), static_cast<int>(luabind::caret_blink_ms()));
      }
    }
  }
  // **I el sostre de memòria de l'intèrpret** (0.192.94): `auto` i una paraula que no és un número tornen al valor de sèrie, i el número es
  // queda com a número de kilobytes fins que algú el torni a mirar —és l'arrencada de cada app qui el compara amb el que la placa dona, i
  // allà es queda dins del que hi cap— (docs/20, docs/75).
  {
    char raw_mem[kMemCap] = {0};
    size_t mem_len = 0;
    if (g_nvs != nullptr) {
      meter::Scope scope(meter::kKvGet);
      if (g_nvs->get(kNamespace, kMemKey, raw_mem, sizeof(raw_mem) - 1, mem_len) && mem_len > 0 &&
          mem_len < sizeof(raw_mem)) {
        raw_mem[mem_len] = '\0';
        uint32_t kb = 0;
        if (parse_interpreter_mem(raw_mem, kb, 0)) {
          g_interpreter_mem_kb = kb;
        } else {
          log_printf(pal::kLogWarn, "CFG", "mem '%s' is not a number of kilobytes; the interpreter keeps the half of the board", raw_mem);
        }
      }
    }
  }
  char raw_turn[kTurnCap] = {0};
  size_t turn_len = 0;
  if (g_nvs != nullptr) {
    meter::Scope scope(meter::kKvGet);
    if (g_nvs->get(kNamespace, kTurnKey, raw_turn, sizeof(raw_turn) - 1, turn_len) && turn_len > 0 &&
        turn_len < sizeof(raw_turn)) {
      raw_turn[turn_len] = '\0';
      uint32_t instructions = 0;
      if (parse_turn(raw_turn, instructions)) {
        luabind::set_turn_budget(instructions);
      } else {
        log_printf(pal::kLogWarn, "CFG", "turn '%s' is not a slice of %u to %u instructions; turn stays at %u",
                   raw_turn, static_cast<unsigned>(luabind::turn_budget_min()),
                   static_cast<unsigned>(luabind::turn_budget_max()),
                   static_cast<unsigned>(luabind::turn_budget()));
      }
    }
  }
  // **L'app dels ajustos, si la botiga en diu una** (0.189.109): `none` is the word that puts the gear back to nothing, exactly like
  // the app of the start, and a name longer than an app of this system has is not a name and is said (docs/30, docs/40).
  snprintf(g_settings, sizeof(g_settings), "%s", "settings");
  {
    char raw_settings[kNameCap] = {0};
    size_t settings_len = 0;
    if (g_nvs != nullptr) {
      meter::Scope scope(meter::kKvGet);
      if (g_nvs->get(kNamespace, kSettingsKey, raw_settings, sizeof(raw_settings) - 1, settings_len) && settings_len > 0 &&
          settings_len < sizeof(raw_settings)) {
        raw_settings[settings_len] = '\0';
        if (same(raw_settings, "none")) {
          g_settings[0] = '\0';
        } else {
          snprintf(g_settings, sizeof(g_settings), "%s", raw_settings);
        }
      }
    }
  }
  // **El bloqueig de la pantalla, si la botiga en diu res** (0.189.150): the page, the switch and the minutes. A `lockon` that
  // is neither `on` nor `off` leaves the switch off and says so —a switch that reads half a word is a board that locks
  // itself or does not for a reason nobody can see— and the minutes only take the three the page offers.
  {
    char raw_lock[kLockCap] = {0};
    size_t lock_len = 0;
    if (g_nvs != nullptr) {
      meter::Scope scope(meter::kKvGet);
      if (g_nvs->get(kNamespace, kLockKey, raw_lock, sizeof(raw_lock) - 1, lock_len) && lock_len > 0 &&
          lock_len < sizeof(raw_lock)) {
        raw_lock[lock_len] = '\0';
        if (same(raw_lock, "none")) {
          g_lock[0] = '\0';
        } else {
          snprintf(g_lock, sizeof(g_lock), "%s", raw_lock);
        }
      }
      char raw_sleep[8] = {0};
      size_t sleep_len = 0;
      if (g_nvs->get(kNamespace, kSleepKey, raw_sleep, sizeof(raw_sleep) - 1, sleep_len) && sleep_len > 0 &&
          sleep_len < sizeof(raw_sleep)) {
        raw_sleep[sleep_len] = '\0';
        if (same(raw_sleep, kOn) || same(raw_sleep, kOff)) {
          g_sleep_on = same(raw_sleep, kOn);
        } else {
          log_printf(pal::kLogWarn, "CFG", "sleep '%s' is not on or off; the board keeps its own policy", raw_sleep);
        }
      }
      char raw_bt[8] = {0};
      size_t bt_len = 0;
      if (g_nvs->get(kNamespace, kBtKey, raw_bt, sizeof(raw_bt) - 1, bt_len) && bt_len > 0 && bt_len < sizeof(raw_bt)) {
        raw_bt[bt_len] = '\0';
        if (same(raw_bt, kOn) || same(raw_bt, kOff)) {
          g_bt_on = same(raw_bt, kOn);
        } else {
          log_printf(pal::kLogWarn, "CFG", "bt '%s' is not on or off; the keyboard stays off", raw_bt);
        }
      }
      // **I la ràdio del wifi** (0.190.221): es llegeix aquí amb tota la resta i s'aplica més avall, perquè `cfg::boot` passa abans que
      // `net::boot` i la ràdio encara no té porta en aquest moment.
      char raw_wifi[8] = {0};
      size_t wifi_len = 0;
      if (g_nvs->get(kNamespace, kWifiKey, raw_wifi, sizeof(raw_wifi) - 1, wifi_len) && wifi_len > 0 &&
          wifi_len < sizeof(raw_wifi)) {
        raw_wifi[wifi_len] = '\0';
        if (same(raw_wifi, kOn) || same(raw_wifi, kOff)) {
          g_wifi_on = same(raw_wifi, kOn);
        } else {
          log_printf(pal::kLogWarn, "CFG", "wifi '%s' is not on or off; the radio stays off", raw_wifi);
        }
      }
      // **I el color** (0.190.226): la paraula es llegeix amb tota la resta, i el que no és `on` ni `off` no la toca —el valor de fàbrica és
      // `on` i una paraula trencada no ha de decidir res (docs/30, docs/50).
      char raw_ansi[8] = {0};
      size_t ansi_len = 0;
      if (g_nvs->get(kNamespace, kAnsiKey, raw_ansi, sizeof(raw_ansi) - 1, ansi_len) && ansi_len > 0 &&
          ansi_len < sizeof(raw_ansi)) {
        raw_ansi[ansi_len] = '\0';
        if (same(raw_ansi, kOn) || same(raw_ansi, kOff)) {
          g_ansi = same(raw_ansi, kOn);
        } else {
          log_printf(pal::kLogWarn, "CFG", "ansi '%s' is not on or off; the codes stay on", raw_ansi);
        }
      }
      // **I les claus del mesh** (0.190.218): es llegeixen aquí amb tota la resta, o sigui que la xarxa d'aquesta placa torna amb la placa
      // i no espera que ningú torni a escriure les claus (docs/30, docs/50).
      char raw_net[kKeyTextCap] = {0};
      size_t net_len = 0;
      if (g_nvs->get(kNamespace, kMeshNetKey, raw_net, sizeof(raw_net) - 1, net_len) && net_len == kKeyTextCap - 1) {
        raw_net[net_len] = '\0';
        snprintf(g_mesh_net, sizeof(g_mesh_net), "%s", raw_net);
      }
      char raw_app[kKeyTextCap] = {0};
      size_t app_len = 0;
      if (g_nvs->get(kNamespace, kMeshAppKey, raw_app, sizeof(raw_app) - 1, app_len) && app_len == kKeyTextCap - 1) {
        raw_app[app_len] = '\0';
        snprintf(g_mesh_app, sizeof(g_mesh_app), "%s", raw_app);
      }
      char raw_note[sizeof(g_last_sleep)] = {0};
      size_t note_len = 0;
      if (g_nvs->get(kNamespace, kLastSleepKey, raw_note, sizeof(raw_note) - 1, note_len) && note_len > 0 &&
          note_len < sizeof(raw_note)) {
        raw_note[note_len] = '\0';
        snprintf(g_last_sleep, sizeof(g_last_sleep), "%s", raw_note);
      }
      char raw_uart[8] = {0};
      size_t uart_len = 0;
      if (g_nvs->get(kNamespace, kUartWakeKey, raw_uart, sizeof(raw_uart) - 1, uart_len) && uart_len > 0 &&
          uart_len < sizeof(raw_uart)) {
        raw_uart[uart_len] = '\0';
        if (same(raw_uart, kOn) || same(raw_uart, kOff)) {
          g_uart_wake = same(raw_uart, kOn);
        } else {
          log_printf(pal::kLogWarn, "CFG", "uartwake '%s' is not on or off; the console stays a door", raw_uart);
        }
      }
      char raw_announce[8] = {0};
      size_t announce_len = 0;
      if (g_nvs->get(kNamespace, kAnnounceChargeKey, raw_announce, sizeof(raw_announce) - 1, announce_len) &&
          announce_len > 0 && announce_len < sizeof(raw_announce)) {
        raw_announce[announce_len] = '\0';
        if (same(raw_announce, kOn) || same(raw_announce, kOff)) {
          g_announce_charge = same(raw_announce, kOn);
        } else {
          log_printf(pal::kLogWarn, "CFG", "announcecharge '%s' is not on or off; the cable says nothing", raw_announce);
        }
      }
      char raw_breathe[8] = {0};
      size_t breathe_len = 0;
      if (g_nvs->get(kNamespace, kBreatheKey, raw_breathe, sizeof(raw_breathe) - 1, breathe_len) && breathe_len > 0 &&
          breathe_len < sizeof(raw_breathe)) {
        raw_breathe[breathe_len] = '\0';
        if (same(raw_breathe, kOn) || same(raw_breathe, kOff)) {
          g_breathe_on = same(raw_breathe, kOn);
        } else {
          log_printf(pal::kLogWarn, "CFG", "breathe '%s' is not on or off; the light stays still on a cable", raw_breathe);
        }
      }
      char raw_nap[8] = {0};
      size_t nap_len = 0;
      if (g_nvs->get(kNamespace, kNapKey, raw_nap, sizeof(raw_nap) - 1, nap_len) && nap_len > 0 &&
          nap_len < sizeof(raw_nap)) {
        raw_nap[nap_len] = '\0';
        if (same(raw_nap, kOn) || same(raw_nap, kOff)) {
          g_nap_on = same(raw_nap, kOn);
        } else {
          log_printf(pal::kLogWarn, "CFG", "nap '%s' is not on or off; the waits stay awake", raw_nap);
        }
      }
      // **I el llindar del registre** (0.190.89): the word of the row of the System tab; el ring no filtra res mentre no se sap què vol
      // la persona, i el que es llegeix aquí és el que val per a aquest run (docs/30, docs/50).
      char raw_log[kLogLevelCap] = {0};
      size_t log_len = 0;
      if (g_nvs->get(kNamespace, kLogLevelKey, raw_log, sizeof(raw_log) - 1, log_len) && log_len > 0 &&
          log_len < sizeof(raw_log)) {
        raw_log[log_len] = '\0';
        pal::LogLevel at = pal::kLogDebug;
        if (log_level_from_name(raw_log, &at)) {
          snprintf(g_log_level, sizeof(g_log_level), "%s", log_level_name(at));
        } else {
          log_printf(pal::kLogWarn, "CFG", "loglevel '%s' is not debug, info, warn or error; the log keeps everything", raw_log);
        }
      }
      pal::LogLevel restored = pal::kLogDebug;
      if (log_level_from_name(g_log_level, &restored)) log_set_level(restored);
      // **I els tres ajustos de la 0.190.106 i la 0.190.110, llegits com tots els altres** (0.190.111): the light of the glass, its level,
      // the button under the glass and the scan of the panel were read **inside the block of the log level**, o sigui que només tornaven si la
      // clau `loglevel` era a la botiga —que és el cas estrany—. Amb la clau absent, un commutador que una persona havia encès tornava apagat a
      // cada arrencada, i això era el que no anava bé (docs/30, docs/70).
      char raw_dim[8] = {0};
      size_t dim_len = 0;
      if (g_nvs->get(kNamespace, kDimIdleKey, raw_dim, sizeof(raw_dim) - 1, dim_len) && dim_len > 0 &&
          dim_len < sizeof(raw_dim)) {
        raw_dim[dim_len] = '\0';
        if (same(raw_dim, kOn) || same(raw_dim, kOff)) {
          g_dim_when_idle = same(raw_dim, kOn);
        } else {
          log_printf(pal::kLogWarn, "CFG", "dimidle '%s' is not on or off; the glass's light goes down when idle", raw_dim);
        }
      }
      char raw_sw[8] = {0};
      size_t sw_len = 0;
      if (g_nvs->get(kNamespace, kSoftwareButtonKey, raw_sw, sizeof(raw_sw) - 1, sw_len) && sw_len > 0 &&
          sw_len < sizeof(raw_sw)) {
        raw_sw[sw_len] = '\0';
        if (same(raw_sw, kOn) || same(raw_sw, kOff)) {
          g_software_button = same(raw_sw, kOn);
        } else {
          log_printf(pal::kLogWarn, "CFG", "swbutton '%s' is not on or off; the button under the glass stays used", raw_sw);
        }
      }
      char raw_scan[8] = {0};
      size_t scan_len = 0;
      if (g_nvs->get(kNamespace, kTouchScanKey, raw_scan, sizeof(raw_scan) - 1, scan_len) && scan_len > 0 &&
          scan_len < sizeof(raw_scan)) {
        raw_scan[scan_len] = '\0';
        if (same(raw_scan, kOn) || same(raw_scan, kOff)) {
          g_touch_adaptive = same(raw_scan, kOn);
        } else {
          log_printf(pal::kLogWarn, "CFG", "touchscan '%s' is not on or off; the scan stays adaptive", raw_scan);
        }
      }
      char raw_light[8] = {0};
      size_t light_len = 0;
      if (g_nvs->get(kNamespace, kBacklightKey, raw_light, sizeof(raw_light) - 1, light_len) && light_len > 0 &&
          light_len < sizeof(raw_light)) {
        raw_light[light_len] = '\0';
        if (same(raw_light, kOn) || same(raw_light, kOff)) {
          g_backlight = same(raw_light, kOn);
        } else {
          log_printf(pal::kLogWarn, "CFG", "backlight '%s' is not on or off; the glass stays dark", raw_light);
        }
      }
      char raw_level[8] = {0};
      size_t level_len = 0;
      if (g_nvs->get(kNamespace, kBacklightLevelKey, raw_level, sizeof(raw_level) - 1, level_len) && level_len > 0 &&
          level_len < sizeof(raw_level)) {
        raw_level[level_len] = '\0';
        const long asked = strtol(raw_level, nullptr, 10);
        if (asked > 0 && asked <= 255) {
          g_backlight_level = static_cast<uint8_t>(asked);
        } else {
          log_printf(pal::kLogWarn, "CFG", "backlightlevel '%s' is not 1 to 255; the light stays at %u", raw_level,
                     static_cast<unsigned>(g_backlight_level));
        }
      }
      // **I el gir del vidre** (0.190.281): a word that is not one of the four leaves the page where it has always been —270— and
      // ho diu, com fa la resta de gustos del vidre (docs/30, docs/50).
      char raw_rotate[8] = {0};
      size_t rotate_len = 0;
      if (g_nvs->get(kNamespace, kRotateKey, raw_rotate, sizeof(raw_rotate) - 1, rotate_len) && rotate_len > 0 &&
          rotate_len < sizeof(raw_rotate)) {
        raw_rotate[rotate_len] = '\0';
        const long asked = strtol(raw_rotate, nullptr, 10);
        if (asked == 0 || asked == 90 || asked == 180 || asked == 270) {
          g_rotation = static_cast<uint16_t>(asked);
        } else {
          log_printf(pal::kLogWarn, "CFG", "rotate '%s' is not 0, 90, 180 or 270; the page stays at %u", raw_rotate,
                     static_cast<unsigned>(g_rotation));
        }
      }
      char raw_touch[8] = {0};
      size_t touch_len = 0;
      if (g_nvs->get(kNamespace, kTouchWakeKey, raw_touch, sizeof(raw_touch) - 1, touch_len) && touch_len > 0 &&
          touch_len < sizeof(raw_touch)) {
        raw_touch[touch_len] = '\0';
        if (same(raw_touch, kOn) || same(raw_touch, kOff)) {
          g_wake_on_touch = same(raw_touch, kOn);
        } else {
          log_printf(pal::kLogWarn, "CFG", "touchwake '%s' is not on or off; the glass is not a door", raw_touch);
        }
      }
      char raw_gps[8] = {0};
      size_t gps_len = 0;
      if (g_nvs->get(kNamespace, kGpsSyncKey, raw_gps, sizeof(raw_gps) - 1, gps_len) && gps_len > 0 &&
          gps_len < sizeof(raw_gps)) {
        raw_gps[gps_len] = '\0';
        if (same(raw_gps, kOn) || same(raw_gps, kOff)) {
          g_gps_sync = same(raw_gps, kOn);
        } else {
          log_printf(pal::kLogWarn, "CFG", "gpssync '%s' is not on or off; the sky does not set the clock", raw_gps);
        }
      }
      char raw_on[8] = {0};
      size_t on_len = 0;
      if (g_nvs->get(kNamespace, kLockOnKey, raw_on, sizeof(raw_on) - 1, on_len) && on_len > 0 &&
          on_len < sizeof(raw_on)) {
        raw_on[on_len] = '\0';
        if (same(raw_on, kOn) || same(raw_on, kOff)) {
          g_lock_on = same(raw_on, kOn);
        } else {
          log_printf(pal::kLogWarn, "CFG", "lock '%s' is not on or off; the screen is not locked", raw_on);
        }
      }
      char raw_after[8] = {0};
      size_t after_len = 0;
      if (g_nvs->get(kNamespace, kLockAfterKey, raw_after, sizeof(raw_after) - 1, after_len) && after_len > 0 &&
          after_len < sizeof(raw_after)) {
        raw_after[after_len] = '\0';
        const int minutes = atoi(raw_after);
        if (minutes == 5 || minutes == 10 || minutes == 15) {
          g_lock_after = static_cast<uint8_t>(minutes);
        } else {
          log_printf(pal::kLogWarn, "CFG", "lock after '%s' is not 5, 10 or 15 minutes; %u stays", raw_after,
                     static_cast<unsigned>(g_lock_after));
        }
      }
    }
  }
  // **I quanta quietud vol la persona abans del son** (0.190.9), amb la mateixa forma que els minuts del pany: un número que no
  // és cap dels tres es queda on era i ho diu (docs/30, docs/60).
  {
    char raw_idle[8] = {0};
    size_t idle_len = 0;
    if (g_nvs != nullptr &&
        g_nvs->get(kNamespace, kIdleKey, raw_idle, sizeof(raw_idle) - 1, idle_len) && idle_len > 0 &&
        idle_len < sizeof(raw_idle)) {
      raw_idle[idle_len] = '\0';
      const int seconds = atoi(raw_idle);
      if (seconds == 300 || seconds == 600 || seconds == 900) {
        g_idle_seconds = static_cast<uint16_t>(seconds);
      } else {
        log_printf(pal::kLogWarn, "CFG", "idle '%s' is not 5, 10 or 15 minutes; %u s stays", raw_idle,
                   static_cast<unsigned>(g_idle_seconds));
      }
    }
  }
  // **Les velocitats permeses, llegides abans d'aplicar res**: the mask is what the governor may use, so it has to be in
  // `power` before the first paint of the boot asks for the top step (0.189.107, docs/50, docs/60).
  power::set_allowed_steps(kFreqAll);
  {
    char raw_freq[kFreqCap] = {0};
    size_t freq_len = 0;
    if (g_nvs != nullptr) {
      meter::Scope scope(meter::kKvGet);
      if (g_nvs->get(kNamespace, kFreqKey, raw_freq, sizeof(raw_freq) - 1, freq_len) && freq_len > 0 &&
          freq_len < sizeof(raw_freq)) {
        raw_freq[freq_len] = '\0';
        uint8_t mask = 0;
        if (parse_freq(raw_freq, mask)) {
          power::set_allowed_steps(mask);
        } else {
          log_printf(pal::kLogWarn, "CFG", "freq '%s' is not a list of 80, 160 and 240; the board may use all three",
                     raw_freq);
        }
      }
    }
  }
  apply();
  // **I si el llançador ordena les apps pels usos** (0.189.13): a word that is neither `on` nor `off` leaves the switch
  // where it was —off— and says so, like the calendar does with a week that is not a week (docs/30, docs/40).
  char raw_sort[kNameCap] = {0};
  size_t sort_len = 0;
  if (g_nvs != nullptr) {
    meter::Scope scope(meter::kKvGet);
    if (g_nvs->get(kNamespace, kSortKey, raw_sort, sizeof(raw_sort) - 1, sort_len) && sort_len > 0 &&
        sort_len < sizeof(raw_sort)) {
      raw_sort[sort_len] = '\0';
      if (same(raw_sort, kOn) || same(raw_sort, kOff)) {
        g_sort_by_usage = same(raw_sort, kOn);
      } else {
        log_printf(pal::kLogWarn, "CFG", "sort '%s' is not on or off; the launcher keeps the door's order", raw_sort);
      }
    }
  }
  char freq_words[kFreqCap] = {0};
  freq_text(freq_words, sizeof(freq_words), freq_mask());
  // **Tres línies i no una, i la tercera vegada que es parteix** (0.189.150 i 0.190.32): the summary grew past the hundred
  // and ninety-two bytes of a line of the registry twice, o sigui que la cua s'escrivia i no es veia. La primera línia és
  // **el disc i el calendari**, la segona **la política del processador i de la pàgina**, i la tercera **les apps que una
  // persona ha triat**, que és la pestanya System llegida en veu alta. **I el motiu de la tercera**: un magatzem amb un nom
  // llarg —`littlefs` en lloc de `sd`— ja torna a passar dels cent noranta-dos bytes i el `cpu allows 80,160,240` sortia com
  // `cpu allows 80,16…` al log del 19/11 (docs/30, docs/45).
  // **I el llindar del registre, que decideix quantes d'aquestes línies es veuen** (0.190.89): a line that says the level is written
  // after the filter is already set, or the summary of the boot would be the first thing the filter hides (docs/30, docs/50).
  log_printf(pal::kLogInfo, "CFG", "/cfg on %s (asked for %s), keyboard %s, log %s, boot %s, tz %+03d:%02d, dst %s, week %s",
             (g_unit[0] != '\0') ? g_unit : "nowhere", g_preference, g_keyboard, g_log_level, g_boot, g_tz_minutes / 60,
             (g_tz_minutes < 0 ? -g_tz_minutes : g_tz_minutes) % 60, g_dst ? kOn : kOff,
             (g_week_start == 7) ? kSunday : kMonday);
  // **I la velocitat de la CPU, que es deia a mitges** (0.190.86): the line said which steps are allowed and nothing about
  // the two things that decide what the board actually does —whether the scaling is on, and which step a person chose—, o sigui
  // que una placa amb l'escala apagada i la CPU pujant a 240 era exactament el cas que no es podia veure d'aquesta línia (i el
  // `cpu allows` hi sortia sense l'espai, que és el senyal que la línia ja anava curta) (docs/50, docs/60).
  log_printf(pal::kLogInfo, "CFG",
             "the page: turn %u, sort apps by usage %s, cursor delay %d ms, cpu %s (allows %s, speed %u), nap %s",
             static_cast<unsigned>(luabind::turn_budget()), g_sort_by_usage ? kOn : kOff,
             static_cast<int>(luabind::caret_blink_ms()), power::cpu_auto() ? kOn : kOff, freq_words,
             static_cast<unsigned>(power::resting_cpu_mhz()), g_nap_on ? kOn : kOff);
  // **I els ajustos del maquinari, que també es llegeixen a l'arrencada** (0.190.111): the glass's light, the button under it and the scans of
  // the panel; una línia que diu el que ha tornat, perquè un commutador que no sobreviu a un reinici es vegi en una línia i no en tres pantalles
  // (docs/30, docs/70).
  log_printf(pal::kLogInfo, "CFG",
             "the glass: backlight %s at %u, out when idle %s, button under it %s, adaptive scan %s, wake on touch %s, "
             "turned %u",
             g_backlight ? kOn : kOff, static_cast<unsigned>(g_backlight_level), g_dim_when_idle ? kOn : kOff,
             g_software_button ? kOn : kOff, g_touch_adaptive ? kOn : kOff, g_wake_on_touch ? kOn : kOff,
             static_cast<unsigned>(g_rotation));
  log_printf(pal::kLogInfo, "CFG", "and on a cable the glass breathes %s and the picture of the charge is %s",
             g_breathe_on ? kOn : kOff, g_announce_charge ? kOn : kOff);
  // **I la consola com a porta de la becaina, i només de la becaina** (0.190.147, 0.190.152, l'amo: *«l'UART si que ha de despertar el
  // lightSleep, si no no es pot usar la shell per serial (es menja caràcters) pero no de DeepSleep»*): the switch is said with the two
  // halves of its meaning in one line, perquè una persona ha de veure d'un cop que el son profund no en depèn pas (docs/30, docs/50).
  log_printf(pal::kLogInfo, "CFG", "the console wakes a nap and never a deep sleep: %s", g_uart_wake ? kOn : kOff);
  // **I si l'hora del cel mana** (0.192.63): una línia i prou, perquè és una decisió que es pren una vegada i que després ningú no
  // recorda haver pres (docs/30, docs/60).
  log_printf(pal::kLogInfo, "CFG", "the clock is set from the sky: %s", g_gps_sync ? kOn : kOff);
  log_printf(pal::kLogInfo, "CFG", "the apps a person chose: home %s, settings %s, lock %s; the lock is %s and waits %u min",
             (g_home[0] != '\0') ? g_home : "none", (g_settings[0] != '\0') ? g_settings : "none",
             (g_lock[0] != '\0') ? g_lock : "none", g_lock_on ? kOn : kOff, static_cast<unsigned>(g_lock_after));

  // **I que la botiga ha contestat, dit a la primera línia de totes** (0.190.115, l'amo: *«I on dius que es llegeix al boot l'NVS?»*). Les
  // vint-i-cinc lectures d'aquest `boot` són **silencioses** —una línia per clau serien vint-i-cinc línies dient que tot va bé—, o sigui que el
  // que faltava era **una** línia que digués que la botiga s'ha llegit i quant hi havia: el nombre de claus del seu espai és el que una persona
  // compara amb el que creu haver desat, i `0` vol dir que la botiga contesta però és buida (una placa nova) o que no contesta (docs/30, docs/70).
  //
  // **I és una marca**: aquesta no l'ha de poder amagar un nivell de registre, perquè és la resposta a «on es llegeix l'NVS» (docs/70, docs/75).
  uint32_t kept = 0;
  if (g_nvs != nullptr) {
    const char* key = nullptr;
    bool first = true;
    while (g_nvs->next(kNamespace, key, first)) ++kept;
  }
  log_markf("NVS", "the store was read at the boot: %u setting(s) in '%s', and /cfg is on %s", static_cast<unsigned>(kept),
            kNamespace, (g_unit[0] != '\0') ? g_unit : "nowhere");
}

// **I el teclat s'obre quan el vidre ja hi és, i no abans** (0.191.1, l'amo: *«abort() was called at PC 0x420c49d7 … could not allocate LUT»*):
// això era la meitat del `cfg::boot` que encén la ràdio si la persona la va deixar encesa, i el seu lloc era **abans de la primera pàgina**
// —una app que escriu amb ella no ho ha de demanar cada cop— però no abans del **panell**: NimBLE s'hi queda gairebé trenta kilobytes de
// memòria interna en un grapat de blocs (`the radio is up … 29544 B spent`), i el panell d'aquesta placa en demana **un de sol de 64 KiB**
// per a la seva taula de conversió (`epd_init(…, EPD_LUT_64K)` → `heap_caps_malloc(64 KiB, MALLOC_CAP_INTERNAL)`). Amb la ràdio a dalt
// queden cent quaranta kilobytes lliures **en trossos** i el bloc més gran no arriba als seixanta-quatre: la taula no es pot fer i epdiy
// crida `abort()`. **El que es paga amb aquest ordre no és res**: el vidre no llegeix la ràdio per a res i el primer que la vol és una app,
// que ve molt després (docs/50, docs/75).
//
// **I la lliçó és per a qualsevol cosa gran**: el que demana un bloc contigu es fa abans del que deixa un munt de blocs petits, i el que és
// gran i contigu en aquesta casa és la taula del panell.
void start_the_radio() {
  if (g_bt_on && !bt::is_on()) {
    if (!bt::start(nullptr)) g_bt_on = false;
  }
  // **I el teclat al qual aquesta placa es va lligar, de tornada** (0.190.191, l'amo: *«Em sembla bé reconnectar»*): the bonds are in
  // the radio's store, o sigui que el que calia era preguntar-li-ho a l'arrencada en lloc d'esperar que una persona ho tornés a fer.
  // Llança el link i no espera —el boot no ha de costar mig segon de prompt— i si el teclat no hi és, no passa res: l'`bt scan` hi és
  // per quan en vulgui un altre (docs/50).
  //
  // **I només si aquesta placa ha aparellat alguna cosa alguna vegada** (0.190.203): preguntar-ho a la botiga del ràdio vol la pila a
  // dalt, i engegar-la en una placa verge només per sentir que no hi ha res es paga amb el que costa un ràdio encès —i, la primera
  // vegada, amb una calibració sencera del PHY—. `host_remembers` és un byte a la NVS i no obre la pila; el que sí que ho fa és el
  // `bt host reconnect` del prompt, que passa per sobre d'aquesta línia perquè allà ho ha demanat una persona (docs/50, docs/60).
  if (bt::can_host() && bt::host_remembers()) bt::host_reconnect();
}

const char* unit() { return g_unit; }

// **The slice of a turn: read in the module that counts it and written here** (0.174.7). The door answers the number in
// force —the block ceiling of what was asked— and not the one that was typed, so what `cfg turn` prints is what the hook
// compares with (docs/20, docs/75).
uint32_t turn_instructions() { return luabind::turn_budget(); }

// **El sostre de l'intèrpret, i el que mana quan ningú no n'ha dit res** (0.192.94): the share is the half of what the board answers, i el
// número que una persona ha triat no pot ser més que el que el xip té —un sostre per sobre del que hi ha no és un sostre, és una promesa—.
// Una placa que no en diu cap (l'ATOM, sense PSRAM) no té meitat i el que mana és el de sèrie de l'intèrpret, que és cosa de `shell`.
uint32_t interpreter_mem_kb() { return g_interpreter_mem_kb; }

uint32_t interpreter_mem_default_kb(uint32_t board_kb) { return board_kb / 2; }

// **The launcher's switch** (0.189.13): off until the store says `on`, and it is a word and not a bit for the same reason
// the calendar's is —a person reads the store with `nvs` (docs/30, docs/70).
bool sort_by_usage() { return g_sort_by_usage; }

// **I el son sol** (0.190.5): the value of the page is the policy's (`power`), so writing it here turns the board's rule off in the same
// moment a person moves the switch instead of on the next boot (docs/60).
bool sleep_on() { return g_sleep_on; }

// **I el dit** (0.190.16): the value is the setting the system keeps, i el boot l'empeny al maquinari perquè la placa es construeix abans
// que els ajustos es llegeixin (docs/30, docs/50). **Una placa que no hi sent no el refusa** (0.190.17, l'amo: *«a koala, atom i matri[x]
// simplement s'ignora el paràmetre»*): el desa i no en fa res, que és el que una persona demana quan el toca en una placa sense tàctil.
bool wake_on_touch() { return g_wake_on_touch; }

bool set_wake_on_touch(bool on) {
  g_wake_on_touch = on;
  (void)power::set_wake_on_touch(on);
  return remember_wake_on_touch();
}

// **I la becaina** (0.190.63): the value of the page is the policy's (`power`), like the sleep's, so moving the switch changes what the
// board does in the same moment and not on the next boot (docs/30, docs/60).
bool nap_on() { return g_nap_on; }

// **I el teclat, que mana sobre la son mentre viu** (0.190.180): the switch does the real thing and not just a word, perquè la fila de
// Settings ha de poder encendre el ràdio i no esperar la propera arrencada (docs/30, docs/50).
bool bt_on() { return g_bt_on; }

// **El commutador de la ràdio del wifi** (0.190.221): el que hi ha desat, i res més; qui l'encén és `set_wifi_on`, que és el que el prompt
// i la targeta criden (docs/30, docs/50).
bool wifi_on() { return g_wifi_on; }

// **El commutador del color** (0.190.226): no hi ha cap ràdio a encendre ni cap aparell a preguntar, o sigui que el que fa és desar la paraula
// i prou; qui la llegeix és el filtre de la consola (docs/30, docs/50).
bool ansi_on() { return g_ansi; }

bool set_ansi(bool on) {
  g_ansi = on;
  (void)remember_ansi();
  return g_ansi == on;
}

bool set_wifi_on(bool on) {
  const bool was = g_wifi_on;
  g_wifi_on = on;
  if (on && !was) {
    // **Si la ràdio no arrenca, el commutador no es queda encès**: una paraula desada que no és certa a la placa és pitjor que un
    // commutador que torna enrere i una línia al registre (0.190.183, docs/30).
    if (!net::radio_on()) {
      g_wifi_on = false;
      log_printf(pal::kLogWarn, "CFG", "the wifi radio did not come up, and the switch stays off");
    }
  } else if (!on && was) {
    (void)net::radio_off();
  }
  (void)remember_wifi_on();
  return g_wifi_on == on;
}

// **La xarxa que aquesta placa té a les mans** (0.190.218): el que hi ha desat, i res més —qui les fa o les reparteix és el mesh i no la
// botiga—, o sigui que una placa que no ha fet mai `mesh key` torna la cadena buida (docs/50).
const char* mesh_net_key() { return g_mesh_net; }
const char* mesh_app_key() { return g_mesh_app; }

// I desar-les és la parella sencera: la de xarxa i la d'aplicació, que és l'única cosa que fa una xarxa que serveixi (docs/50).
bool set_mesh_keys(const char* net, const char* app) {
  if (net == nullptr || app == nullptr) return false;
  snprintf(g_mesh_net, sizeof(g_mesh_net), "%s", net);
  snprintf(g_mesh_app, sizeof(g_mesh_app), "%s", app);
  return remember_the_keys();
}

// **I la consola, que és una porta del maquinari com la del vidre** (0.190.147): the value of the page is the board's, so moving the switch
// changes what the next nap does and not the next boot (docs/30, docs/60).
bool uart_wake() { return g_uart_wake; }

bool set_uart_wake(bool on) {
  g_uart_wake = on;
  (void)power::set_wake_from_uart(on);
  // **I el preu de tancar aquesta porta, dit quan es tanca** (0.190.152): la consola és l'única manera de parlar amb una placa sense
  // teclat, i un xip que dorm a trossos sense escoltar-la es menja el que una persona escriu. No s'impedeix —qui té la placa en una
  // prestatgeria vol estalviar el corrent d'un cable que fa soroll— però no es fa en silenci (docs/30, docs/50).
  if (!on) {
    log_printf(pal::kLogWarn, "CFG",
               "the console will not wake a nap: what is typed while the chip naps can be lost");
  }
  return remember_uart_wake();
}

bool set_nap_on(bool on) {
  g_nap_on = on;
  (void)power::set_nap_on(on);
  return remember_nap_on();
}

// **El teclat s'encén i s'apaga aquí mateix**: `bt::start` obre el ràdio i en diu el preu al registre, i una placa que no en té només
// desa el commutador, que és el que fa que l'ajust no es refusi a una placa que no en sap (docs/30, docs/50).
bool set_bt_on(bool on) {
  const bool was = g_bt_on;
  g_bt_on = on;
  if (on && !was) {
    if (!bt::start(nullptr)) g_bt_on = false;
  } else if (!on && was) {
    bt::stop();
  }
  return remember_bt_on();
}

// **El llindar del registre** (0.190.89): the word of the row is the word of the ring, and the ring is told in the same line, or the
// page would show a level the log does not obey until the next boot (docs/30, docs/50).
// **El llum que s'apaga quan ningú mira** (0.190.118): cert vol dir que la llum del vidre baixa un minut després de l'últim dit, tecla o byte
// de consola, i que el primer toc la torna a encendre sense clicar res (docs/30, docs/50).
bool dim_when_idle() { return g_dim_when_idle; }

// **Un cable que es veu de lluny** (0.190.162): the switch of the Battery tab, i el que mana el respir del vidre mentre el carregador diu que
// hi ha corrent cap a la cel·la. El valor és el del run: moure el commutador canvia el que passa aquesta tarda i no el pròxim arrencada (docs/30).
bool breathe_while_charging() { return g_breathe_on; }

bool set_breathe_while_charging(bool on) {
  g_breathe_on = on;
  return remember_breathe();
}

// **I l'avís del cable, que és una foto i no una llum** (0.190.165): cert vol dir que la placa ensenya la imatge del cable quan el carregador
// diu que la cel·la ha començat a prendre corrent —una vegada per cable, no a cada torn— i el valor és el d'aquest run (docs/30, docs/50).
bool announce_charge() { return g_announce_charge; }

bool set_announce_charge(bool on) {
  g_announce_charge = on;
  return remember_announce_charge();
}

bool set_dim_when_idle(bool on) {
  g_dim_when_idle = on;
  return remember_dim_idle();
}

// **El botó de sota el vidre** (0.190.110): cert vol dir que el sistema fa servir la zona del tàctil com una tecla més, i fals que la deixa
// a la pàgina com qualsevol tros de vidre (docs/30, docs/50).
bool software_button() { return g_software_button; }

bool set_software_button(bool on) {
  g_software_button = on;
  return remember_software_button();
}

// **El règim del tàctil** (0.190.107): cert vol dir que el vidre baixa els escombrats quan no hi ha ningú i els apuja quan hi ha un dit;
// fals vol dir que es queda al màxim mentre la placa és desperta (docs/30, docs/50).
bool touch_adaptive() { return g_touch_adaptive; }

bool set_touch_adaptive(bool on) {
  g_touch_adaptive = on;
  return remember_touch_adaptive();
}

// **El llum del vidre** (0.190.106): the switch and the level, kept together and answered as what stands (docs/30, docs/50).
bool backlight() { return g_backlight; }

bool set_backlight(bool on) {
  g_backlight = on;
  return remember_backlight();
}

uint8_t backlight_level() { return g_backlight_level; }

bool set_backlight_level(uint8_t level) {
  if (level == 0) level = 1;  // una llum encesa a zero és una llum apagada, i per això hi ha el commutador
  g_backlight_level = level;
  return remember_backlight();
}

const char* log_level() { return g_log_level; }

bool set_log_level(const char* word) {
  pal::LogLevel at = pal::kLogDebug;
  if (!log_level_from_name(word, &at)) return false;
  snprintf(g_log_level, sizeof(g_log_level), "%s", log_level_name(at));
  // **La línia del canvi es veu sempre** (0.190.111): the level that was just chosen decides what is written from here on, o sigui que la línia
  // que diu el canvi s'escriu amb **el més permissiu dels dos nivells** —el vell i el nou— i tot seguit s'aplica el nou. Així `loglevel warn`
  // deixa la seva pròpia línia escrita i `loglevel debug` també, que és el que una persona espera d'una ordre que parla del registre
  // (docs/30, docs/50).
  const pal::LogLevel was = kernel::log_level();
  log_set_level((was < at) ? was : at);
  const bool kept = remember_log_level();
  log_set_level(at);
  return kept;
}

bool set_sleep_on(bool on) {
  g_sleep_on = on;
  power::set_idle_sleep_allowed(on);
  return remember_sleep_on();
}

uint16_t rotation() { return g_rotation; }

bool set_rotation(uint16_t degrees) {
  if (degrees != 0 && degrees != 90 && degrees != 180 && degrees != 270) return false;
  g_rotation = degrees;
  (void)remember_rotation();
  return g_rotation == degrees;
}

bool set_sort_by_usage(bool on) {
  g_sort_by_usage = on;
  return remember_sort();
}

// **The six words a switch takes** (0.189.13): the person writes the one their head reaches for and the store keeps the
// two the prompt prints, so `cfg sort yes` and `cfg sort on` end in the same line of the key-value store (docs/30).
bool parse_switch(const char* text, bool& on) {
  if (text == nullptr) return false;
  if (same(text, "on") || same(text, "true") || same(text, "yes")) {
    on = true;
    return true;
  }
  if (same(text, "off") || same(text, "false") || same(text, "no")) {
    on = false;
    return true;
  }
  return false;
}

uint32_t turn_default() { return luabind::turn_budget_default(); }

bool parse_turn(const char* text, uint32_t& instructions) {
  if (text == nullptr || text[0] == '\0') return false;
  // **Digits and nothing else**: a slice is a count of instructions, so a sign, a space or a unit is not one.
  uint32_t value = 0;
  for (const char* c = text; *c != '\0'; ++c) {
    if (*c < '0' || *c > '9') return false;
    value = value * 10u + static_cast<uint32_t>(*c - '0');
    if (value > luabind::turn_budget_max()) return false;
  }
  if (value < luabind::turn_budget_min() || value > luabind::turn_budget_max()) return false;
  instructions = value;
  return true;
}

bool set_turn_instructions(uint32_t instructions) {
  if (!luabind::set_turn_budget(instructions)) return false;
  return remember_turn();
}

// **`cfg mem [<kb>|auto]`, i el número s'ha de poder complir** (0.192.94): `auto` —o `0`— és la meitat del que la placa dona, un número són
// kilobytes i ha de cabre en el que la placa té, i per sota de seixanta-quatre kilobytes no hi cap ni l'intèrpret amb les seves llibreries, o
// sigui que no és un sostre sinó una manera de no poder arrencar cap app (docs/20, docs/75).
const uint32_t kInterpreterMemMinKb = 64;

bool parse_interpreter_mem(const char* text, uint32_t& kb, uint32_t board_kb) {
  kb = 0;
  if (text == nullptr || text[0] == '\0') return false;
  if (same(text, "auto") || same(text, "0")) return true;
  uint32_t value = 0;
  for (const char* c = text; *c != '\0'; ++c) {
    if (*c < '0' || *c > '9') return false;
    value = value * 10u + static_cast<uint32_t>(*c - '0');
    if (value > 65535u) return false;
  }
  if (value < kInterpreterMemMinKb) return false;
  if (board_kb > 0 && value > board_kb) return false;
  kb = value;
  return true;
}

bool set_interpreter_mem(uint32_t kb) {
  g_interpreter_mem_kb = kb;
  return remember_interpreter_mem();
}

const char* settings_app() { return g_settings; }

const char* settings_default() { return "settings"; }

bool set_settings_app(const char* id) {
  if (id == nullptr || id[0] == '\0' || same(id, "none")) {
    g_settings[0] = '\0';
    return remember_settings();
  }
  if (strlen(id) >= sizeof(g_settings)) return false;
  snprintf(g_settings, sizeof(g_settings), "%s", id);
  return remember_settings();
}

const char* lock_app() { return g_lock; }

const char* lock_default() { return "device_lock"; }

bool set_lock_app(const char* id) {
  if (id == nullptr || id[0] == '\0' || same(id, "none")) {
    g_lock[0] = '\0';
    return remember_lock();
  }
  if (strlen(id) >= sizeof(g_lock)) return false;
  snprintf(g_lock, sizeof(g_lock), "%s", id);
  return remember_lock();
}

bool lock_on() { return g_lock_on; }

bool gps_sync() { return g_gps_sync; }

bool set_gps_sync(bool on) {
  g_gps_sync = on;
  return remember_gps_sync();
}

bool set_lock_on(bool on) {
  g_lock_on = on;
  return remember_lock_on();
}

uint8_t lock_after_minutes() { return g_lock_after; }

bool set_lock_after_minutes(uint8_t minutes) {
  // **Cinc, deu o quinze i res més**: the list of the page is these three, and a number that is not one of them would be a
  // setting the page cannot show and a person cannot change back (docs/30, docs/60).
  if (minutes != 5 && minutes != 10 && minutes != 15) return false;
  g_lock_after = minutes;
  return remember_lock_after();
}

// **Quanta quietud abans del son profund** (0.190.9): the three times of the page and nothing else, which is the same rule the
// minutes of the lock follow —a setting a page cannot show is a setting a person cannot undo (docs/30, docs/60).
uint16_t idle_seconds() { return g_idle_seconds; }

uint16_t idle_default() { return kIdleDefault; }

int64_t idle_quiet_ms() { return static_cast<int64_t>(g_idle_seconds) * 1000; }

bool set_idle_seconds(uint16_t seconds) {
  if (seconds != kIdleSeconds[0] && seconds != kIdleSeconds[1] && seconds != kIdleSeconds[2]) return false;
  g_idle_seconds = seconds;
  return remember_idle();
}

uint32_t freq_step_of(uint8_t bit) {
  switch (bit) {
    case 0x01: return power::kIdleMhz;
    case 0x02: return power::kRestingMhz;
    case 0x04: return power::kBusyMhz;
    default: return 0;
  }
}

uint8_t freq_mask() { return power::allowed_steps(); }

bool freq_allowed(uint32_t mhz) {
  const uint8_t mask = freq_mask();
  for (uint8_t bit = 0x01; bit <= 0x04; bit = static_cast<uint8_t>(bit << 1)) {
    if ((mask & bit) != 0 && freq_step_of(bit) == mhz) return true;
  }
  return false;
}

bool set_freq_allowed(uint32_t mhz, bool on) {
  // **El pas del mig ja no és un ajust** (0.190.175, l'amo: *«el toggle del mig, fora»*): the chip's power management moves the processor
  // between a floor and a ceiling and it decides the frequencies in between, so 160 cannot be forbidden or allowed —a mask with it would
  // be a promise this system cannot keep—. The two that are a setting are 80 and 240, que són el terra i el sostre (docs/50, docs/60).
  if (mhz == power::kRestingMhz) return false;
  uint8_t bit = 0;
  for (uint8_t candidate = 0x01; candidate <= 0x04; candidate = static_cast<uint8_t>(candidate << 1)) {
    if (freq_step_of(candidate) == mhz) bit = candidate;
  }
  if (bit == 0) return false;
  const uint8_t wanted = on ? static_cast<uint8_t>(freq_mask() | bit) : static_cast<uint8_t>(freq_mask() & ~bit);
  // **L'última no se'n pot anar**: a mask with no bit is a processor with no speed, and a governor that cannot choose is a
  // board that does not come back from the answer (docs/50).
  if (wanted == 0) return false;
  power::set_allowed_steps(wanted);
  return remember_freq();
}

bool freq_text(char* out, size_t cap, uint8_t mask) {
  if (out == nullptr || cap < 4) return false;
  out[0] = '\0';
  size_t used = 0;
  for (uint8_t bit = 0x01; bit <= 0x04; bit = static_cast<uint8_t>(bit << 1)) {
    if ((mask & bit) == 0) continue;
    char one[8] = {0};
    snprintf(one, sizeof(one), "%s%u", (used == 0) ? "" : ",", static_cast<unsigned>(freq_step_of(bit)));
    const size_t more = strlen(one);
    if (used + more + 1 > cap) return false;
    memcpy(out + used, one, more + 1);
    used += more;
  }
  return used > 0;
}

bool parse_freq(const char* text, uint8_t& mask) {
  if (text == nullptr || text[0] == '\0') return false;
  if (same(text, "all") || same(text, "auto")) {
    mask = kFreqAll;
    return true;
  }
  uint8_t found = 0;
  const char* at = text;
  while (*at != '\0') {
    while (*at == ',' || *at == ' ' || *at == ';') ++at;
    if (*at == '\0') break;
    // **A step is 80, 160 or 240 and nothing else**: the three numbers of the chip, so a typo is refused here and not
    // discovered by a governor that has no step to go to (docs/50).
    uint32_t value = 0;
    int digits = 0;
    while (*at >= '0' && *at <= '9') {
      value = value * 10u + static_cast<uint32_t>(*at - '0');
      ++at;
      ++digits;
    }
    if (digits == 0) return false;
    bool known = false;
    for (uint8_t bit = 0x01; bit <= 0x04; bit = static_cast<uint8_t>(bit << 1)) {
      if (freq_step_of(bit) != value) continue;
      found = static_cast<uint8_t>(found | bit);
      known = true;
    }
    if (!known) return false;
  }
  if (found == 0) return false;
  mask = found;
  return true;
}

// **El parpalleig del cursor, que és una preferència i no una constant** (0.189.34): the number lives in `luabind`, which
// owns the clock of the caret, and this door keeps what the person chose and puts it there at the boot — the same shape the
// slice of a turn has, and for the same reason: an app cannot change the cursor of every other page (docs/30, docs/90).
int32_t cursor_delay() { return static_cast<int32_t>(luabind::caret_blink_ms()); }

int32_t cursor_delay_default() { return static_cast<int32_t>(luabind::caret_blink_default_ms()); }

bool parse_cursor_delay(const char* text, int32_t& delay) {
  if (text == nullptr || text[0] == '\0') return false;
  // **Digits and nothing else**: a blink is a count of milliseconds, so a sign, a space, a unit or a `ms` is not one.
  int32_t value = 0;
  for (const char* c = text; *c != '\0'; ++c) {
    if (*c < '0' || *c > '9') return false;
    value = value * 10 + static_cast<int32_t>(*c - '0');
    if (value > static_cast<int32_t>(luabind::caret_blink_max_ms())) return false;
  }
  if (value < static_cast<int32_t>(luabind::caret_blink_min_ms()) ||
      value > static_cast<int32_t>(luabind::caret_blink_max_ms())) {
    return false;
  }
  delay = value;
  return true;
}

bool set_cursor_delay(int32_t delay) {
  if (!luabind::set_caret_blink_ms(delay)) return false;
  return remember_cursor();
}

const char* preference() { return g_preference; }

const char* keyboard() { return g_keyboard; }

bool set_keyboard(const char* id) {
  if (id == nullptr || id[0] == '\0' || strlen(id) >= sizeof(g_keyboard)) return false;
  snprintf(g_keyboard, sizeof(g_keyboard), "%s", id);
  return remember_keyboard();
}

// **The offset of the hour, and the three calls that read and write it** (0.125.0). The number is kept in the store as
// text, and the boot puts the default of the build in front of the reading so that a second boot of the same object —the
// tests — does not inherit what the run before set.
int16_t tz_minutes() { return g_tz_minutes; }

bool tz_text(char* out, size_t cap, int32_t minutes) {
  if (out == nullptr || cap < kTzCap) return false;
  if (minutes > kTzMaxMinutes || minutes < -kTzMaxMinutes) return false;
  const int32_t magnitude = (minutes < 0) ? -minutes : minutes;
  snprintf(out, cap, "%c%02d:%02d", (minutes < 0) ? '-' : '+', static_cast<int>(magnitude / 60),
           static_cast<int>(magnitude % 60));
  return true;
}

bool parse_tz(const char* text, int32_t& minutes) {
  if (text == nullptr || text[0] == '\0') return false;
  const char* at = text;
  int32_t sign = 1;
  if (*at == '+' || *at == '-') {
    if (*at == '-') sign = -1;
    ++at;
  }
  if (*at == '\0') return false;

  // **A number on its own is a number of minutes, always**: `60` is one hour east, `-90` an hour and a half west and
  // `-0330` is three hundred and thirty minutes west —printed back as `-05:30`, so a person sees what was understood—.
  // The `±HH:MM` form is the one with a colon, and a bare number never means hours, because `0130` could be either
  // and a setting that can be read two ways is a setting that will be.
  bool digits = true;
  if (*at == '\0') return false;
  for (const char* c = at; *c != '\0'; ++c) {
    if (*c < '0' || *c > '9') {
      digits = false;
      break;
    }
  }
  if (digits) {
    if (strlen(at) > 5) return false;
    const int32_t value = static_cast<int32_t>(atoi(at));
    if (value > kTzMaxMinutes) return false;
    minutes = sign * value;
    return true;
  }

  // `H:MM` or `HH:MM`, and the colon is what says the first number is hours.
  int hours = 0;
  int mins = 0;
  if (sscanf(at, "%2d:%2d", &hours, &mins) != 2) return false;
  if (hours < 0 || hours > 14 || mins < 0 || mins > 59) return false;
  const int32_t total = hours * 60 + mins;
  if (total > kTzMaxMinutes) return false;
  minutes = sign * total;
  return true;
}

bool set_tz_minutes(int32_t minutes) {
  if (minutes > kTzMaxMinutes || minutes < -kTzMaxMinutes) return false;
  g_tz_minutes = static_cast<int16_t>(minutes);
  return remember_tz();
}

// **The first day of the week** (0.170.0): the same shape as the offset —the number in this file, the word in the store,
// and the calendar told every time so that the page and the call cannot disagree (docs/60).
bool dst() { return g_dst; }

bool set_dst(bool on) {
  g_dst = on;
  return remember_dst();
}

int16_t offset_minutes() {
  return static_cast<int16_t>(g_tz_minutes + (g_dst ? 60 : 0));
}

uint8_t week_start() { return g_week_start; }

bool set_week_start(uint8_t first) {
  // **The week of this system is Monday's or Sunday's and nothing else is written**: a week that begins on a Wednesday
  // is not a calendar a person asks for, and the store keeps the two words it has to be able to read back (docs/60).
  if (first != 1 && first != 7) return false;
  g_week_start = first;
  calendar::set_week_start(first);
  return remember_week();
}

const char* boot_splash() { return g_boot; }
// **The two words that are drawings of the logo**: the splash is the one that says which moments exist, so the
// question is asked there and not repeated here as a list that could drift (docs/30, docs/50).
bool is_a_boot_word(const char* word) {
  if (word == nullptr) return false;
  const splash::Kind kind = splash::kind_of(word);
  return kind == splash::kBoot;
}

bool set_boot_splash(const char* word) {
  // **A word that is not a drawing of the logo is refused where it is typed**: the boot has no room to ask anything
  // and a setting that lands nowhere would be a boot that lies about what it will paint (docs/30, docs/50).
  if (!is_a_boot_word(word) || strlen(word) >= sizeof(g_boot)) return false;
  snprintf(g_boot, sizeof(g_boot), "%s", word);
  return remember_boot();
}

const char* home_app() { return g_home; }

const char* home_default() { return LUNOK_HOME_APP; }

bool set_home_app(const char* id) {
  // **No name and the word `none` are the same setting**: a board that comes up at the prompt (docs/30).
  if (id == nullptr || id[0] == '\0' || same(id, "none")) {
    g_home[0] = '\0';
    return remember_home();
  }
  if (strlen(id) >= sizeof(g_home)) return false;
  snprintf(g_home, sizeof(g_home), "%s", id);
  return remember_home();
}

bool set_unit(const char* name) {
  if (name == nullptr) return false;
  if (!same(name, "auto") && !same(name, "sd") && !same(name, "littlefs")) return false;
  // Asking for a unit that is not there is refused instead of accepted and ignored: the
  // person has to know that the card is not mounted before trusting it with settings.
  if (same(name, "sd") && g_card == nullptr) return false;
  if (same(name, "littlefs") && g_flash == nullptr) return false;

  snprintf(g_preference, sizeof(g_preference), "%s", name);
  apply();
  remember();
  return true;
}

bool dir_for_app(char* out, size_t cap, const char* app) {
  if (out == nullptr || cap == 0) return false;
  if (g_unit[0] == '\0') return false;
  const char* prefix = same(g_unit, "sd") ? "/sd" : "/littlefs";
  const int written = (app != nullptr && app[0] != '\0')
                          ? snprintf(out, cap, "%s/cfg/%s", prefix, app)
                          : snprintf(out, cap, "%s/cfg", prefix);
  return written > 0 && static_cast<size_t>(written) < cap;
}

}  // namespace cfg
}  // namespace kernel
