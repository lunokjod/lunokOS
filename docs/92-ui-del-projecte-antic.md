# 92 · La UI del projecte antic (una revisió de `OLD/PaperOS`)

`OLD/PaperOS` és el projecte anterior, arxivat i fora del git. Aquí no es compila ni s'executa:
el que s'ha llegit és el **codi i les capçaleres**, que és on són escrites les regles que van costar
mesos. Aquest document diu **què hi ha**, **què en té ja el sistema nou**, **què no s'ha de portar** i
**què val la pena**, perquè la decisió no es prengui dues vegades.

## 1. Què s'ha mirat

| Lloc | Què és | Mida |
|---|---|---|
| `OLD/PaperOS/src/ui/` | els controls i els contenidors | **65 fitxers, 23 037 línies** (14 623 `.cpp` + 8 414 `.h`) |
| `OLD/PaperOS/src/gfx/` | text (UTF-8, retall, salt de línia), formes, canvas de 4 bits | `text.cpp` 358, `shapes.cpp` 235, `canvas4.cpp` 187 |
| `OLD/PaperOS/src/system/lua_ui.cpp` | el pont de la UI cap a Lua | **4 115 línies**, 16 constructors, ~40 mètodes |
| `OLD/PaperOS/src/input/` | els esdeveniments: dit, gest, tecla | `TouchEvent`/`GestureEvent`/`KeyEvent` |
| `OLD/PaperOS/src/apps/` | les pantalles del sistema que els fan servir | 13 apps |

El vell té **trenta-un controls** i, a sobre, **set maneres diferents de fer una llista**
(`paged_list`, `icon_grid` que n'és filla, `list_box` que es pinta les files, `list_row` que és una
fila, `menu` que és una columna de botons, `menu_bar` que és una barra amb desplegables, `tab_strip`
que és una fila de botons, i `option_picker`/`selector` que són el mateix camp mirat de dues
maneres). Aquesta duplicació és el senyal més clar del que **no** s'ha de repetir.

## 2. Control per control, i què en té el sistema nou

El nou té **cinc** controls: `ui.canvas` (una vista pròpia), `ui.control` (un marc amb fills, sense
cap píxel propi), `ui.label`, `ui.button` i `ui.checkbox`, amb el focus, el `pressed` i el tàctil
portats pel sistema (`docs/30`). La resta no hi és.

| Control vell | Línies | Al nou sistema |
|---|---|---|
| `widget` (la base de tots) | 598 | **a mitges**: el `Canvas` i els camps `focused`/`pressed`/`focusable` del pont Lua fan la meitat de contracte |
| `theme` (colors, espais, cares) | 199 | **no hi és**: cada crida porta els seus colors i cada canvas la seva cara |
| `panel` (vertical, horitzontal, superposat; pesos i marges) | 806 | **no hi és** |
| `label` | 282 | `UILabel` ✓ |
| `button` | 650 | `UIButton` ✓ |
| `checkbox` | 369 | `UICheckbox` ✓ (3/11) |
| `progress` | 183 | no |
| `slider` | 567 | no |
| `sparkline` | 300 | no |
| `pie_chart` | 605 | no |
| `month_grid` (calendari) | 837 | no |
| `modal` + `dialog` | 725 | no |
| `option_picker` | 416 | no |
| `file_dialog` | 856 | no |
| `menu` | 631 | no |
| `menu_bar` (barra amb desplegables) | 1 294 | no |
| `tab_strip` (pestanyes) | 516 | no |
| `list_row` (una fila de dues columnes) | 1 199 | no |
| `list_box` (llista que no guarda text) | 603 | no |
| `paged_list` (pàgines, mai scroll) | 1 298 | no |
| `icon_grid` (graella d'icones) | 1 733 | no |
| `selector` (un camp que s'escull) | 519 | no |
| `page_dots` (els punts de pàgina) | 528 | no |
| `title_bar` | 1 876 | **parcial**: el nou ja pinta les 48 files de dalt (hora, títol, càrrega), i no és de cap app |
| `notification_bar` | 396 | no |
| `power_screen` (apagat, arrencada) | 1 106 | **parcial**: el nou té l'arrencada i els estats dins del nucli |
| `text_box` | 795 | no |
| `text_box_multiline` | 1 188 | no |
| `keyboard` | 957 | no |
| `game_pad` | 720 | no |
| `format` («12 KB», una data) | 190 | no |
| `bar_doors` (les tres zones de la barra) | 95 | no |

## 3. Les regles de tinta electrònica que el vell va aprendre

Aquestes nou són les que costen mesos i no es poden deduir mirant el codi del nou:

1. **Res que parpellegi no arriba al vidre.** `gc16` i `init` queden prohibits als controls i
   `withoutFlicker()` és l'única porta que els reconverteix (`src/core/update_mode.h:185`).
2. **Netejar és `gld16`, no `gc16`**: setze grisos, 450 ms, sense flaix, i el preu està escrit (0,6
   de fantasma per fila en comptes de 0) (`src/core/update_mode.h:118`).
3. **La família de forma d'ona es recorda per fila i és d'un sol sentit**: `clampFamily()` no
   serveix mai un patró a una fila plana, `recordFamily()` només ho escriu des d'una imatge i
   `splitOnFamily()` talla una banda de dany perquè una passada no sigui de dues famílies
   (`src/system/display_manager.cpp:1258`, `:1335`, `:1382`). **Això és el que va curar el
   «PARPALLEJA CADA 2 PER 3» i no cap mode de pintat.**
4. **Tot el que es pot tocar és binari** (negre i blanc, sense grisos), i l'element seleccionat
   s'**inverteix**, no s'engriseix: un gris obliga a la forma d'ona cara (`src/ui/theme.h:19-52`).
5. **Cada control declara la qualitat que vol** (`paintKind`, `paintMode`, `paintLatency`), i el
   sistema la pot sobrepassar però no rebaixar. Els dos casos que ho ensenyen són la graella
   d'icones (icones de setze grisos → `TextContent`, `icon_grid.h:280`) i els punts de pàgina
   (demanen `du` pel nom perquè volen l'esborrat net, `page_dots.h:190`).
6. **Es declara el dany, no es repinta**: només la cel·la del cursor parpelleja
   (`text_box.cpp:308`), una selecció de calendari toca **dues cel·les** (`month_grid.cpp:390`), la
   barra de dalt envia **una** de les tres bandes de tinta (`title_bar.cpp:757`), i un fill que s'ha
   mogut és un rectangle que acumula el contenidor (`panel.cpp:221`).
7. **Un control que no es dibuixa no té píxels i no pot marcar dany**, i amagar-se és un canvi de
   vidre que només pot declarar el que s'amaga (`widget.cpp:45`).
8. **La latència és un segon eix**: una premuda i una tecla escrita són `Immediate`, un canvi de
   pàgina pot ser `Relaxed` (`button.cpp:172`, `text_box.cpp:343`).
9. **A la tinta no es fa scroll** (es fan pàgines) i **no es segueix el dit**: 260 ms no pot anar
   darrere d'un dit, i per això el gest és un canvi de pàgina (`paged_list.h:1-40`).

## 4. Dotze paranys que ja estan pagats

Tots són errors reals, amb la línia on van quedar resolts:

| # | El parany | On |
|---|---|---|
| 1 | **La geometria es calcula una vegada i serveix per pintar, per al dit i per saber què és decoració**; dues computacions són un botó invisible | `title_bar.h:150-200`, `keyboard.cpp:389` |
| 2 | **El `Up` només compta si el dit encara és a dins i no s'ha mogut més del llindar**, o un gest de passar pàgina prem el que hi ha sota | `button.cpp:325` |
| 3 | **Tres senyals que no es poden barrejar**: focus (invertit), dit (`pressed`), premsa llarga (desfer) | `button.cpp:378` |
| 4 | **Files per pàgina acumulant altures reals**, no `alçada / alçada_de_fila`: la divisió deixa el focus en una fila que no es dibuixa | `paged_list.cpp:282`, `:795` |
| 5 | **El text que no hi cap es retalla amb punts i en un codi sencer**, no a un byte: un tros de UTF-8 és una lletra inventada | `gfx/text.cpp:315` |
| 6 | **Un camp de text és una finestra que deixa caure els primers caràcters**, i el cursor es mesura amb **la mateixa** funció que el dibuixa | `text_box.cpp:248`, `:272` |
| 7 | **Un text copiat, no un punter**: el `snprintf` del costat reutilitzava l'adreça i l'etiqueta no s'invalidava mai («només surt després d'un clean») | `label.cpp:48` |
| 8 | **Altures que depenen de l'amplada** (`heightForWidth`), i el contenidor que reparteix espai ha de saber què és rígid i què és elàstic o les files se sobreposen | `list_row.cpp:481`, `panel.cpp:248` |
| 9 | **El focus viu es valida a cada tecla**: un control amagat o desactivat deixa anar el focus, o `centre` activa l'invisible | `panel.cpp:587` |
| 10 | **L'ordre és llegir l'acció → tancar → executar**: així una acció pot reiniciar la placa sense el diàleg a sobre | `modal.h:44` |
| 11 | **`setIndex` no pot cridar el `on_change`**: el codi que mou un valor no és un dit, i per això hi ha dues portes | `tab_strip.h:44` |
| 12 | **El que un control pinti i el que el dit trobi han de sortir de la mateixa taula de files** (teclats de files ponderades) | `keyboard.cpp:389`, `:419` |

## 5. Què **no** s'ha de portar

- **El model de memòria.** El vell dimensiona a mà: `Panel::MaxChildren = 24`, `ListBox::MaxItems = 8`,
  `PieChart::MaxSlices = 4`, `Menu::MaxItems = 6`, i un `LuaWidgets` amb un sostre mesurat a la
  capçalera (`lua_ui.h:320`: «14 880 B de 14 900»). És una solució per a un firmware que és l'únic
  que dibuixa. Al nou, una app és Lua i el control que no té vista costa **0 píxels**: el sostre ha
  de ser el pressupost de l'app (`docs/75`), no una taula de capacitats.
- **Els 4 115 línies de `lua_ui.cpp` i els 16 constructors.** El pont del nou és una taula de mètodes
  per objecte i prou; un registre de «tipus de control» amb un índex numèric és el que el vell va
  haver de fer per no pagar punters.
- **Les set llistes.** Si se'n porta una, se'n porta **una**: pàgines amb altures acumulades, files
  pintades per una sola funció, i el concepte de «graella» com un cas de la mateixa cosa.
- **Els comentaris en català** dins del codi: el codi d'aquest repositori va en anglès (llei del
  `README`). El vell n'és ple; el que s'aprofiti s'ha de traduir en el moment de copiar-lo.

## 6. Què val la pena, per ordre

Ordenat pel que costaria retrobar-ho: el primer és el que més car surt de descobrir sol.

| # | Què | On és | Per què |
|---|---|---|---|
| 1 | **Un contenidor amb layout i focus** (flux, pesos, marges, layout brut cap amunt, focus validat, `revealFocused`) | `panel.cpp:221`, `:433`, `:587` | és la diferència entre «l'ordre de les crides importa» i «una pàgina és una declaració»; avui el nou no en té cap |
| 2 | **El dany dels que desapareixen** | `widget.cpp:45` | el nou té `pending_release` per la sortida d'una app, no per amagar un control |
| 3 | **La memòria de família de forma d'ona per fila** | `display_manager.cpp:1258`, `:1335` | invisible a les proves, només mesurable a la placa, i és el defecte que més vegades es va reportar |
| 4 | **Pàgines amb altures acumulades i `revealFocused`** | `paged_list.cpp:282`, `:795` | l'error d'un és un focus en una fila que ningú dibuixa |
| 5 | **Un sol pintor de files amb retall en punts** | `list_row.cpp:531`, `gfx/text.cpp:315` | dos pintors de la mateixa llista sempre divergeixen |
| 6 | **Els tres senyals del botó i el llindar del dit** | `button.cpp:325`, `:172` | tota implementació ingènua fa que un gest premi el que hi ha sota |
| 7 | **«Binari en tots els estats» com a contracte de dibuix** | `theme.h:19-52`, `checkbox.h:1-20` | és el que permet que el retorn sigui la forma d'ona barata |
| 8 | **La finestra del camp de text i el cursor mesurat igual** | `text_box.cpp:248`, `:272`, `:308` | un cursor que no coincideix amb la lletra és una app trencada |
| 9 | **El dany mínim d'una selecció pas a pas** | `month_grid.cpp:390` | l'exemple més clar de «dues cel·les, no la pantalla» |
| 10 | **El teclat que troba les tecles cridant la funció que les pinta** | `keyboard.cpp:389` | les graelles ponderades són exactament on una divisió deixa una tecla morta |

## 7. La decisió que això demana

El nou **no té `require`** (docs/20): una app és un sol fitxer, i dues apps que vulguin el mateix
codi el dupliquen. Per tant **un joc de controls compartit no pot viure en Lua**: o és al nucli
(C++, com el vell) o no existeix. I el nucli té un pressupost de flaix i de RAM per a totes les
plaques (`docs/75`), no per app.

D'aquí surt la regla que es proposa, i que cal decidir:

- **al nucli** hi va el que **tothom** necessita i no es pot duplicar bé: el contenidor amb layout i
  focus (§6.1), el pintor de files amb retall (§6.5), i les regles de dany i de forma d'ona (§6.2,
  §6.3). Són les peces que una app no pot escriure's sola amb correcció.
- **a l'app** hi va el que és el seu dibuix: un calendari, un gràfic, un teclat de joc, un diàleg.
  Amb `ui.control` i `ui.canvas`, una app ja ho pot fer, i el vell demostra que es fa (l'`Agenda`
  era un script sobre `month_grid`).
- **no s'hi posa** cap col·lecció de controls «perquè hi eren»: cada control del nucli es paga a
  cada placa i a cada build, i el vell va trigar mesos a buidar-ne els que no feien res.

## 8. Les fonts, i per què no es poden copiar

Les cares del vell són **de mapa de bits, 1 bit per píxel, proporcionals** (un `GlyphInfo` per codi amb el
seu avanç) i porten **216 glifs** —ASCII, Latin-1 i els accents que falten—, generades amb
`OLD/PaperOS/tools/fontgen.py` a partir de DejaVu:

| cara | fitxer | cos | alçada de línia | bits |
|---|---|---|---|---|
| `ui16` | `DejaVuSans.ttf` | 15 px | 18 px | — |
| `ui24b` | `DejaVuSans-Bold.ttf` | 24 px | 29 px | — |
| `ui28b` | `DejaVuSans-Bold.ttf` | 28 px | 33 px | — |
| `ui36b` | `DejaVuSans-Bold.ttf` | 36 px | 43 px | **15 878 B** |
| `ui48b` | `DejaVuSans-Bold.ttf` | 48 px | 57 px | — |
| `ui70b` | `DejaVuSans-Bold.ttf` | 70 px | 82 px | — |

**No es poden copiar tal com són**, i els números són el motiu:

- Una `.lfont` d'aquest sistema és **de pas fix**, amb una cel·la de com a molt **16 × 16** i **96 codis**
  (`0x20`–`0x7F`, `lib/kernel/include/kernel/font.h:44-52`). Una cara de 36 px demana una cel·la d'unes
  25 × 43 i porta 216 codis: no hi cap ni de lluny, i el lector la refusaria.
- Una `.lfont` és **text** (`#` i `.`): a 36 px serien unes 184 000 lletres al disc, contra els
  **15 878 B** que ocupa el mateix cos en una taula binària dins del firmware.

