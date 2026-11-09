# lunokOS

Sistema mínim per a ESP32 que executa **una app en Lua alhora** i exposa el maquinari com a
capacitats opcionals. El mateix codi es compila per a la placa i per al PC, on un emulador el
fa funcionar amb el mateix prompt, el mateix sistema de fitxers i el mateix sostre de memòria.

> **Estat: M0 a M7 fets, amb les mesures de camp de M5, M6 i M7 pendents** (M4 inclou la targeta
> SD, que al Paper ja munta i llegeix); de **M8** ja hi ha la identitat —l'emulador rep la placa que ha
> de ser (`--board`) i `sys.board` en diu el nom— i el maquinari fals que surt del model, i hi queden
> la forma de les tecles i poder-les prémer a mà. Les sis bandes compilen, l'emulador és
> interactiu i **executa apps de Lua**: `run calculator` arrenca una app de veritat al vidre,
> **també a la placa**. Les apps poden **esperar amb promeses** (`sys.delay(300):await()`) sense
> aturar el nucli, **llegir línies** amb `console.read_line()`, **escriure amb colors** amb `term`,
> demanar què hi ha amb `sys.caps()`, obrir un fitxer amb `open` (que pregunta quina app, **cada
> cop**) i **posar alarmes**: `alarm.at`/`after`/`cancel`/`list`/`caps`, amb `on_alarm` entregat a
> l'app que corre, en una **finestra** si no hi és, o posant-la al davant amb `raise`. També hi ha
> `time.now`/`now_ms`/`caps`, i **alarmes que sobreviuen al reinici** (`persist`, a la NVS). Una app
> que es tanca per deixar pas a una altra rep `on_pause`/`on_save`/`on_close`. **552 proves natives**
> en **24 suites**, totes verdes.
> A l'ATOM (9/11): **2 268 577 B** de flash (86,5 % de la partició d'aplicació, 2 621 440 B) i **122 736 B**
> de RAM estàtica (37,5 % de 327 680 B).
>
> **Els fitxers** ja es poden llegir i escriure des de Lua: `fs.list`/`stat`/`read`/`read_at`/`write`
> (escriure és **atòmic**: un temporal i un reanomenament), `mkdir`/`remove`/`rename`, `fs.data()` per
> al propi racó, i `fs.open` amb un **objecte fitxer** (`read`/`write`/`seek`/`size`/`close`, **vuit
> oberts** com a màxim). Cada app toca **vuit llocs i prou**: `/data/<id>/` i `/cfg/`, que són
> escrivibles; `/rtc` i `/fram`, que són **memòria crua** (la del RTC del xip i la FRAM) i també
> s'hi pot escriure; i `/res/` (el seu: `apps/<id>/res/`), `/apps/`, `/fonts/` (les cares que el disc
> porta per a tothom) i `/sd/`, que només es llegeixen; i
> **el límit és el disc**: no hi ha
> quota. Un camí amb espais es pot escriure entre cometes (`cat "res/el meu fitxer.txt"`). **`/cfg/`
> és una porta i no un disc**: apunta a `cfg/<id>/` de la targeta si n'hi ha i de la flash si no, i qui
> ho tria és la persona (`cfg` a la consola, `fs.cfg()` a Lua).
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
> torna una promesa amb `{status, body, bytes}`. La NVS es
> pot mirar en **lectura** com un directori (`ls /nvs`).
>
> **Totes les apps porten icona** (0.170.1): el manifest la declara, el PNG és a `res/` i el llançador el dibuixa a la
> cel·la; una app sense icona és una cel·la amb una lletra, i la suite ho posa en vermell el dia que es crea
> (`test_every_app_of_the_repository_carries_an_icon`, docs/40).
>
> **La pàgina sap ensenyar imatges.** `ui.image(x, y, path[, w, h[, mode[, mask]]])` dibuixa un fitxer del disc a la
> vista de l'app i `ui.image_info(path)` en diu la família i la mida sense dibuixar-lo: **PNG, JPEG,
> BMP, GIF, ICO** i els netpbm **PBM/PGM/PPM**, tots amb el mateix codi que compila per a la placa. El
> color cau a les setze tintes amb la seva lluminància i un **dither ordenat** —un degradat es veu com un
> degradat—, una imatge amb transparència es compon sobre el paper, i una imatge que el sistema no llegeix
> ho diu amb un codi en lloc de sortir torta. Qui les fa servir és el lector, que dibuixa la coberta i les
> figures del llibre.
>
> **Hi ha un editor de debò.** `run editor [path]` és un editor de text gràfic que es comanda amb la
> barra de menús del sistema —**File** obre, desa, desa com i tanca; **View** el torna de només lectura i
> parteix les línies llargues, les dues amb una casella dins del menú— i el camí s'escriu en una targeta
> amb el teclat. El fitxer que la persona nomena al prompt és l'únic d'aquell disc que l'app pot desar
> (`E_INVAL` des d'un altre camí), i la consola sap ser **crua** (`console.raw` i `console.read_byte`:
> les fletxes senceres, sense eco i sense línia) per a una app que vulgui llegir les tecles ella mateixa.
>
> **La pàgina té controls i els dibuixa el sistema.** `ui.panel`, `ui.list`, `ui.textbox`,
> `ui.textbox_multiline`, `ui.selector`, `ui.checkbox`, `ui.progress`, `ui.slider`, `ui.row`, `ui.tabs`,
> `ui.dots`, `ui.dialog`, `ui.sparkline`, `ui.pie`, `ui.month`, `ui.menu`, `ui.menu_bar`,
> `ui.notification_bar` i `ui.icon_grid`: cada un és una ranura del nucli on l'app posa camps i el sistema
> pinta, guarda el focus i la tecla premuda, i **només passa les fletxes al control que les vol**. Les
> cares del disc viuen una sola vegada a `/fonts` (`res/fonts/`, de només lectura) i no una còpia per app.
> Els que els fan servir són les apps del disc —el lector, l'editor, la calculadora, el rellotge i el
> launcher—, i la revisió del projecte antic que va portar-los és a
> [docs/92](docs/92-ui-del-projecte-antic.md).
> **Un fitxer llarg, a l'editor, porta la seva barra de desplaçament al cantó dret de la caixa** —la pista diu quant text hi ha i
> el polze on és la persona— i **es mou amb un swipe** sobre el text: el gest el registra l'app (`on_swipe`), el
> sistema fa l'aritmètica de les files i el polze, i la finestra no sobreviu a la pàgina que la va obrir.
> **El teclat** (`ui.keyboard`) és una banda del sistema i no un control: des de 0.85.0 les lletres són
> **DejaVu Sans Bold** de `/fonts`, dibuixades a doble mida i rebaixades a quatre nivells de tinta —la corba
> d'una lletra i no una escala de píxels—, amb `MAY` com a **bloqueig de majúscules** (no un shift d'una
> lletra) i el retrocés i l'enter dibuixats. El dit i el dial el prenen abans que la pàgina, i des de 0.84.0
> **la vista de l'app és el que queda sobre la banda**: `ui.caps().height` baixa amb el teclat obert i l'app
> rep `on_resize`, o sigui que cap pàgina ha de restar res a mà. **Prémer una lletra no torna a pintar la
> banda**: només ho fan el bloqueig i el canvi d'alfabet (0.85.0). **Des de 0.91.0 la banda segueix el focus**:
> surt quan una caixa que pren tecles (`box.keyboard`) rep el dit o un `kb:show()`, i es tanca quan el dit cau en un
> botó o a la pàgina; `box.keyboard = false` deixa una caixa de només lectura sense teclat. **El cursor és un bloc de
> l'amplada de la lletra i parpelleja sol** (0.92.0): el sistema el gira cada 600 ms i torna a pintar la caixa, i
> l'app no hi ha de portar cap rellotge. **El teclat és una classe amb un pare comú** (0.93.0): `QwertyKeyboard` i
> `AzertyKeyboard` comparteixen la banda, el dit, el dial i la cara, i només canvien les files; es tria amb
> `cfg keyboard qwerty` o `cfg keyboard azerty` i la tria sobreviu a l'arrencada.
>
> **La placa sap dormir.** `sleep [light|deep] <ms>` a la shell i `power.sleep({mode, in_ms})` des
> de Lua: el son lleuger torna i el profund reinicia la placa, i en tornar l'app que hi havia al
> davant **es torna a obrir sola** amb `app.args.restore` (després de `on_save`, que és on desa el
> que vol recordar). El despert és el **temporitzador del RTC** i el **botó**, i el sistema diu quant
> s'ha desviat: `power: woke on the timer, +N ms from the hour it was given`. `power.battery()` diu
> `nil` on no hi ha indicador, que és diferent de zero volts. **Al Paper el sistema s'apaga sol fins a
> la propera alarma** —xip de rellotge, bateria, i ningú fent-lo servir; `power idle off` ho atura—,
> i a les plaques sense rellotge no s'adorm sol mai, perquè una consola que es menja les tecles és
> pitjor que gastar. Les dues mesures d'M6 —consum i desviació de l'hora— encara són pendents. I el
> **processador** es pot dur de **80 a 240 MHz** a mà
> (`power cpu 240`, `power.cpu(mhz)`): el sistema no el mou sol —no hi ha DFS en aquest
> framework— i un número que no sigui un dels passos es refusa en comptes d'arrodonir-se.
>
> **I les dues coses que aquesta placa té per a una persona.** El **LED** (un WS2812 a G27) és del
> sistema, que hi diu quatre moments amb quatre efectes —respiració blanca en arrencar, parpelleig
> blau mentre la ràdio treballa, rebot turquesa quan venç una alarma i SOS vermell quan alguna cosa
> falla—, i una app el pot prendre mentre viu (`led.set(0,0,40)`) o **jugar-hi efectes**
> (`led.play{kind="breathe", color={0,0,40}, period=3000}`, amb `blink`, `sos` i `bounce`, un número
> de cops o per sempre, i `led.stop(800)` per marxar amb un fade). Els efectes corren **sense cap fil
> nou i sense aturar res**: el nucli els pinta a final de cada torn i només quan canvien. Els quatre
> moments del sistema són de l'app, que els pot canviar amb `led.system(...)`. **Quan l'app marxa el
> llum s'apaga**, no torna a cap color d'abans, perquè un llum encès per ningú és energia. A la shell,
> `led 00ff00` i `led off`. El
> **botó** (G39) es llegeix a cada torn del nucli: `button.wait(ms)` dona una promesa amb
> `{kind="press"|"long", held_ms}` i `on_button(kind, held)` és el hook de qui no vol esperar. **Tres
> tocs dins d'1,5 s tanquen l'app** i els compta el sistema abans d'avisar-la, o sigui que cap app no
> se'ls pot empassar. El contracte és de **tira**: `led.pixel(i, r, g, b)` en pinta **un**, i el
> **Matrix** es pinta sencer així, amb la premissa de sempre: quan l'app marxa, s'apaguen tots.
>
> **I el sensor del Matrix**: `imu.read()` torna en **una** lectura l'acceleració en g, el gir en
> graus per segon i la temperatura del xip en °C. El Lite no en té i la crida respon
> `E_UNSUPPORTED` en comptes d'inventar zeros.
>
> **I l'aire de la Paper**: `env.read()` torna la temperatura i la humitat del SHT30 en una sola
> lectura —dos números i dos CRCs que es comproven abans de donar-los—, i als ATOM respon
> `E_UNSUPPORTED` perquè no hi ha xip.
>
> **El que queda** és a [90 · Full de ruta](docs/90-full-de-ruta.md), i el que encara no s'ha
> mesurat va marcat com a **pendent** a la documentació i no se'n dona cap número com si fos mesurat.

