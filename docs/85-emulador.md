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
| `pio run -e emulator -t exec` o `-t upload` | PlatformIO executa el programa amb la sortida conductada: el banner surt, el prompt es queda al buffer i **el que escrius no arriba enlloc** |

Al repositori hi ha la tasca de VS Code **lunokOS: emulator**, que compila i executa el binari amb el terminal connectat de debò. També es pot fer a mà:

```
pio run -e emulator
.pio/build/emulator/program
```

Aquestes són les opcions del binari:

| Opció | Què fa |
|---|---|
| `--fs <dir>` | Disc de treball (per defecte `emulator/`) |
| `--mem-cap <KB>` | Fixa el sostre de la VM; posa-hi el de la placa per veure-hi el pic |
| `--now <època>` | Fixa l'hora d'arrencada. **Sense aquesta opció no hi ha època**: el rellotge val 0 i `time.caps().source` és `"none"`, com una placa que mai s'ha sincronitzat || `--time-scale <N>` | Accelera el temps N vegades (1000 = un dia en 86 segons) |
| `--seed <n>` | Llavor del generador aleatori |
| `--wifi <ms>` | Quant triga la **xarxa de mentida** a pujar (per defecte 300 ms simulats) |
| `--no-wifi` | Una placa sense ràdio: `net()` és `nullptr` i `net.caps().wifi` és fals |
| `--wifi-fail` | Una xarxa que refusa: la promesa de `net.connect` falla amb `E_NONET` |
| `--wifi-scan <n>` | Quantes xarxes veu la ràdio de mentida (per defecte 1). Amb `0` és una placa enmig del camp: `net.scan` contesta `0`, no `true` |
| `--prov-ms <ms>` | Quant està obert l'**AP de provisioning de mentida** (per defecte 2 000 ms) |
| `--prov-ssid <s>` / `--prov-pass <s>` | La xarxa que el provisioning fals «rebuifa» (`casa` / `secret`) |
| `--prov-never` | Una placa que ningú no arriba a provisionar: el termini del nucli salta |
| `--prov-fail` | Un provisioning que falla: `E_NONET` i l'AP es tanca |
| `--http-ms <ms>` | Quant triga la **pàgina de mentida** a arribar (per defecte 50 ms simulats) |
| `--http-status <n>` | El codi que contesta (per defecte 200) |
| `--http-body <s>` | El cos que contesta. Sense aquesta opció contesta `page for <url>`, amb l'adreça a dins perquè es vegi que el que s'ha demanat és el que torna |
| `--http-fail` | Un servidor que no contesta: `E_HTTP` |
| `--imu-ax <g>` / `--imu-ay <g>` / `--imu-az <g>` | Què llegeix el **sensor de mentida** a cada eix. Per defecte 0, 0 i 1, que és la placa plana damunt la taula |
| `--imu-temp <C>` | La temperatura que dona el sensor (per defecte 25) |
| `--imu-fail` | Un sensor que no contesta: `imu.read()` respon `E_IO` |
| `--run <id>` | Arrenca una app pel mateix camí que `run <id>` de la shell i **surt** amb el resultat (0 si ha anat bé) |
| `--script <fitxer>` | Mode lot: executa ordres i surt |
| `--verbose` | El log també surt per la consola |

En mode lot, cada app que arrenca es deixa acabar abans de la línia següent, i **també cada ordre que
treballa** (`wifi add`, `ntp`, `http`): una comanda que espera la ràdio tindria la línia següent
trobant-la ocupada, que és el que va passar quan només s'esperaven les apps (31/10). I després de
**cada** línia es donen dos torns, perquè el que es fa a final de torn —el llum, per posar un cas— hagi
passat abans de la línia següent; sense això un guió veia una placa que no pintava mai res (31/10). El límit és de
**30 s** per línia: una app que espera una tecla que no arriba no pot penjar el guió. Si el guió
arrenca una app que llegeix, l'entrada es pot encaminar per stdin, i ha d'arribar **mentre l'app ja
corre**:

```sh
printf 'run echo\n' > /tmp/s.txt
( sleep 0.4; printf 'hola mon\r' ) | ./.pio/build/emulator/program --fs emulator --script /tmp/s.txt
```

## El disc de treball

```
emulator/
  littlefs/
    apps/<id>/…        les apps que hi hagi
    res/
    cfg/
    data/<id>/         estat de cada app
  nvs/<espai>/<clau>   cada clau és un fitxer, amb el valor a dins
```

No hi ha `sd/`: el muntatge `/sd` existeix a la VFS i cap HAL d'aquest build el munta, o sigui que
una targeta al PC no seria la mateixa targeta que a la placa (docs/50).

Es pot editar amb qualsevol editor. Es pot esborrar per començar net. Es pot versionar un joc de
proves petit dins del repositori i deixar la resta fora del git.

Per tenir-hi les apps de la imatge de la flash (les de `data/`), es copien un cop:

