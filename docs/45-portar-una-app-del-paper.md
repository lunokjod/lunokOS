# 45 · Portar una app de Lua del projecte del paper

Aquest document és el **procediment** per agafar una app de `~/code/PaperOS/data/app/` i fer-la
viure aquí, amb la mateixa cara. No és una traducció línia per línia: les dues bandes tenen models
d'interfície diferents i el que es porta és **la pantalla i el comportament sencers**, amb els
números de l'original.

Les que s'han fet així són `calculator` (0.121.0), `about` (0.122.0, amb la llista de biblioteques a
0.124.0) i `timeclock` (0.125.0), i tot el que hi ha aquí surt d'haver-les fet.

## 1 · Què es llegeix de l'original, abans d'escriure res

| On | Què hi ha |
|---|---|
| `data/app/<App>/manifest.json` | Títol, versió, `icon`, `opens`, `opensArgs`, `splash` i la descripció de dues línies |
| `data/app/<App>/pos_launch.lua` | L'app sencera: la maqueta, els números i les regles |
| `data/app/<App>/res/` | Els recursos que l'app porta (imatges, fonts, dades) |
| `data/app/<App>/icon.icn` | La icona ja convertida, en el format de **la seva** pantalla (94 × 94, quatre bits) |
| `tools/icons/<App>.png` | **La imatge original de la icona**: 448 × 448, grisa, la que una persona edita |
| `src/gfx/fonts/fonts.h` | Les quatre mides de la tipografia, en **píxels de línia** |
| `src/ui/button.cpp`, `label.cpp` | Les propietats per defecte dels controls (alineació, marges, alçada) |

Res d'això no s'inventa: si un número no es troba a l'original, no es posa.

## 2 · Els recursos: icones, splash i dades

- **La icona es porta amb el seu màster.** Es copia `tools/icons/<App>.png` a
  `apps/<id>/res/icon-source.png` —el fitxer que es pot tornar a editar— i es converteix amb
  `tools/make_icon.py` a `apps/<id>/res/icon.png`, que és el que el manifest nomena amb
  `"icon": "res/icon.png"`. El màster viatja amb l'app perquè el dia que es vulgui canviar el
  dibuix no s'hagi de tornar al repositori del paper.
  - El `.icn` **no es porta**: és el format de quatre bits de l'altra pantalla i aquí no el llegeix
    ningú. El que es pinta aquí és un PNG, i `ui.image` en llegeix i n'escala.
  - El tamany és **94 × 94**, el número del paper (la caixa que el seu menú dona a una icona).
  - **`tools/icons/` té vint-i-quatre dibuixos i no vint-i-quatre apps** (mesurat el 8/11): els vint-i-tres de les
    apps que en tenen —`2048`, `About`, `Agenda`, `Banner`, `Battery`, `Calculator`, `Clock`, `EPUB`, `Editor`,
    `FRAM`, `Files`, `ImageViewer`, `Jellyfin`, `Life`, `Meteo`, `News`, `Pacman`, `Password`, `SerialFUSE`,
    `Snake`, `Tamagotchi`, `Tetris` i `Timeclock`— i `Example.png`, que és **el dibuix per començar-ne un** i no
    una app. **`Paint` no hi és perquè l'original tampoc no en porta**: el seu `manifest.json` no té `icon` i la
    carpeta no té `res/`, o sigui que la seva icona, si un dia se'n vol una, s'ha de dibuixar i no s'ha de buscar.
- **El `splash` no es copia: es torna a compondre** (0.151.17, i abans es copiava tal com era). De les
  vint-i-quatre apps del paper, només l'EPUB en tenia un, i el fitxer era el que havia fet
  `tools/splashgen.py` **a partir del dibuix de la icona**: aquí la recepta és `tools/app_splash.py` i
  el splash es fa amb una ordre des del màster, no s'arrossega un PNG que ningú sap refer.
  - `"splash": "res/splash.png"` al manifest, i el fitxer a `res/`.
  - `python3 tools/app_splash.py --icon apps/<id>/res/icon-source.png --title <Títol> --foot lunokOS
    --out apps/<id>/res/splash.png` — 420×520, la icona a 232 px, una ratlla, el títol, `loading...` i
    el peu.
  - **El peu és el nom del sistema**, i és l'única llibertat: el de PaperOS deia `PaperOS` i aquí diu
    `lunokOS` (`--foot PaperOS` dona l'original de tornada). La mida no es toca: l'artwork és el mateix
    i l'aspecte que l'amo va triar allà és el que ha de veure's aquí.
  - El splash **surt abans de carregar l'app i abans de la màquina de Lua** (0.151.17: l'arrencada té
    dues meitats i la imatge va entremig, docs/40), o sigui que un PNG de 7 KB es veu molt abans que un
    lector de 50 KB hagi començat.
