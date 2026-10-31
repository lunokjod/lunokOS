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
són les mesures de camp d'M5, M6 i M7, dues crides que ningú no ha demanat encara (`net.softap` i
escriptura a `/nvs`), la resta del contracte de Lua que no té codi al darrere (`docs/30`, taula
d'⏳) i les decisions que necessiten maquinari o una persona al davant.

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
| **Fora** | La targeta SD: no hi ha maquinari. La VFS ja la preveu i cap HAL no la munta. |
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

## La M5 Paper, pas a pas

L'entorn `paper` hi és i el firmware **compila** (551 201 B de flash i 52 248 B de RAM estàtica,
docs/75): porta la consola, el disc, la NVS, el rellotge del SoC, les dues fondàries de son i el
*main power hold*. **No s'ha flashejat encara**, i el que falta té ordre perquè cada pas necessita
l'anterior:

| # | Pas | Criteri per dir-lo fet |
|---|---|---|
| 1 | **Que arrenqui**: `pio run -e paper`, flashejar i veure el prompt. El *main power hold* de GPIO2 és el primer que fa el HAL, perquè sense ell la placa s'apaga i sembla un crash | El prompt respon `help` i `caps` diu **8 de 17** |
| 2 | **El panell**: driver propi de l'**IT8951E** (SPI: CS 15, SCK 14, MOSI 12, MISO 13, BUSY 27, amb l'alimentació a GPIO23 i GPIO5) i `IDisplay` de 960×540 amb 16 grisos | Una pàgina en blanc i un rectangle negre de debò, amb els modes `INIT` i `GC16` mesurats |
| 3 | **Les tecles**: fetes. `IInput` porta l'índex, la cara, el lloc, si és simple o composta i el grup, i l'app rep tot això amb `button.list()` per donar una funció a cada **posició** (docs/30, docs/50). El que falta és **comprovar a la placa quina tecla és quina**: la llibreria del fabricant no diu el mateix al capçalari (`GPIO37` esquerra, `GPIO39` dreta) i al seu README (al revés), i el HAL segueix el capçalari | Prémer cada tecla i veure a `button` si el lloc que diu és el que el dit toca |
| 4 | **El rellotge BM8563** (I2C 0x51) i el despert per la seva alarma: és el que fa que l'hora sobrevisqui a treure el cable | `time.caps().source` diu `"rtc"` i un son de deu minuts torna a l'hora |
| 5 | **La bateria**: divisor a GPIO35 (×2), la corba del full del fabricant i una mesura amb el mesurador al davant | `power.battery()` dona mV i % que quadren amb el mesurador |
| 6 | **La ràdio**: el mateix `INet` de l'ATOM (esp_wifi directe, provisioning, NTP i una pàgina). És el bloc més gran i el que més flash costa | `provision`, `ntp` i `http` funcionen com a l'ATOM |
| 7 | **La targeta microSD**: CS a GPIO4, compartint SCK/MOSI/MISO amb el panell; el muntatge `/sd` de la VFS ja hi és | `mounts` llista `/sd` i `fs.space` en dona números |
| 8 | **El toc GT911** (0x14, INT a GPIO36) i el SHT30 (0x44) | Un toc diu on és i el SHT30 dona temperatura |

I una cosa que no és de la placa però que aquesta placa fa evident: **les parts d'ESP32 que els dos
HAL comparteixen**. El disc i la NVS hi són dues vegades (376 línies copiades) i la ràdio ho serà més;
plegar-les en una llibreria comuna és feina del proper torn que toqui aquesta banda.

## Fora de la v1

| Cosa | Per què queda fora |
|---|---|
| Pantalla (`IDisplay`) | No hi ha maquinari a la placa de proves; la interfície ja hi és |
| Targeta SD | No hi ha maquinari |
| Instal·lació d'apps des del prompt o per xarxa | Primer ha d'existir el disc, i el disc hi és: això és el següent de debò |
| Actualització del firmware (OTA) | La taula de particions ja hi deixa el forat, però les dues ranures es van fusionar per fer-hi cabre el Bluetooth |
| Àudio | Ni maquinari ni necessitat |
| Múltiples apps simultànies | És un no-objectiu, no una fita pendent |

## Com es tanca una fita

1. El criteri passa a l'emulador, amb un guió de proves.
2. El criteri passa a la placa, o es diu exactament quina part no s'ha pogut provar i per què.
3. Els números mesurats s'escriuen a `75`.
4. Si una decisió ha canviat, es canvia el document de la matèria **abans** de tancar la fita.
