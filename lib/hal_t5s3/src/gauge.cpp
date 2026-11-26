// The gauge of the T5 E-Paper S3 Pro: a BQ27220 that counts and a BQ25896 that charges (docs/50).
#include "t5s3/gauge.h"

#include <cstring>

#include "hal/log.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace hal {
namespace {

// **Les dues adreces i les tres ordres d'aquest xip** (BQ27220, SLUUBE7): `Voltage()` a 0x08 i
// `StateOfCharge()` a 0x2C, totes dues de dos bytes i el primer el més baix —little endian, com
// tota la família—, i un byte d'ordre que es queda al registre del xip entre lectures.
const uint8_t kGaugeAddress = 0x55;
const uint8_t kVoltage = 0x08;
const uint8_t kStateOfCharge = 0x2C;
// **El mapa del fabricant, llegit del seu propi fitxer i no deduït** (0.190.155, l'amo: *«Les adreces 0x0C/0x0E/0x10 les he
// deduïdes del mapa del xip <- revisa els exemples del fabricant»*). `lib/BQ27220/bq27220_def.h` del repositori de la placa diu
// `CommandCurrent 0x0C`, `CommandRemainingCapacity 0x10` i `CommandFullChargeCapacity 0x12`, o sigui que **les capacitats no són
// on les vaig posar**: el mapa salta del corrent (0x0C, dos bytes) directament a la càrrega que queda, i el 0x0E que jo havia
// suposat no existeix. Les unitats són les que el seu exemple imprimeix: mA amb signe, mAh i mAh (docs/50, docs/95).
const uint8_t kCurrent = 0x0C;
const uint8_t kRemainingCapacity = 0x10;
const uint8_t kFullChargeCapacity = 0x12;
// **I les quatre que diuen com és la cel·la** (0.190.156, el mateix `bq27220_def.h` del fabricant): `Temperature()` a 0x06
// —**dècimes de kelvin**, que és el que el seu exemple imprimeix amb `getTemperature() / 10.0`—, `CycleCount()` a 0x2A,
// `StateOfHealth()` a 0x2E i `DesignCapacity()` a 0x3C, que és la que la cel·la era de nova i contra la qual es llegeix la salut.
// **El registre d'estat de la bateria** (0.192.16): `BatteryStatus` a `0x0A`, amb el bit 9 `FC` («full-charged is detected») i el bit 6
// `TCA` («terminate charge alarm»), que és la paraula del comptador sobre si la cel·la és plena. El mapa és el del `bq27220.h` del fabricant.
const uint8_t kBatteryStatus = 0x0A;
const uint16_t kFullCharged = 1u << 9;
const uint16_t kTerminateChargeAlarm = 1u << 6;
const uint8_t kTemperature = 0x06;
const uint8_t kCycleCount = 0x2A;
const uint8_t kStateOfHealth = 0x2E;
const uint8_t kDesignCapacity = 0x3C;
// **Kelvin a centígrads, en dècimes**: 273.15 K is zero, o sigui que 2731 is the offset of a tenth of a degree and the number the
// app shows is exact to the tenth the part measures (docs/50, docs/95).
const int kKelvinToCelsius10 = 2731;

// And the charger's one register: REG0B, where bits 4:3 are `CHRG_STAT` —00 not charging, 01
// pre-charge, 10 fast charge, 11 done— and bit 5 is `PG_STAT`, the power that is good (BQ25896,
// SLUSCQ1). What a person sees is the bolt: a cable that is feeding the cell.
const uint8_t kChargerAddress = 0x6B;
const uint8_t kChargerStatus = 0x0B;
// **Els quatre registres de mesura del carregador** (0.190.156, `PowersBQ25896.tpp` del fabricant): VSYS a REG0F, el termistor a
// REG10, VBUS a REG11 i el corrent de càrrega a REG12. Les escales també són les seves: 20 mV des de 2,304 V, 0,465 % des de 21,
// 100 mV des de 2,6 V i 50 mA per pas amb els set bits de baix (docs/50, docs/95).
const uint8_t kChargerVsys = 0x0F;
const uint8_t kChargerNtc = 0x10;
const uint8_t kChargerVbus = 0x11;
const uint8_t kChargerCurrent = 0x12;
// **I el registre que apaga la placa** (0.190.33): REG09 bit 5 is `BATFET_DIS`, the transistor between the cell and the
// board, and it is the maker's own way of switching this board off (`LilyGoT5_Shutdown` in its repository). It does
// nothing while the cable is in (docs/50).
const uint8_t kChargerControl = 0x09;
const uint8_t kBatteryFetDisable = 0x20;
// **Els set registres que configuren la càrrega** (0.192.13, el `bq25896_init` del firmware de fàbrica del fabricant i
// `PowersBQ25896.tpp`): REG00 el límit d'entrada i el pin `ILIM`, REG02 l'ADC, REG03 el rail del sistema i la càrrega encesa, REG04 el
// corrent de càrrega ràpida, REG05 el de pre-càrrega, REG06 la tensió objectiu i REG07 el watchdog. Les escales són les seves i estan
// escrites a la vora de cada escriptura (docs/50, docs/95).
const uint8_t kChargerInput = 0x00;
const uint8_t kChargerAdc = 0x02;
const uint8_t kChargerPowerPath = 0x03;
const uint8_t kChargerFastCurrent = 0x04;
const uint8_t kChargerPreCurrent = 0x05;
const uint8_t kChargerVoltage = 0x06;
const uint8_t kChargerWatchdog = 0x07;

// **El BQ27220 es desperta quan veu activitat al bus** (0.190.31, el log del 19/11): the gauge of this board answered
// `ESP_ERR_TIMEOUT` on a bus where the touch controller (0x5D) and the clock chip (0x51) answered at once, and the
// datasheet's own answer to that is that the chip leaves SHUTDOWN when something talks to it —the first transaction is
// the one it can lose—. So a read that fails is asked again a couple of times with a few milliseconds between, and that
// is not a blind loop: it is the wake the chip describes (docs/50).
const uint8_t kTries = 3;
const uint32_t kWaitMs = 3;

bool read_with_a_wake(II2c* bus, uint8_t command, uint8_t* out, size_t len) {
  for (uint8_t attempt = 0; attempt < kTries; ++attempt) {
    if (bus->read(kGaugeAddress, command, out, len)) return true;
    vTaskDelay(pdMS_TO_TICKS(kWaitMs));
  }
  return false;
}

uint16_t little_endian(const uint8_t* bytes, size_t len) {
  uint16_t value = 0;
  for (size_t i = 0; i < len && i < 2; ++i) value = static_cast<uint16_t>(value | (bytes[i] << (8 * i)));
  return value;
}

}  // namespace

