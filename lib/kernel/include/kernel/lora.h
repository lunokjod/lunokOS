// **El servei de LoRa del nucli** (0.192.49): el que una app pot demanar de la ràdio que només escolta, i el que el
// sistema li dona. És prim a propòsit —el xip és del HAL i la política de què es mira és de la pàgina— i té dues feines
// que no són del driver: **comptar el que costa** i **no deixar la ràdio encesa quan l'app que la va encendre ja no hi és**
// (docs/30, docs/50).
#ifndef LUNOK_KERNEL_LORA_H
#define LUNOK_KERNEL_LORA_H

#include <stdint.h>

#include "hal/hal.h"

namespace kernel {
namespace lora {

// Installs what Lua may ask for. False on a board with no radio, and then every `lora.*` answers `E_UNSUPPORTED` instead
// of pretending (docs/30).
bool boot(hal::IPlatform& platform);

// Wether this board has one at all.
bool available();

// **El repòs quan l'app que la va engegar ja no hi és** (0.192.49): the chip in receive draws milliamps, i una app que peta
// deixa l'intèrpret sense ningú que el torni a posar a dormir. El torn del nucli ho mira i ho posa en repòs, i ho diu una
// vegada al registre perquè una ràdio encesa per un mort sigui una cosa que es pugui trobar (docs/30, docs/50).
void poll();

// The three doors the interpreter uses, with their meter.
bool begin();
bool set_frequency(uint32_t hz);
uint32_t frequency();
bool set_reception(const hal::LoraConfig& config);
bool listen(uint32_t timeout_ms);
bool sniff();
int32_t rssi_dbm10();
bool poll_packet(hal::LoraPacket& packet);
bool idle();
bool sleep();
bool caps(hal::LoraCaps& out);
const char* last_error();

}  // namespace lora
}  // namespace kernel

#endif  // LUNOK_KERNEL_LORA_H
