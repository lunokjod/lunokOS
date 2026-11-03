# 75 · Pressupost de memòria

## La regla

**L'únic que alloca dinàmicament és Lua.** Tot el demés són taules, pools i buffers fixos, decidits a
la compilació i contats aquí. Si una peça del nucli necessita créixer en temps d'execució, és un error
de disseny i es discuteix abans d'escriure-la.

D'on surt la regla: la placa de referència té **320 KB de RAM i prou** (ni PSRAM ni res). Un sistema
que fragmenta la memòria s'aguanta tres hores i després peta, i això no es pot depurar.

## Sostres

| Nivell | Qui el fixa | Valor |
|---|---|---|
| RAM de la placa | El maquinari | 320 KB a l'M5 ATOM, sense PSRAM |
| Heap lliure amb el prompt | **Mesurat a l'ATOM, 30/10, amb LittleFS muntada i Lua al binari** | **338 864 B** |
| Heap mínim vist | **Mesurat a l'ATOM, 30/10** | **327 496 B** amb el prompt sol; **315 944 B** amb una app que espera; **301 640 B** amb l'app `demo` (colors, espera i lectura) |
| Sostre de la VM | `kDefaultHeapBytes` a `lib/kernel/src/shell.cpp` | **65 536 B** mentre `heap_limit_kb()` no digui un altre número |
| Sostre per app | `IPlatform::heap_limit_kb()` | El de la VM (65 536 B) mentre la placa no en digui un altre |
| **Memòria extra (PSRAM)** | `IPlatform::extra_memory()` (torna `{chip_bytes, total_bytes, free_bytes}`) i `pal::IExtraMemory` | **4 096 KB al Paper** (el xip en té 8 MB i l'ESP32 en mapeja la meitat) i res als ATOM. És el sostre d'una app en aquesta placa (docs/50) |
| Bloc «gran» que va a la memòria extra | `kExtraThreshold` a `lib/luabind/src/luabind.cpp` | **4 096 B**: d'aquí cap amunt el bloc viu a la PSRAM, i per sota a la RAM del xip |
| Pressupost d'instruccions | **No existeix encara** | Avui una app que no cedeix penja la placa (docs/20, docs/90) |
| Sostre vist al **Paper** | **Mesurat a la placa, 31/10**, amb `run burst` | `heap used 37 883 B, peak 37 883 B`: la mesura és d'abans de la 0.7.0, quan la placa encara contestava el sostre per defecte. **Avui** `IPlatform::heap_limit_kb()` hi diu **4 096 KB** (la PSRAM mapejada) i el mateix `run burst` dona `limit 4 194 304 B` |

Nota sobre els 338 864: és més gran que els 320 KB del full de dades del xip perquè el heap de
l'ESP-IDF inclou regions d'instruccions que també es poden fer servir com a memòria. El número que
compta per a nosaltres és el que reporta el sistema, no la suma del datasheet. Els 346 524 B i
341 200 B que hi havia abans eren **d'abans del sistema de fitxers i de Lua** i queden substituïts
per aquests, no heretats.

## Consum estàtic

| Component | Reserva | Consum |
|---|---|---|
| Cua d'esdeveniments | 16 × `sizeof(Event)` | **128 B** (16 × 8 B) |
| Anella de log | 2 KB | **2 048 B** |
| Motor dels efectes del llum (`led`) | 4 efectes del sistema + la capa de l'app | **uns 250 B**, calculat amb `sizeof` (vegeu §«El motor dels efectes»); el build de la 0.10.0 hi suma **+312 B** de RAM amb el mòdul `env` a dins, i no es poden separar sense treure'n un |
| Editor de línia de la shell | 1 línia de 128 B + 1 esborrany de 128 B + 8 ordres × 128 B | **1 304 B** (mesurat amb `sizeof`) |
| Buffer de línia de l'app (`luabind`) | 1 × 128 B | **128 B** |
| Selector d'extensió (`shell`) | 8 candidats × 32 B + camí (128 B) + extensió (16 B) | **400 B** (mesurat: treure la tria desada va baixar la RAM estàtica de 28 948 a 28 756 B, i eixamplar el camí de 96 a 128 B l'ha apujada 32 B, fins a 28 788 B) |
| Buffer d'ordre del nucli | 1 × 128 B | **128 B** (a la pila) |
| Línia del log | 1 × 160 B | **160 B** (a la pila) |
| Pila de la tasca lectora de consola | 2 048 paraules | **8 192 B** |
| Taula de muntatges (`vfs`) | 10 entrades (9 ocupades al Paper: `/littlefs`, `/apps`, `/res`, `/data`, `/sd`, `/rtc`, `/fram`, `/nvs` i `/cfg`; 7 a l'ATOM, que no té targeta ni FRAM) | **10 × (nom + estat + 40 B)** |
| Pool de fitxers (`fs_ops`) | 8 × (mànec, posició, mida) | **288 B** en total amb l'objecte de Lua (mesurat: la RAM estàtica de l'ATOM ha passat de 31 044 a 31 332 B) |
| Cua de temporitzadors | N × mida fixa, ordenada en lloc | encara no existeix (M3) |
| Cua d'alarmes (`lib/alarms`) | 8 entrades de 112 B | **896 B** (mesurat amb `sizeof`: `alarms::Entry` fa 112 B) |
| Còpia del nom de l'app (`luabind`) | 1 × 32 B | **32 B** (el `Boot` porta punters del qui crida i poden morir abans que l'app) |
| Buffer del valor de `/nvs` (`kernel`) | 1 × 1 024 B | **1 024 B** (un valor sencer a la memòria: obrir un fitxer no pot reservar; docs/70) |

### Què costa la ràdio (mesurat el 30/10, ATOM)