**El camí que sí que hi ha és el `.ttf`**, i és el mateix tipus de lletra: el lector de contorns del nucli
és **proporcional com les cares velles** (avanç per glif, no cel·la fixa), dibuixa a qualsevol mida
(`scale` és el cos en vuitens: **3 = 24 px, 4 = 32, 5 = 40, 6 = 48, 9 = 72**), i el DejaVu Sans Bold
retallat a l'ASCII imprimible eren **8 548 B** —avui **9 416 B**, que hi porten els signes d'una tecla i les
fletxes (§34)—, la meitat dels 15 878 B d'un sol cos vell, i serveix per a
tots—. Per això la integració ha estat portar les cares de DejaVu a `/fonts` com a una sola còpia per a
totes les apps, i no traduir `font_*.h` (3/11, 0.68.0; docs/30, docs/70).

El que **sí** que val la pena heretar del vell és la regla de mida, no el número: la cara del text principal
es tria amb el càlcul de «quants caràcters hi caben» i no pel gust. Al vell eren **20 caràcters dins dels
516 px útils** i el resultat va ser 43 px d'alçada de línia (`OLD/PaperOS/src/gfx/fonts/fonts.h:23-53`).
Aquí el mateix càlcul es fa amb `font:text_width` sobre la cara de `/fonts` a la mida que es vulgui provar, i
no hi ha cap mida fixada al nucli: **la tria l'app**. `tools/ttf_to_lfont.py` segueix sent l'eina per a una
cara de mapa de bits de 16 px o menys que una app vulgui portar-se a dins.

## 9. Què no està mesurat

- El vell **no es compila** aquí: no hi ha build ni proves seves, i tot el que diu aquest document
  surt de llegir codi i capçaleres, amb els números de línia citats.
- Del nou no s'ha executat res per fer aquesta revisió: ni `pio run`, ni les proves.

## 10. Com adaptar els controls

**La regla la va fixar l'amo el 3/11 i mana sobre tot el que segueix**: *«tot control és dibuixat pel
sistema; les apps Lua tenen o els controls UI o el canvas amb primitives. Cap control UI es dibuixa a
Lua.»* O sigui que una app té **dues eines i prou**: els controls del nucli (que els pinta el nucli) i el
seu canvas amb les primitives, per al que és **contingut** seu —un joc, un dibuix, una imatge, un gràfic
fet a mida—. El que és un control —una fila, un menú, un diàleg, un calendari— va al nucli.

D'aquí surten tres regles per decidir què es porta i en quin ordre:

1. **Al nucli hi va tot control**, i dins del nucli primer el que modela entrada (un llindar de dit, una
   tecla que entra i surt d'un submode, un focus que s'ha de validar) i el que decideix **quin dany** es
   declara. L'error d'aquestes dues coses no es veu quan passa: es veu un fantasma tres pantalles més
   enllà (docs/92 §3, §4).
2. **A l'app hi va el seu contingut, i amb primitives**: un joc, un dibuix, una imatge, una gràfica feta a
   mida sobre `ui.canvas`. El que una app **no fa mai** és dibuixar un control: si el vol, el demana al
   nucli (docs/30).
3. **No s'hi posa res «perquè hi era»**: cada control del nucli es paga a cada placa i a cada build, i el
   vell va trigar mesos a buidar-ne els que no feien res.

Traducció, control per control, amb el que canvia de veritat:

| Del vell | Al nou | Què canvia |
|---|---|---|
| `Panel` (flux, pesos, marges, focus, `revealFocused`) | **`ui.panel(w, h, 'vertical'\|'horizontal'\|'overlay')`** al nucli, amb `add`, `padding`, `gap`, `weight` i el focus recorrent els fills en ordre | el lloc el calcula el panel i no l'app; `revealFocused` només té sentit quan hi hagi pàgines |
| `Button`, `Checkbox`, `Label` | **ja hi són** (`ui.button`, `ui.checkbox`, `ui.label`) | el `paintKind`/`paintLatency` del vell és aquí el `mode` de cada dibuix; `focused`/`pressed` els porta el sistema i no el control |
| `Slider` | **fet** (0.69.0, `ui.slider`): les fletxes mouen el número mentre té el focus i al final del recorregut la tecla torna al dial | el vell va haver d'afegir el submode perquè una pàgina amb un slider era un cul-de-sac (`slider.h:28`); aquí el número és del control i el sistema el mou |
| `PagedList` + `ListRow` (`paintListRow`) | **la fila ja hi és** (0.69.0, `ui.row`: títol, detall a la dreta, subtítol, separador i marcat invertit); **falta la llista**: pàgines amb altures acumulades i fletxes | el vell en tenia set de llistes; aquí n'hi ha d'haver una |
| `TextBox`, `TextBoxMultiline`, `Keyboard` | **al nucli**: el camp, el cursor i el teclat són controls i es dibuixen al sistema, com tots els altres | el vell els tenia al nucli perquè el punter d'un camp no cabia al pressupost; `edit` i `hexedit` són apps que llegeixen el teclat de la consola mentrestant |
| `Menu`, `MenuBar`, `TabStrip`, `PageDots` | **al nucli**, com a controls: són dibuix i ordre de focus, i les dues coses són del sistema | cap és una cosa que una app hagi de pintar |
| `Dialog`, `OptionPicker`, `FileDialog` | **al nucli**, amb la regla del vell: llegir l'acció → tancar → executar (`modal.h:44`) | són finestres sobre la pàgina i la pàgina és del sistema |
| `Progress` | **fet** (0.69.0, `ui.progress`): pista i el tros fet, amb −1 = «encara no ho sé» | `Sparkline`, `PieChart` i `MonthGrid` segueixen el mateix camí, al nucli |
| `TitleBar`, `NotificationBar`, `PowerScreen` | **al nucli, i ja hi és la meitat**: la barra de 48 files és del sistema | el que falta és el que el vell hi va aprendre: `bandOf` (només la banda que canvia) i les zones de toc calculades una vegada |
| regles de forma d'ona (§3) | **al nucli**: la memòria de família per fila i el dany mínim | és el que no es pot fer des d'una app |

**L'ordre, i per què.** Primer el `panel`, perquè sense ell cada app col·loca fills a mà i el que
s'aprengui després s'ha de refer; després el pintor de fila i la llista, que és el que més es repeteix;
després el `slider`; i al final el cursor del text. Les regles de dany i de forma d'ona (§3) van **abans
que res d'això**, perquè són les que diuen si un control nou pot declarar el seu dany o l'ha de demanar.

**Les tres decisions que cal prendre abans d'escriure codi**, i la que prendria jo:

1. **Qui té el layout**: el `panel` del nucli (proposat) o una col·lecció de regles a cada app. → Al nucli,
   perquè la regla 1 de dalt.
2. **Si el focus del sistema ha de baixar dins d'un panel** o si un panel és una parada més del recorregut.
   → Que hi baixi: el vell ho tenia així (`collectFocusable`) i una pàgina amb dues columnes sense això
   és un recorregut que salta files.
3. **Qui diu la qualitat de pintat**: la crida de dibuix (com avui) o el control. → La crida, i el control
   només la tria en un sol lloc; el vell va haver de declarar-la per control perquè el sistema decidia
   l'ordre de les passades, i aquí `present` ja ajunta el que és de la mateixa qualitat.

### Què hi ha ja, amb la regla de l'amo

| Fet | Versió | Prova que mesura la conseqüència |
|---|---|---|
| `ui.label`, `ui.button`, `ui.checkbox` | 0.5x–0.67 | `test_ui`, `test_apps` |
| **`ui.progress`** — pista i tros fet, −1 = «encara no ho sé» | **0.69.0** | `test_a_progress_bar_fills_a_prefix_of_its_track` |
| **`ui.slider`** — el número que mouen les fletxes, amb `on_change` | **0.69.0** | `test_a_slider_puts_the_knob_where_the_value_says`, `test_the_dial_moves_the_value_of_a_slider` |
| **`ui.row`** — títol, detall a la dreta, subtítol, separador, marcat invertit | **0.69.0** | `test_a_row_puts_its_detail_on_the_right_and_inverts_when_it_is_marked` |
| **`ui.panel`** — columna, fila o superposat, amb `padding`, `gap` i `weight` per fill | **0.70.0** | `test_a_panel_stacks_its_children_in_a_column`, `test_a_row_panel_splits_the_width_and_stretches_the_height`, `test_a_panel_arranges_the_page_and_the_dial_changes_the_tab` |
| **`ui.tabs`** — columnes iguals i la triada sense línia a sota | **0.70.0** | `test_a_strip_of_tabs_has_no_line_under_the_chosen_one` |
| **`ui.dots`** — la pàgina on ets, i res si només n'hi ha una | **0.70.0** | `test_the_dots_of_a_page_draw_nothing_when_there_is_one_page` |
| **`ui.list`** — files en pàgines, el peu `2 / 4` i `on_page` amb el dial | **0.71.0** | `test_a_list_measures_its_pages_with_one_arithmetic`, `test_a_list_draws_one_page_and_says_which_one_it_is`, `test_a_list_turns_its_page_with_the_dial` |
| **`ui.textbox`** — la finestra del text i el cursor de bloc amb la lletra en paper | **0.72.0** | `test_a_textbox_keeps_the_caret_on_the_glass` |
| **el teclat del sistema** — banda pròpia, dial que camina les tecles, i el camp escrit abans de `on_change` | **0.72.0** | `test_the_keyboard_types_into_the_box_it_was_given` |
| **el teclat, dibuixat** (0.83.0) — QWERTY esglaonat de minúscules amb majúscules, barra d'espai, icones (← ↵ ⇧) en lloc de paraules, cap botó, quatre files en els dos jocs i la banda neta amb `gl16` | **0.83.0** | `test_the_keyboard_is_a_qwerty_the_finger_and_the_glass_agree_on` |
| **el teclat que no parpelleja** (0.83.1) — es pinta al llenç a cada present i al vidre només hi va el que ha canviat: dues cel·les quan el dial camina, la banda quan canvia el joc, res quan s'escriu una lletra | **0.83.1** | el mateix, amb el comptador de pintades del vidre |
| **la vista que el teclat no trepitja** (0.84.0) — `ui.caps().height` baixa a 656 amb la banda (736 fins a 0.106.0, amb tecles de 44) i torna a 912 quan marxa, l'app rep `on_resize`, i `cv:resize` deixa un panel prendre la mida nova | **0.84.0** | `test_the_keyboard_takes_its_band_out_of_the_view_the_app_is_given` |
| **la caixa de text, neta** (0.84.1) — el text de la finestra s'acumulava entre pintades (un buffer estàtic que no s'esborrava), el cursor a zero es pintava al final de la fila, i les apps presenten el canvi amb `gl16` | **0.84.1** | `test_a_box_paints_its_text_and_not_the_one_before_it`, `test_the_caret_of_a_box_of_lines_is_at_the_cursor_and_not_at_the_end_of_the_row` |
| **el teclat, dibuixat de debò** (0.85.0) — DejaVu Sans Bold de `/fonts` a doble mida i rebaixat a quatre nivells de tinta, `MAY` com a bloqueig, i la banda que només es repinta quan canvia ella o quan la pàgina la trepitja | **0.85.0** | `test_the_keyboard_is_a_qwerty_the_finger_and_the_glass_agree_on` |
| **el dial de la finestra, dibuixat** (0.85.1) — fletxa amunt, cercle farcit i fletxa avall com a runs d'un píxel (`gui_key_icon`) en lloc de les paraules, botons de 40 × 40 amb 6 de buit, i una placa d'una tecla que es queda la paraula | **0.85.1** | `test_the_keys_of_the_window_are_the_dial_of_the_board` |
| **el cursor on toca el dit** (0.85.2) — la fila pel `y` i la lletra pel `x`, una fila buida és una fila, una fila que no hi és és el final del text, i el sistema presenta amb `gld16` el cursor que ha mogut | **0.85.2** | `test_a_box_of_lines_keeps_the_caret_on_the_glass` |
| **la barra, en tres caixes** (0.85.2) — l'hora, el títol i la càrrega, cadascuna neta i es dibuixa sola amb `gld16`, sense el `gc16` de tota la franja per un títol | **0.85.2** | `test_a_new_title_repaints_the_bar`, `test_a_new_minute_repaints_the_hour_and_not_the_strip` |
| **`ui.dialog`** — targeta, missatge que s'ajusta, dues portes i el dit que tria la que toca | **0.73.0** | `test_a_dialog_wraps_its_message_and_has_two_doors`, `test_a_dialog_answers_with_the_door_the_dial_is_on` |
| **`ui.sparkline`** — una barra per mostra, escala del pic de la finestra i la segona sèrie buida | **0.74.0** | `test_a_sparkline_scales_to_the_peak_of_its_window` |
| **`ui.pie`** — quatre porcions, angles d'una taula de direccions i tons que camina el control | **0.74.0** | `test_a_pie_is_two_halves_when_the_slices_are_equal` |
| **`ui.month`** — set columnes, sis setmanes sempre, i la regla dels traspasos una sola vegada al nucli | **0.75.0** | `test_a_calendar_puts_the_first_day_where_the_weekday_says`, `test/test_calendar` |
| **`ui.menu`** — una opció per caixa, cap de menys de 52 px, l'amagada no ocupa lloc i el dial la salta | **0.76.0** | `test_a_menu_hides_the_options_the_app_cannot_do`, `test_a_menu_skips_the_option_the_app_hid` |
| **`ui.menu_bar`** — una fila de paraules i el menú que penja de la desplegada; la ratlla no és una fila i l'entrada en el to `dim` no respon | **0.77.0** | `test_a_bar_of_menus_hangs_the_menu_under_its_word`, `test_a_menu_bar_answers_with_the_title_and_the_entry` |
| **`ui.textbox_multiline`** — la finestra que segueix el cursor, `wrap` com a manera de pintar, i el retorn que escriu un salt | **0.78.0** | `test_a_box_of_lines_keeps_the_caret_on_the_glass`, `test_a_box_of_lines_types_and_the_enter_writes_a_break` |
| **`ui.selector`** — la pregunta en el to `dim` i la resposta en tinta, la porta és la columna de la fletxa i la llista la posa l'app | **0.79.0** | `test_a_selector_is_a_field_with_a_door_on_its_right`, `test_a_selector_opens_the_list_the_app_puts_up` |
| **`ui.notification_bar`** — targetes d'una punta a l'altra, la més nova a baix, el `+N` dels que esperen i la creu que descarta | **0.80.0** | `test_a_band_of_notifications_puts_the_newest_at_the_bottom`, `test_the_cross_of_a_notice_dismisses_the_newest` |
| **`ui.icon_grid`** — una icona amb el nom a sota, la pàgina segueix el cursor i el nom no s'inverteix amb la cel·la | **0.81.0** | `test_a_grid_puts_the_page_where_the_cursor_is`, `test_a_grid_walks_its_cells_and_pages_itself` |
| pàgines d'exemple amb ells | **0.69.0–0.74.0** | `run controls`, `run typing`, `run dialog`, `run trend` |

