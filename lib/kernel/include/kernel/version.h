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
// seed of math.random, the RTC chip of the Paper, the keys and lights of the emulator, the
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
// letter— in the canvas, in the panel driver and in the emulator; the damage list that runs full threw the
// last region away instead of growing to hold it (eight pieces at the top and one at the bottom came out
// as 430 rows); the emulator's glass recorded the last `blit` instead of the rectangle that changed, so the
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
// Running it said three things that were not so: the emulator **never announced `display`**, so an app that
// asked `sys.caps().display` was told no on an emulator whose glass was there —and the view behind it was
// built anyway—; the console of the emulator and the kernel's `printf` were two streams, so the boot lines
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
// 0.19.1 (1/11): **the day on the board, written down**. The control layer says the same on the emulator and on the Paper
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
// in `docs/50`. **And one error the emulator brought out**: the memory of a Lua *userdata* does not come at zero, and the
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
// come out— and it counts now what the system takes to come up: 108 ms on the emulator. And the meters that **contain**
// others (`boot`, `kernel.loop`, `lua.run`, `lua.own`, `lua.call`) do not go into the sum of the doors, or the same
// microsecond was counted three times and the total went past a hundred per cent (138 %, measured). It costs **1 208 B
// of RAM and 9 076 B of flash** on the Paper (docs/30, docs/50, docs/75, docs/80, docs/85).
// 0.21.0 (1/11): **the glass of the emulator in a window**. `--gui` opens an X11 window with the buffer of the
// `MemorySurface` inside, and paints it every time the panel paints —the counter of paints is the question, so a turn
// with no paint costs nothing— with the sixteen levels in greys and the page as the app draws it (540x960, not the
// glass lying down). It carries `--display` with it, it says no if there is no `DISPLAY`, and closing it ends the run
// like three ESC. It is X11 and a `-lX11` behind `-DLUNOK_GUI_X11`; without Xlib the emulator compiles the same and says it
// has no window. **The window is not the panel**: there is no ink, no ghost and no blink in it, and the conversion of
// four bits to a pixel —the part that can be tested with no screen— has its test in `test_ui` (docs/85).
// 0.22.0 (1/11): **the glass of the emulator moves ink**. An e-ink panel does not light pixels: a paint of `du` leaves a
// quarter of what was there, the dirt piles up if nothing cleans, one in a thousand misses a level, and a paint lasts
// as long as its quality (450 ms a `gc16`, 120 a `du`), so `idle()` on the emulator tells the truth and `run panel` gives
// 480 ms there instead of 1. The paper is warm (`#EFEADC`) and the ink is not completely black (`#1A1B18`), because a
// page read in a perfect monochrome is a page that looks better than the one the person will have in their hands. The
// whole model is a table (`emu_ink.cpp`) and `--no-ink` takes it out; the tests run with it out and two of them try it
// —the trace of a `du`, the clean of a `gc16`, and the time of each quality— (docs/50, docs/85).
// 0.23.0 (1/11): **the emulator pays the bus and draws the blink**. A paint is worth now what it is worth on the board:
// the region that changes is sent before the paint is asked for (`ink_transfer_us`) at 0.8 µs a byte —the bus of the
// driver, 10 MHz— **plus 1 100 µs per row**, which is the only number of this model that comes out of a measurement:
// the page of `run panel` cost 1 985 ms on the board and its counts leave that per row, and the whole path on the emulator
// gives 1 970 ms (docs/95, E-7). The wait is inside `flush`, the way it is on the board: the bus stops the kernel and
// the emulator does the same with a `usleep`, and a paint waits for the ink of the one before (`wait_paint`). And **`a2`
// blinks**: white, then black and then the image (`step_ink`), which is what the sheet of the maker says and not what
// this unit did, where `a2` painted nothing (docs/95, E-7). `--no-ink` is still the glass of the tests: there is nothing
// to send either (docs/50, docs/85).
// 0.24.0 (1/11): **the glass of the emulator has fingers**. `--touch` gives the Paper's model its GT911
// (`emu::Gt911Panel`): the frame of the page —a finger at (100,200) is at (100,172) of the view—, the **two** fingers
// of its configuration, the size of the contact the board gave (28 and 30) and what it costs to read it: **800 µs**
// with no finger, **1 830** with one and **2 550** with two, three transactions at 100 kHz and nine bits per byte, plus
// the ~17 ms of the probe of 186 bytes. **And the real problem**: the chip scans by itself and gives the frame **once**,
// so a reader faster than the glass reads "nobody" with a finger on it; `--touch-scan <ms>` is how that is reproduced
// (40 ms against a turn of 20) and `Gt911Panel::missed()` counts it. On the emulator there is no bus, and that is said
// too (docs/50, docs/85, docs/95).
// 0.25.0 (1/11): **one switch for every time of the emulator**. `Options::delays` —which is born **false**, which is how
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
// the walk of the allocator (20 µs): the bytes are CPU time and this emulator has no CPU clock, and it says so.
// `--read-delay` disappears because the flat clock was part of what was fixed (docs/50, docs/85).
// 0.27.0 (1/11): **the battery the emulator runs on, borrowed from the board**. The cell of the Paper is **1150 mAh** and
// it is read as millivolts behind a divider on GPIO35; the machine of the emulator says a percentage. `--battery-host`
// takes that percentage and runs it through **the curve of the cell of the model** (`hal::battery_mv`, the inverse of
// `battery_percent`), so 63 % of the laptop is 3 847 mV of the Paper and not the 11 V of its battery. And what it costs
// to ask is paid: **four conversions of ADC at ~100 µs and ~1 ms of characterisation the first time**. With
// `--no-delay`, nothing (docs/50, docs/85).
// 0.28.0 (1/11): **three apps that try the emulator and the board with the same page**. `ghost` paints eight passes of
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
// 0.29.0 (1/11): **the code of the app costs too**. The emulator ran the instructions of Lua at the speed of a PC, so a
// loop that lasts a second on the glass lasted one millisecond here and no number of `lua.own` meant anything. The hook
// of the interpreter counts **two thousand** of them and pays the price when the sum passes half a millisecond, and what
// is left is paid at the end of the turn (`--lua-cpu`, **60 ns at 160 MHz**, divided by the simulated frequency). The
// band says what one instruction costs —zero on a board, where the time is the real one— and the emulator is the one that
// says sixty (docs/85).
// 0.30.0 (1/11): **the battery of the emulator does not have to be asked for any more**. A model with a gauge answers
// `power.battery()` on the first try, because the `%` of this machine is read by itself at the boot and goes through the
// curve of the cell of the Paper; a machine with no battery leaves the cell at **its nominal charge (3 700 mV, 42 %)**, says
// so, and `--battery-mv` rules over the loan. Before this, `run battery` on the Paper said "this board does not know its
// battery", which is what the board says only when it has none (docs/50, docs/85).
// 0.31.0 (1/11): **the hardware of the model is there from the boot, and does not have to be asked for**. The glass and
// the GT911 were opt-in, and `run draw` on a Paper of the emulator said "this board needs a touch panel and a glass for
// this": a board somebody has in their hands does not have that state. `Model::glass` says which board draws now (the
// Paper; the ATOMs have no screen), the binary turns on the glass and the fingers the model has, and `--no-display` /
// `--no-touch` take them out for a measurement that is not of pixels. A `--display` or a `--gui` on an ATOM is refused
// saying so, and `sys.caps()` only announces what the board has (docs/50, docs/85).
// 0.32.0 (1/11): **the mouse of the window is a finger of the GT911**. A Paper of the emulator with `--gui` had the glass on
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
// 0.34.1 (1/11): **the side of the buttons is said by the descriptor of the input, and not by the emulator**. In 0.34.0 the
// strip was on the left and that was it, when what rules is `IInput::face()`: the dial of the Paper is on the **left
// face** (key 0 = GPIO38 centre, key 1 = GPIO37 `Spot::kUp`, key 2 = GPIO39 `Spot::kDown`) and the key of an ATOM is on
// the **top face**. The side comes out of the face now and out of the order of the spots, so a dial is a column on that
// side and a key of the top face is a row under the page, and the page is painted where the keys leave it
// (docs/50, docs/85).
// 0.34.2 (1/11): **the dial of the Paper is on the right of the page, and the descriptor said on the left**. Whoever has
// the board in their hands says it plainly, and `Face::kLeft` was a guess of the sheet of the maker (their README
// contradicts itself about left and right, docs/50). `hal_paper` and the emulator say `Face::kRight` now, and the window
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
// 0.43.0 (2/11): **the window of the emulator at the size a person asked for** (`--gui-scale <1..4>`): the page
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
// 0.48.0 (2/11): **the window of the emulator is portable**. The pure colours, scaling and key geometry are
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
// (docs/85). Measured on the emulator: a pipe says `no terminal, ansi not asked`, a pseudo-terminal that does not
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
// 0.56.1 (3/11): **the emulator tells the truth about a run that failed, and an emulator without a glass has no
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
// 0.58.0 (3/11): **the emulator has the memory of the board and the clock of its turns.** The PC's heap is
// handed out against the two homes of the model —the RAM the board reports free at the prompt (338 864 B
// on an ATOM, 162 728 B on the Paper) and the PSRAM it can map (4 095 KB, none on the ATOMs)— with
// sixteen bytes of header per block and the board's own rule: the other home before a refusal, and a
// block that fits neither is refused. `mem` and `sys.heap()` now say what the glass says, a table of
// 400 000 entries dies on the Paper with `not enough memory`, and `run heap` gives its megabyte back to
// the byte. And a turn of the kernel pays what it costs on the board: 850 µs after every turn while the
// emulator pays the times of the hardware, so a thousand awaits of a millisecond take 1 888 ms here and
// 1 990 ms on the glass instead of the 1 080 ms they took before (docs/75, docs/85, docs/90).
// 0.59.0 (3/11): **what the board remembers, the emulator remembers too.** The credentials of a network go
// to the key-value store —`lunoknet/ssid` for the name and `wifidrv/pass` for the pair, the board's own
// split— instead of two strings of the run, so `wifi` after a restart says the same network and
// `wifi connect` gets back on it; provisioning writes them too and `wifi forget` erases the name, which
// is what the board's `own_record_erase()` does. The hour that a chip with a coin cell
// keeps survives between runs in `<state>/clock.bin`, with the time the board was unplugged added in,
// so the Paper comes back saying `source rtc`; and `--persist` is a board that was never unplugged: the
// RTC RAM is not wiped on the ATOMs and the hour comes back there as `soc`. The NVS and the FRAM needed
// nothing: they are flash and ferrite (docs/60, docs/70, docs/85, docs/90).
// 0.60.0 (3/11): **the emulator pays what each chip costs, with the numbers of its datasheet.** The heap of
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
// 0.60.2 (3/11): **turning the board on is not free either.** The emulator pays what the board does before
// the kernel exists: the bootloader reads the image out of the flash (2 097 008 B on an ATOM, 2 182 672 on
// the Paper, at the ten megabytes a second of dual SPI at forty megahertz) and ESP-IDF's PSRAM test
// writes and reads every mapped byte at the twenty of quad SPI, so the prompt arrives 210 ms later on an
// ATOM and 638 ms later on the Paper —0,39 s and 0,84 s from the start instead of 0,19 and 0,20— and the
// run says whether it is paying (docs/75, docs/85).
// 0.61.0 (3/11): **the page of the emulator is the page the address answers.** `http` from the prompt and
// `http.get` from an app fetch the real URL: the emulator hands it to `curl` in a child of its own —`-q`,
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
// instead of being written halfway; on the emulator the file `curl` already wrote is copied through the
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
// saw was a page that changed in one go. The fault was the emulator's and not the model's: a paint has its caller stopped for
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
//   - And **an error of the heap of the emulator**, which is what brought the problem out: a block of the PSRAM that could
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
// boards, and `data/` plus a **live link** to `apps/` for the Paper (11,9 MB) and the emulator. Measured the
// same day: `mklittlefs` **follows** a symbolic link to a directory, and so does the emulator (`emu_storage`
// uses `stat()` and `S_ISDIR()`), so there is one source, no copy to go stale, and the Paper's image does
// carry the apps. The emulator reads `build/disk` by default, the tests read `apps/…` instead of
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
// —and the one the emulator reads— behind its back. `tools/make_disk.py` now assembles `build/disk/<env>` and
// points `PROJECT_DATA_DIR` at it from a pre-script, which runs before the filesystem target is made (read in
// PlatformIO's own `builder/main.py`: the pre-scripts come after the `PROJECT_DATA_DIR` default and before
// `DataToBin`), so `-t buildfs` and `-t uploadfs` take the tree of their own environment. Inside it, `apps/`
// is a **folder of links, one per app**, to `apps/` at the root —not one link to the directory— so a build can
// carry a subset with `custom_apps = id1,id2` and stop carrying an app by not listing it, and `mklittlefs`
// follows every one of them (measured on 3/11). **The tests copy instead of linking** (`custom_apps_copied`
// on that environment) because a test creates and deletes apps in the disk it mounts, and writing through a
// link would write into the repository. The native binaries learn which disk is theirs at build time
// (`-DLUNOK_DISK_DIR`), so the emulator reads `build/disk/emulator` and the suite `build/disk/test` and neither
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
//   - **The disk of the emulator is the one of whoever asks for it again.** `Options::flash_dir` had `LUNOK_DISK_DIR`
//     serialised into it, so it **covered `fs_dir`** and every test read the disk of the build and not its own: five
//     tests of `test_img` said the pictures that were there were not. The disk of the build is the default of the
//     **binary** (`main_host` already sets it) and not of the structure of options.
//   - **The ceiling of an app goes up to 128 KB** (it was 64): the floor of the interpreter is **29 842 B** since the UI
//     library (13 021 of Lua, 13 200 of the UI and 3 600 of the other globals), and on the ATOM that left 35 KB for the
//     app: the same `writer` of the test that wrote six files of 15 000 B began to say `not enough memory`, and
//     `run edit` did not start. The ATOM answers **338 864 B free**, and 128 KB is 39 % (docs/75, docs/85).
//   - **Each suite has its own disk, and for real**: `test_shell` had no disk —it wrote what it found, and that is why its
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
// **And it works**: three things of the emulator were hiding the finger. The GT911 of the emulator scans by itself and **keeps
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
// 0.84.3 (4/11): **the tests give back what they ask for.** Two things of the emulator were not of the process but of the
// terminal of whoever starts it, and both were left broken. The first: `pump_captured` and the other captures put
// `stdout` in a file with `dup2`, and a `TEST_ASSERT` that fails goes out through a `longjmp` of Unity and does not pass
// through the line that puts it back —the failure and the rest of the run were written to the file, `pio test` waited for
// a summary that did not arrive and killed it with SIGHUP—; the console is put back in `tearDown` now, which Unity calls
// whatever happens, and what had been captured is said again when the test has failed. The second: `pal::init()` puts
// the terminal in raw mode and `pal::shutdown()` puts it back, but no suite says anything about it, so the terminal of
// whoever had started the tests was left without echo until a `reset`; the PAL of the emulator installs `atexit` when it
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
// 0.88.2 (4/11): **the emulator has no app at all**. Neither on the flash nor on the card: its disk is the system (`res/`,
// `http/`, `data/`) and the slot is empty until somebody puts a card in it, so `apps` on an emulator just made says
// `no apps` and `run` says it does not know the name. An app arrives at the emulator as it arrives at a board: with
// `repo install <app>`, or with a card of its own through `--sd`. **And `/apps` is a mount only when the directory is
// there**: a disk with no apps has no such row, so `ls /` and `mounts` do not show a door that leads nowhere (the emulator
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
// the reader knows where it is. **Measured on the emulator** with forty entries of about a hundred bytes: **3 131 µs** one
// call each against **110 µs** in one walk, **28 times less**, and the gap grows with the number of entries. The reader
// gives its file back when the folder is finished and also when an install dies in the middle of one (docs/70, docs/75).
// 0.88.6 (4/11): **`repo install` has a test of the whole chain, and the emulator answers per address to make it possible.**
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
// 0.88.16 (4/11): **two lines that said something that is not true.** The window of the emulator answered a mouse that
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
// already this emulator's— and the test that guards it asks `tcgetattr` for the flags of before and of after, with three
// `init`s in the middle (measured: `c_lflag` 35387 before and 35377 after with the defect, which is `ECHO` and `ICANON`
// gone; 35387 and 35387 with the fix) (found on the emulator, 4/11, docs/80, docs/85).
// 0.88.18 (4/11): **the emulator says what not blinking costs.** `glr16` and `gld16` —the qualities an app presents with,
// the bar repaints a box with and the band of the keyboard is washed with, chosen **because they do not blink**, which
// is the rule of this project— were modelled as a perfect clean: no ghost, no dirt, no speckle. With that, the one
// decision this system takes everywhere was free here, and a page could be painted a thousand times without showing a
// mark, which is the one thing an emulator must not say. A waveform that does not blink does not take the page back: it
// lands close to the level and not on it, so the model gives them **two sixteenths** of what was there and **one
// sixteenth of dirt per paint** (lighter than a `du`, which is five and one, and not the clean a `gc16` is), with a
// speckle of two in a thousand. **The numbers are the model's and not a measurement** (docs/95): the test that guards
// them paints black with `du`, white over it with `gld16` and reads the glass (docs/85, docs/92).
// 0.88.19 (4/11): **`/apps` is the two disks in one directory, and it is there from the boot.** The row was added only
// when `littlefs/apps` existed at `vfs::init`, and the emulator is a disk with no app at all: `repo install squares` wrote
// the first one, the install said `2 files, 4028 bytes`, `apps` listed it —it walks the drive— and `run` answered
// `cannot read: /apps/squares/squares.lua`, because the door of the apps had never been opened. The folder is **made**
// now on every mounted disk (a card too), so the door is always a row, and it is what its name promises: `ls /apps`
// shows `littlefs/apps` and `sd/apps` together —the card first, an app of the card is the one that runs, and no name
// twice— and a path under it reads the disk that runs that app, which is the same rule `apps` and `root_of` already
// followed. **Measured on the emulator**: a disk with no `apps/` boots with 8 mounts instead of 7 and `ls /` shows it,
// and the two tests that guard it —the door on a disk that had none, and the card and the flash in one listing— fall
// when either half is taken away (found on the emulator, 4/11, docs/40, docs/70).
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
// on the emulator**: the suite is **419 cases and all of them pass**, and each half of this was taken away on purpose to
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
// —the mount row that is a disk and not an alias—. Measured on the emulator: the suite is **420 cases and all of them
// pass**, and with the manifest question taken out the test of the folder that is not an app goes red, and with the disk
// asked twice instead of once the delete says the app is still there (docs/30, docs/40, docs/70).
// 0.89.1 (5/11): **the two metering scopes that sat after their `return`**. `power::mono_now` and `net::now_ms`
// kept the `return` of the one-liner they were when the meter arrived (0.20.0), so the scope that follows it was dead
// code the compiler threw away and `clock.mono` never counted the reads of the power policy and of the radio;
// `button::now_ms` had the same shape and was fixed in 0.49.1. The three count the read now, which is what docs/30
// and docs/50 promise (docs/30, docs/50, docs/75).
// 0.89.2 (5/11): **the boot does not walk `/apps` any more**. The `[APPS] N found` line counted every app at every
// boot —a manifest read, a JSON parse and a `stat` of the entry for each— and the boot was therefore a function of how
// many apps the disk carried, which docs/10 and docs/40 say it is not: measured on the emulator with the repository's
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
// in the order the disk gives them, and the CI's file system put `txt` before `txts` where the emulator put `txts` first,
// so `1` started the other app and the assertion failed; the test now reads the number the selector printed for `txts`
// and answers that. The kernel was right; the test was the one that depended on readdir (docs/40, docs/80).
// 0.89.7 (5/11): **the ESP32 platform is pinned to 6.5.0**. A fresh CI runner took the latest espressif32, whose
// Arduino core is the split `framework-arduinoespressif32-esp32-libs` package: its deprecated `driver/adc.h` emits a
// `#warning` and the project's `-Werror` turned it into a failed `hal_esp32/battery.cpp`, while the same tree built
// here. The numbers of docs/75 and the README are measured with a toolchain, and the toolchain is now the one
// `platformio.ini` names (docs/75, docs/80).
// 0.90.0 (5/11): **an app that never yields no longer takes the board with it**. The interpreter installs the count
// hook on every band —not only where the emulator prices instructions— and a turn is 100 000 instructions; when it
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
// `ttgo-t-koala`, the emulator has the same model so an app of this board can be tried on the PC, and the CI
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
// Measured on the emulator: 431 native cases green, and each half of it falls when it is taken away —without the paper
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
// Measured on the emulator: 432 native cases green, the app builds on the Paper with no field refused and each of its eight
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
// Measured on the emulator: `[I][UI] face /fonts/dejavusans-bold.ttf, 8 px of body at scale 1`, 433 native cases green, and
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
// **And the same day the amber of the owner made the emulator able to SHOW the page**: `--dump <file>` writes the glass to
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
// Measured on the emulator: **433 native cases green** (90 of them in `test_ui`), the tests of the button and of the keyboard
// fall when their drawing is broken on purpose (`drop = 0` and the margin taken away), and the emulator, `paper` and
// `atom_matrix` build with no warning.
// 0.97.1 (7/11): **a page of the network no longer takes the memory the TLS handshake needs, and the handshake itself
// goes to the extra memory.** On the M5Paper the owner ran `repo` and the board answered `mbedtls_ssl_setup returned
// -0x7F00` —`MBEDTLS_ERR_SSL_ALLOC_FAILED`, the library saying it could not have a block— with the network up and the
// address resolved; in the emulator the same command brought sixty folders down, because an emulator has the memory of a PC.
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
// Measured on the emulator: 433 native cases green, and the pages of the catalogue drawn with `--dump` compare with the
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
// Measured on the emulator: 433 native cases green, the twelve apps run with no error of Lua, and two of them looked at with
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
// Measured on the emulator: 434 native cases green —the one of `ui.focus` presses the centre key with and without the call and
// sees which button answers—, the app runs in five pages with no error of Lua, and the Paper builds with no warning.
// 0.97.5 (7/11): **the environments are named after the boards.** The owner asked for `M5 Atom`, `M5 Atom Matrix`,
// `M5 Paper`…, and PlatformIO has a rule that makes that name its own: *«the name can contain alphanumeric, underscore,
// and hyphen characters (a-z, 0-9, -, _)»* —it says so and refuses the build—, so the boards are **`m5-atom`**,
// **`m5-atom-matrix`**, **`m5-paper`**, **`esp32-devkit`** (a bare module, which is the one that has no name of a
// product) and **`ttgo-t-koala`** (the LilyGO, which is not an M5). The emulator keeps `emulator` and the suite `test`,
// which are not boards. The five build, the CI and every command of the documentation and the README use the new names,
// and the directories of the old ones are gone from `build/` (docs/80).
// Measured: `m5-atom`, `m5-atom-matrix`, `m5-paper`, `esp32-devkit` and `ttgo-t-koala` build (five SUCCESS, from a clean
// build directory each, 78 to 88 s), 434 native cases green, and `pio project config` reads the seven environments.
//
// 0.98.0 (8/11): **a key paints its own answer on the glass.** `ui_button_event` takes the press before the app hears
// anything, so the ring moving to the next control and a button going down were changes in the tree that nobody put
// on the glass: the app only paints when it asks for a present, and an app waiting in `sys.delay` asks for nothing.
// The owner pressed the dial on the board and read it as *«l'app Showcase no respon a l'usuari»* — the walk was
// working (three stops and the centre delivered, measured on the emulator) and the glass said nothing. This is the
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
// 0.110.0 (8/11): **the suite runs, and the emulator gives the terminal back.** With the owner's leave the whole native suite
// was run for the first time since 0.104.0, and it found three things that no reading had found:
//   - **A page of more than a hundred and twenty-seven controls took the process down.** The two walks of `open_draw`
//     held the slot in an `int8_t`: past the hundred and twenty-seventh the chain was a negative number, the table was
//     read before its first slot and the emulator died with a segmentation fault in `button_draw` —the same `int8_t` the
//     slot of the dial had until 0.106.0—. It is an `int16_t` now, and the page of a hundred and forty buttons of the
//     new test walks and takes its ring.
//   - **A cell that is not in the list had a place.** `icon_grid_cell` answered for any cell of the page, so an app that
//     asked for the ninth cell of a grid of eight was given a box nobody draws.
//   - **A child the app turned off was given back by its father.** `ui_set_disabled_below` wrote its own mark over a
//     `disabled` the app had set, so `panel.disabled = true` and `false` around it lost the app's decision; only the
//     child the father is the one turning off is his now.
// And `pal_host` **puts the terminal back when the process dies on a signal**: `atexit` is not reached by a fault, so a
// suite that stopped on the segmentation fault above (or an emulator killed with a `kill`) left the terminal of the person
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
// and `/res/splash.png` for every one. `boot`, `shutdown`, `charging` and `sleep`; the emulator carries three pictures of
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
// Measured: the editor on the emulator with the band up has a page of **540 x 656** and a band of **256** px, and the
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
// `test_an_app_that_names_a_picture_has_it_painted_before_it_starts`, which runs an app of the emulator and reads the
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
// Measured on the emulator: `[I][CFG] /cfg on sd (asked for sd), keyboard qwerty, boot boot-alt`,
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
// the row of the line under the page. Measured on the emulator: the page of the editor has no frame and the text starts at
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
// **Three tests could not have passed and had never run**: two read pixels of a panel that was off —the emulator is born
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
// finish`), because a no without a reason is an afternoon finding out which of the five cases it was; and **the emulator
// can say no too** (`--ink-refuse`): while the ink moves, `flush` answers false with the driver's own reason, which is
// what makes the waiting testable instead of lucky.
// The test written is `test_the_splash_waits_for_the_ink_of_the_page_before`: the first drawing leaves the whole page
// moving ink, the emulator refuses a paint, and the second drawing lands anyway because the splash waited; taking `settle`
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
// The second test is `test_a_page_the_panel_refused_is_painted_when_it_can`: the emulator says no while the boot's picture
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
// carrying every app as copies (its own tests write to the disk they mount) and the emulator carries none, as it has since
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
// The emulator can reproduce it (`Options::refuse_why`) and the new case is
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
// only until View turns it off. Measured on the emulator (m5stack-paper, `--flash` the tree): `apps info editor` shows
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
// by zero. Measured on the emulator (m5stack-paper, `--flash` the tree): `run calculator keys` prints
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
// Two new cases, `test_the_line_height_of_a_face_is_what_centres_a_text` (the call, against the face the emulator
// boots with) and `test_the_calculator_of_the_paper_project_adds_with_the_finger` (the app from the disk, run
// with real fingers on the emulator's panel: `7 + 3 = 10`, `8 / 0 = Error` and the key that clears it,
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
// is the original's: big, title, body, small. Measured on the emulator with the font of the disk (m5stack-paper,
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
// Measured on the emulator with the font of the disk (m5stack-paper, `--flash build/disk/emulator` and the tree as
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
// Measured on the emulator (m5stack-paper, `--flash build/disk/emulator` and the tree as the card): `apps info
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
// Measured on the emulator (m5stack-paper, the font of the disk, the tree as the card): `run about` prints the
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
// Measured on the emulator (m5stack-paper, the font of the disk, the app on the card, the clock fixed at 2026-11-01
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
// Measured on the emulator of the computer: `lib/zip` is **a header of 109 lines and 293 of C++**, with `-Wall -Wextra
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
// Measured on the glass of the emulator, with a probe app that loads the face and paints three lines: `caf` is **54 px**
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
// Measured on the emulator of the computer: `lib/epub` is **186 lines of header and 805 of C++**, with `-Wall
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
// Measured on the emulator of the computer: `test/test_font` has **four cases more** —a bold run is exactly one pixel
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
// Measured on the emulator of the computer: `lib/epub/page.h` is **138 lines** and `page.cpp` **646**, with `-Wall
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
// Measured on the emulator of the computer: **88 lines of header and 390 of C++**, with `-Wall -Wextra -Werror`, and
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
// Measured on the emulator of the computer, with the emulator's glass and `--dump`: the sample book is
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
// Measured on the emulator of the computer, looking at the glass: the sample book grew a plate drawn by
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
// Measured on the emulator of the computer, looking at the glass: `apps/reader/reader.lua` is **316 lines** of Lua and
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
// disk to carry it —that file is his—, and until then the app of the emulator runs from the card.
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
// Measured on the emulator of the computer, looking at the glass: `apps/reader/reader.lua` is **332 lines** of Lua, the
// page is composed into a canvas of **540 × 872** (the view is 912 and the foot takes 40) and the foot says
// `ch01 0%` with nothing under it. `apps/reader` is named in `custom_apps` of `platformio.ini`, which its owner asked
// for in the same breath, so a built disk carries the app and not only the card.
// **Pending**: the 521 cases are **not executed** (that wants the owner's word for it); **the emulator cannot press a key
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
// Measured on the emulator of the computer, looking at the glass: `apps/reader/reader.lua` is **375 lines** of Lua and asks
// for the whole glass one line after it starts. Run with `run reader /littlefs/epub/la-vaca-cega.epub` the log says
// `whole glass: the view is 540x960 at 0,0`, the wash after it is the two-level one (`a2`, the bar's ink went with it)
// and **the page is the whole panel**: the ink of the poem starts at row 8 and the foot `ch01 0%` ends at row 935, where
// with the bar it was 48 rows less. A probe app that takes the whole glass, paints it black and dies leaves **the bar
// back on the glass** (the hour is read in the dump) and the view at 540 × 912; a probe that asks for it back gets
// `under the bar glass: the view is 540x912 at 0,48` and the bar paints itself whole again. `pio run -e emulator` says
// SUCCESS with 0 warnings and `pio test -e test --without-testing` builds the **23 suites and 527 cases** with 0
// warnings, five of them new (four in `test_bar`, one in `test_touch`).
// **Pending**: the 527 cases are **not executed** (that wants the owner's word for it); **the three taps were not
// exercised on the glass**, because the emulator lays no fingers —`--script` runs shell lines and the panel of the emulator
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
// Measured on the emulator, looking at the glass: a probe that opens the book, clears its canvas to 0 and asks for
// `{dark = true}` gives **black paper and white words**, with the plate of the poem inverted (its white paper is black
// and its lines are white); the same page with the menu over it shows the row `Book View` —with the View inverted, and
// a black card with white words on the dark page— and the two entries with their boxes, `Full screen` checked. The
// reader itself is **492 lines** of Lua and still composes its page from row 8 to row 935 with the whole glass.
// `pio run -e emulator` says SUCCESS with 0 warnings and `pio test -e test --without-testing` builds the **23 suites and
// 529 cases** with 0 warnings, two of them new in `test_ui` (a bitmap inverted on a dark page and a page of a book
// painted with the ink it is given).
// **Pending**: the 529 cases are **not executed** (that wants the owner's word for it); **the long press was not
// exercised on the glass** —the emulator lays no fingers—, so what is proven there is the paint of the menu and of the dark
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
// Measured on the emulator, looking at the glass: the chapter of `la-vaca-cega.epub` is **1154 B and 2 pages**, and with
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
// Measured on the emulator, looking at the glass: `la-vaca-cega.epub` says **`Chapter 1/2 - 0%`** with three balls and the
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
// on the glass** this turn —the emulator was not run—, and what is proven is the arithmetic of the test; and the foot of the
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
// glass has not been looked at** this turn: the emulator was not run, so the loading page, the band of the notice and the
// titles in the sans are proven by the code and the test and not by a photograph of the panel. The page count the test
// assumes (three pages in the first chapter of `la-vaca-cega.epub`) comes from the app's own count and is the first
// thing to check on the first run. Still missing, with their fitxes in docs/46: C7 (the cover as a state), C15 (a page
// by number), C2 with C14 (the OPF and the contents, which want the ZIP door B6–B7), C11 (the cache of thumbnails,
// which wants B9) and the foot of the shelf.
// 0.141.0 (8/11): **the keys of the board are the page's only after the centre enters it**, which is the rule the slider
// already had. The owner, with the reader in his hands: *«si la pàgina del llibre té focus i s'ha polsat el botó físic
// central es pot pujar i baixar pàgina amb els botons físics (com l'Slider, que necessita un pas d'entrada al control per
// poder usar els botons). Si no, els botons físics funcionen normal recorrent controls. Un cop obtingut el focus a la
// pàgina s'ha de polsar el botó central per poder passar pàgines»*.
//
//   - **the gate**: the centre toggles `page_mode` and, while it is on, key 1 is the page before and key 2 the page
//     after. Before the centre the arrows are the walk of the dial, and this page carries no focusable control to walk,
//     so they do nothing —which is the same answer the walk gives on a page with one control and nothing else—;
//   - **it is the app's flag and not a new control**, and the two reasons are the ones the system would charge for: a
//     focusable control is a **pointer target**, so a tap on the page would enter the page as well as turn it (the tap
//     turns the page in 0.140.0 and has to keep doing so), and a page with a focusable control **takes the centre away
//     from the three presses that close an app** (`ui_has_focusable`, docs/30), which is the only way out of an app
//     without a control on the Paper. When the page gets other controls, `ui_button_event` takes the keys before the app
//     and this flag is not reached — the rule of the walk wins by itself (docs/30);
//   - **the mode says so on the glass**: the right of the foot says `keys` while it lasts, on the row that already says
//     the end of a chapter, because a mode nobody sees is a key that looks broken. A menu, a notice, leaving the book or
//     closing it take the mode away, and the page is entered again with the centre.
//
// The app test of 0.140.0 grew this half —`test_the_reader_turns_its_page_with_a_finger_and_with_the_keys`—: after the
// taps have walked the book to the byte zero, key 2 does nothing, the centre followed by key 2 lands on the byte of the
// second page, key 1 comes back to zero, and after a second and a half (the window of the three presses that close an
// app) the centre leaves the page and key 2 does nothing again. The suites are still **532 cases**, and
// `pio test -e test --without-testing` builds the 23 with **0 warnings**.
// **Pending**: the cases are **not executed** and **the glass has not been looked at** — the `keys` mark of the foot and
// the mode itself are proven by the code and the test, not by a photograph of the panel.
// 0.142.0 (8/11): **the book says what it is** — the OPF of the book, read from Lua (docs/46, C2).
//
// Until today the reader opened the first file whose name looked like a chapter and counted the chapters by their
// names: a book whose `ch02.xhtml` is the first of the spine opened in the wrong place, and its title was the name of
// the file. The two halves of the fix are the door and the reading:
//
//   - **the door** (B6): `book::read(name)` takes an entry of the book out of the zip into **a block of its own, 64 KB
//     asked for the first time an app reads one**, because the chapter is a block of its own and a page is laid out
//     from it —a read that landed there would leave the reader with a page of the OPF—. The compressed bytes go through
//     the block of the stream, which is scratch between two chapters. `epub.read(name)` copies what came out into a Lua
//     string once, so the block is free for the next read;
//   - **the reading** (C2, in the app): `META-INF/container.xml` names the OPF, the OPF gives `dc:title` and the
//     manifest, and the **spine** gives the order —which is the order of the book and not the order of the names—. The
//     `href`s are resolved against the folder of the OPF with `..` and `.` taken out, an `href` the book does not carry
//     is dropped, and a book whose OPF says nothing this reader can use falls back to the `.xhtml`/`.html`/`.htm`
//     outside `META-INF` sorted by name, which is what it did before. A saved position that names a chapter the book no
//     longer has opens at the first one instead of failing;
//   - **what a person sees**: the bar carries the title of the book and the foot's `Chapter i/n` counts the spine of the
//     book and not its file names. The log says one line when a book opens —`reader: <title>, <n> chapter(s)`—, which is
//     how a test proves that the OPF was read and not guessed.
//
// Tests: `test_book` has a new case (**the two doors give the same bytes**, the chapter is still the one that was open
// after a read and a page of it is laid out, a name the book does not carry and a closed book say so), and
// `test/test_apps` reads the line of the sample book (`La vaca cega, 2 chapters`). The suites are **533 cases**, and
// `pio test -e test --without-testing` builds the 23 with **0 warnings**.
// **Pending**: the cases are **not executed** and **the glass has not been looked at** this turn; what is missing of
// the book, with its fitxa in docs/46: **C14** (the index, `nav`/NCX, which the OPF now makes reachable), **C7** (the
// cover as a state; the OPF already carries the `cover` item for it), **C15** (a page by number), **C11** (the cache of
// thumbnails, which wants B9) and the foot of the shelf.
// 0.143.0 (8/11): **the index of the book, on the glass and in the menu** (docs/46, C14) — and the ceiling of an app's
// source, raised for it.
//
//   - **the index, read**: EPUB 3 leaves a `nav` in an XHTML file of its own and EPUB 2 a `toc.ncx` with a `navPoint`
//     per entry. The two end in a label and an `href`, the `#fragment` is dropped —an entry lands on the first page of
//     its chapter, which is what this reader can do— and the `href` becomes a chapter of the spine; two entries in the
//     same file keep the first one, the chapter itself; and a book that declares no index **gets one made of its
//     chapters** (`Chapter k`) when it has more than one, which is the one that lets chapter thirty of eighty be
//     reached. The name is the number and not the title, because a title means inflating every chapter when the book is
//     opened and the book keeps one in memory. The log says where it came from: `index 2 (ncx)`;
//   - **the index, on the glass**: `Book → Contents` gives the page back and puts up a `ui.list` that pages itself,
//     with **a control for every row** —the dial walks the chapters and the centre opens one, and a finger opens the row
//     it presses—. The row of the chapter that is open is the marked one and the list opens on its page. A long press
//     or a swipe puts it away; choosing a row opens the chapter from its first byte, recounts its pages and remembers
//     the position, and a chapter that cannot be read leaves the reader on the shelf with a notice;
//   - **the finger that chose belongs to the choice**: the system answers the press and the same finger arrives once
//     more as a `tap`, so for six hundred milliseconds after a choice a tap is that one and not a page turn. It was a
//     latent bug of the menu of 0.137.0 (choosing *Dark page* turned a page behind the card) and the index made it
//     impossible to ignore;
//   - **the ceiling of the source is 65 536 B** (docs/75): the reader is **49 710 B** of Lua and the 49 152 B cap said
//     `too big to run`. The block is the heap's and goes back before the app runs a line, so 64 KB are 32 KB of heap
//     while an app starts and nothing while it runs. It is a step and not the decision docs/46 D6 asks for.
//
// The sample book of `data/epub/` carries a real index since this version (`tools/make_epub.py` writes the `toc.ncx`
// and names it in the spine), and the app test reads the line: `La vaca cega, 2 chapter(s), index 2 (ncx)`. The suites
// are **533 cases**, and `pio test -e test --without-testing` builds the 23 with **0 warnings**.
// **Pending**: the cases are **not executed** and **the glass has not been looked at** this turn: the list of the index,
// its marked row and the `Contents` entry are proven by the code and by the line of the log, not by a photograph of the
// panel. Of the book, what is still missing with its fitxa in docs/46: **C7** (the cover as a state), **C15** (a page by
// number), **C11** (the cache of thumbnails, which wants B9) and the foot of the shelf.
// 0.144.0 (8/11): **the cover of the book, as a state of the reader** (docs/46, C7).
//
// The first page of the first chapter starts at **byte zero**, so «I am on the cover» cannot be read from the byte: it is
// the page before that one and a state of the reader, which is what the paper had (`book.cover`) and what this turn adds.
//
//   - **the door**: `book::picture_into(slot, name)` —and `epub.picture(canvas, name)` in Lua— paints a picture of the
//     book on a canvas by its **whole name inside the book** (`OEBPS/img/cover.png`), centred, with the paper of the
//     book around it. The name is whole because a cover is not inside a chapter: the plate of a page is resolved
//     against the chapter and this one comes resolved from the OPF. The two share the cache and the decoder of
//     `load_picture`, which was split in two (`load_picture_named` and the resolver). **A picture keeps its paper**: it
//     is not inverted on the dark page, because a negative cover is not a cover (C9);
//   - **which picture**: the item the OPF names —`<meta name="cover" content="id">` of EPUB 2, `properties` with
//     `cover-image` of EPUB 3, or an image whose id or file name says `cover`— and only if the entry is really in the
//     book. A book without one has no cover screen and the first page is where it opens;
//   - **the state**: the cover sits **before the first page of the first chapter and only there**; back from that page
//     is the cover, forward from the cover is the page, and it is **saved with the position** —the fourth field of
//     `reading.txt`, `path TAB chapter TAB byte TAB cover`— so a book left on its cover opens on its cover, drawn
//     without a present of the first page under it. A resize, a menu closed over it, the dark page and the index all
//     come back to the cover because they come back through `build_the_book`;
//   - **what it says**: the foot says `Cover - right to read` (the hyphen of this face, as in C5), the strip of the
//     balls is cleared and does not come back while the cover is up, and the page after the cover takes the **wash**,
//     because a cover is a picture and not two tones (C10).
//
// The sample book of `data/epub/` carries a cover since this version (`tools/make_epub.py` draws it and names it in the
// OPF; the book is 4 845 B), and the app test reads the line —`La vaca cega, 2 chapter(s), index 2 (ncx), cover yes`—
// and walks the state: from the first page a tap on the left half leaves the file saying `cover 1` **with the byte still
// zero**, and a tap on the right half says `cover 0` on the same zero. The suites are still **533 cases**, and
// `pio test -e test --without-testing` builds the 23 with **0 warnings**.
// **Pending**: the cases are **not executed** and **the glass has not been looked at** this turn: the cover drawn in the
// middle of the page, the foot of the cover and the pass that leaves it are proven by the code and by the test, not by a
// photograph of the panel. Of the book, what is still missing with its fitxa in docs/46: **C15** (a page by number),
// **C11** (the cache of thumbnails, which wants B9) and the foot of the shelf.
// 0.145.0 (8/11): **a page by its number, and the menu from the keys** (docs/46, C15 and C4).
//
//   - **`Book → Page`**: the list of the pages of the chapter, one row `Page k` for every page the count of the chapter
//     knows (C5), opened at the page that is on the glass with that row marked. The dial walks the rows, the centre or a
//     finger opens one, and the reader jumps to **the byte that page starts at** —the table of the count— with the wash,
//     because a jump of ten pages is not a turn. The number is **walked and not typed**: a field that takes a number
//     brings the keyboard band, 581 rows of this glass, and walking a list with the two keys is quicker and does not
//     take the page away (docs/30);
//   - **it is the same screen as the index**: `open_index` and `open_pages` both build their rows and hand them to
//     `open_list(head, rows, marked, pick)`, so the two half-pages of the reader are one function and one drawing
//     (docs/46, C14);
//   - **the menu from the keys**: a **long press of the centre** opens the menu with the `Book` unfolded —the arrows
//     only walk the entries of the open title and a title is unfolded by the glass and not by the dial, so a person at
//     the keys had no menu at all until today— and the second long press folds it. The finger keeps the menu it had: a
//     long press on the glass opens it with the `View` down. A long press is delivered to the app even when a control
//     has the dial, because `ui_button_event` only takes `press`;
//   - **it is app only**: no door, no control and no number of the system changed this turn, and the app is **57 818 B**
//     of Lua —under the 65 536 B of the source ceiling of 0.143.0—.
//
// The app test walks the whole thing with the keys: a long press of the centre, two arrows to `Page`, the centre, two
// arrows to the second row and the centre again, and the file of positions says the byte of the second page —the same
// byte the taps reached in the first half of the test—. The suites are still **533 cases**, and
// `pio test -e test --without-testing` builds the 23 with **0 warnings**.
// **Pending**: the cases are **not executed** and **the glass has not been looked at** this turn: the list of pages and
// the long press of the centre are proven by the code and by the test, not by a photograph of the panel. Of the book,
// what is still missing with its fitxa in docs/46: **C11** (the cache of thumbnails, which wants B9) and the foot of
// the shelf.
// 0.146.0 (8/11): **the cache of the book, beside the book** (docs/46, C11).
//
//   - **`ICS1` is a picture of this system and not a private trick of the reader** (docs/30): `lib/img` read it and now
//     it **writes** it too (`ics_bytes`, `ics_header`, `ics_row`), because the cache one app leaves is a photo for the
//     next one. The width is written always even —two pixels share a byte— and an odd picture is padded one column wider
//     with **paper** in the last nibble, not with the nibble a decoder happened to leave there, which was a black stripe
//     down the side of a thumbnail;
//   - **the file is `.<the book's own file name>.cache.ics`**, in the folder of the book: `.la-vaca-cega.epub.cache.ics`
//     beside `.la-vaca-cega.epub`. The point in front is what keeps it out of a shelf listing and the name of the book is
//     what makes a folder of thirty books readable. The cover of the sample (300 × 400) is **48 × 64**, so the file is the
//     8 B of the header and 64 rows of 24 B: **1 544 B**;
//   - **the system writes it and not the app**, because an app cannot write beside a book (docs/46, C6): the door is
//     `epub.thumbnail(name[, max])` —`max` 64 by default, 256 at the most— and `book::thumbnail` builds the path, scales
//     from the block of the pictures and writes **a row at a time** through a line of 128 B, so the cache is never a file
//     in the memory of the board;
//   - **three rules of the paper and one of this turn**: a cache that is there **and whole** is not written again (the
//     size of the file has to be the one its own two words of the header say, so a cache cut in the middle by a power
//     that went off is written again, and a write that fails **removes** what it left instead of leaving half a picture
//     there); the room is asked **before** the picture is scaled (under 8 KB free, no cache: it is a courtesy beside a
//     book and not the book); and the app asks inside a `pcall`, once per book opened and not at every page. **Nothing of
//     B9**: no surface is saved and `ui.image_save` is still not there;
//   - **the app is the caller**: six comment lines and one call in `open_book`, **58 296 B** of Lua under the 65 536 B of
//     the source ceiling. Nothing else of the reader changed.
//
// The cache goes round in two places: the image suite writes one with `ics_header` and `ics_row` and reads it back —the
// odd picture padded with paper, a file cut in the middle refused as `kTruncated` and a header of an odd width as bad
// data—, and the app test opens the book and then reads `/epub/.la-vaca-cega.epub.cache.ics` as a picture of 48 × 64.
// The suites are **534 cases**, and `pio test -e test --without-testing` builds the 23 with **0 warnings**.
// **Pending**: the cases are **not executed** and **the glass has not been looked at** this turn. And the cache is not a
// thumbnail yet: the shelf still draws its books as words, so this turn leaves the file that the app which draws them
// will read. Of the book, what is still missing with its fitxa in docs/46: the foot of the shelf.
// 0.147.0 (8/11): **the foot of the shelf** (docs/46, C1).
//
//   - **the shelf is a page and its foot is the page's**: the band of 84 rows at the bottom of the glass with the paper
//     of the page, the line across its top —the second row of the band, from x 16 to x 523, sixteen pixels of paper on
//     each side— and one row of words: the books it has on the left (`1 book`, `12 books`) and what the centre does on
//     the right (`centre opens`). The grid of cells stops eight rows above the band, so a cell never reaches the words;
//   - **the row of dots of the paper's shelf is NOT here, and that is a decision**: the grid already says which page it
//     is on at the bottom of its own box (`2 / 3`, with a triangle on each side that has somewhere to go), and the
//     system had written that a grid's footer replaces the row of dots —two counters of one number on one glass is one
//     too many—. The count of the books is the shelf's because the grid counts pages of cells and not books;
//   - **the labels go back with the glass**: they are released where the foot of a page is released, so going back to
//     the shelf thirty times leaves one foot and not thirty, because a label left behind keeps its words on the glass
//     for ever (docs/30, docs/90);
//   - **and a bug of the shelf that came out with the foot**: the glass was paper only where the grid and the foot drew,
//     so coming back from a page read in the dark left the margins and the air in black —the ink of a page that is not
//     there any more—. The shelf now paints its paper **from edge to edge** before it draws anything (docs/46, C9);
//   - **app only**: no door, no control and no number of the system changed this turn. The app is **60 930 B** of Lua
//     under the 65 536 B of the source ceiling: **4 606 B** of room, which is what the thumbnail on the shelf has.
//
// The test opens the reader with no book —the shelf is where it starts (B10)— and reads the glass: the line of the foot
// in ink at row 878 with paper outside it, the two words of the band in ink in their own halves of it, and the row of
// paper under the grid still paper. The suites are **535 cases**, and `pio test -e test --without-testing` builds the 23
// with **0 warnings**.
// **Pending**: the cases are **not executed** and **the glass has not been looked at** this turn. Of the book, what is
// still missing with its fitxa in docs/46: **the thumbnail drawn in a cell of the shelf** —the cache of a book is
// written beside it since 0.146.0 and nothing reads it back for a cell yet— and the size of the app (D6).
// 0.147.1 (8/11): **the plan says what is left, and one of its rows was wrong** (docs/46, docs/90). No line of code
// changed: the suites are the same **535 cases**, and `pio test -e test --without-testing` builds the 23 with **0
// warnings**.
//
//   - **A1 was stale and it lied**: it said the container of a book was not in this system while `lib/zip` has been
//     here since the port —`Source`/`Entry`/`Archive`, **293 + 109 lines** and **7 cases** in `test/test_zip`—. The row
//     now says what the container is: the deflate is `lib/inflate`, `stored` decompresses nothing, **nothing
//     allocates** (the table of entries and the chapter's block belong to the caller, and the only block of its own is
//     the window of the closing record, static and not on the stack), and what it refuses has its word — `kNotAZip`,
//     `kSplit`, `kZip64`, `kDirectory`, `kNoRoom`, `kBadData`, `kEncrypted`, `kMethod` and `kCrc`;
//   - **the order of what is left is the owner's, of 8/11**, and it is written in §6 of docs/46 so that no turn invents
//     it: the **embedded fonts** of the book (TTF and WOFF) first, then the **fixed layout**, then **SVG** as a picture,
//     and **the fourteen layout holes at the very end** (*«en parlarem al acabar tot això»*);
//   - **B7 and B8 are wanted after all**: the reader does not need the two doors of the zip —`epub.read` carries the
//     `container.xml` and the OPF— but another app does, and the example the owner gave is a **browser of files**
//     opening a `.zip`, so the two rows stop being a question and become work. **D7** (`prefs` of an app) is accepted;
//   - **and the thumbnail of the shelf has a fitxa of its own (C16)**: what was missing there is the **cell**, not the
//     file —the cache is written beside the book since 0.146.0 and no cell reads it back—, which is what the app of the
//     files does with the preview of a file: the icon of a thing is a picture of that thing. **Nothing of the system is
//     needed**: the cache is a picture and a cell of a grid can be a control of the app since 0.105.0.
//
// **Pending**: as before, the cases are **not executed** and **the glass has not been looked at**; the owner asks for
// those at the end of the app of the book and not now.
// 0.148.0 (8/11): **`require`: an app in more than one file** (docs/46 D6, docs/20, docs/30, docs/70).
//
//   - **the door**: `require('shelf')` reads `shelf.lua` from the folder the app was installed in —the flash, or the
//     card when it was installed there, through the same `apps::root_of` that `/res` uses— and runs it **once**; the
//     name answers the value of that first run to every later `require`, and a module that returns nothing is `true`.
//     What is loaded lives in `package.loaded`, which is the **only** field of `package`: there is no `package.path`,
//     no `cpath`, no searcher and no preload, because a module is a file of the app and the kernel is what names it;
//   - **the name is checked in the kernel and not in the interpreter**, where every other path is checked: `/` is a
//     folder inside the app (`require('pages/chapter')` is `pages/chapter.lua`), and a name that climbs (`..`), starts
//     at a unit, carries a dot or repeats a slash is `E_INVAL`. A module of another app is therefore not something an
//     app can ask for, and `load`, `loadfile` and `dofile` stay sealed: this is a door with a policy and not a loader;
//   - **the module's text lives in a block of 65 536 B** (`kModuleCap`, the ceiling of an app's source) which is
//     **given back before the module runs a line**, so a module that waits forever does not hold its own text; a text
//     that fills the block is refused as `E_TOOBIG` instead of being run by halves;
//   - **and the reason it exists is room**: a module is a chunk of its own, with its own ceiling of source and its own
//     **200 locals**. The reader of the book is **1 501 lines, 60 930 B and 113 locals** in its main chunk —87 left—
//     and that wall comes before the size wall. **This turn moves nothing of the app**: the reader is the same one
//     file, and splitting it is a turn of its own.
//
// The proof is in two places: the interpreter suite reads a fake module and checks that it runs once, that the second
// `require` does not touch the kernel, that no value is `true`, that a missing name and a module that raises are
// errors with the name of the file and the line, and that `package` carries no path; and the app suite runs a real app
// of two files on the disk and watches it read `part.lua`, not read it twice, and **fail to climb** into another
// app's folder (`../other/secret`). The suites are now **540 cases**, and `pio test -e test --without-testing` builds
// the 23 with **0 warnings**.
// **Pending**: the cases are **not executed** and **the glass has not been looked at**, as the owner asked for the end
// of the app of the book. And the half of D6 that is left is the ceiling of **one** file, which is decided when the
// reader reaches it.
// 0.149.0 (8/11): **D6 decided from both sides**: the ceiling of one file goes up and the reader is four files
// (docs/46 D6, docs/75).
//
//   - **`kSourceCap` 65 536 -> 98 304 B**: an app may be more than one file since 0.148.0, and the ceiling of **one**
//     file goes up as well because the reader still has its page, its page count and its foot in a single chunk and the
//     three things it wants next —the faces of the book, the fixed layout and SVG— all add lines to it. The cost is the
//     same as always and for the same reason: the block is the heap's and it is given back before the app runs a line,
//     so 96 KB are the biggest block an app's start asks for and nothing while it runs. The board with the least heap
//     is the ATOM, with **338 864 B** free measured, and the block is a quarter of it during the start;
//   - **the reader is four files**, and only the pieces that could leave without sharing the app's state did:
//     `chapters.lua` (**204 lines, 8 722 B**: the `container.xml`, the OPF, the `nav` and the `toc.ncx`), `library.lua`
//     (**68 lines, 2 865 B**: the shelf and the position of each book) and `notice.lua` (**90 lines, 3 609 B**: the band
//     and the card, with their own state, so the reader asks `up()` and `gone()` and never holds a control it did not
//     make). `reader.lua` is **1 203 lines and 49 215 B** with **101 locals** in its main chunk —it was 113— and 39
//     functions. The whole app is **1 565 lines and 64 411 B**;
//   - **what is NOT split, and why it is written down**: the page, its count, the strip of balls, the foot, the two
//     lists and the menu share the app's state —the book, the byte, the table of pages, the running count, the key
//     mode—, and moving them means a state table that every one of those lines has to read. That is a change to do
//     **with the tests running** and not blind, so it is left as its own turn for the day the 49 089 B of room run out;
//   - **the app's tests learned the modules**: both of them put the four files on their own disk, and the reader's test
//     now also fails if the log says `require:`, which is what a module that is not there answers.
//
// The suites are **540 cases** —no case was added: the reader's own test is the proof, since a missing module would stop
// the page from ever being built— and `pio test -e test --without-testing` builds the 23 with **0 warnings**.
// **Pending**: the cases are **not executed** and **the glass has not been looked at**, as the owner asked for the end
// of the app of the book. And this refactor's only proof today is that every file compiles and that the calls are the
// ones they were: it is the app test that will confirm it when it runs.
// 0.150.0 (8/11): **the face a book carries inside itself** (docs/46, the first programme of the owner's list).
//
//   - **`lib/ttf` opens a WOFF** (new `woff.cpp`): a `.woff` is an sfnt in a box —a header of 44 bytes, a directory of
//     twenty per table and the tables themselves zlib-compressed— and what comes out is the sfnt the TrueType reader
//     already knows: the offset table with its three bisection numbers, one record of sixteen bytes per table, the tables
//     padded to four bytes with zeros. A table that does not pay for compressing says the same length twice and is copied
//     as it is. **WOFF2 is refused by name**: it is Brotli and this system has no Brotli, and a face read wrongly is worse
//     than a face not read;
//   - **`book::font(name)`** takes the entry out of the zip into a block of **128 KB** and gives back the sfnt —the file
//     itself when it already is one, the opened box when it was a `.woff`, in a block of up to **512 KB** asked for the
//     first time a book needs it—, with a sentence for each way it cannot: the book carries no such file, a `WOFF2`, a
//     face too big, a table that does not come out of its compression, no room;
//   - **`ui::font_load_bytes`** is the third door to the same table of three faces: `font_load`'s body has become
//     `face_from_text` and the two doors end there, one with a block read from a file and one with a block a book gave.
//     The block is **copied**, because the book's block is given to the next file it reads. And the door of Lua is
//     **`epub.font(name)`**, which answers a font object like `ui.load_font` (docs/30);
//   - **the app asks for it**: `chapters.lua` takes the `item` of the manifest whose media type is a font or whose file
//     ends in `.ttf`, `.otf`, `.woff` or `.woff2`, joined against the OPF's folder and kept only when the book really
//     carries it; the first one is the **body** of the page, and the serif of the disk is back when the book closes, so a
//     face of a book is one of the three slots only while that book is open. A book with two faces uses the first: three
//     slots, two for the disk, and the second face of a book would need a fourth.
//
// Two cases prove the engine: the face suite opens a box of two tables —one stored and one compressed by the real
// zlib— and checks the sfnt byte by byte against what it has to be, and refuses a block too small, a box cut in the
// middle and bytes that are not a box; the book suite writes a book with a `.woff`, a plain file and a `WOFF2`, and
// checks that the first comes out as its sfnt, the second as it is, and the third with the word `WOFF2` in its reason.
// The suites are now **542 cases**, and `pio test -e test --without-testing` builds the 23 with **0 warnings**.
// **Pending**: the cases are **not executed** and **the glass has not been looked at**, as the owner asked for the end of
// the app of the book. **And the app half of this turn has no case of its own**: the sample book carries no face, so the
// path `manifest → epub.font → the page` is proven by the code and not by a test — the next turn that touches the sample
// is the one that can add it. A book that declares two faces uses the first (docs/46).
// 0.150.1 (8/11): **the suite run for the first time since 0.140, and the first thing it caught was `require`**
// (docs/46 D6). The owner asked for every test, and this is what came out.
//
//   - **a real bug in `require`**: it read the table of `package` with `lua_getfield(L, 1, "loaded")`, and **index 1 of a
//     function written in C is its first argument**, not the first thing the function pushed. Every table read there was
//     the module name —a string— and the `lua_setfield` that marked a module as running hit a nil. The function is
//     rewritten so that **nothing is ever counted from the bottom of the stack**: `package` and `loaded` stay on it and
//     every read is relative. That is the fix this whole turn is for, and it came out of a test, not of a reading;
//   - **the interpreter suite is green: 48 of 48**, the four cases of a module included (read once, no value is `true`,
//     a name that is not there is an error, a module that raises says `part.lua:1:`, and `package` carries no path);
//   - **the face suite is green: 5 of 5**, the WOFF box included (a table stored and a table compressed by the real zlib,
//     the sfnt byte by byte, and the three refusals);
//   - **the book suite is 7 of 8**: `test_a_picture_of_the_book_is_placed` fails on «the page says it holds a picture» and
//     it is **not** something this turn touched —the picture path, the paginator and `measure_picture` are the same lines
//     as in 0.133.0—, so it is left named here for the next turn rather than guessed at;
//   - **and 34 host paths of the test suites pointed at apps the owner has moved to `OLD/apps`**: `handles`, `mtab`,
//     `heap`, `battery`, `images`, `keys`, `light`, `matrix`, `page`, `showcase`, `sleepy`, `tilt`, `timers`, `later`,
//     `alarmqueue`, the two fonts of the face suite (`lato-black.ttf`, `lunok-8x8.ttf`) and the rest. They are repointed
//     at `OLD/apps/...`, which is where the files are, and that alone turned 36 failures of the app suite into runs that
//     actually test something;
//   - **the app suite is 149 of 188 and its roots are not the book**: `ntp` and `http` without a radio answer `E_BUSY`
//     (something is left in flight), `test_every_app_of_the_repository_declares_its_boards` cannot find the apps, and
//     `test_the_boot_paints_the_drawing_the_settings_chose` **leaves an app running** —after it,
//     `app_running()` is true and every `cannot start the interpreter` in the suite is that one leak—. The reader, the
//     shelf and the modules fail there **by cascade and not by their own case**: they are the last tests of the file.
//
// **Pending**: `test_book`'s picture case, the leak of the boot-drawing test, the two network cases, the repository-apps
// case, and the app suite run clean enough to see the reader. The glass has still not been looked at.
// 0.150.2 (8/11): **the tests no longer reach into `OLD/apps`, and the suite's real state is written down**.
//
//   - **fourteen cases of the app suite are gone**: `handles`, `mtab`, `heap`, `battery`, `images`, `keys`, `light`,
//     `matrix`, `page`, `showcase`, `sleepy`, `tilt`, `timers`, `later` and the two alarm ones ran apps that are **not in
//     `apps/` any more**, so they were testing nothing —the files the owner moved are untouched—. The app suite is now
//     **174 cases** instead of 188;
//   - **the vectors are the suites' own**: the face suite reads `test/test_ttf/res/lunok-8x8.ttf` and
//     `lato-black.ttf`, and the font suite `test/test_font/res/abcdef-8x8.bdf` and `.pcf`, copied from where the apps
//     that carried them were. **`grep -rn "OLD/apps" test/` answers nothing**: no test depends on that folder;
//   - **and the honest map of the suite, run whole (534 cases, 55 failed, 473 succeeded)**: sixteen suites green —
//     `led`, `battery`, `calendar`, `ttf`, `zip`, `disk`, `inflate`, `json`, `ui`, `orientation`, `luabind`, `qr`,
//     `line_editor`, `alarms`, `bar`, `shell`— and seven red: **`test_apps` 136/174**, **`test_layout` 8 failures**,
//     **`test_font` 5**, **`test_book` 1**, **`test_epub` 1**, **`test_touch` 1**, **`test_img` 1**. The numbers of
//     `test_layout` (`Expected 24 Was 96`, `3 Was 2`) and of `test_font` (`Expected 8 Was 41057872`, `-3 Was 2`) are
//     the paginator's metrics and a face read as garbage, and **they are not from this session's four commits**: none of
//     them touched `lib/epub`, `lib/kernel/font.cpp` or the paginator. They were hidden behind the app tests that died
//     first —the suite had not been executed since 0.140, so a red suite was not known to be red—;
//   - **in `test_apps` the roots are still not the book**: `ntp` and `http` without a radio answer `E_BUSY`,
//     `test_every_app_of_the_repository_declares_its_boards` finds no apps, `test_ui_focus_puts_the_dial_where_the_app_says`
//     and `test_the_splash_waits_for_the_ink_of_the_page_before` get a world another test did not leave as it found it,
//     and `test_the_boot_paints_the_drawing_the_settings_chose` **leaves an app running** — every `cannot start the
//     interpreter` after it is that one leak, and the reader, the shelf and the modules are the last tests of the file.
//
// **Pending**: the seven red suites, in that order of size, and the glass. This turn did not run a single test of the
// engine that the red ones cover: it ran the suite, took `OLD` out of it, and wrote down what it found.
// 0.151.0 (8/11): **the leak that hid a dozen cases: two settings a case can turn on and not turn off**.
//
//   - **the app the board starts by itself** (0.119.0): `test_the_home_app_comes_up_with_the_board` names `comer` as the
//     home app and fails before it can clear it, so **every boot after it starts that app**. An app running before the
//     case asked for one is `cannot start the interpreter` for the case, and that message —which reads like a broken
//     kernel— was the leak. The tear-down now puts the home app back to empty, where it was;
//   - **and the emulator goes back whole**: the tear-down reset three fields of `emu::Options` by hand, so `delays`,
//     `ink_refuse`, `display` and `touch` stayed on for the next case, and a glass that refuses paints while an app
//     starts makes the interpreter's own setup raise — the same message again, from the other side. It now builds a
//     fresh `Options` and keeps only where the suite keeps its disks;
//   - **what that opened**: the **app suite goes from 38 failures to 14**, and the whole suite from **55 to 31**
//     (**534 cases, 497 succeeded**). And the reader's own test **runs at last**: it opens the book, turns pages with a
//     finger and with the keys, and fails on **one** assertion — `tap(100, 400)` from the first page of the first
//     chapter does not leave `cover = 1` in the file of positions, and the cover is not the problem: it says `cover yes`
//     in its own log. That is the next thing to look at, not a guess;
//   - **the fourteen that are left in `test_apps` are not one thing**: `ntp` and `http` without a radio answer `E_BUSY`,
//     `test_ui_focus_puts_the_dial_where_the_app_says`, `test_an_app_opens_a_list_of_extensions` and
//     `test_apps_info_shows_the_keys_a_manifest_carries` fail on a `TRUE` that is not; `test_the_home_app_comes_up_with_the_board`
//     still fails; `about` and `timeclock` print a whole console and then fail on the assertion after it; and the shelf's
//     foot test fails on its first `TEST_ASSERT_TRUE`, which is a `boot_once` question and not a drawing one.
//
// **Pending**: those fourteen, the eight of `test_layout`, the five of `test_font`, and the four single ones. The glass
// has still not been looked at.
// 0.151.1 (8/11): **an emulator that cannot write rubbish: `emu::Options` has a default in every one of its 35 plain fields**.
//
// The root of the repository filled with more than forty files of exactly **518 415 bytes** — a PGM of the glass (15-byte
// header plus 540x960 pixels) — whose names were raw stack bytes. The writer was not a mystery: a commit of the glass
// writes the picture whenever `emu::options().dump != nullptr` (`emu_surface.cpp:362`), and `emu::Options` is a plain
// struct with no constructor, so **`emu::Options opts;` was whatever the stack had**. Five test files said it that way
// (test_apps, test_book, test_json, test_led, test_shell) and the `tearDown` of the app suite did too
// (`emu::Options fresh;`, added in 0.151.0 — my line, and the one that made it worse). A garbage `dump` is a garbage
// file name, written once per paint.
//
// Fixed where it comes from: all 35 plain fields of the struct have a default now (`nullptr`, `0`, `false`), which is
// **exactly what the global `g_options` already was** — it is zero-initialised — so no behaviour changes and no
// declaration of it can come out as rubbish again; and every declaration that exists says so with braces
// (`emu::Options opts{};`) so the next reader sees the danger. The emulator itself was already safe: `parse()` sets
// `opts.dump = nullptr` before it looks at the arguments. It is written in `docs/85` next to `--dump`.
//
// **Measured**: `pio test -e test --without-testing` builds all 23 suites with **no warning**; the tree has no file left
// whose name is not printable ASCII (only the accented EPUB under `OLD/PaperOS/tests/`, which is a real name).
// **Pending**: the suites themselves, untouched — the 31 failing cases of 0.151.0 are where they were, and the glass
// still has not been looked at.
// 0.151.2 (8/11): **the glass of the reader, dead, and the two errors that leave it dead for the whole board**.
//
//   The board said it in its own log: `run reader`, two paints, and then `the panel had stopped answering; the rail was
//   cycled and it is back: 540x960 turned 270 (glass 65535x65535), image at 0xffffffff, words ffff ffff f`. From that
//   moment nothing painted, for anybody, until the board was restarted — *«no pinta res de res al eink i després ningú
//   pot tocar la pantalla fins al reinici»*. It had happened with the splash too.
//
//   - **`ffff` is not a panel that came back** (`it8951.cpp`, `arm()`): after the rail cycle `read_words` "succeeds" and
//     hands back twenty `ffff`, and the driver believed them —a glass of 65535 by 65535 with its image at 0xffffffff—,
//     so `arm()` said yes, `ready_` stayed up, and every page after that was loaded into an address nobody has, with
//     the paint accepted and the glass never changing. It was one dead paint turned into a dead board. The four words
//     are checked **before** they are believed now: all ones, a zero, a side past `kMaxGlassSide` (4 096) or an image
//     at `0xffffffff` are "no panel", `revive()` returns false, the rail is tried again and `kReviveTries` gets to say
//     the truth in the log;
//   - **a `du` never covers the glass** (`ui.cpp`, `paint()`): the two-level waveform is a *partial* one and this
//     controller stops answering on the bus when the whole page is asked of it in it. That is what the log shows one
//     paint before the `ffff`. Any `du`, `du4` or `a2` that covers seven eighths of the glass or more goes up as a
//     `gc16`; the threshold is generous on purpose, so a block of text that covers most of the view and the clock of
//     the bar are still `du`. The reader's own wash of the shelf asks for `gc16` by name (it was a full-glass `du`);
//   - **`apps/reader/reader.lua`**: the three `console.print('DBG …')` of the cover hunt are gone (they printed on
//     every key event), and `forget_page` is declared before `show_cover`, which called it as a global that did not
//     exist yet — the suite found it on 8/11 (`attempt to call a nil value (global 'forget_page')`).
//
//   **A test pins the rule**: `test_a_du_of_the_whole_glass_is_painted_as_a_wash` (test_ui) paints a black page, asks
//   for white over the whole glass in `du`, and checks both what the glass was told (`last_mode()` is `gc16`) and that
//   no trace of the black is left — and that a 40-by-40 `du` afterwards is still a `du`. The driver half has no host
//   test: it only exists on the board, and what it needs is a flash.
//
//   **Measured**: `pio test -e test --without-testing` builds the 23 suites with no warning; `pio run -e m5-paper`
//   builds the firmware with no warning (2 338 113 B of flash, 55,7 %). **Pending**: the firmware is not flashed and no test has
//   been run; the 31 failing cases of 0.151.0 are untouched.
// 0.151.3 (8/11): **the glass of the reader was killed by the order of three accesses, and the mode had nothing to do with it**.
//
//   The board ran the firmware of 0.151.2 and its log said the guard fired —`a a2 of 0,0 540x960 is the whole glass, and it
//   goes up as a gc16`— and then the panel died anyway, on the paint after it. So the `du` was not the killer, and that
//   theory of mine is retracted in `docs/50` where it was written. What the log shows is this:
//
//   - `flush()` **read `LUTAFSR`, cleared it and read it again before waiting** for the paint before it to be over. The
//     first three SPI accesses of a paint therefore fell **while the paint before it was still moving ink**;
//   - a controller that is running `DPY_AREA` does not answer on MISO, and those reads come back `ffff`;
//   - the driver read `ffff` as "the panel stopped answering" and did what it does for a deaf panel: **it cut the rail**,
//     in the middle of the panel's own waveform. It never came back from that, the three `kReviveTries` were spent and
//     `dead_` left the glass with nobody able to paint until a restart.
//
//   That is why it hit the reader and the splash and no other app: they are the only two that paint **whole pages back to
//   back**, and a whole page is the only thing that leaves the engine running long enough for the next paint to find it
//   working.
//
//   **Fixed in the driver, and all of it is a measure so the wrong call cannot be made** (`it8951.cpp`):
//     - `wait_paint()` goes **first**, before the panel is spoken to at all: it uses the BUSY line and the mode's own time
//       and no SPI;
//     - **a `ffff` does not buy a rail cycle**: it is asked twice more, 250 ms apart (`kSilentTries`, `kSilentPauseMs`), so
//       a controller that is only busy answers again and the board is not killed;
//     - **the wait is measured and not supposed**: `wait_paint()` measures how long BUSY stayed low after `DPY_AREA`, and
//       when the paint covered half the glass or more that number becomes the minimum of the next one —it can only grow,
//       and a blinking caret never touches it—, said in the log as `the panel held busy %u ms for a page (the table says
//       %u)`;
//     - and the 0.151.2 guard stays: `arm()` does not believe an all-`ffff` device info, and a `du`/`du4`/`a2` of seven
//       eighths of the glass or more goes up as a `gc16` (that one is about ghosting and the partial waveform, not about
//       the dead panel).
//
//   **A paint with nothing marked no longer waits** for somebody else's ink either: the check went to the top of `flush`.
//
//   **Measured**: `pio run -e m5-paper` builds with no warning, 2 338 417 B of flash (55,8 %). **Pending**: flash it and run
//   `run reader` — the log has to say `the panel held busy <n> ms for a page (the table says 450)` and no `ffff`. The
//   suites are untouched (31 failing cases of 0.151.0), and the driver half has no host test: it only exists on the board.
// 0.151.4 (8/11): **the panel said what a page costs —1051 ms, not 450— and the driver made every paint pay it**.
//
//   The board booted 0.151.3 with no `ffff` at all, which is the good news: the order of `flush()` was the bug and it is
//   gone. And it said the number this driver had been guessing:
//
//     [I][PANEL] the panel held busy 1051 ms for a page (the table says 450)
//
//   **A whole page of the Paper takes 1051 ms of ink, not the 450 ms of the maker's row.** Every wait for a page after
//   the first one was therefore cutting a waveform that had 600 ms left to run — which is what the panel was dying of.
//
//   But the measure was wired wrong: `engine_over()` used it for **every** paint, so after the splash each repaint of the
//   bar and every turn of the shell waited a whole second for ink that was not theirs (1 frame per second, and the shell
//   that does not come up healthy). It now applies **only when the paint being waited for covered half the glass or
//   more**: a page waits the measured 1051 ms, a caret or the clock of the bar takes the mode's own time (0.151.4).
//
//   **Pending**: flash it again; `run reader` has to open the shelf and the book. If the glass still says the boot logo and
//   the bar and nothing else, that is what a healthy boot of this system looks like on the emulator too (the logo stays until
//   an app paints), and what is wrong is something else — the serial log is the only witness then.
// 0.151.5 (8/11): **a glass that is gone costs nothing, and the measurements stop lying — the trigger is still unknown**.
//
//   The board of 0.151.4 said three things. The first is that the panel dies **between the two paints of the app's own
//   start**: `run reader`, the wash of the whole glass goes up (`paint 0,0 960x540 mode 2 … 540 rows of 480 B`, accepted
//   by the panel), the app runs, and the next paint finds `ffff` before it sends anything. That has now happened in three
//   runs and in the same place, which is a measurement and not a theory. The second is that everything after that was
//   worse than it had to be: a dead glass made every turn wait three seconds and printed the same warning for ever.
//   The third is that my own instrument was lying: `the panel held busy 2613 ms … 5739 ms` is the time since the last
//   paint that went out, not a paint at all.
//
//   Fixed here, and nothing of it is about the trigger:
//     - `flush()` refuses a `dead_` panel **first**, before the wait, so a gone glass costs nothing per turn;
//     - `engine_over()` answers "the ink is over" when the panel is dead, so the kernel does not keep asking;
//     - **only a paint that reached `DPY_AREA` is measured** (`painted_pending_`), and a measurement is capped by the
//       paint timeout, so the log cannot say `held busy 5739 ms` about a panel that painted nothing;
//     - the sentence of a refused page is said **once while the reason does not change**, instead of one warning per
//       turn.
//
//   **What is still open, said plainly**: between those two paints the app lists the books (`epub.books`, which walks the
//   flash **and the card**) and loads two faces from `/fonts` — and the panel goes `ffff` with no paint of ours moving.
//   So the panel is being reset or losing its supply during the app's start, and the candidates are the card on the bus
//   it shares with the panel and the two faces. The next step is an experiment on the board and not another guess:
//   `splash boot` (a whole page, no app), then `ls /epub` (the card, no paint), then `splash boot` again — and `run
//   reader` once with the card out of the slot.
//
//   **Measured**: `pio run -e m5-paper` builds with no warning, 2 338 501 B of flash (55,8 %). No test run.
// 0.151.6 (8/11): **the driver now asks the engine the way the maker's own driver asks it** — `M5EPD` was read, not guessed.
//
//   The owner's order was to read the official M5Paper library instead of guessing again, and it is in the tree:
//   `OLD/PaperOS/.pio/libdeps/m5stack-paper/M5EPD`. What it says, and what this driver was doing:
//
//   - **the engine is `LUTAFSR == 0`** (`M5EPD_Driver::CheckAFSR`: a loop on that register, three seconds at most, run
//     before every `DPY_BUF_AREA`). This driver was asking the **BUSY pin** and a table of millisecond guesses instead,
//     and the board said how wrong that is: "busy" reported a page taking 1052 ms, then 3000 (a timeout), then 2613, for
//     the same page. **The BUSY pin goes high before the engine is done**, so the next `LD_IMG_AREA` was landing on top of
//     an engine that was still reading the image — which is how the controller of this panel stops answering;
//   - **the order is**: `CheckAFSR` → `SetTargetMemoryAddr` (high, then low) → `LD_IMG_AREA` → the rows → `LD_IMG_END` →
//     `DPY_BUF_AREA` with seven words (the last two, the image address). The `LD_IMG_END` was already there; the wait was
//     the missing one;
//   - **the flag is read, never written**: the maker's driver does not clear `LUTAFSR`, and this one wrote 1 to it before
//     every paint (0.118.5) because the flag "never came down" — it never came down because the engine never finished.
//     That write is gone;
//   - `read_register()` returned **0** when the command did not go out, and 0 is exactly what "the engine is idle" reads
//     like. It returns `ffff` now, which is the sentence the rest of the driver understands;
//   - the pins, the prefixes (0x6000 command, 0x0000 data, 0x1000 read), the opcodes (0x0021, 0x0022, 0x0034, 0x1224),
//     the pixel format (`B_ENDIAN`, 4 bpp, rotate 0), the VCOM (0x0039, 0x0001, 2300), the pack-write register and the
//     image address (0x001236E0) were all already the maker's numbers, word for word. The SPI at 10 MHz, mode 0, and BUSY
//     active low too.
//
//   **What went out of the driver**: `ink_ms_of()`, `kEngineStartMs`, `measured_ms_`, `last_wide_`, `painted_pending_`,
//   `painted_ms_`, `ink_ms_` and `clear_engine_flag()` — the whole machinery of guessing how long ink takes, which existed
//   to answer a question the controller answers by itself.
//
//   **Measured**: `pio run -e m5-paper` builds with no warning, 2 338 193 B of flash (55,7 %). **Pending**: flash it. The log
//   of a paint is now `engine status %04x after the paint` —a non-zero status is an engine that took the command— and no
//   `ffff` may appear. The suites are untouched.
// 0.151.7 (8/11): **the wait was in the wrong place: the register reads zero until the engine has started**.
//
//   The board ran 0.151.6 and said the thing that settles it, in its own words: `engine status 0000 after the paint`. A
//   `LUTAFSR` read right after `DPY_BUF_AREA` is a **zero** — the engine has not marked itself busy yet — so the
//   `CheckAFSR` this driver had put **at the top of the paint** read that zero, answered "the engine is idle", and did
//   not wait at all. The second page of the reader then went out on top of the first one, and the controller stopped
//   answering: the same `ffff`, from a different mistake of mine.
//
//   `M5EPD_Driver` has the wait **after the image is loaded and `LD_IMG_END` has closed it, right before
//   `DPY_BUF_AREA`** (`WritePartGram4bpp` → `UpdateArea`: `CheckAFSR()` then the seven words). That is where it is now:
//   the load of a page is two hundred milliseconds of SPI, and by the end of it the engine before has said what it is.
//
//   And a floor, because a small paint loads in a millisecond: **a paint of ours that went out less than
//   `kEngineStartMs` (50 ms) ago is not over, whatever the register says** — the guard of 0.19.2, moved from the BUSY
//   pin to the register. It is a floor and not a measurement, and it can only make the answer later.
//
//   **Measured**: `pio run -e m5-paper` builds with no warning, 2 338 233 B of flash (55,7 %). **Pending**: flash it; the
//   reader's two paints of the whole glass must both go up, one after the other, and no `ffff` may appear.
//
//   **And on the app that "must be doing something wrong"**: it is not. The reader asks for the whole glass and paints it
//   —a legal thing, the same one the splash does at boot— and it is the only app that paints **pages** back to back
//   instead of strips under the bar. What the system has to guarantee is that a paint that big cannot break the glass,
//   and that guarantee only exists in one place: the driver, which is where it was missing (docs/50).
// 0.151.8 (8/11): **the read that failed was answering "the panel is gone", and it was cutting the rail of a busy panel**.
//
//   0.151.7 failed in the same place, and the reason is a mistake of the same family as all the others: **one number for
//   two questions**. `read_register()` answered a value whatever happened, so a read that never reached the controller
//   came back as a number; when that number was made `ffff` (0.151.6) a **failed read** became indistinguishable from a
//   **silent panel**, and the answer to a silent panel in this driver is to cut its rail. The reader paints the whole
//   glass twice when it starts; the second paint asks for the register a millisecond after the first one has gone out, the
//   bus is still occupied by the load of the second page, the read fails, the rail is cut — on a panel that was working.
//
//   Three things now, and each of them is a guarantee and not a hope:
//     - **`read_register(reg, value)` says whether it read at all**; a failed read is a retry of the wait and never a
//       rail cycle. Only a *successful* read of `ffff` is silence, and silence needs three of them;
//     - **the wait has a floor, and the floor of a page is the measured time of a page**: the board said 1051 and 1052 ms
//       for a whole page, twice, so the driver starts a page at 1050 ms and can only grow it from what the panel itself
//       reports (`a page held the engine %u ms`). A page that arrives a millisecond after another one now **cannot** load
//       its image into an engine that is still reading the one before — which is what the log of every run was showing;
//     - **the wait is asked twice**: before the image load and again before `DPY_BUF_AREA` (the maker's place), and
//       `idle()` asks with the same floor, so nothing in the system looks at a page that has just started.
//
//   And a line that is the question this driver has never been able to answer with data: `LUTAFSR %04x %u ms after the
//   paint`, once per paint.
//
//   **Measured**: `pio run -e m5-paper` builds with no warning, 2 338 741 B of flash (55,8 %). **Pending**: flash it.
// 0.151.9 (8/11): **the paint that dies is always the first one after the app reads the disk**.
//
//   Seven logs of this afternoon, read together, have one thing in common and it is not the mode, the wait, the read or
//   the size: **the paint that finds `ffff` is the first one after the app has touched the disk**. In 0.151.8 the panel
//   went silent between the wash of the whole glass and the shelf's page, and what happens in that gap is `ui.load_font`
//   twice (from `/fonts`, on the flash) and `epub.books` (the flash **and the card**). At boot, between the splash and the
//   bar, there is no disk traffic at all, and every paint goes up.
//
//   So the ink is moving —a page of this glass holds its waveform for more than a second, `a page held the engine
//   1051/1131 ms`— while the disk is read. That is the shape of the failure: disk traffic on a bus the panel shares, or a
//   supply that sags under both, while the controller is driving a whole-page waveform.
//
//   The reader now does the slow work **before** it asks for the whole glass: the two faces, the list of the shelf, and
//   only then `ui.full_screen(true)` and the page — the `on_resize` that follows does not touch the disk again
//   (`shelf_read`). The page is painted after the disk is quiet, which is what the boot does by accident.
//
//   **This is a hypothesis with evidence and it is not proven.** If the glass holds with the reader ordered this way, the
//   cause is the overlap and the rule belongs in the kernel (no disk read while `display::idle()` is false after a big
//   paint). If it does not hold, the next step is the SPI layer: who owns the bus and the CS between `give_bus()` of a
//   paint and the next register read.
//
//   **Measured**: `pio run -e m5-paper` builds with no warning, the disk image carries the new `apps/reader/reader.lua`
//   (checked with `diff`), 2 338 741 B of flash (55,8 %). **Pending**: flash it and run `run reader`.
// 0.151.10 (8/11): **the reader paints like the apps that work: under the bar, and the whole glass is a switch**.
//
//   The owner is right and I was not: **other apps read the disk and paint, and nothing happens**. So the disk is not the
//   trigger by itself, and 0.151.9's theory —disk traffic inside a page's waveform— is not enough to explain it.
//
//   What no other app of the disk does, and this one does at its very start, is **ask for the whole glass**: that makes the
//   kernel re-make the root canvas and **wash the whole page before the app has drawn anything**, so the start of the
//   reader is **two paints of the whole glass**, the system's and its own, one right after the other. Every app that works
//   lives in a view of 540x912 under the bar, and its page is one paint.
//
//   So `kFullGlass` (a constant at the top of the app, `false` now) keeps the reader under the bar: same drawing code, same
//   foot, the bar keeps its forty-eight rows, and the sequence of paints is the one of the apps that do not break the glass.
//   Set it to `true` again the day the driver can take the whole glass — and that day will be measured, not assumed.
//
//   **Measured**: `pio run -e m5-paper` builds with no warning and the disk image carries the new
//   `apps/reader/reader.lua` (checked with `diff`). **Pending**: flash it and run `run reader`. If the panel holds, the
//   culprit is the whole-glass view (the re-make plus two full-page paints) and it is a kernel question, not an app one.
//   If it does not hold, what is left of the app is the page it draws, and that is where the next measurement goes.
// 0.151.11 (8/11): **the reader does not ask for the whole glass: that is the `Full screen` entry of the book's menu**.
//
//   The owner's correction, and it is exact: in the reader of the paper, full screen is one entry of the text menu, it is
//   not something the app asks for when it opens, and much less from the main menu. `apps/reader/reader.lua` asked for it
//   at the top of the file since 0.136.0 and **that is mine and it was wrong** — the constant I put in 0.151.10 to switch
//   it off was me inventing a knob for a mistake instead of reading what the app was supposed to do.
//
//   It is gone: at start-up the reader lives under the bar (540x912) like every other app, and the whole glass is asked
//   for when the person picks `Full screen` in the menu of the book (`on_menu_pick`, which already did
//   `ui.full_screen(not ui.full_screen())` and is untouched). `docs/46` (D1, item 3) is corrected: it said the reader had
//   it on open, which is exactly the sentence that made the mistake look like a feature.
//
//   What that removes from the start of the app is the thing the board could not take: the kernel re-making the canvas and
//   **washing the whole page before the app had drawn a single pixel**, i.e. two paints of the whole glass in a row.
//
//   **Measured**: `pio run -e m5-paper` builds with no warning and the disk image carries the new
//   `apps/reader/reader.lua` (checked with `diff`). **Pending**: flash it and run `run reader`.
// 0.151.12 (8/11): **a probe app: the start-up of the reader in stages, and a small square after each one**.
//
//   0.151.11 made the moment of the death sharper than ever and it is not a paint: with no whole-glass request, the first
//   paint of the reader found the panel silent at **5530 ms after the last one** — the bar's —, so the panel died while the
//   app was loading, with nothing of ours in the air. What the reader does between the shell giving it a view and its
//   first page is: `require` of its three modules (flash), two `ui.load_font` from `/fonts` (flash), and `epub.books`
//   (flash **and** the card).
//
//   `apps/probe/` (temporary, and named as such in its manifest) does exactly that work in four stages and, after each one,
//   paints a 40x40 square and presents it:
//
//     probe: 0 nothing / 1 requires ok / 2 first face true / 3 second face true / 4 books N / probe: done
//
//   The log of a run of `run probe` says which stage came last and whether the glass still answered after it. It is the
//   measurement the seven logs of this afternoon could not give, and it costs a boot.
//
//   **The Paper's image carries it because of one line that is not committed**: `custom_apps = ...,probe` in
//   `platformio.ini` — the owner's file, left as he had it plus that name, and to be taken out when this is over.
//
//   **Measured**: `pio run -e m5-paper` builds and the disk image carries `apps/probe` (checked with `ls`).
// 0.151.13 (8/11): **the glass of the board: `storage()` took the panel's CS away from the SPI controller**.
//
//   Found with the board in my hands, over the USB serial port and the interpreter of the shell (`lua`), in one session
//   and without a single flash:
//
//     ui.rect(4,4,40,40,0,"gc16") ui.present("gc16")      -- the glass takes it
//     ui.load_font("/fonts/dejavuserif-latin1.ttf")       -- and its paint
//     ui.load_font("/fonts/dejavusans-latin1.ttf")        -- and its paint
//     epub.books()                                        -- 7 books
//     ui.rect(...) ui.present("gc16")                     -- **ffff: the panel stopped answering**
//
//   `epub.books()` and nothing else. It is the only thing in that list that calls `hal::IPlatform::storage()`, which on
//   the Paper was this:
//
//     IStorage* storage(const char* mount) override {
//       ...
//       deselect_panel();            // GPIO15 as a plain output, driven high
//
//   And `deselect_panel()` —GPIO15 written as a plain GPIO output— **takes the pad away from the SPI controller**, which is
//   the panel's CS. From that moment the panel is never selected again: its reads come back `ffff`, every paint is
//   accepted and shown nowhere, and only a reboot brings it back. **`caps()` has been guarded against exactly this since
//   0.118.9** (`if (!panel_joined_) deselect_panel();`, with the sentence *«from that moment the panel is never selected
//   again»* written next to it) and **this call was not**. `epub.books()` asks for the storage of the flash **and** of the
//   card, so the first thing the reader does —open its shelf— left the glass dead; and the shell's `ls` never did it
//   because it goes through `vfs`, which resolves the storage **once**, at mount time, when the panel had not joined yet.
//
//   That is the whole afternoon: the mode, the wait, the order, the disk, the size of the page, the whole glass of the
//   app — none of it was the cause. It was one missing `if`.
//
//   **Measured on the board** after the fix, over the serial port: `epub.books()` answers 7 and **two paints after it go
//   up** (`LUTAFSR 0000`, `engine status read`, `paint 0,496 92x40`), and `run reader` paints its shelf and its page with
//   no `ffff` at all. The driver's guards of 0.151.2 to 0.151.11 stay: they are true on their own (the busy line and the
//   engine's register are not the same question) and they are what turned this into a legible failure instead of a frozen
//   board.
//
//   `apps/probe` stays in the tree as the tool that found it; the image does not carry it (`custom_apps` of
//   `platformio.ini` is as the owner had it). It is run by adding the name to that list.
// 0.151.14 (8/11): **a book of the flash could not be opened: the path had two slashes in it**.
//
//   The board said it in one line: `VFSFileImpl(): fopen(/littlefs//epub/la-vaca-cega.epub) failed`. `book::open` splits
//   `/littlefs/epub/x.epub` into the unit and the rest, and copied the rest **with its leading slash**; every storage of
//   this system builds `<mount>/<rel>`, so that slash became a second one. **littlefs answers no to `//`**, so every book
//   of the flash was unreadable — the only book this disk carries — while FATFS swallowed it on the card, which is why
//   the shelf looked fine and the card's books opened. `vfs::resolve` skips the slashes (`while (*rest == '/') ++rest;`)
//   and that is exactly why `ls /littlefs/epub` always worked and `epub.open` did not.
//
//   Fixed in the same place and the same way: the relative path loses its leading slash when it is kept.
//
//   **Measured on the board** over the serial port, with the interpreter of the shell: `epub.open("/littlefs/epub/
//   la-vaca-cega.epub")` answers `true` and `epub.entries()` 8, and `epub.open("/sd/epub/Don_Quijote…epub")` answers
//   `true` too. `run reader` paints its shelf with no `fopen` failure at all.
//
//   **Two limits that this turn found and did not change**, said with their numbers:
//     - the shelf holds **24 books** and a path of **63 characters** (`Shelf` in `book.cpp`); a longer name is dropped in
//       silence;
//     - a book's zip table holds **`kEntries` = 128** entries, and `100-mejores-obras.epub` (231 MB, an omnibus of a
//       hundred works) does not open because of it. Raising it costs `sizeof(zip::Entry) * kEntries` of the extra memory
//       on **every** open — 128 is about 11 KB and 2048 would be about 180 KB — or the table can be made the size the
//       archive says it has, which is the right fix and a bigger one.
// 0.151.15 (8/11): **the zip table is the size of the book and not a fixed 128**.
//
//   The owner took the second of the two options 0.151.14 left open, and it is the one that costs nothing: the book is
//   asked how many entries it carries **before** any memory is taken, and the table is exactly that.
//
//   - `zip::Archive::entries_in(source, count)` reads the record that closes the zip and answers the count, making the
//     same refusals as `open` about the container itself (`kNotAZip`, `kSplit`, `kZip64`, `kDirectory`). The walk that
//     finds that record moved out of `open` into `closing_record()` so both questions use one code;
//   - `book::open` asks it, refuses a book with nothing in it and one past `zip::kMaxEntries`, and then takes a table of
//     the size the book said. `kEntries` (128, in `book.h`) is gone: `zip::kMaxEntries` went to **4 096** and is only
//     what this reader is willing to walk, because the memory taken is now the book's own size;
//   - and the smaller books got cheaper, which is the part nobody asked for and everybody gets: 8 entries are **1,2 KB**
//     of the extra memory instead of 19 KB, and the omnibus of a hundred works is **648 entries, 98 KB**, taken only
//     while it is open.
//
//   **Measured on the board** over the serial port, after the fix: `epub.open("/sd/epub/100-mejores-obras.epub")` answers
//   `true` and `epub.entries()` **648** (it answered `nil` and an error before), and
//   `epub.open("/littlefs/epub/la-vaca-cega.epub")` answers `true` with **8**. `pio run -e m5-paper` builds with no
//   warning. The shelf's other limits (24 books, a path of 63 characters) are untouched and are the next thing to raise
//   if a disk ever needs it.
// 0.151.16 (8/11): **the shelf stops dropping books in silence: sixty-four of them, and two hundred characters per path**.
//
//   The two limits 0.151.14 named and left standing, raised:
//
//     - `Shelf::kMax` **24 → 64** and a path **64 → 200 bytes** (`names[24][64]` was 1 536 B of BSS; the new block is
//       `64 × 200 = 12 800 B` and it lives in the **extra memory**, taken the first time the shelf is asked for and kept
//       for the session, because a path handed to an app has to outlive the call that made it);
//     - two hundred bytes hold `/littlefs/epub/` —fifteen— and a name of a hundred and eighty-five, which is more than
//       either file system of this board accepts, so nothing a card or the flash can hold is now dropped;
//     - `g.rel` went from 160 to **224** bytes for the same reason: the reader's own copy of the path had the old ceiling.
//
//   **A book that does not fit is still dropped in silence**, and that is the difference between a limit and a bug: with a
//   path of two hundred characters the drop cannot happen for any name the two file systems hold, which is the whole
//   point of the number.
//
//   **Measured on the board**: `epub.books()` answers the **7** paths whole (`/littlefs/epub/la-vaca-cega.epub`,
//   `/sd/epub/Trilogía de la fundación by Isaac Asimov.epub`, …), `epub.open("…/100-mejores-obras.epub")` still answers
//   `true` with **648** entries, and `mem` says `extra memory 3053 of 4095 KB usable` with the shelf's block already
//   taken. A name of a hundred and fifty characters could **not** be tried on the board: the door of `fs` refuses a write
//   outside the app's own ground, so the long side of this change is arithmetic and not a measurement — said here so it
//   is not taken for one.
// 0.151.17 (8/11): **the picture of an app goes up before the app is loaded and
// before the Lua machine exists, and the reader brings the icon and the splash of the EPUB of PaperOS**.
//
//   The owner asked for two things in one sentence: bring the original artwork
// over, and show the splash at once.
//
//     - **The artwork.** `apps/reader/res/` now holds the drawing a person edits
//       (`icon-source.png`, 448×448, greys, 2 818 B — it is
//       `OLD/PaperOS/tools/icons/EPUB.png`), the icon the manifest names
//       (`icon.png`, 94×94, 1 760 B, made with `tools/make_icon.py`, the same
//       448→94 step as `calculator` and `about`) and the composed splash
//       (`splash.png`, 420×520, 6 876 B). The M5 `icon.icn` of that app is
//       **not** ported: this system does not read that format and the manifest
//       names the PNG (docs/40);
//     - **the recipe travels too** (`tools/app_splash.py`, which is PaperOS's
//       `tools/splashgen.py` with its header and comments brought to this
//       project): the splash is not a file nobody can say how it was made, it is
//       one command —`--icon res/icon-source.png --title EPUB --foot lunokOS
//       --out res/splash.png`— and it draws the same card it drew there: the
//       icon at 232 px, a rule, the title, `loading...` and a foot. The **only**
//       difference from the original file is that foot: it said `PaperOS` and
//       says `lunokOS` here, and `--foot PaperOS` gives the old one back
//       (docs/40);
//     - **the order of a start went in two.** `run_app` used to read the app's
//       source —`malloc(98 304)` and a file read— and build the interpreter
//       before it looked at the splash, so the picture appeared *after* the most
//       expensive part of the start and did nothing while the reader (a Lua file
//       of 100 KB with the fonts of its library behind it) was coming in. The
//       lookup and the read are now two functions: `find_app` (a walk of the
//       listing, two string copies and the board check) and `read_source`
//       (the block and the read). `run_app` finds the app, then does `ui::attach()`,
//       the picture of the manifest and `bar::title`, and only then reads the
//       source and starts the VM. A start that dies in the read gives the view
//       and the bar back, as one that could not compile already did, and an alarm
//       window keeps `load_source` —the two halves in one call— because a window
//       is not a start and shows no picture (docs/40).
//
//   **The order *inside* the first half is untouched**: `ui::attach()` washes the
//   glass, and a wash after the picture is a picture nobody sees (0.118.1); what
//   changed is that the title no longer waits for the interpreter.
//
//   **Measured on the board** (Paper, over the serial port, `run reader`): the log
//   comes in this order —`[I][SPLASH] /apps/reader/res/splash.png, 420x520 at
//   60,220` with its `blit` and its `paint 0,0 960x540` first, then
//   `[I][APPS] reader, stack 7488 B free` (the breadcrumb written immediately
//   before `luabind::start`), then `[I][UI] paint 0,0 540x912` of the shelf. The
//   drawing is on the glass before the machine exists, and the shelf arrives
//   after it. `pio run -e m5-paper` is **2 339 557 B / 55,8 % of flash and
//   121 596 B / 2,7 % of RAM**, with no warning; the firmware and the disk image
//   were both written to the board and `/apps/reader/res/` holds the three files
//   (2 818, 1 760 and 6 876 B).
//
//   **The proof** is `test_the_picture_of_an_app_goes_up_before_it_is_read` in
//   `test/test_apps`: an app whose entry is one byte short of the 98 304 B block
//   is listed, gets its picture, and then dies in the read —the corner ink gone
//   and the two pixels of its picture in the middle of the glass is the picture
//   having gone up *before* the read— while
//   `test_an_app_that_names_a_picture_has_it_painted_before_it_starts` keeps
//   saying that a start that works still shows it. **Broken on purpose** (the
//   read moved back above `ui::attach()`, which is the order of 0.151.16): only
//   that one test goes red (`Expected 15 Was 0`), and it is green again with the
//   order of this version. The suite is **176 cases, 161 green and 15 red**, and
//   the 15 are the ones this working tree already has: 14 are red on a clean
//   checkout of `b14ec0e` too, and the fifteenth —
//   `test_every_app_of_the_repository_declares_its_boards`— counts the apps of
//   `apps/` and wants seventeen of them, of which there are six here because the
//   owner's working tree has the rest deleted and not committed. None of the
//   fifteen is about this change.
// 0.151.18 (8/11): **a file that will not open says why**.
//
//   The owner asked why the reader cannot open `la vaca cega`. From the prompt the book opens —`epub.open` answers
//   `true`, `epub.entries()` **8**, and `epub.thumbnail('OEBPS/img/cover.png', 64)` writes
//   `epub/.la-vaca-cega.epub.cache.ics` at `48x64`— and inside the app it opens too, with the page on the glass; what
//   failed was two **writes**, and the core said only `fopen(...) failed`, which is the same line for a file that is
//   not there, a disk with no room and a chip that cannot give the `FILE`.
//
//   `Esp32Fs::open` now says which: `[W][FS] cannot open <path> for <mode>: errno <n>, <u> of 8 open, <bytes> B of
//   heap`, and the refusal of the pool of eight files has a line of its own with the path, which it never had. That
//   line is what answered the question in 0.151.19 (`errno 12` with a pool of one of eight).
//
//   **A wrong answer of this version, kept here because it cost a turn**: I first believed the cause was a missing
//   folder —the image carries no `/data`, since `mklittlefs` drops an empty one— and changed `write_atomic` to make
//   it. The test written to prove it **passed with that code broken on purpose**, because `write_atomic` already
//   called `make_dirs()` before opening the temporary file, exactly as `mkdir` and `open` did. The change and its test
//   are not here: nothing about the folders was wrong. What the code keeps from this version is the diagnostic.
//
// 0.151.19 (8/11): **the board's own RAM keeps a floor, and the app's blocks go to the extra memory when it would lose
// it**.
//
//   This is the answer to the owner's two questions —why `la vaca cega` would not open, and then why `Don quijote` would
//   not either— and it is one bug, not two.
//
//     - **What the board said**, with the diagnostic of 0.151.18 in: `[W][FS] cannot open
//       /epub/.la-vaca-cega.epub.cache.ics for r: errno 12, 1 of 8 open, 1709479 B of heap`. `errno 12` is `ENOMEM`,
//       our pool held **one** file of eight, and the heap it printed was the sum with the PSRAM. The number that
//       mattered was the other one: `mem` says the board's own RAM has **125 464 B free at rest, the biggest piece
//       110 580 B**, and `EspExtraMemory` sent every Lua block under **4 KB** home to that same pool;
//     - **the consequence**, and it was not only the reader: after **any** app had run, `ls /littlefs` printed nothing
//       and every `fopen` on the flash answered `ENOMEM` until the board was rebooted, while the card still listed. The
//       Lua machine of the reader asks for about **120 KB** in small tables and strings, and the file system, the card
//       and the panel need that pool for a `FILE` and for DMA buffers — which is also why a read of the card failed
//       with `sdmmc_read_blocks failed (257)`, `ESP_ERR_INVALID_CRC`: a buffer that was not there;
//     - **the fix.** A block goes to the board's RAM only while what is left after it stays above `kHomeReserve`
//       (**64 KB**, `lib/pal_esp32/src/pal_esp32.cpp`), and to the PSRAM after that; the free bytes are read every
//       two hundred and fifty-six asks and not at every one, because a walk of the heap per block would cost more than
//       the block. A block that asked for the extra memory still tries there first, and a board with no PSRAM falls
//       back home as it always did, so the ATOM is untouched.
//
//   **Measured on the board, after the fix** (Paper, over the serial port):
//
//     - `open /littlefs/epub/la-vaca-cega.epub` → `[I][BOOK] the cache of the book is already at
//       epub/.la-vaca-cega.epub.cache.ics`, `[D][FS] write 54 B in 40 ms`, `reader: La vaca cega, 2 chapter(s),
//       index 2 (ncx), cover yes` and the page on the glass — **no `fopen` refusal at all**;
//     - `ls /littlefs/data/reader` → `reading.txt 54`: **the position of the book is saved again**, which the flash
//       could not do at all before this version;
//     - `open /sd/epub/Don_Quijote_de_la_Mancha-Cervantes_Miguel.epub` → `reader: El ingenioso hidalgo Don Quijote de
//       la Mancha, 56 chapter(s), index 53 (ncx), cover yes` and **no `sdmmc_read_blocks failed`**;
//     - `pio run -e m5-paper`: **2 339 921 B of flash (55,8 %)**, 121 596 B of RAM (2,7 %), no warning.
//
//   **Not measured, and said so**: the free bytes of the board's RAM **while an app is alive**, because the prompt does
//   not answer while an app owns the console. The floor of 64 KB is therefore a reasoned number —the drivers ask in
//   pieces of one and four kilobytes and the floor has to survive the pieces— and not a measured one; the constant is
//   one line to change the day a measurement says otherwise.
//
//   **Still open, and seen while measuring**: the cover of `Don quijote` is named `OEBPS/Images/don quijote-
//   cervantes.png` and cannot be read (`[W][BOOK] the picture … cannot be read`), which is the reader resolving an
//   `href` with a space in it and not this version's business.
// 0.152.0 (8/11): **the floor of the board's RAM is a rule of the PAL —so the emulator obeys it and a test can hold
// it— and a picture that does not come up says which of the seven reasons it was**.
//
//     - **The rule moved out of the chip and into `pal/pal.h`** (`kHomeReserveBytes`, the same 64 KB, and
//       `pal::take_from_the_extra(free_home, bytes, prefer_extra)`), and **the emulator obeys it too**. It did not before:
//       `pal_host` sent every small block to the home it was asked for, which is why a test could pass on the emulator while
//       the board failed — and it is exactly what one of the `/data` theories of 0.151.18 did, with a test that passed
//       over a broken change;
//     - **`test/test_pal` is the proof, and it is a whole suite because the rule is the PAL's**: the home's free bytes
//       never cross the floor while a hundred and twenty-eight small blocks are taken (the shape of what the reader
//       asked for), a board with no extra memory takes everything home —the ATOM must not be refused a block for a floor
//       it cannot jump over— and a block that asked for the extra memory still pays the slow one. **Broken on purpose by
//       taking the rule out of `allocate`** (the emulator goes back to `prefer_extra ? extra : home`): the first two cases
//       go red, and they are green again with it. *Not yet run on the board or in the suite when this was written*: the
//       two runs are the next thing, and if they say otherwise this block is wrong and gets fixed with them;
//     - **a picture says why**: `book::picture_why()` names the reason `load_picture_named` refused —there is no book
//       open, the book carries no entry with that name, the entry is a folder, **the entry is N B and the block is
//       262 144 B**, the stream did not come out, the entry is not a picture this system reads, NxM is bigger than a page
//       of the glass, there is no room for its file or for its pixels, or its scratch does not fit in the block of a
//       chapter— and it reaches three places: the `[W][BOOK]` line, `epub.picture`'s refusal (which the reader now prints)
//       and `epub.thumbnail`'s. **The cover of `Don quijote` that 0.151.19 left open is what this measures next**: the
//       name it printed, `OEBPS/Images/don quijote- cervantes.png`, is a name this system reads byte for byte, so the
//       answer is one of the numbers and not a guess;
//     - **and an app can ask the board's own RAM**, which is why this is a `Y` and not a `Z`: `sys.heap()` carries
//       `ram_free` and `ram_largest` next to the app's own budget and the extra memory. It is the number that was
//       invisible —`mem` prints it, and `mem` is a command of the prompt, which an app cannot read while it runs— and it
//       is what makes the floor checkable from a book, an editor or a test (docs/30, docs/75).
// 0.152.1 (8/11): **the block of a picture is the size of the picture and not a fixed two hundred and fifty-six
// kilobytes**.
//
//   The cover of `Don quijote` did not come up, and the reason the diagnostic of 0.152.0 made it say is the whole bug:
//   `the entry is 1046256 B and the block is 262144 B`. The block was a guess and the PSRAM had three megabytes free.
//   It is the same lesson the table of the zip learned in 0.151.15 —a fixed size that refuses a book for being what it
//   is— and the same shape of fix: `load_picture_named` asks for what the entry needs, in whole kilobytes, the first
//   time that entry is read, and keeps the block for the next picture (a book with a plate of a megabyte and thirty
//   pages does not pay it thirty times). `kPictureFileMax` (**3 MB**) is the most this reader will hold for one picture,
//   and a picture past it is refused with both numbers in the sentence instead of in silence.
//
//   **Measured on the board**, opening `Don_Quijote_de_la_Mancha-Cervantes_Miguel.epub` from the card: the refusal went
//   from `the entry is 1046256 B and the block is 262144 B` to the next wall, which was the scratch of the stream, and
//   then to the last one, which is 0.152.2's. **The cover of that book does not come up yet**, and the reason is no
//   longer a guess.
//
// 0.152.2 (8/11): **the block of a chapter and the pixels of a picture are the size of the thing too, and the scratch of
// a stream grows**.
//
//   The three walls the cover of `Don quijote` found, one after another, and all of them the same lesson as 0.151.15 and
//   0.152.1 —a fixed size that refuses a book for being what it is—:
//
//     - **the pixels of a picture**: `kPicturePixels` (259 200 B, a whole page) was asked for even by a cache of
//       sixty-four pixels, which needs 2 048. The block is now the size the picture is going to have.
//     - **the block of a chapter**: half a megabyte per book, which a novel of three-kilobyte chapters paid in full and
//       which left no room for a cover next to it. The block grows to what the chapter needs and is kept for the next
//       one, and `kChapterBytes` is the most this reader will hold for a chapter.
//     - **the scratch of the compressed stream**: `g.packed` was made at `kChapterBytes` and never grew, so an entry of
//       a megabyte answered `kNoRoom`. It now grows to the entry —**the new block comes before the old one goes**: the
//       first version of this released first and left the book with no scratch at all when the bigger one did not fit,
//       which is a failure that costs more than the picture it was asked for— and a **stored** entry does not ask for one
//       at all, because its bytes are copied straight into the caller's block.
//
//   **Measured on the board, and where it stops**: with the book open and nothing else, the PSRAM has **2 566 863 B**
//   free, and the cover needs about **2,1 MB** —1 046 256 B of file and as much of stream— which **fits** there: the
//   extraction came out whole and the refusal became the last one, `the entry is not a picture this system reads
//   (89504e47 0d0a1a0a)`. That signature is a **PNG**, so the file is fine and what refuses it is `lib/img`'s header
//   reader (`read_chunks` in `lib/img/src/png.cpp`): a critical chunk it does not know, or a run of `IDAT` that another
//   chunk interrupts. **Inside the reader it does not fit yet**: the app holds its canvas, its two faces and the book,
//   and what is left is not the 2,1 MB the picture wants. So the cover of this book is two things away —`lib/img` has to
//   read that PNG and say which chunk stopped it, and a picture of a megabyte wants a stream read in pieces instead of a
//   block of its own size— and neither of them is in this version. The reader opens the book and paints its pages, which
//   is what a reader is for.
//
//   `pio run -e m5-paper`: **2 341 465 B (55,8 %)**, 121 708 B of RAM (2,7 %), no warning.
// 0.152.3 (8/11): **one of the red tests was mine, and it was asking the wrong question**.
//
//   The owner asked whether everything was green and it was not: fifteen of the hundred and seventy-six cases of
//   `test_apps`, and I had been calling them «the working tree's». **One was mine and had been red since 0.151.1**, the
//   version written to stop the emulator writing files under a garbage name: `test_the_options_of_the_emulator_start_empty`
//   asked that every field of `emu::Options` be null, and the constructor of the emulator gives `fs_dir` the word
//   **`"emulator"`** —the folder the emulated disks live in— from the day it was written. A case that asks the wrong
//   question is red whatever the code does, and fifteen reds is what hides one of your own.
//
//   **And the wrong question hid nothing**: checked against the source, the struct has **51 fields**, 43 carry their
//   value in the header and **50 are in the constructor**, and the union covers all of them — there is no field that
//   comes out of the stack. The nine floats of the fake sensors are the board **at rest** and not zero: a g downwards,
//   25 °C, and half the air, which is what an app that asks an IMU nobody configured has to find. The case asserts the
//   value of every field it can now, instead of asking that they all be empty.
//
//   **The other fourteen reds are not this version's** and they are not explained by it: they are red on a clean
//   checkout of the commit before it too, and they are the next piece of work, one by one and with their own
//   measurements.
// 0.152.4 (8/11): **hunting the red tests found three bugs and two cases that asked the wrong question**.
//
//   The count went from fifteen to eight, and what the hunt turned up is worth more than the number:
//
//     - **`E_NONET` was thrown away** (`net.cpp`, `start_next`): `begin` sets it when a request needs a network and the
//       board has none, and the line after it wrote `E_BUSY` over it whenever the driver had no words of its own —which
//       is the normal case—. So `ntp` and `http` without a network answered `no time came back, E_BUSY` and sent a person
//       to look at the radio for a network that was not there. The driver now speaks only when it has something to say;
//     - **an extension was normalised on one side and not the other** (`apps.cpp`, `find_for_extension`): a manifest's
//       `opens` goes through `normalise_extension` (`"TXT"`, `".txt"` and `"txt"` are one thing) and the **query** did
//       not, so a file called `NOTES.TXT` opened nothing. Both go through the same door now;
//     - **the emulator's `use_board` gave the board's name and not its glass**: the binary gives a model the parts it has
//       (`docs/85`) and the helper did not, so a case that ran an app that presents was running it on a board with no
//       screen and the app died inside `ui.present` — `test_ui_focus_puts_the_dial_where_the_app_says` was red for that
//       and nothing else;
//     - and **two cases asked questions that were not theirs**: the badge of `apps info` was written into a folder nobody
//       had made, and the present-with-a-mode case painted the **whole view**, which the system promotes to `gc16` by
//       rule (`docs/50`) — a small square asks the question about the mode and not about the promotion.
//
//   **What is left, counted**: eight, and they are named —`test_every_app_of_the_repository_declares_its_boards` wants
//   seventeen apps in `apps/` and this working tree has six (the owner's uncommitted deletions); the splash-waits-for-the-
//   ink case and the panel-refused one are about the ink of the emulator and are the next to look at; the About, the
//   timeclock, the home app and the reader-with-a-finger ones run a real app and fail after it has painted. Each needs its
//   own measurement and none of them is left as «the working tree's».
// 0.152.5 (8/11): **the last eight red cases are gone and three of them were not the tests' fault**. The count is
// now **175 of 176**, and the one that is left is not a question of code (see the end).
//
//   - **the emulator's hour was read once, at the first boot of the process** (`hal_emulator`, `HostClock`): the board
//     has one clock and so has the emulator, so `emu::Options::now_epoch` was taken by the constructor —built on the
//     first boot of the suite, with the default options— and a case that asked for nine in the morning of a Sunday
//     got the board that never synced (`[I][CLOCK] source none epoch 0`) and the app printed a day with no date.
//     The hour is now read where the hour is read, `epoch_ms()`, and applied when the options change, so an emulator
//     moved by `emulator_advance_clock` keeps the hour it was moved to (docs/85);
//   - **the About page listed 23 of the 25 libraries** (`apps/about/res/libs.txt`, a defect of mine): `epub` and
//     `zip` went into `lib/` for the reader and `tools/gen_about.py` was never run, so the page of every board was
//     missing the two libraries the reader is made of, and the test that reads the tree and the page and compares
//     them was right (docs/45);
//   - **the reader's menu could not open a page with the keys**: the case pressed the arrow twice and the row that
//     opened was page 1. A `ui.list` is **a stop of the dial of its own before its rows** (docs/30, «una llista de
//     sis files té sis parades a més de la pròpia llista»), so from the menu bar the dial needs three arrows: one to
//     fall on the list, one into the first row and one into the second. The case had two and nobody saw it because
//     it stopped earlier, on a line the app has not printed since 0.151.11;
//   - **four cases asked their question at the wrong moment**: the splash and the panel-refused ones wanted a glass
//     that moves ink (`--ink-refuse` implies `--ink` in the binary and they set half of it: with `ink == 0` the
//     emulator keeps no second buffer, so nothing is ever in flight to refuse, and `level_at` —the page drawn— is not
//     `shown_at` —what the glass shows—); the home-app case opened its capture **after** `kernel::boot`, and the
//     first line of an app is printed inside `run_app`; the shelf case asked `mkdir` for `/apps/reader`, which the
//     reader case before it leaves behind, so it was red only when the suite ran whole;
//   - **the reader case looked for a sentence that no longer exists** (`whole glass: the view is 540x960`, gone
//     since 0.151.11 moved the whole glass to the person's choice): it now takes the size of the canvas the app
//     ended with and asks the log for the paint that covers it;
//   - **`LUNOK_TEST=<name>`** runs one case of the native suite (Unity has no filter and the suite is a minute);
//   - and **a probe of mine was still in the kernel**: `lib/kernel/src/button.cpp` opened `/tmp/dbg-keys.txt` on
//     every key event —four blocks of `fopen`/`fprintf` with no `#ifdef`, which on the board is a write to a path
//     that is not there—. Removed, and the case that removed that file goes with it.
//
//   **What is left, counted: one.** `test_every_app_of_the_repository_declares_its_boards` wants seventeen apps in
//   `apps/` and this working tree has six, because 136 files of `apps/` are deleted and not committed (the owner's
//   working tree: `git status` says so). Restoring them, or committing the deletion and lowering the number, is his
//   call and not a line to change behind his back.
// 0.152.6 (8/11): **the sixty apps that were not the paper project's are gone, and the audit of the manifests names
// the ones that ship**. The suite is **176 of 176**.
//
//   - **the app set of this repository is the twenty-four apps of `OLD/PaperOS/data/app`**, ported one by one, and the
//     sixty folders `apps/` had were not from there: they are deleted, and the deletion is this commit's (136 files).
//     Five ports fly today —`about`, `calculator`, `editor`, `reader` (which is `EPUB`) and `timeclock`— plus `probe`,
//     a temporary instrument of the emulator that says in its own description that it is not a screen and is not meant to
//     stay. The nineteen that are left are the queue, and they are named in docs/45 §8 with the ids and the state;
//   - **the guard of that audit was a count** (`seen >= 17`), which said nothing about *which* app was missing and was
//     the number of a set that is being made again —it was red for six apps and would have gone green again for six
//     wrong ones if a folder had been renamed to a duplicate—. It is now the list of ids that have to be in `apps/`,
//     and a folder that is not found and a folder that is there but empty are asked apart. Broken on purpose (the id
//     `clock` added to the list, an app that does not exist yet) it goes red with the message `clock`, and that is the
//     measurement of the change;
//   - and **the documents that name apps of the old list are not rewritten to say nothing**: `docs/40`, `docs/50`,
//     `docs/90` and this one use `clock`, `battery` or `checkbox` as examples, and each name becomes true again when
//     its app is ported. What is not done is inventing the app so the sentence looks right (docs/45 §8).
// 0.152.7 (8/11): **the masters of the icons enter the repository, and the two things the working tree was still
// keeping out are said**. No code changed: this is the tree, and the suite is the same 176 of 176.
//
//   - **`tools/icons/` is in git** (24 files, `2048.png` to `Timeclock.png`, 134 063 B, each one a 448 × 448 grey
//     PNG). They are the drawings a person edits, one for each app of the paper project, and `docs/45` has named them
//     as **the first thing a port reads** since it was written — line 19 of the table— while the folder lived only
//     beside `OLD/`, which is ignored ("leftovers of an older layout, kept next to the repository and never built").
//     The copy here is identical to `OLD/PaperOS/tools/icons/`, measured with `diff -r`, and `tools/make_icon.py` is
//     what turns a master into the `res/icon.png` an app carries (docs/45 §2);
//   - **`.gitignore` says what the tree has**: `OLD/` was already there, and the owner's `DevelNotes.md` joins it —
//     the notes are his and not the project's, and a note that shows up as `??` every time is a note that gets
//     committed by accident one day—. The file ends with a break like the rest of the repository;
//   - and **the title of the reader is his**: `Reader` → `EPUB reader`, which is the port of `EPUB` (docs/45 §8). No
//     case of the suite reads a title, the id is still `reader`, and the manifest keeps what the port owes: `version`
//     0.1.0, `entry`, `boards`, `icon`, `splash`, `opens` and a description of what the app does.
// 0.152.8 (8/11): **`tools/icons/` has twenty-four drawings and not twenty-four apps**, and docs/45 §2 says which
// is which now that the folder is in the repository: the twenty-three that an app of the paper project has, plus
// `Example.png`, which is the drawing to start one from and not an app. `Paint` has none because the original carries
// none —its `manifest.json` has no `icon` and the folder has no `res/`—, so its icon, if a person wants one, is drawn
// and not searched for.
// 0.153.0 (8/11): **the launcher**: an app of Lua that lists the apps of the disk and starts the one a finger presses,
// and the three calls of the system it is made of. The suite is **178 of 178**.
//
//   - **`apps.list()`** (`luabind`, `kernel/apps_ops.cpp`): the door of the apps handed to Lua as a table per app —
//     `{id, title, icon, launcher, card, works_here}`—, with the icon already a path the disk answers
//     (`/apps/<id>/res/icon.png`). The launcher does not walk `/apps` or parse a manifest: the door already merges the
//     two disks, refuses a folder that is not an app and says which board it needs, and a second reader of the same
//     file is the one that goes wrong the day the format changes (docs/30, docs/40);
//   - **`app.run(id[, back])`**: the wish to start another app, carried out **after the turn**. It cannot be a switch
//     inside the call —the app that asks is the one the interpreter belongs to, so closing it there would destroy the
//     state the call is running in—; the shell takes the name in `poll_app`, closes the app in front with its goodbye
//     hooks and starts the one asked for. **`back` is the way back** and is true unless the app says otherwise: the app
//     that asked is started again when the one it opened ends **or when three keys close it**, which is the only way
//     back on a board whose only way out of an app is a key (docs/30, docs/90);
//   - **an app is in front from its first line** (`shell.cpp`, `run_app`): `g_app_alive` is set before the first turn
//     of the entry and not after it, because a launcher that asks for another app in its own first lines was answered
//     «no app is in front» by the system that was running it. A window of an alarm still is not in front, so a window
//     cannot ask for an app;
//   - **`canvas:image(x, y, path[, w[, h[, mode]]])`**: a picture inside a control and not on the page, which is what a
//     cell of a grid needs. It is the same primitive as `ui.image` —`ui::image_on_canvas` with the page as its canvas
//     for the one and the control's slot for the other— so a picture cannot be drawn one way on the page and another in
//     a control (docs/30);
//   - **`"launcher": true` in a manifest** (`kernel::apps::Entry::launcher`, `json::get_bool` and `json::has_key`): what
//     keeps a launcher out of another launcher's grid. It is the app's own declaration and the system does not guess it
//     from a title or an id, and a value that is not `true` or `false` is left out with a line instead of being read as
//     one (docs/40);
//   - **the app `home_eink`** (`apps/home_eink`, title *Home*): the grid of `ui.icon_grid` with a cell per app —the
//     picture in a canvas, the name under it—, **sorted by name** because the order of a directory is the order of the
//     disk and a page whose cells move between two boots is a page a person cannot learn, and **the ring on the first
//     cell** because a grid is a stop of the dial before its cells are and the first press of the middle key has to do
//     something. An app without a picture, an app of another board and the launchers are said in the console, one line
//     each, which is also what a test reads (docs/45 §8, docs/85);
//   - **its icon is drawn for it**: `tools/icons/Home.png`, 448 × 448 grey, nine rounded squares with the ink and the
//     stroke of the other drawings of the folder, and `res/icon.png` is the 94 × 94 that `tools/make_icon.py` makes of
//     it;
//   - and **two cases**, one that runs the launcher of the repository on a disk with two apps, another launcher and an
//     app of another board —and presses the centre, sees the app start, end, and the launcher come back— and one that
//     pins the three reasons `app.run` refuses with. Broken on purpose: with the way back not remembered the first goes
//     red with `Expected 'home_eink' Was ''`, and with `app.run` refusing everything it goes red with
//     `home: alpha cannot start`.
// 0.153.1 (8/11): **the launcher's page is the whole glass and not the box of its grid**. Found by running the app on the
// disk of the board and looking at the picture it left: the grid is a control and a control paints inside its own frame,
// so the air around it kept the picture of the boot —the launcher went up as `paint 20,8 500x884`, a box of white with
// the drawing of the start around it—. `ui.clear(15)` before building the page is what docs/40 asks of every app that is
// a page and not a box, and with it the first present is `paint 0,0 540x912`, which the system promotes to a `gc16`
// (docs/50). The case that runs the launcher now asks for that whole paint, and broken on purpose —the wash out— it goes
// red with the box in the capture.
// 0.153.2 (8/11): **the editor has its badge**, which the launcher showed as an empty cell. `apps/editor/res/` had the
// notes of the app and no picture, and its manifest named none —the port of `Editor` forgot the one file `docs/45 §2` asks
// for—, so the grid of `home_eink` had a cell with a name and no icon and said so in the console (`home: editor has no
// picture`). The master `tools/icons/Editor.png` was in the repository all along: it is copied to `res/icon-source.png`
// and `tools/make_icon.py` makes the 94 × 94 `res/icon.png` the manifest now names (docs/45). Found on the board, running
// the launcher and reading its own log over the serial port.
// 0.154.0 (8/11): **the page arrives before its pictures**. `apps.list()` and `apps.info(id)` are two calls and not one,
// and the launcher is a queue.
//
//   - **`apps.list()` is the names of the door and opens no manifest** (`kernel/apps_ops.cpp`, a walk of `/apps` through
//     the VFS, which is the door's own listing): what a launcher can draw at once, and the only thing that is true before
//     a single file is read. **`apps.info(id)`** is that manifest, one app at a time, with the reason in the system's
//     words when it cannot be read;
//   - **why**: one page that waited for every manifest and every picture arrived late —the board spent seconds before the
//     first pixel— and one app whose picture could not be read held up the ones behind it. Now the grid is up with a
//     **generic picture** —a box with the first letter of the name, drawn by the app and not shipped as a file, so a cell
//     whose picture fails keeps the letter and not a hole— and the queue fills in the real name and the real picture one
//     cell at a time, measured on the board and on the emulator: `paint 0,0 540x912` (the whole page) and then one
//     `158x96` per cell as each manifest lands;
//   - the queue takes **one app per turn** and the page is only laid out again when an app has to leave it (a launcher of
//     its own, an app of another board), which is once and at the end; an app whose manifest cannot be read keeps its name
//     and its letter and says so;
//   - **the order of the page is the order of the names**, decided once before the first cell: the titles arrive one at a
//     time and a grid that reorders itself when one lands is a grid whose cells jump under the finger;
//   - and the case that runs the launcher now measures the order of the evidence —the names, then the whole page, then
//     the first `home: app` line— which is the promise of the two calls. 178 of 178 green.
// 0.154.1 (8/11): **the launcher turns its own pages, and the band under the grid is what turns them**. A grid of the
// system keeps the cells it cannot fit out of sight —the page follows the cell the dial is on, and this page keeps the
// ring on the cells themselves—, so a disk with more apps than fit showed the first page of them and lost the rest.
//
//   - **the page is the launcher's**: only the cells of the page that is on the glass reach the grid, and how many fit is
//     the grid's own arithmetic read once —the box of a cell, and the twenty-eight pixels a grid taller than a hundred
//     and sixty keeps at its foot— and never counted by the eye (docs/90);
//   - **the foot of the page is the band of the pages**: an arrow at each bottom corner and the page it is between them
//     (`1 / 1` when everything fits, with both arrows `disabled` —drawn, and answered by neither the dial nor a finger,
//     which is what the system's own state means—), and a press is a wish the loop takes, because a control cannot give
//     itself back inside its own callback;
//   - **the build says the page** (`home: page 2 / 3 (6 cells)`): it is the one place a resize, an app that left the page
//     and an arrow all end on, so the console says which page is on the glass without anybody counting cells;
//   - measured: on the Paper's page seven apps fit in one page and the band reads `1 / 1`; in a page of 420 by 360 the
//     eight apps of the case are two pages, a finger on the right arrow says `home: page 2 / 2` and one on the left says
//     `1 / 2` again;
//   - and **a view inside a cell of a grid survives the page**: `test_a_canvas_inside_a_cell_of_a_grid_survives_the_page`
//     paints twice and asks all six pictures whether they are still there, which is what a launcher of badges needs and
//     what no test said before. 179 of 179 green.
// 0.155.0 (8/11): **the emulator comes up in a window and in the launcher**, which is the owner's ask —*«que l'emulador
// arrenqui amb el paràmetre --gui i l'app per defecte home_eink»*— and two things of `platformio.ini` plus the one it
// needed in the emulator.
//
//   - **`LUNOK_GUI_BY_DEFAULT`** makes a run open the glass in a window without being asked, and **`--no-gui`** is the
//     new flag that takes it away. **A demand and a default are not the same thing**: `--gui` asked for by hand still
//     refuses `--no-delay` and `--no-ink` and still refuses a board with no glass and no key, and the default —which
//     nobody asked for— steps aside with a line and the run goes on. Without that, every script of this project that
//     says `--no-delay` would have stopped working the day the window became the default;
//   - `LUNOK_HOME_APP` is `home_eink` in the environment of the emulator and the disk of that build carries it
//     (`custom_apps = home_eink`), because an app that is not on the disk the emulator reads is a boot that lands on the
//     prompt. **No test is touched**: the suite has its own environment, its own disk and its home app empty;
//   - measured on this machine: `run` without arguments says `gui: the page 540x960 is in a window at half, one pixel of
//     the window is 2 of the page` and `gui: the 3 keys of the board are a column on the right of the page`, and then
//     `home: page 1 / 1 (2 cells)` (the two apps of the card of this emulator, which is a directory that exists);
//     `--no-delay` says `gui: no window, --no-delay is an emulator without the times of the hardware` and runs; and
//     `--gui --no-delay` is refused with the sentence it always had. 179 of 179 green.
// 0.156.0 (8/11): **the eighteen red cases of the suite are seventeen repairs and one case that was wrong**, and
// three of them were not the tests' fault. The count is exact and every one of them was measured with `pio test
// -e test`: **538 of 538** green today, against 518 cases with eighteen red and two suites that never finished.
//
//   - **the bytes of `0x80` to `0x9F` are the characters of Windows-1252** (`lib/ttf`): the frame has no picture in
//     Latin-1 and the 1252 puts the quotes, the dashes, the ellipsis and the bullet there, which is how a page of a
//     book is written. The tokenizer already turned `&#150;` into one byte and the face answered «no such glyph», so
//     the dash vanished from the page (docs/30, docs/46 A2);
//   - **the last 256 bytes of a Lua ceiling are not the app's** (`luabind::kErrorReserve`): a state whose ceiling is
//     exactly full cannot write the sentence `not enough memory` and **Lua aborts the process** instead of raising at
//     the app. On the emulator that was the SIGABRT of `test_luabind`; on the board it would be the kernel restarting in
//     the middle of an app that asked for too much (docs/75);
//   - **the task of a chunk is made inside a protected call** (`luabind::make_the_task`): `lua_newthread` outside a
//     `lua_pcall` is the same abort with no message at all, and it was the case of a chunk over the ceiling that
//     never reported (docs/75);
//   - **the cases that were the tests' fault and not the code's**: the runs of a text kept an uninitialised count, so
//     a case passed or failed by the luck of the stack; a page of a book was written with the places of the OS/2
//     header inside a header that says forty bytes, so its picture was refused; the layouts of `test_layout` asked
//     for a page of step one and got the reader's four; `café` counted five bytes when the reader writes four; the
//     samples of the netpbm family lived in an app that the purge of 0.152.6 took away; the three taps of the glass
//     inherited the finger of the case before them, and the page of that emulator is 960 high because it is turned;
//   - what the cases said of the two homes of memory was written before the floor of 64 KB (0.151.20): the case of
//     the ceilings now says that the floor decides **the order and not the refusal** (docs/75).
// 0.156.1 (8/11): **the page is a ring, and a box is not a stop**. The owner, with the launcher in his hand: *«el focus
// ha de saber quin és el primer i l'últim control de la vista per tornar a començar des del control de l'altre punta
// (moviment circular). Sembla que et pots sortir per l'esquerra i per la dreta amb el focus»*.
//
//   - **the phantom stop was the grid**: `ui.icon_grid` is `focusable` by default, which is right for a grid of its own
//     items —there the cells are the control's and `index` walks them— and wrong for a grid that holds the app's cells,
//     where the stops are the cells. The ring landed on the whole box of the grid, the control paints no ring for that,
//     and the walk then went on in the order of the tree: a person reads that as the focus leaving the page. Measured
//     on the emulator with the launcher: eleven stops for ten cells, and the eleventh was `icon grid 500x818 at 20,8`;
//   - **the ends of the ring are the ends of the page** and the walk wraps (`(current + direction + count) % count`),
//     which it already did —what it lacked was a list without phantoms in it—, so no key leaves the page at either end;
//   - the case `test_the_dial_walks_the_controls_of_the_page_in_a_ring` walks three cells and two buttons with the keys
//     and says the order, the wrap in both directions and that the grid is never a stop; with the phantom put back it
//     is red (measured). 539 of 539 green.
// 0.157.0 (8/11): **the pages of a grid are the grid's, and the launcher has no paginator of its own**. The owner, with
// the launcher in his hand: *«El Grid de l'app Home te paginador Dots intern o un grid pagina amb un paginador extern? Has
// creat un paginador manualment? ETS IDIOTA O QUE ET PASSA? usa sempre els UIControls que hi hagin abans de fer-ne un de
// nou»*.
//
//   - **the walk reaches the cells of every page of a grid** (`ui_walk_targets`): a grid keeps the page of the cell its
//     `index` says, and the cells that are on the other pages are placed out of its frame, so a walk that ignored them
//     could never leave the first page —a launcher with more apps than fit had no way of reaching the rest—. Now they
//     are stops: the cell the dial lands on is placed by the grid when the app moves `index`, which is what the
//     `on_focus` of a cell is for (docs/30, docs/90);
//   - **`apps/home_eink` lost its paginator**: two `ui.button`s and a `ui.label` under the grid were a control the system
//     already has. The launcher hands the grid every cell and tells it which one the dial is on; the grid paints its own
//     footer `N / M` and pages itself. The file went from 430 lines to 334 and the ring of the page is now the cells
//     alone (docs/30);
//   - the case `test_the_pages_of_a_grid_follow_the_dial` walks five cells in a box of two and says that the third and
//     the fifth are reached —the second and the third page— and that the grid is laid out again for each of them; with
//     the off-page cells out of the walk it is red (measured). 540 of 540 green.
// 0.158.0 (8/11): **the keys of the calculator wear the signs of the original**, and the road that was missing
// between the two. The owner asked how `pos_launch.lua` of PaperOS paints its backspace, its sign, its divide
// and its times, and the answer is two lines of it plus one decision of the system:
//
//   - **the original writes the glyph and not the operation**: `Rows` is `{"C"}, {"←", "back"}, {"±", "neg"},
//     {"÷", "/"}` and `{"×", "*"}` in the second row, and its comment says where those glyphs come from —
//     `tools/fontgen.py` of PaperOS cooks `0x2190` to `0x2193` with the letters, 216 code points of a face —;
//   - **because its text is UTF-8 and its faces are indexed by code point** (`src/gfx/text.cpp` decodes the
//     string before it looks a glyph up), a `"←"` of `U+2190` is three bytes of the script and one glyph. **A
//     text here is one byte a character** (docs/30), so a byte above `U+00FF` does not exist and three of the
//     four signs are the only ones that fit: `÷` is `0xF7`, `×` is `0xD7` and `±` is `0xB1`. The arrow gets
//     `0x81`, one of the **five codes Windows-1252 leaves empty** —no file can ever mean them—, with `→` at
//     `0x8D`, `↑` at `0x8F`, `↓` at `0x90` and `⌫` at `0x9D` (docs/30, docs/92 §34);
//   - **the system face carried none of them**: `dejavusans-bold.ttf` was the printable ASCII, so the key would
//     have painted a hole. It is made now with `python3 tools/ttf_latin1.py --set signs` —a third set of the
//     tool: the pretty ASCII plus `±`, `×`, `÷`, the four arrows and the backspace—, **104 glyphs and 9 416 B**
//     in place of 8 548 (**868 B** more of disk, and of RAM while the face is loaded), 78 points and 5 contours
//     in the biggest glyph, inside the reader's 256 and 64. The letters with an accent are still not in it:
//     an app that wants those loads `/fonts/dejavusans-latin1.ttf` (docs/30, docs/75);
//   - **`apps/calculator` is the original, key for key**: the four signs in the keypad and `Symbols` with `×`
//     and `÷` in the line of the display, written with `string.char` and never as the letter in the file, which
//     is UTF-8 and would paint two marks where the key has one. The drawing was already the same call for call
//     —`rect` grey 12 on the operators, `frame` on every key, the label centred in `title`—, and what still
//     differs is the size: the four faces of the paper project were bitmaps baked at 33, 43, 57 and 82 px of
//     line and this face steps **eleven** pixels, so the same four asked in pixels come out **33, 44, 55 and
//     77** (docs/92 §34);
//   - **the three keys that are not a figure have a grey of their own** (asked for by the owner the same day):
//     `C`, the arrow of the backspace and `=` are filled with **grey 8** and wear **white letters**, and the four
//     operations keep the **light grey 12** of the original, so the page reads «these three change the state and
//     those four are the arithmetic». The two levels are `kOperatorGrey` and `kActionGrey` at the top of the page,
//     one place each (`key_grey`), and the white letter comes out of the same test (docs/30, docs/92 §34);
//   - **the emulator carries every app** (asked for by the owner: «afegeix totes les apps a l'emulador»): the disk of
//     `[env:emulator]` is `custom_apps_in_disk = yes` and, as the emulator has no board, the filter of `make_disk`
//     keeps every folder of `apps/` —**7 apps, 35 files, 295 793 B** measured on this machine: about, calculator,
//     editor, home_eink, probe, reader and timeclock—, so the launcher shows seven cells instead of one and an app
//     added tomorrow is on the emulator without touching a line of configuration. They go in as **links**, so an edit
//     is what the next run reads; the boards keep a short list, which is what their 4 MB of LittleFS allow
//     (docs/70, docs/85);
//   - **every line of a paint says the quality by its name** (asked for by the owner): `mode 3 gl16` and not
//     `mode 3`, because a number has to be looked up in a header and the question «which quality painted this
//     rectangle?» is the one the log is read for. `hal::mode_name` is the only table of the eight words
//     (`lib/hal/include/hal/hal.h`) and the panel, the UI, the refusal of a page and the emulator all read it —
//     the emulator had its own copy, which is a second truth— (docs/50, docs/85);
//   - **`exit`, `logout` and Ctrl+D close the session of the console, and what closing means is the console's
//     business** (asked for by the owner): they are one verb with two names plus the byte a terminal ends its input
//     with, and they ask `IConsole::virtual_terminal` —a policy the emulator turns on from `main_host` with
//     `pal::set_virtual_terminal(true)`— because the two consoles of this system are not the same thing:
//     - **a serial port** is the console of a board **and it cannot be hung up**: nobody is at the other end to
//       close the wire and it is the only way into a system with no glass. There **the session is not closed at
//       all** — the board answers `logout: the session is closed; press Enter to open a new one`, throws the half
//       line away, stops taking lines (a letter is not a line and an arrow is not a key) and **the Enter of the
//       terminal opens a new one**, `login: a new session`, which is what a getty does;
//     - **a virtual terminal** —the console of the emulator, a program on a PC that somebody started— **really
//       closes**: the line is written, the port is flushed and the run stops, which is what `poweroff` does on the
//       emulator too;
//     - inside `lua`, Ctrl+D keeps its meaning: it leaves the interpreter. The case
//       `test_the_session_of_the_console_closes_and_opens_again` says all of it, the two consoles one after the
//       other, and boots the emulator again so the cases after it find it running (test_shell, docs/30, docs/50,
//       docs/85);
//   - **and the size of a disk, corrected**: `docs/70`, `docs/80` and the comment of `platformio.ini` said that an
//     image with all of `apps/` was *148 files and 418 720 B* and that the tool left two of them out. `apps/` is
//     **35 files and 295 793 B** and the system alone **16 files and 130 217 B** (measured on 8/11), so that figure
//     is withdrawn —it is not this tree's— and what fits is what `check_it_fits` prints on every build, not a
//     number written in a document (docs/70, docs/80);
//   - the case of the calculator **loads the bold face of the disk for itself** (the suite's emulator carries
//     none), asserts the four signs have a width on it, presses them by the byte they carry —`0xF7`, `0xD7`,
//     `0xB1` and `0x81`— and reads the display as the original writes it, `Calculator: 8 ÷ 0 = Error` and
//     `Calculator: 6 × 7 = 42`.
// 0.159.0 (8/11): **the shell is the system's and not the wire's**. The owner asked for the shell to be usable from
// anywhere in the system —an app asks and gets a session— with a handle whose three pipes are the shell's stdin, its
// stdout and its stderr:
//
//   - **a session is a shell somebody asked for** (`kernel/shell.h`): `open_session` gives a handle and
//     `session_write` are the lines, `session_read` the answers and `session_read_error` the complaints, and the
//     lines go to the **same `execute`** the console uses —one shell, two doors— with no console in the middle, no
//     prompt and no echo, because a pipe is not a terminal (docs/30);
//   - **the redirection is one number**: every word of this file already went through `out_bytes`, so `g_sink` says
//     where it goes —zero is the console and anything else is a session— and `error_out`/`usage_out` say that the
//     line is the complaint, not the answer, for the length of one call. One place, and the prompt and the echo are
//     asked for in `prompt()`, which stays quiet for a session (docs/30, docs/50);
//   - **an app asks for it with `sys.shell()`** (docs/30, «Una sessió del shell»): a handle with `stdin`, `stdout`
//     and `stderr` —each a closure over the id—, `close`, `dropped` and `id`, and the metatable of the handle in
//     `luabind`. The six calls travel as a table of function pointers (`luabind::ShellOps`, installed by the boot
//     with `shell::install_shell_ops`), because `luabind` never includes the kernel (docs/20);
//   - **what it costs is asked and given back**: 1 024 B of answers and 256 B of complaints from the heap while a
//     session lives and nothing when it closes, plus a line of 128 B; **one session at a time**, and the second
//     answers `E_BUSY` with the reason instead of a queue nobody drains. A byte that does not fit its ring is
//     dropped and counted (`sh:dropped()`), because an app that stops reading must not stop the shell (docs/75);
//   - **`lua` is refused from a session** —the interpreter is the console's prompt and paints a view of its own— and
//     `run` is already refused while an app is in front, which is the case of whoever has a session;
//   - the case `test_a_session_of_the_shell_is_a_shell_with_three_pipes` (test_shell) runs a command and reads its
//     answer, runs a word that is not a command and reads it on the **error** pipe, says `no` to a second session,
//     closes the first with `exit`, and does the same again **from Lua** with `sys.shell()`, so the call of an app
//     and the call of the kernel are one road.
// 0.160.0 (8/11): **`&` sends an app to the background without a second task**. The owner asked for `run calculator &`:
// the app starts, the prompt comes back at once, and its stdin, stdout and stderr are detached from the console that
// started it.
//
//   - **the `&` is read where the line is cut** (`execute`): it is taken off the end, and it is **only an app** that can
//     go there —every other command of this shell is over before a person could type the next one— so a `&` on one of
//     them says `only an app goes to the background: `run <id> &`` and does not run it in silence (docs/30);
//   - **nothing is threaded**: `g_app_detached` is the whole of it. The app is polled by the same turn of the kernel as
//     any other, one thing at a time, and what changes is who owns the console: `feed` gives the lines to the shell
//     instead of to the app (`g_app_alive && !g_app_detached`) and the prompt comes back for the shell
//     (`!g_app_alive || g_app_detached`), which is what the person typed `&` for (docs/30, docs/50);
//   - **a detached app's `print` goes to the log**: `luabind::set_console_attached(false)` sends `con_write` to the sink
//     the kernel installs (`console_away`), which is one line of the log each and cut to what a line holds. Two writers
//     on one prompt is the mess `&` exists to avoid, and an app that prints into the log is not an app that is silent
//     (docs/30, docs/50);
//   - **it ends the way it started**: a detached app that ends says `[<id> ended in the background]`, `close_app` and the
//     end of the turn give the console back (`set_console_attached(true)`), and an app that asks for another one from
//     the background keeps it detached —the continuation of the same wish and not a new page in front of a prompt—;
//   - the case `test_an_ampersand_runs_an_app_in_the_background` (test_shell) writes an app that prints and waits, runs
//     it with `&`, and measures the three things that matter: the app is alive, **its line is not on the wire** (it is
//     the log's), and a line typed after it runs **in the shell** (it answers `version` and the prompt comes back); and
//     that a `&` on something that is not an app is said and not obeyed.
// 0.161.0 (8/11): **a strip of tabs whose tabs can be closed**. The owner asked for a terminal in the glass whose tabs
// carry an `X`, and the strip had no such thing: a tab was a word and a finger could not press anything on it.
//
//   - **the cross is the strip's arithmetic and not the app's** (`tabs_draw_closeable`, `tabs_close_at`, `tabs_tab_at`
//     of `kernel/ui.h`): `tab_cross_side` says how big the cross is —a third of the strip's height at most, a sixth of
//     the tab at most— the paint puts it on the right of the tab and centred, and the two hits answer with the same
//     box, so **what a finger presses is what the glass painted**. The cross a finger presses is at least
//     `kTabCrossHit` (28 px) on a side, because a finger is not a mouse, and the word of the tab is measured against
//     what the cross leaves so a long name is cut there instead of painted under it (docs/30, docs/90);
//   - **`tabs_draw` did not change**: the strip with crosses is a call of its own, so every page that draws a strip of
//     words draws exactly what it drew before (docs/30);
//   - **`tabs:tap(x, y)` is the app's one call** (docs/30, «`ui.tabs`»): it takes the point of a finger, and the strip
//     decides —a cross closes its tab and the app hears **`on_close(self, index)`**, a word chooses its tab and the app
//     hears the `on_change` the dial already calls—. It answers true when the strip did something with the finger. It
//     is one call because an app that worked out the columns would be a second arithmetic that presses a tab that is
//     not there the day the strip changes;
//   - **and the app is the one that frees**: the strip says *which* tab, and giving back what is behind it —a session
//     of the shell, a file, a page— is the app's, because a strip does not know what a tab holds (docs/30).
// 0.162.0 (8/11): **the Shell app, the system's own shell in the glass with tabs**. The owner asked for a Lua page with
// a contextual menu, one tab a session, a cross on every tab and a terminal of black with white letters.
//
//   - **`apps/shell` is a client of the shell and not a second shell** (docs/30, «L'app `shell`»): every tab is a
//     `sys.shell()` session, what a person types on the last line goes to `stdin`, and what the session answers is
//     appended from `stdout` and `stderr`. The app never reads a line of its own, which is the whole reason the session
//     API of 0.159.0 exists;
//   - **the page is a terminal and it says so**: `ui.menu_bar` on top —**File**, with **New** opening a session and a
//     tab and **Close** giving away the one in front—, `ui.tabs` with `closeable = true` below it, and a
//     `ui.textbox_multiline` for the rest with `color = 15` and `background = 0`. The black page with white letters is
//     a decision of the page and not a theme of the system (docs/30, docs/90);
//   - **the box is read-only wherever the caret is not on the line of the prompt**, and that rule lives in the app
//     because the multiline has **no caret-moved hook**: it reads `box.cursor` at every turn and sets `readonly` (and
//     `keyboard`) from it. A page that is read is not a page a finger writes on, and the line of the prompt is
//     (docs/30);
//   - **the cross keeps its promise**: `on_close` closes the session before the tab goes away. One session at a time is
//     the kernel's rule (`kMaxSessions`), so a tab that left its session open would also leave the page without a
//     second tab (docs/30, docs/75);
//   - **the Paper carries it** (its list goes from six apps to seven, **34 files and 278 127 B**) and the emulator carries
//     every app of `apps/` as before (**8 folders, 39 files, 305 615 B**), all of it measured on 8/11; **the image is
//     what `make_disk` wrote that day and not the sum of those two numbers** —*`build/disk/m5-paper is 410534 B in 50
//     files, 7 apps`* and *`build/disk/emulator is 438116 B in 56 files, 8 apps`*— and the figures of docs/70, docs/80,
//     docs/85 and `platformio.ini` are those and no longer the ones of the seven-app tree (docs/70);
//   - **the icon is the old project's style and not a black square**: the owner asked for it (*«aquesta icona es massa
//     negre, poc amigable»*). The masters of `OLD/PaperOS/tools/icons/` are 448 × 448 and use **four levels and only
//     four** —white 255, light grey 221, mid grey 170 and ink 68— with a thick rounded outline, so the window here is
//     a **light grey screen with an ink outline**, a mid grey title bar with three dots, an ink prompt and one line of
//     grey answer; `tools/make_icon.py` takes it down to 94 × 94 like every other icon of `apps/` (docs/40);
//   - **what the compiler of 8/11 caught, and which is fixed here**: the three setters of `luabind` (`set_shell_ops`,
//     `set_console_attached`, `set_console_away`) had been written **inside the file's anonymous namespace**, so they
//     were three functions with internal linkage that the kernel could not link (`defined but not used` under
//     `-Werror`), and `session_write` compared `session->line_used + 1` with `sizeof` across two signednesses. The
//     setters sit outside the anonymous namespace now, and both environments build: `emulator` in 3 s and `m5-paper`
//     in 87 s on 8/11, warning-free (docs/80);
//   - the case `test_the_shell_app_is_a_page_with_a_session_of_the_shell` (test_apps) runs the app that ships on a
//     board with a glass and a panel, and measures the half of it that is the kernel's: the app in front is `shell`, it
//     **opened exactly one session** (id 1 — the ids are one based and zero is the console), and the session answers
//     `version` with `kernel::kVersion`; `exit` closes it, so the page did not keep it for nobody.
// 0.163.0 (8/11): **the pinch from big to small closes**. The owner asked for it as the gesture of leaving: a page
// with one view goes out of the app, a page with several goes back one at a time, and a launcher cannot be closed.
//
//   - **the gesture is the system's and not the app's** (`touch::take_pinch_in`, `kPinchClosePx = 60` in the shell):
//     it is taken out of the queue before the app reads it, the way the three taps are, because a gesture every app
//     implements is a gesture one of them will not. **Only the ones that come from big to small**: a pinch that opens
//     the fingers, or that comes back by less than sixty pixels —the step the paper project gave its pinches— stays in
//     the queue and the app reads it with `touch.gesture()` as before (docs/30, docs/46);
//   - **the views of an app are a number of the kernel** (`apps::views/view_push/view_pop/reset_views`): zero means the
//     app is on its only page, and an app that shows a detail over the page it came from says `app.view_push()` and
//     gives it back with `app.view_pop()`. The counter lives there because a pinch means the same wish everywhere and
//     an app that kept the number itself is an app that can get it wrong; it starts at zero with every app and it goes
//     with it when it leaves (docs/30);
//   - **three wishes in `poll_app`**: a view on top is given back and the app hears **`on_back(depth)`**
//     (`luabind::call_back`, a hook between turns like `on_resize`) so it draws the page below it again; with no views
//     the app is closed and the page that asked for it comes back the way it does for the three taps; and **a launcher
//     is refused with a line**, because `"launcher": true` is the app's own declaration (`apps::read`) and a board with
//     no page at all is a board a person reaches with a cable (docs/30, docs/40);
//   - **an app uses it**: `about` pushes its detail as a view and its `on_back` builds the cover again, so the gesture
//     has one page that proves it. `home_eink` already says `"launcher": true`, so it is the first page a pinch does
//     not close (docs/40);
//   - the case `test_a_pinch_from_big_to_small_goes_back_and_then_closes` (test_apps) makes the gesture with two fingers
//     of the emulator —eighty pixels either side of the middle, four steps of fifteen coming back— and measures the three
//     wishes: the counter goes 1 → 0 and the app hears `on_back(0)` and stays, the next pinch closes it, and a launcher
//     with the same gesture is still there with its line in the log.
// 0.163.1 (8/11): **the ways out go back one view before they close**. The owner saw the log of the middle key
// —*«button: three taps, closing the app / app closed»*— and asked for the same as the pinch.
//
//   - **`shell::go_back_one_view` is one function and three callers**: the pinch from big to small, the three taps of
//     the middle key (`button.cpp`) and the three taps on the glass (`luabind::exit_requested`) all ask it first. A
//     page with views on top gives the last one back and hears `on_back(depth)`, and only the wish that finds no view
//     closes the app. **«Go back» has one meaning**, and three places that each decide it is three places that can
//     disagree (docs/30);
//   - **the wish is spent on the view it took** (`luabind::clear_exit_request`): without it the next turn would find
//     the same exit request and close the app anyway, which is the bug this version would have had (docs/30);
//   - **the launcher stays the exception of the pinch and not of the keys**: three taps of the middle key still close
//     a launcher, because that gesture is the escape hatch of a page that stopped listening —and on the launcher it
//     does not even get there, since a grid of cells is focusable and the middle key is Enter (docs/30);
//   - the case of the pinch grows: the app with a view pushed is started again and **three taps of the middle key**
//     take the view (the counter goes 1 → 0 and the app stays, with the line `button: three taps, back one view`), and
//     the three after it close it with `button: three taps, closing the app`.
// 0.163.2 (8/11): **the shell app found three bugs in the new pieces, and they are fixed here**. The owner ran it and
// got `attempt to index a boolean value` in `ui.present`, then the pipes refused their own colon.
//
//   - **`closeable` was read after the fields table was popped** (`l_tabs_draw`): the painter asked the stack for the
//     field once the table under it was gone, so the value it indexed was whatever was underneath —a boolean, in the
//     shell app— and every page with a strip that can be closed died on its **first present**. It is read before the
//     pops now (docs/30);
//   - **and `closeable` was a field the painter read and the strip did not accept** (`is_tabs_field`): the assignment
//     fell through to the raw set, so a wrong value was a cross that did not appear instead of a line. It is a field
//     of the strip now, true or false, and writing it marks the strip for a new paint (docs/30, docs/46);
//   - **the three pipes take the colon the docs teach**: `stdin:write`, `stdout:read` and `stderr:read` are closures
//     over the id of their session, and they looked for their argument at index one —so `s.stdout:read()`, which is
//     how docs/30 and the app write it, passed the pipe table and answered `calling 'read' on bad self (number
//     expected, got table)`—. A table in front is stepped over, and both `s.stdout:read()` and `s.stdout.read()` work;
//   - **measured on the emulator, 8/11**: the shell app comes up (`paint 12,12 516x888 gc16`) and runs twenty seconds with
//     **no line of `lua:`**, and a probe app that writes `version\n` into its own session reads the answer back
//     (`lunokOS 0.163.1  board m5stack-paper (emulator)`), so the pipes are a round trip and not a wish. Typing into the
//     app's box was not exercised: the emulator had no finger and no key in that run;
//   - the case `test_a_strip_that_closes_its_tabs_draws_its_crosses` (test_apps) is the one that would have caught the
//     first of them: a page with two tabs and `closeable = true` is presented, and the log has to say `present true`.
// 0.164.0 (8/11): **a redirected app is an id with an asterisk and a name with the word of its unit**. The owner
// found the launcher asking for the picture of an app of the card and getting nothing, and named the rule: `id: *shell`
// with `name: Shell-SD` is `/sd/apps/shell`, and `id: *editor` with `name: Editor-Littlefs` is `/littlefs/apps/editor`.
//
//   - **the mark is on the id and the unit is on the name** (`vfs.cpp`'s `kRedirect`, `apps.cpp`'s `kRedirectMark` and
//     `unit_word`): the id is a **path** —`/apps/*editor/res/icon.png` has to exist or the launcher cannot paint that
//     cell— and the name is what **a person reads** in the grid and in the bar. The `-SD` used to be on the id, and then
//     the picture of the duplicated app was the path of a folder no disk has: `editor-SD has no picture`, and the cell
//     fell back to the letter of the placeholder (found on the emulator on 8/11, docs/40);
//   - **the same code that presents the duplicate does both**: the `/apps` listing of the card (`CardSink`) writes
//     `*<folder>` instead of `<folder>-SD`, and `apps::load` appends `-<unit>` to the title of a marked id —`SD` for the
//     card and `Littlefs` for the flash— so a board whose default were the card would name the flash copy the same way
//     (docs/40, docs/70);
//   - **the mark resolves by the same question the listing asked**: `*<folder>` is answered by the disk that does not
//     carry `<folder>` as a plain app —the card when the flash has that folder, the flash when it does not— so the name
//     and the path cannot disagree, and the old `-SD` stripping is gone (one rule and not two);
//   - **the shell says the new name** where it told a person to run `-SD` after an install: it says `run *<id>`;
//   - **measured on the emulator, 8/11**: with a card carrying a copy of `editor`, the launcher logs
//     `8 ids: *editor about calculator editor probe reader shell timeclock`, then `home: app *editor Editor-SD`, paints
//     its cell (`paint 24,12 158x96`) and **no `has no picture` line for it**, which is the bug;
//   - the case `test_an_app_on_the_two_disks_is_listed_twice_and_the_card_one_says_SD` asks the three things this
//     version promises: `root_of("*twice")` is `/apps/*twice`, the door's entry is `id *twice` with the title
//     `Card twice-SD` and `on_card`, and `/apps/*twice/res/which.txt` **reads the card's file**.
// 0.164.1 (8/11): **the Shell app has a prompt, a fixed small face, and a page that is washed and rebuilt**. The owner
// saw the page come up with the corners of the boot still on it, the keyboard over the text and the box painted twice a
// second, and asked for the prompt and for a fixed, smaller letter.
//
//   - **the prompt is the app's**: a session of the shell has no prompt and no echo —a pipe is not a terminal— so the
//     page writes `> ` in front of the line a person types and echoes the line when it goes. `at_the_prompt` is
//     `box.cursor >= #tab.text`, which is one number and not a search for the last newline;
//   - **the letters are three steps of the face** (`kTerminal = 3`, 33 px with the disk's bold and 21 with the flash's
//     table): a terminal does not grow its letters with the screen, and this one is dense on purpose;
//   - **the page washes the glass** (`ui.clear(15, 'gc16')`): the corners kept the picture of the boot because the app
//     painted only inside its margins (docs/40);
//   - **and it is built again when the system gives another view** (`on_resize`): the keyboard is the system's and
//     always sits outside the app's view, so when it opens the view shrinks and the app heard nothing about it and kept
//     painting its 778 rows over the band. It now gives the old controls back (`release`) and builds the page in the box
//     it is given, with the tabs and their text (docs/30);
//   - **what is still the system's and measured here**: with the keyboard as the box's target, an **idle** page paints
//     15 times in 12 s against 5 without it —`caret_poll` invalidates the box **whole** and presents it in `du` every
//     600 ms to blink the block, so a terminal pays a full-page `du` twice a second—. The block should cost the rect it
//     occupies, and that is a change of the box's paint and not of this app (docs/30, docs/90);
// 0.164.2 (8/11): **an empty strip washes its own box, and the defect that is still open is written down here**. The
// owner closed the only tab of the Shell app and the page's layout broke.
//
//   - **what is fixed**: `tabs_paint` filled its box and then returned early on a count of zero —before the fill, so a
//     strip with no tab left its words on the glass for ever— and `l_tabs_draw` skipped the painter for a count of zero
//     as well (`count > 0`). Both are gone: a strip with no tab is a page state of its own and it washes itself
//     (docs/30, docs/90);
//   - **what is measured and is not fixed**: on the emulator, **assigning a field of a strip does not repaint it**. A page
//     with one strip presents, then `s.items = { 'Only' }` (or `s.index = 2`) and presents again: the second present
//     paints **nothing** —one `paint 20,20 500x60` in the whole run, and no line after the second `present true`— while
//     a label's `text` does repaint the page. That is why closing a tab leaves the strip as it was, and it is the defect
//     to chase next: the `is_tabs_field` branch of `l_canvas_newindex` computes `redraw` and asks for the invalidate, and
//     the paint that follows does not happen (docs/30, docs/90);
//   - **the app waits for it**: `show()` with no tab leaves a bare prompt and `File -> New` opens a session again, so the
//     page is legal with no tab —but the strip of the closed tab is still on the glass until the defect above is found—.
// 0.164.3 (8/11): **the strip of the system is asked for whole when the glass under it stops being the bar**. The
// owner: the splash erases the bar, and an app that opens repaints only what changed of it.
//
//   - **the picture of the app washes the whole panel** —the bar with it— and `bar` paints itself only when the minute,
//     the title or the charge changes, so what the title did after the picture was **the box of the title** and not the
//     strip: the hour and the battery stayed erased (docs/30, docs/50);
//   - **`bar::refresh()` is asked right after a picture that washed the glass** —the app's splash in `run_app`, and the
//     one a person asks for with the `splash` command— and **when the view comes back from full screen**
//     (`ui::make_view`); the bar then paints its 48 rows whole with `gc16` on the next turn (docs/50);
//   - **the moments that put the board down still do not ask for it**: `poweroff`, `reboot` and the deep sleep paint
//     their picture through `power.cpp` and the strip does not come back on it —it must not, until the next boot—.
//     The wish lives in the two places that put a page back on the glass and nowhere else;
//   - **measured on the emulator, 8/11**: an app whose manifest names a splash gives
//     `[I][SPLASH] /apps/sp/res/start.png, 460x200 at 40,380` and then `gui: paint 0,0 540x48 gc16`, which is the strip
//     painted whole over the picture; before this version the line after the splash was the box of the title only;
//   - the case `test_an_app_that_names_a_picture_has_it_painted_before_it_starts` now reads two more pixels: the line
//     under the strip (row 47) is ink and the paper of the strip (row 10) is paper, so the bar is back and what is above
//     the page is not the wash of the splash.
// 0.164.4 (8/11): **a command that opens a file says which file**. The owner, at the prompt: *«si cat demana <path>
// perqué hexdump te <path> per defecte? no hauria de dir el mateix que cat?»*. It should, and now it does.
//
//   - **`hexdump` with nothing after it answers `usage: hexdump <path>`** and reads nothing: it used to answer with the
//     7 168 B of `/rtc`, the one memory a person most wants to look at and the one thing a prompt must not guess. `cat`
//     already said `usage: cat <path>` and now the two say the same thing (docs/30, docs/70);
//   - **`ls` keeps its default** and it is not the same case: it lists a place and the door of the places is `/`, while
//     `cat` and `hexdump` read a file and a file has to be named (docs/70);
//   - **the help line says `<path>`** (`hexdump <path>    the bytes of a file in hex and in visible letters`), and the
//     README and docs/70 no longer promise a default that does not exist: `hexdump /rtc` is how the memory is looked at;
//   - the case `test_hexdump_with_no_path_says_what_it_wants` (test_shell) runs `hexdump` with nothing after it, asserts
//     the line is the usage one, that it ran `false`, and that **no hex of the memory is on the wire**; then it asks the
//     same of `cat`, so the two commands are measured together.
// 0.165.0 (8/11): **the picture of the boot is full screen, in the fastest format the glass reads, and it says which build
// it is**. The owner brought three webp pictures of his own and asked for them as the background of the splash, with a
// discreet `lunokOS vX.Y.Z` in a corner in the system's face.
//
//   - **three pictures at the panel's own size** (`data/res/splash/boot-2.bmp`, `boot-3.bmp`, `boot-5.bmp`): 540 × 960,
//     the middle of the square cut to the shape of the glass and scaled once, so the splash paints them **at 0,0 and
//     whole** and not in the middle of a white page —the splash cuts an image bigger than the glass and leaves one that
//     fits as it came, so the size has to be the panel's— (docs/30, docs/50);
//   - **BMP and not PNG**: a picture of the boot is read at every boot and an uncompressed **8-bit paletted** file is a
//     row and a table lookup per pixel, with no decompressor in the way —519 478 B each, and the decoder already hands
//     4-bit rows to the glass—. The three are measured on the emulator: `[I][SPLASH] /res/splash/boot-2.bmp, 540x960 at
//     0,0` (docs/50);
//   - **the stamp**: `lunokOS v<version>` drawn by `draw_version` after the picture is on the surface and before the one
//     `gc16` that takes both to the panel, **black, at scale 2 of the face the system draws with and a margin of sixteen
//     pixels** in the bottom right corner. At the boot that face is the five by seven of the flash —a stamp, which is
//     what this is— and later it is the bold face of the disk (docs/30, docs/50);
//   - **what is still to come, and the owner said so**: an argument with a **list of pictures** among which the splash
//     picks one at random, and **formatted text**. The three files are named so that list has something to name, and the
//     stamp is the first text a splash draws;
//   - **measured on the emulator, 8/11**: the page dumped after `splash /res/splash/boot-2.bmp` is 540 × 960 and its bottom
//     right corner carries **980 dark pixels** of the stamp, which reads `lunokOS v0.164.4` in the crop.
// 0.165.1 (8/11): **the picture of a moment is on the glass before the board goes**. The owner: the reboot picture opens
// too late and is never seen.
//
//   - **why**: `splash::show` waits for the ink that was moving **before** its picture and then asks for a `gc16` of the
//     whole panel —half a second of ink— and `reboot` restarted the board where the call found it, so the restart cut
//     the paint and the glass kept the page of before (docs/50);
//   - **`splash::wait()`** is the missing half: it gives the panel one turn of the queue —the driver takes the command of
//     the paint on its next turn, so asking "is the ink over?" in the same millisecond answers about the ink of before—
//     and then waits on `idle()` with the ceiling the splash already had, because a panel that never answers must not
//     keep a board from restarting (docs/50);
//   - **the three moments that put the board down ask it**: `reboot` (after writing its line, so the wire and the glass
//     say the same thing), `poweroff` (after flushing the console and before cutting the supply) and the deep sleep
//     (`power.cpp`). A light sleep paints nothing and waits for nothing (docs/50, docs/60);
//   - **measured on the emulator, 8/11**: `reboot` with the page dumped at the end leaves the picture of the moment on the
//     glass —`reboot`, the line and `restarting`, and the stamp `lunokOS v0.165.0` in the corner, 5 158 dark pixels—
//     where before the dump was the page of the prompt. **The emulator cannot measure the wait itself**: its glass paints
//     what it is given in the same call and the ink is not paid, so what the emulator proves is that the picture is asked
//     for and up at the end, and the waiting is what a real panel needs (docs/50, docs/85);
// 0.166.0 (8/11): **the page of About is a list, one row a library with its name and its version**. The owner: *«usa una
// llista i afegeix el nom de la llibrería i versió a l'entrada, així pots tenir més elements que els que caben a pantalla
// i no cal fer el paginador merdós de "More XX to XX of XX"»*.
//
//   - **the rows are the entries of a `ui.list`** (`ui.list_entry` filled with two labels): the control places the ones
//     that do not fit a page **out of its frame**, the dial turns the pages and **its foot says `1 / 4`**, so the app has
//     no count of rows, no page, no `per_page` and no `More` row any more —four numbers and a sentence that the system
//     already had— (docs/30, docs/90);
//   - **the name and the version are two texts in one row and side by side**: the first attempt put the version at the
//     bottom of the row and a label paints its own paper, so the small one **painted over the lower half of the name**
//     and the page showed the libraries cut in half. The name keeps the left, the version a 96 px column on the right
//     (docs/30);
//   - **the list is given one row less than the room left**: its foot is a band of its own and a list that ends at the
//     last pixel of the page leaves it cut at the edge, which is what the first version did (`1 / 4` half off the glass);
//   - **measured on the emulator, 8/11**: the dumped page reads `alarms 0.1.0`, `board_atom 0.1.0`, `board_esp32 0.1.0`,
//     `board_matrix 0.1.0`, `board_paper 0.1.0`… with the separator of each row, and the foot of the list says `1 / 4`
//     with the chevron of the pages after it; the app logs `about: 25 rows in a list of 567 px` and no `lua:` line;
//   - the case `test_the_about_app_lists_the_libraries_of_the_project` now asserts that **the app's own paginator is
//     gone** (`about: cover page` is not in the log) and that the list was built with the rows of the tree.
//
// 0.166.1 (8/11): **the grid of the launcher gets its size, and the cell that the system has just filled is the one that
// is refreshed**. The owner: *«vull les icones i el text mes gran al grid. I evidentment has de refrescar millor l'ara de
// l'icona que s'acaba d'actualitzar»*.
//
//   - **two columns instead of three and a name at 33 px instead of 22** (`kColumns`, `kName`): a cell is 250 wide with
//     its padding and the name of `EPUB reader` fits in one line with room to spare, and the box of the cell is
//     recomputed in `build()` from the width of the view, so a narrow glass gets narrower cells instead of a name that
//     runs off the edge (docs/30, docs/90);
//   - **the mark of the cell stays 72 px, and that was measured three times**: the cap of `icon_grid_cells` was raised to
//     120 and then to 96 and the emulator gave **exactly the same two cells without a picture in all three runs** (`Squares`
//     and `Timeclock`), so the size of the canvas was never the cause and a canvas of 120 x 120 at four bits is 7 200 B
//     of a page for no measured gain (docs/75);
//   - **why those cells are empty is in the disk, not in the grid**: `squares` is an app of the emulator's SD card
//     (`emulator/sd/apps/squares`) with a `manifest.json` and a `squares.lua` and **no `res/` and no `icon` field at
//     all**, and the card's `editor` and the workspace's `probe` are the same, so the cell is a name and no picture and
//     that is what the manifest says (docs/40);
//   - **one cell is still open and it is not explained**: `timeclock` lives in `apps/timeclock` **with** a
//     `res/icon.png` and comes out with its name and without its picture in all three runs of the emulator (docs/90);
//   - **the present is one per app, right after its picture** (the app's loop): a single paint of the whole page when the
//     queue is over was tried and measured wrong —it lands **before** the cells arrive, so the last pictures of the grid
//     stay unwritten—, and the owner's sentence asks for the opposite: the cell that has just been filled is the one that
//     goes to the glass;
//   - **measured on the emulator, 8/11**: `home: 9 cells`, `lua: 0`, one `[I][UI] paint 0,0 540x912 mode 1 du` for the wash
//     of the page and then a `gc16` per cell, and the dumped page shows the apps of the disk in two columns with their
//     names at 33 px (`Editor-SD` from the card, `About`, `Calculator`, `Editor`, `Probe`, `EPUB reader`, `Shell`);
//   - **and a defect is left open and named here** (measured, not fixed): with a state built from nothing the grid of the
//     launcher comes out **empty** —`home: about has no picture: this canvas is not on the screen any more` five times—
//     because the stream of pictures writes into canvases that a second `build()` of the view has already given back.
//     That is the second half of the owner's sentence, and it is the next piece of work (docs/90).
//
// 0.166.2 (8/11): **the word of the unit in the name of a redirected app is read and not only written**. The owner: *«si
// l'id porta asterisc davant, has de mirar l'última paraula amb guió del nom de l'app "Editor-SD"»*.
//
//   - **`name_says_unit` reads the last word behind a dash of the title** and answers which unit the name claims, so a
//     manifest that already says `-SD` keeps its name instead of becoming `Editor-SD-SD`, and a name that says one disk
//     while the door answered the other **keeps its word** and puts a line in the log naming both —a person reads the
//     name, and hiding one of the two would be the lie— (docs/40, docs/70);
//   - **the paths are still the door's**: the word decides what the name says and `vfs` decides which disk a file comes
//     from, so a name that disagrees with its disk cannot send an app to read the other copy's files (docs/70);
//   - **measured on the emulator, 8/11**: the launcher logs `home: app *editor Editor-SD` with `[I][APPS] *editor`, and with
//     `cfg home none` the same id runs —`editor: File opens, saves and closes...`— which is the card's
//     `emulator/sd/apps/editor`. The `cannot start the interpreter` that `--run '*editor'` gives **with the launcher
//     still in front** is the interpreter of the launcher holding the heap and the second one not fitting, not a name
//     that does not resolve (docs/45 D1, docs/75);
//   - **and the missing picture of that copy is a fact of the disk and not of the resolution**: `home: *editor has no
//     picture` is a card folder with a `manifest.json` and an `editor.lua` and **no `res/` at all**, so there is nothing
//     to draw (docs/40);
//   - the case `test_a_name_that_already_says_its_unit_keeps_its_word` covers the three readings: a name with the word
//     of its own disk is not given a second one, a name that says the other disk keeps its word, and a bare `SD` in a
//     name is a name and not a unit.
//
// 0.166.3 (8/11): **an app is started only when the one in front has let go of the interpreter**, and that is what the
// owner saw as a name that does not resolve.
//
//   - **`run_app` closes the app in front first**: `luabind::start` answers no while a state is alive —`if (g_state !=
//     nullptr) return false`—, so `run <id>` with an app in front said `cannot start the interpreter` **for every id of
//     the disk** —`editor`, `shell`, `about`, `*editor`, all four measured on the emulator on 8/11—, which a person reads as
//     «`Editor-SD` does not resolve» and is a start that never happened (docs/30, docs/40);
//   - **and it is the same road an app that asks for another one takes**: the goodbye hooks, the views, the colour and the
//     bar go with the app that leaves, so the app that comes up is in the same state as one the launcher asked for by a
//     tap (docs/30, docs/90);
//   - **measured on the emulator, 8/11**, with the launcher in front and `run <id>` from a script: before, `cannot start the
//     interpreter` for all four; after, `app closed` then `[I][APPS] *editor` and `editor: File opens, saves and closes;
//     View is read only and wrapping`, which is the card's copy of `emulator/sd/apps/editor`;
//   - **and the two halves of the owner's report are answered**: the id `*editor` **does** resolve —`/apps/*editor` is
//     the card's folder— and what it was missing was the interpreter, not the name; the picture of that copy is missing
//     for a reason of the disk (`emulator/sd/apps/editor` has no `res/` at all), which is what `home: *editor has no
//     picture` says (docs/40, docs/70).
//
// 0.167.0 (8/11): **one road out of a page, the pages said in dots, and a picture for the card's editor**. The owner:
// *«a l'About, si entro en una opció de la llista no puc sortir... les 3 pulsacions del botó central tanca l'app però no
// torna a Home... 3 botons => back, 3 tocs => back, pinch => back»*, *«a la llista de llibreries no usis un paginador
// manual, usa el paginador de boletes (slide esquerra i dreta et desplaça per la llista)»*, *«revisa el refresc de l'app...
// al tancar una vista, el sistema avisa a la vista de sota que es redibuixi»*.
//
//   - **`shell::leave_one_page()` and its four answers**: a page with another one on top gives the top one back and stays;
//     a page that is the only one goes with the app **and the page that asked for it takes the glass back**, which is the
//     launcher; a launcher is the one page that is not left; and no app at all is nothing to do. The **pinch**, the
//     **three presses of the middle key** and the **three taps on the glass** all call it, which is what the header of
//     `go_back_one_view` already promised and only two of the three did —the button closed the app and left the prompt
//     where the launcher had been, which is exactly what the owner described— (docs/30, docs/45);
//   - **the three presses are the system's on every page** (the exception for a page with controls is gone): the owner's
//     About detail has a Back control, and the gesture he asked for is the one that has to work there. The first two
//     presses are still delivered to the page, so the ring moves as it did (docs/30, docs/90);
//   - **the system owes the page when a view is given back** (`ui::owe_the_page()`): the page below was drawn before the
//     one on top of it, so its ink is not under the glass any more and the app's next present is the **whole view with
//     `gc16`** instead of the damage of the page it has just built. It is the system's business because the system made
//     the change of page (docs/30, docs/50);
//   - **the pages of a list are dots** (`list_draw`): the page on the glass is one full dot and the others are hollow
//     rings in the middle of the band, and the arrows and the `1 / 4` went with them. A count that does not fit a row of
//     dots is said in numbers, because dropping the only sign of the page would be worse than the number (docs/90);
//   - **the About turns its list with a slide**: `touch.gesture()` in its loop, a drag of forty pixels that is mostly
//     horizontal, left for the page after and right for the one before, and the list clamps the number (docs/30);
//   - **and the room of that list is two rows less**: the head is measured by the app with `ui.text_width` and placed by
//     the panel with the face it draws with, and the two disagreed by one line of the big size —the list of 0.166.0 ended
//     20 px under the glass and the foot of the page was **off the page**, found on the emulator on 8/11 with a dump— (docs/30);
//   - **the card's editor has a picture**: `OLD/PaperOS/data/app/Editor/icon.icn` is a square of four bits a pixel without
//     a header —4 418 bytes are 94 x 94— and it is `emulator/sd/apps/editor/res/icon.png` now, named by that copy's
//     manifest, so `home: *editor has no picture` is gone (docs/40);
//   - **measured on the emulator, 8/11**: the About page dumps one full dot and three hollow ones at `x 247..292, y 849..854`
//     under a list of `25 rows in a list of 503 px`, `lua: 0`, and the eight separators of the rows at `615, 679, 743,
//     807, 861` with the band of dots under the last one (docs/30, docs/85).
//
// 0.167.1 (8/11): **the list of the libraries has no frame**. The owner: *«no posis vora a la llista de llibreries
// d'About»*. The focus of a list is drawn as a two-pixel frame around it, which is right for a list whose rows the system
// paints —there the frame is the only thing that says where the list is— and wrong for a list of entries: those rows are
// controls of the app, each with its own paper and its own ring, so the frame was a box nobody asked for around the whole
// column. Measured on the emulator with a dump of the page: the two pixels at `x 20` and `x 518` along the list are gone and
// the rows, the separators and the dots are where they were (docs/30, docs/90).
//
// 0.168.0 (8/11): **the law of the redraw, written down, and the first half of it made true**. The owner: *«els UIControl
// només ha de redibuixar la part que ha canviat del anterior fotograma, no tot el control… En cas de que el sistema demani
// un redibuixat de la vista a una app, aquesta ho farà a la qualitat que necessiti… el sistema sempre et passarà una vista
// neta. Ell ha d'esborrar la vista i la pantalla abans de passar-te la vista. Preferiblement d'una sola pasada»*.
//
//   - **the law is a memory of the project now** (`/memories/repo/lunokOS-drawing-law.md`): a control draws the part that
//     changed and nothing else, in the smallest area and the quality its graphics need; the system hands a clean view in
//     one pass and the app chooses what to paint and in which quality; the system asks for a whole view when an app starts
//     and for a part when a part of the view is dirtied (docs/30, docs/50);
//   - **`attach()` already did the system's half**: it releases every control of the app before, fills the view with white,
//     clears the damage and asks the glass for the whole page with the cleaner the last page deserves —one pass, before a
//     single control of the new app exists—. It is measured on the emulator: `gui: paint 0,0 540x960 gc16`, once, and nothing
//     else until the app presents (docs/30, docs/50);
//   - **and a `ui.clear(15, …)` on a page the system has just handed white costs nothing** (0.168.0): `g_page_owes_nothing`
//     is set by `attach()` and cleared by the first control composed into the page or by any other wash, so the eleven
//     `ui.clear` calls of six apps —About 2, Editor 2, Shell 2, Timeclock 3, Calculator 1, Home 1— stop paying a second
//     whole-page clean for paper that is already clean. **Measured on the emulator, 8/11**: running About logs
//     `a clear of a page the system handed white: nothing to wash` once and the whole-page paints of the run are **one**
//     (the system's) instead of two, with the page still whole —separators at `551, 615, 679, 743, 807` and the four dots
//     at `y 850..853`— (docs/30, docs/50);
//   - **what is left of the law and is not done**, said here so it is not forgotten: (1) `canvas_invalidate` marks a
//     **whole** control and is called from **31 places** of `luabind.cpp`, so a label whose text changes repaints its whole
//     box; (2) `caret_poll` repaints the whole box of several lines every 600 ms, which wants a `textbox_caret_box` in the
//     kernel and one rectangle of damage; (3) `list_draw` and `tabs_paint` fill their whole frame on every draw instead of
//     the rows and the tab that changed (docs/30, docs/90).
//
// 0.168.1 (8/11): **a focus that moves repaints two controls, each in its own box, and cleans them**. The owner:
// *«al moure el focus s'han de redibuixar dos controls, el nou per dibuixar-lo amb focus i l'anterior que s'ha de
// dibuixar sense focus»*.
//
//   - **the two controls were already the two**, and it was measured with an emulator app of two buttons: `ui.focus(two)`
//     invalidates the one that loses the ring and the one that takes it, and nothing else (`ui_apply_focus`). What was
//     wrong was the **quality** and the **area** (docs/30);
//   - **the quality was `du`, which paints and does not take ink away**: the ring that went stayed on the glass as a ghost,
//     which is what the owner saw. `ui_feedback` —the paint of a focus and of a press— presents now with **`gl16`**, that
//     cleans the box without the flash of `gc16` (docs/30, docs/50);
//   - **and the two boxes were merged into one**: presenting everything pending with one quality went through
//     `merge_all`, which turns the two regions into the rectangle around both —measured on the emulator, 8/11: two boxes of
//     `300x64` at `y 40` and `y 320` became **one of `300x344`**, 103 200 pixels of ink where 38 400 are owed—. The new
//     `ui::present_each` writes the quality into each region that already exists and leaves the boxes where they are, and
//     `ui_feedback` goes through it with the ops table (docs/30, docs/50);
//   - **measured on the emulator, 8/11**: moving the ring from `One` to `Two` paints `20,40 300x64 gl16` and
//     `20,320 300x64 gl16` — two regions, the two controls, the air between them untouched — and the log of the run has no
//     other paint between the two names (docs/30, docs/85);
//   - `present_all` keeps merging, because an app that asks for a whole page with one quality asked for exactly that.
//
// 0.168.2 (8/11): **the redraw box of a control is real for every control, and the caret is the first one that says which
// part changed**. The owner: *«un control que es vol redibuixar ha de cridar a tots els controls fills demanant-los el seu
// buffer… el pare rep el canvas del fill i després decideix quines parts pintar de si mateix (incloent l'àrea del fill)»*
// and *«ho has d'aplicar a tots els controls del sistema»*.
//
//   - **the composition already worked that way**: `compose_region` copies only the damaged regions of a child, cut at the
//     father's border, with the child's quality, and the father takes them as its own damage. What was missing is that
//     **the redraw box of a control without a view of its own was never applied**: most controls of this system —labels,
//     buttons, boxes of text— paint straight into the nearest canvas that has one, so the rectangle lived on the control and
//     the paint went out whole. `open_draw` now **cuts it into that canvas**, in that canvas' pixels, which is one change
//     that every control inherits (docs/30, docs/90);
//   - **and the caret is the first control that says which part changed**: `textbox_draw` and `textbox_multiline_draw`
//     record the block of the caret in the canvas that draws it, `textbox_caret_box` brings it back to the control's pixels
//     —a box with no view drew its caret on canvas 0, which is the page, and asking the control for a box it never had is
//     why the blink went on painting everything— and `caret_poll` invalidates **that** rectangle instead of the control. A
//     whole invalidate forgets the block, because the text or the cursor moved and the old rectangle is a guess (docs/30);
//   - **measured on the emulator, 8/11**, a box of text of `420x64` with the ring on it and the caret blinking every 600 ms:
//     before, **8 paints of `420x64`** —215 040 pixels of ink for nine seconds of a blinking cursor—; after, **one paint of
//     the page and seven of `10x33`**, which is 2 310 pixels: **98,8 % less ink** for the same blink (docs/30, docs/50);
//   - **what is left for the same law**, said so it is not forgotten: the fields of the other controls still invalidate the
//     whole control —`canvas_invalidate` in 31 places of `luabind.cpp`— and for most of them the whole control **is** the
//     minimal area (a label's text is its box, a panel's paper is its box); the ones that can do better and do not yet are
//     the slider's value, the progress bar, the two tabs of a strip and the changed row of a list (docs/90).
//
// 0.168.3 (8/11): **the launcher does not rebuild its page for a resize that says the size it already has**, and the
// question of the lost pictures got an answer instead of a patch.
//
//   - `on_resize` compares the size with the page on the glass and returns when it is the same one: rebuilding threw seven
//     picture canvases of 72 x 72 (18 144 B) and a whole-page paint to end in the same page (docs/30, docs/50);
//   - **and the lost pictures were measured, not guessed**: with the app started **twice** —`lunokOS` boots its home app
//     and an emulator asked for it again with `--run home_eink`, which the log says with `[I][APPS] home_eink` twice— the first
//     instance's grid and its seven canvases are released while its queue of manifests is still filling the cells, and
//     every picture that arrives after that answers `this canvas is not on the screen any more`. **Started once, the page is
//     built once and all seven pictures are there** (`home: 7 ids`, `home: 7 cells`, `no picture` zero times, measured with
//     a dump of the page on 8/11) (docs/30, docs/85);
//   - **why a name can go and a picture cannot be recovered**: `ui.canvas` is the only control of this system with a buffer
//     of its own —`create_control(L, true)`— and the picture lives in those bits, so it can be given to the glass again
//     without redrawing it; a name is a label, which has no canvas and is painted straight on the page, so it exists only
//     while the page that painted it is the page on the glass (docs/30, docs/90).
//
// 0.168.4 (8/11): **a father that repaints itself asks its children for their pixels again**, which is the lost content of
// the launcher when the ring moves. The owner: *«l'efecte de perdre el contingut és quan uso el focus»*.
//
//   - **the cause**: the ring of a cell calls `grid.index = index` and `grid:invalidate()`, and a whole invalidation painted
//     the **grid's paper over its cells**. A child with nothing changed of its own was never composed again —`compose_tree`
//     only carries what is in `damage`— so the picture of the cell stayed under the paper and the name, which has no canvas
//     at all, was gone from the glass for good (docs/30, docs/90);
//   - **two halves of the fix, one rule**: `invalidate()` now **walks the subtree** and invalidates every child, and
//     `invalidate_rect()` makes a control **with bits** owe its rectangle as damage again —`gl16`, because the paper that
//     covered it was a wash—. A control without bits needs no damage of its own: drawing is what marks it, and `needs_draw`
//     is what makes it draw (docs/30);
//   - **measured on the emulator, 8/11**, a `ui.icon_grid` of two cells with a black picture of `72x72` and a name in each, the
//     grid invalidated as the ring does: before, the grid painted `20,20 400x220` and the ink inside it fell to **2 376**
//     pixels —the frames and nothing else—; after, the same paint and then `40,30 72x72 gl16` and `240,30 72x72 gl16`, with
//     **12 744** pixels of ink inside the grid. The two pictures and the two names are back (docs/30, docs/50);
//   - and the launcher of the disk is unchanged in a clean run: `home: 7 ids`, `home: 7 cells`, **no `no picture` line** and
//     35 840 pixels of ink in the grid (docs/85).
//
// 0.169.0 (8/11): **a picture can be given with its own transparency, and an app that does not ask pays not one byte**. The
// owner: *«si cal, fem-ho! que sigui opcional per estalviar memòria»*.
//
//   - **the alpha of a PNG is now a mask and not a flattening**: the decoder reads the alpha it was already reading and, when
//     the caller asks for it, hands a row of nibbles a pixel —`15` where the picture has a pixel, `0` where it leaves what
//     is under it— **before** the colours of that row. **The row lives in the caller's scratch and not in the firmware's
//     data**: the first attempt made it a `static` of the decoder and the DRAM of the Paper overflowed by **1 064 bytes**,
//     which is exactly the kind of cost this project refuses —a feature nobody uses taking memory of everybody—. The
//     scratch of a PNG is two rows now instead of one, and it is a block the caller already allocates in the extra memory
//     (docs/30, docs/75);
//   - **`canvas:image(x, y, path, w, h, mode, 'mask')`** is the seventh argument, and `ui.image` the seventh of its own:
//     without it the white of the alpha is painted over the canvas as it always was, with it the pixels stay as the canvas
//     had them. The manifest does not change, no file is added, and a picture without transparency is drawn the same either
//     way (docs/30);
//   - **measured on the emulator, 8/11**, a `72x72` PNG with a transparent ring and a black square in the middle, drawn twice
//     over a canvas of grey 8: **without** the mask the ring is `255` —white paper painted over the grey— and **with** it the
//     ring is `136`, the grey of the canvas; the square is `0` in both. The whole feature is one row of 271 bytes and a
//     branch in the row writer (docs/30, docs/50);
//   - **and a picture of a family that carries no transparency gives no mask**: it is drawn as if everything were there,
//     which is what a BMP or a JPEG is (docs/30).
//
// 0.169.1 (9/11): **`help <command>` no longer cuts a command that has two lines, and the documents name the apps that
// exist**. The owner: *«1- arregla-ho … 2- treu les apps antigues de la documentació … 6- Documenta-ho i ja ho farem»*.
//
//   - **`help splash` and `help cfg` printed the first line of the entry and nothing else**: the list is
//     `"  <syntax>  <description>"` and the entry of a command with a second line is two of them, so the second hung from no
//     syntax and was dropped. `help_continuation()` is the new mark —a line that hangs from the one before—, `g_help_previous`
//     remembers which that was, and both lines come out now. `help poweroff` still does not print the advice that closes the
//     list, which is what the old guard was for (lib/kernel/src/shell.cpp, docs/80);
//   - **the documents no longer send anybody to an app that is not on the disk**: sixty apps were deleted between 0.94.0 and
//     0.169.0 and the manuals still said `run air`, `run panel`, `run fonts`, `run heap` and a hundred and fifty more. Every
//     one of them is now the call that was measured (`env.read()`, `power.battery()`, a `lua` loop, a page of the shape it
//     describes) or the app of today that does the same job, and the two that are only a historical log are quoted and said
//     to be so (docs/30, docs/40, docs/50, docs/60, docs/70, docs/75, docs/80, docs/85, docs/90, docs/92, docs/95);
//   - **the numbers of the disk are the ones the tool printed on 9/11**: the system alone is 19 files and 1 688 651 B —of
//     which 1 558 434 B are three splash bitmaps— and the seven apps are 34 files and 290 501 B, so `build/disk/m5-paper` is
//     53 files and 1 979 152 B and `build/disk/emulator` 55 files and 1 981 991 B (docs/70, docs/80, docs/85). **And the four
//     4 MB boards do not fit**: 1 688 651 B of system against the 1 441 792 B of `partitions.csv`, so `-t buildfs` stops with
//     `this disk does not fit` until the splash bitmaps are not carried (docs/70);
//   - **the table of canvases is 2 978 slots of 176 bytes and not 3 276 of 160**, and the document says what happens when the
//     board has no extra memory: `allocate(..., true)` falls back to the internal RAM, so the half megabyte lands there and the
//     halving leaves the board with what fits —**744 slots would be 130 944 B, 39 % of what an ATOM reports free**—. It is
//     written down as the pending risk it is, with the numbers that are measured and the one that is not (docs/75, docs/90);
//   - **the emulator is the emulator everywhere**: the manuals called it *banc* and the code called it *bench*, and both are
//     now `emulador` and `emulator`, including the two messages a person reads (README, docs/85). The himem *banks* of 32 KB
//     keep their name, because a bank is not a machine (docs/50).
//
// 0.169.2 (9/11): **the twelve red cases of 0.169.0 are green, and four of them were the code's fault**. The owner: *«els
// test han de passar nets. Analitza les proves en vermell, i el codi a veure qui te raò»*.
//
//   - **a row of a picture had no mask** (lib/img): `Row` is declared and then started by every decoder, and `alpha` —the mask
//     of 0.169.0— had no value, so the row of a BMP wrote fifteen through a pointer of the stack. `test_book` died with a
//     segmentation fault on the first picture of a book, and `bmp`, `ico`, `jpeg` and `pnm` carried the same fault (docs/75);
//   - **and the scratch the caller was told to give is the one the decoder asks for**: `png_scratch` counted two rows —the
//     colours and the mask— and `decode_png` counted one of them when no mask was asked, so a block one byte short of the
//     answer was accepted. A caller that allocates what it was told has to be refused, and now is (docs/30);
//   - **the damage of a hole is not the damage of a control** (kernel/ui): 0.168.4 put a `damage_add` inside
//     `invalidate_rect`, and that call is also how a father is told which hole a child left behind —in the *father's* pixels—
//     so a control that moved painted a whole page twice, the first time at the wrong place. The rectangle of a child that
//     must come back is now added where a father invalidates its children, and only when the father has no buffer of its own
//     to compose them into: the paper of the grid over the cells of the launcher is the case the owner met, and it is kept
//     (docs/30, docs/90);
//   - **a page that painted only in canvases is not white paper**: `g_levels` counts the primitives of the page, and the
//     pixels of a control arrive composed, so the wash between two apps was skipped over the ghost of the last one. The
//     composition says so now and the wash is the sixteen-level one (docs/50, docs/90);
//   - **the page a resize makes is not white on the glass**: the view is white and the glass holds the page of the size
//     before, so `ui.clear(15, …)` is not free after a resize —a launcher that grew a page kept the old one around it— and a
//     view that has just been made still owes nothing (docs/30, docs/90);
//   - **a strip of tabs that says three tabs and hands no name is refused** instead of washing itself in silence (docs/30);
//   - **and the cases that were the test's**: the card's copy of an app is `*<id>` and the word of its unit is at the end of
//     its *name* (0.164.0); `hexdump` asks for its path (0.164.4); the instrument app of the emulator is not in the list of
//     the apps of the repository; three taps on the glass or three presses of the centre are the system's way out (0.163.0,
//     0.167.0), so a case that taps three times lets the window pass; a detached app's console is the log and the log is
//     echoed, so the case of `&` turns the mirror off; `ui.image` takes the mask of 0.169.0; and the sentence a launcher
//     answers to a pinch is the one road out of a page's (0.167.0) (docs/30, docs/40, docs/70, docs/92);
//   - **and the About page had no ring on its first row**: the list takes the dial when it is built, so the middle key
//     pressed the list and the page could only be opened with a finger. The app gives the ring to the first row, which is
//     what the launcher does with its first cell (apps/about, docs/30);
//   - **the suite is 548 cases and none of them is red**: the twelve of 0.169.0 included a crash that hid forty-nine cases —
//     everything after `test_book` in `test_apps`— and those are the ones that had aged.
//
// 0.169.3 (9/11): **what one instruction of Lua costs is a measurement and not a comment**. The owner asked whether a
// smaller budget before the yield would make the buttons and the glass faster, and then: *«arregla el comentari i fes
// una app de mesura»*.
//
//   - **the comment of `kTurnBudgetInstructions` was wrong by ten**: it said "~0.6 ms at 160 MHz" and 100 000
//     instructions at the 60 ns the emulator prices them are **6 ms**. It is corrected, it no longer hides that the 60 ns
//     is a model, and it points at the app that says the board's number (lib/luabind, docs/85);
//   - **`apps/luaperf`** (a new app, and a dial for an icon): a loop of `for i = 1, n do k = k + 1 end`, **three
//     instructions a lap** counted on the bytecode of Lua 5.4.7 —`ADDI`, `MMBINI` and `FORLOOP`— timed with `sys.time_ms()`
//     after a warm-up thrown away and as **the smallest of three tries with the glass quiet**, and then half a second of
//     laps to price a whole turn. On the emulator it says **66 ns an instruction and 6.6 ms a turn**, which is the model
//     of 60 ns with its 850 µs turn inside, and it prints the verdict against the **120 ms** a page of ink costs: the
//     budget of a turn is nothing next to the glass (apps/luaperf, docs/95 E-9, docs/85);
//   - **and the number it does *not* print is the turn of the kernel alone**, because it cannot be had from here: a turn
//     falls on every 33 333rd lap, so both costs grow with the number of laps and only their sum is visible. The first
//     version of the app subtracted a figure from itself and said "a turn costs 0.0 ms more"; the measurement is one
//     number now, and the comment says why (apps/luaperf);
//   - **the first version was also measured on a moving glass**: the same loop read 195 ns a lap in one run and 2 085 in
//     another, the second with the page of the boot still being painted, which is why a sample now waits for `ui.idle()`
//     and the smallest one is kept —a paint only ever adds time— (apps/luaperf);
//   - **the Paper carries it** (its list of apps goes from seven to eight) and the counts of docs/70, docs/80, docs/85
//     and `platformio.ini` are today's measurement: `apps/` is **38 files and 305 737 B**, `build/disk/m5-paper` is **57
//     files and 1 994 388 B** and `build/disk/emulator` **60 files and 1 998 650 B** (docs/70, docs/80, docs/85).
//
// 0.169.4 (9/11): **the apps of prova carry the lunokhod**. The owner: *«fes una icona específica per totes les apps
// de prova. Una icona del lunokhod estaria molt guay, però amb l'icona d'un "bug" ja està bé»*.
//
//   - **a rover and not a dial**: `luaperf` shipped in 0.169.3 with a dial, which is a fine picture for a measurement
//     and a bad way of telling a person that an app is one of the ones that exist to measure the system. The drawing is
//     now the rover the system is named after —body, lid, three of its eight wheels and the antenna— and it says that
//     at 74 x 72 px on the launcher's grid (apps/luaperf, docs/40);
//   - **and the drawing is shared and out of the app**: the master is `tools/icons/Lunokhod.png` —448 x 448, in the four
//     greys of the paper project's masters, beside the other twenty-five— and the app carries only the 94 x 94 the
//     manifest names, made with `tools/make_icon.py`, because an icon inside the app is the only path a manifest can name
//     and a copy per app is a lunokhod that goes old the day the drawing changes (docs/40). A second app of prova brings
//     the same picture with one command and no new drawing.
//
// 0.169.5 (9/11): **a pass over every document against the code, and twenty claims the documents made that the code does
// not**. The owner: *«revisa de nou la documentació per trobar afirmacions falses. Has de comparar l'afirmació amb el codi
// per assegurar-te que tens la prova de que es real»*. Each one was checked against the source before it was touched, and
// the source won every time.
//
//   - **the system has a screen, and the documents said it did not**: the table of devices of docs/50 gave `IDisplay` as
//     *pendent* on the Paper and *nothing* on the emulator, with a snippet `virtual IDisplay* display() = 0;  // nullptr
//     en aquesta versió` — and `IPlatform::display()` returns an **`ISurface*`**, the Paper's is the IT8951E with all
//     eight update modes and the emulator's is the glass of the model (`hal_paper.cpp`, `hal_emulator.cpp`). It is the
//     interface the whole page of docs/30, docs/90 and docs/92 is built on (docs/50);
//   - **and docs/00 listed the screen as something the emulator does not model**, when the ink is a table of eight modes
//     with their milliseconds, their trace and their dirt (`lib/hal_emulator/include/emu/ink.h`). What is not modelled is
//     the consumption, the wear of the flash and the physics (docs/00);
//   - **no app is killed after two silent turns**: docs/00 said it was, and the counter of 0.90.0 **yields** instead of
//     killing (`kTurnBudgetInstructions`, `lua_yield`), which is what docs/20 and docs/10 say in the same repository
//     (docs/00, docs/20);
//   - **`console.read_byte`, `time.utc` and `time.tz` exist**: docs/30 listed them under «Encara no hi és» and docs/90 in
//     its table of absent calls, and the three are in `lib/luabind/src/luabind.cpp` and have been since 0.125.0. The list is
//     now `sys.reboot`, `console.flush`/`read`/`on_data`, `time.format`/`parse`/`set`/`sync`, `Promise.all`/`race`,
//     `app.spawn`/`raise`/`save`/`restore`, `net.softap`, `power.off`, `store` and `log` (docs/30, docs/90);
//   - **the controls are not thirty-two**: docs/30 said «hi ha fins a 32 controls» in a section that two pages later says
//     there is no number, because since 0.102.0 the table is taken from the extra memory (2 978 slots of 176 bytes on the
//     Paper). The old number is kept only where it is history, in docs/90 (docs/30);
//   - **the ceiling of an app is 131 072 B and not 65 536**: docs/20 gave the old number of 0.81.2 as the current one, and
//     docs/75 has the right one with its date (docs/20);
//   - **the `mask` of a picture is part of the contract**: `ui.image` takes it as a seventh argument and `cv:image` as an
//     eighth since 0.169.0, and neither signature in docs/30 nor the one in the README said so (docs/30, README);
//   - **`--sd` does create the card**: docs/85's row said the emulator did not create `emulator/sd` since 0.88.2, and the
//     same document, forty lines later, said it did — `make_slot()` does, and that is what makes a Paper come up with a
//     slot (docs/85);
//   - **the listing of the repository is 6 113 B and eight folders**, and docs/85 said 45 KB and sixty folders: the
//     repository's `apps/` is what the API answers, and it was asked today with `curl` (9/11) — the example of
//     a `repo install` keeps the numbers of the day it was taken, as the quote it is (docs/85);
//   - **the system disk is 19 files and 1 688 651 B**: docs/70 and docs/80 said 20 and 1 688 656, which counts
//     `data/data/rtccheck/rtccheck.txt` — an app's own state, gitignored — and the three splash bitmaps are 1 558 434 B and
//     not 1 595 456, which is the whole `data/res/splash` folder with the five PNGs of the other moments (docs/70, docs/80);
//   - **the EPUB layer is not pending**: docs/70's section of the zip ended with «el que encara no hi és: la capa d'epub»,
//     and `lib/epub` is the zip, the OPF, the XHTML and the paginator, and the book the reader opens every day (docs/46);
//   - **the table of utility libraries of docs/10 is complete again**: `img`, `inflate`, `zip`, `ttf` and `epub` were
//     missing from it, and the document says that a library that is not in it is an oversight and not a decision (docs/10);
//   - **the module block is 65 536 B and the entry's ceiling is not**: docs/75 said the two were the same number, which was
//     true before 0.149.0 (`kSourceCap` is 98 304) (docs/75);
//   - **and the small ones of the same pass**: the Paper has a page and it rotates, so a button is where the contact is and
//     not where the screen is (docs/30); what a dead app leaves is the console line and not a page, and the reason is not
//     «there is no screen» any more (docs/20); `apps/` is 37 files in the tree and not 35 (docs/80); the ATOM's firmware is
//     2 265 873 B and the README had 2 265 753 from the build of another day (README); the log line of a manifest without
//     an entry is `[W][APPS] ...` and not `apps: ...` (docs/40); and docs/90's «Fora de la v1» no longer lists the screen
//     or installing an app from the prompt, both of which are in and used every day (docs/90);
//   - **and two comments that lied about their own numbers**: `ui.h` said a slot of the table of canvases is 192 bytes
//     (176, said by the boot's own line) and `shell.cpp` priced the listing of the repository at forty-four kilobytes
//     (six today, and forty-four with the sixty apps of 0.88.5) (`kernel/ui.h`, `kernel/shell.cpp`).
//
// 0.169.6 (9/11): **les dues apps que falten, escrites amb el que el sistema encara no els dona**. The owner left the
// spec of two apps —the time zone with a map and the eraser of the NVS and the card— and asked for the documents to be
// updated, so the work is on the list with the code that decides how it can be done.
//
//   - **`tz`** (docs/90): a map of the world with the zones marked, a tap that says `Selected timezone: <valor>` in a
//     `ui.label`, a button for `net.ntp`, a checkbox for the leap year that comes on when the hour syncs, and a list of
//     the first day of the week that has to reach the calendar. **It saves on close and not while it is used**, and
//     `time.tz` desa a l'acte (`cfg::set_tz_minutes` → `remember_tz`), so the app keeps the choice and calls it in
//     `on_close` — which is now in the contract (docs/30);
//   - **and the two things it cannot decide**: `calendar::weekday_of` answers 1 = Monday and `ui.month` lays the first
//     column from it without asking (`cells_of`), and `days_in_month` counts the 29th of February always. A week that
//     begins on Sunday and a leap year that can be turned off need **a preference of the system** —a key of `cfg` that
//     `calendar` reads, beside the one of the time zone— and their test (docs/60);
//   - **the eraser** (docs/90): modals of warning that a person has to answer, a bar of progress while it erases and one
//     that suggests restarting when it ends. `ui.dialog` and `ui.progress` are there; **what is not** is a way to empty
//     the NVS —the HAL erases one key (`IKeyValue::erase`) and `/nvs` is read-only and not an app's ground—, a format of
//     a volume (`IStorage` has no such call) and `sys.reboot`, so the last modal can only suggest it (docs/70, docs/90);
//   - **and the two of them are apps and not apps of prova**: the lunokhod of 0.169.4 is the picture of the ones that
//     exist to measure the system, and an app a person uses draws its own (docs/40).
//
// 0.170.0 (9/11): **the hour of the person is chosen on a map of the world, and the calendar has the two settings a
// person asks for**. The owner left the spec of the app —*«vull un mapa del mon per seleccionar la zona… al tancar
// l'app es guarda la zona horària (no durant l'us)… afegeix un botó per sincronitzar l'ntp… un checkbox per activar
// l'any de traspás… un desplegable per indicar en quin dia de la setmana comença»*— and asked for the calls it needs.
//
//   - **`time.leap([on])` and `time.week([day])`** are the two new calls of `time` (docs/30): the leap day of February
//     —`true`/`false` or the words `on`/`off`— and the day a week begins on —`1` Monday, the value this system was
//     born with, or `7` Sunday, or the words `monday`/`sunday`—, both read with no argument, written with one, **saved
//     at once** like the offset of the hour and refused with `E_INVAL` when the value is neither. They are `cfg`'s
//     (`leap`, `week`, written in the store as the words a person reads with `nvs`) and the prompt takes the same two:
//     `cfg leap on|off` and `cfg week monday|sunday`, which is what makes an app and a person say the same thing;
//   - **the settings are the calendar's, and there is one of each**: `calendar::days_in_month` answers 28 for February
//     in every year when the leap day is off, and **the weekday of a date does not move** —the 29th of February 2024 is
//     a Thursday with the day and without it, because the sky is not a preference— and `ui.month` puts the first of the
//     month in the column of the day the week begins with, turning its seven names with it. An app writes the names of
//     a week that begins on Monday and the glass says the week the person asked for (docs/30, docs/60, docs/90);
//   - **`apps/tz`**: the map of the world (500 x 250, a public-domain land layer of Natural Earth drawn into
//     `res/world.png`, 6 219 B), the zones marked as a graph —a band above the map and one below with a line at every
//     hour, the chosen hour filled in both so the land is never hidden—, the sentence `Selected timezone: +01:00`, the
//     button of `net.ntp` (which turns the leap day on when the hour comes from a server, the owner's rule), the
//     checkbox and the list of the week. **A tap says what the hour is and saves nothing**: `time.tz` writes at once, so
//     the page keeps the choice and calls it in `on_close`, and the log says `tz: saved +03:00` when it does. It needs
//     a glass and a finger, has its own icon —the globe with a hand, a normal app and not one of prova— and the Paper
//     carries it (docs/40, docs/85);
//   - **and the tests**: `test_calendar` takes the leap day and the week as settings, `test_apps` runs the page, puts a
//     finger on the band of +3, turns the two settings with a tap and reads `tz: saved +03:00` and `settings: tz
//     +03:00` after three taps of the centre — **551 cases and none of them is red** (docs/80);
//   - **and the numbers**: `apps/` is **42 files and 331 536 B**, `build/disk/m5-paper` **61 files and 2 020 187 B**,
//     `build/disk/emulator` **64 files and 2 024 449 B**, and the ATOM's firmware **2 268 577 B** of flash and
//     **122 736 B** of RAM (README, docs/70, docs/80, docs/85).
//
// 0.170.1 (9/11): **totes les apps porten icona, i és una norma i no un costum**. The owner: *«fes una norma, totes les
// apps han de tenir icona»*.
//
//   - **the rule**: an app of this project declares `icon` in its manifest and the picture is beside it —a PNG of 94 x 94
//     made with `tools/make_icon.py` from `res/icon-source.png`, or from the shared lunokhod when the app is one of the
//     ones that exist to measure the system— because the launcher draws that file in every cell of its grid, and an app
//     without one is a cell with the first letter of its name in a box: `home: <id> has no picture` (docs/40);
//   - **the rule is checked where it is cheap**: `test_every_app_of_the_repository_carries_an_icon` walks `apps/` with
//     the disk of the PC and, for every `manifest.json`, asks for the field, that the path stays inside the app's folder,
//     and that the file exists with the eight bytes of a PNG and something after them. A new app without a picture turns
//     the suite red the day it is created, which is the day it costs nothing to fix; a case that only walked the folder
//     and found it empty would pass without checking anything, and says so instead (docs/40, docs/80);
//   - **and the kernel keeps the field optional**, which is a different thing: an app from outside may arrive without a
//     picture and its cell shows the letter, and what it lacks is the drawing and not the permission. The rule is this
//     project's, and `docs/40`, `docs/45` (what a ported app carries) and `docs/85` (the nine apps of the disk) say so.
//
// 0.170.2 (9/11): **the documents of the calls that came with the map, and the icon that is optional and is always
// there**. The owner asked whether the documents had the new calls of the system, and left a rule of work for the
// pictures: *«l'icona es opcional, però et demano que si generes alguna app o la modifiques, revisa si l'icona no hi es
// per posar-ne una (amb l'estil de les altres)»*.
//
//   - **`time.leap` and `time.week` are in every list that counts the calls** (docs/30): the table of `time`, the
//     paragraph that says what there is today, and the section of its own with the two words each takes and what it
//     does **not** move —the weekday of a date—. And the two settings of the calendar are in the table of «Què hi ha
//     avui» of docs/60 and in the door of `/cfg` of docs/70, with their keys `cfg/leap` and `cfg/week` and the words a
//     person reads with `nvs` (docs/30, docs/60, docs/70);
//   - **and the prompt's list of settings says all six** (README): `cfg keyboard`, `cfg boot`, `cfg home`, `cfg tz`,
//     `cfg leap` and `cfg week`, which is what `help` prints and what a person can change without opening a document
//     (the shell's help has carried them since the day each one arrived);
//   - **the icon is optional and is always there** (docs/40): a manifest without `icon` is accepted by the kernel and its
//     cell shows the first letter of the name, which is what an app from outside may do; **the rule of work is another
//     one**: an app that is created, ported or touched is looked at for its picture, and an app without one gets one **in
//     the style of the others** —448 x 448 of four greys in `res/icon-source.png`, and `tools/make_icon.py` down to the
//     94 x 94 the manifest names— instead of a new shape. The nine apps of `apps/` were checked one by one and all nine
//     answer with a 94 x 94 picture; `luaperf` is the one without a master of its own, because its drawing is the shared
//     lunokhod of `tools/icons/` (docs/40, docs/85).
// 0.171.0 (9/11): **the eraser, and the three doors it needed**. The owner asked for a page that empties the NVS and
// formats the card: *«ambdues opcions ha de mostrar un modal amb un avís que l'usuari ha de respondre afirmativament per
// procedir»*, a bar while it works and, at the end, *«un modal amb la suggerencia de reiniciar»*.
//
//   - **`fs.format(path)` is a call of the system** (docs/30, docs/70): the mount a path lives on, emptied, with how many
//     things went in `{erased}`. On `/nvs` it walks the namespaces and the keys and erases them with `IKeyValue::erase`;
//     on a card it takes every file and every folder and **leaves the FAT as it was**, because what is emptied is the
//     content and not the file system —`f_mkfs` would need an unmount and a remount, and there is no board to prove
//     that— (docs/70). A path that is not the root of a mount is `E_INVAL`; the flash of the system is
//     `E_UNSUPPORTED`, because a door that eats the running system does not open from an app; and a backend that cannot
//     do it says so instead of a silent `true`. **It is the one door that reaches outside an app's ground**, and it is
//     that way on purpose: what a page of an eraser needs is a door with an answer and not a walk of `remove`s from an
//     app (docs/30);
//   - **`IStorage::format` is in the HAL** (`lib/hal`), answered by the emulator's volume —the same walk of a directory,
//     paying a block like every other call— and by the board's card (POSIX over the FATFS mounted in the VFS, with the
//     `opendir`/`unlink`/`rmdir` of the platform) (docs/70, docs/85);
//   - **`sys.reboot([reason])` is in Lua** (docs/30): the `reboot` of the prompt, as one function —the picture of the
//     moment on the glass, the ink waited on, and the board starting again—, with the reason in the log
//     (`[I][APPS] wipe restarts the board: the NVS was erased`). It does not come back, so what has to be saved is saved
//     before it;
//   - **and the app is `apps/wipe`** (docs/40, docs/85): two doors with a label inside each, the card of `ui.dialog` for
//     the warning —**with the safe door lit and the dial on it**, because a finger that lands on a card it did not read
//     must not be the finger that emptied the board—, a `ui.progress` that says `-1` while the call holds the kernel and
//     100 when the answer arrives, and a card that ends offering the restart with `sys.reboot`. **The door of the card is
//     `disabled` when there is no card in the slot** —`fs.mounts` is asked, not `caps.sd`, which says that the board has
//     a slot and not that a card is in it— and the same for a board with no store. Its icon is a bin of four greys in the
//     style of the others, with its `res/icon-source.png`, and the Paper carries it because it is the board of the
//     project with a slot (docs/40);
//   - **a hook of a control cannot wait, and the page of the eraser says it in code**: `on_accept` closes the card and
//     leaves the wish in a variable, and the loop of the app is what erases in the next turn —the `sys.delay` that waits
//     for the bar to be painted cannot run inside the system's call—. It is the old rule of the project taken to the
//     letter: read the action, close the card, and then run it (docs/30);
//   - **and the message of a card was being measured against the page** (docs/30): `dialog_draw` compared the row of the
//     text —which carried the place of the card in the view— with the row of the doors —which is a row of the card—, so a
//     card that was not at the top of the glass had **zero lines of message** and showed only its title. Found with the
//     card of `wipe` on the emulator (9/11) and fixed where it always meant to be measured; the tests of the dialog only
//     read the doors, which is why nothing had caught it (docs/30, docs/90).

