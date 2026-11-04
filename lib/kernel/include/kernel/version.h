#ifndef LUNOK_KERNEL_VERSION_H
#define LUNOK_KERNEL_VERSION_H

namespace kernel {

// The version of the system, and the rule it follows (docs/80, law 10 of the README):
//
//   - **X** when something is rewritten from the ground up, of the kind that makes an app that ran
//     have to be looked at again;
//   - **Y** when something **new** appears that a person or an app can use —a call of Lua, a command
//     of the prompt, a piece of hardware driven—;
//   - **Z** for everything else: a fix, a number that was measured, a document that said something
//     untrue.
//
// Every change bumps one of the three, and the number goes up in the same commit as the change. Apps
// do not declare against it; docs/20 says what happens when the API has to change.
//
// 0.3.0 (31/10): http.get, a page with TLS, and the radio that is switched on for
// work and switched off when the queue is empty.
// 0.4.0 (31/10): both sleeps, the wake by timer and by button, and the app that
// comes back where it was (docs/90, M6).
// 0.5.0 (31/10): power.cpu, the processor from 80 to 240 MHz.
// 0.6.0 (31/10): the light of the board and the button, with on_button and the
// three taps that close an app (docs/90, M7).
// 0.7.0 (31/10): the power off, the FRAM as `/fram`, the PSRAM for any app, the
// room of the RTC memory and of the FRAM in `mounts`, the serial port at the speed of
// the board, `run <id> [path]` and the dump app (docs/50, docs/70, docs/75, docs/85).
// 0.7.1 (31/10): the extra memory is the PSRAM the chip maps and not the chip's
// datasheet, and the numbers the Paper gave are in docs/75.
// 0.8.0 (31/10): the `heap` app, which asks for a megabyte and gives it back, so the
// extra memory of a board can be watched working (docs/95).
// 0.8.1 (31/10): the extra memory says three numbers —what the chip has, what the CPU
// can address and what is left— because 8 MB on the datasheet against a pool of 4 MB
// read as a bug (docs/75, docs/90).
// 0.8.2 (31/10): a pass over every document against the code, with the code winning: the
// error table, app.info, fs.mounts (which stopped the list at eight of the ten rows), the
// seed of math.random, the RTC chip of the Paper, the keys and lights of the bench, the
// memories of the emulator and three numbers that did not add up (docs/30...docs/95).
// 0.9.0 (31/10): the light has effects. `led.play` runs breathe, blink, sos and bounce on the
// turn clock —no thread, no queue of frames it owes— with a colour ramp, `led.stop` hands it back
// at once or with a fade, and the system has four moments of its own (boot, radio, alarm, error)
// that an app may redefine with `led.system`. An alarm or a failure **interrupts** the app: its
// light is kept, given back as it was, and `on_light()` tells it to redraw (docs/30, docs/50).
// 0.10.0 (1/11): the air around the board. `ISensor` in the HAL, the SHT30 of the Paper with its two
// CRCs, `env.read()` for an app, and the capability announced only when a whole measurement arrived
// (docs/30, docs/50, docs/85).
// 0.10.1 (1/11): the bus of the Paper lets a chip hold the clock. The driver of this IDF allows ten
// bus cycles —100 µs at 100 kHz— and the SHT30 needs six milliseconds, so the sensor was announced as
// absent (`i2c: read of 44 failed (ESP_ERR_TIMEOUT)`); the ceiling is now the register's maximum, 13.1 ms
// (docs/50, docs/95).
// 0.11.0 (1/11): the glass of the Paper, up to the HAL. `ITouch` (five fingers, a frame, a size and a
// track id), the GT911 driver on both of its addresses, and one line at boot saying which one answered,
// in which frame and with how many fingers. **No capability and no call of Lua yet**: a touch means
// nothing until there is something drawn under it (docs/50, docs/90).
// 0.12.0 (1/11): the panel of the Paper, up to an app. `ISurface` in the HAL, the IT8951 driver on the
// bus of the card —its handshake, its image in PSRAM, the rectangle that changed and a paint that is
// asked for and not waited on— and `display` and `touch` in Lua with a gesture for every thing a finger
// can do. **The system draws nothing of its own**: an app decides what, how and when (docs/30, docs/50).
// 0.13.0 (1/11): the page on the glass, and a log without a lie. The image of the panel goes out with
// **one data preamble per frame** and then the bytes of the surface (a preamble per word needs a CS edge
// around each one, and without it the page came out sheared), the registers of the controller carry the
// maker's addresses (`LISAR` 0x0208, `LUTAFSR` 0x1224) and the I2C bus can be **asked** whether a chip is
// there, so the touch probe no longer writes an error about the address it is not at (docs/50, docs/95).
// 0.14.0 (1/11): **the view of an app**, and the board's buffer back where it belongs. An app draws on
// its own canvas (`ui.clear`, `ui.rect`, `ui.pixel`), which the system makes when the app starts and
// re-makes —white— when the size changes, telling the app `on_resize`; `ui.present` is what composes
// the rectangle that changed onto the system's surface and asks for the paint. The `display` table is
// gone: no app can reach the glass directly any more (docs/30, docs/50).
// 0.15.0 (1/11): **the damage**, and the quality of each piece of it. Every draw records its region and
// the quality it wants, the system merges what it can (up to eight regions, and past that one `gc16`),
// and a present paints **one region per quality** after translating it to where the view sits on the
// glass. The view also has a place now (`ui.caps().x`/`.y`), because a canvas is not the panel, and the
// driver waits for a paint to end before starting the next (docs/30, docs/50).
// 0.16.0 (1/11): **the bar of the system**, in C++, on the top 28 rows of the glass: the hour on the
// left, the title of the app in front in the middle and the charge on the right, with a 5×7 font of its
// own. The view of an app is the glass under it, and it paints itself only when the minute, the title or
// the charge change —`du` for a digit and `gc16` when the whole bar has to look clean— (docs/30, docs/50).
// 0.16.1 (1/11): the bar that **does not paint without a change**. The turn of a bar with nothing new is
// two comparisons: no read of the clock, no read of the gauge and no paint. The values are read once and
// carried to the paint —reading them again inside it made the bar repaint itself every half minute with
// nothing changed— and the charge has a dead band of two points against the noise of the ADC (docs/30).
// 0.17.0 (1/11): **one orientation for the glass and for the fingers.** The chip counts in 540x960 and
// the glass is 960x540; the system keeps the quarter turn between them in one value
// (`display::rotation()`, 90 by default) and the touch turns every frame into the app's own frame when
// it is read —dropping a finger that lands on the bar— so an app compares a finger with what it drew
// without turning anything itself (docs/50, docs/90).
// 0.18.0 (1/11): **the page turns in software, and it is a portrait one by default.** The driver keeps the
// image in the frame an app draws in (540x960) and gathers every row of the glass from it when something
// changes, so the controller only ever sees its own frame. `ui.rotation([degrees])` turns the glass, the
// bar and the fingers with one value, and every app is told with `on_resize` (docs/30, docs/50).
// 0.18.1 (1/11): **the finger arrives where it was put.** The chip counts in the page's own frame —a tap
// on the clock, the top left corner, arrives as raw 20,31— so there is no quarter turn between the fingers
// and the glass, and the page of today needs no turn at all (`kChipPage` is 270). The turn also read its
// own output when a caller chained two of them into one frame (a tap on the clock came out at 539,900), and
// no point is dropped any more for not fitting the frame this code expects: a filter that fails leaves the
// app without a touch, which is what the board showed (docs/50, docs/95 E-6).
// 0.18.2 (1/11): **the four corners, and the instrument put away.** The chip speaks in the page's own frame
// and the same way round —a touch on each corner of the portrait page said 46,28 · 515,49 · 31,926 ·
// 500,920, so `x` grows to the right and `y` downwards— and no turn or mirror is left to guess. The line
// that printed the first frame of every touch is gone: it was the instrument that found this, and one line
// per touch is not a console a system can be used with (docs/50, docs/95 E-6).
// 0.18.3 (1/11): **the four holes the native suite found, the first time it was run.** A rectangle that
// starts or ends at an odd pixel painted its neighbour —the text of the bar got one column fatter per
// letter— in the canvas, in the panel driver and in the bench; the damage list that runs full threw the
// last region away instead of growing to hold it (eight pieces at the top and one at the bottom came out
// as 430 rows); the bench's glass recorded the last `blit` instead of the rectangle that changed, so the
// bar's paint carried another copy's region; and `orientation::turn` read its own output when a caller
// chained a turn into the same point. 230 cases in 10 suites, green (docs/30, docs/50, docs/85).
// 0.19.0 (1/11): **the canvas of an app, which is the first piece of the control layer.** `ui.canvas(w, h)`
// makes a buffer with a place of its own inside the view, canvases go inside canvases, and a `ui.present`
// composes the whole tree —depth first, in the order they were made— onto the glass. A canvas is cut at
// its border and **a parent only copies what a child painted since the last composition**, so a text over
// a picture needs no mask and a control that painted nothing costs nothing. Eight canvases at once, and a
// view made again takes them with it: an object that outlives it answers `E_INVAL` instead of writing
// into the canvas that took its slot. `font::draw` grew the `Clip` a label will be made of (docs/30,
// docs/90), and the app `layers` shows it.
//
// Running it said three things that were not so: the bench **never announced `display`**, so an app that
// asked `sys.caps().display` was told no on a bench whose glass was there —and the view behind it was
// built anyway—; the console of the bench and the kernel's `printf` were two streams, so the boot lines
// came out after the prompt; and the prompt was written **before** the hardware boot, so every line the
// board says about itself looked like an answer to something nobody had typed. The first is the
// capability that was missing, the other two are one order (docs/85).
//
// And the **primitives of a canvas**: `rect` (filled) and `frame` (the outside), `line` —a rectangle when
// it is straight and Bresenham when it is not—, `circle` and `disc` with the midpoint walk, and `text`
// with the font of the system at a size and a colour chosen by the app. Each one leaves **one** region of
// damage, the box it covers, because a diagonal line that looked like two hundred separate draws would
// fill the list of eight and turn the whole canvas into one `gc16` paint (docs/30, docs/50).
//
// And the tree: **one father and ordered children** (`cv:add`), so a control holds a control and a place
// is read in the pixels of the canvas that holds it; the children are composed in the order they were
// added, which is the order of the layers. **Drawing a control is asking its children**: `cv:draw()`
// calls the control's own `on_draw(self)` and then each child's `draw()`, and an object of the app can
// carry fields of its own —the callback and the children live beside it, not in a second table to keep in
// step— (docs/30, docs/90).
//
// And **a control with nothing changed is not drawn**: the flag is set when it is made, cleared when it
// has been drawn, and set again when it moves, when it changes father, when it is given a new `on_draw` or
// when the app says `cv:invalidate()`; it goes **up to the fathers**, so a child that moved makes its
// father paint over what it left behind, which is the hole that was written down as open. `ui.present()`
// draws what changed and composes it, and the view is a control too (`ui.root()`), so an app that presents
// on every turn pays only for what it touched.
//
// And **a redraw paints only what it owes**: while a control draws itself the system keeps a box open —the
// rectangle it must repaint— and every primitive of that canvas is cut at it, at the quality the drawing
// asks for. The signal does **not** go up to the fathers: a control whose content changed is composed over
// its father again, and the father repaints only when a child **leaves a hole** —it moved, or it went
// away— and then only the rectangle the child was occupying (docs/30, docs/90).
//
// And **the frame is not the view**: a control may have no buffer at all (`ui.control`), and then it paints
// into the first canvas that holds it, cut at its own frame, and its children are composed through it with
// every place added. **Two ceilings instead of one** —32 controls, 8 of them with a view— which is the
// difference between a page with twenty labels and a page with eight, and the reason a `UILabel` will need
// no buffer (docs/30, docs/90).
//
// And **a control that goes away is cleaned before it is freed**: `release` marks it and its children, tells
// the control that paints which rectangle is left, and the buffer is wiped white and the slot given back at
// the end of the next `present`, when that part of the page has already been painted over. A ghost on the
// paper and a stale block handed to the next canvas are the same bug seen from two sides (docs/30, docs/90).
// **And the rectangle is the father's, one for the whole subtree**: three children inside a panel that goes
// away are one region, not four, because theirs are all inside his —a list of eight that fills with pieces
// of the same hole ends up in one `gc16` of the union, which is the opposite of cheap. **What has been
// painted is paid**: the box of the view is spent when the paint happens, because a box that outlived its
// paint came back inside the next reason and repainted a part of the page that was already clean (found by
// this suite, 1/11). And **an object that points at a control that is gone** is not a dangling pointer:
// the object carries the slot and the generation, every call checks both, and an object whose control is
// no longer there answers `nil, E_INVAL` —`canvas (gone)` from `tostring`— while a new canvas in that slot
// can never be written through it, because the generation of that slot has moved on (docs/30, docs/90).
// 0.19.1 (1/11): **el dia a la placa, escrit**. La capa de controls diu el mateix al banc i a la Paper
// —`run layers` dona les sis línies iguals, els comptes `1 / 1 / 2` inclosos—, i els números que falten
// són al seu lloc: `ui: view 540x932 at 0,28, 251640 bytes of canvas`, el tàctil amb dos punts i el gest
// de prémer llarg amb 1002 ms de debò. I dues coses que el run ha destapat i que la documentació deia al
// revés: el `~120 ms` que imprimeix `run panel` és **l'espera de l'app** i no el temps de la tinta (el
// `gc16` continua sense número), i la pàgina de prova es dibuixava amb `du4`, que aquí mateix està
// mesurat com a **no pintar** —ara va amb `du` i `gc16`, i a la placa la pàgina hi és sencera i neta, o
// sigui que qui no pintava era el mode i no el camí— (docs/95, E-6, E-7 i E-8; docs/75; docs/85;
// docs/90). El que continua **sense número** és el temps d'un `gc16`: la mesura de l'app no el pot donar
// (espera 120 ms i pregunta, i el bucle surt a la primera).
// 0.19.2 (1/11): **`ui.idle()` no diu la veritat, i ara sí**. El controlador del panell no es marca ocupat
// en el mateix instant que rep la comanda de pintar, i una pregunta feta un mil·lisegon després llegia zero
// amb la pàgina encara per començar: `run panel` treia un `0 ms` que no era cap mesura, i un `present` de
// dues qualitats podia enviar el segon pintat a sobre d'un primer que no havia començat. El conductor
// guarda ara quan es va demanar el pintat i no contesta que sí fins passats `kEngineStartMs` (20 ms), una
// **guarda** i no una mesura, i la mateixa pregunta la fan `idle()` i l'espera del pintat anterior. I
// `run panel` compta des d'abans del `present` i pregunta cada 10 ms en lloc de dormir 120 ms i restar-ho:
// el número que ara imprimeix és el del vidre (docs/30, docs/50, docs/95 E-7).
// 0.19.3 (1/11): **el primer número de debò del vidre**. `run panel` a la placa amb la 0.19.2 diu
// `the ink took 1985 ms` per a una pàgina de dues regions (`du` i `gc16`), i aquest número porta a dins
// l'enviament de la pàgina sencera per SPI i els dos pintats: és **tres o quatre vegades** el que voldrien
// dir els 450 ms del fabricant per a un `gc16`, i no se sap encara quina de les tres parts paga. La
// mesura que ho separa és `squares`, que pinta una sola qualitat per `present` i ara imprimeix
// `painted with du in N ms` i `erased with gc16 in N ms` d'un quadrat de 40 × 40, on la transferència és
// 800 B i el número és el motor (docs/95, E-7).
// 0.20.0 (1/11): **el sistema diu el que costa, i el log té una sola forma**. Cada porta d'un
// dispositiu —el sistema de fitxers per portes, la vista amb `flush` i la pregunta d'`idle()`, la lectura
// del tàctil— té un comptador amb **quantes vegades, quant de temps i quan va ser l'última**, i `stats`
// les ensenya en una taula que es pot posar a zero amb `stats reset`. **Cada crida d'una app a l'API es
// compta i es cronometra** amb el ganxo del propi intèrpret —un ganxo i cap tanc per funció, perquè una
// app de l'ATOM té 64 KB i els tancs se'n menjarien cinc— i la fila `app <id>` dona el que aquella app ha
// costat des que va arrencar. I el log: **`[nivell][etiqueta] missatge`**, amb la lletra de nivell i
// l'etiqueta posades pel format i no a mà dins del text, els trenta-vuit `printf` dels conductors
// convertits a `hal::logf` amb un *sink* que el nucli instal·la, i el mirall a la consola encès mentre el
// sistema arrenca. I **el temps d'una crida es compta fins que es resol**: una crida que torna una
// promesa no ha acabat quan torna, i `sys.delay(50)` costa cinquanta mil·lisegons a `lua.call` i no els
// vuit microsegons de la trucada. El repàs de cada porta del HAL —el que es compta i el que no, amb el
// per què— és a `docs/50`. **I un error que va sortir al banc**: la memòria d'un *userdata* de Lua no ve
// a zero, i el camp del comptador d'una promesa hi portava escombraries; una promesa sense temps hi
// va posar **una hora i mitja** des de cinc crides (`lua.call 4988744 ms`, mesurat a la placa l'1/11).
// El camp s'inicia en fer la promesa i el tancament refusa un començament que no ha passat mai. I **cada
// temps de `stats` s'escriu en l'escala que es llegeix** (`845 us`, `12.3 ms`, `1.98 s`, `3 min 07 s`,
// `1 h 23 min`), perquè `4988744 ms` no diu res i `1 h 23 min` sí. **I el repàs de debò, que va destapar
// tres forats**: el sistema de fitxers té ara **una fila per unitat i porta** (`flash.read`, `sd.read`,
// `nvs.write`, `rtc.stat`…) perquè una targeta no és una flash; `clock.mono` hi és perquè el comptador
// monòton **és una lectura del xip RTC** i el botó, la llum i la ràdio el demanen cada torn (abans
// `clock.now` deia una crida en dos minuts); i la vista té dues files, `glass.flush` i `glass.gc16`,
// perquè una pintada que neteja no és una pintada qualsevol. **I la suma amb el repartiment**: la darrera
// línia de `stats` diu el **temps de sistema** (el que sumen les files) contra el **temps d'usuari** (la
// resta de la vida del sistema, comptada des de l'última arrencada o l'últim `stats reset`), que és el
// que separa «l'app és lenta» de «el dispositiu és lent». **I `stats` no és una línia de log**: no porta
// nivell ni etiqueta, perquè ningú no fa `grep` d'una taula —es llegeix— i amb les etiquetes a sobre les
// columnes no s'alineaven; ara és una taula de columnes fixes. **I el compte complet**: `kernel.loop` (el
// torn del nucli), `lua.run`/`lua.own` (el torn de l'app i el que és codi seu), `psram.alloc`/`psram.free`
// (la memòria extra és un dispositiu d'aquesta placa), i **el mínim i el màxim** de cada fila a més de la
// mitjana. **I el `boot` era un comptador mort** —hi havia l'identificador i ningú no hi apuntava res, o
// sigui que la fila no hauria sortit mai— i ara compta el que el sistema triga a venir: 108 ms al banc.
// I els medidors que **contenen** d'altres (`boot`, `kernel.loop`, `lua.run`, `lua.own`, `lua.call`) no
// entren a la suma de portes, o el mateix microsegon es comptava tres vegades i el total passava del cent
// per cent (138 %, mesurat). Costa **1 208 B de RAM i 9 076 B de flash** al
// Paper (docs/30, docs/50, docs/75, docs/80, docs/85).
// 0.21.0 (1/11): **el vidre del banc en una finestra**. `--gui` obre una finestra X11 amb el buffer del
// `MemorySurface` dins, i la pinta cada cop que el panell es pinta —el comptador de pintades és la
// pregunta, o sigui que un torn sense pintada no costa res— amb els setze nivells en grisos i la pàgina
// com la dibuixa l'app (540x960, no el vidre ajagut). Porta `--display` amb ell, diu que no si no hi ha
// `DISPLAY`, i tancar-la acaba el run com tres ESC. És X11 i un `-lX11` darrere de `-DLUNOK_GUI_X11`;
// sense Xlib el banc compila igual i diu que no té finestra. **La finestra no és el panell**: no hi ha
// tinta, ni fantasma, ni parpelleig, i la conversió de quatre bits a píxel —la part que es pot provar
// sense pantalla— té prova a `test_ui` (docs/85).
// 0.22.0 (1/11): **el vidre del banc mou tinta**. Un panell d'e-ink no encén píxels: una pintada de `du`
// deixa un quart del que hi havia, la brutícia s'acumula si res no neteja, una de cada mil la falla un
// nivell, i una pintada dura el que dura la qualitat (450 ms un `gc16`, 120 un `du`), o sigui que
// `idle()` al banc diu la veritat i `run panel` hi dona 480 ms en lloc de 1. El paper és calent
// (`#EFEADC`) i la tinta no és negra del tot (`#1A1B18`), perquè una pàgina llegida en un monocrom
// perfecte és una pàgina que es veu millor que la que tindrà la persona a les mans. Tot el model és una
// taula (`emu_ink.cpp`) i `--no-ink` el treu; les proves corren amb ell fora i n'hi ha dues que el
// proven —el rastre d'un `du`, la neteja d'un `gc16`, i el temps de cada qualitat— (docs/50, docs/85).
// 0.23.0 (1/11): **el banc paga el bus i dibuixa el parpelleig**. Una pintada val ara el que val a la
// placa: la regió que canvia s'envia abans de demanar el pintat (`ink_transfer_us`) a 0,8 µs el byte —el
// bus del driver, 10 MHz— **més 1 100 µs per fila**, que és l'únic número d'aquest model que surt d'una
// mesura: la pàgina de `run panel` va costar 1 985 ms a la placa i els seus comptes deixen això per fila,
// i el camí sencer al banc dona 1 970 ms (docs/95, E-7). L'espera és dins del `flush`, com a la placa: el
// bus atura el nucli i el banc fa el mateix amb un `usleep`, i una pintada espera la tinta de l'anterior
// (`wait_paint`). I **`a2` parpelleja**: blanc, després negre i després la imatge (`step_ink`), que és el
// que diu el full del fabricant i no el que va fer aquesta unitat, on `a2` no va pintar res (docs/95,
// E-7). `--no-ink` segueix essent el vidre de les proves: tampoc hi ha res a enviar (docs/50, docs/85).
// 0.24.0 (1/11): **el vidre del banc té dits**. `--touch` dona al model de la Paper el seu GT911
// (`emu::Gt911Panel`): el marc de la pàgina —un dit a (100,200) és a (100,172) de la vista—, els **dos**
// dits de la seva configuració, la mida del contacte que va donar la placa (28 i 30) i el que costa
// llegir-lo: **800 µs** sense dit, **1 830** amb un i **2 550** amb dos, tres transaccions a 100 kHz i nou
// bits per byte, més els ~17 ms del probe de 186 bytes. **I el problema de debò**: el xip escombra sol i
// dona el marc **una vegada**, o sigui que un lector més ràpid que el vidre llegeix «ningú» amb un dit a
// sobre; `--touch-scan <ms>` és com es reprodueix (40 ms contra un torn de 20) i `Gt911Panel::missed()` ho
// compta. Al banc no hi ha bus, i això també es diu (docs/50, docs/85, docs/95).
// 0.25.0 (1/11): **un commutador per a tots els temps del banc**. `Options::delays` —que neix **fals**, que
// és com sempre han corregut les proves, i que el binari encén— decideix si una lectura del disc, un byte
// del port, el bus i la tinta d'una pintada i les tres transaccions del tàctil es paguen o no. El model
// no canvia: el fantasma, la brutícia i el marc d'una vegada hi són igual. `--no-delay` el deixa apagat, i
// `--gui` **l'exigeix**: un run amb finestra i sense esperes, o amb un vidre que canvia píxels
// (`--no-ink`), es refusa, perquè és una foto d'una placa que no existeix. Mesurat: `run panel` 2 899 ms
// amb els temps i 24 ms sense, i el run sencer 3,11 s contra 0,03 s (docs/85).
// 0.26.0 (1/11): **el disc costa el que costa cada dispositiu, i la PSRAM no és gratis**. S'ha acabat el
// "1 ms per lectura" per a tot: cada volum passa pel seu temps (`emu/disk.h`) — la flash a 40 MHz, blocs
// de 4 KB (819 µs de cable), pàgines de 256 B (0,4 ms) i **sectors de 45 ms que s'han d'esborrar abans
// d'escriure**; la targeta a 10 MHz, sectors de 512 B (410 µs) i una comanda de 200; i la FRAM, que és I2C
// a 100 kHz i on **un byte són 90 µs**. La NVS és una partició de la flash i paga el mateix. I el bloc de
// la PSRAM que demana el nucli costa el recorregut de l'assignador (20 µs): els bytes són temps de CPU i
// aquest banc no té rellotge de CPU, i es diu. `--read-delay` desapareix perquè el rellotge pla era part
// del que s'ha arreglat (docs/50, docs/85).
// 0.27.0 (1/11): **la bateria d'on corre el banc, prestada a la placa**. La cel·la del Paper és de
// **1150 mAh** i es llegeix com a mil·livolts darrere d'un divisor a GPIO35; la màquina del banc en diu un
// percentatge. `--battery-host` agafa aquest percentatge i el passa per **la corba de la cel·la del model**
// (`hal::battery_mv`, la inversa de `battery_percent`), o sigui que 63 % del portàtil són 3 847 mV del
// Paper i no els 11 V de la seva bateria. I es paga el que costa preguntar-ho: **quatre conversions d'ADC
// a ~100 µs i ~1 ms de caracterització la primera vegada**. Amb `--no-delay`, res (docs/50, docs/85).
// 0.28.0 (1/11): **tres apps que proven el banc i la placa amb la mateixa pàgina**. `ghost` pinta vuit
// passades de `du` sobre el mateix bloc i les neteja amb un `gc16` —el fantasma, amb el temps de cada
// pintada—; `draw` fa un traç que segueix el dit i té un quadre que neteja la pàgina, i diu on cau cada
// punt; i `cost` mesura a on corre el que costa un `stat`, una llista, un llegir, un escriure, una
// pintada de cada qualitat, el gauge, el tàctil i la memòria, per posar les dues taules al costat
// (docs/85).
// 0.28.1 (1/11): **el fantasma es veu**. El model de la tinta deixava un quart i, sobretot, la brutícia
// podia arribar a quinze setzens i congelar la pàgina, o sigui que el fantasma s'esvaïa en dues passades
// i el d'una pàgina de setze nivells no es distingia del d'una de dues. Ara el `du` deixa **cinc setzens**
// (un `du4`, set), la brutícia suma fins a un sostre d'onze —la imatge enganxada, que és el que fa el
// vidre i no un píxel que es para— i el gra és **vuit de cada mil** amb una fallada de dos nivells una
// vegada de cada quatre. La prova que ho mesura: un esglaó de setze nivells repintat amb `du` deixa cinc
// nivells de recorregut a les bandes, i la mateixa pintada sobre una pàgina plana en deixa zero.
// 0.29.0 (1/11): **el codi de l'app també costa**. El banc executava les instruccions de Lua a la
// velocitat d'un PC, o sigui que un bucle que al vidre dura un segon aquí durava un mil·lisegon i cap
// número de `lua.own` volia dir res. El ganxo de l'intèrpret en compta **dos mil** i en paga el preu quan
// la suma passa de mig mil·lisegon, i el que queda es paga al final del torn (`--lua-cpu`, **60 ns a
// 160 MHz**, dividit per la freqüència simulada). La banda diu quant costa una instrucció —zero en una
// placa, on el temps és el de debò— i el banc és qui en diu seixanta (docs/85).
// 0.30.0 (1/11): **la bateria del banc ja no s'ha de demanar**. Un model amb indicador contesta
// `power.battery()` a la primera, perquè el `%` d'aquesta màquina es llegeix sol a l'arrencada i passa per
// la corba de la cel·la del Paper; una màquina sense bateria deixa la cel·la al **seu càrrec nominal
// (3 700 mV, 42 %)**, ho diu, i `--battery-mv` mana sobre el préstec. Abans d'això `run battery` al Paper
// deia «this board does not know its battery», que és el que diu la placa només quan no en té (docs/50,
// docs/85).
// 0.31.0 (1/11): **el maquinari del model hi és des de l'arrencada, i no s'ha de demanar**. El vidre i el
// GT911 eren opt-in, i `run draw` en una Paper del banc deia «this board needs a touch panel and a glass
// for this»: una placa que algú té a les mans no té aquest estat. `Model::glass` passa a dir quina placa
// dibuixa (la Paper; els ATOM no tenen pantalla), el binari encén el vidre i els dits que el model té, i
// `--no-display` / `--no-touch` els treuen per a la mesura que no és de píxels. Un `--display` o un
// `--gui` a un ATOM es refusa dient-ho, i `sys.caps()` només anuncia el que la placa té (docs/50, docs/85).
// 0.32.0 (1/11): **el ratolí de la finestra és un dit del GT911**. Una Paper del banc amb `--gui` tenia el
// vidre a la pantalla i cap manera de tocar-lo: `run draw` i `run glass` demanaven un dit que ningú no
// podia posar. Prémer és el dit que baixa, moure amb el botó premut és el dit que es mou i deixar-lo anar
// és el dit que puja, en el marc del xip —i el píxel de la finestra es tradueix al píxel de la pàgina pel
// centre del bloc d'escala (`gui_page_point`, amb prova). El que el ratolí no pot fer és un pessic de dos
// dits: això és del test o del guió (docs/50, docs/85).
// 0.33.0 (1/11): **les tecles de la placa, en una finestra de les seves**. Amb `--gui` el vidre es veu i
// s'hi pot posar un dit, però les tres tecles del dial no hi eren: `run keys` demanava una tecla que
// ningú no podia prémer. Ara hi ha una segona finestra amb un botó per tecla —a dalt la de pujar, al mig la
// del centre i a baix la de baixar, la forma de la roda de la placa— i prémer és `press_key(key, true)` i
// deixar anar `press_key(key, false)`, el mateix camí que fa servir una prova o un guió. El mapa del clic a
// tecla (`gui_key_at`) és una funció pura amb prova, perquè un botó que prem la tecla del costat és un
// error que no es veu. I `--gui` passa a ser «les finestres del que la placa té»: un ATOM, sense vidre,
// treu només la de les tecles en lloc de refusar-se (docs/50, docs/85).
// 0.34.0 (1/11): **una sola finestra**: el vidre i les tecles junts. Dues finestres eren dues coses per
// col·locar i per mirar, i el gestor de finestres les posava on volia. Les tecles passen a estar dins de la
// finestra del vidre, i un clic a un botó és una tecla i un clic a la pàgina és un dit, amb els botons
// mesurats en píxels de finestra perquè no s'enconguin amb l'escala (docs/85).
// 0.34.1 (1/11): **el costat dels botons el diu el descriptor de l'entrada, i no el banc**. A la 0.34.0 la
// tira era a l'esquerra i prou, quan el que mana és `IInput::face()`: el dial del Paper és a la **cara
// esquerra** (key 0 = GPIO38 centre, key 1 = GPIO37 `Spot::kUp`, key 2 = GPIO39 `Spot::kDown`) i la tecla
// d'un ATOM és a la **cara de dalt**. Ara el costat surt de la cara i l'ordre de les posicions, o sigui que
// un dial és una columna en aquell costat i una tecla de cara de dalt és una fila sota la pàgina, i la
// pàgina es pinta on les tecles li deixen (docs/50, docs/85).
// 0.34.2 (1/11): **el dial del Paper és a la dreta de la pàgina, i el descriptor deia que a l'esquerra**.
// Qui té la placa a les mans ho diu clar, i `Face::kLeft` era una suposició del full del fabricant (el
// seu README es contradiu sobre esquerra i dreta, docs/50). `hal_paper` i el banc passen a dir
// `Face::kRight`, i la finestra dibuixa els tres botons a la dreta de la pàgina, que és on són. Quina de
// les dues tecles del costat és `up` segueix pendent de prémer-les (docs/95, E-1).
// 0.35.0 (1/11): **la finestra sembla una placa i diu si un clic arriba**. El que no és la pàgina es pinta en
// el **gris fosc de la carcassa** —la tira de les tecles i la resta de la finestra— i els botons són grisos
// amb el text clar, de manera que el que mira va a la pàgina i no a un marc blanc; **un botó premut es pinta
// amb el color del paper** del panell, que és l'única manera de veure que el clic ha arribat. I amb
// `--verbose` cada tecla diu `gui: key 0 (Centre) down` i `up`, per distingir un botó que no fa res d'un botó
// que ningú no ha premut (docs/85).
// 0.36.0 (1/11): **el text té cares, i una cara és un fitxer de text**. Fins ara hi havia una sola lletra: la
// de 5 × 7 de la barra, a la flash i només en majúscules. Ara `UICanvas` pot carregar-ne d'altres amb
// `ui.load_font` d'un fitxer de `/res/fonts` (`lunok-font 1`, `width`, `height`, `gap` i un bloc de `#` i `.`
// per glif: portable, llegible i sense descodificador) i dibuixar-hi el seu text; `ui.font` i `cv:font`
// trien la cara, `font:text_width` i `font:size` la deixen mesurar, i el bloc és de l'app i torna quan ella
// el deixa anar o quan s'acaba. La font del sistema passa a ser **una cara més** del mateix camí, i dues
// apps de prova ho ensenyen: `run fonts` i `run sheet` (docs/30, docs/80).
// 0.37.0 (1/11): **el sistema tria com neteja la pàgina entre una app i la següent**. Fins ara la vista es
// posava blanca i el vidre s'esperava que la pintada de l'app que venia ho tapés; ara, quan una app marxa, el
// sistema mira **quins dels setze nivells ha ensenyat la pàgina i quantes pintades ràpides ha pres**
// (`ui::levels_shown`, `ui::cleaner_for`) i demana el rentat que toca: `a2` si només hi ha hagut negre i
// blanc, `gl16` si hi ha hagut uns quants grisos, `gc16` si era una fotografia o si la brutícia de tantes
// pintades ja és el que es veu. Són 290 ms contra 450 per canvi d'app, i no ho decideix cap app perquè cap app
// no sap què hi havia abans (docs/50, docs/90).
// 0.38.0 (1/11): **el deflate llegit de tornada**, que és el que cal per obrir un zip —i un epub és un zip—.
// `lib/inflate` descomprimeix els tres tipus de bloc del format (stored, taula fixa i taula dinàmica) cap a un
// buffer que és del qui crida i que fa de finestra, sense reservar res: la distància cap enrere d'un stream
// mira dins del que ja s'ha escrit, i un stream que demana més enllà del principi es refusa. Amb els dos
// checksums dels contenidors (Adler-32 del zlib/woff i CRC-32 del zip) i quatre proves fetes amb blocs que ha
// comprimit el zlib de debò, no inventats. El lector del zip i la lectura de l'epub venen darrera (docs/70).
// 0.39.0 (1/11): **a face can come out of a BDF**, the bitmap format an X server read: plain text with
// `STARTFONT`, a box, and for every glyph its rows in hexadecimal. `font::load_bdf` reads it into the same
// block as always (eight by eight, ninety-six codes, in order) and `font::load_any` looks at **what the file
// says of itself** —`STARTFONT` or `lunok-font`— to pick the reader, so `ui.load_font` takes either and no
// app has to know which format its face came in. `run font-bdf` shows the face of the file beside the one of
// the system (docs/30).
// 0.40.0 (1/11): **a face can also come out of a PCF**, the binary form of a BDF --`bdftopcf` writes it and
// an X server served it--: the table directory (always little-endian) names the encodings, the metrics
// (five signed bytes a glyph, or five sixteen-bit numbers) and the bitmaps, and the numbers inside a table
// follow the byte order that table declares. The block that comes out is the same as the BDF it was
// compiled from, **byte for byte**, which is what the suite proves with the real file; and what this reader
// does not know --two-byte codes, a scan unit bigger than a byte, the bits the other way round-- is refused
// with its reason instead of guessed. `run font-pcf` shows the face beside the one of the system (docs/30).
// 0.41.0 (1/11): **the outlines of a TrueType font, read and drawn** (`lib/ttf`): the table directory, the
// seven tables a text is laid out with (head, hhea, hmtx, maxp, loca, glyf, cmap of format 4), the glyph a
// code asks for and that glyph's contours —quadratic curves and straight lines— flattened to edges and filled
// by the winding rule into a block of rows the caller owns. No allocation of its own: the file is the caller's
// bytes and the drawing happens in the caller's block. A font of the other flavour (`OTTO`, CFF), a glyph made
// of pieces and an outline with more edges than the block holds are refused with the reason. The vector is a
// font this repository builds, so the `A` at eight pixels has to come out as the eight rows of `font-bdf`.
// And the last step, the same day: **`ui.load_font` takes a `.ttf`** --the file *is* the face, so it stays in
// the app's memory and no block of bits is allocated for it-- and **`scale` means the body in eighths of a
// pixel**, so `text(x, y, s, 3)` is a line of twenty-four and every size is the letter drawn again. `run
// font-ttf` shows the same six letters at two, four and six (docs/30).
// 0.42.0 (1/11): **real fonts on the page**. `run fonts-ttf` draws DejaVu Sans, DejaVu Serif and DejaVu Sans
// Mono —cut down to the printable ascii with `pyftsubset`, eight to eleven kilobytes each, licence included—
// at three sizes, which is the three faces the system keeps at once. What a real font asked for: **a glyph
// with more edges than the block holds is now refused** instead of being drawn with the ones that fitted
// (`kMaxEdges` 384, the curve cut in six) (docs/30).
// 0.42.1 (1/11): **the first time the two new readers were ever run, and the suite found a way into the page.**
// A PCF whose table directory says an offset of `0xFFFFFFFF` was read as if it were a small one, because the
// check was `offset + 4 > length` and that sum wraps around in a `uint32_t`: the reader then read past its
// buffer and answered with a face. Every check of a number that comes from a file is now a subtraction from
// the length (`at > length || length - at < size`), in both readers. Of 299 cases, **this one was the only
// red**: the first run of the two formats was 297 green and one that had found a real hole (docs/30, docs/80).
// 0.43.0 (2/11): **the window of the bench at the size a person asked for** (`--gui-scale <1..4>`): the page
// at half —`2`— even on a screen that would hold it whole, which until now only happened when the display was
// too small. The choice of scale is a pure function (`emu::gui_fit_scale`) with proof in `test_ui`, and the
// page itself does not change: 540x960 is the model's glass (docs/85). The same day, the turn of the page
// reopens the window with the new frame instead of freezing the old one, and the host queue honours a timeout
// with stdin at EOF: that is what kept the three ink tests of `test_ui` and the long press of `test_touch` red
// under `pio test` (docs/85).
// 0.44.0 (2/11): **UILabel**, the first control of the layer: a control without a buffer whose frame is a
// background and one line, with the face `cv:font` gives it, the text and background levels, the horizontal
// and vertical alignment, and the `...` that ends a text which does not fit. It draws when the text —or one
// of its fields— really changes, and when the view is made again, which is what `needs_draw` is for
// (docs/30, docs/90).
// 0.45.0 (2/11): **UIButton**, the first control that is a container: a frame with a background and a
// border that can hold a child and clips it to the same frame, and a `pressed` state that paints a second
// inner border. The example `buttons` puts eight labels of different faces, sizes and colours inside
// eight buttons (docs/30, docs/90).
// 0.46.0 (2/11): **the prompt edits**: left and right move a cursor inside the line, up and down walk the
// last eight commands, Home/End/Delete are the keys they say, and the redraw uses ANSI when the console
// speaks it (backspaces when it does not). The line editor is pure and has its own suite; Ctrl+D at the
// `lua>` prompt closes the session like `exit` (docs/30, docs/85).
// 0.47.0 (2/11): **`hexdump` is a command of the shell and not an app**: the bytes of `/rtc`, `/fram` or
// any file go out as offset, sixteen bytes in hex and the same sixteen in visible text, with the empty
// tail left out. Like `cat`, it works before the interpreter starts and does not depend on what is
// installed on the disk (docs/30, docs/70).
// 0.48.0 (2/11): **the window of the bench is portable**. The pure colours, scaling and key geometry are
// one common file; the window itself has an SDL2 backend for X11, Wayland, Quartz and Windows, and the
// old Xlib backend stays as the POSIX fallback. The build picks SDL2 when it is installed and X11 when
// only Xlib is there; `LUNOK_GUI=sdl|x11|none` forces one. The host clock and waits are C++11's
// `steady_clock` and `sleep_for`, not `clock_gettime` and `usleep` (docs/85).
// 0.48.1 (2/11): **`ls` of the root tells a memory from a directory**. The root is the mount table, and
// `/rtc` and `/fram` are rows that are one file: they now come out as `file` with their size instead of
// `<dir>`, and `cat` with no path says how it is used instead of falling through to an unknown command
// (docs/70).
// 0.49.0 (2/11): **the controls receive a person**. The dial focuses the first pressable control and
// walks the others (`up`/`left` before, `down`/`right` after, `centre` enter), a finger on the touch
// panel focuses and holds the control under it and clicks it on release, and the UI layer owns the focus
// and the pressed state. `focusable` and `focused`, `on_focus`, `on_click` and `on_activate` are the
// contract; a page with controls does not close on three centre taps, and a generic control that sets
// `focusable = true` is in the same walk as a `ui.button` (docs/30, docs/90).
// 0.49.1 (2/11): **three numbers the review of the UI found, and one of them was a lie.** The clock of the
// dial (`button::now_ms`) had its `meter::Scope` after the `return`, so `stats` never counted the read it
// does once a turn and once per key; `label_draw` kept a kilobyte of its line buffer on the stack of the
// eight the loop task has, and it is static now; and `font:size()` said **zero glyphs** about a face of
// outlines, which is DejaVu saying it has no letters —the count is the file's now, and it is sixteen bits
// wide because a font of a thousand letters does not fit a byte— (docs/30, docs/75).
// 0.50.0 (2/11): **the example of the buttons answers a press, and the entry chunk it needed is bigger.**
// `run buttons` gives every button a real `on_click` (one of them `on_activate`, to show the other name a
// press can arrive with) and an `on_focus`, and each one counts its presses in its own label: a click is
// **one label that changes and not one button**, which the app counts to the console. It went to 9 329 B and
// the 8 KB entry cap said no, so `kSourceCap` is **16 384 B** now: +8 192 B of static RAM, +0 B of flash,
// measured (docs/75). And the suite drives the dial through a page of two buttons —the centre presses, a side
// key walks, `on_click` and `on_activate` both arrive— which is the chain nothing had tried (docs/30, docs/90).
// 0.51.0 (2/11): **a button looks like a button**. Its frame is a box with twelve pixels of round corner, a
// two-pixel border and a bevel inside it — light on the top and the left, shadow on the bottom and the right,
// the two swapped while it is held— and the focus is a **ring two pixels thick** five pixels in, black on a
// light button and white on a dark one, because the old second border could not be told from the first. One
// pixel of ink and not a ramp: the panel has sixteen levels and what a person sees is an edge (docs/30, docs/90).
// 0.52.0 (2/11): **the bar of the system is twice the bar it was**: 48 rows and the font at four times instead
// of 28 rows and twice, which read only from close up. Nine rows of air, twenty-eight of glyph, nine more and
// the black line of two, at 12 pixels of side padding and 8 between the three things. The page of an app is
// **540 × 912** now and its buffer **246 240 B** (it was 932 and 251 640): the six thousand bytes go back to
// the pool, and the touch frame of a finger follows the view down by twenty rows (docs/30, docs/50).
// 0.53.0 (2/11): **the terminal is asked what it is instead of being guessed at.** At start the shell sends the
// Device Attributes query (`ESC [ c`) and scans what comes back for `ESC [ ? ... c`: an ANSI terminal answers,
// a dumb one stays quiet, and the answer is what the redraw of a line uses from then on (`erase` and `move`
// against spaces and backspaces). **The platform's own belief is the gate** —a pipe, a file or the suite is not
// asked, so nothing pays 150 ms for an answer nobody was going to give— and what arrived while the question was
// in the air is fed to the line, so the first key a person presses is not eaten by the boot. The rule is pure
// (`shell::ansi_reply`) and the suite tries it with the replies of a VT100, an xterm, an arrow and a keystroke
// (docs/85). Measured on the bench: a pipe says `no terminal, ansi not asked`, a pseudo-terminal that does not
// answer says `no ansi, spaces and backspaces`, and one that is handed the reply says `ansi`.
// 0.53.1 (2/11): **a page of controls on the board rebooted with a corrupted backtrace.** `run buttons` on the
// Paper died before its first line: the UI of a page, the reader of a TrueType font and the interpreter all run
// in the loop task, and that task had the eight kilobytes the core defaults to. It is **16 KB now**
// (`-DARDUINO_LOOP_STACK_SIZE`, the core's own macro, written in all three board environments because a child
// does not inherit its father's flags). And the number is no longer a matter of opinion: `mem` prints **the
// stack left** at its lowest point (`ISystem::stack_free`, FreeRTOS' own high-water mark) and every app says
// its name and that number before the interpreter takes the task, so the next crash has a place to be
// (docs/75).
// 0.54.0 (2/11): **a press of a button does not flash any more.** `button_draw` and `label_draw` painted their
// frame and their word with `gc16`, the wash that flashes a whole box black and white, and a button painted it
// twice —once when the finger went down and once when it came up—, which on the glass is a flicker for every
// press. The two controls paint with **`du`** now, the quick paint, and it is the page that asks for the one
// wash it needs with `ui.present('gc16')` when it comes out. What that costs is ghost on a word that changes
// in place, and the example says so (docs/50, docs/90). And `run buttons` is **black on white in the system's
// own face at scale three** instead of four DejaVu faces at four sizes: a button is a frame and a word, and the
// word is read from a metre away (docs/30).
// 0.54.1 (2/11): **pressing a button is a hole and the word goes down with it.** Out, a button is a line of
// light on the top and the left and a shadow on the bottom and the right; held, the shadow is **two pixels of a
// much darker level** (`background - 8`) instead of one line, because a button pushed in is a hole and a hole
// has a mouth. And `run buttons` moves the label **three pixels down and to the right** while the button is
// held — a frame that sinks with a word that does not is a picture of a button— which it does in the button's
// own `on_draw`, before the children are composed, so the word is in its place in the same page (docs/30,
// docs/90).
// 0.55.0 (2/11): **a face of bits is sixteen by sixteen, and the buttons wear a real font.** The cell was eight
// because a five by seven doubled never asked for more; it is **16 × 16 with two bytes a row** now
// (`kGlyphRowBytes`), so the block of a face is **3 072 B** instead of 768 and a letter of a rounded font has
// room to be one. And `tools/ttf_to_lfont.py` turns a `.ttf` into a `.lfont` —Pillow, the glyph centred in the
// cell because the table is fixed pitch, the stroke optional— so a page can have a big rounded face with upper
// and lower case and **nothing to decode when it draws**. `run buttons` loads one made out of **Nunito
// SemiBold at 14 px**: 16 × 16, 96 glyphs, 27 049 bytes of text, OFL licence beside it (docs/30).
// 0.56.0 (2/11): **the system builds for a bare ESP32, and the sixteen kilobytes that the entry chunk needed
// are no longer reserved for ever.** A new environment, `esp32` (`esp32dev`, four megabytes of flash and no
// PSRAM, the most ordinary board there is), proves the minimum: same `hal_esp32` and same `pal_esp32` as the
// ATOM, nothing on GPIO27 for the light and nothing on GPIO39 for the key, the console as the only way in and
// out (docs/50, docs/80). And the buffer that holds an app's Lua while it is compiled is **asked from the heap
// and given back the moment the interpreter has it**: the ATOM Matrix came out **1 184 B over `dram0`** with
// the static one, and the ATOM goes from **123 392 to 107 008 B of static RAM** (docs/75).
// 0.56.1 (3/11): **the bench tells the truth about a run that failed, and a bench without a glass has no
// page.** `--script` and `--run` answer **1** when an app has died or has exited with a code that is not
// zero —and a line that does not finish in 30 s is a `1` too—, which is what docs/85 already promised.
// `--gui` on a model with no glass (an ATOM) is the window of its keys: the panel is not made and no page
// is shown, and a headless run does not pay its buffers either. The help lists `--wifi-scan`, and the
// documents are brought back to the code: the bar is 48 rows (view 540 × 912, 246 240 B), a face of bits
// is 16 × 16 (3 072 B), `rtc.bin` is the RTC memory and not the clock, the host battery is Linux's, and
// the batch's exit codes are the ones the code gives (docs/30, docs/75, docs/80, docs/85).
// 0.56.2 (3/11): **the button has weight, the page is white and round, the focus cannot be missed, and a
// present with a mode paints with that mode.** The frame is **three pixels** on a button bigger than 24 —two
// on a small one, one on a chip—, the bevel is one pixel thinner than the frame —one pixel on a chip—, the
// hole of a pressed button has as many rings as the frame, and the focus is **the two pixels of the bevel as a
// pause and then a band of seven** —the frame and four— in the colour that cannot be missed: a ring that only
// doubles the border reads as a thicker border and not as "this is the one". `run buttons` is **eight white
// buttons** wearing **Lato Black** from an outline `.ttf` cut to printable ascii (19 340 B, the OFL licence
// beside it) at 24 px, with the title at 32 and the notes at 16, and the eight labels come in three sizes.
// The reader of outlines **cuts a curve again before giving a glyph up**: six pieces a curve and 384 edges
// are the block it has, and a letter with more curves —Lato Black's `g` has ninety-one points— is drawn with
// four and then three, because a corner is visible and a missing letter is not a letter. And
// **`ui.present('gc16')` was a present with no mode**: the root the draw pushed onto the stack covered the
// app's argument and the `lua_pop` took it away, so every page painted with the `du` the controls ask for
// and the ghost of a word that had moved stayed on the glass; the stack goes back to the height it had
// before the root, and the app's quality is the paint's (docs/30, docs/50, docs/90).
// 0.57.0 (3/11): **the console has a palette, and only when the terminal asked for it.** The prompt is the
// name in green and the `>` in grey; an error is red, a warning yellow and an answer green; `ls` puts the
// kind of an entry and its size dim and a directory in blue; `help` bolds the command; the log echoes and
// dumps each line in the colour of its level. **Not one escape byte leaves** on a terminal that did not
// answer the Device Attributes question the shell asks at start, and `ansi [on|off]` says what was detected
// or overrides it when a serial bridge eats the question; `clear` no longer sends `ESC [ 2 J` to a line
// that never said it could read it (docs/30, docs/85, docs/90).
// 0.57.1 (3/11): **`stats` does not hide the rows nobody called, and does not show the ones this board does
// not have.** The table is the one of this build and this board —`sd.*` and `fram.*` with the slot and the
// chip, `glass.*` and `touch.read` on the Paper, `led.show` on the ATOMs, `imu.read` on the Matrix,
// `env.read` and `power.battery` on the Paper, `net.*` with a radio and `psram.*` with extra memory— and
// every row that exists is printed every time: zero calls and a dash in the four time columns, because a
// zero is a measurement and a row that is missing is a row that does not exist (docs/30, docs/85).
// 0.57.2 (3/11): **the glass moves its ink in time, and the blinking is the blink of the mode.** A `gc16`
// blinks black and white four times before the image —which is why it cleans—, a `gl16` twice, an `a2`
// white and then black, and the last third of every paint is the image arriving in six visible pieces;
// a quality that does not blink (`du`, `du4`, `glr16`, `gld16`) keeps the page before it until its time
// has passed, because the ink is not there before. `step_ink()` walks the phases, `idle()` waits for the
// last one, and the window redraws every frame of it: a blink nobody draws is a blink nobody sees
// (docs/85).
// 0.58.0 (3/11): **the bench has the memory of the board and the clock of its turns.** The PC's heap is
// handed out against the two homes of the model —the RAM the board reports free at the prompt (338 864 B
// on an ATOM, 162 728 B on the Paper) and the PSRAM it can map (4 095 KB, none on the ATOMs)— with
// sixteen bytes of header per block and the board's own rule: the other home before a refusal, and a
// block that fits neither is refused. `mem` and `sys.heap()` now say what the glass says, a table of
// 400 000 entries dies on the Paper with `not enough memory`, and `run heap` gives its megabyte back to
// the byte. And a turn of the kernel pays what it costs on the board: 850 µs after every turn while the
// bench pays the times of the hardware, so a thousand awaits of a millisecond take 1 888 ms here and
// 1 990 ms on the glass instead of the 1 080 ms they took before (docs/75, docs/85, docs/90).
// 0.59.0 (3/11): **what the board remembers, the bench remembers too.** The credentials of a network go
// to the key-value store —`lunoknet/ssid` for the name and `wifidrv/pass` for the pair, the board's own
// split— instead of two strings of the run, so `wifi` after a restart says the same network and
// `wifi connect` gets back on it; provisioning writes them too and `wifi forget` erases the name, which
// is what the board's `own_record_erase()` does. The hour that a chip with a coin cell
// keeps survives between runs in `<state>/clock.bin`, with the time the board was unplugged added in,
// so the Paper comes back saying `source rtc`; and `--persist` is a board that was never unplugged: the
// RTC RAM is not wiped on the ATOMs and the hour comes back there as `soc`. The NVS and the FRAM needed
// nothing: they are flash and ferrite (docs/60, docs/70, docs/85, docs/90).
// 0.60.0 (3/11): **the bench pays what each chip costs, with the numbers of its datasheet.** The heap of
// a home is a heap and not a counter —a region cut into chunks with a free list, a split that leaves the
// rest for somebody else, a merge with a free neighbour on the way out, and a largest block that is not
// the free total—, so a page fails where the board's would fail with room to spare, `mem` says the
// largest block, an allocation walks at a hundred nanoseconds a node and a block that changes home is
// copied at the twenty megabytes a second of the PSRAM. A flash page programme is the **0.7 ms** of the
// W25Q32JV and not 0.4, a file commit pays its sixteen pages and the sector, and **the NVS does not pay a
// sector per key**: it appends thirty-two-byte entries and erases the page once in 126. An air
// measurement is the **4.5 ms** the SHT30 takes, the sensor of the Matrix is 1.5 ms of bus, the hour is
// 0.9 ms of I2C, a light of the panel is thirty microseconds and the last one latches eighty after, and
// a deadline that expires is dispatched on the following turn —one millisecond— so a thousand promises
// of one millisecond take 2 107 ms here against the 1 990 ms of the glass (docs/50, docs/70, docs/75,
// docs/85).
// 0.60.1 (3/11): **the serial port pays its two directions.** The bytes that go out already cost 86,8
// microseconds each; now the ones that come in do too, so a line pasted into the terminal is a line that
// arrives at the speed of the wire and not a block that appears at once (docs/85).
// 0.60.2 (3/11): **turning the board on is not free either.** The bench pays what the board does before
// the kernel exists: the bootloader reads the image out of the flash (2 097 008 B on an ATOM, 2 182 672 on
// the Paper, at the ten megabytes a second of dual SPI at forty megahertz) and ESP-IDF's PSRAM test
// writes and reads every mapped byte at the twenty of quad SPI, so the prompt arrives 210 ms later on an
// ATOM and 638 ms later on the Paper —0,39 s and 0,84 s from the start instead of 0,19 and 0,20— and the
// run says whether it is paying (docs/75, docs/85).
// 0.61.0 (3/11): **the page of the bench is the page the address answers.** `http` from the prompt and
// `http.get` from an app fetch the real URL: the bench hands it to `curl` in a child of its own —`-q`,
// without a `~/.curlrc`, following up to ten redirections, with the certificate checked against the
// roots of this machine— and the body and the status come back from the wire, so
// `http https://github.com/...` shows the beginning of that page instead of a line saying the
// address. The kernel's path did not change: the queue, the promise, the 40 s deadline, the 16 384
// bytes and the `E_*` codes are the same, and a page that goes on past what the shell carries now
// says so and shows the first 800 B —the board keeps the beginning and answers `E_TOOBIG`, and the
// canned page does the same—. An address the board's client does not speak is `E_HTTP` and not a
// refused task, a `curl` that is not on this machine says it on the console, and `--http-fake` (or
// any of `--http-ms`, `--http-status`, `--http-body`, `--http-fail`) puts the canned page back for a
// test that must not depend on the network. A batch line now gets 60 s, because a real page needs the
// kernel's deadline plus the association (docs/30, docs/50, docs/85).
// 0.62.0 (3/11): **a page that does not fit in the heap goes to a file, whole.** `http.download(url, path)`
// writes the page to a volume as it arrives —`{status, path, bytes}`— and the app reads it with
// `fs.open`, so a few hundred kilobytes of HTML are a file and not an `E_TOOBIG`. `http <url>` at the
// prompt does the same by itself when the page passes the 16 384 B it holds —it saves it to the card, or
// to the flash when there is no card, as `<vol>/http/<name>`, and says the path and the first 800 B read
// back from there—, and `http <url> <path>` writes it wherever it is told. On the board the page streams
// from `esp_http_client` into the file in 512 B chunks, the first `max_bytes` stay in memory when the
// caller wanted a body, and a page that cannot fit in the volume is refused from its `Content-Length`
// instead of being written halfway; on the bench the file `curl` already wrote is copied through the
// storage that charges the disk, so half a megabyte of flash costs what it costs. **A download is a
// write**: it lands where the same caller could write a file —`/data/<id>/` for an app, any mounted
// drive for the prompt—, so a page cannot go where `fs.write` would answer `E_INVAL`, and a path that is
// not a drive of this board is `E_UNAVAIL` before the server is asked (docs/30, docs/50, docs/70,
// docs/75, docs/85).
// 0.63.0 (3/11): **les imatges**. `lib/img` és una llibreria de descodificadors sense cap assignació pròpia que
// treu les files d'una imatge en l'empaquetament de quatre bits del vidre, i `ui.image` i `ui.image_info` les
// porten a la pàgina: **PNG** (tipus 0, 2, 3, 4 i 6, profunditats 1 a 16, tots els filtres, paleta i
// transparència), **JPEG** base i seqüencial estès (qualsevol mostreig, marcadors de reinici, IDCT en coma
// flotant i YCbCr), **BMP** de 1, 4, 8, 16, 24 i 32 bits amb paleta o màscares i de dalt o de baix, **GIF**
// 87a/89a amb LZW, transparència i entrellaçat, **ICO** amb una imatge de BMP o un PNG a dins, i la família
// **netpbm** (PBM, PGM i PPM, en text i en cru). El color es converteix amb la lluminància dels setze nivells i
// un **dither ordenat de 4×4** —el negre és negre i el blanc és blanc a tot arreu, i un degradat no fa
// franges—, una imatge amb transparència es compon sobre el paper, i una mida més gran o més petita és un
// escalat al més proper. El que no es llegeix es diu i no es dibuixa malament: PNG entrellaçat, JPEG progressiu
// o aritmètic, CMYK i BMP amb RLE són `E_UNSUPPORTED`; un fitxer que diu ser una imatge i no ho és, `E_IO`; i
// una imatge de més de 2 MB, de més de 4 096 píxels o que no cap a la memòria, `E_INVAL`/`E_NOSPC`. L'exemple
// `run images` porta **una imatge de cada família** a `apps/images/res` —i **una imatge diferent per a
// cadascuna**, que ensenya el que aquell format sap fer: una fotografia en JPEG, tinta plana en BMP, una
// paleta de vuit tintes, una cantonada transparent en GIF, una icona amb alfa, línies primes en PNG, dos
// degradats i un bit— passades totes amb els botons; el **mateix pla en sis formats**, que és el que demostra
// que els sis descodificadors donen els mateixos píxels, viu amb les proves a `test/test_img/data/plan.*`. **Dues coses que només es veuen al vidre** i que l'exemple va haver
// d'aprendre: una app que plega després de pintar és una app que ningú no pot prémer —l'exemple escolta i té
// una sortida—, i un control es compon pel que diu que ha canviat, o sigui que una pàgina que neteja el seu
// canvas ha de tornar a demanar els botons o els perd (docs/30, docs/70, docs/75, docs/85, docs/90).
// 0.64.0 (4/11): **el parpelleig del vidre arriba a la finestra**. El model de la tinta ja hi era des de la
// 0.22.0 —un `du` deixa una part del que hi havia, un `gc16` blinca i neteja, i la brutícia s'acumula—, però
// la finestra es dibuixava un cop per torn i un torn és una pintada: entre la pàgina d'abans i la de després
// no hi havia cap fotograma, i el que una persona veia era una pàgina que es canviava d'un cop. La culpa era
// del banc i no del model: una pintada té el seu cridador parat més d'un segon —el bus de la regió que
// s'envia i la tinta que ha de moure— i el panell, durant tot aquest temps, continua fent les blincades de la
// qualitat que li han demanat. Ara `MemorySurface::wait_and_pump()` parteix cada espera del vidre en trossos
// de **10 ms** i crida el *pump* de la finestra entremig, que és `pump_gui` amb la mateixa `step_ink()` de
// sempre; el binari el instal·la i el model no canvia cap número, només qui mira i quan. Gravat a 25
// fotogrames per segon, un `gc16` de pàgina sencera són dos fotogrames negres, dos de blancs, dos de negres i
// dos de blancs abans de la imatge —els 450 ms del full del fabricant— i la finestra respon al teclat durant
// tota l'espera. Amb això `run ghost` ensenya el que l'app diu que ensenya: vuit passades de `du` que deixen
// el rastre de les anteriors i un `gc16` que se l'emporta (docs/85).
// 0.64.1 (4/11): **el focus de `run images` es veu**. L'app tenia tres botons i cap `on_focus`, o sigui que
// el dial movia el focus i el systema invalidava els dos controls, però la repintada sortia amb `du`: l'anell
// que arribava quedava gris i el que se n'anava quedava allà com un anell gris més, i la pàgina semblava no
// moure's (trobat amb les tecles de la finestra, 4/11). Ara cada botó diu a `on_focus` que cal un rentat i el
// torn següent presenta amb `gc16`; com que els únics danys són els dos botons, el parpelleig és la fila dels
// botons —l'1/11 del model de la tinta— i no la pàgina sencera. És la mateixa regla que ja segueix
// `run buttons` i que diu docs/30: un canvi de focus és un canvi d'estat de la pàgina i s'ha de rentar
// (docs/30, docs/50, docs/90).
// 0.65.0 (4/11): **els editors**. Dues apps noves —`run edit <path>` i `run hexedit <path>`— que funcionen
// igual pel **port sèrie** de la placa i per la **consola nadiua**, perquè són el mateix codi: `edit` és un
// editor de text modal com el vim (`h j k l`, `i a o`, `x`, `dd`, `yy`, `p`, `u`/`Ctrl+R`, `/` amb `n`/`N`,
// `:w`, `:q`, `:wq`, `:e`, i els nombres de línia) i `hexedit` és l'editor hexadecimal de sempre (setze bytes
// per fila, el desplaçament i l'ASCII, dos niblies per byte, `x` esborra, `i` insereix, `u` desfà). Tres coses
// del sistema han hagut de canviar perquè hi cabessin:
//   - **`console.raw([on])` i `console.read_byte()`**: el teclat deixa de ser una línia i passa a ser bytes
//     —les fletxes senceres, sense eco i sense edició—, amb una cua de 256 claus perquè escriure sigui més
//     ràpid que el torn, i amb els tres ESC de sempre com a sortida d'emergència.
//   - **El camí que la persona nomena**: `run edit /littlefs/notes.txt` dona a l'app l'autoritat del prompt
//     **sobre aquell fitxer** mentre viu (llegir i escriure) i sobre cap altre, que és el que fa que un editor
//     desi on li han dit; la resta del disc de la persona segueix dient `E_INVAL`. De passada s'ha arreglat un
//     error de debò: el nom de l'app era un punter a una pila del shell que ja no hi era quan l'app corria, i
//     ara se'n guarda una còpia (`luabind::app_path`).
//   - **El sostre del codi d'entrada, de 16 a 32 KB**: l'editor fa 26 644 B i el buffer és al heap des de la
//     0.56.0 i es torna just després de compilar, o sigui que el que puja és un bloc transitori (docs/75).
// El mode cru i el camí nomenat porten prova nativa (`test_luabind`, `test_apps`), i els dos editors s'han
// conduït per un pseudo-terminal contra el binari: `jA!` + `:wq` sobre un fitxer i el fitxer queda amb el
// signe, i `41` i `:wq` sobre un binari i el byte queda en `41` (docs/30, docs/70, docs/75, docs/85).
// 0.65.1 (4/11): **`fs.write` sobre `/rtc` i `/fram`**. Aquelles dues memòries són **un sol fitxer** cadascuna
// i el seu nom és el del muntatge, o sigui que no tenen `/rtc.tmp` on escriure i després reanomenar: el camí
// atòmic de `fs.write` responia `E_NOENT`, i el que veia una persona era un editor dient `write failed: no
// such file or directory` en desar-hi (trobat amb `run hexedit /fram`, 4/11). Des d'aquí una escriptura a una
// memòria crua **va al lloc**: els bytes al començament de la memòria i la resta a zero, perquè el que s'ha
// escrit sigui el que es torna a llegir, i una escriptura que no hi cap sencera és `E_NOSPC` i no canvia res
// —mitja memòria no es pot desfer. El mànec (`fs.open` + `write`) segueix sent el camí per posar el que es
// vulgui a qualsevol desplaçament (docs/30, docs/50, docs/70).
// 0.66.0 (4/11): **els editors obren un fitxer de qualsevol mida**. La 0.65.0 hi posava un sostre de 64 KB a
// `edit` i de 512 KB a `hexedit`, i era un sostre inventat: el sistema deixa llegir un fitxer a trossos
// (`fs.read_at`, `fs.open` + `file:read`) i el que una app no ha de fer és carregar-lo sencer. Ara:
//   - **`edit` en fa un índex de línies** llegint el fitxer a trossos de 64 KB, llegeix **només les línies que
//     es veuen** (una finestra de 2 KB per a una línia de més de 8 KB, que es veu però no s'edita) i desa
//     escrivint un temporal on el que no ha canviat **es copia de l'original**, 64 KB a la vegada. Obrir el
//     fitxer d'1,3 MB que deixa el `http`, anar-ne a l'última línia, escriure-hi i desar-lo funciona.
//   - **`hexedit` llegeix la finestra que es veu** i desa els bytes escrits amb un mànec al seu desplaçament;
//     fins a 256 KB continua tenint el fitxer sencer a la memòria, que és el que permet `x` i `i` de moure el
//     que hi ha darrere. Un binari d'1,5 MB es mira i es pedaça sense tenir-lo sencer enlloc.
//   - **El temporal del camí nomenat també és el camí nomenat**: `fs.open('/littlefs/x.txt.tmp', 'w')` estava
//     prohibit i un desament atòmic des d'una app no podia existir; ara és el mateix permís que el fitxer
//     (docs/70).
//   - I **un error del heap del banc**, que és el que va fer sortir el problema: un bloc de la PSRAM que no
//     podia créixer al seu lloc només provava l'altra casa i deia `not enough memory` amb quatre megabytes
//     lliures a la mateixa PSRAM (docs/75). `move_into` el mou a qualsevol taca de la seva.
// 0.66.1 (4/11): **the apps leave `data/` and move to `apps/`**, at the root of the project, one folder
// per id, because they do not fit the flash image of the small boards. Measured on 3/11 with
// `tool-mklittlefs` 1.203.210628 and a block of 4 KB: the 113 files of `apps/` need **between 1 179 648
// and 1 310 720 B** of LittleFS, and the partition of the ATOM, the Matrix and the bare ESP32 is
// **917 504 B**, where the tool drops **14 files** and still exits 0 —the image of every 4 MB board has
// been going out short of apps, in silence, for as long as the apps have been there—. The system's own
// `data/` (`res/` and `data/`) fits that partition with room to spare. So the disk is now **assembled per
// board** by `tools/make_disk.py` into `build/disk` (`data_dir` in platformio.ini): `data/` for the 4 MB
// boards, and `data/` plus a **live link** to `apps/` for the Paper (11,9 MB) and the bench. Measured the
// same day: `mklittlefs` **follows** a symbolic link to a directory, and so does the bench (`emu_storage`
// uses `stat()` and `S_ISDIR()`), so there is one source, no copy to go stale, and the Paper's image does
// carry the apps. The bench reads `build/disk` by default, the tests read `apps/…` instead of
// `data/apps/…`, and `tools/check_fs_names.py` walks the assembled disk with `followlinks` so the names
// inside an app are still looked at (docs/10, docs/70, docs/80, docs/85).
// 0.67.0 (4/11): **`ui.checkbox`**. A checkbox is a row —a square box at its left and the room for the words
// that say what is being checked, which are a `UILabel` the app adds with `cb:add`— and it is the first control
// **whose click means one thing and the system does it**: the press writes `checked` and then tells the app
// with `on_change(self, checked)`, so the field and the drawing cannot disagree; `on_activate` is not called
// for it. The box keeps the weight the button taught at a quarter of the size —four pixels of frame, a border
// of three on a page and one on a chip, a bevel that swaps and a body that goes a step darker while it is
// held—, the tick is **two strokes of a brush in its own level** (`mark`, because a black tick in a black box
// says nothing) and the ring of the focus is drawn **outside** the box, two pixels of band and one of pause,
// which is the only place a box of twelve pixels has for it. `cb:box()` is the kernel's number for the side of
// that box, which is where the words go; the side is four pixels of frame off the row's height, two on a row
// too short for that, and zero under six. The pixels are pinned by `test_ui` (the box, the tick, the hole of a
// press and the ring) and the whole chain by `test_apps`: the centre flips the focused box twice and the app
// hears `change true`, `change false` with the field already written. The app is `run checkbox`, five rows with
// their words inside, one the dial skips and one that is dark (docs/30, docs/90).
// 0.67.1 (4/11): **every build has its own disk.** `PROJECT_DATA_DIR` is one value for the whole project, so a
// single `build/disk` meant that building the ATOM after the Paper changed the disk the Paper was built from
// —and the one the bench reads— behind its back. `tools/make_disk.py` now assembles `build/disk/<env>` and
// points `PROJECT_DATA_DIR` at it from a pre-script, which runs before the filesystem target is made (read in
// PlatformIO's own `builder/main.py`: the pre-scripts come after the `PROJECT_DATA_DIR` default and before
// `DataToBin`), so `-t buildfs` and `-t uploadfs` take the tree of their own environment. Inside it, `apps/`
// is a **folder of links, one per app**, to `apps/` at the root —not one link to the directory— so a build can
// carry a subset with `custom_apps = id1,id2` and stop carrying an app by not listing it, and `mklittlefs`
// follows every one of them (measured on 3/11). **The tests copy instead of linking** (`custom_apps_copied`
// on that environment) because a test creates and deletes apps in the disk it mounts, and writing through a
// link would write into the repository. The native binaries learn which disk is theirs at build time
// (`-DLUNOK_DISK_DIR`), so the bench reads `build/disk/emulator` and the suite `build/disk/test` and neither
// can leave the other reading its leftovers (docs/70, docs/80, docs/85).
// 0.67.2 (4/11): **each board's disk carries the apps that name it.** `tools/make_disk.py` reads the manifest
// of every app of `apps/` and keeps the ones whose `boards` lists **the board's id or its chip family** —the
// same rule the kernel uses to refuse an app at `run` (docs/40)—, so the ATOM's disk has the generic examples
// plus `light` and `keys`, the Matrix adds `tilt` and `matrix`, the Paper has the generic ones plus `keys`, and
// the bare ESP32 has the generic ones alone. The board id comes from PlatformIO and the family from the
// board's own `identity.h`, so nothing is written twice. Every folder of `apps/` in the disk is a link (the
// tests copy, because they write there) and `custom_apps = id1,id2` still wins over the filter. And **the disk
// is measured before it is written**: with a block of 4 KB and the 917 504 B partition of a 4 MB board, the
// ATOM's 48 apps are 124 files and **15 of them do not fit**, the Matrix's 50 leave **18** out and the bare
// ESP32's 46 leave **13** —the tool printed `No more free space` and PlatformIO still said SUCCESS before
// this—. The script now stops a `-t buildfs`/`-t uploadfs` whose estimate (three blocks a file, the cautious
// side of the 2.25 to 2.5 measured) does not fit, and says the two ways out: a list in `custom_apps`, or a
// bigger littlefs partition. The Paper's 47 apps fit (docs/70, docs/80).
// 0.67.3 (4/11): **the firmware never formats the disk.** The fallback of `LittleFS.begin(true)`
// turned "no disk" into "a disk that never mounts": the core formats a superblock with name_max
// 255 and then refuses it (`Unsupported name_max (255 > 32)`, EINVAL), after throwing away what
// the uploader had left there. A board with no disk now says so and waits for the image, the way
// the card reader already did (docs/70).
// 0.68.0 (3/11): **the faces of the disk are one copy for every app.** `/fonts` is the disk's
// `res/fonts/` seen from the root —one more row of the mount table, twelve now, because the Paper
// with a card had ten of ten— and an app reads it and does not write it, the way it reads `/apps`.
// The four DejaVu faces cut to the printable ascii (9 948, 8 548, 8 084 and 10 896 B, the licence
// beside them) sit there once instead of once per app that wants one: `fonts-ttf` and `label`
// carried their own copies, 37 476 B paid twice, and a page now says
// `ui.load_font('/fonts/dejavusans.ttf')`. The old project's bitmap faces do not come with it, and
// `docs/92` says why with numbers: a `.lfont` is a fixed cell of at most sixteen by sixteen with
// ninety-six codes, and DejaVu at 36 px wants a cell of 25 by 43 with 216 of them — and the same
// body in text form would be 184 000 characters on the disk against 15 878 bytes of bits. The
// outlines are that same typeface, proportional like the old faces, and draw at any size (docs/30,
// docs/70, docs/92).
// 0.69.0 (3/11): **three more controls, and the system draws every one of them** — the rule the owner set:
// a page has the controls of the kernel or its own canvas with primitives, and **no control is painted in
// Lua**. `ui.progress` is a track and a prefix of it, with a negative value as "nothing to say yet" and a
// frame of one pixel because a bar is not something to press; `ui.slider` is the number the arrows of the
// dial move while it has the focus —the system writes the field and then calls `on_change`, the rule a
// checkbox already followed, and at either end of the range the key goes back to the walk so the page is
// not a dead end—; and `ui.row` is one line of a list: a title, a detail on the right, a subtitle under it,
// a separator, and **marked, which is the row inverted and not greyed**. `run controls` is the page of the
// three (docs/30, docs/90, docs/92).
// 0.70.0 (3/11): **a page is a declaration.** `ui.panel` is the room that arranges what it holds —a column, a
// row or everything on top— with `padding` and `gap` of its own, each child as big as it asked to be and
// **stretched across the flow**, and the free room split by `child.weight` with the last one taking the
// remainder; the layout happens when the panel draws, so a page turned, a view of another size or a child
// added late arrange themselves. `ui.tabs` is a strip of words in equal columns where **the chosen one is the
// tab with no line under it** —on two tones that missing piece is the signal, the lesson of `tab_strip.h:44`—
// and the arrows of the dial change it while it has the focus. `ui.dots` is the strip that says where a page
// is among the others, and it draws nothing at all when there is only one page (docs/30, docs/90).
// 0.71.0 (3/11): **a list of rows on pages, and never a scroll.** `ui.list` takes the items from the app —a Lua
// array of tables with `title`, `detail` and `subtitle`, or of strings, which are a title— and draws the rows of
// one page with the painter `ui.row` already had, because two painters of the same list always drift. **How many
// rows fit and how many pages the items make is one arithmetic and not two**: the footer of the paging exists
// only when there is more than one page, and when it does it takes its band and the rows are counted again in
// what is left, so a list cannot say "2 / 4" over the rows of the fifth page; the app reads that number from the
// same function the drawing asks. The arrows of the dial turn the page while the list has the focus and the app
// hears it at `on_page(self, page)` (docs/30, docs/90).
// 0.72.0 (3/11): **a text box with its caret, and the keyboard of the system that types into it.** The caret is
// the important half: a block as wide as the letter under it, drawn in the ink with that letter in the paper so
// the block shows what it is on, and the text is a **window** that drops leading letters until the caret is on the
// glass —typing at the end of a long word into a place nobody can see is what a box without a window does. The
// blinking is the app's (`caret`), because a control has no clock. **The keyboard is not a control**: it is a band
// the system paints at the bottom of the view over the composed page, it takes the finger and the dial before the
// page's controls, the arrows walk its keys and the centre presses one, and every key arrives at the box the app
// named —the text and the caret are written before `on_change(self, text)` arrives, the rule a checkbox and a
// slider already follow. `run typing` is the page of the two (docs/30, docs/90).
// 0.73.0 (3/11): **a dialog, which is a card with words that wrap and two doors.** The message is not cut with
// "...": a card that says half of a question is a card nobody can answer, so the lines break where they fit and
// the room above the doors is what decides how many there are. The emphasised door is **inverted** and the one the
// dial is on wears the ring —on this glass there is no colour to say "this is the default"—, the arrows walk the
// two doors, the centre fires `on_accept` or `on_cancel`, and **a finger picks the door it is on** rather than the
// one the ring was on. Where the doors are is one geometry for the paint and for the finger. Closing the card is
// still the app's, with the old rule: read the action, close, and then run it (docs/30, docs/90).
// 0.74.0 (3/11): **the shape of some numbers.** `ui.sparkline` is one bar per sample with **the scale of the peak of
// its own window** —a fixed scale shows the numbers, and what a sparkline is asked for is the shape: up, down,
// flat— and a second series that is a part of the same total is drawn as an **outline**, because the ink is the
// only level a chart may use. `ui.pie` is four slices at most, with the angles from their sum, **thirty-two
// directions in a fixed-point table** because this kernel has no floating point, and four tones that walk from the
// one the app chose so a legend can say the same numbers. Both take no focus (docs/30, docs/90).
// 0.75.0 (3/11): **a calendar month, and one copy of the leap years in the whole system.** `ui.month` is seven
// columns, a row of weekday names **the app gives** —the names of the days are a language and a language is not the
// kernel's— and **six weeks always**, so the row under the calendar does not move in February. `selected` is
// inverted and `today` wears a frame: two signals, because there is no colour for two facts. Which weekday the
// first falls on and how long February is come from `kernel/calendar.h`, the only place that knows the rule —a
// second rule in an app is a second answer, and a calendar with two of them disagrees with itself twice a year— and
// the dial stops at the end of the month because it asks that same function; the finger asks the same cells the
// paint put down (docs/30, docs/60, docs/90).
// 0.76.0 (3/11): **the menu, which is the contextual menu of this system.** `ui.menu` is a card of options over the
// page: **one box per option, a column before the next one, never under fifty-two pixels of height** —what a fingertip
// needs— and the rows grow into the room that is left. **An option the app cannot do now is hidden and not shown dead**,
// because a row that looks like it does something and does nothing is worse than no row at all and this glass has no
// third tone to draw a disabled one with; the option of the moment is inverted and the ring of the dial is drawn inside
// the box in the tone of the words. The arrows walk **the options that are on the card**, skipping the hidden ones, and
// the centre or a finger picks one with `on_pick(self, index)`. The ring of an inverted door of `ui.dialog` was the tone
// of the ink and disappeared on the door a person was looking at; it is the tone of the words now (docs/30, docs/90).
// 0.77.0 (3/11): **the bar of menus.** `ui.menu_bar` is a row of words with a line under it and **one menu that hangs from
// the word that is down** —five words and sixteen entries, one level deep, and the frame is the whole page because the
// menu opens over it and the bar paints only its own two boxes—. **An entry that cannot be used now is the dim tone and
// not a third colour**, a dash is a line between two entries and not a row a finger presses, and a tick is a box with its
// middle filled because there is no grey to say "on" with. A finger on a title folds or unfolds it; the dial walks **the
// entries that can be pressed** while a menu is down and gives the arrow back when none is; a choice fires
// `on_pick(self, title, entry)` **with the menu already folded** (docs/30, docs/90).
// 0.78.0 (3/11): **a box of several lines.** `ui.textbox_multiline` is the sibling of `ui.textbox` with room to fall: the
// same frame, the same block caret and the same byte offset, and **the window that keeps the caret on the glass**. `wrap`
// is a way of painting and not a change of the text —a row is a line and is cut at the border, or a row is broken at the
// last space that fits— and where a row ends and where the caret sits come from **one function**, so the caret of the
// paint and the caret of a finger cannot disagree; the finger puts the caret on the letter it is on, the row from the y
// and the letter from the x. **The enter key of the keyboard writes a line break here and does not end the edition**, and
// `box:rows()` says how many rows the box shows, asked from the same arithmetic (docs/30, docs/90).
// 0.79.0 (3/11): **a field that is chosen and not typed.** `ui.selector` is the question in the small dim face and the answer
// in the ink, with **a column of the triangle on its right, which is the door**. A value of a known set does not want a
// keyboard: typing a whole date and a time on this glass takes the view down to 581 pixels and a wrong digit is a value that
// looks right. **The list is not in the control** —the field is a door and what a finger or the centre gets is `on_open`,
// where the app puts up a `ui.menu` drawn by the system— and **the empty value is a state of its own**, drawn where a value
// would be. An open field keeps its paper and only its column goes to ink (docs/30, docs/90).
// 0.80.0 (3/11): **the band of notifications.** `ui.notification_bar` gives a card from edge to edge to every notice, a title,
// a text and a cross, and **stacks them upwards with the newest at the bottom**, which is the nearest to the finger and the
// one that goes away first. Three at most on the glass: **what does not fit is not lost and the band says how many are
// waiting with a `+N`**, because a notice that disappears without saying so is a notice somebody has lost. The cross
// dismisses on the way down and the rest of the card opens on the way up, and the centre dismisses the newest —a notice a
// board without a touch cannot take away is a notice that stays forever— (docs/30, docs/90).
// 0.81.0 (3/11): **the grid of cells.** `ui.icon_grid` is an icon with its name under it, on pages: **the mark in the box is
// what is recognised at a glance and the name is what confirms it**, and that is why a menu of things is a grid and not a list.
// **The page follows the cursor**, so the app hands over the whole list and never turns a page, and the footer says which one is
// on the glass. The cell that is chosen is inverted and the cell the dial is on wears the ring; the name stays in the ink even
// on a chosen cell, because it sits on the paper of the page and not inside the box of the mark. The icon is a box with a short
// mark drawn by the system —this kernel has no bitmap of its own— and the finger asks the same cells the paint put down, page
// included (docs/30, docs/90). With this one the controls of the old project are done: what is left of its UI is `format`,
// which is a table of texts and not a control, and the `option_picker`, which is a menu of many options and is already
// `ui.menu` plus `ui.list` (docs/92).
// 0.81.1 (3/11): **a finger held down is one press and not a run of them.** The chip hands the frame of a scan over
// once and the driver takes it away, so a caller that asks faster than the glass reads "nobody" between two frames of
// the same finger; the kernel read those gaps as a lift and turned one tap on a card into two choices —a menu picked
// the option the ring was on and then picked it again— and a held control let go of itself. A frame that does not
// come now keeps the one that did until the glass has been quiet for a whole scan (30 ms, three of the chip's), and
// **the turn of the frame follows the frame that arrived** and not every poll (the point was being turned once per
// turn and a finger walked away on its own). And the grid was missing from the list of controls that take the
// arrows, so the dial crossed it and the cells never moved (docs/30, docs/50, docs/90).
// 0.81.2 (4/11): **el que estava trencat després de la feina dels controls.** Cap línia de codi de la pàgina
// canvia; el que canvia és el que la fa funcionar i el que la comprova.
//   - **Les plaques tornaven a no compilar** en una màquina sense `tool-mklittlefs`: `get_package_dir` contesta
//     `None` quan el paquet no hi és i no llança, i `os.path.join(None, "mklittlefs")` era un `TypeError` que
//     aturava el build d'`atom`, `paper` i `atom_matrix` just després de la línia que diu que la mida no es
//     comprova. Ara el disc es munta igual i només se salta la comprovació.
//   - **El disc del banc torna a ser el de qui el demana.** `Options::flash_dir` tenia `LUNOK_DISK_DIR` de
//     sèrie, o sigui que **tapava `fs_dir`** i qualsevol prova llegia el disc del build i no el seu: cinc
//     proves de `test_img` deien que no hi havia les imatges que sí que hi eren. El disc del build és el
//     defecte del **binari** (`main_host` ja el posa) i no el de l'estructura d'opcions.
//   - **El sostre d'una app puja a 128 KB** (era 64): el terra de l'intèrpret és **29 842 B** des de la
//     llibreria d'UI (13 021 de Lua, 13 200 de la UI i 3 600 dels altres globals), i a l'ATOM això deixava
//     35 KB per a l'app: el mateix `writer` de la prova que escrivia sis fitxers de 15 000 B va començar a dir
//     `not enough memory`, i `run edit` no arrencava. L'ATOM contesta **338 864 B lliures**, i 128 KB és el
//     39 % (docs/75, docs/85).
//   - **Els bancs de proves són seus de debò**: `test_shell` no tenia disc —escrivia el que trobava, i per
//     això les seves quatre proves feia dies que estaven vermelles i es van posar verdes per accident quan un
//     defecte d'opcions els va fer escriure dins del repositori—; ara es fa un directori temporal amb les tres
//     portes (`/apps`, `/res`, `/data`). La prova de les cares de `/fonts` escriu les seves dues cares al seu
//     disc i deixa el lector de `.ttf` a la seva suite, que és l'única que té vidre.
// **386 proves natives, totes verdes** (eren 387 amb 12 vermelles, i les quatre de `test_shell` que
// arrossegàvem des de feia dies també hi són).
// 0.82.0 (4/11): **`repository`, una comanda del prompt que demana al repositori quines apps té.** Escriu
// `repository` i baixa el directori `apps/` de `lunokjod/lunokOS` de l'API de continguts de GitHub —la versió
// crua de la pàgina `tree/main/apps` del navegador, que és HTML i no és el que vol una comanda— i en llista les
// carpetes amb el nom, que és el que una persona vol saber. **És del sistema i no una app**: parla del sistema i
// el que ha de respondre ho ha de respondre el sistema, amb la ràdio que es porta i deixa anar com `http`
// (començada com a app de Lua, va durar una hora). Tres coses la fan possible i són les de sempre:
//   - **La resposta no cap i no hi ha de cabre**: 45 KB de JSON amb la URL de cada entrada. `http_download` la
//     deixa a `/littlefs/http/apps` i `kernel::repository::Listing` la passeja amb una finestra de **2 KB**,
//     un objecte a cada pas i cap arbre i cap taula de noms (docs/75).
//   - **Un objecte és una carpeta**: dins d'un objecte es recorda `name` i quan `type` diu `dir` aquell nom
//     surt; el nom es llença en començar l'objecte següent, o sigui que un `type` no pot fer sortir el nom d'un
//     altre (docs/70).
//   - **En columnes i amb dos passeigs**: un per comptar i saber l'amplada de la més llarga, i un altre per
//     pintar-les, amb una fila a la pila i el document al disc. L'API ja les dona ordenades pel nom.
// L'ordre és `repository [owner/repo] [dir]`, amb `lunokjod/lunokOS` i `apps` per defecte, i els errors són els
// de `http`: `E_NONET` diu que cal `wifi add`, un 404 diu el número i que un llistat és un 200, i el camí de la
// resposta es diu al final perquè es pugui tornar a llegir (docs/30, docs/85).
// 0.82.1 (4/11): **`repository info <app>`**: la fitxa d'una app del repositori. Demana el **manifest cru**
// (`raw.githubusercontent.com/…/apps/<app>/manifest.json`) i no el de l'API, que el donaria amb el contingut en
// base64 a dins d'un objecte; és un fitxer d'un kilobyte i **cap a la memòria** (2 KB de marge), o sigui que
// no necessita disc com el llistat. El pinta amb el mateix lector de JSON que llegeix els manifests del disc
// —títol, versió, autor, entrada, extensió, plaques i descripció— i la descripció es parteix a 80 columnes amb
// la continuació sota del camp, que és el que fa llegible una fitxa. Una app sense `entry` o sense `title` ho
// diu (`not a usable app`), perquè un manifest així no el pot córrer ningú (docs/30, docs/40).
// 0.82.2 (4/11): **`cat` imprimeix el fitxer sencer.** Llegia una finestra de **1 024 B** amb `vfs::read_file`
// i la imprimia sense dir res més, o sigui que un `cat` d'una pàgina que l'`http` havia desat al disc s'aturava
// a la primera pantalla i mitja i **semblava un fitxer tallat** (trobat amb `cat /sd/http/apps`, 4/11). Ara
// llegeix a finestres fins al final, amb la lectura comptada al `stats` com qualsevol altra, i el que triga és
// el cable —1,3 MB a 115 200 baudes són uns dos minuts, que és el que trigaria la placa— i no un sostre. Un
// fitxer que no acaba amb un salt en rep un, o el prompt quedaria enganxat a la seva última lletra.
// **`hexdump` ja el llegia sencer** (les seves 16 B a 16 B, amb el recompte dels zeros del final): s'ha
// comprovat amb el mateix fitxer i no s'hi ha tocat res (docs/30, docs/50, docs/70, docs/85).
// 0.83.0 (4/11): **el teclat del sistema, dibuixat i amb les dues caixes.** La banda era una graella de deu
// columnes amb caixes de botó i les paraules `DEL`, `SPACE`, `OK` i `123` en una tecla de 54 píxels, mig tallades; i
// les lletres eren totes majúscules perquè la cara del sistema no en tenia d'altres. Ara és un **QWERTY esglaonat**
// (la fila de la `a` mitja tecla a la dreta), amb la **barra d'espai** de sis dècimes al seu lloc, **majúscules** (⇧)
// i **retrocés** (←) a les puntes, `123`/`ABC` per canviar de joc, **↵ per l'enter** i **cap botó**: només lletres, i
// el bloc de tinta de la tecla on és el dial. La tecla de majúscules gira el joc sencer i deixa anar després d'una
// lletra. La cara del sistema **porta les dues caixes** (0x20 a 0x7A, 455 B) i no plega la minúscula a la capital, o
// sigui que el que s'escriu es veu com s'ha escrit, a la caixa i a l'editor. La banda fa **quatre files en els dos
// jocs** i es pinta amb `gl16`: un `du` deixava les lletres del joc vell a mig esborrar i s'hi veien dos teclats.
// **I funciona**: tres coses del banc amagaven el dit. El GT911 del banc escombra sol i **guarda el marc** fins que
// una lectura se l'emporta (un toc que comença i s'acaba entre dues lectures és encara un marc, no res);
// `ITouch::finger_still_down()` diu si el marc és d'un dit que encara hi és, i el nucli lliura llavors el toc i la
// seva deixada alhora; i la cua del nucli es talla a vint mil·lisegons quan hi ha finestra, o dos clics entraven
// plegats al panell i en sortien com un. Mesurat amb dotze clics de 90 ms sobre una tecla i una tecla premuda 1,8 s:
// **una lletra abans i deu ara**, i el dit que jeu escriu una lletra i no una ratxa (docs/30, docs/50, docs/85,
// docs/90).
// 0.83.1 (4/11): **el teclat no parpelleja.** `keyboard_paint` marcava la banda sencera a cada `present`, i una app
// que espera escrivint torna a presentar cada sis-cents mil·lisegons pel cursor de la caixa: el teclat es refrescava
// sencer a cada parpelleig encara que no s'hi hagués tocat res (vist a la finestra, 4/11). Ara la banda es pinta al
// llenç —és la capa de sobre i hi ha de ser— i **el que arriba al vidre és només el que ha canviat**: el bloc de la
// tecla que es mou són dues cel·les i el canvi de joc o de caixa és la banda, i una presentada sense res premut no
// pinta res. Mesurat a la finestra amb quatre captures seguides: la banda **0 píxels de diferència** entre elles i la
// caixa 45, que és el cursor (docs/50, docs/90).
// 0.84.0 (4/11): **la vista d'una app és el que queda sobre el teclat.** La banda sempre havia estat el que queda
// sota: el sistema la pintava a sobre dels últims cent setanta-sis píxels del llenç i l'app havia de preguntar
// `kb:height()` i restar-lo ella de la seva pàgina, o posar-hi coses a sota que ningú no podia prémer. Ara
// `ui.caps().height` i `ui.height()` diuen **736 i no 912** mentre el teclat és al vidre i l'app rep `on_resize` quan la
// banda surt i quan marxa —el mateix hook d'una rotació—, o sigui que una pàgina feta a partir de `ui.caps()` ja queda
// bé i no ha de restar res. **El llenç no es torna a demanar** i no costa ni un byte: la banda continua pintant-se a
// sobre dels seus últims píxels i `ui.canvas().height` diu l'alçada física per a qui la necessiti. El hook arriba amb el
// motiu i no amb el fet: `canvas:resize(w, h)` (nou) deixa un `ui.panel` —o una etiqueta, o una caixa de text, que no
// tenen píxels propis— prendre la mida nova, i un canvas amb buffer la refusa perquè la seva és un bloc demanat amb una
// mida. La banda que marxa amb l'app que l'obria no diu res a ningú: `attach` la treu amb la pàgina fora dels llibres
// (docs/30, docs/90).
// 0.84.1 (4/11): **les dues coses que feien mal a la caixa de text.** La finestra d'una línia es pinta amb un buffer que
// es guarda entre pintades (mig kilobyte que el torn no sempre té) i **no s'esborrava**: cada pintada hi afegia el seu
// text al de l'anterior, o sigui que la caixa mostrava «h», després «hhe», després «hhehel» —el que a la finestra es veu
// com un fantasma de cada paraula escrita fins ara— (trobat amb `run typing`, 4/11). I el cursor d'una caixa de línies
// amb el cursor a zero es pintava **al final de la primera fila**: les lletres d'una fila es compten des d'u i el
// tros de codi que tanca la fila prenia qualsevol posició que el bucle no hagués comptat. Les dues amb la seva prova.
// **I les apps que teclegen han de presentar**: `notes` no ho feia i el teclat hi semblava mort (el text canviava i el
// vidre no), i les dues presenten el canvi amb `gl16` i no amb `du`, perquè un `du` deixa cinc setzens de la lletra
// anterior sota la nova (docs/30, docs/90).
// 0.84.2 (4/11): **la comanda del prompt es diu `repo`.** Es deia `repository` —deu lletres per a un nom que el
// repositori ja diu— i des d'avui és `repo [owner/repo] [dir]` i `repo info <app>`; `repository` **no** queda com a
// àlies, perquè dos noms per a una comanda és una persona que escriu el que no funciona. El que no canvia és el
// concepte: `repository::Listing` i el treball de la ràdio són els mateixos, i el que parla amb GitHub continua essent
// l'API de continguts i el `manifest.json` cru. **`cat` i `hexdump` ja eren comandes i no apps** —`print_file` i
// `hexdump_file` a la shell, des de sempre— i al disc no hi ha cap app amb aquests noms: no hi havia res a treure, i
// el que fa el sistema des d'aquí és dir-ho, amb la prova de la comanda i sense cap carpeta `apps/cat` ni
// `apps/hexdump` (docs/30, docs/85).
// 0.84.3 (4/11): **les proves tornen el que demanen.** Dues coses del banc no eren del procés i sí del terminal de qui
// les llança, i totes dues quedaven trencades. La primera: `pump_captured` i les altres captures posen `stdout` en un
// fitxer amb `dup2`, i un `TEST_ASSERT` que falla surt per un `longjmp` de Unity i no passa per la línia que el torna a
// posar —la fallada i la resta de la tirada s'escrivien al fitxer, el `pio test` esperava un resum que no arribava i el
// matava amb SIGHUP—; ara la consola es torna a `tearDown`, que Unity crida passi el que passi, i el que hi havia
// capturat es torna a dir quan la prova ha fallat. La segona: `pal::init()` posa el terminal en mode cru i
// `pal::shutdown()` el torna, però cap suite en diu res, o sigui que el terminal de qui havia llançat les proves quedava
// sense eco fins a un `reset`; el PAL del banc instal·la `atexit` en encendre el mode cru i el torna encara que el
// procés acabi en un `exit` d'una fallada. I l'última espera sense sostre d'una prova —`while (app_running()) poll()`—
// té el pressupost de torns de les altres (docs/80).
// 0.85.0 (4/11): **el teclat, dibuixat de debò.** Les lletres de les tecles eren la taula de 5 × 7 de la barra —la
// mateixa que diu una hora i una càrrega— escalada, i en una tecla de quaranta-quatre píxels això és una graella de
// píxels i no una lletra. Ara són **DejaVu Sans Bold de `/fonts`**, que és la cara que tot disc d'aquest sistema porta:
// `keyboard_face()` la llegeix una vegada a `keyboard_show` (8,5 KB de la memòria extra, els mateixos que paga una
// pàgina que carrega una cara), la deixa anar a `keyboard_hide`, i si el disc no porta `/fonts` es queda la taula del
// sistema i ningú no es queda sense teclat. **I cada lletra es dibuixa a doble mida i es rebaixa a quatre nivells de
// tinta** (`outline_key_text`, `aa_row`): el vidre té setze nivells i la corba d'una `x` és el que fa que no sembli una
// graella. **La tecla de les majúscules és `MAY` i és un bloqueig**, no el shift d'una lletra: premut es queda premut, i
// una lletra no el desfà. **I la banda no es repinta en prémer una lletra**: es dibuixa al llenç quan canvia ella
// —quan surt, quan canvia el bloqueig o l'alfabet— i quan la pàgina la trepitja (`ui.clear`, un rectangle o un píxel, que
// és el que diu `the_band_may_be_gone`), i res més; el `keyboard_paint` que feia quaranta glifs a cada `present` (i el
// parpelleig del cursor en fa un cada sis-cents mil·lisegons) és el que costava el dibuix (docs/30, docs/50, docs/90).
// 0.85.1 (4/11): **el dial de la finestra, dibuixat i petit.** Les tecles de la finestra de l'emulador deien `Up`,
// `Centre` i `Down` amb la taula del sistema, i una paraula en un botó de vuitanta píxels és el que fa que la finestra
// sembli un formulari. Ara **el signe de la tecla és un dibuix** —una fletxa amunt, un cercle farcit i una fletxa
// avall, de quaranta píxels i centrat— que `gui_key_icon` dona com a **runs d'un píxel** i que el SDL i l'X11 pinten
// igual, perquè el mapa del clic i el dibuix surtin de la mateixa funció; una placa d'**una sola tecla** no en té, de
// signe, i es queda la paraula, que allà el botó és tot el costat i no una icona. **I els botons són de quaranta per
// quaranta amb sis de buit** (eren vuitanta per cinquanta-quatre amb vuit): amb el ratolí, un botó de dos dits és un
// botó gros i el que cal és que la columna no es mengi la pàgina (docs/85).
// 0.85.2 (4/11): **el cursor va on toca el dit, i la barra pinta les seves caixes.** Un dit en un camp de diverses
// línies posava el cursor al mig de l'última línia quan tocava per sota del text, i la pàgina no es repintava: el
// cursor es movia al model i el vidre ensenyava el d'abans. Ara **una fila que no hi és és el final de tot el text**,
// una fila buida que hi és és una fila, i **el sistema presenta el que ha mogut ell** amb `gld16` —neteja i no fa cap
// flaix, que és la regla d'aquest projecte (docs/92)—, amb la caixa dibuixada abans de compondre, perquè presentar el
// que la caixa encara no ha pintat és presentar el cursor vell. I **la barra són tres caixes** i no una franja: l'hora,
// el títol i la càrrega, cadascuna mesurada amb el text més ample que pot portar, i el que canvia es neteja i es dibuixa
// sol. Un títol nou era un `gc16` de tota la franja —quatre ensurts per una paraula— i un minut era un `du`, que pinta
// però no neteja; ara les dues coses són un `gld16` de la caixa que ha canviat. El `gc16` de la franja sencera queda per
// al boot i per a un gir de pàgina, que són els dos casos en què la franja sencera és nova (docs/50, docs/85, docs/92).
// 0.85.3 (4/11): **el que es veu a la finestra, dit amb una línia.** Amb `--verbose` cada pintat del vidre surt amb la
// seva regió i la seva qualitat (`gui: paint 12,9 116x28 gld16`), que és com es respon «el títol repinta la franja?» amb
// el log i no amb l'ull: el que s'hi veu és el boot amb `gc16` de 0,0 540x48, la pàgina de l'app, i després **una hora de
// 116x28 per minut** i res més. I **les apps de controls deixen de demanar `gc16`**: una pàgina de text i de controls és
// binària —tinta i paper, i el que està triat s'inverteix— i el rentat que li toca és `gld16`, que neteja i **no fa cap
// flaix**; `gc16` són quatre ensurts de pàgina per una casella marcada. Les demos que dibuixen grisos o fotos es queden
// com estaven (docs/50, docs/85, docs/92).
// 0.85.4 (4/11): **els controls són dos nivells.** El relleu d'un botó era `fons + 4` i `fons − 4` —onze i set en un
// botó blanc— i el d'una casella el mateix, amb el cos un pas més fosc en prémer-se: **quatre grisos en un control que
// només diu dues coses**, i un gris és un control que demana el rentat de setze nivells perquè quedi bé i que deixa un
// fantasma gris quan es mou. Ara el relleu és **paper i tinta**: un cos clar no té aresta més clara i la seva ombra és
// tinta, i un cos fosc té lluentor de paper i cap ombra; prémer-se és el relleu que canvia de lloc i el cos que es queda
// com era, i el que està triat s'inverteix. **I la banda del teclat neteja sense ensurt**: demanava `gl16`, que són dos
// flaixos de tota la banda a cada `MAY` i a cada `123`, i ara demana `gld16`, que neteja els setze nivells de les lletres
// amb zero flaixos. És la regla que aquest projecte ja tenia escrita: res que parpellegi no arriba al vidre, i netejar
// és `gld16` (docs/30, docs/50, docs/92).
// 0.86.0 (4/11): **`repo install <app>`, i una app pot viure a la targeta.** La comanda demana a l'API de continguts de
// GitHub el directori de l'app, en baixa **cada fitxer** —el manifest, el codi i el que porti a `res/`, carpetes
// incloses— i els deixa al disc que la persona ha triat amb `cfg`: la targeta amb `auto` quan hi és i la flash quan no.
// El llistat d'una carpeta és una baixada i cada fitxer una altra, i la cadena fa **un pas per torn**: una baixada
// demanada dins de la resposta de l'anterior és una segona pàgina amb una de pendent, i el nucli la refusa amb
// `E_BUSY` (docs/30, docs/75). **I el sistema mira les apps a la targeta i a la flash**, la targeta primer: el mateix
// id instal·lat a la targeta és el que surt al llistat i el que s'executa, i treure la targeta torna la de la flash —
// `apps::root_of` és el camí que pregunten el llistat, el `run` i la porta de `res/`, o sigui que una app de la
// targeta llegeix el seu `res/` de la targeta (docs/40, docs/70). Amb `--verbose` una instal·lació deixa una línia per
// fitxer, que és com s'ha trobat que el buffer d'un nom tallava l'adreça de descàrrega (docs/85).
// 0.86.1 (4/11): **un camí que no hi és diu per què, i on és el que sí que hi ha.** `ls /sd/app/timers` responia
// `cannot list: /sd/app/timers`, que és cert i no ajuda: la meitat que existia era `/sd/apps` i el missatge no ho deia.
// Ara `ls` mira primer i diu **`no such directory: <path>`** amb el tros que existeix i què té a dins (`/sd has: apps`)
// —vuit noms i prou, que això és una pista i no un llistat—, `cat` diu el mateix quan el fitxer no hi és, i **`ls`
// d'un fitxer el llista** amb el seu nom i la seva mida en comptes de dir que no és cap carpeta. El que no s'inventa
// és el nom: la pista és el que hi ha, i trobar la lletra que falta és de qui llegeix (docs/30, docs/70).
// 0.87.0 (4/11): **un editor de text gràfic** (`run editor [path]`). La barra de menús del sistema és el seu comandament:
// **File** obre, desa, desa com i tanca —el camí s'escriu en una targeta amb el teclat del sistema, i el que no és el
// camí que el prompt ha nomenat ni el propi de l'app es refusa i es diu per què— i **View** porta **Read Only** i
// **Word wrap** amb una casella dins del menú (`checked`), que en aquest vidre és una caixa amb el mig ple perquè no hi
// ha un tercer to per a «encesa» —i **una entrada que és un interruptor porta la caixa buida quan està apagada**
// (`checkable`: el camp `checked` hi és encara que sigui `false`), perquè una caixa buida i cap caixa són un interruptor
// i una ordre sense cap paraula—. Cada lletra es pinta amb `gld16` (neteja i no fa cap flaix) i el cursor del text va on
// toca el dit (docs/30, docs/90). **I tres coses del nucli que l'editor ha destapat**: la barra de menús **no té buffer**
// i pinta la targeta desplegada a sobre de la pàgina, o sigui que quan el menú es plega —o quan un títol en desplega un
// altre— les seves lletres es queden al canvas de sota —l'app repinta la pàgina i torna a demanar els píxels de cada
// control, i això és el que fa `repaint_page`, també quan la pàgina es mou perquè el teclat apareix o desapareix—; **la
// caixa d'una línia no dibuixava el començament del text** quan el text era més ample que el que quedava després del
// cursor (dibuixava la «resta» començant pel principi de la finestra i el cap no es dibuixava mai, o sigui que
// `/data/editor/note.txt` es veia com `/data/editor/not` amb el cursor sol a la dreta); i la targeta d'un camí **desa a
// la memòria de l'app** (`/data/editor`) o al camí que el prompt ha nomenat, que és la política de docs/70 i no una
// porta nova (docs/30, docs/70, docs/90).
// 0.87.1 (4/11): **les apps es busquen on són, i ho diuen**. Les dues bandes tenen el camí sencer en un sol lloc de
// codi —`/littlefs/apps` i `/sd/apps`— i no l'àlies `/apps` que hi havia escampat, **la targeta només es camina si el
// muntatge de `/sd` hi és** (una placa sense targeta no paga una volta pel buit ni pot ensenyar un disc que no hi ha),
// i el llistat marca amb **[card]** l'app que ha sortit de la targeta, perquè el mateix id pot ser als dos discs i qui
// prova una versió ha de poder llegir quina de les dues veu. Una app que no es troba diu on s'ha buscat, amb els dos
// camins: `no such app: x` i a sota `apps are in /littlefs/apps and /sd/apps` (o que no hi ha targeta al slot). El
// camí que respon `apps::root_of()` continua sent **la porta** —`/apps/<id>` a la flash, `/sd/apps/<id>` a la
// targeta—, que és per on una app llegeix el seu `res/` sense que `/littlefs` sencer sigui seu (docs/40, docs/70).
static const char kVersion[] = "0.87.1";

}  // namespace kernel

#endif  // LUNOK_KERNEL_VERSION_H
