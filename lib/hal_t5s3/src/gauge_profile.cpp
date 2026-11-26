// **El perfil del comptador de coulombs, posat com el fabricant el posa** (0.192.14, l'amo: *«La batería no carrrga mai al 100%, perqué?»*,
// i després de veure el que deia el xip: *«the cell says 0 mA, 1136 of 3000 mAh of 3000»* en una cel·la de **1500**).
//
// El BQ27220 no endevina res: el seu `StateOfCharge()` surt d'un model de bateria —la corba CEDV— que viu al seu *data memory* i que algú hi ha
// d'escriure. La placa va sortir de fàbrica amb un model de **3000 mAh** en una cel·la de 1500, o sigui que el número que ensenya la barra és la
// meitat del que és: 1136 mAh reals sobre un «ple» de 3000 són el 38 % que l'amo veia amb la cel·la a 4158 mV.
//
// El firmware de fàbrica del fabricant l'hi escriu (`lib/BQ27220/bq27220_data_memory.c` del seu repositori) i aquest fitxer és **aquell perfil,
// xifra per xifra**: capacitat de disseny i plena 1500 mAh, EMF 3743, C0/R0/T0/R1 149/867/4030/316, TC 9, C1 0, els onze `StartDOD` i els tres
// `EDV`, la banda morta del corrent a 1 i el corrent de repòs a 1 mA, amb la `GaugingConfig` i les dues `OperationConfig` seves.
//
// **La seqüència és la del seu `BQ27220::init()`**: comprovar el número de xip, obrir el pany (unseal), comparar el que hi ha amb el perfil, i
// **només si no hi quadra** reiniciar el gauge, demanar accés complet, entrar al mode de configuració, escriure els paràmetres, sortir-ne
// reiniciant i tornar a tancar el pany. Si ja hi és, no s'escriu res: això és el que estalvia l'EEPROM del xip a cada arrencada (docs/50, docs/95).
#include "t5s3/gauge.h"

#include <cstring>