// 0.171.1 (9/11): **the flash is a volume too**. The owner asked for one more door on the page of the eraser: *«afegeix
// també el botó de format per littlefs a l'app wipe»*.
//
//   - **`fs.format('/littlefs')` empties the disk the system lives on**, with the same contract as the card: file by file
//     and folder by folder, **and the superblock stays**, because what is emptied is the content and not the file system.
//     It is the door that leaves the board with the prompt and nothing else —the apps of the system, their pictures and the
//     faces of `/fonts` live there— and the warning of `wipe` says so in those words (docs/30, docs/70);
//   - **and only a volume is a volume** (found while wiring it): `/data`, `/apps`, `/res` and `/fonts` are rows of the same
//     flash, and `IStorage::format` takes no path, o sigui que `fs.format('/data')` emptied **the whole disk** under the
//     name of a folder. The door now reads the prefix of the mount (`vfs::mount_sub`) and answers `E_INVAL` for a folder of
//     a volume (docs/70);
//   - **the walk of a volume on the board is one function** (`esp32/volume_wipe.h`), shared by the card's backend and the
//     flash's, so the two filesystems —FATFS and littlefs— are emptied by the same code (docs/70, docs/85);
//   - **and the emulator's walk does not follow a link**: its disk is built with `apps/` as a symlink to the repository
//     (`make_disk`), and the walk with `stat` emptied the folder of a real app of this machine —it stopped at the first
//     `rmdir` of a link, which is why only one went—. `lstat` and `unlink` instead, which is what `rm -rf` does (docs/85).
//     On the board the walk keeps `stat`: neither FATFS nor the littlefs of this core has links, and this IDF does not
//     declare `lstat` at all;
//   - **the page is a column of three doors** —store, card, flash— and the flash one is the last, is open on every board
//     and its card says what goes with it. And the test of the page learned a rule of the system the hard way: **three taps
//     of the middle key inside a second and a half are the way out of an app** (`kTapsToClose`), so the case that answers a
//     second card waits 1.7 s before the press that would otherwise close the page (docs/30, docs/85).

