// **El llistat d'un directori de GitHub, llegit a trossos** (docs/30, docs/70).
//
// La pàgina del navegador (`github.com/…/tree/main/apps`) és HTML i no és el que vol el sistema: el que vol és
// la llista, i l'API de continguts la dona en JSON, on una carpeta és un objecte amb `"type": "dir"`. La
// resposta d'aquest repositori són uns **44 KB** —la URL de cada entrada és la major part— i la placa no els
// ha de tenir enlloc: aquest lector passa pel fitxer amb una finestra de dos kilobytes i en treu els noms d'un
// en un, sense construir cap arbre i sense una taula de noms que creixi (docs/75).
//
// No és un lector de JSON general i no ho vol ser: entén el que aquesta API escriu —objectes amb claus i
// cadenes— i prou. Un document que no és el que espera dona la volta amb `false` i no penja.
#ifndef LUNOK_KERNEL_REPOSITORY_H
#define LUNOK_KERNEL_REPOSITORY_H

#include <stddef.h>
#include <stdint.h>

#include "hal/hal.h"

namespace kernel {
namespace repository {

// **Què és una entrada d'un llistat de continguts**: una carpeta, i un fitxer amb una adreça d'on baixar-lo tal com
// és (docs/30, docs/70).
enum class Kind : uint8_t { kFile, kDir };

// **Els noms de les carpetes d'un llistat de continguts, un darrere l'altre.** `open` diu si el fitxer s'ha
// pogut obrir; `next` torna el nom següent (sense cometes) i `false` quan no n'hi ha més o quan el document no
// és el que sembla. El nom es copia a `out` i el lector no en guarda cap llista.
class Listing {
 public:
  Listing() = default;
  ~Listing();
  Listing(const Listing&) = delete;
  Listing& operator=(const Listing&) = delete;

  bool open(const char* path);
  void close();
  bool next(char* out, size_t cap);

  // **El següent element, sigui fitxer o carpeta** (0.86.0): una instal·lació vol els dos i, per a un fitxer,
  // l'adreça de descàrrega que aquesta API dona al costat del nom. `next` segueix essent el camí curt de la
  // llista de carpetes (docs/30, docs/70).
  bool next_entry(Kind& kind, char* name, size_t name_cap, char* url, size_t url_cap);

 private:
  // La finestra del fitxer: dos kilobytes, mitja fila de flash i més que un objecte del llistat.
  static const uint16_t kChunk = 2048;
  // **Un nom de directori hi cap sencer**: quaranta-vuit bytes són un nom de fitxer de qualsevol disc, i el
  // que no hi cap es talla en comptes de desbordar (docs/75).
  static const uint8_t kName = 48;
  // **Una adreça de descàrrega crua hi cap**: `raw.githubusercontent.com/<amo>/<repo>/main/apps/<app>/<nom>` és
  // cent setze bytes amb noms de trenta-dos, i cent quaranta-vuit són marge (docs/30, docs/75).
  static const uint8_t kUrl = 148;

  bool fill();
  int peek();
  int take();
  void skip_space();
  // Una cadena amb les cometes, amb `\"` i `\\` com a únics escapes (el que aquesta API fa servir).
  bool read_string(char* out, size_t cap);
  // El valor següent com una paraula sense cometes.
  bool read_word(char* out, size_t cap);

  hal::IStorage* storage_ = nullptr;
  hal::FileSlot slot_ = {};
  char buffer_[kChunk] = {};
  uint16_t at_ = 0;
  uint16_t len_ = 0;
  bool ended_ = false;
  char pending_[kName] = {};
  bool have_pending_ = false;
  // L'adreça de descàrrega de l'objecte que s'està llegint, si en té: ve abans del `type` i es guarda igual que el
  // nom (docs/70).
  char pending_url_[kUrl] = {};
  bool have_url_ = false;
};

// **L'element n-èssim d'un llistat, començant per zero.** La instal·lació d'una app recorre el document un element
// a la vegada i el torna a llegir per cada un: un pla de tots els fitxers seria una taula d'adreces a la RAM i el
// llistat és un fitxer que es llegeix en unes dècimes (docs/75). False quan l'índex passa del final o el document
// no és el que sembla.
bool entry_at(const char* path, uint32_t index, Kind& kind, char* name, size_t name_cap, char* url,
              size_t url_cap);

}  // namespace repository
}  // namespace kernel

#endif  // LUNOK_KERNEL_REPOSITORY_H
