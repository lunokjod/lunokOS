#ifndef LUNOK_HAL_ESP32_BOARD_NAME_H
#define LUNOK_HAL_ESP32_BOARD_NAME_H

#include <cstddef>

namespace hal {

// **El nom d'aquesta placa** (0.190.197, l'amo: *«El nom de la máquina, usa lunokOS-XXYY (els dos últims digits de la mac) si no ca
// "lunokXXYY"»*, i 0.190.215: *«canvia el nom de la màquina lunokOS-XXYY, com amb el bluetooth»*): `lunokOS-XXYY` amb els dos últims bytes de
// l'adreça de la ràdio llegida de l'eFuse, i el curt quan al lloc on va no hi cap.
//
// **Un sol lloc i no tres** (0.190.215): el teclat de Bluetooth, el node del mesh, l'mDNS i el que vingui després diuen el mateix nom amb la
// mateixa adreça, perquè una placa amb dos noms en dues llistes és una placa que ningú no reconeix. La del Bluetooth és la que mana —el nom va
// néixer aquí i és el que un telèfon ja ha vist— i les altres la demanen.
//
// **`cap` és el que hi ha al lloc on va**: si el nom sencer no hi cap, s'escriu el curt (`lunokXXYY`), i si tampoc no hi cap, una cadena
// buida. No hi ha cap tall a mitges: un nom tallat és un nom que no és el d'aquesta placa (docs/50).
void the_board_name(char* out, size_t cap);

}  // namespace hal

#endif  // LUNOK_HAL_ESP32_BOARD_NAME_H
