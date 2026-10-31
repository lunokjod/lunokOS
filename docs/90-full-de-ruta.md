# 90 · Full de ruta

## Regla general

Una fita no està feta perquè el codi compila. Està feta quan **passa el seu criteri** a l'emulador i,
quan en depèn, a la placa. Cada fita diu què s'ha de mesurar, i el número que surti es desa a
`75 · Pressupost de memòria`.

Aquest document només té **el que falta**. El que ja està fet es llegeix al codi i a l'historial de
git, i el que el sistema **fa** avui s'explica als altres documents: `10` (mapa), `20` (intèrpret),
`30` (API de Lua), `40` (apps), `50` (maquinari), `60` (temps), `70` (fitxers), `75` (memòria),
`80` (compilació) i `85` (emulador).

Estat d'avui, en una línia: **M0, M1, M2, M3, M4, M5, M6 (mecanisme) i M7 estan fets**; el que queda
són les mesures de camp d'M5, M6 i M7, **M8** (que el banc rebi la placa que ha d'imitar), dues
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
| **Què queda** | **Mesurar a la placa** els microsegons d'un frame del llum (avui són un càlcul, no una mesura) i el corrent d'un llum encès i apagat amb el mesurador USB. I **el sensor del Matrix**: el driver està escrit pel full del fabricant i no s'ha vist en maquinari, o sigui que el **sentit dels eixos** en aquesta placa i la qualitat de la mesura estan per comprovar (`run tilt` els ensenya a la consola). |
| **Criteri** | El que ja es compleix: el gest de tres tocs tanca l'app del davant, una app rep les premudes, i un llum que ha pres una app no li sobreviu. |

## M8 · El banc és una placa

El banc és **una** placa des del 31/10: `--board m5stack-atom|m5stack-atom-matrix|m5stack-paper`
diu quina, i sense dir res és l'ATOM (`board::models_count()`, `model(i)`, `default_model()`).
`sys.board` respon **aquell nom** i no `emulator`, que no és cap placa; `sys.emulator` continua cert i
és la pregunta que ha de fer canviar el comportament d'una app pel fet d'estar al banc. Com que
`board_name()` respon **un** nom, `boards` del manifest es compara al PC exactament com a la placa:
una app del model corre i una d'un altre queda llistada amb l'avís i es refusa dient quin és aquesta
placa. `version` i la capçalera diuen `m5stack-paper (emulator)`, les dues coses alhora.

El que **encara no** fa és que el maquinari fals segueixi el model —avui és la **uniò** dels tres—,
perquè el que no hi és no es pot provar:

| Peça | Com ha de quedar |
|---|---|
| Capacitats | Surten del model, com a la placa: `kCapLed` fora al Paper (té 0 llums), `kCapImu` fora al Paper, `kCapDisplay` només quan hi hagi qui dibuixi el panell (avui ningú). Cap capacitat de mentida |
| Tecles | El **nombre, la cara, el lloc i si és composta** són del model (una tecla simple a l'ATOM, el dial de tres al Paper); l'**estat** (a baix o a dalt) ve d'una sola crida, `emu::press_key(key, down)`, que és la que ja fan servir les proves |
| Llums | El comptador del model: 1 a l'ATOM i al Paper, 25 al Matrix amb el sostre de 20 (`board::led_max()`) |

Els perfils han de viure en una taula en temps d'execució: la identitat de les plaques de xip és una
capçalera triada per `-I` (docs/50) i l'emulador no en pot veure tres de cop. Les plaques de xip es
queden amb la capçalera i el banc té una fila per model amb els mateixos números; una prova ha de
comprovar que cada fila té la seva `lib/board_<id>` i que els números diuen el mateix que el document
de la placa.

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

Quan es faci, `docs/30`, `docs/40`, `docs/85` i el `--help` del binari es canvien al mateix commit.

## La M5 Paper, pas a pas

L'entorn `paper` hi és i el firmware **compila**: porta la consola, el disc, la NVS, el rellotge del
SoC, les dues fondàries de son, el *main power hold*, les tres tecles, la ràdio que hereta del xip i
la targeta quan n'hi ha al slot. **Mesurat el 31/10**: 2 064 009 B de flash (49,2 % de 4 MB) i 92 924 B
de RAM estàtica (docs/75). **S'ha flashejat i arrenca**: el banner (`lunokOS 0.6.0  m5stack-paper`)
i el prompt hi són. El que falta té ordre perquè cada pas necessita l'anterior:

| # | Pas | Criteri per dir-lo fet |
|---|---|---|
| 1 | **Que arrenqui**: `pio run -e paper`, flashejar i veure el prompt. El *main power hold* de GPIO2 és el primer que fa el HAL, perquè sense ell la placa s'apaga i sembla un crash | El prompt respon `help` i `caps` diu **12 de 17** |
| 2 | **El panell**: driver propi de l'**IT8951E** (SPI: CS 15, SCK 14, MOSI 12, MISO 13, BUSY 27, amb l'alimentació a GPIO23 i GPIO5) i `IDisplay` de 960×540 amb 16 grisos | Una pàgina en blanc i un rectangle negre de debò, amb els modes `INIT` i `GC16` mesurats |
| 3 | **Les tecles**: fetes. `IInput` porta l'índex, la cara, el lloc, si és simple o composta i el grup, i l'app rep tot això amb `button.list()` per donar una funció a cada **posició** (docs/30, docs/50). La lectura és la del xip (`GpioKeys`) i el que és d'aquesta placa és la taula: el **damunt del dial és `GPIO37`**, perquè és el que fa que prémer la tecla de dalt digui `up` (docs/50). La llibreria del fabricant es contradiu (capçalari: `GPIO37` esquerra i `GPIO39` dreta; README: al revés) | Prémer la tecla de dalt i veure a `button` que diu `up`: si diu `down`, es canvien dues línies de la taula `kKeys` de `lib/hal_paper/` |
| 4 | **El rellotge BM8563** (I2C 0x51) i el despert per la seva alarma: és el que fa que l'hora sobrevisqui a treure el cable | `time.caps().source` diu `"rtc"` i un son de deu minuts torna a l'hora |
| 5 | **La bateria**: divisor a GPIO35 (×2), la corba del full del fabricant i una mesura amb el mesurador al davant | `power.battery()` dona mV i % que quadren amb el mesurador |
| 6 | **La ràdio**: **provada a la placa i funciona** (31/10). El codi és el de l'ATOM i el `net()` d'aquesta placa el retorna (`lib/hal_esp32/src/net.cpp`, mogut i no copiat; les capacitats venen de `esp32_caps()`) | `provision`, `ntp` i `http` funcionen com a l'ATOM |
| 7 | **La targeta microSD**: **feta** (31/10). `esp32_sd()` munta la targeta a `/sd` amb FATFS per SPI (CS GPIO4, SCK/MOSI/MISO compartits amb el panell), només si una targeta respon i amb una sola temptativa per arrencada. Dues coses que va dir la placa: el **CS del panell (GPIO15) s'ha de deseleccionar** abans de tocar el bus —arrenca seleccionat perquè és un pin de *strapping*— i la targeta va a **10 MHz**; no se sap si 20 MHz també hi van, perquè els dos canvis es van fer junts | `mounts` llista `/sd` amb números i `ls /sd` llegeix el directori |
| 8 | **El toc GT911** (0x14, INT a GPIO36) i el SHT30 (0x44) | Un toc diu on és i el SHT30 dona temperatura |

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
