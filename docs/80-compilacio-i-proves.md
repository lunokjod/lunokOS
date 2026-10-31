# 80 · Compilació i proves

## Cinc entorns

| Entorn | Plataforma | Per a què |
|---|---|---|
| `atom` | espressif32 + Arduino | El firmware de la placa de proves |
| `paper` | espressif32 + Arduino, placa pròpia | La M5 Paper (16 MB de flash, PSRAM i un panell e-ink): definició a `boards/m5stack-paper.json`, taula a `partitions_16mb.csv` (docs/50) |
| `atom_matrix` | espressif32 + Arduino | La placa del panell de vint-i-cinc llums: mateix xip, mateixa taula i mateix `board =`, i el que canvia és el `-I` de la identitat (docs/50) |
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
lib_ignore = hal_emulator, pal_host, board_matrix,
             WiFi, WiFiClientSecure, HTTPClient, ESPmDNS, WebServer,
             DNSServer, WiFiProv

[env:atom_matrix]
extends = env:atom
build_flags = -std=gnu++11 -Ilib/board_matrix/include
lib_ignore = hal_emulator, pal_host, board_atom, …   ; i les de xarxa, igual

[env:paper]
platform = espressif32
board = m5stack-paper
framework = arduino
board_build.partitions = partitions_16mb.csv
build_flags = -std=gnu++11 -Ilib/board_paper/include
lib_ignore = hal_emulator, pal_host, hal_atom, board_atom, board_matrix, …
; hal_esp32 no s'ignora: és el que aquesta placa comparteix amb l'ATOM (docs/50)

[env:emulator]
platform = native
build_unflags = -std=gnu++11 -std=gnu++14 -std=gnu++17
build_flags = -std=c++11 -Ilib/hal_emulator/include
lib_ignore = hal_atom, hal_paper, hal_esp32, pal_esp32, board_atom, board_matrix

