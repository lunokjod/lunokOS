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
static const char kVersion[] = "0.19.1";

}  // namespace kernel

#endif  // LUNOK_KERNEL_VERSION_H
