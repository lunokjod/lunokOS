// PAL for ESP32 + Arduino.
//
// Two rules live here and nowhere else: how to queue events (FreeRTOS) and how
// to talk to the serial port (Arduino Serial). The kernel sees neither.
#include <Arduino.h>
#include <Esp.h>
#include <bootloader_random.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>
#include <esp_rom_sys.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "pal/pal.h"

namespace pal {
namespace {

const uint16_t kQueueLength = 16;
const uint32_t kReaderPeriodMs = 5;

// **The noise of this boot, taken once** (0.174.8). The chip's hardware RNG is a true one only while the radio is on
// —on this family the number is read out of the Wi-Fi peripheral's register, `WDEV_RND_REG`, and the entropy is the RF
// noise (docs/50)— and this system keeps the radio off until something asks for it, so at boot a `esp_random()` can hand
// back the same word every time. The chip's own answer for that case is `bootloader_random_enable()`: it mixes the SAR
// ADC's internal noise into the generator, is documented for use from the app early in the run, and **has to be let go
// before anything else touches the ADC** (the battery gauge of the Paper is an ADC pin, docs/50). So this is the whole
// window: enable, take eight bytes, disable — before the first widget of the system exists.
//
// **And what is mixed with it** is the microsecond count since the reset, because two boots of the same board are two
// different moments even when the amplifier of the ADC says the same thing twice.
//
// **Whether the radio of the chip is up** (0.178.6), which is the one question that decides whether the chip's generator
// is a true one: `esp_random()` is documented as true random **while Wi-Fi or Bluetooth is enabled**, because the entropy
// is the RF noise of the peripheral (docs/50). The driver answers `ESP_ERR_WIFI_NOT_INIT` when nobody has brought it up,
// and the mode is null while it is not started, so this is "the radio is on" and not "somebody asked for Wi-Fi once".
bool radio_is_up() {
  wifi_mode_t mode = WIFI_MODE_NULL;
  return esp_wifi_get_mode(&mode) == ESP_OK && mode != WIFI_MODE_NULL;
}

uint32_t mix32(uint32_t x) {
  x += 0x9E3779B9u;
  x = (x ^ (x >> 16)) * 0x21F0AAADu;
  x = (x ^ (x >> 15)) * 0x735A2D97u;
  return x ^ (x >> 15);
}

// **The last word this board drew, kept where a reset does not reach it** (0.178.8). The RTC memory of the chip is the
// one place that survives a reset, a panic and a deep sleep without touching the flash —which wears— and it is where
// the hour of `/rtc` lives too (docs/50, docs/70). **It does not survive the power going away**, and that is said in
// the docs: a board that has been unplugged starts from the noise of its own boot, which is exactly the case the ADC
// window exists for. With the last word mixed into the seed, **two runs of the same board cannot share a sequence**
// even if the entropy source hands back the same bytes twice, which was the fault this is here to close (docs/20).
//
// `RTC_NOINIT_ATTR` and not `RTC_DATA_ATTR`: an initialised variable is written on every boot and would erase what the
// boot before it left. The block is read as garbage the first time the power comes on, so it carries a magic and a
// check and a block that does not pass them is not mixed —it is not trusted, and it does not stop the boot either—.
struct Kept {
  uint32_t magic;
  uint32_t state;
  uint32_t serial;
  uint32_t check;
};
RTC_NOINIT_ATTR Kept g_kept;
const uint32_t kKeptMagic = 0x524E4721u;  // "RNG!"

uint32_t kept_check(uint32_t state, uint32_t serial) { return mix32(state ^ mix32(serial ^ kKeptMagic)); }

bool kept_is_sound() { return g_kept.magic == kKeptMagic && g_kept.check == kept_check(g_kept.state, g_kept.serial); }

void kept_write(uint32_t state, uint32_t serial) {
  g_kept.state = state;
  g_kept.serial = serial;
  g_kept.check = kept_check(state, serial);
  g_kept.magic = kKeptMagic;
}

struct Random {
  bool seeded = false;
  uint32_t state = 0;

