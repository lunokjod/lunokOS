// PAL for ESP32 + Arduino.
//
// Two rules live here and nowhere else: how to queue events (FreeRTOS) and how
// to talk to the serial port (Arduino Serial). The kernel sees neither.
#include <Arduino.h>
#include <Esp.h>
#include <esp_heap_caps.h>
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

  uint32_t random() override { return esp_random(); }

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

class EspExtraMemory : public IExtraMemory {
 public:
  void* allocate(size_t bytes, bool prefer_extra) override {
    if (bytes == 0) return nullptr;
    if (prefer_extra) {
      if (void* block = heap_caps_malloc(bytes, kExtraCaps)) return block;
      return heap_caps_malloc(bytes, kHomeCaps);  // the extra memory is full, the block still fits
    }
    if (void* block = heap_caps_malloc(bytes, kHomeCaps)) return block;
    // A board with no PSRAM has nowhere else to look and this is the answer it gives (docs/75).
    return heap_caps_malloc(bytes, kExtraCaps);
  }

  void* resize(void* block, size_t bytes, bool prefer_extra) override {
    if (block == nullptr) return allocate(bytes, prefer_extra);
    if (bytes == 0) {
      release(block);
      return nullptr;
    }
    const uint32_t home = prefer_extra ? kExtraCaps : kHomeCaps;
    if (void* moved = heap_caps_realloc(block, bytes, home)) return moved;
    // The same block in the other home: `heap_caps_realloc` allocates, copies and frees, which is
    // exactly what moving a block to the other memory means. On failure the block is still the one
    // the caller gave and it is not touched (docs/75).
    return heap_caps_realloc(block, bytes, prefer_extra ? kHomeCaps : kExtraCaps);
  }

  void release(void* block) override { heap_caps_free(block); }
};

EspExtraMemory g_extra;

}  // namespace

bool init() {
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
  // **The tick of the chip wakes the system by itself**: a slice is a bench's way of looking at a window, and
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

void delay_us(uint32_t us) {
  // **The chip's own busy wait**: on the board nothing asks for this —the time is the real one— and the
  // honest answer to "wait" is to wait (docs/85).
  if (us > 0) esp_rom_delay_us(us);
}

}  // namespace pal
