// **La lletra que dibuixa un dit, posada en una quadrícula, i el model que aprèn com escriu aquesta persona** (docs/30,
// docs/90): the glyph as the finger drew it, the 7x7 grid the model looks at, and a k-nearest-neighbours classifier over the
// examples the person has drawn —the one that learns in one example and not in a thousand epochs.
//
// **Per què no un perceptró** (0.192.71, la conversa amb l'amo: *«Son els percptrons la millor opció? (l'usuari personalitzarà
// el seu traços, necessitem quelcom que aprengui)»*): a linear classifier over 49 cells is a plane per letter, and `c` and `e`
// are not separated by a plane that survives tomorrow's stroke. With three or five examples per character —what a person
// actually draws in a sitting— comparing against the examples themselves wins, learns at the instant and needs no training
// pass; the perceptron's day comes with hundreds of examples. What this library keeps of the idea is the shape of the answer:
// **one score per character**, a **reject threshold**, and the **three best candidates** instead of one guess.
//
// **La quadrícula és 7x7 i de dos bits** (l'amo: *«potser em quedo curt i cal una matriu mes gran, 7x7?»*): with 25 bits a
// hand-written glyph cannot tell `o` from `0` or `c` from `e` —not because the classifier is weak but because the picture has
// lost the information—, and 49 cells is the smallest grid that keeps a letter a letter. Each cell is 0 (nothing), 1 (the path
// crosses it once) or 2 (it crosses twice, or turns inside it, or two strokes meet there), i un traç gruixut i un de prim que
// diuen la mateixa lletra **no** han de semblar dues lletres diferents: el nivell compta passades i no tinta.
//
// **Res d'aquí reserva memòria**: the caller hands the glyph, the grid and —for the model— the block of examples it owns. És la
// mateixa regla que `lib/rle` i `lib/zip` donen, i aquí encara compta més: el model d'un teclat viu al nucli i el d'una app
// d'entrenament viu a l'app, i tots dos volen el mateix codi (docs/30, docs/75).
#ifndef LUNOK_INK_INK_H
#define LUNOK_INK_INK_H

#include <stddef.h>
#include <stdint.h>

namespace ink {

// ------------------------------------------------------------------ el glif

// **El que un dit dibuixa**: the points of one glyph, in the page's pixels, grouped by stroke. A letter of a second and a
// half at the glass's twenty frames a second is some thirty points per stroke; a crossed `t` or a `ç` is three strokes. The
// ceilings are the caller's memory and not a measurement: a glyph that fills them is a glyph nobody drew.
const uint16_t kMaxPoints = 768;
const uint8_t kMaxStrokes = 6;

struct Point {
  int16_t x;
  int16_t y;
};

struct Glyph {
  Point point[kMaxPoints];
  uint16_t count = 0;
  // Where each stroke starts, in `point`. The first one is always zero.
  uint16_t start[kMaxStrokes] = {0};
  uint8_t strokes = 0;
  bool drawing = false;

