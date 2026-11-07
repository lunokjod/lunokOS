// **The pictures of a page** (docs/30): every family the system reads, decoded against the same plan so a
// decoder that loses its filters, its LZW or its transform shows it here; the refusals of the kinds this
// board does not read; and the whole path from the volume to the canvas of an app, which is what a page
// really does.
//
// The samples live with the demo app (`apps/images/res`) because a format that only a test reads is a
// format nobody has looked at.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <unity.h>

#include "img/img.h"
#include "emu/options.h"
#include "emu/surface.h"
#include "hal/hal.h"
#include "kernel/display.h"
#include "kernel/ui.h"
#include "kernel/vfs.h"
#include "pal/pal.h"

namespace ui = kernel::ui;

void setUp(void) {}

void tearDown(void) {}

namespace {

// **The same plan in six families**: what a decoder that loses its filters, its LZW or its transform gets
// wrong (docs/30). The plan lives with the tests and not with the demo, because the demo shows one picture
// per family —each format doing what it is good at— and a test needs them to be the same one.
const char* const kPlan[] = {
    "plan.png", "plan.jpg", "plan.bmp", "plan8.bmp", "plan.gif", "plan.ico",
};
const uint32_t kPlanCount = sizeof(kPlan) / sizeof(kPlan[0]);

// The netpbm family with its own pictures: a grey gradient, a colour one, and one bit a pixel, which is the
// smallest picture the format has.
const char* const kNetpbm[] = {"shades.pgm", "colours.ppm", "checks.pbm"};
const uint32_t kNetpbmCount = sizeof(kNetpbm) / sizeof(kNetpbm[0]);

bool g_booted = false;
char g_root[96];

// **A bench with the glass of the Paper and the disk of the repository's `data`**: the pictures are copied
// into a little filesystem of this run so nothing of the machine's own state is touched (docs/85).
bool boot_bench() {
  if (g_booted) return true;
  snprintf(g_root, sizeof(g_root), "/tmp/lunok-img-XXXXXX");
  if (mkdtemp(g_root) == nullptr) return false;
  char dir[128];
  snprintf(dir, sizeof(dir), "%s/littlefs/img", g_root);
  char command[256];
  snprintf(command, sizeof(command), "mkdir -p '%s'", dir);
  if (system(command) != 0) return false;
  snprintf(command, sizeof(command),
           "cp test/test_img/data/plan.* test/test_img/data/plan8.bmp test/test_img/data/noise.bin "
           "test/test_img/data/progressive.jpg '%s/'",
           dir);
  if (system(command) != 0) return false;

  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.display = true;
  opts.delays = false;  // a paint that is instant: this suite is about the pixels, not the glass
  opts.fs_dir = g_root;
  emu::set_options(opts);
  kernel::display::boot(hal::platform());
  kernel::display::set_rotation(0);
  kernel::vfs::init(hal::platform());
  ui::boot(hal::platform(), 0);
  g_booted = true;
  return true;
}

// **The call a page makes**: a path, where it lands and how big, in the order the kernel has it (docs/30).
bool draw(const char* path, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char*& code,
          const char*& reason) {
  return ui::image(x, y, w, h, path, nullptr, code, reason);
}

// The level of a pixel of the app's view (docs/30).
uint8_t view_level(uint16_t x, uint16_t y) {
  const ui::Canvas& view = ui::canvas();
  if (view.bits == nullptr || x >= view.width || y >= view.height) return 0;
  const uint8_t byte = view.bits[static_cast<uint32_t>(y) * view.stride + x / 2];
  return ((x & 1) == 0) ? static_cast<uint8_t>(byte >> 4) : static_cast<uint8_t>(byte & 0x0F);
}

void wipe() {
  ui::attach();
  ui::clear(15, nullptr);
}

// The bytes of a file of the host, for the tests of the decoder itself.
uint32_t read_host(const char* path, uint8_t* out, uint32_t cap) {
  FILE* file = fopen(path, "rb");
  if (file == nullptr) return 0;
  const size_t got = fread(out, 1, cap, file);
  fclose(file);
  return static_cast<uint32_t>(got);
}

struct Rows {
  uint8_t* bits = nullptr;
  uint32_t stride = 0;
  uint16_t width = 0;
  uint16_t height = 0;
};

void rows_sink(void* context, uint16_t y, const uint8_t* packed) {
  Rows& rows = *static_cast<Rows*>(context);
  if (y >= rows.height) return;
  memcpy(rows.bits + y * rows.stride, packed, rows.stride);
}

uint8_t rows_level(const Rows& rows, uint16_t x, uint16_t y) {
  const uint8_t byte = rows.bits[static_cast<uint32_t>(y) * rows.stride + x / 2];
  return ((x & 1) == 0) ? static_cast<uint8_t>(byte >> 4) : static_cast<uint8_t>(byte & 0x0F);
}

// **The plan of the sample**: four quadrants (white, black, grey, a red) and a diagonal across them, so
// every decoder can be asked the same five questions and a decoder that is wrong answers one of them wrong
// (docs/30).
void check_the_plan(const Rows& rows, const char* name) {
  TEST_ASSERT_EQUAL_UINT16(96, rows.width);
  TEST_ASSERT_EQUAL_UINT16(72, rows.height);
  // The four corners of the quadrants, twelve pixels inside them. **A JPEG rings** around an edge, so the
  // flat white of a corner is 15 or one under it and the flat black is 0 or one over (docs/30).
  const uint8_t white = rows_level(rows, 12, 12);
  TEST_ASSERT_TRUE_MESSAGE(white >= 14, name);
  const uint8_t black = rows_level(rows, 84, 12);
  TEST_ASSERT_TRUE_MESSAGE(black <= 1, name);
  const uint8_t grey = rows_level(rows, 6, 66);
  TEST_ASSERT_TRUE_MESSAGE(grey == 7 || grey == 8, name);              // 128, dithered
  const uint8_t red = rows_level(rows, 86, 66);
  TEST_ASSERT_TRUE_MESSAGE(red == 4 || red == 5, name);                // 200,30,40 dithered to its luma
  // The diagonal crosses the middle of the picture.
  TEST_ASSERT_TRUE_MESSAGE(rows_level(rows, 48, 36) != 15, name);
}

// ---- the decoder itself (docs/30) ----

// Every sample of every family, decoded into rows of four bits and checked against the same plan.
void test_every_family_draws_the_same_plan(void) {
  static uint8_t file[2 << 20];
  static uint8_t scratch[3 << 20];
  for (uint32_t i = 0; i < kPlanCount; ++i) {
    char path[128];
    snprintf(path, sizeof(path), "test/test_img/data/%s", kPlan[i]);
    const uint32_t size = read_host(path, file, sizeof(file));
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, size, path);

    img::Info info;
    TEST_ASSERT_EQUAL_MESSAGE(img::kOk, img::read_info(file, size, info), path);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(96, info.width, path);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(72, info.height, path);

    static uint8_t bits[96 * 48];
    Rows rows;
    rows.bits = bits;
    rows.stride = 48;
    rows.width = 96;
    rows.height = 72;
    memset(bits, 0xEE, sizeof(bits));
    const uint32_t need = img::scratch_bytes(file, size);
    TEST_ASSERT_TRUE_MESSAGE(need <= sizeof(scratch), path);
    img::RowSink sink(rows_sink, &rows);
    TEST_ASSERT_EQUAL_MESSAGE(img::kOk, img::decode(file, size, sink, scratch, sizeof(scratch)), path);
    check_the_plan(rows, path);
  }
}