**Els controls del vell estan portats tots.** L'últim que va quedar és `icon_grid` (0.81.0). El que **no** serà un control
és `format`: és una taula de textos i números per a la consola i l'app ja té `string.format` a Lua. I la `option_picker` del
vell —la targeta que ensenyava moltes opcions en columnes— és una feina que ja fan **`ui.menu`** (dotze opcions en fins a
quatre columnes) i **`ui.list`** (files paginades sense límit), i no en cal cap de nou. Queden a mitges, i no són d'aquest
fil: `title_bar` (zones de toc i `bandOf`), `power_screen` (la pantalla d'apagada) i `paged_list` (un contenidor amb
pàgines per a fills que no són files de dades; el paginat de dades ja és dins d'`ui.list`).


### El **dibuix**, control per control

**Tenir el control no és tenir-ne el dibuix**, i això és el que va fer aturar l'amo el 5/11 amb la placa a la mà:
*«cap control s'assembla, ni s'acosten… VULL UNA CÒPIA EXACTA DELS BOTONS, EL TECLAT, EL MENU CONTEXTUAL AMB LA
MATEIXA LLETRA I TOT IGUAL, VULL QUE NO INVENTIS, MIRA EL CODI DELS CONTROLS ORIGINALS UN PER UN I ELS ADAPTES UN
PER UN»*. La taula de sota és aquesta feina, i cada fila és una lectura del codi del vell i el seu dibuix adaptat.

| Control | Estat | Números que s'han portat |
|---|---|---|
| **`ui.button`** | **portat** (0.97.0) | caixa del color de l'ombra + cara aixecada **4 px** del fons (mai més d'un terç de l'altçada), això que queda a la vista a sota és l'ombra; radi **14** (el 10 del tema més 4) amb el sostre d'un terç del costat petit; **sense bisell**; el triat va **invertit** (cara en tinta, paraula en paper) perquè el focus i el dit són el mateix senyal; el marc va **a la cara i no a la caixa**, d'1 px i de **3** al primari, i només quan el botó no és triat; `padX` **16**, etiqueta amb radi 4, `padX` 6, `padY` 2, buit 8 i la paraula centrada al que queda a la dreta |
| **el teclat** | **portat** (0.97.0) | els marges de la graella: **12 px** a l'esquerra, a la dreta i a baix; una tecla que no és la triada és **un píxel de tinta a la seva vora esquerra** i no una caixa; la triada és **la cel·la sencera en tinta**; les files, separades per una línia tan ampla com les tecles; **2 px** de tinta a dalt de la banda; la barra d'espai diu la seva paraula |
| **`ui.menu`** | **portat** (0.97.0) | la targeta del vell: paper amb **tres anells** d'1 px, **15** de marge interior, **10** entre el títol i les opcions, i **cada opció és el botó** de dalt (la del dial invertida, la primària amb el marc de tres) |
| **`ui.menu_bar`** | **portat** (0.97.0) | el desplegat és la mateixa targeta de tres anells i l'entrada del dial va invertida (fila sencera en tinta, paraula en paper) |
| **`ui.row`** | pendent | |
| **`ui.list`** | pendent | |
| **`ui.tabs`** | **portat** (0.97.0) | la tab on ets és un **punt de llibre**: 2 px a l'esquerra, a dalt i a la dreta i **no a baix**, i les altres porten la ratlla a sota; res de subratllat |
| **`ui.checkbox`** | **portat** (0.97.0) i **simplificat** (0.102.1) | **un quadrat** de marc de **2 px** amb els cantons rectes i, quan està marcat, **una X de dues línies de 2 px**; res a dins quan no ho està. El control amb el focus s'inverteix. Abans era el del paper project (radi 5 i un tick en V) i l'amo va demanar la versió esquemàtica: *«un quadrat i una X dins si està activat o res si no ho està»* |
| **`ui.slider`** | **portat** (0.97.0) | el tub és paper amb vora d'1 px, el tros fet és **tinta**, i la nansa és un **cercle de l'altçada del control** (tinta amb un punt de paper quan té el focus); sense marc al voltant |
| **`ui.progress`** | **portat** (0.97.0) | el tros fet porta un **anell de tinta** a sobre del to clar, que és el que diu on ha arribat la barra |
| **`ui.textbox`**, **`ui.textbox_multiline`** | pendent | |
| **`ui.dialog`** | **portat** (0.97.0) | la targeta de tres anells i **les dues portes són botons** (la del dial invertida, l'èmfasi amb el marc de tres) |
| **`ui.selector`** | pendent | |
| **`ui.notification_bar`** | pendent | |
| **`ui.icon_grid`** | pendent | |
| **`ui.month`**, **`ui.sparkline`**, **`ui.pie`** | pendent | |
| **les mides de la lletra** | **portat** (0.97.2) | un pas de `scale` és **9 px de cos** i **11 px de línia** (`kOutlineBody`/`kOutlineLine`): `scale` 4 = **36 px de cos i 44 de línia**, que és el `ui36b` del vell (36/43); `scale` 3 = 27, el seu text dens és 24; `scale` 5 = 45, el seu títol és 48. I la **barra** a tres passos: línia de **33 px**, el seu `fontSmall` |
| **el tema** (radi 10, farciments 6/12/18, buit 10) | pendent | |
| **la barra** | pendent | la línia és de 33 px al vell; aquí en fa 32 dins de 48 files |

**I dues coses que no són cap control i van amb això**: la **lletra** ja és la del vell des de 0.96.0
(`/fonts/dejavusans-bold.ttf`), i el que falta és la **mida**: el pas d'una cara d'outlines és de 8 px de cos i el
vell escriu amb una línia de 43 px, que és 32 px de cos. Fins que el tema dels números no hi sigui, un botó portat
té la forma exacta i la lletra més petita que l'original.



## 11. L'aparença per defecte, i les apps que en surten amb un motiu

**La regla de l'amo (7/11):** *«vull que tot el sistema usi el mateix tipus de lletra i estètica dels controls.
Les apps han de seguir totes l'estètica del sistema, això s'aconsegueix amb els valors per defecte de font,
tamany i colors. Si una app decideix canviar-ho ha de tenir un motiu.»* O sigui que **el pont de Lua és on viu
l'aparença**: un control que una app no configura ha de sortir com tota la resta del vidre.

**Els valors per defecte, control per control** (`lib/luabind/src/luabind.cpp`, 0.97.3):

| Control | `scale` per defecte | Per què |
|---|---|---|
| `label`, `button`, `slider`, `row`, `list`, `month`, `dialog`, `menu`, `textbox`, `textbox_multiline`, `selector`, `notification_bar`, `icon_grid` | **4** | un cos de **36 px** i una línia de 44, que és el text del paper project (el seu `ui36b`) |
| `tabs`, `menu_bar` | **3** | una tira de paraules, que és la mida densa del paper project (el seu `fontSmall`, línia de 33) |

I els colors, els del tema: `background` **15**, `border`/`color`/`text`/`mark` **0**, `dim` **8** i `fill`/`track` del
que diu el dibuix de cada control (el to clar **10** al tros fet d'una barra i paper a la pista del slider).

**Què s'ha tret de les apps (12 fitxers, 27 línies):** totes les línies que només repetien el defecte —
`.scale = 2` (vint-i-una), `.scale = 3` (cinc), `.scale = 1` (una) i els colors iguals al tema— de `checkbox`,
`controls`, `dialog`, `editor`, `form`, `grid`, `images`, `menu`, `menubar`, `notes`, `trend` i `typing`. I les alçades
de les seves etiquetes, que estaven fetes per a un text de la meitat: passen a **46** (una línia del sistema), la fila
de la llista de `controls` a **68** (44 de línia i 12 d'aire) i l'estat i la nota de l'editor a **46** cadascun.

**Les tres apps que sí que diuen una mida, i per què:**

| App | Què diu | Motiu |
|---|---|---|
| `showcase` | `body_scale = 4`, `small_scale = 3`, `title_scale = 5` | **és el catàleg de les tres mides**: la seva feina és ensenyar-les |
| `buttons` | `spec.scale` per botó | **és la pàgina dels botons de cada mida**, i el títol d'un botó gran va a 3 |
| `label` | `spec.scale or 1` | **és la pàgina de les etiquetes de cada mida** |

I el to `8` d'una paraula secundària (`answer.color = 8` a `dialog`, `menu`, `form`, `grid`, `menubar`, `typing`) **no
és una excepció**: és el `dim` del tema, que és l'únic to que un color de text pot dir quan la paraula no és la que
compta.

**Com es comprova que continua sent cert**: `run` de cada app al banc i una imatge amb `--dump` (docs/85) mirada al
costat de les captures del vell. Les dotze apps de dalt s'han corregut així, i les que tenien un botó o una llista amb
la mida vella s'han arreglat mirant-les: el botó del diàleg, que deia «Delete e…», i les files de `controls`, que es
trepitjaven.

## 12. El cercle i la pulsació els pinta el sistema (0.98.0)

**La volta del dial és del sistema i el seu dibuix també.** `ui_button_event` es queda la tecla abans que l'app en
sàpiga res: mou el cercle d'un control a un altre, o baixa el botó que toca, i ho fa a l'arbre de controls, que és
una cosa que el vidre no veu fins que algú demana un `ui.present`. Una app que espera dins d'un `sys.delay` no en
demana cap, i el resultat amb la placa a la mà és *«l'app Showcase no respon a l'usuari»*.

**El que estava mesurat abans de tocar res** (banc, `use_board("m5stack-paper")`, `run showcase 1` i sis tecles
seguides): la volta **sí que hi era** —`count 3` a la pàgina, el cercle anava de la tira al botó i al quadre de
marcar, i el centre arribava a `ui_activate`— i **no es pintava ni un píxel**, perquè el present de la pàgina feia
temps que havia passat. Tres intents abans d'encertar-lo, i tots tres són la mateixa lliçó: `present` sol no dibuixa
els controls que deuen un dibuix (ho fa el `draw` de l'arbre, que és el que `ui.present` crida abans), i la guarda
que vaig inventar amb `damage()` era buida perquè el dany es compta dins del present i no abans.

**El que es fa ara**, al mateix camí que l'app:

| Moment | Què es pinta |
|---|---|
| el dial es mou (`ui_button_event`, la volta) | el control que deixa el cercle i el que el pren |
| `ui.focus(control)` | el cercle, sense esperar el present de la pàgina |
| el centre prem (`ui_activate`) | el botó baixat, i el botó pujat quan el callback torna |

**I el que no és del sistema**: què *fa* la pulsació. Un control sense `on_click` es prem i no di res, i això és una
app muda. L'app de mostra dona un `on_click` a cada control que posa al dial i escriu a la línia de baix què s'ha
premut; les dues proves noves ho mesuren (el vidre es mou quan el dial es mou, i el centre arriba a l'`on_click`), i
les dues es posen vermelles quan se'ls treu la línia que proven.

## 13. Una parada per pestanya (0.99.0)

**La tira de pestanyes era una sola parada del dial**, i això la feia una trampa: les fletxes li arribaven i les
consumien canviant la pestanya, la pàgina de sota es tornava a construir, i la volta tornava a començar a la tira. El
7/11, amb la placa a la mà, l'amo ho va dir així: *«no puc pitjar res, i els botons físics només em mouen per les
pestanyes»*.

**El projecte del paper no ho fa així.** `tab_strip.h` ho diu a la capçalera: *«The tabs ARE `Button`s and the strip
hands them to the navigator as focusable: the arrows walk into the strip and from one tab to the next, and the centre
key chooses the one that has the focus. The strip itself never takes a key.»* I `TabStrip::draw()` pinta la pestanya
que respondrà **invertida** (`lit = enabled() && (focused() || isPressed())`), amb la cara de tinta, la paraula de
paper i el marc i la línia girats.

**Què s'ha fet, un per un:**

| Peça | Què fa ara |
|---|---|
| el recorregut (`ui_walk_targets`) | una tira de `n` pestanyes dona **`n` parades**, una per columna, amb el mateix control i un `item` que diu quina és |
| el focus (`ui_apply_focus`) | escriu `focus_index` al control i `g_ui_focus_item` a la volta; el control que pren el dial per si sol posa l'item a zero |
| les fletxes (`ui_step_value`) | la tira **ja no hi és**: no s'empassa cap tecla i la volta continua endins o enfora |
| el centre (`ui_activate`) | posa `index` a la pestanya del cercle i l'app ho sent a `on_change(self, index)` |
| el dibuix (`tabs_draw`) | la pestanya de `lit` va invertida; la resta, com sempre |
| el dit | el tacte també desa quina pestanya ha tocat, o un toc a la tira hauria triat la primera |

**L'app del catàleg ja no crida `ui.focus`**: el dial es queda a la tira, que és on el va deixar la persona, i la
pàgina es construeix a sota. La crida continua existint per a les pàgines que es reconstrueixen per un altre motiu.

**Mesurat**: 148 casos natius verds, amb dues proves noves —`test_each_tab_of_a_strip_is_a_stop_of_the_dial` (una
fletxa mou el cercle dins la tira i no tria res, tres fletxes el treuen de la tira i cauen al botó de sota, el centre
prem el botó, i tornar enrere el deixa a l'última pestanya) i la lectura de la inversió **píxel a píxel** de la
superfície (`level_at(24, 88)` i `level_at(124, 88)`)—. Trencant `ui_tabs_items` perquè torni zero (la tira d'una sola
parada) es posen vermelles dues proves; trencant el `lit` del pintor, la de la inversió.

## 14. Els estats d'un control: seleccionat i deshabilitat (0.100.0)

**Fins a 0.99.0 un control no podia estar deshabilitat.** L'única cosa que s'hi assemblava era `focusable = false`,
que el treia del dial i del dit però **no canviava ni un píxel**: el control seguia tenint l'aspecte d'un control que
respon. I la frase que ho justificava —*«aquest vidre té dos tons i no hi ha manera de dibuixar una fila
deshabilitada que es distingeixi d'una activa»*— era del projecte vell: aquest vidre té **setze nivells** i fa servir
el to `dim` (8) per a les paraules secundàries de mitja dotzena de controls des de 0.97.3. El que ja existia era a la
**barra de menú**, on una entrada amb `enabled = false` es pinta amb `dim` i la volta se la salta (`ui.cpp:4522` i
`luabind.cpp:9083`).

**Què s'ha fet**, i per què cada peça és on és:

| Peça | Què fa |
|---|---|
| `disabled` (camp de tots els controls) | l'estat; `false` per defecte, sense haver-lo de declarar |
| el to (`ui_disabled_ink` / `ink_or_dim`) | la tinta del control cau al seu `dim`; **el paper i la pista d'un slider no es toquen**, i l'anella no es pinta |
| `ui_target_is_focusable` | el refusa, i és la pregunta que fan el dial, el dit i el teclat: un sol lloc per als tres camins |
| `__newindex` | escriure `disabled` **repinta** i, si el control tenia el dial, **el torna** |
| `ui_set_disabled_below` | l'estat **baixa als fills** (un botó apaga l'etiqueta que porta a dins) |
| els 21 pintors | cada un passa la seva tinta per `ink_or_dim` i el focus per `&& !off.on` |

**Els vint-i-un pintors i no un embolcall** és una decisió: cada pintor ja rep els tons explícits —és el que va fer
possible portar el projecte del paper sense inventar un tema— i aquest canvi només diu **quins d'aquells tons són
tinta**. Un embolcall genèric hauria hagut d'endevinar-ho per control.

**Què no s'ha tocat**: les opcions d'un **menú de targeta** continuen amb `hidden`, i el comentari que ho justifica
s'ha reescrit —la raó no és el vidre, és que una targeta és la llista del que es pot fer **ara**—. La barra de menú
continua tenint el seu `enabled` per entrada.

**Mesurat**: **438 casos natius verds** (437 abans, i un de nou); una prova nova
(`test_a_disabled_control_is_drawn_lighter_and_answers_nothing`) que mira el vidre —una fila marcada encesa és un
bloc de nivell **0** i la mateixa fila apagada un bloc de nivell **8**—, que una fletxa **salta** el control del mig,
que el centre prem el tercer i **mai** el segon, i que el **dit** tampoc no el toca (el banc necessita `opts.touch` i
`kernel::touch::boot`, que és el que faltava perquè el toc no fes res). Tres trencades a propòsit: fer que el dial hi
caigui torna la prova vermella, no aclarir els tons també, i no baixar l'estat als fills també.

## 15. Què pot contenir què, i la cara invertida (0.101.0)

**L'anidament ja hi era i era massa permissiu.** Tots els controls són un canvas amb un `children[32]` i
`attach_to()` no mira quin control és el pare: `button:add(label)` funciona des del primer dia, i `label:add(button)`
també. El problema no era la mecànica sinó **el dibuix**: el pare pinta el seu contingut i el fill es compon a sobre.

**El defecte mesurat** (app de prova al banc, un botó amb una etiqueta a dins i el cercle posat): el botó enfocat pinta
la cara de tinta i l'etiqueta, que té el seu `background` de paper, **la tapa**. Al bolcat el botó sortia com una barra
negra sota un rectangle blanc. És el que deia el comentari de `button_draw`: *«a child… is not inverted»*.

**Què s'ha fet:**

| Peça | Què fa |
|---|---|
| `l_canvas_draw` | la inversió **viatja com a argument** (`draw(self, depth, inverted)`) i el pare la passa als fills; un botó o la caixa d'un checkbox amb `focused` o `pressed` diuen «sota meu tot va sobre tinta» |
| `label`, `row`, `checkbox`, `sparkline`, `pie` | intercanvien els seus dos tons quan (2) és cert; el `dim` no es toca |
| `l_canvas_add` | `label` i `dots` refusen fills amb `E_INVAL`, que és el que l'amo va dir: no tenen fills |

**Per què un argument i no un flag del sistema**: `lua_pcall` fa `longjmp` i un flag encès abans d'un error es
quedaria encès; la pàgina sencera sortiria girada. Un argument no té estat que netejar.

**Què **no** s'ha fet i per què**: els controls d'elements (`tabs`, `list`, `menu`, `menu_bar`, `month`,
`icon_grid`, `notification_bar`, `selector`, `dialog`) segueixen sense fill per element. Caldria que **cada element
fos un canvas propi** —creat i alliberat en canviar de pàgina—, que el pintor deixés de dibuixar el mot quan
l'element té fill, i una consulta de geometria (`menu_hit`, `month_hit` i `icon_grid_hit` ja són la meitat). I topa
amb **`kMaxControls = 32`** canvases per a tot el sistema: vuit files amb checkbox i etiqueta en gasten disset. Pujar
la taula a 96 són uns 7 KB, i a la Paper queden 28,5 KB d'interna, o sigui que la taula hauria d'anar a PSRAM com la
vista.

**Mesurat**: **440 casos natius verds** (438 abans; dos de nous). El del botó llegeix el vidre: el paper de l'etiqueta
és **nivell 0** i el mot és **nivell 15** dins de la cara de tinta; el del refús comprova que `label:add` i
`dots:add` tornen `nil, E_INVAL` mentre `panel:add` torna `true`. Trencant el pas de l'argument al pintor, la prova del
botó es posa vermella («Expected 0 Was 15»).

## 16. Quants controls hi caben (0.102.0)

**`kMaxControls = 32` era una taula estàtica i, pitjor, era tres sostres en un**: quants controls cabien en una
pàgina, quants fills podia tenir cada control (la llista era un `int8_t children[kMaxControls]` **dins** de cada
canvas, o sigui 32 bytes per canvas i un sostre de fills igual al de controls) i, al pont, quantes parades podia
recórrer el dial (`kMaxFocusTargets = 32`, una matriu a la pila de l'event). A més, el slot era un `int8_t`, que deixa
de funcionar a partir de 127.

**L'amo ho va dir clar**: *«per qué hi ha un kMaxControls? vaig dir que la psram és el límit»*.

**Què s'ha fet:**

| Peça | Abans | Ara |
|---|---|---|
| la taula | `Canvas g_canvases[32]` a `.bss` | mig megabyte de la memòria extra, **3 276 slots** mesurats, i es parteix per dos si el bloc no hi cap |
| el slot | `int8_t` (127) | `int16_t` (32 767), i `kNoSlot = -1` **amb signe** |
| els fills | `int8_t children[32]` dins de cada canvas | llista enllaçada: `first_child` i `next_sibling`, sense sostre |
| la llista de parades del dial | `FocusTarget[32]` a la pila | un bloc de la memòria extra, un per event, tan llarg com slots té la placa |
| el panel | 16 fills (la pila del pont) | els que hi caben: 16 a la pila i un bloc per als panells més grans |

`sizeof(Canvas)` baixa de **192 a 160 bytes** mesurats (treure el vector de fills de 32 i posar-hi dos `int16_t`), i
la taula deixa de viure a la RAM interna.

**Una trampa que va sortir pel camí**: la primera `kNoSlot` era `uint16_t`, i `index != kNoSlot` amb un `int16_t`
compara **65535 amb -1**; el compilador ho va dir (`-Winfinite-recursion`) i el bucle de composició hauria estat
infinit.

**Mesurat**: **442 casos natius verds**, dos de nous: `test_the_table_of_canvases_is_what_the_memory_holds` (la taula
té més de 32 slots, es fan **150 controls** i tots pengen de la vista) i
`test_the_dial_walks_more_stops_than_the_old_list_held` (quaranta botons al vidre i el dial arriba al **trenta-sisè**,
quan abans s'aturava al trenta-dos). Tornant la taula a 32 a propòsit, les dues es posen vermelles.

## 17. Un checkbox esquemàtic (0.102.1)

L'amo el va demanar així: *«fes el control UI Checkbox més esquemàtic, un quadrat i una X dins si està activat o res
si no ho està»*, recordant que també pot estar deshabilitat i que llavors es dibuixa en un to més clar.

**Què era**: el checkbox portat del paper project a 0.97.0 —`Checkbox::paintBox()` i `drawCheckMark()`—, o sigui **radi
5** a les cantonades i un **tick en V** de dues pinzellades d'1/8 de la caixa.

**Què és ara**: **quatre traços de marc de 2 px** amb els cantons rectes, paper a dins, i quan `checked` **dues
diagonals de 2 px** d'un cantó de dins al de davant amb un píxel d'aire; si la caixa fa menys de deu píxels, es queda
sola. El que **no** ha canviat: la mida de la caixa (`cb:box()`, la mateixa), el color de la X (`mark`, un camp propi),
el focus que **inverteix** el quadrat, i el deshabilitat que passa pel to `dim` com tots els controls (0.100.0).

**Mesurat**: la prova del nucli `test_a_checkbox_is_a_square_with_a_cross` llegeix el vidre punt per punt —el cantó
`(10, 14)` és marc i no corba, el mig `(16, 20)` és el cos quan no està marcat i és tinta quan ho està, i les dues
diagonals `(14, 18)` i `(18, 18)` hi són—; **442 casos natius verds**, i al bolcat es veuen els tres estats: apagat,
encès amb la X, i encès i deshabilitat **en gris** (el marc, la X i la paraula del fill).

## 18. Un VBox i un HBox, i la cel·la que vol un percentatge (0.103.0)

**Els dos ja hi eren, amb un sol control**: `ui.panel(w, h, 'vertical' | 'horizontal' | 'overlay')`. Un panel no té
buffer ni pinta cap píxel: dona a cada fill el seu marc, i tot el que fa és aritmètica. El repartiment passa **quan el
panel es dibuixa**, i per això una pàgina girada o d'una altra mida es col·loca sola.

**El que hi havia**: `padding`, `gap`, i `child.weight` com a part del **lloc que sobra** (els fills de pes 0 es
queden la mida amb què es van fer, i el romanent el pren el darrer amb pes). O sigui que un `50/50` només sortia si les
dues cel·les tenien pes i no n'hi havia cap de fixa.

**El que s'hi ha afegit**: **`child.share`**, un percentatge (1 a 100) **de dins del box** i no del que queda, o sigui
que una cel·la que diu `50` és la meitat del panel passi el que passi amb els germans. Les tres maneres es poden
barrejar en un mateix panel: `share` primer, `weight` sobre el que queda, i la mida pròpia per a qui no diu res.

**I una cosa que estava trencada des de 0.102.0**: el pont va deixar de tallar el panel a setze fills, però
`panel_layout` **del nucli** encara feia `if (count > 16) return 0` i tenia la llista filtrada en una matriu de setze a
la pila — o sigui que un panel de quaranta files **no es col·locava gens** (totes a 0,0) i la prova que vaig escriure
llavors no ho veia perquè només mirava el dial. Ara la llista llarga va a un bloc, com al pont.

**Mesurat**: **444 casos natius verds**, dos de nous: `test_a_cell_can_claim_a_percentage_of_the_box` (50/50 d'un box
de 100 → 50 i 50; un `share` de 25 amb un pes darrere → 25 i 75; i **vint fills** en un panel tots col·locats, el
vintè a y=190) i `test_a_cell_of_a_box_claims_a_share_of_it` (des de Lua: `share = 30` i `70` en un box de 100 donen
30 i 70, i en un HBox de 300 `share = 25` i `75` donen 75 i 225). Ignorant el `share` al mesurament, la prova del
nucli es posa vermella.

## 19. Una llista de controls i no de dades (0.104.0)

**L'amo ho va dir així**: *«El control List te una llista dinàmica de controls ListEntry amb un UIControl fill cada
entry (la llista simple te un label, per coses més avançades un control Box per ficar-hi més controls)»*.

**El que hi havia**: `items`, una taula de taules amb `title`, `detail` i `subtitle`, i el nucli pintant les files. Una
fila no podia ser res més que tres paraules.

**El que hi ha ara**: `ui.list_entry(w, h)`, un control que pinta el paper de la fila —o la tinta quan és la `marked`
o la que té el cercle— i la línia del separador, **sense cap paraula dins**: el contingut és el que l'app hi posa (una
etiqueta, un `panel` horitzontal de dues cel·les, una caixa amb una imatge i un nom). La llista col·loca les entrades
de la **pàgina on és** a `row_height` de distància, reserva el peu, i les de les altres pàgines les posa **fora del
marc** (`frame_h + row_height`), on el retall no les compon i el recorregut del dial no hi arriba.

**Tres decisions**, dites perquè són meves: (1) `items` **es queda** i una llista amb fills i una llista amb dades
conviuen, perquè migrar dotze apps no és el que s'ha demanat; (2) el nombre de files d'una llista d'entrades és el de
**fills** quan `items` és buit, i és el que les fletxes munten i desmunten; (3) cada entrada és **una parada del dial**
pel fet de ser un fill —el recorregut ja baixa per l'arbre— i la llista continua sent la parada que gira les pàgines.

**Mesurat**: **444 + 1 casos natius verds**; la prova nova `test_a_list_of_entries_places_a_page_of_controls` munten
una llista de 400 × 180 amb sis entrades de 56 px: el peu en pren vint, queden 160, hi caben **dues** files i la
tercera es col·loca a **236** (el marc més una fila), i les fletxes porten de la pàgina 1 a la 2 amb `on_page`. Si es
col·loquen totes les entrades dins del marc (la trencada), la prova es posa vermella.

**El que faltava** llavors: la **graella** (`icon_grid`), que seguia amb cel·les de dades. Li va tocar el mateix
tractament al torn següent (§20).

## 20. Una graella de controls i no de dades (0.105.0)

**El que hi havia**: la graella tenia una cel·la feta de dues paraules (`mark` dins la caixa de la icona i `label` a
sota), i prou. Un launcher que vol una imatge amb el seu nom, o una cel·la de dues meitats, no hi cap.

**El que hi ha ara**: `ui.grid_entry(w, h)`, que és **el mateix control que `ui.list_entry` naixent amb el separador
apagat** —una sola funció al pont, dos noms— i el contingut és un fill. La graella, quan té fills, no pinta cap cel·la:
pinta **el paper de la seva pàgina i el peu** i posa cada fill on el nucli diu que és la seva cel·la.

**Dues peces noves al contracte del nucli**, perquè la geometria no es copiï mai:

- `icon_grid_draw(…, bool cells_of_the_kernel)`: amb `false` no entra al bucle de les cel·les (comença a
  `first + per_page`, que és fora del sostre del bucle) i només fa el fons i el peu.
- `icon_grid_cell(slot, count, index, columns, scale, which, x, y, w, h)`: la caixa d'una cel·la **en les
  coordenades del marc**, o `false` si aquesta cel·la és d'una altra pàgina. És la mateixa aritmètica que el pintat
  (`icon_grid_cells` i `icon_grid_box`), o sigui que el dit, el pinzell i el control de l'app no poden divergir.

**Tres decisions**, dites perquè són meves: (1) **`items` es queda**, com a la llista, i una graella amb fills i una
graella amb dades conviuen; (2) **el nombre de cel·les el compta el nucli** en el seu propi arbre de fills quan la
graella és d'entrades, perquè l'app no hagi de dir dues vegades quantes cel·les té (les pàgines del peu i el cursor
surten d'aquest número); (3) una cel·la fora de la pàgina es col·loca a `frame_h + 40`, **fora del marc**, que és el
mateix parany que la llista fa servir: el retall no la compon i el recorregut del dial no hi arriba.

**Mesurat aquest torn**: `sizeof(Canvas)` continua sent **160 B** i la taula de llenços **3 276 llocs** —llegit a
l'arrencada de l'emulador, `the table of canvases: 3276 slots of 160 bytes`; la cel·la no és cap camp del llenç, la
geometria es calcula i no s'hi desa—. El bolcat de l'emulador amb una graella de 500 × 400, quatre columnes i vuit
cel·les de 100 px dona **quatre cel·les per fila i dues files**, cada una amb el seu fill col·locat dins (mirat amb
`--dump`).

**Pendent de mesurar**: la suite no s'ha executat en aquest torn (l'amo ho va dir així). El que la prova nova
`test_a_grid_of_entries_places_cells_of_controls` ha de dir, quan es corri, és: graella de 400 × 200 amb quatre
columnes, la primera cel·la a **`0 0`** i **100** d'amplada, la segona a **`100 0`** —una columna a la dreta—, la
cinquena de sis a **`240`** (el marc de 200 més quaranta, o sigui fora), i quan les fletxes portin el cursor a la
cinquena, aquesta a **`0 0`**. La prova de nucli ha de donar cel·la de **100** d'amplada i **102** d'alçada en una
graella de 300 amb tres columnes. La trencada és col·locar totes les cel·les dins del marc, que ha de posar les dues
proves vermelles.

**El que falta**: res de la llista ni de la graella. El que queda de la UI del projecte antic és el que diu
`docs/90-full-de-ruta.md`.

## 21. Qui té el focus quan un control en porta un altre a dins (0.106.0)

**La pregunta de l'amo** va ser exacta: *«Com es soluciona el focus amb els controls anidats? veig que el UIControl
amb focus (el color invertit), però els controls fills estan normals (no focused).... No s'hauria de propagar d'alguna
manera? Només un UIControl te focus, però això fa confús el tema de qui te el focus en un botó amb un label fill
oi?»*. I la resposta que va demanar: *«fem que el control pugui declarar-se focusable o no (i de pas els botons
físics podran saber a qui donar focus o no). Si un UIControl està deshabilitat, els seus fills també i un control
deshabilitat no pot rebre focus.»*

**La resposta curta, que ara és a `docs/30`**: el focus és **d'un** control i no es reparteix; el que es propaga és
**la cara** on es dibuixa. Un botó amb una etiqueta a dins és una parada del dial —el botó— i l'etiqueta no n'és cap,
però com que la cara del botó s'ha girat a tinta, el que porta a dins s'ha de dibuixar **sobre tinta**. La pregunta
«qui té el focus aquí» es desfà preguntant **qui és al recorregut**, i això ho diu `focusable`.

**El que estava malament, i eren tres coses:**

1. **La cara invertida només arribava als fills de dos controls.** El dibuix tenia una llista de noms a dins —
   `__button` i `__checkbox`— i prou. Un `ui.list_entry` o un `ui.grid_entry` (0.104.0 i 0.105.0) pinten el seu paper
   de tinta quan són la fila marcada o la que té el cercle, **i no deien res als fills**: una etiqueta dins d'una
   entrada marcada es pintava amb el seu paper de sempre, o sigui **un pegat blanc damunt la tinta de la fila**. Ara
   és una propietat del control (`__inverts`, que porten `button`, `checkbox`, `row` i les dues entrades) i el dibuix
   només la llegeix.
2. **Un fill que arribava després no s'assabentava que el pare estava apagat.** `ui_set_disabled_below` camina els
   fills que hi ha **en el moment** d'escriure `disabled`, o sigui que `panel.disabled = true` i després
   `panel:add(button)` deixava el botó encès i el dial hi entrava. Ara **un fill d'un pare apagat neix apagat**.
3. **El pare tornava a encendre el que l'app havia apagat.** Apagar i tornar a encendre un panell escrivia `disabled`
   a tots els fills, i la decisió de l'app sobre un fill desapareixia. Ara el sistema guarda de qui ve cada `disabled`
   (`__off_father`) i tornar a encendre el pare retorna **els fills que ell va apagar** i deixa com estaven els que
   l'app va apagar.

**I un quart, que va sortir mirant el focus de prop**: `g_ui_focus_slot` era un **`int8_t`**. Des de 0.102.0 la taula
de llenços és tan llarga com la memòria (3 276 llocs), o sigui que a partir del control **128** d'una pàgina el lloc
del dial es desava en un byte amb signe, la comparació amb el lloc del control no coincidia mai i **l'anella era
invisible**. Ara és un `int16_t`, com el lloc del llenç.

**`focusable`, dit sencer**: és un camp de **tots** els controls (abans només el validaven els botons), val **`true` o
`false` i res més** —un `3` és un error, perquè és l'únic camp que decideix si una persona pot arribar a un control—,
i un control **deshabilitat no és enfocable encara que digui `true`**. Els botons físics pregunten el mateix que la
volta i el dit (`ui_target_is_focusable`), o sigui que un control que diu que no, no el rep de cap de les tres
maneres.

**Mesurat aquest torn**: la compilació del nucli, del pont i de la suite de proves surt **sense cap avís**; el bolcat
de l'emulador amb una fila marcada i un botó amb el cercle, tots dos amb una etiqueta a dins, dona **dos blocs de
tinta amb la paraula en paper** (`--dump`, mirat). **La suite no s'ha executat** (l'amo ho va dir així en el torn
anterior): les quatre proves noves estan escrites i compilen —
`test_an_entry_that_is_marked_inverts_what_it_holds`, `test_a_child_of_a_disabled_control_is_born_disabled`,
`test_a_control_declares_whether_it_takes_the_dial` i
`test_the_ring_reaches_a_control_past_the_hundred_and_twenty_seventh`— i les trencades que les han de posar vermelles
són: treure `__inverts` de l'entrada, no heretar el `disabled` a `l_canvas_add`, i tornar `g_ui_focus_slot` a
`int8_t`.

## 22. Un teclat que un dit pot prémer (0.107.0)

**L'amo, amb la placa a la mà**: *«El teclat ha de ser més alt i amb tecles i lletres més grans, gairebé no es poden
polsar. També trobo a faltar una tecla: "Backspace" (en icona, com l'<enter>)»*.

**Les tecles feien 44 píxels i la lletra dos passos de cos.** En una banda de 176 píxels (quatre files), la lletra
feia divuit píxels d'alçada: es llegeix a un pam de la cara i no a la distància a què es té una placa a la mà. La
xifra que decideix és aquesta: el panell del Paper té sis píxels per mil·límetre, i un polze prem sense mirar a partir
d'un centímetre, o sigui **seixanta píxels**. Ara la tecla en fa **64** i la banda **256** (quatre files), o sigui que
l'app té **656** píxels de pàgina on abans en tenia 736 —el preu de poder escriure—, i la banda no passa mai de dos
cinquens de la vista, així que en un vidre petit s'encongeix ella (mínim 32).

**La lletra ocupa ara l'alçada de la tecla**: la mida surt de la cel·la i no d'una constant (`(h - 8) / body`), amb el
sostre a **cinc passos**, i la paraula d'una tecla (`123`, `MAY`, `space`) va **dos passos per sota** de la seva
lletra perquè capgi en una cel·la de cinc o sis caràcters. El sostre de cinc no és arbitrari: el glif es dibuixa
antialiasejat a **doble mida**, o sigui que cinc passos són noranta píxels i el bloc de la corba era de seixanta-quatre
—**la lletra més gran queia al text de blocs**, precisament la que no es pot llegir—. El bloc ara és de **96 × 96**
(1 152 bytes en lloc de 512, al costat del codi i no a la memòria extra: el glif d'una tecla, un cada vegada).

**El retrocés era una fletxa**, la mateixa que faria servir un gest de «tornar», i l'amo la trobava a faltar com a
tecla perquè no la llegia com a tal. Ara és **el símbol del retrocés**: el pentàgon de la caixa que esborra —dos braços
cap a un punt a l'esquerra i tres costats rectes— amb una **creu a dins**. L'enter continua sent la fletxa que gira
(`↵`), i la barra d'espai diu `space`. Les dues icones comparteixen el dibuix de traços (`ink_line`) i no el d'un
caràcter de la tipografia, que a vint-i-cinc píxels seria una taca.

**Mesurat aquest torn**: `kb:height()` diu **256** i `ui.caps().height` **656** amb la banda al vidre (llegit de
l'emulador, no suposat), i el bolcat de la banda amb el QWERTY dona quatre files de tecles grans amb les lletres
llegibles, el **pentàgon amb la creu** a la dreta de la `m` i el `↵` a la dreta del punt. **La suite no s'ha
executat** (l'amo no ho ha demanat): les dues proves de la banda s'han reapuntat de 176/736 a 256/656 i és el que
s'hauria de córrer —`test_the_keyboard_takes_its_band_out_of_the_view_the_app_is_given` i
`test_the_keyboard_follows_the_box_that_takes_the_focus`—, i la trencada que les ha de posar vermelles és tornar la
tecla a 44.

## 23. El cursor del final d'un document, i l'editor al dia (0.108.0)

**L'amo ho va veure a la placa**: *«He observat un glitch al polsar <enter> al teclat, si es el final del document, el
cursor no baixa i queda text solapat, al escriure es veu el text nou a la fila seguent. Tot sembla funcionar, però el
cursor no està anant on toca (el document s'amplia amb un retorn de carro al final, així que el cursor pot arribar-hi
per les normes que vam parlar de com es comporta el cursor als camps d'introducció de text)»*. La diagnosi era
exacta: **el cursor hi era i el dibuix no sabia on**.

**El que passava**: el recorregut que compta les files d'una caixa de diverses línies parava quan el punter arribava al
`\0`, i un text que s'acaba en `\n` té una fila buida **després** de l'últim salt. Amb `"one\ntwo\n"` el
recorregut en comptava **dues** i el cursor del final (byte 8) no pertanyia a cap de les dues: `cursor_row` es quedava
al valor inicial, que és zero, i el bloc de tinta es pintava **a la primera fila**, sobre les paraules del document —
mentre el text, que sí que creixia, posava la línia nova a la tercera—. El resultat era exactament el que es veu al
vidre: text solapat a dalt i lletres noves baixant.

**La correcció**: el recorregut continua una fila més quan l'últim byte consumit és un salt de línia, i s'atura quan la
fila mesura zero bytes (que és la fila buida del final, comptada una vegada). `box:lines()` diu **3** per a
`"one\ntwo\n"`, i la fila del cursor és la tercera amb el cursor a zero. Res més no canvia: el pintor, el dit i el
gest demanen la mateixa mesura, o sigui que la correcció val per als tres alhora.

**L'editor, posat al dia** (era el motiu del torn, i les tres coses que l'amo va demanar):

- **El peu era massa gran**: dues línies de quaranta-sis píxels amb el cos del sistema. Ara són de **28 i 24** amb
  **dos passos** de cos (catorze píxels de text), el peu en tinta i la nota en el to `dim` (8). El peu diu **la fila
  del cursor i el total** (`row 1/67`), que surt de `box:lines()` i del comptatge dels salts abans del cursor: és
  informació que el control ja donava i l'app no ensenyava.
- **La caixa ocupa el vidre**: de sota la barra fins al peu i **d'extrem a extrem**, sense els dotze píxels de marge de
  cada costat que l'app es quedava (eren dos-cents quaranta píxels d'una línia de codi perduts). L'únic marge és el
  marc de la caixa, que és el control. I el text va a **tres passos** perquè el vidre porta un document i no un llibre:
  vint-i-set píxels per fila són vint-i-nou files on el cos del sistema en donava vint-i-dos.
- **Una pàgina d'exemple, `notes.txt`**: viu a `apps/editor/res/notes.txt`, que és el `/res` **propi de l'app** (la
  regla del nucli: `/res` són els fitxers estàtics de l'app, `apps/<id>/res/`), i la primera arrencada la copia al
  terreny de l'app (`/data/editor/notes.txt`, amb `fs.mkdir` del terreny abans, que una app que no ha escrit mai no el
  té) perquè **Save hi escrigui sense demanar cap camí**. Fa **1 417 bytes**, té ratlles llargues per provar el wrap,
  més files que el vidre per provar la barra, i una llista del que es pot provar.

**Mesurat aquest torn**: `box:lines()` dona **3** per a `"one\ntwo\n"` i el bolcat de la caixa amb el cursor al
final posa el bloc **a la tercera fila** (`--dump`, mirat); l'editor obre la pàgina d'exemple (`the sample page is
/data/editor/notes.txt, 1417 bytes`), el peu diu `/data/editor/notes.txt  1417 B  row 1/67` amb el text a dos passos,
i la caixa arriba a les dues vores del vidre. **La suite no s'ha executat** (l'amo no ho ha demanat): la prova nova
`test_a_text_that_ends_in_a_break_has_a_row_for_the_caret` està escrita i compila, i la trencada que l'ha de posar
vermella és tornar el recorregut a parar al `\0` sense comptar la fila del salt.

## 24. Les dues icones del teclat, dibuixades de nou (0.109.0)

**L'amo les va assenyalar una per una**: *«Al teclat hi han dos botons "malament" el boto <backspace> te la "X"
desplaçada a la dreta. L' <enter> te la fletxa feta amb dos segments. Fes el dibuixos de nou d'aquests dos botons i
fes-los bé.»*

**Eren dos errors de dibuix i no de disseny**, i tots dos es veien a la mida real del símbol (vint-i-cinc píxels):

1. **El traç d'una línia es dibuixava amb la cantonada del bloc al punt de la línia.** `ink_line` posava un bloc de
   `t × t` a cada pas començant pel seu vèrtex de dalt a l'esquerra, o sigui que tota diagonal sortia **mig traç cap a
   la dreta i mig cap avall**. En una creu, dues diagonals que es desplacen igual es desplacen juntes: la X quedava
   centrada a `head + (w - head) / 2` (que comptava el punt del pentàgon com si la creu hi pogués anar) **i a més mig
   traç a la dreta**, i amb uns braços d'un cinquè de l'alçada arribava a tocar el costat dret del pentàgon i es
   llegia com una taca. Ara el traç va **centrat a la seva línia** (el bloc es posa mig traç enrere i mig amunt) i la
   creu es mesura des del **cos** del pentàgon, amb un terç del que hi cap a dins.
2. **La barra de l'enter s'acabava abans del traç que baixa.** Se li donava `2 * arm - head` d'amplada i el traç que
   baixa és a `cx + arm - t`: un forat de píxels a la cantonada, i la tecla es veia com **dos segments** —una barra i
   una punta— en lloc de la fletxa que gira. Ara la barra **arriba al traç** (es dibuixa fins a `right + t`), la
   cantonada és una sola peça, i el cap és un triangle les columnes del qual porten el gruix de la barra.

**I el símbol és més gran**: `side` passa de dos cinquens de la tecla a **tres cinquens** (38 píxels en una tecla de
64). A vint-i-cinc el pentàgon amb un traç de tres píxels té un cos de catorze i una creu que hi toca; a trenta-vuit el
cos és de vint-i-tres i la creu de disset, amb aire als costats.

**Mesurat aquest torn**, llegint els píxels del bolcat de la banda amb el QWERTY: a la fila del mig del cos del
retrocés la creu ocupa **cinc columnes centrades** (491..495) i el costat dret és a **504..506**, o sigui **vuit
columnes de paper** entremig —abans la creu arribava fins al costat—; i a la tecla de l'enter la barra i el traç que
baixa fan **una tirada de trenta-set columnes** (470..506) en una sola peça, sense el forat que hi havia a la cantonada.
La prova nova `test_the_two_symbols_of_the_band_are_one_piece_each` mesura les dues coses: ink al creuament, **paper** a
`icon_x + 3·side/4` (que és el píxel que la trencada posa en tinta) i la tirada d'ink més llarga de la tecla de
l'enter, que ha de passar els dos terços de l'amplada del símbol. **La suite no s'ha executat** (l'amo no ho ha
demanat).

## 25. La suite sencera, i el terminal que es quedava cru (0.110.0)

**L'amo va donar el permís per córrer les proves** —*«fes les proves i deixa-ho tot llest perquè pugi el firmware i el
littlefs al paper»*— i després va dir el que li havia passat: *«per algun motiu els tests fan que el terminal perdi
l'echo, només una comanda "reset" restableix el terminal rera el test»*.

**La primera passada de la suite des de la 0.104.0** (452 casos) en va trobar tres que cap lectura havia trobat:

1. **Una pàgina de més de cent vint-i-set controls matava el procés.** Els dos recorreguts de `open_draw` duien el lloc
   en un **`int8_t`**: a partir del 128 la cadena era un número negatiu, es llegia la taula abans del primer lloc i el
   banc moria amb un **SIGSEGV a `button_draw`**. És exactament la mateixa errada que el lloc del dial tenia fins a la
   0.106.0, i la va trobar la prova nova de la pàgina de cent quaranta botons (que és el cas que l'amo va demanar:
   *«per què hi ha un kMaxControls?»*). Ara és un `int16_t` i la pàgina passa.
2. **Una cel·la que no és a la llista tenia lloc.** `icon_grid_cell` responia per a qualsevol cel·la de la pàgina: demanar
   la novena d'una graella de vuit donava una caixa que ningú no pinta.
3. **Un fill que l'app havia apagat, el pare el tornava a encendre.** `ui_set_disabled_below` escrivia la seva marca a
   sobre d'un `disabled` que havia posat l'app, o sigui que `panel.disabled = true` i després `false` es menjava la
   decisió de l'app. Ara només és del pare el fill que el pare apaga.

**I el terminal es quedava cru perquè el `atexit` no arriba quan el procés mor d'un senyal**: la suite moria amb aquell
SIGSEGV, el restabliment no corria mai, i la persona es trobava el terminal sense echo fins a un `reset`. Ara `pal_host`
instal·la gestors per a `SIGSEGV`, `SIGBUS`, `SIGABRT`, `SIGTERM`, `SIGINT`, `SIGHUP` i `SIGQUIT` que posen el terminal
com estava —amb `ioctl(TCSETS)`, que és de les poques crides que un gestor pot fer— i deixen que el senyal mati el
procés com ho hauria fet.

**Mesurat aquest torn**: **452 casos natius, 452 verds** en 1 min 28 s (test_ui 94, test_apps 159); el firmware del Paper
compila **sense cap avís** (2 308 304 B) i el seu disc fa **372 437 B en 141 fitxers, 58 apps**, i cap a la partició de
12 451 840 B. Les trencades, totes provades: treure la fila del salt final del recorregut posa vermella
`test_a_text_that_ends_in_a_break_has_a_row_for_the_caret` (3 files contra 2); fixar el traç a la cantonada posa
vermella `test_the_two_symbols_of_the_band_are_one_piece_each` (la creu toca el costat); una barra curta la posa
vermella per l'altra banda (el píxel que ha de ser tinta és paper); i **sense els gestors de senyal el terminal es
queda cru**, mesurat amb un pty: `IT_WAS_RAW` i `TERMINAL_LEFT_RAW` contra `TERMINAL_BACK`.

**El que queda llest per pujar**: `pio run -e m5-paper -t upload` (firmware) i `pio run -e m5-paper -t uploadfs` (el
littlefs), que és el que fa l'amo i no l'assistent.

## 26. Una imatge als moments del sistema: la splash (0.111.0)

**El que va demanar l'amo**: *«Crea una app nativa C++ de sistema que faci una splash screen, se li passa una imatge
que l'app mostra al centre de la pantalla rera haver fet una neteja completa del panell. Aquesta app s'executarà en
diferents moments durant l'execució del sistema (boot, shutdown, charging...)»*.

**És un mòdul del nucli i no una app**, i el perquè és el dels moments: una placa que s'aixeca no té cap app al
davant i una que baixa acaba de tancar-ne la seva, i l'únic que hi ha en tots dos casos és el sistema. O sigui que
`kernel/splash.h` és germà de `kernel/display.h` i no de `apps/`: pinta **la superfície** (`hal::ISurface`) i no el
llenç d'una pàgina.

**Dues coses i una regla.** La neteja és **de tot el panell** (`clear(15)` i un `flush(gc16)`, el mode que
s'emporta el fantasma) i la imatge va **al mig, a la seva mida i sense escalar** —més gran que el vidre, es talla pel
centre; si hi cap, es deixa com és—. L'`img` del sistema dona les files a quatre bits i cada fila es copia a un bloc
de la memòria extra de la mida del que hi cap; el fitxer sencer i el bloc de descodificar també van a la memòria
extra i es tornen quan la imatge és al vidre.

**Els moments són paraules i la paraula és el nom del fitxer**: `/res/splash/<moment>.png` (o `.bmp`, `.jpg`) i
`/res/splash.png` per a tots. En la 0.111.0 eren **quatre** —`boot`, `shutdown`, `charging` i `sleep`— i el banc
portava **tres imatges de 460 × 200** fetes amb la tipografia del sistema (`boot.png`, `shutdown.png`,
`charging.png`); `sleep` no en tenia, i això és el que la prova fa servir per veure que un moment sense imatge **no
pinta res i diu per què**. La 0.114.0 els va portar a **sis** i va canviar `boot.png` pel logotip: vegeu §29.

**On s'encén**: a `kernel::boot`, just després de `display::boot` (el panell acaba d'arrencar i no hi ha res més al
vidre); i a `power::sleep`, abans que el xip parin els rellotges, **només en un son profund** —un son lleuger és una
becaina: la placa té el que tenia i torna de seguida—. L'encès de `charging` **queda per cablejar** a la política
d'energia: el moment i la seva imatge existeixen i `splash charging` la pinta, però el sistema encara no té un moment
que sigui «al cable, esperant».

**I una comanda per provar-ho sense reiniciar**: `splash` (la llista), `splash <moment>`, `splash /ruta/imatge.png`
i `splash hide`.

**Mesurat aquest torn**: **453 casos natius, 453 verds** (un de nou, `test_the_splash_paints_a_picture_in_the_middle_of_the_glass`,
a `test_apps` perquè és on hi ha el disc i el vidre); el bolcat de l'emulador ensenya `[I][SPLASH] /res/splash/boot.png,
460x200 at 40,380` i la imatge al mig del vidre de 540 × 960 amb la barra a sobre. Les trencades, provades: **treure la
neteja** deixa la taca de tinta de la cantonada a la prova (Expected 15 Was 0) i **pintar la imatge a la cantonada**
deixa el mig buit (Expected 0 Was 15).

## 27. Els punts de pàgina, la còpia del projecte del paper (0.112.0)

**L'amo ho va dir així**: *«Revisa el control original de Dots al PaperOS i compara'l amb el que has implementat a
LunokOS.... et vaig dir que volía una copia exacta del aspecte dels controls de PaperOS a LunokOS»*. I tenia raó: la
tira de punts de lunokOS era **una altra tira**.

**El que diu l'original** (`OLD/PaperOS/src/ui/page_dots.h` i `.cpp`, amb els comentaris del seu amo a dins):

| Cosa | PaperOS |
|---|---|
| bola de les pàgines que no es llegeixen | **19** px, anella de **2** px de tinta i paper a dins |
| bola de la pàgina que es llegeix | **25** px, **sòlida**, i és l'ample de **totes** les caselles |
| aire entre dues boles | **7** px |
| pal que les enfila | **3** px, dibuixat **abans** que les boles |
| caixa del disseny | **37** px: 25 de bola i **12** d'aire a sota; la tira es pinta **a dalt** de la seva caixa |
| pàgines que no hi caben | fins a **16**, i la tira es queda clavada a setze |

**El que hi havia a lunokOS fins a la 0.111.0** era una tira derivada de l'alçada de la caixa: bola petita `height/3`
(lligada a 3..12), bola gran `small + 4` (16 com a molt), anella 2 o 1 segons la mida, pal 2 o 3, aire 8 o 5, la tira
**centrada a la caixa** i el lloc de cada bola el de la gran (16 com a molt) — i les dues apps del disc li donaven una
caixa de **10 px d'ample i 22 o 26 de alt**, o sigui que les boles sortien de **7 i 11 píxels**: menys de la meitat de
les de l'original, i el comptador ni tan sols es veia bé.

**Ara** les set xifres són les de l'original, fixes i no un terç de l'alçada: **19, 25, 2, 3, 7 i 12**, amb la caixa de
37 i la tira a dalt de la seva caixa. Una caixa **més baixa** que 37 encongeix tota la tira amb el mateix factor —perquè
una app que li dona vint píxels no pot rebre una bola de vint-i-cinc— i una de més alta li deixa l'aire a sota; amb la
caixa del disseny, la tira és la de l'original **píxel per píxel**, perquè el dibuix és el mateix: un disc de tinta i,
per a les que no es llegeixen, el paper a dins de dos píxels enllà; el pal abans que les boles.

**Dues diferències que es queden i que es diuen**: (1) el **finestra** de 0.94.1 quan les pàgines no hi caben —l'original
es quedava a setze i la tira sortia de la pantalla, i una tira que surt del vidre no és un comptador—; i (2) l'**escala**
quan la caixa és petita. Cap de les dues es veu amb la caixa del disseny.

**I les apps s'han arreglat**: `showcase` i `controls` donaven a la tira deu píxels d'ample; ara li donen
`view.width - 2 * 12` i **37** d'alçada, que és el que li demanava el projecte del paper amb el `preferredSize()`.

**Mesurat aquest torn**: la tira de cinc boles de `showcase`, llegida píxel a píxel del bolcat, dona **bola sòlida de 25
píxels** (de la fila 863 a la 887), **anelles de 19 amb dos píxels de tinta** (la vora esquerra a 58 i 59 i el paper a
60), **pal de tres files** (11, 12 i 13) i **la tira sencera de 149 píxels**, que és 12 + 4 · 32 + 9; i el bolcat de
`controls` ensenya dues anelles de 19 i una bola sòlida de 25 amb el pal de 3 al mig. Les proves dels punts s'han
reapuntat amb els números de l'original (box de 200 × 37: centres a 67, 99 i 131, fila dels centres la 12, l'aire de
sota fins a la 36) i la suite sencera està verda.

## 28. El document de l'editor i el dit (0.113.0)

**La queixa de l'amo**: *«No aconsegueixo fer scroll al document de l'Editor»*. I la caixa de diverses línies tenia
gest des de la 0.88.0: el que faltava no era el gest sinó les condicions que el fan segur.

**Què hi havia fins a la 0.112.0.** El sistema buscava el control que el dit va tocar en baixar, li donava el focus,
s'apuntava el `slot` i la `generation` i pintava. A cada moviment tornava a buscar el control per aquella parella i, si
el trobava, movia la finestra. **No preguntava res més**: ni si el control tenia el focus encara, ni si el dit era dins
del rectangle de la caixa, ni si l'app havia donat el control de baixa pel camí. I `box:release()` i el col·lector
deixaven el gest viu: el dit podia seguir movent un `slot` que ja era d'un altre control.

**Què fa ara.**

1. **El focus i el rectangle, a cada moviment.** Si el control ha perdut el dial (`focused` fals) o el dit ha sortit del
   rectangle de dibuix de la caixa, el gest s'atura i les files que havia recorregut es perden.
2. **Una caixa que es deixa anar es queda sense gest.** `release()` i el `__gc` criden el mateix oblidador, que neteja
   el contacte, el gest i el cursor que esperava. I el contacte queda marcat com a **orfe** (`g_ui_touch_orphan`) fins
   que el dit s'aixeca: la caixa que l'app construeixi al mateix lloc **no** rep el swipe d'un contacte que va començar
   a la que s'ha anat.
3. **El teclat també.** Si la caixa que es deixa anar és la que escrivia el teclat, el teclat es queda sense objectiu:
   una referència a un `slot` que es recicla és un número que aviat vol dir un altre control.
4. **El `wrap` ja comptava i ara ho diu una prova.** `multiline_layout` i `textbox_multiline_scroll` reben el `wrap` de
   la caixa des de la 0.88.0, i `ui_swipe_by` el llegeix del camp; el que faltava era una prova que ho mesurés. Amb deu
   línies de quaranta lletres en una caixa de 200 × 100 a escala 2: **11 files** sense `wrap` (les deu línies i la fila
   buida del salt final, 0.108.0), **més files amb `wrap`**, **6 files que s'hi veuen**, el **polze més curt** amb
   `wrap` que sense, i l'**última finestra** a `total − 6` en tots dos casos, que és on el dit s'ha d'aturar.

**També s'ha trobat, i s'ha arreglat després** (0.116.0): aleshores `ui.focus(box)` **no feia res**, perquè `ui.focus`
camina els controls del dial i una caixa de text no n'era una parada (`ui_target_is_focusable` la deixava passar només
amb el dit a sobre). La 0.116.0 fa que la caixa sigui una parada del dial i `ui.focus(box)` funciona; la prova d'aquest
torn fa servir un botó perquè en aquell moment no hi havia cap altra manera.

**Les proves noves**: `test_the_document_of_the_editor_scrolls_with_a_finger` (arrenca l'editor del disc, que copia la
seva mostra de `/res`, i el fa rodar amb el dit), `test_a_swipe_only_moves_the_box_that_has_the_dial`,
`test_a_swipe_that_leaves_the_box_stops_where_it_left_it`, `test_a_box_that_is_given_back_leaves_no_swipe_to_the_one_that_follows`
i `test_the_bar_and_the_window_of_a_box_of_lines_count_the_rows_of_the_glass` al nucli.

**Mesurat aquest torn**: amb el teclat obert la pàgina de l'editor és de **540 × 656** i la banda del teclat de **256**
px (ho diu la consola de l'app: `editor: view 540x656 band 256`), i el dit que camina la caixa fa dir a l'app
`editor: row N of M`. **La suite sencera queda pendent de passar** en el moment d'escriure això: són **458** casos
(453 i cinc de nous) i les trencades per provar són treure el guard del focus (la prova del dial s'ha de posar
vermella) i treure el marcatge del contacte orfe (la prova de la baixa ha de veure el `fresh`).

## 29. El logotip, els sis moments i la splash d'una app (0.114.0)

**El que va demanar l'amo**: *«El logo de lunokOS ha de ser una lluna plena realista amb la paraula "lunok" dins la
lluna i "OS" sobresortint per la dreta de la lluna amb el color invertit (sobre la lluna text negre amb vores
blanques) fora de la lluna text blanc amb vores negres. Fes una versió alternativa amb la luna en plan clipart. Les
provarem ambdues.»* I, en el torn següent, l'aclariment que va canviar el dibuix: *«en quant a la lluna realista, em
refería a una foto amb el fons blanc»*.

**El logotip són dues tintes i una paraula.** «lunok» va **dins** de la lluna: tinta amb vora de paper, perquè el disc
és un gris clar i la vora blanca és el que fa que la lletra segueixi sent una lletra allà on hi ha un cràter fosc.
«OS» va **fora**: paper amb vora de tinta, perquè una lletra blanca sobre paper no és res sense la vora. La unió de la
paraula és la vora del disc, o sigui que és la lluna la que separa les dues meitats, i el gruix de la vora és de **tres
píxels**: amb quatre la vora s'empassa la lletra i «OS» surt tota de tinta (mesurat: la tija d'una lletra aquí fa nou o
deu píxels).

**La versió «realista» és una fotografia.** És la imatge **`as11-44-6667`** de la NASA —la lluna plena fotografiada des
de l'Apollo 11—, que és una obra del govern dels Estats Units i, per tant, de domini públic: el cel negre s'hi treu
(retallant el disc, que és el que el cinturó de brillantor diu on és) i el paper del sistema hi va al darrere. No té
res a veure amb la primera versió que vaig fer, que era una esfera pintada per mi: l'amo va dir que volia una foto, i
una foto és el que hi ha. La segona versió és el mateix logotip amb la lluna **en clipart**: disc pla, contorn gruixut,
cràters que són cercles i quatre estrelles de quatre puntes; els cràters no cauen dins de la banda on passa la paraula
perquè una taca sota una lletra es llegeix com a part de la lletra.

**Les dues imatges les dibuixa `tools/make_splash_images.py`**, que també escriu `reboot.png` en l'estil pla de
`poweroff.png` i `charging.png` (la paraula, una ratlla, la línia de sota). La fotografia es baixa **un cop** i es
deixa a `~/.cache/lunokOS/`: el repositori guarda el que en surt —`boot.png` de **18,8 KB**— i no els 160 KB de la foto
de ningú.

**Els moments passen de quatre a sis**, i són les paraules que una persona escriu: `boot`, `boot-alt` (el logotip en
clipart, que és un moment propi i no un commutador del dibuix, perquè una imatge és un fitxer i un fitxer és una
paraula), `poweroff` (era `shutdown`), `reboot`, `charging` i `sleep`. `poweroff` i `reboot` s'encenen **de debò**: el
`poweroff` de la consola, abans de tallar el corrent, i el `reboot`, abans de reiniciar; i el son profund, que ja ho
feia, ara pinta `poweroff`. `sleep` continua sense imatge, i és el cas que la prova fa servir.

**I una app pot tenir la seva**: `"splash": "res/logo.png"` al `manifest.json`. El sistema, quan arrenca una app de
Lua, pinta la imatge amb la mateixa crida dels moments —neteja de tot el panell i al mig, sense escalar— i després
arrenca l'intèrpret; la primera pàgina que l'app presenta se l'emporta. És el sistema i no l'app qui la pinta perquè
una app no pot pintar abans d'existir. Un nom que surti del directori de l'app (`/`, `..`) es descarta amb una línia
al registre, i un nom que hi és però no és cap imatge no impideix res: **el dibuix és decoració i l'app arrenca
igual**.

**Les proves escrites**: `test_the_splash_paints_a_picture_in_the_middle_of_the_glass` repassa ara els sis mots
(`count()` és **6**, `kind_of("boot-alt")` no és `boot`, i `word(kBootAlt)` és `boot-alt`) i
`test_an_app_that_names_a_picture_has_it_painted_before_it_starts` arrenca una app del banc amb `"splash"` i mira el
vidre: els dos píxels de la imatge al mig del panell i el paper a la vora, la tinta d'un racó esborrada per la neteja,
i una app sense el camp que deixa el racó amb la tinta com era.

**Pendent**: passar la suite (les proves d'aquest torn i les de la 0.113.0, que encara no han corregut) i mirar les
dues imatges al vidre de debò, que és el que l'amo vol provar.

## 30. El logotip a l'arrencada, el més aviat possible i triat per la persona (0.115.0)

**El que va demanar l'amo**: *«El logo s'ha de veure a l'iniciar el sistema en cas de que el dispositiu tingui
algun tipus de pantalla. Usa la comanda splash boot per mostrar aquest boot splash. La comanda cfg ha de tenir una
opció per mostrar "boot" o "boot-alt" al iniciar el sistema. L'splash del boot s'ha d'arrencar el mes aviat
possible als dispositius amb pantalla.»*

**El logotip ja hi era, però no era el primer que passava.** Fins a la 0.114.0 `display::boot` i la splash eren
**al mig** de l'arrencada: després de les alarmes, de la ràdio, de la llum, del botó, del sensor, de l'aire i del
tàctil. O sigui que la placa encenia el panell quan ja havia fet mitja feina, i la persona mirava un vidre blanc
mentre tot allò passava. Ara **el panell i el logotip són el primer que fa el sistema després de tenir les eines
per fer-ho**: els muntatges (el dibuix és un fitxer) i els ajustos (el dibuix és el que la persona ha triat) van
davant, i la resta del maquinari —les alarmes que han sobreviscut al tall, la ràdio, la llum, el botó, el sensor,
l'aire, el tàctil, la barra— es fa **mentre el vidre ja diu lunokOS**. És literalment la mateixa crida que fa la
comanda `splash boot` (`splash::show(kind_of(cfg::boot_splash()))`), que és el que l'amo va demanar: una manera de
pintar-lo i no dues. Una placa sense pantalla contesta `no glass` i la línia queda al registre de depuració,
perquè no hi ha cap altre lloc on dir-ho.

**La tria és un ajust del sistema, com el de la unitat i el del teclat.** `cfg boot` llista els dos dibuixos amb el
mot, el camí de la imatge que el disc té per a cada un i una estrella al que surt ara:

```
lunok> cfg boot
cfg: boot pictures are
  * boot  /res/splash/boot.png
    boot-alt  /res/splash/boot-alt.png
lunok> cfg boot boot-alt
settings: boot boot-alt
```

Va a l'NVS (`cfg/boot`) i sobreviu a un reinici; un mot que aquest build no té es **refusa allà on s'escriu** (amb
la llista), i el que hi havia es queda. **És una paraula i no un camí**, i això és una decisió i no una comoditat:
el que es desa és un dels mots de la splash, i un camí a l'ajust seria una manera de fer que el primer alè de la
placa obrís qualsevol fitxer del disc. El mot es pregunta a la mateixa llista de moments de la splash
(`is_a_boot_word`), i no a una segona llista que podria divergir: quan hi hagi un tercer dibuix, només caldrà que
sigui un moment del boot.

**Mesurat a l'emulador**, amb la targeta i l'NVS a `emulator/`:

```
[I][CFG] /cfg on sd (asked for sd), keyboard qwerty, boot boot-alt
[I][SPLASH] /res/splash/boot-alt.png, 460x200 at 40,380
[I][BOOT] board=m5stack-paper caps=16/19
```

O sigui que la splash surt **abans** de la línia que diu què té la placa, que fins ara era de les primeres; el
bolcat del vidre ensenya el clipart al mig del panell i el paper a les vores; i `cfg boot boot` el torna a la
fotografia. La prova del banc (`test_the_boot_paints_the_drawing_the_settings_chose`) escriu dos BMP de dos per dos
—el mateix dibuix de cap per avall— amb els dos noms, tria el mot, arrenca el sistema i llegeix **el píxel del mig
del vidre**: blanc sobre negre amb `boot-alt` i negre sobre blanc amb `boot`, i a més el refús de `charging` (un
moment que no és un dibuix del logotip) i que la tria sobreviu a un `cfg::boot`.

## 31. Entrar a una caixa, el cursor sense banda i la vora que no hi és (0.116.0)

**El que va demanar l'amo**: *«Si un control multiline esta en mode read only no s'ha de mostrar el teclat quan el Text
multiline reb el focus. El cursor si que hi ha de ser en qualevol cas. El Control Multiline te una vora per defecte, en
aquesta app no vull que hi sigui, fes que el control tingui opció per mostrar vora o no. També que el "read only" ha de
ser una propietat del control Multiline, que no aixecarà el teclat en cas de rebre focus. El control de text d'una sola
linia igual. Quan el control de text d'una sola linia reb el focus, com el control Slider, s'ha de polsar el botó central
per "entrar" al control i poder usar els botons físics per avançar el cursor o anar enrrere. El control multiline igual,
però amb linies, cada toc dels botons físics puja o baixa una linia. Revisa l'app lua Editor per si els canvis li
afecten.»*

**El cursor era del teclat i ara és del focus.** Fins a la 0.115.0 el bloc d'una caixa vivia de la banda: `caret_poll`
no feia res si el teclat no era al vidre i `kb:hide()` apagava el cursor. Amb un camp de només lectura això és
exactament el contrari del que ha de passar —la pàgina es llegeix i el cursor és on és la persona—, o sigui que la regla
és ara la del focus: **una caixa que té el focus té el cursor**, amb banda o sense, i el bloc s'apaga quan el focus se'n
va (o quan l'app l'oblida). `kb:hide()` amaga les lletres i no el lloc.

**`readonly` és una propietat de la caixa**, i el sistema la pregunta abans de posar cap banda: `ui_box_keyboard` contesta
que la caixa **no vol tecles** quan el camp és cert, i el camí del cursor la deixa passar igualment. Val per a les dues
caixes, la d'una línia i la de diverses. Abans, l'editor deia `box.keyboard = false` i s'emportava el bloc amb el
teclat.

**El dial s'atura a una caixa de text** (i això canvia una regla de la 0.85.2: *una caixa de text és un objectiu del dit
i no del dial*). El motiu és el que demana l'amo: per entrar-hi amb el centre primer s'hi ha de poder arribar, i una
placa amb tecles i sense tàctil no hi arribava de cap manera. El dial hi cau amb el cursor i **sense banda**; la banda és
el que compra el centre. `ui.focus(box)` també funciona des d'aquesta versió (era la limitació que la 0.113.0 va deixar
escrita a docs/30).

**El centre entra i surt de la caixa** (`editing`), i dins seu **les dues tecles del dial són el cursor**:

- una línia → una **lletra** a cada tecla, i una lletra és un punt de codi i no un byte (`textbox_caret` al nucli, i
  l'accent de «el meu fitxer» no es parteix);
- diverses línies → una **fila** a cada tecla (`textbox_multiline_row` al nucli), i la fila és la que el vidre ensenya:
  amb `wrap` una línia llarga són més files. La columna es queda tan a prop com la fila permet i a les puntes el cursor
  es queda quiet;
- `top` torna a `-1` quan el cursor es mou amb una tecla, i **l'app ho sent a `box.on_move(self, cursor)`** abans que el
  sistema pinti, perquè les seves paraules de sota la pàgina surtin a la mateixa passada.

L'aritmètica és al nucli i no a l'app pel mateix motiu que el valor d'un slider: una fila depèn de la tipografia i de la
finestra, i una app que en tingués una còpia dibuixaria el bloc on la pintada no és.

**I `framed = false` treu la vora.** El control pinta la vora en `border` i l'interior en `background`, o sigui que un
marc del color de dins no és cap marc: luabind dona el segon com a primer quan la bandera és falsa, i el paper arriba
fins a la vora. És una bandera i no un quart color perquè el que una app vol dir és «sense vora».

**L'editor, repassat**: `box.framed = false` (el document és tota la pàgina), `box.readonly = doc.readonly` en lloc de
`box.keyboard = ...`, i un `on_move` que actualitza la fila de la línia de sota sense presentar (el sistema pinta).
La pàgina de l'editor, mirada al vidre de l'emulador, ja no té cap marc i el text arriba a la vora esquerra.

**Les proves escrites**: `test_the_dial_stops_on_a_text_box_and_the_centre_enters_it` (la regla nova, amb la banda
apagada en arribar i encesa en entrar, i el botó encara abastable), `test_a_read_only_box_has_a_caret_and_never_a_band`
(les dues caixes), `test_the_keys_walk_the_rows_of_a_box_the_centre_entered` (files 4 i 8 d'un text de tres línies, i
abans del centre el cursor no es mou), `test_the_keys_walk_the_letters_of_a_line` i
`test_a_box_without_a_frame_leaves_its_edge_in_paper` (la vora del control a la tinta i la del sense vora al paper).

**Pendent**: passar la suite sencera —les cinc proves d'aquest torn i les de la 0.113.0, la 0.114.0 i la 0.115.0, que
encara no han corregut— i tocar-ho amb el dit i amb el dial a la placa, que és el que l'amo vol veure.

## 32. Un slider es toca després d'entrar-hi (0.117.0)

**El que va demanar l'amo**: *«Quan un slider rep el focus, pujar i baixar amb els botons físics significa continuar amb
el funcionament normal del focus. Certs controls s'han d'activar (botó central) per poder-los usar: per pujar o baixar el
valor del slider, perdent els botons per canviar el focus. Per controls que tinguin un comportament similar al slider. Al
rebre el focus, els botons continuarán com sempre (canviant focus) s'ha de polsar el botó central per "entrar" al slider
i llavors si escollir el valor amb els mateixos dos botons (amunt/avall). El botó central torna els botons físics al
sistema i torna a servir per moure el focus.»*

**Fins a la 0.116.0 el slider prenia les fletxes pel sol fet de tenir el focus** (`ui_step_value`), i la regla era la del
projecte del paper: un control que té un valor es deixa moure i, quan no pot més, torna la tecla al caminar. El resultat,
en una pàgina amb un slider al mig, és que **les tecles de caminar no hi eren**: les fletxes movien el número quan la
persona volia anar al control de baix.

**Ara el focus és el focus i el centre és la porta**, que és la mateixa forma que la 0.116.0 va donar a les caixes de
text:

- amb el slider enfocat, amunt i avall **continuen caminant el dial**;
- **el centre l'entra** (`editing` és cert dins), i les dues tecles són el número, de `step` en `step`, amb
  `on_change(self, value)` a cada moviment —la resta del camí del valor no canvia: `min`, `max`, `top`, els extrems que
  tornen la tecla... res d'això no s'ha tocat, només quan hi arriba;
- **el centre en surt** i les tecles tornen al sistema. L'app sent les dues presses per `on_click(self)`, o sigui que un
  slider amb un callback no perd res i a més pot llegir `self.editing` per saber per què li arriba;
- **`editing` s'apaga sol** quan el dial se'n va a un altre control: qui torna ha de dir «entrar» una altra vegada.

**La regla és una funció i no un cas** (`ui_enters_to_move`): el que ha de complir un control per entrar-hi és **no tenir
cap acció pròpia al botó central**. Avui és el slider i prou. Un **menú**, una **targeta**, un **calendari**, una
**graella** i una **llista** trien alguna cosa amb el centre: entrar-hi trauria a aquest botó el significat que ja té, i
això és un altre canvi i no aquest. Queda dit aquí perquè la llista de «controls semblants al slider» sigui una decisió
i no una endevinalla.

**La prova escrita**: `test_a_slider_takes_the_keys_only_after_the_centre` —amb el slider enfocat, avall no imprimeix
cap `value`; el centre imprimeix `click slider true`; avall dues vegades dona `value 6` i `value 7`; el centre imprimeix
`click slider false`; i avall ja no mou el número. Compilada, no executada: la suite sencera continua pendent del permís
de l'amo.

## 33. La pila que la placa va trobar i la pàgina d'imatges al dia (0.118.0)

L'amo va portar el log de la placa, i no era una petició d'interfície sinó un reinici:

```
lunok> run images
***ERROR*** A stack overflow in task loopTask has been detected
```

…després de tres fotos. I amb el log, dues preguntes: **«el carregador d'imatges usa PSRAM?»** i
**«aprofita per modernitzar l'app `images` amb els controls UI nous i les seves mecàniques»**.

**La resposta a la primera és sí i no, i el «no» era el que petava.** El fitxer i el bloc de treball sí que
venien de la memòria extra (`pal::extra_memory()`, docs/75); el que no hi era era **l'estat dels
descodificadors**, que vivia en variables locals, o sigui a la pila de la tasca de l'app. El GIF era el pitjor:
la taula del LZW (12 288 B), la cadena (4 096 B) i una fila de còpia (2 048 B) contra els **7 320 B lliures**
que la mateixa tasca diu en arrencar. Cada família té ara dues funcions —`*_state_at` i `*_state_bytes`—, el seu
estat va al bloc del cridador darrere del que la imatge ja demanava, i la fila que es dona al `RowSink` també hi
va. Mesurat amb `-fstack-usage` (docs/75 té la taula): **GIF 16 656 → 256 B, JPEG 6 432 → 912 B, PNG 2 320 →
256 B, BMP/ICO/PNM ~2 280 → ~220 B**. I `tools/check_stack.py` ho vigila, perquè aquesta mena de número no la
veu ni el compilador ni l'emulador fins que la placa reinicia.

**L'app d'imatges passava a ser de les velles**: nou botons de res, un text al mig i tres botons de
`back`/`quit`/`next` que caminaven el menú d'una en una. Ara és una pàgina de controls de debò:

- **una graella de nou cel·les** (`ui.icon_grid`) amb la marca d'una o dues lletres i l'extensió del fitxer sota:
  el dial **camina les cel·les**, la cel·la triada s'inverteix, la del dial porta l'anell i **el centre tria** amb
  `on_pick`. Els nou formats no es recorren d'un en un a cops de botó sinó com el que són, un menú de nou.
- **la caixa de sota la imatge és de només lectura i sense vora** (`readonly` i `framed = false`, 0.116.0): porta
  les xifres del format i la frase que diu per a què serveix. **El centre hi entra i les fletxes en caminen les
  files**, i el teclat no puja mai; el cursor hi és, que és el que diu que allò és un lloc on una persona és.
- **una tira de punts** (`ui.dots`, 0.112.0) diu on ets de les nou, on abans hi havia un `1 / 9` pintat amb
  `ui.text`.
- **un sol botó**, `quit`; el `back` i el `next` no hi són perquè la graella ja és el camí.

I un detall trobat amb la pàgina a la mà de l'emulador: a `scale` 3 el nom de la cel·la `bmp8` es tallava a
`bm...`. Una graella de cinc columnes dona **103 px de cel·la** i el nom va a `scale` 2, el text dens del paper:
un nom de format tallat amb tres punts no és el nom d'un format.

**Les proves escrites i compilades, sense executar**: `test_a_block_that_is_smaller_than_the_picture_needs_is_refused`
(cada família demana bloc, un byte menys és `kNoRoom`, i el número que demana és exactament el que necessita) i
`test_the_page_of_pictures_walks_its_nine_families` (l'app del disc, arrencada de debò: `9 pictures, 1 drawn, 0
failed`, tres premses de dial fins a la graella i el centre que canvia la imatge a la segona família). La suite
sencera continua pendent del permís de l'amo.

**I la suite, per primera vegada** (0.118.1, amb permís de l'amo: *«fes tots els test i trencades»*): **467 casos, 467
verds en 1 min 30 s**. Al primer intent, cinc vermelles a `test_apps`, i no eren de la prova:

- **una era un error del sistema** (i el va trobar una prova que havia escrit el torn anterior i no s'havia corregut
  mai): la imatge del manifest es pintava **abans** de `ui::attach()`, i l'attach neteja el vidre sencer quan la pàgina
  anterior hi havia pintat alguna cosa (`g_levels`). O sigui que la splash d'una app podia ser esborrada abans que l'app
  comencés. Mesurat: amb la imatge abans de l'attach el mig del vidre llegeix **15** (paper) i amb la imatge després
  llegeix **0** i **0**, els dos píxels del dibuix. La imatge va **després de l'attach** i abans del títol de la barra;
- **dues eren de l'app d'imatges**, i les va trobar la prova que la fa anar: `grid.on_pick` cridava un `draw` que era
  un `local function` **de més avall**, o sigui que la clausura tenia un global buit i triar una cel·la no dibuixava
  res; i la pàgina deixava el focus al paràgraf (el primer control enfocable que es fa) en lloc de la graella, de manera
  que la primera fletxa caminava les files del text. Ara `draw` es declara abans i la pàgina demana `ui.focus(grid)`
  després de col·locar els tres controls, i la prova prem una tecla, tria la segona família i llegeix `2 of 9` i `BMP`;
- **dues més no haurien pogut passar mai** i també eren de les proves: dues llegien píxels d'un panell que era apagat
  (el banc neix amb tot apagat i una prova que llegeix el vidre l'ha d'encendre amb `emu::Options.display`), i la de la
  caixa de text del 0.116.0 esperava que el centre **sortís** d'una caixa amb la banda a dalt, on les tres tecles són
  del teclat: ara escriu una lletra, l'app baixa la banda, el centre surt del camp i la fletxa amunt camina fins al botó.

**Les trencades, una a una i cadascuna amb la seva vermella**: el guard de focus del swipe (2 vermelles), la marca del
contacte orfe (1), una caixa de només lectura demanant la banda (3), la porta del slider (2), el bloc que rep un
descodificador (1 vermella i la tanda morint-se per l'escriptura fora del bloc), la tria de la graella d'imatges (1) i
la imatge abans de l'attach (1).

**I la placa va dir que no** (0.118.2): amb el firmware pujat, `splash boot-alt` des del prompt va contestar

```
splash: the panel did not take the picture
```

No era la imatge: era **el pintat d'abans**. El `flush` del controlador espera tres segons que la tinta del
pintat anterior s'hagi acabat (`wait_paint`, docs/50) i la neteja sencera de l'arrencada en mou més; un
`splash` dos segons després del boot arriba a mitges d'aquella tinta i rep un no. Tres coses, doncs:

- **la splash espera la tinta** (`settle`: `idle()` cada 20 ms, fins a 6 s) abans de demanar el seu pintat —ningú
  està esperant una pàgina mentre puja un logotip, i perdre el dibuix per dos segons sí que costa—;
- **el missatge porta el motiu del HAL** (`the panel did not take the picture: the previous paint did not
  finish`), perquè un no sense motiu és una tarda buscant quin dels cinc casos era;
- **el banc també diu que no** (`--ink-refuse` a l'emulador): mentre la tinta es mou, `flush` contesta fals amb el
  motiu del driver. Sense això el banc sempre espera i una prova no pot distingir el codi que espera del que té
  sort. La prova és `test_the_splash_waits_for_the_ink_of_the_page_before`: el primer dibuix deixa la pàgina
  sencera movent tinta, el banc refusa un pintat, i el segon dibuix **arriba igualment** perquè la splash ha
  esperat; traient `settle`, la crida contesta fals i el vidre es queda amb el primer dibuix.

**I la segona mitat va ser pitjor** (0.118.2, el mateix torn, amb la placa a la mà): l'amo va dir *«l'splash sembla que
bloqueja l'eink? les apps s'executen però no mostren res a pantalla»*, i tenia raó a mitges — la splash no bloqueja res,
però **la pàgina que el panell refusava es llençava**. `paint_root` buidava la llista del que esperava un pintat passés
el que passés: un `flush` refusat era una pàgina perduda per sempre, i com que la neteja de l'arrencada mou tinta més
estona que els tres segons que el driver espera, la primera pàgina de cada app hi queia. Una app que presenta **una
vegada i espera** —`run images`, mitja dotzena del disc— es quedava amb el logotip de l'arrencada al vidre: s'executa i
no es veu.

Ara **el que no ha pujat es queda a deure**:

- `paint_root` torna les regions refusades a la llista (`kept`, una còpia en el mateix ordre) i aixeca una bandera;
- **el torn del nucli** pregunta `ui::owes_paint()` —i només llavors paga un `ui::idle()`—, que és on es torna a demanar
  sense que l'app hagi de fer res;
- **`ui.idle()`** de l'app també ho fa, o sigui que una app que ja espera la tinta (`while not ui.idle() do`) veu la seva
  pàgina arribar sense saber res de tot això;
- i el pintat només es torna a demanar **quan el panell diu que la tinta s'ha acabat**, que és el que evita que cada torn
  es passi tres segons esperant dins de `wait_paint`.

La prova és `test_a_page_the_panel_refused_is_painted_when_it_can`: el vidre del banc diu que no (`--ink-refuse`) mentre
la imatge de l'arrencada mou la tinta, una app fa **un sol `present`** i s'hi queda, la consola diu
`present nil the previous paint did not finish`, el vidre encara és paper on hi ha d'haver el quadre… i després d'uns
torns **el quadre hi és**, sense que l'app torni a presentar. Trencant-la (tornar a buidar la llista) la prova es queda
sense el quadre per sempre.

**I la tercera va ser la bona, i no era ni la splash ni el canvas** (0.118.4). L'amo, amb el firmware a la placa:

```
lunok> run editor
[I][APPS] editor, stack 6960 B free
editor: File opens, saves and closes; View is read only and wrapping
(no es veu res a pantalla)
lunok> run images
images: 9 pictures, 1 drawn, 0 failed
(tampoc no es veu res a pantalla)
```

«el splash no marxa del eink», va dir, i tenia tota la raó: el que passava és que **després del logotip el vidre no
tornava a pintar mai més**. El controlador té un motor de LUT per pintat i una bandera (`LUTAFSR`, 0x1224) que diu si
encara mou tinta; el codi del fabricant **l'espera i la neteja** (una escriptura d'1), i nosaltres **només la
llegíem**. Una bandera que es va quedar aixecada deixava `idle()` dient «encara no» per sempre, `wait_paint` refusant
cada pintat al cap de tres segons, i el sistema amb un vidre que ja no es podia pintar: **el primer pintat d'un
arrencada (la splash) puja, i cap més**. Ni el canvas ni la splash segrestaven res — el panell s'havia quedat amb la
bandera a dalt.

Ara el motor té **6 segons** (`kStuckMs`, més del que dura el pintat més llarg d'aquest panell) i, passats, la bandera
es neteja i el log ho diu:

```
[W][PANEL] the ink engine kept its flag up for 6120 ms; cleared (LUTAFSR)
```

I com que una pàgina que no puja és la diferència entre una app que funciona i una app que s'executa amb el vidre
mort, **la UI també ho diu**:

```
[W][UI] the glass refused a page (0,48 540x912, gc16): the previous paint did not finish
[I][UI] the page the glass refused is up
```

Les dues línies són la mesura que faltava: si la primera surt i la segona també, el vidre s'ha curat sol i la pàgina hi
és; si la primera surt i la segona no, el panell no torna mai a dir que està lliure i el problema és del driver i no
del sistema.

**La quarta l'ha acabada de decidir el log** (0.118.5). Amb la 0.118.4 a la placa, `run images` va contestar això:

```
[W][PANEL] the ink engine kept its flag up for 9560 ms; cleared (LUTAFSR)
[W][UI] the glass refused a page (12,486 516x120, du): the previous paint did not finish
[W][PANEL] the ink engine kept its flag up for 6001 ms; cleared (LUTAFSR)
[W][UI] the glass refused a page (12,876 516x72, du): the previous paint did notfinish
images: JPEG 96x72 (1988 bytes) at 492x369, 1 of 9
[W][PANEL] the ink engine kept its flag up for 6003 ms; cleared (LUTAFSR)
[I][UI] the page the glass refused is up
images: 9 pictures, 1 drawn, 0 failed
```

Tres coses i una que no diu: **el vidre torna a pintar** (la pàgina puja, o sigui que el sistema de la 0.118.2 era
correcte i el culpable era el driver), **la bandera torna a estar a dalt després de cada pintat** (és un latch que el
maquinari no baixa), i **amb el pedaç de la 0.118.4 cada pàgina costa sis segons d'espera**, que no és manera de fer
servir una placa. El que fa el codi del fabricant és **contestar la bandera** (escriptura d'1) **abans** de pintar, i no
llegir-la després: això fa el driver ara, i el que espera són les dues coses que no menteixen —la **línia BUSY** del
panell i **el temps de tinta del mode** de la taula del fabricant (120/290/350/450 ms, docs/95)—. La línia
`the ink engine kept its flag up` desapareix amb el pedaç que la necessitava.

**I la cinquena va ser la del log** (0.118.7). Amb la 0.118.6 a la placa, cada pintat va dir què enviava, i el patró era
exacte: **començava a la columna 0 del vidre i pujava; començava a qualsevol altra columna i no pujava res**, amb els
números de dins del rectangle correctes i el controlador acceptant la comanda.

| Pinta | Cantonada nativa | Al vidre |
|---|---|---|
| la splash | `0,0 960x540` | sí |
| la barra a l'arrencada | `0,0 48x540` | sí |
| la pàgina d'`images` | `48,0 912x540` | **res** |
| els quatre controls d'`images` | `56,12`, `484,12`, `616,12`, `876,12` | **res** |
| el rellotge de la barra | `4,104`, `4,440` | **res** |

Un rectangle amb `x ≠ 0` no cobreix la fila sencera de la imatge del controlador, i aquest no la camina: carrega una
imatge que no és la que ensenya i al vidre hi torna la pàgina d'abans —la splash, i per això «el splash no marxa del
eink»—. El driver **carrega i pinta des de la columna 0** i, si el pintat neteja (`gc16` i família) i ja cobria mig
panell o més, neteja **tot el vidre**, que és el que una pàgina nova necessita i el que la splash feia des del primer dia
sense saber-ho.

## 34. Com pinta una tecla la calculadora del paper (0.158.0)

L'amo va demanar com fa el botó de retrocés, el de canviar de signe, el de dividir i el de multiplicar la
calculadora del projecte vell, i que el de lunokOS faci el mateix. El codi és
`OLD/PaperOS/data/app/Calculator/pos_launch.lua` i la captura del resultat és
`OLD/PaperOS/captures/Calculator - the keypad.png`.

**El dibuix d'una tecla són quatre línies, i són les que ja té el port:**

```lua
if isOperator(k.action) or k.action == "=" then
    draw.rect(k.x, k.y, k.w, k.h, 12)          -- el quart columna va sobre gris 12
end
draw.frame(k.x, k.y, k.w, k.h)                  -- la vora d'un píxel, quadrada
local lw = draw.text_width(k.label, "title")
local lh = draw.text_height(k.label, 100000, "title")
draw.text(math.floor(k.x + (k.w - lw) / 2),
          math.floor(k.y + (k.h - lh) / 2), k.label, "title")
```

O sigui que la tecla **no és cap control**: és un rectangle amb gris, una vora quadrada d'un píxel i una
etiqueta centrada a la cara `title`, i el seu propi capçalera diu per què —«the controls of an app are a FIXED
POOL and a script has exactly **EIGHT** buttons (`MaxButtons`, a `src/system/lua_ui.h`). A pocket calculator
asks for **NINETEEN**»—. `lDrawRect`/`lDrawFrame` de `src/system/lua_api.cpp` són `fillRect` i `drawRect` del
canvas i no hi ha cap radi: l'API té `draw.round_rect`/`draw.round_frame` (els fa servir `Paint` i `Files`),
però **la calculadora no en fa servir cap**, i la captura ho ensenya: arestes rectes. El port ja pinta el
mateix (`ui.rect` gris 12, `ui.frame`, `ui.text` centrat amb `kTitle`), i l'única cosa que hi afegeix —un
`ui.rect` blanc dins de la caixa del display— **fa falta aquí** perquè aquesta app repinta només la caixa del
número a cada tecla i el canvas no es torna a esclarir (docs/50).

**El que sí que és diferent és l'etiqueta, i té dos motius que van junts:**

1. **El signe no és una paraula, és un glif.** `Rows` de l'original és `{"←", "back"}, {"±", "neg"},
   {"÷", "/"}` i `{"×", "*"}`, i el seu comentari diu d'on surten: «the glyphs are the ones the bitmap font
   carries (`tools/fontgen.py`): the arrows, the plus-minus, the multiplication and the division signs are all
   there». Aquella eina còïa **216 codis**, i la llista inclou `0x2190`–`0x2193` (les fletxes), `0x00B1`,
   `0x00D7` i `0x00F7`.
2. **Per què allà es podia escriure `"←"` i aquí no.** El text del paper és **UTF-8** i les seves cares
   s'indexen **per punt de codi** (`src/gfx/text.cpp`, `utf8Decode` camina la cadena abans de buscar el glif),
   o sigui que un `"←"` de tres bytes del guió és un glif de `U+2190`. El text d'aquí **és d'un byte per
   caràcter** (Latin-1, i la franja `0x80`–`0x9F` amb els caràcters de la 1252, docs/30): un `U+2190` no hi cap.
   Dels quatre signes, tres són Latin-1 (`÷` `0xF7`, `×` `0xD7`, `±` `0xB1`) i la fletxa viu a **`0x81`**, un
dels cinc codis que la 1252 deixa buits.

**El que s'ha fet, doncs**, és el que demanava l'amo: la mateixa taula de tecles, amb els mateixos quatre
signes, i la cara del sistema ensenyada a portar-los (`tools/ttf_latin1.py --set signs`: **104 glifs, 9 416 B**,
+868 B de disc i de RAM). La línia del display també torna a ser la de l'original, `Symbols = {["*"] = "×",
["/"] = "÷"}`.

**I tres tecles tenen un gris propi** (0.158.0, demanat el mateix dia): `C`, la fletxa i `=` es pinten amb
**gris 8** i **lletra blanca** (nivell 15), i les quatre operacions conserven el **gris 12** de l'original, o
sigui que la pàgina diu «aquestes tres canvien l'estat i aquelles quatre són l'aritmètica» —la `±` es queda en
paper, com al vell—. Els dos números són `kOperatorGrey` i `kActionGrey` de `apps/calculator/calculator.lua`,
un lloc cada un (`key_grey`), i la lletra blanca surt del mateix test.

**El que encara no és igual del tot, i és una limitació de debò**: la **mida**. El vell tenia quatre cares de mapa
de bits fetes a 33, 43, 57 i 82 px d'alçada de línia; aquí la cara és d'outlines i el seu pas és d'11 px, o sigui
que les quatre mides surten **33, 44, 55 i 77** — la del número gran, cinc píxels curta— (docs/30). Fora d'això,
les dues calculadores són la mateixa imatge: mateixa graella, mateixos marges (16 px de marge, 10 de separació),
mateixa caixa del display, mateix gris i mateixa vora.
