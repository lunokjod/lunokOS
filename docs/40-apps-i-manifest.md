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
**`<id>-SD`** la de la targeta —el sufix és literalment `-SD`—, o sigui que les dues es poden llegir
i córrer per separat: `run squares` corre l'app de la flash i `run squares-SD` la de la targeta, i
el `res/` de cadascuna surt del seu disc. És el que fa que instal·lar una app que ja ve al firmware
sigui una actualització que es pot comparar amb l'original en lloc d'un reemplaçament cec, i que
treure la targeta esborri el nom amb sufix i deixi el de la flash. Un id que només és en un dels
dos discs es troba igualment, sense sufix.

**La targeta només es camina si hi és** (0.87.1): la pregunta és al muntatge de `/sd` i no a una
carpeta, o sigui que una placa sense targeta —o amb una que el sistema no ha pogut muntar— no paga
cap volta pel buit i el llistat no pot ensenyar un disc que no hi és. **I el llistat diu de quin disc
ve cada app de la targeta** (`[card]` al costat del títol): el mateix id pot ser als dos, i qui prova
una versió ha de poder llegir quina de les dues li ensenya. Una app que no es troba diu **on s'ha
buscat**, que és la porta:

```
lunok> run timerss
no such app: timerss
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
si l'app és de la flash (`/apps/squares`) com si és de la targeta (`/apps/squares-SD`). El que respon
darrere el resol la VFS, i el disc que ha contestat es diu amb `Entry::on_card` —el `[card]` del
llistat— i no amb un camí, perquè `/littlefs` sencer és el disc del sistema i no és d'una app
(docs/70). Els **discs** sencers, `/littlefs/apps` i `/sd/apps`, només surten al missatge de
`repo install`, que diu on ha deixat els fitxers.

**`repo install <app> [owner/repo]`** baixa el directori sencer de l'app —el manifest, el codi i el
que porti a `res/`, carpetes incloses— al disc que la persona ha triat amb `cfg`: la targeta amb
`auto` quan hi és, la flash quan no. Cada fitxer és una baixada de la cua de la ràdio i el llistat de
l'API de continguts de GitHub és el que diu quins fitxers té cada carpeta i des d'on baixar-los
(docs/30, docs/70, docs/75).

**`apps remove <id>` esborra l'app** (0.89.0), i el nom és el que surt al llistat: `squares` per a la
còpia de la flash i `squares-SD` per a la de la targeta, o sigui que una de les dues còpies se'n va i
l'altra es queda —i quan la de la flash ja no hi és, la de la targeta perd el sufix—. Se'n va **la
carpeta de l'app i res més**: el que ha deixat a `/data/<id>` i a `/cfg/<id>` és el seu passat i no
l'app, i qui ho vulgui esborrar ha de dir aquell camí. Una carpeta de `/apps` que no porta
`manifest.json` **no és cap app** i no es pot esborrar escrivint-ne el nom, i l'ordre diu el disc i el
que ha tornat:

```
lunok> apps remove squares
apps: squares removed from /littlefs/apps/squares, 2 files, 1 folder, 4028 bytes
```

## El manifest

```json
{
  "title": "Clock",
  "author": "@lunokjod",
  "version": "0.1.0",
  "entry": "clock.lua",
  "boards": ["esp32"],
  "description": "The hour, kept by an alarm.",
  "opens": "txt",
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
| `opens` | cadena | No | 15 B | L'extensió que aquesta app sap obrir |
| `splash` | cadena | No | 47 B | La imatge que el sistema pinta abans d'arrencar l'app, relativa al directori de l'app |
| `author`, `description` | cadena | No | — | **No es llegeixen**: hi són al disc i el sistema no les fa servir enlloc encara |

Els camps que aquest document preveia i que **no existeixen al codi** —`icon`, `permissions`, `mem_kb`—
`s'ignoren com qualsevol camp desconegut: el sostre de memòria és el de la placa i els permisos encara
no hi són (docs/30). El dia que existeixin, es llegiran aquí i s'escriuran en aquesta taula.

Regles de validació, totes amb error clar i el nom del camp:

- Si falta un camp obligatori, o és buit, l'app **no es carrega** i el motiu surt per la consola.
- `entry` ha d'existir i no pot sortir del directori de l'app (ni `..` ni camins absoluts). Es
  comprova a l'escaneig, no a `run`: una app que no es pot obrir **no surt a `apps`**, i el log diu
  `apps: <id> has no entry file (<camí>)`. Una app llistada que després no arrenca seria una mentida.
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
  res per prémer. L'app `keys` és el cas: nomena les tres plaques que en tenen.
- **El banc no és cap placa i no es nomena, però diu quina és.** `sys.board` a l'emulador diu el
  **model que se li ha dit que sigui** —`--board m5stack-paper`, que és obligatori—, i
  `sys.emulator` continua cert (docs/85). Com que respon **un** nom i no tres, `boards` es compara al
  PC exactament com a la placa: una app del model corre i una d'un altre model es refusa, dient les
  plaques que vol i quina és aquesta. Cap manifest ha de dir `emulator`, que no és cap placa; si algú
  l'hi posa, no el troba enlloc i la prova que repassa el repositori ho vigila.
- `opens` s'accepta amb punt o sense (`"epub"` i `".epub"` són el mateix) i es compara **sense
  distingir majúscules**.
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
banc ha rebut amb `--board`, i `sys.emulator` és el que diu que el maquinari de sota és fals. Una app
que toca maquinari mira això abans de res i decideix el camí; mai suposa que és a la placa on es va
escriure. El detall i les regles són a [`30 · API de Lua`](30-api-lua.md#saber-on-sexecuta).

**I `boards` no és només per al `run`**: és el que decideix **què porta el disc de cada placa**. El disc
d'un entorn el munta `tools/make_disk.py` amb les apps el manifest de les quals nomena **aquella placa o la
seva família** —l'ATOM porta les genèriques (`esp32`) més `light` i `keys`, que són les que demanen el seu
maquinari, i la Paper les genèriques més `keys`—, i cada carpeta del disc és un enllaç a `apps/` de l'arrel.
El banc i les proves no són cap placa i en porten totes (docs/70, docs/80).

## Handlers d'extensió

Un manifest pot declarar `opens`: l'extensió que l'app sap obrir. `"epub"`, `".epub"` i `"EPUB"` són la
mateixa cosa (el sistema ho normalitza a minúscules i sense punt); qualsevol altra cosa es rebutja al
log i l'app queda sense handler.

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
- L'app rep el fitxer a `app.args` (`path`, `ext`, `source`) i sap qui és amb `app.id`.
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
| `on_resize(width, height)` | Quan la vista de l'app és una altra: una rotació (`ui.rotation`), un `ui.resize` o un canvi del sistema. El canvas és **nou i blanc** i el sistema no sap què hi va: tornar a dibuixar és de l'app (docs/30, docs/50) |
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
-- apps/hello/hello.lua: l'app més curta que té sentit
console.print("Hola. Escriu 'sortir' i Enter.")
while true do
  local line = console.read_line()
  if line == nil or line == "sortir" then break end
  console.print("Has escrit: " .. line)
end
```

Manifest:

```json
{ "title": "Hola", "version": "0.1.0", "entry": "hello.lua", "boards": ["esp32"] }
```

## Assets

| Fitxer | Format | Estat |
|---|---|---|
| `splash` | PNG o, més endavant, el que decideixi la placa | El format `.icn` d'M5 **no està documentat** fins a tenir un fitxer de mostra: mentrestant es dibuixa un substitut |
| `icon` | El que la placa sàpiga pintar | Igual |

Com que a la v1 no hi ha pantalla, els assets no es fan servir: es validen (que el fitxer hi sigui) i
res més.

## Instal·lació

| Via | Estat |
|---|---|
| `pio run -e m5-atom -t uploadfs` (imatge de `data/`) | És la via de la v1 |
| Copiar el directori a `/sd/apps/` | Sí: el Paper munta la targeta a `/sd` (docs/50) i `apps` i `run` la busquen abans que la flash (0.86.0) |
| Baixar-la per Wi-Fi des del prompt | Fora de la v1 |

Desinstal·lar és esborrar el directori. Les alarmes persistents d'una app que ja no hi és es
descarten a l'arrencada, amb una línia al log.
