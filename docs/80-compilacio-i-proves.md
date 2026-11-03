# 80 · Compilació i proves

## Sis entorns

| Entorn | Plataforma | Per a què |
|---|---|---|
| `atom` | espressif32 + Arduino | El firmware de la placa de proves |
| `paper` | espressif32 + Arduino, placa pròpia | La M5 Paper (16 MB de flash, PSRAM i un panell e-ink): definició a `boards/m5stack-paper.json`, taula a `partitions_16mb.csv` (docs/50) |
| `atom_matrix` | espressif32 + Arduino | La placa del panell de vint-i-cinc llums: mateix xip, mateixa taula i mateix `board =`, i el que canvia és el `-I` de la identitat (docs/50) |
| `esp32` | espressif32 + Arduino | **La placa del mínim**: un ESP32 de DevKit (`esp32dev`, 4 MB, sense PSRAM), la més corrent que hi ha. No és cap fitxa de cap aparell: és la prova que el sistema cap en el xip pelat (docs/50) |
| `emulator` | native | El mateix codi compilat per al PC |
| `test` | native | Les proves natives (Unity) |

```ini
[platformio]
default_envs = emulator
boards_dir = boards        ; el Paper no és una placa que la plataforma conegui

[env:atom]
platform = espressif32
board = m5stack-atom
framework = arduino
board_build.partitions = partitions.csv
board_build.filesystem = littlefs
monitor_speed = 115200
monitor_rts = 0
monitor_dtr = 0
build_flags = -std=gnu++11 -Ilib/board_atom/include
             -DARDUINO_LOOP_STACK_SIZE=16384
lib_ignore = hal_emulator, pal_host, board_matrix, board_paper, board_esp32,
             WiFi, WiFiClientSecure, HTTPClient, ESPmDNS, WebServer,
             DNSServer, WiFiProv

[env:atom_matrix]
extends = env:atom
build_flags = -std=gnu++11 -Ilib/board_matrix/include
             -DARDUINO_LOOP_STACK_SIZE=16384
lib_ignore = hal_emulator, pal_host, board_atom, board_paper, board_esp32, …
; i les de xarxa, igual

[env:paper]
platform = espressif32
board = m5stack-paper
framework = arduino
board_build.partitions = partitions_16mb.csv
extra_scripts = pre:tools/check_fs_names.py
build_flags = -std=gnu++11 -Ilib/board_paper/include
             -DARDUINO_LOOP_STACK_SIZE=16384
lib_ignore = hal_emulator, pal_host, hal_atom, board_atom, board_matrix, …
; hal_esp32 no s'ignora: és el que aquesta placa comparteix amb l'ATOM (docs/50)

[env:esp32]
platform = espressif32
board = esp32dev
framework = arduino
board_build.partitions = partitions.csv
extra_scripts = pre:tools/check_fs_names.py
build_flags = -std=gnu++11 -Ilib/board_esp32/include
             -DARDUINO_LOOP_STACK_SIZE=16384
lib_ignore = hal_emulator, pal_host, board_atom, board_matrix, board_paper, …

[env:emulator]
platform = native
build_unflags = -std=gnu++11 -std=gnu++14 -std=gnu++17
build_flags = -std=c++11 -Ilib/hal_emulator/include
; SDL2 quan hi és, X11 de reserva, o cap finestra: ho tria aquest script (docs/85).
extra_scripts = pre:tools/select_gui.py
lib_ignore = hal_atom, hal_paper, hal_esp32, pal_esp32,
             board_atom, board_matrix, board_paper, board_esp32

[env:test]
platform = native
test_framework = unity
test_build_src = yes
build_unflags = -std=gnu++11 -std=gnu++14 -std=gnu++17
build_flags = -std=c++11 -Ilib/hal_emulator/include
lib_ignore = hal_atom, hal_paper, hal_esp32, pal_esp32,
             board_atom, board_matrix, board_paper, board_esp32
```

