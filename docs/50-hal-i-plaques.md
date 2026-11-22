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
| el pont de Lua | totes les taules | **les mateixes taules, i contesten** (0.190.183): `ui.label(...)` torna `nil, {code = "E_UNSUPPORTED", message = "this board has no display"}` en lloc de no existir, perquè una API que desapareix diu una mentida sobre el sistema (l'amo: *«L'API hauría ser igual per tots, si no quina gracia té?»*) |
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
que aquestes quatre plaques no poden ensenyar (docs/75). **I des de la 0.190.183 el pont de Lua no s'hi estalvia**: 79 KB de codi i
840 B de RAM a l'ATOM tornen al build perquè les taules hi siguin i contestin, o sigui que l'estalvi de debò és la pàgina i no la seva API
(docs/30).

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

## El Bluetooth, que és un teclat (0.190.179)

L'amo: *«afegeix bluetooth al projecte, anem a fer un experiment»*, i tot seguit *«Segur que no te nimble?»*. La ràdio d'aquest xip pot
fer una sola cosa que aquest sistema necessiti —**ser un teclat** per a un mòbil o un ordinador—, i no és una xarxa: no hi ha res a
connectar, hi ha caràcters a escriure. Per això no viu a `net` sinó a `hal::IBluetooth`, i la política viu a `kernel::bt`.

- **NimBLE i no Bluedroid**. La pila del fabricant costa **684 KB de flash i uns 50-70 KB de heap** (mesurat el 31/10, docs/75) i en un
  xip sense PSRAM no hi són —el Wi-Fi ja no arrenca amb 43 KB lliures—. Amb NimBLE el binari de l'ATOM passa d'**1 516 693 B a
  1 759 081 B** (+233 KB) i la RAM estàtica de **71 124 B a 88 456 B**, o sigui que la ranura de 2,5 MB encara té 865 KB per créixer.
  La ràdio s'inicialitza en **BLE-only** (`CONFIG_BTDM_CTRL_MODE_BLE_ONLY`), que és l'única cosa que un teclat necessita i estalvia la
  meitat Classic del controlador.
- **El perfil HID s'escriu aquí** perquè `esp_hid` d'aquesta IDF només té transport per a Bluedroid: el seu `CMakeLists.txt` enllaça
  `ble_hidd.c` sota `CONFIG_BT_BLUEDROID_ENABLED`. A `lib/hal_esp32/src/bluetooth.cpp` hi ha el servei **0x1812** amb el report map de
  sis tecles, el report d'entrada (0x2A4D), el protocol mode, el boot report i el descriptor de report; a l'anunci hi van el nom, el
  servei i **l'aparença de teclat (0x03C1)**, que és el que fa que un mòbil ensenyi un teclat i no un «dispositiu desconegut».
- **L'aparellament és Just Works amb bonding**: un teclat no té pantalla per ensenyar un número, i el vincle es desa a la NVS perquè
  duri més enllà d'una apagada.
- **El ràdio mana sobre la son**. Un son lleuger atura els rellotges que viu el link, o sigui que `wants_the_chip_awake()` és una de les
  portes de `power::open_the_sleep`: mentre el teclat és engegat el torn espera despert com abans de la 0.190.63, i el `bt on` ho diu.
- **Les bandes que no en porten no paguen res**: tot el ràdio viu sota `CONFIG_BT_ENABLED` i la resta del fitxer són respostes que diuen
  que no, o sigui que el devkit i el T-Koala compilen igual i el prompt els diu `this board has no radio` (docs/30). Les
  que en porten són **tres**: l'**ATOM** (i el Matrix, que és el mateix firmware), la **T5S3 Pro** i la **Paper**. Les dues últimes
  no heretaven res del `sdkconfig.defaults` de l'ATOM i el `CONFIG_BT_ENABLED` no hi era enlloc: el T5S3 el va tenir a la 0.190.198 i
  la Paper a la 0.190.201 (docs/85).

### I l'altra direcció: el Bluetooth amfitrió (0.190.188)

L'amo: *«Ja tenim bluetooth HID device, fem el Bluetooth HID Host»*. La mateixa ràdio, amb la placa **amfitriona** i el teclat d'algú
altre. `esp_hid` té host, i és el de Bluedroid, que en aquest xip no hi cap; o sigui que el camí és el mateix que al device: **GATT a
mà**, ara com a client.

- **El rol ja hi era**: `CONFIG_BT_NIMBLE_ROLE_CENTRAL=y` és el default de NimBLE i ja viatjava al binari, o sigui que el GATT client i
  el link central no costen ni una línia de `sdkconfig`. El que s'ha escrit és el client: explorar (`ble_gap_disc`), connectar
  (`ble_gap_connect`), descobrir el servei **0x1812**, descobrir-ne les característiques i els descriptors, i subscriure's al report.
- **El boot protocol és el que estalvia el parser**: s'escriu `0x00` al **Protocol Mode (0x2A4E)** i el teclat envia el report de
  **vuit bytes** de tota la vida —modificadors, un byte de reserva i sis Usage IDs— en lloc del report que descriu el seu report map. Un
  teclat que no l'accepta ho diu al registre i els seus reports es llegeixen igual, que és el que passa quan el report és el mateix de
  vuit bytes.
- **La lligadura, si el teclat la vol**: la subscripció es refusa amb `Insufficient Authentication` i el que es fa és
  `ble_gap_security_initiate` i esperar l'event `ENC_CHANGE`, que és qui torna a demanar la subscripció. No es reintenta en bucle.
- **Les tecles van pel camí del cable**: la ràdio dona els vuit bytes i prou; qui en fa text és `kernel::bt`, que tradueix Usage ID a
  caràcter amb **la mateixa taula del device girada** (`key_of`, sense una segona taula que es desdigui) i crida **`shell::feed`**. O
  sigui que el prompt, les apps, l'editor de línia, les fletxes, `Ctrl+D` i `Escape` són exactament els del port sèrie, i el teclat de
  l'aire no inventa un segon dialecte de tecles.
- **El que no fa**: no es reconnecta sol. L'amo ho va triar així (*«la persona tria»*): `bt scan` escolta tres segons i en fa la llista,
  `bt pair <n>` es queda amb un, i `bt forget` esborra el vincle de les dues bandes. Un prompt que es lliga sol a un aparell que passava
  per allà és un prompt que no pregunta.
- **I el pany del son també mana per aquest costat**: `wants_the_chip_awake` és cert mentre hi ha un teclat lligat encara que el teclat
  virtual d'aquesta placa estigui apagat, perquè un link que dorm no és un link.
- **Quant costa**: **+8 192 B** de flash a l'ATOM (de 1 852 848 a 1 861 040 B, o sigui del 70,7 % al 71,0 % de la ranura). El heap d'un
  link BLE és d'un parell de kilobytes i es paga quan hi ha un teclat, no a l'arrencada.
- **I el que no s'ha pogut provar aquí**: la placa de debò. Des de la 0.190.200 l'emulador **sí que té ràdio** —una de mentida, amb una taula
  d'aparells i un teclat de l'altre cap que escriu pel camí de debò— quan el model la porta al seu build, o sigui que `bt scan` i `bt pair`
  es poden mirar al PC; el que segueix demanant un teclat a la mà és el maquinari: la pila de NimBLE, la lligadura amb un mòbil i el que aquell mòbil
  en fa de veritat (docs/85).

#### Els dos costats tenen nom (0.190.192)

L'amo: *«si poso bt on només veig el teclat … Fem-ho bé»*. La ràdio té dos extrems que es deien amb una sola paraula, i ara es diuen per
seu:

```
bt                       l'estat de les dues meitats, en tres o quatre línies
bt dev [on|off]          aquesta placa com a dispositiu
bt dev keyboard on|off   i el teclat tot sol
bt dev mouse on|off      i un ratolí propi (encara no hi és: ho diu)
bt host [on|off]         aquesta placa com a amfitriona, sense oferir-se a ningú
bt host scan [segons]    qui hi ha a l'aire
bt host pair <n>         quedar-se amb un
bt host reconnect        tornar al que ja s'hi havia lligat
bt host forget           esborrar els vincles
bt type <text>           escriure a l'aparell de l'altre cap
```

**`bt` diu les dues meitats**: `this board is: a keyboard, offered` i `on the other side: a keyboard writing into this console` (o
`free`).

**I les paraules curtes es van haver de desdir** (0.190.194, l'amo: *«bt off ho apaga tot, bt on no encén ni dev ni host, que s'han
d'activar a ma cadascun: bt dev on. Si es posen a off tant dev com host, bt ha de ser off»*): un `bt on` que ho encenia tot era el mateix
embolic que el `bt on` vell, o sigui que **`bt on` no encén res** —di les dues portes que hi ha— i **`bt off` apaga el dispositiu i
l'amfitrió**. I **`bt` diu `off` només quan ho són tots dos**: `on, a keyboard`, `on, a host`, `on, a keyboard and a host`, amb el que
estigui connectat afegit a la frase. Per saber-ho calia una cosa que el HAL no tenia —**si el costat amfitrió s'ha demanat**, perquè la
ràdio engegada pel dispositiu no vol dir que algú escolti— i és `host_is_up`: cert a `host_on`, a explorar, a aparellar i a tornar-hi, i
fals a `host_off`.

**`bt host off` baixa la ràdio només si el dispositiu també és fora**: les dues meitats comparteixen la pila de NimBLE i matar-la per una
banda seria tancar l'altra.

### I la placa també pot ser un ratolí (0.190.193)

L'amo: *«opció B»*. Dues col·leccions en un sol servei HID voldrien **report ids** i renunciar al **boot protocol**, que és el que fa que
aquest teclat escrigui a qualsevol lloc; o sigui que **la placa és un teclat o un ratolí, i es tria**:

```
bt dev mouse          què és ara
bt dev mouse on       un ratolí, ofert com a "lunokOS"
bt dev mouse off      un teclat una altra vegada
bt dev mouse move 10 -5   moure el cursor
bt dev mouse click [left|right|middle]
```

- **El servei GATT no es toca**: el que canvia és el **report map** que el host llegeix (`0x2A4B` es llegeix de nou a cada petició, o sigui
  que el canvi és en calent), el **report** que s'envia —vuit bytes de teclat o quatre de ratolí— i l'**aparença de l'anunci** (`0x03C1`
  teclat, `0x03C2` ratolí), que obliga a tornar-se a anunciar.
- **Cal tornar a aparellar des de l'altre cap**: el mòbil va llegir el mapa d'un teclat i no endevina el d'un ratolí. La comanda ho diu.
- **El moviment és relatiu i el report de repòs va darrere del de moviment**, o sigui que el cursor es queda quiet quan el dit s'atura; i
  el report d'un ratolí porta de **−127 a 127**, o sigui que un moviment més gran es fa en més d'un report i ho parteix el sistema
  (`bt::move_for_a_page`) en lloc de refusar-lo.
- **El clic és pressió, trenta mil·lisegons i deixar anar**, la mateixa forma que una tecla: un botó premut per sempre és el que l'altre cap
  veu si no es deixa anar.

#### Les dues coses alhora: el mode `both` (0.190.204)

L'amo: *«fem una app lua, una app per aixicar un bluetooth HID device amb un teclat i un mouse… la pantalla buida serà el mouse i es
trasmetrà amb el dit. El teclat gracies a un Textbox»*. Un teclat de boot i un ratolí de boot en un sol vincle no existeixen: dues
col·leccions en un mapa volen **report IDs**, i un aparell amb IDs no pot servir el boot protocol. O sigui que el que la 0.190.193 va
triar com a `opció B` es queda com és i se n'hi afegeix un tercer, amb el preu escrit a la cara:

| Mode | Mapa | Report | Boot protocol |
|---|---|---|---|
| `keyboard` (de sèrie) | el del teclat, sense IDs | 8 bytes per la característica del teclat | **sí** |
| `mouse` | el del ratolí, sense IDs | **4 bytes** (botons, x, y i la roda) per la del ratolí | sí |
| **`both`** | el del teclat **i** el del ratolí, amb `Report ID 1` i `2` | 9 i **5** bytes, l'ID al davant | **no** |

- **El servei GATT és el mateix en els tres**: info, mapa, **dues** característiques de report d'entrada (teclat i ratolí, cadascuna amb
  el seu handle i la seva subscripció), la del boot, la de sortida dels llums, la de sortida del report, el control point i el protocol
  mode. El que canvia és el mapa que es llegeix (`0x2A4B`, a cada petició), l'**ID que diu el descriptor de referència** (`0x2908`: 0
  als dos modes d'un sol report, 1 i 2 al compost) i el report que s'envia. Engegar i apagar una característica a cada canvi de mode
  voldria tornar a registrar el servei, i el servei és el mateix.
- **Els dos UUIDs repetits es distingeixen per l'adreça**: `hid_access` i `report_access` reben l'`arg` i no el valor, o sigui que hi ha
  dos `ble_uuid16_t` de `0x2A4D` i dos de `0x2908` i la comparació és de punter.
- **Els llums del teclat** (`0x2A32` i el report de sortida) arriben amb l'ID al davant al mode compost, i qui els llegeix salta el
  primer byte.
- **L'aparença de l'anunci** és el teclat (0x03C1) al mode compost: el Bluetooth no té cap paraula per a «les dues coses», i el que
  aquesta placa fa sobretot és escriure.
- **I l'altre aparell ha de tornar a aparellar-se un cop**: va llegir el mapa d'un teclat i no endevina un altre mapa. La comanda ho diu
  i la pàgina també.

**I per què un ratolí sol no movia res** (0.190.208, l'amo: *«connecta, pero encara no envia bé sembla»*). En un mode d'un sol report el mapa
declara **un** report i cap ID, o sigui que les dues característiques d'entrada porten la **mateixa** Report Reference (ID zero, tipus entrada)
i descriuen els mateixos bytes: a quina de les dues es subscriu l'amfitrió és cosa seva. El codi enviava el ratolí sempre a la segona i el
teclat sempre a la primera, i un aparell subscrit a l'altra no rebia res mentre la placa deia «connectat». Ara `send_report_to_every_char`
empenta el report **a totes dues** quan el mode és d'un sol report (la que ningú ha demanat es descarta), i el mode compost continua enviant
cada report a la seva, perquè allà l'ID del davant és el que diu de qui és.

**I connectat no és escoltat**: el CCCD de cada característica d'entrada és el que decideix si una notificació arriba, i fins ara no es mirava.
`BLE_GAP_EVENT_SUBSCRIBE` es llegeix i es diu al registre —*«the other side asked for the mouse report»*—, i `IBluetooth::reports_enabled()`
(amb `bt::reports_enabled()` i **`bt.ready()`** a Lua) diu si l'altre aparell ja hi sent. L'entremig és el que una persona veu com «connecta
però no envia».

**I la tria es desa**: `cfg.btmode keyboard|mouse|both` viu a la botiga perquè la sessió d'una app només arriba fins al son, i `bt::start`
aplica el mode desat quan la ràdio arrenca —una placa que es va deixar com a ratolí torna com a ratolí (docs/30)—.

**I per què l'enllaç queia tot sol** (0.190.211, l'amo: *«a device is connected» → «the device went away»*, una vegada i una altra, *«No es
realitza la conexio»*). Tres coses, totes tres de protocol:

| Què passava | Per què fa caure un enllaç | Què s'ha fet |
|---|---|---|
| La lectura del report tornava **sempre** la mida del mode compost (9 i 4) | a `keyboard` el mapa en declara vuit i a `mouse` tres; un amfitrió que compara el valor amb el mapa veu una mida que no és la de cap report | `report_access` contesta la mida del mode d'ara |
| `send_report` enviava a **tot arreu**, i `ble_gatts_notify_custom` **no mira el CCCD** | una notificació en una característica que l'altre cap no ha encès és una violació del protocol, i alguns amfitrions la contesten tancant | `send_report` mira `g_report_subscribed`/`g_mouse_subscribed`, que ja es llegien des de la 0.190.208 |
| Canviar de mode deixava el **vincle vell** | l'altre cap va llegir un altre mapa i en reconnectar encreua les claus (MIC) | `device_be` esborra els vincles i **tanca l'enllaç** quan el mode canvia |

