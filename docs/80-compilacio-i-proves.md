# 80 · Compilació i proves

## Sis entorns

| Entorn | Plataforma | Per a què |
|---|---|---|
| `M5 Atom` | espressif32 + Arduino | El firmware de la placa de proves |
| `M5 Paper` | espressif32 + Arduino, placa pròpia | La M5 Paper (16 MB de flash, PSRAM i un panell e-ink): definició a `boards/m5stack-paper.json`, taula a `partitions_16mb.csv` (docs/50) |
| `M5 Atom Matrix` | espressif32 + Arduino | La placa del panell de vint-i-cinc llums: mateix xip, mateixa taula i mateix `board =`, i el que canvia és el `-I` de la identitat (docs/50) |
| `ESP32 DevKit` | espressif32 + Arduino | **La placa del mínim**: un ESP32 de DevKit (`esp32dev`, 4 MB, sense PSRAM), la més corrent que hi ha. No és cap fitxa de cap aparell: és la prova que el sistema cap en el xip pelat (docs/50) |
| `emulator` | native | El mateix codi compilat per al PC |
| `test` | native | Les proves natives (Unity) |

```ini
[platformio]
default_envs = emulator
boards_dir = boards        ; el Paper no és una placa que la plataforma conegui

[env:m5-atom]
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

[env:m5-atom-matrix]
extends = env:m5-atom
build_flags = -std=gnu++11 -Ilib/board_matrix/include
             -DARDUINO_LOOP_STACK_SIZE=16384
lib_ignore = hal_emulator, pal_host, board_atom, board_paper, board_esp32, …
; i les de xarxa, igual

[env:m5-paper]
platform = espressif32
board = m5stack-paper
framework = arduino
board_build.partitions = partitions_16mb.csv
extra_scripts = pre:tools/check_fs_names.py
build_flags = -std=gnu++11 -Ilib/board_paper/include
             -DARDUINO_LOOP_STACK_SIZE=16384
lib_ignore = hal_emulator, pal_host, hal_atom, board_atom, board_matrix, …
; hal_esp32 no s'ignora: és el que aquesta placa comparteix amb l'ATOM (docs/50)

[env:esp32-devkit]
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
identitats de les altres tres plaques** perquè el build en vegi exactament una; l'emulador no en té cap
de `lib/board_*` i porta la seva a `lib/hal_emulator/include/board/`.
**L'emulador no enllaça res per a la xarxa**: la pàgina que demana `http` la baixa el `curl` d'aquesta
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
   a les altres. (L'emulador i les proves no tenen cap `lib/board_*`: la seva identitat és dins
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
pio run -e m5-atom            # firmware; les proves de flashejar les fa l'amo
pio run -e m5-atom-matrix     # la placa del panell: 25 llums al mateix GPIO27
pio run -e esp32-devkit           # el mínim: un ESP32 de DevKit, sense res a sobre
pio run -e ttgo-t-koala          # la LilyGO T-Koala: WROVER-B, PSRAM mapejada, LED i SD
pio test -e test           # proves natives
```

Cap sessió automàtica no puja res a la placa. Compilar sí; flashejar, no.

**Una sola prova, per buscar-ne una de vermella** (0.152.5): la suite sencera
dura un minut, i caçar un cas vermell demanava un minut per pregunta. Unity no té
cap filtre, així que `main` de `test/test_apps/test_main.cpp` substitueix el seu
`RUN_TEST` per un que mira l'entorn abans de córrer el cas:

```
LUNOK_TEST=test_the_shelf_has_a_foot_of_its_own pio test -e test --filter test_apps
```

Sense la variable corren tots els casos com sempre, i el resum compta els que han
 corregut (un cas saltat no compta ni com a verd ni com a vermell). El nom és el
de la funció, tal com el llista el resum del `RUN_TEST`.

## El CI

`.github/workflows/ci.yml` passa a cada push i a cada pull request:

- la **llei 10**: `tools/check_version.py` recorre els commits del push o de la pull request i refusa
  el que canvia fitxers sense pujar `kernel::kVersion`; els dos commits de 0.88.2 que se'n van escapar
  són el motiu (docs/80);
- la passada de **clang amb `-Wunreachable-code`** sobre el nucli, `luabind` i les llibreries pròpies; és
  l'única que veu el codi que GCC deixa passar, perquè a GCC `-Wunreachable-code` és un no-op (els dos
  `meter::Scope` morts que es van arreglar a 0.89.1 hi van viure, docs/30);