## En una frase

Una app Lua al davant, alarmes de rellotge per a la feina de fons, i tot el maquinari —pantalla,
botons, targeta, xarxa, bateria— darrere d'una taula de capacitats que cada placa omple com pot.

## Dues bandes, un codi

| | Placa | PC |
|---|---|---|
| Plataforma | ESP32 (M5 ATOM, ATOM Matrix, Paper i LilyGO T-Koala) | Linux |
| Entorn de PlatformIO | `m5-atom`, `m5-atom-matrix`, `m5-paper`, `esp32-devkit` i `ttgo-t-koala` | `emulator` (i `test` per a les proves) |
| Es diu (`sys.board`) | `m5stack-atom` / `m5stack-atom-matrix` / `m5stack-paper` / `ttgo-t-koala` | el model que rep a `--board`, que és **obligatori**, mai `emulator` |
| És de debò (`sys.emulator`) | `false` | `true` |
| Consola | UART | stdin/stdout en mode cru |
| Fitxers | LittleFS + NVS, i la targeta SD al Paper i al T-Koala | un directori del disc, i `--sd` per a una targeta |
| Rellotge | RTC del SoC | rellotge del PC o temps virtual |

El nucli, l'intèrpret i les dependències de Lua són els mateixos a les dues bandes. Només canvien
les implementacions de placa. La regla que ho fa possible: **`#include <Arduino.h>` només existeix a
`lib/pal_esp32/` i `src/main.cpp`** —el punt d'entrada, que només compila sota
`ARDUINO`—, i a la resta del projecte no n'hi ha cap. Es comprova amb un `grep`.

