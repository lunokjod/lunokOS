# 90 · Full de ruta

## Regla general

Una fita no està feta perquè el codi compila. Està feta quan **passa el seu criteri** a l'emulador i,
quan en depèn, a la placa. Cada fita diu què s'ha de mesurar, i el número que surti es desa a
`75 · Pressupost de memòria`. **Les mesures de maquinari, amb el comandament i on va cada número,
són al full de camp: `docs/95-mesures-a-la-placa.md`.**

Aquest document només té **el que falta**. El que ja està fet es llegeix al codi i a l'historial de
git, i el que el sistema **fa** avui s'explica als altres documents: `10` (mapa), `20` (intèrpret),
`30` (API de Lua), `40` (apps), `50` (maquinari), `60` (temps), `70` (fitxers), `75` (memòria),
`80` (compilació) i `85` (emulador).

Estat d'avui, en una línia: **M0, M1, M2, M3, M4, M5, M6 (mecanisme) i M7 estan fets**; el que queda
són les mesures de camp d'M5, M6 i M7, **M8** (la forma de les tecles al banc i poder-les prémer a mà),
dues
crides que ningú no ha demanat encara (`net.softap` i escriptura a `/nvs`), la resta del contracte
de Lua que no té codi al darrere (`docs/30`, taula d'⏳) i les decisions que necessiten maquinari o
una persona al davant.

## El contracte de Lua que encara no té codi

Una app que cridi qualsevol d'aquestes peta amb «attempt to call a nil value», i cada una té el seu
perquè:

| Crida | Per què no hi és | On va |
|---|---|---|
| `console.flush`, `console.read`, `console.read_byte`, `console.on_data` | cap app no les ha demanades; `read_line` cobreix el teclat | M1 |
| `sys.reboot` | reiniciar sense dir res a qui hi ha al davant no és una crida d'app | M6 |
| `time.utc`, `local`, `format`, `parse`, `set`, `tz` | el rellotge dona l'hora i prou; cap app ha demanat formatar-la | M3 |
| `time.sync` | ja hi ha `net.ntp`, que és el que fa la feina | M5 |
| `Promise.all`, `Promise.race` | cap app ha necessitat esperar dues coses alhora | M1 |
| `app.spawn`, `app.raise` | una app té un sol fil i una finestra d'alarma ja passa a primer pla amb `raise` de l'alarma | M2 |
| `app.save`, `app.restore` | una app desa on sobreviu, al disc (`fs.data`), i ha estat suficient | M2 |
| `store` | és un fitxer petit per app, i `fs` ja el pot fer | M4 |
| `log` | una app escriu a la consola, i l'anell de log del sistema ja existeix per a ell | M1 |
| `net.softap` | el HAL sap aixecar un AP pel provisioning; falta la crida, i cap app l'ha demanada | M5 |

La llista no és una promesa: **una crida d'aquestes s'afegeix quan una app la demana**, amb la
política a dins del sistema i la seva prova, com qualsevol altra (docs/10).

## M0 · Les dues bandes arrenquen i parlen

Feta. El que queda és el que queda de tota la llista: res d'específic.

## M1 · Consola i promeses

| | |
|---|---|
| **Què queda** | Les crides de consola i `sys.reboot` de la taula de dalt, `Promise.all`/`race` i el mòdul `log`. |
| **Criteri per tancar-les** | Cada una amb la seva prova: una app que llegeix bytes sense perdre el teclat, un reinici que diu per què, i dues promeses que es compleixen alhora. |

## M2 · Apps i manifest

| | |
|---|---|
| **Què queda** | `app.spawn`/`raise`/`save`/`restore`, si una app els demana. El selector d'extensió ja pregunta sempre i `open <app> <path>` en imposa una. |
| **Es mesura, i no està mesurat** | El temps d'escanejar les apps d'un disc ple, i el pic de memòria de llegir un manifest. Avui no hi ha número. |

## M3 · Temps i alarmes

| | |
|---|---|
| **Què queda** | `time.utc`/`local`/`format`/`parse`/`set`/`tz`, si una app els demana. Les alarmes persisteixen, es reprodueixen amb `late_ms` i compten les repeticions perdudes. |
| **Es mesura, i no està mesurat** | L'error d'entrega amb el sistema despert, a la placa; i quantes alarmes hi caben a la NVS abans que digui que no. |

## M4 · Disc

| | |
|---|---|
| **Què queda** | **Escriure a `/nvs`**: el muntatge és de només lectura i el costat d'escriptura no hi és. I el mòdul `store`, si una app el vol. |
| **Fora** | La targeta SD **a l'ATOM**: no hi ha maquinari. Al Paper hi és i la munta el seu HAL (docs/90, pas 7). |
| **Es mesura, i no està mesurat** | L'espai que ocupa una LittleFS buida, i el temps d'una escriptura atòmica amb el disc ple. |

## M5 · Xarxa

| | |
|---|---|
| **Què queda** | `net.softap` (que una app aixequi el seu punt d'accés: el HAL ja ho sap fer pel provisioning). I decidir si la placa es connecta sola a l'arrencada, que és una decisió d'energia. |
| **Cal provar a la placa** | Que `network:` surti amb el nom després de provisionar i que un reinici el recordi; `ntp` contra un servidor de debò i el desfasament que en surt; i una baixada d'una pàgina amb TLS, amb el heap durant l'apretada de mans i el temps que triga. |
| **Nota** | Amb el Bluetooth a dalt queda molt poc heap, i els buffers de `mbedtls` d'aquest build són de 16 384 B: **TLS i provisioning per Bluetooth no poden coincidir**. No és una opinió, és un número (docs/75). |

## M6 · Energia

| | |
|---|---|
| **Què queda** | **Les dues mesures de la fita**: el consum amb un mesurador USB (en repòs i en actiu) i la **desviació de l'hora després de N hores en son** (l'ATOM porta el RTC RC intern, que és el pitjor cas). I la decisió de si el sistema s'ha d'adormir sol, que demana pantalla: un caràcter que arribi mentre dorm es perd i aquesta placa no té res per dir-ho a la persona. |
| **Nota** | L'ATOM no té bateria: el consum es mesura, l'autonomia no es promet. El número de la desviació que el sistema ja diu (`power: woke on the timer, +N ms from the hour it was given`) és el que cal apuntar a `docs/75` quan es prengui a la placa. |

## M7 · El maquinari que hi ha

| | |
|---|---|
| **Què queda** | **Mesurar a la placa** els microsegons d'un frame del llum (avui són un càlcul, no una mesura), el corrent d'un llum encès i apagat amb el mesurador USB, i **quant costa un efecte**: 30 fps d'un frame de 30 µs són 0,09 % de CPU en teoria, i a la placa no s'ha mesurat. I **el sensor del Matrix**: el driver està escrit pel full del fabricant i no s'ha vist en maquinari, o sigui que el **sentit dels eixos** en aquesta placa i la qualitat de la mesura estan per comprovar (`run tilt` els ensenya a la consola). |
| **Criteri** | El que ja es compleix: el gest de tres tocs tanca l'app del davant, una app rep les premudes, i un llum que ha pres una app no li sobreviu. |

## M8 · El banc és una placa

El banc és **una** placa des del 31/10: `--board m5stack-atom|m5stack-atom-matrix|m5stack-paper`
diu quina, i sense dir res és l'ATOM (`board::models_count()`, `model(i)`, `default_model()`).
`sys.board` respon **aquell nom** i no `emulator`, que no és cap placa; `sys.emulator` continua cert i
és la pregunta que ha de fer canviar el comportament d'una app pel fet d'estar al banc. Com que
`board_name()` respon **un** nom, `boards` del manifest es compara al PC exactament com a la placa:
una app del model corre i una d'un altre queda llistada amb l'avís i es refusa dient quin és aquesta
placa. `version` i la capçalera diuen `m5stack-paper (emulator)`, les dues coses alhora.

El maquinari fals **ja surt del model** (`emu_model.cpp` és l'única llista de què porta cada placa, i
`hal_emulator.cpp` la llegeix per a les capacitats, els llums, el nombre de tecles, el sensor, la
bateria, la ranura i la FRAM). El que **encara no** segueix el model és la **forma** de les tecles
—cara, lloc i si són compostes—, que avui és sempre el dial de la Paper:

| Peça | Com està | Què hi falta |
|---|---|---|
| Capacitats | Fet: surten del model (`kCapLed` fora al Paper, `kCapImu` fora al Paper, `kCapBattery` només amb indicador i `--battery-mv`, `kCapSd` només amb ranura i `--sd`) | res, tret de `kCapDisplay` quan hi hagi qui dibuixi el panell (avui ningú) |
| Llums | Fet: el comptador és el del model (1 a l'ATOM, 25 al Matrix, **0 al Paper**) i el sostre del canal és **20 a tots**, per decisió: un banc que deixés passar 255 amagaria el límit del panell (`lib/hal_emulator/include/board/identity.h`) | decidir si el sostre ha de ser el de cada model (255 a l'ATOM) o quedar-se a 20 |
| Tecles (quantes) | Fet: `IInput::count()` és el del model (1 a l'ATOM i al Matrix, 3 al Paper) | — |
| Tecles (forma) | **Pendent**: la cara, el lloc i si és composta són sempre els de la Paper (`hal_emulator.cpp`, el dial de la cara esquerra) | una fila per model a `emu_model.cpp` i que `IInput::info()` la llegeixi |

Els perfils **viuen en una taula en temps d'execució** (`emu_model.cpp`): la identitat de les plaques
de xip és una capçalera triada per `-I` (docs/50) i l'emulador no en pot veure tres de cop. Les
plaques de xip es queden amb la capçalera i el banc té una fila per model amb els mateixos números.
**Falta la prova** que comprovi que cada fila diu el mateix que la `lib/board_<id>` de la seva placa.

I per poder prémer una tecla a mà hi ha un sol camí i portes fines. Cap de les portes no és una font
de veritat nova: totes criden el mateix `emu::press_key`.

| # | Porta | Per a què | Criteri |
|---|---|---|---|
| 1 | Prefix de terminal, `Ctrl-A` com `tmux`: `Ctrl-A 1` és un toc, `Ctrl-A 1 h` / `Ctrl-A 1 u` és prémer i deixar anar | L'ús diari i el mode lot; no depèn de res i va per SSH | Una app que llegeix una línia no veu mai aquests bytes, i una premuda es veu a `button` |
| 2 | `--control <socket>` amb línies `key 0 down`, `key 0 up`, `tap 0 120` | Guions, proves i qualsevol eina de fora (també una finestra) | Es llegeix sense bloquejar abans de `kernel::poll()` i és apagat per defecte |
| 3 | `--events <fitxer>` amb `ms key down` / `ms key up` | Reproduir una sessió amb `held_ms` de debò | El mateix fitxer dona sempre el mateix resultat |
| 4 | Finestra (SDL2), **només com a client** de les portes 1 i 2 | Veure el panell i clicar les tecles del perfil | `SDL_PollEvent` dins del pump i mai `WaitEvent`; les proves i el CI han d'anar sense pantalla |

Ordre: 1 i 2 primer, 3 si cal, i la finestra quan hi hagi `IDisplay`, que és el motiu de debò per
obrir una finestra (tres botons no el justifiquen).

Paranys que aquesta feina ha de respectar:

- **Un toc no cap en una passada.** `button::poll()` corre un cop cada `kAppIdleMs` = **20 ms**, o
  sigui que el «baix» ha de sobreviure un torn: el toc és una pujada **programada**, no immediata.
- **`--time-scale` multiplica el monòton** (`elapsed_ms()` × escala). A 1000×, un toc de 120 ms són
  120 s de `held_ms` i surt `long` de seguida, i la finestra d'**1,5 s** dels tres tocs queda en
  1,5 ms. Les portes han de dir els temps en **ms simulats** i el banc ha de fer la conversió.
- **El terminal no dona el «puja».** Sense el prefix amb `h`/`u` només es poden fer polsos, i
  `kind == "long"` no es pot provar a mà.
- **Un sol lector d'`stdin`.** Amb `--script`, `stdin` pot estar alimentant l'app: el filtre del
  prefix va dins del camí que ja encamina els bytes, no en un segon lector que hi competeixi.
- **Un toc de mentida ha de fer caure un costat de debò.** El comptador de tocs mira la baixada i la
  pujada: una porta que deixi la tecla a baix per sempre deixa l'app sense poder-se tancar.
- **La tecla del sistema no pot ser una constant escampada.** Avui és la 0 per conveni (`button.cpp`:
  només la 0 compta tocs, i tres tocs tanquen l'app). A l'ATOM és l'única i al Paper és la del mig,
  o sigui que és un fet del perfil.

Es tanca quan: una app que nomena una placa (el `matrix`, que nomena `m5stack-atom-matrix`) corre
amb `--board m5stack-atom-matrix` i la refusa `--board m5stack-atom`; el `keys` corre amb els tres
perfils; una tecla premuda des del terminal arriba a una app que està llegint una línia **sense
embrutar la línia**; el mode lot pot prémer tecles amb `printf`; i les proves existents no canvien
de forma, perquè `emu::press_key` continua essent la veritat.

**Un deute petit, dit**: `--fs` del banc **no fa res** des que el disc és `--flash` (el binari sempre
posa `flash_dir`), i `Options` només hi cau com a recanvi. O es treu, o passa a ser un altre nom de
`--flash`; mentre hi sigui, `docs/85` diu què fa de debò.

Quan es faci, `docs/30`, `docs/40`, `docs/85` i el `--help` del binari es canvien al mateix commit.

## La M5 Paper, pas a pas

L'entorn `paper` hi és i el firmware **compila**: porta la consola, el disc, la NVS, el rellotge del
SoC, les dues fondàries de son, el *main power hold*, les tres tecles, la ràdio que hereta del xip i
la targeta quan n'hi ha al slot. **Mesurat l'1/11 amb la 0.10.1**: 2 098 905 B de flash (50,0 % de 4 MB) i 93 652 B
de RAM estàtica (docs/75). **S'ha flashejat i arrenca**: el banner (`lunokOS <versió>  board
m5stack-paper`, que el 31/10 deia `0.6.0`) i el prompt hi són. El que falta té ordre perquè cada pas
necessita l'anterior:

| # | Pas | Criteri per dir-lo fet |
|---|---|---|
| 1 | **Que arrenqui**: `pio run -e paper`, flashejar i veure el prompt. El *main power hold* de GPIO2 és el primer que fa el HAL, perquè sense ell la placa s'apaga i sembla un crash | El prompt respon `help` i `caps` diu **15 de 18**, que és el sostre d'aquesta placa (no té ni llum ni sensor inercial, i l'única que falta és la pantalla) — mesurat l'1/11 amb bateria i targeta |
| 2 | **El panell**: driver propi de l'**IT8951E** (SPI: CS 15, SCK 14, MOSI 12, MISO 13, BUSY 27, amb l'alimentació a GPIO23 i GPIO5) i `ISurface` de 960×540 amb 16 nivells. **Fet de codi i vist al vidre** (1/11, 0.13.0): el handshake contesta (`panel: 960x540, image at 0x001236e0`) i l'app `panel` hi pinta el marc, l'escala de 16 nivells i cinc quadres, rectes | Una pàgina en blanc i un rectangle negre de debò, amb els modes `INIT` i `GC16` mesurats — la pàgina hi és, i falten els dos temps (docs/95, E-7) |
| 3 | **Les tecles**: fetes. `IInput` porta l'índex, la cara, el lloc, si és simple o composta i el grup, i l'app rep tot això amb `button.list()` per donar una funció a cada **posició** (docs/30, docs/50). La lectura és la del xip (`GpioKeys`) i el que és d'aquesta placa és la taula: el **damunt del dial és `GPIO37`**, perquè és el que fa que prémer la tecla de dalt digui `up` (docs/50). La llibreria del fabricant es contradiu (capçalari: `GPIO37` esquerra i `GPIO39` dreta; README: al revés) | Prémer la tecla de dalt i veure a `button` que diu `up`: si diu `down`, es canvien dues línies de la taula `kKeys` de `lib/hal_paper/` |
| 4 | **El rellotge BM8563** (I2C 0x51): **fet de codi** (31/10). El bus de la I2C del xip (`esp32/i2c.h`), el xip (`esp32/bm8563.h`: hora en BCD i el bit VL que diu si ha perdut la pila) i `RtcClock`, que llegeix el xip a l'arrencada i l'escriu a cada `set_epoch`; `time.caps().source` diu `"rtc"`. El convertidor de data a època és a `hal/civil.h` i té prova pròpia al PC. **I la seva alarma, que no va a cap GPIO, va al circuit d'alimentació**: `sleep deep` amb una hora arma `Bm8563::arm_at` i baixa GPIO2, o sigui que **la placa s'apaga de debò** i l'alarma la torna a encendre (`deep_powers_off()` és cert aquí; docs/50). **Falten dues proves de foc**: desendollar el Paper un minut i veure que l'hora hi és, i comprovar que el corrent cau i que l'alarma el torna a pujar | `time.caps().source` diu `"rtc"`, `sleep deep <hora>` torna sol a l'hora demanada (al segon fins a 4 h 15, al minut més enllà), i un son de deu minuts torna a l'hora |
| 5 | **La bateria**: **feta de codi** (31/10). El divisor de GPIO35 (×2) es llegeix amb l'ADC1 i la calibració de l'eFuse (`esp32_battery`), i el % surt de la corba de `hal/battery.h`; `kCapBattery` només s'anuncia si la lectura és un número que pot ser una cel·la. **Falta la mesura**: dos punts amb el multímetre a `BAT` i `GND` (ple i a mitges) per veure si `mv` quadra o si s'ha de corregir el factor | `power.battery()` dona mV i % que quadren amb el mesurador |
| 6 | **La ràdio**: **provada a la placa i funciona** (31/10). El codi és el de l'ATOM i el `net()` d'aquesta placa el retorna (`lib/hal_esp32/src/net.cpp`, mogut i no copiat; les capacitats venen de `esp32_caps()`) | `provision`, `ntp` i `http` funcionen com a l'ATOM |
| 7 | **La targeta microSD**: **feta** (31/10). `esp32_sd()` munta la targeta a `/sd` amb FATFS per SPI (CS GPIO4, SCK/MOSI/MISO compartits amb el panell), només si una targeta respon i amb una sola temptativa per arrencada. Dues coses que va dir la placa: el **CS del panell (GPIO15) s'ha de deseleccionar** abans de tocar el bus —arrenca seleccionat perquè és un pin de *strapping*— i la targeta va a **10 MHz**; no se sap si 20 MHz també hi van, perquè els dos canvis es van fer junts | `mounts` llista `/sd` amb números i `ls /sd` llegeix el directori |
| 8 | **El toc GT911** (0x14 o 0x5D, INT a GPIO36). **Fet de codi l'1/11**: `ITouch` al HAL, el driver de `esp32/gt911.h` (les dues adreces, la configuració de 186 bytes, el marc de cinc punts i el zero que el tanca), `touch.caps`/`read`/`gesture` a Lua amb els gestos fets pel nucli (`tap`, `long`, `drag`, `two`) i l'app `glass`. El detall del xip és a docs/50 §«El tàctil». **Falta**: comprovar al xip el marc i la velocitat (docs/95, E-6) i la **política d'energia** d'aquest pas: `GPIO23` i `GPIO5` s'encenen només mentre s'usen (docs/50). El SHT30 d'aquest pas ja no hi és: provat al xip l'1/11 (`env yes` i `air 21.1 C 63.5 %`) | Un dit diu on és, l'app `glass` ho ensenya, la capacitat s'anuncia i els gestos surten; el marc i la velocitat mesurats al xip |

**Dues peces de la placa que no surten en aquesta taula i també falten** (el deute sencer, peça per
peça, és a `docs/50` §«Què en fa el sistema, avui»):

- **Els 8 MB de PSRAM, dels quals 4 són els que es poden fer servir**: **fets de codi el 31/10**.
  `extra_memory()` a la plataforma (torna `{chip_bytes, total_bytes, free_bytes}`), `pal::IExtraMemory`
  per assignar-hi, i l'intèrpret posa a la PSRAM els blocs de 4 KB en amunt sense que l'app ho sàpiga;
  el sostre del Paper passa a ser la PSRAM **mapejada** (4 096 KB, no els 8 192 del full de dades) i
  `sys.heap()` i `mem` en diuen l'espai lliure (docs/75). Falta provar-ho al xip (docs/95).
- **Els altres 4 MB (el `himem`): decisió presa, res a fer avui.** L'ESP32 només adreça 4 MB de
  memòria externa; la meitat restant del xip només existeix a través d'un commutador de bancs de
  32 KB (`esp_himem`, `CONFIG_SPIRAM_BANKSWITCH_RESERVE`) que **mou la memòria sota els punters**, i
  per tant no pot ser transparent per a una app: ni una taula, ni una cadena, ni cap objecte de Lua
  poden viure allà. Dues sortides i cap de bona: (a) reservar bancs al `himem` **treu** memòria a la
  piscina transparent, o sigui que una app en tindria menys; (b) donar una crida de «memòria gran»
  per fer-hi córrer un fitxer o un búfer a trossos, que no és transparent i que avui no demana res.
  **No es fa fins que hi hagi un consumidor de debò** (un caché de fitxers, la imatge d'un
  desplegament, un flux de xarxa): quan n'hi hagi, la porta és `esp_himem` amb una API de còpia
  d'entrada i sortida, i serà una crida de Lua **nova i explícita**, no un canvi darrere l'app.
- **La FM24C02** (256 B de FRAM a la I2C 0x50): **feta de codi el 31/10**. El xip és el fitxer
  (`esp32/fm24c02.h`, sense còpia a RAM), es munta a `/fram` i el banc ja en tenia el model. Falta
  provar-ho al xip i mirar-s'ho amb `run hexdump /fram` (docs/95). **I una decisió oberta**: la resta
  de volums (`/littlefs`, `/sd`, `/nvs` i `/rtc`) tenen bit a `sys.caps()` i la FRAM no, o sigui que
  una app no pot preguntar-ho sense caminar pels muntatges. Si s'hi posa, és un `kCapFram` amb set
  llocs: `hal.h`, `hal_paper` (només quan el xip contesta), `emu_model.cpp`, `docs/50`, `docs/30`,
  `docs/85` i la prova que ho mira, i puja la versió **Y** perquè és un nom nou de l'API.
- **Els tres ports HY2.0** (A G25/G32, B G26/G33, C G18/G19). No hi ha **cap** manera que una app
  toqui un GPIO, i això és una crida nova de Lua (`gpio.*`) més que un driver: qui l'obre, amb quina
  política i què pot fer una app amb un pin és una decisió, i per això no es fa sense parlar-ne.
- **La rotació, i és una sola per al vidre i el panell.** Avui `touch.read()` dona les coordenades del
  **xip** (540 × 960 en retrat, el marc de la seva configuració) i no hi ha res girat: és honest i és
  incomplet. Quan hi hagi el panell, **el dibuix i el toc han de viure en el mateix marc** o una app
  que pinta un botó i el comprova amb un dit es passa el dia girant números. El que s'ha de fer, i
  tot en un sol lloc:
  - **una rotació compartida** (0, 90, 180 o 270 °) que el sistema aplica **a les dues coses alhora**,
    amb la del panell com a mestra: `display.rotation(n)` quan la pantalla existeixi, i el toc la
    llegeix d'allà i no té opinió pròpia;
  - **la conversió dels punts**, feta **una vegada** en el camí del tàctil (al HAL o al nucli, mai a
    l'app): de (x, y) del xip a (x, y) del panell, i els límits de `touch.caps()` també girats — un
    `width`/`height` que no gira amb les coordenades és el pitjor dels dos móns;
  - **els esdeveniments també giren**: el `dx`/`dy` d'un `drag` i el `x`/`y` d'un `tap` volen dir una
    altra cosa si la placa es té girada, i qui els fa —`lib/kernel/src/touch.cpp`— és qui els ha de
    girar, perquè és l'únic lloc on hi ha el flux sencer i on es pot provar amb un fals;
  - **la Paper no pot saber com la tens**: no té acceleròmetre (`imu` no s'anuncia), i per tant no hi
    ha orientació automàtica possible. És una **decisió** —fixa o un ajust que es desa— i no una
    mesura.

  **Abans de decidir-la cal la mesura d'E-6** (tocar les quatre cantonades): diu si la cantonada de dalt
  a l'esquerra és `(0,0)` o `(539,0)`, i d'aquí surt quina de les quatre rotacions és la bona. Suposar-ho
  és exactament el que no s'ha de fer.


**El deure d'estalviar bateria, dit com a feina pendent** (docs/50, docs/60):
1. **Mesurar quant deriva el son.** Al Paper ara el compta el **quars** del xip (`sleep deep` apaga la
   placa i el compte enrere la torna a encendre), i a les plaques sense xip el temporitzador del SoC,
   que compta amb l'RC intern (`CONFIG_ESP32_RTC_CLK_SRC_INT_RC=y`). **El mecanisme ja hi és**: el
   nucli desa a la NVS quant s'ha desviat l'últim despert i `power last` ho ensenya, amb `(clock)` o
   `(timer)` al costat. I el número ja és **del rellotge i no del firmware**: al Paper, un son de 20 s
   sortia `+1,2 s` perquè hi havia el segon d'arrencada a dins, i `hal::wake_error_ms` el treu
   (31/10). Falta la dormida de deu minuts **amb bateria**: `run alarmset` → desendollar → `power
   last` en tornar, i el número a `docs/60` (`docs/95`, A-2).
2. **Mesurar la política d'oci a la placa**: el nucli ja s'apaga sol fins a la propera alarma quan la
   placa té xip de rellotge (`kernel::power`, docs/60) i `power idle` ho ensenya i ho pot desactivar.
   Falta veure-la fer: una alarma a deu minuts, res més, i la placa apagada fins aleshores.
3. **Ensenyar a les apps qui les ha despertat**: `power last` ja diu `(clock)` o `(timer)` per consola
   i el xip deixa el bit `AF` (`RtcClock::woke_by_alarm()`), però la taula `power` de Lua no ho té.
4. **Apagar el que es pugui quan el sistema dorm**: el `PON`/`POFF` del panell, el repòs del GT911 i
   del SHT30 quan arribin, i dir a `power.caps().wake` qui pot despertar de debò.
5. **Decidir qui mana quan una app dorm profundament i hi ha una alarma abans**: avui el son profund
   guanya i l'alarma s'entrega tard (`late_ms` ho diu); un son lleuger, en canvi, ja es talla a
   l'alarma. Per fer-ho bé al son profund cal decidir si el sistema ha de despertar la placa per
   entregar l'alarma i **tornar-la a dormir** després, o si la data de l'app és la que val.

**Una cosa que s'havia vist a la placa (31/10, abans de donar la ràdio per bona)**: el provisioning
**per Bluetooth** arribava a `CRED_SUCCESS`, l'aparell rebia adreça… i en parar el gestor peta:
`wifi_prov_mgr: Failed to create prov_stop_task!` i `abort()`. La pila d'una tasca només pot venir de
la **DRAM interna** i no de la PSRAM, o sigui que el que faltava era memòria interna en aquell instant
(`docs/75`). L'`INet` del Paper s'ha provat després i **funciona**; si el `abort()` torna a sortir en
tancar el provisioning per Bluetooth, el camí és aquest i el número de DRAM encara no hi és.

## Fora de la v1

| Cosa | Per què queda fora |
|---|---|
| Pantalla (`IDisplay`) | No hi ha maquinari a la placa de proves; la interfície ja hi és |
| Targeta SD a l'ATOM | No hi ha maquinari (al Paper hi és, i és el pas 7) |
| Instal·lació d'apps des del prompt o per xarxa | Primer ha d'existir el disc, i el disc hi és: això és el següent de debò |
| Actualització del firmware (OTA) | La taula de particions ja hi deixa el forat, però les dues ranures es van fusionar per fer-hi cabre el Bluetooth |
| Àudio | Ni maquinari ni necessitat |
| Múltiples apps simultànies | És un no-objectiu, no una fita pendent |

## Com es tanca una fita

1. El criteri passa a l'emulador, amb un guió de proves.
2. El criteri passa a la placa, o es diu exactament quina part no s'ha pogut provar i per què.
3. Els números mesurats s'escriuen a `75`.
4. Si una decisió ha canviat, es canvia el document de la matèria **abans** de tancar la fita.
