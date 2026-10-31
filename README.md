# lunokOS

Sistema mínim per a ESP32 que executa **una app en Lua alhora** i exposa el maquinari com a
capacitats opcionals. El mateix codi es compila per a la placa i per al PC, on un emulador el
fa funcionar amb el mateix prompt, el mateix sistema de fitxers i el mateix sostre de memòria.

> **Estat: M0 a M7 fets, amb M4 sense SD (no hi ha maquinari) i les mesures de camp de M5, M6 i M7
> pendents**. Les tres bandes compilen, l'emulador és
> interactiu i **executa apps de Lua**: `run hello` arrenca una app de veritat i n'escriu la línia,
> **també a la placa**. Les apps poden **esperar amb promeses** (`sys.delay(300):await()`) sense
> aturar el nucli, **llegir línies** amb `console.read_line()`, **escriure amb colors** amb `term`,
> demanar què hi ha amb `sys.caps()`, obrir un fitxer amb `open` (que pregunta quina app, **cada
> cop**) i **posar alarmes**: `alarm.at`/`after`/`cancel`/`list`/`caps`, amb `on_alarm` entregat a
> l'app que corre, en una **finestra** si no hi és, o posant-la al davant amb `raise`. També hi ha
> `time.now`/`now_ms`/`caps`, i **alarmes que sobreviuen al reinici** (`persist`, a la NVS). Una app
> que es tanca per deixar pas a una altra rep `on_pause`/`on_save`/`on_close`. **150 proves natives**
> passen en 6 suites.
> A l'ATOM: **2 000 681 B** de flash (63,6 % d'una partició de 3 MB) i **92 456 B** de RAM estàtica (28,2 %); `hello` fa un
> pic de **10 897 B** de 65 536 de sostre i el port sèrie **4,1 ms** d'anada i tornada.
>
> **Els fitxers** ja es poden llegir i escriure des de Lua: `fs.list`/`stat`/`read`/`read_at`/`write`
> (escriure és **atòmic**: un temporal i un reanomenament), `mkdir`/`remove`/`rename`, `fs.data()` per
> al propi racó, i `fs.open` amb un **objecte fitxer** (`read`/`write`/`seek`/`size`/`close`, **vuit
> oberts** com a màxim). Cada app només toca els seus camins, amb una quota de **64 KB**, i un camí
> amb espais es pot escriure entre cometes (`cat "res/el meu fitxer.txt"`).
>
> **La xarxa** ja s'encén sense parar res: `net.connect(ssid, pass)` torna una **promesa** i el
> nucli mira com va a cada torn, amb el termini comptat pel sistema (`E_TIMEOUT`) i un intent alhora
> (`E_BUSY`). I la contrasenya no s'escriu enlloc: **el provisioning és el d'Espressif** (Security 1
> amb proof of possession, i **Bluetooth** per defecte, amb `provision softap` com a camí sense
> Bluetooth), així que la xarxa la porta el telèfon amb l'app *ESP BLE Provisioning*;
> `net.provision()` **dibuixa el QR al port sèrie** (escrit aquí, `lib/qr`), es resol quan la placa és
> a la xarxa i `net.forget()` ho esborra. Aquella llibreria fa **qualsevol QR** (nivells L/M/Q/H,
> modes numèric/alfanumèric/byte) i **també es pot cridar des de Lua** (`qr.encode`), igual que el
> JSON (`json.get_string`): tota llibreria útil es veu des de Lua, i està escrit a `docs/10`.
>
> **La ràdio és del sistema i s'encén per feina, no per estat**: `wifi`, `provision`, `ntp` i `http`
> són **comandes** de la shell (no apps), passen per una **cua de peticions** d'una alhora, i quan
> la cua es buida la ràdio es tanca. Una tasca que necessita la xarxa se la porta ella: `ntp` i
> `http.get` es connecten amb el que la placa té desat i la deixen anar en acabar. `http.get(url)`
> baixa una **pàgina amb TLS** (el paquet d'autoritats del build, cap certificat al repositori) i
> torna una promesa amb `{status, body, bytes}`, i l'exemple de mà és `run page`. La NVS es
> pot mirar en **lectura** com un directori (`ls /nvs`).
>
> **La placa sap dormir.** `sleep [light|deep] <ms>` a la shell i `power.sleep({mode, in_ms})` des
> de Lua: el son lleuger torna i el profund reinicia la placa, i en tornar l'app que hi havia al
> davant **es torna a obrir sola** amb `app.args.restore` (després de `on_save`, que és on desa el
> que vol recordar). El despert és el **temporitzador del RTC** i el **botó**, i el sistema diu quant
> s'ha desviat: `power: woke on the timer, +N ms from the hour it was given`. `power.battery()` diu
> `nil` on no hi ha indicador, que és diferent de zero volts. **El sistema no s'adorm sol** (una
> consola que es menja les tecles és pitjor que gastar) i les dues mesures d'M6 —consum i desviació
> de l'hora— encara són pendents. I el **processador** es pot dur de **80 a 240 MHz** a mà
> (`power cpu 240`, `power.cpu(mhz)`): el sistema no el mou sol —no hi ha DFS en aquest
> framework— i un número que no sigui un dels passos es refusa en comptes d'arrodonir-se.
>
> **I les dues coses que aquesta placa té per a una persona.** El **LED** (un WS2812 a G27) és del
> sistema i només hi diu una cosa —que la ràdio treballa—, i una app el pot
> prendre mentre viu (`led.set(0,0,40)`); **quan l'app marxa el llum s'apaga**, no torna a cap color
> d'abans, perquè un llum encès per ningú és energia. A la shell, `led 00ff00` i `led off`. El
> **botó** (G39) es llegeix a cada torn del nucli: `button.wait(ms)` dona una promesa amb
> `{kind="press"|"long", held_ms}` i `on_button(kind, held)` és el hook de qui no vol esperar. **Tres
> tocs dins d'1,5 s tanquen l'app** i els compta el sistema abans d'avisar-la, o sigui que cap app no
> se'ls pot empassar. El contracte és de **tira**: `led.pixel(i, r, g, b)` en pinta **un**, i el
> **Matrix** es pinta sencer així —l'app `matrix` és una roda de color per als vint-i-cinc llums del
> panell, amb la mateixa premissa de sempre: en marxar, s'apaguen tots.
>
> **I el sensor del Matrix**: `imu.read()` torna en **una** lectura l'acceleració en g, el gir en
> graus per segon i la temperatura del xip en °C. El Lite no en té i la crida respon
> `E_UNSUPPORTED` en comptes d'inventar zeros; l'app `tilt` és un nivell de bombolla als
> vint-i-cinc llums, amb els números a la consola perquè es pugui comprovar el que llegeix.
>
> **El que queda** és a [90 · Full de ruta](docs/90-full-de-ruta.md), i el que encara no s'ha
> mesurat va marcat com a **pendent** a la documentació i no se'n dona cap número com si fos mesurat.

