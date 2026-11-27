// **El glif, la quadrícula i el model que aprèn una mà** (docs/30, docs/90): the three pieces of the handwriting keyboard that
// this system has to get right without a board under it —a glyph drawn with a finger becomes a 7x7 grid that says something, two
// different letters are far apart and two drawings of the same letter are near, a letter nobody taught is refused instead of
// guessed, and a model goes to a file and comes back the same.
//
// **The strokes here are made with arithmetic and not with a finger**: a circle, a `c` that is the same circle with a gap, a
// line, a stick and a stick with a dot. It is enough to say that the pipeline separates what a hand separates; the rest is the
// trainer's accuracy, which is measured on the glass and not here.
#include <math.h>
#include <stddef.h>
#include <string.h>

#include <unity.h>

#include "ink/ink.h"

void setUp(void) {}

void tearDown(void) {}

namespace {

// **Un traç a partir d'una corba**: `from` and `to` in degrees, so a circle and a `c` are the same code with a different gap.
void an_arc(ink::Glyph& g, double cx, double cy, double r, double from, double to, int steps) {
  for (int i = 0; i <= steps; ++i) {
    const double at = from + (to - from) * i / steps;
    g.add(static_cast<int16_t>(cx + r * cos(at * M_PI / 180.0)),
          static_cast<int16_t>(cy + r * sin(at * M_PI / 180.0)));
  }
  g.lift();
}

// A straight line, drawn in twenty-one points like a finger would.
void a_line(ink::Glyph& g, int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
  for (int i = 0; i <= 20; ++i) {
    g.add(static_cast<int16_t>(x0 + (x1 - x0) * i / 20), static_cast<int16_t>(y0 + (y1 - y0) * i / 20));
  }
  g.lift();
}

void a_circle(double r, ink::Grid& out) {
  ink::Glyph g;
  an_arc(g, 150, 150, r, 0, 360, 48);
  TEST_ASSERT_TRUE(ink::raster(g, out));
}

void a_cee(ink::Grid& out) {
  ink::Glyph g;
  an_arc(g, 150, 150, 40, 45, 315, 40);
  TEST_ASSERT_TRUE(ink::raster(g, out));
}

void a_bar(ink::Grid& out) {
  ink::Glyph g;
  a_line(g, 110, 150, 190, 150);
  TEST_ASSERT_TRUE(ink::raster(g, out));
}

void a_stick(ink::Grid& out) {
  ink::Glyph g;
  a_line(g, 150, 110, 150, 190);
  TEST_ASSERT_TRUE(ink::raster(g, out));
}

// **La `i`: la barra i el punt**, which is the pair a 7x7 cannot tell apart from an `l` by its cells alone.
void an_eye(ink::Grid& out) {
  ink::Glyph g;
  a_line(g, 150, 120, 150, 190);
  g.add(150, 105);
  g.lift();
  TEST_ASSERT_TRUE(ink::raster(g, out));
}

void a_zigzag(ink::Grid& out) {
  ink::Glyph g;
  a_line(g, 110, 120, 190, 130);
  a_line(g, 190, 130, 110, 180);
  a_line(g, 110, 180, 190, 190);
  TEST_ASSERT_TRUE(ink::raster(g, out));
}

// **El paper on es dibuixa**: four hundred by three hundred pixels, que és el que una pàgina li dona a un dit (docs/30).
ink::Field a_paper() {
  ink::Field paper;
  paper.w = 400;
  paper.h = 300;
  return paper;
}

// A block the model looks at, like the one the kernel owns.
ink::Example g_block[64];
ink::Model a_model() {
  ink::Model model;
  model.examples = g_block;
  model.capacity = 64;
  return model;
}

}  // namespace

