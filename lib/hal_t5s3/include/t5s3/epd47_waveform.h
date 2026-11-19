// **La forma d'ona de l'ED047TC1 d'aquesta placa** (0.190.33): 16 rows by 38 columns, and each value is what a
// phase does to a pixel that has to end up at that level —`0` nothing, `1` towards black, `2` towards white—.
//
// It is **the panel's own data and not a drawing of mine**, and where it comes from is part of the file: it is
// `u8GrayMatrix` of **FastEPD**, the library the board's own repository carries (`lib/FastEPD/src/FastEPD.inl`),
// which is published under the **Apache License 2.0 by BitBank Software, Inc.** —the same table the vendor's
// firmware drives with, since its factory example is epdiy's `epd_board_v7`—. It is reproduced here with that
// attribution and under that licence, and it is the one thing of this driver that is not this project's code.
//
// **I la mesura que hi ha a sota és la de l'original** (`// 38 columns by 16 rows. From white (15) to each gray
// (0-black to 15-white) at 20C`): a row is the destination level and the table goes **from white**, o sigui que
// repintar sobre tinta que ja hi és deixa un fantasma i la matriu de 16 × 16 arribarà quan hi hagi la manera de
// recordar què hi havia (docs/50, docs/95).
#ifndef LUNOK_T5S3_EPD47_WAVEFORM_H
#define LUNOK_T5S3_EPD47_WAVEFORM_H

#include <stdint.h>

namespace hal {
namespace epd47 {

// How many phases the table has: the screen is scanned this many times for one update (docs/50).
const uint8_t kPhases = 38;

// **I la curta, que és d'aquest projecte i no del panell** (0.190.38): `kPhases` is thirty-eight scans of the glass for a
// page of sixteen greys, and a `du` —the fast mode, two levels— does not need them. What is below is **not** the panel's
// data and does not pretend to be: it is the shortest shape that moves ink to a rail and leaves it there, a push of two
// phases towards the opposite rail (the reset that keeps the ghost down) and four towards the target, o sigui sis
// passades en lloc de trenta-vuit. El preu és el de sempre en tinta: un `du` deixa fantasma i el `gc16` del nucli —que
// sí que fa servir la taula del panell— el neteja (docs/50, docs/95).
const uint8_t kFastPhases = 6;
// Vers el negre: primer un empenta cap al blanc i després cap al negre.
const uint8_t kFastToBlack[kFastPhases] = {2, 2, 1, 1, 1, 1};
// I vers el blanc, al revés.
const uint8_t kFastToWhite[kFastPhases] = {1, 1, 2, 2, 2, 2};

// **I el paper que la taula del panell suposa** (0.190.42). `kWaveform` és una taula **des de blanc**: the row of a level is
// the shape that takes a pixel **that is white** to that level, and the maker says so in its own note («From white (15) to
// each gray»). Applying it to a pixel that is not white is a pixel that ends somewhere else, and that is what «falten
// tintes» was: the picture of the splash is drawn on a page of its own colour, so half the glass started where the table
// did not expect. Aquestes passades són el **rentat a blanc** que es fa abans de la taula, o sigui que cada píxel que s'ha
// de moure comença el viatge des del lloc on la taula el comença. **No és cap invent**: `2` és el codi que empeny cap al
// blanc, i les vuit passades són les que el propi nivell 0 de la taula gasta per anar de blanc a negre (docs/50, docs/95).
const uint8_t kResetPhases = 8;
const uint8_t kTowardsWhite = 2;

// `kPhases` values per level, 0 black to 15 white.
static const uint8_t kWaveform[16][kPhases] = {
  0, 0, 0, 2, 2, 2, 2, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
  0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
  0, 0, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0,
  0, 0, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
  0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0,
  0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 0,
  0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 0,
  0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 0, 0,
  0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 0,
  0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 0,
  0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0,
  0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0,
  0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 2, 0,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0,
};

}  // namespace epd47
}  // namespace hal

#endif  // LUNOK_T5S3_EPD47_WAVEFORM_H
