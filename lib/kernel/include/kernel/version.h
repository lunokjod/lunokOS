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
// 0.19.1 (1/11): **the day on the board, written down**. The control layer says the same on the bench and on the Paper
// —`run layers` gives the six lines the same, the counts `1 / 1 / 2` included—, and the numbers that were missing are
// in their place: `ui: view 540x932 at 0,28, 251640 bytes of canvas`, the touch with two points and the long press
// gesture with 1002 ms of real time. And two things the run brought out and the documentation said the other way
// round: the `~120 ms` that `run panel` prints is **the app's wait** and not the time of the ink (the `gc16` is still
// without a number), and the test page was drawn with `du4`, which is measured right here as **not painting** —it goes
// with `du` and `gc16` now, and on the board the page is whole and clean, so what did not paint was the mode and not
// the path— (docs/95, E-6, E-7 and E-8; docs/75; docs/85; docs/90). What is still **without a number** is the time of
// a `gc16`: the measurement of the app cannot give it (it waits 120 ms and asks, and the loop goes out on the first
// one).
// 0.19.2 (1/11): **`ui.idle()` does not tell the truth, and now it does**. The driver of the panel does not mark
// itself busy in the same instant it receives the order to paint, and a question asked one millisecond later read zero
// with the page not even started: `run panel` came out with a `0 ms` that was no measurement at all, and a `present` of
// two qualities could send the second paint over a first one that had not started. The driver keeps now when the paint
// was asked for and does not answer yes until `kEngineStartMs` (20 ms) have gone by, a **guard** and not a
// measurement, and the same question is asked by `idle()` and by the wait of the paint before. And `run panel` counts
// from before the `present` and asks every 10 ms instead of sleeping 120 ms and subtracting it: the number it prints
// now is the one of the glass (docs/30, docs/50, docs/95 E-7).
// 0.19.3 (1/11): **the first real number of the glass**. `run panel` on the board with 0.19.2 says `the ink took 1985
// ms` for a page of two regions (`du` and `gc16`), and that number carries inside it the sending of the whole page over
// SPI and the two paints: it is **three or four times** what the 450 ms of the maker for a `gc16` would mean, and it is
// not known yet which of the three parts pays. The measurement that separates them is `squares`, which paints one
// quality per `present` and now prints `painted with du in N ms` and `erased with gc16 in N ms` of a square of 40 × 40,
// where the transfer is 800 B and the number is the engine (docs/95, E-7).
// 0.20.0 (1/11): **the system says what it costs, and the log has one single shape**. Every door of a device —the file
// system through its doors, the view with `flush` and the question of `idle()`, the read of the touch— has a counter
// with **how many times, how long and when it was the last one**, and `stats` shows them in a table that can be put to
// zero with `stats reset`. **Every call of an app to the API is counted and timed** with the hook of the interpreter
// itself —one hook and no closure per function, because an app of the ATOM has 64 KB and the closures would eat five of
// them— and the row `app <id>` gives what that app has cost since it started. And the log: **`[level][tag] message`**,
// with the letter of the level and the tag put there by the format and not by hand inside the text, the thirty-eight
// `printf` of the drivers turned into `hal::logf` with a *sink* the kernel installs, and the mirror on the console on
// while the system boots. And **the time of a call is counted until it resolves**: a call that returns a promise has
// not finished when it returns, and `sys.delay(50)` costs fifty milliseconds in `lua.call` and not the eight
// microseconds of the call. The review of every door of the HAL —what is counted and what is not, with the reason— is
// in `docs/50`. **And one error the bench brought out**: the memory of a Lua *userdata* does not come at zero, and the
// field of the counter of a promise carried rubbish in it; a promise without a time put **an hour and a half** there
// from five calls (`lua.call 4988744 ms`, measured on the board on 1/11). The field is started when the promise is made
// and the closure refuses a beginning that has never happened. And **every time of `stats` is written in the scale it
// is read in** (`845 us`, `12.3 ms`, `1.98 s`, `3 min 07 s`, `1 h 23 min`), because `4988744 ms` says nothing and
// `1 h 23 min` does. **And the real review, which brought out three holes**: the file system has **one row per unit and
// door** now (`flash.read`, `sd.read`, `nvs.write`, `rtc.stat`…) because a card is not a flash; `clock.mono` is there
// because the monotonic counter **is a read of the RTC chip** and the button, the light and the radio ask for it every
// turn (before, `clock.now` said one call in two minutes); and the view has two rows, `glass.flush` and `glass.gc16`,
// because a paint that cleans is not just any paint. **And the sum with the split**: the last line of `stats` says the
// **system time** (what the rows add up to) against the **user time** (the rest of the life of the system, counted from
// the last boot or the last `stats reset`), which is what separates "the app is slow" from "the device is slow". **And
// `stats` is not a line of log**: it carries no level and no tag, because nobody greps a table —it is read— and with the
// tags on top the columns did not line up; it is a table of fixed columns now. **And the full count**: `kernel.loop`
// (the turn of the kernel), `lua.run`/`lua.own` (the turn of the app and what is its own code), `psram.alloc`/`psram.free`
// (the extra memory is a device of this board), and **the least and the most** of every row on top of the average. **And
// `boot` was a dead counter** —the identifier was there and nobody pointed anything at it, so the row would never have
// come out— and it counts now what the system takes to come up: 108 ms on the bench. And the meters that **contain**
// others (`boot`, `kernel.loop`, `lua.run`, `lua.own`, `lua.call`) do not go into the sum of the doors, or the same
// microsecond was counted three times and the total went past a hundred per cent (138 %, measured). It costs **1 208 B
// of RAM and 9 076 B of flash** on the Paper (docs/30, docs/50, docs/75, docs/80, docs/85).
// 0.21.0 (1/11): **the glass of the bench in a window**. `--gui` opens an X11 window with the buffer of the
// `MemorySurface` inside, and paints it every time the panel paints —the counter of paints is the question, so a turn
// with no paint costs nothing— with the sixteen levels in greys and the page as the app draws it (540x960, not the
// glass lying down). It carries `--display` with it, it says no if there is no `DISPLAY`, and closing it ends the run
// like three ESC. It is X11 and a `-lX11` behind `-DLUNOK_GUI_X11`; without Xlib the bench compiles the same and says it
// has no window. **The window is not the panel**: there is no ink, no ghost and no blink in it, and the conversion of
// four bits to a pixel —the part that can be tested with no screen— has its test in `test_ui` (docs/85).
// 0.22.0 (1/11): **the glass of the bench moves ink**. An e-ink panel does not light pixels: a paint of `du` leaves a
// quarter of what was there, the dirt piles up if nothing cleans, one in a thousand misses a level, and a paint lasts
// as long as its quality (450 ms a `gc16`, 120 a `du`), so `idle()` on the bench tells the truth and `run panel` gives
// 480 ms there instead of 1. The paper is warm (`#EFEADC`) and the ink is not completely black (`#1A1B18`), because a
// page read in a perfect monochrome is a page that looks better than the one the person will have in their hands. The
// whole model is a table (`emu_ink.cpp`) and `--no-ink` takes it out; the tests run with it out and two of them try it
// —the trace of a `du`, the clean of a `gc16`, and the time of each quality— (docs/50, docs/85).
// 0.23.0 (1/11): **the bench pays the bus and draws the blink**. A paint is worth now what it is worth on the board:
// the region that changes is sent before the paint is asked for (`ink_transfer_us`) at 0.8 µs a byte —the bus of the
// driver, 10 MHz— **plus 1 100 µs per row**, which is the only number of this model that comes out of a measurement:
// the page of `run panel` cost 1 985 ms on the board and its counts leave that per row, and the whole path on the bench
// gives 1 970 ms (docs/95, E-7). The wait is inside `flush`, the way it is on the board: the bus stops the kernel and
// the bench does the same with a `usleep`, and a paint waits for the ink of the one before (`wait_paint`). And **`a2`
// blinks**: white, then black and then the image (`step_ink`), which is what the sheet of the maker says and not what
// this unit did, where `a2` painted nothing (docs/95, E-7). `--no-ink` is still the glass of the tests: there is nothing
// to send either (docs/50, docs/85).
// 0.24.0 (1/11): **the glass of the bench has fingers**. `--touch` gives the Paper's model its GT911
// (`emu::Gt911Panel`): the frame of the page —a finger at (100,200) is at (100,172) of the view—, the **two** fingers
// of its configuration, the size of the contact the board gave (28 and 30) and what it costs to read it: **800 µs**
// with no finger, **1 830** with one and **2 550** with two, three transactions at 100 kHz and nine bits per byte, plus
// the ~17 ms of the probe of 186 bytes. **And the real problem**: the chip scans by itself and gives the frame **once**,
// so a reader faster than the glass reads "nobody" with a finger on it; `--touch-scan <ms>` is how that is reproduced
// (40 ms against a turn of 20) and `Gt911Panel::missed()` counts it. On the bench there is no bus, and that is said
// too (docs/50, docs/85, docs/95).
// 0.25.0 (1/11): **one switch for every time of the bench**. `Options::delays` —which is born **false**, which is how
// the tests have always run, and which the binary turns on— decides whether a read of the disk, a byte of the port, the
// bus and the ink of a paint and the three transactions of the touch are paid or not. The model does not change: the
// ghost, the dirt and the frame of one time are still there the same. `--no-delay` leaves it off, and `--gui`
// **demands it**: a run with a window and with no waits, or with a glass that changes pixels (`--no-ink`), is refused,
// because it is a photograph of a board that does not exist. Measured: `run panel` 2 899 ms with the times and 24 ms
// without them, and the whole run 3.11 s against 0.03 s (docs/85).
// 0.26.0 (1/11): **the disk costs what each device costs, and the PSRAM is not free**. The "1 ms per read" for
// everything is over: every volume goes through its own time (`emu/disk.h`) — the flash at 40 MHz, blocks of 4 KB
// (819 µs of wire), pages of 256 B (0.4 ms) and **sectors of 45 ms that have to be erased before writing**; the card at
// 10 MHz, sectors of 512 B (410 µs) and an order of 200; and the FRAM, which is I2C at 100 kHz and where **one byte is
// 90 µs**. The NVS is a partition of the flash and pays the same. And the block of the PSRAM the kernel asks for costs
// the walk of the allocator (20 µs): the bytes are CPU time and this bench has no CPU clock, and it says so.
// `--read-delay` disappears because the flat clock was part of what was fixed (docs/50, docs/85).
// 0.27.0 (1/11): **the battery the bench runs on, borrowed from the board**. The cell of the Paper is **1150 mAh** and
// it is read as millivolts behind a divider on GPIO35; the machine of the bench says a percentage. `--battery-host`
// takes that percentage and runs it through **the curve of the cell of the model** (`hal::battery_mv`, the inverse of
// `battery_percent`), so 63 % of the laptop is 3 847 mV of the Paper and not the 11 V of its battery. And what it costs
// to ask is paid: **four conversions of ADC at ~100 µs and ~1 ms of characterisation the first time**. With
// `--no-delay`, nothing (docs/50, docs/85).
// 0.28.0 (1/11): **three apps that try the bench and the board with the same page**. `ghost` paints eight passes of
// `du` over the same block and cleans them with a `gc16` —the ghost, with the time of every paint—; `draw` makes a
// stroke that follows the finger and has a box that cleans the page, and says where every point lands; and `cost`
// measures where it runs what a `stat`, a listing, a read, a write, a paint of each quality, the gauge, the touch and
// the memory cost, to put the two tables side by side (docs/85).
// 0.28.1 (1/11): **the ghost is seen**. The model of the ink left a quarter and, above all, the dirt could reach fifteen
// sixteenths and freeze the page, so the ghost faded in two passes and the one of a page of sixteen levels could not be
// told from the one of a page of two. The `du` leaves **five sixteenths** now (a `du4`, seven), the dirt adds up to a
// ceiling of eleven —the stuck image, which is what the glass does and not a pixel that stops— and the grain is **eight
// in a thousand** with a miss of two levels once in every four. The test that measures it: a step of sixteen levels
// repainted with `du` leaves five levels of travel on the bands, and the same paint over a flat page leaves zero.
// 0.29.0 (1/11): **the code of the app costs too**. The bench ran the instructions of Lua at the speed of a PC, so a
// loop that lasts a second on the glass lasted one millisecond here and no number of `lua.own` meant anything. The hook
// of the interpreter counts **two thousand** of them and pays the price when the sum passes half a millisecond, and what
// is left is paid at the end of the turn (`--lua-cpu`, **60 ns at 160 MHz**, divided by the simulated frequency). The
// band says what one instruction costs —zero on a board, where the time is the real one— and the bench is the one that
// says sixty (docs/85).
// 0.30.0 (1/11): **the battery of the bench does not have to be asked for any more**. A model with a gauge answers
// `power.battery()` on the first try, because the `%` of this machine is read by itself at the boot and goes through the
// curve of the cell of the Paper; a machine with no battery leaves the cell at **its nominal charge (3 700 mV, 42 %)**, says
// so, and `--battery-mv` rules over the loan. Before this, `run battery` on the Paper said "this board does not know its
// battery", which is what the board says only when it has none (docs/50, docs/85).
// 0.31.0 (1/11): **the hardware of the model is there from the boot, and does not have to be asked for**. The glass and
// the GT911 were opt-in, and `run draw` on a Paper of the bench said "this board needs a touch panel and a glass for
// this": a board somebody has in their hands does not have that state. `Model::glass` says which board draws now (the
// Paper; the ATOMs have no screen), the binary turns on the glass and the fingers the model has, and `--no-display` /
// `--no-touch` take them out for a measurement that is not of pixels. A `--display` or a `--gui` on an ATOM is refused
// saying so, and `sys.caps()` only announces what the board has (docs/50, docs/85).
// 0.32.0 (1/11): **the mouse of the window is a finger of the GT911**. A Paper of the bench with `--gui` had the glass on
// the screen and no way of touching it: `run draw` and `run glass` asked for a finger nobody could put there. Pressing is
// the finger that goes down, moving with the button down is the finger that moves and letting it go is the finger that
// comes up, in the frame of the chip —and the pixel of the window is turned into the pixel of the page through the centre
// of the block of the scale (`gui_page_point`, with its test). What the mouse cannot do is a pinch of two fingers: that
// is for the test or for the script (docs/50, docs/85).
// 0.33.0 (1/11): **the keys of the board, in a window of their own**. With `--gui` the glass is seen and a finger can be
// put on it, but the three keys of the dial were not there: `run keys` asked for a key nobody could press. There is a
// second window now with one button per key —the up one on top, the centre one in the middle and the down one at the
// bottom, the shape of the wheel of the board— and pressing is `press_key(key, true)` and letting go is
// `press_key(key, false)`, the same path a test or a script uses. The map of the click to a key (`gui_key_at`) is a pure
// function with its test, because a button that presses the key next to it is an error that is not seen. And `--gui`
// comes to be "the windows of what the board has": an ATOM, with no glass, takes out only the one of the keys instead of
// being refused (docs/50, docs/85).
// 0.34.0 (1/11): **one single window**: the glass and the keys together. Two windows were two things to place and to
// look at, and the window manager put them wherever it wanted. The keys are now inside the window of the glass, and a
// click on a button is a key and a click on the page is a finger, with the buttons measured in pixels of the window so
// that they do not shrink with the scale (docs/85).
// 0.34.1 (1/11): **the side of the buttons is said by the descriptor of the input, and not by the bench**. In 0.34.0 the
// strip was on the left and that was it, when what rules is `IInput::face()`: the dial of the Paper is on the **left
// face** (key 0 = GPIO38 centre, key 1 = GPIO37 `Spot::kUp`, key 2 = GPIO39 `Spot::kDown`) and the key of an ATOM is on
// the **top face**. The side comes out of the face now and out of the order of the spots, so a dial is a column on that
// side and a key of the top face is a row under the page, and the page is painted where the keys leave it
// (docs/50, docs/85).
// 0.34.2 (1/11): **the dial of the Paper is on the right of the page, and the descriptor said on the left**. Whoever has
// the board in their hands says it plainly, and `Face::kLeft` was a guess of the sheet of the maker (their README
// contradicts itself about left and right, docs/50). `hal_paper` and the bench say `Face::kRight` now, and the window
// draws the three buttons on the right of the page, which is where they are. Which of the two side keys is `up` is still
// waiting for somebody to press them (docs/95, E-1).
// 0.35.0 (1/11): **the window looks like a board and says whether a click arrives**. What is not the page is painted in
// the **dark grey of the shell** —the strip of the keys and the rest of the window— and the buttons are grey with the
// text light, so that whoever looks goes to the page and not to a white frame; **a pressed button is painted with the
// colour of the paper** of the panel, which is the only way of seeing that the click has arrived. And with `--verbose`
// every key says `gui: key 0 (Centre) down` and `up`, to tell a button that does nothing from a button nobody has pressed
// (docs/85).
// 0.36.0 (1/11): **the text has faces, and a face is a text file**. Until now there was one single letter: the 5 × 7 one
// of the bar, on the flash and only in capitals. `UICanvas` can load others now with `ui.load_font` from a file of
// `/res/fonts` (`lunok-font 1`, `width`, `height`, `gap` and a block of `#` and `.` per glyph: portable, readable and
// with no decoder) and draw its text with them; `ui.font` and `cv:font`
// choose the face, `font:text_width` and `font:size` let it be measured, and the block is the app's and comes back when
// it lets it go or when it ends. The font of the system becomes **one more face** of the same path, and two test apps
// show it: `run fonts` and `run sheet` (docs/30, docs/80).
// 0.37.0 (1/11): **the system chooses how it cleans the page between one app and the next**. Until now the view was put
// white and the glass was expected to be covered by the paint of the app that came; now, when an app goes, the system
// looks at **which of the sixteen levels the page has shown and how many fast paints it has taken**
// (`ui::levels_shown`, `ui::cleaner_for`) and asks for the wash that is due: `a2` if there has only been black and
// white, `gl16` if there have been a few greys, `gc16` if it was a photograph or if the dirt of so many paints is
// already what is seen. It is 290 ms against 450 per change of app, and no app decides it because no app knows what was
// there before (docs/50, docs/90).
// 0.38.0 (1/11): **the deflate read back**, which is what is needed to open a zip —and an epub is a zip—. `lib/inflate`
// decompresses the three kinds of block of the format (stored, fixed table and dynamic table) into a buffer that is the
// caller's and acts as the window, reserving nothing: the distance back of a stream looks inside what has already been
// written, and a stream that asks beyond the beginning is refused. With the two checksums of the containers (Adler-32 of
// zlib/woff and CRC-32 of zip) and four tests made with blocks the real zlib compressed, not invented ones. The reader
// of the zip and the reading of the epub come after it (docs/70).
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
// 0.63.0 (3/11): **the pictures**. `lib/img` is a library of decoders with no allocation of its own that takes the rows of
// a picture out in the packed four-bit form of the glass, and `ui.image` and `ui.image_info` carry them to the page:
// **PNG** (types 0, 2, 3, 4 and 6, depths 1 to 16, every filter, palette and transparency), **JPEG** base and extended
// sequential (any sampling, restart markers, IDCT in floating point and YCbCr), **BMP** of 1, 4, 8, 16, 24 and 32 bits
// with a palette or masks and top down or bottom up, **GIF** 87a/89a with LZW, transparency and interlacing, **ICO** with
// a BMP image or a PNG inside, and the **netpbm** family (PBM, PGM and PPM, in text and in raw). The colour is turned
// with the luminance of the sixteen levels and an **ordered dither of 4×4** —black is black and white is white
// everywhere, and a gradient does not band—, a picture with transparency is composed over the paper, and a size bigger
// or smaller is a scale to the nearest one. What is not read is said and is not drawn badly: interlaced PNG,
// progressive or arithmetic JPEG, CMYK and BMP with RLE are `E_UNSUPPORTED`; a file that says it is a picture and is not,
// `E_IO`; and a picture of more than 2 MB, of more than 4 096 pixels or that does not fit in the memory,
// `E_INVAL`/`E_NOSPC`. The example `run images` carries **one picture of every family** in `apps/images/res` —and **a
// different picture for each one**, which shows what that format can do: a photograph in JPEG, flat ink in BMP, a
// palette of eight inks, a transparent corner in GIF, an icon with alpha, thin lines in PNG, two gradients and one bit—
// all of them with the buttons; the **same plan in six formats**, which is what proves the six decoders give the same
// pixels, lives with the tests in `test/test_img/data/plan.*`. **Two things that are only seen on the glass** and that the
// example had to learn: an app that folds after painting is an app nobody can press —the example listens and has a way
// out—, and a control is composed by what it says has changed, so a page that wipes its canvas has to ask for the buttons
// again or it loses them (docs/30, docs/70, docs/75, docs/85, docs/90).
// 0.64.0 (4/11): **the blink of the glass reaches the window**. The model of the ink was already there since 0.22.0 —a
// `du` leaves a part of what was there, a `gc16` blinks and cleans, and the dirt piles up—, but the window was drawn once
// per turn and a turn is one paint: between the page before and the one after there was no frame at all, and what a person
// saw was a page that changed in one go. The fault was the bench's and not the model's: a paint has its caller stopped for
// more than a second —the bus of the region that is sent and the ink that has to move— and the panel, all that time, keeps
// doing the blinks of the quality it has been asked for. `MemorySurface::wait_and_pump()` splits every wait of the glass
// into pieces of **10 ms** now and calls the *pump* of the window in between, which is `pump_gui` with the same `step_ink()`
// as always; the binary installs it and the model changes no number, only who looks and when. Recorded at 25 frames a
// second, a `gc16` of the whole page is two black frames, two white ones, two black ones and two white ones before the
// image —the 450 ms of the sheet of the maker— and the window answers the keyboard all through the wait. With that `run
// ghost` shows what the app says it shows: eight passes of `du` that leave the trace of the ones before and a `gc16` that
// takes it away (docs/85).
// 0.64.1 (4/11): **the focus of `run images` is seen**. The app had three buttons and no `on_focus`, so the dial moved the
// focus and the system invalidated the two controls, but the repaint came out with `du`: the ring that arrived stayed grey
// and the one that went stayed there as one more grey ring, and the page seemed not to move (found with the keys of the
// window, 4/11). Every button says in `on_focus` that a wash is needed now and the turn after presents with `gc16`; since
// the only damage is the two buttons, the blink is the row of buttons —the 1/11 of the model of the ink— and not the whole
// page. It is the same rule `run buttons` already follows and that docs/30 states: a change of focus is a change of state
// of the page and has to be washed (docs/30, docs/50, docs/90).
// 0.65.0 (4/11): **the editors**. Two new apps —`run edit <path>` and `run hexedit <path>`— that work the same through the
// **serial port** of the board and through the **native console**, because they are the same code: `edit` is a modal text
// editor like vim (`h j k l`, `i a o`, `x`, `dd`, `yy`, `p`, `u`/`Ctrl+R`, `/` with `n`/`N`, `:w`, `:q`, `:wq`, `:e`, and
// the line numbers) and `hexedit` is the hexadecimal editor of always (sixteen bytes per row, the offset and the ASCII,
// two nibbles per byte, `x` erases, `i` inserts, `u` undoes). Three things of the system have had to change for them to
// fit:
//   - **`console.raw([on])` and `console.read_byte()`**: the keyboard stops being a line and becomes bytes —whole arrows,
//     with no echo and no editing—, with a queue of 256 keys so that typing is faster than the turn, and with the three ESC
//     as always as the emergency way out.
//   - **The path a person names**: `run edit /littlefs/notes.txt` gives the app the authority of the prompt **over that
//     file** while it lives (read and write) and over no other, which is what makes an editor save where it has been told;
//     the rest of the disk of the person keeps saying `E_INVAL`. On the way a real error has been fixed: the name of the
//     app was a pointer into a stack of the shell that was not there any more when the app ran, and a copy of it is kept
//     now (`luabind::app_path`).
//   - **The ceiling of the entry code, from 16 to 32 KB**: the editor is 26 644 B and the buffer has been on the heap since
//     0.56.0 and is given back right after compiling, so what goes up is a passing block (docs/75).
// The raw mode and the named path have their native test (`test_luabind`, `test_apps`), and the two editors have been
// driven through a pseudo-terminal against the binary: `jA!` + `:wq` on a file and the file comes out with the sign, and
// `41` and `:wq` on a binary and the byte comes out in `41` (docs/30, docs/70, docs/75, docs/85).
// 0.65.1 (4/11): **`fs.write` on `/rtc` and `/fram`**. Those two memories are **one single file** each and their name is
// the name of the mount, so they have no `/rtc.tmp` to write to and then rename: the atomic path of `fs.write` answered
// `E_NOENT`, and what a person saw was an editor saying `write failed: no such file or directory` when saving there (found
// with `run hexedit /fram`, 4/11). From here a write to a raw memory **goes in place**: the bytes at the start of the
// memory and the rest at zero, so that what has been written is what is read again, and a write that does not fit whole is
// `E_NOSPC` and changes nothing —half a memory cannot be undone. The handle (`fs.open` + `write`) is still the path for
// putting whatever one wants at any offset (docs/30, docs/50, docs/70).
// 0.66.0 (4/11): **the editors open a file of any size**. 0.65.0 put a ceiling of 64 KB on `edit` and of 512 KB on
// `hexedit`, and it was an invented ceiling: the system lets a file be read in pieces (`fs.read_at`, `fs.open` +
// `file:read`) and what an app must not do is load it whole. Now:
//   - **`edit` makes an index of lines** reading the file in pieces of 64 KB, reads **only the lines that are seen** (a
//     window of 2 KB for a line of more than 8 KB, which is seen but not edited) and saves by writing a temporary file
//     where what has not changed **is copied from the original**, 64 KB at a time. Opening the 1.3 MB file `http` leaves
//     behind, going to the last line, writing in it and saving it works.
//   - **`hexedit` reads the window that is seen** and saves the bytes written with a handle at their offset; up to 256 KB
//     it still holds the whole file in memory, which is what lets `x` and `i` move what is behind.
//   - **The temporary of the named path is the named path too**: `fs.open('/littlefs/x.txt.tmp', 'w')` was forbidden and
//     an atomic save from an app could not exist; it is the same permission as the file now (docs/70).
//   - And **an error of the heap of the bench**, which is what brought the problem out: a block of the PSRAM that could
//     not grow in its place only tried the other home and said `not enough memory` with four megabytes free in the same
//     PSRAM (docs/75). `move_into` moves it to any spot of its own.
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
// 0.81.2 (4/11): **what was broken after the work on the controls**. No line of code of the page changes; what changes is
// what makes it work and what tries it.
//   - **The boards stopped compiling again** on a machine with no `tool-mklittlefs`: `get_package_dir` answers `None`
//     when the package is not there and does not throw, and `os.path.join(None, "mklittlefs")` was a `TypeError` that
//     stopped the build of `atom`, `paper` and `atom_matrix` right after the line that says the size is not checked. The
//     disk is assembled the same now and only the check is skipped.
//   - **The disk of the bench is the one of whoever asks for it again.** `Options::flash_dir` had `LUNOK_DISK_DIR`
//     serialised into it, so it **covered `fs_dir`** and every test read the disk of the build and not its own: five
//     tests of `test_img` said the pictures that were there were not. The disk of the build is the default of the
//     **binary** (`main_host` already sets it) and not of the structure of options.
//   - **The ceiling of an app goes up to 128 KB** (it was 64): the floor of the interpreter is **29 842 B** since the UI
//     library (13 021 of Lua, 13 200 of the UI and 3 600 of the other globals), and on the ATOM that left 35 KB for the
//     app: the same `writer` of the test that wrote six files of 15 000 B began to say `not enough memory`, and
//     `run edit` did not start. The ATOM answers **338 864 B free**, and 128 KB is 39 % (docs/75, docs/85).
//   - **The test benches are their own for real**: `test_shell` had no disk —it wrote what it found, and that is why its
//     four tests had been red for days and turned green by accident when an error of the options made them write
//     inside the repository—; a temporary directory with the three doors (`/apps`, `/res`, `/data`) is made now. The
//     test of the faces of `/fonts` writes its two faces on its own disk and leaves the reader of `.ttf` to its suite,
//     which is the only one that has a glass.
// **386 native cases, all green** (they were 387 with 12 red, and the four of `test_shell` that we had been dragging
// along for days are there too).
// 0.82.0 (4/11): **`repository`, a command of the prompt that asks the repository which apps it has.** Type
// `repository` and it downloads the `apps/` directory of `lunokjod/lunokOS` from the GitHub contents API —the raw
// version of the browser's `tree/main/apps` page, which is HTML and not what a command wants— and lists its folders
// with their name, which is what a person wants to know. **It is the system's and not an app**: it talks about the
// system and what has to answer has to be answered by the system, with the radio taken and let go the way `http` does
// (started as a Lua app, it lasted an hour). Three things make it possible and they are the usual ones:
//   - **The answer does not fit and does not have to**: 45 KB of JSON with the URL of every entry. `http_download`
//     leaves it at `/littlefs/http/apps` and `kernel::repository::Listing` walks it with a window of **2 KB**, one
//     object at each step, with no tree and no table of names (docs/75).
//   - **An object is a folder**: inside an object `name` is remembered and when `type` says `dir` that name comes out;
//     the name is thrown away when the next object starts, so a `type` cannot make the name of another one come out
//     (docs/70).
//   - **In columns and with two walks**: one to count and know the width of the widest one, and another to paint them,
//     with one row on the stack and the document on the disk. The API already gives them sorted by name.
// The order is `repository [owner/repo] [dir]`, with `lunokjod/lunokOS` and `apps` by default, and the errors are those
// of `http`: `E_NONET` says that `wifi add` is needed, a 404 says the number and that a listing is a 200, and the path
// of the answer is said at the end so that it can be read again (docs/30, docs/85).
// 0.82.1 (4/11): **`repository info <app>`**: the card of an app of the repository. It asks for the **raw manifest**
// (`raw.githubusercontent.com/…/apps/<app>/manifest.json`) and not the one of the API, which would give it with the
// contents in base64 inside an object; it is a file of one kilobyte and it goes **into the memory** (2 KB of margin),
// so it needs no disk the way the listing does. It paints it with the same JSON reader that reads the manifests of the
// disk —title, version, author, entry, extension, boards and description— and the description is broken at 80 columns
// with the continuation under the field, which is what makes a card readable. An app without `entry` or without `title`
// says so (`not a usable app`), because such a manifest cannot be run by anybody (docs/30, docs/40).
// 0.82.2 (4/11): **`cat` prints the whole file.** It read a window of **1 024 B** with `vfs::read_file` and printed it
// without saying anything more, so a `cat` of a page `http` had saved on the disk stopped after a screen and a half and
// **looked like a cut file** (found with `cat /sd/http/apps`, 4/11). It reads in windows up to the end now, with the
// read counted in `stats` like any other, and what it costs is the wire —1.3 MB at 115 200 baud is about two minutes,
// which is what the board would take— and not a ceiling. A file that does not end with a newline gets one, or the
// prompt would be glued to its last letter.
// **`hexdump` already read it whole** (its 16 B at a time, with the count of the zeros at the end): it has been checked
// with the same file and nothing has been touched (docs/30, docs/50, docs/70, docs/85).
// 0.83.0 (4/11): **the keyboard of the system, drawn, with both cases.** The band was a grid of ten columns with
// button boxes and the words `DEL`, `SPACE`, `OK` and `123` in a key of 54 pixels, half cut; and the letters were all
// capitals because the face of the system had no others. It is a **staggered QWERTY** now (the row of the `a` half a key
// to the right), with the **space bar** six tenths wide in its place, **capitals** (⇧) and **backspace** (←) at the ends,
// `123`/`ABC` to change the set, **↵ for the enter** and **no button**: only letters and the block of ink of the key the
// dial is on. The capitals key turns the whole set and lets go after one letter. The face of the system **carries both
// cases** (0x20 to 0x7A, 455 B) and does not fold the lower case into the capital, so what is typed is seen as typed, in
// the box and in the editor. The band is **four rows in both sets** and it is painted with `gl16`: a `du` left the
// letters of the old set half erased and two keyboards could be seen in it.
// **And it works**: three things of the bench were hiding the finger. The GT911 of the bench scans by itself and **keeps
// the frame** until a read takes it away (a touch that starts and ends between two reads is still one frame, not
// nothing); `ITouch::finger_still_down()` says whether the frame is of a finger that is still there, and the kernel then
// delivers the touch and its lift together; and the queue of the kernel is cut at twenty milliseconds when there is a
// window, or two clicks went into the panel together and came out as one. Measured with twelve clicks of 90 ms on one
// key and one key held for 1.8 s: **one letter before and ten now**, and the finger that lies still types one letter and
// not a run (docs/30, docs/50, docs/85, docs/90).
// 0.83.1 (4/11): **the keyboard does not blink.** `keyboard_paint` marked the whole band on every `present`, and an app
// that waits while typing presents again every six hundred milliseconds for the caret of the box: the keyboard was
// refreshed whole on every blink even when nothing had been touched (seen in the window, 4/11). The band is painted on
// the canvas now —it is the layer on top and it has to be there— and **what reaches the glass is only what has
// changed**: the block of the key that moves is two cells and a change of set or of case is the band, and a present with
// nothing pressed paints nothing. Measured in the window with four captures one after the other: the band **0 pixels of
// difference** between them and the box 45, which is the caret (docs/50, docs/90).
// 0.84.0 (4/11): **the view of an app is what is left over the keyboard.** The band had always been what is left below:
// the system painted it over the last hundred and seventy-six pixels of the canvas and the app had to ask `kb:height()`
// and subtract it from its page, or put things under it that nobody could press. `ui.caps().height` and `ui.height()`
// say **736 and not 912** now while the keyboard is on the glass and the app receives `on_resize` when the band comes out
// and when it goes —the same hook of a rotation—, so a page made from `ui.caps()` is already right and has nothing to
// subtract. **The canvas is not asked for again** and it costs not one byte: the band keeps painting itself over its last
// pixels and `ui.canvas().height` says the physical height for whoever needs it. The hook arrives with the reason and
// not with the fact: `canvas:resize(w, h)` (new) lets a `ui.panel` —or a label, or a text box, which have no pixels of
// their own— take the new size, and a canvas with a buffer refuses it because its size is a block asked for with a size.
// The band that goes away with the app that opened it says nothing to anybody: `attach` takes it out with the page off
// the books (docs/30, docs/90).
// 0.84.1 (4/11): **the two things that hurt the text box.** The window of one line is painted with a buffer kept between
// paints (half a kilobyte that the turn does not always have) and **it was not cleared**: every paint added its text to
// the one before, so the box showed "h", then "hhe", then "hhehel" —what in the window is seen as a ghost of every word
// written so far— (found with `run typing`, 4/11). And the caret of a box of lines with the cursor at zero was painted
// **at the end of the first row**: the letters of a row are counted from one and the piece of code that closes the row
// took any position the loop had not counted. Both with their test.
// **And the apps that type have to present**: `notes` did not and the keyboard looked dead in it (the text changed and
// the glass did not), and both present the change with `gl16` and not with `du`, because a `du` leaves five sixteenths of
// the letter before under the new one (docs/30, docs/90).
// 0.84.2 (4/11): **the command of the prompt is called `repo`.** It was called `repository` —ten letters for a name the
// repository already says— and from today it is `repo [owner/repo] [dir]` and `repo info <app>`; `repository` does **not**
// stay as an alias, because two names for one command is a person typing the one that does not work. What does not change
// is the concept: `repository::Listing` and the work of the radio are the same, and what talks to GitHub is still the
// contents API and the raw `manifest.json`. **`cat` and `hexdump` were already commands and not apps** —`print_file` and
// `hexdump_file` in the shell, from the beginning— and there is no app on the disk with those names: there was nothing to
// take out, and what the system does from here is say it, with the test of the command and without an `apps/cat` or an
// `apps/hexdump` folder (docs/30, docs/85).
// 0.84.3 (4/11): **the tests give back what they ask for.** Two things of the bench were not of the process but of the
// terminal of whoever starts it, and both were left broken. The first: `pump_captured` and the other captures put
// `stdout` in a file with `dup2`, and a `TEST_ASSERT` that fails goes out through a `longjmp` of Unity and does not pass
// through the line that puts it back —the failure and the rest of the run were written to the file, `pio test` waited for
// a summary that did not arrive and killed it with SIGHUP—; the console is put back in `tearDown` now, which Unity calls
// whatever happens, and what had been captured is said again when the test has failed. The second: `pal::init()` puts
// the terminal in raw mode and `pal::shutdown()` puts it back, but no suite says anything about it, so the terminal of
// whoever had started the tests was left without echo until a `reset`; the PAL of the bench installs `atexit` when it
// turns raw mode on and puts it back even when the process ends in the `exit` of a failure. And the last wait without a
// ceiling of a test —`while (app_running()) poll()`— has the turn budget of the others (docs/80).
// 0.85.0 (4/11): **the keyboard, drawn for real.** The letters of the keys were the 5 × 7 table of the bar —the same one
// that says an hour and a charge— scaled, and in a key of forty-four pixels that is a grid of pixels and not a letter.
// They are **DejaVu Sans Bold from `/fonts`** now, which is the face every disk of this system carries: `keyboard_face()`
// reads it once at `keyboard_show` (8.5 KB of the extra memory, the same a page that loads a face pays), it lets it go
// at `keyboard_hide`, and if the disk does not carry `/fonts` the table of the system stays and nobody is left without a
// keyboard. **And every letter is drawn at double size and brought down to four levels of ink** (`outline_key_text`,
// `aa_row`): the glass has sixteen levels and the curve of an `x` is what keeps it from looking like a grid. **The key of
// the capitals is `MAY` and it is a lock**, not the shift of one letter: pressed it stays pressed, and one letter does
// not undo it. **And the band does not repaint when a letter is pressed**: it is drawn on the canvas when the band
// changes —when it comes out, when the lock or the alphabet change— and when the page steps on it (`ui.clear`, a
// rectangle or a pixel, which is what `the_band_may_be_gone` says), and nothing more; the `keyboard_paint` that made
// forty glyphs on every `present` (and the blink of the caret makes one every six hundred milliseconds) is what the
// drawing cost (docs/30, docs/50, docs/90).
// 0.85.1 (4/11): **the dial of the window, drawn and small.** The keys of the window of the emulator said `Up`, `Centre`
// and `Down` with the table of the system, and a word in a button of eighty pixels is what makes the window look like a
// form. **The sign of the key is a drawing** now —an arrow up, a filled circle and an arrow down, forty pixels and
// centred— which `gui_key_icon` gives as **runs of one pixel** and which SDL and X11 paint the same way, so the map of
// the click and the drawing come out of the same function; a board of **a single key** has no sign, and keeps the word,
// because there the button is the whole side and not an icon. **And the buttons are forty by forty with six of air**
// (they were eighty by fifty-four with eight): with a mouse, a button of two fingers is a big button and what is needed
// is that the column does not eat the page (docs/85).
// 0.85.2 (4/11): **the caret goes where the finger lands, and the bar paints its boxes.** A finger in a field of several
// lines put the caret in the middle of the last line when it landed below the text, and the page was not repainted: the
// caret moved in the model and the glass showed the one from before. **A row that is not there is the end of the whole
// text** now, an empty row that is there is a row, and **the system presents what it has moved** with `gld16` —it cleans
// and does no flash, which is the rule of this project (docs/92)—, with the box drawn before composing, because
// presenting what the box has not painted yet is presenting the old caret. And **the bar is three boxes** and not one
// strip: the hour, the title and the charge, each one measured with the widest text it can carry, and what changes is
// cleaned and drawn on its own. A new title was a `gc16` of the whole strip —four flashes for one word— and a minute was
// a `du`, which paints but does not clean; both are a `gld16` of the box that changed now. The `gc16` of the whole strip
// is left for the boot and for a turn of the page, which are the two cases in which the whole strip is new
// (docs/50, docs/85, docs/92).
// 0.85.3 (4/11): **what is seen in the window, said with one line.** With `--verbose` every paint of the glass comes
// out with its region and its quality (`gui: paint 12,9 116x28 gld16`), which is how "does the title repaint the strip?"
// is answered with the log and not with the eye: what is seen is the boot with a `gc16` of 0,0 540x48, the page of the
// app, and then **one hour of 116x28 per minute** and nothing more. And **the apps of controls stop asking for `gc16`**:
// a page of text and of controls is binary —ink and paper, and what is chosen is inverted— and the wash it deserves is
// `gld16`, which cleans and **does no flash**; `gc16` is four flashes of the page for one marked box. The demos that
// draw greys or photos stay as they were (docs/50, docs/85, docs/92).
// 0.85.4 (4/11): **the controls are two levels.** The relief of a button was `background + 4` and `background − 4`
// —eleven and seven in a white button— and the one of a checkbox the same, with the body one step darker when pressed:
// **four greys in a control that only says two things**, and a grey is a control that asks for the wash of sixteen
// levels to look right and that leaves a grey ghost when it moves. The relief is **paper and ink** now: a light body has
// no lighter edge and its shadow is ink, and a dark body has the shine of paper and no shadow; pressing is the relief
// changing place and the body staying as it was, and what is chosen is inverted. **And the band of the keyboard cleans
// with no flash**: it asked for `gl16`, which is two flashes of the whole band on every `MAY` and on every `123`, and it
// asks for `gld16` now, which cleans the sixteen levels of the letters with zero flashes. It is the rule this project
// already had written: nothing that blinks reaches the glass, and cleaning is `gld16` (docs/30, docs/50, docs/92).
// 0.86.0 (4/11): **`repo install <app>`, and an app can live on the card.** The command asks the contents API of GitHub
// for the directory of the app, downloads **every file** —the manifest, the code and whatever it carries in `res/`,
// folders included— and leaves them on the disk the person chose with `cfg`: the card with `auto` when there is one and
// the flash when not. The listing of a folder is one download and every file another, and the chain takes **one step per
// turn**: a download asked for inside the answer of the previous one is a second page with one pending, and the kernel
// refuses it with `E_BUSY` (docs/30, docs/75). **And the system looks for apps on the card and on the flash**, the card
// first: the same id installed on the card is the one that comes out in the listing and the one that runs, and taking
// the card out brings the flash one back — `apps::root_of` is the path the listing, the `run` and the door of `res/` ask
// about, so an app of the card reads its `res/` from the card (docs/40, docs/70). With `--verbose` an install leaves one
// line per file, which is how it was found that the buffer of a name cut the download address (docs/85).
// 0.86.1 (4/11): **a path that is not there says why, and where the one that is there lives.** `ls /sd/app/timers`
// answered `cannot list: /sd/app/timers`, which is true and does not help: the half that existed was `/sd/apps` and the
// message did not say so. `ls` looks first and says **`no such directory: <path>`** now with the piece that exists and
// what it holds (`/sd has: apps`) —eight names and no more, because this is a hint and not a listing—, `cat` says the
// same when the file is not there, and **`ls` of a file lists it** with its name and its size instead of saying it is not
// a folder. What is not invented is the name: the hint is what is there, and finding the letter that is missing is the
// reader's job (docs/30, docs/70).
// 0.87.0 (4/11): **a graphical text editor** (`run editor [path]`). The menu bar of the system is its command: **File**
// opens, saves, saves as and closes —the path is typed in a card with the keyboard of the system, and what is neither the
// path the prompt named nor the app's own is refused and said why— and **View** carries **Read Only** and **Word wrap**
// with a box inside the menu (`checked`), which on this glass is a box with the middle filled because there is no third
// tone for "on" —and **an entry that is a switch carries the empty box when it is off** (`checkable`: the field `checked`
// is there even when it is `false`), because an empty box and no box are a switch and an order with no word
// between them—. Every letter is painted with `gld16` (it cleans and does no flash) and the caret of the text goes where
// the finger lands (docs/30, docs/90). **And three things of the kernel the editor brought out**: the menu bar **has no
// buffer** and paints the unfolded card over the page, so when the menu folds —or when one title unfolds another— its
// letters stay on the canvas under it —the app repaints the page and asks for the pixels of every control again, and that
// is what `repaint_page` does, also when the page moves because the keyboard appears or goes away—; **the box of one
// line did not draw the beginning of the text** when the text was wider than what was left after the caret (it drew the
// "rest" starting at the beginning of the window and the head was never drawn, so `/data/editor/note.txt` was seen as
// `/data/editor/not` with the caret alone on the right); and the card of a path **saves in the memory of the app**
// (`/data/editor`) or at the path the prompt named, which is the policy of docs/70 and not a new door
// (docs/30, docs/70, docs/90).
// 0.87.1 (4/11): **apps are looked for where they are, and they say so**. The two bands have the whole path in one place
// of the code —`/littlefs/apps` and `/sd/apps`— and not the `/apps` alias that was spread around, **the card is walked
// only when the mount of `/sd` is there** (a board with no card does not pay a turn through the void and cannot show a
// disk that is not there), and the listing marks with **[card]** the app that has come from the card, because the same
// id can be on both disks and whoever tries a version has to be able to read which of the two is being seen. An app
// that is not found says where it was looked for, with both paths: `no such app: x` and under it `apps are in
// /littlefs/apps and /sd/apps` (or that there is no card in the slot). The path `apps::root_of()` answers is still **the
// door** —`/apps/<id>` on the flash, `/sd/apps/<id>` on the card—, which is how an app reads its `res/` without the whole
// `/littlefs` being its own (docs/40, docs/70).
// 0.88.0 (4/11): **the scroll bar of a box of several lines, and the gesture that moves it**. When the text does not fit
// in the box, the right side is a track of one pixel with a thumb as long as the part of the text that is seen, and the
// text is measured with the room that is left —a line that goes under the thumb is not read—, so **a box the text fits in
// has no bar and loses nothing**. The window is the caret's while the app does not name another one (`top`, `-1` = the
// caret's, and the box gives it up to the limits of the text), and **the gesture is registered by the app** with
// `on_swipe`: without that function the finger puts the caret there as always. With the gesture registered a tap waits
// for the finger to let go —twelve pixels are the line between a tap and a swipe— and the system does the arithmetic:
// one row every fourteen pixels, the `top` written by it, a `gld16` that cleans and does no flash and `on_swipe(self,
// rows)` for the app. The start of the finger, the pixels and the caret that waits **are cleared when the page comes in
// and when it goes**, which is what keeps an app that ended with the finger down from leaving a half gesture to the one
// after it. `box:lines()` says how many rows the whole text has, and the editor uses it to say "row 22 of 120"
// (docs/30, docs/90).
// 0.88.1 (4/11): **`help` with a command is the help of that command**. `help repo` shows the three lines of `repo` and
// no other, `help repo install` the one of that syntax, and what is compared is **the syntax of the line** —what goes
// before the two spaces, which is what the list already puts in bold— with the words that have been typed: a word that
// is only the beginning of a command is not the command (`help pow` is not `power`), and a word that answers to nothing
// says `no such command: x` and that `help` lists them all. On the way, the line of `http` in the list **had a single
// space** and that is why it came out flat and was no command, not even for `help http` (docs/30, docs/85).
// 0.88.2 (4/11): **the bench has no app at all**. Neither on the flash nor on the card: its disk is the system (`res/`,
// `http/`, `data/`) and the slot is empty until somebody puts a card in it, so `apps` on a bench just made says
// `no apps` and `run` says it does not know the name. An app arrives at the bench as it arrives at a board: with
// `repo install <app>`, or with a card of its own through `--sd`. **And `/apps` is a mount only when the directory is
// there**: a disk with no apps has no such row, so `ls /` and `mounts` do not show a door that leads nowhere (the bench
// found it, saying `no such directory: /apps` and right after `/ has: apps`); the tests mount their disk with the
// folders before the boot, like a disk that comes with the apps inside (docs/70, docs/80, docs/85).
// 0.88.3 (4/11): **the code speaks English.** The law says every comment in this repository is in English, and the work of
// 0.82 to 0.88 had left 172 lines of Catalan in the comments of the kernel, the bridge, the headers, the apps and the
// tests —the review of 4/11 measured them file by file: `shell.cpp` 68, `luabind.cpp` 22, `repository.h` 20, `ui.cpp` 15,
// `repository.cpp` 11, `apps.cpp` 8, `editor.lua` 10—. All of them are English now, and so is everything else that was
// left: **the 233 lines of the entries of 0.10 to 0.81 of this file**, which were the only Catalan left in the tree apart
// from three old apps (`apps/edit`, `apps/hexedit`, `apps/images`, 93 lines between them, also translated here). Nothing
// of the behaviour changes: it is the same code with its reasons written in the language the law asks for, and the three
// apps still parse (`luac -p`) with their keys and their messages untouched.
// 0.88.4 (4/11): **the listing of the repository is one object in static memory and not a frame of the stack.** It
// carries a window of two kilobytes, a name and a download address —`sizeof(Listing)` is **2 280 B**, measured with the
// header— and it was a local of `entry_at`, which the install calls **once per file of an app**, on a path that already
// comes from the shell with the radio and the download on top; `print_listing` had two of them alive at the same time,
// which is 4 560 B of one frame. The stack of the loop task is sixteen kilobytes and the QR of four broke the board once
// with a canary watchpoint (docs/75). **`Listing::one()`** gives the object every caller walks now —the install, the
// listing of the prompt and the test that guards it—, `open` throws away what the one before left in it, and the reader
// gives its file back to the backend when it has finished so a shared handle is never held by nobody (docs/70, docs/75).
// 0.88.5 (4/11): **the listing of the repository is walked in one pass and not once per entry.** `entry_at` answers an
// index by reading the document from its first byte, and `repo install` asked it for every file of the app: an app of
// twenty files paid twenty opens and twenty parses of the same forty-four kilobytes of JSON, which is O(n²) in the
// number of entries. The install walks the listing it has open now —`repository::start`, `next` and `stop`, over the
// same static reader `entry_at` uses— and the index it used to carry between turns (`g_install_entry`) is gone, because
// the reader knows where it is. **Measured on the bench** with forty entries of about a hundred bytes: **3 131 µs** one
// call each against **110 µs** in one walk, **28 times less**, and the gap grows with the number of entries. The reader
// gives its file back when the folder is finished and also when an install dies in the middle of one (docs/70, docs/75).
// 0.88.6 (4/11): **`repo install` has a test of the whole chain, and the bench answers per address to make it possible.**
// The fake wire had one canned page for every URL, which is a page and not a chain: an install asks for the listing of
// the app, the listing of each folder and every file, and all of them are different answers of the same server.
// **`emu::http_canned(key, body, status)`** adds an answer whose key is a part of the address —the first that matches
// wins, and with none of them the canned page answers—, and `http_canned_clear` starts a test from a wire with none
// (docs/85). The test installs an app of four files and a folder on the flash, checks that the summary says the count of
// what came down and that every file is on the disk with its bytes, that no `.listing.json` is left behind, and that the
// app that landed is one the system runs; a second one makes the last file of the folder answer a 404 and checks that
// the shell says which file stopped it and that the same command afterwards is a whole install and not the rest (docs/30,
// docs/70, docs/85).
// 0.88.7 (4/11): **the depth of a listing is an `int` and not a `char`.** Both walkers of `repository.cpp` counted the
// objects they were inside in a `char`, which has a sign on this board: a document with a hundred and thirty open
// objects took it past `CHAR_MAX` and into numbers no document has, which is signed overflow and undefined behaviour.
// **The consequence is not theoretical**: with a negative depth `depth == 0` never comes, the name kept for an object is
// never thrown away where the document closes, and the `name` of one object is answered as the entry of a later `type`
// that has no name of its own —an entry that is not in the answer, and a file an install would download for nobody—. Two
// bytes of `int` instead of one of `char`, and the cost is zero (found in the review of 4/11, docs/30, docs/75).
// 0.88.8 (4/11): **a key of the answer that does not fit is dropped whole and not cut in the middle.** Both walkers read
// the key into sixteen bytes and the sixteenth character went to the bin without a word: the lesson of the address that
// was cut at forty-eight and cost a 404 had not been applied to the key. A key cut in the middle can only ever fail a
// comparison —it is fifteen bytes and the keys this reader knows are shorter—, so what it must not do is pretend it was
// read: `kKey` is now the name of that size, `read_string` says when it dropped something, and the walkers skip such a
// key whole without comparing it, so the object is read as if the field were not there (the API sends `submodule_git_url`
// with sixteen characters in some answers) (found in the review of 4/11, docs/30, docs/75).
// 0.88.9 (4/11): **the folders an install has to walk are a file of the app's folder and not a table of six.** The queue
// was `g_install_waiting[6][48]`, so an app of seven folders was an install that said "this app has more folders than an
// install carries" and stopped —a fixed ceiling written on the disk— and a folder inside a folder had no room in the table
// at all: the name it took was the bare one, so its files landed in the root of the app. The plan is `<root>/.folders`
// now, one line per folder with the path it has inside the app, appended as the walk finds them and read line by line
// when the one before is finished; the ceiling is the disk and the nesting is the path. **It is also smaller**: the table
// was 288 B of `.bss` and what is there now is a path of 112 B and two counters of two, with the queue on the disk —ten
// folders are a hundred and twenty bytes read once per folder, next to the two kilobytes of the listing—. The plan goes
// away with the listing, also when the install dies in the middle, because the app's own `fs.list` must not see a file
// of the system (found in the review of 4/11, docs/30, docs/70, docs/75).
// 0.88.10 (4/11): **the list of apps does not repeat one from its twenty-fifth app on.** The walk kept the ids of the card
// in a table of twenty-four (`kSeen`) so the flash would not list them again, and past that the flash repeated an app the
// card also had: the card is the one that runs, so the list showed the same app twice with different titles, which is a
// degradation of something a person reads and the only place it happened. The question "does the card carry this id?" is
// `has_manifest` on the card now —one stat, no logging, the same call `root_of` already used—, so there is no number of
// apps that breaks it. **It is also 768 B of `.bss` less**: the table was `24 x sizeof(Entry::id)`, which is 24 x 32
// (found in the review of 4/11, docs/40, docs/75).
// 0.88.11 (4/11): **a manifest that names more boards than the system carries says so, instead of saying it names none.**
// `get_strings` refused the whole list when it did not fit —which is right, because a list with one board left out would
// say the app runs where it does not— and the caller said `%s does not say which boards it runs on` for every way of
// failing: a lie about a file that says nine of them. The reader returns **why** now (`Strings::kAbsent`, `kNotArray`,
// `kUnclosed`, `kEmptyItem`, `kItemTooLong`, `kTooMany`), walks the list **whole even when it does not fit** so it can
// say how many there were (`total`), and the two places that read a list of boards tell the truth: the log of the walk
// (`names 9 boards and this system carries 8`) and the card of `repo info` (`9 in the manifest, more than this system
// carries` instead of a dash) (found in the review of 4/11, docs/40, docs/75).
// 0.88.12 (4/11): **the userdata of the keyboard does not pretend to carry a datum.** It was `struct LuaKeyboard { int
// placeholder; }`: a field that existed so the struct would not be empty, with nothing in the code that said so —the one
// thing the keyboard has, the box it types into, is a reference of the registry for the whole system—, and a field with a
// name is a field the next reader takes for data that is there for a reason. The struct is empty now —one byte, which is
// what `lua_newuserdatauv` wants— with the reason written above it and a `static_assert` that keeps it from becoming zero
// (found in the review of 4/11, docs/30, docs/75).
// 0.88.13 (4/11): **the window of a text box measures the tail once and not once per letter.** To know how many letters
// after the caret fit, the paint asked `font::width(face, rest, scale)` for the whole accumulated tail after every
// letter it added —O(n²) in the letters the window shows— and `strlen` for the same buffer, all of it over a `static`
// that the paint before had written. The width is carried in `pen` now, the way `advance_of` carries it for a row of a
// box of several lines, with the separation after the last letter taken out of it, which is the same number the whole
// string answered; and the length of `rest` is `fit`, which the loop already knows. Same letters on the glass, and one
// pass (found in the review of 4/11, docs/75, docs/90).
// 0.88.14 (4/11): **`repo install` puts the app in place in one step and an install that dies leaves nothing behind.**
// Every file was downloaded straight into `apps/<id>` and a failure only took the folder away when no file had arrived:
// a chain that died at the fifth file of nine left half an app that `apps` counted as one —its manifest and its entry
// were the first two files— and an update that died left a mixture of the two versions. The downloads go to
// `apps/.<id>.install` now, `.listing.json` and `.folders` are inside it, and the last step renames the folder whole;
// an app that was already there is moved to `apps/<id>.old`, taken back if the last move fails, and dropped when the
// new one is in place. **What a failure leaves is no file of the app in `apps/`**, which is what the test of the chain
// checks —nothing of the four files that did come, no folder of the work, and no app in the listing— (found in the
// review of 4/11, docs/30, docs/70).
// 0.88.15 (4/11): **`install_fetch` says whether the address is already in the state instead of comparing pointers.**
// It skipped the copy when `url == g_install_url`, which works and was written down, but it is a rule about the caller's
// buffer that neither the compiler nor a reader can check: two callers build the address in the state and one hands over
// a buffer of its own, and now each one says which it is with a word of the call (found in the review of 4/11, docs/75).
// 0.88.16 (4/11): **two lines that said something that is not true.** The window of the bench answered a mouse that
// cannot reach the panel with `gui: the window is 540x960 …`, and the number it printed is the **page** and not the
// window —the window is that plus the strip of the keys, five hundred and ninety-two on the Paper— so whoever read it
// went looking for a window that size. Both backends say `the page is` now. And the rule written over `kRowUnits` was
// "the whole of a row is twenty twentieths", which the row of `a`..`l` does not keep: it is nine keys of two units with
// one unit of air at each end, nineteen of twenty, and that air is what makes a staggered keyboard read as one —a finger
// that lands on it is the keyboard's and presses nothing, which is what `keyboard_touch` answers—. Both found in the
// review of the pieces that ask to look at pixels (docs/85, docs/90).
// 0.88.17 (4/11): **the terminal of whoever runs the tests comes back as it was.** `pal::init()` puts stdin in raw mode
// —a key at a time, no echo— and saves what it found to give it back at the end of the process. The suites that boot the
// interpreter once per test call it forty-three times in one run (`test_luabind`), and **the second call saved the raw
// mode that the first had just put there**: the restore at the end then put the raw mode back, and the terminal was left
// with no echo and no line until a `reset`. The save is done once now —a `tcgetattr` that runs only when the mode is not
// already this bench's— and the test that guards it asks `tcgetattr` for the flags of before and of after, with three
// `init`s in the middle (measured: `c_lflag` 35387 before and 35377 after with the defect, which is `ECHO` and `ICANON`
// gone; 35387 and 35387 with the fix) (found on the bench, 4/11, docs/80, docs/85).
// 0.88.18 (4/11): **the bench says what not blinking costs.** `glr16` and `gld16` —the qualities an app presents with,
// the bar repaints a box with and the band of the keyboard is washed with, chosen **because they do not blink**, which
// is the rule of this project— were modelled as a perfect clean: no ghost, no dirt, no speckle. With that, the one
// decision this system takes everywhere was free here, and a page could be painted a thousand times without showing a
// mark, which is the one thing a bench must not say. A waveform that does not blink does not take the page back: it
// lands close to the level and not on it, so the model gives them **two sixteenths** of what was there and **one
// sixteenth of dirt per paint** (lighter than a `du`, which is five and one, and not the clean a `gc16` is), with a
// speckle of two in a thousand. **The numbers are the model's and not a measurement** (docs/95): the test that guards
// them paints black with `du`, white over it with `gld16` and reads the glass (docs/85, docs/92).
// 0.88.19 (4/11): **`/apps` is the two disks in one directory, and it is there from the boot.** The row was added only
// when `littlefs/apps` existed at `vfs::init`, and the bench is a disk with no app at all: `repo install squares` wrote
// the first one, the install said `2 files, 4028 bytes`, `apps` listed it —it walks the drive— and `run` answered
// `cannot read: /apps/squares/squares.lua`, because the door of the apps had never been opened. The folder is **made**
// now on every mounted disk (a card too), so the door is always a row, and it is what its name promises: `ls /apps`
// shows `littlefs/apps` and `sd/apps` together —the card first, an app of the card is the one that runs, and no name
// twice— and a path under it reads the disk that runs that app, which is the same rule `apps` and `root_of` already
// followed. **Measured on the bench**: a disk with no `apps/` boots with 8 mounts instead of 7 and `ls /` shows it,
// and the two tests that guard it —the door on a disk that had none, and the card and the flash in one listing— fall
// when either half is taken away (found on the bench, 4/11, docs/40, docs/70).
// 0.88.20 (4/11): **`/apps` is the only entry point of the apps, and it presents what the system wants.** One name on
// both disks is now presented **twice** —`squares` is the copy of the flash and `squares-SD` the copy of the card, and
// each name reads and runs its own— instead of the card's copy hiding the flash's, which is what made installing an app
// the firmware already carries a blind replacement and made the two impossible to compare. The VFS decides it in the
// listing (`CardSink` adds the suffix after the flash's list said which names it has, `PresentedSink` drops what the
// kernel refuses), `vfs::resolve` answers `/apps/<name>` from the flash and `/apps/<name>-SD` from the card, and
// **nothing outside the VFS builds a path of a disk any more**: `apps::for_each` is one walk of the door —the second
// pass over `/sd/apps`, the table of ids and the `has_manifest` shadow are gone—, `apps::root_of` answers `/apps/<id>`
// for either disk, and `apps::load` reads `/apps/<id>/manifest.json`, so `run`, the listing, the handler search and an
// app's own `/res/` all ask the same door. The kernel installs a presenter at boot (`vfs::set_apps_presenter` with
// `apps::is_an_app`): a folder that carries no `manifest.json` is not listed, not run and not read by anyone, and one
// that names another board is still presented with its `[needs …]`, because hiding it would leave a person with an app
// on their disk and no trace of it. **The id grew to 40 bytes** (`apps::Entry::id`, `luabind`'s `kAppIdBytes`, the
// shell's choice table and `open`) because the suffix rides on it and `/cfg` and `/res` hang from that name. **Measured
// on the bench**: the suite is **419 cases and all of them pass**, and each half of this was taken away on purpose to
// see it fall —without the suffix in `CardSink` the three tests that read a name twice go red (the two disks, the door
// and the thirty apps of a card); with the flash's half listed without the presenter the test of the folder that is not
// an app goes red; and with `root_of` answering `/littlefs/apps/<id>` an app cannot read its own `res/` any more —
// `/littlefs` is the system's disk and answers `E_INVAL` to an app, which is exactly the reason the path of an app is
// the door— (docs/40, docs/70).
// 0.89.0 (4/11): **`apps remove <id>` deletes an app**, and the id is the name the listing shows —`squares` for the copy
// of the flash and `squares-SD` for the copy of the card— so one of two copies can go without the other; when the flash's
// goes, the card's loses the suffix and `run squares` is the app of the card, because what the door presents is what
// exists. The disk is asked **once** and kept: the first shape of this asked the door again after deleting and reported
// that it had failed at the moment it worked, since the door then answered with the card's folder (found by the test the
// same minute). **Only the app goes** —`/data/<id>` and `/cfg/<id>` are what it left behind and not the app— and a folder
// of `/apps` that carries no `manifest.json` cannot be deleted by typing its name. The message says the disk, not the
// door: `apps: squares removed from /littlefs/apps/squares, 2 files, 4 028 bytes`, which is what `vfs::on_disk` is for
// —the mount row that is a disk and not an alias—. Measured on the bench: the suite is **420 cases and all of them
// pass**, and with the manifest question taken out the test of the folder that is not an app goes red, and with the disk
// asked twice instead of once the delete says the app is still there (docs/30, docs/40, docs/70).
// 0.89.1 (5/11): **the two metering scopes that sat after their `return`**. `power::mono_now` and `net::now_ms`
// kept the `return` of the one-liner they were when the meter arrived (0.20.0), so the scope that follows it was dead
// code the compiler threw away and `clock.mono` never counted the reads of the power policy and of the radio;
// `button::now_ms` had the same shape and was fixed in 0.49.1. The three count the read now, which is what docs/30
// and docs/50 promise (docs/30, docs/50, docs/75).
// 0.89.2 (5/11): **the boot does not walk `/apps` any more**. The `[APPS] N found` line counted every app at every
// boot —a manifest read, a JSON parse and a `stat` of the entry for each— and the boot was therefore a function of how
// many apps the disk carried, which docs/10 and docs/40 say it is not: measured on the bench with the repository's
// sixty apps, the boot went from **495.8 ms to about 176 ms**, the same boot of a disk with no app at all, and the
// line of the log goes with the count. `apps::count()` had no other caller and went too; what a person asks for is
// still answered by `apps`, `ls` and `run`, which walk the door when they are asked (docs/10, docs/40, docs/75, docs/85).
// 0.89.3 (5/11): **the repository has CI**. `.github/workflows/ci.yml` runs, on every push and pull request, the clang
// `-Wunreachable-code` pass, the 420 native tests and the five environments (`emulator` with SDL2, `atom`, `atom_matrix`,
// `paper`, `esp32`); it builds and never flashes, and the field measurements stay manual (docs/80, docs/95).
// 0.89.4 (5/11): **the README's numbers are the build's numbers again**. 420 cases in 19 suites (it said 394), and the
// ATOM's 2 196 273 B of flash (83,8 %) and 120 664 B of RAM (36,8 %), measured on this tree; the old 2 183 081 B and
// 117 192 B were from an older build. A number in a document is a measurement or it is not written (docs/75, docs/95).
// 0.89.5 (5/11): **the version law has a guard**. `tools/check_version.py` walks the commits of a push or a pull
// request and refuses one that changes a file without raising `kernel::kVersion`; the CI runs it before the builds.
// The two commits of 0.88.2 that escaped —the reused number and the tool that did not touch the header— stay where
// git put them; this is for the ones that come next (docs/80, law 10 of the README).
// 0.89.6 (5/11): **the test of the quoted path does not assume the directory order**. The selector lists the handlers
// in the order the disk gives them, and the CI's file system put `txt` before `txts` where the bench put `txts` first,
// so `1` started the other app and the assertion failed; the test now reads the number the selector printed for `txts`
// and answers that. The kernel was right; the test was the one that depended on readdir (docs/40, docs/80).
// 0.89.7 (5/11): **the ESP32 platform is pinned to 6.5.0**. A fresh CI runner took the latest espressif32, whose
// Arduino core is the split `framework-arduinoespressif32-esp32-libs` package: its deprecated `driver/adc.h` emits a
// `#warning` and the project's `-Werror` turned it into a failed `hal_esp32/battery.cpp`, while the same tree built
// here. The numbers of docs/75 and the README are measured with a toolchain, and the toolchain is now the one
// `platformio.ini` names (docs/75, docs/80).
// 0.90.0 (5/11): **an app that never yields no longer takes the board with it**. The interpreter installs the count
// hook on every band —not only where the bench prices instructions— and a turn is 100 000 instructions; when it
// reaches them the hook yields the coroutine (`lua_yield` from a hook is supported by 5.4, and `lua_isyieldable`
// keeps it out of a C call) and `poll` sees a turn and not a promise, so the kernel reads the console, delivers the
// alarms and asks the button and the radio before the app goes on. **The app is not killed**: a `while true do end`
// runs forever, but the board stays alive and `!!exit`/three ESC close it. The two warnings and `E_BUDGET` of
// docs/75 are still the next step, as are the alarm window, the `lua` prompt and the coroutines an app creates
// (docs/20, docs/75).
// 0.91.0 (5/11): **the keyboard follows the focus**. A text box says what keyboard it wants in `box.keyboard` —
// `'alpha'`, `'numeric'`, or `false` for a page that only scrolls— and the system reads it when the box takes the
// finger: the band comes, the box becomes the target, and a finger on a button, a card or the empty page takes the band
// away and clears the focus. **The band no longer appears over a control that cannot type**: `kb:show()` without a live
// text box answers `nil, E_INVAL` instead of painting a keyboard that writes nowhere; with one it shows the band and
// writes the alphabet on the box. **Text boxes are stops of the finger and not of the dial**, as docs/30 said and the
// code now says; the keyboard on the glass counts as a control for `ui_has_focusable`, so the centre is Enter and not
// the three-tap exit. `run editor` keeps its read-only page scrollable with `box.keyboard = false` (docs/30, docs/90).
// 0.92.0 (5/11): **the caret of a text box blinks without the app**. The system owns the block of the box the keyboard
// types into: `caret_poll` in the kernel loop turns `box.caret` every six hundred milliseconds, invalidates that box and
// paints it with a `du`, and `until_next_ms` counts the blink as a deadline so an app that waits for a promise still has
// a cursor. A key puts the block back and restarts the clock. **And the block is the size of a letter**: under a letter
// it is that letter, and at the end of the text it is an `M` instead of the three-pixel hairline the first boxes drew;
// an empty box had a cursor nobody could see (docs/30, docs/90). `run typing` no longer keeps a blink loop of its own.
// 0.93.0 (5/11): **the keyboard is a class with a common parent**. `Keyboard` owns the band, the finger, the dial, the
// chosen block, the face and the geometry; each child says its id and its two layouts, and a new keyboard is one more
// child and no change in the parent. `QwertyKeyboard` is the old layout and `AzertyKeyboard` is the second, with `a`
// and `q` at the head of their rows and `MAJ` on the capitals key. **The person chooses in the settings**: `cfg
// keyboard` lists them, `cfg keyboard qwerty|azerty` selects and saves the id in the NVS under `cfg/keyboard`, and the
// boot asks the UI for it; a name this build does not carry falls back to qwerty with a line. The free functions of
// `ui` keep the same signatures for the apps and Lua (docs/30, docs/70, docs/90).
// 0.94.0 (5/11): **the LilyGO TTGO T-Koala is a board of its own**. The environment `tkoala` builds for an
// ESP32-WROVER-B with 4 MB of flash and the 4 MB of PSRAM the chip maps: the board definition carries the QIO
// flash and the QSPI PSRAM, the partition table is the 4 MB one and the disk is the one of the other 4 MB
// boards. Its HAL says the three things only the board knows: the green LED is a plain GPIO5, there is no key
// the firmware can read, and the microSD is the HSPI slot (CS 15, SCK 14, MOSI 13, MISO 12). **I cada muntatge és el
// que es diu i cap altre**: la targeta contesta `/sd` i prou, perquè la placa **no té FRAM** i el camí que el Paper fa
// servir —«si no és la flash, prova la targeta»— donava a `/fram` la targeta sencera, una memòria crua que no hi és
// amb els fitxers d'algú sota un nom que promet 256 bytes (trobat en revisar la placa, 5/11). `sys.board` answers
// `ttgo-t-koala`, the bench has the same model so an app of this board can be tried on the PC, and the CI
// builds it with the other five bands (docs/50, docs/70, docs/80, docs/85).
// 0.94.1 (5/11): **the dots of a page are the counter the old project settled on, and they stay inside their box.** The
// strip was a row of solid discs —all of them in the same tone, the open one only bigger— and with more pages than fit it
// was drawn from outside the box, so the counter left the glass on both sides. It is now **two tones and a rod**: the
// ball of the page that is open is a solid disc and the biggest one, the others are **rings of ink with paper inside**,
// and a bar threads them all which is only seen in the air between two balls —drawn first, so a rod does not cross the
// middle of every ball—. There is no intermediate tone anywhere, so the strip is carried by the binary waveform and
// nothing in it fights the clean of the next pass (the old project's grey strip smeared for exactly that reason,
// docs/92). **And what does not fit is a window**: as many balls as the box holds, the ones around the page a person is
// on, so the strip never leaves its frame and the open page is always one of the balls. The whole strip owes **one**
// damaged region and not one per ball: with a ball made of two discs the marks would pass the eight a canvas holds and
// the page would be served the wash of the panel for a page turn (docs/30, docs/90).
// Measured on the bench: 431 native cases green, and each half of it falls when it is taken away —without the paper
// inside a ring the two new tests of `test_ui` go red, without the window the one of the narrow strip goes red, and
// without the rod the one of the three balls goes red—.
// 0.95.0 (5/11): **the system ships `gallery`, the page that shows every control it has.** Twenty of them in eight pages
// —label, row, button, checkbox, progress, slider, sparkline, pie, list, notification bar, text box, multi-line box,
// selector, dialog, menu bar, menu, month, icon grid and the dots of a page—, built as a declaration and not as a list of
// coordinates: a `ui.panel` stacks them and gives the room that is left to the one with a `weight`. The strip of tabs
// chooses the page and the dots say which one it is, so the two controls that are about "where am I" are the navigation;
// a page that is left is given back whole and built again on the same glass, and the word of `run` is the number of the
// page (`run gallery 5`), which is also how the suite builds all eight of them without a finger (docs/30, docs/90).
// **And it found one number**: `ui.dots` refuses more than **32** pages (`dots_field_valid`), which is a ceiling of the
// bridge and not of the drawing —the strip of 0.94.1 already shows a window of what fits— so a book with forty pages
// cannot say so on its strip. It is written down here because the day it matters it is a line, and because the app that
// exists to show what the system has is the one that says what the system cannot do.
// Measured on the bench: 432 native cases green, the app builds on the Paper with no field refused and each of its eight
// pages says its own name; the suite runs one `run gallery <n>` per page and asserts that name, so a page that dies says
// so instead of looking like a page nobody looked at.
// 0.95.1 (5/11): **`run gallery` with no word at all is the way a person opens an app, and it was the one that broke.**
// `app.args` is a table that exists when `run` carries an argument and not before, and the gallery read `app.args.path` on
// the first line it runs: the board answered `attempt to index a nil value (field 'args')` on the page that says what the
// system has. The guard is the one every app that reads the word already used (`app ~= nil and app.args ~= nil and
// app.args.path ~= nil`), and **the test that was missing is the one that would have caught it**: the suite only ever ran
// `run gallery <n>`, so the path was always there. It now runs it with no argument first and asserts the first page —with
// the guard taken out the test says the same line the board said— (docs/30, docs/40).
// 0.96.0 (5/11): **the whole glass is the typeface of the paper project, and it is read from the disk.** The face of the
// system was a five by seven table of the flash, so a label at `scale` 2 drew a **14 px line** —1,5 mm on a 235 ppi
// glass— and no control could be made to look like the paper project by drawing it differently: the letter itself was
// the problem (the owner, with the board in his hand: *«tot el UI actual de lunokOS és massa petit… vull la mateixa
// estètica i tipus de lletra»*). `font::set_system_face` lets the kernel point the face of the system at another one, and
// `ui::boot` loads `/fonts/dejavusans-bold.ttf` —the subset the disk already carries, eight and a half kilobytes— into a
// slot no app can release and makes it the face of everything: the bar, every control, and every text of an app that does
// not say a face of its own. **A board whose disk carries no font keeps the table of the flash and says which of the two
// it is in the log.** The step of a face of outlines is eight pixels of body and the table's is seven, so the bar asks the
// face for the height of a line instead of counting seven: its text is 32 px now and not 28, inside the same 48 rows
// (docs/30, docs/50, docs/92).
// Measured on the bench: `[I][UI] face /fonts/dejavusans-bold.ttf, 8 px of body at scale 1`, 433 native cases green, and
// the test falls when the path does not exist (docs/85).
// **What this does NOT do, and is the next block**: the numbers. A step is still 8 px, so an app that says `scale` 2 draws
// 16 px of body and the paper project's text is 32 px —the 43 px line—. Changing that is the theme of the controls
// (`ui.cpp`: radius 10, pads 6/12/18, gap 10, heights out of the line) and the sweep of the catalogue: **61 apps, 72
// `scale =` lines and 8 control-draw sites**, plus the bar (48 rows with a line of 33). It is written down here because a
// face without its sizes is half of the look.
// 0.97.0 (5/11): **the button is the drawing of the paper project, line by line.** It was a frame of three pixels with a
// bevel of two greys and a ring of focus; it is now the port of the original control: a box in the colour of the shadow,
// the face **lifted four pixels** off the bottom so the strip of box in sight under it is what makes it a key, a corner of
// fourteen, a frame of one pixel **on the face** and of three when the button is primary, and **the chosen one inverted** —
// ink face— because the focus and the finger are one signal. With the finger on it the face slides down those four pixels
// and the strip goes away, which is the half of "pressed" a bevel cannot say. `button_draw` takes the seven words the
// original control takes (`primary`, `title`, `tag`, `color`, `scale`), and the bridge of Lua passes them, so an app can
// draw the button of the system and not an imitation of it.
//
// **And with it, the rest of the controls that were named on the same day** (docs/92):
//   - the **keyboard** is the original's grid: the keys keep **twelve pixels of margin** from the left, the right and the
//     bottom of the strip, a key that is not lit is **one pixel of ink on its left edge** and not a box, the chosen one is
//     its **whole cell in ink**, the rows are divided by a line as wide as the keys and the band wears **two pixels** of
//     ink on top;
//   - the **space** says its word, as it did there, instead of a bar of pixels nobody could see;
//   - **`ui.menu`** is the original's card: paper with **three rings** of one pixel, **fifteen** of padding, **ten**
//     between the title and the options, and **every option is the button above** —so the option the dial is on inverts
//     and the one the app calls primary wears the frame of three;
//   - **`ui.menu_bar`** has the same card unfolded under its word and the entry the dial is on inverted; **`ui.dialog`**
//     is the same card with its two doors as buttons;
//   - **`ui.checkbox`** is a **ring of two pixels** and paper inside with a tick of an eighth of the box, inverted whole
//     when it has the focus, and **no bevel anywhere**: the volume of a key is its shadow, and that is the whole of the
//     drawing of this system now.
// **What this does NOT do**: the face is still 8 px of body at `scale` 1, so the words are smaller than the original's
// until the theme of the numbers lands; the row, the list, the tabs, the slider, the text boxes, the selector, the
// notification bar, the icon grid, the month, the sparkline and the pie keep their old drawing and are the next blocks.
// **And the same day the amber of the owner made the bench able to SHOW the page**: `--dump <file>` writes the glass to
// a PGM after every paint (docs/85), so what the system draws is looked at instead of guessed. Looking at it found five
// things that no test was saying and that the owner had been saying with words: the strip of tabs cut every word to
// "T... N..." because eight columns of a page of five hundred and forty pixels are sixty-seven each —**the bookmarks of
// the paper project went in** (two pixels on the left, the top and the right of the tab you are on and not on the bottom,
// so the line it sits on is not painted under it) and the catalogue of the gallery is **five pages of five words**; the
// slider was a **chip with a square knob** and it is now the paper project's: a tube of paper with an outline of ink, the
// part the value has taken filled with that ink, and a **circle of the height of the control** that is ink with a dot of
// paper while it has the focus —and it paints its whole rectangle, or a knob that moves leaves the one before on the
// glass—; the **progress** was a black bar because its fill was the ink of the text and now it is **level ten**, the light
// tone of the theme, with the ring of ink the paper project draws on it, and the track is the paper of the page; and the
// **captions of the catalogue** were at half the size of the text, which is the "it is all too small" the owner had said
// twice (docs/92).
// Measured on the bench: **433 native cases green** (90 of them in `test_ui`), the tests of the button and of the keyboard
// fall when their drawing is broken on purpose (`drop = 0` and the margin taken away), and the emulator, `paper` and
// `atom_matrix` build with no warning.
// 0.97.1 (7/11): **a page of the network no longer takes the memory the TLS handshake needs, and the handshake itself
// goes to the extra memory.** On the M5Paper the owner ran `repo` and the board answered `mbedtls_ssl_setup returned
// -0x7F00` —`MBEDTLS_ERR_SSL_ALLOC_FAILED`, the library saying it could not have a block— with the network up and the
// address resolved; in the bench the same command brought sixty folders down, because a bench has the memory of a PC.
// The first measure said where the memory was not: **28 500 B free of the chip's own RAM (26 612 in one piece) beside
// **3 650 839 B** of extra memory that nobody was using, and a handshake asks for **two buffers of sixteen kilobytes**
// plus its session. Two things changed. The **page** of a download is asked to the extra memory of the board and not to
// the chip's own (`pal::extra_memory().allocate(bytes, true)`, the rule the interpreter already followed). And **mbedTLS
// itself allocates through the platform hooks** —this core has `MBEDTLS_PLATFORM_MEMORY` on— so a block of four
// kilobytes or more is taken from the extra memory and the small ones stay home: the two buffers of the handshake are
// the biggest thing this firmware asks for and they now live where there is room. The request says **how much memory it
// had** before opening and how much is left when it cannot open, so the next run of any board answers the question with
// a number and not with the name of an allocator (docs/75, docs/85).
// Measured: `paper`, `atom`, `atom_matrix`, `tkoala` and `esp32` build with no warning. **This file is the HAL of the
// ESP32 and has no native test**: what proves the change is the board, which with **the same 28 944 B of internal RAM**
// that failed before now says `[I][HTTP] 0 B for the page, 28944 B of RAM free (26612 in one piece), 3650839 B of
// extra` and brings **61 folders and 40 251 B of JSON** down (docs/75).
// 0.97.2 (7/11): **the text is the size of the paper project.** The face was already its own —DejaVu Sans Bold, read from
// the disk— and the sizes were not: a step of `scale` was **eight pixels of body**, so the text of every page at `scale` 4
// was a body of **32** where the original's `ui36b` is **36**, and the height of a line was eight per step where the
// original's line for that body is **43**. Eleven per cent smaller, in the words whose whole job is to be read: the owner
// said it in one line —*«el tipus de lletra per defecte no és la mateixa que PaperOS i el mateix tamany»*— and the family
// was right and the numbers were not (docs/92). A step is **nine** and a line is **eleven**, so `scale` 4 is a body of 36
// and a line of 44, `scale` 3 is 27 —the original's dense text is 24— and `scale` 5 is 45, near its title of 48. And the
// **bar** went from four steps to **three**: its letters are a line of **33 px**, which is exactly the `fontSmall` the
// original puts in a title bar, inside the same 48 rows.
// Measured on the bench: 433 native cases green, and the pages of the catalogue drawn with `--dump` compare with the
// captures of the original at the same size (docs/85, docs/92).
// 0.97.3 (7/11): **el sistema té una sola aparença, i viu als valors per defecte dels controls.** The owner asked for it
// in one line —*«vull que tot el sistema usi el mateix tipus de lletra i estètica dels controls… si una app decideix
// canviar-ho ha de tenir un motiu»*— and the controls had **six different defaults** between them: a step for a label,
// two for a row, three for a text box, and every app of the catalogue saying its own. The defaults of the bridge are the
// system's now (docs/92 §11): **four steps** —a body of thirty-six pixels, the text of the paper project— for every
// control whose words are read, and **three** for the two strips that are a bar of words (`tabs`, `menu_bar`). And the
// apps were swept: **twelve files and twenty-seven lines** that only said again what the default says are gone, the
// heights of their labels went to a line of the system, and the three apps that keep a size say why —`gallery` is the
// catalogue of the three sizes, `buttons` and `label` are the pages of every size of their control—. The `dim` tone of a
// secondary word stays, because it is the theme's own (docs/92).
// Measured on the bench: 433 native cases green, the twelve apps run with no error of Lua, and two of them looked at with
// `--dump` —which is how a button with a cut word and a list whose rows walked over each other were found (docs/85).
// 0.97.4 (7/11): **the page of a catalogue can be pressed, and it changes whole.** The owner opened the app that shows every
// control on the board and said three things in one line: *«no puc pitjar res, i els botons físics només em mouen per les
// pestanyes»*, *«refresca bé al canviar de pestanya?»* and *«em sembla un nom horrible»*. All three were true. **Nothing
// could be pressed** because a page that is built again loses the control the dial was on and the walk starts at the first
// one of the tree —the strip of tabs— where the arrows change the tab, which builds the page again: the dial never left the
// strip. There is a call for it now, **`ui.focus(control)`**, and the app sends the dial to the first control of the page it
// has just built (docs/30). **The change of page left a mix of two pages** because a control paints its own rectangle and
// no more: the app wipes the page with `ui.clear(15)` before building the next one. And the app is called **`showcase`**,
// which says what it is, and not `gallery`.
// Measured on the bench: 434 native cases green —the one of `ui.focus` presses the centre key with and without the call and
// sees which button answers—, the app runs in five pages with no error of Lua, and the Paper builds with no warning.
// 0.97.5 (7/11): **the environments are named after the boards.** The owner asked for `M5 Atom`, `M5 Atom Matrix`,
// `M5 Paper`…, and PlatformIO has a rule that makes that name its own: *«the name can contain alphanumeric, underscore,
// and hyphen characters (a-z, 0-9, -, _)»* —it says so and refuses the build—, so the boards are **`m5-atom`**,
// **`m5-atom-matrix`**, **`m5-paper`**, **`esp32-devkit`** (a bare module, which is the one that has no name of a
// product) and **`ttgo-t-koala`** (the LilyGO, which is not an M5). The bench keeps `emulator` and the suite `test`,
// which are not boards. The five build, the CI and every command of the documentation and the README use the new names,
// and the directories of the old ones are gone from `build/` (docs/80).
// Measured: `m5-atom`, `m5-atom-matrix`, `m5-paper`, `esp32-devkit` and `ttgo-t-koala` build (five SUCCESS, from a clean
// build directory each, 78 to 88 s), 434 native cases green, and `pio project config` reads the seven environments.
//
// 0.98.0 (8/11): **a key paints its own answer on the glass.** `ui_button_event` takes the press before the app hears
// anything, so the ring moving to the next control and a button going down were changes in the tree that nobody put
// on the glass: the app only paints when it asks for a present, and an app waiting in `sys.delay` asks for nothing.
// The owner pressed the dial on the board and read it as *«l'app Showcase no respon a l'usuari»* — the walk was
// working (three stops and the centre delivered, measured on the bench) and the glass said nothing. This is the
// paper project's `feedback()` (`src/ui/button.cpp`: *"the pressed state is immediate feedback: it is what the user
// is waiting for"*), and it goes the same way in as the app's own present —draw what owes a draw, then compose and
// paint— so a ring costs its control's box and not the page. The showcase app also answers now: every control it
// gives the dial has an `on_click` that writes the line at the bottom, because what a press *does* is the app's.
// Measured: 436 native cases green (two new: the glass moves when the dial moves, and the centre reaches the
// control's `on_click`), and both go red when their line is taken out (docs/30, docs/92).
//
// 0.99.0 (8/11): **one stop per tab.** A strip of tabs was a single stop of the dial and ate the arrows to change
// the tab, which is the trap the owner met on 7/11 (*«no puc pitjar res, i els botons físics només em mouen per les
// pestanyes»*). The paper project's strip is a row of `Button`s the navigator walks one at a time and the strip
// never takes a key, so the walk now gives a strip one target per column: the arrows go tab by tab and leave the
// strip at either end, the centre chooses the tab the ring is on (`on_change`), and the tab that will answer is
// painted inverted, as `TabStrip::draw()` does. `focus_index` says which tab has the ring, and the finger records
// the tab it went down on. The showcase app no longer calls `ui.focus`: the dial stays where the person left it.
// Measured: 148 native cases green (a new one walks the strip and reads the inversion pixel by pixel), and both go
// red when the stop per tab or the `lit` tab are taken out (docs/30, docs/92).
//
// 0.100.0 (8/11): **a control can be disabled.** A control had no such state: `focusable = false` took it off the dial
// and off the finger and changed not one pixel of it, and the sentence that justified that —*"this glass has two
// tones and there is no way to draw a disabled row"*— was the old project's, from a two-tone panel: this glass has
// sixteen levels and has used the `dim` (8) tone for secondary words since 0.97.3. A bar of a menu already had it
// per entry (`enabled` greyed and skipped). Now `disabled` is a field of every control: its ink falls to its `dim`
// (paper and the track of a slider stay, and the ring is not painted), the walk, the finger and the keyboard all
// refuse it through the one question they ask (`ui_target_is_focusable`), writing it repaints the control and gives
// the dial back if it was there, and the state goes down the tree so the label inside a disabled button is off with
// it. The menu card keeps `hidden` —a card is the list of what can be done now— and its comment says so.
// Measured: 438 native cases green (one new: the row that is on is a block of level 0 and the same row off is 8, an
// arrow skips the middle button, the centre presses the third and the finger never touches the second), and three
// deliberate breaks turn it red: letting the walk land on it, not lightening the tones, and not telling the
// children (docs/30, docs/92).
//
// 0.101.0 (8/11): **a button inverts what it holds, and a label takes no children.** Nesting was already there —
// every control is a canvas with children and `attach_to` never looked at the kind— and it was too permissive and
// half blind: the father paints its content and the child composes over it, so a label inside a button with the ring
// painted its own paper and covered the ink face (measured: the button came out as a black bar under a white
// rectangle). The inversion now travels with the draw —`draw(self, depth, inverted)`, an argument and never a flag,
// because a draw that throws long-jumps and a flag would stay on— and the painters that own a paper/ink pair swap
// it: `label`, `row`, `checkbox`, `sparkline` and `pie`; a panel only passes it on. `label` and `dots` refuse a
// child with `E_INVAL`, which is what the owner asked for the two controls that are their whole frame. The controls
// whose items are fields (tabs, list, menu, menu_bar, month, icon_grid, notification_bar, selector, dialog) still
// take a child that is not part of any item; that is a redesign and it says so in docs/92 §15, with the limit that
// decides it: `kMaxControls = 32` canvases for the whole system.
// Measured: 440 native cases green (two new: the label's paper inside a ringed button is level 0 and its word level
// 15; and `label:add`/`dots:add` answer `nil, E_INVAL` while `panel:add` answers true), and taking the argument out
// of the painter's call turns the first one red (docs/30, docs/92).
//
// 0.102.0 (8/11): **the number of controls is what the memory holds.** `kMaxControls = 32` was a static table and it
// was three ceilings at once: how many controls a page had, how many children one control could hold (the list was
// `int8_t children[kMaxControls]` inside every canvas) and, in the bridge, how many stops the dial could walk
// (`FocusTarget[32]` on the stack of the event); the slot was an `int8_t`, which stops working at 127. The owner said
// it plainly: *«per qué hi ha un kMaxControls? vaig dir que la psram és el límit»*. The table of canvases is taken
// from the extra memory at boot —half a megabyte, 3 276 slots of 160 bytes measured, halved until the block is accepted— and
// `canvas_count()` says how many there are; a slot is an `int16_t`; the children are a linked list with no ceiling of
// its own; the list of stops is a block of the extra memory, one per event, as long as the table; and a panel lays
// out as many children as it holds. `sizeof(Canvas)` went from 192 to 160 bytes.
// Measured: 442 native cases green (two new: 150 controls on one page and forty stops walked to the thirty-sixth),
// and putting the table back to 32 turns both red (docs/30, docs/75, docs/92).
//
// 0.102.1 (8/11): **a schematic checkbox.** The owner asked for it: *«fes el control UI Checkbox més esquemàtic, un
// quadrat i una X dins si està activat o res si no ho està»*, and a reminder that it can be disabled and is drawn in a
// lighter tone. The box was the paper project's (`paintBox` + `drawCheckMark`: radius 5 and a tick of two strokes);
// it is now four strokes of a 2-pixel frame with square corners, paper inside, and **two 2-pixel diagonals** when it
// is checked —with a pixel of air so they do not touch the frame, and nothing at all when it is not—. The size
// (`cb:box()`), the colour of the cross (`mark`), the focus that inverts the square and the disabled tone are what
// they were.
// Measured: 442 native cases green, and the checkbox test reads the glass pixel by pixel (the corner is the frame and
// not a curve, the middle is the body and then the ink, and both diagonals are there). Three states looked at with
// `--dump`: off, on with the cross, and on and disabled in the lighter tone (docs/30, docs/92).
//
// 0.103.0 (8/11): **a cell of a box can claim a percentage of it.** `ui.panel(w, h, 'vertical' | 'horizontal')` is the
// VBox and the HBox of this system and it was already there —padding, gap and `child.weight` as a share of what is
// left—, but a cell could only ask for the room the others did not want, so a fifty/fifty was a coincidence of weights
// and fixed cells broke it. `child.share` (1..100) is now a percentage **of the inside of the box**, so a cell that
// says 50 is half of it whatever its brothers ask for; the three ways (share, weight, its own size) mix in one panel.
// And a bug of 0.102.0 came out with it: the bridge stopped cutting a panel at sixteen children but `panel_layout` in
// the kernel still did (`if (count > 16) return 0`), so a panel of forty rows was laid out as forty controls on top of
// each other and the test of that day did not see it because it only looked at the dial.
// Measured: 444 native cases green (two new: 50/50, 25+weight, and twenty children with the twentieth at y=190 in the
// kernel; `share = 30`/`70` and 25/75 in an HBox from Lua), and ignoring the share turns the kernel one red (docs/30,
// docs/90, docs/92).
//
// 0.104.0 (8/11): **a list of entries and not of data.** The owner: *«El control List te una llista dinàmica de
// controls ListEntry amb un UIControl fill cada entry»*. Until now a row was three words (`title`, `detail`,
// `subtitle`) painted by the kernel. `ui.list_entry(w, h)` is a control that paints the paper of its row —ink when it
// is the marked one or has the ring— and the separator, **and no words**: the app puts inside it whatever it wants.
// The list places the entries of the page it is on at `row_height`, keeps the footer, and puts the entries of the
// other pages **past the frame**, where the clip does not compose them and the walk does not reach them; the arrows
// page a list of entries as they page a list of data, and `items` stays for the twelve apps that use it.
// Measured: 445 native cases green (one new: six entries of 56 px in a 400x180 list —two fit and the third lands at
// 236— and the arrows carry it to page 2), and placing every entry inside the frame turns it red.
// The grid is what is left of the owner's request and it is not in this commit (docs/30, docs/92 §19).
//
// 0.105.0 (8/11): **a grid of cells and not of data.** The owner: *«El control Grid també te una llista dinàmica de
// controls GridEntry (normalment un HBox de 2 elements, amb una imatge i un label)»*. A cell was two words (`mark`
// inside the box of the icon and `label` under it) and nothing else. `ui.grid_entry(w, h)` is the control of a cell —
// **the same control as `ui.list_entry`, born with the separator off**, one function in the bridge with two names—
// and its content is a child the app builds. A grid that holds children paints no cell: it paints the paper of its
// page and the footer, and each child goes where the kernel says its cell is.
// Two pieces in the kernel's contract, so that the geometry is never copied: `icon_grid_draw` takes
// `cells_of_the_kernel`, and with it false the cell loop starts past the top of the page and no cell is painted; and
// `icon_grid_cell` answers the box of one cell in the frame's coordinates, or false when that cell is on another
// page —the same arithmetic the paint and the finger use (`icon_grid_cells` and `icon_grid_box`), so the finger, the
// brush and the app's control cannot disagree—. The count of the cells of a grid of entries is counted in its own
// tree, so the pages of the footer and the page of the cursor are the same number the app never has to say twice;
// `items` stays, and the cells of another page are placed at `frame_h + 40`, past the frame, where the clip does not
// compose them and the walk does not reach them. The arrows walk the cells and the page follows the cursor.
// Measured: `sizeof(Canvas)` is still 160 B and the table of canvases 3 276 slots (read at the emulator's boot); a
// 500x400 grid of four columns and eight cells is dumped as four cells per row and two rows, each with its child
// inside. **The suite was not run in this turn** (the owner said not to run anything): the two new tests —
// `test_a_grid_of_entries_places_cells_of_controls` in test_apps and the box of a cell in test_ui— are written and
// compile, and the break that has to turn them red is placing every cell inside the frame (docs/30, docs/92 §20).
//
// 0.106.0 (8/11): **who has the focus when a control holds another one.** The owner asked it with the board in his
// hand: *«veig que el UIControl amb focus (el color invertit), però els controls fills estan normals (no focused).... No
// s'hauria de propagar d'alguna manera?»*, and asked for the rule: *«fem que el control pugui declarar-se focusable o
// no ... Si un UIControl està deshabilitat, els seus fills també i un control deshabilitat no pot rebre focus»*. The
// focus is one control's and is not shared; **what is passed down is the face**: a control that turns its own face to
// ink while it has the ring, the finger or the mark says so with `__inverts` —`button`, `checkbox`, `row` and the two
// entries of 0.104.0 and 0.105.0— and what it holds is drawn on ink. Until now the list of names was inside the draw
// and had only two of them, so **a label inside a marked entry was left on paper: a white patch on the ink of the
// row**. A child added to a control that is disabled **is born disabled** (`ui_set_disabled_below` only walked the
// children that were there when the field was written), and a child the app turned off itself is **not given back**
// when the father comes back (`__off_father` keeps the two facts apart). `focusable` is a field of every control,
// `true` or `false` and nothing else, and a disabled control is not focusable whatever it says. And a fourth thing
// found looking at the focus closely: `g_ui_focus_slot` was an `int8_t`, so from the hundred and twenty-eighth control
// of a page the comparison with the control's slot never matched and **the ring was invisible**; it is an `int16_t`
// now, as the slot of a canvas is.
// Measured: the build of the kernel, the bridge and the suite compiles with no warning; the dump of a marked row and a
// ringed button, each with a label inside, is two ink blocks with the word in paper (`--dump`, looked at). **The suite
// was not run** (the owner said not to run anything in the turn before): the four new tests are written and compile —
// an entry that is marked inverts what it holds, a child of a disabled control is born disabled, a control declares
// whether it takes the dial, and the ring reaches the control past the hundred and twenty-seventh— and the breaks
// that turn them red are: taking `__inverts` off the entry, not inheriting `disabled` in `l_canvas_add`, and putting
// `g_ui_focus_slot` back to `int8_t` (docs/30, docs/92 §21).
//
// 0.107.0 (8/11): **a keyboard a thumb can press.** The owner, with the board in his hand: *«El teclat ha de ser més
// alt i amb tecles i lletres més grans, gairebé no es poden polsar. També trobo a faltar una tecla: "Backspace" (en
// icona, com l'<enter>)»*. A key was forty-four pixels tall and its letter two steps of a body of nine —eighteen
// pixels on a band of a hundred and seventy-six—, which is read at a hand's distance and not where a board is held.
// This panel reads a finger at about six pixels a millimetre, so **a key is sixty-four** now: a little over a
// centimetre, the smallest a thumb presses without looking. The band is two hundred and fifty-six and the app keeps
// six hundred and fifty-six of page (it had seven hundred and thirty-six), and the band still never takes more than
// two fifths of the view, so a short view shrinks the key instead (fifty-four in five hundred and forty, thirty-two at
// the floor). **The letter takes the height of its key** —the step comes out of the cell and not from a constant— with
// the ceiling at five, and the word of a key (`123`, `MAY`, `space`) two steps under its letter. The five is what the
// block of the antialiased glyph holds at twice its size: sixty-four pixels refused the tallest letters and the key
// fell back to the blocky text, which is exactly the letter a person cannot read; the block is **96 x 96** (1 152
// bytes instead of 512). And the backspace wears **its own symbol** and not the arrow of a "back" gesture: the
// pentagon of the box it deletes with a cross inside, drawn with the same strokes (`ink_line`) as the enter, which
// stays the arrow that turns.
// Measured: `kb:height()` says **256** and `ui.caps().height` **656** with the band on the glass (read from the
// emulator); the dumps of the alpha and the numeric bands are four rows of big keys with readable letters, the
// pentagon with the cross at the end of the row and the arrow of the enter after the dot. **The suite was not run**
// (the owner did not ask for it): the two band tests were re-pointed from 176/736 to 256/656 and from the rows of
// forty-four to the rows of the rule, and the break that has to turn them red is putting the key back to forty-four
// (docs/30, docs/92 §22).
//
// 0.108.0 (8/11): **the caret of the end of a document.** The owner saw it with the board in his hand: *«al polsar
// <enter> al teclat, si es el final del document, el cursor no baixa i queda text solapat, al escriure es veu el text
// nou a la fila seguent»*, and his diagnosis was the right one —the cursor can be after the last byte, and a document
// whose last key was an enter ends in a break—. The walk that counts the rows of a box of several lines stopped at the
// `\0`, so a text ending in `\n` had one row fewer than it has: with `"one\ntwo\n"` it counted two, the cursor of the
// end (byte 8) belonged to neither of them, `cursor_row` kept its initial zero and **the block was painted in the
// first row, over the words of the document**, while the text grew below it. The walk now goes one row further when
// the last byte it ate is a break, and stops when a row measures zero bytes —the empty row at the end, counted
// once—. The paint, the finger and the swipe ask the same measurement, so all three are right at once.
// And the editor of the glass is brought up to the system: **the footer at two steps** (28 and 24 pixels instead of
// two lines of 46 at the body of the glass, the state in ink and the note in the dim tone) with **the row of the caret
// and the whole text** in it (`row 1/67`, from `box:lines()`), **the box from edge to edge** —the twelve pixels of
// margin on each side were two hundred and forty pixels of a line of code given away— and its text at three steps,
// and **a sample page `notes.txt`** in the app's own `/res` (`apps/<id>/res/`, the kernel's rule) copied into its
// ground on the first run, 1 417 bytes, with long lines for the wrap and more rows than the glass holds.
// Measured: `box:lines()` is 3 for `"one\ntwo\n"` and the dump of the box with the cursor at the end puts the block in
// the third row (`--dump`, looked at); the editor opens the sample (`the sample page is /data/editor/notes.txt, 1417
// bytes`) and its footer says `/data/editor/notes.txt  1417 B  row 1/67` at two steps with the box touching both
// edges. **The suite was not run** (the owner did not ask for it): the new test
// `test_a_text_that_ends_in_a_break_has_a_row_for_the_caret` is written and compiles, and the break that turns it red
// is the walk stopping at the `\0` again (docs/30, docs/92 §23).
//
// 0.109.0 (8/11): **the two symbols of the band, drawn again.** The owner, one by one: *«el boto <backspace> te la "X"
// desplaçada a la dreta. L' <enter> te la fletxa feta amb dos segments. Fes el dibuixos de nou d'aquests dos botons i
// fes-los bé.»* Two drawing faults and not two designs. `ink_line` put the block of each step with its corner on the
// point of the line, so **every diagonal came out half a stroke to the right and half down**; in a cross both diagonals
// move together and it came out pushed to the right on top of that, and its arms were a fifth of the height, so the two
// strokes of the cross of the backspace reached the right side of the pentagon and read as a blob. The stroke is
// centred on its line now, the cross is measured from the **body** of the pentagon —a third of what is inside it— and
// the symbol takes **three fifths of the key** (38 pixels in a key of 64) instead of two, which is the room a pentagon
// with a cross inside needs. And the bar of the enter was given `2 * arm - head` of width, which stopped it `head`
// pixels short of the stroke that comes down: the key showed **two segments with a gap between them**. The bar reaches
// the stroke now, the corner is one piece, and the head is a triangle whose columns carry the stroke of the bar.
// Measured: reading the pixels of the band's dump, the cross of the backspace is five columns in the middle of the body
// (491..495) with eight columns of paper to the side (504..506), and the bar and the stroke of the enter are one run of
// thirty-seven columns (470..506) with no gap. **The suite was not run** (the owner did not ask for it): the new test
// `test_the_two_symbols_of_the_band_are_one_piece_each` measures both, and the break that turns it red is the stroke
// pinned at its corner again (docs/30, docs/92 §24).
//
// 0.110.0 (8/11): **the suite runs, and the bench gives the terminal back.** With the owner's leave the whole native suite
// was run for the first time since 0.104.0, and it found three things that no reading had found:
//   - **A page of more than a hundred and twenty-seven controls took the process down.** The two walks of `open_draw`
//     held the slot in an `int8_t`: past the hundred and twenty-seventh the chain was a negative number, the table was
//     read before its first slot and the bench died with a segmentation fault in `button_draw` —the same `int8_t` the
//     slot of the dial had until 0.106.0—. It is an `int16_t` now, and the page of a hundred and forty buttons of the
//     new test walks and takes its ring.
//   - **A cell that is not in the list had a place.** `icon_grid_cell` answered for any cell of the page, so an app that
//     asked for the ninth cell of a grid of eight was given a box nobody draws.
//   - **A child the app turned off was given back by its father.** `ui_set_disabled_below` wrote its own mark over a
//     `disabled` the app had set, so `panel.disabled = true` and `false` around it lost the app's decision; only the
//     child the father is the one turning off is his now.
// And `pal_host` **puts the terminal back when the process dies on a signal**: `atexit` is not reached by a fault, so a
// suite that stopped on the segmentation fault above (or a bench killed with a `kill`) left the terminal of the person
// with no echo and no line until a `reset`. The handler restores what was there with `ioctl` —the one of the two calls
// a handler may make— and lets the signal kill the process as it would have.
// Measured: **452 native cases, 452 green** in 1 min 28 s (test_ui 94, test_apps 159); the firmware of the Paper builds
// with no warning (2 308 304 B) and its disk is 372 437 B in 141 files, 58 apps, and fits the 12 451 840 B partition.
// The breaks: taking the trailing row of a text out of the walk turns `test_a_text_that_ends_in_a_break_has_a_row_for_the_caret`
// red (3 rows against 2), pinning the stroke at its corner turns `test_the_two_symbols_of_the_band_are_one_piece_each`
// red (the cross reaches the side), a bar that stops short turns it red on the other half, and not installing the signal
// handlers leaves the terminal raw —measured with a pty: `IT_WAS_RAW` and `TERMINAL_LEFT_RAW` against
// `TERMINAL_BACK`— (docs/80, docs/90, docs/92 §25).
//
// 0.111.0 (8/11): **a picture at the moments of the system.** The owner: *«Crea una app nativa C++ de sistema que
// faci una splash screen, se li passa una imatge que l'app mostra al centre de la pantalla rera haver fet una neteja
// completa del panell. Aquesta app s'executarà en diferents moments durant l'execució del sistema (boot, shutdown,
// charging...)»*. It is a module of the kernel and not an app, because the moments it exists for are the ones where
// no app is in front: **`splash` paints the surface of the board** (`hal::ISurface`) and not the canvas of a page.
// Two things and one rule: the wash is the **whole panel** (`clear(15)` and one `flush(gc16)`, the mode that takes
// the ghost with it) and the picture sits **in the middle, at its own size and never scaled** —bigger than the glass
// it is cut at its centre, and one that fits is left as it came—. The rows come from `img` four bits a pixel and go
// into one block of the extra memory the size of what fits; the file and the block the decoder asks for live there
// too and go back when the picture is on the glass.
// **The moments are words and the word is the name of the file**: `/res/splash/<moment>.png` (and `.bmp`, `.jpg`)
// and `/res/splash.png` for every one. `boot`, `shutdown`, `charging` and `sleep`; the bench carries three pictures of
// 460 x 200 drawn with the system's face, and `sleep` has none, which is what the test uses to see that a moment
// without a picture paints nothing and says why. It is turned on at `kernel::boot` right after `display::boot`, and
// at `power::sleep` before the chip stops its clocks —only on a deep sleep, because a light one is a nap—. The
// moment of the charge is there and painted by the command, but **the energy policy has no "on the cable, waiting"
// moment yet**: that is the piece left. And the shell has `splash [<moment>|hide|<path>]` to try all of it without a
// restart.
// Measured: **453 native cases, 453 green** (one new, and it is in test_apps because that is where the disk and the
// glass are); the emulator's dump says `[I][SPLASH] /res/splash/boot.png, 460x200 at 40,380` and shows the picture in
// the middle of the 540 x 960 glass with the bar over it. The breaks: taking the wash out leaves the mark of ink of
// the corner in the test (15 expected, 0 found), and painting the picture at the corner leaves the middle empty
// (docs/50, docs/92 §26).
//
// 0.112.0 (8/11): **the strip of dots is the one of the paper project, pixel for pixel.** The owner: *«Revisa el
// control original de Dots al PaperOS i compara'l amb el que has implementat a LunokOS.... et vaig dir que volía una
// copia exacta del aspecte dels controls de PaperOS a LunokOS»*. The original (`OLD/PaperOS/src/ui/page_dots.*`) has
// **19** for the balls of the pages that are not open —a ring of **2** pixels of ink with paper inside—, **25** for the
// one being read and solid, and that is the room of **every** ball, **7** of air between two of them and a rod of
// **3**, drawn before the balls; the box the design asks for is **37** —the big ball and **12** of air under it, with
// the strip at the top of its box—. lunokOS had a strip derived from the height of the box: a ball of `height / 3`
// (three to twelve), a big one of `small + 4` (sixteen at most), a ring of two or one, an air of eight or five, the
// strip centred in its box —and the two apps of the disk gave it a box of ten pixels wide and twenty-two or
// twenty-six tall, so its balls came out **seven and eleven pixels**: less than half of the original's—.
// The seven numbers are the original's now, fixed and not a third of the height, the strip is painted at the top of
// its box with the air under it, and `showcase` and `controls` give it `view.width - 2 * 12` by **37**, which is what
// the original's `preferredSize()` asked the layout for. **Two differences stay, and they are said**: the window of
// 0.94.1 when the pages do not fit (the original clamped at sixteen and the strip left the screen), and a box shorter
// than thirty-seven shrinking the whole strip by the same factor, so an app that gives it twenty pixels does not get a
// ball of twenty-five. Neither is seen with the box of the design.
// Measured: reading the dump pixel by pixel, the five balls of `showcase` are a **solid ball of 25** (rows 863 to 887),
// **rings of 19 with two pixels of ink** (the left edge at 58 and 59, the paper at 60), a **rod of three rows** (11, 12
// and 13) and the whole strip **149 pixels** wide, which is 12 + 4 * 32 + 9; the dots tests were re-pointed to the
// original's numbers and the suite is green (docs/30, docs/92 §27).
//
// 0.113.0 (8/11): **the document of the editor scrolls with a finger, and the gesture of a box of lines is the box's
// and nobody else's.** The owner: *«No aconsegueixo fer scroll al document de l'Editor»*, and then the four rules he
// wanted around it: the swipe reaches the control only while it has the focus and only while the finger is inside the
// frame it draws, a control that is given back unregisters its gesture (an app that is not closed in order leaves no
// callback behind), and both the bar and the swipe count the rows of the glass, which is what `wrap` decides.
// What was there: the move branch looked the control up by slot and generation and moved it —it never asked for
// `focused`, never asked whether the finger was still inside the rectangle, and `release()` and `__gc` left the six
// gesture globals alive, so a finger could go on moving a slot that already belonged to another control.
// Now: (1) the focus and the frame are asked at every move and the rows travelled so far are dropped when either fails
// —a window that jumps when the finger comes back is worse than one that stopped where it was—; (2) `release()` and
// `__gc` share one forgetter that clears the contact, the gesture and the tap, and the contact is marked **orphan** so
// the box the app builds in its place is not pressed by a contact that began on the one that went, until the finger is
// lifted; (3) the box the keyboard was typing into leaves the keyboard without a target; (4) `wrap` was already read
// by `ui_swipe_by` and by `multiline_layout`, and it now has a test that measures it.
// **Also found and said**: `ui.focus(box)` does nothing, because `ui.focus` walks the stops of the dial and a box of
// text is only a stop of the finger (`ui_target_is_focusable`), so an app that wants the dial out of a box has to hand
// it to a button. It is left as it is, and docs/30 says so.
// Measured: the editor on the bench with the band up has a page of **540 x 656** and a band of **256** px, and the
// finger makes the app print `editor: row N of M`; in the kernel test a box of 200 x 100 at scale 2 with ten lines of
// forty letters has **11 rows** without `wrap` (the ten lines and the empty row of the last break), **more** with it,
// **6** rows that fit, a **shorter thumb** for the wrapped page and its **last window at total - 6** in both cases.
// **Pending**: the whole suite has to be run (**458 cases**, five of them new), and the two breaks are written but not
// proved: taking the focus guard out has to turn the hand-over test red, and taking the orphan mark out has to make
// the released-box test see the `fresh` box.
//
// 0.114.0 (8/11): **the logo, the six moments of the splash and the splash of an app.** The owner: *«El logo de
// lunokOS ha de ser una lluna plena realista amb la paraula "lunok" dins la lluna i "OS" sobresortint per la dreta de
// la lluna amb el color invertit (sobre la lluna text negre amb vores blanques) fora de la lluna text blanc amb vores
// negres. Fes una versió alternativa amb la luna en plan clipart.»*, and then *«en quant a la lluna realista, em
// refería a una foto amb el fons blanc»*. And the command and the manifest: **`splash` takes `boot`, `boot-alt`,
// `poweroff`, `reboot`, `sleep`, `charging` or the path of a file, and an app that writes `"splash"` in its manifest
// gets that picture painted, washed whole, before the interpreter starts.**
// The logo is one word in two inks: `lunok` over the disc in ink with a paper edge —the moon is a bright grey and the
// edge is what keeps a letter a letter where a crater is dark— and `OS` outside it in paper with an ink edge, which is
// what reads on paper. The join of the word is the limb, so it is the moon that separates the two halves, and the edge
// is **three** pixels: at four it eats the letter and `OS` comes out solid ink (measured: a stem is nine or ten pixels
// here). `boot.png` is **a photograph** now —NASA's `as11-44-6667`, the full Moon from Apollo 11, a work of the United
// States government and so in the public domain— cut to its disc and put on the paper of the system; `boot-alt.png`
// is the same mark with the moon drawn as clipart. `tools/make_splash_images.py` draws both and `reboot.png`, fetches
// the photograph once into `~/.cache/lunokOS/` and keeps 160 KB of somebody else's picture out of the repository.
// The moments are the words a person types: `boot`, `boot-alt`, `poweroff` (it was `shutdown`), `reboot`, `charging`
// and `sleep`, six and not four. `poweroff` and `reboot` are wired to the two commands of the console that stop the
// board, and the deep sleep paints `poweroff` as it painted `shutdown`. `sleep` still has no picture, which is the
// case the tests use.
// And an app may name its own: `"splash": "res/logo.png"` in the manifest, stored in the entry (47 B) and painted by
// the system —an app cannot paint before it exists— with the same call of the moments, right before `luabind::start`;
// the first page the app presents takes it away. A name that leaves the app is dropped with a line of the log, and a
// name that is not a picture is said when it is painted: decoration does not stop an app.
// Measured: the photograph is 1273 x 1280 and the mark it becomes is 460 x 200 and **18,8 KB**; the emulator and the
// Paper build with zero warnings. The tests written: the six words in `test_the_splash_paints_a_picture_in_the_middle_
// of_the_glass` (`count()` is 6 and `kind_of("boot-alt")` is not `boot`) and
// `test_an_app_that_names_a_picture_has_it_painted_before_it_starts`, which runs an app of the bench and reads the
// glass: the two pixels of the picture in the middle, paper at the rim, the ink of a corner washed away, and an app
// without the field that leaves the corner as it was.
// **Pending**: the whole suite (this turn's tests and the ones of 0.113.0, which have not run yet) and the two
// pictures seen on the glass, which is what the owner wants to try.
//
// 0.115.0 (8/11): **the logo is the first thing a board with a glass says, and the person chooses which drawing.**
// The owner: *«El logo s'ha de veure a l'iniciar el sistema en cas de que el dispositiu tingui algun tipus de
// pantalla. Usa la comanda splash boot per mostrar aquest boot splash. La comanda cfg ha de tenir una opció per
// mostrar "boot" o "boot-alt" al iniciar el sistema. L'splash del boot s'ha d'arrencar el mes aviat possible als
// dispositius amb pantalla.»*
// **As early as it can be**: `display::boot` and the splash were in the middle of the boot —after the alarms, the
// radio, the light, the button, the sensor, the air and the touch— and a person looked at a white glass while all
// of that happened. They are now right after the mounts (the picture is a file) and the settings (the picture is
// the one the person chose), and everything else of the hardware runs **while the glass already says lunokOS**. It
// is the same call the command `splash boot` makes —`splash::show(splash::kind_of(cfg::boot_splash()))`— so there
// is one way to paint it and not two. A board with no glass answers `no glass` in the debug line, which is the only
// place that sentence can go.
// **The choice is a setting of the system**, like the unit and the keyboard: `cfg boot` lists the two drawings with
// the picture the disk carries for each word and a star on the one that comes up, `cfg boot boot-alt` chooses it,
// and the word goes to the nvs (`cfg/boot`) so it survives a restart. A word this build does not have is refused
// where it is typed, with the list, and the one that was there stays. **It is a word and not a path** on purpose:
// what is stored is a moment of the splash, and a path in the settings would be a way to make the board open any
// file at its first breath (`cfg::is_a_boot_word` asks the list of moments and not a second list that could drift).
// Measured on the bench: `[I][CFG] /cfg on sd (asked for sd), keyboard qwerty, boot boot-alt`,
// `[I][SPLASH] /res/splash/boot-alt.png, 460x200 at 40,380` and then `[I][BOOT] board=m5stack-paper caps=16/19`,
// which is the splash **before** the line that used to be among the first; the dump shows the clipart in the middle
// of the panel and paper at the rims, and `cfg boot boot` brings the photograph back. The test
// `test_the_boot_paints_the_drawing_the_settings_chose` writes two 2 x 2 BMPs —the same picture upside down— under
// the two names, chooses the word, boots and reads the pixel in the middle of the glass, and refuses `charging`.
// **Pending**: the whole suite (this turn's test and the ones of 0.113.0 and 0.114.0, which have not run), which is
// what the owner has to allow.
//
// 0.116.0 (8/11): **a box of text a person enters, a caret that is the focus's, and a frame an app can leave out.** The
// owner: *«Si un control multiline esta en mode read only no s'ha de mostrar el teclat quan el Text multiline reb el
// focus. El cursor si que hi ha de ser en qualevol cas. El Control Multiline te una vora per defecte, en aquesta app no
// vull que hi sigui, fes que el control tingui opció per mostrar vora o no. També que el "read only" ha de ser una
// propietat del control Multiline, que no aixecarà el teclat en cas de rebre focus. El control de text d'una sola linia
// igual. Quan el control de text d'una sola linia reb el focus, com el control Slider, s'ha de polsar el botó central per
// "entrar" al control i poder usar els botons físics per avançar el cursor o anar enrrere. El control multiline igual,
// però amb linies, cada toc dels botons físics puja o baixa una linia. Revisa l'app lua Editor per si els canvis li
// afecten.»*
// **The caret was the keyboard's and it is the focus's now**: `caret_poll` did nothing without a band and `kb:hide()`
// put the block out, which is the opposite of a page that is read —the caret is where the person is, band or not—. Hide
// hides the letters and not the place, and the block goes when the focus goes.
// **`readonly` is a field of both boxes** and the system asks it before putting a band up: a box that is read takes no
// keys, is never overruled by a `kb:show()`, and still has its caret. It is what the editor said with `keyboard = false`
// before, which threw the block away with the band.
// **The dial stops on a box of text** —the finger's only since 0.85.2— because entering with the centre needs arriving
// there first, and a board with keys and no touch had no way in. The ring on a field is the caret and **no band**, which
// is what the centre buys; `ui.focus(box)` works from here too, which was the limitation 0.113.0 left written.
// **The centre enters and leaves the box** (`editing`) and inside it the two keys are the caret: one **letter** a press
// in a box of one line —a letter is a code point and not a byte (`textbox_caret`)— and one **row** a press in a box of
// several (`textbox_multiline_row`), the rows of the glass and not the lines of the file, keeping the column as near as
// the row allows. `top` goes back to `-1` on a key, and the app hears `on_move(self, cursor)` before the paint so its
// own line under the page goes out in the same wash. The arithmetic is the kernel's for the same reason a slider's value
// is: a row depends on the font and on the window.
// **`framed = false` is no frame**: the control paints its frame in `border` and its inside in `background`, so luabind
// hands the second as the first and the paper reaches the edge. A flag and not a fourth colour, because what an app says
// is "no frame".
// The editor was looked at and changed: `box.framed = false`, `box.readonly = doc.readonly` and an `on_move` that keeps
// the row of the line under the page. Measured on the bench: the page of the editor has no frame and the text starts at
// the edge of the glass, and the emulator and the Paper build with zero warnings.
// The tests written: `test_the_dial_stops_on_a_text_box_and_the_centre_enters_it` (band off on arrival, on on entering,
// and the button still reachable), `test_a_read_only_box_has_a_caret_and_never_a_band` (both boxes),
// `test_the_keys_walk_the_rows_of_a_box_the_centre_entered` (rows 4 and 8 of three lines, and no move before the
// centre), `test_the_keys_walk_the_letters_of_a_line` and `test_a_box_without_a_frame_leaves_its_edge_in_paper`.
// **Pending**: the whole suite (these five and the ones of 0.113.0 to 0.115.0, which have not run) and the finger and the
// dial on the board.
//
// 0.117.0 (8/11): **a slider is entered with the centre before its keys are its own.** The owner: *«Quan un slider rep el
// focus, pujar i baixar amb els botons físics significa continuar amb el funcionament normal del focus. Certs controls
// s'han d'activar (botó central) per poder-los usar: per pujar o baixar el valor del slider, perdent els botons per
// canviar el focus. [...] Al rebre el focus, els botons continuarán com sempre (canviant focus) s'ha de polsar el botó
// central per "entrar" al slider i llavors si escollir el valor amb els mateixos dos botons (amunt/avall). El botó
// central torna els botons físics al sistema i torna a servir per moure el focus.»*
// **Until 0.116.0 a slider took the arrows for having the focus** (`ui_step_value`), the rule of the paper project: a
// control with a value is moved by the keys and gives the key back at the end of its range. On a page with a slider in
// the middle that meant **there were no walking keys**: the arrows moved the number when the person wanted the control
// below. Now the focus is the focus and the centre is the door —the same shape the text boxes got in 0.116.0—: outside,
// up and down walk the dial; the centre enters it (`editing`), the two keys are the number from `step` to `step` with
// `on_change(self, value)`, and the centre leaves it and gives the keys back. The app hears both presses at
// `on_click(self)` and can read `self.editing`, and `editing` clears itself when the dial leaves the control, so coming
// back means saying "enter" again.
// **The rule is a function and not a case** (`ui_enters_to_move`): what a control has to fulfil to be entered is **to
// have no action of its own on the centre**. Today it is the slider and nothing else — a menu, a dialog, a calendar, a
// grid and a list choose something with the centre, and taking that meaning away is another change and not this one.
// It is written in docs/30 and docs/92 §32 so the list of "controls like the slider" is a decision and not a guess.
// Measured: the emulator and the Paper build with zero warnings. The test written is
// `test_a_slider_takes_the_keys_only_after_the_centre` (with the ring on the slider a down key prints no `value`; the
// centre prints `click slider true`; two downs print `value 6` and `value 7`; the centre prints `click slider false`;
// and a down after that moves nothing).
// **Pending**: the whole suite (this test and the ones of 0.113.0 to 0.116.0, which have not run), which is what the
// owner has to allow, and the slider in the hand on the board.
// 0.118.0 (8/11): **the stack the board found on a page of pictures, and the images app brought up to the controls of
// the system.** The owner, with the board in his hand: *«***ERROR*** A stack overflow in task loopTask has been
// detected»* after three pictures of `run images`, and two questions: *«El carregador d'imatges usa PSRAM?»* and
// *«Aprofita per modernitzar l'app images amb els controls UI nous i les seves mecàniques.»*
// **The loader did use the PSRAM and that was not the bug**: the file and the working block came from
// `pal::extra_memory()` since the pictures were added, but **the state of every decoder was in locals**, which is the
// stack of the task that runs an app — the one that prints `stack 7320 B free` at boot. A GIF kept the LZW table
// (12 288 B), its string (4 096 B) and a copy of the row (2 048 B) in one frame: 16 656 B against 7 320, which is a
// restart and not a warning. Measured with `-fstack-usage` (`python3 tools/check_stack.py`, the table is in docs/75):
// **`decode_gif` 16 656 → 256 B, `decode_jpeg` 6 432 → 912 B, `jpeg_info`/`jpeg_scratch` 3 504 → 90 B, `decode_png`
// 2 320 → 256 B, `decode_bmp` 2 288 → 240 B, `decode_ico` 2 288 → 208 B, `decode_pnm` 2 256 → 208 B.** The biggest
// frame left in the whole firmware is `kernel::shell::execute` at 2 272 B, in the console's task.
// **Where the bytes went**: into the block the caller already reserved, behind what the picture asks for. Every family
// has a `*_state_at` that says where its state begins and a `*_state_bytes` that says how much it is, and the sum is
// what `scratch_bytes()` answers — a number that is now a contract: a block one byte short is `kNoRoom` and nothing is
// written past it (`test_a_block_that_is_smaller_than_the_picture_needs_is_refused`). The row every decoder hands to
// its `RowSink` —2 048 B, the widest a page can be— is in the block too, and the GIF stopped copying a row it already
// had there. `tools/check_stack.py` is the guard: it compiles a library with `-fstack-usage` and fails over a budget
// (1 024 B for `lib/img`), it says which libraries need the board's headers instead of crashing, and it runs without
// permission because it executes nothing of the firmware (docs/80).
// **The images app was one of the old ones** and is a page of controls now: **a grid of nine cells** (`ui.icon_grid`)
// with the mark of the family and the extension under it —the dial walks the cells, the chosen one is inverted, the
// one of the dial wears the ring and the centre picks with `on_pick`—, **the numbers and the sentence of the format in
// a box that is read** (`readonly`, `framed = false`, 0.116.0) which the centre enters so the arrows walk its rows and
// which never raises the keyboard, **a strip of dots** (`ui.dots`) where there was a `1 / 9` painted with `ui.text`,
// and **one button**, `quit`: `back` and `next` are gone because the grid is the way. Found with the page in the
// emulator's hand: at `scale` 3 the cell name `bmp8` was cut to `bm...`, and a grid of five columns gives 103 px of
// cell, so the names went to `scale` 2, the dense text of the paper (docs/92 §33).
// Measured: the emulator builds with zero warnings and the page draws in 470x353 with the nine cells and the box of the
// format under it (the dump of `run images`); the test suite compiles with 467 cases. **Pending**: the whole suite
// (this turn's two tests and the ones of 0.113.0 to 0.117.0, which have not run), which is what the owner has to
// allow, and a page of nine pictures in the hand on the board.
//
// 0.118.1 (8/11): **the suite ran for the first time after this work and found five red tests, one of them a bug of the
// system.** The owner: *«fes tots els test i trencades»*.
// **The bug was the order of two lines**: the picture a manifest names was painted **before** `ui::attach()`, and the
// attach washes the glass whole when the page before it had painted something (`g_levels`), so the splash of an app
// could be washed away before the app ran a single line. The paint is after the attach now, and the break proves it:
// with the picture first the middle of the glass reads **15** (paper) and with the picture after it reads **0** and
// **0** —the two pixels of the picture— and `test_an_app_that_names_a_picture_has_it_painted_before_it_starts` is red
// in the first order and green in the second (docs/30, docs/40, docs/92 §33).
// **The images app had two bugs that its test found on first run**: `grid.on_pick` called a `draw` that was written as a
// `local function` **below** it, so the closure held a nil global and picking a cell drew nothing; and the page left the
// focus on the paragraph —the first focusable control it builds— instead of the grid, so the first arrow key walked the
// rows of the text. It forward-declares `draw` and asks `ui.focus(grid)` after the three places, and the test presses
// one key, picks the second family and reads `2 of 9` and `BMP` in the console.
// **Three tests could not have passed and had never run**: two read pixels of a panel that was off —the bench is born
// with everything off, so a test that reads the glass has to turn it on with `emu::Options.display`— and the third, of
// 0.116.0, expected the centre to *leave* a box that has the band up, where the three keys belong to the keyboard: it
// now types a letter, the app hides the band, the centre leaves the field and the up key walks to the button.
// **Measured**: `pio test -e test` gives **467 cases, 467 green in 1 min 30 s**; the emulator builds with zero warnings;
// the Paper is **2 300 561 B of flash (54,8 %) and 117 148 B of RAM (2,6 %)**, and its disk is **416 065 B in 146 files
// and 58 apps**. **The breaks, each with its own red**: the focus guard of the swipe (2 red), the orphan contact (1), a
// read-only box asking for the band (3), the slider gate (2), the block a decoder is given (1 red and the run dying of
// the overrun), the pick of the grid (1) and the picture before the attach (1).
//
// 0.118.2 (8/11): **the panel said no to the second picture and the splash had not waited for the ink of the first.**
// The owner, with the firmware on the board: *«lunok> splash boot-alt / splash: the panel did not take the picture»*.
// **It was not the picture, it was the paint before it**: `It8951::flush` waits up to three seconds for the ink of the
// previous paint to be over (`wait_paint`) and the wash of the whole glass at boot moves it for longer than that, so a
// `splash` typed two seconds after the boot was refused. Three things came out of it:
// **the splash waits for the ink** (`settle` in `splash.cpp`: `idle()` every 20 ms up to 6 s) before asking for its
// own paint — nobody is waiting on a page while a logo goes up, and losing the drawing to two seconds is the bad
// trade—; **the message carries the HAL's reason** (`the panel did not take the picture: the previous paint did not
// finish`), because a no without a reason is an afternoon finding out which of the five cases it was; and **the bench
// can say no too** (`--ink-refuse`): while the ink moves, `flush` answers false with the driver's own reason, which is
// what makes the waiting testable instead of lucky.
// The test written is `test_the_splash_waits_for_the_ink_of_the_page_before`: the first drawing leaves the whole page
// moving ink, the bench refuses a paint, and the second drawing lands anyway because the splash waited; taking `settle`
// out answers false and the glass keeps the first drawing (docs/50, docs/85, docs/92 §33).
// **And the second half of it was worse**: the owner, the same turn, with the board on: *«l'splash sembla que bloqueja
// l'eink? les apps s'executen però no mostren res a pantalla»*. The splash blocks nothing — **the page the panel refused
// was thrown away**: `paint_root` emptied the list of what was waiting whatever happened, so a `flush` refused was a
// page lost for ever, and the wash of the boot moves ink for longer than the three seconds the driver waits, so the
// first page of an app fell into it. An app that presents **once** and then waits —`run images`, half the disk— kept
// the logo of the boot on the glass: it runs and nothing is seen.
// **What did not go up is now owed**: `paint_root` puts the refused regions back in the list (`kept`, a copy in its own
// order) and raises a flag; **the turn of the kernel** asks `ui::owes_paint()` and only then pays an `ui::idle()`, which
// is where the page is asked for again with nobody doing anything; `ui.idle()` of an app does it too, so a page that
// waits for the ink sees its own page arrive; and the paint is only asked for **when the panel says the ink is over**,
// which is what keeps a turn from spending three seconds inside `wait_paint`.
// The second test is `test_a_page_the_panel_refused_is_painted_when_it_can`: the bench says no while the boot's picture
// moves the ink, an app presents once and stays, the console says `present nil the previous paint did not finish`, the
// glass is still paper where the square goes… and after a few turns **the square is there**, with the app presenting
// nothing else. Emptying the list again leaves it without the square for ever (docs/30, docs/50, docs/92 §33).
// **Pending**: running the suite of this turn, and the board with the two repairs.
//
// 0.118.3 (8/11): **no board carries apps in its littlefs image.** The owner: *«Cap placa ha de contenir apps al
// littlefs al flashejar (fes les imatges littlefs sense apps)»*, after `pio run -e m5-atom -t buildfs` stopped with
// `make_disk: **this disk does not fit**: the tool leaves 2 of the 148 files out of 1441792 B`.
// **The board environments say `custom_apps_in_disk = no` now** and their image is the system alone: **13 files and
// 79 288 B**, measured on the ATOM, the Matrix, the bare ESP32 and the T-Koala. **The Paper names three** —the owner
// wrote `custom_apps = editor,controls,images` himself— and its image is **29 files and 181 618 B**; a list wins over
// the filter and every name that does not name the board stays out **and says so**, which was not true of the option
// before: `custom_apps` was documented as "exactly those, whatever the manifests say" and the board filter applied to
// it all the same (found by asking the file what it really does, 8/11). `test` keeps
// carrying every app as copies (its own tests write to the disk they mount) and the bench carries none, as it has since
// 0.88.2. The two ways of asking for apps are still there, and are documented now instead of being the default:
// `custom_apps = id1,id2` for an exact list, and `custom_apps_in_disk = yes` for the ones that name the board by id or
// by family — measured at **29 files and 90 182 B with eight apps** against the 1 441 792 B partition of a 4 MB board
// (docs/70 «Com afegir apps a la imatge del disc», docs/80).
// **Why that is the right default**: the 148 files of `apps/` (418 720 B on the host) need more than the whole
// 1 441 792 B partition of a 4 MB board —the tool left two of them out and PlatformIO still said SUCCESS— and an image
// that carries apps is an image that is stale the day an app is touched, which is every day. A board with the clean
// image is not a board without apps: `repo install <id>` puts them in `/apps` of the disk and a card is read at
// `/sd/apps`. The line `make_disk` prints for such a disk says which empty disk it is: `no apps: the system alone`.
// **Pending**: the suite of this turn (the two tests of 0.118.2 and the ones of this change go with it) and the board.
//
// 0.118.4 (8/11): **the glass stopped painting for ever because a flag of the controller was read and never cleared.**
// The owner, with the firmware on the board: *«l'splash continua "segrestant" el canvas del sistema ??? … (no es veu res
// a pantalla) … el splash no marxa del eink»*, with `run editor` and `run images` printing their lines and nothing on
// the screen.
// **The splash was not stealing anything and it was not the canvas**: `It8951::engine_over()` reads `LUTAFSR` (0x1224)
// to know whether the LUT engine is still moving ink, and the maker's own code **clears that bit with a write of 1**
// once it has waited long enough. This driver only read it —docs/50 said "the maker's driver waits (`CheckAFSR`)" and
// never "and clears it"—, so one bit that stayed up left `idle()` saying "not over" for ever, `wait_paint` refusing
// every paint after its three seconds, and the system with a glass that could not be painted again: **the logo of the
// boot goes up because it is the first paint of a boot, and nothing else ever does** — the app runs, its lines come out
// on the console, and the screen keeps the logo. That is the report, word for word.
// **The engine is given `kStuckMs` = 6 s** —more than the longest paint this panel does, a whole page in `gc16` moves
// its ink for three or four— and then the flag is cleared and the log says it:
// `[W][PANEL] the ink engine kept its flag up for N ms; cleared (LUTAFSR)`. `wait_paint` does the same when its own
// three seconds run out on a paint older than that, because a page over fresh ink is a faded page and a glass that
// never paints is a dead board (docs/50, docs/92 §33).
// **And the UI says it out loud now**: `[W][UI] the glass refused a page (x,y wxh, du/gc16): <the panel's reason>` for
// every paint that does not go up, and `[I][UI] the page the glass refused is up` when the retry of 0.118.2 finally
// lands it. A page that does not go up is the difference between an app that works and an app that runs with a dead
// glass, and until now only the return of the app's own `ui.present` said it, which every app in the disk ignores.
// **Pending**: the tests of this turn (the two of 0.118.2 and the ones of 0.118.3 go with them), the commit, and the
// board with 0.118.4 in the hand —the `[W][PANEL]` line is what tells whether that flag was the one.
//
// 0.118.5 (8/11): **the flag of the ink engine is a latch and the driver answers it; the wait is a number.** The log the
// owner sent with 0.118.4 on the board decided it, and it is worth writing down as it came:
//   [W][PANEL] the ink engine kept its flag up for 9560 ms; cleared (LUTAFSR)
//   [W][UI] the glass refused a page (12,486 516x120, du): the previous paint did not finish
//   [W][PANEL] the ink engine kept its flag up for 6001 ms; cleared (LUTAFSR)
//   [I][UI] the page the glass refused is up
// Three things it says and one it does not: the glass **paints again** (the page came up, so the system of 0.118.2 was
// right and the driver was the fault), the flag **stays up after every single paint** —six seconds later it is up
// again, so it is a latch that the hardware never takes down—, and with the healing of 0.118.4 **every page costs a six
// second stall**, which is no way to use a board.
// **What the maker's own code does is acknowledge it**: a write of 1 to `LUTAFSR` before the paint, not a read after
// it. The driver does that now, at the head of `flush`, and what it waits on is the two things that do not lie: the
// **BUSY line** of the panel, which it holds low while it works, and **the ink time of the mode**, which is the maker's
// own table (docs/95): 120 ms for `du` and `du4`, 290 for `a2`, 350 for `gl16` and `glr16`, 450 for `gc16` and
// `gld16`. The wait can only be longer than the datasheet, never shorter, and it costs a pin read and a counter.
// `the ink engine kept its flag up` is gone with the healing that needed it; if `[W][PANEL] the previous paint did not
// finish in N ms` ever appears, a paint is still moving ink after three seconds and no mode of this panel takes that
// long, and the line says so instead of guessing.
// **Pending**: the tests of this turn (0.118.2, 0.118.3 and this), the commit, and the board with 0.118.5: the page of
// `run images` should come up **without** a refusal line and without six-second waits.
//
// 0.118.6 (8/11): **the driver writes down every paint it sends, because the two paths are the same call.** With 0.118.5 on
// the board the owner sent the log of `run editor` and of `run images` and there were **no refusals at all** — not one
// `[W][PC]` line, not one `[W][UI]` line — and the glass still kept the logo of the boot (*«CONTINUA SENSE FUNCIONAR.
// QUE OSTIES FA L'SPLASH DE DIFERENT ? l'splash arrenca, neteja la pantalla, posa la imatge i es mor»*). The splash and
// the page of an app reach the controller through **the same `flush`**: the same `LISAR`, the same `LoadImageArea`, the
// same `DPY_AREA`. If one goes up and the other does not, the difference is in the numbers, and the numbers were not
// written down anywhere.
// **Now they are**: every paint logs its rectangle and its mode —
// `[I][PANEL] paint 48,0 912x540 mode 2 at 0x001236e0 (page 0,48 540x912, 540 rows of 456 B)`—, a paint that finds
// nothing marked says so at debug (`a paint was asked with nothing marked`), and every `blit` the UI makes says what it
// put down and where (`blit 0,48 540x912 of a line of 270 B`). The line costs one log call per paint and it is the
// measurement that turns "the page does not appear" into "the page is sent with these five words and the controller does
// not paint it".
// **Pending**: the two lines of the splash and of the page in one run, the tests of this turn, the commit.
//
// 0.118.7 (8/11): **the panel only paints a rectangle that starts at the left edge of its own memory.** The log of 0.118.6
// decided it, and the pattern is exact — every paint of one night, on the board:
//   paint 0,0 960x540 mode 2   (the splash)           → on the glass
//   paint 0,0 48x540  mode 2   (the bar at boot)      → on the glass
//   paint 48,0 912x540 mode 2  (the page of `images`) → nothing
//   paint 56,12 40x516 mode 1, 484,12, 616,12, 876,12 (its four controls) → nothing
//   paint 4,104 36x330 mode 5, 4,440 36x88 (the bar's clock) → nothing
// The numbers inside each rectangle were right (`page 0,48 540x912`, 540 rows of 456 B) and the controller **accepted
// the command**: what it loaded is not what it showed, and the glass kept the page before —the logo—. A rectangle whose
// `x` is not 0 does not cover the whole row of the image it belongs to, and this controller does not walk it: **the load
// and the paint start at native column 0** now, and the pixels between the edge and the change are paid with the change.
// The bar's own pixels are in the driver's image, so repainting them is repainting the truth.
// **And a paint that cleans cleans the whole glass**: a `gc16` of a region leaves the ghost of everything outside it,
// which is what a change of page is; it is done for the paints that already cover half the glass or more —the splash and
// the first page of an app— and not for the ones that move a number, which ask for `du` (docs/50, docs/92 §33).
// **Pending**: the page of `run images` on the board with this, the tests of this turn, the commit.
//
// 0.118.8 (8/11): **the engine says what it thinks, three times per paint.** The owner, with 0.118.7 on the board and the
// page of `run images` still not on the glass: *«m'estás prenent el pel o algo?»*, and the question that names the whole
// problem: *«que fa l'Splash que no faci qualsevol altra app?»*. With 0.118.7 the page sends **the same command as the
// splash** —`0,0 960x540 mode 2`— and does not appear, so the rectangle was not it: either no paint after the first one
// of a boot reaches the controller at all, or the pixels of the page never reached the driver's image.
// **The one number that tells them apart** is the flag of the LUT engine, read three times: before the driver clears it,
// after clearing it, and after the `DPY_AREA` is sent —
// `[I][PANEL] engine flag 0001 -> 0001 -> 0001`—. A `0001` that survives our write is a controller that ignores every
// display command after the first, which is what the board looks like; a `0001 -> 0000 -> 0001` is a flag that works and
// a page whose pixels are not in the image. The experiment that costs no flash is `splash boot-alt`: the clipart is a
// different drawing, so a glass that changes says the paints arrive and a glass that does not says they do not
// (docs/50, docs/92 §33).
// 0.118.9 (8/11): **the panel was never selected again after the splash, and the CS was this board's
// own file that took it away.** The line of 0.118.8 answered itself on the board: the logo of the boot
// paints with `engine flag 0000 -> 0000 -> 0000` and **every** paint after it reads `ffff -> ffff ->
// ffff`, which is a MISO nobody drives —the controller is not listening, the glass keeps the last page
// because e-ink does—, and no rectangle, no mode and no `splash hide` changes anything.
//
// **Why the splash and nothing else.** `PaperPlatform::caps()` parks the CS of the panel before the bus
// is used (`deselect_panel()`: GPIO15 as a plain output held high, because that pin is a strapping one
// and the panel comes up *selected*, which made the card unreliable — measured on the board, 31/10).
// That call was written when this board had no display driver. Since 0.115.0 the boot paints the splash
// **before** anything asks for the capabilities, so the panel is already an SPI device with CS 15 when
// `count_caps()` runs, and parking the pin takes the pad away from the peripheral: from that moment the
// controller is never selected again, its reads come back `ffff` and every `DPY_AREA` is accepted and
// shown nowhere. The order was the whole of it: first paint before the caps, everything after them.
//
// **The fix**: the park happens **only while the panel is not a device of the bus yet** (`panel_joined_`,
// set by `display()`), so the card still gets a bus without a second listener and the panel keeps its CS
// for the session. And under it, a net that was not there: `It8951::flush` reads the engine flag before
// every paint anyway, and a `ffff` now brings the controller back (`revive()`: the rail down and up, the
// handshake again, the whole image sent to a memory that came up empty, at most three times and then the
// driver says it out loud) instead of painting into nothing for ever (docs/50, docs/95).
// **Pending**: the reading of the two lines on the board (the splash, and the page of `images` after it),
// the tests of the turn and the commit.
// 0.118.10 (8/11): **a radio that refuses says which refusal it was.** The Paper answers `wifi scan` and
// `provision` and the ATOM answers *«wifi: the scan failed, E_BUSY»* and *«provision: nothing came of
// it, E_BUSY»*, which is the same four letters for four different faults: a radio working on something
// else, a radio that never came up, one the chip would not initialise —most likely out of memory, this
// band has no PSRAM and the radio is initialised lazily on the first command—, and a scan the driver
// turned down. `INet::last_error()` gives the driver a voice, the esp32 one fills it at every step that
// can fail, and the kernel hands it to the person instead of `E_BUSY`, with **the free heap and the
// largest block** next to it: `ESP_ERR_NO_MEM` with 4 KB free is a fragmented heap and not a fault, and
// the number is what tells them apart without another flash (docs/30, docs/50, docs/75).
// The bench can reproduce it (`Options::refuse_why`) and the new case is
// `test_a_radio_that_refuses_says_why`.
// **Pending**: the ATOM's line, the tests of the turn and the commit.
// 0.119.0 (8/11): **the home app**: the board can come up inside a Lua app instead of at the prompt. That is what
// a launcher, a clock or the page a person wants to see is, and until now the only way to have one was to type `run`
// every time —or to leave an app open and go to sleep, which is the session record and not the same thing—.
//
// Three pieces and no more. `LUNOK_HOME_APP` in `platformio.ini` is the **default of each board** (the id of an app
// of the disk, or empty for the prompt, and it is empty in every environment of this tree so nothing changes for
// anybody who does not ask); `cfg::home_app()` keeps the name in the board's own store, so `cfg home <id>` overrides
// the default without a flash and `cfg home none` puts the prompt back; and the boot starts it **last and only when
// nothing came back**, because the session record is the more precise answer —a board that went to sleep inside an
// app returns to that app— and a home app never starts on top of anything. The name is checked against the door of
// the apps where it is typed (`cfg home ghost` is refused with the reason), and a name that the disk has lost is a
// line in the log and the prompt: a board that insisted would be a boot loop nobody asked for (docs/30, docs/40,
// docs/90).
// The `home` key is in `cfg` with the same shape as `keyboard` and `boot`, `cfg` prints it in its listing, and the
// two new cases are `test_the_home_app_comes_up_with_the_board` and
// `test_a_home_app_that_is_not_there_leaves_the_prompt` (471 -> 473, compiled).
// **Pending**: the same line of the ATOM, the tests of the turn (none executed) and the commit.
// 0.120.0 (9/11): **the manifest reads the five keys the apps of PaperOS wrote and this system ignored**. Its
// twenty-four apps under `data/app/` all carry `author` and `description`, twenty-three carry `icon`, four carry
// `opens` as a *list* of extensions and one carries `opensArgs`; in this tree sixty-one apps carried `author` and
// forty-nine `description` in their manifests and the reader touched neither, `opens` held one extension in a
// sixteen-byte field, and `opensArgs` did not exist at all.
//
// Five things and nothing else. `author` (31 B) and `description` (159 B, `\n` kept, because the descriptions of
// PaperOS are two lines) are read and shown, and nothing is refused when they are missing: they are decoration.
// `opens` is now **a list of up to eight names of eleven bytes**, written in one string the way PaperOS writes it
// —`"txt md lua json"`—, and every name is a handler: `find_for_extension`, the question the shell asks and the
// jump `open` does all walk the list through the same `apps::handles`, so a second name is worth as much as the
// first. A name that is not an extension is dropped with a line that says it; a list longer than eight keeps the
// first eight and says both numbers —unlike `boards`, which refuses the whole list, an app that opens eight of
// fifteen extensions is more use than one that opens none— (docs/40). `opensArgs` reaches the app as
// `app.args.flags` and the system does not read it: only the app knows what its own switches mean, and a value
// that does not begin with `-` is dropped with a line instead of being handed over as a flag. `icon` is checked
// as the splash is checked —inside the app, no `..`, no absolute path— and **nothing paints it yet**, because the
// page that would list the apps on the glass does not exist; `apps info` says it (docs/40, docs/90).
//
// And the command that makes them visible: **`apps info <id>`** reads the manifest of an app of this disk and
// shows the card `repo info` already showed for one of the repository —one card, two callers, `label` and `where`
// are the only difference—, with a line per key, a long dash for what the manifest does not say, `icon` and
// `opensArgs` added to the rows, and `no such app` for a name the door does not have instead of an empty card.
//
// The sixty-one manifests of `apps/` gain `author` and `description` (61 of 61 each, up from 48 and 49, the twelve
// apps that lacked a description being described from their own code, one line each), and `editor` —the app that
// hands a person somebody else's file— is the first that uses the two new keys for something: `"opens"` with eight
// extensions and `"opensArgs": "-ro"`, so `open` of a `.md`, a `.json` or a `.lua` finds it and the page starts read
// only until View turns it off. Measured on the bench (m5stack-paper, `--flash` the tree): `apps info editor` shows
// the nine rows, and `open editor data/res/hola.txt` prints `editor: -ro, the page is read only` and paints.
//
// **Two kilobytes moved from the stack to the RAM** (a decision taken here, and it is reversible): the buffer that
// reads a manifest and the one `apps info` prints were locals, and the chain that starts an app —`execute`,
// `run_app`, `load_source`, `for_each`, `on_entry`, `load`— came out at 6,7 KB of the eight the task has. Neither
// function can be re-entered (one walk at a time, one command at a time, on the same task), so both buffers are
// static now and the chain is ~5,6 KB, less than before this version. The price is measured: the ATOM Matrix's
// internal RAM goes from 119 096 B (36,3 %) to 121 144 B (37,0 %) and the flash from 2 218 733 to 2 218 681 B
// (`tools/check_stack.py --all --budget 4096`, every frame under 4 096; docs/75).
//
// The four new cases are `test_an_app_opens_a_list_of_extensions`, `test_the_switches_of_a_manifest_reach_the_app`,
// `test_apps_info_shows_the_keys_a_manifest_carries` and
// `test_a_manifest_that_names_more_extensions_than_the_system_carries_says_so` (473 -> 477, compiled).
// **Pending**: the tests of the turn (none executed) and the same line of the ATOM.
// 0.121.0 (7/11): **the first app of the paper project ported here, and the one call it needed**. `calculator`
// is the paper's pocket calculator, key for key: nineteen keys painted by hand, a display box with the waiting
// operation above and the number below, the twelve digits of the original, `-0` shown as `0`, the font stepping
// down until the number fits, an error that clears on the next key, and `nil` as the only answer to a division
// by zero. Measured on the bench (m5stack-paper, `--flash` the tree): `run calculator keys` prints
// `Calculator display 16 16 508 200` and the centre of the nineteen keys, and the page is painted once with
// `gl16` (540x912); a key paints the box alone (508x200) with `a2`, which is the original's rule —a digit costs
// a box and not nineteen keys— said with this system's qualities (docs/30, docs/50).
//
// **The finger is polled and not taken from a gesture**, and that is the port's one real difference: the
// system says a `tap` 350 ms after the finger leaves and merges two taps inside that window into one `double`,
// which on a calculator is a digit that arrives late and two keys that become one. So the app reads
// `touch.read()` —the frame of the glass in its own view's coordinates— and presses on the first frame of a
// contact, and drops the gestures it does not use (docs/30, docs/50).
//
// **The new call is `ui.line_height([scale])`**, the other half of `ui.text_width`: `ui.text` puts the top of
// the line at `y`, so centring a glyph in a key is `y + (h - line_height) / 2`, and the number cannot be written
// in the app because the two faces of this system do not agree —the bold face of outlines on the disk is eleven
// pixels of line per step and the five by seven of the flash is seven— (docs/30, docs/92). The kernel answer is
// `ui::font_line_height`, which is the same `face.height * scale` that `draw_text` puts down as damage.
//
// Two new cases, `test_the_line_height_of_a_face_is_what_centres_a_text` (the call, against the face the bench
// boots with) and `test_the_calculator_of_the_paper_project_adds_with_the_finger` (the app from the disk, run
// with real fingers on the bench's panel: `7 + 3 = 10`, `8 / 0 = Error` and the key that clears it,
// `12 = 12` after the backspace, `5.5 + 1 = 6.5` and `-5 + 1 = -4`), so 477 -> 479 cases. **A key repaints the
// display box and not the page is asserted with the region the log prints** —under 508x200, and never the
// 540x912 of the page— (docs/50).
// **Pending**: the tests of the turn (none executed) and the same line of the ATOM.
// 0.121.1 (7/11): **the calculator's page was noticeably smaller than the original, and the reason was the
// unit**. The port asked for its glyphs in steps of the face —3 for the keys, 4 for the number— and the paper
// project's calculator asks for the four sizes of its own typography, which are **pixels of line**: `small`
// 33, `body` 43, `title` 57 and `big` 82 (`lib/gfx/fonts/fonts.h`). A step of this system's face is 11 px of
// line with the bold face of the disk, so the keys came out at 33 px where the original had 57 and the number
// at 44 where it had 82: the page was half the size, and its owner saw it on the glass.
//
// The app asks for the size in pixels now and turns it into steps with the call of 0.121.0 —`scale_for()` is
// `pixels / ui.line_height(1)`, rounded— so the page is the same physical size on a board with the face of
// outlines (a step of 11) and on one with the table of the flash (a step of 7), and the ladder of the number
// is the original's: big, title, body, small. Measured on the bench with the font of the disk (m5stack-paper,
// `--flash build/disk/emulator --sd` the tree): `Calculator sizes 33 44 55 77` against the 33/43/57/82 of the
// original —the exact number of the hint, the keys at 55 instead of 57 and the number at 77 instead of 82,
// which is the closest whole step of eleven to eighty-two—. `run calculator keys` prints that line, so a page
// that loses a step fails a test instead of looking wrong to one person (docs/30, docs/92).
// **Pending**: the tests of the turn (none executed) and the same line of the ATOM.
// 0.122.0 (7/11): **the second app of the paper project, `about`, and the paper goes into the image of the
// Paper**: `custom_apps = editor,calculator,about`. Two screens, as the original —a cover with one big line and
// the list of the pieces, and the detail of one piece, which opens with a finger and comes back with Back—.
//
// **The list is this board's and not a table of libraries.** The original read a `libs.lua` that its build wrote
// from the working tree; this disk does not carry the source tree, so what can be told without lying is what the
// board **is**: the system and its version, the interpreter and its memory, the glass, the finger, the disk, the
// radio and the cell —**seven pieces**, and every number comes from a call of docs/30 and none is written in the
// app—. The motto of the cover is the name and the version of the system: the original's "hack the planet" is
// not this project's line and a motto is not mine to write.
//
// **The two screens cost the app nothing at rest**, and the port is where two rules of this system bit:
// **there is no `visible`** (docs/30, docs/90), so a screen is built and given back with `release()` —which
// takes its fills with it and is not composed again, so no ghost stays under the new one—; and **a label does
// not wrap** —a text that does not fit ends in "..."— so the sentences are broken in the app with `ui.text_width`.
// The sizes are the paper project's in pixels of line, through `ui.line_height`, as 0.121.1 taught.
//
// Measured on the bench with the font of the disk (m5stack-paper, `--flash build/disk/emulator` and the tree as
// the card): `run about` prints `about: 7 pieces, lunokOS 0.122.0 on m5stack-paper` and one `about: piece …` line
// per row —`Lua 5.4.7`, `The glass 540 x 912`, `The finger 2 points`, `The disk 10 mounts`,
// `The radio wifi, http, tls, provisioning`, `The cell 99 %`— and paints the page once (540x912); the middle key
// of the board opens the first piece (`about: open lunokOS …`) and the same key on the detail goes home
// (`about: cover`), one whole paint each. Three paints for the three pages, and no error.
//
// The new case is `test_the_about_app_says_what_the_board_is_made_of` —the app from the disk, the cover's seven
// pieces, the middle key opening the first and going back— so 479 -> 480 cases. The three rules a port of that
// project follows are written in docs/40, because nineteen apps of it are still to come.
// **Pending**: the tests of the turn (none executed) and the same line of the ATOM.
// 0.123.0 (7/11): **the port of an app brings its drawing and its numbers, and the procedure is written down**.
// Four things, and the fifth is the document.
//
// **The icons of the two ported apps come from the paper project**. Its drawings live apart from the apps, in
// `tools/icons/<App>.png` —448 x 448, greys, the file a person edits— and its icons ship as a 94 x 94 `.icn` of
// four bits, which is a format of *its* panel and that nothing here reads. So the master travels with the app
// (`res/icon-source.png`, to be edited) and `tools/make_icon.py` does the same three things the paper's
// `icongen.py` does to it —compose the transparency over paper, take the greys, resize with LANCZOS— and writes
// `res/icon.png`, which is what `"icon"` names: **94 x 94 and 1 730 B** for the calculator (the master is 6 770)
// and **2 774 B** for About (its master 17 894). `apps info` shows the name; nothing paints it yet, because the
// page that would list the apps on the glass does not exist (docs/40).
//
// **`about` is now the original's page and not an approximation of it** (docs/45): the padding of 20 and the gaps
// of 12 for the list and 14 for the detail, the rows in the font of the body —43 px of line, which the port had
// at 33—, the height of a button of the paper project (the line of the body plus twice its vertical padding:
// 63 px there, 64 here), the text centred inside them, and the four lines of the detail in the order the
// original wrote them, the third one empty because a piece of a board has no address to give.
//
// **And an app raises its own version when it changes**, as the system raises its: `calculator` and `about` go
// from 0.1.0 to 0.2.0 in this commit, and docs/45 says so as a rule of the port.
//
// The fifth thing is **`docs/45-portar-una-app-del-paper.md`**: what is read from the original (manifest, entry,
// `res/`, the icon master, `fonts.h`, the defaults of its controls), how the resources travel, the four sizes in
// pixels of line with the table of the faces, the equivalence table from `paperos.*` to this system's calls —
// including the four things that have no equivalent: `launch`, `serial`, `zip` and `epub`— and the seven traps
// that bit here (no `visible`, no wrapping label, printable ascii only, the merged taps, the page that is the
// app's, the focus that is born set, and the hooks that exist and the ones that do not).
//
// Measured on the bench (m5stack-paper, `--flash build/disk/emulator` and the tree as the card): `apps info
// calculator` and `apps info about` print `icon        res/icon.png`; `run about` paints 540x912 once for the
// cover and, with the two screens exercised by hand, three paints for three pages; `run calculator keys` still
// prints `Calculator sizes 33 44 55 77` and its nineteen keys. The About case now also asserts that the reader
// keeps `res/icon.png` for the app on the disk and for the one of the repository, so 480 cases stand as they
// were.
// **Pending**: the tests of the turn (none executed) and the same line of the ATOM.
// 0.124.0 (7/11): **the About page lists the libraries of the project, and its title can be read**.
//
// **The title was cut because it was one label** ("lunokOS 0..."). A label of this system does not wrap, and the
// name and the version of the system at the size of the original —82 pixels of line, seven steps— do not fit the
// 500 pixels of the page. It now goes through the same breaker the sentences go through, centred, and takes the
// two lines it needs: measured on the glass, the ink of "lunokOS" sits in the rows 78-126 and the one of
// "0.123.0" in 174-221, so the title is read whole and the page keeps no "..." at the top.
//
// **And what an About says is what the system is made of, which is its libraries and not its hardware.** The
// page listed seven pieces of the board —the system, Lua, the glass, the finger, the disk, the radio and the
// cell—, which is a page of the datasheet: a person who opens About wants to know what has been built and with
// what. It now lists the twenty-three libraries of `lib/`, each with its version, its folder, the licence when
// the library declares one (today only `lua` does, MIT) and the sentence the library says about itself, six to a
// page with a More row for the rest (`23 libraries, lunokOS 0.124.0 on m5stack-paper, 6 rows a page`, four
// pages).
//
// **The list is generated, not typed**: `tools/gen_about.py` reads the `library.json` of every folder of `lib/` —
// the same file the build reads, so the page cannot disagree with the binary it is running on— and writes
// `apps/about/res/libs.txt`; `--check` says whether the file is up to date, and a folder of `lib/` with no
// `library.json` stops the tool with its name. Three libraries had none and got one in this commit —
// `board_esp32`, `board_paper` and `qr`, with their description and their own flags— and `lua` got its licence:
// twenty-three of twenty-three now say who they are, which is what a list of libraries needs.
//
// **The file is data and not a chunk**: `dofile`, `loadfile` and `load` are removed from an app's state on
// purpose (docs/20), so the generator writes one library a line with a tab between fields and the app parses it.
// The first attempt wrote Lua and died on `attempt to call a nil value (global 'load')` — which is the system
// working as designed, and the reason docs/45 now says a resource of the paper project that is code does not
// travel as code.
//
// The detail of a library is the four lines of the original in its order: the name and the version, the folder
// and the licence, an empty line, and what the library says about itself. The summary lost its "tap a piece" and
// says the page it is on, and the paging row says which libraries it holds (`More   7 to 12 of 23`).
//
// Measured on the bench (m5stack-paper, the font of the disk, the tree as the card): `run about` prints the
// headline, six `about: row <name> <version>` lines and `about: cover page 1 of 4`, and paints 540x912 once;
// with the middle key on the cover it prints `about: open alarms 0.1.0` and paints, and again for `about: back`
// and `about: page 2`, four whole paints and no `lua:` error. `python3 tools/gen_about.py --check` says the file
// is up to date at 23 libraries and 4 368 bytes.
//
// **`about` goes from 0.2.0 to 0.3.0** (an app raises its own version with its change, docs/45) and its manifest
// says what it now lists. The case is `test_the_about_app_lists_the_libraries_of_the_project`: it counts the
// `library.json` of `lib/` on the host, asserts that the page prints **that** number —so a stale list is a red
// case— and the version of `alarms` as the tree has it, then the middle key opening the first row and Back, so
// 480 cases stand where they were.
// **Pending**: the tests of the turn (none executed) and the same line of the ATOM.
// 0.125.0 (7/11): **the time clock of the paper project runs here, and the clock learns to say the hour of the person**.
//
// The eighth app of the porting plan is the first one that is not a page of controls: `Timeclock` is a day of work kept
// in a file that only grows, three keys, a list with a note on each mark, and a bar chart of the week behind an eye. The
// original is `pos_launch.lua` (1 187 lines) + `day.lua` (660) + `week.lua` (163), and the port is one file of
// **47 507 B** —of which **23 556 B** are the stripped chunk the interpreter keeps— with the data, the chart and the
// page in the order they are needed and the reason each number is what it is in the comment above it (docs/45 §6 bis).
//
// **One file and not three, because an app cannot execute a file here**: `dofile`, `loadfile` and `load` are removed
// from an app's state on purpose (docs/20), so the two modules of the original cannot be loaded and are not modules any
// more. And **the source cap of the entry chunk had to grow from 32 768 B to 49 152 B**: the app did not fit, and the
// cap is a block of the heap that is asked when an app starts and given back the moment the chunk is compiled, so the
// price is the load and nothing while the app runs (docs/75). The next growth of that number needs its own paragraph
// and its own measurement. **`keys.pof` is not ported**: the three keys of the original are three glyphs of the private
// use area, and a face here is ASCII only, so the buttons say `Start`, `Pause`/`Resume` and `End` —the fallback the
// original carried for a missing face— and the eye says `Week` or `Day`.
//
// **And the clock learned the hour of the person**, which is the part that was a call of the system and not an app's
// business: the chip counts UTC and every hour a person reads is local, so `kernel::cfg` keeps one number —the minutes
// the local hour is ahead of UTC—, `cfg tz +01:00` writes it, `time.tz('+01:00')` writes the same value, and three calls
// arrived with it: **`time.utc([t])`** (the moment as the chip counts it), **`time.date([t])`** (the same moment in the
// hour of the person) and **`time.tz([offset])`**. It is `date` and not `local` because `local` is a word of Lua and
// `time.local()` does not even parse —the wall the `in` of an alarm hit— and because `date` is the name the paper
// project gave it, which makes one more row of the equivalence table of docs/45 have nothing to explain. The weekday of
// a date is `kernel::calendar::weekday_of` through a hook and the day of the year is `hal::days_from_civil`: the rule of
// the leap years does not get a second copy, which is the thing docs/60 exists to prevent.
//
// Measured on the bench (m5stack-paper, the font of the disk, the app on the card, the clock fixed at 2026-11-01
// 09:00): `run timeclock` prints `timeclock: day 2026-11-01, 0 marks, work 0h 00m, rest 0h 00m, Not started today` and
// paints **540x912 once**; a day written by hand of four marks reads `work 9h 00m, rest 1h 00m, Day closed at 18:00`
// and repaints **one region of 500x705** —the controls, not the page—; the eye turns the sheet with **one 540x912** and
// the chart prints `avg 8h 07m in 4 day(s)` over five bars from Monday to Friday with the scale at 11h, 5h and 0h; the
// turn back is another 540x912 and says the day again. `time.utc()` at the epoch 1793523600 says
// **2026-11-01 09:00:00, wday 7, yday 305**, and `time.tz('+01:00')` makes `time.date()` say 10:00 and returns 60;
// `cfg tz` answers `settings: tz +01:00`, `cfg tz nonsense` is refused with the two forms that are accepted, and the
// setting survives a second boot (`tz +01:00` in the line of the CFG). The memory of the app is **141 510 B of the
// 4 194 304 B** of the Paper (`sys.heap()`), peak included, which is why its own manifest still says `esp32`: a board
// with no glass stops at the first line, before it pays any of it.
//
// **The icon travels with its master**, as docs/45 says: `res/icon-source.png` is the 448 x 448 greys a person edits
// and `tools/make_icon.py` writes the **94 x 94** that `"icon"` names, **2 881 B** —the same three things the paper's
// `icongen.py` did to it—, and `apps info timeclock` prints `icon        res/icon.png`.
//
// `apps/timeclock` is at **0.1.0**, the number a port starts at (docs/45), and the case is
// `test_the_timeclock_of_the_paper_project_keeps_the_day`: the app says where its controls are (`run timeclock keys`),
// the test puts the finger on Start, on Pause, on Pause again and on the eye, and it reads the file the marks went to —
// `2026-11-01|09:00:00|S` and nothing else— so 480 -> **481 cases**.
// **Pending**: the tests of the turn (none executed), `timeclock` in `custom_apps` (a word in `platformio.ini`, which is
// the owner's), and the same line of the ATOM.
// 0.126.0 (7/11): **the plan of the EPUB port, and its first piece: the zip container**.
//
// The reader of the paper project is not an app that draws pages: it asks the system for the page already composed,
// because measuring words and deciding where every line goes is arithmetic over a buffer and, in Lua, it would have to
// be walked in pieces under a budget of instructions. Porting it is therefore **porting an engine** —3 319 lines of C++
// and 1 344 of tests in the paper— and then the 3 959-line app. The whole inventory, functionality by functionality and
// with the order of the phases, is **docs/46-epub-el-motor-i-el-port.md**: the engine (A1..A7: zip, XHTML, paginator,
// emphasis, the open book, the grey count, the limits), the door of Lua (B1..B10), the app (C1..C15) and the seven
// things this system does not have and the reader asks for (D1..D7: fullscreen, how the page comes back, Latin-1
// faces, a blit of pixels, styled text, the size of an app, the settings).
//
// **And the first piece is done: `lib/zip`** (A1). A zip is walked from its two ends —the record that closes it and the
// central directory— and never from the middle: a book of ten megabytes opens in three reads. It is **the container and
// nothing else** (stored and deflate, the checksum, the name, where the stream starts), so it adds to a system that
// already had `lib/inflate` only the structure of the frame — and it is the first library of this tree with the same
// discipline as the rest: **no allocation of its own**. The table of entries and the block of the compressed stream are
// the caller's, and the one block of its own is the window where the closing record is searched, which is **static**
// because this runs under the interpreter of an app and the loop that holds it has eight kilobytes of stack
// (docs/46, A7). There is one zip open at a time in this system and the header says so.
//
// Five things the format teaches, all of them written where they bite: the closing record is searched **from the end**
// (a zip may carry 65 535 bytes of comment after it, and the copy that matters is the last one), the comment has to land
// **exactly at the end of the file** (a signature inside a comment is not a record), **the central directory is the only
// truth** (a local header can lie with zeros and a data descriptor), a `stored` entry whose two sizes disagree is
// damaged, and the window of the deflate **is the output block**, which is why `inflate::raw` is one-shot.
//
// What it refuses, each with its own word: a zip split in volumes, **ZIP64**, an **encrypted** entry, a method that is
// not stored or deflate, a name of more than 127 bytes, an entry of more than two gigabytes, a stream that inflates to
// something other than what its header promised, a compressed entry bigger than its own contents (a bomb wearing the
// size of a chapter) and a **checksum that does not match** —which is exactly what a card with a bad block gives—.
//
// Measured on the bench of the computer: `lib/zip` is **a header of 109 lines and 293 of C++**, with `-Wall -Wextra
// -Werror` from its own `library.json`, and `test/test_zip` builds 7 cases: a zip of three entries built **byte by byte
// inside the test** (a folder, a stored `mimetype` and a deflated chapter), the same file read by python's `zipfile` to
// prove that what the test writes is a legal zip —`['mimetype', 'META-INF/', 'OEBPS/text/cap1.xhtml']` and the chapter
// comes back whole—, a comment with the signature of a record written inside it, and six refusals each with the file
// patched where the number lives. `pio test -e test --without-testing` builds the 20 suites, **488 cases**, with 0
// warnings.
//
// **The deflate of the test is real**: the 81 bytes of the chapter are what zlib produced from the same sentence, and
// the two checksums are its numbers (`2CAB616F` for `application/epub+zip` and `447404B8` for the chapter), so what the
// suite tries is this reader against a compressor that is not mine.
// **Pending**: the tests of that turn (written, compiled, **not executed**: that wants the owner's word for it), the
// doc of the next phase, and the same line of the ATOM.
//
// 0.127.0 (7/11): **the accents, and the glyph that was made of pieces**.
//
// **The second piece of the port is the one that was a wall of one line: the accents** (docs/46 D3). The drawing of
// a text asked a face of outlines its glyph **only for the bytes up to `0x7F`** —one line in the width of a text and
// one in the walk of the runs— before asking the file anything, so an `e acute` was dropped whatever the file had
// inside, and every reader of a Catalan book would have lost the letter silently. A byte is a code and the face
// decides: what the file does not carry is still a gap (`ttf::glyph_of` answers, and the line advances by nothing),
// and what it carries is drawn —which is exactly what the paper project's own faces meant when they were tables of
// bits— (docs/30).
//
// **And the second half of the wall was the file itself.** Cutting DejaVu Serif down to Latin-1 with `pyftsubset`
// gave an `e acute` with its own advance and **no ink at all**, and the reason is a thing worth writing down: the
// reader of this system walks a glyph that is a list of contours, and DejaVu writes `e acute` as a **glyph made of
// pieces** —the `e` and the accent, each one its own glyph, with an offset— which `lib/ttf` refuses by name. So
// **`tools/ttf_latin1.py`** cuts the face and then writes the pieces out where they belong (`DecomposingRecordingPen`
// walks what a glyph is made of and hands back the drawing, `TTGlyphPen` turns it into a glyph again). The hints go
// too, because this reader never runs an instruction of a glyph and they were a third of the file. Measured on the
// file: **232 glyphs, 55 of them flattened, the biggest of 85 points and 5 contours** (the reader takes 256 and 64),
// **23 856 B** against the 10 896 of the ASCII-only `dejavuserif.ttf` beside it, and `/fonts/dejavuserif-latin1.ttf`
// is the face a book is read with —the printable ASCII, `U+00A0` to `U+00FF`, the long dashes, the quotation marks,
// the ellipsis and the bullet—.
//
// Measured on the glass of the bench, with a probe app that loads the face and paints three lines: `caf` is **54 px**
// and `caf\xE9` is **75 px** at step one —the same 54 and 54 of before the fix—, and the page shows `café, pera i
// poma`, `aà è é í ò ó ú ü ç · « »` and `guixos amb accent: À È É Í Ò` with the accents above the letters and not
// beside them. `test_font` gets a case that measures the two widths, the ink of the accent above the ink of the `e`
// it belongs to, and a byte the face does not carry: **489 cases in 20 suites**, built with `pio test -e test
// --without-testing` and 0 warnings.
// **Pending**: the 489 cases are **not executed** (that wants the owner's word for it); the three faces of the
// system are still ASCII and one command each would make them Latin-1; and the tokenizer of the next phase is the
// one that turns UTF-8 into this one byte per character.
//
// 0.128.0 (7/11): **the tokenizer, the second piece of the EPUB port**.
//
// A book is read a page at a time and a page has to start **exactly** where the one before it ended, because the
// position of a reader is a byte inside a chapter and not a page number. So `lib/epub` walks the XHTML of one
// chapter and hands one token at a time, each with the byte it starts at: a **word** (with the block that opens
// it and the ink it is in), a **picture** (with the entry inside the zip and the side it floats to), a **line
// end or a rule**, or **one of the four marks of the grid of a table** —because two cells one after the other
// are the same block and a page cannot tell where one ends and the next begins without them—. A `<script>`, a
// `<style>`, a `<head>` and a comment are skipped whole: a book carries a page of JavaScript in front of every
// chapter and none of it is a paragraph.
//
// **The one decision that is not the paper's**: the text comes out as **one byte per code**. There, a word kept
// the UTF-8 the book was written in and the layout turned it into glyphs when it painted; here the face of this
// system is Latin-1 —`tools/ttf_latin1.py`, 0.127.0— and the whole pipeline of text is `ttf::glyph_of(face,
// uint8_t code)`, so the turning happens once, where the chapter is already being walked byte by byte. It comes
// with three tables that are decisions and not translations: the entities of the XML and the **60 letters of
// Latin-1** (a Catalan book painted `caf&eacute;` on the glass the day `&eacute;` was not in the table); the
// **stand-ins for the punctuation Latin-1 cannot carry** —`—` is `-`, `“ ”` are `"`, `…` is `.`, the bullet is
// the middle dot, the soft hyphen paints nothing— because a long dash painted as a question mark is worse than a
// short one; and the **C1 range read as Windows-1252**, so `&#146;` is an apostrophe and `&#150;` a dash, which
// is what they mean in a book of the nineties. A byte that is not UTF-8 is taken as Latin-1 —some books are
// labelled one way and written another, and a lone `e acute` must not be a question mark—.
//
// What it does not do, said here so nobody looks for it: the CSS of the book is ignored (only `style`, `align`
// and `class` of the tag itself, and only for the side of a picture), there are no columns and no right-to-left,
// and the words are not hyphenated.
//
// Measured on the bench of the computer: `lib/epub` is **186 lines of header and 805 of C++**, with `-Wall
// -Wextra -Werror` from its own `library.json`, and `test/test_epub` has **12 cases**: the byte of every word and
// the block that opens it, the entities in all three forms and the ones nobody knows, a chapter of raw UTF-8 and
// a chapter already in Latin-1, a script and a style and a comment skipped whole (with the byte of the word that
// comes after them), two `<br>` in a row and an `<hr>`, the three ways a book says the side of a picture, a
// `<b>` nested in an `<i>` and the counters of `sup`/`sub`, a `<pre>` that keeps the line ends of the author, the
// grid of a table in order, a word longer than the buffer cut and not lost, and a page that starts at a byte in
// the middle of a word. `pio test -e test --without-testing` builds the **21 suites and 500 cases** with 0
// warnings, and `pio run -e emulator` says SUCCESS with 0 warnings —the library is not linked into a firmware
// yet, because nothing of the firmware calls it until the paginator exists—.
// **Pending**: the 500 cases are **not executed** (that wants the owner's word for it), and the next piece is the
// paginator (docs/46, A3 and A4: the lines, the air of every block, the pictures with bands and the styled text).
//
// 0.129.0 (7/11): **the ink of a line, which is the third piece of the EPUB port**.
//
// A book says more with a line than its letters: a bold word, an italic one, a note mark in the air. The faces of
// this system cannot give any of the three —a face of one bit has no bold and no italic, and the faces of the disk
// are one weight— so the three are made here, and they are made **to the rectangles a glyph is already made of**
// and not to a bitmap of it: a styled text costs what a plain one costs and no more, and the retall, the damage
// and the canvases that already exist do not learn anything new (docs/46, A4 and D5).
//
// The three rules are the paper project's, which its owner looked at on the glass word by word: **a bold is one
// pixel more to the right** —a glyph painted twice with that offset is thicker and stays a glyph of the same face,
// which is the only way to have a bold with a face that has one weight—, **an italic is a slant row by row**, a
// fifth of the height of the glyph from its bottom row to the one above, and the pen does not move for it, and
// **a `sup` goes up a third of the line and a `sub` down a fifth**, and the line does not grow, because a note
// mark that pushes the lines apart is worse than one that touches the line above.
//
// It is `font::Ink` —three fields and no memory— through `font::runs`, which is the one place where a text becomes
// rectangles, so both kinds of face get it: the outlines of the disk and the tables of bits of the flash, with the
// line being the height of the face in both. `ui::canvas_text_styled` is the canvas' door, and it also grew the
// **damage** of a text that is off the baseline: a `sup` paints above the box of its line, and a box that stopped
// at the line would leave the mark of the note on the glass for ever. There is **no call of Lua** for it: what
// needs a bold is the reader of books, which is the system's, and an app does not have to be able to ask a face
// that has one weight for one it does not have (docs/30, docs/46).
//
// Measured on the bench of the computer: `test/test_font` has **four cases more** —a bold run is exactly one pixel
// wider than the same run plain and the advance of the word does not change, an italic moves the first row of the
// cell and not the last, a `sup` of a line of ten pixels goes up three and a `sub` down two, and the same three on
// the face of a book at step four with a slant of a fifth of the glyph—, and `pio test -e test --without-testing`
// builds the **21 suites and 504 cases** with 0 warnings. `pio run -e emulator` says SUCCESS with 0 warnings.
// **Pending**: the 504 cases are **not executed** (that wants the owner's word for it); and this turn has **not
// looked at the glass**, which is said out loud because it is the rule of this house: the ink has no door from Lua
// and no page composes a text with it yet, so the first time it will be seen is with the paginator of the next
// piece (docs/46, A3), and until then what is measured is rectangles in a test and not a word on the screen.
//
// 0.130.0 (7/11): **the paginator, and a page that is a plan**.
//
// The paper composed a page by **painting** into a canvas as it laid it out, because its canvas was one bit and a
// glyph was a bitmap it blitted. Here a text is `font::runs` and `ttf`, which are the kernel's and know about
// faces, steps and ink, so the paginator computes **where** and the kernel does **what**: `epub::layout()` fills a
// `Sheet` —a list of items and a pool of words— and says what came out. Three things come from that, and they are
// the reason: **measuring a page costs nothing** (it is the same list, walked by nobody, which is what the door of
// Lua will ask for), **a page can be tried without a screen** (a ruler of a face that says every letter is six
// pixels and every line ten proves the arithmetic of a line and of a table with a pencil), and **the memory is the
// caller's**, the list and the words both, because a word of a chapter is a slice of a buffer that is not ended by
// a zero.
//
// An item is a **word** (x, y, face, step and ink), a **rule** (an `<hr>` or the underline of a header) or the
// **box of a picture** with its href in the pool. And what the paginator does with them is what the paper did, with
// its own numbers: a line breaks where the word does not fit, the air of a block is a fraction of the line of the
// body (a title two thirds, an `li` a quarter, a `dt` a half, a `dd` a sixth), a floated picture has a band of
// **45 per cent** of the width for the words to flow around, a picture that does not fit **opens the page** —a
// plate is not cut in half—, and a table counts its columns with a probe over the same bytes before the first cell
// is placed, because the width of a cell is the width of the page divided by columns it does not know yet.
//
// **And one thing the paper never did right, which is fixed here**: the inset of a quotation. There the inset came
// from the block that opened the word, and inside a quotation that block is a `p` like any other, so
// `<blockquote><p>…</p></blockquote>` —which is how books are written— was never inset, not once, in every book
// its owner read. The tokenizer now **counts** quotations, figures and definitions and the token carries `inset`.
// Which is also why the probe of a table starts at one cell and not at zero: the cell being placed is the first.
//
// Measured on the bench of the computer: `lib/epub/page.h` is **138 lines** and `page.cpp` **646**, with `-Wall
// -Wextra -Werror`, and `test/test_layout` has **12 cases**: a paragraph with the place of every word, a line that
// wraps where the word does not fit, a page that ends at the byte of the word it could not hold and a second page
// composed from that same byte, a title of its own face and step with the air of a block, a quotation inset and a
// caption in the middle, a table of two columns with its header underlined, a floated picture and the band it
// leaves, a picture that does not fit and opens the next page, the ink of a bold and a `sup` travelling to the
// item, a word wider than the box that is placed anyway, a pool that is full and ends the page at a word, and a
// sheet with no room that is refused. `pio test -e test --without-testing` builds the **22 suites and 516 cases**
// with 0 warnings, and `pio run -e emulator` says SUCCESS with 0 warnings.
// **Pending**: the 516 cases are **not executed** (that wants the owner's word for it); the glass is **still not
// looked at**, because a page is a plan and nothing paints it yet —the painter is the kernel's and comes with the
// open book (docs/46, A5 and B3)—; and the next piece is that book: a chapter inflated into the big memory, an
// `href` resolved against the chapter, and the page the reader shows.
//
// 0.131.0 (7/11): **the book open, and a chapter of a real file in the memory of the board**.
//
// The reader is three pieces that already existed —the zip of `lib/zip`, the tokenizer and the paginator of
// `lib/epub`— and this is the one that gives them a file. `kernel/book.h` opens a `.epub` of a mount through an
// adapter of six lines (the zip asks for a size and for bytes at an offset, and a file asks to be seeked and read),
// reads its central directory, **inflates a chapter into the extra memory** and lays out a page of it from the byte
// the reader was at. One book at a time, as with the zip, and for the same reason: the blocks are the board's.
//
// **The blocks are counted and given back**: a chapter of up to **512 KB** and its compressed stream beside it, the
// table of **128** entries and the sheet of a page, all of them from `pal::extra_memory()` —the PSRAM of the
// Paper— and released when the book closes, which is what a failed `open` does too: a file that is not a book
// leaves the extra memory exactly as it found it, and the case of the test proves it by opening a real one after
// four refusals.
//
// **The three faces of a page are the caller's** (`Fonts{body, title, small}`, a null being the system's), because
// which typeface a book is read with is the app's decision and not the kernel's (docs/46, C13), and the two numbers
// a page asks of each one —the width of a word and the height of a line— are `font::width` and the height of the
// face times the step, which is what the whole system already means by a line (docs/30).
//
// Measured on the bench of the computer: **88 lines of header and 390 of C++**, with `-Wall -Wextra -Werror`, and
// `test/test_book` has **4 cases**, and they are the whole road: **a `.epub` written byte by byte by the test on the
// disk of the test** (a `mimetype` and two chapters, all stored, with the checksums of `inflate::crc32`), opened by
// the path of a person, with its three entries, its `find` by name and its first chapter found by looking like one;
// a chapter that comes out **whole and equal to the one that went in**; **a page laid out with the real face of a
// book** read from the repository —thirteen items of the chapter, the first word in the body step four— and a page
// of a box of sixty by sixty that ends at the word `same`, with the page after it **starting at that same byte**;
// and the four refusals (a relative path, a file that is not there, a unit that is not mounted, a sentence that is
// not a zip) after which a real book still opens. `pio test -e test --without-testing` builds the **23 suites and
// 520 cases** with 0 warnings, and `pio run -e emulator` says SUCCESS with 0 warnings.
// **Pending**: the 520 cases are **not executed** (that wants the owner's word for it); **the pictures of a book are
// not measured yet** —a page of a chapter with figures places the words and leaves the picture out—; the glass is
// **still not looked at**, and the next piece is the one that changes that: paint a page of items in a canvas and
// open the door from Lua, which is what a person needs to see a book on the screen (docs/46, B2 and B3).
//
// 0.132.0 (7/11): **a book on the glass, and the door of Lua that puts it there**.
//
// A page of a book is now a picture a person can look at: `ui::canvas_book_page` walks the plan of the paginator and
// paints it —a word with its face, its step and its ink, a rule as a row of ink— and the module `epub` of Lua asks
// for it. The rest is the same road of the last four pieces: `/littlefs/epub/la-vaca-cega.epub`, its central
// directory, the chapter inflated to the extra memory, the tokenizer, the paginator and the pixels.
//
// **The door is a module and not an object** (`epub.open`, `epub.chapter`, `epub.page`, `epub.close`), because there
// is one book open at a time in this system and a chapter is half a megabyte of the extra memory: an object with a
// metatable would promise many of them. `epub.chapter` says how many bytes a chapter has and not its text, because
// an app does not have to hold a chapter to read one. And **the page is composed into a canvas the app already
// has** —the decision D2 of docs/46, which costs no surface and no `free()`—: what comes back is `next`, the byte
// the next page starts at, which is the reading position of a book.
//
// **Two faults were found by looking at the glass, and neither would have been found by a test that passes**: the
// first page came out in the bold sans face of the system with every accent missing, and there were two reasons at
// once. **The size of a file is the one the mount said in its `stat`** and not the one the slot a mount hands out
// carries —that one is the backend's business and was zero, so the zip said "not a zip"—, and **the faces of a page
// are the objects `ui.load_font` gives**, not numbers, so reading them as numbers fell back to the face of the
// system, which is ASCII and has no `ç`. The accent then painted as a gap and the word closed up: `camí` was `cam`.
//
// Measured on the bench of the computer, with the emulator's glass and `--dump`: the sample book is
// `data/epub/la-vaca-cega.epub`, **2 275 bytes**, two chapters of **308 words**, built by `tools/make_epub.py` from
// the text in `tools/epub-sources/la-vaca-cega.txt` —«La vaca cega», of Joan Maragall, 1893, public domain—. The
// probe app reads it: **5 entries**, the first chapter `OEBPS/ch01.xhtml` of **1 107 bytes**; the first page is
// laid out from byte 0 and ends at **byte 625** having used **891 of the 912 rows** of the view; the second page
// starts at that byte, uses **638 rows** and closes the chapter. The image of the two pages is what the glass shows:
// the serif face of the book, the title in its own step, the air between the stanzas and every accent where it goes.
// `pio run -e emulator` says SUCCESS with 0 warnings, and the suites are still **23 and 520 cases** built with 0
// warnings (the door and the painter have no case yet: a canvas needs a booted kernel and the picture was looked at
// instead, which is said here as it is).
// **Pending**: the 520 cases are **not executed** (that wants the owner's word for it); there is no app of the reader
// —the shelf, the menus, the foot and the saved position are docs/46 C1..C13—; and the next piece is a plate that is
// bigger than the block of a page, which today is refused and not shrunk before it is decoded.
//
// 0.133.0 (7/11): **the pictures of a book, and a blit of pixels in a canvas**.
//
// A book has plates and the reader had none: the tokenizer had carried `<img>` since 0.128.0 and the paginator
// reserved its box since 0.130.0, and what was missing was the two ends —where the name points and what to put in
// the box—. `ui::canvas_bitmap` is the first: four bits a pixel, two in a byte with the left one in the high nibble,
// which is the packing the glass, the canvas and `img::decode` already speak, so a picture drawn on a page is a
// **copy** and not a conversion. And `kernel::book` is the second: the `href` of a picture is **resolved against the
// chapter** (`img/x.png` is `OEBPS/img/x.png` for `OEBPS/ch01.xhtml`, and `../img/x.png` goes up a folder), its size
// comes from the header of the file and not from decoding it (`img::read_info`), the pixels are fitted to the box the
// page leaves for it **row by row in the sink of the decoder** —the cheapest resampler there is and the one an e-ink
// plate deserves— and **one picture is kept**, because a page turns around one and the block is the board's.
//
// The three blocks of a picture are asked for the first time a book has one and they leave with the book: **256 KB**
// for the file as it comes out of the zip and **259 200 bytes** of pixels, which is a page of the Paper at four bits.
// The scratch of the decoder is the block of the compressed stream, which is free at that moment: a chapter and a
// picture are not decoded at once, and the block is the board's either way.
//
// Measured on the bench of the computer, looking at the glass: the sample book grew a plate drawn by
// `tools/make_epub.py` itself (`--figure`: a frame, a mountain and a sun, 240 × 150 PNG) and it is **3 421 bytes**;
// the first page of the poem now ends at **byte 586 instead of 625** —the picture took its room— and says
// `picture=true`; the image is on the glass, centred under the title, with the words flowing under it. `test_book`
// has a fifth case with a **BMP of two by two pixels** built byte by byte: the entry is resolved against the chapter,
// the item of the page is a picture of two by two —the size of the file, not scaled— and the page says it holds one.
// `pio test -e test --without-testing` builds the **23 suites and 521 cases** with 0 warnings, and `pio run -e
// emulator` says SUCCESS with 0 warnings.
// **Pending**: the 521 cases are **not executed** (that wants the owner's word for it); a plate **bigger** than the
// block of a page is refused and not shrunk before it is decoded; and the app has no full screen, no dark page, no
// cover and no thumbnails (docs/46, C9, C11 and D1).
//
// 0.134.0 (7/11): **the app of the reader, and the shelf that was missing from the system**.
//
// A book is read by a person now: `apps/reader` is the shelf of every `.epub` of the flash and the card, the page
// composed by the engine with the serif face of the book, the foot with the chapter and the percentage, two buttons
// for the pages and the position of each book kept as the chapter and the byte. **The app does not lay out
// anything**: what it does is what a hand does —which book, which page, and how far in— and the rest is the engine's,
// which is what docs/46 has been building towards since 0.126.0.
//
// **Three things of the system came out of writing it**, and none of them were planned:
//
//   - **the shelf belongs to the system and not to the app**: an app of this system does not list the flash —it has
//     its own folder and it reads the rest through the doors— and the folder of the books is the system's, so
//     `epub.books()` answers the paths of the `.epub` of `/littlefs/epub` and `/sd/epub` and the app makes the
//     titles out of the names (`book.cpp`, a `ListSink` of twenty-four names);
//   - **going back a page needs a page that is not painted** (docs/46, B4): the position is a byte and the page
//     before it is the last one that started before it, so `epub.page` learned a `measure` that lays the pages out
//     and touches no pixel, and the app walks the chapter with it;
//   - **the positions live in the app's own data** and not beside the book, because an app writes in its own folder
//     and reads the rest of the machine (docs/30, docs/70): the day the system gives an app a writable shelf, that
//     is one line of the app that changes.
//
// The door also learned that a canvas is **the object an app holds** and not only its number, and that a page of a
// book can be asked for a canvas of any size —which is what lets the app keep a foot and a bar of buttons under the
// page it paints—.
//
// Measured on the bench of the computer, looking at the glass: `apps/reader/reader.lua` is **316 lines** of Lua and
// its manifest declares `opens: epub`. Run with `run reader /littlefs/epub/la-vaca-cega.epub` the app opens the book,
// composes page one with the statue of the poem under the title and writes `ch01 0%` in the foot, with the two
// buttons under it; with the position of the book set to byte **586** the app opens **at that byte**, the page starts
// at `Topa de morro en l'esmolada` and the foot says **51 %**. The file of positions is
// `data/reader/reading.txt`, one line, `path TAB chapter TAB byte`. `pio run -e emulator` says SUCCESS with 0
// warnings and `pio test -e test --without-testing` still builds the 23 suites and 521 cases with 0 warnings.
// **Pending**: the 521 cases are **not executed** (that wants the owner's word for it); the app has no case of its
// own —it was looked at, which is said here as it is—; and the two pieces the reader of the paper had and this one
// does not are the full screen (D1) and the dark page (C9), which want the bar of the system to be able to go away.
// **And one thing for the owner**: `apps/reader` has to be named in `custom_apps` of `platformio.ini` for a built
// disk to carry it —that file is his—, and until then the app of the bench runs from the card.
//
// 0.135.0 (7/11): **the reader without a single button, turned with a finger and with the keys**.
//
// The owner, with the app in his hands: *«No vull botons per les págines, vull gestos swipe esquerra, dreta i amunt
// per anar al principi del capítol. Els botons físics també son per canviar de página anterior/seguint.»* So the bar
// of two buttons is gone, the page takes the whole view but its foot, and the two ways of turning it are the
// system's own:
//
//   - **the finger**: `touch.gesture()` gives the shape of the flow of fingers —a `drag` and how far it went— and the
//     app reads it: a drag to the **left** is the page after this one, to the **right** the one before, and **up** the
//     beginning of the chapter. Sixty pixels is a hand's, and the way that moved more is the way the gesture is, so a
//     diagonal is not two pages at once (docs/30, docs/46 C3);
//   - **the keys**: `on_button(kind, held_ms, key)` —the hook the system calls between two turns— with the key `1`
//     going back and the keys `0` and `2` going forward, which is the same order as the fingers and leaves the single
//     key of the ATOM as the key that turns the page forwards.
//
// **And the keys only reach the app because the page has no focusable control**: the dial is the system's while there
// is something to focus (docs/30, docs/90), so an app that wants the keys has to be a page of one thing —a canvas and
// a foot—, which is exactly what a reader is. The foot stays: it is the chapter and how far in, and it is
// information and not a control.
//
// Measured on the bench of the computer, looking at the glass: `apps/reader/reader.lua` is **332 lines** of Lua, the
// page is composed into a canvas of **540 × 872** (the view is 912 and the foot takes 40) and the foot says
// `ch01 0%` with nothing under it. `apps/reader` is named in `custom_apps` of `platformio.ini`, which its owner asked
// for in the same breath, so a built disk carries the app and not only the card.
// **Pending**: the 521 cases are **not executed** (that wants the owner's word for it); **the bench cannot press a key
// nor lay a finger** —`--script` runs shell lines and the panel of the emulator only takes fingers from a test—, so
// the two ways of turning a page are wired to the hooks the system documents and **were not exercised on the glass**;
// and the app still has no full screen, no dark page, no cover and no thumbnails (docs/46, C9, C11 and D1).
// 0.136.0 (7/11): **the whole glass for an app that asks for it, and three taps that are the system's** (docs/46 D1).
//
// The owner, with the list of what the reader was missing in front of him: *«Fes la D1, pantalla sencera, l'app demana
// al sistema de quedar-se tota la pantalla, el sistema li dona permis, amplia la vista de l'app i amaga la barra de
// sistema. Per sortir de pantalla completa son 3 tocs a la vista de l'app que ha demanat pantalla completa. [...] L'app
// no gestiona els 3 tocs, ho fa el sistema.»* So the system has one more thing an app can ask for, and it has it in the
// shape the rest of the system already has:
//
//   - **`ui.full_screen(true)`** — or `(false)` — from Lua, and `ui::full_screen` in the kernel: the strip of the bar is
//     hidden, the view is made the size of the panel and placed at the corner (540 × 960 at 0,0 instead of 540 × 912 at
//     0,48), and the app is told with `on_resize`, which is the hook a turn already gives it. Without an argument the
//     call answers whether the app has the whole glass, the same shape `ui.rotation` has. Asking for what it already
//     has costs nothing: no view is made and no repaint is owed;
//   - **the way out is the system's**: three taps are a `triple` the gesture machine already made, and the kernel asks
//     for it right after `touch::poll` and before the app's turn, so the app neither handles them nor hears them — the
//     queue keeps the gestures that were there before it (`touch::take_gesture_of`) and the app hears `on_resize`;
//   - **an app that ends gives the glass back**, by its own will or by an error: the closing, the death between two
//     turns, the death inside the first turn, and a window of an alarm all go through `ui::give_back_the_glass`, and
//     `ui::attach` promises the view of every app to the app that comes next whatever the one before did.
//
// **Two things the code taught me while it was being written**, and both are in the system now:
//
//   - **a view that has just been made again is washed with the ink that was under it**: when the view grows over the
//     strip, the hour is the bar's ink and not the app's, so the wash after the change counts levels 0 and 15 whatever
//     the app has drawn, or a `du` wash leaves the ghost of the clock on the first page of a book;
//   - **`ui.clear` never told the counters what it had painted**: it writes the page with a `memset` and not a box at a
//     time, so a page emptied to black counted as a page of nothing and the wash between two apps —and the one after
//     the whole glass— chose `du` for a page that was black. One line, and the black page of C9 will need it.
//
// Measured on the bench of the computer, looking at the glass: `apps/reader/reader.lua` is **375 lines** of Lua and asks
// for the whole glass one line after it starts. Run with `run reader /littlefs/epub/la-vaca-cega.epub` the log says
// `whole glass: the view is 540x960 at 0,0`, the wash after it is the two-level one (`a2`, the bar's ink went with it)
// and **the page is the whole panel**: the ink of the poem starts at row 8 and the foot `ch01 0%` ends at row 935, where
// with the bar it was 48 rows less. A probe app that takes the whole glass, paints it black and dies leaves **the bar
// back on the glass** (the hour is read in the dump) and the view at 540 × 912; a probe that asks for it back gets
// `under the bar glass: the view is 540x912 at 0,48` and the bar paints itself whole again. `pio run -e emulator` says
// SUCCESS with 0 warnings and `pio test -e test --without-testing` builds the **23 suites and 527 cases** with 0
// warnings, five of them new (four in `test_bar`, one in `test_touch`).
// **Pending**: the 527 cases are **not executed** (that wants the owner's word for it); **the three taps were not
// exercised on the glass**, because the bench lays no fingers —`--script` runs shell lines and the panel of the emulator
// only takes fingers from a test— and the case that covers them is the one in `test_touch`; and the reader still has no
// dark page, no cover, no thumbnails and no menus (docs/46, C4, C9, C11).
// 0.136.1 (7/11): **the roadmap said the reader was missing what it already has**. `docs/90` still named
// painting the page in a canvas, the door of Lua, the picture in a canvas and the whole glass as things the
// reader was waiting for, and all four are in the system since 0.132.0 to 0.136.0. The paragraph now says what
// the reader really lacks —the OPF read from Lua (C2), the pass of the page (C10), the dark page (C9), the
// menus (C4), the foot with the count (C5), the cover (C7), the loading screen (C8), the contents (C14), going
// to a page by number (C15), the cache of the picture (C11), an error as a notice (C12), the two faces of the
// book (C13), the size of the app (D6) and its settings (D7)— with the card of each one in docs/46, because a
// roadmap that promises what is already done is a person looking for a file that is not there. No code in this
// commit: `docs/90-full-de-ruta.md` and this line.
// 0.137.0 (7/11): **the menu of the reader and the dark page** (docs/46 C4 and C9).
//
// The owner, reading the list of what the reader was missing: *«C9 es invertir colors? si es aixó: es farà amb opció al
// menú de text del C4»*. Yes: the dark page is the page with its ink and its paper swapped, and it lives in the menu, so
// the two cards are one piece of work.
//
//   - **C9, the dark page**: `epub.page(..., {dark = true})` paints the same words in the same places with the ink at 15
//     and the plates **inverted as they are copied** (`ui::canvas_bitmap` and its `dark`), because the cache of the book
//     holds the picture as the file says it —paper white— and a white paper on a black page is a hole in the page. The
//     app clears the canvas to 0 and says the foot in white: the paper of a page is the app's and the ink is the page's
//     (docs/46, A5). It is **only the page of the book** —not the shelf, not the cover— and it is not saved.
//   - **C4, the menus**: `Book` (Shelf) and `View` (Full screen, Dark page), with `ui.menu_bar`, and **only while the
//     menu is open**. That is not a detail: a control that is always on the page takes the dial, and then the keys of
//     the board stop turning the page — the lesson of 0.135.0. So the menu is made when it is asked for and given back
//     when it goes, and the page between the two is a page of one canvas and a foot, which is what keeps the keys.
//   - **A long press is the way in**, and the ways out are the same long press, a tap below the row of words and a
//     swipe; a key does not turn the page behind a card. The row is 52 rows at the top of the page, the View is the menu
//     that is down (it is the one with the two states) and the Book is a word a finger unfolds.
//
// **One thing of the door of Lua learned something**: `epub.page`'s switches were read as numbers only, so `dark = true`
// —the way anybody writes a switch— would have done nothing and said nothing. `epub_opt_number` takes a boolean now, and
// `measure = true` works for the same reason (docs/30).
//
// Measured on the bench, looking at the glass: a probe that opens the book, clears its canvas to 0 and asks for
// `{dark = true}` gives **black paper and white words**, with the plate of the poem inverted (its white paper is black
// and its lines are white); the same page with the menu over it shows the row `Book View` —with the View inverted, and
// a black card with white words on the dark page— and the two entries with their boxes, `Full screen` checked. The
// reader itself is **492 lines** of Lua and still composes its page from row 8 to row 935 with the whole glass.
// `pio run -e emulator` says SUCCESS with 0 warnings and `pio test -e test --without-testing` builds the **23 suites and
// 529 cases** with 0 warnings, two of them new in `test_ui` (a bitmap inverted on a dark page and a page of a book
// painted with the ink it is given).
// **Pending**: the 529 cases are **not executed** (that wants the owner's word for it); **the long press was not
// exercised on the glass** —the bench lays no fingers—, so what is proven there is the paint of the menu and of the dark
// page, and the gesture has to be tried on the board; the Contents of the Book menu waits for the real chapters of the
// book (C2, C14); and the reader still has no cover, no thumbnails and no count of pages (docs/46, C5, C11).
// 0.138.0 (7/11): **the foot of the reader counts the pages of its chapter** (docs/46 C5).
//
// The foot said `ch01 25%`, which is a share of the bytes of the chapter and not a page among pages —the owner has been
// reading it for a day—, and the design of the reader of the paper has a strip of dots under the page that says where a
// person is. Both need the same number: **how many pages the chapter has and which of them is the one that is read**.
//
//   - **the count is a walk in the background**: `epub.page(..., {measure = true})` lays a page out and paints nothing,
//     and the walk of a chapter is more than one turn of the glass, so it pays **four pages per turn** in the loop of
//     the app. What it leaves is the table of the byte each page starts at, which answers three questions at once: how
//     many pages (`#pages`), which one the byte we are at is on, and **the page before this one without walking the
//     chapter again** — that walk was the price of going back a page since 0.134.0 (docs/46, B4);
//   - **the foot** says `ch01 3/12 25%` when the count has landed and only the percentage while it is walking, because a
//     number is better than a guess and a guess is better than nothing. The label is only as wide as the room the dots
//     leave: 260 of the 540 pixels of the page;
//   - **the dots** are `ui.dots` at the right of the foot, 264 x 40, which is eight balls of the strip of the paper
//     project, and the window follows the page that is read. **They were a byte with a ceiling of thirty-two pages**
//     —`uint8_t` in the kernel and a validation that refused more— and a chapter of a book has more: they take the
//     **512** of the paper project now (docs/30).
//
// **And a foot that stayed on the glass**: `give_back` released the canvas of the page and **dropped the label without
// releasing it**, so going from a book to the shelf left `ch01 51%` under the shelf for ever — nothing paints over what
// is not drawn again (docs/30, docs/90). The label and the dots are released now, and the same two lines keep the dots
// of a dark page from outliving it.
//
// Measured on the bench, looking at the glass: the chapter of `la-vaca-cega.epub` is **1154 B and 2 pages**, and with
// the position at byte **586** the foot says **`ch01 2/2 51%`** with the second ball solid. A probe book of **6160
// words** —the poem twenty times— is **27 pages**: the foot says `ch01 1/27 0%` and the strip shows **8 balls** with the
// first one solid, and the count landed over the turns of the loop with one repaint of the foot and no repaint of the
// page. The reader is **612 lines** of Lua. `pio run -e emulator` says SUCCESS with 0 warnings and `pio test -e test
// --without-testing` builds the **23 suites and 530 cases** with 0 warnings, one of them new (`test_ui`: three hundred
// pages in a strip of two hundred pixels, the window of the open page inside the box).
// **Pending**: the 530 cases are **not executed** (that wants the owner's word for it); **what a page of `measure` costs
// on the board is not measured** —on the PC a walk of twenty-seven pages does not reach a millisecond, and the Xtensa is
// not the PC—, which is exactly why the count is spread over turns; the strip of the paper project shows fourteen balls
// and this one eight, because the foot of this reader shares its row with the words of the chapter; and the dots of a
// chapter of more than 512 pages stop at 512 (docs/46, C5).
// 0.138.1 (7/11): **the foot is the one of the reader of the paper, and 0.138.0 had invented its own**.
//
// The owner, with the glass in front of him: *«a l'app original el peu de C5 es diferent, perqué no el fas igual?»*. He
// was right, and the reason it was not is a thing worth writing down: 0.138.0 took the call of the system —`ui.dots`, the
// **Dots** control of the paper project, balls of 19 and 25 pixels on a strip of 37— and put it at the right of a text of
// its own. The foot of the reader of the paper is another drawing:
//
//   - **84 rows and two bands**: a line across at two rows, `Chapter %d/%d · %d%%` at eight in the face of the app with
//     the end of the chapter on the right, and the strip of dots **under them**, centred, in a band of its own;
//   - **the strip is the app's and not the control's**: balls of **12** pixels with **6** of air, a window of **14**, and
//     at each end the window does not reach, **three balls that get smaller** —8, 6 and 4— which is how it says that there
//     are many more and it cannot draw them. It is drawn in a canvas of the width of the page, so the whole band is one
//     layer and one region of damage, which is what the control was for; the numbers are the ones of the paper;
//   - **`Chapter 1/2` and not `ch01`**: the chapter is counted among the chapters the reader can see —the `.xhtml`,
//     `.html` and `.htm` of the book that are not inside `META-INF`, in the order of the names, which is the list
//     `epub.first_chapter` walks until the OPF is read (C2)— and **the percentage is of the book**, not of the chapter:
//     the chapters behind plus the share of this one, over the chapters there are;
//   - **and `chapter end` / `the end`** on the right when the page is the last one of the chapter or of the book.
//
// **Two limits of this build, said and not hidden**: the separator of the paper is the middle dot `·` and the face of the
// system is 95 glyphs of ASCII, so the foot says `-` — a Latin-1 face of the system is the owner's decision and it is
// written in docs/46 D3—; and the `loading...` the paper puts in the foot while a page is being composed is not here,
// because this system composes the page inside the turn of the app and a word on the glass for thirty milliseconds is ink
// spent on nothing (the screen of a book that is opening is C8).
//
// Measured on the bench, looking at the glass: `la-vaca-cega.epub` says **`Chapter 1/2 - 0%`** with three balls and the
// first one solid, and the chapter is 3 pages with the taller foot —the page is 876 rows now—; a probe book of 6160 words
// says **`Chapter 1/1 - 18%`** with a window of **14 balls** —the fifth of them solid— and **three smaller ones** after it,
// which are the pages the window does not show; the same foot on a dark page is a black band with a white line, white words
// and white balls, and `chapter end` on the right. The reader is **724 lines** of Lua. `pio run -e emulator` says SUCCESS
// with 0 warnings and `pio test -e test --without-testing` still builds the 23 suites and 530 cases with 0 warnings.
// **Pending**: the 530 cases are **not executed**; the `·`; the `loading...`; and the foot of the shelf, which the paper
// also has and this app does not draw (docs/46, C5).
// 0.139.0 (7/11): **the strip of dots says that there are more pages than balls, with the drawing of the paper**.
//
// The owner, with the glass in front of him: *«l'UIControl Dots (paginador) es molt mes sofisticat a PaperOS que a la
// versio lunokOS ... quan hi han masses pàgines, el paginador original fa una línia amb marques de nivell per expressar
// a l'usuari que son masses pàgines per mostrar-ho amb boletes»*. He was right about the drawing and about the reason:
//
//   - **the strip itself was already the paper's, pixel for pixel** —balls of 19 and 25, a ring of 2, an air of 7, a
//     rod of 3 and 12 of air under it—, and that is what the test `test_the_dots_of_a_page_are_the_strip_of_the_paper_
//     project` has held since 0.112.0;
//   - **what was missing is the end of a window that cannot show every page**: this build slid the window of balls and
//     said nothing else, so a chapter of three hundred pages and a chapter of seven looked the same; the paper closes
//     the window with **three balls that get smaller** —its foot strip writes them beside a ball of 12 as 8, 6 and 4
//     (`FOOT_TRAIL`), with 4 of air between two and 8 from the strip—, and that closing is the "there are many more and
//     I cannot draw them" a person reads without counting;
//   - **the numbers are the paper's ratios and not a copy of its sizes**: 2/3, 1/2 and 1/3 of the ball, an air of a
//     third and a separation of two thirds, which beside the ball of 25 of this strip are **16, 12 and 8**, **8** of
//     air and **16** of separation —68 pixels at each end—. Beside the ball of 12 of the paper's foot the same rule
//     gives 8, 6 and 4 with 4 and 8, which is exactly what the app writes: one rule, two sizes (docs/30);
//   - **the window shrinks so the marks fit at both ends**, which is the widest case and not the current one: the strip
//     does not change its width while the page turns, so a book does not see the counter jump when a mark appears. In
//     the 540 pixels of the paper's glass seventeen balls fit (537 px) and, with the marks, **twelve** —377 px of strip
//     and 68 + 68 of marks, 513 of 540—; the marks are drawn only at the ends the window does not reach, so the first
//     page of a book has three on the right and none on the left (docs/30, docs/90);
//   - **a box too narrow for the three marks keeps the plain window of 0.94.1** —the 200-pixel strip of the tests is one
//     of those—, because a mark squeezed into a box that does not hold it is worse than no mark at all;
//   - **and the strip still owes one damaged region**: the balls, the rod and up to six marks are one `mark_box`, or
//     the regions would pass the eight a canvas holds and the page turn would become a wash of the panel (docs/90).
//
// `pio test -e test --without-testing` builds the 23 suites and **531 cases** with 0 warnings, one of them new
// (`test_ui`: forty pages in five hundred and forty pixels, the window of twelve, the marks at 57, 35, 17 and 481, 503,
// 521, the first page with marks only on the right, and seventeen pages with none at all).
// **Pending**: the 531 cases are **not executed** (that wants the owner's word for it); **the drawing has not been seen
// on the glass** this turn —the bench was not run—, and what is proven is the arithmetic of the test; and the foot of the
// reader of the paper still draws its own strip in Lua with balls of 12 (docs/46, C5), which is the same drawing at the
// size of a foot and could be this control the day the foot is a control and not a canvas.
// 0.140.0 (8/11): **the frame of the reader of the paper**, and five of its pending fitxes in one go.
//
// The reader was reading a book; what it did not have is what a person expects around the page. Each one is a fitxa of
// docs/46, done with the app that needs it and not with new machinery where the system already had the piece:
//
//   - **C8, the loading page**: the panel only starts travelling when the callback that drew has finished, so a book
//     that is opened from the shelf (or from `run <id> <path>`) first draws a page of the size of the glass that says
//     `Opening the book...` and the name of the book, presents it in that turn, and is opened in the next one — one
//     line at the top of the loop (`pending`) and the same road for both doors, which is the paper's `openOnNextTurn`;
//   - **C10, the pass of a page**: the turn of a page asks the glass for `du` when **both pages are of two tones** and
//     for the wash (`gc16`) when either is not. This engine paints words, rules and plates and nothing between them, so
//     the levels of a page are exactly `has_picture` and no counter has to walk a canvas; the app keeps the answer for
//     the page that is on the glass and decides with the two. A page of text over a page of text is the two-level pass
//     the paper asks for, and a plate —or the page that comes after one— takes the full one (docs/50);
//   - **C12, an error is a notice**: the book that will not open, the chapter with no words this reader can read and
//     the page that cannot be composed are a `ui.notification_bar` over the shelf or over the page, and the card of the
//     notice opens a `ui.dialog` with the whole sentence. The console line stays for whoever reads the log; the person
//     looking at the glass has the sentence in front of them and a card that is not cut at the edge;
//   - **C13, two faces of the book**: `/fonts/dejavusans-latin1.ttf` is new —**21 360 B, 232 glyphs, 55 of them made of
//     pieces flattened, 77 points and 5 contours at the largest**, cut from the DejaVu Sans with `tools/ttf_latin1.py`
//     exactly as the serif of 0.127.0— and the titles of a page wear it while the words keep the serif. A disk without
//     the sans reads the titles in the serif and a disk with neither reads the book in the face of the system, so the
//     two files are a choice of the disk and not a requirement;
//   - **C3, a tap is a page**: the right half of the page is the one after this one and the left half the one before,
//     which is the finger people who read on a phone already have. The foot is not the page, so a tap on it does
//     nothing, a long press is still the menu, and the keys of the board keep turning the page because the page has no
//     focusable control (docs/30).
//
// The app test is new and runs the app of the repository on a disk with the book and the two faces of the repository:
// `test/test_apps` `test_the_reader_turns_its_page_with_a_finger` — the whole glass (`540x960`), the byte zero in
// `reading.txt`, a tap on the right half that moves the byte forward, the third page painted with **mode 1** (`du`)
// because two pages of words meet, and two taps on the left half that walk back to zero. The suites are **532 cases**
// now, and `pio test -e test --without-testing` builds the 23 of them with **0 warnings** —the app suite with its object
// thrown away first, so the new test was compiled and not read from a cache—.
// **Pending, and said out loud**: the **532 cases are not executed** —that wants the owner's word for it— and **the
// glass has not been looked at** this turn: the bench was not run, so the loading page, the band of the notice and the
// titles in the sans are proven by the code and the test and not by a photograph of the panel. The page count the test
// assumes (three pages in the first chapter of `la-vaca-cega.epub`) comes from the app's own count and is the first
// thing to check on the first run. Still missing, with their fitxes in docs/46: C7 (the cover as a state), C15 (a page
// by number), C2 with C14 (the OPF and the contents, which want the ZIP door B6–B7), C11 (the cache of thumbnails,
// which wants B9) and the foot of the shelf.
static const char kVersion[] = "0.140.0";

}  // namespace kernel

#endif  // LUNOK_KERNEL_VERSION_H
