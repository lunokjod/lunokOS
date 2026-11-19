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

| Interfície | Què abstrau | Lite | Matrix | Paper | T-Koala | Emulador |
|---|---|---|---|---|---|---|
| `IClock` | hora de paret, rellotge monòton, **un sol** despertador, i tornar a prendre l'hora al xip | ✓ | ✓ | ✓ (**amb la BM8563**: l'hora sobreviu al cable, el xip arma el despert i corregeix el comptador després de cada son) | ✓ | ✓ |
| `IStorage` | un volum muntat: stat, list, open, read, write, seek, close, remove, mkdir, rename, space, **`flush`** (0.189.18: el que el backend tingui a mig fer; a l'emulador és un `sync()` del PC i a l'ESP32 no n'hi ha cap d'enllaçable, perquè cada fitxer es tanca dins la crida que l'escriu) | ✓ | ✓ | ✓ | ✓ (LittleFS i targeta) | ✓ |
| `IKeyValue` | claus i valors, exposats com a fitxers a `/nvs`, i **`commit`** (0.189.18: la baixada el demana abans de tallar el corrent, i el backend de la placa ja el fa a cada `set`) | ✓ | ✓ | ✓ | ✓ | ✓ |
| `INet` | la ràdio: connectar, escanejar, provisioning, NTP i una pàgina | ✓ | ✓ | ✓ (provat a la placa) | ✓ (el mateix codi; pendent de provar a la placa) | **la ràdio és de mentida i la pàgina no**: `curl` la baixa de debò (docs/85) |
| `IPower` | bateria, son, **apagar** (tallar el corrent de debò), velocitat del processador, i si el son profund és una **apagada**; **`can_power_off()`** (0.189.18) diu si la placa té el corrent a la mà **sense intentar-ho**, perquè la baixada no gasti el repòs d'una targeta en una placa que no es pot apagar | ✓ (son i velocitat; **no es pot apagar**, no té el seu corrent) | ✓ (el mateix) | ✓ (bateria i son; el son profund apaga; `power_off` fa **la seqüència del fabricant**: desarma el xip del rellotge, espera 600 ms, baixa la **GPIO23** (panell), la **GPIO5** (ports), la **GPIO2** (corrent principal) i posa el xip a dormir profund, o sigui que només el botó la torna a encendre) | ✓ (son i velocitat; **no hi ha indicador de bateria** i no es pot apagar) | de mentida (el model del Paper acaba el procés; l'ATOM diu que no) |
| `ILed` | un llum o una tira: quants n'hi ha, el sostre del canal, pintar | 1 | 25 | — (no en té) | 1 (LED verd a GPIO5, només encès/apagat) | el del model: 1, 25, 0 o 1 |
| `IInput` | les tecles: quantes n'hi ha, si una és premuda i quant fa | ✓ (1) | ✓ (1) | ✓ (3) | — (cap tecla; el botó és RST) | ✓ (les del model) |
| `IImu` | acceleració, gir i temperatura del xip inercial | — | ✓ | — | — | de mentida |
| `ISensor` | l'aire: temperatura i humitat relatives | — | — | ✓ (**SHT30**, a la I2C interna; es mesura quan es demana i no té estat) | — | de mentida (el model del Paper) |
| `ISurface` (sobre un `IDisplay` buit) | la pantalla: la mida, els modes de pintat i l'espera de la tinta | — | — | ✓ (e-ink **IT8951E**, amb `init`, `du`, `gc16`, `gl16`, `glr16`, `gld16`, `du4` i `a2` i la línia `engine flag` del log) | — | ✓ (el vidre del model, amb `--display` i la finestra amb `--gui`, docs/85) |

El contracte sencer, amb totes les funcions i els comentaris que les expliquen, és a
`lib/hal/include/hal/hal.h`. **Quan aquest document i el capçalari no diguin el mateix, mana el
capçalari**: aquí hi ha el perquè de les decisions, no una còpia de les signatures.

```cpp
class IPlatform {
 public:
  virtual const char* id() const = 0;      // "m5stack-atom", "m5stack-atom-matrix"; a l'emulador, el model
  virtual const char* family() const = 0;  // "esp32": el que un manifest vol dir amb "esp32"
  virtual bool emulator() const = 0;       // cert només al PC
  // Les plaques a les quals aquesta banda respon: el seu `id()` i prou, també a l'emulador,
  // que respon el model que se li ha dit que sigui. És contra això i la família que es
  // compara `boards` (docs/40).
  virtual uint32_t board_count() const { return 1; }
  virtual const char* board_name(uint32_t index) const {
    return (index == 0) ? id() : nullptr;
  }
  // **L'alimentació de la placa, agafada abans de qualsevol altra cosa** (0.189.24): una placa que s'aguanta el corrent
  // amb un pin rep aquesta crida a la primera línia de l'arrencada; la que no, no fa res (docs/50 § «Apagar tot el que es
  // pot apagar»). És idempotent i no pot fallar.
  virtual void hold_the_power() {}
  virtual const Caps& caps() const = 0;
  virtual uint32_t heap_limit_kb() const = 0;
  virtual IClock* clock() = 0;
  virtual IStorage* storage(const char* mount) = 0;   // "/littlefs", i "/sd" on hi hagi ranura
  virtual IKeyValue* nvs() = 0;
  virtual INet* net() = 0;
  virtual IPower* power() = 0;
  virtual ISurface* display() = 0;   // nullptr a les plaques sense vidre (els ATOM i el T-Koala)
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
| `id()` | el nom de la placa: `m5stack-atom`, `m5stack-atom-matrix`, `m5stack-paper` i `ttgo-t-koala`; a l'emulador, el model que se li ha dit que és |
| `family()` | el xip: `esp32`. És el que un manifest vol dir amb `"boards": ["esp32"]`, i val per a les cinc bandes |
| `emulator()` | cert només al PC, i és el que diu que la placa de sota `id()` és de mentida |
| `led_count()` | quants llums: 1 al Lite, 25 al Matrix, cap al Paper i 1 al T-Koala. **A l'emulador no hi és**: el nombre de llums el diu el model (`emu::model().lights`: 1 al Lite i al T-Koala, 25 al Matrix i 0 al Paper) |
| `led_max()` | el sostre d'un canal: 255 al Lite i al T-Koala, 20 al Matrix i a l'emulador |
| `has_imu()` | si la placa porta sensor inercial |
| **`LUNOK_GLASS`** | **si aquest build porta vidre**, i és un `#define` i no una funció: 1 al Paper i a l'emulador, 0 als dos ATOM, a l'ESP32 de DevKit i al T-Koala |

L'emulador té una cosa que cap placa no té: **`models_count()`, `model(i)` i `default_model()`**, els
models que sap ser —`m5stack-atom`, `m5stack-atom-matrix`, `m5stack-paper` i `ttgo-t-koala`—. No els respon tots alhora:
la persona li diu quin és (`--board m5stack-paper`, i **cal dir-lo**: l'emulador no tria placa) i `id()` i
`board_name()` responen **aquell**, de manera que una app s'accepta o es refusa al PC igual que a la
placa (docs/40, docs/85). `sys.emulator` continua cert: el nom és el d'una placa que existeix i el
maquinari de sota és fals.

El maquinari fals **segueix el model** que l'emulador ha rebut —capacitats, nombre de llums, nombre de
tecles, sensor, indicador de bateria, ranura i FRAM surten tots de `emu::model()`—, perquè una app que
demani més del que té la seva placa no ho trobi a l'emulador i es cregui un número de mentida. L'únic que
encara és comú als quatre models és la **forma de les tecles** (el dial compost de la Paper: cara
esquerra, `centre`/`up`/`down`), i això és el que queda a M8 (docs/90).

`board/impl.h` arrossega les capçaleres de debò (el HAL d'aquesta placa i la seva PAL), i només
l'inclou `src/board_link.cpp`.

### `LUNOK_GLASS`: el vidre i el que penja d'ell (0.179.9)

**Un `#define` i no una funció, perquè la pregunta es fa abans del compilador.** `LUNOK_GLASS` és l'única cosa de la
identitat que no pot ser una funció: el que decideix és **quines unitats de traducció existeixen**, i una funció no pot
treure un fitxer d'un build. La pregunta de debò —si la placa que corre té pantalla— continua sent la del HAL
(`kCapDisplay`, i `sys.caps` la contesta), i `hal/hal.h` inclou `board/identity.h` i peta amb un `#error` si una identitat
no diu quin dels dos valors és, perquè una placa nova que s'oblidi no es compili en silenci com un sistema sense pàgina.

| Què | Amb `LUNOK_GLASS 1` | Amb `LUNOK_GLASS 0` |
|---|---|---|
| `kernel/ui.cpp`, `font.cpp`, `book.cpp`, `splash.cpp`, `display.cpp`, `bar.cpp`, `touch.cpp`, `orientation.cpp` | tot el fitxer | el fitxer és buit |
| les llibreries `epub`, `zip`, `inflate`, `ttf` i `img` | al build | **no s'enllaça ni un byte** (`qr` sí: un QR s'imprimeix en text) |
| el pont de Lua | `ui`, `canvas`, `keyboard`, `font` i `epub` | les taules **no existeixen** (`nil`), i `sys`, `fs`, `net`, `button`, `qr`, `json` i la resta queden |
| els comandaments de la consola | tots | tots, i els de la pàgina contesten el que és veritat: `splash` diu `no glass on this board` i `cfg keyboard` diu que aquest build no en porta cap; `lua`, `run`, `apps`, `mem`, `net` i la resta no canvien |

**I el que el sistema encara crida d'una pàgina que no hi és, contesta des d'un sol fitxer** (`kernel/src/no_glass.cpp`):
un `attach()` que no fa res, una barra que no diu res, un dit que no hi és. És el que fa que el shell i el nucli no hagin de
preguntar de quina placa són a cada línia: el camí sense vidre és el mateix codi amb les respostes curtes.

**I cada porta nova del shell hi ha de tenir la seva resposta, o les quatre plaques no enllacen** (0.189.141): `display.cpp`
és tot dins `#if LUNOK_GLASS`, o sigui que una crida que `shell.cpp` fa a `display::` —`surface()`, la que diu si hi ha un vidre
on tornar amb la casa del panell— existeix al codi i **no té definició** en aquest build: compila i no enllaça, i el que es veu
és `undefined reference to 'kernel::display::surface()'` en un `pio run` de l'Atom. Les sis construccions són el que ho vigila
(docs/75, docs/80).

**El preu, mesurat** (11/11, `pio run` de les cinc plaques, `firmware.bin`): l'ATOM Lite i l'ESP32 de DevKit passen de
**2 302 464 B a 2 105 456 B** (−197 008 B, un 8,6 %), el Matrix de 2 302 576 a 2 105 536 i el T-Koala de 2 378 240 a
2 172 960 (−205 280). La ranura de 2 621 440 B passa de **319 KB lliures a 516 KB**: la meitat del que ocupava la pàgina
que aquestes quatre plaques no poden ensenyar (docs/75).

**I es pot veure al PC**: la identitat de l'emulador és `#ifndef LUNOK_GLASS / #define … 1`, o sigui que
`PLATFORMIO_BUILD_FLAGS=-DLUNOK_GLASS=0 pio run -e emulator` fa un emulador sense pàgina —el mateix nucli, el mateix
shell, el mateix intèrpret— i és la manera de veure què arrenca una placa sense vidre sense gravar un xip (docs/80, docs/85).

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

**El T-Koala és una placa de PlatformIO pròpia**, `boards/ttgo-t-koala.json`, perquè el WROVER-B no és
cap dels mòduls que la plataforma porta amb PSRAM: el fitxer diu la flash QIO de 40 MHz, la PSRAM QSPI,
`BOARD_HAS_PSRAM` i el remei de la cache del xip. La identitat és `ttgo-t-koala` i el HAL és
`lib/hal_tkoala/`, que només afegeix el que és seu: **LED verd a GPIO5** (un LED normal, no un WS2812:
s'encén o s'apaga, i el color que l'app demana no hi cap), **cap tecla que el firmware pugui llegir** (l'únic botó és RST, que reinicia el xip) i **targeta microSD als pins HSPI** —CS 15, SCK 14, MOSI 13, MISO 12—,
que és el grup que la serigrafia del fabricant posa al costat de la ranura. El PSRAM és el del
WROVER-B: el xip en té 8 MB i l'ESP32 n'hi mapeja 4, i `heap_limit_kb()` contesta 4 096 només si el xip
respon que en té; una versió WROOM-32 sense PSRAM cau a zero i no promet memòria que no hi és.
**La bateria no s'anuncia**: la placa té carregador i connector, però el divisor no està documentat
pels fabricants i cap lectura s'ha pres al maquinari; el full de camp de docs/95 és on es tancarà.

**I els muntatges són els que són**: el `/rtc` del xip, la flash a `/littlefs` i la targeta a `/sd`, i res
més. Aquesta placa **no té FRAM**, i el camí que el Paper fa servir —«si no és la flash, prova la targeta»—
li donava a `/fram` la targeta sencera: una memòria crua que no hi existeix, amb els fitxers d'algú sota un
nom que promet 256 bytes. `storage()` respon cada muntatge pel seu nom i torna `nullptr` per a la resta, o
sigui que `ls /` i `mounts` no ensenyen ni `fram` ni res que la placa no tingui (docs/70).

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
límit que la placa té. **I el PC els ensenya**: la finestra de l'emulador té la tira de llums a sota del vidre
—cinc per cinc al Matrix, un punt al Lite i al T-Koala— amb el color del marc que la tira ha enganxat
(0.179.17, docs/85).

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
| Cara | `top` (el botó gran de dalt) | `right` (el dial del costat dret de la pàgina) |
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
  comprovar en un segon: per això una app que imprimeixi `imu.read()` dona els números a la consola.

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
- **I la línia s'ha buidat també abans de moure el processador** (0.179.43): la velocitat del port sèrie
  surt del rellotge del bus, i el xip la torna a calcular quan canvia la freqüència. Els bytes que ja són
  a la FIFO i al registre de desplaçament surten a la velocitat de abans i el port els ensenya com a
  **escombraries**, o sigui que la corrupció cau a l'**última línia escrita abans del canvi** i no a la que
  ve després. Es va veure al registre de la placa: `[D][PANEL] paint 60,356 72x76 … (page 108lx␘␂����2,
  76 rows of 36 B)` amb `[I][POWER] cpu 160 MHz from 240` a la línia següent. `move_to` i `set_cpu_mhz`
  buiden la consola (`IConsole::flush()`) abans de cridar la placa, pel mateix motiu que el son.
- **No hi ha DFS.** `CONFIG_PM_ENABLE` no està activat al framework precompilat, o sigui que pujar i
  baixar sol queda fora i la velocitat és sempre una decisió de qui la demana.
- **I les tres velocitats es poden limitar** (0.189.107, l'amo: *«[X] Frequency scaling allowed: [X] 80Mhz
  [X] 160Mhz [X] 240Mhz»*): el governador tria entre les que la persona li deixa —`cfg freq 240 off` deixa el
  sostre a 160 i `cfg freq 80,160` les deixa totes dues— i **el sostre val per a tothom**, també per a un
  finestra que demana el pas de dalt per pintar, perquè un ajust que només és una suggerència no és un ajust.
  L'aritmètica és una sola (`power::allowed_step`): **el pas permès més petit que no quedi per sota del que es
  demana**, o el més gran dels permesos quan no n'hi ha cap per sobre. Un pas que no és cap dels tres es refusa
  i l'última permetuda no se'n pot anar —una placa sense cap velocitat no torna de la resposta— (docs/30,
  docs/60).

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

**L'agafada del corrent va la primera, i això era un error de dos segons** (0.189.24, l'amo: *«El poweroff deixa la placa
tonta? no puc arrencar-la sense punxarla a un usb?!»*): el **GPIO2** s'aguantava des de `caps()`, i `caps()` arriba
**després del splash** —un `gc16` del panell sencer, segons de tinta—. El commutador del SY7088 deixa anar la placa **al
voltant d'un segon** després de prémer la tecla, o sigui que amb la bateria la placa **s'apagava a mitges de la seva pròpia
arrencada** i només el cable la sostenia. Ara es fa **abans de qualsevol altra cosa**: a `setup()` de la placa i a la
primera línia de `kernel::boot()`, que és com ho fa la pròpia biblioteca del fabricant (`M5EPD::begin()` fa
`enableMainPower()` a la primera instrucció). És `IPlatform::hold_the_power()`, una crida que a l'emulador només compta
(`emu::held_the_power()`) i que a les plaques que no aguanten el seu corrent amb un pin no fa res.

**I encendre-la quan està apagada és una tecla, no un GPIO**: la placa Paper no té cap manera d'engegar l'ESP32 des dels
botons, perquè **quan està apagada l'ESP32 no té corrent**. Les dues úniques entrades són el **PWR del dial, premut i
aguantat uns 2 s** (ho diu el manual del fabricant: *«Power on: Press and hold the power button (rotary switch)»*), que és
un circuit, i **l'alarma del BM8563**, que arriba al mateix circuit d'alimentació i és el que fa servir `M5.shutdown(5)`
de l'exemple oficial `RTC_WakeUp`. **Amb el cable posat és una altra història** (0.189.24): la placa no es pot apagar
—ho diu el fabricant— i el que la deixa callada és el **son profund**, que abans s'armava sense cap despertar i deixava
una placa que només un reset tornava a engegar. Ara el son arma **`ext0` sobre el GPIO38** (la tecla PWR, que és del domini
RTC), i prémer-la la torna a despertar; amb la bateria el corrent ja ha caigut abans d'aquesta línia i l'armat no vol dir res.
Un `esp_sleep_enable_ext1_wakeup` sobre GPIO37/38/39 seria un **son amb el corrent aguantat** (una tecla desperta i la
pàgina torna a començar), que és una altra cosa que `poweroff`, i és el que fa el **`deepsleep`** de la 0.189.25: la placa
**no baixa el GPIO2**, adorm el panell i baixa la GPIO23 i la GPIO5 i arma **`ext0` sobre el GPIO38**; una premsa de la
tecla del dial la torna a engegar. **El repòs costa**: el xip i vuit megues de PSRAM en self-refresh són uns quants
mil·liamps i no zero, o sigui dies de bateria i no mesos, i per això és una comanda a part (`deepsleep`, la lluna de la
targeta, `sys.deepsleep()`) i no el que fa `poweroff`.

**I des de la 0.190.15 el xip del rellotge no es desarma en aquesta son**: la placa només en té una (`IPower::sleep`) i el
que decideix si el rellotge queda armat és **l'hora** que li han dit, no qui la demana. Amb una alarma a la cua —també si
és una persona qui demana la son— el rellotge es queda armat i el seu `INT` torna a engegar la placa si el corrent se'n va
de debò, i el temporitzador del xip la torna si el corrent s'aguanta; sense cap alarma el rellotge es desarma, perquè ningú
no ha demanat que torni sola (docs/60). `sleep_with_the_keys` ja no existeix al HAL: una placa amb tecla dorm amb
`sleep(kDeep, 0)`.