// 0.171.2 (9/11): **what a format costs in the model**. The owner asked whether the emulator's format was only emptying a
// directory —it is— and the answer turned up a lie in a comment: `TimedStorage::format` said *«it is not one block either,
// every removal writes the table of blocks»* and paid **one** block for the whole volume.
//
//   - **the price is now one block per thing that goes**, which is what a `remove` costs in the same file, plus one
//     directory read at the start: emptying sixty files is not the same millisecond as emptying two, and the page of the
//     eraser with a full card is the one that shows it (docs/70, docs/85). Measured on the emulator: three entries in
//     77 ms and sixty-one in 181 ms;
//   - **and it is still a walk of the directory and not a `f_mkfs`/`lfs_format`**, on both sides: the volume keeps its FAT
//     and its superblock and what leaves is the content. That is the contract of the call (docs/70) and it did not move.

//
// 0.172.0 (9/11): **a card takes the ring while it is up, and gives it back when it goes**. The owner: *«a l'app wipe veig
// que els botons físics continuen amb el focus de l'app quan hi ha un diàleg modal obert... quan es mostra el modal, es busca a
// l'arbre quina té el focus, li treus i li dones al botó per defecte del modal. Al tancar li tornes el focus al control que hi
// havia»*.
//
//   - **the walk of the focus knows the card** (`ui_collect_targets`): while a `ui.dialog` is alive the stops are the card and
//     what is inside it, and nothing else. A control that is not the card and does not lead to it is not a stop and its children
//     are not walked, so the dial, the finger and the middle key cannot land on a page behind a question nobody answered
//     (docs/30);
//   - **the ring is taken from the control that had it and given to the card**: that control is no longer in the list of stops,
//     so it is found **in the tree by its slot** (`ui_push_slot_from`), its `focused` is put down and the card takes it. The
//     three numbers of the stop it had are kept, and the turn after the card goes the same walk gives them back (docs/30, 90);
//   - **a finger outside the card is a «no»**: a touch that lands on no stop while a card is up is the cancel door, and the app
//     hears `on_cancel` and closes its own card, the same rule the two doors follow (docs/30);
//   - **measured on the bench, 9/11**, a page with a button behind and a card over it: `card up -> behind false card true` and
//     `card closed -> behind true`, with the walk seeing **one** stop where it used to see eleven (docs/30, docs/85);
//   - and three stack mistakes of my own were found by that bench before this was written down —`settop` that dropped the card,
//     `remove(-2)` that took the table of children, and a collection whose objects table was left on the stack so that
//     `ui.present('gc16')` read it as its mode— and each one is a comment where it happened, because the next person to touch
//     this walk will make the same three (docs/90).
//
// 0.172.1 (9/11): **the card of the eraser, taken one by one**. The owner, with the board in his hand: *«a Wipe no sembla que
// pugui picar els botons i el focus fa coses rares. Picar fora del modal tampoc torna a l'app»*.
//
//   - **the ring came back one present late**, and a present that the app did not make after it —the eraser paints once and
//     waits— left the page with no ring anywhere. The question «is the card still there?» is now answered by the kernel with
//     `canvas_pending`: a card the app has **given back** is on its way out and is not a card any more for the focus, so the ring
//     comes back **in the same present** that closes it (docs/30, docs/90);
//   - **and the walk asked for the stops on every present while the card was up**, which put a walk, a focus and the app's
//     `on_focus` hooks inside the app's own draw: that is what the owner saw as *«el focus fa coses rares»*. The check is now
//     «the card is alive and not on its way out» and it costs a number (docs/30);
//   - **measured on the bench, 9/11**: `card up -> behind false card true`, `still up` the same, and `closed (1st present) ->
//     behind true` —the ring is back in the paint that closes the card— (docs/30, docs/85);
//   - **what this does not prove and the board will say**: a finger on the card's doors and a finger outside the card. Both go
//     through `ui_hit` over a list of stops that is now the card alone, and a tap on no stop calls `on_cancel`; the bench has no
//     way to put a finger on the glass, so the next `uploadfs` is the measurement (docs/85).
//
// 0.172.2 (9/11): **the first mark of the time clock can reach the disk on a board**. The owner: *«al polsar Start no veig que
// passi res»* and `timeclock: not saved`.
//
//   - **the cause was a folder without its father**: `fs.data()` is `/data/<app>` and the image of a board carries **no
//     `/data`** until something writes there —the bench's disk has one because of the apps installed in it—, so `fs.mkdir`
//     answered no, `fs.open(path, 'a')` answered no, the app said `not saved` and the page did not change: a key that seems to
//     do nothing (docs/30, docs/70);
//   - **`fs.mkdir` now makes the chain**: it walks the path from the root, asks for every piece and asks for the whole path again
//     at the end, so the answer is still the caller's. Measured on the bench, 9/11: `fs.mkdir('/data/mkbench/deep')` answers
//     `true` and a file written inside it arrives —`build/disk/emulator/data/mkbench/deep/x.txt`—, which is the same call the
//     time clock makes with `fs.data()` (docs/30, docs/70);
//   - **and the two `fopen` errors of that log were not a missing font**: `build/disk/m5-paper/res/fonts/dejavusans-bold.ttf`
//     **is** in the image of the Paper (measured today), so those two lines come from the board running an older filesystem —
//     the same thing the launcher's missing pictures taught on 8/11: `uploadfs` is what carries a change of an app or of `res/`
//     (docs/70, docs/85).
// 0.173.0 (9/11): **the keys of the time clock are marks again, and a face may carry the codes below space**. The
// owner: *«FES LA PUTA APP»*, after the port drew `Start`, `Pause` and `End` where the original drew a play, two bars
// and a square.
//
//   - **where those figures come from, in the original**: they were not a drawing and not a picture —
//     `data/app/Timeclock/keys.pof` was `tools/fontgen.py` run over **`FontAwesome.otf`**, as its README of 27/10
//     says— and the marks were the code points U+F04B play, U+F04C pause, U+F04D stop, U+F06E the eye that opens the
//     week and U+F060 the arrow back (docs/45);
//   - **`tools/make_keys.py` takes those five glyphs out of that font** instead of drawing polygons: the same
//     characters of the same font, CFF outlines turned into quadratic ones, written into
//     `apps/timeclock/res/keys.ttf` —1 500 B, `.notdef` and five glyphs, 1 792 units to the em—, and the tool reads the
//     file back and prints the width and the contours of each mark, because the file is what the app gets (docs/80);
//   - **and they live at the codes 1 to 5 and not at U+F04B**, because a text here is one byte a character: the room
//     no page and no book means is the codes below space, so `font::width` and `font::runs` stop skipping them and let
//     the face answer —a face with no shape for a code answers a gap exactly as before— (docs/30);
//   - **the fallback of the original is kept**: when `ui.load_font('/res/keys.ttf')` answers nil the keys say `Start`,
//     `Pause`/`Resume`, `End` and `Week`/`Day`, because half an app is worse than one that needs reading (docs/45);
//   - **the new case of the suite is the consequence**: `test_font` reads the file the app ships, measures
//     `font::width` of each of the five codes at the five steps the app draws with and the ink `ttf::render` leaves
//     —39 px of play and 29 of the eye, from the contours of the file—, and checks that code 6 and a newline are gaps
//     (docs/80);
//   - **and the reader refused the face, which is why no mark was ever seen**: `ttf::glyph_of` answered "no glyph" for
//     any segment of the character map whose `idDelta` is **zero**, and zero is exactly what a face of five marks
//     writes when the glyph *is* the code (code 1 to glyph 1). The shortcut was meant for the last segment of every
//     map —`0xFFFF`, the code the standard gives no picture to— and the sentinel needs no shortcut: the arithmetic
//     already turns it into glyph zero, which is the answer that line gives. Measured on the bench, 9/11, after the
//     fix: `code 1 -> glyph 1 advance 35`, `2 -> 39`, `3 -> 39`, `4 -> 45`, `5 -> 39`, `6 -> kNoGlyph`;
//   - **the page of the time clock, counted on the dump of the bench** (540x960 `P5`, the app's own frame): the play
//     triangle, the two bars and the square are there, 1 782, 2 306 and 2 536 pixels of level 0 inside the box of each
//     key, and the eye 4 087 in its —the eye focused, so its key is black with the mark in paper— (docs/85);
//   - **and the apps stop naming themselves in the bar**: eight of them wrote `ui.title('About')` and its siblings while
//     the system has already written the `title` of the manifest there before the first line of the app runs, so the
//     call was doing nothing. What is left is what has a reason —the reader's `Library` and the title of the book, the
//     time clock's day and week— (docs/30);
//   - **what this does not prove and the board will say**: the mark on the glass. The suite can read the face and
//     measure it; the title of a button drawn at 45 px of em is a picture only the panel shows (docs/85).
// 0.173.1 (10/11): **the card of 0.172, taken to the glass**. The owner asked for an analysis of the night's commit and the
// analysis turned into three defects of the system, one of them of the feature itself, and one claim that was not true.
//
//   - **the app is told that it lost the ring when a card takes it** (docs/30): the control that had the focus is not a
//     stop while a card is up, so `ui_apply_focus` cannot reach it and the ring was written into its `focused` field
//     without the app hearing anything. `on_focus(self, false)` is the contract of every focusable control and a card is
//     the dial moving: it is called now, and a page that keeps its own idea of the focus —a caret, a key— is not left with
//     the old one under a question (found by the new case of `test_apps`, 10/11);
//   - **and the tap that dismisses a card does not press what was behind it** (docs/30): the panel reports the same finger
//     while it is on the glass, so the report after the cancel found the card gone, the page back and the button under the
//     finger a stop, and the button heard a click that nobody asked for —on the eraser that is a question answered by the
//     tap that closed it—. The contact that cancels is spent (`g_ui_touch_orphan`, the flag this system already had for a
//     control that went away mid-gesture), and it is cleared when the finger goes up (found by the same case);
//   - **the six lines that gave the ring back were written twice** in `ui_collect_targets`, one after the other, and the
//     second could never run because the first clears `g_modal_was_up`: a copy left behind and a reader's trap;
//   - **the chain of `fs.mkdir` of 0.172.2 was not needed and is gone**: `fs_ops::mkdir` has created the fathers inside the
//     storage with `make_dirs` since the import, and the case of the time clock works without the Lua-level walk —measured
//     on the bench with the code of 0.171.2 on a flash with no `data/`: an app made `/data/<id>` and wrote in it, so what the
//     board saw was the older image of its flash, as the second bullet of that same entry already said—. The behaviour is
//     now pinned by `test_a_folder_is_made_with_the_fathers_it_needs`, so nobody has to «fix» it twice;
//   - **and `docs/30` no longer says that an app has to call `ui.focus(card)`**: that was true until 0.172.0 and it is not
//     since —the card takes the walk, the dial, the finger and the ring by itself—, and the rule the owner asked for on the
//     eraser had **no paragraph** in that document although the entry of 0.172.0 cited it. The dialog section has it now,
//     with the outside tap and the contact that is spent (docs/30, docs/90);
//   - `apps/wipe` stops asking for the focus of its two cards and says why; `tools/make_keys.py` says five marks where it
//     said four; and the numbers of the documents are the ones of today —560 cases, 47 files of `apps/`, both disks and the
//     ATOM— (docs/70, docs/80, docs/85, README).

