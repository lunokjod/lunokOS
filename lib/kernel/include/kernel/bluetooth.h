#ifndef LUNOK_KERNEL_BLUETOOTH_H
#define LUNOK_KERNEL_BLUETOOTH_H

#include <cstddef>
#include <cstdint>

namespace kernel {
namespace bt {

// **El teclat de Bluetooth** (0.190.179, l'amo: *«afegeix bluetooth al projecte, anem a fer un experiment»*). The board owns the radio;
// this is the policy: whether the board has one at all, what a person writes being turned into HID usage IDs, and the fact that while it
// is on the chip does not nap. It is not a network and it is not a page: there is nothing to connect to from here, there are characters
// to send (docs/30, docs/50).
bool available();
// **Encendre'l amb el nom que veurà l'altre aparell** i apagar-lo. Engegar-lo dues vegades no és un error.
// **Un nom buit vol dir el d'aquesta placa** (0.190.197, `lunokOS-A1B2` amb els dos últims bytes de l'adreça del ràdio).
bool start(const char* name);
// El nom que el ràdio anuncia, sigui el de la placa o el que algú hi va posar.
const char* name();
void stop();
bool is_on();
// Si algú hi és connectat: un teclat que no ho diu envia caràcters al buit.
bool connected();
// **Des de quan el teclat és engegat**, en els mil·lisegons del sistema (0.190.182, l'amo: *«mentre el bluetooth estigui actiu al M5Atom, vull
// que el led sigui blau i amb la sequencia "breathe"»*). El respir del llum necessita un començament, i el ràdio ja el té: així el llum no ha
// de portar un comptador propi ni endevinar quan va canviar (docs/30, docs/50).
int64_t on_since_ms();
// **Una tecla**, pel seu Usage ID del HID i amb les de modificació (`0x02` és Shift). `false` vol dir que no hi ha ningú a l'altre cap.
bool tap(uint8_t mods, uint8_t usage);
// **Un text sencer**, caràcter a caràcter. Torna quants n'ha enviat dels que ha rebut, que és el que una app vol saber.
size_t type(const char* text);
// **El que sap aquest sistema del ràdio, en una frase** per al prompt i per al registre.
const char* state_word();

// ---- l'amfitrió (0.190.188) ----
//
// **L'altra direcció**: the board is the host and somebody else's keyboard writes into the system. Les tecles d'aquest teclat passen
// pel **mateix camí que els bytes del cable** (`shell::feed`), o sigui que el prompt, les apps, les fletxes, Ctrl+D i Escape són els
// mateixos i no hi ha un segon teclat que mantenir (docs/30, docs/50).
bool can_host();
// Un dels aparells que ha contestat a l'exploració, tal com el va sentir la ràdio.
struct Peer {
  uint8_t addr[6];
  uint8_t addr_type;
  char name[25];
  bool hid;
  int8_t rssi;
  uint16_t appearance;
  uint16_t service;
};
// **Explorar i esperar**: omple `out` i torna quants n'hi ha. Bloqueja el que ho demana, o sigui que el prompt ho diu abans.
uint8_t host_scan(uint32_t ms, Peer* out, uint8_t cap);
// Lligar-se a un dels de l'última exploració, per l'índex, i esperar que el teclat accepti la subscripció.
bool host_pair(uint8_t index);
bool host_connected();
// **Tornar amb el teclat que ja hi era**, sense explorar: els vincles són a la botiga del ràdio.
bool host_reconnect();
// **Encendre i apagar el costat amfitrió**, sense oferir-se com a teclat.
bool host_on();
void host_off();
// **Si algú escolta per aquest costat**: la ràdio pot ser engegada pel dispositiu i això no vol dir que hi hagi amfitrió.
bool host_is_up();
// **Si la ràdio treballa per alguna cosa, sigui quin sigui el costat** (0.190.195, l'amo: *«perquè només salta el led de kRadio si està dev
// on… kRadio ha de saltar amb bt on/off i prou»*): the blue breath says the radio is doing something, and being a host is doing something.
bool radio_is_working();

// ---- el que aquesta placa és, no el que escolta (0.190.193) ----
//
// **Un teclat o un ratolí, i es tria** (l'amo: *«opció B»*): two reports in one HID service would mean report ids and giving up the boot
// protocol, o sigui que la placa és una cosa o l'altra. `be(true)` la fa un ratolí —i demana tornar a aparellar des de l'altre cap,
// perquè el mapa que aquell va llegir era el d'un teclat— i `be(false)` la torna a fer un teclat (docs/50).
bool is_a_mouse();
bool be(bool mouse);
bool move(int8_t dx, int8_t dy);
bool press(uint8_t buttons);
bool click(uint8_t buttons);
uint8_t buttons();
void host_forget();
// **Els cinc llums del teclat virtual** (bit 0 num, 1 caps, 2 scroll, 3 compose, 4 kana), tal com els va deixar l'altre aparell.
uint8_t leds();
// **El ratolí, si n'hi ha un**: on és i quins botons té. El cursor a la pàgina no és aquí.
bool mouse_seen();
int16_t mouse_x();
int16_t mouse_y();
uint8_t mouse_buttons();
// **Instal·lar el camí de les tecles**: el nucli és qui sap de caràcters; la ràdio només li dona els vuit bytes del report.
void set_the_host_sink();

}  // namespace bt
}  // namespace kernel

#endif  // LUNOK_KERNEL_BLUETOOTH_H