```
tools/sync_emulator.sh
```

Fa el mateix que `mkdir -p emulator/littlefs && cp -r data/apps emulator/littlefs/`, però **esborra
primer** `apps/` i `res/` del disc de l'emulador: és una còpia, no una fusió, i una app treta de
`data/` ha de desaparèixer també d'aquí. També hi ha la tasca de VS Code **lunokOS: seed emulator
disk**.

**Per què cal recordar-ho**: editar una app a `data/apps` **no canvia res** a l'emulador; el que
corre és la còpia del disc de treball.

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
| `1` | Una ordre ha fallat |
| `2` | Els arguments no s'entenen |
| `3` | Una app ha acabat amb error |

Això és el que fan servir les proves del nivell «app»: un guió, una app de prova i un codi de sortida.
Res de comparar text a ulls.

## Fidelitat: què s'assembla i què no

| Aspecte | A l'emulador | A la placa |
|---|---|---|
| Nucli, Lua, VFS, promeses | **Idèntic** | Idèntic |
| Resolució de camins i permisos | Idèntic | Idèntic |
| Sostre de memòria | Limitat a voluntat | 320 KB reals |
| Cost d'una lectura | Microsegons de disc | Mil·lisegons de flash o SD |
| Desgast de la flash | No existeix | Existeix |
| Consum | No existeix | Existeix |
| Despertar de son | Simulat | Real o inexistent segons la placa |
| Espai lliure (`fs.space`, `mounts`) | **No ho pot dir**: el disc és el del PC, i `fs.space` dona `E_UNSUPPORTED` | Números de debò (`/littlefs` 634 880 B lliures de 917 504 el 30/10, i `/nvs` diu `E_UNSUPPORTED` a les dues bandes) |
| Pantalla | No | No (a la v1) |
| Port sèrie | stdin/stdout en mode cru | UART a 115200 |
| Xarxa | **De mentida**: connecta a res en `--wifi` ms i no surt enlloc | La de debò (`WiFi.begin`, que no espera) |
| Provisioning | **De mentida**: un AP que no existeix i unes credencials que surten de les opcions | El component d'Espressif: l'app ESP SoftAP Provisioning del telèfon el troba de debò |
| Pàgines (`http.get`) | **De mentida**: el cos i el codi surten de les opcions, i el TLS no hi és: no hi ha cap certificat a validar | `esp_http_client` amb TLS i el paquet d'autoritats del build; el codi, el cos i els errors de certificat són els de debò |
| Energia (`power.*`) | **De mentida i ho diu**: no hi ha bateria (`power.battery()` és `nil`), i dormir vol dir que **el rellotge virtual salta** fins a l'hora demanada. El PC no s'atura | Bateria si la placa en té; el son és el de debò: `esp_light_sleep_start` i, en profund, la placa es reinicia |
| Tornar d'un son profund | **No passa**: l'emulador no es reinicia enmig d'una crida, i per això les proves criden `kernel::power::restore_session()` a mà, que és exactament el que fa l'arrencada | El xip es reinicia i arrenca: l'arrencada llegeix la nota del son i torna a obrir l'app amb `app.args.restore` |
| Velocitat del processador | **De mentida**: el PC no canvia de marxa; el número es desa (arrenca a **160** i accepta 80, 160 i 240) perquè el camí i els seus refusos es puguin fer servir i provar | `setCpuFrequencyMhz` de debò, amb l'APB i el tick de FreeRTOS movent-se amb ell |
| Llum (`led.*`) | **De mentida**: el color es desa i el test el pot preguntar (`emu::led_red()`… i `emu::led_frames()`, que compta els frames). Té **vint-i-cinc** llums, els del panell del Matrix, i `emu::led_pixel(i, …)` en llegeix un: així una app escrita per al panell es pot provar al PC | El `neopixelWrite` i el frame d'RMT de debò, sobre el WS2812 o els 25 del panell a GPIO27 |
| Apps i manifest (`boards`) | **El banc és una banda amb nom**: respon `emulator` i la família `esp32`, i una app que demani una placa pel seu nom hi surt llistada amb l'avís i `run` la refusa; per provar-la al PC la seva llista ha de dir `emulator` (docs/40) | `apps` la llista amb el que necessita i `run` la refusa dient les plaques que vol i quina és aquesta |
| Sensor (`imu.read`) | **De mentida i a la mà del test**: contesta el que diuen les opcions (`--imu-*`), i `--imu-fail` és un xip que no hi és. El que es prova aquí és el camí de la dada —de Lua al HAL i de tornada a l'app— i no la qualitat de la mesura, que només es pot veure al maquinari | L'MPU6886 per la I2C interna, amb les escales del full del fabricant (docs/50) |
| Botó (`button.wait`, `on_button`) | **De mentida i a la mà del test**: `emu::press_button(true/false)` prem i deixa anar, i el temps que es compta és el del PC | El GPIO39 amb 10 ms de rebot |

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