**I la tercera porta és el dit, i la diu el `IPower` d'aquesta placa** (0.190.16): `wake_on_touch()` /
`set_wake_on_touch()` són la pregunta i el commutador, apagat per defecte. Amb el commutador encès, la baixada del Paper **no talla
`kPanelPowerPin`** —el GT911 viu en aquell rail i un controlador sense corrent no sent ningú—, reposa el vidre i arma `ext1` sobre el
**GPIO36** amb `ESP_EXT1_WAKEUP_ALL_LOW`: el GT911 porta la línia avall quan un escombrat té un marc, i amb un sol pin «tots baixos» és
«aquest baix». **I la línia no s'arma fins que el xip l'ha deixada anar** (M5Unified #91): el GT911 la té baixa mentre hi ha un marc que
ningú no ha llegit —i la torna a polsar mentre l'amfitrió no llegeix—, o sigui que una son armada amb la línia ja baixa desperta a
l'instant, i una altra vegada a cada intent. La baixada llegeix l'estat i l'escriu a zero (`ITouch::flush`, el `M5.TP.flush()` de
l'original) i espera que GPIO36 sigui alt abans d'armar; si no ho és al cap d'un segon, el dit no és una porta d'aquella son i el rail
del panell es torna a tallar (llevat d'un despert per una alarma, que el guarda per la imatge). El que costa és el corrent del panell i
del tàctil durant tota la son, i per això ve apagat. `ext0` (la tecla) i `ext1` (el dit) conviuen: són dues condicions del bloc RTC i la placa es desperta amb la que
arribi primer. **I el xip diu quina de les dues ha estat** (0.190.19): `ESP_SLEEP_WAKEUP_EXT1` és `WakeCause::kTouch` i `EXT0` és el
botó, o sigui que el registre del despert diu «un dit al vidre» o «la tecla del dial» i no les dues coses alhora.

**I una baixada que no ha tocat el vidre no baixa el rail del panell** (0.190.19): `IPower::sleep(mode, at, keep_the_panel)` és el
paràmetre que ho diu, i el Paper el rep quan el run ha estat un despert per una alarma sense ningú al davant. El controlador del panell
es queda amb el seu estat i la imatge que ensenya —el que costa és el corrent del repòs del controlador— o sigui que el despert següent
no ha de reinicialitzar el panell ni repintar-lo. Les tres
baixades de la placa queden així: **`poweroff`** talla el corrent (mesos, i només la tecla del dial), **`sleep <hora>`**
talla el corrent amb una alarma que la torna a engegar, i **`deepsleep`** deixa el corrent posat amb una tecla per tornar.
El que **no** es pot fer és despertar un xip que no té corrent: això no ho fa cap placa.

## La consola

Els primers bytes que surten per un terminal a 115200 **no són nostres**: abans de `Serial.begin()` el
xip ja ha imprès el que imprimeix la seva ROM (la línia de reset i l'`ets Jun 8 2016…`) a **74880
bauds**, i un terminal a una altra velocitat en llegeix una pila de bytes `0x00`. El que sí que és nostre
és netejar **l'entrada**: `pal::init()` buida el que hagi arribat i el nucli espera que la línia calli
abans de llegir la primera ordre. **No espera un rellotge** (0.190.21): la UART compta els flancs del
pin RX, ensenya el nivell de la línia i guarda els bytes que ningú no ha llegit, i
`IConsole::wait_quiet()` torna quan no hi ha hagut cap flanc ni cap byte i la línia és alta durant
~12 ms —el màxim del RX timeout del xip, 126 símbols a 115200—. El quart de segon és només el
**sostre** d'una línia que no calla; un `break` (línia baixa) no compta com a quiet, i un byte a la
FIFO o a l'anella del driver tampoc. Amb bateria, el pont de l'altra banda no té corrent i el pin
queda flotant: l'espera li posa el pull-up intern, que és el que fa que «alta i quieta» sigui
«ningú no parla».

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
  virtual bool wait_quiet(uint32_t quiet_ms, uint32_t cap_ms);  // la línia ha callat; `cap_ms` és el sostre
  virtual bool ansi() const = 0;
};

class ISystem {
 public:
  virtual IQueue* queue() = 0;
  virtual IConsole* console() = 0;
  virtual int64_t millis() const = 0;      // monòton, mai enrere
  virtual void reboot() = 0;
  virtual bool reboot_requested() const = 0;
  virtual uint32_t random() = 0;           // el soroll del xip, remenat (vegeu «Els números de la placa»)
  virtual HeapInfo heap() const = 0;       // free_bytes, min_free_bytes
};

bool init();  void shutdown();  ISystem& system();
void set_random_seed(uint32_t seed);      // zero és «sembra'm tu»; a la placa no fa res
```

La consola de la PAL dona **bytes**, no línies. Qui parteix les línies, qui mira la seqüència de sortida
i qui fa l'eco és el nucli, perquè ho ha de fer igual a totes les bandes.

## Taula de capacitats

| Capacitat | Lite | Matrix | Paper | Emulador | Notes |
|---|---|---|---|---|---|
| `console` | ✓ | ✓ | ✓ | ✓ | UART a 115200 / stdin-stdout cru |
| `ansi` | ✓ | ✓ | ✓ | ✓ al PC | Depèn del terminal que hi hagi a l'altra banda |
| `display` | — | — | ✓ amb l'IT8951 contestant | ✓ al model de la Paper (`Model::glass`), i `--no-display` el treu | El vidre: una app hi dibuixa i el nucli no hi pinta res de seu. S'anuncia quan `display()` no és nul —el controlador va contestar a l'arrencada—, i **a l'emulador el diu el model** (docs/50, docs/85) |
| `input` | ✓ | ✓ | ✓ | ✓ | Un botó a l'ATOM, tres tecles a la Paper i, a l'emulador, les del model |
| `touch` | — | — | ✓ amb el GT911 contestant | ✓ al model de la Paper (`Model::touch`), i `--no-touch` el treu | El dit que travessa el vidre. S'anuncia quan el xip contesta al probe (0x5D o 0x14): **a l'emulador quan el model en té** (la Paper; un ATOM no en té de cap manera), i el dit pot ser el **ratolí de la finestra** de `--gui`. El que l'emulador simula —la velocitat de mostra, el cost del bus i el marc que es pren— és a `docs/85` |
| `led` | ✓ | ✓ | — | ✓ al model que en té (no al del Paper) | El Paper no té cap llum |
| `imu` | — | ✓ | — | ✓ al model del Matrix | El Lite no en té i la crida respon `E_UNSUPPORTED` |
| `littlefs` | ✓ | ✓ | ✓ | ✓ | Obligatori: sense ell no hi ha apps |
| `sd` | — | — | ✓ amb targeta | ✓ amb `--sd`, si el model té ranura | Targeta SD per SPI (FATFS), muntada a `/sd` a **10 MHz**. Només s'anuncia si una targeta de debò ha muntat: al Paper quan n'hi ha una al slot, a l'emulador quan se li diu `--sd <dir>` **i** el model imitat la té (el Paper; els ATOM no en tenen) |
| `nvs` | ✓ | ✓ | ✓ | ✓ | Partició NVS / directori de fitxers |
| `rtc` | ✓ | ✓ | ✓ | ✓ | Dona l'hora mentre hi ha corrent. **Al Paper és de debò**: la BM8563 amb pila de botó es llegeix a l'arrencada i s'hi escriu a cada `set_epoch`, així que `time.caps().source` diu `"rtc"` i l'hora sobreviu a treure el cable (docs/60). A l'ATOM i al Matrix és el **domini RTC del xip**, que la perd |
| `rtc_wake` | ✓ | ✓ | ✓ | ✓ | Despertar pel temporitzador del RTC (i pel botó, a l'ATOM) |
| `net_wifi` | ✓ | ✓ | ✓ | ✓ | La ràdio de debò / la de mentida (docs/85); la del Paper és la del xip i **està provada a la placa** (31/10) |
| `net_http` | ✓ | ✓ | ✓ | ✓ | `esp_http_client` amb TLS / la pàgina falsa |
| `net_prov` | ✓ | ✓ | ✓ | ✓ | El component d'Espressif / l'AP de mentida |
| `battery` | — | — | ✓ | ✓ si el model té indicador, i **sense cap bandera** | El Paper la llegeix del divisor de GPIO35 i l'emulador, amb un model que en té (**el Paper; els ATOM no**), de **la bateria d'aquesta màquina passada per la mateixa corba** —el percentatge, no la tensió— o de `--battery-mv` si la línia de comandes en diu un, que mana. Només s'anuncia quan la lectura és un número que pot ser una cel·la; `charging` és **fals sempre** perquè cap d'aquestes bandes no té un pin per preguntar-ho (docs/30, docs/85) |
| `sleep_light` | ✓ | ✓ | ✓ | de mentida | Al PC el rellotge salta en comptes d'esperar |
| `sleep_deep` | ✓ | ✓ | ✓ | de mentida | A la placa, tornar vol dir arrencar |

`caps` i `sys.caps()` diuen el mateix i surten d'aquesta taula. Cada placa d'ESP32 **parteix de `esp32_caps()`** (10 de 19: consola, ANSI, NVS, rellotge, despert del RTC, els dos sons i la ràdio amb provisioning i pàgina) i afegeix només el que té de seu: el sistema de fitxers que munta, i doncs **13 de 19** al Lite, **14 de 19** al Matrix (que hi afegeix `imu`), **15 de 19** al Paper amb el SHT30 contestant, el vidre i el GT911, i sense bateria ni targeta —**16** amb la bateria llegida **o** la targeta, **17** amb totes dues, que és el màxim d'aquesta placa— i **el que diu el model** a l'emulador: **13 de 19** al de l'ATOM (**12** si el terminal no és ANSI), **14** al del Matrix i **17** al del Paper (que ja hi té la targeta `emulator/sd`, el vidre, el GT911 i **la bateria que es presta sola**; **16** si el terminal no és ANSI), que baixa a **15** amb `--no-touch --no-display`. I les dinou existeixen en algun model, comptant-hi l'emulador.

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

## Fitxa: ESP32 de DevKit (la placa del mínim)

| | |
|---|---|
| Nom (`sys.board`) | `esp32dev` |
| Entorn | `esp32`, amb la placa `esp32dev` que la plataforma ja porta |
| Xip | ESP32 estàndard (WROOM-32), 240 MHz, arrenca a 160 |
| Flash | 4 MB |
| RAM | 320 KB, **sense PSRAM** |
| Pantalla | No |
| Entrada | **Cap tecla**: la família llegeix la **GPIO39**, i a un DevKit no hi ha res connectat |
| Llum | **Cap**: la família pinta a la **GPIO27**, i no hi ha cap xip que ho escolti |
| Sensor | Cap |
| Rellotge | Només el del SoC |
| Consola | La del pont USB-sèrie |

**Per a què hi és: per provar el mínim.** És el `esp32dev` de tota la vida, la placa més corrent que
hi ha, i és el terra de la memòria i de les capacitats: si un canvi no hi cap, no cap enlloc. **No és
la fitxa de cap aparell venut**: només s'ha compilat, no s'ha flashejat mai, i això es diu.

Què hi fa el sistema: el mateix que a l'ATOM menys el que aquesta placa no té —el llum, la tecla, el
sensor inercial i el vidre—. Comparteix `hal_esp32` (que és el HAL de la família) i `pal_esp32`, i el
que és seu és només la identitat, a `lib/board_esp32/`. El camí d'entrada i de sortida d'una app és la
consola, i la sortida del llum no té xip que la rebi: `led.play` escriu a un pin que no va enlloc.

Es mesura amb `pio run -e esp32-devkit` cada cop que es toca la memòria: **107 008 B de RAM estàtica de
327 680 (32,7 %) i 2 081 225 B de flash de 3 145 728 (66,2 %)** (2/11). No té fila a la taula de
capacitats perquè seria la de l'ATOM amb guions a sobre, i diria dues vegades el mateix.

## Fitxa: LilyGO T5 E-Paper S3 Pro (0.190.29, el primer xip que no és un ESP32)

| | |
|---|---|
| Nom (`sys.board`) | `lilygo-t5s3-epaper-pro` (la placa del fabricant és `H752-01`) |
| Entorn | `lilygo-t5s3`, amb la definició del projecte a `boards/lilygo-t5s3-epaper-pro.json` |
| Xip | **ESP32-S3-WROOM-1**, 240 MHz, arrenca a 160 |
| Flash | 16 MB, la taula `partitions_16mb.csv` (4 MB d'app i 11,9 MB de disc) |
| RAM | 320 KB, i **8 MB de PSRAM OPI**, tota mapejable |
| Pantalla | **ED047TC1, 4,7", 960 × 540, setze nivells, en paral·lel i sense controlador** — el driver no hi és encara |
| Entrada | La tecla BOOT del mòdul (GPIO0). El botó `S3` de la placa és a l'expansor PCA9535 i arribarà amb ell |
| Llum | Cap |
| Sensor | Cap (té LoRa SX1262 i GPS, sense driver) |
| Rellotge | **PCF8563** (0x51) amb pila de botó, i el del SoC |
| Bateria | **BQ27220** (0x55) d'indicador i **BQ25896** (0x6B) de càrrega, per I2C |
| Consola | La UART0 del xip, **GPIO43/GPIO44**, que també van al GPS |

**Per què és una fitxa i no una línia a la del Paper**: perquè **el xip no és el mateix**. Fins a la
0.190.29 totes les plaques d'aquest projecte eren ESP32-D0WDQ6 i `hal_esp32` era, de fet, el HAL del
xip *i* de la família alhora. L'ESP32-S3 té els seus propis caps de PSRAM (`esp32/spiram.h` i no
`esp_spiram.h`), el seu propi repartiment d'SPI (`SPI2_HOST` i `SPI3_HOST` són els dos dispositius, i
no hi ha l'`HSPI` del D0WDQ6) i el seu propi joc de fonts de despertar. O sigui que aquesta placa és
**la prova que `lib/hal_esp32` és de la família i no del xip**: el que hi ha escrit és el que els dos
xips comparteixen, i el que no ho és viu a `lib/hal_t5s3/`.

**Què fa avui** (0.190.33): el prompt, les alarmes, la targeta, la NVS, la ràdio, l'hora del PCF8563,
el tàctil GT911, l'indicador de bateria **i el vidre**. El driver del panell és
`lib/hal_t5s3/src/epd47.cpp` i el de l'expansor `lib/hal_t5s3/src/expander.cpp`, i el que hi ha dins
són tres coses que el Paper no havia de fer mai:

1. **El bus paral·lel**: vuit línies de dades i un rellotge amb el perifèric i80 del xip, a vint
   megahertz. **I dues trampes que costen un flash cadascuna**: el bus **no es deixa crear sense un
   `dc_gpio_num`** encara que el panell no tingui línia de comandament —és el GPIO46, el *chip select*
   de la LoRa, que el driver té alt sempre, que és el seu repòs—, i amb `lcd_cmd_bits` a vuit el driver
   envia **un byte d'ordre abans de cada transferència** que cauria al registre de desplaçament i faria
   lliscar la fila quatre píxels. Zero bits vol dir zero cicles d'ordre.
2. **L'alimentació**, que és del **TPS65185** (0x68) però la obre el **PCA9535** (0x20): `EP_MODE` **alt**,
   `EP_OE` alt, i després `WAKEUP`, `PWRUP` i `VCOM_CTRL`; el `POWER_GOOD` de l'expansor; el registre
   d'habilitació a `0x3F`; la **VCOM a 1560 mV** —el valor amb què surt de fàbrica aquesta placa, no els
   1600 del codi d'epdiy—; i el registre de *power good* a `0xFA`, que és l'única cosa que diu que el
   panell es pot pintar. **I la tercera trampa, que és la que va costar la primera llum** (0.190.34): el
   registre de configuració del PCA9535 diu quin pin és **entrada** amb un `1`, i la primera versió del
   HAL li passava la **negació** de la màscara de les entrades. Els cinc pins de l'alimentació
   (`IO1_0..IO1_5`) quedaven com a **entrades** i el `PWR_GOOD` (`IO1_6`) com a **sortida**: el TPS65185
   no es despertava mai, no contestava a `0x68` (`write to 68 failed`) i el pin de bo llegia zero per
   sempre, sense cap error que ho digués. Tres coses en queden: la màscara s'escriu tal com és
   (`0xC400`), **es llegeix de tornada** i es compara —un xip que contesta i no es queda la configuració
   és un error invisible des de fora—, i les portes es tanquen també quan l'alimentació falla a mitges.
   **I la quarta trampa** (0.190.35): el `EP_MODE`/`GMOD` ha d'anar **alt** mentre es pinta. `epd_board_v7`
   d'epdiy el deixa baix perquè és una altra placa; el firmware que aquesta porta de fàbrica —FastEPD,
   `EPDiyV7EinkPower`, `bbepPCA9535DigitalWrite(9, 1); // GMOD on`— el posa alt, i amb el GMOD baix el
   panell no pren els codis de dos bits de cada passada: la font puja, el registre de bo diu `fa`, les
   files surten pel bus (2,7 s de feina) i **la tinta no es mou gens**.
   **I dues més del mateix forat, que són les que van costar el segon i el tercer flash** (0.190.36): el
   `EP_STH` (GPIO41) **no és un pin quiet** —és el «començament horitzontal» i va al perifèric com a
   `cs_gpio_num`, polsat a cada fila, tal com fa `bbepWriteRow`—, i el **`CKV` ha de pujar abans de cada
   transferència**, perquè és el que avança la porta una línia. Amb el `STH` quiet i el `CKV` baix, el bus
   treballa 2,7 s per pintada i no hi ha ni un píxel que rebi el codi que li toca.
3. **La forma d'ona**, que és l'única part que no és codi d'aquest projecte: `t5s3/epd47_waveform.h`,
   la `u8GrayMatrix` de **FastEPD** (Apache-2.0, BitBank Software), que és la mateixa taula que porta
   el firmware de fàbrica de la placa perquè el seu exemple és `epd_board_v7` d'epdiy.

**I el preu de no tenir controlador, dit clar**: una actualització és **la forma d'ona sencera, trenta-
vuit passades** —una per fase, amb el `ROW_START` a l'inici de cada passada i el `LE` a cada fila—, o
sigui que refrescar una pàgina sencera costa **2,5 segons** i no mil·lisegons (mesurat a la placa: 125 µs
per fila, dels quals 96 són el cable a vint megahertz i la resta el perifèric). **El que sí que es pot
retallar, i ja es fa** (0.190.37): com que el nucli marca el rectangle que ha canviat, una pintada
escaneja **només fins a l'última fila marcada** —les files de sobre del rectangle surten amb el codi que
no fa res, perquè la porta s'ha de recórrer des de la primera línia, i les de sota no surten gens— i una
pintada sense res marcat no es gasta. O sigui que una banda estreta prop d'una vora costa una fracció del
que costa una pàgina.

**I les tres palanques de la velocitat** (0.190.38), que es poden provar totes tres juntes i cada una
sola:

1. **La forma d'ona curta**: `du`, `du4` i `a2` són de dues tintes i no necessiten les trenta-vuit
   fases del panell. `kFastPhases` en té sis —dues cap a la via contrària i quatre cap a la de destí,
   que és el que manté el fantasma a ratlla—, i **és d'aquest projecte i no del panell**, dit a
   `epd47_waveform.h`; el `gc16` del nucli, que sí que fa servir la taula del panell, és el que neteja
   el que les curtes deixen. La UI és tota `du` (el nucli només la puja a `gc16` quan la regió és set
   vuitens del vidre), o sigui que aquesta és la palanca que es nota.
2. **El rellotge del bus, en calent**: `set_bus_mhz` torna a obrir el perifèric amb el rellotge nou i deixa
   el quadre, el panell i la font on són. La comanda `panel bus <mhz>` del prompt ja hi era esperant-lo, o
   sigui que 20, 26, 32 i 40 MHz es proven en una sola sessió sense cap flash.
3. **La mesura**: cada pintada diu files, fases, mil·lisegons de cable, mil·lisegons de la resta i els
   megahertz, i amb això se sap quina de les tres ha guanyat.

**I la finestra, que és el que fa que un dígit no repinti una columna sencera** (0.190.40): el rectangle que el
nucli marca es tradueix a coordenades del vidre **senceres** —les quatre cantonades, perquè el gir de 270° inverteix
un eix— i una pintada només dona codi a les **columnes de dins** d'aquell rectangle; la resta de la fila surt amb el
codi que no fa res, i les files de fora no surten. Sense això un `du`, que és de dues tintes, empenyia totes les
nou-centes seixanta columnes de la banda de files cap a negre o blanc i s'enduia els grisos del que hi havia a la vora
—que és exactament el que es veia: pocs tons i tota una columna repintada per canviar un dígit—. El preu del que queda
és el de sempre en tinta: dins de la finestra, un `du` deixa dues tintes i el `gc16` del nucli és el que hi torna a
posar setze.

**I el motor de la diferència** (0.190.41), que és el que fa que una actualització parcial sigui parcial de debò: el
vidre guarda el **quadre que ja hi ha** —259 200 B més de PSRAM, i en sobraren set mega— i una pintada només dona codi
als píxels que han canviat, que és el `pPrevious` del FastEPD. `gc16` i `init` no s'hi apunten (passen per tot el
rectangle a propòsit, i són els que treuen el fantasma) i la primera pintada d'una arrencada tampoc (el vidre pot tenir
la imatge d'abans). **I la finestra s'encongeix al que de debò s'ha mogut**: el rectangle del nucli és el que ha
*dibuixat*, i una pàgina redibuixada sencera són 540 files per dos dígits; comparar els dos quadres fila a fila dona la
caixa del que no és igual, o sigui que el panell camina només fins a l'última fila que ha canviat i una pintada on no ha
canvat res no es gasta gens.

**I la quarta palanca, que és la que més ha valgut** (0.190.39): la mateixa pàgina sencera costa **2,67 s amb el
processador a 80 MHz i 1,2 s a 240**, perquè aquí el xip no espera res —converteix el quadre en els codis
del panell fila a fila i alimenta el i80 a cada fila— i el cable només són dotze microsegons d'una fila
de setanta-vuit. Aquell «en enviar dades al eink, 80 MHz» era cert en una placa **amb** controlador, que
pinta de la seva pròpia memòria. **Qui ho decideix és la placa i no el driver**: dues plaques poden portar
el mateix panell i el mateix driver i no costar el mateix, o sigui que la pregunta és `IPlatform::a_paint_is_work()`
—falsa per defecte, certa aquí— i la resta de plaques no canvien de comportament.

Aquesta placa continua sense animar res: el que hi ha és una actualització curta, no un vídeo.

**La trampa de debò d'aquesta placa, i és com la del Paper però al revés**: no té cap latch
d'alimentació —el corrent de la cel·la arriba al xip i prou— i l'única manera d'**apagar-la** és el bit
`BATFET_DIS` del **carregador** (BQ25896, registre 9, bit 0x20), que obre el transistor entre la cel·la
i la placa. És el que fa `LilyGoT5_Shutdown` al repositori del fabricant, i per això `can_power_off()`
és cert en aquesta placa i `poweroff` és una apagada de debò. **Amb el cable a dins no fa res**, o sigui
que una placa endollada que diu que s'ha apagat és una placa que no s'ha apagat.

**El que aquesta placa comparteix amb el Paper i no s'ha escrit dues vegades**: el GT911 (el mateix
controlador, altres pins: `INT` 3 i `RST` 9) i el rellotge —el **PCF8563 és el xip que el BM8563
clona**, o sigui que el driver de `lib/hal_esp32/bm8563.cpp` el llegeix i l'escriu sense una línia de
més—. **I les dues portes del son** (0.190.29): la tecla BOOT (GPIO0) i la línia del PCF8563 (GPIO2),
armades alhora amb **`ext1` en el mode «qualsevol baixa»**, que és el que aquest xip té i l'ESP32 no
—els seus dos modes són «tots baixos», que demanaria la tecla *i* l'alarma al mateix temps, i
«qualsevol alt», que és l'estat de repòs de les dues línies—. O sigui que aquí una placa adormida torna
amb una tecla **i** amb l'alarma del rellotge, i `power.caps().wake` ho diu: `timer, button, clock`. **I el que no comparteix**: la bateria, que aquí no és un divisor en un ADC sinó dos xips que
parlen; i el despertar, perquè aquí no hi ha cap latch d'alimentació com el GPIO2 del Paper
(`deep_powers_off()` és fals: el corrent no se'n va, el xip dorm i la cel·la el manté).

**El mapa de pins és el del fabricant** (`docs/pinmap.md` del repositori de la placa) i aquest projecte
no l'inventa: I2C 39/40, panell `CKH` 4 i `D0..D7` 5-8 i 15-18, `STV` 45, `CKV` 48, `STH` 41, `LE` 42,
targeta `CS` 12 `SCK` 14 `MOSI` 13 `MISO` 21, tàctil `INT` 3 `RST` 9, `PCA_INT` 38, `RTC_INT` 2,
`BL_EN` 11.

**I les dues unitats dels bytes, que és el que tallava la banda** (0.190.43): el quadre del vidre és de quatre bits per
píxel —un byte seu són **dos** píxels— i la fila que surt pel bus és de dos bits per píxel —un byte seu en són
**quatre**—. La finestra es convertia de columnes a bytes dividint sempre per dos, o sigui que cada pintada donava codi
al doble d'ample i desplaçada: la banda del sistema quedava tallada a l'esquerra i el rentat de l'splash començava vint
columnes enllà. La comparació amb el quadre anterior va en bytes del quadre, la fila del bus en bytes de la fila, i cada
una té el seu factor.

**I la taula parla des de blanc, i això s'havia de complir** (0.190.42): `u8GrayMatrix` és «des de blanc» —la
nota del fabricant diu *from white (15) to each gray*— i aplicar-la sobre un píxel que no és blanc és un píxel que
acaba on no toca. Cada píxel que s'ha de moure passa ara per **vuit passades cap al blanc** abans de la taula (el
mateix codi que el nivell 0 gasta per anar de blanc a negre), i el que es veia com a pocs tons desapareix. El preu és
un 20 % de passades en els modes de qualitat; els `du`, que ja comencen amb dues passades cap al blanc, no el paguen.
**I el rentat de l'splash demanava dues tintes**: `splash::hide()` feia un `a2` sobre tota la pàgina, i un mode de dues
tintes és parcial —sobre una foto només s'hi empenyen els foscos i els clars queden a mig esborrar—; ara demana `gld16`,
que és blanc sobre el que no ho és i res sobre el que ja ho és.

