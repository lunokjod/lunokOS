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
| `IClock` | hora de paret, rellotge monòton, **un sol** despertador | ✓ | ✓ | ✓ (del SoC) | ✓ |
| `IStorage` | un volum muntat: stat, list, open, read, write, seek, close, remove, mkdir, rename, space | ✓ | ✓ | ✓ | ✓ |
| `IKeyValue` | claus i valors, exposats com a fitxers a `/nvs` | ✓ | ✓ | ✓ | ✓ |
| `INet` | la ràdio: connectar, escanejar, provisioning, NTP i una pàgina | ✓ | ✓ | **pendent** | de mentida |
| `IPower` | bateria, son, velocitat del processador | ✓ | ✓ | ✓ (sense bateria) | de mentida |
| `ILed` | un llum o una tira: quants n'hi ha, el sostre del canal, pintar | 1 | 25 | — (no en té) | 25 |
| `IInput` | el botó: si és premut i quant fa | ✓ | ✓ | **pendent** (3 tecles) | ✓ |
| `IImu` | acceleració, gir i temperatura del xip inercial | — | ✓ | — | de mentida |
| `IDisplay` | la pantalla | — | — | **pendent** (e-ink IT8951E) | — |

El contracte sencer, amb totes les funcions i els comentaris que les expliquen, és a
`lib/hal/include/hal/hal.h`. **Quan aquest document i el capçalari no diguin el mateix, mana el
capçalari**: aquí hi ha el perquè de les decisions, no una còpia de les signatures.

