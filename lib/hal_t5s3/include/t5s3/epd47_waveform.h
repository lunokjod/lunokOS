// **La forma d'ona del fabricant, que es conserva com a document i que aquest vidre no sap separar** (0.190.33, i la
// 0.190.46 la substitueix a la pràctica): 16 rows by 38 columns, and each value is what a
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

// **I la forma d'ona d'aquest projecte, que ara és la del fabricant** (0.190.51). La de `kWaveform` (les trenta-vuit passades
// del FastEPD) no separa els nivells en aquest vidre i una taula feta de *formes* tampoc: el que hi ha dins del panell, per al
// mode de setze grisos, és una altra cosa, i s'ha llegit del seu propi fitxer (`epd_wp_epdiy_ED047TC1_16_0`, quinze passades):
//
//   **cada nivell s'empeny cap al negre un nombre comptat de vegades, i el compte es fa des del blanc.** El nivell 0 rep les
//   quinze passades, el 15 no en rep cap, i el 14 en rep una. Res més: no hi ha empentes de tornada, ni balances, ni trama.
//
// O sigui que **una passada és 1/15 del camí** de blanc a negre, i d'aquí surten els setze nivells. El nostre esquema anterior
// (vuit passades cap al negre i després comptar les de tornada cap al blanc) tenia el compte al revés, i per això `panel bars`
// donava quatre colors tots a l'esquerra: la tornada des del negre és molt més sensible que l'anada, i mitja passada de blanc ja
// era mig camí (docs/50, docs/95).
//
// **El rentat a blanc no és opcional i el fabricant el pot estalviar perquè la seva pàgina ja ho és**: la seva taula només dona
// codi als píxels que venen del blanc (`from` = 15) —el `data[frame][to][from]` és una matriu de transició de 16×16 i les seves
// quinze files només tenen el bit del 15—. Nosaltres no podem suposar res del que hi ha al vidre, o sigui que el rentat es fa, i
// la seva llargada també surt del temps de porta (0.190.52).
const uint8_t kTowardsWhite = 2;
const uint8_t kTowardsBlack = 1;
// **I quant temps de porta demana aquest vidre per fer el camí sencer**, que és el número que la mesura ha trobat (0.190.52).
// El fabricant hi gasta quinze passades de sis microsegons —noranta—, i aquí el camí de blanc a negre s'acaba amb **quatre
// passades de sis**, o sigui vint-i-quatre microsegons: `panel bars` amb la seva forma d'ona donava *«cinc colors i la resta
// tot negre»*, que és exactament una tinta que satura a la quarta empenta. La diferència és del nostre vidre i no del compte:
// el compte del fabricant és el bo i el que canvia és **quant de temps hi ha d'anar cada empenta**, o sigui que en comptes de
// comptar passades es compta **temps de porta** i les passades que hi caben. Així, baixar la porta no aclareix la pàgina: la
// parteix més fi (docs/50, docs/95).
const uint32_t kBlackTravelNs = 24000;
// **I el camí cap al blanc és més curt**, que és el que la mateixa mesura diu: dues empentes de sis ja el feien. El rentat ha
// de ser suficient per saturar, i prou (docs/50).
const uint32_t kWhiteTravelNs = 12000;
// **El sostre, que és el del temps i no el de la física**: una passada és una escombrada del vidre (uns dotze mil·lisegons de
// cable), o sigui que quaranta-vuit ja són més de mig segon i la prova es fa feixuga abans que la tinta digui res (docs/50).
const uint8_t kMostPasses = 48;
const uint8_t kMostWash = 16;
// **Les empentes cap al negre que demana un nivell**: el 0 les demana totes i el 15 cap, i al mig es reparteixen. És l'única
// aritmètica d'aquesta forma d'ona (docs/50, docs/95).
inline uint8_t steps_towards_black(uint8_t level, uint8_t passes) {
  const uint16_t steps = static_cast<uint16_t>(15u - (level & 0x0F)) * passes;
  return static_cast<uint8_t>((steps + 7u) / 15u);
}

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
