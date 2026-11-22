// The door of the apps as the interpreter sees it (0.153.0, and 0.154.0 with the list and the manifest apart): the names
// a launcher draws first, the manifest of one app when it asks, and the wish to start one.
//
// **Why this comes from here and not from a walk of `/apps` written in Lua**: the door already merges the two disks,
// refuses a folder that is not an app, reads the manifest and says whether the app runs on this board (docs/40). A
// launcher that walked the disk itself would be a second reader of the same file, and the second one is the one that
// goes wrong the day the format changes. The same goes for the reason a start is refused: it is the system's
// vocabulary, because an app that fails to start and cannot say why leaves a person with a dead screen (docs/30).
#include <stdio.h>
#include <string.h>

#include <new>

#include "hal/hal.h"
#include "kernel/apps.h"
#include "kernel/shell.h"
#include "kernel/vfs.h"
#include "luabind/luabind.h"
#include "pal/pal.h"

namespace kernel {
namespace apps {
namespace {

void say(char* why, size_t cap, const char* text) {
  if (why == nullptr || cap == 0) return;
  snprintf(why, cap, "%s", text);
}

// **The walk of the door that opens no manifest** (0.154.0): `/apps` is the virtual directory that puts the flash's apps
// and the card's together, and the names it answers are the apps the system presents —a folder without a manifest is not
// one of them— (docs/40, docs/70). What it does not say is anything of a manifest, which is the whole point of the two
// calls apart: a launcher draws these names first and asks for the rest one app at a time.
class IdsSink : public hal::ListSink {
 public:
  IdsSink(luabind::AppVisitor visitor, void* context) : visitor_(visitor), context_(context) {}

  bool on_entry(const hal::ListEntry& entry) override {
    if (entry.type != hal::NodeType::kDir) return true;
    luabind::AppInfo info = {};
    snprintf(info.id, sizeof(info.id), "%s", entry.name);
    return visitor_(info, context_);
  }