```cpp
class IPlatform {
 public:
  virtual const char* id() const = 0;      // "m5stack-atom", "m5stack-atom-matrix", "emulator"
  virtual const char* family() const = 0;  // "esp32": el que un manifest vol dir amb "esp32"
  virtual bool emulator() const = 0;       // cert només al PC
  virtual const Caps& caps() const = 0;
  virtual uint32_t heap_limit_kb() const = 0;
  virtual IClock* clock() = 0;
  virtual IStorage* storage(const char* mount) = 0;   // "/littlefs" i prou
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

## La identitat de la placa

La identitat no és dins de cap HAL: és una llibreria pròpia per placa, a `lib/board_<id>/include/board/`,
amb dues capçaleres de nom fix:

| `board/identity.h` | Què diu |
|---|---|
| `id()` | el nom de la placa: `m5stack-atom`, `m5stack-atom-matrix`, `emulator` |
| `family()` | el xip: `esp32`. És el que un manifest vol dir amb `"boards": ["esp32"]` |
| `emulator()` | cert només al PC |
| `led_count()` | quants llums: 1 al Lite, 25 al Matrix i al banc |
| `led_max()` | el sostre d'un canal: 255 al Lite, 20 al Matrix i al banc |
| `has_imu()` | si la placa porta sensor inercial |

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

## El botó

Un botó a **GPIO39**, entrada només i sense pull-up intern: la resistència la porta la placa i prémer el
porta a GND. El rebot és de **10 ms**, el mateix que fa servir la llibreria del fabricant, i es llegeix
**a cada torn del nucli** (cada 20 ms mentre una app viu), sense cap interrupció: un dit no baixa de
50 ms, i una premuda que no arriba a un torn no és una premuda.

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
  mentre dorm es perd; i per això el sistema **no s'adorm sol**: una placa sense pantalla que s'adorm
  quan ningú escriu es menjaria les tecles de la persona (docs/90).
- **El processador arrenca a 160 MHz** i es pot dur a **240 o a 80**, mai per sota: `setCpuFrequencyMhz`
  del core mou també l'APB i el tick de FreeRTOS, i per sota de 80 l'APB baixa i cap perifèric es
  reconfigura —ni el port sèrie, que és l'única interfície d'aquesta placa. Un valor que no sigui un dels
  passos es refusa, no s'arrodoneix.
- **No hi ha DFS.** `CONFIG_PM_ENABLE` no està activat al framework precompilat, o sigui que pujar i
  baixar sol queda fora i la velocitat és sempre una decisió de qui la demana.

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
| `input` | ✓ | ✓ | — | ✓ | El botó de l'ATOM; el Paper en té tres i no es llegeixen encara |
| `led` | ✓ | ✓ | — | ✓ | El Paper no té cap llum |
| `imu` | — | ✓ | — | ✓ | El Lite no en té i la crida respon `E_UNSUPPORTED` |
| `littlefs` | ✓ | ✓ | ✓ | ✓ | Obligatori: sense ell no hi ha apps |
| `sd` | — | — | — | — | El Paper té ranura i encara no la munta; la VFS el preveu |
| `nvs` | ✓ | ✓ | ✓ | ✓ | Partició NVS / directori de fitxers |
| `rtc` | ✓ | ✓ | ✓ | ✓ | Ni l'ATOM ni el Paper tenen xip amb pila en ús: l'hora ve del SoC i es perd |
| `rtc_wake` | ✓ | ✓ | ✓ | ✓ | Despertar pel temporitzador del RTC (i pel botó, a l'ATOM) |
| `net_wifi` | ✓ | ✓ | — | ✓ | La ràdio de debò / la de mentida (docs/85); al Paper, pendent |
| `net_http` | ✓ | ✓ | — | ✓ | `esp_http_client` amb TLS / la pàgina falsa |
| `net_prov` | ✓ | ✓ | — | ✓ | El component d'Espressif / l'AP de mentida |
| `battery` | — | — | — | — | Cap d'aquestes bandes pot dir l'estat d'una bateria |
| `sleep_light` | ✓ | ✓ | ✓ | de mentida | Al PC el rellotge salta en comptes d'esperar |
| `sleep_deep` | ✓ | ✓ | ✓ | de mentida | A la placa, tornar vol dir arrencar |

`caps` i `sys.caps()` diuen el mateix i surten d'aquesta taula: **13 de 17** al Lite, **14 de 17** al
Matrix (que hi afegeix `imu`), **8 de 17** al Paper (li falten la ràdio, la pantalla, les tecles, la
targeta i el sensor) i **14 de 17** a l'emulador.

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
| Rellotge | **BM8563** amb pila de botó: és el que sobreviu a treure el cable (encara no es llegeix) |
| Bateria | 1150 mAh, amb divisor a **GPIO35** (encara no es calibra) |
| Expansió | Tres ports HY2.0-4P (A: G25/G32, B: G26/G33, C: G18/G19) i una ranura microSD |
| Consola | Pont USB-sèrie: **CP2104 o CH9102** segons la unitat |

Pins que el HAL fa servir, i que són els de la llibreria del fabricant (`M5EPD.h`):

| Què | Pin |
|---|---|
| **Main power hold** (s'ha de mantenir alt o la placa s'apaga) | **GPIO2** |
| EPD: CS, SCK, MOSI, MISO, BUSY | 15, 14, 12, 13, 27 |
| Alimentació del panell / dels ports HY2.0 | GPIO23 / GPIO5 |
| microSD: CS (comparteix SCK/MOSI/MISO amb el panell) | GPIO4 |
| I2C intern: SDA, SCL | GPIO21, GPIO22 |
| GT911 (0x14) amb INT a GPIO36 · SHT30 (0x44) · BM8563 (0x51) · FM24C02 (0x50) | al mateix bus |
| Tecles: dreta, central (`PWR`), esquerra | GPIO39, 38, 37 |

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

**Què hi ha avui en aquesta placa** (docs/90): consola, LittleFS, NVS, rellotge del SoC, les dues
fondàries de son amb el despert pel temporitzador del RTC i la velocitat del processador. **No hi és
encara** el panell (IT8951E), el toc (GT911), el rellotge BM8563, la bateria, la targeta, les tres
tecles i la ràdio: la taula de capacitats ho diu en comptes de prometre-ho.

**Què està provat i què no**: `pio run -e paper` **compila net** (zero avisos propis, docs/75) i el
binari que en surt porta la identitat del Paper i cap de les de l'ATOM, que és el que es pot comprovar
sense la placa. **No s'ha flashejat encara**: el prompt pel port sèrie, el `caps` de debò i el
comportament de l'alimentació són el primer que s'ha de veure amb la placa a la mà (docs/90).

**Una discrepància que s'ha de comprovar a la placa**: la llibreria del fabricant té els noms de les
tecles en dos llocs i no diuen el mateix —`M5EPD.h` defineix `KEY_RIGHT_PIN 39` i `KEY_LEFT_PIN 37`,
mentre que el README de la mateixa llibreria diu dreta=37 i esquerra=39—. El codi que corre és el
capçalari, i és el que s'ha de creure; però el primer que cal fer amb la placa a la mà és prémer cada
tecla i veure quin GPIO es tanca.

## Fitxa: emulador (PC)

| | |
|---|---|
| Nom (`sys.board`) | `emulator` (no és una placa i no té línia `board =`) |
| Plataforma | POSIX (Linux) |
| RAM | La del PC, però **limitada al mateix sostre que la placa** |
| Fitxers | Un directori del disc (`emulator/`) amb `littlefs/` i `nvs/` |
| Rellotge | El del PC, o virtual (`--time-scale`, `--now`) |
| Consola | stdin/stdout en mode cru |
| Xarxa, energia i sensor | **De mentida i ho diuen** (docs/85) |
| Pantalla | No |

Els detalls de cada mentida són a `85 · Emulador`.

## Afegir una placa

1. Si la placa no comparteix HAL amb cap altra, copiar `lib/hal_atom/` a `lib/hal_<id>/` i canviar-ne el
   nom al `library.json`; si en comparteix (com el Matrix amb el Lite), no es copia res.
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