I el que faltava per poder-ho veure: **el motiu de la desconnexió**. `BLE_GAP_EVENT_DISCONNECT` porta un codi (`0x08` temps esgotat, `0x13`
l'altre cap l'ha tancat, `0x05`/`0x3D`/`0x3E` autenticació i MIC, `0x16` l'altre cap per un motiu seu) i es llençava; ara surt al registre amb
els paràmetres de la connexió (interval, latència, supervisor) i amb **cada accés** al servei a nivell de feina. I les claus que es
reparteixen porten la identitat (`ENC | ID`), que és el que un telèfon espera d'un teclat de debò.

#### Un ratolí de debò, i el de l'exemple (0.190.259)

L'amo: *«Continuarem amb l'app bthid reanòmena-la a "bt HID". Recorda, la idea es usar el panell com a mouse. Fes que funcioni un dels
dispositius, o el mouse o el teclat. Quan funcioni ja els ajuntarem»*, amb l'exemple `esp_hid_device` de la IDF 5.2.1 a la mà. Tres
coses, i les tres són de protocol i no de gustos:

- **El mapa del ratolí porta la roda, i és el del seu exemple, byte a byte**: `X`, `Y` i `Wheel` amb `Logical Minimum (−127)` i
  `Maximum (127)` i `Report Count (3)`, o sigui **quatre bytes de report** —botons, x, y, roda—, que és el que el seu `send_mouse()`
  envia amb `esp_hidd_dev_input_set(dev, 0, 0, buffer, 4)`. Abans el mapa declarava dos eixos i el report era de tres bytes: legal, i
  és el que aquesta casa tenia, però **el que tothom prova primer és el de tres eixos** i una roda que el mapa no declara és una roda
  que el report no pot portar. El mode `both` porta la mateixa roda (cinc bytes amb l'ID al davant).
- **Dues característiques d'entrada que diuen el mateix `Report ID` són un servei que no compleix el HID-over-GATT.** Al mode d'un sol
  report el mapa declara **un** report i cap ID, i les dues característiques (les que el mode compost necessita) contestaven totes dues
  `0, input`: un amfitrió que les llegeix no sap quina és la del mapa i la que triï pot ser la que té la mida de l'altra. Ara la
  segona contesta **`0xFF`**, un ID que el mapa no té, o sigui que **l'amfitrió veu exactament un report d'entrada** i la porta per on
  aquesta placa envia queda una. L'altra manera hauria estat tornar a registrar el servei a cada canvi de mode, i el servei és
  compartit amb el mesh (`ble_gatts_start` el crida `esp_ble_mesh_init`).
- **I la mida que un amfitrió llegeix és la del mapa d'ara**: `report_access` contesta quatre bytes al mode `mouse` (abans deia vuit a
  la primera característica i tres a la segona, o sigui que el valor que l'amfitrió llegia abans de subscriure-s'hi no era el de cap
  report del mapa).

**I dues línies que diuen on s'atura**, perquè això és el que una persona pot llegir d'aquesta banda sense un analitzador: una quan hi ha
quelcom per enviar i l'altre cap encara no ha escrit el CCCD —*«this board has something to send and the other side has not asked for
the reports yet»*— i una quan el primer report surt de debò —*«the first report went out: 4 bytes on 0x…»*—. Són una vegada per vincle,
no una per fotograma, perquè un ratolí envia un report per cada fotograma del dit (docs/50, docs/95).

**I l'app és un ratolí i prou** (`apps/bthid`, títol *btHID Mouse*, 0.3.1): o sigui que la pàgina d'espera té **un sol botó** (el
commutador del ràdio, que no s'encén sol) i la vista viva és el pad: el dit que camina mou el cursor de l'altre aparell i el toc que no
camina és un clic. El que la placa és es desa a `cfg btmode`, i el que s'ha deixat la persona és la vista (la sessió `state`).

**I el ratolí té els seus tres botons** (0.190.267, i la icona del teclat amb la insígnia a la 0.190.268, l'amo: *«afegeix 2 o 3 botons a l'app “btHID Mouse” a la part de dalt del panell que fa
de mouse, sota la barra de sistema per fer de botons del mouse»*): a la vista viva hi ha una fila de tres caixes —`Left`, `Middle` i
`Right`— a dalt de tot, sota la barra del sistema, i **el pad del punter comença sota la fila**. Un botó que el dit prem és un `bt.click`
de la seva paraula i **el pad no el veu**, perquè el rectangle que el dit camina (`pad_rect`) no arriba a la fila; un clic que no surt es
diu a la línia i al registre, i un que surt no diu res —un ratolí que escriu al registre cada clic és un registre que no es pot llegir.

#### El teclat, i la caixa que ha d'estar a dalt (0.190.267)

L'amo va demanar la versió teclat —*«Ara fes la versió teclat de l'app “btHID Keyboard”, revisa els requisits
https://novelbits.io/bluetooth-hid-device-design-example-project/»*— i va assenyalar-ne la dificultat abans que hi caiguéssim: *«Hauràs de
posar un TextBox sota la barra de sistema a dalt de tot... Si no hi ha focus, el teclat per software no surt ni es manté»*.

**I tenia raó, i és del sistema i no de l'app**:

- **La banda del teclat segueix el focus** (docs/30, docs/90): surt quan una caixa que pren tecles el té i marxa quan el focus se'n va. Una
  caixa **no enfocada** és una pàgina on el teclat no surt, i una pàgina que només espera que el dit trobi la caixa és una pàgina morta en
  una placa que no té tecles físiques. El que fa `apps/btkbd` en entrar a la vista viva és donar-li el focus i aixecar la banda ella
  mateixa (`kb.target = box` i `kb:show('alpha')`), i `on_resize` la torna a col·locar quan la banda apareix o marxa.
- **La banda ocupa els 256 px de baix**: una caixa allà seria una caixa tapada pel teclat que ella mateixa fa sortir. Per això va **a dalt
  de la vista, a tocar de la barra del sistema**, i el que queda al mig és el que diu què està passant.

**El que hi ha a la pàgina** (`apps/btkbd`, 0.1.1): la caixa a dalt, la línia d'estat i la nota; la banda la posa el sistema. El que
s'escriu marxa **lletra per lletra** amb el `bt.type` que ja existia, i el que compta és la diferència amb el que ja ha sortit —el que
s'ha esborrat són retrocessos i el que s'ha afegit són les tecles, en aquest ordre—, o sigui que una lletra escrita al mig és un retrocés
i la cua tornada a escriure, que és el que fa un teclat de debò. **El retorn de carro de la banda viatja com a Enter i no es queda dins la
caixa**: una caixa d'una línia amb un salt a dins no és el que una persona ha escrit.

**Els requisits de l'article, un per un** (novelbits, *Bluetooth HID Device Design*) i què en fa aquesta banda: el servei `0x1812` amb
*HID Information*, *Control Point*, *Report Map*, *Protocol Mode* i l'*Input Report* amb el seu CCC i el *Report Reference* ja hi eren des
de la 0.190.204 (docs/50); **el que l'article recorda i aquí es compleix** és que el report ha de cabre en el que el mapa declara —el
teclat són vuit bytes i les tecles que en surten no en són mai més de sis alhora, amb els modificadors al primer octet— i que el que
arriba de l'amfitrió (el *Control Point* i el *Protocol Mode*) es llegeix i no s'ignora.

**I el que queda dit a les dues pàgines, perquè no enganyi**: les dues apps **no comparteixen vincle**. El mode es desa a `cfg btmode` i
canviar-lo torna a anunciar el mapa, o sigui que l'altre aparell s'ha de **tornar a aparellar** en passar d'una app a l'altra. El mode
`both` ho estalviaria —dos report IDs en un sol mapa— i no s'ha tocat perquè encara no s'ha vist funcionar en un amfitrió de debò, i el
ratolí sí que hi ha de funcionar.
**I la icona és el que l'app fa** (0.190.265, i refeta a la 0.190.268, l'amo: *«canvía el logo, ara es el logo de bluetooth, posa un mouse
amb logo bluetooth millor»* i després *«L'icona del mouse tampoc te garie detall.. curra't-ho»*): el cos d'un ratolí amb els dos botons en
el to clar i el cos en l'ombra, la ratlla que els separa i la roda amb les seves crestes, i **el logo de Bluetooth en una insígnia** a la
cantonada —disc de paper, anella de tinta i la runa de sempre, que és el mateix gest que el disc del `About`.

**Les dues icones segueixen la paleta de casa i no un catàleg** (0.190.268): paper 255, cos 221, ombra 187, detall 170 i tinta 68 —els
valors que surten de comptar els vint-i-vuit dibuixos de `tools/icons/`—, contorn de setze píxels i cantonades de quaranta-quatre. El
primer intent, amb els dibuixos de Lucide tal com venen, era de línia fina i blanca i no era d'aquest joc (docs/45).

#### Un controlador que ja era encès (0.190.263)

De la placa de l'amo, en obrir *bt HID* i prémer el commutador:

```
[I][APP] bt hid: bringing the radio up as a mouse
[I][NVS] cfg/btmode = mouse
E (83461) BLE_INIT: controller init failed
[W][BT] the radio did not come up (259): the keyboard has nothing to use
```

**259 és `ESP_ERR_INVALID_STATE`** i diu exactament una cosa: `esp_bt_controller_init()` ha trobat el controlador **ja inicialitzat**. És
la mateixa lliçó que el wifi de la 0.190.221 —el que s'inicialitza una vegada no s'inicialitza dues—, però aquí el que el deixava encès
no era el nostre codi:

- **El mesh** corre el seu amfitrió sobre el mateix controlador, i `mesh off` **només deixa de mirar** (`mesh_node_withdraw` desactiva la
  provisió i prou): el controlador i la pila es queden a dalt. Un `bt` després d'un mesh no pot funcionar fins a la propera arrencada.
- **Un `nimble_port_deinit()` que falla a mig camí**: `esp_nimble_deinit()` torna abans d'hora quan `esp_nimble_hci_deinit()` diu que no, i
  llavors `esp_bt_controller_disable`/`deinit` **no s'arriben a fer mai**. El controlador es queda encès per a la resta de l'arrencada i
  cada intent següent rep el mateix 259.

I hi havia una segona cosa, pitjor perquè era invisible: **`g_the_stack_is_up` no es desfeia** a `keyboard_stop()` ni a `host_off()`. Després
de baixar la pila, el següent `bt on` es pensava que ja hi era, es saltava `nimble_port_init` i anava a registrar serveis en un amfitrió mort.

El que fa ara [`the_stack_up`](../../lib/hal_esp32/src/bluetooth.cpp):

1. **La marca baixa amb la pila** als dos llocs que la desfan.
2. **Es pregunta l'estat abans d'inicialitzar** (`esp_bt_controller_get_status()`). Si no és `IDLE`:
   - i el mesh el té → es diu i no es toca, amb la veritat a la frase: *«the mesh has the radio …, and `mesh off` does not give it back
     yet: the keyboard has to wait for the next start of the board»*;
   - i no és de ningú → es desfà aquí (`disable` + `deinit`) i la ràdio es torna a provar. La línia del registre diu l'estat que hi havia
     i què se n'ha fet, o sigui que un 259 no torna a arribar sense explicació.

**El que encara no està fet, i és el següent**: que `mesh off` torni el controlador de debò (`esp_ble_mesh_deinit`), perquè llavors les dues
ràdios conviuen en una sola arrencada. No s'ha tocat perquè el provisioner té el paper desat a la NVS i un `deinit` a mitges el deixaria
sense xarxa fins a tornar a donar les claus; és una feina amb la seva pròpia prova.

#### El `-1` era el host sense memòria, i la paraula era d'un teclat (0.190.264)

La segona placa, amb el remei del controlador ja a dins, va arribar **més enllà i va fallar al pas següent** —que és exactament el que havia
de fer si el primer forat era el de debò—:

```
I (32181) BLE_INIT: BT controller compile version [59725b5]
I (32182) BLE_INIT: Bluetooth MAC: 30:ed:a0:ad:0a:fe
E (32214) BLE_INIT: hci inits failed
E (32215) BLE_INIT: nimble host init failed
[W][BT] the radio did not come up (-1): the keyboard has nothing to use
```

I l'amo, llegint-lo: *«“the keyboard” potser estem confonent-nos?»*. Sí: la porta es diu `keyboard_start` des que tot això era un teclat i el
missatge sortia d'aquell nom, en una placa que és un ratolí.

- **`-1` no és cap codi d'error**: és el que `nimble_port_init` contesta sempre que el host falla (`return ESP_FAIL` escrit a mà dins d'IDF),
  o sigui que amaga el motiu. Dins seu, el primer que fa el host és `os_msys_buf_alloc`, i **aquesta sí que torna `ESP_FAIL` quan un
  `calloc` no cap**: el host no podia prendre els seus blocs de treball (12 × 256 + 24 × 320, més les cues d'esdeveniments, d'ordres i
  d'ACL) en la RAM interna, perquè el controlador de la ràdio ja se n'havia menjat uns trenta kilobytes.
- **El remei**: `CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y`, que fa que `bt_osi_mem_calloc` demani `MALLOC_CAP_SPIRAM`. Va a les dues
  bandes que tenen ràdio **i** PSRAM (`lilygo-t5s3-idf` i `m5-paper-idf`, al `sdkconfig` i al `sdkconfig.defaults`); l'ATOM i el devkit, que
  no en tenen, es queden com eren. **El controlador no s'hi mou** —la seva memòria ha de ser interna—, i el que s'hi mou és el host: uns
  vint kilobytes que així no depenen de quants en quedin lliures quan una app encén el Bluetooth.
- **I el registre diu el que abans no deia**: la memòria interna lliure i el bloc més gran en el moment de pujar la pila, i una segona
  línia quan el codi és `ESP_FAIL` que nomena les dues causes possibles —els blocs del host, o un callback de HCI ja pres—.
- **La paraula, arreglada**: la frase surt d'`g_device_mode` —`the mouse`, `the keyboard`, `the keyboard and the mouse`— i l'èxit diu
  `the device is up as "lunokOS-XXYY" (the mouse)`.

**Provat**: `lilygo-t5s3-idf` i `m5-paper-idf` compilen amb el canvi de `sdkconfig` (recompilació sencera de la IDF a totes dues, perquè el
`sdkconfig` no és un fitxer de codi), i `emulator` segueix compilant. **El que no es pot provar sense la placa és que el ratolí aparegui al
mòbil**: el que hauria de sortir al registre ara és `the radio is up for the mouse: N B of internal memory before, M B after`.

#### La ràdio del wifi té commutador (0.190.221)

L'amo: *«Si pico l'icona de bluetooth es veu al log: `[I][NVS] cfg/bt = on`. Pero a la de wifi no ho he vist cap reacció. Es ben senzill... igual
que el cfg/bt cfg/wifi on/off. Si actives la wifi (passes a "on") està apagada en aquell moment, s'inicialitza»*. I tenia raó que no hi havia
reacció: la clau del wifi cridava `net::connect_stored`, o sigui que **intentava una connexió**, i en una placa que no té cap xarxa desada
contestava amb un avís d'una connexió que ningú no havia demanat.

- **`cfg/wifi`, una paraula a la botiga com `cfg/bt`**: `cfg wifi on|off` al prompt i la clau de la targeta fan el mateix, i el que es llegeix
  després és **el resultat i no el desig** —una ràdio que no arrenca desfà la tria i ho diu.
- **I per fer-ho calia separar el driver de la ràdio**, que aquesta banda tenia en un sol `espWifiReady()`: `esp_netif_init`, el bucle
  d'esdeveniments, les dues interfícies i `esp_wifi_init` **no es poden fer dues vegades** (la segona contesta `ESP_ERR_INVALID_STATE`), i el
  que s'encén i s'apaga és `esp_wifi_start`/`esp_wifi_stop`, que sí que es poden fer tantes vegades com calgui. `wifi_driver_up` fa el primer
  (una vegada) i `wifi_radio_up`/`wifi_radio_off` el segon; el nom vell `espWifiReady` es queda com a àlies del segon, perquè tots els camins
  que ja hi havia volien dir exactament això.
- **Encendre la ràdio la instal·la si cal**: això és el que demanava l'amo amb «si passes a on i està apagada en aquell moment, s'inicialitza».
  Apagar-la deixa anar el que hi hagués (`esp_wifi_disconnect`) i atura el driver **sense desinstal·lar-lo**, o sigui que tornar-la a encendre
  no torna a pagar el driver.
- **La paraula desada es compleix a l'arrencada**: `kernel::net::boot` mira `cfg::wifi_on()` i encén la ràdio, com fa el teclat de Bluetooth des
  de la 0.190.180; si no arrenca, el commutador torna a apagar-se i el registre diu per què.
- **I el que no vol dir encesa, tampoc**: aquest sistema usa la xarxa i la deixa anar quan la feina s'acaba (`net::poll` tanca la ràdio quan la cua
  és buida). El commutador diu «la ràdio pot treballar», i per això la clau no s'apaga tota sola quan una tasca acaba: el que fa la feina és
  `wifi connect`/`http get`/`ntp`, i el commutador és la porta de la persona (docs/30, docs/75).
- **On és**: `INet::wifi_radio_on/off/is_on` a `hal/hal.h` (amb definició per defecte, com la resta del HAL), `wifi_driver_up` i
  `wifi_radio_up` a `hal_esp32/src/net.cpp`, l'emulador a `hal_emulator`, `kernel::net::radio_on/off/is_on`, `cfg::wifi_on`/`set_wifi_on` i la
  clau `WiFi` de la targeta del panell.

#### El mesh: la placa com a node (0.190.212)

L'amo: *«pots posar algun mesh?»*, i és **Bluetooth Mesh**. El que hi ha és el primer pas de debò i no una demo: la pila ESP-BLE-MESH
(`CONFIG_BLE_MESH`) damunt de la **mateixa NimBLE** del teclat, amb **un element** i el servidor de configuració —el mínim que tot node ha de
tenir—, i res més. `mesh on` encén la ràdio sense oferir-se com a teclat, i el node s'anuncia pels dos portadors de provisió (l'anunciant i el
GATT), o sigui que l'app d'un telèfon —nRF Mesh o la d'Espressif— el troba, el provisiona i li dóna una adreça, que la pila **desa a la NVS** i
que `mesh` ensenya (`this node is 0x00A1 of a network`). Cada pas de la provisió és una línia del registre (`a provisioner is talking to this
node`, `provisioned: this node is 0x…`, `the mesh forgot this node`).

- **El mesh i el teclat no són alhora**: the stack is shared and the advertiser is one, o sigui que `mesh on` amb el teclat enlaire es refusa i
  diu què fer (`cfg bt off`). És el mateix criteri de la 0.190.192: dues coses que es disputen el mateix aire es diuen per separat.
- **Cap model de més**: un model és una promesa (un llum, un relé) i aquesta placa encara no n'ha triat cap. El node sap existir, tenir adreça i
  respondre al provisionador, i els models són la feina següent.
- **On és**: `IMesh` a `hal/hal.h`, `Esp32Mesh` a `hal_esp32/src/mesh.cpp`, el node en C a `hal_esp32/src/mesh_node.c` —els macros d'Espressif
  fan servir `[0 ... N]`, que en C++ és una lambda—, el tauler que el dóna (`hal_atom`, `hal_paper`, `hal_t5s3`), `kernel::mesh` i l'ordre `mesh`
  del prompt. Una banda sense `CONFIG_BLE_MESH` contesta que no i no engega res.
