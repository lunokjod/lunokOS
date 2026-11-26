// **La pantalla, desada com una foto** (0.192.59, l'amo: *«Una comanda per fer captura de la pantalla i escollir format de sortida (per
// defecte ics) "screenshoot /sd/capture.ics"»*).
//
// **Els píxels no passen per cap bloc**: la imatge la té el vidre i aquesta porta la llegeix **una fila a la vegada**
// (`hal::ISurface::read_row`) i l'escriu mentre la llegeix, o sigui que una captura de la pàgina sencera no demana els
// dos-cents seixanta kilobytes que faria una còpia (docs/50, docs/75).
//
// **El format el diu l'extensió** i n'hi ha dos, que són els dos que aquest sistema sap escriure: **`ICS1`** —quatre bits per
// píxel, el fitxer més barat que hi ha aquí i el que aquesta casa llegeix com qualsevol foto— i **`PNG`** de vuit bits de gris,
// que és el que obre qualsevol programa. Un camí sense extensió és un `ICS1` i l'extensió s'hi afegeix; qualsevol altra cosa es
// refusa amb el motiu i no s'endevina (docs/30, docs/46, docs/70).
#ifndef LUNOK_KERNEL_SCREENSHOT_H
#define LUNOK_KERNEL_SCREENSHOT_H

#include <stddef.h>

namespace kernel {
namespace screenshot {

// **Una captura del vidre a `path`**. `written` torna el camí de debò —amb l'extensió que s'hi ha posat— perquè la línia que una
// persona llegeix digui el fitxer que hi ha i no el que es va demanar. Els dos textos són el motiu quan torna fals —un codi `E_*` i
// una frase—, i una escriptura que falla a mig camí **esborra** el que ha deixat: un fitxer de dos megabytes a mitges és una foto
// que ningú no pot obrir i que ningú no sap que és a mitges (docs/70, docs/75).
bool capture(const char* path, char* written, size_t cap, const char*& code, const char*& reason);

}  // namespace screenshot
}  // namespace kernel

#endif  // LUNOK_KERNEL_SCREENSHOT_H
