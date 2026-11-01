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
| `--now <època>` | Fixa l'hora d'arrencada. **Sense aquesta opció no hi ha època**: el rellotge val 0 i `time.caps().source` és `"none"`, com una placa que mai s'ha sincronitzat |
| `--time-scale <N>` | Accelera el temps N vegades (1000 = un dia en 86 segons) |
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
| `--display` | **Un vidre**: el panell del Paper, 960x540 i quatre bits per píxel, en un buffer d'aquest costat (`emu::MemorySurface`), **i la capacitat `display` amb ell** —`sys.caps().display` diu la veritat, que no deia fins a la 0.19.0: el vidre hi era i el banc deia que no—. Sense l'opció la placa no té pantalla, que és el que volen gairebé totes les proves (docs/30, docs/50) |
| `--gui` | **El vidre en una finestra X11**, pintada cada cop que el panell es pinta: el mateix buffer del `MemorySurface` i **la pàgina com la dibuixa l'app** (540x960), no el vidre ajagut, amb els setze nivells en els colors del paper. Porta `--display` amb ell (una finestra de res no és una finestra), s'obre només quan hi ha un `DISPLAY` i, si no n'hi ha, el banc ho diu i segueix sense finestra. La finestra ensenya **el que el vidre mostra** i no el que es va pintar, o sigui que el fantasma d'un `du` s'hi veu; l'únic que no hi és és el parpelleig d'`a2` (docs/85) |
| `--no-ink` | **Un vidre que canvia píxels i no mou tinta**: una pintada és instantània i exacta, la pàgina és la que s'ha pintat sense res del que hi havia abans i **no s'envia res al bus**. És el que fan servir les proves —no paguen mig megabyte de buffers, ni mig segon de tinta, ni un segon de bus per pàgina— i el que queda si es vol comparar el dibuix amb el d'abans del model (docs/85) |
| `--touch` | **El panell tàctil del model**: un GT911 en el marc d'aquesta unitat (540x960, dos dits) que **costa el que costa el bus** —800 µs per torn sense dit, 2 550 amb dos— i que **dona el marc una vegada**. Porta la capacitat `touch` amb ell; sense l'opció el banc és una Paper amb un panell que no contesta, que és el que vol una prova que no és sobre el tàctil (docs/50, docs/85) |
| `--touch-scan <ms>` | Cada quant **mostreja** el panell: `0` és el del xip (10 ms) i un número més gran que el torn del nucli és un vidre que contesta «ningú» a la meitat de les lectures, que és exactament el cas que es vol poder reproduir (docs/85) |
| `--no-delay` | **Cap espera**: el model del disc, del port, de la tinta i del tàctil és el mateix i el rellotge no es toca. És el que fa servir una prova o una mesura que no és de temps, i el que un run amb `--gui` **no pot** demanar (docs/85) |
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

## Una sola sortida, i en ordre

