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

- **Com es dibuixa una icona, que és el que fa que surtin bé**: el projecte del paper no tenia cap model dibuixant, tenia
  `tools/iconart.py` —**el dibuix és codi**— per a les icones de les apps, i **les marques de la interfície les treia d'un
  clipart**: la família **Font Awesome**, que el seu `tools/fontgen.py` rasteritzava. Aquest repositori en porta la mostra a
  `tools/make_keys.py` (les cinc lletres del rellotge, del mateix `fa-solid-900.ttf`).
  - **Per a les icones d'una app** la regla del paper és la que dona el to: es dibuixa a 448 unitats i es redueix **d'una sola
    passada amb LANCZOS** (les vores porten els grisos del panell en lloc d'una escala de píxels), la vora és el dibuix —15
    unitats, 2,1 px a 64; amb 8 la reducció se la menja—, i la paleta té nom: cos de 8 a 13, vora a 4, paper a 15.
  - **I per a les marques del sistema no es dibuixa res**: `tools/iconart.py` rasteritza els glifs de **Font Awesome Free
    5.15.4** (`angle-left`, `times`, `cog`, `redo`, `power-off`, `moon`, `house` i **`lock`**; `--only <nom>` en refà una de
    sola, que és com es va afegir la casa el 16/11 i el candau el 17/11 sense tocar les que ja hi eren), els ajusta al 80 % de
    la caixa i en desa el mestre a `tools/icons/system/` i la icona de 64 a `data/res/icons/`, que és el que el disc porta i el
    que `ui::picture_of_a_mark` llegeix (docs/30, docs/92). **Llicències**: els glifs són SIL OFL 1.1 i les icones CC BY 4.0, amb
    el crèdit al `README`.
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
| `Selector` + **`OptionPicker`** | `ui.selector` + **`ui.picker`** (0.189.102) | El camp és la porta i la targeta és del sistema: el títol, les files d'una pàgina, les columnes que demana el text més llarg i **les dues fletxes del peu** |
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

## 5 bis · La vista es recorda: la sessió `state` (0.189.12)

**Tota app amb pàgina porta una sessió que es diu `state`, i és el que fa que la pàgina torni com la va deixar la
persona**: el control amb el cercle, la pàgina de la llista, la pestanya, les caselles i el que hi ha escrit a les caixes.
El contracte sencer, amb el format i el codi que es copia, és a **docs/30 § «La sessió `state`»**; aquí només hi ha la
llista de què s'hi ha de mapejar en portar una app:

| El que té l'original | Què va a `state` |
|---|---|
| el control enfocat | `focus=<nom>` — pel nom de l'app, mai pel número del control |
| la pàgina d'una llista o d'una graella | `page=<n>`, i `marked=<n>` si la tria és part de la vista |
| la pestanya triada | `tab=<n>` |
| una casella | `check:<nom>=0|1` |
| una caixa de text | `text:<nom>=<text>`, i `scroll=<n>` si és de diverses línies |
| **el tema, la unitat, l'idioma, l'ordre triat per la persona** | **no hi va**: és preferència, i viu a `/cfg` o a `fs.data` |
| **el document obert, la partida, les notes** | **no hi va**: és dada de l'app, i té el seu fitxer |

I queda dit com a norma: **cada cop que es toca el codi d'una app es repassa aquesta sessió** —que desi on toca, que
llegeixi amb el valor acotat, i que `kLayout` pugi si algun control ha canviat de nom—. **I el bloc és d'una tirada**: el nucli
esborra el `state` de totes les apps a cada arrencada de debò (0.189.167), o sigui que una app que torna a la seva forma ho fa
dins de la mateixa encesa i mai a través d'un reinici; **l'excepció és la tornada d'un son profund** (0.189.176), on la pàgina
ha de ser la que la persona va deixar i el `state` és el que la fa (docs/30 § la sessió `state`, docs/60).

**Quin full hi ha de mapejar què, i quins no hi han de mapejar res** (repàs complet, 0.189.52, i la calculadora des de la
0.189.165): la sessió `state` la tenen avui **divuit de les vint-i-tres apps** del disc, i les cinc que no la tenen és una decisió
dita al fitxer i no un oblit.

| App | Què va a `state` |
|---|---|
| `home_eink` | `focus` = l'**id de l'app** de la cel·la amb el cercle (i amb ell la pàgina de la graella) |
| `about` | `page` del llistat, `focus` = el **nom de la llibreria** amb el cercle i `marked` = la llibreria amb el detall obert |
| `fonts` | `page`, `focus` (el fitxer amb el cercle) i `marked` (l'exemplar obert) |
| `shell` | `tabs`, `tab` i `text:t<n>`/`text:l<n>` (el títol i les últimes línies de cada pestanya) |
| `timeclock` | `page` (el dia o la setmana) i `marked` (la marca amb la nota oberta) |
| `splash` | `page` del llistat i `focus` = el **nom del moment** amb el cercle |
| `tz` | `focus` = quin dels tres controls del sistema tenia el cercle (els valors són preferències i viuen a `/cfg`) |
| `life` | `page` (el tauler o els ajustos) i `focus` (el control amb el cercle); les dues preferències són a `fs.data` |
| `editor` | `scroll` (la primera fila visible) i `focus`; **el document no hi és**, que és dada amb el seu fitxer |
| `reader` | `focus` = el **camí del llibre** amb el cercle al prestatge; la posició dins del llibre ja és al seu fitxer |
| `wipe` | `focus` = la porta amb el cercle; **mai la targeta d'una pregunta** |
| `settings` | `tab` (la pestanya on era) i `focus` (el nom del control amb el cercle); **cap ajust no hi va**, que són de `/cfg` |
| `calculator` | `entry`/`acc` (el número que s'està teclejant o l'acumulat), `pending` (l'operació que espera el segon operand), `fresh` i `failed` —el que la pantalla diu, amb la línia de dalt— i **una línia `h=` per operació de l'historial** (0.189.165, l'amo: *«la calculador ha de tenir també session state, per el número de la pantalla i l'historial d'operacions»*). La 0.189.52 havia dit que no hi anava i el que ho va fer canviar és que una calculadora que es torna a obrir amb un zero i sense l'última suma no és la que la persona va deixar; **l'historial no és un document** —un document ha de sobreviure a `sessions destroy` i va a `fs.data`— i se'n guarden les últimes dotze |
| `agenda` | **la pàgina sencera i el que s'hi està fent** (0.189.171): `page` (el mes, el dia o el formulari), `day`/`y`/`mo` (el dia obert i el mes que el calendari ensenyava), `marked` (l'esdeveniment amb el cercle), `editing` (el número de l'esdeveniment que el formulari escriu, o 0 per a un de nou), i les tries del formulari —`hour`, `min`, `len`, `rep` i `formy`/`formmo`/`formd`— amb **`text:title` i `text:note`** i `check:all_day`, que és el que la persona havia escrit i el que no s'ha de perdre quan la pàgina es reprèn. **Els esdeveniments no hi són**: són dada de l'app i tenen el seu fitxer a `fs.data` |
| `appinfo`, `battery`, `clock`, `meteo` | **el que la seva pàgina és**: `appinfo` la pestanya i el detall obert (`page`, `focus`, `marked`), `battery` el cercle i prou —**la finestra del gràfic és una preferència i viu a `fs.data`**, que és la línia que separa la forma del gust—, `clock` la pestanya dels quatre fulls, el cercle i la fila triada (`tab`, `focus`, `marked`), i `meteo` el focus dels tres botons de la finestra —**la finestra triada i el registre de mostres són a `fs.data`**, i el registre no és forma sinó història— |
| `2048`, `brokenapp`, `device_lock`, `luaperf`, `rainbow` | **res**: no tenen cap control amb cercle ni cap pàgina que tornar (les setze cel·les del 2048 són la partida, `rainbow` no té ni `ui`, `brokenapp` és un full de set botons que no recorda res, el pany és una pàgina que es tanca sola i `luaperf` és un instrument de mesura). Un bloc buit seria un kilobyte per no dir res |

**I el que va fer possible que totes aquestes tornessin de debò** (0.189.52): el bloc es llegia amb els zeros del darrere
—`read(0, 1024)` torna els mil vint-i-quatre bytes i el que s'hi ha escrit és més curt—, o sigui que l'última línia no
valia res: `tonumber` en treu `nil` i un nom deixa de ser el nom. El tros que talla el text a la primera marca de zero és
ara part del codi que es copia (docs/30), i per això `fonts` i `shell` també tornen on toca.

**La primera que l'ha estrenada és la galeria de tipus de lletra** (0.189.27): `fonts` desa `page` (la pàgina del
prestatge), `focus` (la fila amb el cercle, **pel nom del fitxer**) i `marked` (la fila **amb el seu exemplar obert**, que
la llista pinta amb la seva pròpia marca), llegeix el bloc **abans de col·locar res** i acota el que hi troba —una pàgina
que ja no existeix, un nom que ja no és a la porta—, desa a cada canvi de forma (girar la pàgina, obrir un exemplar,
tornar-ne) i a `on_pause`, `on_save` i `on_close`, i **obre l'exemplar que la persona tenia al vidre** quan torna. És la
referència per a la resta, i el llançador fa el mateix dibuix amb la cel·la del cercle des de la 0.189.52.

**I el terminal del sistema la segueix** (0.189.29): `apps/shell` desa `tabs`, `tab` i un `text:t<n>`/`text:l<n>` per
pestanya —el títol i **les últimes línies** del terminal—, o sigui que una pàgina que és text hi posa text i no només
números. **La sessió no hi cap i no s'hi fingeix**: un tub d'una app que s'ha anat és un tub de ningú, així que el segon
obre obre sessions noves i el que torna és la memòria del que s'hi va llegir (docs/30).

**I el rellotge també** (0.189.51): `apps/timeclock` desa `page` —el dia o el full de la setmana— i `marked` —la marca
amb la nota oberta—, els llegeix abans de construir res, els acota amb `tonumber` i els desa al canvi de full, en triar
una fila, en posar una marca i a les tres portes del comiat. **No hi ha cap preferència a dins**: les marques són dada de
l'app i viuen a `/data/timeclock/marks.txt`, i l'hora i la bateria no hi són. És una app que es tanca des del full de la
setmana i torna al full de la setmana, que és exactament el que la norma demana d'una pàgina de dues cares.

**I el llançador, que és la pàgina que més torna** (0.189.52): `home_eink` desa `focus` = l'id de l'app de la cel·la amb el
cercle, i amb això la graella torna també al full que la persona mirava —la pàgina d'una graella és la del seu `index`—.
El cercle el mou el sistema (0.179.4), o sigui que el llançador no té cap `on_focus` per cel·la: **el torn del bucle és
l'únic lloc que sap on és el dial**, i compara `grid.index` amb el que ja ha desat abans de copiar-lo al bloc. Un id que el
disc ja no té és la primera cel·la, que és el que fa una pàgina sense record.

## 5 ter · El que corre: el cronòmetre i el compte enrere (0.189.103)

**Un número que corre no és la forma de la vista i no va a `state`**: va a la dada de l'app, amb el seu fitxer. I el que
hi va no és el número, sinó **el moment del món en què va arrencar** —`time.now_ms()`, l'època de la pólvora de la
placa— més el que ja comptava, perquè és l'única parella que es pot tornar a llegir després d'un reinici.

```
chrono	run|hold	<el que ja comptava>	<època en què va arrencar>
timer	run|hold	<el que quedava>	<època en què va arrencar>
pick	<minuts>	<segons>
```

- **La tirada es mesura amb `sys.time_ms()`**, que és monòton i no salta quan arriba una sincronització NTP; `time.now_ms()`
  només serveix per creuar el forat d'un reinici, i quan la placa no té hora **es diu** en lloc d'inventar-se una estona.
- **`alarm.at` del compte enrere porta `persist`**, com l'alarma diària (`repeat`): una promesa que no sobreviu a un
  desendollament és una promesa que no sona, i la promesa es torna a deixar **després** d'esborrar les velles.
- **Cada canvi de la marxa va al disc abans de pintar** (arrencar, aturar, posar a zero), i en tornar l'app hi torna a
  escriure el que ha llegit, amb l'ancora nova: el valor i l'ancora es mouen junts o el proper arrencada el compta dues
  vegades.
- **El que la persona havia triat** (els minuts i els segons d'un compte enrere) també és al mateix fitxer: no és una
  preferència, però tampoc no és la forma d'un control que es pugui tornar sola. **I un camp sense triar és un zero** (0.189.104):
  una durada no demana els dos camps, només no admet que sigui de res.

**On és, a les apps del disc**: `apps/clock` (el cronòmetre i el compte enrere) i `apps/timeclock` (les marques, que ja hi
eren des del port i es llegeixen amb `time.now()`, i per això una estona que travessa un reinici encara és la que va ser).

## 5 quater · El panell d'ajustos: una pàgina per temes (0.189.106)

**El panell d'ajustos del sistema no ve de cap app del paper: és la pàgina que l'amo va demanar per als ajustos d'aquest
sistema** —*«Lua app de Settings de sistema … Farem un panell de configurció clàssic, amb pestanyes agrupant controls per
temática, butons, textbox, togglebox, desplegables…»*— i és `apps/settings`, que és d'administrador (`"admin": true`)
perquè la privacitat toca el pany de la pantalla. El que fa és tot del sistema: **cap control pintat a mà**.

- **Les pestanyes són una tira del sistema de més d'una fila** (0.189.106): set pestanyes en 156 píxels són tres files de
  tres cel·les de 172, i el dit que cau a la segona fila tria una pestanya de la segona fila. **La pestanya on era la
  persona va al `state`** (`tab=`), i amb ella el control amb el cercle (`focus=`), mai el número d'un control.
- **Cada fila és un control del sistema**: un `ui.checkbox` amb la paraula a dins (les paraules **es col·loquen a
  `cb:box() + 10`** i la seva amplada és la que queda, que és el que fa que el text no es mengi la càpsula), un
  `ui.selector` que obre un `ui.picker`, un `ui.button` per a les accions i un `ui.row` per al que només es llegeix
  (`title` i `detail`).
- **Les preguntes són una targeta**: `ui.dialog` amb el títol, el text que s'ajusta sol i les dues portes. **La targeta no
  arriba mai al fons de la pàgina**: mentre és a sobre el dit i el dial són seus, i l'única sortida sense triar és un tros
  de paper al costat —una targeta que acaba a la vora de baix no deixa cap paper on tocar—, o sigui que el panell li
  deixa quaranta píxels de paper a sota (més l'alçada de la línia d'ajuda).
- **El que una fila ensenya es torna a llegir del sistema**: el camp d'una tria guarda l'`index` que l'app li va donar, i
  una escriptura feta dins d'una targeta el deixaria dient el valor vell. Cada vegada que una fila es col·loca, l'app
  demana el valor a la porta (`cfg.*`, `time.*`, `power.cpu()`) i li posa: **el que es veu és el que el sistema ha
  acceptat**, també quan ha refusat el que l'app li demanava.
- **Un canvi de mida torna a fer la pàgina sencera** (i això és el sostre d'aquesta mena de pàgina): un `ui.full_screen`
  o el pany de la pantalla fan **una vista nova**, i una vista nova dona a cada canvas de la pàgina el que tenia —els
  controls moren—. O sigui que cada escriptura que pot moure el vidre passa per un `settle()` que compara `ui.caps()` amb
  la mida que la pàgina té: si és la mateixa, col·loca i pinta; si no, torna a construir-ho tot i torna a posar el cercle
  al control on era. Una pàgina que només fa `layout()` després d'un canvi de mida peta amb `attempt to perform arithmetic
  on a nil value` al primer `cb:box()` —ho va trobar la prova del panell, no la persona— (docs/30, docs/90).
- **La pestanya del processador és una mesura i no una opinió** (0.189.107 i 0.189.108, l'amo: *«Número d'instruccions per
  cicle … Cal fer mesures amb Lua per torn, donar velocitat lenta, normal, ràpida»* i *«el número son instruccions de la máquina
  virtual LUA pot executar per torn … si pot ser agafa de les estadístiques els tempos de les apps lua, així es pot valorar
  quin es el numero mágic»*). El commutador és `power.cpu_auto()`, les tres caselles són `cfg.freq(<mhz>, on)` —els passos entre
  els quals el governador pot moure's—, el desplegable de la velocitat fixa un pas (`power.cpu`) i el del torn dona **tres
  marxes amb nom** (Slow 5 000, Normal 20 000 i Fast 50 000) i no un número sol, perquè el número és **les instruccions de la
  màquina de Lua que val un torn abans que el sistema el recuperi per al vidre i les tecles**.
- **I les xifres surten de dues bandes, totes dues mesurades en aquesta placa**: una volta de `k = k + 1` és tres
  instruccions i el temps de vuit mil voltes (la més barata de tres) diu què costa una instrucció amb el torn del nucli a
  dins, o sigui que `sys.turn() / 3 × µs_per_volta` és la fila «a turn of 20 000 = 2 ms»; i **`sys.stats()`** dona els
  comptadors de debò, que són tres files:
  - **«A whole turn»** és la mitjana de `kernel.loop`: **el que triga un torn de tot el sistema**, amb l'app, els
    dispositius, el rellotge i l'espera a dins (0.189.148). És el número que la fila de dalt només *pronostica*: allà
    surt del preu d'una instrucció i de la llesca triada, i aquí surt del rellotge;
  - **«Of that, waiting»** és `queue.wait` entre els torns: el tros d'aquell mateix torn que la placa va passar **aturada
    a la cua d'esdeveniments**. Sense aquesta fila el número enganya —en una placa quieta gairebé tot un torn de deu
    mil·lisegons és espera, i es llegeix com una CPU saturada—, i el tros de treball és el que queda;
  - **«Of that, Lua»** és `lua.run` entre els torns: què hi ha de l'app a dins.

  Les tres files són la mateixa moneda a la placa i les parts hi caben dins del torn. **A l'emulador no**: `kernel.loop`
  es mesura amb el rellotge del PC, i la línia del registre ho diu («the clock of this PC, not the board's») perquè una
  persona no busqui una incoherència que és del maquinari de mentida (docs/85). Amb les tres, una persona tria el número:
  un torn de quaranta mil·lisegons és un dit que espera quaranta, i el que hi guanya és el que la fila de Lua diu que s'hi
  estarà més estona (docs/30, docs/75, docs/95; `apps/luaperf` és la recepta sencera de la mesura).
- **El paginador d'un desplegable es gira amb el dit**: la targeta d'una tria és una llista de valors per pàgines i el gest
  de costat és el mateix —el sistema el gasta i l'aixecar el dit no tria res—, o sigui que una app no ha de fer res perquè
  funcioni (docs/30).
- **Les tres apps que el sistema obre per una persona són a la pestanya System** (0.189.109, la tercera a 0.189.150, l'amo:
  *«Afegeix un selector d'aplicació per gestionar els settings … a la pestanya "System" on posa "App at start" doncs igual»*):
  la de l'inici —la fila **Home**, que és `cfg.home` i la que surt amb la placa—, **la del botó dels ajustos del panell** —la
  fila **Settings**, `cfg.settings`— i **la que tapa el vidre** —la fila **Lock**, `cfg.lock`, que és `device_lock` de sèrie—,
  totes tres amb la llista d'apps del disc i `none` al davant. **L'app dels ajustos no s'ofereix com a app d'inici** —una
  pàgina que obre els ajustos no és la pàgina amb què arrenca una placa— i sí que és a la llista del botó i a la del pany, que
  és el seu lloc (docs/30, docs/40). **Els noms són els que va demanar l'amo** (`Home`, `Settings` i `Lock`): una fila que diu
  `Home` diu què és l'ajust i no com es diu una variable.
- **I la pestanya Battery té el son de la placa** (0.190.5, l'amo: *«On es l'opció a Settings per desactivar-ho?»*): el
  commutador **Sleep when idle** és `cfg.sleep` i el que fa és la política de la llei 9 —la placa s'apaga fins a la propera alarma
  quan ningú no l'ha tocada deu segons—, que és un ajust i no una llei perquè hi ha tardes en què una persona vol la placa
  desperta. El que la fila ensenya és el que la política té, i la resposta es desa perquè el pròxim boot no la desfaci (docs/30,
  docs/60). **I el temps de quietud és un desplegable** (0.190.9, l'amo: *«un selector desplegable … per seleccionar quan es posa en
  DeepSleep»*): la fila **Sleep after** obre la targeta de tria del sistema amb tres temps —10 seconds, 1 minute i 5 minutes—, que és
  `cfg.idle`, el mateix camí que la fila *Lock after* de Privacy (el desplegable i no una casella, perquè són tres valors i no dos).
- **La pestanya Privacy és el bloqueig** (0.189.150): el commutador **Lock the screen** escriu `cfg.lockon` —la barra no
  desapareix, perquè el que tapa el vidre és una app i no una barra que s'amaga— i la fila **Lock after** escull **5, 10 o 15
  minutes** amb deu de sèrie, que és `cfg.lockafter`. Amb el commutador cert i cap pàgina triada, la línia de sota les files ho
  diu («No lock app: choose one in System») i el registre hi posa la comanda, en comptes d'engegar una pàgina que no existeix
  (docs/30, docs/60).
- **La pestanya Disk diu on viuen les dades de les apps** (0.190.8, l'amo: *«perqué es perden les dades d'historic de Battery
  i TimeClock al pujar un firmware nou? … i tinc SD»*): `/data/<id>/` era el flaix sempre i `-t uploadfs` reescriu la partició
  sencera, i ara **segueix el disc preferit** —la fila **Preferred disk** de dalt mateix, que és `fs.cfg` des de sempre—, o sigui
  que no hi ha cap ajust nou ni cap porta nova: la fila **Apps data** només es llegeix i treu la resposta de `fs.mounts()` (el
  `type` de la fila `/data`), perquè el que una persona vol saber abans de flashejar és **on ha quedat de debò**. Una segona
  preferència per a la mateixa pregunta serien dues files que poden dir coses diferents i vint bytes de DRAM que la Paper no té
  (18/11: `dram0_0_seg` overflowed by 24 bytes) (docs/70, docs/80).
- **El que la pàgina escriu és de `cfg` i el que la persona va deixar a mitges és de `state`**: les hores i la data es
  desen amb `time.set`, la zona amb `time.tz`, el calendari amb `time.leap`/`time.week`/`time.dst`, el disc amb `fs.cfg`,
  el processador amb `power.cpu`, i el teclat, l'arrencada, l'inici, l'ordre, el torn, el cursor i el terra de les dades amb
  `cfg.*` (docs/30 § «`cfg`»). **La pestanya i el cercle són de la pàgina.**

**L'hora local d'una persona la converteix el sistema** (0.189.130): `time.set` compta en UTC i **`time.epoch(y, m, d, h, min)`**
és la seva inversa —porta l'offset efectiu, el del mapa més l'hora d'estiu—, o sigui que la pàgina no té cap aritmètica pròpia.
La que hi havia aquí —`days_from_civil` i `time.tz()`, la de Hinnant— va marxar amb la crida: era la tercera manera d'arribar a
l'hora de la persona i **ignorava `time.dst()`**, i per això un rellotge posat des d'aquest panell quedava una hora desviat de la
que deia la pàgina de la zona horària (docs/30, docs/60).

```lua
local function epoch_of(day, month, hour, minute)
  local today = time.date()
  if today == nil then return nil end
  return time.epoch(today.year, month, day, hour, minute)
end
```

**I el botó de l'hora espera i diu la veritat** (0.189.143): `net.ntp` és una promesa del nucli com qualsevol altra petició de la
ràdio i el botó la cridava **sense `await`** —`pcall` responia `true` amb la promesa a la mà, o sigui que la pàgina deia «The hour
comes from the network» en una placa sense cap xarxa desada—; ara la crida va dins d'un `pcall` amb un termini de vuit segons i
la negativa arriba amb la frase d'una persona (la mateixa que fa servir la pàgina de la zona horària). **I la línia de sota les
files es pinta**: l'acció d'un botó corre al bucle de la pàgina i ningú no demanava la pàgina després, o sigui que el que deia el
botó s'escrivia i no es veia fins que una altra cosa repintava la pàgina (docs/30, docs/60).

**Les zones del desplegable són hores senceres** (0.189.142, l'amo: *«Treu la selecció de +30 a les zones horàries, no he vist mai
que fos +X.5, sempre hores +1, -3, +3…»*): de -12:00 a +14:00, una hora per fila. El sistema accepta qualsevol minut (`cfg tz 90`
és una hora i mitja) i una placa posada així ensenya la fila buida, que és el que la pàgina pot dir de veritat.

**I una fila que ensenya l'hora del vidre i en desa una altra ho ha de dir** (0.189.141): la fila **Time zone** ensenya
`time.offset()` —el número que la franja de la barra i la pàgina de la zona horària ensenyen— i desa `time.tz()`, que és el
número del mapa. Amb la casella de l'estiu marcada els dos números són a una hora de distància, i la primera versió d'aquesta
fila ensenyava +04:00 i desava +04:00: **obrir el desplegable i acceptar el valor que ja hi era movia el rellotge de la placa
una hora més**. El que es desa és el que s'ensenya menys l'hora de la casella, i la pàgina ho diu en una línia del registre
(`settings: zone +03:00, and the board shows +04:00`) perquè el cas pugui veure els dos números alhora (docs/30, docs/60).

**El que encara no hi és, i per què** (i és la resposta honesta a mitja llista de l'amo): el panell té set pestanyes i
només les que tenen porta darrere. **Ja hi són** les tres velocitats del processador amb la seva llista blanca i la marxa del
torn amb nom (0.189.107 i 0.189.108, amb les xifres mesurades a la placa) i **l'app del botó dels ajustos** (0.189.109, que és
a la pestanya System amb el mateix selector que l'app de l'inici) i **el bloqueig del vidre** (0.189.150: la fila `Lock`, el
commutador i els minuts de la pestanya Privacy). **Falten** la política de miniatures, les sessions al disc, el NTP a
l'arrencada, el paper pintat de fons, el splash propi amb el navegador de fitxers i el PIN: **cap d'aquests no és un ajust del
nucli encara**, i una fila que no escriu res és pitjor que una fila que no hi és. Els temps de son lleuger i profund tampoc no
són una fila: els decideix la política d'energia (docs/60), i el que s'hi pot triar és el bloqueig que ve després. Cadascun és una feina de subsistema amb la seva porta, la seva prova i el seu document.

**I el que es prova, es prova amb el dit i amb el dial** (`test_apps`,
`test_the_panel_of_settings_writes_through_the_doors_of_the_system`): una pestanya de la segona fila es tria amb un dit,
la fletxa d'un `ui.selector` obre el `ui.picker` i **el dial tria el valor** (les tres tecles de la Paper són centre, amunt i
avall: clau 0, 1 i 2), un dit tria l'hora al quadrant de la targeta, el botó escriu l'hora a la placa, el commutador de la
privacitat encén i apaga `cfg.lockon` —la barra es queda on és, que és el canvi de la 0.189.150—, i una esborrada pregunta abans.

## 6 · Les proves, i la versió de l'app

- **Cada port porta una prova que arrenca l'app de debò** (`test_apps`, docs/80): es copia l'app del
  repositori al disc de la prova, es corre amb `run <id>`, es premen les tecles o es toca el vidre
  de l'emulador, i es llegeix el que l'app diu per consola. Una app que no s'ha arrencat mai no està
  portada.
- **L'app puja la seva pròpia versió** (`version` del manifest) cada cop que canvia, com el sistema
  puja la seva: la calculadora va néixer en 0.1.0 i és a 0.2.0, i `about` va d'0.1.0 a 0.3.0. Una app
  que canvia i es queda amb el mateix número està mentint sobre el que porta.
- **I cada llibreria la seva** (0.186.0, literal: *«les apps i llibreries també pugen de versió amb els
  canvis… tot ha de pujar de versió si és modificat»*): el commit que toca
  `lib/<nom>/` puja el `version` del `library.json`, amb la mateixa paraula X/Y/Z. **No és decoratiu**:
  `apps/about/res/libs.txt` es genera d'aquests números (`tools/gen_about.py`, i `--check` diu si el
  fitxer s'ha quedat enrere), o sigui que una llibreria que s'ha mogut sense dir-ho és una pàgina del
  About que menteix. `tools/check_version.py` refusa el commit que ho fa malament, i la tasca
  `lunokOS: check versions` ho pregunta abans de fer el commit (docs/40, docs/80).
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
- **Una icona de la zona privada d'Unicode es pot portar, i el camí és el de l'original** (9/11, 0.173.0). Les tres
  tecles de l'original eren glifs U+F04B, U+F04C i U+F04D d'un `keys.pof` que **no era cap dibuix**: era
  `tools/fontgen.py` passat per **`FontAwesome.otf`**, i el codi de la zona privada només deia on vivia la figura
  (README de l'original, 27/10). Aquí el text és **d'un byte per caràcter**, i la cambra equivalent és la dels codis
  per sota de l'espai (docs/30): les cinc figures —play, pausa, stop, l'ull que obre la setmana i la fletxa enrere— són
  els codis `1` a `5` de `apps/timeclock/res/keys.ttf`, que fa `tools/make_keys.py` **amb els glifs del mateix
  `FontAwesome.otf`**, i les pinta el `title` de cada botó amb `botó:font(cara)` —el que l'original feia amb
  `set_text()` en una tecla que ja era un botó del sistema—. I si la cara no hi és, les tecles
  diuen `Start`, `Pause`/`Resume`, `End` i `Week`/`Day`, que és el que l'original mateix feia quan li faltava la cara.
- **L'hora d'una persona no és l'hora del xip**, i això va ser una crida nova del sistema: `time.utc()`, `time.date()`
  i `time.tz()` amb `cfg tz +01:00`, i el calendari té els seus dos ajustos a `cfg leap` i `cfg week` (docs/30, docs/60). Una app que diu «de 7 a 19» ha de dir-ho en l'hora de qui la
  llegeix, i el desfasament és una cosa que el sistema ha de saber, no cada app.

**I el que sí que es va poder copiar sencer**: el format del fitxer d'apunts (`2026-11-01|09:00:00|S`, només creix,
el dia es llegeix com el cul del fitxer), el de les notes (un fitxer a part perquè una nota s'edita i una edició és
una reescriptura), la finestra de feina de 07:00 a 19:00 que retalla cada interval abans de sumar-lo, el dia que es
pinta (l'últim amb apunts i no avui) i la mitjana dels dies que tenen apunts. Les mides tornen a ser les de l'original
en píxels de línia: `small` 33, `body` 43 i `title` 57, i la fila d'una tecla fa 84 px —i la figura d'una tecla fa 39
px, que és el 41 de l'original, mesurat sobre els contorns del fitxer (0.173.0)—.

**I dos retocs de l'amo sobre el full de la setmana** (0.189.51). El primer, que **una setmana que comença de nou no
diu res**: la lletra deia *«al passar la setmana comença de nou, jo vull que surti una línia de la mitjana de les altres
mostres (mateix dia de la setmana)»*, i cada columna del gràfic porta ara una **ratlla de dos píxels** a l'alçada del que
aquell dia de la setmana ha valgut **les quatre setmanes de darrere** (`kMeanDays = 28`, o sigui el cul del fitxer que ja
es llegeix). És la **feina** i no el total —el número que la línia del full ja diu «avg»— i només compta els dies amb
marques: un dilluns que ningú va treballar no és un zero. **L'escala inclou les mitjanes**, perquè una ratlla més alta
que el dia més alt d'aquesta setmana es dibuixaria fora del full, i el log diu el número de cada dia
(`timeclock: mean Mon 4h 30m in 2, …`), amb `--` i no un zero quan no hi ha cap altra mostra. El segon, que **la llegenda
es trepitjava**: *«la llegenda de [ ] Work i [ ] Rest estàn sobreposades»*, i era certa perquè les posicions eren a ull
(`rect(92)` i `text(114)`) i valien amb la taula de cinc per set del flaix i no amb els onze píxels de línia de la cara
del disc. Ara es mesura amb `ui.text_width` i cada cosa comença on acaba l'anterior més divuit píxels, i hi ha **tres**
coses: `Work`, `Rest` i la ratlla de la mitjana, que és la que explica el gràfic.

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

Portades i viatjant avui, **dotze** (`home_eink` va a part, a sota):

| Original | Id aquí | Versió |
|---|---|---|
| `About` | `about` | 0.5.151 |
| `Calculator` | `calculator` | 0.2.2 |
| `Clock` | `clock` | 0.1.5 |
| `Device Lock` (de `src/apps/lock_app.cpp`) | `device_lock` | 0.1.3 |
| `Editor` | `editor` | 0.1.1 |
| `EPUB` | `reader` | 0.1.1 |
| `Timeclock` | `timeclock` | 0.2.2 |
| `2048` | `2048` | 0.2.2 |
| `Life` | `life` | 0.1.5 |
| `Battery` | `battery` | 0.1.3 |
| `Meteo` | `meteo` | 0.1.1 |
| `Agenda` | `agenda` | 0.1.2 |

**La columna «Versió» és la del `manifest.json` d'avui i no la del dia que es va portar** (0.190.7): un commit que toca una app
en puja el número i **toca aquesta fila al mateix commit**, o la taula es queda dient el que ja no és (docs/40, docs/80).

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

Les **tretze** que falten, i que són la cua d'aquesta feina: `Banner`,
`FRAM`, `Files`, `ImageViewer`, `Jellyfin`, `News`, `Pacman`, `Paint`, `Password`, `SerialFUSE`,
`Snake`, `Tamagotchi` i `Tetris`.

**El que va ensenyar la `Battery`** (0.189.144, i el control que en va sortir): el gràfic del projecte del paper era **un
dibuix de l'app** (`graph.lua`, amb `paperos.draw`) i aquí és **un control del sistema** (`ui.chart`), perquè el que demana
—el marc, tres guies amb el seu número, la línia, el quadrat de l'última mostra i les dues hores de l'eix— és el mateix dibuix
que demanarà la `Meteo`, que al projecte del paper era **el mateix fitxer copiat dues vegades** perquè una app només podia
carregar els mòduls de la seva carpeta. Les altres tres coses que aquest port va ensenyar:

- **Una finestra nova no es dibuixa a sobre de la vella**: el canvas del control es rentja amb el color del paper, però la
  passada barata del panell (`du`) deixa el fantasma de la línia d'abans i el vidre ensenya dues corbes. La pàgina que canvia
  de finestra demana el rentat de setze nivells (`gc16`) —una vegada, quan la persona ho tria— i la que només pren una mostra
  es queda amb el barat (docs/50, docs/90).
- **Una preferència no és una forma**: la finestra triada (una hora, sis, un dia) va al fitxer de l'app (`fs.data`); el cercle
  del dial va a la sessió `state`. És la mateixa línia que la 0.189.12 va escriure per a totes les apps (docs/30, §5 bis).
- **`fs.write` torna els bytes i no un booleà**: un `pcall(fs.write, ...)` que només mira si ha petat diu que sí quan la porta
  ha respost `nil, error` i la pàgina ensenya un registre que no existeix. La resposta s'ha de mirar (docs/30).
- **La mostra es pren dins del ganxo de l'alarma i no al bucle de la pàgina** (0.189.145, l'amo: *«però els avisos arriben a
  l'app per fer la recolecció d'informació?»*): una alarma que venç amb l'app tancada obre una **finestra**, i una finestra
  crida `on_alarm` i tot seguit tanca l'app — o sigui que un ganxo que només deixa una bandera perquè el bucle faci la feina
  deixa la mostra sense prendre, que és exactament el que l'alarma havia de fer. El ganxo no pot esperar, i apuntar una mostra
  no espera res. La segona meitat: **la finestra no ha llegit res**, i per tant llegeix el registre del dia abans d'afegir-hi
  la línia, o el desaria a sobre amb una de sola. L'`on_alarm` del `Clock` ja ho feia així (desa el que ha sonat i torna), i
  aquest port ho havia de fer igual (docs/30, docs/60).

**El que va ensenyar la `Meteo`** (0.189.169, la portada que havia de ser **idèntica** a l'original):

- **L'ordre de l'amo va ser literal** —*«L'interficie ha de ser IGUAL, EXACTAMENT IGUAL, l'unica diferencia han de ser els
  controls UI, que has d'usar els del sistema»*— i el que va canviar és **qui dibuixa cada cosa**: el termòmetre i les barres
  de la humitat es pinten a mà (aquest sistema no té instrument ni control de barres, i un instrument es mira i no es toca),
  el gràfic és `ui.chart` i les etiquetes i els tres botons són del sistema. Les mides de l'original (33, 43, 57 i 82 píxels
  de línia) es demanen amb `ui.line_height(1)`, perquè les dues cares del sistema no fan la mateixa línia.
- **El gràfic va créixer perquè la pàgina li demanava coses que no sabia dir** (0.189.169): `decimals` (un termòmetre dona
  centèsims i la banda ha d'escriure `22.5`), `left`/`right` (una finestra d'un dia diu una hora i la d'un trimestre una
  data, i això no ho pot saber un control que només rep minuts) i `gap = 0` (una sèrie que l'app ja ha aprimat no té forats
  que tallar). Cap d'elles no és una preferència d'una app: són les tres coses que qualsevol pàgina de mesures demana.
- **Dues gràfiques en una pàgina comparteixen el marge esquerre i l'eix del temps, i això es compta i no s'endevina**: la
  banda és el número més ample més el tros de guia que surt del marc, o sigui que la pàgina mesura la seva amb
  `ui.text_width` i el **mateix número de dalt** que el control (`125.0`, el sostre físic del SHT30) i la mateixa cara; una
  banda posada a ull fa que les barres i la corba comencin en dues columnes diferents.
- **El text del projecte del paper cap a les seves cares i no a les d'aquest sistema** (0.189.170, i va ser el grau qui ho
  va ensenyar): els mapes de bits de PaperOS portaven ASCII + Latin-1 + accents, o sigui que `%.1f °C` sortia sencer; la cara
  del disc d'aquí és l'ASCII imprimible més els signes d'una tecla, i el grau quedava en un forat. La cara que hi posa remei
  és **la mateixa família amb Latin-1** (`dejavusans-bold-latin1.ttf`, `ui.load_font`) i la demana la pàgina, no el sistema.
  **I una llegenda que no hi cap baixa de cara abans que perdre cap paraula**: «40 min: 2 samples» no entra a la columna amb
  la cara de 33 px, i el que diu la finestra és el que es llegeix primer (docs/30).
- **El repartiment vertical de la columna de la dreta no es copia, es recalcula** (i això és el que la 0.189.169 va haver
  d'aprendre mirant els números): el projecte del paper gasta 82 + 33 + 140 + 33 + 20 = 308 píxels de la fila i en deixa
  vint-i-set per a la corba, perquè **la seva Meteo és de 540 × 960** (el retall 9:10 de la columna dona dotze píxels en una
  amplada de cinc-cents, que és com se sap). En aquest sistema el vidre és **960 × 540** i la mateixa pila de coses no hi cap:
  doncs les barres es queden el **27 %** del que sobra —com a l'original, 140 de 519— amb el mínim que necessiten per dibuixar-se
  (mig full de lletra per a la guia de dalt, trenta píxels de paper i l'aire del zero) i la corba la resta, i si tot i així no hi
  arriba **el número de la humitat baixa a la cara del títol**, que és la llei que el `gauges.lua` de l'original ja va escriure.
  El que no es toca mai és l'ordre ni el que hi ha a cada lloc (docs/30, docs/45).
- **L'escala del gràfic és la de la finestra i la del termòmetre és fixa**: `chart_draw` pren les dues puntes del paper de
  les mostres que li donen i només les acota al `min`/`max`, o sigui que dos graus d'una setmana tranquil·la són una corba, i
  l'instrument es queda a −10…50 perquè un instrument no canvia d'escala perquè la setmana sigui càlida.
- **El registre són dues cadenes i no una taula** (docs/75): quatre bytes per mostra (dinou de minut i dotze de temperatura en
  passos de 0,05 °C) i un byte de humitat, amb el **255** per a «aquesta línia no en tenia», perquè quatre mil quatre-cents
  números en una taula de Lua són una rehash de seixanta-quatre kilobytes. Les cadenes es fan de trossos de dos-cents
  cinquanta-sis bytes en llegir, el desat normal és **una línia afegida**, i l'escombratge és **una sola reescriptura**
  (`fs.write`, que és atòmica) cada dos dies i no cada mitja hora.
- **I el desat del registre no és un `pcall` d'una escriptura**: `fs.write` i `f:write` tornen els bytes, i una escriptura que
  ha respost `nil, error` amb un `pcall` que només mira si ha petat és un registre que la pàgina diu que hi és i no hi és (la
  mateixa lliçó de la `Battery`).
- **El vidre es repinta només si alguna dada ha canviat, i la passada és la de setze nivells**: l'alarma llegeix, el ganxo
  apunta la mostra, i el bucle compara el que el vidre ensenyaria —la temperatura, la humitat, les línies d'error, el número
  de mostres i la finestra— amb el que hi ha pintat. L'hora i el nombre de lectures no hi entren, perquè canvien a cada
  lectura. La passada és `gld16` i no `du`: el mercuri es mou i la corba es fa sencera, i una passada barata deixa el
  fantasma del valor d'abans (docs/50).
- **Una pàgina de controls que a més pinta a mà es torna a construir quan el vidre canvia de mida**: el sistema fa una vista
  nova i **tots els llenços de la pàgina moren**, o sigui que un `resize` no és prou —el camí és `build()` i tornar a llegir
  el `state` que la mateixa app acaba de desar, com fa la `Battery`— (docs/30).

**El que va ensenyar l'`Agenda`** (0.189.171, la portada que havia de ser *«tot EXACTALENT IGUAL, però amb els nostres
controls UI i es nostres crides a sistema»*):

- **El calendari del paper marcava els dies que tenen alguna cosa, i el calendari d'aquí no en sabia res**: `ui.month`
  guanya **`marks`**, la llista dels dies que porten un punt, i el dibuix la converteix en un punt de tres píxels sota el
  número (en el to del paper quan la cel·la és la triada, que si no desapareixeria sota la tinta). El control **no en sap
  res, de què és «alguna cosa»**: això és de l'app. És la mateixa llei de la `ui.chart` de la `Meteo` —una cosa que demanaran
  el calendari d'una app i el d'una altra no s'escriu dins de l'app.
- **Un mes no és una pàgina: es gira**. El control no té cap `on_month` ni botons de costat, i l'original tampoc no en tenia:
  el mes i l'any es giren amb el mateix gest del paper —un `drag` del dit, amb quaranta píxels de llindar— que la pàgina llegeix
  de `touch.gesture()` després que els controls s'hagin quedat els seus (docs/30).
- **La banda del teclat no és al vidre fins que hi és, i `kb:height()` torna zero mentre no hi és**: el formulari de
  l'Agenda té nou files i dues línies de peu, i si es maqueta amb els nou-cents dotze píxels de sempre la banda en menja
  dos-cents cinquanta-sis i l'última fila desapareix. La pàgina **es maqueta per a la vista que tindrà amb la banda**
  (`kBandHeight`, mesurada quan la vista s'encongeix) i les files es reparteixen el que queda: `(hei − band − peu) // 9`.
  **I la vista no es torna a fer**: un teclat obert escriu a la caixa que ja té, i refer la pàgina deixa el teclat escrivint
  en una caixa que ja no és al vidre. El canvi de mida es passa a la pàgina amb **`screen:resize(w, h)`**, que és el mateix
  camí que fa servir un panel sense píxels propis.
- **La fila on és el cercle no la pot moure el nucli** (la lliçó de la `ui.list_entry`): una llista del sistema no té
  `children` ni `remove` a Lua, o sigui que l'app que ha de moure la marca es queda **una taula de les seves files** i
  apaga i encén el `marked` de cadascuna. És el que fa l'Agenda amb els esdeveniments del dia.
- **El proper dia d'una repetició es compta en mesos i no caminant dies**: el mòdul `events.lua` del paper ja ho havia après
  i el port ho conserva —caminar dia a dia seixanta-dos dies buscava una repetició en milers de crides i l'escriptura es va
  quedar sense pressupost amb mitja pantalla pintada—. Els mesos candidats surten del mes de l'event saltant pel pas de la
  repetició, i els mesos que no tenen aquell dia es salten sense preguntar cap data.
- **`require('events')` llegeix el fitxer de la carpeta de l'app** (docs/30): el mòdul de dades viatja amb l'app com un
  fitxer més i el manifest no l'ha de nomenar enlloc.
- **L'avís és una alarma del sistema i només se n'anul·len les seves**: `alarm.list()` és la cua sencera —la `Meteo` i la
  `battery` també hi són— i el camp `app` de cada entrada és qui la va demanar. Una app anul·la les seves i en posa una de
  nova, i `alarm.caps().max` és **el nombre d'alarmes que hi caben**, no una distància de temps (docs/30).
- **Una targeta no s'obre dins del ganxo que la demana, i això es veu com una fila del formulari estampada a sobre de la
  targeta**: el sistema posa `choosing` al camp i l'escriu **després** de cridar `on_open`, o sigui que la targeta creada allà
  queda amb la fila del camp pintada al mig. El camp deixa el desig (`wanted_ = function() pick_from(...) end`) i el bucle
  l'obre un torn més tard amb `sys.delay(300)`, que és la mateixa recepta del `Clock` (docs/30, docs/45).
- **El que canvia de dalt a baix demana el rentat de setze nivells**: girar el mes és tinta nova sobre la de trenta-un números
  vells, i amb la passada barata el desembre es llegeix amb el novembre a sota. `ui.present('gl16')` al gir i a l'obrir i
  tancar la targeta; la resta de la pàgina es queda amb el que el sistema triï (docs/50, docs/90).
- **Un avís amb `raise` obre la pàgina de zero, i el que ha sonat no és cap variable de l'app** (0.190.7): el ganxo d'una
  alarma amb `raise` arriba amb la pàgina acabada de construir, o sigui que `armed_` és *el pròxim* event i no el que ha sonat.
  El que sobreviu és **el payload**, i per això l'`agenda` hi posa el dia de l'ocurrència (`2026-11-20`) i no el mot `remind`:
  el ganxo el llegeix, i el dia que s'obre és el de l'ocurrència i no el de la primera vegada que l'event passa. El `Clock` fa
  el mateix amb el text de la fila, i per això un despertador que sona amb el rellotge tancat ensenya «It's time» i no
  l'esfera (docs/30, docs/60).

**El que va ensenyar el `Device Lock`** (0.189.97, i el permís que en va sortir a la 0.189.99):

- **Una pantalla de bloqueig no ho és si el sistema la deixa obrir**: `ui.full_screen(true)` treu la barra, però el sistema la
  torna amb **tres tocs a qualsevol lloc** —la sortida d'una pàgina que ho cobreix tot— i la barra porta el gest que obre la
  targeta amb la creu que tanca l'app. La resposta és **una app d'administrador** (`"admin": true` al manifest) i
  `admin.LockScreen(true)`: el mateix vidre sencer amb el pany posat, on els tres tocs no hi són (i no es prenen de la cua, o
  sigui que la pàgina els compta) i `ui.full_screen(false)` és refusat amb `E_PERM` (docs/30, docs/40).
- **I el pany no pot sobreviure a l'app**: el final d'una app —el seu `app.exit`, tres premudes de la tecla, un error— torna
  el vidre i la barra, perquè una placa que es quedés sense franja i sense pàgina no la pot fer servir ningú (docs/50).
- **Un gest del tàctil pot ser més d'un toc**: tres tocs seguits dins la finestra del tàctil surten com **un sol** gest
  `triple`, i dos com a `double`. La pàgina que comptava només `tap` comptava dos tocs d'una persona que n'havia fet tres i no
  s'obria mai —només amb els tocs separats més de la finestra—, i ara compta els tres noms (0.189.99, docs/30).
- **I els tres tocs que tanquen una app són una porta que s'ha de poder tancar** (0.189.101): el sistema pren el gest de la
  cua abans que la pàgina el llegeixi —és la sortida de tota app—, i una pantalla de bloqueig que els compta per obrir-se es
  trobava que **el tercer toc tancava l'app**. `admin.triple_tap_close(false)` deixa el gest a la cua i la pàgina el llegeix
  (docs/30).
- **El compte el porta el candau, i el candau és un control** (0.189.101, l'amo: *«L'app ha de registrarse el triple tap al
  botó sense vores amb el dibux del candau»*): un canvas amb `focusable = true` i `on_click` és el botó sense vores que
  aquesta pàgina vol —el dit hi cau, el dial el pot prendre i **tres tocs són tres clics**—, i el dibuix va en un canvas seu
  a dins perquè un `ui.button` amb `framed = false` pinta la seva cara a cada premuda i un requadre de 184 × 255 píxels de
  pintada deixa **el fantasma gris de la caixa** sobre el candau (mesurat a l'emulador). **I el dibuix només es fa quan ha
  canviat**: el sistema demana el dibuix d'un control en tocar-lo, i tornar a pintar el candau sencer amb tinta a cada toc
  és el mateix fantasma per un altre camí —el que un toc ha de moure són els tres cercles, i prou— (docs/30, docs/50).
- **I `app.exit()` no es pot cridar des d'un `on_click`**: el ganxo del control corre dins d'una crida protegida del sistema,
  i el que `app.exit()` fa és **llençar l'error que acaba l'app**: el ganxo se'l menja, la consola diu `on_click: app.exit` i
  la pàgina es queda amb el pany obert i l'app viva. La porta es demana amb una bandera i es creua al torn de la pàgina, que
  és el camí que fan els controls de totes les apps (mesurat a l'emulador, 15/11; docs/30, docs/90).
- **I un botó sense vores d'una pàgina no té buffer** (0.189.101): `ui.button` es fa sense canvas propi i pinta la cara al
  canvas del pare, i el `draw_clear` que el seu dibuix feia servir per als cantons **esborrava tota la pàgina** —les dues
  frases i els cercles del `Device Lock` van quedar en blanc al primer toc del candau—; ara la cara és la caixa del botó i
  no el canvas (mesurat a l'emulador, docs/30, docs/50).

**El que va ensenyar el `Clock`** (0.189.98, la primera app portada que **dibuixa a mà** i que **porta alarmes**):

- **L'esfera és el mateix dibuix i els mateixos números** que `dial.lua`: l'anell és una **anella** i no una banda
  —cada fila s'omple de la vora de fora cap a dins, o sigui que el gruix és una distància al cercle i surt igual a les
  dotze que a les tres—, els seixanta tics pengen de la vora de **dins** de l'anell, les dotze marques són més gruixudes
  que els tics i la de l'hora que assenyala la busca encara més, i les busques porten cua i un tap al mig. El to de la
  cara és **el 10** i no un altre: és un dels quatre nivells que la passada de quatre nivells pot dibuixar.
- **`ui.line` no té gruix** en aquest canvas, i el que fa un llapis és passar-hi un disc de la mida del gruix al llarg
  del camí: això és `brush_line`, tres línies.
- **La lletra de la nota i de les files és la petita** (33 píxels de línia, el pas 3 d'aquesta cara) i no la del cos: ho
  diu la captura de l'original, i amb el cos la frase del cronòmetre es parteix en dues ratlles que a PaperOS no hi són.
- **La qualitat de la passada és una decisió de cada moment**: en **canviar de pestanya** es paga un `gc16` —el gris de
  l'esfera no marxa amb una passada de dos nivells i queda una taca a la pestanya següent—, i el número que corre va amb
  `gld16`, que és net i no fa el flaix que un rellotge no es pot permetre cada segon. El `du` és el més barat i deixa cinc
  setzens de la xifra d'abans: el número s'acaba llegint doble.
- **El compte enrere és una promesa com l'alarma** (`alarm.at` amb `raise`), i quan venç el sistema obre l'app i crida
  `on_alarm` igual que per a una hora: la pantalla gran és la mateixa i és la de PaperOS («Time is up», «The countdown has
  finished.»). Una app que armés alarmes des del chunk principal en una finestra n'armaria una a cada crida, i per això hi
  ha la porta `console.is_foreground()` abans de fer res.
- **Una alarma que la cua no pot armar no mata l'app**: `alarm.at` pot dir que no (`E_NOMEM`) quan la cua és plena, i el
  que ha de passar és una línia a la consola (mesurat a l'emulador, amb sis promeses velles de la mateixa app a la cua).
  **I les promeses velles es donen per acabades a l'arrencada** (`alarm.list` i `alarm.cancel`): la cua sobreviu als
  reinicis i una app que n'arriba amb sis de seves no en pot armar cap més.
- **El desig d'un control es fa al torn següent** (`wanted`): un botó que s'ha deixat anar es torna a pintar ell mateix
  **després** del ganxo, i una pàgina repintada dins del mateix gest es queda amb la paraula del botó vell estampada on ja
  no és (el «Delete» d'una pàgina que ja no el té). És la mateixa regla que el `tz` ja seguia amb el botó del rellotge.
- **La targeta d'una llista de seixanta no cap en un `ui.menu`** —el menú dibuixa les opcions que hi tenen caixa i prou—,
  o sigui que el camp d'un dia, d'una hora o d'un minut obre una **llista paginada** amb una fila per valor, i el que hi
  havia a sota es treu de la pàgina mentre la targeta és a sobre (un control que rep el seu toc després pinta a sobre).

**El que va ensenyar el `2048`** (la primera app portada que és un joc i no una pàgina de dades):

- **El gest de fregar no existeix aquí, i el joc no en té la culpa.** El paper donava `swipe_left`,
  `swipe_right`, `swipe_up` i `swipe_down`; aquest sistema dóna **un `drag` amb `dx` i `dy`** i la
  direcció és de l'app. El joc la llegeix amb un llindar de **30 px** —un toc en mou com a molt vint— i
  l'eix que ha caminat més mana, que és la mateixa regla que fa que un dit tort no mogui res (docs/30).
- **La qualitat no es demana, es pinta.** `paperos.damage(x, y, w, h, "gld16")` és aquí un argument del
  dibuix (`ui.rect(..., "gld16")`), o sigui que el disseny del refresc de l'amo —una rajola que canvia
  viatja sola i a setze grisos, els dígits en dues tintes, el peu només quan el seu missatge és un
  altre— s'escriu igual i amb una crida menys. **I la qualitat es NOMENA** (0.186.0), que és el que fa
  que cada caixa sigui el seu pintat: un `ui.present()` sense qualitat va pel camí de la pàgina, on quatre
  caixes són una pàgina i el vidre sencer parpelleja; un present que la nomena pinta caixa per caixa.
  **Des de la 0.189.5 un moviment refresca tres coses i cada una amb la passada que la seva tinta demana**
  (docs/30, docs/50):
  - **la pàgina d'arrencada és `gl16` i UNA passada**: `ui.clear` d'una pàgina que el sistema ja ha donat
    blanca no afegeix cap caixa (0.168.0), i les setze cel·les, les tres etiquetes i el botó eren **set
    caixes desunides i set passades** (mesurat: `12,197 516x385`, quatre de `124x123`, `12,8 516x73` i
    `12,727 504x30`). Ara la vista sencera es marca amb un `ui.rect` del mode de la pàgina i tot el que es
    dibuixa després hi cau a dins: **una passada de `0,0 540x912 gl16`**;
  - **la tira (puntuació, màxima i botó) és una caixa i una passada de `gld16`** (450 ms, setze nivells,
    **cap parpelleig**): el marc de la puntuació s'acaba on comença el botó, o sigui que les tres caixes
    s'ajunten soles, i el botó es marca a cada moviment perquè el marc d'una premuda no s'hi quedi. `du`
    —el que hi havia a la 0.189.4— **no serveix aquí**: deixa cinc setzens del que troba i els dígits d'abans
    es veuen;
  - **i el tauler és `gl16` en UNA caixa**: és la passada que **neteja**, i una cel·la que es buida ha de
    quedar paper i no l'ombra de la fitxa que hi havia. El paper del requadre es marca primer i les setze
    cel·les hi cauen a dins, o sigui que un moviment de dues fitxes paga una forma d'ona de 516 px i no
    quatre de 123. **I l'aire del mig no viatja**: no hi ha cap caixa que englobi la tira i el tauler —ho era
    a la 0.189.5 de pas, `540x716` per a dues fitxes i un número—. Mesurat: `paint 12,6 516x65 mode 5 gld16`
    i `paint 12,206 516x516 mode 3 gl16`.
- **La seva partida guardada va a `/data/2048/game.txt`** (`fs.data`) i té **quatre línies** des de la
  0.189.4: els setze exponents, el marcador, **la màxima i el dia que es va fer** (`AAAA-MM-DD` de
  `time.date`, que és el rellotge de la persona amb el seu fus). Un desat de dues línies —el d'abans—
  s'obre igual i només perd la màxima. **I el que fa que sigui un desat és que el trobi l'execució de
  després**: `test_a_save_is_there_for_the_run_after_it` arrenca una app dues vegades sobre el mateix disc
  —«no save E_NOENT» i després el que hi ha escrit— i comprova que el fitxer és a `data/<id>/` i a cap altre
  lloc (docs/30, docs/70).
- **I una pàgina que es fa de nou es torna a preguntar sencera** (0.186.0): `ui.clear` omple la vista de
  paper, i el sistema ara demana a cada control que hi havia que es dibuixi (és la regla d'un forat a la
  pàgina). Abans, la vista que canviava d'alçada deixava el botó New i el marcador fora del vidre per
  sempre, perquè no s'havien mogut: *«el botó new desapareix»* (docs/30, docs/90).
- **I el botó New no pregunta res** (0.189.4): un toc comença la partida. El «Sure?» demanava dues
  decisions per a un desig —i dues pintades de la paraula, que és el que el va fer desaparèixer— i el desat
  de la jugada anterior és el que torna si el toc va ser un error; la màxima, en canvi, no es toca.

**El que va ensenyar el `Life`** (el segon joc portat, i el primer que es mira com un rellotge):

- **La pàgina és una passada i la generació és la passada barata sobre les caixes que han canviat.** Mesurat a
  l'emulador: la primera pàgina és `paint 0,0 540x912 mode 5 gld16` —una passada de setze nivells i cap
  parpelleig— i una generació és `paint 8,80 524x824 mode 1 du` i `paint 0,0 448x40 mode 1 du`: el joc i la
  línia dels comptadors, en el mode més barat del panell (120 ms, dos nivells, cap flaix). **`ui.present(mode)`
  és el que dóna el control**: les caixes que es toquen s'ajunten i prou, cap pàgina sencera pel mig.
- **El tauler és més petit que la vista, i no és un gust**: amb la franja de paper a tot el volt, la caixa del
  joc i la dels comptadors no es toquen mai i cap de les dues és el vidre — i el nucli converteix en rentada
  (`gc16`, 450 ms i dos flaixos) qualsevol passada de dos nivells sobre set vuitens del vidre (docs/50). El
  paper feia el contrari (la graella fins a l'última fila) perquè allà el que decidia era *comptar files*, i
  aquí el que mana és la caixa.
- **`du` pinta i no neteja** (docs/95 E-7), o sigui que cada sis generacions la pàgina es renta amb `gld16`
  (450 ms, cap flaix): és la política del paper, que portava el deute de ghosting per fila i rentava quan tocava.
- **Els números**: un brou costa **1 415 ms per generació** —1 080 dels quals són les 824 files de la caixa— on
  abans en costava **1 770** amb un `gc16` de tot el vidre i dos flaixos; un món que es mou en un lloc (un
  planejador) costa **uns 500 ms**. **I el que queda no és de l'app**: `It8951::flush` fa una transacció d'SPI
  per fila —1 100 µs mesurades per fila— i el projecte del paper enviava la regió en esclats amb el CS aguantat
  (docs/50).
- **I dos noms de mode estan intercanviats** respecte del full d'E Ink: el que aquí es diu `du4` és l'`A2`
  autèntic (120 ms, sense flaix, el mode de l'animació de blanc i negre) i el que es diu `a2` és el `DU4`
  (290 ms). No s'ha reanomenat res (docs/50 ho deixa escrit).
- **El dit pinta una cel·la, i els voltants es pinten amb ella**: el tocadís es llegeix amb `touch.read()`
  —el primer marc que veu el dit, §5 trampa 4— i el `gesture` d'aquell mateix torn es buida i es llença. La
  línia de paper que separa dues cel·les vives es dibuixa DINS de la cel·la de dalt i de la de l'esquerra, o
  sigui que qui toca una cel·la ha de repintar les vuit del costat o deixa una línia que no toca.
- **Les tecles són de l'app i el centre segueix sent la sortida del sistema**: `on_button` llegeix el `spot` de
  `button.list()` (centre = pausa, costats = el temps de refresc), no espera res i no hi pinta —el bucle pinta
  les dues línies de la tira quan el seu text és un altre—; els tres tocs del centre en un segon i mig tanquen
  l'app, i això no és una cosa que un hook pugui prendre (docs/30).
- **I una pàgina d'ajustos, que és l'altra cosa que una pàgina d'aquesta app pot ser** (15/11, l'amo: *«fes una
  vista de settings i posa un checkbox amb aquesta opció … i un desplegable amb el temps de refresc»*): la roda
  dentada de la tira l'obre, la fletxa que en pren el lloc torna enrere, i el món queda **aturat a sota** mentre
  es tria. Ofereix les dues coses que són decisió i no partida: **si la pàgina es renta cada sis generacions** i
  **amb quin dels dos temps de refresc corre** (1200 o 700, i les tecles laterals només canvien entre aquests
  dos).
- **La pàgina d'ajustos és de controls i prou** (15/11, l'amo: *«Et recordo que has d'usar els controls del
  sistema»*, i després *«el botó no sembla connectat»*): `ui.checkbox` amb un `ui.label` a dins, un `ui.selector`
  que obre un `ui.menu` i un `ui.button` amb la marca `back` del nucli; el nucli els dibuixa, el cercle del focus
  hi arriba i el dit és seu, i l'app només hi pinta el paper, el títol, dues ratlles i quatre línies d'ajuda
  (docs/30, docs/92). **Els dos botons de la tira són `ui.button` també**: la roda dentada porta la marca
  `settings` del nucli i el món la cara de l'app, amb el nivell quatre, que és la tinta de les marques del
  sistema (docs/40).
- **I el canvi de pàgina és l'únic que parpelleja**: `gl16` —setze nivells i **un** flaix— i no `gld16`, perquè
  una pàgina de paper sobre un tauler de cel·les negres deixa dos setzens i el que se l'emporta és una forma
  d'ona que porta el vidre a negre i blanc i torna (l'amo: *«al canviar a settings hauràs d'usar alguna cosa que
  netegi molt bé, ara queda brut»*, docs/50). Els tocs de la pàgina són `gld16`: paper que no es mou i ningú no
  mira el seu rellotge.
- **I el que es recorda és una preferència, no una forma de vista** (§5 bis): el món i la pausa moren amb l'app,
  com al paper, i els dos ajustos viuen al fitxer de l'app (`/data/life/settings.txt`, `fs.data`) i tornen la
  propera arrencada — que és el que docs/30 diu de les preferències i per què **no** van a la sessió `state`.
- **`run life blank` arrenca el món buit I ATURAT**, perquè una generació d'un món buit és un mapa nou —la regla
  de l'avorriment de `step`— i la mà no arribaria a pintar la primera cel·la. És la porta que fa que la prova
  pugui comptar cel·les: paper, una cel·la girada pel dit, la del costat intacta, el botó de món nou que omple el
  món amb el vint-i-vuit per cent i el centre que para el rellotge —un joc parat no demana cap pintat—.
- **El botó és un botó amb una icona a dins, i no un mot** (15/11, l'amo: *«un botó amb una icona dins, i no és una
  partida nova, és generar un món nou»*): `res/marks.ttf` és la cara de l'app feta amb `tools/make_keys.py --marks`
  —el `random` del clipart, U+F074, les dues fletxes que es creuen— i el botó és un `ui.button` amb `title` = el codi
  de la marca, `font(marks)` i `scale`; quan la cara no hi és el botó diu `World` i, si no hi cap, el nucli el talla
  amb `...`, que és el que fa amb qualsevol títol. **I `ui.text_width` no mesura una icona pròpia** —respon amb la cara
  del sistema i dóna zero—: quan la marca es pintava a mà calia `marks:text_width`, i amb el botó del nucli el problema
  desapareix perquè el títol el col·loca ell (docs/30).
- **Un `local function` que es declara més avall del ganxo que el crida és un global buit** (trobat per l'amo, 15/11:
  *«el botó no sembla connectat»*): `gear.on_click = function() openSettings() end` amb `local function openSettings()`
  quaranta línies més avall no dóna cap error de càrrega ni cap avís —el ganxo es tanca sobre el **global** `openSettings`,
  que és `nil`— i prémer el botó és cridar `nil`, que el sistema diu i ningú no llegeix. Les funcions que un ganxo
  cridarà **s'anuncien amunt** (`local openSettings, closeSettings, …`) i es donen baix (`openSettings = function()`), i
  el mateix val per a `saveSettings`, que la casella cridava i deixava el fitxer sense escriure.
- **Una targeta no s'obre dins del toc que la demana**: el clic d'un control arriba amb el dit encara a sobre i la
  targeta que hi naix pren **el mateix clic** com una tria de la fila on era el dial —la llista es tancava sola i no
  desava res—. El camp deixa el desig i el bucle obre la targeta quan el vidre és buit (`wantedMenu` i `not held`), que
  és la regla que aquest projecte ja té per a un botó que demana una cosa lenta (docs/30, docs/90). **I la targeta ha de
  tenir lloc per a les files que duu**: un `ui.menu` de cent vint-i-vuit píxels són **una** fila —setze de paper a dalt
  i a baix i una fila mai sota cinquanta-dos— i les opcions van de costat, amb el dit caient a la ranura entre les dues
  columnes; dues files de la cara petita demanen cent seixanta-vuit (docs/90).

La prova `test_every_app_of_the_repository_declares_its_boards` porta **la llista dels ids que hi ha d'haver a
`apps/`** i comprova que tots s'han auditat: quan en portis una, l'id s'hi afegeix en el mateix commit. Si una app
que la placa porta (el `custom_apps` de `platformio.ini`) desapareix, el cas es posa vermell i diu quin id falta.

**I tota app que es porti porta icona** (0.170.1, la norma de docs/40): el manifest declara `icon`, el PNG surt del
seu `icon-source.png` amb `tools/make_icon.py` i la prova `test_every_app_of_the_repository_carries_an_icon` no deixa
entrar una carpeta sense el dibuix. Una app portada a mitges es veu de seguida: la seva cel·la és una lletra.

**Una conseqüència que es tanca sola**: aquest document, `docs/40`, `docs/50` i `docs/90` nomenen apps de la
llista d'abans (per exemple `clock`, `battery` o `checkbox`) com a exemples. Cada nom torna a ser cert quan la
seva app es porta; el que no es fa és inventar-se l'app perquè el text quedi bé.
