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
| Pressupost d'instruccions | **No existeix encara** | Avui una app que no cedeix penja la placa (docs/20, docs/90) |

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
| Buffer de línia de la shell | 1 × 128 B | **128 B** |
| Buffer de línia de l'app (`luabind`) | 1 × 128 B | **128 B** |
| Selector d'extensió (`shell`) | 8 candidats × 32 B + camí (128 B) + extensió (16 B) | **400 B** (mesurat: treure la tria desada va baixar la RAM estàtica de 28 948 a 28 756 B, i eixamplar el camí de 96 a 128 B l'ha apujada 32 B, fins a 28 788 B) |
| Buffer d'ordre del nucli | 1 × 128 B | **128 B** (a la pila) |
| Línia del log | 1 × 160 B | **160 B** (a la pila) |
| Pila de la tasca lectora de consola | 2 048 paraules | **8 192 B** |
| Taula de muntatges (`vfs`) | 8 entrades (7 ocupades al Paper: `/littlefs`, `/apps`, `/res`, `/data`, `/sd`, `/nvs` i `/cfg`; 6 a l'ATOM, que no té targeta) | **8 × (nom + estat + 40 B)** |
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
baixada d'una pàgina amb TLS a dins, més el mòdul dels ajustos (`/cfg`), la reescriptura de `/res` i el
buitat de la consola abans de dormir: **+2 920 B de flash i +40 B de RAM** sobre la mesura anterior del
mateix dia (2 005 361 B i 92 624 B).

| Què | Valor |
|---|---|
| Flash | **2 008 281 B** de 3 145 728 (63,8 %) — la partició d'aplicació és de 3 MB des del 31/10 |
| Flash lliure | **1 137 447 B** |
| RAM estàtica | **92 664 B** de 327 680 (28,3 %) |
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

## La M5 Paper, amb la ràdio heretada (31/10)

`pio run -e paper` del 31/10, amb el que aquesta placa hereta del xip (`esp32_caps()`, `esp32_net()`,
docs/50):

| | |
|---|---|
| Flash | **2 064 009 B** de 4 194 304 (49,2 %) — la partició d'aplicació de `partitions_16mb.csv` |
| Flash lliure | **2 130 295 B** per al panell i el que vingui |
| RAM estàtica | **92 924 B** de 327 680 (28,3 %); el build informa d'un 2,1 % perquè el denominador de la placa inclou els 4,5 MB de PSRAM |

**El preu d'heretar la ràdio, mesurat amb els dos builds del mateix dia**: el Paper sense ràdio era de
**553 665 B** de flash i **52 336 B** de RAM; amb `esp_wifi`, el provisioning, el NTP i
`esp_http_client` amb el paquet de certificats és de 2 064 009 B i 92 924 B, o sigui **+1 510 344 B de
flash i +40 588 B de RAM**. És el mateix bloc que l'ATOM ja pagava: el que costa no és la placa, és el
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
| Partició de 3 MB | 62,2 % | **62,7 %** (lliures: 1 172 447 B) |

Els +176 B són el comptador de la feina d'energia i el que costa a la política saber si hi ha una app
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

La columna «consum real» no s'inventa mai: es treu de `sys.heap()`, de l'ordre `mem` i del mapa de
l'enllaçador (`pio run -e atom -t size`), i s'apunta el número i el dia.

## L'assignador de Lua

És un embolcall de `malloc`/`free` que:

- Porta el compte del que ha donat i del que s'ha tornat.
- Compara amb el sostre de l'app **abans** de donar memòria: si el demanat no hi cap, falla.
- Guarda el **pic** (el màxim que s'ha fet servir) per poder dir «aquesta app arriba a X KB».
- No fragmenta a posta: no hi ha pools per mida ni res per l'estil. Si el pic d'una app és massa a
  prop del sostre, es baixa el sostre i es mira on peta.

`sys.heap()` dona `{free, used, limit, peak}`.

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
