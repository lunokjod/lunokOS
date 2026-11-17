// Apps are not kept in a list. /apps is walked when someone asks, and one
// manifest is in memory at a time, so there is no limit on how many apps there
// may be. See docs/40.
#ifndef LUNOK_KERNEL_APPS_H
#define LUNOK_KERNEL_APPS_H

#include <stddef.h>
#include <stdint.h>

#include "kernel/vfs.h"

namespace kernel {
namespace apps {

struct Entry {
  // **The id is the name the door presents** (0.88.20): `squares` for the app of the flash and **`*squares`** for the
  // copy of the card (0.164.0), which is the mark of a redirect, so it has room for the mark on top of the longest
  // folder the disks carry.
  char id[40];
  // **The name a person reads, and for a redirect the word of its unit at the end**: `Editor-SD` is the card's copy
  // and `Editor-Littlefs` the flash's, which is the unit the redirect resolves to (0.164.0, docs/40, docs/70).
  char title[48];
  char version[16];
  char entry_file[48];
  // **The extensions this app handles** (0.120.0): one name or several, lowercase and without the dot. The
  // manifests of the apps of PaperOS wrote a list from the first day —`"opens": "txt md lua json"`, fifteen of
  // them in its Editor— and this system read a single one; now a manifest may say a list and every one of them
  // goes to the same handler (docs/40). An app that handles none is the same as before: it opens nothing.
  static const uint32_t kMaxOpens = 8;
  char opens[kMaxOpens][12];
  uint32_t opens_count;
  // **The switches the app wants when it is handed a file** (0.120.0): `opensArgs`, which PaperOS's Editor
  // writes as `-ro`. The system does not read them: it hands them to the app in `app.args.flags`, because only
  // the app knows what its own switches mean (docs/30, docs/40).
  char opens_arg[32];
  // **The picture the app wants on the glass before it starts** (0.114.0): a file inside the app, written in
  // the manifest as `"splash": "res/logo.png"`, which the system paints washed whole and in the middle with
  // the same call the moments use and takes away with the first page the app presents. It is decoration and
  // not a requirement: an app without one, or with a name that is not a picture, runs all the same (docs/40).
  char splash[48];
  // **The badge of the app** (0.120.0): `icon`, the picture a listing would paint next to the name —the app
  // folders of PaperOS carry an `icon.icn`—. There is no launcher on the glass yet, so this system reads it,
  // checks it as it checks the splash and says it in `apps info`; it paints nothing with it (docs/40).
  char icon[48];
  // **Who wrote the app and what it is** (0.120.0): forty-eight and forty-nine of the sixty-one apps of
  // `apps/` write these two and the system read neither. They are decoration —a manifest without them is still
  // an app— and `apps info` is where they are said (docs/40).
  char author[32];
  char description[160];