// **On cau la tinta dins del paper és mitja lletra** (0.195.0, l'amo: *«no detecta bé els símbols… VUll que m'entengui»*): un `-` al
// mig del paper i un `_` al peu són el mateix dibuix —les mateixes cel·les, la mateixa proporció, un traç— o sigui que l'única cosa que
// els pot separar és on han caigut. Sense camp les dues distàncies són zero, que és el que hi havia i el que aquest canvi arregla
// (docs/30, docs/90).
void test_a_dash_and_an_underline_are_two_drawings_when_the_field_is_known(void) {
  const ink::Field paper = a_paper();
  ink::Glyph dash;
  a_line(dash, 110, 150, 190, 150);
  ink::Glyph under;
  a_line(under, 110, 260, 190, 260);
  ink::Grid on_the_middle;
  ink::Grid at_the_bottom;
  TEST_ASSERT_TRUE(ink::raster(dash, paper, on_the_middle));
  TEST_ASSERT_TRUE(ink::raster(under, paper, at_the_bottom));
  // **El dibuix és el mateix**: the difference is not the shape, and that is the whole point.
  for (uint8_t i = 0; i < ink::kCells; ++i) {
    TEST_ASSERT_EQUAL_UINT8(on_the_middle.cell[i], at_the_bottom.cell[i]);
  }
  TEST_ASSERT_EQUAL_UINT8(8, on_the_middle.aspect);
  TEST_ASSERT_EQUAL_UINT8(8, at_the_bottom.aspect);
  // **I el lloc, no**: low in the paper against the middle of it, and far enough that no model confuses them.
  TEST_ASSERT_TRUE(on_the_middle.at_y < at_the_bottom.at_y);
  TEST_ASSERT_GREATER_THAN(ink::kNear, ink::distance(on_the_middle, at_the_bottom));
  // **Sense camp no hi ha res a dir**, que és el comportament de sempre i el que un caller que no el passa encara veu.
  ink::Grid plain_one;
  ink::Grid plain_two;
  TEST_ASSERT_TRUE(ink::raster(dash, plain_one));
  TEST_ASSERT_TRUE(ink::raster(under, plain_two));
  TEST_ASSERT_EQUAL_UINT16(0, ink::distance(plain_one, plain_two));
  TEST_ASSERT_EQUAL_UINT8(ink::kWhereUnknown, plain_one.at_y);
}

// **Un toc és un punt i no una negativa** (0.195.0): a finger that pressed and nothing else used to be refused —«a press is not a
// letter»— i el `.` no es podia escriure. Ara és un glif d'una cel·la, i el que el separa d'una ratlla és quant n'ocupa del paper
// (docs/30, docs/90).
void test_a_tap_is_a_dot_and_not_a_dash(void) {
  const ink::Field paper = a_paper();
  ink::Glyph tap;
  TEST_ASSERT_TRUE(tap.add(150, 150));
  TEST_ASSERT_TRUE(tap.lift());
  ink::Grid dot;
  TEST_ASSERT_TRUE_MESSAGE(ink::raster(tap, paper, dot), "a tap is a dot");
  TEST_ASSERT_EQUAL_UINT8(1, dot.inked());
  ink::Glyph dash;
  a_line(dash, 110, 150, 190, 150);
  ink::Grid line;
  TEST_ASSERT_TRUE(ink::raster(dash, paper, line));
  TEST_ASSERT_TRUE(dot.size < line.size);
  TEST_ASSERT_GREATER_THAN(ink::kNear, ink::distance(dot, line));
}

// **I el mateix traç una mica més amunt és el mateix traç**: the place has to separate a `-` from a `_` and not a stroke from itself
// drawn a little higher, or the model would ask a second time for every stroke the person draws (docs/90).
void test_a_stroke_drawn_a_little_higher_is_the_same_stroke(void) {
  const ink::Field paper = a_paper();
  ink::Glyph here;
  a_line(here, 110, 150, 190, 150);
  ink::Glyph a_bit_higher;
  a_line(a_bit_higher, 110, 162, 190, 162);  // twelve pixels of three hundred: less than an eighth of the paper
  ink::Grid one;
  ink::Grid two;
  TEST_ASSERT_TRUE(ink::raster(here, paper, one));
  TEST_ASSERT_TRUE(ink::raster(a_bit_higher, paper, two));
  TEST_ASSERT_EQUAL_UINT8(one.at_y, two.at_y);
  TEST_ASSERT_EQUAL_UINT16(0, ink::distance(one, two));
}

