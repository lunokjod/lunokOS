// **L'entrada del sistema** (0.190.90, i l'única des de la 0.190.176): `app_main`, que és el punt on la IDF deixa començar una aplicació.
//
// El cos és el de l'antic `setup()`/`loop()` del camí d'Arduino i no una còpia amb idees pròpies: el corrent de la placa abans que res, la
// PAL (consola i cua) i el nucli, i després el torn del nucli per sempre. **Aquest fitxer és de totes les bandes de placa i no d'una**
// (docs/50).
//
// **`ESP_PLATFORM` i no una altra cosa**: aquest fitxer no ha d'entrar als builds natius —l'emulador i les proves tenen el seu
// `main` a `lib/hal_emulator/` i no tenen FreeRTOS—, i el que ho diu és la plataforma de la IDF (docs/50, docs/85).
#if defined(ESP_PLATFORM)

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "hal/hal.h"
#include "kernel/kernel.h"
#include "pal/pal.h"

// **I les peces de la IDF que el nostre codi inclou** (0.190.90): una llibreria de PlatformIO no és un component d'IDF, o sigui que el
// seu cercador de dependències no sap que fem servir el LittleFS, el USB-Serial-JTAG o el rellotge del xip; citar-los aquí és el que els
// fa entrar al build i el que posa les seves capçaleres a l'abast de `lib/` (docs/50, docs/80). **L'ADC vell no hi és a propòsit**: la
// seva capçalera crida sola que està obsoleta i el component entra igualment per `battery.cpp`, que és qui el fa servir (docs/50).
#include <driver/uart.h>
#include <driver/usb_serial_jtag.h>
#include <esp_littlefs.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <nvs.h>
#include <soc/rtc.h>

extern "C" void app_main() {
  // **Tres marques pel registre de la IDF, i no per al del sistema** (0.190.90): el primer flaix d'aquest entorn es va quedar mut després de
  // l'`entry` del bootloader i no hi havia manera de saber si el que hi havia era un penjament, un xoc o una consola mal posada. Aquestes
  // tres línies van pel mateix port que el bootloader i diuen on és la placa en cada moment: abans del corrent, després del corrent i
  // després de la PAL. Quan `pal::init` falla es diu i no es fa un bucle mut, que és el pitjor que pot fer una placa (docs/50, docs/85).
  ESP_LOGI("LUNOK", "the board is taking its power");
  hal::platform().hold_the_power();
  ESP_LOGI("LUNOK", "the PAL: the console and the queue");
  if (!pal::init()) {
    ESP_LOGE("LUNOK", "the PAL did not come up: there is no console and no queue to run on");
    for (;;) {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }
  ESP_LOGI("LUNOK", "the kernel");
  kernel::boot(hal::platform());
  for (;;) {
    kernel::poll();
  }
}

#endif  // ESP_PLATFORM