// 0.173.2 (10/11): **the card that goes away takes its ink with it**. The owner: *«sembla que els modals al tancar-se no
// netejen correctament la pantalla»*, and he was right: the frame and the words of a card stayed on the glass for ever.
//
//   - **why**: a card is a control **without a buffer** —a card costs no pixels on purpose— so its ink went into the pixels of
//     the page, nobody kept a copy of what was underneath, and the release only marked the rectangle. The page was never
//     painted again and the ghost stayed (measured on the emulator, 10/11, with a page of a label and a button);
//   - **the hole is paper before it is anything else**: `request_release` fills the box of the control with **the paper of the
//     page** —the level of the last `ui.clear` of the app, white if it never cleared, and reset to white when the view is
//     handed over— and adds it to the painter's damage with `gl16`, because a `du` over a frame of ink keeps the ghost
//     (docs/30, docs/50);
//   - **and every control that was inside it paints itself again**: with a buffer of its own (its whole box is composed into
//     the page) or without one (its `on_draw` paints the box, and a label fills its own paper and a button paints its frame).
//     A page of controls —`wipe`, and any app of `ui.label`/`ui.button`/`ui.canvas`— comes back **whole and by itself**;
//   - **the shape is the card's and not a rectangle**: a card paints a rounded box of radius twelve and its four corners are
//     the page's, so the hole is drawn with the same radius. The system had no way to know a control is a card, so `ui.dialog`
//     says it when it makes one (`mark_card`, one bool per canvas) (docs/30, docs/92);
//   - **what the system cannot give back is what the app painted with primitives** (`ui.text`, `ui.rect`…): that is not a
//     control and cannot draw itself, so a page painted by hand is painted again by the app when its card goes —the recipe
//     the editor already used and that docs/30 did not say anywhere—. The decision is written down in docs/90 with its price:
//     a copy of the 49 400 bytes under a 380 × 260 card would make it clean with no help from the app, and it is not done
//     because the board without PSRAM does not pay for it (docs/75, docs/90);
//   - **and the case that proves it** counts the pixels of the glass before the card and after it: the page of a label and a
//     button comes back the same, the dark square the app painted at the card's corner survives where the radius does not
//     reach, and the part of a primitive that the card covered is paper —which is the rule, said out loud so that the day it
//     changes it changes on purpose— (0.173.2, `test_apps`).

