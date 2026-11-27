// **L'entrada del sistema** (0.190.90, i l'única des de la 0.190.176): `app_main`, que és el punt on la IDF deixa començar una aplicació.
//
// El cos és el de l'antic `setup()`/`loop()` del camí d'Arduino i no una còpia amb idees pròpies: el corrent de la placa abans que res, la
// PAL (consola i cua) i el nucli, i després el torn del nucli per sempre. **Aquest fitxer és de totes les bandes de placa i no d'una**
// (docs/50).
//
// **I el torn ja no és aquesta tasca** (0.192.96, l'amo: *«Fes-ho, passa-ho a psram fent una tasca nova al app_main. i Fes que l'app_main
// tingui la mínima ram necessaria»*): aquí es crea la tasca del nucli, el nucli arrenca i gira allà, i aquesta funció **torna**. Quan
// `app_main` torna, `main_task` fa `vTaskDelete(NULL)` i els seus kilobytes tornen al heap intern: el que queda aquí a baix només ha de
// cobrir el corrent, la PAL i el naixement de la tasca, i per això `CONFIG_ESP_MAIN_TASK_STACK_SIZE` baixa al valor de sèrie de la IDF
// (3 584 B, docs/50, docs/75). La pila d'aquesta tasca **no pot anar a la PSRAM** —el perquè és a `kTurnStack`— i el que sí que cal és que
// es demani **abans que res**, i això també és a `kTurnStack`.
//
// **`ESP_PLATFORM` i no una altra cosa**: aquest fitxer no ha d'entrar als builds natius —l'emulador i les proves tenen el seu
// `main` a `lib/hal_emulator/` i no tenen FreeRTOS—, i el que ho diu és la plataforma de la IDF (docs/50, docs/85).
#if defined(ESP_PLATFORM)

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/semphr.h>
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

namespace {

// **La pila del torn és a la RAM del xip, i no per gust** (0.192.97, 0.192.96 ho va provar a la PSRAM i la placa ho va dir a la primera
// arrencada): `spi_flash_disable_interrupts_caches_and_other_cpu` **afirma** que el punter de pila del corrent és a la DRAM
// (`cache_utils.c:154`, `esp_task_stack_is_sane_cache_disabled`), perquè escriure o esborrar la flaix apaga la cau i una pila a la PSRAM
// seria una pàgina que no es pot llegir enmig de l'operació. **I el torn és exactament la tasca que toca la flaix**: les sessions, `/cfg`,
// els fitxers de les apps, una captura. Amb la pila a la PSRAM el que surt és:
//
//     assert failed: spi_flash_disable_interrupts_caches_and_other_cpu cache_utils.c:154 (esp_task_stack_is_sane_cache_disabled())
//
// **I `xTaskCreate` no pot donar-li la PSRAM ni que es volgués**: a la IDF 5.1 el `pvPortMalloc` de FreeRTOS és
// `heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)` (`freertos/heap_idf.c:71`), o sigui que la pila de **qualsevol** tasca
// que no es creï amb la porta de les capacitats surt de la RAM del xip, sempre.
//
// **El que sí que compta és QUAN es demana aquests setze kilobytes** (0.192.98). La tasca de la IDF els demanava a l'arrencada del
// sistema, amb el munt encara sencer; la 0.192.96 els va passar a demanar-los al final d'`app_main`, i el que fa un `malloc` de setze
// kilobytes en un munt ja poblat és **partir pel mig el forat més gran que hi havia**. El resultat no és que hi hagi menys memòria: és
// que el **forat més gran** baixa, i la ràdio necessita un de sol de trenta kilobytes i escalars per encendre's. A la placa es va veure
// així —`free heap 79.3 KB` i `largest block 31.0 KB`, i `wifi connect` contestant `ESP_ERR_NO_MEM; 53623 B free, largest 26612 B`—,
// amb la ràdio havent funcionat abans del canvi. Per això la tasca es crea **aquí dalt, abans de la PAL i abans de qualsevol altra cosa
// d'aquest fitxer**, que és exactament el punt on la IDF la creava (docs/50, docs/75).
const uint32_t kTurnStack = 16 * 1024;
// **La prioritat i el nucli del torn són els que tenia la tasca de la IDF** (`ESP_TASK_MAIN_PRIO` = 1 i `CONFIG_ESP_MAIN_TASK_AFFINITY` =
// CPU0): el torn és el que ha de deixar pas a tot —el vidre, les tecles, la ràdio— i no a l'inrevés (docs/30, docs/50).
const uint8_t kTurnPriority = 1;
const BaseType_t kTurnCore = 0;

// **El marge de la tasca d'aquí**, dit abans de marxar: the water mark of the stack that is about to be given back, o sigui el número amb
// què es decideix si els 3 584 B de `CONFIG_ESP_MAIN_TASK_STACK_SIZE` són els que calen o si encara en sobren (docs/50, docs/75).
void say_the_stack_of_this_task() {
  const UBaseType_t words = uxTaskGetStackHighWaterMark(nullptr);
  ESP_LOGI("LUNOK", "the boot needed %u B of the main task's stack, of %u",
           static_cast<unsigned>(words * sizeof(StackType_t)),
           static_cast<unsigned>(CONFIG_ESP_MAIN_TASK_STACK_SIZE));
}

// **El nucli, i per sempre el seu torn**: el que abans hi havia al cos d'`app_main` després de la PAL (docs/30, docs/50). **Espera una
// senyal abans de fer res**: aquesta tasca neix abans que la consola existeixi —és el preu de demanar la seva pila amb el munt encara
// sencer— i sense la senyal un tic del rellotge entre la creació i `pal::init` el deixaria cridant `kernel::boot` sobre una PAL que encara
// no hi és. La senyal arriba per paràmetre i no per una variable compartida, que és el que fa que no hi hagi cap moment en què algú l'hagi
// de llegir mentre l'altre l'esborra (docs/50).
void the_turn(void* gate) {
  const SemaphoreHandle_t the_pal_is_up = static_cast<SemaphoreHandle_t>(gate);
  xSemaphoreTake(the_pal_is_up, portMAX_DELAY);
  vSemaphoreDelete(the_pal_is_up);
  kernel::boot(hal::platform());
  for (;;) {
    kernel::poll();
  }
}

}  // namespace

