# 46 · L'EPUB: el motor del paper i el port a aquest sistema

Aquest document és el **pla** de portar el lector d'EPUB de `OLD/PaperOS` a aquest sistema, i la
referència de cada peça mentre es fa. No és un resum: és la llista de funcionalitats, una per una,
amb l'ordre en què s'han de fer i amb el que li falta a aquest sistema per a cada una.

Es llegeix així: la **§2** diu què hi ha al paper (fitxer a fitxer i amb quines crides); la **§3**
és la llista numerada —`A1`, `B3`, `C7`…— i és la que mana; la **§4** compara els dos maquinari i
les dues màquines de Lua; la **§5** és l'ordre de les fases, un commit per fase.

## 1 · Les dues meitats

L'EPUB del paper **no maqueta res**: demana al sistema la pàgina ja composta i la pinta. El
repartiment, que és la decisió de l'amo del 23/10 (*«veig que implementar en lua el lector d'epub
dona problemes... fem-ho com amb les imatges, un render a un canvas»*), és:

| Meitat | Què fa | On viu |
|---|---|---|
| **El motor** | obre el ZIP, llegeix l'XHTML, mesura les paraules, decideix on va cada línia i compon la pàgina en un canvas | C++ del sistema (`src/core/`, `src/system/`) |
| **El producte** | el prestatge, l'índex, els menús, el peu, la posició de cada llibre | l'app de Lua (`pos_launch.lua`, 3 959 línies) |

El contracte entre les dues meitats, que és el que aquest port ha de respectar:

```
paperos.epub.open(unit, path) → book
book:chapter(href) → bytes              infla UN capítol a la PSRAM
book:measure(from, w, h, opts) → next   compta una pàgina sense pintar-la
book:page(from, w, h, opts) → surface, next, hasPicture, used, levels
book:close()
surface:draw(x, y) / width() / height() / free() / save(path)
```

**La posició de lectura és un byte dins del capítol.** No és un número de pàgina: una pàgina és
`page(from, …)` i torna `next`, i el que es desa per llibre és `(capítol, byte)`. Per això el
tokenitzador dona l'offset de cada paraula i per això `measure()` existeix.

## 2 · Què hi ha al paper, fitxer a fitxer

| Fitxer | Línies | Què fa |
|---|---|---|
| `src/core/zip_archive.{h,cpp}` | 130 + 508 | El **contenidor ZIP**: recorregut del final (EOCD), directori central, capçalera local per entrada, stored (0) i deflate (8), `find` per nom, `extract` amb un bloc del qui crida. Refusa per nom: ZIP64, xifrat, mètodes que no siguin 0 o 8, noms de més de 127 B, entrades de més de 2 GB, i un deflate més gran que el seu contingut (bomba). |
| `src/system/storage_zip_source.h` | 84 | L'adaptador entre el ZIP i la memòria del sistema (`readChunk` en trossos de 512 + 1). |
| `src/core/epub_html.{h,cpp}` | 242 + 773 | L'**XHTML d'un capítol, paraula a paraula**: el byte de cada paraula, el bloc que l'obre, les entitats (inclosa la Latin-1), `script`/`style`/`head`/comentaris sencers fora, `<br>`/`<hr>` com a tokens, les imatges amb el costat on floten, `b`/`i`/`sup`/`sub` amb comptador de profunditat, `<pre>` i els marcadors de taula (`<td>`, `</td>`, `</tr>`, `</table>`). |
| `src/core/epub_layout.{h,cpp}` | 105 + 836 | **Una pàgina composta dins d'un canvas**: el llomall en píxels, la cara de cada bloc, l'aire abans de cada bloc, la sagnia de cita i de figura, el peu de figura centrat, les imatges flotades amb bandes perquè les paraules les envoltin, `<hr>`, les **taules com a reixa** (columnes comptades amb una sonda sobre el mateix capítol), i el final de pàgina que mai no parteix una imatge ni una fila. També el **mode mesura**: la mateixa aritmètica sense tocar un píxel ni descodificar una imatge. |
| `src/system/epub_book.{h,cpp}` | 155 + 486 | **El llibre obert**: un capítol inflat a la memòria gran (sostre 1 MB), una imatge en cau (sostre 2 MB), la resolució de l'`href` relatiu al capítol, i el recompte de grisos de la pàgina composta (`levels`). |
| `src/system/lua_api.cpp` (EPUB/ZIP) | ~300 | La **porta de Lua**: `epub.open`, `book:chapter/page/measure/close`, `zip.entries/open/extract`, i l'objecte d'una entrada (`size/name/read/close`). |
| `test/native/tests_zip.cpp` | 442 | El ZIP amb un fitxer de joguina fet a mà i blocs que ha comprimit el zlib de debò. |
| `test/native/tests_epub.cpp` + `_styles.cpp` | 718 + 184 | La maquetació, els blocs, les taules, les imatges flotades i els estils. |
| `data/app/EPUB/pos_launch.lua` | **3 959** | L'app: el prestatge, el llibre, els menús, el peu, la llista de lectures. |
| `data/app/EPUB/EPUB.md` + `PENDENT.md` | 494 + 258 | El disseny i els catorze forats coneguts del lector. |

