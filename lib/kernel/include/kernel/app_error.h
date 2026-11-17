// **La pàgina d'una app que s'ha mort** (0.189.162, l'amo: *«Si peta una app, que menys que saber el perqué amb la pantalla i
// els dits. Un modal amb el missatge d'error? vull detalls del perqué una app peta»*).
//
// Fins ara, una app que es trencava deixava el seu error **al registre i a la consola**, i el que veia una persona amb el vidre
// era la pàgina de sota, com si no hagués passat res: la lletra del perquè hi era, però calia un cable per llegir-la. Aquest
// fitxer és el que la posa al vidre: el vidre sencer rentat, el nom de l'app, el missatge, el rastre de les crides i una línia
// que diu com continuar. **I s'espera el dit o una tecla** —o els segons que la pàgina espera sola— perquè una pàgina d'error
// que marxa abans que ningú la llegeixi és una pàgina que no explica res (docs/30, docs/50).
#ifndef LUNOK_KERNEL_APP_ERROR_H
#define LUNOK_KERNEL_APP_ERROR_H

#include <stdint.h>

namespace kernel {
namespace app_error {

// **El que s'ensenya**, amb l'app que ha petat i el text que el intèrpret ha deixat: el missatge i el rastre, amb els salts de
// línia que tingui. No fa res en una placa sense vidre. Torna quan la persona ha tocat el vidre, ha premut una tecla o han
// passat els segons de la pàgina.
void show(const char* id, const char* text);

// **Els segons que la pàgina espera sola, que un cas de prova pot baixar** (docs/80, docs/85): the number is the system's —a person
// reading a traceback needs a while— and a suite that runs a page nobody is going to touch cannot wait it out. Zero is the system's
// own wait and the default.
void set_wait_ms(int64_t ms);

}  // namespace app_error
}  // namespace kernel

#endif  // LUNOK_KERNEL_APP_ERROR_H
