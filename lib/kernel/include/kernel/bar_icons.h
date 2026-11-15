// **Les icones de la barra del sistema, portades del PaperOS amb les seves mides** (0.189.56, l'amo: *«el projecte PaperOS te
// unes icones exquisides a la seva barra del sistema, usb, wifi, bluetooth i l'icona de batería, diría que també el de
// càrrega… Portem aquestes icones a la barra de sistema del lunokOS. La barra de sistema de lunokOS es mes baixeta que la de
// PaperOS, vull usar les mateixes icones, sense escalar per la diferencia d'alçada de la barra»*).
//
// **No són un fitxer de píxels: són dibuixos fets amb línies i rectangles** (`PaperOS/src/ui/title_bar.cpp`, les funcions
// `drawWifiGlyph`, `drawBluetoothGlyph`, `drawSerialGlyph`, `drawNtpGlyph`, `drawBatteryGlyph` i `fillBolt`), i per això es
// poden portar sense escalar-los: cada un es mesura contra la caixa que rep, i el que fa aquest mòdul és donar-los **la
// caixa del projecte del paper** —40 × 28 per als quatre glifs i 46 × 26 per a la figura de la bateria— i prou. Una barra
// més baixa no toca el dibuix: el que la barra decideix és **on** va la caixa, no com de gran és (docs/50, docs/92).
//
// **Les tintes que el paper projecte va mesurar al vidre** (18/10, `captures/`): el ventall del WiFi **31 × 27**, la marca
// de l'USB tombat **38 × 21**, la runa del Bluetooth **18 × 26** i el rellotge de sorra de l'NTP **19 × 21**. La caixa de
// 40 × 28 és la que els aguanta tots quatre i la que deixa una fila d'aire, que és el que fa que el dibuix no toqui la vora
// del que s'envia al panell.
#ifndef LUNOK_KERNEL_BAR_ICONS_H
#define LUNOK_KERNEL_BAR_ICONS_H

#include <stdint.h>

#include "hal/hal.h"

namespace kernel {
namespace bar_icons {

// **Les caixes del projecte del paper, i són les úniques mides d'aquest fitxer**: `IconW`/`IconH` per als quatre glifs
// d'estat i la de la bateria, que no és un estat sinó una lectura i per això té la seva (`BatteryGlyphW/H`, 46 × 26).
const uint16_t kIconW = 40;
const uint16_t kIconH = 28;
const uint16_t kBatteryW = 46;
const uint16_t kBatteryH = 26;

// **Els estats de la barra**: la xarxa, el Bluetooth, el cable del port sèrie, el rellotge de sorra —una cosa que compta
// enrere: el rellotge que s'està posant o el temporitzador que algú ha deixat— i el despertador d'una alarma posada
// (0.189.57, l'amo: *«Afegeix també al costat del rellotge l'alarma en plan despertador alarma i el rellotge de sorra per
// el temportizador»*).
enum Kind : uint8_t {
  kWifi = 0,
  kBluetooth,
  kSerial,
  kHourglass,
  // **El despertador** (0.189.57): the drawing is this project's and not the paper project's —that bar had no alarms—, made
  // of the same lines as the others so it sits beside them without a second language (docs/50, docs/92).
  kAlarm,
};

// **Quina tinta ocupa cada dibuix dins de la seva caixa**: the box of the paper project is 40 x 28 and the drawings are
// narrower than that —the fan 31, the mark of the port 38, the rune 18 and the hourglass 19—, so the bar can place two of
// them **by the ink they have** and not by a box with air in it (0.189.57). The drawings do not change size: what changes
// is how much of the strip each one asks for.
struct Ink {
  uint8_t w;
  uint8_t h;
};

Ink ink_of(Kind kind);

// **Un glif, a la caixa que se li dóna**: `x`/`y` són la cantonada de la **caixa de 40 x 28 del projecte del paper**, que és
// la que mesura el dibuix, i la tinta hi cau centrada —`ink_of` diu on—. `ink` és el nivell de la tinta (0 en una barra de paper, perquè la barra d'aquest
// sistema és de dos tons) i el dibuix no surt de la caixa més del que el projecte del paper el deixava sortir —el ventall
// del WiFi té els radis d'una caixa de 40 i creix amb l'amplada, que és el que fa que no s'hagi d'escalar per la barra.
void glyph(hal::ISurface& surface, Kind kind, uint16_t x, uint16_t y, uint8_t ink = 0);

// **La figura de la bateria**, amb el seu nivell de zero a quatre barres i el llamp quan la placa s'està alimentant: és el
// mateix dibuix que el del paper projecte, amb les seves quatre barres, el seu mugró i el seu llamp de sis punts.
void battery(hal::ISurface& surface, uint16_t x, uint16_t y, int level, bool charging, uint8_t ink = 0);

}  // namespace bar_icons
}  // namespace kernel

#endif  // LUNOK_KERNEL_BAR_ICONS_H
