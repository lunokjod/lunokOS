// **El servei del cel al nucli** (0.192.63, l'amo: *«Fes un app de GPS … Crea la part del sistema, les potes lua i finalment
// l'app. Crea una comanda /gps [on|off]»*).
//
// El mòdul és del HAL i el que és el cel no ho sap ningú més; aquí hi ha les tres coses que **no són del driver**: quan es llegeix
// (aquesta placa no té la interrupció del receptor, o sigui que algú ho ha de preguntar cada estona), **qui creu l'hora** —el
// commutador de Settings, i escriure el rellotge i la RTC és una decisió i no una lectura— i el comptador del que costa (docs/30,
// docs/50, docs/60).
#ifndef LUNOK_KERNEL_GPS_H
#define LUNOK_KERNEL_GPS_H

#include <stddef.h>
#include <stdint.h>

#include "hal/hal.h"

namespace kernel {
namespace gps {

// Installs what Lua may ask for. False on a board with no receiver, and then `gps.*` answers `E_UNSUPPORTED` instead of making
// up a position (docs/30).
bool boot(hal::IPlatform& platform);

// **Si aquesta placa mira el cel**, que és la pregunta que fa la pàgina de Settings per deixar el commutador desactivat (docs/30).
bool available();

// **Llegir el que el receptor tingui i, si la persona ho ha demanat, creure-li l'hora** (0.192.63): the turn of the kernel calls
// this once and it does nothing when the module is off; the fix that comes out is the one every door answers (docs/30, docs/50).
void poll();

// The switches: power the module, or leave it dark. `on` is remembered for the run, not across a reboot.
bool turn_on();
// **Apagar-ho és d'una persona**: `turn_off` diu que el mòdul no es vol, i el vigilant no el torna a encendre (0.192.90).
bool turn_off();
// **I tancar el cable sense dir-ho**, que és el que fa el vigilant quan el mòdul calla: el fil es deixa anar i el proper intent el
// torna a sondar, però el mòdul segueix volgut (docs/30, docs/50).
bool the_wire_gone();
bool powered();

// **El que se sap**: the last fix, the sky of satellites and the name of the module (docs/30).
bool fix(hal::GpsFix& out);
const hal::GpsSatellite* satellites(uint8_t& count);
bool caps(hal::GpsCaps& out);
const char* last_error();

// **El que s'ha cregut del cel**, que és el que la pàgina ensenya com a «sincronitzat fa…» (docs/30): **quants mil·lisegons fa** que el
// rellotge es va posar des d'una data bona —una edat i no una marca, o sigui que la pàgina no ha de saber quan va arrencar la placa—,
// o `-1` si no s'ha fet mai.
int64_t last_sync_ms();
// **Quant s'ha mogut el rellotge** en aquella sincronització, en mil·lisegons (pot ser negatiu). És el número que diu si el
// rellotge de la placa anava bé o si el cel li ha hagut de dir l'hora (docs/30, docs/60).
int64_t last_shift_ms();

}  // namespace gps
}  // namespace kernel

#endif  // LUNOK_KERNEL_GPS_H
