# 40 · Apps i manifest

## Què és una app

Un directori amb un `manifest.json` i el codi de Lua. Res més. No hi ha paquets, ni compilació, ni
binaris: si el directori és al lloc, l'app existeix.

```
apps/
  clock/
    manifest.json
    clock.lua
```

**Una app és un sol fitxer de Lua.** No hi ha `require` ni mòduls: la llibreria `package` no
s'obre a l'intèrpret (docs/20), o sigui que una app que vulgui codi compartit l'ha de tenir al
mateix fitxer.

L'**identificador** de l'app és el nom del directori, i el sistema el guarda en **32 B**: un nom més
llarg es talla i la resta del sistema ja no hi arriba. La convenció és `[a-z0-9_-]` (el que fan
servir totes les del repositori), i no es valida a l'escaneig: un directori amb un nom estrany és una
app amb un nom estrany.

## On es busquen les apps

Des de la 0.88.20 les apps es busquen **en un sol lloc: la porta `/apps`**, que és els dos discs en
una sola carpeta virtual —`littlefs/apps` de la flash i `sd/apps` de la targeta junts—. No hi ha cap
altre camí: el llistat, el `run` i els fitxers propis d'una app (`/res/`) hi passen tots, i mai pel
directori real d'una unitat. L'ordre és la flash primer i la targeta després.

**Un nom que els dos discs tenen es presenta dues vegades.** `<id>` és la còpia de la flash i
**`*<id>`** la de la targeta —**l'asterisc és la marca d'una app redirigida**—, o sigui que les dues es poden llegir
i córrer per separat: `run editor` corre l'app de la flash i `run *editor` la de la targeta, i
el `res/` de cadascuna surt del seu disc. És el que fa que instal·lar una app que ja ve al firmware
sigui una actualització que es pot comparar amb l'original en lloc d'un reemplaçament cec, i que
treure la targeta esborri el nom amb la marca i deixi el de la flash. Un id que només és en un dels
dos discs es troba igualment, sense marca.

**La marca i la unitat són dues coses i cadascuna viu al seu lloc** (0.164.0): **l'asterisc va davant de
l'id** —`*editor`— i **la paraula de la unitat va al final del nom** —`Editor-SD`, o `Editor-Littlefs`
si la còpia fos la de la flash—. Les dues es fan servir per coses diferents: **l'id és un camí**
—`/apps/*editor/res/icon.png` ha d'existir, o el llançador no pot pintar la imatge d'aquella cel·la— i
el nom és el que **una persona llegeix** a la graella del llançador i a la barra del sistema. Abans
d'això el sufix anava a l'id (`editor-SD`) i el camí de la imatge era el d'una carpeta que cap disc té:
el llançador deia `editor-SD has no picture` i la cel·la sortia amb la lletra del placeholder (trobat al
emulador el 8/11). El sistema resol la marca amb la mateixa pregunta que fa el llistat —quina còpia és la
que **no** és la simple—, o sigui que el nom i el camí no es poden contradir mai.

**La targeta només es camina si hi és** (0.87.1): la pregunta és al muntatge de `/sd` i no a una
carpeta, o sigui que una placa sense targeta —o amb una que el sistema no ha pogut muntar— no paga
cap volta pel buit i el llistat no pot ensenyar un disc que no hi és. **I el llistat diu de quin disc
ve cada app de la targeta** (`[card]` al costat del títol): el mateix id pot ser als dos, i qui prova
una versió ha de poder llegir quina de les dues li ensenya. Una app que no es troba diu **on s'ha
buscat**, que és la porta:

```
lunok> run edit
no such app: edit
  apps are in /apps, and there is no card in the slot
```

**Què presenta la porta ho decideix el sistema.** La carpeta virtual no sap què és una app, i el
kernel li dona un presentador al boot (`vfs::set_apps_presenter`, amb `apps::is_an_app`): el que
refusa **no arriba al llistat**, i com que la porta és l'únic punt d'entrada, tampoc no arriba a un
`run` ni a la cerca d'un obridor. Avui la regla és **«la carpeta porta un `manifest.json`»**: una
carpeta que no en porta no és una app i no es veu enlloc, i una que en porta però diu una altra placa
**sí** que hi surt, amb el `[needs …]` que diu què necessita —amagar-la deixaria una persona amb una
app al disc i cap rastre d'ella—. La política és aquí i no al disc: una app que el sistema no vulgui
ensenyar no s'ha de poder llistar per cap altre camí.

El camí d'una app és el que diu **`apps::root_of()`**, i és **sempre la porta**: `/apps/<id>`, tant
si l'app és de la flash (`/apps/squares`) com si és de la targeta (`/apps/*squares`). El que respon
darrere el resol la VFS, i el disc que ha contestat es diu amb `Entry::on_card` —el `[card]` del
llistat— i no amb un camí, perquè `/littlefs` sencer és el disc del sistema i no és d'una app
(docs/70). Els **discs** sencers, `/littlefs/apps` i `/sd/apps`, només surten al missatge de
`repo install`, que diu on ha deixat els fitxers.

**`repo install <app> [owner/repo]`** baixa el directori sencer de l'app —el manifest, el codi i el
que porti a `res/`, carpetes incloses— al disc que la persona ha triat amb `cfg`: la targeta amb
`auto` quan hi és, la flash quan no. Cada fitxer és una baixada de la cua de la ràdio i el llistat de
l'API de continguts de GitHub és el que diu quins fitxers té cada carpeta i des d'on baixar-los
(docs/30, docs/70, docs/75).