// **I el model les llegeix per separat** (0.195.0): les dues ratlles del primer cas, ensenyades com dos caràcters i llegides amb el
// paper que tenen, que és el que la banda i l'app fan de debò (docs/30, docs/90).
void test_the_model_reads_a_dash_and_an_underline_apart(void) {
  ink::Model model = a_model();
  const ink::Field paper = a_paper();
  ink::Glyph dash;
  a_line(dash, 110, 150, 190, 150);
  ink::Glyph under;
  a_line(under, 110, 260, 190, 260);
  ink::Grid on_the_middle;
  ink::Grid at_the_bottom;
  TEST_ASSERT_TRUE(ink::raster(dash, paper, on_the_middle));
  TEST_ASSERT_TRUE(ink::raster(under, paper, at_the_bottom));
  TEST_ASSERT_TRUE(model.add(on_the_middle, '-'));
  TEST_ASSERT_TRUE(model.add(at_the_bottom, '_'));
  const ink::Answer read_a_dash = model.guess(on_the_middle);
  TEST_ASSERT_TRUE(read_a_dash.any);
  TEST_ASSERT_EQUAL_CHAR('-', read_a_dash.top[0].text);
  TEST_ASSERT_TRUE(read_a_dash.confident);
  const ink::Answer read_an_underline = model.guess(at_the_bottom);
  TEST_ASSERT_TRUE(read_an_underline.any);
  TEST_ASSERT_EQUAL_CHAR('_', read_an_underline.top[0].text);
  TEST_ASSERT_TRUE(read_an_underline.confident);
}

// **La quadrícula diu on és la tinta**: a circle inks a ring and the middle stays quiet, o sigui que el que el model mira és el
// dibuix i no un graó de grisos.
void test_a_circle_inks_a_ring_and_keeps_the_middle_quiet(void) {
  ink::Grid circle;
  a_circle(40, circle);
  TEST_ASSERT_TRUE(circle.inked() > 8);
  TEST_ASSERT_EQUAL_UINT8(ink::kQuiet, circle.cell[3 * ink::kSide + 3]);
  // **I el requadre és quadrat**: 8 vuitens, que és el que una `-` no té.
  TEST_ASSERT_EQUAL_UINT8(8, circle.aspect);
  TEST_ASSERT_EQUAL_UINT8(1, circle.strokes);
}

// **Un glif prim no s'estira**: a `-` and a `1` are the same ink in two proportions, and the grid has to keep the proportion or
// the two letters end up on top of each other (docs/90).
void test_a_thin_glyph_keeps_its_proportion(void) {
  ink::Grid bar;
  ink::Grid stick;
  a_bar(bar);
  a_stick(stick);
  TEST_ASSERT_EQUAL_UINT8(8, bar.aspect);
  TEST_ASSERT_EQUAL_UINT8(1, stick.aspect);
  // The bar is one row of cells and the stick is one column: the middle row is inked and the ones above and below are not.
  uint8_t inked_middle = 0;
  for (uint8_t x = 0; x < ink::kSide; ++x) {
    if (bar.cell[3 * ink::kSide + x] != ink::kQuiet) ++inked_middle;
  }
  TEST_ASSERT_TRUE(inked_middle >= 6);
  TEST_ASSERT_EQUAL_UINT8(ink::kQuiet, bar.cell[1 * ink::kSide + 3]);
  // **I no s'assemblen en res**: only the cell of the middle crosses, o sigui que la distància és la de dues lletres que no
  // comparteixen tinta més la del requadre.
  TEST_ASSERT_TRUE(ink::distance(bar, stick) >= 40);
  // I un model que les ha vist totes dues no les confon mai, que és el que importa.
  ink::Model model = a_model();
  TEST_ASSERT_TRUE(model.add(bar, '-'));
  TEST_ASSERT_TRUE(model.add(stick, '1'));
  const ink::Answer on_the_bar = model.guess(bar);
  const ink::Answer on_the_stick = model.guess(stick);
  TEST_ASSERT_TRUE(on_the_bar.confident);
  TEST_ASSERT_EQUAL_CHAR('-', on_the_bar.top[0].text);
  TEST_ASSERT_TRUE(on_the_stick.confident);
  TEST_ASSERT_EQUAL_CHAR('1', on_the_stick.top[0].text);
}

