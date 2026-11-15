// **Generat per `tools/make_bar_icon.py` i no s'edita a ma** (0.189.58): el glif `U+23F0 ALARM CLOCK` (U+23F0) de
// **Noto Color Emoji**, rasteritzat a 21 px d'alçada i retallat a la seva tinta —18 x 21—. La font és SIL OFL 1.1 i el codi Apache 2.0 (https://github.com/googlefonts/noto-emoji). L'atribució viu aquí
// i a docs/50.
//
// **Per que una taula de bits i no un dibuix de linies**: la resta d'icones de la barra són cinc línies i tres arcs que es
// corregeixen llegint-les, i aquesta és una corba composta que a vint píxels no es pot tornar a dibuixar millor que la font.
// Les mides les publica `ink_of()` com les de tots els altres dibuixos, així que la barra la col·loca igual.
#ifndef LUNOK_KERNEL_BAR_ICONS_ART_H
#define LUNOK_KERNEL_BAR_ICONS_ART_H

#include <stdint.h>

namespace kernel {
namespace bar_icons {
namespace art {

// 18 x 21 píxels, una fila darrere l'altra, un bit per píxel i el bit de més a l'esquerra és el de l'esquerra.
const uint8_t kAlarmW = 18;
const uint8_t kAlarmH = 21;
const uint8_t kAlarmBits[48] = {
    0x3C, 0x0F, 0x1F, 0xA7, 0xEF, 0xE1, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xC7,
    0x87, 0x83, 0x80, 0x71, 0xC0, 0x0E, 0x60, 0x01, 0x90, 0x00, 0x6C, 0x21,
    0x8F, 0x07, 0x83, 0xC0, 0xC0, 0xF0, 0x00, 0x24, 0x00, 0x09, 0x80, 0x06,
    0x30, 0x03, 0x0E, 0x01, 0x81, 0xC0, 0xC0, 0x7F, 0xF0, 0x10, 0x02, 0x00,
};

}  // namespace art
}  // namespace bar_icons
}  // namespace kernel

#endif  // LUNOK_KERNEL_BAR_ICONS_ART_H