[env:test]
platform = native
test_framework = unity
test_build_src = yes
build_unflags = -std=gnu++11 -std=gnu++14 -std=gnu++17
build_flags = -std=c++11 -Ilib/hal_emulator/include
lib_ignore = hal_atom, hal_paper, hal_esp32, pal_esp32, board_atom, board_matrix
```

El text de debò és a `platformio.ini`, amb un comentari per línia; aquí n'hi ha la forma. Les
llibreries de xarxa d'Arduino són fora del build a posta (docs/50), i `board_atom`/`board_matrix` són
a `lib_ignore` de **tots** els entorns perquè cada build en vegi exactament una.
`extra_scripts = pre:tools/check_fs_names.py` és a `[env:atom]`: l'eina de la imatge de LittleFS
deixa caure en silenci qualsevol nom de `data/` més llarg de 32 caràcters i tot i així diu SUCCESS, i
aquest script ho converteix en un build fallit (docs/70).

Quatre coses que aquesta configuració fa complir:

1. **C++11 a les dues bandes.** El nucli d'Arduino per a ESP32 compila amb `-std=gnu++11`; si el codi
   del PC fa servir C++17, el projecte es trenca en silenci quan arribi a la placa. Es compila igual
   a tot arreu i s'acaba el problema.
2. **Els avisos són errors, però només els nostres.** A cada `library.json` hi ha
   `"build": {"flags": ["-Wall", "-Wextra", "-Werror"]}`. No es posa a `build_flags` perquè també
   s'aplicaria al **framework d'Arduino**, que té avisos propis (un de conegut a `esp32-hal-uart.c`) i
   no és feina d'aquest projecte arreglar-los. Així el build surt net i el nostre codi no en passa
   ni un.
3. **`lib_ignore` i `src/board_link.cpp`.** Cada entorn no veu mai el codi de l'altra placa: ho diuen el
   `lib_ignore` i el camí d'inclusió (`-Ilib/board_<id>/include`), que és el que fa que
   `#include "board/identity.h"` i `#include "board/impl.h"` resolguin a la placa d'aquest entorn i no
   a l'altra. Però això sol no n'hi prou: PlatformIO descobreix les llibreries de `lib/` seguint els
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
   viu a `lib/hal_emulator/`, tancat amb `#if !defined(UNIT_TEST)` perquè quan corre `pio test` mana
   el `main` de les proves.
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
pio test -e test           # proves natives
```

Cap sessió automàtica no puja res a la placa. Compilar sí; flashejar, no.

## Què s'ha de posar a la placa (31/10)

A la flash del Paper hi van **dues coses diferents**, i una ordre no fa l'altra:

| Què | Ordre | On va |
|---|---|---|
| El firmware | `pio run -e paper -t upload` | la partició `app0`, 4 MB (`partitions_16mb.csv`) |
| El disc: les apps i els `res/` | `pio run -e paper -t uploadfs` | la partició `littlefs`, 11,9 MB, amb el contingut de `data/` |

- **Sense el segon, la placa arrenca i no té cap app**: `apps` surt buit, `run hello` diu que no la
  troba, i això no és cap error del sistema. Es fa un cop, i cada cop que canviï `data/`.
- **No cal posar res a la targeta SD**: el slot és una targeta i prou. Si n'hi ha una, `/sd` apareix i
  `cfg auto` hi desa els ajustos; cap fitxer és obligatori.
- **`emulator/littlefs` no es puja mai**: és la còpia del banc (docs/85). El que es puja és `data/`.
- El nom d'un fitxer de `data/` no pot passar de 32 caràcters: `tools/check_fs_names.py` ho converteix
  en un build fallit en lloc de deixar que la imatge el perdi en silenci (docs/70).
- Per saber que tot hi és, després de flashejar: `version` (la versió i la placa), `caps` (**14 de
  17** al Paper amb targeta i bateria), `mem` (els tres números de la memòria extra), `mounts` (fins a
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

Quatre nivells, i cada cosa va al seu:

| Nivell | On | Què comprova |
|---|---|---|
| **Unitat** | `test/test_json/` | Resolució de camins, muntatges, llistat de `/`, analitzador JSON |
| **Unitat** | `test/test_alarms/` | La cua d'alarmes: ordre, repeticions, perdudes i la còpia a la NVS |
| **Unitat** | `test/test_qr/` | El símbol llegit de tornada com el llegiria un escàner: BCH, desmàscara i Reed-Solomon |
| **Unitat** | `test/test_led/` | El motor dels efectes: els quatre efectes com a funció del temps, les dues capes, la interrupció pel sistema (desar, pintar, restaurar i avisar `on_light`) i el sostre de brillantor de la placa |
| **Contracte** | `test/test_luabind/` | Cada funció de `30 · API`: que existeixi, que torni el que diu, i **què fa quan la capacitat no hi és** |
| **Shell** | `test/test_shell/` | Les ordres del prompt responen i no peten |
| **App** | `test/test_apps/` | Apps de Lua senceres: el manifest, l'intèrpret, les promeses, la consola i el codi de sortida junts. La consola s'escriu a un fitxer (`dup2` sobre `STDOUT_FILENO`) i el que l'app ha escrit es comprova lletra a lletra |
| **Maquinari** | `test/device/` | Una llista de comprovacions que només es poden fer amb la placa a la mà (encara no hi és) |

Regles:

- **Una prova per canvi.** Curta, i que mesuri la **conseqüència**, no que la funció existeixi.
- **Les proves han de fallar, no petar.** Si una prova peta en comptes de fallar, la prova està
  mal escrita.
- **Trencada a propòsit.** Cada prova nova s'ha de veure vermella almenys una vegada, trencant el codi
  que vigila. Una prova que no s'ha vist fallar no se sap si mira res.
- La prova nova d'una crida de Lua ha d'incloure el **cas de la capacitat absent**: és el camí que
  més vegades es trencarà.

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