// **Dos dibuixos de la mateixa lletra són a prop, i dues lletres diferents no**: el cercle i la `c` són la mateixa corba amb un
// forat, i aquesta diferència és el que el model ha de veure.
void test_two_drawings_of_one_letter_are_near_and_two_letters_are_far(void) {
  ink::Grid one;
  ink::Grid the_same;
  ink::Grid other;
  a_circle(40, one);
  a_circle(34, the_same);
  a_cee(other);
  // **El mateix cercle dibuixat dues vegades és zero**: the box is normalised away, o sigui que el radi no és una diferència.
  TEST_ASSERT_EQUAL_UINT16(0, ink::distance(one, the_same));
  TEST_ASSERT_TRUE(ink::distance(one, other) < ink::kFar);
  TEST_ASSERT_TRUE(ink::distance(one, other) > 20);
}

// **El model escriu el que se li ha ensenyat** (docs/90): two examples, a circle and a `c`, and each drawing has to come back
// with its own letter, segur i sense preguntar.
void test_the_model_answers_the_letter_it_was_taught(void) {
  ink::Model model = a_model();
  ink::Grid circle;
  ink::Grid another_circle;
  ink::Grid cee;
  a_circle(40, circle);
  a_circle(37, another_circle);
  a_cee(cee);
  TEST_ASSERT_TRUE(model.add(circle, 'o'));
  TEST_ASSERT_TRUE(model.add(cee, 'c'));

  const ink::Answer on_a_circle = model.guess(another_circle);
  TEST_ASSERT_TRUE(on_a_circle.any);
  TEST_ASSERT_TRUE(on_a_circle.confident);
  TEST_ASSERT_EQUAL_CHAR('o', on_a_circle.top[0].text);

  const ink::Answer on_a_cee = model.guess(cee);
  TEST_ASSERT_TRUE(on_a_cee.any);
  TEST_ASSERT_EQUAL_CHAR('c', on_a_cee.top[0].text);
}

// **Una lletra que ningú ha ensenyat no s'escriu**: el model ha de poder dir que no, o la banda escriuria una lletra que la
// persona no ha dibuixat (docs/90).
void test_a_glyph_nobody_taught_has_no_candidate(void) {
  ink::Model model = a_model();
  ink::Grid circle;
  ink::Grid stick;
  ink::Grid zigzag;
  a_circle(40, circle);
  a_stick(stick);
  a_zigzag(zigzag);
  TEST_ASSERT_TRUE(model.add(circle, 'o'));
  TEST_ASSERT_TRUE(model.add(stick, 'l'));
  const ink::Answer answer = model.guess(zigzag);
  TEST_ASSERT_FALSE(answer.any);
  TEST_ASSERT_FALSE(answer.confident);
}

// **La `i` i la `l` són la mateixa línia i el punt les separa**: the cells alone cannot say it, o sigui que el model compta els
// traços i la distància els pesa (docs/90).
void test_the_dot_is_what_tells_an_i_from_an_l(void) {
  ink::Grid stick;
  ink::Grid with_a_dot;
  a_stick(stick);
  an_eye(with_a_dot);
  TEST_ASSERT_EQUAL_UINT8(1, stick.strokes);
  TEST_ASSERT_EQUAL_UINT8(2, with_a_dot.strokes);
  TEST_ASSERT_TRUE(ink::distance(stick, with_a_dot) >= 9);
}

// **El model va i torna del fitxer** (docs/70): the header, the two-bit cells and the label, i el que s'ha desat és el que es
// llegeix.
void test_the_model_saves_and_comes_back(void) {
  ink::Model model = a_model();
  ink::Grid circle;
  ink::Grid cee;
  ink::Grid eye;
  a_circle(40, circle);
  a_cee(cee);
  an_eye(eye);
  TEST_ASSERT_TRUE(model.add(circle, 'o'));
  TEST_ASSERT_TRUE(model.add(cee, 'c'));
  TEST_ASSERT_TRUE(model.add(eye, 'i'));
  TEST_ASSERT_EQUAL_UINT16(3, model.count);

  uint8_t blob[512];
  const size_t wrote = ink::save(model, blob, sizeof(blob));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(ink::blob_bytes(model)), static_cast<uint32_t>(wrote));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(ink::kHeaderBytes + 3 * ink::kExampleBytes),
                           static_cast<uint32_t>(wrote));

  ink::Example block[64];
  ink::Model back;
  back.examples = block;
  back.capacity = 64;
  TEST_ASSERT_TRUE(ink::load(back, blob, wrote));
  TEST_ASSERT_EQUAL_UINT16(3, back.count);
  ink::Grid a_bit_other;
  a_circle(38, a_bit_other);
  TEST_ASSERT_EQUAL_CHAR('c', back.guess(cee).top[0].text);
  TEST_ASSERT_EQUAL_CHAR('o', back.guess(a_bit_other).top[0].text);
  TEST_ASSERT_EQUAL_UINT16(1, back.examples_of('i'));
}