// **A block that is smaller than the picture asks for is refused and never written past** (0.118.0): the state of a
// decoder —the table of a GIF, the header of a JPEG, the row it builds— lives in the block the caller gives, so the
// number `scratch_bytes` answers is a contract. Before this, a GIF needed 16 656 bytes of the stack and the board
// answered a page of pictures with an overflow in `loopTask` (docs/75, docs/85, docs/92 §33).
void test_a_block_that_is_smaller_than_the_picture_needs_is_refused(void) {
  static uint8_t file[2 << 20];
  static uint8_t scratch[3 << 20];
  static uint8_t bits[96 * 48];
  for (uint32_t i = 0; i < kPlanCount; ++i) {
    char path[128];
    snprintf(path, sizeof(path), "test/test_img/data/%s", kPlan[i]);
    const uint32_t size = read_host(path, file, sizeof(file));
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, size, path);
    const uint32_t need = img::scratch_bytes(file, size);
    // **Every family asks for something now**, because every family builds the row it hands to the sink and keeps
    // its own state in the same block (0.118.0).
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(1, need, path);
    Rows rows;
    rows.bits = bits;
    rows.stride = 48;
    rows.width = 96;
    rows.height = 72;
    memset(bits, 0xEE, sizeof(bits));
    img::RowSink sink(rows_sink, &rows);
    // One byte short: `kNoRoom` and not a picture written over the end of the block.
    TEST_ASSERT_EQUAL_MESSAGE(img::kNoRoom, img::decode(file, size, sink, scratch, need - 1), path);
    // And the number it asked for is enough, which is the other half of the contract.
    TEST_ASSERT_EQUAL_MESSAGE(img::kOk, img::decode(file, size, sink, scratch, need), path);
  }
}

