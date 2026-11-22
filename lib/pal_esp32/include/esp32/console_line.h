#ifndef LUNOK_PAL_ESP32_CONSOLE_LINE_H
#define LUNOK_PAL_ESP32_CONSOLE_LINE_H

#include <stddef.h>
#include <stdint.h>

// **El terminal del cable, amb el `console` de la IDF** (0.190.222, l'amo: *«Vull una migració completa de la shell a aquest terminal
// serie. Poder moure el cursor, fer efectes, i colors dona molta vidilla»*). El que aquesta porta encén és **l'editor de línia**: el
// cursor, les fletxes, l'historial, el tabulador i les pistes els porta `linenoise` —el que la documentació d'Espressif anomena *line
// editing*—, i el que la línia diu quan s'acaba va al `shell` de sempre, que continua essent el terminal virtual d'aquest sistema
// (docs/30, docs/50).
//
// **Aquest fitxer és la part de sota**: el que sap de la IDF i de FreeRTOS i res del shell. La taula de comandes li arriba com una llista
// de noms perquè el `console` pugui completar-los, i les línies surten per una cua que el torn del nucli buida (docs/20, docs/45).
namespace pal {

// Encén el terminal amb els noms que el tabulador ha de completar. False si aquesta placa no el pot tenir: el controlador de la consola
// no arrenca, o ja hi era. **Un false no és un error**: el shell té el seu editor i la placa es queda com estava (docs/50).
bool start_the_line_editor(const char* const* names, uint8_t count);

bool the_line_editor_is_on();

// **Si el terminal de l'altre cap entén els codis d'escapament** —cursor, color, esborrar pantalla— (0.190.223): ho contesta la sonda, un
// sol cop i a l'arrencada, i **no és el mateix que «hi ha editor»**: el driver del USB pot no instal·lar-se i el terminal ser-hi igualment.
// El que decideix escriure codis ho pregunta aquí; el que decideix qui dibuixa la línia ho pregunta a `the_line_editor_is_on`
// (docs/30, docs/50).
bool the_terminal_speaks_ansi();

// **El prompt el diu el shell i el dibuixa el terminal**: qui edita la línia és qui sap on és el cursor, o sigui que el text es deixa
// aquí i el proper `linenoise()` el posa (docs/30).
void set_the_line_prompt(const char* text);

// Una línia acabada, sense el salt i sense bloquejar. False quan no n'hi ha cap d'espera.
bool take_a_finished_line(char* out, size_t capacity);
// **I la mateixa esperant-la fins a `timeout_ms`** (0.190.244): zero vol dir no esperar (docs/30).
bool take_a_finished_line(char* out, size_t capacity, int32_t timeout_ms);

// **Si el terminal té la consola**: mentre la té, el camí de bytes del nucli no ha de llegir, o li robaria les tecles a l'editor
// (docs/50).
bool the_line_editor_owns_the_console();

// **L'historial de la sessió, desat a un fitxer del VFS** (0.190.236): les llistes les té `linenoise` a la memòria i el que fa aquestes dues
// és el que el component ja sap fer —`linenoiseHistoryLoad` i `linenoiseHistorySave`, que van amb `fopen` i per tant entenen tant `/littlefs`
// com `/sd`, que són els dos discs que la IDF té muntats al VFS—. El camí no el tria aquest mòdul: el tria el nucli, que és qui sap quin
// disc prefereix la persona (docs/30, docs/50, docs/70).
bool load_the_line_history(const char* path);
bool keep_the_line_history(const char* path);

// **La mida que el terminal ha contestat** (0.190.234, l'amo: *«al ser una sessió ansi, pots obtenir el tamany de la vista, oi?»*): files i
// columnes, preguntades un sol cop quan l'editor s'encén. **Zero files vol dir «no ho ha dit»** —la pregunta de debò és una extensió d'xterm
// i qui no la coneix calla—, i false vol dir que aquesta banda no té terminal i no té cap mida a dir (docs/50, docs/85).
bool the_terminal_size(int& rows, int& columns);

}  // namespace pal

#endif  // LUNOK_PAL_ESP32_CONSOLE_LINE_H