- les **556 proves natives** (`pio test -e test`), i **cap de vermella** (0.171.0): les dotze de la 0.169.0 eren quatre
  errors del codi i vuit casos que havien envellit, i la caiguda d'`test_book` amagava quaranta-nou casos d'`test_apps`
  que no s'havien executat mai (docs/75, docs/90, `kernel/version.h`). Les quatre de la 0.171.0 són les dues portes
  noves (`test_luabind`) i la pàgina de l'esborrador amb targeta i sense (`test_apps`);
- les **sis bandes**: `emulator` (amb SDL2, perquè també es compili la finestra), `m5-atom`, `m5-atom-matrix`,
  `m5-paper`, `esp32-devkit` i `ttgo-t-koala`. **Els noms dels entorns són els de les plaques** (7/11, l'amo), amb
  guions i no amb espais, i és el nom sencer el que va a `-e`: `pio run -e m5-paper`.

El CI **no flasheja cap placa ni pren cap mesura de camp**: això continua sent d'una persona (docs/95).

## Què s'ha de posar a la placa (31/10)

A la flash del Paper hi van **dues coses diferents**, i una ordre no fa l'altra:

| Què | Ordre | On va |
|---|---|---|
| El firmware | `pio run -e m5-paper -t upload` | la partició `app0`, 4 MB (`partitions_16mb.csv`) |
| El disc: els `res/` del sistema | `pio run -e m5-paper -t uploadfs` | la partició `littlefs`, 11,9 MB, amb el contingut de `build/disk/m5-paper` (**el sistema sol**: `res/`, `http/` i `data/`, **16 fitxers i 130 217 B** mesurats el 8/11) |

- **La imatge d'una placa no porta apps** (0.118.3, l'amo: *«cap placa ha de contenir apps al littlefs al
  flashejar»*): el disc de cada entorn el munta `tools/make_disk.py` a **`build/disk/<entorn>`** amb el `data/`
  del sistema i res més. L'única excepció és la Paper, que en nomena **deu** (`custom_apps =
  editor,calculator,about,timeclock,reader,home_eink,shell,luaperf,tz,wipe`, **46 fitxers i 357 820 B** mesurats el 9/11). Un build que en vulgui portar les demana al seu `platformio.ini` —`custom_apps =
  id1,id2` per a una llista exacta, o `custom_apps_in_disk = yes` per a les que nomenen la placa per l'id o per
  la família `esp32`, que és la regla amb què el nucli refusa una app al `run`— i llavors cada app del disc és
  **un enllaç** a la seva carpeta d'`apps/` (l'entorn `test` les copia, perquè les seves proves escriuen al
  disc). El detall és a docs/70, «Com afegir apps a la imatge del disc».
- **L'emulador sí que les porta totes** (8/11, l'amo: *«afegeix totes les apps a l'emulador»*): l'entorn
  `emulator` diu `custom_apps_in_disk = yes` i, com que l'emulador no té placa, el filtre de `make_disk` les
  hi deixa passar totes —**10 apps, 46 fitxers, 357 820 B** mesurats el 9/11— i les posa **enllaçades**, o
  sigui que l'emulador llegeix el que hi ha a `apps/` i no una còpia. Fins a l'8/11 en portava una, el llançador, i
  `apps` en un emulador acabat de fer deia `no apps`; ara diu deu noms i la graella del llançador té deu cel·les.
  Provar una app instal·lada continua sent el que fa la placa quan el disc que es llegeix és el d'una placa
  (`--flash build/disk/m5-paper`, i llavors `repo install <app>` o `--sd`, docs/70, docs/85).
- **Si un build en demana massa, ho diu i no perd fitxers en silenci.** `make_disk` munta la imatge amb
  `mklittlefs` —la mateixa partició de la taula, pàgina de `0x100` i bloc de `0x1000`— i, si l'eina deixa
  fitxers fora, **refusa l'ordre** `-t buildfs`/`-t uploadfs` en comptes d'escriure una imatge curta que
  PlatformIO donaria per bona (el guard és `check_it_fits`, a `tools/make_disk.py`). A cada build la línia ho
  diu i el número és la mesura: `the tool takes every one of the N files in P B of partition, so it fits`, o
  **`this disk does not fit`** amb quants en queden fora i quins noms.
  **El que hi ha avui, mesurat el 9/11**: `apps/` són **46 fitxers i 357 820 B**, i el `data/` del sistema
  **19 fitxers i 1 688 651 B** (el que el repositori porta; `data/data/` és estat d'una app i no és al git). **El disc no és la suma** i la seva mida la diu `make_disk` a cada build: el 9/11 va
  escriure *`build/disk/emulator is 2050733 B in 68 files, 10 apps, linked to apps/`* i *`build/disk/m5-paper is
  2046471 B in 65 files, 10 apps, linked to apps/`*, i les dues imatges hi caben (la Paper, en la partició de
  12 451 840 B). **A les quatre plaques de 4 MB no hi cap**: sense cap app, `build/disk/m5-atom` fa **1 688 651 B en
  19 fitxers** i la partició de `partitions.csv` és de **1 441 792 B**, o sigui que `-t buildfs` s'hi atura mentre els
  tres bitmaps de splash (`data/res/splash`, 1 558 434 B dels tres i 1 595 456 B la carpeta) siguin a `data/` (docs/70). La xifra que hi havia aquí —*148 fitxers i 418 720 B per a «tot `apps/`»*, i la imatge deixant
  dos fitxers fora— **no és d'aquest arbre** (`git ls-tree -r apps` en dona 37 el 9/11) i s'ha retirat: el que queda
  d'aquella mesura és el guard, que és cert, i no els números. La raó per la qual cap placa no en porta no és
  la mida sinó la decisió de l'amo i el fet que una imatge amb apps es queda vella (docs/70).

- **Una placa flashejada amb la imatge del sistema no té cap app**: `apps` surt buit, `run reader` diu que no
  la troba, i això no és cap error del sistema. Les apps hi arriben amb `repo install <id>` o amb una targeta
  (docs/40). El `data/` del sistema sí que s'hi ha de pujar quan canvia: `res/`, els tipus de lletra de la
  barra i el teixit de la splash viatgen aquí.
- **No cal posar res a la targeta SD**: el slot és una targeta i prou. Si n'hi ha una, `/sd` apareix i
  `cfg auto` hi desa els ajustos; cap fitxer és obligatori.
- **`emulator/littlefs` no es puja mai**: és la còpia privada d'un emulador que demana `--flash
  emulator/littlefs`; el disc de l'emulador, per defecte, és `build/disk/emulator` (docs/85).
- El nom d'un fitxer de `data/` o d'`apps/` no pot passar de 32 caràcters: `tools/check_fs_names.py` ho converteix
  en un build fallit en lloc de deixar que la imatge el perdi en silenci (docs/70).
- Per saber que tot hi és, després de flashejar: `version` (la versió i la placa), `caps` (**16 de
  19** al Paper amb targeta i bateria, o **17** si el terminal contesta ANSI), `mem` (els tres números
  de la memòria extra), `mounts` (fins a
  nou files, amb `/rtc` i `/fram`) i `apps` (les apps del disc). I una taula de 60 000 enters des de `lua`,
  que és la meitat que falta de la mesura E-4 de `docs/95`.

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
| **Unitat** | `test/test_ui/` | La capa de controls contra el vidre de l'emulador: la vista i el canvas, els danys amb la seva qualitat, el rectangle que comença en un píxel senar, la llista de vuit que s'omple, la composició **fills primer**, els controls **sense vista** (pinten al primer canvas que els conté, tallats al seu marc), el redibuix tallat a la caixa que el control deu, el forat que deixa un fill que es mou, la neteja abans d'alliberar amb **un sol rectangle per subarbre**, el **marc i la generació** que fan que un objecte mort respongui `E_INVAL`, el **model de la tinta** (el rastre d'un `du`, la neteja d'un `gc16`, el temps de cada qualitat, el parpelleig d'`a2` en tres moments i el preu del bus que quadra amb els 1 985 ms de la placa l'1/11) i **la finestra** (el color calent dels setze nivells i el pas a píxel amb escala), i **quina placa té vidre**: el model diu que la Paper en té i que els ATOM no, i una opció no dona pantalla a una placa que no en té, i **el camí de tornada del píxel de la finestra al píxel de la pàgina** (el centre del bloc d'escala i la vora per a un punter fora), i **la tira de les tecles** (la columna és pujar/centre/baixar, la seva geometria —96 d'ample, botons de 80 × 54 centrats— i que un clic al buit no prem cap tecla), i **els controls**: el retall d'UILabel, i el marc d'UIButton amb la vora premuda, el fill tallat al botó i el fill que es torna a dibuixar quan el marc canvia, i **UICheckbox** (la caixa amb el tick d'un color propi, el forat de premut i l'anell del focus fora de la caixa) |
| **Unitat** | `test/test_touch/` | El panell de la Paper a l'emulador: **què costa una lectura** (800 µs sense dit, 1 830 amb un i 2 550 amb dos, i el probe de 186 bytes de l'arrencada), **un marc per escombrat** —la segona lectura de la mateixa finestra diu que no hi ha ningú—, la configuració d'aquesta unitat (dos dits, mides 28 i 30, el punt fora del marc refusat), el dit que se'n va, el **xip més lent que el torn** (40 ms contra 20: la meitat de les lectures diuen ningú), i un dit que acaba en un gest `long` amb les coordenades de la vista (100,200 del vidre són 100,152 de la vista) |
| **Unitat** | `test/test_battery/` | La bateria: la **corba en els dos sentits** (4 200 mV plena, 3 300 buida, cada fila torna com va anar-hi, i el voltatge només puja amb la càrrega), la **bateria de la màquina** llegida d'uns fitxers del nucli (63 %, carregant o no, i un `150` que no és cap bateria), la **cel·la prestada** (el 100 % són 4 200 mV, el 63 % són 3 847, i una màquina sense bateria deixa la cel·la al **càrrec nominal, 3 700 mV**, perquè un Paper sempre en té), i que **preguntar el gauge pagui l'ADC** (400 µs) i no el pagui amb els temps apagats |
| **Unitat** | `test/test_font/` | El tipus de lletra portable: un fitxer de text que es converteix en una cara (la mida, el forat, els codis en ordre i cada `#` on toca), **l'amplada d'una línia** amb l'escala i sense forat al final, els glifs **com a tirades de píxels** tallades a la caixa del label, i cada manera que un fitxer deixa de ser una cara amb el seu motiu. I **el BDF**: les files hexadecimals cap al mateix bloc de setze per setze (amb dues bytes per fila quan el glif passa de vuit columnes), el `COMMENT` i les propietats que no diuen res, `ENCODING -1 <n>`, les seves maneres de dir que no, i que el **sniffer** (`font::load_any`) triï el lector pel que diu el fitxer i salti els comentaris de davant. I **el PCF**, amb el fitxer de debò que `bdftopcf` va escriure a partir del BDF de l'app: surt **el mateix bloc, byte a byte**, i un directori tallat, una taula fora del fitxer, els bits al revés, una codificació de dos bytes o un codi que apunta més enllà dels glifs es refusen |
| **Unitat** | `test/test_inflate/` | El deflate llegit de tornada: un stream comprimit pel zlib de debò —bloc dinàmic i bloc fix— torna com era, els **dos checksums** dels contenidors (Adler-32 del zlib i CRC-32 del zip) amb els valors que tothom cita, i el que **no és un stream** es refusa amb el seu motiu: un stream tallat, un quart tipus de bloc que no existeix, una longitud que no concorda amb el seu complement, una sortida que no hi cap, un diccionari preestablert, i un punter nul, que no és el mateix cas que un buffer buit |
| **Unitat** | `test/test_ttf/` | Els contorns d'un TrueType: el que la font diu de si mateixa (64 unitats de cos, set glifs, l'ascendent), el codi que troba el seu glif pel `cmap` i el que no hi és, el **pas del glif** en píxels i a qualsevol mida, i el dibuix: **la `A` a vuit píxels surt amb les vuit files del `font-bdf`**, columna a columna, i a setze és el doble amb la caixa de tinta que li toca. Es refusen un fitxer que no és cap font, un `OTTO` (CFF), un directori tallat, una taula fora del fitxer i un glif que no cap al bloc |
| **Unitat** | `test/test_disk/` | El disc de l'emulador, dispositiu per dispositiu: la **taula** de `emu/disk.h` (blocs de 4 KB i 819 µs de flash, 45 ms d'esborrat, 512 B i 410 µs de targeta, **90 µs per byte** de FRAM), que un bloc de flash pagui el seu cable i no un errada, que una **escriptura pagui el sector** que littlefs ha d'esborrar, que vuit bytes de FRAM costin el bus I2C, i que amb els temps apagats el mateix bloc d'escriptura no demani res al rellotge |
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
- **El que una prova desa, una prova ho torna.** Dues coses d'aquest emulador no són del procés i sí del terminal
  de qui el llança, i totes dues s'han vist quedar trencades (4/11):
  - **La consola.** `pump_captured`, `feed_captured` i `capture_begin` posen `stdout` en un fitxer amb `dup2`
    per llegir el que diu una app. Un `TEST_ASSERT` que falla surt per un `longjmp` de Unity i **no passa mai
    per la línia que el torna a posar**: la fallada i totes les proves de després s'escrivien al fitxer, i el
    `pio test` es quedava esperant un resum que no arribava (i el matava amb SIGHUP). La consola es torna a
    `tearDown`, que Unity crida passi el que passi, i **el que hi havia capturat es torna a dir** quan la prova
    ha fallat, que és l'única vegada que algú el vol llegir.
  - **El terminal.** `pal::init()` el posa en mode cru (sense eco i sense línia) i `pal::shutdown()` el torna,
    però una suite no en diu mai res: s'acabava amb el terminal de qui havia llançat les proves sense eco fins
    a un `reset`. El PAL de l'emulador instal·la `atexit` en encendre el mode cru, o sigui que el terminal es torna
    encara que el procés acabi en un `exit` d'una fallada. Comprovat amb un pty (`script -q -c 'stty -g; …;
    stty -g'`): sense el ganxo el `stty` de després és un altre, i amb el ganxo és el mateix.
