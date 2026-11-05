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
| `pio run -e emulator -t exec` o `-t upload` | No poden passar `--board`, i el binari es refusa abans de fer res (`which board is this bench?`). Encara que se'ls donés, PlatformIO executa el programa amb la sortida conductada: el prompt es queda al buffer i **el que escrius no arriba enlloc** |

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

**`help` amb una comanda és l'ajuda d'aquella comanda** (0.88.1): `help repo` són les tres línies de `repo` i cap
altra, `help repo install` és la d'aquella sintaxi, i un mot que només és el començament d'una comanda no ho és
—`help pow` respon `no such command: pow` i diu on és la llista sencera— (docs/30).

**I la consola té una paleta, i només si el terminal la va demanar** (3/11, 0.57.0): el prompt és el nom
en verd i el `>` en gris; un error és vermell, un avís groc i una resposta bona verda; `ls` posa el tipus
i la mida apagats i el directori en blau; `help` posa l'ordre en negreta, i el `log` pinta cada línia pel
seu nivell (`W` groc, `E` vermell). **No en surt ni un byte si el terminal no va contestar la pregunta
ANSI** del començament, i `ansi [on|off]` diu què s'ha detectat o ho força quan un pont sèrie se la menja
—que és el cas d'un `clear` que ara no envia `ESC [ 2 J` a una línia que no ha dit que el sàpiga llegir
(docs/30, docs/50).