  void seed() {
    if (seeded) return;
    seeded = true;
    uint32_t noise[2] = {0, 0};
    bootloader_random_enable();
    esp_fill_random(noise, sizeof(noise));
    bootloader_random_disable();
    const uint32_t us = static_cast<uint32_t>(esp_timer_get_time());
    // **And the word the run before left in the RTC memory**, if it left one: a reset is not a new world, and a board
    // whose entropy source says the same thing twice still gets two different sequences because the second boot knows
    // what the first one drew (docs/50).
    const uint32_t before = kept_is_sound() ? g_kept.state : 0;
    const uint32_t serial = kept_is_sound() ? (g_kept.serial + 1u) : 1u;
    // A zero state is a mixer that answers zero for ever, and `mix32` of anything is not zero.
    state = mix32(noise[0] ^ mix32(noise[1] ^ us ^ mix32(before ^ serial))) | 1u;
    kept_write(state, serial);
  }

  // **With the radio up the number is the chip's** (0.178.6): while the radio is on, `esp_random()` is a true random
  // word —the noise of the RF stage, docs/50— and that is what an app gets, so a session with Wi-Fi is not a session of
  // xorshift32. **And each of those words stirs the state of the mixer**, so that the sequence that answers when the
  // radio goes back down is not the one seeded at the boot any more. With the radio off this is the xorshift32 of the
  // boot, as it always was, because asking the chip then is asking a register that does not refresh (docs/20, docs/50).
  uint32_t next() {
    seed();
    if (radio_is_up()) {
      const uint32_t word = esp_random();
      state ^= mix32(word);
      if (state == 0) state = 1u;
      // **The word of the chip is the one the next boot should know**: it is the last thing this board said, and it is
      // written where a reset does not reach (a write to the RTC memory, not to the flash) (docs/50).
      kept_write(word, g_kept.serial);
      return word;
    }
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    kept_write(state, g_kept.serial);
    return state;
  }
};
Random g_random;

class EspSystem : public ISystem, public IQueue, public IConsole {
 public:
  bool bring_up() {
    q_ = xQueueCreate(kQueueLength, sizeof(Event));
    if (q_ == nullptr) return false;
    // The reader only looks at how many bytes are waiting and posts an event.
    // It never reads them: only the kernel reads the console.
    if (xTaskCreate(&EspSystem::reader_trampoline, "pal_rx", 2048, this, 1, nullptr) != pdPASS) {
      return false;
    }
    return true;
  }

  // ISystem -----------------------------------------------------------------

  IQueue* queue() override { return this; }
  IConsole* console() override { return this; }

  int64_t millis() const override {
    return static_cast<int64_t>(esp_timer_get_time() / 1000);
  }

  int64_t micros() const override { return esp_timer_get_time(); }

  void reboot() override { ESP.restart(); }
  bool reboot_requested() const override { return false; }

  uint32_t random() override { return g_random.next(); }

  HeapInfo heap() const override {
    HeapInfo info;
    info.valid = true;
    // Asked by capability and not with `ESP.getFreeHeap()`, which is one number for a board that now
    // has two kinds of memory: this is the board's own RAM, which is what the system and the drivers
    // need. What the PSRAM has left is the board's answer, in `IPlatform::extra_memory` (docs/75).
    info.free_bytes =
        static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    info.min_free_bytes =
        static_cast<uint32_t>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    // **What a page could still take**: the pieces do not add up, and the board has the number of the
    // biggest one (docs/75).
    info.largest_free_bytes =
        static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    return info;
  }

  // **What is left of the stack of this task, at its lowest point**: FreeRTOS counts the words nobody has
  // touched since the task started, and a task whose stack overflows does not report it — it corrupts a
  // backtrace and reboots (measured on the Paper with `run buttons`, 2/11, docs/75).
  int32_t stack_free() const override {
    const UBaseType_t words = uxTaskGetStackHighWaterMark(nullptr);
    return static_cast<int32_t>(words * sizeof(StackType_t));
  }

  // IQueue ------------------------------------------------------------------

