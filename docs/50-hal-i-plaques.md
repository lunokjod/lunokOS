# 50 · HAL i plaques

## Dues capes de sota, no una

- **HAL** (*Hardware Abstraction Layer*): els **dispositius**. Rellotge, discs, xarxa, energia, llum,
  botó, sensor. Cada placa implementa els que té.
- **PAL** (*Platform Abstraction Layer*): el **sistema de sota**. Cua d'esdeveniments, consola,
  rellotge monòton, reinici. Cada sistema operatiu (FreeRTOS o POSIX) n'implementa una.

Si una cosa és «un aparell que es pot endollar», és HAL. Si és «una manera de fer cues», és PAL.

## Interfícies del HAL

Totes són classes amb funcions virtuals pures, sense memòria dinàmica i sense excepcions. Una placa
que no té un dispositiu torna `nullptr` i la seva capacitat és `false`.

| Interfície | Què abstrau | Lite | Matrix | Paper | Emulador |
|---|---|---|---|---|---|
| `IClock` | hora de paret, rellotge monòton, **un sol** despertador, i tornar a prendre l'hora al xip | ✓ | ✓ | ✓ (**amb la BM8563**: l'hora sobreviu al cable, el xip arma el despert i corregeix el comptador després de cada son) | ✓ |
| `IStorage` | un volum muntat: stat, list, open, read, write, seek, close, remove, mkdir, rename, space | ✓ | ✓ | ✓ | ✓ |
| `IKeyValue` | claus i valors, exposats com a fitxers a `/nvs` | ✓ | ✓ | ✓ | ✓ |
| `INet` | la ràdio: connectar, escanejar, provisioning, NTP i una pàgina | ✓ | ✓ | ✓ (provat a la placa) | de mentida |
| `IPower` | bateria, son, **apagar** (tallar el corrent de debò), velocitat del processador, i si el son profund és una **apagada** | ✓ (son i velocitat; **no es pot apagar**, no té el seu corrent) | ✓ (el mateix) | ✓ (bateria i son; el son profund apaga; `power_off` fa **la seqüència del fabricant**: desarma el xip del rellotge, espera 600 ms, baixa la **GPIO23** (panell), la **GPIO5** (ports), la **GPIO2** (corrent principal) i posa el xip a dormir profund, o sigui que només el botó la torna a encendre) | de mentida (el model del Paper acaba el procés; l'ATOM diu que no) |
| `ILed` | un llum o una tira: quants n'hi ha, el sostre del canal, pintar | 1 | 25 | — (no en té) | el del model: 1, 25 o cap |
| `IInput` | les tecles: quantes n'hi ha, si una és premuda i quant fa | ✓ (1) | ✓ (1) | ✓ (3) | ✓ (les del model) |
| `IImu` | acceleració, gir i temperatura del xip inercial | — | ✓ | — | de mentida |
| `ISensor` | l'aire: temperatura i humitat relatives | — | — | ✓ (**SHT30**, a la I2C interna; es mesura quan es demana i no té estat) | de mentida (el model del Paper) |
| `IDisplay` | la pantalla | — | — | **pendent** (e-ink IT8951E) | — |

El contracte sencer, amb totes les funcions i els comentaris que les expliquen, és a
`lib/hal/include/hal/hal.h`. **Quan aquest document i el capçalari no diguin el mateix, mana el
capçalari**: aquí hi ha el perquè de les decisions, no una còpia de les signatures.

```cpp
class IPlatform {
 public:
  virtual const char* id() const = 0;      // "m5stack-atom", "m5stack-atom-matrix"; al banc, el model
  virtual const char* family() const = 0;  // "esp32": el que un manifest vol dir amb "esp32"
  virtual bool emulator() const = 0;       // cert només al PC
  // Les plaques a les quals aquesta banda respon: el seu `id()` i prou, també al banc,
  // que respon el model que se li ha dit que sigui. És contra això i la família que es
  // compara `boards` (docs/40).
  virtual uint32_t board_count() const { return 1; }
  virtual const char* board_name(uint32_t index) const {
    return (index == 0) ? id() : nullptr;
  }
  virtual const Caps& caps() const = 0;
  virtual uint32_t heap_limit_kb() const = 0;
  virtual IClock* clock() = 0;
  virtual IStorage* storage(const char* mount) = 0;   // "/littlefs", i "/sd" on hi hagi ranura
  virtual IKeyValue* nvs() = 0;
  virtual INet* net() = 0;
  virtual IPower* power() = 0;
  virtual IDisplay* display() = 0;   // nullptr en aquesta versió
  virtual ILed* led() = 0;
  virtual IInput* input() = 0;
  virtual IImu* imu() = 0;
};
```

Tres decisions que valen per a totes les interfícies:

- **Ningú no espera el maquinari.** La ràdio, l'escaneig, el provisioning, el NTP i una baixada de
  més comencen i **tornen de seguida**; el nucli pregunta com va a cada torn. Una crida que es queda
  esperant un xip és un prompt que no respon (docs/30, docs/60).
- **Ningú no reserva memòria.** Els buffers són fixos i de mida coneguda; quan no n'hi ha, l'error es
  torna com a valor i es diu. El pressupost és a docs/75.
- **Un sol despertador.** `arm_wake` arma el pròxim termini i prou: dos despertadors volen dir que un
  d'ells no es complirà (docs/60).

## El que és del xip i no de la placa

Una placa no és només un xip, però tot el que és del xip és el mateix a totes les plaques d'aquest
projecte, i escriure-ho dues vegades és escriure-ho dues vegades. Viu a **`lib/hal_esp32/`**, i una
llibreria de placa no en copia res: ho fa servir i diu els pins.

| A `lib/hal_esp32/` | Per què és del xip |
|---|---|
| `esp32/caps.h` | Les capacitats que el xip té a qualsevol placa: consola, ANSI, rellotge del SoC i el seu despert, NVS, els dos sons i la ràdio amb provisioning i pàgina. Una placa **les hereta i només afegeix** el que té de seu |
| `esp32/storage.h` | LittleFS sobre la partició de dades, trobada per la seva etiqueta |
| `esp32/nvs.h` | El magatzem clau-valor sobre la partició `nvs` |
| `esp32/clock.h` | El rellotge del SoC i l'època a la memòria RTC, que sobreviu al son profund |
| `esp32/power.h` | Els dos sons, el temporitzador del RTC i les tres freqüències del processador. Rep quin pin desperta el xip, o cap |
| `esp32/gpio_keys.h` | El **lector** de tecles en un GPIO: actiu en baix, sense interrupció, 10 ms de rebot. **El xip no té cap tecla**: quines n'hi ha, en quin pin i en quina cara ho diu la taula de la placa |
| `esp32/ws2812.h` | El **controlador** d'una tira de WS2812 per RMT. **El xip no té cap llum**: el pin i quantes n'hi ha són paràmetres de plantilla que posa la placa (els buffers són de la mida de la tira d'aquella placa, docs/75) |
| `esp32/net.h` | La ràdio: `esp_wifi` directe, scans, provisioning (AP i Bluetooth), NTP i una pàgina amb TLS |
| `esp32/sd_card.h` | Una targeta al **slot** de la placa: FATFS per SPI i les crides de fitxer. **El xip no té ranura**; els quatre pins i l'host SPI els diu la placa. La targeta i el panell del Paper comparteixen bus |
| `esp32/i2c.h` | El bus I2C: una parella de pins i les dues transaccions que parlen tots aquests xips (escriure, i escriure un registre i llegir-ne). **El xip no té cap xip penjat**; què hi ha a la línia ho diu cada placa |
| `esp32/bm8563.h` | El **rellotge amb pila** d'aquesta família de xips: hora en BCD, el bit VL que diu si ha perdut la pila, i l'escriptura. La placa diu l'adreça i el bus |
| `esp32/rtc_clock.h` | El patró de qualsevol placa amb un rellotge amb pila a sobre del del xip: el SoC compta (monòton i despert) i el xip recorda (es llegeix a l'arrencada i s'hi escriu a cada `set_epoch`) |

La línia que separa les dues llibreries és aquesta: **si el codi no sap sobre quina placa corre, és
del xip; si ha de saber un pin, una etiqueta de partició o quants llums hi ha, és de la placa.** I el
que el xip **no** té és cap tecla i cap llum: el que hi ha a `hal_esp32` són els **lectors i els
controladors** del que una placa hi enganxa, no els aparells. Per això `esp32_caps()` no diu `input`
ni `led`, i els diu cada placa que de debò els té.

## La identitat de la placa

La identitat no és dins de cap HAL: és una llibreria pròpia per placa, a `lib/board_<id>/include/board/`,
amb dues capçaleres de nom fix:

| `board/identity.h` | Què diu |
|---|---|
| `id()` | el nom de la placa: `m5stack-atom`, `m5stack-atom-matrix`, `m5stack-paper`; al banc, el model que se li ha dit que és |
| `family()` | el xip: `esp32`. És el que un manifest vol dir amb `"boards": ["esp32"]`, i val per a les quatre bandes |
| `emulator()` | cert només al PC, i és el que diu que la placa de sota `id()` és de mentida |
| `led_count()` | quants llums: 1 al Lite, 25 al Matrix i cap al Paper. **Al banc no hi és**: el nombre de llums el diu el model (`emu::model().lights`: 1, 25 o 0) |
| `led_max()` | el sostre d'un canal: 255 al Lite, 20 al Matrix i al banc |
| `has_imu()` | si la placa porta sensor inercial |

El banc té una cosa que cap placa no té: **`models_count()`, `model(i)` i `default_model()`**, els
models que sap ser —`m5stack-atom`, `m5stack-atom-matrix` i `m5stack-paper`—. No els respon tots alhora:
la persona li diu quin és (`--board m5stack-paper`, i **cal dir-lo**: el banc no tria placa) i `id()` i
`board_name()` responen **aquell**, de manera que una app s'accepta o es refusa al PC igual que a la
placa (docs/40, docs/85). `sys.emulator` continua cert: el nom és el d'una placa que existeix i el
maquinari de sota és fals.

El maquinari fals **segueix el model** que el banc ha rebut —capacitats, nombre de llums, nombre de
tecles, sensor, indicador de bateria, ranura i FRAM surten tots de `emu::model()`—, perquè una app que
demani més del que té la seva placa no ho trobi al banc i es cregui un número de mentida. L'únic que
encara és comú als tres models és la **forma de les tecles** (el dial compost de la Paper: cara
esquerra, `centre`/`up`/`down`), i això és el que queda a M8 (docs/90).

`board/impl.h` arrossega les capçaleres de debò (el HAL d'aquesta placa i la seva PAL), i només
l'inclou `src/board_link.cpp`.

**Com es tria una placa, sense cap `#ifdef`.** Cada entorn de `platformio.ini` afegeix
`-Ilib/board_<id>/include` i fa `lib_ignore` de les altres identitats, o sigui que cada build en veu
**exactament una**. Dues precaucions que aquest mecanisme imposa:

- **Les identitats han de ser dins de `lib/`.** El cercador de dependències de PlatformIO només segueix
  capçaleres que siguin d'una llibreria: amb `board/impl.h` en un directori del projecte, el cercador
  no veia `hal_atom` i el build petava amb `hal_atom.h: No such file or directory`. Ara falla **fort**,
  que és el que es vol.
- **`lib_ignore` ha de ser complet.** PlatformIO posa els `-I` de les llibreries **davant** dels del
  `build_flags` (`piolib.py`: `PrependUnique(CPPPATH=…)` contra l'`Append` dels flags), o sigui que
  dues capçaleres `board/identity.h` al camí es decidirien per ordre i en silenci. Amb `lib_ignore` no
  n'hi ha dues.

**El Matrix no és una placa de PlatformIO**: les dues entorns escriuen `board = m5stack-atom` perquè el
xip, la flash i la taula de particions són els mateixos, i aquest nom no les pot distingir. Per això la
identitat del Matrix diu `m5stack-atom-matrix`, i una app que hagi de triar camí de maquinari té un nom
per fer-ho (docs/40).

## El llum

Una interfície, dues mides: el Lite té **un** WS2812 i el Matrix **vint-i-cinc** WS2812C de 2020 al
mateix pin. El contracte és el mateix —`count()`, `max_value()`, `fill()`, `pixel()`, `show()`—, i la
política és del nucli i no del driver: qui mana què es pinta és `lib/kernel/src/led.cpp` (docs/30).

| | Lite | Matrix |
|---|---|---|
| Llums | 1 | 25 |
| Sostre d'un canal | 255 | **20** (el número de M5Stack: més brillantor escalfa els LEDs i l'acrílic) |
| Com s'escriu | `neopixelWrite` del core (RMT, tick de 100 ns, GRB) | un sol frame d'RMT amb els 25 llums |
| Cost d'un frame | **30 µs**, bloquejant | **750 µs**, bloquejant |

**Una escriptura per llum no és una opció**: cada escriptura és un frame amb la seva pausa i la tira es
quedaria a la primera. El sostre del canal és una constant de la identitat i no un gust, i el sistema el
fa complir **abans** que res arribi al driver; l'emulador té el mateix sostre perquè el PC no amagui un
límit que la placa té.

**Què diu el llum, i per què hi ha dues capes.** El driver només sap pintar; qui decideix el color és
`lib/kernel/src/led.cpp`, i ho fa en dues capes (docs/30):

- **el sistema**, amb quatre moments i quatre efectes: respiració blanca a l'arrencada, parpelleig blau
  mentre la ràdio treballa, rebot turquesa quan venç una alarma i SOS vermell quan el registre escriu
  un error. L'ordre mana: un error no el tapa una alarma, i una alarma no la tapa la ràdio;
- **l'app**, amb `led.play`: mentre dura, el que diu l'app és el llum, i en marxar es deixa anar (i
  deixar anar vol dir apagar, no tornar al color d'abans).

Els quatre efectes són `breathe`, `blink`, `sos` i `bounce`, i **no hi ha cap fil nou**: un efecte és
una funció del temps des que va començar, i el nucli el pinta a final de cada torn demanant-li a
`ILed` només el canvi (docs/30, docs/75). El que això compra és que una animació no pugui anar
endarrerida ni estirada: un torn que arriba tard pinta el frame d'aquell mil·lisegon.

**Un llum encès és corrent**, i per això el motor té tres regles d'estalvi: els efectes tenen un
sostre de nivell (`level`) que no és 255 si no cal; un moment del sistema **sempre s'acaba**; i la
política d'energia no deixa la placa apagar-se mentre el llum té alguna cosa a dir (docs/60).

## Les tecles

L'ATOM té **un botó** a **GPIO39**; la Paper, **tres tecles** a **GPIO38** (la central, que és també el
botó d'encesa), **GPIO37** i **GPIO39** (les laterals). Totes són entrades sense pull-up configurat:
la resistència la porta la placa i prémer les porta a GND. El rebot és de **10 ms**, el mateix que fa
servir la llibreria del fabricant, i es llegeixen **a cada torn del nucli** (cada 20 ms mentre una app
viu), sense cap interrupció: un dit no baixa de 50 ms, i una premuda que no arriba a un torn no és una
premuda.

La **tecla 0 és la principal** —l'única de l'ATOM, la central de la Paper— i és l'única que compta el
gest de tancar; les laterals són de l'app (docs/30).

**I cada tecla diu on és**, perquè una app doni una funció a una posició i no a un número. El model és
el d'una capsa: la placa té **sis cares** i la tecla és en una d'elles, en un dels **cinc llocs** de la
cara. A sobre, la tecla diu **com està feta** i a quin **component** pertany (docs/30):

| | ATOM (Lite i Matrix) | Paper |
|---|---|---|
| Tecles | 1 | 3 |
| Cara | `top` (el botó gran de dalt) | `left` (el dial del costat esquerre) |
| Lloc | `centre` | `centre` (la central), `up` i `down` (les altres dues, que són el **damunt i el sota** del dial) |
| Com està feta | `simple` | `compound` |
| Component (`group`) | 0 | 0 per a les tres: **és un sol dial** |

El `kind` no és decoració: el botó de l'ATOM i les tres posicions del dial de la Paper són el mateix
contacte i no són la mateixa cosa de tocar, i una app que dibuixi un menú ha de saber si té un botó o
tres posicions d'una roda.

## El sensor inercial (Matrix)

El Matrix porta un **MPU6886** de sis eixos —acceleració i gir, **sense brúixola**— a la I2C interna:
SDA a **GPIO25**, SCL a **GPIO21**, **100 kHz**, adreça **0x68**, i `WHO_AM_I` (0x75) ha de contestar
**0x19**. Una lectura són **14 octets seguits** des de 0x3B:

| Magnitud | Escala | Aritmètica |
|---|---|---|
| Acceleració | ±2 g | 16 384 comptes per g |
| Gir | ±250 °/s | 131 comptes per grau per segon |
| Temperatura | — | 326,8 comptes per grau, amb +25 °C de desplaçament |

- **Les tres coses vénen juntes** perquè són a la mateixa ràfega de registres: agafar-les en dues crides
  és dues mostres de dos instants diferents (docs/30).
- **El xip pot no contestar**: és un xip en un bus compartit, i la crida torna `false` en comptes de
  quedar-s'hi.
- **El sentit de cada eix en aquesta placa no s'ha mesurat** (docs/90). Qui n'hi depengui ho ha de poder
  comprovar en un segon: per això l'app `tilt` ensenya els números a la consola.

## La ràdio

**El sistema parla amb `esp_wifi` directament; la classe WiFi d'Arduino no s'inicialitza mai.** No és un
gust: `WiFiGeneric` respon **cada** `ARDUINO_EVENT_WIFI_SCAN_DONE` amb `_scanDone()`, que llegeix la
llista d'escaneig i la **allibera**, i `wifi_prov_mgr` la llegeix pel seu compte dins del seu propi
gestor. Dues biblioteques volent la mateixa llista deixen el telèfon amb el que sobra.

Al HAL hi ha `esp_netif_init`, el bucle d'esdeveniments, `esp_wifi_init` i `esp_wifi_start`, i **l'estat
de l'estació surt dels esdeveniments** (`WIFI_EVENT_STA_CONNECTED`, `WIFI_EVENT_STA_DISCONNECTED`,
`IP_EVENT_STA_GOT_IP`), no de `WiFi.status()`. Les llibreries de xarxa d'Arduino estan fora del build
amb `lib_ignore` (docs/80).

Regles que això imposa i que el codi compleix:

- **Una feina a la vegada.** Escanejar, connectar i provisionar no es barregen; quan una és en marxa,
  `net::busy_with()` diu quina és (`scanning`, `connecting`, `provisioning`) i la resta respon `E_BUSY`
  amb aquest nom.
- **`esp_wifi_set_mode()` reinicia el controlador.** Una connexió demanada just abans d'aquell reinici es
  perd **sense error i sense cap esdeveniment**. Per això hi ha un sol lloc que decideix l'associació
  (`ask_station()`): si el controlador encara no és en mode estació, es canvia el mode i la connexió es
  demana a `WIFI_EVENT_STA_START`, que és el moment en què ja és a punt; si ja hi és, es demana de
  seguida.
- **La placa té el seu propi registre de la xarxa.** `esp_wifi` pot tenir credencials d'un firmware
  anterior —la partició `nvs` no s'esborra en tornar a flashejar—, i creure-se-les vol dir dir que hi ha
  xarxa quan no n'hi ha. El sistema només creu el seu registre (`lunoknet/ssid`), que s'escriu quan una
  app dona la xarxa i quan algú la posa a mà amb `wifi add`. **La contrasenya no es copia enlloc**: la té
  el driver, i el registre propi guarda només el nom.
- **Connectar amb la xarxa desada és cosa del driver.** `WiFi.begin()` d'Arduino sense arguments no
  connecta (respon `WL_CONNECT_FAILED` de seguida): el que connecta és `esp_wifi_get_config` +
  `esp_wifi_connect`, i és el que fa `wifi_start_stored`.
- **Les comandes que esperen es poden aturar.** `wifi stop` talla la feina que hi hagi i treu la placa
  de la xarxa; `wifi forget` primer atura i després esborra, perquè esborrar és la decisió de la persona
  i val més que la feina que hi havia en marxa.
- **Encendre la ràdio a cada arrencada és una decisió d'energia, no un descuit.** Una placa que ha desat
  una xarxa arrenca amb `network: <ssid>` i `state: off`; qui la vol connectada ho demana (`wifi
  connect`) (docs/75).

### Escanejar

`wifi_scan_start` i `wifi_scan_done` no esperen, i `wifi_scan_count` diu quants registres se'n poden
llegir. La llista que rep una app és **la taula pròpia del HAL**, copiada del controlador: la del
controlador la pot buidar qualsevol altre component que pregunti, i una llista que desapareix no és una
llista. Una xarxa que no diu el nom surt com a `(hidden network)`: una entrada que s'amaga és igual que
un escaneig que no ha tornat res.

### Provisioning: el component d'Espressif, sense inventar

El provisioning és el component **`wifi_provisioning`** de l'ESP-IDF amb **Security 1** (una paraula de
proof of possession), que és el que esperen les apps **ESP BLE Provisioning** i **ESP SoftAP
Provisioning** d'Espressif. No hi ha protocol propi ni cap endpoint inventat.

| | Bluetooth (`provision`) | `provision softap` |
|---|---|---|
| Transport | `wifi_prov_scheme_ble` sobre **Bluedroid** | `wifi_prov_scheme_softap` |
| Per què | és el que recomana el fabricant del xip i no treu el telèfon de la seva xarxa | no necessita Bluetooth |
| La ràdio | només l'anunci | AP+STA, i **mDNS** perquè el telèfon trobi el servei |
| El nom que anuncia | el que diu **la placa** (`PROV_xxxxxx`, dels últims octets del MAC) | el mateix |

- **Qui mana del nom és la placa.** El nom viatja en un buffer que passa el nucli i és el mateix que
  anuncien la ràdio i el QR: si el QR digués un nom i l'anunci un altre, el telèfon no trobaria res.
- **mDNS no és opcional a SoftAP**: si no està engegat, el codi d'Espressif només escriu un avís i
  continua, i el resultat és un telèfon connectat a l'AP que no troba el servei. S'inicialitza abans
  d'obrir l'AP i s'anuncia **després** que l'AP és a dalt.
- **El QR surt al port sèrie** (`lib/qr`, nivell M, `{"ver":"v1","name":…,"pop":…,"transport":…}`) amb
  l'etiqueta de l'app que toca segons el transport. Si el payload no cabés en un símbol, s'escriu el text
  perquè la persona el pugui teclejar.
- **El component es plega en acabar.** `wifi_prov_mgr_deinit()` a `WIFI_PROV_END`: sense això el gestor
  segueix viu amb la ràdio en AP+STA i el prompt no torna. El que queda després és una associació com
  qualsevol, amb el seu termini curt.
- **El Bluetooth s'encén quan algú el demana i s'allibera en acabar**: `bt_ready()` fa el controlador i
  Bluedroid, i l'esquema els deixa anar. Una placa que no es provisiona mai no paga la pila en heap,
  només el codi al binari.
- **El mode del controlador ha de ser el de la configuració**: aquest build és BTDM
  (`CONFIG_BTDM_CTRL_MODE_BTDM=y`) i demanar `ESP_BT_MODE_BLE` amb una configuració BTDM dona
  `ESP_ERR_INVALID_ARG`. El HAL ajusta el camp `mode` de la configuració i després fa
  `esp_bt_controller_enable(ESP_BT_MODE_BLE)`.
- **`btInUse()` el defineix aquest projecte.** `initArduino()` fa
  `if (!btInUse()) esp_bt_controller_mem_release(ESP_BT_MODE_BTDM)`, i al nucli hi ha **dues**
  definicions weak d'`btInUse()` —una que diu que no i una que diu que sí—, o sigui que quina queda al
  binari depenia de l'enllaçador i, amb la que diu que no, la memòria del controlador s'alliberava i
  `esp_bt_controller_init` contestava `ESP_ERR_INVALID_STATE`. El nostre HAL en defineix una de
  **forta** que diu que sí, que és el ganxo que el mateix nucli documenta. El preu és que la memòria del
  controlador queda reservada sempre (docs/75).
- **Una sola feina amb la ràdio, i el controlador no s'ha de quedar a mitges**: quan el gestor ha
  acabat, la ràdio torna a ser del sistema i es diu (`prov: the radio is ours again`).

### NTP

`net.ntp` i `ntp` posen el rellotge amb l'`esp_sntp` de lwip. El client posa el rellotge del sistema ell
mateix i el nucli li diu l'hora al rellotge **una altra vegada** per la via de sempre
(`IClock::set_epoch_ms`), perquè la banda falsa no té lwip i les dues han de fer el mateix camí
(docs/60). Sense xarxa es refusa **a l'acte** amb `E_NONET`: un servidor de temps no contesta si no hi
ha xarxa, i esperar el termini sencer per saber-ho és fer perdre el temps a qui ho demana.

## Una pàgina, amb TLS, sense parar el sistema

`http_get_start` no baixa res: desa l'adreça i aixeca una tasca de **10 240 B** de pila, que és qui fa
la feina. El nucli pregunta `http_poll` a cada torn i dona la resposta com a promesa.

| Qui | Què fa |
|---|---|
| Tasca | `esp_http_client_init` → `open` → `fetch_headers` → llegeix → `close` → publica (o llença, si ningú la vol) |
| Nucli | pregunta l'estat a cada torn, compta el termini, copia el cos i **allibera** |
| Ràdio | es puja perquè la petició porta `needs_network` i es tanca quan la cua es buida |

- **TLS**: `esp_crt_bundle_attach`, el paquet d'autoritats del build
  (`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y`). No hi ha cap certificat escrit al repositori i no hi ha
  manera de desactivar la verificació.
- **Els errors es diuen pel seu nom**: el que és `>= 0x8000` ve de la pila de TLS; els quatre codis que
  són del sòcol i no del certificat (`CANNOT_RESOLVE_HOSTNAME`, `CANNOT_CREATE_SOCKET`,
  `FAILED_CONNECT_TO_HOST`, `CONNECTION_TIMEOUT`) es queden a `kConnect`. La resta és `kTls`, i el nucli
  en fa `E_TLS` en comptes de fer creure que el servidor no hi era.
- **Una pàgina que no cap es refusa, no es talla**: en arribar al sostre es llegeix **un byte més**; si
  el servidor encara en té, l'error és `kTooBig`.
- **Si ningú la vol** (`http_release` amb la feina a mitges, o `wifi stop`), la tasca allibera ella
  mateixa el que havia demanat.
- **`HttpResult` no porta capçaleres**: no les ha demanades ningú i una llista costa memòria (docs/75).

## Energia: son i velocitat

- **Dues fondàries.** `kLight` conserva la memòria, l'intèrpret i la consola i el sistema segueix on era;
  `kDeep` només conserva el que hi ha a la memòria del RTC i la placa **s'arrenca de nou**, i el que hi
  havia corrent torna del registre de sessió (docs/30).
- **Es desperta pel temporitzador del RTC i pel botó.** El rellotge de paret sobreviu al son profund
  perquè l'IDF el desa a la memòria del RTC i nosaltres hi guardem la marca (`RTC_DATA_ATTR`), i
  l'`esp_timer_get_time` segueix comptant, o sigui que en tornar el sistema sap quant s'ha desviat.
- **El RTC d'aquest xip és l'oscil·lador RC intern** (no hi ha cristall de 32 kHz a la placa): el drift
  és real i és un número que s'ha de mesurar (docs/90).
- **El port sèrie no el desperta.** `esp_sleep_enable_uart_wakeup` existeix, però un caràcter que arribi
  mentre dorm es perd; i per això una placa **sense xip de rellotge no s'adorm sola**: una placa sense
  pantalla que s'adorm quan ningú escriu es menjaria les tecles de la persona. Al Paper sí que ho fa,
  i només quan hi ha una alarma pendent (docs/60).
- **La línia s'ha buidat abans de parar el rellotge.** Un `write()` de la consola torna quan el byte és
  al buffer del port, no quan ha sortit pel fil, i els dos sons aturen la UART: sense esperar, l'última
  línia que llegeix una persona arriba **tallada a mitges**. El nucli ho demana a la consola
  (`IConsole::flush()`) just abans de cridar el `sleep` de la placa, sigui quin sigui el so; a l'ESP32
  això és `Serial.flush()`, que espera que la FIFO i el registre de desplaçament quedin buits. És al
  nucli i no a cada placa perquè la consola és del sistema i no del xip.
- **El processador arrenca a 160 MHz** i es pot dur a **240 o a 80**, mai per sota: `setCpuFrequencyMhz`
  del core mou també l'APB i el tick de FreeRTOS, i per sota de 80 l'APB baixa i cap perifèric es
  reconfigura —ni el port sèrie, que és l'única interfície d'aquesta placa. Un valor que no sigui un dels
  passos es refusa, no s'arrodoneix.
- **No hi ha DFS.** `CONFIG_PM_ENABLE` no està activat al framework precompilat, o sigui que pujar i
  baixar sol queda fora i la velocitat és sempre una decisió de qui la demana.

## Apagar tot el que es pot apagar

> **Cap maquinari s'afegeix sense la seva política d'energia.** Dormir és l'única manera d'estalviar
> bateria, i una peça que només se sap encendre està feta a mitges.

Tres preguntes abans d'escriure el driver d'una peça nova, i les respostes van al document de la
placa, no al cap de qui la va escriure:

1. **Té una alimentació que es pot tallar?** S'encén just abans d'usar-la i es torna a apagar de
   seguida; mai encesa «per si de cas».
2. **Té un mode de repòs propi?** (el panell IT8951E té `PON`/`POFF`, el GT911 accepta reset, el
   SHT30 fa una mesura i torna a dormir.) S'hi entra quan el sistema dorm i quan ningú la mira.
3. **Pot despertar el sistema?** Si pot, ha de sortir a `power.caps().wake` i ha de ser una font de
   despert de debò: una promesa que el maquinari no compleix és pitjor que no tenir-ne.

I el que hi ha avui, sense prometre res que no s'hagi mesurat:

| Rail o peça | Estat |
|---|---|
| Main power hold (**GPIO2**) | **Encesa sempre**: sense ella la placa s'apaga. Deixar-la anar és l'últim pas d'**apagar-la** (`poweroff`), i la seqüència sencera és la del fabricant: **GPIO23** (alimentació del panell), **GPIO5** (ports HY2.0), **GPIO2** i son profund del xip. Amb el cable posat el corrent no cau, i per això l'últim pas és el son: la placa calla igualment (mesurat al codi de `M5Paper_FactoryTest`, `Shutdown()`) |
| Alimentació del panell (**GPIO23**) | **Apagada**, i hi queda fins que arribi el driver del panell (docs/90) |
| Alimentació dels ports HY2.0 (**GPIO5**) | **Apagada**, igual |
| Targeta SD | **No té interruptor**: el que hi hagi al slot consumeix mentre la placa és encesa. És un fet de la placa i no una decisió |
| BM8563 (pila de botó) | No la governa el sistema: la pila la manté quan no hi ha corrent. El que consumeix la I2C quan està en repòs **no està mesurat** |
| Llums WS2812 | Un color encès consumeix mentre hi és; el nucli els apaga quan l'app marxa (docs/30) |

## La consola

Els primers bytes que surten per un terminal a 115200 **no són nostres**: abans de `Serial.begin()` el
xip ja ha imprès el que imprimeix la seva ROM (la línia de reset i l'`ets Jun 8 2016…`) a **74880
bauds**, i un terminal a una altra velocitat en llegeix una pila de bytes `0x00`. El que sí que és nostre
és netejar **l'entrada**: `pal::init()` buida el que hagi arribat i el nucli en llença una mica més
abans de llegir la primera ordre, de manera que aquesta brossa no es pot convertir mai en una comanda.

## Interfícies de la PAL

```cpp
enum EventType : uint16_t {  // POD de mida fixa, cap punter de qui
  kEventNone, kEventConsoleRx, kEventConsoleOwner, kEventTimer, kEventAlarmDue,
  kEventPromiseDone, kEventAppExit, kEventPower, kEventSystem, kEventTypeCount
};
struct Event { uint16_t type; uint16_t a; int32_t b; };

class IQueue {
 public:
  virtual bool push(const Event& ev, uint32_t timeout_ms) = 0;  // 0 = prova un cop
  virtual bool pop(Event& out, int32_t timeout_ms) = 0;         // <0 = per sempre
  virtual uint16_t size() const = 0;
  virtual uint16_t capacity() const = 0;
};

class IConsole {
 public:
  virtual size_t write(const void* data, size_t len) = 0;
  virtual size_t read(void* data, size_t len) = 0;   // el que hi hagi ara; no bloqueja
  virtual bool ansi() const = 0;
};

class ISystem {
 public:
  virtual IQueue* queue() = 0;
  virtual IConsole* console() = 0;
  virtual int64_t millis() const = 0;      // monòton, mai enrere
  virtual void reboot() = 0;
  virtual bool reboot_requested() const = 0;
  virtual uint32_t random() = 0;
  virtual HeapInfo heap() const = 0;       // free_bytes, min_free_bytes
};

bool init();  void shutdown();  ISystem& system();
```

La consola de la PAL dona **bytes**, no línies. Qui parteix les línies, qui mira la seqüència de sortida
i qui fa l'eco és el nucli, perquè ho ha de fer igual a totes les bandes.

## Taula de capacitats

| Capacitat | Lite | Matrix | Paper | Emulador | Notes |
|---|---|---|---|---|---|
| `console` | ✓ | ✓ | ✓ | ✓ | UART a 115200 / stdin-stdout cru |
| `ansi` | ✓ | ✓ | ✓ | ✓ al PC | Depèn del terminal que hi hagi a l'altra banda |
| `display` | — | — | — | — | **Reservat**: la interfície hi és i encara no la implementa cap placa (docs/90) |
| `input` | ✓ | ✓ | ✓ | ✓ | Un botó a l'ATOM, tres tecles a la Paper i, al banc, les del model |
| `led` | ✓ | ✓ | — | ✓ al model que en té (no al del Paper) | El Paper no té cap llum |
| `imu` | — | ✓ | — | ✓ al model del Matrix | El Lite no en té i la crida respon `E_UNSUPPORTED` |
| `littlefs` | ✓ | ✓ | ✓ | ✓ | Obligatori: sense ell no hi ha apps |
| `sd` | — | — | ✓ amb targeta | ✓ amb `--sd`, si el model té ranura | Targeta SD per SPI (FATFS), muntada a `/sd` a **10 MHz**. Només s'anuncia si una targeta de debò ha muntat: al Paper quan n'hi ha una al slot, al banc quan se li diu `--sd <dir>` **i** el model imitat la té (el Paper; els ATOM no en tenen) |
| `nvs` | ✓ | ✓ | ✓ | ✓ | Partició NVS / directori de fitxers |
| `rtc` | ✓ | ✓ | ✓ | ✓ | Dona l'hora mentre hi ha corrent. **Al Paper és de debò**: la BM8563 amb pila de botó es llegeix a l'arrencada i s'hi escriu a cada `set_epoch`, així que `time.caps().source` diu `"rtc"` i l'hora sobreviu a treure el cable (docs/60). A l'ATOM i al Matrix és el **domini RTC del xip**, que la perd |
| `rtc_wake` | ✓ | ✓ | ✓ | ✓ | Despertar pel temporitzador del RTC (i pel botó, a l'ATOM) |
| `net_wifi` | ✓ | ✓ | ✓ | ✓ | La ràdio de debò / la de mentida (docs/85); la del Paper és la del xip i **està provada a la placa** (31/10) |
| `net_http` | ✓ | ✓ | ✓ | ✓ | `esp_http_client` amb TLS / la pàgina falsa |
| `net_prov` | ✓ | ✓ | ✓ | ✓ | El component d'Espressif / l'AP de mentida |
| `battery` | — | — | ✓ | ✓ amb `--battery-mv`, si el model té indicador | El Paper la llegeix del divisor de GPIO35 i el banc del número que se li diu, **i només si el model imitat en té** (el Paper; els ATOM no). Només s'anuncia quan la lectura és un número que pot ser una cel·la; `charging` és **fals sempre** perquè cap d'aquestes bandes no té un pin per preguntar-ho (docs/30) |
| `sleep_light` | ✓ | ✓ | ✓ | de mentida | Al PC el rellotge salta en comptes d'esperar |
| `sleep_deep` | ✓ | ✓ | ✓ | de mentida | A la placa, tornar vol dir arrencar |

`caps` i `sys.caps()` diuen el mateix i surten d'aquesta taula. Cada placa d'ESP32 **parteix de `esp32_caps()`** (10 de 18: consola, ANSI, NVS, rellotge, despert del RTC, els dos sons i la ràdio amb provisioning i pàgina) i afegeix només el que té de seu: el sistema de fitxers que munta, i doncs **13 de 18** al Lite, **14 de 18** al Matrix (que hi afegeix `imu`), **14 de 19** al Paper amb el SHT30 contestant i el vidre, i sense bateria ni targeta —**15** amb la bateria llegida **o** la targeta, **16** amb totes dues, que és el màxim d'aquesta placa— i **el que diu el model** a l'emulador: **13 de 19** al de l'ATOM (**12** si el terminal no és ANSI), **14** al del Matrix i **14** al del Paper (que ja hi té la targeta `emulator/sd`), que puja a **15** amb `--battery-mv`. L'única que no té ningú és `display`.

## Fitxa: M5 ATOM i ATOM Matrix

| | |
|---|---|
| Nom (`sys.board`) | `m5stack-atom` / `m5stack-atom-matrix` |
| Xip | ESP32-PICO-D4, 240 MHz, arrenca a 160 |
| Flash | 4 MB |
| RAM | 320 KB, **sense PSRAM** |
| Pantalla | No |
| Entrada | Un botó a **GPIO39** (entrada només, la resistència la porta la placa) amb 10 ms de rebot |
| Llum | Lite: un **WS2812** a **GPIO27**. Matrix: **25 WS2812C de 2020** al mateix pin, sostre 20 |
| Sensor | Matrix: **MPU6886** a la I2C interna (G25/G21, 0x68). Lite: cap |
| Expansió | Un port Grove I2C (G26/G32) |
| Rellotge | Només el del SoC; el RTC del son és l'RC intern |
| Consola | Pont USB-sèrie, `/dev/ttyUSB*` |

Conseqüències que aquesta placa imposa:

- És el **pitjor cas de memòria**: tot el que es mesuri aquí és el terra del sistema (docs/75).
- És el **cas sense pantalla**: tots els camins de «capacitat absent» es proven aquí primer.
- **El Grove és I2C lliure**: si cal provar el camí d'un xip RTC de debò, s'hi pot posar un mòdul sense
  canviar de placa.

## Fitxa: M5 Paper v1.0 i v1.1

| | |
|---|---|
| Nom (`sys.board`) | `m5stack-paper` (les dues versions, un sol firmware: vegeu a sota) |
| Entorn | `paper`, amb la definició de placa del projecte a `boards/m5stack-paper.json` |
| Xip | ESP32-D0WDQ6-V3, 240 MHz, **8 MB de PSRAM** |
| Flash | **16 MB**, amb la taula de `partitions_16mb.csv`: 4 MB d'aplicació i 11,9 MB de disc |
| Pantalla | **E-ink de 4,7 polzades, 960 × 540** (540 × 960 en vertical), **16 nivells de gris**, controlador **IT8951E** per SPI |
| Entrada | **Tres tecles** (esquerra, central `PWR`, dreta) **i panell tàctil GT911** per I2C |
| Sensor | SHT30 (temperatura i humitat) i una EEPROM FM24C02 |
| Rellotge | **BM8563** amb pila de botó, a la I2C interna (0x51): es llegeix a l'arrencada i s'hi escriu l'hora a cada canvi d'època, o sigui que **sobreviu a treure el cable**. El seu `INT` **no arriba a cap GPIO** —al mapa del fabricant només el té la GT911, a GPIO36—: arriba al **circuit d'alimentació** (el SY7088), i per això la seva alarma és el que **torna a encendre la placa** quan s'ha apagat del tot (vegeu «Apagar-se de debò», a sota) |
| Bateria | 1150 mAh, amb divisor a **GPIO35** (×2): es llegeix (`esp32_battery`, ADC1 canal 7) i el % surt de la corba de `hal/battery.h`. **Pendent de creuar amb un multímetre**, que és l'única cosa que diu si el divisor i l'ADC diuen la veritat. De mà: `run battery` |
| Expansió | Tres ports HY2.0-4P (A: G25/G32, B: G26/G33, C: G18/G19) i una ranura microSD |
| Consola | Pont USB-sèrie: **CP2104 o CH9102** segons la unitat (vegeu el bloc de sota) |

Pins que el HAL fa servir, i que són els de la llibreria del fabricant (`M5EPD.h`):

| Què | Pin |
|---|---|
| **Main power hold** (s'ha de mantenir alt o la placa s'apaga) | **GPIO2** |
| EPD: CS, SCK, MOSI, MISO, BUSY | 15, 14, 12, 13, 27 |
| Alimentació del panell / dels ports HY2.0 | GPIO23 / GPIO5 |
| microSD: CS (comparteix SCK/MOSI/MISO amb el panell) | GPIO4 |
| I2C intern: SDA, SCL | GPIO21, GPIO22 |
| GT911 (0x14) amb INT a GPIO36 · SHT30 (0x44) · BM8563 (0x51) · FM24C02 (0x50) | al mateix bus |

### El tàctil: un GT911 que parla en registres de dos bytes (1/11)

El que se sap del xip, i d'on surt: del **mapa de registres** que fan servir les dues bases de codi del
fabricant —el `Touch_GT911` de `M5GFX` (el que M5 manté) i l'`esp_lcd_touch_gt911` d'Espressif—, que
coincideixen entre ells i amb el que la placa ja ha dit:

| Què | Valor |
|---|---|
| Adreça | **0x14** o **0x5D**, triada pel nivell de la línia `INT` durant el reset. Aquí `INT` és **GPIO36, que és una entrada**: el firmware no pot triar-la, i per això el driver **pregunta les dues** i es queda la que contesta. La pregunta és `II2c::present(address)` —una lectura d'un byte que no diu res— i no una lectura del producte id: preguntar a l'adreça on no hi ha ningú **no és una avaria** i no ha d'escriure cap línia d'error a l'arrencada (era `i2c: read of 14:8140 failed (ESP_FAIL)` fins a la 0.13.0) |
| Identitat | `0x8140`, tres caràcters: `9`, `1`, `1` (i un zero). Un xip que contesta una altra cosa és un altre aparell als mateixos fils |
| Registres | De **dos bytes** (`0x814E` és un estat, no un byte): el bus del HAL té una lectura de registre de 16 bits per a això (`II2c::read16`) |
| La configuració | `0x8047`, **186 bytes** i un checksum al final. La versió és el primer byte, els dos màxims (el marc) a `0x8048` i `0x804A`, i **quants dits** a `0x804C` |
| Un frame | L'estat a `0x814E` —bit 7 = «hi ha frame», bits 3:0 = quants dits— i fins a **cinc** punts de **vuit bytes** des de `0x814F`: `id` de traça, x, y, **mida** del contacte i un byte reservat |
| Per tancar el frame | **S'escriu un zero a `0x814E`**. Sense això el mateix dit es llegeix per sempre més, que és la manera més fàcil de fer una app que no respira |
| Dits alhora | El xip en pot reportar **cinc**, però **mana la seva configuració**: la unitat de la Paper diu **2** (`0x804C`, config 66, mesurat l'1/11), que és el «two-point touch» que anuncia el fabricant. El driver llegeix aquest número i el que no passarà mai és el que una app ha de rebre |
| «Pressió» | El camp de **mida** és l'àrea que el xip mesura sota el dit, no una pressió: dos dits que premen igual de fort donen dues mides diferents. S'exposa com a `size` i no s'inventa una escala |
| Velocitat | És un valor de la configuració del xip; el `M5GFX` el llegeix de `0x8056` i en fa `5 + (byte & 0x0F)` Hz. **No es dóna per bo**: es llegeix i es mesura al xip (docs/95, E-6) |
| Gestos | El xip en té de propis, i **ni el driver de M5 ni el d'Espressif els llegeixen**. Aquí tampoc: un toc, una premuda llarga, un arrossegament o dos dits són **formes del flux de punts**, volen dir el mateix a qualsevol vidre i es poden provar al PC amb un fals; un gest que inventa el xip només el pot fer servir una placa (docs/85) |

**El que hi ha escrit, l'1/11**: `ITouch` al HAL, el driver sencer (les dues adreces, la configuració,
el marc dels cinc punts, el zero de tancament), **les tres crides de Lua** (`touch.caps`, `touch.read`,
`touch.gesture`), els gestos fets pel nucli (`tap`, `double`, `triple`, `long`, `drag` amb un dit o
amb dos, `two` i `pinch`, amb la mateixa finestra que el botó), l'app `glass` i la línia de l'arrencada —`touch: GT911 at 0x14, config N, 540x960, 5
fingers`— que diu què és aquesta unitat sense un oscil·loscopi. **La capacitat s'anuncia** quan el xip
contesta, perquè ara una app ho pot fer servir. **El que falta**: dos mesures al xip —**en quin marc
compta** (es toquen les quatre cantonades) i **a quina velocitat mostreja** (docs/95, E-6)—, la
**rotació compartida amb el panell** —una sola rotació per al dibuix i per al toc, amb els `x`/`y` i
els `dx`/`dy` dels gestos convertits en un sol lloc (docs/90)— i, del pas 8, la política d'energia de la
I2C i de les alimentacions.

### El bus ha de deixar que el xip aguanti el rellotge (mesurat l'1/11)

Un xip de la I2C pot **aguantar la línia del rellotge a baix** mentre pensa, i això és
clock stretching: el mestre ha d'esperar i no pot comptar-ho com una avaria. El driver
d'aquest IDF (**v4.4.6**, el que enllaça el build; ho diu `tools/sdk/versions.txt` del
paquet del framework) hi posa un sostre per defecte a `i2c_ll_cal_bus_clk`:

```c
clk_cal->tout = half_cycle * 20;   // deu cicles de bus
```

A 100 kHz i 80 MHz d'APB són **8 000 cicles = 100 µs**. El SHT30, amb clock stretching i
repetibilitat mitjana, aguanta el rellotge fins a **6 ms**: cinquanta vegades més. La
conseqüència, a la placa:

```
i2c: read of 44 failed (ESP_ERR_TIMEOUT)
  env  no
```

i, a més, el driver **reseteja el bus sencer** quan això passa (`i2c_hw_fsm_reset`), que és
per què la resta de xips de la I2C (el rellotge, la FRAM) van seguir funcionant i el sensor
no hi era enlloc. La 0.10.1 fixa el sostre al **màxim del registre**, `0xFFFFF` cicles =
**13,1 ms a 80 MHz**, amb `i2c_set_timeout()` **després** de `i2c_param_config()` (que és qui
escriu el sostre de deu cicles). Dues notes que això deixa lligades:

- **la repetibilitat mitjana és el que hi cap**: l'alta és de **15 ms** i no passaria del
  sostre; per això el driver demana `0x2C 0x0D` i no `0x2C 0x06` (docs/50, `esp32/sht30.h`);
- **el sostre se sobreviu a un reset de bus**: `i2c_hw_fsm_reset` desa i torna a escriure el
  temps límit, o sigui que un timeout no el deixa baix per sempre.

**Comprovat a la placa l'1/11 amb la 0.10.1**: `caps` diu `env yes` i `run air` dona
`air 21.1 C 63.5 %` dues vegades seguides, sense cap línia `i2c:` al log (docs/95, E-5).


### El CS no es queda baix sense el bus bloquejat (mesurat l'1/11)

El panell no és un dispositiu qualsevol del bus de la targeta: la seva trama és **un prefix i el
seu mot amb el CS baix entre tots dos**, i el controlador vol mirar la línia BUSY entremig. Amb
`spi_device_transmit()` això es fa amb `SPI_TRANS_CS_KEEP_ACTIVE`, i l'IDF **el refusa** si el
dispositiu no ha bloquejat el bus abans (`spi_master.c` de la v4.4.6, dins `spi_device_queue_trans`):

```c
/* Even when using interrupt transfer, the CS can only be kept activated if the bus has been
 * acquired with `spi_device_acquire_bus()` first. */
if (host->device_acquiring_lock != handle && (trans_desc->flags & SPI_TRANS_CS_KEEP_ACTIVE)) {
    return ESP_ERR_INVALID_ARG;
}
```

La conseqüència a la placa és una línia que enganya, perquè parla del bus i no del que passa:

```
panel: SYS_RUN did not go out: bus refused the prefix (busy reads 1) (no panel)
```

El `busy reads 1` és la meitat bona de la notícia —el panell hi és— i el `refused the prefix` és
`ESP_ERR_INVALID_ARG` per la regla de dalt. El driver ara fa el mateix que el de la targeta SD:
`spi_device_acquire_bus(handle, portMAX_DELAY)` abans de la primera transacció que ha de deixar el CS
baix, i `spi_device_release_bus()` quan la trama s'ha acabat. Tres coses més que la placa ha ensenyat
el mateix dia:

- **el rail del panell vol un segon**: `M5EPD::begin` encén GPIO23 i espera **1000 ms** abans del
  primer mot. Amb 100 ms la línia BUSY encara llegeix baixa i el handshake es rendeix a `SYS_RUN`
  (`M5EPD_Driver::begin` no prem cap reset: el seu `rst` val `-1` de sèrie);
- **un sol preàmbul de dades per trama, i després els bytes tal com són**: el fabricant posa un
  `0x0000` davant de cada mot (`write32(0x0000FFFF)` a `Clear()`), però **aixeca el CS a cada parell de
  mots** i el controlador es resincronitza en aquell flanc. Amb el CS aguantat tota la fila —que és el
  que fa aquest driver— un segon preàmbul es llegeix com dos píxels més, la fila desborda la següent i
  la pàgina surt **torçada** (mesurat a la placa l'1/11, docs/95 E-7). Dins d'una trama: un `0x0000` i
  el raig de bytes de la superfície, sense res entremig, en trossos de fins a 2000 bytes (el sostre del
  bus són 4000);
- **cada pas diu el seu nom**: les quatre maneres de fallar el handshake (busy baixa, bus que no
  bloqueja, escriptura refusada, lectura que no torna) donen quatre línies diferents, perquè endevinar
  entre elles costa un flashatge cada cop.

**I les adreces dels registres són sumes, no números.** `utility/IT8951_Defines.h` del fabricant les
escriu com a `IT8951_MCSR_BASE_ADDR + 0x0008` i `IT8951_DISPLAY_REG_BASE + 0x0224`, i això dona
**`LISAR` a 0x0208 (mot baix) i 0x020A (mot alt)** i **`LUTAFSR` a 0x1224**. Escriure-hi `0x0200` —que
és `IT8951_MCSR`, un registre d'estat del convertidor de memòria— deixa la imatge a l'adreça per
defecte del controlador mentre `DPY_AREA` en pinta una altra, i al vidre surt una pàgina **torçada**
(mesurat a la placa l'1/11, docs/95 E-7).


### Què en fa el sistema, avui (1/11)

| Peça | Estat | Què falta |
|---|---|---|
| ESP32-D0WDQ6-V3, 240 MHz, 520 KB de SRAM | **fet**: consola, disc, NVS, xarxa, son | — |
| 16 MB de flash | **fet**: 2 108 973 B (50,3 %, mesurat amb la 0.13.0 l'1/11) i 11,9 MB de disc | — |
| **8 MB de PSRAM** (4 MB mapejats) | **fet de codi i mesurat a la placa** (31/10): `extra_memory()` a la plataforma torna els tres números (xip, adreçable, lliure) i `pal::IExtraMemory` assigna a la PSRAM; l'intèrpret hi posa els blocs de 4 KB en amunt i el sostre de les apps és la PSRAM **mapejada** (4 096 KB). `mem` diu `extra memory 4067 of 4095 KB usable, of 8192 KB on the chip (psram)` i `sys.heap()` en dona els bytes. Els altres 4 MB necessiten el `himem` amb bancs i **no poden ser transparents** (docs/90) | comprovar que una app d'1 MB hi cap (docs/95, E-4) |
| **Panell e-ink ED047TC1/ET047TC1 + IT8951E** (SPI 15/14/12/13, BUSY 27, alimentació GPIO23) | **fet de codi i amb crida de Lua** (1/11): `ISurface` al HAL (una superfície de 4 bits amb el rectangle brut i un `flush` que no espera), el driver a `esp32/it8951.h` (trames de 16 bits, `GetDeviceInfo`, el bus de la targeta a **10 MHz** —la del fabricant— fins que el handshake es vegi al vidre, amb un sostre de 26,7 MHz per la matriu de GPIO, mesurat l'1/11—, els vuit modes) i `display.caps`/`clear`/`rect`/`pixel`/`flush`/`idle` a Lua. La capacitat **s'anuncia** quan el controlador contesta. **El sistema no dibuixa res mai** | **fet** (1/11, 0.13.0): la imatge arriba i surt recta; falten el sentit dels nivells i el temps d'un `gc16` (docs/95, E-7), després el **text** (una font) i la política d'encesa de GPIO23 |
| **GT911**, tàctil de dos dits amb gestos (I2C 0x14 o 0x5D, INT GPIO36) | **fet de codi i amb crida de Lua** (1/11): `ITouch` al HAL, el driver a `esp32/gt911.h` (les dues adreces, la configuració de 186 bytes, el marc de cinc punts i el zero que el tanca), `touch.caps`/`read`/`gesture` a Lua i l'app `glass`; el sistema en fa `tap`, `double`, `triple`, `long`, `drag` (amb un dit o dos), `two` i `pinch`. La capacitat **s'anuncia** quan el xip contesta (vegeu «El tàctil», a sota) | comprovar al xip que el marc i els gestos quadren: tocar les quatre cantonades i contar frames amb el dit (docs/95, E-6) i, més endavant, la política d'energia del pas 8 |
| **SHT30** (I2C 0x44) | **fet i provat al xip** (1/11): `ISensor` al HAL (`read(celsius, humidity)`), el driver a `esp32/sht30.h` —la comanda, els dos CRCs i les dues fórmules del full de dades— i `env.read()` des de Lua. La capacitat s'anuncia **només si una mesura sencera arriba amb els dos CRCs bons**. El primer enviament a la placa (1/11, 0.10.0) va dir `env no` amb `i2c: read of 44 failed (ESP_ERR_TIMEOUT)`: **el bus només deixava 100 µs de clock stretching** i el xip n'aguanta sis mil·lisegons (vegeu «El bus ha de deixar…», a sota); amb la 0.10.1 la placa diu **`env yes`** i `run air` dona `air 21.1 C 63.5 %` (docs/95, E-5) | que la temperatura **quadri amb un termòmetre** de la mateixa sala (docs/95, E-5) |
| **FM24C02**, 256 B (I2C 0x50) | **fet** (31/10): `esp32/fm24c02.h`, un `IStorage` que **és el xip** —llegeix i escriu per la I2C, sense còpia a RAM— muntat a `/fram`, i el banc el té com a `fram.bin` (`Model::fram`) | provar-ho al xip: que contesti a 0x50 i que els bytes hi siguin amb la bateria treta (docs/95) |
| **BM8563** amb pila (I2C 0x51) i la seva alarma al SY7088 | **fet**: l'hora, el despert, i `poweroff` | les dues proves de foc (docs/95, A-2) |
| **SY7088** i les tres alimentacions (hold GPIO2, EPD GPIO23, ports GPIO5) | **a mitges**: el hold es manté i es deixa anar, i les altres dues **es tanquen** al `poweroff` | **encendre-les només mentre s'usen** (docs/90 pas 8): és la política que estalvia corrent |
| Bateria de 1150 mAh (ADC GPIO35 ×2) | **fet de codi** | la mesura amb el multímetre (docs/95, B-1) |
| Dial de tres tecles (37/38/39) | **fet** | comprovar quina és `up` (docs/95, E-1) |
| microSD (CS 4, bus compartit amb el panell, 10 MHz) | **fet i provat** | si 20 MHz també hi van (docs/95, C-2) |
| **3 ports HY2.0-4P**: A G25/G32 (I2C), B G26/G33 (DAC/ADC), C G18/G19 (UART) | **res**: no hi ha cap manera que una app toqui un GPIO | **una crida de Lua** (`gpio.*`) o res: és una decisió de política, no un driver |
| Pont USB-sèrie CP2104 o CH9102 | **fet** | la latència (docs/95, D-1) |
| Ràdio 2,4 GHz (Wi-Fi i BLE) | **fet**: `wifi`, provisioning, `ntp`, `http` | el `net.softap`, que ningú no ha demanat |
| **No hi ha** llum, ni sensor inercial, ni altaveu | **correcte**: `kCapLed` i `kCapImu` no s'anuncien mai en aquesta placa | — |

És a dir: **17 de les 19 capacitats**, i l'única que falta a aquesta placa és `imu`, que no hi és.
| Tecles: dreta, central (`PWR`), esquerra | GPIO39, 38, 37 |

### Apagar-se de debò: el son profund d'aquesta placa no és un son

El mapa de pins del fabricant té una columna `INT` a la taula de la I2C interna, i només la té
plena la GT911 (GPIO36). La BM8563 hi té una barra, i durant un temps aquí es va llegir com «la seva
alarma no es pot fer servir». **És fals a mitges**: no arriba a un GPIO, però sí al circuit
d'alimentació. La llibreria del fabricant ho diu sense embuts:

```
M5EPD::shutdown(int seconds)  =  RTC.clearIRQ(); RTC.setAlarmIRQ(seconds); disableMainPower();
```

`disableMainPower()` és **baixar GPIO2**, el mateix pin que el HAL posa alt perquè la placa no
s'apagui sola. O sigui que:

- **`sleep deep` aquí apaga la placa:** s'arma el xip (`Bm8563::arm_at`, amb el compte enrere o
  l'alarma de rellotge segons què arribi més lluny i amb més exactitud), es baixa GPIO2, i la placa es
  queda sense corrent. La pila de botó manté el rellotge comptant i el seu `INT` torna a encendre el
  SY7088 a l'hora demanada: el pin és de **drenatge obert**, o sigui que amb la placa sense corrent
  encara pot baixar la línia, que és exactament el que el circuit d'alimentació necessita. És el mínim
  consum que aquesta placa pot fer, i és el que demana la regla d'energia del README.
- **Amb el cable posat la placa no es pot apagar** (ho diu el fabricant): baixar GPIO2 no la fa caure,
  i el hold es torna a **pujar de seguida** perquè el son que ve és el del xip i la placa no ha de
  quedar penjada del cable per seguir encesa —un cable que parpelleja és una placa que torna sola i
  que no arriba mai a la seva hora—. Cau al son profund del xip, que desperta amb el seu
  temporitzador. No es perd res: l'hora és al xip.
- **L'hora del xip s'alinea abans d'armar-lo.** Mentre la placa va, ningú no escriu el xip (només
  s'hi escriu quan algú posa l'època), o sigui que pot estar dies enrere de l'hora que el sistema
  dona a les apps. Com que la data demanada s'ha expressat en l'hora del sistema, s'hi escriu
  aquella hora abans d'armar: sinó la placa es quedaria apagada el temps equivocat.
- **Tres guardes abans de tallar el corrent**, i cada una és una placa que no tornaria: que hi hagi
  una hora per escriure-hi (una placa que ningú no ha posat no pot armar cap espera, i una pila morta
  no sonaria mai), que l'espera sigui **menys d'un dia** (més enllà, el xip no sap expressar-la), i
  que hi hagi alguna cosa a esperar.
- **El xip té dues maneres d'avisar, i s'usa la més exacta que arribi** (`hal/alarm.h`):
  - el **comptador de compte enrere** (`0x0E` + `0x0F`), amb un tic per segon fins a **255 s** i un
    tic per minut fins a **255 minuts** (4 h 15 min) — és el que fa que una alarma a 30 s soni al
    segon i no al minut;
  - l'**alarma de rellotge** (`0x09`–`0x0C`, hora i minut) per a esperes de més de quatre hores, que
    s'arrodoneix **cap amunt**: despertar-se abans d'hora faria que la placa tornés a armar la
    mateixa hora i minut, i això és l'endemà;
  - i per a una espera **de més d'un dia** no hi ha res més, però tampoc no cal: s'arma la primera
    coincidència, la placa es desperta abans d'hora, troba que no toca i **torna a armar**. El quars
    compta igual i el preu és un arrencada de més per dia, que és menys que tenir la placa encesa
    esperant. **L'app no es dona per desperta en una tanda**: el nucli desa l'hora que falta, no la
    restaura i s'apaga un altre cop (docs/60).
- **Qui decideix apagar-se no és una app: és el nucli.** A cada torn, `kernel::power` mira si hi ha
  una alarma pendent i si ningú no fa servir la placa —deu segons de consola en silenci, cap app
  oberta i cap `keep_awake`— i, si l'espera és de **deu segons o més**, s'apaga fins a aquella
  alarma. Amb menys de deu segons es queda despert: un despert d'una apagada és un arrencada, i no
  val la pena per a nou segons. `power idle off` ho desactiva mentre una persona treballa al prompt.
- **Qui va despertar-ho**: després d'una apagada, un despert per alarma i una premuda del botó `PWR`
  són el mateix arrencada. L'única cosa que els distingeix és el bit `AF` del xip, que es llegeix
  **abans** de desarmar-lo (`RtcClock::woke_by_alarm`). Avui es llegeix i es desa; ensenyar-ho al
  sistema és feina pendent (docs/90).

El que encara no està provat a la placa: que el corrent caigui de debò i que l'alarma el torni a
pujar. És la primera cosa de la llista de mesures de `docs/90`.

### `poweroff`: apagar sense tornar

`sleep deep` apaga la placa **perquè torni**; `poweroff` l'apaga i no arma res. La seqüència no és
només baixar GPIO2, i és la que fa servir el firmware de fàbrica del fabricant
(`M5Paper_FactoryTest`, `Shutdown()` a `global_setting.cpp`):

```
fflush  →  espera 600 ms  →  GPIO23 = 0  →  GPIO5 = 0  →  GPIO2 = 0  →  esp_deep_sleep_start()
```

Les dues primeres són **l'alimentació del panell** i la dels **ports HY2.0**; l'última és el xip
posat a dormir, perquè amb el cable posat el corrent no cau i la placa ha de callar igualment. La
`M5EPD::shutdown()` de la llibreria, la que no té hora, només fa `disableMainPower()`.

L'única cosa que s'hi afegeix és **desarmar la BM8563 abans**: la `shutdown()` de la llibreria no
mira l'alarma, i una que vingués d'un son anterior tornaria a encendre una placa que hom ha demanat
apagada. La seva pròpia `shutdown(seconds)` sí que comença amb `RTC.clearIRQ()`, o sigui que la
llibreria també ho fa quan pensa en el rellotge.

I com que el son profund no té cap font de despertar, **l'única manera de tornar-la a encendre és el
botó** (o treure i posar el cable).

El **CS del panell (GPIO15) s'ha de posar alt abans de tocar el bus**: és un pin de *strapping* que
la placa porta a baix, o sigui que l'IT8951E arrenca **seleccionat** i comparteix MISO amb la targeta.
El HAL el deselecciona a l'arrencada (docs/90, pas 7).

**El que canvia entre v1.0 i v1.1**, i és tot:

| | v1.0 (SKU K049, descatalogada) | v1.1 (SKU K049-B) |
|---|---|---|
| Panell | **ED047TC1**, rígid | **ET047TC1**, flexible |
| Pont USB-sèrie | **CP2104** | **CP2104 o CH9102** (el fabricant posa l'un o l'altre) |
| Pes | 87,1 g | 81,6 g |
| Consum anunciat en repòs (`RTC` sol) | — | 3,7 V a **10 µA** |

**I el que no canvia**: el xip, la flash, la PSRAM, el controlador del panell (**IT8951E** als dos), el
toc, el rellotge, el sensor, l'EEPROM, la bateria i **tot el mapa de pins**. Per això hi ha **un sol
entorn i un sol firmware**: la diferència del panell viu dins de la flash del propi IT8951 (la forma
d'ona i el VCOM els té ell) i la del pont USB-sèrie és un controlador del PC, no una línia de codi.
L'única cosa que s'ha de vigilar és la del pont: si el port no apareix, és el driver del CP2104 o del
CH9102, no el firmware.

**Què hi ha avui en aquesta placa** (docs/90): consola, LittleFS, NVS, **el rellotge BM8563 amb pila**
(l'única hora que sobreviu a treure el cable), les dues fondúries de son amb el despert pel
temporitzador del RTC, la velocitat del processador, **les tres tecles** (docs/30), **la ràdio** —la
mateixa de l'ATOM, perquè és la del xip: `wifi`, `provision`, `ntp` i `http`—, **provada en aquest
maquinari i funcionant** (31/10), **la targeta microSD** quan n'hi ha una al slot (SPI: CS 4,
compartint SCK/MOSI/MISO amb el panell) i **la bateria**, que es llegeix del divisor de GPIO35.
**No hi és encara** el panell (IT8951E) ni el toc (GT911): la taula de capacitats ho diu en comptes
de prometre-ho. I dues coses estan escrites però **no verificades en aquest maquinari**: que el
`mv` de la bateria quadri amb un multímetre, i que l'hora segueixi allà després de desendollar-lo.

**Què està provat i què no**: `pio run -e paper` **compila net** (zero avisos propis, docs/75) i el
binari que en surt porta la identitat del Paper i cap de les de l'ATOM. **S'ha flashejat i arrenca**
(31/10 amb la 0.6.0 i l'1/11 amb la 0.10.1): el banner diu `lunokOS <versió>  board m5stack-paper`, el
prompt respon, `caps` dona **15 de 18** amb bateria i targeta dins (12 de 18 amb la 0.6.0, que encara
no tenia ni el sensor ni el `led`), `mounts` llista `/sd` amb **15 193 MB**, `ls sd` el llegeix, `cfg` diu la
targeta, `run air` dona temperatura i humitat, i **la ràdio funciona**: l'`INet` d'aquesta placa està
provat en aquest maquinari (31/10).
El que **encara no s'ha vist** en aquest maquinari és el panell i el toc; el que s'ha vist i s'ha
verificat és a la llista de dalt, amb el que queda pendent dit allà mateix.

**El pont USB-sèrie: quin és, i com saber-ho.** És un xip que hi ha entre l'USB i la UART del ESP32, i
**el firmware no el pot veure**: no hi ha cap línia de dades entre ells més enllà de TX i RX, ni I2C ni
cap GPIO que el connecti al xip. Des del PC sí que es veu, i és el que decideix tres coses:

| | CP2104 (Silicon Labs, `10c4`) | CH9102 (WCH, `1a86`) |
|---|---|---|
| Driver de Linux | `cp210x`, **al nucli des de sempre** | **CDC-ACM** (`cdc_acm`, al nucli també), o el `ch343` de WCH |
| El port | `/dev/ttyUSB0` | `/dev/ttyACM0` (o `ttyCH343USB0` amb el driver de WCH) |
| Velocitat màxima | 2 Mbaud | 4 Mbaud |

A 115 200 i a 460 800 —les que fem servir— els dos van sobradament, i el circuit de reseteig per
DTR/RTS és el mateix. La diferència pràctica és **el nom del port** i **el driver**: si la placa no
apareix o una pujada de firmware peta, és això i no el firmware.

Per saber-ho: `tools/serial_bridge.py` ho diu d'una línia, sense obrir el port,

```
$ tools/serial_bridge.py
/dev/ttyUSB0: Silicon Labs CP2104 (10c4:ea60) driver cp210x
```

o a mà, `lsusb` (`10c4:ea60` és el CP2104 i `1a86:…` un WCH) i `ls -l /dev/serial/by-id/`, que dona
el nom del pont i el número de sèrie de la unitat.

**Una discrepància que s'ha de comprovar a la placa**: la llibreria del fabricant té els noms de les
tecles en dos llocs i no diuen el mateix —`M5EPD.h` defineix `KEY_RIGHT_PIN 39` i `KEY_LEFT_PIN 37`,
mentre que el README de la mateixa llibreria diu dreta=37 i esquerra=39—. El codi que corre és el
capçalari, i és el que s'ha de creure; però el primer que cal fer amb la placa a la mà és prémer cada
tecla i veure quin GPIO es tanca.

## Fitxa: emulador (PC)

| | |
|---|---|
| Nom (`sys.board`) | El model que se li ha dit que sigui: `m5stack-atom` (per defecte), `m5stack-atom-matrix` o `m5stack-paper`. Mai `emulator`, que no és cap placa (docs/85) |
| Plataforma | POSIX (Linux) |
| RAM | La del PC, però **limitada al mateix sostre que la placa** |
| Fitxers | Un directori del disc (`emulator/`) amb `littlefs/` i `nvs/`, i una targeta amb `--sd` |
| Rellotge | El del PC, o virtual (`--time-scale`, `--now`) |
| Consola | stdin/stdout en mode cru |
| Xarxa, energia i sensor | **De mentida i ho diuen** (docs/85) |
| Pantalla | No |

Els detalls de cada mentida són a `85 · Emulador`.

## Afegir una placa

1. Si la placa comparteix xip amb una altra (com el Matrix amb el Lite), no es copia res: es fa servir
   `lib/hal_esp32/` i el que queda és el que la placa decideix. Si no comparteix HAL amb cap altra,
   `lib/hal_<id>/` amb el seu `library.json` i les seves dependències al `library.json`.
2. Escriure `lib/board_<id>/include/board/identity.h` amb el nom, la família, `emulator()` a `false`,
   els llums que té de debò i si porta sensor, i `impl.h` amb les capçaleres d'aquesta placa i de la seva
   PAL.
3. Implementar `IPlatform` i les interfícies que la placa tingui de debò. Les que no, `nullptr`.
4. Afegir un `[env:<id>]` a `platformio.ini` amb el seu `-Ilib/board_<id>/include`, el seu `lib_ignore`
   (**totes** les altres identitats) i la PAL que correspongui. Si PlatformIO no coneix la placa, es
   descriu a `boards/<id>.json` i el `[platformio]` ja porta `boards_dir = boards`: així la definició
   viatja amb el projecte i no depèn de la versió de la plataforma (el Paper és aquest cas).
5. Afegir la fitxa en aquest document i la fila a la taula de capacitats.
6. Dir la veritat a la fitxa: què s'ha provat i què no. Una placa que només compila no «funciona».

## Regles per a qui escriu un controlador

- **Mai** cridar `lua_*`: el HAL no sap que hi hagi un intèrpret.
- **Mai** bloquejar la tasca del nucli més del que costa una operació de maquinari; el que triga va a una
  tasca auxiliar i torna com a esdeveniment o promesa.
- **Mai** reservar memòria dinàmica: buffers fixos, i si no n'hi ha, error.
- Els errors es tornen com a valor, no s'amaguen.
- Una lectura de maquinari no es repeteix en bucle dins d'una crida: si el xip no respon, es diu.
- Un controlador no decideix **què** s'ha de fer amb el maquinari; això és del nucli. El driver diu què
  pot fer i quant costa.
