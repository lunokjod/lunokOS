# Moonlight Installer: la botiga (0.200.0)

L'amo: *«Farem la botiga de descarregues de lunokOS. Li direm "Moonlight Installer". Mira la comanda "repo", hem de fer les
mateixes funcions... Et baixes la llista d'arxius, et baixes el manifesto.json i vas baixant les icones. Mostraràs una llista
d'apps amb la seva icona en una llista, sobre la llista una caixa de text de cerca, amb la seva lupa... Si toques una app de
la llista vas a una vista de detalls on tens tota la info de l'app. I el botó "Install" si l'app no està al sistema,
"Uninstall" si està i "Upgrade" si la versió es mes gran. En cas de tenir una app d'una versió mes avançada que la del repo
haurás de marcar-la amb un fons diferent»*.

| Fitxer | Què hi ha |
|---|---|
| `apps/moonlight/moonlight.lua` | les dues pàgines: la llista amb la cerca i la fitxa amb el botó |
| `apps/moonlight/store.lua` | el costat de github.com: el llistat, els manifests, les icones, instal·lar i treure, i `plain()` |
| `apps/moonlight/json.lua` | el lector de JSON que el Jellyfin ja porta |
| `apps/moonlight/res/` | `icon.png` (94) i `icon-source.png` (el mestre de 448); `res/icon/` té **cada versió del dibuix** i `passos.txt` |

**És una app d'administrador** (`"admin": true`) i no una comanda: la comanda `repo` fa aquest treball des del prompt amb el
nucli a la mà, i aquí el fa una pàgina amb les portes que una app té —`http`, `fs` i la taula `admin`—. És el que fa que
instal·lar i desinstal·lar es puguin fer des del vidre (docs/30, docs/70).

## El que baixa i on ho deixa

- **El llistat** és `https://api.github.com/repos/lunokjod/lunokOS/contents/apps?ref=main`, trenta-un kilobytes per a
  quaranta-una carpetes. `store.list_ids` el baixa **a un fitxer** (`http.download`, docs/75) i el llegeix **a trossos de dos
  kilobytes** buscant `"path": "apps/<id>"`, amb quaranta-vuit bytes de solapament per a una carpeta que caigui a la frontera
  de dos trossos. Una carpeta que porta un punt al nom és un fitxer i no una app. **La pàgina sencera no passa pel heap**, que
  és el que fa possible mirar una resposta de trenta-un kilobytes en una placa amb onze kilobytes en un tros.
- **El manifest de cada app** és `https://raw.githubusercontent.com/lunokjod/lunokOS/main/apps/<id>/manifest.json`, de dos
  kilobytes i mig, i **la icona és la que el manifest nomena** (`res/icon.png`, amb el camí penjat de la mateixa arrel crua).
- **Tot queda a `/data/moonlight/repo/`**: `listing.json` (l'última pàgina de la API), `m/<id>.json` (el manifest sencer, el
  que la fitxa vol), `i/<id>.png` (la icona) i `index.txt`, **una línia per app** amb `id`, `version`, `icon` i `title` i un
  tabulador entremig. L'índex és el que fa que la botiga **s'obri amb la llista que tenia l'última vegada**, sense tocar la
  ràdio, i `Refresh` el torna a fer.

## Dues versions es comparen número a número

`store.compare("1.10.0", "1.9.0")` és **positiu**: una comparació de textos diria el contrari i una app que s'ha d'actualitzar
seria la que no s'ha de tocar. El que no és un número compta com un zero, `1.2` i `1.2.0` són la mateixa versió i el que sobra
mana si tot el que hi ha abans és igual. És aquest número el que decideix què diu el botó de la fitxa.

## La llista

- **És una `ui.list` del sistema** amb les files fetes per l'app: un `ui.canvas` amb la icona i dues línies de text —el nom a
  `kBody` i, a sota, la versió i l'estat (docs/30 § `ui.list`).