## En una frase

Una app Lua al davant, alarmes de rellotge per a la feina de fons, i tot el maquinari —pantalla,
botons, targeta, xarxa, bateria— darrere d'una taula de capacitats que cada placa omple com pot.

## Dues bandes, un codi

| | Placa | PC |
|---|---|---|
| Plataforma | ESP32 (M5 ATOM) | Linux |
| Entorn de PlatformIO | `atom` i `atom_matrix` | `emulator` (i `test` per a les proves) |
| Es diu (`sys.board`) | `m5stack-atom` / `m5stack-atom-matrix` | `emulator` |
| Consola | UART | stdin/stdout en mode cru |
| Fitxers | LittleFS + NVS | un directori del disc |
| Rellotge | RTC del SoC | rellotge del PC o temps virtual |

El nucli, l'intèrpret i les dependències de Lua són els mateixos a les dues bandes. Només canvien
les implementacions de placa. La regla que ho fa possible: **`#include <Arduino.h>` només existeix a
`lib/hal_atom/`, `lib/pal_esp32/` i `src/main.cpp`** —el punt d'entrada, que només compila sota
`ARDUINO`—, i a la resta del projecte no n'hi ha cap. Es comprova amb un `grep`.

## Índex de la documentació

| Document | Què hi ha |
|---|---|
| [00 · Visió](docs/00-visio.md) | Principis, no-objectius i glossari |
| [10 · Arquitectura](docs/10-arquitectura.md) | Capes, bucle del nucli, arrencada, estats d'una app, mapa del repositori |
| [20 · Intèrpret Lua](docs/20-interpret-lua.md) | Versió, flags, mòduls, sandbox, pressupostos, errors |
| [30 · API de Lua](docs/30-api-lua.md) | Referència de tots els mòduls que veu una app |
| [40 · Apps i manifest](docs/40-apps-i-manifest.md) | Format d'una app, manifest, cicle de vida, handlers d'extensió |
| [50 · HAL i plaques](docs/50-hal-i-plaques.md) | Interfícies, taula de capacitats, com s'afegeix una placa |
| [60 · Temps i alarmes](docs/60-temps-i-alarmes.md) | Fonts de temps, cua d'alarmes, finestra d'alarma, son |
| [70 · Sistema de fitxers](docs/70-sistema-de-fitxers.md) | Muntatges virtuals, finestra de lectura, quotes |
| [75 · Pressupost de memòria](docs/75-pressupost-de-memoria.md) | Regles d'allocació i què es mesura |
| [80 · Compilació i proves](docs/80-compilacio-i-proves.md) | Entorns, proves natives, què no es pot provar sense maquinari |
| [85 · Emulador](docs/85-emulador.md) | Com es fa servir i què no simula |
| [90 · Full de ruta](docs/90-full-de-ruta.md) | El que queda, fita per fita, amb el criteri per tancar-la |