- **La resta de `res/`** es copia sencera quan l'app la fa servir: imatges (`ui.image` en llegeix
  els mateixos formats), dades i fitxers de text.
- **Un recurs que és codi no viatja com a codi** (0.124.0): `libs.lua`, allà, és un fitxer que el build
del paper genera llegint l'arbre de codi, i aquí no es pot executar —**`dofile`, `loadfile` i `load`
són `nil` a l'estat d'una app** (docs/20)—. El que es porta és **el generador**, i el que llegeix l'app
és **dades**: `tools/gen_about.py` llegeix el `library.json` de cada carpeta de `lib/` i escriu
`apps/about/res/libs.txt`, una biblioteca per línia i un tabulador entre camps, que és el que la pàgina
parteix. `--check` diu si el fitxer és vell, i una carpeta de `lib/` sense `library.json` atura l'eina
amb el seu nom.

## 3 · Les mides i els controls: el mateix lloc, les mateixes propietats

**Les mides de la lletra es demanen en píxels de línia, i mai en passos.**

| Nom del paper | Píxels de línia | Cara d'allà | Aquí |
|---|---|---|---|
| `small` | 33 | `ui28b` (28 de cos) | `scale_for(33)` → 3 passos amb la negreta del disc |
| `body` | 43 | `ui36b` (36) | `scale_for(43)` → 4 |
| `title` | 57 | `ui48b` (48) | `scale_for(57)` → 5 |
| `big` | 82 | `ui70b` (70) | `scale_for(82)` → 7 |

```lua
-- Un pas de la cara fa onze píxels de línia amb la negreta del disc i set amb la taula del flash: el
-- que es demana és la mida, no el pas, i així la pàgina fa la mateixa mida física a les dues bandes.
local function scale_for(line_px)
  local step = ui.line_height(1)
  if step <= 0 then step = 1 end
  return math.max(1, math.min(16, math.floor(line_px / step + 0.5)))
end
```

De la resta de propietats, el que s'ha de copiar tal com és:

- **Els marges i els forats dels panells**: `set_padding(20)` i `set_gap(12)` de la llista, `20` i
  `14` del detall, són números de l'app i es mantenen.
- **L'alçada d'un botó** és l'aritmètica de l'altre costat: `lineHeight() + 2 * padY`, amb
  `padY = 10`. Amb la mida del cos són 63 px allà i 64 aquí.
- **L'alineació per defecte d'un botó és el centre** (la propietat i el valor), i un label el
  `valign` del qual és el mig.
- **Els grisos**: un fons de 12 o un text de nivell 8 volen dir el mateix a les dues bandes (setze
  nivells), o sigui que els números es copien.

## 4 · La taula d'equivalències