// A file of a family this board does not read is refused with the reason and not drawn wrong.
void test_the_pictures_this_board_does_not_read_say_so(void) {
  static uint8_t file[2 << 20];
  uint32_t size = read_host("test/test_img/data/progressive.jpg", file, sizeof(file));
  img::Info info;
  TEST_ASSERT_EQUAL(img::kUnsupported, img::read_info(file, size, info));
  size = read_host("test/test_img/data/interlaced.png", file, sizeof(file));
  TEST_ASSERT_EQUAL(img::kUnsupported, img::read_info(file, size, info));
}

// What is not a picture at all, and what is a picture that was cut: two different answers, because they are
// two different problems (docs/30).
void test_a_broken_file_and_a_strange_one_are_told_apart(void) {
  static uint8_t file[2 << 20];
  img::Info info;
  uint32_t size = read_host("test/test_img/data/noise.bin", file, sizeof(file));
  TEST_ASSERT_EQUAL(img::kUnknown, img::read_info(file, size, info));
  size = read_host("test/test_img/data/truncated.png", file, sizeof(file));
  TEST_ASSERT_EQUAL(img::kTruncated, img::read_info(file, size, info));
}

// **The netpbm family with its own pictures**: a gradient goes from black to white, and one bit a pixel is
// two levels and nothing else (docs/30).
void test_the_netpbm_family_draws_its_own_pictures(void) {
  static uint8_t file[2 << 20];
  static uint8_t scratch[3 << 20];
  static uint8_t bits[96 * 48];
  for (uint32_t i = 0; i < kNetpbmCount; ++i) {
    char path[128];
    snprintf(path, sizeof(path), "apps/images/res/%s", kNetpbm[i]);
    const uint32_t size = read_host(path, file, sizeof(file));
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, size, path);
    img::Info info;
    TEST_ASSERT_EQUAL_MESSAGE(img::kOk, img::read_info(file, size, info), path);
    Rows rows;
    rows.bits = bits;
    rows.stride = 48;
    rows.width = 96;
    rows.height = 72;
    memset(bits, 0xEE, sizeof(bits));
    img::RowSink sink(rows_sink, &rows);
    TEST_ASSERT_EQUAL_MESSAGE(img::kOk, img::decode(file, size, sink, scratch, sizeof(scratch)), path);
    if (strcmp(kNetpbm[i], "checks.pbm") == 0) {
      // One bit: every pixel is the paper or the ink, and both are there.
      bool ink = false;
      bool paper = false;
      for (uint16_t y = 0; y < 72; y += 3) {
        for (uint16_t x = 0; x < 96; x += 3) {
          const uint8_t level = rows_level(rows, x, y);
          TEST_ASSERT_TRUE_MESSAGE(level == 0 || level == 15, path);
          if (level == 0) ink = true;
          if (level == 15) paper = true;
        }
      }
      TEST_ASSERT_TRUE(ink);
      TEST_ASSERT_TRUE(paper);
      continue;
    }
    if (strcmp(kNetpbm[i], "shades.pgm") == 0) {
      // Grey: black on the left and white on the right, and the middle is neither.
      TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(1, rows_level(rows, 1, 36), path);
      TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(14, rows_level(rows, 94, 36), path);
      const uint8_t middle = rows_level(rows, 48, 36);
      TEST_ASSERT_TRUE_MESSAGE(middle >= 6 && middle <= 9, path);
    } else {
      // Colour: the red goes up along a row and the blue comes down, so the two ends of the gradient are not
      // the same grey and the middle is another one (docs/30).
      const uint8_t left = rows_level(rows, 1, 36);
      const uint8_t right = rows_level(rows, 94, 36);
      TEST_ASSERT_GREATER_THAN_MESSAGE(left, right, path);
      TEST_ASSERT_TRUE_MESSAGE(left >= 3 && right <= 12, path);
    }
  }
}

