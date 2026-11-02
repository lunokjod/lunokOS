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
.pio/build/emulator/program --board m5stack-paper
```

**La placa és obligatòria i diu el maquinari**: `--board m5stack-paper` dona el banc amb vidre, GT911,
targeta, FRAM, rellotge amb pila i bateria; sense l'opció el binari es refusa i en llista les tres.

**El prompt s'edita** (2/11, 0.46.0): esquerra i dreta mouen el cursor per la línia, amunt i avall
recorren les **vuit ordres** més recents (i baixar més enllà de la més nova torna el que s'estava
escrivint), i Inici/Fi/Supr també hi són. La línia es redibuixa amb codis ANSI quan la consola els
entén; si no, amb espais i retrocessos, o sigui que el terminal de la placa i un `TERM` sense ANSI
fan el mateix amb mitjans diferents (docs/30, docs/75). Al prompt `lua>`, **Ctrl+D** tanca la sessió
de l'intèrpret igual que `exit`; la línia que s'estigués escrivint es descarta.

Aquestes són les opcions del binari:

| Opció | Què fa |
|---|---|
| `--flash <dir>` | El directori que **és** el volum de la flash (per defecte `data`, el del repositori): `/littlefs/apps` és `data/apps` i no es copia res |
| `--state <dir>` | On viuen les memòries que no són un volum: `nvs/` i, quan n'hi hagi, la RTC i la FRAM (per defecte `emulator/`) |
| `--fs <dir>` | **Recanvi** del disc quan no hi ha `--flash`: `Options` hi cau si `flash_dir` és nul (`emu_storage.cpp`). El binari sempre acaba de posar `--flash` a `data`, o sigui que **des del binari no fa res**; el fan servir les proves, que es muntin les seves opcions |
| `--sd <dir>` | **La targeta**: aquest directori n'és el volum i es munta a `/sd`. Per defecte `emulator/sd`, i el banc el crea si el model té ranura; si s'esborra, la ranura queda buida (docs/70) |
| `--battery-mv <mv>` | **Una bateria de mentida**: el que llegeix `power.battery()`. **Mana sobre la de la màquina** (la del punt de sota), i el `%` no es posa: surt de la corba de `hal/battery.h`, la mateixa que fa servir el Paper. Un model **sense indicador** (els ATOM) dona `nil` encara que se li digui un número, que és el que faria la placa (docs/30) |
| `--battery-host` | **La bateria d'aquesta màquina, prestada a la placa, i la demana poques vegades perquè ja ve sola**: un model amb indicador llegeix el `%` de `/sys/class/power_supply` a l'arrencada sense cap bandera i el passa per la **corba de la cel·la del model** (1150 mAh, 4,2 V plena), o sigui que 99 % d'aquest portàtil són **4 190 mV del Paper** i no 12 V d'una bateria de tres cel·les. Si la màquina no en té cap, la cel·la comença al **seu càrrec nominal (3 700 mV, 42 %)** i el banc ho diu, perquè una placa amb indicador sempre llegeix alguna cosa. La bandera serveix per **demanar-ho en veu alta** (i perquè se't digui que un ATOM no té indicador); amb `--battery-mv` alhora, el banc el refusa (docs/50, docs/85) |
| `--board <nom>` | **Quina placa és el banc, i és obligatori**: `m5stack-atom`, `m5stack-atom-matrix` o `m5stack-paper`. `sys.board` respon aquest nom, `sys.emulator` continua cert, i `boards` del manifest es compara amb aquest i no amb els tres (docs/50). **El maquinari és el del model**: un ATOM té un llum i una tecla, la Matrix el panell de 25 i el sensor, i el Paper les tres tecles, l'indicador de bateria, la targeta i el rellotge amb pila — el mateix que diu la fitxa de docs/50, ni més ni menys |
| `--mem-cap <KB>` | **Baixa** el sostre de la VM (mai per sobre del que té el model, que es refusa: 320 KB als ATOM i 4 096 KB al Paper, docs/75): posa-hi el del model per veure-hi el pic |
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
| `--display` | **El vidre del model, que hi és sol**: el panell del Paper, 960x540 i quatre bits per píxel, en un buffer d'aquest costat (`emu::MemorySurface`), **i la capacitat `display` amb ell** —`sys.caps().display` diu la veritat, que no deia fins a la 0.19.0: el vidre hi era i el banc deia que no—. Qui ho diu és el model (`Model::glass`): el Paper en té i els ATOM no, i un `--display` a un ATOM es refusa dient que aquella placa no té pantalla. La bandera serveix perquè un guió digui que necessita vidre; el que apaga és `--no-display` (docs/30, docs/50) |
| `--no-display` | **Una placa sense pantalla, amb tota la resta del model**: només consola, i sense els **518 400 B** dels dos buffers de la pàgina. És el que vol una mesura que no és de píxels (docs/85) |
| `--gui-scale <1..4>` | **Quants píxels de la pàgina és un píxel de la finestra**, forçat: `1` és la pàgina tal com és i `2` és la pàgina a la meitat, encara que la pantalla la hi deixi sencera. Sense l'opció la finestra tria **l'escala entera més petita que hi cap** (1 a 4), que és el que fa que una pantalla petita ensenyi la pàgina sencera i no un tros. No canvia la resolució del panell del model (540×960): només com es veu (docs/85) |
| `--gui` | **Una sola finestra amb tot el que la placa té per tocar i per veure** (0.34.0). Si el model té vidre, **la pàgina en una finestra (SDL2, o X11 de reserva)**, pintada cada cop que el panell es pinta —el mateix buffer del `MemorySurface` i **la pàgina com la dibuixa l'app** (540x960), amb els setze nivells en els colors del paper— i **les tecles al costat que diu el descriptor de l'entrada**, un botó per tecla del model; una placa sense pantalla en treu només les tecles, que és el que sí que té. La finestra ensenya **el que el vidre mostra** i no el que es va pintar, o sigui que el fantasma d'un `du` s'hi veu; l'únic que no hi és és el parpelleig d'`a2`. El ratolí és el dit a la pàgina i és el dit que prem a un botó. No es pot combinar amb `--no-delay`, ni amb `--no-ink` quan hi ha vidre; s'obre quan hi ha una pantalla on obrir-la i, si no n'hi ha, el banc ho diu i segueix sense finestra (docs/85) |
| `--no-ink` | **Un vidre que canvia píxels i no mou tinta**: una pintada és instantània i exacta, la pàgina és la que s'ha pintat sense res del que hi havia abans i **no s'envia res al bus**. És el que fan servir les proves —no paguen mig megabyte de buffers, ni mig segon de tinta, ni un segon de bus per pàgina— i el que queda si es vol comparar el dibuix amb el d'abans del model (docs/85) |
| `--touch` | **El tàctil del model, que hi és sol**: un GT911 en el marc d'aquesta unitat (540x960, dos dits) que **costa el que costa el bus** —800 µs per torn sense dit, 2 550 amb dos— i que **dona el marc una vegada**, amb la capacitat `touch`. Qui ho diu és el model (`Model::touch`): el Paper en té i un `--touch` a un ATOM es refusa. El que apaga és `--no-touch`, i llavors el panell d'aquella Paper és un xip que no contesta, que és el que vol una prova que no és sobre el tàctil (docs/50, docs/85) |
| `--no-touch` | **Cap dit**: el panell del model no es llegeix i no costa els 800 µs de cada torn (docs/85) |
| `--touch-scan <ms>` | Cada quant **mostreja** el panell: `0` és el del xip (10 ms) i un número més gran que el torn del nucli és un vidre que contesta «ningú» a la meitat de les lectures, que és exactament el cas que es vol poder reproduir (docs/85) |
| `--no-delay` | **Cap espera**: el model del disc, del port, de la tinta i del tàctil és el mateix i el rellotge no es toca. És el que fa servir una prova o una mesura que no és de temps, i el que un run amb `--gui` **no pot** demanar (docs/85) |
| `--lua-cpu <ns>` | Què costa **una instrucció de l'intèrpret** a 160 MHz: **60 ns** per defecte (el model; la mesura de la placa encara es deu, docs/95), i `0` perquè el codi de l'app sigui gratis, que és el que era el banc fins a la 0.29.0. Es divideix per la freqüència simulada, o sigui que a 240 MHz una instrucció costa 40 ns i a 80, 120 |
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

**La bateria al banc ve de fora, i ve sola**: a la placa és un divisor connectat a un pin, i aquí no hi ha
ADC; el que hi ha és **el `%` d'aquesta màquina** (`/sys/class/power_supply/*/capacity`), que un model amb
indicador es presta a l'arrencada sense que ningú ho demani i que passa per la corba de la cel·la del
Paper. Una màquina sense bateria (un sobretaula) deixa la cel·la al **seu càrrec nominal, 3 700 mV**, i ho
diu; `--battery-mv` hi posa el número que vulguis i mana sobre el préstec.

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
buffer es copia a una finestra **SDL2** —o X11, si el build no té SDL2— cada cop que el panell es pinta
—es pregunta pel comptador de pintades del `MemorySurface`, o sigui que un torn sense pintada no costa ni
una conversió— i el que l'app ha dibuixat es veu:

```
$ .pio/build/emulator/program --board m5stack-paper --gui --script guio.txt
gui: the page 540x960 is in a window at half, one pixel of the window is 2 of the page
```

- **La finestra és SDL2 i té X11 de reserva**: el codi viu darrere de `-DLUNOK_GUI_SDL` o de
  `-DLUNOK_GUI_X11`, i `tools/select_gui.py` tria el primer que troba. Amb SDL2 la mateixa finestra
  funciona a X11, Wayland, macOS i Windows; Xlib continua sent el camí de les màquines POSIX que no
  tenen SDL2. Es pot forçar amb `LUNOK_GUI=sdl`, `LUNOK_GUI=x11` o `LUNOK_GUI=none`; per a una còpia de
  SDL2 que `pkg-config` no conegui, `SDL2_CFLAGS` i `SDL2_LIBS`. Sense cap de les dues, el banc compila
  i corre igual, dient que no té finestra. A Debian/Ubuntu la dependència de desenvolupament és
  `libsdl2-dev`; a Windows i macOS, el paquet SDL2 del compilador que es faci servir.
- **La geometria no depèn del backend**: els colors del paper, `gui_expand`, `gui_page_point`, l'escala
  que hi cap i la geometria de les tecles viuen a `gui_common.cpp`, i el backend només obre la finestra,
  dibuixa i tradueix els esdeveniments. Les proves de `test_ui` en proven la part que no necessita
  pantalla. `pio test` no enllaça ni SDL2 ni X11: compila el backend buit de `gui_none.cpp`.
- **El que encara és POSIX**: la consola (`termios`/`select`) i el disc (`dirent`/`statvfs`) no són
  portables encara; SDL només és la finestra. La fase següent és la capa d'amfitrió comuna per a
  consola, fitxers, rellotge i bateria, amb una implementació Win32 a més de la POSIX. Fins que hi
  sigui, el banc corre sencer a Linux i macOS, i a Windows només quan aquestes peces hi hagin estat
  portades.
- **La finestra ensenya la pàgina, no el vidre ajagut**: el `MemorySurface` viu en el marc en què l'app
  dibuixa (540 × 960 amb la pàgina de fàbrica), i la finestra fa el mateix. El vidre físic és 960 × 540 i
  el gir és cosa del conductor; si el gir canvia el marc de la pàgina, **la finestra es torna a obrir amb
  el marc nou** en comptes de quedar-se amb l'últim fotograma (2/11).
- **I ensenya el que el vidre mostra, no el que es va pintar** (`shown_bits()`): amb el model de tinta,
  el fantasma d'un `du` i el gra s'hi veuen, que és exactament el que una persona tindria a les mans. Amb
  `--no-ink` els dos buffers són el mateix i la finestra ensenya la pintada neta.
- **L'escala la tria la finestra, i la diu**: el gestor de finestres no dona una pàgina de 960 punts
  d'alçada, i una pàgina retallada pel marc és pitjor que una pàgina sencera a la meitat. La finestra
  agafa **l'escala entera més petita que hi cap** (1 a 4, amb marge per a la barra del gestor) i ho diu a
  la primera línia: en aquesta màquina, 540 × 960 queden en **270 × 480** i cada píxel de la finestra és
  un bloc de 2 × 2 de la pàgina. **`--gui-scale <1..4>` força una escala concreta** —`2` és la pàgina a la
  meitat— encara que la pantalla la hi deixi sencera, i la finestra ho diu igualment. El pas de quatre bits
  a píxel i l'escala són la funció pura (`emu::gui_expand`) i la tria ho és també (`emu::gui_fit_scale`),
  totes dues amb prova a `test_ui`, perquè és la part que es pot provar sense pantalla.
- **La finestra de prova no és la de l'usuari** (2/11): `--gui` necessita un `DISPLAY`, i el que rep ha
  de ser **un X propi de la prova**, no el de la persona que treballa. La manera curta és `xvfb-run`,
  que encén un X virtual, hi apunta `DISPLAY` i el tanca en sortir:

  ```sh
  xvfb-run -a -s "-screen 0 800x600x24" ./.pio/build/emulator/program \
    --board m5stack-paper --gui --run label
  ```

  Per a una sessió amb captures o clics, s'encén `Xvfb :99` a part i s'hi executa el banc amb
  `DISPLAY=:99`; així no es mou el ratolí de l'usuari, no es roba el focus i no s'hi obre cap finestra.
  La mida de la pantalla virtual decideix l'escala automàtica (800x600 deixa la pàgina a la meitat;
  1920x1080 la deixa sencera), o sigui que una prova gràfica ha de fixar la mida que vol o passar
  `--gui-scale`. `pio test` no obre cap finestra: no enllaça SDL2 ni X11.
- **Un gir de la pàgina torna a obrir la finestra** (2/11): si `ui.rotation` canvia el marc del vidre,
  `gui_show` tanca la finestra i l'obre amb la pàgina nova —les tecles i l'escala demanada no canvien— i
  la pàgina girada torna a prendre l'escala que hi cap. El que no es fa és deixar l'últim marc congelat a
  la pantalla mentre el vidre ja és un altre rectangle (docs/30, docs/85).
- **Tancar la finestra acaba el run**, com tres ESC.
- **El ratolí és un dit** (0.32.0): la finestra és el vidre i el vidre d'aquesta placa s'hi toca. Prémer el
  botó esquerre és el dit que baixa **al píxel de la pàgina** que hi ha sota el punter, moure'l amb el botó
  premut és el dit que es mou i deixar-lo anar és el dit que puja (`emu::Gt911Panel`, el mateix xip que
  llegeixen les proves). El píxel de la finestra és un bloc de 2 × 2 de la pàgina i el dit cau **al mig del
  bloc** (`emu::gui_page_point`, amb prova a `test_ui`); un punter fora de la pàgina s'enganxa a la vora.
  Sense `--touch` no s'hi envia res, i amb el vidre girat en un altre marc el banc ho diu una vegada en
  comptes de callar. **El que el ratolí no pot fer és un pessic de dos dits**: això és del test o del guió.
- **La finestra és una placa i no un full blanc** (0.35.0): la pàgina és el panell i és clara; tot el que no
  és la pàgina —la tira de les tecles i el que queda de finestra— es pinta amb el **gris fosc de la carcassa**,
  i els botons són grisos amb el text clar. Un botó **premut es pinta amb el color del paper**, o sigui que un
  clic té una resposta que es veu; i amb `--verbose` cada tecla deixa una línia (`gui: key 0 (Centre) down` i
  el seu `up`), que és el que distingeix un botó que no fa res d'un botó que ningú no ha premut.
- **Les tecles, a la mateixa finestra** (0.33.0 → 0.34.2): el vidre a la pantalla no deixa prémer el dial, i
  `run keys` demanava una tecla que ningú no podia donar. Ara hi ha **un botó per tecla del model** dins de la
  finestra del vidre, col·locat **on ho diu el descriptor de l'entrada**: `IInput::face()` dona la cara de la
  tecla i `IInput::spot()` la seva posició, i el banc en fa una **columna a aquell costat** —el dial del
  Paper és a la cara **dreta**, amb `Spot::kUp` a dalt i `Spot::kDown` a baix (docs/50)— o una **fila sota la
  pàgina** quan la tecla és a la cara de dalt, com la d'un ATOM. És **la mateixa finestra que el vidre**: la
  pàgina es pinta al costat que les tecles deixen lliure i no hi ha res a col·locar ni dues coses per mirar;
  una placa sense vidre té les tecles i prou.
  Prémer és `press_key(key, true)` i deixar anar `press_key(key, false)` —el camí de la prova i del guió— i
  el botó es pinta ple mentre és premut, perquè una tecla que baixa i puja sense dir res no es comprova a
  l'ull. **Un clic a un botó és una tecla i un clic a la pàgina és un dit**, i és la posició del punter la que
  ho diu. Els botons es mesuren en píxels de finestra i no de pàgina, o sigui que no s'encongiran amb
  l'escala; **els buits entre botons no premen res** i el mapa del clic a tecla (`emu::gui_key_at`) té prova
  a `test_ui`.
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
| `du` | **120 ms** | **casi un terç** del que hi havia (cinc setzens), i deixa un punt de brutícia |
| `du4` | 120 ms | **set setzens**, i el doble de brutícia |
| `a2` | 290 ms | gairebé res, i **parpelleja**: la pàgina va blanca, després negra, i aleshores arriba la imatge |
| `init` | 2000 ms | tot, i la brutícia del panell sencer |

- **El rastre és el que hi havia i no una boira**: el que queda és una part del camí que el píxel hauria
d'haver fet, o sigui que **el fantasma d'una pàgina de setze nivells és aquella pàgina a menys
contrast** —el fosc encara fosc, el clar encara clar— i el d'una pàgina plana és una pàgina plana una mica
grisa. És el que es veu al vidre: una foto redibuixada deixa la foto, i un text redibuixat deixa el text.
La prova que ho separa és `test_the_trace_of_a_rich_page_is_richer_than_a_flat_one`: un esglaó de setze
nivells pintat amb `du` deixa **cinc nivells** de recorregut a les bandes, i la mateixa pintada sobre una
pàgina plana en deixa **zero**.
- **La brutícia s'acumula i té sostre**: cada pintada ràpida afegeix un setzè de brutícia al píxel i fa el
rastre més fort —fins a **onze setzens**, que és el que anomenem imatge enganxada—, però **no es
congela**: una pàgina que es queda quieta no és una pàgina, i el que fa el vidre és embrutar-se. Una de
`gc16` torna la brutícia a zero.
- **El gra**: **vuit de cada mil** píxels d'una pintada de `du` no arriben al nivell que se'ls va demanar
(quinze de cada mil amb `du4`), i **una de cada quatre vegades la fallada és de dos nivells** i no d'un.
El píxel s'hi queda fins que alguna cosa neteja la zona. És determinista —un hash del lloc i no un
`random`— perquè una pàgina que canvia a cada tirada no es pot provar.
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
- **I el codi de l'app també costa** (0.29.0): la placa executa cada instrucció de l'intèrpret a
aquests milisegons, i el PC a un gigahertz, o sigui que un bucle que al vidre dura un segon aquí dura
un mil·lisegon. El ganxo de l'intèrpret en compta **dos mil** i en paga el preu 
(`--lua-cpu`, **60 ns** a 160 MHz, dividit per la freqüència simulada) quan la suma passa de mig
mil·lisegon, i el que queda es paga al final del torn; amb això, `lua.own` i `lua.run` de `stats` diuen
a la placa i al banc el mateix ordre de número. És el número del model i no una mesura: es pren a la
placa amb un bucle i la fila `lua.own` (docs/95).
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

## Els tipus de lletra: un fitxer de text, i res més (1/11, 0.36.0)

El text del sistema tenia una sola lletra —5 × 7, només majúscules, 295 bytes de taula a la flash— i ara
**una cara és una cosa que es pot carregar**: `ui.load_font('/res/fonts/el-que-sigui.lfont')`. El format és
text pla, un glif per bloc i `#` per tinta, o sigui que s'escriu amb qualsevol editor i el sistema no porta
cap descodificador.

```
; un tipus de lletra de lunok, un glif per bloc
lunok-font 1
width 3
height 5
gap 1
char 48
###
#.#
#.#
#.#
###
```

| Directiva | Què diu |
|---|---|
| `lunok-font 1` | La primera cosa del fitxer, una vegada. Una altra versió es refusa |
| `width <1..8>` | Columnes d'un glif. Ha de ser abans del primer `char` |
| `height <1..8>` | Files d'un glif, i les línies que es llegeixen després de cada `char` |
| `gap <0..4>` | Columnes en blanc entre dos glifs |
| `char <codi>` | El codi que dibuixen les `height` línies següents, **en ordre i sense forats** |

- **Un fitxer que no és una cara es refusa amb el motiu**, i el motiu és el que l'app ha d'ensenyar a qui l'ha
  escrit: `not a lunok font`, `width is one to eight`, `a glyph has fewer rows than the face is tall`, `the
  glyphs of a face come in order`, `a face with no glyph`. Una línia que no és cap directiva tampoc
  s'ignora: una errata en un fitxer és una cara que no funciona i això s'ha de dir.
- **Una cara pot ser parcial**: un codi que no hi és és un forat i la línia avança igual, de manera que una
  cara de deu xifres és legal i el text del voltant no es desplaça.
- **El que costa**: `96 × 8 = 768` bytes per cara, demanats a la memòria extra i comptats al pressupost de
  l'app (docs/75), més el text del fitxer mentre es llegeix (el bloc es torna de seguida). La cara torna quan
  l'app fa `font:release()` o quan l'app s'acaba, i un canvas que la feia servir torna a la font del sistema
  en comptes de llegir un bloc que ja no hi és.
- **I es mira amb dues apps**: `run fonts` posa la font del sistema i una cara carregada a la mateixa pàgina,
  amb el que cada cara diu de si mateixa i l'amplada d'una línia a la consola; `run sheet` dibuixa tots els
  glifs del sistema dins de la seva cel·la, que és el que diu si els bits cauen on toca. Totes dues van amb
  `--display` (o `--gui`) i s'acaben soles.
- **El que no hi és**: ni tipus escalables, ni kerning, ni una taula per sota de `0x20` o per sobre de `0x7F`.
  Una lletra d'aquest sistema és un mapa de bits, i un fitxer de text el descriu sencer (docs/30).

## El tàctil: un dit que costa el que costa (1/11)

El vidre del banc tenia ulls i no dits. **El panell és del model** (`Model::touch`): una Paper del banc porta
el seu **GT911** des que arrenca, i el que se simula no és «on és el dit» sinó el xip: en aquesta placa el
que escanya una app és el que hi ha entre el dit i el full de dades
(`lib/hal_emulator/src/emu_gt911.cpp`, docs/50). Amb `--no-touch` el panell hi és i no contesta, que és el
cas d'una prova que no és sobre el tàctil.

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

- **Els dits són del test, del guió o del ratolí**, com les tecles: `emu::touch_panel()->finger_down(0, 100,
  200)`, `finger_move`, `finger_up`, i amb `--gui` el botó esquerre de la finestra fa el mateix en el píxel
  que assenyala. Un punt fora del marc no és un dit i es refusa; el tercer dit també.
- **El que no hi és, i s'ha de saber**: al banc **no hi ha bus**, o sigui que el tàctil no comparteix res
  amb el rellotge, la FRAM ni el SHT30 i no hi ha cap NAK ni cap conflict; el xip del banc contesta
  sempre a la primera, quan a la placa el probe va haver de preguntar a dues adreces (docs/50); i el marc
  que dona és el de fa menys de `--touch-scan` ms i no el d'un escombrat concret.
- **El que la placa ha de dir encara**: la **velocitat de mostra** (comptar marcs amb el dit, docs/95
  E-6) i **el cost d'una lectura**, que al banc és la física del bus i que `stats` ja compta a la fila
  `touch.read` a les dues bandes.

## Els temps del banc, i el commutador que els apaga (1/11)

Un run del banc **paga la mateixa estona que la placa**, perquè una app que aquí va bé i allà va malament
no serveix de res: el disc (**819 µs** per bloc de 4 KB de flash i **45 ms** d'esborrat per cada sector que
s'escriu, 410 µs per sector de targeta, **90 µs per byte** de FRAM), el **port sèrie** (86,8 µs per byte
a 115 200), la **tinta** (0,8 µs per byte i 1 100 µs per fila, i la qualitat de cada pintada) i el
**tàctil** (800 µs per torn sense dit, 1 830 amb un i 2 550 amb dos). **Tot és un sol
commutador** (`Options::delays`), perquè un run o paga el temps del maquinari o és una prova, i qui en
paga la meitat no mesura res:

| Com s'engega | Els temps | Per a què |
|---|---|---|
| **el binari, per defecte** | **es paguen** | mirar què costa una placa: `run panel` diu **2 899 ms** de pintada i el run sencer **3,11 s** |
| **`--gui`** | **es paguen, i és obligatori** | una finestra és per veure què costa una placa; `--gui --no-delay` es refusa sempre i `--gui --no-ink` quan hi ha vidre, amb una línia que diu per què |
| **`--no-delay`** | **cap espera** | un guió o una mesura que no sigui de temps: el mateix `run panel` dona **24 ms** i el run sencer **0,03 s** — **104 vegades més ràpid** |
| **les proves** | **cap espera** (el camp neix fals) | cap suite paga el rellotge; la que mesura temps (`test_ui`, `test_touch`) el posa a cert |

**El model no canvia amb el commutador**: el fantasma d'un `du`, la brutícia, la mida del contacte i el
marc que es dona una vegada hi són igual. El que s'estalvia és el rellotge i no el model, que és
exactament el que vol una prova de píxels.

## El disc: cada dispositiu amb el seu temps (1/11)

Un sol mil·lisegon per lectura era el número del banc per a tot, i no és el d'enlloc: el disc del Paper
són **tres aparells diferents** i un no s'assembla a l'altre (`emu/disk.h`, docs/50).

| Dispositiu | Unitat | Llegir-ne una | Escriure-la | D'on surt |
|---|---|---|---|---|
| la **flash** (littlefs) | **4 KB** | **819 µs** de cable + 60 de comanda | **6 400 µs** de programa + **45 000 µs** d'esborrar el sector | SPI nor a **40 MHz**, pàgines de 256 B a 0,4 ms i sectors de 45 ms, els números del fabricant |
| la **targeta** (FATFS) | **512 B** | **410 µs** + 200 de comanda | 1 500 µs | el bus de la targeta a **10 MHz** (docs/50) i el temps propi de la targeta |
| la **FRAM** (FM24C02) | **1 byte** | **90 µs** | 90 µs, sense espera | I2C a **100 kHz**, nou bits per byte |
| la **NVS** | una partició de la flash | el de la flash | el de la flash | és la mateixa flash (docs/70) |
| la **RTC RAM** | — | res | res | és RAM del xip, i cobrar-la seria inventar-se un xip |

- **Per què una escriptura és tan cara**: littlefs **no escriu a sobre** del que hi ha. Desar un fitxer
  petit pren un bloc nou, i un bloc nou vol dir **esborrar el sector sencer abans** —45 ms—. És el que fa
  que desar sigui una cosa que es pensa dues vegades, i al banc es paga igual.
- **La PSRAM paga el que es pot pagar**: el bloc que el nucli demana costa el **recorregut de
  l'assignador (20 µs)**. Els bytes no es cobren: llegir i escriure PSRAM és un fallo de cau i un bus,
  que és temps de CPU, i aquest banc no té rellotge de CPU. On la PSRAM sí que es paga és al bus del
  panell, perquè cada fila que s'envia es copia de la PSRAM (docs/85).
- **`--read-delay` ja no hi és**: era el rellotge pla que s'ha arreglat. El que fa anar de pressa és
  `--no-delay`, que apaga el disc sencer amb la resta.
- **El que la placa pot confirmar**: la fila `flash.read` i `flash.write` de `stats` a les dues bandes;
  el que hi ha aquí és la física de cada aparell i no una mesura d'aquesta unitat.

## La bateria: la d'on tu, al cos de la placa (1/11)

El Paper porta **una cel·la de 1150 mAh a 3,7 V** i la llegeix com a mil·livolts darrere d'un divisor a
**GPIO35** (×2), amb quatre conversions de l'ADC; el portàtil on corre el banc porta el que porti i el nucli
en diu un **percentatge**. El que es presta és **el percentatge i no la tensió**, i es presta **sol** (un
model amb indicador el llegeix a l'arrencada; `--battery-host` només ho demana en veu alta): 63 %
d'aquí són 63 % d'allà, i la corba de `hal/battery.h` —la mateixa que fa servir la placa— diu que això és
**3 847 mV**. Una bateria de tres cel·les prestada literalment donaria 11 V, i cap placa d'aquesta família
llegeix 11 V.

- **Es llegeix del nucli**: `/sys/class/power_supply/*/capacity` i `/status`, el primer aparell de la llista
  que sigui una bateria i tingui percentatge. Un `150` o un `0` no és cap bateria i el banc no se'l creu;
  i si la màquina no en té cap, la cel·la del model comença al **seu càrrec nominal (3 700 mV, 42 %)**, que
  és el que una Paper amb una cel·la de fàbrica diria —no `nil`, perquè una Paper no té aquest estat.
- **El cable es diu però no es diu a l'app**: `status` diu si la màquina està carregant, i el banc ho
  escriu a la línia d'engegada; `power.battery().charging` segueix essent **fals**, perquè el Paper no té
  cap pin per preguntar-ho i una app no ha de rebre una cosa que la placa no sap (docs/30, docs/50).
- **I el rellotge es paga**: quatre conversions d'ADC a ~100 µs i la caracterització del canal la primera
  vegada (~1 ms), o sigui **400 µs per lectura** amb els temps engegats. Amb `--no-delay`, res.
- **L'opció explícita no hi és per comoditat**: el préstec ve sol, però `--battery-mv` mana i una prova no
  en depèn (una prova construeix les seves opcions i no llegeix mai aquesta màquina).
- **Es llegeix una vegada, a l'arrencada**: el percentatge del portàtil no va baixant dins del banc,
  perquè el banc no dibuixa el consum de la placa —té una fila pròpia a la taula de fidelitat— i una
  bateria que baixa sola seria un número inventat.

## El rentat entre dues apps: el sistema mira què ha ensenyat la pàgina (1/11, 0.37.0)

Quan una app marxa, la vista es posa blanca i **el vidre s'ha de quedar net**, i el que costa netejar-lo no és
el mateix per a totes les pàgines: una pàgina de text no necessita el mateix rentat que una fotografia. El
sistema ho compta sol, i no ho pregunta a l'app, perquè **cap app sap què hi havia abans**.

- **Què compta**: `ui::levels_shown()` és un bit per cada nivell dels setze que la pàgina ha ensenyat des de
  l'últim rentat (es marca a `put_pixel` i a `put_rect`, que és on acaben totes les primitives i el text), i
  `ui::paints_since_clean()` compta les caixes de píxels que s'hi han posat.
- **Què decideix** (`ui::cleaner_for`, amb prova a `test_ui`):

| La pàgina ha ensenyat | Rentat | Per què |
|---|---|---|
| res | `du` | no hi ha res a netejar, i el més barat és el que no fa res |
| només els nivells 0 i 15 | **`a2`** | és el rentat de dos nivells: 290 ms en comptes de 450 |
| quatre grisos o menys | `gl16` | setze nivells sense el flaix negre |
| una fotografia (molts nivells) | `gc16` | només el rentat complet deixa el paper net |
| **32 pintades ràpides o més** | `gc16` | passat aquest número el que es veu és el fantasma de les pintades, sigui quina sigui la pàgina |

- **I qui paga**: el rentat es demana a `ui::attach()`, quan la vista passa a l'app següent, i **no s'espera**:
  la primera pintada de l'app que ve s'encua darrere. El número `32` és una constant dita en un sol lloc
  (`kManyPaints`, `ui.cpp`) perquè algú la compti a la placa i la canviï (docs/50).
- **Què es veu al banc**: `docs/85` ja sap com costa cada qualitat; el que això afegeix és que el canvi d'app
  costa 290 ms i no 450 quan l'app era de text, i que una app que no hagi pintat res no costa res.

## El terminal es pregunta, no s'endevina (2/11, 0.53.0)

La línia del prompt es redibuixa a cada tecla, i com es redibuixa depèn del terminal: amb **ANSI** es fa amb
`ESC [ K` i `ESC [ n D` (esborrar i tornar enrere); sense, amb **espais i backspaces**, que és el mateix
dibuix amb els codis que qualsevol port entén. Fins la 0.52.0 això es decidia **endevinant** (`isatty` i la
variable `TERM` al PC, i «sempre sí» a la placa), i una placa amb un pont sèrie sense ANSI rebia caràcters
d'escapament com a brossa.

Ara, a l'arrencada, el shell **pregunta**: envia la consulta *Device Attributes* (`ESC [ c`) i busca la
resposta `ESC [ ? … c` en el que arriba, amb un màxim de **150 ms**. Un terminal ANSI contesta, un de ximple
no contesta i el sistema ho apunta:

```
[I][TERM] ansi
[I][TERM] no ansi, spaces and backspaces
[I][TERM] no terminal, ansi not asked
```

Tres detalls que són la diferència entre detectar i fer veure que es detecta:

- **El que creu la plataforma és la porta.** Un console que ja diu que no és un terminal (una canonada, un
  fitxer, la suite) **no es pregunta**: no hi ha ningú per contestar i 150 ms d'espera són un peatge per res.
- **El que arriba mentre la pregunta és a l'aire no es llença**: si no és una resposta, s'entrega a la línia
  quan el prompt ja hi és. La primera tecla d'una persona no se la menja l'arrencada.
- **La regla és pura** (`kernel::shell::ansi_reply`) i la suite la prova amb la resposta d'un VT100
  (`ESC [ ? 1 ; 2 c`), la d'un xterm, una fletxa, un informe de cursor, una tecla i un text qualsevol.

Mesurat al banc: amb una canonada, `no terminal, ansi not asked`; dins d'un pseudo-terminal que no contesta,
`no ansi, spaces and backspaces`; i donant-li la resposta pel seu `stdin`, `ansi`.

## Què mirar, i amb quina app (1/11)

**Al banc, el maquinari del model hi és des de l'arrencada**: el vidre i el GT911 d'una Paper són allà sense cap bandera (i `--no-display` / `--no-touch` els treuen), o sigui que `run draw` i `run glass` van amb `--board m5stack-paper` i prou. **I amb `--gui` el dit és el ratolí**: es prem i s'arrossega damunt de la finestra. El que no hi és mai és el que el model no té: un `--display` a un ATOM es refusa dient que aquella placa no té pantalla.

Cada cosa que el banc simula té una app que la fa servir, i totes **corren igual a la placa**: el que es
veu al banc i el que es veu al vidre és la mateixa pàgina, i els números s'hi poden comparar.

| App | Què prova | Què mirar |
|---|---|---|
| `run panel` | la pàgina, la vora i les setze qualitats | que el dibuix hi és i recte, i `the ink took N ms` |
| `run squares` | una pintada per qualitat, cronometrada | `painted with du in N ms` contra `erased with gc16 in N ms` |
| **`run ghost`** | **el fantasma**: vuit passades de `du` sobre el mateix bloc i un `gc16` al final | el bloc que guarda la forma de cada passada i el comptador amb els números dels passats a sota; al banc, a més, la finestra |
| **`run draw`** | **el tàctil**: un traç que segueix el dit, i un quadre que neteja la pàgina | que el punt cau on és el dit —amb `--gui`, el del ratolí—, el traç de la vora del gest i el temps de cada pintada |
| **`run cost`** | **el que costa tot**: `stat`, `list`, llegir, escriure, una pintada de cada qualitat, la bateria, el tàctil i la memòria | les dues taules (placa i banc) al costat: si no s'assemblen, algú menteix i `docs/85` diu qui |
| `run glass` | el tàctil a la consola | els punts de cada frame i els gestos amb la seva durada |
| `run keys` | les tecles del dial | que la posició que prems és la que surt al compte, i a dalt/centre/baix amb la tira de tecles de `--gui` |
| `run battery` | el gauge | mV, `%` i que `charging` no hi és |
| `run mtab` | el disc i l'espai | que quatre noms de la mateixa partició són quatre passejades |

## Fidelitat: què s'assembla i què no

| Aspecte | A l'emulador | A la placa |
|---|---|---|
| Nucli, Lua, VFS, promeses | **Idèntic** | Idèntic |
| Resolució de camins i permisos | Idèntic | Idèntic |
| Sostre de memòria | **El de la placa, i no més**: l'ATOM contesta el sostre per defecte (64 KB, `Model::app_limit_kb` = 0, que vol dir «la placa no en diu cap») i el **Paper contesta la seva PSRAM mapejada (4 096 KB, de 8 MB que en té el xip)**. `--mem-cap` el pot **baixar** per veure-hi el pic, i demanar-ne més es refusa amb el número que té el model. `sys.heap()` dona el pressupost de l'app (`free`, `used`, `limit`, `peak`) i la memòria extra del model (`extra_total`, `extra_free`), i `mem` escriu els tres números del model (`Model::psram_kb` mapejats i `Model::psram_chip_kb` del xip) | 320 KB de SRAM al xip i, al Paper, **4 MB de PSRAM mapejada** on van els blocs de 4 KB en amunt (docs/75) |
| Cost d'una lectura | **Simulat per dispositiu** (`emu/disk.h`): la flash llegeix **blocs de 4 KB en 819 µs** (SPI nor a 40 MHz) més 60 de comanda, la targeta **sectors de 512 B en 410 µs** (10 MHz) més 200 de comanda, i la FRAM **90 µs per byte** (I2C a 100 kHz). Una escriptura a la flash paga els 6 400 µs dels setze programes de pàgina **i els 45 000 del sector que s'ha d'esborrar**, que és d'on vénen els segons d'escriure un fitxer. La NVS és una partició de la flash i paga el mateix (docs/85) | Mil·lisegons de flash o SD |
| Targeta (SD) | **Un directori del disc, i només si el model té ranura** (el Paper): el mateix backend POSIX que la flash, o sigui que el que llegeix aquí llegeix a la placa. Es presenta com una targeta de **16 GB** amb el lliure del PC, i el que falta fins a 16 GB és l'ocupat | FATFS per SPI a la Paper: CS GPIO4, 10 MHz i el CS del panell (GPIO15) deseleccionat abans de tocar el bus (docs/50) |
| El vidre del banc (`Model::glass`) | **Un buffer que és el panell** (`emu::MemorySurface`, 4 bits per píxel): recorda **el rectangle brut de cada pintat** —el mateix que el driver del panell envia, marcat per cada escriptura i buidat a `flush`— i la qualitat que es va demanar, i en guarda els **vuit últims**. Des de la 0.22.0 **la pintada mou tinta** —un `du` deixa una part del que hi havia i acumula brutícia, un `gc16` neteja, i vuit de cada mil píxels fallen un nivell o dos— i des de la 0.23.0 **paga el bus**: 0,8 µs per byte a 10 MHz i 1 100 µs per fila, i `idle()` no diu que sí fins que han passat el bus i la tinta (docs/85). Les proves hi corren amb el model fora (`--no-ink`). **I el gir és diferent**: girar-lo torna a fer el buffer, o sigui que el banc s'oblida del que hi havia, cosa que un vidre de tinta no fa mai | El vidre de veritat: la tinta hi queda i un gir obliga a tornar a enviar la pàgina sencera. El número de `run panel` és el mateix a les dues bandes (1 985 ms a la placa, 1 970 mesurats al banc, 1 954 al model) i el que queda obert és el perquè del cost d'una fila (docs/95, E-7) |
| Desgast de la flash | No existeix | Existeix |
| Consum | No existeix | Existeix |
| Despertar de son | Simulat | Real o inexistent segons la placa |
| Espai lliure (`fs.space`, `mounts`) | **La targeta sí**: és una targeta de **16 GB** i el lliure és el que queda de debò al disc del PC, així que el que falta fins a 16 GB es mostra com a **ocupat** (`free 6587 MB of 16384 MB`, 31/10). **La flash també**, des del 31/10: el total és el de la partició de la placa (`Model::disk_bytes`, 0x0E0000 = 917 504 B a l'Atom i 0xBE0000 = 12 451 840 B al Paper) i l'ocupat és el que sumen els fitxers del directori, amb el que quedi al disc del PC com a sostre (si el PC s'omple, es mostra com a ocupat). Mesurat a l'Atom: `/data littlefs rw  915701 of 917504 B free`. `/nvs` segueix dient `E_UNSUPPORTED` a les dues bandes, perquè guarda entrades i no bytes | Números de debò (`/littlefs` 634 880 B lliures de 917 504 el 30/10, i `/nvs` diu `E_UNSUPPORTED` a les dues bandes) |
| Pantalla | No | No (a la v1) |
| Port sèrie | stdin/stdout en mode cru, **i a la velocitat de la placa**: 115 200 baud, o sigui 86,8 µs per byte, i els bytes surten per tandes de dos mil·lisegons com sortirien del cable en lloc de sortir tots de cop. Mesurat: 2 563 B costen 0,21 s (86,8 µs × 2 563 = 0,22 s). `--baud <n>` la canvia i `--baud 0` treu l'espera | UART a 115 200 |
| Xarxa | **De mentida**: connecta a res en `--wifi` ms i no surt enlloc | La de debò (`WiFi.begin`, que no espera) |
| Provisioning | **De mentida**: un AP que no existeix i unes credencials que surten de les opcions | El component d'Espressif: l'app ESP SoftAP Provisioning del telèfon el troba de debò |
| Pàgines (`http.get`) | **De mentida**: el cos i el codi surten de les opcions, i el TLS no hi és: no hi ha cap certificat a validar | `esp_http_client` amb TLS i el paquet d'autoritats del build; el codi, el cos i els errors de certificat són els de debò |
| Energia (`power.*`) | **De mentida i ho diu**: un model amb indicador (el Paper) contesta la bateria **sense que calgui res** —el `%` d'aquesta màquina passat per la corba de la cel·la, o 3 700 mV si la màquina no en té— i `--battery-mv` hi posa un número i mana; un model sense indicador (els ATOM) contesta `nil` sempre. I dormir vol dir que **el rellotge virtual salta** fins a l'hora demanada. El PC no s'atura. `poweroff` sí que és de debò en el que importa: **el model del Paper contesta que sí i el procés s'acaba** (l'estat de les memòries es desa en sortir, com en una apagada), i el model de l'ATOM contesta que no perquè no té control del seu corrent — el banc diu el que diu la placa | Bateria si la placa en té; el son és el de debò: `esp_light_sleep_start` i, en profund, la placa es reinicia; `poweroff` fa la seqüència del fabricant (GPIO23, GPIO5, GPIO2 i son profund) i la placa es queda sense corrent |
| Tornar d'un son profund | **No passa**: l'emulador no es reinicia enmig d'una crida, i per això les proves criden `kernel::power::restore_session()` a mà, que és exactament el que fa l'arrencada | El xip es reinicia i arrenca: l'arrencada llegeix la nota del son i torna a obrir l'app amb `app.args.restore` |
| Velocitat del processador | **De mentida en el rellotge i de debò en el preu**: el PC no canvia de marxa, però el número es desa (arrenca a **160** i accepta 80, 160 i 240) i **el codi de l'app es cobra a aquesta velocitat** (`--lua-cpu`, 60 ns per instrucció a 160 MHz): un bucle que al vidre dura un segon aquí dura un mil·lisegon amb el model apagat i el que toqui amb ell encès | `setCpuFrequencyMhz` de debò, amb l'APB i el tick de FreeRTOS movent-se amb ell |
| Llum (`led.*`) | **De mentida**: el color es desa i el test el pot preguntar (`emu::led_red()`… i `emu::led_frames()`, que compta els frames). El **nombre de llums és el del model** (1 a l'ATOM, 25 al Matrix, cap al Paper) i el sostre del canal és **20**, el del panell, a tots tres; `emu::led_pixel(i, …)` en llegeix un: així una app escrita per al panell es pot provar al PC. Els efectes es proven **movent el rellotge a mà** (`emu::advance_ms`), que és la mateixa hora que llegeix el nucli: una respiració de cinc segons es mira en un mil·lisegon | El `neopixelWrite` i el frame d'RMT de debò, sobre el WS2812 o els 25 del panell a GPIO27 |
| Apps i manifest (`boards`) | **El banc és una placa, la que se li diu i és obligatori dir-la**: `--board m5stack-atom`, `m5stack-atom-matrix` o `m5stack-paper`, i respon **aquell** nom i la família `esp32`. Una app del seu model corre sense dir `emulator` al manifest; una d'un altre model hi surt llistada amb l'avís i `run` la refusa dient les plaques que vol i quina és aquesta. És el mateix criteri que a la placa, i per això provar-ho al PC vol dir alguna cosa (docs/40) | `apps` la llista amb el que necessita i `run` la refusa dient les plaques que vol i quina és aquesta |
| Sensor (`imu.read`) | **De mentida i a la mà del test**: contesta el que diuen les opcions (`--imu-*`), i `--imu-fail` és un xip que no hi és. El que es prova aquí és el camí de la dada —de Lua al HAL i de tornada a l'app— i no la qualitat de la mesura, que només es pot veure al maquinari | L'MPU6886 per la I2C interna, amb les escales del full del fabricant (docs/50) |
| Aire (`env.read`) | **De mentida i a la mà del test**: el model del Paper té sensor i els ATOM no, i les dues xifres són el que diuen `--env-celsius` i `--env-humidity`; `--env-fail` és un xip que hi és i no contesta, que és `E_IO` i no `E_UNSUPPORTED` | El SHT30 per la I2C interna: una comanda, sis bytes i dos CRCs (docs/50). **El banc no té bus**: no hi ha cap clock stretching que es pugui comptar, així que un sensor que el banc llegeix bé pot ser un sensor que la placa dona per mort (va passar l'1/11: `env yes` al PC i `env no` a la placa, docs/50) |
| Tecles (`button.*`, `on_button`) | **De mentida i a la mà del test o del ratolí**: `emu::press_button(true/false)` prem la principal i `emu::press_key(n, true/false)` qualsevol de les tecles **del model** (fins a tres; una a l'ATOM i al Matrix, tres al Paper), i amb `--gui` els botons de la finestra fan el mateix amb un clic. El temps que es compta és el del PC. La forma, però, encara és la de la Paper a tots els models: un dial compost a la cara **dreta** (`centre`, `up` i `down`), o sigui que una app que tria per posició es pot provar al PC. Fer que la forma segueixi el model és el que queda de M8 (docs/90) | El GPIO39 de l'ATOM (una tecla simple a la cara de dalt) i les tres de la Paper, amb 10 ms de rebot |
| Tàctil (`touch.*`, `on_touch`) | **Un GT911 simulat** (`Model::touch`, `emu::Gt911Panel`): el marc de la pàgina, dos dits, la mida del contacte i les tres transaccions del bus a 100 kHz (**800, 1 830 i 2 550 µs** segons els dits). **Escombra sol i dona el marc una vegada**, o sigui que un lector més ràpid que el vidre llegeix «ningú» la meitat de les vegades (docs/85) | El GT911 de debò: el mateix protocol, i a sobre un bus que comparteix amb el rellotge, la FRAM i el SHT30, amb clock stretching de fins a 6 ms del sensor i NAKs possibles |

**Comprovat al banc el 31/10 amb la 0.8.2**, amb `--board m5stack-paper --sd <dir>` i un guió:
`mem` diu `extra memory 4096 of 4096 KB usable, of 8192 KB on the chip (psram)`, `mounts` diu
**`mounts: 9`** (amb `/rtc` i `/fram` entremig, i `/nvs` donant `entries 1`), `run mtab` en llista
nou amb els bytes de cadascun, i `help` ensenya `sleep at <epoch seconds>`. La línia `free heap` **no
hi és al PC** i `mem` ho diu (`heap not available on this build`): la RAM interna del xip no té un
equivalent al banc, i inventar-ne un número seria pitjor que dir-ho.

**I la capa de controls diu el mateix a les dues bandes** (1/11, 0.19.0): `run layers` dona **les sis
línies iguals** al banc amb el vidre del model i a la Paper —els comptes `1 / 1 / 2`, el pare al slot 3, el marc
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
- **No fer les proves gràfiques al `DISPLAY` de l'usuari.** La finestra de l'emulador s'obre en un X
  virtual (`xvfb-run -a`) o en un `DISPLAY` propi, i es tanca en acabar; el ratolí, el focus i les
  finestres de qui treballa no es toquen.