// 0.173.3 (10/11): **the page that paints itself paints its own hole**. One question from the owner over the release of
// 0.173.2: *«I si no refresques la vista sencera i simplement marques com abruta la zona on hi havía el modal? l'app hauria
// de repintar la seva area dañada, no?»*. He was right, and the mechanism was already there.
//
//   - **`ui.present` calls the draw of the view**: the app's `on_draw` runs on every present, cut at the box the page owes
//     —the same `begin_draw`/`end_draw` a control gets—, so a page with a draw of its own brings back its primitives with
//     **nothing new in the bridge**. What was missing was for the system to stop painting paper on top of it;
//   - **`Canvas::paints_itself`** is one bool the bridge sets when the app gives a canvas an `on_draw` (`mark_paints_itself`,
//     next to `mark_card`), and `request_release` **skips the wash of the hole** for the painter that has it. The hole is
//     still owed to the page —`invalidate_covered` and `invalidate_rect` run for both paths— and the app repaints it, which is
//     the exact answer: no guessing with the paper level and no rounded box, because the app paints what it painted before;
//   - **what the wash cost**: two regions over the same box —the `gl16` of the paper and the `du` of the app's draw do not
//     merge—, o sigui dues passades de tinta per a un forat que el dibuix de l'app tapava tot seguit. On the glass the picture
//     was the same; on a panel it is a flash of the whole card for nothing;
//   - **the paper is the fallback and it stays** for the pages without a draw of their own —`wipe` is one, and its cards land
//     on labels and buttons that paint themselves—, with the radius of the card and the controls asked to draw again
//     (0.173.2 unchanged);
//   - **two cases**: `test_a_page_that_paints_itself_keeps_the_hole_for_the_app` (`test_ui`) holds the flag itself —no region
//     of the wash, the page untouched, and one region instead of two when the app paints—, and it fails with the wash forced
//     back in; `test_a_page_that_paints_itself_paints_its_own_hole` (`test_apps`) runs an app whose whole page is one `rect`
//     under the card and counts the glass: the dark square comes back, and the draw of the view ran again. `563 cases`;
//   - **and docs/30 says the two ways**, with the recipe (an app that paints by hand gives its view an `on_draw` and does
//     nothing else when a modal goes); docs/90 writes the decision down: the 49 400 B copy of the pixels is no longer on the
//     list, because a page with a draw of its own comes back without any copy (docs/30, docs/75, docs/90).

