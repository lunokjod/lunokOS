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

// ---- l'iBeacon (0.194.0) ----
//
// **El mateix ràdio, dient un nombre i no oferint un teclat** (l'amo: *«pots integrar això (iBeacon) al ble dev? `ble dev beacon on`»*).
// Un beacon no vol enllaç: el que fa és repetir quatre números a l'aire —un UUID de proximitat, un `major`, un `minor` i el senyal que
// dona a un metre— perquè un telèfon pugui situar-lo. **Mentre dura, el teclat i el ratolí no s'anuncien** (una advertise de llegat és un
// sol contingut) però no s'apaguen: `beacon_stop()` els torna a posar (docs/30, docs/50).
//
// **La identitat de sèrie és d'aquesta casa i es pot canviar**: `6c756e6f-6b4f-5321-0000-000000000001`, que és `lunokOS!` en bytes i
// després zeros, amb `major` 1, `minor` 1 i `-59 dBm` a un metre. Un UUID de proximitat no es registra enlloc; el que ha de fer és no
// coincidir amb el d'una altra cosa que una persona ja tingui (docs/50).
extern const char kBeaconUuid[];
// **Els valors amb què neix**, perquè el prompt els pugui dir sense endevinar-los.
const uint16_t kBeaconMajor = 1;
const uint16_t kBeaconMinor = 1;
const int8_t kBeaconPower = -59;
// **Un UUID de text a setze bytes**, amb o sense guions (`6c756e6f-6b4f-5321-…` o els trenta-dos dígits seguits). Fals si no és un UUID.
bool beacon_uuid_from_text(const char* text, uint8_t out[16]);
// **El UUID de text del que s'està anunciant** —o del que s'anunciaria—, per al prompt i per al registre.
const char* beacon_uuid_text(const uint8_t uuid[16]);
// **La identitat que hi ha ara, en bytes**: el que cal per canviar-ne un tros (el `major`) i tornar a engegar el beacon amb la resta
// igual (docs/30).
const uint8_t* beacon_uuid();
// **Encendre'l**, amb la identitat que es digui, i apagar-lo. `false` vol dir que aquesta placa no té ràdio o que la pila no hi és.
bool beacon_start(const uint8_t uuid[16], uint16_t major, uint16_t minor, int8_t power);
void beacon_stop();
bool beacon_on();
// **El que fa el beacon en aquest moment**, perquè `bt status` i `bt dev` ho puguin dir: si hi és, amb quins números.
uint16_t beacon_major();
uint16_t beacon_minor();
int8_t beacon_power();

// ---- la bateria i la consola, per BLE (0.196.0) ----
//
// **Dos serveis que es poden obrir i tancar** (l'amo: *«Afegeix al bt dev battery on/off per notificar la batería»* i *«bt dev console
// on/off per exposar la shell de sistema (el mateix que es veu via serial, però per ble)»*). El de la bateria és el **Battery Service**
// del Bluetooth (0x180F, nivell a 0x2A19) i el de la consola és el **Nordic UART Service**, que és el que parlen les apps de terminal
// de BLE per telèfon. **Tancats no contesten**: el servei és a la taula des del primer aparellament i el commutador és el que fa que
// respongui —un nivell que es llegeix a zero i una escriptura que es refusa— (docs/30, docs/50).
bool battery_on();
void battery_off();
bool battery_on_now();
// **El nivell, quan canvia**: fals vol dir que no hi ha ningú escoltant o que el servei és tancat.
bool battery_notify(int percent);
// **La consola**: obrir-la vol dir que el que arriba pel port entra a la shell i que el que la shell escriu en surt. El nucli és qui
// instal·la el mirall de la consola (el mateix que veu el sèrie) i qui rep els bytes (docs/30, docs/50).
bool console_on();
void console_off();
bool console_on_now();

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
bool host_remembers();
// **Si l'altre cap ha encès els reports**: connectat no és escoltat, i el CCCD de cada característica d'entrada és el que ho diu.
// **Si l'altre cap escolta**, i des de la 0.191.0 es pot preguntar per un dels dos reports: `"keyboard"`, `"mouse"`, o `nullptr` per
// a «algun dels dos» (docs/30, docs/50).
bool reports_enabled(const char* which = nullptr);
bool host_reconnect();
// **Encendre i apagar el costat amfitrió**, sense oferir-se com a teclat.
bool host_on();
void host_off();
// **Si algú escolta per aquest costat**: la ràdio pot ser engegada pel dispositiu i això no vol dir que hi hagi amfitrió.
bool host_is_up();
// **Si la ràdio treballa per alguna cosa, sigui quin sigui el costat** (0.190.195, l'amo: *«perquè només salta el led de kRadio si està dev
// on… kRadio ha de saltar amb bt on/off i prou»*): the blue breath says the radio is doing something, and being a host is doing something.
bool radio_is_working();

// ---- el que aquesta placa és, que ja no es tria (0.191.0) ----
//
// **Un teclat i un ratolí, sempre i alhora** (l'amo: *«i no podem anunciar els dos dispositius sempre? després que hi hagi una app de
// teclat o de mouse és indiferent»*): el mapa és el dels dos reports amb els seus IDs i no es toca mai, o sigui que no hi ha paraula a
// triar ni cara que canviar. El que queda és el punter (docs/30, docs/50).
// **Oblidar l'altre cap** (0.191.2): un amfitrió es queda el report map que va llegir quan va aparellar i no el torna a llegir mai, o sigui
// que un mapa que ha canviat deixa un vincle que parseja el que li arriba amb el mapa vell. Això esborra els vincles d'aquesta banda i
// tanca l'enllaç; **treure l'aparell de la llista de l'altre costat ho ha de fer una persona** (docs/50).
void forget_the_device();
bool move(int8_t dx, int8_t dy);
bool press(uint8_t buttons);
bool click(uint8_t buttons);
// Les dues portes que una pàgina de Lua té al ratolí (0.190.204).
bool move_for_a_page(int dx, int dy);
bool click_for_a_page(const char* button);
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