  bool push(const Event& ev, uint32_t timeout_ms) override {
    if (q_ == nullptr) return false;
    return xQueueSend(q_, &ev, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
  }

  bool pop(Event& out, int32_t timeout_ms) override {
    if (q_ == nullptr) return false;
    const TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xQueueReceive(q_, &out, ticks) == pdTRUE;
  }

  uint16_t size() const override {
    if (q_ == nullptr) return 0;
    return static_cast<uint16_t>(uxQueueMessagesWaiting(q_));
  }

  uint16_t capacity() const override { return kQueueLength; }

  // IConsole ----------------------------------------------------------------

  size_t write(const void* data, size_t len) override {
    return Serial.write(static_cast<const uint8_t*>(data), len);
  }

  // The UART keeps a FIFO and a shift register after write() returns, and both stop when
  // the chip sleeps: without this wait the last line before a sleep arrives cut in half
  // (docs/50, docs/60).
  void flush() override { Serial.flush(); }

  size_t read(void* data, size_t len) override {
    uint8_t* out = static_cast<uint8_t*>(data);
    size_t n = 0;
    while (n < len && Serial.available() > 0) {
      const int c = Serial.read();
      if (c < 0) break;
      out[n++] = static_cast<uint8_t>(c);
    }
    return n;
  }

  bool ansi() const override { return true; }

 private:
  static void reader_trampoline(void* self) {
    static_cast<EspSystem*>(self)->reader_loop();
  }

  void reader_loop() {
    for (;;) {
      const int waiting = Serial.available();
      if (waiting > 0) {
        Event ev;
        ev.type = kEventConsoleRx;
        ev.a = 0;
        ev.b = static_cast<int32_t>(waiting);
        push(ev, 0);
      }
      vTaskDelay(pdMS_TO_TICKS(kReaderPeriodMs));
    }
  }

  QueueHandle_t q_ = nullptr;
};

EspSystem g_system;

// The two homes a block can have, as the chip names them. INTERNAL is the board's own RAM, the one
// the system and the drivers need, and SPIRAM is the PSRAM the M5Paper carries (docs/50, docs/75).
constexpr uint32_t kExtraCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
constexpr uint32_t kHomeCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

// **Where a block goes is `pal::take_from_the_extra`** (0.151.20): the floor of the board's own RAM and the rule
// that keeps it live in `pal/pal.h` so the emulator obeys the same one and a test can hold it. What is here is only
// the reading of the chip's free bytes.
// **The relief of a heap that is full** (0.179.0): the kernel's session memories go first, and the ask is tried again
// once. A pointer and not a call into the kernel, because this layer is below it (docs/30, docs/75).
void (*g_relief)(void* keep, size_t bytes) = nullptr;
bool g_relieving = false;

// **`keep` is the block the ask is about**, when it is about one: a block that is being moved must not be given back by
// the relief and then moved (0.179.6, docs/75).
void relieve(void* keep, size_t bytes) {
  if (g_relief == nullptr || g_relieving) return;
  g_relieving = true;
  g_relief(keep, bytes);
  g_relieving = false;
}

class EspExtraMemory : public IExtraMemory {
 public:
  void* allocate(size_t bytes, bool prefer_extra) override {
    if (bytes == 0) return nullptr;
    // A block that asked for the extra memory goes there first and the board's RAM is the fallback; a
    // small one goes home only while the home keeps its floor, and to the PSRAM when it would lose it.
    if (take_from_the_extra(room_at_home(), bytes, prefer_extra)) {
      if (void* block = heap_caps_malloc(bytes, kExtraCaps)) return block;
    }
    if (void* block = heap_caps_malloc(bytes, kHomeCaps)) return block;
    // A board with no PSRAM has nowhere else to look and this is the answer it gives (docs/75)...
    if (void* block = heap_caps_malloc(bytes, kExtraCaps)) return block;
    // ...**unless the session memories of the apps are what is in the way** (0.179.0): they go, once, and the two
    // homes are asked again. It is a *failed* ask and not a heap that looks full: a block that fits is never paid for with
    // somebody's cache (0.179.6, docs/75).
    relieve(nullptr, bytes);
    if (take_from_the_extra(room_at_home(), bytes, prefer_extra)) {
      if (void* block = heap_caps_malloc(bytes, kExtraCaps)) return block;
    }
    if (void* block = heap_caps_malloc(bytes, kHomeCaps)) return block;
    return heap_caps_malloc(bytes, kExtraCaps);
  }

  void* resize(void* block, size_t bytes, bool prefer_extra) override {
    if (block == nullptr) return allocate(bytes, prefer_extra);
    if (bytes == 0) {
      release(block);
      return nullptr;
    }
    const bool to_extra = take_from_the_extra(room_at_home(), bytes, prefer_extra);
    const uint32_t home = to_extra ? kExtraCaps : kHomeCaps;
    if (void* moved = heap_caps_realloc(block, bytes, home)) return moved;
    // The same block in the other home: `heap_caps_realloc` allocates, copies and frees, which is
    // exactly what moving a block to the other memory means. On failure the block is still the one
    // the caller gave and it is not touched (docs/75).
    if (void* moved = heap_caps_realloc(block, bytes, to_extra ? kHomeCaps : kExtraCaps)) return moved;
    // **And the session memories go before the refusal here too** (0.179.0), with the block untouched and the same
    // two homes tried again. **The block that is being moved is not given back**: it is the one the ask is about, and a
    // `realloc` of what the relief has just freed is a block used after it was given away (0.179.6, docs/75).
    relieve(block, bytes);
    if (void* moved = heap_caps_realloc(block, bytes, home)) return moved;
    return heap_caps_realloc(block, bytes, to_extra ? kHomeCaps : kExtraCaps);
  }