| Al paper | Aquí | Nota |
|---|---|---|
| `paperos.draw.clear/rect/frame/line/text` | `ui.clear/rect/frame/line/text` | Les mateixes primitives; `mode` és la qualitat de la tinta |
| `paperos.draw.text_width` / `text_height` | `ui.text_width` / **`ui.line_height`** | `ui.text` posa el cim de la línia a `y` |
| `paperos.damage` + `paperos.paint` | `ui.present` + `ui.idle` | El dany és el que s'ha dibuixat des de l'últim `present` |
| `paperos.redraw` | `ui.invalidate` / `ui.clear` | Un control se senyala sol quan canvia el text |
| `paperos.display.width/height` | `ui.caps().width/height` | La vista comença sota la barra del sistema |
| `paperos.tick` | `sys.delay(ms):await()` | El nucli no es bloqueja |
| `paperos.millis` | `sys.time_ms()` | Monòton |
| `paperos.storage.*` | `fs.*` | `list`, `stat`, `read`, `write`, `mkdir`, `remove`, `rename`, `size`, `free`, `units` |
| `paperos.files.load/read` | `fs.read` **i res més** | `load` és `nil` aquí: un recurs que és codi s'ha de portar com a dades (vegeu la trampa 8) |
| `paperos.storage.preferred/locate` | `fs.cfg()` / `fs.data()` | El terreny de l'app és `/data/<id>` |
| `paperos.prefs.get/set` | `fs.cfg()` escrivint un fitxer | No hi ha `store` encara (docs/90) |
| `paperos.ui.*` | `ui.*` | Els controls són els mateixos noms; el que canvia és `set_x` → camp `x` |
| `paperos.keys.left/right/center` | `on_key` / `on_button` / el dial del sistema | Els controls enfocables ja el reben |
| `paperos.alarm.*` | `alarm.*` | `at`, `after`, `cancel`, `list`, `caps` |
| `paperos.time.*` | `time.*` | `now`, `now_ms`, `utc`, `local`, `format`, `caps` |
| `paperos.notify` | `ui.notification_bar` | |
| `paperos.info` | `ui.dialog` | |
| `paperos.image.load` | `ui.image` / `ui.image_info` | |
| `paperos.font.load` | `ui.load_font` | `.lfont`, `.bdf`, `.pcf` o `.ttf` |
| `paperos.net.*` | `net.*` i `http.get` | |
| `paperos.system.version` | `sys.version` | |
| `paperos.args` | `app.args.path` | `run <id> keys` és l'equivalent de `-keys` |
| `paperos.opened_file`/`opener` | `app.args` i `open` | `opens`/`opensArgs` del manifest |
| `paperos.session.get/set` | `app.save`/`app.restore` ⏳ | Encara no hi són |
| `paperos.launch` | — | **No hi ha** manera d'arrencar una app des d'una app |
| `paperos.serial.*`, `paperos.zip.*`, `paperos.epub.*` | — | No hi són: són feina de sistema |
| `paperos.fram.*` | `fs` sobre `/fram` | |

## 5 · Les trampes, que són totes d'aquest sistema

1. **No hi ha `visible`.** Una pàgina de dues pantalles es construeix i es dona amb `release()`; els
   fills se'n van amb el pare i el control que marxa **no es compon**, o sigui que no queda cap
   fantasma de la pàgina anterior. No hi ha manera d'amagar un control.
2. **Un `ui.label` no parteix les línies**: el que no hi cap s'acaba amb `...`. Una app que porta
   paràgrafs els parteix ella mateixa amb `ui.text_width`.
3. **La cara del sistema només dibuixa ASCII imprimible.** `←`, `±`, `×` i `÷` no es poden pintar:
   la calculadora fa servir `<`, `+/-`, `*` i `/` fins que una cara porti aquests glifs.
4. **El dit no és un `tap` si l'app necessita pressa.** El sistema diu `tap` 350 ms després que el
   dit marxi i **fusiona dos tocs** dins d'aquella finestra en un `double`: una calculadora llegeix
   `touch.read()` i prem al primer frame d'un contacte. Els gestos que no es fan servir s'han de
   consumir igualment, o la cua creix.
5. **La pàgina és de l'app sencera**: els controls pinten dins del seu marc, i l'aire del voltant es
   queda amb el que hi havia al vidre si l'app no fa un `ui.clear(15)` abans de construir.
6. **Enfocar no és el mateix que prémer**: el primer control enfocable d'una pàgina neix amb
   l'anell, i el centre del dial és el clic. És el que fa que una prova pugui provar una pàgina
   sense cap dit.
7. **Els ganxos són els que hi ha**: `on_open`, `on_resize`, `on_alarm`, `on_button`, `on_light`,
   `on_pause`, `on_save`, `on_close`. Un `on_back`, un `on_event` o un `on_draw` global d'allà no
   existeixen aquí (docs/40).
8. **Una app no pot executar un fitxer**: `dofile`, `loadfile` i `load` són `nil` (docs/20). Si
   l'original llegeix un módul de dades amb `files.load`, aquí el que es porta és el **format**: el
   generador escriu dades i l'app les parteix (`about` i el seu `libs.txt`, 0.124.0).

## 6 · Les proves, i la versió de l'app

- **Cada port porta una prova que arrenca l'app de debò** (`test_apps`, docs/80): es copia l'app del
  repositori al disc de la prova, es corre amb `run <id>`, es premen les tecles o es toca el vidre
  de l'emulador, i es llegeix el que l'app diu per consola. Una app que no s'ha arrencat mai no està
  portada.
