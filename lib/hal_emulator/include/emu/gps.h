// **El cel de mentida de l'emulador** (0.192.63).
//
// El model de la T5S3 diu que porta un receptor GNSS, i un emulador sense cel faria una pàgina de GPS que no es pot provar: una
// pantalla buida no distingeix «encara no hi ha posició» de «el driver no llegeix res». El que hi ha aquí és **un receptor
// inventat i declarat**: un mòdul que contesta que és un `MIA-M10Q`, que triga uns segons a tenir posició —com el de debò, que
// ha de sentir el cel— i que després camina per un lloc que existeix, amb els satèl·lits del seu horitzó i l'hora del cel. **El
// que s'imita és el contracte i no el cable**: no hi ha cap NMEA ni cap UART, perquè el que una app veu és `hal::IGps` i no un
// port sèrie (docs/50, docs/85).
#ifndef LUNOK_EMU_GPS_H
#define LUNOK_EMU_GPS_H

#include "hal/hal.h"

namespace emu {

// **El cel d'aquest emulador**, amb la mateixa interfície que el mòdul de debò (docs/50, docs/85).
class FakeGps : public hal::IGps {
 public:
  hal::GpsCaps caps() const override;
  bool on() override;
  bool off() override;
  bool poll(hal::GpsFix& out) override;
  const hal::GpsSatellite* satellites(uint8_t& count) const override;
  const char* last_error() const override { return error_; }

 private:
  // **El que el receptor ha après**: el temps encès (que és el que fa que la posició arribi tard), el camí que porta i el cel
  // d'aquest moment. Tot surt de `pal::system().millis()`, o sigui que dues voltes del mateix run donen els mateixos números
  // (docs/85).
  void wake_up_if_needed();
  void walk_to(uint32_t ms);

  bool powered_ = false;
  uint32_t on_ms_ = 0;
  uint32_t spoken_ = 0;  // les preguntes que s'han contestat, que és el que compta com una frase llegida
  hal::GpsFix fix_;
  hal::GpsSatellite sky_[12];
  uint8_t sky_count_ = 0;
  const char* error_ = "";
};

// The one of this run, or nullptr when the model does not carry the receiver (docs/50).
FakeGps* emu_gps();

}  // namespace emu

#endif  // LUNOK_EMU_GPS_H