El text de debò és a `platformio.ini`, amb un comentari per línia; aquí n'hi ha la forma. Les
llibreries de xarxa d'Arduino són fora del build a posta (docs/50), i **cada entorn ignora les
identitats de les altres tres plaques** perquè el build en vegi exactament una; el banc no en té cap
de `lib/board_*` i porta la seva a `lib/hal_emulator/include/board/`.
**El banc no enllaça res per a la xarxa**: la pàgina que demana `http` la baixa el `curl` d'aquesta
màquina, en un procés fill, i un `curl` que no hi sigui no trenca el build — la petició respon
`E_HTTP` i ho diu a la consola (docs/85).
`extra_scripts = pre:tools/check_fs_names.py` és a tots els entorns de placa (`atom` —i el seu fill
`atom_matrix`—, `paper` i `esp32`): l'eina de la imatge de LittleFS deixa caure en silenci qualsevol nom
de `data/` més llarg de 32 caràcters i tot i així diu SUCCESS, i aquest script ho converteix en un build
fallit (docs/70).

Sis coses que aquesta configuració fa complir:

1. **C++11 a les dues bandes.** El nucli d'Arduino per a ESP32 compila amb `-std=gnu++11`; si el codi
   del PC fa servir C++17, el projecte es trenca en silenci quan arribi a la placa. Es compila igual
   a tot arreu i s'acaba el problema.
2. **Els avisos són errors, però només els nostres.** A cada `library.json` hi ha
   `"build": {"flags": ["-Wall", "-Wextra", "-Werror"]}`. No es posa a `build_flags` perquè també
   s'aplicaria al **framework d'Arduino**, que té avisos propis (un de conegut a `esp32-hal-uart.c`) i
   no és feina d'aquest projecte arreglar-los. Així el build surt net i el nostre codi no en passa
   ni un.
3. **`lib_ignore` i `src/board_link.cpp`.** Cada entorn no veu mai el codi de les altres plaques: ho
   diuen el `lib_ignore` i el camí d'inclusió (`-Ilib/board_<id>/include`), que és el que fa que
   `#include "board/identity.h"` i `#include "board/impl.h"` resolguin a la placa d'aquest entorn i no
   a les altres. (El banc i les proves no tenen cap `lib/board_*`: la seva identitat és dins
   `lib/hal_emulator/include/board/`.) Però això sol no n'hi prou: PlatformIO descobreix les llibreries
   de `lib/` seguint els
   `#include`, i com que `board/impl.h` no es diu com cap llibreria (`hal_atom`, `pal_esp32`…), sense
   ningú que les anomeni (`undefined reference to hal::platform()`) no es compilen mai.
   `src/board_link.cpp` inclou `board/impl.h`, que és l'únic fitxer que arrossega `hal_atom.h` +
   `pal_esp32.h` (o `hal_emulator.h` + `pal_host.h`), i per això les proves també compilen `src/`.
   La identitat de cada placa és una llibreria de capçaleres (`lib/board_<id>/`) i `lib_ignore` deixa
   només la de l'entorn: `hal_atom` serveix dues plaques, i dues capçaleres amb aquest nom al camí
   farien que l'ordre decidís, en silenci (docs/50). Fora de `lib/` no hi poden ser: el cercador de
   dependències només segueix capçaleres de llibreries (mesurat el 31/10, docs/50).
4. **Dos punts d'entrada.** `src/main.cpp` compila només sota `ARDUINO` (a la resta és un stub, perquè
   PlatformIO es nega a compilar un entorn el `src/` del qual queda buit) i el `main()` de l'emulador
   viu a `lib/hal_emulator/`, tancat amb `#if !defined(UNIT_TEST) && !defined(PIO_UNIT_TESTING)` perquè
   quan corre `pio test` mana el `main` de les proves.