- **L'app puja la seva pròpia versió** (`version` del manifest) cada cop que canvia, com el sistema
  puja la seva: la calculadora va néixer en 0.1.0 i és a 0.2.0, i `about` va d'0.1.0 a 0.3.0. Una app
  que canvia i es queda amb el mateix número està mentint sobre el que porta.
- **I la versió del sistema** puja també en el mateix commit (llei 10 del README).

## 6 bis · El port gran: `timeclock` (0.125.0)

`Timeclock` és la primera app portada que **no cap en un fitxer** de l'original: allà eren `pos_launch.lua` (1 187
línies), `day.lua` (660) i `week.lua` (163), amb els dos mòduls carregats per `files.load()`. Cinc coses que aquest
port va ensenyar i que valen per al següent:

- **Un mòdul no viatja com a mòdul.** `dofile`, `loadfile` i `load` són `nil` a l'estat d'una app (docs/20), o sigui
  que `day.lua` i `week.lua` **no es poden carregar**: les tres parts són un sol fitxer de 1 250 línies, amb l'ordre
  de sempre —dades, dibuix, pàgina— i el capçal de cada part dient de quin fitxer de l'original surt.
- **El preu de la font es diu al pitjor moment i no a la pàgina.** L'app fa **44 545 B** i el sostre de font de la
  shell era de 32 768 B (`too big to run`): el sostre és **49 152 B** des d'aquesta versió (i **65 536 B** des de la
  0.143.0, docs/75), i no costa RAM mentre
  l'app corre perquè el bloc es torna quan el chunk ja està compilat (docs/75). El chunk que queda són **21 578 B**
  sense noms de depuració.
- **Dibuixar un gràfic és un `ui.canvas`.** L'original el pintava a `on_draw()`, que aquí no existeix; un canvas amb
  `on_draw` propi fa la mateixa feina i a sobre té el seu buffer i el seu dany (docs/30). Les quatre tintes
  (paper 15, graella 11, descans 8, feina 0) hi caben a la mateixa taula de setze grisos.
- **Una icona de la zona privada d'Unicode no es pot portar.** Les tres tecles de l'original eren glifs U+F04B, U+F04C
  i U+F04D d'un `keys.pof` propi, i aquest sistema només coneix `lfont`, BDF, PCF i TTF **de l'ASCII imprimible**: les
  tecles diuen `Start`, `Pause`/`Resume` i `End`, que és el que l'original mateix feia quan li faltava la cara.
- **L'hora d'una persona no és l'hora del xip**, i això va ser una crida nova del sistema: `time.utc()`, `time.date()`
  i `time.tz()` amb `cfg tz +01:00` (docs/30, docs/60). Una app que diu «de 7 a 19» ha de dir-ho en l'hora de qui la
  llegeix, i el desfasament és una cosa que el sistema ha de saber, no cada app.

