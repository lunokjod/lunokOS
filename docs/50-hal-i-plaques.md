# 50 · HAL i plaques

## Dues capes de sota, no una

- **HAL** (*Hardware Abstraction Layer*): els **dispositius**. Pantalla, entrada, rellotge, discs,
  xarxa, energia, LED. Cada placa implementa els que té.
- **PAL** (*Platform Abstraction Layer*): el **sistema de sota**. Cua d'esdeveniments,
  temporitzador, consola, log, rellotge monòton, reinici. Cada sistema operatiu (FreeRTOS o POSIX)
  n'implementa una.

Si una cosa és «un aparell que es pot endollar», és HAL. Si és «una manera de fer cues», és PAL.

## Interfícies del HAL

Totes són classes amb funcions virtuals pures, sense memòria dinàmica i sense excepcions. Una placa
que no té un dispositiu torna `nullptr` i la seva capacitat és `false`.

```cpp
// Rellotge: la font de temps i el despertador.
class IClock {
 public:
  virtual int64_t epoch_ms() = 0;                 // hora de paret
  virtual bool    set_epoch_ms(int64_t) = 0;      // pot fallar si el xip no s'hi deixa
  virtual int64_t monotonic_ms() = 0;             // mai enrere
  virtual bool    arm_wake(int64_t at_epoch_ms) = 0;  // NOMÉS un despertador
  virtual void    disarm_wake() = 0;
  virtual WakeCause last_wake() = 0;              // POWER_ON, TIMER, CONSOLE, OTHER
  virtual bool    can_wake() const = 0;           // pot despertar el sistema?
};

// Un volum muntat. Els camins que rep són relatius a l'arrel del volum.
class IStorage {
 public:
  virtual bool stat(const char* rel, Stat& out) = 0;
  virtual bool list(const char* rel, ListSink& sink) = 0;   // sink per no allocar
  virtual bool open(const char* rel, OpenMode, Handle& out) = 0;
  virtual bool remove(const char* rel) = 0;
  virtual bool mkdir(const char* rel) = 0;
  virtual bool rename(const char* from, const char* to) = 0;
  virtual bool space(uint64_t& total, uint64_t& free) = 0;
};

// Claus i valors, exposat com a fitxers.
class IKeyValue {
 public:
  virtual bool get(const char* ns, const char* key, void* buf, size_t& len) = 0;
  virtual bool set(const char* ns, const char* key, const void* buf, size_t len) = 0;
  virtual bool erase(const char* ns, const char* key) = 0;
  virtual bool next(const char* ns, const char*& key, bool& first) = 0;
};

class INet {
 public:
  // Comença i torna de seguida: **no hi ha cap crida que esperi la ràdio**. El
  // termini és del nucli, que és qui ha de poder seguir atenent el prompt i les
  // alarmes mentre l'associació va fent (docs/30).
  virtual bool wifi_start(const char* ssid, const char* pass) = 0;
  virtual bool wifi_start_stored() = 0;  // la xarxa que la placa ja té desada
  virtual NetState wifi_state() = 0;     // off, connecting, up, failed
  virtual bool wifi_disconnect() = 0;

  // Provisioning: l'AP i el servei protocomm, i a preguntar com va a cada torn.
  virtual bool prov_start(const char* pop, const char* service_name) = 0;
  virtual ProvState prov_state() = 0;    // off, running, done, failed
  virtual bool prov_stop() = 0;
  virtual bool prov_provisioned() = 0;
  virtual bool prov_forget() = 0;
  // http_get: treballa en una tasca auxiliar i deixa anar un event en acabar.
  virtual uint32_t http_get_start(const char* url, const HttpOpts&) = 0;
  virtual bool http_poll(uint32_t op, bool& done, HttpResult& out) = 0;
  virtual void http_release(uint32_t op) = 0;
};

// Dues fondàries: `kLight` ho conserva tot i torna; `kDeep` només conserva la
// memòria del RTC i la placa s'arrenca de nou (docs/90).
enum class SleepMode : uint8_t { kLight = 0, kDeep = 1 };

class IPower {
 public:
  virtual bool battery(int& mv, int& pct, bool& charging) = 0;  // false = no ho sap
  virtual bool sleep(SleepMode, int64_t at_epoch_ms) = 0;       // profond: no torna
  virtual const char* wakes() = 0;            // "timer, button", "timer" o "none"
  virtual uint32_t cpu_mhz() = 0;             // a quina velocitat va ara
  virtual bool set_cpu_mhz(uint32_t mhz) = 0; // i el canvi, entre els passos de la placa
};
// `keep_awake` no és aquí: és del nucli, que és qui decideix quan dormir (docs/90).

// Reserves fins que hi hagi maquinari d'aquesta mena.
class IDisplay { /* versió 1: no implementada */ };
class IInput   { /* versió 1: no implementada */ };
class ILed     { /* versió 1: no implementada */ };
```

```cpp
class IPlatform {
 public:
  virtual const char* id() const = 0;         // el nom de `board =` o "emulator"
  virtual bool        emulator() const = 0;   // cert només al PC
  virtual const Caps& caps() const = 0;
  virtual int64_t     heap_limit_kb() const = 0;
  virtual IClock*     clock() = 0;
  virtual IStorage*   storage(const char* mount) = 0;   // "/littlefs", "/sd"
  virtual IKeyValue*  nvs() = 0;
  virtual INet*       net() = 0;
  virtual IPower*     power() = 0;
  virtual IDisplay*   display() = 0;          // nullptr si no n'hi ha
};
```

`id()` és **el nom que la placa té a `platformio.ini`**, lletra per lletra (`board = m5stack-atom`).

### El provisioning no és inventat: és el d'Espressif (30/10)

L'ATOM fa servir el **component `wifi_provisioning`** de l'ESP-IDF, amb l'esquema **SoftAP** i
**Security 1** (proof of possession). És exactament el que espera l'app **ESP SoftAP Provisioning**
d'Espressif, que és la que hi ha a les botigues d'Android i iOS: no hi ha protocol propi, ni un
endpoint inventat per nosaltres.

| Què | Valor |
|---|---|
| Transport | SoftAP: la placa obre `PROV_xxxxxx` (els tres últims octets de la MAC) |
| Seguretat | `WIFI_PROV_SECURITY_1`: el POP que la persona escriu al telèfon |
| Quantes a la vegada | Una; el nucli no engega res mentre una altra cosa va |
| Els esdeveniments | `PROV_START`, `CRED_RECV`, `CRED_FAIL`, `END` → una bandera, i el nucli la llegeix al seu torn |
| Les credencials | Al magatzem del nucli Wi-Fi d'Espressif, a la NVS. **No les escrivim enlloc més** |
| El termini | Del nucli, no del component: 180 s per defecte (una persona escrivint al mòbil) |

#### El QR al port sèrie

L'ATOM no té pantalla, però l'app d'Espressif llegeix un **QR** amb el payload que el component
espera. El nucli l'escriu a la consola abans d'aixecar l'AP:

```
scan this with the ESP SoftAP Provisioning app:
  █▀▀▀▀▀█ ▄▀ ▄▀▄ █▀▀▀▀▀█
  ...
```

- El payload és el de sempre, sense res inventat:
  `{"ver":"v1","name":"<servei>","pop":"<paraula>","transport":"softap"}`.