- **Una fila sí i una no al revés** (l'amo: *«Pots fer que cada fila vagi al revés de l'anterior?»*): les senars porten la icona
  a l'esquerra i les paraules a la dreta, i les parelles són la mateixa fila girada —la icona a la dreta, el títol a
  l'esquerra—, amb la versió i l'estat intercanviats amb ella:

  ```
       [icona] [títol]                     [títol] [icona]
       [versió] [estat]                    [estat] [versió]
  ```

  El títol va **enganxat a la icona** i no a la vora de la fila (l'amo: *«el títol de l'app imparell hauría d'estar enganxada a
  l'icona de la part dreta, com ja ho estàn a la esquerra»*): la caixa de les paraules és la mateixa a les dues files i el que
  gira és contra quina vora toquen.

  La segona línia és **un grup i no dues columnes** (vegeu més avall): la versió a la vora del costat de la icona i l'estat
  enganxat a ella.
- **La fila és gairebé una fitxa: cent cinquanta-sis píxels i tres pisos** (l'amo: *«Les icones queden tallades a la llista! Has de
  fer les files mes altes, quasi fitxes»*): la icona ocupa el pis de dalt i fa **noranta-sis píxels, la mida del dibuix que una app
  porta** —no una imatge de noranta-quatre aixafada dins de seixanta, que és el que la feia semblar tallada—, el títol hi seu al
  costat i la segona línia va **a sota de la icona** amb tota l'amplada de la fila. Hi caben quatre files per pàgina, i amb
  setanta-sis la segona línia creuava la icona.
- **La versió i el seu estat són un sol grup** (l'amo: *«l'«up to date» al costat de la versió (i en text petit)»*): la versió
  toca la vora del costat de la icona i l'estat s'hi enganxa, una mida més baix (`kTiny`, vint-i-dos píxels de línia), o sigui
  `0.2.4  up to date` i, a la fila girada, `up to date  0.5.301`. Les dues caixes es tornen a col·locar quan el text canvia,
  perquè l'amplada d'un número no és sempre la mateixa.
- **El fons d'una fila és gris (`kNewer = 13`) quan aquesta placa té una versió més avançada que la del dipòsit.** És l'única
  manera que aquest vidre té de dir-ho sense una paraula més, i és el que l'amo ha demanat. La resta de files van amb el paper.
- **L'estat de cada app és una de cinc paraules**: `not here`, `here only` (el dipòsit no la té), `upgrade to X.Y.Z`, `newer
  here` i `up to date`.
- **Les pàgines les fa la llista del sistema, i el dit les passa** (l'amo: *«Botons "next" i "back" son lo puto pitjor. Deixa el
  recompte de pàgina i el conteig d'apps totals. Afegeix un "Slide for more!" (i registra els gestos a la llista)»*): totes les
  files filtrades són fills de la `ui.list`, que compta les que hi caben (`list_page_of`), dibuixa la tira de boles al peu, les
  passa amb el dit i amb les fletxes del dial i avisa l'app per `on_page`. **Els dos botons no hi són** i al seu lloc hi ha el
  comptador a l'esquerra —`2 / 11 - 42 app(s)`— i **`Slide for more!`** a la dreta, que només surt quan hi ha més d'una pàgina.
  La pàgina on era la persona es desa a `state` i es torna a demanar amb `list.page`.
- **I el número de pàgines el compta l'app amb la mateixa aritmètica del nucli** (`pages_of_the_list`, que copia
  `list_page_of`): la tira del peu es menja quaranta-un píxels —trenta-set del dibuix de les boles i quatre del filet—, i amb
  una fila de cent cinquanta-sis aquesta banda costava una fila sencera: tres per pàgina en lloc de quatre. Amb cent quaranta-vuit
  hi tornen a cabre quatre.
- **Una app que ja hi és es llegeix en un to més clar** (l'amo: *«Les apps instal·lades han de tenir un color diferent (text una
  mica mes clar)»*): el títol, la versió i l'estat d'una app que aquesta placa té van al nivell cinc i les que no hi són a la
  tinta. La fila que té el fons gris (*newer here*) es queda amb la tinta, que gris sobre gris no el llegeix ningú.
- **I el botó de refrescar és una fletxa circular, i està apagat** (l'amo: *«El botó refresh es massa retro, una fletxa circular
  i deshabilitada per ara (ja es refresca al iniciar, i lo ideal seria eperar 2h per habilitat el refresc)»*): la marca del
  sistema `reboot` —un cercle que torna a començar— sense marc, en tinta quan es pot demanar i en un gris mig (`kDim = 5`) quan
  no, i sense cap `on_click` en aquest cas: una porta que no fa res és pitjor que una porta que no hi és.
- **El termini són dues hores i el rellotge és el del disc**: `may_refresh()` mira el `mtime` de l'índex (el sistema el dona des
  de la 0.199.0) i un zero —que vol dir «ningú no ho sap», no «molt vell»— o una placa sense hora contesten *pregunta*. A
  l'arrencar es pregunta sol si el cau té més de dues hores, i amb el cau calent la botiga s'obre sense tocar la ràdio. Mentre
  la pàgina és al vidre es mira cada vint segons, i la fletxa s'encén sola quan el termini venç. Quan el refresc s'acaba la
  fletxa es torna a apagar i la línia de sota diu l'edat del cau (`asked just now`, `asked 40 min ago`).
- **I l'amplada d'un botó es mesura en pintar la pàgina**: al vidre es va veure `R...`, `B...` i `N...` —una clau de cent
  quatre píxels no hi porta set lletres— i, després de mesurar-la amb `ui.text_width`, encara `Refre...`, perquè la mesura
  s'havia pres **en carregar l'app**, quan la cara del sistema encara no era la que dibuixa la pàgina, i perquè la clau d'un
  botó es queda setze píxels de marge a cada banda i n'hi havia vint-i-vuit de sumats.
- **La icona es dibuixa en el moment de pintar i no en el de fer la fila**: `on_draw` pregunta cada cop quin camí hi ha —el del
  cau primer i el de l'app instal·lada si no—, o sigui que una icona que encara no ha arribat es veu quan arriba i no abans.

### Cada fila diu què és l'app en dues línies (0.200.4)

L'amo: *«Hi ha una zona que queda buida es pot posar text entre el títol i el cartell de "newer here", queda molta tarjeta. Pots
posar un trosset de l'inici de la descripció de l'app o si hi han captures de /res/shots/ uns retalls»*. La fitxa té tres pisos i
el del mig era paper: **el títol ara va al capdamunt** de la columna de les paraules i sota seu hi caben **dues línies del
començament de la descripció**, a `kTiny`, amb els punts suspensius només a l'última (`two_lines`) i alineades com el títol —a
l'esquerra, i a la dreta a les files girades, on les paraules van enganxades a la icona.

- **El que diu una app es demana una vegada**: el manifest ja és al cau després d'un Refresh, i `blurb_of` el llegeix el primer
  cop que aquella app surt en una pàgina i no el torna a llegir en aquesta arrencada (un fitxer i un descodificador per cada
  quatre files seria el que la llista no fa).
- **Les captures encara no hi són**: cap app del repositori no porta `res/shots/`, i el retall amb `ui.canvas` no s'arribava ni a
  fer en una fila que ja té la icona. El dia que una app en porti, el lloc és aquesta banda i el dibuix és un `cv:image` amb el
  retall centrat (l'amo: *«en farem mes tard de captures»*).

### La llista camina amunt i avall, i no té pàgines (0.200.2)

L'amo: *«Canviar el swipe dels costats per el d'amunt/avall dins del control de la llista d'aplicacions del repositori. Sense el
paginador es el mes natural»*. La llista del sistema gira **una pàgina** amb un dit de costat i dibuixa la tira de boles al peu, i
el que una botiga de quaranta-tres apps vol és caminar les files. Per això les files ja no són filles d'una `ui.list`:

- l'app es queda **la primera fila visible** (`first_`, que és el que desa la sessió) i construeix **només les que hi caben**, com
  a fills de la pàgina —cap fila no es pot veure tallada perquè cap no es fa fora de la caixa—;
- el dit que va **amunt o avall** la mou les files que ha caminat (`|dy| / kRow`, amb un llindar de seixanta píxels), i el gest
  es llegeix de `touch.gesture()` al bucle perquè **un gest no arriba mai a l'arbre dels controls** (docs/30, docs/90);
- el peu diu la finestra i el total —`1-4 / 43 app(s)`— i `Slide for more!` surt només quan en queda alguna a sota. **Els punts
  del paginador no hi són** perquè no hi ha pàgines.

**I dues coses es van aprendre mirant el vidre**:

- **una fila que no es dona es queda pintada**: les files viuen a la pàgina i no dins d'una llista, o sigui que ningú del sistema
  les torna; ara es fan amb `keep` i `build_rows` allibera les d'abans. La primera versió d'aquest canvi ensenyava **la llista
  sencera a sota de la fitxa**;
- **una etiqueta pinta el seu paper**, i el requadre de l'estat —de l'ample de la fila sencera i dibuixat després— **esborrava el
  número de versió de totes les files girades** (les que tenen les paraules enganxades a la icona de la dreta). L'amo ho va veure
  de seguida: *«Clarament es el text que te fons blanc i tapa, perqué es tant llarg?»*. Ara cada etiqueta es mesura amb
  `ui.text_width` i es torna a mesurar amb `cv:resize` quan el text canvia, i el paper d'una no arriba mai al de l'altra.

### La barra de la feina, i les claus mortes mentre dura (0.200.2)

L'amo: *«Tens una barra de progrés per la instalació/desinstalar/actualitzar oi que si??? I mentre esperem: botons deshabilitats
(install/uninstall/update) fins acabar el procés»*. Mentre una feina és pel mig la pàgina té una **`ui.progress`** de deu píxels:
al peu de la llista (amb la línia de l'estat pujant els catorze que ocupa) i a la fitxa, just a sobre de la clau, que li deixa
catorze píxels a la descripció.

- **El número és el que se sap i prou**: la cua del dipòsit sap quantes apps falten (`fet / total`), i una baixada d'una app no
  sap quants fitxers li queden fins que les llistes de les carpetes arriben —allà la barra va amb **−1**, que en aquest sistema vol
  dir «encara no ho sé»—. Una barra que s'inventa un percentatge és pitjor que una barra que diu que no ho sap (docs/30).
- **Les claus no es premien mentre dura**: un control d'aquest sistema no té `disabled`, o sigui que el que marxa és la tinta i el
  clic (`color = kDim` i sense `on_click`, i fora del recorregut del dial), el mateix parell que ja portava la fletxa de refresc.
- **La barra hi és abans que la feina comenci**: el torn que instal·la es queda dins la crida, i una barra que apareix quan ja ha
  acabat no és cap barra; per això la pàgina es torna a pintar amb `progress_` posat i després es fa la feina.

## La cerca: una caixa de text amb la seva lupa

**La lupa és el glif de FontAwesome** (l'amo: *«no es veu, es massa fina, busca una bon icona de lupa a la font que tenim
d'awesome»*): `res/marks.ttf` és **un sol glif** —el de la lupa, U+F002— escrit amb `tools/make_keys.py` en el byte `\1`, que
és el que una app pot escriure en un text d'aquest sistema (un codepoint de la zona d'ús privat no cap en una cadena d'un byte)
i dibuixat per `ui.label` amb `:font(marks_face)`. Un anell de dos píxels dibuixat a mà amb `circle` i `line` era un gargot en
aquest vidre; el glif té el gruix de la família i s'escala sol. La caixa és un `ui.textbox` del sistema al seu costat.

    python3 tools/make_keys.py --marks search=0x01 --family "lunokOS marks" --out apps/moonlight/res/marks.ttf

Escriure-hi **redueix la llista** pel nom o per l'id i **sense que comptin les majúscules** —qui escriu `jelly` vol trobar
`Jellyfin`—, i hi ha dues coses que no es fan:

1. **La pàgina no es torna a fer mentre es tecleja.** La caixa té el focus del teclat i el cursor, i alliberar-la a cada lletra
   seria perdre'ls i tornar a demanar el teclat. El que es refà és **la llista** i prou (`refilter`), i la caixa es queda on és.
2. **Una resposta que arriba no refà les files que ja hi són.** `update_rows` **toca les etiquetes** —el nom, la versió, el
   fons— i només torna a fer la llista si el que ha arribat no hi cabia, i encara així no a cada arribada: cada quatre i sempre
   l'última. Una fila és una icona i una icona és una descodificació de PNG; quaranta-una apps demanades d'una en una haurien
   estat tres-centes seixanta-nou descodificacions per ensenyar la mateixa llista (docs/75).

## La fitxa

La icona gran, el nom, l'id i l'autor, les tres dades (`here`, `in the repository`, `boards`), **la descripció sencera partida
en línies per l'app** —un manifest porta salts de línia i un `ui.text` d'una línia no els sap portar— i **un sol botó**:

Cinc dades i no tres (0.200.3, l'amo: *«falta el tamany del programa sencer (Mb/Kb/b) i la data de publicació (ctime del arxiu
manifest.json)»*): `here`, `in the repository`, `boards`, **`size`** i **`published`**. La mida és la carpeta de l'app **de debò**
—`store.footprint` la camina i suma el que el disc gasta, que no és el que un manifest promet— i la data surt de l'`mtime` del
seu `manifest.json`. Una app que no hi és no té ni mida ni data i les dues línies diuen `-`, i una placa sense rellotge tampoc no
en té: un zero vol dir «no ho sé» i no l'u de gener (docs/30, docs/70).

**I la carpeta es busca on és i no on s'instal·la** (es va veure a l'emulador: la fitxa deia `size: -`): `apps_root()` contesta el
volum on va una app **nova** —la targeta primer i la flash després—, i en una placa amb targeta això no és on són les apps. Una
pregunta que només llegeix ha de demanar el disc del qual l'app va sortir, provant els dos i quedant-se el que contesta el nom.

| El que hi ha | El botó |
|---|---|
| l'app no hi és | `Install` |
| hi és i la del dipòsit és més nova | `Upgrade` |
| hi és i és la mateixa, o **la d'aquí és més nova** | `Uninstall` |
| és la botiga mateixa | cap: una app no es treu a si mateixa mentre corre |

El botó deixa el que ha de fer a `asked` i **ho fa el bucle**, que és l'única banda on es pot esperar una descàrrega (docs/30,
docs/85). Mentre baixa, la línia de sota diu cada fitxer.

**`Upgrade` vol dir pujar i no baixar** (i es va veure al vidre): la fitxa d'una app que és *newer here* oferia `Upgrade` cap a la
versió vella del dipòsit. El botó només és `Upgrade` quan l'estat és `upgrade to X.Y.Z`; quan la d'aquí és més nova el que hi ha
per fer és treure-la, i diu `Uninstall`.

**La clau de treure porta la paperera dins** (0.200.0, l'amo: *«Al botó "Uninstall" de la vista detalls afegeix al botó una icona
de paperera (dins, amb el label) i el títol títol dins el botó»*): `button.icon = 'trash'` amb `icon_side = 'left'`, que és la
marca del sistema que la targeta i la fitxa comparteixen —dinou glifs a `tools/iconart.py` des d'aquesta versió, i la paperera
és l'`U+F1F8`—, i el nucli li deixa un quadrat de l'alçada de la cara al costat i centra la paraula a la resta (docs/30, docs/45).
Un botó amb la marca dibuixada per l'app hauria de girar també la tinta de la marca quan el dial hi cau a sobre, i això només
ho sap fer el nucli.

**I les tres claus de la fitxa són tres tons i tres marques** (0.200.1, l'amo: *«Al botó Uninstall posa el color del botó en un
to mes fosc, no el mateix color que els altres. El botó d'install que sigui mes claret i amb una icona de polze amunt»* i *«El
botó Actualitzar, amb una icona d'actualitzar (no sé, fletxes amunt?)»*): `Install` es queda el paper (15) i el polze
(`thumb`, l'`U+F164`), `Upgrade` baixa un graó (13) i porta dos xevrons apilats (`upgrade`, l'`U+F102`, el mateix pes del xevró
de tornar), i `Uninstall` baixa fins al mig (9) amb la paperera —el to que el dit troba sense buscar-lo i que el dial no ha de
ser el primer a oferir—. **La clau és alta** (82 px i no 64): la marca d'una clau és un quadrat de l'alçada de la **cara** i el
dibuix es queda un cinquè del quadrat d'aire, o sigui que en una cara de seixanta el dibuix tocava la vora del botó i el que es
veia era el requadre de la icona; amb setanta-vuit de cara hi ha vuit píxels d'aire a dalt i a baix i la icona es llegeix sola
(docs/30, docs/45).

**I la descripció és una caixa de només lectura** (0.200.1, l'amo: *«La vista detall, caldrà posar la descripció dins una caixa
de text de només lectura»*): `ui.textbox_multiline` amb `readonly` i `wrap`, sense vores (`framed = false`) i amb la cara del
sistema a `kSmall`; **rep el lloc que deixen les dades i el botó**, o sigui que la fitxa d'una descripció de tres línies i la
d'una de vint fan el mateix dibuix, i quan el text no hi cap **el cantó dret és la barra del desplaçament** i el dit el mou
(el gest el fa el sistema i n'hi ha prou amb registrar-hi `on_swipe`). Abans era una pila de `ui.label` partides per l'app amb
`ui.text_width`, que es menjaven la mitja línia de sota i no es podien moure (docs/30, docs/90).

**I treure una app es pregunta abans** (0.200.0, l'amo: *«Espero que aquest botò tingui modal "Are you sure?" amb els botons ben
amples»*): la clau no fa `asked = 'remove'`, obre una **targeta del sistema** (`ui.dialog`, l'ample de la pàgina i cinc-cents
d'alçada) amb el nom de l'app, el que se'n va —la carpeta, la memòria cau i les alarmes que va deixar a la cua— i el que es queda
—`/data/<id>` i `/cfg/<id>`—, la nota `it can be installed again` en to fluix i **dues portes de setanta-un píxels** que es
reparteixen l'ample. El dial comença a `Keep it` i no a `Uninstall`: la vora de tres (`emphasis = 1`) i l'anell (`choice = 1`) diuen
el mateix, perquè treure una app no és el camí fàcil. El que la targeta accepta no s'executa dins del ganxo: deixa `asked = 'remove'`
i el bucle tanca la targeta pintant la fitxa i després treu l'app (docs/30, docs/90).

**I el text del manifest es passa per `store.plain`**: un manifest és UTF-8 i la cara del sistema és Latin-1, i la descripció de
l'`about` sortia al vidre com `Back â€”or a pinchâ€”`. El que es pot tornar a Latin-1 es torna (`C2`/`C3` més un byte és un byte
Latin-1: `·` és `C2 B7` en UTF-8 i `B7` al vidre), la puntuació que un manifest fa servir de debò es converteix en ASCII (el guió
llarg en `--`, les cometes llises en `"`, l'el·lipsi en `...`) i la resta es treu en lloc de dibuixar-hi tres caixes (docs/30,
docs/45).

