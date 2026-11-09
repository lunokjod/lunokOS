// **What the disk of the emulator costs**, device by device (docs/50, docs/85).
//
// The emulator used to charge one flat millisecond for every read of every volume, which is a number that
// is wrong for all of them: a flash block is eight hundred microseconds of wire and a sector of it has
// to be erased before anything is written, a card is a command and a sector at ten megahertz, and the
// FRAM is an I2C chip where **a byte is ninety microseconds**. This is the table of `emu/disk.h` paid
// through the two volumes and the chip, and the switch that turns all of it off.
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <unity.h>

#include "emu/disk.h"
#include "emu/memories.h"
#include "emu/options.h"
#include "hal/hal.h"
#include "pal/pal.h"

void setUp(void) {}

void tearDown(void) {}

namespace {

const char* kRoot = "/tmp/lunokos_disk_test";

bool g_booted = false;

// An emulator whose flash is a directory of its own, so nothing of this suite touches the repository's
// `data/`, and whose board is the Paper: it is the one with the FRAM and the card (docs/50, docs/85).
void boot_with_a_disk() {
  if (g_booted) return;
  ::mkdir(kRoot, 0777);
  char flash[128];
  snprintf(flash, sizeof(flash), "%s/littlefs", kRoot);
  ::mkdir(flash, 0777);
  emu::Options opts = emu::options();
  opts.board = "m5stack-paper";
  opts.fs_dir = kRoot;
  opts.flash_dir = nullptr;  // the volume is <fs_dir>/littlefs
  emu::set_options(opts);
  g_booted = true;
}

// **The waits are the run's choice**, and a test says which one it is measuring.
void pay_times(bool on) {
  emu::Options opts = emu::options();
  opts.delays = on;
  emu::set_options(opts);
}

int64_t micros() { return pal::system().micros(); }

}  // namespace

// **The table is the physics of each device**, and these are its numbers: a flash block of 4 KB at
// 40 MHz, a page programme and a sector erase; a card's sector of 512 B at 10 MHz after a command; and
// the FRAM, where a byte is nine bits on a hundred-kilohertz bus (docs/50, docs/85).
static void test_the_table_says_what_each_device_costs() {
  const emu::DiskTiming flash = emu::disk_timing(emu::Disk::kFlash);
  TEST_ASSERT_EQUAL_UINT32(4096, flash.block_bytes);
  TEST_ASSERT_EQUAL_UINT32(819, flash.read_block_us);   // 4 KB at 40 MHz
  TEST_ASSERT_EQUAL_UINT32(45000, flash.erase_us);      // a sector, the maker's typical
  TEST_ASSERT_EQUAL_UINT32(256, flash.write_unit_bytes);   // a page of the nor
  TEST_ASSERT_EQUAL_UINT32(700, flash.write_unit_us);      // and 0.7 ms to programme it (W25Q32JV, tPP)
  const emu::DiskTiming card = emu::disk_timing(emu::Disk::kCard);
  TEST_ASSERT_EQUAL_UINT32(512, card.block_bytes);
  TEST_ASSERT_EQUAL_UINT32(410, card.read_block_us);    // 512 B at 10 MHz
  TEST_ASSERT_EQUAL_UINT32(0, card.erase_us);           // a card has no erase
  const emu::DiskTiming fram = emu::disk_timing(emu::Disk::kFram);
  TEST_ASSERT_EQUAL_UINT32(1, fram.block_bytes);
  TEST_ASSERT_EQUAL_UINT32(90, fram.read_block_us);     // ninety microseconds a byte
  // The RTC memory is RAM: nothing of it is paid, and an emulator that charged for it would be inventing a
  // chip that is not there.
  const emu::DiskTiming memory = emu::disk_timing(emu::Disk::kNone);
  TEST_ASSERT_EQUAL_UINT32(0, memory.command_us);
  TEST_ASSERT_EQUAL_UINT32(0, memory.block_bytes);
}

// **A read of the flash is a block**, which is 819 us of wire plus the request: the same 4 KB here and on
// the board, because it is the same nor at the same clock (docs/85).
static void test_a_flash_read_pays_its_block() {
  boot_with_a_disk();
  hal::IStorage* disk = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(disk);
  uint8_t block[4096];
  memset(block, 0x5A, sizeof(block));
  hal::FileSlot slot;
  TEST_ASSERT_TRUE(disk->open("probe.bin", hal::OpenMode::kWrite, slot));
  TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(sizeof(block)), disk->write(slot, block, sizeof(block)));
  TEST_ASSERT_TRUE(disk->close(slot));

  pay_times(true);
  TEST_ASSERT_TRUE(disk->open("probe.bin", hal::OpenMode::kRead, slot));
  const int64_t started = micros();
  const int32_t got = disk->read(slot, block, sizeof(block));
  const int64_t spent = micros() - started;
  TEST_ASSERT_TRUE(disk->close(slot));
  TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(sizeof(block)), got);
  // 60 us of request and 819 of wire: at least that, and not the second of a wrong unit.
  TEST_ASSERT_TRUE_MESSAGE(spent >= 870, "a block of the flash was read faster than its wire");
  TEST_ASSERT_TRUE_MESSAGE(spent < 20000, "a block of the flash took as long as an erase");
}

