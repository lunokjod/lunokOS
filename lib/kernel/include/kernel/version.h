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
static const char kVersion[] = "0.42.0";

}  // namespace kernel

#endif  // LUNOK_KERNEL_VERSION_H
