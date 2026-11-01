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
static const char kVersion[] = "0.11.0";

}  // namespace kernel

#endif  // LUNOK_KERNEL_VERSION_H