**`apps remove <id>` esborra l'app** (0.89.0), i el nom és el que surt al llistat: `editor` per a la
còpia de la flash i `*editor` per a la de la targeta, o sigui que una de les dues còpies se'n va i
l'altra es queda —i quan la de la flash ja no hi és, la de la targeta perd el sufix—. Se'n va **la
carpeta de l'app i res més**: el que ha deixat a `/data/<id>` i a `/cfg/<id>` és el seu passat i no
l'app, i qui ho vulgui esborrar ha de dir aquell camí. Una carpeta de `/apps` que no porta
`manifest.json` **no és cap app** i no es pot esborrar escrivint-ne el nom, i l'ordre diu el disc i el
que ha tornat:

```
lunok> apps remove squares
apps: squares removed from /littlefs/apps/squares, 2 files, 1 folder, 4028 bytes
```

**`apps info <id>` llegeix el manifest d'una app que ja hi és** (0.120.0), clau per clau, amb la
mateixa fitxa que `repo info` treu per a una del repositori:

```
lunok> apps info editor
app: Editor (/apps/editor)
  title       Editor
  version     0.1.0
  author      @lunokjod
  entry       editor.lua
  opens       txt md lua json csv ini cfg log
  opensArgs   -ro
  boards      esp32
  icon        --
  description A graphical text editor on the glass, with the menu bar of the
              system: File opens, saves, saves as and closes a page, and View
              turns it read-only and turns word wrap on and off, the two of them
              with a tick in the menu. A long file carries a scroll bar on the
              right of the box, and the gesture that moves it is the app's to
              register.
```

El que el manifest no diu surt amb un guió llarg i no s'amaga, i un nom que la porta no té és
`no such app: <id>` i no una fitxa buida.

## El manifest

```json
{
  "title": "Clock",
  "author": "@lunokjod",
  "version": "0.1.0",
  "entry": "clock.lua",
  "boards": ["esp32"],
  "description": "The hour, kept by an alarm.",
  "opens": "txt md lua json",
  "opensArgs": "-ro",
  "icon": "res/clock.png",
  "splash": "res/clock.png"
}
```

El que el codi llegeix avui, amb la mida que hi cap:

| Camp | Tipus | Obligatori | Mida | Què és |
|---|---|---|---|---|
| `title` | cadena | **Sí** | 47 B | Nom que es mostra |
| `entry` | cadena | **Sí** | 47 B | Fitxer Lua d'entrada, relatiu al directori de l'app |
| `boards` | taula de cadenes | **Sí** | 8 noms de 23 B | Les plaques on l'app funciona: el nom d'una placa o el d'una família (`esp32`) |
| `version` | cadena | No | 15 B | Versió de l'app, text lliure |
| `author` | cadena | No | 31 B | Qui l'ha escrita (0.120.0) |
| `description` | cadena | No | 159 B | Què és l'app; dues línies amb `\n` si cal (0.120.0) |
| `opens` | cadena | No | 8 extensions d'11 B | Les extensions que aquesta app sap obrir, separades per espais (0.120.0) |
| `opensArgs` | cadena | No | 31 B | Els commutadors que l'app vol quan li donen un fitxer; comença per `-` (0.120.0) |
| `icon` | cadena | **Sí** (0.170.1) | 47 B | La imatge de l'app per a un llistat, relativa al directori de l'app (0.120.0). **Des de la 0.153.0 es pinta**: el llançador `home_eink` la posa a la seva graella amb `apps.list()` i `canvas:image` (docs/30). **Tota app d'aquest projecte en porta una** —vegeu la norma de sota— |
| `splash` | cadena | No | 47 B | La imatge que el sistema pinta abans d'arrencar l'app, relativa al directori de l'app |
| `launcher` | booleà | No | — | **L'app és un llançador** (0.153.0): `true` vol dir que la graella d'un altre llançador no la llista. Continua sent una app —`apps` la mostra, `run` l'arrenca i obre el que sap obrir—, i el sistema no ho endevina pel títol ni per l'id: ho diu l'app. Un valor que no és `true` ni `false` es deixa fora amb una línia del registre i no s'endevina. **Des de la 0.163.0 també és la pàgina que un pessic no tanca**: el gest de tancar és del sistema i ho pregunta aquí, perquè una placa que es quedés sense cap pàgina s'hauria d'anar a buscar amb un cable (docs/30) |

Els camps que aquest document preveia i que **no existeixen al codi** —`permissions`, `mem_kb`—
s'ignoren com qualsevol camp desconegut: el sostre de memòria és el de la placa i els permisos encara
no hi són (docs/30). El dia que existeixin, es llegiran aquí i s'escriviran en aquesta taula.

**Els cinc camps de la 0.120.0 venen de les apps de PaperOS** (`data/app/*/manifest.json` del projecte
germà, vint-i-quatre apps): totes hi escriuen `author` i `description`, vint-i-tres hi escriuen `icon`,
quatre hi escriuen `opens` amb una **llista** d'extensions i una hi escriu `opensArgs`. Aquest sistema
en llegia un i mig —un `opens` d'un sol nom— i la resta era text al disc que no arribava enlloc. Ara es
llegeixen tots cinc, i els que l'app pot veure els rep a `app.info()` i a `app.args` (docs/30).

Regles de validació, totes amb error clar i el nom del camp:

- Si falta un camp obligatori, o és buit, l'app **no es carrega** i el motiu surt per la consola.
- `entry` ha d'existir i no pot sortir del directori de l'app (ni `..` ni camins absoluts). Es
  comprova a l'escaneig, no a `run`: una app que no es pot obrir **no surt a `apps`**, i el log diu
  `[W][APPS] <id> has no entry file (<camí>)`. Una app llistada que després no arrenca seria una mentida.
- `boards` diu on funciona l'app. Cada nom és una placa (`m5stack-atom`) o una **família**
  (`esp32`, que vol dir qualsevol placa d'aquest xip), i l'app corre si la placa actual hi surt pel
  seu nom o pel de la seva família. Si no hi surt, **l'app no desapareix**: `apps` la llista amb què
  necessita (`[needs m5stack-atom-matrix]`), `run` la refusa **abans de fer cap VM** i diu les dues
  coses en una línia (`elsewhere runs on m5stack-core2 and this board is m5stack-atom`), i no pot ser
  el gestor de cap extensió (un gestor que no pot arrencar no obre res). Una llista buida, o que
  falta, és una altra cosa: **no és una app** a cap banda.
- **Una llista de més de vuit noms es refusa sencera, i es diu per què** (0.88.11): el sistema en
  guarda vuit (la fila `boards` de la taula), i una llista amb un nom deixat fora diria que l'app
  corre on no corre. El que **no** passa és que la línia digui el que no és: quan el manifest en
  nomena nou, el log diu `apps: <id> names 9 boards and this system carries 8` — i no pas «does not
  say which boards it runs on», que seria una mentida sobre un fitxer que sí que ho diu. La fitxa de
  `repo info` diu el mateix en una línia (`boards      9 in the manifest, more than this system
  carries`) en lloc d'un guió.
- **`esp32` és el xip, no una placa amb tot.** Una ESP32 pelada no porta cap botó, o sigui que una app
  que **necessita** tecles les ha de nomenar pel seu nom (`m5stack-atom`, `m5stack-paper`) i no pot
  dir `esp32`: el manifest diu on l'app **funciona**, i la família sencera inclouria una placa sense
  res per prémer. Una app que demani les tecles nomena les tres plaques que en tenen, una per una.
- **L'emulador no és cap placa i no es nomena, però diu quina és.** `sys.board` a l'emulador diu el
  **model que se li ha dit que sigui** —`--board m5stack-paper`, que és obligatori—, i
  `sys.emulator` continua cert (docs/85). Com que respon **un** nom i no tres, `boards` es compara al
  PC exactament com a la placa: una app del model corre i una d'un altre model es refusa, dient les
  plaques que vol i quina és aquesta. Cap manifest ha de dir `emulator`, que no és cap placa; si algú
  l'hi posa, no el troba enlloc i la prova que repassa el repositori ho vigila.
- `opens` **és una llista d'extensions en una cadena** (0.120.0): `"txt"` i `"txt md lua json"` són el
  mateix camp amb un nom o amb vuit, separats per espais. Cada nom s'accepta amb punt o sense (`"epub"` i
  `".epub"` són el mateix) i es compara **sense distingir majúscules**. Una paraula que no és cap extensió
  es descarta amb una línia que la nomena (`apps: <id> declares an opens that is not an extension (nope/xx)`),
  i una llista de més de **vuit** noms conserva els vuit primers i diu els dos números
  (`apps: <id> names 9 extensions and this system carries 8`): a diferència de `boards`, una llista
  d'extensions que no hi cap **no es refusa sencera**, perquè una app que obre vuit extensions de quinze
  és més útil que una app que no n'obre cap, i el que no passa és que el registre calli. **Un nom de més
  d'onze bytes no es talla**: es descarta amb els dos números
  (`apps: <id> names an extension of 14 bytes and this system carries 11`), perquè una extensió amb
  l'última lletra perduda seria un gestor per a un fitxer que ningú no té.
- **`opensArgs` són els commutadors de l'app, i el sistema no els llegeix** (0.120.0): el valor ha de
  començar per `-` (`"-ro"`), i arriba a l'app tal com està escrit a `app.args.flags`, on només l'app sap
  què vol dir. Un valor que no comença per `-` es descarta amb una línia
  (`apps: <id> has an opensArgs that is not a switch (ro)`), perquè donar a una app una paraula qualsevol
  com si fos un commutador seu és pitjor que no donar-n'hi cap. Al repositori només el fa servir
  `editor`, que obre la pàgina en només lectura quan el sistema li passa un fitxer.
- **`icon` és la imatge de l'app per a un llistat, i es comprova com el `splash`** (0.120.0): el nom s'ha
  de quedar dins del directori de l'app —un camí absolut o amb `..` es descarta amb una línia— i res no el
  pinta encara, perquè la pàgina que llistaria les apps a la pantalla no existeix; `apps info <id>` és on
  es veu. Quan arribi aquesta pàgina, la imatge és allà i el manifest no s'ha de tocar (docs/90).
- `author` i `description` són decoració (0.120.0): no es refusa res si falten, i un text més llarg que
  la seva mida es talla —31 B i 159 B—, que és el que la taula promet. `description` conserva els `\n`,
  perquè les descripcions de les apps de PaperOS són de dues línies i `apps info` les treu tal com són.
