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
| Heap mínim vist | **Mesurat a l'ATOM, 30/10** | **327 496 B** amb el prompt sol; **315 944 B** amb una app que espera; **301 640 B** amb una app que dibuixa, espera i llegeix |
| Sostre de la VM | `kDefaultHeapBytes` a `lib/kernel/src/shell.cpp` | **131 072 B** mentre `heap_limit_kb()` no digui un altre número (era 65 536 fins a la 0.81.2, vegeu la secció del terra) |
| Sostre per app | `IPlatform::heap_limit_kb()` | El de la VM (131 072 B) mentre la placa no en digui un altre |
| **Sostre del codi d'una app** | `kSourceCap` a `lib/kernel/src/shell.cpp` | **98 304 B** des de la 0.149.0, i era **65 536 B** des de la 0.143.0. És el sostre d'**un fitxer**: una app pot ser més d'un amb `require` (0.148.0) i cada mòdul té el seu bloc |
| **Terra de l'intèrpret** | Mesurat a l'ATOM de l'emulador, 4/11, amb `sys.heap()` en arrencar una app | **29 842 B** abans que l'app corri una línia: 13 021 de Lua i les seves llibreries, 13 200 de la llibreria d'UI (canvas, teclat, cares) i 3 600 dels altres globals. És el que una app **no** pot fer servir del seu sostre |
| **Memòria extra (PSRAM)** | `IPlatform::extra_memory()` (torna `{chip_bytes, total_bytes, free_bytes}`) i `pal::IExtraMemory` | **4 096 KB al Paper** (el xip en té 8 MB i l'ESP32 en mapeja la meitat) i res als ATOM. És el sostre d'una app en aquesta placa (docs/50) |
| Bloc «gran» que va a la memòria extra | `kExtraThreshold` a `lib/luabind/src/luabind.cpp` | **4 096 B**: d'aquí cap amunt el bloc viu a la PSRAM, i per sota a la RAM del xip |
| Pressupost d'instruccions | **20 000 per torn** de sèrie des de la 0.174.7 (eren 100 000, 0.90.0) i **ajustable** amb `cfg turn <n>`, de 2 000 a 200 000 | En esgotar-se, l'app fa `lua_yield` i el nucli recupera el torn; **no es mata**. És la latència del sistema, no el seu rendiment: un torn més curt és un dit que espera menys (docs/20, docs/30) |
| Sostre vist al **Paper** | **Mesurat a la placa, 31/10**, amb cent promeses seguides i `mem` | `heap used 37 883 B, peak 37 883 B`: la mesura és d'abans de la 0.7.0, quan la placa encara contestava el sostre per defecte. **Avui** `IPlatform::heap_limit_kb()` hi diu **4 096 KB** (la PSRAM mapejada) i la mateixa mesura dona `limit 4 194 304 B` |

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
| Lector del llistat de GitHub (`repository`) | 1 × `sizeof(Listing)` | **2 280 B** (mesurat amb `sizeof` el 4/11: finestra de 2 KB + 48 B de nom + 148 B d'adreça + el mànec). **És estàtic i és un de sol** (`Listing::one()`, 0.88.4): era una local de `entry_at` i dues de `print_listing`, o sigui fins a 4 560 B de pila en un camí que ja ve del shell amb la baixada a sobre, i la pila del `loopTask` fa 16 384 B (el QR de 4 096 B la va fer petar amb `canary watchpoint`, §«Pila») |
| Buffer del valor de `/nvs` (`kernel`) | 1 × 1 024 B | **1 024 B** (un valor sencer a la memòria: obrir un fitxer no pot reservar; docs/70) |
| Blocs dels pintors (`ui`) | `paint_row` 3 × 256 B i el títol d'una targeta 96 B | **864 B**. **El títol va néixer de 192 B i el link del Paper va dir que no**: `dram0_0_seg overflowed by 32 bytes` (0.189.102). Un bloc que es pot comptar és el que fa que el següent es mesuri abans de pujar-lo —i el d'un pintor no pot ser de pila, que un control es dibuixa dins del torn d'un altre— |
| L'últim error d'una app, copiat per a la pàgina (`luabind`) | un text per a la pàgina de l'error | **512 B**, i el Paper és qui va posar el número: amb els 640 que tenia de naixement el link va dir `dram0_0_seg overflowed by 128 bytes` (0.189.162), i la pàgina que no hi cap diu el que falta amb els seus tres punts. La primera versió de la pàgina guardava **les vint-i-quatre línies** ja partides (2 304 B) i el link del Paper va dir que no abans que cap prova: ara el text es passeja **dues vegades** —una per comptar les línies i una per pintar-les— amb una línia de 96 B a la pila i cap taula al `.bss` (docs/30, docs/50) |
| La política de la CPU i el seu sostre (`power`) | un `bool` i un `uint8_t` | **eren 8 B de `.bss` amb el seu farcit** i el link del Paper va tornar a dir que no (`overflowed by 8 bytes`, 0.189.107) el dia que hi va arribar el segon: ara són **un sol byte de bits** —els tres passos que la persona permet i, al quart, si la política és encesa— i les dues portes del `.bss` es paguen amb el títol d'un menú que baixa de 192 a 128 B. **Quan el `.bss` d'una placa ja és ple, un byte és un byte**; la mesura de l'error del linker és la que diu quant en falta, i no una estimació |
| El gràfic (`ui.chart`) | 48 valors de 2 B i 48 minuts de 4 B, a la **pila** de la crida i no al `.bss` | **288 B** (0.189.144): el pont passeja les dues taules de Lua al frame de `chart_draw` a **cada pintat**, i una sèrie de mostres no es fa mai un estàtic que la Paper hagi de fer cabre. El `.bss` d'aquesta placa ja és ple i una sèrie viu el que viu la pàgina que la dibuixa; el sostre de quaranta-vuit mostres és el que manté el número petit (docs/30, docs/45) |

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

Mesures de l'enllaçador a l'ATOM el 31/10 (`pio run -e m5-atom`), amb Lua 5.4.7, `run`, les promeses i la
baixada d'una pàgina amb TLS a dins, més el mòdul dels ajustos (`/cfg`), la reescriptura de `/res`, el
buitat de la consola abans de dormir i la mesura de l'últim despert: **+3 612 B de flash i +40 B de
RAM** sobre la mesura anterior del mateix dia (2 005 361 B i 92 624 B).

| Què | Valor |
|---|---|
| Flash | **2 020 769 B** de 3 145 728 (64,2 %) — la partició d'aplicació és de 3 MB des del 31/10 |
| Flash lliure | **1 124 959 B** |
| RAM estàtica | **93 096 B** de 327 680 (28,4 %) |
| **Mesurat amb la 0.10.0** (1/11, `pio run -e m5-atom`): és el total d'avui, i el que creix des de la columna del mig d'aquesta secció són el `/rtc`, el `/fram`, el `poweroff`, la memòria extra, els efectes del llum i el sensor d'ambient. L'`atom_matrix` dona **2 020 781 B** i **95 464 B** (29,1 %) | — |
| Pic d'una app que no espera res, a l'ATOM | **10 897 B** de 65 536 (assignador de Lua, **mesurat a la placa el 30/10** amb `mem` i una app de tres línies, amb la 0.10.0 i el terra de llavors). **Amb la 0.81.2 i el sostre de 128 KB, la mateixa app a l'ATOM de l'emulador dona 31 655 B de 131 072** — el terra de l'intèrpret és el que ha crescut, no l'app |
| Pic d'una app que no espera res, a l'emulador | **31 655 B** de 131 072 (mesurat el 4/11 amb la 0.81.2; eren **14 364 B** de 65 536 amb la 0.10.0, i el que ha pujat és la llibreria d'UI que tot intèrpret registra en arrencar) |
| Pic d'una app amb `sys.delay(1000)` a l'ATOM | **13 402 B** de 65 536 (**mesurat a la placa el 30/10** amb `mem`) |
| Pic d'una app que dibuixa i espera, a l'ATOM | **26 821 B** de 65 536 (**mesurat a la placa el 30/10**, dues vegades: 26 808 i 26 821); heap mínim vist, **301 640 B** |
| Pic d'una app amb `sys.delay(1000)` a l'emulador | **17 285 B** de 65 536 (el mateix codi al PC) |
| Pic d'una app amb **100 promeses seguides**, a l'emulador | **38 205 B** de 65 536 (100 × `sys.delay(1)`, 115 ms); abans del bucle, 19 969 B |
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

**I amb NimBLE, el mateix Bluetooth costa una tercera part** (0.190.179, l'amo: *«afegeix bluetooth al projecte»* i *«Segur que no te
nimble?»*). Mesurat a l'ATOM amb la taula de particions d'avui i el nucli sencer a sobre:

| | Sense ràdio | Amb NimBLE (BLE-only) |
|---|---|---|
| Flash | 1 516 693 B (57,9 % de 2 621 440) | **1 759 081 B (67,1 %)** — +242 388 B |
| RAM estàtica | 71 124 B | **88 456 B** — +17 084 B |
| Heap intern abans/després d'obrir el teclat | — | **169 364 B → 139 820 B**, o sigui **29 544 B** |

O sigui que la pila petita fa el mateix servei que la del fabricant per **+242 KB en lloc de +684 KB** de flash, i és el que fa que un
teclat de Bluetooth capiga en un xip sense PSRAM. **I hi cap amb marge** (mesurat a l'ATOM el 21/11, amb el nucli sencer carregat):

```
lunok> bt on
[I][BT] the keyboard is up as "lunokOS": 169364 B of internal memory before, 139820 B after (29544 B spent)
```

Els **43 156 B lliures** d'aquella nota del Wi-Fi eren d'un moment molt pitjor —amb la ràdio de la xarxa demanant la seva part— i no són el
pressupost d'aquest xip: el nucli en repòs en té **169 KB**, i el teclat en gasta **29 KB**. El que queda després d'obrir-lo és el que ha de
tenir una app que dibuixa, i per això el commutador (`cfg bt`) pot estar encès de sèrie (docs/50).

El Bluetooth **no s'encén a l'arrencada**: `bt_ready()` el puja quan algú demana
`provision` i l'esquema l'allibera en acabar (`WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM`).
Una placa que no es provisiona mai no paga la pila en RAM, només el codi al binari.

## El sostre del codi d'entrada, pujat a 8 KB (31/10)

`run` llegeix el fitxer Lua de l'app en un buffer estàtic abans de compilar-lo, i aquest buffer era de
**4 096 B** (`kSourceCap`). Una font d'onze rampes fa **4 407 B**: el sistema responia
`too big to run: /apps/matrix-app/matrix.lua`, que és un límit dient que no a una app que cap a la
placa. El sostre és ara de **8 192 B**, i es paga en RAM estàtica, que és el que diu la taula:

| | Abans | Amb el sostre a 8 KB |
|---|---|---|
| RAM estàtica | 88 256 B | **92 456 B** (+4 200 B) |
| Flash | 1 987 113 B | **2 000 681 B** (+13 568 B) |

Els +4 200 B de RAM són **+4 096 del buffer** i uns **+104 de l'IMU** (la taula i l'estat del
driver); el flash puja molt més perquè aquest torn hi entra també la feina de les plaques, de la
sessió `lua` i d'una app que llegeixi el sensor. Un buffer més gran **no** vol dir que una app pugui ser tan gran com
vulgui: el que mana és la memòria de l'app, i el sostre és el dir quan una app no hi cap abans de
gastar un sol byte de heap.

## El sostre del codi d'entrada, pujat a 16 KB (2/11)

El sostre de `kSourceCap` era de **8 192 B** des del 31/10. L'exemple `buttons`, quan se li van afegir els
callbacks de veritat (un `on_click`, un `on_activate` i un `on_focus` per botó, amb els comptes al costat),
va arribar a **9 329 B** i el sistema va dir `too big to run: <el camí de l'app>`. Un exemple que no
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
mínim a dins del projecte, `pio run -e m5-atom-matrix` va deixar d'enllaçar amb

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

## El sostre del codi d'entrada, pujat a 64 KB (8/11, 0.143.0)

El lector d'EPUB d'aquest sistema és **49 710 B** de Lua —el prestatge, la pàgina, el peu, els menús, l'índex i les
dues cares— i el sostre de 49 152 B li deia `too big to run` just quan va arribar el C14. El sostre és ara de
**65 536 B**, i el preu és el mateix d'abans i pel mateix motiu: el bloc és del heap i es torna abans que l'app corri
una línia (0.56.0), o sigui que **64 KB són 32 KB de heap mentre una app arrenca i res mentre corre**. El flash no es
mou.

El que **no** és: la decisió de docs/46 D6, que vol triar entre un sostre molt més gran i un `require` per a les apps.
El chunk del lector i el cost d'aquest pas a l'emulador **no s'han mesurat** i queden a docs/95.

## El bloc d'un mòdul (8/11, 0.148.0)

`require` llegeix un mòdul dins un bloc **de 65 536 B** (`kModuleCap`; el codi d'entrada d'una app té un sostre més
gran des de la 0.149.0, 98 304 B, i un mòdul no el té) i el
torna **abans que el mòdul corri una línia**: el chunk ja té les seves cadenes i el text no ha de viure mentre el
mòdul espera. O sigui que el cost és **un bloc gros mentre un mòdul s'està llegint** i res més: no hi ha un segon
sostre i no hi ha text de mòduls guardat al costat de l'app. Un text que omple el bloc es refusa (`E_TOOBIG`),
perquè un text que hi arriba pot ser un text que s'ha tallat.

## La cara d'un llibre, i els seus dos blocs (8/11, 0.150.0)

Una cara que el llibre porta dins seu demana **dos blocs temporals** i **un de permanents**, i cap dels tres és del codi
de l'app:

| Bloc | Mida | Quant viu |
|---|---|---|
| el fitxer de la lletra, com surt del zip | **128 KB** (`kFontFileBytes`, el sostre d'una lletra de llibre) | fins al `close` del llibre, i es torna a fer servir a cada `font` |
| el seu sfnt, quan era un `.woff` | fins a **512 KB** (`kFontSfntBytes`, el `totalSfntSize` de la capça) | ídem, i només s'obre quan un llibre en porta un |
| la cara que el sistema dibuixa | el fitxer sencer d'un TTF d'outlines, o la taula de glifs | mentre el llibre és obert: **és una de les tres places de cara de l'app** (docs/30) |

O sigui que un llibre amb una lletra de 60 KB en un `.woff` costa uns **60 KB de fitxer, 150 KB de sfnt i 150 KB de cara**
mentre és obert, i res quan es tanca: els blocs del llibre es tornen al `close` i la cara es dona amb `font:release()`
quan l'app obre un altre llibre o torna al prestatge. Al Paper (4 MB de memòria extra) és un terç del que hi cap; a
l'ATOM no hi cap i el refús és net —«there is no room for the face of the book»— i el llibre es llegeix amb la lletra del
disc.

## El lector en quatre fitxers, i el sostre a 96 KB (8/11, 0.149.0)

La decisió de docs/46 **D6** presa de les dues bandes alhora, que és com la va demanar l'amo:

- **el lector és quatre fitxers**: `reader.lua` (l'entrada i la pàgina: **1 203 línies i 49 215 B**, **101 locals** al
  chunk principal i 39 funcions), `chapters.lua` (**204 línies, 8 722 B**: el `container.xml`, l'OPF, el `nav` i el
  `toc.ncx`), `library.lua` (**68 línies, 2 865 B**: el prestatge i la posició de cada llibre) i `notice.lua`
  (**90 línies, 3 609 B**: la banda i la targeta de l'avís, amb el seu estat). Total **1 565 línies i 64 411 B**, i el
  fitxer més gran baixa de 60 930 a **49 215**;
- **i `kSourceCap` passa de 65 536 a 98 304 B**, perquè el que queda a l'entrada —la pàgina, el seu comptatge i el
  peu— encara és un chunk i les tres coses que vénen hi afegeixen línies. El marge d'un fitxer és ara **49 089 B**, més
  que tota l'app d'avui.

El preu és el mateix d'abans i pel mateix motiu: el bloc és del heap i es torna abans que l'app corri una línia, o
sigui que **96 KB són el bloc més gran que demana un arrencament d'app i res mentre corre**. La placa amb menys heap és
l'ATOM, amb **338 864 B** lliures mesurats, i el bloc n'és una quarta part durant l'arrencada.

**El que no s'ha partit, i per què**: la pàgina, el comptatge de pàgines, la tira de boles, el peu, les llistes i el
menú **comparteixen l'estat de l'app** (el llibre, el byte, la taula de pàgines, el compte corregut, el mode de les
tecles…), i tocar-los vol dir passar aquest estat a una taula que tots llegeixin i escriure `S.` a cada línia de
l'app. És un canvi que s'ha de fer **amb les proves corrent**, no a cegues, i per això queda com a feina pròpia si mai
cal: avui el marge és de 49 089 B i no cal.

## Un bloc que no creix no es queda sense lloc (4/11)

El heap de l'emulador (`pal_host`, `HostExtraMemory`) té **dues cases** com la placa: la RAM del xip i la memòria
extra (la PSRAM del Paper). Un `resize` que no podia créixer on era provava l'altra casa i prou, i si el bloc
era a la PSRAM i el veí de darrere estava ocupat, deia que no — **amb quatre megabytes lliures a la mateixa
PSRAM**. Va sortir obrint el fitxer d'1,3 MB d'una pàgina del `http`: el `luaL_Buffer` de `fs.read_at` demanava
88 572 B i l'intèrpret deia `not enough memory` amb `home free 50 568 / extra free 4 056 272` a la mà.

Ara un `resize` que no cap on és **mou el bloc a qualsevol altra taca de la seva casa** (`move_into`), que és
el que fa un heap de debò: un bloc de vuitanta-vuit kilobytes no necessita el lloc just darrere seu, només
algun lloc. És un error de l'emulador i no de la placa —l'`esp32` porta el seu propi `malloc`— i per això el que
canvia és el model, no el sistema (docs/85).

## El sostre d'una app, a 128 KB (4/11)

El sostre per defecte era de **65 536 B** des del començament, i el 4/11 va deixar de ser suficient **sense
que cap app hagués crescut**: el mateix `writer` que la prova escrivia sis fitxers de 15 000 B el 3/11 va
començar a dir `not enough memory` amb la llibreria de controls a dins, i l'editor (31 937 B de codi, 32 KB
de buffer d'entrada) no arrencava a l'ATOM. El motiu és el **terra**: cada intèrpret gasta **29 842 B** abans
que l'app corri una línia.

| | Amb la 0.67.0 (abans dels controls) | Amb la 0.81.2 |
|---|---|---|
| Terra de l'intèrpret a l'ATOM | uns 12 000 B | **29 842 B** |
| Pic d'una app que no espera res | 14 364 B de 65 536 | **31 655 B** de 65 536 |
| El `writer` de la prova (6 × 15 000 B) | passa | **`not enough memory`** |

El sostre és ara de **131 072 B**, i el número no és un gust: l'ATOM contesta **338 864 B lliures** (mesurat
amb `mem`, docs/95), o sigui que 128 KB és el 39 % del que hi ha i el sistema se'n queda 208. El Paper no hi
arriba mai —diu el seu propi sostre de 4 MB de PSRAM— i les plaques sense memòria extra (l'ATOM, la Matrix)
són les úniques que el fan servir.

**El que això no és**: un permís per gastar. El terra de 29 842 B és el que s'ha de mirar quan una app diu
`not enough memory`, i una app que en demana 128 ho ha de poder justificar amb una mesura com la d'aquest
paràgraf.

## El sostre del codi, a 32 KB (4/11)

L'editor que parla vim que hi havia llavors (`apps/edit/edit.lua`, que ja no és al disc) fa **26 644 B** i el
4/11 el sistema li va dir `too big to run: /apps/edit/edit.lua`: el sostre era de 16 384 B. El que havia crescut no era farciment —és un editor de
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

`pio run -e m5-paper` del 31/10, amb el que aquesta placa hereta del xip (`esp32_caps()`, `esp32_net()`,
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
esperant la resposta. **No hi ha cap buffer nou**: una nota de son són quatre cadenes curtes a la NVS
(`session/app`, `session/path`, `session/detached` —des de la 0.190.278, perquè una app que dormia al
fons torni al fons— i `session/wake_at`) i la marca de l'època viu a la memòria del RTC.

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
| ATOM (un `tick` de 500 ms) | 500 ms | **501 ms** | +1 ms |
| ATOM (un `sys.delay` de 1000 ms) | 1000 ms | **1001 ms** | +1 ms |
| ATOM (el mateix, una altra app) | 1000 ms | **1001 ms** | +1 ms |
| Emulador (un `tick` de 500 ms) | 500 ms | **501 ms** | +1 ms |
| Emulador (un `sys.delay` de 1000 ms) | 1000 ms | **1001 ms** | +1 ms |

El mil·lisegon de més és la resolució del torn del nucli (`kernel::poll` espera fins al venciment i
després despatxa els esdeveniments de la consola abans de reprendre la tasca), no un error del
rellotge. Un `sys.delay` no serveix per comptar temps: per a això hi ha `sys.time_ms()` i les
alarmes (`60 · Temps i alarmes`).

El pic de les apps que esperen **a l'ATOM** és **13 402 B** (mesurat el 30/10). La diferència amb
una app que no espera res, és de **2 505 B** a l'ESP32 i **2 921 B** al PC: això és el que costa
tenir promeses en marxa (la metataula de `Promise`, la taula de pendents, un userdata amb el seu
valor d'usuari i la taula de continuacions) més la promesa d'aquest `sys.delay`.

El mateix codi fa **14 364 B al PC i 10 897 B a l'ESP32** (una app que no espera res) i **17 285 B al PC i
13 402 B a l'ESP32** (una app que espera): un 24 % menys a la placa, perquè al PC els punters fan 8 bytes i a l'ESP32 en fan
4. El número que compta per al pressupost és el de la placa; el del PC serveix per comparar canvis
sense flashejar.

Cent promeses seguides costen **11 353 B** a l'ESP32 i **18 236 B** al PC per sobre del punt de
partida, i el bucle triga **199 ms a la placa contra 115 ms al PC**: això és **1 ms de sobrecost per
torn** a l'ESP32 (100 esperes d'1 ms → 199 ms) contra 0,15 ms al PC. És el primer número que tenim
del cost d'un torn del nucli, i el que el pressupost d'instruccions (aleshores 100 000 per torn, avui
20 000 i ajustable) ha de deixar passar entre torns.

**Des de la 0.58.0 l'emulador el paga** (`kTurnOverheadUs` = 850 µs, la diferència mesurada, després de
cada torn i fora de `--no-delay`), i des de la 0.60.0 **el venciment també es despatxa al torn
següent** (`kDispatchUs` = 1 000 µs sobre una espera que expira): **1 000 esperes d'1 ms triguen
2 107 ms a l'emulador i 1 990 ms a la placa** (mesurat el 3/11, amb el bucle de cent promeses estès a mil), contra
els **1 064 ms** que triguen amb el model apagat. El 6 % de més és la resolució del despertar del PC
i no una decisió: el número que es desa és el de la diferència entre les dues bandes, i la placa té
el seu propi ±5 %. El que **no** es modela és la instrucció: no hi ha cap Xtensa a l'emulador i el codi de
Lua es cobra a part (`--lua-cpu`, 60 ns per instrucció a 160 MHz, docs/85).

La columna «consum real» no s'inventa mai: es treu de `sys.heap()`, de l'ordre `mem` i del mapa de
l'enllaçador (`pio run -e m5-atom -t size`), i s'apunta el número i el dia.

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
- **I la RAM del xip té un terra des de la 0.151.19** (`pal::kHomeReserveBytes`, **64 KB**, i la regla
  `pal::take_from_the_extra()` a `lib/pal/include/pal/pal.h`, des de la 0.151.20): un bloc petit hi va
  **només mentre el que en queda després de donar-lo es quedi per sobre del terra**, i si no, va a la
  PSRAM. **La regla és del PAL i no del xip** perquè l'emulador l'ha d'obeir igual: `pal_host` enviava cada
  bloc petit a la casa que li demanaven i un test podia passar a l'emulador on la placa fallava. El llindar de 4 KB sol no n'hi havia
  prou: la màquina de Lua del lector demana uns **120 KB** en taules i cadenes petites, se'ls endú tots
  de la RAM del xip, i el sistema es queda sense el bloc que necessiten el `FILE` d'un fitxer i els
  buffers de DMA de la targeta. Mesurat a la placa: `mem` deia **125 464 B lliures (el tros més gran
  110 580 B)** amb el prompt, i després d'arrencar una app `fopen` responia `ENOMEM` amb 1,7 MB de
  PSRAM lliures, `ls /littlefs` no llistava res i una lectura de la targeta fallava amb un error de
  CRC (`sdmmc_read_blocks failed (257)`) fins a reiniciar la placa. Amb el terra, el lector obre els dos
  llibres i torna a desar la posició. **Els bytes lliures amb una app viva no estan mesurats** (el
  prompt no contesta mentre una app té la consola): el número de 64 KB és raonat i és una constant.
- **Els bytes lliures d'aquest terra es llegeixen cada 256 peticions i no a cada una**: passejar pel
  munt per a cada un dels milers de blocs que demana una màquina de Lua costaria més que el bloc, i un
  terra que es llegeix dues-centes cinquanta-sis peticions tard és igualment un terra.
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
  inútils quan es canvia de emulador. Una taula de Lua que visqués allà es trencaria al mapejar l'emulador
  següent, i per tant **no es pot fer transparent**: el que és transparent és el que la CPU pot
  adreçar, i aix. no arriba enlloc (docs/90).
- **I el `himem` ja es veu i ja es prova** (0.190.173, l'amo: *«Anem a activar el himem»*, amb
  l'exemple `system/himem` de la IDF al davant). No calia encendre'l: `CONFIG_SPIRAM_BANKSWITCH_ENABLE`
  és `default y` a la Kconfig de la IDF i era al `sdkconfig.h` des del primer build d'IDF de la Paper
  i del Koala; el que faltava era dir-ho i poder-ho comprovar. `IPlatform::extra_memory()` porta ara
  **`himem_bytes`**, **`himem_free_bytes`** i **`himem_window_bytes`**, `mem` escriu la línia d'himem
  sota la de la PSRAM, i **`mem himem` escriu i rellegeix 32 KB de dalt de tot** i respon `yes` o `no`,
  que és la mateixa pregunta que fa l'exemple de la IDF. Segueix **sense ser un heap**: el que la
  comanda demostra és que el maquinari hi és i que hi aguanta el que s'hi escriu, no que una app hi
  pugui tenir una taula (docs/50).
- **Mesurat a la placa el 21/11 (0.190.173, la Paper amb les apps i la targeta)**:
  **`extra memory 2940 of 4096 KB usable, of 8192 KB on the chip (psram)`** i, sota seu,
  **`himem 4352 of 4352 KB free, 256 KB of window (bank switching)`**. I els dos números no sumen el
  del xip: **4 096 + 4 352 = 8 448 contra 8 192**, o sigui que **les 256 KB de la finestra es compten
  dos cops**. La IDF comença l'himem a `4 MB − finestra` —«the reserved banks can't have an unity
  mapping to be used by malloc anymore; we treat them as himem instead»— i descompta la finestra de la
  regió del heap a `esp_psram.c`, **però el heap d'aquest build diu que en té 4 096**: el símbol fort
  `esp_himem_reserved_area_size` hi és al binari i la PSRAM el crida (comprovat amb `objdump` sobre el
  `firmware.elf`, no llegint el codi font). Amb la dada a la mà, **el que aquest sistema en diu i n'usa
  és el que queda després de la finestra**: `mem` escriu **`4096 of 4096 KB free`** i la prova comença
  al bloc 8 —`esp_himem_alloc` numera des del principi de l'himem, i els vuit primers blocs són els de
  la finestra— per no escriure mai dins del que el heap creu seu. Si algun dia el heap baixa a 3 840 KB
  tot sol, aquesta línia pot tornar a la de la IDF; el que es perd ara són 256 KB que igualment no es
  podien fer servir sense risc.
- **I qui hi viu són les cachés de les sessions** (0.190.174, l'amo: *«usa himem per les sessions»*). A session block
  of **32 KB or more** —the launcher's `apps` is exactly 32 KB— is no longer a pointer: `Block` has `bytes` or `banked`, and
  `store_read`/`store_write`/`store_zero`/`store_hash` are the only doors to its bytes, so the piece of the block that is
  being read is mapped in the window, copied and unmapped (32 KB at a time, with the region's own window of one block). The
  API of the app does not change, because it was already by offset: `block:read(at, n)` and `block:write(at, text)` are the
  same calls, and `cv:capture`/`cv:blit` —which do need a pointer— bring the piece to the extra memory, use it and give it
  back. The park to the disk also goes in pieces (`restore_block`, a 4 KB chunk at a time), so a two-megabyte cache never
  needs two megabytes of the board's own RAM. What changes is what the PSRAM keeps for: **the faces, the fonts and the
  pictures**, which is what it was put there for (docs/30, docs/50).
  **Una regió alhora**, perquè el xip té una finestra i no dues: el segon bloc gran que es demani es queda a la PSRAM (**no**
  falla, i la pàgina no ho nota), i el primer que la deixa la torna. I els blocs de menys de 32 KB segueixen sent punters, que
  és el que fa que una caché petita no pagui cap mapeig.
- **I la mida del xip torna a ser la del xip** (0.190.173): des de la migració a IDF (0.190.166) la
  pregunta es feia al heap, i `mem` deia `of 4096 KB on the chip` on el full de dades, aquest document
  i docs/95 diuen **8 192**. És `esp_psram_get_size()` (`esp_psram.h`) i el heap segueix responent el
  que s'adreça.
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
  números. **I des de la 0.58.0 l'emulador els compta de debò** (`pal::set_memory_bytes`,
  `pal::memory_left`): els dos casals del xip tenen el seu llibre —**338 864 B** de RAM interna als
  ATOM, **162 728 B** al Paper, i **4 095 KB** de PSRAM al Paper— i cada bloc que demana el sistema o
  una app porta **16 B de capçalera** (el casal i la mida) que també es descompten, com fa el
  `malloc` de la placa. Un bloc que no cap al casal que vol prova l'altre, i un que no cap a cap dels
  dos **es refusa**: és el que fa que una app massa gran mori a l'emulador abans de morir a la placa
  (docs/85). El que no hi pot haver és el desgast dels blocs ni la fragmentació de debò.
- **Mesurat a l'emulador amb els sostres posats (3/11)** —la prova que el refús és el de la placa i no el
  del PC—: una taula de **400 000 enters** (~6,4 MB) al Paper, amb el sostre de l'app ja al màxim
  dels seus 4 MB (`--mem-cap 8192` es refusa: «this board has 4096 KB of memory and no more»), mor
  amb **`lua: not enough memory`** i la PSRAM torna exacta (`extra free 4189168 B` abans i després,
  `lua peak 2125981 of 4194304 B`); i **60 000 enters** (~1 MB) a l'ATOM, amb el sostre al màxim
  dels seus 320 KB, moren igual amb el pic a **290 956 B** i el `free heap` torna a **338 864 B**. I
  la mateixa taula de 60 000 enters, quan cap, continua cabent al Paper i ensenya el llibre exacte:
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
- **Mesurat a l'emulador el 31/10**: la mateixa app (una taula de 60 000 enters, ~1 MB) **no hi cap a
  l'ATOM** (sostre de 64 KB) i **hi cap al Paper** (sostre de 4 MB), i l'app només diu
  `sys.heap()` per ensenyar `limit 4194304  extra 4194304`. És la prova que la memòria és
  transparent: el mateix codi, la mateixa crida, i el que canvia és la placa.
- **El canvas de la vista és un bloc de debò, i el número és el de la comptabilitat**: la línia de
  l'arrencada diu avui `ui: view 540x892 at 0,48, 240840 bytes of canvas`, que és **540 × 892 / 2**
  —quatre bits per píxel— en **un sol bloc** de la PSRAM (des de la 0.190.287; abans deia `540x912` i
  246 240 B, i el 1/11, amb la barra de 28 files, `540x932 at 0,28, 251640 bytes`). **El sostre de vuit canvas ja no hi és** (0.174.5): mentre hi era, vuit
  canvas de pàgina sencera haurien estat **1 969 920 B** (1 924 KiB) dels 4 096 que té una app —**gairebé la
  meitat**—, i per això **cada control és de la seva mida** i un control **sense vista no gasta ni un byte**, que
  continua sent el que mana ara que el número no hi és: el que es demana és el que es reserva i el que no hi cap
  ho diu (docs/90, M9).
- **El que encara no està mesurat**: quants blocs reals van a la PSRAM en una app de debò i quants a
  la interna (`sys.heap().used` i el pic en surten, però el repartiment no es desa enlloc). Es mesura
  quan hi hagi una app que ho necessiti, amb `docs/95` al costat.

## La memòria de sessió: el primer que es dóna quan falta memòria (0.179.0)

**Un bloc per (app, clau) que el nucli guarda i l'app torna a trobar**: `sys.session('apps', 32768)` és una caché que el
llançador construeix una vegada i troba a cada arrencada de l'app —que en una placa amb tres tecles és cada cop que una
app s'acaba— fins que la placa s'apaga o reinicia. Les claus són **de l'app**: el nucli sap qui corre (`luabind::app_id`)
i el bloc és una parella (app, clau), o sigui que `"apps"` no és el mateix bloc per a dues apps. Cada nom és de **setze
bytes** i el bloc es demana a la memòria extra amb `pal::extra_memory().allocate(bytes, true)` i es compta al pressupost de
l'app com qualsevol altre.

**I el nombre de blocs no té sostre** (0.189.36, l'amo: *«el número... ja haviem parlat que no vull límits artificials. Les
apps han de poder fer totes les sesions que vulguin. fins que la ram o el disc rebenti!»*): fins a la 0.189.35 era **una
taula de vuit files** (`session::kMaxBlocks`) i una app que en demanava nou rebia `nullptr` amb la placa buida —una galeria
de disset cares s'aturava a la vuitena i el registre deia `and the table of 8 is full`, que és un sostre que no té res a
veure amb la memòria—. Ara un bloc és un **node d'una llista**: **quaranta-vuit bytes a la RAM de la placa** (el punter, els
dos noms de setze bytes, el punter als bytes i la mida) que es fa quan l'app demana el bloc i es torna quan el bloc es torna.
El sostre és el que contesti l'assignador i prou: quaranta-vuit bytes són el que costava una fila de la taula vella, o sigui
que **cent sessions costen el que una cara d'un tipus de lletra** (4 800 B de nodes i els bytes de cada caché a la memòria
extra). Quan no hi ha lloc el registre ho diu una vegada —`there is no room for the row of a session`, o `there is no room`
per als bytes— i l'app segueix **sense caché**, que no és un error seu. La RAM de la placa **baixa 344 B** (124 572 →
124 228 B) perquè la taula de vuit files era estàtica i els nodes no existeixen fins que serveixen.

**I és el primer que es dóna quan una petició falla**, que és la part que fa que aquesta memòria sigui barata de tenir.
**Quan es decideix que falta memòria és una pregunta de l'amo** (0.179.6): *«Només s'ha de fer si falla una alocació de
memoria d'una app o sistema. (s'esborra i es reintenta de forma transparent per l'app)»*. I és exactament així: el PAL té un
punter de relleu (`pal::set_memory_relief`) que el nucli omple amb `session::release_all`, i **cada allocator el crida des
del lloc on la petició ja ha dit que no a totes les memòries i enlloc més** —mai per un llindar de bytes lliures i mai abans
de provar—, i tot seguit **torna a fer la mateixa petició**, o sigui que l'app que la va fer no arriba a saber que el preu
era una caché (el del PC i el del xip, amb el `resize` inclòs). Un bloc que no s'ha pogut donar mai no arriba al relleu,
i un que s'ha donat deixa la seva generació enrere, així que l'objecte de l'app que el tenia respon
`the session memory was given back` en lloc d'escriure a memòria lliure (docs/30, docs/90).

**I el preu el paga una app encara que la petició sigui d'una altra** (0.179.8, i això és el que es va veure al vidre): la
caché pot ser del propi llançador mentre demana una altra cosa —`[W][MEMORY] the memory is short: 1 block(s), 32 768 B
given back for an ask of 95 296 B`— i llavors l'app que la tenia **s'ha d'assabentar**. La transparència és de la petició i
no del bloc: `sys.session` torna un objecte que respon **`alive()`**, i la resta de crides d'un bloc que se n'ha anat
aixequen en lloc de llegir memòria lliure. Una app que pregunti abans d'usar-lo i en demani un altre quan la resposta és
`false` no perd la pàgina; una que no ho faci pinta l'error a cada dibuix, que és exactament el que va passar amb el
llançador fins a la 0.179.6 (`present: … [string "/apps/home_eink/home_eink.lua"]:248: the session memory was given back`,
quatre vegades, i una pàgina de lletres sense icones). El llançador ho fa així, i el torn següent torna a demanar el bloc
fora de qualsevol dibuix: amb els passos de sempre (32 768 → 24 576 → 16 384 → 8 192 → 1 544), o sigui que d'una placa
que encara va curta en surt un bloc dels noms, o cap, i la pàgina segueix sent una pàgina (docs/30).

**I el bloc que la petició està movent no es toca** (0.179.6). El relleu rep dues coses: **el bloc** de qui va la petició
—`nullptr` si és una reserva nova, el bloc si és un `resize`— i **quants bytes en volia**. Un `resize` que falla dona el
bloc a l'allocator perquè el mogui, i donar-lo abans al relleu seria deixar l'allocator movent un bloc que ja no hi és; el
relleu el deixa estar i ho diu: `[I][MEMORY] home_eink keeps 'apps': 32 768 B, it is the block that asked`. La resta de
blocs sí que hi van, i la línia groga diu per a què:
`[W][MEMORY] the memory is short: 1 block(s), 32 768 B given back for an ask of 1 024 B`.

**I cada bloc que va i ve ho diu al registre** (0.179.3), perquè una memòria que el sistema es pot quedar sense avisar és una
memòria que fa desaparèixer una pàgina i ningú no sap per què: `[I][MEMORY] <app> asks for '<clau>': N B, made` quan es fa,
`… and it has them` quan es troba, `… grown from M` quan creix, `[I][MEMORY] <app> gives back '<clau>': N B` quan és l'app
qui el dóna (`block:release()`), i quan és el sistema —el relleu de l'allocator— **una línia per bloc** amb el motiu
`the memory is short` i **una de groga amb el total i la petició que el va fer anar-hi**: `[W][MEMORY] the memory is short:
2 block(s), 1 081 344 B given back for an ask of 1 024 B` (0.179.6).
Una petició que no hi cap també es diu, amb `[W]`: `there is no room`, o `there is no room for the row of a session` quan
el que no hi cap és el node de quaranta-vuit bytes (0.189.36). **Ja no hi ha una segona manera de dir que no**: la taula de
vuit files va marxar amb la 0.189.36 i amb ella el `and the table of 8 is full`, que era un sostre que no tenia res a veure
amb la memòria (docs/30).

**Què s'hi guarda i a quin preu, mesurat a l'emulador**: el llançador hi deixa els manifests (títol, camí de la icona i
**el número de versió** des de la 0.189.22) i
**les icones ja descodificades** (72 × 72 a quatre bits són 2 592 B cada una, deu apps són 25 920 B i el bloc sencer
32 768), i la segona pàgina **no llegeix ni un fitxer**: el log diu `home: cache warm, 11 apps, 10 pictures` i no hi ha
cap `home: app` perquè la cua de manifests és buida, i el bolcat del vidre de la pàgina calenta és **idèntic** al de la
freda (1 377 píxels de diferència, tots dins de la barra del sistema, que és el rellotge). `mem` en diu quants blocs hi
ha i quants bytes: `session memory  32 KB in 1 block(s)`.

**I el número de versió cap al mateix registre** (0.189.22): el que el bloc desa d'una app és
`id<TAB>title<TAB>icon<TAB>slot<TAB>picture<TAB>version`, amb la versió del manifest al darrere, que és el que deixa saber
a la pàgina que la cel·la que té al bloc és d'una app d'abans. Un registre de la forma vella no s'ha de llegir a mitges, i
per això la marca del bloc puja (`HEC2` a la 0.189.22 i `HEC3` a la 0.190.279): la primera arrencada després d'un canvi de
format és una caché freda i prou (docs/30, docs/40).

**I la llista ja no té finestra ni cap sostre** (0.190.279, l'amo: *«home: too many apps for the cache of the session — No ho
entenc. Se suposa que no hi ha limits…»*): el bloc era `[8 B de capçalera][1 528 B de registres][la resta, imatges]`, i amb
trenta-dues apps el text en feia 1 659 i el llaçador **no escrivia res** —cada arrencada tornava a llegir els manifests i a
descodificar totes les icones—. Ara és `[8 B][imatges a llocs fixos][registres al final]`, la capçalera diu quants bytes fan
els registres i el pressupost és el bloc: amb trenta-dues apps la llista fa **808 B** (era 1 659, i 802 dels que hi havia eren
el camí absolut de la icona, que trenta-una de les trenta-dues apps diu igual) i el bloc de 32 768 en té 31 200 de sobres per
a les imatges. Si un dia no hi cap, `sys.session` deixa créixer el bloc i, si la placa no el pot donar, s'escriuen les línies
senceres que hi caben en lloc de no escriure'n cap (docs/30, docs/75).

**I l'avís de la llista d'apps costa vuit bytes de DRAM, comptats** (0.189.23): el primer intent guardava el nom de l'app
que havia canviat i el verb en un `struct` de 56 B, i l'enllaç de la placa va dir el que el pressupost no perdona —
`region 'dram0_0_seg' overflowed by 40 bytes`—. El que s'hi va quedar és un `uint32_t` amb el número de la llista i un
`uint8_t` amb el verb (ningú, a l'altra banda, necessita el nom: la pàgina torna a preguntar la llista sencera), i el nom el
diu el registre en el moment del canvi. Mesurat: la DRAM de la banda passa de **124 548 B a 124 556 B** i el flash de
2 414 569 B a 2 415 297 B (docs/30, docs/40).

**I el mateix llançador hi té un segon bloc, molt més barat** (0.189.13): amb `cfg sort on` la pàgina es dibuixa de la
més usada a la menys, i el recompte és **una clau pròpia** (`sys.session('usage', …)`) d'**una línia per app**,
`id<TAB>count` —una vintena d'apps són menys de 400 B—, que baixa de 1 024 a 512 o 256 B si el sistema no en té
tants, i si no hi cap sencera **es queden les més usades** i el registre diu quantes en queden fora. És el bloc que el
relleu de l'allocator es pot endur sense que la pàgina perdi res que no pugui tornar a comptar: una app que no té el
bloc es dibuixa en l'ordre de la porta, que és el que feia abans de l'ajust.

**I el prompt el llegeix sense ser-ne l'amo** (0.189.14): `apps` demana els bytes del bloc amb `session::copy` —la
memòria si hi és, el fitxer si no— i el que rep és **una còpia** i no un punter, perquè el relleu el pot donar en aquell
mateix torn; el que llegeix són 1 024 B i una taula de vint-i-quatre files a la pila (id de quaranta bytes i el número),
que és la mida que el bloc pot tenir amb els ids més llargs (docs/30, docs/40).

**I la galeria de tipus de lletra desa una mostra per cara** (0.189.26, i la mida de les 0.189.30 i 0.189.32): el títol d'una
fila de `fonts` és el nom de la font dibuixat amb la seva pròpia lletra, i el que es desa és **el dibuix i no la lletra** —una
cara ocupa un dels tres
llocalls de cares del sistema, i un prestatge de nou no en cap—. Cada bloc és **128 B de capçal i 16 384 B de píxels**
(16 512 B, demanats sempre de la mateixa mida perquè la mida d'una mostra només se sap després de dibuixar-la): el títol va
pujar a sis passes la 0.189.32 i una mostra d'un nom de 480 x 66 és 31 680 píxels, o sigui 15 840 B, i amb els 8 192 B de
la 0.189.30 una quarta part de les mostres no s'hauria desat mai. Una mostra que no hi cap **no es desa** i la fila es
dibuixa igualment: la caché és un estalvi i no una condició de la pàgina. **I la imatge que s'hi desa ha de ser una que el
canvas pugui tenir**: l'amplada es desa **parell**, perquè `ui.canvas` retalla una amplada senar i `cv:blit` llegeix les
files amb l'amplada que li diuen —un 315 demanat i un 314 de canvas són files de 157 bytes llegides com a 158, o sigui una
mostra que torna esbiaixada dos píxels per fila— (0.189.49, docs/30). El sostre és **la memòria de la placa i no un
número** (0.189.36): la taula de vuit blocs que deixava una galeria de més de **set** cares sense bloc ja no hi és, o sigui
que un prestatge de disset s'emporta disset mostres si la PSRAM les té, i **disset mostres són 280 704 B de PSRAM i els
mateixos 280 704 B de disc** quan es desen (docs/30).

**I EL BLOC D'UNA FILA QUE NO ES VEU ES TORNA** (0.189.35, l'amo: *«[W][MEMORY] fonts asks for 's12201f79': 16512 B, and the
table of 8 is full <- has posat un límit al número de sessions? I el tamany? també està limitat?»*): la primera resposta va ser
que el límit era el de la taula del nucli (vuit blocs) i la mida la de l'app (16 512 B demanats sempre de la mateixa mida);
la segona, la que va arribar amb la **0.189.36**, és que la taula no hi és i que el sostre és la memòria. El que sí que pot
triar l'app és **quin bloc es queda**, i ara el tria: quan `sys.session` diu que no, la galeria **torna el bloc d'una fila
que no és a la pàgina** (`sys.session_destroy` de la seva clau) i **torna a demanar** una vegada. No es perd res que una
persona pugui veure: el bloc és la **còpia que sobreviu a l'app** i el dibuix ja és al vidre, al llenç de la seva fila —el
que es perd és que el pròxim arrencament el torni a dibuixar amb la cara, que és el que costava la caché de desar—.
El registre diu `fonts: the block of <nom> is given back for this page` i, si la segona petició també falla, la fila es
dibuixa i l'avís del nucli es queda com a testimoni (docs/30).

**I només es demanen les mostres de la pàgina** (0.189.30, l'amo: *«Carrega de les sessions només els tipus de lletra que
et calen per aquesta pàgina. Si l'usuari passa pàgina ja carregaràs aquelles. No les carreguis totes de cop»*): les files
d'una pàgina del prestatge es fan buides i el títol —la còpia del bloc o la cara que s'obre i el dibuixa— es fa **quan la
fila és al vidre**. Un prestatge de nou cares ja no obre nou cares per ensenyar-ne tres: el que costa un gir de pàgina és
el que costen les seves files i prou.

### Desar-les abans de plegar, i l'ordre quan falta memòria (0.189.11)

**Un bloc que era a la PSRAM quan la placa s'apagava es perdia**: el disc només rebia el que s'escrivia en tancar una app
o quan el relleu de l'allocator se'l quedava, i un `poweroff` o un `reboot` amb una caché calenta la llençava. Ara hi ha
tres portes i totes tres diuen el mateix, bloc a bloc:

| Quan | Qui ho crida | Què diu el registre |
|---|---|---|
| `poweroff` | `shell::power_off` | `lua parks 'one': 1024 B to /session/lua/one in 13 ms (the board is going off)` |
| `reboot` | `shell::reboot` | `… (the board is restarting)` |
| `logout` d'un terminal virtual | `shell::close_console_session` | `… (the session of the console was closed)` |

I el resum darrere, que és la línia que una persona busca:
`sessions parked (the board is going off): 2 written, 0 already there, 3072 B in 2 block(s), at /session`. **Els blocs no es
tornen**: el run encara els pot llegir i el que marxa és la placa, no la taula.

### Un bloc que no ha canviat no es torna a escriure (0.189.53)

**Un desament es demana per quatre camins** —l'app s'ha acabat (cada tancament!), la placa se'n va, la memòria va curta i
una app dóna el bloc— i fins a la 0.189.52 **tots quatre escrivien el fitxer tant si un byte s'havia mogut com si no**. Un
llançador que desa la forma a cada moviment del dial i es tanca sense que ningú hagi tocat res escrivia el mateix kilobyte
a cada app que s'acabava, i el seu `apps` de trenta-dos kilobytes, a més. L'amo ho va dir així: *«Si es el mateix bloc, no
cal gravar-lo, només farem malbé abans la flash»*.

**La pregunta la contesta el contingut i no un senyal**: cada bloc porta una **petjada FNV-1a de 32 bits** dels bytes que
el disc té d'ell (`seen`), posta en néixer el bloc —el que s'ha restaurat, o els zeros quan no hi havia fitxer— i després
de cada escriptura. Un desament que troba la mateixa petjada **i el fitxer amb la mida del bloc** (`stat`) no escriu res:

```
[I][MEMORY] home_eink does not park 'apps': the 32768 B on the disk are the ones it has (the app has ended)
[I][MEMORY] home_eink ended: 0 session block(s) written to /session, 3 already there
```

- **Per què una petjada i no un senyal de «algú ha escrit»**: totes les apps d'aquest sistema desen la forma amb un
  `clear()` i un `write()` (docs/30), o sigui que un senyal diria «escrit» cada cop que la pàgina és la mateixa que la del
  disc. El número és el que són els bytes.
- **Per què també el `stat`**: un fitxer esborrat amb `sessions destroy`, o una targeta que ha canviat, no es pot confondre
  amb un desament fet. Un `stat` costa molt menys que una pàgina de programa (0,7 ms) i el que compra és que «ja hi és»
  sigui cert.
- **Quant costa mirar**: una kilobyte són uns microsegons i els trenta-dos kilobytes del llançador menys d'un mil·lisegon,
  contra els **11,2 ms** d'una pàgina de programa i els **45 000** del sector que s'esborra (docs/95). Mesurat a l'emulador:
  el segon tancament d'una app diu `0 session block(s) written, 1 already there`.
- **El que un xoc de la petjada costaria**: un desament que no es fa sobre uns bytes que no són els del bloc —un entre
  quatre mil milions—, o sigui una caché o una pàgina que es queda com era mitja passa enrere, i el desament següent ho
  arregla. El que no pot passar mai és perdre un canvi per haver escrit de més, que és el que passava abans.

I els dos números van separats a la línia (`Parked{written, unchanged}`) perquè **«no he escrit res» i «no hi havia res a
escriure» es llegeixen diferent en un registre**, i un `0 of 3` es llegiria com un fracàs.

**I el relleu té dos passos**, que és el que va demanar l'amo: les cachés de les apps que **no** són al vidre primer, i la
de l'app que hi és només si la primera passada no cobreix la petició. Mesurat: amb un bloc de 1 024 B de l'app de davant i
2 560 B de dues cachés d'una altra app, una petició de **512 B** s'aguanta **només** amb les de l'altra app, i l'app de
davant no paga res; una de **8 192 B** no hi cap i llavors sí que va al disc la de davant, que la tornarà a trobar quan la
demani (`restored … from the disk`). El bloc que la petició està movent no es toca mai (0.179.6) i la línia groga diu les
dues passades: `the memory is short: 2 block(s) of other apps (2560 B) and 0 of the app in front (0 B) given back for an
ask of 512 B`.

El PAL dona **un sol punter de funció** (`void (*)(void*, size_t)`), així que el nucli no hi pot passar l'id de l'app que
és al vidre: instal·la un pas intermedi —`relieve_memory` a `kernel.cpp`— que llegeix `luabind::app_id()` i crida
`session::relieve(current, keep, asked)`. `session::release_all` queda com la porta de sempre —«no hi ha cap app a
protegir, van tots»—, que és el que fan servir les proves i el cas d'una petició que falla sense app.

**`sessions` ensenya també el que només és un fitxer**: després d'un reinici, sense cap app que hagi demanat res, la fila
diu `disk only   /session/lua/probe` amb la clau i els bytes, que és la caché que tornarà la propera vegada que l'app
arrenqui. I **`sessions destroy [<id> [<key>]]`** esborra de la memòria **i del disc** —una memòria buida amb el fitxer a lloc és
un destroy que torna a la següent arrencada—; les tres formes són les tres preguntes: **`sessions destroy`** (tot),
**`sessions destroy <id>`** (tot el d'una app) i **`sessions destroy <id> <key>`** (una de sola, i les altres de la mateixa
app es queden); mesurat: `sessions destroy lua: 1 block(s) and 1 file(s), 4096 B` i `sessions destroy (every app): 0
block(s) and 2 file(s), 0 B`. **El mateix des de dins d'una app**: `sys.session_destroy(key)` fa la parella amb l'id que
posa el nucli, amb el mateix parell de meitats —el bloc de la memòria i el fitxer de `/session/<id>/<key>`, i la carpeta si
s'ha quedat buida— (docs/30).

### Escriure no és haver escrit: el recompte, el flush i el repòs (0.189.18)

**Un `poweroff` que va dir `2 of 2 block(s), 33792 B` en 119 ms i va tallar el corrent tot seguit** va deixar l'amo amb el
dubte raonable —*«Haurà donat temps a gravar-ho tot al SD?? s'ha apagat tot molt ràpid»*—, i el dubte és correcte: que
`write_block` torni vol dir que el sistema ha escrit, no que el medi tingui els bytes. La baixada de la placa té ara **cinc
passos i tots surten al registre**:

| Pas | Què fa | La línia |
|---|---|---|
| 0 | **l'app que és al vidre es tanca amb els seus comiats** (`on_pause`, `on_save`, `on_close`) i les seves cachés van al disc | `app closed` |
| 1 | la imatge del moment i **la consola buidada** (`console->flush()`), que és el buffer del port sèrie | `[I][SPLASH] /res/splash/poweroff.png, …` |
| 2 | s'espera la tinta (`splash::wait`), perquè el que una persona llegeix i el vidre diguin el mateix | — |
| 3 | **`session::park_all`**, un fitxer per bloc | `home_eink parks 'apps': 32768 B to /session/home_eink/apps in 106 ms` |
| 4 | **`session::check_parked`**, que és el que respon la pregunta: es fa un `stat` de cada fitxer i **es compara la seva mida amb la del bloc** | `2 of 2 session file(s) are on the disk` (i amb `[W]` si no) |
| 5 | **`power::settle_before_leaving`**: consola, **cada volum una vegada** encara que hi apuntin cinc portes (`/apps`, `/cfg`, `/fonts`, `/session`, `/nvs`), el magatzem de claus, i **el repòs** | `…: the console, 2 of 2 volume(s) and the store are written` i `a margin of 2000 ms before the current goes (a card finishes writing after the last sector)` |

- **El repòs és el que cap crida pot fer per la targeta**: el controlador d'una targeta copia el seu buffer propi a la
  memòria flaix uns mil·lisegons després que hagi arribat l'últim sector, i tallar el corrent al mig d'això és com es perd
  una carpeta. Són **2 000 ms** (`power::kLeavingMs`) i es poden canviar d'un lloc (`power::set_leaving_ms`), que és el que
  fa una prova per no esperar-los.
- **El `flush()` d'un volum diu la veritat de cada backend i no una crida decorativa**: a l'emulador el disc és una carpeta
  del PC i es fa un `sync()` de debò; **a l'ESP32 no hi ha cap `sync()` que es pugui enllaçar** (declarat a `unistd.h` i
  sense implementació —l'enllaçador ho va dir el 14/11—) i el backend contesta que no té res a mig fer, que és cert: cada
  fitxer s'obre, s'escriu i es tanca dins la mateixa crida, i littlefs i FatFs escriuen en tancar.
- **`can_power_off()`** (nou al HAL): la baixada pregunta **abans** si la placa té el corrent a la mà, perquè una placa
  alimentada pel cable no ha d'esperar dos segons per sentir que no es pot apagar. La Paper diu que sí, l'emulador diu el
  que diu el model, i l'ATOM que no.
- **El reinici passa pel mateix camí**: `reboot` també comprova els fitxers, renta els volums i reposa, perquè un reinici
  talla els busos dels volums igual que una apagada. **I el pas 0 també hi és des de la 0.189.54**: ni la baixada ni el
  reinici tancaven l'app, o sigui que una apagada amb una pàgina oberta se saltava el seu `on_close` —l'hora de `tz`
  s'escriu allà, i la forma d'una pàgina també— mentre que el son amb les tecles sí que la tancava (0.189.25). Ara les tres
  baixades fan el mateix: comiats, imatge, fitxers, repòs i corrent.

**I què és síncron, capa per capa** (la pregunta de l'amo: *«el poweroff respecta els temps necessaris… tot això és
síncron?»*). Sí, i cada cosa ho és per una raó que es pot dir:

| Capa | És síncron? | Per què |
|---|---|---|
| **littlefs** (`/apps`, `/cfg`, `/session` del flaix) | **sí** | Each file is opened, written and closed inside the call that writes it, and littlefs writes the block and its metadata on `lfs_file_close` — the call does not answer until the flash has it (`lib/hal_esp32/src/storage.cpp`, docs/70) |
| **La targeta** (FatFs) | **sí, i amb un asterisc** | `fclose` writes the data and the FAT entry and waits for its sectors; the asterisk is the **card's own controller**, which may still be programming its internal flash for a few milliseconds after the last sector — that is the 2 000 ms margin, which no call of the filesystem can replace (there is no `sync()` in this IDF to link against, measured on the board, docs/70) |
| **El FRAM** (`/fram`, I2C) | **sí** | Each write is one blocking `i2c_master_write_to_device` — the STOP is on the wire when it answers — and an FM24C02 is FRAM: the byte is stored by the end of the transaction, with no write cycle to wait for (`lib/hal_esp32/src/fm24c02.cpp`, docs/50) |
| **El magatzem de claus** (NVS) | **sí** | `Preferences::putBytes` commits the namespace inside the call, and `settle_before_leaving` asks `commit()` on the store as the belt to that (docs/70) |
| **El vidre** (la imatge del moment) | **sí, amb `splash::wait`** | A present is a queue: the ink is waited on before anything else happens, so what a person reads and what the panel has are the same thing (docs/50) |
| **El port sèrie** | **sí, amb `console->flush()`** | A UART keeps bytes in flight after a write returns, so the line is emptied twice: before the picture and in `settle_before_leaving` (docs/50) |

## La taula de canvas i la RAM interna (pendent, i és un risc)

**La taula de canvas no és una constant de codi: és mig megabyte demanat a la memòria extra** (0.102.0). A
`lib/kernel/src/ui.cpp`, `kSlotTableBytes` són **512 KB**, i `ui::boot()` en fa
`pal::extra_memory().allocate(kSlotTableBytes, true)` —una ranura per canvas, `sizeof(Canvas)` = **176 B** avui,
que són **2 978 ranures**— abans de fer la vista i abans que cap app corri. El log ho diu un cop:
`[I][UI] the table of canvases: 2978 slots of 176 bytes`.

**El parany és el `true` de la crida.** A `lib/pal/include/pal/pal.h`, `allocate(bytes, prefer_extra)` **prefereix
la PSRAM i, si no hi cap o no n'hi ha, prova la RAM del xip abans de dir que no**: «a block is a block and an app
does not have to know where it lives». O sigui que **en una placa sense PSRAM la taula sencera cau a la RAM
interna**, i la meitat gran del risc és que 2 978 × 176 B són **524 128 B**, més que els **338 864 B** que
l'ATOM diu que té lliures amb el prompt sol.

**El que passa llavors és la meitat bona**: el bloc es parteix per dos fins que l'assignador diu que sí, o sigui
que la placa no es queda sense pantalla i es queda amb la taula que hi cap. Les mides que prova són, dividint
per dos, **2 978, 1 489, 744, 372, 186, 93 i 46** ranures —**524 128, 262 064, 130 944, 65 472, 32 736, 16 368 i
8 096 B**—; el terra del bucle és `kMinSlots = 32` (5 632 B, que és una pàgina de trenta-un controls) i el bucle
s'atura abans, quan la meitat ja no hi arriba.

**I la meitat dolenta és que aquests bytes es paguen abans que res més** i surten del mateix lloc on aniran la
vista de l'app (a la Paper, 540 × 892 = **241 KB**) i el seu sostre. En una placa sense memòria extra, un
`130 944 B` de taula sobre 338 864 B és **el 39 % del que hi ha**, i el que queda ha de passar pel terra de
`kHomeReserveBytes` (64 KB) que protegeix els buffers del sistema (aquesta mateixa secció, més amunt).

**El que està mesurat i el que no:**

| Què | Número | Com |
|---|---|---|
| `sizeof(Canvas)` i les ranures | **176 B, 2 978 ranures** | `[I][UI] the table of canvases: 2978 slots of 176 bytes` a l'emulador, amb `--board m5stack-atom` i amb `--board m5stack-paper` (els dos donen el mateix, perquè la taula no depèn de la placa) |
| Quantes en rep una placa **sense** PSRAM | **no mesurat** | la línia de l'arrencada de l'ATOM és la que ho dirà, i `mem` després; cap build no ho ha llegit encara |
| Quantes en rep la Paper | **2 978** | cap dels 524 128 B no és a la RAM interna: la PSRAM els té de sobres |

**Des de la 0.179.9 això només és cert a les plaques amb vidre**: l'ATOM, el Matrix, l'ESP32 de DevKit i el T-Koala no
compilen `ui.cpp` i per tant no demanen cap taula (vegeu el capítol de sota, «La pàgina que una placa sense vidre no
compila»). El que queda pendent és la Paper, que sí que la demana i on el número és **2 978 ranures**.

**Què s'hi farà quan es mesuri** (i no abans, perquè el número ha de sortir de la placa): si l'ATOM es queda
amb 744 ranures o menys, la taula s'ha de dimensionar pel que la placa té i no per una constant —per exemple
`extra_memory()` quan el xip és zero, i `kSlotTableBytes` quan no ho és—, o baixar `kSlotTableBytes` a la meitat.
El que **no** s'ha de fer és llevar el `true`: sense ell, la Paper deixaria de tenir la taula a la PSRAM.

## La pàgina que una placa sense vidre no compila (0.179.9)

**La meitat del firmware d'un ATOM era codi que la placa no pot executar.** El vidre, el lector d'EPUB, les cares de la
lletra i les llibreries que els donen suport (`epub`, `zip`, `inflate`, `ttf`, `img`) hi eren perquè el nucli els demanava
a l'arrencada, i el que és pitjor: `ui::boot()` **demanava mig megabyte** de taula de canvas en una placa de 320 KB de RAM
—el bucle el partia per dos fins que hi cabia, i el log deia quantes ranures havien quedat—. Amb un sol
`LUNOK_GLASS 0` a `board/identity.h` aquestes quatre plaques no en compilen ni una línia (docs/50), i el que queda és el
sistema: consola, disc, VFS, xarxa amb TLS, alarms, llums, tecles, sensors i l'intèrpret.

**El que ha desaparegut del binari** (mapa d'enllaç de l'ATOM, `pio run -e m5-atom`, 11/11):

| Peça | Abans | Ara |
|---|---|---|
| `ui.cpp` | 52 482 B | — |
| `font.cpp` | 7 623 B | — |
| `book.cpp` | 8 664 B | — |
| `splash.cpp`, `bar.cpp`, `touch.cpp`, `display.cpp`, `orientation.cpp` | 7 964 B | — |
| `luabind.cpp` els enllaços de la pàgina i del lector | 107 688 B | **39 813 B** |
| `epub` + `zip` + `inflate` + `ttf` + `img` | 33 666 B | **0 B** |
| `qr` | 4 782 B | **4 750 B** (es queda: un QR s'imprimeix en text) |
| `no_glass.cpp` (les respostes curtes) | — | **189 B** |

**I el número que compta, `firmware.bin`** (11/11, les cinc plaques):

| Placa | Abans | Ara | Estalvi | Lliure a la ranura de 2 621 440 B |
|---|---|---|---|---|
| `m5-atom` | 2 302 464 B | **2 105 456 B** | −197 008 B (8,6 %) | de 318 976 a **515 984 B** |
| `m5-atom-matrix` | 2 302 576 B | **2 105 536 B** | −197 040 B | de 318 864 a **515 904 B** |
| `esp32-devkit` | 2 302 464 B | **2 105 456 B** | −197 008 B | de 318 976 a **515 984 B** |
| `ttgo-t-koala` | 2 378 240 B | **2 172 960 B** | −205 280 B | de 243 200 a **448 480 B** |
| `m5stack-paper` | 2 390 128 B | 2 390 336 B | **+208 B: és el text de la versió i de les dues frases noves del shell, i té vidre** | 231 104 B |

**I la RAM, que és el regal que no s'havia comptat**: sense `ui::boot()` la taula de canvas no es demana, o sigui que
l'ATOM no torna a provar de reservar 524 128, 262 064, 130 944… bytes per quedar-se amb la que hi cap (vegeu el capítol de
dalt, que queda **tancat per aquestes quatre plaques**: no hi ha taula perquè no hi ha pàgina). `mem` a l'ATOM diu `extra
memory none on this board` i el pic de l'intèrpret en arrencar una app baixa dels ~29 800 B de terra fins als **~13 000 B**
que és Lua i prou, perquè la llibreria d'UI no hi és.

**El preu és una frontera que les apps han de saber**: `ui`, `canvas`, `keyboard`, `font` i `epub` no existeixen en aquests
quatre builds i una app que els toqui peta a la primera línia (docs/30). És el que es volia dir amb «no fan falta»: no una
taula que contesta que no, sinó una taula que no hi és.

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

**Implementat en la seva primera meitat a la 0.90.0.** `lua_sethook` amb comptador cada 2 000
instruccions; cada **torn** de l'app té un màxim de **20 000 instruccions** (deu blocs) des de la
0.174.7, i abans 100 000. Quan s'esgota, el hook fa `lua_yield` i la tasca torna a la cua; el nucli
atén la consola, les alarmes, el botó, el vidre i la ràdio abans de reprendre-la.

**El número és un ajust del sistema i es va fer curt per una raó de latència, no de rendiment**
(0.174.7): la pregunta de l'amo va ser *«vull un sistema que respongui ràpid als botons i el touch»*, i la
resposta és que el pressupost és exactament el temps que un dit o una tecla poden esperar que el nucli
torni a mirar. Amb les 100 000 d'abans i el model de 60 ns eren **6 ms** de codi d'app entre dues mirades del
vidre; amb 20 000, **1,2 ms**. `cfg turn <n>` el canvia —de 2 000 (un bloc, el terra: per sota el comptador
no el veuria i no es tallaria mai) a 200 000 (12 ms, el sostre: més enllà el bucle és de l'app)—, el valor
es desa a `cfg/turn` de l'NVS com a text i `sys.turn()` el diu des de Lua. El que es paga és rendiment:
menys codi per torn i més torns, i per això el número és de la placa i no d'aquest document.

- Un torn no és un càstig: una app que fa un bucle llarg simplement es reparteix en torns.
- **No es mata l'app**: un `while true do end` corre per sempre, però la placa no es penja, i
  `!!exit`/3 ESC el tanquen.
- El yield només es fa si `lua_isyieldable` (fora d'una crida C); si no, s'espera el proper punt
  segur. El comptador tampoc veu el que passa dins d'una funció C, ni pot cedir dins d'un ganxo que
  el nucli crida amb `lua_pcall` (`on_click`, `on_alarm`, `on_draw`…).
- **Pendent**: els **dos avisos seguits** i el matar amb `E_BUDGET`, el pressupost més curt de la
  finestra d'alarma, el prompt `lua` (que fa `lua_pcall` i no és una coroutine) i les coroutines que
  crea l'app (un yield allà torna a `coroutine.resume`, no al nucli).

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
2. `mem blocks` al prompt: **la llista de blocs** —punter, bytes i qui l'ha demanat— i el resum per amo (§«Els llibres de la memòria»).
3. `sys.heap()` des de dins de l'app, en els moments que interessin.
4. `pio run -e m5-atom -t size` per al consum de flash i l'ús de RAM estàtica.
5. A l'emulador, `--mem-cap=<KB>` per reproduir el sostre de la placa al PC i veure-hi el pic sense
   haver de flashejar.

## Els llibres de la memòria (0.190.0)

**La pregunta de l'amo** —*«El sistema fa un seguiment de memoria que es demana i s'allibera? podríem fer una llista de punter,
tamany i qui l'ha demanat?»*— no tenia resposta fins avui. El que hi havia era tot el que és el bloc i res del de qui és:

| Què se sabia | Fins a quin nivell |
|---|---|
| `mem`: lliure, mínim i bloc més gran de cada casa | **totals**, cap bloc |
| `luabind`: pic i sostre de l'app del vidre | **per app**, cap bloc |
| `sessions`: amo, clau, bytes i on viu | **només** els blocs que una app demana amb `sys.session` |
| `meter`: els comptadors de la porta | **temps** de cada crida, no bytes ni amo |

Ara **cada bloc que travessa la porta de `pal::extra_memory()` s'apunta** en una taula amb el punter, els bytes i dues paraules: el
**mòdul** que l'ha demanat i **l'app que hi havia al vidre** en aquell moment. La marca és un objecte d'àmbit, com els comptadors del
port:

```cpp
pal::MemoryTag the_books(pal::kMemoryUi, shell::app_id());
```

i el que hi ha a sota —`ui` demanant un canvas, el llibre demanant el seu capítol, l'intèrpret demanant la seva taula— queda apuntat
amb el nom del de dalt, perquè el nom més interior és el que mana. Els amos d'avui: `kernel`, `shell`, `ui`, `session`, `book`,
`splash`, `lua`, `meter`, `net`, `board` i `other`; les paraules al *flash*, com les dels comptadors.

**On viu la taula i quant costa.** És **un bloc de vuit kilobytes de la memòria extra** (la PSRAM del Paper) i, en una placa que no
en té, de la RAM que li queda; **no és cap variable estàtica**, i això no és un detall: el Paper té la DRAM plena fins a l'últim
byte i la primera versió d'aquesta feina no hi va cabre (l'enllaçador va dir *«region `dram0_0_seg' overflowed by 24 bytes»*).
Qui la dona és la PAL, que és l'única que sap quines cases té la placa, i la taula li torna a preguntar a cada petició a través de
`IExtraMemory::books()`; els llibres no tenen ni un punter propi. La taula s'obre amb **la primera marca** —o sigui amb la primera
funció del sistema que diu qui és— i no amb el primer bloc: així el que un run té lliure abans d'això és el que la placa té.

**Un quart de la taula és dels blocs grans.** Una màquina de Lua té milers de blocs petits vius i uns quants de grans, i una taula
que es dona a qui arriba primer s'omple de petits i perd exactament el que una persona ha vingut a veure. Un bloc de **4 KB o més**
pot fer servir les files reservades encara que la resta sigui plena; els petits no les toquen mai. **I el que no hi cap es compta**:
`mem` diu `198 block(s) of 255 rows, 3256 ask(s) not written down`, o sigui que una llista que no és tota ho diu on es mira.

**La llista, tal com surt a l'emulador** (Paper, el llançador al davant, després d'`mem blocks`):

```
  198 block(s) written down in 255 rows, 3256 ask(s) did not fit
  who                 blocks        bytes
  ui                      3       779873 B
  session (home_eink)      1        32768 B
  lua (home_eink)       194        28764 B
  the blocks, the biggest first:
       524216 B  0x7702b2404510  ui
       246240 B  0x7702b24844d8  ui
        32768 B  0x7702b24c75b8  session (home_eink)
         9417 B  0x7702b2402030  ui
         8192 B  0x7702b2e42548  lua (home_eink)
  ... and 186 more; the smallest is 192 B
```

Les tres files del davant són la pàgina del llançador sencera: **el canvas de 540×892 (524 216 B), la vista (240 840 B) i la caché
d'`apps` (32 768 B)**, i totes tres amb `ui` i `session` com a amo. És la resposta que es volia: qui té els megabytes.

**El preu, mesurat abans d'escriure'l** (emulador, `--run about`, dotze segons): **24 070 peticions de menys de 256 B, 478 de
256 B a 4 KB i 28 de 4 KB o més**. O sigui que el que importa són centenars de blocs i no desenes de milers, i la cerca d'una fila
per petició no es nota. El que sí que es nota és el que la taula **no** pot tenir: els blocs petits de Lua són milers i la taula en
guarda el que hi cap —i ho diu.

**Un bloc que es mou no canvia d'amo**: un `resize` actualitza el punter i els bytes de la seva fila i deixa el nom com era, perquè
un `resize` no és una petició nova sinó el mateix bloc que ha canviat de mida. **I un `resize` d'un bloc que no hi era és un bloc
nou**: `realloc(NULL, n)` és com l'intèrpret demana un bloc que no ha tingut mai, i buscar una fila amb un punter nul trobava **la
primera fila lliure** —que és de ningú— i deixava el bloc sense amo per sempre. Es va veure a l'emulador amb dues-centes quaranta-nou
files `nobody` i totes les de Lua amb amo; ara hi ha un cas a `test_pal` que ho guarda.

## Quan no hi cap

- L'assignador falla, Lua llança `E_NOMEM`, l'app rep l'error i es tanca amb el missatge.
- **Els últims 256 B del sostre no són de l'app** (0.156.0, `luabind::kErrorReserve`): Lua **no pot llançar
  `E_NOMEM` si no té amb què escriure el missatge** —la cadena `not enough memory` demana uns quants bytes i
  l'assignador diu que no— i aleshores no llança res: **avorta el procés** (`luaD_throw` sense ningú que el
  reculli, i després `abort()`). Es va veure a l'emulador el 8/11 (un `SIGABRT` de la suite) i a la placa hauria estat un
  reinici del nucli enmig d'una app que només demanava massa. La reserva és el que el missatge necessita i l'app no
  la veu mai com a seva: `sys.heap().limit` segueix dient el sostre i el que queda per gastar és 256 B menys.
- **El fil d'un chunk també es demana protegit** (0.156.0): `lua_newthread` fora d'un `lua_pcall` és un sostre que
  diu que no i un procés que se'n va; ara el fil es crea dins una crida protegida i el refús és un `E_NOMEM` que
  l'app llegeix (`luabind::make_the_task`).
- No es mata una altra app per fer lloc: no n'hi ha cap altra.
- Si qui no hi cap és el nucli en arrencar, s'obre el prompt amb menys capacitats i es diu al log
  quina s'ha desactivat.

## El llistat del repositori, d'una passada (4/11, 0.88.5)

`kernel::repository::entry_at(path, index, …)` contesta un índex **rellegint el document des del
principi**: un `open`, una passada per la finestra de 2 KB i una parsejada, per a cada element que se
li demana. `repo install` el cridava un cop per **fitxer de l'app**, o sigui que una app de vint
fitxers pagava vint obertures i vint passades sobre el mateix document de ~44 KB: O(n²) en el nombre
d'entrades.

Ara l'install **camina el llistat obert**: `repository::start` obre el document, `next` torna
l'entrada següent (el lector comparteix el mateix objecte que `entry_at`, que és l'estàtic de 2 280 B
de §«Consum estàtic») i `stop` el torna al backend. L'índex `g_install_entry` ha desaparegut: el
lector ja sap on és.

**Mesurat a l'emulador** (`test_a_listing_is_walked_once_and_not_once_per_entry`, 40 entrades d'uns cent
bytes cadascuna, el relleu de la resposta real de l'API):

| Manera | Cost |
|---|---|
| Una crida a `entry_at` per entrada (la d'abans) | **3 131 µs** — 40 obertures i 40 passades |
| Un sol passeig amb `start`/`next` | **110 µs** — 1 obertura i 1 passada |

Són **28 vegades** menys en aquest document, i el guany creix amb el nombre d'entrades perquè una
manera és O(n²) i l'altra O(n). El número és del PC i no de la placa: a la flash, on una lectura de
4 KB costa 819 µs, el que compta és la quantitat de passades, i aquesta és la que ha canviat.

## Una pàgina de la xarxa no es menja la memòria del handshake (7/11, 0.97.1)

**El que va passar a la placa, amb el registre a la mà.** A l'M5Paper, `repo` va respondre:

```
E (719547) esp-tls-mbedtls: mbedtls_ssl_setup returned -0x7F00
E (719547) esp-tls: create_ssl_handle failed
[W][HTTP] could not open https://api.github.com/repos/lunokjod/lunokOS/contents/apps (ESP_ERR_HTTP_CONNECT)
```

`-0x7F00` és **`MBEDTLS_ERR_SSL_ALLOC_FAILED`**: la biblioteca diu que **no ha pogut tenir un bloc**, i no té res a
veure amb el certificat, amb el DNS ni amb la xarxa — que ja havien anat bé. A l'emulador la mateixa ordre baixa seixanta
carpetes perquè un PC té tota la memòria que vulgui, i per això l'emulador no ho veia.

**D'on surt el número.** Un handshake de TLS demana a la **RAM pròpia del xip** uns **quaranta kilobytes en peces**
(16 KB de buffer de lectura, 16 KB d'escriptura, la sessió i els transformats del xifrat). I abans del handshake,
aquesta funció demanava **la pàgina sencera** a la mateixa RAM: `kHttpBytes` del shell són **16 384 B** en un sol
bloc, més el `malloc` del client. Amb els dos comptats, al Paper no hi arribaven.

**La regla, que és la que l'intèrpret ja feia servir** (`counting_alloc`): **un bloc gran va on hi ha lloc i els
petits es queden a casa**. La pàgina d'una descàrrega va ara a la **memòria extra** de la placa
(`pal::extra_memory().allocate(bytes, true)`, PSRAM al Paper) i només cau a la interna quan no n'hi ha. Els blocs
petits de Lua ja ho feien amb `kExtraThreshold`.

**I la petició diu els números**, abans d'obrir i quan no pot obrir:

```
[I][HTTP] 16384 B for the page, 96 000 B of RAM free (40 000 in one piece), 4 067 000 B of extra
```

Si una placa torna a dir `-0x7F00`, la línia de dalt diu **quant** hi havia i **si era en una peça**: `free_bytes` és
el total i `largest_free_bytes` el bloc més gran, que és el que un `malloc` pot donar de debò.

**I el ganxo de mbedTLS.** El nucli d'Arduino porta `MBEDTLS_PLATFORM_MEMORY` activat, o sigui que la biblioteca demana
la seva memòria per `mbedtls_platform_set_calloc_free()`. La petició instal·la els ganxos abans del primer client:
**un bloc de 4 KB o més va a la memòria extra** i els petits es queden a la RAM del xip. Els dos buffers de setze
kilobytes del handshake són el bloc més gran que demana aquest firmware i ara viuen a la PSRAM. La PSRAM no és DMA i
no cal que ho sigui: aquests buffers els camina el processador, i la ràdio té els seus.

**Mesurat a la placa després del canvi (7/11, M5Paper):**

```
[I][HTTP] 0 B for the page, 28944 B of RAM free (26612 in one piece), 3650839 B of extra
repo: 61 folders in lunokjod/lunokOS/apps (40251 B of JSON)
repo: the answer is in /sd/http/apps
```

O sigui: **amb els mateixos 28 944 B de RAM interna** que abans fallaven, ara el handshake passa, perquè els seus
blocs grans ja no surten d'aquí.

## La pila dels descodificadors d'imatge, i la pàgina que va petar (0.118.0)

L'amo va portar el log de la placa: una pàgina amb tres fotos i

```
***ERROR*** A stack overflow in task loopTask has been detected
```

La pregunta era directa —«el carregador d'imatges usa PSRAM?»— i la resposta honesta és **sí i no**. El fitxer i el
bloc de treball ja venien de `pal::extra_memory()` (la PSRAM, docs/75 «La memòria extra»), però **l'estat propi de
cada descodificador era dins del descodificador**: taules i files en variables locals, que vol dir a la pila de la
tasca que corre l'app. L'emulador no ho ensenya mai perquè la pila del PC és gran; la tasca de l'app **diu quanta
n'hi ha lliure en arrencar** (`stack 7320 B free`) i el GIF en demanava el doble i mig.

**Mesurat amb `-fstack-usage`** (`python3 tools/check_stack.py`, 0.118.0, abans i després):

| Funció | Abans | Després |
|---|---|---|
| `decode_gif` | **16 656 B** | **256 B** |
| `decode_jpeg` | **6 432 B** | **912 B** |
| `jpeg_info` / `jpeg_scratch` | **3 504 B** | **90 B** |
| `decode_png` | **2 320 B** | **256 B** |
| `decode_bmp` | **2 288 B** | **240 B** |
| `decode_ico` | **2 288 B** | **208 B** |
| `decode_pnm` | **2 256 B** | **208 B** |
| `inflate::raw` (dins d'un PNG) | 224 B | 224 B |

- **On van anar els bytes**: al bloc que el cridador ja reservava, al darrere del que la imatge demana. Cada família
  té ara una funció que diu **on** comença el seu estat dins del bloc (`gif_state_at`, `jpeg_state_at`,
  `png_state_at`, …) i una altra que diu **quant** en fa (`*_state_bytes()`), i la suma entra a `scratch_bytes()`. El
  contracte és el de sempre i ara es compleix: **el número que respon `scratch_bytes` és el que el descodificador fa
  servir, i un bloc més petit es refusa amb `kNoRoom`** (`test_a_block_that_is_smaller_than_the_picture_needs_is_refused`).
- **La fila també hi va**: els sis descodificadors construïen la fila que donen al `RowSink` en una local de
  `(kMaxSide+1)/2` —2 048 B, la pàgina més ampla possible—. És al bloc, darrere l'estat, alineada a quatre bytes.
- **I el GIF va deixar de copiar una fila**: la fila comprimida que sortia del LZW era **la mateixa** que la que
  s'escrivia al bloc, així que la còpia a una local de 2 048 B va desaparèixer.
- **I l'arrencada diu la pila a cada passa** (0.190.290, l'amo: *«posa les linies, son informatives»*): `[I][STACK] the kernel starts`,
  `the view is up`, `the first page is up` i `the first turn`, amb els bytes que queden a la tasca. **Les quatre surten a totes les bandes
  perquè són del registre del sistema**: la 0.190.290 en tenia una cinquena per `ESP_LOGI` («the stack after the pal») i a la Paper no
  s'hauria vist mai —aquesta placa té la IDF a `WARN` a posta des de la 0.190.167, que els seus avisos de cada pin caurien al mig del
  registre que una persona llegeix (l'amo ho va veure: *«el codi del M5 no ho te!»*)—, o sigui que el número d'abans del nucli és el
  primer d'aquests quatre. **I surten al cable encara que el mirall del registre sigui tancat** (`log_bootf`, 0.190.292): el cable no
  rep el registre des de la 0.190.185, però aquestes quatre diuen un moment que passa una vegada i el cercle de 2 KB ja no les té quan
  una persona escriu `log`. El número és el *high-water mark* de FreeRTOS —el mínim que ha quedat lliure mai—, o sigui que la sèrie diu
  **quina passa cava** els kilobytes i permet comparar dues bandes al mateix moment de la vida de la sessió; dues lectures de moments
  diferents no es poden comparar, perquè el número només baixa. Les dues bandes tenen la mateixa pila (`CONFIG_ESP_MAIN_TASK_STACK_SIZE`
  a 16384, comprovat al `sdkconfig.h` compilat de cada entorn): el que canvia és fins on arriba el codi de cada placa (docs/50, docs/75).
- **El sostre que ho vigila**: `tools/check_stack.py` compila una llibreria amb `-fstack-usage` i falla si una funció
  passa del pressupost (1 024 B per a `lib/img`, que és el que crida la tasca de l'app). Amb `--all --budget 4096`
  dona el mapa de tot el firmware i les llibreries que necessiten les capçaleres de la placa es **diuen saltades** i
  no es mesuren (docs/80). El pitjor marc de tot el projecte avui és `kernel::shell::execute`, 2 272 B, a la tasca de
  la consola.

## El manifest, la fitxa i el kilobyte que va canviar de lloc (0.120.0)

Llegir cinc claus més d'un manifest no costa RAM de dades —són camps d'una `apps::Entry` que ja existia— però sí
**pila**: el lector hi porta el manifest sencer en un buffer local i l'`Entry` que omple viu al marc del visitant.
Mesurat amb `-fstack-usage` (`python3 tools/check_stack.py --all --budget 4096`, abans i després):

| Funció | 0.119.0 | 0.120.0 |
|---|---|---|
| `kernel::apps::load` | 1 488 B | **1 568 B**, i després **~520 B** amb el buffer estàtic (vegeu avall) |
| `WalkSink::on_entry` | (dins del visitant) | **1 104 B** |
| `kernel::shell::run_app` | 944 B | **1 056 B** |
| `kernel::shell::print_manifest` | (dins de `radio_done`, 1 664 B) | **1 376 B** en marc propi |
| `kernel::shell::execute` | 2 272 B | **2 640 B** |

- **El pitjor marc continua essent `shell::execute`**, ara 2 640 B (era 2 272 B): el que hi va créixer és
  `apps info`, que hi cap a dins amb el camí de l'app i el del manifest.
- **El kilobyte del manifest i el de la fitxa són estàtics i no de la pila**: `load` era el punt més fondo de la
  cadena que arrenca una app (`execute` → `run_app` → `load_source` → `for_each` → `on_entry` → `load`, **6,7 KB
  dels vuit de la tasca** amb els dos kilobyte a la pila). Cap de les dues funcions no es pot tornar a cridar dins
  seu —`read` i el recorregut es fan d'un en un, a la mateixa tasca, i cap visitant no carrega un altre manifest— o
  sigui que els dos buffers són un de sol i **2 048 B de `.bss`** en lloc de pila.
- **El preu, amb números**: la RAM interna de l'ATOM Matrix passa de **119 096 B (36,3 %)** a **121 144 B (37,0 %)**
  i la flash de 2 218 733 B a 2 218 681 B. La cadena més fonda baixa a **~5,6 KB**, que és menys del que era abans
  d'aquest canvi tot i haver-hi tres marcs més.

## La llista d'objectius del dit, de 91 168 B a 8 192 B (0.189.10)

**El recorregut que decideix on pot caure un dit o el dial reservava un objectiu per cada lloc de la taula de
llenços** i ho feia **a cada esdeveniment**: cada marc d'un contacte, cada tecla, cada volta del dial. Amb la
taula d'aquesta placa són `2 849 × 32 B = 91 168 B` per esdeveniment, i això va tenir dues conseqüències que es
van veure juntes:

- **la petició de 91 168 B és la que deixava la memòria curta**: no hi cap en una pàgina que ja té els seus
  llenços (la vista de la Paper són 246 240 B), el PAL crida el relleu, i **és el relleu qui es queda les
  memòries de sessió de les apps** (docs/75, la secció de la memòria de sessió). El registre del llançador ho
  deia amb els dos números: `home_eink gives back 'apps': 32768 B, the memory is short` i
  `the memory is short: 1 block(s), 32768 B given back for an ask of 91168 B`. O sigui que **el llançador pagava
  la seva pròpia caché per cada toc que responia**;
- **i quan el bloc no es podia aconseguir, el recorregut es quedava amb zero objectius i la pàgina deixava de
  respondre a tot** —ni el cercle del dial, ni el dit, ni les tecles— sense una línia al registre. Les dotze
  cel·les del llançador es recorrien, es veien amb la caixa bona, i cap no esdevenia objectiu: un toc cau sobre
  la pàgina i no obre res (mesurat a l'emulador, 14/11).

Ara:

| | Abans | Ara (0.189.10) |
|---|---|---|
| Mida del bloc | 91 168 B (una entrada per lloc) | **8 192 B** (256 objectius) |
| Quan es demana | a cada esdeveniment, i es torna | **una vegada per app**, es queda fins que l'intèrpret s'acaba |
| Si la memòria no en té | `capacity = 0`: la pàgina no respon | **es demana més petit**: 256 → 128 → 64 → 32 → 16, i la pàgina respon els que hi caben |

- **El bloc es queda entre esdeveniments** i es torna a `luabind::stop()`, que és quan l'app s'ha acabat: la
  petició passa de seixanta per segon a una per app, i el relleu ja no té cap motiu per quedar-se una memòria de
  sessió.
- **El mínim és el que fa una pàgina**: 256 objectius són **cinc vegades la pàgina més llarga del sistema** (les
  53 parades de la llista de comandes del shell, amb títol, frase i `Back`) i **vint vegades les dotze cel·les
  del llançador**. Una pàgina que no hi cap ho diu al registre i la volta següent en demana el doble, fins al
  sostre de la taula.
- **I el zero pràcticament desapareix**: si la memòria extra no pot donar els 8 192 B, el recorregut en demana la meitat,
  i la meitat, fins a **16 objectius (512 B)** —que és mitja dotzena de cel·les i mig centenar de pàgines del sistema més
  petites que la del llançador—. **No hi ha cap bloc estàtic de reserva**: els 8 192 B de `.bss` que vaig provar de posar-hi
  no enllacen la Paper (`region `dram0_0_seg' overflowed by 8088 bytes`), i un recurs que no enllaça no és un recurs. Si ni
  16 objectius es poden aconseguir, la pàgina ho diu al registre en lloc de callar i ho torna a demanar al següent
  esdeveniment.
- **Mesurat a l'emulador**: `[I][UI] the list of stops: 256 of 2849 slots, 8192 B` surt **una sola vegada** per
  app; la caché del llançador es recupera del disc (`asks for 'apps': 32768 B, restored 32768 B from the disk`) i
  s'hi queda (`home: cache kept`); i un toc a la cel·la del 2048 obre l'app amb la caché calenta i amb la freda.

## El que es pagava sempre i només servia de vegades (0.190.202)

L'amo, mirant una nota seva sobre el Wi-Fi que no arrencava en un ATOM: *«busca coses allotjades estàticament i fes-les dinàmiques, passa a
psram tot el que puguis»*. La nota deia `ESP_ERR_NO_MEM; 43156 B free, largest 32756 B` —un heap amb quaranta-tres kilobytes i cap forat de
trenta-dos— i el que hi havia darrere eren **tretze kilobytes i mig de taules fixes** que la placa pagava a l'arrencada per a feines que
moltes vegades no es fan mai:

| Què | Bytes | Quan es fa servir | Què se n'ha fet |
|---|---|---|---|
| El símbol del QR del provisioning | **2 025** | una vegada a la vida, si algú provisiona per codi | es demana a `pal::extra_memory()` i es torna en dibuixar-lo |
| Els mòduls de `qr.encode` de Lua | **4 096** | quan una app dibuixa un QR | el mateix, per a aquesta crida i prou |
| Els tres blocs de treball del codificador | **3 577** | el mateix moment | un `Scratch` que els demana en entrar a `encode` i els dona en sortir, per qualsevol camí |
| Els zeros d'esborrar un bloc del banc | **4 096** | quan un bloc de sessió viu al `himem` | cent vint-i-huit bytes a la pila, en comptes de quatre mil noranta-sis a la `.bss` |

**Mesurat a l'ATOM, amb el mateix codi i el mateix `sdkconfig`** (`xtensa-esp32-elf-size` sobre el `firmware.elf`):

| | abans | després |
|---|---|---|
| `.bss` | 61 796 B | **52 100 B** |
| `.data` | 385 697 B | **381 617 B** |
| RAM estàtica que compta la plataforma | 89 976 B (27,5 %) | **80 280 B (24,5 %)** |

- **La porta és la de sempre i no una de nova**: `pal::extra_memory()` ja dona PSRAM quan la placa en té i la memòria de casa quan no, i
  els blocs surten a `mem blocks` amb el seu amo, o sigui que una fuita d'aquests es veu com qualsevol altra.
- **El codificador del QR és pur i no en sap res, de la PSRAM**: `lib/qr` no depèn de `pal` i no hi ha d'afegir la dependència per un
  bloc de treball. Els seus tres blocs es presten amb `malloc`/`free` dins d'`encode`, i el que es queda és el que és una taula i no un
  bloc: els dos logaritmes del cos de Galois (768 B), que es calculen una vegada.
- **El bloc es presta i no es parteix**: `data_modules()` dibuixa sobre el mateix mapa de mòduls de funció i es pot cridar mentre
  `encode` treballa, o sigui que el seu `Scratch` rep el bloc que ja hi és i no en demana un segon. Això va ser un `SIGSEGV` de la
  suite del QR la primera vegada —la funció que dibuixa sense codificar no demanava res—, i és el motiu pel qual la prova hi és.
- **El que es va deixar com era en aquesta passada, i per què**: la cua de peticions de la xarxa (4 064 B) i el valor d'una clau de la NVS
  (1 024 B) es van quedar a la `.bss` perquè fer-los dinàmics *per crida* els trauria d'on són per posar-los al mateix lloc un instant
  després —i perquè el valor de la NVS ha de sobreviure entre les crides d'un mateix fitxer obert—. **Dues versions més tard es van moure
  igualment, però no per crida sinó un cop i es queda** (vegeu la secció de la 0.190.207). El lector del llistat (2 264 B) té el seu
  propi comentari i la seva raó —la pila del bucle són setze kilobytes i un lector de dos i mig a sobre va petar una vegada—, i no s'hi
  ha tocat **fins a la 0.190.260**, que és la secció de sota.

### La finestra del llistat, un cop i es queda (0.190.260)

L'amo, després de mirar el `mem` de la placa: *«cal estalviar memoria... Pero cosetes petites i sempre explicant-me quin es el canvi
abans de canviar res!»*. El lector del llistat del repositori tenia el seu comentari des de la 0.88.4 —no pot ser una local, perquè la
pila del bucle són setze kilobytes— i **això no ha canviat**: el que ha canviat és que l'objecte ja no porta la finestra a dins.

| Què | Abans | Després | Quan es fa servir |
|---|---|---|---|
| `repository::Listing` (la finestra de 2 048 B + el nom i l'adreça) | **2 264 B** de `.bss` | **220 B** de `.bss` + un bloc de 2 048 B que es demana | un `store`, un `install` o el llistat d'un repositori |

- **El bloc es demana el primer `open()` i es queda**: `pal::extra_memory()` dona PSRAM quan la placa en té i la memòria de casa quan no.
  No es torna a cada `close()` perquè un `entry_at` obre i tanca el document una vegada per entrada, i el que seria estalviar seria
  demanar i tornar el mateix bloc vint vegades seguides (docs/30, docs/70).
- **Cap crida no ha canviat**: `one()` segueix tornant la mateixa referència i `entry_at`, `start`, `next`, `stop` i el llistat del
  prompt fan exactament el mateix. El que ha canviat és d'on surten els dos kilobytes.
- **Una placa que no en pot donar cap** —ni PSRAM ni dos kilobytes de heap— no té llistat i `open` diu `false`, que és el que ja hauria
  passat en no poder obrir el fitxer.
- **La mesura al `lilygo-t5s3-idf`**: `.dram0.bss` 79 416 → **77 376 B** i el símbol 2 264 → **220 B** (docs/50, docs/75).

### Els manifests de la porta de les apps (0.190.261)

La segona coseta de la mateixa tarda: `apps_ops` tenia **dos `static Entry`** —el manifest sencer, 772 B cada un—, un a `lua_run`
(`app.run`) i un a `lua_info` (`apps.info`), i els pagava tota l'estona una placa que només els toca quan el llançador prem una cel·la
o quan un visitant d'un llistat demana un manifest.

| Què | Bytes | Quan es fa servir | Què se n'ha fet |
|---|---|---|---|
| El manifest d'`app.run` | **772** | quan una app en vol arrencar una altra | `pal::Scratch` dins la crida, tornat per qualsevol camí |
| El manifest d'`apps.info` | **772** | per cada cel·la que un llançador dibuixa | el mateix |

- **És la porta que ja hi era**: la mateixa que fa dinàmic el *text* del manifest a `apps::load` des de la 0.190.206. El bloc s'omple
  amb un `Entry()` de col·locació, o sigui que comença a zero exactament com el `static` que hi havia —un camp que un manifest no diu
  no pot arribar amb brutícia del bloc d'abans.
- **Si no hi ha bloc, es diu**: `there is no memory to read its manifest`, que és millor que dir que l'app no existeix.
- **Provat a l'emulador amb l'intèrpret de debò** (0.190.261): `apps.info("files")` torna `Files`, `0.1.0`, `works_here = true`;
  `apps.info("nosuchapp")` falla amb la seva taula d'error; `app.run("nosuchapp")` diu *«there is no such app on this disk»* i
  `app.run("files")` llegeix el manifest i arriba fins al shell.
- **La mesura**: `.dram0.bss` 77 376 → **75 832 B** (1 544 B).

### L'històric del prompt, lletra per lletra en un bloc (0.190.262)

L'amo va fer la pregunta que calia: *«si l'històric de les comandes ocupa massa és perquè és una llista fixa, no? no trobes que és millor
que es guardin lletra per lletra en el seu propi psmalloc»*. Sí. `history_[kHistoryEntries][kLineBytes]` era **vuit calaixos de 128 B** —
1 024 B de RAM interna— i una ordre de tres lletres en desaprofitava cent vint-i-quatre.

| Què | Abans | Després |
|---|---|---|
| `shell::g_editor` | **1 292 B** de `.bss` | **280 B** de `.bss` + un bloc de 2 048 B |
| Ordres que es recorden | **8** | una mica més de **cent** de setze lletres |
| On viu l'històric | RAM interna, sempre | el bloc del `pal::extra_memory()`, demanat al primer byte |

- **El bloc es demana quan algú escriu i no a l'arrencada**: a board nobody ever types a command at does not pay for it, i quan arriba
  la primera tecla els llibres de la memòria ja són oberts, o sigui que el bloc surt a `mem blocks` **amb el seu amo** (`shell`) i no com
  «nobody» —que és el que va sortir la primera vegada, i és el motiu pel qual la crida va amb un `pal::MemoryTag`—.
- **La línia i l'esborrany no es mouen**: `line_[128]` i `draft_[128]` es toquen **a cada tecla** i es queden a la RAM interna, dins de
  l'objecte. El que és fred és l'històric: només es llegeix quan es prem ↑ o ↓.
- **L'editor segueix sense assignar res de seu**: `history_in()` li dona el bloc i `line::Editor` continua essent la classe pura de sempre
  —el bloc el posa qui el té, com el `Scratch` del QR—. Sense bloc: línia sí, històric no, i una línia al registre.
- **L'ordre que no hi cap no es trenca**: es llença la més vella i es mou la resta cap avall; una ordre més llarga que el bloc sencer es
  descarta sencera, perquè mig ordre és pitjor que cap.
- **Provat**: la suite de l'editor amb **7 casos** —dos de nous: quaranta ordres es recorden i es camina fins a la primera, i un prompt
  sense bloc no peta— i a l'emulador pel camí de debò (tecles al port): `mem blocks` ensenya `shell 1 2048 B` i `mem` + ↑ + Enter torna a
  executar `mem`.

## El que la `.bss` tenia per a feines d'un cop (0.190.207)

L'amo va tornar a la càrrega: *«si veus que és perquè falta memòria, rebusquem al codi, segur que trobem mes declaracions de cues
estàtiques. Quan podem aprofitar al màxim la PSRAM i fer tot lo raonablement més dinàmic possible...»*. **No era falta de memòria** —tres
bandes no compilaven per un *stub* trencat, i això és un enllaç i no un pressupost—, però la pregunta era bona i la resposta són **7 136
bytes** de la Paper:

| Què | Bytes | Quan es fa servir | Què se n'ha fet |
|---|---|---|---|
| La cua de peticions de la xarxa | **4 064** | vuit peticions de mig kilobyte que una placa sense ràdio no omple mai | `the_queue()` les pren de `extra_memory()` la primera vegada que una petició hi entra, i les vuit entrades es construeixen al bloc |
| El manifest d'una app | **1 024** | cada vegada que el sistema llegeix una app | `pal::Scratch` a `apps::load`: es demana en entrar i es torna en sortir, per qualsevol de les nou sortides |
| El manifest d'`apps info` | **1 024** | quan una persona escriu la comanda | el mateix, amb `pal::kMemoryShell` |
| El valor d'un fitxer de la NVS | **1 024** | quan una app navega el `/nvs` com a fitxers | un bloc que es pren un cop i es queda, perquè el `read` el camina amb `g_pos` entre crides |

| | abans | després |
|---|---|---|
| RAM de la Paper (`pio run -e m5-paper-idf`) | 105 860 B | **98 724 B** |
| RAM de l'ATOM (`pio run -e m5-atom-idf`) | 80 280 B | **73 208 B** |

I la resta de bandes, amb el mateix canvi: **Matrix 55 436 B**, **devkit 55 364 B**, **T-Koala 56 000 B** i **T5S3 93 024 B** (aquestes
quatre no porten la ràdio i el que hi guanyen és la cua, que mai omplen).

- **`pal::Scratch` és nou i és l'eina que faltava**: `pal::extra_memory()` amb la tornada dins del destructor, amb una marca (el bloc
  surt amb amo a `mem blocks`) i amb un `false` quan el bloc no s'ha pogut prendre en lloc d'un punter que mata. Una funció amb nou
  sortides ja no ha de recordar-se de tornar un kilobyte.
- **Un bloc de la memòria extra no arriba a zero, i la `.bss` sí**: les vuit peticions de la cua es construeixen amb placement new, no
  amb `memset` —`Request` té valors de sèrie i el compilador té raó a refusar l'escombrat—, perquè `page_pending` llegeix el `kind`
  d'una entrada que ningú ha escrit encara.
- **El que NO s'ha mogut, i és a posta**: el taller del rasteritzador de ttf (`g_edges` 6 144 B, `g_points` 3 072 B, `g_crossing`
  1 536 B, més la direcció de cada aresta) i les files de l'antialiàsing (`g_aa_rows`, 1 152 B) són **el bucle interior d'una lletra**:
  cada fila d'un glif recorre les seves arestes, i això passat per la PSRAM d'un ESP32 —que es llegeix per la **mateixa** memòria cau
  que el codi de la flash— és canviar tinta per velocitat de pintada en una placa que pinta text tota l'estona. `zip::g_window`
  (1 024 B) es queda perquè la fitxa de la llibreria diu «with no allocation of its own» i això és una propietat, no un accident.
- **I el que queda a la llista, amb el número a la mà**: els tres blocs del ttf i el de l'antialiàsing són **12 096 B** i són l'única
  cosa gran que queda que sigui nostra; moure'ls demana abans una mesura de quant costa pintar una pàgina amb text quan el taller és a
  la PSRAM, i aquesta mesura encara no hi és.

## El provisionador del mesh, i la clau que ningú no posava (0.190.218)

La pregunta de l'amo —*«no puc provisionar jo manualment la clau del mesh per linia de comandaments, com amb el wifi?»*— es va respondre
encenent el provisionador d'Espressif (`CONFIG_BLE_MESH_PROVISIONER`, només a l'ATOM de moment), i això **es va mesurar abans de dir que sí**:

| Què | Número | D'on surt |
|---|---|---|
| Imatge de l'ATOM Matrix (`pio run -e m5-atom-matrix-idf`) | **2 131 994 B** de 2 621 440 | el **81 %** de la partició d'app; `size` del `firmware.elf` |
| RAM estàtica de l'ATOM, amb el provisionador | **85 056 B** | `.dram0.data` 26 528 + `.dram0.bss` 58 528 (`readelf -S`) |

- **El que aquest número encara no compta**: el que va vindre després de la mesura —les dues claus a `cfg` (`meshnet` i `meshapp`, 33 B cada
  una a la `.bss`), el pla de lligams (`g_to_bind`, 12 × 4 B) i els comptadors del camí de la clau d'aplicació— són **uns centenars de bytes**
  i no s'han tornat a mesurar. No és una estimació posada en una taula: és l'ordre de la cosa, i quan es torni a compilar es canvia el número.
- **I la imatge creix molt més que la RAM**, perquè el que entra és codi: el provisionador, el client de configuració i els seus estats. És el
  motiu pel qual el T5S3 i el Paper es queden de moment **només node**: primer es mira si hi caben amb el número a la mà, com sempre.
- **El mesh no és alhora que el teclat** (docs/50), o sigui que el que s'ha de mirar a la placa és `mem` amb el mesh apagat i amb `mesh on`:
  la pila demana desenes de kilobytes de RAM interna quan arrenca, i això és heap i no `.bss`.

## Un bloc de 64 KiB i el munt de trossos de la ràdio (0.191.1)

L'amo, amb la T5S3 a la mà i la 0.191.0 gravada: *«abort() was called at PC 0x420c49d7 on core 0»*, just després del rètol. Desxifrat amb
l'ELF del mateix firmware: `abort` ← `epd_renderer_init` (render.c:285) ← `epd_init` ← `Epd47::begin` ← `display::boot`.

**La línia 285 no és cap error del panell: és la seva taula de conversió**, `heap_caps_malloc(64 KiB, MALLOC_CAP_INTERNAL)`, i epdiy hi
crida `abort()` si torna nul perquè no té cap camí de sortida. La taula no es pot fer petita (amb `EPD_LUT_1K` el `gc16` respon
`EPD_DRAW_LOOKUP_NOT_IMPLEMENTED`, ja provat i descartat).

**El que li prenia el bloc era el Bluetooth**, i l'ordre d'arrencada és tota la història:

| Moment | Memòria interna de la T5S3 |
|---|---|
| Abans del panell, amb la ràdio apagada | ~169 KB lliures, un sol bloc gran |
| `cfg::boot` encén el dispositiu (la persona el va deixar encès) | **−29 544 B** en un grapat de blocs: pools de NimBLE, cues i piles de tasca |
| `display::boot` demana **un** bloc de 64 KiB | el més gran que queda no hi arriba → `abort()` |

O sigui que **el total no era el problema i el tros més gran sí**: cent quaranta kilobytes lliures no fan una taula de seixanta-quatre si
estan partits. La 0.190.296 hi passava per poc; la 0.191.0 va moure unes desenes de bytes de dades i el tall va caure de l'altre costat.

**El que s'ha fet**: `cfg::boot` ja no encén la ràdio. El que fa és llegir els ajustos, i `cfg::start_the_radio()` es crida **després de
`display::boot`**, quan el vidre ja té la seva taula. No costa res —el vidre no llegeix la ràdio per a res i el primer que la vol és una
app— i és la regla que val per a qualsevol cosa gran d'aquesta casa: **el que demana un bloc contigu es fa abans del que deixa el munt de
blocs petits**.

**I el número queda escrit a cada arrencada**, perquè la propera vegada no calgui desxifrar cap adreça:

```
[I][MEMORY] before the panel: 162728 B free of internal memory, the biggest block is 162712 B
```

Va per `log_bootf` (0.190.292) i no per `log_printf`: és d'un moment que passa una vegada i ha d'arribar al cable.

## Els búfers del ràdio, reservats a l'arrencada (0.191.4)

L'amo, amb el mapa del report ja ben enviat: `E (77948) BLE_INIT: Malloc failed`, just després que l'altre cap llegís `0x2A4B`.

**D'on surt aquesta línia**: `components/bt/controller/esp32c3/bt.c` té un `malloc_internal_wrapper` que és el que el controlador del BLE
fa servir per als seus búfers —`heap_caps_malloc(size, DEFAULT | INTERNAL | DMA)`— i que **només diu «Malloc failed»**: ni la mida, ni el
bloc més gran que hi havia, ni qui el demanava. És la pitjor mena de línia i és la que es va passar tota una tarda mirant.

**El controlador té dues maneres de tenir els búfers de transmissió** (ho diu el mateix Kconfig):

| `BT_CTRL_BLE_STATIC_ACL_TX_BUF_NB` | Com viatja un paquet |
|---|---|
| `0` (el de sèrie) | es demana un búfer **abans de cada TX** i es deixa anar després: cada paquet depèn del bloc més gran que hi hagi en aquell moment |
| `> 0` | es reserven **una vegada a `esp_bt_controller_init`** i no es tornen fins que el controlador es desfà |

Amb `0`, una lectura de mapa, un anunci o una tecla poden fallar per com estigui el heap en aquell instant —i el heap d'aquesta placa té el
panell demanant **un sol bloc de 64 KiB** i les piles dient la seva. **Ara se'n reserven quatre** i el que fallava deixa de dependre del
moment.

**I el que es demanava de més**: `BT_CTRL_BLE_MAX_ACT` era **sis** instàncies d'aire de **828 B** cadascuna, i aquesta placa només en pot fer
servir tres alhora —un anunci, una exploració i **un** enllaç, perquè el teclat que s'ofereix i el que s'escolta no conviuen amb dos aparells—.
Es queda a tres. I `BT_NIMBLE_MAX_CONNECTIONS` baixa de tres a **un**: l'estat de tres aparells que no hi són mai.

**I el número que faltava, a partir d'ara, es diu sol** cada cop que un aparell es connecta:

```
[I][BT] internal memory: N B free (M B in the biggest block), of the DMA ones N B free (M B in one block)
```

El que compta **no és el total sinó el bloc més gran**, i encara més el més gran que és DMA: cent kilobytes lliures en cinquanta trossos no
fan un paquet de dos-cents seixanta bytes, i això és el que aquesta línia ha de dir la pròxima vegada que alguna cosa no hi càpiga.