- El dibuixa **`lib/qr`**, escrit aquí: **qualsevol QR** que aquest build pugui dibuixar —versions 1 a
  20, nivells **L, M, Q i H**, i els modes **numèric, alfanumèric i byte**— amb la màscara que mana
  l'estàndard (la de menys penalització, provant-ne les vuit). El provisioning en fa servir un de
  nivell M, que és el que l'app espera. La mateixa llibreria **es veu des de Lua** (`qr.encode`), i no
  només aquí (docs/10).
  **Dos mòduls per caràcter** amb mitjos blocs (`▀▄█`), perquè una pantalla de telèfon necessita
  quadrats i un caràcter de terminal és dues vegades més alt que ample.
- Es dibuixa amb **zona clara de dos mòduls** al voltant, que també és part del símbol.
- El `name` és el nom de l'AP que la persona veurà al telèfon (`lunokos-prov` per defecte) i el `pop`
  és la paraula que ha d'escriure si escriu a mà.
- **Si el payload no cap en un QR** (no passa amb 70 bytes, però es mira) no es dibuixa un símbol
  fals: s'escriu el text perquè la persona el pugui teclejar.
- **Fins on arriba, i per què**: el build dibuixa fins a la versió 20 (97×97, uns 700 bytes de text a
  nivell M) perquè cada mòdul és un byte al buffer del qui crida i el mapa de mòduls de funció és
  estàtic. Les taules de l'estàndard arriben a la versió 40; apujar el límit és una constant i el
  preu és RAM (docs/75). Un símbol de versió 40 faria 31 329 bytes, que en aquesta placa no hi caben.

#### El log d'IDF que no hi és (30/10)

El build compila els components d'Espressif amb **`CONFIG_LOG_MAXIMUM_LEVEL 1` (només ERROR)**: els
`LOGW`/`LOGD` de `wifi_prov_mgr`, `protocomm` i `scheme_softap` **no existeixen al binari**, i per això
`esp_log_level_set(..., DEBUG)` no fa res. Tota la informació dins d'aquestes llibreries està
compilada fora: el que no es pugui veure des del nostre costat, no es veurà.

#### L'escaneig del provisioning, instrumentat (30/10)

La llista de xarxes de l'app la respon la placa, i no n'hi havia prou amb suposar per què no sortia.
Ara el board ho diu tot sol, cada tres segons mentre l'AP és obert:

```
prov: mdns up
prov: mode 3 (1 STA, 2 AP, 3 APSTA)
prov: stations 1, own scan done/23, mode 3      <- el telèfon hi és i la ràdio escaneja
```

- **`stations`**: quants telèfons hi ha associats a l'AP. Si és **0**, el telèfon no ha arribat i el
  problema no és l'escaneig sinó l'AP o el DHCP del telèfon. Si és **1**, el telèfon hi és i el que
  falla és la petició.
- **`own scan`**: el nostre propi escaneig, fet just després d'aixecar l'AP, en les mateixes
  condicions (AP+STA): és la prova que la ràdio pot escanejar amb l'AP obert.
- **El log intern d'Espressif encès** (`wifi_prov_mgr`, `protocomm`, `protocomm_httpd`,
  `wifi_prov_scheme_softap` a nivell debug): quan l'app demana `prov-scan`, el manager escriu si
  l'escaneig ha arrencat i què ha trobat.
- I la **reconnexió automàtica d'Arduino desactivada** (`WiFi.setAutoReconnect(false)`): la llibreria
  respon a un `disconnect` amb el seu propi `connect()`, i això es baralla amb el manager, que
  desconnecta a posta abans d'escanejar.

#### mDNS: sense això, el telèfon no troba el servei (30/10)

Llegint `scheme_softap.c` d'Espressif es veu què fa el provisioning quan arrenca: a més de l'AP,
**anuncia el servei per mDNS** (`mdns_service_add("_esp_wifi_prov", "_tcp", …)`) i hi posa els camins
dels endpoints (`/proto-ver`, `/prov-session`, `/prov-config`). **Si mDNS no està engegat, el codi
d'Espressif només escriu un avís i continua** — i el resultat és exactament el que es veia: el telèfon
es connecta a l'AP, l'app no troba el servei, la llista de xarxes surt buida i refrescar no fa res.

- **Ara s'inicialitza mDNS abans d'obrir el provisioning** (`MDNS.begin("lunokos")`) i es diu a la
  consola (`prov: mdns up`).
- Cost mesurat: **+25 708 B** de flash i **+1 920 B** de RAM estàtica. És el preu de ser trobat.

#### Només compta el que el sistema ha desat (30/10)

A la placa, `provision` (llavors es deia `run net`) va dir `stored network: MOVISTAR_B7DC` en un board **on ningú no havia
provisionat mai res**. Aquella xarxa era al magatzem del driver, posada per un altre firmware que havia
viscut en aquella mateixa placa abans: la partició `nvs` no s'esborra en tornar a flashejar. El sistema
la va creure i va dir que ja tenia xarxa.

- **Ara el sistema té el seu propi registre**, al seu propi espai de la NVS (`lunoknet/ssid`), i
  només creu això. El que hi hagi al driver no és seu i no compta.
- S'hi escriu en dos llocs: quan l'app dona la xarxa (a l'esdeveniment `CRED_RECV`) i quan algú la
  posa a mà (`net.add`). `net.forget()` l'esborra.
- La **contrasenya no es copia enlloc**: el driver ja la té, i el registre propi només guarda el nom.

I una segona cosa que va sortir de la mateixa traça: després d'un intent fallit el board responia
**`E_BUSY` per sempre** (`provisioned E_BUSY after 1 ms`), perquè la marca d'«intent en curs» no es
reiniciava en fallar. Ara es reinicia, i el següent intent pot començar.

#### Connectar amb la xarxa desada: `WiFi.begin()` no serveix (30/10)

`WiFiSTAClass::begin()` d'Arduino **sense arguments no connecta**: mira la llista de paràmetres i, si
no hi ha SSID, torna `WL_CONNECT_FAILED` de seguida («SSID too long or missing», `WiFiSTA.cpp:175`).
Jo el feia servir per connectar amb el que la placa tenia desat, i el resultat era exactament això:

```
stored network: casa
connecting with it failed: E_NONET
```

Amb la xarxa ben desada i la ràdio veient 27 xarxes al voltant. El que connecta és **el driver**:
llegeixes la configuració desada (`esp_wifi_get_config`) i li demanes `esp_wifi_connect()`. És el que
fa ara `wifi_start_stored`, i el número de `WiFi.status()` surt a la consola quan falla, perquè un
«failed» sense número no és cap investigació.

#### Quan l'app no llista les xarxes

La llista de xarxes de l'app **no la fa el telèfon**: la demana a la placa (una petició `scan` de
protocomm) i la placa és qui escaneja. Si surt buida i refrescar no canvia res, l'escaneig està
fallant al nostre costat o responent tard. Per no endevinar-ho:

- **Cada esdeveniment del provisioning surt a la consola** amb el seu nom (`prov: START`,
  `prov: CRED_RECV`, `prov: CRED_FAIL`, `prov: END`), i els errors de l'IDF surten com a `[E]` al
  mateix port. Amb aquesta traça se sap si l'app arriba a demanar l'escaneig.