**I el que sí que es va poder copiar sencer**: el format del fitxer d'apunts (`2026-11-01|09:00:00|S`, només creix,
el dia es llegeix com el cul del fitxer), el de les notes (un fitxer a part perquè una nota s'edita i una edició és
una reescriptura), la finestra de feina de 07:00 a 19:00 que retalla cada interval abans de sumar-lo, el dia que es
pinta (l'últim amb apunts i no avui) i la mitjana dels dies que tenen apunts. Les mides tornen a ser les de l'original
en píxels de línia: `small` 33, `body` 43 i `title` 57, i la fila d'una tecla fa 84 px.

## 7 · La llista, en ordre

1. Llegir el manifest, l'entry i el `res/` de l'original (i la icona del màster).
2. Copiar els recursos: `icon-source.png`, la icona feta amb `tools/make_icon.py`, el `splash`
   **composat** amb `tools/app_splash.py` si n'hi havia un, i la resta de `res/`.
3. Escriure l'app amb la **maqueta** de l'original: panells amb el mateix `padding`/`gap`, controls
   amb les mateixes propietats, mides en píxels de línia.
4. Escriure el manifest: `title`, `author`, `version` (**0.1.0 la primera vegada**), `entry`,
   `boards`, `icon`, `splash` si n'hi ha, `description`.
5. Fer-la córrer a l'emulador: `--flash build/disk/emulator --sd` l'arbre, i mirar el log i els pintats.
6. Escriure la prova a `test_apps` i compilar-la (executar-la vol permís, docs/80).
7. Posar-la a la llista de la placa si hi ha de viatjar (`custom_apps`, docs/70).
8. Pujar la versió del sistema i escriure el commit amb els números mesurats.

## 8 · El conjunt d'apps, i què hi ha portat (8/11)

**Les apps d'aquest repositori són les del projecte del paper, portades d'una en una.** El conjunt és el de
`OLD/PaperOS/data/app`, que té **24** carpetes, i no se n'inventa cap altra: una app que no hi és no és una app
d'aquest sistema. Les 60 carpetes que `apps/` tenia abans del 8/11 es van esborrar perquè no venien d'allà, i
l'esborrat és al commit de la 0.152.6 amb el número del davant.

**L'excepció és el llançador** (0.153.0): `home_eink` no ve del projecte del paper perquè allà el menú d'inici és
**del sistema** —una pantalla en C++ del nucli, no una app de Lua— i aquí és una app, amb les seves crides
(`apps.list()` i `app.run`, docs/30). És l'única app d'aquest arbre que no és un port, i el dia que PaperOS en
tingui una de Lua es compara amb aquesta.

Portades i viatjant avui, **cinc** (`home_eink` va a part, a sota):

| Original | Id aquí | Versió |
|---|---|---|
| `About` | `about` | 0.1.0 |
| `Calculator` | `calculator` | 0.1.0 |
| `Editor` | `editor` | 0.1.0 |
| `EPUB` | `reader` | 0.1.0 |
| `Timeclock` | `timeclock` | 0.1.0 |

I a part, **dues que no són ports**:

- **`home_eink`** (`title` *Home*, 0.2.0), el llançador: la graella de les apps del disc amb la icona i el nom de
  cadascuna, i un dit o el centre sobre una cel·la arrenca aquella app. No llista els llançadors —ni a si mateix—,
  i és la pàgina on la persona torna quan l'app que ha obert s'acaba (0.153.0). El seu manifest diu
  `"launcher": true` i la seva icona és el novè dibuix de `tools/icons/` que s'ha fet per a ella (`Home.png`).
  **Les pàgines són seves** (0.154.1): la graella és un tros dels noms —les cel·les de les altres pàgines no arriben
  al control, perquè una graella del sistema amaga les que no hi caben i la pàgina no les segueix quan el cercle és
  a les cel·les— i la banda del peu té dues fletxes i el número de la pàgina (`1 / 1` quan tot hi cap, amb les dues
  fletxes apagades). La pàgina es torna de dues maneres: el dit a sobre d'una fletxa, o el dial fins a la fletxa i el
  centre; la volta la fa el bucle de l'app i no el clic, perquè el control que l'ha pres no es pot donar a si mateix
  mentre corre.
- **`probe`**, que **no és una pantalla**: és un instrument temporal de l'emulador que repeteix l'arrencada del lector
  per estacions i diu quina no ha sobreviscut (0.151.12). Marxa quan ja no calgui, i el seu id surt de la llista
  de la prova al mateix commit.

Les **dinou** que falten, i que són la cua d'aquesta feina: `2048`, `Agenda`, `Banner`, `Battery`, `Clock`,
`FRAM`, `Files`, `ImageViewer`, `Jellyfin`, `Life`, `Meteo`, `News`, `Pacman`, `Paint`, `Password`, `SerialFUSE`,
`Snake`, `Tamagotchi` i `Tetris`.

La prova `test_every_app_of_the_repository_declares_its_boards` porta **la llista dels ids que hi ha d'haver a
`apps/`** i comprova que tots s'han auditat: quan en portis una, l'id s'hi afegeix en el mateix commit. Si una app
que la placa porta (el `custom_apps` de `platformio.ini`) desapareix, el cas es posa vermell i diu quin id falta.

**Una conseqüència que es tanca sola**: aquest document, `docs/40`, `docs/50` i `docs/90` nomenen apps de la
llista d'abans (per exemple `clock`, `battery` o `checkbox`) com a exemples. Cada nom torna a ser cert quan la
seva app es porta; el que no es fa és inventar-se l'app perquè el text quedi bé.