| Què | Abans | Amb la ràdio | Cost |
|---|---|---|---|
| Flash | 482 909 B | **932 645 B** | **+449 736 B** |
| RAM estàtica | 31 340 B | **52 460 B** | **+21 120 B** |

I el **provisioning** (el component d'Espressif, amb SoftAP i Security 1) a sobre: flash
**932 645 → 1 034 765 B** (**+102 120 B**) i RAM estàtica **52 460 → 52 516 B** (**+56 B**).
I el **QR** que es dibuixa a la consola: flash **1 034 765 → 1 038 889 B** (**+4 124 B**) i RAM
estàtica **52 516 → 56 532 B** (**+4 016 B**, gairebé tot la matriu del símbol, que és estàtica per no
reservar res en dibuixar-lo).

El QR general (versions 1-20, quatre nivells, tres modes) i la seva crida de Lua: flash **1 038 889 →
1 042 333 B** (**+3 444 B**) i RAM estàtica **56 532 → 62 980 B** (**+6 448 B**). D'aquests, la
llibreria té `g_fn` (mapa de mòduls de funció, 1 177 B) i dos buffers de codewords de 1 200 B, i
`luabind` té els 4 096 B del símbol que es pot tornar a una app (`qr.encode`). **El límit de versió 20
és exactament aquest preu**: apujar-lo a 40 demanaria uns 12 KB més de RAM per a un símbol que no cap
enlloc. Queden **530 567 B** lliures.

### La pila també és memòria, i el 30/10 es va acabar

La primera versió del QR general tenia una matriu de blocs d'error correction **a la pila**:
`uint8_t ec[64][64]`, 4 096 B. A l'emulador no es va veure mai perquè la pila del PC és gran; a la
placa va petar de seguida:

```
Guru Meditation Error: Core 1 panic'ed (Unhandled debug exception).
Debug exception reason: Stack canary watchpoint triggered (loopTask)
```

La **pila del `loopTask` és de 8 192 B** i la cadena de crides (`poll` → `provision_start` →
`print_qr` → `qr::encode` → `build_codewords`) se n'enduia més de la meitat d'un sol cop.

- **La regla, ara**: cap buffer gran a la pila. El que passa d'unes desenes de bytes va a **estàtic**
  (un codificador alhora, una segona còpia no compraria res). Cost del canvi: **+952 B** de RAM
  estàtica, que és menys del que ocupava a la pila i no es perd.
- **El que ho vigila**: `test_the_table_fits_the_working_buffers` comprova que la taula no demana mai
  més de **25 blocs** ni més de **30** codewords de correcció per bloc entre les versions 1 i 20, que
  és la mida dels buffers. Si algú apuja el límit de versió, aquesta prova ho diu abans que la placa.

La xarxa Wi-Fi sola (sense TLS) es menja **449 736 B**, el 28,6 % de la partició d'app. El TLS
hi és des del 31/10 i costa **+196 348 B** més, ja mesurat i a la secció «El preu del TLS» d'aquí
dalt.

Les files que diuen «encara no existeix» no són forats: són components de fites posteriors i es
mesuren quan es fan. La resta són mides preses del codi i del mapa de l'enllaçador, amb data.

Mesures de l'enllaçador a l'ATOM el 31/10 (`pio run -e atom`), amb Lua 5.4.7, `run`, les promeses i la
baixada d'una pàgina amb TLS a dins, més el mòdul dels ajustos (`/cfg`), la reescriptura de `/res`, el
buitat de la consola abans de dormir i la mesura de l'últim despert: **+3 612 B de flash i +40 B de
RAM** sobre la mesura anterior del mateix dia (2 005 361 B i 92 624 B).

| Què | Valor |
|---|---|
| Flash | **2 020 769 B** de 3 145 728 (64,2 %) — la partició d'aplicació és de 3 MB des del 31/10 |
| Flash lliure | **1 124 959 B** |
| RAM estàtica | **93 096 B** de 327 680 (28,4 %) |
| **Mesurat amb la 0.10.0** (1/11, `pio run -e atom`): és el total d'avui, i el que creix des de la columna del mig d'aquesta secció són el `/rtc`, el `/fram`, el `poweroff`, la memòria extra, els efectes del llum i el sensor d'ambient. L'`atom_matrix` dona **2 020 781 B** i **95 464 B** (29,1 %) | — |
| Pic de l'app `hello` a l'ATOM | **10 897 B** de 65 536 (assignador de Lua, **mesurat a la placa el 30/10** amb `run hello` i `mem`) |
| Pic de l'app `hello` a l'emulador | **14 364 B** de 65 536 (mateix codi al PC) |
| Pic d'una app amb `sys.delay(1000)` a l'ATOM | **13 402 B** de 65 536 (**mesurat a la placa el 30/10** amb `run wait` i `mem`) |
| Pic de l'app `demo` (colors + espera + línia) a l'ATOM | **26 821 B** de 65 536 (**mesurat a la placa el 30/10**, dues vegades: 26 808 i 26 821); heap mínim vist, **301 640 B** |
| Pic d'una app amb `sys.delay(1000)` a l'emulador | **17 285 B** de 65 536 (`wait`, mateix codi al PC) |
| Pic d'una app amb **100 promeses seguides**, a l'emulador | **38 205 B** de 65 536 (`burst`: 100 × `sys.delay(1)`, 115 ms); abans del bucle, 19 969 B |
| Pic d'una app amb **100 promeses seguides**, a l'ATOM | **27 018 B** de 65 536 (**mesurat a la placa el 30/10**); abans del bucle, 15 665 B → **11 353 B** de sobrecost; 100 esperes d'1 ms en **199 ms** |

## El preu de Bluetooth, mesurat (31/10)

El provisioning per Bluetooth és el que el fabricant recomana, i la pila que
aquest build enllaça és **Bluedroid** (`CONFIG_BT_ENABLED=y`, `CONFIG_BT_NIMBLE_ENABLED`
no; no hi ha `libnimble.a` al paquet). Costat amb el número davant:

| | Abans (SoftAP) | Amb Bluedroid |
|---|---|---|
| Flash | 1 050 677 B | **1 761 265 B** (+699 940 B, ~684 KB) |
| RAM estàtica | 68 392 B | **84 408 B** (+16 016 B: +12 856 del Bluetooth, +1 344 de la cua, +1 856 del client de temps) |
| Partició d'aplicació | 1 572 864 B → **111,3 %** | 3 145 728 B → **55,7 %** |

O sigui: **no hi cabia**. Amb la taula de particions d'abans el build va dir
`Flash: 111.3% (used 1750617 bytes from 1572864 bytes)` i va fallar. Les dues ranures
d'1,5 MB s'han fusionat en una de 3 MB (`partitions.csv`), cosa que la mateixa taula
ja preveia: la segona ranura era per a OTA, i OTA no existeix. El preu és que **no hi
ha actualització per aire**; el firmware entra pel cable, que és el que es fa avui.
Si algun dia OTA torna, el primer que ha de marxar és el provisioning per Bluetooth
amb Bluedroid.

El Bluetooth **no s'encén a l'arrencada**: `bt_ready()` el puja quan algú demana
`provision` i l'esquema l'allibera en acabar (`WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM`).
Una placa que no es provisiona mai no paga la pila en RAM, només el codi al binari.

## El sostre del codi d'entrada, pujat a 8 KB (31/10)

`run` llegeix el fitxer Lua de l'app en un buffer estàtic abans de compilar-lo, i aquest buffer era de
**4 096 B** (`kSourceCap`). L'app `matrix`, un cop plena de rampes, fa **4 407 B**: el sistema responia
`too big to run: /apps/matrix-app/matrix.lua`, que és un límit dient que no a una app que cap a la
placa. El sostre és ara de **8 192 B**, i es paga en RAM estàtica, que és el que diu la taula:

| | Abans | Amb el sostre a 8 KB |
|---|---|---|
| RAM estàtica | 88 256 B | **92 456 B** (+4 200 B) |
| Flash | 1 987 113 B | **2 000 681 B** (+13 568 B) |

Els +4 200 B de RAM són **+4 096 del buffer** i uns **+104 de l'IMU** (la taula i l'estat del
driver); el flash puja molt més perquè aquest torn hi entra també la feina de les plaques, de la
sessió `lua` i de l'app `tilt`. Un buffer més gran **no** vol dir que una app pugui ser tan gran com
vulgui: el que mana és la memòria de l'app, i el sostre és el dir quan una app no hi cap abans de
gastar un sol byte de heap.