  void release(void* block) override { heap_caps_free(block); }

 private:
  // **The free bytes of the board's RAM are read every so many asks and not at every one**: the walk
  // of the heap for each of the thousands of blocks a Lua machine takes would cost more than the block
  // itself, and a floor that is read two hundred and fifty-six blocks late is still a floor.
  size_t room_at_home() {
    if ((++asked_ & 0xFFu) == 0u) home_free_ = heap_caps_get_free_size(kHomeCaps);
    return home_free_;
  }

  size_t asked_ = 0;
  // Nothing has been read yet: the first blocks go to the extra memory, which is the safe answer when
  // the board has one and the only answer when it does not.
  size_t home_free_ = 0;
};

EspExtraMemory g_extra;

}  // namespace

bool init() {
  // **The noise of this boot, taken before anything else of the board exists**: the window of `Random::seed` leaves the
  // entropy source off, and the documents of the chip are clear that it has to be off before the ADC is initialised
  // —the battery gauge of the Paper is an ADC pin— and before the radio (docs/50). Doing it here and not at the first
  // `random()` is what makes that a fact of the boot instead of a fact of whoever asks first (docs/20).
  g_random.seed();
  Serial.setDebugOutput(false);
  Serial.begin(115200);
  // Right after a reset the line carries junk for a few milliseconds. Let it
  // arrive and throw it away before the console is handed to anyone.
  delay(20);
  while (Serial.available() > 0) {
    Serial.read();
  }
  return g_system.bring_up();
}

void shutdown() {}

ISystem& system() { return g_system; }

IExtraMemory& extra_memory() { return g_extra; }

void set_dispatch_us(uint32_t us) {
  // **The tick of the chip dispatches for itself**: a board is not told how its own FreeRTOS wakes
  // (docs/85).
  (void)us;
}

void set_wait_slice_ms(uint32_t ms) {
  // **The tick of the chip wakes the system by itself**: a slice is an emulator's way of looking at a window, and
  // a board has no window (docs/85).
  (void)ms;
}

void set_heap_timing(uint32_t step_ns, uint32_t bytes_per_us) {
  // **The chip's allocator is the chip's**: a board is not told what its own malloc costs (docs/85).
  (void)step_ns;
  (void)bytes_per_us;
}

MemoryLeft memory_left(bool extra) {
  // **The chip answers for itself**: the board's own RAM through `MALLOC_CAP_INTERNAL` and the PSRAM
  // through `MALLOC_CAP_SPIRAM`, which is what `mem` prints and what `sys.heap()` already reads. A board
  // with no PSRAM has no extra home and says so (docs/75).
  MemoryLeft out = {};
  if (extra && heap_caps_get_total_size(kExtraCaps) == 0) return out;
  const uint32_t caps = extra ? kExtraCaps : kHomeCaps;
  out.counted = true;
  out.free_bytes = heap_caps_get_free_size(caps);
  out.lowest_free_bytes = heap_caps_get_minimum_free_size(caps);
  return out;
}

void set_memory_bytes(uint32_t home_bytes, uint32_t extra_bytes) {
  // **A board is not told how much memory it has**: the chip answers for itself, and a ceiling here
  // would be a lie told to the firmware that runs on the glass (docs/85).
  (void)home_bytes;
  (void)extra_bytes;
}

void set_random_seed(uint32_t seed) {
  // **A board is not told its noise**: a firmware whose sequences can be made to repeat is a firmware whose keys can be
  // guessed, and the chip has a generator that nobody has to configure (docs/20, docs/85).
  (void)seed;
}

void set_memory_relief(void (*relief)(void* keep, size_t bytes)) { g_relief = relief; }

void delay_us(uint32_t us) {
  // **The chip's own busy wait**: on the board nothing asks for this —the time is the real one— and the
  // honest answer to "wait" is to wait (docs/85).
  if (us > 0) esp_rom_delay_us(us);
}

}  // namespace pal