// The family and the depth of each sample, which is what `ui.image_info` tells an app.
void test_the_info_of_every_family(void) {
  struct Expected {
    const char* name;
    const char* format;
    uint8_t depth;
  };
  static const Expected kExpected[] = {
      {"plan.png", "PNG", 24},  {"plan.jpg", "JPEG", 24}, {"plan.bmp", "BMP", 24},
      {"plan8.bmp", "BMP", 8},  {"plan.gif", "GIF", 8},   {"plan.ico", "ICO", 32},
  };
  static uint8_t file[2 << 20];
  for (uint32_t i = 0; i < sizeof(kExpected) / sizeof(kExpected[0]); ++i) {
    char path[128];
    snprintf(path, sizeof(path), "test/test_img/data/%s", kExpected[i].name);
    const uint32_t size = read_host(path, file, sizeof(file));
    img::Info info;
    TEST_ASSERT_EQUAL_MESSAGE(img::kOk, img::read_info(file, size, info), kExpected[i].name);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(kExpected[i].format, info.format, kExpected[i].name);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(kExpected[i].depth, info.depth, kExpected[i].name);
  }
}

// ---- the picture on the page (docs/30, docs/70) ----

// A picture drawn where the app says, at the size of the file.
void test_a_picture_lands_where_it_is_put(void) {
  TEST_ASSERT_TRUE(boot_bench());
  wipe();
  const char* code = nullptr;
  const char* reason = nullptr;
  TEST_ASSERT_TRUE_MESSAGE(draw("/littlefs/img/plan.png", 10, 20, 0, 0, code, reason),
                           (reason != nullptr) ? reason : "no reason");
  TEST_ASSERT_NULL(code);
  TEST_ASSERT_EQUAL_UINT8(15, view_level(16, 26));   // white, 6 inside
  TEST_ASSERT_EQUAL_UINT8(0, view_level(96, 26));    // black
  TEST_ASSERT_TRUE(view_level(16, 86) == 7 || view_level(16, 86) == 8);
  // Outside the picture the page is what it was.
  TEST_ASSERT_EQUAL_UINT8(15, view_level(5, 30));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(10, 40 + 72));
  // And the picture left its damage, which is what a present paints (docs/30).
  TEST_ASSERT_GREATER_THAN(0, ui::damage_count());
}

// A size that is not the picture's is the picture scaled, quadrant by quadrant.
void test_a_picture_scales_to_the_size_it_is_given(void) {
  TEST_ASSERT_TRUE(boot_bench());
  wipe();
  const char* code = nullptr;
  const char* reason = nullptr;
  TEST_ASSERT_TRUE(draw("/littlefs/img/plan.png", 0, 0, 48, 36, code, reason));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(4, 4));   // the white quadrant, at half size
  TEST_ASSERT_EQUAL_UINT8(0, view_level(44, 4));   // the black one
  TEST_ASSERT_TRUE(view_level(4, 32) == 7 || view_level(4, 32) == 8);
  // Half the picture: the page beyond 48 by 36 is untouched.
  TEST_ASSERT_EQUAL_UINT8(15, view_level(48, 20));
  TEST_ASSERT_EQUAL_UINT8(15, view_level(20, 36));
}

