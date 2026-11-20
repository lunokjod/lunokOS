// **El motiu del reinici, que el xip diu i que aquí es tradueix** (0.190.68).
//
// `esp_reset_reason()` és una lectura d'un registre i no una opinió: val la pena preguntar-l'ho
// sempre que el firmware vulgui dir si una arrencada és la primera o una tornada, perquè la
// marca desada a la NVS es pot perdre —una placa que queda sense corrent just després d'escriure-la
// diria que ha dormit quan el que ha passat és que l'han desendollada— i el xip, en canvi, no
// menteix sobre això (docs/50, docs/60).
#include <esp_system.h>

#include "esp32/reset.h"

namespace hal {

ResetCause esp32_reset_cause() {
  switch (esp_reset_reason()) {
    case ESP_RST_DEEPSLEEP:
      return ResetCause::kDeepSleep;
    case ESP_RST_SW:
      return ResetCause::kSoftware;
    case ESP_RST_BROWNOUT:
      return ResetCause::kBrownout;
    case ESP_RST_EXT:
      return ResetCause::kExternal;
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
    case ESP_RST_SDIO:
      // El pànic, els gossos i el que el xip no ha pogut acabar d'arrencar: cap d'aquestes no
      // és ni una dormida ni un corrent que arriba, i totes volen dir el mateix a qui llegeix
      // el registre —que l'arrencada anterior no va poder acabar.
      return ResetCause::kPanic;
    case ESP_RST_POWERON:
    case ESP_RST_UNKNOWN:
    default:
      // `ESP_RST_UNKNOWN` és el que el xip diu quan no ho pot dir, i el que més s'hi assembla
      // és el corrent que arriba: una incògnita no s'ha de llegir com una dormida.
      return ResetCause::kPowerOn;
  }
}

}  // namespace hal