**I el que de debò hi havia de ser: el codi del fabricant, no el meu** (0.190.55). Entre la 0.190.46 i la
0.190.54 aquesta fitxa va acumular una sèrie de números que eren invents meus —una finestra de porta comptada a
mà, un camí de blanc a negre de tretze microsegons i mig, «passades» en lloc de temps, una trama de 2×2— i
l'amo ho va tallar amb raó: *«el codi d'exemple de LilyGO fa la feina i la fa bé... vols fer el favor de
copiar el que fan ells?»*. El que fan ells és, exactament:

1. **La seva taula**: `u8_103Grays` del seu `examples/FastEPD/grayscale_test`, setze nivells per **divuit
   passades**, instal·lada amb `setCustomMatrix()` («determines how the shades of gray look in 4-bpp mode»).
   És a `t5s3/epd47_waveform.h` **sense una xifra tocada**, amb l'atribució de FastEPD (BitBank Software,
   Apache 2.0). Dins de cada fila hi ha les **dues tintes** —`1` cap al negre i `2` cap al blanc—, que és el
   que separa setze nivells amb divuit escombrades.
2. **La seva seqüència de fila** (`bbepWriteRow`): `ROW_STEP` —`CKV` baix i pols de `LE`— i tot seguit
   **`CKV` alt per a tota la transferència**, no una finestra comptada; el que tanca la porta és el pols de la
   fila següent. La finestra que hi vaig posar jo a la 0.190.48 és, literalment, la que em menjava el
   contrast i em va fer buscar set formes d'ona diferents durant sis versions.
3. **El seu rellotge**: vint-i-sis megahertz i mig (`initPanel(BB_PANEL_EPDIY_V7, 26666666)`), no els vint de
   la taula del panell.
4. **El seu repòs entre passades**: 230 µs, i una passada de codi neutral al final per descarregar els
   drivers del panell.

Les comandes `panel gate` i `panel road` **no hi són**: eren palanques sobre un mecanisme que no era el bo.

**I les dues coses que encara faltaven del seu codi** (0.190.56). Amb la seva matriu, `panel bars` feia escala però
amb colors intercalats i el dibuix sortia *«massa fosc»*. Totes dues són a `bbepFullUpdate`:

1. **La neteja va abans de la taula.** El seu `fullUpdate()` és `CLEAR_SLOW` per defecte: quatre passades cap al
   negre, vuit cap al blanc, quatre cap al negre, vuit cap al blanc i una de neutra —**la pàgina queda blanca**— i
   només llavors les divuit passades de la matriu. La matriu compta des d'un paper blanc; sense la neteja, el que hi
   hagués al vidre es queda a sota. És el que fa que la finestra que neteja (`kInit`, `kGc16`) sigui lenta i la
   parcial no: 25 passades més.
2. **La porta de la fila, que era oberta massa estona.** Ells tanquen la porta amb el `ROW_STEP` de la fila següent i
   fabriquen la fila amb la porta encara oberta. Aquí la fabricació llegia el quadre de la PSRAM byte a byte i hi
   passava **setanta microsegons**, o sigui que la porta quedava oberta vuit vegades més que la línia del panell i la
   tinta rebia vuit vegades més càrrega per passada —que és exactament el que feia que la seva matriu de divuit
   passades saturessin als quatre colors. Ara la porta es tanca **abans** de fabricar la fila, i la fila es fabrica
   amb dos miralls de 256 entrades com els seus (`pGrayUpper`/`pGrayLower`), no amb quatre cerques per byte.

**I la forma d'ona del fabricant sí, la seva durada no** (0.190.57). La matriu de deu polzades del seu
`grayscale_test` (`u8_103Grays`) donava setze barres però *«no en l'ordre correcte, amb colors repetits en
dues columnes i tres o quatre barres negres a l'esquerra»*: era la taula d'un altre panell. El que sí que
comparteixen tots dos vidres és l'**estructura**, i és la del mode de setze grisos del panell
(`epd_wp_epdiy_ED047TC1_16_0`):

- **quinze passades**, i cada nivell rep una empenta cap al negre **mentre li calgui** —el 0 les quinze, el
  15 cap, el 14 una—, sense cap empenta de tornada;
- **i el compte es fa des d'un paper blanc**: la seva taula només dona codi al píxel que ve del blanc, i per
  això el firmware que la fa servir neteja el vidre abans (`epd_clear()` abans de `MODE_GC16`).

El que **no** es pot copiar és quant dura cada empenta, perquè aquest vidre es mou unes set vegades més de
pressa: amb la porta oberta tota la transferència —la seva manera— satura a la segona passada i la pàgina
queda en tres colors. Amb la porta comptada, els tretze microsegons i mig del camí es reparteixen en
**quinze empentes de nou-cents nanosegons** i en surten setze esglaons. `panel gate <us>` (amb coma) mou la
finor; el sostre és el camí sencer.

**I el driver propi, fora: aquest vidre el porta epdiy** (0.190.59). Després de set versions perseguint setze
grisos amb un bucle de software, l'amo ho va tallar: *«integra la llibreria i abandona el driver propi»*. I té
raó, perquè el que faltava no era un número sinó una peça sencera: el seu `lib/epdiy` 2.0.0 és el driver que
el firmware de fàbrica fa servir, i porta coses que un bucle no pot donar —

- el **bus LCD** enviant el fotograma i el **`CKV` generat per RMT**, amb la línia de dotze microsegons i la
  porta oberta sis (la meitat), que és el que la tinta espera;
- la forma d'ona com a **matriu de transició 16×16** triada per **temperatura**, no una taula de destí;
- la **neteja** (`MODE_GC16` renta) i la diferència (`MODE_GL16` no renta), que és el que fa que una pàgina
  cremada i una hora que canvia no costin el mateix.

El que queda del nostre costat és **la pàgina i prou**: el mapa entre la pàgina de 540 × 960 i el vidre de
960 × 540, el nivell d'un píxel, el rectangle que ha canviat, i la traducció de les quatre qualitats del nucli
als seus modes (`kGc16`/`kInit` → `MODE_GC16`, `kGl16`/`kGlr16`/`kGld16` → `MODE_GL16`, `kDu`/`kDu4`/`kA2` →
`MODE_DU`). Les palanques `panel gate` i `panel bus` ja no existeixen: el rellotge i la porta són del seu
`epd_board_v7` i el prompt ho diu.

**I la llicència, dita clar**: epdiy és **LGPL-3** i viatja amb el seu `LICENSE` a `lib/epdiy/`, sense una
línia tocada. No és codi d'aquest projecte i no s'ha de presentar com a tal.

**I el voltant de la pintada, que és el que faltava copiar** (0.190.60). El panell que inicialitzen —`epd_board_v7`,
`ED047TC1`, `EPD_LUT_64K`, la forma d'ona de sèrie, i `EPD_ROT_INVERTED_PORTRAIT` com a gir— és exactament el que ja hi
ha, i el gir és el mateix mapa de pàgina que fa aquest fitxer. El que faltava és el que fan **al voltant**:

- el seu `screen_update()` de fàbrica és `epd_poweron()` → `epd_hl_update_screen(&hl, MODE_GL16, epd_ambient_temperature())`
  → `epd_poweroff()`: **les tensions no es queden enceses** entre dues hores;
- l'exemple dels setze grisos fa **`epd_clear()` abans** de `MODE_GC16`, i és el que posa el paper blanc que la taula suposa;
- la **VCOM** és 1560 mV a la seva NVS i **ajustable des del seu menú de fàbrica** (0 a 5,1 V, passos de 10 mV): és un
  número per unitat, i per això `panel vcom <mv>` l'exposa.

## Fitxa: M5 Paper v1.0 i v1.1

