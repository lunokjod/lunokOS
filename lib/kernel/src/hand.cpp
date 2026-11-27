// **Els dos models de la lletra escrita a mà** (docs/30, docs/90): where the examples of a zone live, how the glyph the
// interpreter filled becomes an example or an answer, and the file that both of them leave at `/data/handwrite/`.
#include "kernel/hand.h"

#include <stdio.h>
#include <string.h>

#include "hal/log.h"
#include "luabind/luabind.h"
#include "kernel/vfs.h"
#include "pal/pal.h"

namespace kernel {
namespace hand {

namespace {

// **Els noms de les zones són dels dos costats i han de dir el mateix** (docs/30): the app of the training asks for `letters`, the
// band asks for `letters` and the file is `/data/handwrite/letters`. A name that is not one of the two is refused where it
// enters, which is this door.
const char* const kZoneNames[kZones] = {"letters", "symbols"};

// The folder of the model: **el racó de l'app d'entrenament**, que és l'únic lloc on una app pot escriure, i el teclat hi llegeix
// com a sistema (docs/70).
const char kFolder[] = "/data/handwrite";

struct Zone {
  ink::Model model;
  bool asked = false;   // the block of the extra memory has been asked for once
  bool loaded = false;  // the file has been read (or there was none)
};

Zone g_zone[kZones];
ink::Glyph g_glyph;

// **El bloc de les mostres, demanat quan la primera lletra s'escriu** (docs/75): a model that nobody has trained costs nothing,
// and the ten kilobytes of a zone are in the extra memory so the loop's stack and the internal RAM do not pay for it.
bool the_block(uint8_t zone) {
  if (zone >= kZones) return false;
  Zone& one = g_zone[zone];
  if (one.model.examples != nullptr) return true;
  if (one.asked) return false;
  one.asked = true;
  pal::MemoryTag who_asks(pal::kMemoryOther);
  void* block = pal::extra_memory().allocate(sizeof(ink::Example) * kCapacity, false);
  if (block == nullptr) {
    hal::logf(pal::kLogWarn, "HAND", "no room for the %s model: %u examples of %u bytes",
              zone_name(zone), static_cast<unsigned>(kCapacity), static_cast<unsigned>(sizeof(ink::Example)));
    return false;
  }
  one.model.examples = static_cast<ink::Example*>(block);
  one.model.capacity = kCapacity;
  one.model.count = 0;
  return true;
}

// **El camí d'una zona**, dit en un sol lloc: `/data/handwrite/letters` i prou.
void the_path(uint8_t zone, char* out, size_t cap) {
  snprintf(out, cap, "%s/%s", kFolder, zone_name(zone));
}

bool available() { return true; }

}  // namespace

bool zone_from_name(const char* name, uint8_t& out) {
  if (name == nullptr) return false;
  for (uint8_t i = 0; i < kZones; ++i) {
    if (strcmp(name, kZoneNames[i]) == 0) {
      out = i;
      return true;
    }
  }
  return false;
}

const char* zone_name(uint8_t zone) { return (zone < kZones) ? kZoneNames[zone] : "?"; }

ink::Glyph& glyph() { return g_glyph; }

// **El punter de la porta**: the interpreter wants a pointer and the service a reference, and the block is one and not two.
ink::Glyph* the_glyph() { return &g_glyph; }

bool add(uint8_t zone, char text, const ink::Field& where) {
  if (zone >= kZones || text == '\0') return false;
  if (!the_block(zone)) return false;
  // **El que hi ha al disc i el que hi ha a la memòria són el mateix model** (0.192.71): an `add` that did not read the file first
  // would throw away what the person taught in an earlier session —the block starts empty— i la primera lletra d'una sessió
  // esborraria les altres. Es llegeix una vegada i prou.
  if (!g_zone[zone].loaded) (void)load(zone);
  ink::Grid grid;
  if (!ink::raster(g_glyph, where, grid)) {
    hal::logf(pal::kLogInfo, "HAND", "'%c' was not learned: the drawing has no shape", text);
    return false;
  }
  Zone& one = g_zone[zone];
  // **El log diu què s'ha après i què no** (0.192.74, l'amo: *«Al guardar les dades ho deixes clar al log i el resultat?»*): a
  // person watching the console has to be able to tell a letter that went in from one the model already had, i el compte de la
  // lletra i el del model sencer són el que diu si l'entrenament avança (docs/30, docs/90).
  if (!one.model.add(grid, text)) {
    const char* why = !one.model.usable()          ? "the model has no block"
                      : (one.model.count >= one.model.capacity) ? "the model is full"
                                                                : "that drawing is already one of its examples";
    hal::logf(pal::kLogInfo, "HAND", "'%c' was not learned in %s: %s", text, zone_name(zone), why);
    return false;
  }
  hal::logf(pal::kLogInfo, "HAND", "'%c' learned in %s: %u example(s) of it, %u in all", text, zone_name(zone),
            static_cast<unsigned>(one.model.examples_of(text)), static_cast<unsigned>(one.model.count));
  return true;
}

ink::Answer guess(uint8_t zone, const ink::Field& where) {
  ink::Answer none;
  if (zone >= kZones) return none;
  // **El model es llegeix la primera vegada que es pregunta** (docs/30): the band asks when it has to write, and what the app of
  // the training has just saved is what it reads.
  if (!g_zone[zone].loaded) (void)load(zone);
  ink::Grid grid;
  if (!ink::raster(g_glyph, where, grid)) return none;
  return g_zone[zone].model.guess(grid);
}

uint16_t examples(uint8_t zone) { return (zone < kZones) ? g_zone[zone].model.count : 0; }

uint16_t examples_of(uint8_t zone, char text) {
  if (zone >= kZones) return 0;
  return g_zone[zone].model.examples_of(text);
}

// **Llegir el model del disc**, una vegada per zona i sota demanda: the band asks the first time it has to write a character, i
// el que l'app d'entrenament acaba de desar és el que es llegeix, perquè `save` en té prou de tornar-ho a demanar.
bool load(uint8_t zone) {
  if (zone >= kZones) return false;
  if (!the_block(zone)) return false;
  Zone& one = g_zone[zone];
  one.model.count = 0;
  one.loaded = true;
  char path[48];
  the_path(zone, path, sizeof(path));
  // **El fitxer cap en un bloc de la pila?** No: three kilobytes of a full model. It is asked from the extra memory and given
  // back at the end of this function, que és el que `pal::Scratch` fa.
  pal::Scratch room(ink::kHeaderBytes + static_cast<size_t>(kCapacity) * ink::kExampleBytes, pal::kMemoryOther);
  if (!room) return false;
  size_t len = 0;
  if (!vfs::read_file(path, static_cast<char*>(room.get()), ink::kHeaderBytes + static_cast<size_t>(kCapacity) * ink::kExampleBytes, len)) {
    // **Un model que no hi és no és una fallada**: a person who has not trained anything yet has no file, and the band has to
    // say "no" and not shout (docs/30).
    hal::logf(pal::kLogInfo, "HAND", "there is no %s model yet: %s", zone_name(zone), path);
    return false;
  }
  if (!ink::load(one.model, static_cast<const uint8_t*>(room.get()), len)) {
    hal::logf(pal::kLogWarn, "HAND", "%s is not a model of this grid: it is not read", path);
    return false;
  }
  hal::logf(pal::kLogInfo, "HAND", "the %s model: %u example(s), %u B from %s", zone_name(zone),
            static_cast<unsigned>(one.model.count), static_cast<unsigned>(len), path);
  return one.model.count > 0;
}

// **Desar el model** (docs/70): the folder is the app's and the kernel makes it if it is not there —the app asked to save, i un
// desament que falla perquè falta una carpeta és un desament que no diu per què—.
bool save(uint8_t zone) {
  if (zone >= kZones) return false;
  if (!the_block(zone)) return false;
  Zone& one = g_zone[zone];
  char path[48];
  the_path(zone, path, sizeof(path));
  hal::IStorage* storage = nullptr;
  char rel[96];
  if (!vfs::resolve(kFolder, storage, rel, sizeof(rel)) || storage == nullptr) return false;
  hal::Stat info;
  if (!storage->stat(rel, info)) (void)storage->mkdir(rel);
  if (!vfs::resolve(path, storage, rel, sizeof(rel)) || storage == nullptr) return false;

  const size_t bytes = ink::blob_bytes(one.model);
  pal::Scratch room(bytes, pal::kMemoryOther);
  if (!room) return false;
  const size_t wrote = ink::save(one.model, static_cast<uint8_t*>(room.get()), bytes);
  if (wrote == 0) return false;
  hal::FileSlot file;
  if (!storage->open(rel, hal::OpenMode::kWrite, file)) {
    hal::logf(pal::kLogWarn, "HAND", "%s would not open for writing", path);
    return false;
  }
  const int32_t put = storage->write(file, room.get(), static_cast<uint32_t>(wrote));
  storage->close(file);
  if (put != static_cast<int32_t>(wrote)) {
    // **Un fitxer a mitges no és un model**: the bytes that did go in are a file the band would refuse; it is taken away so the
    // next reading says "no model yet" and not "not a model of this grid" (docs/70).
    (void)storage->remove(rel);
    hal::logf(pal::kLogWarn, "HAND", "%s was left at %d of %u bytes: it is not a model", path, static_cast<int>(put),
              static_cast<unsigned>(wrote));
    return false;
  }
  hal::logf(pal::kLogInfo, "HAND", "the %s model saved: %u example(s), %u B in %s", zone_name(zone),
            static_cast<unsigned>(one.model.count), static_cast<unsigned>(wrote), path);
  one.loaded = true;
  return true;
}

uint16_t the_capacity() { return kCapacity; }

// **Esborrar les mostres d'un caràcter** (0.192.86): the only way a person has of taking back a sample that is wrong —and it has
// to be written, or el fitxer el tornaria a portar al proper arrencament— (docs/30, docs/70).
uint16_t forget_of(uint8_t zone, char text) {
  if (zone >= kZones || text == '\0') return 0;
  if (!the_block(zone)) return 0;
  if (!g_zone[zone].loaded) (void)load(zone);
  const uint16_t removed = g_zone[zone].model.remove_of(text);
  if (removed > 0) {
    (void)save(zone);
    hal::logf(pal::kLogInfo, "HAND", "%u example(s) of '%c' forgotten in %s", static_cast<unsigned>(removed), text,
              zone_name(zone));
  }
  return removed;
}

void forget(uint8_t zone) {
  if (zone >= kZones) return;
  g_zone[zone].model.count = 0;
  g_zone[zone].loaded = false;
}

// **Quantes zones hi ha, per a la taula de `caps`**: the interpreter builds the list of names from this and asks for each one,
// o sigui que el nom d'una zona no s'escriu mai dues vegades (docs/30).
uint8_t zones() { return kZones; }

void install() {
  luabind::HandOps ops = {};
  ops.available = &available;
  ops.glyph = &the_glyph;
  ops.zone = &zone_from_name;
  ops.zone_name = &zone_name;
  ops.zones = &zones;
  ops.add = &add;
  ops.guess = &guess;
  ops.save = &save;
  ops.load = &load;
  ops.examples = &examples;
  ops.examples_of = &examples_of;
  ops.forget = &forget;
  ops.forget_of = &forget_of;
  ops.capacity = &the_capacity;
  luabind::set_hand_ops(ops);
}

}  // namespace hand
}  // namespace kernel
