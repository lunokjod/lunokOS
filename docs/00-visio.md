# 00 · Visió

## Què és

lunokOS és un sistema per a ESP32 que executa **una app escrita en Lua alhora**. L'app no veu
maquinari: veu capacitats. Si la placa on corre té pantalla, l'app pot pintar-hi; si no en té, la
mateixa app ha de poder fer la feina pel port sèrie o dir que no pot.

L'objectiu no és competir amb un sistema operatiu de debò. És tenir un sistema petit, previsible i
**reparable**, on canviar d'idea és editar un `.lua`, i on el maquinari concret de cada placa queda
tancat en una carpeta.

## Els deu principis

1. **Una sola app alhora.** No és una limitació tècnica sinó una decisió: una sola VM viva vol dir
   que el sostre de memòria es pot raonar, que no hi ha fugues entre apps i que un error mai no és
   d'una altra app. La feina de fons es fa amb alarmes, no amb un segon intèrpret.

2. **El maquinari és una capacitat, no una obligació.** Cada placa declara què té. Una crida a una
   cosa que no hi és retorna `E_UNSUPPORTED` en comptes de petar, i `sys.caps()` ho diu abans de
   provar-ho.

3. **Res no penja el sistema.** Tot el que pot trigar passa per una promesa: cap crida de C feta des
   de Lua bloqueja el nucli. I el comptador d'instruccions **hi és des de la 0.90.0**: cada torn té
   100 000 instruccions i, en esgotar-se, la tasca fa `lua_yield` i el nucli recupera el torn; una app
   que no cedeix no penja la placa, corre a trossos (docs/20, docs/75).

4. **La memòria és el bé escàs.** L'únic que alloca dinàmicament és Lua, i amb un sostre. La resta
   són taules i buffers fixos. Cap fitxer es carrega sencer.

5. **Tot el que espera, espera amb una promesa.** No hi ha una segona manera d'esperar. Això fa que
   el codi d'una app sigui sempre igual de llegible, tant si llegeix un fitxer com si baixa una
   pàgina web.

6. **El que es pot provar al PC, es prova al PC.** El mateix codi compila per a l'emulador. El que
   només es pot mesurar a la placa —consum, temps real, desgast de la flash— es mesura a la placa i
   es diu que s'ha mesurat allà.

7. **El sistema sempre es pot recuperar.** Si una app trenca, el prompt `lunok>` torna. Si una app
   es menja la memòria, es tanca. Si una app no cedeix mai, el comptador li talla el torn i el nucli
   recupera el comandament entre trossos —**no es mata**: les tres ESC o `!!exit` la tanquen, i
   `docs/20` diu per què el sistema no pot distingir una app penjada d'un joc—. El camí de tornada no
   depèn de Lua.

8. **Una sola cua temporal, ordenada, i només se'n programa la més propera.** El maquinari només ha
   de gestionar un despertador; la resta és una llista ordenada a la RAM.

9. **Res d'amagat.** El que una app no pot fer es diu amb un error, no amb una excepció silenciosa.
   El que el sistema no ha pogut mesurar es diu que està pendent.

10. **El codi en anglès, els documents en català.** Identificadors, cadenes d'interfície i
    comentaris en anglès; la documentació i els missatges per a l'amo, en català.

## No-objectius

- **No hi ha aïllament de memòria real.** No hi ha MMU, i el model de permisos del manifest encara
  no existeix (docs/30): quan hi sigui serà una barrera per evitar errors, no una frontera de
  seguretat.
- **No hi ha multitasca preemptiva.** Les coroutines cedeixen quan volen i el comptador les atura si
  no ho fan.
- **No és un simulador de maquinari.** L'emulador corre el sistema de debò i en paga els temps —els
  blocs de la flash, els sectors de la targeta, el bus i la tinta del panell, les transaccions del
  tàctil, el torn del nucli (docs/85)—, però **no modela el consum, el desgast de la flash ni la
  física del refresc** d'una pantalla d'e-ink: en modela els mil·lisegons de cada mode, no la tinta.
- **No és un sistema multiusuari** ni té comptes, ni permisos per persona, ni xarxa de confiança.
- **No executa binaris d'altres sistemes** ni cap app que no sigui un directori amb un manifest.
- **El firmware no pretén ser portable a altres arquitectures** que no siguin ESP32. L'emulador de PC
  sí que vol ser portable: la finestra ja és SDL2 amb X11 de reserva, i la consola i el disc s'hi aniran
  portant per fases (docs/85).

## Glossari

| Terme | Què és |
|---|---|
| **App** | Un directori `apps/<id>/` amb un `manifest.json` i un entry en Lua |
| **Manifest** | El fitxer que descriu l'app: títol, entry, versió, les plaques on corre i, si obre fitxers, l'extensió |
| **Primer pla** | L'app que té el port sèrie i l'atenció de l'usuari. Només n'hi ha una |
| **Finestra d'alarma** | El moment en què el nucli carrega una app per executar-li `on_alarm` i la torna a tancar |
| **Alarma** | Un treball temporal persistent, sense cap funció de Lua a dins: només un identificador i un payload |
| **Capacitat** | Una cosa que una placa pot tenir o no (pantalla, xarxa, bateria…). Es consulta abans de fer-la servir |
| **Placa** | Una implementació de `IPlatform`: la que parla amb el maquinari |
| **PAL** | *Platform Abstraction Layer*: el que el nucli demana al sistema de sota (cua, temporitzador, consola, log) |
| **HAL** | *Hardware Abstraction Layer*: les interfícies de dispositiu (pantalla, entrada, xarxa, energia) |
| **Muntatge** | Un camí arrel (`/littlefs`, `/nvs`) que lliga un camí virtual amb un backend |
| **Finestra de lectura** | El tros fix de RAM que un fitxer obert té carregat; es mou quan cal |
| **Promesa** | L'objecte que retorna tota operació que pot trigar; es pot encadenar o esperar amb `await` |
| **Tasca** | Una coroutine de l'app que el nucli pot reprendre. No és un fil del sistema |
| **Pressupost** | El màxim de memòria o d'instruccions que una app pot gastar abans que el nucli la talli |
| **Handler** | L'app que declara que pot obrir una extensió de fitxer (`opens`) |