## Una app arriba sencera o no arriba

La mateixa regla que `repo install`:

1. els fitxers van a **`<disc>/apps/.moonlight/<id>/`**, on `<disc>` és el volum on viuen les apps (`sys.settings().unit`:
   `/sd/apps` o `/littlefs/apps`) —una carpeta que el llançador veu com una carpeta sense manifest i per tant com res—;
2. quan l'últim fitxer hi és, **el manifest és l'última porta**: una carpeta de fitxers sense `manifest.json` no és una app i no
   pren cap nom;
3. la carpeta vella marxa i la de feina es renombra a `/apps/<id>`.

**I això va costar una estona de trobar** (0.200.0, amb el registre de la placa dient `a file of the app did not come:
banner.lua (E_UNAVAIL)`): `/apps` **no és una carpeta, és una porta** —`vfs::resolve` contesta un nom seu amb el disc que ja
té aquell nom—, i una carpeta que encara no existeix no la contesta ningú. Escriure-hi amb `http.download` acaba amb
`E_UNAVAIL` («the place the page was to go is not on this board»), i no és cap problema de permisos: la comanda `repo install`
del prompt tampoc no hi escriu, escriu a **`/sd/apps/.<id>.install`**. La botiga fa el mateix, i per això la feina es fa al
volum i el nom es pren amb un `fs.rename`. El mateix registre, abans, deia només `a file of the app did not come` sense dir
per què: ara cada fallada porta el codi (`HTTP 404`, `the file came empty`, `E_UNAVAIL`), i un fitxer que hi era es treu
abans de baixar-lo (l'amo: *«En cas de instalar o actualitzar tens permís per esborrar fitxers preexistents»*).

**I el que es buida és una llista curta de camins** (0.200.0): `store.drop` és l'única funció de la botiga que esborra fitxers,
i un camí fet amb un nom buit —`<disc>/apps/` **és la porta** i no una carpeta— hauria recorregut cada app de la placa i les
hauria buidades una per una. Ara `drop` només accepta `<disc>/apps/<id>` i `<disc>/apps/.moonlight/<id>`, i la resta de camins
els refusa amb un `false` sense tocar res.

Una cadena que mor a mig camí deixa **l'app de davant intacta** i llença la carpeta de feina. Treure una app és
**`admin.uninstall`** —la carpeta, les alarmes de la cua, la memòria cau de sessions i la generació de la llista d'apps—, i el
que no es toca són `/data/<id>` i `/cfg/<id>`, que són de la persona (docs/30, docs/70).

## La cua: una app per torn

Una descàrrega és una espera i una espera dins d'un ganxo no es pot fer: `Refresh` **demana el llistat, omple la cua i torna**,
i cada torn del bucle baixa **una** app (el manifest i la seva icona) i diu què queda a la línia de sota (`40 left · 2048`).
Quaranta-una apps són quaranta-un torns d'un segon i la pàgina es pot tocar mentre això passa. **La primera vegada que s'obre,
si no se sap res del dipòsit, la botiga es pregunta tota sola**: una pàgina buida que espera un botó és una pàgina que no diu
què fer.

## La icona: la lluna i els seus passos

**La dibuixa `tools/make_moonlight_icon.py`**, que escriu cada versió a `res/icon/` i el mestre a
`res/icon-source.png`; `tools/make_icon.py` el baixa a la icona de 94 que el manifest nomena. El mestre i les versions
viuen a `res/` perquè l'amo ho va demanar (*«Guarda el master i si pot ser els passos que has fet i totes les versions que
hem fet a res/»*) i perquè una icona sense la recepta és una imatge que ningú no pot tornar a fer: la carpeta diu els
grisos, el gir, què es va provar i per què es va descartar (el detall és a `res/icon/passos.txt`, i el resum és aquí).

