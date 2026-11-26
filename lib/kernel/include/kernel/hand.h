// **La lletra escrita a mà, i el model que aprèn la d'aquesta persona** (docs/30, docs/90): the two models of the handwriting
// keyboard —one for the letters and one for the numbers and symbols—, the glyph the interpreter fills when an app (or the band)
// draws one, and the file both of them leave on the volume.
//
// **Per què dos models i no un** (0.192.71): the letter zone and the symbol zone are two surfaces a finger draws on and two
// alphabets that never mix; a model of its own learns twice as fast with the same examples, and a person can train the letters
// without teaching the digits again.
//
// **El model viu aquí i no a l'app** perquè el teclat és del nucli: la banda que escriu sobre la pàgina no és una app, i el que
// classifica ha de ser el mateix codi que fa servir l'app d'entrenament o el dia que un canviï l'altre no hi serà (docs/30,
// docs/75).
#ifndef LUNOK_KERNEL_HAND_H
#define LUNOK_KERNEL_HAND_H

#include <stdint.h>

#include "ink/ink.h"

namespace kernel {
namespace hand {

const uint8_t kZones = 2;
const uint8_t kLetters = 0;
const uint8_t kSymbols = 1;

// **Quantes mostres hi caben per zona**: 192 × 52 bytes són deu kilobytes per zona, i viuen a la memòria extra quan la primera
// lletra s'escriu i no abans (docs/75). Amb dues zones són una mica més de vint kilobytes, o sigui que un model que no s'ha
// entrenat no costa res.
const uint16_t kCapacity = 192;

// The name of a zone as the interpreter and the file say it: `letters` and `symbols`.
bool zone_from_name(const char* name, uint8_t& out);
const char* zone_name(uint8_t zone);

// **El glif que s'està mirant**: the door of the interpreter fills this one and the service reads it, perquè el dibuix d'una
// lletra no pot viure a la pila del torn —són tres kilobytes i el torn en té vuit— i només n'hi ha un alhora (docs/75).
ink::Glyph& glyph();

// **El que el servei fa amb el glif**: apuntar-lo com a mostra d'un caràcter, preguntar quin caràcter és, i les dues portes del
// fitxer. `add` refusa el que ja hi és i el que no cap; `guess` torna els tres candidats i si el primer és prou segur.
bool add(uint8_t zone, char text);
ink::Answer guess(uint8_t zone);
uint16_t examples(uint8_t zone);
uint16_t examples_of(uint8_t zone, char text);

// **El fitxer**, a `/data/handwrite/letters` i `/data/handwrite/symbols` (docs/70): the app of the training writes them —its own
// folder, which is the only one it may write— and the band reads them. `load` is called by itself the first time a zone is
// asked for something, o sigui que un model desat per l'app és el que la banda fa servir sense cap renaixement.
bool save(uint8_t zone);
bool load(uint8_t zone);
void forget(uint8_t zone);

// **Les portes del servei per a l'intèrpret**: same shape as the other services of the system (docs/30).
struct HandOps {
  bool (*available)();
  ink::Glyph* (*glyph)();
  bool (*add)(uint8_t zone, char text);
  ink::Answer (*guess)(uint8_t zone);
  bool (*save)(uint8_t zone);
  bool (*load)(uint8_t zone);
  uint16_t (*examples)(uint8_t zone);
  uint16_t (*examples_of)(uint8_t zone, char text);
  void (*forget)(uint8_t zone);
};

void install();

}  // namespace hand
}  // namespace kernel

#endif  // LUNOK_KERNEL_HAND_H
