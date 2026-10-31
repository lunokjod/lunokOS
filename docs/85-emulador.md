# 85 · Emulador

## Per què hi és

Perquè el cicle de treball no sigui «editar, compilar, flashejar, mirar el port, tornar-hi». Amb
l'emulador el cicle és «editar, `run <id>`, mirar la consola». I perquè les proves de debò —les que
executen una app sencera— es puguin fer en un portàtil sense tenir la placa endollada.

**L'emulador no és una maqueta: és el mateix codi.** Mateix nucli, mateix intèrpret, mateix VFS,
mateixes promeses, mateixos pressupostos. Només canvien les dues capes de sota.

## Com s'engega

Hi ha dues maneres i **només una és interactiva**:

| Via | Què passa |
|---|---|
| El binari directament, en un terminal | És la bona: stdin i stdout són el terminal i el prompt respon |
| `pio run -e emulator -t exec` o `-t upload` | PlatformIO executa el programa amb la sortida conductada: el banner surt, el prompt es queda al buffer i **el que escrius no arriba enlloc** |

Al repositori hi ha la tasca de VS Code **lunokOS: emulator**, que compila i executa el binari amb el terminal connectat de debò. També es pot fer a mà:

```
pio run -e emulator
.pio/build/emulator/program
```

Aquestes són les opcions del binari:

| Opció | Què fa |
|---|---|
| `--flash <dir>` | El directori que **és** el volum de la flash (per defecte `data`, el del repositori): `/littlefs/apps` és `data/apps` i no es copia res |
| `--state <dir>` | On viuen les memòries que no són un volum: `nvs/` i, quan n'hi hagi, la RTC i la FRAM (per defecte `emulator/`) |
| `--fs <dir>` | **Recanvi** del disc quan no hi ha `--flash`: `Options` hi cau si `flash_dir` és nul (`emu_storage.cpp`). El binari sempre acaba de posar `--flash` a `data`, o sigui que **des del binari no fa res**; el fan servir les proves, que es muntin les seves opcions |
| `--sd <dir>` | **La targeta**: aquest directori n'és el volum i es munta a `/sd`. Per defecte `emulator/sd`, i el banc el crea si el model té ranura; si s'esborra, la ranura queda buida (docs/70) |
| `--battery-mv <mv>` | **Una bateria de mentida**: el que llegeix `power.battery()`. Sense l'opció no hi ha bateria i la crida dona `nil`, que és el cas d'una placa que no en sap (docs/30). El `%` no es posa: surt de la corba de `hal/battery.h`, la mateixa que fa servir el Paper |
| `--board <nom>` | **Quina placa és el banc, i és obligatori**: `m5stack-atom`, `m5stack-atom-matrix` o `m5stack-paper`. `sys.board` respon aquest nom, `sys.emulator` continua cert, i `boards` del manifest es compara amb aquest i no amb els tres (docs/50). **El maquinari és el del model**: un ATOM té un llum i una tecla, la Matrix el panell de 25 i el sensor, i el Paper les tres tecles, l'indicador de bateria, la targeta i el rellotge amb pila — el mateix que diu la fitxa de docs/50, ni més ni menys |
| `--mem-cap <KB>` | **Baixa** el sostre de la VM (mai per sobre del que té el model, que es refusa: 320 KB als ATOM i 4 096 KB al Paper, docs/75): posa-hi el del model per veure-hi el pic |
| `--read-delay <us>` | Què costa **una lectura**, en microsegons: **1 000** per defecte (un mil·lisegon de flash, i el doble a la targeta) i `0` per no esperar gens. És el número que fa que una app que llegeix a cada torn no sembli ràpida aquí i lenta a la placa |
| `--baud <n>` | La velocitat del port sèrie, en baud: **115 200** per defecte, la de l'UART de la placa, o sigui **86,8 µs per byte** (10 bits a 8N1). `0` treu l'espera, que és el que vol una prova, perquè el seu console és un fitxer i no un cable |
| `--wake-early <ms>` | Que el banc torni d'un son **aquesta estona abans** de l'hora que se li ha donat: `0` (per defecte) és el rellotge honrat, i qualsevol altra cosa és un son que s'ha tallat —un dit al botó, un cable, una alimentació que parpelleja— per poder provar el cas en què la placa no arriba a la seva hora (docs/60) |
| `--now <època>` | Fixa l'hora d'arrencada. **Sense aquesta opció no hi ha època**: el rellotge val 0 i `time.caps().source` és `"none"`, com una placa que mai s'ha sincronitzat || `--time-scale <N>` | Accelera el temps N vegades (1000 = un dia en 86 segons) |
| `--seed <n>` | Llavor del generador aleatori — **encara no fa res**: es llegeix i ningú no el fa servir (docs/20) |
| `--wifi <ms>` | Quant triga la **xarxa de mentida** a pujar (per defecte 300 ms simulats) |
| `--no-wifi` | Una placa sense ràdio: `net()` és `nullptr` i `net.caps().wifi` és fals |
| `--wifi-fail` | Una xarxa que refusa: la promesa de `net.connect` falla amb `E_NONET` |
| `--wifi-scan <n>` | Quantes xarxes veu la ràdio de mentida (per defecte 1). Amb `0` és una placa enmig del camp: `net.scan` contesta `0`, no `true` |
| `--prov-ms <ms>` | Quant està obert l'**AP de provisioning de mentida** (per defecte 2 000 ms) |
| `--prov-ssid <s>` / `--prov-pass <s>` | La xarxa que el provisioning fals «rebuifa» (`casa` / `secret`) |
| `--prov-never` | Una placa que ningú no arriba a provisionar: el termini del nucli salta |
| `--prov-fail` | Un provisioning que falla: `E_NONET` i l'AP es tanca |
| `--http-ms <ms>` | Quant triga la **pàgina de mentida** a arribar (per defecte 50 ms simulats) |
| `--http-status <n>` | El codi que contesta (per defecte 200) |
| `--http-body <s>` | El cos que contesta. Sense aquesta opció contesta `page for <url>`, amb l'adreça a dins perquè es vegi que el que s'ha demanat és el que torna |
| `--http-fail` | Un servidor que no contesta: `E_HTTP` |
| `--imu-ax <g>` / `--imu-ay <g>` / `--imu-az <g>` | Què llegeix el **sensor de mentida** a cada eix. Per defecte 0, 0 i 1, que és la placa plana damunt la taula |
| `--imu-temp <C>` | La temperatura que dona el sensor (per defecte 25) |
| `--imu-fail` | Un sensor que no contesta: `imu.read()` respon `E_IO` |
| `--env-celsius <C>` | La temperatura que dona el **sensor d'ambient de mentida** (per defecte 25) |
| `--env-humidity <%>` | I la humitat (per defecte 50) |
| `--env-fail` | Un sensor d'ambient que hi és i no contesta: `env.read()` respon `E_IO`, que no és el mateix que una placa sense sensor |
| `--run <id>` | Arrenca una app pel mateix camí que `run <id>` de la shell i **surt** amb el resultat (0 si ha anat bé) |
| `--script <fitxer>` | Mode lot: executa ordres i surt |
| `--verbose` | El log també surt per la consola |

En mode lot, cada app que arrenca es deixa acabar abans de la línia següent, i **també cada ordre que
treballa** (`wifi add`, `ntp`, `http`): una comanda que espera la ràdio tindria la línia següent
trobant-la ocupada, que és el que va passar quan només s'esperaven les apps (31/10). I després de
**cada** línia es donen dos torns, perquè el que es fa a final de torn —el llum, per posar un cas— hagi
passat abans de la línia següent; sense això un guió veia una placa que no pintava mai res (31/10). El límit és de
**30 s** per línia: una app que espera una tecla que no arriba no pot penjar el guió. Si el guió
arrenca una app que llegeix, l'entrada es pot encaminar per stdin, i ha d'arribar **mentre l'app ja
corre**:

```sh
printf 'run echo\n' > /tmp/s.txt
( sleep 0.4; printf 'hola mon\r' ) | ./.pio/build/emulator/program --board m5stack-atom --script /tmp/s.txt
```

## El disc de treball

**El disc del banc és el `data/` del repositori, tal qual.** El binari hi apunta per defecte
(`--flash data`), o sigui que `/littlefs/apps` és `data/apps` i `/littlefs/res` és `data/res`: el que
s'edita és el que corre, i **no cal copiar res**. El que una app escriu va a `data/data/<id>/`, que
és estat i no codi, i per això és al `.gitignore`.