## 3 · La llista de funcionalitats

Cada fila és **una cosa que es pot fer i provar tota sola**. «Avui aquí» diu què hi ha al lunokOS
**verificat al codi**, no al full de ruta.

### A · El motor (C++, dins del sistema)

| # | Funcionalitat | Al paper | Avui aquí | Què cal |
|---|---|---|---|---|
| **A1** | **El contenidor ZIP**: llegir el final del fitxer, el directori central i les capçaleres locals; una entrada `stored` i una de `deflate`; buscar per nom; treure una entrada a un bloc del qui crida; refusar ZIP64, xifrat, mètodes estranys, noms llargs i bombes, cada un amb la seva frase | `zip_archive.cpp` 508 + `storage_zip_source.h` 84 | **Res.** `lib/inflate` té el deflate i el `crc32`, que és mitja feina; `docs/70` diu que el contenidor no hi és | `lib/zip` (~500 línies) + l'adaptador sobre `hal::IStorage` (~80) + proves (~400) |
| **A2** | **El tokenitzador XHTML** — **fet el 7/11 (0.128.0)**: `lib/epub` (**186 + 805 línies**) i **12 casos** a `test/test_epub` | `epub_html.cpp` 773 | **No hi havia res**: aquí només hi ha `lib/json`, i cap lector d'XML ni d'HTML | Fet: paraules amb el byte, blocs, entitats, `script`/`style`/`head`/comentaris sencers fora, `br`/`hr`, imatges amb costat, `b`/`i`/`sup`/`sub` amb comptador, `<pre>` i els quatre marcadors de taula. **La sortida és d'un byte per codi** (Latin-1), que és el que la cara del llibre i `ttf::glyph_of` volen |
| **A3** | **El paginador** — **fet el 7/11 (0.130.0)**: `lib/epub` part 2 (`page.h` **138 línies**, `page.cpp` **646**) i **12 casos** a `test/test_layout` | `epub_layout.cpp` 836 | **No hi era**. El canvas i les mètriques sí que hi són: `ui::canvas_text`, `font_text_width`, `font_line_height` | Fet, i **reescrit**: aquí no hi ha `IFont`/`ICanvas` amb glifs i bitmaps. Una pàgina surt com un **pla** (paraules amb el seu lloc, regles i caixes de foto) i el nucli la pinta |
| **A4** | **Els senyals dins d'una línia** — **fet el 7/11 (0.129.0)**: `font::Ink` (negreta, cursiva i salt) a `lib/kernel/src/font.cpp`, `ui::canvas_text_styled` a `lib/kernel/src/ui.cpp` | `epub_layout.cpp` (`drawGlyph`, `drawRun`) | **No hi era**: `canvas_text` pintava una cadena sencera amb una cara i prou | Fet: la **negreta és un píxel més a la dreta** (el glif pintat dues vegades, que és el que fa una cara d'un sol pes), la **cursiva és un biaix fila a fila** (un cinquè de l'altura del glif, de la fila de baix a la de dalt) i el **`sup`/`sub` és un terç de línia amunt i un cinquè avall**, sense fer créixer la línia |
| **A5** | **El llibre obert** — **fet el 7/11 (0.131.0) llevat de la imatge en cau**: `kernel/book.h` (**88 línies**) i `lib/kernel/src/book.cpp` (**390**), 4 casos a `test/test_book` | `epub_book.cpp` 486 | **No hi era** | Fet: `open` amb l'adaptador sobre `hal::IStorage`, la taula d'entrades a la memòria extra (**128** com a sostre), `chapter(name)`/`chapter_at(i)` que inflen un capítol fins a **512 KB** al mateix bloc, `first_chapter()` (el nom que sembla un capítol, C2) i `page(from, w, h, style, fonts)` amb les mètriques de les tres cares. **Falta** el `member(href)` amb una imatge en cau: una pàgina amb fotos en col·loca els mots i no la foto |
| **A6** | **El recompte de grisos d'una pàgina composta** (`levels`): es passeja pels píxels i compta quants nivells hi ha, que és el que decideix si la pàgina es pot girar amb `du` o necessita una passada que esborri | `levelsOf()` a `epub_book.cpp` | El concepte hi és (`hal::UpdateMode`, docs/50) però **cap app no pot preguntar-ho d'una pàgina** | Sortirà sol si la pàgina es compon dins d'un canvas de l'app: el nucli ja sap comptar els nivells (`ui::levels_shown`) |
| **A7** | **La disciplina de límits i de pila**: sostres declarats (capítol 1 MB, imatge 2 MB, nom 127 B), deflate en un bloc del qui crida (no 32 KB de finestra pròpia), taules del descompressor fora de la pila (el `loopTask` fa 8 KB i el paper va reiniciar la placa dues vegades per això) | tot el `core/` | Aquí el `lib/inflate` ja treballa sobre un bloc del qui crida i sense al·locar | Es manté la regla: **cap funció del motor al·loca**, i els blocs grans van a `extra_memory` |

### A2 · El que diu el tokenitzador, i les tres taules que són una decisió

`epub::Tokens` camina el capítol des del byte que li donen i torna un `epub::Token` per crida: **una
paraula** (amb el `block` que l'obre —només la primera del bloc, que és qui paga l'aire i la cara— i el
byte `at`), **una imatge** (amb l'`href` dins del zip, `at`, `after` i el costat on flota, llegit de
`style`, `align` o `class`), **un salt** (`<br>` o `<hr>`) o **una marca de taula** (obre cel·la, tanca
cel·la, tanca fila, tanca taula, en una cua de quatre: `</td><td>` són dues marques seguides i amb un
sol senyal se'n perdria una de cada fila).

Tres taules del fitxer són una **decisió** i no una traducció del paper:

1. **Les entitats**: les cinc de l'XML, les **60 de Latin-1** (`&eacute;` és `0xE9`, i això va ser un
dia una pàgina amb `caf&eacute;` al vidre) i les de la puntuació. Un nom que no és real queda **tal com
es va escriure**, que és el que vol veure qui llegeix un llibre vell.
2. **Els substituts de la puntuació**: la cara del llibre és Latin-1 i la canonada és d'un byte per
caràcter, o sigui que el guió llarg és `-`, les cometes `“ ”` són `"`, els punts suspensius són `.`, el
bullet és el punt volat `·`, el guionet bla (`&shy;`) no pinta res i l'espai dur és un espai. Una
ratlla llarga pintada com a `?` seria pitjor que una de curta.
3. **El rang C1 és Windows-1252 i no Latin-1**: `&#146;` és un apòstrof i `&#150;` un guió, que és el
que vol dir en un llibre dels noranta; els cinc bytes que cp1252 no fa servir són `?`.

I el que **no** fa, dit aquí perquè ningú el busqui: no llegeix el CSS del llibre, no fa columnes ni
dreta-a-esquerra, i **un byte que no és UTF-8 es tracta com a Latin-1** (n'hi ha de llibres etiquetats
d'una manera i escrits d'una altra, i un `é` sol no ha de ser un `?`).

### A3 · Una pàgina és un pla i no un dibuix (7/11, 0.130.0)

El paper componia la pàgina **pintant** dins d'un canvas mentre la maquetava, perquè el seu canvas era d'un
bit i un glif era un bitmap que hi enganxava. Aquí el text es pinta amb `font::runs` i `ttf`, que són del
nucli i saben de cares, escales i tinta, o sigui que el paginador calcula **on** i el nucli fa **què**:
`epub::layout()` omple un `Sheet` (una llista d'`Item` i un **dipòsit de paraules**) i en diu el resultat.

D'això en surten tres coses, i les tres són el motiu:

- **mesurar una pàgina no costa res**: és la mateixa llista i ningú no la camina (B4);
- **una pàgina es pot provar sense pantalla**: amb un regle de cara que diu que tota lletra fa sis píxels i
  tota línia deu, l'aritmètica d'una línia i la d'una taula es proven amb llapis (`test/test_layout`, 12 casos);
- i **la memòria és del qui crida**, com a tot el sistema: la llista i les paraules, perquè una paraula d'un
  capítol és un tros d'un buffer que no acaba en zero (A7).

Un `Item` és una **paraula** (`x`, `y`, cara, escala i tinta), una **regla** (l'`<hr>` o el subratllat d'una
capçalera) o la **caixa d'una foto** amb l'`href` al dipòsit. El que el paginador fa amb ells és el del paper
i amb les seves xifres: la línia es trenca on la paraula no hi cap, l'aire d'un bloc és una fracció de la
línia del cos (títol 2/3, `li` 1/4, `dt` 1/2, `dd` 1/6), la foto flotada té una banda del 45 % de l'amplada,
una foto que no hi cap **obre pàgina**, i una taula compta les columnes amb una sonda abans de col·locar la
primera cel·la.

I **una cosa que el paper no sabia fer**: la sagnia d'una cita. Allà sortia del bloc que obria la paraula, i
dins d'una cita el bloc és `p` com qualsevol altre, o sigui que `<blockquote><p>…</p></blockquote>` —com
escriuen els llibres— no sagnava mai. Aquí el tokenitzador **compta** les cites, les figures i les
definicions i el `Token` porta `inset` (0.130.0).

### B · La porta de Lua

| # | Funcionalitat | Al paper | Avui aquí | Què cal |
|---|---|---|---|---|
| **B1** | `epub.open(unit, path)` → objecte llibre, amb el motiu quan no és un llibre | `lEpubOpen` | **No hi és** | `luabind`: un objecte amb metataula i `__gc`, com `paperos.epub.book` |
| **B2** | `book:chapter(href)` → bytes del capítol, i `close()` | `lEpubChapter` | **No hi és** | el mateix |
| **B3** | `book:page(from, w, h, opts)` → la pàgina composta | `lEpubPage` | **No hi és** | Vegeu la decisió **D2**: superficie nova o directament dins d'un canvas de l'app |
| **B4** | `book:measure(from, w, h, opts)` → on acaba la pàgina, sense pintar | `lEpubMeasure` | **No hi és** | el mateix, sense canvas |
| **B5** | Les opcions d'una pàgina: `face`, `title_face`, `small_face`, `dark` | `epubStyleFrom` | Les cares es donen per **número** (`ui.load_font`) i no per nom | `{face = <número>, title_face = …, dark = true}`, i `paper`/`ink` segons `dark` |
| **B6** | `zip.entries(unit, path)` → la llista d'entrades | `lZipEntries` | **No hi és** | surt de l'A1; l'app en llegeix el `container.xml` i l'OPF |
| **B7** | `zip.open(unit, path, name)` → entrada amb `size()/name()/read(from,len)/close()` | `lZipOpen` + l'objecte | **No hi és** | el mateix |
| **B8** | `zip.extract(unit, path, name, destUnit, dest)` → una entrada a un fitxer | `lZipExtract` | **No hi és** | el mateix; aquí, a més, `img::decode` llegeix **de memòria**, o sigui que la portada no cal extreure-la |
| **B9** | `surface:save(path)` (la miniatura `.ics`) | `ImageSurface::save` | **No hi és** (`ui.image_save` no existeix) | Decisió **D4**: afegir-ho o deixar córrer el cau de miniatures |
| **B10** | `opens: "epub"` i `app.args.path` | `paperos.opened_file` | **Sí** (0.120.0) | res |

### C · L'app (Lua)

| # | Funcionalitat | Al paper | Què cal aquí |
|---|---|---|---|
| **C1** | **El prestatge**: els `.epub` de `/epub` de cada unitat, en pàgines, amb la tira de boletes del sistema (16 com a molt, la finestra al voltant de l'actual) | `shelf*` de l'app | `fs.list`/`fs.mounts` i `ui.dots` ja hi són |
| **C2** | **Obrir un llibre**: `META-INF/container.xml` → OPF → `dc:title` + `manifest`/`spine`, i si l'OPF no es llegeix, les entrades `.xhtml`/`.html`/`.htm`/`.xml` fora de `META-INF/` per ordre de nom | `openBook()` | B6 (o B7 llegint-lo a trossos) |
| **C3** | **Passar pàgina**: tecles, toc al costat, lliscament lateral; dos tocs al centre (dins de 1500 ms) demanen la pantalla sencera | `nextPage/prevPage`, `on_tap` | les tecles i el toc hi són; **la pantalla sencera no** (D1) |
| **C4** | **Els menús** `Book` (Shelf, Contents) i `View` (Full screen, Dark page / Light page) | `paperos.ui.menubar` | `ui.menu_bar` hi és |
| **C5** | **El peu**: capítol i percentatge, i les boletes de les pàgines del capítol (14 de finestra, tres de més petites als caps, sostre de 512) | `foot*` | B4 per comptar, `ui.dots` per pintar. El comptatge va **de fons**, unes quantes pàgines per volta del rellotge |
| **C6** | **La llista de lectures**: `/epub/reading.txt`, una línia per llibre `unitat TAB camí TAB capítol TAB byte TAB portada`, 200 com a molt, i el més vell cau | `readingList*` | `fs.read`/`fs.write` sobre la unitat del llibre: res de sistema |
| **C7** | **La portada com a estat**: la primera pàgina del primer capítol comença al byte zero, o sigui que «sóc a la portada» no es pot deduir del byte | `book.cover` | res |
| **C8** | **La pantalla de càrrega**: «Opening the book…» i el nom del fitxer, i el llibre s'obre **al torn següent** (el panell només viatja quan el callback s'ha acabat) | `openOnNextTurn` | el bucle de l'app: res |
| **C9** | **El paper fosc i el clar**: només per a les pàgines del llibre, no per al prestatge ni per a la portada, i **no es desa** | `dark` | B5 (`dark = true`) |
| **C10** | **La passada de la pàgina**: `du` quan la pàgina que marxa i la que arriba són de dos tons, i una passada que esborri quan no; `levels` de les dues | `turnPage()` | aquí: `ui.present('du')` / `ui.present('gc16')` |
| **C11** | **El cau de miniatures** `.llibre.epub.cache.ics` al costat del llibre (64 px, `ICS1`), amb l'espai preguntat **abans** d'escalar i el cau dins d'un `pcall` | `saveThumb()` | B9 + `fs.space` (que ja hi és) |
| **C12** | **Un error és un avís**: el registre, la ratlla del prestatge i un avís del sistema que, tocat, obre la targeta amb la frase sencera | `oops()/showError()` | `ui.notification_bar` + `ui.dialog` hi són |
| **C13** | **Dues cares del llibre**: `/fonts/book.pof` pel cos i `/fonts/book-sans.pof` pels títols, i el sistema si no hi són | `bookFace` | `ui.load_font` accepta `.ttf`/`.bdf`/`.pcf`/`.lfont`: les cares del paper s'han de **refer** en un d'aquests quatre formats |
| **C14** | **L'índex del llibre** (`Contents`) | **No hi és al paper** (`PENDENT.md` pas 1) | Es fa aquí o no es fa: no és cap regressió, però és la meitat d'un lector |
| **C15** | **Anar a una pàgina pel número** | **No hi és al paper** (pas 14) | igual |

### D · El que el sistema encara no té i l'EPUB demana

| # | Què | Per què l'EPUB el necessita | Mida |
|---|---|---|---|
| **D1** | **La pantalla sencera**: amagar la barra de 48 files i donar a l'app les 540 × 960, amb el gest de sortida del sistema | El lector del paper té `paperos.fullscreen` i sense això perd 48 files i el gest de dos tocs | ~150 línies al nucli (vista, barra, toc) |
| **D2** | **Com torna la pàgina**: un objecte superfície nou (com el paper) o **compondre dins d'un canvas de l'app** | És la decisió de menys codi: aquí la vista ja és un canvas i no caldria `free()` ni un tipus nou | 0 si és dins del canvas; ~200 si és un objecte |
| **D3** | **Cares amb accents** — **resolt el 7/11 (0.127.0)**: el dibuix d'un text preguntava a la cara només pels codis per sota de `0x7F` i ara li pregunta per **qualsevol byte**; la cara del llibre és `/fonts/dejavuserif-latin1.ttf` (**23 856 B, 232 glifs**) | Un llibre en català és ple de `à è í ò ú ç · « » — …`; tota la canonada de text és d'**un byte per caràcter** (`ttf::glyph_of(face, uint8_t code)`) | Fet amb `tools/ttf_latin1.py`, que retalla el DejaVu Serif i **aplana els glifs fets de peces** (el lector refusa un glif muntat de trossos: 55 glifs compostos, el més gran de 85 punts i 5 contorns). Queda el tokenitzador convertint UTF-8 → un byte (fase 2); les **tres cares del sistema segueixen sent ASCII** |
| **D4** | **Pintar un bloc de píxels en un canvas** | Les imatges del llibre surten del ZIP a la memòria i `img::decode` dona files; `canvas_image` llegeix un **fitxer** | `ui::canvas_bitmap` (~40 línies) |
| **D5** | **El text amb èmfasi** (A4) — **fet el 7/11 (0.129.0)**: `font::Ink` a les dues canonades del text (outlines i bits) i `canvas_text_styled` | Negreta i cursiva són a la llista de funcionalitats del paper | Fet a `lib/kernel/src/font.cpp` i `lib/kernel/src/ui.cpp`; quatre casos a `test/test_font` compten els rectangles |
| **D6** | **La mida de l'app**: 3 959 línies i **131 161 B** (100 499 B sense comentaris) contra un sostre de font de **49 152 B**, i `load`/`dofile` són `nil` | L'app no hi cap i no es pot partir en mòduls | Decisió: sostre a ~160 KB, o `require` per a les apps |
| **D7** | **Els ajustos d'una app** (`prefs`) | El paper hi desa l'últim llibre i la unitat | Un fitxer a `/cfg` de l'app: res de sistema |

## 4 · Els dos maquinari, i les dues màquines de Lua

| Cosa | Paper | Aquí | Què vol dir per al port |
|---|---|---|---|
| Pantalla | 540 × 960 | 540 × 912 (sota la barra) | D1, si el lector ha de tenir les 960 |
| Caixa de text del paper | 508 × 801 | 500 × ~800 | la maquetació s'adapta sola: tot ve d'arguments |
| Cares | **sis, d'un bit, totes en negreta**, de 16 a 70 px | **TTF d'outlines** amb avanç per glif; les del sistema encara ASCII imprès, i la del llibre amb Latin-1 | A4 (èmfasi); D3 ja hi és per a la cara del llibre |
| Cares per app | — | **3** (`kMaxFaces`) | cos + títol + (res més): l'app del paper en fa servir dues |
| Memòria gran | PSRAM, capítol 1 MB, imatge 2 MB | `pal::extra_memory()`, **4 MB al Paper** | A5 |
| Màquina de Lua | 128 KB, 400 000 instruccions per callback | **4 MB al Paper** (diu el seu sostre), 128 KB a l'ATOM | el comptatge de pàgines de fons potser no cal; es mesura |
| Deflate | `core/deflate.h` | `lib/inflate` (raw, zlib, crc32) | A1 és **menys** feina que al paper |
| Imatges | des d'un fitxer (cal extreure) | **`img::decode` des de memòria** | B8 no cal per a les portades: es descodifiquen del bloc |

## 5 · Les fases, i què ha de provar cada una

| Fase | Què es fa | Què queda provat |
|---|---|---|
| **1** | **`lib/zip`** (A1) i el seu adaptador | Un zip de joguina fet a mà, un `stored`, un `deflate` de debò, un nom llarg, una entrada xifrada i una de ZIP64 refusades amb la seva frase |
| **2** | **`lib/epub`, el tokenitzador** (A2) — **fet el 7/11 (0.128.0)** | Paraules amb el byte, blocs, entitats (inclosa `&eacute;` i el rang C1), `script`/`style`/`head` fora, `br`/`hr` (dos seguits), imatges amb els tres costats, `b`/`i`/`sup`/`sub` niats, `<pre>` i les quatre marques de taula: **12 casos** |
| **3** | **Les cares amb Latin-1 (D3, feta el 7/11) i el text amb èmfasi (D5, fet el 7/11)** | `à` es pinta (fet: 54 px contra 75 px en una línia de prova, i la tinta de l'accent per sobre de la lletra); una negreta és un píxel més ampla i la línia no s'allarga; una cursiva biaixa de la fila de dalt a la de baix; un `sup` puja un terç de línia |
| **4** | **El paginador** (A3) — **fet el 7/11 (0.130.0)** | Les línies amb l'avanç de cada paraula (una cara de regla al test), l'aire de cada bloc i la seva cara, la sagnia de cita, el peu centrat, la imatge flotada amb banda, la que no cabe que obre pàgina, la reixa d'una taula de dues columnes i la capçalera subratllada, el final de pàgina que diu el byte i una pàgina que continua **des d'aquell byte**: 12 casos |
| **5** | **El llibre** (A5) — **fet el 7/11 (0.131.0) menys la foto en cau** —, la imatge en un canvas (D4) i els grisos (A6) | Un `.epub` de joguina **escrit al disc del test** i obert pel camí de debò (fitxer → zip → capítol → tokenitzador → paginador amb la cara del llibre), un capítol inflat de 512 KB, una pàgina des d'un byte i la que comença en aquell byte: **4 casos** || **6** | **La porta de Lua** (B1–B8) | Les mateixes proves, però des d'un script: `run epub-probe` |
| **7** | **La pantalla sencera** (D1) | La vista passa a 540 × 960 i la sortida és el gest del sistema |
| **8** | **L'app sencera** (C1–C13) | El prestatge, un llibre obert, una pàgina girada, la posició desada i represa |

Cada fase és un commit amb la seva versió, les proves escrites i, si es poden executar, els números
mesurats. Les fases 1 a 6 no toquen l'app; la 8 és la que té el problema de mida (D6).

## 6 · Les decisions que són de l'amo

1. **La mida de les apps** (D6): el sostre de font puja a ~160 KB o les apps guanyen `require`. Sense
   això la fase 8 no cap.
2. **Com torna la pàgina** (D2): un objecte superfície o compondre dins d'un canvas de l'app.
3. **La pantalla sencera** (D1): si el lector ha de tenir les 960 files.
4. **Els accents** (D3): **decidit el 7/11 (0.127.0)**: la cara del llibre és llatina i les tres del sistema
   no s'han tocat, perquè canviar-les canvia el que veu tothom i no fa falta per al lector. Si l'amo les vol
   llatines, és una ordre d'eina per cada fitxer (`tools/ttf_latin1.py`).
5. **El cau de miniatures** (C11): amb `ui.image_save` o sense.

## 7 · El que no es porta, i per què

- **DRM** (Adobe, LCP): no és compressió, és un altre problema. Es refusa per nom, com al paper.
- **ZIP64**: un llibre que el necessita no cap en aquesta placa.
- **Els tipus de lletra incrustats de l'EPUB** (TTF/WOFF a dins del llibre): aquí una cara es carrega
  d'un fitxer del disc i n'hi ha tres; el paper tampoc no els fa servir.
- **L'EPUB de maquetació fixa** (`fixed-layout`): el paper no el llegeix i aquí tampoc no es farà
  sense una decisió.
- **SVG** com a imatge: no el llegeix cap dels dos sistemes.
- Els catorze forats que el paper ja té llistats al seu `PENDENT.md` (CSS, justificat, partició de
  paraules, text vertical…) **no són regressions**: es porten tal com són, i si es volen tancar, es
  tanquen després amb la seva pròpia llista.
