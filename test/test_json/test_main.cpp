// Tests for the pull-style JSON helpers and for path resolution.
//
// Both are pure logic on purpose: they are the two pieces that decide whether an
// app is found, and neither needs a real disk to be checked.
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <unity.h>

#include "emu/options.h"

#include "hal/hal.h"
#include "json/json.h"
#include "kernel/apps.h"
#include "kernel/cfg.h"
#include "kernel/vfs.h"

void setUp(void) {}

void tearDown(void) {}

namespace {

// Every directory on the way, because the volume has to exist before the boot looks for it: a backend that finds no
// directory is a board with no disk (docs/85).
void make_dirs(const char* path) {
  char partial[256];
  const size_t len = strlen(path);
  if (len >= sizeof(partial)) return;
  memcpy(partial, path, len + 1);
  for (size_t i = 1; i <= len; ++i) {
    if (partial[i] == '/' || partial[i] == '\0') {
      const char saved = partial[i];
      partial[i] = '\0';
      ::mkdir(partial, 0777);  // already there is not an error
      partial[i] = saved;
    }
  }
}

// **A disk that comes with the folder of the apps**: `/apps` is a mount only when `/littlefs/apps` is there
// (0.88.2), so the two tests about that mount make their own disk first —a directory of the run's and not the
// repository's— and then the table, the way a board boots on a disk that carries apps (docs/70, docs/85).
void mount_a_disk_with_apps(void) {
  static char root[96];
  if (root[0] == '\0') {
    snprintf(root, sizeof(root), "/tmp/lunok-json-XXXXXX");
    if (mkdtemp(root) == nullptr) root[0] = '\0';
    char volume[128];
    snprintf(volume, sizeof(volume), "%s/littlefs", root);
    make_dirs(volume);
    emu::Options opts{};
    opts.fs_dir = root;
    emu::set_options(opts);
    if (hal::IStorage* storage = hal::platform().storage("/littlefs")) storage->mkdir("/apps");
  }
  kernel::vfs::init(hal::platform());
}
}  // namespace

