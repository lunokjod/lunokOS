# 45 · Portar una app de Lua del projecte del paper

Aquest document és el **procediment** per agafar una app de `~/code/PaperOS/data/app/` i fer-la
viure aquí, amb la mateixa cara. No és una traducció línia per línia: les dues bandes tenen models
d'interfície diferents i el que es porta és **la pantalla i el comportament sencers**, amb els
números de l'original.

Les dues primeres que s'han fet així són `calculator` (0.121.0) i `about` (0.122.0), i tot el que hi
ha aquí surt d'haver-les fet.

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
- **El splash, si en té.** El `splash` d'un manifest d'allà és el mateix camp que el d'aquí: es
  copia el fitxer a `res/` i es declara amb el nom tal com era. De les vint-i-quatre apps del paper,
  només l'EPUB en té un.
- **La resta de `res/`** es copia sencera quan l'app la fa servir: imatges (`ui.image` en llegeix
  els mateixos formats), dades i fitxers de text.
- **`libs.lua` no es porta mai**: és un fitxer que el build del paper genera llegint l'arbre de codi,
  i aquí el disc no porta l'arbre. L'app que el vol dirà la veritat que sí que pot dir (vegeu
  `about`).

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
| `paperos.files.load/read` | `dofile` / `fs.read` | `files.load` era un chunk de dades |
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

## 6 · Les proves, i la versió de l'app

- **Cada port porta una prova que arrenca l'app de debò** (`test_apps`, docs/80): es copia l'app del
  repositori al disc de la prova, es corre amb `run <id>`, es premen les tecles o es toca el vidre
  del banc, i es llegeix el que l'app diu per consola. Una app que no s'ha arrencat mai no està
  portada.
- **L'app puja la seva pròpia versió** (`version` del manifest) cada cop que canvia, com el sistema
  puja la seva: la calculadora va néixer en 0.1.0 i és a 0.2.0, i `about` igual. Una app que canvia
  i es queda amb el mateix número està mentint sobre el que porta.
- **I la versió del sistema** puja també en el mateix commit (llei 10 del README).

## 7 · La llista, en ordre

1. Llegir el manifest, l'entry i el `res/` de l'original (i la icona del màster).
2. Copiar els recursos: `icon-source.png`, la icona feta amb `tools/make_icon.py`, el `splash` si
   n'hi ha, i la resta de `res/`.
3. Escriure l'app amb la **maqueta** de l'original: panells amb el mateix `padding`/`gap`, controls
   amb les mateixes propietats, mides en píxels de línia.
4. Escriure el manifest: `title`, `author`, `version` (**0.1.0 la primera vegada**), `entry`,
   `boards`, `icon`, `description`.
5. Fer-la córrer al banc: `--flash build/disk/emulator --sd` l'arbre, i mirar el log i els pintats.
6. Escriure la prova a `test_apps` i compilar-la (executar-la vol permís, docs/80).
7. Posar-la a la llista de la placa si hi ha de viatjar (`custom_apps`, docs/70).
8. Pujar la versió del sistema i escriure el commit amb els números mesurats.
