# 20 · Intèrpret Lua

## Versió i per què

**Lua 5.4.x**, vendoritzat a `lib/lua/` (no és una dependència que es baixi sola: el codi és al
repositori i es pot tocar `luaconf.h`).

Per què 5.4 i no 5.1 (estil NodeMCU) ni 5.3:

- **Enters de debò.** Sense enters, mitja API de temps i de fitxers es torna un embolic de números
  en coma flotant.
- **`<close>` i `goto`** fan que el codi d'app no hagi de fer malabarismes per tancar fitxers.
- **Coroutines madures**, que són la base de les promeses.
- 5.1 està congelada i no té res que ens faci falta que no tinguem.

## Flags de compilació

Els dos tipus de número **no es fixen per línia d'ordres**: en 5.4.7 `luaconf.h` defineix
`LUA_INT_TYPE` i `LUA_FLOAT_TYPE` incondicionalment i un `-D` queda sobreescrit. Per això els fonts
són al repositori i no són una dependència que es baixi a compilar: la línia modificada és

```c
#define LUA_FLOAT_DEFAULT   LUA_FLOAT_FLOAT /* lunokOS: 32-bit floats */
```

Hi ha **un segon pedaç** al mateix fitxer, i no és de gust sinó de necessitat: el `newlib` de
l'ESP32 amaga `LLONG_MAX` quan la capçalera s'inclou des de C++, i llavors `luaconf.h` peta amb
«Compiler does not support 'long long'». Al costat de l'`#include <limits.h>` s'hi afegeix:

```c
#if !defined(LLONG_MAX) && defined(__LONG_LONG_MAX__)
#define LLONG_MAX   __LONG_LONG_MAX__
#define LLONG_MIN   (-__LONG_LONG_MAX__ - 1LL)
#define ULLONG_MAX  (__LONG_LONG_MAX__ * 2ULL + 1ULL)
#endif
```

I una trampa que no és cap pedaç però s'ha de saber: **les capçaleres de Lua no porten `extern "C"`
propi** (en 5.4.7 això viu a `lua.hpp`). Qui les inclogui des de C++ ha de fer

```cpp
extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}
```

Si no, els símbols surten amb nom desmanglat (`_Z15lua_pushintegerP9lua_Statex`) i l'enllaçador diu
que `liblua` no els té, quan sí que els té.

| Decisió | Valor | Motiu |
|---|---|---|
| Enters | 64 bits (`LUA_INT_LONGLONG`) | L'època Unix en segons no cap en 32 bits (2038) |
| Floats | 32 bits (`LUA_FLOAT_FLOAT`) | N'hi ha prou per a temps, mides i coordenades, i estalvia RAM |
| `LUAI_MAXSTACK` | el d'origen, pendent d'ajustar | La pila per defecte és pensada per a un PC |
| `LUA_USE_POSIX` | **no** | No hi ha `popen` ni `system` |

Conseqüències que la documentació d'usuari ha de recordar:

- Un **enter** és de 64 bits i és exacte sempre. Una **fita** (temps en segons, mil·lisegons) ha de
  viatjar com a enter.
- Un **número** (`lua_Number`) és un `float` de 32 bits: **perd precisió per sobre de 16 777 216**.
  Una app no ha de comptar mil·lisegons en un `float` (per això `sys.time_ms()` torna un enter).
- Les operacions amb enters es queden en enters; no hi ha promoció automàtica a coma flotant.

## Mòduls que hi són

Els que s'obren, i **només** aquests (`open_libraries` a `lib/luabind/src/luabind.cpp`):

| Mòdul | Estat |
|---|---|
| `base` | Sí |
| `coroutine` | Sí: és la base de les promeses |
| `table` | Sí |
| `string` | Sí |
| `math` | Sí. **`math.random` es sembra amb el soroll de la banda** (0.174.8): cada arrencada té la seva seqüència, i `math.randomseed(n)` la fixa quan una app vol reproduir-se (vegeu «Determinisme») |
| `utf8` | Sí |
| `os`, `io`, `package`, `debug` | **No hi són**: ni `os.time`, ni `io.write`. El temps el dona `time.*`, la consola `console.*`, i els fitxers `fs.*` (docs/30). `package` hi és **des del 0.148.0 com una taula amb un sol camp, `loaded`**, i no té ni `path` ni `cpath` ni searchers: `require` és una porta i no un carregador (vegeu «Sandbox») |
| mòduls en C, binaris precompilats | **No hi són**: `dofile`, `loadfile` i `load` es posen a `nil`, i cap `.so` no es pot obrir |

## Sandbox

Cada app s'executa amb un `_ENV` propi:

- Només hi ha els globals de la llibreria base que no permeten escapar-se'n, els mòduls de la taula
  de `30 · API` i les funcions que l'app declari.