- **Els quatre tons surten dels dibuixos del projecte**, mesurats als seus mestres: 68 la tinta, 170 l'ombra, 221 el gris
  clar dels farciments i 255 el paper. Amb aquests quatre un dibuix té volum sense cap degradat (docs/45).
- **Res foradat**: els cràters són grisos amb la seva pròpia ombra i no forats blancs —l'amo: *«Em referia a usar color gris
  i ombres.... No a que foradessis el dibuix xD»*.
- **La lluna va girada 30 graus i cap amunt**: amb el gir cap avall el cantó de la llum mira a terra i la figura sembla que
  caigui (*«així sembla que cau»*); cap amunt sembla que surti.
- **Els núvols van davant**: sis lòbuls, base plana, contorn de tinta i dues bandes d'ombra, tapant la panxa de la lluna. El
  núvol que l'amo va portar (`0-nuvol.png`) és a la carpeta com a peça d'una de les versions provades, no com a dibuix final.
- **Els passos que viatgen amb l'app van a 224 i en `LA`**: són un record i no el dibuix que s'edita, i la carpeta sencera
  fa cent quaranta kilobytes en lloc de tres-cents.

## El que encara no fa

- **No mira les dependències ni les plaques**: instal·la el que el dipòsit té i prou, i una app que aquesta placa no pot córrer
  es veu a la llista i s'hi instal·la igualment —el filtre de `works_here` és de la porta, no del manifest.
- **No pot actualitzar-se a si mateixa**: una app que es renombra mentre corre és una pàgina que desapareix a mig dibuix, i per
  això `install` i `uninstall` de `moonlight` es refusen. El dia que la botiga canviï, es copia des del prompt.
- **No firma res**: el que arriba és el que hi ha a la branca `main` d'aquest dipòsit i la confiança és la de github.
