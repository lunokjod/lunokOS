// Where an app keeps its settings.
//
// `/cfg` is not a disk: it is a door to one. Behind it there is `cfg/<id>/` on a unit, and
// which unit is the person's to choose — the card, because a card can be taken out and put
// in another board, or the flash, because a card can also be lost. The preference lives in
// the key-value store so it survives a reboot, and an app never learns which unit answered:
// it writes `/cfg/settings.json` and that is all (docs/70).
#ifndef LUNOK_KERNEL_CFG_H
#define LUNOK_KERNEL_CFG_H

#include <stddef.h>

#include "hal/hal.h"

namespace kernel {
namespace cfg {

// Reads the preference, decides the unit (the card first, unless the person said the
// flash), and points `/cfg` at it. A preference the board cannot honour falls back and
// says so in the log: a person who asked for the card has to know where the settings went.
void boot(hal::IPlatform& platform);

// The unit behind `/cfg`: "sd", "littlefs", or "" when neither is there.
const char* unit();

// What the person asked for: "auto", "sd" or "littlefs".
const char* preference();

// **The keyboard chosen in the settings** (0.93.0): the id of a child of `ui`, `"qwerty"` by default. The UI says
// which ids exist; this door only keeps the name alive across a reboot (docs/30, docs/70).
const char* keyboard();
bool set_keyboard(const char* id);

// **The word of the picture the system shows when it comes up** (0.115.0): one of the moments of the splash the boot
// accepts —`boot`, and only `boot` since the boot draws the drawings of the disk and picks one, 0.178.5—, chosen with
// `cfg boot <word>` and kept across a reboot like the unit and the keyboard. It is **a word and not a path**: the boot
// paints before anything else of this system exists, and a path in the settings would be a way to make the board open
// any file at its first breath (docs/30, docs/50, docs/70).
const char* boot_splash();
bool set_boot_splash(const char* word);
// Whether the word is one of the drawings of the logo this build carries, which is the question both the command
// and the boot ask and neither answers on its own (docs/30, docs/50).
bool is_a_boot_word(const char* word);

// **The app the board starts by itself when it comes up** (0.119.0): the "home" app —a launcher, a clock, the
// page a person wants to see when the board has just been switched on—. The empty name means **no home app**, and
// that is what every board did until now: the board comes up at the prompt and the person says what to run.
//
// The name **comes from the build**: `platformio.ini` gives each board its own with `-D LUNOK_HOME_APP="<id>"`,
// and `cfg home <id>` overrides it on that board's own store, the same way the keyboard and the picture of the boot
// are overridden. It is a name and not a path for the same reason the picture is a word: a path in the settings is a
// way to make a board open any file at its first breath (docs/30, docs/40, docs/70).
const char* home_app();
// What this build says, so `cfg home` can tell the person what the default of their board is.
const char* home_default();
// Keeps the name (`nullptr` or "none" clears it) and saves it. False when it could not be written.
bool set_home_app(const char* id);

// **La pàgina que es posa a sobre quan la placa es bloqueja** (0.189.150): the app the system runs when the screen has to be
// covered —the one of the Privacy tab, `device_lock` of series—, the switch that says whether it is used at the start and
// when the board comes back from a sleep, and **how long a person may leave it alone before it happens**. The three are
// settings of the system and live here for the reason `cfg home` does: a page that chooses them is a page, and a board that
// boots without one still has to know what to do with its glass (docs/30, docs/40, docs/60).
//
// `device_lock` is the default because it is the page the system already carries for this; an empty name is **no lock at
// all**, which is what a board with no such page wants and what a person gets when they choose `none`.
const char* lock_app();
const char* lock_default();
// Keeps the name (`nullptr` or "none" clears it) and saves it. False when it is too long to be an id of this system.
bool set_lock_app(const char* id);

// **The switch of the Privacy tab**: on, the lock app is run at the start of the board and when it comes back from a sleep,
// and the idle counter of `lock after` is armed. Off, none of it happens and the page under the finger stays where it is.
bool lock_on();
bool set_lock_on(bool on);

// **Els minuts sense tocar res** abans que la pàgina de bloqueig surti: five, ten or fifteen and nothing else, perquè és una
// tria d'una llista i no un número que una app pugui inventar-se. Ten by default (docs/30, docs/60).
uint8_t lock_after_minutes();
bool set_lock_after_minutes(uint8_t minutes);

// **Quant de quietud abans del son profund** (0.190.9, l'amo: *«afegir selector de temps per l'idle de settings->battery (per fer
// el DeepSleep)»*): the minutes of the lock were the first question of "how long without a hand" and this is the second —ten
// seconds, a minute or five, and nothing else—, and the policy of `power` reads it instead of its own constant (docs/30, docs/60).
uint16_t idle_seconds();
uint16_t idle_default();
int64_t idle_quiet_ms();
bool set_idle_seconds(uint16_t seconds);

// **Si la placa s'adorm sola** (0.190.5, l'amo: *«On es l'opció a Settings per desactivar-ho?»*): the idle policy of law 9 —the board
// powers itself off until its next alarm when nobody has touched it for ten seconds— is a setting and not a law of physics, because
// there are afternoons when a person wants the board awake (a demo, a measuring session, an app that logs). It is on by default, which
// is the board's own rule; off, `power::idle_sleep` does nothing at all and the board stays up until somebody says otherwise.
// **The state is the policy's** (`power::set_idle_sleep_allowed`) and this door is only what remembers it, so the switch of the page
// and the prompt of a person cannot disagree about what the board is doing (docs/30, docs/60).
bool sleep_on();
bool set_sleep_on(bool on);

// **Si el vidre és una porta per tornar d'un son profund** (0.190.16, l'amo: *«del DeepSleep s'ha de poder tornar sempre … també els
// botons físics i opcionalment el panell tàctil (per defecte false) així no cal mantenir despert el panell»*). Off by default: hearing a
// finger means keeping the panel's rail and its touch controller awake for the whole sleep, which is the current a deep sleep exists to
// stop. The key and the alarm are not this switch —a deep sleep always arms them—, i el que això encén és **una tercera porta** (docs/50,
// docs/60).
// **La becaina en lloc d'esperar** (0.190.63): the switch of the Battery tab, on by default, kept here and pushed to the policy so that
// moving it changes the board's next wait and not the next boot (docs/30, docs/60).
bool nap_on();
bool set_nap_on(bool on);
// **El teclat de Bluetooth** (0.190.180, l'amo: *«afegeix bluetooth al projecte»*): the switch that offers this board as a keyboard to
// another device, kept in the shop like every other one and read at the boot. **Off by default**, i no pas pel que costa —29 544 B de
// heap, mesurats a la placa el 21/11 (docs/75)— sinó perquè mentre és encès el xip **no fa becaines** (`wants_the_chip_awake` tanca el
// pany del torn): una placa que es queda a la prestatgeria no ha de pagar el preu d'un teclat que ningú fa servir (docs/30, docs/50,
// docs/60).
bool bt_on();
bool set_bt_on(bool on);
// **La consola com a porta de la becaina** (0.190.147, l'amo: *«Posa un toggle a Settings->Battery que doni la opció a "Wake from
// UART"»*): encès de sèrie —el prompt és l'única manera de parlar amb una placa sense teclat— i apagat per a qui vulgui que les esperes
// només les desfaci un dit (docs/30, docs/50, docs/60).
// **La darrera dormida, desada a la botiga** (0.190.152, l'amo: *«el botó de power fa un reset i tot s'envà a la merda»*): the board knows how
// it armed the last deep sleep and the RTC RAM does not survive the power button, so the line is written here and read back when a person
// asks —`power last`— with the cable already back on (docs/50, docs/70).
bool keep_the_last_sleep(const char* text);
const char* the_last_sleep();
bool uart_wake();
bool set_uart_wake(bool on);

// **El nivell del registre** (0.190.89, l'amo: *«Afegeix "Log level" a Settings->System Amb els nivells de log, per defecte Debug»*):
// `debug`, `info`, `warn` o `error`, desat com el commutador del costat i aplicat en el mateix moment. El de sèrie és `debug`, que és el
// que el sistema ha fet sempre; una paraula que no és cap de les quatre es refusa i el registre es queda com era (docs/30, docs/50).
// **El llum que s'apaga quan ningú mira** (0.190.118, l'amo: *«unaltre Toggle "Dim when idle" … en 1 minut que no es toqui l apantalla el
// backlight s'apaga i es torna a encendre quan toquen el panell»*): cert vol dir que la llum del vidre baixa un minut després de l'últim dit,
// tecla o byte de consola, i que el primer toc la torna a encendre sense clicar res (docs/30, docs/50).
bool dim_when_idle();
bool set_dim_when_idle(bool on);

// **El vidre que respira mentre el cable carrega** (0.190.162, l'amo: *«Si el T5S3 està carregant batería vull el efecte "breathe" al backight
// fins que deixa de carregar»*): cert vol dir que la llum del vidre fa el respir de la casa mentre el carregador diu que hi ha corrent cap a la
// cel·la, i que s'atura quan el cable marxa o la cel·la està plena. És un ajust d'aquesta placa i no de la pàgina: qui no té retroil·luminació
// el desa i no en fa res (docs/30, docs/50).
bool breathe_while_charging();
bool set_breathe_while_charging(bool on);

// **L'avís de la càrrega** (0.190.165, l'amo: *«un toggle a Settings->System "Announce charge" ... que mostrarà l' splash de "charging" quan es
// detecti que s'endolla»*): cert vol dir que el sistema ensenya la imatge del cable quan el carregador comença a donar corrent a la cel·la, i
// **una vegada per cable**: fins que no es desendolla no torna a sortir (docs/30, docs/50).
bool announce_charge();
bool set_announce_charge(bool on);

// **El botó de sota el vidre** (0.190.110, l'amo: *«Afegeix a Settings->Input un Toggle per habilitar-lo o deshabilitar-lo "Use software
// button"»*): cert vol dir que el sistema fa servir la zona del tàctil de sota la pantalla com una tecla més —el segon botó de la placa, que
// en realitat és un tros de vidre— i fals que la deixa a la pàgina (docs/30, docs/50).
bool software_button();
bool set_software_button(bool on);

// **El règim del tàctil** (0.190.107, l'amo: *«afegeix a Settings->Input un toggle que posi "Adaptative touch scan"»*): cert vol dir que el
// vidre baixa els escombats quan no hi ha ningú i els apuja quan hi ha un dit; fals, que es queda al màxim mentre la placa és desperta. El
// son, en canvi, mana el seu règim a cada mode (docs/30, docs/50).
bool touch_adaptive();
bool set_touch_adaptive(bool on);

// **La llum del vidre** (0.190.106, l'amo: *«Afegeix a Settings->System un toggle amb "Use backlight" i un selector desplegable amb tots els
// valors possibles del backlight»*): whether the board lights its glass and with how much light. They are two keys because they are two
// decisions: turning it off does not forget the level somebody chose (docs/30, docs/50).
bool backlight();
bool set_backlight(bool on);
uint8_t backlight_level();
bool set_backlight_level(uint8_t level);

const char* log_level();
bool set_log_level(const char* word);


bool wake_on_touch();
bool set_wake_on_touch(bool on);

// Asks for a unit and persists it. `auto` prefers the card and falls back to the flash.
// False when the name is not one of the three, or when that unit is not mounted.
bool set_unit(const char* name);

// **I la unitat dels ajustos és també el terra de les apps** (0.190.8, l'amo: *«perqué es perden les dades d'historic de Battery
// i TimeClock al pujar un firmware nou? … i tinc SD»*): `/data/<id>/` era el flaix sempre i `-t uploadfs` reescriu la partició
// sencera, i ara segueix aquesta mateixa tria —`auto` és la targeta quan n'hi ha, que és el que un flaix no toca (docs/30,
// docs/70). No hi ha cap preferència nova: la pregunta és la mateixa i la Paper no té DRAM per a dues respostes.
//
// **I el que ja hi ha al flaix no es mou**: passar a la targeta estrena les apps buides (la decisió de l'amo del 18/11), o sigui
// que la fila nova no hereta res i una còpia és cosa d'una persona (docs/70).
// **The clock of the chip is UTC and a person reads their own hour** (0.125.0): the system keeps the one number that
// joins them —how many minutes the local hour is ahead of UTC— and `time.local()` applies it, so an app that says
// "07:00 to 19:00" is saying it in the hour the person is living in and not in the one the quartz counts (docs/30,
// docs/60). It is a setting of the system and not of an app: `cfg tz +01:00` writes it, `time.tz()` does the same
// from Lua, and the two read the same value.
//
// **It is an offset and not a zone**: this system has no table of the world's zones and does not pretend to, so a
// board that lives where the hour changes twice a year has to be told the new offset —one command, twice a year—.
int16_t tz_minutes();
// Minutes east of UTC, from -14:00 to +14:00. False when the number is not a zone this clock could have.
bool set_tz_minutes(int32_t minutes);
// "+01:00", "-03:30" or "+00:00": the way the setting is written in the store and on the glass (seven characters and
// the null, so `cap` has to be eight).
bool tz_text(char* out, size_t cap, int32_t minutes);
// **Reads the two ways a person writes an offset**: `+01:00`, `-0330`, `+1`, `01:00` or a number of minutes on its
// own (`60`, `-90`). True when `text` is one of them, and then `minutes` holds it.
bool parse_tz(const char* text, int32_t& minutes);

// **The calendar a person wants** (0.170.0, docs/60, docs/90): whether February has its twenty-ninth day and which
// weekday a week begins on. Both are settings of the system and not of an app —`ui.month`, `time.utc`/`date` and any
// page that draws a month ask the same rule— and both survive a reboot like the offset of the hour does. The rule
// itself is `kernel/calendar.h`; this door keeps the choice, puts it in the calendar at boot, and answers the same
// value the calendar is using.
bool leap_years();
bool set_leap_years(bool on);

// **L'hora d'estiu** (0.182.0): the box of the Time zone page that the owner ticked and that was kept nowhere. It is a setting of
// the system and not a shift of a page, because the hour a person reads is the system's: the offset of the map plus, when this is
// on, **sixty minutes**. The RTC cannot do it —the BM8563 of the Paper counts seconds and has a century bit, not a rule of summer—
// and the chip's library would want a name of zone per country, which is a table this system does not carry (docs/30, docs/60).
bool dst();
bool set_dst(bool on);
// **L'offset que compta**: the one the map says plus the hour of summer. `tz_minutes()` is still the map's, which is what an app
// shows and what `time.tz()` answers; this is the one that makes an hour of a person (docs/30, docs/60).
int16_t offset_minutes();
// **1 is Monday and 7 is Sunday**: Monday is the default of this system since its first calendar, and Sunday is the
// week a person may ask for with `cfg week sunday` or `time.week(7)` (docs/60).
uint8_t week_start();
bool set_week_start(uint8_t first);

// **The slice of a turn of Lua, which is the latency of the whole board** (0.174.7, docs/20, docs/75): how many
// instructions an app runs before the kernel takes the turn back for the glass, the keys and the radio. It is a setting
// and not a constant because it is a trade between the app's throughput and how long a finger waits, and that trade is
// this board's to make: `cfg turn 20000` writes it, `sys.turn()` reads it from Lua, and the boot puts it in the module
// that counts it. The number in force is the block ceiling of what was asked (the hook counts in blocks of 2000), which
// is why the door answers a number and not the text it was given.
uint32_t turn_instructions();
// What this build ships with, so `cfg turn` can say which of the two a person is looking at.
uint32_t turn_default();
// A slice is a count of digits: true when `text` is one of the numbers a slice may be. `instructions` holds it then.
bool parse_turn(const char* text, uint32_t& instructions);
// Applies the number now —the next turn of the app uses it— and saves it. False when the number is not a slice or when
// there is nowhere to keep it.
bool set_turn_instructions(uint32_t instructions);

// **L'app dels ajustos, la que obre el botó del panell** (0.189.109, l'amo: *«Enllaça el botó de Settings del panell desplegable
// de sistema amb l'app <id: settings> Si no està l'app no passsa res»*). The gear of the card was the console in the glass because
// there was no page of settings on the disk; now there is, and **which app it is is a choice of the person** like the app of the
// start: `cfg settings <id>` keeps the name, `none` says the gear does nothing, and the panel asks this door before it asks the
// shell to run anything (docs/30, docs/40).
const char* settings_app();
// What this build says when nobody has chosen, so the command can tell a person what the default of their board is.
const char* settings_default();
// Keeps the name (`nullptr`, "" or "none" clears it) and saves it. False when it could not be written.
bool set_settings_app(const char* id);

// **Les velocitats que la CPU pot fer servir** (0.189.107, l'amo: *«[X] Frequency scaling allowed: [X] 80Mhz [X] 160Mhz
// [X] 240Mhz»*): the board has three steps and the person chooses which of them the policy may move between. It is a
// setting of the system and not of an app, because the governor is the system's and a step it is not allowed to take is a
// step it must not take even for a paint: `cfg freq 240 off` keeps the ceiling at 160 and the board runs slower and lasts
// longer (docs/50, docs/60).
//
// **The mask is the setting and the words are what a person reads**: `80`, `160` and `240` are the three bits, and the
// store keeps the list as text —`80,160,240`— so `nvs` shows the truth without a table (docs/70).
const uint8_t kFreqAll = 0x07;  // the three steps: 80 is bit 0, 160 bit 1 and 240 bit 2
uint32_t freq_step_of(uint8_t bit);   // the step a bit of the mask names
uint8_t freq_mask();                  // what the governor may use, never zero
bool freq_allowed(uint32_t mhz);
// Turns a step on or off. **The last one cannot go**: a board with no allowed step is a board with no speed, so the
// refusal is a number and not a promise, and the caller gets the mask it still has.
bool set_freq_allowed(uint32_t mhz, bool on);
// The list as a person reads it: "80,160,240". False when the mask has no bit at all.
bool freq_text(char* out, size_t cap, uint8_t mask);
// `all`, `80,160` or `80 160 240` — the three steps a person may write, in any order and with a comma or a space between
// them. False when a step is not one of the three or when the list is empty.
bool parse_freq(const char* text, uint8_t& mask);

// The real path of one app's settings, for a person to read: "/sd/cfg/<id>".
bool dir_for_app(char* out, size_t cap, const char* app);

// **Si el llançador ordena les apps pels usos** (0.189.13): the switch is off by default, because the door of the apps has
// an order of its own —the flash's folder and then the card's— and a launcher that shuffles a person's page without being
// asked is a page nobody can find anything on. With it on, `home_eink` shows the apps from the most used to the least, and
// the count is the launcher's own business in a session of its own (docs/30, docs/40).
//
// It is a **preference of the system and not a shape of a page**, so it lives here and not in the session `state` of the
// app: what it says is how a person wants the launcher to behave, and the person asked for it with `cfg sort on`.
bool sort_by_usage();
// Saved as the two words a person reads in the store, `on` and `off`, like the calendar and the hour of summer.
bool set_sort_by_usage(bool on);
// **The six words a switch takes** (0.189.13): `on`, `true` and `yes` are one and the same, and `off`, `false` and `no`
// are the other. A person writes the word their head reaches for, and the store still keeps only `on` or `off`.
bool parse_switch(const char* text, bool& on);

// **El parpalleig del cursor, que és de tots i no d'una pàgina** (0.189.34, l'amo: *«Crea una opcio nova de cfg anomenada
// "cursor delay" Allarga el parpalleig del cursor a 850milis. Aquest valor es comú a tots els cursors del sistema»*): the
// milliseconds a block of ink lasts before it turns, which is the box of one line, the box of several, the terminal of the
// shell and the note of the time clock at once. It is kept as the decimal text a person reads with `nvs` —`850`— and the
// boot puts it in `luabind`, which is the module that owns the clock of the caret: this door keeps the choice, like the
// slice of a turn (docs/30, docs/75).
int32_t cursor_delay();
int32_t cursor_delay_default();
// `delay` holds the number of milliseconds when the text is one a blink may be, and false when it is not.
bool parse_cursor_delay(const char* text, int32_t& delay);
// Applies it now —the next blink already uses it— and saves it. False when the number is not a blink or when there is
// nowhere to keep it.
bool set_cursor_delay(int32_t delay);

}  // namespace cfg
}  // namespace kernel

#endif  // LUNOK_KERNEL_CFG_H