#include "hal/log.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace hal {
namespace {

const uint8_t kGaugeAddress = 0x55;
// El registre de control i els subcomandaments que aquest fitxer fa servir (SLUUBE7, i el `bq27220_def.h` del fabricant).
const uint8_t kControl = 0x00;
const uint8_t kOperationStatus = 0x3A;
const uint8_t kSelectSubclass = 0x3E;
const uint8_t kMacData = 0x40;
const uint8_t kMacDataSum = 0x60;
const uint8_t kMacDataLen = 0x61;
const uint16_t kDeviceNumber = 0x0001;
const uint16_t kReset = 0x0041;
const uint16_t kSealed = 0x0030;
const uint16_t kEnterConfigUpdate = 0x0090;
const uint16_t kExitConfigUpdateReinit = 0x0091;
const uint16_t kUnsealKey1 = 0x0414;
const uint16_t kUnsealKey2 = 0x3672;
const uint16_t kFullAccessKey = 0xFFFF;
const uint16_t kTheChip = 0x0220;  // el número de dispositiu d'un BQ27220
// Els temps del driver del fabricant, que són els de bqStudio: la pausa entre les dues claus, la de després d'escollir una subclasse, la de
// després d'escriure un paràmetre i la de després d'escriure'n el CRC (docs/95).
const uint32_t kMagicDelayMs = 5;
// Les dues claus del pany, el més juntes que el bus permet.
const uint32_t kKeyGapMs = 2;
const uint32_t kSelectDelayMs = 1;
// **El número de xip vol quinze mil·lisegons, i ho diu el driver del fabricant**: *«Enterprise wait (MAC read fails if less than 500us), bqstudio
// uses ~15ms»*. Amb un sol intent i un mil·lisegon, la placa va contestar `0x0000` i el perfil no s'hi va escriure (trobat al port sèrie,
// 25/11) (docs/95).
const uint32_t kDeviceNumberDelayMs = 15;
const uint8_t kDeviceNumberTries = 3;
const uint32_t kWriteDelayMs = 1;
const uint32_t kConfigDelayMs = 10;
// **El mode de configuració no arriba en quaranta mil·lisegons**: el driver del fabricant l'espera fins a **dos segons**
// (`BQ27220_TIMEOUT_COMMON_US` = 2 000 000 amb voltes d'un mil·lisegon), i el 25/11 el nostre ho esperava quaranta voltes i deia que no hi
// era. És el mateix nombre que fa servir per al reset (docs/50, docs/95).
const int kConfigUpdateTries = 2000;
const int kInitCompleteTries = 2000;

// **El perfil, tal com el fabricant el té escrit** (`bq27220_data_memory.c`): adreça del data memory, valor i mida en bytes. Els valors de dos
// bytes van **big-endian** al fil, que és com els demana el xip (docs/95).
struct Setting {
  uint16_t address;
  uint16_t value;
  uint8_t size;
};

const Setting kMakerProfile[] = {
    {0x929B, 0x0D31, 2},  // GaugingConfig: CCT, SC, FIXED_EDV0, FCC_LIM, FC_FOR_VDQ, IGNORE_SD
    {0x9206, 0x0C8C, 2},  // OperationConfigA
    {0x9208, 0x004C, 1},  // OperationConfigB
    {0x929D, 1500, 2},    // FullChargeCapacity, mAh
    {0x929F, 1500, 2},    // DesignCapacity, mAh
    {0x92A3, 3743, 2},    // EMF
    {0x92A9, 149, 2},     // C0
    {0x92AB, 867, 2},     // R0
    {0x92AD, 4030, 2},    // T0
    {0x92AF, 316, 2},     // R1
    {0x92B1, 9, 1},       // TC
    {0x92B2, 0, 1},       // C1
    {0x92BD, 4173, 2},    // StartDOD0
    {0x92BF, 4043, 2},    // StartDOD10
    {0x92C1, 3925, 2},    // StartDOD20
    {0x92C3, 3821, 2},    // StartDOD30
    {0x92C5, 3725, 2},    // StartDOD40
    {0x92C7, 3665, 2},    // StartDOD50
    {0x92C9, 3619, 2},    // StartDOD60
    {0x92CB, 3585, 2},    // StartDOD70
    {0x92CD, 3515, 2},    // StartDOD80
    {0x92CF, 3439, 2},    // StartDOD90
    {0x92D1, 2713, 2},    // StartDOD100
    {0x92B4, 3031, 2},    // EDV0
    {0x92B7, 3385, 2},    // EDV1
    {0x92BA, 3501, 2},    // EDV2
    {0x91DE, 1, 1},       // CurrentDeadband
    {0x9217, 1, 2},       // PowerSleepCurrent, mA
};
const uint8_t kProfileCount = static_cast<uint8_t>(sizeof(kMakerProfile) / sizeof(kMakerProfile[0]));

// **El gauge dorm i el primer que li parla el pot perdre** (0.190.31): el xip surt del seu repòs quan veu activitat al bus, o sigui que la
// primera transacció és la que es pot quedar pel camí. Es llegeix un registre que sempre hi és —la tensió— unes quantes vegades abans de
// demanar-li res de la memòria interna (docs/50, docs/95).
bool wake_the_gauge(II2c* bus) {
  uint8_t raw[2] = {0, 0};
  for (uint8_t attempt = 0; attempt < kDeviceNumberTries; ++attempt) {
    if (bus->read(kGaugeAddress, 0x08, raw, sizeof(raw))) {
      const uint16_t mv = static_cast<uint16_t>(raw[0] | (raw[1] << 8));
      if (mv > 1000 && mv < 5000) return true;
    }
    vTaskDelay(pdMS_TO_TICKS(kSelectDelayMs));
  }
  return false;
}

bool write_a_command(II2c* bus, uint16_t command) {
  const uint8_t bytes[2] = {static_cast<uint8_t>(command & 0xFF), static_cast<uint8_t>(command >> 8)};
  return bus->write(kGaugeAddress, bytes, sizeof(bytes));
}

uint16_t the_status(II2c* bus) {
  uint8_t raw[2] = {0, 0};
  if (!bus->read(kGaugeAddress, kOperationStatus, raw, sizeof(raw))) return 0xFFFF;
  return static_cast<uint16_t>(raw[0] | (raw[1] << 8));
}

// **Els tres panys** (SLUUBE7): bits 2:1 de l'estat, `11` tancat, `10` obert i `01` accés complet. Un `0xFFFF` vol dir que el registre no ha
// contestat i no es confon amb cap dels tres.
enum : uint8_t { kSealedState = 3, kUnsealedState = 2, kFullAccessState = 1 };

uint8_t the_lock(II2c* bus) {
  const uint16_t status = the_status(bus);
  if (status == 0xFFFF) return 0xFF;
  return static_cast<uint8_t>((status >> 1) & 0x03);
}

bool wait_for_the_flag(II2c* bus, uint16_t flag, int tries) {
  for (int i = 0; i < tries; ++i) {
    const uint16_t status = the_status(bus);
    if (status != 0xFFFF && (status & flag) != 0) return true;
    vTaskDelay(pdMS_TO_TICKS(kSelectDelayMs));
  }
  return false;
}

// **Llegir un paràmetre del data memory**: s'escull la subclasse (l'adreça del data memory, dos bytes little-endian, darrere del registre
// `0x3E`) i es llegeixen els bytes del bloc MAC.
bool read_a_setting(II2c* bus, uint16_t address, uint8_t* out, uint8_t size) {
  const uint8_t which[3] = {kSelectSubclass, static_cast<uint8_t>(address & 0xFF),
                            static_cast<uint8_t>(address >> 8)};
  if (!bus->write(kGaugeAddress, which, sizeof(which))) return false;
  vTaskDelay(pdMS_TO_TICKS(kSelectDelayMs));
  return bus->read(kGaugeAddress, kMacData, out, size);
}

// **Escriure un paràmetre**: el bloc, la suma de control a `0x60` i la llargada a `0x61`, que és el que el xip espera després de cada escriptura
// (el driver del fabricant ho fa exactament així i la suma és `0xFF` menys la suma de l'adreça i les dades).
bool write_a_setting(II2c* bus, const Setting& one) {
  uint8_t block[5] = {0, 0, 0, 0, 0};
  block[0] = static_cast<uint8_t>(one.address & 0xFF);
  block[1] = static_cast<uint8_t>(one.address >> 8);
  if (one.size == 1) {
    block[2] = static_cast<uint8_t>(one.value & 0xFF);
  } else {
    block[2] = static_cast<uint8_t>(one.value >> 8);
    block[3] = static_cast<uint8_t>(one.value & 0xFF);
  }
  const size_t in_the_block = static_cast<size_t>(one.size) + 2;
  uint8_t frame[6] = {kSelectSubclass, 0, 0, 0, 0, 0};
  memcpy(frame + 1, block, in_the_block);
  if (!bus->write(kGaugeAddress, frame, in_the_block + 1)) return false;
  vTaskDelay(pdMS_TO_TICKS(kWriteDelayMs));
  uint8_t sum = 0;
  for (size_t i = 0; i < in_the_block; ++i) sum = static_cast<uint8_t>(sum + block[i]);
  // La suma a `0x60`, la llargada a `0x61` i el bloc darrere: registre + `2 + mida` bytes, que és la trama del driver del fabricant.
  const uint8_t sum_frame[5] = {kMacDataSum, static_cast<uint8_t>(0xFF - sum), static_cast<uint8_t>(in_the_block + 2),
                                block[2], block[3]};
  if (!bus->write(kGaugeAddress, sum_frame, static_cast<size_t>(one.size) + 3)) return false;
  vTaskDelay(pdMS_TO_TICKS(kConfigDelayMs));
  return true;
}

// **El perfil hi és o no hi és**: es llegeix cada paràmetre i es compara. Un que no contesti és un «no», perquè el que no s'ha pogut llegir no es
// pot donar per bo.
// `say` diu a quin paràmetre no li ha pres el valor, que és el que falta per tancar-ho.
bool the_profile_is_there(II2c* bus, uint8_t& first_wrong, uint8_t& wrong, bool say) {
  first_wrong = 0xFF;
  wrong = 0;
  for (uint8_t i = 0; i < kProfileCount; ++i) {
    const Setting& one = kMakerProfile[i];
    uint8_t got[2] = {0, 0};
    uint16_t value = 0xFFFF;
    if (read_a_setting(bus, one.address, got, one.size)) {
      value = (one.size == 1) ? got[0] : static_cast<uint16_t>((got[0] << 8) | got[1]);
    }
    if (value != one.value) {
      ++wrong;
      if (first_wrong == 0xFF) first_wrong = i;
      if (say && wrong <= 6) {
        logf(pal::kLogWarn, "GAUGE", "0x%04x is 0x%04x and the maker says 0x%04x", one.address, value, one.value);
      }
    }
  }
  return wrong == 0;
}

}  // namespace