**La pàgina del banc és la de debò** (3/11, 0.61.0): `http <url>` al prompt i `http.get` d'una app
baixen l'adreça de veritat. El banc la dona a `curl` en un procés seu —`-q`, sense `~/.curlrc`, fins a
deu redireccions, amb el certificat comprovat contra les autoritats d'**aquesta** màquina— i el cos i el
codi tornen del servidor, o sigui que la comanda ensenya el començament de la pàgina que algú ha
demanat i no una línia que digui l'adreça. El camí no canvia gens: la cua, la promesa, el termini de
40 s, els 16 384 B i els codis `E_*` són els del nucli, i el que el servidor trigui es paga de debò. Una
adreça que el client de la placa no parla (`ftp://`, un camí d'aquest disc) és `E_HTTP` i no una tasca
refusada. **Una pàgina que no cap en memòria va sencera a un fitxer**: `http <url>` la desa a la
targeta o a la flash quan passa dels 16 384 B i en diu el camí, `http <url> <path>` l'hi desa sempre, i
una app fa el mateix amb `http.download` dins del que pot escriure (docs/30, docs/70). Un
`curl` que no és en aquesta màquina dona `E_HTTP` i ho diu una vegada a la consola. **`--http-fake`**
torna la pàgina de mentida, i les altres quatre opcions `--http-*` la demanen totes soles: és el que
vol una prova que no ha de dependre de la xarxa. I cada run ho diu una vegada a la consola d'errors —una línia
que comença per `emulator:` i diu que `http` baixa la pàgina de debò—, perquè una petició que surt
d'aquesta màquina no ha de ser una sorpresa.

**Un banc que només sap contestar una pàgina no pot provar una cadena** (4/11, 0.88.6): `repo install <app>`
demana el llistat de l'app, el llistat de cada carpeta i cada fitxer, i tots són respostes diferents del mateix
servidor. Per això la xarxa falsa té, a més de la pàgina dels `--http-*`, **una taula de respostes per adreça**:
`emu::http_canned(<tros d'adreça>, <cos>, <codi>)` n'hi afegeix una —la primera que encaixa amb l'adreça
demanada és la que contesta, i si no n'hi ha cap, contesta la pàgina de sempre— i `emu::http_canned_clear()`
deixa la xarxa sense cap, que és com comença una prova la seva pròpia cadena. La taula és del banc i no del
nucli: el nucli no en sap res, i la baixada que arriba és la de debò, amb el fitxer escrivint-se pel camí dels
volums. **Un cos que no cap a la taula es refusa** i no es talla, perquè mig llistat és un install que falla
per un motiu que no és el de la prova. Amb això, la cadena d'un `repo install` es prova sencera al banc
(`test_the_repo_command_installs_an_app_with_its_folders` i
`test_an_install_that_hits_a_404_says_so_and_can_be_tried_again`).

Aquestes són les opcions del binari:

| Opció | Què fa |
|---|---|
| `--flash <dir>` | El directori que **és** el volum de la flash (per defecte el que el build va rebre de `platformio.ini`, `build/disk/emulator`, que és `data/` i prou: **el banc no porta apps**, 0.88.2); `/littlefs/res` és `data/res` i no es copia res |
| `--state <dir>` | On viuen les memòries que no són un volum: `nvs/` i, quan n'hi hagi, la RTC, la FRAM i l'hora que el xip recorda (per defecte `emulator/`) |
| `--persist` | **La placa no s'ha desendollat**: el run arrenca on el run anterior va deixar la placa, o sigui que la **RTC RAM** no s'esborra ni a l'ATOM (un reinici no apaga el domini del RTC) i **l'hora** de `clock.bin` es llegeix també als models sense pila, amb `source` = `"soc"`. La NVS i la FRAM no depenen d'això: són flash i ferrita i es guarden sempre (docs/60, docs/85) |
| `--fs <dir>` | **Recanvi** del disc quan no hi ha `--flash`: `Options` hi cau si `flash_dir` és nul (`emu_storage.cpp`). El binari sempre acaba de posar `--flash` a `data`, o sigui que **des del binari no fa res**; el fan servir les proves, que es muntin les seves opcions |
| `--sd <dir>` | **La targeta**: aquest directori n'és el volum i es munta a `/sd`. Per defecte `emulator/sd`, que **el banc no crea i que no porta cap app** (0.88.2): si no hi és, la ranura és buida i `apps` diu `no apps`; si hi és, el que hi hagi és el que el banc té (docs/70) |
| `--battery-mv <mv>` | **Una bateria de mentida**: el que llegeix `power.battery()`. **Mana sobre la de la màquina** (la del punt de sota), i el `%` no es posa: surt de la corba de `hal/battery.h`, la mateixa que fa servir el Paper. Un model **sense indicador** (els ATOM) dona `nil` encara que se li digui un número, que és el que faria la placa (docs/30) |
| `--battery-host` | **La bateria d'aquesta màquina, prestada a la placa, i la demana poques vegades perquè ja ve sola**: en un Linux, un model amb indicador llegeix el `%` de `/sys/class/power_supply` a l'arrencada sense cap bandera (en un altre amfitrió no hi ha res a llegir i la cel·la comença al nominal) i el passa per la **corba de la cel·la del model** (1150 mAh, 4,2 V plena), o sigui que 99 % d'aquest portàtil són **4 190 mV del Paper** i no 12 V d'una bateria de tres cel·les. Si la màquina no en té cap, la cel·la comença al **seu càrrec nominal (3 700 mV, 42 %)** i el banc ho diu, perquè una placa amb indicador sempre llegeix alguna cosa. La bandera serveix per **demanar-ho en veu alta** (i perquè se't digui que un ATOM no té indicador); amb `--battery-mv` alhora, el banc el refusa (docs/50, docs/85) |
| `--board <nom>` | **Quina placa és el banc, i és obligatori**: `m5stack-atom`, `m5stack-atom-matrix` o `m5stack-paper`. `sys.board` respon aquest nom, `sys.emulator` continua cert, i `boards` del manifest es compara amb aquest i no amb els tres (docs/50). **El maquinari és el del model**: un ATOM té un llum i una tecla, la Matrix el panell de 25 i el sensor, i el Paper les tres tecles, l'indicador de bateria, la targeta i el rellotge amb pila — el mateix que diu la fitxa de docs/50, ni més ni menys |
| `--mem-cap <KB>` | **Baixa** el sostre de la VM (mai per sobre del que té el model, que es refusa: 320 KB als ATOM i 4 096 KB al Paper, docs/75): posa-hi el del model per veure-hi el pic. És el pressupost **de l'app**, i per sota hi ha el de la placa: des de la 0.58.0 el PAL compta la RAM interna i la PSRAM del model i refusa el bloc que no hi cap encara que el sostre de l'app el deixés passar (docs/75, docs/85) |
| `--baud <n>` | La velocitat del port sèrie, en baud: **115 200** per defecte, la de l'UART de la placa, o sigui **86,8 µs per byte** (10 bits a 8N1). `0` treu l'espera, que és el que vol una prova, perquè el seu console és un fitxer i no un cable |
| `--wake-early <ms>` | Que el banc torni d'un son **aquesta estona abans** de l'hora que se li ha donat: `0` (per defecte) és el rellotge honrat, i qualsevol altra cosa és un son que s'ha tallat —un dit al botó, un cable, una alimentació que parpelleja— per poder provar el cas en què la placa no arriba a la seva hora (docs/60) |
| `--now <època>` | Fixa l'hora d'arrencada, i **mana sobre el que la placa recorda**. Sense aquesta opció l'hora surt de `clock.bin` quan la placa té pila darrere el rellotge (el Paper, `source` = `"rtc"`) o quan el run diu `--persist` (`source` = `"soc"`); si no hi ha res a recordar, el rellotge val 0 i `time.caps().source` és `"none"`, com una placa que mai s'ha sincronitzat, fins que algú la posi (`ntp` o una app) |
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
| `--http-fake` | **La pàgina de mentida**: la descarrega no surt d'aquesta màquina i el cos i el codi són els que diuen les tres opcions de sota. És el que vol una prova que no ha de dependre de la xarxa (docs/85) |
| `--http-ms <ms>` | Quant triga la **pàgina de mentida** a arribar (50 ms; demana `--http-fake` tot sol) |
| `--http-status <n>` | El codi que contesta la pàgina de mentida (200; demana `--http-fake` tot sol) |
| `--http-body <s>` | El cos que contesta la pàgina de mentida. Sense aquesta opció contesta `page for <url>`, amb l'adreça a dins perquè es vegi que el que s'ha demanat és el que torna (demana `--http-fake` tot sol) |
| `--http-fail` | Un servidor que no contesta: `E_HTTP` (demana `--http-fake` tot sol) |
| `--imu-ax <g>` / `--imu-ay <g>` / `--imu-az <g>` | Què llegeix el **sensor de mentida** a cada eix. Per defecte 0, 0 i 1, que és la placa plana damunt la taula |
| `--imu-temp <C>` | La temperatura que dona el sensor (per defecte 25) |
| `--imu-fail` | Un sensor que no contesta: `imu.read()` respon `E_IO` |
| `--env-celsius <C>` | La temperatura que dona el **sensor d'ambient de mentida** (per defecte 25) |
| `--env-humidity <%>` | I la humitat (per defecte 50) |
| `--env-fail` | Un sensor d'ambient que hi és i no contesta: `env.read()` respon `E_IO`, que no és el mateix que una placa sense sensor |
| `--run <id>` | Arrenca una app pel mateix camí que `run <id>` de la shell i **surt** amb el resultat: `0` si l'app ha acabat bé i `1` si no s'ha pogut arrencar, ha mort amb un error o ha sortit amb un codi diferent de zero |
| `--script <fitxer>` | Mode lot: executa ordres i surt |
| `--display` | **El vidre del model, que hi és sol**: el panell del Paper, 960x540 i quatre bits per píxel, en un buffer d'aquest costat (`emu::MemorySurface`), **i la capacitat `display` amb ell** —`sys.caps().display` diu la veritat, que no deia fins a la 0.19.0: el vidre hi era i el banc deia que no—. Qui ho diu és el model (`Model::glass`): el Paper en té i els ATOM no, i un `--display` a un ATOM es refusa dient que aquella placa no té pantalla. La bandera serveix perquè un guió digui que necessita vidre; el que apaga és `--no-display` (docs/30, docs/50) |
| `--no-display` | **Una placa sense pantalla, amb tota la resta del model**: només consola, i **sense el panell del banc ni la vista de l'app** —ni els 246 240 B del canvas de la vista ni els 259 200 B de la imatge del panell ni els 518 400 B dels dos buffers de tinta, que no es creen. És el que vol una mesura que no és de píxels (docs/85) |
| `--gui-scale <1..4>` | **Quants píxels de la pàgina és un píxel de la finestra**, forçat: `1` és la pàgina tal com és i `2` és la pàgina a la meitat, encara que la pantalla la hi deixi sencera. Sense l'opció la finestra tria **l'escala entera més petita que hi cap** (1 a 4), que és el que fa que una pantalla petita ensenyi la pàgina sencera i no un tros. No canvia la pàgina (540×960): només com es veu (docs/85) |
| `--gui` | **Una sola finestra amb tot el que la placa té per tocar i per veure** (0.34.0). Si el model té vidre, **la pàgina en una finestra (SDL2, o X11 de reserva)**, pintada cada cop que el panell es pinta o una **fase del parpelleig** canvia —el mateix buffer del `MemorySurface` i **la pàgina com la dibuixa l'app** (540x960), amb els setze nivells en els colors del paper— i **les tecles al costat que diu el descriptor de l'entrada**, un botó per tecla del model; una placa sense pantalla en treu només les tecles, que és el que sí que té. La finestra ensenya **el que el vidre mostra** i no el que es va pintar, o sigui que el fantasma d'un `du` s'hi veu i el parpelleig d'un `gc16` o d'un `a2` hi passa per les seves fases. El ratolí és el dit a la pàgina i és el dit que prem a un botó. No es pot combinar amb `--no-delay`, ni amb `--no-ink` quan hi ha vidre; s'obre quan hi ha una pantalla on obrir-la i, si no n'hi ha, el banc ho diu i segueix sense finestra (docs/85) |
| `--no-ink` | **Un vidre que canvia píxels i no mou tinta**: una pintada és instantània i exacta, la pàgina és la que s'ha pintat sense res del que hi havia abans i **no s'envia res al bus**. És el que fan servir les proves —no paguen mig megabyte de buffers, ni mig segon de tinta, ni un segon de bus per pàgina— i el que queda si es vol comparar el dibuix amb el d'abans del model (docs/85) |
| `--touch` | **El tàctil del model, que hi és sol**: un GT911 en el marc d'aquesta unitat (540x960, dos dits) que **costa el que costa el bus** —800 µs per torn sense dit, 2 550 amb dos— i que **dona el marc una vegada**, amb la capacitat `touch`. Qui ho diu és el model (`Model::touch`): el Paper en té i un `--touch` a un ATOM es refusa. El que apaga és `--no-touch`, i llavors el panell d'aquella Paper és un xip que no contesta, que és el que vol una prova que no és sobre el tàctil (docs/50, docs/85) |
| `--no-touch` | **Cap dit**: el panell del model no es llegeix i no costa els 800 µs de cada torn (docs/85) |
| `--touch-scan <ms>` | Cada quant **mostreja** el panell: `0` és el del xip (10 ms) i un número més gran que el torn del nucli és un vidre que contesta «ningú» a la meitat de les lectures, que és exactament el cas que es vol poder reproduir (docs/85) |
| `--no-delay` | **Cap espera**: el model del disc, del port, de la tinta i del tàctil és el mateix i el rellotge no es toca — ni el torn del nucli, que deixa de costar els 850 µs que costa a la placa (`kTurnOverheadUs`). És el que fa servir una prova o una mesura que no és de temps, i el que un run amb `--gui` **no pot** demanar (docs/85) |
| `--lua-cpu <ns>` | Què costa **una instrucció de l'intèrpret** a 160 MHz: **60 ns** per defecte (el model; la mesura de la placa encara es deu, docs/95), i `0` perquè el codi de l'app sigui gratis, que és el que era el banc fins a la 0.29.0. Es divideix per la freqüència simulada, o sigui que a 240 MHz una instrucció costa 40 ns i a 80, 120 |
| `--verbose` | El log també surt per la consola, i amb ell **cada pintat del vidre**: `gui: paint 12,9 116x28 gld16` és la regió i la qualitat que s'ha demanat, que és com es comprova que un títol nou no repinta la franja sencera (0.85.2) |

En mode lot, cada app que arrenca es deixa acabar abans de la línia següent, i **també cada ordre que
treballa** (`wifi add`, `ntp`, `http`): una comanda que espera la ràdio tindria la línia següent
trobant-la ocupada, que és el que va passar quan només s'esperaven les apps (31/10). I després de
**cada** línia es donen dos torns, perquè el que es fa a final de torn —el llum, per posar un cas— hagi
passat abans de la línia següent; sense això un guió veia una placa que no pintava mai res (31/10). El límit és de
**60 s** per línia: una app que espera una tecla que no arriba no pot penjar el guió, i una pàgina de debò necessita fins al termini de 40 s del nucli més l'associació. Si el guió
arrenca una app que llegeix, l'entrada es pot encaminar per stdin, i ha d'arribar **mentre l'app ja
corre**:

```sh
printf 'run echo\n' > /tmp/s.txt
( sleep 0.4; printf 'hola mon\r' ) | ./.pio/build/emulator/program --board m5stack-atom --script /tmp/s.txt
```

## El disc de treball

**El disc del banc és el del seu build, `build/disk/emulator`**, i el camí li arriba compilat
(`-DLUNOK_DISK_DIR` a `platformio.ini`), o sigui que dos builds no comparteixen disc i el banc no llegeix el
que les proves acaben d'escriure. `/littlefs` és `data/` i prou —`/littlefs/res` és `data/res`—, i el que una
app escriu va a `data/data/<id>/`, que és estat i no codi i per això és al `.gitignore`.

**El banc no porta cap app** (0.88.2): ni a la flash ni a la targeta. El seu disc és el sistema —`res/`,
`http/` i `data/`, que és el que una app hi escriu— i el slot de la targeta és buit fins que algú hi posa
una targeta, o sigui que un banc acabat de fer diu `no apps` a `apps` i `no such app` a `run`. Una app hi
arriba com arriba a una placa: **`repo install <app>`** (que la baixa a `/sd/apps`, i al banc això vol
dir una targeta), o una targeta pròpia amb `--sd <dir>`. I com que el directori no hi és, **`/apps` no
és ni un muntatge**: `ls /` i `mounts` no ensenyen una porta que no porta enlloc.

A part del disc hi ha les memòries que **no són un volum**, i totes viuen en un sol directori
(`--state emulator`, per defecte):

```
emulator/
  nvs/…                 el magatzem clau-valor (avui un fitxer per clau)
  rtc.bin               la memòria RTC del xip, 7 168 B, binària
  fram.bin              la FM24C02 del Paper, 256 B, binària
  clock.bin             l'hora que el xip recorda: època en ms i el rellotge del PC que la va escriure
  sd/                   la targeta, quan aquest directori existeix
```

El magatzem clau-valor **no és a dins del disc** perquè a la placa tampoc no ho és: la NVS és una
partició pròpia i no un fitxer que es veu a `/littlefs`. La seva mida a la placa la diu
`partitions.csv` (16 KB); aquí és un arbre de fitxers i no té sostre, i `stats()` ho diu tornant 0.
**La NVS és flash i sobreviu sempre**: el que hi escriu una execució ho troba la següent, i des de la
0.59.0 el que hi ha és també **la xarxa**: `nvs/lunoknet/ssid` és el registre del sistema i
`nvs/wifidrv/pass` fa el paper del blob del driver —a la placa la contrasenya és dins de l'NVS del
driver i el registre propi només guarda el nom— (docs/50, docs/70). `wifi` després d'un reinici diu el
nom que es va desar, i `wifi forget` esborra el nom i deixa el blob, que és el que fa
`own_record_erase()` a la placa.

**Les altres memòries són fitxers binaris de la mida exacta del maquinari**, i es mantenen entre
execucions com la memòria d'un aparell (`emu/memories.h`):

| Fitxer | Mida | De qui és | Quan es perd |
|---|---|---|---|
| `rtc.bin` | **7 168 B** | la memòria RTC lenta del SoC (docs/50) | amb el corrent — **excepte** al Paper, que el model conserva perquè té pila darrere el rellotge; els ATOM i el Matrix l'esborren, i `--persist` la conserva a tots tres perquè un reinici no és un cicle d'alimentació (docs/50) |
| `fram.bin` | **256 B** | la FM24C02 del Paper (docs/50) | **mai**: una memòria ferroeléctrica no és volàtil |
| `clock.bin` | **16 B** | l'hora que el xip o la pila recorden (docs/60) | quan la placa no té pila darrere el rellotge i el run no diu `--persist`: llavors no es llegeix i l'època val 0 |

L'hora **no és dins de `rtc.bin`**, perquè no és un byte de la memòria del xip: és el que un xip amb
pila de botó compta mentre la placa és morta. El fitxer porta l'època i el segon del PC en què es va
escriure, i en llegir-lo el banc hi suma el temps que la placa ha estat desendollada — que és
exactament el que fa la pila (docs/60).

**Els fitxers els crea el banc sol, i només els que el model té** (`emu/model.h` és l'única llista de
què porta cada placa): un ATOM té `rtc.bin` i, si el run va arribar a saber l'hora, `clock.bin`; un
Paper té `rtc.bin`, `fram.bin`, `clock.bin` i la targeta. Un model sense FRAM no rep `fram.bin`, i
`emu::fram()` torna `nullptr` — el mateix que una placa sense aquest xip.

O sigui que una execució de l'emulador és un **cicle d'alimentació**: `emu::power_cycle()` esborra la
RTC si la placa no té pila darrere el rellotge (ATOM i Matrix) i la conserva si en té (Paper). Amb
`--persist` el cicle és un **reinici** i no un cicle d'alimentació —el domini del RTC no s'apaga— i la
RTC es conserva a tots tres models; la NVS i la FRAM no canvien mai, perquè són flash i ferrita.

**La memòria i el rellotge són dues coses diferents**, però totes dues tenen el mateix patró: el banc
no té la BM8563 i el que fa és el que fa el xip que sí que hi és. L'època surt de `--now` quan el run
la diu; si no, surt de `clock.bin` quan la placa té pila (el Paper, amb `source` = `"rtc"`) o quan el
run diu `--persist` (a l'ATOM, amb `source` = `"soc"`, que és el comptador del SoC que sobreviu a un
reinici); i si no hi ha cap de les dues, val **0** i `source` és `"none"`, que és una placa que ningú
no ha posat (docs/60). El que es conserva al Paper és també el contingut de `/rtc`, que una app pot
llegir i escriure.

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

Per tenir-hi les apps d'un build que les porti, es copien un cop:

```
tools/sync_emulator.sh
```

Fa el mateix que `mkdir -p emulator/littlefs && cp -rL build/disk/emulator/apps emulator/littlefs/`, però
**esborra primer** `apps/` i `res/` del disc de l'emulador: és una còpia, no una fusió, i una app treta de
`apps/` ha de desaparèixer també d'aquí. També hi ha la tasca de VS Code **lunokOS: seed emulator disk**.
**El disc del banc no en té, d'`apps/`** (0.88.2), o sigui que amb el disc per defecte aquest script no hi
posa cap app i ho diu.

**Per què cal recordar-ho** (només per a un banc que copiï el seu disc, `--flash emulator/littlefs`):
editar una app a `apps/` **no canvia res** en aquella còpia; el que corre és el disc de treball.
Amb el disc del build —el camí per defecte, `build/disk/emulator`— això no passa, perquè les seves
apps són enllaços al repositori.

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
| `1` | Una ordre ha fallat, una app ha mort o ha sortit amb un codi diferent de zero, o una línia no ha acabat dins del límit |
| `2` | Els arguments no s'entenen |

No hi ha cap codi més: una ordre o una app que falla fan **1**, i no hi ha cap `3` al binari. La
comprovació és la mateixa que fa la shell amb una persona: `luabind::failed()` i el codi de sortida de
l'app, i una línia que no acaba en **60 s** també és un `1`.

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
  SDL2 que `pkg-config` no conegui, `SDL2_CFLAGS` i `SDL2_LIBS`. El selector mira `sdl2-config`, després
  `pkg-config` i, si no hi són, aquestes dues variables. Sense cap de les dues, el banc compila i corre
  igual, dient que no té finestra. A Debian/Ubuntu la dependència de desenvolupament és `libsdl2-dev`; a
  macOS, `brew install sdl2`; a Windows, un SDL2 amb `sdl2-config` al `PATH` (MSYS2) o els dos camins i
  les dues llibreries a `SDL2_CFLAGS`/`SDL2_LIBS`.
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
- **La finestra de prova no és la de l'usuari** (2/11): amb l'backend X11, `--gui` necessita un
  `DISPLAY` —Wayland té el seu `WAYLAND_DISPLAY` i SDL2 el tria sol—, i el que rep una prova ha de ser
  **un X propi de la prova**, no el de la persona que treballa. La manera curta és `xvfb-run`, que encén
  un X virtual, hi apunta `DISPLAY` i el tanca en sortir:

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
  llegeixen les proves). Amb escala 2, el píxel de la finestra és un bloc de 2 × 2 de la pàgina i el dit
  cau **al mig del bloc** (`emu::gui_page_point`, amb prova a `test_ui`); un punter fora de la pàgina
  s'enganxa a la vora.
  Sense `--touch` no s'hi envia res, i amb el vidre girat en un altre marc el banc ho diu una vegada en
  comptes de callar. **El que el ratolí no pot fer és un pessic de dos dits**: això és del test o del guió.
  **Un clic és un dit i no un marc**: el xip escombra cada deu mil·lisegons i qui no hi és en un escombrat no
  hi és enlloc, o sigui que el banc guarda el toc fins que una lectura se l'emporta i la cua del nucli
  s'escurça a vint mil·lisegons amb la finestra oberta (0.83.0). Mesurat a la banqueta, dotze clics de 90 ms
  sobre la mateixa tecla del teclat: **una lletra abans de les tres arregles i deu ara**, amb una tecla
  premuda 1,8 s que escriu **una** lletra i no una ratxa. Les dues que falten són clics que entren plegats
  al panell mentre una escriptura del vidre té el fil (docs/90).
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
- **El signe d'una tecla és un dibuix** (0.85.1): les tres del dial deien `Up`, `Centre` i `Down` amb la
  taula de la barra, i una paraula dins d'un botó de vuitanta píxels fa que la finestra sembli un formulari.
  Ara `gui_key_icon` dona el signe com a **runs d'un píxel** —una fletxa amunt, un **cercle farcit** i una
  fletxa avall— i el SDL i l'X11 el pinten igual, o sigui que el dibuix i el mapa del clic surten de la
  mateixa casa. Una placa d'**una sola tecla** no en té, de signe: allà el botó és tot el costat i es queda
  la paraula. I els botons són de **quaranta per quaranta amb sis de buit** (eren vuitanta per
  cinquanta-quatre amb vuit): el que cal amb un ratolí és que la columna no es mengi la pàgina.
- **El que no s'hi veu**: el **temps** de la tinta, que només es nota perquè la finestra s'espera i el
  prompt no torna —el número es llegeix amb `stats` o amb `run panel`, no a l'ull—, i el moviment del
  pigment dins de cada moment (la finestra ensenya blanc, negre i la imatge, no els passos entremig). Per
  això la finestra serveix per **veure què dibuixa una app**, i el que es veu a l'ull continua essent el de
  la placa (docs/95).

## La tinta del vidre: la pàgina que ningú ha vist (1/11)

Un panell d'e-ink no encén píxels: mou pigment. El que hi havia **no desapareix en una pintada**, i
només les qualitats que netegen (`gc16` i companyia) tornen la pàgina. El banc ho simula amb una taula i
una barreja d'enters (`lib/hal_emulator/src/emu_ink.cpp`):

| Qualitat | Quant tarda | Què deixa | Blincades |
|---|---|---|---|
| `gc16` | **450 ms** | res: neteja, i la brutícia del píxel torna a zero | **quatre** (negre, blanc, negre, blanc) |
| `gl16` | **450 ms** | res: neteja | **dues** (negre, blanc) |
| `glr16`, `gld16` | **450 ms** | **dos setzens** del que hi havia, i deixa un punt de brutícia: no parpellegen i, per això mateix, no tornen la pàgina | cap |
| `du` | **120 ms** | **casi un terç** del que hi havia (cinc setzens), i deixa un punt de brutícia | cap |
| `du4` | 120 ms | **set setzens**, i el doble de brutícia | cap |
| `a2` | 290 ms | gairebé res | **dues** (blanc, negre) |
| `init` | 2000 ms | tot, i la brutícia del panell sencer | **quatre** |

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
`gc16` torna la brutícia a zero.- **No parpellejar té un preu, i és el que el sistema paga** (0.88.18): `glr16` i `gld16` són les
  qualitats amb què una app presenta la seva pàgina, amb què la barra repinta una caixa de l'hora i amb
  què es renta la banda del teclat —**perquè no parpellegen**, que és la regla d'aquest projecte—, i una
  forma d'ona que no parpelleja tampoc no es recupera la pàgina: aterra a prop del nivell i no al nivell.
  Deixa **dos setzens** del que hi havia —un vuitè, més clar que un `du` i molt més fosc que un `gc16`— i
  un setzè de brutícia per pintada, o sigui que una caixa que canvia cada minut acaba ensenyant el minut
  deabans. **Fins a la 0.88.18 el banc deia el contrari** («netegen sense ensordir la pàgina») i això feia
  gratis, aquí, l'única decisió que el sistema pren a tot arreu: una pàgina que no s'embruta mai.
  La prova que ho separa és `test_a_gld16_leaves_a_trace_and_a_gc16_takes_it_away`.- **El gra**: **vuit de cada mil** píxels d'una pintada de `du` no arriben al nivell que se'ls va demanar
(quinze de cada mil amb `du4`), i **una de cada quatre vegades la fallada és de dos nivells** i no d'un.
El píxel s'hi queda fins que alguna cosa neteja la zona. És determinista —un hash del lloc i no un
`random`— perquè una pàgina que canvia a cada tirada no es pot provar.
- **I la pintada ocupa el seu temps**: `idle()` no diu que sí fins que el rellotge del banc passa el bus
  de la regió **i** la durada de la qualitat, o sigui que `run panel` al banc dona el número de la placa
  (vegeu el bus, a sota).
- **El color és el del paper i no el d'una pantalla**: el nivell 15 és `#EFEADC` (paper calent), el 0 és
  `#1A1B18` (la tinta més fosca que arriba el pigment) i els catorze del mig són la recta entre els dos.
  El panell no té color i la finestra no n'inventa cap.
- **El parpelleig, en fases i en el seu temps**: els dos terços primers de la pintada són les **blincades**
  de la qualitat —quatre al `gc16`, dues al `gl16` i a l'`a2`, cap al `du`— i l'últim terç és **la imatge
  arribant**, en sis trossos visibles que van del nivell de l'última blincada al que va demanar la pintada.
  `step_ink()` és el que ho mou, i el demanen les dues coses que miren el vidre: `idle()` —una app que
  espera la tinta espera el parpelleig i l'arribada— i la finestra, que per això redibuixa quan canvia el
  comptador de blincades (`ink_steps()`) i no només quan canvia el de pintades. Un parpelleig que no es
  dibuixa no és un parpelleig. **I les qualitats que no parpellegen ho diuen amb el temps**: un `du` no
  canvia la pàgina fins que els seus 120 ms han passat, perquè la tinta no hi és abans. Les blincades i
  els temps són els del full del fabricant i no d'aquesta unitat: a la placa, `a2` es va mesurar com a no
  pintar res (docs/95, E-7), o sigui que aquí el banc és més optimista que el vidre que té algú a les mans.
- **El vidre treballa mentre el driver espera** (0.64.0). Una pintada té el seu cridador parat **més d'un
  segon** —el bus de la pàgina sencera, i la tinta— i el panell no s'atura: el controlador continua fent
  les blincades de la regió que li han donat. El banc dibuixava la finestra **un cop per torn**, i un torn
  és una pintada, o sigui que s'empassava justament el que la finestra havia d'ensenyar: entre la pàgina
  d'abans i la de després no hi havia cap fotograma (trobat a la finestra, 4/11: `run images` passava de
  la pàgina del shell a la imatge sense un negre). Des d'aquí `MemorySurface::wait_and_pump()` parteix
  cada espera del vidre —el `wait_paint` de la pintada anterior i l'enviament pel bus— en trossos de
  **10 ms** i crida el *pump* de la finestra entre tros i tros, que és `pump_gui` i la mateixa
  `step_ink()` de sempre. Amb una gravació a 25 fotogrames per segon de `run ghost` al `--gui`: **dos
  fotogrames negres, dos de blancs, dos de negres, dos de blancs** i després els sis trossos de l'arribada
  —que són els 450 ms del `gc16`—, i la finestra respon al teclat durant tot el temps. El model no canvia
  cap número: només qui mira i quan.