  void clear();
  // **Un punt més del traç que s'està dibuixant**: the first one of a stroke opens it, and the finger going up (`lift`) closes
  // it. Answers false when there is no room, which is a glyph nobody drew and not an error to shout about.
  bool add(int16_t x, int16_t y);
  bool lift();
  bool empty() const { return count == 0; }
};

// ------------------------------------------------------------------ la quadrícula

const uint8_t kSide = 7;
const uint8_t kCells = kSide * kSide;

// The three levels of a cell: nothing, crossed once, crossed again (or turned in).
const uint8_t kLevels = 3;
const uint8_t kQuiet = 0;
const uint8_t kThrough = 1;
const uint8_t kTwice = 2;

// **El lloc del dibuix dins del camp no hi és, i és mitja lletra** (0.195.0, l'amo: *«no detecta bé els símbols… VUll que
// m'entengui»*). The box is found in the glyph itself, o sigui que un `-` i un `_` són **literalment la mateixa quadrícula**
// —distància 0, mesurada— i un `.` d'un dit que tremola dos píxels es converteix en una ratlla de set cel·les. `at_x`, `at_y` i
// `size` són el que la caixa centrada llença: on cau el dibuix dins del camp on s'ha fet, i quant n'ocupa. Es guarden en **bandes
// grolleres** i no en precisió, perquè el mateix traç dibuixat un pèl més amunt o una mica més gran ha de seguir essent el mateix
// (docs/30, docs/90).
const uint8_t kWhereUnknown = 0xFF;  // a grid made without a field: no place and no size to count

// **El camp on s'ha dibuixat**: the rectangle the page gave the finger —the paper of the trainer, or the zone of the band— in the
// same coordinates as the points of the glyph. An unknown field (a width or a height of zero) is the old behaviour: the grid is made
// and nothing about the place or the size travels with it (docs/30, docs/90).
struct Field {
  int32_t x = 0;
  int32_t y = 0;
  uint16_t w = 0;
  uint16_t h = 0;
  bool known() const { return w > 0 && h > 0; }
};

struct Grid {
  uint8_t cell[kCells] = {0};
  // **El que la quadrícula no pot dir i sí que és la lletra**: the number of strokes (`l` and `i` are one line and one line
  // and a dot) and the shape of the box (`-`, `|` and `o` are the same ink in three proportions). They travel with the cells
  // and the distance counts them, perquè una `l` i una `i` no es poden separar amb quaranta-nou cel·les.
  uint8_t strokes = 0;
  uint8_t aspect = 0;  // the width over the longest side, in eighths: 8 is square or wider, 1 is a hair
  // Where the ink sits in the field it was drawn in, in eighths of it (0 is the left or the top edge, 8 the right or the
  // bottom), and how much of the field it takes (0 is a jot, 8 fills it). `kWhereUnknown` when the caller has no field.
  uint8_t at_x = kWhereUnknown;
  uint8_t at_y = kWhereUnknown;
  uint8_t size = kWhereUnknown;
  uint8_t inked() const;
};

// **El dibuix, posat a la quadrícula** (docs/90): the box of the glyph is found, the stroke is scaled by **its longest side**
// —so a `-` stays thin and a `1` stays tall instead of both being stretched into squares— and every segment is walked in half
// a cell's steps, counting how many times the path enters each cell. Speed does not count: the steps are geometric and not the
// samples of the finger, o sigui que el mateix traç dibuixat de pressa i a poc a poc dona la mateixa quadrícula.
//
// Answers false when there is nothing to put on the grid.
bool raster(const Glyph& g, Grid& out);

// **Amb el camp on s'ha dibuixat** (0.195.0): the same, and the three numbers of where and how big are filled from the box of the
// field. It is what separates a `-` from a `_` and a `.` from a `-`, i el camp és el requadre on la persona dibuixa —el paper del
// teclat, o la zona de la banda— i no la pàgina sencera (docs/30, docs/90).
bool raster(const Glyph& g, const Field& where, Grid& out);

// The same, from an array of points of one stroke: what a page that has its own path (the trainer) hands over.
bool raster(const Point* points, uint16_t count, Grid& out);
bool raster(const Point* points, uint16_t count, const Field& where, Grid& out);

// **Un toc és un punt** (0.195.0): a glyph made of a single press is a `.` and not a refusal —abans es refusava i el punt no es podia
// escriure—; el que el separa d'una ratlla és la seva `size`, que amb un camp és zero bandes (docs/30).

// ------------------------------------------------------------------ el model

// **El que la resposta d'una lletra és**: the three best characters, each with the distance of its nearest example, and
// whether the first one is far enough from the second to be typed without asking. Learn from the answer: a tap on one of the
// three candidates is an example with a label, i el model millora amb l'ús (l'amo: *«l'usuari personalitzarà el seu traços»*).
const uint8_t kCandidates = 3;
// **Els tres números de la decisió, i d'on surten** (0.192.71): measured on the scale `distance` gives, where the same circle
// drawn at two radii is 0 and two different letters are 25 to 110 apart. A glyph whose nearest example is beyond `kFar` is no
// character of this model —the band says "no" instead of typing a letter nobody drew—, one nearer than `kNear` with a margin of
// `kMargin` over the second is typed without asking, i la resta són la tira de candidats (docs/90).
const uint16_t kFar = 70;
const uint16_t kMargin = 10;
const uint16_t kNear = 20;

struct Example {
  Grid grid;
  char text;
};

struct Guess {
  char text;
  uint16_t distance;
};

struct Answer {
  bool any = false;
  bool confident = false;
  uint8_t count = 0;
  Guess top[kCandidates];
};

// **La distància de dues quadrícules**: the cells first —a level of difference is a level of ink— and then the things the
// cells cannot say: the number of strokes, which weighs the most because it is structure, the shape of the box, i el lloc i la
// mida dins del camp quan les dues quadrícules en tenen (una que no en té no compta en aquests tres, o sigui que un model antic i
// un dibuix sense camp es comparen com sempre) (docs/30, docs/90).
uint16_t distance(const Grid& a, const Grid& b);

// **Els exemples d'una persona, i prou**. The model is a view of a block the caller owns (`examples`, `capacity`) and the
// number in it: no allocation, no file, no training epochs. `add` keeps one copy of each —the same grid with the same label
// twice is one example and not two— and answers false when the block is full or the glyph is empty.
struct Model {
  Example* examples = nullptr;
  uint16_t capacity = 0;
  uint16_t count = 0;