bool T5Gauge::write_the_maker_profile() {
  if (bus_ == nullptr) return false;
  // **El número de xip, primer**: escriure un perfil a un xip que no és aquest és pitjor que no escriure'n cap. I el xip s'ha de despertar
  // abans de preguntar-l'hi, o la primera pregunta es perd i la resposta és un `0x0000` que no vol dir res (25/11, al port sèrie).
  if (!wake_the_gauge(bus_)) {
    logf(pal::kLogWarn, "GAUGE", "the gauge did not answer even its voltage: its profile is not touched");
    return false;
  }
  // **L'estat del xip, dit abans de res**: el registre d'operació (0x3A) i el pany (bits 2:1) són la meitat del diagnòstic quan el número de xip
  // no arriba, i una línia que els digui val més que tres intents a cegues (0.192.17, la placa del 25/11 va contestar `0x0000` tres vegades).
  const uint16_t status = the_status(bus_);
  logf(pal::kLogInfo, "GAUGE", "the gauge's operation status is 0x%04x and its lock is %u", status,
       static_cast<unsigned>(the_lock(bus_)));
  // Les dues claus, amb tres intents: la primera escriptura al xip pot fallar i no ha de deixar el pany tancat per sempre.
  bool was_locked = false;
  for (uint8_t attempt = 0; attempt < 3 && the_lock(bus_) == kSealedState; ++attempt) {
    was_locked = true;
    write_a_command(bus_, kUnsealKey1);
    vTaskDelay(pdMS_TO_TICKS(kKeyGapMs));
    write_a_command(bus_, kUnsealKey2);
    vTaskDelay(pdMS_TO_TICKS(kMagicDelayMs));
  }
  if (the_lock(bus_) == kSealedState) {
    logf(pal::kLogInfo, "GAUGE", "the gauge's lock did not open: its profile is not read and nothing is written");
    return false;
  }
  // **La prova d'identitat que sí que funciona: la memòria de dades del propi xip** (0.192.18). El número de dispositiu d'aquest gauge no
  // arriba mai —tres intents, quinze mil·lisegons i el pany obert, i sempre `0x0000`—, però el *data memory* sí: es demana la **capacitat de
  // disseny** (`0x929F`) i, si és un nombre de mAh d'una cel·la (de 100 a 20 000), aquest xip és un comptador de veritat i el seu perfil es pot
  // escriure. Una lectura que no arriba o un zero és un xip que no és el que creiem, i llavors no es toca (docs/50, docs/95).
  uint8_t design[2] = {0, 0};
  const bool design_read = read_a_setting(bus_, 0x929F, design, sizeof(design));
  const uint16_t design_mah = static_cast<uint16_t>((design[0] << 8) | design[1]);
  if (!design_read || design_mah < 100 || design_mah > 20000) {
    // **I el número de dispositiu, com a informació**: es pregunta una vegada i es diu què ha contestat, perquè és el que el fabricant mira i
    // el registre ha de poder explicar per què aquí no s'ha fet servir (docs/85, docs/95).
    write_a_command(bus_, kDeviceNumber);
    vTaskDelay(pdMS_TO_TICKS(kDeviceNumberDelayMs));
    uint8_t id[2] = {0, 0};
    bus_->read(kGaugeAddress, kMacData, id, sizeof(id));
    logf(pal::kLogWarn, "GAUGE",
         "the gauge's data memory does not answer (design capacity %u mAh, the device number 0x%04x): its profile is not touched",
         static_cast<unsigned>(design_mah), static_cast<unsigned>(id[0] | (id[1] << 8)));
    if (was_locked) {
      write_a_command(bus_, kSealed);
      vTaskDelay(pdMS_TO_TICKS(kMagicDelayMs));
    }
    return false;
  }
  logf(pal::kLogInfo, "GAUGE", "the gauge's data memory answers: %u mAh of design capacity",
       static_cast<unsigned>(design_mah));
  // **I si el perfil ja hi és, no s'hi escriu res**: la comparació és el que estalvia l'EEPROM del xip a cada arrencada.
  uint8_t first = 0xFF;
  uint8_t wrong = 0;
  if (the_profile_is_there(bus_, first, wrong, /*say=*/false)) {
    if (was_locked) {
      write_a_command(bus_, kSealed);
      vTaskDelay(pdMS_TO_TICKS(kMagicDelayMs));
    }
    logf(pal::kLogInfo, "GAUGE", "the gauge already has the maker's profile: 1500 mAh, nothing written");
    return true;
  }
  logf(pal::kLogWarn, "GAUGE",
       "%u of %u setting(s) of the gauge are not the maker's (the first is 0x%04x): it is reset and written", wrong,
       static_cast<unsigned>(kProfileCount), kMakerProfile[first].address);
  // **Reiniciar el gauge i demanar accés complet**, que és el que el fabricant fa abans d'escriure el data memory.
  if (!write_a_command(bus_, kReset)) return false;
  vTaskDelay(pdMS_TO_TICKS(kMagicDelayMs));
  if (!wait_for_the_flag(bus_, 0x0020, kInitCompleteTries)) {  // INITCOMP, bit 5 de l'estat d'operació
    logf(pal::kLogWarn, "GAUGE", "the gauge did not finish its reset: nothing is written");
    return false;
  }
  // **Un reset torna a tancar el pany, o sigui que es torna a obrir**: el que s'obre una vegada no es pot donar per obert després d'un reinici.
  if (the_lock(bus_) == kSealedState) {
    write_a_command(bus_, kUnsealKey1);
    vTaskDelay(pdMS_TO_TICKS(kMagicDelayMs));
    write_a_command(bus_, kUnsealKey2);
    vTaskDelay(pdMS_TO_TICKS(kMagicDelayMs));
  }
  write_a_command(bus_, kFullAccessKey);
  vTaskDelay(pdMS_TO_TICKS(kMagicDelayMs));
  write_a_command(bus_, kFullAccessKey);
  vTaskDelay(pdMS_TO_TICKS(kMagicDelayMs));
  if (the_lock(bus_) == kSealedState) {
    logf(pal::kLogWarn, "GAUGE", "the gauge did not give full access: nothing is written");
    return false;
  }
  // Les dues ordres del mode de configuració van pel punter del data memory: en aquest xip el registre de control no les pren.
  bool in_the_configured_mode = false;
  {
    const uint8_t which[3] = {kSelectSubclass, static_cast<uint8_t>(kEnterConfigUpdate & 0xFF),
                              static_cast<uint8_t>(kEnterConfigUpdate >> 8)};
    if (bus_->write(kGaugeAddress, which, sizeof(which))) {
      in_the_configured_mode = wait_for_the_flag(bus_, 0x0400, kConfigUpdateTries);
    }
  }
  if (!in_the_configured_mode) {
    logf(pal::kLogWarn, "GAUGE", "the gauge did not enter its configuration mode (status 0x%04x): nothing is written",
         the_status(bus_));
    return false;
  }
  logf(pal::kLogInfo, "GAUGE", "the gauge is in its configuration mode");
  // Cada paràmetre es comprova després d'escriure'l i se'n fa un segon intent si no s'ha pres.
  bool ok = true;
  for (uint8_t i = 0; i < kProfileCount; ++i) {
    const Setting& one = kMakerProfile[i];
    uint16_t value = 0xFFFF;
    bool wrote = false;
    for (uint8_t attempt = 0; attempt < 2; ++attempt) {
      wrote = write_a_setting(bus_, one);
      uint8_t got[2] = {0, 0};
      if (read_a_setting(bus_, one.address, got, one.size)) {
        value = (one.size == 1) ? got[0] : static_cast<uint16_t>((got[0] << 8) | got[1]);
      }
      if (wrote && value == one.value) break;
    }
    if (!wrote || value != one.value) {
      ok = false;
      logf(pal::kLogWarn, "GAUGE", "0x%04x stays at 0x%04x and the maker says 0x%04x", one.address, value, one.value);
    }
  }
  // Sortir reiniciant, pel mateix punter: és el que fa que el gauge rellegeixi el model que acaba de rebre.
  {
    const uint8_t which[3] = {kSelectSubclass, static_cast<uint8_t>(kExitConfigUpdateReinit & 0xFF),
                              static_cast<uint8_t>(kExitConfigUpdateReinit >> 8)};
    ok = bus_->write(kGaugeAddress, which, sizeof(which)) && ok;
  }
  vTaskDelay(pdMS_TO_TICKS(kMagicDelayMs));
  // El pany es torna a tancar com pugui: si el registre de control no el pren, el gauge es queda obert, que no és un problema de funcionament.
  write_a_command(bus_, kSealed);
  vTaskDelay(pdMS_TO_TICKS(kMagicDelayMs));
  // **El que ha quedat dit pel propi xip**, tornant a comparar: una escriptura que no s'ha pres s'ha de veure aquí i no d'aquí a un mes.
  if (!write_a_command(bus_, kUnsealKey1)) return false;
  vTaskDelay(pdMS_TO_TICKS(kMagicDelayMs));
  write_a_command(bus_, kUnsealKey2);
  vTaskDelay(pdMS_TO_TICKS(kMagicDelayMs));
  uint8_t still = 0;
  const bool there = the_profile_is_there(bus_, first, still, /*say=*/true);
  write_a_command(bus_, kSealed);
  vTaskDelay(pdMS_TO_TICKS(kMagicDelayMs));
  if (there) {
    logf(pal::kLogInfo, "GAUGE",
         "the gauge took the maker's profile: 1500 mAh of design and full, and the CEDV curve of this cell");
  } else {
    logf(pal::kLogWarn, "GAUGE", "the gauge did not take the maker's profile: %u setting(s) are still its own",
         static_cast<unsigned>(still));
  }
  return there && ok;
}

}  // namespace hal