| | |
|---|---|
| Nom (`sys.board`) | `m5stack-paper` (les dues versions, un sol firmware: vegeu a sota) |
| Entorn | `paper`, amb la definició de placa del projecte a `boards/m5stack-paper.json` |
| Xip | ESP32-D0WDQ6-V3, 240 MHz, **8 MB de PSRAM** |
| Flash | **16 MB**, amb la taula de `partitions_16mb.csv`: 4 MB d'aplicació i 11,9 MB de disc |
| Pantalla | **E-ink de 4,7 polzades, 960 × 540** (540 × 960 en vertical), **16 nivells de gris**, controlador **IT8951E** per SPI |
| Entrada | **Tres tecles** (una central `PWR` i les dues del costat) **i panell tàctil GT911** per I2C |
| Sensor | SHT30 (temperatura i humitat) i una EEPROM FM24C02 |
| Rellotge | **BM8563** amb pila de botó, a la I2C interna (0x51): es llegeix a l'arrencada i s'hi escriu l'hora a cada canvi d'època, o sigui que **sobreviu a treure el cable**. El seu `INT` **no arriba a cap GPIO** —al mapa del fabricant només el té la GT911, a GPIO36—: arriba al **circuit d'alimentació** (el SY7088), i per això la seva alarma és el que **torna a encendre la placa** quan s'ha apagat del tot (vegeu «Apagar-se de debò», a sota). **Quin dels dos rellotges ha agafat el despert ho diu `IClock::wake_keeper()`** (0.189.149): `"chip"`, `"timer"` o `"none"`, i és el que el registre escriu a la línia `armed on …` i el que `alarm.caps().keeper` contesta a una app, perquè «una alarma armada» sense dir on no explica per què una placa es desperta i una altra no |
| Bateria | 1150 mAh, amb divisor a **GPIO35** (×2): es llegeix (`esp32_battery`, ADC1 canal 7) i el % surt de la corba de `hal/battery.h`. La lectura són **quatre conversions d'ADC** (~100 µs cada una) i la caracterització del canal una vegada (~1 ms), o sigui **~400 µs per lectura**, que a l'emulador es paguen amb els temps engegats. **Pendent de creuar amb un multímetre**, que és l'única cosa que diu si el divisor i l'ADC diuen la veritat. De mà: `power.battery()` des de `lua`. A l'emulador la cel·la és **la d'aquesta màquina, prestada sola**: el **percentatge** passa per aquesta mateixa corba i no la tensió del portàtil, i `--battery-mv` hi posa un número si el vols triar (docs/85) |
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
| Velocitat | És un valor de la configuració del xip, i **es llegeix**: el byte de `0x8056` —quinze després de l'inici del bloc— són els hertzs que fa servir tothom (`5 + (byte & 0x0F)`, la fórmula que aplica el `M5GFX`), i el driver en fa els mil·lisegons d'un escombrat (0.188.1). La unitat de la Paper diu **vint hertz**, o sigui **un escombrat cada cinquanta mil·lisegons**, i la línia d'arrencada ho diu (`…, 2 fingers, 50 ms a scan`). **Importa perquè el nucli tanca un contacte després de tres escombrats de silenci**: amb deu mil·lisegons suposats la finestra era de trenta i un dit que caminava es donava per anat entre dos frames seus —el swipe es trencava en contactes d'un frame i la pàgina es passava «quan volia»— (0.188.1, docs/95) |
| Gestos | El xip en té de propis, i **ni el driver de M5 ni el d'Espressif els llegeixen**. Aquí tampoc: un toc, una premuda llarga, un arrossegament o dos dits són **formes del flux de punts**, volen dir el mateix a qualsevol vidre i es poden provar al PC amb un fals; un gest que inventa el xip només el pot fer servir una placa (docs/85) |
| Què costa llegir-lo | **Tres transaccions al bus del xip, que és de 100 kHz** i nou bits per byte (vuit i l'acknowledge), o sigui **90 µs el byte**: llegir l'estat, el marc dels punts i escriure el zero que el tanca. Sense dit són **400 µs** des de 0.174.4 (quatre bytes i una transacció: el zero no s'escriu quan el xip no té cap marc), amb un dit **1 830** (dinou bytes) i amb dos **2 550** (vint-i-set). El probe de l'arrencada és el producte id més els **186 bytes** de configuració: **~17 ms**, una vegada (mesurat al codi i a l'emulador, 1/11, docs/85) |

**Qui pregunta i quan (0.174.4)**: el vidre escombra el panell tot sol i **guarda el marc fins que algú el pren**, i el
nucli el pregunta **una vegada per escombrat** —deu mil·lisegons, `ITouch::scan_ms()`— i no a cada torn. Un torn d'un
mil·lisegon preguntant cada vegada és el 80 % del processador en una I2C de cent quilohertz que també és la del
rellotge, la FRAM i el SHT30, i la resposta no pot ser cap altra que la de la lectura anterior; l'emulador compta
aquestes lectures com a `missed` des del primer dia. Un vidre que no digui la seva velocitat (`scan_ms()` a zero) es
pregunta a cada torn, que és el que es feia abans. I el zero de tancament **s'escriu només quan el xip té un marc**
(bit 7 de l'estat), com fan els dos drivers del fabricant: dir-li que no té res quan no té res eren tres bytes al bus a
cada lectura (docs/95).

**I des de la 0.188.0, el vidre avisa en lloc de ser preguntat.** El GT911 té una sortida `INT` que **baixa quan un
escombrat té un marc**, i al Paper eixa línia és **GPIO36**: la mateixa que el xip mira en sortir del reset per triar
adreça (0x14 o 0x5D) i que, com que és **d'entrada**, no es pot conduir però **sí escoltar**. És el que fa el codi del
fabricant —`M5EPD.cpp` crida `TP.begin(21, 22, 36)` i `GT911::begin` hi penja `attachInterrupt(pin_int, ...,
FALLING)`— i el que fa ara aquest HAL:

- `esp32_gt911(bus, int_pin)` configura el pin com a **`GPIO_INTR_NEGEDGE`** sense pull (el xip el mena) i hi penja un
  handler **a IRAM** que només aixeca un `volatile bool`. Una interrupció no pot esperar el flash, i **el bus no és cosa
  d'una interrupció**: el marc es llegeix fora, al torn.
- `read()` respon **zero sense tocar el bus** quan la línia encara no ha avisat, i baixa el flamenc quan ha llegit el
  marc, quan l'estat diu que no n'hi ha i quan el xip no contesta.
- `ITouch::warns()` diu si aquest vidre avisa, i el nucli se'n refia: **un vidre que avisa es pregunta a cada torn**
  (no costa res) i un que no avisa es pregunta al ritme de `scan_ms()`. La porta dels deu mil·lisegons de la 0.174.4 es
  queda per als que no avisen.

**El que es guanya i què no**: amb ningú a la pantalla, les lectures de **400 µs** cada deu mil·lisegons —un **4 %** d'un
bus de 100 kHz que es comparteix amb el rellotge, la FRAM i el SHT30— **desapareixen**, i el marc es pren **el torn que
arriba el flamenc** en lloc de fins a deu mil·lisegons després. Amb un dit, la lectura és la mateixa (**1 830 µs** per
marc) perquè el marc s'ha de llegir igual. **La precisió no canvia**: els punts són els del xip i la línia només diu
«hi ha alguna cosa» (docs/95).

**I des de la 0.189.0 el nucli decideix abans que la pàgina senti res.** Cada marc del vidre anava a
`luabind::ui_touch_progress` i la pàgina decidia a cada un què volia dir; els controls, doncs, veien **els fotogrames
d'un gest** i el que un lliscament fos una premuda depenia de quants marcs arribessin —que és el que va obrir una fila
amb un swipe quan una pintada s'empassava els torns (docs/95, 0.188.9)—. Ara `kernel::touch::poll()` **no envia cap
fotograma** mentre el dit és a sobre: al final del contacte diu una sola cosa a la pàgina —la premuda i el relleu del
punt on va caure (`ui_touch_progress`), o **un gest** (`ui_gesture`) amb el punt i el viatge del contacte sencer— i la
decisió queda escrita a `kernel::touch::last_contact()`, que és el que la prova de `test_touch` mesura. El gest no és
un toc: un contacte que ha caminat no activa mai una fila, un botó ni una targeta (docs/30).

**I el contacte és de la pàgina on va començar, no de la que hi hagi quan s'acaba** (0.189.100, l'amo: *«en tornar al
Home se m'han obert apps totes soles … és simplement que un event de la vista anterior arriba a la vista nova (actual)?»*).
La premuda d'un control es lliura en aixecar el dit i **amb el punt on va caure**, o sigui que un dit que era al vidre
quan l'app va canviar clicava el control de la pàgina nova que hi hagués sota aquell píxel —a un llançador, una cel·la, i
una cel·la obre una app—; i la cua de gestos i la finestra dels tocs sobrevivien al canvi, així que el toc de la pàgina
anterior (que es diu 350 ms després de llevar el dit) arribava a una app que no hi era quan va passar. `shell` ho diu a
`kernel::touch::forget_the_page_before()` quan una app se'n va i quan n'arriba una altra: la cua i els tocs es buiden, el
parell de dits s'oblida i **el contacte que hi ha al vidre es marca com a de ningú (`kNobodys`)** —`poll()` no en diu res
a la pàgina ni a la cua fins que el vidre diu que el dit ja no hi és— (docs/30). I la pàgina nova **no hereta el que el xip
encara té a dins**: el GT911 escombra sol i guarda el marc de l'últim escombrat que va trobar algú fins que una lectura
se l'emporta, i el nucli no gira mentre una app es llegeix i es compila, o sigui que un toc sobre un vidre buit es lliura
després del canvi (`g_page_wants_an_empty_glass`) i clicava la cel·la del llançador de sota el dit. La pàgina nova ha de
veure el vidre buit abans que cap contacte sigui seu.

**I el que una app no pot fer: menjar-se els torns** (0.189.65, l'amo: *«no es possible, ja que bloqueja les lectures del
tàctil i no "pilla" els gestos per falta de mostres»*). El tàctil es pregunta **un cop per torn** —`touch::poll` és al
`kernel::turn`—, o sigui que **el ritme del torn és el sostre de la resolució d'un gest**: una app que pinta un frame de
64 × 64 amb `du` gasta **194 ms** per volta i el tàctil es llegeix a **cinc hertzs**, i un lliscament de tres-cents
mil·lisegons arriba amb una o dues mostres, que és un contacte que no ha caminat. La prova és l'app `loading`, que no és una
app per a una persona i ho diu: **una animació al vidre d'aquest sistema es menja els gestos**, i no és un defecte de
l'animació sinó del mostreig. Una app que vulgui les dues coses ha de pintar **menys sovint que el tàctil llegeix** o deixar
els torns lliures entre frames.

**El que hi ha escrit, l'1/11**: `ITouch` al HAL, el driver sencer (les dues adreces, la configuració,
el marc dels cinc punts, el zero de tancament), **les tres crides de Lua** (`touch.caps`, `touch.read`,
`touch.gesture`), els gestos fets pel nucli (`tap`, `double`, `triple`, `long`, `drag` amb un dit o
amb dos, `two` i `pinch`, amb la mateixa finestra que el botó), una app que llegeixi el panell en bucle i la
línia de l'arrencada —`touch: GT911 at 0x14, config N, 540x960, 5
fingers`— que diu què és aquesta unitat sense un oscil·loscopi. **La capacitat s'anuncia** quan el xip
contesta, perquè ara una app ho pot fer servir. La **rotació compartida amb el panell** ja hi és
(0.18.1): una sola rotació per al dibuix i per al toc, amb els `x`/`y` i els `dx`/`dy` dels gestos
convertits en un sol lloc. **El que falta**: una mesura al xip —**a quina velocitat mostreja** (docs/95,
E-6)— i, del pas 8, la política d'energia de la I2C i de les alimentacions.

### Els números de la placa: el RNG del xip, i per què cal la ràdio o l'ADC (0.174.8)

**El que diu Espressif, i és cert a mitges.** El capçal de `esp_random()` de l'IDF que enllaixa aquest build
(`esp_hw_support/include/esp_random.h`) diu, literalment: *«If Wi-Fi or Bluetooth are enabled, this function returns
true random numbers»*, i en qualsevol altre cas et mana al capítol «Random Number Generation» de la guia. El motiu
és al xip i no a la documentació: en aquesta família —l'ESP32 de tota la vida, el del Paper i els ATOM— **el número
aleatori no té cap registre propi**: es llegeix del registre del **perifèric de Wi-Fi**, `WDEV_RND_REG`
(`soc/esp32/include/soc/wdev_reg.h`, `0x60035144`, «hardware random number generator»), i el que el remena és el
**soroll de radiofreqüència** de l'etapa de ràdio. Amb la ràdio apagada el registre no es refresca i `esp_random()`
pot tornar **el mateix número una vegada i una altra** —que és exactament el pitjor cas possible per a una llavor: no
una llavor dolenta, sinó una llavor constant.

**I el mateix IDF porta la resposta per a un sistema que no vol la ràdio encesa**: `bootloader_random_enable()`
(`bootloader_support/include/bootloader_random.h`), que el capçal descriu com *«Enable an entropy source for RNG if RF
is disabled»* i que, en paraules seves, barreja soroll intern de l'**ADC (SAR)** dins del generador; diu també que
**es pot fer servir des del codi de l'aplicació, al principi de tot**, i que s'ha de desfer amb
`bootloader_random_disable()` **abans que el RF, l'ADC o l'I2S s'inicialitzin**.

Per això la banda (`lib/pal_esp32/src/pal_esp32.cpp`) fa **una finestra d'un microsegon a l'arrencada**, dins de
`pal::init()` i abans de qualsevol altra cosa de la placa:

1. `bootloader_random_enable()`,
2. `esp_fill_random()` de **vuit bytes**,
3. `bootloader_random_disable()`,

i el resultat es barreja amb `esp_timer_get_time()` —dos arrencades de la mateixa placa són dos moments diferents— i
passa a un **xorshift32** propi, que és el que respon `ISystem::random()`. El remenat no és decoració: si el xip
repetís un número, el que respon la banda **no** el repeteix (`docs/20`). La finestra va a `pal::init()` i no a la
primera crida de `random()` precisament perquè la documentació mana que l'ADC no s'hagi inicialitzat quan la font
d'entropia està encesa, i al Paper **l'indicador de bateria és un ADC** (GPIO35, vegeu la fitxa): la primera lectura
de la bateria arriba després i no hi té res a veure.

**I amb la ràdio encesa el número és del xip** (0.178.6): `esp_random()` és de debò **mentre el Wi-Fi o el Bluetooth
estan encesos**, perquè l'entropia és el soroll de l'etapa de RF, i el que respon la banda és aquest número i no el
xorshift quan el driver diu que la ràdio està encesa (`radio_is_up()`, `esp_wifi_get_mode()` diferent de
`WIFI_MODE_NULL`). Cada paraula del xip **també remena l'estat del remenador**, o sigui que la seqüència que respon
quan la ràdio es torna a apagar no és la que es va sembrar a l'arrencada. Amb la ràdio apagada, el xorshift de sempre.
Val a dir què és i què no: amb la ràdio encesa les paraules són impredictibles i serveixen per a un joc, un color o
una sal; **no** són una font criptogràfica (no hi ha estimació d'entropia ni salut de la font) i per a claus de debò
caldria un TRNG certificat o el generador del xip llegit amb una DRBG al damunt.

**I l'última paraula que la placa ha dit es queda a la memòria del RTC** (0.178.8): `RTC_NOINIT_ATTR` hi posa un bloc de
setze bytes —una màgia, l'estat, un comptador d'arrencades i un checksum— i la llavor de cada arrencada es remena amb
ell, o sigui que **dues execucions de la mateixa placa no poden compartir seqüència encara que la font d'entropia
respongui el mateix dues vegades**, que és l'avaria que aquest bloc existeix per tancar. És la mateixa memòria que el
fitxer `/rtc` (`docs/70`), escrita **sense tocar la flash** —que es gastaria amb una escriptura a cada arrencada— i
invisible per a les apps, perquè és de la banda i no un fitxer del disc. **Què sobreviu i què no, sense embuts**:
sobreviu a un reinici, a un pànic i a un son profund; **no** sobreviu a treure el corrent, i llavors l'arrencada torna
a començar amb el soroll de l'ADC i el rellotge, que és exactament el cas pel qual existeix la finestra. **Al Paper, el
RTC que porta pila és el BM8563 i aquell només guarda l'hora**: la pila de botó alimenta el rellotge i no el domini RTC
del xip, i per això el bloc no hi és a dins. Un bloc que sobrevisqués al corrent voldria dir NVS o una pàgina de flash
—que es gasten— o una memòria ferroelèctrica com la de l'ATOM, i no s'ha fet: el que cal protegir és que dues
arrencades no s'assemblin, i per a això n'hi ha prou amb el que hi ha.

**El que no es fa**: no s'encén la ràdio **només** per tenir números. Costa desenes de mil·lisegons i mil·liampers, i
aquest sistema apaga el Wi-Fi quan no té feina (docs/75); l'ADC fa la feina de la llavor en un microsegon. Però quan la
ràdio ja és encesa per una altra cosa, no costa res preguntar-li. **I no es pot fixar una llavor en una placa**:
`set_random_seed()` és un no-res a la banda d'ESP32, perquè un firmware que pot fer repetir les seves seqüències és un
firmware les claus del qual es poden endevinar (docs/20).

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

**Comprovat a la placa l'1/11 amb la 0.10.1**: `caps` diu `env yes` i `env.read()` dona
`21.1 C 63.5 %` dues vegades seguides, sense cap línia `i2c:` al log (docs/95, E-5).


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

- **el rail del panell vol un segon, i només el fred el paga** (0.190.22): `M5EPD::begin` encén GPIO23 i
  espera **1000 ms** abans del primer mot. Amb 100 ms la línia BUSY encara llegeix baixa i el handshake es
  rendeix a `SYS_RUN` (`M5EPD_Driver::begin` no prem cap reset: el seu `rst` val `-1` de sèrie). **I un
  panell que ja és calent no torna a pagar-lo**: `It8951::begin` pregunta la línia BUSY abans de
  `power_on()`; si és alta, arma el controlador sense ciclar el rail, i els vint mots del `GetDeviceInfo`
  són els que refusen un `ffff`. El segon només el paga un vidre fred —la bateria, el `poweroff`—; un
  reinici o un despert que ha conservat el rail s'estalvien el segon (docs/60);
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


### El buffer és del sistema, i l'app dibuixa a la seva vista (1/11)

El vidre té **un sol buffer** i és del sistema: el del driver (`It8951::image_`, 253 KB a la PSRAM).
Una app **no hi arriba**: dibuixa a un **canvas propi** (`kernel/ui.h`) que el sistema li fa quan
arrenca, i `ui.present` és el que compon el rectangle que ha canviat sobre el buffer del sistema i
demana el pintat. Tres coses que això dona, i cap és cerimònia:

- **el sistema pot ser l'amo del vidre** sense que cap app ho sàpiga: una rotació, la barra o una
  segona vista són canvis d'un sol lloc;
- **una app no pot treure una altra del vidre** per accident, perquè no toca la memòria que es pinta;
- **`ui.resize` i la rotació passen pel mateix forat**: el sistema torna a fer el canvas, blanc, i
  avisa l'app amb `on_resize(width, height)`, que és qui sap què hi havia d'anar. `ui.rotation(n)` fa
  les tres coses alhora —el vidre, la barra i els dits— amb el mateix valor (docs/50, «La pàgina es
  gira en software…») i la conversió dels dits és a `touch.cpp`.

### El log dels conductors, i el que es compta (1/11)

**Un conductor no escriu frases soltes a la consola.** Formatega la línia amb `hal::logf(level, tag, fmt,
...)` —`[I][PANEL] 540x960 turned 270 (glass 960x540), …`— i la deixa a un *sink* que el nucli instal·la:
d'aquesta manera la línia d'un conductor i la del nucli són la mateixa cosa al mateix cercle, i el
conductor no ha de saber que el nucli existeix (docs/30, docs/85). Abans de la 0.20.0 eren **trenta-vuit
`printf`** amb el prefix escrit a mà dins del text; ara la lletra de nivell i l'etiqueta les posa el
format, i una línia que no porta etiqueta no compila.

**I el que es compta a la porta** (docs/30). El repàs és aquest, i és el que diu quina fila de `stats`
paga cada cosa:

| Dispositiu | Crida | Es compta a |
|---|---|---|
| `IStorage` | `stat`, `list`, `space`, i obrir/llegir/escriure/cercar/tancar/esborrar/renombrar | **una fila per unitat i porta**: `flash.read`, `sd.read`, `nvs.write`, `rtc.stat`, `fram.list`… |
| `ISurface` | `flush` | `glass.flush` (totes les pintades) |
| | `flush` d'un mode que neteja (`gc16`, `gl16`, `glr16`, `gld16`) | `glass.gc16` (les lentes, les que treuen el fantasma) |
| | `idle` | `glass.idle` |
| | `set_rotation` | `glass.flush` — un gir torna a fer la imatge i envia la pàgina sencera |
| `ITouch` | `read` | `touch.read` |
| `IClock` | `epoch_ms` | `clock.now` |
| | `monotonic_ms` | `clock.mono` — **també és el xip**, i el botó, la llum i la ràdio el demanen cada torn |
| | `arm_wake`, `disarm_wake`, `last_wake` | `clock.wake` |
| `IInput` | `pressed`, `held_ms`, una vegada per torn | `keys.read` |
| `ILed` | `fill`, `pixel`, `show` | `led.show` |
| `IImu` | `read` | `imu.read` |
| `ISensor` | `read` | `env.read` |
| `IPower` | `battery` / `sleep` / `set_cpu_mhz` | `power.battery` / `power.sleep` / `power.cpu` |
| `IKeyValue` | `get`, `set` (i l'`erase`) | `kv.get`, `kv.set` |
| **La PSRAM** | un bloc demanat o donat (`pal::extra_memory`) | `psram.alloc`, `psram.free` |
| **El nucli** | un torn sencer | `kernel.loop` |
| **L'intèrpret** | un torn d'una app, i què se'n va en codi de l'app | `lua.run`, `lua.own`, `lua.call` |
| `INet` | escaneig, connexió, desar i oblidar la xarxa | `net.wifi` |
| | una pàgina | `net.http` |
| | l'hora | `net.ntp` |

**El que no es compta, i per què**: `count()`, `width()`, `height()`, `caps()`, `rotation()`,
`max_value()`, `face`/`spot`/`kind`/`group`, `last_error()`, `wifi_state()` i `prov_provisioned()` són
camps en memòria del conductor o de la placa, no viatges al maquinari: comptar-los ompliria la taula de
files que sempre costen zero i amagaria les que no.

El conductor del panell no compta les seves crides una a una: el compte el porta el nucli, que és qui sap
de qui és la crida i qui ho ha de poder ensenyar amb `stats` (docs/75).

**I el `idle()` que no deia la veritat** (1/11, 0.19.2): el controlador **no es marca ocupat en el mateix
instant** que rep la comanda de pintar, i una pregunta feta un mil·lisegon després llegia zero amb la
pàgina encara per començar. Dues conseqüències, totes dues dolentes: una app que mesurava deia «0 ms», i
un `present` de dues qualitats podia enviar el segon pintat a sobre d'un primer que no havia començat. El
conductor guarda ara **quan es va demanar el pintat** i no diu que s'ha acabat fins passats **20 ms**
(`kEngineStartMs`), que és una **guarda i no una mesura** —ningú no ha cronometrat quant triga el motor a
marcar-se ocupat, i el número només pot fer la resposta més tard, mai més aviat—. Es nota igual a `idle()`
i a l'espera del pintat anterior, perquè els dos fan la mateixa pregunta.

**I una cosa que el vidre ha dit i que el full de dades no diu** (mesurat a la placa l'1/11, docs/95
E-7): amb la imatge de quatre bits, **`du` pinta però no neteja** i **`du4` i `a2` no van pintar res** —el
`present` responia `true` i el vidre es quedava igual—. Per això el camí que es fa servir és **pintar amb
`du` i esborrar amb `gc16` sobre la mateixa regió**: el blanc torna net i només costa la regió.

El canvas és **4 bits per píxel amb el mateix empaquetat que el vidre** (un byte per a dos píxels, el de
l'esquerra al nibble alt), o sigui que compondre és una còpia per files i no mig milió de píxels un a un:
és el que fa el `blit` que el HAL va afegir a `ISurface` (docs/50).


### El control de danys: què s'ha tocat, i amb quina qualitat (1/11)

Cada dibuix d'una app deixa una anotació: **la regió i la qualitat que vol** (`du`, `gc16`, la que
sigui). El sistema en guarda fins a **vuit**, ajunta les del mateix mode que es toquen (a dos píxels) i,
quan `ui.present` arriba, pinta **una regió per mode**. Tres números que això fixa:

- **una pàgina d'un sol mode és un sol pintat**, i un canvi petit (el dígit d'un rellotge) és una regió
  de 20 × 30 píxels i no la pàgina sencera;
- **una app que dibuixa més de vuit regions** abans de presentar-les les veu convertir-se en una de sola
  amb la qualitat més forta de les que hi havia, i ho pot saber amb `ui.damage()` abans de pagar-ho. **El
  `gc16` que hi havia** (fins a la 0.179.4) feia que tota pàgina de més de vuit controls es convertís en el
  pintat que fa el flaix sense que ningú l'hagués demanat; ara qui decideix és el `present` (vegeu a sota).
  **I aquesta regió gran inclou la peça que acaba d'arribar**: la primera versió suposava que la peça
  nova ja queia dins del que hi havia i la llençava quan no era veritat, o sigui que vuit peces a dalt i
  una a baix es pintaven com una regió de 430 files i la de baix es quedava per pintar (trobat per la
  suite nativa l'1/11: esperava 510);
- **les coordenades són de la vista i el vidre en vol unes altres**: la traducció es fa **en un sol
  lloc** (`ui.cpp`, dins de `paint`), sumant el lloc on el sistema ha posat la vista i tallant el que
  queda fora del panell. Una vista no és el vidre: pot ser més petita i estar en un altre lloc.

**La regió es guarda en nibbles sencers i el dibuix és exacte al píxel.** Un byte porta dos píxels, o
sigui que una regió que comença o acaba en un píxel senar parteja un byte amb el seu veí: la regió
s'emmotlla cap al byte sencer —així la còpia és byte per byte i no píxel a píxel— i el que hi ha a la
vora és el píxel del canvas mateix, que és la veritat de la vista. Per això `fill_rect`, a `ui.cpp` **i al
driver del panell**, escriu els dos extrems nibble a nibble: pintar el byte sencer pintava el veí, i el
text de la barra —que és una tirada de rectangles de cada caràcter— engreixava una columna de píxels a
cada lletra (trobat per la suite nativa l'1/11: un rectangle a `x = 101` pintava el píxel 100).

**I el controlador només pinta un rectangle que comenci a la columna 0 de la seva memòria** (0.118.7, mesurat a la
placa amb el log de la 0.118.6): la splash (`0,0 960x540`) i la barra a l'arrencada (`0,0 48x540`) pujaven al vidre, i
**totes** les altres —la pàgina d'`images` a `48,0 912x540`, els seus quatre controls a `56,12`, `484,12`, `616,12` i
`876,12`, el rellotge de la barra a `4,104`— el controlador les **acceptava i no pintava res**: carregava una imatge que
no era la que ensenyava i al vidre hi tornava la pàgina d'abans. Un rectangle amb `x ≠ 0` no cobreix la fila sencera de
la imatge, i aquest controlador no la camina. Des d'aquesta versió **la càrrega i el pintat comencen sempre a la columna
0** del vidre —els píxels entre la vora i el canvi es paguen amb el canvi, i els de la barra són a la imatge del driver,
o sigui que repintar-los és repintar la veritat—, i **un pintat que neteja neteja tot el vidre** quan ja cobria mig
panell o més (la splash i la primera pàgina d'una app); els que només mouen un número demanen `du` i no canvien.

**I aquell mesurament no era net** (0.179.16): la mateixa arrencada tenia un segon error, trobat l'endemà —el
panell no es tornava a seleccionar després de la splash (0.118.9)—, i **totes** les pintures posteriors a la
primera fallaven fos quin fos el seu rectangle. **La prova és l'altre projecte d'aquesta placa**: PaperOS porta
setmanes pintant caixes arbitràries en aquest mateix panell amb `LD_IMG_AREA` i `UpdateArea` amb el rectangle de
debò i res més (`src/plat/m5epd_hal.cpp`), i l'única precaució que hi posa és **arrodonir a quatre** —la x, i la
y quan el marc és el del panell, que és el nostre cas perquè aquest driver gira en software—. Des d'aquesta versió
**l'àrea és el requadre del canvi i comença on és el canvi**. No és un detall: la pàgina va girada 90°, o sigui que
l'x del vidre és la y de la pàgina, i forçar la vora de l'esquerra volia dir «des de dalt de la pàgina fins al
canvi»: una cel·la del llançador costava una **columna de la pàgina** —168 px a la primera fila i 652 a l'última,
contra els 107 que té la cel·la (mesurat a la placa l'11/11)—. `panel [edge|box]` diu què fa aquest vidre i ho
canvia, i `edge` és la tornada enrere sense flashejar si un pintat no aparegués mai (docs/30).

#### La qualitat d'un `present`: la del que ha canviat (0.179.5)

L'amo, amb el codi al davant: *«Comptant que totes les apps comencen amb un panell net i un buffer net. Perqué
totes refresquen amb tanta qualitat quan no fa falta? usa sempre el mode necessari adequat per els canvis del buffer
que es volen pujar a l'eink»*. Tenia raó i el comptador era el pitjor: **cada app demanava `gc16` a cada pàgina**, i
`gc16` és la passada que fa quatre flaixos, sobre un vidre que el sistema ja havia rentat abans de deixar-li el
paper. El que fa ara el sistema:

- **una pàgina és un sol pintat** (0.179.5): si el que ha canviat cobreix mig vidre o més, o són quatre caixes o
  més, es pinta d'un cop —el panell pinta una caixa cada vegada i cada passada espera la seva tinta, o sigui que
  quatre caixes són quatre passades per una pàgina—; **dues caixes són dues**, perquè el cercle que marxa i el que
  arriba no es toquen i ajuntar-los pintaria l'aire del mig (0.177.4). Això també val per al present del focus
  (`present_each`), que abans podia pintar la pàgina sencera en cinc passades quan una app demanava `ui.focus()`
  abans del primer `present`;
- **i `ui.present(mode)` tampoc no ajunta el que no es toca** (0.179.57): `present_all` és el present amb una
  qualitat per a tota la pàgina, i feia una sola caixa amb **totes** les regions pendents —la unió de dues caixes
  que només comparteixen aire és una caixa que també cobreix el que ningú ha canviat—. Al teclat de l'editor això
  era `paint 6,54 534x602` per una lletra i la línia del peu, i ara són dues passades de `532x74` i `528x28`. El
  que sí que ajunta és el que es toca, amb el mateix marge de dos píxels que `damage_add`, i **una pàgina que es
  deu sencera continua sent una sola regió** (la rentada de `gc16`, que és una passada i no quaranta);
- **la qualitat és la que demana la tinta**: `gl16` —setze nivells i cap flaix— i `gc16` quan el panell ha rebut
  tantes passades que li queda fantasma. El pressupost de fantasma és **el de passades i no el de caixes**: es
  compten les passades que el vidre ha rebut des de l'última rentada (`kManyPaints`, 32) i no els rectangles amb
  què s'ha dibuixat la pàgina, perquè quaranta paraules són una pàgina i quaranta passades són quaranta taques;
- **i el gir d'una pàgina no nomena cap mode** (0.189.2, l'amo: *«és una puta llista de text blanc i negre, la llista
  no decidirà a quina qualitat pintar-se, ho faran els elements de la llista un per un»*). El gir d'una `ui.list` i el
  d'una `ui.icon_grid` deien `gl16` i la caixa sencera es pintava amb aquest mode, trepitjant el que cada fila havia
  demanat: la llista de l'`about` —25 files de lletres— pagava **450 ms i cap flaix per cada swipe** quan la seva
  tinta és de dos nivells. Ara el gir no nomena res i el sistema fa el que ja deia la regla: **la més forta de les
  qualitats que les regions han nomenat**, i si no n'ha nomenat cap, la de la tinta del que ha canviat
  (`cleaner_for`: `a2` per a text, `gl16` per a uns quants grisos, `gc16` per a una fotografia). **Mesurat a
  l'emulador** (vidre de 50 ms, `about`): el gir era `paint 20,312 500x580 mode 3 gl16` i ara és
  `paint 20,312 500x580 mode 7 a2`;
- **i l'única cosa que es queda de la regla vella és el perill que aquest panell va mesurar** (0.189.2): una passada
  de dos nivells **de la vista sencera** va deixar el motor sense imatge —`blit 0,48 540x912` i `LUTAFSR` sense
  llegir en 3006 ms, la 0.179.64—, o sigui que un dany que és la vista sencera continua pagant `gl16`
  (`the_damage_is_the_whole_view`). La caixa d'un control no és la vista: mig vidre de lletres va amb `a2`, i això és
  el que **s'ha de confirmar a la placa** amb la llista de l'`about` a la mà;
- **una qualitat que l'app va nomenar es respecta**: `ui.rect(…, 'gc16')` és l'app dient què necessita la seva
  tinta, i una regió que no en porta és d'un control que es dibuixa amb `du`, que és el barat i no una promesa
  sobre el panell (`Damage::named`). El `present` amb `mode` no canvia: el que diu l'app és el que va al vidre;
- **i el present que fa el sistema tampoc no la sobreescriu** (0.179.41): el cercle que es mou, el control que
  deixa anar i el valor que ha canviat es pinten amb `present_each`, que abans escrivia el seu mode a **totes** les
  regions. El resultat a la placa va ser el que l'amo va veure i va preguntar —*«Perqué l'editor fa tants
  refrescos i a aquesta qulitat???»*—: el primer toc a la pàgina de l'editor pintava el **seu quadre de document
  sencer** (550 dels 912 píxels d'alt, o sigui més de mig vidre) amb un `gl16` de 450 ms i un flaix, sobre el `du`
  de 120 ms que l'app havia demanat pel nom. Ara una regió que ha nomenat la seva qualitat la conserva, i la que no
  en porta rep la passada del present;
- **i la passada del present és `a2` i no `gl16`** (0.179.41): 290 ms contra 450, i la forma d'ona de dos nivells
  és la que té la tinta d'un cercle i d'una cara premuda. Una pàgina que un present ajunta es queda amb la qualitat
  més forta que l'app va nomenar, i només pren la del present quan l'app no en va nomenar cap;
- **el rentat entre dues apps compta passades** (0.169.2 i 0.179.5): `cleaner_for` mira els nivells de la pàgina i
  el pressupost de passades, i el que abans comptava caixes feia que tota pàgina de text arribés a les trenta-dues
  i s'emportés un `gc16` entre app i app. Amb el comptador de passades, una pàgina de text es renta amb `a2` i una
  de grisos amb `gl16`;
- **la imatge de l'arrencada és l'excepció que el sistema diu** (0.179.5): la splash pinta el vidre sencer i la
  seva tinta no ha passat per la pàgina, o sigui que la primera pàgina que la tapa es deu **sencera i neta**
  (`ui::owe_the_page()`, el mateix camí que quan una app torna una vista enrere). Abans això ho deia el `gc16`
  que cada llançador escrivia a la primera línia de la seva pàgina; ara ho diu el sistema, que és qui sap què hi
  ha al vidre.
- **i la primera pàgina d'una pàgina nova va al vidre d'una passada** (0.190.28, l'amo: *«Perqué l'app device_lock dibuixa item
  per item a la pantalla? També ho he vist a "Broken App Test"»*): `paint_root` envia **una passada de tinta per caixa**, i les
  caixes d'una pàgina que s'acaba de construir no es toquen mai —cada control té el seu forat—, o sigui que la pàgina del
  Broken App Test eren **vuit `gc16` de 470 ms** (3,7 s per una pàgina que surt de cop) i la del pany cinc. `present()` ara
  ajunta les caixes en una sola regió **quan aquesta és la primera passada de la pàgina** (`g_updates == 0`, el comptador de
  passades de tinta, que torna a zero cada vegada que la pàgina es dona o un rentat sencer hi puja). La resta de la vida de la
  pàgina segueix amb les seves caixes: una animació vol enviar només el que ha canviat (docs/30, docs/50, docs/95);
- **i un deute sencer ja és el rentat** (0.190.27): quan el sistema deu la pàgina —un despert per una alarma que aixeca una
  app, la imatge d'una splash—, el primer present de l'app la pinta **sencera i neta** (`present_all(kGc16)`), o sigui que
  rentar la pàgina blanca a `attach` era pagar dues vegades el mateix vidre: al log del 19/11, `799 ms` d'un `gld16` de blanc
  i tot seguit `782 ms` del `gc16` de l'app. És el mateix argument que `attach_under_a_picture` ja feia servir, i el que es
  mira al `stats` és el `boot` d'una arrencada per alarma, que ha de baixar uns vuit-cents mil·lisegons (docs/30, docs/95);
- **i el deute és la vista i no les caixes de la pàgina** (0.179.53): `present_all` ajuntava el dany que hi havia i
  no n'afegia cap, i una pàgina que el sistema ha donat blanca fa gratuït el `ui.clear(15)` de l'app (0.168.0) — o
  sigui que l'app que torna una vista i no ha dibuixat res demanava una pàgina sencera, no trobava cap regió i el
  vidre es quedava amb la targeta del panell a sobre. Ara la passada que paga el deute marca la vista sencera amb la
  qualitat que li han donat, i les caixes que hi havia es descarten perquè la pàgina les tapa totes.
- **i el deute es pot deixar sense pagar per pintar-hi a sobre** (0.189.38, l'amo: *«així es queda l'splash amb la
  barra de progrés que creix mentre l'app carrega completament»*): el deute és el que fa fora la imatge de
  l'arrencada, o sigui que una app que vol una barra de progrés damunt del seu propi splash no podia fer servir
  `ui.present()` —pintava la pàgina blanca al primer pas—. `ui::present_over()` compon la pàgina igual i posa al
  vidre **només les caixes** del que l'app ha dibuixat des de l'últim present, amb el deute sencer encara pendent per
  al `ui.present()` del final, que és el que el paga amb `gc16` i ensenya la pàgina de l'app sencera i neta.

**Mesurat a l'emulador, canviant de la pantalla d'inici a una app**: el que era un `gc16` de 540×912 (la rentada)
i un `gc16` de la pàgina és ara un `a2` de 540×912 i un `gl16` de la pàgina; l'arrencada del llançador amb deu
apps passa de vint `gc16` de dues caixes a vint `gl16` de dues caixes, i la pàgina d'`about` —cinc caixes— de cinc
pintats a un.

I una cosa que la placa mana i que el codi no pot saltar-se: **el controlador pinta d'una en una**. Cada
`DPY_AREA` arrenca un motor de LUT, i el driver del fabricant espera (`CheckAFSR`) que l'anterior
s'hagi acabat —**i en surt netejant la bandera**, que és un **escriptura d'1** a `LUTAFSR` (0x1224), i no
només llegint-la. Això es va aprendre a la placa (0.118.4): el nostre driver la llegia i no la netejava mai, i
una bandera que es va quedar aixecada va deixar el vidre **sense cap pintat més** —el logotip de l'arrencada
puja perquè és el primer pintat d'un arrencada, i després ja no puja res: l'app s'executa i no es veu—. I **la bandera no baixa mai sola** (0.118.5, mesurat a la placa amb el log de la 0.118.4 a la mà): després de
**cada** pintat torna a estar a dalt sis segons més tard, o sigui que és un **latch** que el maquinari no neteja.
El que fa el codi del fabricant és **contestat-la** —una escriptura d'1 a `LUTAFSR`— **abans** del pintat, i no
llegir-la després; això fa ara el driver, al començament de `flush`. I el que espera són les dues coses que no
menteixen: la **línia BUSY** del panell, que té a zero mentre treballa, i **el temps de tinta del mode**, que és la
taula del fabricant (docs/95): **120 ms** per a `du` i `du4`, **290** per a `a2`, **350** per a `gl16` i `glr16`,
**450** per a `gc16` i `gld16`. L'espera només pot ser més llarga que el full de dades, mai més curta, i costa una
lectura de pin i un comptador. Guanyar-hi: la pàgina puja **sense cap sis segons d'espera** (docs/92 §33). Per això `It8951::flush` espera —amb un sostre de tres segons— si encara hi ha una
pintada en curs: **el primer pintat d'un `present` no espera mai, el segon sí**. L'alternativa era
enviar el segon damunt del primer i que el controlador l'ignorés, que al vidre es veu com mitja pàgina.

### Els noms dels modes: dos estan intercanviats, i el «60 Hz» no és d'aquest panell (14/11)

**Els noms de la taula de dalt són els de M5Stack, i dos d'ells estan intercanviats respecte del full de dades
d'E Ink** (E Ink 800-1101 REV01, «AF 16 Tone Grayscale 5-bit Waveform Flash File Spec», taula 1 i 2; el text és
copiat literalment a `M5EPD_Driver.h`). La numeració d'E Ink és: `0 init, 1 du, 2 gc16, 3 gl16, 4 glr16, 5 gld16,
6 A2, 7 du4`, i les descripcions del full diuen:

| Codi | Nom d'E Ink | Què fa | Temps a 85 Hz |
|---|---|---|---|
| 1 | `du` | de qualsevol gris a **només blanc i negre**, **molt ràpid i sense flaix** | 260 ms |
| 6 | **`A2`** | **ràpid i sense flaix**, pensat per a passar pàgines i per a **animació de blanc i negre** | **120 ms** |
| 7 | `du4` | ràpid com `du` i sense flaix, **quatre nivells** | 290 ms |

`M5EPD_Driver.h` posa els noms al revés: el valor **6** el diu `DU4` («120 ms, fast page flipping») i el **7**
`A2` («290 ms, anti-aliased text») —les descripcions són les de l'altre—, i M5GFX ho repeteix. **Aquest projecte
ho va copiar tal com era** (`hal.h`: `kDu4 = 6, kA2 = 7`), o sigui que el que aquí s'escriu `du4` és l'**A2**
autèntic (120 ms, sense flaix, per a animació de blanc i negre) i el que aquí s'escriu `a2` és el **DU4** (290 ms).
Sospita que creix: la mesura E-7 del 1/11 (docs/95) diu que *«`du4` i `a2` no van pintar res»* amb la imatge de
quatre bits —i l'A2 **vol** dades d'1 o 2 bits per píxel, que és el que la FAQ del controlador anomena el mode
1bpp (`UP1SR` bit 2, `BGVR` per a fons i tinta)—. **No s'ha canviat cap nom**: un reanomenament toca el HAL, mig
arbre i totes les proves, i el que es fa servir avui (`du`, mode 1) és correcte amb els dos fulls.

**I el «driver que no parpelleja posant el panell a 60 Hz» no existeix per a aquest controlador** (l'amo ho
recordava bé, però era un altre aparell): el **IT8951 no té cap registre de freqüència de quadre accessible per
l'amfitrió**. El `0x0004` que sembla un «TCON setting» és `I80CPCR` (mode Intel-80 i escriptura empaquetada) i tots
els drivers hi escriuen `0x0001`; el bloc `0x1000` que els drivers anomenen *«TCon registers»* és el de les taules
de LUT (`LUT0EWHR`, `LUTAFSR`…), i la freqüència viu **al fitxer de waveform** (WBF, offset `0x18`, `0x55 = 85 Hz`)
i, com a molt, a un camp del firmware del controlador (`SCCR`) sense cap ordre publicada. La història dels 60 Hz és
del **M5PaperS3**, que **no porta controlador**: el seu ESP32 escriu la vora del panell directament, i això és el
que diuen les notes pròpies d'aquest projecte (`OLD/PaperOS/notes/trampes-maquinari.md`). Aquí no hi ha camí de
programari per a això.

**I les tres maneres d'enviar la imatge, triables en calent** (0.189.66): el mode del controlador és una cosa i **com hi
arriben els píxels** una altra, i el driver ara les té totes tres amb `hal::FlushStyle`:

| estil | què fa | el que costa |
|---|---|---|
| `rows` | el camí de sempre: **una transacció d'SPI per fila**, amb el seu prefix i la seva vora de CS | **1 100 µs per fila** mesurats a la placa, contra els 216 que voldria el cable |
| `burst` | les files s'ajunten en trossos de **2 000 bytes** que van amb **un sol prefix i el CS aguantat** fins al final del tros | el cable: 800 ns per byte i quaranta microsegons per tros |
| `2bit` | el mateix que un burst amb **la meitat dels bytes**: la imatge baixa a quatre tons i viatja a `IT8951_2BPP` | mig cable —**i a la placa només un 17 % menys**, perquè el cable no és el coll (docs/95) |

**Es tria amb `panel flush rows|burst|2bit`** i no sobreviu a un reinici a propòsit: un estil que es queda i deixa cada
pàgina malament és una placa que s'ha de tornar a flashejar per tornar enrere. **El que s'ha de vigilar**: un prefix de més
amb el CS aguantat el controlador el llegeix com dos píxels i **la pàgina llisca** (mesurat l'1/11). El tros de 2 000 bytes
**viu a la PSRAM i no a la DRAM**: a la DRAM no hi cap —el linker va dir `region dram0_0_seg overflowed by 1 688 bytes`— i
el driver SPI ja copia cada transacció al seu buffer DMA.

**I la placa va dir que el model era fals, i després va dir per què** (0.189.68 i 0.189.69). Amb una pàgina sencera de
960 × 540 i la línia ja partida en dos:

| estil | cable | ajuntar les files | total |
|---|---|---|---|
| `rows` | 282 ms | 383 ms | 665 ms |
| `burst` | 229 ms | 433 ms | 662 ms |
| `2bit` | 111 ms | 441 ms | 552 ms |

O sigui: **el cable sí que és el que deia el model** (229 ms per a 259 200 B són 1,1 MB/s, un deu megahertz ben aprofitat, i
`2bit` en fa exactament la meitat), però **la meitat del que costa una pàgina no és el cable sinó ajuntar les files**: 433 ms
per a 518 400 píxels són **133 cicles per píxel**, que és exactament una **fallada de memòria cau de la PSRAM per píxel**. La
causa és la rotació: a noranta graus, una fila del vidre és una **columna** de la imatge, i el bucle la llegeix amb un salt de
270 bytes per píxel — cada lectura, una línia de memòria cau nova (518 400 × ~100 cicles / 160 MHz = 324 ms, que és el
número mesurat). **El que costa una pàgina no és enviar-la: és girar-la.**

I el preu d'una fila del camí de sempre queda mesurat de passada, dues vegades i amb el mateix resultat: **uns cent
microsegons de transacció per fila** (282 − 229 en una pàgina, 66 − 10 en una franja de 540 files). El `kRowUs = 1100` de
l'emulador era una inferència i era **deu vegades el que és** (docs/95, E-11).

**I el gir, fet a bocins** (0.189.70): el gir continua essent de programari i pel mateix camí, però les files d'un tros
s'ajunten **en una passada** (`hal::gather_band`, a `hal/gather.h`) en comptes d'una a una. Les files d'una banda són veïnes
en `x`, o sigui que **una lectura de trenta-dos bytes de la imatge en porta vuit** amb el format ample i setze amb l'estret, i
les fallades de memòria cau baixen de 518 400 a unes 65 000. La funció és **pura** —ni bus, ni panell, ni PSRAM— i té una
prova que compara la banda amb les seves files una a una: la pàgina ha de sortir igual i només ha de canviar el que es tarda
(docs/95, E-11). De passada, el tros del bus passa de 2 000 a **3 840 bytes** —vuit files d'una pàgina de 960 píxels a quatre
bits, setze a dos, i per sota dels 4 092 que caben en una transacció d'aquest bus—: una pàgina són 68 trossos i no 130.
**I la primera mesura de la banda va dir dues coses** (placa, 15/11): la pàgina amb `burst` va baixar de 662 a **537 ms**
(cable 216, ajuntar **321** —no els 100 que s'esperaven—) i, en canvi, el camí de sempre va **empitjorar**: 383 → **527 ms**
d'ajuntar, perquè també se'l va fer passar per la funció nova, que amb una fila de banda és més lenta que el bucle antic.
Dos arreglos, cap d'ells un canvi d'idea:

1. **El byte es fa al registre**: the first version kept an accumulator, a count and a place **per row of the band**, three
   arrays indexed by a variable —more memory traffic than the pixels they carried—. Now the `per_byte` pixels of a byte are
   read in a row and the byte is written once (docs/95).
2. **El camí de sempre també ajunta per bandes**: the sending is still one row per transaction —that is the style— but the
   rows come out of a band gathered in one pass, so the slow path is not slow because of the turn.

**I a la placa va acabar baixant a 99 ms** (0.189.75, docs/95): tres passos, i cap d'ells era el que semblava. El primer,
les files en bandes (433 → 321). El segon, veure que el número no responia al codi i
preguntar-ho **al rellotge** —318 ms a 160 MHz i 232 a 240, o sigui que era codi i no memòria—. El tercer, llegir el binari:
el bucle feia **35 instruccions i 13 càrregues per píxel**, vuit de les quals eren les variables que una lambda `[&]` captura
per referència, rellegides d'un punter a cada píxel. Sense lambda, amb els dos formats com a especialitzacions de compilació i
la imatge caminada amb punters: **11 instruccions per píxel i 99 ms**.

**El rellotge del bus es canvia en calent** (0.189.77): `panel bus` diu a quants megahertz van els píxels i `panel bus 20` ho
canvia, perquè el número que queda per mesurar no costi un flaix i una sessió per valor. El panell accepta de cinc a
vint-i-quatre; **no se'n recorda cap reinici**, com l'estil, i si el controlador deixés de contestar a un rellotge massa alt,
un reinici el torna a deu.

**I el bus no pot portar dos rellotges amb un sol CS** (0.189.82, i això és el que va deixar la placa sense despertar): the
attempt was two devices on the same bus —ten megahertz for the words, twenty-four for the image— and **it cannot work**, perquè
`spi_bus_add_device` **reserva una de les tres línies de CS del perifèric i la connecta al GPIO que li dones**
(`spicommon_cs_initialize(host, spics_io_num, freecs, …)`): the second device asked for the same pin and **the second routing
takes the pin away from the first**, so the panel's commands were never selected again and the board answered nothing at all.
El camí correcte, quan es torni a provar, és el del fabricant: **CS per GPIO** (els dos dispositius amb `spics_io_num = -1` i
la línia pujada i baixada a mà), que és com `M5EPD_Driver::WriteCommand` ho fa. **Mentrestant el bus va a deu megahertz i el
que es va guanyar amb les bandes i el bucle es queda**, que és de 662 a 313 ms de preparació.

**I el bus són dos rellotges i no un** (0.189.81, **retirat**): a vint-i-quatre megahertz les escriptures de píxels van bé i **les
lectures tornen brossa** —el panell es va creure de 270 × 480 amb la imatge a `0x010e010e`, i la pantalla es va quedar com
estava—, o sigui que el bus porta **un dispositiu per a les paraules a deu megahertz** (comandes, registres, el `GetDeviceInfo`
de l'arrencada) i **un altre per a la imatge** al rellotge que demani `panel bus`. És el que fa el driver de referència. I el
handshake es refusa si les seves quatre paraules són la mateixa, que és el que escriu un MISO que no arriba (docs/95).

**I el que quedava era el bus, i el bus es va pujar** (0.189.78): amb `panel bus` movent el rellotge en calent, la placa va
dir 214 ms de cable a deu megahertz, 136 a setze, 110 a vint i **84 a vint-i-quatre**, amb la pàgina ben pintada als quatre.
**L'SPI va a vint-i-quatre de fàbrica** i la preparació d'una pàgina queda en **182 ms** (84 de cable i 98 de gir) contra els
662 de què es va partir. **I l'última mica, la mida de la banda** (0.189.80): the rows of a band are neighbours in `x`, so a band of sixty-four rows is
what a thirty-two-byte line of the PSRAM's cache carries at four bits a pixel — with eight rows, seven eighths of every line
were thrown away. The gathering and the sending stop being the same buffer: thirty-two kilobytes to gather into, and four
thousand and ninety-two byte chunks out to the bus.

**I aquí s'acaba el que el driver pot fer**: amb 182 ms de preparació i 1 150 de tinta, el que domina
una pàgina és la tinta, i això ja no és un número d'aquest fitxer sinó de la qualitat que cada pintada demana.

**El tercer estil era un bit per píxel i no podia ser-ho** (0.189.67, corregit): el camp de format de `LD_IMG_AREA` té
**quatre valors i cap cinquè** —`IT8951_2BPP 0`, `IT8951_3BPP 1`, `IT8951_4BPP 2`, `IT8951_8BPP 3`, a `IT8951_Defines.h`
del fabricant—, o sigui que el `0` que s'enviava volia dir **dos bits per píxel** i el controlador llegia la meitat de la
imatge. I a sobre s'escrivia el format a `UP1SR` (0x1138), que és el registre de **paràmetres d'actualització** i no cap
registre de format: cap de les tres llibreries del fabricant hi posa mai un format. L'estil demana ara el que el controlador
té (`2bit`, amb la imatge baixada a quatre tons) i **no toca cap registre**. **El mode d'un bit continua essent una
pregunta oberta**, i la pista que en queda és la d'aquest mateix document: la FAQ del controlador l'anomena `UP1SR` **bit 2**
amb `BGVR` (0x1250) per als dos colors, que és un mecanisme diferent del camp de format —i que caldria provar amb una
escriptura de read-modify-write d'aquell bit, no amb el format sencer.

**I el que això ja diu a l'emulador, que modela els tres preus**: un frame de 64 × 64 costa **194 ms de present amb `rows`,
5 amb `burst` i 4 amb `2bit`** —o sigui que el coll passa a ser la tinta de `du`, que són 120 ms, i no el cable—. A la placa
el número s'ha de mesurar amb el log, i és la feina que queda del refresc ràpid (docs/95, E-11).

**I cada pintada ara diu què ha costat** (0.189.67): `display::present` mesura el rellotge abans i després de demanar la
pintada i escriu una línia per cada una,

```
[D][GLASS] flush du 194 ms (rows)
[D][GLASS] flush du 5 ms (burst)
[D][GLASS] flush gc16 1669 ms (rows)
```

que és **el que ha costat la crida** —la transferència del rectangle i la comanda, que es paguen en aquell moment— i no la
tinta, que ve després i la pregunta `idle()`.

**Però el número de la crida no compara els tres estils** (0.189.68, la placa el 15/11): una pintada **paga primer la
tinta de la pàgina anterior**, perquè el motor ha d'estar quiet abans de carregar-li una imatge a sobre, i això són fins a
més d'un segon. Es va veure de seguida: la mateixa franja de 48 × 540 va dir `1 661 ms (rows)` i `1 670 ms (burst)` —el
mateix número per a dos enviaments que no s'assemblen—, i aquell `stats` ho confirma des de l'altra banda
(`glass.flush` 10 crides, 964,8 ms de mitjana, 1,67 s la pitjor). El número que compara els estils és el que ara porta la
línia del **driver**, que surt per a cada pintada de qualsevol porta —també les de l'splash, que no passen per `present`—:

```
[I][PANEL] paint 0,0 48x540 mode 5 gld16 at 0x1236e0 (page 0,0 540x48, 540 rows of 24 B, 11 ms on the wire as burst, 1 223 ms of ink before it)
```

**`on the wire` és el que canvia amb l'estil** i `of ink before it` és el mateix per als tres.

**`LUTAFSR` és l'estat del motor de LUT i no la mesura de la tinta** (0.179.41): `0x1224` val `0000` quan cap
motor de LUT del controlador està corrent, i el log l'escriu a la **primera** lectura que contesta, amb els
mil·lisegons que han passat **des del pintat fins a la pregunta** —i no la durada del pintat—. Una línia com
`LUTAFSR 0000 3632 ms after the paint` vol dir que la pregunta s'ha fet tres segons i mig després d'enviar la
pàgina. El que la feia trigar tant era el conductor mateix: el mínim que s'espera abans de preguntar
(`measured_ms_`) s'aprenia d'una mesura que s'acabava quan el registre contestava, o sigui que una lectura que
el bus va retenir dos segons es desava com **dos segons de tinta** i, com que el número només creix, **totes**
les pàgines de la tirada esperaven això abans de preguntar un registre que hauria dit `0000` de seguida. Ara una
lectura que s'ha hagut de repetir no mesura res i el mínim après té un sostre de **1 200 ms**
(`kEngineFloorCeilingMs`): torna a decidir la resposta del panell i no el pitjor dia del bus.

**I la lectura que es feia just després del pintat no hi és** (0.179.41): era un debug de la 0.118.8 —la línia
`engine status read after the paint`— **el resultat del qual no s'usava** (només es deia si la lectura havia
funcionat), i com que `read_words` espera la línia **BUSY** abans de prendre un mot, aquella línia esperava **tot
el pintat** (de 120 a 450 ms, i el sostre de tres segons quan el motor anava lent) amb el torn del nucli a dins,
en un pintat que aquest conductor demana **sense esperar**. A més era dins del `now - painted_ms_` del qual el
següent espera aprèn la tinta d'una pàgina, o sigui que el conductor apuntava el seu propi bloqueig com a tinta.
L'amo ho va preguntar amb el log a la mà: *«son necesaris per fer la feina? o es un debug antic que està fent
consultes innecessàries a la eink fent-la més lenta?»*. El que diu el motor es llegeix igualment, on vol dir
alguna cosa: a `wait_paint`. I **`idle()` tampoc no s'hi bloqueja**: ara mira primer el pin (un GPIO, baix vol dir
"encara pinta") i només pregunta el registre quan la línia diu que la tinta s'ha aturat, o quan ja ha passat la
tinta d'una pàgina sense que la línia es mogui.

**I tres segons de sostre volen dir que el pintat gros pot no haver acabat** (0.118.2): una pàgina sencera
en `gc16` mou la tinta dos o tres segons, o sigui que un `flush` que arriba just després en rep un **no**,
amb el motiu del driver (`the previous paint did not finish`, `last_error()`). El que pinta una pàgina
sencera i no té ningú esperant —la **splash** d'un moment del sistema— **espera la tinta i torna a
demanar** (`splash.cpp`, `settle`: pregunta `idle()` cada 20 ms fins a 6 s) i, si tot i així el panell no
l'agafa, el missatge porta el motiu del HAL i no només «el panell no ha pres la imatge». Va sortir a la
placa: `splash boot-alt` dos segons després d'arrencar contestava `the panel did not take the picture`
perquè la neteja sencera de l'arrencada encara s'estava movent (docs/92 §33).

**I una pàgina refusada no es llença** (0.118.2): el camí del `ui.present` es queda amb el dany que el
panell no ha pres (`paint_root` el torna a la llista) i el torna a demanar quan el vidre el pot prendre —
`ui::owes_paint()` ho diu, i el torn del nucli pregunta `ui::idle()` quan hi ha alguna cosa a deure, i
`ui.idle()` de l'app també—. Abans d'això la llista s'esborrava passés el que passés, i una app que fa
**un sol `present` i es queda esperant** es quedava amb la pàgina de l'arrencada al vidre: **l'app s'executa
i no mostra res**, que és el que l'amo va veure a la placa (docs/92 §33).


### La barra del sistema: l'hora, el títol i la càrrega (1/11)

El sistema té una franja pròpia a dalt del vidre (`lib/kernel/bar.cpp`, **en C++ i no en Lua**) amb
l'hora a l'esquerra, el títol de l'app del davant al mig i la càrrega a la dreta, sobre blanc i amb una
línia negra de dos píxels a sota. **48 files**, i la vista d'una app comença a la fila 48: una pàgina que
surt malament no es pot emportar l'hora per davant (docs/30).

- **Es pinta ella mateixa i només quan canvia alguna cosa seva**: un minut nou, un títol nou, una càrrega
  nova. Cada repàs és un `present` de la seva franja i res més. **Un torn sense novetat és dues
  comparacions i prou**: ni lectura del rellotge, ni de l'ADC, ni pintada. I el que es compara és el que
  es dibuixa: la lectura es fa **una vegada** i els valors viatgen fins al `paint`, perquè llegir-los una
  altra vegada dins del pintat va fer que la barra es repintés cada mig minut sense que res hagués canviat
  (mesurat a la placa l'1/11).
- **La càrrega té una banda morta de dos punts**: el mesurador és un ADC darrere d'un divisor i una
  lectura de 83 torna com a 84 a la següent. La barra ensenya el número però no repinta per un píxel
  d'electricitat, perquè un repintat és tinta i la tinta no és gratis. El rellotge es llegeix com a molt
  cada 2 s i la càrrega cada 30 s.
- **I les icones de la barra són les del projecte del paper, a la seva mida** (0.189.56, l'amo: *«el projecte
  PaperOS te unes icones exquisides a la seva barra del sistema, usb, wifi, bluetooth i l'icona de batería, diría que
  també el de càrrega… Portem aquestes icones a la barra de sistema del lunokOS. La barra de sistema de lunokOS es
  mes baixeta que la de PaperOS, vull usar les mateixes icones, sense escalar per la diferencia d'alçada de la barra»*):
  `lib/kernel/src/bar_icons.cpp` és el port **número per número** de `drawWifiGlyph`, `drawSerialGlyph`,
  `drawBluetoothGlyph`, `drawNtpGlyph`, `drawBatteryGlyph` i `fillBolt` del `PaperOS/src/ui/title_bar.cpp` —el ventall
  del WiFi, la marca de l'USB tombat, la runa del Bluetooth, el rellotge de sorra de l'NTP i la figura de la bateria amb
  el seu llamp—, i **no són un fitxer de píxels sinó un dibuix de línies**: per això es poden portar sense escalar, perquè
  cada un es mesura contra la caixa que rep i la barra els dóna **la caixa del projecte del paper**, 40 × 28 per als
  glifs i 46 × 26 per a la figura. Una barra de 48 files no els toca: el que la barra decideix és **on** van. Les tintes
  mesurades al vidre del projecte del paper (18/10) i comprovades aquí amb un cas de `test_bar` que compta els píxels:
  el ventall **31 × 27**, la marca del port **38 × 21**, la runa **18 × 26**, el rellotge de sorra **19 × 21** i la figura
  **46 × 26**. **I què diu cada icona és un fet del nucli i no una decoració**: el ventall és la ràdio encesa (`net::state()`
  o una feina en curs), la runa és una provisió que va per Bluetooth i no per l'apunt d'accés (`net::over_bluetooth`), la
  marca del port és **el cable** —la placa s'està alimentant o una app té el port— i el llamp de la figura és la bateria que
  s'alimenta. El títol es centra **al tros que li queda** entre l'hora i les icones, que és exactament la regla del projecte
  del paper (`titleRect`).
- **I les caixes de les icones són de l'amplada de la seva tinta, no de 40** (0.189.57): the box of the paper project is
  40 × 28 and the drawings are narrower —the fan 31, the port 38, the rune 18, the hourglass 19—, so a box of 40 for a
  drawing of 18 is 22 pixels of air the title cannot use, and with five icons on a strip of 540 that air **is** the title.
  `bar_icons::ink_of()` is what the bar asks, and **an icon that is not lit has no box at all**: the title takes the room
  back. The price is that the title grows when an icon goes out, and that is a whole strip at `gld16` on the turn the signs
  change —a handful of times an hour, when the radio comes on or the cable goes in— and not a jump on every turn, because
  the boxes are still the same while the signs are the same.
- **Dues icones més, i són a l'esquerra** (0.189.57, l'amo: *«Afegeix també al costat del rellotge l'alarma en plan
  despertador alarma i el rellotge de sorra per el temportizador»*): the **bell** says an alarm is set —a repeating one, the
  one a person sets for a time of day— and the **hourglass** says something is counting down, which is the clock being set
  from the network **or** a timer somebody left (`alarms::has_once()`). They sit between the hour and the title, so the
  strip reads outwards from the clock.
  **I només s'encenen per a les alarmes que sonen** (0.189.154, l'amo: *«Veig l'icona d'alarma a la barra de sistema, està
  activa per algun motiu?»*): la cua del sistema també porta la feina de fons d'una app —`battery` hi arma una alarma que es
  repeteix cada deu minuts per prendre una mostra, i és `persist`, o sigui que era a la cua a cada arrencada— i la campana
  s'encenia per ella com si una persona hagués posat una alarma. Les dues preguntes de la barra compten ara **només les
  alarmes que han de venir al vidre** (`raise_to_front`): la del rellotge de cada dia i el compte enrere el demanen, la mostra
  de la bateria no, i per tant la franja diu el que una persona ha posat i no el que el sistema s'ha manat a si mateix (docs/60). The alarm is **`U+23F0 ALARM CLOCK`** —a bedroom alarm clock: two bells, the hammer,
  the face, the two handles and the hand— and it is the only icon of this bar that is a **table of bits** and not lines. It is
  a drawing that was tried three times by hand and did not work at twenty pixels: two bells over a face is a smiley, the bells
  glued to the ring are an eyebrow, and a bigger bell with the hammer between the two is an `M`. The table is generated and
  not edited: `python3 tools/make_bar_icon.py` writes `lib/kernel/src/bar_icons_art.h`, it takes the glyph from **Noto Color
  Emoji** at its only size and steps it down to the height asked for, and `ink_of` publishes **18 × 21**, which is the table.
  **Llicència**: la font és SIL OFL 1.1 i el codi de Noto és Apache 2.0 (`https://github.com/googlefonts/noto-emoji`), i
  l'atribució viu al fitxer generat. L'hora de sorra és **19 × 21** i és un dibuix de línies del projecte del paper.
- **I la casella del sistema és l'altre dibuix que ve del clipart** (0.189.90): l'amo la va trobar *«massa bàsica»* quan era un
  quadrat amb una X i la volia **de càpsula** —*«Estic molt segur que al fontawesome trobaràs el que busques»*—, i les dues
  imatges són `U+F204 TOGGLE OFF` i `U+F205 TOGGLE ON` de **Font Awesome 6 Free (Solid)**, rasteritzades a **60 × 40** per
  `python3 tools/make_toggle_mark.py` (que escriu `lib/kernel/src/toggle_mark_art.h`, i tampoc no s'edita a mà). El cos es busca
  i no es demana —el que mana és l'alçada de la tinta—, es dibuixa a quatre vegades la mida i es baixa amb la mitjana de cada
  bloc, que és el que fa els caps rodons rodons. **El gruix el fa la mida i res més**: un pas d'engreixar la tinta després
  de rasteritzar-la tanca el buit entre el botó i el marc i hi deixa fils blancs (vist a l'emulador i descartat per l'amo:
  *«res de bold»*). **Llicència**: les icones de Font Awesome són CC BY 4.0 i la font SIL OFL 1.1, i l'atribució viu al
  fitxer generat i al README.
- **I el candau de `Device Lock`** (0.189.97): la vista el dibuixa amb les primitives del canvas —dues barres i quatre
  discs fan un requadre de cantons rodons, i el disc del sistema fa la resta— i la icona de l'app és **el mateix dibuix a
  una altra mida**, que és el que fa que la cel·la del menú i la pantalla siguin el mateix candau: les proporcions són un
  sol joc de números a `apps/device_lock/device_lock.lua` i a `python3 tools/make_lock_icon.py` (448 × 448, el mateix
  costat que la resta d'icones del menú). Ve de `OLD/PaperOS/src/apps/lock_app.cpp`, on l'amo va haver de demanar dues
  vegades que el candau fos **un dibuix de línia i no una llosa**: el cos és un contorn, la nansa un anell amb les cames
  dins del cos, i la tinta és només la placa del pany, el forat, la ranura i els dos reblons (docs/45).
- **I la casella d'una entrada de menú, que era l'última que quedava feta a mà** (0.189.91): the checkable entry of a menu
  —the View menu of the editor, `read only` and `wrap`— carried a square of two strokes of two pixels and a square of ink
  inside them, drawn by `menu_bar_draw`. A menu row has no room for a capsule, so the same tool writes a second pair,
  `U+F0C8 SQUARE` and `U+F14A CHECK-SQUARE` **del pes Regular** (al Solid el quadre és ple, ■, i no és una casella) a
  **18 × 18**, i el nucli els posa amb el mateix `draw_bits` que la càpsula.
- **I la càrrega es pregunta al `platform` i no a `power::`** (0.189.57): `bar::boot` runs before `power::boot`, which is
  after the shell because it may bring an app back from a sleep, so the first read of the gauge answered nothing and the
  strip said `--` with no figure beside it **for the whole half minute of the read rhythm**. The boot asks the gauge the
  same way for the picture it shows, and the bar does it here (docs/60, docs/85).
- **La qualitat la tria ella, i la regió també** (0.85.2): la franja és **tres caixes de text** —l'hora, el títol
  i la càrrega, cadascuna mesurada amb el text més ample que pot portar— més les tres de les icones i la de la figura, i
  el que canvia és la seva caixa
  i no la franja. Un títol nou és el mig de la barra i no la barra; un minut és el dígit i no el mig. La
  qualitat és **`gld16`, que neteja i no fa cap flaix**: `du` pinta però no neteja —i el dígit d'abans es
  queda al vidre—, `gc16` neteja a canvi de dos ensurts i `a2` deixa dos setzens del que hi havia. És la
  regla que el projecte ja tenia escrita: res que parpellegi no arriba al vidre, i netejar és `gld16`
  (docs/92). **I la franja sencera també va a `gld16` des de la 0.179.40**: el boot, una tornada de pàgina i
  una imatge que hagi rentat el vidre són una franja sencera, i `gc16` allà eren dos flaixos de tota la franja
  cada cop; la franja és **de dos nivells** (paper 15, línia i lletres 0, `font::draw` baixa un sol nivell), o sigui
  que el rentat que deixa dos setzens aterra **al** nivell i no a prop —el que queda de la pàgina de sota és un
  tretze i no una banda negra—, i un cas de la suite comprova que no hi ha cap píxel de la franja que no sigui zero
  o quinze (`test_the_strip_of_the_bar_is_two_tones`, docs/50).
- **I el dit que hi cau és del sistema** (0.179.22): arrossegar-lo cap avall obre el **panell del sistema**, una
  targeta d'un terç de la pàgina que penja de la franja —buida pel que fa a commutadors, i és on aniran els del
  sistema—, i la pàgina de sota queda retinguda mentre hi és. **El botó de la cantonada dreta tanca l'app del
  davant** (0.179.23) i ho fa pel camí únic de sortir d'una pàgina —i **no es dibuixa sobre un llançador** (0.179.25), que
  és la pàgina que no es deixa—, i **el de l'esquerra, una fletxa, torna l'app a la vista de sota** (0.179.52) pel mateix
  camí que el pessic de gran a petit, i tampoc no es dibuixa quan l'app no té cap vista a sobre de la primera. Les maneres de tancar la targeta són totes de la persona: un toc fora seu i un
  lliscament cap amunt que hi comenci (0.179.49). El gest, el llindar, la rodona i què hi sent
  l'app són a docs/30, «El panell del sistema».
- **I una app d'administrador pot treure la barra i deixar-la fora** (0.189.99): `admin.LockScreen(true)` és el vidre
  sencer de la 0.136.0 amb el pany posat, i el que tanca és **aquesta targeta**: el gest que l'obre comença **a la
  franja**, o sigui que sense franja no hi ha targeta, i sense targeta no hi ha la creu que tanca l'app del davant ni la
  fletxa que torna a la vista de sota. És el que necessita una pantalla de bloqueig —`apps/device_lock`, que és l'única
  app amb `"admin": true` avui—: una porta que es deixa obrir amb un gest del sistema no és una porta. **El pany no és una
  manera de tancar el sistema**: la tecla de la placa i el final de l'app el tornen a obrir, perquè una placa que es
  quedés sense franja i sense pàgina no la pot fer servir ningú (docs/30, docs/40).
- **I els tres tocs que tanquen l'app també es poden cedir** (0.189.101): el sistema pren el `triple` de la cua abans que la
  pàgina el llegeixi —és la sortida de tota app, i una pàgina no l'ha de poder amagar— i `admin.triple_tap_close(false)` el
  deixa a la cua per a una app que el necessita, que és el que fa una pantalla de bloqueig que compta els tocs per obrir-se.
  La cua els torna a prendre quan l'app s'acaba, i les tres premudes de la tecla del mig no es toquen: són l'altra carretera i
  la vàlvula d'una pàgina que no respon. **I un `ui.button` sense vores sobre una pàgina no té buffer** (0.189.101): la seva
  cara es pinta al canvas del pare, i el `draw_clear` dels cantons esborrava tota la pàgina; ara la cara és la caixa del botó
  (trobat amb el candau del `Device Lock`, docs/30).
- **I quan el vidre queda net sota seu, s'ha de dir** (0.164.3): una imatge de l'arrencada de l'app
  (`splash::show`) renta **tot** el vidre —la franja amb ell— i la barra, que només es pinta quan canvia
  alguna cosa seva, després no sabia que l'hora i la càrrega havien desaparegut: el títol nou repintava la
  **caixa del títol** i prou. Per això el sistema demana un `bar::refresh()` (una pintada sencera de la
  franja, `gc16`) **just després d'una imatge que ha rentat el vidre** —la de l'app a `run_app` i la que
  algú demana amb l'ordre `splash`— i **en tornar de la pantalla sencera** (`ui::make_view`). **Els moments
  que baixen la placa no el demanen**: el `poweroff`, el `reboot` i la son pinten la seva imatge i la barra
  no ha de tornar-hi fins a la propera arrencada.
- **Un splash no és una imatge de programa de dibuix: és el pla del vidre** (0.179.15). Un `.bmp` de 540 per
  960 amb vuit bits és **519 478 B** i el panell en té quatre, o sigui que el camí antic llegia el fitxer
  sencer, convertia 518 400 píxels amb una lluminància i un llindar del dither **un a un**, escrivia una
  segona memòria de 246 240 B i la blitejava. El que la placa llegeix ara és un **`.e4`**: quatre bits a
  píxel, un byte per a dos, el de l'esquerra al nibble alt, `width/2` bytes per fila i **cap capçalera** —
  exactament l'embalatge de la imatge del controlador—, que són **259 200 B** del vidre de 540 per 960 (la
  franja del sistema inclosa, que es pinta a sobre després).
  Es llegeix en un sol bloc i es bliteja a `0,0` amb un `memcpy` per fila: ni descodificació, ni segona
  memòria, ni el rentat previ (el blit cobreix cada píxel). L'ordre d'extensions fa que **`e4` guanyi** sobre
  el PNG, el BMP i el JPEG d'aquell moment, i un pla que no és la mida d'aquest vidre es refusa amb la raó
  («the plane is not the size of this glass») en comptes de pintar-se en una cantonada.
  **`tools/e4pack.py`** és qui el fa, i la seva aritmètica és la del descodificador i no una segona: la
  lluminància 601 (`(77r + 150g + 29b + 128) >> 8`) i el dither ordenat de quatre per quatre
  (`(grey * 15 + bayer[y & 3][x & 3] * 16) / 255`), o sigui que una imatge empaquetada és la que el sistema
  hauria pintat, píxel a píxel. **`make_disk` la fa servir a cada build** d'una placa amb vidre i deixa el
  `.bmp` fora de la imatge: les tres imatges de l'arrencada passen de 1 558 434 B a 777 600 B, i el que
  arriba al controlador per a cada una baixa de 519 478 B llegits i descodificats a 259 200 B copiats.
- **I una imatge que no és un pla es retalla a la seva tinta i el fons de la pàgina el diu el seu marge** (0.179.39):
  el camí del PNG/BMP descodifica el fitxer, mesura **l'anella exterior** de la imatge —si més de la meitat dels seus
  píxels són foscos, la pàgina és tinta (0), i si no paper (15); un empat és paper— i **descarta l'aire uniforme** al
  voltant del dibuix (un píxel és dibuix quan és a més de dos nivells del fons, que és el que deixa el dither d'una
  àrea plana), o sigui que el que es bliteja i el que el panell mou és **el dibuix i prou**, centrat. La columna on
  comença es fa **parell**, perquè el nibble de l'esquerra d'un byte és el píxel del costat. El segell de la versió es
  pinta amb el nivell **contrari** al de la pàgina (tinta sobre paper, paper sobre tinta), i la línia del registre diu
  el dibuix i la pàgina: `SPLASH /apps/x/res/splash.png, 302x448 at 119,256, a drawing of 420x520, the page is paper`
  (docs/30, docs/40).
- **I la imatge s'ha d'acabar de moure abans que la placa se'n vagi** (0.165.1): `splash::show` espera la tinta
  que es movia **abans** de la seva imatge i després demana un `gc16` de tot el panell, que és mig segon de
  tinta. Un `reboot` que talla la placa on la crida l'ha trobada deixa el vidre amb la pàgina d'abans: el que
  va veure l'amo era la imatge «massa tard, i no es veu». `splash::wait()` dona un torn al panell —el
  conductor pren l'ordre de la pintada al torn següent— i després espera `idle()` amb el sostre que el splash
  ja tenia, perquè un panell que no contesta no ha de retenir una placa. El demanen els **tres moments que
  baixen la placa**: `reboot`, `poweroff` i el son profund (docs/50).
- **El text és una font de 5 × 7 píxels** (`lib/kernel/font.cpp`), escalada al doble i dibuixada amb un
  rectangle per tirada horitzontal i no un per píxel: 295 bytes de taula i cap decodificador. **Només
  majúscules**: una minúscula es dibuixa com la seva majúscula, que és el que feia una barra de títol
  durant dècades (docs/30).
- **L'hora és la del rellotge, en UTC**, perquè el sistema té una època i cap zona: una zona és un ajust,
  i el dia que n'hi hagi serà una línia d'aquí i no una endevinalla (docs/60).
- **La càrrega es llegeix com a molt cada 30 s i l'hora cada 2 s** —una lectura és l'ADC i no un
  registre— i sense indicador de bateria la barra diu `--` (docs/50).


### Una sola orientació per al vidre, per a la pàgina i per als dits (1/11, mesurat)

El xip del tàctil compta en un marc propi —la seva configuració diu **540 × 960**— i la pàgina on dibuixa
una app també és **540 × 960** (vertical), perquè el gir del vidre es fa tot en software. **Els dos marcs
són el mateix, i del dret**: quatre tocs, un a cada cantonada de la pàgina, i el que el xip va dir (placa,
1/11, docs/95 E-6)

| Toc | `raw` del xip | Pàgina |
|---|---|---|
| dalt a l'esquerra (el rellotge) | `46,28` | `46,0` |
| dalt a la dreta | `515,49` | `515,21` |
| baix a l'esquerra | `31,926` | `31,898` |
| baix a la dreta | `500,920` | `500,892` |

La `x` creix cap a la dreta i la `y` cap avall, igual que es pinta la pàgina: **ni quart de volta ni
mirall**. El `touch.caps()` anterior donava el marc del xip i el `ui.caps()` el del vidre, i una app que
pintava un botó i comprovava si l'han tocat havia de girar els números ella mateixa; ara no.

| Qui | Què dona |
|---|---|
| `display.rotation()` | 0, 90, 180 o 270: quant està girat el contingut del vidre respecte del marc del xip (270 de fàbrica) |
| `touch.caps()` | **la mida de la vista de l'app** (540 × 912 amb la barra de 48 files), que és on es dibuixa |
| `touch.read()` i `touch.gesture()` | coordenades **de la vista de l'app**, no del xip |

La conversió es fa **una sola vegada**, a `turn_frame()` de `lib/kernel/src/touch.cpp`, just quan el marc
es llegeix del xip (`kernel/orientation.h`, que és aritmètica pura i té prova pròpia al PC) i després es
desplaça al lloc de la vista. Com que a **270** el marc del xip i el de la pàgina coincideixen, el gir és
**zero** amb la pàgina de fàbrica (`kChipPage = 270`): girar la pàgina 180 graus gira el dit 180 graus, i
prou.

**Cap punt es descarta.** La primera versió llencava els punts que no queien dins del marc que esperava, i
el resultat va ser **un tàctil que no contestava gens** (placa, 1/11): un filtre que, quan s'equivoqui,
deixa l'app sense toc és pitjor que no filtrar. Un dit que cau a la barra és de la barra, però es queda
—es filtra a la vora de la vista—.

Per mesurar-ho hi va haver, durant la 0.18.0 i la 0.18.1, una línia de consola amb el primer marc de cada
toc (`touch: raw 46,28 (chip 540x960) -> page 46,0 (view 540x932)`). **Ja no hi és** (0.18.2): les quatre
cantonades estan apuntades aquí i la prova del PC en guarda els quatre números, i una consola que escup una
línia per toc no és una consola d'un sistema que es pugui fer servir. Si algun dia es canvia de panell o
sospita un mirall, es torna a posar allà mateix: és una línia.


### La imatge d'un moment: la splash (0.111.0)

**Hi ha una sola cosa del sistema que pinti el panell fora d'una pàgina**, i és la imatge d'un moment: a
l'arrencada, quan la placa marxa, quan està al cable. Es fa així perquè són justament els moments en què no hi ha
cap app al davant —una placa que s'aixeca encara no té ni prompt, i una que baixa acaba de tancar la seva app—.

- **La neteja és de tot el panell**: `clear(15)` i un sol `flush(gc16)`, que és el mode que s'emporta el fantasma del
  que hi havia i no només el rectangle de la imatge. Una imatge centrada sobre un tros de la pàgina de abans és una
  imatge amb un fantasma a sota.
- **La imatge va al mig i no s'escala mai**: una imatge més gran que el vidre es talla pel seu centre i una que hi
  cap es deixa tal com és. Escalar quatre bits és un blur, i el que fa la splash és ensenyar el que hi ha al fitxer.
- **I el fitxer és el dibuix, no la pàgina** (0.183.4, l'amo: *«per què l'splash charging i lowbatt són a pantalla
  completa? són blancs de fons!»*): el que es pinta ja era el dibuix —`splash::show` treu la vora uniforme de la imatge
  (`drawing_of`) i el centra—, però el paper que el volta s'escrivia al disc, es llegia sencer, es descodificava i es
  llençava: 540 × 960 de blanc per a una insígnia de 456 files. Les sis imatges dels moments es retallen **a la caixa de
  tinta** (regla del sistema: un píxel a més de dos nivells del paper és dibuix) i el vidre no canvia ni un píxel, perquè
  el que es pintava era el dibuix i segueix centrat. **Els dibuixos del boot no es toquen**: són pàgines del vidre
  sencer i la pàgina és el dibuix.
- **Els moments són paraules**, i la paraula és el nom del fitxer: `/res/splash/<moment>.png` (o `.bmp`, `.jpg`) i,
  si el moment no en té, `/res/splash.png` per a tots. Un moment sense imatge **no pinta res i diu per què**: una
  splash que dibuixa una endevinalla és una placa que amaga un fitxer trencat. Des de la 0.114.0 són les paraules de
  la comanda, i des de la 0.183.0 són **set**: `boot`, `poweroff`, `reboot`, `charging`, `sleep`, `lowbatt` i `oops` —
  el mateix joc que accepta la comanda i que un manifest pot nomenar amb `splash` (docs/40). **`boot-alt` ja no hi és**
  (l'amo, 11/11: *«boot-alt.png i boot.png no han d'estar a la llista del boot»*): el boot no és un dibuix sinó **els
  dibuixos que el disc porta**, i un segon mot per a un segon logotip era un commutador del pintat que les imatges ja fan.
- **I el boot en té més d'un** (0.178.5): un moment pot portar diverses imatges amb el mot i una cua —`boot-2.bmp`,
  `boot-3.bmp`, `boot-5.bmp`—, i **una d'elles es tria amb `ISystem::random()` a cada engegada**, o sigui que la
  mateixa placa no diu el mateix dues vegades (i un `--seed` de l'emulador sí, que és el que fa repetible una prova).
  Una cua que sigui **el mot d'un altre moment** no és un dibuix d'aquest —`boot-alt.png` era el d'un moment i no ho
  és—, i un nom que no és d'una imatge que aquest sistema llegeixi no entra al sorteig. `splash` i `cfg boot`
  ensenyen **la imatge que es dibuixaria**, i no el nom pla del moment, perquè una llista que digués `boot.png` mentre
  el boot dibuixa `boot-3.bmp` seria una llista que menteix.
- **La del son és un dibuix i no una paraula** (0.189.55, l'amo: *«vull canviar l'splash del estat Sleep per una imatge
  adorable d'un os bru dormilega… la vull de fons transparent»*): `sleep.png` és un os que dorm, i **el "fons transparent"
  d'una tinta electrònica és el paper**: el panell no té alfa, i el que el sistema fa amb una imatge és rentar el vidre
  amb el color de la **vora** de la imatge (`splash::background_of`) i pintar-hi el dibuix al mig. L'arxiu que va arribar
  era un WebP **sense canal alfa i amb el tauler de quadres blanc i gris dibuixat als píxels** —la falsa transparència
  d'una previsualització—, o sigui que `tools/make_bear_sleep.py` treu el tauler amb un **flood fill des d'una cantonada**
  i una tolerància de seixanta (els dos grisos del tauler són 255 i 239, quaranta-vuit de distància sumant les bandes, i el
  marró és a dos-cents), es menja **un píxel de l'aire de la vora** —el dibuix estava antialiasat contra els quadres grisos
  i aquesta vora es llegiria com una franja grisa sobre paper blanc—, retalla el dibuix, hi deixa **vint-i-quatre píxels de
  paper** al voltant (el que fa que la vora que `background_of` llegeix sigui paper i la pàgina surti blanca) i el deixa a
  **500 × 310**, que és el que es veu al vidre. **La línia `by cleanpng.com` que portava el fitxer a sota de l'herba no hi
  és**: no és el dibuix, i el dibuix és el que es pinta. La font es queda a `tools/splash-sources/` com les altres.
- **I el lector de PNG va haver d'aprendre a ajuntar els trossos del stream** (0.189.55): a 79 KB, aquesta és la primera
  imatge de l'arbre que un programa de dibuix escriu en **més d'un `IDAT`** (un cada 65 536 bytes de stream), i el lector
  la refusava —`the file is not a picture this system reads`— perquè la comparació que havia de dir «aquests dos `IDAT` van
  seguits» demanava que els dos cossos es toquessin, i entre dos cossos hi ha **dotze bytes** (el CRC del davant, la mida
  d'aquest i el seu tipus). Ara els ajunta, i com que el zlib d'aquest projecte és d'una sola crida, els cossos es copien
  al bloc del cridador quan són més d'un (docs/30, docs/75).
- **El que hi ha al disc, el 15/11**: **sis PNG, tots a la mida del seu dibuix** (0.183.4) —`poweroff.png`
  **306 × 118** (3 383 B), `reboot.png` **249 × 123** (2 343 B), `sleep.png` **500 × 310** (79 032 B, l'os que dorm), `oops.png`
  **257 × 113** (2 632 B), `charging.png` **293 × 200** (2 317 B) i `lowbatt.png` **540 × 456** (9 719 B); abans eren
  quatre de 460 × 200 i dues pàgines de 540 × 960—, i els **tres dibuixos del boot**, `boot-2.bmp`, `boot-3.bmp` i
  `boot-5.bmp`, de **540 × 960** i vuit bits (519 478 B cada un), que són tres pàgines d'un wireframe i omplen el vidre
  sencer. **`lowbatt.png` no pot ser més estreta**: la línia `The battery is running out.` fa 548 px a la seva cara de
  36 i la pàgina del projecte vell en té 540, o sigui que la còpia la talla a les dues vores —així la dibuixava el
  PaperOS i així es va copiar— i el mínim d'aquell dibuix és l'amplada del vidre. **Les dues imatges del logotip ja no hi són**
  (0.178.5): `boot.png` era **una fotografia de la Lluna** (`as11-44-6667`, la lluna plena fotografiada des de l'Apollo
  11, obra del govern dels Estats Units i per tant de domini públic) amb el cel tret, i `boot-alt.png` era la mateixa
  lluna **en clipart**; totes dues viuen a la història del projecte (`git show 9dfa038:data/res/splash/boot.png`) i
  `tools/make_splash_images.py` ja no les escriu. **Cap moment del disc queda sense imatge** des de la 0.183.0 (`sleep`
  no en tenia i era el cas que una prova feia servir per comprovar que un moment sense imatge ho diu); aquest camí el
  prova `splash::show` amb un camí que no hi és.
- **Les dues pàgines són del projecte vell, copiades** (0.183.1, l'amo: *«per l'splash de batería agafa el del projecte
  antic PaperOS… fes una còpia exacta»* i *«fes el mateix amb charging»*): `lowbatt.png` és
  `drawPowerOffScreen(canvas, PowerOffNotice::BatteryLow)` del `PaperOS/src/ui/power_screen.cpp` —la insígnia de
  `drawBatteryLowMark`: la pila de quatre cel·les amb la de baix plena i el triangle d'avis amb la vora de paper i
  l'admiració— amb el tema d'aquell projecte (`padLarge` **18**, `gap` **10**) i les seves tres cares
  (`font_ui48b`/`ui36b`/`ui28b`, que són DejaVuSans-Bold a 48, 36 i 28 px amb 57/45, 43/34 i 33/26 d'alçada i ascens); i
  `charging.png` és `ChargingApp::arrange` i `::onDraw` del `PaperOS/src/apps/charging_app.cpp` —el cos de 40 % del
  costat curt, la paret del 7 %, el terminal del 10 % i el llamp de sis punts de `title_bar.cpp` (`fillBolt`)—.
  `tools/make_splash_from_paperos.py` és el port número per número, i **la imatge font que hi havia es queda a
  `tools/splash-sources/`** (la captura del PaperOS per a la de la pila; de la targeta de càrrega no n'hi ha cap, perquè
  el seu `ui_preview.cpp` no la dibuixa, i la font és el codi). La còpia es va comprovar contra la captura: la caixa de
  tinta és la mateixa (**251..707**) i la insígnia difereix en **67 píxels sobre 11 468** de tinta, que és l'antialiasing
  de la vora del triangle. **L'única cosa que no és una còpia és el nom del sistema**: la primera línia diu `lunokOS` i
  no `PaperOS`, perquè aquesta placa no és l'altra.

Des de la consola: **`splash`** (la llista de moments i la imatge que hi ha),
**`splash boot|poweroff|reboot|charging|sleep|lowbatt|oops`** (la del moment), **`splash /ruta/al/fitxer.png`** (una imatge
qualsevol) i **`splash hide`** (el vidre torna a paper). Des d'una app són les mateixes dues coses amb dos noms:
**`ui.splashes()`** (les paraules que aquest build té, en l'ordre del sistema) i **`ui.splash(word)`** (la imatge), i l'app
`splash` del disc és la pàgina que les ensenya totes (docs/30).
L'ordre en què s'encenen és el del sistema, i **el de l'arrencada és el primer moment possible** (0.115.0): a
`kernel::boot`, just després dels muntatges i dels ajustos (el dibuix és un fitxer i és el que la persona ha
triat) i **abans de la resta del maquinari** —les alarmes que han sobreviscut al tall, la ràdio, els sensors, la
barra—, o sigui que tot això es fa mentre el vidre ja diu lunokOS. És la mateixa crida que fa la comanda
`splash boot`, i una placa sense pantalla contesta `no glass` i segueix, que és l'únic lloc on pot dir-ho. **Quin
dels dibuixos surt no el tria ningú**: es tria sol, a cada engegada (0.178.5), i el que la persona pot triar és el
mot —`cfg boot boot`, l'únic que hi ha— que és el que queda desat a l'NVS (`cfg/boot`) i el que fa que un `splash`
digui **quin dibuix sortiria ara** (docs/30, docs/70).

**Els moments i el que els encén, des de la 0.183.0** (`power::boot_picture` i `power::going_down_picture`, que és on
viu la regla sencera):

| moment | quan | quin mot surt |
|---|---|---|
| `boot` | a `kernel::boot`, abans del maquinari | el dibuix de `cfg boot`, o **`lowbatt`** si la pila és sota el llindar i no hi ha cable |
| `sleep` | a `power::sleep`, abans que el xip pari els rellotges i **només en un son profund** (un de lleuger és una becaina i no pinta res) | `sleep`, o `lowbatt` si la pila és el motiu |
| `poweroff` | al comandament `poweroff`, abans que el rail baixi | `poweroff`, o `lowbatt` si la pila és el motiu |
| `reboot` | al comandament `reboot`, abans de reiniciar | `reboot` |
| `charging` | el moment del cable, que cap camí no encén sol encara | `charging` |
| `lowbatt` | a l'arrencada i a la baixada, quan la pila és sota el llindar | `lowbatt` |
| `oops` | quan el sistema no pot presentar la seva pròpia pàgina —l'app de casa no arrenca a l'arrencada— i el prompt és l'única porta que queda | `oops` |

- **El llindar és un número i una funció** (`power::kLowBatteryPercent`, **cinc per cent**): una pila que es llegeix, un
  cable que no hi és i una càrrega en el llindar o per sota. Una placa que no llegeix la pila no diu mai «buida», i una
  placa al cable no diu mai que s'ha acabat (docs/50).
- **Un pànic no es pot pintar**: el xip que pànica no està conduint el vidre, i un `gc16` des del gestor de pànic és una
  pàgina que no arriba. Per això `oops` és la cara d'un sistema que **ha decidit** que no pot seguir i no la d'un abort.
- **Una app que posa una imatge i torna** (`ui.splash`) deixa la barra del sistema deuda: la imatge renta el panell sencer
  i la franja de l'hora amb ell, i és la pàgina que torna la que la torna a pintar (`present_for_lua`, `bar::refresh`), no
  el moment de la imatge —una barra pintada mentre la imatge és al vidre és una franja del sistema sobre un dibuix.
**Una imatge d'una app** que la nomeni al seu manifest (`"splash": "res/logo.png"`) es pinta de la mateixa manera
just abans que arrenqui l'intèrpret, i la primera pàgina que l'app presenta se l'emporta (docs/30, docs/40).

### La pàgina es gira en software, i és vertical per defecte (1/11)

El panell viu en **960 × 540** (el marc del controlador) i **una app dibuixa en una pàgina de 540 × 960**:
vertical. El gir es fa **tot en software**, a dins del driver, i el controlador només veu el seu marc.

- **La imatge es guarda en el marc de la pàgina** (`It8951::image_`, 540 × 960), així que compondre-hi és
  una còpia i no una transformació.
- **El gir es paga només pel que s'envia**: a `flush`, cada fila del vidre es **recull** de la imatge
  (`gather_row`) girant píxel a píxel, i la comanda `LD_IMG_AREA`/`DPY_AREA` porta el rectangle ja en el
  marc del vidre (`logical_to_native`). Una pàgina sencera són 518 400 píxels; un quadrat de 40 × 40, 1 600.
- **Hi ha una sola crida i un sol valor**: `ui.rotation([degrees])` des de Lua, `ui::set_rotation` i
  `display::rotation()` al nucli. Tornar-la fa tres coses alhora: el driver torna a fer la imatge amb els
  costats canviats, el sistema torna a fer la pàgina de l'app (`on_resize` l'hi diu) i la barra es repinta
  sencera a dalt de la pàgina nova.
- **Els dits també giren**: `touch` fa el gir invers de la pàgina respecte de **270**, que és el gir en
  què el marc del xip i el de la pàgina són el mateix (docs/50, «Una sola orientació…»).

El número que el gir no pot inventar és **a quin gir el marc del xip i el de la pàgina coincideixen**
(`kChipPage` a `touch.cpp`): està mesurat a **270** tocant les quatre cantonades (docs/95, E-6). Els altres
tres girs en surten, i no queda cap número per endevinar.


### Què en fa el sistema, avui (1/11)

| Peça | Estat | Què falta |
|---|---|---|
| ESP32-D0WDQ6-V3, 240 MHz, 520 KB de SRAM | **fet**: consola, disc, NVS, xarxa, son | — |
| 16 MB de flash | **fet**: 2 108 973 B (50,3 %, mesurat amb la 0.13.0 l'1/11) i 11,9 MB de disc | — |
| **8 MB de PSRAM** (4 MB mapejats) | **fet de codi i mesurat a la placa** (31/10): `extra_memory()` a la plataforma torna els tres números (xip, adreçable, lliure) i `pal::IExtraMemory` assigna a la PSRAM; l'intèrpret hi posa els blocs de 4 KB en amunt i el sostre de les apps és la PSRAM **mapejada** (4 096 KB). `mem` diu `extra memory 4067 of 4095 KB usable, of 8192 KB on the chip (psram)` i `sys.heap()` en dona els bytes. Els altres 4 MB necessiten el `himem` amb bancs i **no poden ser transparents** (docs/90) | comprovar que una app d'1 MB hi cap (docs/95, E-4) |
| **Panell e-ink ED047TC1/ET047TC1 + IT8951E** (SPI 15/14/12/13, BUSY 27, alimentació GPIO23) | **fet de codi i amb crida de Lua** (1/11): `ISurface` al HAL (una superfície de 4 bits amb el rectangle brut i un `flush` que no espera), el driver a `esp32/it8951.h` (trames de 16 bits, `GetDeviceInfo`, el bus de la targeta a **10 MHz** —la del fabricant— fins que el handshake es vegi al vidre, amb un sostre de 26,7 MHz per la matriu de GPIO, mesurat l'1/11—, els vuit modes) i, des de la 0.14.0, res de Lua que arribi al vidre directament: una app dibuixa a la seva vista (`ui.*`) i `ui.present` és el que compon. La capacitat **s'anuncia** quan el controlador contesta. **El sistema només pinta la barra**, les 48 files de dalt; la pàgina és sempre d'una app | **fet** (1/11, 0.13.0): la imatge arriba i surt recta; falten el sentit dels nivells i el temps d'un `gc16` (docs/95, E-7) i la política d'encesa de GPIO23. El **text** ja hi és per a la barra (una font de 5 × 7, 0.16.0) i no pas per a una app |
| **GT911**, tàctil de dos dits amb gestos (I2C 0x14 o 0x5D, INT GPIO36) | **fet de codi i amb crida de Lua** (1/11): `ITouch` al HAL, el driver a `esp32/gt911.h` (les dues adreces, la configuració de 186 bytes, el marc de cinc punts i el zero que el tanca), `touch.caps`/`read`/`gesture` a Lua i una app que llegeixi el panell; el sistema en fa `tap`, `double`, `triple`, `long`, `drag` (amb un dit o dos), `two` i `pinch`. La capacitat **s'anuncia** quan el xip contesta (vegeu «El tàctil», a sota) | la **velocitat de mostra** (contar frames amb el dit, docs/95 E-6) i la política d'energia del pas 8; el marc ja està mesurat amb quatre tocs, un a cada cantonada (1/11) |
| **SHT30** (I2C 0x44) | **fet i provat al xip** (1/11): `ISensor` al HAL (`read(celsius, humidity)`), el driver a `esp32/sht30.h` —la comanda, els dos CRCs i les dues fórmules del full de dades— i `env.read()` des de Lua. La capacitat s'anuncia **només si una mesura sencera arriba amb els dos CRCs bons**. El primer enviament a la placa (1/11, 0.10.0) va dir `env no` amb `i2c: read of 44 failed (ESP_ERR_TIMEOUT)`: **el bus només deixava 100 µs de clock stretching** i el xip n'aguanta sis mil·lisegons (vegeu «El bus ha de deixar…», a sota); amb la 0.10.1 la placa diu **`env yes`** i `env.read()` dona `21.1 C 63.5 %` (docs/95, E-5) | que la temperatura **quadri amb un termòmetre** de la mateixa sala (docs/95, E-5) |
| **FM24C02**, 256 B (I2C 0x50) | **fet** (31/10): `esp32/fm24c02.h`, un `IStorage` que **és el xip** —llegeix i escriu per la I2C, sense còpia a RAM— muntat a `/fram`, i l'emulador el té com a `fram.bin` (`Model::fram`) | provar-ho al xip: que contesti a 0x50 i que els bytes hi siguin amb la bateria treta (docs/95) |
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

### El vidre deixa de contestar: qui li talla el corrent és el driver (8/11, mesurat)

El lector d'EPUB va deixar la Paper **amb el vidre mort**: no pintava res de res i, a partir d'aquell moment, cap altra
app no podia pintar fins a reiniciar («després del reader ningú pot tocar la pantalla fins al reinici»). El mateix amb
el *splash*.

La segona tirada de la placa, amb el guard que convertia la `du` en `gc16` ja posat, va ensenyar que **la qualitat no
hi tenia res a veure**: el log diu `a a2 of 0,0 540x960 is the whole glass, and it goes up as a gc16`, la pintada surt
com a `mode 2`… i el panell mor igual, a la pintada següent:

```
[I][UI] paint 0,0 540x960 mode 7
[D][UI] a a2 of 0,0 540x960 is the whole glass, and it goes up as a gc16
[I][PANEL] paint 0,0 960x540 mode 2 at 0x1236e0 (page 0,0 540x960, 540 rows of 480 B)
[I][UI] paint 0,0 540x960 mode 2
[W][UI] the glass refused a page (0,0 540x960, gc16): the panel stopped answering
```

**I des de la 0.158.0 la línia diu el nom de la qualitat i no només el número** (8/11, demanat per l'amo):
aquella tirada es va haver de llegir amb `mode 2` i la capçalera al costat, i això és una cosa que el log no ha
de fer-te fer. Ara `hal::mode_name` (`lib/hal/include/hal/hal.h`) és l'única taula dels noms i la fan servir el
panell, la UI i l'emulador, i les tres línies es llegeixen soles:

```
[I][UI] paint 0,0 540x960 mode 3 gl16
[I][PANEL] paint 0,0 960x540 mode 3 gl16 at 0x1236e0 (page 0,0 540x960, 540 rows of 480 B)
[W][UI] the glass refused a page (12,486 516x120, du): the previous paint did not finish
```

El número s'hi queda perquè és el que viatja a la comanda del controlador (`DPY_BUF_AREA`, vegeu més avall) i
el nom és el que es llegeix.

El que va matar el vidre és **l'ordre de les coses dins de `flush()`**, i és un error meu en tres actes:

1. `flush()` **llegia `LUTAFSR`, l'esborrava i el tornava a llegir abans d'esperar** que la pintada anterior acabés.
   O sigui que els tres primers accessos SPI d'una pintada queia **mentre la pintada anterior encara movia tinta**.
2. Un controlador que està corrent `DPY_AREA` no contesta pel MISO, i aquelles lectures tornen `ffff`.
3. El driver llegia `ffff` com «el panell ha deixat de contestar» i feia el que fa amb un panell sord: **li tallava el
   corrent**. Amb la seva pròpia forma d'ona a mig fer. D'aquí no va tornar: la resta de lectures són `ffff`, els tres
   intents de `kReviveTries` s'exhaureixen i `dead_` deixa el vidre sense pintar per a tothom.

Per això passava amb el lector i amb el *splash* i amb cap altra app: són els dos únics que pinten **pàgines senceres
seguides**, i una pàgina sencera és l'única cosa que deixa el motor engegat prou temps perquè la pintada següent el
trobi treballant.

El que hi ha ara, i tot és al driver (`it8951.cpp`):

- **`wait_paint()` va el primer**, abans de tocar el controlador: fa servir la línia BUSY i el temps del mode i **cap
  SPI**.
- **Un `ffff` no compra un cicle de rail.** Abans de tallar el corrent es pregunta dues vegades més, amb 250 ms entremig
  (`kSilentTries`, `kSilentPauseMs`): un controlador que només està enfeinat torna a contestar i la placa no es mata.
- **El temps d'espera es mesura, no se suposa.** `wait_paint()` mesura quants mil·lisegons ha estat BUSY avall el panell
  després de `DPY_AREA`, i si la pintada cobria **mitja pàgina o més** aquell número passa a ser el mínim de la
  següent —només pot créixer, i un cursor que parpelleja no el toca, perquè **només s'aplica a les pintades que estan
  esperant una pàgina** (0.151.4: la primera versió el va aplicar a tot i el sistema va quedar a un quadre per segon)—.
  Es diu al log: `the panel held busy %u ms for a page (the table says %u)`.
  **Mesurat el 8/11 amb la 0.151.3: una pàgina sencera té la tinta 1051 ms**, no els 450 de la fila del fabricant.
- **`arm()` no creu un `ffff`** (0.151.2): una informació de dispositiu tota a `ffff`, una geometria més gran que
  `kMaxGlassSide` (4 096) o una imatge a `0xffffffff` són «no hi ha panell», i `revive()` torna fals en comptes de
  fer veure que l'ha recuperat.

### El driver del fabricant mana: `LUTAFSR == 0`, i `LD_IMG_END` (8/11, 0.151.6)

El `M5EPD` oficial és al disc (`OLD/PaperOS/.pio/libdeps/m5stack-paper/M5EPD`) i la seva seqüència d'una pintada és
aquesta, sense res més, **i en aquest ordre**:

1. `SetTargetMemoryAddr`: `LIS_HIGH` i després `LIS_LOW`;
2. `LD_IMG_AREA` (0x0021) amb cinc paraules: format, x, y, w, h;
3. els píxels, en paraules de 32 bits amb el CS baix;
4. **`LD_IMG_END` (0x0022)**, que tanca la càrrega (`WritePartGram4bpp`);
5. **`CheckAFSR()`**: llegeix `LUTAFSR` i espera que valgui zero, tres segons com a molt (`M5EPD_Driver.cpp:308`);
6. `DPY_BUF_AREA` (0x0037) amb set paraules: x, y, w, h, mode i l'adreça de la imatge (baixa, alta).

**La posició del `CheckAFSR` és el detall que faltava** (0.151.7): va **després de la càrrega i just abans de la comanda
de pintar**, no al començament. Un `LUTAFSR` llegit un mil·lisegon després d'un `DPY_AREA` val **zero**, perquè el motor
encara no s'ha marcat com a ocupat —ho diu el log de la placa: `engine status 0000 after the paint`—, i una espera que es
pregunta al començament de la pintada no espera res. La càrrega d'una pàgina és de dos-cents mil·lisegons d'SPI, i al
final ja diu què és.

El meu driver feia **1 amb el pin BUSY i una taula de mil·lisegons** en comptes del registre. La placa va dir com
d'equivocat era: el pin deia que una pàgina trigava 1052 ms, després 3000 (un temps esgotat), després 2613 — tres
números per a la mateixa pàgina. **El pin BUSY puja abans que el motor hagi acabat**, i per això el `LD_IMG_AREA` següent
queia a sobre d'un motor que encara llegia la imatge: així és com aquest controlador deixa de contestar.

I dues coses més que eren dolentes pel mateix motiu:

- el driver **escrivia un 1 a `LUTAFSR` abans de cada pintada** (0.118.5) perquè «la bandera no baixava mai»: no baixava
  perquè el motor no acabava mai. El fabricant només la llegeix, i ara aquí també;
- `read_register()` tornava **zero** quan la comanda no sortia, i zero és exactament el que llegeix «el motor està aturat».
  Ara torna `ffff`, que és la frase que la resta del driver entén.

Tot el que ja coincidia amb el fabricant, paraula per paraula: els prefixos (0x6000 comanda, 0x0000 dada, 0x1000
lectura), els codis (0x0021, 0x0022, 0x0034, 0x1224), el format de píxel (`B_ENDIAN`, 4 bpp, rotació 0), el VCOM (0x0039,
0x0001, 2300), el registre d'escriptura empaquetada, l'adreça de la imatge (0x001236E0), l'SPI a 10 MHz en mode 0 i el
BUSY actiu baix.

### Una lectura que falla no és un panell que call (8/11, 0.151.8)

`read_register()` tornava un número passés el que passés, i quan aquell número es va fer `ffff` (0.151.6) una **lectura
fallida** es va tornar indistingible d'un **panell que no contesta** — i la resposta d'aquest driver a un panell que no
contesta és **tallar-li el corrent**. El lector pinta el vidre sencer dues vegades en arrencar; la segona pintada demana
el registre un mil·lisegon després que la primera hagi sortit, el bus encara està ocupat per la càrrega de la segona
pàgina, la lectura falla, i el corrent es talla a un panell que estava bé.

Ara:

- **`read_register(reg, value)` diu si ha llegit**. Una lectura fallida és un reintent de l'espera i **mai** un cicle de
  rail. Només una lectura amb èxit que valgui `ffff` és silenci, i el silenci vol **tres** lectures així;
- **l'espera té un terra, i el terra d'una pàgina és el temps mesurat d'una pàgina**: la placa va dir **1051 i 1052 ms**
  per a una pàgina sencera, dues vegades. El driver comença amb 1050 ms i només el pot fer créixer amb el que el panell
  diu de si mateix (`a page held the engine %u ms`). Una pàgina que arriba un mil·lisegon després d'una altra **no pot**
  carregar la imatge dins d'un motor que encara llegeix l'anterior;
- **l'espera es pregunta dues vegades**: abans de carregar la imatge i abans de `DPY_BUF_AREA` (el lloc del fabricant),
  i `idle()` pregunta amb el mateix terra.

### El disc es llegeix amb el vidre quiet (8/11, 0.151.9)

Els set logs d'aquesta tarda tenen tots el mateix moment de la mort: **la pintada que falla és sempre la primera que ve
després que l'app hagi llegit el disc**. Les cares de `/fonts` (flash) i el camí dels llibres (`epub.books`, flash **i
targeta**) es llegeixen mentre la rentada de tota la pàgina encara té la forma d'ona en marxa —més d'un segon—, i la
pintada següent troba el controlador en `ffff`. A l'arrencada, en canvi, entre el splash i la barra no hi ha cap lectura,
i tot va bé.

Per això el lector ara fa la feina lenta **abans** de demanar el vidre sencer: les dues cares, la llista de llibres, i
aleshores `ui.full_screen(true)` i la pàgina. L'`on_resize` que arriba després no torna a tocar el disc (`shelf_read`).

Això és la hipòtesi amb més evidència que tenim i **no està provada**: si el vidre aguanta amb el lector endreçat, la
causa és aquesta i la regla ha de passar al nucli (cap lectura de disc mentre `display::idle()` sigui fals després d'una
pintada gran); si no aguanta, el següent pas és l'SPI: qui té el bus i el CS entre `give_bus()` i la lectura següent.

### El que encara no se sap (8/11, i és el que toca mesurar)

En tres tirades seguides el vidre mor **entre les dues pintades de l'arrencada d'una app**: la rentada de tota la pàgina
puja (el panell l'accepta), l'app arrenca, i la pintada següent troba `ffff` **abans d'enviar res**. Entre aquestes dues
pintades el lector fa exactament dues coses pesades: camina els llibres (`epub.books`, que passa per la **flash i la
targeta**) i carrega **dues cares** de `/fonts`. Cap pintada nostra s'està movent en aquell moment, i tot i així el
controlador deixa de contestar: o es reinicia, o perd l'alimentació.

Els candidats, per ordre: **la targeta SD, que comparteix el bus SPI amb el panell** (i que es llegeix just en aquell
moment), i les dues cares. L'experiment que ho separa és de dos minuts i no necessita res més que la placa:

1. `splash boot` — una pàgina sencera i cap app: ha de pujar.
2. `ls /epub` — camina la flash i la targeta i no pinta res.
3. `splash boot` una altra vegada — si ara el vidre no contesta, la culpa és de la targeta; si puja, no ho és.
4. I `run reader` un cop **amb la targeta fora** del slot (`/cfg` cau a la flash i el lector arrenca igual).

Mentrestant, el que sí que està arreglat (0.151.5) és que un vidre mort **no costa res**: `flush()` el refusa abans
d'esperar, `engine_over()` diu que no hi ha tinta en moviment, i la frase d'una pàgina refusada es diu **una vegada
mentre no canviï el motiu** — abans, cada volta del sistema pagava tres segons i escopia la mateixa advertència.

I una regla que es queda encara que no fos la culpable: **una `du` no cobreix tota la pàgina**. La forma d'ona de dos
nivells és *parcial* i una pàgina sencera és una rentada; el nucli converteix en `gc16` qualsevol `du`, `du4` o `a2`
que cobreixi set vuitens del vidre o més (`ui.cpp`, `paint()`), i el lector demana `gc16` a la seva rentada de
prestatge. El llindar és generós a posta, perquè un bloc de text que omple la vista i el rellotge de la barra
segueixin sent `du`.

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
  una alarma pendent i si ningú no fa servir la placa —la quietud que tria la persona a *Sleep
  after*, cap app oberta i cap `keep_awake`— i, si l'espera és de **vint segons o més**, s'apaga
  fins a aquella alarma. Amb menys de vint segons es queda despert: un despert d'una apagada és un
  arrencada, i la baixada sola ja en gasta uns quants. `power idle off` ho desactiva mentre una
  persona treballa al prompt. **Sense cap alarma a la cua baixa igualment, fins a la tecla** (0.190.12).
- **Qui va despertar-ho**: després d'una apagada, un despert per alarma i una premuda del botó `PWR`
  són el mateix arrencada. L'únic que els distingeix és el motiu del despert del xip, que es llegeix
  **abans** de desarmar-lo (`RtcClock::woke_by_alarm`), i des de la 0.190.19 la primera línia del
  registre el diu amb el que vol dir: `back from a deep sleep, woken by the clock chip: an alarm,
  nobody in front, the glass is not touched` o `… woken by the key of the dial: a person, the page
  comes back` (docs/60, docs/90).

El que encara no està provat a la placa: que el corrent caigui de debò i que l'alarma el torni a
pujar. És la primera cosa de la llista de mesures de `docs/90`.

### `poweroff`: apagar sense tornar

`sleep deep` apaga la placa **perquè torni**; `poweroff` l'apaga i no arma res. La seqüència no és
només baixar GPIO2, i és la que fa servir el firmware de fàbrica del fabricant
(`M5Paper_FactoryTest`, `Shutdown()` a `global_setting.cpp`):

```
fflush  →  espera 600 ms  →  REPÒS DEL PANELL  →  GPIO23 = 0  →  GPIO5 = 0  →  GPIO2 = 0  →  esp_deep_sleep_start()
```

Les dues primeres són **l'alimentació del panell** i la dels **ports HY2.0**; l'última és el xip
posat a dormir, perquè amb el cable posat el corrent no cau i la placa ha de callar igualment. La
`M5EPD::shutdown()` de la llibreria, la que no té hora, només fa `disableMainPower()`.

**El repòs del panell és el pas que faltava** (0.183.0, i és el primer de la `Shutdown()` del
fabricant): `M5.EPD.Sleep()` escriu **`IT8951_TCON_SLEEP`** (la comanda `0x0003`, sense arguments) al
controlador abans que el seu rail baixi — un controlador despert sobre una alimentació que se'n va és
un controlador demanant el que ja no té, i la imatge del vidre s'hi queda perquè és pigment i no
memòria. És `hal::ISurface::rest()`: la placa la té a `It8951::rest`, l'emulador diu la línia i **es
nega a pintar-hi després** (`the panel has gone to sleep`), i la placa la crida des de
`PaperPower::power_off()` amb el vidre que li passa `PaperPlatform::display()`. **El son profund no
la fa servir**: baixa l'alimentació principal i el panell mor amb ella.

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

**Què està provat i què no**: `pio run -e m5-paper` **compila net** (zero avisos propis, docs/75) i el
binari que en surt porta la identitat del Paper i cap de les de l'ATOM. **S'ha flashejat i arrenca**
(31/10 amb la 0.6.0 i l'1/11 amb la 0.10.1): el banner diu `lunokOS <versió>  board m5stack-paper`, el
prompt respon, `caps` dona **15 de 18** amb bateria i targeta dins (12 de 18 amb la 0.6.0, que encara
no tenia ni el sensor ni el `led`), `mounts` llista `/sd` amb **15 193 MB**, `ls sd` el llegeix, `cfg` diu la
targeta, `env.read()` dona temperatura i humitat, i **la ràdio funciona**: l'`INet` d'aquesta placa està
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
| Nom (`sys.board`) | El model que se li ha dit que sigui: `m5stack-atom` (per defecte), `m5stack-atom-matrix`, `m5stack-paper` o `ttgo-t-koala`. Mai `emulator`, que no és cap placa (docs/85) |
| Plataforma | POSIX (Linux) |
| RAM | La del PC, però **limitada al mateix sostre que la placa** |
| Fitxers | Un directori del disc (`emulator/`) amb `littlefs/` i `nvs/`, i una targeta amb `--sd` |
| Rellotge | El del PC, o virtual (`--time-scale`, `--now`) |
| Consola | stdin/stdout en mode cru |
| Xarxa, energia i sensor | **De mentida i ho diuen**, tret de la pàgina, que la baixa `curl` de la xarxa d'aquesta màquina (docs/85) |
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