## Índex de la documentació

| Document | Què hi ha |
|---|---|
| [00 · Visió](docs/00-visio.md) | Principis, no-objectius i glossari |
| [10 · Arquitectura](docs/10-arquitectura.md) | Capes, bucle del nucli, arrencada, estats d'una app, mapa del repositori |
| [20 · Intèrpret Lua](docs/20-interpret-lua.md) | Versió, flags, mòduls, sandbox, pressupostos, errors |
| [30 · API de Lua](docs/30-api-lua.md) | Referència de tots els mòduls que veu una app |
| [40 · Apps i manifest](docs/40-apps-i-manifest.md) | Format d'una app, manifest, cicle de vida, handlers d'extensió |
| [45 · Portar una app del paper](docs/45-portar-una-app-del-paper.md) | El procediment per portar una app de `OLD/PaperOS` amb la mateixa cara: recursos, icones, mides i les trampes d'aquest sistema |
| [46 · L'EPUB: el motor i el port](docs/46-epub-el-motor-i-el-port.md) | El lector d'EPUB del paper, funcionalitat per funcionalitat, i el pla de portar-lo aquí |
| [50 · HAL i plaques](docs/50-hal-i-plaques.md) | Interfícies, taula de capacitats, com s'afegeix una placa |
| [60 · Temps i alarmes](docs/60-temps-i-alarmes.md) | Fonts de temps, cua d'alarmes, finestra d'alarma, son |
| [70 · Sistema de fitxers](docs/70-sistema-de-fitxers.md) | Muntatges virtuals, finestra de lectura, quotes |
| [75 · Pressupost de memòria](docs/75-pressupost-de-memoria.md) | Regles d'allocació i què es mesura |
| [80 · Compilació i proves](docs/80-compilacio-i-proves.md) | Entorns, proves natives, què no es pot provar sense maquinari |
| [85 · Emulador](docs/85-emulador.md) | Com es fa servir i què no simula |
| [90 · Full de ruta](docs/90-full-de-ruta.md) | El que queda, fita per fita, amb el criteri per tancar-la |
| [92 · La UI del projecte antic](docs/92-ui-del-projecte-antic.md) | Revisió de `OLD/PaperOS`: els controls, les regles d'e-ink i què val la pena portar |
| [95 · Mesures a la placa](docs/95-mesures-a-la-placa.md) | El full de camp: què cal mesurar al maquinari, amb quin comandament i on va cada número |

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
9. **Cap maquinari sense la seva política d'energia.** Dormir és l'única manera d'estalviar bateria:
   cada peça que tingui una alimentació commutada, un mode de repòs o un reset s'implementa amb les
   dues meitats —com s'encén quan cal i com es deixa apagada quan no—, i el despert l'arma **el
   rellotge més exacte que hi hagi** i no el primer que funcioni (docs/50, docs/60). I si la placa
   sap **apagar-se del tot** i tornar-se a encendre sola, això és el que fa quan ha de dormir molt:
   al Paper, `sleep deep` no és un son, és una apagada amb l'alarma del rellotge armada. I qui ho
   decideix **no és l'app que ho demana sinó el nucli**: a cada torn, si hi ha una alarma pendent i
   ningú no fa servir la placa, s'apaga fins a aquella alarma i el quars la compta.
10. **Cada canvi puja la versió, i la puja en el mateix commit** (`kernel::kVersion`, docs/80):
    **Z** si és un pedaç —una correcció, un número mesurat, un document que deia una cosa falsa—,
    **Y** si és una cosa nova que una persona o una app pot fer servir —una crida de Lua, un
    comandament, una peça de maquinari conduïda—, i **X** si és una reescriptura de debò, d'aquelles
    que fan que una app que anava s'hagi de tornar a mirar. El CI ho comprova amb
    `tools/check_version.py`: un commit que canvia fitxers i no puja el número no passa.

## Compilació i emulador

```
# per a --gui: SDL2 (recomanat) o, si no hi és, Xlib
# Debian/Ubuntu: sudo apt install libsdl2-dev
pio run -e emulator      # binari per al PC
./.pio/build/emulator/program --board m5stack-atom
# l'emulador es diu la placa que és: --board m5stack-atom (obligatori),
# m5stack-atom-matrix, m5stack-paper o ttgo-t-koala. `sys.board` respon
# aquest nom i `sys.emulator` continua cert
pio run -e m5-atom           # firmware (el puja l'amo, mai una sessió automàtica)
pio run -e m5-atom-matrix    # la placa del panell: 25 llums al mateix GPIO27
pio run -e m5-paper          # la M5 Paper (v1.0 i v1.1): e-ink, 16 MB, PSRAM (docs/50)
pio run -e esp32-devkit      # el mínim: un ESP32 DevKit de 4 MB, sense PSRAM ni res a sobre
pio run -e ttgo-t-koala      # la LilyGO T-Koala: WROVER-B, 4 MB de PSRAM mapejada, LED i SD
pio test -e test             # proves natives
```

`LUNOK_GUI=sdl`, `LUNOK_GUI=x11` o `LUNOK_GUI=none` força el backend de la finestra; sense cap
d'ells, la build tria SDL2 si el troba, després X11, i finalment un emulador sense finestra.

Al prompt de l'emulador: `help`, `version`, `caps`, `mem`, `apps`, `apps remove <id>` (esborra l'app
d'aquell nom —`reader` o `reader-SD`, que és la còpia de la targeta—, del disc on és, i els seus ajustos i
dades es queden),
`mounts`, `run <id> [path] [&]` (l'ampersand al final **deixa l'app en segon pla**: el prompt torna de
seguida, el `stdin`, el `stdout` i el `stderr` de l'app queden desenganxats d'aquesta consola i el que l'app
imprimeix va al `log`; no hi ha cap fil ni cap segona tasca, l'app es segueix atenent al mateix torn que
sempre),
`lua` (el prompt es torna l'intèrpret, amb tota l'API, i `exit` en surt), `ls`, `cat <fitxer>`,
`hexdump <fitxer>` (els bytes en hex i en visible), `open [app] <path>`, `wifi` (`scan`, `add`, `connect`, `forget`, `stop`), `provision [softap]`,
`ntp`, `http <url>`, `repo [owner/repo] [dir]` i `repo info <app>` (les carpetes d'un directori del repositori i el manifest d'una app, de GitHub), `repo install <app>` (el directori sencer d'una app, al disc que `cfg` diu), `power [cpu [mhz]]`, `power last` (quant es va desviar l'últim son), `power
idle [on|off]` (si la placa pot dormir-se sola fins a la pròxima alarma), `led`,
`button`, `sleep [light|deep] <ms>` (o `sleep at <hora>` per a una hora concreta), `cfg`
(`auto`, `sd` o `littlefs`: on van els ajustos de les apps; i `cfg keyboard`, `cfg boot`, `cfg home`,
`cfg tz`, `cfg leap` i `cfg week`: els ajustos del sistema, el de l'hora de la persona i els dos del
calendari), `log`,
`verbose on|off`, `clear`, `reboot` i `poweroff` (talla el corrent de la placa si en té el control; si
no, ho diu). **`exit` i `logout`** tanquen la sessió de la consola, i què vol dir tancar depèn de la consola:
**en un port sèrie** (la placa) la sessió no es pot tancar de debò, així que el sistema ho diu i espera un
**Enter** per obrir-ne una de nova; **en un terminal virtual** (l'emulador) la sessió es tanca de veritat i
l'execució s'atura. **Ctrl+D** fa el mateix que `exit`. `cat` i `hexdump`
sempre necessiten un camí: un camí que el prompt endevina és el camí que no s'ha demanat, i `/rtc` —que és
el que abans sortia quan no es deia res— es mira dient-lo (`hexdump /rtc`).

El disc de l'emulador és el del seu build, **`build/disk/emulator`** —el `data/` del sistema i **totes les
apps d'`apps/`**, enllaçades i no copiades des del 8/11: l'emulador no té placa, així que les rep totes, i el
llançador les ensenya a la graella—, i `--flash <dir>` en tria una altra; la tasca
**lunokOS: seed emulator disk** de VS Code copia les apps de `apps/` a `emulator/littlefs`, que és
el que es fa servir quan es vol un disc propi (`--flash emulator/littlefs`) — i és una **còpia**, que
després d'editar una app **no** es refresca tota sola—. Per córrer un
guio sense seure al davant: `./.pio/build/emulator/program --board m5stack-atom --script <fitxer>`.

Les proves que obrin la finestra de l'emulador (`--gui`) es fan **en un X propi**, mai al `DISPLAY` de
l'usuari: un X virtual (`xvfb-run -a`, per exemple) o un servidor X que la prova encengui i apagui.
Així no es mou el ratolí, no es roba el focus ni es tapen finestres. La finestra de l'emulador és
**SDL2** quan hi és, amb **X11** de reserva; `pio test` no enllaça cap de les dues i no obre cap
finestra. Els detalls són a [80 · Compilació i proves](docs/80-compilacio-i-proves.md) i
[85 · Emulador](docs/85-emulador.md).

## Llicència

**GNU Affero General Public License, versió 3** — el text sencer és a `LICENSE`, a l'arrel. És
copyleft i, a diferència de la GPL, també compta l'ús **per xarxa**: qui faci servir una versió
modificada com a servei ha de publicar el codi que hi ha darrere.