El nucli escriu les seves línies amb `printf` i la consola amb `IConsole::write`. A la placa les dues
surten pel mateix UART, o sigui que l'ordre és l'ordre en què s'han escrit; al banc són dos fluxos
diferents i les línies de l'arrencada sortien **després del prompt** i després del que havia escrit una
app (mesurat l'1/11). La consola del banc buida `stdout` abans d'escriure, i així el que es veu és el
mateix que veuria una persona amb la placa endollada (docs/85).

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

## La finestra: mirar el vidre en lloc d'endevinar-lo (1/11)

Sense finestra, el vidre del banc és un buffer que només veuen les proves. Amb **`--gui`** el mateix
buffer es copia a una finestra **X11** cada cop que el panell es pinta —es pregunta pel comptador de
pintades del `MemorySurface`, o sigui que un torn sense pintada no costa ni una conversió— i el que l'app
ha dibuixat es veu:

```
$ .pio/build/emulator/program --board m5stack-paper --gui --script guio.txt
gui: the page 540x960 is in a window at half, one pixel of the window is 2 of the page
```

- **És X11 i res més**: l'enllaç és un `-lX11` i el codi viu darrere de `-DLUNOK_GUI_X11`, que és a
  `platformio.ini` a l'entorn de l'emulador. Una màquina sense Xlib treu aquelles dues línies i el banc
  compila i corre igual, dient que no té finestra.
- **La finestra ensenya la pàgina, no el vidre ajagut**: el `MemorySurface` viu en el marc en què l'app
  dibuixa (540 × 960 amb la pàgina de fàbrica), i la finestra fa el mateix. El vidre físic és 960 × 540 i
  el gir és cosa del conductor, no de la finestra.
- **I ensenya el que el vidre mostra, no el que es va pintar** (`shown_bits()`): amb el model de tinta,
  el fantasma d'un `du` i el gra s'hi veuen, que és exactament el que una persona tindria a les mans. Amb
  `--no-ink` els dos buffers són el mateix i la finestra ensenya la pintada neta.
- **L'escala la tria la finestra, i la diu**: el gestor de finestres no dona una pàgina de 960 punts
  d'alçada, i una pàgina retallada pel marc és pitjor que una pàgina sencera a la meitat. La finestra
  agafa **l'escala entera més petita que hi cap** (1 a 4, amb marge per a la barra del gestor) i ho diu a
  la primera línia: en aquesta màquina, 540 × 960 queden en **270 × 480** i cada píxel de la finestra és
  un bloc de 2 × 2 de la pàgina. El pas de quatre bits a píxel i l'escala són la funció pura
  (`emu::gui_expand`) que té prova a `test_ui`, perquè és la part que es pot provar sense pantalla.
- **Tancar la finestra acaba el run**, com tres ESC.
- **El que no s'hi veu**: el **temps** de la tinta, que només es nota perquè la finestra s'espera i el
  prompt no torna —el número es llegeix amb `stats` o amb `run panel`, no a l'ull—, i el moviment del
  pigment dins de cada moment (la finestra ensenya blanc, negre i la imatge, no els passos entremig). Per
  això la finestra serveix per **veure què dibuixa una app**, i el que es veu a l'ull continua essent el de
  la placa (docs/95).

## La tinta del vidre: la pàgina que ningú ha vist (1/11)

Un panell d'e-ink no encén píxels: mou pigment. El que hi havia **no desapareix en una pintada**, i
només les qualitats que netegen (`gc16` i companyia) tornen la pàgina. El banc ho simula amb una taula i
una barreja d'enters (`lib/hal_emulator/src/emu_ink.cpp`):

| Qualitat | Quant tarda | Què deixa |
|---|---|---|
| `gc16`, `gl16`, `glr16`, `gld16` | **450 ms** | res: netegen, i la brutícia del píxel torna a zero |
| `du` | **120 ms** | un quart del que hi havia, i deixa un punt de brutícia |
| `du4` | 120 ms | més: tres vuitens, i el doble de brutícia |
| `a2` | 290 ms | gairebé res, i **parpelleja**: la pàgina va blanca, després negra, i aleshores arriba la imatge |
| `init` | 2000 ms | tot, i la brutícia del panell sencer |

- **La brutícia s'acumula**: cada pintada de `du` afegeix un setzè al píxel i fa que el rastre del que hi
  havia sigui més fort; una de `gc16` el torna a zero. És el que es veu en una pàgina pintada deu cops
  amb `du` i no amb `gc16`.
- **El gra**: una de cada mil píxels d'una pintada de `du` no arriba al nivell que se li va demanar (una
  de cada cinc-centes amb `du4`), i el píxel s'hi queda fins que alguna cosa neteja la zona. És
  determinista —un hash del lloc i no un `random`— perquè una pàgina que canvia a cada tirada no es pot
  provar.
- **I la pintada ocupa el seu temps**: `idle()` no diu que sí fins que el rellotge del banc passa el bus
  de la regió **i** la durada de la qualitat, o sigui que `run panel` al banc dona el número de la placa
  (vegeu el bus, a sota).
- **El color és el del paper i no el d'una pantalla**: el nivell 15 és `#EFEADC` (paper calent), el 0 és
  `#1A1B18` (la tinta més fosca que arriba el pigment) i els catorze del mig són la recta entre els dos.
  El panell no té color i la finestra no n'inventa cap.