- **El mode de la ràdio es diu en veu alta** en arrencar el provisioning (`prov: mode 3 (1 STA, 2 AP,
  3 APSTA)`): un board que no és en **AP+STA** no pot escanejar, i això és exactament el que fa que
  l'app ensenyi una llista buida. Si el mode no és APSTA, el codi el força i ho diu.
- **`net.scan()` dona el número abans de res**: una app pot escanejar i escriure `scan: N networks` al
  port sèrie. Si aquell número és més gran que zero, la ràdio hi és i el problema és de l'app o del
  telèfon; si és zero, el problema és nostre i ja té número.
- **I la llista també es pot veure**: `net.scan_record(i)` dona `{ssid, rssi}` de cada xarxa de
  l'últim escaneig, i la comanda `wifi scan` la imprimeix. Així una persona pot
  veure què veu la placa sense dependre de cap app de telèfon (docs/30).
#### El sistema és l'amo de la ràdio (31/10)

La llista d'escaneig del controlador **no era nostra**, i això va costar dos defectes.

- **El que es va mesurar**: `arduino-esp32 2.0.14` respon **cada** `ARDUINO_EVENT_WIFI_SCAN_DONE`
  amb `WiFiScanClass::_scanDone()` (`WiFiGeneric.cpp:1041`), i `_scanDone()` fa
  `esp_wifi_scan_get_ap_num` + `esp_wifi_scan_get_ap_records` (`WiFiScan.cpp:109-121`). Aquella
  crida **allibera la llista**, però **el número hi queda**. Un escaneig demanat per nosaltres i
  llegit per nosaltres donava exactament això al port sèrie: `scan: 0 records of 23 (ESP_OK)` — el
  número era cert i darrere seu no hi havia res.
- **I el gestor de provisioning juga la mateixa carta**: `wifi_prov_mgr` llegeix la llista del
  controlador dins del seu propi handler de `WIFI_EVENT_SCAN_DONE` (`manager.c.obj`: totes les
  crides a `esp_wifi_scan_get_ap_num` i `esp_wifi_scan_get_ap_records` són dins de
  `wifi_prov_mgr_event_handler_internal`, vist amb `nm` i `objdump` sobre
  `libwifi_provisioning.a`). Dues biblioteques i el sistema volent la mateixa llista, i el telèfon
  rebent-ne el que sobrava.
- **La decisió**: el HAL parla amb `esp_wifi` directament i **la classe WiFi d'Arduino no
  s'inicialitza mai**. `esp_netif_init`, `esp_event_loop_create_default`,
  `esp_netif_create_default_wifi_sta`, `esp_netif_create_default_wifi_ap`, `esp_wifi_init` i
  `esp_wifi_start` són aquí; l'estat de l'estació surt dels esdeveniments (`WIFI_EVENT_STA_CONNECTED`,
  `WIFI_EVENT_STA_DISCONNECTED`, `IP_EVENT_STA_GOT_IP`) i no de `WiFi.status()`. Sense la classe
  d'Arduino no hi ha ningú que s'endugui la llista abans que el gestor.
- **El que costa i el que estalvia, mesurat**: **−23 588 B** de flash (1 071 949 → 1 048 361) i
  **−384 B** de RAM estàtica (68 768 → 68 384). Sortir d'una capa també és estalviar.
- **mDNS passa per IDF**: `mdns_init` + `mdns_hostname_set("lunokos")`, i s'anuncia **després** que
  l'access point és a dalt. Abans anava per `ESPmDNS` i s'anunciava abans d'obrir l'AP, que deixa el
  nom en una interfície que encara no existeix.
- **`esp_wifi_disconnect` no esborra res**: l'`WiFi.disconnect(true)` d'abans també buidava la
  configuració de l'estació, o sigui que una app que només havia escanejat deixava la placa sense
  xarxa al driver.
- **La llista que rep una app és la de la taula pròpia**: `wifi_scan_done()` copia els registres del
  controlador a `g_records` i `wifi_scan_count()` contesta quants se'n poden llegir. La línia del
  port sèrie és `scan: 23 records of 23`.
#### La pila de Bluetooth no és nostra: no s'hi toca (31/10)

Tercer intent, i la traça va tancar el cas:

```
E (8666) wifi_prov_scheme_ble: bt_mem_release of classic BT failed 259
E (8670) simple_ble: simple_ble_start enable controller failed 259
E (8670) protocomm_ble: simple_ble_start failed w/ error code 0x103
```

259 és `ESP_ERR_INVALID_STATE` i 0x103 el mateix. `simple_ble_start` — dins del protocomm **precompilat**
— ja fa la feina: inicialitza el controlador amb el mode d'aquest build (**BTDM**), l'habilita i puja
Bluedroid. I l'esquema fa l'alliberament del clàssic al `WIFI_PROV_INIT`.

O sigui que **l'aplicació no hi ha de tocar res**. Tot el que fèiem nosaltres (init, enable, Bluedroid,
alliberar el clàssic) era, en el millor cas, feina duplicada i, en el pitjor, la raó per la qual la
llibreria no podia arrencar: el controlador ja era nostre amb un altre mode.

Del que havíem escrit, **només queda `btInUse()`**: sense ell Arduino allibera la memòria de BTDM a
l'arrencada i res no pot arrencar després. La resta és esborrada.

#### El mode del controlador ha de ser el de la configuració (31/10)

Segon intent, i el port sèrie va dir `prov: the bluetooth controller did not enable (ESP_ERR_INVALID_ARG)`.
El capçal `esp_bt.h` d'aquesta versió ho diu clar:

```
@param mode : the mode(BLE/BT/BTDM) to enable. ... This mode must be equal as the
              mode in "cfg" of esp_bt_controller_init().
```

I `BT_CONTROLLER_INIT_CONFIG_DEFAULT()` posa `.mode = BTDM_CONTROLLER_MODE_EFF`, que en aquest build
val **BTDM** (`CONFIG_BTDM_CTRL_MODE_BTDM=y`). Demanar `ESP_BT_MODE_BLE` amb una configuració BTDM és
`ESP_ERR_INVALID_ARG`, exactament el que va sortir.

**La diferència amb la llibreria oficial d'M5Stack** (`m5stack/M5DualKey-UserDemo`,
`main/ble/esp_hid_gap.c`, que és l'`esp_hid_gap.c` d'Espressif): els passos són **els mateixos** —
`esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT)`, `esp_bt_controller_init`,
`esp_bt_controller_enable(ESP_BT_MODE_BLE)`, `esp_bluedroid_init`, `esp_bluedroid_enable`. No hi ha
cap trampa: el que canvia és que **el seu `sdkconfig` és BLE-only** (`CONFIG_BTDM_CTRL_MODE_BLE_ONLY`),
i per tant la seva configuració per defecte ja diu `mode = BLE`. El d'Arduino és dual i no es pot
canviar, perquè ve precompilat. La línia que faltava és aquesta:

```c
esp_bt_controller_config_t config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
config.mode = ESP_BT_MODE_BLE;   /* el camp és dels que es poden canviar en execució */
```

I amb `enable(ESP_BT_MODE_BLE)` la mateixa documentació diu que **sí** que és segur alliberar la
memòria del clàssic en arrencar, o sigui que `ESP_BT_MODE_CLASSIC_BT` es torna a alliberar aquí.

#### Demanar la connexió una sola vegada (31/10)

`wifi connect` va funcionar a la primera, però el port sèrie va deixar anar una línia d'error del
controlador:

```
wifi: connecting to MOVISTAR-A210-IOT
E (25153) wifi: sta is connecting, return error
wifi: address 192.168.1.37
```

No era cap fallada: era **jo demanant la connexió dues vegades** (una de seguida i una altra a
`WIFI_EVENT_STA_START`), i el controlador contestant la segona amb un error que ni tan sols era un
error. Ara hi ha un sol lloc que ho decideix, `ask_station()`:

- si el controlador **no** és en mode estació, es canvia el mode (que el reinicia) i la connexió es
  demana a `STA_START`, que és l'únic moment en què no es perd;
- si **ja** és en mode estació, no es toca el mode i es demana de seguida.

I el flag `g_connect_asked` impedeix que el gestor de `STA_START` ho torni a demanar per al mateix
intent. Una ordre, una petició, cap línia d'error.

#### Del record a la ràdio hi ha una paraula (31/10)

Un reinici amb la xarxa desada deixa la placa en `network: <ssid>` i `state: off`: el record és a la
NVS i la ràdio no s'encén. **`wifi connect`** és el que els ajunta: connecta amb el que el sistema té
desat (`net::connect_stored`), i si no hi ha res desat ho diu sense arrencar cap feina
(`wifi: the board has no network to connect to`).

Si la placa ha de connectar tota sola en arrencar és una **decisió d'energia**, no un descuit: encendre
la ràdio a cada arrencada per una xarxa que potser ningú no vol fer servir és el que M6 ha de decidir
amb números (docs/75).

I la comanda `wifi` ja no acaba amb una línia d'ús: l'estat és l'estat, i les ordres són a `help`.

#### El nom desat es llegeix de l'esdeveniment, no del controlador (31/10)

Després del provisioning la placa va dir `wifi: already on MOVISTAR-A210-IOT` i tot seguit
**`network: none`**, amb `state: up`. És a dir: **connectada i sense xarxa desada**. La causa era que
la fitxa pròpia es desava a `WIFI_PROV_CRED_RECV` llegint `esp_wifi_get_config()`, i en aquell moment
el gestor **encara no hi ha escrit** la configuració. Ara el nom es pren de **l'esdeveniment mateix**
(`event_data` és el `wifi_sta_config_t` que ha arribat), i el controlador només es pregunta si
l'esdeveniment vingués buit (aleshores ho diu: `prov: the credentials came with no name`).

I **`state` surt amb paraules** (`off`, `connecting`, `up`, `failed`) en comptes de `state: 2`:
`net::state_name()` dona el mateix número del HAL en un mot que una persona pot llegir.

#### Connectar-se quan el controlador ja és a punt (31/10)

La traça de la placa ho va ensenyar tot en nou línies:

```
wifi: address 192.168.1.37        ← el gestor hi havia posat l'estació per provar les claus
prov: END
prov: the radio is ours again
wifi: connecting to MOVISTAR-A210-IOT
provision: nothing came of it, E_TIMEOUT   ← vint segons de res
wifi: dropped, reason 8
```

Dues coses, totes dues meves:

1. **Ja hi érem i ho vaig desfer**: el gestor connecta l'estació ell mateix per comprovar les
   credencials, i el sistema, en rebre el final, tornava a canviar el mode i a demanar la connexió.
   Ara, si ja hi ha adreça, **no es toca res**: `wifi: already on <ssid>` i la feina acaba de
   seguida amb el nom de la xarxa.
2. **La connexió es demanava en el pitjor moment**: `esp_wifi_set_mode()` **reinicia el
   controlador**, i una `esp_wifi_connect()` demanada just abans d'aquell reinici es perd **sense
   error i sense cap esdeveniment** — d'aquí els vint segons muts i el `reason 8` (assoc leave) que
   arribava al final, quan el termini tancava. Ara la connexió es demana també a
   `WIFI_EVENT_STA_START`, que és el moment en què el controlador ja és a punt, i la que arriba
   tard rep `ESP_ERR_WIFI_CONN`, que no és cap fracàs. El flag és `g_want_station`, i el baixa
   `wifi_disconnect()`.

El camí de dalt (l'adreça ja hi és) és el que ha de passar sempre després del provisioning; el de
baix (connectar a `STA_START`) és el que ha de funcionar quan la connexió s'ha de refer.

#### Una ordre que espera no pot tancar la placa (31/10)

- **El que es va veure**: `wifi forget` responia `the radio is busy: wait for it` i no hi havia cap
  manera de sortir-ne. La ràdio estava ocupada per una feina que esperava un telèfon, i el missatge,
  a més de no deixar fer res, no deia **amb què** estava ocupada.
- **Ara**: `wifi forget` **atura primer** el que estigui corrent (`radio: stopped`) i després
  esborra: esborrar és la decisió de la persona i val més que la feina que hi havia en marxa. I el
  missatge diu què passa — `the radio is busy: provisioning, use \`wifi stop\` to drop it` — perquè
  `net::busy_with()` dona el nom de la feina (`scanning`, `connecting`, `provisioning`).
- **`wifi stop`** és la sortida explícita: baixa l'access point, talla l'escaneig o l'associació, i
  torna el prompt. És el que faltava perquè una ordre que espera una persona no pugui deixar la
  placa sense respostes.
- **Prova**: `test_wifi_forget_stops_a_job_that_is_running` arrenca `provision` en un emulador que
  espera 5 s un telèfon, comprova que la feina és viva i que `wifi forget` la deixa anar.

## Una pàgina, amb TLS, sense parar el sistema (31/10)

`http_get_start` **no baixa res**: desa l'adreça i aixeca una tasca de **10 240 B** de pila, que és
qui fa la feina. El nucli pregunta `http_poll` a cada torn i dona la resposta com a promesa. La tasca
deixa la pàgina en un buffer que és **seu** fins que el nucli crida `http_release`, i allà es fa el
lliurament:

| Qui | Què fa |
|---|---|
| Tasca | `esp_http_client_init` → `open` → `fetch_headers` → llegeix → `close` → publica (o llença, si ningú la vol) |
| Nucli | pregunta l'estat a cada torn, compta el termini, copia el cos (a Lua o a la consola) i **allibera** |
| Ràdio | la de sempre: es puja perquè la petició porta `needs_network` i es tanca quan la cua es buida |

- **TLS**: `esp_http_client` amb `esp_crt_bundle_attach`, o sigui el **paquet d'autoritats del build**
  (`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y`, comprovat al `sdkconfig` del framework). No hi ha cap
  certificat escrit al repositori i no hi ha manera de desactivar la verificació.
- **Els errors es diuen pel seu nom**: tot el que és `>= 0x8000` ve de la pila de TLS
  (`esp_tls_errors.h`), i els quatre codis que són del sòcol i no del certificat
  (`CANNOT_RESOLVE_HOSTNAME`, `CANNOT_CREATE_SOCKET`, `FAILED_CONNECT_TO_HOST`,
  `CONNECTION_TIMEOUT`) es queden a `kConnect`. La resta és `kTls`, i el nucli ho converteix en
  `E_TLS` en comptes de fer creure que el servidor no hi era.
- **Una pàgina que no cap es refusa, no es talla**: en arribar al sostre es llegeix **un byte més**;
  si el servidor encara en té, l'error és `kTooBig`.
- **Si ningú la vol** (`http_release` amb la feina a mitges o `wifi stop`), la tasca ho veu i allibera
  ella mateixa el que havia demanat: la ràdio i el heap es queden com estaven (docs/75).
- **`HttpResult` no porta capçaleres**: no les ha demanades ningú i una llista de capçaleres costa
  memòria que docs/75 no ha donat.
- **`lib_ignore` al `platformio.ini`**: en veure `esp_crt_bundle.h`, el cercador de llibreries de
  PlatformIO carregava `WiFiClientSecure` (i petava a `WiFi.h`). Les llibreries de xarxa d'Arduino
  estan fora del build a posta: la ràdio i el TLS són del sistema, com la WiFi que ja es va treure
  (mesurat: −25 780 B de flash quan es va fer).

**Què costa, mesurat a l'ATOM (31/10)**: flash **1 761 265 → 1 957 613 B** (**+196 348 B**, del
62,2 % d'una partició de 3 MB) i RAM estàtica **84 408 → 87 408 B** (+3 000, dels quals 1 536 són
l'adreça de 192 B a cada un dels 8 llocs de la cua). Queden **1 188 115 B** de flash lliures.
**Pendent de mesurar a la placa**: el heap durant l'apretada de mans del TLS (les memòries del
`mbedtls` d'aquest build són de 16 384 B d'entrada i 16 384 de sortida, o sigui que l'ordre és de
desenes de kB) i el temps d'una baixada de debò. **El que ja se sap i no és una opinió**: amb el
Bluetooth a dalt queden **12 152 B** de heap i una apretada de mans no hi cap; el TLS i el
provisioning per Bluetooth no poden coincidir, i és per això que el provisioning no deixa res
ençès després.