  // The boards this app says it runs on, as the manifest writes them. A name is a
  // board (`m5stack-atom-matrix`) or a chip family (`esp32`), and every board of
  // that family accepts an app that says the family. An app that declares none runs
  // nowhere: the list is not optional (docs/40).
  static const uint32_t kMaxBoards = 8;
  char boards[kMaxBoards][24];
  uint32_t board_count;
  // False when the list above does not name this board nor its family: the app is
  // still an app of the disk —`apps` lists it and says what it needs— but it is not
  // one this board can run (docs/40).
  bool works_here;
  // **An app that is a launcher of its own** (0.153.0): the manifest says `"launcher": true` and a launcher's grid does
  // not list it. It is still an app —`apps` shows it, `run` starts it, it opens the files it handles— and the system
  // does not guess it from a title or an id: the app says it (docs/30, docs/40).
  bool launcher;
  // **Whether the app is an administrator** (0.189.99): `"admin": true` in the manifest is the only way an app gets the
  // `admin` table of the Lua machine —the calls that change **the system itself** and not the page: the strip and the card
  // of the system, and whatever comes after them— and for every other app the table is not there at all, so a page that
  // was written without the key cannot call it by accident and a hand that types `admin.` gets a nil (docs/30, docs/40).
  // It is a manifest key and not a permission anybody grants at run time: the powers are read with the app, before the
  // machine exists, and an app that is copied to the card carries its own answer with it (docs/45).
  bool admin;
  // **Which half of the door answered** (0.88.20): the same id may sit on both disks and the door presents them apart, so
  // the listing says which of the two it read (docs/40).
  bool on_card;
};

// True when the app says it runs on this board: because it names the board itself
// or the family the board belongs to. An app with no list runs nowhere. It is a
// function so that the kernel and the tests ask the question the same way (docs/40).
bool compatible(const Entry& entry, const char* board_id, const char* family);

// Return false to stop the walk.
typedef bool (*Visitor)(const Entry& entry, void* context);

// **The one door of the apps** (0.88.20): `/apps` is the virtual directory that puts the flash's `apps/` and the card's
// `apps/` together, and it is the only place the system looks —the listing, the `run` and the files of an app all go
// through it—. A name the flash has and the card also has is presented **once as `name` and once as `*name`** (0.164.0),
// so the two can be read and run apart, and what the door presents is what the system decides to present: a folder that
// is not an app is not in it, and a disk the system did not offer is never read (docs/40, docs/70).
extern const char kAppsDoor[];

// **Whether there is a card in the slot**: the mount says so and not a folder, because a card the system could not
// mount is not a place to look for anything in (docs/50, docs/70).
bool card_here();

// **The id of the door of an app, `name` or `*name`**: `/apps/<id>`, which the filesystem answers from the disk that
// has that app —the mark of a redirect is answered by the disk the plain copy is not on— (0.164.0, docs/40, docs/70).
bool root_of(const char* id, char* out, size_t cap);

// **What the door of the apps presents** (0.88.20): the kernel hands this to the VFS, which asks it for every name of
// `/apps` and lists only the ones it accepts. `disk` is the half of the door the name came from (`/littlefs/apps` or
// `/sd/apps`) and `name` is the folder on that disk: a folder that carries a `manifest.json` is an app there. An app that
// names another board is still presented —the listing says what it needs— because hiding it would leave a person with an
// app on their disk and no trace of it (docs/40, docs/70).
bool is_an_app(const char* disk, const char* name);

// Walks the door of the apps and loads one manifest at a time. When only_id is not null it stops
// after that app. Returns how many apps loaded correctly.
uint32_t for_each(const char* only_id, Visitor visitor, void* context);

// Normalises an extension the way a manifest writes it: "TXT", ".txt" and "txt"
// are the same thing. Writes at most cap bytes and returns false when there is
// nothing usable.
bool normalise_extension(const char* text, char* out, size_t cap);

// True when the app says it opens that extension (already normalised). An app
// that declares none opens nothing, which is not the same as opening everything
// (docs/40).
bool handles(const Entry& entry, const char* ext);

// The whole list of an app, written the way a manifest writes it —`"txt md lua"`—, which is what
// `app.info().opens` answers and what `apps info` shows. 96 B is the most it can be: eight names of
// eleven bytes and the seven spaces between them. Returns how many bytes it wrote.
size_t opens_text(const Entry& entry, char* out, size_t cap);

// The extension of a path, normalised. False when the name has none.
bool extension_of(const char* path, char* out, size_t cap);

// Reads the manifest of one app. False when it is not an app of this build: no
// title, no entry, an entry file that is not there, no `boards` list, or a list
// that does not name this board. A caller that does not need the answer can use
// `for_each`, which walks the directory and skips those (docs/40).
bool read(const char* id, Entry& out);

// **The door as the interpreter sees it** (0.153.0): the list `apps.list()` draws and the wish `app.run` makes. It is
// installed once, at boot, and it lives here because the door is what knows what an app is: the walk merges the two
// disks, skips a folder that is not an app and reads the manifest, and a launcher doing that from Lua would be a second
// reader of the same file (docs/30, docs/40).
void install_apps_ops();

// The first app (in directory order) that declares "opens" for ext. When more
// than one does, matches says how many; the caller decides what to say.
bool find_for_extension(const char* ext, Entry& out, uint32_t& matches);

// **The views of the app in front, on top of its first one** (0.163.0): an app that shows a detail over the page it
// came from says so with `app.view_push()` and gives it back with `app.view_pop()`, and this is the number the system
// reads when a finger asks to go back —zero means the app is on its only page, which is the page a pinch closes—.
// **The counter lives here and not in the app** because a pinch means the same wish everywhere and an app that had to
// keep the number itself is an app that can get it wrong (docs/30, docs/46). It starts at zero with every app and it
// goes back to zero when one leaves (docs/30).
uint8_t views();
uint8_t view_push();
uint8_t view_pop();
void reset_views();

// **Una app marxa, i només l'app** (0.189.157, l'amo: *«Permet desinstalarla (esborrarla)»*): the folder of the app on the
// disk it came from goes with everything inside it, and **what the app had left in the system goes with it**: the alarms it
// armed —una alarma d'una app que ja no hi és despertaria la placa per a res— and its session blocks, in the memory and on
// the disk. **Els seus ajustos i la seva dada es queden** (`/cfg/<id>` and `/data/<id>`): those are what a person wrote and not
// the app, i esborrar els apunts d'algú perquè ha tret una app és una decisió que no es pren per ell (docs/30, docs/40, docs/70).
// The three numbers say what went, and the disk says where it was. It is the one road: the prompt's `apps remove` and the
// `admin.uninstall` of an app both come here (docs/30).
struct Removing {
  vfs::Dropped gone;         // the files and the folders of the app
  uint32_t alarms = 0;       // les alarmes que havia armat
  // **La cau, amb les dues meitats i no la suma** (0.189.164): a session that is in the memory and on the disk is **one**
  // session in two places, i sumar-les deia «6» quan n'hi havia tres —el prompt i la fitxa d'una app ho deien així—. Són
  // els dos números que `session::forget` ja dona i que el registre ja escrivia (docs/30, docs/75).
  uint32_t session_blocks = 0;  // els blocs de la memòria
  uint32_t session_files = 0;   // els fitxers de `/session/<id>`
  uint32_t session_bytes = 0;   // el que hi havia a la memòria (els fitxers del disc no es compten: docs/75)
  char disk[48] = {0};       // on era, amb el nom del disc i no el de la porta
  // **Per què no s'ha pogut, quan no s'ha pogut**: una carpeta que no hi és i una carpeta que hi és però no és cap app són
  // dos errors diferents i el prompt els diu amb dues frases (docs/40). La porta de Lua només en fa un, perquè una pàgina
  // que pregunta per un nom sap si el nom era seu o no.
  enum class Missing { kNone, kNotThere, kNoManifest, kNotDropped };
  Missing missing = Missing::kNone;
};
bool remove(const char* id, Removing& out);

// **La llista d'apps té un número, i puja quan la llista canvia** (0.189.23): `repo install <id>` i `apps remove <id>` són
// els dos moments en què el conjunt d'apps canvia amb la placa encesa, i qui dibuixa aquesta llista és l'app que `cfg home`
// nomena. El número és el que deixa que una pàgina sàpiga que la seva vista és d'un conjunt d'abans (docs/30, docs/40).
uint32_t generation();

// **Un canvi, esperant el torn que el diu**: les dues comandes corren on la coroutine de l'app pot ser a qualsevol lloc, i
// un ganxo es crida **entre dos torns** i enlloc més —com `on_alarm`, docs/40—, així que el canvi es deixa aquí i el bucle
// del nucli el lliura. **Només se'n guarda el número i el verb** perquè qui el rep no mira quin: torna a preguntar la llista
// sencera a la porta, i el que vol saber és que n'hi ha una de nova. L'`id` el diu el registre en el moment del canvi
// (`the list changed (install squares), generation 3`) i no es guarda: són quaranta bytes de DRAM que la placa no té
// (docs/30, docs/75).
//
// Diu que la llista ha canviat: el número puja i el canvi espera `take_the_change`.
void note_a_change(const char* what, const char* id);

// El canvi que espera: el número de la llista i si el verb era `install`. `false` quan no n'hi ha cap.
bool take_the_change(uint32_t& generation, bool& installed);
}  // namespace apps
}  // namespace kernel

#endif  // LUNOK_KERNEL_APPS_H