- **El parpelleig d'`a2`, en tres moments**: el primer terç del temps és la pàgina **blanca**, el segon és
  **negra** i l'últim és la imatge arribant. `step_ink()` és el que els mou, i el demanen les dues coses
  que miren el vidre: `idle()` —una app que espera la tinta espera els tres moments— i la finestra, que
  per això redibuixa també quan canvia el comptador de blincades (`ink_steps()`) i no només quan canvia
  el de pintades. Un parpelleig que no es dibuixa no és un parpelleig. **I és el parpelleig del full del
  fabricant i no d'aquesta unitat**: a la placa, `a2` es va mesurar com a no pintar res (docs/95, E-7), o
  sigui que aquí el banc és més optimista que el vidre que té algú a les mans.

### El bus, que no és la tinta i també es paga (1/11)

Entre l'app i la tinta hi ha el cable. El driver envia **la regió que canvia** al controlador abans de
demanar-li que pinti (`It8951::flush`), i aquell enviament té dos preus: els bytes i les files.

| Peça | Quant costa | D'on surt |
|---|---|---|
| Un byte | **0,8 µs** | el bus del driver, **10 MHz** (`kSpiHz`): quatre bits per píxel, o sigui dos píxels per byte |
| Una fila | **1 100 µs** | **d'una mesura** i no d'un full del fabricant: la pàgina de `run panel` va costar **1 985 ms** a la placa l'1/11, i els seus comptes —570 ms de tinta, 233 ms de cable i 1 080 files— deixen uns 1 095 µs per fila. El driver envia **una fila per transacció**, i per què una transacció costa un mil·lisegon fora del cable no es respon aquí (docs/95) |