// **Every family draws the same plan through the canvas too**, which is the whole path: the volume, the
// decoder, the rows and the pixels of the app (docs/30, docs/70).
void test_every_family_reaches_the_canvas(void) {
  TEST_ASSERT_TRUE(boot_bench());
  for (uint32_t i = 0; i < kPlanCount; ++i) {
    char path[128];
    snprintf(path, sizeof(path), "/littlefs/img/%s", kPlan[i]);
    wipe();
    const char* code = nullptr;
    const char* reason = nullptr;
    if (!draw(path, 0, 0, 96, 72, code, reason)) {
      char why[192];
      snprintf(why, sizeof(why), "%s: %s (%s)", path, (reason != nullptr) ? reason : "?",
               (code != nullptr) ? code : "?");
      TEST_FAIL_MESSAGE(why);
    }
    (void)code;
    TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(14, view_level(12, 12), path);
    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(1, view_level(84, 12), path);
    const uint8_t grey = view_level(6, 66);
    TEST_ASSERT_TRUE_MESSAGE(grey == 7 || grey == 8, path);
    const uint8_t red = view_level(86, 66);
    TEST_ASSERT_TRUE_MESSAGE(red == 4 || red == 5, path);
  }
}

// A path an app may not name, a file that is not there, and a file that is not a picture: three refusals
// with a reason, which is what an app shows a person (docs/30, docs/70).
void test_a_picture_that_cannot_be_read_says_why(void) {
  TEST_ASSERT_TRUE(boot_bench());
  wipe();
  const char* code = nullptr;
  const char* reason = nullptr;
  TEST_ASSERT_FALSE(draw("/littlefs/img/no-such.png", 0, 0, 0, 0, code, reason));
  TEST_ASSERT_NOT_NULL(reason);
  TEST_ASSERT_EQUAL_STRING("there is no such picture", reason);

  TEST_ASSERT_FALSE(draw("/littlefs/img/noise.bin", 0, 0, 0, 0, code, reason));
  TEST_ASSERT_EQUAL_STRING("E_INVAL", code);
  TEST_ASSERT_EQUAL_STRING("that is not a picture this board knows", reason);

  // A picture of a kind the board does not read is `E_UNSUPPORTED` and not a broken file (docs/30).
  TEST_ASSERT_FALSE(draw("/littlefs/img/progressive.jpg", 0, 0, 0, 0, code, reason));
  TEST_ASSERT_EQUAL_STRING("E_UNSUPPORTED", code);
}

// What a picture is, from the page's point of view (docs/30).
void test_the_info_of_a_picture_on_the_volume(void) {
  TEST_ASSERT_TRUE(boot_bench());
  char format[8];
  uint16_t width = 0;
  uint16_t height = 0;
  const char* code = nullptr;
  const char* reason = nullptr;
  TEST_ASSERT_TRUE(ui::image_info("/littlefs/img/plan.gif", format, sizeof(format), width, height, code,
                                  reason));
  TEST_ASSERT_EQUAL_STRING("GIF", format);
  TEST_ASSERT_EQUAL_UINT16(96, width);
  TEST_ASSERT_EQUAL_UINT16(72, height);
  TEST_ASSERT_FALSE(ui::image_info("/littlefs/img/no-such.png", format, sizeof(format), width, height, code,
                                   reason));
  TEST_ASSERT_EQUAL_STRING("there is no such picture", reason);
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_every_family_draws_the_same_plan);
  RUN_TEST(test_a_block_that_is_smaller_than_the_picture_needs_is_refused);
  RUN_TEST(test_the_netpbm_family_draws_its_own_pictures);
  RUN_TEST(test_the_pictures_this_board_does_not_read_say_so);
  RUN_TEST(test_a_broken_file_and_a_strange_one_are_told_apart);
  RUN_TEST(test_the_info_of_every_family);
  RUN_TEST(test_a_picture_lands_where_it_is_put);
  RUN_TEST(test_a_picture_scales_to_the_size_it_is_given);
  RUN_TEST(test_every_family_reaches_the_canvas);
  RUN_TEST(test_a_picture_that_cannot_be_read_says_why);
  RUN_TEST(test_the_info_of_a_picture_on_the_volume);
  return UNITY_END();
}
