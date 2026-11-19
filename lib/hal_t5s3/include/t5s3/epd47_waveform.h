// **La forma d'ona de setze grisos del fabricant, que no és una taula sinó un compte** (0.190.57). It is the sixteen-grey mode
// of the panel's own file, `epd_wp_epdiy_ED047TC1_16_0`: **fifteen frames in which each level gets a push towards black for as
// long as it still needs one** —the level 0 gets all fifteen, the 15 gets none, the 14 gets one— **and only for a pixel that
// comes from white**, which is why the firmware that uses it clears the glass to white first (`epd_clear()` before
// `MODE_GC16`). There is no return pulse anywhere in it.
//
// **I això és el que s'ha de copiar**: la neteja a blanc primer, i després el compte. El que no es pot copiar és la *durada* de
// cada empenta, perquè la del nostre vidre no és la seva: amb la porta oberta tota la transferència —la seva manera— aquesta
// tinta satura a la segona passada i la pàgina queda en tres o quatre colors. Amb una finestra de porta, la mateixa tinta dona
// nou esglaons a microsegon i mig i quinze a nou-cents nanosegons, i per això el camí sencer són **tretze microsegons i mig** de
// porta: quinze empentes de nou-cents nanosegons, que és el que aquest fitxer declara (docs/50, docs/95).
#ifndef LUNOK_T5S3_EPD47_WAVEFORM_H
#define LUNOK_T5S3_EPD47_WAVEFORM_H

#include <stdint.h>

namespace hal {
namespace epd47 {

// **Quinze empentes de blanc a negre**, que són les del mode de setze grisos del panell (docs/50).
const uint8_t kGrayPhases = 15;
// **I els dos codis i el neutre**: `1` cap al negre, `2` cap al blanc, `0` cap enlloc (docs/50).
const uint8_t kCodeNeutral = 0;
const uint8_t kCodeBlack = 1;
const uint8_t kCodeWhite = 2;
// **La neteja que fa el seu `fullUpdate()` abans de la taula** (0.190.56): `CLEAR_SLOW` —quatre passades cap al negre, vuit cap
// al blanc, quatre cap al negre i vuit cap al blanc— i una de neutra, o sigui que el paper queda **blanc**. El mode de setze
// grisos compta des d'un paper blanc i no dona cap codi a un píxel que no vingui d'allà: sense aquesta neteja, el que hi hagués
// al vidre es queda a sota (docs/50, docs/95).
const uint8_t kClearDarkPasses = 4;
const uint8_t kClearLightPasses = 8;
// **I la finestra de porta d'aquest vidre** (0.190.57): tretze microsegons i mig de porta fan el camí sencer de blanc a negre i
// quinze passades les reparteixen en quinze esglaons. És el número que la mesura dona i no el del fabricant, que en gasta
// noranta per al mateix camí (docs/50, docs/95).
const uint32_t kRoadNs = 13500;

// **El codi d'un nivell en una passada**: `1` mentre li quedin empentes, i res després. El nivell 0 les demana totes, el 15 cap.
inline uint8_t gray_code(uint8_t level, uint8_t phase) {
  return (phase < static_cast<uint8_t>(15u - (level & 0x0F))) ? kCodeBlack : kCodeNeutral;
}

// **I la curta, que és d'aquest projecte i no del panell** (0.190.38): a `du` —the fast mode, two levels— does not need
// fifteen scans. What is below is **not** the panel's data and does not pretend to be: it is the shortest shape that moves ink
// to a rail and leaves it there, two phases towards the opposite rail (the reset that keeps the ghost down) and four towards
// the target. El preu és el de sempre en tinta: un `du` deixa fantasma i el `gc16` del nucli —que sí que fa servir el compte
// del panell— el neteja (docs/50, docs/95).
const uint8_t kFastPhases = 6;
// Vers el negre: primer un empenta cap al blanc i després cap al negre.
const uint8_t kFastToBlack[kFastPhases] = {2, 2, 1, 1, 1, 1};
// I vers el blanc, al revés.
const uint8_t kFastToWhite[kFastPhases] = {1, 1, 2, 2, 2, 2};

}  // namespace epd47
}  // namespace hal

#endif  // LUNOK_T5S3_EPD47_WAVEFORM_H
