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
static const char kVersion[] = "0.25.0";

}  // namespace kernel

#endif  // LUNOK_KERNEL_VERSION_H