### El bus, que no és la tinta i també es paga (1/11)

Entre l'app i la tinta hi ha el cable. El driver envia **la regió que canvia** al controlador abans de
demanar-li que pinti (`It8951::flush`), i aquell enviament té dos preus: els bytes i les files.

| Peça | Quant costa | D'on surt |
|---|---|---|
| Un byte | **0,8 µs** | el bus del driver, **10 MHz** (`kSpiHz`): quatre bits per píxel, o sigui dos píxels per byte |
| Una fila | **1 100 µs** | **d'una mesura** i no d'un full del fabricant: la pàgina de `run panel` va costar **1 985 ms** a la placa l'1/11, i els seus comptes —570 ms de tinta, 233 ms de cable i 1 080 files— deixen uns 1 095 µs per fila. El driver envia **una fila per transacció**, i per què una transacció costa un mil·lisegon fora del cable no es respon aquí (docs/95) |

- **La pàgina sencera, amb la barra de 48 files, són 1 200 ms de bus**: **912 files** de 270 bytes a
  0,8 µs més les 912 transaccions (la vista d'una app fa 540 × 912). La barra dels setze nivells, 157 ms.
  Amb la tinta (120 del `du` i 450 del `gc16`), la pàgina de `run panel` dona **1 927 ms** al model.
  **La mesura de l'1/11, amb la barra de 28 files**, era d'**1 954 ms al model** i **1 970 ms al banc**
  —`stats` deia 1 240 ms pel pintat de `du`, 280 pel de `gc16` i 450 de tinta—, contra els **1 985 ms**
  de la placa: **un 0,8 %** (docs/95, E-7). És la mateixa aritmètica amb una altra vista.
- **I el que `run panel` imprimeix al banc és més gran, i amb raó**: **al voltant de 2 840 ms** el 3/11 amb la barra de
  48 files, perquè la **barra del sistema** es repinta amb `gc16` mentre l'app espera la tinta i
  `ui.idle()` de l'app espera també aquella tinta. `stats` en compta **cinc `glass.flush`**: el `du` de
  la pàgina i quatre `gc16` —el de l'escala i els de la barra que es repinta. És el que fa la placa quan
  la barra canvia; a la mesura de l'1/11 la barra no es va moure i per això no hi és.
- **I una pintada espera l'anterior**: un `present` de dues qualitats és **dos pintats**, i el segon no
  envia res fins que el primer ha acabat la tinta (`wait_paint`, docs/50). És el que fa que la pàgina sigui
  la **suma** —1 200 de bus del `du`, 120 de la seva tinta, 157 de bus del `gc16` i 450 de la seva— i no
  una cursa entre dues coses que al vidre van d'una en una.
- **I el bus atura el nucli, com a la placa**: el banc fa l'espera dins del `flush` amb `pal::delay_us`
  —`std::this_thread::sleep_for` al PC— i el fil no avança, que és el que passa mentre el bus està
  ocupat. És el mateix criteri que el port sèrie a 115 200 baud i que les lectures del disc: el cost
  s'hi paga, no es desa en un número.
- **I el codi de l'app també costa** (0.29.0): la placa executa cada instrucció de l'intèrpret a
aquests milisegons, i el PC a un gigahertz, o sigui que un bucle que al vidre dura un segon aquí dura
un mil·lisegon. El ganxo de l'intèrpret en compta **dos mil** i en paga el preu 
(`--lua-cpu`, **60 ns** a 160 MHz, dividit per la freqüència simulada) quan la suma passa de mig
mil·lisegon, i el que queda es paga al final del torn; amb això, `lua.own` i `lua.run` de `stats` diuen
a la placa i al banc el mateix ordre de número. És el número del model i no una mesura: es pren a la
placa amb un bucle i la fila `lua.own` (docs/95).
- **I el torn també costa** (0.58.0, 0.60.0): un torn del nucli és **1 ms** a la placa —FreeRTOS, els
controladors i la volta al bucle— i **0,15 ms** al PC, i la diferència es paga **després de cada
torn** (`kTurnOverheadUs` = 850 µs, `--no-delay` la treu). I **un venciment es despatxa al torn
següent**: quan l'espera s'acaba, el banc encara paga el torn que la placa triga a reprendre la tasca
(`kDispatchUs` = 1 000 µs), que és el mil·lisegon de més que fa que cent esperes d'1 ms costin 199 ms
a la placa i 115 al PC (docs/75). No es modela la instrucció del Xtensa: es modela el **torn**.
Mesurat el 3/11: 1 000 esperes d'1 ms triguen **2 107 ms** al banc amb el model encès i **1 064 ms**
amb `--no-delay`, contra els **1 990 ms** de la placa: un **+5 %**, que és la resolució del despertar
d'aquest PC i no una decisió.
- **Per això una pàgina al banc es nota**: amb `--gui`, una pintada de pàgina sencera té el cridador quiet
  més d'un segon. No és un defecte de la finestra: és el que fa la placa. I des de la 0.64.0 la finestra
  **no s'hi queda quieta**: el vidre camina la seva tinta durant l'espera i les blincades es veuen.

**`--no-ink`** treu el model i deixa el vidre de sempre: pintada instantània i exacta **i cap enviament**
(un vidre que canvia píxels no té res a enviar). És el que fan servir les proves —que no han de pagar mig
megabyte de buffers, mig segon de tinta ni un segon de bus per pàgina— i és el que queda si algú vol
comparar el dibuix amb el d'abans.

**Quant costen aquests dos buffers**: el model guarda *el que el vidre ensenya* i *la brutícia de cada
píxel*, quatre bits cada un, o sigui **259 200 B per buffer** a la pàgina de fàbrica —**518 400 B els
dos—**. És memòria del PC i no de la placa, però és el motiu pel qual **el model neix apagat a les
proves**: `set_ink(true)` és el que diu una prova que vol mirar la tinta, i la resta no paguen el mig
megabyte ni el mig segon d'una pàgina.

**El que continua sense ser el panell**: la tinta es mou **a fases i no en una corba contínua** —quatre
blincades i sis trossos d'arribada són deu fotogrames, no el pendent real d'un pigment—, el cost d'una fila
no està descompon·gut (1 100 µs és el que surt dels comptes d'una mesura, no una mesura d'ençà), i els
temps de la tinta són els del full del fabricant i no s'han vist en aquesta unitat. Tot el que és model és
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
| `width <1..16>` | Columnes d'un glif. Ha de ser abans del primer `char` |
| `height <1..16>` | Files d'un glif, i les línies que es llegeixen després de cada `char` |
| `gap <0..4>` | Columnes en blanc entre dos glifs |
| `char <codi>` | El codi que dibuixen les `height` línies següents, **en ordre i sense forats** |

- **Un fitxer que no és una cara es refusa amb el motiu**, i el motiu és el que l'app ha d'ensenyar a qui l'ha
  escrit: `not a lunok font`, `width is one to sixteen`, `height is one to sixteen`, `a glyph has fewer
  rows than the face is tall`, `the glyphs of a face come in order`, `a face with no glyph`. Una línia que
  no és cap directiva tampoc s'ignora: una errata en un fitxer és una cara que no funciona i això s'ha de
  dir.
- **Una cara pot ser parcial**: un codi que no hi és és un forat i la línia avança igual, de manera que una
  cara de deu xifres és legal i el text del voltant no es desplaça.
- **El que costa**: `96 × 16 × 2 = 3 072` bytes per cara —des de la 0.55.0 la cel·la és de setze per setze
  amb dos bytes per fila—, demanats a la memòria extra i comptats al pressupost de l'app (docs/75), més el
  text del fitxer mentre es llegeix (el bloc es torna de seguida). La cara torna quan l'app fa
  `font:release()` o quan l'app s'acaba, i un canvas que la feia servir torna a la font del sistema en
  comptes de llegir un bloc que ja no hi és.
- **I una cara es pot fer d'una font de debò**: `tools/ttf_to_lfont.py` converteix un `.ttf` en un `.lfont`
  a una mida triada (Pillow, el glif centrat a la cel·la perquè la taula és d'amplada fixa, el traç opcional)
  i refusa el que no cap a setze; és el que carrega `run fonts` (docs/30). I una app també pot carregar el
  `.ttf` sencer: `run buttons` ho fa amb Lato Black a 24 px i el nucli en dibuixa els outlines, que és el camí
  que no té taula de bits (docs/30).
- **I es mira amb dues apps**: `run fonts` posa la font del sistema i una cara carregada a la mateixa pàgina,
  amb el que cada cara diu de si mateixa i l'amplada d'una línia a la consola; `run sheet` dibuixa tots els
  glifs del sistema dins de la seva cel·la, que és el que diu si els bits cauen on toca. Totes dues van amb
  `--display` (o `--gui`) i s'acaben soles.
- **El que no hi és**: ni kerning, ni una taula per sota de `0x20` o per sobre de `0x7F`. Una cara de
  bits és un mapa de bits i un fitxer de text el descriu sencer; una cara d'**outlines** (`.ttf`) és
  l'altre camí i la dibuixa el nucli a la mida que es demana (docs/30).

## El tàctil: un dit que costa el que costa (1/11)

El vidre del banc tenia ulls i no dits. **El panell és del model** (`Model::touch`): una Paper del banc porta
el seu **GT911** des que arrenca, i el que se simula no és «on és el dit» sinó el xip: en aquesta placa el
que escanya una app és el que hi ha entre el dit i el full de dades
(`lib/hal_emulator/src/emu_gt911.cpp`, docs/50). Amb `--no-touch` el panell hi és i no contesta, que és el
cas d'una prova que no és sobre el tàctil.

| Què | El banc | D'on surt |
|---|---|---|
| El marc | **540 × 960**, i és el de la pàgina: un dit a (100, 200) és a (100, 152) de la vista, sota les 48 files de la barra | la configuració d'aquesta unitat, mesurada amb quatre tocs l'1/11 (docs/95, E-6) |
| Els dits | **2**, no els cinc que caben al marc | `0x804C` = 2 a la config 66 d'aquesta unitat |
| La mida del contacte | **28** al primer dit i **30** al segon | els dos números que va donar la placa amb dos dits (docs/95, E-6) |
| Què costa una lectura | **800 µs** sense dit, **1 830** amb un i **2 550** amb dos: l'estat, el marc i el zero que el tanca, a 100 kHz i nou bits per byte. Mesurat al banc, `touch.read` diu **935 µs – 1,1 ms** per a una lectura de 800: l'espera és `pal::delay_us` (`std::this_thread::sleep_for` al PC) i el planificador hi afegeix la resta | la física del bus del xip (docs/50) |
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
- **Un toc que comença i s'acaba entre dues lectures és encara el marc de l'últim escombrat** (0.83.0): el
  xip escombra sol i **guarda el que hi ha trobat** fins que una lectura se l'emporta, i el banc fa el
  mateix. Sense això, el ratolí de la finestra —que deixa anar el botó en el mateix torn en què el va
  prémer— no arribava mai al vidre: **una de cada vuit premudes** a la banqueta, mesurat amb vuit clics de
  90 ms sobre una tecla (docs/90). El toc es lliura **una vegada i prou**: la lectura següent diu que no hi
  ha ningú.
- **`finger_still_down()` diu si el marc és d'un dit que encara hi és** (0.83.0): el nucli el pregunta i, quan
  la resposta és que no, lliura el toc com una premuda **i la seva deixada alhora**. És el que fa que un dit
  que jeu sobre una tecla escrigui una lletra i un dit que toca i se'n va escrigui la seva, sense comptar
  temps ni endevinar res (docs/50, docs/90).
- **Una espera de la cua del nucli es talla a vint mil·lisegons quan hi ha finestra** (0.83.0,
  `pal::set_wait_slice_ms`): el fil d'un PC dorm en un `select` sobre la consola fins al pròxim venciment
  —sis-cents mil·lisegons en una app que espera—, i la finestra només es mira entre esperes, o sigui que
  dos clics podien entrar plegats al panell i sortir-ne com un de sol. La retallada és el doble del període
  d'escombrat del xip i no toca ni el model ni les proves, que la deixen a zero.
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
| **el binari, per defecte** | **es paguen** | mirar què costa una placa: `run panel` diu **al voltant de 2 840 ms** de pintada (3/11, barra de 48 files) |
| **`--gui`** | **es paguen, i és obligatori** | una finestra és per veure què costa una placa; `--gui --no-delay` es refusa sempre i `--gui --no-ink` quan hi ha vidre, amb una línia que diu per què |
| **`--no-delay`** | **cap espera** | un guió o una mesura que no sigui de temps: el mateix `run panel` dona **11 ms**, que és el sondeig de 10 ms de l'app i res del maquinari |
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

- **Es llegeix del nucli, i només quan el nucli és Linux**: `/sys/class/power_supply/*/capacity` i
  `/status`, el primer aparell de la llista que sigui una bateria i tingui percentatge. Un `150` o un `0`
  no és cap bateria i el banc no se'l creu; i si la màquina no en té cap —o el sistema és macOS o
  Windows, on aquest camí no existeix—, la cel·la del model comença al **seu càrrec nominal (3 700 mV,
  42 %)**, que és el que una Paper amb una cel·la de fàbrica diria —no `nil`, perquè una Paper no té
  aquest estat.
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

**`cat` imprimeix el fitxer sencer** des de la 0.82.2 (abans s'aturava als primers 1 024 B sense dir-ho) i
`hexdump` el llegia sencer des de sempre: el que triga una pàgina gran és el **cable** —1,3 MB a 115 200
baudes són uns dos minuts— i no un sostre amagat. Al banc, `cat /sd/http/watch` amb una targeta de debò
(`--sd emulator/sd`) treu els 1 327 963 B i l'última línia hi és; `hexdump /sd/http/apps` dona 2 783 files i
l'última comença a `0000ade0`, que és l'últim tros de 16 B del fitxer (docs/30, docs/70).

**El que va fer falta perquè els editors obrissin un fitxer de debò**: un bloc de la PSRAM que no podia créixer
on era es movia només a l'altra memòria, i si el veí estava ocupat el banc deia `not enough memory` amb quatre
megabytes lliures (docs/75, la secció del heap del banc). Ara pot anar a qualsevol taca de la seva.

Cada cosa que el banc simula té una app que la fa servir, i totes **corren igual a la placa**: el que es
veu al banc i el que es veu al vidre és la mateixa pàgina, i els números s'hi poden comparar.

| App | Què prova | Què mirar |
|---|---|---|
| `run panel` | la pàgina, la vora i les setze qualitats | que el dibuix hi és i recte, i `the ink took N ms` |
| `run squares` | una pintada per qualitat, cronometrada | `painted with du in N ms` contra `erased with du in N ms`, i cada vint quadrats un `erased with gc16 in N ms`: el rastre gris que queda és el dany, i el quadrat del `gc16` torna blanc |
| **`run ghost`** | **el fantasma**: vuit passades de `du` sobre el mateix bloc i un `gc16` al final | el bloc que guarda la forma de cada passada i el comptador amb els números dels passats a sota; al banc, a més, la finestra |
| **`run draw`** | **el tàctil**: un traç que segueix el dit, i un quadre que neteja la pàgina | que el punt cau on és el dit —amb `--gui`, el del ratolí—, el traç de la vora del gest i el temps de cada pintada |
| **`run cost`** | **el que costa tot**: `stat`, `list`, llegir, escriure, una pintada de cada qualitat, la bateria, el tàctil i la memòria | les dues taules (placa i banc) al costat: si no s'assemblen, algú menteix i `docs/85` diu qui |
| **`run images`** | **les imatges**: una de cada família que el sistema llegeix —JPEG, dos BMP (8 i 24 bits), GIF, ICO, PNG i els netpbm PGM/PPM/PBM— i **cada una ensenya el que aquell format sap fer**: una fotografia, tinta plana, una paleta, una cantonada transparent, una icona amb alfa, línies primes, dos degradats i un bit. Cada pàgina en diu la família, la mida, els bytes i per què és aquell format | que les nou imatges són **diferents**, que la transparència del GIF i de l'ICO es veu sobre el fons gris, que el degradat del `pgm` no fa franges i que el `pbm` és només tinta i paper. **El que han de dibuixar igual —el mateix pla en sis formats— es prova a `test_img`, amb les seves còpies a `test/test_img/data/plan.*`**. Des de la 0.64.1 els tres botons tenen `on_focus` i el focus es renta amb un `gc16` de la fila dels botons: l'anell que arriba surt nítid, el que se'n va desapareix, i el parpelleig és aquella fila i no la pàgina (docs/30) |
| **`run edit <path>`** | **l'editor de text** (0.65.0, i des de la 0.66.0 **sense sostre de mida**): modal com el vim, amb `h j k l`, `i a o`, `x dd yy p`, `u`, `/` i `:w :q`, sobre el teclat **en mode cru** (`console.raw`, `console.read_byte`) i dibuixat amb `term`. **El fitxer no es carrega mai sencer**: l'editor en fa un **índex de línies** llegint-lo a trossos de 64 KB (`fs.read_at`), llegeix només les línies que es veuen i desa copiant de l'original els trossos que no han canviat; una línia de més de 8 KB es veu per **finestra** i no s'hi escriu | que el fitxer s'obre i es desa **on el prompt l'ha nomenat**, que `Ctrl+L` torna a pintar la pàgina si una línia del log del sistema l'ha trencada, —l'app rep l'autoritat d'aquell camí i de cap altre—, que `ESC` torna al mode normal, que `dd` i `u` fan el que diuen i que la línia d'estat diu la fila, la columna i si hi ha canvis sense desar. **Pel port sèrie de la placa i per la consola nadiua és el mateix codi** |
| **`run editor [path]`** | **l'editor gràfic** (0.87.0): una pàgina que s'escriu amb el teclat del sistema i que es comanda amb la **barra de menús del sistema** —**File** obre, desa, desa com i tanca, i **View** porta `Read Only` i `Word wrap` amb una casella dins del menú—. El camí d'obertura i el de desar s'escriuen en una targeta amb el teclat, i el que no és el camí que el prompt ha nomenat ni el propi de l'app es refusa i es diu per què | que el menú es plega sol i no deixa la targeta pintada al paper, que passar d'un títol a un altre canvia el menú (i no dos), que `Read Only` amaga el teclat i la pàgina creix sense deixar les lletres de quan era petita, que un dit sobre el text de només lectura **no** torna a obrir la banda (0.91.0), que `Word wrap` mou les files del text i que desar **escriu el fitxer de debò** |
| **`run hexedit <path>`** | **l'editor hexadecimal** (0.65.0, i des de la 0.66.0 **sense sostre de mida**): la graella de setze bytes amb el desplaçament i l'ASCII, el cursor amb dos nibli, `0`–`9`/`a`–`f` per escriure, `x` esborra, `i` insereix, `u` desfà i `:w :q` per desar. Fins a 256 KB el fitxer és a la memòria (i `x`/`i` hi mouen els bytes); més gran, llegeix **la finestra que es veu** i desa cada byte al seu desplaçament amb un mànec, o sigui que un binari d'1,5 MB es pedaça sense tenir-lo sencer enlloc | que un fitxer binari s'obre sencer i sense pèrdua, que `41` sobre el byte `48` el deixa en `41`, que esborrar i inserir mouen el que hi ha darrere, i que `:w` escriu exactament els bytes que es veuen |
| **`run controls`** | **el que dibuixa el sistema** (0.69.0–0.81.1): una barra de progrés que s'omple, un `slider` que mou el dial i les files del panell | que el dial mou el control que té el focus i no un altre, que el valor es veu a la barra i que cada control respon a la tecla que li toca |
| **`run menu`** / `run menubar` | el menú contextual i la barra de menús: el menú s'obre **on és el dit** i la barra té el mot de baix invertit i un menú penjant | que tocar una nota obre el menú al seu lloc, que l'entrada marcada és la que s'activa i que el menú es tanca sol |
| **`run typing`** / `run notes` | el teclat del sistema dins d'un `textbox` i d'un `textbox_multiline`: el cursor hi parpalleja, el dial camina les tecles i `Enter` parteix la línia (o baixa la finestra) | que cada tecla surt on toca, que el text del camp és el que s'ha escrit, i que un dit fora de la caixa tanca la banda i un dit a dins la torna a obrir (0.91.0) |
| **`run dialog`** / `run form` / `run checkbox` | el diàleg amb dues portes, els camps que es trien i la casella que s'encén | que el dial recorre les portes i les opcions, que el centre n'activa una i que el que queda triat és el que l'app llegeix |
| **`run trend`** / `run grid` | el `sparkline` d'una finestra de mostres, el pastís i la graella de cel·les amb pàgines | que el gràfic segueix les mostres, que el pastís reparteix el total i que la graella passa de pàgina amb el cursor |
| `run glass` | el tàctil a la consola | els punts de cada frame i els gestos amb la seva durada |
| `run keys` | les tecles del dial | que la posició que prems és la que surt al compte, i a dalt/centre/baix amb la tira de tecles de `--gui` |
| `run battery` | el gauge | mV, `%` i que `charging` no hi és |
| `run mtab` | el disc i l'espai | que quatre noms de la mateixa partició són quatre passejades |
| **`repo`** (comanda del prompt, no app) | **les apps que té el repositori, dites pel repositori** (0.82.0): demana a l'API de continguts de GitHub el directori `apps/` de `lunokjod/lunokOS` —la versió crua d'aquella pàgina del navegador— i en llista les carpetes; **`repo info <app>`** (0.82.1) en llegeix el `manifest.json` cru i en fa una fitxa amb títol, versió, autor, entrada, extensió, plaques i descripció; **`repo install <app>`** (0.86.0) baixa el directori sencer —carpetes incloses— al disc que `cfg` diu, i al banc es comprova amb una targeta de debò (`--sd`) i el curl d'aquesta màquina: `repo: images in /sd/apps/images, 11 files, 1 folder, 67494 bytes` | que el llistat arriba **sencer** (45 KB de JSON, o sigui que va amb `http_download` a `/littlefs/http/apps` i es llegeix a trossos de 2 KB) i que surten les 60 carpetes amb el seu nom; que la fitxa cap en **2 KB de memòria** i que una app sense `entry` es diu `not a usable app`; al banc necessita xarxa de debò o `--http-fake`, i amb `--no-wifi` diu que la placa no té ràdio |

## Fidelitat: què s'assembla i què no

| Aspecte | A l'emulador | A la placa |
|---|---|---|
| Nucli, Lua, VFS, promeses | **Idèntic** | Idèntic |
| **L'arrencada** (0.60.2) | **El que la placa fa abans que existeixi el nucli**: el bootloader llegeix la imatge de la placa —**2 097 008 B** a l'ATOM i **2 182 672 B** al Paper, mesurats amb `-t size` el 3/11— a **10 MB/s** (dual a 40 MHz, el mode amb què arrenca), i després el **test de PSRAM** d'ESP-IDF (`CONFIG_SPIRAM_MEMTEST`, actiu per defecte) escriu i llegeix els 4 MB mapejats a **20 MB/s** (quad a 40 MHz): **210 ms a l'ATOM i 638 ms al Paper**. El banc els paga abans del banner, i el prompt arriba quan arribaria al vidre: **0,39 s i 0,84 s** fins al prompt (abans, 0,19 i 0,20). `--no-delay` no els paga. El que **no** és del model: el que el ROM i el bootloader triguen a més de llegir la imatge, i el primer refresc del panell | El mateix, més el que triga el propi ROM |
| Resolució de camins i permisos | Idèntic | Idèntic |
| Sostre de memòria | **El de la placa, i no més**: l'ATOM contesta el sostre per defecte (64 KB, `Model::app_limit_kb` = 0, que vol dir «la placa no en diu cap») i el **Paper contesta la seva PSRAM mapejada (4 096 KB, de 8 MB que en té el xip)**. `--mem-cap` el pot **baixar** per veure-hi el pic, i demanar-ne més es refusa amb el número que té el model. `sys.heap()` dona el pressupost de l'app (`free`, `used`, `limit`, `peak`) i la memòria extra del model (`extra_total`, `extra_free`), i `mem` escriu els tres números del model (`Model::psram_kb` mapejats i `Model::psram_chip_kb` del xip) | 320 KB de SRAM al xip i, al Paper, **4 MB de PSRAM mapejada** on van els blocs de 4 KB en amunt (docs/75) |
| **RAM de debò, un heap de debò** (0.58.0, 0.60.0) | **Els dos casals del xip i un heap dins de cadascun** (`pal::set_memory_bytes`, `pal::memory_left`): `Model::free_heap_bytes` (338 864 B a l'ATOM i al Matrix, 162 728 B al Paper) i `Model::extra_free_bytes` (4 095 KB al Paper, cap als ATOM). Un casal és **una regió de bytes tallada en trossos** amb capçalera de 16 B —mida, mida del tros anterior, casal i si és pres— i els lliures van en una llista que l'assignador recorre: **el primer que hi cap es parteix** si el que en queda val la pena (48 B), i quan un bloc es deixa anar **es torna a ajuntar amb el veí lliure d'abans i el de després**. Els trossos que ja no es toquen queden separats, que és el que fa que una pàgina **no hi capigue amb el lliure total de sobres**: `mem` en diu el **bloc més gran** que hi cap ara (`HeapInfo::largest_free_bytes`, com `heap_caps_get_largest_free_block`) i no només el lliure. Un bloc que no cap al casal que vol va a l'altre, i un que no cap a cap dels dos **es refusa**. **El temps també es paga**: cada node de la llista que es recorre són **100 ns** (fins a setze, que és el que costa la cerca del xip) i un bloc que canvia de casal es copia a **20 MB/s**, que és el que dona la PSRAM en quad SPI a 40 MHz. Mesurat el 3/11: una taula de 400 000 enters (~6,4 MB) al Paper mor amb `lua: not enough memory` i la PSRAM torna sencera quan el bloc es deixa anar; 64 blocs de mides que no lliguen donen **cada byte** en alliberar-se. El que **no** és del model és la **pila de les tasques**: al banc no n'hi ha (és el procés del PC) i `stack_free` torna −1 en lloc d'un número inventat (docs/75) | El `heap_caps_malloc` d'ESP-IDF amb les seves regions i el seu bitmap de nivells; i cada tasca té la seva pila, comptada en aquests 320 KB |
| **Rellotge de la CPU: el torn** (0.58.0) | **El que costa un torn del nucli, pagat després de cada torn** (`kTurnOverheadUs` = **850 µs**, la diferència mesurada a `docs/75`: 100 esperes d'1 ms triguen 199 ms a la placa i 115 ms al PC). Amb el model encès i el despatxament del venciment, 1 000 esperes d'1 ms triguen **2 107 ms** al banc (1 064 ms amb `--no-delay`), i el que **no** es modela és la instrucció: no hi ha cap Xtensa aquí i el que costa una línia de Lua es cobra amb `--lua-cpu`, no amb el rellotge del PC | FreeRTOS despertant cada mil·lisegon amb els seus canvis de context, i la instrucció al ritme del vidre |
| **Imatges (0.63.0)** | **Els mateixos descodificadors, byte a byte**: `ui.image` obre el fitxer pel VFS del banc i el passa per `lib/img`, que és el codi que compila també per a la placa —PNG, JPEG, BMP, GIF, ICO i netpbm—, i les files cauen al canvas amb el mateix empaquetament de quatre bits. **El que no és igual**: la transformada del JPEG és en coma flotant, i el banc en té de doble precisió i el xip de simple —les setze tintes són molt més gruixudes que la diferència, i les proves ho comproven amb un marge d'un nivell—; i el disc de la imatge es paga per la mateixa capa que cobra les lectures (docs/70, docs/75) | El mateix codi a 240 MHz: el que triga és la transformada i l'inflate, i el refresc de la tinta és molt més llarg |
| Cost d'una lectura | **Simulat per dispositiu** (`emu/disk.h`): la flash llegeix **blocs de 4 KB en 819 µs** (SPI nor a 40 MHz) més 60 de comanda, la targeta **sectors de 512 B en 410 µs** (10 MHz) més 200 de comanda, i la FRAM **90 µs per byte** (I2C a 100 kHz). Una escriptura a la flash paga els **setze programes de pàgina de 0,7 ms** que té un bloc de 4 KB —**11,2 ms**, el tPP típic del W25Q32JV, no els 0,4 ms que deia la primera taula— **i els 45 000 del sector que s'ha d'esborrar** (el tSE típic del mateix full de dades), que és d'on vénen els segons d'escriure un fitxer. **La NVS no paga el sector a cada clau**: el magatzem del xip afegeix entrades de 32 B a una pàgina de 4 KB i l'esborra quan s'omple, o sigui que una clau és **un programa de pàgina (0,7 ms)** i el sector de 45 ms cau **una vegada cada 126 claus** (`emu_nvs.cpp`), que és el que fa `nvs_set` i no el que fa un commit de littlefs (docs/70, docs/85) | Mil·lisegons de flash o SD |
| Cost d'un xip de la I2C | **Pels bytes que mou, a 100 kHz**: un byte són nou bits al fil —**90 µs**, el vuitè i l'acknowledge—, i la suma dels bytes de cada transacció és el que es paga. **Una mesura de l'aire** (SHT30, `env.read`) és el **tMEAS,m del full de dades, 4,5 ms típics i 6 de màxim**, més els deu bytes de la comanda i la resposta: **5,4 ms** mesurats a `stats`. **Una mesura del sensor de la Matrix** (MPU6886, `imu.read`) és l'adreça, el registre i els catorze bytes de la mesura, **1,5 ms**. **El rellotge** (BM8563) són deu bytes quan es llegeix a l'arrencada i quan s'hi escriu l'hora (`set_epoch`), **0,9 ms**. I el **FRAM** ja els pagava. La temperatura i la humitat triguen el que triguen perquè el xip mesura de debò; el banc no les inventa (docs/50, docs/85) | Unes desenes de mil·lisegons per mesura al xip de veritat (docs/95) |
| Cost d'un llum | **El que tarda el RMT a escriure el fil**: vint-i-quatre bits per llum a 1,25 µs són **30 µs per llum** i **80 µs de tancament**, o sigui **110 µs** al botó de l'Atom i **830 µs** al panell de 25 del Matrix, amb les interrupcions parades mentre dura (`led.show`). Mesurat a `stats`: **906 µs** de mitjana al Matrix, que inclou la resolució del despertar del PC (docs/50, docs/85) | El mateix, i un fil que no torna fins que el RMT ha acabat |
| Targeta (SD) | **Un directori del disc, i només si el model té ranura** (el Paper): el mateix backend POSIX que la flash, o sigui que el que llegeix aquí llegeix a la placa. Es presenta com una targeta de **16 GB** amb el lliure del PC, i el que falta fins a 16 GB és l'ocupat | FATFS per SPI a la Paper: CS GPIO4, 10 MHz i el CS del panell (GPIO15) deseleccionat abans de tocar el bus (docs/50) |
| El vidre del banc (`Model::glass`) | **Un buffer que és el panell** (`emu::MemorySurface`, 4 bits per píxel): recorda **el rectangle brut de cada pintat** —el mateix que el driver del panell envia, marcat per cada escriptura i buidat a `flush`— i la qualitat que es va demanar, i en guarda els **vuit últims**. Des de la 0.22.0 **la pintada mou tinta** —un `du` deixa una part del que hi havia i acumula brutícia, un `gc16` neteja, i vuit de cada mil píxels fallen un nivell o dos— i des de la 0.23.0 **paga el bus**: 0,8 µs per byte a 10 MHz i 1 100 µs per fila, i `idle()` no diu que sí fins que han passat el bus i la tinta (docs/85). Des de la 0.64.0 **el parpelleig arriba a la finestra**: les esperes del vidre es fan a trossos de 10 ms i `pump_gui` hi entra entremig, o sigui que un `gc16` són dos fotogrames negres, dos de blancs, dos de negres i dos de blancs abans de la imatge (gravat a 25 fps, 4/11). Les proves hi corren amb el model fora (`--no-ink`). **I el gir és diferent**: girar-lo torna a fer el buffer, o sigui que el banc s'oblida del que hi havia, cosa que un vidre de tinta no fa mai | El vidre de veritat: la tinta hi queda i un gir obliga a tornar a enviar la pàgina sencera. La mesura de l'1/11, amb la barra de 28 files, era la mateixa a les dues bandes (1 985 ms a la placa, 1 970 al banc, 1 954 al model); amb la barra de 48 la pàgina del banc és una altra i el que queda obert és el perquè del cost d'una fila (docs/95, E-7) |
| Desgast de la flash | No existeix | Existeix |
| Consum | No existeix | Existeix |
| Despertar de son | Simulat | Real o inexistent segons la placa |
| L'hora entre execucions (0.59.0) | **La recorda com la recordaria la placa**: `clock.bin` porta l'època i el segon del PC en què es va escriure, i el temps que la placa ha estat desendollada se suma en llegir-la, que és el que fa una pila. Al Paper sempre (`source` = `"rtc"`), als ATOM només amb `--persist` (`"soc"`, perquè un reinici no buida el domini del RTC), i `--now` mana sobre les dues | La BM8563 amb pila al Paper, i el comptador del RTC del SoC als ATOM |
| Espai lliure (`fs.space`, `mounts`) | **La targeta sí**: és una targeta de **16 GB** i el lliure és el que queda de debò al disc del PC, així que el que falta fins a 16 GB es mostra com a **ocupat** (`free 6587 MB of 16384 MB`, 31/10). **La flash també**, des del 31/10: el total és el de la partició de la placa (`Model::disk_bytes`, 0x0E0000 = 917 504 B a l'Atom i 0xBE0000 = 12 451 840 B al Paper) i l'ocupat és el que sumen els fitxers del directori, amb el que quedi al disc del PC com a sostre (si el PC s'omple, es mostra com a ocupat). Mesurat a l'Atom: `/data littlefs rw  915701 of 917504 B free`. `/nvs` segueix dient `E_UNSUPPORTED` a les dues bandes, perquè guarda entrades i no bytes | Números de debò (`/littlefs` 634 880 B lliures de 917 504 el 30/10, i `/nvs` diu `E_UNSUPPORTED` a les dues bandes) |
| Port sèrie | stdin/stdout en mode cru, **i a la velocitat de la placa en les dues direccions**: 115 200 baud, o sigui 86,8 µs per byte. Els bytes **surten** per tandes de dos mil·lisegons com sortirien del cable en lloc de sortir tots de cop, i els que **arriben** es lliuren al nucli quan el fil els hauria portat, o sigui que una línia enganxada al terminal arriba durant el temps que trigaria a teclejar-se (0.60.1). Mesurat: 2 563 B costen 0,21 s (86,8 µs × 2 563 = 0,22 s); i un `help` sencer en un pseudo-terminal, **2 577 B en 0,79 s**, que és el fil (0,22 s) més les esperes del propi nucli entre línies. `--baud <n>` la canvia i `--baud 0` treu l'espera de les dues bandes | UART a 115 200 |
| Xarxa | **De mentida**: connecta a res en `--wifi` ms i no surt enlloc. **El que no és de mentida és el que es recorda**: des de la 0.59.0 el nom i la contrasenya van a la NVS (`lunoknet/ssid` i `wifidrv/pass`, com els desa la placa), o sigui que un run que arrenca després diu el mateix `network:` i `wifi connect` s'hi torna a posar | La de debò (`WiFi.begin`, que no espera) |
| Provisioning | **De mentida**: un AP que no existeix i unes credencials que surten de les opcions | El component d'Espressif: l'app ESP SoftAP Provisioning del telèfon el troba de debò |
| Pàgines (`http.get`) | **La pàgina és la de debò i el camí és el mateix**: la cua de peticions, la promesa, el termini que compta el nucli, el cos màxim i els codis `E_*` són els del nucli, i el cos i el codi són els que contesta el servidor. Qui la demana és `curl` en un procés d'aquesta màquina (`-q`, sense `~/.curlrc`, seguint fins a 10 redireccions, amb el certificat verificat contra les autoritats **d'aquesta màquina** i no contra el paquet del build), i el banc paga el que el servidor trigui de debò. Un `curl` que no hi és dona `E_HTTP` i ho diu a la consola; `--http-fake` torna la pàgina de mentida per a una prova que no ha de dependre de la xarxa. **El que no és igual**: els errors de certificat són els del magatzem d'aquesta màquina —un servidor amb una autoritat que el build no porta passa aquí i no a la placa, i al revés— i el banc no té la ràdio del model, o sigui que no paga el temps de l'associació ni el del paquet. **Una pàgina que va a un volum sí que paga el disc**: el cos es copia del fitxer de `curl` al volum per la mateixa capa que cobra les escriptures, o sigui que mig megabyte de flash són els seus 0,7 ms per cada 819 B (docs/70, docs/75) | `esp_http_client` amb TLS i el paquet d'autoritats del build; el codi, el cos i els errors de certificat són els de debò |
| Energia (`power.*`) | **De mentida i ho diu**: un model amb indicador (el Paper) contesta la bateria **sense que calgui res** —el `%` d'aquesta màquina passat per la corba de la cel·la, o 3 700 mV si la màquina no en té— i `--battery-mv` hi posa un número i mana; un model sense indicador (els ATOM) contesta `nil` sempre. I dormir vol dir que **el rellotge virtual salta** fins a l'hora demanada. El PC no s'atura. `poweroff` sí que és de debò en el que importa: **el model del Paper contesta que sí i el procés s'acaba** (l'estat de les memòries es desa en sortir, com en una apagada), i el model de l'ATOM contesta que no perquè no té control del seu corrent — el banc diu el que diu la placa | Bateria si la placa en té; el son és el de debò: `esp_light_sleep_start` i, en profund, la placa es reinicia; `poweroff` fa la seqüència del fabricant (GPIO23, GPIO5, GPIO2 i son profund) i la placa es queda sense corrent |
| Tornar d'un son profund | **No passa**: l'emulador no es reinicia enmig d'una crida, i per això les proves criden `kernel::power::restore_session()` a mà, que és exactament el que fa l'arrencada | El xip es reinicia i arrenca: l'arrencada llegeix la nota del son i torna a obrir l'app amb `app.args.restore` |
| Velocitat del processador | **De mentida en el rellotge i de debò en el preu**: el PC no canvia de marxa, però el número es desa (arrenca a **160** i accepta 80, 160 i 240) i **el codi de l'app es cobra a aquesta velocitat** (`--lua-cpu`, 60 ns per instrucció a 160 MHz): un bucle que al vidre dura un segon aquí dura un mil·lisegon amb el model apagat i el que toqui amb ell encès. I el **torn** també es cobra: 850 µs després de cada torn (vegeu la fila del rellotge de la CPU) | `setCpuFrequencyMhz` de debò, amb l'APB i el tick de FreeRTOS movent-se amb ell |
| Llum (`led.*`) | **De mentida**: el color es desa i el test el pot preguntar (`emu::led_red()`… i `emu::led_frames()`, que compta els frames). El **nombre de llums és el del model** (1 a l'ATOM, 25 al Matrix, cap al Paper) i el sostre del canal és **20**, el del panell, a tots tres; `emu::led_pixel(i, …)` en llegeix un: així una app escrita per al panell es pot provar al PC. Els efectes es proven **movent el rellotge a mà** (`emu::advance_ms`), que és la mateixa hora que llegeix el nucli: una respiració de cinc segons es mira en un mil·lisegon | El `neopixelWrite` i el frame d'RMT de debò, sobre el WS2812 o els 25 del panell a GPIO27 |
| Apps i manifest (`boards`) | **El banc és una placa, la que se li diu i és obligatori dir-la**: `--board m5stack-atom`, `m5stack-atom-matrix` o `m5stack-paper`, i respon **aquell** nom i la família `esp32`. Una app del seu model corre sense dir `emulator` al manifest; una d'un altre model hi surt llistada amb l'avís i `run` la refusa dient les plaques que vol i quina és aquesta. És el mateix criteri que a la placa, i per això provar-ho al PC vol dir alguna cosa (docs/40) | `apps` la llista amb el que necessita i `run` la refusa dient les plaques que vol i quina és aquesta |
| Sensor (`imu.read`) | **De mentida i a la mà del test**: contesta el que diuen les opcions (`--imu-*`), i `--imu-fail` és un xip que no hi és. El que es prova aquí és el camí de la dada —de Lua al HAL i de tornada a l'app— i no la qualitat de la mesura, que només es pot veure al maquinari | L'MPU6886 per la I2C interna, amb les escales del full del fabricant (docs/50) |
| Aire (`env.read`) | **De mentida i a la mà del test**: el model del Paper té sensor i els ATOM no, i les dues xifres són el que diuen `--env-celsius` i `--env-humidity`; `--env-fail` és un xip que hi és i no contesta, que és `E_IO` i no `E_UNSUPPORTED` | El SHT30 per la I2C interna: una comanda, sis bytes i dos CRCs (docs/50). **El banc no té bus**: no hi ha cap clock stretching que es pugui comptar, així que un sensor que el banc llegeix bé pot ser un sensor que la placa dona per mort (va passar l'1/11: `env yes` al PC i `env no` a la placa, docs/50) |
| Tecles (`button.*`, `on_button`) | **De mentida i a la mà del test o del ratolí**: `emu::press_button(true/false)` prem la principal i `emu::press_key(n, true/false)` qualsevol de les tecles **del model** (fins a tres; una a l'ATOM i al Matrix, tres al Paper), i amb `--gui` els botons de la finestra fan el mateix amb un clic. El temps que es compta és el del PC. La forma, però, encara és la de la Paper a tots els models: un dial compost a la cara **dreta** (`centre`, `up` i `down`), o sigui que una app que tria per posició es pot provar al PC. Fer que la forma segueixi el model és el que queda de M8 (docs/90) | El GPIO39 de l'ATOM (una tecla simple a la cara de dalt) i les tres de la Paper, amb 10 ms de rebot |
| Tàctil (`touch.*`) | **Un GT911 simulat** (`Model::touch`, `emu::Gt911Panel`): el marc de la pàgina, dos dits, la mida del contacte i les tres transaccions del bus a 100 kHz (**800, 1 830 i 2 550 µs** segons els dits). **Escombra sol i dona el marc una vegada**, o sigui que un lector més ràpid que el vidre llegeix «ningú» la meitat de les vegades (docs/85) | El GT911 de debò: el mateix protocol, i a sobre un bus que comparteix amb el rellotge, la FRAM i el SHT30, amb clock stretching de fins a 6 ms del sensor i NAKs possibles |

**I el que sobreviu d'un run a l'altre, comprovat al banc el 3/11 amb la 0.59.0** i el mateix
`--state` a cada run: `wifi add mynet secret` deixa `nvs/lunoknet/ssid` (`mynet`) i
`nvs/wifidrv/pass` (`secret`), i el run següent diu **`network: mynet`** abans de fer res i
`wifi connect` s'hi torna a posar; `wifi forget` esborra el nom i `wifi` torna a dir `network: none`. A l'ATOM, un `rtc.bin` començat amb `ABCD` torna a zeros
després d'un run sense `--persist` i **hi és amb `--persist`** (un reinici no apaga el domini del
RTC); al Paper hi és sempre. I l'hora: `--now 1700000000` al Paper i, al run següent sense `--now`,
la línia és **`[CLOCK] source rtc epoch …`** i el número ha crescut amb el temps que el banc ha estat
parat (la pila compta); a l'ATOM sense `--persist` torna a **`source none epoch 0`** i amb
`--persist` a **`source soc`**.

**Comprovat al banc el 31/10 amb la 0.8.2**, amb `--board m5stack-paper --sd <dir>` i un guió:
`mem` diu `extra memory 4096 of 4096 KB usable, of 8192 KB on the chip (psram)`, `mounts` diu
**`mounts: 9`** (amb `/rtc` i `/fram` entremig, i `/nvs` donant `entries 1`), `run mtab` en llista
nou amb els bytes de cadascun, i `help` ensenya `sleep at <epoch seconds>`. La línia `free heap`
**també hi és des de la 0.58.0** i dona **el número del model i no un d'inventat** —`free heap
162728` al Paper, `338864` als ATOM, que és el que la placa diu al prompt (docs/75)—, perquè el
banc porta el llibre dels dos casals de la memòria (vegeu la fila de la RAM). I des de la 0.60.0 la
línia del **`largest block`** hi és i diu el que el lliure no diu: al prompt del Paper, `free heap
162728` i `largest block 162712` (el bloc sencer menys la capçalera); després de `run heap` i del
seu `sys.gc('full')`, el lliure torna a **162728** i el bloc més gran es queda a **154416**, que és
la fragmentació que deixa un app d'un megabyte (mesurat el 3/11).

**I la capa de controls diu el mateix a les dues bandes** (1/11, 0.19.0): `run layers` dona **les sis
línies iguals** al banc amb el vidre del model i a la Paper —els comptes `1 / 1 / 2`, el pare al slot 3, el marc
sense vista i `canvas (gone)`—, o sigui que la composició i el dany que es proven aquí són els que passen
allà (docs/95, E-8). L'única cosa que el banc no pot dir és el **temps de tinta**, que és justament el que
`docs/95` encara té pendent.

**El que encara no fa.** El maquinari fals **ja segueix el model** —capacitats, llums, nombre de tecles,
sensor, indicador de bateria, ranura i FRAM surten de `emu::model()`—, i el que queda és la **forma de
les tecles**: el dial compost de la Paper és el de tots tres models, quan el model de l'ATOM n'hauria
de tenir una de simple a la cara de dalt. Prémer una tecla **a mà** sí que es pot: amb `--gui`, el ratolí
sobre la tira de tecles crida `emu::press_key` (i la secció de la finestra ho diu). La forma de les
tecles és a [`90 · Full de ruta`](90-full-de-ruta.md), M8.

**I el log del banc és el de la placa** (1/11, 0.20.0): les mateixes línies amb la mateixa forma
(`[I][CFG] /cfg on littlefs (asked for littlefs)`, `[I][UI] view 540x912 at 0,48, 246240 bytes of
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
