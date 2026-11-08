// The door of the apps as the interpreter sees it (0.153.0): the list a launcher draws and the wish to start one.
//
// **Why the list comes from here and not from a walk of `/apps` written in Lua**: the door already merges the two disks,
// refuses a folder that is not an app, reads the manifest and says whether the app runs on this board (docs/40). A
// launcher that walked the disk itself would be a second reader of the same file, and the second one is the one that
// goes wrong the day the format changes. The same goes for the reason a start is refused: it is the system's
// vocabulary, because an app that fails to start and cannot say why leaves a person with a dead screen (docs/30).
#include <stdio.h>
#include <string.h>

#include "hal/hal.h"
#include "kernel/apps.h"
#include "kernel/shell.h"
#include "luabind/luabind.h"

namespace kernel {
namespace apps {
namespace {

struct LuaWalk {
  luabind::AppVisitor visitor;
  void* context;
};

bool lua_visitor(const Entry& entry, void* context) {
  LuaWalk* walk = static_cast<LuaWalk*>(context);
  luabind::AppInfo info = {};
  snprintf(info.id, sizeof(info.id), "%s", entry.id);
  snprintf(info.title, sizeof(info.title), "%s", entry.title);
  // **The picture is a path the disk answers** —`/apps/<id>/res/icon.png`— and not the name the manifest writes: a
  // launcher hands it to `canvas:image` and the system is the one that knows where the app is (docs/30, docs/40).
  if (entry.icon[0] != '\0') {
    snprintf(info.icon, sizeof(info.icon), "/apps/%s/%s", entry.id, entry.icon);
  }
  info.launcher = entry.launcher;
  info.on_card = entry.on_card;
  info.works_here = entry.works_here;
  return walk->visitor(info, walk->context);
}

bool lua_each(luabind::AppVisitor visitor, void* context) {
  if (visitor == nullptr) return false;
  LuaWalk walk = {visitor, context};
  // The walk hands one manifest at a time to the visitor, and the visitor copies it into the interpreter's heap: nothing
  // of the size of the list is ever in C (docs/75).
  for_each(nullptr, &lua_visitor, &walk);
  return true;
}

void say(char* why, size_t cap, const char* text) {
  if (why == nullptr || cap == 0) return;
  snprintf(why, cap, "%s", text);
}

// **`app.run(id)`**: every way a launcher can be wrong is answered here, before the wish is taken, so the app has a
// reason to show. The name is checked against the door —it is an app, and it runs on this board— and against the app in
// front —a launcher that asks for itself is a loop with a finger on it— (docs/30, docs/90).
bool lua_run(const char* id, bool back, char* why, size_t why_cap) {
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
  static Entry entry;
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
  return shell::ask_run(id, back, why, why_cap);
}

}  // namespace

void install_apps_ops() {
  luabind::AppsOps apps_ops = {};
  apps_ops.each = &lua_each;
  luabind::set_apps_ops(apps_ops);

  luabind::AppOps app_ops = {};
  app_ops.run = &lua_run;
  luabind::set_app_ops(app_ops);
}

}  // namespace apps
}  // namespace kernel
