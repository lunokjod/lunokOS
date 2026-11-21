#ifndef LUNOK_HAL_ESP32_MESH_H
#define LUNOK_HAL_ESP32_MESH_H

#include "hal/hal.h"

namespace hal {

// **El node de Bluetooth Mesh d'aquesta placa** (0.190.212, l'amo: *«pots posar algun mesh?»*): la pila ESP-BLE-MESH compilada a dins
// (`CONFIG_BLE_MESH`), amb un element i el model que tot node ha de tenir —el servidor de configuració—, i res més. El que això fa és el
// primer pas de debò: la placa **apareix** a l'app d'un provisionador, s'hi deixa provisionar i recorda l'adreça que li han donat, i a partir
// d'aquí el que hi visqui són models, que és una decisió d'una altra estona (docs/50).
//
// **No és el teclat**: la pila de NimBLE és la mateixa, però el mesh fa servir l'anunci i la connexió per als seus portadors propis, o sigui
// que mentre el mesh és enlaire no hi cap un altre anunciador —i `mesh on` amb el teclat encès es refusa i ho diu.
IMesh* esp32_mesh();

}  // namespace hal

#endif  // LUNOK_HAL_ESP32_MESH_H