## El sostre del codi d'entrada, pujat a 16 KB (2/11)

El sostre de `kSourceCap` era de **8 192 B** des del 31/10. L'exemple `buttons`, quan se li van afegir els
callbacks de veritat (un `on_click`, un `on_activate` i un `on_focus` per botó, amb els comptes al costat),
va arribar a **9 329 B** i el sistema va dir `too big to run: /apps/buttons/buttons.lua`. Un exemple que no
s'arrenca no és un exemple, i el que havia crescut no era farciment: era el que l'app ensenya. El sostre és
ara de **16 384 B**, i el preu és exactament el doble del buffer:

| | Amb el sostre a 8 KB (0.49.1) | Amb el sostre a 16 KB (0.50.0) |
|---|---|---|
| RAM estàtica | 116 020 B | **124 212 B** (+8 192 B) |
| Flash | 2 165 137 B | **2 165 137 B** (+0 B) |

El flash no es mou perquè el buffer és dades i no codi. **El que no canvia és el que mana**: el sostre diu
quan una app no hi cap *abans* de gastar un sol byte de heap, i el que limita una app de debò és la seva
memòria, no aquest número.

## El codi d'entrada passa al heap (2/11)

El buffer de 16 384 B era **estàtic**, i el 2/11 es va veure el preu: amb la identitat de la placa del
mínim a dins del projecte, `pio run -e atom_matrix` va deixar d'enllaçar amb