// **Un fitxer d'una altra quadrícula es refusa**: read as a 7x7 it would be a picture of nothing, i el model ha de quedar buit i
// no amb dues lletres inventades (docs/70).
void test_a_blob_of_another_grid_is_refused(void) {
  ink::Model model = a_model();
  ink::Grid circle;
  a_circle(40, circle);
  TEST_ASSERT_TRUE(model.add(circle, 'o'));
  uint8_t blob[512];
  const size_t wrote = ink::save(model, blob, sizeof(blob));
  blob[4] = 5;  // a 5x5 model, read here
  ink::Example block[64];
  ink::Model other;
  other.examples = block;
  other.capacity = 64;
  TEST_ASSERT_FALSE(ink::load(other, blob, wrote));
  TEST_ASSERT_EQUAL_UINT16(0, other.count);
}

// **El mateix dibuix dues vegades és una mostra**: a person who draws the same circle twice has not taught the model anything
// new, and two copies would weigh twice in the count (docs/90).
void test_the_same_drawing_twice_is_one_example(void) {
  ink::Model model = a_model();
  ink::Grid circle;
  ink::Grid the_same;
  a_circle(40, circle);
  a_circle(40, the_same);
  TEST_ASSERT_TRUE(model.add(circle, 'o'));
  TEST_ASSERT_FALSE(model.add(the_same, 'o'));
  TEST_ASSERT_EQUAL_UINT16(1, model.count);
  // **I el mateix dibuix amb una altra lletra sí que és una altra mostra**: és el que fa l'aprenentatge per correcció.
  TEST_ASSERT_TRUE(model.add(the_same, 'O'));
  TEST_ASSERT_EQUAL_UINT16(2, model.count);
}

// **El bloc és del qui el deixa**: a model with a full block says no, i un glif buit tampoc no és una mostra.
void test_a_full_block_and_an_empty_glyph_are_refused(void) {
  ink::Example small_block[1];
  ink::Model model;
  model.examples = small_block;
  model.capacity = 1;
  ink::Grid circle;
  ink::Grid cee;
  a_circle(40, circle);
  a_cee(cee);
  TEST_ASSERT_TRUE(model.add(circle, 'o'));
  TEST_ASSERT_FALSE(model.add(cee, 'c'));
  ink::Grid empty;
  TEST_ASSERT_FALSE(model.add(empty, 'x'));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_a_circle_inks_a_ring_and_keeps_the_middle_quiet);
  RUN_TEST(test_a_thin_glyph_keeps_its_proportion);
  RUN_TEST(test_two_drawings_of_one_letter_are_near_and_two_letters_are_far);
  RUN_TEST(test_the_model_answers_the_letter_it_was_taught);
  RUN_TEST(test_a_glyph_nobody_taught_has_no_candidate);
  RUN_TEST(test_the_dot_is_what_tells_an_i_from_an_l);
  RUN_TEST(test_a_dash_and_an_underline_are_two_drawings_when_the_field_is_known);
  RUN_TEST(test_a_tap_is_a_dot_and_not_a_dash);
  RUN_TEST(test_a_stroke_drawn_a_little_higher_is_the_same_stroke);
  RUN_TEST(test_the_model_reads_a_dash_and_an_underline_apart);
  RUN_TEST(test_the_model_saves_and_comes_back);
  RUN_TEST(test_a_blob_of_another_grid_is_refused);
  RUN_TEST(test_the_same_drawing_twice_is_one_example);
  RUN_TEST(test_a_full_block_and_an_empty_glyph_are_refused);
  return UNITY_END();
}