// 0.174.0 (10/11): **the page of the time zone uses the system's controls, and the dial comes back to it**. The owner,
// with the page on the emulator: *«el desplegable que hi ha a TimeZone amb "Week stats" no te cap estètica que mostri que
// es una llista desplegable? per altra banda, el botó de sync NTP tampoc sembla un botó de sistema... son controls UI o
// uses alguna altra cosa? el focus sembla no funcionar amb ells»*. All four were true and all four had one cause.
//
//   - **they were not controls**: `ui.frame`, `ui.rect`, `ui.line` and `ui.text` with a hit test of the app, so there was
//     nothing focusable on the page and the ring had nowhere to land. `apps/tz` now builds a `ui.button` with its `title`
//     and `primary`, a `ui.checkbox` with its label inside it and a `ui.selector`; the list of the two days is a `ui.menu`
//     of the system under the field. **The map stays primitives** —it is a picture and a band of fifteen degrees and the
//     finger is its only reader— and the app's own hit test is only the map's;
//   - **and the dial is the system's again**, which is the rule of a page with controls: the two sides walk the ring and
//     the centre is Enter, so the hour is chosen with the finger (the app needs a touch panel to run at all) and the three
//     settings are reached with the dial. **The three taps of the centre are three activations now** and not a way out,
//     which is what the pinch of the glass is for (docs/30);
//   - **`net.ntp()` never answered an app** (a real bug of the bridge since the call exists): `net_progress` settles the
//     promise the registry holds in `kNetKey` and `l_net_ntp` was the only one of the seven calls of the network that did
//     not put it there, so an app's `net.ntp():await()` waited for ever while the shell's `ntp` answered —the shell waits
//     on the native waiter and not on this promise—. Found by the case of the `tz` page, fixed in one line and held by
//     `test_net_ntp_answers_the_app_and_not_only_the_shell`, which fails without it (docs/30, docs/85);
//   - **a refusal is a raise and not a `nil`** (the contract of every promise of this system), so the button of a board
//     with no network saved —the commonest board there is— took the app down with it; the request is now inside a `pcall`
//     and each code says its own sentence to a person (`no network saved`, `the server did not answer`, `the radio is
//     busy`). The click is a hook and a hook cannot wait, so the button leaves the wish and the loop of the page does the
//     request —the rule a card's accept already follows—; while it waits the button is `disabled` and the request has a
//     timeout of eight seconds instead of the twenty of the system's default (docs/30, docs/90);
//   - **two cases and a sentence the tests can read**: the case of `tz` now presses the button, the box and the door with
//     a finger, walks the ring with the right key and presses the box with the centre, opens the menu and picks the second
//     option, and closes the app with a close of the shell —the three taps do not close it any more—; `run tz zones` also
//     prints one `tz stop true` per setting, so a page whose controls the dial could not reach is a red case and not a
//     person's afternoon (`564 cases`).

