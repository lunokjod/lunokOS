#ifndef LUNOK_HAL_ESP32_MESH_NODE_H
#define LUNOK_HAL_ESP32_MESH_NODE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// **El node de Bluetooth Mesh, en C** (0.190.212): the mesh of Espressif is a C API whose macros cannot be compiled as C++ —the model
// macros use `[0 ... N]`, que en C++ és una lambda i no un designador—, o sigui que aquesta part viu en un `.c` i el que hi ha aquí és la
// frontera: unes quantes preguntes i ordres, sense cap capçalera del mesh a la vista (docs/45, docs/50).
//
// **I des de la 0.190.218 aquesta placa és el provisionador** (l'amo: *«no puc provisionar jo manualment la clau del mesh per linia de
// comandaments, com amb el wifi?»*): un node no pot rebre les claus a mà, perquè la provisió **és** l'intercanvi que les crea; el que sí que
// es pot és tenir-les nosaltres i repartir-les. O sigui que aquí hi ha les dues meitats: què és aquesta placa dins la xarxa, i què fa amb
// les altres.

// ---- què és aquesta placa ----
void mesh_node_set_uuid(const uint8_t mac[6]);
void mesh_node_set_name(const char* name);
// The stack up. **No encén la ràdio**: això ho fa qui la porta (`esp32_radio_for`) abans, i la tasca de l'amfitrió després
// (`esp32_radio_start`), perquè els serveis GATT del mesh han d'entrar abans que res corri (docs/50).
bool mesh_node_init(void);
// **Si la pila del mesh corre** (0.190.218): el provisionador que encara no té claus té la pila enlaire i cap xarxa, i això és un estat bo
// —el primer pas cap a `mesh key`—, o sigui que cal preguntar-ho a part de si hi ha adreça.
bool mesh_node_is_up(void);
bool mesh_node_is_provisioned(void);
uint16_t mesh_node_address(void);
bool mesh_node_offer(void);
void mesh_node_withdraw(void);

// ---- la xarxa que aquesta placa té ----
// **Les claus són setze bytes aleatoris i prou**: el mesh no hi posa cap checksum. `make_keys` les fa amb el generador del xip i les guarda;
// `own_the_network` les hi poses tu. Totes dues contesten `false` si la pila no les ha volgudes (docs/50).
bool mesh_node_make_keys(uint8_t net_key[16], uint8_t app_key[16]);
bool mesh_node_own_the_network(const uint8_t net_key[16], const uint8_t app_key[16]);
bool mesh_node_has_the_network(void);
bool mesh_node_network_keys(uint8_t net_key[16], uint8_t app_key[16]);
// **El provisionador escolta** els aparells que no són enlloc, i contesta quants n'ha sentit. Bloqueja el que ho demana, o sigui que el
// prompt ho diu abans.
uint8_t mesh_node_watch(uint32_t ms);
uint8_t mesh_node_heard_count(void);
bool mesh_node_heard_at(uint8_t index, uint8_t uuid[16], uint8_t* bearer, int8_t* rssi);
// **I li dóna una adreça**: `address` zero vol dir «la propera lliure», i el que el node ha acceptat queda a `given`.
bool mesh_node_provision(uint8_t index, uint16_t address, uint16_t* given);
// Qui és dins la xarxa d'aquesta placa: les adreces, i quants n'hi caben.
uint8_t mesh_node_known(uint16_t* addresses, uint8_t cap);

#ifdef __cplusplus
}
#endif

#endif  // LUNOK_HAL_ESP32_MESH_NODE_H