- **`apps info <id>`** (0.120.0) és la fitxa d'una app que ja és al disc: la mateixa que `repo info`
  ensenya per a una del repositori, amb una línia per clau i un guió llarg al que el manifest no diu.
- **`splash` és una imatge de dins l'app i res més** (0.114.0): el nom s'ha de quedar dins del
  directori de l'app —un camí absolut o amb `..` es descarta amb una línia al registre— perquè un
  manifest que apunta fora del que porta és un manifest que menteix. Un nom que hi és però no és
  cap imatge **no es refuseix aquí**: l'app arrenca igualment i el registre diu per què no s'ha
  pintat, perquè el dibuix és decoració i una app que no arrenca per una imatge és pitjor que una
  app sense imatge. La imatge es pinta amb `splash` —la mateixa crida dels moments del sistema,
  neteja de tot el panell i al mig— **just abans que arrenqui l'intèrpret i després que la pàgina
  de l'app estigui preparada** (0.118.1: l'`attach` de la vista neteja el vidre quan la pàgina
  anterior hi havia pintat, i pintar abans volia dir una imatge que ningú no arribava a veure), i
  la primera pàgina que l'app presenta se l'emporta (docs/30, docs/50, docs/92 §33).
- **I des de 0.151.17 la imatge surt abans de carregar l'app i abans de la màquina de Lua**, i no
  només abans de l'intèrpret: l'arrencada es fa en dos trossos. El primer és `find_app`, que només
  mira el llistat i llegeix el manifest —dues copies de cadenes i prou—, i és el que diu com es diu
  la imatge; el segon és `read_source`, que és el que costa (un bloc de 98 304 B del munt i la
  lectura del fitxer) i va **després** d'`ui::attach()`, del `splash::show` i del títol de la barra.
  Així el vidre ensenya el dibuix mentre es llegeix l'app i mentre es construeix la màquina, que és
  el que es veu quan una app triga. L'ordre dins del primer tros és el de sempre —`attach`, imatge,
  títol— perquè l'`attach` neteja el vidre (0.118.1); el que ha canviat és que el títol de la barra
  ja no espera l'intèrpret. Si la lectura de l'app falla, la vista i la barra es tornen a donar
  (`ui::give_back_the_glass()` i `bar::title("")`), perquè ja eren de l'app que no arriba. Una
  finestra d'alarma (`run_window`) **no** pinta cap imatge: no és una arrencada ni té pàgina pròpia,
  i fa servir `load_source`, que és els dos trossos junts.
- Els camps desconeguts s'ignoren (per poder afegir-ne en el futur sense trencar res).
- Si hi ha un camp `id`, **s'ignora**: l'identificador d'una app és sempre el nom del seu directori,
  i un `id` que digui una altra cosa no es comprova ni es fa servir.

## L'escaneig

No hi ha cap registre d'apps desat i **no hi ha cap límit d'apps**: quan algú demana la llista
(`apps`, `ls`, `run <id>`), es torna a llegir `/sd/apps` i `/littlefs/apps`, un directori a la
vegada. Una app nova la veu la petició següent, sense reiniciar.

- El manifest es llegeix amb un **sostre** (1 KB) i se n'extreuen només les claus que el sistema
  necessita. No es construeix cap arbre JSON: `75 · Pressupost de memòria` ho prohibeix.
- Un manifest que falta, que no és JSON o que no té `title`, `entry` o `boards` **descarta aquella app i només
  aquella**; les altres es veuen igualment, i el motiu queda al log.
- El que responen `apps`, `ls`, `cat` i `run <id>` és el que hi ha al disc en aquell moment.
- Es fa quan es demana i no a l'arrencada: el temps d'arrencada no depèn de quantes apps hi hagi.

## Una app ha de saber on corre