- `dofile`, `loadfile` i `load` **no hi són** (es posen a `nil`): una app no pot obrir un fitxer i cridar-lo.
- **`require` hi és des del 0.148.0 i és una porta amb política, no un carregador** (docs/46, D6):
  `require('shelf')` llegeix **`shelf.lua` de la carpeta on es va instal·lar l'app** —el flaix o la targeta, i l'app
  no ho sap mai—, el corre **una vegada** i desa el que torna a `package.loaded`; el nom respon el mateix valor a
  cada `require` següent i un mòdul sense valor és `true`. **El nom no pot pujar (`..`), no pot començar per `/` i no
  porta punts**: `/` és una carpeta dins de l'app (`require('pages/chapter')` és `pages/chapter.lua`), i el qui ho
  comprova és el nucli, que és qui sap on és l'app. **No hi ha camí de cerca**, ni `package.path`, ni mòduls en C, i
  `load`/`loadfile`/`dofile` segueixen a `nil`.
  El motiu és **l'espai**: cada mòdul és un chunk propi, amb el seu sostre de codi i els seus **200 locals**, i una
  app de mil cinc-centes línies (el lector d'EPUB) no ha de capsolar-ho tot al chunk principal. Un mòdul que no hi
  cap al bloc de 64 KB es refusa sencer (`E_TOOBIG`) i no es corre a mitges.
- **Dos mòduls que es demanen l'un a l'altre no pengen**: el nom es marca com a carregat **abans** de córrer el
  mòdul, o sigui que es responen `true` en comptes de recórrer la pila de l'intèrpret fins a esgotar-la.
- `print` va a la consola de l'app (no a l'stdout del nucli).

## Pressupostos

| Pressupost | Com es compta | Què passa en excedir-lo |
|---|---|---|
| **Memòria** | L'assignador de Lua porta el compte del que ha donat i el compara amb el sostre de la placa (`IPlatform::heap_limit_kb()`: si la placa no en diu cap, **131 072 B** —els 128 KB de `kDefaultHeapBytes`, `lib/kernel/src/shell.cpp`— i **4 096 KB al Paper**, que és la PSRAM mapejada) | L'assignador falla, Lua llança `E_NOMEM` i l'app rep l'error |
| **Instruccions** | `lua_sethook` amb comptador: **20 000 per torn** de sèrie (0.174.7, eren 100 000), en blocs de 2 000, i és **un ajust de la placa**: `cfg turn <n>`, de 2 000 a 200 000, que `sys.turn()` llegeix des de Lua | En esgotar-se, la tasca fa `lua_yield` i torna al nucli; **no es mata**. Un `while true do end` corre a trossos i la placa no es penja; `!!exit`/3 ESC la tanquen (docs/75) |
| **Temps de torn** | El comptador talla el torn: una app que cedeix torna al nucli a cada espera, i una que no cedeix hi torna igualment quan el comptador s'esgota | Es torna a la cua de tasques |

**Per què el torn és curt** (0.174.7): el pressupost és **la latència del sistema i no el seu rendiment**. Un torn de
l'app que arriba fins al final és un torn en què ningú no ha llegit el vidre, les tecles ni la ràdio, i la pregunta de
l'amo va ser exactament aquesta —*«vull un sistema que respongui ràpid als botons i el touch»*—: amb 100 000
instruccions (6 ms al model de 60 ns) un dit esperava sis mil·lisegons perquè el nucli el mirés; amb 20 000 en són
1,2. El preu és el de sempre en aquesta casa: menys codi d'app per torn i més torns, o sigui que el que es guanya és
resposta i el que es paga és una mica de rendiment, i el número el tria la placa perquè és la placa qui ho pateix
(`cfg turn`). El terra és **un bloc** (2 000: per sota, el comptador no veuria el pressupost i no es tallaria mai) i el
sostre **200 000** (12 ms: més enllà el bucle és de l'app i el nucli només hi passa de tant en tant, que és just el que
aquesta porta existeix per no deixar triar sense saber-ho).

El comptador no distingeix una app que pinta un joc d'una que s'ha penjat; per això el que es talla és el
**torn** i no l'execució: una app pot viure hores. El yield només es fa si `lua_isyieldable` (fora d'una
crida C); si no, s'espera el proper punt segur. **Encara no hi ha** el matar amb `E_BUDGET` després de dos
avisos, ni el pressupost propi de la finestra d'alarma, ni el del prompt `lua`, ni el de les coroutines
que crea l'app, ni el dels ganxos que el nucli crida amb `lua_pcall` (docs/75, docs/90).

## El prompt com a intèrpret (`lua`)

`lua` a la shell converteix el prompt en una sessió de l'intèrpret: el mateix estat que rep una app, amb
**tota l'API de `30 · API`** (console, `fs`, `led`, `button`, `net`, `power`, `sys`, alarmes), però
sense app a sota. Serveix per programar i per fer proves sense escriure un fitxer i reiniciar.

- **Una línia alhora, i el nucli segueix girant** entre línies (cada línia arriba com qualsevol
  comanda): el botó, el llum i les alarmes continuen treballant mentre escrius. No hi ha cap bucle de
  C esperant el teclat.
