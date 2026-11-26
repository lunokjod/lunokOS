// **L'aire de mentida de l'emulador: portadores i paquets d'on no n'arriba cap** (0.192.49).
//
// El model de la T5S3 diu que porta un SX1262, i un emulador sense res a l'altre costat de l'antena faria una pàgina
// d'escombrat que no es pot provar: una línia plana que no distingeix un aire buit d'un driver que no llegeix res. El que hi
// ha aquí és **un espectre inventat i declarat**: tres nodes de LoRa a la banda de 868 amb el seu factor d'expansió i el seu
// període, un polsador a 433,92 que només fa portadora —el que un monitor ha de saber distingir d'un paquet— i un terra de
// soroll que es mou. **No hi ha cap manera de transmetre'l**: aquest emulador només escolta, com la placa (docs/50, docs/85).
#ifndef LUNOK_EMU_LORA_H
#define LUNOK_EMU_LORA_H

#include "hal/hal.h"

namespace emu {

// **El monitor de l'aire d'aquest emulador**, amb la mateixa interfície que el xip de debò (docs/50, docs/85).
class FakeLora : public hal::ILora {
 public:
  hal::LoraCaps caps() const override;
  bool begin() override;
  bool set_frequency(uint32_t hz) override;
  uint32_t frequency() const override { return config_.hz; }
  bool set_reception(const hal::LoraConfig& config) override;
  hal::LoraConfig reception() const override { return config_; }
  bool listen(uint32_t timeout_ms) override;
  bool sniff() override;
  int32_t rssi_dbm10() override;
  bool poll(hal::LoraPacket& packet) override;
  bool idle() override;
  bool sleep() override;
  const char* last_error() const override { return error_; }

 private:
  hal::LoraConfig config_;
  // **El darrer paquet de cada node que ja s'ha entregat**: una finestra de recepció que dura més que una volta del monitor
  // tornaria a donar el mateix paquet a cada pregunta, i un monitor que compta els que arriben ha de comptar-los un cop (docs/85).
  uint32_t last_serial_[4] = {0, 0, 0, 0};
  bool listening_ = false;
  bool sniffing_ = false;
  const char* error_ = "";
};

// The one of this run, or nullptr when the model does not carry the radio (docs/50).
FakeLora* emu_lora();

}  // namespace emu

#endif  // LUNOK_EMU_LORA_H