// 0.174.1 (10/11): **`/fonts` is a door of three places, and the viewer that shows what is in it**. The owner:
// *«/fonts ha de ser com /apps ha de juntar /littlefs/fonts i /sd/fonts. En cas de el que veu l'app també suma el seu
// /apps/<id>/fonts»*, and then *«fes una app visualitzadora de fonts. Que obri tots formats suportats per el sistema i
// mostri la font»*.
//
//   - **the door**: `/fonts` was an alias of one folder of the disk (`res/fonts/`) and it is now the same shape as
//     `/apps` —the app's own `apps/<id>/fonts` first, the flash's `res/fonts/` after it and the card's `fonts/` last, and
//     a name two places carry is presented twice with the `*` mark of `/apps` on the card's copy—. **Nothing moved on the
//     disk**: `ui::boot` and every document keep naming `/fonts/dejavusans-bold.ttf` (docs/30, docs/70);
//   - **the card's folder is made at boot**, like the folder of the apps: a card without `fonts/` is a card the system
//     makes room on, and a door that opened only when the folder was already there would be a door that stays shut until
//     the next boot (the lesson of 0.88.2, docs/70);
//   - **an app's folder is the app's**: the door asks the kernel who is reading (`vfs::set_fonts_app(&luabind::app_id)`,
//     one hook and no copy), so the prompt's `ls /fonts` shows the faces of the board and an app also sees its own. An app
//     that carries a copy of a system face wins over it for its own eyes and the disk's listing does not show it twice;
//   - **and `apps/fonts` is the viewer**: the shelf asks the kernel to load every file of `/fonts` once —**the four
//     readers: `.lfont`, `.bdf`, `.pcf` and TrueType**— and says what each one is and how many bytes it takes, and a file
//     that is not a face says the reason instead of disappearing. A finger on a row opens the specimen: the name, what the
//     kernel made of it —outlines or a cell of bits, how many codes, a line of how many pixels— and the same words drawn
//     with that face at the sizes it has. **A face of bits stays at its size and at doubles of it** (a 6x12 cell blown up
//     four times is a blob and not a bigger letter) and a face of outlines is drawn at every size, which is the whole
//     difference between the two families and is said on the glass (docs/30, docs/92);
//   - **the disk carries one face of each format** so the four readers are seen on the board and not only here:
//     `sample-8x8.bdf` (862 B), `sample-8x8.pcf` (968 B) and `dejavusansmono-6x12.lfont` (9 017 B, from
//     `tools/ttf_to_lfont.py` at a body of ten pixels) beside the six of DejaVu (docs/30);
//   - **two cases**: `test_the_door_of_the_fonts_puts_the_flash_and_the_card_together` holds the two disks, the mark and
//     the resolution of both copies, and `test_the_font_viewer_opens_every_format_the_system_reads` runs the app, reads
//     the four formats off the console, presses a row with a finger, opens its specimen and comes back by the button.
//     Both fail without the door —measured by mutating it— (`566 cases`).

// 0.174.2 (10/11): **the foot of a list is the paginator of the system, and it sits under the last row**. The owner,
// looking at the viewer of the faces: *«a la part d'abaix has posat un pager? perqué ocupa tant espai i es veu tant
// petit? aquest paginador es del sistema?»*. It was, and it had two real faults.
//
//   - **it was a drawing of its own**: four private dots of four pixels of radius, while this system already had the
//     paginator of the paper project (`ui.dots`, 0.112.0 —balls of 19 and 25, a ring of two pixels, the rod that threads
//     them and the three balls that shrink when the pages do not fit—). **`paint_dots` is now one function of a box and a
//     ball**: `ui.dots` calls it with the design's ball and the foot of a list with the ball its band holds, and every
//     other number —the ring, the rod, the air— is a fraction of that ball. One idea and one place, and the small copy of
//     it is gone (docs/30, docs/92);
//   - **it was painted at the bottom of the box and not under the rows**: with a box of 588 px and seven rows of 72, the
//     60 pixels of air fell **between** the last row and its foot, which reads as a pager that takes half a page. The foot
//     is drawn **where the last row of the page ends**, and the paper that is left is under it, where a page ends. To know
//     where that is, `list_draw` takes **the rows the page holds** (`rows`) beside the ones it paints (`count`): a list of
//     entries paints none —every entry paints itself— and the foot has to fall under the last one anyway (docs/90);
//   - **and the band is a little taller for a small row**: a third of the row with a floor of 24 px (it was 20) and a
//     ceiling of 37, the box the design asks for. With rows of 72 it costs no row and the ball goes from 8 to 20 pixels,
//     which is what makes it readable from a metre away;
//   - **the number is still the fallback**: a band too narrow to draw one ball says `2 / 4` and nothing else, which is
//     what a box of a few pixels gets;
//   - `about` and the viewer of the faces are the two pages that show it, and
//     `test_the_foot_of_a_list_is_the_paginator_of_the_system` holds both halves —the open page is a disc and the others
//     are rings, and the foot falls under the last row— which is red with the code of 0.174.1, measured by mutating it
//     (`567 cases`).