```
region `dram0_0_seg' overflowed by 1184 bytes
```

Aquest buffer es fa servir **un sol cop per arrencada d'app** i dura el que dura un `luaL_loadbuffer`, que
el compila i ja no el mira més. Tenir-lo reservat sempre és pagar setze kilobytes per un mil·lisegon;
l'`ATOM Matrix`, que és el pitjor cas, no els té. El que fa ara és **demanar-lo a l'heap a `load_source()` i
tornar-lo just després de compilar** (a `run_app` i a `run_window`, i també quan l'intèrpret no arrenca).

| | Buffer estàtic (0.55.0) | Buffer a l'heap (0.56.0) |
|---|---|---|
| RAM estàtica de l'ATOM | 123 392 B | **107 008 B** (−16 384 B) |
| RAM estàtica del Matrix | **no enllaçava** (1 184 B de més a `dram0`) | **109 376 B** |
| Flash de l'ATOM | 2 081 113 B | 2 081 225 B (+112 B) |
| Flash del Paper | 2 166 817 B | 2 166 893 B (+76 B) |

Els +112 B de flash són el `malloc`/`free` i les tres sortides que l'alliberen. **No contradiu la regla de
no posar buffers grans a la pila** (més amunt): la regla prohibeix la **pila**, no el **heap**, i un bloc
que es demana i es torna abans que l'app corri una línia no el paga ningú quan no s'usa. Si el `malloc`
falla, el sistema ho diu (`no memory for the source`) i no arrenca l'app, que és el que ha de fer.

## Un bloc que no creix no es queda sense lloc (4/11)

El heap del banc (`pal_host`, `HostExtraMemory`) té **dues cases** com la placa: la RAM del xip i la memòria
extra (la PSRAM del Paper). Un `resize` que no podia créixer on era provava l'altra casa i prou, i si el bloc
era a la PSRAM i el veí de darrere estava ocupat, deia que no — **amb quatre megabytes lliures a la mateixa
PSRAM**. Va sortir obrint el fitxer d'1,3 MB d'una pàgina del `http`: el `luaL_Buffer` de `fs.read_at` demanava
88 572 B i l'intèrpret deia `not enough memory` amb `home free 50 568 / extra free 4 056 272` a la mà.

Ara un `resize` que no cap on és **mou el bloc a qualsevol altra taca de la seva casa** (`move_into`), que és
el que fa un heap de debò: un bloc de vuitanta-vuit kilobytes no necessita el lloc just darrere seu, només
algun lloc. És un error del banc i no de la placa —l'`esp32` porta el seu propi `malloc`— i per això el que
canvia és el model, no el sistema (docs/85).

## El sostre del codi, a 32 KB (4/11)

L'editor que parla vim (`data/apps/edit/edit.lua`) fa **26 644 B** i el 4/11 el sistema li va dir `too big to
run: /apps/edit/edit.lua`: el sostre era de 16 384 B. El que havia crescut no era farciment —és un editor de
debò, amb desfer, cerca i les ordres de desament— i un editor que no arrenca no és un editor.

| | Sostre a 16 KB (0.56.0) | Sostre a 32 KB (0.65.0) |
|---|---|---|
| RAM estàtica | 107 008 B (ATOM) | **107 008 B** (+0 B) |
| Heap mentre l'app compila | 16 384 B | **32 768 B** |
| Heap mentre l'app corre | 0 B | **0 B** |
| Flash | 2 081 225 B (ATOM) | el mateix codi, +0 B de dades |

El buffer és al heap des de la 0.56.0 i **es torna just després de compilar** (`drop_source`), o sigui que el
que puja és un bloc transitori de 16 KB que dura el que dura un `luaL_loadbuffer`. El número que limita una
app de debò continua sent el seu heap (docs/75, més amunt), no aquest.

## La M5 Paper, amb la ràdio heretada (31/10)

`pio run -e paper` del 31/10, amb el que aquesta placa hereta del xip (`esp32_caps()`, `esp32_net()`,
docs/50):

| | |
|---|---|
| Flash | **2 108 973 B** de 4 194 304 (50,3 %) — la partició d'aplicació de `partitions_16mb.csv`, **mesurat amb la 0.13.0** l'1/11 |
| Flash lliure | **2 085 331 B** per al que vingui (el text i les fonts són el primer) |
| RAM estàtica | **94 188 B** de 327 680 (28,7 %); el build informa d'un 2,1 % perquè el denominador de la placa inclou els 4,5 MB de PSRAM |

**La bateria i el rellotge amb pila són el que l'ATOM no paga**; la mesura del despert, en canvi, és
codi de nucli i hi arriba. El de les dues primeres és del xip: `hal_esp32/battery.cpp` (l'ADC1 i
l'eFuse), `hal_esp32/bm8563.cpp` (el xip de la I2C), `hal_esp32/i2c.cpp` (el bus) i les corbes i el
calendari de `hal/`. Costat del mateix dia: **+5 276 B de flash i +48 B de RAM** per la bateria,
**+13 376 B i +152 B** pel bus i el rellotge (2 069 285 B i 92 972 B → 2 082 661 B i 93 124 B) i
**+692 B** per la mesura de l'últim despert, que és de nucli i per això **sí** que arriba a l'ATOM
(2 008 281 → 2 008 973 B). Les altres dues no hi arriben perquè cap crida les toca i
l'enllaçador no se les emporta.

**El preu d'heretar la ràdio, mesurat amb els dos builds del mateix dia**: el Paper sense ràdio era de
**553 665 B** de flash i **52 336 B** de RAM; amb `esp_wifi`, el provisioning, el NTP i
`esp_http_client` amb el paquet de certificats és de 2 083 369 B i 93 124 B, o sigui **+1 529 704 B de
flash i +40 788 B de RAM**. És el mateix bloc que l'ATOM ja pagava: el que costa no és la placa, és el
xip (docs/50).

## El preu de les tecles, mesurat (31/10)

Les tecles amb la seva descripció —quantes n'hi ha, en quina cara, en quin lloc, si són simples o
compostes i de quin grup— costen això:

| | Abans | Amb les tecles |
|---|---|---|
| `atom` | 2 000 681 B de flash / 92 456 B de RAM | **2 002 389** / **92 480** (+1 708 / +24) |
| `atom_matrix` | 2 000 673 B de flash / 94 824 B de RAM | **2 002 405** / **94 848** (+1 732 / +24) |
| `paper` | 551 201 B de flash / 52 248 B de RAM | **553 665** / **52 336** (+2 464 / +88) |

Els 24 B de RAM de l'ATOM són les taules per tecla del nucli (estat, `long` ja dit) i la descripció a
la capa de Lua; a la Paper s'hi afegeix l'estat de tres tecles en comptes d'una. El flash que creix és
el codi de la descripció i la crida nova, i no depèn de quantes tecles tingui la placa. **La fila del
Paper és d'abans de la ràdio**: el delta continua essent vàlid, i els totals d'avui són a la secció
del Paper.

## El preu del llum i del botó, mesurat (31/10)

| | Abans | Amb el llum i el botó |
|---|---|---|
| Flash | 1 971 281 B | **1 986 613 B** (+15 332 B) |
| RAM estàtica | 87 592 B | **88 152 B** (+560 B) |

Els +560 B són la política del llum (estat, color pres, el que s'està mostrant), l'estat del botó
(rebot, comptador de tocs, espera oberta) i les taules de Lua.

**I el llum només està encès quan diu una cosa que ningú no veu**: des del 31/10 la política té **dos**
estats (`kRadio` i `kOff`), i quan una app que havia pres el color marxa, el llum **s'apaga** i no
torna a un color d'abans. Un llum encès per ningú és justament el que aquest pressupost no vol.

**El llum també costa en corrent, i això és una nota per a M6**: un WS2812 encès deixa anar fins a
**~20 mA** a blanc ple i **prop d'1 mA quan està «apagat»** (corrent de repòs del xip, segons el full
de dades). Per això els colors de la política són fluixos i per això el llum es pinta **només quan
canvia**: un frame costa 24 bits a 1,25 µs = **30 µs** de CPU i una escriptura, no un bucle. **No s'ha
mesurat cap d'aquests dos corrents** i no s'inventa: cal el mesurador USB.

## El preu del son, mesurat (31/10)

El son és el que M6 porta: la política d'energia del nucli (`lib/kernel/src/power.cpp`), les dues
fondàries al HAL de l'ATOM, `power.*` a Lua i el camí per tornar d'una app.

| | Abans | Amb el son |
|---|---|---|
| Flash | 1 957 613 B | **1 971 281 B** (+13 668 B, amb el son i `power.cpu`) |
| RAM estàtica | 87 408 B | **87 592 B** (+184 B) |
| Partició de 3 MB | 62,2 % | **62,7 %** (lliures: 1 174 447 B) |

Els +184 B són el comptador de la feina d'energia i el que costa a la política saber si hi ha una app
esperant la resposta. **No hi ha cap buffer nou**: una nota de son són tres cadenes curtes a la NVS
(`session/app`, `session/path`, `session/wake_at`) i la marca de l'època viu a la memòria del RTC.

**El que queda per mesurar, i es diu**:
- **Consum en repòs i en actiu.** L'ATOM no té bateria i el consum es mesura amb un **mesurador USB**
  entre el cable i la placa; no s'ha pres cap número. El que sí que canvia el firmware: amb la ràdio
  apagada i el xip despert, el consum és el del `loopTask` a 240 MHz; en son lleuger el xip para i
  torna; en son profund només queda el RTC.
- **El preu de la velocitat.** El temps d'una app a 80 contra 240 MHz i els mA de cada pas: cal el
  mesurador USB i, per al temps, una app que miri `sys.time_ms()` al voltant del mateix bucle. No
  s'ha pres cap número.
- **Desviació de l'hora després de N hores en son.** La mesura la dona el propi sistema: `sleep` i,
  en tornar, `power: woke on the timer, ±N ms from the hour it was given`. El RTC de l'ATOM és el
  **RC intern**, que és el pitjor dels casos; el número és el que M6 ha de deixar escrit aquí.

## El preu del TLS, mesurat (31/10)

Baixar una pàgina vol dir `esp_http_client` + `esp-tls` + `mbedtls` + el paquet d'autoritats del
build. Costat amb el número davant, que és el que docs/90 demanava abans de decidir:

| | Abans | Amb TLS |
|---|---|---|
| Flash | 1 761 265 B | **1 957 613 B** (+196 348 B) |
| RAM estàtica | 84 408 B | **87 408 B** (+3 000 B) |
| Partició de 3 MB | 56,0 % | **62,2 %** (lliures: 1 188 115 B) |

Dels +3 000 B de RAM, **1 536** són l'adreça de 192 B que porta cada un dels 8 llocs de la cua
(`Request`), i la resta són el lloc de la feina i el que `mbedtls` té d'estàtic.

**El que no s'ha mesurat, i es diu**: el **heap durant l'apretada de mans** a la placa. El que se sap
sense mesurar-la és el terra: `CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=16384` vol dir buffers de 16 KB
d'entrada i 16 KB de sortida, i amb el Bluetooth a dalt queden **12 152 B** de heap (mesurat el 31/10).
O sigui que **TLS i BLE no poden coincidir**, i no és una opinió sinó una resta. Al prompt, amb el
Bluetooth a baix, hi ha **338 432 B** lliures, que és on la baixada viu.

La pila de la tasca de la baixada és de **10 240 B** (l'apretada de mans corre damunt d'ella).

## L'espera de les promeses, mesurada

`sys.delay(ms)` es compara amb el que diu l'app que ha esperat (ella mateixa ho mesura amb
`sys.time_ms()`), el 30/10:

| On | Demanat | Mesurat | Diferència |
|---|---|---|---|
| ATOM (`run tick`) | 500 ms | **501 ms** | +1 ms |
| ATOM (`run tick`) | 1000 ms | **1001 ms** | +1 ms |
| ATOM (`run wait`) | 1000 ms | **1001 ms** | +1 ms |
| Emulador (`run tick`) | 500 ms | **501 ms** | +1 ms |
| Emulador (`run wait`) | 1000 ms | **1001 ms** | +1 ms |

El mil·lisegon de més és la resolució del torn del nucli (`kernel::poll` espera fins al venciment i
després despatxa els esdeveniments de la consola abans de reprendre la tasca), no un error del
rellotge. Un `sys.delay` no serveix per comptar temps: per a això hi ha `sys.time_ms()` i les
alarmes (`60 · Temps i alarmes`).

El pic de les apps que esperen **a l'ATOM** és **13 402 B** (mesurat el 30/10). La diferència amb
`hello`, que no espera res, és de **2 505 B** a l'ESP32 i **2 921 B** al PC: això és el que costa
tenir promeses en marxa (la metataula de `Promise`, la taula de pendents, un userdata amb el seu
valor d'usuari i la taula de continuacions) més la promesa d'aquest `sys.delay`.

El mateix codi fa **14 364 B al PC i 10 897 B a l'ESP32** (`hello`) i **17 285 B al PC i 13 402 B a
l'ESP32** (`wait`): un 24 % menys a la placa, perquè al PC els punters fan 8 bytes i a l'ESP32 en fan
4. El número que compta per al pressupost és el de la placa; el del PC serveix per comparar canvis
sense flashejar.

Cent promeses seguides costen **11 353 B** a l'ESP32 i **18 236 B** al PC per sobre del punt de
partida, i el bucle triga **199 ms a la placa contra 115 ms al PC**: això és **1 ms de sobrecost per
torn** a l'ESP32 (100 esperes d'1 ms → 199 ms) contra 0,15 ms al PC. És el primer número que tenim
del cost d'un torn del nucli, i el que caldrà vigilar quan existeixi el pressupost d'instruccions.

**Des de la 0.58.0 el banc el paga** (`kTurnOverheadUs` = 850 µs, la diferència mesurada, després de
cada torn i fora de `--no-delay`), i des de la 0.60.0 **el venciment també es despatxa al torn
següent** (`kDispatchUs` = 1 000 µs sobre una espera que expira): **1 000 esperes d'1 ms triguen
2 107 ms al banc i 1 990 ms a la placa** (mesurat el 3/11, amb l'app `burst` estesa a mil), contra
els **1 064 ms** que triguen amb el model apagat. El 6 % de més és la resolució del despertar del PC
i no una decisió: el número que es desa és el de la diferència entre les dues bandes, i la placa té
el seu propi ±5 %. El que **no** es modela és la instrucció: no hi ha cap Xtensa al banc i el codi de
Lua es cobra a part (`--lua-cpu`, 60 ns per instrucció a 160 MHz, docs/85).

La columna «consum real» no s'inventa mai: es treu de `sys.heap()`, de l'ordre `mem` i del mapa de
l'enllaçador (`pio run -e atom -t size`), i s'apunta el número i el dia.

## L'assignador de Lua

És un embolcall de `malloc`/`free` que:

- Porta el compte del que ha donat i del que s'ha tornat.
- Compara amb el sostre de l'app **abans** de donar memòria: si el demanat no hi cap, falla.
- Guarda el **pic** (el màxim que s'ha fet servir) per poder dir «aquesta app arriba a X KB».
- No fragmenta a posta: no hi ha pools per mida ni res per l'estil. Si el pic d'una app és massa a
  prop del sostre, es baixa el sostre i es mira on peta.

`sys.heap()` dona `{free, used, limit, peak, extra_total, extra_free}`.

## La memòria extra: la PSRAM, i transparent per a l'app (31/10)

El Paper porta **8 MB de PSRAM** i la resta de plaques cap, i el que fa el sistema és el que faria
qualsevol persona: **els blocs grans hi van i els petits es queden a la RAM del xip**.

- **Qui decideix on viu un bloc és l'intèrpret i no l'app**: `kExtraThreshold` (4 096 B) a
  `lib/luabind/src/luabind.cpp`. D'aquí cap amunt, `pal::extra_memory()` dona el bloc de la PSRAM; per
  sota, de la RAM interna, que és més ràpida i on mil objectes petit fragmenten mil vegades menys.
  Una app no sap res de tot això: demana memòria i prou.
- **Si la casa preferida és plena, es prova l'altra abans de dir que no**. Un bloc pot viure a
  qualsevol de les dues, i la PSRAM plena no és un error de l'app.
- **El sostre d'una app al Paper és la PSRAM mapejada** (`heap_limit_kb` = 4 096 KB), perquè el que
  ha de protegir el sistema és la seva pròpia RAM, no la memòria que l'app té de sobres. Als ATOM el
  sostre continua essent el de per defecte (**65 536 B**), que és el que hi ha.
- **8 MB de xip, 4 MB de veritat.** El M5Paper porta 8 MB de PSRAM i l'**ESP32 només en pot mapejar
  4**. No és una decisió del sistema: és la manera com el xip adreça la memòria externa. La
  documentació de l'ESP-IDF ho diu amb aquestes paraules —«only the first 4 MiB can be used as such»
  i «the length of this region is the same as the SPI RAM size, up to the limit of 4 MB»— i, per a la
  resta, cal el `himem`: **bancs de 32 KB** que es mapegen quan es volen i que deixen els punters
  inútils quan es canvia de banc. Una taula de Lua que visqués allà es trencaria al mapejar el banc
  següent, i per tant **no es pot fer transparent**: el que és transparent és el que la CPU pot
  adreçar, i aix. no arriba enlloc (docs/90).
- **El sistema diu els tres números i no un de sol** (31/10), perquè el primer que es va veure a la
  placa va ser una contradicció: `mem` deia `extra memory 4067 of 8192 KB free`, que és una piscina
  de 4 MB comptada contra un xip de 8. `IPlatform::extra_memory()` torna ara
  `{chip_bytes, total_bytes, free_bytes}` —el que diu el full de dades, el que la CPU adreça i el
  que en queda— i `mem` ho escriu tot: **`extra memory  4067 of 4095 KB usable, of 8192 KB on the chip
  (psram)`**. A l'ATOM el xip és zero i la línia diu `none on this board`, que és la veritat del
  maquinari i no un error.
- **Mesurat a la placa amb la 0.7.1 (31/10)**, amb el prompt sol: `free heap 162 728 B`, `min free
  heap 156 648 B` i **`extra memory 4067 of 4095 KB free (psram)`**. Els 4 095 i no 4 096 són la
  comptabilitat del propi assignador (`heap_caps_get_total_size` descompta el que es menja la regió),
  i el sostre de l'app continua essent **4 096 KB**: una app que demani els 4 MB sencers morirà a
  4 095 amb un error de memòria de Lua, que és el que ha de passar i no un número arrodonit.
- **L'espai lliure el diu la placa**, no el PC: `IPlatform::extra_memory()` surt de
  `esp_spiram_get_size()` (el xip), `heap_caps_get_total_size` i `heap_caps_get_free_size` amb
  `MALLOC_CAP_SPIRAM` (el que es pot fer servir), i a la consola `mem` ho ensenya amb els tres
  números. **I des de la 0.58.0 el banc els compta de debò** (`pal::set_memory_bytes`,
  `pal::memory_left`): els dos casals del xip tenen el seu llibre —**338 864 B** de RAM interna als
  ATOM, **162 728 B** al Paper, i **4 095 KB** de PSRAM al Paper— i cada bloc que demana el sistema o
  una app porta **16 B de capçalera** (el casal i la mida) que també es descompten, com fa el
  `malloc` de la placa. Un bloc que no cap al casal que vol prova l'altre, i un que no cap a cap dels
  dos **es refusa**: és el que fa que una app massa gran mori al banc abans de morir a la placa
  (docs/85). El que no hi pot haver és el desgast dels blocs ni la fragmentació de debò.
- **Mesurat al banc amb els sostres posats (3/11)** —la prova que el refús és el de la placa i no el
  del PC—: una taula de **400 000 enters** (~6,4 MB) al Paper, amb el sostre de l'app ja al màxim
  dels seus 4 MB (`--mem-cap 8192` es refusa: «this board has 4096 KB of memory and no more»), mor
  amb **`lua: not enough memory`** i la PSRAM torna exacta (`extra free 4189168 B` abans i després,
  `lua peak 2125981 of 4194304 B`); i **60 000 enters** (~1 MB) a l'ATOM, amb el sostre al màxim
  dels seus 320 KB, moren igual amb el pic a **290 956 B** i el `free heap` torna a **338 864 B**. I
  `run heap` (60 000 enters, la mateixa taula) continua cabent al Paper i ensenya el llibre exacte:
  **4 189 168 → 3 140 576 → 4 189 168 B** de PSRAM lliure.
- **El heap és un heap i no un comptador** (3/11, 0.60.0): cada casal és **una regió de bytes tallada en
  trossos** de 16 B de capçalera, amb la llista dels lliures, primera concordança, partició del que sobra
  quan val la pena (48 B) i **fusió amb el veí lliure d'abans i el de després** en alliberar. Per això el
  **lliure total no és el que una pàgina pot prendre**: `mem` en diu el **bloc més gran**
  (`largest block`, el `heap_caps_get_largest_free_block` de la placa) i una petició que no hi cap es
  refusa amb el lliure de sobres. Mesurat: 16 blocs de 4 080 B en un casal de 64 KB deixen **vuit forats
  de 4 096 B** i una petició de **6 000 B es refusa** amb 32 KB lliures; i 64 blocs de mides que no
  lliguen, deixats anar mig i mig, tornen el casal als **131 072 B** exactes. El temps també es paga:
  **100 ns per node** de la llista que es recorre (fins a setze, que és el que costa la cerca del xip) i
  **20 MB/s** per cada bloc que canvia de casal i es copia.
- **Mesurat a la placa el 31/10**, amb el prompt sol: `free heap 162 728 B` i `min free heap
  156 648 B` de RAM interna (menys que a l'ATOM, que en té 338 864: el Paper paga la targeta, la I2C,
  el rellotge i les tres tecles).
- **Mesurat al banc el 31/10**: la mateixa app (una taula de 60 000 enters, ~1 MB) **no hi cap a
  l'ATOM** (sostre de 64 KB) i **hi cap al Paper** (sostre de 4 MB), i l'app només diu
  `sys.heap()` per ensenyar `limit 4194304  extra 4194304`. És la prova que la memòria és
  transparent: el mateix codi, la mateixa crida, i el que canvia és la placa.
- **El canvas de la vista és un bloc de debò, i el número és el de la comptabilitat**: la línia de
  l'arrencada diu avui `ui: view 540x912 at 0,48, 246240 bytes of canvas`, que és **540 × 912 / 2**
  —quatre bits per píxel— en **un sol bloc** de la PSRAM (el 1/11, amb la barra de 28 files, deia
  `540x932 at 0,28, 251640 bytes`). Els vuit canvas del sostre, si tots fossin de pàgina sencera, serien
  **1 969 920 B** (1 924 KiB) dels 4 096 que té una app: **gairebé la meitat**, i per això **cada control
  és de la seva mida** i un control **sense vista no gasta ni un byte** (docs/90, M9).
- **El que encara no està mesurat**: quants blocs reals van a la PSRAM en una app de debò i quants a
  la interna (`sys.heap().used` i el pic en surten, però el repartiment no es desa enlloc). Es mesura
  quan hi hagi una app que ho necessiti, amb `docs/95` al costat.

## Els comptadors de la porta (1/11)

La taula de comptadors és **fixa i petita a propòsit**: **quaranta-sis files** de vint bytes (comptes,
moment de l'última crida, microsegons acumulats, mínim i màxim), els noms al *flash* i una fila de
`stats` per cada una que s'hagi fet servir.
**Mesurat**: la imatge del Paper passa de **2 130 497 B a 2 139 573 B** de flash (**+9 076 B**, els
noms de les quaranta-sis files, els comptadors i l'escala de temps) i la RAM de **100 188 B a 101 396 B**
(**+1 208 B**). És el preu de saber què costa cada
cosa, i cap app el paga: la taula és del sistema i no del pressupost de l'app (docs/30, docs/95).

## El motor dels efectes del llum (31/10)

Els quatre efectes (`breathe`, `blink`, `sos`, `bounce`) i les dues capes no costen ni un byte
dinàmic: tot és estat fix a `lib/kernel/src/led.cpp`.

| Què | Com es compta | Quant |
|---|---|---|
| Els quatre efectes del sistema | `4 × sizeof(led::Effect)` | **96 B** (24 B cada un: mena, dos colors de rampa, període, cicles, llum de la tira i nivell) |
| La capa de l'app **i l'estat desat** per una interrupció | dos `Effect` més l'instant d'inici i el punt del cicle | **uns 90 B** |
| **Total** | **calculat amb `sizeof`**; el build de la 0.10.0 (1/11) hi suma **+312 B** de RAM per a aquest motor **i** el mòdul `env` | **uns 250 B** de RAM estàtica (els dos junts fan els +312 B de la taula de dalt) |

**I el que costa mentre corre**, que és el número que compta en una placa que vol dormir:

- un frame és **30 µs** al Lite i **750 µs** al Matrix, i el motor en demana un cada **33 ms** (uns 30
  per segon) només mentre té alguna cosa a dir. Al Lite són **0,09 % de CPU** i al Matrix **2,3 %**,
  calculat i no mesurat;
- quan no hi ha cap efecte ni cap moment, `next_frame_ms()` torna `-1` i el nucli torna a esperar el
  que esperava: **un llum que no es mou no manté la placa desperta**, i ho diu el codi i no la bona
  voluntat de qui el crida;
- mentre hi ha un efecte, la política d'energia **no deixa la placa apagar-se**: un efecte s'ha de
  pintar a cada torn, i una placa que s'apaga al mig d'una respiració és un llum que menteix
  (docs/60).

## Pressupost d'instruccions

`lua_sethook` amb comptador. Cada **torn** d'una tasca té un màxim d'instruccions; quan s'esgota, la
tasca torna a la cua i el nucli fa la seva feina (esdeveniments, alarmes, col·lector).

- Un torn no és un càstig: una app que fa un bucle llarg simplement es reparteix en torns.
- **Dos avisos seguits** vol dir que l'app no cedeix mai ni quan el nucli ho demana: es mata amb
  `E_BUDGET`.
- Dins d'una finestra d'alarma el pressupost és més curt que en primer pla.

## Col·lector

- Un pas incremental per volta del nucli, amb un pressupost de temps.
- Mai una col·lecció completa enmig d'una operació que l'usuari està mirant.
- `sys.gc("full")` existeix per a les proves i per als moments morts, no per a ús general.
- Una app no pot canviar els paràmetres del col·lector; si els vol tocar, és que té un problema de
  disseny que s'ha de mirar.

## La latència del port sèrie, mesurada

`tools/serial_ping.py`, 20 línies amb una d'escalfament, ATOM a 115200 baud, 30/10:

| Què | Valor |
|---|---|
| Mínim | **3,4 ms** |
| Mediana | **4,1 ms** |
| Màxim | **8,1 ms** |

De què es compon: la tasca lectora de consola mira el port cada **5 ms** (`kReaderPeriodMs` a
`pal_esp32.cpp`), i d'aquí surt la quantització —les mostres cauen en dos grups, ~4 ms i ~8 ms—. El
torn del nucli i la promesa hi sumen **menys d'1 ms**; la resta és el port i el pont USB.

La **primera** línia d'un port acabat d'obrir triga **400 ms**, sempre la primera i només una vegada:
és el primer enviament del port, no el sistema. Per això l'eina en descarta una d'escalfament; sense
això, la mediana surt bé igualment però el màxim enganya.

## La ràdio s'encén per feina, no per estat (31/10)

Una app **no** manté la ràdio encesa. El model és: hi ha coses a la cua → la ràdio s'aixeca; la cua es
buida → es tanca. I una tasca que necessita la xarxa (`net.ntp`) **se la porta ella**: es connecta amb
el que hi ha desat i fa la feina dins de la mateixa petició, amb dues passes (`needs_network` i
`on_the_network`).

Conseqüències que es veuen al port sèrie:

```
lunok> wifi start
wifi: connecting to MOVISTAR-A210-IOT
wifi: address 192.168.1.37
network: MOVISTAR-A210-IOT
lunok> wifi
network: MOVISTAR-A210-IOT
state: off                  ← la connexió va durar el que va durar la petició