// **El carregador, posat com el fabricant el posa** (0.192.13): the numbers are the ones of its factory firmware and the register
// arithmetic is the one of its own driver (`PowersBQ25896.tpp`), **excepte un defecte del seu driver que no copiem**: el seu
// `setSysPowerDownVoltage` desplaça el registre sencer un bit a l'esquerra i acaba encenent l'OTG; aquí el camp del rail del sistema es
// toca al seu lloc (bits 3:1). Cada escriptura és read-modify-write, perquè el registre porta altres bits que no són nostres (docs/50).
//
// **Per què això fa que la cel·la arribi al 100 %**: amb el valor de sèrie el límit d'entrada és **500 mA** i el pin `ILIM` mana sobre el
// corrent de càrrega, o sigui que la fase de tensió constant s'arrossega i la caiguda de corrent que el comptador de coulombs necessita per
// declarar la cel·la plena no arriba mai; amb els números del fabricant (3250 mA d'entrada, 1024 mA de càrrega, 4208 mV objectiu) la
// càrrega acaba com ha d'acabar i el percentatge del BQ27220 puja fins al 100 (docs/50, docs/95).
bool T5Gauge::set_the_charger_as_the_maker_does() {
  if (bus_ == nullptr) return false;
  bool ok = true;
  uint8_t value = 0;
  // REG00: `EN_ILIM` fora (bit 6) i el límit d'entrada a 3250 mA —50 mA per pas des de 100 mA—, que és el sostre del xip i el que el
  // fabricant posa (docs/95).
  if (bus_->read(kChargerAddress, kChargerInput, &value, 1)) {
    uint8_t next = static_cast<uint8_t>(value & 0xC0);
    next = static_cast<uint8_t>(next | ((3250 - 100) / 50));
    const uint8_t write_in[2] = {kChargerInput, static_cast<uint8_t>(next & 0xBF)};
    ok = bus_->write(kChargerAddress, write_in, sizeof(write_in)) && ok;
  } else {
    ok = false;
  }
  // REG02 bit 6: l'ADC en mode continu. Sense això les mesures de VSYS, VBUS i IBAT no s'actualitzen i el que es llegeix és el que hi
  // havia (el comentari del fabricant: *«To obtain voltage data, the ADC must be enabled first»*) (docs/95).
  if (bus_->read(kChargerAddress, kChargerAdc, &value, 1)) {
    const uint8_t write_in[2] = {kChargerAdc, static_cast<uint8_t>(value | 0x40)};
    ok = bus_->write(kChargerAddress, write_in, sizeof(write_in)) && ok;
  } else {
    ok = false;
  }
  // REG03: el rail del sistema no baixa de 3300 mV (bits 3:1, 100 mV per pas des de 3000) i la càrrega queda encesa (`CHG_CONFIG`, bit 4).
  if (bus_->read(kChargerAddress, kChargerPowerPath, &value, 1)) {
    uint8_t next = static_cast<uint8_t>(value & ~0x0E);
    next = static_cast<uint8_t>(next | (((3300 - 3000) / 100) << 1));
    next = static_cast<uint8_t>(next | 0x10);
    const uint8_t write_in[2] = {kChargerPowerPath, next};
    ok = bus_->write(kChargerAddress, write_in, sizeof(write_in)) && ok;
  } else {
    ok = false;
  }
  // REG04: el corrent de càrrega ràpida, 64 mA per pas, fins a 1024 mA (el bit 7 és el de la limitació per temperatura i es queda com és).
  if (bus_->read(kChargerAddress, kChargerFastCurrent, &value, 1)) {
    const uint8_t next = static_cast<uint8_t>((value & 0x80) | (1024 / 64));
    const uint8_t write_in[2] = {kChargerFastCurrent, next};
    ok = bus_->write(kChargerAddress, write_in, sizeof(write_in)) && ok;
  } else {
    ok = false;
  }
  // REG05: la pre-càrrega, 64 mA per pas des de 64 mA, o sigui el primer esglaó (el de menys corrent) —el que el fabricant posa.
  if (bus_->read(kChargerAddress, kChargerPreCurrent, &value, 1)) {
    const uint8_t next = static_cast<uint8_t>((value & 0x0F) | (((64 - 64) / 64) << 4));
    const uint8_t write_in[2] = {kChargerPreCurrent, next};
    ok = bus_->write(kChargerAddress, write_in, sizeof(write_in)) && ok;
  } else {
    ok = false;
  }
  // REG06: la tensió objectiu, 16 mV per pas des de 3840 mV, o sigui **4208 mV** —el número del fabricant i el d'aquesta cel·la.
  if (bus_->read(kChargerAddress, kChargerVoltage, &value, 1)) {
    const uint8_t next = static_cast<uint8_t>((value & 0x03) | (((4208 - 3840) / 16) << 2));
    const uint8_t write_in[2] = {kChargerVoltage, next};
    ok = bus_->write(kChargerAddress, write_in, sizeof(write_in)) && ok;
  } else {
    ok = false;
  }
  // REG07 bits 5:4: el watchdog a zero, que és el que el seu `init()` fa abans de tocar res. Amb el watchdog armat i ningú que el
  // pentini, el xip es reinicia sol a la meitat d'una càrrega (docs/95).
  if (bus_->read(kChargerAddress, kChargerWatchdog, &value, 1)) {
    const uint8_t write_in[2] = {kChargerWatchdog, static_cast<uint8_t>(value & 0xCF)};
    ok = bus_->write(kChargerAddress, write_in, sizeof(write_in)) && ok;
  } else {
    ok = false;
  }
  if (ok) {
    logf(pal::kLogInfo, "GAUGE",
         "the charger is set as the maker sets it: input 3250 mA, charge 1024 mA, target 4208 mV, the ADC on and no watchdog");
  } else {
    logf(pal::kLogWarn, "GAUGE", "the charger did not take the maker's settings: a register of it did not answer");
  }
  return ok;
}