 private:
  luabind::AppVisitor visitor_;
  void* context_;
};

bool lua_ids(luabind::AppVisitor visitor, void* context) {
  if (visitor == nullptr) return false;
  IdsSink sink(visitor, context);
  return vfs::list(kAppsDoor, sink);
}

// **`apps.info(id)`**: one manifest, read the way `apps info` from the prompt reads it, and the two things a launcher
// asks of it after the name —the picture and whether it is a launcher or an app of another board— (docs/30, docs/40).
bool lua_info(const char* id, luabind::AppInfo& out, char* why, size_t why_cap) {
  if (id == nullptr || id[0] == '\0') {
    say(why, why_cap, "an app with no name");
    return false;
  }
  // **The entry is not on the stack of the app's task**: a whole manifest is 1,1 KB and this call runs in the deepest
  // frame the interpreter has, which is the one docs/75 measures. One at a time is enough: the door loads one manifest
  // and no visitor of this walk loads another (docs/40, docs/75). **I no és un `static`** (0.190.261, l'amo: *«cal
  // estalviar memoria... Pero cosetes petites»*): 772 B que la placa pagava des de l'arrencada per a una crida que fa un
  // visitant d'un llistat. La porta és la mateixa que la del text del manifest a `apps::load` —`pal::Scratch`, que el
  // demana en entrar i el torna en sortir per qualsevol camí—.
  pal::Scratch block(sizeof(Entry), pal::kMemoryKernel);
  if (!block) {
    say(why, why_cap, "there is no memory to read its manifest");
    return false;
  }
  Entry& entry = *new (block.get()) Entry();
  if (!read(id, entry)) {
    say(why, why_cap, "its manifest is not one this system reads");
    return false;
  }
  snprintf(out.id, sizeof(out.id), "%s", entry.id);
  snprintf(out.title, sizeof(out.title), "%s", entry.title);
  // **I la versió, que és el que diu si una cel·la guardada és d'aquesta app o de l'anterior** (0.189.22, docs/30).
  snprintf(out.version, sizeof(out.version), "%s", entry.version);
  // **The picture is a path the disk answers** —`/apps/<id>/res/icon.png`— and not the name the manifest writes: a
  // launcher hands it to `canvas:image` and the system is the one that knows where the app is (docs/30, docs/40).
  if (entry.icon[0] != '\0') {
    snprintf(out.icon, sizeof(out.icon), "/apps/%s/%s", entry.id, entry.icon);
  }
  out.launcher = entry.launcher;
  out.on_card = entry.on_card;
  out.works_here = entry.works_here;
  return true;
}

// **`app.run(id)`**: every way a launcher can be wrong is answered here, before the wish is taken, so the app has a
// reason to show. The name is checked against the door —it is an app, and it runs on this board— and against the app in
// front —a launcher that asks for itself is a loop with a finger on it— (docs/30, docs/90).
bool lua_run(const char* id, bool back, const char* flags, const char* path, char* why,
                size_t why_cap) {
  if (id == nullptr || id[0] == '\0') {
    say(why, why_cap, "an app with no name");
    return false;
  }
  const char* front = shell::app_id();
  if (front != nullptr && front[0] != '\0' && strcmp(front, id) == 0) {
    say(why, why_cap, "that is the app that is asking");
    return false;
  }
  // **The entry is not on the stack of the app's task**: a whole manifest is 1,1 KB and this call runs in the deepest
  // frame the interpreter has, which is the one docs/75 measures. It is one at a time —`app.run` is two hundred lines of
  // Lua away from being re-entered, and a second call in the same turn is refused by the shell— so one entry is enough.
  // **I no és un `static`** (0.190.261): the block is asked when the call starts and given back when it ends, the same
  // door `apps::load` uses for the text of the manifest (docs/40, docs/75).
  pal::Scratch block(sizeof(Entry), pal::kMemoryKernel);
  if (!block) {
    say(why, why_cap, "there is no memory to read its manifest");
    return false;
  }
  Entry& entry = *new (block.get()) Entry();
  if (!read(id, entry)) {
    say(why, why_cap, "there is no such app on this disk");
    return false;
  }
  if (!entry.works_here) {
    char text[96];
    snprintf(text, sizeof(text), "%s runs on another board and this one is %s", id, hal::platform().id());
    say(why, why_cap, text);
    return false;
  }
  return shell::ask_run(id, back, flags, path, why, why_cap);
}

// **`apps.opener(name)`: which app claims that file** (0.190.257). The extension is the question and it is read from the
// last dot, in lower case, by the same door the prompt's `open` uses —so a card written on a computer is the same kind of
// file here— and a hidden name has no opener on purpose: the thumbnail beside a picture is not the picture (docs/30).
// **The first app that claims it wins, and it has to run on this board**: an app of another board cannot open anything
// here, and the door lists it anyway (docs/40).
struct OpenerWalk {
  const char* ext;
  char* id;
  size_t id_cap;
  char* flags;
  size_t flags_cap;
  bool found;
};

bool take_the_opener(const Entry& entry, void* context) {
  OpenerWalk* walk = static_cast<OpenerWalk*>(context);
  if (walk == nullptr || walk->found) return false;
  if (entry.opens_count == 0) return true;
  if (!entry.works_here) return true;
  if (!handles(entry, walk->ext)) return true;
  snprintf(walk->id, walk->id_cap, "%s", entry.id);
  snprintf(walk->flags, walk->flags_cap, "%s", entry.opens_arg);
  walk->found = true;
  return false;
}

bool lua_opener(const char* name, char* id, size_t id_cap, char* flags, size_t flags_cap, char* why,
                size_t why_cap) {
  if (name == nullptr || name[0] == '\0') {
    say(why, why_cap, "a file with no name");
    return false;
  }
  if (name[0] == '.') {
    say(why, why_cap, "a hidden name is not a file anybody opens");
    return false;
  }
  char ext[16];
  if (!extension_of(name, ext, sizeof(ext))) {
    say(why, why_cap, "that name has no extension any app could claim");
    return false;
  }
  OpenerWalk walk;
  walk.ext = ext;
  walk.id = id;
  walk.id_cap = id_cap;
  walk.flags = flags;
  walk.flags_cap = flags_cap;
  walk.found = false;
  for_each(nullptr, &take_the_opener, &walk);
  if (!walk.found) {
    char text[96];
    snprintf(text, sizeof(text), "no app of this system opens a .%s file", ext);
    say(why, why_cap, text);
    return false;
  }
  return true;
}

// **An app that restarts the board** (0.171.0): the page of the eraser offers it when it ends, because an app that has
// emptied the store it is running on is an app nobody wants to keep running. The reason is the app's and goes to the log
// with its name: a restart nobody can explain is a restart a person blames on the board (docs/30, docs/50).
bool lua_reboot(const char* reason) { return shell::reboot(reason); }

}  // namespace

void install_apps_ops() {
  luabind::AppsOps apps_ops = {};
  apps_ops.ids = &lua_ids;
  apps_ops.info = &lua_info;
  apps_ops.opener = &lua_opener;
  luabind::set_apps_ops(apps_ops);

  luabind::AppOps app_ops = {};
  app_ops.run = &lua_run;
  app_ops.views = &views;
  app_ops.push_view = &view_push;
  app_ops.pop_view = &view_pop;
  app_ops.reboot = &lua_reboot;
  luabind::set_app_ops(app_ops);
}

}  // namespace apps
}  // namespace kernel
