#ifndef LUNOK_KERNEL_MESH_H
#define LUNOK_KERNEL_MESH_H

#include <cstdint>

namespace kernel {
namespace mesh {

// **La xarxa de Bluetooth Mesh** (0.190.212, l'amo: *«pots posar algun mesh?»*): what this board is when it is a **node** of a mesh and not
// a keyboard. El primer pas és el que hi ha: el node s'ofereix a un provisionador —el telèfon amb l'app de nRF Mesh o la d'Espressif—, es
// deixa provisionar, i **recorda** l'adreça que li han donat, perquè el que hi viu a la NVS és el que la pila va desar.
//
// **I el mesh i el teclat no són alhora**: the stack of NimBLE is the same and the advertiser is one, o sigui que encendre el mesh amb el
// teclat de Bluetooth enlaire es refusa i es diu per què (docs/50).

// Whether this build carries the stack at all: false on a board whose build has no `CONFIG_BLE_MESH`, and the prompt says so.
bool available();
bool is_up();
// The radio up and this node offering itself. Refused when the keyboard is holding the radio, and it says so in the log.
bool start();
void stop();
// What a provisioner left: whether this node is in a network and the unicast address of its first element (zero when it is not).
bool provisioned();
uint16_t address();

}  // namespace mesh
}  // namespace kernel

#endif  // LUNOK_KERNEL_MESH_H