- **Cap espera no és per sempre.** Un bucle de proves que espera una app acaba amb un pressupost de torns
  (`kTurnBudget`) i diu que no ha acabat; el que no pot passar és que la suite sencera s'aturi en un bucle
  que no acaba i que ningú sàpiga per què.
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
`tools/serial_ping.py`. Arrenca una app que respon `pong` a cada línia, hi envia deu línies i
cronometra cada anada i tornada des del PC. La placa ha d'estar al prompt i l'app que respon al disc:

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

## La pila també es mesura abans de pujar res: `tools/check_stack.py` (0.118.0)

Una pila que es passa no la veu ni el compilador ni l'emulador: la veu la placa, i la veu com un reinici
(`***ERROR*** A stack overflow in task loopTask has been detected`). El compilador, en canvi, sí que pot
dir **quant gasta cada funció**, i ho fa amb `-fstack-usage`. `tools/check_stack.py` compila una llibreria
així i falla si una funció passa del pressupost:

```
python3 tools/check_stack.py                        # lib/img, 1 024 B per funció
python3 tools/check_stack.py --all --budget 4096    # el mapa de tot el firmware
```

- **Qui té pressupost estret és `lib/img`**, perquè els seus descodificadors corren a la tasca de l'app
  (`stack 7320 B free` al log d'arrencada) i una imatge d'un GIF s'enduia 16 656 B (docs/75).
- **Les llibreries que necessiten les capçaleres de la placa** (`pal_esp32`, `hal_paper`, …) no es poden
  compilar al PC: l'eina les **diu saltades** i no les mesura, en comptes de petar.
- **No és una prova i no executa codi del firmware**: compila objectes i llegeix els `.su` que deixa el
  compilador. Per això es pot córrer sense permís, com `pio run`.

## Proves que han d'existir abans de la primera fita

- La cua de temporitzadors ordena i programa **només la primera**.
- Un fitxer de 5 KB llegit amb una finestra de 512 B fa els accesses que toca i no carrega el fitxer.
- Un camí amb `..` dona `E_INVAL` i no arriba al backend.
- Un `await` fora d'una tasca dona `E_NOTASK`.
- Una alarma d'una app no carregada s'executa a la finestra d'alarma i el primer pla continua.
- Un error dins d'una finestra d'alarma no tomba el primer pla.

## Una prova que llegeix píxels encén el vidre (0.118.1)

**L'emulador neix amb tot apagat** —sense vidre, sense tàctil, sense targeta— perquè això és el que vol una mesura del treball de la placa, i `use_board("m5stack-paper")` canvia el model **sense encendre res**. Una prova que llegeix píxels ha de demanar-ho abans de fer el boot:

```cpp
emu::Options opts = emu::options();
opts.board = "m5stack-paper";
opts.display = true;
emu::set_options(opts);
kernel::boot(hal::platform());          // `hal::platform().display()` is null until this line
```

Sense això, `hal::platform().display()` és `nullptr` i `emu::memory_surface()` dona una pàgina que no s'ha pintat mai: la prova llegeix paper a tot arreu i la imatge que diu comprovar no hi és. Dues proves del torn 0.118.0 fallaven així i no pel codi que provaven (docs/85, docs/92 §33).

**I una app que la prova ha de veure a la pantalla s'ha de quedar viva** (`while true do sys.delay(20):await() end`), perquè quan una app s'acaba el sistema neteja el vidre (`ui::attach`, 0.37.0) i el que la prova vol llegir ja no hi és (docs/30).