## Lleis que no es trenquen

1. **Una app alhora.** Mai dues MVs vives.
2. **Cap crida de C bloqueja** el nucli; el que espera, espera amb una promesa.
3. **Cap fitxer es carrega sencer**; es llegeix per finestra.
4. **Res de `String` ni `std::string`** al nucli: buffers fixos.
5. **C++11** estricte, perquè és el que compila el nucli d'Arduino per a ESP32.
6. **Cap `#ifdef` de placa.** El que canvia entre bandes és una capçalera amb el mateix nom i
   contingut diferent (`board/identity.h`, `board/impl.h`), i el camí d'inclusió de l'entorn decideix
   quina es veu.
7. Els comptadors de memòria i de temps que no s'hagin mesurat es diuen **pendents**.
8. **El codi mana sobre la documentació.** Si un document i el codi no diuen el mateix, el document
   està malament i es canvia al mateix commit que el codi. La documentació explica **com funciona el
   projecte avui**; el que s'ha arreglat i quan ho diu git, no els documents.

## Compilació i emulador

```
pio run -e emulator      # binari per al PC
./.pio/build/emulator/program --fs emulator
pio run -e atom          # firmware (el puja l'amo, mai una sessió automàtica)
pio run -e atom_matrix   # la placa del panell: 25 llums al mateix GPIO27
pio run -e paper         # la M5 Paper (v1.0 i v1.1): e-ink, 16 MB, PSRAM (docs/50)
pio test -e test         # proves natives
```

Al prompt de l'emulador: `help`, `version`, `caps`, `mem`, `apps`, `mounts`, `run <id>`,
`lua` (el prompt es torna l'intèrpret, amb tota l'API, i `exit` en surt), `ls`, `cat <fitxer>`,
`open [app] <path>`, `wifi` (`scan`, `add`, `connect`, `forget`, `stop`), `provision [softap]`,
`ntp`, `http <url>`, `power [cpu [mhz]]`, `led`, `button`, `sleep [light|deep] <ms>`, `log`,
`verbose on|off`, `clear` i `reboot`. El disc de l'emulador és el directori `emulator/littlefs`, i la
tasca **lunokOS: seed emulator disk** de VS Code hi copia les apps de `data/apps`. Per córrer un
guio sense seure al davant: `./.pio/build/emulator/program --fs emulator --script <fitxer>`.

## Llicència

Pendent de decidir.