void test_extension_is_read_the_same_way_it_is_declared(void) {
  char out[16];
  // A manifest may write it with a dot, with capitals, or plain.
  TEST_ASSERT_TRUE(kernel::apps::normalise_extension(".EPUB", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("epub", out);
  TEST_ASSERT_TRUE(kernel::apps::normalise_extension("epub", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("epub", out);
  TEST_ASSERT_FALSE(kernel::apps::normalise_extension("", out, sizeof(out)));
  TEST_ASSERT_FALSE(kernel::apps::normalise_extension("no/good", out, sizeof(out)));
}

void test_extension_of_a_path(void) {
  char out[16];
  TEST_ASSERT_TRUE(kernel::apps::extension_of("/res/hola.txt", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("txt", out);
  TEST_ASSERT_TRUE(kernel::apps::extension_of("hola.TXT", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("txt", out);
  // A dot in a directory name is not an extension.
  TEST_ASSERT_FALSE(kernel::apps::extension_of("/res.old/hola", out, sizeof(out)));
  TEST_ASSERT_FALSE(kernel::apps::extension_of("/res/hola", out, sizeof(out)));
  TEST_ASSERT_FALSE(kernel::apps::extension_of("/res/", out, sizeof(out)));
}

void test_reads_a_string_field(void) {
  const char text[] = "{\"title\":\"Hello\",\"version\":\"0.1.0\"}";
  char out[32];
  TEST_ASSERT_TRUE(lunok::json::get_string(text, sizeof(text) - 1, "title", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("Hello", out);
}

void test_reads_a_field_in_the_middle(void) {
  const char text[] = "{\"a\":\"1\",\"version\":\"2.3\",\"z\":\"9\"}";
  char out[32];
  TEST_ASSERT_TRUE(lunok::json::get_string(text, sizeof(text) - 1, "version", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("2.3", out);
}

void test_missing_key_fails(void) {
  const char text[] = "{\"title\":\"Hello\"}";
  char out[32];
  TEST_ASSERT_FALSE(lunok::json::get_string(text, sizeof(text) - 1, "entry", out, sizeof(out)));
}

void test_key_text_inside_a_value_is_not_a_key(void) {
  const char text[] = "{\"description\":\"say \\\"title\\\": x\",\"title\":\"Real\"}";
  char out[32];
  TEST_ASSERT_TRUE(lunok::json::get_string(text, sizeof(text) - 1, "title", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("Real", out);
}

void test_reads_an_integer(void) {
  const char text[] = "{\"mem_kb\":64}";
  long value = 0;
  TEST_ASSERT_TRUE(lunok::json::get_int(text, sizeof(text) - 1, "mem_kb", value));
  TEST_ASSERT_EQUAL_INT(64, static_cast<int>(value));
}

void test_non_numeric_value_is_not_an_integer(void) {
  const char text[] = "{\"mem_kb\":\"64\"}";
  long value = 0;
  TEST_ASSERT_FALSE(lunok::json::get_int(text, sizeof(text) - 1, "mem_kb", value));
}

// **A boolean is the word and nothing else** (0.153.0): the `launcher` of a manifest is `true` or `false`, and a reader
// that took the first four letters of `trueish` or read `"yes"` as a value would make an app say what it does not
// (docs/40).
void test_reads_a_boolean(void) {
  const char yes[] = "{\"launcher\": true}";
  const char no[] = "{\"launcher\": false}";
  const char spaced[] = "{\"launcher\" :  true , \"title\": \"A\"}";
  const char word[] = "{\"launcher\": \"true\"}";
  const char prefix[] = "{\"launcher\": trueish}";
  bool value = false;
  TEST_ASSERT_TRUE(lunok::json::get_bool(yes, sizeof(yes) - 1, "launcher", value));
  TEST_ASSERT_TRUE(value);
  TEST_ASSERT_TRUE(lunok::json::get_bool(no, sizeof(no) - 1, "launcher", value));
  TEST_ASSERT_FALSE(value);
  TEST_ASSERT_TRUE(lunok::json::get_bool(spaced, sizeof(spaced) - 1, "launcher", value));
  TEST_ASSERT_TRUE(value);
  // A value that is not the word is not a boolean, and the caller can tell it from nothing being said.
  TEST_ASSERT_FALSE(lunok::json::get_bool(word, sizeof(word) - 1, "launcher", value));
  TEST_ASSERT_FALSE(lunok::json::get_bool(prefix, sizeof(prefix) - 1, "launcher", value));
  TEST_ASSERT_TRUE(lunok::json::has_key(word, sizeof(word) - 1, "launcher"));
  TEST_ASSERT_FALSE(lunok::json::has_key(yes, sizeof(yes) - 1, "splash"));
}

void test_apps_alias_resolves_inside_the_backend(void) {
  mount_a_disk_with_apps();
  hal::IStorage* storage = nullptr;
  char rel[128];
  TEST_ASSERT_TRUE(kernel::vfs::resolve("/apps/demo/manifest.json", storage, rel, sizeof(rel)));
  TEST_ASSERT_NOT_NULL(storage);
  TEST_ASSERT_EQUAL_STRING("apps/demo/manifest.json", rel);
}

void test_root_mount_strips_its_prefix(void) {
  kernel::vfs::init(hal::platform());
  hal::IStorage* storage = nullptr;
  char rel[128];
  TEST_ASSERT_TRUE(kernel::vfs::resolve("/littlefs/apps/demo/x.lua", storage, rel, sizeof(rel)));
  TEST_ASSERT_EQUAL_STRING("apps/demo/x.lua", rel);
}

void test_parent_segments_are_refused(void) {
  kernel::vfs::init(hal::platform());
  hal::IStorage* storage = nullptr;
  char rel[128];
  TEST_ASSERT_FALSE(kernel::vfs::resolve("/apps/../etc", storage, rel, sizeof(rel)));
}

void test_unknown_root_is_refused(void) {
  kernel::vfs::init(hal::platform());
  hal::IStorage* storage = nullptr;
  char rel[128];
  TEST_ASSERT_FALSE(kernel::vfs::resolve("/nowhere/x", storage, rel, sizeof(rel)));
}

namespace {

class CountingSink : public hal::ListSink {
 public:
  bool on_entry(const hal::ListEntry& entry) override {
    (void)entry;
    ++count;
    return true;
  }
  uint32_t count = 0;
};


}  // namespace

void test_root_lists_the_mounts(void) {
  mount_a_disk_with_apps();
  // `/cfg` is not one of the mounts the VFS builds: it is a door, and it is the settings
  // policy that opens it on a unit (docs/70). Without this the table is one row shorter.
  kernel::cfg::boot(hal::platform());
  CountingSink sink;
  TEST_ASSERT_TRUE(kernel::vfs::list("/", sink));
  // /littlefs, /apps, /res, /data, /fonts (the faces every app reads), /nvs (the key-value store as
  // files), /rtc (the RTC memory as one file) and /cfg.
  TEST_ASSERT_EQUAL_INT(8, static_cast<int>(sink.count));
}

void test_root_is_a_directory(void) {
  kernel::vfs::init(hal::platform());
  hal::Stat info;
  TEST_ASSERT_TRUE(kernel::vfs::stat("/", info));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(hal::NodeType::kDir), static_cast<int>(info.type));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_extension_is_read_the_same_way_it_is_declared);
  RUN_TEST(test_extension_of_a_path);
  RUN_TEST(test_reads_a_string_field);
  RUN_TEST(test_reads_a_field_in_the_middle);
  RUN_TEST(test_missing_key_fails);
  RUN_TEST(test_key_text_inside_a_value_is_not_a_key);
  RUN_TEST(test_reads_an_integer);
  RUN_TEST(test_non_numeric_value_is_not_an_integer);
  RUN_TEST(test_reads_a_boolean);
  RUN_TEST(test_apps_alias_resolves_inside_the_backend);
  RUN_TEST(test_root_mount_strips_its_prefix);
  RUN_TEST(test_parent_segments_are_refused);
  RUN_TEST(test_unknown_root_is_refused);
  RUN_TEST(test_root_lists_the_mounts);
  RUN_TEST(test_root_is_a_directory);
  UNITY_END();
  return 0;
}
