// **Aquest fitxer ja no és cap punt d'entrada** (0.190.176): el sistema arrenca a `src/main_idf.cpp` (`app_main`), i el que hi havia
// aquí era el `setup()`/`loop()` del build d'Arduino, que va marxar amb els seus entorns (0.190.171). **Es queda el fitxer i el seu
// símbol** perquè `src/` no quedi buit —PlatformIO es nega a compilar un entorn el `src/` del qual és buit— i per no esborrar res que
// algú vulgui mirar (docs/50, docs/80).
extern "C" void lunok_board_entry_stub() {}
