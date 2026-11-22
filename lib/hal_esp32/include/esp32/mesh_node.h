#ifndef LUNOK_HAL_ESP32_MESH_NODE_H
#define LUNOK_HAL_ESP32_MESH_NODE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// **El node de Bluetooth Mesh, en C** (0.190.212): the mesh of Espressif is a C API whose macros cannot be compiled as C++ —the model
// macros use `[0 ... N]`, que en C++ és una lambda i no un designador—, o sigui que aquesta part viu en un `.c` i el que hi ha aquí és la
// frontera: quatre preguntes i dues ordres, sense cap capçalera del mesh a la vista (docs/45, docs/50).
//
// **`mesh_node_init` no encén la ràdio**: the NimBLE host has to be up before this, o sigui que qui crida això és qui ha portat la ràdio
// (`esp32_radio_for`). False is a stack that refused, and the reason is in the log.
// **La identitat del node, posada abans d'inicialitzar-lo**: the six bytes of the radio's address, que és el que fa que dues plaques no
// siguin el mateix node davant d'un provisionador.
void mesh_node_set_uuid(const uint8_t mac[6]);

// **El nom amb què aquest node es presenta** (0.190.215, l'amo: *«canvia el nom de la màquina lunokOS-XXYY, com amb el bluetooth»*): the name
// an unprovisioned node broadcasts in its advertising, o sigui el que surt a la llista del provisionador abans de triar-lo. La pila només el
// posa a la resposta de l'escaneig (PB-GATT) i no a l'anunciant, o sigui que és el nom que es veu quan el telèfon ja l'ha trobat.
void mesh_node_set_name(const char* name);

bool mesh_node_init(void);
bool mesh_node_offer(void);
void mesh_node_withdraw(void);
bool mesh_node_is_provisioned(void);
uint16_t mesh_node_address(void);

#ifdef __cplusplus
}
#endif

#endif  // LUNOK_HAL_ESP32_MESH_NODE_H