5. **Qui depèn de qui ho diu `library.json`.** El cercador de dependències de PlatformIO segueix els
   `#include`, i això falla de dues maneres que ja han passat: `lib/luabind` no trobava `lauxlib.h`
   (el cercador no sap que `lua` és una llibreria fins que algú ho diu) i, quan el va trobar, els
   objectes de `lua` s'enllaçaven **abans** de qui els crida i l'enllaçador deia que no hi eren. La
   solució són dues línies de `lib/luabind/library.json`:

   ```json
   "dependencies": { "lua": "*", "pal": "*" },
   "build": { "flags": ["-Wall", "-Wextra", "-Werror"], "libArchive": false }
   ```

   Amb `libArchive` a `false`, `lua` i `luabind` entren a l'enllaç com a **objectes** i no com a
   arxius `.a`, i l'ordre deixa de tenir importància. `lib/lua` ho porta igual. No cal cap
   `-Ilib/lua/include` a `platformio.ini`: la dependència declarada ja el posa.
6. **Cap `#ifdef` de placa.** Quan dues bandes han de fer coses diferents, es fa amb una capçalera
   **amb el mateix nom i contingut diferent** a cada placa (`board/identity.h`, `board/impl.h`) i el
   camí d'inclusió de l'entorn. El preprocessador no decideix mai de quina placa és el codi, i per
   tant no hi ha cap branca morta que ningú no compila ni prova.

`src/main.cpp` només existeix per a Arduino (el `setup()`/`loop()` el crida el framework). A
l'emulador, el `main()` és a `lib/hal_emulator/`.
## Com es compila

```
pio run -e emulator        # binari per al PC
pio run -e atom            # firmware; les proves de flashejar les fa l'amo
pio run -e atom_matrix     # la placa del panell: 25 llums al mateix GPIO27
pio run -e esp32           # el mínim: un ESP32 de DevKit, sense res a sobre
pio test -e test           # proves natives
```

Cap sessió automàtica no puja res a la placa. Compilar sí; flashejar, no.

## Què s'ha de posar a la placa (31/10)

A la flash del Paper hi van **dues coses diferents**, i una ordre no fa l'altra:

| Què | Ordre | On va |
|---|---|---|
| El firmware | `pio run -e paper -t upload` | la partició `app0`, 4 MB (`partitions_16mb.csv`) |
| El disc: les apps i els `res/` | `pio run -e paper -t uploadfs` | la partició `littlefs`, 11,9 MB, amb el contingut de `build/disk/paper` (el `data/` del sistema més les apps d'`apps/`) |

- **Les apps viuen a `apps/`**, a l'arrel del projecte, i el disc de cada entorn el munta
  `tools/make_disk.py` a **`build/disk/<entorn>`** amb **les apps que el seu manifest nomena**: la placa per
  l'id o per la família `esp32`, que és la mateixa regla amb què el nucli refusa una app al `run` (docs/40).
  Cada app del disc és **un enllaç** a la seva carpeta d'`apps/` (l'entorn `test` les copia, perquè les seves
  proves escriuen al disc). El banc i les proves no són cap placa i les porten totes. I `custom_apps =
  id1,id2` mana sobre el filtre, per a un build que vulgui una llista exacta.
- **Les de 4 MB no hi caben, i el build ho diu en comptes de perdre fitxers en silenci.** Mesurat el 3/11
  amb blocs de 4 KB i la partició de 917 504 B: de les 48 apps de l'ATOM (124 fitxers) en queden **15 fora**;
  de les 50 del Matrix (128), **18**; de les 46 de l'ESP32 pelat (120), **13**. La Paper, amb 47 apps i una
  partició d'11,9 MB, les hi porta totes. `make_disk` refusa l'ordre `-t buildfs`/`-t uploadfs` quan el disc
  estimat no hi cap —tres blocs per fitxer, que és el costat prudent dels 2,25 a 2,5 mesurats— i diu les dues
  sortides: `custom_apps = id1,id2` o una partició `littlefs` més gran (docs/70).