// **A write pays the page programmes and the sector it takes**, and that is where the seconds of writing
// a file come from: littlefs does not write over what is there (docs/85).
static void test_a_flash_write_pays_the_sector_it_takes() {
  boot_with_a_disk();
  hal::IStorage* disk = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(disk);
  uint8_t block[4096];
  memset(block, 0x11, sizeof(block));
  pay_times(true);
  hal::FileSlot slot;
  TEST_ASSERT_TRUE(disk->open("write.bin", hal::OpenMode::kWrite, slot));
  const int64_t started = micros();
  const int32_t put = disk->write(slot, block, sizeof(block));
  const int64_t spent = micros() - started;
  TEST_ASSERT_TRUE(disk->close(slot));
  TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(sizeof(block)), put);
  // 6 400 of programme and 45 000 of the sector, plus the request.
  TEST_ASSERT_TRUE_MESSAGE(spent >= 51000, "a block of the flash was written without erasing a sector");
}

// **The FRAM is an I2C chip and a byte is ninety microseconds**: eight bytes are over half a millisecond,
// which is four hundred times what a block of flash costs per byte (docs/50, docs/85).
static void test_the_fram_is_the_i2c_and_a_byte_is_ninety_microseconds() {
  boot_with_a_disk();
  hal::IStorage* fram = hal::platform().storage("/fram");
  TEST_ASSERT_NOT_NULL(fram);
  uint8_t bytes[8] = {0};
  hal::FileSlot slot;
  pay_times(true);
  TEST_ASSERT_TRUE(fram->open("", hal::OpenMode::kRead, slot));
  const int64_t started = micros();
  const int32_t got = fram->read(slot, bytes, sizeof(bytes));
  const int64_t spent = micros() - started;
  TEST_ASSERT_TRUE(fram->close(slot));
  TEST_ASSERT_EQUAL_INT32(8, got);
  // 40 us of transaction and eight bytes at ninety: 760 in all.
  TEST_ASSERT_TRUE_MESSAGE(spent >= 750, "eight bytes of the FRAM did not pay the bus");
}

// **The key-value store writes a page programme and not a sector**, which is what the NVS of the chip
// does: a thirty-two-byte entry costs the 0.7 ms of a page programme and the 45 ms of the sector only
// when the page fills, where littlefs pays a sector for every commit (docs/70, docs/85).
static void test_a_key_of_the_store_pays_a_page_and_the_sector_once_in_a_while() {
  boot_with_a_disk();
  hal::IKeyValue* kv = hal::platform().nvs();
  TEST_ASSERT_NOT_NULL(kv);
  pay_times(true);
  const uint8_t value[4] = {1, 2, 3, 4};
  const int64_t started = micros();
  TEST_ASSERT_TRUE(kv->set("timing", "first", value, sizeof(value)));
  const int64_t first = micros() - started;
  TEST_ASSERT_TRUE_MESSAGE(first >= 600, "a key of the store did not pay its page programme");
  TEST_ASSERT_TRUE_MESSAGE(first < 40000, "a key of the store paid a sector it did not need");
  // A page of the store is 126 entries: a run of them pays the page programmes and the sector when it
  // turns over, and not a sector per key.
  const int64_t before = micros();
  for (int i = 0; i < 127; ++i) {
    TEST_ASSERT_TRUE(kv->set("timing", "entry", value, sizeof(value)));
  }
  const int64_t total = micros() - before;
  TEST_ASSERT_TRUE_MESSAGE(total >= 44000, "the page turned over and nothing was erased");
  TEST_ASSERT_TRUE_MESSAGE(total <= 200000, "every key of the store paid a sector");
  pay_times(false);
}

// **And the switch that turns all of it off**: with the waits off the same write of a block of flash is
// the model and no clock, which is what every suite but this one runs with (docs/85).
static void test_with_the_waits_off_the_disk_is_free() {
  boot_with_a_disk();
  hal::IStorage* disk = hal::platform().storage("/littlefs");
  TEST_ASSERT_NOT_NULL(disk);
  uint8_t block[4096];
  memset(block, 0x22, sizeof(block));
  pay_times(false);
  hal::FileSlot slot;
  TEST_ASSERT_TRUE(disk->open("free.bin", hal::OpenMode::kWrite, slot));
  const int64_t started = micros();
  const int32_t put = disk->write(slot, block, sizeof(block));
  const int64_t spent = micros() - started;
  TEST_ASSERT_TRUE(disk->close(slot));
  TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(sizeof(block)), put);
  TEST_ASSERT_TRUE_MESSAGE(spent < 5000, "a write with the waits off asked the clock for a sector");
}