lunok> ntp                  ← sense cap `wifi start` al davant
ntp: asking pool.ntp.org
time: 1793407338 (epoch seconds, from soc)
```

Si la placa no té cap xarxa desada, `ntp` no pot ni connectar-se i ho diu:
`ntp: the board has no network to use, try `wifi add``. L'apagada es fa al torn següent al buidar-se la
cua, perquè la contestació de la petició encara s'ha de poder llegir.

## Què compta i què no

| Compta al pressupost de l'app | No hi compta |
|---|---|
| Taules, cadenes, funcions, coroutines | Els buffers estàtics del nucli |
| Objectes de fitxer (petits) | Les finestres de lectura (són del pool) |
| Les promeses i les seves continuacions | La cua d'esdeveniments |
| Els trossos de text que torna `fs.read` | El buffer de la consola |

Si una app vol un buffer gran (una pàgina d'EPUB), el paga del seu pressupost i és el primer lloc on
mirar quan una app no cap.

## Com es mesura

1. `mem` al prompt: dona lliure, usat, pic i límit de cada app viva.
2. `sys.heap()` des de dins de l'app, en els moments que interessin.
3. `pio run -e atom -t size` per al consum de flash i l'ús de RAM estàtica.
4. A l'emulador, `--mem-cap=<KB>` per reproduir el sostre de la placa al PC i veure-hi el pic sense
   haver de flashejar.

## Quan no hi cap

- L'assignador falla, Lua llança `E_NOMEM`, l'app rep l'error i es tanca amb el missatge.
- No es mata una altra app per fer lloc: no n'hi ha cap altra.
- Si qui no hi cap és el nucli en arrencar, s'obre el prompt amb menys capacitats i es diu al log
  quina s'ha desactivat.