Una app pot acabar corrent en una placa que no ha vist mai. El que li ho diu és `sys.board`, amb **el
nom que la placa té a `platformio.ini`** (`board = m5stack-atom`) o el nom propi de la placa de la
família que canvia què es pot tocar (`m5stack-atom-matrix`). Al PC, el nom és el de la placa que el
emulador ha rebut amb `--board`, i `sys.emulator` és el que diu que el maquinari de sota és fals. Una app
que toca maquinari mira això abans de res i decideix el camí; mai suposa que és a la placa on es va
escriure. El detall i les regles són a [`30 · API de Lua`](30-api-lua.md#saber-on-sexecuta).

**I hi ha una placa que no té ni les eines per dibuixar** (0.179.9): als dos ATOM, a l'ESP32 de DevKit i al T-Koala el
build no porta la pàgina, el lector ni les cares, i `ui`, `canvas`, `keyboard`, `font` i `epub` no existeixen al costat de
l'app (docs/30, docs/50). Una app que dibuixa ha de mirar `sys.caps().display` **abans** de tocar-ne cap, perquè no hi ha
cap taula que contesti que no: la primera línia peta. El llançador i el lector del repositori ja ho fan, i per això el que
diuen en aquestes quatre plaques és `home: this board has no glass` i `about: this board has no glass`.

**I `boards` no és només per al `run`**: és el que decideix **què porta el disc de cada placa**. El disc
d'un entorn el munta `tools/make_disk.py` amb les apps el manifest de les quals nomena **aquella placa o la
seva família** —avui totes les del repositori nomenen `esp32`, o sigui que qualsevol placa les pot portar
si el seu entorn les demana—, i cada carpeta del disc és un enllaç a `apps/` de l'arrel.
L'emulador i les proves no són cap placa i en porten totes (docs/70, docs/80).

## Handlers d'extensió

Un manifest pot declarar `opens`: les extensions que l'app sap obrir, escrites en una sola cadena i
separades per espais (0.120.0). `"epub"`, `".epub"` i `"EPUB"` són la mateixa cosa (el sistema ho
normalitza a minúscules i sense punt); una paraula que no és cap extensió es rebutja al log i les altres
de la mateixa llista es queden. L'app obre un fitxer quan la seva extensió és **qualsevol** dels noms de la
llista, i amb quin nom l'ha oberta ho sap a `app.args.ext`.

```
open <path>          → l'extensió del fitxer tria l'app
open <app> <path>    → l'app és la que diu l'ordre
```

- El fitxer ha d'existir i no ser un directori: si no, l'ordre diu què passa i no arrenca res.
- **El camí no es talla mai en silenci.** El sostre és de **128 B**, el mateix que el sistema de
  fitxers, i un camí més llarg no arriba ni a la comprovació: el sistema de fitxers el refusa i
  l'ordre diu `no such file: <camí>`. El selector en guarda 128 B i, si algun dia una altra via li
  demana obrir un camí que no hi cap, ho diu amb els bytes que té i el màxim que accepta en lloc de
  tallar-lo (des d'`open` això no es pot veure, perquè el refús arriba abans). Un camí tallat
  arrencaria l'app amb un fitxer que ningú no ha demanat.
- Un fitxer sense extensió no s'obre: **no hi ha handler per defecte**, i endevinar-lo seria pitjor
  que dir-ho.
- Si cap app declara l'extensió: `no app opens .txt`.
- Si **més d'una** la declara, el sistema **pregunta**: treu la llista numerada i espera un número.

  ```
  more than one app opens .txt:
    1) txt
    2) txt2
  which one? (1-2)
  ```

  - El número tria l'app. **Es pregunta sempre**: no es recorda cap tria, perquè una resposta
    desada amaga l'altra app i no hi hauria manera d'arribar-hi des del selector. Qui vulgui una
    app concreta la nomena, i així no es pregunta res.
  - Qualsevol altra cosa que no sigui un número vàlid cancel·la (`cancelled`) i no arrenca res.
  - `open <app> <path>` sempre mana: no pregunta mai.
  - El número és **la posició a la llista que s'acaba de veure**, no un identificador estable. La
    llista surt en **ordre de directori**, no alfabètic: si s'hi afegeix una app, els números poden
    canviar. Per això el que mana sempre és `open <app> <path>`, que no depèn de cap ordre.
- L'app rep el fitxer a `app.args` (`path`, `ext`, `source`) i sap qui és amb `app.id`. Si el manifest
declara `opensArgs`, la línia d'aquells commutadors arriba a `app.args.flags`, i el sistema no la llegeix
(0.120.0, docs/30).
- L'escaneig no es fa en arrencar sinó quan es demana, i per això una app nova amb el seu `opens` ja
  funciona sense reiniciar.

Quan es vol obrir un fitxer (des del prompt amb `open <camí>`, o perquè una altra app ho demana):

| Handlers per a l'extensió | Què passa |
|---|---|
| Cap | Error: «cap app sap obrir `.xyz`» |
| Un | S'obre directament |
| Més d'un | **Es pregunta**: llista numerada a la consola, cada cop que es demana |

La pregunta es fa sempre i no es desa enlloc: una tria recordada faria que l'altra app fos
inabastable des del selector. `open <app> <path>` és la via per anar de dret a una app.

L'app que s'obre rep `app.args = { path = "/sd/llibres/un.txt", ext = "txt", source = "open" }`,
amb el camí **tal com escrit** (el de l'exemple és un que l'app pot llegir: la targeta o un lloc
seu). `source` val `"open"` quan la obre el prompt o una altra app, i `"sleep"` quan torna d'un son
de qui gairebé es va desar la feina. El camí no es tradueix per a l'app: `/res` vol dir el `res/` **de
l'app**, o sigui que `open res/x.txt` troba l'app per l'extensió i després l'app no pot llegir el
fitxer, perquè el `/res` que veu és el seu (docs/70).

## Cicle de vida

Totes les funcions són opcionals. Si existeixen, es criden en aquest ordre:

| Funció | Quan |
|---|---|
| `on_load()` | Després de carregar l'entry i abans d'obrir res |
| `on_open(args)` | Quan l'app passa a tenir el port (o rep un fitxer) |
| `on_event(ev)` | Per cada esdeveniment, si l'app en declara |
| `on_alarm(id, payload, meta)` | Quan venç una alarma seva |
| `on_button(kind, held_ms, key)` | Quan es prem una tecla (docs/30) |
| `on_light()` | Quan un moment del sistema s'acaba i el llum torna a ser de l'app, perquè el torni a pintar si durant la interrupció ha canviat d'idea (docs/30) |
| `on_resize(width, height)` | Quan la vista de l'app és una altra: una rotació (`ui.rotation`), un `ui.resize`, **la pantalla sencera o la tornada** (`ui.full_screen`, i els tres tocs que la desfan, que fa el sistema, 0.136.0) o un canvi del sistema. El canvas és **nou i blanc** i el sistema no sap què hi va: tornar a dibuixar és de l'app (docs/30, docs/46 D1, docs/50) |
| `on_pause()` | Abans de tancar-se per deixar pas a una altra app |
| `on_save()` | Just després de `on_pause`; bon moment per escriure l'estat al disc (`fs.data`) |
| `on_close()` | Últim, abans de destruir la VM |

Si una app no declara `on_open`, s'executa el cos de l'entry i prou: és la manera curta d'escriure
una app.

**Què es crida avui (1/11)**: `on_alarm`, `on_button`, `on_light`, `on_resize`, `on_pause`, `on_save` i
`on_close`. Els tres de comiat es
criden abans de tancar una app, tant si la tanca un `raise` com si la treu la persona amb ESC o
`!!exit`. `on_load`, `on_open` i `on_event` encara no existeixen. Cap ganxo no pot esperar: es criden
entre dos torns del nucli, amb la coroutine de l'app suspesa.

`on_event`, `console.on_data` i les lectures directes (`console.read_line()`) són **excloents dins de la
mateixa app**: o l'app és dirigida per esdeveniments, o rep els trossos amb `on_data`, o llegeix ella
mateixa. Fer-ho tot alhora fa que no se sàpiga qui es queda la línia.

## Les apps portades del projecte del paper (0.121.0)

Les apps que venen del projecte germà —`calculator` (0.121.0), `about` (0.122.0, amb la llista de
biblioteques a 0.124.0) i `timeclock` (0.125.0) fins avui— es porten amb **la pantalla i el comportament
sencers** i amb els números de l'original, i el procediment sencer —què es llegeix, com es porten les icones i
el splash, la taula d'equivalències i les trampes— és a
[`45 · Portar una app del paper`](45-portar-una-app-del-paper.md). Les tres coses que més han costat, i
que val la pena tenir aquí a mà:

- **Les mides de la lletra es demanen en píxels de línia, no en passos.** El paper fa servir quatre
  mides de la seva tipografia —`small` **33 px**, `body` **43**, `title` **57** i `big` **82** de
  línia, a `lib/gfx/fonts/fonts.h` d'allà— i aquí un pas de la cara val **onze** píxels de línia amb
  la negreta del disc i **set** amb la taula del flash. Escriure el pas directament deixa la pàgina a
  la meitat de mida a la placa (el cas de la calculadora, 0.121.1): el que es fa és
  `scale_for(px)` → `math.floor(px / ui.line_height(1) + 0.5)`, i així la pàgina fa la mateixa mida
  física a les dues bandes (docs/30).
- **No hi ha `visible`: una pantalla es construeix i es torna, no s'amaga.** Una app de dues pantalles
  —una portada i un detall— dona el control de la que marxa amb `release()` i construeix l'altra; els
  fills se'n van amb el pare i `release()` no compon el que marxa, o sigui que no queda cap fantasma
  a sota (docs/30, docs/90).
- **Un `ui.label` no parteix les línies**: un text que no hi cap s'acaba amb `...`. Una app que ve
  amb paràgrafs els parteix ella mateixa amb `ui.text_width`, que és la mateixa mesura amb què el
  label dibuixa. **I la pàgina és de l'app sencera**: els controls pinten dins del seu marc, o sigui
  que l'aire del voltant es queda amb el que hi havia al vidre —el dibuix de l'arrencada— si l'app no
  fa un `ui.clear(15)` abans de construir la pàgina.

## Una app sense pàgina: l'arc iris del Matrix (0.179.18)

L'amo: *«fes una app de demo per l'M5Atom Matrix: Leds multicolor com l'arc iris i amb canvis de to suaus i amb
moviment»*. És `apps/rainbow`, i és **la primera app d'aquest arbre que no dibuixa res**: les dues plaques que la
poden córrer no tenen vidre, i la seva cara és el llum —vint-i-cinc WS2812 en un cinc per cinc al Matrix, un al Lite
(docs/50)—. Una app així no té `ui`, ni `canvas`, ni `font`: els té a `nil`, perquè la placa no els porta (0.179.9), i
el que fa és pintar la tira i esperar el torn següent.

- **La forma del panell surt del número de la placa i no d'una constant**: `led.caps().count` és vint-i-cinc i
  `side = floor(sqrt(count) + 0.5)` és cinc, o sigui que el mateix fitxer sobre el llum únic del Lite és un to que
  respira (`rainbow: 1 light(s) in 1 by 1`) i cap línia del codi no diu «Matrix» enlloc (docs/30).
- **Quatre camps es tornen —`bands`, `iris`, `spiral` i `rain`— i el canvi és una barreja i no un tall**: el que marxa
  i el que arriba es barregen durant dos segons i mig **en to i en nivell**, i el to va **per la drecera de la roda**
  —la diferència es plega a ±mig gir—, i així els dos colors que es veuen entremig són veïns de la roda i no una
  mitjana terrosa en RGB. A sobre, una respiració del nou per cent cada onze segons és el to que no para de moure's.
- **El nivell és el sostre de la placa**: `led.caps().max` és **vint** al Matrix —vint-i-cinc LEDs de 2020 en una caixa
  tancada s'escalfen per sobre— i una rampa escrita contra 255 seria una rampa que el sistema talla sense dir res. Un
  frame és com a molt `(20, 20, 20)`, i això és el que el panell de debò ensenya.
- **Un frame és un torn, i el torn s'acaba esperant**: `sys.delay(40):await()` torna la coroutine al nucli, que és qui
  llatxa la tira a final de torn, i els vint-i-cinc llums amb dos camps i una barreja a sobre costen uns tres mil
  instructions dels vint mil que té un torn. Cap panell s'envia mig pintat (docs/20, docs/75).
- **L'únic botó salta a la frontera entre dos camps**, que és l'únic instant en què el frame no té barreja: el que es
  veu després de prémer és el camp següent sencer. Es llegeix amb `button.down(0)` un cop per frame, que és la tercera
  de les tres maneres que dona docs/30.
- **I el que diu el registre és el que el cas de la suite llegeix**: `rainbow: 25 light(s) in 5 by 5, 4 fields of 9 s,
  the button takes the next one` a l'arrencada i `[rainbow] field 2 of 4: iris` quan en canvia un.
- **No és a la imatge de cap placa**: cap placa no porta apps (`custom_apps_in_disk = no`), o sigui que viatja amb una
  targeta o amb `custom_apps = rainbow` a l'entorn. Al PC ja és al disc que l'emulador obre, i la manera de mirar-la
  és la finestra del 0.179.17:

  ```
  .pio/build/emulator/program --board m5stack-atom-matrix --gui --run rainbow
  .pio/build/emulator/program --board m5stack-atom --gui --run rainbow
  ```

- **El `boards` són els dos ATOM**, i el Paper la llista i la refusa amb el motiu, com qualsevol app d'una altra placa
  (docs/40, docs/85). **I porta icona pròpia** —el panell de vint-i-cinc punts amb la roda girada en grisos— perquè és
  una app que una persona fa córrer i no una de prova amb el lunokhod.

## L'app que surt sola en arrencar (l'app «de casa», 0.119.0)

Una placa pot arrencar **dins d'una app** en lloc de deixar el prompt: és el que fa un launcher, un
rellotge o la pàgina que una persona vol veure en encendre la placa. El nom surt de tres llocs, en
aquest ordre:

1. **L'store de la placa**, que és el que escriu `cfg home <id>` (i `cfg home none` per tornar al
   prompt). És el que mana.
2. **El build**, `-D LUNOK_HOME_APP="<id>"` a `platformio.ini`: el valor per defecte de cada placa.
   Buit a tots els entorns d'aquest arbre, o sigui que una placa sense res dit arrenca al prompt.
3. Res: el prompt.

La comprovació és **on s'escriu el nom**: `cfg home` passa el nom per la porta de les apps, i un nom
que no es presenta —o que és d'una altra placa— es refusa amb el motiu. A l'arrencada, si el disc ha
perdut l'app (una targeta que no hi és, una app esborrada), el log diu `no app '<id>' on this disk`
amb `[W][HOME]` i la placa es queda al prompt: **no hi ha bucle d'arrencada**.

**No trepitja el registre de sessió**: una placa que se'n va anar a dormir dins d'una app torna a
*aquella* app, i l'app de casa només s'engega quan no ha tornat res (docs/30, docs/60, docs/90). I no
arriba cap `app.args.restore`: no ve de cap son, ve d'un arrencada.

## Una app mínima

```lua
-- apps/<id>/<id>.lua: l'app més curta que té sentit
console.print("Hola. Escriu 'sortir' i Enter.")
while true do
  local line = console.read_line()
  if line == nil or line == "sortir" then break end
  console.print("Has escrit: " .. line)
end
```

Manifest:

```json
{
  "title": "Hola",
  "author": "@lunokjod",
  "version": "0.1.0",
  "entry": "hello.lua",
  "boards": ["esp32"],
  "description": "One line out, and that is the whole app."
}
```

## Assets

| Camp | Format | Qui el pinta |
|---|---|---|
| `splash` | El que llegeixi `img`: avui **PNG**, BMP, GIF, ICO i PNM | El sistema, abans d'arrencar l'app (0.114.0) |
| `icon` | El mateix que el `splash` | **Ningú encara**: la pàgina que llistaria les apps a la pantalla no existeix. El nom es llegeix, es comprova que quedi dins de l'app i `apps info <id>` el diu (0.120.0) |

El format `.icn` d'M5 —el que porten les apps de PaperOS, `icon.icn`— **no està documentat** fins a
tenir un fitxer de mostra i, per tant, no es llegeix: un manifest que l'anomeni queda amb un `icon`
que no dibuixa res, i no passa res més, perquè el dibuix és decoració (docs/30).

**La imatge font mana, i el splash es compon d'ella** (0.151.17): una app que vol imatge porta el
dibuix que una persona edita a `res/icon-source.png` i en treu el que el manifest nomeja amb
`tools/make_icon.py` —94×94, la mateixa mida que les altres apps del disc—, i compon el splash amb
`tools/app_splash.py`, que és el `splashgen.py` de PaperOS portat aquí. Cap de les dues coses es
dibuixa a mà: es tornen a fer amb una ordre, i la mateixa recepta dona sempre la mateixa imatge.

```
python3 tools/make_icon.py --png apps/reader/res/icon-source.png \
    --out apps/reader/res/icon.png
python3 tools/app_splash.py --icon apps/reader/res/icon-source.png --title EPUB \
    --foot lunokOS --out apps/reader/res/splash.png
```

La mida i l'aspecte del splash són els del projecte de paper —420×520, la icona a 232 px, una
ratlla, el títol, el `loading...` i un peu— perquè l'artwork és el mateix; el peu és el nom del
sistema on corre, i `--foot PaperOS` torna a donar l'original de PaperOS.

**La primera app amb icona i splash és `reader`** (0.151.17): `res/icon-source.png` és el dibuix
font de l'EPUB de PaperOS (`OLD/PaperOS/tools/icons/EPUB.png`, 448×448, escala de grisos, 2 818 B),
`res/icon.png` n'és la icona de 94×94 (1 760 B) i `res/splash.png` la composició (6 876 B).
L'`icon.icn` d'aquella app **no s'ha portat**: el manifest nomeja el PNG, que és el que aquest
sistema sap llegir.

Les altres apps —`about`, `calculator`, `editor`, `fonts`, `home_eink`, `reader`, `shell`, `timeclock`, `tz`, `wipe`, `2048` i
`rainbow`— segueixen la mateixa convenció, amb el seu `res/icon-source.png` i el seu `res/icon.png`, i des de la 0.153.0
**totes les apps del disc declaren `icon`**: el camp va néixer perquè el manifest el pogués dir el dia que la
pàgina de les apps es dibuixés, i aquell dia és el llançador, que les posa a la seva graella (docs/30, docs/85).

**Les apps de prova porten el lunokhod** (0.169.4, l'amo: *«una icona del lunokhod estaria molt guay»*): una
app que existeix per mesurar o per provar el sistema —avui `luaperf`— no dibuixa una icona pròpia, sinó que
comparteix un sol dibuix, el del rover que dona nom al sistema. El mestre és **fora de l'app**, a
`tools/icons/Lunokhod.png` (448×448, 2 888 B), perquè és el mateix per a totes, i el que el manifest nomenja
surt d'ell amb la recepta de sempre:

```
python3 tools/make_icon.py --png tools/icons/Lunokhod.png --out apps/luaperf/res/icon.png
```

El `res/icon.png` sí que és de l'app —el manifest no pot nomenar res de fora de la seva carpeta, i el nucli
buida el camp si ho intenta—, però el dibuix no s'hi copia: una còpia per app seria un lunokhod vell el dia
que el dibuix canviï.

**I el lunokhod és de les apps de prova, no de les que una persona fa servir** (9/11, l'amo): una app que existeix
per mesurar o per provar el sistema el porta perquè es vegi d'un cop d'ull quina mena d'app és; una app de debò
—l'editor, el lector, la calculadora, la de la zona horària (`tz`), l'esborrador (`wipe`, que dibuixa una paperera amb
les seves tres costelles) i la de les cares (`fonts`, un full amb una `Aa` sobre un regla)— dibuixa la seva,
amb el seu `res/icon-source.png` i la recepta de dalt. El que no pot passar és que una app de prova
s'amagui darrere una icona que sembla d'una app normal.

### La norma: **totes les apps porten icona** (0.170.1)

L'amo: *«fes una norma, totes les apps han de tenir icona»*. Una app d'aquest projecte **declara `icon` i el fitxer hi
és**: el manifest el nomena, el dibuix és a `res/` i és un **PNG** amb alguna cosa a dins. No és un consell i no és
decoració: el llançador dibuixa la imatge de cada cel·la, i una app sense icona és una cel·la amb la primera lletra
del nom en una caixa —el llançador ho diu, `home: <id> has no picture`—, o sigui una app que sembla inacabada al
costat de les altres.

- **Qui ho comprova**: la prova `test_every_app_of_the_repository_carries_an_icon` (`test/test_apps`), que passeja
  `apps/` amb el disc del PC i, per a cada `manifest.json`, exigeix el camp, que el camí quedi dins de la carpeta de
  l'app i que el fitxer existeixi amb la signatura d'un PNG i més de dos-cents bytes. Una app nova sense icona posa la
  suite en vermell el mateix dia que es crea, que és quan és barat arreglar-ho.
- **La icona és una imatge i no un dibuix a mà**: `tools/make_icon.py` baixa el mestre (`res/icon-source.png`, o el
  lunokhod compartit de les apps de prova) a un PNG de 94 × 94, que és la mida que el llançador li dona.
- **El nucli no refusa un manifest sense `icon`**: el camp és opcional per al sistema —una app de fora pot no dur-ne i
  la seva cel·la ensenya la lletra— i la norma és del projecte. El dia que una app de fora vulgui entrar aquí, el que
  li falta és el dibuix, no el permís (docs/30, docs/85).
- **I qui toca una app mira la icona** (9/11, l'amo: *«si generes alguna app o la modifiques, revisa si l'icona no hi és
  per posar-ne una (amb l'estil de les altres)»*): crear una app, portar-ne una o canviar-ne la pàgina vol dir mirar si
  el manifest en declara una i si el PNG hi és; si no hi és, s'hi posa **amb l'estil de les altres** —el dibuix a
  `res/icon-source.png`, 448 × 448, els quatre grisos del projecte del paper, i la recepta de `tools/make_icon.py`— i no
  una forma nova. És el que fa que la graella del llançador sembli una graella i no una llista de deures.

## Instal·lació

| Via | Estat |
|---|---|
| `pio run -e m5-atom -t uploadfs` (imatge de `data/`) | És la via de la v1 |
| Copiar el directori a `/sd/apps/` | Sí: el Paper munta la targeta a `/sd` (docs/50) i `apps` i `run` la busquen abans que la flash (0.86.0) |
| Baixar-la per Wi-Fi des del prompt | Fora de la v1 |

Desinstal·lar és esborrar el directori. Les alarmes persistents d'una app que ja no hi és es
descarten a l'arrencada, amb una línia al log.
