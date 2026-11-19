// Who this firmware is for.
//
// Every board carries a header with exactly this name, with the same names inside
// and its own content, and platformio.ini puts the right one on the include path
// (-Ilib/board_<id>/include). Nothing in the project writes #ifdef for a board:
// whoever needs to know asks the HAL, and the HAL answers from here (docs/50).
// This header includes nothing else on purpose, so any translation unit of the
// board can use it.
//
// The LilyGO T5 E-Paper S3 Pro (LilyGO board `H752-01`; the repository calls it
// T5S3-4.7-e-paper-PRO) is **not an ESP32**: it is an **ESP32-S3-WROOM-1** with 16 MB of
// flash and 8 MB of OPI PSRAM, and that is the first board of this project whose chip is not
// the D0WDQ6 of the others. The pin map is the one the vendor publishes (`docs/pinmap.md` of
// the board's repository), and what this board has that no other has is:
//
//   - a **4.7 inch ED047TC1 with no controller**: 960 x 540, sixteen levels, and the chip
//     drives the panel itself through an 8-bit parallel bus (D0-D7, CKH, STV, CKV, STH, LE).
//     Its supply comes from a **TPS65185** (I2C 0x68) gated by a **PCA9535** (0x20), which is
//     the other half of what this board's HAL has to do (docs/50, docs/90);
//   - the same **GT911** touch controller as the Paper (0x5D), on other pins;
//   - a **PCF8563** (0x51) with a coin cell, which is register-compatible with the BM8563 the
//     Paper carries —the driver of `lib/hal_esp32` serves both— (docs/50);
//   - a **BQ27220** gauge (0x55) and a **BQ25896** charger (0x6B) instead of a divider on an ADC;
//   - a card slot on an SPI bus of its own, a SX1262 LoRa and a GPS module on UART0's pins,
//     none of which this system has a driver for yet.
#ifndef BOARD_IDENTITY_H
#define BOARD_IDENTITY_H

#include <stdint.h>

// **Whether this build carries a glass** (0.179.9): **yes**. The panel of this board is an ED047TC1 with no controller
// and the driver is `lib/hal_t5s3/src/epd47.cpp` —the parallel bus, the supply of the TPS65185, the frame in the PSRAM
// and the panel's own waveform—, o sigui que la pàgina, els controls, les cares, el lector i la barra hi són. **El que
// costa**: una actualització és la forma d'ona sencera, trenta-vuit passades de tot el vidre, i per això aquesta placa
// no anima res i pinta quan la pàgina ha de quedar quieta (docs/50, docs/95).
#define LUNOK_GLASS 1

namespace board {

// The name this board has in platformio.ini, letter for letter (`board = ...`).
// A Lua app reads it as sys.board and picks its hardware path with it (docs/30).
inline const char* id() { return "lilygo-t5s3-epaper-pro"; }

// The chip family, which is what an app means when its manifest says it runs on
// "esp32": every board of the family accepts that app, and a board that names a chip
// this one does not have is not an app of this board (docs/40). **L'S3 és de la família** i
// no un nom a part: el que un manifest vol dir és «una placa amb aquest sistema i aquest
// maquinari», i una app de llapis i paper no ha de saber quin número porta el xip (docs/40).
inline const char* family() { return "esp32"; }

// This build is for a board and not for the PC emulator.
inline bool emulator() { return false; }

// No light of its own: what it has is a screen (docs/50).
inline uint32_t led_count() { return 0; }
inline uint32_t led_max() { return 0; }

// No inertial sensor on this board.
inline bool has_imu() { return false; }

}  // namespace board

#endif  // BOARD_IDENTITY_H