// **El cable, preguntat sol** (0.190.272): `VBUS_GD` de REG11 és el bit que el carregador posa quan el cable hi és i la
// tensió d'entrada és bona, i és el mateix registre que `charger()` llegeix per dir els mil·livolts. Una lectura que no
// arriba contesta que no: una placa no es declara endollada per un bus que calla (docs/50, docs/95).
bool T5Gauge::on_a_cable() const {
  if (bus_ == nullptr) return false;
  uint8_t vbus = 0;
  if (!bus_->read(kChargerAddress, kChargerVbus, &vbus, 1)) return false;
  return (vbus & 0x80) != 0;
}

bool T5Gauge::shut_down() {
  if (bus_ == nullptr) return false;
  // **Read, or, write**: the register has other bits in it and the maker's own example keeps them, o sigui que el que
  // es gira és un sol bit i la resta queda com estava (docs/50).
  uint8_t value = 0;
  if (!bus_->read(kChargerAddress, kChargerControl, &value, 1)) return false;
  const uint8_t off[2] = {kChargerControl, static_cast<uint8_t>(value | kBatteryFetDisable)};
  return bus_->write(kChargerAddress, off, sizeof(off));
}

bool T5Gauge::detail(IPower::BatteryDetail& out) {
  out = IPower::BatteryDetail{};
  if (bus_ == nullptr) return false;
  uint8_t raw[2] = {0, 0};
  bool any = false;

  // **El corrent, amb signe** (0.190.155): the gauge counts coulombs, o sigui que el signe és la direcció —positiu mentre la
  // cel·la es carrega i negatiu mentre alimenta la placa— i és el número que explica el pendent del gràfic. Un valor fora de
  // vuit amperes no és una mesura: és un bus que ha contestat una altra cosa (docs/50, docs/95).
  if (read_with_a_wake(bus_, kCurrent, raw, sizeof(raw))) {
    const int16_t ma = static_cast<int16_t>(little_endian(raw, sizeof(raw)));
    if (ma > -8000 && ma < 8000) {
      out.current_ma = static_cast<int>(ma);
      any = true;
    }
  }

  // **Les capacitats, i l'una valida l'altra**: the full one is the cell this board was sold with (a thousand-odd mAh) and the
  // remaining one can never be more than it; si no quadren no es diu cap de les dues, perquè un número sol no es pot desmentir
  // (docs/50, docs/95).
  int full = IPower::kUnknown;
  int left = IPower::kUnknown;
  if (read_with_a_wake(bus_, kFullChargeCapacity, raw, sizeof(raw))) {
    const int mah = static_cast<int>(little_endian(raw, sizeof(raw)));
    if (mah >= 100 && mah <= 20000) full = mah;
  }
  if (read_with_a_wake(bus_, kRemainingCapacity, raw, sizeof(raw))) {
    const int mah = static_cast<int>(little_endian(raw, sizeof(raw)));
    if (mah >= 0 && mah <= 20000) left = mah;
  }
  if (full != IPower::kUnknown && left != IPower::kUnknown && left <= full) {
    out.full_mah = full;
    out.left_mah = left;
    any = true;
  }

  // **La cel·la com és, no com estava** (0.190.156): the temperature in tenths of a degree Celsius, the health the gauge trusts,
  // the cycles it has counted and the capacity the cell was sold with. Cada un amb el seu rang i independent: una cel·la que no diu
  // la salut no ha de fer perdre el corrent (docs/50, docs/95).
  uint8_t raw2[2] = {0, 0};
  if (read_with_a_wake(bus_, kTemperature, raw2, sizeof(raw2))) {
    const int kelvin10 = static_cast<int>(little_endian(raw2, sizeof(raw2)));
    // De 220 a 350 K és el que una cel·la de liti pot dir; fora d'això és un registre que ha contestat una altra cosa.
    if (kelvin10 > 2200 && kelvin10 < 3500) {
      out.temp_c10 = kelvin10 - kKelvinToCelsius10;
      any = true;
    }
  }
  if (read_with_a_wake(bus_, kStateOfHealth, raw2, sizeof(raw2))) {
    const int pct = static_cast<int>(little_endian(raw2, sizeof(raw2)));
    if (pct > 0 && pct <= 100) {
      out.health = pct;
      any = true;
    }
  }
  if (read_with_a_wake(bus_, kCycleCount, raw2, sizeof(raw2))) {
    const int count = static_cast<int>(little_endian(raw2, sizeof(raw2)));
    if (count >= 0 && count < 10000) {
      out.cycles = count;
      any = true;
    }
  }
  if (read_with_a_wake(bus_, kDesignCapacity, raw2, sizeof(raw2))) {
    const int mah = static_cast<int>(little_endian(raw2, sizeof(raw2)));
    if (mah >= 100 && mah <= 20000) {
      out.design_mah = mah;
      any = true;
    }
  }

  // **I el que el gauge diu del ple** (0.192.16): `FC` i `TCA` del `BatteryStatus`. És el que converteix un «38 %» en una frase: el comptador
  // no considera la cel·la plena, i amb 4159 mV això vol dir que el que balla és el seu model (docs/50, docs/95).
  uint8_t state[2] = {0, 0};
  if (read_with_a_wake(bus_, kBatteryStatus, state, sizeof(state))) {
    const uint16_t bits = little_endian(state, sizeof(state));
    out.declared_full = (bits & kFullCharged) != 0;
    out.terminate_alarm = (bits & kTerminateChargeAlarm) != 0;
    any = true;
  }

  // **Una línia, una vegada, perquè una prova de foc es pugui fer des del prompt** (0.190.155): the numbers of a gauge that has
  // never been read on this hardware are worth what a person can check them against, i el registre és el lloc on es veuen al
  // costat del voltatge i del percentatge que ja eren bons (docs/50, docs/95).
  static bool said = false;
  if (!said && any) {
    said = true;
    logf(pal::kLogInfo, "GAUGE",
         "the cell says %d mA, %d of %d mAh of %d, %.1f C, health %d, %d cycles", out.current_ma, out.left_mah,
         out.full_mah, out.design_mah, static_cast<double>(out.temp_c10) / 10.0, out.health, out.cycles);
  }
  return any;
}