## El provisioning per Bluetooth, provat a la placa (31/10)

Funciona de punta a punta, i aquesta és la traça que ho diu:

```
lunok> provision
prov: INIT
prov: START
prov: bluetooth, name PROV_xxxxxx
<QR amb "scan this with the ESP BLE Provisioning app">
prov: scan done (status 0, 28 ap in the event, 0 left for us, ESP_OK, heap 12152)
prov: CRED_RECV
wifi: address 192.168.1.37
prov: CRED_SUCCESS
prov: bluetooth freed
prov: END
prov: the radio is ours again
wifi: already on MOVISTAR-A210-IOT
network: MOVISTAR-A210-IOT
```

Tres coses que aquesta traça va ensenyar i que s'han arreglat:

- **El nom ha de ser el que s'anuncia.** La traça deia `name lunokos-prov`, que era un nom per defecte
  del nucli: el QR ha de portar el nom que la ràdio anuncia o el telèfon no troba res. Ara el nom el
  posa **la placa** (a partir del seu MAC, `PROV_xxxxxx`) i l'escriu al buffer que li passa el nucli,
  o sigui que el QR i l'anunci diuen exactament el mateix.
- **L'etiqueta del QR ha de dir l'app que toca**: deia «ESP SoftAP Provisioning» en un QR de Bluetooth.
  Ara diu **ESP BLE Provisioning** o **ESP SoftAP Provisioning** segons el transport, i hi ha prova
  (`test_the_qr_names_the_app_of_the_transport`).
- **El heap, mesurat**: **12 152 B** lliures mentre el Bluetooth és a dalt. La pila de Bluedroid més el
  controlador se'n mengen gairebé tots els ~120 KB que hi havia. Funciona, però és poc marge: una app de
  Lua no hi cap en aquest moment i el sistema no ha d'intentar fer res gran mentre es provisiona. Queda
  anotat com a número, no com a opinió.

## Provisonar per Bluetooth (31/10)

`provision` és **Bluetooth**, que és el que el fabricant recomana i el que no treu el telèfon de la
seva xarxa. `provision softap` és el camí d'abans, i es queda perquè no necessita Bluetooth.

- **Què fa servir**: `wifi_prov_scheme_ble` del paquet, o sigui **Bluedroid**. `CONFIG_BT_ENABLED=y`,
  `CONFIG_BT_NIMBLE_ENABLED` no, i **no hi ha `libnimble.a`**: amb aquest SDK, NimBLE no és una opció
  de configuració (docs/75).
- **Quan s'encén**: mai a l'arrencada. `bt_ready()` fa `esp_bt_controller_init` + `esp_bt_controller_enable(ESP_BT_MODE_BLE)`
  + `esp_bluedroid_init` + `esp_bluedroid_enable` quan algú demana `provision`, i l'esquema l'allibera
  en acabar (`WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM`).
- **`esp_bt_controller_mem_release` no es fa enlloc**, ni el de la part clàssica (aquella recepta és
  per a un build BLE-only i aquest és `CONFIG_BTDM_CTRL_MODE_BTDM=y`) ni el de BTDM. El que és BLE és
  el **enable**: la memòria es reserva per a tots dos.
- **I qui l'alliberava era Arduino, a l'arrencada** (31/10, mesurat). `initArduino()` fa
  `if (!btInUse()) esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);`, i `btInUse()` té **dues
  definicions al nucli i totes dues són weak**: `return false` a `esp32-hal-misc.c` i `return true` a
  `esp32-hal-bt.c`. Quina queda al binari és una moneda a l'aire de l'ordre de l'enllaçador, i en
  aquest build va tocar la que diu que no. S'ha comprovat al firmware: el símbol era `W btInUse` i el
  cos era `movi.n a2, 0`. Amb la memòria alliberada, `esp_bt_controller_init` contesta
  **`ESP_ERR_INVALID_STATE`**, que és exactament el que la placa va dir. El nostre HAL defineix ara
  un `btInUse()` **fort** que diu que sí — és el ganxo que el mateix nucli documenta per a aquest
  cas (`// user may want to change it to free resources`) — i al binari ja surt `T btInUse` amb
  `movi.n a2, 1`.
- **El cost d'això**: la memòria del controlador queda reservada sempre, no només quan es fa servir.
  És RAM que el heap no veu; es compensa amb el que ja estalvia no engegar mai el Bluetooth si ningú
  no el demana.
- **Cada pas diu el seu error**: `esp_err_to_name` a cada crida de `bt_ready()`, perquè una fallada
  digui `ESP_ERR_NO_MEM` o `ESP_ERR_INVALID_STATE` en comptes de «no ha arrencat».
- **El QR ho diu**: el payload passa a `"transport":"ble"` i el `name` és el nom que anuncia el
  Bluetooth (`PROV_xxxxxx`). És `net.provision`, `build_payload` i res inventat (docs/30).
- **El que cal a la placa**: res, ni mDNS ni AP+STA — el telèfon troba la placa per l'anunci. El
  `provision softap` continua forçant AP+STA i anunciant mDNS, perquè allà sí que cal.
- **El preu, mesurat**: **+699 940 B** de flash i **+12 856 B** de RAM estàtica. Amb la taula de
  particions d'abans no hi cabia (`111.3 %`), i per això les dues ranures d'1,5 MB són ara una de
  3 MB (docs/75).

#### El gestor s'ha de plegar quan acaba (31/10)

- **El que es va veure**: la placa deia `prov: CRED_RECV`, `CRED_SUCCESS` i `END`, i el prompt no
  tornava. No era una penjada del sistema: la comanda `provision` esperava el seu termini, que era el
  **del provisioning sencer (180 s)**, perquè en acabar la feina el nucli no l'escurçava. Ara, quan
  arriba `kDone`, el termini passa a **20 s** (`kProvConnectMs`): el que queda és una associació com
  qualsevol i no ha de retenir el prompt tres minuts.