  bool usable() const { return examples != nullptr && capacity > 0; }
  bool add(const Grid& grid, char text);
  Answer guess(const Grid& grid) const;
  // How many examples each character has; the trainer counts with this and the band says which letters it knows.
  uint16_t examples_of(char text) const;
  // **Esborrar les d'un caràcter** (0.192.86): a wrong example is the one thing a person cannot take back any other way, i el
  // model no té identificadors —les mostres d'una lletra són totes les que porten la seva etiqueta i prou—, o sigui que el que
  // es fa és treure-les totes i tornar a escriure les altres a sobre. Torna quantes se n'han anat (docs/30, docs/90).
  uint16_t remove_of(char text);
  void clear() { count = 0; }
};

// ------------------------------------------------------------------ el fitxer

// **El model tal com viu al volum** (docs/70): a header of eight bytes and sixteen per example, o sigui quatre kilobytes per a
// dues-centes cinquanta-sis mostres. The magic says it is a model of this system, the side and the levels say which grid it
// was drawn on —a model of another grid is refused and not read as rubbish— and the count says how much to read.
//
//   `I` `N` `K` `2` · side · levels · count (little endian, two bytes)
//
// and then, per example, thirteen bytes of two-bit cells (four to a byte, the last one with three spare), three of the label,
// the strokes and the aspect, and three more of the place and the size in the field.
//
// **`INK2` i no `INK1`** (0.195.0): a file of sixteen bytes per example read as nineteen is rubbish with a shape, o sigui que el
// nom canvia i els models vells es refusen amb el mateix missatge de sempre —«is not a model of this grid»— en lloc de fer-se servir
// amb números que no hi són (docs/70).
const uint8_t kMagic[4] = {'I', 'N', 'K', '2'};
const size_t kHeaderBytes = 8;
const size_t kExampleBytes = 19;

// What `save` needs, and what a caller budgets before it writes: the header and the examples that are there.
size_t blob_bytes(const Model& model);

// **Escriu i llegeix el model**, with the caller's buffer and its capacity. `save` answers the bytes written or 0 when there is
// no room; `load` fills the model up to its capacity and answers **whether the blob is a model of this grid at all** —a file of
// another version or another quadrícula is refused and the model is left empty—, i quantes mostres hi han entrat ho diu
// `model.count`.
size_t save(const Model& model, uint8_t* out, size_t cap);
bool load(Model& model, const uint8_t* in, size_t len);

}  // namespace ink

#endif  // LUNOK_INK_INK_H