extern "C" void app_main() {
  // **Tres marques pel registre de la IDF, i no per al del sistema** (0.190.90): el primer flaix d'aquest entorn es va quedar mut després de
  // l'`entry` del bootloader i no hi havia manera de saber si el que hi havia era un penjament, un xoc o una consola mal posada. Aquestes
  // tres línies van pel mateix port que el bootloader i diuen on és la placa en cada moment: abans del corrent, després del corrent i
  // després de la PAL. Quan `pal::init` falla es diu i no es fa un bucle mut, que és el pitjor que pot fer una placa (docs/50, docs/85).
  ESP_LOGI("LUNOK", "the board is taking its power");
  hal::platform().hold_the_power();
  ESP_LOGI("LUNOK", "the kernel, in a task of its own");
  // **El torn, i aquesta és la primera cosa que demana memòria** (0.192.98): la seva pila és a la DRAM perquè la flaix ho exigeix (vegeu
  // `kTurnStack`), i el que sí que es tria és **quan**: aquí, amb el munt tal com el va deixar la IDF, i no al final d'`app_main`, on
  // setze kilobytes parteixen pel mig el forat que la ràdio necessita sencer (docs/50, docs/75). La tasca neix aturada a `gate`.
  const SemaphoreHandle_t gate = xSemaphoreCreateBinary();
  const bool born = gate != nullptr && xTaskCreatePinnedToCore(&the_turn, "lunok_turn", kTurnStack, static_cast<void*>(gate),
                                                              kTurnPriority, nullptr, kTurnCore) == pdPASS;
  if (!born) {
    ESP_LOGW("LUNOK", "the task of the turn did not start: the kernel runs on this task's stack, which is the boot's size");
    if (gate != nullptr) vSemaphoreDelete(gate);
  }
  ESP_LOGI("LUNOK", "the PAL: the console and the queue");
  if (!pal::init()) {
    ESP_LOGE("LUNOK", "the PAL did not come up: there is no console and no queue to run on");
    for (;;) {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }
  say_the_stack_of_this_task();
  if (!born) {
    // **No hi ha tasca i el nucli es queda aquí**: no es crida `the_turn` amb una senyal que ja no existeix (docs/50).
    kernel::boot(hal::platform());
    for (;;) {
      kernel::poll();
    }
  }
  // **I el nucli pot començar, i aquesta tasca s'acaba aquí**: `main_task` fa `vTaskDelete(NULL)` i els seus kilobytes tornen al heap
  // intern (docs/50, docs/75). La senyal és l'última cosa que aquesta tasca fa, i des d'aquí ja no hi ha res que la torni a mirar.
  xSemaphoreGive(gate);
}

#endif  // ESP_PLATFORM
