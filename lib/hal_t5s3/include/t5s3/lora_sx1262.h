// **El SX1262 d'aquesta placa, i només per escoltar** (0.192.49).
//
// Pins del fabricant (`docs/pinmap.md` del repositori de la placa, branca `H752-01`, i el seu
// `examples/lora_recv/main/utilities.h`):
//
//   CS 46 · IRQ (DIO1) 10 · RST 1 · BUSY 47 · rail `LORA_EN` a l'expansor PCA9535 `IO0_0`
//
// **I el bus és el de la targeta**: SCK 14, MOSI 13 i MISO 21, o sigui SPI2, el mateix que la ranura SD. El seu exemple ho diu
// —*«LORA、SD use the same spi, in order to avoid mutual influence; before powering on, all CS signals should be pulled high
// and in an unselected state»*— i per això aquest driver **afegeix un dispositiu** a un bus que potser ja és a dalt en comptes
// d'inicialitzar-lo a la seva manera (docs/50, docs/70).
//
// **El que aquest fitxer no té**: cap `SetTx`. Ni `SetTx`, ni `SetPaConfig`, ni `SetTxParams`, ni un `SetPacketType` que
// deixi el xip a punt de parlar. Un monitor escolta, i una ràdio que no pot parlar és una ràdio que no pot fer malbé el
// que hi ha a l'aire (docs/30, docs/50).
#ifndef LUNOK_T5S3_LORA_SX1262_H
#define LUNOK_T5S3_LORA_SX1262_H

#include <stdint.h>

#include "esp32/i2c.h"
#include "hal/hal.h"
#include "t5s3/expander.h"

namespace hal {

// **Els pins, dits pel fabricant** (docs/pinmap.md de la placa): el CS del mòdul, la seva interrupció, el seu reset i la
// línia amb què diu que està ocupat. Són els mateixos que el HAL ja subjecta quan la placa baixa, i el comentari és aquí
// perquè qui llegeixi el driver no hagi d'anar a buscar-los (docs/50).
inline constexpr int kLoraCsPin = 46;
inline constexpr int kLoraIrqPin = 10;
inline constexpr int kLoraResetPin = 1;
inline constexpr int kLoraBusyPin = 47;

class Sx1262 : public ILora {
 public:
  // **El rail és de l'expansor** i per això el driver el rep en comptes de tocar els pins: encendre `LORA_EN` encén el
  // LoRa i el GPS alhora, que és el que el fabricant diu al seu `io_extend_lora_gps_power_on` (docs/50).
  explicit Sx1262(Pca9535* rail);
  ~Sx1262() override;

  LoraCaps caps() const override;
  bool begin() override;
  bool set_frequency(uint32_t hz) override;
  uint32_t frequency() const override { return config_.hz; }
  bool set_reception(const LoraConfig& config) override;
  LoraConfig reception() const override { return config_; }
  bool listen(uint32_t timeout_ms) override;
  bool sniff() override;
  int32_t rssi_dbm10() override;
  bool poll(LoraPacket& packet) override;
  bool idle() override;
  bool sleep() override;
  const char* last_error() const override { return error_; }

 private:
  // **Una comanda i una resposta**, en una sola transacció: el primer byte és l'opcode i els altres les dades, i el xip
  // contesta pel mateix MISO. El que torna és el que hi ha després de l'opcode, o sigui que un `Get` llegeix el que hi ha
  // a `out` i una comanda sense resposta no llegeix res (docs/50).
  bool command(uint8_t opcode, const uint8_t* params, size_t count);
  bool query(uint8_t opcode, uint8_t* out, size_t count);
  bool write_register(uint16_t address, const uint8_t* bytes, size_t count);
  // La línia BUSY del xip: alta mentre pensa. **Cada comanda del SX1262 ha d'esperar que baixi**, i una que no ho faci
  // arriba a un xip que encara no ha acabat l'anterior i es perd (docs/50).
  bool wait_busy(uint32_t timeout_ms);
  bool reset_the_chip();
  // La calibració de la imatge, que el fabricant dona per cinc bandes i que només cal repetir quan se'n canvia (docs/50).
  bool calibrate_the_image(uint32_t hz);
  // El mode de repòs del xip (`STDBY_RC`), que és on ha de ser abans de rebre qualsevol cosa (docs/50).
  bool standby();
  bool add_the_device();
  void say(const char* text);
  void fail(const char* text);

  Pca9535* rail_;
  // **El lloc del xip al bus**, opac aquí perquè aquesta capçalera no ha de portar el driver d'SPI del xip a dins (docs/50).
  void* device_ = nullptr;
  LoraConfig config_;
  const char* error_ = "";
  bool began_ = false;
  bool bus_ready_ = false;
};

// **El SX1262 d'aquesta placa**, construït la primera vegada que algú el demana. `nullptr` si el bus o el rail no hi són:
// una placa que no pot parlar amb el xip no ha de fer veure que l'escolta (docs/50).
Sx1262* t5s3_lora(Pca9535* rail);

}  // namespace hal

#endif  // LUNOK_T5S3_LORA_SX1262_H