- **S'hi pot fer de tot menys esperar una promesa.** No hi ha coroutine a sota, o sigui que
  `sys.delay(100):await()` no hi funciona: la resposta és un error d'una línia i la sessió continua.
  Per esperar, una app.
- **Cada línia es prova primer com a expressió** (`return <línia>`) i, si no compila així, com a
  instrucció. L'ordre és aquest i no el contrari perquè una crida és totes dues coses: `fs.data()`
  compila com a instrucció i no respondria res, quan el que vol qui ho escriu és el valor. Així
  `1 + 1` respon `2`, `fs.data()` respon el camí, `x = 21` cau al segon intent i no respon res, i una
  crida que no torna res (`print(1)`, `led.set(...)`) no afegeix cap línia de més. El valor d'una
  línia es mostra perquè a un prompt el valor **és** la resposta (31/10: la suite ho va enxampar amb
  `fs.data()` responent en blanc).
- **Un error no es menja la sessió**: surt `lua: prompt:1: …` i les variables que ja tenies hi són.
- **La sessió és una banda amb nom propi**: `sys.board` és el de la placa (o el del model que el
  emulador imita, docs/85), el seu `fs.data()` és `/data/lua` i la llista de `boards` no la mira ningú (no
  hi ha manifest). Un rellotge que hi armin no té app a qui tornar quan tanques la sessió.
- **Sortir**: `exit` (o `quit`), i el que la sessió havia pres —el llum— es deixa anar, com quan
  marxa una app (docs/75).
- **Mentre la sessió és oberta, l'intèrpret és seu**: una finestra d'alarma no pot arrencar (només
  n'hi ha un estat a la vegada), i el log ho diu.

## Determinisme

**La llavor la posa el nucli i ve del maquinari** (0.174.8). Fins ara la que hi havia era la de Lua mateix
—`setrandfunc` de `lmathlib.c` sembra amb `time(NULL)` i amb **l'adreça de l'estat**—, i en aquesta placa això
volia dir dues coses dolentes alhora: `time(NULL)` és **zero** fins que algú posa l'hora (aquest sistema no toca el
rellotge de newlib, el seu és `time.now()`), i l'adreça de l'estat és la mateixa a cada arrencada perquè el heap és
el heap. O sigui que **dues arrencades podien donar exactament la mateixa seqüència**, que és el pitjor que pot fer
un generador.

Ara, després d'obrir les llibreries, `open_libraries()` crida `math.randomseed(a, b)` amb **dues paraules de
`ISystem::random()`**: a la placa el generador del xip, amb la finestra d'ADC que explica docs/50; a l'emulador, el
rellotge de la màquina i el seu procés. I **una app que vulgui reproduir-se crida `math.randomseed(n)` i prou** —és
la porta de Lua i no se li ha tret res—, que és el que fa un cas de la suite que necessiti la mateixa pàgina dues
vegades.

**L'emulador té `--seed <n>`**: amb un número, la mateixa execució dues vegades (les mateixes xifres, el mateix
ordre d'una baralla, la mateixa llavor de l'intèrpret); sense número, cada execució és la seva. És el que fa servir
una prova que vulgui un resultat repetible sense tocar l'app. A la placa no hi ha aquest commutador: `set_random_seed`
hi és un no-res, perquè un firmware les seqüències del qual es poden fer repetir és un firmware previsible (docs/50).

## Errors

- Tota crida a Lua passa per `lua_pcall` amb un gestor de missatges que afegeix el `traceback`.
- Els errors del nucli són taules `{ code = "E_...", message = "..." }`; els errors del codi de
  l'app són el que llança l'app (normalment una cadena).
- Quan una app mor per un error, la consola rep el missatge i les primeres línies del traceback. **No
  hi ha pàgina d'error**: la consola és l'única que ho diu, també a la placa amb vidre (docs/30).
- Un error dins d'una finestra d'alarma **no** mata res més: es registra, es tanca la finestra i el
  primer pla continua.

## Una sola API

No hi ha versions d'API i el manifest no en declara cap. Hi ha una API i prou: si una crida canvia de
forma, canvia per a tothom alhora, i no hi ha un número que una app pugui comparar per quedar-se amb
la vella.

Quan arribi el moment de trencar alguna cosa, la manera serà afegir la crida nova al costat de la
vella, aguantar-les totes dues una temporada i treure la vella quan cap app de la imatge no la faci
servir. El camp `version` del manifest i el log són el que queda per saber què tenia instal·lat cada
app.

## Coses que l'usuari d'aquesta documentació ha de saber

- `string.pack`/`unpack` hi són, però una app no hauria de fer servir `struct` per llegir formats de
  fitxer grans: per això hi ha la finestra de lectura de `70 · Sistema de fitxers`.
- Concatenar en un bucle gasta memòria del pressupost. Per construir text llarg, `table.concat`.
- Cada coroutine nova costa una pila; una app no ha de fer-ne centes.