// **What a board keeps, and what a run that starts again finds** (docs/60, docs/70, docs/85). The emulator
// keeps the two things the hardware keeps outside a volume: the hour a chip with a coin cell holds and
// the key-value store of the network. These tests are the two files, one run apart.
//
// The store is read here **before anything of this suite has touched the radio**, which is exactly the
// state of an emulator that has just started: the name comes from the flash and not from a string of the
// run.
static void test_the_network_in_the_store_is_what_a_new_run_answers() {
  boot_with_a_disk();
  hal::IKeyValue* kv = hal::platform().nvs();
  TEST_ASSERT_NOT_NULL(kv);
  TEST_ASSERT_TRUE(kv->set("lunoknet", "ssid", "mynet", 5));
  TEST_ASSERT_TRUE(kv->set("wifidrv", "pass", "secret", 6));
  hal::INet* net = hal::platform().net();
  TEST_ASSERT_NOT_NULL(net);
  char ssid[33] = {0};
  TEST_ASSERT_TRUE_MESSAGE(net->wifi_stored_ssid(ssid, sizeof(ssid)), "the store has a name in it");
  TEST_ASSERT_EQUAL_STRING("mynet", ssid);
  TEST_ASSERT_TRUE(net->wifi_start_stored());
}

// **What the run stores goes to the flash and not to a string**: `wifi add` is a promise that the next
// run can keep, and on the board the driver writes the pair to the key-value store (docs/50, docs/85).
static void test_a_network_that_is_stored_is_written_to_the_key_value_store() {
  boot_with_a_disk();
  hal::IKeyValue* kv = hal::platform().nvs();
  hal::INet* net = hal::platform().net();
  TEST_ASSERT_NOT_NULL(kv);
  TEST_ASSERT_NOT_NULL(net);
  TEST_ASSERT_TRUE(net->wifi_store("other", "pw"));
  char got[16] = {0};
  size_t len = 0;
  TEST_ASSERT_TRUE(kv->get("lunoknet", "ssid", got, sizeof(got) - 1, len));
  got[len] = '\0';
  TEST_ASSERT_EQUAL_STRING("other", got);
  memset(got, 0, sizeof(got));
  TEST_ASSERT_TRUE(kv->get("wifidrv", "pass", got, sizeof(got) - 1, len));
  got[len] = '\0';
  TEST_ASSERT_EQUAL_STRING("pw", got);
}

// **Forgetting is forgetting on the flash, and the name is what is trusted** (docs/50, docs/85): the
// system erases its own record —the same `own_record_erase()` the board calls— and the driver keeps its
// blob, which without a name is a password nobody can use. What matters is that the next run answers
// `network: none`, and it does.
static void test_forgetting_a_network_erases_the_name_it_trusts() {
  boot_with_a_disk();
  hal::IKeyValue* kv = hal::platform().nvs();
  hal::INet* net = hal::platform().net();
  TEST_ASSERT_NOT_NULL(kv);
  TEST_ASSERT_NOT_NULL(net);
  TEST_ASSERT_TRUE(net->prov_forget());
  size_t len = 0;
  char got[16] = {0};
  TEST_ASSERT_FALSE(kv->get("lunoknet", "ssid", got, sizeof(got), len));
  char ssid[33] = {0};
  TEST_ASSERT_FALSE(net->wifi_stored_ssid(ssid, sizeof(ssid)));
}

// **The hour is a file and it counts the time the board was unplugged**: the run writes the epoch and
// the second it was written, so the next run's hour is the one of the clock with the off-time added in,
// which is what a coin cell does. A state directory that never had one is a board nobody has set.
static void test_the_hour_a_board_keeps_survives_a_run() {
  boot_with_a_disk();
  char path[192];
  snprintf(path, sizeof(path), "%s/clock.bin", kRoot);
  ::remove(path);
  TEST_ASSERT_TRUE(emu::load_clock_ms() == 0);
  const int64_t saved = 1700000000000LL;
  emu::save_clock_ms(saved);
  const int64_t back = emu::load_clock_ms();
  TEST_ASSERT_TRUE_MESSAGE(back >= saved, "the hour went backwards");
  TEST_ASSERT_TRUE_MESSAGE(back < saved + 5000, "the hour jumped past the test's own run");
  // A zero is not an hour: it is a board that never knew one, and it is not written.
  emu::save_clock_ms(0);
  TEST_ASSERT_TRUE(emu::load_clock_ms() >= saved);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_the_table_says_what_each_device_costs);
  RUN_TEST(test_a_flash_read_pays_its_block);
  RUN_TEST(test_a_flash_write_pays_the_sector_it_takes);
  RUN_TEST(test_the_fram_is_the_i2c_and_a_byte_is_ninety_microseconds);
  RUN_TEST(test_with_the_waits_off_the_disk_is_free);
  RUN_TEST(test_a_key_of_the_store_pays_a_page_and_the_sector_once_in_a_while);
  RUN_TEST(test_the_network_in_the_store_is_what_a_new_run_answers);
  RUN_TEST(test_a_network_that_is_stored_is_written_to_the_key_value_store);
  RUN_TEST(test_forgetting_a_network_erases_the_name_it_trusts);
  RUN_TEST(test_the_hour_a_board_keeps_survives_a_run);
  return UNITY_END();
}
