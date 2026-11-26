// **La cara d'una foto: el cau de miniatures, fet pel sistema** (0.192.38, l'amo: *«no han de ser les apps qui facin els
// thumbnails, sinó el sistema. Que revisi si hi ha thumbnail i si no hi ha, el genera. Les apps no tenen que fer res»*).
//
// El format és el del cau del llibre i no un altre (docs/46, C11): un `ICS1`, quatre bits per píxel, **al costat del fitxer,
// amagat i amb el seu nom a dins** —`.<nom>.cache.ics`—, o sigui que qui llegeix la carpeta té la cara del fitxer sense obrir-lo.
// Les tres regles del paper es guarden: un cau que hi és i és **sencer** no s'escriu, l'espai es pregunta **abans** d'escalar, i
// una escriptura que falla **esborra** el que ha deixat (docs/30, docs/46, docs/70).
#ifndef LUNOK_KERNEL_THUMB_H
#define LUNOK_KERNEL_THUMB_H

#include <stddef.h>
#include <stdint.h>

namespace kernel {
namespace thumb {

// El costat més llarg d'un cau si l'app no en demana cap: el mateix número que `epub.thumbnail` (docs/46, C11).
const int32_t kSide = 64;
// El sostre, perquè un cau és una miniatura i no una foto (el mateix que `book::kThumbMax`).
const int32_t kMaxSide = 256;

// **La cara d'una foto, escrita si no hi era**: `path` és el camí d'una porta (`/sd/foto.jpg`) i `out` torna el camí del cau
// —o el del **propi fitxer** quan la foto ja és tan petita que el cau seria el mateix dibuix—. `false` i `why` quan el fitxer
// no és una foto que aquest sistema llegeixi, quan el volum no és escrivible, quan no hi ha lloc per al cau o quan el nom és
// massa llarg per al camí del costat. **Un cau que ja hi és no s'escriu**: el que la mesura diu és si el fitxer fa el que diuen
// les dues paraules de la seva capçalera (docs/46, C11).
bool of_the_picture(const char* path, int32_t max, char* out, size_t out_cap, const char*& why);

// **El cau que hi ha, sense escriure-hi res**: el camí del costat del fitxer si és un `ICS1` sencer, o `nullptr`. És la pregunta
// de qui només vol saber si la cara ja hi és (docs/30, docs/46).
bool the_cache_beside(const char* path, char* out, size_t out_cap);

}  // namespace thumb
}  // namespace kernel

#endif  // LUNOK_KERNEL_THUMB_H
