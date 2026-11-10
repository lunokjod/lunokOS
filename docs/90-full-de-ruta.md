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
són les mesures de camp d'M5, M6 i M7, **M8** (la forma de les tecles a l'emulador; la finestra ja les prem
amb el ratolí, i falten les portes del terminal i dels guions),
dues
crides que ningú no ha demanat encara (`net.softap` i escriptura a `/nvs`), la resta del contracte
de Lua que no té codi al darrere (`docs/30`, taula d'⏳) i les decisions que necessiten maquinari o
una persona al davant.

## El contracte de Lua que encara no té codi

Una app que cridi qualsevol d'aquestes peta amb «attempt to call a nil value», i cada una té el seu
perquè:

| Crida | Per què no hi és | On va |
|---|---|---|
| `console.flush`, `console.read`, `console.on_data` | cap app no les ha demanades; `read_line` i `read_byte` cobreixen el teclat (`read_byte` ja hi és) | M1 |
| `time.format`, `parse`, `set` | el rellotge dona l'hora i les parts del dia; formatar-la no l'ha demanada cap app (`utc`, `date` i `tz` hi són des de la 0.125.0) | M3 |
| `time.sync` | ja hi ha `net.ntp`, que és el que fa la feina | M5 |
| `Promise.all`, `Promise.race` | cap app ha necessitat esperar dues coses alhora | M1 |
| `app.spawn`, `app.raise` | una app té un sol fil i una finestra d'alarma ja passa a primer pla amb `raise` de l'alarma | M2 |
| `app.save`, `app.restore` | una app desa on sobreviu, al disc (`fs.data`), i ha estat suficient | M2 |
| `store` | és un fitxer petit per app, i `fs` ja el pot fer | M4 |
| `log` | una app escriu a la consola, i l'anell de log del sistema ja existeix per a ell | M1 |
| `net.softap` | el HAL sap aixecar un AP pel provisioning; falta la crida, i cap app l'ha demanada | M5 |

La llista no és una promesa: **una crida d'aquestes s'afegeix quan una app la demana**, amb la
política a dins del sistema i la seva prova, com qualsevol altra (docs/10). **I tres files que hi eren ja no hi
són**, perquè la 0.171.0 va tancar-les el 9/11: `sys.reboot`, `fs.format(<volum>)` i el buidatge de l'NVS, que és
la mateixa porta que el format d'un volum (`fs.format('/nvs')`, docs/30, docs/70).

## M0 · Les dues bandes arrenquen i parlen

Feta. El que queda és el que queda de tota la llista: res d'específic.

## M1 · Consola i promeses

| | |
|---|---|
| **Què queda** | Les crides de consola de la taula de dalt, `Promise.all`/`race` i el mòdul `log`. |
| **Criteri per tancar-les** | Cada una amb la seva prova: una app que llegeix bytes sense perdre el teclat, un reinici que diu per què, i dues promeses que es compleixen alhora. |

## M2 · Apps i manifest

| | |
|---|---|
| **Què queda** | `app.spawn`/`raise`/`save`/`restore`, si una app els demana. El selector d'extensió ja pregunta sempre i `open <app> <path>` en imposa una. |
| **Es mesura, i no està mesurat** | El temps d'escanejar les apps d'un disc ple, i el pic de memòria de llegir un manifest. Avui no hi ha número. |

## M3 · Temps i alarmes

| | |
|---|---|
| **Què queda** | `time.format`/`parse`/`set`/`sync`, si una app els demana —`time.utc`, `time.date` i `time.tz` ja hi són des de la 0.125.0—. Les alarmes persisteixen, es reprodueixen amb `late_ms` i compten les repeticions perdudes. |
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
| **Què queda** | **Mesurar a la placa** els microsegons d'un frame del llum (avui són un càlcul, no una mesura), el corrent d'un llum encès i apagat amb el mesurador USB, i **quant costa un efecte**: 30 fps d'un frame de 30 µs són 0,09 % de CPU en teoria, i a la placa no s'ha mesurat. I **el sensor del Matrix**: el driver està escrit pel full del fabricant i no s'ha vist en maquinari, o sigui que el **sentit dels eixos** en aquesta placa i la qualitat de la mesura estan per comprovar (`imu.read()` en una pàgina els ensenya a la consola). |
| **Criteri** | El que ja es compleix: el gest de tres tocs tanca l'app del davant, una app rep les premudes, i un llum que ha pres una app no li sobreviu. |

## M8 · L'emulador és una placa

L'emulador és **una** placa des del 31/10: `--board m5stack-atom|m5stack-atom-matrix|m5stack-paper|ttgo-t-koala`
diu quina i **és obligatori**; sense l'opció el binari es refusa i en llista les quatre (el `default_model()`
de la identitat només és la resposta d'un emulador construït a mà). `sys.board` respon **aquell nom** i no
`emulator`, que no és cap placa; `sys.emulator` continua cert i
és la pregunta que ha de fer canviar el comportament d'una app pel fet d'estar a l'emulador. Com que
`board_name()` respon **un** nom, `boards` del manifest es compara al PC exactament com a la placa:
una app del model corre i una d'un altre queda llistada amb l'avís i es refusa dient quin és aquesta
placa. `version` i la capçalera diuen `m5stack-paper (emulator)`, les dues coses alhora.

El maquinari fals **ja surt del model** (`emu_model.cpp` és l'única llista de què porta cada placa, i
`hal_emulator.cpp` la llegeix per a les capacitats, els llums, el nombre de tecles, el sensor, la
bateria, la ranura i la FRAM). El que **encara no** segueix el model és la **forma** de les tecles
—cara, lloc i si són compostes—, que avui és sempre el dial de la Paper:

| Peça | Com està | Què hi falta |
|---|---|---|
| Capacitats | Fet: surten del model (`kCapLed` fora al Paper, `kCapImu` fora al Paper, `kCapBattery` només amb indicador i una cel·la —`--battery-mv` o la prestada—, `kCapSd` només amb ranura i una targeta que existeixi, `kCapDisplay` amb el vidre del model, que `--no-display` treu) | res |
| Llums | Fet: el comptador és el del model (1 a l'ATOM, 25 al Matrix, **0 al Paper**) i el sostre del canal és **20 a tots**, per decisió: un emulador que deixés passar 255 amagaria el límit del panell (`lib/hal_emulator/include/board/identity.h`) | decidir si el sostre ha de ser el de cada model (255 a l'ATOM) o quedar-se a 20 |
| Tecles (quantes) | Fet: `IInput::count()` és el del model (1 a l'ATOM i al Matrix, 3 al Paper) | — |
| Tecles (forma) | **Pendent**: la cara, el lloc i si és composta són sempre els de la Paper (`hal_emulator.cpp`, el dial de la cara dreta) | una fila per model a `emu_model.cpp` i que `IInput::info()` la llegeixi |

**La memòria i el rellotge també són del model** (3/11, 0.58.0): `Model::free_heap_bytes` i
`Model::extra_free_bytes` són els dos casals del xip —**338 864 B** de RAM interna als ATOM,
**162 728 B** i **4 095 KB** de PSRAM al Paper, els números que la placa dona al prompt— i
`pal::set_memory_bytes()` els dona al PAL del PC, que compta cada bloc amb els seus setze bytes de
capçalera, prova l'altre casal abans de dir no i **refusa** el que no hi cap: una taula de 400 000
enters mor a l'emulador amb el mateix `not enough memory` que a la placa, i quan la taula es deixa anar la
memòria torna al byte (docs/75). I el **torn** del nucli paga els 850 µs que costa a la placa, o sigui que mil
esperes d'un mil·lisegon triguen 1 888 ms aquí i 1 990 ms al vidre. El que **no** és del model: la
fragmentació del heap del PC, el desgast dels blocs i la instrucció del Xtensa, que no hi és i es
cobra a part (`--lua-cpu`, docs/85).

**I el que la placa recorda, l'emulador també** (3/11, 0.59.0): les **credencials de la xarxa** van a la
NVS (`lunoknet/ssid` i `wifidrv/pass`) com les desa la placa, o sigui que un run que arrenca
després diu el mateix `network:`; l'**hora** viu a `<state>/clock.bin` amb el temps que la placa ha
estat parada, i la llegeixen el Paper (pila de la BM8563) i qualsevol model amb `--persist`; i
`--persist` és **la placa que no s'ha desendollat**, que conserva la RTC RAM també a l'ATOM. La NVS i
la FRAM no depenen de res: són flash i ferrita (docs/60, docs/70, docs/85).

Els perfils **viuen en una taula en temps d'execució** (`emu_model.cpp`): la identitat de les plaques
de xip és una capçalera triada per `-I` (docs/50) i l'emulador no en pot veure tres de cop. Les
plaques de xip es queden amb la capçalera i l'emulador té una fila per model amb els mateixos números.
**Falta la prova** que comprovi que cada fila diu el mateix que la `lib/board_<id>` de la seva placa.

I per poder prémer una tecla a mà hi ha un sol camí i portes fines. Cap de les portes no és una font
de veritat nova: totes criden el mateix `emu::press_key`.

| # | Porta | Per a què | Criteri |
|---|---|---|---|
| 1 | Prefix de terminal, `Ctrl-A` com `tmux`: `Ctrl-A 1` és un toc, `Ctrl-A 1 h` / `Ctrl-A 1 u` és prémer i deixar anar | L'ús diari i el mode lot; no depèn de res i va per SSH | Una app que llegeix una línia no veu mai aquests bytes, i una premuda es veu a `button` |
| 2 | `--control <socket>` amb línies `key 0 down`, `key 0 up`, `tap 0 120` | Guions, proves i qualsevol eina de fora (també una finestra) | Es llegeix sense bloquejar abans de `kernel::poll()` i és apagat per defecte |
| 3 | `--events <fitxer>` amb `ms key down` / `ms key up` | Reproduir una sessió amb `held_ms` de debò | El mateix fitxer dona sempre el mateix resultat |
| 4 | Finestra (`--gui`), **ja feta** (0.33.0–0.34.2) | **Prémer les tecles del model a la finestra**: el ratolí sobre la tira de tecles crida `emu::press_key`, i sobre la pàgina és el dit del GT911 (docs/85). Les tecles segueixen el descriptor de l'entrada (una columna a la cara que digui) | Fet: un clic prem i deixa anar la tecla, i amb `--verbose` deixa la línia `gui: key … down`/`up`; la finestra d'un ATOM, que no té vidre, és només el seu botó |

Les portes 1, 2 i 3 falten; la finestra ja hi és i ja prem tecles. L'ordre que queda és 1 i 2 primer,
3 si cal — i el que encara no té la finestra no és la pàgina ni els dits, sinó **la forma de les tecles
del model** (la fila «Tecles (forma)» d'aquesta taula).

Paranys que aquesta feina ha de respectar:

- **Un toc no cap en una passada.** `button::poll()` corre un cop cada `kAppIdleMs` = **20 ms**, o
  sigui que el «baix» ha de sobreviure un torn: el toc és una pujada **programada**, no immediata.
- **`--time-scale` multiplica el monòton** (`elapsed_ms()` × escala). A 1000×, un toc de 120 ms són
  120 s de `held_ms` i surt `long` de seguida, i la finestra d'**1,5 s** dels tres tocs queda en
  1,5 ms. Les portes han de dir els temps en **ms simulats** i l'emulador ha de fer la conversió.
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

**Un deute petit, dit**: `--fs` de l'emulador **no fa res** des que el disc és `--flash` (el binari sempre
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
| 1 | **Que arrenqui**: `pio run -e m5-paper`, flashejar i veure el prompt. El *main power hold* de GPIO2 és el primer que fa el HAL, perquè sense ell la placa s'apaga i sembla un crash | El prompt respon `help` i `caps` diu **15 de 18**, que és el sostre d'aquesta placa (no té ni llum ni sensor inercial, i l'única que falta és la pantalla) — mesurat l'1/11 amb bateria i targeta |
| 2 | **El panell**: driver propi de l'**IT8951E** (SPI: CS 15, SCK 14, MOSI 12, MISO 13, BUSY 27, amb l'alimentació a GPIO23 i GPIO5) i `ISurface` de 960×540 amb 16 nivells. **Fet de codi i vist al vidre** (1/11, 0.13.0): el handshake contesta (`panel: 960x540, image at 0x001236e0`) i una pàgina de prova hi pinta el marc, l'escala de 16 nivells i cinc quadres, rectes | Una pàgina en blanc i un rectangle negre de debò, amb els modes `INIT` i `GC16` mesurats — la pàgina hi és, i falten els dos temps (docs/95, E-7) |
| 3 | **Les tecles**: fetes. `IInput` porta l'índex, la cara, el lloc, si és simple o composta i el grup, i l'app rep tot això amb `button.list()` per donar una funció a cada **posició** (docs/30, docs/50). La lectura és la del xip (`GpioKeys`) i el que és d'aquesta placa és la taula: el **damunt del dial és `GPIO37`**, perquè és el que fa que prémer la tecla de dalt digui `up` (docs/50). La llibreria del fabricant es contradiu (capçalari: `GPIO37` esquerra i `GPIO39` dreta; README: al revés) | Prémer la tecla de dalt i veure a `button` que diu `up`: si diu `down`, es canvien dues línies de la taula `kKeys` de `lib/hal_paper/` |
| 4 | **El rellotge BM8563** (I2C 0x51): **fet de codi** (31/10). El bus de la I2C del xip (`esp32/i2c.h`), el xip (`esp32/bm8563.h`: hora en BCD i el bit VL que diu si ha perdut la pila) i `RtcClock`, que llegeix el xip a l'arrencada i l'escriu a cada `set_epoch`; `time.caps().source` diu `"rtc"`. El convertidor de data a època és a `hal/civil.h` i té prova pròpia al PC. **I la seva alarma, que no va a cap GPIO, va al circuit d'alimentació**: `sleep deep` amb una hora arma `Bm8563::arm_at` i baixa GPIO2, o sigui que **la placa s'apaga de debò** i l'alarma la torna a encendre (`deep_powers_off()` és cert aquí; docs/50). **Falten dues proves de foc**: desendollar el Paper un minut i veure que l'hora hi és, i comprovar que el corrent cau i que l'alarma el torna a pujar | `time.caps().source` diu `"rtc"`, `sleep deep <hora>` torna sol a l'hora demanada (al segon fins a 4 h 15, al minut més enllà), i un son de deu minuts torna a l'hora |
| 5 | **La bateria**: **feta de codi** (31/10). El divisor de GPIO35 (×2) es llegeix amb l'ADC1 i la calibració de l'eFuse (`esp32_battery`), i el % surt de la corba de `hal/battery.h`; `kCapBattery` només s'anuncia si la lectura és un número que pot ser una cel·la. **Falta la mesura**: dos punts amb el multímetre a `BAT` i `GND` (ple i a mitges) per veure si `mv` quadra o si s'ha de corregir el factor | `power.battery()` dona mV i % que quadren amb el mesurador |
| 6 | **La ràdio**: **provada a la placa i funciona** (31/10). El codi és el de l'ATOM i el `net()` d'aquesta placa el retorna (`lib/hal_esp32/src/net.cpp`, mogut i no copiat; les capacitats venen de `esp32_caps()`) | `provision`, `ntp` i `http` funcionen com a l'ATOM |
| 7 | **La targeta microSD**: **feta** (31/10). `esp32_sd()` munta la targeta a `/sd` amb FATFS per SPI (CS GPIO4, SCK/MOSI/MISO compartits amb el panell), només si una targeta respon i amb una sola temptativa per arrencada. Dues coses que va dir la placa: el **CS del panell (GPIO15) s'ha de deseleccionar** abans de tocar el bus —arrenca seleccionat perquè és un pin de *strapping*— i la targeta va a **10 MHz**; no se sap si 20 MHz també hi van, perquè els dos canvis es van fer junts | `mounts` llista `/sd` amb números i `ls /sd` llegeix el directori |
| 8 | **El toc GT911** (0x14 o 0x5D, INT a GPIO36). **Fet de codi l'1/11**: `ITouch` al HAL, el driver de `esp32/gt911.h` (les dues adreces, la configuració de 186 bytes, el marc de cinc punts i el zero que el tanca), `touch.caps`/`read`/`gesture` a Lua amb els gestos fets pel nucli (`tap`, `long`, `drag`, `two`) i una app que llegeixi el panell. El detall del xip és a docs/50 §«El tàctil». El marc està mesurat (quatre tocs, un a cada cantonada de la pàgina, 1/11): el xip compta en el marc de la pàgina, del dret i sense mirall. **Falta**: la velocitat de mostra (docs/95, E-6) i la **política d'energia** d'aquest pas: `GPIO23` i `GPIO5` s'encenen només mentre s'usen (docs/50). El SHT30 d'aquest pas ja no hi és: provat al xip l'1/11 (`env yes` i `air 21.1 C 63.5 %`) | Un dit diu on és, una app que llegeixi el panell ho ensenya, la capacitat s'anuncia i els gestos surten; la velocitat mesurada al xip |

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
  (`esp32/fm24c02.h`, sense còpia a RAM), es munta a `/fram` i l'emulador ja en tenia el model. Falta
  provar-ho al xip i mirar-s'ho amb `hexdump /fram` (docs/95). **I una decisió oberta**: la resta
  de volums (`/littlefs`, `/sd`, `/nvs` i `/rtc`) tenen bit a `sys.caps()` i la FRAM no, o sigui que
  una app no pot preguntar-ho sense caminar pels muntatges. Si s'hi posa, és un `kCapFram` amb set
  llocs: `hal.h`, `hal_paper` (només quan el xip contesta), `emu_model.cpp`, `docs/50`, `docs/30`,
  `docs/85` i la prova que ho mira, i puja la versió **Y** perquè és un nom nou de l'API.
- **Els tres ports HY2.0** (A G25/G32, B G26/G33, C G18/G19). No hi ha **cap** manera que una app
  toqui un GPIO, i això és una crida nova de Lua (`gpio.*`) més que un driver: qui l'obre, amb quina
  política i què pot fer una app amb un pin és una decisió, i per això no es fa sense parlar-ne.
- **La rotació, i és una sola per al vidre, la pàgina i els dits.** **Feta** (0.17.0, 0.18.0 i 0.18.1), i
  el detall viu a `docs/50`: `display.rotation()` és l'únic valor —**270**, vertical, de fàbrica—, el
  vidre es gira tot en software, `touch.caps()`/`read()`/`gesture()` donen la vista de l'app i la conversió
  es fa **una vegada** a `turn_frame()`. El marc del xip i el de la pàgina són el mateix i del dret:
  quatre tocs, un a cada cantonada, ho van dir l'1/11 (docs/95, E-6). **El que queda és una decisió i no
  una mesura**: la Paper no té acceleròmetre (`imu` no s'anuncia), o sigui que no pot saber com la tens;
  avui és fixa en vertical, i si ha de ser un ajust que es desa, és una feina d'ajustos (docs/30).

  **El forat ja hi és** (0.14.0): una app dibuixa a la seva vista i `ui.resize(w, h)` fa un canvas nou
  —blanc— i crida `on_resize(width, height)` a l'app, que és qui sap què hi havia d'anar (docs/30). El
  que falta és girar el vidre mateix i el tàctil amb la mateixa ordre, i passar-hi la rotació com a
  paràmetre en comptes de cridar-lo a mà.


**El deure d'estalviar bateria, dit com a feina pendent** (docs/50, docs/60):
1. **Mesurar quant deriva el son.** Al Paper ara el compta el **quars** del xip (`sleep deep` apaga la
   placa i el compte enrere la torna a encendre), i a les plaques sense xip el temporitzador del SoC,
   que compta amb l'RC intern (`CONFIG_ESP32_RTC_CLK_SRC_INT_RC=y`). **El mecanisme ja hi és**: el
   nucli desa a la NVS quant s'ha desviat l'últim despert i `power last` ho ensenya, amb `(clock)` o
   `(timer)` al costat. I el número ja és **del rellotge i no del firmware**: al Paper, un son de 20 s
   sortia `+1,2 s` perquè hi havia el segon d'arrencada a dins, i `hal::wake_error_ms` el treu
   (31/10). Falta la dormida de deu minuts **amb bateria**: una alarma persistent, desendollar i
   `power last` en tornar, i el número a `docs/60` (`docs/95`, A-2).
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

## M9 · La capa de controls (disseny en curs, 1/11)

**UICanvas és el control bàsic: un buffer**, i tota imatge és un UICanvas. Els controls es posen uns
dins dels altres i es dibuixen **en cascada per capes** fins que la interfície sencera queda composta, i
el que arriba al vidre és el resultat. **UILabel és un control sense vista amb un text a dins**, i per això
el text **es talla a la vora del control** en lloc de trepitjar el que té al costat: no hi ha aritmètica de
marges a fora, hi ha una vora que no es pot passar.

- **Els events i els gestos es registren dins de la vista d'un UICanvas**, i cada control que en registra
  un ha de rebre la informació **adaptada al seu espai de coordenades local**: el punt, el `dx`/`dy` i el
  començament del gest, restant el desplaçament de cada capa que té a sobre. És la mateixa conversió que
  avui fa `turn_frame()` una sola vegada, estesa a una cadena; i el gest és d'un sol control —el que
  tenia el dit a sobre quan va començar— o el mateix gest el rebrien tots els de la pila.
- **Falta per parlar i decidir**: les funcions per carregar una imatge dins d'un UICanvas, i tres coses
  que el model obliga a triar (els tipus de lletra d'un UILabel ja no són una pregunta: són una cara, i es
  carreguen amb `ui.load_font` i es posen amb `cv:font`):
  - **què vol dir compondre un control sobre un altre**: una còpia de bytes (el fill és opac i porta el
    seu fons) o només la tinta (el fill guarda una màscara a part). Amb quatre bits i setze nivells, un
    «nivell transparent» no hi cap sense prohibir el blanc sobre negre;
  - **la memòria**: un canvas de pàgina sencera són 251 640 B de PSRAM al Paper (mesurat: la línia
    d'arrencada de la 0.18.x), o sigui que un arbre de controls no pot ser de capes de pàgina sencera;
    cada control ha de ser de la seva mida;
  - **el preu de compondre**: cada capa és una passada de píxels, i la tinta no és gratis (docs/50).

**Què hi ha, l'1/11 (0.19.0)**: el primer tros ja és codi i està provat a l'emulador — **el canvas com a
objecte** (`ui.canvas(w, h)` a Lua, `ui::create` al nucli): un buffer de quatre bits amb el seu lloc, un
arbre de canvas dins de la vista, la composició en cascada a cada `ui.present` (l'ordre d'aparició és
l'ordre de les capes), el retall a la vora de cada canvas, i **el que costa és el que ha canviat**: el pare
només copia el que el fill ha pintat des de l'última composició, o sigui que no hi ha màscares ni alfes i
un canvas sense res pintat no costa res. Són vuit canvas alhora amb la vista inclosa, i una vista feta de
nou (una rotació, un `ui.resize`) se'ls emporta: l'objecte que l'app encara té respon `E_INVAL` en lloc
d'escriure al canvas que hagi ocupat el seu lloc. Una pàgina de dos canvas i un botó és la prova que es veu.

**I a la placa diu el mateix, número per número** (1/11, 0.19.0): la pàgina de dos canvas i un botó a la
Paper dona les sis línies idèntiques a les de l'emulador amb `--display` —els comptes `1 / 1 / 2`, el pare al
slot 3, el marc sense vista i `canvas (gone)`—, i la línia de l'arrencada diu `ui: view 540x932 at 0,28, 251640 bytes
of canvas` (docs/95, E-8).

**I les primitives, que són del canvas**: `rect` i `frame`, `line`, `circle` i `disc`, i `text` amb la
font del sistema, la mida i el color triats —les mateixes a la vista de l'app i a qualsevol canvas, i cada
una amb **una sola regió de dany** en lloc d'una per píxel—. El text d'un canvas **es talla a la seva
vora** perquè el camí dels glifs (`font::runs`) ja porta la caixa de retall: és el que farà el `UILabel`
sense res de nou.

**I l'arbre de controls**: un canvas pot tenir **fills** (un sol pare, en l'ordre en què s'afegeixen, i
aquell ordre és el de les capes) i **dibuixar-se vol dir preguntar als fills** —`cv:draw()` crida el seu
`on_draw(self)` i després el `draw()` de cada fill—, amb el `on_draw` i el `children` guardats al costat de
l'objecte, en un camp que l'app pot escriure.

**I el redibuix, que és el que fa que això sigui un sistema i no un dibuix:** cada canvas sap si té alguna
cosa a refer (`needs_draw`), neix amb el senyal posat i el perd quan s'ha dibuixat; el senyal es posa quan
el canvas **es mou**, quan **canvia de pare**, quan se li assigna un **`on_draw`** nou o quan l'app diu
`cv:invalidate()`, i **puja als pares** perquè un fill que s'ha mogut deixa els seus píxels dins del buffer
del pare. `ui.present()` dibuixa el que ha canviat i prou: **el forat de moure un control i deixar el rastre
enrere queda tancat**, perquè el pare es torna a dibuixar sencer (el seu `on_draw` pinta el seu canvas de
bon principi) i el fill es compon al lloc nou.

**I el redibuix és barat per construcció**: mentre un control es redibuixa perquè ha canviat, el sistema
obre una **caixa** —el rectangle que deu— i **totes les primitives d'aquell canvas es tallen a dins**; la
qualitat de cada tros és la que demana el dibuix, i viatja fins al vidre. El senyal no puja als pares: el
pare només repinta quan un fill **deixa un forat** (es mou, o desapareix), i llavors repinta exactament el
rectangle que el fill ocupava. Un control que canvia el seu contingut i un control que es mou són dos
casos diferents i costen coses diferents. I **el que s'ha pintat queda pagat**: la caixa de la vista es
gasta quan el `present` pinta el que hi havia, perquè una caixa que sobrevisqui al seu pintat torna a
entrar dins del motiu següent i repinta un tros de pàgina que ja era net (el va trobar la suite, 1/11: el
rectangle que deixava un `place` tornava dins del rectangle d'un `release`).

**El que queda d'aquest pas, i està deixat per a més endavant a propòsit** (1/11, decidit amb l'amo):

| Què | Per què s'ajorna | Què voldrà dir fer-ho |
|---|---|---|
| **Carregar una imatge dins d'un canvas** | No hi ha cap app que en demani, i una imatge obre les preguntes de sempre: quin format, quanta memòria i qui la té | un objecte `image` (4 bits per píxel, la mateixa empaquetadura que el canvas) i `cv:image(x, y, img)`, que és una còpia de bytes amb retall. La composició ja està feta |

**I el `UILabel` ja hi és** (2/11, 0.44.0): `ui.label(w, h)` és un control sense buffer amb `text`, `color`,
`background`, `align`, `valign` i `scale`, la cara de `cv:font`, el retall amb `...` quan el text no cap i el
redibuix només quan un d'aquests camps canvia de valor (docs/30).

**I el `UIButton` ja hi és** (2/11, 0.45.0): és el primer control que és un **contenidor amb un marc**:
`ui.button(w, h)` té `background`, `border` i `pressed`, i el fill es posa amb `button:add(child)` i es
col·loca en les coordenades del botó. El fill es compon després del marc i es talla al mateix rectangle;
com que el marc és opac, redibuixar el botó invalida els fills perquè el fons nou no els tapi. Un botó
pot portar vuit labels de la mateixa cara d'outlines —Lato Black— a tres tamanys dins de vuit botons blancs
(docs/30).

**I el `UICheckbox` ja hi és** (4/11, 0.67.0): `ui.checkbox(w, h)` és una **fila** amb una caixa quadrada a
l'esquerra i el lloc de les paraules a la dreta, que són un `UILabel` fill colocat amb `cb:box()`. És el primer
control **el clic del qual vol dir una cosa i la fa el sistema**: `checked` es gira abans que l'app se
n'assabenti (`on_change(self, checked)`), i per això no crida `on_activate`. La caixa té el pes del botó a un
quart de mida, el tick és dues pinzellades d'un color propi (`mark`) i l'anell del focus va **fora** de la
caixa, que és l'únic lloc on hi cap. Una pàgina de cinc files amb una que el dial no toca i una de fosca és el
que se'n pot fer (docs/30, docs/90).

**I el marc del botó té pes, i el present d'una pàgina torna a ser el que l'app demana** (3/11, 0.56.2): la
vora és de **tres píxels** en un botó més gran de 24 (dos en un de petit, un en un xip), el relleu és un
píxel més prim que la vora (d'un píxel en un xip) i el forat de premut té tantes anelles com la vora. I
`ui.present('gc16')` **era un present sense mode**: el canvas de l'arrel que el
dibuix deixava a la pila tapava l'argument de l'app i el `lua_pop` se l'emportava, o sigui que cada present
pintava amb el `du` que demanen els controls i el fantasma d'un text que s'havia mogut es quedava al vidre.
La pila es torna a l'altura que tenia abans de l'arrel, i el `gc16` de l'app mana (docs/30, docs/50).

**I el focus és el doble marc més fort de la pàgina, i la pàgina és blanca i rodona** (3/11, 0.56.2): després
de la vora, els dos píxels del relleu fan de **pausa** i aleshores ve una **banda de set píxels** del color que
no es pot confondre, amb el cos a dins —un anell que només dobla la vora es llegeix com una vora més gruixuda—.
Com que el canvi de focus és un canvi d'estat, l'app el **renta amb `gc16`** al torn següent; un `du` hi deixa
el rastre de l'anell anterior. La pàgina dels botons passa a **vuit botons blancs** i a **Lato Black** (19 340 B,
llicència OFL), una rodona de debò: el seu `g` té noranta-un punts i no capia al bloc d'arestes amb sis trossos
per corba, i per això el lector ara **torna a tallar les corbes amb quatre i després tres trossos** abans de
refusar un glif (docs/30, docs/50, docs/75).

**I el botó ja rep la persona** (2/11, 0.49.0): el sistema dona el focus al primer control enfocable,
`amunt`/`esquerra` i `avall`/`dreta` recorren els controls polsables, `centre` activa el que té el focus, i
el tàctil fa el mateix amb el dit a sobre. `focused`, `focusable`, `on_focus`, `on_click` i `on_activate`
són el contracte; el `pressed` el porta el sistema mentre dura el contacte, i qualsevol control amb
`focusable = true` entra al mateix recorregut encara que no sigui un `ui.button` (docs/30).

**I el teclat segueix aquest focus** (0.91.0): una caixa de text que pren tecles (`box.keyboard`) obre la banda i
en rep les tecles; un botó o la pàgina buida la tanquen; les caixes són objectiu del dit i no del dial (docs/30).

**I el cursor és un bloc de la mida de la lletra que parpelleja sol** (0.92.0): el sistema el gira cada 600 ms i
marca la caixa perquè es torni a pintar; l'app no hi ha de portar cap rellotge. El bloc és l'amplada del caràcter
que hi ha sota el cursor i, al final del text, la d'una `M`; una caixa buida també el té, i el cursor no surt mai
del marc de la caixa (docs/30).

**I el teclat és una classe amb un pare comú** (0.93.0): `Keyboard` té la banda, el dit, el dial, el bloc triat, la
cara i la geometria, i els fills només diuen el seu id i les seves files. `QwertyKeyboard` és el teclat de sempre i
`AzertyKeyboard` el segon, amb la `a` i la `q` al cap de les files i `MAJ` a la tecla de majúscules; un teclat nou és
un fill més i una fila a la llista. L'estat és de l'objecte i no del fitxer, i triar-ne un és canviar l'objecte amb qui
parlen les funcions lliures: si la banda és a la vista, baixa i torna a pujar amb el mateix alfabet. **La tria és un
ajust del sistema** i no d'una app: `cfg keyboard` en dona la llista, `cfg keyboard <id>` la desa a l'NVS
(`cfg/keyboard`) i l'arrencada la llegeix, amb `qwerty` de reserva i una línia si el nom no existeix (docs/30, docs/70).

**I la LilyGO T-Koala és una placa més** (0.94.0): l'ESP32-WROVER-B de 4 MB de flash i 8 MB de PSRAM
—4 mapejats— entra com a entorn `tkoala`, amb una definició de placa pròpia perquè la plataforma no en
porta cap amb `BOARD_HAS_PSRAM` i QSPI. El seu HAL només diu el que és seu: el **LED verd de GPIO5** és
un LED normal i no un WS2812 (s'encén o s'apaga, el color no hi cap), no té cap tecla que el
firmware llegeixi (l'únic botó és RST) i la **microSD** va al grup HSPI que la serigrafia del fabricant posa al
costat de la ranura (CS 15, SCK 14, MOSI 13, MISO 12). El PSRAM és el del xip i `heap_limit_kb()` en
contesta 4 096 només si el xip respon; una versió WROOM sense PSRAM no promet memòria que no té. La
bateria no s'anuncia fins que el divisor no estigui mesurat (docs/50, docs/95). L'emulador té el mateix
model, amb una targeta i sense vidre (docs/85).

**I una cara és un fitxer, que és el que aquella taula deia que faltava** (1/11, de la 0.36.0 a la
0.39.0). La lletra de 5 × 7 del sistema passa a ser **una cara més del mateix camí**: `font::system_face()`
i les cares d'un fitxer comparteixen `font::width` i `font::runs`, i el dibuix llegeix rectangles i no sap
d'on vénen. `ui.load_font(path)` dona **un objecte `font`** amb `:text_width`, `:size` i `:release`;
`cv:font(f)` i `ui.font(f)` el posen a un canvas o a la vista (`nil` torna a la del sistema); i el bloc de
glifs és de l'app i compta al seu pressupost —768 B per cara aleshores, **3 072 B des de la 0.55.0**, tres
cares alhora—. Una cara era de **fins a 8 × 8** (ara **16 × 16**) i de **96 codis del 0x20 al 0x7F, en
ordre i sense forats**, i n'hi ha **quatre formats i el fitxer mateix diu quin**: el propi (`lunok-font 1`,
una línia de directives i els glifs en `#` i `.`), el **BDF**
d'un servidor X (`STARTFONT`, les files del `BITMAP` en hexadecimal), el **PCF**, que és un BDF compilat
(`bdftopcf`), amb les taules indexades i cada taula en l'ordre de bytes que ella mateixa diu, i el **TTF**,
que no és una taula de bits sinó **contorns** (`lib/ttf`): una cara d'aquest últim no té cel·la, el seu bloc
és el fitxer sencer i `scale` vol dir la mida del cos en vuitens de píxel. Un fitxer que
no és ni una cosa ni l'altra es refusa amb el motiu, i també un BDF d'una caixa de més de setze columnes.
Vuit casos de `test/test_font` i tres de `test/test_ttf` ho proven, refusos
inclosos, amb el `.pcf` de debò que surt **idèntic al `.bdf`** del qual es va compilar i amb la `A` del `.ttf`
que surt **amb les vuit files del `.bdf`**. **Qui converteix una cara en píxels és el nucli i no l'app**:
Lua demana `text` i prou, perquè el retall, les escales i la comptabilitat del dany viuen en un sol lloc.

**I un control que se'n va es neteja abans de ser alliberat** (1/11): `cv:release()` **demana**, no fa. El
control i els seus fills queden marcats, el control que pinta rep **un sol rectangle, el del pare** —els
dels fills són tots a dins del seu, i omplir la llista de vuit amb trossos del mateix forat acaba en un
`gc16` de la unió, que és el contrari de barat—, i el buffer s'esborra a blanc i l'espai torna al sistema
**al final del `present` següent**, quan aquell tros de pàgina ja s'ha pintat a sobre. Un fantasma al paper
i un bloc vell donat al canvas següent són el mateix error vist de dos costats.

**I un objecte que apunta a un control que ja no hi és no és un punter penjat**: l'objecte de Lua es
guarda **l'espai i la generació**, i cada crida comprova els dos. Si el control ha desaparegut, la crida
respon `nil, E_INVAL` («aquest canvas ja no és a la pantalla»), `tostring` diu `canvas (gone)`, el recolector
no fa res (no hi ha res a alliberar dues vegades) i cap canvas nou pot escriure's a través d'aquell objecte
perquè la generació de l'espai ha avançat. L'únic forat teòric és que la generació doni la volta als 65 536
canvas fets.

**I el marc ja no és el buffer** (1/11): hi ha fins a **32 controls** i només **vuit** amb vista pròpia, i
un control sense vista —una graella, una caixa, un espaiador— **pinta al canvas que el conté, tallat al seu
marc**, i els seus fills es componen amb el desplaçament de cada capa sumat. Això és el que fa possible una
pàgina amb vint etiquetes, i és el que fa que un `UILabel` no necessiti cap buffer.

**I des de la 0.102.0 el sostre de 32 no hi és**: la taula de canvas es pren de la memòria extra a l'arrencada
—**512 KB, 2 978 ranures de 176 B**, ampliable o retallada per la memòria de la placa— i la llista de fills és
enllaçada (docs/30). **El que queda pendent és el preu d'aquesta taula en una placa sense memòria extra**: la
crida prefereix la PSRAM i cau a la RAM del xip, o sigui que a l'ATOM la taula es menja RAM interna abans que
cap app corri. El risc, els números i què s'hi farà són a [`75`](75-pressupost-de-memoria.md) §«La taula de
canvas i la RAM interna».

El **registre d'events i gestos per control**, amb les coordenades locals, és el següent pas: el punt, el
`dx`/`dy` i el començament del gest es calculen **sobre el marc** (el lloc i la cadena de pares) i no sobre
el buffer, o sigui que un control **sense vista també els ha de poder rebre** —una graella que es mou amb
el dit, un carrusel que es passa—, i el que decideix qui els rep és qui conté el punt, no qui té píxels.

Mentre el UILabel no existeixi, `bar.cpp` porta un pedaç honest, i **el pedaç és el retall**: el títol
s'escriu al mig de la barra amb una caixa (entre l'hora i la càrrega) i el que en surt no es pinta, o
sigui que el text ja es talla a la vora i no trepitja res. El que falta és que la caixa sigui **un
control** i no dos números calculats a `bar.cpp`: el retall viu a `font::draw` (paràmetre `Clip`), que és
on ha de viure perquè qualsevol text el faci servir (1/11).

## El que costa, i com es mira (fet l'1/11)

**Quaranta-sis comptadors a la porta** i `stats` al prompt (docs/30, docs/50): cada dispositiu, cada
unitat del disc, el rellotge, el tàctil, la PSRAM, el nucli i l'intèrpret, amb comptes, temps, mínim,
màxim i el moment de l'última crida. **La taula és la d'aquest build i aquesta placa** —un ATOM no té
vidre i la Paper no té llum— i cada fila que hi és surt amb zero si no s'ha fet servir (3/11, 0.57.1).
Costa **9 076 B de flash i 1 208 B de RAM** al Paper (docs/75) i el log de tot el sistema té una sola
forma, `[nivell][etiqueta] missatge` (docs/30, docs/85).

**El que queda d'aquest tros**, i no és gaire:

| Què | Per què |
|---|---|
| El pic de memòria d'una crida | avui es compta el temps i no els bytes; `sys.heap()` dona el pic de l'app sencera i no el d'una crida |
| El repartiment de les pintades per mode | només `gc16` té fila pròpia: `du`, `du4` i `a2` van juntes amb `glass.flush` |
| Un `stats` en format JSON o en una ordre a part | per a un guió de proves que vulgui els números sense llegir una taula d'ulls |

## El lector d'EPUB, i per què no és una app (7/11)

El lector d'EPUB del projecte del paper **no maqueta res**: demana al sistema la pàgina ja composta i
la pinta, perquè mesurar paraules i decidir on va cada línia és aritmètica sobre un buffer i, en Lua,
s'hauria de fer a bocins perquè un callback té un pressupost. O sigui que portar-lo és, sobretot,
**portar un motor**: el contenidor ZIP, el tokenitzador de l'XHTML, el paginador i el llibre obert
—3 319 línies de C++ i 1 344 de proves al paper— i després l'app de 3 959 línies.

El pla sencer, funcionalitat per funcionalitat i amb l'ordre de les fases, és a
[`46 · L'EPUB: el motor i el port`](46-epub-el-motor-i-el-port.md). El que cal saber d'aquí estant:
tres de les peces del motor ja hi són —el **ZIP** (7/11, 0.126.0: `lib/zip`), el **tokenitzador de l'XHTML**
(7/11, 0.128.0: 186 + 805 línies i 12 casos) i el **paginador** (7/11, 0.130.0: 138 + 646 línies i 12
casos, que treu una pàgina com un pla: paraules amb el seu lloc, regles i caixes de foto)—, la **tinta de
línia** hi és des de la 0.129.0 (negreta, cursiva i `sup`/`sub` al `lib/kernel/src/font.cpp`), el rellotge i
el disc també hi són, i les **cares amb accents** hi són per a la cara del llibre (0.127.0: `à è é ç · « »`
es pinten; les tres cares del sistema segueixen sent ASCII). I el **llibre obert** ja hi és (7/11, 0.131.0:
`kernel/book.h` i `book.cpp`, 88 + 390 línies i 4 casos, amb un `.epub` de debò escrit al disc del test i un
capítol inflat de 512 KB a la memòria extra). La **pàgina pintada en un canvas** (0.132.0), la **porta de
Lua** (0.132.0), la **foto en un canvas** (0.133.0), l'**app del lector** (0.134.0, amb els dits i les
tecles el 0.135.0), la **pantalla sencera** (0.136.0), **els menús amb la pàgina fosca** (0.137.0), **el peu amb el
comptatge de pàgines** (0.138.0) i, el 0.140.0, **el toc que passa pàgina** (la meitat dreta endavant i l'esquerra
enrere), **la pantalla de càrrega** (C8), **la passada de la pàgina** (C10: `du` quan les dues pàgines són de dos
tons), **l'error com a avís** (C12) i les **dues cares del llibre** (C13: un serif pel cos i un sans pels títols, les
dues amb Latin-1) ja hi són. I el 0.142.0 **l'OPF llegit de debò** (C2: `META-INF/container.xml` → OPF → `dc:title` i
l'`spine`, amb `epub.read` i la porta del ZIP, B6), que és el que diu el títol del llibre i **l'ordre dels seus
capítols**. El que no hi és i el lector demana, amb la seva fitxa a docs/46: la **miniatura del prestatge** (C16) —la cel·la d'un llibre amb la seva portada llegida del cau, el que fa l'app Files amb la
previsualització d'un fitxer: el cau ja s'escriu des del 0.146.0 (C11) i encara no el llegeix cap cel·la—, amb els
seus **ajustos** (D7). **L'índex (C14) hi és des del 0.143.0**, **la portada com a estat (C7) des del 0.144.0**, **anar
a una pàgina pel número (C15) des del 0.145.0** i **el peu del prestatge des del 0.147.0**. I el
**cau de miniatures (C11) des del 0.146.0**: la portada escalada a 64 píxels al costat del llibre, escrita pel
sistema i llegida com una foto més. **I el que ve darrere, decidit l'amo el 8/11**: els **tipus de lletra incrustats**
del llibre (**fets el 8/11, 0.150.0**: `epub.font`, el WOFF obert taula per taula i la cara del llibre a la pàgina),
la **maquetació fixa**, **SVG** com a imatge i, al final de tot, **els catorze forats** de
maquetació; i les portes de `zip` (**B7** i **B8**) perquè una altra app —un **navegador d'arxius**— pugui obrir un `.zip`.
I el 8/11 **D6 queda resolta de les dues bandes**: **`require`** (0.148.0) i el sostre d'un fitxer a **98 304 B**
(0.149.0), amb el lector partit en **quatre fitxers** —`reader.lua` **49 215 B**, `chapters.lua`, `notice.lua` i
`library.lua`, **64 411 B** en total—, o sigui **49 089 B** de marge al fitxer més gran.

## Les dues apps que l'amo va demanar el 9/11

Cap de les dues no falta: **la de la zona horària** hi és des de la 0.170.0 (`tz`, amb `time.leap` i `time.week`) i
**l'esborrador** des de la 0.171.0 (`wipe`, amb `fs.format` i `sys.reboot`), i totes dues amb icona pròpia i el seu
cas a `test_apps` (docs/30, docs/40, docs/70). **I el 9/11 el mateix dia en va demanar una tercera porta** —*«afegeix
també el botó de format per littlefs a l'app wipe»*—, que és la 0.171.1: la flash del sistema és un volum com la
targeta i es buida amb la mateixa crida (docs/30, docs/70).

### El que l'esborrador va deixar dit

- **La targeta i la flash es buiden però no es reformaten**: el format deixa el FAT de la targeta i el superblock del
  littlefs com eren i treu tots els fitxers i carpetes. Refer-los demana desmuntar i tornar a muntar el volum, i **no
  hi ha placa per provar-ho**; queda aquí com a decisió oberta, amb el risc de deixar un volum que la placa no
  reconeix si es fa a mitges (docs/70).
- **Un volum i una carpeta d'un volum no són el mateix**: `/data`, `/apps`, `/res` i `/fonts` són files de la mateixa
  flash, i `IStorage::format` no rep cap camí, o sigui que la porta mira el prefix del muntatge i refusa una carpeta.
  El dia que hi hagi un format per carpeta serà una altra crida i no aquesta (0.171.1, docs/70).
- **La barra no pot dir quant falta**: `fs.format` és una crida i prou, o sigui que la pàgina ensenya «encara no ho
  sé» (`value` negatiu) i la posa a 100 quan la resposta arriba. Una barra de debò necessitaria un format a trossos,
  i cap de les dues bandes el té (docs/30).
- **I la pàgina és l'única cosa que impedeix un `fs.format` sense voler**: el sistema no té model de permisos i una
  app pot cridar la porta quan vulgui. El que la fa honesta és la targeta d'avís amb el dial a la porta segura; el
  dia que hi hagi permisos (el `E_PERM` reservat de docs/30), aquesta és la primera porta que hi hauria de passar.

### La zona horària, amb els controls del sistema (10/11)

L'amo, amb la pàgina a l'emulador: *«el desplegable que hi ha a TimeZone amb "Week stats" no te cap estètica que mostri que
es una llista desplegable? per altra banda, el botó de sync NTP tampoc sembla un botó de sistema... son controls UI o uses
alguna altra cosa? el focus sembla no funcionar amb ells»*. **Les quatre coses eren certes i tenien una sola causa**: la
pàgina no tenia cap control. El desplegable era un `ui.frame` amb un text, una fletxa de dues línies i un `ui.rect` de
fila triada; el botó era un marc d'un píxel amb la paraula a dins; la casella era un quadrat d'un píxel amb una aspa de
`ui.line`; i el focus no hi arribava perquè **no hi havia res enfocable** (docs/30, docs/92).

- **Ara són els controls del sistema**: un `ui.button` amb `title` i `primary`, un `ui.checkbox` amb la seva etiqueta a
  dins i un `ui.selector` amb `label` i `items`, i la llista dels dos dies és un `ui.menu` del sistema sota el camp
  (0.174.0). El mapa **es queda en primitives**: és una imatge i una banda de quinze graus, i el dit és l'únic que el
  llegeix;
- **i el dial torna a ser del sistema**: les fletxes caminen l'anella i el centre és Enter, o sigui que l'hora es tria amb
  el dit —l'app necessita pantalla tàctil per arrencar— i les tres opcions amb el dial. **Els tres tocs del centre ja no
  tanquen la pàgina**: són tres activacions, i la sortida és el pessic del vidre, que és la regla de qualsevol pàgina amb
  controls (docs/30);
- **`net.ntp()` no contestava mai a una app** —un error de debò del pont, des que la crida existeix—: `net_progress`
  resol la promesa que el registre guarda a `kNetKey` i `l_net_ntp` era **l'única de les set crides de la xarxa** que no
  la hi posava, o sigui que un `net.ntp():await()` d'una app esperava per sempre mentre la comanda `ntp` de la consola
  contestava (la consola espera el waiter natiu i no aquesta promesa). El va trobar el cas de la pàgina `tz`, que és la
  primera app de l'arbre que demana l'hora a un servidor, i el subjecta
  `test_net_ntp_answers_the_app_and_not_only_the_shell`, que falla sense l'arranjament (docs/30, docs/85);
- **i un refús arriba com a llançament, no com un `nil`** (el contracte de tota promesa d'aquest sistema): el botó d'una
  placa sense xarxa desada —la placa més normal que hi ha— s'enduia l'app amb ell. La petició va dins d'un `pcall` i cada
  codi diu la seva frase a una persona (`no network saved`, `the server did not answer`, `the radio is busy`). El clic és
  un ganxo i un ganxo no pot esperar, o sigui que el botó deixa el desig i és el bucle de la pàgina qui demana l'hora —la
  regla que ja segueix l'acceptar d'una targeta—; mentre espera, el botó és `disabled`, i la petició té **vuit segons** de
  termini i no els vint del sistema (docs/30).

### La targeta que no netejava (10/11)

L'amo: *«sembla que els modals al tancar-se no netejen correctament la pantalla»*. Era cert, i la causa és de disseny: **una
targeta no té buffer** —no costa cap píxel, que és el que volíem— i per tant pinta la seva tinta **dins dels píxels de la
pàgina**. Res no en guarda una còpia, i el `release` només marcava el rectangle: la pàgina es quedava amb el marc i les
paraules de la targeta per sempre (mesurat a l'emulador amb una pàgina d'etiquetes i botons, 10/11).

- **El que fa ara el sistema** (0.173.2): omple el requadre de la targeta **amb el paper de la pàgina** (el nivell de
  l'últim `ui.clear` de l'app, blanc si no n'ha fet cap) i **demana a cada control que hi havia dins que es pinti una altra
  vegada**; el requadre es dibuixa amb el radi de la targeta, perquè les cantonades són de la pàgina. Una pàgina de
  controls —el cas de `wipe`, i el de qualsevol app que faci servir `ui.label`/`ui.button`/`ui.canvas`— **torna sencera
  tota sola**;
- **el que no pot tornar és el que l'app ha pintat amb primitives** (`ui.text`, `ui.rect`…): allò no és un control i no sap
  dibuixar-se, i és l'app qui ha de tornar a pintar la pàgina quan la targeta marxa. És la recepta que l'editor ja feia
  servir i que `docs/30` no deia enlloc;
- **i el forat és de l'app quan la vista té un `on_draw`** (0.173.3, i és una pregunta de l'amo: *«l'app hauria de repintar
  la seva area dañada, no?»*). El mecanisme ja hi era —`ui.present` crida el dibuix de l'arrel, tallat en el requadre que es
  deu—, o sigui que n'hi havia prou de **no tapar-lo amb paper**: el sistema marca el forat, el dibuix de l'app el repinta i
  la pàgina torna **amb les seves primitives**. Sense el flag, el forat es pintava dues vegades —una rentada de `gl16` per a
  un dibuix de `du` que la tapava tot seguit—, que en aquest vidre és una passada de tinta per a res. És el camí preferit:
  **una app que pinta la seva pàgina a mà posa `ui.root().on_draw` i no ha de fer res més quan un modal marxa**. El paper
  queda com a caiguda per a les pàgines que no tenen dibuix propi, que és el cas de `wipe` (docs/30, docs/75);
- **la còpia dels píxels de sota la targeta ja no és a la llista** (era la decisió oberta de la 0.173.2: 49 400 B per a una
  targeta de 380 × 260). Amb `on_draw` una pàgina torna sencera sense cap còpia, i una app que pinta amb primitives i no vol
  el seu `on_draw` és una app que es pot escriure de la manera bona; el pressupost de la placa sense PSRAM no la pagaria
  (docs/30, docs/75).

## Fora de la v1

| Cosa | Per què queda fora |
|---|---|
| Targeta SD a l'ATOM | No hi ha maquinari (al Paper hi és, i és el pas 7) |
| Actualització del firmware (OTA) | La taula de particions ja hi deixa el forat, però les dues ranures es van fusionar per fer-hi cabre el Bluetooth |
| Àudio | Ni maquinari ni necessitat |
| Múltiples apps simultànies | És un no-objectiu, no una fita pendent |

## Com es tanca una fita

1. El criteri passa a l'emulador, amb un guió de proves.
2. El criteri passa a la placa, o es diu exactament quina part no s'ha pogut provar i per què.
3. Els números mesurats s'escriuen a `75`.
4. Si una decisió ha canviat, es canvia el document de la matèria **abans** de tancar la fita.
