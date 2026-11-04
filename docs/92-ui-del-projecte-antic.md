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
retallat a l'ASCII imprimible són **8 548 B** —la meitat dels 15 878 B d'un sol cos vell, i serveix per a
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
| **la vista que el teclat no trepitja** (0.84.0) — `ui.caps().height` baixa a 736 amb la banda i torna a 912 quan marxa, l'app rep `on_resize`, i `cv:resize` deixa un panel prendre la mida nova | **0.84.0** | `test_the_keyboard_takes_its_band_out_of_the_view_the_app_is_given` |
| **la caixa de text, neta** (0.84.1) — el text de la finestra s'acumulava entre pintades (un buffer estàtic que no s'esborrava), el cursor a zero es pintava al final de la fila, i les apps presenten el canvi amb `gl16` | **0.84.1** | `test_a_box_paints_its_text_and_not_the_one_before_it`, `test_the_caret_of_a_box_of_lines_is_at_the_cursor_and_not_at_the_end_of_the_row` |
| **el teclat, dibuixat de debò** (0.85.0) — DejaVu Sans Bold de `/fonts` a doble mida i rebaixat a quatre nivells de tinta, `MAY` com a bloqueig, i la banda que només es repinta quan canvia ella o quan la pàgina la trepitja | **0.85.0** | `test_the_keyboard_is_a_qwerty_the_finger_and_the_glass_agree_on` |
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