- **I la placa té un sol nom** (0.190.215, l'amo: *«canvia el nom de la màquina lunokOS-XXYY (dos ultims bytes de la mac, com amb el
  bluetooth)»*): `hal::the_board_name` (`esp32/board_name.h`) compon `lunokOS-XXYY` una vegada i el teclat de Bluetooth, **aquest node** i
  l'**mDNS** el demanen tots tres. El node el presenta a la resposta de l'escaneig (PB-GATT), que és el que l'app del provisionador ensenya
  abans de triar-lo; a l'anunciant (PB-ADV) la pila no hi posa cap nom, i això és de la pila. I l'mDNS deixava de dir `lunokos` a totes les
  plaques, que volia dir dues `lunokos.local` a la mateixa xarxa.
- **I costa**: la pila demana desenes de kilobytes de RAM interna quan arrenca, o sigui que el número que s'ha de mirar després de gravar-lo és
  `mem` amb el node apagat i amb `mesh on` (docs/75).

#### Les claus són d'aquesta placa: el provisionador (0.190.218)

L'amo: *«no puc provisionar jo manualment la clau del mesh per linia de comandaments, com amb el wifi?»*. La resposta curta és que **un node
no pot rebre les claus a mà, i no és cap mancança de la pila**: la provisió **és** l'intercanvi que les crea —la NetKey viatja xifrada amb la
clau que surt de l'ECDH, i la DeviceKey en surt d'allà mateix—, o sigui que «posar-hi la clau com amb el wifi» no vol dir escriure-la al node,
vol dir **ser qui la té**. I això és el que fa aquesta versió: `CONFIG_BLE_MESH_PROVISIONER` encén el provisionador d'Espressif i aquesta
placa passa a ser-ho de la **seva** xarxa. El que abans s'oferia a un telèfon ara és qui reparteix.

```
lunok> mesh key
mesh: this board has a network of its own, and the keys are new
  net 6f1c…(32 dígits hexadecimals)…
  app 0b9e…(32 dígits hexadecimals)…
lunok> mesh find 20
mesh: 1 device(s) said they are in no network
  #0 -54 dBm dd00…(la UUID de 16 bytes)…
lunok> mesh prov 0
mesh: device #0 gets 0x0005; it takes a moment, and the log says when it is in
[I][MESH] 0x0005 is in the network: 3 model(s) bound to the application key
lunok> mesh nodes
mesh: 1 device(s) in this board's network
  0x0005
```

- **Les claus són setze bytes i prou**: el mesh no hi posa cap checksum —a diferència del Thread, que sí—, o sigui que el que es llegeix a
  `mesh key show` és literalment la clau. `mesh key` les fa amb `esp_fill_random`, i `mesh key <net> <app>` hi posa les que vulgui l'amo
  (trenta-dos dígits hexadecimals cada una, que és el que dóna `openssl rand -hex 16`). Amb una de sola no n'hi ha prou: la de xarxa mana la
  xarxa i la d'aplicació és la que fa que els models es puguin parlar.
- **L'índex el tria la placa i és 0 per a totes dues**, que és el que fa l'exemple d'Espressif (`ESP_BLE_MESH_KEY_PRIMARY`). Si la clau de
  l'índex 0 canvia, la pila contesta `EEXIST` a l'add i el camí és l'**actualització** —el procediment de refresc de claus del mesh—, no una
  escriptura a mà.
- **I es desen, que és el que fa que siguin seves**: `cfg/meshnet` i `cfg/meshapp` a la botiga, en hexadecimal, i `kernel::mesh::start()` les
  torna a posar quan la placa arrenca. Una xarxa que s'ha de tornar a fer a cada reiniciada no és una xarxa.
- **Provar no és provisionar**: `mesh find [segons]` escolta els aparells que no són enlloc (deu segons de sèrie, fins a dos minuts) i els
  deixa en una llista de vuit amb la UUID, qui els ha sentit i amb quina força; `mesh prov <n> [<adreça>]` en tria un. **L'adreça la diu
  aquesta placa**: 0x0001 és la seva i de 0x0005 ençà les que dóna, amb la primera lliure de la taula, perquè una placa d'aquesta casa sàpiga
  sempre on és sense dependre del que un telèfon va decidir l'última vegada.
- **Una sola porta per provisionar**: el full d'Espressif de `provisioner_prov_device_with_addr` diu, a la nota 4, que un provisionador no ha
  de fer servir alhora `provisioner_add_unprov_dev` i aquesta, i la nota 2 diu que el portador ha de ser exactament el que va arribar a
  `RECV_UNPROV_ADV_PKT_EVT`. Abans les cridàvem totes dues: era demanar dues provisions del mateix aparell.
- **I tot seguit la clau d'aplicació, o la provisió no serveix de res**: la provisió deixa el node amb la NetKey i una adreça **i els seus
  models muts**, perquè cap no té una AppKey lligada. Això no ho fa la provisió: ho fa el provisionador amb el **client de configuració**
  (`ESP_BLE_MESH_MODEL_CFG_CLI`), que és l'únic que pot manar-li coses amb la clau de dispositiu. Són tres passos i cada un espera la resposta
  de l'anterior: `COMPOSITION_DATA_GET` (per saber quins models té i a quins elements), `APP_KEY_ADD` (la clau) i `MODEL_APP_BIND` per a cada
  model de la SIG de la seva composició. Una resposta que no arriba es torna a provar tres vegades, i un lligam que no contesta es deixa estar
  i els altres segueixen: tot això surt al registre.
- **El context d'un missatge de configuració porta la clau de dispositiu, no la d'aplicació**, i no és cosa nostra: la capa de la pila que
  tradueix l'API (`btc_ble_mesh_config_model.c`) hi posa `BLE_MESH_KEY_DEV` passi el que passi, i el model de configuració té
  `keys[0] = BLE_MESH_KEY_DEV`. O sigui que el que s'escriu a `ctx.app_idx` no decideix res, però s'hi posa l'índex de la xarxa per no deixar
  un número inventat.
- **El client de configuració és una opció de menú i no ve amb el provisionador** (`CONFIG_BLE_MESH_CFG_CLI=y`): sense ella
  `esp_ble_mesh_config_client_set_state`, `…_get_state` i `…_register_config_client_callback` **no existeixen al binari**, i el que es veu és un
  `undefined reference` a l'enllaç que no diu què falta —va passar exactament així. I com que una opció nova al `sdkconfig.defaults` no entra si
  el `sdkconfig.<banda>` ja existeix, cal esborrar `sdkconfig.<banda>` **i** `.pio/build/<banda>` abans de tornar a compilar (docs/80). Des
  d'ara un build amb provisionador i sense aquesta opció s'atura amb un `#error` que ho diu amb paraules.
- **Els entrebancs de l'API de la 5.1.2** (tots quatre picaven amb `-Werror` i cap era al codi de més amunt): `prov_uuid` i
  `prov_unicast_addr` són **`const`** dins de `esp_ble_mesh_prov_t`, o sigui que la identitat del provisionador es **declara** amb la variable
  i no s'assigna després; `provisioner_add_local_app_key` vol l'**índex per valor** (`(app_key, net_idx, app_idx)`) i no per punter, i les
  dues funcions de les claus viuen a `esp_ble_mesh_networking_api.h` i no a la de provisió; `provisioner_prov_link_open` **no té**
  `prov_link_idx` (aquell camp és del `prov_read_oob_pub_key`) i el que diu l'event és el portador; i `prov_device_with_addr` vol **sis**
  arguments, amb la UUID, l'adreça i el tipus d'adreça que va arribar a l'anunci.
- **L'ordre de la ràdio és el de la 0.190.217 i no es toca**: pila, `esp_ble_mesh_init` (que registra els serveis GATT del mesh) i després la
  tasca de l'amfitrió. I **`mesh key` amb prou feines arrenca la pila** si no ho era, perquè desar una clau és un missatge a la tasca `btc`
  que neix amb la pila; `mesh on` sense claus deixa la pila enlaire **sense dir que ha fracassat**, perquè és el primer pas.
- **On és**: `IMesh` a `hal/hal.h` (les portes del provisionador tenen definició per defecte que contesta que no, com la resta del HAL),
  `Esp32Mesh` a `hal_esp32/src/mesh.cpp`, tot el provisionador en C a `hal_esp32/src/mesh_node.c`, `kernel::mesh` amb les claus a `cfg`, i les
  ordres `mesh [on|off|key|find|prov|nodes]` al prompt. Una banda sense `CONFIG_BLE_MESH_PROVISIONER` **continua essent un node** que
  s'ofereix, i ho diu (`this build is a node only`).
- **I costa, mesurat i no estimat** (ATOM Matrix, 22/11): la imatge fa 2 131 994 B de 2 621 440 —el **81 %** de la partició d'app— i la RAM
  estàtica 85 056 B (240 B més que sense el provisionador). O sigui que hi cap a l'ATOM; el T5S3 i el Paper es queden de moment només node fins
  que es miri si hi caben (docs/75).

#### El que s'hi ha anat afegint (0.190.189 → 0.190.191)

- **El nom d'aquesta placa porta l'adreça** (0.190.197): `lunokOS-A1B2` amb els dos últims bytes de la MAC del ràdio (`ESP_MAC_BT` de
  l'eFuse, que es pot llegir abans que la pila sincronitzi), i `lunokA1B2` si el nom no cabés a la resposta de l'escaneig. Un nom igual a
  totes les plaques és una llista de teclats que ningú no distingeix.
- **I els vincles es desen de debò** (0.190.197): `CONFIG_BT_NIMBLE_NVS_PERSIST` no hi era, o sigui que la botiga del ràdio vivia a la RAM.
  El símptoma no era cap error sinó dues coses que no funcionaven mai: el teclat d'un mòbil s'havia de tornar a aparellar a cada reinici, i el
  `bt host reconnect` no podia trobar res. Ara els vincles van a la partició NVS que el sistema ja té per als ajustos.
- **Els noms de la llista** (0.190.189). El nom d'un aparell no cap als trenta-un bytes de l'anunci: qui el porta és la **resposta de
  l'escaneig**, que arriba com un segon event de la mateixa adreça. La fila s'actualitza en lloc de descartar-se, i l'exploració és
  activa i sense filtre de duplicats. **Va ser un error meu** i l'amo el va veure des de la placa: *«0 (no name) 68:13:85:87:EE:3B other
  … No hi han noms ?»*.
- **`bt scan [segons]`** (0.190.191): tres segons de sèrie i fins a seixanta, perquè un aparell que dorm no es veu en tres segons.
- **I la llista diu què és cada aparell** (0.190.196, l'amo: *«No em crec que no pesqui ni un sol nom! ni identificar el tipus, ni cap info
  adicional (senyal batería… no sé alguna dada mes)»*). El que es llegia de l'anunci era el nom i prou; ara també se'n treuen tres coses que
  hi són i que es llençaven: el **senyal** (`disc->rssi`, en dBm), l'**aparença** (`0x19` del registre del Bluetooth: telèfon, ordinador,
  teclat, ratolí, televisió…) i el **primer servei de setze bits** que anuncia (`0x1812` és un HID, `0x110B` un aparell d'àudio). El tipus
  és l'aparença, i si no n'hi ha el servei; i quan les taules no el saben, la línia de sota diu els números en lloc d'inventar-se'n un nom.
  **El que no es pot tenir a l'scan, i es diu**: el nom d'un aparell que no l'anuncia només es pot llegir **connectant** (GAP Device Name,
  `0x2A00`), i la bateria igual (`0x180F`); són les dues feines que queden en aquesta llista.
- **Tornar-hi sol** (0.190.191): els vincles viuen a la botiga del ràdio, o sigui que `host_reconnect` pregunta
  `ble_store_util_bonded_peers` i llança el link a l'arrencada, **sense esperar** —el boot no ha de costar mig segon de prompt.
