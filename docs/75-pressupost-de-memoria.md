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
  l'arrencada diu avui `ui: view 540x912 at 0,48, 246240 bytes of canvas`, que és **540 × 912 / 2**
  —quatre bits per píxel— en **un sol bloc** de la PSRAM (el 1/11, amb la barra de 28 files, deia
  `540x932 at 0,28, 251640 bytes`). Els vuit canvas del sostre, si tots fossin de pàgina sencera, serien
  **1 969 920 B** (1 924 KiB) dels 4 096 que té una app: **gairebé la meitat**, i per això **cada control
  és de la seva mida** i un control **sense vista no gasta ni un byte** (docs/90, M9).
- **El que encara no està mesurat**: quants blocs reals van a la PSRAM en una app de debò i quants a
  la interna (`sys.heap().used` i el pic en surten, però el repartiment no es desa enlloc). Es mesura
  quan hi hagi una app que ho necessiti, amb `docs/95` al costat.

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
vista de l'app (a la Paper, 540 × 912 = **246 KB**) i el seu sostre. En una placa sense memòria extra, un
`130 944 B` de taula sobre 338 864 B és **el 39 % del que hi ha**, i el que queda ha de passar pel terra de
`kHomeReserveBytes` (64 KB) que protegeix els buffers del sistema (aquesta mateixa secció, més amunt).

**El que està mesurat i el que no:**

| Què | Número | Com |
|---|---|---|
| `sizeof(Canvas)` i les ranures | **176 B, 2 978 ranures** | `[I][UI] the table of canvases: 2978 slots of 176 bytes` a l'emulador, amb `--board m5stack-atom` i amb `--board m5stack-paper` (els dos donen el mateix, perquè la taula no depèn de la placa) |
| Quantes en rep una placa **sense** PSRAM | **no mesurat** | la línia de l'arrencada de l'ATOM és la que ho dirà, i `mem` després; cap build no ho ha llegit encara |
| Quantes en rep la Paper | **2 978** | cap dels 524 128 B no és a la RAM interna: la PSRAM els té de sobres |

**Què s'hi farà quan es mesuri** (i no abans, perquè el número ha de sortir de la placa): si l'ATOM es queda
amb 744 ranures o menys, la taula s'ha de dimensionar pel que la placa té i no per una constant —per exemple
`extra_memory()` quan el xip és zero, i `kSlotTableBytes` quan no ho és—, o baixar `kSlotTableBytes` a la meitat.
El que **no** s'ha de fer és llevar el `true`: sense ell, la Paper deixaria de tenir la taula a la PSRAM.

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
torni a mirar. Amb 100 000 instruccions i el model de 60 ns són **6 ms** de codi d'app entre dues mirades del
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
2. `sys.heap()` des de dins de l'app, en els moments que interessin.
3. `pio run -e m5-atom -t size` per al consum de flash i l'ús de RAM estàtica.
4. A l'emulador, `--mem-cap=<KB>` per reproduir el sostre de la placa al PC i veure-hi el pic sense
   haver de flashejar.

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