- **I la causa**: `wifi_prov_mgr` **no es plegava**. Els exemples d'Espressif criden
  `wifi_prov_mgr_deinit()` a `WIFI_PROV_END` i jo no ho feia, o sigui que el gestor seguia viu amb la
  ràdio en AP+STA i l'access point a dalt. Ara es fa el mateix que a l'exemple, des del mateix
  esdeveniment, i es diu al port sèrie: `prov: the radio is ours again`.
- **Cada pas de l'associació diu una línia** (una per esdeveniment, res periòdic):
  `wifi: connecting to <ssid>`, `wifi: dropped, reason <n>` i `wifi: address <ip>`. Amb això una
  associació que no puja diu **on** s'ha aturat en comptes de deixar un prompt que no arriba. Si
  falla en arrencar, la línia diu el pas que ha refusat (config del controlador, mode, o
  `esp_wifi_connect` amb el seu `esp_err_to_name`).

#### La brossa del principi no és nostra (31/10)

En obrir el port sèrie surten unes quantes desenes de bytes `0x00` abans del banner. **No és cap
flush** i no és codi nostre: `Serial.begin(115200)` és la primera cosa que fa el PAL, i **abans** d'això
el xip ja ha imprès el que imprimeix el seu ROM (la línia de reset i l'`ets Jun 8 2016…`) a **74880**
bauds. Un terminal a 115200 llegeix aquells bytes com el que no són, i una UART amb el baud mal
interpretat dona `0x00` a dojo.

El que sí que netegem és **l'entrada**: `pal::init()` buida el que hagi arribat en 20 ms i
`kernel::settle_console()` en llença 250 ms més, de manera que aquesta brossa no pot convertir-se mai
en una ordre. I res del que escrivim porta `0x00`: cada `write` va amb la seva longitud explícita.

De passada: la línia `wifi: dropped, reason 8` que sortia després de `wifi stop` **era nostra**, la
baixada que nosaltres mateixos havíem demanat. Ara no es diu (`g_expect_drop`): quan algú prem `stop`
no ha de llegir una línia que sembla una avaria.

#### `wifi stop` és el que diu, i `ntp` necessita xarxa (31/10)

Dues coses que es van veure a la placa el mateix moment:

- **`wifi stop` deia «nothing to stop»** quan la placa era a la xarxa sense cap feina corrent, o
  sigui que no hi havia manera de baixar-la de la xarxa si no era esborrant-la. Ara `wifi stop` (i
  `wifi forget`) **tallen el que corria i treuen la ràdio de la xarxa** (`net::stop()`: buida la cua,
  atura el provisioning i desconnecta). És el que vol dir «stop» i és el que l'energia demana.
- **`ntp` sense xarxa esperava el termini sencer** (40 s) perquè un servidor de temps no contesta si
  no hi ha xarxa. Ara es refusa **a l'acte** i ho diu: `ntp: the board is not on a network, use
  \`wifi connect\``. Des de Lua el codi és **`E_NONET`** i no `E_BUSY`, perquè el que ha passat és
  que no hi ha xarxa, no que la ràdio estigués ocupada.
- I la línia d'ús de `wifi` no deia **`connect`**, que és la meitat de la comanda. Ara hi és, i
  `wifi start` fa el mateix que `wifi connect` (és el que una persona escriu quan la placa és apagada
  i la vol encesa).

#### El rellotge, i el client de temps (31/10)

`net.ntp` i `ntp` posen el rellotge amb el `esp_sntp` de lwip. El client **posa el rellotge del
sistema ell mateix** (`settimeofday`), així que el nostre `AtomClock` el veu de seguida; la crida de
notificació només serveix perquè el nucli sàpiga que ha passat. El nucli, quan li arriba, li diu
l'hora al rellotge **una altra vegada** per la via de sempre (`IClock::set_epoch_ms`), perquè la
banda falsa no té lwip i les dues han de fer el mateix camí.

Costa **+1 856 B** de RAM estàtica (el client de temps) i **+8 868 B** de flash (les taules de NTP
de lwip entren al binari quan algú les crida). No deixa la ràdio encesa: la cua ho sap perquè la
petició no demana `keeps_radio`.

- **`prov_scan_probe_cb` es queda com a mesura**: registrat després del gestor, diu qui s'ha endut
  la llista, i a la placa diu el que havia de dir:
  `prov: scan done (status 0, 29 ap in the event, 0 left for us, ESP_OK, heap 119120)` (31/10). Zero
  vol dir que **el gestor hi ha arribat primer**, que és el que calia; i els **119 120 B** de heap
  són l'espai que té la resposta per construir-se. Després d'aquesta línia la placa va dir
  `prov: CRED_RECV`, `prov: CRED_SUCCESS` i `prov: END`: la llista surt al telèfon i la persona tria
  la xarxa.

#### Res que escanegi sol (31/10)

- **Fora el diagnòstic periòdic**: `prov_state()` imprimia una línia cada 3 segons amb el número
  d'estacions i el compte del controlador. No escanejava (era un getter), però llegit al port sèrie
  semblava una ràdio treballant per a res, i el prompt es menjava les línies útils. Ara no imprimeix
  res.
- **Fora l'escaneig de diagnòstic a l'inici del provisioning**: dos escanejos de la mateixa ràdio es
  fan fora l'un a l'altre, i el telèfon en demana un tot just obrir la llista. La ràdio només
  treballa quan una app o el telèfon ho demanen (docs/75).
- **L'app diu quantes n'ha llistat**: `listed: N` al final de la llista. Un recompte sense noms a
  sota és una placa que va escanejar i no va poder llegir el que va veure, i això s'ha de veure, no
  quedar en blanc. I una xarxa que no diu el nom surt com a `(hidden network)` en comptes de
  desaparèixer: una entrada que s'amaga és igual que un escaneig que no ha tornat res.
- **Hi ha un camí que no depèn de l'app**: `net.add(ssid, pass)` desa la xarxa a la placa i s'hi
  connecta. La comanda `wifi add <ssid> [password]` ho fa des del port sèrie. La xarxa queda
  desada, així que a partir d'aquí `net.connect_stored()` la fa servir quan calgui.

L'alternativa hauria estat fer servir el wrapper `WiFiProv` d'Arduino, però no deixa veure els
esdeveniments (només escriu al port sèrie) i el nucli no podria dir a l'app si el provisioning ha
acabat. Per això es fa servir l'API de l'IDF directament, amb la mateixa seqüència que el wrapper.
Al PC no hi ha cap placa i no hi ha cap línia `board =`, i per això l'emulador respon `emulator`. És
el mateix nom que una app de Lua llegeix a `sys.board` (`30 · API`): si una app ha de triar camí de
maquinari, ho fa amb aquest nom i no amb una suposició.

## Com es tria la placa, sense `#ifdef`

Cada placa porta **dues capçaleres amb el mateix nom que les altres** i contingut
diferent, i el camí d'inclusió de l'entorn decideix quina es veu:

```
lib/board_atom/include/board/identity.h       lib/hal_emulator/include/board/identity.h
  board::id()  -> "m5stack-atom"                board::id()  -> "emulator"
  board::family() -> "esp32"                    board::family() -> "host"
  board::emulator() -> false                    board::emulator() -> true
  board::led_count() -> 1                       (el PC no en té cap de debò)

lib/board_matrix/include/board/identity.h
  board::id()  -> "m5stack-atom-matrix"
  board::family() -> "esp32"
  board::led_count() -> 25
```

A `platformio.ini`, cada entorn afegeix **només** la carpeta de la seva placa, i diu `lib_ignore` de
les altres:

```ini
[env:atom]
build_flags = -std=gnu++11 -Ilib/board_atom/include
lib_ignore = hal_emulator, pal_host, board_matrix, …

[env:atom_matrix]
extends = env:atom
build_flags = -std=gnu++11 -Ilib/board_matrix/include
lib_ignore = hal_emulator, pal_host, board_atom, …

[env:emulator]
build_flags = -std=c++11 -Ilib/hal_emulator/include
lib_ignore = hal_atom, pal_esp32, board_atom, board_matrix
```

**Per què la identitat de l'ATOM és una llibreria pròpia i no viu dins de `lib/hal_atom/`**: `hal_atom`
és el HAL de **la família** —el Lite i el Matrix tenen el mateix xip, la mateixa flash, la mateixa
taula, el mateix botó i la mateixa consola—, i un HAL amb la identitat a dins només pot servir una
placa. Les dues identitats són dins de `lib/`, en llibreries diferents, i el que les separa és
`lib_ignore`: PlatformIO posa els `-I` de les llibreries **davant** dels del `build_flags`
(`piolib.py`: `PrependUnique(CPPPATH=…)` contra l'`Append` dels flags), o sigui que amb dues
capçaleres amb el mateix nom al camí guanyaria la primera per ordre, en silenci. `lib_ignore` deixa
**exactament una** identitat per entorn, i el que no es veu és el que no s'ha compilat.

**I no poden viure fora de `lib/`** (provat el 31/10, i és el motiu pel qual això està escrit): el
cercador de dependències de PlatformIO només segueix capçaleres que siguin d'una llibreria. Amb
`board/impl.h` en un directori del projecte (`boards/atom/include/`), el cercador no el veia, no
marcava `hal_atom` com a dependència i el build petava amb
`boards/atom/include/board/impl.h:10:10: fatal error: hal_atom.h: No such file or directory`. Falla
**fort**, no en silenci, i això és el que es vol.

**`sys.board` diu el nom de la placa, i el de `board =` quan aquest nom existeix.** L'ATOM Matrix no
és una placa de PlatformIO: les dues entorns escriuen `board = m5stack-atom` perquè el xip, la flash
i la taula són els mateixos, i aquest nom no les pot distingir. Per això la identitat del Matrix diu
`m5stack-atom-matrix`: una app que ha de triar camí de maquinari té un nom per fer-ho, i el que no
pot fer mai és suposar-lo (`30 · API`).

L'única cosa que canvia entre les dues plaques és la identitat: `led_count()` és **una constant de
compilació**, i per això el driver del llum té el frame de la mida de la seva tira i de cap altra —1
llum i 24 entrades al Lite, 25 llums i 600 al Matrix (2 400 B), que és el que diu `docs/75`.

Conseqüències, que són el motiu de fer-ho així:

- **Cap fitxer del projecte no porta `#ifdef` de placa.** Ni el nucli, ni la capa de Lua, ni les
  proves. Si un fitxer ha de saber on corre, pregunta al HAL.
- **No es poden veure les dues plaques alhora**: el camí d'inclusió d'un entorn no conté la carpeta de
  l'altra, i `lib_ignore` no compila l'altra.
- `board/identity.h` **no inclou res**, perquè qualsevol fitxer de la banda el pugui fer servir.
  `board/impl.h` és el que arrossega les capçaleres de la placa i de la PAL, i només l'inclou
  `src/board_link.cpp` (vegeu `80 · Compilació i proves`).

## Interfícies de la PAL

```cpp
struct Event { uint16_t type; uint16_t a; int32_t b; void* ptr; };  // POD, mida fixa

class IQueue {
 public:
  virtual bool push(const Event&, uint32_t timeout_ms) = 0;
  virtual bool pop(Event&, int32_t timeout_ms) = 0;    // -1 = indefinidament
};

class IConsole {
 public:
  virtual size_t write(const void* data, size_t len) = 0;
  virtual size_t read(void* data, size_t len) = 0;     // no bloqueja
  virtual bool   ansi() = 0;                           // el terminal entén ANSI?
};

class ILog { public: virtual void write(Level, const char* line) = 0; };

int64_t pal_millis();
void    pal_reboot();
uint32_t pal_random();
```

La consola de la PAL dona **bytes**, no línies. Qui parteix les línies, qui mira la seqüència de
sortida i qui fa l'eco és el nucli, perquè ho ha de fer igual a totes les plaques.

## Taula de capacitats

| Capacitat | ATOM (v1) | Emulador | Notes |
|---|---|---|---|
| `console` | ✓ | ✓ | UART a 115200 / stdin-stdout cru |
| `ansi` | ✓ | ✓ | Depèn del terminal que hi hagi a l'altra banda |
| `display` | — | — | Reservat |
| `input` | — | — | Reservat (el botó no s'implementa encara) |
| `led` | — | — | El maquinari hi és; la implementació no |
| `imu` | ✓ només al Matrix | ✓ | MPU6886 a la I2C interna; el Lite no en té i la crida respon `E_UNSUPPORTED` |
| `littlefs` | ✓ obligatori | ✓ obligatori | Muntat a l'arrencada a totes les bandes: sense ell no hi ha apps |
| `sd` | — | opcional | Depèn de si existeix el directori |
| `nvs` | ✓ | ✓ | Partició NVS / directori de fitxers |
| `rtc` | — | ✓ | L'ATOM no té xip amb pila: l'hora ve del SoC i es perd |
| `rtc_wake` | — | — | Despertar des de son profund |
| `net_wifi` | — | — | El maquinari hi és; la implementació arriba a M5 |
| `net_http` | — | ✓ | A l'emulador, per la xarxa del PC |
| `battery` | — | — | L'ATOM pelat no en té |
| `sleep_light` | — | — | Pendent |
| `sleep_deep` | — | — | Pendent |

## Fitxa: M5 ATOM (placa de proves)

| | |
|---|---|
| Nom (`sys.board`) | `m5stack-atom`, el de `board =` a `platformio.ini` |
| Xip | ESP32-PICO-D4, 240 MHz |
| Flash | 4 MB |
| RAM | 320 KB, **sense PSRAM** |
| Pantalla | No |
| Entrada | Un botó a **GPIO39** (entrada només, sense pull-up intern: la resistència la porta la placa i prémer el porta a GND), amb **10 ms** de rebot, el mateix que fa servir la llibreria del fabricant (`Button(39, true, 10)`) |
| LED | Un **WS2812** a **GPIO27**: un sol fil, 800 kHz, ordre **GRB**, 8 bits per color. El del Matrix són **25 WS2812C de 2020** al mateix pin, i el contracte ja els preveu: `count()` diu quants n'hi ha i `pixel()` en pinta un. **Sostre del canal** (`max_value()`): 255 al Lite i **20 al Matrix**, que és el número de M5Stack —amb aquest panell, més brillantor escalfa els LEDs i l'acrílic i els pot fer malbé—; el sistema no deixa passar-hi res (docs/30) |
| IMU | **El Lite no en té.** El Matrix porta un **MPU6886** de sis eixos (acceleració i gir, sense brúixola) a la **I2C interna**, SDA a **GPIO25** i SCL a **GPIO21**, a **100 kHz** i a l'adreça **0x68**; `WHO_AM_I` (0x75) ha de contestar **0x19**. És una lectura de 14 octets seguits des de 0x3B: acceleració a **±2 g** (16 384 comptes per g), gir a **±250 °/s** (131 comptes per grau per segon, que és el mateix rang) i temperatura a **326,8 comptes per grau** amb **+25 °C** de desplaçament. El que no fa és dir **quant** val un compte de debò en aquesta placa: això és un número mesurat i encara no ho està (pendent, docs/90) |
| Expansió | Un port Grove I2C (G26/G32) |
| Rellotge | Només el del SoC: l'hora es perd quan es treu l'alimentació |
| Consola | Pont USB-sèrie, `/dev/ttyUSB*` |
| Capacitats a la v1 | `console`, `ansi`, `littlefs`, `nvs` |
| Capacitats avui | `console`, `ansi`, `littlefs`, `nvs`, `rtc`, `rtc_wake`, `net_wifi`, `net_http`, `net_prov`, `sleep_light`, `sleep_deep`, `led` i `input` — `caps` respon **13 de 17** al Lite i **14 de 17** al Matrix, que hi afegeix `imu` (31/10) |

Conseqüències que aquesta placa imposa:

- És el **pitjor cas de memòria**: tot el que es mesuri aquí és el terra del sistema.
- És el **cas sense pantalla**: tots els camins de «capacitat absent» es proven aquí primer.
- **El panell s'escalfa si se'l passa.** La fitxa de M5Stack diu que amb aquest panell la brillantor no passi de **20** (de 255) perquè els LEDs de 2020 i l'acrílic es poden fer malbé. Per això el màxim d'un canal és una constant de la identitat de la placa (`board::led_max()`) i no un gust: la política del llum el fa complir abans que res arribi al driver, i l'emulador té el mateix sostre perquè el PC no amagui un límit que la placa té (docs/30, docs/85).
- **El llum i el botó són de debò i ho diuen.** El LED s'escriu amb el `neopixelWrite` del core (RMT,
  tick de 100 ns, 0,4 µs i 0,8 µs, GRB) i no s'ha escrit cap controlador: **24 bits a 1,25 µs són
  **30 µs** per frame**, i bloqueja, perquè no hi ha res millor a fer en aquells 30 µs. Al Matrix són
  els mateixos 24 bits per llum, vint-i-cinc cops, en **un sol** frame d'RMT: **750 µs**, també
  bloquejant, i una escriptura per llum no valdria perquè cada escriptura és un frame i la tira es
  quedaria a la primera. El botó es
  llegeix a cada torn del nucli (cada **20 ms** mentre una app viu), o sigui que **no cal cap
  interrupció**: una premuda de menys d'un torn no es veu, i un dit no baixa de 50 ms.
- **El processador va a 160 MHz d'arrencada** i es pot dur a **240 o a 80**, mai per sota:
  `setCpuFrequencyMhz` del core (que és qui mou també l'APB i el tick de FreeRTOS) s'usa amb la
  guarda de 80, perquè per sota l'APB baixa i **cap perifèric es reconfigura** — ni el port sèrie —,
  i el `Serial` és l'única interfície d'aquesta placa. El **DFS automàtic no es pot fer**:
  `CONFIG_PM_ENABLE` no està activat al framework precompilat, o sigui que pujar i baixar sol queda
  fora i el canvi és sempre una decisió de qui el demana (docs/30, docs/90).
- **Sap dormir: a `kLight` i a `kDeep`, i es desperta pel temporitzador del RTC i pel botó.** El
  rellotge de paret sobreviu al son profund perquè l'IDF el desa a la memòria del RTC i nosaltres
  hi guardem la marca `RTC_DATA_ATTR`; l'`esp_timer_get_time` segueix comptant, i per això en tornar
  el sistema sap quant s'ha desviat. El RTC d'aquest xip és el **RC intern** (no hi ha cristall de
  32 kHz a la placa): el drift és real i **és el número que M6 ha de mesurar** (docs/90).
- **El port sèrie no el desperta**: `esp_sleep_enable_uart_wakeup` existeix a `esp_sleep.h`, però un
  caràcter que arribi mentre dorm es perd igualment. És la raó per la qual el sistema **no s'adorm
  sol**: una placa sense pantalla que s'adorm quan ningú escriu es menjaria les tecles de la persona
  (docs/90).
- El Grove és I2C lliure: si cal provar el camí del xip RTC de debò, s'hi pot posar un mòdul sense
  canviar de placa.

## Fitxa: emulador (PC)

| | |
|---|---|
| Nom (`sys.board`) | `emulator` (no és una placa i no té línia `board =`) |
| Plataforma | POSIX (Linux) |
| RAM | La del PC, però **limitada al mateix sostre que la placa** |
| Fitxers | Un directori del disc (`emulator/`) amb `littlefs/`, `sd/`, `nvs/` |
| Rellotge | El del PC, o virtual (`--time-scale`, `--now`) |
| Consola | stdin/stdout en mode cru |
| Xarxa | **De mentida**: connecta a res en `--wifi` ms, i la pàgina i el provisioning també són falsos (docs/85) |
| Energia | **De mentida i ho diu**: no hi ha bateria, i dormir vol dir que el rellotge virtual salta en comptes del PC |
| Pantalla i entrada | No |

Detalls a `85 · Emulador`.

## Afegir una placa

1. Si la placa no compartix HAL amb cap altra, copiar `lib/hal_atom/` a `lib/hal_<id>/` i canviar-ne el
   nom al `library.json`; si compartix el HAL (com el Matrix amb el Lite), no es copia res.
2. Escriure `lib/board_<id>/include/board/identity.h` amb `board::id()` —el nom de `board =` si
   PlatformIO en té una placa i el de la placa si no—, `board::emulator()` a `false` i
   `board::led_count()` amb els llums que té de debò. Escriure `impl.h` amb les capçaleres d'aquesta
   placa i de la seva PAL.
3. Implementar `IPlatform` i les interfícies que la placa tingui de debò. Les que no, `nullptr`.
4. Omplir el `BoardSpec`: identificador, flash, RAM, sostre de memòria per app, baud de consola.
5. Afegir un `[env:<id>]` a `platformio.ini` amb el seu `-Ilib/board_<id>/include`, el seu `lib_ignore`
   (totes les altres plaques) i la PAL que correspongui.
6. Afegir la fitxa en aquest document i la fila a la taula de capacitats.
7. Dir la veritat a la fitxa: què s'ha provat i què no. Una placa que només compila no «funciona».

## Regles per a qui escriu un controlador

- **Mai** cridar `lua_*`.
- **Mai** bloquejar la tasca del nucli més del que costa una operació de maquinari; el que triga va
  a una tasca auxiliar i torna com a esdeveniment o promesa.
- **Mai** reservar memòria dinàmica: buffers fixos, i si no n'hi ha, error.
- Els errors es tornen com a valor, no s'amaguen.
- Una lectura de maquinari no es repeteix en bucle dins d'una crida: si el xip no respon, es diu.