A part del disc hi ha les memòries que **no són un volum**, i totes viuen en un sol directori
(`--state emulator`, per defecte):

```
emulator/
  nvs/…                 el magatzem clau-valor (avui un fitxer per clau)
  rtc.bin               la memòria RTC del xip, 7 168 B, binària
  fram.bin              la FM24C02 del Paper, 256 B, binària
  sd/                   la targeta, quan aquest directori existeix
```

El magatzem clau-valor **no és a dins del disc** perquè a la placa tampoc no ho és: la NVS és una
partició pròpia i no un fitxer que es veu a `/littlefs`. La seva mida a la placa la diu
`partitions.csv` (16 KB); aquí és un arbre de fitxers i no té sostre, i `stats()` ho diu tornant 0.

**Les altres dues memòries són fitxers binaris de la mida exacta del maquinari**, i es mantenen entre
execucions com la memòria d'un aparell (`emu/memories.h`):

| Fitxer | Mida | De qui és | Quan es perd |
|---|---|---|---|
| `rtc.bin` | **7 168 B** | la memòria RTC lenta del SoC (docs/50) | amb el corrent — **excepte** al Paper, que té el BM8563 amb pila de botó i torna sabent l'hora |
| `fram.bin` | **256 B** | la FM24C02 del Paper (docs/50) | **mai**: una memòria ferroeléctrica no és volàtil |

**Els fitxers els crea el banc sol, i només els que el model té** (`emu/model.h` és l'única llista de
què porta cada placa): un ATOM té `rtc.bin` i prou, i un Paper té `rtc.bin`, `fram.bin` i la targeta.
Un model sense FRAM no rep `fram.bin`, i `emu::fram()` torna `nullptr` — el mateix que una placa sense
aquest xip.

O sigui que una execució de l'emulador és un **cicle d'alimentació**: `emu::power_cycle()` esborra la
RTC si la placa no té pila darrere el rellotge (ATOM i Matrix) i la conserva si en té (Paper), que és
exactament el que fa el maquinari de debò. L'hora que hi ha a `rtc.bin` és el primer que mira el
rellotge de l'emulador quan no hi ha `--now`, i s'hi torna a escriure cada cop que algú posa l'hora o
que la placa dorm.

**La targeta la posa el banc sol si el model té ranura**: un Paper arrenca amb `emulator/sd` creat, o
sigui que `/sd` hi és i el muntatge i la capacitat `sd` també, com a la placa de debò. Un ATOM o una
Matrix no tenen ranura i no reben res; i si el directori s'esborra, la ranura queda buida i `/sd`
desapareix, que és el cas d'una targeta treta (docs/70). `--sd <dir>` canvia el directori (i també
s'hi crea). Amb la targeta posada, `cfg auto` hi desa els ajustos, i això és el que fa que el camí de
la targeta es pugui provar sencer al PC.

**La bateria és l'única cosa que al banc no es pot llegir sola**: a la placa és un divisor connectat a
un pin, i aquí no hi ha ADC, així que un model amb indicador sense `--battery-mv` diu que no ho sap —
la veritat del banc, no una tensió inventada.

Un banc que vulgui el seu propi disc (una prova, un joc de dades) diu `--flash <dir>` i posa el volum
allà. `tools/sync_emulator.sh` i la tasca **lunokOS: seed emulator disk** segueixen funcionant per a
aquest cas, però amb el disc real (`data/`) ja no fan falta.

L'espai també és el de la placa. `fs.space('/sd')` diu **16 GB** (el que queda de debò al disc del
PC és el lliure, i el que en falta es mostra com a ocupat), i `fs.space('/littlefs')` o `/data`
diuen la partició del model (`Model::disk_bytes`) amb el que sumen els fitxers com a ocupat. Mesurat
a l'Atom el 31/10: `/data littlefs rw  915701 of 917504 B free` (1 803 B dins del disc).

Es pot editar amb qualsevol editor. Es pot esborrar per començar net. Es pot versionar un joc de
proves petit dins del repositori i deixar la resta fora del git.

Per tenir-hi les apps de la imatge de la flash (les de `data/`), es copien un cop:

```
tools/sync_emulator.sh
```

