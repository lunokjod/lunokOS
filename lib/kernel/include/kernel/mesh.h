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

// ---- la xarxa que aquesta placa dóna als altres ----
// **Des de la 0.190.218 aquesta placa pot ser el provisionador** (l'amo: *«no puc provisionar jo manualment la clau del mesh per linia de
// comandaments, com amb el wifi?»*). Un node no rep les claus a mà; el provisionador les té i les reparteix, o sigui que aquí hi ha les
// mateixes tres coses que amb el wifi: veure quines claus té, fer-ne unes de noves o posar-hi les que vulguis, i ensenyar-les per poder-les
// apuntar (docs/30, docs/50).
bool can_keep_the_keys();
bool has_the_network();
bool make_keys(uint8_t net_key[16], uint8_t app_key[16]);
bool own_the_network(const uint8_t net_key[16], const uint8_t app_key[16]);
bool network_keys(uint8_t net_key[16], uint8_t app_key[16]);
// **Sentir qui no és enlloc** (bloqueja `ms`), i qui ha sentit la placa: la UUID, la ràdio i la força.
int watch(uint32_t ms);
int heard_count();
bool heard_at(int index, uint8_t uuid[16], int* rssi);
// Donar-li una adreça: zero vol dir la propera de la taula, i el número que ha quedat es diu a `given`.
bool provision(int index, uint16_t address, uint16_t* given);
int known(uint16_t* addresses, int cap);

// **Una clau, escrita com tothom l'escriu** (0.190.218): trenta-dos dígits hexadecimals, els setze bytes i cap checksum. Una sola definició
// perquè el prompt, la botiga i el registre no puguin escriure la mateixa clau de tres maneres (docs/45, docs/50).
void key_to_text(const uint8_t key[16], char text[33]);
bool key_from_text(const char* text, uint8_t key[16]);

}  // namespace mesh
}  // namespace kernel

#endif  // LUNOK_KERNEL_MESH_H