// 0.174.3 (10/11): **the list of a page takes the room the panel has**, and the two pages that have one say so. The
// owner, one question after the other: *«i perqué el control list no ocupa tota la vista fins abaix?»*.
//
//   - **why**: the app was giving the list a height measured by hand —the view, less the lines of the head and a couple of
//     rows of slack— and **a panel gives a child exactly the size it has unless the child says `weight`**. A guessed height
//     is wrong twice: it leaves paper at the bottom of the glass, and the rows of the last page stop where the guess said
//     instead of where the page ends (docs/30, docs/90);
//   - **`apps/fonts` and `apps/about` put `weight = 1` on their list**: the box ends at the bottom margin and the rows fill
//     it. The viewer of the faces goes from two pages to one —ten files fit in a page that is the whole glass— and `about`
//     from three pages to four pages of eight rows, which is what filling a page means (docs/30);
//   - **and the arithmetic of the head goes with the guess**: `about` keeps no `motto_lines`, no `summary_lines` and no
//     `used`, because a number that existed to size a child by hand is a number that the panel now knows better. The line
//     the case reads (`about: N rows in a list of M px`) says the box the list ended up with, which is the truthful number
//     and not the one the app asked for;
//   - nothing of the system changed for this: `weight` is the field the panel has had since 0.70.0 and this is the second
//     page of this tree that uses it after the `spring` of the detail of `about` (`567 cases`, and the three suites of the
//     apps are green).
//
// 0.174.4 (10/11): **the glass is asked at the rate it can have news, and not at the rate the kernel turns**. The owner:
// *«al loop principal hi ha algun tipus de delay? … es fa algun pas innecesari … consultar l'estat a cada dibuixat i que
// no sigui necessari?»*. The answer was yes, one, and it was the touch: the chip was read once per turn.
//
//   - **why**: the GT911 scans the panel by itself every ten milliseconds and keeps the frame of its last scan until a
//     read takes it, so a turn of a millisecond that reads again can only be told what the last read already said. The
//     emulator has counted those reads as `missed` since 0.24.0 —«a read that is free here would make a caller that
//     reads ten times a turn look reasonable»— and on the board each one is a status byte on a hundred-kilohertz bus it
//     shares with the clock, the FRAM and the SHT30: a turn of a busy app was 80 % bus (docs/50, docs/95);
//   - **`ITouch::scan_ms()`** is the contract: how often this glass can have news, zero for one that does not say. The
//     GT911 answers ten milliseconds (`kGt911ScanMs`) and the emulator's panel its own `--touch-scan`; `touch::poll()`
//     asks the chip at most once per scan and keeps doing every turn what is not the chip —the gestures, the quiet timer
//     of three scans, the controls and the keyboard of the system—, so a finger held is still a finger and a tap still
//     lands;
//   - **and the driver stops writing when it has nothing to say**: `read()` released the frame on every read, three
//     bytes of a hundred-kilohertz bus to tell a chip that it has no frame that the frame is taken. Both vendor drivers
//     (M5GFX and `esp_lcd_touch_gt911`) clear the status only inside bit 7, and now this one does too: a read with
//     nothing on the glass is `400 µs` and not `800` (docs/50, docs/85, docs/95);
//   - **`touch.read` in `stats` counts reads of the chip and not turns** (`meter::Scope` moved inside the gate), so the
//     number a person compares with the board is the reads, which are now ten per second and not one per turn;
//   - **what the audit found and left alone**, because it is not waste: `ui::idle()` asks the panel twice only on a turn
//     that was owed a paint (`&&` short-circuits, and the second answer is the true one *after* the paint started), and
//     `kernel::poll()` already gates it on `ui::owes_paint()`; `bar::poll()` is two comparisons until the minute, the
//     charge or the title change; `arm_nearest_alarm()` is a compare against the value it last wrote; `net::poll()` is a
//     comparison when the queue is empty; the two `wait_paint`s of `It8951::flush` and its five-millisecond poll are the
//     maker's sequence (docs/50); and the twenty milliseconds of `kAppIdleMs` are the cadence of the board and not a
//     sleep in it (`567 + 1 cases`, and the suites of the touch, the apps and the ui are green).

// 0.174.5 (10/11): **a view is as many as the memory gives, and not eight**. The owner: *«al grid no es carreguen les
// icones Shell, Timeclock, Time zone, Wipe»*.
//
//   - **why**: `kMaxCanvases = 8` was the last number of `ui.h` that the memory could not say —the header itself says a
//     few lines above that the controls are what the memory holds and not a number in the file (0.102.0)—, and the
//     launcher makes one `ui.canvas` of 72 x 72 per app. With eleven apps the eighth came back `nil`, `add_cell` skipped
//     it in silence and four cells of the grid were a name with no picture: exactly the four the owner named, and the
//     seven paints of the page in the log said so (docs/30, docs/90);
//   - **`kMaxCanvases` is gone**: the buffers are the memory's, the allocator refuses what does not fit, and `ui.canvas`
//     answers `nil, E_NOMEM` when it does. A page may hold as many views as there is room for —the launcher had eleven
//     pictures of 72 x 72, which is 28 KB— and nothing of this header decides it any more (docs/75);
//   - **and the launcher says it** when a picture has no box, like it already said when a manifest cannot be read: a grid
//     with four icons missing and no line about it is a page that hides what the memory answered (docs/30);
//   - the case of the suite that pinned the eight is now the other way round —eleven views of a grid at once, and a
//     canvas given back is one to take— (`103 cases`, and the suite of the ui is green; the emulator's page of the home
//     has its eleven icons, checked on the dump, 10/11).

// 0.174.6 (10/11): **the window of the emulator is up before the board is**. The owner: *«l'emulador obre la pantalla eink
// molt tard … no es veu l'splash del boot … per quin motiu hi ha crides a un dispositiu que no s'ha "inicialitzat"?»*.
//
//   - **why**: the ink model, the window and the pump of the window were put in place **after** `kernel::boot` —and the
//     boot is the thing that paints the splash—, so every paint of the boot went to a glass nobody was looking at and the
//     window opened on the page of the launcher. The log of the owner said it plainly: eight `paint` lines of the boot
//     before the line that opens the window (docs/85);
//   - **the three go before the boot, and before the curve of the boot too** (`boot_us`, 637 ms on this model): the
//     window is white —which is what a panel with nothing written on it is—, the splash arrives in it with its `gc16`
//     blinking, and the page of the launcher comes behind, which is the order of the board (docs/85);
//   - **the pump wired before the boot is what makes the splash move**: `MemorySurface::wait_and_pump` asks the window
//     every ten milliseconds of a paint's bus and of its ink, and the splash is eight hundred milliseconds of bus —before
//     this version that wait was walked with nobody to show it to;
//   - nothing of the board changed: `--gui` already refused `--no-delay` and `--no-ink`, so a window only ever exists
//     where the ink and the times are paid (docs/85).

// 0.174.7 (10/11): **the turn of Lua is short, and it is a setting**. The owner: *«fer més curt el temps de lua per torn
// (menys instruccions) perquè la responsabilitat de tàctil, pantalla i botons sigui més alta … vull un sistema que
// respongui ràpid als botons i el touch. Afegeix un paràmetre a la comanda cfg»*.
//
//   - **the budget is the latency of the board**, and nobody had said it in those words: a turn of the app that reaches
//     its end is a turn in which the glass, the keys and the radio were not read. With 100 000 instructions and the 60 ns
//     of the model that is **6 ms** between two looks; the build ships **20 000** (ten blocks, 1,2 ms) from now on
//     (docs/20, docs/75);
//   - **`cfg turn [<n>]`**: the number in force and the default of the build, from **2 000** (one block: below it the
//     counter would never see the budget and no app would ever be cut) to **200 000** (12 ms: past it the loop is the
//     app's and the kernel looks once in a while). It is applied at once, kept in the store as the decimal text a person
//     reads with `nvs` (`cfg/turn`), and put back at boot by `cfg` — which is the module that keeps choices and never the
//     one that counts them: the count is `luabind`'s (docs/30, docs/70);
//   - **`sys.turn()`** is the door for an app, and it is what makes `luaperf` honest: the app used to carry a copy of
//     `kTurnBudgetInstructions` and say it had to be changed by hand; it asks now, so the report it prints is of the
//     slice the board is really using (docs/30, docs/95);
//   - the number in force is the **block ceiling** of what was asked —the hook counts in blocks of two thousand— so
//     `cfg turn 20001` leaves 22 000 and both `cfg turn` and `sys.turn()` say the number of truth (docs/20);
//   - **and the verdict of `luaperf` compares with the hand and not with the glass**: it said a 6,6 ms turn was "nothing
//     next to the 120 ms of a page" and that is the comparison that kept the budget long; it compares with the ten
//     milliseconds a person does not notice now (docs/95).

// 0.174.8 (10/11): **the numbers of this board have a real seed**. The owner: *«afegir alguna font de random per poder
// tenir una llavor aleatòria … a la documentació d'Espressif diuen que per agafar una llavor aleatòria s'ha d'usar la
// wifi. És així?»*.
//
//   - **the question, answered**: the header of `esp_random()` in the IDF this build links says it —*«if Wi-Fi or
//     Bluetooth are enabled, this function returns true random numbers»*— and the reason is the chip: on this family the
//     hardware RNG has no register of its own, it is read out of the **Wi-Fi peripheral** (`WDEV_RND_REG`, 0x60035144)
//     and what stirs it is RF noise. With the radio off it can hand back the same word for ever. But **Wi-Fi is not
//     required**: the same IDF ships `bootloader_random_enable()` —«enable an entropy source for RNG if RF is disabled»,
//     the SAR ADC's internal noise— documented for use from the application early in the run, and to be let go before
//     the ADC, the RF or the I2S is initialised (docs/50);
//   - **so the band has a window of eight bytes at boot**: `pal::init()` enables it, fills eight bytes, disables it,
//     mixes them with `esp_timer_get_time()` and steps a xorshift32 that is what `ISystem::random()` answers. The window
//     is at the boot and not at the first call because the Paper's battery gauge is an ADC and the documents are clear
//     about the order (docs/50);
//   - **and `math.random` was the same sequence on every boot**: Lua seeds itself with `time(NULL)` —zero until the hour
//     is set, and this system keeps its own clock— and the address of the state, which is the same every time the heap is
//     the same. `open_libraries()` now calls `math.randomseed(a, b)` with two words of the band, so every boot has its
//     own numbers; an app that wants to reproduce a run still calls `math.randomseed(n)` itself (docs/20);
//   - **`--seed <n>` of the emulator does what it always promised** and `pal::set_random_seed(0)` is "this machine's
//     clock and process": a run can be repeated on the PC, which is what a case that draws a page needs. On a board the
//     call is nothing, because a firmware whose sequences can be made to repeat is a firmware whose keys can be guessed
//     (docs/20, docs/85).

// 0.174.9 (10/11): **the card in the slot of the emulator carries every app too**. The owner: *«fes que l'emulador
// tingui totes les apps»*, and the card was the one place it did not: `emulator/sd/apps` had a single `editor`, and it was
// a **copy** from the 9/11, so the code that ran from the card was not the code of the repository.
//
//   - **`custom_card_apps = emulator/sd`** in the emulator's environment, read by `tools/make_disk.py`: one **link** per
//     app of `apps/` inside the card's `apps/`, the same rule the flash disk has had since 8/11 —one source, nothing to
//     go stale, an edit read by the next run— and it replaces a copy of an app the repository also has, which is exactly
//     the `editor` that was there (docs/80, docs/85);
//   - **and it touches nothing else**: the card is somebody's data, so an app only the card has —the one `repo install`
//     put there— stays, and so do `cfg/`, `http/` and `fonts/`. That is the difference between seeding a card and
//     assembling an image (docs/70);
//   - **what it looks like**: `apps` answers **22 names**, the eleven of the flash and the eleven of the card with the `*`
//     and the `[card]` word (`*about  About-SD  [card]`), and the launcher has **21 cells in two pages** —it does not list
//     itself— with the pictures of both. That is the case the door of `apps` was built for in 0.164.0, finally on the disk
//     a person looks at (docs/85);
//   - nothing of the system changed: the door, the `*` and the `[card]` word were already there, and this is the data
//     under them.

// 0.175.0 (10/11): **the foot of a grid is the paginator of the system, and a finger turns its pages**. The owner: *«que
// es 1 / 2 que hi ha sota el grid? usa el paginador Dots i el gest swipe left/right per canviar de pàgina. Usa sempre
// controls UI del sistema, no inventis controls o dibuixs controls falsos»*.
//
//   - **the `N / M` and its two marks are gone**: the foot of `ui.icon_grid` was the page in words and two three-pixel
//     rectangles drawn to look like arrows —the third paginator of this file, invented inside the control— and it is now
//     `paint_dots`, the one drawing that the `ui.dots` control and the foot of a list already share since 0.174.2: the balls
//     of the paper project threaded on a rod, the page that is read bigger, the lines of marks that close a window that does
//     not fit. The words are still the answer of a grid too narrow to draw a strip, which is what a list does (docs/30,
//     docs/92);
//   - **a finger that walks sideways turns the page**: forty pixels and not the twelve of a caret, one page per contact and
//     never a pick —the cell the finger ends over is not pressed—, and **the ring goes to the first cell of the page**,
//     because the page of a grid follows the cursor: a page turned with the cursor left behind is a page whose dial is
//     nowhere. Left is the page after and right the one before, like a sheet of paper, and there is no wrap: the first page
//     back and the last one forward are nothing (docs/30, docs/90);
//   - **the arithmetic is the grid's and not the bridge's**: `ui::icon_grid_page_cell` answers the first cell of the page
//     `delta` pages away with the same `icon_grid_cells` the paint, the finger and the foot use, so a swipe cannot land on a
//     page the control would not draw. The bridge finds the grid by its **slot** —a cell's father, and the tree when the
//     finger is on the grid itself, which is not a stop of the dial— and moves the cursor to that cell (docs/30, docs/75);
//   - **every stop of a page turn costs one paint**: the present is `gl16`, which cleans without the flash of a `gc16`,
//     and the launcher's grid said so by itself when its cells took the ring (`paint 20,8 500x884 gl16`, measured on the
//     window, 10/11) (`571 cases`, and the case of the swipe is one of them).

// 0.176.0 (10/11): **one paginator, one size, one ink**. The owner: *«per qué és tant gris el paginador? no és un
// UIControl? per qué cada cop que n'uses un és diferent? mira a tot el codi i busca tots els paginadors que hi ha. Perqué
// es veuen diferent? El paginador ha de ser igual a tot arreu, si no no s'entén que és un control paginador»*.
//
// **The search, and it found three**: `ui::paint_dots` (the one drawing since 0.174.2, used by the `ui.dots` control and
// the foot of a list), the foot of a grid —which in 0.175.0 was given `paint_dots` but with the ink of a secondary word
// and a band of its own— and **the strip of the EPUB reader**, a canvas the app drew itself with balls of twelve pixels,
// a gap of six and a window of fourteen: a second paginator, with other numbers, in Lua. The three looked different
// because they were three.
//
//   - **the ink is the control's** (`color`/`text`), not `dim`: the foot of a grid was grey while the same strip under a
//     list was black and the `ui.dots` control was black (docs/30, docs/92);
//   - **the box is the design's, 37, in all three**: a list gave the strip a third of the row clamped to 24..37 —a ball
//     of 20 with a row of 56— and a grid 28. `list_footer` is now the design's box plus the four rows of the line and its
//     air, and the strip of a list lands with the same twelve rows of air under it as the one of a grid and the one of a
//     `ui.dots` (docs/30, docs/92 §27);
//   - **the line that closes the page is the rows' or the foot's and never both**: a list of data has no separator under
//     its last row of the page, so the foot draws it; a list of entries brings its own, and the foot draws none —a rule
//     there was a second line one pixel under the first (docs/90);
//   - **the reader of EPUB is a `ui.dots`**: `pages`, `current`, `level` and `background`, and the window of fourteen
//     balls is the control's now. Its strip was the only paginator of this tree that was not the system's (docs/46 C5);
//   - **and `ui.dots` with one page paints its paper**: a control that paints nothing paints nothing over what it had, and
//     the reader of a one-page chapter found the balls of the chapter before on the glass (docs/30, docs/90);
//   - the cases of the suite that pinned the old numbers are the other way round: the grid's foot is ink and not the dim
//     tone, and the list's foot is a ball of twenty-five and a ring of nineteen (`103 cases`, and the suites are green).

// 0.176.1 (10/11): **the case of the swipe reads the panel it swipes on**. The whole suite found it red, and the fault was
// the case and not the system: it booted the Paper with `display` on and **`touch` off** —a model without a finger, the same
// as `--no-touch`—, so the kernel had no panel, no frame ever reached the controls and the two swipes did nothing. The
// other case of the suite that touches the glass says `opts.touch = true`; this one forgot it, and a case that presses a
// panel that is not there is a case that passes for the wrong reason (`570 cases`, the whole suite green).

// 0.178.3 (11/11): **one question, two answers, and the paint's answer is the one that stands**. A grid can be a grid of
// names (`items`, painted by the kernel) or a grid of controls the app built (`children`, each cell painting itself), and
// `docs/30` says which wins when it has both —«si la graella té fills, la cel·la és el fill i el control només pinta el
// paper de la pàgina i el peu»—. The paint decided it with `children > 0` (`l_icon_grid_draw`), the damage of a ring
// decided it with `items > 0` (`ui_invalidate_grid_ring`), and a grid with both would have painted the app's cells and
// owed the damage of the kernel's: a rectangle of the geometry of nine names over a page of nine pictures. The ring takes
// the paint's rule now, one line and a comment, and the step of a page already counted the cells of the tree
// (`ui_grid_turn_the_page`), so the three of them agree (docs/30, docs/90);
//   - **measured**: `test/test_ui` and `test/test_touch` green, which are the suites of the control and of the glass; the
//     case of the ring (`test_a_ring_of_a_grid_damages_the_cells_it_crosses`) keeps passing, and no suite has a case with
//     `items` and children at once, which is why the disagreement lived unseen.

// 0.178.2 (11/11): **a paragraph of `docs/30` that a reader could not read**. The owner asked, after the sweep of the day
// before, whether anything of the documentation still had to change, and one thing did: the section of `ui.icon_grid` with
// entries —written on the 7/11 in 0.105.0— was **wrapped at eighty columns of bytes and not of letters**, so it had broken
// words at the end of nine lines: «un co / ntrol», «una min / iatura», «el pi / ntat», «el cont / rol», «les cel / ·les»,
// «les pà / gines», «les entrad / es» and «Fins a 0 / .104.0», and Markdown joins two lines with a space, so the rendered
// page of the reference read «un co ntrol de l'app» (docs/30). The paragraph is the same text, wrapped at the width of its
// neighbours, and a scan of every document with the vocabulary of the documents themselves finds no other word cut in two.

// 0.178.1 (10/11): **the documents, read against the code one claim at a time**. The owner asked for a review of
//   every document with the code open beside it, and the sweep found the numbers that a week of repairs had left
//   behind —not a feature, so no line of code moved except one comment that had stopped being true—:
//   - **the panel of the emulator is not a chip that keeps quiet** (`docs/85`): the row of `--no-touch` said «el panell
//     hi és i no contesta», and `IPlatform::touch()` answers `nullptr` when the option is off (`hal_emulator.cpp`), so
//     the board of that run **has no chip at all** and the kernel does not even announce the capability. The same row
//     still priced a turn at eight hundred microseconds of touch, which is the read of before 0.174.4: it is **four
//     hundred**, and it is paid **once per scan** and not once per turn;
//   - **the band of the foot of a list is forty-one** (`docs/90`, `docs/30`): the documents of 0.174.2 still said «un
//     terç de la fila amb un mínim de 24 i un màxim de 37» and «la bola escalada a la banda», and `list_footer()`
//     ignores the row since 0.176.0 and returns `kDotDesignH + kFootRule`. The same paragraph of `docs/30` mixed three
//     units —a floor of 24 for the list, a band of 28 for the grid, a ball of 25 for `ui.dots`— and now says which is
//     which, because the ball of a list was 20 to 33 and that is the whole reason the owner wanted one size;
//   - **the ceiling of eight canvases is gone everywhere it was quoted** (`docs/30`, `docs/75`): «només vuit poden
//     tenir vista pròpia» survived the 0.174.5 that took `kMaxCanvases` out, and it is the exact sentence that made
//     four cells of the launcher paint no icon —the eighth `ui.canvas` answered `nil`—. The documents say the ceiling
//     is the table of the extra memory now, and that the launcher **says in the console why** a picture has no box;
//   - **the verdict of `luaperf` is two lines** (`docs/95`): the capture of 0.169.3 still ended with «the budget is
//     nothing next to the glass: do not touch it», which the app stopped printing in 0.174.7 when the comparison became
//     the hand and not the glass. The block is marked as the capture of its day and the lines of today are written
//     beside it, and the two passages that said a hundred thousand instructions in the present tense say «the one
//     hundred thousand of before»;
//   - **and the read of the glass that a test writes** (`docs/80`): the row of `test/test_touch` priced the idle read
//     at eight hundred microseconds too, and the probe of the boot and the 10 ms of the scan were already right;
//   - **the one comment corrected**: the foot of a list drew the ball with `kDotBig` and its comment still explained
//     that «a foot of twenty-four gives a ball of twenty» —a truth of 0.174.2, and the opposite of what the code does
//     since 0.176.0—. A comment that says the opposite of the line under it is worse than no comment (`ui.cpp`);
//   - **what was checked and needed nothing**: the window before the board, the card with every app, the entropy of the
//     chip and `--seed`, `cfg turn` and `sys.turn()`, the once-per-scan read of the GT911, the two cells of a ring and
//     the map of damage, `ui.invert`, the swipe of a grid, and the uniformity of the paginator —each of them already
//     had its paragraph, in the document and with the number the code gives—.

// 0.177.0 (10/11): **the drop-down of a menu is a line of words and not a stack of keys**. An option was drawn with the
// function that draws a key, so every row of a menu came out with a face lifted four pixels over a shadow, a frame of its
// own and the tag in a box of air: three raised keys inside a card, where the reference of this system draws one card and a
// line per entry.
//
//   - **a row of a menu was drawn with `paint_button`** since 0.97.0 —"an option of a menu is a button"—, so every entry
//     came out with a face lifted four pixels over a shadow, its own frame and the tag in a box of air at the left: three
//     raised keys inside a card. The reference of this system draws the other thing (`OLD/PaperOS/src/ui/menu_bar.cpp`,
//     `MenuBar::draw()`): one card and, on it, a line per entry, the one the dial is on a **band of ink from edge to edge**
//     with the word turned over, and a separator as a line (docs/92);
//   - **the row is now `paint_row`**, the function the list already uses: the word on the paper, the band of ink for the
//     row of the dial, and **`tag` at the right in the `dim` ink** —which was a parameter of `menu_draw` that went
//     nowhere— as the shortcut of a row of a menu is read everywhere (docs/30);
//   - **`primary` is a hairline now**: the frame of three was the frame of a key and a row has no face to wear it on, so
//     the only thing left of that signal is one pixel of the dim ink (docs/90);
//   - **and the case of `test_ui` was measuring the keys**: it counted the rows of the frame of three and asked the row
//     of the dial to be a face of ink inside its box. It now measures the band —its two edges, and the fact that the row
//     above it is paper with no frame at all— (docs/80);
//   - **what this does not prove**: the light of the glass. The band and the hairline are measured on the emulator, and
//     whether a menu reads better as lines than as keys is a question the page answers.
// 0.177.1 (10/11): **a face that a page draws with lives as long as the page**. Found reviewing the code of the delta
// since 0.173.0, in the app that shows the faces (`apps/fonts`).
//
//   - **the specimen loaded its face and never gave it back**: the comment beside it said it did, and `give_back()`
//     released the labels and nothing else. `kMaxFaces` is three, so the fourth specimen was answered «there is no room
//     for another face» —and the console said it, which is how the leak stayed quiet in the log only— (docs/30, docs/75);
//   - **and the collector was worse than the leak**: a control keeps the **number** of its face and not the object, so a
//     face that Lua collected left every label of the page drawing with the face of the system, with no line anywhere;
//   - **the face is of the page now** (`specimen_face`): loaded once, kept while the specimen is on the glass, and given
//     back **after** the controls, because `face_of()` answers the face of the system for a slot that is gone. Going back
//     to the shelf goes through `give_back()` too, so the slot is freed on every way out (docs/75);
//   - **and its case is the consequence**: `test_apps` runs the app, opens **four** specimens one after another, comes
//     back to the shelf between them, and asks that each one says `fonts: open` and that no line says `fonts: cannot
//     open` —which is what the fourth one did before this version, with `kMaxFaces` at three—. Written and **not run yet**:
//     a run of this suite is asked for before it happens, as every run of it does (docs/80).
// 0.177.2 (10/11): **the workflow of CI was red on every push, and its second red was a board without DRAM**. Two causes,
// one under the other: a list of include directories that had gone stale, and 2 400 bytes of DRAM that one board held for
// the whole run of the program.
//
//   - **the first red was a list of include directories written by hand**: the step of `clang -Wunreachable-code` had the
//     libs of the day it was written, and `lib/kernel/include/kernel/ui.h` has asked for `epub/page.h` since the reader
//     arrived —`ui.h` is included by `ui.cpp`, `shell.cpp`, `button.cpp`, `touch.cpp` and `kernel.cpp`—, so the step failed
//     on **every** run with `fatal error: 'epub/page.h' file not found`, in 30 to 50 seconds, before the suite and before
//     the four builds. It was reproduced here with the same command and the same error before it was touched. The list is
//     now derived from `lib/*/include` minus the rule of the emulator's `lib_ignore` (the board identities and the ESP32
//     halves), so a new library cannot be forgotten by it again, and the step passes on the ten directories it checks;
//   - **the second red was under the first one**: `m5-atom-matrix` did not link —`region 'dram0_0_seg' overflowed by 584
//     bytes`—, and it was not the workflow's fault. Measured with `xtensa-esp32-elf-size` over the two builds of the ATOM:
//     `.bss` of `hal_atom.cpp.o` was 304 B for the ATOM and 2 680 for the Matrix, the whole difference in one symbol
//     (`g_platform`, 0x130 against 0xA78): the strip of the Matrix is twenty-five lights and the driver kept **24 entries
//     of four bytes per light** of the frame on the wire as a member, 2 400 B of `DRAM` reserved for ever (docs/50, docs/75);
//   - **and the frame is the heap's now** (`ensure_wire()`, `lib/hal_esp32/include/esp32/ws2812.h`): taken the first time
//     a frame has to go out, with `MALLOC_CAP_DMA`, and said out loud if the memory is not there. It is what the header of
//     that file already promised —the biggest board is not paid for by the smallest— and it is what puts the Matrix back
//     inside its segment: **RAM 122 768 B of 327 680 (37,5 %)** against the 122 704 of the ATOM, and the five environments
//     of this build green: emulator 7,9 s, atom 54,5 s, atom_matrix 4,2 s, paper 61,8 s (123 676 B de RAM, 2 365 385 B de
//     flash) and esp32-devkit 76,6 s;
//   - **and the two actions whose majors moved** were updated (`checkout@v5`, `setup-python@v6`), because the run of every
//     push printed the warning of Node 20 on the three of them;
//   - **measured, after all of it**: the 571 cases of the native suite green in 2 min 43 s, the workflow's own clang step
//     green over the ten source directories, and the four builds the workflow runs green here. What is not proven is the
//     runner itself: the run of this commit is the first one that will reach the suite and the builds at all.
// 0.177.3 (10/11): **a move of the ring of a grid painted the page instead of the two cells it crosses**. The law of this
// system is that a control repaints what changed and nothing else, and a grid broke it on its most common gesture: the
// dial walking through the cells. Three roads asked for the frame of the control —the step of the dial, the walk giving
// the grid the focus and the walk taking it away—, and the frame of a grid is the whole page of pictures.
//
//   - **one call of the kernel says what the move costs** (`ui::icon_grid_ring`): the cell that wore the ring and the cell
//     that wears it now, from 1 and 0 for no ring, and a flag that says whether those two numbers are a **cursor that
//     walked** or a **ring that came or went** on the cell the cursor does not leave. The answer is **true when the glass
//     owes nothing more** than the damage it put down —the rectangle the two cells span, whole cells, because the paper of a
//     grid covers the air between them and a paint cut in the middle of an icon would tear it— and false when the caller has
//     to repaint the control whole, which is a cursor that walked onto another page (the cells are laid out again and the
//     footer says another number) and a grid with no geometry to measure;
//   - **and a grid of entries owes nothing at all**: its cells are controls of the app (`ui.grid_entry`), each one paints
//     its own ring, so what moves on the paper of the grid is nothing —and an app that invalidated the grid on every cell
//     of its focus was painting nine pictures for one ring, which is what `apps/home_eink` did (`grid.index = index;
//     grid:invalidate()` in the hook of a cell)—. The field of a grid asks the same call, so that line is free now, and the
//     whole control comes back by itself when the page does turn;
//   - **the numbers of the case** (`test_ui`, `test_a_ring_of_a_grid_damages_the_cells_it_crosses`): a grid of six cells of
//     a hundred by a hundred and two in a frame of three hundred by three hundred, the ring going from the first cell to
//     the second. Before: 1 region of **300×300 = 90 000 px²**. Now: 1 region of **200×102 = 20 400 px²**, 4,4 times less,
//     and **the two cells the ring did not cross keep a pixel of a tone the grid never paints**, which is the consequence
//     and not a number of it. **Measured**: the 572 cases of the native suite green in 2 min 40 s —104 of `test_ui`, 197 of
//     `test_apps`— (docs/80);
//   - **and the suite found a fault of the first cut, which is why the numbers above are not a guess**: a cursor that
//     **appears** on another page was not a page turn. `grid.index = 5` on a grid that had no cursor compared the page of
//     the cell with itself —both numbers were the same cell— and answered «nothing changed», so the cells were never laid
//     out again and the cell stayed off the frame: `test_apps` said `back 0 240` where the case asks `back 0 0`
//     (`test_a_grid_of_entries_places_cells_of_controls`). The two numbers are not the same question in the two cases, so
//     `cursor_moved` says which one it is: a cursor that walked compares the two pages —and no cursor is the page of the
//     first cell—, and a ring that comes or goes gives the same cell twice and asks nothing of the pages (docs/30, docs/90);
//   - **and the workflow was two lists away from being right** (the question of whether `ci.yml` was as it should be, and
//     it was not): `ttgo-t-koala` is an environment of `platformio.ini` like the other five and **this job never built it**,
//     so the header said «the five environments» while `docs/80` says six; and the files the `clang` step looks at were the
//     ten libraries of the day they were written, which left `epub`, `zip` and `hal` —three the host builds— outside the
//     one check that sees an unreachable scope, exactly the trap the list of include directories had. Both are derived from
//     `lib/` now, the six bands are built, and the five files this step had never seen were run through it here before the
//     commit with no diagnostic at all (docs/80);
//   - **what is not done and is said out loud**: the focus frame of a list is still the whole box, because the frame is four
//     bands and a control draws with one rectangle of damage and not four. The route is there —the frame is the only thing
//     a focus changes in a list and its rows are painted identically— and it is not taken in this version. And the six
//     builds of the boards are not run in this version either: they are what the workflow does on every push, the table of
//     the ops of the kernel grew by one pointer, which is the same size on every one of them, and a build is not a measure
//     of this change.
// 0.177.4 (10/11): **the map of what the glass owes, and the three invalidations that were not a change**. A control
// repaints what it touches and nothing else is the law of this system, and the launcher of the glass was breaking it in
// two places that had nothing to do with each other.
//
//   - **the walk invalidated the control the dial left three times**: `focused`, `editing` and `focus_index`, and the
//     last two only because a field the app had never written was answered as a change —a cell has no `editing` and is
//     not a strip, and writing `false` and `0` over nothing took the whole subtree of the cell through `invalidate` for
//     a ring that was leaving—. A field with no value that is given the default is not a change now
//     (`ui_set_field_bool`, `ui_set_field_number`), and it is still written, so the app reads what it read before;
//   - **and the pieces of two cells that do not touch were joined into the band they span**: `damage_add` joined any two
//     regions of one quality within two pixels, and the union of two rectangles that only share a corner is a rectangle
//     that also covers what nobody changed. The cell of the last column and the cell of the first of the row below came
//     out as one `500x246` —both rows of the grid— for two cells of `250x123`. A join may add a quarter of the smaller
//     piece and no more now, which keeps what is worth an update less —the letters of a word, the two halves of a
//     frame— and refuses the bands;
//   - **and a piece the map already owes is not added again**: a picture's ink asked for `du` and the box of the cell
//     that holds it for `gl16`, so both were recorded and the picture was painted **after** the cell that already had
//     it. The map is one rectangle per piece of ink now, with the strongest quality of the two when they meet;
//   - **measured on the launcher, three steps of the dial** (`test_apps`,
//     `test_a_step_of_the_dial_in_the_launcher_paints_the_cells_it_crosses`): **277 968 px² in nine paints** before and
//     **184 500 px² in four** after, and no region wider than the two cells the dial crossed. The case adds up the
//     pixels of every `paint` line of the console and refuses a band of two rows; it is red with the join of the first
//     cut —246 000 px²— as it has to be;
//   - **what a step of the launcher still costs and cannot cost less**: a cell of a grid that takes the ring is drawn
//     **inverted** —paper and ink swapped, the same signal a button gives— so its whole box changes colour and the whole
//     box is what the glass owes. What the law asks is that it be that cell and no other, and that is what it is now.
//   - **measured again**: 573 cases, 24 suites, all green in 2 min 42 s.
// 0.178.0 (10/11): **a box turned over, and the icon of a launcher goes with its cell**. The selected cell of this system is
// a black block and not a grey, and it said so by filling its own paper with ink; what the app had put inside it stayed on
// paper, so an icon came out as a **white box** in the middle of the block. The old project turned the whole selected cell
// over with one call (`invertRoundRect`) —and its own notes say the bitmap door could not be used on a selected cell
// because «the door paints the paper of the drawing too (15)»— and that is why its icons looked as if they had no box at
// all: the paper of the picture became the black of the cell.
//
//   - **`ui.invert(x, y, w, h [, mode])` and `cv:invert(…)`**, new: every level of a box swapped with its opposite, 0 with
//     15 and 5 with 10, cut at what the control owes like every other primitive and with one region of damage. It is a
//     primitive and not a mode of a paint because what is turned over is **what is already there** —the paper of a
//     picture, its greys and the letter between them— (docs/30, docs/92);
//   - **and the launcher is the first app that uses it**: the canvas of a picture is a child of the cell, and a canvas
//     without `on_draw` composes the same pixels it holds, so the cell could say `__inverts` —and did— and the picture
//     would not hear it. The picture has an `on_draw` now: it paints the letter and the file, and when the second argument
//     of the draw says it is on ink it turns the whole box over. The frame of the placeholder is gone with it, because what
//     says «this app has no picture» is the letter and the console line, and the box was a second signal for the same
//     thing that the old launcher never had (docs/30, docs/45);
//   - **measured**: 575 cases, 24 suites, all green in 2 min 44 s. The two new ones are the box turned over (`test_ui`: the
//     0 becomes 15, the 5 becomes 10, the box the control owes is respected and twice is once) and the picture of a cell
//     following the face of its cell in **pixels of the page** (`test_apps`: ink when the dial is on it and paper when the
//     dial leaves), and the second is red —`Expected 0 Was 15`— when the walk stops telling a child which face it draws
//     on, which is the fault it exists for;
//   - **and what it is not**: a picture that carries its own transparency is still not used here —the icons of the disk are
//     8-bit grayscale with no alpha and no `tRNS`, and `tools/make_icon.py` composes the transparency over white on
//     purpose, like the tool of the old project—. The turn-over is what gives the old look, and it needs no new file.
static const char kVersion[] = "0.178.3";

}  // namespace kernel

#endif  // LUNOK_KERNEL_VERSION_H