Fa el mateix que `mkdir -p emulator/littlefs && cp -r data/apps emulator/littlefs/`, però **esborra
primer** `apps/` i `res/` del disc de l'emulador: és una còpia, no una fusió, i una app treta de
`data/` ha de desaparèixer també d'aquí. També hi ha la tasca de VS Code **lunokOS: seed emulator
disk**.

**Per què cal recordar-ho** (només per a un banc que copiï el seu disc, `--flash emulator/littlefs`):
editar una app a `data/apps` **no canvia res** en aquella còpia; el que corre és el disc de treball.
Amb el disc real —el camí per defecte— això no passa.

## Temps virtual

El rellotge de paret i el monòton avancen de pressa quan hi ha `--time-scale`, però **el sistema no ho
sap**: la cua d'alarmes, el son i els torns es comporten igual.

- `--time-scale 1000` amb una alarma a les 6:00 del dia següent: es veu disparar en menys de dos
  minuts de rellotge de paret.
- `--now` fixa l'arrencada, que és el que fa que una prova sigui reproduïble. Sense `--now` el
  rellotge de paret val **0** i diu que no en té, que és el cas de la placa abans que algú posi
  l'hora (docs/60).
- Els temporitzadors de maquinari no existeixen: n'hi ha prou amb el rellotge del PC.

## Mode lot

`--script guio.txt` llegeix ordres com les del prompt (`run epub`, `ls /apps`, `alarms`…) i, en
acabar, surt amb codi:

| Codi | Vol dir |
|---|---|
| `0` | Tot ha anat bé |
| `1` | Una ordre ha fallat |
| `2` | Els arguments no s'entenen |

No hi ha cap codi més: una ordre o una app que falla fan **1**, i no hi ha cap `3` al binari.

Això és el que fan servir les proves del nivell «app»: un guió, una app de prova i un codi de sortida.
Res de comparar text a ulls.

## Fidelitat: què s'assembla i què no