- **Sense el segon, la placa arrenca i no té cap app**: `apps` surt buit, `run hello` diu que no la
  troba, i això no és cap error del sistema. Es fa un cop, i cada cop que canviï `apps/` o `data/`.
- **No cal posar res a la targeta SD**: el slot és una targeta i prou. Si n'hi ha una, `/sd` apareix i
  `cfg auto` hi desa els ajustos; cap fitxer és obligatori.
- **`emulator/littlefs` no es puja mai**: és la còpia privada d'un banc que demana `--flash
  emulator/littlefs`; el disc del banc, per defecte, és `build/disk/emulator` (docs/85).
- El nom d'un fitxer de `data/` o d'`apps/` no pot passar de 32 caràcters: `tools/check_fs_names.py` ho converteix
  en un build fallit en lloc de deixar que la imatge el perdi en silenci (docs/70).
- Per saber que tot hi és, després de flashejar: `version` (la versió i la placa), `caps` (**16 de
  19** al Paper amb targeta i bateria, o **17** si el terminal contesta ANSI), `mem` (els tres números
  de la memòria extra), `mounts` (fins a
  nou files, amb `/rtc` i `/fram`) i `apps` (les apps del disc). I `run heap`, que és la meitat que
  falta de la mesura E-4 de `docs/95`.

## La versió: X.Y.Z, i puja a cada canvi (31/10)

La versió és **`kernel::kVersion`** (`lib/kernel/include/kernel/version.h`) i surt al banner de cada
arrencada, a `version` i a `app.version`. La regla és del propietari del projecte, i no és opcional:

| Dígit | Quan puja | Exemples |
|---|---|---|
| **Z** (pedaç) | una correcció, un número que s'ha mesurat, un document que deia una cosa falsa | un marge que era un minut curt, el drift que comptava l'arrencada, `docs/70` que deia que `/fram` no hi era |
| **Y** (menor) | una cosa **nova** que una persona o una app pot fer servir | una crida de Lua, un comandament del prompt (`poweroff`), una peça de maquinari conduïda (la FRAM, la PSRAM), `run <id> [path]` |
| **X** (major) | una **reescriptura de debò**, d'aquelles que fan que una app que anava s'hagi de tornar a mirar | canviar el model d'apps o el contracte de Lua d'una manera que trenca |

- **Cada canvi puja un dels tres, i el número puja al mateix commit que el canvi.** Un torn que toca
  codi i no toca `kVersion` és un torn a mitges.
- Si un torn fa diverses coses, mana la **més gran**: dues crides noves i un pedaç són un **Y**.
- El número que es posa a la capçalera del fitxer no s'esborra: el darrer de la llista és el que val,
  i cada línia diu el dia i què va ser (les de pedaç no cal que hi siguin totes; les de menor i major,
  sí).
- **`app.version` no és aquesta**: és la de cada app i la puja qui l'escriu (docs/40).

## Què és una prova aquí

Cinc nivells, i cada cosa va al seu:

| Nivell | On | Què comprova |
|---|---|---|
| **Unitat** | `test/test_json/` | Resolució de camins, muntatges, llistat de `/`, analitzador JSON |
| **Unitat** | `test/test_alarms/` | La cua d'alarmes: ordre, repeticions, perdudes i la còpia a la NVS |
| **Unitat** | `test/test_qr/` | El símbol llegit de tornada com el llegiria un escàner: BCH, desmàscara i Reed-Solomon |
| **Unitat** | `test/test_led/` | El motor dels efectes: els quatre efectes com a funció del temps, les dues capes, la interrupció pel sistema (desar, pintar, restaurar i avisar `on_light`) i el sostre de brillantor de la placa |
| **Unitat** | `test/test_orientation/` | El gir: les quatre cantonades del Paper, el quart de volta i que encadenar un gir en el mateix objecte no llegeixi la seva pròpia sortida |
| **Unitat** | `test/test_line_editor/` | El cursor d'una línia: una lletra al mig, el retrocés que se l'emporta, inici/final, i **l'historial**: vuit ordres, la que s'estava escrivint que torna en passar de la més nova, i ni les línies buides ni la repetició immediata no hi entren |
| **Unitat** | `test/test_ui/` | La capa de controls contra el vidre del banc: la vista i el canvas, els danys amb la seva qualitat, el rectangle que comença en un píxel senar, la llista de vuit que s'omple, la composició **fills primer**, els controls **sense vista** (pinten al primer canvas que els conté, tallats al seu marc), el redibuix tallat a la caixa que el control deu, el forat que deixa un fill que es mou, la neteja abans d'alliberar amb **un sol rectangle per subarbre**, el **marc i la generació** que fan que un objecte mort respongui `E_INVAL`, el **model de la tinta** (el rastre d'un `du`, la neteja d'un `gc16`, el temps de cada qualitat, el parpelleig d'`a2` en tres moments i el preu del bus que quadra amb els 1 985 ms de la placa l'1/11) i **la finestra** (el color calent dels setze nivells i el pas a píxel amb escala), i **quina placa té vidre**: el model diu que la Paper en té i que els ATOM no, i una opció no dona pantalla a una placa que no en té, i **el camí de tornada del píxel de la finestra al píxel de la pàgina** (el centre del bloc d'escala i la vora per a un punter fora), i **la tira de les tecles** (la columna és pujar/centre/baixar, la seva geometria —96 d'ample, botons de 80 × 54 centrats— i que un clic al buit no prem cap tecla), i **els controls**: el retall d'UILabel, i el marc d'UIButton amb la vora premuda, el fill tallat al botó i el fill que es torna a dibuixar quan el marc canvia, i **UICheckbox** (la caixa amb el tick d'un color propi, el forat de premut i l'anell del focus fora de la caixa) |
| **Unitat** | `test/test_touch/` | El panell de la Paper al banc: **què costa una lectura** (800 µs sense dit, 1 830 amb un i 2 550 amb dos, i el probe de 186 bytes de l'arrencada), **un marc per escombrat** —la segona lectura de la mateixa finestra diu que no hi ha ningú—, la configuració d'aquesta unitat (dos dits, mides 28 i 30, el punt fora del marc refusat), el dit que se'n va, el **xip més lent que el torn** (40 ms contra 20: la meitat de les lectures diuen ningú), i un dit que acaba en un gest `long` amb les coordenades de la vista (100,200 del vidre són 100,152 de la vista) |
| **Unitat** | `test/test_battery/` | La bateria: la **corba en els dos sentits** (4 200 mV plena, 3 300 buida, cada fila torna com va anar-hi, i el voltatge només puja amb la càrrega), la **bateria de la màquina** llegida d'uns fitxers del nucli (63 %, carregant o no, i un `150` que no és cap bateria), la **cel·la prestada** (el 100 % són 4 200 mV, el 63 % són 3 847, i una màquina sense bateria deixa la cel·la al **càrrec nominal, 3 700 mV**, perquè un Paper sempre en té), i que **preguntar el gauge pagui l'ADC** (400 µs) i no el pagui amb els temps apagats |
| **Unitat** | `test/test_font/` | El tipus de lletra portable: un fitxer de text que es converteix en una cara (la mida, el forat, els codis en ordre i cada `#` on toca), **l'amplada d'una línia** amb l'escala i sense forat al final, els glifs **com a tirades de píxels** tallades a la caixa del label, i cada manera que un fitxer deixa de ser una cara amb el seu motiu. I **el BDF**: les files hexadecimals cap al mateix bloc de setze per setze (amb dues bytes per fila quan el glif passa de vuit columnes), el `COMMENT` i les propietats que no diuen res, `ENCODING -1 <n>`, les seves maneres de dir que no, i que el **sniffer** (`font::load_any`) triï el lector pel que diu el fitxer i salti els comentaris de davant. I **el PCF**, amb el fitxer de debò que `bdftopcf` va escriure a partir del BDF de l'app: surt **el mateix bloc, byte a byte**, i un directori tallat, una taula fora del fitxer, els bits al revés, una codificació de dos bytes o un codi que apunta més enllà dels glifs es refusen |
| **Unitat** | `test/test_inflate/` | El deflate llegit de tornada: un stream comprimit pel zlib de debò —bloc dinàmic i bloc fix— torna com era, els **dos checksums** dels contenidors (Adler-32 del zlib i CRC-32 del zip) amb els valors que tothom cita, i el que **no és un stream** es refusa amb el seu motiu: un stream tallat, un quart tipus de bloc que no existeix, una longitud que no concorda amb el seu complement, una sortida que no hi cap, un diccionari preestablert, i un punter nul, que no és el mateix cas que un buffer buit |
| **Unitat** | `test/test_ttf/` | Els contorns d'un TrueType: el que la font diu de si mateixa (64 unitats de cos, set glifs, l'ascendent), el codi que troba el seu glif pel `cmap` i el que no hi és, el **pas del glif** en píxels i a qualsevol mida, i el dibuix: **la `A` a vuit píxels surt amb les vuit files del `font-bdf`**, columna a columna, i a setze és el doble amb la caixa de tinta que li toca. Es refusen un fitxer que no és cap font, un `OTTO` (CFF), un directori tallat, una taula fora del fitxer i un glif que no cap al bloc |
| **Unitat** | `test/test_disk/` | El disc del banc, dispositiu per dispositiu: la **taula** de `emu/disk.h` (blocs de 4 KB i 819 µs de flash, 45 ms d'esborrat, 512 B i 410 µs de targeta, **90 µs per byte** de FRAM), que un bloc de flash pagui el seu cable i no un errada, que una **escriptura pagui el sector** que littlefs ha d'esborrar, que vuit bytes de FRAM costin el bus I2C, i que amb els temps apagats el mateix bloc d'escriptura no demani res al rellotge |
| **Unitat** | `test/test_bar/` | La barra: que es quedi les 48 files de dalt, que es pinti quan canvia alguna cosa seva, que **no** es pinti quan no canvia res i que el gir se l'emporti amb la vista |
| **Contracte** | `test/test_luabind/` | Cada funció de `30 · API`: que existeixi, que torni el que diu, i **què fa quan la capacitat no hi és**. I el que **costa** l'intèrpret: amb una banda que diu el preu d'una instrucció, un bucle de dues mil sumes en paga el doble a quatre mil, i una banda que en diu zero —una placa— no en paga cap (docs/85) |
| **Shell** | `test/test_shell/` | Les ordres del prompt responen i no peten, `cat` i `hexdump` inclosos, **i el que costa es conta**: una crida pel VFS és una fila del seu comptador amb el seu temps, `stats` l'ensenya i `stats reset` la torna a zero, i una línia de log porta sempre la seva lletra de nivell i la seva etiqueta |
| **App** | `test/test_apps/` | Apps de Lua senceres: el manifest, l'intèrpret, les promeses, la consola i el codi de sortida junts, **i el comptador d'una crida que torna una promesa, que ha de portar el temps de l'espera i no el de la trucada**. La consola s'escriu a un fitxer (`dup2` sobre `STDOUT_FILENO`) i el que l'app ha escrit es comprova lletra a lletra. També **el camí d'una persona pels controls**: el dial canvia el focus, el centre activa el botó i el tàctil fa el mateix amb les coordenades de la vista, amb un `ui.control` genèric que també és enfocable |
| **Maquinari** | `test/device/` | Una llista de comprovacions que només es poden fer amb la placa a la mà (encara no hi és) |

Regles:

- **Una prova per canvi.** Curta, i que mesuri la **conseqüència**, no que la funció existeixi.
- **Les proves han de fallar, no petar.** Si una prova peta en comptes de fallar, la prova està
  mal escrita.
- **Trencada a propòsit.** Cada prova nova s'ha de veure vermella almenys una vegada, trencant el codi
  que vigila. Una prova que no s'ha vist fallar no se sap si mira res.
- La prova nova d'una crida de Lua ha d'incloure el **cas de la capacitat absent**: és el camí que
  més vegades es trencarà.
- **Cap prova no paga el temps del maquinari.** `Options::delays` neix **fals**, o sigui que una suite
  no espera els 1 ms d'una lectura del disc, ni els 86,8 µs d'un byte del port, ni el bus i la tinta
  d'una pintada, ni les tres transaccions del tàctil; la que mesura temps el posa a cert i ho diu
  (`test_ui`, `test_touch`). Al binari és al revés: els temps es paguen sempre, i `--no-delay` és el
  mateix commutador per a un guió (docs/85).
- **La pantalla de l'usuari no es toca.** Si una prova o una comprovació necessita la finestra de
  l'emulador, s'obre en un X propi —un X virtual (`xvfb-run -a`, o `Xvfb :99`) o un servidor X que la
  prova encengui i apagui—, mai al `DISPLAY` de qui treballa. Així no es mou el ratolí, no es roba el
  focus ni es tapen finestres. `pio test` no enllaça SDL2 ni X11 i no obre cap finestra; la finestra
  és SDL2 quan hi és i X11 de reserva (docs/85).

## Que una app Lua funcioni a l'emulador vol dir…

…que el codi de l'app, el manifest, les promeses, la consola, el VFS i els pressupostos funcionen. **No**
vol dir que la placa vagi: a la placa hi ha un rellotge de debò, una flash de debò i un port sèrie de
debò.

## Què no es pot provar sense maquinari

| Cosa | Per què |
|---|---|
| Consum elèctric | L'emulador no té bateria |
| Temps real d'un `read` de flash o d'SD | L'emulador llegeix del disc, que és milers de vegades més ràpid |
| Desgast de la flash | No hi ha cicles que comptar |
| Despertar de son profund | No hi ha son |
| Latència del port sèrie i del monitor | Depèn del pont USB de la placa |
| Memòria de debò | 320 KB no es poden simular; es **limita** la VM amb `--mem-cap`, que és una aproximació |

La latència del port **sí** que es pot mesurar, només que des de fora de la placa: és
`tools/serial_ping.py`. Arrenca l'app `ping` (que respon `pong` a cada línia), hi envia deu línies i
cronometra cada anada i tornada des del PC. La placa ha d'estar al prompt i l'app `ping` al disc:

```
tools/serial_ping.py --port /dev/ttyUSB0 --rounds 10
```

Dona mínim, mediana i màxim. El número inclou el port, la tasca lectora de consola, el torn del nucli,
la promesa i la tornada; no és el temps d'una operació de l'app. **El port pot ser `ttyUSB0` o
`ttyACM0`** segons el pont de la placa (docs/50), i `tools/serial_bridge.py` diu quin és sense obrir-lo.

**Si una pujada de firmware falla** amb `timeout` o `Failed to write to target RAM`, abans de tocar
res: mira quin pont és (`tools/serial_bridge.py`), i si és un WCH comprova que el port és el de debò.
El pla B que sempre funciona és baixar la velocitat a 115 200 a `boards/<id>.json`
(`upload.speed`).

Quan un número d'aquesta llista aparegui en un report, ha de portar la paraula «mesurat a la placa» i
amb què. Si no, va marcat com a pendent.

## Proves que han d'existir abans de la primera fita

- La cua de temporitzadors ordena i programa **només la primera**.
- Un fitxer de 5 KB llegit amb una finestra de 512 B fa els accesses que toca i no carrega el fitxer.
- Un camí amb `..` dona `E_INVAL` i no arriba al backend.
- Un `await` fora d'una tasca dona `E_NOTASK`.
- Una alarma d'una app no carregada s'executa a la finestra d'alarma i el primer pla continua.
- Un error dins d'una finestra d'alarma no tomba el primer pla.
