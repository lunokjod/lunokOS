// **La forma d'ona amb què el fabricant pinta setze grisos en aquesta placa, copiada tal com és** (0.190.55). It is
// `u8_103Grays` of the board's own repository, `examples/FastEPD/grayscale_test`, the sketch that installs it with
// `setCustomMatrix()` and then fills the screen with the sixteen levels ("Set a custom gray matrix for this panel. This
// determines how the shades of gray look in 4-bpp mode"). **No és cap dibuix meu ni cap número trobat a cops de porta**: és la
// seva taula, amb la seva atribució —FastEPD és de BitBank Software, Inc., sota Apache License 2.0— i sense tocar-ne ni una
// xifra.
//
// Setze files (el nivell de destí, 0 negre a 15 blanc) i **divuit passades**, i cada valor és què fa aquella passada a aquell
// píxel: `0` res, `1` cap al negre, `2` cap al blanc. Fixa-t'hi que **les dues tintes hi són dins de cada fila**: no és un camí
// d'anada i tornada com els que vaig inventar, sinó una seqüència amb empentes en tots dos sentits, que és el que separa setze
// nivells amb divuit escombrades.
//
// **I la manera de donar-li temps a la tinta també és la seva** (vegeu `scan_a_pass` a `epd47.cpp`): la porta de la fila està
// oberta **durant tota la transferència**, no en una finestra comptada, i el que la tanca és el pols de `LE` de la fila
// següent. Qualsevol finestra que hi posis a mà és una invenció que et menges el contrast.
#ifndef LUNOK_T5S3_EPD47_WAVEFORM_H
#define LUNOK_T5S3_EPD47_WAVEFORM_H

#include <stdint.h>

namespace hal {
namespace epd47 {

// **Divuit escorregudes del vidre per pintar setze grisos** (docs/50).
const uint8_t kGrayPhases = 18;
// **Els dos codis i el neutre**, que és el que fa servir la neteja de debò del fabricant: `0x55` és tots els píxels cap al negre,
// `0xaa` tots cap al blanc i `0x00` cap enlloc (`bbepClear` de FastEPD) (docs/50).
const uint8_t kCodeNeutral = 0;
const uint8_t kCodeBlack = 1;
const uint8_t kCodeWhite = 2;
// **I la neteja que fa el seu `fullUpdate()` abans de la taula** (0.190.56): `CLEAR_SLOW` —quatre passades cap al negre, vuit cap
// al blanc, quatre cap al negre i vuit cap al blanc— i una de neutra. **Això és el que fa que la taula de setze per divuit
// funcioni**: la seva matriu dona codi comptant des d'un paper blanc, i sense aquesta neteja el que hi hagi al vidre es queda a
// sota i la pàgina surt bruta i fosca (docs/50, docs/95).
const uint8_t kClearDarkPasses = 4;
const uint8_t kClearLightPasses = 8;

// **I la curta, que és d'aquest projecte i no del panell** (0.190.38): a `du` —the fast mode, two levels— does not need
// eighteen scans. What is below is **not** the panel's data and does not pretend to be: it is the shortest shape that moves
// ink to a rail and leaves it there, two phases towards the opposite rail (the reset that keeps the ghost down) and four
// towards the target, o sigui sis passades en lloc de divuit. El preu és el de sempre en tinta: un `du` deixa fantasma i el
// `gc16` del nucli —que sí que fa servir la taula del panell— el neteja (docs/50, docs/95).
const uint8_t kFastPhases = 6;
// Vers el negre: primer un empenta cap al blanc i després cap al negre.
const uint8_t kFastToBlack[kFastPhases] = {2, 2, 1, 1, 1, 1};
// I vers el blanc, al revés.
const uint8_t kFastToWhite[kFastPhases] = {1, 1, 2, 2, 2, 2};

// **La taula del fabricant, sense una xifra tocada**: setze nivells per divuit passades (docs/50, docs/95).
static const uint8_t kGrayMatrix[16][kGrayPhases] = {
  0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0,   // 0
  0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 1, 1, 1, 0, 0, 0, 0,   // 1
  0, 0, 0, 0, 0, 1, 1, 1, 2, 1, 1, 1, 2, 1, 0, 0, 0, 0,   // 2
  0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 1, 0, 0, 0, 0, 0,   // 3
  0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 1, 0, 0, 0, 0, 0, 0,   // 4
  0, 0, 0, 0, 0, 1, 0, 0, 1, 2, 0, 1, 0, 0, 0, 0, 0, 0,   // 5
  0, 0, 0, 0, 0, 1, 2, 0, 1, 2, 0, 1, 0, 0, 0, 0, 0, 0,   // 6
  0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 0, 0, 0,   // 7
  0, 0, 0, 0, 0, 0, 0, 1, 2, 2, 1, 2, 1, 0, 0, 0, 0, 0,   // 8
  0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 0, 0, 0,   // 9
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 0, 0, 0,   // 10
  0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 2, 1, 2, 0, 0,   // 11
  0, 0, 0, 0, 0, 1, 1, 1, 2, 1, 2, 0, 0, 0, 0, 0, 0, 0,   // 12
  0, 0, 0, 0, 0, 1, 1, 2, 2, 2, 2, 1, 2, 0, 0, 0, 0, 0,   // 13
  1, 1, 1, 1, 1, 1, 2, 2, 1, 2, 2, 0, 0, 0, 0, 0, 0, 0,   // 14
  0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,   // 15
};

}  // namespace epd47
}  // namespace hal

#endif  // LUNOK_T5S3_EPD47_WAVEFORM_H