bool T5Gauge::charger(IPower::ChargerDetail& out) {
  out = IPower::ChargerDetail{};
  if (bus_ == nullptr) return false;
  bool any = false;

  // **L'estat en un sol registre** (0.190.156, `PowersBQ25896.tpp`): REG0B porta `BUS_STAT` als bits 7:5, `CHRG_STAT` als 4:3 i la
  // bona alimentació al 2. El fabricant els tradueix amb les mateixes taules que hi ha aquí, i els mots són els seus (docs/50, docs/95).
  uint8_t status = 0;
  if (bus_->read(kChargerAddress, kChargerStatus, &status, 1)) {
    static const char* const kInputs[8] = {"no input", "USB host", "adapter", "OTG", "?", "?", "?", "?"};
    static const char* const kCharges[4] = {"not charging", "pre-charge", "fast", "done"};
    out.input = kInputs[(status >> 5) & 0x07];
    out.charge = kCharges[(status >> 3) & 0x03];
    out.power_good = (status & 0x04) != 0;
    any = true;
  }

  // **El corrent que entra a la cel·la**: REG12, set bits i passos de 50 mA, i **zero quan no s'està carregant** —el registre es
  // queda amb l'últim valor si el cable marxa, i el fabricant el filtra per l'estat igual que això (docs/50, docs/95).
  uint8_t current = 0;
  if (bus_->read(kChargerAddress, kChargerCurrent, &current, 1)) {
    if (strcmp(out.charge, "not charging") == 0) {
      out.ichgr_ma = 0;  // the register keeps the last value; the state is the one that says nobody is charging
      any = true;
    } else {
      const int ma = (current & 0x7F) * 50;
      if (ma >= 0 && ma <= 6000) {
        out.ichgr_ma = ma;
        any = true;
      }
    }
  }

  // **I les dues tensions que el xip mesura sol**: VBUS a REG11 (bit 7 diu si hi ha cable, i els set bits de baix són 100 mV des de
  // 2,6 V) i VSYS a REG0F (20 mV des de 2,304 V), que és el rail del qual viu la placa quan la cel·la no la pot alimentar (docs/95).
  uint8_t vbus = 0;
  if (bus_->read(kChargerAddress, kChargerVbus, &vbus, 1)) {
    if ((vbus & 0x80) != 0) {
      out.vbus_mv = (vbus & 0x7F) * 100 + 2600;
      any = true;
    }
  }
  uint8_t vsys = 0;
  if (bus_->read(kChargerAddress, kChargerVsys, &vsys, 1)) {
    if ((vsys & 0x7F) != 0) {
      out.vsys_mv = (vsys & 0x7F) * 20 + 2304;
      any = true;
    }
  }

  // **El termistor, en percentatge de REGN i no en graus**: the BQ25896 measures the NTC against its own rail, o sigui que el que
  // en surt és un número de control del carregador i no una temperatura; la de la cel·la la diu el BQ27220 (docs/95).
  uint8_t ntc = 0;
  if (bus_->read(kChargerAddress, kChargerNtc, &ntc, 1)) {
    if ((ntc & 0x7F) != 0) {
      out.ntc_pct = ((ntc & 0x7F) * 465) / 1000 + 21;
      any = true;
    }
  }
  return any;
}

