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
static const char kVersion[] = "0.98.0";

}  // namespace kernel

#endif  // LUNOK_KERNEL_VERSION_H