| Aspecte | A l'emulador | A la placa |
|---|---|---|
| Nucli, Lua, VFS, promeses | **Idèntic** | Idèntic |
| Resolució de camins i permisos | Idèntic | Idèntic |
| Sostre de memòria | **El de la placa, i no més**: l'ATOM contesta el sostre per defecte (64 KB, `Model::app_limit_kb` = 0, que vol dir «la placa no en diu cap») i el **Paper contesta la seva PSRAM mapejada (4 096 KB, de 8 MB que en té el xip)**. `--mem-cap` el pot **baixar** per veure-hi el pic, i demanar-ne més es refusa amb el número que té el model. `sys.heap()` dona el pressupost de l'app (`free`, `used`, `limit`, `peak`) i la memòria extra del model (`extra_total`, `extra_free`), i `mem` escriu els tres números del model (`Model::psram_kb` mapejats i `Model::psram_chip_kb` del xip) | 320 KB de SRAM al xip i, al Paper, **4 MB de PSRAM mapejada** on van els blocs de 4 KB en amunt (docs/75) |
| Cost d'una lectura | **Simulat**: cada lectura costa **1 ms** (i el doble a la targeta), o sigui que una app que llegeix un fitxer per torn va tan lenta aquí com al vidre. El número és una aproximació fins que algú el mesuri amb un cronòmetre, i es canvia amb `--read-delay <us>` (0 = cap espera) | Mil·lisegons de flash o SD |
| Targeta (SD) | **Un directori del disc, i només si el model té ranura** (el Paper): el mateix backend POSIX que la flash, o sigui que el que llegeix aquí llegeix a la placa. Es presenta com una targeta de **16 GB** amb el lliure del PC, i el que falta fins a 16 GB és l'ocupat | FATFS per SPI a la Paper: CS GPIO4, 10 MHz i el CS del panell (GPIO15) deseleccionat abans de tocar el bus (docs/50) |
| Desgast de la flash | No existeix | Existeix |
| Consum | No existeix | Existeix |
| Despertar de son | Simulat | Real o inexistent segons la placa |
| Espai lliure (`fs.space`, `mounts`) | **La targeta sí**: és una targeta de **16 GB** i el lliure és el que queda de debò al disc del PC, així que el que falta fins a 16 GB es mostra com a **ocupat** (`free 6587 MB of 16384 MB`, 31/10). **La flash també**, des del 31/10: el total és el de la partició de la placa (`Model::disk_bytes`, 0x0E0000 = 917 504 B a l'Atom i 0xBE0000 = 12 451 840 B al Paper) i l'ocupat és el que sumen els fitxers del directori, amb el que quedi al disc del PC com a sostre (si el PC s'omple, es mostra com a ocupat). Mesurat a l'Atom: `/data littlefs rw  915701 of 917504 B free`. `/nvs` segueix dient `E_UNSUPPORTED` a les dues bandes, perquè guarda entrades i no bytes | Números de debò (`/littlefs` 634 880 B lliures de 917 504 el 30/10, i `/nvs` diu `E_UNSUPPORTED` a les dues bandes) |
| Pantalla | No | No (a la v1) |
| Port sèrie | stdin/stdout en mode cru, **i a la velocitat de la placa**: 115 200 baud, o sigui 86,8 µs per byte, i els bytes surten per tandes de dos mil·lisegons com sortirien del cable en lloc de sortir tots de cop. Mesurat: 2 563 B costen 0,21 s (86,8 µs × 2 563 = 0,22 s). `--baud <n>` la canvia i `--baud 0` treu l'espera | UART a 115 200 |
| Xarxa | **De mentida**: connecta a res en `--wifi` ms i no surt enlloc | La de debò (`WiFi.begin`, que no espera) |
| Provisioning | **De mentida**: un AP que no existeix i unes credencials que surten de les opcions | El component d'Espressif: l'app ESP SoftAP Provisioning del telèfon el troba de debò |
| Pàgines (`http.get`) | **De mentida**: el cos i el codi surten de les opcions, i el TLS no hi és: no hi ha cap certificat a validar | `esp_http_client` amb TLS i el paquet d'autoritats del build; el codi, el cos i els errors de certificat són els de debò |
| Energia (`power.*`) | **De mentida i ho diu**: no hi ha bateria (`power.battery()` és `nil`), i dormir vol dir que **el rellotge virtual salta** fins a l'hora demanada. El PC no s'atura. `poweroff` sí que és de debò en el que importa: **el model del Paper contesta que sí i el procés s'acaba** (l'estat de les memòries es desa en sortir, com en una apagada), i el model de l'ATOM contesta que no perquè no té control del seu corrent — el banc diu el que diu la placa | Bateria si la placa en té; el son és el de debò: `esp_light_sleep_start` i, en profund, la placa es reinicia; `poweroff` fa la seqüència del fabricant (GPIO23, GPIO5, GPIO2 i son profund) i la placa es queda sense corrent |
| Tornar d'un son profund | **No passa**: l'emulador no es reinicia enmig d'una crida, i per això les proves criden `kernel::power::restore_session()` a mà, que és exactament el que fa l'arrencada | El xip es reinicia i arrenca: l'arrencada llegeix la nota del son i torna a obrir l'app amb `app.args.restore` |
| Velocitat del processador | **De mentida**: el PC no canvia de marxa; el número es desa (arrenca a **160** i accepta 80, 160 i 240) perquè el camí i els seus refusos es puguin fer servir i provar | `setCpuFrequencyMhz` de debò, amb l'APB i el tick de FreeRTOS movent-se amb ell |
| Llum (`led.*`) | **De mentida**: el color es desa i el test el pot preguntar (`emu::led_red()`… i `emu::led_frames()`, que compta els frames). El **nombre de llums és el del model** (1 a l'ATOM, 25 al Matrix, cap al Paper) i el sostre del canal és **20**, el del panell, a tots tres; `emu::led_pixel(i, …)` en llegeix un: així una app escrita per al panell es pot provar al PC. Els efectes es proven **movent el rellotge a mà** (`emu::advance_ms`), que és la mateixa hora que llegeix el nucli: una respiració de cinc segons es mira en un mil·lisegon | El `neopixelWrite` i el frame d'RMT de debò, sobre el WS2812 o els 25 del panell a GPIO27 |
| Apps i manifest (`boards`) | **El banc és una placa, la que se li diu i és obligatori dir-la**: `--board m5stack-atom`, `m5stack-atom-matrix` o `m5stack-paper`, i respon **aquell** nom i la família `esp32`. Una app del seu model corre sense dir `emulator` al manifest; una d'un altre model hi surt llistada amb l'avís i `run` la refusa dient les plaques que vol i quina és aquesta. És el mateix criteri que a la placa, i per això provar-ho al PC vol dir alguna cosa (docs/40) | `apps` la llista amb el que necessita i `run` la refusa dient les plaques que vol i quina és aquesta |
| Sensor (`imu.read`) | **De mentida i a la mà del test**: contesta el que diuen les opcions (`--imu-*`), i `--imu-fail` és un xip que no hi és. El que es prova aquí és el camí de la dada —de Lua al HAL i de tornada a l'app— i no la qualitat de la mesura, que només es pot veure al maquinari | L'MPU6886 per la I2C interna, amb les escales del full del fabricant (docs/50) |
| Aire (`env.read`) | **De mentida i a la mà del test**: el model del Paper té sensor i els ATOM no, i les dues xifres són el que diuen `--env-celsius` i `--env-humidity`; `--env-fail` és un xip que hi és i no contesta, que és `E_IO` i no `E_UNSUPPORTED` | El SHT30 per la I2C interna: una comanda, sis bytes i dos CRCs (docs/50). **El banc no té bus**: no hi ha cap clock stretching que es pugui comptar, així que un sensor que el banc llegeix bé pot ser un sensor que la placa dona per mort (va passar l'1/11: `env yes` al PC i `env no` a la placa, docs/50) |
| Tecles (`button.*`, `on_button`) | **De mentida i a la mà del test**: `emu::press_button(true/false)` prem la principal i `emu::press_key(n, true/false)` qualsevol de les tecles **del model** (fins a tres; una a l'ATOM i al Matrix, tres al Paper), i el temps que es compta és el del PC. La forma, però, encara és la de la Paper a tots els models: un dial compost a la cara esquerra (`centre`, `up` i `down`), o sigui que una app que tria per posició es pot provar al PC. Fer que la forma segueixi el model és el que queda de M8 (docs/90) | El GPIO39 de l'ATOM (una tecla simple a la cara de dalt) i les tres de la Paper, amb 10 ms de rebot |

**Comprovat al banc el 31/10 amb la 0.8.2**, amb `--board m5stack-paper --sd <dir>` i un guió:
`mem` diu `extra memory 4096 of 4096 KB usable, of 8192 KB on the chip (psram)`, `mounts` diu
**`mounts: 9`** (amb `/rtc` i `/fram` entremig, i `/nvs` donant `entries 1`), `run mtab` en llista
nou amb els bytes de cadascun, i `help` ensenya `sleep at <epoch seconds>`. La línia `free heap` **no
hi és al PC** i `mem` ho diu (`heap not available on this build`): la RAM interna del xip no té un
equivalent al banc, i inventar-ne un número seria pitjor que dir-ho.

**El que encara no fa.** El maquinari fals **ja segueix el model** —capacitats, llums, nombre de tecles,
sensor, indicador de bateria, ranura i FRAM surten de `emu::model()`—, i el que queda és la **forma de
les tecles**: el dial compost de la Paper és el de tots tres models, quan el model de l'ATOM n'hauria
de tenir una de simple a la cara de dalt. També falta poder prémer una tecla **a mà**: `emu::press_key`
només el criden les proves. Les dues coses són a [`90 · Full de ruta`](90-full-de-ruta.md), M8.

## Consell de treball

1. Escriure l'app amb l'emulador, amb `--mem-cap` posat al número de la placa.
2. Passar-la al guió de proves en mode lot abans de tocar la placa.
3. Flashejar i provar-hi **només** el que l'emulador no pot dir: temps, memòria real i port sèrie.
4. Si una cosa funciona a la placa i no a l'emulador, és un error del sistema i s'arregla; si funciona
   a l'emulador i no a la placa, és una diferència de maquinari i s'apunta a la fitxa de la placa.

## El que no s'ha de fer

- No afegir a l'emulador cap comoditat que la placa no tingui (per exemple, fer que `console.read_line`
  torni de seguida). L'emulador ha de ser **avorrit**: si divergeix, deixa de servir.
- No fer servir l'emulador per amagar un `#include <Arduino.h>` que s'ha colat al nucli. El `grep` del
  `80 · Compilació i proves` és el que ho vigila.