bool T5Gauge::read(int& mv, int& pct, bool& charging) {
  if (bus_ == nullptr) return false;
  uint8_t raw[2] = {0, 0};
  if (!read_with_a_wake(bus_, kVoltage, raw, sizeof(raw))) return false;
  const uint16_t millivolts = little_endian(raw, sizeof(raw));
  // A number outside a single cell is not a measurement: under a volt is a gauge that is not there
  // and over five is a bus that answered something else (docs/50).
  if (millivolts < 1000 || millivolts > 5000) return false;
  if (!read_with_a_wake(bus_, kStateOfCharge, raw, sizeof(raw))) return false;
  const uint16_t percent = little_endian(raw, sizeof(raw));
  if (percent > 100) return false;
  mv = static_cast<int>(millivolts);
  pct = static_cast<int>(percent);
  // **I el cable**: a register that does not answer is «not charging» and not a failure —the cell's
  // two numbers are already a good answer, and a board on a cable that cannot be read is a board
  // whose bolt is missing, not a board without a battery (docs/50).
  charging = false;
  uint8_t status = 0;
  if (bus_->read(kChargerAddress, kChargerStatus, &status, 1)) {
    const uint8_t charge_state = static_cast<uint8_t>((status >> 3) & 0x03);
    charging = (charge_state == 1 || charge_state == 2);
  }
  return true;
}

}  // namespace hal