- **La pàgina sencera són 1 227 ms de bus**: 932 files de 270 bytes a 0,8 µs més les 932 transaccions (la
  vista d'una app fa 540 × 932, sota les 28 files de la barra). La barra dels setze nivells, 157 ms. Amb
  la tinta (570 ms), la pàgina de `run panel` dona **1 954 ms** al model i **1 970 ms mesurats al banc**
  —`stats` diu 1 240 ms pel pintat de `du`, 280 pel de `gc16` i 450 de tinta—, contra els **1 985 ms** de
  la placa: **un 0,8 %** (docs/95, E-7).
- **I el que `run panel` imprimeix al banc és més gran, i amb raó**: 2 469 ms, perquè la **barra del
  sistema** es repinta amb `gc16` mentre l'app espera la tinta —un `present` de 477 ms, que és 450 de la
  tinta de la barra més el seu enviament— i `ui.idle()` de l'app espera també aquella tinta. És el que fa
  la placa quan la barra canvia; a la mesura de l'1/11 la barra no es va moure i per això no hi és.
- **I una pintada espera l'anterior**: un `present` de dues qualitats és **dos pintats**, i el segon no
  envia res fins que el primer ha acabat la tinta (`wait_paint`, docs/50). És el que fa que la pàgina sigui
  la **suma** —1 263 de bus del `du`, 120 de la seva tinta, 157 de bus del `gc16` i 450 de la seva— i no
  una cursa entre dues coses que al vidre van d'una en una.
- **I el bus atura el nucli, com a la placa**: el banc fa l'espera amb un `usleep` dins del `flush` i el
  fil no avança, que és el que passa mentre el bus està ocupat. És el mateix criteri que el port sèrie a
  115 200 baud i que les lectures del disc: el cost s'hi paga, no es desa en un número.
- **Per això una pàgina al banc es nota**: amb `--gui`, una pintada de pàgina sencera té la finestra quieta
  més d'un segon. No és un defecte de la finestra: és el que fa la placa.

**`--no-ink`** treu el model i deixa el vidre de sempre: pintada instantània i exacta **i cap enviament**
(un vidre que canvia píxels no té res a enviar). És el que fan servir les proves —que no han de pagar mig
megabyte de buffers, mig segon de tinta ni un segon de bus per pàgina— i és el que queda si algú vol
comparar el dibuix amb el d'abans.

**Quant costen aquests dos buffers**: el model guarda *el que el vidre ensenya* i *la brutícia de cada
píxel*, quatre bits cada un, o sigui **259 200 B per buffer** a la pàgina de fàbrica —**518 400 B els
dos—**. És memòria del PC i no de la placa, però és el motiu pel qual **el model neix apagat a les
proves**: `set_ink(true)` és el que diu una prova que vol mirar la tinta, i la resta no paguen el mig
megabyte ni el mig segon d'una pàgina.

**El que continua sense ser el panell**: la tinta no es mou davant dels ulls —el vidre passa d'un moment
al següent sense cap pas entremig, i el parpelleig és de tres fotogrames i no d'una corba—, el cost d'una
fila no està descompon·gut (1 100 µs és el que surt dels comptes d'una mesura, no una mesura d'ençà), i
el temps de la tinta i els del full del fabricant no s'han vist en aquesta unitat. Tot el que és model és
en un fitxer (`emu_ink.cpp`), perquè un dia a la placa el canviï (docs/95).

## El tàctil: un dit que costa el que costa (1/11)

El vidre del banc tenia ulls i no dits. Amb **`--touch`** el model de la Paper té el seu **GT911**, i el
que se simula no és «on és el dit» sinó el xip: en aquesta placa el que escanya una app és el que hi ha
entre el dit i el full de dades (`lib/hal_emulator/src/emu_gt911.cpp`, docs/50).

| Què | El banc | D'on surt |
|---|---|---|
| El marc | **540 × 960**, i és el de la pàgina: un dit a (100, 200) és a (100, 172) de la vista, sota les 28 files de la barra | la configuració d'aquesta unitat, mesurada amb quatre tocs l'1/11 (docs/95, E-6) |
| Els dits | **2**, no els cinc que caben al marc | `0x804C` = 2 a la config 66 d'aquesta unitat |
| La mida del contacte | **28** al primer dit i **30** al segon | els dos números que va donar la placa amb dos dits (docs/95, E-6) |
| Què costa una lectura | **800 µs** sense dit, **1 830** amb un i **2 550** amb dos: l'estat, el marc i el zero que el tanca, a 100 kHz i nou bits per byte. Mesurat al banc, `touch.read` diu **935 µs – 1,1 ms** per a una lectura de 800: l'espera és un `usleep` i el planificador del PC hi afegeix la resta | la física del bus del xip (docs/50) |
| El probe de l'arrencada | **~17 ms**, una vegada: el producte id i els 186 bytes de configuració, i la línia `[I][TOUCH] GT911 at 0x5d, config 66, 540x960, 2 fingers` | el driver de la placa (docs/50) |
| El temps entre mostres | **10 ms** (`--touch-scan`), i una lectura es pot trobar el marc ja pres | el full del fabricant; **la placa encara no ho ha comptat** (docs/95, E-6) |

**El problema de debò, i el motiu de simular-lo**: el xip **escombra sol i dona el marc una vegada**. El
driver se l'emporta amb un zero al registre d'estat, i una lectura que arriba dins de la mateixa finestra
troba **ningú** encara que hi hagi un dit a sobre. Si el que llegeix va més de pressa que el vidre —un
torn de 20 ms contra un xip de 40— la meitat de les lectures diuen que no hi ha ningú, i el que ha de
sobreviure això és el nucli, no el banc. `--touch-scan 40` és exactament aquest cas, i
`Gt911Panel::missed()` compta les vegades que el marc ja era pres.

- **Els dits són del test o del guió**, com les tecles: `emu::touch_panel()->finger_down(0, 100, 200)`,
  `finger_move`, `finger_up`. Un punt fora del marc no és un dit i es refusa; el tercer dit també.
- **El que no hi és, i s'ha de saber**: al banc **no hi ha bus**, o sigui que el tàctil no comparteix res
  amb el rellotge, la FRAM ni el SHT30 i no hi ha cap NAK ni cap conflict; el xip del banc contesta
  sempre a la primera, quan a la placa el probe va haver de preguntar a dues adreces (docs/50); i el marc
  que dona és el de fa menys de `--touch-scan` ms i no el d'un escombrat concret.
- **El que la placa ha de dir encara**: la **velocitat de mostra** (comptar marcs amb el dit, docs/95
  E-6) i **el cost d'una lectura**, que al banc és la física del bus i que `stats` ja compta a la fila
  `touch.read` a les dues bandes.

## Els temps del banc, i el commutador que els apaga (1/11)

Un run del banc **paga la mateixa estona que la placa**, perquè una app que aquí va bé i allà va malament
no serveix de res: el disc (**1 ms** per lectura, el doble a la targeta), el **port sèrie** (86,8 µs per byte
a 115 200), la **tinta** (0,8 µs per byte i 1 100 µs per fila, i la qualitat de cada pintada) i el
**tàctil** (800 µs per torn sense dit, 1 830 amb un i 2 550 amb dos). **Els quatre són un sol
commutador** (`Options::delays`), perquè un run o paga el temps del maquinari o és una prova, i qui en
paga la meitat no mesura res:

| Com s'engega | Els temps | Per a què |
|---|---|---|
| **el binari, per defecte** | **es paguen** | mirar què costa una placa: `run panel` diu **2 899 ms** de pintada i el run sencer **3,11 s** |
| **`--gui`** | **es paguen, i és obligatori** | una finestra és per veure què costa una placa; `--gui --no-delay` i `--gui --no-ink` **es refusen**, amb una línia que diu per què |
| **`--no-delay`** | **cap espera** | un guió o una mesura que no sigui de temps: el mateix `run panel` dona **24 ms** i el run sencer **0,03 s** — **104 vegades més ràpid** |
| **les proves** | **cap espera** (el camp neix fals) | cap suite paga el rellotge; la que mesura temps (`test_ui`, `test_touch`) el posa a cert |

**El model no canvia amb el commutador**: el fantasma d'un `du`, la brutícia, la mida del contacte i el
marc que es dona una vegada hi són igual. El que s'estalvia és el rellotge i no el model, que és
exactament el que vol una prova de píxels.

## Fidelitat: què s'assembla i què no

| Aspecte | A l'emulador | A la placa |
|---|---|---|
| Nucli, Lua, VFS, promeses | **Idèntic** | Idèntic |
| Resolució de camins i permisos | Idèntic | Idèntic |
| Sostre de memòria | **El de la placa, i no més**: l'ATOM contesta el sostre per defecte (64 KB, `Model::app_limit_kb` = 0, que vol dir «la placa no en diu cap») i el **Paper contesta la seva PSRAM mapejada (4 096 KB, de 8 MB que en té el xip)**. `--mem-cap` el pot **baixar** per veure-hi el pic, i demanar-ne més es refusa amb el número que té el model. `sys.heap()` dona el pressupost de l'app (`free`, `used`, `limit`, `peak`) i la memòria extra del model (`extra_total`, `extra_free`), i `mem` escriu els tres números del model (`Model::psram_kb` mapejats i `Model::psram_chip_kb` del xip) | 320 KB de SRAM al xip i, al Paper, **4 MB de PSRAM mapejada** on van els blocs de 4 KB en amunt (docs/75) |
| Cost d'una lectura | **Simulat**: cada lectura costa **1 ms** (i el doble a la targeta), o sigui que una app que llegeix un fitxer per torn va tan lenta aquí com al vidre. El número és una aproximació fins que algú el mesuri amb un cronòmetre, i es canvia amb `--read-delay <us>` (0 = cap espera) | Mil·lisegons de flash o SD |
| Targeta (SD) | **Un directori del disc, i només si el model té ranura** (el Paper): el mateix backend POSIX que la flash, o sigui que el que llegeix aquí llegeix a la placa. Es presenta com una targeta de **16 GB** amb el lliure del PC, i el que falta fins a 16 GB és l'ocupat | FATFS per SPI a la Paper: CS GPIO4, 10 MHz i el CS del panell (GPIO15) deseleccionat abans de tocar el bus (docs/50) |
| El vidre del banc (`--display`) | **Un buffer que és el panell** (`emu::MemorySurface`, 4 bits per píxel): recorda **el rectangle brut de cada pintat** —el mateix que el driver del panell envia, marcat per cada escriptura i buidat a `flush`— i la qualitat que es va demanar, i en guarda els **vuit últims**. Des de la 0.22.0 **la pintada mou tinta** —un `du` deixa un quart del que hi havia i acumula brutícia, un `gc16` neteja, una de cada mil falla un nivell— i des de la 0.23.0 **paga el bus**: 0,8 µs per byte a 10 MHz i 1 100 µs per fila, i `idle()` no diu que sí fins que han passat el bus i la tinta (docs/85). Les proves hi corren amb el model fora (`--no-ink`). **I el gir és diferent**: girar-lo torna a fer el buffer, o sigui que el banc s'oblida del que hi havia, cosa que un vidre de tinta no fa mai | El vidre de veritat: la tinta hi queda i un gir obliga a tornar a enviar la pàgina sencera. El número de `run panel` és el mateix a les dues bandes (1 985 ms a la placa, 1 970 mesurats al banc, 1 954 al model) i el que queda obert és el perquè del cost d'una fila (docs/95, E-7) |
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
| Tàctil (`touch.*`, `on_touch`) | **Un GT911 simulat** (`--touch`, `emu::Gt911Panel`): el marc de la pàgina, dos dits, la mida del contacte i les tres transaccions del bus a 100 kHz (**800, 1 830 i 2 550 µs** segons els dits). **Escombra sol i dona el marc una vegada**, o sigui que un lector més ràpid que el vidre llegeix «ningú» la meitat de les vegades (docs/85) | El GT911 de debò: el mateix protocol, i a sobre un bus que comparteix amb el rellotge, la FRAM i el SHT30, amb clock stretching de fins a 6 ms del sensor i NAKs possibles |

**Comprovat al banc el 31/10 amb la 0.8.2**, amb `--board m5stack-paper --sd <dir>` i un guió:
`mem` diu `extra memory 4096 of 4096 KB usable, of 8192 KB on the chip (psram)`, `mounts` diu
**`mounts: 9`** (amb `/rtc` i `/fram` entremig, i `/nvs` donant `entries 1`), `run mtab` en llista
nou amb els bytes de cadascun, i `help` ensenya `sleep at <epoch seconds>`. La línia `free heap` **no
hi és al PC** i `mem` ho diu (`heap not available on this build`): la RAM interna del xip no té un
equivalent al banc, i inventar-ne un número seria pitjor que dir-ho.

**I la capa de controls diu el mateix a les dues bandes** (1/11, 0.19.0): `run layers` dona **les sis
línies iguals** al banc amb `--display` i a la Paper —els comptes `1 / 1 / 2`, el pare al slot 3, el marc
sense vista i `canvas (gone)`—, o sigui que la composició i el dany que es proven aquí són els que passen
allà (docs/95, E-8). L'única cosa que el banc no pot dir és el **temps de tinta**, que és justament el que
`docs/95` encara té pendent.

**El que encara no fa.** El maquinari fals **ja segueix el model** —capacitats, llums, nombre de tecles,
sensor, indicador de bateria, ranura i FRAM surten de `emu::model()`—, i el que queda és la **forma de
les tecles**: el dial compost de la Paper és el de tots tres models, quan el model de l'ATOM n'hauria
de tenir una de simple a la cara de dalt. També falta poder prémer una tecla **a mà**: `emu::press_key`
només el criden les proves. Les dues coses són a [`90 · Full de ruta`](90-full-de-ruta.md), M8.

**I el log del banc és el de la placa** (1/11, 0.20.0): les mateixes línies amb la mateixa forma
(`[I][CFG] /cfg on littlefs (asked for littlefs)`, `[I][UI] view 540x932 at 0,28, 251640 bytes of
canvas`) i els mateixos comptadors, amb una diferència que és honesta i s'ha de saber: **al banc el temps
d'un dispositiu és el del PC**. `vfs.list` pot costar 40 ms aquí per la lectura simulada d'1 ms per
fitxer. Des de la 0.22.0 una pintada **sí que costa** el que costa la seva qualitat —450 ms un `gc16`,
120 un `du`— i des de la 0.23.0 hi entra també el bus de la regió, 0,8 µs per byte i 1 100 µs per fila,
que és el que fa que `glass.flush` a `stats` ensenyi mig segon per una pàgina. El que es mesura aquí és
**quantes vegades** es demana una cosa i quin camí es pren; el que costa de debò amb el maquinari de debò
es mesura a la placa (docs/95).

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