- **I si no hi ha res a què tornar, el ràdio no s'encén per preguntar-ho** (0.190.203). El camí de la botiga buida sortia deixant
  `g_host_wanted` a cert i la pila encesa, i això es veia a la Paper a cada arrencada: `bt` deia `host: on` sense que ningú ho hagués
  demanat, i el controlador —i el PHY amb ell— corria per a un vincle que no existia. La primera arrencada d'una placa que mai no ha
  aparellat res s'ho emportava tot, i el registre ho diu amb dues línies que no semblen del mateix assumpte: `W (861) esp_psram: … 4MB
  is mapped` i, dos segons i mig més tard, `W (5480) phy_init: failed to load RF calibration data (0x1102), falling back to full
  calibration` —la partició `phy_init` era buida perquè en aquest build el ràdio no havia corregut mai, i la calibració sencera del PHY
  és el que hi havia entremig—.
- **I el remei no és baixar la pila allà mateix, i això es va aprendre amb un pànic**: la primera versió d'aquest arreglament cridava
  `host_off` tot just després d'haver pujat la pila, i la placa va contestar `Guru Meditation Error … LoadProhibited` amb la traça a
  `ble_hs_event_start_stage2 → ble_hs_start → ble_hs_sync → npl_freertos_callout_is_active` i `EXCVADDR 0x0`, anunciat cada dos segons
  amb `NimBLE: Host not enabled. Dropping the packet!`. **Un `nimble_port_deinit()` just després de `nimble_port_freertos_init()` deixa
  la tasca del host corrent amb les seves estructures ja alliberades.** Desfer-se de la pila només és segur quan la pila ha acabat de
  néixer, i el lloc on es fa és el `bt off` del prompt.
- **La resposta bona és no encendre-la**: una marca d'un byte a la NVS (`bt/paired`, escrita al `ENC_CHANGE` sense error i esborrada a
  `bt host forget`) contesta `host_remembers()` sense obrir la pila, i l'arrencada només reconnecta si diu que sí. Una placa verge no
  toca el ràdio i no paga la calibració del PHY per res; el `bt host reconnect` del prompt passa per sobre de la marca, perquè allà ho
  ha demanat una persona. La marca diu la veritat en els dos sentits: si diu que sí i la botiga és buida —l'altre cap ens ha
  oblidat—, la marca s'esborra i el que queda és el que hi ha (docs/50, docs/60, docs/70).
- **Els llums del teclat virtual** (0.190.191): el **report de sortida** del HID és el que el mòbil escriu per encendre el Caps Lock, i
  ara es llegeix (l'`0x2A32` del boot i una característica **Report** `0x2A4D` de sortida per a qui parla el protocol de report) i es diu
  al registre i a `bt`. La placa no té teclat físic: saber-ho és l'única cosa que en pot fer.
- **El ratolí** (0.190.191): el mateix servei `0x1812` amb un report de **tres o quatre bytes** en lloc de vuit. Es distingeix per la
  **mida** i es compta —on és i quins botons té, a `bt`—, i el clic va al registre quan canvia (un moviment no: un ratolí en fa desenes
  per segon). **El cursor a la pàgina no hi és**: això és una feina de la UI i no d'aquest fitxer, i fer-la a mitges seria un ratolí que
  mou un punter invisible.
- **El que encara no hi és, i es diu**: la **bateria** del teclat (`0x180F`/`0x2A19`) vol una segona descoberta de serveis dins la
  cadena de callbacks que fa que el teclat funcioni, i no s'ha volgut embolicar aquesta cadena per una dada de guarniment.
- **I el que costa, fila a fila** (0.190.199, l'amo: *«la comanda stats s'està quedant enrere… no trobo el bluetooth i segur que hi han
  molts més que falten, calen sondes per tot arreu»*). El ràdio i obrir una app eren les dues úniques feines que no tenien fila a `stats`,
  i ara en tenen cinc: **`radio.up`** (encendre la pila, per la porta del dispositiu o la de l'amfitrió: el preu és el mateix i és el que
  decideix si el ràdio cap en una placa sense PSRAM), **`radio.scan`**, **`radio.pair`** (lligar-se o tornar a un de conegut), **`radio.tap`**
  (una tecla, amb l'espera de prémer i deixar anar a dins) i **`app.start`** (l'intèrpret i el primer torn, al voltant de `luabind::start`).
  Cap d'elles no és dins el HAL: **el HAL no mesura res** —les sondes les posa el nucli al voltant de les crides—, que és la mateixa regla
  que fa que un conductor no sàpiga que el `stats` existeix (docs/75).
- **`keys_arrived` no es mesura, i s'ha de dir per què**: les tecles que arriben de l'aire es tracten al **fil de NimBLE** i no al torn, i
  el metre no té pany. Una escriptura des d'allà es barallaria amb el `stats` del nucli, i un comptador que es trenca és pitjor que un de
  fosc: el que es compta és el **torn que la tecla desvetlla**, que ja és a `kernel.loop` i a `queue.wait`.

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
  això és el `flush` del PAL, que espera que la FIFO i el registre de desplaçament de la UART quedin
  buits (amb un sostre de cent mil·lisegons, 0.190.69). És al nucli i no a cada placa perquè la consola
  és del sistema i no del xip.
- **El processador arrenca a 160 MHz** i es pot dur a **240 o a 80**, mai per sota: el rellotge el mou
  `esp_pm` des de la 0.190.175 —que manté l'APB i el tick en pas— i per sota de 80 l'APB baixa i cap perifèric es
  reconfigura —ni el port sèrie, que és l'única interfície d'aquesta placa. Un valor que no sigui un dels
  passos es refusa, no s'arrodoneix.
- **I la línia s'ha buidat també abans de moure el processador** (0.179.43): la velocitat del port sèrie
  surt del rellotge del bus, i el xip la torna a calcular quan canvia la freqüència. Els bytes que ja són
  a la FIFO i al registre de desplaçament surten a la velocitat de abans i el port els ensenya com a
  **escombraries**, o sigui que la corrupció cau a l'**última línia escrita abans del canvi** i no a la que
  ve després. Es va veure al registre de la placa: `[D][PANEL] paint 60,356 72x76 … (page 108lx␘␂����2,
  76 rows of 36 B)` amb `[I][POWER] cpu 160 MHz from 240` a la línia següent. `move_to` i `set_cpu_mhz`
  buiden la consola (`IConsole::flush()`) abans de cridar la placa, pel mateix motiu que el son.
- **I la consola no penja de l'APB** (0.190.177, vist a l'ATOM el 21/11: `lunok> [I][P��f␙�X] cpu 80 to␀␘L␓␘…`): la UART de l'ESP32 treu
  el baud del seu rellotge font, i `UART_SCLK_DEFAULT` és l'**APB** — el mateix que el DFS mou. El PAL la declara amb **REF_TICK**
  (1 MHz, que la IDF manté quan l'APB canvia) a l'ESP32 i amb el **cristall** a l'S3; 115 200 sobre 1 MHz és un divisor de 8,68 que el
  perifèric accepta amb la part fraccionària (docs/60).
- **I sí que hi ha DFS des de la 0.190.175** (abans aquesta línia deia el contrari): `CONFIG_PM_ENABLE` és a les sis bandes i el
  rellotge el mou `esp_pm` entre un **terra** i un **sostre** que li diu `power::apply_the_range`. El xip manté el tick, l'APB i els
  PLLs en pas quan canvia, i els drivers de **SPI master, I2C, I2S i SDMMC agafen el seu lock d'APB** durant la transacció, o sigui
  que el vidre, el tàctil, el panell de la T5S3 i la targeta no noten el canvi. **I el son lleuger automàtic, des de la 0.190.178,
  també hi és** —la 0.190.175 el va deixar apagat a propòsit i aquesta línia deia que el tickless tindria la seva idea de quietud:
  el que faltava no era la idea sinó **la pregunta**. La gestió de potència crida `the_doors_are_shut()` abans de cada son, i aquest
  pany només és obert mentre el torn del nucli espera a la cua amb les portes de la placa armades (`open_the_nap_doors`, docs/60).
- **I el terra i el sostre es poden limitar** (0.189.107 i 0.190.175, l'amo: *«[X] Frequency scaling allowed: [X] 80Mhz
  [X] 160Mhz [X] 240Mhz»*, i després *«el toggle del mig, fora»*): `Allow 80 MHz` i `Allow 240 MHz` són els dos únics ajustos i
  són el `min_freq_mhz` i el `max_freq_mhz` que rep el xip —`cfg freq 240 off` baixa el sostre a 80 i `cfg freq all` els torna—
  i **el sostre val per a tothom**, també per a una feina que demana el cim per pintar, perquè un ajust que només és una
  suggerència no és un ajust. **El 160 no és cap ajust**: `cfg::set_freq_allowed(160, …)` respon que no, perquè el DFS pot passar
  per qualsevol freqüència entre el terra i el sostre i una que no es pot permetre ni prohibir no ha de ser un toggle (docs/30,
  docs/60).
- **I un dit al vidre vol el cim** (0.190.87, l'amo: *«Si el dit està tocant la pantalla i hi ha cpu scaling => 240mhz»*).
  Mentre hi ha un contacte, `power::note_a_finger` agafa el lock del sostre —el lleva el dit i no un rellotge— i el xip no té res a
  decidir: la càrrega d'un dit quiet és la d'una placa avorrida, i el que aquell dit espera és la resposta d'un control. **Passa pel
  mateix `hold_cpu` de sempre**, o sigui que amb l'escala apagada no mou res i mana la fila `Speed:` (docs/60).

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

**I la quarta cosa que se sap fer amb el corrent és no gastar-lo esperant** (0.190.63, l'amo: *«[X] Light sleep instead of wait. Aplica
lightSleep allá on puguis»*). `IPower::nap(max_ms)` és un **son lleuger amb hora de tornada curta**: el xip atura els rellotges, la RAM i
l'estat no es perden (no és un reset) i el temporitzador del RTC el torna; la resposta és sempre, o sigui que qui l'ha demanat segueix el
seu camí. `can_nap()` és la pregunta prèvia, perquè les portes del despert són les de la placa:

| placa | `can_nap()` | per què |
|---|---|---|
| Paper | **sí** | la tecla del dial (GPIO38), **la línia del GT911** (GPIO36, que cau mentre hi ha un dit i puja quan el marc s'ha pres: és el flanc que un son lleuger vol) i la consola |
| ATOM, Matrix, T-Koala, ESP32 | **sí** | la tecla de la placa i la consola |
| T5 E-Paper S3 Pro | **no** | la seva consola és el **USB del xip** i un son lleuger atura aquell perifèric: el prompt s'ensordiria a cada becaina |
| l'emulador | **no** | el PC no dorm, i un run que s'aturés a posta només faria més lentes les proves |

La consola és **la porta que no es pot perdre**: `esp_sleep_enable_uart_wakeup(0)` i, si el xip no la pren, la becaina no es fa. Cada
porta s'arma amb `gpio_wakeup_enable` (nivell baix, que és el que fa una tecla a massa i la línia oberta del tàctil) i **es desarma
tota en sortir** amb `esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL)`: l'IDF guarda els despertars en una estructura estàtica que
el son profund també llegeix, o sigui que una becaina que deixés la consola armada acabaria despertant un son profund per una tecla que
ja no és —i el compte del temporitzador es posa a zero amb ells (`esp32_note_wake_cleared`), o el `disarm` de després demanaria al xip
que tragués una cosa que ja no hi és— (docs/60, docs/95).

**I el son profund no l'escolta mai, la becaina sí** (0.190.152, l'amo: *«l'UART si que ha de despertar el lightSleep, si no no es pot
usar la shell per serial (es menja caràcters) pero no de DeepSleep»*). Són dues preguntes i les dues s'han de fer per separat: el
**commutador** de la pestanya Battery (`cfg.uart_wake`, clau `uartwake`, `cfg uartwake on|off`) diu si la persona vol que el cable
desperti la becaina, i la **placa** diu si la seva consola pot ser una porta amb `IPower::the_console_can_wake()` — el T5 S3 Pro
contesta que no, perquè la seva consola és el USB del xip. Amb les dues a favor, s'arma a `nap()` i al `sleep light` explícit; a
`SleepMode::kDeep` no s'arma mai, perquè el camí del son profund neteja **totes** les fonts abans d'armar les seves portes. Una
becaina que es mengi el que una persona escriu és pitjor que no estalviar res, i tancar aquesta porta deixa un avís a `CFG`
(docs/30, docs/60).

## La consola

Els primers bytes que surten per un terminal a 115200 **no són nostres**: abans que el PAL obri la consola el
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

### Què triga una tecla a arribar al prompt (0.190.190)

L'amo: *«podem fer mes responsiu el teclat al port serie? … suposo que el torn del nucli»*. No era el torn, i tampoc la becaina: el
camí d'un byte té **quatre** estacions i les dues que pesaven s'han retallat.

| Estació | Abans | Ara |
|---|---|---|
| El perifèric rep el byte | ISR del driver, ring de 1 024 B | igual |
| **El fil del PAL s'assabenta** | preguntava `available()` **cada 5 ms** | **la cua d'esdeveniments del driver**: la ISR el desbloqueja |
| El torn el recull | `pop` de la cua, despertat per l'event | igual |
| **L'eco al terminal** | **la línia sencera a cada tecla**: `\r` + prompt + tota la línia | **un byte**, quan el cursor és al final |

- **La cua del driver és la interrupció** que l'amo preguntava, i ja hi havia el perifèric per fer-la: `uart_driver_install` amb
  `queue_size` fa que la ISR hi deixi un `UART_DATA` i el fil del PAL s'hi bloquegi amb `portMAX_DELAY`. **Els bytes no es toquen**:
  l'event diu que n'hi ha i qui llegeix és el torn. I a la banda del **USB-Serial-JTAG**, que no té driver amb cua (es llegeix la FIFO a
  mà), el sondeig baixa de cinc mil·lisegons a un.
- **L'eco era el que més es notava**: [`redraw_line`](/home/sharek/code/lunokOS/lib/kernel/src/shell.cpp:423) reescrivia la línia sencera
  per cada caràcter, o sigui que escriure una ordre de seixanta caràcters eren set bytes de prompt més seixanta **a cada tecla**, i el
  cost creixia amb el que ja havies escrit. Amb el cursor a la dreta de tot, ara s'escriu el caràcter i prou; quan el cursor és al mig,
  o hi ha espais a esborrar, es reescriu, que és quan el redibuix és el que cal.
- **El que no s'ha tocat**: el `wait` del torn (fins a un segon quan no hi ha cap app) no canvia la latència, perquè l'event el desperta;
  i la becaina no hi entra mentre tecleges, perquè cada byte renova el comptador de quietud i el gate és de deu segons.

### El terminal del cable: qui llegeix les tecles ara (0.190.222)

L'amo: *«Vull una migració completa de la shell a aquest terminal serie. Poder moure el cursor, fer efectes, i colors dona molta vidilla :)»*.
El que hi ha a sota del prompt continua essent el mateix shell, i el que canvia és **qui llegeix els bytes**. Són tres decisions i cap és
gratuïta:

- **Un sol lector**. El component `console` porta `linenoise`, que llegeix per `stdin`; si el fil `pal_rx` continués avisant el torn i el torn
  continués llegint, hi hauria **dos editors** sobre el mateix ring i les tecles es repartirien a sorts. Amb el terminal enlaire
  (`IConsole::has_a_line_editor`) **el torn no llegeix** —`read()` contesta zero— i el fil del PAL **no avisa**, però segueix **buidant la
  cua d'esdeveniments** del driver: la ISR de la UART hi deixa un `UART_DATA` per ràfega i una cua plena fa que el driver cridi i perdi
  events, que és una avaria al registre que no és cap avaria.
- **Un sol camí d'escriptura**. Amb el terminal, tot el que el nucli escriu —el registre, una taula, un avís— passa pel `write` del VFS i
  no per la FIFO del perifèric. Dues plomes al mateix paper es trepitgen: una línia de registre cauria **al mig** d'una comanda que s'està
  editant i el cursor de `linenoise` quedaria descol·locat.
- **I el terminal és multitasca** (0.190.243, l'amo: *«el terminal serie ha de ser multitasca… per poder seguir atenent al usuari encara que
  la pantalla estigui robant molt temps de torns»*): **dues tasques i el torn no toca el filferro**. `pal_rx` llegeix i edita la línia;
  **`pal_tx`** s'inicialitza amb la consola i queda esperant, i el que el nucli escriu es deixa en una **cua de 2 KB** (del munt, que la
  DRAM de la Paper no en té per a una de fixa) que aquesta tasca buida en trossos contigus —dues escriptures no es barregen mai—. El torn
  només espera si la cua és plena, amb un sostre de 200 ms. **I les escriptures que no passen per la cua** (`linenoise`, la sonda i el
  rentat, que són del component) **es fan amb la cua buida**: `flush` abans de cada una. El buidatge té dos passos: la cua i després el
  registre del perifèric, o el que quedés a la cua no arribaria abans d'un `poweroff` (docs/30, docs/75).
- **El driver del VFS, i les dues consoles no es tracten igual**. `linenoise` llegeix del descriptor 0, i per això la consola ha de ser al
  VFS amb una lectura que **bloquegi**. A la **UART0** el driver ja hi era des de la 0.190.190 (amb la seva cua) i només cal donar-lo al VFS
  amb `esp_vfs_dev_uart_use_driver`; els finals de línia es posen explícits (`ESP_LINE_ENDINGS_CR` a l'entrada, `LF` a la sortida) i
  `stdin` sense buffer. **I `LF` a la sortida vol dir «no toquis res»** (0.190.237, `components/vfs/vfs_uart.c`: el CR només s'insereix
  quan el mode **no** és `LF`), o sigui que els `CRLF` que aquesta casa escriu a mà surten sencers — que és el que la documentació de la IDF
  demana (*Console → Configuring UART*: el terminal envia CR en prémer Enter i espera **CR LF** per baixar de línia). El mode `CRLF` del
  fabricant (`esp_console_repl.c`) fa el mateix dibuix des de l'altra banda: el seu codi escriu `\n` i el driver hi posa el CR. Al **USB-Serial-JTAG** no hi havia driver des de la 0.190.91 —aquella banda llegeix la FIFO amb les crides de baix
  nivell— i el terminal en demana un: s'instal·la amb `usb_serial_jtag_driver_install` i es dona al VFS. **Si no arrenca, el terminal no
  s'encén**: `start_the_terminal` contesta que no i aquesta banda es queda amb el camí de bytes de sempre, perquè una placa sense terminal
  ha de tenir prompt igualment.
- **La sonda, una vegada i abans de tot**. `linenoise` mesura el prompt i l'amplada preguntant-ho a l'altre cap (`␛[6n`) i espera la
  resposta per `stdin`: en un conducte que no és un terminal —`pio device monitor`, `cat`— la resposta no arriba i el primer caràcter de
  cada línia es menjaria la sonda. O sigui que es pregunta amb `linenoiseProbe()` (mig segon, sense bloquejar) i, si no contesta ningú,
  **`start_the_terminal` diu que no i el shell es queda amb el seu editor**. El REPL d'Espressif (`esp_console_new_repl_uart`) no es fa
  servir perquè quan falla la sonda es queda amb un `fgets` en mode lleig, i el que aquesta casa té és millor. Els noms de comanda
  s'enregistren a `esp_console` **només** per al tabulador i les pistes (`esp_console_cmd_register` no demana cap funció), i el `help` de
  la casa no es toca: qui executa la línia és el `shell` (docs/30).
- **El que no hi passa**: `linenoise` neteja la línia amb `sanitize()` abans de tornar-la, i el que no és `isprint` en el joc de caràcters C
  **cau**. O sigui que al terminal del cable una lletra accentuada **no arriba** al shell —el camí de bytes, l'emulador i la consola del
  vidre no en perden cap—. És una limitació del component i es diu (docs/30, docs/85).
- **La memòria**: la línia més llarga són cent vint-i-set caràcters, l'historial setze comandes, i les línies acabades passen per una cua de
  quatre. Si la cua s'omple es deixa caure la més vella: una comanda perduda és pitjor que dues de seguides, però qui llegeix és el torn i el
  torn pot trigar.
- **Els codis d'escapament es filtren a la porta i no a l'escriptor** (0.190.226): el codi porta el color sempre i `shell::filter_ansi` el treu
  quan `ansi off`, a l'única porta del nucli cap a la consola i també a la línia del registre. L'arrencada decideix el valor de la sessió amb
  `IConsole::speaks_ansi` i **no desa res** —una mesura no és una preferència—; el que la persona tria viu a `cfg/ansi` (docs/30, docs/85).
- **La placa no s'ha de quedar clavada, i això mana sobre el cursor** (0.190.225, l'amo: *«S'ha quedat pillat i no podia moure el cursor»*,
  amb `task_wdt: CPU 1: pal_line` al registre): `linenoise` viu d'un **estat a la pila** —el buffer, el prompt i **l'amplada**— i d'una
  amplada mesurada a l'altre cap. Si la pila s'omple o la mesura surt malament, `refreshMultiLine` **divideix per zero** (en aquest xip, sense
  divisió al maquinari, la rutina de la llibreria dona voltes per sempre) i `refreshSingleLine` fa baixar un `size_t` fins que dona la volta:
  la tasca es menja el processador i l'`IDLE` del segon nucli no arriba a córrer. Dues defenses: **vuit kilobytes de pila** per a la tasca
  —eren quatre— i **l'amplada preguntada abans** (`␛ [ 6 n`, resposta `␛ [ files ; columnes R`); si no és d'un terminal de debò (vint
  columnes o més), el terminal no s'encén i el shell es queda amb el seu editor. I el registre diu quants bytes de pila queden després de la
  primera línia (docs/50, docs/75, docs/85).
- **El fil de la consola torna a fer sondeig** (0.190.225): mira el port cada mil·lisegon en lloc d'adormir-se a la cua del driver.
  **La interrupció no s'ha esborrat** —és a `pal_esp32.cpp`, dins d'un `#if 0` i amb el seu nom— i amb el sondeig la cua del driver es buida
  **sencera** a cada volta, perquè quan s'omple la ISR de la UART crida des de la interrupció (docs/50, docs/85).

- **I la sonda es renta quan no contesta ningú** (0.190.224): a un terminal la pregunta és invisible, però en un monitor que no ho és queda
  escrita com a `␛[5n` i és el primer que llegeix una persona en arrencar. Es tapa amb `\r` i vuit espais, i el registre diu què cal per
  tenir cursor i colors. **La llista de monitors que valen no és d'aquesta casa**: la documentació de la IDF 5.1 (*Console → Line
  editing*) diu que un monitor que només ensenya els bytes crus del port no pot conviure amb l'editor de línia, i que els que se sap que
  funcionen són **GNU screen, minicom i `esp-idf-monitor`** (o `idf.py monitor`); `pio device monitor`, amb qualsevol filtre, **no** n'és un:
  es menja les respostes del terminal. **No hi ha interruptor per forçar-ho**: sense resposta, `linenoise` es menjaria les tecles que una
  persona escriu fins que en sortís una `R` (docs/50, docs/85).
- **La mida de la vista es pregunta de debò** (0.190.233, l'amo: *«al ser una sessió ansi, pots obtenir el tamany de la vista, oi?»*): dues
  preguntes i cap és `ESC[6n` a seques. Primer **`ESC[18t`** (*report window size in chars*, extensió d'xterm), que contesta
  `ESC[8;files;columnes t` —les dues dades de cop i el cursor quiet— i que **no és estàndard**: xterm.js la implementa però l'apaga per
  defecte (`windowOptions.getWinSizeChars`) i el VS Code l'encén. Si calla, el mètode de `linenoise` (`getColumns`): `ESC[999C` clava el
  cursor al marge dret i `ESC[6n` torna la columna, que **és** l'amplada, i el cursor es restaura amb `ESC[nD`. **Fins la 0.190.232 es
  preguntava `ESC[6n` sol** i es deia amplada a la columna on era el cursor —que al registre de la IDF és la llargada de l'última línia de
  log, perquè acaba en `\n`—, o sigui que el número del registre no era la mida de res. Es desen les dues (`g_columns`, `g_rows`) i **zero
  files vol dir «no ho ha dit»**. El motiu del rebuig també és un motiu: `kNoTerminal`, `kNoSize` o `kNoVfs`, perquè dir «no terminal» quan
  la sonda ha contestat és un registre que menteix (docs/30, docs/85).
- **I les taules de la consola es poden llegir** (0.190.238): les tintes són sis i són les de la casa (el cian clar del prompt, el gris del mig
  del vidre, el verd i el vermell de `log.cpp`), i les taules es fan amb `title`, `row` —una clau de divuit columnes comptada en caràcters
  visibles— i `row_inline`. `stats` no passa pel shell i per això filtra la seva taula amb el mateix `shell::filter_ansi` i la mateixa decisió
  `shell::ansi_on`: una segona regla per al mateix commutador seria la mena de cosa que aquesta casa no vol (docs/30, docs/85).
- **I una sessió oberta pel cable ensenya l'art de l'amo** (0.190.232): cinc línies de caixes en blanc i blau (`ESC[0;97m`, `ESC[0;94m`,
  `ESC[0;34m`), el dibuix que l'amo va triar a `data/res/logo2.ans`, que es diu quan una sessió s'obre —el primer Enter amb el terminal
  tancat— **i no a l'arrencada**, on sortiria a cada flaix. Va **darrere** `login: a new session`, que segueix dient què ha passat, i **només
  al cable**: `a_wire_console()` és la mateixa porta que el pany del son, o sigui que la consola que es penja (el vidre, l'emulador, les
  proves) no el diu. **Els bytes del `.ans` són dins del nucli i no es llegeixen del disc**: `data/res` viu a `littlefs` i canviar-hi una
  lletra vol `uploadfs`, que reescriu la partició sencera — i al ATOM, sense targeta, s'endú `/data` amb el registre de la bateria a dins.
  Cada línia del literal acaba en `CRLF` (el `.ans` porta `LF` sol) i l'últim codi és un `ESC[0m`, perquè **l'art no pinti el prompt**
  (docs/30, docs/70).
- **I es pinta a la dreta, amb la sessió i no a la cantonada** (0.190.234, l'amo: *«posala alinada a la dreta a dalt, el més alt possible i
  el més a la dreta possible»*, i després *«massa alt, es perd amunt amb el retorn de carro i també veig com salta de la dreta a l'esquerra
  abans de pujar»*). **Un terminal que fa scroll no té cantonada**: posar el dibuix a la fila u de la vista fa que el primer salt de línia de
  la sessió el faci pujar i se n'endugui la primera línia, i el següent la segona. O sigui que l'art es pinta **en el seu lloc**, just darrere
  de `login: a new session` i línia a línia, i puja amb el que la persona escriu com qualsevol altra línia. La dreta es troba **sense
  preguntar la mida**: a cada línia, `ESC[999C` deixa el cursor clavat al marge dret —el terminal no el deixa passar, sigui quina sigui
  l'amplada— i `ESC[58D` el torna enrere l'amplada del dibuix menys un (les cinc línies fan 59 columnes). **El cursor s'amaga mentre es
  col·loca** (`ESC[?25l` / `ESC[?25h`): el que s'ha de veure és el dibuix i no el camí. La mida que la 0.190.233 mesura només serveix per no
  dibuixar de més (un terminal que declara menys de 59 columnes o menys de 5 files no rep l'art), i **sense ANSI no es dibuixa res**
  (*«Si no hi ha ansii, no mostris l'art»*). **I sota el dibuix, centrada en les seves 59 columnes**, va la llegenda en blanc i negreta
  (`ESC[1;97m`): la versió i la placa (0.190.239) — centrada al dibuix i no a la pantalla, que és de qui és la llegenda (docs/30, docs/85).

### La shell té la seva tasca, i el que no pot fer ho demana (0.190.244)

L'amo: *«la Shell en una tasca independent no pot arrencar apps per si sola, les envia a una cua perquè el nucli les pugui executar. Per matar
una app el mateix. S'envía a la tasca del torn i allà es mata o s'arrenca l'app. Aquestes cues han de ser protegides amb mutex»*.

Fins aquí la línia es llegia en una tasca (`pal_rx`), s'escrivia en una altra (`pal_tx`) i **s'executava al torn**: el terminal era
multitasca i l'ordre de les tasques era el pitjor possible, perquè la línia esperava el torn i el torn esperava la línia. Ara són tres
peces:

- **`lunok_shell`**, la tasca nova: s'inicialitza **tot just després que la shell tingui el seu estat i abans de les alarmes, de l'app de la
  sessió i de la pàgina de casa** (0.190.246) i es queda esperant (`IConsole::wait_for_a_line`, vint mil·lisegons de sostre) el que `pal_line`
  hi deixa. **L'ordre és el que la fa útil**: en una T5S3 cada pàgina de l'`epdiy` és un `draw` de tres-cents a quatre-cents mil·lisegons, i
  una tasca que naixés després de l'app de casa deixaria el port mut tota l'arrencada (l'amo ho va veure: els registres de l'`epdiy` i cap
  prompt). Les línies que arribin mentre l'arrencada encara va queden a la cua i el torn les fa al primer `poll`, i **la resta de l'arrencada
  es fa amb el pany del torn a la mà** perquè l'eco d'una tecla no entri mentre es munten el vidre i l'intèrpret.
  **Sis kilobytes** de pila: llegeix, fa l'eco i deixa la línia a la cua, o sigui que les comandes que pinten es fan a la pila del torn, on es
  feien abans de tot això. Prioritat per damunt del torn, i no li lleva res: passa la vida esperant.
- **`kernel/orders`**, la cua: **quatre línies** de cent vint-i-vuit bytes amb **mutex** (`pal::make_lock`) i **el qui la demana**. Aquest
  últim no és un detall: al cable la sessió arrenca **tancada** (0.190.184, perquè el xip pugui fer becaines), i la línia d'una persona tancada
  **obre la sessió i no s'executa**; una ordre del sistema —la pàgina de casa d'una arrencada— ha de córrer igualment. Sense la distinció, el
  primer `home` de cada arrencada se'l menjava el `login` i la pàgina del sistema no sortia mai a la placa (0.190.251, docs/30). Des de la 0.190.245 no hi passen
  «arrencades» sinó **comandes senceres**: la shell només llegeix i fa l'eco, i el que ha escrit una persona és una línia en aquesta cua. El que
  no hi cap **es refusa dient-ho**: una comanda perduda en silenci seria una tecla que no ha fet res, i qui la veu refusada pot tornar a prémer
  Enter. El torn les buida al començament de cada volta (`shell::carry_out_the_orders` → `take_a_line`), **abans de l'app**, o sigui que una
  comanda es fa al mateix torn en què arriba (docs/30).
- **`kill`**, la comanda nova: tanca l'app que és al vidre. És una comanda normal perquè totes les comandes es fan al torn; fins avui, des del
  prompt no hi havia cap camí per tancar-ne una de fons.

**I el torn té el pany a la mà** (`kernel/the_turn`, fet a l'arrencada amb el de la cua): el pren per a la volta sencera i el deixa anar
**just el temps de l'espera d'esdeveniments** —quan no hi ha res a fer—. La tasca de la shell no executa cap comanda, o sigui que l'únic que
encara toca del torn és **l'eco del camí de bytes** (`feed` escriu la tecla i, si una app és al davant, la dona al seu intèrpret), i per això
el pany continua essent aquí. El resultat és el que l'amo demanava: el prompt i l'eco segueixen vius mentre el panell pinta, i el que es fa de
debò es fa sempre al fil de sistema (docs/30).

**Què no ha canviat, i es diu**: al PC (`pal_host`) no hi ha tasques —`pal::tasks_are_here` contesta que no, el pany és nul i `start_a_task`
diu que no— i el torn serveix la consola ell mateix amb `shell::serve_once(0)`, que és el que la suite i l'emulador esperen (docs/85). Allà la
línia es deixa a la cua i el torn la buida dins del **mateix** torn, o sigui que res no canvia de moment.

**I el que això tanca**: la 0.190.244 deixava `splash`, `panel` i `wipe` corrent a la tasca de la shell amb el pany a la mà. Des de la
0.190.245 **no hi ha cap comanda que s'executi fora del fil de sistema**, o sigui que el que pinten també pinta el torn (docs/30).


### Una sola ploma al filferro, i les línies de la IDF a casa (0.190.247)

L'amo, amb el registre a la mà: *«Les repintades de epdiy es veuen al canal sèrie!! no havíem quedat que tot anava al log circular del kernel?»*.
Dues coses, i cap de les dues era nostra:

- **Les línies `I (761007) epdiy: …` són de la llibreria**, no d'aquest codi: escriuen amb `ESP_LOGx`, i el `vprintf` de la IDF les posa
  **directe al descriptor de la consola**, sense passar ni pel registre circular ni per la regla de què surt pel cable. **El ganxo**
  (`esp_log_set_vprintf` a `pal::init`) les porta a `hal::say_a_line`, que és el mateix pou que les nostres: el registre, el mirall i la cua.
  Una línia de la IDF ja ve feta i no se li posa cap prefix; el que canvia és on va.
- **I quatre tasques escrivint al mateix paper**: la shell, el registre, l'editor de línia i `pal_tx`. El que hi havia era un pany **per tros**
  de cua. Ara hi ha **la ploma del filferro** ([`LunokSerial`](/home/sharek/code/lunokOS/lib/pal_esp32/src/pal_esp32.cpp)): un mutex recursiu
  fet amb l'objecte, pres per l'escriptura **sencera** i deixat anar a la sortida, o sigui que dues línies de tasques diferents no es barregen.
  **I no s'espera amb la ploma a la mà quan la cua és plena** —qui la buida és `pal_tx`, i un pany retingut seria un pany mort—: es deixa anar,
  s'espera un mil·lisegon i es torna a provar. El que queda fora del pany és el que l'editor de línia escriu pel VFS pel seu compte
  (`linenoise`), que només es podria agafar posant-hi un driver propi (docs/30, docs/50).

**I la pàgina de casa ja no la comença l'arrencada**: `kernel::boot` demana `shell::ask_for_the_home_app()`, que escriu `home` —la comanda
que arrenca l'app de `cfg home` **sense targeta**, 0.190.249— a la cua de comandes, i el torn l'executa al primer torn. O sigui que la pàgina del sistema arriba **després** del `boot`, amb el cable viu, i
**al darrere del prompt** (`&`): una persona pot demanar coses mentre el llaçador és al vidre (docs/30, docs/40).

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
  virtual bool virtual_terminal() const;  // si el que hi ha a l'altra banda és una sessió que es pot penjar

  // **El terminal de debò** (0.190.222): qui edita la línia, i si hi és.
  virtual bool has_a_line_editor() const { return false; }   // si algú altre que el nucli llegeix les tecles
  virtual bool speaks_ansi() const { return false; }         // si l'altre cap entén els codis (cursor, color, esborrar)
  virtual void set_the_prompt(const char* text) {}           // el prompt el dibuixa qui edita la línia
  virtual int start_the_terminal(const char* const* names, uint8_t count) { return 0; }  // <0: no n'hi ha
  virtual int take_a_line(char* out, size_t cap) { return -1; }  // 1: línia, 0: cap, <0: paciència
  virtual bool wait_for_a_line(char* out, size_t cap, int32_t timeout_ms) { return false; }  // la mateixa, esperant-la (0.190.244)
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

// **El que creua tasques** (0.190.244): la PAL és l'única capa que sap fer un pany i una tasca a les dues bandes. A la placa un mutex
// recursiu i `xTaskCreate` amb la pila en bytes; al PC un punter nul —que no costa res— i una tasca que contesta que no (docs/85).
using Lock = void*;
Lock make_lock();  void take_lock(Lock);  void give_lock(Lock);
bool tasks_are_here();
bool start_a_task(const char* name, uint32_t stack_bytes, uint8_t priority, void (*entry)(void*), void* argument);
```

La consola de la PAL dona **bytes**, no línies. Qui parteix les línies, qui mira la seqüència de sortida
i qui fa l'eco és el nucli, perquè ho ha de fer igual a totes les bandes. **I aquesta segueix essent la regla**: les quatre portes de
`IConsole` que hi ha a sota (`has_a_line_editor`, `set_the_prompt`, `start_the_terminal`, `take_a_line`) **neixen totes amb resposta de
«no»**, i una banda que hi digui que sí assumeix ella l'edició de la línia: aleshores el nucli **no llegeix bytes** i no fa eco, i el que
li arriba és una línia sencera per `take_a_line` —o per `wait_for_a_line` quan qui la demana és la tasca de la shell i no el torn
(0.190.244)—, que desa al mateix `execute` de sempre. Avui només en diu que sí el cable de la IDF
(`pal_esp32/src/console_line.cpp`, docs/30); l'emulador i la consola del vidre no hi toquen res i es queden amb el camí de bytes (docs/85).

**I `speaks_ansi` és una cinquena pregunta que no és la de l'editor** (0.190.223): diu si el que hi ha a l'altre cap entén els codis
d'escapament, i és el que el nucli pregunta abans d'escriure'n un —el color del prompt i el `clear` que neteja la pantalla—. El terminal del
cable ho contesta amb la sonda; una canonada, un fitxer i el monitor d'un `pio` que no és terminal diuen que no. Les dues preguntes se separen
perquè una banda pot tenir el terminal sense l'editor (el driver del USB que no arrenca) i aleshores el prompt s'ha d'escriure igualment, amb
el seu color (docs/30, docs/85).

## Taula de capacitats

| Capacitat | Lite | Matrix | Paper | Emulador | Notes |
|---|---|---|---|---|---|
| `console` | ✓ | ✓ | ✓ | ✓ | UART a 115200 / stdin-stdout cru |
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

### Els dos ATOM no escalen el rellotge (0.190.227)

L'amo: *«busca el sistema de LightSleep que va dormitant entre torns de les plaques atom lite i matrix. Desactiva el Dynamic Frequency Scaling
per l'M5 Atom Lite i l'M5 Atom Matrix. Només per aquests dos models. Que vagin sempre a 240Mhz»*.

**Són dues coses dins d'una sola crida i se'n toca una**: `Esp32Power::set_cpu_range` diu a `esp_pm` el terra i el sostre de la freqüència
(**el DFS**) i si el xip pot dormir quan el planificador queda sense feina (**el son lleuger**, `light_sleep_enable`). Amb el terra i el sostre
en el mateix número, el son lleuger continua igual i el que desapareix és el viatge entre 80 i 240.

- **Qui ho diu és la placa**: `board::scales_the_clock()` a `board/identity.h` —**fals** al Lite i al Matrix, **cert** a la resta— i
  `IPower::scales_the_clock()` (resposta de sí per defecte) que `Esp32Power` llegeix d'allà. És la mateixa regla de sempre: ningú no escriu un
  `#ifdef` de placa.
- **El nucli pregunta un cop a l'arrencada** (`kernel::power::boot`): si la resposta és no, **no llegeix ni la velocitat ni la política desades**,
  posa el repòs a 240 i apaga la política; i el registre ho diu (`this board does not scale its clock: the cpu is always at 240 MHz, and the
  light sleep is untouched`). **El que hi ha desat no s'esborra** —una targeta NVS pot acabar en una placa que sí que escala—.
- **Les tres portes que canviaven el rellotge ho diuen i no fan res**: `power auto on`/el commutador de Settings (no toca res i ho diu),
  `power cpu <mhz>` (refusa tot el que no sigui 240 i diu per què) i el mask de passos (no s'aplica).
- **I l'emulador ho sap pel model** (`emu::Model::dfs`), o sigui que un ATOM emulat sempre va a 240 i un Paper emulat escala com sempre: és el
  que el cas `test_a_board_without_dfs_runs_at_the_top_and_the_paper_still_scales` guarda (docs/85).

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

Es mesura amb `pio run -e esp32-devkit-idf` cada cop que es toca la memòria: **107 008 B de RAM estàtica de
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
| Rellotge | **PCF85063** (0x51) amb pila de botó, i el del SoC. El full del fabricant en diu PCF85063 i el seu exemple `rtc_pcf8563` en diu PCF8563: és el mateix mapa de registres, i el de la placa afegeix l'any de quatre xifres |
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
més—. **I les portes del son** (0.190.29, i 0.190.102 pel que fa a la tecla): la línia del PCF85063 (GPIO2) i la del tàctil (GPIO3),
armades amb **`ext1` en el mode «qualsevol baixa»**, que és el que aquest xip té i l'ESP32 no —els seus dos modes són «tots baixos»,
que demanaria les dues alhora, i «qualsevol alt», que és l'estat de repòs de les dues línies—. O sigui que aquí una placa adormida
torna **amb un dit a sobre del vidre** o amb l'alarma del rellotge, i `power.caps().wake` ho diu: `timer, clock, glass`.

**I la tecla que torna d'un son profund és el BOOT, que és el que arma el fabricant** (0.190.108, l'amo: *«El botó fisic no desperta de
deepsleep. Revisa els exemples del fabricant»*). El seu `ui_sleep()` acaba amb `esp_sleep_enable_ext0_wakeup(BOARD_BOOT_BTN, 0)` —**GPIO0, nivell
baix**— i porta **comentada** la línia que hauria armat el botó de l'expansor (`// esp_sleep_enable_ext1_wakeup((1UL << KEY_BTN), ...)`),
perquè aquesta línia és `PCA_INT` a **GPIO38** i a l'S3 només els GPIO 0–21 són de l'RTC (`SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP` no hi és): el
botó `S3` **no pot** despertar un son profund en aquesta placa, i per això el fabricant arma el BOOT. Aquest projecte fa el mateix: el BOOT
entra a l'`ext1` amb el rellotge i —si «Wake on touch» ho diu— el vidre. **El preu, i és de maquinari**: despertar és un *reset*, i en un reset
el xip mira els *strapping*; si el BOOT encara és premut quan el xip els llegeix, arrenca en **mode de descàrrega de la ROM** (pantalla quieta,
consola muda). Un toc curt no hi cau; una premuda llarga sí, i és el que li passa també al firmware del fabricant. **I el que no comparteix** (amb el Paper): la bateria, que aquí no és un divisor en un ADC sinó dos
xips que parlen; i el despertar, perquè aquí no hi ha cap latch d'alimentació com el GPIO2 del Paper (`deep_powers_off()` és fals: el
corrent no se'n va, el xip dorm i la cel·la el manté).

**El mapa de pins és el del fabricant** (`docs/pinmap.md` del repositori de la placa) i aquest projecte
no l'inventa: I2C 39/40, panell `CKH` 4 i `D0..D7` 5-8 i 15-18, `STV` 45, `CKV` 48, `STH` 41, `LE` 42,
targeta `CS` 12 `SCK` 14 `MOSI` 13 `MISO` 21, tàctil `INT` 3 `RST` 9, `PCA_INT` 38, `RTC_INT` 2,
`BL_EN` 11.

**I el flaix es llegeix en QIO, com el llegeix el fabricant** (0.190.99): la seva taula de configuració diu *Flash Mode QIO 80MHZ* i el
seu build d'Arduino el porta al paquet `qio_opi`; el nostre entorn d'ESP-IDF naixia en **DIO** —dos bits per rellotge en lloc de quatre—,
perquè PlatformIO força `dio` a la capçalera de les imatges i el símbol del `sdkconfig` era el defecte de la IDF. Amb
`CONFIG_ESPTOOLPY_FLASHMODE_QIO=y` el **bootloader** posa el xip en QIO a l'arrencada (`bootloader_enable_qio_mode`, que només es compila
amb aquest símbol), i des d'allà tot el codi i les dades que arriben del flaix viatgen al doble d'ample. La capçalera de la imatge segueix
dient `dio` —és el que la plataforma escriu i el que fa que la ROM carregui el bootloader sense sorpreses—, i el que mana a partir d'aquell
moment és el que el bootloader configura. A la placa es veu a la seva pròpia línia: `SPI Mode : QIO`.

**I el botó és un, i no és cap pin del xip** (0.190.104, l'amo: *«la S3 de l'expansor és el botó de debò; el BOOT només serveix per al
mode de descàrrega»*). El que una persona prem en aquesta placa penja del **PCA9535** (`IO1_2`, al costat de `POWER_GOOD` i de la
interrupció de la font) i la HAL el llegeix pel bus, o sigui que el sistema en declara **una** tecla i no dues. **I el descriptor diu on
és de veritat** —el mateix vocabulari que el dial del Paper: una cara de la caixa i un lloc dins d'aquella cara—: *«el botó físic de
veritat que té la placa, està físicament a la cara dreta, a 1/3 d'alçada comptant des de baix»*, o sigui `kRight` i el lloc de baix
(0.190.104). Serveix per a dues coses que no són la pintada: una app dona una funció a una posició (`button.list()`) i l'emulador posa la
columna de tecles **a la dreta de la pàgina** en lloc d'una fila a sota, que és el que deia el `kTop` anterior (docs/30, docs/85).

**I el BOOT (GPIO0) no és una tecla del sistema**: és el pin que el xip mira en arrencar per entrar al **mode de descàrrega** —prémer-lo
mentre s'endolla el cable deixa la placa esperant un flaix i no arrencant— i segueix sent una porta de la **becaina**, que no reinicia
res (docs/50, docs/60). El wiki de la placa havia llistat l'`IO48` com a botó per herència d'altres plaques d'aquesta casa; al mapa del
fabricant (`H752-01`) l'`IO48` és **`EP_CKV`**, el rellotge de files del panell —el que aquest projecte genera amb el RMT a tretze
microsegons per fila—, o sigui que un botó allà no hi cap.

**I hi ha un segon botó, i no és cap botó** (0.190.110, l'amo: *«Farem un botó virtual (no s'ha de dibuxar res), per el sistema serà com un botó
físic qualsevol (definexi-lo com a tal) però en realitat serà un toc en aquesta zona del panell tàctil, sota el panell hi ha un cercle que sembla
ser un botó tàctil, i es fals... S'exposarà al sistema com a 2on botó, es al frontal, just sota la pantalla eink, centrat horitzontalment, com un
botó d'iphone. Farà de "back"»*). La caixa té un **cercle** mòuldejat sota el vidre que no és cap botó: prémer-lo és tocar el panell amb la punta
del dit, i el que el sistema en fa és **una tecla de debò** —cara `front`, lloc `centre`, `kSimple`, sola— que es declara com les altres i que
`button.list()` ensenya com qualsevol altra. La zona és la **franja de baix de la vista** (centrada, 120 × 80 px): el cercle és petit i un polze
no, i una zona de la mida del cercle seria un botó que funciona de vegades. **El contacte és del sistema des del primer marc** —la pàgina no en
rep ni el toc ni el gest, i el que hi hagi dibuixat a sota del cercle no s'ha de prémer— i actua **quan el dit s'aixeca**: una premuda curta és
el **camí de tornada** (el mateix que tres pulsacions del dial: enrere una vista, o tancar l'app i tornar a la pàgina que la va demanar), un
dit que camina més de setze píxels no és cap premuda, i una premuda llarga és un `long` que sent l'app (docs/30, docs/45, docs/50).

**I el commutador és a `Settings → Input`** (`cfg.software_button`, `cfg swbutton on|off`, `button.software()` per a qui ho ha de preguntar).
Apagat, aquella franja torna a ser de la pàgina com qualsevol tros de vidre. **Les altres caixes no el tenen**: la Paper té el mateix GT911 i cap
cercle allà, o sigui que la placa ho diu (`a_button_under_the_glass`) i la fila dels ajustos només surt on hi ha el cercle. **I no pot despertar
la placa**: el panell tàctil només hi és quan la placa és desperta, i per això hi ha *Wake on touch* a la pestanya Battery, que és tot el vidre
(docs/30, docs/50, docs/60).

**I les tecles no són per a la pàgina** (0.190.103, l'amo: *«en aquesta placa només tenim un botó físic real, no serveix per el focus. En
aquesta placa no hi ha opció a focus per botons»*). El Paper té un dial de tres tecles que camina el cercle i la del mig prem el control
que el té; aquí hi ha **un botó sol**, el de l'expansor, que no està fet per moure's per una pàgina. O sigui que la placa contesta que no a `keys_are_for_the_page`, i cada pulsació va a l'app com anava abans que hi
hagués un cercle; el cercle es mou amb el dit, i les **tres pulsacions que tanquen l'app** continuen comptant-se perquè es compten abans
de la cua, que és el que impedeix que una app es tanqui la seva pròpia porta (docs/30, docs/90).

**I l'expansor té línia d'interrupció pròpia** (`PCA_INT`, GPIO38), que és el que fa que el botó es pugui preguntar sense preguntar
res: el PCA9535 la baixa quan **qualsevol** de les seves entrades es mou i **la deixa baixa fins que algú llegeix els ports d'entrada**,
o sigui que la línia no és un pols sinó un estat —«l'expansor té novetats»— i es llegeix amb un registre, sense bus pel mig. El driver
només toca l'I²C quan la línia és baixa, amb una lectura de fons cada dos-cents mil·lisegons, perquè un botó que només funciona quan una
interrupció arriba és un botó que deixa de funcionar en silenci.

**I en aquell pin no hi ha cap gestor d'interrupció d'aquest projecte, i no n'hi pot haver** (0.190.99): el `epd_board_v7` d'epdiy ja
n'hi té un d'enganxat —`gpio_isr_handler_add(GPIO_NUM_38, interrupt_handler, …)`, que aixeca un `interrupt_done` que **ningú no llegeix
mai**— i el servei d'interrupcions del xip accepta **un sol gestor per pin**. Tocar-lo seria trencar el panell o no rebre res; i no cal:
el nivell de la línia ja diu tot el que l'ISR hauria de dir.

**I la mateixa línia és una porta del son lleuger** i no del profund: GPIO38 no és una RTC GPIO —les de l'S3 són 0–21—, i el son
profund només escolta aquelles. És el quart lloc d'`arm_wake`, amb el mateix desat i restauració del tipus d'interrupció que les altres
tres, o sigui que prémer `S3` treu la placa d'una becaina com la tecla BOOT (docs/60).

**I la retroil·luminació és una `ILed`** (0.190.99, l'amo: *«tenim interficie per leds a lunokOS, ja saps com fer-ho»*): GPIO11 va a
l'`EN` de l'elevador PT4103B23F, que regula la llum de fons del vidre amb un PWM. La interfície d'`ILed` parla de colors i aquesta placa
té **una llum blanca**, o sigui que el color d'una app es converteix en una intensitat amb el pes de l'ull (0,299 del vermell, 0,587 del
verd i 0,114 del blau): `led.fill(255, 255, 255)` és el màxim, `fill(0, 0, 0)` és apagada, i cap app no ha d'aprendre una segona API per
demanar el mateix. El PWM és d'**un quilohertz**, que és el que el full de l'PT4103 demana —l'app de fàbrica del fabricant la mou amb
`analogWrite`, als mateixos 0–255—, i neix **apagada** perquè així neix la del fabricant: la llum de fons és corrent que ningú no ha
demanat fins que una app o una persona la demana. **I els efectes del sistema no la toquen** (0.190.112, l'amo: *«Al iniciar una placa amb led es fa lo del SOS per errors i la respiració... No
la facis en aquesta placa, al declarar-se backlight no es toca a primeres»*). The breath of the boot and the SOS of a failure are a **language**,
and a light the board declares as a **backlight** has no language to speak with: it illuminates what is painted and nothing else. O sigui que en
aquesta placa no hi ha respir a l'arrencada, ni SOS quan una línia del registre és un error, ni parpelleig de la ràdio, ni light-show de
l'alarma: el que hi ha en encendre's és el **nivell que una persona ha triat** i res més. La porta és una sola —`system_now`, que és qui decideix
si la placa diu alguna cosa— i `note()` i `set_state()` s'hi afegeixen per no gastar ni els fotogrames (docs/30, docs/50).

**I la llum d'aquesta placa és una *backlight*, i ho diu** (0.190.106, l'amo: *«Els leds es poden declarar com a "backlight", així el sistema
pot identificar quin led usar-lo per il·luminar la pantalla. El led d'aquesta placa es un backlight»*). `ILed` té un **paper**: `light` —una
llum que diu coses, que és el que són les del ATOM, el Matrix i el T-Koala i el que val per defecte— i `backlight` —la llum que il·lumina el
vidre i no diu res—. Aquesta placa respon `backlight`, o sigui que el sistema sap que la seva única llum és la del panell, i el que en fa és
**deixar-la al nivell que una persona ha triat** quan no ha de dir res: els efectes del sistema i el color d'una app hi manen mentre duren i
en acabar-s'hi es torna al nivell, no al negre (docs/30, docs/50).

**And that is what the `kCapLed` capability could not say** (0.190.106): before this the board advertised a light and nothing more, o sigui que
the row *Use backlight* of the System tab could not exist —a board with a status LED and a board with a backlight were the same thing— i la
pàgina dels ajustos ensenya el commutador i el desplegable **deshabilitats** quan la placa no en té cap, que és el que l'amo va demanar
(docs/30, docs/45).

**I la llum s'apaga sola quan ningú no mira** (0.190.118, l'amo: *«Afegeix a Settings->System->sota el selector de % de backlight unaltre Toggle
"Dim when idle" igual que el Sleep when idle o el Lock the screen. Aquest en 1 minut que no es toqui l apantalla el backlight s'apaga i es torna a
encendre quan toquen el panell»*). El commutador *Dim when idle* (`cfg.dim_when_idle`, `dimidle` a la NVS, **encès de sèrie**) és a la pestanya
System, sota el nivell, i el que governa és `led::dim_when_idle`, que es pregunta cada torn: amb *Use backlight* encès i un minut de quietud
—`power::quiet_ms()`, el comptador únic d'aquesta casa, el mateix que fa saltar el bloqueig i el son—, el vidre es pinta de negre **sense
tocar el nivell triat**, que és el que torna. Com que el comptador és el de tothom, qualsevol cosa que compti com una persona —un dit, una tecla,
un byte de consola— el desfà; i una pàgina que pinta (un color, un efecte, una nota, la ràdio, un moment del sistema) **mana més que el
commutador**: el dim s'aparta i la llum es queda com la tenia. **El primer toc només encén**: `led::wake_the_light()` diu al tàctil que el negre
era d'aquest mòdul, i aquell contacte es queda en `kNobodys` —ni la pàgina, ni la barra, ni el botó de sota el vidre el senten— perquè el que hi
ha sota un vidre fosc no s'ha vist i prémer a cegues no és prémer. Apagar el commutador amb el vidre fosc el torna a encendre a l'instant, i una
placa sense cap llum declarada com a retroil·luminació l'accepta i no en fa res (docs/30, docs/50).

**I el deure és una recta, i s'hi queda** (0.190.102, l'amo: *«lo del led ha estat una mala idea, deixa'l com abans»*). Entre la
0.190.100 i la 0.190.101 hi va haver una **corba de la percepció** (gamma 2,2 sobre dotze bits, amb el terra de l'elevador i el motor de
*fade* del `ledc` per suavitzar els graons) i el veredicte de la placa és que **no era millor**: feia els mateixos salts i la llum
s'apagava abans, o sigui que es llegeix pitjor una llum que no fa el que el número diu. El que hi ha és el de sempre i és el que es queda:
el color que una app demana convertit en **una intensitat** pel pes de l'ull, i un deure **lineal de vuit bits** a un quilohertz. La lliçó
apuntada: el que es veu a bots en una llum de fons **no és el PWM** sinó els graons del nivell que demana el nucli, i això no es tapa amb
una corba al driver (docs/30, docs/50).


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

**I què és aquesta revisió de la placa, segons el fabricant** (0.190.80, el seu wiki): la fulla del producte diu que
**la versió `H752-01` —la que tenim— afegeix el *refresc parcial* i la *VCOM ajustable***, que són precisament les dues
coses que aquesta fitxa ha hagut de descobrir picant: la finestra d'epdiy (`epd_hl_update_area`) i el `panel vcom <mv>`.
També confirma el que ja hi és: `qio_opi` (flash QIO 80 MHz i PSRAM OPI), USB en mode *Hardware CDC and JTAG*, el nucli i
els events al nucli 1, i el mapa de pins exacte. **Dues coses del wiki no es poden prendre al peu de la lletra**: la
llista de botons diu `RST + BOOT + IO48 + PWR`, i **IO48 és el `CKV` del panell** (el mapa de pins del repositori i el
seu propi `epd_board_v7` ho diuen), o sigui que és una llista heretada de la revisió anterior; i l'esquema de particions
de l'IDE és de 3 MB d'app i 9,9 MB de FAT, mentre que aquest projecte en fa 4 i 11,9 de LittleFS a propòsit (docs/70).

**I el voltant de la pintada, que és el que faltava copiar** (0.190.60). El panell que inicialitzen —`epd_board_v7`,
`ED047TC1`, `EPD_LUT_64K`, la forma d'ona de sèrie, i `EPD_ROT_INVERTED_PORTRAIT` com a gir— és exactament el que ja hi
ha, i el gir és el mateix mapa de pàgina que fa aquest fitxer. El que faltava és el que fan **al voltant**:

- el seu `screen_update()` de fàbrica és `epd_poweron()` → `epd_hl_update_screen(&hl, MODE_GL16, epd_ambient_temperature())`
  → `epd_poweroff()`: **les tensions no es queden enceses** entre dues hores;
- l'exemple dels setze grisos fa **`epd_clear()` abans** de `MODE_GC16`, i és el que posa el paper blanc que la taula suposa;
- la **VCOM** és 1560 mV a la seva NVS i **ajustable des del seu menú de fàbrica** (0 a 5,1 V, passos de 10 mV): és un
  número per unitat, i per això `panel vcom <mv>` l'exposa.

**I el rentat del vidre sencer, fora** (0.190.82). La 0.190.60 va copiar el `epd_clear()` de l'exemple de setze
grisos sense mirar què fa: neteja **tot el vidre**, i després es repintava només la finestra marcada — o sigui que cada
pintada que netejava es pagava com una pantalla sencera (vint-i-cinc passades) i deixava blanc tot el que quedava fora.
L'amo ho va veure de seguida: *«el redibuixat al T5S3 és un desastre, tota pantalla sencera, lentíssim»*. **I no calia**:
el `MODE_GC16` d'aquest panell és una matriu de transició **16 × 16** —cada nivell té codi per a cada nivell de sortida,
comprovat als 38 fotogrames de `epd_wp_ED047TC2_2_9_data`—, o sigui que sap portar un píxel de qualsevol gris a
qualsevol altre. El rentat només fa falta als modes que compten des de paper blanc, que aquest sistema no demana.

**I la targeta, que fallava un arrencada sí i una no** (0.190.83). El símptoma era exactament aquest: unes vegades
`[SD] /sd mounted, 30436 MB` i unes altres `sdmmc_io_reset: unexpected return: 0x104` amb
`sdmmc_card_init failed`. La causa és al **seu propi exemple** (`sd_card`, en un comentari): *«LORA、SD use the
same spi, in order to avoid mutual influence; before powering on, all CS signals should be pulled high and in an
unselected state»*. Nosaltres no tocàvem mai el **CS del LoRa (GPIO46)**: flotant, el SX1262 contesta per MISO i
les ordres de la targeta tornen espatllades. Ara la placa declara els xips que comparteixen el bus
(`kSdQuietPins = {46, 1}`, CS alt i reset baix) i el muntatge els posa així abans de la primera transferència. **I
reintenta**: 10 MHz → 5 MHz → 400 kHz, tres intents, perquè una targeta que respon a la prova i llavors es rendeix
és una targeta massa apressada i no un disc trencat.

**Què costa una pintada, mesurat al vidre amb IDF** (0.190.94, l'amo: *«vull que les pintades a pantalla siguin les mínimes
necessàries i a la qualitat adequada per cada zona a pintar»*). El número que decideix tot el que ve després és aquest:

| Fet | Número | D'on surt |
|---|---|---|
| Una fila del vidre | **13 µs** | `line width: 13us, 260 cycles` de l'arrencada; 248 píxels a 20 MHz |
| Una passada sencera (540 files) | **7,0 ms** | 540 × 13 µs |
| `gc16` (neteja, setze grisos) | **38 passades → 442 ms** | els fotogrames de la TC2 a 18–21 °C |
| `gl16`/`gld16` (neteja suau) | **46 passades → 415 ms** | els mateixos |
| `du`/`a2` (dues tintes) | **2 passades → 110 ms** | els mateixos |

**I la conseqüència, que capgira una regla vella**: el panell es pinta **fotograma a fotograma i cada fotograma és sempre el vidre
sencer** —les files que no canvien s'envien en blanc, no s'estalvien—, o sigui que **una pintada costa els fotogrames de la tinta i no
la superfície**. Una tira de 33×273 amb `gld16` costa el mateix (427 ms) que el vidre sencer amb `gld16`; el que estalvia és **triar una
tinta de pocs fotogrames** i **no fer dues passades on en cap una**. D'aquí surten les decisions d'aquesta tongada: les caixes de la franja —el
rellotge i el títol— van amb `a2` i es netegen cada deu canvis (427 → ~160 ms de mitjana, amb el fantasma acotat a nou), i ajuntar dues
caixes que no es toquen **no costa res de temps** si són la mateixa tinta. El que sí que escala amb el rellotge és la fila: per això el pas de 10 a
20 MHz va baixar les pintades a la meitat (818 → 478 ms) i els fotogrames no s'hi poden tocar (docs/50, docs/95).

**I el rellotge del vidre, que anava a un quart** (0.190.84). El log de la placa ho deia i no ho vaig veure:
`Reducing the pixel clock from 20 MHz to 10 MHz` i tot seguit `from 10 MHz to 5 MHz`, en la mateixa arrencada. No és el
panell: `epd_lcd_init` de la llibreria crida **dues vegades** `check_cache_configuration()` —una des
d'`assign_lcd_parameters_from_config` i una altra ella mateixa— i aquesta funció **divideix el rellotge per dos** cada
cop, perquè l'Arduino d'aquest projecte munta la memòria cau de dades en línies de 32 bytes i epdiy les vol de 64. Amb la
crida duplicada, cada fila costava quatre vegades el que tocava: una finestra de 48 × 540 es pagava en **1661 ms**. La
duplicada s'ha tret (canvi local marcat dins de `lib/epdiy`), i `panel bus <mhz>` mou el rellotge en calent perquè sigui la
placa qui digui si aguanta els vint.

**I la velocitat de la CPU, tota per l'escala** (0.190.85). L'amo va apagar `Automatic scaling` i va veure igualment
`cpu 240 MHz from 80 (the glyphs of a page)`. Tres forats:

- **`hold_cpu` no preguntava res**: era l'única porta de la velocitat fora de la política, i per ella hi passaven
  l'arrencada (`kernel::boot`, a 240) i l'inici d'una app (`shell`, a 240). Amb l'escala apagada, la CPU pujava igual i
  el log deia la raó d'un camí que havia de callar. Ara una empenta no mou res si l'escala està apagada.
- **El commutador i el pas no es desaven**: `Automatic scaling` i `Speed:` vivien només a la RAM, o sigui que al primer
  reinici la placa tornava a l'escala automàtica i a 240. Ara es desen a la NVS (`cpuauto`, `cpuspeed`) i l'arrencada els
  respecta.
- **I amb l'escala apagada s'hi ha de baixar**: el rellotge d'un ESP32-S3 arrenca a 240, o sigui que no n'hi ha prou de
  deixar de pujar-lo; `power::boot` baixa al pas triat.

**I el FAT anava sense noms llargs** (0.190.96, el `errno 22` en tancar una app). La targeta es muntava —`/sd mounted, 30436 MB`— i al
desar les sessions deia:

```
[W][SD] cannot open /sd/session/home_eink/state for w: errno 22 (5 file(s) at once is the cap of this mount)
```

El número no era la capsa de cinc fitxers (aquest és només el context de la línia): **`EINVAL` és `FR_INVALID_NAME`** a la taula d'errors
de la FAT de la IDF, i `home_eink` té **nou** caràcters en un sistema de noms 8.3. La diferència era de configuració i no de codi: el
nucli d'Arduino porta `CONFIG_FATFS_LFN_STACK=y` i el nostre `sdkconfig` neix amb `CONFIG_FATFS_LFN_NONE=y`, o sigui que **la targeta es
muntava i cap fitxer amb nom llarg s'hi podia crear** —les sessions de les apps, i qualsevol ruta d'`/apps` o `/data` amb un nom de més
de vuit caràcters—. Ara hi és amb la pila, com el fabricant, i el sostre és el mateix de 255 caràcters (docs/50, docs/70).

**El verí del heap, encès una estona i apagat** (0.190.95). El primer flaix d'aquest entorn va fer un `StoreProhibited` a l'adreça `0xb`
dins del `tlsf` de la PSRAM (`remove_free_block`), o sigui que algú havia escrit fora del seu bloc i la llista de lliures havia quedat
trencada; el xoc va arribar amb `run home_eink &` i no s'ha tornat a veure mai més. Es va encendre `CONFIG_HEAP_POISONING_COMPREHENSIVE`
per caçar-lo —atura el codi **en l'escriptura** i diu el culpable— i no va cridar ni una vegada. **El seu preu és el que el fa de
diagnòstic i no de vida**: vuit bytes per bloc (`0xABBA1234` al davant i `0xBAAD5678` al darrere) i un `memset` del bloc sencer a cada
`malloc` (`0xCE`) i a cada `free` (`0xFE`), que en aquest sistema són blocs de 32 KB —les sessions de les apps— i de 246 KB —el canvas—.
Apagat; si el xoc torna, s'encén una estona i es reprodueix amb ell posat (docs/50, docs/75).

**I un build d'ESP-IDF per tenir el `sdkconfig`** (0.190.90, l'amo: *«podriem portar el projecte a esp_idf i així compilar com ens
surti dels collons les coses?»*). Sí, i és menys del que semblava: **vuit fitxers** tocaven el nucli d'Arduino i el que s'hi usava eren
tres coses —`Serial`, `ESP.restart()` i `delay()`—, perquè epdiy, els HAL, la wifi, Lua, el JSON, l'inflate i l'EPUB ja eren IDF o C
portable. L'entorn `lilygo-t5s3-idf` (docs/80) construeix la mateixa placa amb **IDF 5.1.2 de debò**, i el que calgué fou això:

| Peça | Què hi havia | Què hi ha |
|---|---|---|
| L'entrada | `setup()`/`loop()` del nucli | `app_main` a `src/main_idf.cpp`: el corrent de la placa, la PAL, el nucli i el torn per sempre |
| La consola | `Serial` (USB CDC del nucli) | les cinc respostes que el PAL demana sobre el **USB-Serial-JTAG** del xip, o la UART0 a les altres plaques; sense Bluetooth el log d'IDF calla i l'anell del sistema és l'únic que parla |
| La NVS | `Preferences` (embolcall d'Arduino) | `nvs_*` directe, que és el que l'embolcall fa per dins; i **el cercador té dues formes** —la 4.4 torna iterador, la 5.1 torna `esp_err_t` i el vol per paràmetre— |
| El disc | l'objecte `LittleFS` del nucli | `esp_vfs_littlefs_register` amb `format_if_mount_failed = 0` (la regla de no formatar mai no es toca) i POSIX per a la resta; un `LunokFile` de quatre crides fa la feina del `File` |
| La CPU | `setCpuFrequencyMhz` del nucli | `rtc_clk_cpu_freq_*`, que és exactament el que el nucli fa servir per dins |
| mDNS i BLE | sempre | `__has_include` i `CONFIG_BT_ENABLED`: sense ells la provisió va per **SoftAP** i ho diu |
| L'asm d'epdiy | `lut.S` | `lut_asm.S`: el builder d'espidf donava dos objectes al mateix nom |

**I des de la 0.190.176 aquestes dues portes no hi són** (l'amo: *«treu tot el codi arduino que puguis si te equivalencia
implementada en esp_idf»*): les vint-i-nou condicions `#if defined(ARDUINO)` de dotze fitxers, el `src/main.cpp` de `setup()`/`loop()`
i la clau `"arduino"` de les tres fitxes de `boards/` van marxar, i cada branca tenia la seva bessona d'IDF que és la que ja es
compilava. **I se'n va anar un parany**: la branca Arduino de `lunok_serial_begin` es cridava a si mateixa, una recursió infinita que
hauria petat el dia que es compilés. El que **no** s'ha tocat és el guard `ESP_PLATFORM` de `src/main_idf.cpp`: no és codi d'Arduino,
és el que evita compilar l'entrada de la IDF a l'emulador (docs/80, docs/85).

**El que això compra**, mesurat: RAM **100 128 B contra 132 320 B** (30,6 % contra 40,4 %), flaix **1 863 941 B contra 2 295 113 B**
(44,4 % contra 54,7 %), el disc idèntic (149 fitxers, 24 apps) i **`CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE 64`** al
`config/sdkconfig.h` del build, que és el número que epdiy mira abans de decidir el rellotge del vidre: amb el nucli d'Arduino val 32
i **no es pot canviar** —les seves lliberries venen compilades amb aquell número a dins—, i per això el vidre anava a 10 MHz. **El que
no està provat és el maquinari**: el primer flaix ha de dir `epdiy 2.0.0, 960x540 of glass at 20 MHz` sense cap `Reducing the pixel
clock`, i si el vidre surt amb una fila desplaçada un byte o la vora dreta d'una línia estirada, aquesta placa es queda a 10 MHz amb la
comprovació de la lliberry (docs/80, docs/95).

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

### El GT911 i l'energia: què té el xip, i què s'hi fa (0.190.105)

L'amo va demanar *«Estalvía el maxim d'energia possible al panell... Manté el panell en un consum baix fins a detectar un dit, llavors
pujes la poténcia del panell durant uns segons i la tornes a deixar en LOW_LEVEL_QUERY»*, i la resposta surt del driver del fabricant
(`TouchDrvGT911.hpp` de `SensorLib`), que és l'única cosa que el xip té de debò:

| El que el xip té | On | Què fa |
|---|---|---|
| **Quatre modes de la línia** | `0x804D`, bits 1:0 | `0x0` vora de pujada, `0x1` vora de baixada, **`0x2` nivell baix**, `0x3` nivell alt. El de nivell baix (*«Keep high level when idle, and switch to low level when touched»*) és el que el seu exemple en diu el de menys consum: la línia es queda baixa mentre hi ha un dit i el host no ha de preguntar res |
| **El ritme d'escombrat** | `0x8056` | `5 + (byte & 0x0F)` hertzs, o sigui **de cinc a vint**: és l'única palanca que canvia el que el xip gasta escombrant |
| **Dormir i despertar** | `0x8040 = 0x05` | el comandament de son del xip; el despertar és un pols de vuit mil·lisegons a la línia (`sleep()`/`wakeup()` del fabricant) |
| **Quants dits** | `0x804C` | aquesta unitat en declara dos (mesurat a la Paper, 1/11) |
| **El botó de casa** | llista de punts | el fabricant té una crida per al «botó de casa» del panell; aquí no es fa servir perquè **un gest és una forma del flux de punts i no una cosa que el xip inventa** (docs/50, docs/85) |
| **Gestos propis** | — | el xip en té i **ni el driver de M5 ni el d'Espressif els llegeixen**; aquest tampoc (el mateix motiu) |

**I el que aquest projecte hi fa, que és el que ell demanava**: el xip neix i es queda en **`LOW_LEVEL_QUERY` i deu escombrats per
segon** (`kGt911QuietHz`); quan un marc porta un dit, el ritme puja **al que la unitat porta de fàbrica** (vint en aquesta) i s'hi queda
**cinc segons** després de l'últim dit (`kGt911HoldFastMs`), i quan el vidre calla torna al terra. El canvi de règim costa una lectura de
186 bytes i tres escriptures —la configuració, el `checksum` a `0x80FF` i el «fresh» a `0x8100`, que és la seqüència del
`reloadConfig` del fabricant— i per això es fa quan canvia el règim i no a cada marc. **El deu i no el cinc** perquè el primer toc d'un
vidre adormit espera un escombrat: a cinc hertzs són dos-cents mil·lisegons abans que el xip noti el dit, i cent és el que una pàgina que
triga mig segon a pintar pot amagar (docs/50, docs/95).

**I el règim es pot desactivar des de la pestanya Input** (0.190.107, l'amo: *«afegeix a Settings->Input un toggle que posi "Adaptative touch
scan", aquesta opciò activa o desactiva el comportament del panell de baixar la sensibilitat i pujar-la quan hi ha dit. Deixant el panell a
máxima velocitat mentre estigui el dispositiu despert. En cas de lightsleep o deepsleep cada mode el configurarà com necessiti (low/off)»*).
El commutador *Adaptative touch scan* (`cfg.touch_adaptive`, `touchscan` a la NVS) encès és el que hi ha des de la 0.190.105; apagat, el
controlador **es queda al ritme de la unitat** mentre la placa és desperta i només baixa quan ha de dormir. **I qui mana en el son és el son**,
no el commutador: un son profund que escolta un dit posa el vidre al terra (`quiet_now`) abans d'armar la línia —perquè el que ha de passar és
que caigui, no que caigui amb precisió—, i un que no l'escolta **l'adorm del tot**; en tornar, el règim de la persona es torna a posar
(docs/30, docs/50, docs/60).

**I el panell va al ritme de la llum** (0.190.119, l'amo: *«El touch va per interrupció oi? perqué triga tant en respondre? sembla polling»*). La
línia del GT911 **sí que és una interrupció** i l'ISR només aixeca un flag (el bus no es toca des d'una interrupció), però el que feia l'efecte de
*polling* era el xip: aquesta unitat escombra el vidre a **deu hertzs** —la seva pròpia configuració ho deia a l'arrencada, `10 Hz quiet, 10 Hz
awake`— i el terra que el driver hi posava era el mateix número, o sigui que el *burst* de cinc segons després d'un dit **no tenia res a apujar**:
un marc cada cent mil·lisegons i un toc que necessita **dos** (la baixada i l'aixecament) són dos-cents mil·lisegons abans que una pàgina en sàpiga
res. Ara `Gt911::set_brisk` posa el xip a **vint hertzs** mentre la llum del vidre és encesa (`led::glass_is_lit`: retroil·luminació, commutador
encès, nivell diferent de zero i el dim no l'ha apagada) i el torna al terra quan la llum s'apaga; el tàctil el pregunta a cada torn i el driver
només escriu al xip quan el règim canvia. **I la vora desvetlla el nucli**: l'ISR hi empenta un `pal::kEventTouch` amb
`IQueue::push_from_isr` (`xQueueSendFromISR` a l'ESP32), de manera que el torn no espera els seus deu mil·lisegons —ni els quaranta d'una
becaina— per mirar el vidre; el marc es continua llegint del torn i no de la interrupció (docs/30, docs/50, docs/95).

**I la interrupció deia ser a la IRAM i no ho era** (0.190.206, l'amo, després del segon `Interrupt wdt timeout`: *«No toquis la interrupció!
troba perqué falla»*). `on_the_line` era `IRAM_ATTR` amb aquest comentari al costat —*«una interrupció pot arribar mentre es llegeix la flash»*— i
**tres de les seves quatre crides eren a la flash**: l'accessor `pal::system()` (0x4010526c), `queue()` (0x4022cd80) i el `push_from_isr` virtual
amb el seu thunk (0x40104a0c i 0x40104a70). Amb la memòria cau de flash apagada —una pàgina de la sessió, una font, una pintada— una crida a la
flash **no espera**: executa el que hi hagi, i el xip se'n va a un bucle dins d'una interrupció que el watchdog d'interrupcions mata. La finestra és
petita (entre `vTaskSuspendAll` i `esp_intr_noniram_disable`, `spi_flash/cache_utils.c`) però un dit que hi caigui la troba, i és per això que el
crac era reproduïble tocant el vidre i no pas constant.

**El que s'ha fet no és treure l'empenta, és fer-la certa**: `pal::wake_the_kernel_from_isr()` és una funció lliure —cap taula de mètodes virtuals
pel camí— que crida un mètode **no virtual** de `EspSystem` (`wake_an_isr_here`, també `IRAM_ATTR`) i que empenta `kEventTouch` a la cua; al darrere
només hi queden `xQueueSendFromISR`, `xTaskGetSchedulerState` i `_frxt_setup_switch`, que aquesta IDF ja té a la IRAM, i `memset`, que és a la ROM.
`push_from_isr` continua existint per a la plataforma de l'emulador i per a qui no és una interrupció, però l'ISR ja no la crida. I la línia aixeca el
flag i empenta **un cop per fotograma**: el xip la baixa un cop per escombrat i el fotograma el llegeix el torn, o sigui que una segona vora abans que
el torn llegeixi la primera no té res a dir. La interrupció és de nivell 1 compartida (`gpio_install_isr_service(0)` → `ESP_INTR_FLAG_SHARED` →
`LEVEL1`), que és la condició perquè `portYIELD_FROM_ISR` sigui legal; i el relleu es demana només si el planificador el pot prendre, perquè la
finestra de l'escriptura el té suspès. **Comprovat al binari de la Paper** amb `nm`/`objdump`: `on_the_line` 0x40088ed0 → `wake_the_kernel_from_isr`
0x400817d8 → `wake_an_isr_here` 0x40081760 → `xQueueGenericSendFromISR` 0x40097244 / `xTaskGetSchedulerState` 0x40098938 / `_frxt_setup_switch`
0x400997fc, tots dins de la IRAM (0x40080404–0x4009e77c), i `memset` 0x4000c44c a la ROM (docs/50, docs/95).

**I el que costa una pintada es pregunta a la placa, amb `panel bench`** (0.190.123, l'amo: *«Mes cosetes per la pantalla eink! Vull que facis possible això
dels 60hz»*). La forma d'ona d'aquest panell —la TC2 que el build porta compilada— té **vint-i-dues fases pel `du`** i **trenta-vuit pel `gc16`** a la
temperatura ambient, i una fase costa uns dotze mil·lisegons i mig a pantalla sencera: o sigui que el mode més ràpid que aquest vidre té és ~275 ms i
no hi ha cap paràmetre que el baixi, perquè el que es mou és la tinta. Els 60 fps de l'exemple del fabricant (`epd_60fps_probe`) **no són aquest camí**:
és un driver de vídeo propi, I80 + DMA, que **salta la LUT** i envia dos bits per píxel amb comptadors de polsos per píxel (el seu propi README diu
que és per a experiments de refresc i ghosting). Per deixar de discutir-ho de memòria, `panel bench` pinta tres àrees (el vidre sencer —960×540, i no
la pàgina de 540×960, que va ser el primer error— una franja de quaranta-vuit files i una cel·la de 120×80) amb les tres qualitats, descarta la primera
pintada de cada combinació i dona la mitjana de tres amb el sostre d'imatges per segon. **La pintada de mesura ha de tenir un canvi de debò**:
`ISurface::paint_for_the_count` fa el mateix camí que una pintada —tensions amunt, forma d'ona, tensions avall— però **gira l'àrea del framebuffer**
abans i la torna a girar a la crida següent, perquè la primera versió mesurava una àrea que ja estava pintada i epdiy contestava **1 ms** (el `diff`, no
cap pintada: l'amo ho va veure amb mil imatges per segon en una franja). El nombre de crides per combinació és **parell**, o sigui que la imatge acaba on
era (docs/50, docs/95).

**I el número que va canviar el que fa la UI és que l'àrea no compta** (0.190.141): la mateixa sessió de bench, amb el bus a vint-i-quatre i les
tensions calentes, dona **448 ms** per a la pàgina, **456** per a la franja de quaranta-vuit files i **456** per a la cel·la de 120 × 80 amb `gl16`
—i 383, 383 i 393 amb `gc16`—. És a dir que pintar una cel·la costa el mateix que pintar el vidre sencer, perquè epdiy envia les 540 files sempre i les
que no han canviat hi van en blanc. O sigui que **el que una UI estalvia en aquest vidre és el nombre de pintades i no la mida de les caixes**, i és el
que fa l'ajuntament del damage del torn: vegeu *el control de danys* (docs/50, docs/95).

**I les vuit taules d'epdiy són triables pel nom** (0.190.135, l'amo: *«podries afegir tots els waveforms? els vull provar»*). `builtin_waveforms.c`
de la llibreria inclou **cada taula que porta** —les dues del nostre panell (`ed047tc1` i `ed047tc2`) i les sis dels seus altres vidres (`ed060sc4`,
`ed060sct`, `ed060xc3`, `ed097oc4`, `ed097tc2`, `ed133ut2`)—, o sigui que totes són al binari i el que faltava era el nom per triar-ne una. `panel wave`
les llista amb un asterisc a la que hi ha, i `panel wave <nom>` canvia el punter de l'estat. **Les sis que no són d'aquest panell no hi són per
qualitat**: són de vidres de sis, nou i tretze polzades i el que faran és pintar amb la forma d'ona d'una altra tinta —es proven per entendre els
grisos, no per quedar-s'hi—, i una taula que no tingui el mode que es demana ho diu a la línia de la pintada (`with an error`), no es penja (docs/50, docs/95).

**I la forma d'ona es pot canviar en calent, i aquesta placa en té dues** (0.190.134, l'amo: *«pots canviar `epdiy_ED047TC2.h` i si provem la
C1? Els colors a la C2 no semblen correctes del tot... els 2,4 i 8 colors es veuen bé, pero els 16 no tant»*). El build porta compilades les
dues taules del panell, i el que decideix quina es fa servir és **un punter de l'estat d'epdiy** (`state->waveform`), o sigui que
`panel wave tc1|tc2` les canvia **sense reiniciar** i la pintada següent consulta la nova. I no són la mateixa taula: la **C2** té **set
rangs de temperatura** (3 °C cadascun, de 0 a 48) i **tres modes** —`du`, `gc16` i `gl16`—, mentre que la **C1** té **un sol rang (20-30 °C)** i
**cinc modes**: els tres d'abans més `MODE_EPDIY_WHITE_TO_GL16` (0x10) i `MODE_EPDIY_BLACK_TO_GL16` (0x11), que són els comptes que arrenquen
d'un paper blanc o d'un paper negre i que l'altra taula no porta. **El mode de setze grisos de la C1 és el que el nostre driver vell anomenava
així** («quinze empentes en què cada nivell rep cap al negre mentre li calgui, i només per a un píxel que ve de blanc»); si els grisos de la C2
no surten, el candidat és aquest, i demana una neteja a blanc abans de la matriu (docs/50, docs/95).

**I el frame no es pot tallar amb el pipeline d'epdiy** (0.190.126, l'amo va provar `panel bands on` i la placa va quedar penjada per *task watchdog*). La
idea era bona i el driver d'aquesta casa la feia abans de l'epdiy —acabar el frame a l'última fila bruta, que és el que fa que una franja de quaranta-vuit
files costi quaranta-vuit files i no cinc-centes quaranta—, però el render de l'epdiy no és un bucle de files: són **dues tasques de preparació** que
reparteixen les línies en dues cues de trenta-dues, un ISR que les consumeix i un DMA que les demana, i el frame s'inicia quan la línia **seixanta-tres**
ja està preparada perquè el DMA tingui reserva. Amb un frame de noranta-sis files, el marge que queda després del disparador és de trenta-tres, el ISR
es queda sense cua (`EPD_DRAW_EMPTY_LINE_QUEUE`, 0x400 → `line buffer underrun occurred!`) i el `epd_prep` no torna mai: el watchdog de tasca mata la
placa. **El commutador s'ha tret del nucli** en lloc de deixar-lo penjat allà; si s'hi torna, és amb el disparador escalat i amb la placa a mà (docs/50,
docs/95).

**I el negre del dim passa pel comptable del que s'ha pintat** (0.190.122, l'amo: *«No veig que el backlight es recuperi al tornar a tocar la pantalla...
triga molta estona en encendre per algún motiu? surt al log informació sobre l'apagada i l'encesa del backlight?»*). L'apagada pintava el negre **pel
darrere** —`fill` i `show` a pèl— i deixava `g_shown` dient el nivell d'abans, o sigui que en tornar el dit `paint` veia el mateix color que creia que hi
havia, no veia res a fer i **no encenia res**; la llum tornava quan una altra cosa del sistema posava el flamenc de brut, i això és el «triga molta
estona». Ara el negre és un `paint(0, 0, 0)`, que apunta el que ha pintat, i les tres transicions es diuen al registre com a **marques** (no les filtra
el nivell): `[I][LED] the glass's light is out: 60 s with nobody on the board`, `… is back at 42: a finger on the glass` i `… is back at 42: the board
is in use again` (aquesta última quan qui la desfà és una tecla o un byte de consola, que també mouen la quietud) (docs/30, docs/50, docs/75).

**I el dit que ha demanat el son no és un dit que desperti la placa** (0.190.121, l'amo: *«Al activar l' sleep amb el táctil connectat (settings->battery),
el dispositiu es desperta de seguida. Quelcom similar passava amb l'm5paper recordes?»* — i sí: era el mateix, i a la Paper ja estava resolt des de la
0.190.16). El son profund d'aquesta placa s'arma amb `ext1` i **`ANY_LOW`**: la pregunta no és «hi ha un dit» sinó «**alguna d'aquestes línies és baixa
ara mateix**», i el dit que acaba de prémer el commutador —o el marc que el controlador encara no ha lliurat, o la línia que el `LOW_LEVEL_QUERY` deixa
baixa mentre el contacte dura— fan que el xip torni abans d'haver-se adormit, una vegada i una altra. El que lleva la línia és **llegir el marc i
tornar-li l'estat** (`flush`, el `0x814E` a zero de M5Unified #91) i el que diu si ja és alta és el pin, o sigui que l'armat és un **bucle**: alliberar,
mirar, i si el dit encara hi és esperar deu mil·lisegons més, amb el mateix topall d'un segon que la Paper (`kTouchReleaseMostMs`). Si no puja, el
tàctil **no és una porta d'aquest son** i el log ho diu: val més tornar pel rellotge i per la tecla que no dormir gens. I com que `ANY_LOW` val per
a les tres línies, les altres dues també es miren abans d'armar: un rellotge que encara no ha deixat anar la seva interrupció i un BOOT que ja és
premut són portes obertes i es diuen amb el seu pin (docs/50, docs/60, docs/95).

**I el commutador `Wake on touch` de la pestanya Battery té feina en aquesta placa** (0.190.105). Encès vol dir que el controlador es
queda despert, en el seu règim de menys consum, durant tot el son profund, i que la seva línia (GPIO3) és una porta: **un dit al vidre
torna la placa**. Apagat vol dir que **el controlador s'adorm amb el xip** (`writeCommand(0x05)`) i que la placa només torna pel rellotge;
sense alarma a la cua, doncs, no hi ha camí de tornada i el sistema ni tan sols ofereix la baixada (`can_deep_sleep()`). I la línia es
neteja abans d'armar-la: un controlador amb un marc sense prendre la té baixa, i un son armat en «aquesta línia baixa» tornaria de
seguida, una vegada i una altra (docs/30, docs/60).

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

**I el cercle té un llindar** (0.190.89, l'amo: *«Afegeix "Log level" a Settings->System Amb els nivells de log, per defecte Debug (com la
comanda log)»*). `debug`, `info`, `warn` o `error`, i **el filtre és a la porta única del registre** —la funció que rep la línia abans del
cercle—, o sigui que el mira el mateix lloc per a un conductor, per a `log_printf` i per a una frase: el nivell és el segon byte de la
línia (`[D]`, `[I]`, `[W]`, `[E]`), que és la forma que el format dóna a tot el que escriu aquest sistema, i una línia que no té aquesta
forma és una frase de `log_say` i passa sempre, perquè una frase és una resposta a una persona i no una traça. **El de sèrie és `debug`**,
que és el que el sistema ha fet sempre; el llindar es desa (`cfg loglevel`, NVS) i s'aplica a l'arrencada, i la línia del boot el diu al
costat del teclat (`… keyboard qwerty, log debug, boot boot …`), perquè una placa amb el registre callat no s'ha de veure igual que una
sense res a dir (docs/30, docs/60).

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
| `IBluetooth` | encendre la pila, per qualsevol de les dues portes (`start`, `host_on`) | `radio.up` |
| | un escaneig | `radio.scan` |
| | lligar-se a un aparell (`pair`, i `reconnect` d'un de conegut) | `radio.pair` |
| | una tecla enviada (`tap`) | `radio.tap` |
| **El nucli** | obrir una app: l'intèrpret i el primer torn | `app.start` |

**El que no es compta, i per què**: `count()`, `width()`, `height()`, `caps()`, `rotation()`,
`max_value()`, `face`/`spot`/`kind`/`group`, `last_error()`, `wifi_state()` i `prov_provisioned()` són
camps en memòria del conductor o de la placa, no viatges al maquinari: comptar-los ompliria la taula de
files que sempre costen zero i amagaria les que no. Al ràdio li passa igual amb `state_word()`, `name()`,
`host_connected()` i `host_is_up()`, que són preguntes a la memòria de la pila.

**I una fila que només hi és si la placa ho pot fer** (0.190.199): `radio.up` no surt en una placa sense el Bluetooth al build, i
`radio.scan`/`radio.pair` no surten en una que no faci d'amfitrió. Un zero que vol dir «aquesta placa no ho té» i un zero que vol dir
«no s'ha fet mai» no es poden escriure igual, i `available()` és la porta que els separa.

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
  **i només si tots els forats són la mateixa tinta** (0.190.61, trobat per la suite el 20/11): ajuntar-los pren la qualitat
  més forta, o sigui que una foto que demana `du4` i la línia de text que demana `gc16` haurien pujat juntes amb el mode del
  text i la foto hauria perdut el seu. Dues qualitats són dos pintats, i és el que `test_two_qualities_are_two_paints` diu;
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

**El torn es paga una vegada, i no una vegada per regió** (0.190.141 i 0.190.142, l'amo: *«tot el que s'ha après, s'ha
d'aplicar al interficie UI, dibuixar més eficientment els controls»* i, el dia següent, *«device_lock pinta un per un¿? No
entenc perqué... altres apps fan igual, pinten un per un»*). El control de danys de dalt suposa que **pintar menys píxels
costa menys**, i a la pràctica no és així enlloc d'aquesta casa: el preu d'una pintada és **la tinta del mode**, una espera
de 120 ms (`du`) a 450 ms (`gc16`) que es paga sencera encara que la caixa sigui de tres píxels, i al panell del T5S3 ni
tan sols mira l'àrea, perquè el render d'epdiy envia les 540 files sempre (una franja de 48 files costa els mateixos
**390 ms** que la pàgina sencera; mesurat, docs/95). O sigui que sis regions del damage no són sis sisenes d'una pintada:
són **sis pintades**.

Mesurat a l'emulador amb `--run device_lock`, un sol `ui.present('gl16')` en feia sis —les dues frases, els tres punts i el
candau—, perquè `paint_root` pinta **una regió per regió** i les que no es toquen no s'ajunten mai. Ara, abans del bucle,
**el damage del torn s'ajunta en una sola regió** amb el mode **més fort** de les que hi entren: el net pinta el resultat
final de la ràpida i més, i sis `gl16` es pinten `gl16` i no `gc16` (a l'emulador, 483 ms contra 642). La pàgina del pany
passa de ~3 000 ms a 801. Dues coses no s'ajunten:

- **quan la unió faria escalar una ràpida a `gc16`** (set vuitenes parts del vidre, el llindar de sempre) **i hi ha menys de
  quatre regions** (`kJoinFrom`): tres `du` de 128 ms són menys que un `gc16` de 414, i ajuntar-les seria perdre;
- **quan hi ha una `a2`**, que és el mode del motor de vídeo i una altra manera d'escriure el vidre.

I dues coses més que van sortir dels tests i no del cap:

- **una unió que passa de vuit vegades l'àrea del que ha canviat no es fa** (0.190.143). Un dit a la cel·la de la vida i el comptador de
  l'altra punta són mig vidre d'unió amb `du`: en temps és més barat que dues passades i en tinta no és el que una persona vol veure, perquè
  deixa el fantasma d'una passada ràpida sobre mitja pàgina. El torn del pany —sis caixes, 2,2 vegades— entra; la cel·la amb el comptador
  —23 vegades— no;
- **el mode de la unió és el més fort dels que netegen, i no el de més rang de la taula**: `a2` té un rang propi i no és una rentada, o
  sigui que una franja que va demanar `gld16` i una peça que va demanar `a2` pugen com la que deixa el vidre com la pàgina. Ho va trobar
  `test_bar` amb la franja del títol pujant en `a2`.

El retry, els comptadors i el deute de pintada són exactament els que hi havia, perquè el bucle no canvia.

**I el log diu qui demana cada pintada** (0.190.142, l'amo: *«que he de buscar al log?»*). Hi ha set portes que acaben a
`paint_root` —`present`, `page` (la pàgina sencera), `over` (les caixes damunt d'una imatge), `wash` (la rentada en
enganxar una vista), `attach` (la vista nova sobre tinta que hi ha al vidre), `turn` (el repintat del pendent) i `due` (el
parpelleig del cursor)— i totes feien la mateixa línia. Ara `[D][UI] paint x,y wxh mode N <nom> (<porta>)` diu la porta, i
el que cal comptar al log és `[GLASS] flush`: **una línia per pintada de debò**, contra les línies `paint` de les regions.

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
| **8 MB de PSRAM** (4 MB mapejats) | **fet de codi i mesurat a la placa** (31/10): `extra_memory()` a la plataforma torna els tres números (xip, adreçable, lliure) i `pal::IExtraMemory` assigna a la PSRAM; l'intèrpret hi posa els blocs de 4 KB en amunt i el sostre de les apps és la PSRAM **mapejada** (4 096 KB). `mem` diu `extra memory 4067 of 4095 KB usable, of 8192 KB on the chip (psram)` i `sys.heap()` en dona els bytes. Els altres 4 MB són al **`himem`** (`mem` en diu el lliure, el total i la finestra, i `mem himem` n'escriu i en rellegeix 32 KB, 0.190.173) i **no són transparents**: mouen els blocs sota el punter, i per això **hi viuen les cachés de sessió de 32 KB en amunt** (`read_at`/`write_at`, una regió alhora, 0.190.174) i el que hi ha a la PSRAM són les cares, les fonts i les imatges (docs/75, docs/90) | comprovar que una app d'1 MB hi cap (docs/95, E-4) |
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
