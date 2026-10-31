// PAL for ESP32 + Arduino.
//
// Two rules live here and nowhere else: how to queue events (FreeRTOS) and how
// to talk to the serial port (Arduino Serial). The kernel sees neither.
#include <Arduino.h>
#include <Esp.h>
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

  void reboot() override { ESP.restart(); }
  bool reboot_requested() const override { return false; }

  uint32_t random() override { return esp_random(); }

  HeapInfo heap() const override {
    HeapInfo info;
    info.valid = true;
    info.free_bytes = static_cast<uint32_t>(ESP.getFreeHeap());
    info.min_free_bytes = static_cast<uint32_t>(ESP.getMinFreeHeap());
    return info;
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

}  // namespace pal
