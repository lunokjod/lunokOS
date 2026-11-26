// **La ràdio que només escolta, dita al nucli** (0.192.49). El driver és del HAL i el xip no sap res de Lua; aquí hi ha el
// que una app pot demanar, els comptadors del que costa i la porta que impedeix que una app que ha petat deixi el xip
// escoltant per sempre (docs/30, docs/50).
#include "kernel/lora.h"

#include "kernel/meter.h"
#include "hal/log.h"
#include "pal/pal.h"

#include "luabind/luabind.h"

namespace kernel {
namespace lora {
namespace {

hal::ILora* g_lora = nullptr;
// **Si la ràdio està escoltant i per a qui** (0.192.49): `true` vol dir que el xip està en recepció i que hi ha d'haver un
// intèrpret a qui importi. Quan no n'hi ha, el torn la posa en repòs i ho diu una vegada: una ràdio encesa per una app que
// ja no existeix és energia que algú paga i que ningú no ha demanat (docs/30, docs/50).
bool g_listening = false;
bool g_said_the_orphan = false;

}  // namespace

// **Les portes, amb el comptador del nucli**: una mesura de LoRa costa un canvi de mode del xip i el que ha de dir el preu
// és el mateix metre que cronometra el vidre i la targeta (docs/30, docs/95).
bool begin() {
  if (g_lora == nullptr) return false;
  return g_lora->begin();
}

bool set_frequency(uint32_t hz) {
  if (g_lora == nullptr) return false;
  return g_lora->set_frequency(hz);
}

uint32_t frequency() { return (g_lora != nullptr) ? g_lora->frequency() : 0; }

bool set_reception(const hal::LoraConfig& config) {
  if (g_lora == nullptr) return false;
  return g_lora->set_reception(config);
}

bool listen(uint32_t timeout_ms) {
  if (g_lora == nullptr) return false;
  meter::Scope scope(meter::kLoraListen);
  if (!g_lora->listen(timeout_ms)) return false;
  g_listening = true;
  g_said_the_orphan = false;
  return true;
}

bool sniff() {
  if (g_lora == nullptr) return false;
  meter::Scope scope(meter::kLoraSniff);
  if (!g_lora->sniff()) return false;
  g_listening = true;
  g_said_the_orphan = false;
  return true;
}

int32_t rssi_dbm10() { return (g_lora != nullptr) ? g_lora->rssi_dbm10() : 0; }

bool poll_packet(hal::LoraPacket& packet) {
  if (g_lora == nullptr) return false;
  meter::Scope scope(meter::kLoraPoll);
  return g_lora->poll(packet);
}

bool idle() {
  if (g_lora == nullptr) return false;
  g_listening = false;
  return g_lora->idle();
}

bool sleep() {
  if (g_lora == nullptr) return false;
  g_listening = false;
  return g_lora->sleep();
}

bool caps(hal::LoraCaps& out) {
  if (g_lora == nullptr) return false;
  out = g_lora->caps();
  return true;
}

const char* last_error() { return (g_lora != nullptr) ? g_lora->last_error() : "this board has no radio"; }

bool boot(hal::IPlatform& platform) {
  g_lora = platform.lora();
  // **El que Lua pot veure**: un punter per porta i res més, i la taula sencera absent quan no hi ha ràdio —una crida que no
  // hi és no és una crida que menteix (docs/30).
  luabind::LoraOps ops = {};
  if (g_lora != nullptr) {
    ops.caps = &caps;
    ops.begin = &begin;
    ops.frequency_set = &set_frequency;
    ops.frequency_get = &frequency;
    ops.reception_set = &set_reception;
    ops.listen = &listen;
    ops.sniff = &sniff;
    ops.rssi = &rssi_dbm10;
    ops.poll = &poll_packet;
    ops.idle = &idle;
    ops.sleep = &sleep;
    ops.last_error = &last_error;
  }
  luabind::set_lora_ops(ops);
  return g_lora != nullptr;
}

bool available() { return g_lora != nullptr; }

// **Una ràdio encesa sense amo** (0.192.49): the interpreter is the only thing that can ask for a packet, o sigui que si
// no n'hi ha i el xip està en recepció, algú ha mort sense tancar-la. Es posa en repòs i es diu una vegada —un registre que
// s'omple a cada torn seria pitjor que el silenci (docs/30, docs/50).
void poll() {
  if (g_lora == nullptr || !g_listening) return;
  if (luabind::running()) return;
  (void)g_lora->idle();
  g_listening = false;
  if (!g_said_the_orphan) {
    g_said_the_orphan = true;
    // The line says what happened and not who did it, because the app that did it is gone (docs/30).
    hal::logf(pal::kLogInfo, "LORA", "the radio was left listening by an app that is gone: it goes to rest");
  }
}

}  // namespace lora
}  // namespace kernel
